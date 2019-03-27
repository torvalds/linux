//===-- sanitizer_platform_limits_posix.cc --------------------------------===//
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
// Sizes and layouts of platform-specific POSIX data structures.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_LINUX || SANITIZER_MAC
// Tests in this file assume that off_t-dependent data structures match the
// libc ABI. For example, struct dirent here is what readdir() function (as
// exported from libc) returns, and not the user-facing "dirent", which
// depends on _FILE_OFFSET_BITS setting.
// To get this "true" dirent definition, we undefine _FILE_OFFSET_BITS below.
#ifdef _FILE_OFFSET_BITS
#undef _FILE_OFFSET_BITS
#endif
#include <arpa/inet.h>
#include <dirent.h>
#include <grp.h>
#include <limits.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <termios.h>
#include <time.h>
#include <wchar.h>
#include <regex.h>
#if !SANITIZER_MAC
#include <utmp.h>
#endif

#if !SANITIZER_IOS
#include <net/route.h>
#endif

#if !SANITIZER_ANDROID
#include <fstab.h>
#include <sys/mount.h>
#include <sys/timeb.h>
#include <utmpx.h>
#endif

#if SANITIZER_LINUX
#include <malloc.h>
#include <mntent.h>
#include <netinet/ether.h>
#include <sys/sysinfo.h>
#include <sys/vt.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <linux/input.h>
#include <linux/ioctl.h>
#include <linux/soundcard.h>
#include <linux/sysctl.h>
#include <linux/utsname.h>
#include <linux/posix_types.h>
#include <net/if_arp.h>
#endif

#if SANITIZER_IOS
#undef IOC_DIRMASK
#endif

#if SANITIZER_LINUX
# include <utime.h>
# include <sys/ptrace.h>
# if defined(__mips64) || defined(__aarch64__) || defined(__arm__)
#  include <asm/ptrace.h>
#  ifdef __arm__
typedef struct user_fpregs elf_fpregset_t;
#   define ARM_VFPREGS_SIZE_ASAN (32 * 8 /*fpregs*/ + 4 /*fpscr*/)
#   if !defined(ARM_VFPREGS_SIZE)
#     define ARM_VFPREGS_SIZE ARM_VFPREGS_SIZE_ASAN
#   endif
#  endif
# endif
# include <semaphore.h>
#endif

#if !SANITIZER_ANDROID
#include <ifaddrs.h>
#include <sys/ucontext.h>
#include <wordexp.h>
#endif

#if SANITIZER_LINUX && !SANITIZER_ANDROID
#include <glob.h>
#include <obstack.h>
#include <mqueue.h>
#include <net/if_ppp.h>
#include <netax25/ax25.h>
#include <netipx/ipx.h>
#include <netrom/netrom.h>
#if HAVE_RPC_XDR_H
# include <rpc/xdr.h>
#endif
#include <scsi/scsi.h>
#include <sys/mtio.h>
#include <sys/kd.h>
#include <sys/shm.h>
#include <sys/statvfs.h>
#include <sys/timex.h>
#if defined(__mips64)
# include <sys/procfs.h>
#endif
#include <sys/user.h>
#include <linux/cyclades.h>
#include <linux/if_eql.h>
#include <linux/if_plip.h>
#include <linux/lp.h>
#include <linux/mroute.h>
#include <linux/mroute6.h>
#include <linux/scc.h>
#include <linux/serial.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#endif // SANITIZER_LINUX && !SANITIZER_ANDROID

#if SANITIZER_ANDROID
#include <linux/kd.h>
#include <linux/mtio.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#endif

#if SANITIZER_LINUX
#include <link.h>
#include <sys/vfs.h>
#include <sys/epoll.h>
#include <linux/capability.h>
#endif // SANITIZER_LINUX

#if SANITIZER_MAC
#include <net/ethernet.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#endif

// Include these after system headers to avoid name clashes and ambiguities.
#include "sanitizer_internal_defs.h"
#include "sanitizer_platform_limits_posix.h"

namespace __sanitizer {
  unsigned struct_utsname_sz = sizeof(struct utsname);
  unsigned struct_stat_sz = sizeof(struct stat);
#if !SANITIZER_IOS
  unsigned struct_stat64_sz = sizeof(struct stat64);
#endif // !SANITIZER_IOS
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
  unsigned struct_sigevent_sz = sizeof(struct sigevent);
  unsigned struct_sched_param_sz = sizeof(struct sched_param);
  unsigned struct_regex_sz = sizeof(regex_t);
  unsigned struct_regmatch_sz = sizeof(regmatch_t);

#if SANITIZER_MAC && !SANITIZER_IOS
  unsigned struct_statfs64_sz = sizeof(struct statfs64);
#endif // SANITIZER_MAC && !SANITIZER_IOS

#if !SANITIZER_ANDROID
  unsigned struct_fstab_sz = sizeof(struct fstab);
  unsigned struct_statfs_sz = sizeof(struct statfs);
  unsigned struct_sockaddr_sz = sizeof(struct sockaddr);
  unsigned ucontext_t_sz = sizeof(ucontext_t);
#endif // !SANITIZER_ANDROID

#if SANITIZER_LINUX
  unsigned struct_epoll_event_sz = sizeof(struct epoll_event);
  unsigned struct_sysinfo_sz = sizeof(struct sysinfo);
  unsigned __user_cap_header_struct_sz =
      sizeof(struct __user_cap_header_struct);
  unsigned __user_cap_data_struct_sz = sizeof(struct __user_cap_data_struct);
  unsigned struct_new_utsname_sz = sizeof(struct new_utsname);
  unsigned struct_old_utsname_sz = sizeof(struct old_utsname);
  unsigned struct_oldold_utsname_sz = sizeof(struct oldold_utsname);
#endif // SANITIZER_LINUX

#if SANITIZER_LINUX
  unsigned struct_rlimit_sz = sizeof(struct rlimit);
  unsigned struct_timespec_sz = sizeof(struct timespec);
  unsigned struct_utimbuf_sz = sizeof(struct utimbuf);
  unsigned struct_itimerspec_sz = sizeof(struct itimerspec);
#endif // SANITIZER_LINUX

#if SANITIZER_LINUX && !SANITIZER_ANDROID
  // Use pre-computed size of struct ustat to avoid <sys/ustat.h> which
  // has been removed from glibc 2.28.
#if defined(__aarch64__) || defined(__s390x__) || defined (__mips64) \
  || defined(__powerpc64__) || defined(__arch64__) || defined(__sparcv9) \
  || defined(__x86_64__)
#define SIZEOF_STRUCT_USTAT 32
#elif defined(__arm__) || defined(__i386__) || defined(__mips__) \
  || defined(__powerpc__) || defined(__s390__) || defined(__sparc__)
#define SIZEOF_STRUCT_USTAT 20
#else
#error Unknown size of struct ustat
#endif
  unsigned struct_ustat_sz = SIZEOF_STRUCT_USTAT;
  unsigned struct_rlimit64_sz = sizeof(struct rlimit64);
  unsigned struct_statvfs64_sz = sizeof(struct statvfs64);
#endif // SANITIZER_LINUX && !SANITIZER_ANDROID

#if SANITIZER_LINUX && !SANITIZER_ANDROID
  unsigned struct_timex_sz = sizeof(struct timex);
  unsigned struct_msqid_ds_sz = sizeof(struct msqid_ds);
  unsigned struct_mq_attr_sz = sizeof(struct mq_attr);
  unsigned struct_statvfs_sz = sizeof(struct statvfs);
#endif // SANITIZER_LINUX && !SANITIZER_ANDROID

  const uptr sig_ign = (uptr)SIG_IGN;
  const uptr sig_dfl = (uptr)SIG_DFL;
  const uptr sig_err = (uptr)SIG_ERR;
  const uptr sa_siginfo = (uptr)SA_SIGINFO;

#if SANITIZER_LINUX
  int e_tabsz = (int)E_TABSZ;
#endif


#if SANITIZER_LINUX && !SANITIZER_ANDROID
  unsigned struct_shminfo_sz = sizeof(struct shminfo);
  unsigned struct_shm_info_sz = sizeof(struct shm_info);
  int shmctl_ipc_stat = (int)IPC_STAT;
  int shmctl_ipc_info = (int)IPC_INFO;
  int shmctl_shm_info = (int)SHM_INFO;
  int shmctl_shm_stat = (int)SHM_STAT;
#endif

#if !SANITIZER_MAC && !SANITIZER_FREEBSD
  unsigned struct_utmp_sz = sizeof(struct utmp);
#endif
#if !SANITIZER_ANDROID
  unsigned struct_utmpx_sz = sizeof(struct utmpx);
#endif

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

#if SANITIZER_LINUX
unsigned struct_ElfW_Phdr_sz = sizeof(ElfW(Phdr));
#elif SANITIZER_FREEBSD
unsigned struct_ElfW_Phdr_sz = sizeof(Elf_Phdr);
#endif

#if SANITIZER_LINUX && !SANITIZER_ANDROID
  int glob_nomatch = GLOB_NOMATCH;
  int glob_altdirfunc = GLOB_ALTDIRFUNC;
#endif

#if SANITIZER_LINUX && !SANITIZER_ANDROID && \
    (defined(__i386) || defined(__x86_64) || defined(__mips64) || \
      defined(__powerpc64__) || defined(__aarch64__) || defined(__arm__) || \
      defined(__s390__))
#if defined(__mips64) || defined(__powerpc64__) || defined(__arm__)
  unsigned struct_user_regs_struct_sz = sizeof(struct pt_regs);
  unsigned struct_user_fpregs_struct_sz = sizeof(elf_fpregset_t);
#elif defined(__aarch64__)
  unsigned struct_user_regs_struct_sz = sizeof(struct user_pt_regs);
  unsigned struct_user_fpregs_struct_sz = sizeof(struct user_fpsimd_state);
#elif defined(__s390__)
  unsigned struct_user_regs_struct_sz = sizeof(struct _user_regs_struct);
  unsigned struct_user_fpregs_struct_sz = sizeof(struct _user_fpregs_struct);
#else
  unsigned struct_user_regs_struct_sz = sizeof(struct user_regs_struct);
  unsigned struct_user_fpregs_struct_sz = sizeof(struct user_fpregs_struct);
#endif // __mips64 || __powerpc64__ || __aarch64__
#if defined(__x86_64) || defined(__mips64) || defined(__powerpc64__) || \
    defined(__aarch64__) || defined(__arm__) || defined(__s390__)
  unsigned struct_user_fpxregs_struct_sz = 0;
#else
  unsigned struct_user_fpxregs_struct_sz = sizeof(struct user_fpxregs_struct);
#endif // __x86_64 || __mips64 || __powerpc64__ || __aarch64__ || __arm__
// || __s390__
#ifdef __arm__
  unsigned struct_user_vfpregs_struct_sz = ARM_VFPREGS_SIZE;
#else
  unsigned struct_user_vfpregs_struct_sz = 0;
#endif

