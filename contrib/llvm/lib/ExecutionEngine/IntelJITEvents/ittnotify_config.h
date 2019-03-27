/*===-- ittnotify_config.h - JIT Profiling API internal config-----*- C -*-===*
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===*
 *
 * This file provides Intel(R) Performance Analyzer JIT (Just-In-Time)
 * Profiling API internal config.
 *
 * NOTE: This file comes in a style different from the rest of LLVM
 * source base since  this is a piece of code shared from Intel(R)
 * products.  Please do not reformat / re-style this code to make
 * subsequent merges and contributions from the original source base eaiser.
 *
 *===----------------------------------------------------------------------===*/
#ifndef _ITTNOTIFY_CONFIG_H_
#define _ITTNOTIFY_CONFIG_H_

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

#ifndef ITT_OS
#  if defined WIN32 || defined _WIN32
#    define ITT_OS ITT_OS_WIN
#  elif defined( __APPLE__ ) && defined( __MACH__ )
#    define ITT_OS ITT_OS_MAC
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

#ifndef ITT_PLATFORM
#  if ITT_OS==ITT_OS_WIN
#    define ITT_PLATFORM ITT_PLATFORM_WIN
#  else
#    define ITT_PLATFORM ITT_PLATFORM_POSIX
#  endif /* _WIN32 */
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

#ifndef CDECL
#  if ITT_PLATFORM==ITT_PLATFORM_WIN
#    define CDECL __cdecl
#  else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#    if defined _M_X64 || defined _M_AMD64 || defined __x86_64__
#      define CDECL /* not actual on x86_64 platform */
#    else  /* _M_X64 || _M_AMD64 || __x86_64__ */
#      define CDECL __attribute__ ((cdecl))
#    endif /* _M_X64 || _M_AMD64 || __x86_64__ */
#  endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* CDECL */

#ifndef STDCALL
#  if ITT_PLATFORM==ITT_PLATFORM_WIN
#    define STDCALL __stdcall
#  else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#    if defined _M_X64 || defined _M_AMD64 || defined __x86_64__
#      define STDCALL /* not supported on x86_64 platform */
#    else  /* _M_X64 || _M_AMD64 || __x86_64__ */
#      define STDCALL __attribute__ ((stdcall))
#    endif /* _M_X64 || _M_AMD64 || __x86_64__ */
#  endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* STDCALL */

#define ITTAPI    CDECL
#define LIBITTAPI CDECL

/* TODO: Temporary for compatibility! */
#define ITTAPI_CALL    CDECL
#define LIBITTAPI_CALL CDECL

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
#else  /* __STRICT_ANSI__ */
#define ITT_INLINE           static inline
#endif /* __STRICT_ANSI__ */
#define ITT_INLINE_ATTRIBUTE __attribute__ ((always_inline))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
/** @endcond */

#ifndef ITT_ARCH_IA32
#  define ITT_ARCH_IA32  1
#endif /* ITT_ARCH_IA32 */

#ifndef ITT_ARCH_IA32E
#  define ITT_ARCH_IA32E 2
#endif /* ITT_ARCH_IA32E */

#ifndef ITT_ARCH_IA64
#  define ITT_ARCH_IA64  3
#endif /* ITT_ARCH_IA64 */

#ifndef ITT_ARCH
#  if defined _M_X64 || defined _M_AMD64 || defined __x86_64__
#    define ITT_ARCH ITT_ARCH_IA32E
#  elif defined _M_IA64 || defined __ia64
#    define ITT_ARCH ITT_ARCH_IA64
#  else
#    define ITT_ARCH ITT_ARCH_IA32
#  endif
#endif

#ifdef __cplusplus
#  define ITT_EXTERN_C extern "C"
#else
#  define ITT_EXTERN_C /* nothing */
#endif /* __cplusplus */

#define ITT_TO_STR_AUX(x) #x
#define ITT_TO_STR(x)     ITT_TO_STR_AUX(x)

