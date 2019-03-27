
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef _ITTNOTIFY_H_
#define _ITTNOTIFY_H_

/**
@file
@brief Public User API functions and types
@mainpage

The ITT API is used to annotate a user's program with additional information
that can be used by correctness and performance tools. The user inserts
calls in their program. Those calls generate information that is collected
at runtime, and used by Intel(R) Threading Tools.

@section API Concepts
The following general concepts are used throughout the API.

@subsection Unicode Support
Many API functions take character string arguments. On Windows, there
are two versions of each such function. The function name is suffixed
by W if Unicode support is enabled, and by A otherwise. Any API function
that takes a character string argument adheres to this convention.

@subsection Conditional Compilation
Many users prefer having an option to modify ITT API code when linking it
inside their runtimes. ITT API header file provides a mechanism to replace
ITT API function names inside your code with empty strings. To do this,
define the macros INTEL_NO_ITTNOTIFY_API during compilation and remove the
static library from the linker script.

@subsection Domains
[see domains]
Domains provide a way to separate notification for different modules or
libraries in a program. Domains are specified by dotted character strings,
e.g. TBB.Internal.Control.

A mechanism (to be specified) is provided to enable and disable
domains. By default, all domains are enabled.
@subsection Named Entities and Instances
Named entities (frames, regions, tasks, and markers) communicate
information about the program to the analysis tools. A named entity often
refers to a section of program code, or to some set of logical concepts
that the programmer wants to group together.

Named entities relate to the programmer's static view of the program. When
the program actually executes, many instances of a given named entity
may be created.

The API annotations denote instances of named entities. The actual
named entities are displayed using the analysis tools. In other words,
the named entities come into existence when instances are created.

Instances of named entities may have instance identifiers (IDs). Some
API calls use instance identifiers to create relationships between
different instances of named entities. Other API calls associate data
with instances of named entities.

Some named entities must always have instance IDs. In particular, regions
and frames always have IDs. Task and markers need IDs only if the ID is
needed in another API call (such as adding a relation or metadata).

The lifetime of instance IDs is distinct from the lifetime of
instances. This allows various relationships to be specified separate
from the actual execution of instances. This flexibility comes at the
expense of extra API calls.

The same ID may not be reused for different instances, unless a previous
[ref] __itt_id_destroy call for that ID has been issued.
*/

/** @cond exclude_from_documentation */
#ifndef ITT_OS_WIN
#  define ITT_OS_WIN   1
#endif /* ITT_OS_WIN */

#ifndef ITT_OS_LINUX
#  define ITT_OS_LINUX 2
#endif /* ITT_OS_LINUX */

#ifndef ITT_OS_MAC
#  define ITT_OS_MAC   3
#endif /* ITT_OS_MAC */

#ifndef ITT_OS_FREEBSD
#  define ITT_OS_FREEBSD   4
#endif /* ITT_OS_FREEBSD */

#ifndef ITT_OS
#  if defined WIN32 || defined _WIN32
#    define ITT_OS ITT_OS_WIN
#  elif defined( __APPLE__ ) && defined( __MACH__ )
#    define ITT_OS ITT_OS_MAC
#  elif defined( __FreeBSD__ )
#    define ITT_OS ITT_OS_FREEBSD
#  else
#    define ITT_OS ITT_OS_LINUX
#  endif
#endif /* ITT_OS */

#ifndef ITT_PLATFORM_WIN
#  define ITT_PLATFORM_WIN 1
#endif /* ITT_PLATFORM_WIN */

#ifndef ITT_PLATFORM_POSIX
#  define ITT_PLATFORM_POSIX 2
#endif /* ITT_PLATFORM_POSIX */

#ifndef ITT_PLATFORM_MAC
#  define ITT_PLATFORM_MAC 3
#endif /* ITT_PLATFORM_MAC */

#ifndef ITT_PLATFORM_FREEBSD
#  define ITT_PLATFORM_FREEBSD 4
#endif /* ITT_PLATFORM_FREEBSD */

#ifndef ITT_PLATFORM
#  if ITT_OS==ITT_OS_WIN
#    define ITT_PLATFORM ITT_PLATFORM_WIN
#  elif ITT_OS==ITT_OS_MAC
#    define ITT_PLATFORM ITT_PLATFORM_MAC
#  elif ITT_OS==ITT_OS_FREEBSD
#    define ITT_PLATFORM ITT_PLATFORM_FREEBSD
#  else
#    define ITT_PLATFORM ITT_PLATFORM_POSIX
#  endif
#endif /* ITT_PLATFORM */

#if defined(_UNICODE) && !defined(UNICODE)
#define UNICODE
#endif

#include <stddef.h>
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#include <tchar.h>
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#include <stdint.h>
#if defined(UNICODE) || defined(_UNICODE)
#include <wchar.h>
#endif /* UNICODE || _UNICODE */
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

#ifndef ITTAPI_CDECL
#  if ITT_PLATFORM==ITT_PLATFORM_WIN
#    define ITTAPI_CDECL __cdecl
#  else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#    if defined _M_IX86 || defined __i386__
#      define ITTAPI_CDECL __attribute__ ((cdecl))
#    else  /* _M_IX86 || __i386__ */
#      define ITTAPI_CDECL /* actual only on x86 platform */
#    endif /* _M_IX86 || __i386__ */
#  endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* ITTAPI_CDECL */

#ifndef STDCALL
#  if ITT_PLATFORM==ITT_PLATFORM_WIN
#    define STDCALL __stdcall
#  else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#    if defined _M_IX86 || defined __i386__
#      define STDCALL __attribute__ ((stdcall))
#    else  /* _M_IX86 || __i386__ */
#      define STDCALL /* supported only on x86 platform */
#    endif /* _M_IX86 || __i386__ */
#  endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* STDCALL */

#define ITTAPI    ITTAPI_CDECL
#define LIBITTAPI ITTAPI_CDECL

/* TODO: Temporary for compatibility! */
#define ITTAPI_CALL    ITTAPI_CDECL
#define LIBITTAPI_CALL ITTAPI_CDECL

#if ITT_PLATFORM==ITT_PLATFORM_WIN
/* use __forceinline (VC++ specific) */
#define ITT_INLINE           __forceinline
#define ITT_INLINE_ATTRIBUTE /* nothing */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
/*
 * Generally, functions are not inlined unless optimization is specified.
 * For functions declared inline, this attribute inlines the function even
 * if no optimization level was specified.
 */
#ifdef __STRICT_ANSI__
#define ITT_INLINE           static
#define ITT_INLINE_ATTRIBUTE __attribute__((unused))
#else  /* __STRICT_ANSI__ */
#define ITT_INLINE           static inline
#define ITT_INLINE_ATTRIBUTE __attribute__((always_inline, unused))
#endif /* __STRICT_ANSI__ */
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
/** @endcond */

#ifdef INTEL_ITTNOTIFY_ENABLE_LEGACY
#  if ITT_PLATFORM==ITT_PLATFORM_WIN
#    pragma message("WARNING!!! Deprecated API is used. Please undefine INTEL_ITTNOTIFY_ENABLE_LEGACY macro")
#  else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#    warning "Deprecated API is used. Please undefine INTEL_ITTNOTIFY_ENABLE_LEGACY macro"
#  endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#  include "legacy/ittnotify.h"
#endif /* INTEL_ITTNOTIFY_ENABLE_LEGACY */

/** @cond exclude_from_documentation */
/* Helper macro for joining tokens */
#define ITT_JOIN_AUX(p,n) p##n
#define ITT_JOIN(p,n)     ITT_JOIN_AUX(p,n)

#ifdef ITT_MAJOR
#undef ITT_MAJOR
#endif
#ifdef ITT_MINOR
#undef ITT_MINOR
#endif
#define ITT_MAJOR     3
#define ITT_MINOR     0

/* Standard versioning of a token with major and minor version numbers */
#define ITT_VERSIONIZE(x)    \
    ITT_JOIN(x,              \
    ITT_JOIN(_,              \
    ITT_JOIN(ITT_MAJOR,      \
    ITT_JOIN(_, ITT_MINOR))))

#ifndef INTEL_ITTNOTIFY_PREFIX
#  define INTEL_ITTNOTIFY_PREFIX __itt_
#endif /* INTEL_ITTNOTIFY_PREFIX */
#ifndef INTEL_ITTNOTIFY_POSTFIX
#  define INTEL_ITTNOTIFY_POSTFIX _ptr_
#endif /* INTEL_ITTNOTIFY_POSTFIX */

#define ITTNOTIFY_NAME_AUX(n) ITT_JOIN(INTEL_ITTNOTIFY_PREFIX,n)
#define ITTNOTIFY_NAME(n)     ITT_VERSIONIZE(ITTNOTIFY_NAME_AUX(ITT_JOIN(n,INTEL_ITTNOTIFY_POSTFIX)))

#define ITTNOTIFY_VOID(n) (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)
#define ITTNOTIFY_DATA(n) (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)

#define ITTNOTIFY_VOID_D0(n,d)       (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d)
#define ITTNOTIFY_VOID_D1(n,d,x)     (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x)
#define ITTNOTIFY_VOID_D2(n,d,x,y)   (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y)
#define ITTNOTIFY_VOID_D3(n,d,x,y,z) (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y,z)
#define ITTNOTIFY_VOID_D4(n,d,x,y,z,a)     (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y,z,a)
#define ITTNOTIFY_VOID_D5(n,d,x,y,z,a,b)   (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y,z,a,b)
#define ITTNOTIFY_VOID_D6(n,d,x,y,z,a,b,c) (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y,z,a,b,c)
#define ITTNOTIFY_DATA_D0(n,d)       (!(d)->flags) ?       0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d)
#define ITTNOTIFY_DATA_D1(n,d,x)     (!(d)->flags) ?       0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x)
#define ITTNOTIFY_DATA_D2(n,d,x,y)   (!(d)->flags) ?       0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y)
#define ITTNOTIFY_DATA_D3(n,d,x,y,z) (!(d)->flags) ?       0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y,z)
#define ITTNOTIFY_DATA_D4(n,d,x,y,z,a)     (!(d)->flags) ? 0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y,z,a)
#define ITTNOTIFY_DATA_D5(n,d,x,y,z,a,b)   (!(d)->flags) ? 0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y,z,a,b)
#define ITTNOTIFY_DATA_D6(n,d,x,y,z,a,b,c) (!(d)->flags) ? 0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y,z,a,b,c)

#ifdef ITT_STUB
#undef ITT_STUB
#endif
#ifdef ITT_STUBV
#undef ITT_STUBV
#endif
#define ITT_STUBV(api,type,name,args)                             \
    typedef type (api* ITT_JOIN(ITTNOTIFY_NAME(name),_t)) args;   \
    extern ITT_JOIN(ITTNOTIFY_NAME(name),_t) ITTNOTIFY_NAME(name);
#define ITT_STUB ITT_STUBV
/** @endcond */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @cond exclude_from_gpa_documentation */
/**
 * @defgroup public Public API
 * @{
 * @}
 */

/**
 * @defgroup control Collection Control
 * @ingroup public
 * General behavior: application continues to run, but no profiling information is being collected
 *
 * Pausing occurs not only for the current thread but for all process as well as spawned processes
 * - Intel(R) Parallel Inspector and Intel(R) Inspector XE:
 *   - Does not analyze or report errors that involve memory access.
 *   - Other errors are reported as usual. Pausing data collection in
 *     Intel(R) Parallel Inspector and Intel(R) Inspector XE
 *     only pauses tracing and analyzing memory access.
 *     It does not pause tracing or analyzing threading APIs.
 *   .
 * - Intel(R) Parallel Amplifier and Intel(R) VTune(TM) Amplifier XE:
 *   - Does continue to record when new threads are started.
 *   .
 * - Other effects:
 *   - Possible reduction of runtime overhead.
 *   .
 * @{
 */
/** @brief Pause collection */
void ITTAPI __itt_pause(void);
/** @brief Resume collection */
void ITTAPI __itt_resume(void);
/** @brief Detach collection */
void ITTAPI __itt_detach(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, pause,  (void))
ITT_STUBV(ITTAPI, void, resume, (void))
ITT_STUBV(ITTAPI, void, detach, (void))
#define __itt_pause      ITTNOTIFY_VOID(pause)
#define __itt_pause_ptr  ITTNOTIFY_NAME(pause)
#define __itt_resume     ITTNOTIFY_VOID(resume)
#define __itt_resume_ptr ITTNOTIFY_NAME(resume)
#define __itt_detach     ITTNOTIFY_VOID(detach)
#define __itt_detach_ptr ITTNOTIFY_NAME(detach)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_pause()
#define __itt_pause_ptr  0
#define __itt_resume()
#define __itt_resume_ptr 0
#define __itt_detach()
#define __itt_detach_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_pause_ptr  0
#define __itt_resume_ptr 0
#define __itt_detach_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} control group */
/** @endcond */

/**
 * @defgroup threads Threads
 * @ingroup public
 * Give names to threads
 * @{
 */
/**
 * @brief Sets thread name of calling thread
 * @param[in] name - name of thread
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
void ITTAPI __itt_thread_set_nameA(const char    *name);
void ITTAPI __itt_thread_set_nameW(const wchar_t *name);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_thread_set_name     __itt_thread_set_nameW
#  define __itt_thread_set_name_ptr __itt_thread_set_nameW_ptr
#else /* UNICODE */
#  define __itt_thread_set_name     __itt_thread_set_nameA
#  define __itt_thread_set_name_ptr __itt_thread_set_nameA_ptr
#endif /* UNICODE */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
void ITTAPI __itt_thread_set_name(const char *name);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, thread_set_nameA, (const char    *name))
ITT_STUBV(ITTAPI, void, thread_set_nameW, (const wchar_t *name))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, thread_set_name,  (const char    *name))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_thread_set_nameA     ITTNOTIFY_VOID(thread_set_nameA)
#define __itt_thread_set_nameA_ptr ITTNOTIFY_NAME(thread_set_nameA)
#define __itt_thread_set_nameW     ITTNOTIFY_VOID(thread_set_nameW)
#define __itt_thread_set_nameW_ptr ITTNOTIFY_NAME(thread_set_nameW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_thread_set_name     ITTNOTIFY_VOID(thread_set_name)
#define __itt_thread_set_name_ptr ITTNOTIFY_NAME(thread_set_name)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_thread_set_nameA(name)
#define __itt_thread_set_nameA_ptr 0
#define __itt_thread_set_nameW(name)
#define __itt_thread_set_nameW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_thread_set_name(name)
#define __itt_thread_set_name_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_thread_set_nameA_ptr 0
#define __itt_thread_set_nameW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_thread_set_name_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @cond exclude_from_gpa_documentation */

/**
 * @brief Mark current thread as ignored from this point on, for the duration of its existence.
 */
void ITTAPI __itt_thread_ignore(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, thread_ignore, (void))
#define __itt_thread_ignore     ITTNOTIFY_VOID(thread_ignore)
#define __itt_thread_ignore_ptr ITTNOTIFY_NAME(thread_ignore)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_thread_ignore()
#define __itt_thread_ignore_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_thread_ignore_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} threads group */

/**
 * @defgroup suppress Error suppression
 * @ingroup public
 * General behavior: application continues to run, but errors are suppressed
 *
 * @{
 */

/*****************************************************************//**
 * @name group of functions used for error suppression in correctness tools
 *********************************************************************/
/** @{ */
/**
 * @hideinitializer
 * @brief possible value for suppression mask
 */
#define __itt_suppress_all_errors 0x7fffffff

/**
 * @hideinitializer
 * @brief possible value for suppression mask (suppresses errors from threading analysis)
 */
#define __itt_suppress_threading_errors 0x000000ff

/**
 * @hideinitializer
 * @brief possible value for suppression mask (suppresses errors from memory analysis)
 */
#define __itt_suppress_memory_errors 0x0000ff00

/**
 * @brief Start suppressing errors identified in mask on this thread
 */
void ITTAPI __itt_suppress_push(unsigned int mask);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, suppress_push, (unsigned int mask))
#define __itt_suppress_push     ITTNOTIFY_VOID(suppress_push)
#define __itt_suppress_push_ptr ITTNOTIFY_NAME(suppress_push)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_suppress_push(mask)
#define __itt_suppress_push_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_suppress_push_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Undo the effects of the matching call to __itt_suppress_push
 */
void ITTAPI __itt_suppress_pop(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, suppress_pop, (void))
#define __itt_suppress_pop     ITTNOTIFY_VOID(suppress_pop)
#define __itt_suppress_pop_ptr ITTNOTIFY_NAME(suppress_pop)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_suppress_pop()
#define __itt_suppress_pop_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_suppress_pop_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @enum __itt_model_disable
 * @brief Enumerator for the disable methods
 */
typedef enum __itt_suppress_mode {
    __itt_unsuppress_range,
    __itt_suppress_range
} __itt_suppress_mode_t;

/**
 * @brief Mark a range of memory for error suppression or unsuppression for error types included in mask
 */