  int ptrace_peektext = PTRACE_PEEKTEXT;
  int ptrace_peekdata = PTRACE_PEEKDATA;
  int ptrace_peekuser = PTRACE_PEEKUSER;
#if (defined(PTRACE_GETREGS) && defined(PTRACE_SETREGS)) || \
    (defined(PT_GETREGS) && defined(PT_SETREGS))
  int ptrace_getregs = PTRACE_GETREGS;
  int ptrace_setregs = PTRACE_SETREGS;
#else
  int ptrace_getregs = -1;
  int ptrace_setregs = -1;
#endif
#if (defined(PTRACE_GETFPREGS) && defined(PTRACE_SETFPREGS)) || \
    (defined(PT_GETFPREGS) && defined(PT_SETFPREGS))
  int ptrace_getfpregs = PTRACE_GETFPREGS;
  int ptrace_setfpregs = PTRACE_SETFPREGS;
#else
  int ptrace_getfpregs = -1;
  int ptrace_setfpregs = -1;
#endif
#if (defined(PTRACE_GETFPXREGS) && defined(PTRACE_SETFPXREGS)) || \
    (defined(PT_GETFPXREGS) && defined(PT_SETFPXREGS))
  int ptrace_getfpxregs = PTRACE_GETFPXREGS;
  int ptrace_setfpxregs = PTRACE_SETFPXREGS;
#else
  int ptrace_getfpxregs = -1;
  int ptrace_setfpxregs = -1;
#endif // PTRACE_GETFPXREGS/PTRACE_SETFPXREGS
#if defined(PTRACE_GETVFPREGS) && defined(PTRACE_SETVFPREGS)
  int ptrace_getvfpregs = PTRACE_GETVFPREGS;
  int ptrace_setvfpregs = PTRACE_SETVFPREGS;
#else
  int ptrace_getvfpregs = -1;
  int ptrace_setvfpregs = -1;
#endif
  int ptrace_geteventmsg = PTRACE_GETEVENTMSG;
#if (defined(PTRACE_GETSIGINFO) && defined(PTRACE_SETSIGINFO)) ||              \
    (defined(PT_GETSIGINFO) && defined(PT_SETSIGINFO))
  int ptrace_getsiginfo = PTRACE_GETSIGINFO;
  int ptrace_setsiginfo = PTRACE_SETSIGINFO;
#else
  int ptrace_getsiginfo = -1;
  int ptrace_setsiginfo = -1;
#endif // PTRACE_GETSIGINFO/PTRACE_SETSIGINFO
#if defined(PTRACE_GETREGSET) && defined(PTRACE_SETREGSET)
  int ptrace_getregset = PTRACE_GETREGSET;
  int ptrace_setregset = PTRACE_SETREGSET;
#else
  int ptrace_getregset = -1;
  int ptrace_setregset = -1;
#endif // PTRACE_GETREGSET/PTRACE_SETREGSET
#endif

  unsigned path_max = PATH_MAX;

  // ioctl arguments
  unsigned struct_ifreq_sz = sizeof(struct ifreq);
  unsigned struct_termios_sz = sizeof(struct termios);
  unsigned struct_winsize_sz = sizeof(struct winsize);

#if SANITIZER_LINUX
  unsigned struct_arpreq_sz = sizeof(struct arpreq);
  unsigned struct_cdrom_msf_sz = sizeof(struct cdrom_msf);
  unsigned struct_cdrom_multisession_sz = sizeof(struct cdrom_multisession);
  unsigned struct_cdrom_read_audio_sz = sizeof(struct cdrom_read_audio);
  unsigned struct_cdrom_subchnl_sz = sizeof(struct cdrom_subchnl);
  unsigned struct_cdrom_ti_sz = sizeof(struct cdrom_ti);
  unsigned struct_cdrom_tocentry_sz = sizeof(struct cdrom_tocentry);
  unsigned struct_cdrom_tochdr_sz = sizeof(struct cdrom_tochdr);
  unsigned struct_cdrom_volctrl_sz = sizeof(struct cdrom_volctrl);
  unsigned struct_ff_effect_sz = sizeof(struct ff_effect);
  unsigned struct_floppy_drive_params_sz = sizeof(struct floppy_drive_params);
  unsigned struct_floppy_drive_struct_sz = sizeof(struct floppy_drive_struct);
  unsigned struct_floppy_fdc_state_sz = sizeof(struct floppy_fdc_state);
  unsigned struct_floppy_max_errors_sz = sizeof(struct floppy_max_errors);
  unsigned struct_floppy_raw_cmd_sz = sizeof(struct floppy_raw_cmd);
  unsigned struct_floppy_struct_sz = sizeof(struct floppy_struct);
  unsigned struct_floppy_write_errors_sz = sizeof(struct floppy_write_errors);
  unsigned struct_format_descr_sz = sizeof(struct format_descr);
  unsigned struct_hd_driveid_sz = sizeof(struct hd_driveid);
  unsigned struct_hd_geometry_sz = sizeof(struct hd_geometry);
  unsigned struct_input_absinfo_sz = sizeof(struct input_absinfo);
  unsigned struct_input_id_sz = sizeof(struct input_id);
  unsigned struct_mtpos_sz = sizeof(struct mtpos);
  unsigned struct_rtentry_sz = sizeof(struct rtentry);
  unsigned struct_termio_sz = sizeof(struct termio);
  unsigned struct_vt_consize_sz = sizeof(struct vt_consize);
  unsigned struct_vt_sizes_sz = sizeof(struct vt_sizes);
  unsigned struct_vt_stat_sz = sizeof(struct vt_stat);
#endif // SANITIZER_LINUX

#if SANITIZER_LINUX
#if SOUND_VERSION >= 0x040000
  unsigned struct_copr_buffer_sz = 0;
  unsigned struct_copr_debug_buf_sz = 0;
  unsigned struct_copr_msg_sz = 0;
#else
  unsigned struct_copr_buffer_sz = sizeof(struct copr_buffer);
  unsigned struct_copr_debug_buf_sz = sizeof(struct copr_debug_buf);
  unsigned struct_copr_msg_sz = sizeof(struct copr_msg);
#endif
  unsigned struct_midi_info_sz = sizeof(struct midi_info);
  unsigned struct_mtget_sz = sizeof(struct mtget);
  unsigned struct_mtop_sz = sizeof(struct mtop);
  unsigned struct_sbi_instrument_sz = sizeof(struct sbi_instrument);
  unsigned struct_seq_event_rec_sz = sizeof(struct seq_event_rec);
  unsigned struct_synth_info_sz = sizeof(struct synth_info);
  unsigned struct_vt_mode_sz = sizeof(struct vt_mode);
#endif // SANITIZER_LINUX

#if SANITIZER_LINUX && !SANITIZER_ANDROID
  unsigned struct_ax25_parms_struct_sz = sizeof(struct ax25_parms_struct);
  unsigned struct_cyclades_monitor_sz = sizeof(struct cyclades_monitor);
#if EV_VERSION > (0x010000)
  unsigned struct_input_keymap_entry_sz = sizeof(struct input_keymap_entry);
#else
  unsigned struct_input_keymap_entry_sz = 0;
#endif
  unsigned struct_ipx_config_data_sz = sizeof(struct ipx_config_data);
  unsigned struct_kbdiacrs_sz = sizeof(struct kbdiacrs);
  unsigned struct_kbentry_sz = sizeof(struct kbentry);
  unsigned struct_kbkeycode_sz = sizeof(struct kbkeycode);
  unsigned struct_kbsentry_sz = sizeof(struct kbsentry);
  unsigned struct_mtconfiginfo_sz = sizeof(struct mtconfiginfo);
  unsigned struct_nr_parms_struct_sz = sizeof(struct nr_parms_struct);
  unsigned struct_scc_modem_sz = sizeof(struct scc_modem);
  unsigned struct_scc_stat_sz = sizeof(struct scc_stat);
  unsigned struct_serial_multiport_struct_sz
      = sizeof(struct serial_multiport_struct);
  unsigned struct_serial_struct_sz = sizeof(struct serial_struct);
  unsigned struct_sockaddr_ax25_sz = sizeof(struct sockaddr_ax25);
  unsigned struct_unimapdesc_sz = sizeof(struct unimapdesc);
  unsigned struct_unimapinit_sz = sizeof(struct unimapinit);
#endif // SANITIZER_LINUX && !SANITIZER_ANDROID

#if SANITIZER_LINUX && !SANITIZER_ANDROID
  unsigned struct_audio_buf_info_sz = sizeof(struct audio_buf_info);
  unsigned struct_ppp_stats_sz = sizeof(struct ppp_stats);
#endif // (SANITIZER_LINUX || SANITIZER_FREEBSD) && !SANITIZER_ANDROID

#if !SANITIZER_ANDROID && !SANITIZER_MAC
  unsigned struct_sioc_sg_req_sz = sizeof(struct sioc_sg_req);
  unsigned struct_sioc_vif_req_sz = sizeof(struct sioc_vif_req);
#endif

