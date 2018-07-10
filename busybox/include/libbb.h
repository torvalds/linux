/* vi: set sw=4 ts=4: */
/*
 * Busybox main internal header file
 *
 * Based in part on code from sash, Copyright (c) 1999 by David I. Bell
 * Permission has been granted to redistribute this code under GPL.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#ifndef LIBBB_H
#define LIBBB_H 1

#include "platform.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <setjmp.h>
#include <signal.h>
#include <paths.h>
#if defined __UCLIBC__ /* TODO: and glibc? */
/* use inlined versions of these: */
# define sigfillset(s)    __sigfillset(s)
# define sigemptyset(s)   __sigemptyset(s)
# define sigisemptyset(s) __sigisemptyset(s)
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
/* There are two incompatible basename's, let's not use them! */
/* See the dirname/basename man page for details */
#include <libgen.h> /* dirname,basename */
#undef basename
#define basename dont_use_basename
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#if !defined(major) || defined(__GLIBC__)
# include <sys/sysmacros.h>
#endif
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <sys/param.h>
#include <pwd.h>
#include <grp.h>
#if ENABLE_FEATURE_SHADOWPASSWDS
# if !ENABLE_USE_BB_SHADOW
/* If using busybox's shadow implementation, do not include the shadow.h
 * header as the toolchain may not provide it at all.
 */
#  include <shadow.h>
# endif
#endif
#if defined(ANDROID) || defined(__ANDROID__)
# define endpwent() ((void)0)
# define endgrent() ((void)0)
#endif
#ifdef HAVE_MNTENT_H
# include <mntent.h>
#endif
#ifdef HAVE_SYS_STATFS_H
# include <sys/statfs.h>
#endif
/* Don't do this here:
 * #include <sys/sysinfo.h>
 * Some linux/ includes pull in conflicting definition
 * of struct sysinfo (only in some toolchanins), which breaks build.
 * Include sys/sysinfo.h only in those files which need it.
 */
#if ENABLE_SELINUX
# include <selinux/selinux.h>
# include <selinux/context.h>
#endif
#if ENABLE_FEATURE_UTMP
# if defined __UCLIBC__ && ( \
    (UCLIBC_VERSION >= KERNEL_VERSION(0, 9, 32) \
     && UCLIBC_VERSION < KERNEL_VERSION(0, 9, 34) \
     && defined __UCLIBC_HAS_UTMPX__ \
    ) || ( \
	 UCLIBC_VERSION >= KERNEL_VERSION(0, 9, 34) \
	) \
  )
#  include <utmpx.h>
# elif defined __UCLIBC__
#  include <utmp.h>
#  define utmpx utmp
#  define setutxent setutent
#  define endutxent endutent
#  define getutxent getutent
#  define getutxid getutid
#  define getutxline getutline
#  define pututxline pututline
#  define utmpxname utmpname
#  define updwtmpx updwtmp
#  define _PATH_UTMPX _PATH_UTMP
# else
#  include <utmp.h>
#  include <utmpx.h>
#  if defined _PATH_UTMP && !defined _PATH_UTMPX
#   define _PATH_UTMPX _PATH_UTMP
#  endif
# endif
#endif
#if ENABLE_LOCALE_SUPPORT
# include <locale.h>
#else
# define setlocale(x,y) ((void)0)
#endif
#ifdef DMALLOC
# include <dmalloc.h>
#endif
/* Just in case libc doesn't define some of these... */
#ifndef _PATH_PASSWD
#define _PATH_PASSWD  "/etc/passwd"
#endif
#ifndef _PATH_GROUP
#define _PATH_GROUP   "/etc/group"
#endif
#ifndef _PATH_SHADOW
#define _PATH_SHADOW  "/etc/shadow"
#endif
#ifndef _PATH_GSHADOW
#define _PATH_GSHADOW "/etc/gshadow"
#endif
#if defined __FreeBSD__ || defined __OpenBSD__
# include <netinet/in.h>
# include <arpa/inet.h>
#elif defined __APPLE__
# include <netinet/in.h>
#else
# include <arpa/inet.h>
//This breaks on bionic:
//# if !defined(__socklen_t_defined) && !defined(_SOCKLEN_T_DECLARED)
///* We #define socklen_t *after* includes, otherwise we get
// * typedef redefinition errors from system headers
// * (in case "is it defined already" detection above failed)
// */
//#  define socklen_t bb_socklen_t
//   typedef unsigned socklen_t;
//# endif
//if this is still needed, add a fix along the lines of
//  ifdef SPECIFIC_BROKEN_LIBC_CHECK / typedef socklen_t / endif
//in platform.h instead!
#endif
#ifndef HAVE_CLEARENV
# define clearenv() do { if (environ) environ[0] = NULL; } while (0)
#endif
#ifndef HAVE_FDATASYNC
# define fdatasync fsync
#endif
#ifndef HAVE_XTABS
# define XTABS TAB3
#endif
/*
 * Use '%m' to append error string on platforms that support it,
 * '%s' and strerror() on those that don't.
 */
#ifdef HAVE_PRINTF_PERCENTM
# define STRERROR_FMT    "%m"
# define STRERROR_ERRNO  /*nothing*/
#else
# define STRERROR_FMT    "%s"
# define STRERROR_ERRNO  ,strerror(errno)
#endif


/* Some libc's forget to declare these, do it ourself */

extern char **environ;
/* klogctl is in libc's klog.h, but we cheat and not #include that */
int klogctl(int type, char *b, int len);
#ifndef PATH_MAX
# define PATH_MAX 256
#endif
#ifndef BUFSIZ
# define BUFSIZ 4096
#endif


/* Busybox does not use threads, we can speed up stdio. */
#ifdef HAVE_UNLOCKED_STDIO
# undef  getc
# define getc(stream)   getc_unlocked(stream)
# undef  getchar
# define getchar()      getchar_unlocked()
# undef  putc
# define putc(c,stream) putc_unlocked(c,stream)
# undef  putchar
# define putchar(c)     putchar_unlocked(c)
# undef  fgetc
# define fgetc(stream)  getc_unlocked(stream)
# undef  fputc
# define fputc(c,stream) putc_unlocked(c,stream)
#endif
/* Above functions are required by POSIX.1-2008, below ones are extensions */
#ifdef HAVE_UNLOCKED_LINE_OPS
# undef  fgets
# define fgets(s,n,stream) fgets_unlocked(s,n,stream)
# undef  fputs
# define fputs(s,stream) fputs_unlocked(s,stream)
/* musl <= 1.1.15 does not support fflush_unlocked(NULL) */
//# undef  fflush
//# define fflush(stream) fflush_unlocked(stream)
# undef  feof
# define feof(stream)   feof_unlocked(stream)
# undef  ferror
# define ferror(stream) ferror_unlocked(stream)
# undef  fileno
# define fileno(stream) fileno_unlocked(stream)
#endif


/* Make all declarations hidden (-fvisibility flag only affects definitions) */
/* (don't include system headers after this until corresponding pop!) */
PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN


#if ENABLE_USE_BB_PWD_GRP
# include "pwd_.h"
# include "grp_.h"
#endif
#if ENABLE_FEATURE_SHADOWPASSWDS
# if ENABLE_USE_BB_SHADOW
#  include "shadow_.h"
# endif
#endif

/* Tested to work correctly with all int types (IIRC :]) */
#define MAXINT(T) (T)( \
	((T)-1) > 0 \
	? (T)-1 \
	: (T)~((T)1 << (sizeof(T)*8-1)) \
	)

#define MININT(T) (T)( \
	((T)-1) > 0 \
	? (T)0 \
	: ((T)1 << (sizeof(T)*8-1)) \
	)

/* Large file support */
/* Note that CONFIG_LFS=y forces bbox to be built with all common ops
 * (stat, lseek etc) mapped to "largefile" variants by libc.
 * Practically it means that open() automatically has O_LARGEFILE added
 * and all filesize/file_offset parameters and struct members are "large"
 * (in today's world - signed 64bit). For full support of large files,
 * we need a few helper #defines (below) and careful use of off_t
 * instead of int/ssize_t. No lseek64(), O_LARGEFILE etc necessary */
#if ENABLE_LFS
/* CONFIG_LFS is on */
# if ULONG_MAX > 0xffffffff
/* "long" is long enough on this system */
typedef unsigned long uoff_t;
#  define XATOOFF(a) xatoul_range((a), 0, LONG_MAX)
/* usage: sz = BB_STRTOOFF(s, NULL, 10); if (errno || sz < 0) die(); */
#  define BB_STRTOOFF bb_strtoul
#  define STRTOOFF strtoul
/* usage: printf("size: %"OFF_FMT"d (%"OFF_FMT"x)\n", sz, sz); */
#  define OFF_FMT "l"
# else
/* "long" is too short, need "long long" */
typedef unsigned long long uoff_t;
#  define XATOOFF(a) xatoull_range((a), 0, LLONG_MAX)
#  define BB_STRTOOFF bb_strtoull
#  define STRTOOFF strtoull
#  define OFF_FMT "ll"
# endif
#else
/* CONFIG_LFS is off */
# if UINT_MAX == 0xffffffff
/* While sizeof(off_t) == sizeof(int), off_t is typedef'ed to long anyway.
 * gcc will throw warnings on printf("%d", off_t). Crap... */
typedef unsigned long uoff_t;
#  define XATOOFF(a) xatoi_positive(a)
#  define BB_STRTOOFF bb_strtou
#  define STRTOOFF strtol
#  define OFF_FMT "l"
# else
typedef unsigned long uoff_t;
#  define XATOOFF(a) xatoul_range((a), 0, LONG_MAX)
#  define BB_STRTOOFF bb_strtoul
#  define STRTOOFF strtol
#  define OFF_FMT "l"
# endif
#endif
/* scary. better ideas? (but do *test* them first!) */
#define OFF_T_MAX  ((off_t)~((off_t)1 << (sizeof(off_t)*8-1)))
/* Users report bionic to use 32-bit off_t even if LARGEFILE support is requested.
 * We misdetected that. Don't let it build:
 */
struct BUG_off_t_size_is_misdetected {
	char BUG_off_t_size_is_misdetected[sizeof(off_t) == sizeof(uoff_t) ? 1 : -1];
};

/* Some useful definitions */
#undef FALSE
#define FALSE   ((int) 0)
#undef TRUE
#define TRUE    ((int) 1)
#undef SKIP
#define SKIP	((int) 2)

/* Macros for min/max.  */
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

/* buffer allocation schemes */
#if ENABLE_FEATURE_BUFFERS_GO_ON_STACK
#define RESERVE_CONFIG_BUFFER(buffer,len)  char buffer[len]
#define RESERVE_CONFIG_UBUFFER(buffer,len) unsigned char buffer[len]
#define RELEASE_CONFIG_BUFFER(buffer)      ((void)0)
#else
#if ENABLE_FEATURE_BUFFERS_GO_IN_BSS
#define RESERVE_CONFIG_BUFFER(buffer,len)  static          char buffer[len]
#define RESERVE_CONFIG_UBUFFER(buffer,len) static unsigned char buffer[len]
#define RELEASE_CONFIG_BUFFER(buffer)      ((void)0)
#else
#define RESERVE_CONFIG_BUFFER(buffer,len)  char *buffer = xmalloc(len)
#define RESERVE_CONFIG_UBUFFER(buffer,len) unsigned char *buffer = xmalloc(len)
#define RELEASE_CONFIG_BUFFER(buffer)      free(buffer)
#endif
#endif

#if defined(__GLIBC__)
/* glibc uses __errno_location() to get a ptr to errno */
/* We can just memorize it once - no multithreading in busybox :) */
extern int *const bb_errno;
#undef errno
#define errno (*bb_errno)
#endif

#if !(ULONG_MAX > 0xffffffff)
/* Only 32-bit CPUs need this, 64-bit ones use inlined version */
uint64_t bb_bswap_64(uint64_t x) FAST_FUNC;
#endif

unsigned long FAST_FUNC isqrt(unsigned long long N);

unsigned long long monotonic_ns(void) FAST_FUNC;
unsigned long long monotonic_us(void) FAST_FUNC;
unsigned long long monotonic_ms(void) FAST_FUNC;
unsigned monotonic_sec(void) FAST_FUNC;

extern void chomp(char *s) FAST_FUNC;
extern char *trim(char *s) FAST_FUNC;
extern char *skip_whitespace(const char *) FAST_FUNC;
extern char *skip_non_whitespace(const char *) FAST_FUNC;
extern char *skip_dev_pfx(const char *tty_name) FAST_FUNC;

extern char *strrstr(const char *haystack, const char *needle) FAST_FUNC;

/* dmalloc will redefine these to it's own implementation. It is safe
 * to have the prototypes here unconditionally.  */
void *malloc_or_warn(size_t size) FAST_FUNC RETURNS_MALLOC;
void *xmalloc(size_t size) FAST_FUNC RETURNS_MALLOC;
void *xzalloc(size_t size) FAST_FUNC RETURNS_MALLOC;
void *xrealloc(void *old, size_t size) FAST_FUNC;
/* After v = xrealloc_vector(v, SHIFT, idx) it's ok to use
 * at least v[idx] and v[idx+1], for all idx values.
 * SHIFT specifies how many new elements are added (1:2, 2:4, ..., 8:256...)
 * when all elements are used up. New elements are zeroed out.
 * xrealloc_vector(v, SHIFT, idx) *MUST* be called with consecutive IDXs -
 * skipping an index is a bad bug - it may miss a realloc!
 */
#define xrealloc_vector(vector, shift, idx) \
	xrealloc_vector_helper((vector), (sizeof((vector)[0]) << 8) + (shift), (idx))
void* xrealloc_vector_helper(void *vector, unsigned sizeof_and_shift, int idx) FAST_FUNC;
char *xstrdup(const char *s) FAST_FUNC RETURNS_MALLOC;
char *xstrndup(const char *s, int n) FAST_FUNC RETURNS_MALLOC;
void *xmemdup(const void *s, int n) FAST_FUNC RETURNS_MALLOC;


