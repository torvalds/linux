// new abi support -*- C++ -*-
  
// Copyright (C) 2000, 2002, 2003, 2004, 2006 Free Software Foundation, Inc.
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
// 
// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING.  If not, write to
// the Free Software Foundation, 51 Franklin Street, Fifth Floor,
// Boston, MA 02110-1301, USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// Written by Nathan Sidwell, Codesourcery LLC, <nathan@codesourcery.com>
 
/* This file declares the new abi entry points into the runtime. It is not
   normally necessary for user programs to include this header, or use the
   entry points directly. However, this header is available should that be
   needed.
   
   Some of the entry points are intended for both C and C++, thus this header
   is includable from both C and C++. Though the C++ specific parts are not
   available in C, naturally enough.  */

/** @file cxxabi.h
 *  The header provides an interface to the C++ ABI.
 */

#ifndef _CXXABI_H
#define _CXXABI_H 1

#pragma GCC visibility push(default)

#include <stddef.h>
#include <bits/cxxabi_tweaks.h>
 
#ifdef __cplusplus
namespace __cxxabiv1
{  
  extern "C" 
  {
#endif

  typedef __cxa_cdtor_return_type (*__cxa_cdtor_type)(void *);

  // Allocate array.
  void* 
  __cxa_vec_new(size_t __element_count, size_t __element_size, 
		size_t __padding_size, __cxa_cdtor_type constructor,
		__cxa_cdtor_type destructor);

  void*
  __cxa_vec_new2(size_t __element_count, size_t __element_size,
		 size_t __padding_size, __cxa_cdtor_type constructor,
		 __cxa_cdtor_type destructor, void *(*__alloc) (size_t), 
		 void (*__dealloc) (void*));

  void*
  __cxa_vec_new3(size_t __element_count, size_t __element_size,
		 size_t __padding_size, __cxa_cdtor_type constructor,
		 __cxa_cdtor_type destructor, void *(*__alloc) (size_t), 
		 void (*__dealloc) (void*, size_t));

  // Construct array.
  __cxa_vec_ctor_return_type
  __cxa_vec_ctor(void* __array_address, size_t __element_count,
		 size_t __element_size, __cxa_cdtor_type constructor,
		 __cxa_cdtor_type destructor);

  __cxa_vec_ctor_return_type
  __cxa_vec_cctor(void* dest_array, void* src_array, size_t element_count, 
		  size_t element_size, 
		  __cxa_cdtor_return_type (*constructor) (void*, void*), 
		  __cxa_cdtor_type destructor);
 
  // Destruct array.
  void 
  __cxa_vec_dtor(void* __array_address, size_t __element_count,
		 size_t __element_size, __cxa_cdtor_type destructor);
  
  void 
  __cxa_vec_cleanup(void* __array_address, size_t __element_count,
		    size_t __element_size, __cxa_cdtor_type destructor);
  
  // Destruct and release array.
  void 
  __cxa_vec_delete(void* __array_address, size_t __element_size,
		   size_t __padding_size, __cxa_cdtor_type destructor);

  void 
  __cxa_vec_delete2(void* __array_address, size_t __element_size,
		    size_t __padding_size, __cxa_cdtor_type destructor,
		    void (*__dealloc) (void*));
                  
  void 
  __cxa_vec_delete3(void* __array_address, size_t __element_size,
		    size_t __padding_size, __cxa_cdtor_type destructor,
		    void (*__dealloc) (void*, size_t));

  int 
  __cxa_guard_acquire(__guard*);

  void 
  __cxa_guard_release(__guard*);

  void 
  __cxa_guard_abort(__guard*);

  // Pure virtual functions.
  void
  __cxa_pure_virtual(void);

  // Exception handling.
  void
  __cxa_bad_cast();

  void
  __cxa_bad_typeid();

  // DSO destruction.
  int
  __cxa_atexit(void (*)(void*), void*, void*);

  int
  __cxa_finalize(void*);

  // Demangling routines. 
  char*
  __cxa_demangle(const char* __mangled_name, char* __output_buffer,
		 size_t* __length, int* __status);
#ifdef __cplusplus
  }
} // namespace __cxxabiv1
#endif