  const unsigned long __sanitizer_bufsiz = BUFSIZ;

  const unsigned IOCTL_NOT_PRESENT = 0;

  unsigned IOCTL_FIOASYNC = FIOASYNC;
  unsigned IOCTL_FIOCLEX = FIOCLEX;
  unsigned IOCTL_FIOGETOWN = FIOGETOWN;
  unsigned IOCTL_FIONBIO = FIONBIO;
  unsigned IOCTL_FIONCLEX = FIONCLEX;
  unsigned IOCTL_FIOSETOWN = FIOSETOWN;
  unsigned IOCTL_SIOCADDMULTI = SIOCADDMULTI;
  unsigned IOCTL_SIOCATMARK = SIOCATMARK;
  unsigned IOCTL_SIOCDELMULTI = SIOCDELMULTI;
  unsigned IOCTL_SIOCGIFADDR = SIOCGIFADDR;
  unsigned IOCTL_SIOCGIFBRDADDR = SIOCGIFBRDADDR;
  unsigned IOCTL_SIOCGIFCONF = SIOCGIFCONF;
  unsigned IOCTL_SIOCGIFDSTADDR = SIOCGIFDSTADDR;
  unsigned IOCTL_SIOCGIFFLAGS = SIOCGIFFLAGS;
  unsigned IOCTL_SIOCGIFMETRIC = SIOCGIFMETRIC;
  unsigned IOCTL_SIOCGIFMTU = SIOCGIFMTU;
  unsigned IOCTL_SIOCGIFNETMASK = SIOCGIFNETMASK;
  unsigned IOCTL_SIOCGPGRP = SIOCGPGRP;
  unsigned IOCTL_SIOCSIFADDR = SIOCSIFADDR;
  unsigned IOCTL_SIOCSIFBRDADDR = SIOCSIFBRDADDR;
  unsigned IOCTL_SIOCSIFDSTADDR = SIOCSIFDSTADDR;
  unsigned IOCTL_SIOCSIFFLAGS = SIOCSIFFLAGS;
  unsigned IOCTL_SIOCSIFMETRIC = SIOCSIFMETRIC;
  unsigned IOCTL_SIOCSIFMTU = SIOCSIFMTU;
  unsigned IOCTL_SIOCSIFNETMASK = SIOCSIFNETMASK;
  unsigned IOCTL_SIOCSPGRP = SIOCSPGRP;
  unsigned IOCTL_TIOCCONS = TIOCCONS;
  unsigned IOCTL_TIOCEXCL = TIOCEXCL;
  unsigned IOCTL_TIOCGETD = TIOCGETD;
  unsigned IOCTL_TIOCGPGRP = TIOCGPGRP;
  unsigned IOCTL_TIOCGWINSZ = TIOCGWINSZ;
  unsigned IOCTL_TIOCMBIC = TIOCMBIC;
  unsigned IOCTL_TIOCMBIS = TIOCMBIS;
  unsigned IOCTL_TIOCMGET = TIOCMGET;
  unsigned IOCTL_TIOCMSET = TIOCMSET;
  unsigned IOCTL_TIOCNOTTY = TIOCNOTTY;
  unsigned IOCTL_TIOCNXCL = TIOCNXCL;
  unsigned IOCTL_TIOCOUTQ = TIOCOUTQ;
  unsigned IOCTL_TIOCPKT = TIOCPKT;
  unsigned IOCTL_TIOCSCTTY = TIOCSCTTY;
  unsigned IOCTL_TIOCSETD = TIOCSETD;
  unsigned IOCTL_TIOCSPGRP = TIOCSPGRP;
  unsigned IOCTL_TIOCSTI = TIOCSTI;
  unsigned IOCTL_TIOCSWINSZ = TIOCSWINSZ;
#if SANITIZER_LINUX && !SANITIZER_ANDROID
  unsigned IOCTL_SIOCGETSGCNT = SIOCGETSGCNT;
  unsigned IOCTL_SIOCGETVIFCNT = SIOCGETVIFCNT;
#endif

#if SANITIZER_LINUX
  unsigned IOCTL_EVIOCGABS = EVIOCGABS(0);
  unsigned IOCTL_EVIOCGBIT = EVIOCGBIT(0, 0);
  unsigned IOCTL_EVIOCGEFFECTS = EVIOCGEFFECTS;
  unsigned IOCTL_EVIOCGID = EVIOCGID;
  unsigned IOCTL_EVIOCGKEY = EVIOCGKEY(0);
  unsigned IOCTL_EVIOCGKEYCODE = EVIOCGKEYCODE;
  unsigned IOCTL_EVIOCGLED = EVIOCGLED(0);
  unsigned IOCTL_EVIOCGNAME = EVIOCGNAME(0);
  unsigned IOCTL_EVIOCGPHYS = EVIOCGPHYS(0);
  unsigned IOCTL_EVIOCGRAB = EVIOCGRAB;
  unsigned IOCTL_EVIOCGREP = EVIOCGREP;
  unsigned IOCTL_EVIOCGSND = EVIOCGSND(0);
  unsigned IOCTL_EVIOCGSW = EVIOCGSW(0);
  unsigned IOCTL_EVIOCGUNIQ = EVIOCGUNIQ(0);
  unsigned IOCTL_EVIOCGVERSION = EVIOCGVERSION;
  unsigned IOCTL_EVIOCRMFF = EVIOCRMFF;
  unsigned IOCTL_EVIOCSABS = EVIOCSABS(0);
  unsigned IOCTL_EVIOCSFF = EVIOCSFF;
  unsigned IOCTL_EVIOCSKEYCODE = EVIOCSKEYCODE;
  unsigned IOCTL_EVIOCSREP = EVIOCSREP;
  unsigned IOCTL_BLKFLSBUF = BLKFLSBUF;
  unsigned IOCTL_BLKGETSIZE = BLKGETSIZE;
  unsigned IOCTL_BLKRAGET = BLKRAGET;
  unsigned IOCTL_BLKRASET = BLKRASET;
  unsigned IOCTL_BLKROGET = BLKROGET;
  unsigned IOCTL_BLKROSET = BLKROSET;
  unsigned IOCTL_BLKRRPART = BLKRRPART;
  unsigned IOCTL_CDROMAUDIOBUFSIZ = CDROMAUDIOBUFSIZ;
  unsigned IOCTL_CDROMEJECT = CDROMEJECT;
  unsigned IOCTL_CDROMEJECT_SW = CDROMEJECT_SW;
  unsigned IOCTL_CDROMMULTISESSION = CDROMMULTISESSION;
  unsigned IOCTL_CDROMPAUSE = CDROMPAUSE;
  unsigned IOCTL_CDROMPLAYMSF = CDROMPLAYMSF;
  unsigned IOCTL_CDROMPLAYTRKIND = CDROMPLAYTRKIND;
  unsigned IOCTL_CDROMREADAUDIO = CDROMREADAUDIO;
  unsigned IOCTL_CDROMREADCOOKED = CDROMREADCOOKED;
  unsigned IOCTL_CDROMREADMODE1 = CDROMREADMODE1;
  unsigned IOCTL_CDROMREADMODE2 = CDROMREADMODE2;
  unsigned IOCTL_CDROMREADRAW = CDROMREADRAW;
  unsigned IOCTL_CDROMREADTOCENTRY = CDROMREADTOCENTRY;
  unsigned IOCTL_CDROMREADTOCHDR = CDROMREADTOCHDR;
  unsigned IOCTL_CDROMRESET = CDROMRESET;
  unsigned IOCTL_CDROMRESUME = CDROMRESUME;
  unsigned IOCTL_CDROMSEEK = CDROMSEEK;
  unsigned IOCTL_CDROMSTART = CDROMSTART;
  unsigned IOCTL_CDROMSTOP = CDROMSTOP;
  unsigned IOCTL_CDROMSUBCHNL = CDROMSUBCHNL;
  unsigned IOCTL_CDROMVOLCTRL = CDROMVOLCTRL;
  unsigned IOCTL_CDROMVOLREAD = CDROMVOLREAD;
  unsigned IOCTL_CDROM_GET_UPC = CDROM_GET_UPC;
  unsigned IOCTL_FDCLRPRM = FDCLRPRM;
  unsigned IOCTL_FDDEFPRM = FDDEFPRM;
  unsigned IOCTL_FDFLUSH = FDFLUSH;
  unsigned IOCTL_FDFMTBEG = FDFMTBEG;
  unsigned IOCTL_FDFMTEND = FDFMTEND;
  unsigned IOCTL_FDFMTTRK = FDFMTTRK;
  unsigned IOCTL_FDGETDRVPRM = FDGETDRVPRM;
  unsigned IOCTL_FDGETDRVSTAT = FDGETDRVSTAT;
  unsigned IOCTL_FDGETDRVTYP = FDGETDRVTYP;
  unsigned IOCTL_FDGETFDCSTAT = FDGETFDCSTAT;
  unsigned IOCTL_FDGETMAXERRS = FDGETMAXERRS;
  unsigned IOCTL_FDGETPRM = FDGETPRM;
  unsigned IOCTL_FDMSGOFF = FDMSGOFF;
  unsigned IOCTL_FDMSGON = FDMSGON;
  unsigned IOCTL_FDPOLLDRVSTAT = FDPOLLDRVSTAT;
  unsigned IOCTL_FDRAWCMD = FDRAWCMD;
  unsigned IOCTL_FDRESET = FDRESET;
  unsigned IOCTL_FDSETDRVPRM = FDSETDRVPRM;
  unsigned IOCTL_FDSETEMSGTRESH = FDSETEMSGTRESH;
  unsigned IOCTL_FDSETMAXERRS = FDSETMAXERRS;
  unsigned IOCTL_FDSETPRM = FDSETPRM;
  unsigned IOCTL_FDTWADDLE = FDTWADDLE;
  unsigned IOCTL_FDWERRORCLR = FDWERRORCLR;
  unsigned IOCTL_FDWERRORGET = FDWERRORGET;
  unsigned IOCTL_HDIO_DRIVE_CMD = HDIO_DRIVE_CMD;
  unsigned IOCTL_HDIO_GETGEO = HDIO_GETGEO;
  unsigned IOCTL_HDIO_GET_32BIT = HDIO_GET_32BIT;
  unsigned IOCTL_HDIO_GET_DMA = HDIO_GET_DMA;
  unsigned IOCTL_HDIO_GET_IDENTITY = HDIO_GET_IDENTITY;
  unsigned IOCTL_HDIO_GET_KEEPSETTINGS = HDIO_GET_KEEPSETTINGS;
  unsigned IOCTL_HDIO_GET_MULTCOUNT = HDIO_GET_MULTCOUNT;
  unsigned IOCTL_HDIO_GET_NOWERR = HDIO_GET_NOWERR;
  unsigned IOCTL_HDIO_GET_UNMASKINTR = HDIO_GET_UNMASKINTR;
  unsigned IOCTL_HDIO_SET_32BIT = HDIO_SET_32BIT;
  unsigned IOCTL_HDIO_SET_DMA = HDIO_SET_DMA;
  unsigned IOCTL_HDIO_SET_KEEPSETTINGS = HDIO_SET_KEEPSETTINGS;
  unsigned IOCTL_HDIO_SET_MULTCOUNT = HDIO_SET_MULTCOUNT;
  unsigned IOCTL_HDIO_SET_NOWERR = HDIO_SET_NOWERR;
  unsigned IOCTL_HDIO_SET_UNMASKINTR = HDIO_SET_UNMASKINTR;
  unsigned IOCTL_MTIOCPOS = MTIOCPOS;
  unsigned IOCTL_PPPIOCGASYNCMAP = PPPIOCGASYNCMAP;
  unsigned IOCTL_PPPIOCGDEBUG = PPPIOCGDEBUG;
  unsigned IOCTL_PPPIOCGFLAGS = PPPIOCGFLAGS;
  unsigned IOCTL_PPPIOCGUNIT = PPPIOCGUNIT;
  unsigned IOCTL_PPPIOCGXASYNCMAP = PPPIOCGXASYNCMAP;
  unsigned IOCTL_PPPIOCSASYNCMAP = PPPIOCSASYNCMAP;
  unsigned IOCTL_PPPIOCSDEBUG = PPPIOCSDEBUG;
  unsigned IOCTL_PPPIOCSFLAGS = PPPIOCSFLAGS;
  unsigned IOCTL_PPPIOCSMAXCID = PPPIOCSMAXCID;
  unsigned IOCTL_PPPIOCSMRU = PPPIOCSMRU;
  unsigned IOCTL_PPPIOCSXASYNCMAP = PPPIOCSXASYNCMAP;
  unsigned IOCTL_SIOCADDRT = SIOCADDRT;
  unsigned IOCTL_SIOCDARP = SIOCDARP;
  unsigned IOCTL_SIOCDELRT = SIOCDELRT;
  unsigned IOCTL_SIOCDRARP = SIOCDRARP;
  unsigned IOCTL_SIOCGARP = SIOCGARP;
  unsigned IOCTL_SIOCGIFENCAP = SIOCGIFENCAP;
  unsigned IOCTL_SIOCGIFHWADDR = SIOCGIFHWADDR;
  unsigned IOCTL_SIOCGIFMAP = SIOCGIFMAP;
  unsigned IOCTL_SIOCGIFMEM = SIOCGIFMEM;
  unsigned IOCTL_SIOCGIFNAME = SIOCGIFNAME;
  unsigned IOCTL_SIOCGIFSLAVE = SIOCGIFSLAVE;
  unsigned IOCTL_SIOCGRARP = SIOCGRARP;
  unsigned IOCTL_SIOCGSTAMP = SIOCGSTAMP;
  unsigned IOCTL_SIOCSARP = SIOCSARP;
  unsigned IOCTL_SIOCSIFENCAP = SIOCSIFENCAP;
  unsigned IOCTL_SIOCSIFHWADDR = SIOCSIFHWADDR;
  unsigned IOCTL_SIOCSIFLINK = SIOCSIFLINK;
  unsigned IOCTL_SIOCSIFMAP = SIOCSIFMAP;
  unsigned IOCTL_SIOCSIFMEM = SIOCSIFMEM;
  unsigned IOCTL_SIOCSIFSLAVE = SIOCSIFSLAVE;
  unsigned IOCTL_SIOCSRARP = SIOCSRARP;
# if SOUND_VERSION >= 0x040000
  unsigned IOCTL_SNDCTL_COPR_HALT = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SNDCTL_COPR_LOAD = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SNDCTL_COPR_RCODE = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SNDCTL_COPR_RCVMSG = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SNDCTL_COPR_RDATA = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SNDCTL_COPR_RESET = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SNDCTL_COPR_RUN = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SNDCTL_COPR_SENDMSG = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SNDCTL_COPR_WCODE = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SNDCTL_COPR_WDATA = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SOUND_PCM_READ_BITS = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SOUND_PCM_READ_CHANNELS = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SOUND_PCM_READ_FILTER = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SOUND_PCM_READ_RATE = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SOUND_PCM_WRITE_CHANNELS = IOCTL_NOT_PRESENT;
  unsigned IOCTL_SOUND_PCM_WRITE_FILTER = IOCTL_NOT_PRESENT;
# else  // SOUND_VERSION
  unsigned IOCTL_SNDCTL_COPR_HALT = SNDCTL_COPR_HALT;
  unsigned IOCTL_SNDCTL_COPR_LOAD = SNDCTL_COPR_LOAD;
  unsigned IOCTL_SNDCTL_COPR_RCODE = SNDCTL_COPR_RCODE;
  unsigned IOCTL_SNDCTL_COPR_RCVMSG = SNDCTL_COPR_RCVMSG;
  unsigned IOCTL_SNDCTL_COPR_RDATA = SNDCTL_COPR_RDATA;
  unsigned IOCTL_SNDCTL_COPR_RESET = SNDCTL_COPR_RESET;
  unsigned IOCTL_SNDCTL_COPR_RUN = SNDCTL_COPR_RUN;
  unsigned IOCTL_SNDCTL_COPR_SENDMSG = SNDCTL_COPR_SENDMSG;
  unsigned IOCTL_SNDCTL_COPR_WCODE = SNDCTL_COPR_WCODE;
  unsigned IOCTL_SNDCTL_COPR_WDATA = SNDCTL_COPR_WDATA;
  unsigned IOCTL_SOUND_PCM_READ_BITS = SOUND_PCM_READ_BITS;
  unsigned IOCTL_SOUND_PCM_READ_CHANNELS = SOUND_PCM_READ_CHANNELS;
  unsigned IOCTL_SOUND_PCM_READ_FILTER = SOUND_PCM_READ_FILTER;
  unsigned IOCTL_SOUND_PCM_READ_RATE = SOUND_PCM_READ_RATE;
  unsigned IOCTL_SOUND_PCM_WRITE_CHANNELS = SOUND_PCM_WRITE_CHANNELS;
  unsigned IOCTL_SOUND_PCM_WRITE_FILTER = SOUND_PCM_WRITE_FILTER;
#endif // SOUND_VERSION
  unsigned IOCTL_TCFLSH = TCFLSH;
  unsigned IOCTL_TCGETA = TCGETA;
  unsigned IOCTL_TCGETS = TCGETS;
  unsigned IOCTL_TCSBRK = TCSBRK;
  unsigned IOCTL_TCSBRKP = TCSBRKP;
  unsigned IOCTL_TCSETA = TCSETA;
  unsigned IOCTL_TCSETAF = TCSETAF;
  unsigned IOCTL_TCSETAW = TCSETAW;
  unsigned IOCTL_TCSETS = TCSETS;
  unsigned IOCTL_TCSETSF = TCSETSF;
  unsigned IOCTL_TCSETSW = TCSETSW;
  unsigned IOCTL_TCXONC = TCXONC;
  unsigned IOCTL_TIOCGLCKTRMIOS = TIOCGLCKTRMIOS;
  unsigned IOCTL_TIOCGSOFTCAR = TIOCGSOFTCAR;
  unsigned IOCTL_TIOCINQ = TIOCINQ;
  unsigned IOCTL_TIOCLINUX = TIOCLINUX;
  unsigned IOCTL_TIOCSERCONFIG = TIOCSERCONFIG;
  unsigned IOCTL_TIOCSERGETLSR = TIOCSERGETLSR;
  unsigned IOCTL_TIOCSERGWILD = TIOCSERGWILD;
  unsigned IOCTL_TIOCSERSWILD = TIOCSERSWILD;
  unsigned IOCTL_TIOCSLCKTRMIOS = TIOCSLCKTRMIOS;
  unsigned IOCTL_TIOCSSOFTCAR = TIOCSSOFTCAR;
  unsigned IOCTL_VT_DISALLOCATE = VT_DISALLOCATE;
  unsigned IOCTL_VT_GETSTATE = VT_GETSTATE;
  unsigned IOCTL_VT_RESIZE = VT_RESIZE;
  unsigned IOCTL_VT_RESIZEX = VT_RESIZEX;
  unsigned IOCTL_VT_SENDSIG = VT_SENDSIG;
  unsigned IOCTL_MTIOCGET = MTIOCGET;
  unsigned IOCTL_MTIOCTOP = MTIOCTOP;
  unsigned IOCTL_SNDCTL_DSP_GETBLKSIZE = SNDCTL_DSP_GETBLKSIZE;
  unsigned IOCTL_SNDCTL_DSP_GETFMTS = SNDCTL_DSP_GETFMTS;
  unsigned IOCTL_SNDCTL_DSP_NONBLOCK = SNDCTL_DSP_NONBLOCK;
  unsigned IOCTL_SNDCTL_DSP_POST = SNDCTL_DSP_POST;
  unsigned IOCTL_SNDCTL_DSP_RESET = SNDCTL_DSP_RESET;
  unsigned IOCTL_SNDCTL_DSP_SETFMT = SNDCTL_DSP_SETFMT;
  unsigned IOCTL_SNDCTL_DSP_SETFRAGMENT = SNDCTL_DSP_SETFRAGMENT;
  unsigned IOCTL_SNDCTL_DSP_SPEED = SNDCTL_DSP_SPEED;
  unsigned IOCTL_SNDCTL_DSP_STEREO = SNDCTL_DSP_STEREO;
  unsigned IOCTL_SNDCTL_DSP_SUBDIVIDE = SNDCTL_DSP_SUBDIVIDE;
  unsigned IOCTL_SNDCTL_DSP_SYNC = SNDCTL_DSP_SYNC;
  unsigned IOCTL_SNDCTL_FM_4OP_ENABLE = SNDCTL_FM_4OP_ENABLE;
  unsigned IOCTL_SNDCTL_FM_LOAD_INSTR = SNDCTL_FM_LOAD_INSTR;
  unsigned IOCTL_SNDCTL_MIDI_INFO = SNDCTL_MIDI_INFO;
  unsigned IOCTL_SNDCTL_MIDI_PRETIME = SNDCTL_MIDI_PRETIME;
  unsigned IOCTL_SNDCTL_SEQ_CTRLRATE = SNDCTL_SEQ_CTRLRATE;
  unsigned IOCTL_SNDCTL_SEQ_GETINCOUNT = SNDCTL_SEQ_GETINCOUNT;
  unsigned IOCTL_SNDCTL_SEQ_GETOUTCOUNT = SNDCTL_SEQ_GETOUTCOUNT;
  unsigned IOCTL_SNDCTL_SEQ_NRMIDIS = SNDCTL_SEQ_NRMIDIS;
  unsigned IOCTL_SNDCTL_SEQ_NRSYNTHS = SNDCTL_SEQ_NRSYNTHS;
  unsigned IOCTL_SNDCTL_SEQ_OUTOFBAND = SNDCTL_SEQ_OUTOFBAND;
  unsigned IOCTL_SNDCTL_SEQ_PANIC = SNDCTL_SEQ_PANIC;
  unsigned IOCTL_SNDCTL_SEQ_PERCMODE = SNDCTL_SEQ_PERCMODE;
  unsigned IOCTL_SNDCTL_SEQ_RESET = SNDCTL_SEQ_RESET;
  unsigned IOCTL_SNDCTL_SEQ_RESETSAMPLES = SNDCTL_SEQ_RESETSAMPLES;
  unsigned IOCTL_SNDCTL_SEQ_SYNC = SNDCTL_SEQ_SYNC;
  unsigned IOCTL_SNDCTL_SEQ_TESTMIDI = SNDCTL_SEQ_TESTMIDI;
  unsigned IOCTL_SNDCTL_SEQ_THRESHOLD = SNDCTL_SEQ_THRESHOLD;
  unsigned IOCTL_SNDCTL_SYNTH_INFO = SNDCTL_SYNTH_INFO;
  unsigned IOCTL_SNDCTL_SYNTH_MEMAVL = SNDCTL_SYNTH_MEMAVL;
  unsigned IOCTL_SNDCTL_TMR_CONTINUE = SNDCTL_TMR_CONTINUE;
  unsigned IOCTL_SNDCTL_TMR_METRONOME = SNDCTL_TMR_METRONOME;
  unsigned IOCTL_SNDCTL_TMR_SELECT = SNDCTL_TMR_SELECT;
  unsigned IOCTL_SNDCTL_TMR_SOURCE = SNDCTL_TMR_SOURCE;
  unsigned IOCTL_SNDCTL_TMR_START = SNDCTL_TMR_START;
  unsigned IOCTL_SNDCTL_TMR_STOP = SNDCTL_TMR_STOP;
  unsigned IOCTL_SNDCTL_TMR_TEMPO = SNDCTL_TMR_TEMPO;
  unsigned IOCTL_SNDCTL_TMR_TIMEBASE = SNDCTL_TMR_TIMEBASE;
  unsigned IOCTL_SOUND_MIXER_READ_ALTPCM = SOUND_MIXER_READ_ALTPCM;
  unsigned IOCTL_SOUND_MIXER_READ_BASS = SOUND_MIXER_READ_BASS;
  unsigned IOCTL_SOUND_MIXER_READ_CAPS = SOUND_MIXER_READ_CAPS;
  unsigned IOCTL_SOUND_MIXER_READ_CD = SOUND_MIXER_READ_CD;
  unsigned IOCTL_SOUND_MIXER_READ_DEVMASK = SOUND_MIXER_READ_DEVMASK;
  unsigned IOCTL_SOUND_MIXER_READ_ENHANCE = SOUND_MIXER_READ_ENHANCE;
  unsigned IOCTL_SOUND_MIXER_READ_IGAIN = SOUND_MIXER_READ_IGAIN;
  unsigned IOCTL_SOUND_MIXER_READ_IMIX = SOUND_MIXER_READ_IMIX;
  unsigned IOCTL_SOUND_MIXER_READ_LINE = SOUND_MIXER_READ_LINE;
  unsigned IOCTL_SOUND_MIXER_READ_LINE1 = SOUND_MIXER_READ_LINE1;
  unsigned IOCTL_SOUND_MIXER_READ_LINE2 = SOUND_MIXER_READ_LINE2;
  unsigned IOCTL_SOUND_MIXER_READ_LINE3 = SOUND_MIXER_READ_LINE3;
  unsigned IOCTL_SOUND_MIXER_READ_LOUD = SOUND_MIXER_READ_LOUD;
  unsigned IOCTL_SOUND_MIXER_READ_MIC = SOUND_MIXER_READ_MIC;
  unsigned IOCTL_SOUND_MIXER_READ_MUTE = SOUND_MIXER_READ_MUTE;
  unsigned IOCTL_SOUND_MIXER_READ_OGAIN = SOUND_MIXER_READ_OGAIN;
  unsigned IOCTL_SOUND_MIXER_READ_PCM = SOUND_MIXER_READ_PCM;
  unsigned IOCTL_SOUND_MIXER_READ_RECLEV = SOUND_MIXER_READ_RECLEV;
  unsigned IOCTL_SOUND_MIXER_READ_RECMASK = SOUND_MIXER_READ_RECMASK;
  unsigned IOCTL_SOUND_MIXER_READ_RECSRC = SOUND_MIXER_READ_RECSRC;
  unsigned IOCTL_SOUND_MIXER_READ_SPEAKER = SOUND_MIXER_READ_SPEAKER;
  unsigned IOCTL_SOUND_MIXER_READ_STEREODEVS = SOUND_MIXER_READ_STEREODEVS;
  unsigned IOCTL_SOUND_MIXER_READ_SYNTH = SOUND_MIXER_READ_SYNTH;
  unsigned IOCTL_SOUND_MIXER_READ_TREBLE = SOUND_MIXER_READ_TREBLE;
  unsigned IOCTL_SOUND_MIXER_READ_VOLUME = SOUND_MIXER_READ_VOLUME;
  unsigned IOCTL_SOUND_MIXER_WRITE_ALTPCM = SOUND_MIXER_WRITE_ALTPCM;
  unsigned IOCTL_SOUND_MIXER_WRITE_BASS = SOUND_MIXER_WRITE_BASS;
  unsigned IOCTL_SOUND_MIXER_WRITE_CD = SOUND_MIXER_WRITE_CD;
  unsigned IOCTL_SOUND_MIXER_WRITE_ENHANCE = SOUND_MIXER_WRITE_ENHANCE;
  unsigned IOCTL_SOUND_MIXER_WRITE_IGAIN = SOUND_MIXER_WRITE_IGAIN;
  unsigned IOCTL_SOUND_MIXER_WRITE_IMIX = SOUND_MIXER_WRITE_IMIX;
  unsigned IOCTL_SOUND_MIXER_WRITE_LINE = SOUND_MIXER_WRITE_LINE;
  unsigned IOCTL_SOUND_MIXER_WRITE_LINE1 = SOUND_MIXER_WRITE_LINE1;
  unsigned IOCTL_SOUND_MIXER_WRITE_LINE2 = SOUND_MIXER_WRITE_LINE2;
  unsigned IOCTL_SOUND_MIXER_WRITE_LINE3 = SOUND_MIXER_WRITE_LINE3;
  unsigned IOCTL_SOUND_MIXER_WRITE_LOUD = SOUND_MIXER_WRITE_LOUD;
  unsigned IOCTL_SOUND_MIXER_WRITE_MIC = SOUND_MIXER_WRITE_MIC;
  unsigned IOCTL_SOUND_MIXER_WRITE_MUTE = SOUND_MIXER_WRITE_MUTE;
  unsigned IOCTL_SOUND_MIXER_WRITE_OGAIN = SOUND_MIXER_WRITE_OGAIN;
  unsigned IOCTL_SOUND_MIXER_WRITE_PCM = SOUND_MIXER_WRITE_PCM;
  unsigned IOCTL_SOUND_MIXER_WRITE_RECLEV = SOUND_MIXER_WRITE_RECLEV;
  unsigned IOCTL_SOUND_MIXER_WRITE_RECSRC = SOUND_MIXER_WRITE_RECSRC;
  unsigned IOCTL_SOUND_MIXER_WRITE_SPEAKER = SOUND_MIXER_WRITE_SPEAKER;
  unsigned IOCTL_SOUND_MIXER_WRITE_SYNTH = SOUND_MIXER_WRITE_SYNTH;
  unsigned IOCTL_SOUND_MIXER_WRITE_TREBLE = SOUND_MIXER_WRITE_TREBLE;
  unsigned IOCTL_SOUND_MIXER_WRITE_VOLUME = SOUND_MIXER_WRITE_VOLUME;
  unsigned IOCTL_VT_ACTIVATE = VT_ACTIVATE;
  unsigned IOCTL_VT_GETMODE = VT_GETMODE;
  unsigned IOCTL_VT_OPENQRY = VT_OPENQRY;
  unsigned IOCTL_VT_RELDISP = VT_RELDISP;
  unsigned IOCTL_VT_SETMODE = VT_SETMODE;
  unsigned IOCTL_VT_WAITACTIVE = VT_WAITACTIVE;
#endif // SANITIZER_LINUX

#if SANITIZER_LINUX && !SANITIZER_ANDROID
  unsigned IOCTL_CYGETDEFTHRESH = CYGETDEFTHRESH;
  unsigned IOCTL_CYGETDEFTIMEOUT = CYGETDEFTIMEOUT;
  unsigned IOCTL_CYGETMON = CYGETMON;
  unsigned IOCTL_CYGETTHRESH = CYGETTHRESH;
  unsigned IOCTL_CYGETTIMEOUT = CYGETTIMEOUT;
  unsigned IOCTL_CYSETDEFTHRESH = CYSETDEFTHRESH;
  unsigned IOCTL_CYSETDEFTIMEOUT = CYSETDEFTIMEOUT;
  unsigned IOCTL_CYSETTHRESH = CYSETTHRESH;
  unsigned IOCTL_CYSETTIMEOUT = CYSETTIMEOUT;
  unsigned IOCTL_EQL_EMANCIPATE = EQL_EMANCIPATE;
  unsigned IOCTL_EQL_ENSLAVE = EQL_ENSLAVE;
  unsigned IOCTL_EQL_GETMASTRCFG = EQL_GETMASTRCFG;
  unsigned IOCTL_EQL_GETSLAVECFG = EQL_GETSLAVECFG;
  unsigned IOCTL_EQL_SETMASTRCFG = EQL_SETMASTRCFG;
  unsigned IOCTL_EQL_SETSLAVECFG = EQL_SETSLAVECFG;
#if EV_VERSION > (0x010000)
  unsigned IOCTL_EVIOCGKEYCODE_V2 = EVIOCGKEYCODE_V2;
  unsigned IOCTL_EVIOCGPROP = EVIOCGPROP(0);
  unsigned IOCTL_EVIOCSKEYCODE_V2 = EVIOCSKEYCODE_V2;
#else
  unsigned IOCTL_EVIOCGKEYCODE_V2 = IOCTL_NOT_PRESENT;
  unsigned IOCTL_EVIOCGPROP = IOCTL_NOT_PRESENT;
  unsigned IOCTL_EVIOCSKEYCODE_V2 = IOCTL_NOT_PRESENT;
#endif
  unsigned IOCTL_FS_IOC_GETFLAGS = FS_IOC_GETFLAGS;
  unsigned IOCTL_FS_IOC_GETVERSION = FS_IOC_GETVERSION;
  unsigned IOCTL_FS_IOC_SETFLAGS = FS_IOC_SETFLAGS;
  unsigned IOCTL_FS_IOC_SETVERSION = FS_IOC_SETVERSION;
  unsigned IOCTL_GIO_CMAP = GIO_CMAP;
  unsigned IOCTL_GIO_FONT = GIO_FONT;
  unsigned IOCTL_GIO_UNIMAP = GIO_UNIMAP;
  unsigned IOCTL_GIO_UNISCRNMAP = GIO_UNISCRNMAP;
  unsigned IOCTL_KDADDIO = KDADDIO;
  unsigned IOCTL_KDDELIO = KDDELIO;
  unsigned IOCTL_KDGETKEYCODE = KDGETKEYCODE;
  unsigned IOCTL_KDGKBDIACR = KDGKBDIACR;
  unsigned IOCTL_KDGKBENT = KDGKBENT;
  unsigned IOCTL_KDGKBLED = KDGKBLED;
  unsigned IOCTL_KDGKBMETA = KDGKBMETA;
  unsigned IOCTL_KDGKBSENT = KDGKBSENT;
  unsigned IOCTL_KDMAPDISP = KDMAPDISP;
  unsigned IOCTL_KDSETKEYCODE = KDSETKEYCODE;
  unsigned IOCTL_KDSIGACCEPT = KDSIGACCEPT;
  unsigned IOCTL_KDSKBDIACR = KDSKBDIACR;
  unsigned IOCTL_KDSKBENT = KDSKBENT;
  unsigned IOCTL_KDSKBLED = KDSKBLED;
  unsigned IOCTL_KDSKBMETA = KDSKBMETA;
  unsigned IOCTL_KDSKBSENT = KDSKBSENT;
  unsigned IOCTL_KDUNMAPDISP = KDUNMAPDISP;
  unsigned IOCTL_LPABORT = LPABORT;
  unsigned IOCTL_LPABORTOPEN = LPABORTOPEN;
  unsigned IOCTL_LPCAREFUL = LPCAREFUL;
  unsigned IOCTL_LPCHAR = LPCHAR;
  unsigned IOCTL_LPGETIRQ = LPGETIRQ;
  unsigned IOCTL_LPGETSTATUS = LPGETSTATUS;
  unsigned IOCTL_LPRESET = LPRESET;
  unsigned IOCTL_LPSETIRQ = LPSETIRQ;
  unsigned IOCTL_LPTIME = LPTIME;
  unsigned IOCTL_LPWAIT = LPWAIT;
  unsigned IOCTL_MTIOCGETCONFIG = MTIOCGETCONFIG;
  unsigned IOCTL_MTIOCSETCONFIG = MTIOCSETCONFIG;
  unsigned IOCTL_PIO_CMAP = PIO_CMAP;
  unsigned IOCTL_PIO_FONT = PIO_FONT;
  unsigned IOCTL_PIO_UNIMAP = PIO_UNIMAP;
  unsigned IOCTL_PIO_UNIMAPCLR = PIO_UNIMAPCLR;
  unsigned IOCTL_PIO_UNISCRNMAP = PIO_UNISCRNMAP;
  unsigned IOCTL_SCSI_IOCTL_GET_IDLUN = SCSI_IOCTL_GET_IDLUN;
  unsigned IOCTL_SCSI_IOCTL_PROBE_HOST = SCSI_IOCTL_PROBE_HOST;
  unsigned IOCTL_SCSI_IOCTL_TAGGED_DISABLE = SCSI_IOCTL_TAGGED_DISABLE;
  unsigned IOCTL_SCSI_IOCTL_TAGGED_ENABLE = SCSI_IOCTL_TAGGED_ENABLE;
  unsigned IOCTL_SIOCAIPXITFCRT = SIOCAIPXITFCRT;
  unsigned IOCTL_SIOCAIPXPRISLT = SIOCAIPXPRISLT;
  unsigned IOCTL_SIOCAX25ADDUID = SIOCAX25ADDUID;
  unsigned IOCTL_SIOCAX25DELUID = SIOCAX25DELUID;
  unsigned IOCTL_SIOCAX25GETPARMS = SIOCAX25GETPARMS;
  unsigned IOCTL_SIOCAX25GETUID = SIOCAX25GETUID;
  unsigned IOCTL_SIOCAX25NOUID = SIOCAX25NOUID;
  unsigned IOCTL_SIOCAX25SETPARMS = SIOCAX25SETPARMS;
  unsigned IOCTL_SIOCDEVPLIP = SIOCDEVPLIP;
  unsigned IOCTL_SIOCIPXCFGDATA = SIOCIPXCFGDATA;
  unsigned IOCTL_SIOCNRDECOBS = SIOCNRDECOBS;
  unsigned IOCTL_SIOCNRGETPARMS = SIOCNRGETPARMS;
  unsigned IOCTL_SIOCNRRTCTL = SIOCNRRTCTL;
  unsigned IOCTL_SIOCNRSETPARMS = SIOCNRSETPARMS;
  unsigned IOCTL_TIOCGSERIAL = TIOCGSERIAL;
  unsigned IOCTL_TIOCSERGETMULTI = TIOCSERGETMULTI;
  unsigned IOCTL_TIOCSERSETMULTI = TIOCSERSETMULTI;
  unsigned IOCTL_TIOCSSERIAL = TIOCSSERIAL;
#endif // SANITIZER_LINUX && !SANITIZER_ANDROID

#if SANITIZER_LINUX && !SANITIZER_ANDROID
  unsigned IOCTL_GIO_SCRNMAP = GIO_SCRNMAP;
  unsigned IOCTL_KDDISABIO = KDDISABIO;
  unsigned IOCTL_KDENABIO = KDENABIO;
  unsigned IOCTL_KDGETLED = KDGETLED;
  unsigned IOCTL_KDGETMODE = KDGETMODE;
  unsigned IOCTL_KDGKBMODE = KDGKBMODE;
  unsigned IOCTL_KDGKBTYPE = KDGKBTYPE;
  unsigned IOCTL_KDMKTONE = KDMKTONE;
  unsigned IOCTL_KDSETLED = KDSETLED;
  unsigned IOCTL_KDSETMODE = KDSETMODE;
  unsigned IOCTL_KDSKBMODE = KDSKBMODE;
  unsigned IOCTL_KIOCSOUND = KIOCSOUND;
  unsigned IOCTL_PIO_SCRNMAP = PIO_SCRNMAP;
  unsigned IOCTL_SNDCTL_DSP_GETISPACE = SNDCTL_DSP_GETISPACE;
  unsigned IOCTL_SNDCTL_DSP_GETOSPACE = SNDCTL_DSP_GETOSPACE;
#endif // (SANITIZER_LINUX || SANITIZER_FREEBSD) && !SANITIZER_ANDROID