//TODO: supply a pointer to char[11] buffer (avoid statics)?
extern const char *bb_mode_string(mode_t mode) FAST_FUNC;
extern int is_directory(const char *name, int followLinks) FAST_FUNC;
enum {	/* cp.c, mv.c, install.c depend on these values. CAREFUL when changing them! */
	FILEUTILS_PRESERVE_STATUS = 1 << 0, /* -p */
	FILEUTILS_DEREFERENCE     = 1 << 1, /* !-d */
	FILEUTILS_RECUR           = 1 << 2, /* -R */
	FILEUTILS_FORCE           = 1 << 3, /* -f */
	FILEUTILS_INTERACTIVE     = 1 << 4, /* -i */
	FILEUTILS_MAKE_HARDLINK   = 1 << 5, /* -l */
	FILEUTILS_MAKE_SOFTLINK   = 1 << 6, /* -s */
	FILEUTILS_DEREF_SOFTLINK  = 1 << 7, /* -L */
	FILEUTILS_DEREFERENCE_L0  = 1 << 8, /* -H */
	/* -a = -pdR (mapped in cp.c) */
	/* -r = -dR  (mapped in cp.c) */
	/* -P = -d   (mapped in cp.c) */
	FILEUTILS_VERBOSE         = (1 << 12) * ENABLE_FEATURE_VERBOSE,	/* -v */
	FILEUTILS_UPDATE          = 1 << 13, /* -u */
	FILEUTILS_NO_TARGET_DIR	  = 1 << 14, /* -T */
#if ENABLE_SELINUX
	FILEUTILS_PRESERVE_SECURITY_CONTEXT = 1 << 15, /* -c */
#endif
	FILEUTILS_RMDEST          = 1 << (16 - !ENABLE_SELINUX), /* --remove-destination */
	/*
	 * Hole. cp may have some bits set here,
	 * they should not affect remove_file()/copy_file()
	 */
#if ENABLE_SELINUX
	FILEUTILS_SET_SECURITY_CONTEXT = 1 << 30,
#endif
	FILEUTILS_IGNORE_CHMOD_ERR = 1 << 31,
};
#define FILEUTILS_CP_OPTSTR "pdRfilsLHarPvuT" IF_SELINUX("c")
extern int remove_file(const char *path, int flags) FAST_FUNC;
/* NB: without FILEUTILS_RECUR in flags, it will basically "cat"
 * the source, not copy (unless "source" is a directory).
 * This makes "cp /dev/null file" and "install /dev/null file" (!!!)
 * work coreutils-compatibly. */
extern int copy_file(const char *source, const char *dest, int flags) FAST_FUNC;

enum {
	ACTION_RECURSE        = (1 << 0),
	ACTION_FOLLOWLINKS    = (1 << 1),
	ACTION_FOLLOWLINKS_L0 = (1 << 2),
	ACTION_DEPTHFIRST     = (1 << 3),
	/*ACTION_REVERSE      = (1 << 4), - unused */
	ACTION_QUIET          = (1 << 5),
	ACTION_DANGLING_OK    = (1 << 6),
};
typedef uint8_t recurse_flags_t;
extern int recursive_action(const char *fileName, unsigned flags,
	int FAST_FUNC (*fileAction)(const char *fileName, struct stat* statbuf, void* userData, int depth),
	int FAST_FUNC (*dirAction)(const char *fileName, struct stat* statbuf, void* userData, int depth),
	void* userData, unsigned depth) FAST_FUNC;
extern int device_open(const char *device, int mode) FAST_FUNC;
enum { GETPTY_BUFSIZE = 16 }; /* more than enough for "/dev/ttyXXX" */
extern int xgetpty(char *line) FAST_FUNC;
extern int get_console_fd_or_die(void) FAST_FUNC;
extern void console_make_active(int fd, const int vt_num) FAST_FUNC;
extern char *find_block_device(const char *path) FAST_FUNC;
/* bb_copyfd_XX print read/write errors and return -1 if they occur */
extern off_t bb_copyfd_eof(int fd1, int fd2) FAST_FUNC;
extern off_t bb_copyfd_size(int fd1, int fd2, off_t size) FAST_FUNC;
extern void bb_copyfd_exact_size(int fd1, int fd2, off_t size) FAST_FUNC;
/* "short" copy can be detected by return value < size */
/* this helper yells "short read!" if param is not -1 */
extern void complain_copyfd_and_die(off_t sz) NORETURN FAST_FUNC;

extern char bb_process_escape_sequence(const char **ptr) FAST_FUNC;
char* strcpy_and_process_escape_sequences(char *dst, const char *src) FAST_FUNC;
/* xxxx_strip version can modify its parameter:
 * "/"        -> "/"
 * "abc"      -> "abc"
 * "abc/def"  -> "def"
 * "abc/def/" -> "def" !!
 */
char *bb_get_last_path_component_strip(char *path) FAST_FUNC;
/* "abc/def/" -> "" and it never modifies 'path' */
char *bb_get_last_path_component_nostrip(const char *path) FAST_FUNC;
/* Simpler version: does not special case "/" string */
const char *bb_basename(const char *name) FAST_FUNC;
/* NB: can violate const-ness (similarly to strchr) */
char *last_char_is(const char *s, int c) FAST_FUNC;
const char* endofname(const char *name) FAST_FUNC;
char *is_prefixed_with(const char *string, const char *key) FAST_FUNC;
char *is_suffixed_with(const char *string, const char *key) FAST_FUNC;

int ndelay_on(int fd) FAST_FUNC;
int ndelay_off(int fd) FAST_FUNC;
void close_on_exec_on(int fd) FAST_FUNC;
void xdup2(int, int) FAST_FUNC;
void xmove_fd(int, int) FAST_FUNC;


DIR *xopendir(const char *path) FAST_FUNC;
DIR *warn_opendir(const char *path) FAST_FUNC;

char *xmalloc_realpath(const char *path) FAST_FUNC RETURNS_MALLOC;
char *xmalloc_realpath_coreutils(const char *path) FAST_FUNC RETURNS_MALLOC;
char *xmalloc_readlink(const char *path) FAST_FUNC RETURNS_MALLOC;
char *xmalloc_readlink_or_warn(const char *path) FAST_FUNC RETURNS_MALLOC;
/* !RETURNS_MALLOC: it's a realloc-like function */
char *xrealloc_getcwd_or_warn(char *cwd) FAST_FUNC;

char *xmalloc_follow_symlinks(const char *path) FAST_FUNC RETURNS_MALLOC;


enum {
	/* bb_signals(BB_FATAL_SIGS, handler) catches all signals which
	 * otherwise would kill us, except for those resulting from bugs:
	 * SIGSEGV, SIGILL, SIGFPE.
	 * Other fatal signals not included (TODO?):
	 * SIGBUS   Bus error (bad memory access)
	 * SIGPOLL  Pollable event. Synonym of SIGIO
	 * SIGPROF  Profiling timer expired
	 * SIGSYS   Bad argument to routine
	 * SIGTRAP  Trace/breakpoint trap
	 *
	 * The only known arch with some of these sigs not fitting
	 * into 32 bits is parisc (SIGXCPU=33, SIGXFSZ=34, SIGSTKFLT=36).
	 * Dance around with long long to guard against that...
	 */
	BB_FATAL_SIGS = (int)(0
		+ (1LL << SIGHUP)
		+ (1LL << SIGINT)
		+ (1LL << SIGTERM)
		+ (1LL << SIGPIPE)   // Write to pipe with no readers
		+ (1LL << SIGQUIT)   // Quit from keyboard
		+ (1LL << SIGABRT)   // Abort signal from abort(3)
		+ (1LL << SIGALRM)   // Timer signal from alarm(2)
		+ (1LL << SIGVTALRM) // Virtual alarm clock
		+ (1LL << SIGXCPU)   // CPU time limit exceeded
		+ (1LL << SIGXFSZ)   // File size limit exceeded
		+ (1LL << SIGUSR1)   // Yes kids, these are also fatal!
		+ (1LL << SIGUSR2)
		+ 0),
};
void bb_signals(int sigs, void (*f)(int)) FAST_FUNC;
/* Unlike signal() and bb_signals, sets handler with sigaction()
 * and in a way that while signal handler is run, no other signals
 * will be blocked; syscalls will not be restarted: */
void bb_signals_recursive_norestart(int sigs, void (*f)(int)) FAST_FUNC;
/* syscalls like read() will be interrupted with EINTR: */
void signal_no_SA_RESTART_empty_mask(int sig, void (*handler)(int)) FAST_FUNC;
/* syscalls like read() won't be interrupted (though select/poll will be): */
void signal_SA_RESTART_empty_mask(int sig, void (*handler)(int)) FAST_FUNC;
void wait_for_any_sig(void) FAST_FUNC;
void kill_myself_with_sig(int sig) NORETURN FAST_FUNC;
void sig_block(int sig) FAST_FUNC;
void sig_unblock(int sig) FAST_FUNC;
/* Will do sigaction(signum, act, NULL): */
int sigaction_set(int sig, const struct sigaction *act) FAST_FUNC;
/* SIG_BLOCK/SIG_UNBLOCK all signals: */
int sigprocmask_allsigs(int how) FAST_FUNC;
/* Standard handler which just records signo */
extern smallint bb_got_signal;
void record_signo(int signo); /* not FAST_FUNC! */


void xsetgid(gid_t gid) FAST_FUNC;
void xsetuid(uid_t uid) FAST_FUNC;
void xsetegid(gid_t egid) FAST_FUNC;
void xseteuid(uid_t euid) FAST_FUNC;
void xchdir(const char *path) FAST_FUNC;
void xfchdir(int fd) FAST_FUNC;
void xchroot(const char *path) FAST_FUNC;
void xsetenv(const char *key, const char *value) FAST_FUNC;
void bb_unsetenv(const char *key) FAST_FUNC;
void bb_unsetenv_and_free(char *key) FAST_FUNC;
void xunlink(const char *pathname) FAST_FUNC;
void xstat(const char *pathname, struct stat *buf) FAST_FUNC;
void xfstat(int fd, struct stat *buf, const char *errmsg) FAST_FUNC;
int open3_or_warn(const char *pathname, int flags, int mode) FAST_FUNC;
int open_or_warn(const char *pathname, int flags) FAST_FUNC;
int xopen3(const char *pathname, int flags, int mode) FAST_FUNC;
int xopen(const char *pathname, int flags) FAST_FUNC;
int xopen_nonblocking(const char *pathname) FAST_FUNC;
int xopen_as_uid_gid(const char *pathname, int flags, uid_t u, gid_t g) FAST_FUNC;
int open_or_warn_stdin(const char *pathname) FAST_FUNC;
int xopen_stdin(const char *pathname) FAST_FUNC;
void xrename(const char *oldpath, const char *newpath) FAST_FUNC;
int rename_or_warn(const char *oldpath, const char *newpath) FAST_FUNC;
off_t xlseek(int fd, off_t offset, int whence) FAST_FUNC;
int xmkstemp(char *template) FAST_FUNC;
off_t fdlength(int fd) FAST_FUNC;

uoff_t FAST_FUNC get_volume_size_in_bytes(int fd,
		const char *override,
		unsigned override_units,
		int extend);

void xpipe(int filedes[2]) FAST_FUNC;
/* In this form code with pipes is much more readable */
struct fd_pair { int rd; int wr; };
#define piped_pair(pair)  pipe(&((pair).rd))
#define xpiped_pair(pair) xpipe(&((pair).rd))

/* Useful for having small structure members/global variables */
typedef int8_t socktype_t;
typedef int8_t family_t;
struct BUG_too_small {
	char BUG_socktype_t_too_small[(0
			| SOCK_STREAM
			| SOCK_DGRAM
			| SOCK_RDM
			| SOCK_SEQPACKET
			| SOCK_RAW
			) <= 127 ? 1 : -1];
	char BUG_family_t_too_small[(0
			| AF_UNSPEC
			| AF_INET
			| AF_INET6
			| AF_UNIX
#ifdef AF_PACKET
			| AF_PACKET
#endif
#ifdef AF_NETLINK
			| AF_NETLINK
#endif
			/* | AF_DECnet */
			/* | AF_IPX */
			) <= 127 ? 1 : -1];
};


void parse_datestr(const char *date_str, struct tm *ptm) FAST_FUNC;
time_t validate_tm_time(const char *date_str, struct tm *ptm) FAST_FUNC;
char *strftime_HHMMSS(char *buf, unsigned len, time_t *tp) FAST_FUNC;
char *strftime_YYYYMMDDHHMMSS(char *buf, unsigned len, time_t *tp) FAST_FUNC;

int xsocket(int domain, int type, int protocol) FAST_FUNC;
void xbind(int sockfd, struct sockaddr *my_addr, socklen_t addrlen) FAST_FUNC;
void xlisten(int s, int backlog) FAST_FUNC;
void xconnect(int s, const struct sockaddr *s_addr, socklen_t addrlen) FAST_FUNC;
ssize_t xsendto(int s, const void *buf, size_t len, const struct sockaddr *to,
				socklen_t tolen) FAST_FUNC;

int setsockopt_int(int fd, int level, int optname, int optval) FAST_FUNC;
int setsockopt_1(int fd, int level, int optname) FAST_FUNC;
int setsockopt_SOL_SOCKET_int(int fd, int optname, int optval) FAST_FUNC;
int setsockopt_SOL_SOCKET_1(int fd, int optname) FAST_FUNC;
/* SO_REUSEADDR allows a server to rebind to an address that is already
 * "in use" by old connections to e.g. previous server instance which is
 * killed or crashed. Without it bind will fail until all such connections
 * time out. Linux does not allow multiple live binds on same ip:port
 * regardless of SO_REUSEADDR (unlike some other flavors of Unix).
 * Turn it on before you call bind(). */
