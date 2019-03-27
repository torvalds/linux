// -*- C++ -*- Exception handling routines for catching.
// Copyright (C) 2001, 2003, 2004 Free Software Foundation, Inc.
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

#include <cstdlib>
#include "unwind-cxx.h"

using namespace __cxxabiv1;

extern "C" void *
__cxxabiv1::__cxa_get_exception_ptr(void *exc_obj_in) throw()
{
  _Unwind_Exception *exceptionObject
    = reinterpret_cast <_Unwind_Exception *>(exc_obj_in);

  return __gxx_caught_object(exceptionObject);
}

extern "C" void *
__cxxabiv1::__cxa_begin_catch (void *exc_obj_in) throw()
{
  _Unwind_Exception *exceptionObject
    = reinterpret_cast <_Unwind_Exception *>(exc_obj_in);
  __cxa_eh_globals *globals = __cxa_get_globals ();
  __cxa_exception *prev = globals->caughtExceptions;
  __cxa_exception *header = __get_exception_header_from_ue (exceptionObject);
  void* objectp;

  // Foreign exceptions can't be stacked here.  If the exception stack is
  // empty, then fine.  Otherwise we really have no choice but to terminate.
  // Note that this use of "header" is a lie.  It's fine so long as we only
  // examine header->unwindHeader though.
  if (!__is_gxx_exception_class(header->unwindHeader.exception_class))
    {
      if (prev != 0)
	std::terminate ();

      // Remember for end_catch and rethrow.
      globals->caughtExceptions = header;

      // ??? No sensible value to return; we don't know what the 
      // object is, much less where it is in relation to the header.
      return 0;
    }

  int count = header->handlerCount;
  // Count is less than zero if this exception was rethrown from an
  // immediately enclosing region.
  if (count < 0)
    count = -count + 1;
  else
    count += 1;
  header->handlerCount = count;
  globals->uncaughtExceptions -= 1;

  if (header != prev)
    {
      header->nextException = prev;
      globals->caughtExceptions = header;
    }

  objectp = __gxx_caught_object(exceptionObject);
#ifdef __ARM_EABI_UNWINDER__
  _Unwind_Complete(exceptionObject);
#endif
  return objectp;
}


extern "C" void
__cxxabiv1::__cxa_end_catch ()
{
  __cxa_eh_globals *globals = __cxa_get_globals_fast ();
  __cxa_exception *header = globals->caughtExceptions;

  // A rethrow of a foreign exception will be removed from the
  // the exception stack immediately by __cxa_rethrow.
  if (!header)
    return;

  // A foreign exception couldn't have been stacked (see above),
  // so by definition processing must be complete.
  if (!__is_gxx_exception_class(header->unwindHeader.exception_class))
    {
      globals->caughtExceptions = 0;
      _Unwind_DeleteException (&header->unwindHeader);
      return;
    }

  int count = header->handlerCount;
  if (count < 0)
    {
      // This exception was rethrown.  Decrement the (inverted) catch
      // count and remove it from the chain when it reaches zero.
      if (++count == 0)
	globals->caughtExceptions = header->nextException;
    }
  else if (--count == 0)
    {
      // Handling for this exception is complete.  Destroy the object.
      globals->caughtExceptions = header->nextException;
      _Unwind_DeleteException (&header->unwindHeader);
      return;
    }
  else if (count < 0)
    // A bug in the exception handling library or compiler.
    std::terminate ();

  header->handlerCount = count;
}


bool
std::uncaught_exception() throw()
{
  __cxa_eh_globals *globals = __cxa_get_globals ();
  return globals->uncaughtExceptions != 0;
}