  const int si_SEGV_MAPERR = SEGV_MAPERR;
  const int si_SEGV_ACCERR = SEGV_ACCERR;
} // namespace __sanitizer

using namespace __sanitizer;

COMPILER_CHECK(sizeof(__sanitizer_pthread_attr_t) >= sizeof(pthread_attr_t));

COMPILER_CHECK(sizeof(socklen_t) == sizeof(unsigned));
CHECK_TYPE_SIZE(pthread_key_t);

#if SANITIZER_LINUX
// FIXME: We define those on Linux and Mac, but only check on Linux.
COMPILER_CHECK(IOC_NRBITS == _IOC_NRBITS);
COMPILER_CHECK(IOC_TYPEBITS == _IOC_TYPEBITS);
COMPILER_CHECK(IOC_SIZEBITS == _IOC_SIZEBITS);
COMPILER_CHECK(IOC_DIRBITS == _IOC_DIRBITS);
COMPILER_CHECK(IOC_NRMASK == _IOC_NRMASK);
COMPILER_CHECK(IOC_TYPEMASK == _IOC_TYPEMASK);
COMPILER_CHECK(IOC_SIZEMASK == _IOC_SIZEMASK);
COMPILER_CHECK(IOC_DIRMASK == _IOC_DIRMASK);
COMPILER_CHECK(IOC_NRSHIFT == _IOC_NRSHIFT);
COMPILER_CHECK(IOC_TYPESHIFT == _IOC_TYPESHIFT);
COMPILER_CHECK(IOC_SIZESHIFT == _IOC_SIZESHIFT);
COMPILER_CHECK(IOC_DIRSHIFT == _IOC_DIRSHIFT);
COMPILER_CHECK(IOC_NONE == _IOC_NONE);
COMPILER_CHECK(IOC_WRITE == _IOC_WRITE);
COMPILER_CHECK(IOC_READ == _IOC_READ);
COMPILER_CHECK(EVIOC_ABS_MAX == ABS_MAX);
COMPILER_CHECK(EVIOC_EV_MAX == EV_MAX);
COMPILER_CHECK(IOC_SIZE(0x12345678) == _IOC_SIZE(0x12345678));
COMPILER_CHECK(IOC_DIR(0x12345678) == _IOC_DIR(0x12345678));
COMPILER_CHECK(IOC_NR(0x12345678) == _IOC_NR(0x12345678));
COMPILER_CHECK(IOC_TYPE(0x12345678) == _IOC_TYPE(0x12345678));
#endif // SANITIZER_LINUX