#ifdef __cplusplus

#include <typeinfo>

namespace __cxxabiv1
{
  // Type information for int, float etc.
  class __fundamental_type_info : public std::type_info
  {
  public:
    explicit 
    __fundamental_type_info(const char* __n) : std::type_info(__n) { }

    virtual 
    ~__fundamental_type_info();
  };

  // Type information for array objects.
  class __array_type_info : public std::type_info
  {
  public:
    explicit 
    __array_type_info(const char* __n) : std::type_info(__n) { }

    virtual 
    ~__array_type_info();
  };

  // Type information for functions (both member and non-member).
  class __function_type_info : public std::type_info
  {
  public:
    explicit 
    __function_type_info(const char* __n) : std::type_info(__n) { }

    virtual 
    ~__function_type_info();

  protected:
    // Implementation defined member function.
    virtual bool 
    __is_function_p() const;
  };

  // Type information for enumerations.
  class __enum_type_info : public std::type_info
  {
  public:
    explicit 
    __enum_type_info(const char* __n) : std::type_info(__n) { }

    virtual 
    ~__enum_type_info();
  };

  // Common type information for simple pointers and pointers to member.
  class __pbase_type_info : public std::type_info
  {
  public:
    unsigned int 		__flags; // Qualification of the target object.
    const std::type_info* 	__pointee; // Type of pointed to object.

    explicit 
    __pbase_type_info(const char* __n, int __quals, 
		      const std::type_info* __type)
    : std::type_info(__n), __flags(__quals), __pointee(__type)
    { }
    
    virtual 
    ~__pbase_type_info();

    // Implementation defined type.
    enum __masks 
      {
	__const_mask = 0x1,
	__volatile_mask = 0x2,
	__restrict_mask = 0x4,
	__incomplete_mask = 0x8,
	__incomplete_class_mask = 0x10
      };

  protected:
    __pbase_type_info(const __pbase_type_info&);

    __pbase_type_info&
    operator=(const __pbase_type_info&);

    // Implementation defined member functions.
    virtual bool 
    __do_catch(const std::type_info* __thr_type, void** __thr_obj, 
	       unsigned int __outer) const;

    inline virtual bool 
    __pointer_catch(const __pbase_type_info* __thr_type, void** __thr_obj,
		    unsigned __outer) const;
  };

  // Type information for simple pointers.
  class __pointer_type_info : public __pbase_type_info
  {
  public:
    explicit 
    __pointer_type_info(const char* __n, int __quals, 
			const std::type_info* __type)
    : __pbase_type_info (__n, __quals, __type) { }


    virtual 
    ~__pointer_type_info();

  protected:
    // Implementation defined member functions.
    virtual bool 
    __is_pointer_p() const;

    virtual bool 
    __pointer_catch(const __pbase_type_info* __thr_type, void** __thr_obj, 
		    unsigned __outer) const;
  };

  class __class_type_info;

  // Type information for a pointer to member variable.
  class __pointer_to_member_type_info : public __pbase_type_info
  {
  public:
    __class_type_info* __context;   // Class of the member.

    explicit 
    __pointer_to_member_type_info(const char* __n, int __quals,
				  const std::type_info* __type, 
				  __class_type_info* __klass)
    : __pbase_type_info(__n, __quals, __type), __context(__klass) { }

    virtual 
    ~__pointer_to_member_type_info();

  protected:
    __pointer_to_member_type_info(const __pointer_to_member_type_info&);

    __pointer_to_member_type_info&
    operator=(const __pointer_to_member_type_info&);

    // Implementation defined member function.
    virtual bool 
    __pointer_catch(const __pbase_type_info* __thr_type, void** __thr_obj,
		    unsigned __outer) const;
  };

