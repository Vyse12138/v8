// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/feedback-vector.h"

#include "src/diagnostics/code-tracer.h"
#include "src/heap/local-factory-inl.h"
#include "src/ic/handler-configuration-inl.h"
#include "src/ic/ic-inl.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/feedback-vector-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/map-inl.h"
#include "src/objects/object-macros.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

FeedbackSlot FeedbackVectorSpec::AddSlot(FeedbackSlotKind kind) {
  int slot = slots();
  int entries_per_slot = FeedbackMetadata::GetSlotSize(kind);
  append(kind);
  for (int i = 1; i < entries_per_slot; i++) {
    append(FeedbackSlotKind::kInvalid);
  }
  return FeedbackSlot(slot);
}

FeedbackSlot FeedbackVectorSpec::AddTypeProfileSlot() {
  FeedbackSlot slot = AddSlot(FeedbackSlotKind::kTypeProfile);
  CHECK_EQ(FeedbackVectorSpec::kTypeProfileSlotIndex,
           FeedbackVector::GetIndex(slot));
  return slot;
}

bool FeedbackVectorSpec::HasTypeProfileSlot() const {
  FeedbackSlot slot =
      FeedbackVector::ToSlot(FeedbackVectorSpec::kTypeProfileSlotIndex);
  if (slots() <= slot.ToInt()) {
    return false;
  }
  return GetKind(slot) == FeedbackSlotKind::kTypeProfile;
}

static bool IsPropertyNameFeedback(MaybeObject feedback) {
  HeapObject heap_object;
  if (!feedback->GetHeapObjectIfStrong(&heap_object)) return false;
  if (heap_object.IsString()) {
    DCHECK(heap_object.IsInternalizedString());
    return true;
  }
  if (!heap_object.IsSymbol()) return false;
  Symbol symbol = Symbol::cast(heap_object);
  ReadOnlyRoots roots = symbol.GetReadOnlyRoots();
  return symbol != roots.uninitialized_symbol() &&
         symbol != roots.megamorphic_symbol();
}

std::ostream& operator<<(std::ostream& os, FeedbackSlotKind kind) {
  return os << FeedbackMetadata::Kind2String(kind);
}

FeedbackSlotKind FeedbackMetadata::GetKind(FeedbackSlot slot) const {
  int index = VectorICComputer::index(0, slot.ToInt());
  int data = get(index);
  return VectorICComputer::decode(data, slot.ToInt());
}

void FeedbackMetadata::SetKind(FeedbackSlot slot, FeedbackSlotKind kind) {
  int index = VectorICComputer::index(0, slot.ToInt());
  int data = get(index);
  int new_data = VectorICComputer::encode(data, slot.ToInt(), kind);
  set(index, new_data);
}

// static
template <typename LocalIsolate>
Handle<FeedbackMetadata> FeedbackMetadata::New(LocalIsolate* isolate,
                                               const FeedbackVectorSpec* spec) {
  auto* factory = isolate->factory();

  const int slot_count = spec == nullptr ? 0 : spec->slots();
  const int closure_feedback_cell_count =
      spec == nullptr ? 0 : spec->closure_feedback_cells();
  if (slot_count == 0 && closure_feedback_cell_count == 0) {
    return factory->empty_feedback_metadata();
  }
#ifdef DEBUG
  for (int i = 0; i < slot_count;) {
    DCHECK(spec);
    FeedbackSlotKind kind = spec->GetKind(FeedbackSlot(i));
    int entry_size = FeedbackMetadata::GetSlotSize(kind);
    for (int j = 1; j < entry_size; j++) {
      FeedbackSlotKind kind = spec->GetKind(FeedbackSlot(i + j));
      DCHECK_EQ(FeedbackSlotKind::kInvalid, kind);
    }
    i += entry_size;
  }
#endif

  Handle<FeedbackMetadata> metadata =
      factory->NewFeedbackMetadata(slot_count, closure_feedback_cell_count);

  // Initialize the slots. The raw data section has already been pre-zeroed in
  // NewFeedbackMetadata.
  for (int i = 0; i < slot_count; i++) {
    DCHECK(spec);
    FeedbackSlot slot(i);
    FeedbackSlotKind kind = spec->GetKind(slot);
    metadata->SetKind(slot, kind);
  }

  return metadata;
}

template Handle<FeedbackMetadata> FeedbackMetadata::New(
    Isolate* isolate, const FeedbackVectorSpec* spec);
template Handle<FeedbackMetadata> FeedbackMetadata::New(
    LocalIsolate* isolate, const FeedbackVectorSpec* spec);

bool FeedbackMetadata::SpecDiffersFrom(
    const FeedbackVectorSpec* other_spec) const {
  if (other_spec->slots() != slot_count()) {
    return true;
  }

  int slots = slot_count();
  for (int i = 0; i < slots;) {
    FeedbackSlot slot(i);
    FeedbackSlotKind kind = GetKind(slot);
    int entry_size = FeedbackMetadata::GetSlotSize(kind);

    if (kind != other_spec->GetKind(slot)) {
      return true;
    }
    i += entry_size;
  }
  return false;
}

const char* FeedbackMetadata::Kind2String(FeedbackSlotKind kind) {
  switch (kind) {
    case FeedbackSlotKind::kInvalid:
      return "Invalid";
    case FeedbackSlotKind::kCall:
      return "Call";
    case FeedbackSlotKind::kLoadProperty:
      return "LoadProperty";
    case FeedbackSlotKind::kLoadGlobalInsideTypeof:
      return "LoadGlobalInsideTypeof";
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
      return "LoadGlobalNotInsideTypeof";
    case FeedbackSlotKind::kLoadKeyed:
      return "LoadKeyed";
    case FeedbackSlotKind::kHasKeyed:
      return "HasKeyed";
    case FeedbackSlotKind::kStoreNamedSloppy:
      return "StoreNamedSloppy";
    case FeedbackSlotKind::kStoreNamedStrict:
      return "StoreNamedStrict";
    case FeedbackSlotKind::kStoreOwnNamed:
      return "StoreOwnNamed";
    case FeedbackSlotKind::kStoreGlobalSloppy:
      return "StoreGlobalSloppy";
    case FeedbackSlotKind::kStoreGlobalStrict:
      return "StoreGlobalStrict";
    case FeedbackSlotKind::kStoreKeyedSloppy:
      return "StoreKeyedSloppy";
    case FeedbackSlotKind::kStoreKeyedStrict:
      return "StoreKeyedStrict";
    case FeedbackSlotKind::kStoreInArrayLiteral:
      return "StoreInArrayLiteral";
    case FeedbackSlotKind::kBinaryOp:
      return "BinaryOp";
    case FeedbackSlotKind::kCompareOp:
      return "CompareOp";
    case FeedbackSlotKind::kStoreDataPropertyInLiteral:
      return "StoreDataPropertyInLiteral";
    case FeedbackSlotKind::kLiteral:
      return "Literal";
    case FeedbackSlotKind::kTypeProfile:
      return "TypeProfile";
    case FeedbackSlotKind::kForIn:
      return "ForIn";
    case FeedbackSlotKind::kInstanceOf:
      return "InstanceOf";
    case FeedbackSlotKind::kCloneObject:
      return "CloneObject";
    case FeedbackSlotKind::kKindsNumber:
      break;
  }
  UNREACHABLE();
}