void setsockopt_reuseaddr(int fd) FAST_FUNC; /* On Linux this never fails. */
int setsockopt_keepalive(int fd) FAST_FUNC;
int setsockopt_broadcast(int fd) FAST_FUNC;
int setsockopt_bindtodevice(int fd, const char *iface) FAST_FUNC;
int bb_getsockname(int sockfd, void *addr, socklen_t addrlen) FAST_FUNC;
/* NB: returns port in host byte order */
unsigned bb_lookup_port(const char *port, const char *protocol, unsigned default_port) FAST_FUNC;
#if ENABLE_FEATURE_ETC_SERVICES
# define bb_lookup_std_port(portstr, protocol, portnum) bb_lookup_port(portstr, protocol, portnum)
#else
# define bb_lookup_std_port(portstr, protocol, portnum) (portnum)
#endif
typedef struct len_and_sockaddr {
	socklen_t len;
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
#if ENABLE_FEATURE_IPV6
		struct sockaddr_in6 sin6;
#endif
	} u;
} len_and_sockaddr;
enum {
	LSA_LEN_SIZE = offsetof(len_and_sockaddr, u),
	LSA_SIZEOF_SA = sizeof(
		union {
			struct sockaddr sa;
			struct sockaddr_in sin;
#if ENABLE_FEATURE_IPV6
			struct sockaddr_in6 sin6;
#endif
		}
	)
};
/* Create stream socket, and allocate suitable lsa.
 * (lsa of correct size and lsa->sa.sa_family (AF_INET/AF_INET6))
 * af == AF_UNSPEC will result in trying to create IPv6 socket,
 * and if kernel doesn't support it, fall back to IPv4.
 * This is useful if you plan to bind to resulting local lsa.
 */
int xsocket_type(len_and_sockaddr **lsap, int af, int sock_type) FAST_FUNC;
int xsocket_stream(len_and_sockaddr **lsap) FAST_FUNC;
/* Create server socket bound to bindaddr:port. bindaddr can be NULL,
 * numeric IP ("N.N.N.N") or numeric IPv6 address,
 * and can have ":PORT" suffix (for IPv6 use "[X:X:...:X]:PORT").
 * Only if there is no suffix, port argument is used */
/* NB: these set SO_REUSEADDR before bind */
int create_and_bind_stream_or_die(const char *bindaddr, int port) FAST_FUNC;
int create_and_bind_dgram_or_die(const char *bindaddr, int port) FAST_FUNC;
/* Create client TCP socket connected to peer:port. Peer cannot be NULL.
 * Peer can be numeric IP ("N.N.N.N"), numeric IPv6 address or hostname,
 * and can have ":PORT" suffix (for IPv6 use "[X:X:...:X]:PORT").
 * If there is no suffix, port argument is used */
int create_and_connect_stream_or_die(const char *peer, int port) FAST_FUNC;
/* Connect to peer identified by lsa */
int xconnect_stream(const len_and_sockaddr *lsa) FAST_FUNC;
/* Get local address of bound or accepted socket */
len_and_sockaddr *get_sock_lsa(int fd) FAST_FUNC RETURNS_MALLOC;
/* Get remote address of connected or accepted socket */
len_and_sockaddr *get_peer_lsa(int fd) FAST_FUNC RETURNS_MALLOC;
/* Return malloc'ed len_and_sockaddr with socket address of host:port
 * Currently will return IPv4 or IPv6 sockaddrs only
 * (depending on host), but in theory nothing prevents e.g.
 * UNIX socket address being returned, IPX sockaddr etc...
 * On error does bb_error_msg and returns NULL */
len_and_sockaddr* host2sockaddr(const char *host, int port) FAST_FUNC RETURNS_MALLOC;
/* Version which dies on error */
len_and_sockaddr* xhost2sockaddr(const char *host, int port) FAST_FUNC RETURNS_MALLOC;
len_and_sockaddr* xdotted2sockaddr(const char *host, int port) FAST_FUNC RETURNS_MALLOC;
/* Same, useful if you want to force family (e.g. IPv6) */
#if !ENABLE_FEATURE_IPV6
#define host_and_af2sockaddr(host, port, af) host2sockaddr((host), (port))
#define xhost_and_af2sockaddr(host, port, af) xhost2sockaddr((host), (port))
#else
len_and_sockaddr* host_and_af2sockaddr(const char *host, int port, sa_family_t af) FAST_FUNC RETURNS_MALLOC;
len_and_sockaddr* xhost_and_af2sockaddr(const char *host, int port, sa_family_t af) FAST_FUNC RETURNS_MALLOC;
#endif
/* Assign sin[6]_port member if the socket is an AF_INET[6] one,
 * otherwise no-op. Useful for ftp.
 * NB: does NOT do htons() internally, just direct assignment. */
void set_nport(struct sockaddr *sa, unsigned port) FAST_FUNC;
/* Retrieve sin[6]_port or return -1 for non-INET[6] lsa's */
int get_nport(const struct sockaddr *sa) FAST_FUNC;
/* Reverse DNS. Returns NULL on failure. */
char* xmalloc_sockaddr2host(const struct sockaddr *sa) FAST_FUNC RETURNS_MALLOC;
/* This one doesn't append :PORTNUM */
char* xmalloc_sockaddr2host_noport(const struct sockaddr *sa) FAST_FUNC RETURNS_MALLOC;
/* This one also doesn't fall back to dotted IP (returns NULL) */
char* xmalloc_sockaddr2hostonly_noport(const struct sockaddr *sa) FAST_FUNC RETURNS_MALLOC;
/* inet_[ap]ton on steroids */
char* xmalloc_sockaddr2dotted(const struct sockaddr *sa) FAST_FUNC RETURNS_MALLOC;
char* xmalloc_sockaddr2dotted_noport(const struct sockaddr *sa) FAST_FUNC RETURNS_MALLOC;
// "old" (ipv4 only) API
// users: traceroute.c hostname.c - use _list_ of all IPs
struct hostent *xgethostbyname(const char *name) FAST_FUNC;
// Also mount.c and inetd.c are using gethostbyname(),
// + inet_common.c has additional IPv4-only stuff


#define TLS_MAX_MAC_SIZE 32
#define TLS_MAX_KEY_SIZE 32
struct tls_handshake_data; /* opaque */
typedef struct tls_state {
	int ofd;
	int ifd;

	unsigned min_encrypted_len_on_read;
	uint16_t cipher_id;
	uint8_t  encrypt_on_write;
	unsigned MAC_size;
	unsigned key_size;

	uint8_t *outbuf;
	int     outbuf_size;

	int     inbuf_size;
	int     ofs_to_buffered;
	int     buffered_size;
	uint8_t *inbuf;

	struct tls_handshake_data *hsd;

	// RFC 5246
	// sequence number
	//   Each connection state contains a sequence number, which is
	//   maintained separately for read and write states.  The sequence
	//   number MUST be set to zero whenever a connection state is made the
	//   active state.  Sequence numbers are of type uint64 and may not
	//   exceed 2^64-1.
	/*uint64_t read_seq64_be;*/
	uint64_t write_seq64_be;

	uint8_t *client_write_key;
	uint8_t *server_write_key;
	uint8_t client_write_MAC_key[TLS_MAX_MAC_SIZE];
	uint8_t server_write_MAC_k__[TLS_MAX_MAC_SIZE];
	uint8_t client_write_k__[TLS_MAX_KEY_SIZE];
	uint8_t server_write_k__[TLS_MAX_KEY_SIZE];
} tls_state_t;

static inline tls_state_t *new_tls_state(void)
{
	tls_state_t *tls = xzalloc(sizeof(*tls));
	return tls;
}
void tls_handshake(tls_state_t *tls, const char *sni) FAST_FUNC;
#define TLSLOOP_EXIT_ON_LOCAL_EOF (1 << 0)
void tls_run_copy_loop(tls_state_t *tls, unsigned flags) FAST_FUNC;


void socket_want_pktinfo(int fd) FAST_FUNC;
ssize_t send_to_from(int fd, void *buf, size_t len, int flags,
		const struct sockaddr *to,
		const struct sockaddr *from,
		socklen_t tolen) FAST_FUNC;
ssize_t recv_from_to(int fd, void *buf, size_t len, int flags,
		struct sockaddr *from,
		struct sockaddr *to,
		socklen_t sa_size) FAST_FUNC;

uint16_t inet_cksum(uint16_t *addr, int len) FAST_FUNC;
int parse_pasv_epsv(char *buf) FAST_FUNC;

/* 0 if argv[0] is NULL: */
unsigned string_array_len(char **argv) FAST_FUNC;
void overlapping_strcpy(char *dst, const char *src) FAST_FUNC;
char *safe_strncpy(char *dst, const char *src, size_t size) FAST_FUNC;
char *strncpy_IFNAMSIZ(char *dst, const char *src) FAST_FUNC;
unsigned count_strstr(const char *str, const char *sub) FAST_FUNC;
char *xmalloc_substitute_string(const char *src, int count, const char *sub, const char *repl) FAST_FUNC;
/* Guaranteed to NOT be a macro (smallest code). Saves nearly 2k on uclibc.
 * But potentially slow, don't use in one-billion-times loops */
int bb_putchar(int ch) FAST_FUNC;
/* Note: does not use stdio, writes to fd 2 directly */
int bb_putchar_stderr(char ch) FAST_FUNC;
char *xasprintf(const char *format, ...) __attribute__ ((format(printf, 1, 2))) FAST_FUNC RETURNS_MALLOC;
char *auto_string(char *str) FAST_FUNC;
// gcc-4.1.1 still isn't good enough at optimizing it
// (+200 bytes compared to macro)
//static ALWAYS_INLINE
//int LONE_DASH(const char *s) { return s[0] == '-' && !s[1]; }
//static ALWAYS_INLINE
//int NOT_LONE_DASH(const char *s) { return s[0] != '-' || s[1]; }
#define LONE_DASH(s)     ((s)[0] == '-' && !(s)[1])
#define NOT_LONE_DASH(s) ((s)[0] != '-' || (s)[1])
#define LONE_CHAR(s,c)     ((s)[0] == (c) && !(s)[1])
#define NOT_LONE_CHAR(s,c) ((s)[0] != (c) || (s)[1])
#define DOT_OR_DOTDOT(s) ((s)[0] == '.' && (!(s)[1] || ((s)[1] == '.' && !(s)[2])))

typedef struct uni_stat_t {
	unsigned byte_count;
	unsigned unicode_count;
	unsigned unicode_width;
} uni_stat_t;
/* Returns a string with unprintable chars replaced by '?' or
 * SUBST_WCHAR. This function is unicode-aware. */
const char* FAST_FUNC printable_string(uni_stat_t *stats, const char *str);
/* Prints unprintable char ch as ^C or M-c to file
 * (M-c is used only if ch is ORed with PRINTABLE_META),
 * else it is printed as-is (except for ch = 0x9b) */
enum { PRINTABLE_META = 0x100 };
void fputc_printable(int ch, FILE *file) FAST_FUNC;
/* Return a string that is the printable representation of character ch.
 * Buffer must hold at least four characters. */
enum {
	VISIBLE_ENDLINE   = 1 << 0,
	VISIBLE_SHOW_TABS = 1 << 1,
};
void visible(unsigned ch, char *buf, int flags) FAST_FUNC;

extern ssize_t safe_read(int fd, void *buf, size_t count) FAST_FUNC;
extern ssize_t nonblock_immune_read(int fd, void *buf, size_t count) FAST_FUNC;
// NB: will return short read on error, not -1,
// if some data was read before error occurred
extern ssize_t full_read(int fd, void *buf, size_t count) FAST_FUNC;
extern void xread(int fd, void *buf, size_t count) FAST_FUNC;
extern unsigned char xread_char(int fd) FAST_FUNC;
extern ssize_t read_close(int fd, void *buf, size_t maxsz) FAST_FUNC;
extern ssize_t open_read_close(const char *filename, void *buf, size_t maxsz) FAST_FUNC;
// Reads one line a-la fgets (but doesn't save terminating '\n').
// Reads byte-by-byte. Useful when it is important to not read ahead.
// Bytes are appended to pfx (which must be malloced, or NULL).
extern char *xmalloc_reads(int fd, size_t *maxsz_p) FAST_FUNC;
/* Reads block up to *maxsz_p (default: INT_MAX - 4095) */
extern void *xmalloc_read(int fd, size_t *maxsz_p) FAST_FUNC RETURNS_MALLOC;
/* Returns NULL if file can't be opened (default max size: INT_MAX - 4095) */
extern void *xmalloc_open_read_close(const char *filename, size_t *maxsz_p) FAST_FUNC RETURNS_MALLOC;
/* Never returns NULL */
extern void *xmalloc_xopen_read_close(const char *filename, size_t *maxsz_p) FAST_FUNC RETURNS_MALLOC;

#if defined(ARG_MAX) && (ARG_MAX >= 60*1024 || !defined(_SC_ARG_MAX))
/* Use _constant_ maximum if: defined && (big enough || no variable one exists) */
# define bb_arg_max() ((unsigned)ARG_MAX)
#elif defined(_SC_ARG_MAX)
/* Else use variable one (a bit more expensive) */
unsigned bb_arg_max(void) FAST_FUNC;
#else
/* If all else fails */
# define bb_arg_max() ((unsigned)(32 * 1024))
#endif
unsigned bb_clk_tck(void) FAST_FUNC;

#define SEAMLESS_COMPRESSION (0 \
 || ENABLE_FEATURE_SEAMLESS_XZ \
 || ENABLE_FEATURE_SEAMLESS_LZMA \
 || ENABLE_FEATURE_SEAMLESS_BZ2 \
 || ENABLE_FEATURE_SEAMLESS_GZ \
 || ENABLE_FEATURE_SEAMLESS_Z)

#if SEAMLESS_COMPRESSION
/* Autodetects gzip/bzip2 formats. fd may be in the middle of the file! */
int setup_unzip_on_fd(int fd, int fail_if_not_compressed) FAST_FUNC;
/* Autodetects .gz etc */
extern int open_zipped(const char *fname, int fail_if_not_compressed) FAST_FUNC;
extern void *xmalloc_open_zipped_read_close(const char *fname, size_t *maxsz_p) FAST_FUNC RETURNS_MALLOC;
#else
# define setup_unzip_on_fd(...) (0)
# define open_zipped(fname, fail_if_not_compressed)  open((fname), O_RDONLY);
# define xmalloc_open_zipped_read_close(fname, maxsz_p) xmalloc_open_read_close((fname), (maxsz_p))
#endif
/* lzma has no signature, need a little helper. NB: exist only for ENABLE_FEATURE_SEAMLESS_LZMA=y */
void setup_lzma_on_fd(int fd) FAST_FUNC;

