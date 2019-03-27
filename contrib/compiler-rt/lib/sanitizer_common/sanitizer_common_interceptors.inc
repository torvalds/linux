//===-- sanitizer_common_interceptors.inc -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Common function interceptors for tools like AddressSanitizer,
// ThreadSanitizer, MemorySanitizer, etc.
//
// This file should be included into the tool's interceptor file,
// which has to define its own macros:
//   COMMON_INTERCEPTOR_ENTER
//   COMMON_INTERCEPTOR_ENTER_NOIGNORE
//   COMMON_INTERCEPTOR_READ_RANGE
//   COMMON_INTERCEPTOR_WRITE_RANGE
//   COMMON_INTERCEPTOR_INITIALIZE_RANGE
//   COMMON_INTERCEPTOR_DIR_ACQUIRE
//   COMMON_INTERCEPTOR_FD_ACQUIRE
//   COMMON_INTERCEPTOR_FD_RELEASE
//   COMMON_INTERCEPTOR_FD_ACCESS
//   COMMON_INTERCEPTOR_SET_THREAD_NAME
//   COMMON_INTERCEPTOR_ON_DLOPEN
//   COMMON_INTERCEPTOR_ON_EXIT
//   COMMON_INTERCEPTOR_MUTEX_PRE_LOCK
//   COMMON_INTERCEPTOR_MUTEX_POST_LOCK
//   COMMON_INTERCEPTOR_MUTEX_UNLOCK
//   COMMON_INTERCEPTOR_MUTEX_REPAIR
//   COMMON_INTERCEPTOR_SET_PTHREAD_NAME
//   COMMON_INTERCEPTOR_HANDLE_RECVMSG
//   COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED
//   COMMON_INTERCEPTOR_MEMSET_IMPL
//   COMMON_INTERCEPTOR_MEMMOVE_IMPL
//   COMMON_INTERCEPTOR_MEMCPY_IMPL
//   COMMON_INTERCEPTOR_MMAP_IMPL
//   COMMON_INTERCEPTOR_COPY_STRING
//   COMMON_INTERCEPTOR_STRNDUP_IMPL
//===----------------------------------------------------------------------===//

#include "interception/interception.h"
#include "sanitizer_addrhashmap.h"
#include "sanitizer_errno.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_platform_interceptors.h"
#include "sanitizer_symbolizer.h"
#include "sanitizer_tls_get_addr.h"

#include <stdarg.h>

#if SANITIZER_INTERCEPTOR_HOOKS
#define CALL_WEAK_INTERCEPTOR_HOOK(f, ...) f(__VA_ARGS__);
#define DECLARE_WEAK_INTERCEPTOR_HOOK(f, ...) \
  SANITIZER_INTERFACE_WEAK_DEF(void, f, __VA_ARGS__) {}
#else
#define DECLARE_WEAK_INTERCEPTOR_HOOK(f, ...)
#define CALL_WEAK_INTERCEPTOR_HOOK(f, ...)

#endif  // SANITIZER_INTERCEPTOR_HOOKS

#if SANITIZER_WINDOWS && !defined(va_copy)
#define va_copy(dst, src) ((dst) = (src))
#endif // _WIN32

#if SANITIZER_FREEBSD
#define pthread_setname_np pthread_set_name_np
#define inet_aton __inet_aton
#define inet_pton __inet_pton
#define iconv __bsd_iconv
#endif

#if SANITIZER_NETBSD
#define clock_getres __clock_getres50
#define clock_gettime __clock_gettime50
#define clock_settime __clock_settime50
#define ctime __ctime50
#define ctime_r __ctime_r50
#define devname __devname50
#define fgetpos __fgetpos50
#define fsetpos __fsetpos50
#define fts_children __fts_children60
#define fts_close __fts_close60
#define fts_open __fts_open60
#define fts_read __fts_read60
#define fts_set __fts_set60
#define getitimer __getitimer50
#define getmntinfo __getmntinfo13
#define getpwent __getpwent50
#define getpwnam __getpwnam50
#define getpwnam_r __getpwnam_r50
#define getpwuid __getpwuid50
#define getpwuid_r __getpwuid_r50
#define getutent __getutent50
#define getutxent __getutxent50
#define getutxid __getutxid50
#define getutxline __getutxline50
#define pututxline __pututxline50
#define glob __glob30
#define gmtime __gmtime50
#define gmtime_r __gmtime_r50
#define localtime __locatime50
#define localtime_r __localtime_r50
#define mktime __mktime50
#define lstat __lstat50
#define opendir __opendir30
#define readdir __readdir30
#define readdir_r __readdir_r30
#define scandir __scandir30
#define setitimer __setitimer50
#define setlocale __setlocale50
#define shmctl __shmctl50
#define sigemptyset __sigemptyset14
#define sigfillset __sigfillset14
#define sigpending __sigpending14
#define sigprocmask __sigprocmask14
#define sigtimedwait __sigtimedwait50
#define stat __stat50
#define time __time50
#define times __times13
#define unvis __unvis50
#define wait3 __wait350
#define wait4 __wait450
extern const unsigned short *_ctype_tab_;
extern const short *_toupper_tab_;
extern const short *_tolower_tab_;
#endif

// Platform-specific options.
#if SANITIZER_MAC
namespace __sanitizer {
bool PlatformHasDifferentMemcpyAndMemmove();
}
#define PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE \
  (__sanitizer::PlatformHasDifferentMemcpyAndMemmove())
#elif SANITIZER_WINDOWS64
#define PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE false
#else
#define PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE true
#endif  // SANITIZER_MAC

#ifndef COMMON_INTERCEPTOR_INITIALIZE_RANGE
#define COMMON_INTERCEPTOR_INITIALIZE_RANGE(p, size) {}
#endif

#ifndef COMMON_INTERCEPTOR_UNPOISON_PARAM
#define COMMON_INTERCEPTOR_UNPOISON_PARAM(count) {}
#endif

#ifndef COMMON_INTERCEPTOR_FD_ACCESS
#define COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd) {}
#endif

#ifndef COMMON_INTERCEPTOR_MUTEX_PRE_LOCK
#define COMMON_INTERCEPTOR_MUTEX_PRE_LOCK(ctx, m) {}
#endif

#ifndef COMMON_INTERCEPTOR_MUTEX_POST_LOCK
#define COMMON_INTERCEPTOR_MUTEX_POST_LOCK(ctx, m) {}
#endif

#ifndef COMMON_INTERCEPTOR_MUTEX_UNLOCK
#define COMMON_INTERCEPTOR_MUTEX_UNLOCK(ctx, m) {}
#endif

#ifndef COMMON_INTERCEPTOR_MUTEX_REPAIR
#define COMMON_INTERCEPTOR_MUTEX_REPAIR(ctx, m) {}
#endif

#ifndef COMMON_INTERCEPTOR_MUTEX_INVALID
#define COMMON_INTERCEPTOR_MUTEX_INVALID(ctx, m) {}
#endif

#ifndef COMMON_INTERCEPTOR_HANDLE_RECVMSG
#define COMMON_INTERCEPTOR_HANDLE_RECVMSG(ctx, msg) ((void)(msg))
#endif

#ifndef COMMON_INTERCEPTOR_FILE_OPEN
#define COMMON_INTERCEPTOR_FILE_OPEN(ctx, file, path) {}
#endif

#ifndef COMMON_INTERCEPTOR_FILE_CLOSE
#define COMMON_INTERCEPTOR_FILE_CLOSE(ctx, file) {}
#endif

#ifndef COMMON_INTERCEPTOR_LIBRARY_LOADED
#define COMMON_INTERCEPTOR_LIBRARY_LOADED(filename, handle) {}
#endif

#ifndef COMMON_INTERCEPTOR_LIBRARY_UNLOADED
#define COMMON_INTERCEPTOR_LIBRARY_UNLOADED() {}
#endif

#ifndef COMMON_INTERCEPTOR_ENTER_NOIGNORE
#define COMMON_INTERCEPTOR_ENTER_NOIGNORE(ctx, ...) \
  COMMON_INTERCEPTOR_ENTER(ctx, __VA_ARGS__)
#endif

#ifndef COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED
#define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED (0)
#endif

#define COMMON_INTERCEPTOR_READ_STRING(ctx, s, n)                   \
    COMMON_INTERCEPTOR_READ_RANGE((ctx), (s),                       \
      common_flags()->strict_string_checks ? (REAL(strlen)(s)) + 1 : (n) )

#ifndef COMMON_INTERCEPTOR_ON_DLOPEN
#define COMMON_INTERCEPTOR_ON_DLOPEN(filename, flag) \
  CheckNoDeepBind(filename, flag);
#endif

#ifndef COMMON_INTERCEPTOR_GET_TLS_RANGE
#define COMMON_INTERCEPTOR_GET_TLS_RANGE(begin, end) *begin = *end = 0;
#endif

#ifndef COMMON_INTERCEPTOR_ACQUIRE
#define COMMON_INTERCEPTOR_ACQUIRE(ctx, u) {}
#endif

#ifndef COMMON_INTERCEPTOR_RELEASE
#define COMMON_INTERCEPTOR_RELEASE(ctx, u) {}
#endif

#ifndef COMMON_INTERCEPTOR_USER_CALLBACK_START
#define COMMON_INTERCEPTOR_USER_CALLBACK_START() {}
#endif

#ifndef COMMON_INTERCEPTOR_USER_CALLBACK_END
#define COMMON_INTERCEPTOR_USER_CALLBACK_END() {}
#endif

#ifdef SANITIZER_NLDBL_VERSION
#define COMMON_INTERCEPT_FUNCTION_LDBL(fn)                          \
    COMMON_INTERCEPT_FUNCTION_VER(fn, SANITIZER_NLDBL_VERSION)
#else
#define COMMON_INTERCEPT_FUNCTION_LDBL(fn)                          \
    COMMON_INTERCEPT_FUNCTION(fn)
#endif

#ifndef COMMON_INTERCEPTOR_MEMSET_IMPL
#define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, dst, v, size) \
  {                                                       \
    if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)        \
      return internal_memset(dst, v, size);               \
    COMMON_INTERCEPTOR_ENTER(ctx, memset, dst, v, size);  \
    if (common_flags()->intercept_intrin)                 \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, size);     \
    return REAL(memset)(dst, v, size);                    \
  }
#endif

#ifndef COMMON_INTERCEPTOR_MEMMOVE_IMPL
#define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size) \
  {                                                          \
    if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)           \
      return internal_memmove(dst, src, size);               \
    COMMON_INTERCEPTOR_ENTER(ctx, memmove, dst, src, size);  \
    if (common_flags()->intercept_intrin) {                  \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, size);        \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, src, size);         \
    }                                                        \
    return REAL(memmove)(dst, src, size);                    \
  }
#endif

#ifndef COMMON_INTERCEPTOR_MEMCPY_IMPL
#define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, dst, src, size) \
  {                                                         \
    if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED) {        \
      return internal_memmove(dst, src, size);              \
    }                                                       \
    COMMON_INTERCEPTOR_ENTER(ctx, memcpy, dst, src, size);  \
    if (common_flags()->intercept_intrin) {                 \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, size);       \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, src, size);        \
    }                                                       \
    return REAL(memcpy)(dst, src, size);                    \
  }
#endif

#ifndef COMMON_INTERCEPTOR_MMAP_IMPL
#define COMMON_INTERCEPTOR_MMAP_IMPL(ctx, mmap, addr, sz, prot, flags, fd, \
                                     off)                                  \
  { return REAL(mmap)(addr, sz, prot, flags, fd, off); }
#endif

#ifndef COMMON_INTERCEPTOR_COPY_STRING
#define COMMON_INTERCEPTOR_COPY_STRING(ctx, to, from, size) {}
#endif

#ifndef COMMON_INTERCEPTOR_STRNDUP_IMPL
#define COMMON_INTERCEPTOR_STRNDUP_IMPL(ctx, s, size)                         \
  COMMON_INTERCEPTOR_ENTER(ctx, strndup, s, size);                            \
  uptr copy_length = internal_strnlen(s, size);                               \
  char *new_mem = (char *)WRAP(malloc)(copy_length + 1);                      \
  if (common_flags()->intercept_strndup) {                                    \
    COMMON_INTERCEPTOR_READ_STRING(ctx, s, Min(size, copy_length + 1));       \
  }                                                                           \
  COMMON_INTERCEPTOR_COPY_STRING(ctx, new_mem, s, copy_length);               \
  internal_memcpy(new_mem, s, copy_length);                                   \
  new_mem[copy_length] = '\0';                                                \
  return new_mem;
#endif

struct FileMetadata {
  // For open_memstream().
  char **addr;
  SIZE_T *size;
};

struct CommonInterceptorMetadata {
  enum {
    CIMT_INVALID = 0,
    CIMT_FILE
  } type;
  union {
    FileMetadata file;
  };
};

typedef AddrHashMap<CommonInterceptorMetadata, 31051> MetadataHashMap;

static MetadataHashMap *interceptor_metadata_map;

#if SI_POSIX
UNUSED static void SetInterceptorMetadata(__sanitizer_FILE *addr,
                                          const FileMetadata &file) {
  MetadataHashMap::Handle h(interceptor_metadata_map, (uptr)addr);
  CHECK(h.created());
  h->type = CommonInterceptorMetadata::CIMT_FILE;
  h->file = file;
}

UNUSED static const FileMetadata *GetInterceptorMetadata(
    __sanitizer_FILE *addr) {
  MetadataHashMap::Handle h(interceptor_metadata_map, (uptr)addr,
                            /* remove */ false,
                            /* create */ false);
  if (addr && h.exists()) {
    CHECK(!h.created());
    CHECK(h->type == CommonInterceptorMetadata::CIMT_FILE);
    return &h->file;
  } else {
    return 0;
  }
}

UNUSED static void DeleteInterceptorMetadata(void *addr) {
  MetadataHashMap::Handle h(interceptor_metadata_map, (uptr)addr, true);
  CHECK(h.exists());
}
#endif  // SI_POSIX

#if SANITIZER_INTERCEPT_STRLEN
INTERCEPTOR(SIZE_T, strlen, const char *s) {
  // Sometimes strlen is called prior to InitializeCommonInterceptors,
  // in which case the REAL(strlen) typically used in
  // COMMON_INTERCEPTOR_ENTER will fail.  We use internal_strlen here
  // to handle that.
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_strlen(s);
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strlen, s);
  SIZE_T result = REAL(strlen)(s);
  if (common_flags()->intercept_strlen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, s, result + 1);
  return result;
}
#define INIT_STRLEN COMMON_INTERCEPT_FUNCTION(strlen)
#else
#define INIT_STRLEN
#endif

#if SANITIZER_INTERCEPT_STRNLEN
INTERCEPTOR(SIZE_T, strnlen, const char *s, SIZE_T maxlen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strnlen, s, maxlen);
  SIZE_T length = REAL(strnlen)(s, maxlen);
  if (common_flags()->intercept_strlen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, s, Min(length + 1, maxlen));
  return length;
}
#define INIT_STRNLEN COMMON_INTERCEPT_FUNCTION(strnlen)
#else
#define INIT_STRNLEN
#endif

#if SANITIZER_INTERCEPT_STRNDUP
INTERCEPTOR(char*, strndup, const char *s, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_STRNDUP_IMPL(ctx, s, size);
}
#define INIT_STRNDUP COMMON_INTERCEPT_FUNCTION(strndup)
#else
#define INIT_STRNDUP
#endif // SANITIZER_INTERCEPT_STRNDUP

#if SANITIZER_INTERCEPT___STRNDUP
INTERCEPTOR(char*, __strndup, const char *s, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_STRNDUP_IMPL(ctx, s, size);
}
#define INIT___STRNDUP COMMON_INTERCEPT_FUNCTION(__strndup)
#else
#define INIT___STRNDUP
#endif // SANITIZER_INTERCEPT___STRNDUP

#if SANITIZER_INTERCEPT_TEXTDOMAIN
INTERCEPTOR(char*, textdomain, const char *domainname) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, textdomain, domainname);
  if (domainname) COMMON_INTERCEPTOR_READ_STRING(ctx, domainname, 0);
  char *domain = REAL(textdomain)(domainname);
  if (domain) {
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(domain, REAL(strlen)(domain) + 1);
  }
  return domain;
}
#define INIT_TEXTDOMAIN COMMON_INTERCEPT_FUNCTION(textdomain)
#else
#define INIT_TEXTDOMAIN
#endif

#if SANITIZER_INTERCEPT_STRCMP
static inline int CharCmpX(unsigned char c1, unsigned char c2) {
  return (c1 == c2) ? 0 : (c1 < c2) ? -1 : 1;
}

DECLARE_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strcmp, uptr called_pc,
                              const char *s1, const char *s2, int result)

INTERCEPTOR(int, strcmp, const char *s1, const char *s2) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strcmp, s1, s2);
  unsigned char c1, c2;
  uptr i;
  for (i = 0;; i++) {
    c1 = (unsigned char)s1[i];
    c2 = (unsigned char)s2[i];
    if (c1 != c2 || c1 == '\0') break;
  }
  COMMON_INTERCEPTOR_READ_STRING(ctx, s1, i + 1);
  COMMON_INTERCEPTOR_READ_STRING(ctx, s2, i + 1);
  int result = CharCmpX(c1, c2);
  CALL_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strcmp, GET_CALLER_PC(), s1,
                             s2, result);
  return result;
}

DECLARE_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strncmp, uptr called_pc,
                              const char *s1, const char *s2, uptr n,
                              int result)

INTERCEPTOR(int, strncmp, const char *s1, const char *s2, uptr size) {
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_strncmp(s1, s2, size);
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strncmp, s1, s2, size);
  unsigned char c1 = 0, c2 = 0;
  uptr i;
  for (i = 0; i < size; i++) {
    c1 = (unsigned char)s1[i];
    c2 = (unsigned char)s2[i];
    if (c1 != c2 || c1 == '\0') break;
  }
  uptr i1 = i;
  uptr i2 = i;
  if (common_flags()->strict_string_checks) {
    for (; i1 < size && s1[i1]; i1++) {}
    for (; i2 < size && s2[i2]; i2++) {}
  }
  COMMON_INTERCEPTOR_READ_RANGE((ctx), (s1), Min(i1 + 1, size));
  COMMON_INTERCEPTOR_READ_RANGE((ctx), (s2), Min(i2 + 1, size));
  int result = CharCmpX(c1, c2);
  CALL_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strncmp, GET_CALLER_PC(), s1,
                             s2, size, result);
  return result;
}

#define INIT_STRCMP COMMON_INTERCEPT_FUNCTION(strcmp)
#define INIT_STRNCMP COMMON_INTERCEPT_FUNCTION(strncmp)
#else
#define INIT_STRCMP
#define INIT_STRNCMP
#endif

#if SANITIZER_INTERCEPT_STRCASECMP
static inline int CharCaseCmp(unsigned char c1, unsigned char c2) {
  int c1_low = ToLower(c1);
  int c2_low = ToLower(c2);
  return c1_low - c2_low;
}

DECLARE_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strcasecmp, uptr called_pc,
                              const char *s1, const char *s2, int result)

INTERCEPTOR(int, strcasecmp, const char *s1, const char *s2) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strcasecmp, s1, s2);
  unsigned char c1 = 0, c2 = 0;
  uptr i;
  for (i = 0;; i++) {
    c1 = (unsigned char)s1[i];
    c2 = (unsigned char)s2[i];
    if (CharCaseCmp(c1, c2) != 0 || c1 == '\0') break;
  }
  COMMON_INTERCEPTOR_READ_STRING(ctx, s1, i + 1);
  COMMON_INTERCEPTOR_READ_STRING(ctx, s2, i + 1);
  int result = CharCaseCmp(c1, c2);
  CALL_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strcasecmp, GET_CALLER_PC(),
                             s1, s2, result);
  return result;
}

DECLARE_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strncasecmp, uptr called_pc,
                              const char *s1, const char *s2, uptr size,
                              int result)

INTERCEPTOR(int, strncasecmp, const char *s1, const char *s2, SIZE_T size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strncasecmp, s1, s2, size);
  unsigned char c1 = 0, c2 = 0;
  uptr i;
  for (i = 0; i < size; i++) {
    c1 = (unsigned char)s1[i];
    c2 = (unsigned char)s2[i];
    if (CharCaseCmp(c1, c2) != 0 || c1 == '\0') break;
  }
  uptr i1 = i;
  uptr i2 = i;
  if (common_flags()->strict_string_checks) {
    for (; i1 < size && s1[i1]; i1++) {}
    for (; i2 < size && s2[i2]; i2++) {}
  }
  COMMON_INTERCEPTOR_READ_RANGE((ctx), (s1), Min(i1 + 1, size));
  COMMON_INTERCEPTOR_READ_RANGE((ctx), (s2), Min(i2 + 1, size));
  int result = CharCaseCmp(c1, c2);
  CALL_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strncasecmp, GET_CALLER_PC(),
                             s1, s2, size, result);
  return result;
}

#define INIT_STRCASECMP COMMON_INTERCEPT_FUNCTION(strcasecmp)
#define INIT_STRNCASECMP COMMON_INTERCEPT_FUNCTION(strncasecmp)
#else
#define INIT_STRCASECMP
#define INIT_STRNCASECMP
#endif

#if SANITIZER_INTERCEPT_STRSTR || SANITIZER_INTERCEPT_STRCASESTR
static inline void StrstrCheck(void *ctx, char *r, const char *s1,
                               const char *s2) {
    uptr len1 = REAL(strlen)(s1);
    uptr len2 = REAL(strlen)(s2);
    COMMON_INTERCEPTOR_READ_STRING(ctx, s1, r ? r - s1 + len2 : len1 + 1);
    COMMON_INTERCEPTOR_READ_RANGE(ctx, s2, len2 + 1);
}
#endif

#if SANITIZER_INTERCEPT_STRSTR

DECLARE_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strstr, uptr called_pc,
                              const char *s1, const char *s2, char *result)

INTERCEPTOR(char*, strstr, const char *s1, const char *s2) {
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_strstr(s1, s2);
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strstr, s1, s2);
  char *r = REAL(strstr)(s1, s2);
  if (common_flags()->intercept_strstr)
    StrstrCheck(ctx, r, s1, s2);
  CALL_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strstr, GET_CALLER_PC(), s1,
                             s2, r);
  return r;
}

#define INIT_STRSTR COMMON_INTERCEPT_FUNCTION(strstr);
#else
#define INIT_STRSTR
#endif

#if SANITIZER_INTERCEPT_STRCASESTR

DECLARE_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strcasestr, uptr called_pc,
                              const char *s1, const char *s2, char *result)

INTERCEPTOR(char*, strcasestr, const char *s1, const char *s2) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strcasestr, s1, s2);
  char *r = REAL(strcasestr)(s1, s2);
  if (common_flags()->intercept_strstr)
    StrstrCheck(ctx, r, s1, s2);
  CALL_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_strcasestr, GET_CALLER_PC(),
                             s1, s2, r);
  return r;
}

#define INIT_STRCASESTR COMMON_INTERCEPT_FUNCTION(strcasestr);
#else
#define INIT_STRCASESTR
#endif

#if SANITIZER_INTERCEPT_STRTOK

INTERCEPTOR(char*, strtok, char *str, const char *delimiters) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strtok, str, delimiters);
  if (!common_flags()->intercept_strtok) {
    return REAL(strtok)(str, delimiters);
  }
  if (common_flags()->strict_string_checks) {
    // If strict_string_checks is enabled, we check the whole first argument
    // string on the first call (strtok saves this string in a static buffer
    // for subsequent calls). We do not need to check strtok's result.
    // As the delimiters can change, we check them every call.
    if (str != nullptr) {
      COMMON_INTERCEPTOR_READ_RANGE(ctx, str, REAL(strlen)(str) + 1);
    }
    COMMON_INTERCEPTOR_READ_RANGE(ctx, delimiters,
                                  REAL(strlen)(delimiters) + 1);
    return REAL(strtok)(str, delimiters);
  } else {
    // However, when strict_string_checks is disabled we cannot check the
    // whole string on the first call. Instead, we check the result string
    // which is guaranteed to be a NULL-terminated substring of the first
    // argument. We also conservatively check one character of str and the
    // delimiters.
    if (str != nullptr) {
      COMMON_INTERCEPTOR_READ_STRING(ctx, str, 1);
    }
    COMMON_INTERCEPTOR_READ_RANGE(ctx, delimiters, 1);
    char *result = REAL(strtok)(str, delimiters);
    if (result != nullptr) {
      COMMON_INTERCEPTOR_READ_RANGE(ctx, result, REAL(strlen)(result) + 1);
    } else if (str != nullptr) {
      // No delimiter were found, it's safe to assume that the entire str was
      // scanned.
      COMMON_INTERCEPTOR_READ_RANGE(ctx, str, REAL(strlen)(str) + 1);
    }
    return result;
  }
}

#define INIT_STRTOK COMMON_INTERCEPT_FUNCTION(strtok)
#else
#define INIT_STRTOK
#endif

#if SANITIZER_INTERCEPT_MEMMEM
DECLARE_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_memmem, uptr called_pc,
                              const void *s1, SIZE_T len1, const void *s2,
                              SIZE_T len2, void *result)

INTERCEPTOR(void*, memmem, const void *s1, SIZE_T len1, const void *s2,
            SIZE_T len2) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, memmem, s1, len1, s2, len2);
  void *r = REAL(memmem)(s1, len1, s2, len2);
  if (common_flags()->intercept_memmem) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, s1, len1);
    COMMON_INTERCEPTOR_READ_RANGE(ctx, s2, len2);
  }
  CALL_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_memmem, GET_CALLER_PC(),
                             s1, len1, s2, len2, r);
  return r;
}

#define INIT_MEMMEM COMMON_INTERCEPT_FUNCTION(memmem);
#else
#define INIT_MEMMEM
#endif  // SANITIZER_INTERCEPT_MEMMEM

#if SANITIZER_INTERCEPT_STRCHR
INTERCEPTOR(char*, strchr, const char *s, int c) {
  void *ctx;
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_strchr(s, c);
  COMMON_INTERCEPTOR_ENTER(ctx, strchr, s, c);
  char *result = REAL(strchr)(s, c);
  if (common_flags()->intercept_strchr) {
    // Keep strlen as macro argument, as macro may ignore it.
    COMMON_INTERCEPTOR_READ_STRING(ctx, s,
      (result ? result - s : REAL(strlen)(s)) + 1);
  }
  return result;
}
#define INIT_STRCHR COMMON_INTERCEPT_FUNCTION(strchr)
#else
#define INIT_STRCHR
#endif

#if SANITIZER_INTERCEPT_STRCHRNUL
INTERCEPTOR(char*, strchrnul, const char *s, int c) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strchrnul, s, c);
  char *result = REAL(strchrnul)(s, c);
  uptr len = result - s + 1;
  if (common_flags()->intercept_strchr)
    COMMON_INTERCEPTOR_READ_STRING(ctx, s, len);
  return result;
}
#define INIT_STRCHRNUL COMMON_INTERCEPT_FUNCTION(strchrnul)
#else
#define INIT_STRCHRNUL
#endif

#if SANITIZER_INTERCEPT_STRRCHR
INTERCEPTOR(char*, strrchr, const char *s, int c) {
  void *ctx;
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_strrchr(s, c);
  COMMON_INTERCEPTOR_ENTER(ctx, strrchr, s, c);
  if (common_flags()->intercept_strchr)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, s, REAL(strlen)(s) + 1);
  return REAL(strrchr)(s, c);
}
#define INIT_STRRCHR COMMON_INTERCEPT_FUNCTION(strrchr)
#else
#define INIT_STRRCHR
#endif

#if SANITIZER_INTERCEPT_STRSPN
INTERCEPTOR(SIZE_T, strspn, const char *s1, const char *s2) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strspn, s1, s2);
  SIZE_T r = REAL(strspn)(s1, s2);
  if (common_flags()->intercept_strspn) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, s2, REAL(strlen)(s2) + 1);
    COMMON_INTERCEPTOR_READ_STRING(ctx, s1, r + 1);
  }
  return r;
}

INTERCEPTOR(SIZE_T, strcspn, const char *s1, const char *s2) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strcspn, s1, s2);
  SIZE_T r = REAL(strcspn)(s1, s2);
  if (common_flags()->intercept_strspn) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, s2, REAL(strlen)(s2) + 1);
    COMMON_INTERCEPTOR_READ_STRING(ctx, s1, r + 1);
  }
  return r;
}

#define INIT_STRSPN \
  COMMON_INTERCEPT_FUNCTION(strspn); \
  COMMON_INTERCEPT_FUNCTION(strcspn);
#else
#define INIT_STRSPN
#endif

#if SANITIZER_INTERCEPT_STRPBRK
INTERCEPTOR(char *, strpbrk, const char *s1, const char *s2) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strpbrk, s1, s2);
  char *r = REAL(strpbrk)(s1, s2);
  if (common_flags()->intercept_strpbrk) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, s2, REAL(strlen)(s2) + 1);
    COMMON_INTERCEPTOR_READ_STRING(ctx, s1,
        r ? r - s1 + 1 : REAL(strlen)(s1) + 1);
  }
  return r;
}

#define INIT_STRPBRK COMMON_INTERCEPT_FUNCTION(strpbrk);
#else
#define INIT_STRPBRK
#endif

#if SANITIZER_INTERCEPT_MEMSET
INTERCEPTOR(void *, memset, void *dst, int v, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, dst, v, size);
}

#define INIT_MEMSET COMMON_INTERCEPT_FUNCTION(memset)
#else
#define INIT_MEMSET
#endif

#if SANITIZER_INTERCEPT_MEMMOVE
INTERCEPTOR(void *, memmove, void *dst, const void *src, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
}

#define INIT_MEMMOVE COMMON_INTERCEPT_FUNCTION(memmove)
#else
#define INIT_MEMMOVE
#endif

#if SANITIZER_INTERCEPT_MEMCPY
INTERCEPTOR(void *, memcpy, void *dst, const void *src, uptr size) {
  // On OS X, calling internal_memcpy here will cause memory corruptions,
  // because memcpy and memmove are actually aliases of the same
  // implementation.  We need to use internal_memmove here.
  // N.B.: If we switch this to internal_ we'll have to use internal_memmove
  // due to memcpy being an alias of memmove on OS X.
  void *ctx;
  if (PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE) {
    COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, dst, src, size);
  } else {
    COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
  }
}

#define INIT_MEMCPY                                  \
  do {                                               \
    if (PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE) { \
      COMMON_INTERCEPT_FUNCTION(memcpy);             \
    } else {                                         \
      ASSIGN_REAL(memcpy, memmove);                  \
    }                                                \
    CHECK(REAL(memcpy));                             \
  } while (false)

#else
#define INIT_MEMCPY
#endif

#if SANITIZER_INTERCEPT_MEMCMP

DECLARE_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_memcmp, uptr called_pc,
                              const void *s1, const void *s2, uptr n,
                              int result)

INTERCEPTOR(int, memcmp, const void *a1, const void *a2, uptr size) {
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_memcmp(a1, a2, size);
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, memcmp, a1, a2, size);
  if (common_flags()->intercept_memcmp) {
    if (common_flags()->strict_memcmp) {
      // Check the entire regions even if the first bytes of the buffers are
      // different.
      COMMON_INTERCEPTOR_READ_RANGE(ctx, a1, size);
      COMMON_INTERCEPTOR_READ_RANGE(ctx, a2, size);
      // Fallthrough to REAL(memcmp) below.
    } else {
      unsigned char c1 = 0, c2 = 0;
      const unsigned char *s1 = (const unsigned char*)a1;
      const unsigned char *s2 = (const unsigned char*)a2;
      uptr i;
      for (i = 0; i < size; i++) {
        c1 = s1[i];
        c2 = s2[i];
        if (c1 != c2) break;
      }
      COMMON_INTERCEPTOR_READ_RANGE(ctx, s1, Min(i + 1, size));
      COMMON_INTERCEPTOR_READ_RANGE(ctx, s2, Min(i + 1, size));
      int r = CharCmpX(c1, c2);
      CALL_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_memcmp, GET_CALLER_PC(),
                                 a1, a2, size, r);
      return r;
    }
  }
  int result = REAL(memcmp(a1, a2, size));
  CALL_WEAK_INTERCEPTOR_HOOK(__sanitizer_weak_hook_memcmp, GET_CALLER_PC(), a1,
                             a2, size, result);
  return result;
}

#define INIT_MEMCMP COMMON_INTERCEPT_FUNCTION(memcmp)
#else
#define INIT_MEMCMP
#endif

#if SANITIZER_INTERCEPT_MEMCHR
INTERCEPTOR(void*, memchr, const void *s, int c, SIZE_T n) {
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_memchr(s, c, n);
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, memchr, s, c, n);
#if SANITIZER_WINDOWS
  void *res;
  if (REAL(memchr)) {
    res = REAL(memchr)(s, c, n);
  } else {
    res = internal_memchr(s, c, n);
  }
#else
  void *res = REAL(memchr)(s, c, n);
#endif
  uptr len = res ? (char *)res - (const char *)s + 1 : n;
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s, len);
  return res;
}

#define INIT_MEMCHR COMMON_INTERCEPT_FUNCTION(memchr)
#else
#define INIT_MEMCHR
#endif

#if SANITIZER_INTERCEPT_MEMRCHR
INTERCEPTOR(void*, memrchr, const void *s, int c, SIZE_T n) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, memrchr, s, c, n);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s, n);
  return REAL(memrchr)(s, c, n);
}

#define INIT_MEMRCHR COMMON_INTERCEPT_FUNCTION(memrchr)
#else
#define INIT_MEMRCHR
#endif

#if SANITIZER_INTERCEPT_FREXP
INTERCEPTOR(double, frexp, double x, int *exp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, frexp, x, exp);
  // Assuming frexp() always writes to |exp|.
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, exp, sizeof(*exp));
  double res = REAL(frexp)(x, exp);
  return res;
}

#define INIT_FREXP COMMON_INTERCEPT_FUNCTION(frexp);
#else
#define INIT_FREXP
#endif  // SANITIZER_INTERCEPT_FREXP

#if SANITIZER_INTERCEPT_FREXPF_FREXPL
INTERCEPTOR(float, frexpf, float x, int *exp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, frexpf, x, exp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  float res = REAL(frexpf)(x, exp);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, exp, sizeof(*exp));
  return res;
}

INTERCEPTOR(long double, frexpl, long double x, int *exp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, frexpl, x, exp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  long double res = REAL(frexpl)(x, exp);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, exp, sizeof(*exp));
  return res;
}

#define INIT_FREXPF_FREXPL           \
  COMMON_INTERCEPT_FUNCTION(frexpf); \
  COMMON_INTERCEPT_FUNCTION_LDBL(frexpl)
#else
#define INIT_FREXPF_FREXPL
#endif  // SANITIZER_INTERCEPT_FREXPF_FREXPL

#if SI_POSIX
static void write_iovec(void *ctx, struct __sanitizer_iovec *iovec,
                        SIZE_T iovlen, SIZE_T maxlen) {
  for (SIZE_T i = 0; i < iovlen && maxlen; ++i) {
    SSIZE_T sz = Min(iovec[i].iov_len, maxlen);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, iovec[i].iov_base, sz);
    maxlen -= sz;
  }
}

static void read_iovec(void *ctx, struct __sanitizer_iovec *iovec,
                       SIZE_T iovlen, SIZE_T maxlen) {
  COMMON_INTERCEPTOR_READ_RANGE(ctx, iovec, sizeof(*iovec) * iovlen);
  for (SIZE_T i = 0; i < iovlen && maxlen; ++i) {
    SSIZE_T sz = Min(iovec[i].iov_len, maxlen);
    COMMON_INTERCEPTOR_READ_RANGE(ctx, iovec[i].iov_base, sz);
    maxlen -= sz;
  }
}
#endif

#if SANITIZER_INTERCEPT_READ
INTERCEPTOR(SSIZE_T, read, int fd, void *ptr, SIZE_T count) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, read, fd, ptr, count);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(read)(fd, ptr, count);
  if (res > 0) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, res);
  if (res >= 0 && fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}
#define INIT_READ COMMON_INTERCEPT_FUNCTION(read)
#else
#define INIT_READ
#endif

#if SANITIZER_INTERCEPT_FREAD
INTERCEPTOR(SIZE_T, fread, void *ptr, SIZE_T size, SIZE_T nmemb, void *file) {
  // libc file streams can call user-supplied functions, see fopencookie.
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fread, ptr, size, nmemb, file);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(fread)(ptr, size, nmemb, file);
  if (res > 0) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, res * size);
  return res;
}
#define INIT_FREAD COMMON_INTERCEPT_FUNCTION(fread)
#else
#define INIT_FREAD
#endif

#if SANITIZER_INTERCEPT_PREAD
INTERCEPTOR(SSIZE_T, pread, int fd, void *ptr, SIZE_T count, OFF_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pread, fd, ptr, count, offset);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(pread)(fd, ptr, count, offset);
  if (res > 0) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, res);
  if (res >= 0 && fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}
#define INIT_PREAD COMMON_INTERCEPT_FUNCTION(pread)
#else
#define INIT_PREAD
#endif

#if SANITIZER_INTERCEPT_PREAD64
INTERCEPTOR(SSIZE_T, pread64, int fd, void *ptr, SIZE_T count, OFF64_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pread64, fd, ptr, count, offset);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(pread64)(fd, ptr, count, offset);
  if (res > 0) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, res);
  if (res >= 0 && fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}
#define INIT_PREAD64 COMMON_INTERCEPT_FUNCTION(pread64)
#else
#define INIT_PREAD64
#endif

#if SANITIZER_INTERCEPT_READV
INTERCEPTOR_WITH_SUFFIX(SSIZE_T, readv, int fd, __sanitizer_iovec *iov,
                        int iovcnt) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, readv, fd, iov, iovcnt);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  SSIZE_T res = REAL(readv)(fd, iov, iovcnt);
  if (res > 0) write_iovec(ctx, iov, iovcnt, res);
  if (res >= 0 && fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}
#define INIT_READV COMMON_INTERCEPT_FUNCTION(readv)
#else
#define INIT_READV
#endif

#if SANITIZER_INTERCEPT_PREADV
INTERCEPTOR(SSIZE_T, preadv, int fd, __sanitizer_iovec *iov, int iovcnt,
            OFF_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, preadv, fd, iov, iovcnt, offset);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  SSIZE_T res = REAL(preadv)(fd, iov, iovcnt, offset);
  if (res > 0) write_iovec(ctx, iov, iovcnt, res);
  if (res >= 0 && fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}
#define INIT_PREADV COMMON_INTERCEPT_FUNCTION(preadv)
#else
#define INIT_PREADV
#endif

#if SANITIZER_INTERCEPT_PREADV64
INTERCEPTOR(SSIZE_T, preadv64, int fd, __sanitizer_iovec *iov, int iovcnt,
            OFF64_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, preadv64, fd, iov, iovcnt, offset);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  SSIZE_T res = REAL(preadv64)(fd, iov, iovcnt, offset);
  if (res > 0) write_iovec(ctx, iov, iovcnt, res);
  if (res >= 0 && fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}
#define INIT_PREADV64 COMMON_INTERCEPT_FUNCTION(preadv64)
#else
#define INIT_PREADV64
#endif

#if SANITIZER_INTERCEPT_WRITE
INTERCEPTOR(SSIZE_T, write, int fd, void *ptr, SIZE_T count) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, write, fd, ptr, count);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  if (fd >= 0) COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  SSIZE_T res = REAL(write)(fd, ptr, count);
  // FIXME: this check should be _before_ the call to REAL(write), not after
  if (res > 0) COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, res);
  return res;
}
#define INIT_WRITE COMMON_INTERCEPT_FUNCTION(write)
#else
#define INIT_WRITE
#endif

#if SANITIZER_INTERCEPT_FWRITE
INTERCEPTOR(SIZE_T, fwrite, const void *p, uptr size, uptr nmemb, void *file) {
  // libc file streams can call user-supplied functions, see fopencookie.
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fwrite, p, size, nmemb, file);
  SIZE_T res = REAL(fwrite)(p, size, nmemb, file);
  if (res > 0) COMMON_INTERCEPTOR_READ_RANGE(ctx, p, res * size);
  return res;
}
#define INIT_FWRITE COMMON_INTERCEPT_FUNCTION(fwrite)
#else
#define INIT_FWRITE
#endif

#if SANITIZER_INTERCEPT_PWRITE
INTERCEPTOR(SSIZE_T, pwrite, int fd, void *ptr, SIZE_T count, OFF_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pwrite, fd, ptr, count, offset);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  if (fd >= 0) COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  SSIZE_T res = REAL(pwrite)(fd, ptr, count, offset);
  if (res > 0) COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, res);
  return res;
}
#define INIT_PWRITE COMMON_INTERCEPT_FUNCTION(pwrite)
#else
#define INIT_PWRITE
#endif

#if SANITIZER_INTERCEPT_PWRITE64
INTERCEPTOR(SSIZE_T, pwrite64, int fd, void *ptr, OFF64_T count,
            OFF64_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pwrite64, fd, ptr, count, offset);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  if (fd >= 0) COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  SSIZE_T res = REAL(pwrite64)(fd, ptr, count, offset);
  if (res > 0) COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, res);
  return res;
}
#define INIT_PWRITE64 COMMON_INTERCEPT_FUNCTION(pwrite64)
#else
#define INIT_PWRITE64
#endif

#if SANITIZER_INTERCEPT_WRITEV
INTERCEPTOR_WITH_SUFFIX(SSIZE_T, writev, int fd, __sanitizer_iovec *iov,
                        int iovcnt) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, writev, fd, iov, iovcnt);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  if (fd >= 0) COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  SSIZE_T res = REAL(writev)(fd, iov, iovcnt);
  if (res > 0) read_iovec(ctx, iov, iovcnt, res);
  return res;
}
#define INIT_WRITEV COMMON_INTERCEPT_FUNCTION(writev)
#else
#define INIT_WRITEV
#endif

#if SANITIZER_INTERCEPT_PWRITEV
INTERCEPTOR(SSIZE_T, pwritev, int fd, __sanitizer_iovec *iov, int iovcnt,
            OFF_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pwritev, fd, iov, iovcnt, offset);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  if (fd >= 0) COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  SSIZE_T res = REAL(pwritev)(fd, iov, iovcnt, offset);
  if (res > 0) read_iovec(ctx, iov, iovcnt, res);
  return res;
}
#define INIT_PWRITEV COMMON_INTERCEPT_FUNCTION(pwritev)
#else
#define INIT_PWRITEV
#endif

#if SANITIZER_INTERCEPT_PWRITEV64
INTERCEPTOR(SSIZE_T, pwritev64, int fd, __sanitizer_iovec *iov, int iovcnt,
            OFF64_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pwritev64, fd, iov, iovcnt, offset);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  if (fd >= 0) COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  SSIZE_T res = REAL(pwritev64)(fd, iov, iovcnt, offset);
  if (res > 0) read_iovec(ctx, iov, iovcnt, res);
  return res;
}
#define INIT_PWRITEV64 COMMON_INTERCEPT_FUNCTION(pwritev64)
#else
#define INIT_PWRITEV64
#endif

#if SANITIZER_INTERCEPT_FGETS
INTERCEPTOR(char *, fgets, char *s, SIZE_T size, void *file) {
  // libc file streams can call user-supplied functions, see fopencookie.
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fgets, s, size, file);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(fgets)(s, size, file);
  if (res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, s, REAL(strlen)(s) + 1);
  return res;
}
#define INIT_FGETS COMMON_INTERCEPT_FUNCTION(fgets)
#else
#define INIT_FGETS
#endif

#if SANITIZER_INTERCEPT_FPUTS
INTERCEPTOR_WITH_SUFFIX(int, fputs, char *s, void *file) {
  // libc file streams can call user-supplied functions, see fopencookie.
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fputs, s, file);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s, REAL(strlen)(s) + 1);
  return REAL(fputs)(s, file);
}
#define INIT_FPUTS COMMON_INTERCEPT_FUNCTION(fputs)
#else
#define INIT_FPUTS
#endif

#if SANITIZER_INTERCEPT_PUTS
INTERCEPTOR(int, puts, char *s) {
  // libc file streams can call user-supplied functions, see fopencookie.
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, puts, s);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s, REAL(strlen)(s) + 1);
  return REAL(puts)(s);
}
#define INIT_PUTS COMMON_INTERCEPT_FUNCTION(puts)
#else
#define INIT_PUTS
#endif

#if SANITIZER_INTERCEPT_PRCTL
INTERCEPTOR(int, prctl, int option, unsigned long arg2,
            unsigned long arg3,                        // NOLINT
            unsigned long arg4, unsigned long arg5) {  // NOLINT
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, prctl, option, arg2, arg3, arg4, arg5);
  static const int PR_SET_NAME = 15;
  int res = REAL(prctl(option, arg2, arg3, arg4, arg5));
  if (option == PR_SET_NAME) {
    char buff[16];
    internal_strncpy(buff, (char *)arg2, 15);
    buff[15] = 0;
    COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, buff);
  }
  return res;
}
#define INIT_PRCTL COMMON_INTERCEPT_FUNCTION(prctl)
#else
#define INIT_PRCTL
#endif  // SANITIZER_INTERCEPT_PRCTL

#if SANITIZER_INTERCEPT_TIME
INTERCEPTOR(unsigned long, time, unsigned long *t) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, time, t);
  unsigned long local_t;
  unsigned long res = REAL(time)(&local_t);
  if (t && res != (unsigned long)-1) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, t, sizeof(*t));
    *t = local_t;
  }
  return res;
}
#define INIT_TIME COMMON_INTERCEPT_FUNCTION(time);
#else
#define INIT_TIME
#endif  // SANITIZER_INTERCEPT_TIME

#if SANITIZER_INTERCEPT_LOCALTIME_AND_FRIENDS
static void unpoison_tm(void *ctx, __sanitizer_tm *tm) {
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, tm, sizeof(*tm));
#if !SANITIZER_SOLARIS
  if (tm->tm_zone) {
    // Can not use COMMON_INTERCEPTOR_WRITE_RANGE here, because tm->tm_zone
    // can point to shared memory and tsan would report a data race.
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(tm->tm_zone,
                                        REAL(strlen(tm->tm_zone)) + 1);
  }
#endif
}
INTERCEPTOR(__sanitizer_tm *, localtime, unsigned long *timep) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, localtime, timep);
  __sanitizer_tm *res = REAL(localtime)(timep);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    unpoison_tm(ctx, res);
  }
  return res;
}
INTERCEPTOR(__sanitizer_tm *, localtime_r, unsigned long *timep, void *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, localtime_r, timep, result);
  __sanitizer_tm *res = REAL(localtime_r)(timep, result);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    unpoison_tm(ctx, res);
  }
  return res;
}
INTERCEPTOR(__sanitizer_tm *, gmtime, unsigned long *timep) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gmtime, timep);
  __sanitizer_tm *res = REAL(gmtime)(timep);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    unpoison_tm(ctx, res);
  }
  return res;
}
INTERCEPTOR(__sanitizer_tm *, gmtime_r, unsigned long *timep, void *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gmtime_r, timep, result);
  __sanitizer_tm *res = REAL(gmtime_r)(timep, result);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    unpoison_tm(ctx, res);
  }
  return res;
}
INTERCEPTOR(char *, ctime, unsigned long *timep) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ctime, timep);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(ctime)(timep);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  }
  return res;
}
INTERCEPTOR(char *, ctime_r, unsigned long *timep, char *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ctime_r, timep, result);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(ctime_r)(timep, result);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  }
  return res;
}
INTERCEPTOR(char *, asctime, __sanitizer_tm *tm) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, asctime, tm);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(asctime)(tm);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, tm, sizeof(*tm));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  }
  return res;
}
INTERCEPTOR(char *, asctime_r, __sanitizer_tm *tm, char *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, asctime_r, tm, result);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(asctime_r)(tm, result);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, tm, sizeof(*tm));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  }
  return res;
}
INTERCEPTOR(long, mktime, __sanitizer_tm *tm) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, mktime, tm);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, &tm->tm_sec, sizeof(tm->tm_sec));
  COMMON_INTERCEPTOR_READ_RANGE(ctx, &tm->tm_min, sizeof(tm->tm_min));
  COMMON_INTERCEPTOR_READ_RANGE(ctx, &tm->tm_hour, sizeof(tm->tm_hour));
  COMMON_INTERCEPTOR_READ_RANGE(ctx, &tm->tm_mday, sizeof(tm->tm_mday));
  COMMON_INTERCEPTOR_READ_RANGE(ctx, &tm->tm_mon, sizeof(tm->tm_mon));
  COMMON_INTERCEPTOR_READ_RANGE(ctx, &tm->tm_year, sizeof(tm->tm_year));
  COMMON_INTERCEPTOR_READ_RANGE(ctx, &tm->tm_isdst, sizeof(tm->tm_isdst));
  long res = REAL(mktime)(tm);
  if (res != -1) unpoison_tm(ctx, tm);
  return res;
}
#define INIT_LOCALTIME_AND_FRIENDS        \
  COMMON_INTERCEPT_FUNCTION(localtime);   \
  COMMON_INTERCEPT_FUNCTION(localtime_r); \
  COMMON_INTERCEPT_FUNCTION(gmtime);      \
  COMMON_INTERCEPT_FUNCTION(gmtime_r);    \
  COMMON_INTERCEPT_FUNCTION(ctime);       \
  COMMON_INTERCEPT_FUNCTION(ctime_r);     \
  COMMON_INTERCEPT_FUNCTION(asctime);     \
  COMMON_INTERCEPT_FUNCTION(asctime_r);   \
  COMMON_INTERCEPT_FUNCTION(mktime);
#else
#define INIT_LOCALTIME_AND_FRIENDS
#endif  // SANITIZER_INTERCEPT_LOCALTIME_AND_FRIENDS

#if SANITIZER_INTERCEPT_STRPTIME
INTERCEPTOR(char *, strptime, char *s, char *format, __sanitizer_tm *tm) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strptime, s, format, tm);
  if (format)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, format, REAL(strlen)(format) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(strptime)(s, format, tm);
  COMMON_INTERCEPTOR_READ_STRING(ctx, s, res ? res - s : 0);
  if (res && tm) {
    // Do not call unpoison_tm here, because strptime does not, in fact,
    // initialize the entire struct tm. For example, tm_zone pointer is left
    // uninitialized.
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, tm, sizeof(*tm));
  }
  return res;
}
#define INIT_STRPTIME COMMON_INTERCEPT_FUNCTION(strptime);
#else
#define INIT_STRPTIME
#endif

#if SANITIZER_INTERCEPT_SCANF || SANITIZER_INTERCEPT_PRINTF
#include "sanitizer_common_interceptors_format.inc"

#define FORMAT_INTERCEPTOR_IMPL(name, vname, ...)                              \
  {                                                                            \
    void *ctx;                                                                 \
    va_list ap;                                                                \
    va_start(ap, format);                                                      \
    COMMON_INTERCEPTOR_ENTER(ctx, vname, __VA_ARGS__, ap);                     \
    int res = WRAP(vname)(__VA_ARGS__, ap);                                    \
    va_end(ap);                                                                \
    return res;                                                                \
  }

#endif

#if SANITIZER_INTERCEPT_SCANF

#define VSCANF_INTERCEPTOR_IMPL(vname, allowGnuMalloc, ...)                    \
  {                                                                            \
    void *ctx;                                                                 \
    COMMON_INTERCEPTOR_ENTER(ctx, vname, __VA_ARGS__);                         \
    va_list aq;                                                                \
    va_copy(aq, ap);                                                           \
    int res = REAL(vname)(__VA_ARGS__);                                        \
    if (res > 0)                                                               \
      scanf_common(ctx, res, allowGnuMalloc, format, aq);                      \
    va_end(aq);                                                                \
    return res;                                                                \
  }

INTERCEPTOR(int, vscanf, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(vscanf, true, format, ap)

INTERCEPTOR(int, vsscanf, const char *str, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(vsscanf, true, str, format, ap)

INTERCEPTOR(int, vfscanf, void *stream, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(vfscanf, true, stream, format, ap)

#if SANITIZER_INTERCEPT_ISOC99_SCANF
INTERCEPTOR(int, __isoc99_vscanf, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(__isoc99_vscanf, false, format, ap)

INTERCEPTOR(int, __isoc99_vsscanf, const char *str, const char *format,
            va_list ap)
VSCANF_INTERCEPTOR_IMPL(__isoc99_vsscanf, false, str, format, ap)

INTERCEPTOR(int, __isoc99_vfscanf, void *stream, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(__isoc99_vfscanf, false, stream, format, ap)
#endif  // SANITIZER_INTERCEPT_ISOC99_SCANF

INTERCEPTOR(int, scanf, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(scanf, vscanf, format)

INTERCEPTOR(int, fscanf, void *stream, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(fscanf, vfscanf, stream, format)

INTERCEPTOR(int, sscanf, const char *str, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(sscanf, vsscanf, str, format)

#if SANITIZER_INTERCEPT_ISOC99_SCANF
INTERCEPTOR(int, __isoc99_scanf, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(__isoc99_scanf, __isoc99_vscanf, format)

INTERCEPTOR(int, __isoc99_fscanf, void *stream, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(__isoc99_fscanf, __isoc99_vfscanf, stream, format)

INTERCEPTOR(int, __isoc99_sscanf, const char *str, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(__isoc99_sscanf, __isoc99_vsscanf, str, format)
#endif

#endif

#if SANITIZER_INTERCEPT_SCANF
#define INIT_SCANF                    \
  COMMON_INTERCEPT_FUNCTION_LDBL(scanf);   \
  COMMON_INTERCEPT_FUNCTION_LDBL(sscanf);  \
  COMMON_INTERCEPT_FUNCTION_LDBL(fscanf);  \
  COMMON_INTERCEPT_FUNCTION_LDBL(vscanf);  \
  COMMON_INTERCEPT_FUNCTION_LDBL(vsscanf); \
  COMMON_INTERCEPT_FUNCTION_LDBL(vfscanf);
#else
#define INIT_SCANF
#endif

#if SANITIZER_INTERCEPT_ISOC99_SCANF
#define INIT_ISOC99_SCANF                      \
  COMMON_INTERCEPT_FUNCTION(__isoc99_scanf);   \
  COMMON_INTERCEPT_FUNCTION(__isoc99_sscanf);  \
  COMMON_INTERCEPT_FUNCTION(__isoc99_fscanf);  \
  COMMON_INTERCEPT_FUNCTION(__isoc99_vscanf);  \
  COMMON_INTERCEPT_FUNCTION(__isoc99_vsscanf); \
  COMMON_INTERCEPT_FUNCTION(__isoc99_vfscanf);
#else
#define INIT_ISOC99_SCANF
#endif

#if SANITIZER_INTERCEPT_PRINTF

#define VPRINTF_INTERCEPTOR_ENTER(vname, ...)                                  \
  void *ctx;                                                                   \
  COMMON_INTERCEPTOR_ENTER(ctx, vname, __VA_ARGS__);                           \
  va_list aq;                                                                  \
  va_copy(aq, ap);

#define VPRINTF_INTERCEPTOR_RETURN()                                           \
  va_end(aq);

#define VPRINTF_INTERCEPTOR_IMPL(vname, ...)                                   \
  {                                                                            \
    VPRINTF_INTERCEPTOR_ENTER(vname, __VA_ARGS__);                             \
    if (common_flags()->check_printf)                                          \
      printf_common(ctx, format, aq);                                          \
    int res = REAL(vname)(__VA_ARGS__);                                        \
    VPRINTF_INTERCEPTOR_RETURN();                                              \
    return res;                                                                \
  }

// FIXME: under ASan the REAL() call below may write to freed memory and
// corrupt its metadata. See
// https://github.com/google/sanitizers/issues/321.
#define VSPRINTF_INTERCEPTOR_IMPL(vname, str, ...)                             \
  {                                                                            \
    VPRINTF_INTERCEPTOR_ENTER(vname, str, __VA_ARGS__)                         \
    if (common_flags()->check_printf) {                                        \
      printf_common(ctx, format, aq);                                          \
    }                                                                          \
    int res = REAL(vname)(str, __VA_ARGS__);                                   \
    if (res >= 0) {                                                            \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, str, res + 1);                       \
    }                                                                          \
    VPRINTF_INTERCEPTOR_RETURN();                                              \
    return res;                                                                \
  }

// FIXME: under ASan the REAL() call below may write to freed memory and
// corrupt its metadata. See
// https://github.com/google/sanitizers/issues/321.
#define VSNPRINTF_INTERCEPTOR_IMPL(vname, str, size, ...)                      \
  {                                                                            \
    VPRINTF_INTERCEPTOR_ENTER(vname, str, size, __VA_ARGS__)                   \
    if (common_flags()->check_printf) {                                        \
      printf_common(ctx, format, aq);                                          \
    }                                                                          \
    int res = REAL(vname)(str, size, __VA_ARGS__);                             \
    if (res >= 0) {                                                            \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, str, Min(size, (SIZE_T)(res + 1)));  \
    }                                                                          \
    VPRINTF_INTERCEPTOR_RETURN();                                              \
    return res;                                                                \
  }

// FIXME: under ASan the REAL() call below may write to freed memory and
// corrupt its metadata. See
// https://github.com/google/sanitizers/issues/321.
#define VASPRINTF_INTERCEPTOR_IMPL(vname, strp, ...)                           \
  {                                                                            \
    VPRINTF_INTERCEPTOR_ENTER(vname, strp, __VA_ARGS__)                        \
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, strp, sizeof(char *));                 \
    if (common_flags()->check_printf) {                                        \
      printf_common(ctx, format, aq);                                          \
    }                                                                          \
    int res = REAL(vname)(strp, __VA_ARGS__);                                  \
    if (res >= 0) {                                                            \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *strp, res + 1);                     \
    }                                                                          \
    VPRINTF_INTERCEPTOR_RETURN();                                              \
    return res;                                                                \
  }

INTERCEPTOR(int, vprintf, const char *format, va_list ap)
VPRINTF_INTERCEPTOR_IMPL(vprintf, format, ap)

INTERCEPTOR(int, vfprintf, __sanitizer_FILE *stream, const char *format,
            va_list ap)
VPRINTF_INTERCEPTOR_IMPL(vfprintf, stream, format, ap)

INTERCEPTOR(int, vsnprintf, char *str, SIZE_T size, const char *format,
            va_list ap)
VSNPRINTF_INTERCEPTOR_IMPL(vsnprintf, str, size, format, ap)

#if SANITIZER_INTERCEPT___PRINTF_CHK
INTERCEPTOR(int, __vsnprintf_chk, char *str, SIZE_T size, int flag,
            SIZE_T size_to, const char *format, va_list ap)
VSNPRINTF_INTERCEPTOR_IMPL(vsnprintf, str, size, format, ap)
#endif

#if SANITIZER_INTERCEPT_PRINTF_L
INTERCEPTOR(int, vsnprintf_l, char *str, SIZE_T size, void *loc,
            const char *format, va_list ap)
VSNPRINTF_INTERCEPTOR_IMPL(vsnprintf_l, str, size, loc, format, ap)

INTERCEPTOR(int, snprintf_l, char *str, SIZE_T size, void *loc,
            const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(snprintf_l, vsnprintf_l, str, size, loc, format)
#endif  // SANITIZER_INTERCEPT_PRINTF_L

INTERCEPTOR(int, vsprintf, char *str, const char *format, va_list ap)
VSPRINTF_INTERCEPTOR_IMPL(vsprintf, str, format, ap)

#if SANITIZER_INTERCEPT___PRINTF_CHK
INTERCEPTOR(int, __vsprintf_chk, char *str, int flag, SIZE_T size_to,
            const char *format, va_list ap)
VSPRINTF_INTERCEPTOR_IMPL(vsprintf, str, format, ap)
#endif

INTERCEPTOR(int, vasprintf, char **strp, const char *format, va_list ap)
VASPRINTF_INTERCEPTOR_IMPL(vasprintf, strp, format, ap)

#if SANITIZER_INTERCEPT_ISOC99_PRINTF
INTERCEPTOR(int, __isoc99_vprintf, const char *format, va_list ap)
VPRINTF_INTERCEPTOR_IMPL(__isoc99_vprintf, format, ap)

INTERCEPTOR(int, __isoc99_vfprintf, __sanitizer_FILE *stream,
            const char *format, va_list ap)
VPRINTF_INTERCEPTOR_IMPL(__isoc99_vfprintf, stream, format, ap)

INTERCEPTOR(int, __isoc99_vsnprintf, char *str, SIZE_T size, const char *format,
            va_list ap)
VSNPRINTF_INTERCEPTOR_IMPL(__isoc99_vsnprintf, str, size, format, ap)

INTERCEPTOR(int, __isoc99_vsprintf, char *str, const char *format,
            va_list ap)
VSPRINTF_INTERCEPTOR_IMPL(__isoc99_vsprintf, str, format,
                          ap)

#endif  // SANITIZER_INTERCEPT_ISOC99_PRINTF

INTERCEPTOR(int, printf, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(printf, vprintf, format)

INTERCEPTOR(int, fprintf, __sanitizer_FILE *stream, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(fprintf, vfprintf, stream, format)

#if SANITIZER_INTERCEPT___PRINTF_CHK
INTERCEPTOR(int, __fprintf_chk, __sanitizer_FILE *stream, SIZE_T size,
            const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(__fprintf_chk, vfprintf, stream, format)
#endif

INTERCEPTOR(int, sprintf, char *str, const char *format, ...) // NOLINT
FORMAT_INTERCEPTOR_IMPL(sprintf, vsprintf, str, format) // NOLINT

#if SANITIZER_INTERCEPT___PRINTF_CHK
INTERCEPTOR(int, __sprintf_chk, char *str, int flag, SIZE_T size_to,
            const char *format, ...) // NOLINT
FORMAT_INTERCEPTOR_IMPL(__sprintf_chk, vsprintf, str, format) // NOLINT
#endif

INTERCEPTOR(int, snprintf, char *str, SIZE_T size, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(snprintf, vsnprintf, str, size, format)

#if SANITIZER_INTERCEPT___PRINTF_CHK
INTERCEPTOR(int, __snprintf_chk, char *str, SIZE_T size, int flag,
            SIZE_T size_to, const char *format, ...) // NOLINT
FORMAT_INTERCEPTOR_IMPL(__snprintf_chk, vsnprintf, str, size, format) // NOLINT
#endif

INTERCEPTOR(int, asprintf, char **strp, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(asprintf, vasprintf, strp, format)

#if SANITIZER_INTERCEPT_ISOC99_PRINTF
INTERCEPTOR(int, __isoc99_printf, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(__isoc99_printf, __isoc99_vprintf, format)

INTERCEPTOR(int, __isoc99_fprintf, __sanitizer_FILE *stream, const char *format,
            ...)
FORMAT_INTERCEPTOR_IMPL(__isoc99_fprintf, __isoc99_vfprintf, stream, format)

INTERCEPTOR(int, __isoc99_sprintf, char *str, const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(__isoc99_sprintf, __isoc99_vsprintf, str, format)

INTERCEPTOR(int, __isoc99_snprintf, char *str, SIZE_T size,
            const char *format, ...)
FORMAT_INTERCEPTOR_IMPL(__isoc99_snprintf, __isoc99_vsnprintf, str, size,
                        format)

#endif  // SANITIZER_INTERCEPT_ISOC99_PRINTF

#endif  // SANITIZER_INTERCEPT_PRINTF

#if SANITIZER_INTERCEPT_PRINTF
#define INIT_PRINTF                     \
  COMMON_INTERCEPT_FUNCTION_LDBL(printf);    \
  COMMON_INTERCEPT_FUNCTION_LDBL(sprintf);   \
  COMMON_INTERCEPT_FUNCTION_LDBL(snprintf);  \
  COMMON_INTERCEPT_FUNCTION_LDBL(asprintf);  \
  COMMON_INTERCEPT_FUNCTION_LDBL(fprintf);   \
  COMMON_INTERCEPT_FUNCTION_LDBL(vprintf);   \
  COMMON_INTERCEPT_FUNCTION_LDBL(vsprintf);  \
  COMMON_INTERCEPT_FUNCTION_LDBL(vsnprintf); \
  COMMON_INTERCEPT_FUNCTION_LDBL(vasprintf); \
  COMMON_INTERCEPT_FUNCTION_LDBL(vfprintf);
#else
#define INIT_PRINTF
#endif

#if SANITIZER_INTERCEPT___PRINTF_CHK
#define INIT___PRINTF_CHK                     \
  COMMON_INTERCEPT_FUNCTION(__sprintf_chk);   \
  COMMON_INTERCEPT_FUNCTION(__snprintf_chk);  \
  COMMON_INTERCEPT_FUNCTION(__vsprintf_chk);  \
  COMMON_INTERCEPT_FUNCTION(__vsnprintf_chk); \
  COMMON_INTERCEPT_FUNCTION(__fprintf_chk);
#else
#define INIT___PRINTF_CHK
#endif

#if SANITIZER_INTERCEPT_PRINTF_L
#define INIT_PRINTF_L                     \
  COMMON_INTERCEPT_FUNCTION(snprintf_l);  \
  COMMON_INTERCEPT_FUNCTION(vsnprintf_l);
#else
#define INIT_PRINTF_L
#endif

#if SANITIZER_INTERCEPT_ISOC99_PRINTF
#define INIT_ISOC99_PRINTF                       \
  COMMON_INTERCEPT_FUNCTION(__isoc99_printf);    \
  COMMON_INTERCEPT_FUNCTION(__isoc99_sprintf);   \
  COMMON_INTERCEPT_FUNCTION(__isoc99_snprintf);  \
  COMMON_INTERCEPT_FUNCTION(__isoc99_fprintf);   \
  COMMON_INTERCEPT_FUNCTION(__isoc99_vprintf);   \
  COMMON_INTERCEPT_FUNCTION(__isoc99_vsprintf);  \
  COMMON_INTERCEPT_FUNCTION(__isoc99_vsnprintf); \
  COMMON_INTERCEPT_FUNCTION(__isoc99_vfprintf);
#else
#define INIT_ISOC99_PRINTF
#endif

#if SANITIZER_INTERCEPT_IOCTL
#include "sanitizer_common_interceptors_ioctl.inc"
#include "sanitizer_interceptors_ioctl_netbsd.inc"
INTERCEPTOR(int, ioctl, int d, unsigned long request, ...) {
  // We need a frame pointer, because we call into ioctl_common_[pre|post] which
  // can trigger a report and we need to be able to unwind through this
  // function.  On Mac in debug mode we might not have a frame pointer, because
  // ioctl_common_[pre|post] doesn't get inlined here.
  ENABLE_FRAME_POINTER;

  void *ctx;
  va_list ap;
  va_start(ap, request);
  void *arg = va_arg(ap, void *);
  va_end(ap);
  COMMON_INTERCEPTOR_ENTER(ctx, ioctl, d, request, arg);

  CHECK(ioctl_initialized);

  // Note: TSan does not use common flags, and they are zero-initialized.
  // This effectively disables ioctl handling in TSan.
  if (!common_flags()->handle_ioctl) return REAL(ioctl)(d, request, arg);

  // Although request is unsigned long, the rest of the interceptor uses it
  // as just "unsigned" to save space, because we know that all values fit in
  // "unsigned" - they are compile-time constants.

  const ioctl_desc *desc = ioctl_lookup(request);
  ioctl_desc decoded_desc;
  if (!desc) {
    VPrintf(2, "Decoding unknown ioctl 0x%x\n", request);
    if (!ioctl_decode(request, &decoded_desc))
      Printf("WARNING: failed decoding unknown ioctl 0x%x\n", request);
    else
      desc = &decoded_desc;
  }

  if (desc) ioctl_common_pre(ctx, desc, d, request, arg);
  int res = REAL(ioctl)(d, request, arg);
  // FIXME: some ioctls have different return values for success and failure.
  if (desc && res != -1) ioctl_common_post(ctx, desc, res, d, request, arg);
  return res;
}
#define INIT_IOCTL \
  ioctl_init();    \
  COMMON_INTERCEPT_FUNCTION(ioctl);
#else
#define INIT_IOCTL
#endif

#if SANITIZER_INTERCEPT_GETPWNAM_AND_FRIENDS || \
    SANITIZER_INTERCEPT_GETPWENT || SANITIZER_INTERCEPT_FGETPWENT || \
    SANITIZER_INTERCEPT_GETPWENT_R || \
    SANITIZER_INTERCEPT_GETPWNAM_R_AND_FRIENDS || \
    SANITIZER_INTERCEPT_FGETPWENT_R || \
    SANITIZER_INTERCEPT_FGETGRENT_R
static void unpoison_passwd(void *ctx, __sanitizer_passwd *pwd) {
  if (pwd) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pwd, sizeof(*pwd));
    if (pwd->pw_name)
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(pwd->pw_name,
                                          REAL(strlen)(pwd->pw_name) + 1);
    if (pwd->pw_passwd)
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(pwd->pw_passwd,
                                          REAL(strlen)(pwd->pw_passwd) + 1);
#if !SANITIZER_ANDROID
    if (pwd->pw_gecos)
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(pwd->pw_gecos,
                                          REAL(strlen)(pwd->pw_gecos) + 1);
#endif
#if SANITIZER_MAC
    if (pwd->pw_class)
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(pwd->pw_class,
                                          REAL(strlen)(pwd->pw_class) + 1);
#endif
    if (pwd->pw_dir)
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(pwd->pw_dir,
                                          REAL(strlen)(pwd->pw_dir) + 1);
    if (pwd->pw_shell)
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(pwd->pw_shell,
                                          REAL(strlen)(pwd->pw_shell) + 1);
  }
}

static void unpoison_group(void *ctx, __sanitizer_group *grp) {
  if (grp) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, grp, sizeof(*grp));
    if (grp->gr_name)
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(grp->gr_name,
                                          REAL(strlen)(grp->gr_name) + 1);
    if (grp->gr_passwd)
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(grp->gr_passwd,
                                          REAL(strlen)(grp->gr_passwd) + 1);
    char **p = grp->gr_mem;
    for (; *p; ++p) {
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(*p, REAL(strlen)(*p) + 1);
    }
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(grp->gr_mem,
                                        (p - grp->gr_mem + 1) * sizeof(*p));
  }
}
#endif  // SANITIZER_INTERCEPT_GETPWNAM_AND_FRIENDS ||
        // SANITIZER_INTERCEPT_GETPWENT || SANITIZER_INTERCEPT_FGETPWENT ||
        // SANITIZER_INTERCEPT_GETPWENT_R ||
        // SANITIZER_INTERCEPT_GETPWNAM_R_AND_FRIENDS

#if SANITIZER_INTERCEPT_GETPWNAM_AND_FRIENDS
INTERCEPTOR(__sanitizer_passwd *, getpwnam, const char *name) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwnam, name);
  if (name)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  __sanitizer_passwd *res = REAL(getpwnam)(name);
  if (res) unpoison_passwd(ctx, res);
  return res;
}
INTERCEPTOR(__sanitizer_passwd *, getpwuid, u32 uid) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwuid, uid);
  __sanitizer_passwd *res = REAL(getpwuid)(uid);
  if (res) unpoison_passwd(ctx, res);
  return res;
}
INTERCEPTOR(__sanitizer_group *, getgrnam, const char *name) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrnam, name);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  __sanitizer_group *res = REAL(getgrnam)(name);
  if (res) unpoison_group(ctx, res);
  return res;
}
INTERCEPTOR(__sanitizer_group *, getgrgid, u32 gid) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrgid, gid);
  __sanitizer_group *res = REAL(getgrgid)(gid);
  if (res) unpoison_group(ctx, res);
  return res;
}
#define INIT_GETPWNAM_AND_FRIENDS      \
  COMMON_INTERCEPT_FUNCTION(getpwnam); \
  COMMON_INTERCEPT_FUNCTION(getpwuid); \
  COMMON_INTERCEPT_FUNCTION(getgrnam); \
  COMMON_INTERCEPT_FUNCTION(getgrgid);
#else
#define INIT_GETPWNAM_AND_FRIENDS
#endif

#if SANITIZER_INTERCEPT_GETPWNAM_R_AND_FRIENDS
INTERCEPTOR(int, getpwnam_r, const char *name, __sanitizer_passwd *pwd,
            char *buf, SIZE_T buflen, __sanitizer_passwd **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwnam_r, name, pwd, buf, buflen, result);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getpwnam_r)(name, pwd, buf, buflen, result);
  if (!res) {
    if (result && *result) unpoison_passwd(ctx, *result);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  if (result) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
  return res;
}
INTERCEPTOR(int, getpwuid_r, u32 uid, __sanitizer_passwd *pwd, char *buf,
            SIZE_T buflen, __sanitizer_passwd **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwuid_r, uid, pwd, buf, buflen, result);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getpwuid_r)(uid, pwd, buf, buflen, result);
  if (!res) {
    if (result && *result) unpoison_passwd(ctx, *result);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  if (result) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
  return res;
}
INTERCEPTOR(int, getgrnam_r, const char *name, __sanitizer_group *grp,
            char *buf, SIZE_T buflen, __sanitizer_group **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrnam_r, name, grp, buf, buflen, result);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getgrnam_r)(name, grp, buf, buflen, result);
  if (!res) {
    if (result && *result) unpoison_group(ctx, *result);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  if (result) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
  return res;
}
INTERCEPTOR(int, getgrgid_r, u32 gid, __sanitizer_group *grp, char *buf,
            SIZE_T buflen, __sanitizer_group **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrgid_r, gid, grp, buf, buflen, result);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getgrgid_r)(gid, grp, buf, buflen, result);
  if (!res) {
    if (result && *result) unpoison_group(ctx, *result);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  if (result) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
  return res;
}
#define INIT_GETPWNAM_R_AND_FRIENDS      \
  COMMON_INTERCEPT_FUNCTION(getpwnam_r); \
  COMMON_INTERCEPT_FUNCTION(getpwuid_r); \
  COMMON_INTERCEPT_FUNCTION(getgrnam_r); \
  COMMON_INTERCEPT_FUNCTION(getgrgid_r);
#else
#define INIT_GETPWNAM_R_AND_FRIENDS
#endif

#if SANITIZER_INTERCEPT_GETPWENT
INTERCEPTOR(__sanitizer_passwd *, getpwent, int dummy) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwent, dummy);
  __sanitizer_passwd *res = REAL(getpwent)(dummy);
  if (res) unpoison_passwd(ctx, res);
  return res;
}
INTERCEPTOR(__sanitizer_group *, getgrent, int dummy) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrent, dummy);
  __sanitizer_group *res = REAL(getgrent)(dummy);
  if (res) unpoison_group(ctx, res);;
  return res;
}
#define INIT_GETPWENT                  \
  COMMON_INTERCEPT_FUNCTION(getpwent); \
  COMMON_INTERCEPT_FUNCTION(getgrent);
#else
#define INIT_GETPWENT
#endif

#if SANITIZER_INTERCEPT_FGETPWENT
INTERCEPTOR(__sanitizer_passwd *, fgetpwent, void *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fgetpwent, fp);
  __sanitizer_passwd *res = REAL(fgetpwent)(fp);
  if (res) unpoison_passwd(ctx, res);
  return res;
}
INTERCEPTOR(__sanitizer_group *, fgetgrent, void *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fgetgrent, fp);
  __sanitizer_group *res = REAL(fgetgrent)(fp);
  if (res) unpoison_group(ctx, res);
  return res;
}
#define INIT_FGETPWENT                  \
  COMMON_INTERCEPT_FUNCTION(fgetpwent); \
  COMMON_INTERCEPT_FUNCTION(fgetgrent);
#else
#define INIT_FGETPWENT
#endif

#if SANITIZER_INTERCEPT_GETPWENT_R
INTERCEPTOR(int, getpwent_r, __sanitizer_passwd *pwbuf, char *buf,
            SIZE_T buflen, __sanitizer_passwd **pwbufp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwent_r, pwbuf, buf, buflen, pwbufp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getpwent_r)(pwbuf, buf, buflen, pwbufp);
  if (!res) {
    if (pwbufp && *pwbufp) unpoison_passwd(ctx, *pwbufp);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  if (pwbufp) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pwbufp, sizeof(*pwbufp));
  return res;
}
INTERCEPTOR(int, getgrent_r, __sanitizer_group *pwbuf, char *buf, SIZE_T buflen,
            __sanitizer_group **pwbufp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrent_r, pwbuf, buf, buflen, pwbufp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getgrent_r)(pwbuf, buf, buflen, pwbufp);
  if (!res) {
    if (pwbufp && *pwbufp) unpoison_group(ctx, *pwbufp);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  if (pwbufp) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pwbufp, sizeof(*pwbufp));
  return res;
}
#define INIT_GETPWENT_R                   \
  COMMON_INTERCEPT_FUNCTION(getpwent_r);  \
  COMMON_INTERCEPT_FUNCTION(getgrent_r);
#else
#define INIT_GETPWENT_R
#endif

#if SANITIZER_INTERCEPT_FGETPWENT_R
INTERCEPTOR(int, fgetpwent_r, void *fp, __sanitizer_passwd *pwbuf, char *buf,
            SIZE_T buflen, __sanitizer_passwd **pwbufp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fgetpwent_r, fp, pwbuf, buf, buflen, pwbufp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(fgetpwent_r)(fp, pwbuf, buf, buflen, pwbufp);
  if (!res) {
    if (pwbufp && *pwbufp) unpoison_passwd(ctx, *pwbufp);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  if (pwbufp) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pwbufp, sizeof(*pwbufp));
  return res;
}
#define INIT_FGETPWENT_R                  \
  COMMON_INTERCEPT_FUNCTION(fgetpwent_r);
#else
#define INIT_FGETPWENT_R
#endif

#if SANITIZER_INTERCEPT_FGETGRENT_R
INTERCEPTOR(int, fgetgrent_r, void *fp, __sanitizer_group *pwbuf, char *buf,
            SIZE_T buflen, __sanitizer_group **pwbufp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fgetgrent_r, fp, pwbuf, buf, buflen, pwbufp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(fgetgrent_r)(fp, pwbuf, buf, buflen, pwbufp);
  if (!res) {
    if (pwbufp && *pwbufp) unpoison_group(ctx, *pwbufp);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  if (pwbufp) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pwbufp, sizeof(*pwbufp));
  return res;
}
#define INIT_FGETGRENT_R                  \
  COMMON_INTERCEPT_FUNCTION(fgetgrent_r);
#else
#define INIT_FGETGRENT_R
#endif

#if SANITIZER_INTERCEPT_SETPWENT
// The only thing these interceptors do is disable any nested interceptors.
// These functions may open nss modules and call uninstrumented functions from
// them, and we don't want things like strlen() to trigger.
INTERCEPTOR(void, setpwent, int dummy) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setpwent, dummy);
  REAL(setpwent)(dummy);
}
INTERCEPTOR(void, endpwent, int dummy) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, endpwent, dummy);
  REAL(endpwent)(dummy);
}
INTERCEPTOR(void, setgrent, int dummy) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setgrent, dummy);
  REAL(setgrent)(dummy);
}
INTERCEPTOR(void, endgrent, int dummy) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, endgrent, dummy);
  REAL(endgrent)(dummy);
}
#define INIT_SETPWENT                  \
  COMMON_INTERCEPT_FUNCTION(setpwent); \
  COMMON_INTERCEPT_FUNCTION(endpwent); \
  COMMON_INTERCEPT_FUNCTION(setgrent); \
  COMMON_INTERCEPT_FUNCTION(endgrent);
#else
#define INIT_SETPWENT
#endif

#if SANITIZER_INTERCEPT_CLOCK_GETTIME
INTERCEPTOR(int, clock_getres, u32 clk_id, void *tp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, clock_getres, clk_id, tp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(clock_getres)(clk_id, tp);
  if (!res && tp) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, tp, struct_timespec_sz);
  }
  return res;
}
INTERCEPTOR(int, clock_gettime, u32 clk_id, void *tp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, clock_gettime, clk_id, tp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(clock_gettime)(clk_id, tp);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, tp, struct_timespec_sz);
  }
  return res;
}
namespace __sanitizer {
extern "C" {
int real_clock_gettime(u32 clk_id, void *tp) {
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_clock_gettime(clk_id, tp);
  return REAL(clock_gettime)(clk_id, tp);
}
}  // extern "C"
}  // namespace __sanitizer
INTERCEPTOR(int, clock_settime, u32 clk_id, const void *tp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, clock_settime, clk_id, tp);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, tp, struct_timespec_sz);
  return REAL(clock_settime)(clk_id, tp);
}
#define INIT_CLOCK_GETTIME                  \
  COMMON_INTERCEPT_FUNCTION(clock_getres);  \
  COMMON_INTERCEPT_FUNCTION(clock_gettime); \
  COMMON_INTERCEPT_FUNCTION(clock_settime);
#else
#define INIT_CLOCK_GETTIME
#endif

#if SANITIZER_INTERCEPT_GETITIMER
INTERCEPTOR(int, getitimer, int which, void *curr_value) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getitimer, which, curr_value);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getitimer)(which, curr_value);
  if (!res && curr_value) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, curr_value, struct_itimerval_sz);
  }
  return res;
}
INTERCEPTOR(int, setitimer, int which, const void *new_value, void *old_value) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setitimer, which, new_value, old_value);
  if (new_value) {
    // itimerval can contain padding that may be legitimately uninitialized
    const struct __sanitizer_itimerval *nv =
        (const struct __sanitizer_itimerval *)new_value;
    COMMON_INTERCEPTOR_READ_RANGE(ctx, &nv->it_interval.tv_sec,
                                  sizeof(__sanitizer_time_t));
    COMMON_INTERCEPTOR_READ_RANGE(ctx, &nv->it_interval.tv_usec,
                                  sizeof(__sanitizer_suseconds_t));
    COMMON_INTERCEPTOR_READ_RANGE(ctx, &nv->it_value.tv_sec,
                                  sizeof(__sanitizer_time_t));
    COMMON_INTERCEPTOR_READ_RANGE(ctx, &nv->it_value.tv_usec,
                                  sizeof(__sanitizer_suseconds_t));
  }
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(setitimer)(which, new_value, old_value);
  if (!res && old_value) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, old_value, struct_itimerval_sz);
  }
  return res;
}
#define INIT_GETITIMER                  \
  COMMON_INTERCEPT_FUNCTION(getitimer); \
  COMMON_INTERCEPT_FUNCTION(setitimer);
#else
#define INIT_GETITIMER
#endif

#if SANITIZER_INTERCEPT_GLOB
static void unpoison_glob_t(void *ctx, __sanitizer_glob_t *pglob) {
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pglob, sizeof(*pglob));
  // +1 for NULL pointer at the end.
  if (pglob->gl_pathv)
    COMMON_INTERCEPTOR_WRITE_RANGE(
        ctx, pglob->gl_pathv, (pglob->gl_pathc + 1) * sizeof(*pglob->gl_pathv));
  for (SIZE_T i = 0; i < pglob->gl_pathc; ++i) {
    char *p = pglob->gl_pathv[i];
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, REAL(strlen)(p) + 1);
  }
}

#if SANITIZER_SOLARIS
INTERCEPTOR(int, glob, const char *pattern, int flags,
            int (*errfunc)(const char *epath, int eerrno),
            __sanitizer_glob_t *pglob) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, glob, pattern, flags, errfunc, pglob);
  COMMON_INTERCEPTOR_READ_STRING(ctx, pattern, 0);
  int res = REAL(glob)(pattern, flags, errfunc, pglob);
  if ((!res || res == glob_nomatch) && pglob) unpoison_glob_t(ctx, pglob);
  return res;
}
#else
static THREADLOCAL __sanitizer_glob_t *pglob_copy;

static void wrapped_gl_closedir(void *dir) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(1);
  pglob_copy->gl_closedir(dir);
}

static void *wrapped_gl_readdir(void *dir) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(1);
  return pglob_copy->gl_readdir(dir);
}

static void *wrapped_gl_opendir(const char *s) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(1);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(s, REAL(strlen)(s) + 1);
  return pglob_copy->gl_opendir(s);
}

static int wrapped_gl_lstat(const char *s, void *st) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(2);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(s, REAL(strlen)(s) + 1);
  return pglob_copy->gl_lstat(s, st);
}

static int wrapped_gl_stat(const char *s, void *st) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(2);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(s, REAL(strlen)(s) + 1);
  return pglob_copy->gl_stat(s, st);
}

static const __sanitizer_glob_t kGlobCopy = {
      0,                  0,                   0,
      0,                  wrapped_gl_closedir, wrapped_gl_readdir,
      wrapped_gl_opendir, wrapped_gl_lstat,    wrapped_gl_stat};

INTERCEPTOR(int, glob, const char *pattern, int flags,
            int (*errfunc)(const char *epath, int eerrno),
            __sanitizer_glob_t *pglob) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, glob, pattern, flags, errfunc, pglob);
  COMMON_INTERCEPTOR_READ_STRING(ctx, pattern, 0);
  __sanitizer_glob_t glob_copy;
  internal_memcpy(&glob_copy, &kGlobCopy, sizeof(glob_copy));
  if (flags & glob_altdirfunc) {
    Swap(pglob->gl_closedir, glob_copy.gl_closedir);
    Swap(pglob->gl_readdir, glob_copy.gl_readdir);
    Swap(pglob->gl_opendir, glob_copy.gl_opendir);
    Swap(pglob->gl_lstat, glob_copy.gl_lstat);
    Swap(pglob->gl_stat, glob_copy.gl_stat);
    pglob_copy = &glob_copy;
  }
  int res = REAL(glob)(pattern, flags, errfunc, pglob);
  if (flags & glob_altdirfunc) {
    Swap(pglob->gl_closedir, glob_copy.gl_closedir);
    Swap(pglob->gl_readdir, glob_copy.gl_readdir);
    Swap(pglob->gl_opendir, glob_copy.gl_opendir);
    Swap(pglob->gl_lstat, glob_copy.gl_lstat);
    Swap(pglob->gl_stat, glob_copy.gl_stat);
  }
  pglob_copy = 0;
  if ((!res || res == glob_nomatch) && pglob) unpoison_glob_t(ctx, pglob);
  return res;
}
#endif  // SANITIZER_SOLARIS
#define INIT_GLOB                  \
  COMMON_INTERCEPT_FUNCTION(glob);
#else  // SANITIZER_INTERCEPT_GLOB
#define INIT_GLOB
#endif  // SANITIZER_INTERCEPT_GLOB

#if SANITIZER_INTERCEPT_GLOB64
INTERCEPTOR(int, glob64, const char *pattern, int flags,
            int (*errfunc)(const char *epath, int eerrno),
            __sanitizer_glob_t *pglob) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, glob64, pattern, flags, errfunc, pglob);
  COMMON_INTERCEPTOR_READ_STRING(ctx, pattern, 0);
  __sanitizer_glob_t glob_copy;
  internal_memcpy(&glob_copy, &kGlobCopy, sizeof(glob_copy));
  if (flags & glob_altdirfunc) {
    Swap(pglob->gl_closedir, glob_copy.gl_closedir);
    Swap(pglob->gl_readdir, glob_copy.gl_readdir);
    Swap(pglob->gl_opendir, glob_copy.gl_opendir);
    Swap(pglob->gl_lstat, glob_copy.gl_lstat);
    Swap(pglob->gl_stat, glob_copy.gl_stat);
    pglob_copy = &glob_copy;
  }
  int res = REAL(glob64)(pattern, flags, errfunc, pglob);
  if (flags & glob_altdirfunc) {
    Swap(pglob->gl_closedir, glob_copy.gl_closedir);
    Swap(pglob->gl_readdir, glob_copy.gl_readdir);
    Swap(pglob->gl_opendir, glob_copy.gl_opendir);
    Swap(pglob->gl_lstat, glob_copy.gl_lstat);
    Swap(pglob->gl_stat, glob_copy.gl_stat);
  }
  pglob_copy = 0;
  if ((!res || res == glob_nomatch) && pglob) unpoison_glob_t(ctx, pglob);
  return res;
}
#define INIT_GLOB64                \
  COMMON_INTERCEPT_FUNCTION(glob64);
#else  // SANITIZER_INTERCEPT_GLOB64
#define INIT_GLOB64
#endif  // SANITIZER_INTERCEPT_GLOB64

#if SANITIZER_INTERCEPT_WAIT
// According to sys/wait.h, wait(), waitid(), waitpid() may have symbol version
// suffixes on Darwin. See the declaration of INTERCEPTOR_WITH_SUFFIX for
// details.
INTERCEPTOR_WITH_SUFFIX(int, wait, int *status) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wait, status);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(wait)(status);
  if (res != -1 && status)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, status, sizeof(*status));
  return res;
}
// On FreeBSD id_t is always 64-bit wide.
#if SANITIZER_FREEBSD && (SANITIZER_WORDSIZE == 32)
INTERCEPTOR_WITH_SUFFIX(int, waitid, int idtype, long long id, void *infop,
                        int options) {
#else
INTERCEPTOR_WITH_SUFFIX(int, waitid, int idtype, int id, void *infop,
                        int options) {
#endif
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, waitid, idtype, id, infop, options);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(waitid)(idtype, id, infop, options);
  if (res != -1 && infop)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, infop, siginfo_t_sz);
  return res;
}
INTERCEPTOR_WITH_SUFFIX(int, waitpid, int pid, int *status, int options) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, waitpid, pid, status, options);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(waitpid)(pid, status, options);
  if (res != -1 && status)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, status, sizeof(*status));
  return res;
}
INTERCEPTOR(int, wait3, int *status, int options, void *rusage) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wait3, status, options, rusage);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(wait3)(status, options, rusage);
  if (res != -1) {
    if (status) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, status, sizeof(*status));
    if (rusage) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, rusage, struct_rusage_sz);
  }
  return res;
}
#if SANITIZER_ANDROID
INTERCEPTOR(int, __wait4, int pid, int *status, int options, void *rusage) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __wait4, pid, status, options, rusage);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(__wait4)(pid, status, options, rusage);
  if (res != -1) {
    if (status) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, status, sizeof(*status));
    if (rusage) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, rusage, struct_rusage_sz);
  }
  return res;
}
#define INIT_WAIT4 COMMON_INTERCEPT_FUNCTION(__wait4);
#else
INTERCEPTOR(int, wait4, int pid, int *status, int options, void *rusage) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wait4, pid, status, options, rusage);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(wait4)(pid, status, options, rusage);
  if (res != -1) {
    if (status) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, status, sizeof(*status));
    if (rusage) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, rusage, struct_rusage_sz);
  }
  return res;
}
#define INIT_WAIT4 COMMON_INTERCEPT_FUNCTION(wait4);
#endif  // SANITIZER_ANDROID
#define INIT_WAIT                     \
  COMMON_INTERCEPT_FUNCTION(wait);    \
  COMMON_INTERCEPT_FUNCTION(waitid);  \
  COMMON_INTERCEPT_FUNCTION(waitpid); \
  COMMON_INTERCEPT_FUNCTION(wait3);
#else
#define INIT_WAIT
#define INIT_WAIT4
#endif

#if SANITIZER_INTERCEPT_INET
INTERCEPTOR(char *, inet_ntop, int af, const void *src, char *dst, u32 size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, inet_ntop, af, src, dst, size);
  uptr sz = __sanitizer_in_addr_sz(af);
  if (sz) COMMON_INTERCEPTOR_READ_RANGE(ctx, src, sz);
  // FIXME: figure out read size based on the address family.
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(inet_ntop)(af, src, dst, size);
  if (res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  return res;
}
INTERCEPTOR(int, inet_pton, int af, const char *src, void *dst) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, inet_pton, af, src, dst);
  COMMON_INTERCEPTOR_READ_STRING(ctx, src, 0);
  // FIXME: figure out read size based on the address family.
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(inet_pton)(af, src, dst);
  if (res == 1) {
    uptr sz = __sanitizer_in_addr_sz(af);
    if (sz) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, sz);
  }
  return res;
}
#define INIT_INET                       \
  COMMON_INTERCEPT_FUNCTION(inet_ntop); \
  COMMON_INTERCEPT_FUNCTION(inet_pton);
#else
#define INIT_INET
#endif

#if SANITIZER_INTERCEPT_INET
INTERCEPTOR(int, inet_aton, const char *cp, void *dst) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, inet_aton, cp, dst);
  if (cp) COMMON_INTERCEPTOR_READ_RANGE(ctx, cp, REAL(strlen)(cp) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(inet_aton)(cp, dst);
  if (res != 0) {
    uptr sz = __sanitizer_in_addr_sz(af_inet);
    if (sz) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, sz);
  }
  return res;
}
#define INIT_INET_ATON COMMON_INTERCEPT_FUNCTION(inet_aton);
#else
#define INIT_INET_ATON
#endif

#if SANITIZER_INTERCEPT_PTHREAD_GETSCHEDPARAM
INTERCEPTOR(int, pthread_getschedparam, uptr thread, int *policy, int *param) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_getschedparam, thread, policy, param);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(pthread_getschedparam)(thread, policy, param);
  if (res == 0) {
    if (policy) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, policy, sizeof(*policy));
    if (param) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, param, sizeof(*param));
  }
  return res;
}
#define INIT_PTHREAD_GETSCHEDPARAM \
  COMMON_INTERCEPT_FUNCTION(pthread_getschedparam);
#else
#define INIT_PTHREAD_GETSCHEDPARAM
#endif

#if SANITIZER_INTERCEPT_GETADDRINFO
INTERCEPTOR(int, getaddrinfo, char *node, char *service,
            struct __sanitizer_addrinfo *hints,
            struct __sanitizer_addrinfo **out) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getaddrinfo, node, service, hints, out);
  if (node) COMMON_INTERCEPTOR_READ_RANGE(ctx, node, REAL(strlen)(node) + 1);
  if (service)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, service, REAL(strlen)(service) + 1);
  if (hints)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, hints, sizeof(__sanitizer_addrinfo));
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getaddrinfo)(node, service, hints, out);
  if (res == 0 && out) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, out, sizeof(*out));
    struct __sanitizer_addrinfo *p = *out;
    while (p) {
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, sizeof(*p));
      if (p->ai_addr)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->ai_addr, p->ai_addrlen);
      if (p->ai_canonname)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->ai_canonname,
                                       REAL(strlen)(p->ai_canonname) + 1);
      p = p->ai_next;
    }
  }
  return res;
}
#define INIT_GETADDRINFO COMMON_INTERCEPT_FUNCTION(getaddrinfo);
#else
#define INIT_GETADDRINFO
#endif

#if SANITIZER_INTERCEPT_GETNAMEINFO
INTERCEPTOR(int, getnameinfo, void *sockaddr, unsigned salen, char *host,
            unsigned hostlen, char *serv, unsigned servlen, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getnameinfo, sockaddr, salen, host, hostlen,
                           serv, servlen, flags);
  // FIXME: consider adding READ_RANGE(sockaddr, salen)
  // There is padding in in_addr that may make this too noisy
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res =
      REAL(getnameinfo)(sockaddr, salen, host, hostlen, serv, servlen, flags);
  if (res == 0) {
    if (host && hostlen)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, host, REAL(strlen)(host) + 1);
    if (serv && servlen)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, serv, REAL(strlen)(serv) + 1);
  }
  return res;
}
#define INIT_GETNAMEINFO COMMON_INTERCEPT_FUNCTION(getnameinfo);
#else
#define INIT_GETNAMEINFO
#endif

#if SANITIZER_INTERCEPT_GETSOCKNAME
INTERCEPTOR(int, getsockname, int sock_fd, void *addr, int *addrlen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getsockname, sock_fd, addr, addrlen);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, addrlen, sizeof(*addrlen));
  int addrlen_in = *addrlen;
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getsockname)(sock_fd, addr, addrlen);
  if (res == 0) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, addr, Min(addrlen_in, *addrlen));
  }
  return res;
}
#define INIT_GETSOCKNAME COMMON_INTERCEPT_FUNCTION(getsockname);
#else
#define INIT_GETSOCKNAME
#endif

#if SANITIZER_INTERCEPT_GETHOSTBYNAME || SANITIZER_INTERCEPT_GETHOSTBYNAME_R
static void write_hostent(void *ctx, struct __sanitizer_hostent *h) {
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, h, sizeof(__sanitizer_hostent));
  if (h->h_name)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, h->h_name, REAL(strlen)(h->h_name) + 1);
  char **p = h->h_aliases;
  while (*p) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *p, REAL(strlen)(*p) + 1);
    ++p;
  }
  COMMON_INTERCEPTOR_WRITE_RANGE(
      ctx, h->h_aliases, (p - h->h_aliases + 1) * sizeof(*h->h_aliases));
  p = h->h_addr_list;
  while (*p) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *p, h->h_length);
    ++p;
  }
  COMMON_INTERCEPTOR_WRITE_RANGE(
      ctx, h->h_addr_list, (p - h->h_addr_list + 1) * sizeof(*h->h_addr_list));
}
#endif

#if SANITIZER_INTERCEPT_GETHOSTBYNAME
INTERCEPTOR(struct __sanitizer_hostent *, gethostbyname, char *name) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gethostbyname, name);
  struct __sanitizer_hostent *res = REAL(gethostbyname)(name);
  if (res) write_hostent(ctx, res);
  return res;
}

INTERCEPTOR(struct __sanitizer_hostent *, gethostbyaddr, void *addr, int len,
            int type) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gethostbyaddr, addr, len, type);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, addr, len);
  struct __sanitizer_hostent *res = REAL(gethostbyaddr)(addr, len, type);
  if (res) write_hostent(ctx, res);
  return res;
}

INTERCEPTOR(struct __sanitizer_hostent *, gethostent, int fake) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gethostent, fake);
  struct __sanitizer_hostent *res = REAL(gethostent)(fake);
  if (res) write_hostent(ctx, res);
  return res;
}
#define INIT_GETHOSTBYNAME                  \
  COMMON_INTERCEPT_FUNCTION(gethostent);    \
  COMMON_INTERCEPT_FUNCTION(gethostbyaddr); \
  COMMON_INTERCEPT_FUNCTION(gethostbyname);
#else
#define INIT_GETHOSTBYNAME
#endif  // SANITIZER_INTERCEPT_GETHOSTBYNAME

#if SANITIZER_INTERCEPT_GETHOSTBYNAME2
INTERCEPTOR(struct __sanitizer_hostent *, gethostbyname2, char *name, int af) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gethostbyname2, name, af);
  struct __sanitizer_hostent *res = REAL(gethostbyname2)(name, af);
  if (res) write_hostent(ctx, res);
  return res;
}
#define INIT_GETHOSTBYNAME2 COMMON_INTERCEPT_FUNCTION(gethostbyname2);
#else
#define INIT_GETHOSTBYNAME2
#endif  // SANITIZER_INTERCEPT_GETHOSTBYNAME2

#if SANITIZER_INTERCEPT_GETHOSTBYNAME_R
INTERCEPTOR(int, gethostbyname_r, char *name, struct __sanitizer_hostent *ret,
            char *buf, SIZE_T buflen, __sanitizer_hostent **result,
            int *h_errnop) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gethostbyname_r, name, ret, buf, buflen, result,
                           h_errnop);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(gethostbyname_r)(name, ret, buf, buflen, result, h_errnop);
  if (result) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
    if (res == 0 && *result) write_hostent(ctx, *result);
  }
  if (h_errnop)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, h_errnop, sizeof(*h_errnop));
  return res;
}
#define INIT_GETHOSTBYNAME_R COMMON_INTERCEPT_FUNCTION(gethostbyname_r);
#else
#define INIT_GETHOSTBYNAME_R
#endif

#if SANITIZER_INTERCEPT_GETHOSTENT_R
INTERCEPTOR(int, gethostent_r, struct __sanitizer_hostent *ret, char *buf,
            SIZE_T buflen, __sanitizer_hostent **result, int *h_errnop) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gethostent_r, ret, buf, buflen, result,
                           h_errnop);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(gethostent_r)(ret, buf, buflen, result, h_errnop);
  if (result) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
    if (res == 0 && *result) write_hostent(ctx, *result);
  }
  if (h_errnop)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, h_errnop, sizeof(*h_errnop));
  return res;
}
#define INIT_GETHOSTENT_R                  \
  COMMON_INTERCEPT_FUNCTION(gethostent_r);
#else
#define INIT_GETHOSTENT_R
#endif

#if SANITIZER_INTERCEPT_GETHOSTBYADDR_R
INTERCEPTOR(int, gethostbyaddr_r, void *addr, int len, int type,
            struct __sanitizer_hostent *ret, char *buf, SIZE_T buflen,
            __sanitizer_hostent **result, int *h_errnop) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gethostbyaddr_r, addr, len, type, ret, buf,
                           buflen, result, h_errnop);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, addr, len);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(gethostbyaddr_r)(addr, len, type, ret, buf, buflen, result,
                                  h_errnop);
  if (result) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
    if (res == 0 && *result) write_hostent(ctx, *result);
  }
  if (h_errnop)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, h_errnop, sizeof(*h_errnop));
  return res;
}
#define INIT_GETHOSTBYADDR_R                  \
  COMMON_INTERCEPT_FUNCTION(gethostbyaddr_r);
#else
#define INIT_GETHOSTBYADDR_R
#endif

#if SANITIZER_INTERCEPT_GETHOSTBYNAME2_R
INTERCEPTOR(int, gethostbyname2_r, char *name, int af,
            struct __sanitizer_hostent *ret, char *buf, SIZE_T buflen,
            __sanitizer_hostent **result, int *h_errnop) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gethostbyname2_r, name, af, ret, buf, buflen,
                           result, h_errnop);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res =
      REAL(gethostbyname2_r)(name, af, ret, buf, buflen, result, h_errnop);
  if (result) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
    if (res == 0 && *result) write_hostent(ctx, *result);
  }
  if (h_errnop)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, h_errnop, sizeof(*h_errnop));
  return res;
}
#define INIT_GETHOSTBYNAME2_R                  \
  COMMON_INTERCEPT_FUNCTION(gethostbyname2_r);
#else
#define INIT_GETHOSTBYNAME2_R
#endif

#if SANITIZER_INTERCEPT_GETSOCKOPT
INTERCEPTOR(int, getsockopt, int sockfd, int level, int optname, void *optval,
            int *optlen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getsockopt, sockfd, level, optname, optval,
                           optlen);
  if (optlen) COMMON_INTERCEPTOR_READ_RANGE(ctx, optlen, sizeof(*optlen));
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getsockopt)(sockfd, level, optname, optval, optlen);
  if (res == 0)
    if (optval && optlen) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, optval, *optlen);
  return res;
}
#define INIT_GETSOCKOPT COMMON_INTERCEPT_FUNCTION(getsockopt);
#else
#define INIT_GETSOCKOPT
#endif

#if SANITIZER_INTERCEPT_ACCEPT
INTERCEPTOR(int, accept, int fd, void *addr, unsigned *addrlen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, accept, fd, addr, addrlen);
  unsigned addrlen0 = 0;
  if (addrlen) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, addrlen, sizeof(*addrlen));
    addrlen0 = *addrlen;
  }
  int fd2 = REAL(accept)(fd, addr, addrlen);
  if (fd2 >= 0) {
    if (fd >= 0) COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, fd2);
    if (addr && addrlen)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, addr, Min(*addrlen, addrlen0));
  }
  return fd2;
}
#define INIT_ACCEPT COMMON_INTERCEPT_FUNCTION(accept);
#else
#define INIT_ACCEPT
#endif

#if SANITIZER_INTERCEPT_ACCEPT4
INTERCEPTOR(int, accept4, int fd, void *addr, unsigned *addrlen, int f) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, accept4, fd, addr, addrlen, f);
  unsigned addrlen0 = 0;
  if (addrlen) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, addrlen, sizeof(*addrlen));
    addrlen0 = *addrlen;
  }
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int fd2 = REAL(accept4)(fd, addr, addrlen, f);
  if (fd2 >= 0) {
    if (fd >= 0) COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, fd2);
    if (addr && addrlen)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, addr, Min(*addrlen, addrlen0));
  }
  return fd2;
}
#define INIT_ACCEPT4 COMMON_INTERCEPT_FUNCTION(accept4);
#else
#define INIT_ACCEPT4
#endif

#if SANITIZER_INTERCEPT_PACCEPT
INTERCEPTOR(int, paccept, int fd, void *addr, unsigned *addrlen,
            __sanitizer_sigset_t *set, int f) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, paccept, fd, addr, addrlen, set, f);
  unsigned addrlen0 = 0;
  if (addrlen) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, addrlen, sizeof(*addrlen));
    addrlen0 = *addrlen;
  }
  if (set) COMMON_INTERCEPTOR_READ_RANGE(ctx, set, sizeof(*set));
  int fd2 = REAL(paccept)(fd, addr, addrlen, set, f);
  if (fd2 >= 0) {
    if (fd >= 0) COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, fd2);
    if (addr && addrlen)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, addr, Min(*addrlen, addrlen0));
  }
  return fd2;
}
#define INIT_PACCEPT COMMON_INTERCEPT_FUNCTION(paccept);
#else
#define INIT_PACCEPT
#endif

#if SANITIZER_INTERCEPT_MODF
INTERCEPTOR(double, modf, double x, double *iptr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, modf, x, iptr);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  double res = REAL(modf)(x, iptr);
  if (iptr) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, iptr, sizeof(*iptr));
  }
  return res;
}
INTERCEPTOR(float, modff, float x, float *iptr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, modff, x, iptr);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  float res = REAL(modff)(x, iptr);
  if (iptr) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, iptr, sizeof(*iptr));
  }
  return res;
}
INTERCEPTOR(long double, modfl, long double x, long double *iptr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, modfl, x, iptr);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  long double res = REAL(modfl)(x, iptr);
  if (iptr) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, iptr, sizeof(*iptr));
  }
  return res;
}
#define INIT_MODF                   \
  COMMON_INTERCEPT_FUNCTION(modf);  \
  COMMON_INTERCEPT_FUNCTION(modff); \
  COMMON_INTERCEPT_FUNCTION_LDBL(modfl);
#else
#define INIT_MODF
#endif

#if SANITIZER_INTERCEPT_RECVMSG || SANITIZER_INTERCEPT_RECVMMSG
static void write_msghdr(void *ctx, struct __sanitizer_msghdr *msg,
                         SSIZE_T maxlen) {
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, msg, sizeof(*msg));
  if (msg->msg_name && msg->msg_namelen)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, msg->msg_name, msg->msg_namelen);
  if (msg->msg_iov && msg->msg_iovlen)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, msg->msg_iov,
                                   sizeof(*msg->msg_iov) * msg->msg_iovlen);
  write_iovec(ctx, msg->msg_iov, msg->msg_iovlen, maxlen);
  if (msg->msg_control && msg->msg_controllen)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, msg->msg_control, msg->msg_controllen);
}
#endif

#if SANITIZER_INTERCEPT_RECVMSG
INTERCEPTOR(SSIZE_T, recvmsg, int fd, struct __sanitizer_msghdr *msg,
            int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, recvmsg, fd, msg, flags);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(recvmsg)(fd, msg, flags);
  if (res >= 0) {
    if (fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
    if (msg) {
      write_msghdr(ctx, msg, res);
      COMMON_INTERCEPTOR_HANDLE_RECVMSG(ctx, msg);
    }
  }
  return res;
}
#define INIT_RECVMSG COMMON_INTERCEPT_FUNCTION(recvmsg);
#else
#define INIT_RECVMSG
#endif

#if SANITIZER_INTERCEPT_RECVMMSG
INTERCEPTOR(int, recvmmsg, int fd, struct __sanitizer_mmsghdr *msgvec,
            unsigned int vlen, int flags, void *timeout) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, recvmmsg, fd, msgvec, vlen, flags, timeout);
  if (timeout) COMMON_INTERCEPTOR_READ_RANGE(ctx, timeout, struct_timespec_sz);
  int res = REAL(recvmmsg)(fd, msgvec, vlen, flags, timeout);
  if (res >= 0) {
    if (fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
    for (int i = 0; i < res; ++i) {
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, &msgvec[i].msg_len,
                                     sizeof(msgvec[i].msg_len));
      write_msghdr(ctx, &msgvec[i].msg_hdr, msgvec[i].msg_len);
      COMMON_INTERCEPTOR_HANDLE_RECVMSG(ctx, &msgvec[i].msg_hdr);
    }
  }
  return res;
}
#define INIT_RECVMMSG COMMON_INTERCEPT_FUNCTION(recvmmsg);
#else
#define INIT_RECVMMSG
#endif

#if SANITIZER_INTERCEPT_SENDMSG || SANITIZER_INTERCEPT_SENDMMSG
static void read_msghdr_control(void *ctx, void *control, uptr controllen) {
  const unsigned kCmsgDataOffset =
      RoundUpTo(sizeof(__sanitizer_cmsghdr), sizeof(uptr));

  char *p = (char *)control;
  char *const control_end = p + controllen;
  while (true) {
    if (p + sizeof(__sanitizer_cmsghdr) > control_end) break;
    __sanitizer_cmsghdr *cmsg = (__sanitizer_cmsghdr *)p;
    COMMON_INTERCEPTOR_READ_RANGE(ctx, &cmsg->cmsg_len, sizeof(cmsg->cmsg_len));

    if (p + RoundUpTo(cmsg->cmsg_len, sizeof(uptr)) > control_end) break;

    COMMON_INTERCEPTOR_READ_RANGE(ctx, &cmsg->cmsg_level,
                                  sizeof(cmsg->cmsg_level));
    COMMON_INTERCEPTOR_READ_RANGE(ctx, &cmsg->cmsg_type,
                                  sizeof(cmsg->cmsg_type));

    if (cmsg->cmsg_len > kCmsgDataOffset) {
      char *data = p + kCmsgDataOffset;
      unsigned data_len = cmsg->cmsg_len - kCmsgDataOffset;
      if (data_len > 0) COMMON_INTERCEPTOR_READ_RANGE(ctx, data, data_len);
    }

    p += RoundUpTo(cmsg->cmsg_len, sizeof(uptr));
  }
}

static void read_msghdr(void *ctx, struct __sanitizer_msghdr *msg,
                        SSIZE_T maxlen) {
#define R(f) \
  COMMON_INTERCEPTOR_READ_RANGE(ctx, &msg->msg_##f, sizeof(msg->msg_##f))
  R(name);
  R(namelen);
  R(iov);
  R(iovlen);
  R(control);
  R(controllen);
  R(flags);
#undef R
  if (msg->msg_name && msg->msg_namelen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, msg->msg_name, msg->msg_namelen);
  if (msg->msg_iov && msg->msg_iovlen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, msg->msg_iov,
                                  sizeof(*msg->msg_iov) * msg->msg_iovlen);
  read_iovec(ctx, msg->msg_iov, msg->msg_iovlen, maxlen);
  if (msg->msg_control && msg->msg_controllen)
    read_msghdr_control(ctx, msg->msg_control, msg->msg_controllen);
}
#endif

#if SANITIZER_INTERCEPT_SENDMSG
INTERCEPTOR(SSIZE_T, sendmsg, int fd, struct __sanitizer_msghdr *msg,
            int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sendmsg, fd, msg, flags);
  if (fd >= 0) {
    COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
    COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  }
  SSIZE_T res = REAL(sendmsg)(fd, msg, flags);
  if (common_flags()->intercept_send && res >= 0 && msg)
    read_msghdr(ctx, msg, res);
  return res;
}
#define INIT_SENDMSG COMMON_INTERCEPT_FUNCTION(sendmsg);
#else
#define INIT_SENDMSG
#endif

#if SANITIZER_INTERCEPT_SENDMMSG
INTERCEPTOR(int, sendmmsg, int fd, struct __sanitizer_mmsghdr *msgvec,
            unsigned vlen, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sendmmsg, fd, msgvec, vlen, flags);
  if (fd >= 0) {
    COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
    COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  }
  int res = REAL(sendmmsg)(fd, msgvec, vlen, flags);
  if (res >= 0 && msgvec)
    for (int i = 0; i < res; ++i) {
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, &msgvec[i].msg_len,
                                     sizeof(msgvec[i].msg_len));
      if (common_flags()->intercept_send)
        read_msghdr(ctx, &msgvec[i].msg_hdr, msgvec[i].msg_len);
    }
  return res;
}
#define INIT_SENDMMSG COMMON_INTERCEPT_FUNCTION(sendmmsg);
#else
#define INIT_SENDMMSG
#endif

#if SANITIZER_INTERCEPT_GETPEERNAME
INTERCEPTOR(int, getpeername, int sockfd, void *addr, unsigned *addrlen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpeername, sockfd, addr, addrlen);
  unsigned addr_sz;
  if (addrlen) addr_sz = *addrlen;
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getpeername)(sockfd, addr, addrlen);
  if (!res && addr && addrlen)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, addr, Min(addr_sz, *addrlen));
  return res;
}
#define INIT_GETPEERNAME COMMON_INTERCEPT_FUNCTION(getpeername);
#else
#define INIT_GETPEERNAME
#endif

#if SANITIZER_INTERCEPT_SYSINFO
INTERCEPTOR(int, sysinfo, void *info) {
  void *ctx;
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  COMMON_INTERCEPTOR_ENTER(ctx, sysinfo, info);
  int res = REAL(sysinfo)(info);
  if (!res && info)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, info, struct_sysinfo_sz);
  return res;
}
#define INIT_SYSINFO COMMON_INTERCEPT_FUNCTION(sysinfo);
#else
#define INIT_SYSINFO
#endif

#if SANITIZER_INTERCEPT_READDIR
INTERCEPTOR(__sanitizer_dirent *, opendir, const char *path) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, opendir, path);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  __sanitizer_dirent *res = REAL(opendir)(path);
  if (res)
    COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path);
  return res;
}

INTERCEPTOR(__sanitizer_dirent *, readdir, void *dirp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, readdir, dirp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  __sanitizer_dirent *res = REAL(readdir)(dirp);
  if (res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, res->d_reclen);
  return res;
}

INTERCEPTOR(int, readdir_r, void *dirp, __sanitizer_dirent *entry,
            __sanitizer_dirent **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, readdir_r, dirp, entry, result);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(readdir_r)(dirp, entry, result);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
    if (*result)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *result, (*result)->d_reclen);
  }
  return res;
}

#define INIT_READDIR                  \
  COMMON_INTERCEPT_FUNCTION(opendir); \
  COMMON_INTERCEPT_FUNCTION(readdir); \
  COMMON_INTERCEPT_FUNCTION(readdir_r);
#else
#define INIT_READDIR
#endif

#if SANITIZER_INTERCEPT_READDIR64
INTERCEPTOR(__sanitizer_dirent64 *, readdir64, void *dirp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, readdir64, dirp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  __sanitizer_dirent64 *res = REAL(readdir64)(dirp);
  if (res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, res->d_reclen);
  return res;
}

INTERCEPTOR(int, readdir64_r, void *dirp, __sanitizer_dirent64 *entry,
            __sanitizer_dirent64 **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, readdir64_r, dirp, entry, result);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(readdir64_r)(dirp, entry, result);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
    if (*result)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *result, (*result)->d_reclen);
  }
  return res;
}
#define INIT_READDIR64                  \
  COMMON_INTERCEPT_FUNCTION(readdir64); \
  COMMON_INTERCEPT_FUNCTION(readdir64_r);
#else
#define INIT_READDIR64
#endif

#if SANITIZER_INTERCEPT_PTRACE
INTERCEPTOR(uptr, ptrace, int request, int pid, void *addr, void *data) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ptrace, request, pid, addr, data);
  __sanitizer_iovec local_iovec;

  if (data) {
    if (request == ptrace_setregs)
      COMMON_INTERCEPTOR_READ_RANGE(ctx, data, struct_user_regs_struct_sz);
    else if (request == ptrace_setfpregs)
      COMMON_INTERCEPTOR_READ_RANGE(ctx, data, struct_user_fpregs_struct_sz);
    else if (request == ptrace_setfpxregs)
      COMMON_INTERCEPTOR_READ_RANGE(ctx, data, struct_user_fpxregs_struct_sz);
    else if (request == ptrace_setvfpregs)
      COMMON_INTERCEPTOR_READ_RANGE(ctx, data, struct_user_vfpregs_struct_sz);
    else if (request == ptrace_setsiginfo)
      COMMON_INTERCEPTOR_READ_RANGE(ctx, data, siginfo_t_sz);
    // Some kernel might zero the iovec::iov_base in case of invalid
    // write access.  In this case copy the invalid address for further
    // inspection.
    else if (request == ptrace_setregset || request == ptrace_getregset) {
      __sanitizer_iovec *iovec = (__sanitizer_iovec*)data;
      COMMON_INTERCEPTOR_READ_RANGE(ctx, iovec, sizeof(*iovec));
      local_iovec = *iovec;
      if (request == ptrace_setregset)
        COMMON_INTERCEPTOR_READ_RANGE(ctx, iovec->iov_base, iovec->iov_len);
    }
  }

  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  uptr res = REAL(ptrace)(request, pid, addr, data);

  if (!res && data) {
    // Note that PEEK* requests assign different meaning to the return value.
    // This function does not handle them (nor does it need to).
    if (request == ptrace_getregs)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, data, struct_user_regs_struct_sz);
    else if (request == ptrace_getfpregs)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, data, struct_user_fpregs_struct_sz);
    else if (request == ptrace_getfpxregs)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, data, struct_user_fpxregs_struct_sz);
    else if (request == ptrace_getvfpregs)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, data, struct_user_vfpregs_struct_sz);
    else if (request == ptrace_getsiginfo)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, data, siginfo_t_sz);
    else if (request == ptrace_geteventmsg)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, data, sizeof(unsigned long));
    else if (request == ptrace_getregset) {
      __sanitizer_iovec *iovec = (__sanitizer_iovec*)data;
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, iovec, sizeof(*iovec));
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, local_iovec.iov_base,
                                     local_iovec.iov_len);
    }
  }
  return res;
}

#define INIT_PTRACE COMMON_INTERCEPT_FUNCTION(ptrace);
#else
#define INIT_PTRACE
#endif

#if SANITIZER_INTERCEPT_SETLOCALE
static void unpoison_ctype_arrays(void *ctx) {
#if SANITIZER_NETBSD
  // These arrays contain 256 regular elements in unsigned char range + 1 EOF
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, _ctype_tab_, 257 * sizeof(short));
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, _toupper_tab_, 257 * sizeof(short));
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, _tolower_tab_, 257 * sizeof(short));
#endif
}

INTERCEPTOR(char *, setlocale, int category, char *locale) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setlocale, category, locale);
  if (locale)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, locale, REAL(strlen)(locale) + 1);
  char *res = REAL(setlocale)(category, locale);
  if (res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
    unpoison_ctype_arrays(ctx);
  }
  return res;
}

#define INIT_SETLOCALE COMMON_INTERCEPT_FUNCTION(setlocale);
#else
#define INIT_SETLOCALE
#endif

#if SANITIZER_INTERCEPT_GETCWD
INTERCEPTOR(char *, getcwd, char *buf, SIZE_T size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getcwd, buf, size);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(getcwd)(buf, size);
  if (res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  return res;
}
#define INIT_GETCWD COMMON_INTERCEPT_FUNCTION(getcwd);
#else
#define INIT_GETCWD
#endif

#if SANITIZER_INTERCEPT_GET_CURRENT_DIR_NAME
INTERCEPTOR(char *, get_current_dir_name, int fake) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, get_current_dir_name, fake);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(get_current_dir_name)(fake);
  if (res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  return res;
}

#define INIT_GET_CURRENT_DIR_NAME \
  COMMON_INTERCEPT_FUNCTION(get_current_dir_name);
#else
#define INIT_GET_CURRENT_DIR_NAME
#endif

UNUSED static inline void FixRealStrtolEndptr(const char *nptr, char **endptr) {
  CHECK(endptr);
  if (nptr == *endptr) {
    // No digits were found at strtol call, we need to find out the last
    // symbol accessed by strtoll on our own.
    // We get this symbol by skipping leading blanks and optional +/- sign.
    while (IsSpace(*nptr)) nptr++;
    if (*nptr == '+' || *nptr == '-') nptr++;
    *endptr = const_cast<char *>(nptr);
  }
  CHECK(*endptr >= nptr);
}

UNUSED static inline void StrtolFixAndCheck(void *ctx, const char *nptr,
                             char **endptr, char *real_endptr, int base) {
  if (endptr) {
    *endptr = real_endptr;
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, endptr, sizeof(*endptr));
  }
  // If base has unsupported value, strtol can exit with EINVAL
  // without reading any characters. So do additional checks only
  // if base is valid.
  bool is_valid_base = (base == 0) || (2 <= base && base <= 36);
  if (is_valid_base) {
    FixRealStrtolEndptr(nptr, &real_endptr);
  }
  COMMON_INTERCEPTOR_READ_STRING(ctx, nptr, is_valid_base ?
                                 (real_endptr - nptr) + 1 : 0);
}


#if SANITIZER_INTERCEPT_STRTOIMAX
INTERCEPTOR(INTMAX_T, strtoimax, const char *nptr, char **endptr, int base) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strtoimax, nptr, endptr, base);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *real_endptr;
  INTMAX_T res = REAL(strtoimax)(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return res;
}

INTERCEPTOR(UINTMAX_T, strtoumax, const char *nptr, char **endptr, int base) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strtoumax, nptr, endptr, base);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *real_endptr;
  UINTMAX_T res = REAL(strtoumax)(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return res;
}

#define INIT_STRTOIMAX                  \
  COMMON_INTERCEPT_FUNCTION(strtoimax); \
  COMMON_INTERCEPT_FUNCTION(strtoumax);
#else
#define INIT_STRTOIMAX
#endif

#if SANITIZER_INTERCEPT_MBSTOWCS
INTERCEPTOR(SIZE_T, mbstowcs, wchar_t *dest, const char *src, SIZE_T len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, mbstowcs, dest, src, len);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(mbstowcs)(dest, src, len);
  if (res != (SIZE_T) - 1 && dest) {
    SIZE_T write_cnt = res + (res < len);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dest, write_cnt * sizeof(wchar_t));
  }
  return res;
}

INTERCEPTOR(SIZE_T, mbsrtowcs, wchar_t *dest, const char **src, SIZE_T len,
            void *ps) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, mbsrtowcs, dest, src, len, ps);
  if (src) COMMON_INTERCEPTOR_READ_RANGE(ctx, src, sizeof(*src));
  if (ps) COMMON_INTERCEPTOR_READ_RANGE(ctx, ps, mbstate_t_sz);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(mbsrtowcs)(dest, src, len, ps);
  if (res != (SIZE_T)(-1) && dest && src) {
    // This function, and several others, may or may not write the terminating
    // \0 character. They write it iff they clear *src.
    SIZE_T write_cnt = res + !*src;
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dest, write_cnt * sizeof(wchar_t));
  }
  return res;
}

#define INIT_MBSTOWCS                  \
  COMMON_INTERCEPT_FUNCTION(mbstowcs); \
  COMMON_INTERCEPT_FUNCTION(mbsrtowcs);
#else
#define INIT_MBSTOWCS
#endif

#if SANITIZER_INTERCEPT_MBSNRTOWCS
INTERCEPTOR(SIZE_T, mbsnrtowcs, wchar_t *dest, const char **src, SIZE_T nms,
            SIZE_T len, void *ps) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, mbsnrtowcs, dest, src, nms, len, ps);
  if (src) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, sizeof(*src));
    if (nms) COMMON_INTERCEPTOR_READ_RANGE(ctx, *src, nms);
  }
  if (ps) COMMON_INTERCEPTOR_READ_RANGE(ctx, ps, mbstate_t_sz);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(mbsnrtowcs)(dest, src, nms, len, ps);
  if (res != (SIZE_T)(-1) && dest && src) {
    SIZE_T write_cnt = res + !*src;
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dest, write_cnt * sizeof(wchar_t));
  }
  return res;
}

#define INIT_MBSNRTOWCS COMMON_INTERCEPT_FUNCTION(mbsnrtowcs);
#else
#define INIT_MBSNRTOWCS
#endif

#if SANITIZER_INTERCEPT_WCSTOMBS
INTERCEPTOR(SIZE_T, wcstombs, char *dest, const wchar_t *src, SIZE_T len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wcstombs, dest, src, len);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(wcstombs)(dest, src, len);
  if (res != (SIZE_T) - 1 && dest) {
    SIZE_T write_cnt = res + (res < len);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dest, write_cnt);
  }
  return res;
}

INTERCEPTOR(SIZE_T, wcsrtombs, char *dest, const wchar_t **src, SIZE_T len,
            void *ps) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wcsrtombs, dest, src, len, ps);
  if (src) COMMON_INTERCEPTOR_READ_RANGE(ctx, src, sizeof(*src));
  if (ps) COMMON_INTERCEPTOR_READ_RANGE(ctx, ps, mbstate_t_sz);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(wcsrtombs)(dest, src, len, ps);
  if (res != (SIZE_T) - 1 && dest && src) {
    SIZE_T write_cnt = res + !*src;
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dest, write_cnt);
  }
  return res;
}

#define INIT_WCSTOMBS                  \
  COMMON_INTERCEPT_FUNCTION(wcstombs); \
  COMMON_INTERCEPT_FUNCTION(wcsrtombs);
#else
#define INIT_WCSTOMBS
#endif

#if SANITIZER_INTERCEPT_WCSNRTOMBS
INTERCEPTOR(SIZE_T, wcsnrtombs, char *dest, const wchar_t **src, SIZE_T nms,
            SIZE_T len, void *ps) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wcsnrtombs, dest, src, nms, len, ps);
  if (src) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, sizeof(*src));
    if (nms) COMMON_INTERCEPTOR_READ_RANGE(ctx, *src, nms);
  }
  if (ps) COMMON_INTERCEPTOR_READ_RANGE(ctx, ps, mbstate_t_sz);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(wcsnrtombs)(dest, src, nms, len, ps);
  if (res != ((SIZE_T)-1) && dest && src) {
    SIZE_T write_cnt = res + !*src;
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dest, write_cnt);
  }
  return res;
}

#define INIT_WCSNRTOMBS COMMON_INTERCEPT_FUNCTION(wcsnrtombs);
#else
#define INIT_WCSNRTOMBS
#endif


#if SANITIZER_INTERCEPT_WCRTOMB
INTERCEPTOR(SIZE_T, wcrtomb, char *dest, wchar_t src, void *ps) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wcrtomb, dest, src, ps);
  if (ps) COMMON_INTERCEPTOR_READ_RANGE(ctx, ps, mbstate_t_sz);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(wcrtomb)(dest, src, ps);
  if (res != ((SIZE_T)-1) && dest) {
    SIZE_T write_cnt = res;
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dest, write_cnt);
  }
  return res;
}

#define INIT_WCRTOMB COMMON_INTERCEPT_FUNCTION(wcrtomb);
#else
#define INIT_WCRTOMB
#endif

#if SANITIZER_INTERCEPT_TCGETATTR
INTERCEPTOR(int, tcgetattr, int fd, void *termios_p) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, tcgetattr, fd, termios_p);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(tcgetattr)(fd, termios_p);
  if (!res && termios_p)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, termios_p, struct_termios_sz);
  return res;
}

#define INIT_TCGETATTR COMMON_INTERCEPT_FUNCTION(tcgetattr);
#else
#define INIT_TCGETATTR
#endif

#if SANITIZER_INTERCEPT_REALPATH
INTERCEPTOR(char *, realpath, const char *path, char *resolved_path) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, realpath, path, resolved_path);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);

  // Workaround a bug in glibc where dlsym(RTLD_NEXT, ...) returns the oldest
  // version of a versioned symbol. For realpath(), this gives us something
  // (called __old_realpath) that does not handle NULL in the second argument.
  // Handle it as part of the interceptor.
  char *allocated_path = nullptr;
  if (!resolved_path)
    allocated_path = resolved_path = (char *)WRAP(malloc)(path_max + 1);

  char *res = REAL(realpath)(path, resolved_path);
  if (allocated_path && !res) WRAP(free)(allocated_path);
  if (res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  return res;
}
#define INIT_REALPATH COMMON_INTERCEPT_FUNCTION(realpath);
#else
#define INIT_REALPATH
#endif

#if SANITIZER_INTERCEPT_CANONICALIZE_FILE_NAME
INTERCEPTOR(char *, canonicalize_file_name, const char *path) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, canonicalize_file_name, path);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  char *res = REAL(canonicalize_file_name)(path);
  if (res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  return res;
}
#define INIT_CANONICALIZE_FILE_NAME \
  COMMON_INTERCEPT_FUNCTION(canonicalize_file_name);
#else
#define INIT_CANONICALIZE_FILE_NAME
#endif

#if SANITIZER_INTERCEPT_CONFSTR
INTERCEPTOR(SIZE_T, confstr, int name, char *buf, SIZE_T len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, confstr, name, buf, len);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(confstr)(name, buf, len);
  if (buf && res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, res < len ? res : len);
  return res;
}
#define INIT_CONFSTR COMMON_INTERCEPT_FUNCTION(confstr);
#else
#define INIT_CONFSTR
#endif

#if SANITIZER_INTERCEPT_SCHED_GETAFFINITY
INTERCEPTOR(int, sched_getaffinity, int pid, SIZE_T cpusetsize, void *mask) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sched_getaffinity, pid, cpusetsize, mask);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(sched_getaffinity)(pid, cpusetsize, mask);
  if (mask && !res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, mask, cpusetsize);
  return res;
}
#define INIT_SCHED_GETAFFINITY COMMON_INTERCEPT_FUNCTION(sched_getaffinity);
#else
#define INIT_SCHED_GETAFFINITY
#endif

#if SANITIZER_INTERCEPT_SCHED_GETPARAM
INTERCEPTOR(int, sched_getparam, int pid, void *param) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sched_getparam, pid, param);
  int res = REAL(sched_getparam)(pid, param);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, param, struct_sched_param_sz);
  return res;
}
#define INIT_SCHED_GETPARAM COMMON_INTERCEPT_FUNCTION(sched_getparam);
#else
#define INIT_SCHED_GETPARAM
#endif

#if SANITIZER_INTERCEPT_STRERROR
INTERCEPTOR(char *, strerror, int errnum) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strerror, errnum);
  char *res = REAL(strerror)(errnum);
  if (res) COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, REAL(strlen)(res) + 1);
  return res;
}
#define INIT_STRERROR COMMON_INTERCEPT_FUNCTION(strerror);
#else
#define INIT_STRERROR
#endif

#if SANITIZER_INTERCEPT_STRERROR_R
// There are 2 versions of strerror_r:
//  * POSIX version returns 0 on success, negative error code on failure,
//    writes message to buf.
//  * GNU version returns message pointer, which points to either buf or some
//    static storage.
#if ((_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !_GNU_SOURCE) || \
    SANITIZER_MAC || SANITIZER_ANDROID || SANITIZER_NETBSD ||                 \
    SANITIZER_FREEBSD || SANITIZER_OPENBSD
// POSIX version. Spec is not clear on whether buf is NULL-terminated.
// At least on OSX, buf contents are valid even when the call fails.
INTERCEPTOR(int, strerror_r, int errnum, char *buf, SIZE_T buflen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strerror_r, errnum, buf, buflen);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(strerror_r)(errnum, buf, buflen);

  SIZE_T sz = internal_strnlen(buf, buflen);
  if (sz < buflen) ++sz;
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, sz);
  return res;
}
#else
// GNU version.
INTERCEPTOR(char *, strerror_r, int errnum, char *buf, SIZE_T buflen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strerror_r, errnum, buf, buflen);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(strerror_r)(errnum, buf, buflen);
  if (res == buf)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  else
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, REAL(strlen)(res) + 1);
  return res;
}
#endif //(_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !_GNU_SOURCE ||
       //SANITIZER_MAC
#define INIT_STRERROR_R COMMON_INTERCEPT_FUNCTION(strerror_r);
#else
#define INIT_STRERROR_R
#endif

#if SANITIZER_INTERCEPT_XPG_STRERROR_R
INTERCEPTOR(int, __xpg_strerror_r, int errnum, char *buf, SIZE_T buflen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __xpg_strerror_r, errnum, buf, buflen);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(__xpg_strerror_r)(errnum, buf, buflen);
  // This version always returns a null-terminated string.
  if (buf && buflen)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, REAL(strlen)(buf) + 1);
  return res;
}
#define INIT_XPG_STRERROR_R COMMON_INTERCEPT_FUNCTION(__xpg_strerror_r);
#else
#define INIT_XPG_STRERROR_R
#endif

#if SANITIZER_INTERCEPT_SCANDIR
typedef int (*scandir_filter_f)(const struct __sanitizer_dirent *);
typedef int (*scandir_compar_f)(const struct __sanitizer_dirent **,
                                const struct __sanitizer_dirent **);

static THREADLOCAL scandir_filter_f scandir_filter;
static THREADLOCAL scandir_compar_f scandir_compar;

static int wrapped_scandir_filter(const struct __sanitizer_dirent *dir) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(1);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(dir, dir->d_reclen);
  return scandir_filter(dir);
}

static int wrapped_scandir_compar(const struct __sanitizer_dirent **a,
                                  const struct __sanitizer_dirent **b) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(2);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(a, sizeof(*a));
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(*a, (*a)->d_reclen);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(b, sizeof(*b));
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(*b, (*b)->d_reclen);
  return scandir_compar(a, b);
}

INTERCEPTOR(int, scandir, char *dirp, __sanitizer_dirent ***namelist,
            scandir_filter_f filter, scandir_compar_f compar) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, scandir, dirp, namelist, filter, compar);
  if (dirp) COMMON_INTERCEPTOR_READ_RANGE(ctx, dirp, REAL(strlen)(dirp) + 1);
  scandir_filter = filter;
  scandir_compar = compar;
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(scandir)(dirp, namelist,
                          filter ? wrapped_scandir_filter : nullptr,
                          compar ? wrapped_scandir_compar : nullptr);
  scandir_filter = nullptr;
  scandir_compar = nullptr;
  if (namelist && res > 0) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, namelist, sizeof(*namelist));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *namelist, sizeof(**namelist) * res);
    for (int i = 0; i < res; ++i)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, (*namelist)[i],
                                     (*namelist)[i]->d_reclen);
  }
  return res;
}
#define INIT_SCANDIR COMMON_INTERCEPT_FUNCTION(scandir);
#else
#define INIT_SCANDIR
#endif

#if SANITIZER_INTERCEPT_SCANDIR64
typedef int (*scandir64_filter_f)(const struct __sanitizer_dirent64 *);
typedef int (*scandir64_compar_f)(const struct __sanitizer_dirent64 **,
                                  const struct __sanitizer_dirent64 **);

static THREADLOCAL scandir64_filter_f scandir64_filter;
static THREADLOCAL scandir64_compar_f scandir64_compar;

static int wrapped_scandir64_filter(const struct __sanitizer_dirent64 *dir) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(1);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(dir, dir->d_reclen);
  return scandir64_filter(dir);
}

static int wrapped_scandir64_compar(const struct __sanitizer_dirent64 **a,
                                    const struct __sanitizer_dirent64 **b) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(2);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(a, sizeof(*a));
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(*a, (*a)->d_reclen);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(b, sizeof(*b));
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(*b, (*b)->d_reclen);
  return scandir64_compar(a, b);
}

INTERCEPTOR(int, scandir64, char *dirp, __sanitizer_dirent64 ***namelist,
            scandir64_filter_f filter, scandir64_compar_f compar) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, scandir64, dirp, namelist, filter, compar);
  if (dirp) COMMON_INTERCEPTOR_READ_RANGE(ctx, dirp, REAL(strlen)(dirp) + 1);
  scandir64_filter = filter;
  scandir64_compar = compar;
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res =
      REAL(scandir64)(dirp, namelist,
                      filter ? wrapped_scandir64_filter : nullptr,
                      compar ? wrapped_scandir64_compar : nullptr);
  scandir64_filter = nullptr;
  scandir64_compar = nullptr;
  if (namelist && res > 0) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, namelist, sizeof(*namelist));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *namelist, sizeof(**namelist) * res);
    for (int i = 0; i < res; ++i)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, (*namelist)[i],
                                     (*namelist)[i]->d_reclen);
  }
  return res;
}
#define INIT_SCANDIR64 COMMON_INTERCEPT_FUNCTION(scandir64);
#else
#define INIT_SCANDIR64
#endif

#if SANITIZER_INTERCEPT_GETGROUPS
INTERCEPTOR(int, getgroups, int size, u32 *lst) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgroups, size, lst);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getgroups)(size, lst);
  if (res >= 0 && lst && size > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, lst, res * sizeof(*lst));
  return res;
}
#define INIT_GETGROUPS COMMON_INTERCEPT_FUNCTION(getgroups);
#else
#define INIT_GETGROUPS
#endif

#if SANITIZER_INTERCEPT_POLL
static void read_pollfd(void *ctx, __sanitizer_pollfd *fds,
                        __sanitizer_nfds_t nfds) {
  for (unsigned i = 0; i < nfds; ++i) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, &fds[i].fd, sizeof(fds[i].fd));
    COMMON_INTERCEPTOR_READ_RANGE(ctx, &fds[i].events, sizeof(fds[i].events));
  }
}

static void write_pollfd(void *ctx, __sanitizer_pollfd *fds,
                         __sanitizer_nfds_t nfds) {
  for (unsigned i = 0; i < nfds; ++i)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, &fds[i].revents,
                                   sizeof(fds[i].revents));
}

INTERCEPTOR(int, poll, __sanitizer_pollfd *fds, __sanitizer_nfds_t nfds,
            int timeout) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, poll, fds, nfds, timeout);
  if (fds && nfds) read_pollfd(ctx, fds, nfds);
  int res = COMMON_INTERCEPTOR_BLOCK_REAL(poll)(fds, nfds, timeout);
  if (fds && nfds) write_pollfd(ctx, fds, nfds);
  return res;
}
#define INIT_POLL COMMON_INTERCEPT_FUNCTION(poll);
#else
#define INIT_POLL
#endif

#if SANITIZER_INTERCEPT_PPOLL
INTERCEPTOR(int, ppoll, __sanitizer_pollfd *fds, __sanitizer_nfds_t nfds,
            void *timeout_ts, __sanitizer_sigset_t *sigmask) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ppoll, fds, nfds, timeout_ts, sigmask);
  if (fds && nfds) read_pollfd(ctx, fds, nfds);
  if (timeout_ts)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timeout_ts, struct_timespec_sz);
  if (sigmask) COMMON_INTERCEPTOR_READ_RANGE(ctx, sigmask, sizeof(*sigmask));
  int res =
      COMMON_INTERCEPTOR_BLOCK_REAL(ppoll)(fds, nfds, timeout_ts, sigmask);
  if (fds && nfds) write_pollfd(ctx, fds, nfds);
  return res;
}
#define INIT_PPOLL COMMON_INTERCEPT_FUNCTION(ppoll);
#else
#define INIT_PPOLL
#endif

#if SANITIZER_INTERCEPT_WORDEXP
INTERCEPTOR(int, wordexp, char *s, __sanitizer_wordexp_t *p, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wordexp, s, p, flags);
  if (s) COMMON_INTERCEPTOR_READ_RANGE(ctx, s, REAL(strlen)(s) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(wordexp)(s, p, flags);
  if (!res && p) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, sizeof(*p));
    if (p->we_wordc)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->we_wordv,
                                     sizeof(*p->we_wordv) * p->we_wordc);
    for (uptr i = 0; i < p->we_wordc; ++i) {
      char *w = p->we_wordv[i];
      if (w) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, w, REAL(strlen)(w) + 1);
    }
  }
  return res;
}
#define INIT_WORDEXP COMMON_INTERCEPT_FUNCTION(wordexp);
#else
#define INIT_WORDEXP
#endif

#if SANITIZER_INTERCEPT_SIGWAIT
INTERCEPTOR(int, sigwait, __sanitizer_sigset_t *set, int *sig) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sigwait, set, sig);
  if (set) COMMON_INTERCEPTOR_READ_RANGE(ctx, set, sizeof(*set));
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(sigwait)(set, sig);
  if (!res && sig) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sig, sizeof(*sig));
  return res;
}
#define INIT_SIGWAIT COMMON_INTERCEPT_FUNCTION(sigwait);
#else
#define INIT_SIGWAIT
#endif

#if SANITIZER_INTERCEPT_SIGWAITINFO
INTERCEPTOR(int, sigwaitinfo, __sanitizer_sigset_t *set, void *info) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sigwaitinfo, set, info);
  if (set) COMMON_INTERCEPTOR_READ_RANGE(ctx, set, sizeof(*set));
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(sigwaitinfo)(set, info);
  if (res > 0 && info) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, info, siginfo_t_sz);
  return res;
}
#define INIT_SIGWAITINFO COMMON_INTERCEPT_FUNCTION(sigwaitinfo);
#else
#define INIT_SIGWAITINFO
#endif

#if SANITIZER_INTERCEPT_SIGTIMEDWAIT
INTERCEPTOR(int, sigtimedwait, __sanitizer_sigset_t *set, void *info,
            void *timeout) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sigtimedwait, set, info, timeout);
  if (timeout) COMMON_INTERCEPTOR_READ_RANGE(ctx, timeout, struct_timespec_sz);
  if (set) COMMON_INTERCEPTOR_READ_RANGE(ctx, set, sizeof(*set));
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(sigtimedwait)(set, info, timeout);
  if (res > 0 && info) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, info, siginfo_t_sz);
  return res;
}
#define INIT_SIGTIMEDWAIT COMMON_INTERCEPT_FUNCTION(sigtimedwait);
#else
#define INIT_SIGTIMEDWAIT
#endif

#if SANITIZER_INTERCEPT_SIGSETOPS
INTERCEPTOR(int, sigemptyset, __sanitizer_sigset_t *set) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sigemptyset, set);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(sigemptyset)(set);
  if (!res && set) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, set, sizeof(*set));
  return res;
}

INTERCEPTOR(int, sigfillset, __sanitizer_sigset_t *set) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sigfillset, set);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(sigfillset)(set);
  if (!res && set) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, set, sizeof(*set));
  return res;
}
#define INIT_SIGSETOPS                    \
  COMMON_INTERCEPT_FUNCTION(sigemptyset); \
  COMMON_INTERCEPT_FUNCTION(sigfillset);
#else
#define INIT_SIGSETOPS
#endif

#if SANITIZER_INTERCEPT_SIGPENDING
INTERCEPTOR(int, sigpending, __sanitizer_sigset_t *set) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sigpending, set);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(sigpending)(set);
  if (!res && set) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, set, sizeof(*set));
  return res;
}
#define INIT_SIGPENDING COMMON_INTERCEPT_FUNCTION(sigpending);
#else
#define INIT_SIGPENDING
#endif

#if SANITIZER_INTERCEPT_SIGPROCMASK
INTERCEPTOR(int, sigprocmask, int how, __sanitizer_sigset_t *set,
            __sanitizer_sigset_t *oldset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sigprocmask, how, set, oldset);
  if (set) COMMON_INTERCEPTOR_READ_RANGE(ctx, set, sizeof(*set));
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(sigprocmask)(how, set, oldset);
  if (!res && oldset)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, oldset, sizeof(*oldset));
  return res;
}
#define INIT_SIGPROCMASK COMMON_INTERCEPT_FUNCTION(sigprocmask);
#else
#define INIT_SIGPROCMASK
#endif

#if SANITIZER_INTERCEPT_BACKTRACE
INTERCEPTOR(int, backtrace, void **buffer, int size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, backtrace, buffer, size);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(backtrace)(buffer, size);
  if (res && buffer)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buffer, res * sizeof(*buffer));
  return res;
}

INTERCEPTOR(char **, backtrace_symbols, void **buffer, int size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, backtrace_symbols, buffer, size);
  if (buffer && size)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, buffer, size * sizeof(*buffer));
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char **res = REAL(backtrace_symbols)(buffer, size);
  if (res && size) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, size * sizeof(*res));
    for (int i = 0; i < size; ++i)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res[i], REAL(strlen(res[i])) + 1);
  }
  return res;
}
#define INIT_BACKTRACE                  \
  COMMON_INTERCEPT_FUNCTION(backtrace); \
  COMMON_INTERCEPT_FUNCTION(backtrace_symbols);
#else
#define INIT_BACKTRACE
#endif

#if SANITIZER_INTERCEPT__EXIT
INTERCEPTOR(void, _exit, int status) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, _exit, status);
  COMMON_INTERCEPTOR_USER_CALLBACK_START();
  int status1 = COMMON_INTERCEPTOR_ON_EXIT(ctx);
  COMMON_INTERCEPTOR_USER_CALLBACK_END();
  if (status == 0) status = status1;
  REAL(_exit)(status);
}
#define INIT__EXIT COMMON_INTERCEPT_FUNCTION(_exit);
#else
#define INIT__EXIT
#endif

#if SANITIZER_INTERCEPT_PTHREAD_MUTEX
INTERCEPTOR(int, pthread_mutex_lock, void *m) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_mutex_lock, m);
  COMMON_INTERCEPTOR_MUTEX_PRE_LOCK(ctx, m);
  int res = REAL(pthread_mutex_lock)(m);
  if (res == errno_EOWNERDEAD)
    COMMON_INTERCEPTOR_MUTEX_REPAIR(ctx, m);
  if (res == 0 || res == errno_EOWNERDEAD)
    COMMON_INTERCEPTOR_MUTEX_POST_LOCK(ctx, m);
  if (res == errno_EINVAL)
    COMMON_INTERCEPTOR_MUTEX_INVALID(ctx, m);
  return res;
}

INTERCEPTOR(int, pthread_mutex_unlock, void *m) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_mutex_unlock, m);
  COMMON_INTERCEPTOR_MUTEX_UNLOCK(ctx, m);
  int res = REAL(pthread_mutex_unlock)(m);
  if (res == errno_EINVAL)
    COMMON_INTERCEPTOR_MUTEX_INVALID(ctx, m);
  return res;
}

#define INIT_PTHREAD_MUTEX_LOCK COMMON_INTERCEPT_FUNCTION(pthread_mutex_lock)
#define INIT_PTHREAD_MUTEX_UNLOCK \
  COMMON_INTERCEPT_FUNCTION(pthread_mutex_unlock)
#else
#define INIT_PTHREAD_MUTEX_LOCK
#define INIT_PTHREAD_MUTEX_UNLOCK
#endif

#if SANITIZER_INTERCEPT___PTHREAD_MUTEX
INTERCEPTOR(int, __pthread_mutex_lock, void *m) {
  return WRAP(pthread_mutex_lock)(m);
}

INTERCEPTOR(int, __pthread_mutex_unlock, void *m) {
  return WRAP(pthread_mutex_unlock)(m);
}

#define INIT___PTHREAD_MUTEX_LOCK \
  COMMON_INTERCEPT_FUNCTION(__pthread_mutex_lock)
#define INIT___PTHREAD_MUTEX_UNLOCK \
  COMMON_INTERCEPT_FUNCTION(__pthread_mutex_unlock)
#else
#define INIT___PTHREAD_MUTEX_LOCK
#define INIT___PTHREAD_MUTEX_UNLOCK
#endif

#if SANITIZER_INTERCEPT___LIBC_MUTEX
INTERCEPTOR(int, __libc_mutex_lock, void *m)
ALIAS(WRAPPER_NAME(pthread_mutex_lock));

INTERCEPTOR(int, __libc_mutex_unlock, void *m)
ALIAS(WRAPPER_NAME(pthread_mutex_unlock));

INTERCEPTOR(int, __libc_thr_setcancelstate, int state, int *oldstate)
ALIAS(WRAPPER_NAME(pthread_setcancelstate));

#define INIT___LIBC_MUTEX_LOCK COMMON_INTERCEPT_FUNCTION(__libc_mutex_lock)
#define INIT___LIBC_MUTEX_UNLOCK COMMON_INTERCEPT_FUNCTION(__libc_mutex_unlock)
#define INIT___LIBC_THR_SETCANCELSTATE \
  COMMON_INTERCEPT_FUNCTION(__libc_thr_setcancelstate)
#else
#define INIT___LIBC_MUTEX_LOCK
#define INIT___LIBC_MUTEX_UNLOCK
#define INIT___LIBC_THR_SETCANCELSTATE
#endif

#if SANITIZER_INTERCEPT_GETMNTENT || SANITIZER_INTERCEPT_GETMNTENT_R
static void write_mntent(void *ctx, __sanitizer_mntent *mnt) {
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, mnt, sizeof(*mnt));
  if (mnt->mnt_fsname)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, mnt->mnt_fsname,
                                   REAL(strlen)(mnt->mnt_fsname) + 1);
  if (mnt->mnt_dir)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, mnt->mnt_dir,
                                   REAL(strlen)(mnt->mnt_dir) + 1);
  if (mnt->mnt_type)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, mnt->mnt_type,
                                   REAL(strlen)(mnt->mnt_type) + 1);
  if (mnt->mnt_opts)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, mnt->mnt_opts,
                                   REAL(strlen)(mnt->mnt_opts) + 1);
}
#endif

#if SANITIZER_INTERCEPT_GETMNTENT
INTERCEPTOR(__sanitizer_mntent *, getmntent, void *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getmntent, fp);
  __sanitizer_mntent *res = REAL(getmntent)(fp);
  if (res) write_mntent(ctx, res);
  return res;
}
#define INIT_GETMNTENT COMMON_INTERCEPT_FUNCTION(getmntent);
#else
#define INIT_GETMNTENT
#endif

#if SANITIZER_INTERCEPT_GETMNTENT_R
INTERCEPTOR(__sanitizer_mntent *, getmntent_r, void *fp,
            __sanitizer_mntent *mntbuf, char *buf, int buflen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getmntent_r, fp, mntbuf, buf, buflen);
  __sanitizer_mntent *res = REAL(getmntent_r)(fp, mntbuf, buf, buflen);
  if (res) write_mntent(ctx, res);
  return res;
}
#define INIT_GETMNTENT_R COMMON_INTERCEPT_FUNCTION(getmntent_r);
#else
#define INIT_GETMNTENT_R
#endif

#if SANITIZER_INTERCEPT_STATFS
INTERCEPTOR(int, statfs, char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, statfs, path, buf);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(statfs)(path, buf);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statfs_sz);
  return res;
}
INTERCEPTOR(int, fstatfs, int fd, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fstatfs, fd, buf);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(fstatfs)(fd, buf);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statfs_sz);
  return res;
}
#define INIT_STATFS                  \
  COMMON_INTERCEPT_FUNCTION(statfs); \
  COMMON_INTERCEPT_FUNCTION(fstatfs);
#else
#define INIT_STATFS
#endif

#if SANITIZER_INTERCEPT_STATFS64
INTERCEPTOR(int, statfs64, char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, statfs64, path, buf);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(statfs64)(path, buf);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statfs64_sz);
  return res;
}
INTERCEPTOR(int, fstatfs64, int fd, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fstatfs64, fd, buf);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(fstatfs64)(fd, buf);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statfs64_sz);
  return res;
}
#define INIT_STATFS64                  \
  COMMON_INTERCEPT_FUNCTION(statfs64); \
  COMMON_INTERCEPT_FUNCTION(fstatfs64);
#else
#define INIT_STATFS64
#endif

#if SANITIZER_INTERCEPT_STATVFS
INTERCEPTOR(int, statvfs, char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, statvfs, path, buf);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(statvfs)(path, buf);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs_sz);
  return res;
}
INTERCEPTOR(int, fstatvfs, int fd, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fstatvfs, fd, buf);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(fstatvfs)(fd, buf);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs_sz);
    if (fd >= 0)
      COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  }
  return res;
}
#define INIT_STATVFS                  \
  COMMON_INTERCEPT_FUNCTION(statvfs); \
  COMMON_INTERCEPT_FUNCTION(fstatvfs);
#else
#define INIT_STATVFS
#endif

#if SANITIZER_INTERCEPT_STATVFS64
INTERCEPTOR(int, statvfs64, char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, statvfs64, path, buf);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(statvfs64)(path, buf);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs64_sz);
  return res;
}
INTERCEPTOR(int, fstatvfs64, int fd, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fstatvfs64, fd, buf);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(fstatvfs64)(fd, buf);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs64_sz);
  return res;
}
#define INIT_STATVFS64                  \
  COMMON_INTERCEPT_FUNCTION(statvfs64); \
  COMMON_INTERCEPT_FUNCTION(fstatvfs64);
#else
#define INIT_STATVFS64
#endif

#if SANITIZER_INTERCEPT_INITGROUPS
INTERCEPTOR(int, initgroups, char *user, u32 group) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, initgroups, user, group);
  if (user) COMMON_INTERCEPTOR_READ_RANGE(ctx, user, REAL(strlen)(user) + 1);
  int res = REAL(initgroups)(user, group);
  return res;
}
#define INIT_INITGROUPS COMMON_INTERCEPT_FUNCTION(initgroups);
#else
#define INIT_INITGROUPS
#endif

#if SANITIZER_INTERCEPT_ETHER_NTOA_ATON
INTERCEPTOR(char *, ether_ntoa, __sanitizer_ether_addr *addr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ether_ntoa, addr);
  if (addr) COMMON_INTERCEPTOR_READ_RANGE(ctx, addr, sizeof(*addr));
  char *res = REAL(ether_ntoa)(addr);
  if (res) COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, REAL(strlen)(res) + 1);
  return res;
}
INTERCEPTOR(__sanitizer_ether_addr *, ether_aton, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ether_aton, buf);
  if (buf) COMMON_INTERCEPTOR_READ_RANGE(ctx, buf, REAL(strlen)(buf) + 1);
  __sanitizer_ether_addr *res = REAL(ether_aton)(buf);
  if (res) COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, sizeof(*res));
  return res;
}
#define INIT_ETHER_NTOA_ATON             \
  COMMON_INTERCEPT_FUNCTION(ether_ntoa); \
  COMMON_INTERCEPT_FUNCTION(ether_aton);
#else
#define INIT_ETHER_NTOA_ATON
#endif

#if SANITIZER_INTERCEPT_ETHER_HOST
INTERCEPTOR(int, ether_ntohost, char *hostname, __sanitizer_ether_addr *addr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ether_ntohost, hostname, addr);
  if (addr) COMMON_INTERCEPTOR_READ_RANGE(ctx, addr, sizeof(*addr));
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(ether_ntohost)(hostname, addr);
  if (!res && hostname)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, hostname, REAL(strlen)(hostname) + 1);
  return res;
}
INTERCEPTOR(int, ether_hostton, char *hostname, __sanitizer_ether_addr *addr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ether_hostton, hostname, addr);
  if (hostname)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, hostname, REAL(strlen)(hostname) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(ether_hostton)(hostname, addr);
  if (!res && addr) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, addr, sizeof(*addr));
  return res;
}
INTERCEPTOR(int, ether_line, char *line, __sanitizer_ether_addr *addr,
            char *hostname) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ether_line, line, addr, hostname);
  if (line) COMMON_INTERCEPTOR_READ_RANGE(ctx, line, REAL(strlen)(line) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(ether_line)(line, addr, hostname);
  if (!res) {
    if (addr) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, addr, sizeof(*addr));
    if (hostname)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, hostname, REAL(strlen)(hostname) + 1);
  }
  return res;
}
#define INIT_ETHER_HOST                     \
  COMMON_INTERCEPT_FUNCTION(ether_ntohost); \
  COMMON_INTERCEPT_FUNCTION(ether_hostton); \
  COMMON_INTERCEPT_FUNCTION(ether_line);
#else
#define INIT_ETHER_HOST
#endif

#if SANITIZER_INTERCEPT_ETHER_R
INTERCEPTOR(char *, ether_ntoa_r, __sanitizer_ether_addr *addr, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ether_ntoa_r, addr, buf);
  if (addr) COMMON_INTERCEPTOR_READ_RANGE(ctx, addr, sizeof(*addr));
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(ether_ntoa_r)(addr, buf);
  if (res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  return res;
}
INTERCEPTOR(__sanitizer_ether_addr *, ether_aton_r, char *buf,
            __sanitizer_ether_addr *addr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ether_aton_r, buf, addr);
  if (buf) COMMON_INTERCEPTOR_READ_RANGE(ctx, buf, REAL(strlen)(buf) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  __sanitizer_ether_addr *res = REAL(ether_aton_r)(buf, addr);
  if (res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, sizeof(*res));
  return res;
}
#define INIT_ETHER_R                       \
  COMMON_INTERCEPT_FUNCTION(ether_ntoa_r); \
  COMMON_INTERCEPT_FUNCTION(ether_aton_r);
#else
#define INIT_ETHER_R
#endif

#if SANITIZER_INTERCEPT_SHMCTL
INTERCEPTOR(int, shmctl, int shmid, int cmd, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, shmctl, shmid, cmd, buf);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(shmctl)(shmid, cmd, buf);
  if (res >= 0) {
    unsigned sz = 0;
    if (cmd == shmctl_ipc_stat || cmd == shmctl_shm_stat)
      sz = sizeof(__sanitizer_shmid_ds);
    else if (cmd == shmctl_ipc_info)
      sz = struct_shminfo_sz;
    else if (cmd == shmctl_shm_info)
      sz = struct_shm_info_sz;
    if (sz) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, sz);
  }
  return res;
}
#define INIT_SHMCTL COMMON_INTERCEPT_FUNCTION(shmctl);
#else
#define INIT_SHMCTL
#endif

#if SANITIZER_INTERCEPT_RANDOM_R
INTERCEPTOR(int, random_r, void *buf, u32 *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, random_r, buf, result);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(random_r)(buf, result);
  if (!res && result)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
  return res;
}
#define INIT_RANDOM_R COMMON_INTERCEPT_FUNCTION(random_r);
#else
#define INIT_RANDOM_R
#endif

// FIXME: under ASan the REAL() call below may write to freed memory and corrupt
// its metadata. See
// https://github.com/google/sanitizers/issues/321.
#if SANITIZER_INTERCEPT_PTHREAD_ATTR_GET ||              \
    SANITIZER_INTERCEPT_PTHREAD_ATTR_GET_SCHED ||        \
    SANITIZER_INTERCEPT_PTHREAD_ATTR_GETINHERITSSCHED || \
    SANITIZER_INTERCEPT_PTHREAD_MUTEXATTR_GET ||         \
    SANITIZER_INTERCEPT_PTHREAD_RWLOCKATTR_GET ||        \
    SANITIZER_INTERCEPT_PTHREAD_CONDATTR_GET ||          \
    SANITIZER_INTERCEPT_PTHREAD_BARRIERATTR_GET
#define INTERCEPTOR_PTHREAD_OBJECT_ATTR_GET(fn, sz)            \
  INTERCEPTOR(int, fn, void *attr, void *r) {                  \
    void *ctx;                                                 \
    COMMON_INTERCEPTOR_ENTER(ctx, fn, attr, r);                \
    int res = REAL(fn)(attr, r);                               \
    if (!res && r) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, r, sz); \
    return res;                                                \
  }
#define INTERCEPTOR_PTHREAD_ATTR_GET(what, sz) \
  INTERCEPTOR_PTHREAD_OBJECT_ATTR_GET(pthread_attr_get##what, sz)
#define INTERCEPTOR_PTHREAD_MUTEXATTR_GET(what, sz) \
  INTERCEPTOR_PTHREAD_OBJECT_ATTR_GET(pthread_mutexattr_get##what, sz)
#define INTERCEPTOR_PTHREAD_RWLOCKATTR_GET(what, sz) \
  INTERCEPTOR_PTHREAD_OBJECT_ATTR_GET(pthread_rwlockattr_get##what, sz)
#define INTERCEPTOR_PTHREAD_CONDATTR_GET(what, sz) \
  INTERCEPTOR_PTHREAD_OBJECT_ATTR_GET(pthread_condattr_get##what, sz)
#define INTERCEPTOR_PTHREAD_BARRIERATTR_GET(what, sz) \
  INTERCEPTOR_PTHREAD_OBJECT_ATTR_GET(pthread_barrierattr_get##what, sz)
#endif

#if SANITIZER_INTERCEPT_PTHREAD_ATTR_GET
INTERCEPTOR_PTHREAD_ATTR_GET(detachstate, sizeof(int))
INTERCEPTOR_PTHREAD_ATTR_GET(guardsize, sizeof(SIZE_T))
INTERCEPTOR_PTHREAD_ATTR_GET(scope, sizeof(int))
INTERCEPTOR_PTHREAD_ATTR_GET(stacksize, sizeof(SIZE_T))
INTERCEPTOR(int, pthread_attr_getstack, void *attr, void **addr, SIZE_T *size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_attr_getstack, attr, addr, size);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(pthread_attr_getstack)(attr, addr, size);
  if (!res) {
    if (addr) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, addr, sizeof(*addr));
    if (size) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, size, sizeof(*size));
  }
  return res;
}

// We may need to call the real pthread_attr_getstack from the run-time
// in sanitizer_common, but we don't want to include the interception headers
// there. So, just define this function here.
namespace __sanitizer {
extern "C" {
int real_pthread_attr_getstack(void *attr, void **addr, SIZE_T *size) {
  return REAL(pthread_attr_getstack)(attr, addr, size);
}
}  // extern "C"
}  // namespace __sanitizer

#define INIT_PTHREAD_ATTR_GET                             \
  COMMON_INTERCEPT_FUNCTION(pthread_attr_getdetachstate); \
  COMMON_INTERCEPT_FUNCTION(pthread_attr_getguardsize);   \
  COMMON_INTERCEPT_FUNCTION(pthread_attr_getscope);       \
  COMMON_INTERCEPT_FUNCTION(pthread_attr_getstacksize);   \
  COMMON_INTERCEPT_FUNCTION(pthread_attr_getstack);
#else
#define INIT_PTHREAD_ATTR_GET
#endif

#if SANITIZER_INTERCEPT_PTHREAD_ATTR_GET_SCHED
INTERCEPTOR_PTHREAD_ATTR_GET(schedparam, struct_sched_param_sz)
INTERCEPTOR_PTHREAD_ATTR_GET(schedpolicy, sizeof(int))

#define INIT_PTHREAD_ATTR_GET_SCHED                      \
  COMMON_INTERCEPT_FUNCTION(pthread_attr_getschedparam); \
  COMMON_INTERCEPT_FUNCTION(pthread_attr_getschedpolicy);
#else
#define INIT_PTHREAD_ATTR_GET_SCHED
#endif

#if SANITIZER_INTERCEPT_PTHREAD_ATTR_GETINHERITSCHED
INTERCEPTOR_PTHREAD_ATTR_GET(inheritsched, sizeof(int))

#define INIT_PTHREAD_ATTR_GETINHERITSCHED \
  COMMON_INTERCEPT_FUNCTION(pthread_attr_getinheritsched);
#else
#define INIT_PTHREAD_ATTR_GETINHERITSCHED
#endif

#if SANITIZER_INTERCEPT_PTHREAD_ATTR_GETAFFINITY_NP
INTERCEPTOR(int, pthread_attr_getaffinity_np, void *attr, SIZE_T cpusetsize,
            void *cpuset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_attr_getaffinity_np, attr, cpusetsize,
                           cpuset);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(pthread_attr_getaffinity_np)(attr, cpusetsize, cpuset);
  if (!res && cpusetsize && cpuset)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cpuset, cpusetsize);
  return res;
}

#define INIT_PTHREAD_ATTR_GETAFFINITY_NP \
  COMMON_INTERCEPT_FUNCTION(pthread_attr_getaffinity_np);
#else
#define INIT_PTHREAD_ATTR_GETAFFINITY_NP
#endif

#if SANITIZER_INTERCEPT_PTHREAD_MUTEXATTR_GETPSHARED
INTERCEPTOR_PTHREAD_MUTEXATTR_GET(pshared, sizeof(int))
#define INIT_PTHREAD_MUTEXATTR_GETPSHARED \
  COMMON_INTERCEPT_FUNCTION(pthread_mutexattr_getpshared);
#else
#define INIT_PTHREAD_MUTEXATTR_GETPSHARED
#endif

#if SANITIZER_INTERCEPT_PTHREAD_MUTEXATTR_GETTYPE
INTERCEPTOR_PTHREAD_MUTEXATTR_GET(type, sizeof(int))
#define INIT_PTHREAD_MUTEXATTR_GETTYPE \
  COMMON_INTERCEPT_FUNCTION(pthread_mutexattr_gettype);
#else
#define INIT_PTHREAD_MUTEXATTR_GETTYPE
#endif

#if SANITIZER_INTERCEPT_PTHREAD_MUTEXATTR_GETPROTOCOL
INTERCEPTOR_PTHREAD_MUTEXATTR_GET(protocol, sizeof(int))
#define INIT_PTHREAD_MUTEXATTR_GETPROTOCOL \
  COMMON_INTERCEPT_FUNCTION(pthread_mutexattr_getprotocol);
#else
#define INIT_PTHREAD_MUTEXATTR_GETPROTOCOL
#endif

#if SANITIZER_INTERCEPT_PTHREAD_MUTEXATTR_GETPRIOCEILING
INTERCEPTOR_PTHREAD_MUTEXATTR_GET(prioceiling, sizeof(int))
#define INIT_PTHREAD_MUTEXATTR_GETPRIOCEILING \
  COMMON_INTERCEPT_FUNCTION(pthread_mutexattr_getprioceiling);
#else
#define INIT_PTHREAD_MUTEXATTR_GETPRIOCEILING
#endif

#if SANITIZER_INTERCEPT_PTHREAD_MUTEXATTR_GETROBUST
INTERCEPTOR_PTHREAD_MUTEXATTR_GET(robust, sizeof(int))
#define INIT_PTHREAD_MUTEXATTR_GETROBUST \
  COMMON_INTERCEPT_FUNCTION(pthread_mutexattr_getrobust);
#else
#define INIT_PTHREAD_MUTEXATTR_GETROBUST
#endif

#if SANITIZER_INTERCEPT_PTHREAD_MUTEXATTR_GETROBUST_NP
INTERCEPTOR_PTHREAD_MUTEXATTR_GET(robust_np, sizeof(int))
#define INIT_PTHREAD_MUTEXATTR_GETROBUST_NP \
  COMMON_INTERCEPT_FUNCTION(pthread_mutexattr_getrobust_np);
#else
#define INIT_PTHREAD_MUTEXATTR_GETROBUST_NP
#endif

#if SANITIZER_INTERCEPT_PTHREAD_RWLOCKATTR_GETPSHARED
INTERCEPTOR_PTHREAD_RWLOCKATTR_GET(pshared, sizeof(int))
#define INIT_PTHREAD_RWLOCKATTR_GETPSHARED \
  COMMON_INTERCEPT_FUNCTION(pthread_rwlockattr_getpshared);
#else
#define INIT_PTHREAD_RWLOCKATTR_GETPSHARED
#endif

#if SANITIZER_INTERCEPT_PTHREAD_RWLOCKATTR_GETKIND_NP
INTERCEPTOR_PTHREAD_RWLOCKATTR_GET(kind_np, sizeof(int))
#define INIT_PTHREAD_RWLOCKATTR_GETKIND_NP \
  COMMON_INTERCEPT_FUNCTION(pthread_rwlockattr_getkind_np);
#else
#define INIT_PTHREAD_RWLOCKATTR_GETKIND_NP
#endif

#if SANITIZER_INTERCEPT_PTHREAD_CONDATTR_GETPSHARED
INTERCEPTOR_PTHREAD_CONDATTR_GET(pshared, sizeof(int))
#define INIT_PTHREAD_CONDATTR_GETPSHARED \
  COMMON_INTERCEPT_FUNCTION(pthread_condattr_getpshared);
#else
#define INIT_PTHREAD_CONDATTR_GETPSHARED
#endif

#if SANITIZER_INTERCEPT_PTHREAD_CONDATTR_GETCLOCK
INTERCEPTOR_PTHREAD_CONDATTR_GET(clock, sizeof(int))
#define INIT_PTHREAD_CONDATTR_GETCLOCK \
  COMMON_INTERCEPT_FUNCTION(pthread_condattr_getclock);
#else
#define INIT_PTHREAD_CONDATTR_GETCLOCK
#endif

#if SANITIZER_INTERCEPT_PTHREAD_BARRIERATTR_GETPSHARED
INTERCEPTOR_PTHREAD_BARRIERATTR_GET(pshared, sizeof(int)) // !mac !android
#define INIT_PTHREAD_BARRIERATTR_GETPSHARED \
  COMMON_INTERCEPT_FUNCTION(pthread_barrierattr_getpshared);
#else
#define INIT_PTHREAD_BARRIERATTR_GETPSHARED
#endif

#if SANITIZER_INTERCEPT_TMPNAM
INTERCEPTOR(char *, tmpnam, char *s) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, tmpnam, s);
  char *res = REAL(tmpnam)(s);
  if (res) {
    if (s)
      // FIXME: under ASan the call below may write to freed memory and corrupt
      // its metadata. See
      // https://github.com/google/sanitizers/issues/321.
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, s, REAL(strlen)(s) + 1);
    else
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, REAL(strlen)(res) + 1);
  }
  return res;
}
#define INIT_TMPNAM COMMON_INTERCEPT_FUNCTION(tmpnam);
#else
#define INIT_TMPNAM
#endif

#if SANITIZER_INTERCEPT_TMPNAM_R
INTERCEPTOR(char *, tmpnam_r, char *s) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, tmpnam_r, s);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(tmpnam_r)(s);
  if (res && s) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, s, REAL(strlen)(s) + 1);
  return res;
}
#define INIT_TMPNAM_R COMMON_INTERCEPT_FUNCTION(tmpnam_r);
#else
#define INIT_TMPNAM_R
#endif

#if SANITIZER_INTERCEPT_TTYNAME_R
INTERCEPTOR(int, ttyname_r, int fd, char *name, SIZE_T namesize) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ttyname_r, fd, name, namesize);
  int res = REAL(ttyname_r)(fd, name, namesize);
  if (res == 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, name, REAL(strlen)(name) + 1);
  return res;
}
#define INIT_TTYNAME_R COMMON_INTERCEPT_FUNCTION(ttyname_r);
#else
#define INIT_TTYNAME_R
#endif

#if SANITIZER_INTERCEPT_TEMPNAM
INTERCEPTOR(char *, tempnam, char *dir, char *pfx) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, tempnam, dir, pfx);
  if (dir) COMMON_INTERCEPTOR_READ_RANGE(ctx, dir, REAL(strlen)(dir) + 1);
  if (pfx) COMMON_INTERCEPTOR_READ_RANGE(ctx, pfx, REAL(strlen)(pfx) + 1);
  char *res = REAL(tempnam)(dir, pfx);
  if (res) COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, REAL(strlen)(res) + 1);
  return res;
}
#define INIT_TEMPNAM COMMON_INTERCEPT_FUNCTION(tempnam);
#else
#define INIT_TEMPNAM
#endif

#if SANITIZER_INTERCEPT_PTHREAD_SETNAME_NP && !SANITIZER_NETBSD
INTERCEPTOR(int, pthread_setname_np, uptr thread, const char *name) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_setname_np, thread, name);
  COMMON_INTERCEPTOR_READ_STRING(ctx, name, 0);
  COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name);
  return REAL(pthread_setname_np)(thread, name);
}
#define INIT_PTHREAD_SETNAME_NP COMMON_INTERCEPT_FUNCTION(pthread_setname_np);
#elif SANITIZER_INTERCEPT_PTHREAD_SETNAME_NP && SANITIZER_NETBSD
INTERCEPTOR(int, pthread_setname_np, uptr thread, const char *name, void *arg) {
  void *ctx;
  char newname[32]; // PTHREAD_MAX_NAMELEN_NP=32
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_setname_np, thread, name, arg);
  COMMON_INTERCEPTOR_READ_STRING(ctx, name, 0);
  internal_snprintf(newname, sizeof(newname), name, arg);
  COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, newname);
  return REAL(pthread_setname_np)(thread, name, arg);
}
#define INIT_PTHREAD_SETNAME_NP COMMON_INTERCEPT_FUNCTION(pthread_setname_np);
#else
#define INIT_PTHREAD_SETNAME_NP
#endif

#if SANITIZER_INTERCEPT_PTHREAD_GETNAME_NP
INTERCEPTOR(int, pthread_getname_np, uptr thread, char *name, SIZE_T len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_getname_np, thread, name, len);
  int res = REAL(pthread_getname_np)(thread, name, len);
  if (!res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, name, internal_strnlen(name, len) + 1);
  return res;
}
#define INIT_PTHREAD_GETNAME_NP COMMON_INTERCEPT_FUNCTION(pthread_getname_np);
#else
#define INIT_PTHREAD_GETNAME_NP
#endif

#if SANITIZER_INTERCEPT_SINCOS
INTERCEPTOR(void, sincos, double x, double *sin, double *cos) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sincos, x, sin, cos);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  REAL(sincos)(x, sin, cos);
  if (sin) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sin, sizeof(*sin));
  if (cos) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cos, sizeof(*cos));
}
INTERCEPTOR(void, sincosf, float x, float *sin, float *cos) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sincosf, x, sin, cos);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  REAL(sincosf)(x, sin, cos);
  if (sin) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sin, sizeof(*sin));
  if (cos) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cos, sizeof(*cos));
}
INTERCEPTOR(void, sincosl, long double x, long double *sin, long double *cos) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sincosl, x, sin, cos);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  REAL(sincosl)(x, sin, cos);
  if (sin) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sin, sizeof(*sin));
  if (cos) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cos, sizeof(*cos));
}
#define INIT_SINCOS                   \
  COMMON_INTERCEPT_FUNCTION(sincos);  \
  COMMON_INTERCEPT_FUNCTION(sincosf); \
  COMMON_INTERCEPT_FUNCTION_LDBL(sincosl);
#else
#define INIT_SINCOS
#endif

#if SANITIZER_INTERCEPT_REMQUO
INTERCEPTOR(double, remquo, double x, double y, int *quo) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, remquo, x, y, quo);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  double res = REAL(remquo)(x, y, quo);
  if (quo) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, quo, sizeof(*quo));
  return res;
}
INTERCEPTOR(float, remquof, float x, float y, int *quo) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, remquof, x, y, quo);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  float res = REAL(remquof)(x, y, quo);
  if (quo) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, quo, sizeof(*quo));
  return res;
}
#define INIT_REMQUO                   \
  COMMON_INTERCEPT_FUNCTION(remquo);  \
  COMMON_INTERCEPT_FUNCTION(remquof);
#else
#define INIT_REMQUO
#endif

#if SANITIZER_INTERCEPT_REMQUOL
INTERCEPTOR(long double, remquol, long double x, long double y, int *quo) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, remquol, x, y, quo);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  long double res = REAL(remquol)(x, y, quo);
  if (quo) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, quo, sizeof(*quo));
  return res;
}
#define INIT_REMQUOL                  \
  COMMON_INTERCEPT_FUNCTION_LDBL(remquol);
#else
#define INIT_REMQUOL
#endif

#if SANITIZER_INTERCEPT_LGAMMA
extern int signgam;
INTERCEPTOR(double, lgamma, double x) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, lgamma, x);
  double res = REAL(lgamma)(x);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, &signgam, sizeof(signgam));
  return res;
}
INTERCEPTOR(float, lgammaf, float x) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, lgammaf, x);
  float res = REAL(lgammaf)(x);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, &signgam, sizeof(signgam));
  return res;
}
#define INIT_LGAMMA                   \
  COMMON_INTERCEPT_FUNCTION(lgamma);  \
  COMMON_INTERCEPT_FUNCTION(lgammaf);
#else
#define INIT_LGAMMA
#endif

#if SANITIZER_INTERCEPT_LGAMMAL
INTERCEPTOR(long double, lgammal, long double x) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, lgammal, x);
  long double res = REAL(lgammal)(x);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, &signgam, sizeof(signgam));
  return res;
}
#define INIT_LGAMMAL                  \
  COMMON_INTERCEPT_FUNCTION_LDBL(lgammal);
#else
#define INIT_LGAMMAL
#endif

#if SANITIZER_INTERCEPT_LGAMMA_R
INTERCEPTOR(double, lgamma_r, double x, int *signp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, lgamma_r, x, signp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  double res = REAL(lgamma_r)(x, signp);
  if (signp) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, signp, sizeof(*signp));
  return res;
}
INTERCEPTOR(float, lgammaf_r, float x, int *signp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, lgammaf_r, x, signp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  float res = REAL(lgammaf_r)(x, signp);
  if (signp) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, signp, sizeof(*signp));
  return res;
}
#define INIT_LGAMMA_R                   \
  COMMON_INTERCEPT_FUNCTION(lgamma_r);  \
  COMMON_INTERCEPT_FUNCTION(lgammaf_r);
#else
#define INIT_LGAMMA_R
#endif

#if SANITIZER_INTERCEPT_LGAMMAL_R
INTERCEPTOR(long double, lgammal_r, long double x, int *signp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, lgammal_r, x, signp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  long double res = REAL(lgammal_r)(x, signp);
  if (signp) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, signp, sizeof(*signp));
  return res;
}
#define INIT_LGAMMAL_R COMMON_INTERCEPT_FUNCTION_LDBL(lgammal_r);
#else
#define INIT_LGAMMAL_R
#endif

#if SANITIZER_INTERCEPT_DRAND48_R
INTERCEPTOR(int, drand48_r, void *buffer, double *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, drand48_r, buffer, result);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(drand48_r)(buffer, result);
  if (result) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
  return res;
}
INTERCEPTOR(int, lrand48_r, void *buffer, long *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, lrand48_r, buffer, result);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(lrand48_r)(buffer, result);
  if (result) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, result, sizeof(*result));
  return res;
}
#define INIT_DRAND48_R                  \
  COMMON_INTERCEPT_FUNCTION(drand48_r); \
  COMMON_INTERCEPT_FUNCTION(lrand48_r);
#else
#define INIT_DRAND48_R
#endif

#if SANITIZER_INTERCEPT_RAND_R
INTERCEPTOR(int, rand_r, unsigned *seedp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, rand_r, seedp);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, seedp, sizeof(*seedp));
  return REAL(rand_r)(seedp);
}
#define INIT_RAND_R COMMON_INTERCEPT_FUNCTION(rand_r);
#else
#define INIT_RAND_R
#endif

#if SANITIZER_INTERCEPT_GETLINE
INTERCEPTOR(SSIZE_T, getline, char **lineptr, SIZE_T *n, void *stream) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getline, lineptr, n, stream);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(getline)(lineptr, n, stream);
  if (res > 0) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, lineptr, sizeof(*lineptr));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n, sizeof(*n));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *lineptr, res + 1);
  }
  return res;
}

// FIXME: under ASan the call below may write to freed memory and corrupt its
// metadata. See
// https://github.com/google/sanitizers/issues/321.
#define GETDELIM_INTERCEPTOR_IMPL(vname)                                       \
  {                                                                            \
    void *ctx;                                                                 \
    COMMON_INTERCEPTOR_ENTER(ctx, vname, lineptr, n, delim, stream);           \
    SSIZE_T res = REAL(vname)(lineptr, n, delim, stream);                      \
    if (res > 0) {                                                             \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, lineptr, sizeof(*lineptr));          \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n, sizeof(*n));                      \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *lineptr, res + 1);                  \
    }                                                                          \
    return res;                                                                \
  }

INTERCEPTOR(SSIZE_T, __getdelim, char **lineptr, SIZE_T *n, int delim,
            void *stream)
GETDELIM_INTERCEPTOR_IMPL(__getdelim)

// There's no __getdelim() on FreeBSD so we supply the getdelim() interceptor
// with its own body.
INTERCEPTOR(SSIZE_T, getdelim, char **lineptr, SIZE_T *n, int delim,
            void *stream)
GETDELIM_INTERCEPTOR_IMPL(getdelim)

#define INIT_GETLINE                     \
  COMMON_INTERCEPT_FUNCTION(getline);    \
  COMMON_INTERCEPT_FUNCTION(__getdelim); \
  COMMON_INTERCEPT_FUNCTION(getdelim);
#else
#define INIT_GETLINE
#endif

#if SANITIZER_INTERCEPT_ICONV
INTERCEPTOR(SIZE_T, iconv, void *cd, char **inbuf, SIZE_T *inbytesleft,
            char **outbuf, SIZE_T *outbytesleft) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, iconv, cd, inbuf, inbytesleft, outbuf,
                           outbytesleft);
  if (inbytesleft)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, inbytesleft, sizeof(*inbytesleft));
  if (inbuf && inbytesleft)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, *inbuf, *inbytesleft);
  if (outbytesleft)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, outbytesleft, sizeof(*outbytesleft));
  void *outbuf_orig = outbuf ? *outbuf : nullptr;
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SIZE_T res = REAL(iconv)(cd, inbuf, inbytesleft, outbuf, outbytesleft);
  if (outbuf && *outbuf > outbuf_orig) {
    SIZE_T sz = (char *)*outbuf - (char *)outbuf_orig;
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, outbuf_orig, sz);
  }
  return res;
}
#define INIT_ICONV COMMON_INTERCEPT_FUNCTION(iconv);
#else
#define INIT_ICONV
#endif

#if SANITIZER_INTERCEPT_TIMES
INTERCEPTOR(__sanitizer_clock_t, times, void *tms) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, times, tms);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  __sanitizer_clock_t res = REAL(times)(tms);
  if (res != (__sanitizer_clock_t)-1 && tms)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, tms, struct_tms_sz);
  return res;
}
#define INIT_TIMES COMMON_INTERCEPT_FUNCTION(times);
#else
#define INIT_TIMES
#endif

#if SANITIZER_INTERCEPT_TLS_GET_ADDR
#if !SANITIZER_S390
#define INIT_TLS_GET_ADDR COMMON_INTERCEPT_FUNCTION(__tls_get_addr)
// If you see any crashes around this functions, there are 2 known issues with
// it: 1. __tls_get_addr can be called with mis-aligned stack due to:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=58066
// 2. It can be called recursively if sanitizer code uses __tls_get_addr
// to access thread local variables (it should not happen normally,
// because sanitizers use initial-exec tls model).
INTERCEPTOR(void *, __tls_get_addr, void *arg) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __tls_get_addr, arg);
  void *res = REAL(__tls_get_addr)(arg);
  uptr tls_begin, tls_end;
  COMMON_INTERCEPTOR_GET_TLS_RANGE(&tls_begin, &tls_end);
  DTLS::DTV *dtv = DTLS_on_tls_get_addr(arg, res, tls_begin, tls_end);
  if (dtv) {
    // New DTLS block has been allocated.
    COMMON_INTERCEPTOR_INITIALIZE_RANGE((void *)dtv->beg, dtv->size);
  }
  return res;
}
#if SANITIZER_PPC
// On PowerPC, we also need to intercept __tls_get_addr_opt, which has
// mostly the same semantics as __tls_get_addr, but its presence enables
// some optimizations in linker (which are safe to ignore here).
extern "C" __attribute__((alias("__interceptor___tls_get_addr"),
                          visibility("default")))
void *__tls_get_addr_opt(void *arg);
#endif
#else // SANITIZER_S390
// On s390, we have to intercept two functions here:
// - __tls_get_addr_internal, which is a glibc-internal function that is like
//   the usual __tls_get_addr, but returns a TP-relative offset instead of
//   a proper pointer.  It is used by dlsym for TLS symbols.
// - __tls_get_offset, which is like the above, but also takes a GOT-relative
//   descriptor offset as an argument instead of a pointer.  GOT address
//   is passed in r12, so it's necessary to write it in assembly.  This is
//   the function used by the compiler.
extern "C" uptr __tls_get_offset_wrapper(void *arg, uptr (*fn)(void *arg));
#define INIT_TLS_GET_ADDR COMMON_INTERCEPT_FUNCTION(__tls_get_offset)
DEFINE_REAL(uptr, __tls_get_offset, void *arg)
extern "C" uptr __tls_get_offset(void *arg);
extern "C" uptr __interceptor___tls_get_offset(void *arg);
INTERCEPTOR(uptr, __tls_get_addr_internal, void *arg) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __tls_get_addr_internal, arg);
  uptr res = __tls_get_offset_wrapper(arg, REAL(__tls_get_offset));
  uptr tp = reinterpret_cast<uptr>(__builtin_thread_pointer());
  void *ptr = reinterpret_cast<void *>(res + tp);
  uptr tls_begin, tls_end;
  COMMON_INTERCEPTOR_GET_TLS_RANGE(&tls_begin, &tls_end);
  DTLS::DTV *dtv = DTLS_on_tls_get_addr(arg, ptr, tls_begin, tls_end);
  if (dtv) {
    // New DTLS block has been allocated.
    COMMON_INTERCEPTOR_INITIALIZE_RANGE((void *)dtv->beg, dtv->size);
  }
  return res;
}
// We need a hidden symbol aliasing the above, so that we can jump
// directly to it from the assembly below.
extern "C" __attribute__((alias("__interceptor___tls_get_addr_internal"),
                          visibility("hidden")))
uptr __tls_get_addr_hidden(void *arg);
// Now carefully intercept __tls_get_offset.
asm(
  ".text\n"
// The __intercept_ version has to exist, so that gen_dynamic_list.py
// exports our symbol.
  ".weak __tls_get_offset\n"
  ".type __tls_get_offset, @function\n"
  "__tls_get_offset:\n"
  ".global __interceptor___tls_get_offset\n"
  ".type __interceptor___tls_get_offset, @function\n"
  "__interceptor___tls_get_offset:\n"
#ifdef __s390x__
  "la %r2, 0(%r2,%r12)\n"
  "jg __tls_get_addr_hidden\n"
#else
  "basr %r3,0\n"
  "0: la %r2,0(%r2,%r12)\n"
  "l %r4,1f-0b(%r3)\n"
  "b 0(%r4,%r3)\n"
  "1: .long __tls_get_addr_hidden - 0b\n"
#endif
  ".size __interceptor___tls_get_offset, .-__interceptor___tls_get_offset\n"
// Assembly wrapper to call REAL(__tls_get_offset)(arg)
  ".type __tls_get_offset_wrapper, @function\n"
  "__tls_get_offset_wrapper:\n"
#ifdef __s390x__
  "sgr %r2,%r12\n"
#else
  "sr %r2,%r12\n"
#endif
  "br %r3\n"
  ".size __tls_get_offset_wrapper, .-__tls_get_offset_wrapper\n"
);
#endif // SANITIZER_S390
#else
#define INIT_TLS_GET_ADDR
#endif

#if SANITIZER_INTERCEPT_LISTXATTR
INTERCEPTOR(SSIZE_T, listxattr, const char *path, char *list, SIZE_T size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, listxattr, path, list, size);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(listxattr)(path, list, size);
  // Here and below, size == 0 is a special case where nothing is written to the
  // buffer, and res contains the desired buffer size.
  if (size && res > 0 && list) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, list, res);
  return res;
}
INTERCEPTOR(SSIZE_T, llistxattr, const char *path, char *list, SIZE_T size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, llistxattr, path, list, size);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(llistxattr)(path, list, size);
  if (size && res > 0 && list) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, list, res);
  return res;
}
INTERCEPTOR(SSIZE_T, flistxattr, int fd, char *list, SIZE_T size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, flistxattr, fd, list, size);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(flistxattr)(fd, list, size);
  if (size && res > 0 && list) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, list, res);
  return res;
}
#define INIT_LISTXATTR                   \
  COMMON_INTERCEPT_FUNCTION(listxattr);  \
  COMMON_INTERCEPT_FUNCTION(llistxattr); \
  COMMON_INTERCEPT_FUNCTION(flistxattr);
#else
#define INIT_LISTXATTR
#endif

#if SANITIZER_INTERCEPT_GETXATTR
INTERCEPTOR(SSIZE_T, getxattr, const char *path, const char *name, char *value,
            SIZE_T size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getxattr, path, name, value, size);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  if (name) COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(getxattr)(path, name, value, size);
  if (size && res > 0 && value) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, value, res);
  return res;
}
INTERCEPTOR(SSIZE_T, lgetxattr, const char *path, const char *name, char *value,
            SIZE_T size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, lgetxattr, path, name, value, size);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  if (name) COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(lgetxattr)(path, name, value, size);
  if (size && res > 0 && value) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, value, res);
  return res;
}
INTERCEPTOR(SSIZE_T, fgetxattr, int fd, const char *name, char *value,
            SIZE_T size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fgetxattr, fd, name, value, size);
  if (name) COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  SSIZE_T res = REAL(fgetxattr)(fd, name, value, size);
  if (size && res > 0 && value) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, value, res);
  return res;
}
#define INIT_GETXATTR                   \
  COMMON_INTERCEPT_FUNCTION(getxattr);  \
  COMMON_INTERCEPT_FUNCTION(lgetxattr); \
  COMMON_INTERCEPT_FUNCTION(fgetxattr);
#else
#define INIT_GETXATTR
#endif

#if SANITIZER_INTERCEPT_GETRESID
INTERCEPTOR(int, getresuid, void *ruid, void *euid, void *suid) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getresuid, ruid, euid, suid);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getresuid)(ruid, euid, suid);
  if (res >= 0) {
    if (ruid) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ruid, uid_t_sz);
    if (euid) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, euid, uid_t_sz);
    if (suid) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, suid, uid_t_sz);
  }
  return res;
}
INTERCEPTOR(int, getresgid, void *rgid, void *egid, void *sgid) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getresgid, rgid, egid, sgid);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getresgid)(rgid, egid, sgid);
  if (res >= 0) {
    if (rgid) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, rgid, gid_t_sz);
    if (egid) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, egid, gid_t_sz);
    if (sgid) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sgid, gid_t_sz);
  }
  return res;
}
#define INIT_GETRESID                   \
  COMMON_INTERCEPT_FUNCTION(getresuid); \
  COMMON_INTERCEPT_FUNCTION(getresgid);
#else
#define INIT_GETRESID
#endif

#if SANITIZER_INTERCEPT_GETIFADDRS
// As long as getifaddrs()/freeifaddrs() use calloc()/free(), we don't need to
// intercept freeifaddrs(). If that ceases to be the case, we might need to
// intercept it to poison the memory again.
INTERCEPTOR(int, getifaddrs, __sanitizer_ifaddrs **ifap) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getifaddrs, ifap);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(getifaddrs)(ifap);
  if (res == 0 && ifap) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ifap, sizeof(void *));
    __sanitizer_ifaddrs *p = *ifap;
    while (p) {
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, sizeof(__sanitizer_ifaddrs));
      if (p->ifa_name)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->ifa_name,
                                       REAL(strlen)(p->ifa_name) + 1);
      if (p->ifa_addr)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->ifa_addr, struct_sockaddr_sz);
      if (p->ifa_netmask)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->ifa_netmask, struct_sockaddr_sz);
      // On Linux this is a union, but the other member also points to a
      // struct sockaddr, so the following is sufficient.
      if (p->ifa_dstaddr)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->ifa_dstaddr, struct_sockaddr_sz);
      // FIXME(smatveev): Unpoison p->ifa_data as well.
      p = p->ifa_next;
    }
  }
  return res;
}
#define INIT_GETIFADDRS                  \
  COMMON_INTERCEPT_FUNCTION(getifaddrs);
#else
#define INIT_GETIFADDRS
#endif

#if SANITIZER_INTERCEPT_IF_INDEXTONAME
INTERCEPTOR(char *, if_indextoname, unsigned int ifindex, char* ifname) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, if_indextoname, ifindex, ifname);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  char *res = REAL(if_indextoname)(ifindex, ifname);
  if (res && ifname)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ifname, REAL(strlen)(ifname) + 1);
  return res;
}
INTERCEPTOR(unsigned int, if_nametoindex, const char* ifname) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, if_nametoindex, ifname);
  if (ifname)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ifname, REAL(strlen)(ifname) + 1);
  return REAL(if_nametoindex)(ifname);
}
#define INIT_IF_INDEXTONAME                  \
  COMMON_INTERCEPT_FUNCTION(if_indextoname); \
  COMMON_INTERCEPT_FUNCTION(if_nametoindex);
#else
#define INIT_IF_INDEXTONAME
#endif

#if SANITIZER_INTERCEPT_CAPGET
INTERCEPTOR(int, capget, void *hdrp, void *datap) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, capget, hdrp, datap);
  if (hdrp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, hdrp, __user_cap_header_struct_sz);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(capget)(hdrp, datap);
  if (res == 0 && datap)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, datap, __user_cap_data_struct_sz);
  // We can also return -1 and write to hdrp->version if the version passed in
  // hdrp->version is unsupported. But that's not a trivial condition to check,
  // and anyway COMMON_INTERCEPTOR_READ_RANGE protects us to some extent.
  return res;
}
INTERCEPTOR(int, capset, void *hdrp, const void *datap) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, capset, hdrp, datap);
  if (hdrp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, hdrp, __user_cap_header_struct_sz);
  if (datap)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, datap, __user_cap_data_struct_sz);
  return REAL(capset)(hdrp, datap);
}
#define INIT_CAPGET                  \
  COMMON_INTERCEPT_FUNCTION(capget); \
  COMMON_INTERCEPT_FUNCTION(capset);
#else
#define INIT_CAPGET
#endif

#if SANITIZER_INTERCEPT_AEABI_MEM
INTERCEPTOR(void *, __aeabi_memmove, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memmove4, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memmove8, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memcpy, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memcpy4, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memcpy8, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size);
}

// Note the argument order.
INTERCEPTOR(void *, __aeabi_memset, void *block, uptr size, int c) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size);
}

INTERCEPTOR(void *, __aeabi_memset4, void *block, uptr size, int c) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size);
}

INTERCEPTOR(void *, __aeabi_memset8, void *block, uptr size, int c) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size);
}

INTERCEPTOR(void *, __aeabi_memclr, void *block, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, 0, size);
}

INTERCEPTOR(void *, __aeabi_memclr4, void *block, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, 0, size);
}

INTERCEPTOR(void *, __aeabi_memclr8, void *block, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, 0, size);
}

#define INIT_AEABI_MEM                         \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memmove);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memmove4); \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memmove8); \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memcpy);   \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memcpy4);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memcpy8);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memset);   \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memset4);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memset8);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memclr);   \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memclr4);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memclr8);
#else
#define INIT_AEABI_MEM
#endif  // SANITIZER_INTERCEPT_AEABI_MEM

#if SANITIZER_INTERCEPT___BZERO
INTERCEPTOR(void *, __bzero, void *block, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, 0, size);
}

#define INIT___BZERO COMMON_INTERCEPT_FUNCTION(__bzero);
#else
#define INIT___BZERO
#endif  // SANITIZER_INTERCEPT___BZERO

#if SANITIZER_INTERCEPT_FTIME
INTERCEPTOR(int, ftime, __sanitizer_timeb *tp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ftime, tp);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(ftime)(tp);
  if (tp)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, tp, sizeof(*tp));
  return res;
}
#define INIT_FTIME COMMON_INTERCEPT_FUNCTION(ftime);
#else
#define INIT_FTIME
#endif  // SANITIZER_INTERCEPT_FTIME

#if SANITIZER_INTERCEPT_XDR
INTERCEPTOR(void, xdrmem_create, __sanitizer_XDR *xdrs, uptr addr,
            unsigned size, int op) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, xdrmem_create, xdrs, addr, size, op);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  REAL(xdrmem_create)(xdrs, addr, size, op);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, xdrs, sizeof(*xdrs));
  if (op == __sanitizer_XDR_ENCODE) {
    // It's not obvious how much data individual xdr_ routines write.
    // Simply unpoison the entire target buffer in advance.
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, (void *)addr, size);
  }
}

INTERCEPTOR(void, xdrstdio_create, __sanitizer_XDR *xdrs, void *file, int op) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, xdrstdio_create, xdrs, file, op);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  REAL(xdrstdio_create)(xdrs, file, op);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, xdrs, sizeof(*xdrs));
}

// FIXME: under ASan the call below may write to freed memory and corrupt
// its metadata. See
// https://github.com/google/sanitizers/issues/321.
#define XDR_INTERCEPTOR(F, T)                             \
  INTERCEPTOR(int, F, __sanitizer_XDR *xdrs, T *p) {      \
    void *ctx;                                            \
    COMMON_INTERCEPTOR_ENTER(ctx, F, xdrs, p);            \
    if (p && xdrs->x_op == __sanitizer_XDR_ENCODE)        \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, p, sizeof(*p));  \
    int res = REAL(F)(xdrs, p);                           \
    if (res && p && xdrs->x_op == __sanitizer_XDR_DECODE) \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, sizeof(*p)); \
    return res;                                           \
  }

XDR_INTERCEPTOR(xdr_short, short)
XDR_INTERCEPTOR(xdr_u_short, unsigned short)
XDR_INTERCEPTOR(xdr_int, int)
XDR_INTERCEPTOR(xdr_u_int, unsigned)
XDR_INTERCEPTOR(xdr_long, long)
XDR_INTERCEPTOR(xdr_u_long, unsigned long)
XDR_INTERCEPTOR(xdr_hyper, long long)
XDR_INTERCEPTOR(xdr_u_hyper, unsigned long long)
XDR_INTERCEPTOR(xdr_longlong_t, long long)
XDR_INTERCEPTOR(xdr_u_longlong_t, unsigned long long)
XDR_INTERCEPTOR(xdr_int8_t, u8)
XDR_INTERCEPTOR(xdr_uint8_t, u8)
XDR_INTERCEPTOR(xdr_int16_t, u16)
XDR_INTERCEPTOR(xdr_uint16_t, u16)
XDR_INTERCEPTOR(xdr_int32_t, u32)
XDR_INTERCEPTOR(xdr_uint32_t, u32)
XDR_INTERCEPTOR(xdr_int64_t, u64)
XDR_INTERCEPTOR(xdr_uint64_t, u64)
XDR_INTERCEPTOR(xdr_quad_t, long long)
XDR_INTERCEPTOR(xdr_u_quad_t, unsigned long long)
XDR_INTERCEPTOR(xdr_bool, bool)
XDR_INTERCEPTOR(xdr_enum, int)
XDR_INTERCEPTOR(xdr_char, char)
XDR_INTERCEPTOR(xdr_u_char, unsigned char)
XDR_INTERCEPTOR(xdr_float, float)
XDR_INTERCEPTOR(xdr_double, double)

// FIXME: intercept xdr_array, opaque, union, vector, reference, pointer,
// wrapstring, sizeof

INTERCEPTOR(int, xdr_bytes, __sanitizer_XDR *xdrs, char **p, unsigned *sizep,
            unsigned maxsize) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, xdr_bytes, xdrs, p, sizep, maxsize);
  if (p && sizep && xdrs->x_op == __sanitizer_XDR_ENCODE) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, p, sizeof(*p));
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sizep, sizeof(*sizep));
    COMMON_INTERCEPTOR_READ_RANGE(ctx, *p, *sizep);
  }
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(xdr_bytes)(xdrs, p, sizep, maxsize);
  if (p && sizep && xdrs->x_op == __sanitizer_XDR_DECODE) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, sizeof(*p));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sizep, sizeof(*sizep));
    if (res && *p && *sizep) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *p, *sizep);
  }
  return res;
}

INTERCEPTOR(int, xdr_string, __sanitizer_XDR *xdrs, char **p,
            unsigned maxsize) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, xdr_string, xdrs, p, maxsize);
  if (p && xdrs->x_op == __sanitizer_XDR_ENCODE) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, p, sizeof(*p));
    COMMON_INTERCEPTOR_READ_RANGE(ctx, *p, REAL(strlen)(*p) + 1);
  }
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(xdr_string)(xdrs, p, maxsize);
  if (p && xdrs->x_op == __sanitizer_XDR_DECODE) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, sizeof(*p));
    if (res && *p)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *p, REAL(strlen)(*p) + 1);
  }
  return res;
}

#define INIT_XDR                               \
  COMMON_INTERCEPT_FUNCTION(xdrmem_create);    \
  COMMON_INTERCEPT_FUNCTION(xdrstdio_create);  \
  COMMON_INTERCEPT_FUNCTION(xdr_short);        \
  COMMON_INTERCEPT_FUNCTION(xdr_u_short);      \
  COMMON_INTERCEPT_FUNCTION(xdr_int);          \
  COMMON_INTERCEPT_FUNCTION(xdr_u_int);        \
  COMMON_INTERCEPT_FUNCTION(xdr_long);         \
  COMMON_INTERCEPT_FUNCTION(xdr_u_long);       \
  COMMON_INTERCEPT_FUNCTION(xdr_hyper);        \
  COMMON_INTERCEPT_FUNCTION(xdr_u_hyper);      \
  COMMON_INTERCEPT_FUNCTION(xdr_longlong_t);   \
  COMMON_INTERCEPT_FUNCTION(xdr_u_longlong_t); \
  COMMON_INTERCEPT_FUNCTION(xdr_int8_t);       \
  COMMON_INTERCEPT_FUNCTION(xdr_uint8_t);      \
  COMMON_INTERCEPT_FUNCTION(xdr_int16_t);      \
  COMMON_INTERCEPT_FUNCTION(xdr_uint16_t);     \
  COMMON_INTERCEPT_FUNCTION(xdr_int32_t);      \
  COMMON_INTERCEPT_FUNCTION(xdr_uint32_t);     \
  COMMON_INTERCEPT_FUNCTION(xdr_int64_t);      \
  COMMON_INTERCEPT_FUNCTION(xdr_uint64_t);     \
  COMMON_INTERCEPT_FUNCTION(xdr_quad_t);       \
  COMMON_INTERCEPT_FUNCTION(xdr_u_quad_t);     \
  COMMON_INTERCEPT_FUNCTION(xdr_bool);         \
  COMMON_INTERCEPT_FUNCTION(xdr_enum);         \
  COMMON_INTERCEPT_FUNCTION(xdr_char);         \
  COMMON_INTERCEPT_FUNCTION(xdr_u_char);       \
  COMMON_INTERCEPT_FUNCTION(xdr_float);        \
  COMMON_INTERCEPT_FUNCTION(xdr_double);       \
  COMMON_INTERCEPT_FUNCTION(xdr_bytes);        \
  COMMON_INTERCEPT_FUNCTION(xdr_string);
#else
#define INIT_XDR
#endif  // SANITIZER_INTERCEPT_XDR

#if SANITIZER_INTERCEPT_TSEARCH
INTERCEPTOR(void *, tsearch, void *key, void **rootp,
            int (*compar)(const void *, const void *)) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, tsearch, key, rootp, compar);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  void *res = REAL(tsearch)(key, rootp, compar);
  if (res && *(void **)res == key)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, sizeof(void *));
  return res;
}
#define INIT_TSEARCH COMMON_INTERCEPT_FUNCTION(tsearch);
#else
#define INIT_TSEARCH
#endif

#if SANITIZER_INTERCEPT_LIBIO_INTERNALS || SANITIZER_INTERCEPT_FOPEN || \
    SANITIZER_INTERCEPT_OPEN_MEMSTREAM
void unpoison_file(__sanitizer_FILE *fp) {
#if SANITIZER_HAS_STRUCT_FILE
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(fp, sizeof(*fp));
#if SANITIZER_NETBSD
  if (fp->_bf._base && fp->_bf._size > 0)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(fp->_bf._base,
                                        fp->_bf._size);
#else
  if (fp->_IO_read_base && fp->_IO_read_base < fp->_IO_read_end)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(fp->_IO_read_base,
                                        fp->_IO_read_end - fp->_IO_read_base);
#endif
#endif  // SANITIZER_HAS_STRUCT_FILE
}
#endif

#if SANITIZER_INTERCEPT_LIBIO_INTERNALS
// These guys are called when a .c source is built with -O2.
INTERCEPTOR(int, __uflow, __sanitizer_FILE *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __uflow, fp);
  int res = REAL(__uflow)(fp);
  unpoison_file(fp);
  return res;
}
INTERCEPTOR(int, __underflow, __sanitizer_FILE *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __underflow, fp);
  int res = REAL(__underflow)(fp);
  unpoison_file(fp);
  return res;
}
INTERCEPTOR(int, __overflow, __sanitizer_FILE *fp, int ch) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __overflow, fp, ch);
  int res = REAL(__overflow)(fp, ch);
  unpoison_file(fp);
  return res;
}
INTERCEPTOR(int, __wuflow, __sanitizer_FILE *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __wuflow, fp);
  int res = REAL(__wuflow)(fp);
  unpoison_file(fp);
  return res;
}
INTERCEPTOR(int, __wunderflow, __sanitizer_FILE *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __wunderflow, fp);
  int res = REAL(__wunderflow)(fp);
  unpoison_file(fp);
  return res;
}
INTERCEPTOR(int, __woverflow, __sanitizer_FILE *fp, int ch) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __woverflow, fp, ch);
  int res = REAL(__woverflow)(fp, ch);
  unpoison_file(fp);
  return res;
}
#define INIT_LIBIO_INTERNALS               \
  COMMON_INTERCEPT_FUNCTION(__uflow);      \
  COMMON_INTERCEPT_FUNCTION(__underflow);  \
  COMMON_INTERCEPT_FUNCTION(__overflow);   \
  COMMON_INTERCEPT_FUNCTION(__wuflow);     \
  COMMON_INTERCEPT_FUNCTION(__wunderflow); \
  COMMON_INTERCEPT_FUNCTION(__woverflow);
#else
#define INIT_LIBIO_INTERNALS
#endif

#if SANITIZER_INTERCEPT_FOPEN
INTERCEPTOR(__sanitizer_FILE *, fopen, const char *path, const char *mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fopen, path, mode);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, mode, REAL(strlen)(mode) + 1);
  __sanitizer_FILE *res = REAL(fopen)(path, mode);
  COMMON_INTERCEPTOR_FILE_OPEN(ctx, res, path);
  if (res) unpoison_file(res);
  return res;
}
INTERCEPTOR(__sanitizer_FILE *, fdopen, int fd, const char *mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fdopen, fd, mode);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, mode, REAL(strlen)(mode) + 1);
  __sanitizer_FILE *res = REAL(fdopen)(fd, mode);
  if (res) unpoison_file(res);
  return res;
}
INTERCEPTOR(__sanitizer_FILE *, freopen, const char *path, const char *mode,
            __sanitizer_FILE *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, freopen, path, mode, fp);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, mode, REAL(strlen)(mode) + 1);
  COMMON_INTERCEPTOR_FILE_CLOSE(ctx, fp);
  __sanitizer_FILE *res = REAL(freopen)(path, mode, fp);
  COMMON_INTERCEPTOR_FILE_OPEN(ctx, res, path);
  if (res) unpoison_file(res);
  return res;
}
#define INIT_FOPEN                   \
  COMMON_INTERCEPT_FUNCTION(fopen);  \
  COMMON_INTERCEPT_FUNCTION(fdopen); \
  COMMON_INTERCEPT_FUNCTION(freopen);
#else
#define INIT_FOPEN
#endif

#if SANITIZER_INTERCEPT_FOPEN64
INTERCEPTOR(__sanitizer_FILE *, fopen64, const char *path, const char *mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fopen64, path, mode);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, mode, REAL(strlen)(mode) + 1);
  __sanitizer_FILE *res = REAL(fopen64)(path, mode);
  COMMON_INTERCEPTOR_FILE_OPEN(ctx, res, path);
  if (res) unpoison_file(res);
  return res;
}
INTERCEPTOR(__sanitizer_FILE *, freopen64, const char *path, const char *mode,
            __sanitizer_FILE *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, freopen64, path, mode, fp);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, mode, REAL(strlen)(mode) + 1);
  COMMON_INTERCEPTOR_FILE_CLOSE(ctx, fp);
  __sanitizer_FILE *res = REAL(freopen64)(path, mode, fp);
  COMMON_INTERCEPTOR_FILE_OPEN(ctx, res, path);
  if (res) unpoison_file(res);
  return res;
}
#define INIT_FOPEN64                  \
  COMMON_INTERCEPT_FUNCTION(fopen64); \
  COMMON_INTERCEPT_FUNCTION(freopen64);
#else
#define INIT_FOPEN64
#endif

#if SANITIZER_INTERCEPT_OPEN_MEMSTREAM
INTERCEPTOR(__sanitizer_FILE *, open_memstream, char **ptr, SIZE_T *sizeloc) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, open_memstream, ptr, sizeloc);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  __sanitizer_FILE *res = REAL(open_memstream)(ptr, sizeloc);
  if (res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, sizeof(*ptr));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sizeloc, sizeof(*sizeloc));
    unpoison_file(res);
    FileMetadata file = {ptr, sizeloc};
    SetInterceptorMetadata(res, file);
  }
  return res;
}
INTERCEPTOR(__sanitizer_FILE *, open_wmemstream, wchar_t **ptr,
            SIZE_T *sizeloc) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, open_wmemstream, ptr, sizeloc);
  __sanitizer_FILE *res = REAL(open_wmemstream)(ptr, sizeloc);
  if (res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, sizeof(*ptr));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sizeloc, sizeof(*sizeloc));
    unpoison_file(res);
    FileMetadata file = {(char **)ptr, sizeloc};
    SetInterceptorMetadata(res, file);
  }
  return res;
}
INTERCEPTOR(__sanitizer_FILE *, fmemopen, void *buf, SIZE_T size,
            const char *mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fmemopen, buf, size, mode);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  __sanitizer_FILE *res = REAL(fmemopen)(buf, size, mode);
  if (res) unpoison_file(res);
  return res;
}
#define INIT_OPEN_MEMSTREAM                   \
  COMMON_INTERCEPT_FUNCTION(open_memstream);  \
  COMMON_INTERCEPT_FUNCTION(open_wmemstream); \
  COMMON_INTERCEPT_FUNCTION(fmemopen);
#else
#define INIT_OPEN_MEMSTREAM
#endif

#if SANITIZER_INTERCEPT_OBSTACK
static void initialize_obstack(__sanitizer_obstack *obstack) {
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(obstack, sizeof(*obstack));
  if (obstack->chunk)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(obstack->chunk,
                                        sizeof(*obstack->chunk));
}

INTERCEPTOR(int, _obstack_begin_1, __sanitizer_obstack *obstack, int sz,
            int align, void *(*alloc_fn)(uptr arg, uptr sz),
            void (*free_fn)(uptr arg, void *p)) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, _obstack_begin_1, obstack, sz, align, alloc_fn,
                           free_fn);
  int res = REAL(_obstack_begin_1)(obstack, sz, align, alloc_fn, free_fn);
  if (res) initialize_obstack(obstack);
  return res;
}
INTERCEPTOR(int, _obstack_begin, __sanitizer_obstack *obstack, int sz,
            int align, void *(*alloc_fn)(uptr sz), void (*free_fn)(void *p)) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, _obstack_begin, obstack, sz, align, alloc_fn,
                           free_fn);
  int res = REAL(_obstack_begin)(obstack, sz, align, alloc_fn, free_fn);
  if (res) initialize_obstack(obstack);
  return res;
}
INTERCEPTOR(void, _obstack_newchunk, __sanitizer_obstack *obstack, int length) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, _obstack_newchunk, obstack, length);
  REAL(_obstack_newchunk)(obstack, length);
  if (obstack->chunk)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(
        obstack->chunk, obstack->next_free - (char *)obstack->chunk);
}
#define INIT_OBSTACK                           \
  COMMON_INTERCEPT_FUNCTION(_obstack_begin_1); \
  COMMON_INTERCEPT_FUNCTION(_obstack_begin);   \
  COMMON_INTERCEPT_FUNCTION(_obstack_newchunk);
#else
#define INIT_OBSTACK
#endif

#if SANITIZER_INTERCEPT_FFLUSH
INTERCEPTOR(int, fflush, __sanitizer_FILE *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fflush, fp);
  int res = REAL(fflush)(fp);
  // FIXME: handle fp == NULL
  if (fp) {
    const FileMetadata *m = GetInterceptorMetadata(fp);
    if (m) COMMON_INTERCEPTOR_INITIALIZE_RANGE(*m->addr, *m->size);
  }
  return res;
}
#define INIT_FFLUSH COMMON_INTERCEPT_FUNCTION(fflush);
#else
#define INIT_FFLUSH
#endif

#if SANITIZER_INTERCEPT_FCLOSE
INTERCEPTOR(int, fclose, __sanitizer_FILE *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fclose, fp);
  COMMON_INTERCEPTOR_FILE_CLOSE(ctx, fp);
  const FileMetadata *m = GetInterceptorMetadata(fp);
  int res = REAL(fclose)(fp);
  if (m) {
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(*m->addr, *m->size);
    DeleteInterceptorMetadata(fp);
  }
  return res;
}
#define INIT_FCLOSE COMMON_INTERCEPT_FUNCTION(fclose);
#else
#define INIT_FCLOSE
#endif

#if SANITIZER_INTERCEPT_DLOPEN_DLCLOSE
INTERCEPTOR(void*, dlopen, const char *filename, int flag) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER_NOIGNORE(ctx, dlopen, filename, flag);
  if (filename) COMMON_INTERCEPTOR_READ_STRING(ctx, filename, 0);
  COMMON_INTERCEPTOR_ON_DLOPEN(filename, flag);
  void *res = REAL(dlopen)(filename, flag);
  Symbolizer::GetOrInit()->InvalidateModuleList();
  COMMON_INTERCEPTOR_LIBRARY_LOADED(filename, res);
  return res;
}

INTERCEPTOR(int, dlclose, void *handle) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER_NOIGNORE(ctx, dlclose, handle);
  int res = REAL(dlclose)(handle);
  Symbolizer::GetOrInit()->InvalidateModuleList();
  COMMON_INTERCEPTOR_LIBRARY_UNLOADED();
  return res;
}
#define INIT_DLOPEN_DLCLOSE          \
  COMMON_INTERCEPT_FUNCTION(dlopen); \
  COMMON_INTERCEPT_FUNCTION(dlclose);
#else
#define INIT_DLOPEN_DLCLOSE
#endif

#if SANITIZER_INTERCEPT_GETPASS
INTERCEPTOR(char *, getpass, const char *prompt) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpass, prompt);
  if (prompt)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, prompt, REAL(strlen)(prompt)+1);
  char *res = REAL(getpass)(prompt);
  if (res) COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, REAL(strlen)(res)+1);
  return res;
}

#define INIT_GETPASS COMMON_INTERCEPT_FUNCTION(getpass);
#else
#define INIT_GETPASS
#endif

#if SANITIZER_INTERCEPT_TIMERFD
INTERCEPTOR(int, timerfd_settime, int fd, int flags, void *new_value,
            void *old_value) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, timerfd_settime, fd, flags, new_value,
                           old_value);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, new_value, struct_itimerspec_sz);
  int res = REAL(timerfd_settime)(fd, flags, new_value, old_value);
  if (res != -1 && old_value)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, old_value, struct_itimerspec_sz);
  return res;
}

INTERCEPTOR(int, timerfd_gettime, int fd, void *curr_value) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, timerfd_gettime, fd, curr_value);
  int res = REAL(timerfd_gettime)(fd, curr_value);
  if (res != -1 && curr_value)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, curr_value, struct_itimerspec_sz);
  return res;
}
#define INIT_TIMERFD                          \
  COMMON_INTERCEPT_FUNCTION(timerfd_settime); \
  COMMON_INTERCEPT_FUNCTION(timerfd_gettime);
#else
#define INIT_TIMERFD
#endif

#if SANITIZER_INTERCEPT_MLOCKX
// Linux kernel has a bug that leads to kernel deadlock if a process
// maps TBs of memory and then calls mlock().
static void MlockIsUnsupported() {
  static atomic_uint8_t printed;
  if (atomic_exchange(&printed, 1, memory_order_relaxed))
    return;
  VPrintf(1, "%s ignores mlock/mlockall/munlock/munlockall\n",
          SanitizerToolName);
}

INTERCEPTOR(int, mlock, const void *addr, uptr len) {
  MlockIsUnsupported();
  return 0;
}

INTERCEPTOR(int, munlock, const void *addr, uptr len) {
  MlockIsUnsupported();
  return 0;
}

INTERCEPTOR(int, mlockall, int flags) {
  MlockIsUnsupported();
  return 0;
}

INTERCEPTOR(int, munlockall, void) {
  MlockIsUnsupported();
  return 0;
}

#define INIT_MLOCKX                                                            \
  COMMON_INTERCEPT_FUNCTION(mlock);                                            \
  COMMON_INTERCEPT_FUNCTION(munlock);                                          \
  COMMON_INTERCEPT_FUNCTION(mlockall);                                         \
  COMMON_INTERCEPT_FUNCTION(munlockall);

#else
#define INIT_MLOCKX
#endif  // SANITIZER_INTERCEPT_MLOCKX

#if SANITIZER_INTERCEPT_FOPENCOOKIE
struct WrappedCookie {
  void *real_cookie;
  __sanitizer_cookie_io_functions_t real_io_funcs;
};

static uptr wrapped_read(void *cookie, char *buf, uptr size) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(3);
  WrappedCookie *wrapped_cookie = (WrappedCookie *)cookie;
  __sanitizer_cookie_io_read real_read = wrapped_cookie->real_io_funcs.read;
  return real_read ? real_read(wrapped_cookie->real_cookie, buf, size) : 0;
}

static uptr wrapped_write(void *cookie, const char *buf, uptr size) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(3);
  WrappedCookie *wrapped_cookie = (WrappedCookie *)cookie;
  __sanitizer_cookie_io_write real_write = wrapped_cookie->real_io_funcs.write;
  return real_write ? real_write(wrapped_cookie->real_cookie, buf, size) : size;
}

static int wrapped_seek(void *cookie, u64 *offset, int whence) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(3);
  COMMON_INTERCEPTOR_INITIALIZE_RANGE(offset, sizeof(*offset));
  WrappedCookie *wrapped_cookie = (WrappedCookie *)cookie;
  __sanitizer_cookie_io_seek real_seek = wrapped_cookie->real_io_funcs.seek;
  return real_seek ? real_seek(wrapped_cookie->real_cookie, offset, whence)
                   : -1;
}

static int wrapped_close(void *cookie) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(1);
  WrappedCookie *wrapped_cookie = (WrappedCookie *)cookie;
  __sanitizer_cookie_io_close real_close = wrapped_cookie->real_io_funcs.close;
  int res = real_close ? real_close(wrapped_cookie->real_cookie) : 0;
  InternalFree(wrapped_cookie);
  return res;
}

INTERCEPTOR(__sanitizer_FILE *, fopencookie, void *cookie, const char *mode,
            __sanitizer_cookie_io_functions_t io_funcs) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fopencookie, cookie, mode, io_funcs);
  WrappedCookie *wrapped_cookie =
      (WrappedCookie *)InternalAlloc(sizeof(WrappedCookie));
  wrapped_cookie->real_cookie = cookie;
  wrapped_cookie->real_io_funcs = io_funcs;
  __sanitizer_FILE *res =
      REAL(fopencookie)(wrapped_cookie, mode, {wrapped_read, wrapped_write,
                                               wrapped_seek, wrapped_close});
  return res;
}

#define INIT_FOPENCOOKIE COMMON_INTERCEPT_FUNCTION(fopencookie);
#else
#define INIT_FOPENCOOKIE
#endif  // SANITIZER_INTERCEPT_FOPENCOOKIE

#if SANITIZER_INTERCEPT_SEM
INTERCEPTOR(int, sem_init, __sanitizer_sem_t *s, int pshared, unsigned value) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sem_init, s, pshared, value);
  // Workaround a bug in glibc's "old" semaphore implementation by
  // zero-initializing the sem_t contents. This has to be done here because
  // interceptors bind to the lowest symbols version by default, hitting the
  // buggy code path while the non-sanitized build of the same code works fine.
  REAL(memset)(s, 0, sizeof(*s));
  int res = REAL(sem_init)(s, pshared, value);
  return res;
}

INTERCEPTOR(int, sem_destroy, __sanitizer_sem_t *s) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sem_destroy, s);
  int res = REAL(sem_destroy)(s);
  return res;
}

INTERCEPTOR(int, sem_wait, __sanitizer_sem_t *s) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sem_wait, s);
  int res = COMMON_INTERCEPTOR_BLOCK_REAL(sem_wait)(s);
  if (res == 0) {
    COMMON_INTERCEPTOR_ACQUIRE(ctx, (uptr)s);
  }
  return res;
}

INTERCEPTOR(int, sem_trywait, __sanitizer_sem_t *s) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sem_trywait, s);
  int res = COMMON_INTERCEPTOR_BLOCK_REAL(sem_trywait)(s);
  if (res == 0) {
    COMMON_INTERCEPTOR_ACQUIRE(ctx, (uptr)s);
  }
  return res;
}

INTERCEPTOR(int, sem_timedwait, __sanitizer_sem_t *s, void *abstime) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sem_timedwait, s, abstime);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, abstime, struct_timespec_sz);
  int res = COMMON_INTERCEPTOR_BLOCK_REAL(sem_timedwait)(s, abstime);
  if (res == 0) {
    COMMON_INTERCEPTOR_ACQUIRE(ctx, (uptr)s);
  }
  return res;
}

INTERCEPTOR(int, sem_post, __sanitizer_sem_t *s) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sem_post, s);
  COMMON_INTERCEPTOR_RELEASE(ctx, (uptr)s);
  int res = REAL(sem_post)(s);
  return res;
}

INTERCEPTOR(int, sem_getvalue, __sanitizer_sem_t *s, int *sval) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sem_getvalue, s, sval);
  int res = REAL(sem_getvalue)(s, sval);
  if (res == 0) {
    COMMON_INTERCEPTOR_ACQUIRE(ctx, (uptr)s);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sval, sizeof(*sval));
  }
  return res;
}
#define INIT_SEM                                                               \
  COMMON_INTERCEPT_FUNCTION(sem_init);                                         \
  COMMON_INTERCEPT_FUNCTION(sem_destroy);                                      \
  COMMON_INTERCEPT_FUNCTION(sem_wait);                                         \
  COMMON_INTERCEPT_FUNCTION(sem_trywait);                                      \
  COMMON_INTERCEPT_FUNCTION(sem_timedwait);                                    \
  COMMON_INTERCEPT_FUNCTION(sem_post);                                         \
  COMMON_INTERCEPT_FUNCTION(sem_getvalue);
#else
#define INIT_SEM
#endif // SANITIZER_INTERCEPT_SEM

#if SANITIZER_INTERCEPT_PTHREAD_SETCANCEL
INTERCEPTOR(int, pthread_setcancelstate, int state, int *oldstate) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_setcancelstate, state, oldstate);
  int res = REAL(pthread_setcancelstate)(state, oldstate);
  if (res == 0 && oldstate != nullptr)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, oldstate, sizeof(*oldstate));
  return res;
}

INTERCEPTOR(int, pthread_setcanceltype, int type, int *oldtype) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_setcanceltype, type, oldtype);
  int res = REAL(pthread_setcanceltype)(type, oldtype);
  if (res == 0 && oldtype != nullptr)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, oldtype, sizeof(*oldtype));
  return res;
}
#define INIT_PTHREAD_SETCANCEL                                                 \
  COMMON_INTERCEPT_FUNCTION(pthread_setcancelstate);                           \
  COMMON_INTERCEPT_FUNCTION(pthread_setcanceltype);
#else
#define INIT_PTHREAD_SETCANCEL
#endif

#if SANITIZER_INTERCEPT_MINCORE
INTERCEPTOR(int, mincore, void *addr, uptr length, unsigned char *vec) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, mincore, addr, length, vec);
  int res = REAL(mincore)(addr, length, vec);
  if (res == 0) {
    uptr page_size = GetPageSizeCached();
    uptr vec_size = ((length + page_size - 1) & (~(page_size - 1))) / page_size;
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, vec, vec_size);
  }
  return res;
}
#define INIT_MINCORE COMMON_INTERCEPT_FUNCTION(mincore);
#else
#define INIT_MINCORE
#endif

#if SANITIZER_INTERCEPT_PROCESS_VM_READV
INTERCEPTOR(SSIZE_T, process_vm_readv, int pid, __sanitizer_iovec *local_iov,
            uptr liovcnt, __sanitizer_iovec *remote_iov, uptr riovcnt,
            uptr flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, process_vm_readv, pid, local_iov, liovcnt,
                           remote_iov, riovcnt, flags);
  SSIZE_T res = REAL(process_vm_readv)(pid, local_iov, liovcnt, remote_iov,
                                       riovcnt, flags);
  if (res > 0)
    write_iovec(ctx, local_iov, liovcnt, res);
  return res;
}

INTERCEPTOR(SSIZE_T, process_vm_writev, int pid, __sanitizer_iovec *local_iov,
            uptr liovcnt, __sanitizer_iovec *remote_iov, uptr riovcnt,
            uptr flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, process_vm_writev, pid, local_iov, liovcnt,
                           remote_iov, riovcnt, flags);
  SSIZE_T res = REAL(process_vm_writev)(pid, local_iov, liovcnt, remote_iov,
                                        riovcnt, flags);
  if (res > 0)
    read_iovec(ctx, local_iov, liovcnt, res);
  return res;
}
#define INIT_PROCESS_VM_READV                                                  \
  COMMON_INTERCEPT_FUNCTION(process_vm_readv);                                 \
  COMMON_INTERCEPT_FUNCTION(process_vm_writev);
#else
#define INIT_PROCESS_VM_READV
#endif

#if SANITIZER_INTERCEPT_CTERMID
INTERCEPTOR(char *, ctermid, char *s) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ctermid, s);
  char *res = REAL(ctermid)(s);
  if (res) {
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, REAL(strlen)(res) + 1);
  }
  return res;
}
#define INIT_CTERMID COMMON_INTERCEPT_FUNCTION(ctermid);
#else
#define INIT_CTERMID
#endif

#if SANITIZER_INTERCEPT_CTERMID_R
INTERCEPTOR(char *, ctermid_r, char *s) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ctermid_r, s);
  char *res = REAL(ctermid_r)(s);
  if (res) {
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, REAL(strlen)(res) + 1);
  }
  return res;
}
#define INIT_CTERMID_R COMMON_INTERCEPT_FUNCTION(ctermid_r);
#else
#define INIT_CTERMID_R
#endif

#if SANITIZER_INTERCEPT_RECV_RECVFROM
INTERCEPTOR(SSIZE_T, recv, int fd, void *buf, SIZE_T len, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, recv, fd, buf, len, flags);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  SSIZE_T res = REAL(recv)(fd, buf, len, flags);
  if (res > 0) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, Min((SIZE_T)res, len));
  }
  if (res >= 0 && fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}

INTERCEPTOR(SSIZE_T, recvfrom, int fd, void *buf, SIZE_T len, int flags,
            void *srcaddr, int *addrlen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, recvfrom, fd, buf, len, flags, srcaddr,
                           addrlen);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  SIZE_T srcaddr_sz;
  if (srcaddr) srcaddr_sz = *addrlen;
  (void)srcaddr_sz;  // prevent "set but not used" warning
  SSIZE_T res = REAL(recvfrom)(fd, buf, len, flags, srcaddr, addrlen);
  if (res > 0) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, Min((SIZE_T)res, len));
    if (srcaddr)
      COMMON_INTERCEPTOR_INITIALIZE_RANGE(srcaddr,
                                          Min((SIZE_T)*addrlen, srcaddr_sz));
  }
  return res;
}
#define INIT_RECV_RECVFROM          \
  COMMON_INTERCEPT_FUNCTION(recv);  \
  COMMON_INTERCEPT_FUNCTION(recvfrom);
#else
#define INIT_RECV_RECVFROM
#endif

#if SANITIZER_INTERCEPT_SEND_SENDTO
INTERCEPTOR(SSIZE_T, send, int fd, void *buf, SIZE_T len, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, send, fd, buf, len, flags);
  if (fd >= 0) {
    COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
    COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  }
  SSIZE_T res = REAL(send)(fd, buf, len, flags);
  if (common_flags()->intercept_send && res > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, buf, Min((SIZE_T)res, len));
  return res;
}

INTERCEPTOR(SSIZE_T, sendto, int fd, void *buf, SIZE_T len, int flags,
            void *dstaddr, int addrlen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sendto, fd, buf, len, flags, dstaddr, addrlen);
  if (fd >= 0) {
    COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
    COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  }
  // Can't check dstaddr as it may have uninitialized padding at the end.
  SSIZE_T res = REAL(sendto)(fd, buf, len, flags, dstaddr, addrlen);
  if (common_flags()->intercept_send && res > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, buf, Min((SIZE_T)res, len));
  return res;
}
#define INIT_SEND_SENDTO           \
  COMMON_INTERCEPT_FUNCTION(send); \
  COMMON_INTERCEPT_FUNCTION(sendto);
#else
#define INIT_SEND_SENDTO
#endif

#if SANITIZER_INTERCEPT_EVENTFD_READ_WRITE
INTERCEPTOR(int, eventfd_read, int fd, u64 *value) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, eventfd_read, fd, value);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  int res = REAL(eventfd_read)(fd, value);
  if (res == 0) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, value, sizeof(*value));
    if (fd >= 0) COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  }
  return res;
}
INTERCEPTOR(int, eventfd_write, int fd, u64 value) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, eventfd_write, fd, value);
  if (fd >= 0) {
    COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
    COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  }
  int res = REAL(eventfd_write)(fd, value);
  return res;
}
#define INIT_EVENTFD_READ_WRITE            \
  COMMON_INTERCEPT_FUNCTION(eventfd_read); \
  COMMON_INTERCEPT_FUNCTION(eventfd_write)
#else
#define INIT_EVENTFD_READ_WRITE
#endif

#if SANITIZER_INTERCEPT_STAT
INTERCEPTOR(int, stat, const char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, stat, path, buf);
  if (common_flags()->intercept_stat)
    COMMON_INTERCEPTOR_READ_STRING(ctx, path, 0);
  int res = REAL(stat)(path, buf);
  if (!res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, __sanitizer::struct_stat_sz);
  return res;
}
#define INIT_STAT COMMON_INTERCEPT_FUNCTION(stat)
#else
#define INIT_STAT
#endif

#if SANITIZER_INTERCEPT_LSTAT
INTERCEPTOR(int, lstat, const char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, lstat, path, buf);
  if (common_flags()->intercept_stat)
    COMMON_INTERCEPTOR_READ_STRING(ctx, path, 0);
  int res = REAL(lstat)(path, buf);
  if (!res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, __sanitizer::struct_stat_sz);
  return res;
}
#define INIT_LSTAT COMMON_INTERCEPT_FUNCTION(lstat)
#else
#define INIT_LSTAT
#endif

#if SANITIZER_INTERCEPT___XSTAT
INTERCEPTOR(int, __xstat, int version, const char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __xstat, version, path, buf);
  if (common_flags()->intercept_stat)
    COMMON_INTERCEPTOR_READ_STRING(ctx, path, 0);
  int res = REAL(__xstat)(version, path, buf);
  if (!res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, __sanitizer::struct_stat_sz);
  return res;
}
#define INIT___XSTAT COMMON_INTERCEPT_FUNCTION(__xstat)
#else
#define INIT___XSTAT
#endif

#if SANITIZER_INTERCEPT___XSTAT64
INTERCEPTOR(int, __xstat64, int version, const char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __xstat64, version, path, buf);
  if (common_flags()->intercept_stat)
    COMMON_INTERCEPTOR_READ_STRING(ctx, path, 0);
  int res = REAL(__xstat64)(version, path, buf);
  if (!res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, __sanitizer::struct_stat64_sz);
  return res;
}
#define INIT___XSTAT64 COMMON_INTERCEPT_FUNCTION(__xstat64)
#else
#define INIT___XSTAT64
#endif

#if SANITIZER_INTERCEPT___LXSTAT
INTERCEPTOR(int, __lxstat, int version, const char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __lxstat, version, path, buf);
  if (common_flags()->intercept_stat)
    COMMON_INTERCEPTOR_READ_STRING(ctx, path, 0);
  int res = REAL(__lxstat)(version, path, buf);
  if (!res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, __sanitizer::struct_stat_sz);
  return res;
}
#define INIT___LXSTAT COMMON_INTERCEPT_FUNCTION(__lxstat)
#else
#define INIT___LXSTAT
#endif

#if SANITIZER_INTERCEPT___LXSTAT64
INTERCEPTOR(int, __lxstat64, int version, const char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __lxstat64, version, path, buf);
  if (common_flags()->intercept_stat)
    COMMON_INTERCEPTOR_READ_STRING(ctx, path, 0);
  int res = REAL(__lxstat64)(version, path, buf);
  if (!res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, __sanitizer::struct_stat64_sz);
  return res;
}
#define INIT___LXSTAT64 COMMON_INTERCEPT_FUNCTION(__lxstat64)
#else
#define INIT___LXSTAT64
#endif

// FIXME: add other *stat interceptor

#if SANITIZER_INTERCEPT_UTMP
INTERCEPTOR(void *, getutent, int dummy) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getutent, dummy);
  void *res = REAL(getutent)(dummy);
  if (res)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, __sanitizer::struct_utmp_sz);
  return res;
}
INTERCEPTOR(void *, getutid, void *ut) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getutid, ut);
  void *res = REAL(getutid)(ut);
  if (res)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, __sanitizer::struct_utmp_sz);
  return res;
}
INTERCEPTOR(void *, getutline, void *ut) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getutline, ut);
  void *res = REAL(getutline)(ut);
  if (res)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, __sanitizer::struct_utmp_sz);
  return res;
}
#define INIT_UTMP                      \
  COMMON_INTERCEPT_FUNCTION(getutent); \
  COMMON_INTERCEPT_FUNCTION(getutid);  \
  COMMON_INTERCEPT_FUNCTION(getutline);
#else
#define INIT_UTMP
#endif

#if SANITIZER_INTERCEPT_UTMPX
INTERCEPTOR(void *, getutxent, int dummy) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getutxent, dummy);
  void *res = REAL(getutxent)(dummy);
  if (res)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, __sanitizer::struct_utmpx_sz);
  return res;
}
INTERCEPTOR(void *, getutxid, void *ut) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getutxid, ut);
  void *res = REAL(getutxid)(ut);
  if (res)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, __sanitizer::struct_utmpx_sz);
  return res;
}
INTERCEPTOR(void *, getutxline, void *ut) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getutxline, ut);
  void *res = REAL(getutxline)(ut);
  if (res)
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(res, __sanitizer::struct_utmpx_sz);
  return res;
}
INTERCEPTOR(void *, pututxline, const void *ut) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pututxline, ut);
  if (ut)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ut, __sanitizer::struct_utmpx_sz);
  void *res = REAL(pututxline)(ut);
  if (res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, __sanitizer::struct_utmpx_sz);
  return res;
}
#define INIT_UTMPX                      \
  COMMON_INTERCEPT_FUNCTION(getutxent); \
  COMMON_INTERCEPT_FUNCTION(getutxid);  \
  COMMON_INTERCEPT_FUNCTION(getutxline); \
  COMMON_INTERCEPT_FUNCTION(pututxline);
#else
#define INIT_UTMPX
#endif

#if SANITIZER_INTERCEPT_GETLOADAVG
INTERCEPTOR(int, getloadavg, double *loadavg, int nelem) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getloadavg, loadavg, nelem);
  int res = REAL(getloadavg)(loadavg, nelem);
  if (res > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, loadavg, res * sizeof(*loadavg));
  return res;
}
#define INIT_GETLOADAVG                      \
  COMMON_INTERCEPT_FUNCTION(getloadavg);
#else
#define INIT_GETLOADAVG
#endif

#if SANITIZER_INTERCEPT_MCHECK_MPROBE
INTERCEPTOR(int, mcheck, void (*abortfunc)(int mstatus)) {
  return 0;
}

INTERCEPTOR(int, mcheck_pedantic, void (*abortfunc)(int mstatus)) {
  return 0;
}

INTERCEPTOR(int, mprobe, void *ptr) {
  return 0;
}
#endif

INTERCEPTOR(SIZE_T, wcslen, const wchar_t *s) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wcslen, s);
  SIZE_T res = REAL(wcslen)(s);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s, sizeof(wchar_t) * (res + 1));
  return res;
}

INTERCEPTOR(SIZE_T, wcsnlen, const wchar_t *s, SIZE_T n) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wcsnlen, s, n);
  SIZE_T res = REAL(wcsnlen)(s, n);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s, sizeof(wchar_t) * Min(res + 1, n));
  return res;
}
#define INIT_WCSLEN                  \
  COMMON_INTERCEPT_FUNCTION(wcslen); \
  COMMON_INTERCEPT_FUNCTION(wcsnlen);

#if SANITIZER_INTERCEPT_WCSCAT
INTERCEPTOR(wchar_t *, wcscat, wchar_t *dst, const wchar_t *src) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wcscat, dst, src);
  SIZE_T src_size = REAL(wcslen)(src);
  SIZE_T dst_size = REAL(wcslen)(dst);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, src, (src_size + 1) * sizeof(wchar_t));
  COMMON_INTERCEPTOR_READ_RANGE(ctx, dst, (dst_size + 1) * sizeof(wchar_t));
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst + dst_size,
                                 (src_size + 1) * sizeof(wchar_t));
  return REAL(wcscat)(dst, src);  // NOLINT
}

INTERCEPTOR(wchar_t *, wcsncat, wchar_t *dst, const wchar_t *src, SIZE_T n) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wcsncat, dst, src, n);
  SIZE_T src_size = REAL(wcsnlen)(src, n);
  SIZE_T dst_size = REAL(wcslen)(dst);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, src,
                                Min(src_size + 1, n) * sizeof(wchar_t));
  COMMON_INTERCEPTOR_READ_RANGE(ctx, dst, (dst_size + 1) * sizeof(wchar_t));
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst + dst_size,
                                 (src_size + 1) * sizeof(wchar_t));
  return REAL(wcsncat)(dst, src, n);  // NOLINT
}
#define INIT_WCSCAT                  \
  COMMON_INTERCEPT_FUNCTION(wcscat); \
  COMMON_INTERCEPT_FUNCTION(wcsncat);
#else
#define INIT_WCSCAT
#endif

#if SANITIZER_INTERCEPT_STRXFRM
static SIZE_T RealStrLen(const char *str) { return REAL(strlen)(str); }

static SIZE_T RealStrLen(const wchar_t *str) { return REAL(wcslen)(str); }

#define STRXFRM_INTERCEPTOR_IMPL(strxfrm, dest, src, len, ...)             \
  {                                                                        \
    void *ctx;                                                             \
    COMMON_INTERCEPTOR_ENTER(ctx, strxfrm, dest, src, len, ##__VA_ARGS__); \
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src,                                \
                                  sizeof(*src) * (RealStrLen(src) + 1));   \
    SIZE_T res = REAL(strxfrm)(dest, src, len, ##__VA_ARGS__);             \
    if (res < len)                                                         \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dest, sizeof(*src) * (res + 1)); \
    return res;                                                            \
  }

INTERCEPTOR(SIZE_T, strxfrm, char *dest, const char *src, SIZE_T len) {
  STRXFRM_INTERCEPTOR_IMPL(strxfrm, dest, src, len);
}

INTERCEPTOR(SIZE_T, strxfrm_l, char *dest, const char *src, SIZE_T len,
            void *locale) {
  STRXFRM_INTERCEPTOR_IMPL(strxfrm_l, dest, src, len, locale);
}

#define INIT_STRXFRM                  \
  COMMON_INTERCEPT_FUNCTION(strxfrm); \
  COMMON_INTERCEPT_FUNCTION(strxfrm_l);
#else
#define INIT_STRXFRM
#endif

#if SANITIZER_INTERCEPT___STRXFRM_L
INTERCEPTOR(SIZE_T, __strxfrm_l, char *dest, const char *src, SIZE_T len,
            void *locale) {
  STRXFRM_INTERCEPTOR_IMPL(__strxfrm_l, dest, src, len, locale);
}

#define INIT___STRXFRM_L COMMON_INTERCEPT_FUNCTION(__strxfrm_l);
#else
#define INIT___STRXFRM_L
#endif

#if SANITIZER_INTERCEPT_WCSXFRM
INTERCEPTOR(SIZE_T, wcsxfrm, wchar_t *dest, const wchar_t *src, SIZE_T len) {
  STRXFRM_INTERCEPTOR_IMPL(wcsxfrm, dest, src, len);
}

INTERCEPTOR(SIZE_T, wcsxfrm_l, wchar_t *dest, const wchar_t *src, SIZE_T len,
            void *locale) {
  STRXFRM_INTERCEPTOR_IMPL(wcsxfrm_l, dest, src, len, locale);
}

#define INIT_WCSXFRM                  \
  COMMON_INTERCEPT_FUNCTION(wcsxfrm); \
  COMMON_INTERCEPT_FUNCTION(wcsxfrm_l);
#else
#define INIT_WCSXFRM
#endif

#if SANITIZER_INTERCEPT___WCSXFRM_L
INTERCEPTOR(SIZE_T, __wcsxfrm_l, wchar_t *dest, const wchar_t *src, SIZE_T len,
            void *locale) {
  STRXFRM_INTERCEPTOR_IMPL(__wcsxfrm_l, dest, src, len, locale);
}

#define INIT___WCSXFRM_L COMMON_INTERCEPT_FUNCTION(__wcsxfrm_l);
#else
#define INIT___WCSXFRM_L
#endif

#if SANITIZER_INTERCEPT_ACCT
INTERCEPTOR(int, acct, const char *file) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, acct, file);
  if (file)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, file, REAL(strlen)(file) + 1);
  return REAL(acct)(file);
}
#define INIT_ACCT COMMON_INTERCEPT_FUNCTION(acct)
#else
#define INIT_ACCT
#endif

#if SANITIZER_INTERCEPT_USER_FROM_UID
INTERCEPTOR(const char *, user_from_uid, u32 uid, int nouser) {
  void *ctx;
  const char *user;
  COMMON_INTERCEPTOR_ENTER(ctx, user_from_uid, uid, nouser);
  user = REAL(user_from_uid)(uid, nouser);
  if (user)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, user, REAL(strlen)(user) + 1);
  return user;
}
#define INIT_USER_FROM_UID COMMON_INTERCEPT_FUNCTION(user_from_uid)
#else
#define INIT_USER_FROM_UID
#endif

#if SANITIZER_INTERCEPT_UID_FROM_USER
INTERCEPTOR(int, uid_from_user, const char *name, u32 *uid) {
  void *ctx;
  int res;
  COMMON_INTERCEPTOR_ENTER(ctx, uid_from_user, name, uid);
  if (name)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  res = REAL(uid_from_user)(name, uid);
  if (uid)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, uid, sizeof(*uid));
  return res;
}
#define INIT_UID_FROM_USER COMMON_INTERCEPT_FUNCTION(uid_from_user)
#else
#define INIT_UID_FROM_USER
#endif

#if SANITIZER_INTERCEPT_GROUP_FROM_GID
INTERCEPTOR(const char *, group_from_gid, u32 gid, int nogroup) {
  void *ctx;
  const char *group;
  COMMON_INTERCEPTOR_ENTER(ctx, group_from_gid, gid, nogroup);
  group = REAL(group_from_gid)(gid, nogroup);
  if (group)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, group, REAL(strlen)(group) + 1);
  return group;
}
#define INIT_GROUP_FROM_GID COMMON_INTERCEPT_FUNCTION(group_from_gid)
#else
#define INIT_GROUP_FROM_GID
#endif

#if SANITIZER_INTERCEPT_GID_FROM_GROUP
INTERCEPTOR(int, gid_from_group, const char *group, u32 *gid) {
  void *ctx;
  int res;
  COMMON_INTERCEPTOR_ENTER(ctx, gid_from_group, group, gid);
  if (group)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, group, REAL(strlen)(group) + 1);
  res = REAL(gid_from_group)(group, gid);
  if (gid)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, gid, sizeof(*gid));
  return res;
}
#define INIT_GID_FROM_GROUP COMMON_INTERCEPT_FUNCTION(gid_from_group)
#else
#define INIT_GID_FROM_GROUP
#endif

#if SANITIZER_INTERCEPT_ACCESS
INTERCEPTOR(int, access, const char *path, int mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, access, path, mode);
  if (path)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  return REAL(access)(path, mode);
}
#define INIT_ACCESS COMMON_INTERCEPT_FUNCTION(access)
#else
#define INIT_ACCESS
#endif

#if SANITIZER_INTERCEPT_FACCESSAT
INTERCEPTOR(int, faccessat, int fd, const char *path, int mode, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, faccessat, fd, path, mode, flags);
  if (path)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  return REAL(faccessat)(fd, path, mode, flags);
}
#define INIT_FACCESSAT COMMON_INTERCEPT_FUNCTION(faccessat)
#else
#define INIT_FACCESSAT
#endif

#if SANITIZER_INTERCEPT_GETGROUPLIST
INTERCEPTOR(int, getgrouplist, const char *name, u32 basegid, u32 *groups,
            int *ngroups) {
  void *ctx;
  int res;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrouplist, name, basegid, groups, ngroups);
  if (name)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  if (ngroups)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ngroups, sizeof(*ngroups));
  res = REAL(getgrouplist)(name, basegid, groups, ngroups);
  if (!res && groups && ngroups) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, groups, sizeof(*groups) * (*ngroups));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ngroups, sizeof(*ngroups));
  }
  return res;
}

#define INIT_GETGROUPLIST COMMON_INTERCEPT_FUNCTION(getgrouplist);
#else
#define INIT_GETGROUPLIST
#endif

#if SANITIZER_INTERCEPT_GETGROUPMEMBERSHIP
INTERCEPTOR(int, getgroupmembership, const char *name, u32 basegid, u32 *groups,
            int maxgrp, int *ngroups) {
  void *ctx;
  int res;
  COMMON_INTERCEPTOR_ENTER(ctx, getgroupmembership, name, basegid, groups,
                           maxgrp, ngroups);
  if (name)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  res = REAL(getgroupmembership)(name, basegid, groups, maxgrp, ngroups);
  if (!res && groups && ngroups) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, groups, sizeof(*groups) * (*ngroups));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ngroups, sizeof(*ngroups));
  }
  return res;
}

#define INIT_GETGROUPMEMBERSHIP COMMON_INTERCEPT_FUNCTION(getgroupmembership);
#else
#define INIT_GETGROUPMEMBERSHIP
#endif

#if SANITIZER_INTERCEPT_READLINK
INTERCEPTOR(SSIZE_T, readlink, const char *path, char *buf, SIZE_T bufsiz) {
  void* ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, readlink, path, buf, bufsiz);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  SSIZE_T res = REAL(readlink)(path, buf, bufsiz);
  if (res > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, res);
  return res;
}

#define INIT_READLINK COMMON_INTERCEPT_FUNCTION(readlink)
#else
#define INIT_READLINK
#endif

#if SANITIZER_INTERCEPT_READLINKAT
INTERCEPTOR(SSIZE_T, readlinkat, int dirfd, const char *path, char *buf,
            SIZE_T bufsiz) {
  void* ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, readlinkat, dirfd, path, buf, bufsiz);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  SSIZE_T res = REAL(readlinkat)(dirfd, path, buf, bufsiz);
  if (res > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, res);
  return res;
}

#define INIT_READLINKAT COMMON_INTERCEPT_FUNCTION(readlinkat)
#else
#define INIT_READLINKAT
#endif

#if SANITIZER_INTERCEPT_NAME_TO_HANDLE_AT
INTERCEPTOR(int, name_to_handle_at, int dirfd, const char *pathname,
            struct file_handle *handle, int *mount_id, int flags) {
  void* ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, name_to_handle_at, dirfd, pathname, handle,
                           mount_id, flags);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, pathname, REAL(strlen)(pathname) + 1);

  __sanitizer_file_handle *sanitizer_handle =
      reinterpret_cast<__sanitizer_file_handle*>(handle);
  COMMON_INTERCEPTOR_READ_RANGE(
      ctx, &sanitizer_handle->handle_bytes,
      sizeof(sanitizer_handle->handle_bytes));

  int res = REAL(name_to_handle_at)(dirfd, pathname, handle, mount_id, flags);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(
        ctx, &sanitizer_handle->handle_bytes,
        sizeof(sanitizer_handle->handle_bytes));
    COMMON_INTERCEPTOR_WRITE_RANGE(
        ctx, &sanitizer_handle->handle_type,
        sizeof(sanitizer_handle->handle_type));
    COMMON_INTERCEPTOR_WRITE_RANGE(
        ctx, &sanitizer_handle->f_handle, sanitizer_handle->handle_bytes);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, mount_id, sizeof(*mount_id));
  }
  return res;
}

#define INIT_NAME_TO_HANDLE_AT COMMON_INTERCEPT_FUNCTION(name_to_handle_at)
#else
#define INIT_NAME_TO_HANDLE_AT
#endif

#if SANITIZER_INTERCEPT_OPEN_BY_HANDLE_AT
INTERCEPTOR(int, open_by_handle_at, int mount_fd, struct file_handle* handle,
            int flags) {
  void* ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, open_by_handle_at, mount_fd, handle, flags);

  __sanitizer_file_handle *sanitizer_handle =
      reinterpret_cast<__sanitizer_file_handle*>(handle);
  COMMON_INTERCEPTOR_READ_RANGE(
      ctx, &sanitizer_handle->handle_bytes,
      sizeof(sanitizer_handle->handle_bytes));
  COMMON_INTERCEPTOR_READ_RANGE(
      ctx, &sanitizer_handle->handle_type,
      sizeof(sanitizer_handle->handle_type));
  COMMON_INTERCEPTOR_READ_RANGE(
      ctx, &sanitizer_handle->f_handle, sanitizer_handle->handle_bytes);

  return REAL(open_by_handle_at)(mount_fd, handle, flags);
}

#define INIT_OPEN_BY_HANDLE_AT COMMON_INTERCEPT_FUNCTION(open_by_handle_at)
#else
#define INIT_OPEN_BY_HANDLE_AT
#endif

#if SANITIZER_INTERCEPT_STRLCPY
INTERCEPTOR(SIZE_T, strlcpy, char *dst, char *src, SIZE_T size) {
  void *ctx;
  SIZE_T res;
  COMMON_INTERCEPTOR_ENTER(ctx, strlcpy, dst, src, size);
  if (src) {
    // Keep strnlen as macro argument, as macro may ignore it.
    COMMON_INTERCEPTOR_READ_STRING(
        ctx, src, Min(internal_strnlen(src, size), size - 1) + 1);
  }
  res = REAL(strlcpy)(dst, src, size);
  COMMON_INTERCEPTOR_COPY_STRING(ctx, dst, src, REAL(strlen)(dst) + 1);
  return res;
}

INTERCEPTOR(SIZE_T, strlcat, char *dst, char *src, SIZE_T size) {
  void *ctx;
  SIZE_T len = 0;
  COMMON_INTERCEPTOR_ENTER(ctx, strlcat, dst, src, size);
  // src is checked in the strlcpy() interceptor
  if (dst) {
    len = internal_strnlen(dst, size);
    COMMON_INTERCEPTOR_READ_STRING(ctx, dst, Min(len, size - 1) + 1);
  }
  // Reuse the rest of the code in the strlcpy() interceptor
  return WRAP(strlcpy)(dst + len, src, size - len) + len;
}
#define INIT_STRLCPY \
  COMMON_INTERCEPT_FUNCTION(strlcpy); \
  COMMON_INTERCEPT_FUNCTION(strlcat);
#else
#define INIT_STRLCPY
#endif

#if SANITIZER_INTERCEPT_MMAP
INTERCEPTOR(void *, mmap, void *addr, SIZE_T sz, int prot, int flags, int fd,
            OFF_T off) {
  void *ctx;
  if (common_flags()->detect_write_exec)
    ReportMmapWriteExec(prot);
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return (void *)internal_mmap(addr, sz, prot, flags, fd, off);
  COMMON_INTERCEPTOR_ENTER(ctx, mmap, addr, sz, prot, flags, fd, off);
  COMMON_INTERCEPTOR_MMAP_IMPL(ctx, mmap, addr, sz, prot, flags, fd, off);
}

INTERCEPTOR(int, mprotect, void *addr, SIZE_T sz, int prot) {
  void *ctx;
  if (common_flags()->detect_write_exec)
    ReportMmapWriteExec(prot);
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return (int)internal_mprotect(addr, sz, prot);
  COMMON_INTERCEPTOR_ENTER(ctx, mprotect, addr, sz, prot);
  MprotectMallocZones(addr, prot);
  return REAL(mprotect)(addr, sz, prot);
}
#define INIT_MMAP                                                              \
  COMMON_INTERCEPT_FUNCTION(mmap);                                             \
  COMMON_INTERCEPT_FUNCTION(mprotect);
#else
#define INIT_MMAP
#endif

#if SANITIZER_INTERCEPT_MMAP64
INTERCEPTOR(void *, mmap64, void *addr, SIZE_T sz, int prot, int flags, int fd,
            OFF64_T off) {
  void *ctx;
  if (common_flags()->detect_write_exec)
    ReportMmapWriteExec(prot);
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return (void *)internal_mmap(addr, sz, prot, flags, fd, off);
  COMMON_INTERCEPTOR_ENTER(ctx, mmap64, addr, sz, prot, flags, fd, off);
  COMMON_INTERCEPTOR_MMAP_IMPL(ctx, mmap64, addr, sz, prot, flags, fd, off);
}
#define INIT_MMAP64 COMMON_INTERCEPT_FUNCTION(mmap64);
#else
#define INIT_MMAP64
#endif

#if SANITIZER_INTERCEPT_DEVNAME
INTERCEPTOR(char *, devname, u64 dev, u32 type) {
  void *ctx;
  char *name;
  COMMON_INTERCEPTOR_ENTER(ctx, devname, dev, type);
  name = REAL(devname)(dev, type);
  if (name)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, name, REAL(strlen)(name) + 1);
  return name;
}
#define INIT_DEVNAME COMMON_INTERCEPT_FUNCTION(devname);
#else
#define INIT_DEVNAME
#endif

#if SANITIZER_INTERCEPT_DEVNAME_R
#if SANITIZER_NETBSD
#define DEVNAME_R_RETTYPE int
#define DEVNAME_R_SUCCESS(x) (!(x))
#else
#define DEVNAME_R_RETTYPE char*
#define DEVNAME_R_SUCCESS(x) (x)
#endif
INTERCEPTOR(DEVNAME_R_RETTYPE, devname_r, u64 dev, u32 type, char *path,
            uptr len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, devname_r, dev, type, path, len);
  DEVNAME_R_RETTYPE res = REAL(devname_r)(dev, type, path, len);
  if (DEVNAME_R_SUCCESS(res))
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, path, REAL(strlen)(path) + 1);
  return res;
}
#define INIT_DEVNAME_R COMMON_INTERCEPT_FUNCTION(devname_r);
#else
#define INIT_DEVNAME_R
#endif

#if SANITIZER_INTERCEPT_FGETLN
INTERCEPTOR(char *, fgetln, __sanitizer_FILE *stream, SIZE_T *len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fgetln, stream, len);
  char *str = REAL(fgetln)(stream, len);
  if (str && len) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, len, sizeof(*len));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, str, *len);
  }
  return str;
}
#define INIT_FGETLN COMMON_INTERCEPT_FUNCTION(fgetln)
#else
#define INIT_FGETLN
#endif

#if SANITIZER_INTERCEPT_STRMODE
INTERCEPTOR(void, strmode, u32 mode, char *bp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strmode, mode, bp);
  REAL(strmode)(mode, bp);
  if (bp)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, bp, REAL(strlen)(bp) + 1);
}
#define INIT_STRMODE COMMON_INTERCEPT_FUNCTION(strmode)
#else
#define INIT_STRMODE
#endif

#if SANITIZER_INTERCEPT_TTYENT
INTERCEPTOR(struct __sanitizer_ttyent *, getttyent, void) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getttyent);
  struct __sanitizer_ttyent *ttyent = REAL(getttyent)();
  if (ttyent)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ttyent, struct_ttyent_sz);
  return ttyent;
}
INTERCEPTOR(struct __sanitizer_ttyent *, getttynam, char *name) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getttynam, name);
  if (name)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  struct __sanitizer_ttyent *ttyent = REAL(getttynam)(name);
  if (ttyent)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ttyent, struct_ttyent_sz);
  return ttyent;
}
INTERCEPTOR(int, setttyentpath, char *path) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setttyentpath, path);
  if (path)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  return REAL(setttyentpath)(path);
}
#define INIT_TTYENT \
  COMMON_INTERCEPT_FUNCTION(getttyent); \
  COMMON_INTERCEPT_FUNCTION(getttynam); \
  COMMON_INTERCEPT_FUNCTION(setttyentpath)
#else
#define INIT_TTYENT
#endif

#if SANITIZER_INTERCEPT_PROTOENT
INTERCEPTOR(struct __sanitizer_protoent *, getprotoent) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getprotoent);
  struct __sanitizer_protoent *p = REAL(getprotoent)();
  if (p) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, sizeof(*p));

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->p_name, REAL(strlen)(p->p_name) + 1);

    SIZE_T pp_size = 1; // One handles the trailing \0

    for (char **pp = p->p_aliases; *pp; ++pp, ++pp_size)
       COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *pp, REAL(strlen)(*pp) + 1);

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->p_aliases,
                                   pp_size * sizeof(char **));
  }
  return p;
}

INTERCEPTOR(struct __sanitizer_protoent *, getprotobyname, const char *name) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getprotobyname, name);
  if (name)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  struct __sanitizer_protoent *p = REAL(getprotobyname)(name);
  if (p) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, sizeof(*p));

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->p_name, REAL(strlen)(p->p_name) + 1);

    SIZE_T pp_size = 1; // One handles the trailing \0

    for (char **pp = p->p_aliases; *pp; ++pp, ++pp_size)
       COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *pp, REAL(strlen)(*pp) + 1);

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->p_aliases,
                                   pp_size * sizeof(char **));
  }
  return p;
}

INTERCEPTOR(struct __sanitizer_protoent *, getprotobynumber, int proto) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getprotobynumber, proto);
  struct __sanitizer_protoent *p = REAL(getprotobynumber)(proto);
  if (p) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, sizeof(*p));

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->p_name, REAL(strlen)(p->p_name) + 1);

    SIZE_T pp_size = 1; // One handles the trailing \0

    for (char **pp = p->p_aliases; *pp; ++pp, ++pp_size)
       COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *pp, REAL(strlen)(*pp) + 1);

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p->p_aliases,
                                   pp_size * sizeof(char **));
  }
  return p;
}
#define INIT_PROTOENT \
  COMMON_INTERCEPT_FUNCTION(getprotoent); \
  COMMON_INTERCEPT_FUNCTION(getprotobyname); \
  COMMON_INTERCEPT_FUNCTION(getprotobynumber)
#else
#define INIT_PROTOENT
#endif

#if SANITIZER_INTERCEPT_NETENT
INTERCEPTOR(struct __sanitizer_netent *, getnetent) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getnetent);
  struct __sanitizer_netent *n = REAL(getnetent)();
  if (n) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n, sizeof(*n));

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n->n_name, REAL(strlen)(n->n_name) + 1);

    SIZE_T nn_size = 1; // One handles the trailing \0

    for (char **nn = n->n_aliases; *nn; ++nn, ++nn_size)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *nn, REAL(strlen)(*nn) + 1);

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n->n_aliases,
                                   nn_size * sizeof(char **));
  }
  return n;
}

INTERCEPTOR(struct __sanitizer_netent *, getnetbyname, const char *name) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getnetbyname, name);
  if (name)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  struct __sanitizer_netent *n = REAL(getnetbyname)(name);
  if (n) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n, sizeof(*n));

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n->n_name, REAL(strlen)(n->n_name) + 1);

    SIZE_T nn_size = 1; // One handles the trailing \0

    for (char **nn = n->n_aliases; *nn; ++nn, ++nn_size)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *nn, REAL(strlen)(*nn) + 1);

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n->n_aliases,
                                   nn_size * sizeof(char **));
  }
  return n;
}

INTERCEPTOR(struct __sanitizer_netent *, getnetbyaddr, u32 net, int type) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getnetbyaddr, net, type);
  struct __sanitizer_netent *n = REAL(getnetbyaddr)(net, type);
  if (n) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n, sizeof(*n));

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n->n_name, REAL(strlen)(n->n_name) + 1);

    SIZE_T nn_size = 1; // One handles the trailing \0

    for (char **nn = n->n_aliases; *nn; ++nn, ++nn_size)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *nn, REAL(strlen)(*nn) + 1);

    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, n->n_aliases,
                                   nn_size * sizeof(char **));
  }
  return n;
}
#define INIT_NETENT \
  COMMON_INTERCEPT_FUNCTION(getnetent); \
  COMMON_INTERCEPT_FUNCTION(getnetbyname); \
  COMMON_INTERCEPT_FUNCTION(getnetbyaddr)
#else
#define INIT_NETENT
#endif

#if SANITIZER_INTERCEPT_GETMNTINFO
INTERCEPTOR(int, getmntinfo, void **mntbufp, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getmntinfo, mntbufp, flags);
  int cnt = REAL(getmntinfo)(mntbufp, flags);
  if (cnt > 0 && mntbufp) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, mntbufp, sizeof(void *));
    if (*mntbufp)
#if SANITIZER_NETBSD
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *mntbufp, cnt * struct_statvfs_sz);
#else
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *mntbufp, cnt * struct_statfs_sz);
#endif
  }
  return cnt;
}
#define INIT_GETMNTINFO COMMON_INTERCEPT_FUNCTION(getmntinfo)
#else
#define INIT_GETMNTINFO
#endif

#if SANITIZER_INTERCEPT_MI_VECTOR_HASH
INTERCEPTOR(void, mi_vector_hash, const void *key, SIZE_T len, u32 seed,
            u32 hashes[3]) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, mi_vector_hash, key, len, seed, hashes);
  if (key)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, key, len);
  REAL(mi_vector_hash)(key, len, seed, hashes);
  if (hashes)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, hashes, sizeof(hashes[0]) * 3);
}
#define INIT_MI_VECTOR_HASH COMMON_INTERCEPT_FUNCTION(mi_vector_hash)
#else
#define INIT_MI_VECTOR_HASH
#endif

#if SANITIZER_INTERCEPT_SETVBUF
INTERCEPTOR(int, setvbuf, __sanitizer_FILE *stream, char *buf, int mode,
  SIZE_T size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setvbuf, stream, buf, mode, size);
  int ret = REAL(setvbuf)(stream, buf, mode, size);
  if (buf)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, size);
  if (stream)
      unpoison_file(stream);
  return ret;
}

INTERCEPTOR(void, setbuf, __sanitizer_FILE *stream, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setbuf, stream, buf);
  REAL(setbuf)(stream, buf);
  if (buf) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, __sanitizer_bufsiz);
  }
  if (stream)
      unpoison_file(stream);
}

INTERCEPTOR(void, setbuffer, __sanitizer_FILE *stream, char *buf, int mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setbuffer, stream, buf, mode);
  REAL(setbuffer)(stream, buf, mode);
  if (buf) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, __sanitizer_bufsiz);
  }
  if (stream)
    unpoison_file(stream);
}

INTERCEPTOR(void, setlinebuf, __sanitizer_FILE *stream) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setlinebuf, stream);
  REAL(setlinebuf)(stream);
  if (stream)
    unpoison_file(stream);
}
#define INIT_SETVBUF COMMON_INTERCEPT_FUNCTION(setvbuf); \
    COMMON_INTERCEPT_FUNCTION(setbuf); \
    COMMON_INTERCEPT_FUNCTION(setbuffer); \
    COMMON_INTERCEPT_FUNCTION(setlinebuf)
#else
#define INIT_SETVBUF
#endif

#if SANITIZER_INTERCEPT_GETVFSSTAT
INTERCEPTOR(int, getvfsstat, void *buf, SIZE_T bufsize, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getvfsstat, buf, bufsize, flags);
  int ret = REAL(getvfsstat)(buf, bufsize, flags);
  if (buf && ret > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, ret * struct_statvfs_sz);
  return ret;
}
#define INIT_GETVFSSTAT COMMON_INTERCEPT_FUNCTION(getvfsstat)
#else
#define INIT_GETVFSSTAT
#endif

#if SANITIZER_INTERCEPT_REGEX
INTERCEPTOR(int, regcomp, void *preg, const char *pattern, int cflags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, regcomp, preg, pattern, cflags);
  if (pattern)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, pattern, REAL(strlen)(pattern) + 1);
  int res = REAL(regcomp)(preg, pattern, cflags);
  if (!res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, preg, struct_regex_sz);
  return res;
}
INTERCEPTOR(int, regexec, const void *preg, const char *string, SIZE_T nmatch,
            struct __sanitizer_regmatch *pmatch[], int eflags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, regexec, preg, string, nmatch, pmatch, eflags);
  if (preg)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, preg, struct_regex_sz);
  if (string)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, string, REAL(strlen)(string) + 1);
  int res = REAL(regexec)(preg, string, nmatch, pmatch, eflags);
  if (!res && pmatch)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pmatch, nmatch * struct_regmatch_sz);
  return res;
}
INTERCEPTOR(SIZE_T, regerror, int errcode, const void *preg, char *errbuf,
            SIZE_T errbuf_size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, regerror, errcode, preg, errbuf, errbuf_size);
  if (preg)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, preg, struct_regex_sz);
  SIZE_T res = REAL(regerror)(errcode, preg, errbuf, errbuf_size);
  if (errbuf)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, errbuf, REAL(strlen)(errbuf) + 1);
  return res;
}
INTERCEPTOR(void, regfree, const void *preg) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, regfree, preg);
  if (preg)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, preg, struct_regex_sz);
  REAL(regfree)(preg);
}
#define INIT_REGEX                                                             \
  COMMON_INTERCEPT_FUNCTION(regcomp);                                          \
  COMMON_INTERCEPT_FUNCTION(regexec);                                          \
  COMMON_INTERCEPT_FUNCTION(regerror);                                         \
  COMMON_INTERCEPT_FUNCTION(regfree);
#else
#define INIT_REGEX
#endif

#if SANITIZER_INTERCEPT_REGEXSUB
INTERCEPTOR(SSIZE_T, regnsub, char *buf, SIZE_T bufsiz, const char *sub,
            const struct __sanitizer_regmatch *rm, const char *str) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, regnsub, buf, bufsiz, sub, rm, str);
  if (sub)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sub, REAL(strlen)(sub) + 1);
  // The implementation demands and hardcodes 10 elements
  if (rm)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, rm, 10 * struct_regmatch_sz);
  if (str)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, str, REAL(strlen)(str) + 1);
  SSIZE_T res = REAL(regnsub)(buf, bufsiz, sub, rm, str);
  if (res > 0 && buf)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, REAL(strlen)(buf) + 1);
  return res;
}
INTERCEPTOR(SSIZE_T, regasub, char **buf, const char *sub,
            const struct __sanitizer_regmatch *rm, const char *sstr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, regasub, buf, sub, rm, sstr);
  if (sub)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sub, REAL(strlen)(sub) + 1);
  // Hardcode 10 elements as this is hardcoded size
  if (rm)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, rm, 10 * struct_regmatch_sz);
  if (sstr)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sstr, REAL(strlen)(sstr) + 1);
  SSIZE_T res = REAL(regasub)(buf, sub, rm, sstr);
  if (res > 0 && buf) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, sizeof(char *));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *buf, REAL(strlen)(*buf) + 1);
  }
  return res;
}

#define INIT_REGEXSUB                                                          \
  COMMON_INTERCEPT_FUNCTION(regnsub);                                          \
  COMMON_INTERCEPT_FUNCTION(regasub);
#else
#define INIT_REGEXSUB
#endif

#if SANITIZER_INTERCEPT_FTS
INTERCEPTOR(void *, fts_open, char *const *path_argv, int options,
            int (*compar)(void **, void **)) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fts_open, path_argv, options, compar);
  if (path_argv) {
    for (char *const *pa = path_argv; ; ++pa) {
      COMMON_INTERCEPTOR_READ_RANGE(ctx, pa, sizeof(char **));
      if (!*pa)
        break;
      COMMON_INTERCEPTOR_READ_RANGE(ctx, *pa, REAL(strlen)(*pa) + 1);
    }
  }
  // TODO(kamil): handle compar callback
  void *fts = REAL(fts_open)(path_argv, options, compar);
  if (fts)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, fts, struct_FTS_sz);
  return fts;
}

INTERCEPTOR(void *, fts_read, void *ftsp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fts_read, ftsp);
  if (ftsp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ftsp, struct_FTS_sz);
  void *ftsent = REAL(fts_read)(ftsp);
  if (ftsent)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ftsent, struct_FTSENT_sz);
  return ftsent;
}

INTERCEPTOR(void *, fts_children, void *ftsp, int options) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fts_children, ftsp, options);
  if (ftsp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ftsp, struct_FTS_sz);
  void *ftsent = REAL(fts_children)(ftsp, options);
  if (ftsent)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ftsent, struct_FTSENT_sz);
  return ftsent;
}

INTERCEPTOR(int, fts_set, void *ftsp, void *f, int options) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fts_set, ftsp, f, options);
  if (ftsp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ftsp, struct_FTS_sz);
  if (f)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, f, struct_FTSENT_sz);
  return REAL(fts_set)(ftsp, f, options);
}

INTERCEPTOR(int, fts_close, void *ftsp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fts_close, ftsp);
  if (ftsp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ftsp, struct_FTS_sz);
  return REAL(fts_close)(ftsp);
}
#define INIT_FTS                                                               \
  COMMON_INTERCEPT_FUNCTION(fts_open);                                         \
  COMMON_INTERCEPT_FUNCTION(fts_read);                                         \
  COMMON_INTERCEPT_FUNCTION(fts_children);                                     \
  COMMON_INTERCEPT_FUNCTION(fts_set);                                          \
  COMMON_INTERCEPT_FUNCTION(fts_close);
#else
#define INIT_FTS
#endif

#if SANITIZER_INTERCEPT_SYSCTL
INTERCEPTOR(int, sysctl, int *name, unsigned int namelen, void *oldp,
            SIZE_T *oldlenp, void *newp, SIZE_T newlen) {
  void *ctx;
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_sysctl(name, namelen, oldp, oldlenp, newp, newlen);
  COMMON_INTERCEPTOR_ENTER(ctx, sysctl, name, namelen, oldp, oldlenp, newp,
                           newlen);
  if (name)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, name, namelen * sizeof(*name));
  if (oldlenp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, oldlenp, sizeof(*oldlenp));
  if (newp && newlen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, newp, newlen);
  int res = REAL(sysctl)(name, namelen, oldp, oldlenp, newp, newlen);
  if (!res) {
    if (oldlenp) {
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, oldlenp, sizeof(*oldlenp));
      if (oldp)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, oldp, *oldlenp);
    }
  }
  return res;
}

INTERCEPTOR(int, sysctlbyname, char *sname, void *oldp, SIZE_T *oldlenp,
            void *newp, SIZE_T newlen) {
  void *ctx;
  if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)
    return internal_sysctlbyname(sname, oldp, oldlenp, newp, newlen);
  COMMON_INTERCEPTOR_ENTER(ctx, sysctlbyname, sname, oldp, oldlenp, newp,
                           newlen);
  if (sname)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sname, REAL(strlen)(sname) + 1);
  if (oldlenp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, oldlenp, sizeof(*oldlenp));
  if (newp && newlen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, newp, newlen);
  int res = REAL(sysctlbyname)(sname, oldp, oldlenp, newp, newlen);
  if (!res) {
    if (oldlenp) {
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, oldlenp, sizeof(*oldlenp));
      if (oldp)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, oldp, *oldlenp);
    }
  }
  return res;
}

INTERCEPTOR(int, sysctlnametomib, const char *sname, int *name,
            SIZE_T *namelenp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sysctlnametomib, sname, name, namelenp);
  if (sname)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sname, REAL(strlen)(sname) + 1);
  if (namelenp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, namelenp, sizeof(*namelenp));
  int res = REAL(sysctlnametomib)(sname, name, namelenp);
  if (!res) {
    if (namelenp) {
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, namelenp, sizeof(*namelenp));
      if (name)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, name, *namelenp * sizeof(*name));
    }
  }
  return res;
}

#define INIT_SYSCTL                        \
  COMMON_INTERCEPT_FUNCTION(sysctl);       \
  COMMON_INTERCEPT_FUNCTION(sysctlbyname); \
  COMMON_INTERCEPT_FUNCTION(sysctlnametomib);
#else
#define INIT_SYSCTL
#endif

#if SANITIZER_INTERCEPT_ASYSCTL
INTERCEPTOR(void *, asysctl, const int *name, SIZE_T namelen, SIZE_T *len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, asysctl, name, namelen, len);
  if (name)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, name, sizeof(*name) * namelen);
  void *res = REAL(asysctl)(name, namelen, len);
  if (res && len) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, len, sizeof(*len));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, *len);
  }
  return res;
}

INTERCEPTOR(void *, asysctlbyname, const char *sname, SIZE_T *len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, asysctlbyname, sname, len);
  if (sname)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sname, REAL(strlen)(sname) + 1);
  void *res = REAL(asysctlbyname)(sname, len);
  if (res && len) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, len, sizeof(*len));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, *len);
  }
  return res;
}
#define INIT_ASYSCTL                           \
  COMMON_INTERCEPT_FUNCTION(asysctl);          \
  COMMON_INTERCEPT_FUNCTION(asysctlbyname);
#else
#define INIT_ASYSCTL
#endif

#if SANITIZER_INTERCEPT_SYSCTLGETMIBINFO
INTERCEPTOR(int, sysctlgetmibinfo, char *sname, int *name,
            unsigned int *namelenp, char *cname, SIZE_T *csz, void **rnode,
            int v) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sysctlgetmibinfo, sname, name, namelenp, cname,
                           csz, rnode, v);
  if (sname)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sname, REAL(strlen)(sname) + 1);
  if (namelenp)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, namelenp, sizeof(*namelenp));
  if (csz)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, csz, sizeof(*csz));
  // Skip rnode, it's rarely used and not trivial to sanitize
  // It's also used mostly internally
  int res = REAL(sysctlgetmibinfo)(sname, name, namelenp, cname, csz, rnode, v);
  if (!res) {
    if (namelenp) {
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, namelenp, sizeof(*namelenp));
      if (name)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, name, *namelenp * sizeof(*name));
    }
    if (csz) {
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, csz, sizeof(*csz));
      if (cname)
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cname, *csz);
    }
  }
  return res;
}
#define INIT_SYSCTLGETMIBINFO                  \
  COMMON_INTERCEPT_FUNCTION(sysctlgetmibinfo);
#else
#define INIT_SYSCTLGETMIBINFO
#endif

#if SANITIZER_INTERCEPT_NL_LANGINFO
INTERCEPTOR(char *, nl_langinfo, long item) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, nl_langinfo, item);
  char *ret = REAL(nl_langinfo)(item);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, REAL(strlen)(ret) + 1);
  return ret;
}
#define INIT_NL_LANGINFO COMMON_INTERCEPT_FUNCTION(nl_langinfo)
#else
#define INIT_NL_LANGINFO
#endif

#if SANITIZER_INTERCEPT_MODCTL
INTERCEPTOR(int, modctl, int operation, void *argp) {
  void *ctx;
  int ret;
  COMMON_INTERCEPTOR_ENTER(ctx, modctl, operation, argp);

  if (operation == modctl_load) {
    if (argp) {
      __sanitizer_modctl_load_t *ml = (__sanitizer_modctl_load_t *)argp;
      COMMON_INTERCEPTOR_READ_RANGE(ctx, ml, sizeof(*ml));
      if (ml->ml_filename)
        COMMON_INTERCEPTOR_READ_RANGE(ctx, ml->ml_filename,
                                      REAL(strlen)(ml->ml_filename) + 1);
      if (ml->ml_props)
        COMMON_INTERCEPTOR_READ_RANGE(ctx, ml->ml_props, ml->ml_propslen);
    }
    ret = REAL(modctl)(operation, argp);
  } else if (operation == modctl_unload) {
    if (argp) {
      const char *name = (const char *)argp;
      COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
    }
    ret = REAL(modctl)(operation, argp);
  } else if (operation == modctl_stat) {
    uptr iov_len;
    struct __sanitizer_iovec *iov = (struct __sanitizer_iovec *)argp;
    if (iov) {
      COMMON_INTERCEPTOR_READ_RANGE(ctx, iov, sizeof(*iov));
      iov_len = iov->iov_len;
    }
    ret = REAL(modctl)(operation, argp);
    if (iov)
      COMMON_INTERCEPTOR_WRITE_RANGE(
          ctx, iov->iov_base, Min(iov_len,  iov->iov_len));
  } else if (operation == modctl_exists)
    ret = REAL(modctl)(operation, argp);
  else
    ret = REAL(modctl)(operation, argp);

  return ret;
}
#define INIT_MODCTL COMMON_INTERCEPT_FUNCTION(modctl)
#else
#define INIT_MODCTL
#endif

#if SANITIZER_INTERCEPT_STRTONUM
INTERCEPTOR(long long, strtonum, const char *nptr, long long minval,
            long long maxval, const char **errstr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strtonum, nptr, minval, maxval, errstr);

  // TODO(kamil): Implement strtoll as a common inteceptor
  char *real_endptr;
  long long ret = (long long)REAL(strtoimax)(nptr, &real_endptr, 10);
  StrtolFixAndCheck(ctx, nptr, nullptr, real_endptr, 10);

  ret = REAL(strtonum)(nptr, minval, maxval, errstr);
  if (errstr) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, errstr, sizeof(const char *));
     if (*errstr)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *errstr, REAL(strlen)(*errstr) + 1);
  }
  return ret;
}
#define INIT_STRTONUM COMMON_INTERCEPT_FUNCTION(strtonum)
#else
#define INIT_STRTONUM
#endif

#if SANITIZER_INTERCEPT_FPARSELN
INTERCEPTOR(char *, fparseln, __sanitizer_FILE *stream, SIZE_T *len,
            SIZE_T *lineno, const char delim[3], int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fparseln, stream, len, lineno, delim, flags);
  if (lineno)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, lineno, sizeof(*lineno));
  if (delim)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, delim, sizeof(delim[0]) * 3);
  char *ret = REAL(fparseln)(stream, len, lineno, delim, flags);
  if (ret) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, REAL(strlen)(ret) + 1);
    if (len)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, len, sizeof(*len));
    if (lineno)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, lineno, sizeof(*lineno));
  }
  return ret;
}
#define INIT_FPARSELN COMMON_INTERCEPT_FUNCTION(fparseln)
#else
#define INIT_FPARSELN
#endif

#if SANITIZER_INTERCEPT_STATVFS1
INTERCEPTOR(int, statvfs1, const char *path, void *buf, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, statvfs1, path, buf, flags);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  int res = REAL(statvfs1)(path, buf, flags);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs_sz);
  return res;
}
INTERCEPTOR(int, fstatvfs1, int fd, void *buf, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fstatvfs1, fd, buf, flags);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  int res = REAL(fstatvfs1)(fd, buf, flags);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs_sz);
    if (fd >= 0)
      COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  }
  return res;
}
#define INIT_STATVFS1                  \
  COMMON_INTERCEPT_FUNCTION(statvfs1);  \
  COMMON_INTERCEPT_FUNCTION(fstatvfs1);
#else
#define INIT_STATVFS1
#endif

#if SANITIZER_INTERCEPT_STRTOI
INTERCEPTOR(INTMAX_T, strtoi, const char *nptr, char **endptr, int base,
            INTMAX_T low, INTMAX_T high, int *rstatus) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strtoi, nptr, endptr, base, low, high, rstatus);
  char *real_endptr;
  INTMAX_T ret = REAL(strtoi)(nptr, &real_endptr, base, low, high, rstatus);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  if (rstatus)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, rstatus, sizeof(*rstatus));
  return ret;
}

INTERCEPTOR(UINTMAX_T, strtou, const char *nptr, char **endptr, int base,
            UINTMAX_T low, UINTMAX_T high, int *rstatus) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strtou, nptr, endptr, base, low, high, rstatus);
  char *real_endptr;
  UINTMAX_T ret = REAL(strtou)(nptr, &real_endptr, base, low, high, rstatus);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  if (rstatus)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, rstatus, sizeof(*rstatus));
  return ret;
}
#define INIT_STRTOI                                                            \
  COMMON_INTERCEPT_FUNCTION(strtoi);                                           \
  COMMON_INTERCEPT_FUNCTION(strtou)
#else
#define INIT_STRTOI
#endif

#if SANITIZER_INTERCEPT_CAPSICUM
#define CAP_RIGHTS_INIT_INTERCEPTOR(cap_rights_init, rights, ...)          \
  {                                                                        \
    void *ctx;                                                             \
    COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_init, rights, ##__VA_ARGS__); \
    if (rights)                                                            \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, rights, sizeof(*rights));         \
    __sanitizer_cap_rights_t *ret =                                        \
        REAL(cap_rights_init)(rights, ##__VA_ARGS__);                      \
    if (ret)                                                               \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, sizeof(*ret));              \
    return ret;                                                            \
  }

#define CAP_RIGHTS_SET_INTERCEPTOR(cap_rights_set, rights, ...)           \
  {                                                                       \
    void *ctx;                                                            \
    COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_set, rights, ##__VA_ARGS__); \
    if (rights)                                                           \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, rights, sizeof(*rights));        \
    __sanitizer_cap_rights_t *ret =                                       \
        REAL(cap_rights_set)(rights, ##__VA_ARGS__);                      \
    if (ret)                                                              \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, sizeof(*ret));             \
    return ret;                                                           \
  }

#define CAP_RIGHTS_CLEAR_INTERCEPTOR(cap_rights_clear, rights, ...)         \
  {                                                                         \
    void *ctx;                                                              \
    COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_clear, rights, ##__VA_ARGS__); \
    if (rights)                                                             \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, rights, sizeof(*rights));          \
    __sanitizer_cap_rights_t *ret =                                         \
        REAL(cap_rights_clear)(rights, ##__VA_ARGS__);                      \
    if (ret)                                                                \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, sizeof(*ret));               \
    return ret;                                                             \
  }

#define CAP_RIGHTS_IS_SET_INTERCEPTOR(cap_rights_is_set, rights, ...)        \
  {                                                                          \
    void *ctx;                                                               \
    COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_is_set, rights, ##__VA_ARGS__); \
    if (rights)                                                              \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, rights, sizeof(*rights));           \
    return REAL(cap_rights_is_set)(rights, ##__VA_ARGS__);                   \
  }

INTERCEPTOR(__sanitizer_cap_rights_t *, cap_rights_init,
            __sanitizer_cap_rights_t *rights) {
  CAP_RIGHTS_INIT_INTERCEPTOR(cap_rights_init, rights);
}

INTERCEPTOR(__sanitizer_cap_rights_t *, cap_rights_set,
            __sanitizer_cap_rights_t *rights) {
  CAP_RIGHTS_SET_INTERCEPTOR(cap_rights_set, rights);
}

INTERCEPTOR(__sanitizer_cap_rights_t *, cap_rights_clear,
            __sanitizer_cap_rights_t *rights) {
  CAP_RIGHTS_CLEAR_INTERCEPTOR(cap_rights_clear, rights);
}

INTERCEPTOR(bool, cap_rights_is_set,
            __sanitizer_cap_rights_t *rights) {
  CAP_RIGHTS_IS_SET_INTERCEPTOR(cap_rights_is_set, rights);
}

INTERCEPTOR(int, cap_rights_limit, int fd,
            const __sanitizer_cap_rights_t *rights) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_limit, fd, rights);
  if (rights)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, rights, sizeof(*rights));

  return REAL(cap_rights_limit)(fd, rights);
}

INTERCEPTOR(int, cap_rights_get, int fd, __sanitizer_cap_rights_t *rights) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_get, fd, rights);
  int ret = REAL(cap_rights_get)(fd, rights);
  if (!ret && rights)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, rights, sizeof(*rights));

  return ret;
}

INTERCEPTOR(bool, cap_rights_is_valid, const __sanitizer_cap_rights_t *rights) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_is_valid, rights);
  if (rights)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, rights, sizeof(*rights));

  return REAL(cap_rights_is_valid(rights));
}

INTERCEPTOR(__sanitizer_cap_rights *, cap_rights_merge,
  __sanitizer_cap_rights *dst, const __sanitizer_cap_rights *src) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_merge, dst, src);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, sizeof(*src));

  __sanitizer_cap_rights *ret = REAL(cap_rights_merge)(dst, src);
  if (dst)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, sizeof(*dst));

  return ret;
}

INTERCEPTOR(__sanitizer_cap_rights *, cap_rights_remove,
  __sanitizer_cap_rights *dst, const __sanitizer_cap_rights *src) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_remove, dst, src);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, sizeof(*src));

  __sanitizer_cap_rights *ret = REAL(cap_rights_remove)(dst, src);
  if (dst)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, sizeof(*dst));

  return ret;
}

INTERCEPTOR(bool, cap_rights_contains, const __sanitizer_cap_rights *big,
  const __sanitizer_cap_rights *little) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cap_rights_contains, big, little);
  if (little)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, little, sizeof(*little));
  if (big)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, big, sizeof(*big));

  return REAL(cap_rights_contains)(big, little);
}

INTERCEPTOR(int, cap_ioctls_limit, int fd, const uptr *cmds, SIZE_T ncmds) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cap_ioctls_limit, fd, cmds, ncmds);
  if (cmds)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cmds, sizeof(*cmds) * ncmds);

  return REAL(cap_ioctls_limit)(fd, cmds, ncmds);
}

INTERCEPTOR(int, cap_ioctls_get, int fd, uptr *cmds, SIZE_T maxcmds) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cap_ioctls_get, fd, cmds, maxcmds);
  int ret = REAL(cap_ioctls_get)(fd, cmds, maxcmds);
  if (!ret && cmds)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cmds, sizeof(*cmds) * maxcmds);

  return ret;
}
#define INIT_CAPSICUM                          \
  COMMON_INTERCEPT_FUNCTION(cap_rights_init); \
  COMMON_INTERCEPT_FUNCTION(cap_rights_set); \
  COMMON_INTERCEPT_FUNCTION(cap_rights_clear); \
  COMMON_INTERCEPT_FUNCTION(cap_rights_is_set); \
  COMMON_INTERCEPT_FUNCTION(cap_rights_get);   \
  COMMON_INTERCEPT_FUNCTION(cap_rights_limit); \
  COMMON_INTERCEPT_FUNCTION(cap_rights_contains); \
  COMMON_INTERCEPT_FUNCTION(cap_rights_remove); \
  COMMON_INTERCEPT_FUNCTION(cap_rights_merge); \
  COMMON_INTERCEPT_FUNCTION(cap_rights_is_valid); \
  COMMON_INTERCEPT_FUNCTION(cap_ioctls_get);   \
  COMMON_INTERCEPT_FUNCTION(cap_ioctls_limit)
#else
#define INIT_CAPSICUM
#endif

#if SANITIZER_INTERCEPT_SHA1
INTERCEPTOR(void, SHA1Init, void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, SHA1Init, context);
  REAL(SHA1Init)(context);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, SHA1_CTX_sz);
}
INTERCEPTOR(void, SHA1Update, void *context, const u8 *data, unsigned len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, SHA1Update, context, data, len);
  if (data && len > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, SHA1_CTX_sz);
  REAL(SHA1Update)(context, data, len);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, SHA1_CTX_sz);
}
INTERCEPTOR(void, SHA1Final, u8 digest[20], void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, SHA1Final, digest, context);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, SHA1_CTX_sz);
  REAL(SHA1Final)(digest, context);
  if (digest)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, digest, sizeof(u8) * 20);
}
INTERCEPTOR(void, SHA1Transform, u32 state[5], u8 buffer[64]) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, SHA1Transform, state, buffer);
  if (state)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, state, sizeof(u32) * 5);
  if (buffer)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, buffer, sizeof(u8) * 64);
  REAL(SHA1Transform)(state, buffer);
  if (state)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, state, sizeof(u32) * 5);
}
INTERCEPTOR(char *, SHA1End, void *context, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, SHA1End, context, buf);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, SHA1_CTX_sz);
  char *ret = REAL(SHA1End)(context, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, SHA1_return_length);
  return ret;
}
INTERCEPTOR(char *, SHA1File, char *filename, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, SHA1File, filename, buf);
  if (filename)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, filename, REAL(strlen)(filename) + 1);
  char *ret = REAL(SHA1File)(filename, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, SHA1_return_length);
  return ret;
}
INTERCEPTOR(char *, SHA1FileChunk, char *filename, char *buf, OFF_T offset,
  OFF_T length) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, SHA1FileChunk, filename, buf, offset, length);
  if (filename)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, filename, REAL(strlen)(filename) + 1);
  char *ret = REAL(SHA1FileChunk)(filename, buf, offset, length);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, SHA1_return_length);
  return ret;
}
INTERCEPTOR(char *, SHA1Data, u8 *data, SIZE_T len, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, SHA1Data, data, len, buf);
  if (data)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  char *ret = REAL(SHA1Data)(data, len, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, SHA1_return_length);
  return ret;
}
#define INIT_SHA1                                                              \
  COMMON_INTERCEPT_FUNCTION(SHA1Init);                                         \
  COMMON_INTERCEPT_FUNCTION(SHA1Update);                                       \
  COMMON_INTERCEPT_FUNCTION(SHA1Final);                                        \
  COMMON_INTERCEPT_FUNCTION(SHA1Transform);                                    \
  COMMON_INTERCEPT_FUNCTION(SHA1End);                                          \
  COMMON_INTERCEPT_FUNCTION(SHA1File);                                         \
  COMMON_INTERCEPT_FUNCTION(SHA1FileChunk);                                    \
  COMMON_INTERCEPT_FUNCTION(SHA1Data)
#else
#define INIT_SHA1
#endif

#if SANITIZER_INTERCEPT_MD4
INTERCEPTOR(void, MD4Init, void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD4Init, context);
  REAL(MD4Init)(context);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, MD4_CTX_sz);
}

INTERCEPTOR(void, MD4Update, void *context, const unsigned char *data,
            unsigned int len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD4Update, context, data, len);
  if (data && len > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, MD4_CTX_sz);
  REAL(MD4Update)(context, data, len);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, MD4_CTX_sz);
}

INTERCEPTOR(void, MD4Final, unsigned char digest[16], void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD4Final, digest, context);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, MD4_CTX_sz);
  REAL(MD4Final)(digest, context);
  if (digest)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, digest, sizeof(unsigned char) * 16);
}

INTERCEPTOR(char *, MD4End, void *context, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD4End, context, buf);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, MD4_CTX_sz);
  char *ret = REAL(MD4End)(context, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, MD4_return_length);
  return ret;
}

INTERCEPTOR(char *, MD4File, const char *filename, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD4File, filename, buf);
  if (filename)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, filename, REAL(strlen)(filename) + 1);
  char *ret = REAL(MD4File)(filename, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, MD4_return_length);
  return ret;
}

INTERCEPTOR(char *, MD4Data, const unsigned char *data, unsigned int len,
            char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD4Data, data, len, buf);
  if (data && len > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  char *ret = REAL(MD4Data)(data, len, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, MD4_return_length);
  return ret;
}

#define INIT_MD4                                                               \
  COMMON_INTERCEPT_FUNCTION(MD4Init);                                          \
  COMMON_INTERCEPT_FUNCTION(MD4Update);                                        \
  COMMON_INTERCEPT_FUNCTION(MD4Final);                                         \
  COMMON_INTERCEPT_FUNCTION(MD4End);                                           \
  COMMON_INTERCEPT_FUNCTION(MD4File);                                          \
  COMMON_INTERCEPT_FUNCTION(MD4Data)
#else
#define INIT_MD4
#endif

#if SANITIZER_INTERCEPT_RMD160
INTERCEPTOR(void, RMD160Init, void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, RMD160Init, context);
  REAL(RMD160Init)(context);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, RMD160_CTX_sz);
}
INTERCEPTOR(void, RMD160Update, void *context, const u8 *data, unsigned len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, RMD160Update, context, data, len);
  if (data && len > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, RMD160_CTX_sz);
  REAL(RMD160Update)(context, data, len);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, RMD160_CTX_sz);
}
INTERCEPTOR(void, RMD160Final, u8 digest[20], void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, RMD160Final, digest, context);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, RMD160_CTX_sz);
  REAL(RMD160Final)(digest, context);
  if (digest)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, digest, sizeof(u8) * 20);
}
INTERCEPTOR(void, RMD160Transform, u32 state[5], u16 buffer[16]) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, RMD160Transform, state, buffer);
  if (state)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, state, sizeof(u32) * 5);
  if (buffer)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, buffer, sizeof(u32) * 16);
  REAL(RMD160Transform)(state, buffer);
  if (state)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, state, sizeof(u32) * 5);
}
INTERCEPTOR(char *, RMD160End, void *context, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, RMD160End, context, buf);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, RMD160_CTX_sz);
  char *ret = REAL(RMD160End)(context, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, RMD160_return_length);
  return ret;
}
INTERCEPTOR(char *, RMD160File, char *filename, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, RMD160File, filename, buf);
  if (filename)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, filename, REAL(strlen)(filename) + 1);
  char *ret = REAL(RMD160File)(filename, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, RMD160_return_length);
  return ret;
}
INTERCEPTOR(char *, RMD160FileChunk, char *filename, char *buf, OFF_T offset,
  OFF_T length) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, RMD160FileChunk, filename, buf, offset, length);
  if (filename)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, filename, REAL(strlen)(filename) + 1);
  char *ret = REAL(RMD160FileChunk)(filename, buf, offset, length);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, RMD160_return_length);
  return ret;
}
INTERCEPTOR(char *, RMD160Data, u8 *data, SIZE_T len, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, RMD160Data, data, len, buf);
  if (data && len > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  char *ret = REAL(RMD160Data)(data, len, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, RMD160_return_length);
  return ret;
}
#define INIT_RMD160                                                            \
  COMMON_INTERCEPT_FUNCTION(RMD160Init);                                       \
  COMMON_INTERCEPT_FUNCTION(RMD160Update);                                     \
  COMMON_INTERCEPT_FUNCTION(RMD160Final);                                      \
  COMMON_INTERCEPT_FUNCTION(RMD160Transform);                                  \
  COMMON_INTERCEPT_FUNCTION(RMD160End);                                        \
  COMMON_INTERCEPT_FUNCTION(RMD160File);                                       \
  COMMON_INTERCEPT_FUNCTION(RMD160FileChunk);                                  \
  COMMON_INTERCEPT_FUNCTION(RMD160Data)
#else
#define INIT_RMD160
#endif

#if SANITIZER_INTERCEPT_MD5
INTERCEPTOR(void, MD5Init, void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD5Init, context);
  REAL(MD5Init)(context);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, MD5_CTX_sz);
}

INTERCEPTOR(void, MD5Update, void *context, const unsigned char *data,
            unsigned int len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD5Update, context, data, len);
  if (data && len > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, MD5_CTX_sz);
  REAL(MD5Update)(context, data, len);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, MD5_CTX_sz);
}

INTERCEPTOR(void, MD5Final, unsigned char digest[16], void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD5Final, digest, context);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, MD5_CTX_sz);
  REAL(MD5Final)(digest, context);
  if (digest)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, digest, sizeof(unsigned char) * 16);
}

INTERCEPTOR(char *, MD5End, void *context, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD5End, context, buf);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, MD5_CTX_sz);
  char *ret = REAL(MD5End)(context, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, MD5_return_length);
  return ret;
}

INTERCEPTOR(char *, MD5File, const char *filename, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD5File, filename, buf);
  if (filename)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, filename, REAL(strlen)(filename) + 1);
  char *ret = REAL(MD5File)(filename, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, MD5_return_length);
  return ret;
}

INTERCEPTOR(char *, MD5Data, const unsigned char *data, unsigned int len,
            char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD5Data, data, len, buf);
  if (data && len > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  char *ret = REAL(MD5Data)(data, len, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, MD5_return_length);
  return ret;
}

#define INIT_MD5                                                               \
  COMMON_INTERCEPT_FUNCTION(MD5Init);                                          \
  COMMON_INTERCEPT_FUNCTION(MD5Update);                                        \
  COMMON_INTERCEPT_FUNCTION(MD5Final);                                         \
  COMMON_INTERCEPT_FUNCTION(MD5End);                                           \
  COMMON_INTERCEPT_FUNCTION(MD5File);                                          \
  COMMON_INTERCEPT_FUNCTION(MD5Data)
#else
#define INIT_MD5
#endif

#if SANITIZER_INTERCEPT_FSEEK
INTERCEPTOR(int, fseek, __sanitizer_FILE *stream, long int offset, int whence) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fseek, stream, offset, whence);
  return REAL(fseek)(stream, offset, whence);
}
INTERCEPTOR(int, fseeko, __sanitizer_FILE *stream, OFF_T offset, int whence) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fseeko, stream, offset, whence);
  return REAL(fseeko)(stream, offset, whence);
}
INTERCEPTOR(long int, ftell, __sanitizer_FILE *stream) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ftell, stream);
  return REAL(ftell)(stream);
}
INTERCEPTOR(OFF_T, ftello, __sanitizer_FILE *stream) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ftello, stream);
  return REAL(ftello)(stream);
}
INTERCEPTOR(void, rewind, __sanitizer_FILE *stream) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, rewind, stream);
  return REAL(rewind)(stream);
}
INTERCEPTOR(int, fgetpos, __sanitizer_FILE *stream, void *pos) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fgetpos, stream, pos);
  int ret = REAL(fgetpos)(stream, pos);
  if (pos && !ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pos, fpos_t_sz);
  return ret;
}
INTERCEPTOR(int, fsetpos, __sanitizer_FILE *stream, const void *pos) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fsetpos, stream, pos);
  if (pos)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, pos, fpos_t_sz);
  return REAL(fsetpos)(stream, pos);
}
#define INIT_FSEEK \
  COMMON_INTERCEPT_FUNCTION(fseek); \
  COMMON_INTERCEPT_FUNCTION(fseeko); \
  COMMON_INTERCEPT_FUNCTION(ftell); \
  COMMON_INTERCEPT_FUNCTION(ftello); \
  COMMON_INTERCEPT_FUNCTION(rewind); \
  COMMON_INTERCEPT_FUNCTION(fgetpos); \
  COMMON_INTERCEPT_FUNCTION(fsetpos)
#else
#define INIT_FSEEK
#endif

#if SANITIZER_INTERCEPT_MD2
INTERCEPTOR(void, MD2Init, void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD2Init, context);
  REAL(MD2Init)(context);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, MD2_CTX_sz);
}

INTERCEPTOR(void, MD2Update, void *context, const unsigned char *data,
            unsigned int len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD2Update, context, data, len);
  if (data && len > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, MD2_CTX_sz);
  REAL(MD2Update)(context, data, len);
  if (context)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, MD2_CTX_sz);
}

INTERCEPTOR(void, MD2Final, unsigned char digest[16], void *context) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD2Final, digest, context);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, MD2_CTX_sz);
  REAL(MD2Final)(digest, context);
  if (digest)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, digest, sizeof(unsigned char) * 16);
}

INTERCEPTOR(char *, MD2End, void *context, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD2End, context, buf);
  if (context)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, context, MD2_CTX_sz);
  char *ret = REAL(MD2End)(context, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, MD2_return_length);
  return ret;
}

INTERCEPTOR(char *, MD2File, const char *filename, char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD2File, filename, buf);
  if (filename)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, filename, REAL(strlen)(filename) + 1);
  char *ret = REAL(MD2File)(filename, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, MD2_return_length);
  return ret;
}

INTERCEPTOR(char *, MD2Data, const unsigned char *data, unsigned int len,
            char *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, MD2Data, data, len, buf);
  if (data && len > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len);
  char *ret = REAL(MD2Data)(data, len, buf);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, MD2_return_length);
  return ret;
}

#define INIT_MD2                                                               \
  COMMON_INTERCEPT_FUNCTION(MD2Init);                                          \
  COMMON_INTERCEPT_FUNCTION(MD2Update);                                        \
  COMMON_INTERCEPT_FUNCTION(MD2Final);                                         \
  COMMON_INTERCEPT_FUNCTION(MD2End);                                           \
  COMMON_INTERCEPT_FUNCTION(MD2File);                                          \
  COMMON_INTERCEPT_FUNCTION(MD2Data)
#else
#define INIT_MD2
#endif

#if SANITIZER_INTERCEPT_SHA2
#define SHA2_INTERCEPTORS(LEN, SHA2_STATE_T) \
  INTERCEPTOR(void, SHA##LEN##_Init, void *context) { \
    void *ctx; \
    COMMON_INTERCEPTOR_ENTER(ctx, SHA##LEN##_Init, context); \
    REAL(SHA##LEN##_Init)(context); \
    if (context) \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, SHA##LEN##_CTX_sz); \
  } \
  INTERCEPTOR(void, SHA##LEN##_Update, void *context, \
              const u8 *data, SIZE_T len) { \
    void *ctx; \
    COMMON_INTERCEPTOR_ENTER(ctx, SHA##LEN##_Update, context, data, len); \
    if (data && len > 0) \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len); \
    if (context) \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, context, SHA##LEN##_CTX_sz); \
    REAL(SHA##LEN##_Update)(context, data, len); \
    if (context) \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, context, SHA##LEN##_CTX_sz); \
  } \
  INTERCEPTOR(void, SHA##LEN##_Final, u8 digest[LEN/8], \
  void *context) { \
    void *ctx; \
    CHECK_EQ(SHA##LEN##_digest_length, LEN/8); \
    COMMON_INTERCEPTOR_ENTER(ctx, SHA##LEN##_Final, digest, context); \
    if (context) \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, context, SHA##LEN##_CTX_sz); \
    REAL(SHA##LEN##_Final)(digest, context); \
    if (digest) \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, digest, \
                                     sizeof(digest[0]) * \
  SHA##LEN##_digest_length); \
  } \
  INTERCEPTOR(char *, SHA##LEN##_End, void *context, char *buf) { \
    void *ctx; \
    COMMON_INTERCEPTOR_ENTER(ctx, SHA##LEN##_End, context, buf); \
    if (context) \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, context, SHA##LEN##_CTX_sz); \
    char *ret = REAL(SHA##LEN##_End)(context, buf); \
    if (ret) \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, SHA##LEN##_return_length); \
    return ret; \
  } \
  INTERCEPTOR(char *, SHA##LEN##_File, const char *filename, char *buf) { \
    void *ctx; \
    COMMON_INTERCEPTOR_ENTER(ctx, SHA##LEN##_File, filename, buf); \
    if (filename) \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, filename, REAL(strlen)(filename) + 1);\
    char *ret = REAL(SHA##LEN##_File)(filename, buf); \
    if (ret) \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, SHA##LEN##_return_length); \
    return ret; \
  } \
  INTERCEPTOR(char *, SHA##LEN##_FileChunk, const char *filename, char *buf, \
              OFF_T offset, OFF_T length) { \
    void *ctx; \
    COMMON_INTERCEPTOR_ENTER(ctx, SHA##LEN##_FileChunk, filename, buf, offset, \
  length); \
    if (filename) \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, filename, REAL(strlen)(filename) + 1);\
    char *ret = REAL(SHA##LEN##_FileChunk)(filename, buf, offset, length); \
    if (ret) \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, SHA##LEN##_return_length); \
    return ret; \
  } \
  INTERCEPTOR(char *, SHA##LEN##_Data, u8 *data, SIZE_T len, char *buf) { \
    void *ctx; \
    COMMON_INTERCEPTOR_ENTER(ctx, SHA##LEN##_Data, data, len, buf); \
    if (data && len > 0) \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, data, len); \
    char *ret = REAL(SHA##LEN##_Data)(data, len, buf); \
    if (ret) \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, SHA##LEN##_return_length); \
    return ret; \
  }

SHA2_INTERCEPTORS(224, u32);
SHA2_INTERCEPTORS(256, u32);
SHA2_INTERCEPTORS(384, u64);
SHA2_INTERCEPTORS(512, u64);

#define INIT_SHA2_INTECEPTORS(LEN) \
  COMMON_INTERCEPT_FUNCTION(SHA##LEN##_Init); \
  COMMON_INTERCEPT_FUNCTION(SHA##LEN##_Update); \
  COMMON_INTERCEPT_FUNCTION(SHA##LEN##_Final); \
  COMMON_INTERCEPT_FUNCTION(SHA##LEN##_End); \
  COMMON_INTERCEPT_FUNCTION(SHA##LEN##_File); \
  COMMON_INTERCEPT_FUNCTION(SHA##LEN##_FileChunk); \
  COMMON_INTERCEPT_FUNCTION(SHA##LEN##_Data)

#define INIT_SHA2 \
  INIT_SHA2_INTECEPTORS(224); \
  INIT_SHA2_INTECEPTORS(256); \
  INIT_SHA2_INTECEPTORS(384); \
  INIT_SHA2_INTECEPTORS(512)
#undef SHA2_INTERCEPTORS
#else
#define INIT_SHA2
#endif

#if SANITIZER_INTERCEPT_VIS
INTERCEPTOR(char *, vis, char *dst, int c, int flag, int nextc) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, vis, dst, c, flag, nextc);
  char *end = REAL(vis)(dst, c, flag, nextc);
  // dst is NULL terminated and end points to the NULL char
  if (dst && end)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, end - dst + 1);
  return end;
}
INTERCEPTOR(char *, nvis, char *dst, SIZE_T dlen, int c, int flag, int nextc) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, nvis, dst, dlen, c, flag, nextc);
  char *end = REAL(nvis)(dst, dlen, c, flag, nextc);
  // nvis cannot make sure the dst is NULL terminated
  if (dst && end)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, end - dst + 1);
  return end;
}
INTERCEPTOR(int, strvis, char *dst, const char *src, int flag) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strvis, dst, src, flag);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, REAL(strlen)(src) + 1);
  int len = REAL(strvis)(dst, src, flag);
  if (dst)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, len + 1);
  return len;
}
INTERCEPTOR(int, stravis, char **dst, const char *src, int flag) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, stravis, dst, src, flag);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, REAL(strlen)(src) + 1);
  int len = REAL(stravis)(dst, src, flag);
  if (dst) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, sizeof(char *));
    if (*dst)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *dst, len + 1);
  }
  return len;
}
INTERCEPTOR(int, strnvis, char *dst, SIZE_T dlen, const char *src, int flag) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strnvis, dst, dlen, src, flag);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, REAL(strlen)(src) + 1);
  int len = REAL(strnvis)(dst, dlen, src, flag);
  // The interface will be valid even if there is no space for NULL char
  if (dst && len > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, len + 1);
  return len;
}
INTERCEPTOR(int, strvisx, char *dst, const char *src, SIZE_T len, int flag) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strvisx, dst, src, len, flag);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, len);
  int ret = REAL(strvisx)(dst, src, len, flag);
  if (dst)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  return ret;
}
INTERCEPTOR(int, strnvisx, char *dst, SIZE_T dlen, const char *src, SIZE_T len,
            int flag) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strnvisx, dst, dlen, src, len, flag);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, len);
  int ret = REAL(strnvisx)(dst, dlen, src, len, flag);
  if (dst && ret >= 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  return ret;
}
INTERCEPTOR(int, strenvisx, char *dst, SIZE_T dlen, const char *src, SIZE_T len,
            int flag, int *cerr_ptr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strenvisx, dst, dlen, src, len, flag, cerr_ptr);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, len);
  // FIXME: only need to be checked when "flag | VIS_NOLOCALE" doesn't hold
  // according to the implementation
  if (cerr_ptr)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cerr_ptr, sizeof(int));
  int ret = REAL(strenvisx)(dst, dlen, src, len, flag, cerr_ptr);
  if (dst && ret >= 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  if (cerr_ptr)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cerr_ptr, sizeof(int));
  return ret;
}
INTERCEPTOR(char *, svis, char *dst, int c, int flag, int nextc,
            const char *extra) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, svis, dst, c, flag, nextc, extra);
  if (extra)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, extra, REAL(strlen)(extra) + 1);
  char *end = REAL(svis)(dst, c, flag, nextc, extra);
  if (dst && end)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, end - dst + 1);
  return end;
}
INTERCEPTOR(char *, snvis, char *dst, SIZE_T dlen, int c, int flag, int nextc,
            const char *extra) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, snvis, dst, dlen, c, flag, nextc, extra);
  if (extra)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, extra, REAL(strlen)(extra) + 1);
  char *end = REAL(snvis)(dst, dlen, c, flag, nextc, extra);
  if (dst && end)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst,
                                   Min((SIZE_T)(end - dst + 1), dlen));
  return end;
}
INTERCEPTOR(int, strsvis, char *dst, const char *src, int flag,
            const char *extra) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strsvis, dst, src, flag, extra);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, REAL(strlen)(src) + 1);
  if (extra)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, extra, REAL(strlen)(extra) + 1);
  int len = REAL(strsvis)(dst, src, flag, extra);
  if (dst)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, len + 1);
  return len;
}
INTERCEPTOR(int, strsnvis, char *dst, SIZE_T dlen, const char *src, int flag,
            const char *extra) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strsnvis, dst, dlen, src, flag, extra);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, REAL(strlen)(src) + 1);
  if (extra)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, extra, REAL(strlen)(extra) + 1);
  int len = REAL(strsnvis)(dst, dlen, src, flag, extra);
  // The interface will be valid even if there is no space for NULL char
  if (dst && len >= 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, len + 1);
  return len;
}
INTERCEPTOR(int, strsvisx, char *dst, const char *src, SIZE_T len, int flag,
            const char *extra) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strsvisx, dst, src, len, flag, extra);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, len);
  if (extra)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, extra, REAL(strlen)(extra) + 1);
  int ret = REAL(strsvisx)(dst, src, len, flag, extra);
  if (dst)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  return ret;
}
INTERCEPTOR(int, strsnvisx, char *dst, SIZE_T dlen, const char *src, SIZE_T len,
            int flag, const char *extra) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strsnvisx, dst, dlen, src, len, flag, extra);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, len);
  if (extra)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, extra, REAL(strlen)(extra) + 1);
  int ret = REAL(strsnvisx)(dst, dlen, src, len, flag, extra);
  if (dst && ret >= 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  return ret;
}
INTERCEPTOR(int, strsenvisx, char *dst, SIZE_T dlen, const char *src,
            SIZE_T len, int flag, const char *extra, int *cerr_ptr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strsenvisx, dst, dlen, src, len, flag, extra,
                           cerr_ptr);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, len);
  if (extra)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, extra, REAL(strlen)(extra) + 1);
  // FIXME: only need to be checked when "flag | VIS_NOLOCALE" doesn't hold
  // according to the implementation
  if (cerr_ptr)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cerr_ptr, sizeof(int));
  int ret = REAL(strsenvisx)(dst, dlen, src, len, flag, extra, cerr_ptr);
  if (dst && ret >= 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  if (cerr_ptr)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cerr_ptr, sizeof(int));
  return ret;
}
INTERCEPTOR(int, unvis, char *cp, int c, int *astate, int flag) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, unvis, cp, c, astate, flag);
  if (astate)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, astate, sizeof(*astate));
  int ret = REAL(unvis)(cp, c, astate, flag);
  if (ret == unvis_valid || ret == unvis_validpush) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cp, sizeof(*cp));
  }
  return ret;
}
INTERCEPTOR(int, strunvis, char *dst, const char *src) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strunvis, dst, src);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, REAL(strlen)(src) + 1);
  int ret = REAL(strunvis)(dst, src);
  if (ret != -1)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  return ret;
}
INTERCEPTOR(int, strnunvis, char *dst, SIZE_T dlen, const char *src) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strnunvis, dst, dlen, src);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, REAL(strlen)(src) + 1);
  int ret = REAL(strnunvis)(dst, dlen, src);
  if (ret != -1)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  return ret;
}
INTERCEPTOR(int, strunvisx, char *dst, const char *src, int flag) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strunvisx, dst, src, flag);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, REAL(strlen)(src) + 1);
  int ret = REAL(strunvisx)(dst, src, flag);
  if (ret != -1)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  return ret;
}
INTERCEPTOR(int, strnunvisx, char *dst, SIZE_T dlen, const char *src,
            int flag) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strnunvisx, dst, dlen, src, flag);
  if (src)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, src, REAL(strlen)(src) + 1);
  int ret = REAL(strnunvisx)(dst, dlen, src, flag);
  if (ret != -1)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, ret + 1);
  return ret;
}
#define INIT_VIS                                                               \
  COMMON_INTERCEPT_FUNCTION(vis);                                              \
  COMMON_INTERCEPT_FUNCTION(nvis);                                             \
  COMMON_INTERCEPT_FUNCTION(strvis);                                           \
  COMMON_INTERCEPT_FUNCTION(stravis);                                          \
  COMMON_INTERCEPT_FUNCTION(strnvis);                                          \
  COMMON_INTERCEPT_FUNCTION(strvisx);                                          \
  COMMON_INTERCEPT_FUNCTION(strnvisx);                                         \
  COMMON_INTERCEPT_FUNCTION(strenvisx);                                        \
  COMMON_INTERCEPT_FUNCTION(svis);                                             \
  COMMON_INTERCEPT_FUNCTION(snvis);                                            \
  COMMON_INTERCEPT_FUNCTION(strsvis);                                          \
  COMMON_INTERCEPT_FUNCTION(strsnvis);                                         \
  COMMON_INTERCEPT_FUNCTION(strsvisx);                                         \
  COMMON_INTERCEPT_FUNCTION(strsnvisx);                                        \
  COMMON_INTERCEPT_FUNCTION(strsenvisx);                                       \
  COMMON_INTERCEPT_FUNCTION(unvis);                                            \
  COMMON_INTERCEPT_FUNCTION(strunvis);                                         \
  COMMON_INTERCEPT_FUNCTION(strnunvis);                                        \
  COMMON_INTERCEPT_FUNCTION(strunvisx);                                        \
  COMMON_INTERCEPT_FUNCTION(strnunvisx)
#else
#define INIT_VIS
#endif

#if SANITIZER_INTERCEPT_CDB
INTERCEPTOR(struct __sanitizer_cdbr *, cdbr_open, const char *path, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbr_open, path, flags);
  if (path)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  struct __sanitizer_cdbr *cdbr = REAL(cdbr_open)(path, flags);
  if (cdbr)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cdbr, sizeof(*cdbr));
  return cdbr;
}

INTERCEPTOR(struct __sanitizer_cdbr *, cdbr_open_mem, void *base, SIZE_T size,
  int flags, void (*unmap)(void *, void *, SIZE_T), void *cookie) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbr_open_mem, base, size, flags, unmap,
    cookie);
  if (base && size)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, base, size);
  struct __sanitizer_cdbr *cdbr =
    REAL(cdbr_open_mem)(base, size, flags, unmap, cookie);
  if (cdbr)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cdbr, sizeof(*cdbr));
  return cdbr;
}

INTERCEPTOR(u32, cdbr_entries, struct __sanitizer_cdbr *cdbr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbr_entries, cdbr);
  if (cdbr)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cdbr, sizeof(*cdbr));
  return REAL(cdbr_entries)(cdbr);
}

INTERCEPTOR(int, cdbr_get, struct __sanitizer_cdbr *cdbr, u32 index,
            const void **data, SIZE_T *datalen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbr_get, cdbr, index, data, datalen);
  if (cdbr)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cdbr, sizeof(*cdbr));
  int ret = REAL(cdbr_get)(cdbr, index, data, datalen);
  if (!ret) {
    if (data)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, data, sizeof(*data));
    if (datalen)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, datalen, sizeof(*datalen));
    if (data && datalen)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *data, *datalen);
  }
  return ret;
}

INTERCEPTOR(int, cdbr_find, struct __sanitizer_cdbr *cdbr, const void *key,
            SIZE_T keylen, const void **data, SIZE_T *datalen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbr_find, cdbr, key, keylen, data, datalen);
  if (cdbr)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cdbr, sizeof(*cdbr));
  if (key)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, key, keylen);
  int ret = REAL(cdbr_find)(cdbr, key, keylen, data, datalen);
  if (!ret) {
    if (data)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, data, sizeof(*data));
    if (datalen)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, datalen, sizeof(*datalen));
    if (data && datalen)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *data, *datalen);
  }
  return ret;
}

INTERCEPTOR(void, cdbr_close, struct __sanitizer_cdbr *cdbr) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbr_close, cdbr);
  if (cdbr)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cdbr, sizeof(*cdbr));
  REAL(cdbr_close)(cdbr);
}

INTERCEPTOR(struct __sanitizer_cdbw *, cdbw_open) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbw_open);
  struct __sanitizer_cdbw *ret = REAL(cdbw_open)();
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, sizeof(*ret));
  return ret;
}

INTERCEPTOR(int, cdbw_put, struct __sanitizer_cdbw *cdbw, const void *key,
  SIZE_T keylen, const void *data, SIZE_T datalen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbw_put, cdbw, key, keylen, data, datalen);
  if (cdbw)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cdbw, sizeof(*cdbw));
  if (data && datalen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, datalen);
  if (key && keylen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, key, keylen);
  int ret = REAL(cdbw_put)(cdbw, key, keylen, data, datalen);
  if (!ret && cdbw)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cdbw, sizeof(*cdbw));
  return ret;
}

INTERCEPTOR(int, cdbw_put_data, struct __sanitizer_cdbw *cdbw, const void *data,
  SIZE_T datalen, u32 *index) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbw_put_data, cdbw, data, datalen, index);
  if (cdbw)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cdbw, sizeof(*cdbw));
  if (data && datalen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, data, datalen);
  int ret = REAL(cdbw_put_data)(cdbw, data, datalen, index);
  if (!ret) {
    if (index)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, index, sizeof(*index));
    if (cdbw)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cdbw, sizeof(*cdbw));
  }
  return ret;
}

INTERCEPTOR(int, cdbw_put_key, struct __sanitizer_cdbw *cdbw, const void *key,
  SIZE_T keylen, u32 index) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbw_put_key, cdbw, key, keylen, index);
  if (cdbw)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cdbw, sizeof(*cdbw));
  if (key && keylen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, key, keylen);
  int ret = REAL(cdbw_put_key)(cdbw, key, keylen, index);
  if (!ret && cdbw)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cdbw, sizeof(*cdbw));
  return ret;
}

INTERCEPTOR(int, cdbw_output, struct __sanitizer_cdbw *cdbw, int output,
  const char descr[16], u32 (*seedgen)(void)) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbw_output, cdbw, output, descr, seedgen);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, output);
  if (cdbw)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cdbw, sizeof(*cdbw));
  if (descr)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, descr, internal_strnlen(descr, 16));
  if (seedgen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, (void *)seedgen, sizeof(seedgen));
  int ret = REAL(cdbw_output)(cdbw, output, descr, seedgen);
  if (!ret) {
    if (cdbw)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, cdbw, sizeof(*cdbw));
    if (output >= 0)
      COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, output);
  }
  return ret;
}

INTERCEPTOR(void, cdbw_close, struct __sanitizer_cdbw *cdbw) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, cdbw_close, cdbw);
  if (cdbw)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, cdbw, sizeof(*cdbw));
  REAL(cdbw_close)(cdbw);
}

#define INIT_CDB \
  COMMON_INTERCEPT_FUNCTION(cdbr_open); \
  COMMON_INTERCEPT_FUNCTION(cdbr_open_mem); \
  COMMON_INTERCEPT_FUNCTION(cdbr_entries); \
  COMMON_INTERCEPT_FUNCTION(cdbr_get); \
  COMMON_INTERCEPT_FUNCTION(cdbr_find); \
  COMMON_INTERCEPT_FUNCTION(cdbr_close); \
  COMMON_INTERCEPT_FUNCTION(cdbw_open); \
  COMMON_INTERCEPT_FUNCTION(cdbw_put); \
  COMMON_INTERCEPT_FUNCTION(cdbw_put_data); \
  COMMON_INTERCEPT_FUNCTION(cdbw_put_key); \
  COMMON_INTERCEPT_FUNCTION(cdbw_output); \
  COMMON_INTERCEPT_FUNCTION(cdbw_close)
#else
#define INIT_CDB
#endif

#if SANITIZER_INTERCEPT_GETFSENT
INTERCEPTOR(void *, getfsent) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getfsent);
  void *ret = REAL(getfsent)();
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, struct_fstab_sz);
  return ret;
}

INTERCEPTOR(void *, getfsspec, const char *spec) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getfsspec, spec);
  if (spec)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, spec, REAL(strlen)(spec) + 1);
  void *ret = REAL(getfsspec)(spec);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, struct_fstab_sz);
  return ret;
}

INTERCEPTOR(void *, getfsfile, const char *file) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getfsfile, file);
  if (file)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, file, REAL(strlen)(file) + 1);
  void *ret = REAL(getfsfile)(file);
  if (ret)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ret, struct_fstab_sz);
  return ret;
}

#define INIT_GETFSENT \
  COMMON_INTERCEPT_FUNCTION(getfsent); \
  COMMON_INTERCEPT_FUNCTION(getfsspec); \
  COMMON_INTERCEPT_FUNCTION(getfsfile);
#else
#define INIT_GETFSENT
#endif

#if SANITIZER_INTERCEPT_ARC4RANDOM
INTERCEPTOR(void, arc4random_buf, void *buf, SIZE_T len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, arc4random_buf, buf, len);
  REAL(arc4random_buf)(buf, len);
  if (buf && len)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, len);
}

INTERCEPTOR(void, arc4random_addrandom, u8 *dat, int datlen) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, arc4random_addrandom, dat, datlen);
  if (dat && datlen)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, dat, datlen);
  REAL(arc4random_addrandom)(dat, datlen);
}

#define INIT_ARC4RANDOM \
  COMMON_INTERCEPT_FUNCTION(arc4random_buf); \
  COMMON_INTERCEPT_FUNCTION(arc4random_addrandom);
#else
#define INIT_ARC4RANDOM
#endif

#if SANITIZER_INTERCEPT_POPEN
INTERCEPTOR(__sanitizer_FILE *, popen, const char *command, const char *type) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, popen, command, type);
  if (command)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, command, REAL(strlen)(command) + 1);
  if (type)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, type, REAL(strlen)(type) + 1);
  __sanitizer_FILE *res = REAL(popen)(command, type);
  COMMON_INTERCEPTOR_FILE_OPEN(ctx, res, nullptr);
  if (res) unpoison_file(res);
  return res;
}
#define INIT_POPEN COMMON_INTERCEPT_FUNCTION(popen)
#else
#define INIT_POPEN
#endif

#if SANITIZER_INTERCEPT_POPENVE
INTERCEPTOR(__sanitizer_FILE *, popenve, const char *path,
            char *const *argv, char *const *envp, const char *type) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, popenve, path, argv, envp, type);
  if (path)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, path, REAL(strlen)(path) + 1);
  if (argv) {
    for (char *const *pa = argv; ; ++pa) {
      COMMON_INTERCEPTOR_READ_RANGE(ctx, pa, sizeof(char **));
      if (!*pa)
        break;
      COMMON_INTERCEPTOR_READ_RANGE(ctx, *pa, REAL(strlen)(*pa) + 1);
    }
  }
  if (envp) {
    for (char *const *pa = envp; ; ++pa) {
      COMMON_INTERCEPTOR_READ_RANGE(ctx, pa, sizeof(char **));
      if (!*pa)
        break;
      COMMON_INTERCEPTOR_READ_RANGE(ctx, *pa, REAL(strlen)(*pa) + 1);
    }
  }
  if (type)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, type, REAL(strlen)(type) + 1);
  __sanitizer_FILE *res = REAL(popenve)(path, argv, envp, type);
  COMMON_INTERCEPTOR_FILE_OPEN(ctx, res, nullptr);
  if (res) unpoison_file(res);
  return res;
}
#define INIT_POPENVE COMMON_INTERCEPT_FUNCTION(popenve)
#else
#define INIT_POPENVE
#endif

#if SANITIZER_INTERCEPT_PCLOSE
INTERCEPTOR(int, pclose, __sanitizer_FILE *fp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pclose, fp);
  COMMON_INTERCEPTOR_FILE_CLOSE(ctx, fp);
  const FileMetadata *m = GetInterceptorMetadata(fp);
  int res = REAL(pclose)(fp);
  if (m) {
    COMMON_INTERCEPTOR_INITIALIZE_RANGE(*m->addr, *m->size);
    DeleteInterceptorMetadata(fp);
  }
  return res;
}
#define INIT_PCLOSE COMMON_INTERCEPT_FUNCTION(pclose);
#else
#define INIT_PCLOSE
#endif

#if SANITIZER_INTERCEPT_FUNOPEN
typedef int (*funopen_readfn)(void *cookie, char *buf, int len);
typedef int (*funopen_writefn)(void *cookie, const char *buf, int len);
typedef OFF_T (*funopen_seekfn)(void *cookie, OFF_T offset, int whence);
typedef int (*funopen_closefn)(void *cookie);

struct WrappedFunopenCookie {
  void *real_cookie;
  funopen_readfn real_read;
  funopen_writefn real_write;
  funopen_seekfn real_seek;
  funopen_closefn real_close;
};

static int wrapped_funopen_read(void *cookie, char *buf, int len) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(3);
  WrappedFunopenCookie *wrapped_cookie = (WrappedFunopenCookie *)cookie;
  funopen_readfn real_read = wrapped_cookie->real_read;
  return real_read(wrapped_cookie->real_cookie, buf, len);
}

static int wrapped_funopen_write(void *cookie, const char *buf, int len) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(3);
  WrappedFunopenCookie *wrapped_cookie = (WrappedFunopenCookie *)cookie;
  funopen_writefn real_write = wrapped_cookie->real_write;
  return real_write(wrapped_cookie->real_cookie, buf, len);
}

static OFF_T wrapped_funopen_seek(void *cookie, OFF_T offset, int whence) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(3);
  WrappedFunopenCookie *wrapped_cookie = (WrappedFunopenCookie *)cookie;
  funopen_seekfn real_seek = wrapped_cookie->real_seek;
  return real_seek(wrapped_cookie->real_cookie, offset, whence);
}

static int wrapped_funopen_close(void *cookie) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(1);
  WrappedFunopenCookie *wrapped_cookie = (WrappedFunopenCookie *)cookie;
  funopen_closefn real_close = wrapped_cookie->real_close;
  int res = real_close(wrapped_cookie->real_cookie);
  InternalFree(wrapped_cookie);
  return res;
}

INTERCEPTOR(__sanitizer_FILE *, funopen, void *cookie, funopen_readfn readfn,
            funopen_writefn writefn, funopen_seekfn seekfn,
            funopen_closefn closefn) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, funopen, cookie, readfn, writefn, seekfn,
                           closefn);

  WrappedFunopenCookie *wrapped_cookie =
      (WrappedFunopenCookie *)InternalAlloc(sizeof(WrappedFunopenCookie));
  wrapped_cookie->real_cookie = cookie;
  wrapped_cookie->real_read = readfn;
  wrapped_cookie->real_write = writefn;
  wrapped_cookie->real_seek = seekfn;
  wrapped_cookie->real_close = closefn;

  __sanitizer_FILE *res =
      REAL(funopen)(wrapped_cookie,
                    readfn  ? wrapped_funopen_read  : nullptr,
                    writefn ? wrapped_funopen_write : nullptr,
                    seekfn  ? wrapped_funopen_seek  : nullptr,
                    closefn ? wrapped_funopen_close : nullptr);
  if (res)
    unpoison_file(res);
  return res;
}
#define INIT_FUNOPEN COMMON_INTERCEPT_FUNCTION(funopen)
#else
#define INIT_FUNOPEN
#endif

#if SANITIZER_INTERCEPT_FUNOPEN2
typedef SSIZE_T (*funopen2_readfn)(void *cookie, void *buf, SIZE_T len);
typedef SSIZE_T (*funopen2_writefn)(void *cookie, const void *buf, SIZE_T len);
typedef OFF_T (*funopen2_seekfn)(void *cookie, OFF_T offset, int whence);
typedef int (*funopen2_flushfn)(void *cookie);
typedef int (*funopen2_closefn)(void *cookie);

struct WrappedFunopen2Cookie {
  void *real_cookie;
  funopen2_readfn real_read;
  funopen2_writefn real_write;
  funopen2_seekfn real_seek;
  funopen2_flushfn real_flush;
  funopen2_closefn real_close;
};

static SSIZE_T wrapped_funopen2_read(void *cookie, void *buf, SIZE_T len) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(3);
  WrappedFunopen2Cookie *wrapped_cookie = (WrappedFunopen2Cookie *)cookie;
  funopen2_readfn real_read = wrapped_cookie->real_read;
  return real_read(wrapped_cookie->real_cookie, buf, len);
}

static SSIZE_T wrapped_funopen2_write(void *cookie, const void *buf,
                                      SIZE_T len) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(3);
  WrappedFunopen2Cookie *wrapped_cookie = (WrappedFunopen2Cookie *)cookie;
  funopen2_writefn real_write = wrapped_cookie->real_write;
  return real_write(wrapped_cookie->real_cookie, buf, len);
}

static OFF_T wrapped_funopen2_seek(void *cookie, OFF_T offset, int whence) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(3);
  WrappedFunopen2Cookie *wrapped_cookie = (WrappedFunopen2Cookie *)cookie;
  funopen2_seekfn real_seek = wrapped_cookie->real_seek;
  return real_seek(wrapped_cookie->real_cookie, offset, whence);
}

static int wrapped_funopen2_flush(void *cookie) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(1);
  WrappedFunopen2Cookie *wrapped_cookie = (WrappedFunopen2Cookie *)cookie;
  funopen2_flushfn real_flush = wrapped_cookie->real_flush;
  return real_flush(wrapped_cookie->real_cookie);
}

static int wrapped_funopen2_close(void *cookie) {
  COMMON_INTERCEPTOR_UNPOISON_PARAM(1);
  WrappedFunopen2Cookie *wrapped_cookie = (WrappedFunopen2Cookie *)cookie;
  funopen2_closefn real_close = wrapped_cookie->real_close;
  int res = real_close(wrapped_cookie->real_cookie);
  InternalFree(wrapped_cookie);
  return res;
}

INTERCEPTOR(__sanitizer_FILE *, funopen2, void *cookie, funopen2_readfn readfn,
            funopen2_writefn writefn, funopen2_seekfn seekfn,
            funopen2_flushfn flushfn, funopen2_closefn closefn) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, funopen2, cookie, readfn, writefn, seekfn,
                           flushfn, closefn);

  WrappedFunopen2Cookie *wrapped_cookie =
      (WrappedFunopen2Cookie *)InternalAlloc(sizeof(WrappedFunopen2Cookie));
  wrapped_cookie->real_cookie = cookie;
  wrapped_cookie->real_read = readfn;
  wrapped_cookie->real_write = writefn;
  wrapped_cookie->real_seek = seekfn;
  wrapped_cookie->real_flush = flushfn;
  wrapped_cookie->real_close = closefn;

  __sanitizer_FILE *res =
      REAL(funopen2)(wrapped_cookie,
                     readfn  ? wrapped_funopen2_read  : nullptr,
                     writefn ? wrapped_funopen2_write : nullptr,
                     seekfn  ? wrapped_funopen2_seek  : nullptr,
                     flushfn ? wrapped_funopen2_flush : nullptr,
                     closefn ? wrapped_funopen2_close : nullptr);
  if (res)
    unpoison_file(res);
  return res;
}
#define INIT_FUNOPEN2 COMMON_INTERCEPT_FUNCTION(funopen2)
#else
#define INIT_FUNOPEN2
#endif

#if SANITIZER_INTERCEPT_FDEVNAME
INTERCEPTOR(char *, fdevname,  int fd) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fdevname, fd);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  char *name = REAL(fdevname)(fd);
  if (name) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, name, REAL(strlen)(name) + 1);
    if (fd > 0)
      COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  }
  return name;
}

INTERCEPTOR(char *, fdevname_r,  int fd, char *buf, SIZE_T len) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fdevname_r, fd, buf, len);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  char *name = REAL(fdevname_r)(fd, buf, len);
  if (name && buf && len > 0) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, REAL(strlen)(buf) + 1);
    if (fd > 0)
      COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  }
  return name;
}

#define INIT_FDEVNAME \
  COMMON_INTERCEPT_FUNCTION(fdevname); \
  COMMON_INTERCEPT_FUNCTION(fdevname_r);
#else
#define INIT_FDEVNAME
#endif

#if SANITIZER_INTERCEPT_GETUSERSHELL
INTERCEPTOR(char *, getusershell) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getusershell);
  char *res = REAL(getusershell)();
  if (res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  return res;
}

#define INIT_GETUSERSHELL COMMON_INTERCEPT_FUNCTION(getusershell);
#else
#define INIT_GETUSERSHELL
#endif

#if SANITIZER_INTERCEPT_SL_INIT
INTERCEPTOR(void *, sl_init) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sl_init);
  void *res = REAL(sl_init)();
  if (res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, __sanitizer::struct_StringList_sz);
  return res;
}

INTERCEPTOR(int, sl_add, void *sl, char *item) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sl_add, sl, item);
  if (sl)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sl, __sanitizer::struct_StringList_sz);
  if (item)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, item, REAL(strlen)(item) + 1);
  int res = REAL(sl_add)(sl, item);
  if (!res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, sl, __sanitizer::struct_StringList_sz);
  return res;
}

INTERCEPTOR(char *, sl_find, void *sl, const char *item) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sl_find, sl, item);
  if (sl)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sl, __sanitizer::struct_StringList_sz);
  if (item)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, item, REAL(strlen)(item) + 1);
  char *res = REAL(sl_find)(sl, item);
  if (res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  return res;
}

INTERCEPTOR(void, sl_free, void *sl, int freeall) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sl_free, sl, freeall);
  if (sl)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, sl, __sanitizer::struct_StringList_sz);
  REAL(sl_free)(sl, freeall);
}

#define INIT_SL_INIT                  \
  COMMON_INTERCEPT_FUNCTION(sl_init); \
  COMMON_INTERCEPT_FUNCTION(sl_add);  \
  COMMON_INTERCEPT_FUNCTION(sl_find); \
  COMMON_INTERCEPT_FUNCTION(sl_free);
#else
#define INIT_SL_INIT
#endif

static void InitializeCommonInterceptors() {
  static u64 metadata_mem[sizeof(MetadataHashMap) / sizeof(u64) + 1];
  interceptor_metadata_map =
      new ((void *)&metadata_mem) MetadataHashMap();  // NOLINT

  INIT_MMAP;
  INIT_MMAP64;
  INIT_TEXTDOMAIN;
  INIT_STRLEN;
  INIT_STRNLEN;
  INIT_STRNDUP;
  INIT___STRNDUP;
  INIT_STRCMP;
  INIT_STRNCMP;
  INIT_STRCASECMP;
  INIT_STRNCASECMP;
  INIT_STRSTR;
  INIT_STRCASESTR;
  INIT_STRCHR;
  INIT_STRCHRNUL;
  INIT_STRRCHR;
  INIT_STRSPN;
  INIT_STRTOK;
  INIT_STRPBRK;
  INIT_STRXFRM;
  INIT___STRXFRM_L;
  INIT_MEMSET;
  INIT_MEMMOVE;
  INIT_MEMCPY;
  INIT_MEMCHR;
  INIT_MEMCMP;
  INIT_MEMRCHR;
  INIT_MEMMEM;
  INIT_READ;
  INIT_FREAD;
  INIT_PREAD;
  INIT_PREAD64;
  INIT_READV;
  INIT_PREADV;
  INIT_PREADV64;
  INIT_WRITE;
  INIT_FWRITE;
  INIT_PWRITE;
  INIT_PWRITE64;
  INIT_WRITEV;
  INIT_PWRITEV;
  INIT_PWRITEV64;
  INIT_FGETS;
  INIT_FPUTS;
  INIT_PUTS;
  INIT_PRCTL;
  INIT_LOCALTIME_AND_FRIENDS;
  INIT_STRPTIME;
  INIT_SCANF;
  INIT_ISOC99_SCANF;
  INIT_PRINTF;
  INIT_PRINTF_L;
  INIT_ISOC99_PRINTF;
  INIT_FREXP;
  INIT_FREXPF_FREXPL;
  INIT_GETPWNAM_AND_FRIENDS;
  INIT_GETPWNAM_R_AND_FRIENDS;
  INIT_GETPWENT;
  INIT_FGETPWENT;
  INIT_GETPWENT_R;
  INIT_FGETPWENT_R;
  INIT_FGETGRENT_R;
  INIT_SETPWENT;
  INIT_CLOCK_GETTIME;
  INIT_GETITIMER;
  INIT_TIME;
  INIT_GLOB;
  INIT_GLOB64;
  INIT_WAIT;
  INIT_WAIT4;
  INIT_INET;
  INIT_PTHREAD_GETSCHEDPARAM;
  INIT_GETADDRINFO;
  INIT_GETNAMEINFO;
  INIT_GETSOCKNAME;
  INIT_GETHOSTBYNAME;
  INIT_GETHOSTBYNAME2;
  INIT_GETHOSTBYNAME_R;
  INIT_GETHOSTBYNAME2_R;
  INIT_GETHOSTBYADDR_R;
  INIT_GETHOSTENT_R;
  INIT_GETSOCKOPT;
  INIT_ACCEPT;
  INIT_ACCEPT4;
  INIT_PACCEPT;
  INIT_MODF;
  INIT_RECVMSG;
  INIT_SENDMSG;
  INIT_RECVMMSG;
  INIT_SENDMMSG;
  INIT_GETPEERNAME;
  INIT_IOCTL;
  INIT_INET_ATON;
  INIT_SYSINFO;
  INIT_READDIR;
  INIT_READDIR64;
  INIT_PTRACE;
  INIT_SETLOCALE;
  INIT_GETCWD;
  INIT_GET_CURRENT_DIR_NAME;
  INIT_STRTOIMAX;
  INIT_MBSTOWCS;
  INIT_MBSNRTOWCS;
  INIT_WCSTOMBS;
  INIT_WCSNRTOMBS;
  INIT_WCRTOMB;
  INIT_TCGETATTR;
  INIT_REALPATH;
  INIT_CANONICALIZE_FILE_NAME;
  INIT_CONFSTR;
  INIT_SCHED_GETAFFINITY;
  INIT_SCHED_GETPARAM;
  INIT_STRERROR;
  INIT_STRERROR_R;
  INIT_XPG_STRERROR_R;
  INIT_SCANDIR;
  INIT_SCANDIR64;
  INIT_GETGROUPS;
  INIT_POLL;
  INIT_PPOLL;
  INIT_WORDEXP;
  INIT_SIGWAIT;
  INIT_SIGWAITINFO;
  INIT_SIGTIMEDWAIT;
  INIT_SIGSETOPS;
  INIT_SIGPENDING;
  INIT_SIGPROCMASK;
  INIT_BACKTRACE;
  INIT__EXIT;
  INIT_PTHREAD_MUTEX_LOCK;
  INIT_PTHREAD_MUTEX_UNLOCK;
  INIT___PTHREAD_MUTEX_LOCK;
  INIT___PTHREAD_MUTEX_UNLOCK;
  INIT___LIBC_MUTEX_LOCK;
  INIT___LIBC_MUTEX_UNLOCK;
  INIT___LIBC_THR_SETCANCELSTATE;
  INIT_GETMNTENT;
  INIT_GETMNTENT_R;
  INIT_STATFS;
  INIT_STATFS64;
  INIT_STATVFS;
  INIT_STATVFS64;
  INIT_INITGROUPS;
  INIT_ETHER_NTOA_ATON;
  INIT_ETHER_HOST;
  INIT_ETHER_R;
  INIT_SHMCTL;
  INIT_RANDOM_R;
  INIT_PTHREAD_ATTR_GET;
  INIT_PTHREAD_ATTR_GET_SCHED;
  INIT_PTHREAD_ATTR_GETINHERITSCHED;
  INIT_PTHREAD_ATTR_GETAFFINITY_NP;
  INIT_PTHREAD_MUTEXATTR_GETPSHARED;
  INIT_PTHREAD_MUTEXATTR_GETTYPE;
  INIT_PTHREAD_MUTEXATTR_GETPROTOCOL;
  INIT_PTHREAD_MUTEXATTR_GETPRIOCEILING;
  INIT_PTHREAD_MUTEXATTR_GETROBUST;
  INIT_PTHREAD_MUTEXATTR_GETROBUST_NP;
  INIT_PTHREAD_RWLOCKATTR_GETPSHARED;
  INIT_PTHREAD_RWLOCKATTR_GETKIND_NP;
  INIT_PTHREAD_CONDATTR_GETPSHARED;
  INIT_PTHREAD_CONDATTR_GETCLOCK;
  INIT_PTHREAD_BARRIERATTR_GETPSHARED;
  INIT_TMPNAM;
  INIT_TMPNAM_R;
  INIT_TTYNAME_R;
  INIT_TEMPNAM;
  INIT_PTHREAD_SETNAME_NP;
  INIT_PTHREAD_GETNAME_NP;
  INIT_SINCOS;
  INIT_REMQUO;
  INIT_REMQUOL;
  INIT_LGAMMA;
  INIT_LGAMMAL;
  INIT_LGAMMA_R;
  INIT_LGAMMAL_R;
  INIT_DRAND48_R;
  INIT_RAND_R;
  INIT_GETLINE;
  INIT_ICONV;
  INIT_TIMES;
  INIT_TLS_GET_ADDR;
  INIT_LISTXATTR;
  INIT_GETXATTR;
  INIT_GETRESID;
  INIT_GETIFADDRS;
  INIT_IF_INDEXTONAME;
  INIT_CAPGET;
  INIT_AEABI_MEM;
  INIT___BZERO;
  INIT_FTIME;
  INIT_XDR;
  INIT_TSEARCH;
  INIT_LIBIO_INTERNALS;
  INIT_FOPEN;
  INIT_FOPEN64;
  INIT_OPEN_MEMSTREAM;
  INIT_OBSTACK;
  INIT_FFLUSH;
  INIT_FCLOSE;
  INIT_DLOPEN_DLCLOSE;
  INIT_GETPASS;
  INIT_TIMERFD;
  INIT_MLOCKX;
  INIT_FOPENCOOKIE;
  INIT_SEM;
  INIT_PTHREAD_SETCANCEL;
  INIT_MINCORE;
  INIT_PROCESS_VM_READV;
  INIT_CTERMID;
  INIT_CTERMID_R;
  INIT_RECV_RECVFROM;
  INIT_SEND_SENDTO;
  INIT_STAT;
  INIT_EVENTFD_READ_WRITE;
  INIT_LSTAT;
  INIT___XSTAT;
  INIT___XSTAT64;
  INIT___LXSTAT;
  INIT___LXSTAT64;
  // FIXME: add other *stat interceptors.
  INIT_UTMP;
  INIT_UTMPX;
  INIT_GETLOADAVG;
  INIT_WCSLEN;
  INIT_WCSCAT;
  INIT_WCSXFRM;
  INIT___WCSXFRM_L;
  INIT_ACCT;
  INIT_USER_FROM_UID;
  INIT_UID_FROM_USER;
  INIT_GROUP_FROM_GID;
  INIT_GID_FROM_GROUP;
  INIT_ACCESS;
  INIT_FACCESSAT;
  INIT_GETGROUPLIST;
  INIT_GETGROUPMEMBERSHIP;
  INIT_READLINK;
  INIT_READLINKAT;
  INIT_NAME_TO_HANDLE_AT;
  INIT_OPEN_BY_HANDLE_AT;
  INIT_STRLCPY;
  INIT_DEVNAME;
  INIT_DEVNAME_R;
  INIT_FGETLN;
  INIT_STRMODE;
  INIT_TTYENT;
  INIT_PROTOENT;
  INIT_NETENT;
  INIT_GETMNTINFO;
  INIT_MI_VECTOR_HASH;
  INIT_SETVBUF;
  INIT_GETVFSSTAT;
  INIT_REGEX;
  INIT_REGEXSUB;
  INIT_FTS;
  INIT_SYSCTL;
  INIT_ASYSCTL;
  INIT_SYSCTLGETMIBINFO;
  INIT_NL_LANGINFO;
  INIT_MODCTL;
  INIT_STRTONUM;
  INIT_FPARSELN;
  INIT_STATVFS1;
  INIT_STRTOI;
  INIT_CAPSICUM;
  INIT_SHA1;
  INIT_MD4;
  INIT_RMD160;
  INIT_MD5;
  INIT_FSEEK;
  INIT_MD2;
  INIT_SHA2;
  INIT_VIS;
  INIT_CDB;
  INIT_GETFSENT;
  INIT_ARC4RANDOM;
  INIT_POPEN;
  INIT_POPENVE;
  INIT_PCLOSE;
  INIT_FUNOPEN;
  INIT_FUNOPEN2;
  INIT_FDEVNAME;
  INIT_GETUSERSHELL;
  INIT_SL_INIT;

  INIT___PRINTF_CHK;
}
