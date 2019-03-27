/* 
 * Copyright 2010-2011 PathScale, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include "typeinfo.h"
#include "dwarf_eh.h"
#include "atomic.h"
#include "cxxabi.h"

#pragma weak pthread_key_create
#pragma weak pthread_setspecific
#pragma weak pthread_getspecific
#pragma weak pthread_once
#ifdef LIBCXXRT_WEAK_LOCKS
#pragma weak pthread_mutex_lock
#define pthread_mutex_lock(mtx) do {\
	if (pthread_mutex_lock) pthread_mutex_lock(mtx);\
	} while(0)
#pragma weak pthread_mutex_unlock
#define pthread_mutex_unlock(mtx) do {\
	if (pthread_mutex_unlock) pthread_mutex_unlock(mtx);\
	} while(0)
#pragma weak pthread_cond_signal
#define pthread_cond_signal(cv) do {\
	if (pthread_cond_signal) pthread_cond_signal(cv);\
	} while(0)
#pragma weak pthread_cond_wait
#define pthread_cond_wait(cv, mtx) do {\
	if (pthread_cond_wait) pthread_cond_wait(cv, mtx);\
	} while(0)
#endif

using namespace ABI_NAMESPACE;

/**
 * Saves the result of the landing pad that we have found.  For ARM, this is
 * stored in the generic unwind structure, while on other platforms it is
 * stored in the C++ exception.
 */
static void saveLandingPad(struct _Unwind_Context *context,
                           struct _Unwind_Exception *ucb,
                           struct __cxa_exception *ex,
                           int selector,
                           dw_eh_ptr_t landingPad)
{
#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
	// On ARM, we store the saved exception in the generic part of the structure
	ucb->barrier_cache.sp = _Unwind_GetGR(context, 13);
	ucb->barrier_cache.bitpattern[1] = static_cast<uint32_t>(selector);
	ucb->barrier_cache.bitpattern[3] = reinterpret_cast<uint32_t>(landingPad);
#endif
	// Cache the results for the phase 2 unwind, if we found a handler
	// and this is not a foreign exception.  
	if (ex)
	{
		ex->handlerSwitchValue = selector;
		ex->catchTemp = landingPad;
	}
}

/**
 * Loads the saved landing pad.  Returns 1 on success, 0 on failure.
 */
static int loadLandingPad(struct _Unwind_Context *context,
                          struct _Unwind_Exception *ucb,
                          struct __cxa_exception *ex,
                          unsigned long *selector,
                          dw_eh_ptr_t *landingPad)
{
#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
	*selector = ucb->barrier_cache.bitpattern[1];
	*landingPad = reinterpret_cast<dw_eh_ptr_t>(ucb->barrier_cache.bitpattern[3]);
	return 1;
#else
	if (ex)
	{
		*selector = ex->handlerSwitchValue;
		*landingPad = reinterpret_cast<dw_eh_ptr_t>(ex->catchTemp);
		return 0;
	}
	return 0;
#endif
}

static inline _Unwind_Reason_Code continueUnwinding(struct _Unwind_Exception *ex,
                                                    struct _Unwind_Context *context)
{
#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
	if (__gnu_unwind_frame(ex, context) != _URC_OK) { return _URC_FAILURE; }
#endif
	return _URC_CONTINUE_UNWIND;
}


extern "C" void __cxa_free_exception(void *thrown_exception);
extern "C" void __cxa_free_dependent_exception(void *thrown_exception);
extern "C" void* __dynamic_cast(const void *sub,
                                const __class_type_info *src,
                                const __class_type_info *dst,
                                ptrdiff_t src2dst_offset);

/**
 * The type of a handler that has been found.
 */
typedef enum
{
	/** No handler. */
	handler_none,
	/**
	 * A cleanup - the exception will propagate through this frame, but code
	 * must be run when this happens.
	 */
	handler_cleanup,
	/**
	 * A catch statement.  The exception will not propagate past this frame
	 * (without an explicit rethrow).
	 */
	handler_catch
} handler_type;

/**
 * Per-thread info required by the runtime.  We store a single structure
 * pointer in thread-local storage, because this tends to be a scarce resource
 * and it's impolite to steal all of it and not leave any for the rest of the
 * program.
 *
 * Instances of this structure are allocated lazily - at most one per thread -
 * and are destroyed on thread termination.
 */
struct __cxa_thread_info
{
	/** The termination handler for this thread. */
	terminate_handler terminateHandler;
	/** The unexpected exception handler for this thread. */
	unexpected_handler unexpectedHandler;
	/**
	 * The number of emergency buffers held by this thread.  This is 0 in
	 * normal operation - the emergency buffers are only used when malloc()
	 * fails to return memory for allocating an exception.  Threads are not
	 * permitted to hold more than 4 emergency buffers (as per recommendation
	 * in ABI spec [3.3.1]).
	 */
	int emergencyBuffersHeld;
	/**
	 * The exception currently running in a cleanup.
	 */
	_Unwind_Exception *currentCleanup;
	/**
	 * Our state with respect to foreign exceptions.  Usually none, set to
	 * caught if we have just caught an exception and rethrown if we are
	 * rethrowing it.
	 */
	enum 
	{
		none,
		caught,
		rethrown
	} foreign_exception_state;
	/**
	 * The public part of this structure, accessible from outside of this
	 * module.
	 */
	__cxa_eh_globals globals;
};
/**
 * Dependent exception.  This 
 */
struct __cxa_dependent_exception
{
#if __LP64__
	void *primaryException;
#endif
	std::type_info *exceptionType;
	void (*exceptionDestructor) (void *); 
	unexpected_handler unexpectedHandler;
	terminate_handler terminateHandler;
	__cxa_exception *nextException;
	int handlerCount;
#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
	_Unwind_Exception *nextCleanup;
	int cleanupCount;
#endif
	int handlerSwitchValue;
	const char *actionRecord;
	const char *languageSpecificData;
	void *catchTemp;
	void *adjustedPtr;
#if !__LP64__
	void *primaryException;
#endif
	_Unwind_Exception unwindHeader;
};


namespace std
{
	void unexpected();
	class exception
	{
		public:
			virtual ~exception() throw();
			virtual const char* what() const throw();
	};

}