bool FeedbackMetadata::HasTypeProfileSlot() const {
  FeedbackSlot slot =
      FeedbackVector::ToSlot(FeedbackVectorSpec::kTypeProfileSlotIndex);
  return slot.ToInt() < slot_count() &&
         GetKind(slot) == FeedbackSlotKind::kTypeProfile;
}

FeedbackSlotKind FeedbackVector::GetKind(FeedbackSlot slot) const {
  DCHECK(!is_empty());
  return metadata().GetKind(slot);
}

FeedbackSlot FeedbackVector::GetTypeProfileSlot() const {
  DCHECK(metadata().HasTypeProfileSlot());
  FeedbackSlot slot =
      FeedbackVector::ToSlot(FeedbackVectorSpec::kTypeProfileSlotIndex);
  DCHECK_EQ(FeedbackSlotKind::kTypeProfile, GetKind(slot));
  return slot;
}

// static
Handle<ClosureFeedbackCellArray> ClosureFeedbackCellArray::New(
    Isolate* isolate, Handle<SharedFunctionInfo> shared) {
  Factory* factory = isolate->factory();

  int num_feedback_cells =
      shared->feedback_metadata().closure_feedback_cell_count();

  Handle<ClosureFeedbackCellArray> feedback_cell_array =
      factory->NewClosureFeedbackCellArray(num_feedback_cells);

  for (int i = 0; i < num_feedback_cells; i++) {
    Handle<FeedbackCell> cell =
        factory->NewNoClosuresCell(factory->undefined_value());
    feedback_cell_array->set(i, *cell);
  }
  return feedback_cell_array;
}

// static
Handle<FeedbackVector> FeedbackVector::New(
    Isolate* isolate, Handle<SharedFunctionInfo> shared,
    Handle<ClosureFeedbackCellArray> closure_feedback_cell_array,
    IsCompiledScope* is_compiled_scope) {
  DCHECK(is_compiled_scope->is_compiled());
  Factory* factory = isolate->factory();

  Handle<FeedbackMetadata> feedback_metadata(shared->feedback_metadata(),
                                             isolate);
  const int slot_count = feedback_metadata->slot_count();

  Handle<FeedbackVector> vector =
      factory->NewFeedbackVector(shared, closure_feedback_cell_array);

  DCHECK_EQ(vector->length(), slot_count);

  DCHECK_EQ(vector->shared_function_info(), *shared);
  DCHECK_EQ(vector->optimization_marker(),
            FLAG_log_function_events ? OptimizationMarker::kLogFirstExecution
                                     : OptimizationMarker::kNone);
  // TODO(mythria): This might change if NCI code is installed on feedback
  // vector. Update this accordingly.
  DCHECK_EQ(vector->optimization_tier(), OptimizationTier::kNone);
  DCHECK_EQ(vector->invocation_count(), 0);
  DCHECK_EQ(vector->profiler_ticks(), 0);
  DCHECK(vector->maybe_optimized_code()->IsCleared());

  // Ensure we can skip the write barrier
  Handle<Object> uninitialized_sentinel = UninitializedSentinel(isolate);
  DCHECK_EQ(ReadOnlyRoots(isolate).uninitialized_symbol(),
            *uninitialized_sentinel);
  for (int i = 0; i < slot_count;) {
    FeedbackSlot slot(i);
    FeedbackSlotKind kind = feedback_metadata->GetKind(slot);
    int entry_size = FeedbackMetadata::GetSlotSize(kind);

    Object extra_value = *uninitialized_sentinel;
    switch (kind) {
      case FeedbackSlotKind::kLoadGlobalInsideTypeof:
      case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
      case FeedbackSlotKind::kStoreGlobalSloppy:
      case FeedbackSlotKind::kStoreGlobalStrict:
        vector->Set(slot, HeapObjectReference::ClearedValue(isolate),
                    SKIP_WRITE_BARRIER);
        break;
      case FeedbackSlotKind::kForIn:
      case FeedbackSlotKind::kCompareOp:
      case FeedbackSlotKind::kBinaryOp:
        vector->Set(slot, Smi::zero(), SKIP_WRITE_BARRIER);
        break;
      case FeedbackSlotKind::kLiteral:
        vector->Set(slot, Smi::zero(), SKIP_WRITE_BARRIER);
        break;
      case FeedbackSlotKind::kCall:
        vector->Set(slot, *uninitialized_sentinel, SKIP_WRITE_BARRIER);
        extra_value = Smi::zero();
        break;
      case FeedbackSlotKind::kCloneObject:
      case FeedbackSlotKind::kLoadProperty:
      case FeedbackSlotKind::kLoadKeyed:
      case FeedbackSlotKind::kHasKeyed:
      case FeedbackSlotKind::kStoreNamedSloppy:
      case FeedbackSlotKind::kStoreNamedStrict:
      case FeedbackSlotKind::kStoreOwnNamed:
      case FeedbackSlotKind::kStoreKeyedSloppy:
      case FeedbackSlotKind::kStoreKeyedStrict:
      case FeedbackSlotKind::kStoreInArrayLiteral:
      case FeedbackSlotKind::kStoreDataPropertyInLiteral:
      case FeedbackSlotKind::kTypeProfile:
      case FeedbackSlotKind::kInstanceOf:
        vector->Set(slot, *uninitialized_sentinel, SKIP_WRITE_BARRIER);
        break;

      case FeedbackSlotKind::kInvalid:
      case FeedbackSlotKind::kKindsNumber:
        UNREACHABLE();
        break;
    }
    for (int j = 1; j < entry_size; j++) {
      vector->Set(slot.WithOffset(j), extra_value, SKIP_WRITE_BARRIER);
    }
    i += entry_size;
  }

  Handle<FeedbackVector> result = Handle<FeedbackVector>::cast(vector);
  if (!isolate->is_best_effort_code_coverage() ||
      isolate->is_collecting_type_profile()) {
    AddToVectorsForProfilingTools(isolate, result);
  }
  return result;
}

namespace {

Handle<FeedbackVector> NewFeedbackVectorForTesting(
    Isolate* isolate, const FeedbackVectorSpec* spec) {
  Handle<FeedbackMetadata> metadata = FeedbackMetadata::New(isolate, spec);
  Handle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfoForBuiltin(
          isolate->factory()->empty_string(), Builtins::kIllegal);
  // Set the raw feedback metadata to circumvent checks that we are not
  // overwriting existing metadata.
  shared->set_raw_outer_scope_info_or_feedback_metadata(*metadata);
  Handle<ClosureFeedbackCellArray> closure_feedback_cell_array =
      ClosureFeedbackCellArray::New(isolate, shared);

  IsCompiledScope is_compiled_scope(shared->is_compiled_scope(isolate));
  return FeedbackVector::New(isolate, shared, closure_feedback_cell_array,
                             &is_compiled_scope);
}

}  // namespace

// static
Handle<FeedbackVector> FeedbackVector::NewWithOneBinarySlotForTesting(
    Zone* zone, Isolate* isolate) {
  FeedbackVectorSpec one_slot(zone);
  one_slot.AddBinaryOpICSlot();
  return NewFeedbackVectorForTesting(isolate, &one_slot);
}

