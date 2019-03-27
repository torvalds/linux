//===-- scudo_new_delete.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Interceptors for operators new and delete.
///
//===----------------------------------------------------------------------===//

#include "scudo_allocator.h"
#include "scudo_errors.h"

#include "interception/interception.h"

#include <stddef.h>

using namespace __scudo;

#define CXX_OPERATOR_ATTRIBUTE INTERCEPTOR_ATTRIBUTE

// Fake std::nothrow_t to avoid including <new>.
namespace std {
struct nothrow_t {};
enum class align_val_t: size_t {};
}  // namespace std

// TODO(alekseys): throw std::bad_alloc instead of dying on OOM.
#define OPERATOR_NEW_BODY_ALIGN(Type, Align, NoThrow)              \
  void *Ptr = scudoAllocate(size, static_cast<uptr>(Align), Type); \
  if (!NoThrow && UNLIKELY(!Ptr)) reportOutOfMemory(size);         \
  return Ptr;
#define OPERATOR_NEW_BODY(Type, NoThrow) \
  OPERATOR_NEW_BODY_ALIGN(Type, 0, NoThrow)

CXX_OPERATOR_ATTRIBUTE
void *operator new(size_t size)
{ OPERATOR_NEW_BODY(FromNew, /*NoThrow=*/false); }
CXX_OPERATOR_ATTRIBUTE
void *operator new[](size_t size)
{ OPERATOR_NEW_BODY(FromNewArray, /*NoThrow=*/false); }
CXX_OPERATOR_ATTRIBUTE
void *operator new(size_t size, std::nothrow_t const&)
{ OPERATOR_NEW_BODY(FromNew, /*NoThrow=*/true); }
CXX_OPERATOR_ATTRIBUTE
void *operator new[](size_t size, std::nothrow_t const&)
{ OPERATOR_NEW_BODY(FromNewArray, /*NoThrow=*/true); }
CXX_OPERATOR_ATTRIBUTE
void *operator new(size_t size, std::align_val_t align)
{ OPERATOR_NEW_BODY_ALIGN(FromNew, align, /*NoThrow=*/false); }
CXX_OPERATOR_ATTRIBUTE
void *operator new[](size_t size, std::align_val_t align)
{ OPERATOR_NEW_BODY_ALIGN(FromNewArray, align, /*NoThrow=*/false); }
CXX_OPERATOR_ATTRIBUTE
void *operator new(size_t size, std::align_val_t align, std::nothrow_t const&)
{ OPERATOR_NEW_BODY_ALIGN(FromNew, align, /*NoThrow=*/true); }
CXX_OPERATOR_ATTRIBUTE
void *operator new[](size_t size, std::align_val_t align, std::nothrow_t const&)
{ OPERATOR_NEW_BODY_ALIGN(FromNewArray, align, /*NoThrow=*/true); }

#define OPERATOR_DELETE_BODY(Type) \
  scudoDeallocate(ptr, 0, 0, Type);
#define OPERATOR_DELETE_BODY_SIZE(Type) \
  scudoDeallocate(ptr, size, 0, Type);
#define OPERATOR_DELETE_BODY_ALIGN(Type) \
  scudoDeallocate(ptr, 0, static_cast<uptr>(align), Type);
#define OPERATOR_DELETE_BODY_SIZE_ALIGN(Type) \
  scudoDeallocate(ptr, size, static_cast<uptr>(align), Type);

CXX_OPERATOR_ATTRIBUTE
void operator delete(void *ptr) NOEXCEPT
{ OPERATOR_DELETE_BODY(FromNew); }
CXX_OPERATOR_ATTRIBUTE
void operator delete[](void *ptr) NOEXCEPT
{ OPERATOR_DELETE_BODY(FromNewArray); }
CXX_OPERATOR_ATTRIBUTE
void operator delete(void *ptr, std::nothrow_t const&)
{ OPERATOR_DELETE_BODY(FromNew); }
CXX_OPERATOR_ATTRIBUTE
void operator delete[](void *ptr, std::nothrow_t const&)
{ OPERATOR_DELETE_BODY(FromNewArray); }
CXX_OPERATOR_ATTRIBUTE
void operator delete(void *ptr, size_t size) NOEXCEPT
{ OPERATOR_DELETE_BODY_SIZE(FromNew); }
CXX_OPERATOR_ATTRIBUTE
void operator delete[](void *ptr, size_t size) NOEXCEPT
{ OPERATOR_DELETE_BODY_SIZE(FromNewArray); }
CXX_OPERATOR_ATTRIBUTE
void operator delete(void *ptr, std::align_val_t align) NOEXCEPT
{ OPERATOR_DELETE_BODY_ALIGN(FromNew); }
CXX_OPERATOR_ATTRIBUTE
void operator delete[](void *ptr, std::align_val_t align) NOEXCEPT
{ OPERATOR_DELETE_BODY_ALIGN(FromNewArray); }
CXX_OPERATOR_ATTRIBUTE
void operator delete(void *ptr, std::align_val_t align, std::nothrow_t const&)
{ OPERATOR_DELETE_BODY_ALIGN(FromNew); }
CXX_OPERATOR_ATTRIBUTE
void operator delete[](void *ptr, std::align_val_t align, std::nothrow_t const&)
{ OPERATOR_DELETE_BODY_ALIGN(FromNewArray); }
CXX_OPERATOR_ATTRIBUTE
void operator delete(void *ptr, size_t size, std::align_val_t align) NOEXCEPT
{ OPERATOR_DELETE_BODY_SIZE_ALIGN(FromNew); }
CXX_OPERATOR_ATTRIBUTE
void operator delete[](void *ptr, size_t size, std::align_val_t align) NOEXCEPT
{ OPERATOR_DELETE_BODY_SIZE_ALIGN(FromNewArray); }