void ITTAPI __itt_suppress_mark_range(__itt_suppress_mode_t mode, unsigned int mask, void * address, size_t size);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, suppress_mark_range, (__itt_suppress_mode_t mode, unsigned int mask, void * address, size_t size))
#define __itt_suppress_mark_range     ITTNOTIFY_VOID(suppress_mark_range)
#define __itt_suppress_mark_range_ptr ITTNOTIFY_NAME(suppress_mark_range)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_suppress_mark_range(mask)
#define __itt_suppress_mark_range_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_suppress_mark_range_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Undo the effect of a matching call to __itt_suppress_mark_range.   If not matching
 *        call is found, nothing is changed.
 */
void ITTAPI __itt_suppress_clear_range(__itt_suppress_mode_t mode, unsigned int mask, void * address, size_t size);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, suppress_clear_range, (__itt_suppress_mode_t mode, unsigned int mask, void * address, size_t size))
#define __itt_suppress_clear_range     ITTNOTIFY_VOID(suppress_clear_range)
#define __itt_suppress_clear_range_ptr ITTNOTIFY_NAME(suppress_clear_range)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_suppress_clear_range(mask)
#define __itt_suppress_clear_range_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_suppress_clear_range_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} */
/** @} suppress group */

/**
 * @defgroup sync Synchronization
 * @ingroup public
 * Indicate user-written synchronization code
 * @{
 */
/**
 * @hideinitializer
 * @brief possible value of attribute argument for sync object type
 */
#define __itt_attr_barrier 1

/**
 * @hideinitializer
 * @brief possible value of attribute argument for sync object type
 */
#define __itt_attr_mutex   2

/**
@brief Name a synchronization object
@param[in] addr       Handle for the synchronization object. You should
use a real address to uniquely identify the synchronization object.
@param[in] objtype    null-terminated object type string. If NULL is
passed, the name will be "User Synchronization".
@param[in] objname    null-terminated object name string. If NULL,
no name will be assigned to the object.
@param[in] attribute  one of [#__itt_attr_barrier, #__itt_attr_mutex]
 */

#if ITT_PLATFORM==ITT_PLATFORM_WIN
void ITTAPI __itt_sync_createA(void *addr, const char    *objtype, const char    *objname, int attribute);
void ITTAPI __itt_sync_createW(void *addr, const wchar_t *objtype, const wchar_t *objname, int attribute);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_sync_create     __itt_sync_createW
#  define __itt_sync_create_ptr __itt_sync_createW_ptr
#else /* UNICODE */
#  define __itt_sync_create     __itt_sync_createA
#  define __itt_sync_create_ptr __itt_sync_createA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
void ITTAPI __itt_sync_create (void *addr, const char *objtype, const char *objname, int attribute);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, sync_createA, (void *addr, const char    *objtype, const char    *objname, int attribute))
ITT_STUBV(ITTAPI, void, sync_createW, (void *addr, const wchar_t *objtype, const wchar_t *objname, int attribute))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, sync_create,  (void *addr, const char*    objtype, const char*    objname, int attribute))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_sync_createA     ITTNOTIFY_VOID(sync_createA)
#define __itt_sync_createA_ptr ITTNOTIFY_NAME(sync_createA)
#define __itt_sync_createW     ITTNOTIFY_VOID(sync_createW)
#define __itt_sync_createW_ptr ITTNOTIFY_NAME(sync_createW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_sync_create     ITTNOTIFY_VOID(sync_create)
#define __itt_sync_create_ptr ITTNOTIFY_NAME(sync_create)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_sync_createA(addr, objtype, objname, attribute)
#define __itt_sync_createA_ptr 0
#define __itt_sync_createW(addr, objtype, objname, attribute)
#define __itt_sync_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_sync_create(addr, objtype, objname, attribute)
#define __itt_sync_create_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_sync_createA_ptr 0
#define __itt_sync_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_sync_create_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
@brief Rename a synchronization object

You can use the rename call to assign or reassign a name to a given
synchronization object.
@param[in] addr  handle for the synchronization object.
@param[in] name  null-terminated object name string.
*/
#if ITT_PLATFORM==ITT_PLATFORM_WIN
void ITTAPI __itt_sync_renameA(void *addr, const char    *name);
void ITTAPI __itt_sync_renameW(void *addr, const wchar_t *name);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_sync_rename     __itt_sync_renameW
#  define __itt_sync_rename_ptr __itt_sync_renameW_ptr
#else /* UNICODE */
#  define __itt_sync_rename     __itt_sync_renameA
#  define __itt_sync_rename_ptr __itt_sync_renameA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
void ITTAPI __itt_sync_rename(void *addr, const char *name);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, sync_renameA, (void *addr, const char    *name))
ITT_STUBV(ITTAPI, void, sync_renameW, (void *addr, const wchar_t *name))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, sync_rename,  (void *addr, const char    *name))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_sync_renameA     ITTNOTIFY_VOID(sync_renameA)
#define __itt_sync_renameA_ptr ITTNOTIFY_NAME(sync_renameA)
#define __itt_sync_renameW     ITTNOTIFY_VOID(sync_renameW)
#define __itt_sync_renameW_ptr ITTNOTIFY_NAME(sync_renameW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_sync_rename     ITTNOTIFY_VOID(sync_rename)
#define __itt_sync_rename_ptr ITTNOTIFY_NAME(sync_rename)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_sync_renameA(addr, name)
#define __itt_sync_renameA_ptr 0
#define __itt_sync_renameW(addr, name)
#define __itt_sync_renameW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_sync_rename(addr, name)
#define __itt_sync_rename_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_sync_renameA_ptr 0
#define __itt_sync_renameW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_sync_rename_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 @brief Destroy a synchronization object.
 @param addr Handle for the synchronization object.
 */
void ITTAPI __itt_sync_destroy(void *addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, sync_destroy, (void *addr))
#define __itt_sync_destroy     ITTNOTIFY_VOID(sync_destroy)
#define __itt_sync_destroy_ptr ITTNOTIFY_NAME(sync_destroy)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_sync_destroy(addr)
#define __itt_sync_destroy_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_sync_destroy_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/*****************************************************************//**
 * @name group of functions is used for performance measurement tools
 *********************************************************************/
/** @{ */
/**
 * @brief Enter spin loop on user-defined sync object
 */
void ITTAPI __itt_sync_prepare(void* addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, sync_prepare, (void *addr))
#define __itt_sync_prepare     ITTNOTIFY_VOID(sync_prepare)
#define __itt_sync_prepare_ptr ITTNOTIFY_NAME(sync_prepare)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_sync_prepare(addr)
#define __itt_sync_prepare_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_sync_prepare_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Quit spin loop without acquiring spin object
 */
void ITTAPI __itt_sync_cancel(void *addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, sync_cancel, (void *addr))
#define __itt_sync_cancel     ITTNOTIFY_VOID(sync_cancel)
#define __itt_sync_cancel_ptr ITTNOTIFY_NAME(sync_cancel)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_sync_cancel(addr)
#define __itt_sync_cancel_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_sync_cancel_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Successful spin loop completion (sync object acquired)
 */
void ITTAPI __itt_sync_acquired(void *addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, sync_acquired, (void *addr))
#define __itt_sync_acquired     ITTNOTIFY_VOID(sync_acquired)
#define __itt_sync_acquired_ptr ITTNOTIFY_NAME(sync_acquired)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_sync_acquired(addr)
#define __itt_sync_acquired_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_sync_acquired_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Start sync object releasing code. Is called before the lock release call.
 */
void ITTAPI __itt_sync_releasing(void* addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, sync_releasing, (void *addr))
#define __itt_sync_releasing     ITTNOTIFY_VOID(sync_releasing)
#define __itt_sync_releasing_ptr ITTNOTIFY_NAME(sync_releasing)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_sync_releasing(addr)
#define __itt_sync_releasing_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_sync_releasing_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} */

/** @} sync group */

/**************************************************************//**
 * @name group of functions is used for correctness checking tools
 ******************************************************************/
/** @{ */
/**
 * @ingroup legacy
 * @deprecated Legacy API
 * @brief Fast synchronization which does no require spinning.
 * - This special function is to be used by TBB and OpenMP libraries only when they know
 *   there is no spin but they need to suppress TC warnings about shared variable modifications.
 * - It only has corresponding pointers in static library and does not have corresponding function
 *   in dynamic library.
 * @see void __itt_sync_prepare(void* addr);
 */
void ITTAPI __itt_fsync_prepare(void* addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, fsync_prepare, (void *addr))
#define __itt_fsync_prepare     ITTNOTIFY_VOID(fsync_prepare)
#define __itt_fsync_prepare_ptr ITTNOTIFY_NAME(fsync_prepare)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_fsync_prepare(addr)
#define __itt_fsync_prepare_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_fsync_prepare_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup legacy
 * @deprecated Legacy API
 * @brief Fast synchronization which does no require spinning.
 * - This special function is to be used by TBB and OpenMP libraries only when they know
 *   there is no spin but they need to suppress TC warnings about shared variable modifications.
 * - It only has corresponding pointers in static library and does not have corresponding function
 *   in dynamic library.
 * @see void __itt_sync_cancel(void *addr);
 */
void ITTAPI __itt_fsync_cancel(void *addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, fsync_cancel, (void *addr))
#define __itt_fsync_cancel     ITTNOTIFY_VOID(fsync_cancel)
#define __itt_fsync_cancel_ptr ITTNOTIFY_NAME(fsync_cancel)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_fsync_cancel(addr)
#define __itt_fsync_cancel_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_fsync_cancel_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup legacy
 * @deprecated Legacy API
 * @brief Fast synchronization which does no require spinning.
 * - This special function is to be used by TBB and OpenMP libraries only when they know
 *   there is no spin but they need to suppress TC warnings about shared variable modifications.
 * - It only has corresponding pointers in static library and does not have corresponding function
 *   in dynamic library.
 * @see void __itt_sync_acquired(void *addr);
 */
void ITTAPI __itt_fsync_acquired(void *addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, fsync_acquired, (void *addr))
#define __itt_fsync_acquired     ITTNOTIFY_VOID(fsync_acquired)
#define __itt_fsync_acquired_ptr ITTNOTIFY_NAME(fsync_acquired)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_fsync_acquired(addr)
#define __itt_fsync_acquired_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_fsync_acquired_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup legacy
 * @deprecated Legacy API
 * @brief Fast synchronization which does no require spinning.
 * - This special function is to be used by TBB and OpenMP libraries only when they know
 *   there is no spin but they need to suppress TC warnings about shared variable modifications.
 * - It only has corresponding pointers in static library and does not have corresponding function
 *   in dynamic library.
 * @see void __itt_sync_releasing(void* addr);
 */
void ITTAPI __itt_fsync_releasing(void* addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, fsync_releasing, (void *addr))
#define __itt_fsync_releasing     ITTNOTIFY_VOID(fsync_releasing)
#define __itt_fsync_releasing_ptr ITTNOTIFY_NAME(fsync_releasing)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_fsync_releasing(addr)
#define __itt_fsync_releasing_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_fsync_releasing_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} */

/**
 * @defgroup model Modeling by Intel(R) Parallel Advisor
 * @ingroup public
 * This is the subset of itt used for modeling by Intel(R) Parallel Advisor.
 * This API is called ONLY using annotate.h, by "Annotation" macros
 * the user places in their sources during the parallelism modeling steps.
 *
 * site_begin/end and task_begin/end take the address of handle variables,
 * which are writeable by the API.  Handles must be 0 initialized prior
 * to the first call to begin, or may cause a run-time failure.
 * The handles are initialized in a multi-thread safe way by the API if
 * the handle is 0.  The commonly expected idiom is one static handle to
 * identify a site or task.  If a site or task of the same name has already
 * been started during this collection, the same handle MAY be returned,
 * but is not required to be - it is unspecified if data merging is done
 * based on name.  These routines also take an instance variable.  Like
 * the lexical instance, these must be 0 initialized.  Unlike the lexical
 * instance, this is used to track a single dynamic instance.
 *
 * API used by the Intel(R) Parallel Advisor to describe potential concurrency
 * and related activities. User-added source annotations expand to calls
 * to these procedures to enable modeling of a hypothetical concurrent
 * execution serially.
 * @{
 */
#if !defined(_ADVISOR_ANNOTATE_H_) || defined(ANNOTATE_EXPAND_NULL)

typedef void* __itt_model_site;             /*!< @brief handle for lexical site     */
typedef void* __itt_model_site_instance;    /*!< @brief handle for dynamic instance */
typedef void* __itt_model_task;             /*!< @brief handle for lexical site     */
typedef void* __itt_model_task_instance;    /*!< @brief handle for dynamic instance */

/**
 * @enum __itt_model_disable
 * @brief Enumerator for the disable methods
 */
typedef enum {
    __itt_model_disable_observation,
    __itt_model_disable_collection
} __itt_model_disable;

#endif /* !_ADVISOR_ANNOTATE_H_ || ANNOTATE_EXPAND_NULL */

/**
 * @brief ANNOTATE_SITE_BEGIN/ANNOTATE_SITE_END support.
 *
 * site_begin/end model a potential concurrency site.
 * site instances may be recursively nested with themselves.
 * site_end exits the most recently started but unended site for the current
 * thread.  The handle passed to end may be used to validate structure.
 * Instances of a site encountered on different threads concurrently
 * are considered completely distinct. If the site name for two different
 * lexical sites match, it is unspecified whether they are treated as the
 * same or different for data presentation.
 */
void ITTAPI __itt_model_site_begin(__itt_model_site *site, __itt_model_site_instance *instance, const char *name);
#if ITT_PLATFORM==ITT_PLATFORM_WIN
void ITTAPI __itt_model_site_beginW(const wchar_t *name);
#endif
void ITTAPI __itt_model_site_beginA(const char *name);
void ITTAPI __itt_model_site_beginAL(const char *name, size_t siteNameLen);
void ITTAPI __itt_model_site_end  (__itt_model_site *site, __itt_model_site_instance *instance);
void ITTAPI __itt_model_site_end_2(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, model_site_begin,  (__itt_model_site *site, __itt_model_site_instance *instance, const char *name))
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, model_site_beginW,  (const wchar_t *name))
#endif
ITT_STUBV(ITTAPI, void, model_site_beginA,  (const char *name))
ITT_STUBV(ITTAPI, void, model_site_beginAL,  (const char *name, size_t siteNameLen))
ITT_STUBV(ITTAPI, void, model_site_end,    (__itt_model_site *site, __itt_model_site_instance *instance))
ITT_STUBV(ITTAPI, void, model_site_end_2,  (void))
#define __itt_model_site_begin      ITTNOTIFY_VOID(model_site_begin)
#define __itt_model_site_begin_ptr  ITTNOTIFY_NAME(model_site_begin)
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_model_site_beginW      ITTNOTIFY_VOID(model_site_beginW)
#define __itt_model_site_beginW_ptr  ITTNOTIFY_NAME(model_site_beginW)
#endif
#define __itt_model_site_beginA      ITTNOTIFY_VOID(model_site_beginA)
#define __itt_model_site_beginA_ptr  ITTNOTIFY_NAME(model_site_beginA)
#define __itt_model_site_beginAL      ITTNOTIFY_VOID(model_site_beginAL)
#define __itt_model_site_beginAL_ptr  ITTNOTIFY_NAME(model_site_beginAL)
#define __itt_model_site_end        ITTNOTIFY_VOID(model_site_end)
#define __itt_model_site_end_ptr    ITTNOTIFY_NAME(model_site_end)
#define __itt_model_site_end_2        ITTNOTIFY_VOID(model_site_end_2)
#define __itt_model_site_end_2_ptr    ITTNOTIFY_NAME(model_site_end_2)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_model_site_begin(site, instance, name)
#define __itt_model_site_begin_ptr  0
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_model_site_beginW(name)
#define __itt_model_site_beginW_ptr  0
#endif
#define __itt_model_site_beginA(name)
#define __itt_model_site_beginA_ptr  0
#define __itt_model_site_beginAL(name, siteNameLen)
#define __itt_model_site_beginAL_ptr  0
#define __itt_model_site_end(site, instance)
#define __itt_model_site_end_ptr    0
#define __itt_model_site_end_2()
#define __itt_model_site_end_2_ptr    0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_model_site_begin_ptr  0
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_model_site_beginW_ptr  0
#endif
#define __itt_model_site_beginA_ptr  0
#define __itt_model_site_beginAL_ptr  0
#define __itt_model_site_end_ptr    0
#define __itt_model_site_end_2_ptr    0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief ANNOTATE_TASK_BEGIN/ANNOTATE_TASK_END support
 *
 * task_begin/end model a potential task, which is contained within the most
 * closely enclosing dynamic site.  task_end exits the most recently started
 * but unended task.  The handle passed to end may be used to validate
 * structure.  It is unspecified if bad dynamic nesting is detected.  If it
 * is, it should be encoded in the resulting data collection.  The collector
 * should not fail due to construct nesting issues, nor attempt to directly
 * indicate the problem.
 */