// static
Handle<FeedbackVector> FeedbackVector::NewWithOneCompareSlotForTesting(
    Zone* zone, Isolate* isolate) {
  FeedbackVectorSpec one_slot(zone);
  one_slot.AddCompareICSlot();
  return NewFeedbackVectorForTesting(isolate, &one_slot);
}

// static
void FeedbackVector::AddToVectorsForProfilingTools(
    Isolate* isolate, Handle<FeedbackVector> vector) {
  DCHECK(!isolate->is_best_effort_code_coverage() ||
         isolate->is_collecting_type_profile());
  if (!vector->shared_function_info().IsSubjectToDebugging()) return;
  Handle<ArrayList> list = Handle<ArrayList>::cast(
      isolate->factory()->feedback_vectors_for_profiling_tools());
  list = ArrayList::Add(isolate, list, vector);
  isolate->SetFeedbackVectorsForProfilingTools(*list);
}

void FeedbackVector::SaturatingIncrementProfilerTicks() {
  int ticks = profiler_ticks();
  if (ticks < Smi::kMaxValue) set_profiler_ticks(ticks + 1);
}

// static
void FeedbackVector::SetOptimizedCode(Handle<FeedbackVector> vector,
                                      Handle<Code> code) {
  DCHECK(CodeKindIsOptimizedJSFunction(code->kind()));
  DCHECK(vector->optimization_tier() == OptimizationTier::kNone ||
         (vector->optimization_tier() == OptimizationTier::kMidTier &&
          code->kind() == CodeKind::TURBOFAN));
  DCHECK(vector->optimization_marker() == OptimizationMarker::kNone ||
         vector->optimization_marker() ==
             OptimizationMarker::kInOptimizationQueue);
  vector->set_maybe_optimized_code(HeapObjectReference::Weak(*code));
  int32_t state = vector->flags();
  state = OptimizationTierBits::update(state, GetTierForCodeKind(code->kind()));
  state = OptimizationMarkerBits::update(state, OptimizationMarker::kNone);
  vector->set_flags(state);
}

void FeedbackVector::ClearOptimizedCode() {
  DCHECK(has_optimized_code());
  DCHECK_NE(optimization_tier(), OptimizationTier::kNone);
  set_maybe_optimized_code(HeapObjectReference::ClearedValue(GetIsolate()));
  ClearOptimizationTier();
}

void FeedbackVector::ClearOptimizationMarker() {
  SetOptimizationMarker(OptimizationMarker::kNone);
}

void FeedbackVector::SetOptimizationMarker(OptimizationMarker marker) {
  int32_t state = flags();
  state = OptimizationMarkerBits::update(state, marker);
  set_flags(state);
}

void FeedbackVector::ClearOptimizationTier() {
  int32_t state = flags();
  state = OptimizationTierBits::update(state, OptimizationTier::kNone);
  set_flags(state);
}

void FeedbackVector::InitializeOptimizationState() {
  int32_t state = 0;
  state = OptimizationMarkerBits::update(
      state, FLAG_log_function_events ? OptimizationMarker::kLogFirstExecution
                                      : OptimizationMarker::kNone);
  state = OptimizationTierBits::update(state, OptimizationTier::kNone);
  set_flags(state);
}

void FeedbackVector::EvictOptimizedCodeMarkedForDeoptimization(
    SharedFunctionInfo shared, const char* reason) {
  MaybeObject slot = maybe_optimized_code();
  if (slot->IsCleared()) {
    ClearOptimizationTier();
    return;
  }

  Code code = Code::cast(slot->GetHeapObject());
  if (code.marked_for_deoptimization()) {
    Deoptimizer::TraceEvictFromOptimizedCodeCache(shared, reason);
    if (!code.deopt_already_counted()) {
      code.set_deopt_already_counted(true);
    }
    ClearOptimizedCode();
  }
}

bool FeedbackVector::ClearSlots(Isolate* isolate) {
  if (!shared_function_info().HasFeedbackMetadata()) return false;
  MaybeObject uninitialized_sentinel = MaybeObject::FromObject(
      FeedbackVector::RawUninitializedSentinel(isolate));

  bool feedback_updated = false;
  FeedbackMetadataIterator iter(metadata());
  while (iter.HasNext()) {
    FeedbackSlot slot = iter.Next();

    MaybeObject obj = Get(slot);
    if (obj != uninitialized_sentinel) {
      FeedbackNexus nexus(*this, slot);
      feedback_updated |= nexus.Clear();
    }
  }
  return feedback_updated;
}

void FeedbackVector::AssertNoLegacyTypes(MaybeObject object) {
#ifdef DEBUG
  HeapObject heap_object;
  if (object->GetHeapObject(&heap_object)) {
    // Instead of FixedArray, the Feedback and the Extra should contain
    // WeakFixedArrays. The only allowed FixedArray subtype is HashTable.
    DCHECK_IMPLIES(heap_object.IsFixedArray(), heap_object.IsHashTable());
  }
#endif
}

Handle<WeakFixedArray> FeedbackNexus::CreateArrayOfSize(int length) {
  Isolate* isolate = GetIsolate();
  Handle<WeakFixedArray> array = isolate->factory()->NewWeakFixedArray(length);
  return array;
}

void FeedbackNexus::ConfigureUninitialized() {
  Isolate* isolate = GetIsolate();
  switch (kind()) {
    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreGlobalStrict:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalInsideTypeof: {
      SetFeedback(HeapObjectReference::ClearedValue(isolate),
                  SKIP_WRITE_BARRIER);
      SetFeedbackExtra(*FeedbackVector::UninitializedSentinel(isolate),
                       SKIP_WRITE_BARRIER);
      break;
    }
    case FeedbackSlotKind::kCloneObject:
    case FeedbackSlotKind::kCall: {
      SetFeedback(*FeedbackVector::UninitializedSentinel(isolate),
                  SKIP_WRITE_BARRIER);
      SetFeedbackExtra(Smi::zero(), SKIP_WRITE_BARRIER);
      break;
    }
    case FeedbackSlotKind::kInstanceOf: {
      SetFeedback(*FeedbackVector::UninitializedSentinel(isolate),
                  SKIP_WRITE_BARRIER);
      break;
    }
    case FeedbackSlotKind::kStoreNamedSloppy:
    case FeedbackSlotKind::kStoreNamedStrict:
    case FeedbackSlotKind::kStoreKeyedSloppy:
    case FeedbackSlotKind::kStoreKeyedStrict:
    case FeedbackSlotKind::kStoreInArrayLiteral:
    case FeedbackSlotKind::kStoreOwnNamed:
    case FeedbackSlotKind::kLoadProperty:
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kHasKeyed:
    case FeedbackSlotKind::kStoreDataPropertyInLiteral: {
      SetFeedback(*FeedbackVector::UninitializedSentinel(isolate),
                  SKIP_WRITE_BARRIER);
      SetFeedbackExtra(*FeedbackVector::UninitializedSentinel(isolate),
                       SKIP_WRITE_BARRIER);
      break;
    }
    default:
      UNREACHABLE();
  }
}