/**
 * Class of exceptions to distinguish between this and other exception types.
 *
 * The first four characters are the vendor ID.  Currently, we use GNUC,
 * because we aim for ABI-compatibility with the GNU implementation, and
 * various checks may test for equality of the class, which is incorrect.
 */
static const uint64_t exception_class =
	EXCEPTION_CLASS('G', 'N', 'U', 'C', 'C', '+', '+', '\0');
/**
 * Class used for dependent exceptions.  
 */
static const uint64_t dependent_exception_class =
	EXCEPTION_CLASS('G', 'N', 'U', 'C', 'C', '+', '+', '\x01');
/**
 * The low four bytes of the exception class, indicating that we conform to the
 * Itanium C++ ABI.  This is currently unused, but should be used in the future
 * if we change our exception class, to allow this library and libsupc++ to be
 * linked to the same executable and both to interoperate.
 */
static const uint32_t abi_exception_class = 
	GENERIC_EXCEPTION_CLASS('C', '+', '+', '\0');

static bool isCXXException(uint64_t cls)
{
	return (cls == exception_class) || (cls == dependent_exception_class);
}

static bool isDependentException(uint64_t cls)
{
	return cls == dependent_exception_class;
}

static __cxa_exception *exceptionFromPointer(void *ex)
{
	return reinterpret_cast<__cxa_exception*>(static_cast<char*>(ex) -
			offsetof(struct __cxa_exception, unwindHeader));
}
static __cxa_exception *realExceptionFromException(__cxa_exception *ex)
{
	if (!isDependentException(ex->unwindHeader.exception_class)) { return ex; }
	return reinterpret_cast<__cxa_exception*>((reinterpret_cast<__cxa_dependent_exception*>(ex))->primaryException)-1;
}


namespace std
{
	// Forward declaration of standard library terminate() function used to
	// abort execution.
	void terminate(void);
}

using namespace ABI_NAMESPACE;



/** The global termination handler. */
static terminate_handler terminateHandler = abort;
/** The global unexpected exception handler. */
static unexpected_handler unexpectedHandler = std::terminate;

/** Key used for thread-local data. */
static pthread_key_t eh_key;


/**
 * Cleanup function, allowing foreign exception handlers to correctly destroy
 * this exception if they catch it.
 */
static void exception_cleanup(_Unwind_Reason_Code reason, 
                              struct _Unwind_Exception *ex)
{
	// Exception layout:
	// [__cxa_exception [_Unwind_Exception]] [exception object]
	//
	// __cxa_free_exception expects a pointer to the exception object
	__cxa_free_exception(static_cast<void*>(ex + 1));
}
static void dependent_exception_cleanup(_Unwind_Reason_Code reason, 
                              struct _Unwind_Exception *ex)
{

	__cxa_free_dependent_exception(static_cast<void*>(ex + 1));
}

/**
 * Recursively walk a list of exceptions and delete them all in post-order.
 */
static void free_exception_list(__cxa_exception *ex)
{
	if (0 != ex->nextException)
	{
		free_exception_list(ex->nextException);
	}
	// __cxa_free_exception() expects to be passed the thrown object, which
	// immediately follows the exception, not the exception itself
	__cxa_free_exception(ex+1);
}

/**
 * Cleanup function called when a thread exists to make certain that all of the
 * per-thread data is deleted.
 */
static void thread_cleanup(void* thread_info)
{
	__cxa_thread_info *info = static_cast<__cxa_thread_info*>(thread_info);
	if (info->globals.caughtExceptions)
	{
		// If this is a foreign exception, ask it to clean itself up.
		if (info->foreign_exception_state != __cxa_thread_info::none)
		{
			_Unwind_Exception *e = reinterpret_cast<_Unwind_Exception*>(info->globals.caughtExceptions);
			if (e->exception_cleanup)
				e->exception_cleanup(_URC_FOREIGN_EXCEPTION_CAUGHT, e);
		}
		else
		{
			free_exception_list(info->globals.caughtExceptions);
		}
	}
	free(thread_info);
}


/**
 * Once control used to protect the key creation.
 */
static pthread_once_t once_control = PTHREAD_ONCE_INIT;

/**
 * We may not be linked against a full pthread implementation.  If we're not,
 * then we need to fake the thread-local storage by storing 'thread-local'
 * things in a global.
 */
static bool fakeTLS;
/**
 * Thread-local storage for a single-threaded program.
 */
static __cxa_thread_info singleThreadInfo;
/**
 * Initialise eh_key.
 */
static void init_key(void)
{
	if ((0 == pthread_key_create) ||
	    (0 == pthread_setspecific) ||
	    (0 == pthread_getspecific))
	{
		fakeTLS = true;
		return;
	}
	pthread_key_create(&eh_key, thread_cleanup);
	pthread_setspecific(eh_key, reinterpret_cast<void *>(0x42));
	fakeTLS = (pthread_getspecific(eh_key) != reinterpret_cast<void *>(0x42));
	pthread_setspecific(eh_key, 0);
}

/**
 * Returns the thread info structure, creating it if it is not already created.
 */
static __cxa_thread_info *thread_info()
{
	if ((0 == pthread_once) || pthread_once(&once_control, init_key))
	{
		fakeTLS = true;
	}
	if (fakeTLS) { return &singleThreadInfo; }
	__cxa_thread_info *info = static_cast<__cxa_thread_info*>(pthread_getspecific(eh_key));
	if (0 == info)
	{
		info = static_cast<__cxa_thread_info*>(calloc(1, sizeof(__cxa_thread_info)));
		pthread_setspecific(eh_key, info);
	}
	return info;
}
/**
 * Fast version of thread_info().  May fail if thread_info() is not called on
 * this thread at least once already.
 */
static __cxa_thread_info *thread_info_fast()
{
	if (fakeTLS) { return &singleThreadInfo; }
	return static_cast<__cxa_thread_info*>(pthread_getspecific(eh_key));
}
/**
 * ABI function returning the __cxa_eh_globals structure.
 */
extern "C" __cxa_eh_globals *ABI_NAMESPACE::__cxa_get_globals(void)
{
	return &(thread_info()->globals);
}
/**
 * Version of __cxa_get_globals() assuming that __cxa_get_globals() has already
 * been called at least once by this thread.
 */
