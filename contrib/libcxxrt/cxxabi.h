/* 
 * Copyright 2012 David Chisnall. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */ 

#ifndef __CXXABI_H_
#define __CXXABI_H_
#include <stddef.h>
#include <stdint.h>
#include "unwind.h"
namespace std 
{
	class type_info;
}
/*
 * The cxxabi.h header provides a set of public definitions for types and
 * functions defined by the Itanium C++ ABI specification.  For reference, see
 * the ABI specification here:
 *
 * http://sourcery.mentor.com/public/cxx-abi/abi.html
 *
 * All deviations from this specification, unless otherwise noted, are
 * accidental.
 */

#ifdef __cplusplus
namespace __cxxabiv1 {
extern "C" {
#endif
/**
 * Function type to call when an unexpected exception is encountered.
 */
typedef void (*unexpected_handler)();
/**
 * Function type to call when an unrecoverable condition is encountered.
 */
typedef void (*terminate_handler)();


/**
 * Structure used as a header on thrown exceptions.  This is the same layout as
 * defined by the Itanium ABI spec, so should be interoperable with any other
 * implementation of this spec, such as GNU libsupc++.
 *
 * This structure is allocated when an exception is thrown.  Unwinding happens
 * in two phases, the first looks for a handler and the second installs the
 * context.  This structure stores a cache of the handler location between
 * phase 1 and phase 2.  Unfortunately, cleanup information is not cached, so
 * must be looked up in both phases.  This happens for two reasons.  The first
 * is that we don't know how many frames containing cleanups there will be, and
 * we should avoid dynamic allocation during unwinding (the exception may be
 * reporting that we've run out of memory).  The second is that finding
 * cleanups is much cheaper than finding handlers, because we don't have to
 * look at the type table at all.
 *
 * Note: Several fields of this structure have not-very-informative names.
 * These are taken from the ABI spec and have not been changed to make it
 * easier for people referring to to the spec while reading this code.
 */
struct __cxa_exception
{
#if __LP64__
	/**
	 * Reference count.  Used to support the C++11 exception_ptr class.  This
	 * is prepended to the structure in 64-bit mode and squeezed in to the
	 * padding left before the 64-bit aligned _Unwind_Exception at the end in
	 * 32-bit mode.
	 *
	 * Note that it is safe to extend this structure at the beginning, rather
	 * than the end, because the public API for creating it returns the address
	 * of the end (where the exception object can be stored).
	 */
	uintptr_t referenceCount;
#endif
	/** Type info for the thrown object. */
	std::type_info *exceptionType;
	/** Destructor for the object, if one exists. */
	void (*exceptionDestructor) (void *); 
	/** Handler called when an exception specification is violated. */
	unexpected_handler unexpectedHandler;
	/** Hander called to terminate. */
	terminate_handler terminateHandler;
	/**
	 * Next exception in the list.  If an exception is thrown inside a catch
	 * block and caught in a nested catch, this points to the exception that
	 * will be handled after the inner catch block completes.
	 */
	__cxa_exception *nextException;
	/**
	 * The number of handlers that currently have references to this
	 * exception.  The top (non-sign) bit of this is used as a flag to indicate
	 * that the exception is being rethrown, so should not be deleted when its
	 * handler count reaches 0 (which it doesn't with the top bit set).
	 */
	int handlerCount;
#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
	/**
	 * The ARM EH ABI requires the unwind library to keep track of exceptions
	 * during cleanups.  These support nesting, so we need to keep a list of
	 * them.
	 */
	_Unwind_Exception *nextCleanup;
	/**
	 * The number of cleanups that are currently being run on this exception. 
	 */
	int cleanupCount;
#endif
	/**
	 * The selector value to be returned when installing the catch handler.
	 * Used at the call site to determine which catch() block should execute.
	 * This is found in phase 1 of unwinding then installed in phase 2.
	 */
	int handlerSwitchValue;
	/**
	 * The action record for the catch.  This is cached during phase 1
	 * unwinding.
	 */
	const char *actionRecord;
	/**
	 * Pointer to the language-specific data area (LSDA) for the handler
	 * frame.  This is unused in this implementation, but set for ABI
	 * compatibility in case we want to mix code in very weird ways.
	 */
	const char *languageSpecificData;
	/** The cached landing pad for the catch handler.*/
	void *catchTemp;
	/**
	 * The pointer that will be returned as the pointer to the object.  When
	 * throwing a class and catching a virtual superclass (for example), we
	 * need to adjust the thrown pointer to make it all work correctly.
	 */
	void *adjustedPtr;
#if !__LP64__
	/**
	 * Reference count.  Used to support the C++11 exception_ptr class.  This
	 * is prepended to the structure in 64-bit mode and squeezed in to the
	 * padding left before the 64-bit aligned _Unwind_Exception at the end in
	 * 32-bit mode.
	 *
	 * Note that it is safe to extend this structure at the beginning, rather
	 * than the end, because the public API for creating it returns the address
	 * of the end (where the exception object can be stored) 
	 */
	uintptr_t referenceCount;
#endif
	/** The language-agnostic part of the exception header. */
	_Unwind_Exception unwindHeader;
};

/**
 * ABI-specified globals structure.  Returned by the __cxa_get_globals()
 * function and its fast variant.  This is a per-thread structure - every
 * thread will have one lazily allocated.
 *
 * This structure is defined by the ABI, so may be used outside of this
 * library.
 */
struct __cxa_eh_globals
{
	/**
	 * A linked list of exceptions that are currently caught.  There may be
	 * several of these in nested catch() blocks.
	 */
	__cxa_exception *caughtExceptions;
	/**
	 * The number of uncaught exceptions.
	 */
	unsigned int uncaughtExceptions;
};
/**
 * ABI function returning the __cxa_eh_globals structure.
 */
__cxa_eh_globals *__cxa_get_globals(void);
/**
 * Version of __cxa_get_globals() assuming that __cxa_get_globals() has already
 * been called at least once by this thread.
 */
__cxa_eh_globals *__cxa_get_globals_fast(void);

std::type_info * __cxa_current_exception_type();

/**
 * Throws an exception returned by __cxa_current_primary_exception().  This
 * exception may have been caught in another thread.
 */
void __cxa_rethrow_primary_exception(void* thrown_exception);
/**
 * Returns the current exception in a form that can be stored in an
 * exception_ptr object and then rethrown by a call to
 * __cxa_rethrow_primary_exception().
 */
void *__cxa_current_primary_exception(void);
/**
 * Increments the reference count of an exception.  Called when an
 * exception_ptr is copied.
 */
void __cxa_increment_exception_refcount(void* thrown_exception);
/**
 * Decrements the reference count of an exception.  Called when an
 * exception_ptr is deleted.
 */
void __cxa_decrement_exception_refcount(void* thrown_exception);
/**
 * Demangles a C++ symbol or type name.  The buffer, if non-NULL, must be
 * allocated with malloc() and must be *n bytes or more long.  This function
 * may call realloc() on the value pointed to by buf, and will return the
 * length of the string via *n.
 *
 * The value pointed to by status is set to one of the following:
 *
 * 0: success
 * -1: memory allocation failure
 * -2: invalid mangled name
 * -3: invalid arguments
 */
char* __cxa_demangle(const char* mangled_name,
                     char* buf,
                     size_t* n,
                     int* status);
#ifdef __cplusplus
} // extern "C"
} // namespace

namespace abi = __cxxabiv1;

#endif /* __cplusplus */
#endif /* __CXXABI_H_ */