bool FeedbackNexus::Clear() {
  bool feedback_updated = false;

  switch (kind()) {
    case FeedbackSlotKind::kTypeProfile:
      // We don't clear these kinds ever.
      break;

    case FeedbackSlotKind::kCompareOp:
    case FeedbackSlotKind::kForIn:
    case FeedbackSlotKind::kBinaryOp:
      // We don't clear these, either.
      break;

    case FeedbackSlotKind::kLiteral:
      SetFeedback(Smi::zero(), SKIP_WRITE_BARRIER);
      feedback_updated = true;
      break;

    case FeedbackSlotKind::kStoreNamedSloppy:
    case FeedbackSlotKind::kStoreNamedStrict:
    case FeedbackSlotKind::kStoreKeyedSloppy:
    case FeedbackSlotKind::kStoreKeyedStrict:
    case FeedbackSlotKind::kStoreInArrayLiteral:
    case FeedbackSlotKind::kStoreOwnNamed:
    case FeedbackSlotKind::kLoadProperty:
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kHasKeyed:
    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreGlobalStrict:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalInsideTypeof:
    case FeedbackSlotKind::kCall:
    case FeedbackSlotKind::kInstanceOf:
    case FeedbackSlotKind::kStoreDataPropertyInLiteral:
    case FeedbackSlotKind::kCloneObject:
      if (!IsCleared()) {
        ConfigureUninitialized();
        feedback_updated = true;
      }
      break;

    case FeedbackSlotKind::kInvalid:
    case FeedbackSlotKind::kKindsNumber:
      UNREACHABLE();
  }
  return feedback_updated;
}

bool FeedbackNexus::ConfigureMegamorphic() {
  DisallowHeapAllocation no_gc;
  Isolate* isolate = GetIsolate();
  MaybeObject sentinel =
      MaybeObject::FromObject(*FeedbackVector::MegamorphicSentinel(isolate));
  if (GetFeedback() != sentinel) {
    SetFeedback(sentinel, SKIP_WRITE_BARRIER);
    SetFeedbackExtra(HeapObjectReference::ClearedValue(isolate));
    return true;
  }

  return false;
}

bool FeedbackNexus::ConfigureMegamorphic(IcCheckType property_type) {
  DisallowHeapAllocation no_gc;
  Isolate* isolate = GetIsolate();
  bool changed = false;
  MaybeObject sentinel =
      MaybeObject::FromObject(*FeedbackVector::MegamorphicSentinel(isolate));
  if (GetFeedback() != sentinel) {
    SetFeedback(sentinel, SKIP_WRITE_BARRIER);
    changed = true;
  }

  Smi extra = Smi::FromInt(static_cast<int>(property_type));
  if (changed || GetFeedbackExtra() != MaybeObject::FromSmi(extra)) {
    SetFeedbackExtra(extra, SKIP_WRITE_BARRIER);
    changed = true;
  }
  return changed;
}

Map FeedbackNexus::GetFirstMap() const {
  FeedbackIterator it(this);
  if (!it.done()) {
    return it.map();
  }

  return Map();
}

InlineCacheState FeedbackNexus::ic_state() const {
  Isolate* isolate = GetIsolate();
  MaybeObject feedback = GetFeedback();

  switch (kind()) {
    case FeedbackSlotKind::kLiteral:
      if (feedback->IsSmi()) return UNINITIALIZED;
      return MONOMORPHIC;

    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreGlobalStrict:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalInsideTypeof: {
      if (feedback->IsSmi()) return MONOMORPHIC;

      DCHECK(feedback->IsWeakOrCleared());
      MaybeObject extra = GetFeedbackExtra();
      if (!feedback->IsCleared() ||
          extra != MaybeObject::FromObject(
                       *FeedbackVector::UninitializedSentinel(isolate))) {
        return MONOMORPHIC;
      }
      return UNINITIALIZED;
    }

    case FeedbackSlotKind::kStoreNamedSloppy:
    case FeedbackSlotKind::kStoreNamedStrict:
    case FeedbackSlotKind::kStoreKeyedSloppy:
    case FeedbackSlotKind::kStoreKeyedStrict:
    case FeedbackSlotKind::kStoreInArrayLiteral:
    case FeedbackSlotKind::kStoreOwnNamed:
    case FeedbackSlotKind::kLoadProperty:
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kHasKeyed: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      }
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::MegamorphicSentinel(isolate))) {
        return MEGAMORPHIC;
      }
      if (feedback->IsWeakOrCleared()) {
        // Don't check if the map is cleared.
        return MONOMORPHIC;
      }
      HeapObject heap_object;
      if (feedback->GetHeapObjectIfStrong(&heap_object)) {
        if (heap_object.IsWeakFixedArray()) {
          // Determine state purely by our structure, don't check if the maps
          // are cleared.
          return POLYMORPHIC;
        }
        if (heap_object.IsName()) {
          DCHECK(IsKeyedLoadICKind(kind()) || IsKeyedStoreICKind(kind()) ||
                 IsKeyedHasICKind(kind()));
          Object extra = GetFeedbackExtra()->GetHeapObjectAssumeStrong();
          WeakFixedArray extra_array = WeakFixedArray::cast(extra);
          return extra_array.length() > 2 ? POLYMORPHIC : MONOMORPHIC;
        }
      }
      UNREACHABLE();
    }
    case FeedbackSlotKind::kCall: {
      HeapObject heap_object;
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::MegamorphicSentinel(isolate))) {
        return GENERIC;
      } else if (feedback->IsWeakOrCleared()) {
        if (feedback->GetHeapObjectIfWeak(&heap_object)) {
          if (heap_object.IsFeedbackCell()) {
            return POLYMORPHIC;
          }
          CHECK(heap_object.IsJSFunction() || heap_object.IsJSBoundFunction());
        }
        return MONOMORPHIC;
      } else if (feedback->GetHeapObjectIfStrong(&heap_object) &&
                 heap_object.IsAllocationSite()) {
        return MONOMORPHIC;
      }

      CHECK_EQ(feedback, MaybeObject::FromObject(
                             *FeedbackVector::UninitializedSentinel(isolate)));
      return UNINITIALIZED;
    }
    case FeedbackSlotKind::kBinaryOp: {
      BinaryOperationHint hint = GetBinaryOperationFeedback();
      if (hint == BinaryOperationHint::kNone) {
        return UNINITIALIZED;
      } else if (hint == BinaryOperationHint::kAny) {
        return GENERIC;
      }

      return MONOMORPHIC;
    }
    case FeedbackSlotKind::kCompareOp: {
      CompareOperationHint hint = GetCompareOperationFeedback();
      if (hint == CompareOperationHint::kNone) {
        return UNINITIALIZED;
      } else if (hint == CompareOperationHint::kAny) {
        return GENERIC;
      }

      return MONOMORPHIC;
    }
    case FeedbackSlotKind::kForIn: {
      ForInHint hint = GetForInFeedback();
      if (hint == ForInHint::kNone) {
        return UNINITIALIZED;
      } else if (hint == ForInHint::kAny) {
        return GENERIC;
      }
      return MONOMORPHIC;
    }
    case FeedbackSlotKind::kInstanceOf: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      } else if (feedback ==
                 MaybeObject::FromObject(
                     *FeedbackVector::MegamorphicSentinel(isolate))) {
        return MEGAMORPHIC;
      }
      return MONOMORPHIC;
    }
    case FeedbackSlotKind::kStoreDataPropertyInLiteral: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      } else if (feedback->IsWeakOrCleared()) {
        // Don't check if the map is cleared.
        return MONOMORPHIC;
      }

      return MEGAMORPHIC;
    }
    case FeedbackSlotKind::kTypeProfile: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      }
      return MONOMORPHIC;
    }

    case FeedbackSlotKind::kCloneObject: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      }
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::MegamorphicSentinel(isolate))) {
        return MEGAMORPHIC;
      }
      if (feedback->IsWeakOrCleared()) {
        return MONOMORPHIC;
      }

      DCHECK(feedback->GetHeapObjectAssumeStrong().IsWeakFixedArray());
      return POLYMORPHIC;
    }

    case FeedbackSlotKind::kInvalid:
    case FeedbackSlotKind::kKindsNumber:
      UNREACHABLE();
  }
  return UNINITIALIZED;
}