extern "C" __cxa_eh_globals *ABI_NAMESPACE::__cxa_get_globals_fast(void)
{
	return &(thread_info_fast()->globals);
}

/**
 * An emergency allocation reserved for when malloc fails.  This is treated as
 * 16 buffers of 1KB each.
 */
static char emergency_buffer[16384];
/**
 * Flag indicating whether each buffer is allocated.
 */
static bool buffer_allocated[16];
/**
 * Lock used to protect emergency allocation.
 */
static pthread_mutex_t emergency_malloc_lock = PTHREAD_MUTEX_INITIALIZER;
/**
 * Condition variable used to wait when two threads are both trying to use the
 * emergency malloc() buffer at once.
 */
static pthread_cond_t emergency_malloc_wait = PTHREAD_COND_INITIALIZER;

/**
 * Allocates size bytes from the emergency allocation mechanism, if possible.
 * This function will fail if size is over 1KB or if this thread already has 4
 * emergency buffers.  If all emergency buffers are allocated, it will sleep
 * until one becomes available.
 */
static char *emergency_malloc(size_t size)
{
	if (size > 1024) { return 0; }

	__cxa_thread_info *info = thread_info();
	// Only 4 emergency buffers allowed per thread!
	if (info->emergencyBuffersHeld > 3) { return 0; }

	pthread_mutex_lock(&emergency_malloc_lock);
	int buffer = -1;
	while (buffer < 0)
	{
		// While we were sleeping on the lock, another thread might have free'd
		// enough memory for us to use, so try the allocation again - no point
		// using the emergency buffer if there is some real memory that we can
		// use...
		void *m = calloc(1, size);
		if (0 != m)
		{
			pthread_mutex_unlock(&emergency_malloc_lock);
			return static_cast<char*>(m);
		}
		for (int i=0 ; i<16 ; i++)
		{
			if (!buffer_allocated[i])
			{
				buffer = i;
				buffer_allocated[i] = true;
				break;
			}
		}
		// If there still isn't a buffer available, then sleep on the condition
		// variable.  This will be signalled when another thread releases one
		// of the emergency buffers.
		if (buffer < 0)
		{
			pthread_cond_wait(&emergency_malloc_wait, &emergency_malloc_lock);
		}
	}
	pthread_mutex_unlock(&emergency_malloc_lock);
	info->emergencyBuffersHeld++;
	return emergency_buffer + (1024 * buffer);
}

/**
 * Frees a buffer returned by emergency_malloc().
 *
 * Note: Neither this nor emergency_malloc() is particularly efficient.  This
 * should not matter, because neither will be called in normal operation - they
 * are only used when the program runs out of memory, which should not happen
 * often.
 */
static void emergency_malloc_free(char *ptr)
{
	int buffer = -1;
	// Find the buffer corresponding to this pointer.
	for (int i=0 ; i<16 ; i++)
	{
		if (ptr == static_cast<void*>(emergency_buffer + (1024 * i)))
		{
			buffer = i;
			break;
		}
	}
	assert(buffer >= 0 &&
	       "Trying to free something that is not an emergency buffer!");
	// emergency_malloc() is expected to return 0-initialized data.  We don't
	// zero the buffer when allocating it, because the static buffers will
	// begin life containing 0 values.
	memset(ptr, 0, 1024);
	// Signal the condition variable to wake up any threads that are blocking
	// waiting for some space in the emergency buffer
	pthread_mutex_lock(&emergency_malloc_lock);
	// In theory, we don't need to do this with the lock held.  In practice,
	// our array of bools will probably be updated using 32-bit or 64-bit
	// memory operations, so this update may clobber adjacent values.
	buffer_allocated[buffer] = false;
	pthread_cond_signal(&emergency_malloc_wait);
	pthread_mutex_unlock(&emergency_malloc_lock);
}

static char *alloc_or_die(size_t size)
{
	char *buffer = static_cast<char*>(calloc(1, size));

	// If calloc() doesn't want to give us any memory, try using an emergency
	// buffer.
	if (0 == buffer)
	{
		buffer = emergency_malloc(size);
		// This is only reached if the allocation is greater than 1KB, and
		// anyone throwing objects that big really should know better.  
		if (0 == buffer)
		{
			fprintf(stderr, "Out of memory attempting to allocate exception\n");
			std::terminate();
		}
	}
	return buffer;
}
static void free_exception(char *e)
{
	// If this allocation is within the address range of the emergency buffer,
	// don't call free() because it was not allocated with malloc()
	if ((e >= emergency_buffer) &&
	    (e < (emergency_buffer + sizeof(emergency_buffer))))
	{
		emergency_malloc_free(e);
	}
	else
	{
		free(e);
	}
}

#ifdef __LP64__
/**
 * There's an ABI bug in __cxa_exception: unwindHeader requires 16-byte
 * alignment but it was broken by the addition of the referenceCount.
 * The unwindHeader is at offset 0x58 in __cxa_exception.  In order to keep
 * compatibility with consumers of the broken __cxa_exception, explicitly add
 * padding on allocation (and account for it on free).
 */
static const int exception_alignment_padding = 8;
#else
static const int exception_alignment_padding = 0;
#endif

/**
 * Allocates an exception structure.  Returns a pointer to the space that can
 * be used to store an object of thrown_size bytes.  This function will use an
 * emergency buffer if malloc() fails, and may block if there are no such
 * buffers available.
 */
extern "C" void *__cxa_allocate_exception(size_t thrown_size)
{
	size_t size = exception_alignment_padding + sizeof(__cxa_exception) +
	    thrown_size;
	char *buffer = alloc_or_die(size);
	return buffer + exception_alignment_padding + sizeof(__cxa_exception);
}

extern "C" void *__cxa_allocate_dependent_exception(void)
{
	size_t size = exception_alignment_padding +
	    sizeof(__cxa_dependent_exception);
	char *buffer = alloc_or_die(size);
	return buffer + exception_alignment_padding +
	    sizeof(__cxa_dependent_exception);
}

/**
 * __cxa_free_exception() is called when an exception was thrown in between
 * calling __cxa_allocate_exception() and actually throwing the exception.
 * This happens when the object's copy constructor throws an exception.
 *
 * In this implementation, it is also called by __cxa_end_catch() and during
 * thread cleanup.
 */