extern ssize_t safe_write(int fd, const void *buf, size_t count) FAST_FUNC;
// NB: will return short write on error, not -1,
// if some data was written before error occurred
extern ssize_t full_write(int fd, const void *buf, size_t count) FAST_FUNC;
extern void xwrite(int fd, const void *buf, size_t count) FAST_FUNC;
extern void xwrite_str(int fd, const char *str) FAST_FUNC;
extern ssize_t full_write1_str(const char *str) FAST_FUNC;
extern ssize_t full_write2_str(const char *str) FAST_FUNC;
extern void xopen_xwrite_close(const char* file, const char *str) FAST_FUNC;

/* Close fd, but check for failures (some types of write errors) */
extern void xclose(int fd) FAST_FUNC;

/* Reads and prints to stdout till eof, then closes FILE. Exits on error: */
extern void xprint_and_close_file(FILE *file) FAST_FUNC;

/* Reads a line from a text file, up to a newline or NUL byte, inclusive.
 * Returns malloc'ed char*. If end is NULL '\n' isn't considered
 * end of line. If end isn't NULL, length of the chunk is stored in it.
 * Returns NULL if EOF/error.
 */
extern char *bb_get_chunk_from_file(FILE *file, size_t *end) FAST_FUNC;
/* Reads up to (and including) TERMINATING_STRING: */
extern char *xmalloc_fgets_str(FILE *file, const char *terminating_string) FAST_FUNC RETURNS_MALLOC;
/* Same, with limited max size, and returns the length (excluding NUL): */
extern char *xmalloc_fgets_str_len(FILE *file, const char *terminating_string, size_t *maxsz_p) FAST_FUNC RETURNS_MALLOC;
/* Chops off TERMINATING_STRING from the end: */
extern char *xmalloc_fgetline_str(FILE *file, const char *terminating_string) FAST_FUNC RETURNS_MALLOC;
/* Reads up to (and including) "\n" or NUL byte: */
extern char *xmalloc_fgets(FILE *file) FAST_FUNC RETURNS_MALLOC;
/* Chops off '\n' from the end, unlike fgets: */
extern char *xmalloc_fgetline(FILE *file) FAST_FUNC RETURNS_MALLOC;
/* Same, but doesn't try to conserve space (may have some slack after the end) */
/* extern char *xmalloc_fgetline_fast(FILE *file) FAST_FUNC RETURNS_MALLOC; */

void die_if_ferror(FILE *file, const char *msg) FAST_FUNC;
void die_if_ferror_stdout(void) FAST_FUNC;
int fflush_all(void) FAST_FUNC;
void fflush_stdout_and_exit(int retval) NORETURN FAST_FUNC;
int fclose_if_not_stdin(FILE *file) FAST_FUNC;
FILE* xfopen(const char *filename, const char *mode) FAST_FUNC;
/* Prints warning to stderr and returns NULL on failure: */
FILE* fopen_or_warn(const char *filename, const char *mode) FAST_FUNC;
/* "Opens" stdin if filename is special, else just opens file: */
FILE* xfopen_stdin(const char *filename) FAST_FUNC;
FILE* fopen_or_warn_stdin(const char *filename) FAST_FUNC;
FILE* fopen_for_read(const char *path) FAST_FUNC;
FILE* xfopen_for_read(const char *path) FAST_FUNC;
FILE* fopen_for_write(const char *path) FAST_FUNC;
FILE* xfopen_for_write(const char *path) FAST_FUNC;
FILE* xfdopen_for_read(int fd) FAST_FUNC;
FILE* xfdopen_for_write(int fd) FAST_FUNC;

int bb_pstrcmp(const void *a, const void *b) /* not FAST_FUNC! */;
void qsort_string_vector(char **sv, unsigned count) FAST_FUNC;

/* Wrapper which restarts poll on EINTR or ENOMEM.
 * On other errors complains [perror("poll")] and returns.
 * Warning! May take (much) longer than timeout_ms to return!
 * If this is a problem, use bare poll and open-code EINTR/ENOMEM handling */
int safe_poll(struct pollfd *ufds, nfds_t nfds, int timeout_ms) FAST_FUNC;

char *safe_gethostname(void) FAST_FUNC;

/* Convert each alpha char in str to lower-case */
char* str_tolower(char *str) FAST_FUNC;

char *utoa(unsigned n) FAST_FUNC;
char *itoa(int n) FAST_FUNC;
/* Returns a pointer past the formatted number, does NOT null-terminate */
char *utoa_to_buf(unsigned n, char *buf, unsigned buflen) FAST_FUNC;
char *itoa_to_buf(int n, char *buf, unsigned buflen) FAST_FUNC;
/* Intelligent formatters of bignums */
char *smart_ulltoa4(unsigned long long ul, char buf[4], const char *scale) FAST_FUNC;
char *smart_ulltoa5(unsigned long long ul, char buf[5], const char *scale) FAST_FUNC;
/* If block_size == 0, display size without fractional part,
 * else display (size * block_size) with one decimal digit.
 * If display_unit == 0, show value no bigger than 1024 with suffix (K,M,G...),
 * else divide by display_unit and do not use suffix. */
#define HUMAN_READABLE_MAX_WIDTH      7  /* "1024.0G" */
#define HUMAN_READABLE_MAX_WIDTH_STR "7"
//TODO: provide pointer to buf (avoid statics)?
const char *make_human_readable_str(unsigned long long size,
		unsigned long block_size, unsigned long display_unit) FAST_FUNC;
/* Put a string of hex bytes ("1b2e66fe"...), return advanced pointer */
char *bin2hex(char *dst, const char *src, int count) FAST_FUNC;
/* Reverse */
char* hex2bin(char *dst, const char *src, int count) FAST_FUNC;

/* Generate a UUID */
void generate_uuid(uint8_t *buf) FAST_FUNC;

/* Last element is marked by mult == 0 */
struct suffix_mult {
	char suffix[4];
	unsigned mult;
};
extern const struct suffix_mult bkm_suffixes[];
#define km_suffixes (bkm_suffixes + 1)
extern const struct suffix_mult cwbkMG_suffixes[];
#define kMG_suffixes (cwbkMG_suffixes + 3)
extern const struct suffix_mult kmg_i_suffixes[];

#include "xatonum.h"
/* Specialized: */

/* Using xatoi() instead of naive atoi() is not always convenient -
 * in many places people want *non-negative* values, but store them
 * in signed int. Therefore we need this one:
 * dies if input is not in [0, INT_MAX] range. Also will reject '-0' etc.
 * It should really be named xatoi_nonnegative (since it allows 0),
 * but that would be too long.
 */
int xatoi_positive(const char *numstr) FAST_FUNC;

/* Useful for reading port numbers */
uint16_t xatou16(const char *numstr) FAST_FUNC;


/* These parse entries in /etc/passwd and /etc/group.  This is desirable
 * for BusyBox since we want to avoid using the glibc NSS stuff, which
 * increases target size and is often not needed on embedded systems.  */
long xuname2uid(const char *name) FAST_FUNC;
long xgroup2gid(const char *name) FAST_FUNC;
/* wrapper: allows string to contain numeric uid or gid */
unsigned long get_ug_id(const char *s, long FAST_FUNC (*xname2id)(const char *)) FAST_FUNC;
struct bb_uidgid_t {
	uid_t uid;
	gid_t gid;
};
/* always sets uid and gid; returns 0 on failure */
int get_uidgid(struct bb_uidgid_t*, const char*) FAST_FUNC;
/* always sets uid and gid; exits on failure */
void xget_uidgid(struct bb_uidgid_t*, const char*) FAST_FUNC;
/* chown-like handling of "user[:[group]" */
void parse_chown_usergroup_or_die(struct bb_uidgid_t *u, char *user_group) FAST_FUNC;
struct passwd* xgetpwnam(const char *name) FAST_FUNC;
struct group* xgetgrnam(const char *name) FAST_FUNC;
struct passwd* xgetpwuid(uid_t uid) FAST_FUNC;
struct group* xgetgrgid(gid_t gid) FAST_FUNC;
char* xuid2uname(uid_t uid) FAST_FUNC;
char* xgid2group(gid_t gid) FAST_FUNC;
char* uid2uname(uid_t uid) FAST_FUNC;
char* gid2group(gid_t gid) FAST_FUNC;
char* uid2uname_utoa(uid_t uid) FAST_FUNC;
char* gid2group_utoa(gid_t gid) FAST_FUNC;
/* versions which cache results (useful for ps, ls etc) */
const char* get_cached_username(uid_t uid) FAST_FUNC;
const char* get_cached_groupname(gid_t gid) FAST_FUNC;
void clear_username_cache(void) FAST_FUNC;
/* internally usernames are saved in fixed-sized char[] buffers */
enum { USERNAME_MAX_SIZE = 32 - sizeof(uid_t) };
#if ENABLE_FEATURE_CHECK_NAMES
void die_if_bad_username(const char* name) FAST_FUNC;
#else
#define die_if_bad_username(name) ((void)(name))
#endif
/*
 * Returns (-1) terminated malloced result of getgroups().
 * Reallocs group_array (useful for repeated calls).
 * ngroups is an initial size of array. It is rounded up to 32 for realloc.
 * ngroups is updated on return.
 * ngroups can be NULL: bb_getgroups(NULL, NULL) is valid usage.
 * Dies on errors (on Linux, only xrealloc can cause this, not internal getgroups call).
 */
gid_t *bb_getgroups(int *ngroups, gid_t *group_array) FAST_FUNC;

#if ENABLE_FEATURE_UTMP
void FAST_FUNC write_new_utmp(pid_t pid, int new_type, const char *tty_name, const char *username, const char *hostname);
void FAST_FUNC update_utmp(pid_t pid, int new_type, const char *tty_name, const char *username, const char *hostname);
void FAST_FUNC update_utmp_DEAD_PROCESS(pid_t pid);
#else
# define write_new_utmp(pid, new_type, tty_name, username, hostname) ((void)0)
# define update_utmp(pid, new_type, tty_name, username, hostname) ((void)0)
# define update_utmp_DEAD_PROCESS(pid) ((void)0)
#endif


int file_is_executable(const char *name) FAST_FUNC;
char *find_executable(const char *filename, char **PATHp) FAST_FUNC;
int executable_exists(const char *filename) FAST_FUNC;

/* BB_EXECxx always execs (it's not doing NOFORK/NOEXEC stuff),
 * but it may exec busybox and call applet instead of searching PATH.
 */
#if ENABLE_FEATURE_PREFER_APPLETS
int BB_EXECVP(const char *file, char *const argv[]) FAST_FUNC;
#define BB_EXECLP(prog,cmd,...) \
	do { \
		if (find_applet_by_name(prog) >= 0) \
			execlp(bb_busybox_exec_path, cmd, __VA_ARGS__); \
		execlp(prog, cmd, __VA_ARGS__); \
	} while (0)
#else
#define BB_EXECVP(prog,cmd)     execvp(prog,cmd)
#define BB_EXECLP(prog,cmd,...) execlp(prog,cmd,__VA_ARGS__)
#endif
void BB_EXECVP_or_die(char **argv) NORETURN FAST_FUNC;
void exec_prog_or_SHELL(char **argv) NORETURN FAST_FUNC;

/* xvfork() can't be a _function_, return after vfork in child mangles stack
 * in the parent. It must be a macro. */
#define xvfork() \
({ \
	pid_t bb__xvfork_pid = vfork(); \
	if (bb__xvfork_pid < 0) \
		bb_perror_msg_and_die("vfork"); \
	bb__xvfork_pid; \
})
#if BB_MMU
pid_t xfork(void) FAST_FUNC;
#endif
void xvfork_parent_waits_and_exits(void) FAST_FUNC;

/* NOMMU friendy fork+exec: */
pid_t spawn(char **argv) FAST_FUNC;
pid_t xspawn(char **argv) FAST_FUNC;

pid_t safe_waitpid(pid_t pid, int *wstat, int options) FAST_FUNC;
pid_t wait_any_nohang(int *wstat) FAST_FUNC;
/* wait4pid: unlike waitpid, waits ONLY for one process.
 * Returns sig + 0x180 if child is killed by signal.
 * It's safe to pass negative 'pids' from failed [v]fork -
 * wait4pid will return -1 (and will not clobber [v]fork's errno).
 * IOW: rc = wait4pid(spawn(argv));
 *      if (rc < 0) bb_perror_msg("%s", argv[0]);
 *      if (rc > 0) bb_error_msg("exit code: %d", rc & 0xff);
 */
int wait4pid(pid_t pid) FAST_FUNC;
int wait_for_exitstatus(pid_t pid) FAST_FUNC;
/************************************************************************/
/* spawn_and_wait/run_nofork_applet/run_applet_no_and_exit need to work */
/* carefully together to reinit some global state while not disturbing  */
/* other. Be careful if you change them. Consult docs/nofork_noexec.txt */
/************************************************************************/
/* Same as wait4pid(spawn(argv)), but with NOFORK/NOEXEC if configured: */
int spawn_and_wait(char **argv) FAST_FUNC;
/* Does NOT check that applet is NOFORK, just blindly runs it */
int run_nofork_applet(int applet_no, char **argv) FAST_FUNC;
void run_noexec_applet_and_exit(int a, const char *name, char **argv) NORETURN FAST_FUNC;
#ifndef BUILD_INDIVIDUAL
int find_applet_by_name(const char *name) FAST_FUNC;
void run_applet_no_and_exit(int a, const char *name, char **argv) NORETURN FAST_FUNC;
#endif
#if defined(__linux__)
void set_task_comm(const char *comm) FAST_FUNC;
#else
# define set_task_comm(name) ((void)0)
#endif