#if SANITIZER_LINUX || SANITIZER_FREEBSD
// There are more undocumented fields in dl_phdr_info that we are not interested
// in.
COMPILER_CHECK(sizeof(__sanitizer_dl_phdr_info) <= sizeof(dl_phdr_info));
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_addr);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_name);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_phdr);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_phnum);
#endif // SANITIZER_LINUX || SANITIZER_FREEBSD

#if (SANITIZER_LINUX || SANITIZER_FREEBSD) && !SANITIZER_ANDROID
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
#endif

CHECK_TYPE_SIZE(addrinfo);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_flags);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_family);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_socktype);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_protocol);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_protocol);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_addrlen);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_canonname);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_addr);

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

#ifndef __GLIBC_PREREQ
#define __GLIBC_PREREQ(x, y) 0
#endif

#if SANITIZER_LINUX && (__ANDROID_API__ >= 21 || __GLIBC_PREREQ (2, 14))
CHECK_TYPE_SIZE(mmsghdr);
CHECK_SIZE_AND_OFFSET(mmsghdr, msg_hdr);
CHECK_SIZE_AND_OFFSET(mmsghdr, msg_len);
#endif

COMPILER_CHECK(sizeof(__sanitizer_dirent) <= sizeof(dirent));
CHECK_SIZE_AND_OFFSET(dirent, d_ino);
#if SANITIZER_MAC
CHECK_SIZE_AND_OFFSET(dirent, d_seekoff);
#elif SANITIZER_FREEBSD
// There is no 'd_off' field on FreeBSD.
#else
CHECK_SIZE_AND_OFFSET(dirent, d_off);
#endif
CHECK_SIZE_AND_OFFSET(dirent, d_reclen);