extern "C" void __cxa_free_exception(void *thrown_exception)
{
	__cxa_exception *ex = reinterpret_cast<__cxa_exception*>(thrown_exception) - 1;
	// Free the object that was thrown, calling its destructor
	if (0 != ex->exceptionDestructor)
	{
		try
		{
			ex->exceptionDestructor(thrown_exception);
		}
		catch(...)
		{
			// FIXME: Check that this is really what the spec says to do.
			std::terminate();
		}
	}

	free_exception(reinterpret_cast<char*>(ex) -
	    exception_alignment_padding);
}

static void releaseException(__cxa_exception *exception)
{
	if (isDependentException(exception->unwindHeader.exception_class))
	{
		__cxa_free_dependent_exception(exception+1);
		return;
	}
	if (__sync_sub_and_fetch(&exception->referenceCount, 1) == 0)
	{
		// __cxa_free_exception() expects to be passed the thrown object,
		// which immediately follows the exception, not the exception
		// itself
		__cxa_free_exception(exception+1);
	}
}

void __cxa_free_dependent_exception(void *thrown_exception)
{
	__cxa_dependent_exception *ex = reinterpret_cast<__cxa_dependent_exception*>(thrown_exception) - 1;
	assert(isDependentException(ex->unwindHeader.exception_class));
	if (ex->primaryException)
	{
		releaseException(realExceptionFromException(reinterpret_cast<__cxa_exception*>(ex)));
	}
	free_exception(reinterpret_cast<char*>(ex) -
	    exception_alignment_padding);
}

/**
 * Callback function used with _Unwind_Backtrace().
 *
 * Prints a stack trace.  Used only for debugging help.
 *
 * Note: As of FreeBSD 8.1, dladd() still doesn't work properly, so this only
 * correctly prints function names from public, relocatable, symbols.
 */
static _Unwind_Reason_Code trace(struct _Unwind_Context *context, void *c)
{
	Dl_info myinfo;
	int mylookup =
		dladdr(reinterpret_cast<void *>(__cxa_current_exception_type), &myinfo);
	void *ip = reinterpret_cast<void*>(_Unwind_GetIP(context));
	Dl_info info;
	if (dladdr(ip, &info) != 0)
	{
		if (mylookup == 0 || strcmp(info.dli_fname, myinfo.dli_fname) != 0)
		{
			printf("%p:%s() in %s\n", ip, info.dli_sname, info.dli_fname);
		}
	}
	return _URC_CONTINUE_UNWIND;
}

/**
 * Report a failure that occurred when attempting to throw an exception.
 *
 * If the failure happened by falling off the end of the stack without finding
 * a handler, prints a back trace before aborting.
 */
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4)
extern "C" void *__cxa_begin_catch(void *e) throw();
#else
extern "C" void *__cxa_begin_catch(void *e);
#endif
static void report_failure(_Unwind_Reason_Code err, __cxa_exception *thrown_exception)
{
	switch (err)
	{
		default: break;
		case _URC_FATAL_PHASE1_ERROR:
			fprintf(stderr, "Fatal error during phase 1 unwinding\n");
			break;
#if !defined(__arm__) || defined(__ARM_DWARF_EH__)
		case _URC_FATAL_PHASE2_ERROR:
			fprintf(stderr, "Fatal error during phase 2 unwinding\n");
			break;
#endif
		case _URC_END_OF_STACK:
			__cxa_begin_catch (&(thrown_exception->unwindHeader));
 			std::terminate();
			fprintf(stderr, "Terminating due to uncaught exception %p", 
					static_cast<void*>(thrown_exception));
			thrown_exception = realExceptionFromException(thrown_exception);
			static const __class_type_info *e_ti =
				static_cast<const __class_type_info*>(&typeid(std::exception));
			const __class_type_info *throw_ti =
				dynamic_cast<const __class_type_info*>(thrown_exception->exceptionType);
			if (throw_ti)
			{
				std::exception *e =
					static_cast<std::exception*>(e_ti->cast_to(static_cast<void*>(thrown_exception+1),
							throw_ti));
				if (e)
				{
					fprintf(stderr, " '%s'", e->what());
				}
			}

			size_t bufferSize = 128;
			char *demangled = static_cast<char*>(malloc(bufferSize));
			const char *mangled = thrown_exception->exceptionType->name();
			int status;
			demangled = __cxa_demangle(mangled, demangled, &bufferSize, &status);
			fprintf(stderr, " of type %s\n", 
				status == 0 ? demangled : mangled);
			if (status == 0) { free(demangled); }
			// Print a back trace if no handler is found.
			// TODO: Make this optional
#ifndef __arm__
			_Unwind_Backtrace(trace, 0);
#endif

			// Just abort. No need to call std::terminate for the second time
			abort();
			break;
	}
	std::terminate();
}

static void throw_exception(__cxa_exception *ex)
{
	__cxa_thread_info *info = thread_info();
	ex->unexpectedHandler = info->unexpectedHandler;
	if (0 == ex->unexpectedHandler)
	{
		ex->unexpectedHandler = unexpectedHandler;
	}
	ex->terminateHandler  = info->terminateHandler;
	if (0 == ex->terminateHandler)
	{
		ex->terminateHandler = terminateHandler;
	}
	info->globals.uncaughtExceptions++;

	_Unwind_Reason_Code err = _Unwind_RaiseException(&ex->unwindHeader);
	// The _Unwind_RaiseException() function should not return, it should
	// unwind the stack past this function.  If it does return, then something
	// has gone wrong.
	report_failure(err, ex);
}


/**
 * ABI function for throwing an exception.  Takes the object to be thrown (the
 * pointer returned by __cxa_allocate_exception()), the type info for the
 * pointee, and the destructor (if there is one) as arguments.
 */
extern "C" void __cxa_throw(void *thrown_exception,
                            std::type_info *tinfo,
                            void(*dest)(void*))
{
	__cxa_exception *ex = reinterpret_cast<__cxa_exception*>(thrown_exception) - 1;

	ex->referenceCount = 1;
	ex->exceptionType = tinfo;
	
	ex->exceptionDestructor = dest;
	
	ex->unwindHeader.exception_class = exception_class;
	ex->unwindHeader.exception_cleanup = exception_cleanup;

	throw_exception(ex);
}

