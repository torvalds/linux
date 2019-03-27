//===-- sanitizer_platform_limits_openbsd.h -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer common code.
//
// Sizes and layouts of platform-specific OpenBSD data structures.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_PLATFORM_LIMITS_OPENBSD_H
#define SANITIZER_PLATFORM_LIMITS_OPENBSD_H

#if SANITIZER_OPENBSD

#include "sanitizer_internal_defs.h"
#include "sanitizer_platform.h"

#define _GET_LINK_MAP_BY_DLOPEN_HANDLE(handle, shift) \
  ((link_map *)((handle) == nullptr ? nullptr : ((char *)(handle) + (shift))))

#if defined(__x86_64__)
#define GET_LINK_MAP_BY_DLOPEN_HANDLE(handle) \
  _GET_LINK_MAP_BY_DLOPEN_HANDLE(handle, 312)
#elif defined(__i386__)
#define GET_LINK_MAP_BY_DLOPEN_HANDLE(handle) \
  _GET_LINK_MAP_BY_DLOPEN_HANDLE(handle, 164)
#endif

#define RLIMIT_AS RLIMIT_DATA

namespace __sanitizer {
extern unsigned struct_utsname_sz;
extern unsigned struct_stat_sz;
extern unsigned struct_rusage_sz;
extern unsigned siginfo_t_sz;
extern unsigned struct_itimerval_sz;
extern unsigned pthread_t_sz;
extern unsigned pthread_mutex_t_sz;
extern unsigned pthread_cond_t_sz;
extern unsigned pid_t_sz;
extern unsigned timeval_sz;
extern unsigned uid_t_sz;
extern unsigned gid_t_sz;
extern unsigned mbstate_t_sz;
extern unsigned struct_timezone_sz;
extern unsigned struct_tms_sz;
extern unsigned struct_itimerspec_sz;
extern unsigned struct_sigevent_sz;
extern unsigned struct_statfs_sz;
extern unsigned struct_sockaddr_sz;

extern unsigned struct_rlimit_sz;
extern unsigned struct_utimbuf_sz;
extern unsigned struct_timespec_sz;

struct __sanitizer_iocb {
  u64 aio_offset;
  uptr aio_buf;
  long aio_nbytes;
  u32 aio_fildes;
  u32 aio_lio_opcode;
  long aio_reqprio;
#if SANITIZER_WORDSIZE == 64
  u8 aio_sigevent[32];
#else
  u8 aio_sigevent[20];
#endif
  u32 _state;
  u32 _errno;
  long _retval;
};

struct __sanitizer___sysctl_args {
  int *name;
  int nlen;
  void *oldval;
  uptr *oldlenp;
  void *newval;
  uptr newlen;
};

struct __sanitizer_sem_t {
  uptr data[5];
};

struct __sanitizer_ipc_perm {
  u32 cuid;
  u32 cgid;
  u32 uid;
  u32 gid;
  u32 mode;
  unsigned short seq;
  long key;
};

struct __sanitizer_shmid_ds {
  __sanitizer_ipc_perm shm_perm;
  int shm_segsz;
  u32 shm_lpid;
  u32 shm_cpid;
  short shm_nattch;
  u64 shm_atime;
  long __shm_atimensec;
  u64 shm_dtime;
  long __shm_dtimensec;
  u64 shm_ctime;
  long __shm_ctimensec;
  void *_shm_internal;
};

extern unsigned struct_msqid_ds_sz;
extern unsigned struct_mq_attr_sz;
extern unsigned struct_timex_sz;
extern unsigned struct_statvfs_sz;

struct __sanitizer_iovec {
  void *iov_base;
  uptr iov_len;
};

struct __sanitizer_ifaddrs {
  struct __sanitizer_ifaddrs *ifa_next;
  char *ifa_name;
  unsigned int ifa_flags;
  struct __sanitizer_sockaddr *ifa_addr;     // (struct sockaddr *)
  struct __sanitizer_sockaddr *ifa_netmask;  // (struct sockaddr *)
  struct __sanitizer_sockaddr *ifa_dstaddr;  // (struct sockaddr *)
  void *ifa_data;
};

typedef unsigned __sanitizer_pthread_key_t;

typedef long long __sanitizer_time_t;
typedef int __sanitizer_suseconds_t;

struct __sanitizer_timeval {
  __sanitizer_time_t tv_sec;
  __sanitizer_suseconds_t tv_usec;
};

struct __sanitizer_itimerval {
  struct __sanitizer_timeval it_interval;
  struct __sanitizer_timeval it_value;
};

struct __sanitizer_passwd {
  char *pw_name;
  char *pw_passwd;
  int pw_uid;
  int pw_gid;
  __sanitizer_time_t pw_change;
  char *pw_class;
  char *pw_gecos;
  char *pw_dir;
  char *pw_shell;
  __sanitizer_time_t pw_expire;
};

struct __sanitizer_group {
  char *gr_name;
  char *gr_passwd;
  int gr_gid;
  char **gr_mem;
};

struct __sanitizer_ether_addr {
  u8 octet[6];
};

struct __sanitizer_tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
  long int tm_gmtoff;
  const char *tm_zone;
};

struct __sanitizer_msghdr {
  void *msg_name;
  unsigned msg_namelen;
  struct __sanitizer_iovec *msg_iov;
  unsigned msg_iovlen;
  void *msg_control;
  unsigned msg_controllen;
  int msg_flags;
};
struct __sanitizer_cmsghdr {
  unsigned cmsg_len;
  int cmsg_level;
  int cmsg_type;
};