void FeedbackNexus::ConfigurePropertyCellMode(Handle<PropertyCell> cell) {
  DCHECK(IsGlobalICKind(kind()));
  Isolate* isolate = GetIsolate();
  SetFeedback(HeapObjectReference::Weak(*cell));
  SetFeedbackExtra(*FeedbackVector::UninitializedSentinel(isolate),
                   SKIP_WRITE_BARRIER);
}

bool FeedbackNexus::ConfigureLexicalVarMode(int script_context_index,
                                            int context_slot_index,
                                            bool immutable) {
  DCHECK(IsGlobalICKind(kind()));
  DCHECK_LE(0, script_context_index);
  DCHECK_LE(0, context_slot_index);
  if (!ContextIndexBits::is_valid(script_context_index) ||
      !SlotIndexBits::is_valid(context_slot_index) ||
      !ImmutabilityBit::is_valid(immutable)) {
    return false;
  }
  int config = ContextIndexBits::encode(script_context_index) |
               SlotIndexBits::encode(context_slot_index) |
               ImmutabilityBit::encode(immutable);

  SetFeedback(Smi::From31BitPattern(config));
  Isolate* isolate = GetIsolate();
  SetFeedbackExtra(*FeedbackVector::UninitializedSentinel(isolate),
                   SKIP_WRITE_BARRIER);
  return true;
}

void FeedbackNexus::ConfigureHandlerMode(const MaybeObjectHandle& handler) {
  DCHECK(IsGlobalICKind(kind()));
  DCHECK(IC::IsHandler(*handler));
  SetFeedback(HeapObjectReference::ClearedValue(GetIsolate()));
  SetFeedbackExtra(*handler);
}

void FeedbackNexus::ConfigureCloneObject(Handle<Map> source_map,
                                         Handle<Map> result_map) {
  Isolate* isolate = GetIsolate();
  Handle<HeapObject> feedback;
  {
    MaybeObject maybe_feedback = GetFeedback();
    if (maybe_feedback->IsStrongOrWeak()) {
      feedback = handle(maybe_feedback->GetHeapObject(), isolate);
    } else {
      DCHECK(maybe_feedback->IsCleared());
    }
  }
  switch (ic_state()) {
    case UNINITIALIZED:
      // Cache the first map seen which meets the fast case requirements.
      SetFeedback(HeapObjectReference::Weak(*source_map));
      SetFeedbackExtra(*result_map);
      break;
    case MONOMORPHIC:
      if (feedback.is_null() || feedback.is_identical_to(source_map) ||
          Map::cast(*feedback).is_deprecated()) {
        SetFeedback(HeapObjectReference::Weak(*source_map));
        SetFeedbackExtra(*result_map);
      } else {
        // Transition to POLYMORPHIC.
        Handle<WeakFixedArray> array =
            CreateArrayOfSize(2 * kCloneObjectPolymorphicEntrySize);
        array->Set(0, HeapObjectReference::Weak(*feedback));
        array->Set(1, GetFeedbackExtra());
        array->Set(2, HeapObjectReference::Weak(*source_map));
        array->Set(3, MaybeObject::FromObject(*result_map));
        SetFeedback(*array);
        SetFeedbackExtra(HeapObjectReference::ClearedValue(isolate));
      }
      break;
    case POLYMORPHIC: {
      const int kMaxElements = FLAG_max_valid_polymorphic_map_count *
                               kCloneObjectPolymorphicEntrySize;
      Handle<WeakFixedArray> array = Handle<WeakFixedArray>::cast(feedback);
      int i = 0;
      for (; i < array->length(); i += kCloneObjectPolymorphicEntrySize) {
        MaybeObject feedback_map = array->Get(i);
        if (feedback_map->IsCleared()) break;
        Handle<Map> cached_map(Map::cast(feedback_map->GetHeapObject()),
                               isolate);
        if (cached_map.is_identical_to(source_map) ||
            cached_map->is_deprecated())
          break;
      }

      if (i >= array->length()) {
        if (i == kMaxElements) {
          // Transition to MEGAMORPHIC.
          MaybeObject sentinel = MaybeObject::FromObject(
              *FeedbackVector::MegamorphicSentinel(isolate));
          SetFeedback(sentinel, SKIP_WRITE_BARRIER);
          SetFeedbackExtra(HeapObjectReference::ClearedValue(isolate));
          break;
        }

        // Grow polymorphic feedback array.
        Handle<WeakFixedArray> new_array = CreateArrayOfSize(
            array->length() + kCloneObjectPolymorphicEntrySize);
        for (int j = 0; j < array->length(); ++j) {
          new_array->Set(j, array->Get(j));
        }
        SetFeedback(*new_array);
        array = new_array;
      }

      array->Set(i, HeapObjectReference::Weak(*source_map));
      array->Set(i + 1, MaybeObject::FromObject(*result_map));
      break;
    }

    default:
      UNREACHABLE();
  }
}

int FeedbackNexus::GetCallCount() {
  DCHECK(IsCallICKind(kind()));

  Object call_count = GetFeedbackExtra()->cast<Object>();
  CHECK(call_count.IsSmi());
  uint32_t value = static_cast<uint32_t>(Smi::ToInt(call_count));
  return CallCountField::decode(value);
}

void FeedbackNexus::SetSpeculationMode(SpeculationMode mode) {
  DCHECK(IsCallICKind(kind()));

  Object call_count = GetFeedbackExtra()->cast<Object>();
  CHECK(call_count.IsSmi());
  uint32_t count = static_cast<uint32_t>(Smi::ToInt(call_count));
  uint32_t value = CallCountField::encode(CallCountField::decode(count));
  int result = static_cast<int>(value | SpeculationModeField::encode(mode));
  SetFeedbackExtra(Smi::FromInt(result), SKIP_WRITE_BARRIER);
}

SpeculationMode FeedbackNexus::GetSpeculationMode() {
  DCHECK(IsCallICKind(kind()));

  Object call_count = GetFeedbackExtra()->cast<Object>();
  CHECK(call_count.IsSmi());
  uint32_t value = static_cast<uint32_t>(Smi::ToInt(call_count));
  return SpeculationModeField::decode(value);
}