extern "C" void __cxa_rethrow_primary_exception(void* thrown_exception)
{
	if (NULL == thrown_exception) { return; }

	__cxa_exception *original = exceptionFromPointer(thrown_exception);
	__cxa_dependent_exception *ex = reinterpret_cast<__cxa_dependent_exception*>(__cxa_allocate_dependent_exception())-1;

	ex->primaryException = thrown_exception;
	__cxa_increment_exception_refcount(thrown_exception);

	ex->exceptionType = original->exceptionType;
	ex->unwindHeader.exception_class = dependent_exception_class;
	ex->unwindHeader.exception_cleanup = dependent_exception_cleanup;

	throw_exception(reinterpret_cast<__cxa_exception*>(ex));
}

extern "C" void *__cxa_current_primary_exception(void)
{
	__cxa_eh_globals* globals = __cxa_get_globals();
	__cxa_exception *ex = globals->caughtExceptions;

	if (0 == ex) { return NULL; }
	ex = realExceptionFromException(ex);
	__sync_fetch_and_add(&ex->referenceCount, 1);
	return ex + 1;
}

extern "C" void __cxa_increment_exception_refcount(void* thrown_exception)
{
	if (NULL == thrown_exception) { return; }
	__cxa_exception *ex = static_cast<__cxa_exception*>(thrown_exception) - 1;
	if (isDependentException(ex->unwindHeader.exception_class)) { return; }
	__sync_fetch_and_add(&ex->referenceCount, 1);
}
extern "C" void __cxa_decrement_exception_refcount(void* thrown_exception)
{
	if (NULL == thrown_exception) { return; }
	__cxa_exception *ex = static_cast<__cxa_exception*>(thrown_exception) - 1;
	releaseException(ex);
}

/**
 * ABI function.  Rethrows the current exception.  Does not remove the
 * exception from the stack or decrement its handler count - the compiler is
 * expected to set the landing pad for this function to the end of the catch
 * block, and then call _Unwind_Resume() to continue unwinding once
 * __cxa_end_catch() has been called and any cleanup code has been run.
 */
extern "C" void __cxa_rethrow()
{
	__cxa_thread_info *ti = thread_info();
	__cxa_eh_globals *globals = &ti->globals;
	// Note: We don't remove this from the caught list here, because
	// __cxa_end_catch will be called when we unwind out of the try block.  We
	// could probably make this faster by providing an alternative rethrow
	// function and ensuring that all cleanup code is run before calling it, so
	// we can skip the top stack frame when unwinding.
	__cxa_exception *ex = globals->caughtExceptions;

	if (0 == ex)
	{
		fprintf(stderr,
		        "Attempting to rethrow an exception that doesn't exist!\n");
		std::terminate();
	}

	if (ti->foreign_exception_state != __cxa_thread_info::none)
	{
		ti->foreign_exception_state = __cxa_thread_info::rethrown;
		_Unwind_Exception *e = reinterpret_cast<_Unwind_Exception*>(ex);
		_Unwind_Reason_Code err = _Unwind_Resume_or_Rethrow(e);
		report_failure(err, ex);
		return;
	}

	assert(ex->handlerCount > 0 && "Rethrowing uncaught exception!");

	// ex->handlerCount will be decremented in __cxa_end_catch in enclosing
	// catch block
	
	// Make handler count negative. This will tell __cxa_end_catch that
	// exception was rethrown and exception object should not be destroyed
	// when handler count become zero
	ex->handlerCount = -ex->handlerCount;

	// Continue unwinding the stack with this exception.  This should unwind to
	// the place in the caller where __cxa_end_catch() is called.  The caller
	// will then run cleanup code and bounce the exception back with
	// _Unwind_Resume().
	_Unwind_Reason_Code err = _Unwind_Resume_or_Rethrow(&ex->unwindHeader);
	report_failure(err, ex);
}

/**
 * Returns the type_info object corresponding to the filter.
 */
static std::type_info *get_type_info_entry(_Unwind_Context *context,
                                           dwarf_eh_lsda *lsda,
                                           int filter)
{
	// Get the address of the record in the table.
	dw_eh_ptr_t record = lsda->type_table - 
		dwarf_size_of_fixed_size_field(lsda->type_table_encoding)*filter;
	//record -= 4;
	dw_eh_ptr_t start = record;
	// Read the value, but it's probably an indirect reference...
	int64_t offset = read_value(lsda->type_table_encoding, &record);

	// (If the entry is 0, don't try to dereference it.  That would be bad.)
	if (offset == 0) { return 0; }

	// ...so we need to resolve it
	return reinterpret_cast<std::type_info*>(resolve_indirect_value(context,
			lsda->type_table_encoding, offset, start));
}



/**
 * Checks the type signature found in a handler against the type of the thrown
 * object.  If ex is 0 then it is assumed to be a foreign exception and only
 * matches cleanups.
 */
static bool check_type_signature(__cxa_exception *ex,
                                 const std::type_info *type,
                                 void *&adjustedPtr)
{
	void *exception_ptr = static_cast<void*>(ex+1);
	const std::type_info *ex_type = ex ? ex->exceptionType : 0;

	bool is_ptr = ex ? ex_type->__is_pointer_p() : false;
	if (is_ptr)
	{
		exception_ptr = *static_cast<void**>(exception_ptr);
	}
	// Always match a catchall, even with a foreign exception
	//
	// Note: A 0 here is a catchall, not a cleanup, so we return true to
	// indicate that we found a catch.
	if (0 == type)
	{
		if (ex)
		{
			adjustedPtr = exception_ptr;
		}
		return true;
	}

	if (0 == ex) { return false; }

	// If the types are the same, no casting is needed.
	if (*type == *ex_type)
	{
		adjustedPtr = exception_ptr;
		return true;
	}


	if (type->__do_catch(ex_type, &exception_ptr, 1))
	{
		adjustedPtr = exception_ptr;
		return true;
	}

	return false;
}
/**
 * Checks whether the exception matches the type specifiers in this action
 * record.  If the exception only matches cleanups, then this returns false.
 * If it matches a catch (including a catchall) then it returns true.
 *
 * The selector argument is used to return the selector that is passed in the
 * second exception register when installing the context.
 */