void ITTAPI __itt_model_task_begin(__itt_model_task *task, __itt_model_task_instance *instance, const char *name);
#if ITT_PLATFORM==ITT_PLATFORM_WIN
void ITTAPI __itt_model_task_beginW(const wchar_t *name);
void ITTAPI __itt_model_iteration_taskW(const wchar_t *name);
#endif
void ITTAPI __itt_model_task_beginA(const char *name);
void ITTAPI __itt_model_task_beginAL(const char *name, size_t taskNameLen);
void ITTAPI __itt_model_iteration_taskA(const char *name);
void ITTAPI __itt_model_iteration_taskAL(const char *name, size_t taskNameLen);
void ITTAPI __itt_model_task_end  (__itt_model_task *task, __itt_model_task_instance *instance);
void ITTAPI __itt_model_task_end_2(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, model_task_begin,  (__itt_model_task *task, __itt_model_task_instance *instance, const char *name))
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, model_task_beginW,  (const wchar_t *name))
ITT_STUBV(ITTAPI, void, model_iteration_taskW, (const wchar_t *name))
#endif
ITT_STUBV(ITTAPI, void, model_task_beginA,  (const char *name))
ITT_STUBV(ITTAPI, void, model_task_beginAL,  (const char *name, size_t taskNameLen))
ITT_STUBV(ITTAPI, void, model_iteration_taskA,  (const char *name))
ITT_STUBV(ITTAPI, void, model_iteration_taskAL,  (const char *name, size_t taskNameLen))
ITT_STUBV(ITTAPI, void, model_task_end,    (__itt_model_task *task, __itt_model_task_instance *instance))
ITT_STUBV(ITTAPI, void, model_task_end_2,  (void))
#define __itt_model_task_begin      ITTNOTIFY_VOID(model_task_begin)
#define __itt_model_task_begin_ptr  ITTNOTIFY_NAME(model_task_begin)
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_model_task_beginW     ITTNOTIFY_VOID(model_task_beginW)
#define __itt_model_task_beginW_ptr ITTNOTIFY_NAME(model_task_beginW)
#define __itt_model_iteration_taskW     ITTNOTIFY_VOID(model_iteration_taskW)
#define __itt_model_iteration_taskW_ptr ITTNOTIFY_NAME(model_iteration_taskW)
#endif
#define __itt_model_task_beginA    ITTNOTIFY_VOID(model_task_beginA)
#define __itt_model_task_beginA_ptr ITTNOTIFY_NAME(model_task_beginA)
#define __itt_model_task_beginAL    ITTNOTIFY_VOID(model_task_beginAL)
#define __itt_model_task_beginAL_ptr ITTNOTIFY_NAME(model_task_beginAL)
#define __itt_model_iteration_taskA    ITTNOTIFY_VOID(model_iteration_taskA)
#define __itt_model_iteration_taskA_ptr ITTNOTIFY_NAME(model_iteration_taskA)
#define __itt_model_iteration_taskAL    ITTNOTIFY_VOID(model_iteration_taskAL)
#define __itt_model_iteration_taskAL_ptr ITTNOTIFY_NAME(model_iteration_taskAL)
#define __itt_model_task_end        ITTNOTIFY_VOID(model_task_end)
#define __itt_model_task_end_ptr    ITTNOTIFY_NAME(model_task_end)
#define __itt_model_task_end_2        ITTNOTIFY_VOID(model_task_end_2)
#define __itt_model_task_end_2_ptr    ITTNOTIFY_NAME(model_task_end_2)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_model_task_begin(task, instance, name)
#define __itt_model_task_begin_ptr  0
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_model_task_beginW(name)
#define __itt_model_task_beginW_ptr  0
#endif
#define __itt_model_task_beginA(name)
#define __itt_model_task_beginA_ptr  0
#define __itt_model_task_beginAL(name, siteNameLen)
#define __itt_model_task_beginAL_ptr  0
#define __itt_model_iteration_taskA(name)
#define __itt_model_iteration_taskA_ptr  0
#define __itt_model_iteration_taskAL(name, siteNameLen)
#define __itt_model_iteration_taskAL_ptr  0
#define __itt_model_task_end(task, instance)
#define __itt_model_task_end_ptr    0
#define __itt_model_task_end_2()
#define __itt_model_task_end_2_ptr    0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_model_task_begin_ptr  0
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_model_task_beginW_ptr 0
#endif
#define __itt_model_task_beginA_ptr  0
#define __itt_model_task_beginAL_ptr  0
#define __itt_model_iteration_taskA_ptr    0
#define __itt_model_iteration_taskAL_ptr    0
#define __itt_model_task_end_ptr    0
#define __itt_model_task_end_2_ptr    0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief ANNOTATE_LOCK_ACQUIRE/ANNOTATE_LOCK_RELEASE support
 *
 * lock_acquire/release model a potential lock for both lockset and
 * performance modeling.  Each unique address is modeled as a separate
 * lock, with invalid addresses being valid lock IDs.  Specifically:
 * no storage is accessed by the API at the specified address - it is only
 * used for lock identification.  Lock acquires may be self-nested and are
 * unlocked by a corresponding number of releases.
 * (These closely correspond to __itt_sync_acquired/__itt_sync_releasing,
 * but may not have identical semantics.)
 */
void ITTAPI __itt_model_lock_acquire(void *lock);
void ITTAPI __itt_model_lock_acquire_2(void *lock);
void ITTAPI __itt_model_lock_release(void *lock);
void ITTAPI __itt_model_lock_release_2(void *lock);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, model_lock_acquire, (void *lock))
ITT_STUBV(ITTAPI, void, model_lock_acquire_2, (void *lock))
ITT_STUBV(ITTAPI, void, model_lock_release, (void *lock))
ITT_STUBV(ITTAPI, void, model_lock_release_2, (void *lock))
#define __itt_model_lock_acquire     ITTNOTIFY_VOID(model_lock_acquire)
#define __itt_model_lock_acquire_ptr ITTNOTIFY_NAME(model_lock_acquire)
#define __itt_model_lock_acquire_2     ITTNOTIFY_VOID(model_lock_acquire_2)
#define __itt_model_lock_acquire_2_ptr ITTNOTIFY_NAME(model_lock_acquire_2)
#define __itt_model_lock_release     ITTNOTIFY_VOID(model_lock_release)
#define __itt_model_lock_release_ptr ITTNOTIFY_NAME(model_lock_release)
#define __itt_model_lock_release_2     ITTNOTIFY_VOID(model_lock_release_2)
#define __itt_model_lock_release_2_ptr ITTNOTIFY_NAME(model_lock_release_2)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_model_lock_acquire(lock)
#define __itt_model_lock_acquire_ptr 0
#define __itt_model_lock_acquire_2(lock)
#define __itt_model_lock_acquire_2_ptr 0
#define __itt_model_lock_release(lock)
#define __itt_model_lock_release_ptr 0
#define __itt_model_lock_release_2(lock)
#define __itt_model_lock_release_2_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_model_lock_acquire_ptr 0
#define __itt_model_lock_acquire_2_ptr 0
#define __itt_model_lock_release_ptr 0
#define __itt_model_lock_release_2_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief ANNOTATE_RECORD_ALLOCATION/ANNOTATE_RECORD_DEALLOCATION support
 *
 * record_allocation/deallocation describe user-defined memory allocator
 * behavior, which may be required for correctness modeling to understand
 * when storage is not expected to be actually reused across threads.
 */
void ITTAPI __itt_model_record_allocation  (void *addr, size_t size);
void ITTAPI __itt_model_record_deallocation(void *addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, model_record_allocation,   (void *addr, size_t size))
ITT_STUBV(ITTAPI, void, model_record_deallocation, (void *addr))
#define __itt_model_record_allocation       ITTNOTIFY_VOID(model_record_allocation)
#define __itt_model_record_allocation_ptr   ITTNOTIFY_NAME(model_record_allocation)
#define __itt_model_record_deallocation     ITTNOTIFY_VOID(model_record_deallocation)
#define __itt_model_record_deallocation_ptr ITTNOTIFY_NAME(model_record_deallocation)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_model_record_allocation(addr, size)
#define __itt_model_record_allocation_ptr   0
#define __itt_model_record_deallocation(addr)
#define __itt_model_record_deallocation_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_model_record_allocation_ptr   0
#define __itt_model_record_deallocation_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief ANNOTATE_INDUCTION_USES support
 *
 * Note particular storage is inductive through the end of the current site
 */
void ITTAPI __itt_model_induction_uses(void* addr, size_t size);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, model_induction_uses, (void *addr, size_t size))
#define __itt_model_induction_uses     ITTNOTIFY_VOID(model_induction_uses)
#define __itt_model_induction_uses_ptr ITTNOTIFY_NAME(model_induction_uses)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_model_induction_uses(addr, size)
#define __itt_model_induction_uses_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_model_induction_uses_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief ANNOTATE_REDUCTION_USES support
 *
 * Note particular storage is used for reduction through the end
 * of the current site
 */
void ITTAPI __itt_model_reduction_uses(void* addr, size_t size);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, model_reduction_uses, (void *addr, size_t size))
#define __itt_model_reduction_uses     ITTNOTIFY_VOID(model_reduction_uses)
#define __itt_model_reduction_uses_ptr ITTNOTIFY_NAME(model_reduction_uses)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_model_reduction_uses(addr, size)
#define __itt_model_reduction_uses_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_model_reduction_uses_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief ANNOTATE_OBSERVE_USES support
 *
 * Have correctness modeling record observations about uses of storage
 * through the end of the current site
 */
void ITTAPI __itt_model_observe_uses(void* addr, size_t size);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, model_observe_uses, (void *addr, size_t size))
#define __itt_model_observe_uses     ITTNOTIFY_VOID(model_observe_uses)
#define __itt_model_observe_uses_ptr ITTNOTIFY_NAME(model_observe_uses)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_model_observe_uses(addr, size)
#define __itt_model_observe_uses_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_model_observe_uses_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief ANNOTATE_CLEAR_USES support
 *
 * Clear the special handling of a piece of storage related to induction,
 * reduction or observe_uses
 */
void ITTAPI __itt_model_clear_uses(void* addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, model_clear_uses, (void *addr))
#define __itt_model_clear_uses     ITTNOTIFY_VOID(model_clear_uses)
#define __itt_model_clear_uses_ptr ITTNOTIFY_NAME(model_clear_uses)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_model_clear_uses(addr)
#define __itt_model_clear_uses_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_model_clear_uses_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief ANNOTATE_DISABLE_*_PUSH/ANNOTATE_DISABLE_*_POP support
 *
 * disable_push/disable_pop push and pop disabling based on a parameter.
 * Disabling observations stops processing of memory references during
 * correctness modeling, and all annotations that occur in the disabled
 * region.  This allows description of code that is expected to be handled
 * specially during conversion to parallelism or that is not recognized
 * by tools (e.g. some kinds of synchronization operations.)
 * This mechanism causes all annotations in the disabled region, other
 * than disable_push and disable_pop, to be ignored.  (For example, this
 * might validly be used to disable an entire parallel site and the contained
 * tasks and locking in it for data collection purposes.)
 * The disable for collection is a more expensive operation, but reduces
 * collector overhead significantly.  This applies to BOTH correctness data
 * collection and performance data collection.  For example, a site
 * containing a task might only enable data collection for the first 10
 * iterations.  Both performance and correctness data should reflect this,
 * and the program should run as close to full speed as possible when
 * collection is disabled.
 */
void ITTAPI __itt_model_disable_push(__itt_model_disable x);
void ITTAPI __itt_model_disable_pop(void);
void ITTAPI __itt_model_aggregate_task(size_t x);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, model_disable_push, (__itt_model_disable x))
ITT_STUBV(ITTAPI, void, model_disable_pop,  (void))
ITT_STUBV(ITTAPI, void, model_aggregate_task, (size_t x))
#define __itt_model_disable_push     ITTNOTIFY_VOID(model_disable_push)
#define __itt_model_disable_push_ptr ITTNOTIFY_NAME(model_disable_push)
#define __itt_model_disable_pop      ITTNOTIFY_VOID(model_disable_pop)
#define __itt_model_disable_pop_ptr  ITTNOTIFY_NAME(model_disable_pop)
#define __itt_model_aggregate_task      ITTNOTIFY_VOID(model_aggregate_task)
#define __itt_model_aggregate_task_ptr  ITTNOTIFY_NAME(model_aggregate_task)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_model_disable_push(x)
#define __itt_model_disable_push_ptr 0
#define __itt_model_disable_pop()
#define __itt_model_disable_pop_ptr 0
#define __itt_model_aggregate_task(x)
#define __itt_model_aggregate_task_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_model_disable_push_ptr 0
#define __itt_model_disable_pop_ptr 0
#define __itt_model_aggregate_task_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} model group */

/**
 * @defgroup heap Heap
 * @ingroup public
 * Heap group
 * @{
 */

typedef void* __itt_heap_function;

/**
 * @brief Create an identification for heap function
 * @return non-zero identifier or NULL
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
__itt_heap_function ITTAPI __itt_heap_function_createA(const char*    name, const char*    domain);
__itt_heap_function ITTAPI __itt_heap_function_createW(const wchar_t* name, const wchar_t* domain);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_heap_function_create     __itt_heap_function_createW
#  define __itt_heap_function_create_ptr __itt_heap_function_createW_ptr
#else
#  define __itt_heap_function_create     __itt_heap_function_createA
#  define __itt_heap_function_create_ptr __itt_heap_function_createA_ptr
#endif /* UNICODE */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
__itt_heap_function ITTAPI __itt_heap_function_create(const char* name, const char* domain);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_heap_function, heap_function_createA, (const char*    name, const char*    domain))
ITT_STUB(ITTAPI, __itt_heap_function, heap_function_createW, (const wchar_t* name, const wchar_t* domain))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_heap_function, heap_function_create,  (const char*    name, const char*    domain))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_heap_function_createA     ITTNOTIFY_DATA(heap_function_createA)
#define __itt_heap_function_createA_ptr ITTNOTIFY_NAME(heap_function_createA)
#define __itt_heap_function_createW     ITTNOTIFY_DATA(heap_function_createW)
#define __itt_heap_function_createW_ptr ITTNOTIFY_NAME(heap_function_createW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_heap_function_create      ITTNOTIFY_DATA(heap_function_create)
#define __itt_heap_function_create_ptr  ITTNOTIFY_NAME(heap_function_create)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_heap_function_createA(name, domain) (__itt_heap_function)0
#define __itt_heap_function_createA_ptr 0
#define __itt_heap_function_createW(name, domain) (__itt_heap_function)0
#define __itt_heap_function_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_heap_function_create(name, domain)  (__itt_heap_function)0
#define __itt_heap_function_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_heap_function_createA_ptr 0
#define __itt_heap_function_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_heap_function_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Record an allocation begin occurrence.
 */
void ITTAPI __itt_heap_allocate_begin(__itt_heap_function h, size_t size, int initialized);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_allocate_begin, (__itt_heap_function h, size_t size, int initialized))
#define __itt_heap_allocate_begin     ITTNOTIFY_VOID(heap_allocate_begin)
#define __itt_heap_allocate_begin_ptr ITTNOTIFY_NAME(heap_allocate_begin)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_allocate_begin(h, size, initialized)
#define __itt_heap_allocate_begin_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_allocate_begin_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Record an allocation end occurrence.
 */
void ITTAPI __itt_heap_allocate_end(__itt_heap_function h, void** addr, size_t size, int initialized);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_allocate_end, (__itt_heap_function h, void** addr, size_t size, int initialized))
#define __itt_heap_allocate_end     ITTNOTIFY_VOID(heap_allocate_end)
#define __itt_heap_allocate_end_ptr ITTNOTIFY_NAME(heap_allocate_end)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_allocate_end(h, addr, size, initialized)
#define __itt_heap_allocate_end_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_allocate_end_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Record an free begin occurrence.
 */
void ITTAPI __itt_heap_free_begin(__itt_heap_function h, void* addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_free_begin, (__itt_heap_function h, void* addr))
#define __itt_heap_free_begin     ITTNOTIFY_VOID(heap_free_begin)
#define __itt_heap_free_begin_ptr ITTNOTIFY_NAME(heap_free_begin)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_free_begin(h, addr)
#define __itt_heap_free_begin_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_free_begin_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Record an free end occurrence.
 */
void ITTAPI __itt_heap_free_end(__itt_heap_function h, void* addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_free_end, (__itt_heap_function h, void* addr))
#define __itt_heap_free_end     ITTNOTIFY_VOID(heap_free_end)
#define __itt_heap_free_end_ptr ITTNOTIFY_NAME(heap_free_end)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_free_end(h, addr)
#define __itt_heap_free_end_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_free_end_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Record an reallocation begin occurrence.
 */
void ITTAPI __itt_heap_reallocate_begin(__itt_heap_function h, void* addr, size_t new_size, int initialized);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_reallocate_begin, (__itt_heap_function h, void* addr, size_t new_size, int initialized))
#define __itt_heap_reallocate_begin     ITTNOTIFY_VOID(heap_reallocate_begin)
#define __itt_heap_reallocate_begin_ptr ITTNOTIFY_NAME(heap_reallocate_begin)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_reallocate_begin(h, addr, new_size, initialized)
#define __itt_heap_reallocate_begin_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_reallocate_begin_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Record an reallocation end occurrence.
 */