#if SANITIZER_LINUX && !SANITIZER_ANDROID
COMPILER_CHECK(sizeof(__sanitizer_dirent64) <= sizeof(dirent64));
CHECK_SIZE_AND_OFFSET(dirent64, d_ino);
CHECK_SIZE_AND_OFFSET(dirent64, d_off);
CHECK_SIZE_AND_OFFSET(dirent64, d_reclen);
#endif

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
#if !defined(__s390x__) || __GLIBC_PREREQ (2, 20)
// On s390x glibc 2.19 and earlier sa_flags was unsigned long, and sa_resv
// didn't exist.
CHECK_STRUCT_SIZE_AND_OFFSET(sigaction, sa_flags);
#endif
#if SANITIZER_LINUX && (!SANITIZER_ANDROID || !SANITIZER_MIPS32)
CHECK_STRUCT_SIZE_AND_OFFSET(sigaction, sa_restorer);
#endif

#if SANITIZER_LINUX
CHECK_TYPE_SIZE(__sysctl_args);
CHECK_SIZE_AND_OFFSET(__sysctl_args, name);
CHECK_SIZE_AND_OFFSET(__sysctl_args, nlen);
CHECK_SIZE_AND_OFFSET(__sysctl_args, oldval);
CHECK_SIZE_AND_OFFSET(__sysctl_args, oldlenp);
CHECK_SIZE_AND_OFFSET(__sysctl_args, newval);
CHECK_SIZE_AND_OFFSET(__sysctl_args, newlen);