/* Helpers for daemonization.
 *
 * bb_daemonize(flags) = daemonize, does not compile on NOMMU
 *
 * bb_daemonize_or_rexec(flags, argv) = daemonizes on MMU (and ignores argv),
 *      rexec's itself on NOMMU with argv passed as command line.
 * Thus bb_daemonize_or_rexec may cause your <applet>_main() to be re-executed
 * from the start. (It will detect it and not reexec again second time).
 * You have to audit carefully that you don't do something twice as a result
 * (opening files/sockets, parsing config files etc...)!
 *
 * Both of the above will redirect fd 0,1,2 to /dev/null and drop ctty
 * (will do setsid()).
 *
 * fork_or_rexec(argv) = bare-bones fork on MMU,
 *      "vfork + re-exec ourself" on NOMMU. No fd redirection, no setsid().
 *      On MMU ignores argv.
 *
 * Helper for network daemons in foreground mode:
 *
 * bb_sanitize_stdio() = make sure that fd 0,1,2 are opened by opening them
 * to /dev/null if they are not.
 */
enum {
	DAEMON_CHDIR_ROOT = 1,
	DAEMON_DEVNULL_STDIO = 2,
	DAEMON_CLOSE_EXTRA_FDS = 4,
	DAEMON_ONLY_SANITIZE = 8, /* internal use */
	DAEMON_DOUBLE_FORK = 16, /* double fork to avoid controlling tty */
};
#if BB_MMU
  enum { re_execed = 0 };
# define fork_or_rexec(argv)                xfork()
# define bb_daemonize_or_rexec(flags, argv) bb_daemonize_or_rexec(flags)
# define bb_daemonize(flags)                bb_daemonize_or_rexec(flags, bogus)
#else
  extern bool re_execed;
  /* Note: re_exec() and fork_or_rexec() do argv[0][0] |= 0x80 on NOMMU!
   * _Parent_ needs to undo it if it doesn't want to have argv[0] mangled.
   */
  void re_exec(char **argv) NORETURN FAST_FUNC;
  pid_t fork_or_rexec(char **argv) FAST_FUNC;
  int  BUG_fork_is_unavailable_on_nommu(void) FAST_FUNC;
  int  BUG_daemon_is_unavailable_on_nommu(void) FAST_FUNC;
  void BUG_bb_daemonize_is_unavailable_on_nommu(void) FAST_FUNC;
# define fork()          BUG_fork_is_unavailable_on_nommu()
# define xfork()         BUG_fork_is_unavailable_on_nommu()
# define daemon(a,b)     BUG_daemon_is_unavailable_on_nommu()
# define bb_daemonize(a) BUG_bb_daemonize_is_unavailable_on_nommu()
#endif
void bb_daemonize_or_rexec(int flags, char **argv) FAST_FUNC;
void bb_sanitize_stdio(void) FAST_FUNC;
#define bb_daemon_helper(arg) bb_daemonize_or_rexec((arg) | DAEMON_ONLY_SANITIZE, NULL)
/* Clear dangerous stuff, set PATH. Return 1 if was run by different user. */
int sanitize_env_if_suid(void) FAST_FUNC;