void ITTAPI __itt_heap_reallocate_end(__itt_heap_function h, void* addr, void** new_addr, size_t new_size, int initialized);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_reallocate_end, (__itt_heap_function h, void* addr, void** new_addr, size_t new_size, int initialized))
#define __itt_heap_reallocate_end     ITTNOTIFY_VOID(heap_reallocate_end)
#define __itt_heap_reallocate_end_ptr ITTNOTIFY_NAME(heap_reallocate_end)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_reallocate_end(h, addr, new_addr, new_size, initialized)
#define __itt_heap_reallocate_end_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_reallocate_end_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @brief internal access begin */
void ITTAPI __itt_heap_internal_access_begin(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_internal_access_begin,  (void))
#define __itt_heap_internal_access_begin      ITTNOTIFY_VOID(heap_internal_access_begin)
#define __itt_heap_internal_access_begin_ptr  ITTNOTIFY_NAME(heap_internal_access_begin)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_internal_access_begin()
#define __itt_heap_internal_access_begin_ptr  0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_internal_access_begin_ptr  0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @brief internal access end */
void ITTAPI __itt_heap_internal_access_end(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_internal_access_end, (void))
#define __itt_heap_internal_access_end     ITTNOTIFY_VOID(heap_internal_access_end)
#define __itt_heap_internal_access_end_ptr ITTNOTIFY_NAME(heap_internal_access_end)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_internal_access_end()
#define __itt_heap_internal_access_end_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_internal_access_end_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @brief record memory growth begin */
void ITTAPI __itt_heap_record_memory_growth_begin(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_record_memory_growth_begin,  (void))
#define __itt_heap_record_memory_growth_begin      ITTNOTIFY_VOID(heap_record_memory_growth_begin)
#define __itt_heap_record_memory_growth_begin_ptr  ITTNOTIFY_NAME(heap_record_memory_growth_begin)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_record_memory_growth_begin()
#define __itt_heap_record_memory_growth_begin_ptr  0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_record_memory_growth_begin_ptr  0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @brief record memory growth end */
void ITTAPI __itt_heap_record_memory_growth_end(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_record_memory_growth_end, (void))
#define __itt_heap_record_memory_growth_end     ITTNOTIFY_VOID(heap_record_memory_growth_end)
#define __itt_heap_record_memory_growth_end_ptr ITTNOTIFY_NAME(heap_record_memory_growth_end)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_record_memory_growth_end()
#define __itt_heap_record_memory_growth_end_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_record_memory_growth_end_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Specify the type of heap detection/reporting to modify.
 */
/**
 * @hideinitializer
 * @brief Report on memory leaks.
 */
#define __itt_heap_leaks 0x00000001

/**
 * @hideinitializer
 * @brief Report on memory growth.
 */
#define __itt_heap_growth 0x00000002


/** @brief heap reset detection */
void ITTAPI __itt_heap_reset_detection(unsigned int reset_mask);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_reset_detection,  (unsigned int reset_mask))
#define __itt_heap_reset_detection      ITTNOTIFY_VOID(heap_reset_detection)
#define __itt_heap_reset_detection_ptr  ITTNOTIFY_NAME(heap_reset_detection)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_reset_detection()
#define __itt_heap_reset_detection_ptr  0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_reset_detection_ptr  0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @brief report */
void ITTAPI __itt_heap_record(unsigned int record_mask);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, heap_record, (unsigned int record_mask))
#define __itt_heap_record     ITTNOTIFY_VOID(heap_record)
#define __itt_heap_record_ptr ITTNOTIFY_NAME(heap_record)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_heap_record()
#define __itt_heap_record_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_heap_record_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @} heap group */
/** @endcond */
/* ========================================================================== */

/**
 * @defgroup domains Domains
 * @ingroup public
 * Domains group
 * @{
 */

/** @cond exclude_from_documentation */
#pragma pack(push, 8)

typedef struct ___itt_domain
{
    volatile int flags; /*!< Zero if disabled, non-zero if enabled. The meaning of different non-zero values is reserved to the runtime */
    const char* nameA;  /*!< Copy of original name in ASCII. */
#if defined(UNICODE) || defined(_UNICODE)
    const wchar_t* nameW; /*!< Copy of original name in UNICODE. */
#else  /* UNICODE || _UNICODE */
    void* nameW;
#endif /* UNICODE || _UNICODE */
    int   extra1; /*!< Reserved to the runtime */
    void* extra2; /*!< Reserved to the runtime */
    struct ___itt_domain* next;
} __itt_domain;

#pragma pack(pop)
/** @endcond */

/**
 * @ingroup domains
 * @brief Create a domain.
 * Create domain using some domain name: the URI naming style is recommended.
 * Because the set of domains is expected to be static over the application's
 * execution time, there is no mechanism to destroy a domain.
 * Any domain can be accessed by any thread in the process, regardless of
 * which thread created the domain. This call is thread-safe.
 * @param[in] name name of domain
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
__itt_domain* ITTAPI __itt_domain_createA(const char    *name);
__itt_domain* ITTAPI __itt_domain_createW(const wchar_t *name);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_domain_create     __itt_domain_createW
#  define __itt_domain_create_ptr __itt_domain_createW_ptr
#else /* UNICODE */
#  define __itt_domain_create     __itt_domain_createA
#  define __itt_domain_create_ptr __itt_domain_createA_ptr
#endif /* UNICODE */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
__itt_domain* ITTAPI __itt_domain_create(const char *name);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_domain*, domain_createA, (const char    *name))
ITT_STUB(ITTAPI, __itt_domain*, domain_createW, (const wchar_t *name))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_domain*, domain_create,  (const char    *name))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_domain_createA     ITTNOTIFY_DATA(domain_createA)
#define __itt_domain_createA_ptr ITTNOTIFY_NAME(domain_createA)
#define __itt_domain_createW     ITTNOTIFY_DATA(domain_createW)
#define __itt_domain_createW_ptr ITTNOTIFY_NAME(domain_createW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_domain_create     ITTNOTIFY_DATA(domain_create)
#define __itt_domain_create_ptr ITTNOTIFY_NAME(domain_create)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_domain_createA(name) (__itt_domain*)0
#define __itt_domain_createA_ptr 0
#define __itt_domain_createW(name) (__itt_domain*)0
#define __itt_domain_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_domain_create(name)  (__itt_domain*)0
#define __itt_domain_create_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_domain_createA_ptr 0
#define __itt_domain_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_domain_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} domains group */

/**
 * @defgroup ids IDs
 * @ingroup public
 * IDs group
 * @{
 */

/** @cond exclude_from_documentation */
#pragma pack(push, 8)

typedef struct ___itt_id
{
    unsigned long long d1, d2, d3;
} __itt_id;

#pragma pack(pop)
/** @endcond */

static const __itt_id __itt_null = { 0, 0, 0 };

/**
 * @ingroup ids
 * @brief A convenience function is provided to create an ID without domain control.
 * @brief This is a convenience function to initialize an __itt_id structure. This function
 * does not affect the collector runtime in any way. After you make the ID with this
 * function, you still must create it with the __itt_id_create function before using the ID
 * to identify a named entity.
 * @param[in] addr The address of object; high QWORD of the ID value.
 * @param[in] extra The extra data to unique identify object; low QWORD of the ID value.
 */

ITT_INLINE __itt_id ITTAPI __itt_id_make(void* addr, unsigned long long extra) ITT_INLINE_ATTRIBUTE;
ITT_INLINE __itt_id ITTAPI __itt_id_make(void* addr, unsigned long long extra)
{
    __itt_id id = __itt_null;
    id.d1 = (unsigned long long)((uintptr_t)addr);
    id.d2 = (unsigned long long)extra;
    id.d3 = (unsigned long long)0; /* Reserved. Must be zero */
    return id;
}

/**
 * @ingroup ids
 * @brief Create an instance of identifier.
 * This establishes the beginning of the lifetime of an instance of
 * the given ID in the trace. Once this lifetime starts, the ID
 * can be used to tag named entity instances in calls such as
 * __itt_task_begin, and to specify relationships among
 * identified named entity instances, using the \ref relations APIs.
 * Instance IDs are not domain specific!
 * @param[in] domain The domain controlling the execution of this call.
 * @param[in] id The ID to create.
 */
void ITTAPI __itt_id_create(const __itt_domain *domain, __itt_id id);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, id_create, (const __itt_domain *domain, __itt_id id))
#define __itt_id_create(d,x) ITTNOTIFY_VOID_D1(id_create,d,x)
#define __itt_id_create_ptr  ITTNOTIFY_NAME(id_create)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_id_create(domain,id)
#define __itt_id_create_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_id_create_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup ids
 * @brief Destroy an instance of identifier.
 * This ends the lifetime of the current instance of the given ID value in the trace.
 * Any relationships that are established after this lifetime ends are invalid.
 * This call must be performed before the given ID value can be reused for a different
 * named entity instance.
 * @param[in] domain The domain controlling the execution of this call.
 * @param[in] id The ID to destroy.
 */
void ITTAPI __itt_id_destroy(const __itt_domain *domain, __itt_id id);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, id_destroy, (const __itt_domain *domain, __itt_id id))
#define __itt_id_destroy(d,x) ITTNOTIFY_VOID_D1(id_destroy,d,x)
#define __itt_id_destroy_ptr  ITTNOTIFY_NAME(id_destroy)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_id_destroy(domain,id)
#define __itt_id_destroy_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_id_destroy_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} ids group */

/**
 * @defgroup handless String Handles
 * @ingroup public
 * String Handles group
 * @{
 */

/** @cond exclude_from_documentation */
#pragma pack(push, 8)

typedef struct ___itt_string_handle
{
    const char* strA; /*!< Copy of original string in ASCII. */
#if defined(UNICODE) || defined(_UNICODE)
    const wchar_t* strW; /*!< Copy of original string in UNICODE. */
#else  /* UNICODE || _UNICODE */
    void* strW;
#endif /* UNICODE || _UNICODE */
    int   extra1; /*!< Reserved. Must be zero   */
    void* extra2; /*!< Reserved. Must be zero   */
    struct ___itt_string_handle* next;
} __itt_string_handle;

#pragma pack(pop)
/** @endcond */

/**
 * @ingroup handles
 * @brief Create a string handle.
 * Create and return handle value that can be associated with a string.
 * Consecutive calls to __itt_string_handle_create with the same name
 * return the same value. Because the set of string handles is expected to remain
 * static during the application's execution time, there is no mechanism to destroy a string handle.
 * Any string handle can be accessed by any thread in the process, regardless of which thread created
 * the string handle. This call is thread-safe.
 * @param[in] name The input string
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
__itt_string_handle* ITTAPI __itt_string_handle_createA(const char    *name);
__itt_string_handle* ITTAPI __itt_string_handle_createW(const wchar_t *name);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_string_handle_create     __itt_string_handle_createW
#  define __itt_string_handle_create_ptr __itt_string_handle_createW_ptr
#else /* UNICODE */
#  define __itt_string_handle_create     __itt_string_handle_createA
#  define __itt_string_handle_create_ptr __itt_string_handle_createA_ptr
#endif /* UNICODE */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
__itt_string_handle* ITTAPI __itt_string_handle_create(const char *name);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_string_handle*, string_handle_createA, (const char    *name))
ITT_STUB(ITTAPI, __itt_string_handle*, string_handle_createW, (const wchar_t *name))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_string_handle*, string_handle_create,  (const char    *name))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_string_handle_createA     ITTNOTIFY_DATA(string_handle_createA)
#define __itt_string_handle_createA_ptr ITTNOTIFY_NAME(string_handle_createA)
#define __itt_string_handle_createW     ITTNOTIFY_DATA(string_handle_createW)
#define __itt_string_handle_createW_ptr ITTNOTIFY_NAME(string_handle_createW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_string_handle_create     ITTNOTIFY_DATA(string_handle_create)
#define __itt_string_handle_create_ptr ITTNOTIFY_NAME(string_handle_create)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_string_handle_createA(name) (__itt_string_handle*)0
#define __itt_string_handle_createA_ptr 0
#define __itt_string_handle_createW(name) (__itt_string_handle*)0
#define __itt_string_handle_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_string_handle_create(name)  (__itt_string_handle*)0
#define __itt_string_handle_create_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_string_handle_createA_ptr 0
#define __itt_string_handle_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_string_handle_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} handles group */

/** @cond exclude_from_documentation */
typedef unsigned long long __itt_timestamp;
/** @endcond */

#define __itt_timestamp_none ((__itt_timestamp)-1LL)

/** @cond exclude_from_gpa_documentation */

/**
 * @ingroup timestamps
 * @brief Return timestamp corresponding to the current moment.
 * This returns the timestamp in the format that is the most relevant for the current
 * host or platform (RDTSC, QPC, and others). You can use the "<" operator to
 * compare __itt_timestamp values.
 */
__itt_timestamp ITTAPI __itt_get_timestamp(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, __itt_timestamp, get_timestamp, (void))
#define __itt_get_timestamp      ITTNOTIFY_DATA(get_timestamp)
#define __itt_get_timestamp_ptr  ITTNOTIFY_NAME(get_timestamp)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_get_timestamp()
#define __itt_get_timestamp_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_get_timestamp_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} timestamps */
/** @endcond */

/** @cond exclude_from_gpa_documentation */

/**
 * @defgroup regions Regions
 * @ingroup public
 * Regions group
 * @{
 */
/**
 * @ingroup regions
 * @brief Begin of region instance.
 * Successive calls to __itt_region_begin with the same ID are ignored
 * until a call to __itt_region_end with the same ID
 * @param[in] domain The domain for this region instance
 * @param[in] id The instance ID for this region instance. Must not be __itt_null
 * @param[in] parentid The instance ID for the parent of this region instance, or __itt_null
 * @param[in] name The name of this region
 */
void ITTAPI __itt_region_begin(const __itt_domain *domain, __itt_id id, __itt_id parentid, __itt_string_handle *name);

/**
 * @ingroup regions
 * @brief End of region instance.
 * The first call to __itt_region_end with a given ID ends the
 * region. Successive calls with the same ID are ignored, as are
 * calls that do not have a matching __itt_region_begin call.
 * @param[in] domain The domain for this region instance
 * @param[in] id The instance ID for this region instance
 */
void ITTAPI __itt_region_end(const __itt_domain *domain, __itt_id id);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, region_begin, (const __itt_domain *domain, __itt_id id, __itt_id parentid, __itt_string_handle *name))
ITT_STUBV(ITTAPI, void, region_end,   (const __itt_domain *domain, __itt_id id))
#define __itt_region_begin(d,x,y,z) ITTNOTIFY_VOID_D3(region_begin,d,x,y,z)
#define __itt_region_begin_ptr      ITTNOTIFY_NAME(region_begin)
#define __itt_region_end(d,x)       ITTNOTIFY_VOID_D1(region_end,d,x)
#define __itt_region_end_ptr        ITTNOTIFY_NAME(region_end)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_region_begin(d,x,y,z)
#define __itt_region_begin_ptr 0
#define __itt_region_end(d,x)
#define __itt_region_end_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_region_begin_ptr 0
#define __itt_region_end_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} regions group */

/**
 * @defgroup frames Frames
 * @ingroup public
 * Frames are similar to regions, but are intended to be easier to use and to implement.
 * In particular:
 * - Frames always represent periods of elapsed time
 * - By default, frames have no nesting relationships
 * @{
 */

/**
 * @ingroup frames
 * @brief Begin a frame instance.
 * Successive calls to __itt_frame_begin with the
 * same ID are ignored until a call to __itt_frame_end with the same ID.
 * @param[in] domain The domain for this frame instance
 * @param[in] id The instance ID for this frame instance or NULL
 */
void ITTAPI __itt_frame_begin_v3(const __itt_domain *domain, __itt_id *id);

/**
 * @ingroup frames
 * @brief End a frame instance.
 * The first call to __itt_frame_end with a given ID
 * ends the frame. Successive calls with the same ID are ignored, as are
 * calls that do not have a matching __itt_frame_begin call.
 * @param[in] domain The domain for this frame instance
 * @param[in] id The instance ID for this frame instance or NULL for current
 */
void ITTAPI __itt_frame_end_v3(const __itt_domain *domain, __itt_id *id);

/**
 * @ingroup frames
 * @brief Submits a frame instance.
 * Successive calls to __itt_frame_begin or __itt_frame_submit with the
 * same ID are ignored until a call to __itt_frame_end or __itt_frame_submit
 * with the same ID.
 * Passing special __itt_timestamp_none value as "end" argument means
 * take the current timestamp as the end timestamp.
 * @param[in] domain The domain for this frame instance
 * @param[in] id The instance ID for this frame instance or NULL
 * @param[in] begin Timestamp of the beginning of the frame
 * @param[in] end Timestamp of the end of the frame
 */
void ITTAPI __itt_frame_submit_v3(const __itt_domain *domain, __itt_id *id,
    __itt_timestamp begin, __itt_timestamp end);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, frame_begin_v3,  (const __itt_domain *domain, __itt_id *id))
ITT_STUBV(ITTAPI, void, frame_end_v3,    (const __itt_domain *domain, __itt_id *id))
ITT_STUBV(ITTAPI, void, frame_submit_v3, (const __itt_domain *domain, __itt_id *id, __itt_timestamp begin, __itt_timestamp end))
#define __itt_frame_begin_v3(d,x)      ITTNOTIFY_VOID_D1(frame_begin_v3,d,x)
#define __itt_frame_begin_v3_ptr       ITTNOTIFY_NAME(frame_begin_v3)
#define __itt_frame_end_v3(d,x)        ITTNOTIFY_VOID_D1(frame_end_v3,d,x)
#define __itt_frame_end_v3_ptr         ITTNOTIFY_NAME(frame_end_v3)
#define __itt_frame_submit_v3(d,x,b,e) ITTNOTIFY_VOID_D3(frame_submit_v3,d,x,b,e)
#define __itt_frame_submit_v3_ptr      ITTNOTIFY_NAME(frame_submit_v3)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_frame_begin_v3(domain,id)
#define __itt_frame_begin_v3_ptr 0
#define __itt_frame_end_v3(domain,id)
#define __itt_frame_end_v3_ptr   0
#define __itt_frame_submit_v3(domain,id,begin,end)
#define __itt_frame_submit_v3_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_frame_begin_v3_ptr 0
#define __itt_frame_end_v3_ptr   0
#define __itt_frame_submit_v3_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} frames group */
/** @endcond */

