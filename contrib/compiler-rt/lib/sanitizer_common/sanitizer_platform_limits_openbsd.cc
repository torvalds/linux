//===-- sanitizer_platform_limits_openbsd.cc ------------------------------===//
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
// Sizes and layouts of platform-specific NetBSD data structures.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_OPENBSD
#include <arpa/inet.h>
#include <dirent.h>
#include <glob.h>
#include <grp.h>
#include <ifaddrs.h>
#include <limits.h>
#include <link_elf.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/ppp_defs.h>
#include <net/route.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_mroute.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <semaphore.h>
#include <signal.h>
#include <soundcard.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/filio.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/msg.h>
#include <sys/mtio.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/signal.h>
#include <sys/sockio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <term.h>
#include <time.h>
#include <utime.h>
#include <utmp.h>
#include <wchar.h>

// Include these after system headers to avoid name clashes and ambiguities.
#include "sanitizer_internal_defs.h"
#include "sanitizer_platform_limits_openbsd.h"

namespace __sanitizer {
unsigned struct_utsname_sz = sizeof(struct utsname);
unsigned struct_stat_sz = sizeof(struct stat);
unsigned struct_rusage_sz = sizeof(struct rusage);
unsigned struct_tm_sz = sizeof(struct tm);
unsigned struct_passwd_sz = sizeof(struct passwd);
unsigned struct_group_sz = sizeof(struct group);
unsigned siginfo_t_sz = sizeof(siginfo_t);
unsigned struct_sigaction_sz = sizeof(struct sigaction);
unsigned struct_itimerval_sz = sizeof(struct itimerval);
unsigned pthread_t_sz = sizeof(pthread_t);
unsigned pthread_mutex_t_sz = sizeof(pthread_mutex_t);
unsigned pthread_cond_t_sz = sizeof(pthread_cond_t);
unsigned pid_t_sz = sizeof(pid_t);
unsigned timeval_sz = sizeof(timeval);
unsigned uid_t_sz = sizeof(uid_t);
unsigned gid_t_sz = sizeof(gid_t);
unsigned mbstate_t_sz = sizeof(mbstate_t);
unsigned sigset_t_sz = sizeof(sigset_t);
unsigned struct_timezone_sz = sizeof(struct timezone);
unsigned struct_tms_sz = sizeof(struct tms);
unsigned struct_sched_param_sz = sizeof(struct sched_param);
unsigned struct_sockaddr_sz = sizeof(struct sockaddr);
unsigned struct_rlimit_sz = sizeof(struct rlimit);
unsigned struct_timespec_sz = sizeof(struct timespec);
unsigned struct_utimbuf_sz = sizeof(struct utimbuf);
unsigned struct_itimerspec_sz = sizeof(struct itimerspec);
unsigned struct_msqid_ds_sz = sizeof(struct msqid_ds);
unsigned struct_statvfs_sz = sizeof(struct statvfs);

const uptr sig_ign = (uptr)SIG_IGN;
const uptr sig_dfl = (uptr)SIG_DFL;
const uptr sig_err = (uptr)SIG_ERR;
const uptr sa_siginfo = (uptr)SA_SIGINFO;

int shmctl_ipc_stat = (int)IPC_STAT;

unsigned struct_utmp_sz = sizeof(struct utmp);

int map_fixed = MAP_FIXED;

int af_inet = (int)AF_INET;
int af_inet6 = (int)AF_INET6;

uptr __sanitizer_in_addr_sz(int af) {
  if (af == AF_INET)
    return sizeof(struct in_addr);
  else if (af == AF_INET6)
    return sizeof(struct in6_addr);
  else
    return 0;
}

unsigned struct_ElfW_Phdr_sz = sizeof(Elf_Phdr);

int glob_nomatch = GLOB_NOMATCH;
int glob_altdirfunc = GLOB_ALTDIRFUNC;

unsigned path_max = PATH_MAX;

const int si_SEGV_MAPERR = SEGV_MAPERR;
const int si_SEGV_ACCERR = SEGV_ACCERR;
}  // namespace __sanitizer

using namespace __sanitizer;

COMPILER_CHECK(sizeof(__sanitizer_pthread_attr_t) >= sizeof(pthread_attr_t));

COMPILER_CHECK(sizeof(socklen_t) == sizeof(unsigned));
CHECK_TYPE_SIZE(pthread_key_t);

CHECK_TYPE_SIZE(dl_phdr_info);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_addr);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_name);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_phdr);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_phnum);

CHECK_TYPE_SIZE(glob_t);
CHECK_SIZE_AND_OFFSET(glob_t, gl_pathc);
CHECK_SIZE_AND_OFFSET(glob_t, gl_pathv);
CHECK_SIZE_AND_OFFSET(glob_t, gl_offs);
CHECK_SIZE_AND_OFFSET(glob_t, gl_flags);
CHECK_SIZE_AND_OFFSET(glob_t, gl_closedir);
CHECK_SIZE_AND_OFFSET(glob_t, gl_readdir);
CHECK_SIZE_AND_OFFSET(glob_t, gl_opendir);
CHECK_SIZE_AND_OFFSET(glob_t, gl_lstat);
CHECK_SIZE_AND_OFFSET(glob_t, gl_stat);

CHECK_TYPE_SIZE(addrinfo);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_flags);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_family);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_socktype);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_protocol);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_addrlen);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_addr);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_canonname);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_next);

CHECK_TYPE_SIZE(hostent);
CHECK_SIZE_AND_OFFSET(hostent, h_name);
CHECK_SIZE_AND_OFFSET(hostent, h_aliases);
CHECK_SIZE_AND_OFFSET(hostent, h_addrtype);
CHECK_SIZE_AND_OFFSET(hostent, h_length);
CHECK_SIZE_AND_OFFSET(hostent, h_addr_list);