float FeedbackNexus::ComputeCallFrequency() {
  DCHECK(IsCallICKind(kind()));

  double const invocation_count = vector().invocation_count();
  double const call_count = GetCallCount();
  if (invocation_count == 0.0) {  // Prevent division by 0.
    return 0.0f;
  }
  return static_cast<float>(call_count / invocation_count);
}

void FeedbackNexus::ConfigureMonomorphic(Handle<Name> name,
                                         Handle<Map> receiver_map,
                                         const MaybeObjectHandle& handler) {
  DCHECK(handler.is_null() || IC::IsHandler(*handler));
  if (kind() == FeedbackSlotKind::kStoreDataPropertyInLiteral) {
    SetFeedback(HeapObjectReference::Weak(*receiver_map));
    SetFeedbackExtra(*name);
  } else {
    if (name.is_null()) {
      SetFeedback(HeapObjectReference::Weak(*receiver_map));
      SetFeedbackExtra(*handler);
    } else {
      Handle<WeakFixedArray> array = CreateArrayOfSize(2);
      SetFeedback(*name);
      array->Set(0, HeapObjectReference::Weak(*receiver_map));
      array->Set(1, *handler);
      SetFeedbackExtra(*array);
    }
  }
}

void FeedbackNexus::ConfigurePolymorphic(
    Handle<Name> name, std::vector<MapAndHandler> const& maps_and_handlers) {
  int receiver_count = static_cast<int>(maps_and_handlers.size());
  DCHECK_GT(receiver_count, 1);
  Handle<WeakFixedArray> array = CreateArrayOfSize(receiver_count * 2);

  for (int current = 0; current < receiver_count; ++current) {
    Handle<Map> map = maps_and_handlers[current].first;
    array->Set(current * 2, HeapObjectReference::Weak(*map));
    MaybeObjectHandle handler = maps_and_handlers[current].second;
    DCHECK(IC::IsHandler(*handler));
    array->Set(current * 2 + 1, *handler);
  }
  if (name.is_null()) {
    SetFeedback(*array);
    SetFeedbackExtra(*FeedbackVector::UninitializedSentinel(GetIsolate()),
                     SKIP_WRITE_BARRIER);
  } else {
    SetFeedback(*name);
    SetFeedbackExtra(*array);
  }
}

int FeedbackNexus::ExtractMaps(MapHandles* maps) const {
  DisallowHeapAllocation no_gc;
  Isolate* isolate = GetIsolate();
  int found = 0;
  for (FeedbackIterator it(this); !it.done(); it.Advance()) {
    maps->push_back(handle(it.map(), isolate));
    found++;
  }

  return found;
}

int FeedbackNexus::ExtractMapsAndFeedback(
    std::vector<MapAndFeedback>* maps_and_feedback) const {
  DisallowHeapAllocation no_gc;
  Isolate* isolate = GetIsolate();
  int found = 0;

  for (FeedbackIterator it(this); !it.done(); it.Advance()) {
    Handle<Map> map = handle(it.map(), isolate);
    MaybeObject maybe_handler = it.handler();
    if (!maybe_handler->IsCleared()) {
      DCHECK(IC::IsHandler(maybe_handler) ||
             IsStoreDataPropertyInLiteralKind(kind()));
      MaybeObjectHandle handler = handle(maybe_handler, isolate);
      maps_and_feedback->push_back(MapAndHandler(map, handler));
      found++;
    }
  }

  return found;
}

int FeedbackNexus::ExtractMapsAndHandlers(
    std::vector<MapAndHandler>* maps_and_handlers,
    bool try_update_deprecated) const {
  DCHECK(!IsStoreDataPropertyInLiteralKind(kind()));
  DisallowHeapAllocation no_gc;
  Isolate* isolate = GetIsolate();
  int found = 0;

  for (FeedbackIterator it(this); !it.done(); it.Advance()) {
    Handle<Map> map = handle(it.map(), isolate);
    MaybeObject maybe_handler = it.handler();
    if (!maybe_handler->IsCleared()) {
      DCHECK(IC::IsHandler(maybe_handler));
      MaybeObjectHandle handler = handle(maybe_handler, isolate);
      if (try_update_deprecated &&
          !Map::TryUpdate(isolate, map).ToHandle(&map)) {
        continue;
      }
      maps_and_handlers->push_back(MapAndHandler(map, handler));
      found++;
    }
  }

  return found;
}

MaybeObjectHandle FeedbackNexus::FindHandlerForMap(Handle<Map> map) const {
  DCHECK(!IsStoreInArrayLiteralICKind(kind()));

  for (FeedbackIterator it(this); !it.done(); it.Advance()) {
    if (it.map() == *map && !it.handler()->IsCleared()) {
      return handle(it.handler(), GetIsolate());
    }
  }
  return MaybeObjectHandle();
}

Name FeedbackNexus::GetName() const {
  if (IsKeyedStoreICKind(kind()) || IsKeyedLoadICKind(kind()) ||
      IsKeyedHasICKind(kind())) {
    MaybeObject feedback = GetFeedback();
    if (IsPropertyNameFeedback(feedback)) {
      return Name::cast(feedback->GetHeapObjectAssumeStrong());
    }
  }
  if (IsStoreDataPropertyInLiteralKind(kind())) {
    MaybeObject extra = GetFeedbackExtra();
    if (IsPropertyNameFeedback(extra)) {
      return Name::cast(extra->GetHeapObjectAssumeStrong());
    }
  }
  return Name();
}

KeyedAccessLoadMode FeedbackNexus::GetKeyedAccessLoadMode() const {
  DCHECK(IsKeyedLoadICKind(kind()) || IsKeyedHasICKind(kind()));

  if (GetKeyType() == PROPERTY) return STANDARD_LOAD;

  std::vector<MapAndHandler> maps_and_handlers;
  ExtractMapsAndHandlers(&maps_and_handlers);
  for (MapAndHandler map_and_handler : maps_and_handlers) {
    KeyedAccessLoadMode mode =
        LoadHandler::GetKeyedAccessLoadMode(*map_and_handler.second);
    if (mode != STANDARD_LOAD) return mode;
  }

  return STANDARD_LOAD;
}

