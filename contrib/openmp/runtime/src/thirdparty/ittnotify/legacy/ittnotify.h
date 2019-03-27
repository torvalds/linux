
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LEGACY_ITTNOTIFY_H_
#define _LEGACY_ITTNOTIFY_H_

/**
 * @file
 * @brief Legacy User API functions and types
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

/**
 * @defgroup legacy Legacy API
 * @{
 * @}
 */

/**
 * @defgroup legacy_control Collection Control
 * @ingroup legacy
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
#ifndef _ITTNOTIFY_H_
/** @brief Pause collection */
void ITTAPI __itt_pause(void);
/** @brief Resume collection */
void ITTAPI __itt_resume(void);
/** @brief Detach collection */
void ITTAPI __itt_detach(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, pause,   (void))
ITT_STUBV(ITTAPI, void, resume,  (void))
ITT_STUBV(ITTAPI, void, detach,  (void))
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
#endif /* _ITTNOTIFY_H_ */
/** @} legacy_control group */

/**
 * @defgroup legacy_threads Threads
 * @ingroup legacy
 * Threads group
 * @warning Legacy API
 * @{
 */
/**
 * @deprecated Legacy API
 * @brief Set name to be associated with thread in analysis GUI.
 * @return __itt_err upon failure (name or namelen being null,name and namelen mismatched)
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
int LIBITTAPI __itt_thr_name_setA(const char    *name, int namelen);
int LIBITTAPI __itt_thr_name_setW(const wchar_t *name, int namelen);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_thr_name_set     __itt_thr_name_setW
#  define __itt_thr_name_set_ptr __itt_thr_name_setW_ptr
#else
#  define __itt_thr_name_set     __itt_thr_name_setA
#  define __itt_thr_name_set_ptr __itt_thr_name_setA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
int LIBITTAPI __itt_thr_name_set(const char *name, int namelen);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(LIBITTAPI, int, thr_name_setA, (const char    *name, int namelen))
ITT_STUB(LIBITTAPI, int, thr_name_setW, (const wchar_t *name, int namelen))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(LIBITTAPI, int, thr_name_set,  (const char    *name, int namelen))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_thr_name_setA     ITTNOTIFY_DATA(thr_name_setA)
#define __itt_thr_name_setA_ptr ITTNOTIFY_NAME(thr_name_setA)
#define __itt_thr_name_setW     ITTNOTIFY_DATA(thr_name_setW)
#define __itt_thr_name_setW_ptr ITTNOTIFY_NAME(thr_name_setW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_thr_name_set     ITTNOTIFY_DATA(thr_name_set)
#define __itt_thr_name_set_ptr ITTNOTIFY_NAME(thr_name_set)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_thr_name_setA(name, namelen)
#define __itt_thr_name_setA_ptr 0
#define __itt_thr_name_setW(name, namelen)
#define __itt_thr_name_setW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_thr_name_set(name, namelen)
#define __itt_thr_name_set_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_thr_name_setA_ptr 0
#define __itt_thr_name_setW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_thr_name_set_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief Mark current thread as ignored from this point on, for the duration of its existence.
 */