static handler_type check_action_record(_Unwind_Context *context,
                                        dwarf_eh_lsda *lsda,
                                        dw_eh_ptr_t action_record,
                                        __cxa_exception *ex,
                                        unsigned long *selector,
                                        void *&adjustedPtr)
{
	if (!action_record) { return handler_cleanup; }
	handler_type found = handler_none;
	while (action_record)
	{
		int filter = read_sleb128(&action_record);
		dw_eh_ptr_t action_record_offset_base = action_record;
		int displacement = read_sleb128(&action_record);
		action_record = displacement ? 
			action_record_offset_base + displacement : 0;
		// We only check handler types for C++ exceptions - foreign exceptions
		// are only allowed for cleanups and catchalls.
		if (filter > 0)
		{
			std::type_info *handler_type = get_type_info_entry(context, lsda, filter);
			if (check_type_signature(ex, handler_type, adjustedPtr))
			{
				*selector = filter;
				return handler_catch;
			}
		}
		else if (filter < 0 && 0 != ex)
		{
			bool matched = false;
			*selector = filter;
#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
			filter++;
			std::type_info *handler_type = get_type_info_entry(context, lsda, filter--);
			while (handler_type)
			{
				if (check_type_signature(ex, handler_type, adjustedPtr))
				{
					matched = true;
					break;
				}
				handler_type = get_type_info_entry(context, lsda, filter--);
			}
#else
			unsigned char *type_index = reinterpret_cast<unsigned char*>(lsda->type_table) - filter - 1;
			while (*type_index)
			{
				std::type_info *handler_type = get_type_info_entry(context, lsda, *(type_index++));
				// If the exception spec matches a permitted throw type for
				// this function, don't report a handler - we are allowed to
				// propagate this exception out.
				if (check_type_signature(ex, handler_type, adjustedPtr))
				{
					matched = true;
					break;
				}
			}
#endif
			if (matched) { continue; }
			// If we don't find an allowed exception spec, we need to install
			// the context for this action.  The landing pad will then call the
			// unexpected exception function.  Treat this as a catch
			return handler_catch;
		}
		else if (filter == 0)
		{
			*selector = filter;
			found = handler_cleanup;
		}
	}
	return found;
}

static void pushCleanupException(_Unwind_Exception *exceptionObject,
                                 __cxa_exception *ex)
{
#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
	__cxa_thread_info *info = thread_info_fast();
	if (ex)
	{
		ex->cleanupCount++;
		if (ex->cleanupCount > 1)
		{
			assert(exceptionObject == info->currentCleanup);
			return;
		}
		ex->nextCleanup = info->currentCleanup;
	}
	info->currentCleanup = exceptionObject;
#endif
}

/**
 * The exception personality function.  This is referenced in the unwinding
 * DWARF metadata and is called by the unwind library for each C++ stack frame
 * containing catch or cleanup code.
 */
extern "C"
BEGIN_PERSONALITY_FUNCTION(__gxx_personality_v0)
	// This personality function is for version 1 of the ABI.  If you use it
	// with a future version of the ABI, it won't know what to do, so it
	// reports a fatal error and give up before it breaks anything.
	if (1 != version)
	{
		return _URC_FATAL_PHASE1_ERROR;
	}
	__cxa_exception *ex = 0;
	__cxa_exception *realEx = 0;

	// If this exception is throw by something else then we can't make any
	// assumptions about its layout beyond the fields declared in
	// _Unwind_Exception.
	bool foreignException = !isCXXException(exceptionClass);

	// If this isn't a foreign exception, then we have a C++ exception structure
	if (!foreignException)
	{
		ex = exceptionFromPointer(exceptionObject);
		realEx = realExceptionFromException(ex);
	}

#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
	unsigned char *lsda_addr =
		static_cast<unsigned char*>(_Unwind_GetLanguageSpecificData(context));
#else
	unsigned char *lsda_addr =
		reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(_Unwind_GetLanguageSpecificData(context)));
#endif

	// No LSDA implies no landing pads - try the next frame
	if (0 == lsda_addr) { return continueUnwinding(exceptionObject, context); }

	// These two variables define how the exception will be handled.
	dwarf_eh_action action = {0};
	unsigned long selector = 0;
	
	// During the search phase, we do a complete lookup.  If we return
	// _URC_HANDLER_FOUND, then the phase 2 unwind will call this function with
	// a _UA_HANDLER_FRAME action, telling us to install the handler frame.  If
	// we return _URC_CONTINUE_UNWIND, we may be called again later with a
	// _UA_CLEANUP_PHASE action for this frame.
	//
	// The point of the two-stage unwind allows us to entirely avoid any stack
	// unwinding if there is no handler.  If there are just cleanups found,
	// then we can just panic call an abort function.
	//
	// Matching a handler is much more expensive than matching a cleanup,
	// because we don't need to bother doing type comparisons (or looking at
	// the type table at all) for a cleanup.  This means that there is no need
	// to cache the result of finding a cleanup, because it's (quite) quick to
	// look it up again from the action table.
	if (actions & _UA_SEARCH_PHASE)
	{
		struct dwarf_eh_lsda lsda = parse_lsda(context, lsda_addr);

		if (!dwarf_eh_find_callsite(context, &lsda, &action))
		{
			// EH range not found. This happens if exception is thrown and not
			// caught inside a cleanup (destructor).  We should call
			// terminate() in this case.  The catchTemp (landing pad) field of
			// exception object will contain null when personality function is
			// called with _UA_HANDLER_FRAME action for phase 2 unwinding.  
			return _URC_HANDLER_FOUND;
		}

		handler_type found_handler = check_action_record(context, &lsda,
				action.action_record, realEx, &selector, ex->adjustedPtr);
		// If there's no action record, we've only found a cleanup, so keep
		// searching for something real
		if (found_handler == handler_catch)
		{
			// Cache the results for the phase 2 unwind, if we found a handler
			// and this is not a foreign exception.
			if (ex)
			{
				saveLandingPad(context, exceptionObject, ex, selector, action.landing_pad);
				ex->languageSpecificData = reinterpret_cast<const char*>(lsda_addr);
				ex->actionRecord = reinterpret_cast<const char*>(action.action_record);
				// ex->adjustedPtr is set when finding the action record.
			}
			return _URC_HANDLER_FOUND;
		}
		return continueUnwinding(exceptionObject, context);
	}


	// If this is a foreign exception, we didn't have anywhere to cache the
	// lookup stuff, so we need to do it again.  If this is either a forced
	// unwind, a foreign exception, or a cleanup, then we just install the
	// context for a cleanup.
	if (!(actions & _UA_HANDLER_FRAME))
	{
		// cleanup
		struct dwarf_eh_lsda lsda = parse_lsda(context, lsda_addr);
		dwarf_eh_find_callsite(context, &lsda, &action);
		if (0 == action.landing_pad) { return continueUnwinding(exceptionObject, context); }
		handler_type found_handler = check_action_record(context, &lsda,
				action.action_record, realEx, &selector, ex->adjustedPtr);
		// Ignore handlers this time.
		if (found_handler != handler_cleanup) { return continueUnwinding(exceptionObject, context); }
		pushCleanupException(exceptionObject, ex);
	}
	else if (foreignException)
	{
		struct dwarf_eh_lsda lsda = parse_lsda(context, lsda_addr);
		dwarf_eh_find_callsite(context, &lsda, &action);
		check_action_record(context, &lsda, action.action_record, realEx,
				&selector, ex->adjustedPtr);
	}
	else if (ex->catchTemp == 0)
	{
		// Uncaught exception in cleanup, calling terminate
		std::terminate();
	}
	else
	{
		// Restore the saved info if we saved some last time.
		loadLandingPad(context, exceptionObject, ex, &selector, &action.landing_pad);
		ex->catchTemp = 0;
		ex->handlerSwitchValue = 0;
	}


	_Unwind_SetIP(context, reinterpret_cast<unsigned long>(action.landing_pad));
	_Unwind_SetGR(context, __builtin_eh_return_data_regno(0),
	              reinterpret_cast<unsigned long>(exceptionObject));
	_Unwind_SetGR(context, __builtin_eh_return_data_regno(1), selector);

	return _URC_INSTALL_CONTEXT;
}