CHECK_TYPE_SIZE(__kernel_uid_t);
CHECK_TYPE_SIZE(__kernel_gid_t);

#if SANITIZER_USES_UID16_SYSCALLS
CHECK_TYPE_SIZE(__kernel_old_uid_t);
CHECK_TYPE_SIZE(__kernel_old_gid_t);
#endif

CHECK_TYPE_SIZE(__kernel_off_t);
CHECK_TYPE_SIZE(__kernel_loff_t);
CHECK_TYPE_SIZE(__kernel_fd_set);
#endif

#if !SANITIZER_ANDROID
CHECK_TYPE_SIZE(wordexp_t);
CHECK_SIZE_AND_OFFSET(wordexp_t, we_wordc);
CHECK_SIZE_AND_OFFSET(wordexp_t, we_wordv);
CHECK_SIZE_AND_OFFSET(wordexp_t, we_offs);
#endif

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

#if SANITIZER_LINUX
CHECK_TYPE_SIZE(mntent);
CHECK_SIZE_AND_OFFSET(mntent, mnt_fsname);
CHECK_SIZE_AND_OFFSET(mntent, mnt_dir);
CHECK_SIZE_AND_OFFSET(mntent, mnt_type);
CHECK_SIZE_AND_OFFSET(mntent, mnt_opts);
CHECK_SIZE_AND_OFFSET(mntent, mnt_freq);
CHECK_SIZE_AND_OFFSET(mntent, mnt_passno);
#endif