struct __sanitizer_dirent {
  u64 d_fileno;
  u64 d_off;
  u16 d_reclen;
};

typedef u64 __sanitizer_clock_t;
typedef u32 __sanitizer_clockid_t;

typedef u32 __sanitizer___kernel_uid_t;
typedef u32 __sanitizer___kernel_gid_t;
typedef u64 __sanitizer___kernel_off_t;
typedef struct {
  u32 fds_bits[8];
} __sanitizer___kernel_fd_set;

typedef struct {
  unsigned int pta_magic;
  int pta_flags;
  void *pta_private;
} __sanitizer_pthread_attr_t;

typedef unsigned int __sanitizer_sigset_t;

struct __sanitizer_siginfo {
  // The size is determined by looking at sizeof of real siginfo_t on linux.
  u64 opaque[128 / sizeof(u64)];
};

using __sanitizer_sighandler_ptr = void (*)(int sig);
using __sanitizer_sigactionhandler_ptr = void (*)(int sig,
                                                  __sanitizer_siginfo *siginfo,
                                                  void *uctx);

struct __sanitizer_sigaction {
  union {
    __sanitizer_sighandler_ptr handler;
    __sanitizer_sigactionhandler_ptr sigaction;
  };
  __sanitizer_sigset_t sa_mask;
  int sa_flags;
};

typedef __sanitizer_sigset_t __sanitizer_kernel_sigset_t;

struct __sanitizer_kernel_sigaction_t {
  union {
    void (*handler)(int signo);
    void (*sigaction)(int signo, void *info, void *ctx);
  };
  unsigned long sa_flags;
  void (*sa_restorer)(void);
  __sanitizer_kernel_sigset_t sa_mask;
};

extern const uptr sig_ign;
extern const uptr sig_dfl;
extern const uptr sig_err;
extern const uptr sa_siginfo;

extern int af_inet;
extern int af_inet6;
uptr __sanitizer_in_addr_sz(int af);

struct __sanitizer_dl_phdr_info {
#if SANITIZER_WORDSIZE == 64
  u64 dlpi_addr;
#else
  u32 dlpi_addr;
#endif
  const char *dlpi_name;
  const void *dlpi_phdr;
#if SANITIZER_WORDSIZE == 64
  u32 dlpi_phnum;
#else
  u16 dlpi_phnum;
#endif
};

extern unsigned struct_ElfW_Phdr_sz;

struct __sanitizer_addrinfo {
  int ai_flags;
  int ai_family;
  int ai_socktype;
  int ai_protocol;
  unsigned ai_addrlen;
  struct __sanitizer_sockaddr *ai_addr;
  char *ai_canonname;
  struct __sanitizer_addrinfo *ai_next;
};

struct __sanitizer_hostent {
  char *h_name;
  char **h_aliases;
  int h_addrtype;
  int h_length;
  char **h_addr_list;
};

struct __sanitizer_pollfd {
  int fd;
  short events;
  short revents;
};

typedef unsigned __sanitizer_nfds_t;

struct __sanitizer_glob_t {
  int gl_pathc;
  int gl_matchc;
  int gl_offs;
  int gl_flags;
  char **gl_pathv;
  void **gl_statv;
  int (*gl_errfunc)(const char *, int);
  void (*gl_closedir)(void *dirp);
  struct dirent *(*gl_readdir)(void *dirp);
  void *(*gl_opendir)(const char *);
  int (*gl_lstat)(const char *, void * /* struct stat* */);
  int (*gl_stat)(const char *, void * /* struct stat* */);
};

extern int glob_nomatch;
extern int glob_altdirfunc;

extern unsigned path_max;

typedef char __sanitizer_FILE;
#define SANITIZER_HAS_STRUCT_FILE 0

extern int shmctl_ipc_stat;

// This simplifies generic code
#define struct_shminfo_sz -1
#define struct_shm_info_sz -1
#define shmctl_shm_stat -1
#define shmctl_ipc_info -1
#define shmctl_shm_info -1

extern unsigned struct_utmp_sz;
extern unsigned struct_utmpx_sz;

extern int map_fixed;

// ioctl arguments
struct __sanitizer_ifconf {
  int ifc_len;
  union {
    void *ifcu_req;
  } ifc_ifcu;
};

extern const int si_SEGV_MAPERR;
extern const int si_SEGV_ACCERR;
}  // namespace __sanitizer

#define CHECK_TYPE_SIZE(TYPE) \
  COMPILER_CHECK(sizeof(__sanitizer_##TYPE) == sizeof(TYPE))

#define CHECK_SIZE_AND_OFFSET(CLASS, MEMBER)                      \
  COMPILER_CHECK(sizeof(((__sanitizer_##CLASS *)NULL)->MEMBER) == \
                 sizeof(((CLASS *)NULL)->MEMBER));                \
  COMPILER_CHECK(offsetof(__sanitizer_##CLASS, MEMBER) ==         \
                 offsetof(CLASS, MEMBER))

// For sigaction, which is a function and struct at the same time,
// and thus requires explicit "struct" in sizeof() expression.
#define CHECK_STRUCT_SIZE_AND_OFFSET(CLASS, MEMBER)                      \
  COMPILER_CHECK(sizeof(((struct __sanitizer_##CLASS *)NULL)->MEMBER) == \
                 sizeof(((struct CLASS *)NULL)->MEMBER));                \
  COMPILER_CHECK(offsetof(struct __sanitizer_##CLASS, MEMBER) ==         \
                 offsetof(struct CLASS, MEMBER))

#define SIGACTION_SYMNAME __sigaction14

#endif  // SANITIZER_OPENBSD

#endif