#define __ITT_BUILD_ASSERT(expr, suffix) do { \
    static char __itt_build_check_##suffix[(expr) ? 1 : -1]; \
    __itt_build_check_##suffix[0] = 0; \
} while(0)
#define _ITT_BUILD_ASSERT(expr, suffix)  __ITT_BUILD_ASSERT((expr), suffix)
#define ITT_BUILD_ASSERT(expr)           _ITT_BUILD_ASSERT((expr), __LINE__)

#define ITT_MAGIC { 0xED, 0xAB, 0xAB, 0xEC, 0x0D, 0xEE, 0xDA, 0x30 }

/* Replace with snapshot date YYYYMMDD for promotion build. */
#define API_VERSION_BUILD    20111111

#ifndef API_VERSION_NUM
#define API_VERSION_NUM 0.0.0
#endif /* API_VERSION_NUM */

#define API_VERSION "ITT-API-Version " ITT_TO_STR(API_VERSION_NUM) \
                                " (" ITT_TO_STR(API_VERSION_BUILD) ")"

/* OS communication functions */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#include <windows.h>
typedef HMODULE           lib_t;
typedef DWORD             TIDT;
typedef CRITICAL_SECTION  mutex_t;
#define MUTEX_INITIALIZER { 0 }
#define strong_alias(name, aliasname) /* empty for Windows */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#include <dlfcn.h>
#if defined(UNICODE) || defined(_UNICODE)
#include <wchar.h>
#endif /* UNICODE */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1 /* need for PTHREAD_MUTEX_RECURSIVE */
#endif /* _GNU_SOURCE */
#include <pthread.h>
typedef void*             lib_t;
typedef pthread_t         TIDT;
typedef pthread_mutex_t   mutex_t;
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define _strong_alias(name, aliasname) \
            extern __typeof (name) aliasname __attribute__ ((alias (#name)));
#define strong_alias(name, aliasname) _strong_alias(name, aliasname)
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

#if ITT_PLATFORM==ITT_PLATFORM_WIN
#define __itt_get_proc(lib, name) GetProcAddress(lib, name)
#define __itt_mutex_init(mutex)   InitializeCriticalSection(mutex)
#define __itt_mutex_lock(mutex)   EnterCriticalSection(mutex)
#define __itt_mutex_unlock(mutex) LeaveCriticalSection(mutex)
#define __itt_load_lib(name)      LoadLibraryA(name)
#define __itt_unload_lib(handle)  FreeLibrary(handle)
#define __itt_system_error()      (int)GetLastError()
#define __itt_fstrcmp(s1, s2)     lstrcmpA(s1, s2)
#define __itt_fstrlen(s)          lstrlenA(s)
#define __itt_fstrcpyn(s1, s2, l) lstrcpynA(s1, s2, l)
#define __itt_fstrdup(s)          _strdup(s)
#define __itt_thread_id()         GetCurrentThreadId()
#define __itt_thread_yield()      SwitchToThread()
#ifndef ITT_SIMPLE_INIT
ITT_INLINE long
__itt_interlocked_increment(volatile long* ptr) ITT_INLINE_ATTRIBUTE;
ITT_INLINE long __itt_interlocked_increment(volatile long* ptr)
{
    return InterlockedIncrement(ptr);
}
#endif /* ITT_SIMPLE_INIT */
#else /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
#define __itt_get_proc(lib, name) dlsym(lib, name)
#define __itt_mutex_init(mutex)   {\
    pthread_mutexattr_t mutex_attr;                                         \
    int error_code = pthread_mutexattr_init(&mutex_attr);                   \
    if (error_code)                                                         \
        __itt_report_error(__itt_error_system, "pthread_mutexattr_init",    \
                           error_code);                                     \
    error_code = pthread_mutexattr_settype(&mutex_attr,                     \
                                           PTHREAD_MUTEX_RECURSIVE);        \
    if (error_code)                                                         \
        __itt_report_error(__itt_error_system, "pthread_mutexattr_settype", \
                           error_code);                                     \
    error_code = pthread_mutex_init(mutex, &mutex_attr);                    \
    if (error_code)                                                         \
        __itt_report_error(__itt_error_system, "pthread_mutex_init",        \
                           error_code);                                     \
    error_code = pthread_mutexattr_destroy(&mutex_attr);                    \
    if (error_code)                                                         \
        __itt_report_error(__itt_error_system, "pthread_mutexattr_destroy", \
                           error_code);                                     \
}
#define __itt_mutex_lock(mutex)   pthread_mutex_lock(mutex)
#define __itt_mutex_unlock(mutex) pthread_mutex_unlock(mutex)
#define __itt_load_lib(name)      dlopen(name, RTLD_LAZY)
#define __itt_unload_lib(handle)  dlclose(handle)
#define __itt_system_error()      errno
#define __itt_fstrcmp(s1, s2)     strcmp(s1, s2)
#define __itt_fstrlen(s)          strlen(s)
#define __itt_fstrcpyn(s1, s2, l) strncpy(s1, s2, l)
#define __itt_fstrdup(s)          strdup(s)
#define __itt_thread_id()         pthread_self()
#define __itt_thread_yield()      sched_yield()
#if ITT_ARCH==ITT_ARCH_IA64
#ifdef __INTEL_COMPILER
#define __TBB_machine_fetchadd4(addr, val) __fetchadd4_acq((void *)addr, val)
#else  /* __INTEL_COMPILER */
/* TODO: Add Support for not Intel compilers for IA64 */
#endif /* __INTEL_COMPILER */
#else /* ITT_ARCH!=ITT_ARCH_IA64 */
ITT_INLINE long
__TBB_machine_fetchadd4(volatile void* ptr, long addend) ITT_INLINE_ATTRIBUTE;
ITT_INLINE long __TBB_machine_fetchadd4(volatile void* ptr, long addend)
{
    long result;
    __asm__ __volatile__("lock\nxadd %0,%1"
                          : "=r"(result),"=m"(*(long*)ptr)
                          : "0"(addend), "m"(*(long*)ptr)
                          : "memory");
    return result;
}
#endif /* ITT_ARCH==ITT_ARCH_IA64 */
#ifndef ITT_SIMPLE_INIT
ITT_INLINE long
__itt_interlocked_increment(volatile long* ptr) ITT_INLINE_ATTRIBUTE;
ITT_INLINE long __itt_interlocked_increment(volatile long* ptr)
{
    return __TBB_machine_fetchadd4(ptr, 1) + 1L;
}
#endif /* ITT_SIMPLE_INIT */
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

typedef enum {
    __itt_collection_normal = 0,
    __itt_collection_paused = 1
} __itt_collection_state;

typedef enum {
    __itt_thread_normal  = 0,
    __itt_thread_ignored = 1
} __itt_thread_state;

#pragma pack(push, 8)

typedef struct ___itt_thread_info
{
    const char* nameA; /*!< Copy of original name in ASCII. */
#if defined(UNICODE) || defined(_UNICODE)
    const wchar_t* nameW; /*!< Copy of original name in UNICODE. */
#else  /* UNICODE || _UNICODE */
    void* nameW;
#endif /* UNICODE || _UNICODE */
    TIDT               tid;
    __itt_thread_state state;   /*!< Thread state (paused or normal) */
    int                extra1;  /*!< Reserved to the runtime */
    void*              extra2;  /*!< Reserved to the runtime */
    struct ___itt_thread_info* next;
} __itt_thread_info;

#include "ittnotify_types.h" /* For __itt_group_id definition */

typedef struct ___itt_api_info_20101001
{
    const char*    name;
    void**         func_ptr;
    void*          init_func;
    __itt_group_id group;
}  __itt_api_info_20101001;

typedef struct ___itt_api_info
{
    const char*    name;
    void**         func_ptr;
    void*          init_func;
    void*          null_func;
    __itt_group_id group;
}  __itt_api_info;

struct ___itt_domain;
struct ___itt_string_handle;

typedef struct ___itt_global
{
    unsigned char          magic[8];
    unsigned long          version_major;
    unsigned long          version_minor;
    unsigned long          version_build;
    volatile long          api_initialized;
    volatile long          mutex_initialized;
    volatile long          atomic_counter;
    mutex_t                mutex;
    lib_t                  lib;
    void*                  error_handler;
    const char**           dll_path_ptr;
    __itt_api_info*        api_list_ptr;
    struct ___itt_global*  next;
    /* Joinable structures below */
    __itt_thread_info*     thread_list;
    struct ___itt_domain*  domain_list;
    struct ___itt_string_handle* string_list;
    __itt_collection_state state;
} __itt_global;

#pragma pack(pop)

#define NEW_THREAD_INFO_W(gptr,h,h_tail,t,s,n) { \
    h = (__itt_thread_info*)malloc(sizeof(__itt_thread_info)); \
    if (h != NULL) { \
        h->tid    = t; \
        h->nameA  = NULL; \
        h->nameW  = n ? _wcsdup(n) : NULL; \
        h->state  = s; \
        h->extra1 = 0;    /* reserved */ \
        h->extra2 = NULL; /* reserved */ \
        h->next   = NULL; \
        if (h_tail == NULL) \
            (gptr)->thread_list = h; \
        else \
            h_tail->next = h; \
    } \
}

#define NEW_THREAD_INFO_A(gptr,h,h_tail,t,s,n) { \
    h = (__itt_thread_info*)malloc(sizeof(__itt_thread_info)); \
    if (h != NULL) { \
        h->tid    = t; \
        h->nameA  = n ? __itt_fstrdup(n) : NULL; \
        h->nameW  = NULL; \
        h->state  = s; \
        h->extra1 = 0;    /* reserved */ \
        h->extra2 = NULL; /* reserved */ \
        h->next   = NULL; \
        if (h_tail == NULL) \
            (gptr)->thread_list = h; \
        else \
            h_tail->next = h; \
    } \
}

