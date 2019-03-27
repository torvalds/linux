// -*- C++ -*- Helpers for calling unextected and terminate
// Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
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

#include <bits/c++config.h>
#include <cstdlib>
#include <exception_defines.h>
#include "unwind-cxx.h"

using namespace __cxxabiv1;

#include "unwind-pe.h"


// Helper routine for when the exception handling code needs to call
// terminate.

extern "C" void
__cxa_call_terminate(_Unwind_Exception* ue_header)
{

  if (ue_header)
    {
      // terminate is classed as a catch handler.
      __cxa_begin_catch(ue_header);

      // Call the terminate handler that was in effect when we threw this
      // exception.  */
      if (__is_gxx_exception_class(ue_header->exception_class))
	{
	  __cxa_exception* xh;

	  xh = __get_exception_header_from_ue(ue_header);
	  __terminate(xh->terminateHandler);
	}
    }
  /* Call the global routine if we don't have anything better.  */
  std::terminate();
}


#ifdef __ARM_EABI_UNWINDER__
// The ARM EABI __cxa_call_unexpected has the same semantics as the generic
// routine, but the exception specification has a different format.
extern "C" void
__cxa_call_unexpected(void* exc_obj_in)
{
  _Unwind_Exception* exc_obj
    = reinterpret_cast<_Unwind_Exception*>(exc_obj_in);

  int rtti_count = 0;
  _Unwind_Word rtti_stride = 0;
  _Unwind_Word* rtti_list = NULL;
  bool foreign_exception;
  std::unexpected_handler unexpectedHandler = NULL;
  std::terminate_handler terminateHandler = NULL;
  __cxa_exception* xh;
  if (__is_gxx_exception_class(exc_obj->exception_class))
    {
      // Save data from the EO, which may be clobbered by _cxa_begin_catch.
      xh = __get_exception_header_from_ue(exc_obj);
      unexpectedHandler = xh->unexpectedHandler;
      terminateHandler = xh->terminateHandler;
      rtti_count = exc_obj->barrier_cache.bitpattern[1];

      rtti_stride = exc_obj->barrier_cache.bitpattern[3];
      rtti_list = (_Unwind_Word*) exc_obj->barrier_cache.bitpattern[4];
      foreign_exception = false;
    }
  else
    foreign_exception = true;

  /* This must be called after extracting data from the EO, but before
     calling unexpected().   */
  __cxa_begin_catch(exc_obj);

  // This function is a handler for our exception argument.  If we exit
  // by throwing a different exception, we'll need the original cleaned up.
  struct end_catch_protect
  {
    end_catch_protect() { }
    ~end_catch_protect() { __cxa_end_catch(); }
  } end_catch_protect_obj;


  try 
    { 
      if (foreign_exception)
	std::unexpected();
      else
	__unexpected(unexpectedHandler);
    }
  catch(...) 
    {
      /* See if the new exception matches the rtti list.  */
      if (foreign_exception)
	std::terminate();

      // Get the exception thrown from unexpected.

      __cxa_eh_globals* globals = __cxa_get_globals_fast();
      __cxa_exception* new_xh = globals->caughtExceptions;
      void* new_ptr = new_xh + 1;
      const std::type_info* catch_type;
      int n;
      bool bad_exception_allowed = false;
      const std::type_info& bad_exc = typeid(std::bad_exception);

      // Check the new exception against the rtti list
      for (n = 0; n < rtti_count; n++)
	{
	  _Unwind_Word offset;

	  offset = (_Unwind_Word) &rtti_list[n * (rtti_stride >> 2)];
	  offset = _Unwind_decode_target2(offset);
	  catch_type = (const std::type_info*) (offset);

	  if (__cxa_type_match(&new_xh->unwindHeader, catch_type, false,
			       &new_ptr) != ctm_failed)
	    __throw_exception_again;

	  if (catch_type->__do_catch(&bad_exc, 0, 1))
	    bad_exception_allowed = true;
	}

      // If the exception spec allows std::bad_exception, throw that.
#ifdef __EXCEPTIONS  
      if (bad_exception_allowed)
	throw std::bad_exception();
#endif   

      // Otherwise, die.
      __terminate(terminateHandler);
    }
}
#endif // __ARM_EABI_UNWINDER__