namespace {

bool BuiltinHasKeyedAccessStoreMode(int builtin_index) {
  DCHECK(Builtins::IsBuiltinId(builtin_index));
  switch (builtin_index) {
    case Builtins::kKeyedStoreIC_SloppyArguments_Standard:
    case Builtins::kKeyedStoreIC_SloppyArguments_GrowNoTransitionHandleCOW:
    case Builtins::kKeyedStoreIC_SloppyArguments_NoTransitionIgnoreOOB:
    case Builtins::kKeyedStoreIC_SloppyArguments_NoTransitionHandleCOW:
    case Builtins::kStoreFastElementIC_Standard:
    case Builtins::kStoreFastElementIC_GrowNoTransitionHandleCOW:
    case Builtins::kStoreFastElementIC_NoTransitionIgnoreOOB:
    case Builtins::kStoreFastElementIC_NoTransitionHandleCOW:
    case Builtins::kElementsTransitionAndStore_Standard:
    case Builtins::kElementsTransitionAndStore_GrowNoTransitionHandleCOW:
    case Builtins::kElementsTransitionAndStore_NoTransitionIgnoreOOB:
    case Builtins::kElementsTransitionAndStore_NoTransitionHandleCOW:
      return true;
    default:
      return false;
  }
  UNREACHABLE();
}

KeyedAccessStoreMode KeyedAccessStoreModeForBuiltin(int builtin_index) {
  DCHECK(BuiltinHasKeyedAccessStoreMode(builtin_index));
  switch (builtin_index) {
    case Builtins::kKeyedStoreIC_SloppyArguments_Standard:
    case Builtins::kStoreFastElementIC_Standard:
    case Builtins::kElementsTransitionAndStore_Standard:
      return STANDARD_STORE;
    case Builtins::kKeyedStoreIC_SloppyArguments_GrowNoTransitionHandleCOW:
    case Builtins::kStoreFastElementIC_GrowNoTransitionHandleCOW:
    case Builtins::kElementsTransitionAndStore_GrowNoTransitionHandleCOW:
      return STORE_AND_GROW_HANDLE_COW;
    case Builtins::kKeyedStoreIC_SloppyArguments_NoTransitionIgnoreOOB:
    case Builtins::kStoreFastElementIC_NoTransitionIgnoreOOB:
    case Builtins::kElementsTransitionAndStore_NoTransitionIgnoreOOB:
      return STORE_IGNORE_OUT_OF_BOUNDS;
    case Builtins::kKeyedStoreIC_SloppyArguments_NoTransitionHandleCOW:
    case Builtins::kStoreFastElementIC_NoTransitionHandleCOW:
    case Builtins::kElementsTransitionAndStore_NoTransitionHandleCOW:
      return STORE_HANDLE_COW;
    default:
      UNREACHABLE();
  }
}

}  // namespace

KeyedAccessStoreMode FeedbackNexus::GetKeyedAccessStoreMode() const {
  DCHECK(IsKeyedStoreICKind(kind()) || IsStoreInArrayLiteralICKind(kind()) ||
         IsStoreDataPropertyInLiteralKind(kind()));
  KeyedAccessStoreMode mode = STANDARD_STORE;

  if (GetKeyType() == PROPERTY) return mode;

  std::vector<MapAndHandler> maps_and_handlers;
  ExtractMapsAndHandlers(&maps_and_handlers);
  for (const MapAndHandler& map_and_handler : maps_and_handlers) {
    const MaybeObjectHandle maybe_code_handler = map_and_handler.second;
    // The first handler that isn't the slow handler will have the bits we need.
    Handle<Code> handler;
    if (maybe_code_handler.object()->IsStoreHandler()) {
      Handle<StoreHandler> data_handler =
          Handle<StoreHandler>::cast(maybe_code_handler.object());

      if ((data_handler->smi_handler()).IsSmi()) {
        // Decode the KeyedAccessStoreMode information from the Handler.
        mode = StoreHandler::GetKeyedAccessStoreMode(
            MaybeObject::FromObject(data_handler->smi_handler()));
        if (mode != STANDARD_STORE) return mode;
        continue;
      } else {
        handler = handle(Code::cast(data_handler->smi_handler()),
                         vector().GetIsolate());
      }

    } else if (maybe_code_handler.object()->IsSmi()) {
      // Skip for Proxy Handlers.
      if (*(maybe_code_handler.object()) ==
          *StoreHandler::StoreProxy(GetIsolate()))
        continue;
      // Decode the KeyedAccessStoreMode information from the Handler.
      mode = StoreHandler::GetKeyedAccessStoreMode(*maybe_code_handler);
      if (mode != STANDARD_STORE) return mode;
      continue;
    } else {
      // Element store without prototype chain check.
      handler = Handle<Code>::cast(maybe_code_handler.object());
    }

    if (handler->is_builtin()) {
      const int builtin_index = handler->builtin_index();
      if (!BuiltinHasKeyedAccessStoreMode(builtin_index)) continue;

      mode = KeyedAccessStoreModeForBuiltin(builtin_index);
      break;
    }
  }

  return mode;
}

IcCheckType FeedbackNexus::GetKeyType() const {
  DCHECK(IsKeyedStoreICKind(kind()) || IsKeyedLoadICKind(kind()) ||
         IsStoreInArrayLiteralICKind(kind()) || IsKeyedHasICKind(kind()) ||
         IsStoreDataPropertyInLiteralKind(kind()));
  MaybeObject feedback = GetFeedback();
  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::MegamorphicSentinel(GetIsolate()))) {
    return static_cast<IcCheckType>(
        Smi::ToInt(GetFeedbackExtra()->cast<Object>()));
  }
  MaybeObject maybe_name =
      IsStoreDataPropertyInLiteralKind(kind()) ? GetFeedbackExtra() : feedback;
  return IsPropertyNameFeedback(maybe_name) ? PROPERTY : ELEMENT;
}

BinaryOperationHint FeedbackNexus::GetBinaryOperationFeedback() const {
  DCHECK_EQ(kind(), FeedbackSlotKind::kBinaryOp);
  int feedback = GetFeedback().ToSmi().value();
  return BinaryOperationHintFromFeedback(feedback);
}

CompareOperationHint FeedbackNexus::GetCompareOperationFeedback() const {
  DCHECK_EQ(kind(), FeedbackSlotKind::kCompareOp);
  int feedback = GetFeedback().ToSmi().value();
  return CompareOperationHintFromFeedback(feedback);
}

ForInHint FeedbackNexus::GetForInFeedback() const {
  DCHECK_EQ(kind(), FeedbackSlotKind::kForIn);
  int feedback = GetFeedback().ToSmi().value();
  return ForInHintFromFeedback(static_cast<ForInFeedback>(feedback));
}

MaybeHandle<JSObject> FeedbackNexus::GetConstructorFeedback() const {
  DCHECK_EQ(kind(), FeedbackSlotKind::kInstanceOf);
  Isolate* isolate = GetIsolate();
  MaybeObject feedback = GetFeedback();
  HeapObject heap_object;
  if (feedback->GetHeapObjectIfWeak(&heap_object)) {
    return handle(JSObject::cast(heap_object), isolate);
  }
  return MaybeHandle<JSObject>();
}

namespace {

bool InList(Handle<ArrayList> types, Handle<String> type) {
  for (int i = 0; i < types->Length(); i++) {
    Object obj = types->Get(i);
    if (String::cast(obj).Equals(*type)) {
      return true;
    }
  }
  return false;
}
}  // anonymous namespace

void FeedbackNexus::Collect(Handle<String> type, int position) {
  DCHECK(IsTypeProfileKind(kind()));
  DCHECK_GE(position, 0);
  Isolate* isolate = GetIsolate();

  MaybeObject const feedback = GetFeedback();

  // Map source position to collection of types
  Handle<SimpleNumberDictionary> types;

  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::UninitializedSentinel(isolate))) {
    types = SimpleNumberDictionary::New(isolate, 1);
  } else {
    types = handle(
        SimpleNumberDictionary::cast(feedback->GetHeapObjectAssumeStrong()),
        isolate);
  }

  Handle<ArrayList> position_specific_types;

  InternalIndex entry = types->FindEntry(isolate, position);
  if (entry.is_not_found()) {
    position_specific_types = ArrayList::New(isolate, 1);
    types = SimpleNumberDictionary::Set(
        isolate, types, position,
        ArrayList::Add(isolate, position_specific_types, type));
  } else {
    DCHECK(types->ValueAt(entry).IsArrayList());
    position_specific_types =
        handle(ArrayList::cast(types->ValueAt(entry)), isolate);
    if (!InList(position_specific_types, type)) {  // Add type
      types = SimpleNumberDictionary::Set(
          isolate, types, position,
          ArrayList::Add(isolate, position_specific_types, type));
    }
  }
  SetFeedback(*types);
}