void LIBITTAPI __itt_thr_ignore(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(LIBITTAPI, void, thr_ignore, (void))
#define __itt_thr_ignore     ITTNOTIFY_VOID(thr_ignore)
#define __itt_thr_ignore_ptr ITTNOTIFY_NAME(thr_ignore)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_thr_ignore()
#define __itt_thr_ignore_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_thr_ignore_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} legacy_threads group */

/**
 * @defgroup legacy_sync Synchronization
 * @ingroup legacy
 * Synchronization group
 * @warning Legacy API
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
 * @deprecated Legacy API
 * @brief Assign a name to a sync object using char or Unicode string
 * @param[in] addr    - pointer to the sync object. You should use a real pointer to your object
 *                      to make sure that the values don't clash with other object addresses
 * @param[in] objtype - null-terminated object type string. If NULL is passed, the object will
 *                      be assumed to be of generic "User Synchronization" type
 * @param[in] objname - null-terminated object name string. If NULL, no name will be assigned
 *                      to the object -- you can use the __itt_sync_rename call later to assign
 *                      the name
 * @param[in] attribute - one of [#__itt_attr_barrier, #__itt_attr_mutex] values which defines the
 *                      exact semantics of how prepare/acquired/releasing calls work.
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
void ITTAPI __itt_sync_set_nameA(void *addr, const char    *objtype, const char    *objname, int attribute);
void ITTAPI __itt_sync_set_nameW(void *addr, const wchar_t *objtype, const wchar_t *objname, int attribute);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_sync_set_name     __itt_sync_set_nameW
#  define __itt_sync_set_name_ptr __itt_sync_set_nameW_ptr
#else /* UNICODE */
#  define __itt_sync_set_name     __itt_sync_set_nameA
#  define __itt_sync_set_name_ptr __itt_sync_set_nameA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
void ITTAPI __itt_sync_set_name(void *addr, const char* objtype, const char* objname, int attribute);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, sync_set_nameA, (void *addr, const char    *objtype, const char    *objname, int attribute))
ITT_STUBV(ITTAPI, void, sync_set_nameW, (void *addr, const wchar_t *objtype, const wchar_t *objname, int attribute))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, sync_set_name,  (void *addr, const char    *objtype, const char    *objname, int attribute))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_sync_set_nameA     ITTNOTIFY_VOID(sync_set_nameA)
#define __itt_sync_set_nameA_ptr ITTNOTIFY_NAME(sync_set_nameA)
#define __itt_sync_set_nameW     ITTNOTIFY_VOID(sync_set_nameW)
#define __itt_sync_set_nameW_ptr ITTNOTIFY_NAME(sync_set_nameW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_sync_set_name     ITTNOTIFY_VOID(sync_set_name)
#define __itt_sync_set_name_ptr ITTNOTIFY_NAME(sync_set_name)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_sync_set_nameA(addr, objtype, objname, attribute)
#define __itt_sync_set_nameA_ptr 0
#define __itt_sync_set_nameW(addr, objtype, objname, attribute)
#define __itt_sync_set_nameW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_sync_set_name(addr, objtype, objname, attribute)
#define __itt_sync_set_name_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_sync_set_nameA_ptr 0
#define __itt_sync_set_nameW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_sync_set_name_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief Assign a name and type to a sync object using char or Unicode string
 * @param[in] addr -      pointer to the sync object. You should use a real pointer to your object
 *                        to make sure that the values don't clash with other object addresses
 * @param[in] objtype -   null-terminated object type string. If NULL is passed, the object will
 *                        be assumed to be of generic "User Synchronization" type
 * @param[in] objname -   null-terminated object name string. If NULL, no name will be assigned
 *                        to the object -- you can use the __itt_sync_rename call later to assign
 *                        the name
 * @param[in] typelen, namelen -   a length of string for appropriate objtype and objname parameter
 * @param[in] attribute - one of [#__itt_attr_barrier, #__itt_attr_mutex] values which defines the
 *                        exact semantics of how prepare/acquired/releasing calls work.
 * @return __itt_err upon failure (name or namelen being null,name and namelen mismatched)
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
int LIBITTAPI __itt_notify_sync_nameA(void *addr, const char    *objtype, int typelen, const char    *objname, int namelen, int attribute);
int LIBITTAPI __itt_notify_sync_nameW(void *addr, const wchar_t *objtype, int typelen, const wchar_t *objname, int namelen, int attribute);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_notify_sync_name __itt_notify_sync_nameW
#else
#  define __itt_notify_sync_name __itt_notify_sync_nameA
#endif
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
int LIBITTAPI __itt_notify_sync_name(void *addr, const char *objtype, int typelen, const char *objname, int namelen, int attribute);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(LIBITTAPI, int, notify_sync_nameA, (void *addr, const char    *objtype, int typelen, const char    *objname, int namelen, int attribute))
ITT_STUB(LIBITTAPI, int, notify_sync_nameW, (void *addr, const wchar_t *objtype, int typelen, const wchar_t *objname, int namelen, int attribute))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(LIBITTAPI, int, notify_sync_name,  (void *addr, const char    *objtype, int typelen, const char    *objname, int namelen, int attribute))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_notify_sync_nameA     ITTNOTIFY_DATA(notify_sync_nameA)
#define __itt_notify_sync_nameA_ptr ITTNOTIFY_NAME(notify_sync_nameA)
#define __itt_notify_sync_nameW     ITTNOTIFY_DATA(notify_sync_nameW)
#define __itt_notify_sync_nameW_ptr ITTNOTIFY_NAME(notify_sync_nameW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_notify_sync_name     ITTNOTIFY_DATA(notify_sync_name)
#define __itt_notify_sync_name_ptr ITTNOTIFY_NAME(notify_sync_name)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_notify_sync_nameA(addr, objtype, typelen, objname, namelen, attribute)
#define __itt_notify_sync_nameA_ptr 0
#define __itt_notify_sync_nameW(addr, objtype, typelen, objname, namelen, attribute)
#define __itt_notify_sync_nameW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_notify_sync_name(addr, objtype, typelen, objname, namelen, attribute)
#define __itt_notify_sync_name_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_notify_sync_nameA_ptr 0
#define __itt_notify_sync_nameW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_notify_sync_name_ptr 0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief Enter spin loop on user-defined sync object
 */
void LIBITTAPI __itt_notify_sync_prepare(void* addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(LIBITTAPI, void, notify_sync_prepare, (void *addr))
#define __itt_notify_sync_prepare     ITTNOTIFY_VOID(notify_sync_prepare)
#define __itt_notify_sync_prepare_ptr ITTNOTIFY_NAME(notify_sync_prepare)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_notify_sync_prepare(addr)
#define __itt_notify_sync_prepare_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_notify_sync_prepare_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief Quit spin loop without acquiring spin object
 */
void LIBITTAPI __itt_notify_sync_cancel(void *addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(LIBITTAPI, void, notify_sync_cancel, (void *addr))
#define __itt_notify_sync_cancel     ITTNOTIFY_VOID(notify_sync_cancel)
#define __itt_notify_sync_cancel_ptr ITTNOTIFY_NAME(notify_sync_cancel)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_notify_sync_cancel(addr)
#define __itt_notify_sync_cancel_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_notify_sync_cancel_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief Successful spin loop completion (sync object acquired)
 */
void LIBITTAPI __itt_notify_sync_acquired(void *addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(LIBITTAPI, void, notify_sync_acquired, (void *addr))
#define __itt_notify_sync_acquired     ITTNOTIFY_VOID(notify_sync_acquired)
#define __itt_notify_sync_acquired_ptr ITTNOTIFY_NAME(notify_sync_acquired)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_notify_sync_acquired(addr)
#define __itt_notify_sync_acquired_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_notify_sync_acquired_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief Start sync object releasing code. Is called before the lock release call.
 */
void LIBITTAPI __itt_notify_sync_releasing(void* addr);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(LIBITTAPI, void, notify_sync_releasing, (void *addr))
#define __itt_notify_sync_releasing     ITTNOTIFY_VOID(notify_sync_releasing)
#define __itt_notify_sync_releasing_ptr ITTNOTIFY_NAME(notify_sync_releasing)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_notify_sync_releasing(addr)
#define __itt_notify_sync_releasing_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_notify_sync_releasing_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} legacy_sync group */

#ifndef _ITTNOTIFY_H_
/**
 * @defgroup legacy_events Events
 * @ingroup legacy
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
ITT_STUB(LIBITTAPI, __itt_event, event_create,  (const char *name, int namelen))
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
/** @} legacy_events group */
#endif /* _ITTNOTIFY_H_ */

/**
 * @defgroup legacy_memory Memory Accesses
 * @ingroup legacy
 */

/**
 * @deprecated Legacy API
 * @brief Inform the tool of memory accesses on reading
 */
void LIBITTAPI __itt_memory_read(void *addr, size_t size);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(LIBITTAPI, void, memory_read, (void *addr, size_t size))
#define __itt_memory_read     ITTNOTIFY_VOID(memory_read)
#define __itt_memory_read_ptr ITTNOTIFY_NAME(memory_read)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_memory_read(addr, size)
#define __itt_memory_read_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_memory_read_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief Inform the tool of memory accesses on writing
 */
void LIBITTAPI __itt_memory_write(void *addr, size_t size);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(LIBITTAPI, void, memory_write, (void *addr, size_t size))
#define __itt_memory_write     ITTNOTIFY_VOID(memory_write)
#define __itt_memory_write_ptr ITTNOTIFY_NAME(memory_write)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_memory_write(addr, size)
#define __itt_memory_write_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_memory_write_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief Inform the tool of memory accesses on updating
 */
void LIBITTAPI __itt_memory_update(void *address, size_t size);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(LIBITTAPI, void, memory_update, (void *addr, size_t size))
#define __itt_memory_update     ITTNOTIFY_VOID(memory_update)
#define __itt_memory_update_ptr ITTNOTIFY_NAME(memory_update)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_memory_update(addr, size)
#define __itt_memory_update_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_memory_update_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} legacy_memory group */

/**
 * @defgroup legacy_state Thread and Object States
 * @ingroup legacy
 */

/** @brief state type */
typedef int __itt_state_t;

/** @cond exclude_from_documentation */
typedef enum __itt_obj_state {
    __itt_obj_state_err = 0,
    __itt_obj_state_clr = 1,
    __itt_obj_state_set = 2,
    __itt_obj_state_use = 3
} __itt_obj_state_t;

typedef enum __itt_thr_state {
    __itt_thr_state_err = 0,
    __itt_thr_state_clr = 1,
    __itt_thr_state_set = 2
} __itt_thr_state_t;

typedef enum __itt_obj_prop {
    __itt_obj_prop_watch    = 1,
    __itt_obj_prop_ignore   = 2,
    __itt_obj_prop_sharable = 3
} __itt_obj_prop_t;

typedef enum __itt_thr_prop {
    __itt_thr_prop_quiet = 1
} __itt_thr_prop_t;
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief managing thread and object states
 */
__itt_state_t LIBITTAPI __itt_state_get(void);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, __itt_state_t, state_get, (void))
#define __itt_state_get     ITTNOTIFY_DATA(state_get)
#define __itt_state_get_ptr ITTNOTIFY_NAME(state_get)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_state_get(void) (__itt_state_t)0
#define __itt_state_get_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_state_get_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief managing thread and object states
 */
__itt_state_t LIBITTAPI __itt_state_set(__itt_state_t s);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, __itt_state_t, state_set, (__itt_state_t s))
#define __itt_state_set     ITTNOTIFY_DATA(state_set)
#define __itt_state_set_ptr ITTNOTIFY_NAME(state_set)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_state_set(s) (__itt_state_t)0
#define __itt_state_set_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_state_set_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief managing thread and object modes
 */
__itt_thr_state_t LIBITTAPI __itt_thr_mode_set(__itt_thr_prop_t p, __itt_thr_state_t s);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, __itt_thr_state_t, thr_mode_set, (__itt_thr_prop_t p, __itt_thr_state_t s))
#define __itt_thr_mode_set     ITTNOTIFY_DATA(thr_mode_set)
#define __itt_thr_mode_set_ptr ITTNOTIFY_NAME(thr_mode_set)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_thr_mode_set(p, s) (__itt_thr_state_t)0
#define __itt_thr_mode_set_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_thr_mode_set_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/**
 * @deprecated Legacy API
 * @brief managing thread and object modes
 */
__itt_obj_state_t LIBITTAPI __itt_obj_mode_set(__itt_obj_prop_t p, __itt_obj_state_t s);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUB(ITTAPI, __itt_obj_state_t, obj_mode_set, (__itt_obj_prop_t p, __itt_obj_state_t s))
#define __itt_obj_mode_set     ITTNOTIFY_DATA(obj_mode_set)
#define __itt_obj_mode_set_ptr ITTNOTIFY_NAME(obj_mode_set)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_obj_mode_set(p, s) (__itt_obj_state_t)0
#define __itt_obj_mode_set_ptr 0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_obj_mode_set_ptr 0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} legacy_state group */

/**
 * @defgroup frames Frames
 * @ingroup legacy
 * Frames group
 * @{
 */
/**
 * @brief opaque structure for frame identification
 */
typedef struct __itt_frame_t *__itt_frame;

/**
 * @brief Create a global frame with given domain
 */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
__itt_frame ITTAPI __itt_frame_createA(const char    *domain);
__itt_frame ITTAPI __itt_frame_createW(const wchar_t *domain);
#if defined(UNICODE) || defined(_UNICODE)
#  define __itt_frame_create     __itt_frame_createW
#  define __itt_frame_create_ptr __itt_frame_createW_ptr
#else /* UNICODE */
#  define __itt_frame_create     __itt_frame_createA
#  define __itt_frame_create_ptr __itt_frame_createA_ptr
#endif /* UNICODE */
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
__itt_frame ITTAPI __itt_frame_create(const char *domain);
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_frame, frame_createA, (const char    *domain))
ITT_STUB(ITTAPI, __itt_frame, frame_createW, (const wchar_t *domain))
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_frame, frame_create,  (const char *domain))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_frame_createA     ITTNOTIFY_DATA(frame_createA)
#define __itt_frame_createA_ptr ITTNOTIFY_NAME(frame_createA)
#define __itt_frame_createW     ITTNOTIFY_DATA(frame_createW)
#define __itt_frame_createW_ptr ITTNOTIFY_NAME(frame_createW)
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_frame_create     ITTNOTIFY_DATA(frame_create)
#define __itt_frame_create_ptr ITTNOTIFY_NAME(frame_create)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#else  /* INTEL_NO_ITTNOTIFY_API */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_frame_createA(domain)
#define __itt_frame_createA_ptr 0
#define __itt_frame_createW(domain)
#define __itt_frame_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_frame_create(domain)
#define __itt_frame_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_frame_createA_ptr 0
#define __itt_frame_createW_ptr 0
#else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#define __itt_frame_create_ptr  0
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */

/** @brief Record an frame begin occurrence. */
void ITTAPI __itt_frame_begin(__itt_frame frame);
/** @brief Record an frame end occurrence. */
void ITTAPI __itt_frame_end  (__itt_frame frame);

/** @cond exclude_from_documentation */
#ifndef INTEL_NO_MACRO_BODY
#ifndef INTEL_NO_ITTNOTIFY_API
ITT_STUBV(ITTAPI, void, frame_begin, (__itt_frame frame))
ITT_STUBV(ITTAPI, void, frame_end,   (__itt_frame frame))
#define __itt_frame_begin     ITTNOTIFY_VOID(frame_begin)
#define __itt_frame_begin_ptr ITTNOTIFY_NAME(frame_begin)
#define __itt_frame_end       ITTNOTIFY_VOID(frame_end)
#define __itt_frame_end_ptr   ITTNOTIFY_NAME(frame_end)
#else  /* INTEL_NO_ITTNOTIFY_API */
#define __itt_frame_begin(frame)
#define __itt_frame_begin_ptr 0
#define __itt_frame_end(frame)
#define __itt_frame_end_ptr   0
#endif /* INTEL_NO_ITTNOTIFY_API */
#else  /* INTEL_NO_MACRO_BODY */
#define __itt_frame_begin_ptr 0
#define __itt_frame_end_ptr   0
#endif /* INTEL_NO_MACRO_BODY */
/** @endcond */
/** @} frames group */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LEGACY_ITTNOTIFY_H_ */
