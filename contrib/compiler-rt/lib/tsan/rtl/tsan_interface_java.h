//===-- tsan_interface_java.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Interface for verification of Java or mixed Java/C++ programs.
// The interface is intended to be used from within a JVM and notify TSan
// about such events like Java locks and GC memory compaction.
//
// For plain memory accesses and function entry/exit a JVM is intended to use
// C++ interfaces: __tsan_readN/writeN and __tsan_func_enter/exit.
//
// For volatile memory accesses and atomic operations JVM is intended to use
// standard atomics API: __tsan_atomicN_load/store/etc.
//
// For usage examples see lit_tests/java_*.cc
//===----------------------------------------------------------------------===//
#ifndef TSAN_INTERFACE_JAVA_H
#define TSAN_INTERFACE_JAVA_H

#ifndef INTERFACE_ATTRIBUTE
# define INTERFACE_ATTRIBUTE __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long jptr;  // NOLINT

// Must be called before any other callback from Java.
void __tsan_java_init(jptr heap_begin, jptr heap_size) INTERFACE_ATTRIBUTE;
// Must be called when the application exits.
// Not necessary the last callback (concurrently running threads are OK).
// Returns exit status or 0 if tsan does not want to override it.
int  __tsan_java_fini() INTERFACE_ATTRIBUTE;

// Callback for memory allocations.
// May be omitted for allocations that are not subject to data races
// nor contain synchronization objects (e.g. String).
void __tsan_java_alloc(jptr ptr, jptr size) INTERFACE_ATTRIBUTE;
// Callback for memory free.
// Can be aggregated for several objects (preferably).
void __tsan_java_free(jptr ptr, jptr size) INTERFACE_ATTRIBUTE;
// Callback for memory move by GC.
// Can be aggregated for several objects (preferably).
// The ranges can overlap.
void __tsan_java_move(jptr src, jptr dst, jptr size) INTERFACE_ATTRIBUTE;
// This function must be called on the finalizer thread
// before executing a batch of finalizers.
// It ensures necessary synchronization between
// java object creation and finalization.
void __tsan_java_finalize() INTERFACE_ATTRIBUTE;
// Finds the first allocated memory block in the [*from_ptr, to) range, saves
// its address in *from_ptr and returns its size. Returns 0 if there are no
// allocated memory blocks in the range.
jptr __tsan_java_find(jptr *from_ptr, jptr to) INTERFACE_ATTRIBUTE;

// Mutex lock.
// Addr is any unique address associated with the mutex.
// Can be called on recursive reentry.
void __tsan_java_mutex_lock(jptr addr) INTERFACE_ATTRIBUTE;
// Mutex unlock.
void __tsan_java_mutex_unlock(jptr addr) INTERFACE_ATTRIBUTE;
// Mutex read lock.
void __tsan_java_mutex_read_lock(jptr addr) INTERFACE_ATTRIBUTE;
// Mutex read unlock.
void __tsan_java_mutex_read_unlock(jptr addr) INTERFACE_ATTRIBUTE;
// Recursive mutex lock, intended for handling of Object.wait().
// The 'rec' value must be obtained from the previous
// __tsan_java_mutex_unlock_rec().
void __tsan_java_mutex_lock_rec(jptr addr, int rec) INTERFACE_ATTRIBUTE;
// Recursive mutex unlock, intended for handling of Object.wait().
// The return value says how many times this thread called lock()
// w/o a pairing unlock() (i.e. how many recursive levels it unlocked).
// It must be passed back to __tsan_java_mutex_lock_rec() to restore
// the same recursion level.
int __tsan_java_mutex_unlock_rec(jptr addr) INTERFACE_ATTRIBUTE;

// Raw acquire/release primitives.
// Can be used to establish happens-before edges on volatile/final fields,
// in atomic operations, etc. release_store is the same as release, but it
// breaks release sequence on addr (see C++ standard 1.10/7 for details).
void __tsan_java_acquire(jptr addr) INTERFACE_ATTRIBUTE;
void __tsan_java_release(jptr addr) INTERFACE_ATTRIBUTE;
void __tsan_java_release_store(jptr addr) INTERFACE_ATTRIBUTE;

#ifdef __cplusplus
}  // extern "C"
#endif

#undef INTERFACE_ATTRIBUTE

#endif  // #ifndef TSAN_INTERFACE_JAVA_H