/**
 * @defgroup taskgroup Task Group
 * @ingroup public
 * Task Group
 * @{
 */
/**
 * @ingroup task_groups
 * @brief Denotes a task_group instance.
 * Successive calls to __itt_task_group with the same ID are ignored.
 * @param[in] domain The domain for this task_group instance
 * @param[in] id The instance ID for this task_group instance. Must not be __itt_null.
 * @param[in] parentid The instance ID for the parent of this task_group instance, or __itt_null.
 * @param[in] name The name of this task_group
 */
void ITTAPI __itt_task_group(const __itt_domain *domain, __itt_id id, __itt_id parentid, __itt_string_handle *name);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, task_group, (const __itt_domain *domain, __itt_id id, __itt_id parentid, __itt_string_handle *name))
#define __itt_task_group(d,x,y,z) ITTNOTIFY_VOID_D3(task_group,d,x,y,z)
#define __itt_task_group_ptr      ITTNOTIFY_NAME(task_group)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_task_group(d,x,y,z)
#define __itt_task_group_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_task_group_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} taskgroup group */

/**
 * @defgroup tasks Tasks
 * @ingroup public
 * A task instance represents a piece of work performed by a particular
 * thread for a period of time. A call to __itt_task_begin creates a
 * task instance. This becomes the current instance for that task on that
 * thread. A following call to __itt_task_end on the same thread ends the
 * instance. There may be multiple simultaneous instances of tasks with the
 * same name on different threads. If an ID is specified, the task instance
 * receives that ID. Nested tasks are allowed.
 *
 * Note: The task is defined by the bracketing of __itt_task_begin and
 * __itt_task_end on the same thread. If some scheduling mechanism causes
 * task switching (the thread executes a different user task) or task
 * switching (the user task switches to a different thread) then this breaks
 * the notion of  current instance. Additional API calls are required to
 * deal with that possibility.
 * @{
 */

/**
 * @ingroup tasks
 * @brief Begin a task instance.
 * @param[in] domain The domain for this task
 * @param[in] taskid The instance ID for this task instance, or __itt_null
 * @param[in] parentid The parent instance to which this task instance belongs, or __itt_null
 * @param[in] name The name of this task
 */
void ITTAPI __itt_task_begin(const __itt_domain *domain, __itt_id taskid, __itt_id parentid, __itt_string_handle *name);

/**
 * @ingroup tasks
 * @brief Begin a task instance.
 * @param[in] domain The domain for this task
 * @param[in] taskid The identifier for this task instance (may be 0)
 * @param[in] parentid The parent of this task (may be 0)
 * @param[in] fn The pointer to the function you are tracing
 */
void ITTAPI __itt_task_begin_fn(const __itt_domain *domain, __itt_id taskid, __itt_id parentid, void* fn);

/**
 * @ingroup tasks
 * @brief End the current task instance.
 * @param[in] domain The domain for this task
 */
void ITTAPI __itt_task_end(const __itt_domain *domain);

/**
 * @ingroup tasks
 * @brief Begin an overlapped task instance.
 * @param[in] domain The domain for this task.
 * @param[in] taskid The identifier for this task instance, *cannot* be __itt_null.
 * @param[in] parentid The parent of this task, or __itt_null.
 * @param[in] name The name of this task.
 */
void ITTAPI __itt_task_begin_overlapped(const __itt_domain* domain, __itt_id taskid, __itt_id parentid, __itt_string_handle* name);

/**
 * @ingroup tasks
 * @brief End an overlapped task instance.
 * @param[in] domain The domain for this task
 * @param[in] taskid Explicit ID of finished task
 */
void ITTAPI __itt_task_end_overlapped(const __itt_domain *domain, __itt_id taskid);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, task_begin,    (const __itt_domain *domain, __itt_id id, __itt_id parentid, __itt_string_handle *name))
ITT_STUBV(ITTAPI, void, task_begin_fn, (const __itt_domain *domain, __itt_id id, __itt_id parentid, void* fn))
ITT_STUBV(ITTAPI, void, task_end,      (const __itt_domain *domain))
ITT_STUBV(ITTAPI, void, task_begin_overlapped, (const __itt_domain *domain, __itt_id taskid, __itt_id parentid, __itt_string_handle *name))
ITT_STUBV(ITTAPI, void, task_end_overlapped,   (const __itt_domain *domain, __itt_id taskid))
#define __itt_task_begin(d,x,y,z)    ITTNOTIFY_VOID_D3(task_begin,d,x,y,z)
#define __itt_task_begin_ptr         ITTNOTIFY_NAME(task_begin)
#define __itt_task_begin_fn(d,x,y,z) ITTNOTIFY_VOID_D3(task_begin_fn,d,x,y,z)
#define __itt_task_begin_fn_ptr      ITTNOTIFY_NAME(task_begin_fn)
#define __itt_task_end(d)            ITTNOTIFY_VOID_D0(task_end,d)
#define __itt_task_end_ptr           ITTNOTIFY_NAME(task_end)
#define __itt_task_begin_overlapped(d,x,y,z) ITTNOTIFY_VOID_D3(task_begin_overlapped,d,x,y,z)
#define __itt_task_begin_overlapped_ptr      ITTNOTIFY_NAME(task_begin_overlapped)
#define __itt_task_end_overlapped(d,x)       ITTNOTIFY_VOID_D1(task_end_overlapped,d,x)
#define __itt_task_end_overlapped_ptr        ITTNOTIFY_NAME(task_end_overlapped)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_task_begin(domain,id,parentid,name)
#define __itt_task_begin_ptr    0
#define __itt_task_begin_fn(domain,id,parentid,fn)
#define __itt_task_begin_fn_ptr 0
#define __itt_task_end(domain)
#define __itt_task_end_ptr      0
#define __itt_task_begin_overlapped(domain,taskid,parentid,name)
#define __itt_task_begin_overlapped_ptr         0
#define __itt_task_end_overlapped(domain,taskid)
#define __itt_task_end_overlapped_ptr           0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_task_begin_ptr    0
#define __itt_task_begin_fn_ptr 0
#define __itt_task_end_ptr      0
#define __itt_task_begin_overlapped_ptr 0
#define __itt_task_end_overlapped_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} tasks group */


/**
 * @defgroup markers Markers
 * Markers represent a single discreet event in time. Markers have a scope,
 * described by an enumerated type __itt_scope. Markers are created by
 * the API call __itt_marker. A marker instance can be given an ID for use in
 * adding metadata.
 * @{
 */

/**
 * @brief Describes the scope of an event object in the trace.
 */
typedef enum
{
    __itt_scope_unknown = 0,
    __itt_scope_global,
    __itt_scope_track_group,
    __itt_scope_track,
    __itt_scope_task,
    __itt_scope_marker
} __itt_scope;

/** @cond exclude_from_documentation */
#define __itt_marker_scope_unknown  __itt_scope_unknown
#define __itt_marker_scope_global   __itt_scope_global
#define __itt_marker_scope_process  __itt_scope_track_group
#define __itt_marker_scope_thread   __itt_scope_track
#define __itt_marker_scope_task     __itt_scope_task
/** @endcond */

/**
 * @ingroup markers
 * @brief Create a marker instance
 * @param[in] domain The domain for this marker
 * @param[in] id The instance ID for this marker or __itt_null
 * @param[in] name The name for this marker
 * @param[in] scope The scope for this marker
 */
void ITTAPI __itt_marker(const __itt_domain *domain, __itt_id id, __itt_string_handle *name, __itt_scope scope);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, marker, (const __itt_domain *domain, __itt_id id, __itt_string_handle *name, __itt_scope scope))
#define __itt_marker(d,x,y,z) ITTNOTIFY_VOID_D3(marker,d,x,y,z)
#define __itt_marker_ptr      ITTNOTIFY_NAME(marker)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_marker(domain,id,name,scope)
#define __itt_marker_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_marker_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} markers group */

/**
 * @defgroup metadata Metadata
 * The metadata API is used to attach extra information to named
 * entities. Metadata can be attached to an identified named entity by ID,
 * or to the current entity (which is always a task).
 *
 * Conceptually metadata has a type (what kind of metadata), a key (the
 * name of the metadata), and a value (the actual data). The encoding of
 * the value depends on the type of the metadata.
 *
 * The type of metadata is specified by an enumerated type __itt_metdata_type.
 * @{
 */

/**
 * @ingroup parameters
 * @brief describes the type of metadata
 */
typedef enum {
    __itt_metadata_unknown = 0,
    __itt_metadata_u64,     /**< Unsigned 64-bit integer */
    __itt_metadata_s64,     /**< Signed 64-bit integer */
    __itt_metadata_u32,     /**< Unsigned 32-bit integer */
    __itt_metadata_s32,     /**< Signed 32-bit integer */
    __itt_metadata_u16,     /**< Unsigned 16-bit integer */
    __itt_metadata_s16,     /**< Signed 16-bit integer */
    __itt_metadata_float,   /**< Signed 32-bit floating-point */
    __itt_metadata_double   /**< SIgned 64-bit floating-point */
} __itt_metadata_type;

/**
 * @ingroup parameters
 * @brief Add metadata to an instance of a named entity.
 * @param[in] domain The domain controlling the call
 * @param[in] id The identifier of the instance to which the metadata is to be added, or __itt_null to add to the current task
 * @param[in] key The name of the metadata
 * @param[in] type The type of the metadata
 * @param[in] count The number of elements of the given type. If count == 0, no metadata will be added.
 * @param[in] data The metadata itself
*/
void ITTAPI __itt_metadata_add(const __itt_domain *domain, __itt_id id, __itt_string_handle *key, __itt_metadata_type type, size_t count, void *data);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, metadata_add, (const __itt_domain *domain, __itt_id id, __itt_string_handle *key, __itt_metadata_type type, size_t count, void *data))
#define __itt_metadata_add(d,x,y,z,a,b) ITTNOTIFY_VOID_D5(metadata_add,d,x,y,z,a,b)
#define __itt_metadata_add_ptr          ITTNOTIFY_NAME(metadata_add)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_metadata_add(d,x,y,z,a,b)
#define __itt_metadata_add_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_metadata_add_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup parameters
 * @brief Add string metadata to an instance of a named entity.
 * @param[in] domain The domain controlling the call
 * @param[in] id The identifier of the instance to which the metadata is to be added, or __itt_null to add to the current task
 * @param[in] key The name of the metadata
 * @param[in] data The metadata itself
 * @param[in] length The number of characters in the string, or -1 if the length is unknown but the string is null-terminated
*/
#if ITT_PLATFORM==ITT_PLATFORM_WIN
void ITTAPI __itt_metadata_str_addA(const __itt_domain *domain, __itt_id id, __itt_string_handle *key, const char *data, size_t length);
void ITTAPI __itt_metadata_str_addW(const __itt_domain *domain, __itt_id id, __itt_string_handle *key, const wchar_t *data, size_t length);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_metadata_str_add     __itt_metadata_str_addW
#  define __itt_metadata_str_add_ptr __itt_metadata_str_addW_ptr
#else /* UNICODE */
#  define __itt_metadata_str_add     __itt_metadata_str_addA
#  define __itt_metadata_str_add_ptr __itt_metadata_str_addA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
void ITTAPI __itt_metadata_str_add(const __itt_domain *domain, __itt_id id, __itt_string_handle *key, const char *data, size_t length);
#endif

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, metadata_str_addA, (const __itt_domain *domain, __itt_id id, __itt_string_handle *key, const char *data, size_t length))
ITT_STUBV(ITTAPI, void, metadata_str_addW, (const __itt_domain *domain, __itt_id id, __itt_string_handle *key, const wchar_t *data, size_t length))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, metadata_str_add, (const __itt_domain *domain, __itt_id id, __itt_string_handle *key, const char *data, size_t length))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_metadata_str_addA(d,x,y,z,a) ITTNOTIFY_VOID_D4(metadata_str_addA,d,x,y,z,a)
#define __itt_metadata_str_addA_ptr        ITTNOTIFY_NAME(metadata_str_addA)
#define __itt_metadata_str_addW(d,x,y,z,a) ITTNOTIFY_VOID_D4(metadata_str_addW,d,x,y,z,a)
#define __itt_metadata_str_addW_ptr        ITTNOTIFY_NAME(metadata_str_addW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_metadata_str_add(d,x,y,z,a)  ITTNOTIFY_VOID_D4(metadata_str_add,d,x,y,z,a)
#define __itt_metadata_str_add_ptr         ITTNOTIFY_NAME(metadata_str_add)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_metadata_str_addA(d,x,y,z,a)
#define __itt_metadata_str_addA_ptr 0
#define __itt_metadata_str_addW(d,x,y,z,a)
#define __itt_metadata_str_addW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_metadata_str_add(d,x,y,z,a)
#define __itt_metadata_str_add_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_metadata_str_addA_ptr 0
#define __itt_metadata_str_addW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_metadata_str_add_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup parameters
 * @brief Add metadata to an instance of a named entity.
 * @param[in] domain The domain controlling the call
 * @param[in] scope The scope of the instance to which the metadata is to be added

 * @param[in] id The identifier of the instance to which the metadata is to be added, or __itt_null to add to the current task

 * @param[in] key The name of the metadata
 * @param[in] type The type of the metadata
 * @param[in] count The number of elements of the given type. If count == 0, no metadata will be added.
 * @param[in] data The metadata itself
*/
void ITTAPI __itt_metadata_add_with_scope(const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, __itt_metadata_type type, size_t count, void *data);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, metadata_add_with_scope, (const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, __itt_metadata_type type, size_t count, void *data))
#define __itt_metadata_add_with_scope(d,x,y,z,a,b) ITTNOTIFY_VOID_D5(metadata_add_with_scope,d,x,y,z,a,b)
#define __itt_metadata_add_with_scope_ptr          ITTNOTIFY_NAME(metadata_add_with_scope)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_metadata_add_with_scope(d,x,y,z,a,b)
#define __itt_metadata_add_with_scope_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_metadata_add_with_scope_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup parameters
 * @brief Add string metadata to an instance of a named entity.
 * @param[in] domain The domain controlling the call
 * @param[in] scope The scope of the instance to which the metadata is to be added

 * @param[in] id The identifier of the instance to which the metadata is to be added, or __itt_null to add to the current task

 * @param[in] key The name of the metadata
 * @param[in] data The metadata itself
 * @param[in] length The number of characters in the string, or -1 if the length is unknown but the string is null-terminated
*/
#if ITT_PLATFORM==ITT_PLATFORM_WIN
void ITTAPI __itt_metadata_str_add_with_scopeA(const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, const char *data, size_t length);
void ITTAPI __itt_metadata_str_add_with_scopeW(const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, const wchar_t *data, size_t length);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_metadata_str_add_with_scope     __itt_metadata_str_add_with_scopeW
#  define __itt_metadata_str_add_with_scope_ptr __itt_metadata_str_add_with_scopeW_ptr
#else /* UNICODE */
#  define __itt_metadata_str_add_with_scope     __itt_metadata_str_add_with_scopeA
#  define __itt_metadata_str_add_with_scope_ptr __itt_metadata_str_add_with_scopeA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
void ITTAPI __itt_metadata_str_add_with_scope(const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, const char *data, size_t length);
#endif

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, metadata_str_add_with_scopeA, (const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, const char *data, size_t length))
ITT_STUBV(ITTAPI, void, metadata_str_add_with_scopeW, (const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, const wchar_t *data, size_t length))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, metadata_str_add_with_scope, (const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, const char *data, size_t length))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_metadata_str_add_with_scopeA(d,x,y,z,a) ITTNOTIFY_VOID_D4(metadata_str_add_with_scopeA,d,x,y,z,a)
#define __itt_metadata_str_add_with_scopeA_ptr        ITTNOTIFY_NAME(metadata_str_add_with_scopeA)
#define __itt_metadata_str_add_with_scopeW(d,x,y,z,a) ITTNOTIFY_VOID_D4(metadata_str_add_with_scopeW,d,x,y,z,a)
#define __itt_metadata_str_add_with_scopeW_ptr        ITTNOTIFY_NAME(metadata_str_add_with_scopeW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_metadata_str_add_with_scope(d,x,y,z,a)  ITTNOTIFY_VOID_D4(metadata_str_add_with_scope,d,x,y,z,a)
#define __itt_metadata_str_add_with_scope_ptr         ITTNOTIFY_NAME(metadata_str_add_with_scope)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_metadata_str_add_with_scopeA(d,x,y,z,a)
#define __itt_metadata_str_add_with_scopeA_ptr  0
#define __itt_metadata_str_add_with_scopeW(d,x,y,z,a)
#define __itt_metadata_str_add_with_scopeW_ptr  0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_metadata_str_add_with_scope(d,x,y,z,a)
#define __itt_metadata_str_add_with_scope_ptr   0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_metadata_str_add_with_scopeA_ptr  0
#define __itt_metadata_str_add_with_scopeW_ptr  0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_metadata_str_add_with_scope_ptr   0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @} metadata group */