/* For top, ps. Some argv[i] are replaced by malloced "-opt" strings */
void make_all_argv_opts(char **argv) FAST_FUNC;
char* single_argv(char **argv) FAST_FUNC;
extern const char *const bb_argv_dash[]; /* { "-", NULL } */
extern uint32_t option_mask32;
uint32_t getopt32(char **argv, const char *applet_opts, ...) FAST_FUNC;
# define No_argument "\0"
# define Required_argument "\001"
# define Optional_argument "\002"
#if ENABLE_LONG_OPTS
uint32_t getopt32long(char **argv, const char *optstring, const char *longopts, ...) FAST_FUNC;
#else
#define getopt32long(argv,optstring,longopts,...) \
	getopt32(argv,optstring,##__VA_ARGS__)
#endif
/* BSD-derived getopt() functions require that optind be set to 1 in
 * order to reset getopt() state.  This used to be generally accepted
 * way of resetting getopt().  However, glibc's getopt()
 * has additional getopt() state beyond optind (specifically, glibc
 * extensions such as '+' and '-' at the start of the string), and requires
 * that optind be set to zero to reset its state.  BSD-derived versions
 * of getopt() misbehaved if optind is set to 0 in order to reset getopt(),
 * and glibc's getopt() used to coredump if optind is set 1 in order
 * to reset getopt().
 * Then BSD introduced additional variable "optreset" which should be
 * set to 1 in order to reset getopt().  Sigh.  Standards, anyone?
 *
 * By ~2008, OpenBSD 3.4 was changed to survive glibc-like optind = 0
 * (to interpret it as if optreset was set).
 */
#if 1 /*def __GLIBC__*/
#define GETOPT_RESET() (optind = 0)
#else /* BSD style */
#define GETOPT_RESET() (optind = 1)
#endif


/* Having next pointer as a first member allows easy creation
 * of "llist-compatible" structs, and using llist_FOO functions
 * on them.
 */
typedef struct llist_t {
	struct llist_t *link;
	char *data;
} llist_t;
void llist_add_to(llist_t **old_head, void *data) FAST_FUNC;
void llist_add_to_end(llist_t **list_head, void *data) FAST_FUNC;
void *llist_pop(llist_t **elm) FAST_FUNC;
void llist_unlink(llist_t **head, llist_t *elm) FAST_FUNC;
void llist_free(llist_t *elm, void (*freeit)(void *data)) FAST_FUNC;
llist_t *llist_rev(llist_t *list) FAST_FUNC;
llist_t *llist_find_str(llist_t *first, const char *str) FAST_FUNC;
/* BTW, surprisingly, changing API to
 *   llist_t *llist_add_to(llist_t *old_head, void *data)
 * etc does not result in smaller code... */

/* start_stop_daemon and udhcpc are special - they want
 * to create pidfiles regardless of FEATURE_PIDFILE */
#if ENABLE_FEATURE_PIDFILE || defined(WANT_PIDFILE)
/* True only if we created pidfile which is *file*, not /dev/null etc */
extern smallint wrote_pidfile;
void write_pidfile(const char *path) FAST_FUNC;
#define remove_pidfile(path) do { if (wrote_pidfile) unlink(path); } while (0)
#else
enum { wrote_pidfile = 0 };
#define write_pidfile(path)  ((void)0)
#define remove_pidfile(path) ((void)0)
#endif

enum {
	LOGMODE_NONE = 0,
	LOGMODE_STDIO = (1 << 0),
	LOGMODE_SYSLOG = (1 << 1) * ENABLE_FEATURE_SYSLOG,
	LOGMODE_BOTH = LOGMODE_SYSLOG + LOGMODE_STDIO,
};
extern const char *msg_eol;
extern smallint syslog_level;
extern smallint logmode;
extern uint8_t xfunc_error_retval;
extern void (*die_func)(void);
void xfunc_die(void) NORETURN FAST_FUNC;
void bb_show_usage(void) NORETURN FAST_FUNC;
void bb_error_msg(const char *s, ...) __attribute__ ((format (printf, 1, 2))) FAST_FUNC;
void bb_error_msg_and_die(const char *s, ...) __attribute__ ((noreturn, format (printf, 1, 2))) FAST_FUNC;
void bb_perror_msg(const char *s, ...) __attribute__ ((format (printf, 1, 2))) FAST_FUNC;
void bb_simple_perror_msg(const char *s) FAST_FUNC;
void bb_perror_msg_and_die(const char *s, ...) __attribute__ ((noreturn, format (printf, 1, 2))) FAST_FUNC;
void bb_simple_perror_msg_and_die(const char *s) NORETURN FAST_FUNC;
void bb_herror_msg(const char *s, ...) __attribute__ ((format (printf, 1, 2))) FAST_FUNC;
void bb_herror_msg_and_die(const char *s, ...) __attribute__ ((noreturn, format (printf, 1, 2))) FAST_FUNC;
void bb_perror_nomsg_and_die(void) NORETURN FAST_FUNC;
void bb_perror_nomsg(void) FAST_FUNC;
void bb_verror_msg(const char *s, va_list p, const char *strerr) FAST_FUNC;
void bb_die_memory_exhausted(void) NORETURN FAST_FUNC;
void bb_logenv_override(void) FAST_FUNC;

/* We need to export XXX_main from libbusybox
 * only if we build "individual" binaries
 */
#if ENABLE_FEATURE_INDIVIDUAL
#define MAIN_EXTERNALLY_VISIBLE EXTERNALLY_VISIBLE
#else
#define MAIN_EXTERNALLY_VISIBLE
#endif


/* Applets which are useful from another applets */
int bb_cat(char** argv) FAST_FUNC;
/* If shell needs them, they exist even if not enabled as applets */
int echo_main(int argc, char** argv) IF_ECHO(MAIN_EXTERNALLY_VISIBLE);
int printf_main(int argc, char **argv) IF_PRINTF(MAIN_EXTERNALLY_VISIBLE);
int test_main(int argc, char **argv)
#if ENABLE_TEST || ENABLE_TEST1 || ENABLE_TEST2
		MAIN_EXTERNALLY_VISIBLE
#endif
;
int kill_main(int argc, char **argv)
#if ENABLE_KILL || ENABLE_KILLALL || ENABLE_KILLALL5
		MAIN_EXTERNALLY_VISIBLE
#endif
;
/* Similar, but used by chgrp, not shell */
int chown_main(int argc, char **argv) IF_CHOWN(MAIN_EXTERNALLY_VISIBLE);
/* Used by ftpd */
int ls_main(int argc, char **argv) IF_LS(MAIN_EXTERNALLY_VISIBLE);
/* Don't need IF_xxx() guard for these */
int gunzip_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int bunzip2_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;

#if ENABLE_ROUTE
void bb_displayroutes(int noresolve, int netstatfmt) FAST_FUNC;
#endif

struct number_state {
	unsigned width;
	unsigned start;
	unsigned inc;
	const char *sep;
	const char *empty_str;
	smallint all, nonempty;
};
void print_numbered_lines(struct number_state *ns, const char *filename) FAST_FUNC;


/* Networking */
/* This structure defines protocol families and their handlers. */
struct aftype {
	const char *name;
	const char *title;
	int af;
	int alen;
	char*       FAST_FUNC (*print)(unsigned char *);
	const char* FAST_FUNC (*sprint)(struct sockaddr *, int numeric);
	int         FAST_FUNC (*input)(/*int type,*/ const char *bufp, struct sockaddr *);
	void        FAST_FUNC (*herror)(char *text);
	int         FAST_FUNC (*rprint)(int options);
	int         FAST_FUNC (*rinput)(int typ, int ext, char **argv);
	/* may modify src */
	int         FAST_FUNC (*getmask)(char *src, struct sockaddr *mask, char *name);
};
/* This structure defines hardware protocols and their handlers. */
struct hwtype {
	const char *name;
	const char *title;
	int type;
	int alen;
	char* FAST_FUNC (*print)(unsigned char *);
	int   FAST_FUNC (*input)(const char *, struct sockaddr *);
	int   FAST_FUNC (*activate)(int fd);
	int suppress_null_addr;
};
#define IFNAME_SHOW_DOWNED_TOO ((char*)(intptr_t)1)
int display_interfaces(char *ifname) FAST_FUNC;
int in_ether(const char *bufp, struct sockaddr *sap) FAST_FUNC;
#if ENABLE_FEATURE_HWIB
int in_ib(const char *bufp, struct sockaddr *sap) FAST_FUNC;
#else
#define in_ib(a, b) 1 /* fail */
#endif
const struct aftype *get_aftype(const char *name) FAST_FUNC;
const struct hwtype *get_hwtype(const char *name) FAST_FUNC;
const struct hwtype *get_hwntype(int type) FAST_FUNC;


extern int fstype_matches(const char *fstype, const char *comma_list) FAST_FUNC;
#ifdef HAVE_MNTENT_H
extern struct mntent *find_mount_point(const char *name, int subdir_too) FAST_FUNC;
#endif
extern void erase_mtab(const char * name) FAST_FUNC;
extern unsigned int tty_baud_to_value(speed_t speed) FAST_FUNC;
extern speed_t tty_value_to_baud(unsigned int value) FAST_FUNC;
#if ENABLE_DESKTOP
extern void bb_warn_ignoring_args(char *arg) FAST_FUNC;
#else
# define bb_warn_ignoring_args(arg) ((void)0)
#endif

extern int get_linux_version_code(void) FAST_FUNC;

extern char *query_loop(const char *device) FAST_FUNC;
extern int del_loop(const char *device) FAST_FUNC;
/*
 * If *devname is not NULL, use that name, otherwise try to find free one,
 * malloc and return it in *devname.
 * return value is the opened fd to the loop device, or < on error
 */
extern int set_loop(char **devname, const char *file, unsigned long long offset, unsigned flags) FAST_FUNC;
/* These constants match linux/loop.h (without BB_ prefix): */
#define BB_LO_FLAGS_READ_ONLY 1
#define BB_LO_FLAGS_AUTOCLEAR 4

/* Returns malloced str */
char *bb_ask_noecho(int fd, int timeout, const char *prompt) FAST_FUNC;
/* Like bb_ask_noecho, but asks on stdin with no timeout.  */
char *bb_ask_noecho_stdin(const char *prompt) FAST_FUNC;

int bb_ask_y_confirmation_FILE(FILE *fp) FAST_FUNC;
int bb_ask_y_confirmation(void) FAST_FUNC;

/* Returns -1 if input is invalid. current_mode is a base for e.g. "u+rw" */
int bb_parse_mode(const char* s, unsigned cur_mode) FAST_FUNC;

/*
 * Config file parser
 */
enum {
	PARSE_COLLAPSE  = 0x00010000, // treat consecutive delimiters as one
	PARSE_TRIM      = 0x00020000, // trim leading and trailing delimiters
// TODO: COLLAPSE and TRIM seem to always go in pair
	PARSE_GREEDY    = 0x00040000, // last token takes entire remainder of the line
	PARSE_MIN_DIE   = 0x00100000, // die if < min tokens found
	// keep a copy of current line
	PARSE_KEEP_COPY = 0x00200000 * ENABLE_FEATURE_CROND_D,
	PARSE_EOL_COMMENTS = 0x00400000, // comments are recognized even if they aren't the first char
	PARSE_ALT_COMMENTS = 0x00800000, // delim[0] and delim[1] are two different allowed comment chars
	// (so far, delim[0] will only work as comment char for full-line comment)
	// (IOW: it works as if PARSE_EOL_COMMENTS is not set. sysctl applet is okay with this)
	PARSE_WS_COMMENTS  = 0x01000000, // comments are recognized even if there is whitespace before
	// ("line start><space><tab><space>#comment" is also comment, not only "line start>#comment")
	// NORMAL is:
	// * remove leading and trailing delimiters and collapse
	//   multiple delimiters into one
	// * warn and continue if less than mintokens delimiters found
	// * grab everything into last token
	// * comments are recognized even if they aren't the first char
	PARSE_NORMAL    = PARSE_COLLAPSE | PARSE_TRIM | PARSE_GREEDY | PARSE_EOL_COMMENTS,
};
typedef struct parser_t {
	FILE *fp;
	char *data;
	char *line, *nline;
	size_t line_alloc, nline_alloc;
	int lineno;
} parser_t;
parser_t* config_open(const char *filename) FAST_FUNC;
parser_t* config_open2(const char *filename, FILE* FAST_FUNC (*fopen_func)(const char *path)) FAST_FUNC;
/* delims[0] is a comment char (use '\0' to disable), the rest are token delimiters */
int config_read(parser_t *parser, char **tokens, unsigned flags, const char *delims) FAST_FUNC;
#define config_read(parser, tokens, max, min, str, flags) \
	config_read(parser, tokens, ((flags) | (((min) & 0xFF) << 8) | ((max) & 0xFF)), str)
void config_close(parser_t *parser) FAST_FUNC;

/* Concatenate path and filename to new allocated buffer.
 * Add "/" only as needed (no duplicate "//" are produced).
 * If path is NULL, it is assumed to be "/".
 * filename should not be NULL. */
char *concat_path_file(const char *path, const char *filename) FAST_FUNC;
/* Returns NULL on . and .. */
char *concat_subpath_file(const char *path, const char *filename) FAST_FUNC;


int bb_make_directory(char *path, long mode, int flags) FAST_FUNC;

int get_signum(const char *name) FAST_FUNC;
const char *get_signame(int number) FAST_FUNC;
void print_signames(void) FAST_FUNC;

char *bb_simplify_path(const char *path) FAST_FUNC;
/* Returns ptr to NUL */
char *bb_simplify_abs_path_inplace(char *path) FAST_FUNC;

#ifndef LOGIN_FAIL_DELAY
#define LOGIN_FAIL_DELAY 3
#endif
extern void bb_do_delay(int seconds) FAST_FUNC;
extern void change_identity(const struct passwd *pw) FAST_FUNC;
extern void run_shell(const char *shell, int loginshell, const char **args) NORETURN FAST_FUNC;

/* Returns $SHELL, getpwuid(getuid())->pw_shell, or DEFAULT_SHELL.
 * Note that getpwuid result might need xstrdup'ing
 * if there is a possibility of intervening getpwxxx() calls.
 */
const char *get_shell_name(void) FAST_FUNC;

#if ENABLE_FEATURE_SETPRIV_CAPABILITIES || ENABLE_RUN_INIT
unsigned cap_name_to_number(const char *cap) FAST_FUNC;
void printf_cap(const char *pfx, unsigned cap_no) FAST_FUNC;
void drop_capability(int cap_ordinal) FAST_FUNC;
/* Structures inside "struct caps" are Linux-specific and libcap-specific: */
#define DEFINE_STRUCT_CAPS \
struct caps { \
	struct __user_cap_header_struct header; \
	unsigned u32s; \
	struct __user_cap_data_struct data[2]; \
}
void getcaps(void *caps) FAST_FUNC;
#endif

#if ENABLE_SELINUX
extern void renew_current_security_context(void) FAST_FUNC;
extern void set_current_security_context(security_context_t sid) FAST_FUNC;
extern context_t set_security_context_component(security_context_t cur_context,
						char *user, char *role, char *type, char *range) FAST_FUNC;
extern void setfscreatecon_or_die(security_context_t scontext) FAST_FUNC;
extern void selinux_preserve_fcontext(int fdesc) FAST_FUNC;
#else
#define selinux_preserve_fcontext(fdesc) ((void)0)
#endif
extern void selinux_or_die(void) FAST_FUNC;


/* setup_environment:
 * if chdir pw->pw_dir: ok: else if to_tmp == 1: goto /tmp else: goto / or die
 * if clear_env = 1: cd(pw->pw_dir), clear environment, then set
 *   TERM=(old value)
 *   USER=pw->pw_name, LOGNAME=pw->pw_name
 *   PATH=bb_default_[root_]path
 *   HOME=pw->pw_dir
 *   SHELL=shell
 * else if change_env = 1:
 *   if not root (if pw->pw_uid != 0):
 *     USER=pw->pw_name, LOGNAME=pw->pw_name
 *   HOME=pw->pw_dir
 *   SHELL=shell
 * else does nothing
 *
 * NB: CHANGEENV and CLEARENV use setenv() - this leaks memory!
 * If setup_environment() is used is vforked child, this leaks memory _in parent too_!
 */
#define SETUP_ENV_CHANGEENV (1 << 0)
#define SETUP_ENV_CLEARENV  (1 << 1)
#define SETUP_ENV_TO_TMP    (1 << 2)
#define SETUP_ENV_NO_CHDIR  (1 << 4)
void setup_environment(const char *shell, int flags, const struct passwd *pw) FAST_FUNC;
void nuke_str(char *str) FAST_FUNC;
#if ENABLE_FEATURE_SECURETTY && !ENABLE_PAM
int is_tty_secure(const char *short_tty) FAST_FUNC;
#else
static ALWAYS_INLINE int is_tty_secure(const char *short_tty UNUSED_PARAM) { return 1; }
#endif
#define CHECKPASS_PW_HAS_EMPTY_PASSWORD 2
int check_password(const struct passwd *pw, const char *plaintext) FAST_FUNC;
int ask_and_check_password_extended(const struct passwd *pw, int timeout, const char *prompt) FAST_FUNC;
int ask_and_check_password(const struct passwd *pw) FAST_FUNC;
/* Returns a malloced string */
#if !ENABLE_USE_BB_CRYPT
#define pw_encrypt(clear, salt, cleanup) pw_encrypt(clear, salt)
#endif
extern char *pw_encrypt(const char *clear, const char *salt, int cleanup) FAST_FUNC;
extern int obscure(const char *old, const char *newval, const struct passwd *pwdp) FAST_FUNC;
/*
 * rnd is additional random input. New one is returned.
 * Useful if you call crypt_make_salt many times in a row:
 * rnd = crypt_make_salt(buf1, 4, 0);
 * rnd = crypt_make_salt(buf2, 4, rnd);
 * rnd = crypt_make_salt(buf3, 4, rnd);
 * (otherwise we risk having same salt generated)
 */
extern int crypt_make_salt(char *p, int cnt /*, int rnd*/) FAST_FUNC;
/* "$N$" + sha_salt_16_bytes + NUL */
#define MAX_PW_SALT_LEN (3 + 16 + 1)
extern char* crypt_make_pw_salt(char p[MAX_PW_SALT_LEN], const char *algo) FAST_FUNC;


/* Returns number of lines changed, or -1 on error */
#if !(ENABLE_FEATURE_ADDUSER_TO_GROUP || ENABLE_FEATURE_DEL_USER_FROM_GROUP)
#define update_passwd(filename, username, data, member) \
	update_passwd(filename, username, data)
#endif
extern int update_passwd(const char *filename,
		const char *username,
		const char *data,
		const char *member) FAST_FUNC;

int index_in_str_array(const char *const string_array[], const char *key) FAST_FUNC;
int index_in_strings(const char *strings, const char *key) FAST_FUNC;
int index_in_substr_array(const char *const string_array[], const char *key) FAST_FUNC;
int index_in_substrings(const char *strings, const char *key) FAST_FUNC;
const char *nth_string(const char *strings, int n) FAST_FUNC;

extern void print_login_issue(const char *issue_file, const char *tty) FAST_FUNC;
extern void print_login_prompt(void) FAST_FUNC;

char *xmalloc_ttyname(int fd) FAST_FUNC RETURNS_MALLOC;
/* NB: typically you want to pass fd 0, not 1. Think 'applet | grep something' */
int get_terminal_width_height(int fd, unsigned *width, unsigned *height) FAST_FUNC;
int get_terminal_width(int fd) FAST_FUNC;

int tcsetattr_stdin_TCSANOW(const struct termios *tp) FAST_FUNC;
#define TERMIOS_CLEAR_ISIG      (1 << 0)
#define TERMIOS_RAW_CRNL_INPUT  (1 << 1)
#define TERMIOS_RAW_CRNL_OUTPUT (1 << 2)
#define TERMIOS_RAW_CRNL        (TERMIOS_RAW_CRNL_INPUT|TERMIOS_RAW_CRNL_OUTPUT)
#define TERMIOS_RAW_INPUT       (1 << 3)
int get_termios_and_make_raw(int fd, struct termios *newterm, struct termios *oldterm, int flags) FAST_FUNC;
int set_termios_to_raw(int fd, struct termios *oldterm, int flags) FAST_FUNC;

/* NB: "unsigned request" is crucial! "int request" will break some arches! */
int ioctl_or_perror(int fd, unsigned request, void *argp, const char *fmt,...) __attribute__ ((format (printf, 4, 5))) FAST_FUNC;
int ioctl_or_perror_and_die(int fd, unsigned request, void *argp, const char *fmt,...) __attribute__ ((format (printf, 4, 5))) FAST_FUNC;
#if ENABLE_IOCTL_HEX2STR_ERROR
int bb_ioctl_or_warn(int fd, unsigned request, void *argp, const char *ioctl_name) FAST_FUNC;
int bb_xioctl(int fd, unsigned request, void *argp, const char *ioctl_name) FAST_FUNC;
#define ioctl_or_warn(fd,request,argp) bb_ioctl_or_warn(fd,request,argp,#request)
#define xioctl(fd,request,argp)        bb_xioctl(fd,request,argp,#request)
#else
int bb_ioctl_or_warn(int fd, unsigned request, void *argp) FAST_FUNC;
int bb_xioctl(int fd, unsigned request, void *argp) FAST_FUNC;
#define ioctl_or_warn(fd,request,argp) bb_ioctl_or_warn(fd,request,argp)
#define xioctl(fd,request,argp)        bb_xioctl(fd,request,argp)
#endif

char *is_in_ino_dev_hashtable(const struct stat *statbuf) FAST_FUNC;
void add_to_ino_dev_hashtable(const struct stat *statbuf, const char *name) FAST_FUNC;
void reset_ino_dev_hashtable(void) FAST_FUNC;
#ifdef __GLIBC__
/* At least glibc has horrendously large inline for this, so wrap it */
unsigned long long bb_makedev(unsigned major, unsigned minor) FAST_FUNC;
#undef makedev
#define makedev(a,b) bb_makedev(a,b)
#endif


/* "Keycodes" that report an escape sequence.
 * We use something which fits into signed char,
 * yet doesn't represent any valid Unicode character.
 * Also, -1 is reserved for error indication and we don't use it. */
enum {
	KEYCODE_UP        =  -2,
	KEYCODE_DOWN      =  -3,
	KEYCODE_RIGHT     =  -4,
	KEYCODE_LEFT      =  -5,
	KEYCODE_HOME      =  -6,
	KEYCODE_END       =  -7,
	KEYCODE_INSERT    =  -8,
	KEYCODE_DELETE    =  -9,
	KEYCODE_PAGEUP    = -10,
	KEYCODE_PAGEDOWN  = -11,
	KEYCODE_BACKSPACE = -12, /* Used only if Alt/Ctrl/Shifted */
	KEYCODE_D         = -13, /* Used only if Alted */
#if 0
	KEYCODE_FUN1      = ,
	KEYCODE_FUN2      = ,
	KEYCODE_FUN3      = ,
	KEYCODE_FUN4      = ,
	KEYCODE_FUN5      = ,
	KEYCODE_FUN6      = ,
	KEYCODE_FUN7      = ,
	KEYCODE_FUN8      = ,
	KEYCODE_FUN9      = ,
	KEYCODE_FUN10     = ,
	KEYCODE_FUN11     = ,
	KEYCODE_FUN12     = ,
#endif
	/* ^^^^^ Be sure that last defined value is small enough.
	 * Current read_key() code allows going up to -32 (0xfff..fffe0).
	 * This gives three upper bits in LSB to play with:
	 * KEYCODE_foo values are 0xfff..fffXX, lowest XX bits are: scavvvvv,
	 * s=0 if SHIFT, c=0 if CTRL, a=0 if ALT,
	 * vvvvv bits are the same for same key regardless of "shift bits".
	 */
	//KEYCODE_SHIFT_...   = KEYCODE_...   & ~0x80,
	KEYCODE_CTRL_RIGHT    = KEYCODE_RIGHT & ~0x40,
	KEYCODE_CTRL_LEFT     = KEYCODE_LEFT  & ~0x40,
	KEYCODE_ALT_RIGHT     = KEYCODE_RIGHT & ~0x20,
	KEYCODE_ALT_LEFT      = KEYCODE_LEFT  & ~0x20,
	KEYCODE_ALT_BACKSPACE = KEYCODE_BACKSPACE & ~0x20,
	KEYCODE_ALT_D         = KEYCODE_D     & ~0x20,

	KEYCODE_CURSOR_POS = -0x100, /* 0xfff..fff00 */
	/* How long is the longest ESC sequence we know?
	 * We want it big enough to be able to contain
	 * cursor position sequence "ESC [ 9999 ; 9999 R"
	 */
	KEYCODE_BUFFER_SIZE = 16
};
/* Note: fd may be in blocking or non-blocking mode, both make sense.
 * For one, less uses non-blocking mode.
 * Only the first read syscall inside read_key may block indefinitely
 * (unless fd is in non-blocking mode),
 * subsequent reads will time out after a few milliseconds.
 * Return of -1 means EOF or error (errno == 0 on EOF).
 * buffer[0] is used as a counter of buffered chars and must be 0
 * on first call.
 * timeout:
 * -2: do not poll(-1) for input - read() it, return on EAGAIN at once
 * -1: poll(-1) (i.e. block even on NONBLOCKed fd)
 * >=0: poll() for TIMEOUT milliseconds, return -1/EAGAIN on timeout
 */
int64_t read_key(int fd, char *buffer, int timeout) FAST_FUNC;
void read_key_ungets(char *buffer, const char *str, unsigned len) FAST_FUNC;


#if ENABLE_FEATURE_EDITING
/* It's NOT just ENABLEd or disabled. It's a number: */
# if defined CONFIG_FEATURE_EDITING_HISTORY && CONFIG_FEATURE_EDITING_HISTORY > 0
#  define MAX_HISTORY (CONFIG_FEATURE_EDITING_HISTORY + 0)
unsigned size_from_HISTFILESIZE(const char *hp) FAST_FUNC;
# else
#  define MAX_HISTORY 0
# endif
typedef struct line_input_t {
	int flags;
	int timeout;
	const char *path_lookup;
# if MAX_HISTORY
	int cnt_history;
	int cur_history;
	int max_history; /* must never be <= 0 */
#  if ENABLE_FEATURE_EDITING_SAVEHISTORY
	/* meaning of this field depends on FEATURE_EDITING_SAVE_ON_EXIT:
	 * if !FEATURE_EDITING_SAVE_ON_EXIT: "how many lines are
	 * in on-disk history"
	 * if FEATURE_EDITING_SAVE_ON_EXIT: "how many in-memory lines are
	 * also in on-disk history (and thus need to be skipped on save)"
	 */
	unsigned cnt_history_in_file;
	const char *hist_file;
#  endif
	char *history[MAX_HISTORY + 1];
# endif
} line_input_t;
enum {
	DO_HISTORY       = 1 * (MAX_HISTORY > 0),
	TAB_COMPLETION   = 2 * ENABLE_FEATURE_TAB_COMPLETION,
	USERNAME_COMPLETION = 4 * ENABLE_FEATURE_USERNAME_COMPLETION,
	VI_MODE          = 8 * ENABLE_FEATURE_EDITING_VI,
	WITH_PATH_LOOKUP = 0x10,
	FOR_SHELL        = DO_HISTORY | TAB_COMPLETION | USERNAME_COMPLETION,
};
line_input_t *new_line_input_t(int flags) FAST_FUNC;
/* So far static: void free_line_input_t(line_input_t *n) FAST_FUNC; */
/*
 * maxsize must be >= 2.
 * Returns:
 * -1 on read errors or EOF, or on bare Ctrl-D,
 * 0  on ctrl-C (the line entered is still returned in 'command'),
 * >0 length of input string, including terminating '\n'
 */
int read_line_input(line_input_t *st, const char *prompt, char *command, int maxsize) FAST_FUNC;
void show_history(const line_input_t *st) FAST_FUNC;
# if ENABLE_FEATURE_EDITING_SAVE_ON_EXIT
void save_history(line_input_t *st);
# endif
#else
#define MAX_HISTORY 0
int read_line_input(const char* prompt, char* command, int maxsize) FAST_FUNC;
#define read_line_input(state, prompt, command, maxsize) \
	read_line_input(prompt, command, maxsize)
#endif


#ifndef COMM_LEN
# ifdef TASK_COMM_LEN
enum { COMM_LEN = TASK_COMM_LEN };
# else
/* synchronize with sizeof(task_struct.comm) in /usr/include/linux/sched.h */
enum { COMM_LEN = 16 };
# endif
#endif

struct smaprec {
	unsigned long mapped_rw;
	unsigned long mapped_ro;
	unsigned long shared_clean;
	unsigned long shared_dirty;
	unsigned long private_clean;
	unsigned long private_dirty;
	unsigned long stack;
	unsigned long smap_pss, smap_swap;
	unsigned long smap_size;
	unsigned long smap_start;
	char smap_mode[5];
	char *smap_name;
};

#if !ENABLE_PMAP
#define procps_read_smaps(pid, total, cb, data) \
	procps_read_smaps(pid, total)
#endif
int FAST_FUNC procps_read_smaps(pid_t pid, struct smaprec *total,
		void (*cb)(struct smaprec *, void *), void *data);

typedef struct procps_status_t {
	DIR *dir;
	IF_FEATURE_SHOW_THREADS(DIR *task_dir;)
	uint8_t shift_pages_to_bytes;
	uint8_t shift_pages_to_kb;
/* Fields are set to 0/NULL if failed to determine (or not requested) */
	uint16_t argv_len;
	char *argv0;
	char *exe;
	IF_SELINUX(char *context;)
	IF_FEATURE_SHOW_THREADS(unsigned main_thread_pid;)
	/* Everything below must contain no ptrs to malloc'ed data:
	 * it is memset(0) for each process in procps_scan() */
	unsigned long vsz, rss; /* we round it to kbytes */
	unsigned long stime, utime;
	unsigned long start_time;
	unsigned pid;
	unsigned ppid;
	unsigned pgid;
	unsigned sid;
	unsigned uid;
	unsigned gid;
#if ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS
	unsigned ruid;
	unsigned rgid;
	int niceness;
#endif
	unsigned tty_major,tty_minor;
#if ENABLE_FEATURE_TOPMEM
	struct smaprec smaps;
#endif
	char state[4];
	/* basename of executable in exec(2), read from /proc/N/stat
	 * (if executable is symlink or script, it is NOT replaced
	 * by link target or interpreter name) */
	char comm[COMM_LEN];
	/* user/group? - use passwd/group parsing functions */
#if ENABLE_FEATURE_TOP_SMP_PROCESS
	int last_seen_on_cpu;
#endif
} procps_status_t;
/* flag bits for procps_scan(xx, flags) calls */
enum {
	PSSCAN_PID      = 1 << 0,
	PSSCAN_PPID     = 1 << 1,
	PSSCAN_PGID     = 1 << 2,
	PSSCAN_SID      = 1 << 3,
	PSSCAN_UIDGID   = 1 << 4,
	PSSCAN_COMM     = 1 << 5,
	/* PSSCAN_CMD      = 1 << 6, - use read_cmdline instead */
	PSSCAN_ARGV0    = 1 << 7,
	PSSCAN_EXE      = 1 << 8,
	PSSCAN_STATE    = 1 << 9,
	PSSCAN_VSZ      = 1 << 10,
	PSSCAN_RSS      = 1 << 11,
	PSSCAN_STIME    = 1 << 12,
	PSSCAN_UTIME    = 1 << 13,
	PSSCAN_TTY      = 1 << 14,
	PSSCAN_SMAPS	= (1 << 15) * ENABLE_FEATURE_TOPMEM,
	/* NB: used by find_pid_by_name(). Any applet using it
	 * needs to be mentioned here. */
	PSSCAN_ARGVN    = (1 << 16) * (ENABLE_KILLALL
				|| ENABLE_PGREP || ENABLE_PKILL
				|| ENABLE_PIDOF
				|| ENABLE_SESTATUS
				),
	PSSCAN_CONTEXT  = (1 << 17) * ENABLE_SELINUX,
	PSSCAN_START_TIME = 1 << 18,
	PSSCAN_CPU      = (1 << 19) * ENABLE_FEATURE_TOP_SMP_PROCESS,
	PSSCAN_NICE     = (1 << 20) * ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS,
	PSSCAN_RUIDGID  = (1 << 21) * ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS,
	PSSCAN_TASKS	= (1 << 22) * ENABLE_FEATURE_SHOW_THREADS,
};
//procps_status_t* alloc_procps_scan(void) FAST_FUNC;
void free_procps_scan(procps_status_t* sp) FAST_FUNC;
procps_status_t* procps_scan(procps_status_t* sp, int flags) FAST_FUNC;
/* Format cmdline (up to col chars) into char buf[size] */
/* Puts [comm] if cmdline is empty (-> process is a kernel thread) */
void read_cmdline(char *buf, int size, unsigned pid, const char *comm) FAST_FUNC;
pid_t *find_pid_by_name(const char* procName) FAST_FUNC;
pid_t *pidlist_reverse(pid_t *pidList) FAST_FUNC;
int starts_with_cpu(const char *str) FAST_FUNC;
unsigned get_cpu_count(void) FAST_FUNC;


/* Use strict=1 if you process input from untrusted source:
 * it will return NULL on invalid %xx (bad hex chars)
 * and str + 1 if decoded char is / or NUL.
 * In non-strict mode, it always succeeds (returns str),
 * and also it additionally decoded '+' to space.
 */
char *percent_decode_in_place(char *str, int strict) FAST_FUNC;


extern const char bb_uuenc_tbl_base64[] ALIGN1;
extern const char bb_uuenc_tbl_std[] ALIGN1;
void bb_uuencode(char *store, const void *s, int length, const char *tbl) FAST_FUNC;
enum {
	BASE64_FLAG_UU_STOP = 0x100,
	/* Sign-extends to a value which never matches fgetc result: */
	BASE64_FLAG_NO_STOP_CHAR = 0x80,
};
const char *decode_base64(char **pp_dst, const char *src) FAST_FUNC;
void read_base64(FILE *src_stream, FILE *dst_stream, int flags) FAST_FUNC;

typedef struct md5_ctx_t {
	uint8_t wbuffer[64]; /* always correctly aligned for uint64_t */
	void (*process_block)(struct md5_ctx_t*) FAST_FUNC;
	uint64_t total64;    /* must be directly before hash[] */
	uint32_t hash[8];    /* 4 elements for md5, 5 for sha1, 8 for sha256 */
} md5_ctx_t;
typedef struct md5_ctx_t sha1_ctx_t;
typedef struct md5_ctx_t sha256_ctx_t;
typedef struct sha512_ctx_t {
	uint64_t total64[2];  /* must be directly before hash[] */
	uint64_t hash[8];
	uint8_t wbuffer[128]; /* always correctly aligned for uint64_t */
} sha512_ctx_t;
typedef struct sha3_ctx_t {
	uint64_t state[25];
	unsigned bytes_queued;
	unsigned input_block_bytes;
} sha3_ctx_t;
void md5_begin(md5_ctx_t *ctx) FAST_FUNC;
void md5_hash(md5_ctx_t *ctx, const void *buffer, size_t len) FAST_FUNC;
unsigned md5_end(md5_ctx_t *ctx, void *resbuf) FAST_FUNC;
void sha1_begin(sha1_ctx_t *ctx) FAST_FUNC;
#define sha1_hash md5_hash
unsigned sha1_end(sha1_ctx_t *ctx, void *resbuf) FAST_FUNC;
void sha256_begin(sha256_ctx_t *ctx) FAST_FUNC;
#define sha256_hash md5_hash
#define sha256_end  sha1_end
void sha512_begin(sha512_ctx_t *ctx) FAST_FUNC;
void sha512_hash(sha512_ctx_t *ctx, const void *buffer, size_t len) FAST_FUNC;
unsigned sha512_end(sha512_ctx_t *ctx, void *resbuf) FAST_FUNC;
void sha3_begin(sha3_ctx_t *ctx) FAST_FUNC;
void sha3_hash(sha3_ctx_t *ctx, const void *buffer, size_t len) FAST_FUNC;
unsigned sha3_end(sha3_ctx_t *ctx, void *resbuf) FAST_FUNC;
/* TLS benefits from knowing that sha1 and sha256 share these. Give them "agnostic" names too */
typedef struct md5_ctx_t md5sha_ctx_t;
#define md5sha_hash md5_hash
#define sha_end sha1_end

extern uint32_t *global_crc32_table;
uint32_t *crc32_filltable(uint32_t *tbl256, int endian) FAST_FUNC;
uint32_t *crc32_new_table_le(void) FAST_FUNC;
uint32_t *global_crc32_new_table_le(void) FAST_FUNC;
uint32_t crc32_block_endian1(uint32_t val, const void *buf, unsigned len, uint32_t *crc_table) FAST_FUNC;
uint32_t crc32_block_endian0(uint32_t val, const void *buf, unsigned len, uint32_t *crc_table) FAST_FUNC;

typedef struct masks_labels_t {
	const char *labels;
	const int masks[];
} masks_labels_t;
int print_flags_separated(const int *masks, const char *labels,
		int flags, const char *separator) FAST_FUNC;
int print_flags(const masks_labels_t *ml, int flags) FAST_FUNC;

typedef struct bb_progress_t {
	unsigned last_size;
	unsigned last_update_sec;
	unsigned last_change_sec;
	unsigned start_sec;
	/*unsigned last_eta;*/
	const char *curfile;
} bb_progress_t;

#define is_bb_progress_inited(p) ((p)->curfile != NULL)
#define bb_progress_free(p) do { \
	if (ENABLE_UNICODE_SUPPORT) free((char*)((p)->curfile)); \
	(p)->curfile = NULL; \
} while (0)
void bb_progress_init(bb_progress_t *p, const char *curfile) FAST_FUNC;
void bb_progress_update(bb_progress_t *p,
			uoff_t beg_range,
			uoff_t transferred,
			uoff_t totalsize) FAST_FUNC;