CHECK_TYPE_SIZE(ether_addr);

#if (SANITIZER_LINUX || SANITIZER_FREEBSD) && !SANITIZER_ANDROID
CHECK_TYPE_SIZE(ipc_perm);
# if SANITIZER_FREEBSD
CHECK_SIZE_AND_OFFSET(ipc_perm, key);
CHECK_SIZE_AND_OFFSET(ipc_perm, seq);
# else
CHECK_SIZE_AND_OFFSET(ipc_perm, __key);
CHECK_SIZE_AND_OFFSET(ipc_perm, __seq);
# endif
CHECK_SIZE_AND_OFFSET(ipc_perm, uid);
CHECK_SIZE_AND_OFFSET(ipc_perm, gid);
CHECK_SIZE_AND_OFFSET(ipc_perm, cuid);
CHECK_SIZE_AND_OFFSET(ipc_perm, cgid);
#if !defined(__aarch64__) || !SANITIZER_LINUX || __GLIBC_PREREQ (2, 21)
/* On aarch64 glibc 2.20 and earlier provided incorrect mode field.  */
CHECK_SIZE_AND_OFFSET(ipc_perm, mode);
#endif

CHECK_TYPE_SIZE(shmid_ds);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_perm);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_segsz);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_atime);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_dtime);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_ctime);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_cpid);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_lpid);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_nattch);
#endif

CHECK_TYPE_SIZE(clock_t);

#if SANITIZER_LINUX
CHECK_TYPE_SIZE(clockid_t);
#endif

#if !SANITIZER_ANDROID
CHECK_TYPE_SIZE(ifaddrs);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_next);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_name);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_addr);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_netmask);
#if SANITIZER_LINUX || SANITIZER_FREEBSD
// Compare against the union, because we can't reach into the union in a
// compliant way.
#ifdef ifa_dstaddr
#undef ifa_dstaddr
#endif
# if SANITIZER_FREEBSD
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_dstaddr);
# else
COMPILER_CHECK(sizeof(((__sanitizer_ifaddrs *)nullptr)->ifa_dstaddr) ==
               sizeof(((ifaddrs *)nullptr)->ifa_ifu));
COMPILER_CHECK(offsetof(__sanitizer_ifaddrs, ifa_dstaddr) ==
               offsetof(ifaddrs, ifa_ifu));
# endif // SANITIZER_FREEBSD
#else
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_dstaddr);
#endif // SANITIZER_LINUX
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_data);
#endif

#if SANITIZER_LINUX
COMPILER_CHECK(sizeof(__sanitizer_struct_mallinfo) == sizeof(struct mallinfo));
#endif

#if !SANITIZER_ANDROID
CHECK_TYPE_SIZE(timeb);
CHECK_SIZE_AND_OFFSET(timeb, time);
CHECK_SIZE_AND_OFFSET(timeb, millitm);
CHECK_SIZE_AND_OFFSET(timeb, timezone);
CHECK_SIZE_AND_OFFSET(timeb, dstflag);
#endif

CHECK_TYPE_SIZE(passwd);
CHECK_SIZE_AND_OFFSET(passwd, pw_name);
CHECK_SIZE_AND_OFFSET(passwd, pw_passwd);
CHECK_SIZE_AND_OFFSET(passwd, pw_uid);
CHECK_SIZE_AND_OFFSET(passwd, pw_gid);
CHECK_SIZE_AND_OFFSET(passwd, pw_dir);
CHECK_SIZE_AND_OFFSET(passwd, pw_shell);

#if !SANITIZER_ANDROID
CHECK_SIZE_AND_OFFSET(passwd, pw_gecos);
#endif

#if SANITIZER_MAC
CHECK_SIZE_AND_OFFSET(passwd, pw_change);
CHECK_SIZE_AND_OFFSET(passwd, pw_expire);
CHECK_SIZE_AND_OFFSET(passwd, pw_class);
#endif


CHECK_TYPE_SIZE(group);
CHECK_SIZE_AND_OFFSET(group, gr_name);
CHECK_SIZE_AND_OFFSET(group, gr_passwd);
CHECK_SIZE_AND_OFFSET(group, gr_gid);
CHECK_SIZE_AND_OFFSET(group, gr_mem);

#if HAVE_RPC_XDR_H
CHECK_TYPE_SIZE(XDR);
CHECK_SIZE_AND_OFFSET(XDR, x_op);
CHECK_SIZE_AND_OFFSET(XDR, x_ops);
CHECK_SIZE_AND_OFFSET(XDR, x_public);
CHECK_SIZE_AND_OFFSET(XDR, x_private);
CHECK_SIZE_AND_OFFSET(XDR, x_base);
CHECK_SIZE_AND_OFFSET(XDR, x_handy);
COMPILER_CHECK(__sanitizer_XDR_ENCODE == XDR_ENCODE);
COMPILER_CHECK(__sanitizer_XDR_DECODE == XDR_DECODE);
COMPILER_CHECK(__sanitizer_XDR_FREE == XDR_FREE);
#endif

#if SANITIZER_LINUX && !SANITIZER_ANDROID
COMPILER_CHECK(sizeof(__sanitizer_FILE) <= sizeof(FILE));
CHECK_SIZE_AND_OFFSET(FILE, _flags);
CHECK_SIZE_AND_OFFSET(FILE, _IO_read_ptr);
CHECK_SIZE_AND_OFFSET(FILE, _IO_read_end);
CHECK_SIZE_AND_OFFSET(FILE, _IO_read_base);
CHECK_SIZE_AND_OFFSET(FILE, _IO_write_ptr);
CHECK_SIZE_AND_OFFSET(FILE, _IO_write_end);
CHECK_SIZE_AND_OFFSET(FILE, _IO_write_base);
CHECK_SIZE_AND_OFFSET(FILE, _IO_buf_base);
CHECK_SIZE_AND_OFFSET(FILE, _IO_buf_end);
CHECK_SIZE_AND_OFFSET(FILE, _IO_save_base);
CHECK_SIZE_AND_OFFSET(FILE, _IO_backup_base);
CHECK_SIZE_AND_OFFSET(FILE, _IO_save_end);
CHECK_SIZE_AND_OFFSET(FILE, _markers);
CHECK_SIZE_AND_OFFSET(FILE, _chain);
CHECK_SIZE_AND_OFFSET(FILE, _fileno);
#endif

#if SANITIZER_LINUX && !SANITIZER_ANDROID
COMPILER_CHECK(sizeof(__sanitizer__obstack_chunk) <= sizeof(_obstack_chunk));
CHECK_SIZE_AND_OFFSET(_obstack_chunk, limit);
CHECK_SIZE_AND_OFFSET(_obstack_chunk, prev);
CHECK_TYPE_SIZE(obstack);
CHECK_SIZE_AND_OFFSET(obstack, chunk_size);
CHECK_SIZE_AND_OFFSET(obstack, chunk);
CHECK_SIZE_AND_OFFSET(obstack, object_base);
CHECK_SIZE_AND_OFFSET(obstack, next_free);

CHECK_TYPE_SIZE(cookie_io_functions_t);
CHECK_SIZE_AND_OFFSET(cookie_io_functions_t, read);
CHECK_SIZE_AND_OFFSET(cookie_io_functions_t, write);
CHECK_SIZE_AND_OFFSET(cookie_io_functions_t, seek);
CHECK_SIZE_AND_OFFSET(cookie_io_functions_t, close);
#endif

#if SANITIZER_LINUX || SANITIZER_FREEBSD
CHECK_TYPE_SIZE(sem_t);
#endif

#if SANITIZER_LINUX && defined(__arm__)
COMPILER_CHECK(ARM_VFPREGS_SIZE == ARM_VFPREGS_SIZE_ASAN);
#endif

#endif // SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_MAC