/**
 * @defgroup relations Relations
 * Instances of named entities can be explicitly associated with other
 * instances using instance IDs and the relationship API calls.
 *
 * @{
 */

/**
 * @ingroup relations
 * @brief The kind of relation between two instances is specified by the enumerated type __itt_relation.
 * Relations between instances can be added with an API call. The relation
 * API uses instance IDs. Relations can be added before or after the actual
 * instances are created and persist independently of the instances. This
 * is the motivation for having different lifetimes for instance IDs and
 * the actual instances.
 */
typedef enum
{
    __itt_relation_is_unknown = 0,
    __itt_relation_is_dependent_on,         /**< "A is dependent on B" means that A cannot start until B completes */
    __itt_relation_is_sibling_of,           /**< "A is sibling of B" means that A and B were created as a group */
    __itt_relation_is_parent_of,            /**< "A is parent of B" means that A created B */
    __itt_relation_is_continuation_of,      /**< "A is continuation of B" means that A assumes the dependencies of B */
    __itt_relation_is_child_of,             /**< "A is child of B" means that A was created by B (inverse of is_parent_of) */
    __itt_relation_is_continued_by,         /**< "A is continued by B" means that B assumes the dependencies of A (inverse of is_continuation_of) */
    __itt_relation_is_predecessor_to        /**< "A is predecessor to B" means that B cannot start until A completes (inverse of is_dependent_on) */
} __itt_relation;

/**
 * @ingroup relations
 * @brief Add a relation to the current task instance.
 * The current task instance is the head of the relation.
 * @param[in] domain The domain controlling this call
 * @param[in] relation The kind of relation
 * @param[in] tail The ID for the tail of the relation
 */
void ITTAPI __itt_relation_add_to_current(const __itt_domain *domain, __itt_relation relation, __itt_id tail);

/**
 * @ingroup relations
 * @brief Add a relation between two instance identifiers.
 * @param[in] domain The domain controlling this call
 * @param[in] head The ID for the head of the relation
 * @param[in] relation The kind of relation
 * @param[in] tail The ID for the tail of the relation
 */
void ITTAPI __itt_relation_add(const __itt_domain *domain, __itt_id head, __itt_relation relation, __itt_id tail);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, relation_add_to_current, (const __itt_domain *domain, __itt_relation relation, __itt_id tail))
ITT_STUBV(ITTAPI, void, relation_add,            (const __itt_domain *domain, __itt_id head, __itt_relation relation, __itt_id tail))
#define __itt_relation_add_to_current(d,x,y) ITTNOTIFY_VOID_D2(relation_add_to_current,d,x,y)
#define __itt_relation_add_to_current_ptr    ITTNOTIFY_NAME(relation_add_to_current)
#define __itt_relation_add(d,x,y,z)          ITTNOTIFY_VOID_D3(relation_add,d,x,y,z)
#define __itt_relation_add_ptr               ITTNOTIFY_NAME(relation_add)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_relation_add_to_current(d,x,y)
#define __itt_relation_add_to_current_ptr 0
#define __itt_relation_add(d,x,y,z)
#define __itt_relation_add_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_relation_add_to_current_ptr 0
#define __itt_relation_add_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} relations group */

/** @cond exclude_from_documentation */
#pragma pack(push, 8)

typedef struct ___itt_clock_info
{
    unsigned long long clock_freq; /*!< Clock domain frequency */
    unsigned long long clock_base; /*!< Clock domain base timestamp */
} __itt_clock_info;

#pragma pack(pop)
/** @endcond */

/** @cond exclude_from_documentation */
typedef void (ITTAPI *__itt_get_clock_info_fn)(__itt_clock_info* clock_info, void* data);
/** @endcond */

/** @cond exclude_from_documentation */
#pragma pack(push, 8)

typedef struct ___itt_clock_domain
{
    __itt_clock_info info;      /*!< Most recent clock domain info */
    __itt_get_clock_info_fn fn; /*!< Callback function pointer */
    void* fn_data;              /*!< Input argument for the callback function */
    int   extra1;               /*!< Reserved. Must be zero */
    void* extra2;               /*!< Reserved. Must be zero */
    struct ___itt_clock_domain* next;
} __itt_clock_domain;

#pragma pack(pop)
/** @endcond */

/**
 * @ingroup clockdomains
 * @brief Create a clock domain.
 * Certain applications require the capability to trace their application using
 * a clock domain different than the CPU, for instance the instrumentation of events
 * that occur on a GPU.
 * Because the set of domains is expected to be static over the application's execution time,
 * there is no mechanism to destroy a domain.
 * Any domain can be accessed by any thread in the process, regardless of which thread created
 * the domain. This call is thread-safe.
 * @param[in] fn A pointer to a callback function which retrieves alternative CPU timestamps
 * @param[in] fn_data Argument for a callback function; may be NULL
 */
__itt_clock_domain* ITTAPI __itt_clock_domain_create(__itt_get_clock_info_fn fn, void* fn_data);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, __itt_clock_domain*, clock_domain_create, (__itt_get_clock_info_fn fn, void* fn_data))
#define __itt_clock_domain_create     ITTNOTIFY_DATA(clock_domain_create)
#define __itt_clock_domain_create_ptr ITTNOTIFY_NAME(clock_domain_create)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_clock_domain_create(fn,fn_data) (__itt_clock_domain*)0
#define __itt_clock_domain_create_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_clock_domain_create_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup clockdomains
 * @brief Recalculate clock domains frequences and clock base timestamps.
 */
void ITTAPI __itt_clock_domain_reset(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, clock_domain_reset, (void))
#define __itt_clock_domain_reset     ITTNOTIFY_VOID(clock_domain_reset)
#define __itt_clock_domain_reset_ptr ITTNOTIFY_NAME(clock_domain_reset)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_clock_domain_reset()
#define __itt_clock_domain_reset_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_clock_domain_reset_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup clockdomain
 * @brief Create an instance of identifier. This establishes the beginning of the lifetime of
 * an instance of the given ID in the trace. Once this lifetime starts, the ID can be used to
 * tag named entity instances in calls such as __itt_task_begin, and to specify relationships among
 * identified named entity instances, using the \ref relations APIs.
 * @param[in] domain The domain controlling the execution of this call.
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 * @param[in] id The ID to create.
 */
void ITTAPI __itt_id_create_ex(const __itt_domain* domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id);

/**
 * @ingroup clockdomain
 * @brief Destroy an instance of identifier. This ends the lifetime of the current instance of the
 * given ID value in the trace. Any relationships that are established after this lifetime ends are
 * invalid. This call must be performed before the given ID value can be reused for a different
 * named entity instance.
 * @param[in] domain The domain controlling the execution of this call.
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 * @param[in] id The ID to destroy.
 */
void ITTAPI __itt_id_destroy_ex(const __itt_domain* domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, id_create_ex,  (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id))
ITT_STUBV(ITTAPI, void, id_destroy_ex, (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id))
#define __itt_id_create_ex(d,x,y,z)  ITTNOTIFY_VOID_D3(id_create_ex,d,x,y,z)
#define __itt_id_create_ex_ptr       ITTNOTIFY_NAME(id_create_ex)
#define __itt_id_destroy_ex(d,x,y,z) ITTNOTIFY_VOID_D3(id_destroy_ex,d,x,y,z)
#define __itt_id_destroy_ex_ptr      ITTNOTIFY_NAME(id_destroy_ex)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_id_create_ex(domain,clock_domain,timestamp,id)
#define __itt_id_create_ex_ptr    0
#define __itt_id_destroy_ex(domain,clock_domain,timestamp,id)
#define __itt_id_destroy_ex_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_id_create_ex_ptr    0
#define __itt_id_destroy_ex_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup clockdomain
 * @brief Begin a task instance.
 * @param[in] domain The domain for this task
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 * @param[in] taskid The instance ID for this task instance, or __itt_null
 * @param[in] parentid The parent instance to which this task instance belongs, or __itt_null
 * @param[in] name The name of this task
 */
void ITTAPI __itt_task_begin_ex(const __itt_domain* domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id taskid, __itt_id parentid, __itt_string_handle* name);

/**
 * @ingroup clockdomain
 * @brief Begin a task instance.
 * @param[in] domain The domain for this task
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 * @param[in] taskid The identifier for this task instance, or __itt_null
 * @param[in] parentid The parent of this task, or __itt_null
 * @param[in] fn The pointer to the function you are tracing
 */
void ITTAPI __itt_task_begin_fn_ex(const __itt_domain* domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id taskid, __itt_id parentid, void* fn);

/**
 * @ingroup clockdomain
 * @brief End the current task instance.
 * @param[in] domain The domain for this task
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 */
void ITTAPI __itt_task_end_ex(const __itt_domain* domain, __itt_clock_domain* clock_domain, unsigned long long timestamp);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, task_begin_ex,        (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id, __itt_id parentid, __itt_string_handle *name))
ITT_STUBV(ITTAPI, void, task_begin_fn_ex,     (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id, __itt_id parentid, void* fn))
ITT_STUBV(ITTAPI, void, task_end_ex,          (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp))
#define __itt_task_begin_ex(d,x,y,z,a,b)      ITTNOTIFY_VOID_D5(task_begin_ex,d,x,y,z,a,b)
#define __itt_task_begin_ex_ptr               ITTNOTIFY_NAME(task_begin_ex)
#define __itt_task_begin_fn_ex(d,x,y,z,a,b)   ITTNOTIFY_VOID_D5(task_begin_fn_ex,d,x,y,z,a,b)
#define __itt_task_begin_fn_ex_ptr            ITTNOTIFY_NAME(task_begin_fn_ex)
#define __itt_task_end_ex(d,x,y)              ITTNOTIFY_VOID_D2(task_end_ex,d,x,y)
#define __itt_task_end_ex_ptr                 ITTNOTIFY_NAME(task_end_ex)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_task_begin_ex(domain,clock_domain,timestamp,id,parentid,name)
#define __itt_task_begin_ex_ptr          0
#define __itt_task_begin_fn_ex(domain,clock_domain,timestamp,id,parentid,fn)
#define __itt_task_begin_fn_ex_ptr       0
#define __itt_task_end_ex(domain,clock_domain,timestamp)
#define __itt_task_end_ex_ptr            0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_task_begin_ex_ptr          0
#define __itt_task_begin_fn_ex_ptr       0
#define __itt_task_end_ex_ptr            0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @defgroup counters Counters
 * @ingroup public
 * Counters are user-defined objects with a monotonically increasing
 * value. Counter values are 64-bit unsigned integers.
 * Counters have names that can be displayed in
 * the tools.
 * @{
 */

/**
 * @brief opaque structure for counter identification
 */
/** @cond exclude_from_documentation */

typedef struct ___itt_counter* __itt_counter;

/**
 * @brief Create an unsigned 64 bits integer counter with given name/domain
 *
 * After __itt_counter_create() is called, __itt_counter_inc(id), __itt_counter_inc_delta(id, delta),
 * __itt_counter_set_value(id, value_ptr) or __itt_counter_set_value_ex(id, clock_domain, timestamp, value_ptr)
 * can be used to change the value of the counter, where value_ptr is a pointer to an unsigned 64 bits integer
 *
 * The call is equal to __itt_counter_create_typed(name, domain, __itt_metadata_u64)
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
__itt_counter ITTAPI __itt_counter_createA(const char    *name, const char    *domain);
__itt_counter ITTAPI __itt_counter_createW(const wchar_t *name, const wchar_t *domain);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_counter_create     __itt_counter_createW
#  define __itt_counter_create_ptr __itt_counter_createW_ptr
#else /* UNICODE */
#  define __itt_counter_create     __itt_counter_createA
#  define __itt_counter_create_ptr __itt_counter_createA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
__itt_counter ITTAPI __itt_counter_create(const char *name, const char *domain);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_counter, counter_createA, (const char    *name, const char    *domain))
ITT_STUB(ITTAPI, __itt_counter, counter_createW, (const wchar_t *name, const wchar_t *domain))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_counter, counter_create,  (const char *name, const char *domain))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_counter_createA     ITTNOTIFY_DATA(counter_createA)
#define __itt_counter_createA_ptr ITTNOTIFY_NAME(counter_createA)
#define __itt_counter_createW     ITTNOTIFY_DATA(counter_createW)
#define __itt_counter_createW_ptr ITTNOTIFY_NAME(counter_createW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_counter_create     ITTNOTIFY_DATA(counter_create)
#define __itt_counter_create_ptr ITTNOTIFY_NAME(counter_create)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_counter_createA(name, domain)
#define __itt_counter_createA_ptr 0
#define __itt_counter_createW(name, domain)
#define __itt_counter_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_counter_create(name, domain)
#define __itt_counter_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_counter_createA_ptr 0
#define __itt_counter_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_counter_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Increment the unsigned 64 bits integer counter value
 *
 * Calling this function to non-unsigned 64 bits integer counters has no effect
 */
void ITTAPI __itt_counter_inc(__itt_counter id);

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, counter_inc, (__itt_counter id))
#define __itt_counter_inc     ITTNOTIFY_VOID(counter_inc)
#define __itt_counter_inc_ptr ITTNOTIFY_NAME(counter_inc)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_counter_inc(id)
#define __itt_counter_inc_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_counter_inc_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/**
 * @brief Increment the unsigned 64 bits integer counter value with x
 *
 * Calling this function to non-unsigned 64 bits integer counters has no effect
 */
void ITTAPI __itt_counter_inc_delta(__itt_counter id, unsigned long long value);

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, counter_inc_delta, (__itt_counter id, unsigned long long value))
#define __itt_counter_inc_delta     ITTNOTIFY_VOID(counter_inc_delta)
#define __itt_counter_inc_delta_ptr ITTNOTIFY_NAME(counter_inc_delta)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_counter_inc_delta(id, value)
#define __itt_counter_inc_delta_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_counter_inc_delta_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Decrement the unsigned 64 bits integer counter value
 *
 * Calling this function to non-unsigned 64 bits integer counters has no effect
 */
void ITTAPI __itt_counter_dec(__itt_counter id);

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, counter_dec, (__itt_counter id))
#define __itt_counter_dec     ITTNOTIFY_VOID(counter_dec)
#define __itt_counter_dec_ptr ITTNOTIFY_NAME(counter_dec)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_counter_dec(id)
#define __itt_counter_dec_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_counter_dec_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/**
 * @brief Decrement the unsigned 64 bits integer counter value with x
 *
 * Calling this function to non-unsigned 64 bits integer counters has no effect
 */
void ITTAPI __itt_counter_dec_delta(__itt_counter id, unsigned long long value);

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, counter_dec_delta, (__itt_counter id, unsigned long long value))
#define __itt_counter_dec_delta     ITTNOTIFY_VOID(counter_dec_delta)
#define __itt_counter_dec_delta_ptr ITTNOTIFY_NAME(counter_dec_delta)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_counter_dec_delta(id, value)
#define __itt_counter_dec_delta_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_counter_dec_delta_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup counters
 * @brief Increment a counter by one.
 * The first call with a given name creates a counter by that name and sets its
 * value to zero. Successive calls increment the counter value.
 * @param[in] domain The domain controlling the call. Counter names are not domain specific.
 *            The domain argument is used only to enable or disable the API calls.
 * @param[in] name The name of the counter
 */
void ITTAPI __itt_counter_inc_v3(const __itt_domain *domain, __itt_string_handle *name);

/**
 * @ingroup counters
 * @brief Increment a counter by the value specified in delta.
 * @param[in] domain The domain controlling the call. Counter names are not domain specific.
 *            The domain argument is used only to enable or disable the API calls.
 * @param[in] name The name of the counter
 * @param[in] delta The amount by which to increment the counter
 */
void ITTAPI __itt_counter_inc_delta_v3(const __itt_domain *domain, __itt_string_handle *name, unsigned long long delta);

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, counter_inc_v3,       (const __itt_domain *domain, __itt_string_handle *name))
ITT_STUBV(ITTAPI, void, counter_inc_delta_v3, (const __itt_domain *domain, __itt_string_handle *name, unsigned long long delta))
#define __itt_counter_inc_v3(d,x)         ITTNOTIFY_VOID_D1(counter_inc_v3,d,x)
#define __itt_counter_inc_v3_ptr          ITTNOTIFY_NAME(counter_inc_v3)
#define __itt_counter_inc_delta_v3(d,x,y) ITTNOTIFY_VOID_D2(counter_inc_delta_v3,d,x,y)
#define __itt_counter_inc_delta_v3_ptr    ITTNOTIFY_NAME(counter_inc_delta_v3)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_counter_inc_v3(domain,name)
#define __itt_counter_inc_v3_ptr       0
#define __itt_counter_inc_delta_v3(domain,name,delta)
#define __itt_counter_inc_delta_v3_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_counter_inc_v3_ptr       0
#define __itt_counter_inc_delta_v3_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */


/**
 * @ingroup counters
 * @brief Decrement a counter by one.
 * The first call with a given name creates a counter by that name and sets its
 * value to zero. Successive calls decrement the counter value.
 * @param[in] domain The domain controlling the call. Counter names are not domain specific.
 *            The domain argument is used only to enable or disable the API calls.
 * @param[in] name The name of the counter
 */
void ITTAPI __itt_counter_dec_v3(const __itt_domain *domain, __itt_string_handle *name);

/**
 * @ingroup counters
 * @brief Decrement a counter by the value specified in delta.
 * @param[in] domain The domain controlling the call. Counter names are not domain specific.
 *            The domain argument is used only to enable or disable the API calls.
 * @param[in] name The name of the counter
 * @param[in] delta The amount by which to decrement the counter
 */
void ITTAPI __itt_counter_dec_delta_v3(const __itt_domain *domain, __itt_string_handle *name, unsigned long long delta);

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, counter_dec_v3,       (const __itt_domain *domain, __itt_string_handle *name))
ITT_STUBV(ITTAPI, void, counter_dec_delta_v3, (const __itt_domain *domain, __itt_string_handle *name, unsigned long long delta))
#define __itt_counter_dec_v3(d,x)         ITTNOTIFY_VOID_D1(counter_dec_v3,d,x)
#define __itt_counter_dec_v3_ptr          ITTNOTIFY_NAME(counter_dec_v3)
#define __itt_counter_dec_delta_v3(d,x,y) ITTNOTIFY_VOID_D2(counter_dec_delta_v3,d,x,y)
#define __itt_counter_dec_delta_v3_ptr    ITTNOTIFY_NAME(counter_dec_delta_v3)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_counter_dec_v3(domain,name)
#define __itt_counter_dec_v3_ptr       0
#define __itt_counter_dec_delta_v3(domain,name,delta)
#define __itt_counter_dec_delta_v3_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_counter_dec_v3_ptr       0
#define __itt_counter_dec_delta_v3_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @} counters group */


/**
 * @brief Set the counter value
 */
void ITTAPI __itt_counter_set_value(__itt_counter id, void *value_ptr);

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, counter_set_value, (__itt_counter id, void *value_ptr))
#define __itt_counter_set_value     ITTNOTIFY_VOID(counter_set_value)
#define __itt_counter_set_value_ptr ITTNOTIFY_NAME(counter_set_value)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_counter_set_value(id, value_ptr)
#define __itt_counter_set_value_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_counter_set_value_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Set the counter value
 */
void ITTAPI __itt_counter_set_value_ex(__itt_counter id, __itt_clock_domain *clock_domain, unsigned long long timestamp, void *value_ptr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, counter_set_value_ex, (__itt_counter id, __itt_clock_domain *clock_domain, unsigned long long timestamp, void *value_ptr))
#define __itt_counter_set_value_ex     ITTNOTIFY_VOID(counter_set_value_ex)
#define __itt_counter_set_value_ex_ptr ITTNOTIFY_NAME(counter_set_value_ex)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_counter_set_value_ex(id, clock_domain, timestamp, value_ptr)
#define __itt_counter_set_value_ex_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_counter_set_value_ex_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Create a typed counter with given name/domain
 *
 * After __itt_counter_create_typed() is called, __itt_counter_inc(id), __itt_counter_inc_delta(id, delta),
 * __itt_counter_set_value(id, value_ptr) or __itt_counter_set_value_ex(id, clock_domain, timestamp, value_ptr)
 * can be used to change the value of the counter
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
__itt_counter ITTAPI __itt_counter_create_typedA(const char    *name, const char    *domain, __itt_metadata_type type);
__itt_counter ITTAPI __itt_counter_create_typedW(const wchar_t *name, const wchar_t *domain, __itt_metadata_type type);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_counter_create_typed     __itt_counter_create_typedW
#  define __itt_counter_create_typed_ptr __itt_counter_create_typedW_ptr
#else /* UNICODE */
#  define __itt_counter_create_typed     __itt_counter_create_typedA
#  define __itt_counter_create_typed_ptr __itt_counter_create_typedA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
__itt_counter ITTAPI __itt_counter_create_typed(const char *name, const char *domain, __itt_metadata_type type);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_counter, counter_create_typedA, (const char    *name, const char    *domain, __itt_metadata_type type))
ITT_STUB(ITTAPI, __itt_counter, counter_create_typedW, (const wchar_t *name, const wchar_t *domain, __itt_metadata_type type))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_counter, counter_create_typed,  (const char *name, const char *domain, __itt_metadata_type type))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_counter_create_typedA     ITTNOTIFY_DATA(counter_create_typedA)
#define __itt_counter_create_typedA_ptr ITTNOTIFY_NAME(counter_create_typedA)
#define __itt_counter_create_typedW     ITTNOTIFY_DATA(counter_create_typedW)
#define __itt_counter_create_typedW_ptr ITTNOTIFY_NAME(counter_create_typedW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_counter_create_typed     ITTNOTIFY_DATA(counter_create_typed)
#define __itt_counter_create_typed_ptr ITTNOTIFY_NAME(counter_create_typed)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_counter_create_typedA(name, domain, type)
#define __itt_counter_create_typedA_ptr 0
#define __itt_counter_create_typedW(name, domain, type)
#define __itt_counter_create_typedW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_counter_create_typed(name, domain, type)
#define __itt_counter_create_typed_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_counter_create_typedA_ptr 0
#define __itt_counter_create_typedW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_counter_create_typed_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Destroy the counter identified by the pointer previously returned by __itt_counter_create() or
 * __itt_counter_create_typed()
 */
void ITTAPI __itt_counter_destroy(__itt_counter id);

#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, counter_destroy, (__itt_counter id))
#define __itt_counter_destroy     ITTNOTIFY_VOID(counter_destroy)
#define __itt_counter_destroy_ptr ITTNOTIFY_NAME(counter_destroy)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_counter_destroy(id)
#define __itt_counter_destroy_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_counter_destroy_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} counters group */

/**
 * @ingroup markers
 * @brief Create a marker instance.
 * @param[in] domain The domain for this marker
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 * @param[in] id The instance ID for this marker, or __itt_null
 * @param[in] name The name for this marker
 * @param[in] scope The scope for this marker
 */
void ITTAPI __itt_marker_ex(const __itt_domain *domain,  __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id, __itt_string_handle *name, __itt_scope scope);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, marker_ex,    (const __itt_domain *domain,  __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id, __itt_string_handle *name, __itt_scope scope))
#define __itt_marker_ex(d,x,y,z,a,b)    ITTNOTIFY_VOID_D5(marker_ex,d,x,y,z,a,b)
#define __itt_marker_ex_ptr             ITTNOTIFY_NAME(marker_ex)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_marker_ex(domain,clock_domain,timestamp,id,name,scope)
#define __itt_marker_ex_ptr    0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_marker_ex_ptr    0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @ingroup clockdomain
 * @brief Add a relation to the current task instance.
 * The current task instance is the head of the relation.
 * @param[in] domain The domain controlling this call
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 * @param[in] relation The kind of relation
 * @param[in] tail The ID for the tail of the relation
 */
void ITTAPI __itt_relation_add_to_current_ex(const __itt_domain *domain,  __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_relation relation, __itt_id tail);

/**
 * @ingroup clockdomain
 * @brief Add a relation between two instance identifiers.
 * @param[in] domain The domain controlling this call
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 * @param[in] head The ID for the head of the relation
 * @param[in] relation The kind of relation
 * @param[in] tail The ID for the tail of the relation
 */
void ITTAPI __itt_relation_add_ex(const __itt_domain *domain,  __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id head, __itt_relation relation, __itt_id tail);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, relation_add_to_current_ex, (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_relation relation, __itt_id tail))
ITT_STUBV(ITTAPI, void, relation_add_ex,            (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id head, __itt_relation relation, __itt_id tail))
#define __itt_relation_add_to_current_ex(d,x,y,z,a) ITTNOTIFY_VOID_D4(relation_add_to_current_ex,d,x,y,z,a)
#define __itt_relation_add_to_current_ex_ptr        ITTNOTIFY_NAME(relation_add_to_current_ex)
#define __itt_relation_add_ex(d,x,y,z,a,b)          ITTNOTIFY_VOID_D5(relation_add_ex,d,x,y,z,a,b)
#define __itt_relation_add_ex_ptr                   ITTNOTIFY_NAME(relation_add_ex)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_relation_add_to_current_ex(domain,clock_domain,timestame,relation,tail)
#define __itt_relation_add_to_current_ex_ptr 0
#define __itt_relation_add_ex(domain,clock_domain,timestamp,head,relation,tail)
#define __itt_relation_add_ex_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_relation_add_to_current_ex_ptr 0
#define __itt_relation_add_ex_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @cond exclude_from_documentation */
typedef enum ___itt_track_group_type
{
    __itt_track_group_type_normal = 0
} __itt_track_group_type;
/** @endcond */

/** @cond exclude_from_documentation */
#pragma pack(push, 8)

typedef struct ___itt_track_group
{
    __itt_string_handle* name;     /*!< Name of the track group */
    struct ___itt_track* track;    /*!< List of child tracks    */
    __itt_track_group_type tgtype; /*!< Type of the track group */
    int   extra1;                  /*!< Reserved. Must be zero  */
    void* extra2;                  /*!< Reserved. Must be zero  */
    struct ___itt_track_group* next;
} __itt_track_group;

#pragma pack(pop)
/** @endcond */

/**
 * @brief Placeholder for custom track types. Currently, "normal" custom track
 * is the only available track type.
 */
typedef enum ___itt_track_type
{
    __itt_track_type_normal = 0
#ifdef INTEL_ITTNOTIFY_API_PRIVATE
    , __itt_track_type_queue
#endif /* INTEL_ITTNOTIFY_API_PRIVATE */
} __itt_track_type;

/** @cond exclude_from_documentation */
#pragma pack(push, 8)

typedef struct ___itt_track
{
    __itt_string_handle* name; /*!< Name of the track group */
    __itt_track_group* group;  /*!< Parent group to a track */
    __itt_track_type ttype;    /*!< Type of the track       */
    int   extra1;              /*!< Reserved. Must be zero  */
    void* extra2;              /*!< Reserved. Must be zero  */
    struct ___itt_track* next;
} __itt_track;

#pragma pack(pop)
/** @endcond */

/**
 * @brief Create logical track group.
 */
__itt_track_group* ITTAPI __itt_track_group_create(__itt_string_handle* name, __itt_track_group_type track_group_type);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, __itt_track_group*, track_group_create, (__itt_string_handle* name, __itt_track_group_type track_group_type))
#define __itt_track_group_create     ITTNOTIFY_DATA(track_group_create)
#define __itt_track_group_create_ptr ITTNOTIFY_NAME(track_group_create)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_track_group_create(name)  (__itt_track_group*)0
#define __itt_track_group_create_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_track_group_create_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Create logical track.
 */
__itt_track* ITTAPI __itt_track_create(__itt_track_group* track_group, __itt_string_handle* name, __itt_track_type track_type);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, __itt_track*, track_create, (__itt_track_group* track_group,__itt_string_handle* name, __itt_track_type track_type))
#define __itt_track_create     ITTNOTIFY_DATA(track_create)
#define __itt_track_create_ptr ITTNOTIFY_NAME(track_create)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_track_create(track_group,name,track_type)  (__itt_track*)0
#define __itt_track_create_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_track_create_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Set the logical track.
 */
void ITTAPI __itt_set_track(__itt_track* track);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, set_track, (__itt_track *track))
#define __itt_set_track     ITTNOTIFY_VOID(set_track)
#define __itt_set_track_ptr ITTNOTIFY_NAME(set_track)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_set_track(track)
#define __itt_set_track_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_set_track_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/* ========================================================================== */
/** @cond exclude_from_gpa_documentation */
/**
 * @defgroup events Events
 * @ingroup public
 * Events group
 * @{
 */
/** @brief user event type */
typedef int __itt_event;

/**
 * @brief Create an event notification
 * @note name or namelen being null/name and namelen not matching, user event feature not enabled
 * @return non-zero event identifier upon success and __itt_err otherwise
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
__itt_event LIBITTAPI __itt_event_createA(const char    *name, int namelen);
__itt_event LIBITTAPI __itt_event_createW(const wchar_t *name, int namelen);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_event_create     __itt_event_createW
#  define __itt_event_create_ptr __itt_event_createW_ptr
#else
#  define __itt_event_create     __itt_event_createA
#  define __itt_event_create_ptr __itt_event_createA_ptr
#endif /* UNICODE */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
__itt_event LIBITTAPI __itt_event_create(const char *name, int namelen);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(LIBITTAPI, __itt_event, event_createA, (const char    *name, int namelen))
ITT_STUB(LIBITTAPI, __itt_event, event_createW, (const wchar_t *name, int namelen))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(LIBITTAPI, __itt_event, event_create,  (const char    *name, int namelen))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_event_createA     ITTNOTIFY_DATA(event_createA)
#define __itt_event_createA_ptr ITTNOTIFY_NAME(event_createA)
#define __itt_event_createW     ITTNOTIFY_DATA(event_createW)
#define __itt_event_createW_ptr ITTNOTIFY_NAME(event_createW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_event_create      ITTNOTIFY_DATA(event_create)
#define __itt_event_create_ptr  ITTNOTIFY_NAME(event_create)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_event_createA(name, namelen) (__itt_event)0
#define __itt_event_createA_ptr 0
#define __itt_event_createW(name, namelen) (__itt_event)0
#define __itt_event_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_event_create(name, namelen)  (__itt_event)0
#define __itt_event_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_event_createA_ptr 0
#define __itt_event_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_event_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Record an event occurrence.
 * @return __itt_err upon failure (invalid event id/user event feature not enabled)
 */
int LIBITTAPI __itt_event_start(__itt_event event);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(LIBITTAPI, int, event_start, (__itt_event event))
#define __itt_event_start     ITTNOTIFY_DATA(event_start)
#define __itt_event_start_ptr ITTNOTIFY_NAME(event_start)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_event_start(event) (int)0
#define __itt_event_start_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_event_start_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Record an event end occurrence.
 * @note It is optional if events do not have durations.
 * @return __itt_err upon failure (invalid event id/user event feature not enabled)
 */
int LIBITTAPI __itt_event_end(__itt_event event);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(LIBITTAPI, int, event_end, (__itt_event event))
#define __itt_event_end     ITTNOTIFY_DATA(event_end)
#define __itt_event_end_ptr ITTNOTIFY_NAME(event_end)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_event_end(event) (int)0
#define __itt_event_end_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_event_end_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} events group */


/**
 * @defgroup arrays Arrays Visualizer
 * @ingroup public
 * Visualize arrays
 * @{
 */

/**
 * @enum __itt_av_data_type
 * @brief Defines types of arrays data (for C/C++ intrinsic types)
 */
typedef enum
{
    __itt_e_first = 0,
    __itt_e_char = 0,  /* 1-byte integer */
    __itt_e_uchar,     /* 1-byte unsigned integer */
    __itt_e_int16,     /* 2-byte integer */
    __itt_e_uint16,    /* 2-byte unsigned integer  */
    __itt_e_int32,     /* 4-byte integer */
    __itt_e_uint32,    /* 4-byte unsigned integer */
    __itt_e_int64,     /* 8-byte integer */
    __itt_e_uint64,    /* 8-byte unsigned integer */
    __itt_e_float,     /* 4-byte floating */
    __itt_e_double,    /* 8-byte floating */
    __itt_e_last = __itt_e_double
} __itt_av_data_type;

/**
 * @brief Save an array data to a file.
 * Output format is defined by the file extension. The csv and bmp formats are supported (bmp - for 2-dimensional array only).
 * @param[in] data - pointer to the array data
 * @param[in] rank - the rank of the array
 * @param[in] dimensions - pointer to an array of integers, which specifies the array dimensions.
 * The size of dimensions must be equal to the rank
 * @param[in] type - the type of the array, specified as one of the __itt_av_data_type values (for intrinsic types)
 * @param[in] filePath - the file path; the output format is defined by the file extension
 * @param[in] columnOrder - defines how the array is stored in the linear memory.
 * It should be 1 for column-major order (e.g. in FORTRAN) or 0 - for row-major order (e.g. in C).
 */

#if ITT_PLATFORM==ITT_PLATFORM_WIN
int ITTAPI __itt_av_saveA(void *data, int rank, const int *dimensions, int type, const char *filePath, int columnOrder);
int ITTAPI __itt_av_saveW(void *data, int rank, const int *dimensions, int type, const wchar_t *filePath, int columnOrder);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_av_save     __itt_av_saveW
#  define __itt_av_save_ptr __itt_av_saveW_ptr
#else /* UNICODE */
#  define __itt_av_save     __itt_av_saveA
#  define __itt_av_save_ptr __itt_av_saveA_ptr
#endif /* UNICODE */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
int ITTAPI __itt_av_save(void *data, int rank, const int *dimensions, int type, const char *filePath, int columnOrder);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, int, av_saveA, (void *data, int rank, const int *dimensions, int type, const char *filePath, int columnOrder))
ITT_STUB(ITTAPI, int, av_saveW, (void *data, int rank, const int *dimensions, int type, const wchar_t *filePath, int columnOrder))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, int, av_save,  (void *data, int rank, const int *dimensions, int type, const char *filePath, int columnOrder))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_av_saveA     ITTNOTIFY_DATA(av_saveA)
#define __itt_av_saveA_ptr ITTNOTIFY_NAME(av_saveA)
#define __itt_av_saveW     ITTNOTIFY_DATA(av_saveW)
#define __itt_av_saveW_ptr ITTNOTIFY_NAME(av_saveW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_av_save     ITTNOTIFY_DATA(av_save)
#define __itt_av_save_ptr ITTNOTIFY_NAME(av_save)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_av_saveA(name)
#define __itt_av_saveA_ptr 0
#define __itt_av_saveW(name)
#define __itt_av_saveW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_av_save(name)
#define __itt_av_save_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_av_saveA_ptr 0
#define __itt_av_saveW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_av_save_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