/**
 * ABI function called when entering a catch statement.  The argument is the
 * pointer passed out of the personality function.  This is always the start of
 * the _Unwind_Exception object.  The return value for this function is the
 * pointer to the caught exception, which is either the adjusted pointer (for
 * C++ exceptions) of the unadjusted pointer (for foreign exceptions).
 */
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4)
extern "C" void *__cxa_begin_catch(void *e) throw()
#else
extern "C" void *__cxa_begin_catch(void *e)
#endif
{
	// We can't call the fast version here, because if the first exception that
	// we see is a foreign exception then we won't have called it yet.
	__cxa_thread_info *ti = thread_info();
	__cxa_eh_globals *globals = &ti->globals;
	globals->uncaughtExceptions--;
	_Unwind_Exception *exceptionObject = static_cast<_Unwind_Exception*>(e);

	if (isCXXException(exceptionObject->exception_class))
	{
		__cxa_exception *ex =  exceptionFromPointer(exceptionObject);

		if (ex->handlerCount == 0)
		{
			// Add this to the front of the list of exceptions being handled
			// and increment its handler count so that it won't be deleted
			// prematurely.
			ex->nextException = globals->caughtExceptions;
			globals->caughtExceptions = ex;
		}

		if (ex->handlerCount < 0)
		{
			// Rethrown exception is catched before end of catch block.
			// Clear the rethrow flag (make value positive) - we are allowed
			// to delete this exception at the end of the catch block, as long
			// as it isn't thrown again later.
			
			// Code pattern:
			//
			// try {
			//     throw x;
			// }
			// catch() {
			//     try {
			//         throw;
			//     }
			//     catch() {
			//         __cxa_begin_catch() <- we are here
			//     }
			// }
			ex->handlerCount = -ex->handlerCount + 1;
		}
		else
		{
			ex->handlerCount++;
		}
		ti->foreign_exception_state = __cxa_thread_info::none;
		
		return ex->adjustedPtr;
	}
	else
	{
		// If this is a foreign exception, then we need to be able to
		// store it.  We can't chain foreign exceptions, so we give up
		// if there are already some outstanding ones.
		if (globals->caughtExceptions != 0)
		{
			std::terminate();
		}
		globals->caughtExceptions = reinterpret_cast<__cxa_exception*>(exceptionObject);
		ti->foreign_exception_state = __cxa_thread_info::caught;
	}
	// exceptionObject is the pointer to the _Unwind_Exception within the
	// __cxa_exception.  The throw object is after this
	return (reinterpret_cast<char*>(exceptionObject) + sizeof(_Unwind_Exception));
}



/**
 * ABI function called when exiting a catch block.  This will free the current
 * exception if it is no longer referenced in other catch blocks.
 */
extern "C" void __cxa_end_catch()
{
	// We can call the fast version here because the slow version is called in
	// __cxa_throw(), which must have been called before we end a catch block
	__cxa_thread_info *ti = thread_info_fast();
	__cxa_eh_globals *globals = &ti->globals;
	__cxa_exception *ex = globals->caughtExceptions;

	assert(0 != ex && "Ending catch when no exception is on the stack!");
	
	if (ti->foreign_exception_state != __cxa_thread_info::none)
	{
		if (ti->foreign_exception_state != __cxa_thread_info::rethrown)
		{
			_Unwind_Exception *e = reinterpret_cast<_Unwind_Exception*>(ti->globals.caughtExceptions);
			if (e->exception_cleanup)
				e->exception_cleanup(_URC_FOREIGN_EXCEPTION_CAUGHT, e);
		}
		globals->caughtExceptions = 0;
		ti->foreign_exception_state = __cxa_thread_info::none;
		return;
	}

	bool deleteException = true;

	if (ex->handlerCount < 0)
	{
		// exception was rethrown. Exception should not be deleted even if
		// handlerCount become zero.
		// Code pattern:
		// try {
		//     throw x;
		// }
		// catch() {
		//     {
		//         throw;
		//     }
		//     cleanup {
		//         __cxa_end_catch();   <- we are here
		//     }
		// }
		//
		
		ex->handlerCount++;
		deleteException = false;
	}
	else
	{
		ex->handlerCount--;
	}

	if (ex->handlerCount == 0)
	{
		globals->caughtExceptions = ex->nextException;
		if (deleteException)
		{
			releaseException(ex);
		}
	}
}

