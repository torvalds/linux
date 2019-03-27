//===-- scudo_platform.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Scudo platform specific definitions.
/// TODO(kostyak): add tests for the compile time defines.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_PLATFORM_H_
#define SCUDO_PLATFORM_H_

#include "sanitizer_common/sanitizer_allocator.h"

#if !SANITIZER_LINUX && !SANITIZER_FUCHSIA
# error "The Scudo hardened allocator is not supported on this platform."
#endif

#define SCUDO_TSD_EXCLUSIVE_SUPPORTED (!SANITIZER_ANDROID && !SANITIZER_FUCHSIA)

#ifndef SCUDO_TSD_EXCLUSIVE
// SCUDO_TSD_EXCLUSIVE wasn't defined, use a default TSD model for the platform.
# if SANITIZER_ANDROID || SANITIZER_FUCHSIA
// Android and Fuchsia use a pool of TSDs shared between threads.
#  define SCUDO_TSD_EXCLUSIVE 0
# elif SANITIZER_LINUX && !SANITIZER_ANDROID
// Non-Android Linux use an exclusive TSD per thread.
#  define SCUDO_TSD_EXCLUSIVE 1
# else
#  error "No default TSD model defined for this platform."
# endif  // SANITIZER_ANDROID || SANITIZER_FUCHSIA
#endif  // SCUDO_TSD_EXCLUSIVE

// If the exclusive TSD model is chosen, make sure the platform supports it.
#if SCUDO_TSD_EXCLUSIVE && !SCUDO_TSD_EXCLUSIVE_SUPPORTED
# error "The exclusive TSD model is not supported on this platform."
#endif

// Maximum number of TSDs that can be created for the Shared model.
#ifndef SCUDO_SHARED_TSD_POOL_SIZE
# if SANITIZER_ANDROID
#  define SCUDO_SHARED_TSD_POOL_SIZE 2U
# else
#  define SCUDO_SHARED_TSD_POOL_SIZE 32U
# endif  // SANITIZER_ANDROID
#endif  // SCUDO_SHARED_TSD_POOL_SIZE

// The following allows the public interface functions to be disabled.
#ifndef SCUDO_CAN_USE_PUBLIC_INTERFACE
# define SCUDO_CAN_USE_PUBLIC_INTERFACE 1
#endif

// Hooks in the allocation & deallocation paths can become a security concern if
// implemented improperly, or if overwritten by an attacker. Use with caution.
#ifndef SCUDO_CAN_USE_HOOKS
# if SANITIZER_FUCHSIA
#  define SCUDO_CAN_USE_HOOKS 1
# else
#  define SCUDO_CAN_USE_HOOKS 0
# endif  // SANITIZER_FUCHSIA
#endif  // SCUDO_CAN_USE_HOOKS

namespace __scudo {

#if SANITIZER_CAN_USE_ALLOCATOR64
# if defined(__aarch64__) && SANITIZER_ANDROID
const uptr AllocatorSize = 0x4000000000ULL;  // 256G.
# elif defined(__aarch64__)
const uptr AllocatorSize = 0x10000000000ULL;  // 1T.
# else
const uptr AllocatorSize = 0x40000000000ULL;  // 4T.
# endif
#else
const uptr RegionSizeLog = SANITIZER_ANDROID ? 19 : 20;
#endif  // SANITIZER_CAN_USE_ALLOCATOR64

#if !defined(SCUDO_SIZE_CLASS_MAP)
# define SCUDO_SIZE_CLASS_MAP Default
#endif

#define SIZE_CLASS_MAP_TYPE SIZE_CLASS_MAP_TYPE_(SCUDO_SIZE_CLASS_MAP)
#define SIZE_CLASS_MAP_TYPE_(T) SIZE_CLASS_MAP_TYPE__(T)
#define SIZE_CLASS_MAP_TYPE__(T) T##SizeClassMap

typedef SIZE_CLASS_MAP_TYPE SizeClassMap;

}  // namespace __scudo

#endif // SCUDO_PLATFORM_H_
