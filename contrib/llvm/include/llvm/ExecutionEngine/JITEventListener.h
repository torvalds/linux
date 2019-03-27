//===- JITEventListener.h - Exposes events from JIT compilation -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the JITEventListener interface, which lets users get
// callbacks when significant events happen during the JIT compilation process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITEVENTLISTENER_H
#define LLVM_EXECUTIONENGINE_JITEVENTLISTENER_H

#include "llvm-c/ExecutionEngine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/CBindingWrapping.h"
#include <cstdint>
#include <vector>

namespace llvm {

class IntelJITEventsWrapper;
class MachineFunction;
class OProfileWrapper;

namespace object {

class ObjectFile;

} // end namespace object

/// JITEventListener - Abstract interface for use by the JIT to notify clients
/// about significant events during compilation. For example, to notify
/// profilers and debuggers that need to know where functions have been emitted.
///
/// The default implementation of each method does nothing.
class JITEventListener {
public:
  using ObjectKey = uint64_t;

  JITEventListener() = default;
  virtual ~JITEventListener() = default;

  /// notifyObjectLoaded - Called after an object has had its sections allocated
  /// and addresses assigned to all symbols. Note: Section memory will not have
  /// been relocated yet. notifyFunctionLoaded will not be called for
  /// individual functions in the object.
  ///
  /// ELF-specific information
  /// The ObjectImage contains the generated object image
  /// with section headers updated to reflect the address at which sections
  /// were loaded and with relocations performed in-place on debug sections.
  virtual void notifyObjectLoaded(ObjectKey K, const object::ObjectFile &Obj,
                                  const RuntimeDyld::LoadedObjectInfo &L) {}

  /// notifyFreeingObject - Called just before the memory associated with
  /// a previously emitted object is released.
  virtual void notifyFreeingObject(ObjectKey K) {}

  // Get a pointe to the GDB debugger registration listener.
  static JITEventListener *createGDBRegistrationListener();

#if LLVM_USE_INTEL_JITEVENTS
  // Construct an IntelJITEventListener
  static JITEventListener *createIntelJITEventListener();

  // Construct an IntelJITEventListener with a test Intel JIT API implementation
  static JITEventListener *createIntelJITEventListener(
                                      IntelJITEventsWrapper* AlternativeImpl);
#else
  static JITEventListener *createIntelJITEventListener() { return nullptr; }

  static JITEventListener *createIntelJITEventListener(
                                      IntelJITEventsWrapper* AlternativeImpl) {
    return nullptr;
  }
#endif // USE_INTEL_JITEVENTS

#if LLVM_USE_OPROFILE
  // Construct an OProfileJITEventListener
  static JITEventListener *createOProfileJITEventListener();

  // Construct an OProfileJITEventListener with a test opagent implementation
  static JITEventListener *createOProfileJITEventListener(
                                      OProfileWrapper* AlternativeImpl);
#else
  static JITEventListener *createOProfileJITEventListener() { return nullptr; }

  static JITEventListener *createOProfileJITEventListener(
                                      OProfileWrapper* AlternativeImpl) {
    return nullptr;
  }
#endif // USE_OPROFILE

#if LLVM_USE_PERF
  static JITEventListener *createPerfJITEventListener();
#else
  static JITEventListener *createPerfJITEventListener()
  {
    return nullptr;
  }
#endif // USE_PERF

private:
  virtual void anchor();
};

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(JITEventListener, LLVMJITEventListenerRef)

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITEVENTLISTENER_H
