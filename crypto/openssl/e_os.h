/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_E_OS_H
# define HEADER_E_OS_H

# include <limits.h>
# include <openssl/opensslconf.h>

# include <openssl/e_os2.h>
# include <openssl/crypto.h>
# include "internal/nelem.h"

/*
 * <openssl/e_os2.h> contains what we can justify to make visible to the
 * outside; this file e_os.h is not part of the exported interface.
 */

# ifndef DEVRANDOM
/*
 * set this to a comma-separated list of 'random' device files to try out. By
 * default, we will try to read at least one of these files
 */
#  if defined(__s390__)
#   define DEVRANDOM "/dev/prandom","/dev/urandom","/dev/hwrng","/dev/random"
#  else
#   define DEVRANDOM "/dev/urandom","/dev/random","/dev/srandom"
#  endif
# endif
# if !defined(OPENSSL_NO_EGD) && !defined(DEVRANDOM_EGD)
/*
 * set this to a comma-separated list of 'egd' sockets to try out. These
 * sockets will be tried in the order listed in case accessing the device
 * files listed in DEVRANDOM did not return enough randomness.
 */
#  define DEVRANDOM_EGD "/var/run/egd-pool","/dev/egd-pool","/etc/egd-pool","/etc/entropy"
# endif

# if defined(OPENSSL_SYS_VXWORKS) || defined(OPENSSL_SYS_UEFI)
#  define NO_CHMOD
#  define NO_SYSLOG
# endif

# define get_last_sys_error()    errno
# define clear_sys_error()       errno=0
# define set_sys_error(e)        errno=(e)

/********************************************************************
 The Microsoft section
 ********************************************************************/
# if defined(OPENSSL_SYS_WIN32) && !defined(WIN32)
#  define WIN32
# endif
# if defined(OPENSSL_SYS_WINDOWS) && !defined(WINDOWS)
#  define WINDOWS
# endif
# if defined(OPENSSL_SYS_MSDOS) && !defined(MSDOS)
#  define MSDOS
# endif

# ifdef WIN32
#  undef get_last_sys_error
#  undef clear_sys_error
#  undef set_sys_error
#  define get_last_sys_error()    GetLastError()
#  define clear_sys_error()       SetLastError(0)
#  define set_sys_error(e)        SetLastError(e)
#  if !defined(WINNT)
#   define WIN_CONSOLE_BUG
#  endif
# else
# endif

# if (defined(WINDOWS) || defined(MSDOS))

#  ifdef __DJGPP__
#   include <unistd.h>
#   include <sys/stat.h>
#   define _setmode setmode
#   define _O_TEXT O_TEXT
#   define _O_BINARY O_BINARY
#   define HAS_LFN_SUPPORT(name)  (pathconf((name), _PC_NAME_MAX) > 12)
#   undef DEVRANDOM_EGD  /*  Neither MS-DOS nor FreeDOS provide 'egd' sockets.  */
#   undef DEVRANDOM
#   define DEVRANDOM "/dev/urandom\x24"
#  endif                        /* __DJGPP__ */

#  ifndef S_IFDIR
#   define S_IFDIR     _S_IFDIR
#  endif

#  ifndef S_IFMT
#   define S_IFMT      _S_IFMT
#  endif

#  if !defined(WINNT) && !defined(__DJGPP__)
#   define NO_SYSLOG
#  endif

#  ifdef WINDOWS
#   if !defined(_WIN32_WCE) && !defined(_WIN32_WINNT)
       /*
        * Defining _WIN32_WINNT here in e_os.h implies certain "discipline."
        * Most notably we ought to check for availability of each specific
        * routine that was introduced after denoted _WIN32_WINNT with
        * GetProcAddress(). Normally newer functions are masked with higher
        * _WIN32_WINNT in SDK headers. So that if you wish to use them in
        * some module, you'd need to override _WIN32_WINNT definition in
        * the target module in order to "reach for" prototypes, but replace
        * calls to new functions with indirect calls. Alternatively it
        * might be possible to achieve the goal by /DELAYLOAD-ing .DLLs
        * and check for current OS version instead.
        */
#    define _WIN32_WINNT 0x0501
#   endif
#   if defined(_WIN32_WINNT) || defined(_WIN32_WCE)
       /*
        * Just like defining _WIN32_WINNT including winsock2.h implies
        * certain "discipline" for maintaining [broad] binary compatibility.
        * As long as structures are invariant among Winsock versions,
        * it's sufficient to check for specific Winsock2 API availability
        * at run-time [DSO_global_lookup is recommended]...
        */
#    include <winsock2.h>
#    include <ws2tcpip.h>
       /* yes, they have to be #included prior to <windows.h> */
#   endif
#   include <windows.h>
#   include <stdio.h>
#   include <stddef.h>
#   include <errno.h>
#   if defined(_WIN32_WCE) && !defined(EACCES)
#    define EACCES   13
#   endif
#   include <string.h>
#   ifdef _WIN64
#    define strlen(s) _strlen31(s)
/* cut strings to 2GB */
static __inline unsigned int _strlen31(const char *str)
{
    unsigned int len = 0;
    while (*str && len < 0x80000000U)
        str++, len++;
    return len & 0x7FFFFFFF;
}
#   endif
#   include <malloc.h>
#   if defined(_MSC_VER) && !defined(_WIN32_WCE) && !defined(_DLL) && defined(stdin)
#    if _MSC_VER>=1300 && _MSC_VER<1600
#     undef stdin
#     undef stdout
#     undef stderr
FILE *__iob_func();
#     define stdin  (&__iob_func()[0])
#     define stdout (&__iob_func()[1])
#     define stderr (&__iob_func()[2])
#    elif _MSC_VER<1300 && defined(I_CAN_LIVE_WITH_LNK4049)
#     undef stdin
#     undef stdout
#     undef stderr
         /*
          * pre-1300 has __p__iob(), but it's available only in msvcrt.lib,
          * or in other words with /MD. Declaring implicit import, i.e. with
          * _imp_ prefix, works correctly with all compiler options, but
          * without /MD results in LINK warning LNK4049: 'locally defined
          * symbol "__iob" imported'.
          */