  // Helper class for __vmi_class_type.
  class __base_class_type_info
  {
  public:
    const __class_type_info* 	__base_type;  // Base class type.
    long 			__offset_flags;  // Offset and info.

    enum __offset_flags_masks 
      {
	__virtual_mask = 0x1,
	__public_mask = 0x2,
	__hwm_bit = 2,
	__offset_shift = 8          // Bits to shift offset.
      };
  
    // Implementation defined member functions.
    bool 
    __is_virtual_p() const
    { return __offset_flags & __virtual_mask; }

    bool 
    __is_public_p() const
    { return __offset_flags & __public_mask; }

    ptrdiff_t 
    __offset() const
    { 
      // This shift, being of a signed type, is implementation
      // defined. GCC implements such shifts as arithmetic, which is
      // what we want.
      return static_cast<ptrdiff_t>(__offset_flags) >> __offset_shift;
    }
  };

  // Type information for a class.
  class __class_type_info : public std::type_info
  {
  public:
    explicit 
    __class_type_info (const char *__n) : type_info(__n) { }

    virtual 
    ~__class_type_info ();

    // Implementation defined types.
    // The type sub_kind tells us about how a base object is contained
    // within a derived object. We often do this lazily, hence the
    // UNKNOWN value. At other times we may use NOT_CONTAINED to mean
    // not publicly contained.
    enum __sub_kind
      {
	// We have no idea.
	__unknown = 0, 

	// Not contained within us (in some circumstances this might
	// mean not contained publicly)
	__not_contained, 

	// Contained ambiguously.
	__contained_ambig, 
    
	// Via a virtual path.
	__contained_virtual_mask = __base_class_type_info::__virtual_mask, 

	// Via a public path.
	__contained_public_mask = __base_class_type_info::__public_mask,   

	// Contained within us.
	__contained_mask = 1 << __base_class_type_info::__hwm_bit,
    
	__contained_private = __contained_mask,
	__contained_public = __contained_mask | __contained_public_mask
      };

    struct __upcast_result;
    struct __dyncast_result;

  protected:
    // Implementation defined member functions.
    virtual bool 
    __do_upcast(const __class_type_info* __dst_type, void**__obj_ptr) const;

    virtual bool 
    __do_catch(const type_info* __thr_type, void** __thr_obj, 
	       unsigned __outer) const;

  public:
    // Helper for upcast. See if DST is us, or one of our bases. 
    // Return false if not found, true if found. 
    virtual bool 
    __do_upcast(const __class_type_info* __dst, const void* __obj,
		__upcast_result& __restrict __result) const;

    // Indicate whether SRC_PTR of type SRC_TYPE is contained publicly
    // within OBJ_PTR. OBJ_PTR points to a base object of our type,
    // which is the destination type. SRC2DST indicates how SRC
    // objects might be contained within this type.  If SRC_PTR is one
    // of our SRC_TYPE bases, indicate the virtuality. Returns
    // not_contained for non containment or private containment.
    inline __sub_kind 
    __find_public_src(ptrdiff_t __src2dst, const void* __obj_ptr,
		      const __class_type_info* __src_type, 
		      const void* __src_ptr) const;

    // Helper for dynamic cast. ACCESS_PATH gives the access from the
    // most derived object to this base. DST_TYPE indicates the
    // desired type we want. OBJ_PTR points to a base of our type
    // within the complete object. SRC_TYPE indicates the static type
    // started from and SRC_PTR points to that base within the most
    // derived object. Fill in RESULT with what we find. Return true
    // if we have located an ambiguous match.
    virtual bool 
    __do_dyncast(ptrdiff_t __src2dst, __sub_kind __access_path,
		 const __class_type_info* __dst_type, const void* __obj_ptr, 
		 const __class_type_info* __src_type, const void* __src_ptr, 
		 __dyncast_result& __result) const;
    
    // Helper for find_public_subobj. SRC2DST indicates how SRC_TYPE
    // bases are inherited by the type started from -- which is not
    // necessarily the current type. The current type will be a base
    // of the destination type.  OBJ_PTR points to the current base.
    virtual __sub_kind 
    __do_find_public_src(ptrdiff_t __src2dst, const void* __obj_ptr,
			 const __class_type_info* __src_type,
			 const void* __src_ptr) const;
  };