std::vector<int> FeedbackNexus::GetSourcePositions() const {
  DCHECK(IsTypeProfileKind(kind()));
  std::vector<int> source_positions;
  Isolate* isolate = GetIsolate();

  MaybeObject const feedback = GetFeedback();

  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::UninitializedSentinel(isolate))) {
    return source_positions;
  }

  Handle<SimpleNumberDictionary> types(
      SimpleNumberDictionary::cast(feedback->GetHeapObjectAssumeStrong()),
      isolate);

  for (int index = SimpleNumberDictionary::kElementsStartIndex;
       index < types->length(); index += SimpleNumberDictionary::kEntrySize) {
    int key_index = index + SimpleNumberDictionary::kEntryKeyIndex;
    Object key = types->get(key_index);
    if (key.IsSmi()) {
      int position = Smi::cast(key).value();
      source_positions.push_back(position);
    }
  }
  return source_positions;
}

std::vector<Handle<String>> FeedbackNexus::GetTypesForSourcePositions(
    uint32_t position) const {
  DCHECK(IsTypeProfileKind(kind()));
  Isolate* isolate = GetIsolate();

  MaybeObject const feedback = GetFeedback();
  std::vector<Handle<String>> types_for_position;
  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::UninitializedSentinel(isolate))) {
    return types_for_position;
  }

  Handle<SimpleNumberDictionary> types(
      SimpleNumberDictionary::cast(feedback->GetHeapObjectAssumeStrong()),
      isolate);

  InternalIndex entry = types->FindEntry(isolate, position);
  if (entry.is_not_found()) return types_for_position;

  DCHECK(types->ValueAt(entry).IsArrayList());
  Handle<ArrayList> position_specific_types =
      Handle<ArrayList>(ArrayList::cast(types->ValueAt(entry)), isolate);
  for (int i = 0; i < position_specific_types->Length(); i++) {
    Object t = position_specific_types->Get(i);
    types_for_position.push_back(Handle<String>(String::cast(t), isolate));
  }

  return types_for_position;
}

namespace {

Handle<JSObject> ConvertToJSObject(Isolate* isolate,
                                   Handle<SimpleNumberDictionary> feedback) {
  Handle<JSObject> type_profile =
      isolate->factory()->NewJSObject(isolate->object_function());

  for (int index = SimpleNumberDictionary::kElementsStartIndex;
       index < feedback->length();
       index += SimpleNumberDictionary::kEntrySize) {
    int key_index = index + SimpleNumberDictionary::kEntryKeyIndex;
    Object key = feedback->get(key_index);
    if (key.IsSmi()) {
      int value_index = index + SimpleNumberDictionary::kEntryValueIndex;

      Handle<ArrayList> position_specific_types(
          ArrayList::cast(feedback->get(value_index)), isolate);

      int position = Smi::ToInt(key);
      JSObject::AddDataElement(
          type_profile, position,
          isolate->factory()->NewJSArrayWithElements(
              ArrayList::Elements(isolate, position_specific_types)),
          PropertyAttributes::NONE);
    }
  }
  return type_profile;
}
}  // namespace

JSObject FeedbackNexus::GetTypeProfile() const {
  DCHECK(IsTypeProfileKind(kind()));
  Isolate* isolate = GetIsolate();

  MaybeObject const feedback = GetFeedback();

  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::UninitializedSentinel(isolate))) {
    return *isolate->factory()->NewJSObject(isolate->object_function());
  }

  return *ConvertToJSObject(isolate,
                            handle(SimpleNumberDictionary::cast(
                                       feedback->GetHeapObjectAssumeStrong()),
                                   isolate));
}

void FeedbackNexus::ResetTypeProfile() {
  DCHECK(IsTypeProfileKind(kind()));
  SetFeedback(*FeedbackVector::UninitializedSentinel(GetIsolate()));
}

FeedbackIterator::FeedbackIterator(const FeedbackNexus* nexus)
    : done_(false), index_(-1), state_(kOther) {
  DCHECK(IsLoadICKind(nexus->kind()) ||
         IsStoreICKind(nexus->kind()) | IsKeyedLoadICKind(nexus->kind()) ||
         IsKeyedStoreICKind(nexus->kind()) || IsStoreOwnICKind(nexus->kind()) ||
         IsStoreDataPropertyInLiteralKind(nexus->kind()) ||
         IsStoreInArrayLiteralICKind(nexus->kind()) ||
         IsKeyedHasICKind(nexus->kind()));

  DisallowHeapAllocation no_gc;
  Isolate* isolate = nexus->GetIsolate();
  MaybeObject feedback = nexus->GetFeedback();
  bool is_named_feedback = IsPropertyNameFeedback(feedback);
  HeapObject heap_object;

  if ((feedback->GetHeapObjectIfStrong(&heap_object) &&
       heap_object.IsWeakFixedArray()) ||
      is_named_feedback) {
    index_ = 0;
    state_ = kPolymorphic;
    heap_object = feedback->GetHeapObjectAssumeStrong();
    if (is_named_feedback) {
      polymorphic_feedback_ =
          handle(WeakFixedArray::cast(
                     nexus->GetFeedbackExtra()->GetHeapObjectAssumeStrong()),
                 isolate);
    } else {
      polymorphic_feedback_ =
          handle(WeakFixedArray::cast(heap_object), isolate);
    }
    AdvancePolymorphic();
  } else if (feedback->GetHeapObjectIfWeak(&heap_object)) {
    state_ = kMonomorphic;
    MaybeObject handler = nexus->GetFeedbackExtra();
    map_ = Map::cast(heap_object);
    handler_ = handler;
  } else {
    done_ = true;
  }
}

void FeedbackIterator::Advance() {
  CHECK(!done_);

  if (state_ == kMonomorphic) {
    done_ = true;
    return;
  }

  CHECK_EQ(state_, kPolymorphic);
  AdvancePolymorphic();
}

void FeedbackIterator::AdvancePolymorphic() {
  CHECK(!done_);
  CHECK_EQ(state_, kPolymorphic);
  int length = polymorphic_feedback_->length();
  HeapObject heap_object;

  while (index_ < length) {
    if (polymorphic_feedback_->Get(index_)->GetHeapObjectIfWeak(&heap_object)) {
      MaybeObject handler = polymorphic_feedback_->Get(index_ + kHandlerOffset);
      map_ = Map::cast(heap_object);
      handler_ = handler;
      index_ += kEntrySize;
      return;
    }
    index_ += kEntrySize;
  }

  CHECK_EQ(index_, length);
  done_ = true;
}
}  // namespace internal
}  // namespace v8