unsigned ubi_devnum_from_devname(const char *str) FAST_FUNC;
int ubi_get_volid_by_name(unsigned ubi_devnum, const char *vol_name) FAST_FUNC;


extern const char *applet_name;

/* Some older linkers don't perform string merging, we used to have common strings
 * as global arrays to do it by hand. But:
 * (1) newer linkers do it themselves,
 * (2) however, they DONT merge string constants with global arrays,
 * even if the value is the same (!). Thus global arrays actually
 * increased size a bit: for example, "/etc/passwd" string from libc
 * wasn't merged with bb_path_passwd_file[] array!
 * Therefore now we use #defines.
 */
/* "BusyBox vN.N.N (timestamp or extra_version)" */
extern const char bb_banner[] ALIGN1;
extern const char bb_msg_memory_exhausted[] ALIGN1;
extern const char bb_msg_invalid_date[] ALIGN1;
#define bb_msg_read_error "read error"
#define bb_msg_write_error "write error"
extern const char bb_msg_unknown[] ALIGN1;
extern const char bb_msg_can_not_create_raw_socket[] ALIGN1;
extern const char bb_msg_perm_denied_are_you_root[] ALIGN1;
extern const char bb_msg_you_must_be_root[] ALIGN1;
extern const char bb_msg_requires_arg[] ALIGN1;
extern const char bb_msg_invalid_arg_to[] ALIGN1;
extern const char bb_msg_standard_input[] ALIGN1;
extern const char bb_msg_standard_output[] ALIGN1;