/**
 * ABI function.  Returns the type of the current exception.
 */
extern "C" std::type_info *__cxa_current_exception_type()
{
	__cxa_eh_globals *globals = __cxa_get_globals();
	__cxa_exception *ex = globals->caughtExceptions;
	return ex ? ex->exceptionType : 0;
}

/**
 * ABI function, called when an exception specification is violated.
 *
 * This function does not return.
 */
extern "C" void __cxa_call_unexpected(void*exception) 
{
	_Unwind_Exception *exceptionObject = static_cast<_Unwind_Exception*>(exception);
	if (exceptionObject->exception_class == exception_class)
	{
		__cxa_exception *ex =  exceptionFromPointer(exceptionObject);
		if (ex->unexpectedHandler)
		{
			ex->unexpectedHandler();
			// Should not be reached.  
			abort();
		}
	}
	std::unexpected();
	// Should not be reached.  
	abort();
}

/**
 * ABI function, returns the adjusted pointer to the exception object.
 */
extern "C" void *__cxa_get_exception_ptr(void *exceptionObject)
{
	return exceptionFromPointer(exceptionObject)->adjustedPtr;
}

/**
 * As an extension, we provide the ability for the unexpected and terminate
 * handlers to be thread-local.  We default to the standards-compliant
 * behaviour where they are global.
 */
static bool thread_local_handlers = false;


namespace pathscale
{
	/**
	 * Sets whether unexpected and terminate handlers should be thread-local.
	 */
	void set_use_thread_local_handlers(bool flag) throw()
	{
		thread_local_handlers = flag;
	}
	/**
	 * Sets a thread-local unexpected handler.  
	 */
	unexpected_handler set_unexpected(unexpected_handler f) throw()
	{
		static __cxa_thread_info *info = thread_info();
		unexpected_handler old = info->unexpectedHandler;
		info->unexpectedHandler = f;
		return old;
	}
	/**
	 * Sets a thread-local terminate handler.  
	 */
	terminate_handler set_terminate(terminate_handler f) throw()
	{
		static __cxa_thread_info *info = thread_info();
		terminate_handler old = info->terminateHandler;
		info->terminateHandler = f;
		return old;
	}
}

namespace std
{
	/**
	 * Sets the function that will be called when an exception specification is
	 * violated.
	 */
	unexpected_handler set_unexpected(unexpected_handler f) throw()
	{
		if (thread_local_handlers) { return pathscale::set_unexpected(f); }

		return ATOMIC_SWAP(&unexpectedHandler, f);
	}
	/**
	 * Sets the function that is called to terminate the program.
	 */
	terminate_handler set_terminate(terminate_handler f) throw()
	{
		if (thread_local_handlers) { return pathscale::set_terminate(f); }

		return ATOMIC_SWAP(&terminateHandler, f);
	}
	/**
	 * Terminates the program, calling a custom terminate implementation if
	 * required.
	 */
	void terminate()
	{
		static __cxa_thread_info *info = thread_info();
		if (0 != info && 0 != info->terminateHandler)
		{
			info->terminateHandler();
			// Should not be reached - a terminate handler is not expected to
			// return.
			abort();
		}
		terminateHandler();
	}
	/**
	 * Called when an unexpected exception is encountered (i.e. an exception
	 * violates an exception specification).  This calls abort() unless a
	 * custom handler has been set..
	 */
	void unexpected()
	{
		static __cxa_thread_info *info = thread_info();
		if (0 != info && 0 != info->unexpectedHandler)
		{
			info->unexpectedHandler();
			// Should not be reached - a terminate handler is not expected to
			// return.
			abort();
		}
		unexpectedHandler();
	}
	/**
	 * Returns whether there are any exceptions currently being thrown that
	 * have not been caught.  This can occur inside a nested catch statement.
	 */
	bool uncaught_exception() throw()
	{
		__cxa_thread_info *info = thread_info();
		return info->globals.uncaughtExceptions != 0;
	}
	/**
	 * Returns the number of exceptions currently being thrown that have not
	 * been caught.  This can occur inside a nested catch statement.
	 */
	int uncaught_exceptions() throw()
	{
		__cxa_thread_info *info = thread_info();
		return info->globals.uncaughtExceptions;
	}
	/**
	 * Returns the current unexpected handler.
	 */
	unexpected_handler get_unexpected() throw()
	{
		__cxa_thread_info *info = thread_info();
		if (info->unexpectedHandler)
		{
			return info->unexpectedHandler;
		}
		return ATOMIC_LOAD(&unexpectedHandler);
	}
	/**
	 * Returns the current terminate handler.
	 */
	terminate_handler get_terminate() throw()
	{
		__cxa_thread_info *info = thread_info();
		if (info->terminateHandler)
		{
			return info->terminateHandler;
		}
		return ATOMIC_LOAD(&terminateHandler);
	}
}
#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
extern "C" _Unwind_Exception *__cxa_get_cleanup(void)
{
	__cxa_thread_info *info = thread_info_fast();
	_Unwind_Exception *exceptionObject = info->currentCleanup;
	if (isCXXException(exceptionObject->exception_class))
	{
		__cxa_exception *ex =  exceptionFromPointer(exceptionObject);
		ex->cleanupCount--;
		if (ex->cleanupCount == 0)
		{
			info->currentCleanup = ex->nextCleanup;
			ex->nextCleanup = 0;
		}
	}
	else
	{
		info->currentCleanup = 0;
	}
	return exceptionObject;
}

asm (
".pushsection .text.__cxa_end_cleanup    \n"
".global __cxa_end_cleanup               \n"
".type __cxa_end_cleanup, \"function\"   \n"
"__cxa_end_cleanup:                      \n"
"	push {r1, r2, r3, r4}                \n"
"	bl __cxa_get_cleanup                 \n"
"	push {r1, r2, r3, r4}                \n"
"	b _Unwind_Resume                     \n"
"	bl abort                             \n"
".popsection                             \n"
);
#endif