CHECK_TYPE_SIZE(iovec);
CHECK_SIZE_AND_OFFSET(iovec, iov_base);
CHECK_SIZE_AND_OFFSET(iovec, iov_len);

CHECK_TYPE_SIZE(msghdr);
CHECK_SIZE_AND_OFFSET(msghdr, msg_name);
CHECK_SIZE_AND_OFFSET(msghdr, msg_namelen);
CHECK_SIZE_AND_OFFSET(msghdr, msg_iov);
CHECK_SIZE_AND_OFFSET(msghdr, msg_iovlen);
CHECK_SIZE_AND_OFFSET(msghdr, msg_control);
CHECK_SIZE_AND_OFFSET(msghdr, msg_controllen);
CHECK_SIZE_AND_OFFSET(msghdr, msg_flags);

CHECK_TYPE_SIZE(cmsghdr);
CHECK_SIZE_AND_OFFSET(cmsghdr, cmsg_len);
CHECK_SIZE_AND_OFFSET(cmsghdr, cmsg_level);
CHECK_SIZE_AND_OFFSET(cmsghdr, cmsg_type);

COMPILER_CHECK(sizeof(__sanitizer_dirent) <= sizeof(dirent));
CHECK_SIZE_AND_OFFSET(dirent, d_fileno);
CHECK_SIZE_AND_OFFSET(dirent, d_off);
CHECK_SIZE_AND_OFFSET(dirent, d_reclen);

CHECK_TYPE_SIZE(ifconf);
CHECK_SIZE_AND_OFFSET(ifconf, ifc_len);
CHECK_SIZE_AND_OFFSET(ifconf, ifc_ifcu);

CHECK_TYPE_SIZE(pollfd);
CHECK_SIZE_AND_OFFSET(pollfd, fd);
CHECK_SIZE_AND_OFFSET(pollfd, events);
CHECK_SIZE_AND_OFFSET(pollfd, revents);

CHECK_TYPE_SIZE(nfds_t);

CHECK_TYPE_SIZE(sigset_t);

COMPILER_CHECK(sizeof(__sanitizer_sigaction) == sizeof(struct sigaction));
// Can't write checks for sa_handler and sa_sigaction due to them being
// preprocessor macros.
CHECK_STRUCT_SIZE_AND_OFFSET(sigaction, sa_mask);

CHECK_TYPE_SIZE(tm);
CHECK_SIZE_AND_OFFSET(tm, tm_sec);
CHECK_SIZE_AND_OFFSET(tm, tm_min);
CHECK_SIZE_AND_OFFSET(tm, tm_hour);
CHECK_SIZE_AND_OFFSET(tm, tm_mday);
CHECK_SIZE_AND_OFFSET(tm, tm_mon);
CHECK_SIZE_AND_OFFSET(tm, tm_year);
CHECK_SIZE_AND_OFFSET(tm, tm_wday);
CHECK_SIZE_AND_OFFSET(tm, tm_yday);
CHECK_SIZE_AND_OFFSET(tm, tm_isdst);
CHECK_SIZE_AND_OFFSET(tm, tm_gmtoff);
CHECK_SIZE_AND_OFFSET(tm, tm_zone);

CHECK_TYPE_SIZE(ipc_perm);
CHECK_SIZE_AND_OFFSET(ipc_perm, cuid);
CHECK_SIZE_AND_OFFSET(ipc_perm, cgid);
CHECK_SIZE_AND_OFFSET(ipc_perm, uid);
CHECK_SIZE_AND_OFFSET(ipc_perm, gid);
CHECK_SIZE_AND_OFFSET(ipc_perm, mode);
CHECK_SIZE_AND_OFFSET(ipc_perm, seq);
CHECK_SIZE_AND_OFFSET(ipc_perm, key);

CHECK_TYPE_SIZE(shmid_ds);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_perm);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_segsz);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_atime);
CHECK_SIZE_AND_OFFSET(shmid_ds, __shm_atimensec);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_dtime);
CHECK_SIZE_AND_OFFSET(shmid_ds, __shm_dtimensec);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_ctime);
CHECK_SIZE_AND_OFFSET(shmid_ds, __shm_ctimensec);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_cpid);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_lpid);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_nattch);

CHECK_TYPE_SIZE(clock_t);

CHECK_TYPE_SIZE(ifaddrs);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_next);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_name);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_addr);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_netmask);
// Compare against the union, because we can't reach into the union in a
// compliant way.
#ifdef ifa_dstaddr
#undef ifa_dstaddr
#endif
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_dstaddr);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_data);

CHECK_TYPE_SIZE(passwd);
CHECK_SIZE_AND_OFFSET(passwd, pw_name);
CHECK_SIZE_AND_OFFSET(passwd, pw_passwd);
CHECK_SIZE_AND_OFFSET(passwd, pw_uid);
CHECK_SIZE_AND_OFFSET(passwd, pw_gid);
CHECK_SIZE_AND_OFFSET(passwd, pw_dir);
CHECK_SIZE_AND_OFFSET(passwd, pw_shell);

CHECK_SIZE_AND_OFFSET(passwd, pw_gecos);

CHECK_TYPE_SIZE(group);
CHECK_SIZE_AND_OFFSET(group, gr_name);
CHECK_SIZE_AND_OFFSET(group, gr_passwd);
CHECK_SIZE_AND_OFFSET(group, gr_gid);
CHECK_SIZE_AND_OFFSET(group, gr_mem);

#endif  // SANITIZER_OPENBSD