  // Type information for a class with a single non-virtual base.
  class __si_class_type_info : public __class_type_info
  {
  public:
    const __class_type_info* __base_type;

    explicit 
    __si_class_type_info(const char *__n, const __class_type_info *__base)
    : __class_type_info(__n), __base_type(__base) { }

    virtual 
    ~__si_class_type_info();

  protected:
    __si_class_type_info(const __si_class_type_info&);

    __si_class_type_info&
    operator=(const __si_class_type_info&);

    // Implementation defined member functions.
    virtual bool 
    __do_dyncast(ptrdiff_t __src2dst, __sub_kind __access_path,
		 const __class_type_info* __dst_type, const void* __obj_ptr,
		 const __class_type_info* __src_type, const void* __src_ptr,
		 __dyncast_result& __result) const;

    virtual __sub_kind 
    __do_find_public_src(ptrdiff_t __src2dst, const void* __obj_ptr,
			 const __class_type_info* __src_type,
			 const void* __sub_ptr) const;

    virtual bool 
    __do_upcast(const __class_type_info*__dst, const void*__obj,
		__upcast_result& __restrict __result) const;
  };

  // Type information for a class with multiple and/or virtual bases.
  class __vmi_class_type_info : public __class_type_info 
  {
  public:
    unsigned int 		__flags;  // Details about the class hierarchy.
    unsigned int 		__base_count;  // Number of direct bases.

    // The array of bases uses the trailing array struct hack so this
    // class is not constructable with a normal constructor. It is
    // internally generated by the compiler.
    __base_class_type_info 	__base_info[1];  // Array of bases.

    explicit 
    __vmi_class_type_info(const char* __n, int ___flags)
    : __class_type_info(__n), __flags(___flags), __base_count(0) { }

    virtual 
    ~__vmi_class_type_info();

    // Implementation defined types.
    enum __flags_masks 
      {
	__non_diamond_repeat_mask = 0x1, // Distinct instance of repeated base.
	__diamond_shaped_mask = 0x2, // Diamond shaped multiple inheritance.
	__flags_unknown_mask = 0x10
      };

  protected:
    // Implementation defined member functions.
    virtual bool 
    __do_dyncast(ptrdiff_t __src2dst, __sub_kind __access_path,
		 const __class_type_info* __dst_type, const void* __obj_ptr,
		 const __class_type_info* __src_type, const void* __src_ptr,
		 __dyncast_result& __result) const;

    virtual __sub_kind 
    __do_find_public_src(ptrdiff_t __src2dst, const void* __obj_ptr, 
			 const __class_type_info* __src_type,
			 const void* __src_ptr) const;
    
    virtual bool 
    __do_upcast(const __class_type_info* __dst, const void* __obj,
		__upcast_result& __restrict __result) const;
  };

  // Dynamic cast runtime.
  // src2dst has the following possible values
  //  >-1: src_type is a unique public non-virtual base of dst_type
  //       dst_ptr + src2dst == src_ptr
  //   -1: unspecified relationship
  //   -2: src_type is not a public base of dst_type
  //   -3: src_type is a multiple public non-virtual base of dst_type
  extern "C" void*
  __dynamic_cast(const void* __src_ptr, // Starting object.
		 const __class_type_info* __src_type, // Static type of object.
		 const __class_type_info* __dst_type, // Desired target type.
		 ptrdiff_t __src2dst); // How src and dst are related.


  // Returns the type_info for the currently handled exception [15.3/8], or
  // null if there is none.
  extern "C" std::type_info*
  __cxa_current_exception_type();
} // namespace __cxxabiv1

// User programs should use the alias `abi'. 
namespace abi = __cxxabiv1;

#endif // __cplusplus

#pragma GCC visibility pop

#endif // __CXXABI_H 