/* NB: (bb_hexdigits_upcase[i] | 0x20) -> lowercase hex digit */
extern const char bb_hexdigits_upcase[] ALIGN1;

extern const char bb_path_wtmp_file[] ALIGN1;

/* Busybox mount uses either /proc/mounts or /etc/mtab to
 * get the list of currently mounted filesystems */
#define bb_path_mtab_file IF_FEATURE_MTAB_SUPPORT("/etc/mtab")IF_NOT_FEATURE_MTAB_SUPPORT("/proc/mounts")

#define bb_path_passwd_file  _PATH_PASSWD
#define bb_path_group_file   _PATH_GROUP
#define bb_path_shadow_file  _PATH_SHADOW
#define bb_path_gshadow_file _PATH_GSHADOW

#define bb_path_motd_file "/etc/motd"

#define bb_dev_null "/dev/null"
extern const char bb_busybox_exec_path[] ALIGN1;
/* allow default system PATH to be extended via CFLAGS */
#ifndef BB_ADDITIONAL_PATH
#define BB_ADDITIONAL_PATH ""
#endif
#define BB_PATH_ROOT_PATH "PATH=/sbin:/usr/sbin:/bin:/usr/bin" BB_ADDITIONAL_PATH
extern const char bb_PATH_root_path[] ALIGN1; /* BB_PATH_ROOT_PATH */
#define bb_default_root_path (bb_PATH_root_path + sizeof("PATH"))
/* util-linux manpage says /sbin:/bin:/usr/sbin:/usr/bin,
 * but I want to save a few bytes here:
 */
#define bb_default_path      (bb_PATH_root_path + sizeof("PATH=/sbin:/usr/sbin"))

extern const int const_int_0;
//extern const int const_int_1;

/* This struct is deliberately not defined. */
/* See docs/keep_data_small.txt */
struct globals;
/* '*const' ptr makes gcc optimize code much better.
 * Magic prevents ptr_to_globals from going into rodata.
 * If you want to assign a value, use SET_PTR_TO_GLOBALS(x) */
extern struct globals *const ptr_to_globals;
/* At least gcc 3.4.6 on mipsel system needs optimization barrier */
#define barrier() __asm__ __volatile__("":::"memory")
#define SET_PTR_TO_GLOBALS(x) do { \
	(*(struct globals**)&ptr_to_globals) = (void*)(x); \
	barrier(); \
} while (0)
#define FREE_PTR_TO_GLOBALS() do { \
	if (ENABLE_FEATURE_CLEAN_UP) { \
		free(ptr_to_globals); \
	} \
} while (0)

/* You can change LIBBB_DEFAULT_LOGIN_SHELL, but don't use it,
 * use bb_default_login_shell and following defines.
 * If you change LIBBB_DEFAULT_LOGIN_SHELL,
 * don't forget to change increment constant. */
#define LIBBB_DEFAULT_LOGIN_SHELL  "-/bin/sh"
extern const char bb_default_login_shell[] ALIGN1;
/* "/bin/sh" */
#define DEFAULT_SHELL              (bb_default_login_shell+1)
/* "sh" */
#define DEFAULT_SHELL_SHORT_NAME   (bb_default_login_shell+6)

/* The following devices are the same on all systems.  */
#define CURRENT_TTY "/dev/tty"
#define DEV_CONSOLE "/dev/console"

#if defined(__FreeBSD_kernel__)
# define CURRENT_VC CURRENT_TTY
# define VC_1 "/dev/ttyv0"
# define VC_2 "/dev/ttyv1"
# define VC_3 "/dev/ttyv2"
# define VC_4 "/dev/ttyv3"
# define VC_5 "/dev/ttyv4"
# define VC_FORMAT "/dev/ttyv%d"
#elif defined(__GNU__)
# define CURRENT_VC CURRENT_TTY
# define VC_1 "/dev/tty1"
# define VC_2 "/dev/tty2"
# define VC_3 "/dev/tty3"
# define VC_4 "/dev/tty4"
# define VC_5 "/dev/tty5"
# define VC_FORMAT "/dev/tty%d"
#elif ENABLE_FEATURE_DEVFS
/*Linux, obsolete devfs names */
# define CURRENT_VC "/dev/vc/0"
# define VC_1 "/dev/vc/1"
# define VC_2 "/dev/vc/2"
# define VC_3 "/dev/vc/3"
# define VC_4 "/dev/vc/4"
# define VC_5 "/dev/vc/5"
# define VC_FORMAT "/dev/vc/%d"
# define LOOP_FORMAT "/dev/loop/%u"
# define LOOP_NAMESIZE (sizeof("/dev/loop/") + sizeof(int)*3 + 1)
# define LOOP_NAME "/dev/loop/"
# define FB_0 "/dev/fb/0"
#else
/*Linux, normal names */
# define CURRENT_VC "/dev/tty0"
# define VC_1 "/dev/tty1"
# define VC_2 "/dev/tty2"
# define VC_3 "/dev/tty3"
# define VC_4 "/dev/tty4"
# define VC_5 "/dev/tty5"
# define VC_FORMAT "/dev/tty%d"
# define LOOP_FORMAT "/dev/loop%u"
# define LOOP_NAMESIZE (sizeof("/dev/loop") + sizeof(int)*3 + 1)
# define LOOP_NAME "/dev/loop"
# define FB_0 "/dev/fb0"
#endif


#define ARRAY_SIZE(x) ((unsigned)(sizeof(x) / sizeof((x)[0])))
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))


/* We redefine ctype macros. Unicode-correct handling of char types
 * can't be done with such byte-oriented operations anyway,
 * we don't lose anything.
 */
#undef isalnum
#undef isalpha
#undef isascii
#undef isblank
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef toupper
#undef tolower

/* We save ~500 bytes on isdigit alone.
 * BTW, x86 likes (unsigned char) cast more than (unsigned). */

/* These work the same for ASCII and Unicode,
 * assuming no one asks "is this a *Unicode* letter?" using isalpha(letter) */
#define isascii(a) ((unsigned char)(a) <= 0x7f)
#define isdigit(a) ((unsigned char)((a) - '0') <= 9)
#define isupper(a) ((unsigned char)((a) - 'A') <= ('Z' - 'A'))
#define islower(a) ((unsigned char)((a) - 'a') <= ('z' - 'a'))
#define isalpha(a) ((unsigned char)(((a)|0x20) - 'a') <= ('z' - 'a'))
#define isblank(a) ({ unsigned char bb__isblank = (a); bb__isblank == ' ' || bb__isblank == '\t'; })
#define iscntrl(a) ({ unsigned char bb__iscntrl = (a); bb__iscntrl < ' ' || bb__iscntrl == 0x7f; })
/* In POSIX/C locale isspace is only these chars: "\t\n\v\f\r" and space.
 * "\t\n\v\f\r" happen to have ASCII codes 9,10,11,12,13.
 */
#define isspace(a) ({ unsigned char bb__isspace = (a) - 9; bb__isspace == (' ' - 9) || bb__isspace <= (13 - 9); })
// Unsafe wrt NUL: #define ispunct(a) (strchr("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", (a)) != NULL)
#define ispunct(a) (strchrnul("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", (a))[0])
// Bigger code: #define isalnum(a) ({ unsigned char bb__isalnum = (a) - '0'; bb__isalnum <= 9 || ((bb__isalnum - ('A' - '0')) & 0xdf) <= 25; })
#define isalnum(a) bb_ascii_isalnum(a)
static ALWAYS_INLINE int bb_ascii_isalnum(unsigned char a)
{
	unsigned char b = a - '0';
	if (b <= 9)
		return (b <= 9);
	b = (a|0x20) - 'a';
	return b <= 'z' - 'a';
}
#define isxdigit(a) bb_ascii_isxdigit(a)
static ALWAYS_INLINE int bb_ascii_isxdigit(unsigned char a)
{
	unsigned char b = a - '0';
	if (b <= 9)
		return (b <= 9);
	b = (a|0x20) - 'a';
	return b <= 'f' - 'a';
}
#define toupper(a) bb_ascii_toupper(a)
static ALWAYS_INLINE unsigned char bb_ascii_toupper(unsigned char a)
{
	unsigned char b = a - 'a';
	if (b <= ('z' - 'a'))
		a -= 'a' - 'A';
	return a;
}
#define tolower(a) bb_ascii_tolower(a)
static ALWAYS_INLINE unsigned char bb_ascii_tolower(unsigned char a)
{
	unsigned char b = a - 'A';
	if (b <= ('Z' - 'A'))
		a += 'a' - 'A';
	return a;
}

/* In ASCII and Unicode, these are likely to be very different.
 * Let's prevent ambiguous usage from the start */
#define isgraph(a) isgraph_is_ambiguous_dont_use(a)
#define isprint(a) isprint_is_ambiguous_dont_use(a)
/* NB: must not treat EOF as isgraph or isprint */
#define isgraph_asciionly(a) ((unsigned)((a) - 0x21) <= 0x7e - 0x21)
#define isprint_asciionly(a) ((unsigned)((a) - 0x20) <= 0x7e - 0x20)


/* Simple unit-testing framework */

typedef void (*bbunit_testfunc)(void);

struct bbunit_listelem {
	const char* name;
	bbunit_testfunc testfunc;
};

void bbunit_registertest(struct bbunit_listelem* test);
void bbunit_settestfailed(void);

#define BBUNIT_DEFINE_TEST(NAME) \
	static void bbunit_##NAME##_test(void); \
	static struct bbunit_listelem bbunit_##NAME##_elem = { \
		.name = #NAME, \
		.testfunc = bbunit_##NAME##_test, \
	}; \
	static void INIT_FUNC bbunit_##NAME##_register(void) \
	{ \
		bbunit_registertest(&bbunit_##NAME##_elem); \
	} \
	static void bbunit_##NAME##_test(void)

/*
 * Both 'goto bbunit_end' and 'break' are here only to get rid
 * of compiler warnings.
 */
#define BBUNIT_ENDTEST \
	do { \
		goto bbunit_end; \
	bbunit_end: \
		break; \
	} while (0)

#define BBUNIT_PRINTASSERTFAIL \
	do { \
		bb_error_msg( \
			"[ERROR] Assertion failed in file %s, line %d", \
			__FILE__, __LINE__); \
	} while (0)

#define BBUNIT_ASSERTION_FAILED \
	do { \
		bbunit_settestfailed(); \
		goto bbunit_end; \
	} while (0)

/*
 * Assertions.
 * For now we only offer assertions which cause tests to fail
 * immediately. In the future 'expects' might be added too -
 * similar to those offered by the gtest framework.
 */
#define BBUNIT_ASSERT_EQ(EXPECTED, ACTUAL) \
	do { \
		if ((EXPECTED) != (ACTUAL)) { \
			BBUNIT_PRINTASSERTFAIL; \
			bb_error_msg("[ERROR] '%s' isn't equal to '%s'", \
						#EXPECTED, #ACTUAL); \
			BBUNIT_ASSERTION_FAILED; \
		} \
	} while (0)

#define BBUNIT_ASSERT_NOTEQ(EXPECTED, ACTUAL) \
	do { \
		if ((EXPECTED) == (ACTUAL)) { \
			BBUNIT_PRINTASSERTFAIL; \
			bb_error_msg("[ERROR] '%s' is equal to '%s'", \
						#EXPECTED, #ACTUAL); \
			BBUNIT_ASSERTION_FAILED; \
		} \
	} while (0)

#define BBUNIT_ASSERT_NOTNULL(PTR) \
	do { \
		if ((PTR) == NULL) { \
			BBUNIT_PRINTASSERTFAIL; \
			bb_error_msg("[ERROR] '%s' is NULL!", #PTR); \
			BBUNIT_ASSERTION_FAILED; \
		} \
	} while (0)

#define BBUNIT_ASSERT_NULL(PTR) \
	do { \
		if ((PTR) != NULL) { \
			BBUNIT_PRINTASSERTFAIL; \
			bb_error_msg("[ERROR] '%s' is not NULL!", #PTR); \
			BBUNIT_ASSERTION_FAILED; \
		} \
	} while (0)

#define BBUNIT_ASSERT_FALSE(STATEMENT) \
	do { \
		if ((STATEMENT)) { \
			BBUNIT_PRINTASSERTFAIL; \
			bb_error_msg("[ERROR] Statement '%s' evaluated to true!", \
								#STATEMENT); \
			BBUNIT_ASSERTION_FAILED; \
		} \
	} while (0)

#define BBUNIT_ASSERT_TRUE(STATEMENT) \
	do { \
		if (!(STATEMENT)) { \
			BBUNIT_PRINTASSERTFAIL; \
			bb_error_msg("[ERROR] Statement '%s' evaluated to false!", \
					#STATEMENT); \
			BBUNIT_ASSERTION_FAILED; \
		} \
	} while (0)

#define BBUNIT_ASSERT_STREQ(STR1, STR2) \
	do { \
		if (strcmp(STR1, STR2) != 0) { \
			BBUNIT_PRINTASSERTFAIL; \
			bb_error_msg("[ERROR] Strings '%s' and '%s' " \
					"are not the same", STR1, STR2); \
			BBUNIT_ASSERTION_FAILED; \
		} \
	} while (0)

#define BBUNIT_ASSERT_STRNOTEQ(STR1, STR2) \
	do { \
		if (strcmp(STR1, STR2) == 0) { \
			BBUNIT_PRINTASSERTFAIL; \
			bb_error_msg("[ERROR] Strings '%s' and '%s' " \
					"are the same, but were " \
					"expected to differ", STR1, STR2); \
			BBUNIT_ASSERTION_FAILED; \
		} \
	} while (0)


POP_SAVED_FUNCTION_VISIBILITY

#endif