extern FILE *_imp___iob;
#     define stdin  (&_imp___iob[0])
#     define stdout (&_imp___iob[1])
#     define stderr (&_imp___iob[2])
#    endif
#   endif
#  endif
#  include <io.h>
#  include <fcntl.h>

#  ifdef OPENSSL_SYS_WINCE
#   define OPENSSL_NO_POSIX_IO
#  endif

#  define EXIT(n) exit(n)
#  define LIST_SEPARATOR_CHAR ';'
#  ifndef W_OK
#   define W_OK        2
#  endif
#  ifndef R_OK
#   define R_OK        4
#  endif
#  ifdef OPENSSL_SYS_WINCE
#   define DEFAULT_HOME  ""
#  else
#   define DEFAULT_HOME  "C:"
#  endif

/* Avoid Visual Studio 13 GetVersion deprecated problems */
#  if defined(_MSC_VER) && _MSC_VER>=1800
#   define check_winnt() (1)
#   define check_win_minplat(x) (1)
#  else
#   define check_winnt() (GetVersion() < 0x80000000)
#   define check_win_minplat(x) (LOBYTE(LOWORD(GetVersion())) >= (x))
#  endif

# else                          /* The non-microsoft world */

#  if defined(OPENSSL_SYS_VXWORKS)
#   include <sys/times.h>
#  else
#   include <sys/time.h>
#  endif

#  ifdef OPENSSL_SYS_VMS
#   define VMS 1
  /*
   * some programs don't include stdlib, so exit() and others give implicit
   * function warnings
   */
#   include <stdlib.h>
#   if defined(__DECC)
#    include <unistd.h>
#   else
#    include <unixlib.h>
#   endif
#   define LIST_SEPARATOR_CHAR ','
  /* We don't have any well-defined random devices on VMS, yet... */
#   undef DEVRANDOM
  /*-
     We need to do this since VMS has the following coding on status codes:

     Bits 0-2: status type: 0 = warning, 1 = success, 2 = error, 3 = info ...
               The important thing to know is that odd numbers are considered
               good, while even ones are considered errors.
     Bits 3-15: actual status number
     Bits 16-27: facility number.  0 is considered "unknown"
     Bits 28-31: control bits.  If bit 28 is set, the shell won't try to
                 output the message (which, for random codes, just looks ugly)

     So, what we do here is to change 0 to 1 to get the default success status,
     and everything else is shifted up to fit into the status number field, and
     the status is tagged as an error, which is what is wanted here.

     Finally, we add the VMS C facility code 0x35a000, because there are some
     programs, such as Perl, that will reinterpret the code back to something
     POSIX.  'man perlvms' explains it further.

     NOTE: the perlvms manual wants to turn all codes 2 to 255 into success
     codes (status type = 1).  I couldn't disagree more.  Fortunately, the
     status type doesn't seem to bother Perl.
     -- Richard Levitte
  */
#   define EXIT(n)  exit((n) ? (((n) << 3) | 2 | 0x10000000 | 0x35a000) : 1)

#   define DEFAULT_HOME "SYS$LOGIN:"

#  else
     /* !defined VMS */
#   ifdef OPENSSL_UNISTD
#    include OPENSSL_UNISTD
#   else
#    include <unistd.h>
#   endif
#   include <sys/types.h>
#   ifdef OPENSSL_SYS_WIN32_CYGWIN
#    include <io.h>
#    include <fcntl.h>
#   endif

#   define LIST_SEPARATOR_CHAR ':'
#   define EXIT(n)             exit(n)
#  endif

# endif

/***********************************************/

# if defined(OPENSSL_SYS_WINDOWS)
#  define strcasecmp _stricmp
#  define strncasecmp _strnicmp
#  if (_MSC_VER >= 1310)
#   define open _open
#   define fdopen _fdopen
#   define close _close
#   ifndef strdup
#    define strdup _strdup
#   endif
#   define unlink _unlink
#   define fileno _fileno
#  endif
# else
#  include <strings.h>
# endif

/* vxworks */
# if defined(OPENSSL_SYS_VXWORKS)
#  include <ioLib.h>
#  include <tickLib.h>
#  include <sysLib.h>
#  include <vxWorks.h>
#  include <sockLib.h>
#  include <taskLib.h>

#  define TTY_STRUCT int
#  define sleep(a) taskDelay((a) * sysClkRateGet())

/*
 * NOTE: these are implemented by helpers in database app! if the database is
 * not linked, we need to implement them elsewhere
 */
struct hostent *gethostbyname(const char *name);
struct hostent *gethostbyaddr(const char *addr, int length, int type);
struct servent *getservbyname(const char *name, const char *proto);

# endif
/* end vxworks */

# ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#  define CRYPTO_memcmp memcmp
# endif

/* unistd.h defines _POSIX_VERSION */
# if !defined(OPENSSL_NO_SECURE_MEMORY) && defined(OPENSSL_SYS_UNIX) \
     && ( (defined(_POSIX_VERSION) && _POSIX_VERSION >= 200112L)      \
          || defined(__sun) || defined(__hpux) || defined(__sgi)      \
          || defined(__osf__) )
#  define OPENSSL_SECURE_MEMORY  /* secure memory is implemented */
# endif
#endif