void ITTAPI __itt_enable_attach(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, enable_attach, (void))
#define __itt_enable_attach     ITTNOTIFY_VOID(enable_attach)
#define __itt_enable_attach_ptr ITTNOTIFY_NAME(enable_attach)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_enable_attach()
#define __itt_enable_attach_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_enable_attach_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @cond exclude_from_gpa_documentation */

/** @} arrays group */

/** @endcond */

/**
 * @brief Module load info
 * This API is used to report necessary information in case of module relocation
 * @param[in] start_addr - relocated module start address
 * @param[in] end_addr - relocated module end address
 * @param[in] path - file system path to the module
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
void ITTAPI __itt_module_loadA(void *start_addr, void *end_addr, const char *path);
void ITTAPI __itt_module_loadW(void *start_addr, void *end_addr, const wchar_t *path);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_module_load     __itt_module_loadW
#  define __itt_module_load_ptr __itt_module_loadW_ptr
#else /* UNICODE */
#  define __itt_module_load     __itt_module_loadA
#  define __itt_module_load_ptr __itt_module_loadA_ptr
#endif /* UNICODE */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
void ITTAPI __itt_module_load(void *start_addr, void *end_addr, const char *path);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, void, module_loadA, (void *start_addr, void *end_addr, const char *path))
ITT_STUB(ITTAPI, void, module_loadW, (void *start_addr, void *end_addr, const wchar_t *path))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, void, module_load,  (void *start_addr, void *end_addr, const char *path))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_module_loadA     ITTNOTIFY_VOID(module_loadA)
#define __itt_module_loadA_ptr ITTNOTIFY_NAME(module_loadA)
#define __itt_module_loadW     ITTNOTIFY_VOID(module_loadW)
#define __itt_module_loadW_ptr ITTNOTIFY_NAME(module_loadW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_module_load     ITTNOTIFY_VOID(module_load)
#define __itt_module_load_ptr ITTNOTIFY_NAME(module_load)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_module_loadA(start_addr, end_addr, path)
#define __itt_module_loadA_ptr 0
#define __itt_module_loadW(start_addr, end_addr, path)
#define __itt_module_loadW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_module_load(start_addr, end_addr, path)
#define __itt_module_load_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_module_loadA_ptr 0
#define __itt_module_loadW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_module_load_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ITTNOTIFY_H_ */

#ifdef INTEL_ITTNOTIFY_API_PRIVATE

#ifndef _ITTNOTIFY_PRIVATE_
#define _ITTNOTIFY_PRIVATE_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @ingroup clockdomain
 * @brief Begin an overlapped task instance.
 * @param[in] domain The domain for this task
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 * @param[in] taskid The identifier for this task instance, *cannot* be __itt_null.
 * @param[in] parentid The parent of this task, or __itt_null.
 * @param[in] name The name of this task.
 */
void ITTAPI __itt_task_begin_overlapped_ex(const __itt_domain* domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id taskid, __itt_id parentid, __itt_string_handle* name);

/**
 * @ingroup clockdomain
 * @brief End an overlapped task instance.
 * @param[in] domain The domain for this task
 * @param[in] clock_domain The clock domain controlling the execution of this call.
 * @param[in] timestamp The user defined timestamp.
 * @param[in] taskid Explicit ID of finished task
 */
void ITTAPI __itt_task_end_overlapped_ex(const __itt_domain* domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id taskid);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, task_begin_overlapped_ex,       (const __itt_domain* domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id taskid, __itt_id parentid, __itt_string_handle* name))
ITT_STUBV(ITTAPI, void, task_end_overlapped_ex,         (const __itt_domain* domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id taskid))
#define __itt_task_begin_overlapped_ex(d,x,y,z,a,b)     ITTNOTIFY_VOID_D5(task_begin_overlapped_ex,d,x,y,z,a,b)
#define __itt_task_begin_overlapped_ex_ptr              ITTNOTIFY_NAME(task_begin_overlapped_ex)
#define __itt_task_end_overlapped_ex(d,x,y,z)           ITTNOTIFY_VOID_D3(task_end_overlapped_ex,d,x,y,z)
#define __itt_task_end_overlapped_ex_ptr                ITTNOTIFY_NAME(task_end_overlapped_ex)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_task_begin_overlapped_ex(domain,clock_domain,timestamp,taskid,parentid,name)
#define __itt_task_begin_overlapped_ex_ptr      0
#define __itt_task_end_overlapped_ex(domain,clock_domain,timestamp,taskid)
#define __itt_task_end_overlapped_ex_ptr        0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_task_begin_overlapped_ex_ptr      0
#define __itt_task_end_overlapped_ptr           0
#define __itt_task_end_overlapped_ex_ptr        0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @defgroup makrs_internal Marks
 * @ingroup internal
 * Marks group
 * @warning Internal API:
 *   - It is not shipped to outside of Intel
 *   - It is delivered to internal Intel teams using e-mail or SVN access only
 * @{
 */
/** @brief user mark type */
typedef int __itt_mark_type;

/**
 * @brief Creates a user mark type with the specified name using char or Unicode string.
 * @param[in] name - name of mark to create
 * @return Returns a handle to the mark type
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
__itt_mark_type ITTAPI __itt_mark_createA(const char    *name);
__itt_mark_type ITTAPI __itt_mark_createW(const wchar_t *name);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_mark_create     __itt_mark_createW
#  define __itt_mark_create_ptr __itt_mark_createW_ptr
#else /* UNICODE */
#  define __itt_mark_create     __itt_mark_createA
#  define __itt_mark_create_ptr __itt_mark_createA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
__itt_mark_type ITTAPI __itt_mark_create(const char *name);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_mark_type, mark_createA, (const char    *name))
ITT_STUB(ITTAPI, __itt_mark_type, mark_createW, (const wchar_t *name))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_mark_type, mark_create,  (const char *name))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_mark_createA     ITTNOTIFY_DATA(mark_createA)
#define __itt_mark_createA_ptr ITTNOTIFY_NAME(mark_createA)
#define __itt_mark_createW     ITTNOTIFY_DATA(mark_createW)
#define __itt_mark_createW_ptr ITTNOTIFY_NAME(mark_createW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_mark_create      ITTNOTIFY_DATA(mark_create)
#define __itt_mark_create_ptr  ITTNOTIFY_NAME(mark_create)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_mark_createA(name) (__itt_mark_type)0
#define __itt_mark_createA_ptr 0
#define __itt_mark_createW(name) (__itt_mark_type)0
#define __itt_mark_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_mark_create(name)  (__itt_mark_type)0
#define __itt_mark_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_mark_createA_ptr 0
#define __itt_mark_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_mark_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Creates a "discrete" user mark type of the specified type and an optional parameter using char or Unicode string.
 *
 * - The mark of "discrete" type is placed to collection results in case of success. It appears in overtime view(s) as a special tick sign.
 * - The call is "synchronous" - function returns after mark is actually added to results.
 * - This function is useful, for example, to mark different phases of application
 *   (beginning of the next mark automatically meand end of current region).
 * - Can be used together with "continuous" marks (see below) at the same collection session
 * @param[in] mt - mark, created by __itt_mark_create(const char* name) function
 * @param[in] parameter - string parameter of mark
 * @return Returns zero value in case of success, non-zero value otherwise.
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
int ITTAPI __itt_markA(__itt_mark_type mt, const char    *parameter);
int ITTAPI __itt_markW(__itt_mark_type mt, const wchar_t *parameter);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_mark     __itt_markW
#  define __itt_mark_ptr __itt_markW_ptr
#else /* UNICODE  */
#  define __itt_mark     __itt_markA
#  define __itt_mark_ptr __itt_markA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
int ITTAPI __itt_mark(__itt_mark_type mt, const char *parameter);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, int, markA, (__itt_mark_type mt, const char    *parameter))
ITT_STUB(ITTAPI, int, markW, (__itt_mark_type mt, const wchar_t *parameter))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, int, mark,  (__itt_mark_type mt, const char *parameter))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_markA     ITTNOTIFY_DATA(markA)
#define __itt_markA_ptr ITTNOTIFY_NAME(markA)
#define __itt_markW     ITTNOTIFY_DATA(markW)
#define __itt_markW_ptr ITTNOTIFY_NAME(markW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_mark      ITTNOTIFY_DATA(mark)
#define __itt_mark_ptr  ITTNOTIFY_NAME(mark)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_markA(mt, parameter) (int)0
#define __itt_markA_ptr 0
#define __itt_markW(mt, parameter) (int)0
#define __itt_markW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_mark(mt, parameter)  (int)0
#define __itt_mark_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_markA_ptr 0
#define __itt_markW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_mark_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Use this if necessary to create a "discrete" user event type (mark) for process
 * rather then for one thread
 * @see int __itt_mark(__itt_mark_type mt, const char* parameter);
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
int ITTAPI __itt_mark_globalA(__itt_mark_type mt, const char    *parameter);
int ITTAPI __itt_mark_globalW(__itt_mark_type mt, const wchar_t *parameter);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_mark_global     __itt_mark_globalW
#  define __itt_mark_global_ptr __itt_mark_globalW_ptr
#else /* UNICODE  */
#  define __itt_mark_global     __itt_mark_globalA
#  define __itt_mark_global_ptr __itt_mark_globalA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
int ITTAPI __itt_mark_global(__itt_mark_type mt, const char *parameter);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, int, mark_globalA, (__itt_mark_type mt, const char    *parameter))
ITT_STUB(ITTAPI, int, mark_globalW, (__itt_mark_type mt, const wchar_t *parameter))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, int, mark_global,  (__itt_mark_type mt, const char *parameter))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_mark_globalA     ITTNOTIFY_DATA(mark_globalA)
#define __itt_mark_globalA_ptr ITTNOTIFY_NAME(mark_globalA)
#define __itt_mark_globalW     ITTNOTIFY_DATA(mark_globalW)
#define __itt_mark_globalW_ptr ITTNOTIFY_NAME(mark_globalW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_mark_global      ITTNOTIFY_DATA(mark_global)
#define __itt_mark_global_ptr  ITTNOTIFY_NAME(mark_global)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_mark_globalA(mt, parameter) (int)0
#define __itt_mark_globalA_ptr 0
#define __itt_mark_globalW(mt, parameter) (int)0
#define __itt_mark_globalW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_mark_global(mt, parameter)  (int)0
#define __itt_mark_global_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_mark_globalA_ptr 0
#define __itt_mark_globalW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_mark_global_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Creates an "end" point for "continuous" mark with specified name.
 *
 * - Returns zero value in case of success, non-zero value otherwise.
 *   Also returns non-zero value when preceding "begin" point for the
 *   mark with the same name failed to be created or not created.
 * - The mark of "continuous" type is placed to collection results in
 *   case of success. It appears in overtime view(s) as a special tick
 *   sign (different from "discrete" mark) together with line from
 *   corresponding "begin" mark to "end" mark.
 * @note Continuous marks can overlap and be nested inside each other.
 * Discrete mark can be nested inside marked region
 * @param[in] mt - mark, created by __itt_mark_create(const char* name) function
 * @return Returns zero value in case of success, non-zero value otherwise.
 */
int ITTAPI __itt_mark_off(__itt_mark_type mt);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, int, mark_off, (__itt_mark_type mt))
#define __itt_mark_off     ITTNOTIFY_DATA(mark_off)
#define __itt_mark_off_ptr ITTNOTIFY_NAME(mark_off)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_mark_off(mt) (int)0
#define __itt_mark_off_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_mark_off_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Use this if necessary to create an "end" point for mark of process
 * @see int __itt_mark_off(__itt_mark_type mt);
 */
int ITTAPI __itt_mark_global_off(__itt_mark_type mt);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, int, mark_global_off, (__itt_mark_type mt))
#define __itt_mark_global_off     ITTNOTIFY_DATA(mark_global_off)
#define __itt_mark_global_off_ptr ITTNOTIFY_NAME(mark_global_off)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_mark_global_off(mt) (int)0
#define __itt_mark_global_off_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_mark_global_off_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} marks group */

/**
 * @defgroup counters_internal Counters
 * @ingroup internal
 * Counters group
 * @{
 */


/**
 * @defgroup stitch Stack Stitching
 * @ingroup internal
 * Stack Stitching group
 * @{
 */
/**
 * @brief opaque structure for counter identification
 */
typedef struct ___itt_caller *__itt_caller;

/**
 * @brief Create the stitch point e.g. a point in call stack where other stacks should be stitched to.
 * The function returns a unique identifier which is used to match the cut points with corresponding stitch points.
 */
__itt_caller ITTAPI __itt_stack_caller_create(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, __itt_caller, stack_caller_create, (void))
#define __itt_stack_caller_create     ITTNOTIFY_DATA(stack_caller_create)
#define __itt_stack_caller_create_ptr ITTNOTIFY_NAME(stack_caller_create)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_stack_caller_create() (__itt_caller)0
#define __itt_stack_caller_create_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_stack_caller_create_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Destroy the inforamtion about stitch point identified by the pointer previously returned by __itt_stack_caller_create()
 */
void ITTAPI __itt_stack_caller_destroy(__itt_caller id);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, stack_caller_destroy, (__itt_caller id))
#define __itt_stack_caller_destroy     ITTNOTIFY_VOID(stack_caller_destroy)
#define __itt_stack_caller_destroy_ptr ITTNOTIFY_NAME(stack_caller_destroy)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_stack_caller_destroy(id)
#define __itt_stack_caller_destroy_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_stack_caller_destroy_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief Sets the cut point. Stack from each event which occurs after this call will be cut
 * at the same stack level the function was called and stitched to the corresponding stitch point.
 */
void ITTAPI __itt_stack_callee_enter(__itt_caller id);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, stack_callee_enter, (__itt_caller id))
#define __itt_stack_callee_enter     ITTNOTIFY_VOID(stack_callee_enter)
#define __itt_stack_callee_enter_ptr ITTNOTIFY_NAME(stack_callee_enter)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_stack_callee_enter(id)
#define __itt_stack_callee_enter_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_stack_callee_enter_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @brief This function eliminates the cut point which was set by latest __itt_stack_callee_enter().
 */
void ITTAPI __itt_stack_callee_leave(__itt_caller id);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, stack_callee_leave, (__itt_caller id))
#define __itt_stack_callee_leave     ITTNOTIFY_VOID(stack_callee_leave)
#define __itt_stack_callee_leave_ptr ITTNOTIFY_NAME(stack_callee_leave)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_stack_callee_leave(id)
#define __itt_stack_callee_leave_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_stack_callee_leave_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @} stitch group */

/* ***************************************************************************************************************************** */

#include <stdarg.h>

/** @cond exclude_from_documentation */
typedef enum __itt_error_code
{
    __itt_error_success       = 0, /*!< no error */
    __itt_error_no_module     = 1, /*!< module can't be loaded */
    /* %1$s -- library name; win: %2$d -- system error code; unx: %2$s -- system error message. */
    __itt_error_no_symbol     = 2, /*!< symbol not found */
    /* %1$s -- library name, %2$s -- symbol name. */
    __itt_error_unknown_group = 3, /*!< unknown group specified */
    /* %1$s -- env var name, %2$s -- group name. */
    __itt_error_cant_read_env = 4, /*!< GetEnvironmentVariable() failed */
    /* %1$s -- env var name, %2$d -- system error. */
    __itt_error_env_too_long  = 5, /*!< variable value too long */
    /* %1$s -- env var name, %2$d -- actual length of the var, %3$d -- max allowed length. */
    __itt_error_system        = 6  /*!< pthread_mutexattr_init or pthread_mutex_init failed */
    /* %1$s -- function name, %2$d -- errno. */
} __itt_error_code;

typedef void (__itt_error_handler_t)(__itt_error_code code, va_list);
__itt_error_handler_t* __itt_set_error_handler(__itt_error_handler_t*);

const char* ITTAPI __itt_api_version(void);
/** @endcond */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#define __itt_error_handler ITT_JOIN(INTEL_ITTNOTIFY_PREFIX, error_handler)
void __itt_error_handler(__itt_error_code code, va_list args);
extern const int ITTNOTIFY_NAME(err);
#define __itt_err ITTNOTIFY_NAME(err)
ITT_STUB(ITTAPI, const char*, api_version, (void))
#define __itt_api_version     ITTNOTIFY_DATA(api_version)
#define __itt_api_version_ptr ITTNOTIFY_NAME(api_version)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_api_version()   (const char*)0
#define __itt_api_version_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_api_version_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ITTNOTIFY_PRIVATE_ */

#endif /* INTEL_ITTNOTIFY_API_PRIVATE */