#define NEW_DOMAIN_W(gptr,h,h_tail,name) { \
    h = (__itt_domain*)malloc(sizeof(__itt_domain)); \
    if (h != NULL) { \
        h->flags  = 0;    /* domain is disabled by default */ \
        h->nameA  = NULL; \
        h->nameW  = name ? _wcsdup(name) : NULL; \
        h->extra1 = 0;    /* reserved */ \
        h->extra2 = NULL; /* reserved */ \
        h->next   = NULL; \
        if (h_tail == NULL) \
            (gptr)->domain_list = h; \
        else \
            h_tail->next = h; \
    } \
}

#define NEW_DOMAIN_A(gptr,h,h_tail,name) { \
    h = (__itt_domain*)malloc(sizeof(__itt_domain)); \
    if (h != NULL) { \
        h->flags  = 0;    /* domain is disabled by default */ \
        h->nameA  = name ? __itt_fstrdup(name) : NULL; \
        h->nameW  = NULL; \
        h->extra1 = 0;    /* reserved */ \
        h->extra2 = NULL; /* reserved */ \
        h->next   = NULL; \
        if (h_tail == NULL) \
            (gptr)->domain_list = h; \
        else \
            h_tail->next = h; \
    } \
}

#define NEW_STRING_HANDLE_W(gptr,h,h_tail,name) { \
    h = (__itt_string_handle*)malloc(sizeof(__itt_string_handle)); \
    if (h != NULL) { \
        h->strA   = NULL; \
        h->strW   = name ? _wcsdup(name) : NULL; \
        h->extra1 = 0;    /* reserved */ \
        h->extra2 = NULL; /* reserved */ \
        h->next   = NULL; \
        if (h_tail == NULL) \
            (gptr)->string_list = h; \
        else \
            h_tail->next = h; \
    } \
}

#define NEW_STRING_HANDLE_A(gptr,h,h_tail,name) { \
    h = (__itt_string_handle*)malloc(sizeof(__itt_string_handle)); \
    if (h != NULL) { \
        h->strA   = name ? __itt_fstrdup(name) : NULL; \
        h->strW   = NULL; \
        h->extra1 = 0;    /* reserved */ \
        h->extra2 = NULL; /* reserved */ \
        h->next   = NULL; \
        if (h_tail == NULL) \
            (gptr)->string_list = h; \
        else \
            h_tail->next = h; \
    } \
}

#endif /* _ITTNOTIFY_CONFIG_H_ */
