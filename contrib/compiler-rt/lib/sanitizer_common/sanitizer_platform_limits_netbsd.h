//===-- sanitizer_platform_limits_netbsd.h --------------------------------===//
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

#ifndef SANITIZER_PLATFORM_LIMITS_NETBSD_H
#define SANITIZER_PLATFORM_LIMITS_NETBSD_H

#if SANITIZER_NETBSD

#include "sanitizer_internal_defs.h"
#include "sanitizer_platform.h"

#define _GET_LINK_MAP_BY_DLOPEN_HANDLE(handle, shift) \
  ((link_map *)((handle) == nullptr ? nullptr : ((char *)(handle) + (shift))))

#if defined(__x86_64__)
#define GET_LINK_MAP_BY_DLOPEN_HANDLE(handle) \
  _GET_LINK_MAP_BY_DLOPEN_HANDLE(handle, 264)
#elif defined(__i386__)
#define GET_LINK_MAP_BY_DLOPEN_HANDLE(handle) \
  _GET_LINK_MAP_BY_DLOPEN_HANDLE(handle, 136)
#endif

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
extern unsigned struct_sched_param_sz;
extern unsigned struct_statfs_sz;
extern unsigned struct_sockaddr_sz;
extern unsigned ucontext_t_sz;

extern unsigned struct_rlimit_sz;
extern unsigned struct_utimbuf_sz;
extern unsigned struct_timespec_sz;
extern unsigned struct_sembuf_sz;

extern unsigned struct_kevent_sz;
extern unsigned struct_FTS_sz;
extern unsigned struct_FTSENT_sz;

extern unsigned struct_regex_sz;
extern unsigned struct_regmatch_sz;

extern unsigned struct_fstab_sz;

struct __sanitizer_regmatch {
  OFF_T rm_so;
  OFF_T rm_eo;
};

typedef struct __sanitizer_modctl_load {
  const char *ml_filename;
  int ml_flags;
  const char *ml_props;
  uptr ml_propslen;
} __sanitizer_modctl_load_t;
extern const int modctl_load;
extern const int modctl_unload;
extern const int modctl_stat;
extern const int modctl_exists;

union __sanitizer_sigval {
  int sival_int;
  uptr sival_ptr;
};

struct __sanitizer_sigevent {
  int sigev_notify;
  int sigev_signo;
  union __sanitizer_sigval sigev_value;
  uptr sigev_notify_function;
  uptr sigev_notify_attributes;
};

struct __sanitizer_aiocb {
  u64 aio_offset;
  uptr aio_buf;
  uptr aio_nbytes;
  int aio_fildes;
  int aio_lio_opcode;
  int aio_reqprio;
  struct __sanitizer_sigevent aio_sigevent;
  int _state;
  int _errno;
  long _retval;
};

struct __sanitizer_sem_t {
  uptr data[5];
};

struct __sanitizer_ipc_perm {
  u32 uid;
  u32 gid;
  u32 cuid;
  u32 cgid;
  u32 mode;
  unsigned short _seq;
  long _key;
};

struct __sanitizer_shmid_ds {
  __sanitizer_ipc_perm shm_perm;
  unsigned long shm_segsz;
  u32 shm_lpid;
  u32 shm_cpid;
  unsigned int shm_nattch;
  u64 shm_atime;
  u64 shm_dtime;
  u64 shm_ctime;
  void *_shm_internal;
};

struct __sanitizer_protoent {
  char *p_name;
  char **p_aliases;
  int p_proto;
};

struct __sanitizer_netent {
  char *n_name;
  char **n_aliases;
  int n_addrtype;
  u32 n_net;
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
  void *ifa_addr;     // (struct sockaddr *)
  void *ifa_netmask;  // (struct sockaddr *)
  void *ifa_dstaddr;  // (struct sockaddr *)
  void *ifa_data;
  unsigned int ifa_addrflags;
};

typedef unsigned int __sanitizer_socklen_t;

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

struct __sanitizer_timespec {
  __sanitizer_time_t tv_sec;
  long tv_nsec;
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

struct __sanitizer_timeb {
  __sanitizer_time_t time;
  unsigned short millitm;
  short timezone;
  short dstflag;
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

struct __sanitizer_mmsghdr {
  struct __sanitizer_msghdr msg_hdr;
  unsigned int msg_len;
};

struct __sanitizer_cmsghdr {
  unsigned cmsg_len;
  int cmsg_level;
  int cmsg_type;
};

struct __sanitizer_dirent {
  u64 d_fileno;
  u16 d_reclen;
  // more fields that we don't care about
};

typedef int __sanitizer_clock_t;
typedef int __sanitizer_clockid_t;

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

struct __sanitizer_sigset_t {
  // uint32_t * 4
  unsigned int __bits[4];
};

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

extern unsigned struct_sigaltstack_sz;

typedef unsigned int __sanitizer_sigset13_t;

struct __sanitizer_sigaction13 {
  __sanitizer_sighandler_ptr osa_handler;
  __sanitizer_sigset13_t osa_mask;
  int osa_flags;
};

struct __sanitizer_sigaltstack {
  void *ss_sp;
  uptr ss_size;
  int ss_flags;
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
  uptr dlpi_addr;
  const char *dlpi_name;
  const void *dlpi_phdr;
  short dlpi_phnum;
};

extern unsigned struct_ElfW_Phdr_sz;

struct __sanitizer_addrinfo {
  int ai_flags;
  int ai_family;
  int ai_socktype;
  int ai_protocol;
  unsigned ai_addrlen;
  char *ai_canonname;
  void *ai_addr;
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

typedef int __sanitizer_lwpid_t;

struct __sanitizer_glob_t {
  uptr gl_pathc;
  uptr gl_matchc;
  uptr gl_offs;
  int gl_flags;
  char **gl_pathv;
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

extern int struct_ttyent_sz;

extern int ptrace_pt_io;
extern int ptrace_pt_lwpinfo;
extern int ptrace_pt_set_event_mask;
extern int ptrace_pt_get_event_mask;
extern int ptrace_pt_get_process_state;
extern int ptrace_pt_set_siginfo;
extern int ptrace_pt_get_siginfo;
extern int ptrace_piod_read_d;
extern int ptrace_piod_write_d;
extern int ptrace_piod_read_i;
extern int ptrace_piod_write_i;
extern int ptrace_piod_read_auxv;
extern int ptrace_pt_setregs;
extern int ptrace_pt_getregs;
extern int ptrace_pt_setfpregs;
extern int ptrace_pt_getfpregs;
extern int ptrace_pt_setdbregs;
extern int ptrace_pt_getdbregs;

struct __sanitizer_ptrace_io_desc {
  int piod_op;
  void *piod_offs;
  void *piod_addr;
  uptr piod_len;
};

struct __sanitizer_ptrace_lwpinfo {
  __sanitizer_lwpid_t pl_lwpid;
  int pl_event;
};

extern unsigned struct_ptrace_ptrace_io_desc_struct_sz;
extern unsigned struct_ptrace_ptrace_lwpinfo_struct_sz;
extern unsigned struct_ptrace_ptrace_event_struct_sz;
extern unsigned struct_ptrace_ptrace_siginfo_struct_sz;

extern unsigned struct_ptrace_reg_struct_sz;
extern unsigned struct_ptrace_fpreg_struct_sz;
extern unsigned struct_ptrace_dbreg_struct_sz;

struct __sanitizer_wordexp_t {
  uptr we_wordc;
  char **we_wordv;
  uptr we_offs;
  char *we_strings;
  uptr we_nbytes;
};

struct __sanitizer_FILE {
  unsigned char *_p;
  int _r;
  int _w;
  unsigned short _flags;
  short _file;
  struct {
    unsigned char *_base;
    int _size;
  } _bf;
  int _lbfsize;
  void *_cookie;
  int (*_close)(void *ptr);
  u64 (*_read)(void *, void *, uptr);
  u64 (*_seek)(void *, u64, int);
  uptr (*_write)(void *, const void *, uptr);
  struct {
    unsigned char *_base;
    int _size;
  } _ext;
  unsigned char *_up;
  int _ur;
  unsigned char _ubuf[3];
  unsigned char _nbuf[1];
  int (*_flush)(void *ptr);
  char _lb_unused[sizeof(uptr)];
  int _blksize;
  u64 _offset;
};
#define SANITIZER_HAS_STRUCT_FILE 1

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

struct __sanitizer_ttyent {
  char *ty_name;
  char *ty_getty;
  char *ty_type;
  int ty_status;
  char *ty_window;
  char *ty_comment;
  char *ty_class;
};

extern const unsigned long __sanitizer_bufsiz;

#define IOC_NRBITS 8
#define IOC_TYPEBITS 8
#define IOC_SIZEBITS 14
#define IOC_DIRBITS 2
#define IOC_NONE 0U
#define IOC_WRITE 1U
#define IOC_READ 2U
#define IOC_NRMASK ((1 << IOC_NRBITS) - 1)
#define IOC_TYPEMASK ((1 << IOC_TYPEBITS) - 1)
#define IOC_SIZEMASK ((1 << IOC_SIZEBITS) - 1)
#undef IOC_DIRMASK
#define IOC_DIRMASK ((1 << IOC_DIRBITS) - 1)
#define IOC_NRSHIFT 0
#define IOC_TYPESHIFT (IOC_NRSHIFT + IOC_NRBITS)
#define IOC_SIZESHIFT (IOC_TYPESHIFT + IOC_TYPEBITS)
#define IOC_DIRSHIFT (IOC_SIZESHIFT + IOC_SIZEBITS)
#define EVIOC_EV_MAX 0x1f
#define EVIOC_ABS_MAX 0x3f

#define IOC_DIR(nr) (((nr) >> IOC_DIRSHIFT) & IOC_DIRMASK)
#define IOC_TYPE(nr) (((nr) >> IOC_TYPESHIFT) & IOC_TYPEMASK)
#define IOC_NR(nr) (((nr) >> IOC_NRSHIFT) & IOC_NRMASK)
#define IOC_SIZE(nr) (((nr) >> IOC_SIZESHIFT) & IOC_SIZEMASK)

// ioctl request identifiers

extern unsigned struct_altqreq_sz;
extern unsigned struct_amr_user_ioctl_sz;
extern unsigned struct_ap_control_sz;
extern unsigned struct_apm_ctl_sz;
extern unsigned struct_apm_event_info_sz;
extern unsigned struct_apm_power_info_sz;
extern unsigned struct_atabusiodetach_args_sz;
extern unsigned struct_atabusioscan_args_sz;
extern unsigned struct_ath_diag_sz;
extern unsigned struct_atm_flowmap_sz;
extern unsigned struct_audio_buf_info_sz;
extern unsigned struct_audio_device_sz;
extern unsigned struct_audio_encoding_sz;
extern unsigned struct_audio_info_sz;
extern unsigned struct_audio_offset_sz;
extern unsigned struct_bio_locate_sz;
extern unsigned struct_bioc_alarm_sz;
extern unsigned struct_bioc_blink_sz;
extern unsigned struct_bioc_disk_sz;
extern unsigned struct_bioc_inq_sz;
extern unsigned struct_bioc_setstate_sz;
extern unsigned struct_bioc_vol_sz;
extern unsigned struct_bioc_volops_sz;
extern unsigned struct_bktr_chnlset_sz;
extern unsigned struct_bktr_remote_sz;
extern unsigned struct_blue_conf_sz;
extern unsigned struct_blue_interface_sz;
extern unsigned struct_blue_stats_sz;
extern unsigned struct_bpf_dltlist_sz;
extern unsigned struct_bpf_program_sz;
extern unsigned struct_bpf_stat_old_sz;
extern unsigned struct_bpf_stat_sz;
extern unsigned struct_bpf_version_sz;
extern unsigned struct_btreq_sz;
extern unsigned struct_btsco_info_sz;
extern unsigned struct_buffmem_desc_sz;
extern unsigned struct_cbq_add_class_sz;
extern unsigned struct_cbq_add_filter_sz;
extern unsigned struct_cbq_delete_class_sz;
extern unsigned struct_cbq_delete_filter_sz;
extern unsigned struct_cbq_getstats_sz;
extern unsigned struct_cbq_interface_sz;
extern unsigned struct_cbq_modify_class_sz;
extern unsigned struct_ccd_ioctl_sz;
extern unsigned struct_cdnr_add_element_sz;
extern unsigned struct_cdnr_add_filter_sz;
extern unsigned struct_cdnr_add_tbmeter_sz;
extern unsigned struct_cdnr_add_trtcm_sz;
extern unsigned struct_cdnr_add_tswtcm_sz;
extern unsigned struct_cdnr_delete_element_sz;
extern unsigned struct_cdnr_delete_filter_sz;
extern unsigned struct_cdnr_get_stats_sz;
extern unsigned struct_cdnr_interface_sz;
extern unsigned struct_cdnr_modify_tbmeter_sz;
extern unsigned struct_cdnr_modify_trtcm_sz;
extern unsigned struct_cdnr_modify_tswtcm_sz;
extern unsigned struct_cdnr_tbmeter_stats_sz;
extern unsigned struct_cdnr_tcm_stats_sz;
extern unsigned struct_cgd_ioctl_sz;
extern unsigned struct_cgd_user_sz;
extern unsigned struct_changer_element_status_request_sz;
extern unsigned struct_changer_exchange_request_sz;
extern unsigned struct_changer_move_request_sz;
extern unsigned struct_changer_params_sz;
extern unsigned struct_changer_position_request_sz;
extern unsigned struct_changer_set_voltag_request_sz;
extern unsigned struct_clockctl_adjtime_sz;
extern unsigned struct_clockctl_clock_settime_sz;
extern unsigned struct_clockctl_ntp_adjtime_sz;
extern unsigned struct_clockctl_settimeofday_sz;
extern unsigned struct_cnwistats_sz;
extern unsigned struct_cnwitrail_sz;
extern unsigned struct_cnwstatus_sz;
extern unsigned struct_count_info_sz;
extern unsigned struct_cpu_ucode_sz;
extern unsigned struct_cpu_ucode_version_sz;
extern unsigned struct_crypt_kop_sz;
extern unsigned struct_crypt_mkop_sz;
extern unsigned struct_crypt_mop_sz;
extern unsigned struct_crypt_op_sz;
extern unsigned struct_crypt_result_sz;
extern unsigned struct_crypt_sfop_sz;
extern unsigned struct_crypt_sgop_sz;
extern unsigned struct_cryptret_sz;
extern unsigned struct_devdetachargs_sz;
extern unsigned struct_devlistargs_sz;
extern unsigned struct_devpmargs_sz;
extern unsigned struct_devrescanargs_sz;
extern unsigned struct_disk_badsecinfo_sz;
extern unsigned struct_disk_strategy_sz;
extern unsigned struct_disklabel_sz;
extern unsigned struct_dkbad_sz;
extern unsigned struct_dkwedge_info_sz;
extern unsigned struct_dkwedge_list_sz;
extern unsigned struct_dmio_setfunc_sz;
extern unsigned struct_dmx_pes_filter_params_sz;
extern unsigned struct_dmx_sct_filter_params_sz;
extern unsigned struct_dmx_stc_sz;
extern unsigned struct_dvb_diseqc_master_cmd_sz;
extern unsigned struct_dvb_diseqc_slave_reply_sz;
extern unsigned struct_dvb_frontend_event_sz;
extern unsigned struct_dvb_frontend_info_sz;
extern unsigned struct_dvb_frontend_parameters_sz;
extern unsigned struct_eccapreq_sz;
extern unsigned struct_fbcmap_sz;
extern unsigned struct_fbcurpos_sz;
extern unsigned struct_fbcursor_sz;
extern unsigned struct_fbgattr_sz;
extern unsigned struct_fbsattr_sz;
extern unsigned struct_fbtype_sz;
extern unsigned struct_fdformat_cmd_sz;
extern unsigned struct_fdformat_parms_sz;
extern unsigned struct_fifoq_conf_sz;
extern unsigned struct_fifoq_getstats_sz;
extern unsigned struct_fifoq_interface_sz;
extern unsigned struct_format_op_sz;
extern unsigned struct_fss_get_sz;
extern unsigned struct_fss_set_sz;
extern unsigned struct_gpio_attach_sz;
extern unsigned struct_gpio_info_sz;
extern unsigned struct_gpio_req_sz;
extern unsigned struct_gpio_set_sz;
extern unsigned struct_hfsc_add_class_sz;
extern unsigned struct_hfsc_add_filter_sz;
extern unsigned struct_hfsc_attach_sz;
extern unsigned struct_hfsc_class_stats_sz;
extern unsigned struct_hfsc_delete_class_sz;
extern unsigned struct_hfsc_delete_filter_sz;
extern unsigned struct_hfsc_interface_sz;
extern unsigned struct_hfsc_modify_class_sz;
extern unsigned struct_hpcfb_dsp_op_sz;
extern unsigned struct_hpcfb_dspconf_sz;
extern unsigned struct_hpcfb_fbconf_sz;
extern unsigned struct_if_addrprefreq_sz;
extern unsigned struct_if_clonereq_sz;
extern unsigned struct_if_laddrreq_sz;
extern unsigned struct_ifaddr_sz;
extern unsigned struct_ifaliasreq_sz;
extern unsigned struct_ifcapreq_sz;
extern unsigned struct_ifconf_sz;
extern unsigned struct_ifdatareq_sz;
extern unsigned struct_ifdrv_sz;
extern unsigned struct_ifmediareq_sz;
extern unsigned struct_ifpppcstatsreq_sz;
extern unsigned struct_ifpppstatsreq_sz;
extern unsigned struct_ifreq_sz;
extern unsigned struct_in6_addrpolicy_sz;
extern unsigned struct_in6_ndireq_sz;
extern unsigned struct_ioc_load_unload_sz;
extern unsigned struct_ioc_patch_sz;
extern unsigned struct_ioc_play_blocks_sz;
extern unsigned struct_ioc_play_msf_sz;
extern unsigned struct_ioc_play_track_sz;
extern unsigned struct_ioc_read_subchannel_sz;
extern unsigned struct_ioc_read_toc_entry_sz;
extern unsigned struct_ioc_toc_header_sz;
extern unsigned struct_ioc_vol_sz;
extern unsigned struct_ioctl_pt_sz;
extern unsigned struct_ioppt_sz;
extern unsigned struct_iovec_sz;
extern unsigned struct_ipfobj_sz;
extern unsigned struct_irda_params_sz;
extern unsigned struct_isp_fc_device_sz;
extern unsigned struct_isp_fc_tsk_mgmt_sz;
extern unsigned struct_isp_hba_device_sz;
extern unsigned struct_isv_cmd_sz;
extern unsigned struct_jobs_add_class_sz;
extern unsigned struct_jobs_add_filter_sz;
extern unsigned struct_jobs_attach_sz;
extern unsigned struct_jobs_class_stats_sz;
extern unsigned struct_jobs_delete_class_sz;
extern unsigned struct_jobs_delete_filter_sz;
extern unsigned struct_jobs_interface_sz;
extern unsigned struct_jobs_modify_class_sz;
extern unsigned struct_kbentry_sz;
extern unsigned struct_kfilter_mapping_sz;
extern unsigned struct_kiockeymap_sz;
extern unsigned struct_ksyms_gsymbol_sz;
extern unsigned struct_ksyms_gvalue_sz;
extern unsigned struct_ksyms_ogsymbol_sz;
extern unsigned struct_kttcp_io_args_sz;
extern unsigned struct_ltchars_sz;
extern unsigned struct_lua_create_sz;
extern unsigned struct_lua_info_sz;
extern unsigned struct_lua_load_sz;
extern unsigned struct_lua_require_sz;
extern unsigned struct_mbpp_param_sz;
extern unsigned struct_md_conf_sz;
extern unsigned struct_meteor_capframe_sz;
extern unsigned struct_meteor_counts_sz;
extern unsigned struct_meteor_geomet_sz;
extern unsigned struct_meteor_pixfmt_sz;
extern unsigned struct_meteor_video_sz;
extern unsigned struct_mlx_cinfo_sz;
extern unsigned struct_mlx_pause_sz;
extern unsigned struct_mlx_rebuild_request_sz;
extern unsigned struct_mlx_rebuild_status_sz;
extern unsigned struct_mlx_usercommand_sz;
extern unsigned struct_mly_user_command_sz;
extern unsigned struct_mly_user_health_sz;
extern unsigned struct_mtget_sz;
extern unsigned struct_mtop_sz;
extern unsigned struct_npf_ioctl_table_sz;
extern unsigned struct_npioctl_sz;
extern unsigned struct_nvme_pt_command_sz;
extern unsigned struct_ochanger_element_status_request_sz;
extern unsigned struct_ofiocdesc_sz;
extern unsigned struct_okiockey_sz;
extern unsigned struct_ortentry_sz;
extern unsigned struct_oscsi_addr_sz;
extern unsigned struct_oss_audioinfo_sz;
extern unsigned struct_oss_sysinfo_sz;
extern unsigned struct_pciio_bdf_cfgreg_sz;
extern unsigned struct_pciio_businfo_sz;
extern unsigned struct_pciio_cfgreg_sz;
extern unsigned struct_pciio_drvname_sz;
extern unsigned struct_pciio_drvnameonbus_sz;
extern unsigned struct_pcvtid_sz;
extern unsigned struct_pf_osfp_ioctl_sz;
extern unsigned struct_pf_status_sz;
extern unsigned struct_pfioc_altq_sz;
extern unsigned struct_pfioc_if_sz;
extern unsigned struct_pfioc_iface_sz;
extern unsigned struct_pfioc_limit_sz;
extern unsigned struct_pfioc_natlook_sz;
extern unsigned struct_pfioc_pooladdr_sz;
extern unsigned struct_pfioc_qstats_sz;
extern unsigned struct_pfioc_rule_sz;
extern unsigned struct_pfioc_ruleset_sz;
extern unsigned struct_pfioc_src_node_kill_sz;
extern unsigned struct_pfioc_src_nodes_sz;
extern unsigned struct_pfioc_state_kill_sz;
extern unsigned struct_pfioc_state_sz;
extern unsigned struct_pfioc_states_sz;
extern unsigned struct_pfioc_table_sz;
extern unsigned struct_pfioc_tm_sz;
extern unsigned struct_pfioc_trans_sz;
extern unsigned struct_plistref_sz;
extern unsigned struct_power_type_sz;
extern unsigned struct_ppp_idle_sz;
extern unsigned struct_ppp_option_data_sz;
extern unsigned struct_ppp_rawin_sz;
extern unsigned struct_pppoeconnectionstate_sz;
extern unsigned struct_pppoediscparms_sz;
extern unsigned struct_priq_add_class_sz;
extern unsigned struct_priq_add_filter_sz;
extern unsigned struct_priq_class_stats_sz;
extern unsigned struct_priq_delete_class_sz;
extern unsigned struct_priq_delete_filter_sz;
extern unsigned struct_priq_interface_sz;
extern unsigned struct_priq_modify_class_sz;
extern unsigned struct_ptmget_sz;
extern unsigned struct_pvctxreq_sz;
extern unsigned struct_radio_info_sz;
extern unsigned struct_red_conf_sz;
extern unsigned struct_red_interface_sz;
extern unsigned struct_red_stats_sz;
extern unsigned struct_redparams_sz;
extern unsigned struct_rf_pmparams_sz;
extern unsigned struct_rf_pmstat_sz;
extern unsigned struct_rf_recon_req_sz;
extern unsigned struct_rio_conf_sz;
extern unsigned struct_rio_interface_sz;
extern unsigned struct_rio_stats_sz;
extern unsigned struct_scan_io_sz;
extern unsigned struct_scbusaccel_args_sz;
extern unsigned struct_scbusiodetach_args_sz;
extern unsigned struct_scbusioscan_args_sz;
extern unsigned struct_scsi_addr_sz;
extern unsigned struct_seq_event_rec_sz;
extern unsigned struct_session_op_sz;
extern unsigned struct_sgttyb_sz;
extern unsigned struct_sioc_sg_req_sz;
extern unsigned struct_sioc_vif_req_sz;
extern unsigned struct_smbioc_flags_sz;
extern unsigned struct_smbioc_lookup_sz;
extern unsigned struct_smbioc_oshare_sz;
extern unsigned struct_smbioc_ossn_sz;
extern unsigned struct_smbioc_rq_sz;
extern unsigned struct_smbioc_rw_sz;
extern unsigned struct_spppauthcfg_sz;
extern unsigned struct_spppauthfailuresettings_sz;
extern unsigned struct_spppauthfailurestats_sz;
extern unsigned struct_spppdnsaddrs_sz;
extern unsigned struct_spppdnssettings_sz;
extern unsigned struct_spppidletimeout_sz;
extern unsigned struct_spppkeepalivesettings_sz;
extern unsigned struct_sppplcpcfg_sz;
extern unsigned struct_spppstatus_sz;
extern unsigned struct_spppstatusncp_sz;
extern unsigned struct_srt_rt_sz;
extern unsigned struct_stic_xinfo_sz;
extern unsigned struct_sun_dkctlr_sz;
extern unsigned struct_sun_dkgeom_sz;
extern unsigned struct_sun_dkpart_sz;
extern unsigned struct_synth_info_sz;
extern unsigned struct_tbrreq_sz;
extern unsigned struct_tchars_sz;
extern unsigned struct_termios_sz;
extern unsigned struct_timeval_sz;
extern unsigned struct_twe_drivecommand_sz;
extern unsigned struct_twe_paramcommand_sz;
extern unsigned struct_twe_usercommand_sz;
extern unsigned struct_ukyopon_identify_sz;
extern unsigned struct_urio_command_sz;
extern unsigned struct_usb_alt_interface_sz;
extern unsigned struct_usb_bulk_ra_wb_opt_sz;
extern unsigned struct_usb_config_desc_sz;
extern unsigned struct_usb_ctl_report_desc_sz;
extern unsigned struct_usb_ctl_report_sz;
extern unsigned struct_usb_ctl_request_sz;
extern unsigned struct_autofs_daemon_request_sz;
extern unsigned struct_autofs_daemon_done_sz;
extern unsigned struct_sctp_connectx_addrs_sz;
extern unsigned struct_usb_device_info_old_sz;
extern unsigned struct_usb_device_info_sz;
extern unsigned struct_usb_device_stats_sz;
extern unsigned struct_usb_endpoint_desc_sz;
extern unsigned struct_usb_full_desc_sz;
extern unsigned struct_usb_interface_desc_sz;
extern unsigned struct_usb_string_desc_sz;
extern unsigned struct_utoppy_readfile_sz;
extern unsigned struct_utoppy_rename_sz;
extern unsigned struct_utoppy_stats_sz;
extern unsigned struct_utoppy_writefile_sz;
extern unsigned struct_v4l2_audio_sz;
extern unsigned struct_v4l2_audioout_sz;
extern unsigned struct_v4l2_buffer_sz;
extern unsigned struct_v4l2_capability_sz;
extern unsigned struct_v4l2_control_sz;
extern unsigned struct_v4l2_crop_sz;
extern unsigned struct_v4l2_cropcap_sz;
extern unsigned struct_v4l2_fmtdesc_sz;
extern unsigned struct_v4l2_format_sz;
extern unsigned struct_v4l2_framebuffer_sz;
extern unsigned struct_v4l2_frequency_sz;
extern unsigned struct_v4l2_frmivalenum_sz;
extern unsigned struct_v4l2_frmsizeenum_sz;
extern unsigned struct_v4l2_input_sz;
extern unsigned struct_v4l2_jpegcompression_sz;
extern unsigned struct_v4l2_modulator_sz;
extern unsigned struct_v4l2_output_sz;
extern unsigned struct_v4l2_queryctrl_sz;
extern unsigned struct_v4l2_querymenu_sz;
extern unsigned struct_v4l2_requestbuffers_sz;
extern unsigned struct_v4l2_standard_sz;
extern unsigned struct_v4l2_streamparm_sz;
extern unsigned struct_v4l2_tuner_sz;
extern unsigned struct_vnd_ioctl_sz;
extern unsigned struct_vnd_user_sz;
extern unsigned struct_vt_stat_sz;
extern unsigned struct_wdog_conf_sz;
extern unsigned struct_wdog_mode_sz;
extern unsigned struct_wfq_conf_sz;
extern unsigned struct_wfq_getqid_sz;
extern unsigned struct_wfq_getstats_sz;
extern unsigned struct_wfq_interface_sz;
extern unsigned struct_wfq_setweight_sz;
extern unsigned struct_winsize_sz;
extern unsigned struct_wscons_event_sz;
extern unsigned struct_wsdisplay_addscreendata_sz;
extern unsigned struct_wsdisplay_char_sz;
extern unsigned struct_wsdisplay_cmap_sz;
extern unsigned struct_wsdisplay_curpos_sz;
extern unsigned struct_wsdisplay_cursor_sz;
extern unsigned struct_wsdisplay_delscreendata_sz;
extern unsigned struct_wsdisplay_fbinfo_sz;
extern unsigned struct_wsdisplay_font_sz;
extern unsigned struct_wsdisplay_kbddata_sz;
extern unsigned struct_wsdisplay_msgattrs_sz;
extern unsigned struct_wsdisplay_param_sz;
extern unsigned struct_wsdisplay_scroll_data_sz;
extern unsigned struct_wsdisplay_usefontdata_sz;
extern unsigned struct_wsdisplayio_blit_sz;
extern unsigned struct_wsdisplayio_bus_id_sz;
extern unsigned struct_wsdisplayio_edid_info_sz;
extern unsigned struct_wsdisplayio_fbinfo_sz;
extern unsigned struct_wskbd_bell_data_sz;
extern unsigned struct_wskbd_keyrepeat_data_sz;
extern unsigned struct_wskbd_map_data_sz;
extern unsigned struct_wskbd_scroll_data_sz;
extern unsigned struct_wsmouse_calibcoords_sz;
extern unsigned struct_wsmouse_id_sz;
extern unsigned struct_wsmouse_repeat_sz;
extern unsigned struct_wsmux_device_list_sz;
extern unsigned struct_wsmux_device_sz;
extern unsigned struct_xd_iocmd_sz;

extern unsigned struct_scsireq_sz;
extern unsigned struct_tone_sz;
extern unsigned union_twe_statrequest_sz;
extern unsigned struct_usb_device_descriptor_sz;
extern unsigned struct_vt_mode_sz;
extern unsigned struct__old_mixer_info_sz;
extern unsigned struct__agp_allocate_sz;
extern unsigned struct__agp_bind_sz;
extern unsigned struct__agp_info_sz;
extern unsigned struct__agp_setup_sz;
extern unsigned struct__agp_unbind_sz;
extern unsigned struct_atareq_sz;
extern unsigned struct_cpustate_sz;
extern unsigned struct_dmx_caps_sz;
extern unsigned enum_dmx_source_sz;
extern unsigned union_dvd_authinfo_sz;
extern unsigned union_dvd_struct_sz;
extern unsigned enum_v4l2_priority_sz;
extern unsigned struct_envsys_basic_info_sz;
extern unsigned struct_envsys_tre_data_sz;
extern unsigned enum_fe_sec_mini_cmd_sz;
extern unsigned enum_fe_sec_tone_mode_sz;
extern unsigned enum_fe_sec_voltage_sz;
extern unsigned enum_fe_status_sz;
extern unsigned struct_gdt_ctrt_sz;
extern unsigned struct_gdt_event_sz;
extern unsigned struct_gdt_osv_sz;
extern unsigned struct_gdt_rescan_sz;
extern unsigned struct_gdt_statist_sz;
extern unsigned struct_gdt_ucmd_sz;
extern unsigned struct_iscsi_conn_status_parameters_sz;
extern unsigned struct_iscsi_get_version_parameters_sz;
extern unsigned struct_iscsi_iocommand_parameters_sz;
extern unsigned struct_iscsi_login_parameters_sz;
extern unsigned struct_iscsi_logout_parameters_sz;
extern unsigned struct_iscsi_register_event_parameters_sz;
extern unsigned struct_iscsi_remove_parameters_sz;
extern unsigned struct_iscsi_send_targets_parameters_sz;
extern unsigned struct_iscsi_set_node_name_parameters_sz;
extern unsigned struct_iscsi_wait_event_parameters_sz;
extern unsigned struct_isp_stats_sz;
extern unsigned struct_lsenable_sz;
extern unsigned struct_lsdisable_sz;
extern unsigned struct_mixer_ctrl_sz;
extern unsigned struct_mixer_devinfo_sz;
extern unsigned struct_mpu_command_rec_sz;
extern unsigned struct_rndstat_sz;
extern unsigned struct_rndstat_name_sz;
extern unsigned struct_rndctl_sz;
extern unsigned struct_rnddata_sz;
extern unsigned struct_rndpoolstat_sz;
extern unsigned struct_rndstat_est_sz;
extern unsigned struct_rndstat_est_name_sz;
extern unsigned struct_pps_params_sz;
extern unsigned struct_pps_info_sz;
extern unsigned struct_mixer_info_sz;
extern unsigned struct_RF_SparetWait_sz;
extern unsigned struct_RF_ComponentLabel_sz;
extern unsigned struct_RF_SingleComponent_sz;
extern unsigned struct_RF_ProgressInfo_sz;
extern unsigned struct_nvlist_ref_sz;
extern unsigned struct_StringList_sz;


// A special value to mark ioctls that are not present on the target platform,
// when it can not be determined without including any system headers.
extern const unsigned IOCTL_NOT_PRESENT;


extern unsigned IOCTL_AFM_ADDFMAP;
extern unsigned IOCTL_AFM_DELFMAP;
extern unsigned IOCTL_AFM_CLEANFMAP;
extern unsigned IOCTL_AFM_GETFMAP;
extern unsigned IOCTL_ALTQGTYPE;
extern unsigned IOCTL_ALTQTBRSET;
extern unsigned IOCTL_ALTQTBRGET;
extern unsigned IOCTL_BLUE_IF_ATTACH;
extern unsigned IOCTL_BLUE_IF_DETACH;
extern unsigned IOCTL_BLUE_ENABLE;
extern unsigned IOCTL_BLUE_DISABLE;
extern unsigned IOCTL_BLUE_CONFIG;
extern unsigned IOCTL_BLUE_GETSTATS;
extern unsigned IOCTL_CBQ_IF_ATTACH;
extern unsigned IOCTL_CBQ_IF_DETACH;
extern unsigned IOCTL_CBQ_ENABLE;
extern unsigned IOCTL_CBQ_DISABLE;
extern unsigned IOCTL_CBQ_CLEAR_HIERARCHY;
extern unsigned IOCTL_CBQ_ADD_CLASS;
extern unsigned IOCTL_CBQ_DEL_CLASS;
extern unsigned IOCTL_CBQ_MODIFY_CLASS;
extern unsigned IOCTL_CBQ_ADD_FILTER;
extern unsigned IOCTL_CBQ_DEL_FILTER;
extern unsigned IOCTL_CBQ_GETSTATS;
extern unsigned IOCTL_CDNR_IF_ATTACH;
extern unsigned IOCTL_CDNR_IF_DETACH;
extern unsigned IOCTL_CDNR_ENABLE;
extern unsigned IOCTL_CDNR_DISABLE;
extern unsigned IOCTL_CDNR_ADD_FILTER;
extern unsigned IOCTL_CDNR_DEL_FILTER;
extern unsigned IOCTL_CDNR_GETSTATS;
extern unsigned IOCTL_CDNR_ADD_ELEM;
extern unsigned IOCTL_CDNR_DEL_ELEM;
extern unsigned IOCTL_CDNR_ADD_TBM;
extern unsigned IOCTL_CDNR_MOD_TBM;
extern unsigned IOCTL_CDNR_TBM_STATS;
extern unsigned IOCTL_CDNR_ADD_TCM;
extern unsigned IOCTL_CDNR_MOD_TCM;
extern unsigned IOCTL_CDNR_TCM_STATS;
extern unsigned IOCTL_CDNR_ADD_TSW;
extern unsigned IOCTL_CDNR_MOD_TSW;
extern unsigned IOCTL_FIFOQ_IF_ATTACH;
extern unsigned IOCTL_FIFOQ_IF_DETACH;
extern unsigned IOCTL_FIFOQ_ENABLE;
extern unsigned IOCTL_FIFOQ_DISABLE;
extern unsigned IOCTL_FIFOQ_CONFIG;
extern unsigned IOCTL_FIFOQ_GETSTATS;
extern unsigned IOCTL_HFSC_IF_ATTACH;
extern unsigned IOCTL_HFSC_IF_DETACH;
extern unsigned IOCTL_HFSC_ENABLE;
extern unsigned IOCTL_HFSC_DISABLE;
extern unsigned IOCTL_HFSC_CLEAR_HIERARCHY;
extern unsigned IOCTL_HFSC_ADD_CLASS;
extern unsigned IOCTL_HFSC_DEL_CLASS;
extern unsigned IOCTL_HFSC_MOD_CLASS;
extern unsigned IOCTL_HFSC_ADD_FILTER;
extern unsigned IOCTL_HFSC_DEL_FILTER;
extern unsigned IOCTL_HFSC_GETSTATS;
extern unsigned IOCTL_JOBS_IF_ATTACH;
extern unsigned IOCTL_JOBS_IF_DETACH;
extern unsigned IOCTL_JOBS_ENABLE;
extern unsigned IOCTL_JOBS_DISABLE;
extern unsigned IOCTL_JOBS_CLEAR;
extern unsigned IOCTL_JOBS_ADD_CLASS;
extern unsigned IOCTL_JOBS_DEL_CLASS;
extern unsigned IOCTL_JOBS_MOD_CLASS;
extern unsigned IOCTL_JOBS_ADD_FILTER;
extern unsigned IOCTL_JOBS_DEL_FILTER;
extern unsigned IOCTL_JOBS_GETSTATS;
extern unsigned IOCTL_PRIQ_IF_ATTACH;
extern unsigned IOCTL_PRIQ_IF_DETACH;
extern unsigned IOCTL_PRIQ_ENABLE;
extern unsigned IOCTL_PRIQ_DISABLE;
extern unsigned IOCTL_PRIQ_CLEAR;
extern unsigned IOCTL_PRIQ_ADD_CLASS;
extern unsigned IOCTL_PRIQ_DEL_CLASS;
extern unsigned IOCTL_PRIQ_MOD_CLASS;
extern unsigned IOCTL_PRIQ_ADD_FILTER;
extern unsigned IOCTL_PRIQ_DEL_FILTER;
extern unsigned IOCTL_PRIQ_GETSTATS;
extern unsigned IOCTL_RED_IF_ATTACH;
extern unsigned IOCTL_RED_IF_DETACH;
extern unsigned IOCTL_RED_ENABLE;
extern unsigned IOCTL_RED_DISABLE;
extern unsigned IOCTL_RED_CONFIG;
extern unsigned IOCTL_RED_GETSTATS;
extern unsigned IOCTL_RED_SETDEFAULTS;
extern unsigned IOCTL_RIO_IF_ATTACH;
extern unsigned IOCTL_RIO_IF_DETACH;
extern unsigned IOCTL_RIO_ENABLE;
extern unsigned IOCTL_RIO_DISABLE;
extern unsigned IOCTL_RIO_CONFIG;
extern unsigned IOCTL_RIO_GETSTATS;
extern unsigned IOCTL_RIO_SETDEFAULTS;
extern unsigned IOCTL_WFQ_IF_ATTACH;
extern unsigned IOCTL_WFQ_IF_DETACH;
extern unsigned IOCTL_WFQ_ENABLE;
extern unsigned IOCTL_WFQ_DISABLE;
extern unsigned IOCTL_WFQ_CONFIG;
extern unsigned IOCTL_WFQ_GET_STATS;
extern unsigned IOCTL_WFQ_GET_QID;
extern unsigned IOCTL_WFQ_SET_WEIGHT;
extern unsigned IOCTL_CRIOGET;
extern unsigned IOCTL_CIOCFSESSION;
extern unsigned IOCTL_CIOCKEY;
extern unsigned IOCTL_CIOCNFKEYM;
extern unsigned IOCTL_CIOCNFSESSION;
extern unsigned IOCTL_CIOCNCRYPTRETM;
extern unsigned IOCTL_CIOCNCRYPTRET;
extern unsigned IOCTL_CIOCGSESSION;
extern unsigned IOCTL_CIOCNGSESSION;
extern unsigned IOCTL_CIOCCRYPT;
extern unsigned IOCTL_CIOCNCRYPTM;
extern unsigned IOCTL_CIOCASYMFEAT;
extern unsigned IOCTL_APM_IOC_REJECT;
extern unsigned IOCTL_APM_IOC_STANDBY;
extern unsigned IOCTL_APM_IOC_SUSPEND;
extern unsigned IOCTL_OAPM_IOC_GETPOWER;
extern unsigned IOCTL_APM_IOC_GETPOWER;
extern unsigned IOCTL_APM_IOC_NEXTEVENT;
extern unsigned IOCTL_APM_IOC_DEV_CTL;
extern unsigned IOCTL_NETBSD_DM_IOCTL;
extern unsigned IOCTL_DMIO_SETFUNC;
extern unsigned IOCTL_DMX_START;
extern unsigned IOCTL_DMX_STOP;
extern unsigned IOCTL_DMX_SET_FILTER;
extern unsigned IOCTL_DMX_SET_PES_FILTER;
extern unsigned IOCTL_DMX_SET_BUFFER_SIZE;
extern unsigned IOCTL_DMX_GET_STC;
extern unsigned IOCTL_DMX_ADD_PID;
extern unsigned IOCTL_DMX_REMOVE_PID;
extern unsigned IOCTL_DMX_GET_CAPS;
extern unsigned IOCTL_DMX_SET_SOURCE;
extern unsigned IOCTL_FE_READ_STATUS;
extern unsigned IOCTL_FE_READ_BER;
extern unsigned IOCTL_FE_READ_SNR;
extern unsigned IOCTL_FE_READ_SIGNAL_STRENGTH;
extern unsigned IOCTL_FE_READ_UNCORRECTED_BLOCKS;
extern unsigned IOCTL_FE_SET_FRONTEND;
extern unsigned IOCTL_FE_GET_FRONTEND;
extern unsigned IOCTL_FE_GET_EVENT;
extern unsigned IOCTL_FE_GET_INFO;
extern unsigned IOCTL_FE_DISEQC_RESET_OVERLOAD;
extern unsigned IOCTL_FE_DISEQC_SEND_MASTER_CMD;
extern unsigned IOCTL_FE_DISEQC_RECV_SLAVE_REPLY;
extern unsigned IOCTL_FE_DISEQC_SEND_BURST;
extern unsigned IOCTL_FE_SET_TONE;
extern unsigned IOCTL_FE_SET_VOLTAGE;
extern unsigned IOCTL_FE_ENABLE_HIGH_LNB_VOLTAGE;
extern unsigned IOCTL_FE_SET_FRONTEND_TUNE_MODE;
extern unsigned IOCTL_FE_DISHNETWORK_SEND_LEGACY_CMD;
extern unsigned IOCTL_FILEMON_SET_FD;
extern unsigned IOCTL_FILEMON_SET_PID;
extern unsigned IOCTL_HDAUDIO_FGRP_INFO;
extern unsigned IOCTL_HDAUDIO_FGRP_GETCONFIG;
extern unsigned IOCTL_HDAUDIO_FGRP_SETCONFIG;
extern unsigned IOCTL_HDAUDIO_FGRP_WIDGET_INFO;
extern unsigned IOCTL_HDAUDIO_FGRP_CODEC_INFO;
extern unsigned IOCTL_HDAUDIO_AFG_WIDGET_INFO;
extern unsigned IOCTL_HDAUDIO_AFG_CODEC_INFO;
extern unsigned IOCTL_CEC_GET_PHYS_ADDR;
extern unsigned IOCTL_CEC_GET_LOG_ADDRS;
extern unsigned IOCTL_CEC_SET_LOG_ADDRS;
extern unsigned IOCTL_CEC_GET_VENDOR_ID;
extern unsigned IOCTL_HPCFBIO_GCONF;
extern unsigned IOCTL_HPCFBIO_SCONF;
extern unsigned IOCTL_HPCFBIO_GDSPCONF;
extern unsigned IOCTL_HPCFBIO_SDSPCONF;
extern unsigned IOCTL_HPCFBIO_GOP;
extern unsigned IOCTL_HPCFBIO_SOP;
extern unsigned IOCTL_IOPIOCPT;
extern unsigned IOCTL_IOPIOCGLCT;
extern unsigned IOCTL_IOPIOCGSTATUS;
extern unsigned IOCTL_IOPIOCRECONFIG;
extern unsigned IOCTL_IOPIOCGTIDMAP;
extern unsigned IOCTL_SIOCGATHSTATS;
extern unsigned IOCTL_SIOCGATHDIAG;
extern unsigned IOCTL_METEORCAPTUR;
extern unsigned IOCTL_METEORCAPFRM;
extern unsigned IOCTL_METEORSETGEO;
extern unsigned IOCTL_METEORGETGEO;
extern unsigned IOCTL_METEORSTATUS;
extern unsigned IOCTL_METEORSHUE;
extern unsigned IOCTL_METEORGHUE;
extern unsigned IOCTL_METEORSFMT;
extern unsigned IOCTL_METEORGFMT;
extern unsigned IOCTL_METEORSINPUT;
extern unsigned IOCTL_METEORGINPUT;
extern unsigned IOCTL_METEORSCHCV;
extern unsigned IOCTL_METEORGCHCV;
extern unsigned IOCTL_METEORSCOUNT;
extern unsigned IOCTL_METEORGCOUNT;
extern unsigned IOCTL_METEORSFPS;
extern unsigned IOCTL_METEORGFPS;
extern unsigned IOCTL_METEORSSIGNAL;
extern unsigned IOCTL_METEORGSIGNAL;
extern unsigned IOCTL_METEORSVIDEO;
extern unsigned IOCTL_METEORGVIDEO;
extern unsigned IOCTL_METEORSBRIG;
extern unsigned IOCTL_METEORGBRIG;
extern unsigned IOCTL_METEORSCSAT;
extern unsigned IOCTL_METEORGCSAT;
extern unsigned IOCTL_METEORSCONT;
extern unsigned IOCTL_METEORGCONT;
extern unsigned IOCTL_METEORSHWS;
extern unsigned IOCTL_METEORGHWS;
extern unsigned IOCTL_METEORSVWS;
extern unsigned IOCTL_METEORGVWS;
extern unsigned IOCTL_METEORSTS;
extern unsigned IOCTL_METEORGTS;
extern unsigned IOCTL_TVTUNER_SETCHNL;
extern unsigned IOCTL_TVTUNER_GETCHNL;
extern unsigned IOCTL_TVTUNER_SETTYPE;
extern unsigned IOCTL_TVTUNER_GETTYPE;
extern unsigned IOCTL_TVTUNER_GETSTATUS;
extern unsigned IOCTL_TVTUNER_SETFREQ;
extern unsigned IOCTL_TVTUNER_GETFREQ;
extern unsigned IOCTL_TVTUNER_SETAFC;
extern unsigned IOCTL_TVTUNER_GETAFC;
extern unsigned IOCTL_RADIO_SETMODE;
extern unsigned IOCTL_RADIO_GETMODE;
extern unsigned IOCTL_RADIO_SETFREQ;
extern unsigned IOCTL_RADIO_GETFREQ;
extern unsigned IOCTL_METEORSACTPIXFMT;
extern unsigned IOCTL_METEORGACTPIXFMT;
extern unsigned IOCTL_METEORGSUPPIXFMT;
extern unsigned IOCTL_TVTUNER_GETCHNLSET;
extern unsigned IOCTL_REMOTE_GETKEY;
extern unsigned IOCTL_GDT_IOCTL_GENERAL;
extern unsigned IOCTL_GDT_IOCTL_DRVERS;
extern unsigned IOCTL_GDT_IOCTL_CTRTYPE;
extern unsigned IOCTL_GDT_IOCTL_OSVERS;
extern unsigned IOCTL_GDT_IOCTL_CTRCNT;
extern unsigned IOCTL_GDT_IOCTL_EVENT;
extern unsigned IOCTL_GDT_IOCTL_STATIST;
extern unsigned IOCTL_GDT_IOCTL_RESCAN;
extern unsigned IOCTL_ISP_SDBLEV;
extern unsigned IOCTL_ISP_RESETHBA;
extern unsigned IOCTL_ISP_RESCAN;
extern unsigned IOCTL_ISP_SETROLE;
extern unsigned IOCTL_ISP_GETROLE;
extern unsigned IOCTL_ISP_GET_STATS;
extern unsigned IOCTL_ISP_CLR_STATS;
extern unsigned IOCTL_ISP_FC_LIP;
extern unsigned IOCTL_ISP_FC_GETDINFO;
extern unsigned IOCTL_ISP_GET_FW_CRASH_DUMP;
extern unsigned IOCTL_ISP_FORCE_CRASH_DUMP;
extern unsigned IOCTL_ISP_FC_GETHINFO;
extern unsigned IOCTL_ISP_TSK_MGMT;
extern unsigned IOCTL_ISP_FC_GETDLIST;
extern unsigned IOCTL_MLXD_STATUS;
extern unsigned IOCTL_MLXD_CHECKASYNC;
extern unsigned IOCTL_MLXD_DETACH;
extern unsigned IOCTL_MLX_RESCAN_DRIVES;
extern unsigned IOCTL_MLX_PAUSE_CHANNEL;
extern unsigned IOCTL_MLX_COMMAND;
extern unsigned IOCTL_MLX_REBUILDASYNC;
extern unsigned IOCTL_MLX_REBUILDSTAT;
extern unsigned IOCTL_MLX_GET_SYSDRIVE;
extern unsigned IOCTL_MLX_GET_CINFO;
extern unsigned IOCTL_NVME_PASSTHROUGH_CMD;
extern unsigned IOCTL_FWCFGIO_SET_INDEX;
extern unsigned IOCTL_IRDA_RESET_PARAMS;
extern unsigned IOCTL_IRDA_SET_PARAMS;
extern unsigned IOCTL_IRDA_GET_SPEEDMASK;
extern unsigned IOCTL_IRDA_GET_TURNAROUNDMASK;
extern unsigned IOCTL_IRFRAMETTY_GET_DEVICE;
extern unsigned IOCTL_IRFRAMETTY_GET_DONGLE;
extern unsigned IOCTL_IRFRAMETTY_SET_DONGLE;
extern unsigned IOCTL_ISV_CMD;
extern unsigned IOCTL_WTQICMD;
extern unsigned IOCTL_ISCSI_GET_VERSION;
extern unsigned IOCTL_ISCSI_LOGIN;
extern unsigned IOCTL_ISCSI_LOGOUT;
extern unsigned IOCTL_ISCSI_ADD_CONNECTION;
extern unsigned IOCTL_ISCSI_RESTORE_CONNECTION;
extern unsigned IOCTL_ISCSI_REMOVE_CONNECTION;
extern unsigned IOCTL_ISCSI_CONNECTION_STATUS;
extern unsigned IOCTL_ISCSI_SEND_TARGETS;
extern unsigned IOCTL_ISCSI_SET_NODE_NAME;
extern unsigned IOCTL_ISCSI_IO_COMMAND;
extern unsigned IOCTL_ISCSI_REGISTER_EVENT;
extern unsigned IOCTL_ISCSI_DEREGISTER_EVENT;
extern unsigned IOCTL_ISCSI_WAIT_EVENT;
extern unsigned IOCTL_ISCSI_POLL_EVENT;
extern unsigned IOCTL_OFIOCGET;
extern unsigned IOCTL_OFIOCSET;
extern unsigned IOCTL_OFIOCNEXTPROP;
extern unsigned IOCTL_OFIOCGETOPTNODE;
extern unsigned IOCTL_OFIOCGETNEXT;
extern unsigned IOCTL_OFIOCGETCHILD;
extern unsigned IOCTL_OFIOCFINDDEVICE;
extern unsigned IOCTL_AMR_IO_VERSION;
extern unsigned IOCTL_AMR_IO_COMMAND;
extern unsigned IOCTL_MLYIO_COMMAND;
extern unsigned IOCTL_MLYIO_HEALTH;
extern unsigned IOCTL_PCI_IOC_CFGREAD;
extern unsigned IOCTL_PCI_IOC_CFGWRITE;
extern unsigned IOCTL_PCI_IOC_BDF_CFGREAD;
extern unsigned IOCTL_PCI_IOC_BDF_CFGWRITE;
extern unsigned IOCTL_PCI_IOC_BUSINFO;
extern unsigned IOCTL_PCI_IOC_DRVNAME;
extern unsigned IOCTL_PCI_IOC_DRVNAMEONBUS;
extern unsigned IOCTL_TWEIO_COMMAND;
extern unsigned IOCTL_TWEIO_STATS;
extern unsigned IOCTL_TWEIO_AEN_POLL;
extern unsigned IOCTL_TWEIO_AEN_WAIT;
extern unsigned IOCTL_TWEIO_SET_PARAM;
extern unsigned IOCTL_TWEIO_GET_PARAM;
extern unsigned IOCTL_TWEIO_RESET;
extern unsigned IOCTL_TWEIO_ADD_UNIT;
extern unsigned IOCTL_TWEIO_DEL_UNIT;
extern unsigned IOCTL_SIOCSCNWDOMAIN;
extern unsigned IOCTL_SIOCGCNWDOMAIN;
extern unsigned IOCTL_SIOCSCNWKEY;
extern unsigned IOCTL_SIOCGCNWSTATUS;
extern unsigned IOCTL_SIOCGCNWSTATS;
extern unsigned IOCTL_SIOCGCNWTRAIL;
extern unsigned IOCTL_SIOCGRAYSIGLEV;
extern unsigned IOCTL_RAIDFRAME_SHUTDOWN;
extern unsigned IOCTL_RAIDFRAME_TUR;
extern unsigned IOCTL_RAIDFRAME_FAIL_DISK;
extern unsigned IOCTL_RAIDFRAME_CHECK_RECON_STATUS;
extern unsigned IOCTL_RAIDFRAME_REWRITEPARITY;
extern unsigned IOCTL_RAIDFRAME_COPYBACK;
extern unsigned IOCTL_RAIDFRAME_SPARET_WAIT;
extern unsigned IOCTL_RAIDFRAME_SEND_SPARET;
extern unsigned IOCTL_RAIDFRAME_ABORT_SPARET_WAIT;
extern unsigned IOCTL_RAIDFRAME_START_ATRACE;
extern unsigned IOCTL_RAIDFRAME_STOP_ATRACE;
extern unsigned IOCTL_RAIDFRAME_GET_SIZE;
extern unsigned IOCTL_RAIDFRAME_RESET_ACCTOTALS;
extern unsigned IOCTL_RAIDFRAME_KEEP_ACCTOTALS;
extern unsigned IOCTL_RAIDFRAME_GET_COMPONENT_LABEL;
extern unsigned IOCTL_RAIDFRAME_SET_COMPONENT_LABEL;
extern unsigned IOCTL_RAIDFRAME_INIT_LABELS;
extern unsigned IOCTL_RAIDFRAME_ADD_HOT_SPARE;
extern unsigned IOCTL_RAIDFRAME_REMOVE_HOT_SPARE;
extern unsigned IOCTL_RAIDFRAME_REBUILD_IN_PLACE;
extern unsigned IOCTL_RAIDFRAME_CHECK_PARITY;
extern unsigned IOCTL_RAIDFRAME_CHECK_PARITYREWRITE_STATUS;
extern unsigned IOCTL_RAIDFRAME_CHECK_COPYBACK_STATUS;
extern unsigned IOCTL_RAIDFRAME_SET_AUTOCONFIG;
extern unsigned IOCTL_RAIDFRAME_SET_ROOT;
extern unsigned IOCTL_RAIDFRAME_DELETE_COMPONENT;
extern unsigned IOCTL_RAIDFRAME_INCORPORATE_HOT_SPARE;
extern unsigned IOCTL_RAIDFRAME_CHECK_RECON_STATUS_EXT;
extern unsigned IOCTL_RAIDFRAME_CHECK_PARITYREWRITE_STATUS_EXT;
extern unsigned IOCTL_RAIDFRAME_CHECK_COPYBACK_STATUS_EXT;
extern unsigned IOCTL_RAIDFRAME_CONFIGURE;
extern unsigned IOCTL_RAIDFRAME_GET_INFO;
extern unsigned IOCTL_RAIDFRAME_PARITYMAP_STATUS;
extern unsigned IOCTL_RAIDFRAME_PARITYMAP_GET_DISABLE;
extern unsigned IOCTL_RAIDFRAME_PARITYMAP_SET_DISABLE;
extern unsigned IOCTL_RAIDFRAME_PARITYMAP_SET_PARAMS;
extern unsigned IOCTL_RAIDFRAME_SET_LAST_UNIT;
extern unsigned IOCTL_MBPPIOCSPARAM;
extern unsigned IOCTL_MBPPIOCGPARAM;
extern unsigned IOCTL_MBPPIOCGSTAT;
extern unsigned IOCTL_SESIOC_GETNOBJ;
extern unsigned IOCTL_SESIOC_GETOBJMAP;
extern unsigned IOCTL_SESIOC_GETENCSTAT;
extern unsigned IOCTL_SESIOC_SETENCSTAT;
extern unsigned IOCTL_SESIOC_GETOBJSTAT;
extern unsigned IOCTL_SESIOC_SETOBJSTAT;
extern unsigned IOCTL_SESIOC_GETTEXT;
extern unsigned IOCTL_SESIOC_INIT;
extern unsigned IOCTL_SUN_DKIOCGGEOM;
extern unsigned IOCTL_SUN_DKIOCINFO;
extern unsigned IOCTL_SUN_DKIOCGPART;
extern unsigned IOCTL_FBIOGTYPE;
extern unsigned IOCTL_FBIOPUTCMAP;
extern unsigned IOCTL_FBIOGETCMAP;
extern unsigned IOCTL_FBIOGATTR;
extern unsigned IOCTL_FBIOSVIDEO;
extern unsigned IOCTL_FBIOGVIDEO;
extern unsigned IOCTL_FBIOSCURSOR;
extern unsigned IOCTL_FBIOGCURSOR;
extern unsigned IOCTL_FBIOSCURPOS;
extern unsigned IOCTL_FBIOGCURPOS;
extern unsigned IOCTL_FBIOGCURMAX;
extern unsigned IOCTL_KIOCTRANS;
extern unsigned IOCTL_KIOCSETKEY;
extern unsigned IOCTL_KIOCGETKEY;
extern unsigned IOCTL_KIOCGTRANS;
extern unsigned IOCTL_KIOCCMD;
extern unsigned IOCTL_KIOCTYPE;
extern unsigned IOCTL_KIOCSDIRECT;
extern unsigned IOCTL_KIOCSKEY;
extern unsigned IOCTL_KIOCGKEY;
extern unsigned IOCTL_KIOCSLED;
extern unsigned IOCTL_KIOCGLED;
extern unsigned IOCTL_KIOCLAYOUT;
extern unsigned IOCTL_VUIDSFORMAT;
extern unsigned IOCTL_VUIDGFORMAT;
extern unsigned IOCTL_STICIO_GXINFO;
extern unsigned IOCTL_STICIO_RESET;
extern unsigned IOCTL_STICIO_STARTQ;
extern unsigned IOCTL_STICIO_STOPQ;
extern unsigned IOCTL_UKYOPON_IDENTIFY;
extern unsigned IOCTL_URIO_SEND_COMMAND;
extern unsigned IOCTL_URIO_RECV_COMMAND;
extern unsigned IOCTL_USB_REQUEST;
extern unsigned IOCTL_USB_SETDEBUG;
extern unsigned IOCTL_USB_DISCOVER;
extern unsigned IOCTL_USB_DEVICEINFO;
extern unsigned IOCTL_USB_DEVICEINFO_OLD;
extern unsigned IOCTL_USB_DEVICESTATS;
extern unsigned IOCTL_USB_GET_REPORT_DESC;
extern unsigned IOCTL_USB_SET_IMMED;
extern unsigned IOCTL_USB_GET_REPORT;
extern unsigned IOCTL_USB_SET_REPORT;
extern unsigned IOCTL_USB_GET_REPORT_ID;
extern unsigned IOCTL_USB_GET_CONFIG;
extern unsigned IOCTL_USB_SET_CONFIG;
extern unsigned IOCTL_USB_GET_ALTINTERFACE;
extern unsigned IOCTL_USB_SET_ALTINTERFACE;
extern unsigned IOCTL_USB_GET_NO_ALT;
extern unsigned IOCTL_USB_GET_DEVICE_DESC;
extern unsigned IOCTL_USB_GET_CONFIG_DESC;
extern unsigned IOCTL_USB_GET_INTERFACE_DESC;
extern unsigned IOCTL_USB_GET_ENDPOINT_DESC;
extern unsigned IOCTL_USB_GET_FULL_DESC;
extern unsigned IOCTL_USB_GET_STRING_DESC;
extern unsigned IOCTL_USB_DO_REQUEST;
extern unsigned IOCTL_USB_GET_DEVICEINFO;
extern unsigned IOCTL_USB_GET_DEVICEINFO_OLD;
extern unsigned IOCTL_USB_SET_SHORT_XFER;
extern unsigned IOCTL_USB_SET_TIMEOUT;
extern unsigned IOCTL_USB_SET_BULK_RA;
extern unsigned IOCTL_USB_SET_BULK_WB;
extern unsigned IOCTL_USB_SET_BULK_RA_OPT;
extern unsigned IOCTL_USB_SET_BULK_WB_OPT;
extern unsigned IOCTL_USB_GET_CM_OVER_DATA;
extern unsigned IOCTL_USB_SET_CM_OVER_DATA;
extern unsigned IOCTL_UTOPPYIOTURBO;
extern unsigned IOCTL_UTOPPYIOCANCEL;
extern unsigned IOCTL_UTOPPYIOREBOOT;
extern unsigned IOCTL_UTOPPYIOSTATS;
extern unsigned IOCTL_UTOPPYIORENAME;
extern unsigned IOCTL_UTOPPYIOMKDIR;
extern unsigned IOCTL_UTOPPYIODELETE;
extern unsigned IOCTL_UTOPPYIOREADDIR;
extern unsigned IOCTL_UTOPPYIOREADFILE;
extern unsigned IOCTL_UTOPPYIOWRITEFILE;
extern unsigned IOCTL_DIOSXDCMD;
extern unsigned IOCTL_VT_OPENQRY;
extern unsigned IOCTL_VT_SETMODE;
extern unsigned IOCTL_VT_GETMODE;
extern unsigned IOCTL_VT_RELDISP;
extern unsigned IOCTL_VT_ACTIVATE;
extern unsigned IOCTL_VT_WAITACTIVE;
extern unsigned IOCTL_VT_GETACTIVE;
extern unsigned IOCTL_VT_GETSTATE;
extern unsigned IOCTL_KDGETKBENT;
extern unsigned IOCTL_KDGKBMODE;
extern unsigned IOCTL_KDSKBMODE;
extern unsigned IOCTL_KDMKTONE;
extern unsigned IOCTL_KDSETMODE;
extern unsigned IOCTL_KDENABIO;
extern unsigned IOCTL_KDDISABIO;
extern unsigned IOCTL_KDGKBTYPE;
extern unsigned IOCTL_KDGETLED;
extern unsigned IOCTL_KDSETLED;
extern unsigned IOCTL_KDSETRAD;
extern unsigned IOCTL_VGAPCVTID;
extern unsigned IOCTL_CONS_GETVERS;
extern unsigned IOCTL_WSKBDIO_GTYPE;
extern unsigned IOCTL_WSKBDIO_BELL;
extern unsigned IOCTL_WSKBDIO_COMPLEXBELL;
extern unsigned IOCTL_WSKBDIO_SETBELL;
extern unsigned IOCTL_WSKBDIO_GETBELL;
extern unsigned IOCTL_WSKBDIO_SETDEFAULTBELL;
extern unsigned IOCTL_WSKBDIO_GETDEFAULTBELL;
extern unsigned IOCTL_WSKBDIO_SETKEYREPEAT;
extern unsigned IOCTL_WSKBDIO_GETKEYREPEAT;
extern unsigned IOCTL_WSKBDIO_SETDEFAULTKEYREPEAT;
extern unsigned IOCTL_WSKBDIO_GETDEFAULTKEYREPEAT;
extern unsigned IOCTL_WSKBDIO_SETLEDS;
extern unsigned IOCTL_WSKBDIO_GETLEDS;
extern unsigned IOCTL_WSKBDIO_GETMAP;
extern unsigned IOCTL_WSKBDIO_SETMAP;
extern unsigned IOCTL_WSKBDIO_GETENCODING;
extern unsigned IOCTL_WSKBDIO_SETENCODING;
extern unsigned IOCTL_WSKBDIO_SETMODE;
extern unsigned IOCTL_WSKBDIO_GETMODE;
extern unsigned IOCTL_WSKBDIO_SETKEYCLICK;
extern unsigned IOCTL_WSKBDIO_GETKEYCLICK;
extern unsigned IOCTL_WSKBDIO_GETSCROLL;
extern unsigned IOCTL_WSKBDIO_SETSCROLL;
extern unsigned IOCTL_WSKBDIO_SETVERSION;
extern unsigned IOCTL_WSMOUSEIO_GTYPE;
extern unsigned IOCTL_WSMOUSEIO_SRES;
extern unsigned IOCTL_WSMOUSEIO_SSCALE;
extern unsigned IOCTL_WSMOUSEIO_SRATE;
extern unsigned IOCTL_WSMOUSEIO_SCALIBCOORDS;
extern unsigned IOCTL_WSMOUSEIO_GCALIBCOORDS;
extern unsigned IOCTL_WSMOUSEIO_GETID;
extern unsigned IOCTL_WSMOUSEIO_GETREPEAT;
extern unsigned IOCTL_WSMOUSEIO_SETREPEAT;
extern unsigned IOCTL_WSMOUSEIO_SETVERSION;
extern unsigned IOCTL_WSDISPLAYIO_GTYPE;
extern unsigned IOCTL_WSDISPLAYIO_GINFO;
extern unsigned IOCTL_WSDISPLAYIO_GETCMAP;
extern unsigned IOCTL_WSDISPLAYIO_PUTCMAP;
extern unsigned IOCTL_WSDISPLAYIO_GVIDEO;
extern unsigned IOCTL_WSDISPLAYIO_SVIDEO;
extern unsigned IOCTL_WSDISPLAYIO_GCURPOS;
extern unsigned IOCTL_WSDISPLAYIO_SCURPOS;
extern unsigned IOCTL_WSDISPLAYIO_GCURMAX;
extern unsigned IOCTL_WSDISPLAYIO_GCURSOR;
extern unsigned IOCTL_WSDISPLAYIO_SCURSOR;
extern unsigned IOCTL_WSDISPLAYIO_GMODE;
extern unsigned IOCTL_WSDISPLAYIO_SMODE;
extern unsigned IOCTL_WSDISPLAYIO_LDFONT;
extern unsigned IOCTL_WSDISPLAYIO_ADDSCREEN;
extern unsigned IOCTL_WSDISPLAYIO_DELSCREEN;
extern unsigned IOCTL_WSDISPLAYIO_SFONT;
extern unsigned IOCTL__O_WSDISPLAYIO_SETKEYBOARD;
extern unsigned IOCTL_WSDISPLAYIO_GETPARAM;
extern unsigned IOCTL_WSDISPLAYIO_SETPARAM;
extern unsigned IOCTL_WSDISPLAYIO_GETACTIVESCREEN;
extern unsigned IOCTL_WSDISPLAYIO_GETWSCHAR;
extern unsigned IOCTL_WSDISPLAYIO_PUTWSCHAR;
extern unsigned IOCTL_WSDISPLAYIO_DGSCROLL;
extern unsigned IOCTL_WSDISPLAYIO_DSSCROLL;
extern unsigned IOCTL_WSDISPLAYIO_GMSGATTRS;
extern unsigned IOCTL_WSDISPLAYIO_SMSGATTRS;
extern unsigned IOCTL_WSDISPLAYIO_GBORDER;
extern unsigned IOCTL_WSDISPLAYIO_SBORDER;
extern unsigned IOCTL_WSDISPLAYIO_SSPLASH;
extern unsigned IOCTL_WSDISPLAYIO_SPROGRESS;
extern unsigned IOCTL_WSDISPLAYIO_LINEBYTES;
extern unsigned IOCTL_WSDISPLAYIO_SETVERSION;
extern unsigned IOCTL_WSMUXIO_ADD_DEVICE;
extern unsigned IOCTL_WSMUXIO_REMOVE_DEVICE;
extern unsigned IOCTL_WSMUXIO_LIST_DEVICES;
extern unsigned IOCTL_WSMUXIO_INJECTEVENT;
extern unsigned IOCTL_WSDISPLAYIO_GET_BUSID;
extern unsigned IOCTL_WSDISPLAYIO_GET_EDID;
extern unsigned IOCTL_WSDISPLAYIO_SET_POLLING;
extern unsigned IOCTL_WSDISPLAYIO_GET_FBINFO;
extern unsigned IOCTL_WSDISPLAYIO_DOBLIT;
extern unsigned IOCTL_WSDISPLAYIO_WAITBLIT;
extern unsigned IOCTL_BIOCLOCATE;
extern unsigned IOCTL_BIOCINQ;
extern unsigned IOCTL_BIOCDISK_NOVOL;
extern unsigned IOCTL_BIOCDISK;
extern unsigned IOCTL_BIOCVOL;
extern unsigned IOCTL_BIOCALARM;
extern unsigned IOCTL_BIOCBLINK;
extern unsigned IOCTL_BIOCSETSTATE;
extern unsigned IOCTL_BIOCVOLOPS;
extern unsigned IOCTL_MD_GETCONF;
extern unsigned IOCTL_MD_SETCONF;
extern unsigned IOCTL_CCDIOCSET;
extern unsigned IOCTL_CCDIOCCLR;
extern unsigned IOCTL_CGDIOCSET;
extern unsigned IOCTL_CGDIOCCLR;
extern unsigned IOCTL_CGDIOCGET;
extern unsigned IOCTL_FSSIOCSET;
extern unsigned IOCTL_FSSIOCGET;
extern unsigned IOCTL_FSSIOCCLR;
extern unsigned IOCTL_FSSIOFSET;
extern unsigned IOCTL_FSSIOFGET;
extern unsigned IOCTL_BTDEV_ATTACH;
extern unsigned IOCTL_BTDEV_DETACH;
extern unsigned IOCTL_BTSCO_GETINFO;
extern unsigned IOCTL_KTTCP_IO_SEND;
extern unsigned IOCTL_KTTCP_IO_RECV;
extern unsigned IOCTL_IOC_LOCKSTAT_GVERSION;
extern unsigned IOCTL_IOC_LOCKSTAT_ENABLE;
extern unsigned IOCTL_IOC_LOCKSTAT_DISABLE;
extern unsigned IOCTL_VNDIOCSET;
extern unsigned IOCTL_VNDIOCCLR;
extern unsigned IOCTL_VNDIOCGET;
extern unsigned IOCTL_SPKRTONE;
extern unsigned IOCTL_SPKRTUNE;
extern unsigned IOCTL_SPKRGETVOL;
extern unsigned IOCTL_SPKRSETVOL;
#if 0 /* interfaces are WIP */
extern unsigned IOCTL_NVMM_IOC_CAPABILITY;
extern unsigned IOCTL_NVMM_IOC_MACHINE_CREATE;
extern unsigned IOCTL_NVMM_IOC_MACHINE_DESTROY;
extern unsigned IOCTL_NVMM_IOC_MACHINE_CONFIGURE;
extern unsigned IOCTL_NVMM_IOC_VCPU_CREATE;
extern unsigned IOCTL_NVMM_IOC_VCPU_DESTROY;
extern unsigned IOCTL_NVMM_IOC_VCPU_SETSTATE;
extern unsigned IOCTL_NVMM_IOC_VCPU_GETSTATE;
extern unsigned IOCTL_NVMM_IOC_VCPU_INJECT;
extern unsigned IOCTL_NVMM_IOC_VCPU_RUN;
extern unsigned IOCTL_NVMM_IOC_GPA_MAP;
extern unsigned IOCTL_NVMM_IOC_GPA_UNMAP;
extern unsigned IOCTL_NVMM_IOC_HVA_MAP;
extern unsigned IOCTL_NVMM_IOC_HVA_UNMAP;
#endif
extern unsigned IOCTL_AUTOFSREQUEST;
extern unsigned IOCTL_AUTOFSDONE;
extern unsigned IOCTL_BIOCGBLEN;
extern unsigned IOCTL_BIOCSBLEN;
extern unsigned IOCTL_BIOCSETF;
extern unsigned IOCTL_BIOCFLUSH;
extern unsigned IOCTL_BIOCPROMISC;
extern unsigned IOCTL_BIOCGDLT;
extern unsigned IOCTL_BIOCGETIF;
extern unsigned IOCTL_BIOCSETIF;
extern unsigned IOCTL_BIOCGSTATS;
extern unsigned IOCTL_BIOCGSTATSOLD;
extern unsigned IOCTL_BIOCIMMEDIATE;
extern unsigned IOCTL_BIOCVERSION;
extern unsigned IOCTL_BIOCSTCPF;
extern unsigned IOCTL_BIOCSUDPF;
extern unsigned IOCTL_BIOCGHDRCMPLT;
extern unsigned IOCTL_BIOCSHDRCMPLT;
extern unsigned IOCTL_BIOCSDLT;
extern unsigned IOCTL_BIOCGDLTLIST;
extern unsigned IOCTL_BIOCGDIRECTION;
extern unsigned IOCTL_BIOCSDIRECTION;
extern unsigned IOCTL_BIOCSRTIMEOUT;
extern unsigned IOCTL_BIOCGRTIMEOUT;
extern unsigned IOCTL_BIOCGFEEDBACK;
extern unsigned IOCTL_BIOCSFEEDBACK;
extern unsigned IOCTL_GRESADDRS;
extern unsigned IOCTL_GRESADDRD;
extern unsigned IOCTL_GREGADDRS;
extern unsigned IOCTL_GREGADDRD;
extern unsigned IOCTL_GRESPROTO;
extern unsigned IOCTL_GREGPROTO;
extern unsigned IOCTL_GRESSOCK;
extern unsigned IOCTL_GREDSOCK;
extern unsigned IOCTL_PPPIOCGRAWIN;
extern unsigned IOCTL_PPPIOCGFLAGS;
extern unsigned IOCTL_PPPIOCSFLAGS;
extern unsigned IOCTL_PPPIOCGASYNCMAP;
extern unsigned IOCTL_PPPIOCSASYNCMAP;
extern unsigned IOCTL_PPPIOCGUNIT;
extern unsigned IOCTL_PPPIOCGRASYNCMAP;
extern unsigned IOCTL_PPPIOCSRASYNCMAP;
extern unsigned IOCTL_PPPIOCGMRU;
extern unsigned IOCTL_PPPIOCSMRU;
extern unsigned IOCTL_PPPIOCSMAXCID;
extern unsigned IOCTL_PPPIOCGXASYNCMAP;
extern unsigned IOCTL_PPPIOCSXASYNCMAP;
extern unsigned IOCTL_PPPIOCXFERUNIT;
extern unsigned IOCTL_PPPIOCSCOMPRESS;
extern unsigned IOCTL_PPPIOCGNPMODE;
extern unsigned IOCTL_PPPIOCSNPMODE;
extern unsigned IOCTL_PPPIOCGIDLE;
extern unsigned IOCTL_PPPIOCGMTU;
extern unsigned IOCTL_PPPIOCSMTU;
extern unsigned IOCTL_SIOCGPPPSTATS;
extern unsigned IOCTL_SIOCGPPPCSTATS;
extern unsigned IOCTL_IOC_NPF_VERSION;
extern unsigned IOCTL_IOC_NPF_SWITCH;
extern unsigned IOCTL_IOC_NPF_LOAD;
extern unsigned IOCTL_IOC_NPF_TABLE;
extern unsigned IOCTL_IOC_NPF_STATS;
extern unsigned IOCTL_IOC_NPF_SAVE;
extern unsigned IOCTL_IOC_NPF_RULE;
extern unsigned IOCTL_IOC_NPF_CONN_LOOKUP;
extern unsigned IOCTL_PPPOESETPARMS;
extern unsigned IOCTL_PPPOEGETPARMS;
extern unsigned IOCTL_PPPOEGETSESSION;
extern unsigned IOCTL_SPPPGETAUTHCFG;
extern unsigned IOCTL_SPPPSETAUTHCFG;
extern unsigned IOCTL_SPPPGETLCPCFG;
extern unsigned IOCTL_SPPPSETLCPCFG;
extern unsigned IOCTL_SPPPGETSTATUS;
extern unsigned IOCTL_SPPPGETSTATUSNCP;
extern unsigned IOCTL_SPPPGETIDLETO;
extern unsigned IOCTL_SPPPSETIDLETO;
extern unsigned IOCTL_SPPPGETAUTHFAILURES;
extern unsigned IOCTL_SPPPSETAUTHFAILURE;
extern unsigned IOCTL_SPPPSETDNSOPTS;
extern unsigned IOCTL_SPPPGETDNSOPTS;
extern unsigned IOCTL_SPPPGETDNSADDRS;
extern unsigned IOCTL_SPPPSETKEEPALIVE;
extern unsigned IOCTL_SPPPGETKEEPALIVE;
extern unsigned IOCTL_SRT_GETNRT;
extern unsigned IOCTL_SRT_GETRT;
extern unsigned IOCTL_SRT_SETRT;
extern unsigned IOCTL_SRT_DELRT;
extern unsigned IOCTL_SRT_SFLAGS;
extern unsigned IOCTL_SRT_GFLAGS;
extern unsigned IOCTL_SRT_SGFLAGS;
extern unsigned IOCTL_SRT_DEBUG;
extern unsigned IOCTL_TAPGIFNAME;
extern unsigned IOCTL_TUNSDEBUG;
extern unsigned IOCTL_TUNGDEBUG;
extern unsigned IOCTL_TUNSIFMODE;
extern unsigned IOCTL_TUNSLMODE;
extern unsigned IOCTL_TUNSIFHEAD;
extern unsigned IOCTL_TUNGIFHEAD;
extern unsigned IOCTL_DIOCSTART;
extern unsigned IOCTL_DIOCSTOP;
extern unsigned IOCTL_DIOCADDRULE;
extern unsigned IOCTL_DIOCGETRULES;
extern unsigned IOCTL_DIOCGETRULE;
extern unsigned IOCTL_DIOCSETLCK;
extern unsigned IOCTL_DIOCCLRSTATES;
extern unsigned IOCTL_DIOCGETSTATE;
extern unsigned IOCTL_DIOCSETSTATUSIF;
extern unsigned IOCTL_DIOCGETSTATUS;
extern unsigned IOCTL_DIOCCLRSTATUS;
extern unsigned IOCTL_DIOCNATLOOK;
extern unsigned IOCTL_DIOCSETDEBUG;
extern unsigned IOCTL_DIOCGETSTATES;
extern unsigned IOCTL_DIOCCHANGERULE;
extern unsigned IOCTL_DIOCSETTIMEOUT;
extern unsigned IOCTL_DIOCGETTIMEOUT;
extern unsigned IOCTL_DIOCADDSTATE;
extern unsigned IOCTL_DIOCCLRRULECTRS;
extern unsigned IOCTL_DIOCGETLIMIT;
extern unsigned IOCTL_DIOCSETLIMIT;
extern unsigned IOCTL_DIOCKILLSTATES;
extern unsigned IOCTL_DIOCSTARTALTQ;
extern unsigned IOCTL_DIOCSTOPALTQ;
extern unsigned IOCTL_DIOCADDALTQ;
extern unsigned IOCTL_DIOCGETALTQS;
extern unsigned IOCTL_DIOCGETALTQ;
extern unsigned IOCTL_DIOCCHANGEALTQ;
extern unsigned IOCTL_DIOCGETQSTATS;
extern unsigned IOCTL_DIOCBEGINADDRS;
extern unsigned IOCTL_DIOCADDADDR;
extern unsigned IOCTL_DIOCGETADDRS;
extern unsigned IOCTL_DIOCGETADDR;
extern unsigned IOCTL_DIOCCHANGEADDR;
extern unsigned IOCTL_DIOCADDSTATES;
extern unsigned IOCTL_DIOCGETRULESETS;
extern unsigned IOCTL_DIOCGETRULESET;
extern unsigned IOCTL_DIOCRCLRTABLES;
extern unsigned IOCTL_DIOCRADDTABLES;
extern unsigned IOCTL_DIOCRDELTABLES;
extern unsigned IOCTL_DIOCRGETTABLES;
extern unsigned IOCTL_DIOCRGETTSTATS;
extern unsigned IOCTL_DIOCRCLRTSTATS;
extern unsigned IOCTL_DIOCRCLRADDRS;
extern unsigned IOCTL_DIOCRADDADDRS;
extern unsigned IOCTL_DIOCRDELADDRS;
extern unsigned IOCTL_DIOCRSETADDRS;
extern unsigned IOCTL_DIOCRGETADDRS;
extern unsigned IOCTL_DIOCRGETASTATS;
extern unsigned IOCTL_DIOCRCLRASTATS;
extern unsigned IOCTL_DIOCRTSTADDRS;
extern unsigned IOCTL_DIOCRSETTFLAGS;
extern unsigned IOCTL_DIOCRINADEFINE;
extern unsigned IOCTL_DIOCOSFPFLUSH;
extern unsigned IOCTL_DIOCOSFPADD;
extern unsigned IOCTL_DIOCOSFPGET;
extern unsigned IOCTL_DIOCXBEGIN;
extern unsigned IOCTL_DIOCXCOMMIT;
extern unsigned IOCTL_DIOCXROLLBACK;
extern unsigned IOCTL_DIOCGETSRCNODES;
extern unsigned IOCTL_DIOCCLRSRCNODES;
extern unsigned IOCTL_DIOCSETHOSTID;
extern unsigned IOCTL_DIOCIGETIFACES;
extern unsigned IOCTL_DIOCSETIFFLAG;
extern unsigned IOCTL_DIOCCLRIFFLAG;
extern unsigned IOCTL_DIOCKILLSRCNODES;
extern unsigned IOCTL_SLIOCGUNIT;
extern unsigned IOCTL_SIOCGBTINFO;
extern unsigned IOCTL_SIOCGBTINFOA;
extern unsigned IOCTL_SIOCNBTINFO;
extern unsigned IOCTL_SIOCSBTFLAGS;
extern unsigned IOCTL_SIOCSBTPOLICY;
extern unsigned IOCTL_SIOCSBTPTYPE;
extern unsigned IOCTL_SIOCGBTSTATS;
extern unsigned IOCTL_SIOCZBTSTATS;
extern unsigned IOCTL_SIOCBTDUMP;
extern unsigned IOCTL_SIOCSBTSCOMTU;
extern unsigned IOCTL_SIOCGBTFEAT;
extern unsigned IOCTL_SIOCADNAT;
extern unsigned IOCTL_SIOCRMNAT;
extern unsigned IOCTL_SIOCGNATS;
extern unsigned IOCTL_SIOCGNATL;
extern unsigned IOCTL_SIOCPURGENAT;
extern unsigned IOCTL_SIOCCONNECTX;
extern unsigned IOCTL_SIOCCONNECTXDEL;
extern unsigned IOCTL_SIOCSIFINFO_FLAGS;
extern unsigned IOCTL_SIOCAADDRCTL_POLICY;
extern unsigned IOCTL_SIOCDADDRCTL_POLICY;
extern unsigned IOCTL_SMBIOC_OPENSESSION;
extern unsigned IOCTL_SMBIOC_OPENSHARE;
extern unsigned IOCTL_SMBIOC_REQUEST;
extern unsigned IOCTL_SMBIOC_SETFLAGS;
extern unsigned IOCTL_SMBIOC_LOOKUP;
extern unsigned IOCTL_SMBIOC_READ;
extern unsigned IOCTL_SMBIOC_WRITE;
extern unsigned IOCTL_AGPIOC_INFO;
extern unsigned IOCTL_AGPIOC_ACQUIRE;
extern unsigned IOCTL_AGPIOC_RELEASE;
extern unsigned IOCTL_AGPIOC_SETUP;
extern unsigned IOCTL_AGPIOC_ALLOCATE;
extern unsigned IOCTL_AGPIOC_DEALLOCATE;
extern unsigned IOCTL_AGPIOC_BIND;
extern unsigned IOCTL_AGPIOC_UNBIND;
extern unsigned IOCTL_AUDIO_GETINFO;
extern unsigned IOCTL_AUDIO_SETINFO;
extern unsigned IOCTL_AUDIO_DRAIN;
extern unsigned IOCTL_AUDIO_FLUSH;
extern unsigned IOCTL_AUDIO_WSEEK;
extern unsigned IOCTL_AUDIO_RERROR;
extern unsigned IOCTL_AUDIO_GETDEV;
extern unsigned IOCTL_AUDIO_GETENC;
extern unsigned IOCTL_AUDIO_GETFD;
extern unsigned IOCTL_AUDIO_SETFD;
extern unsigned IOCTL_AUDIO_PERROR;
extern unsigned IOCTL_AUDIO_GETIOFFS;
extern unsigned IOCTL_AUDIO_GETOOFFS;
extern unsigned IOCTL_AUDIO_GETPROPS;
extern unsigned IOCTL_AUDIO_GETBUFINFO;
extern unsigned IOCTL_AUDIO_SETCHAN;
extern unsigned IOCTL_AUDIO_GETCHAN;
extern unsigned IOCTL_AUDIO_MIXER_READ;
extern unsigned IOCTL_AUDIO_MIXER_WRITE;
extern unsigned IOCTL_AUDIO_MIXER_DEVINFO;
extern unsigned IOCTL_ATAIOCCOMMAND;
extern unsigned IOCTL_ATABUSIOSCAN;
extern unsigned IOCTL_ATABUSIORESET;
extern unsigned IOCTL_ATABUSIODETACH;
extern unsigned IOCTL_CDIOCPLAYTRACKS;
extern unsigned IOCTL_CDIOCPLAYBLOCKS;
extern unsigned IOCTL_CDIOCREADSUBCHANNEL;
extern unsigned IOCTL_CDIOREADTOCHEADER;
extern unsigned IOCTL_CDIOREADTOCENTRIES;
extern unsigned IOCTL_CDIOREADMSADDR;
extern unsigned IOCTL_CDIOCSETPATCH;
extern unsigned IOCTL_CDIOCGETVOL;
extern unsigned IOCTL_CDIOCSETVOL;
extern unsigned IOCTL_CDIOCSETMONO;
extern unsigned IOCTL_CDIOCSETSTEREO;
extern unsigned IOCTL_CDIOCSETMUTE;
extern unsigned IOCTL_CDIOCSETLEFT;
extern unsigned IOCTL_CDIOCSETRIGHT;
extern unsigned IOCTL_CDIOCSETDEBUG;
extern unsigned IOCTL_CDIOCCLRDEBUG;
extern unsigned IOCTL_CDIOCPAUSE;
extern unsigned IOCTL_CDIOCRESUME;
extern unsigned IOCTL_CDIOCRESET;
extern unsigned IOCTL_CDIOCSTART;
extern unsigned IOCTL_CDIOCSTOP;
extern unsigned IOCTL_CDIOCEJECT;
extern unsigned IOCTL_CDIOCALLOW;
extern unsigned IOCTL_CDIOCPREVENT;
extern unsigned IOCTL_CDIOCCLOSE;
extern unsigned IOCTL_CDIOCPLAYMSF;
extern unsigned IOCTL_CDIOCLOADUNLOAD;
extern unsigned IOCTL_CHIOMOVE;
extern unsigned IOCTL_CHIOEXCHANGE;
extern unsigned IOCTL_CHIOPOSITION;
extern unsigned IOCTL_CHIOGPICKER;
extern unsigned IOCTL_CHIOSPICKER;
extern unsigned IOCTL_CHIOGPARAMS;
extern unsigned IOCTL_CHIOIELEM;
extern unsigned IOCTL_OCHIOGSTATUS;
extern unsigned IOCTL_CHIOGSTATUS;
extern unsigned IOCTL_CHIOSVOLTAG;
extern unsigned IOCTL_CLOCKCTL_SETTIMEOFDAY;
extern unsigned IOCTL_CLOCKCTL_ADJTIME;
extern unsigned IOCTL_CLOCKCTL_CLOCK_SETTIME;
extern unsigned IOCTL_CLOCKCTL_NTP_ADJTIME;
extern unsigned IOCTL_IOC_CPU_SETSTATE;
extern unsigned IOCTL_IOC_CPU_GETSTATE;
extern unsigned IOCTL_IOC_CPU_GETCOUNT;
extern unsigned IOCTL_IOC_CPU_MAPID;
extern unsigned IOCTL_IOC_CPU_UCODE_GET_VERSION;
extern unsigned IOCTL_IOC_CPU_UCODE_APPLY;
extern unsigned IOCTL_DIOCGDINFO;
extern unsigned IOCTL_DIOCSDINFO;
extern unsigned IOCTL_DIOCWDINFO;
extern unsigned IOCTL_DIOCRFORMAT;
extern unsigned IOCTL_DIOCWFORMAT;
extern unsigned IOCTL_DIOCSSTEP;
extern unsigned IOCTL_DIOCSRETRIES;
extern unsigned IOCTL_DIOCKLABEL;
extern unsigned IOCTL_DIOCWLABEL;
extern unsigned IOCTL_DIOCSBAD;
extern unsigned IOCTL_DIOCEJECT;
extern unsigned IOCTL_ODIOCEJECT;
extern unsigned IOCTL_DIOCLOCK;
extern unsigned IOCTL_DIOCGDEFLABEL;
extern unsigned IOCTL_DIOCCLRLABEL;
extern unsigned IOCTL_DIOCGCACHE;
extern unsigned IOCTL_DIOCSCACHE;
extern unsigned IOCTL_DIOCCACHESYNC;
extern unsigned IOCTL_DIOCBSLIST;
extern unsigned IOCTL_DIOCBSFLUSH;
extern unsigned IOCTL_DIOCAWEDGE;
extern unsigned IOCTL_DIOCGWEDGEINFO;
extern unsigned IOCTL_DIOCDWEDGE;
extern unsigned IOCTL_DIOCLWEDGES;
extern unsigned IOCTL_DIOCGSTRATEGY;
extern unsigned IOCTL_DIOCSSTRATEGY;
extern unsigned IOCTL_DIOCGDISKINFO;
extern unsigned IOCTL_DIOCTUR;
extern unsigned IOCTL_DIOCMWEDGES;
extern unsigned IOCTL_DIOCGSECTORSIZE;
extern unsigned IOCTL_DIOCGMEDIASIZE;
extern unsigned IOCTL_DRVDETACHDEV;
extern unsigned IOCTL_DRVRESCANBUS;
extern unsigned IOCTL_DRVCTLCOMMAND;
extern unsigned IOCTL_DRVRESUMEDEV;
extern unsigned IOCTL_DRVLISTDEV;
extern unsigned IOCTL_DRVGETEVENT;
extern unsigned IOCTL_DRVSUSPENDDEV;
extern unsigned IOCTL_DVD_READ_STRUCT;
extern unsigned IOCTL_DVD_WRITE_STRUCT;
extern unsigned IOCTL_DVD_AUTH;
extern unsigned IOCTL_ENVSYS_GETDICTIONARY;
extern unsigned IOCTL_ENVSYS_SETDICTIONARY;
extern unsigned IOCTL_ENVSYS_REMOVEPROPS;
extern unsigned IOCTL_ENVSYS_GTREDATA;
extern unsigned IOCTL_ENVSYS_GTREINFO;
extern unsigned IOCTL_KFILTER_BYFILTER;
extern unsigned IOCTL_KFILTER_BYNAME;
extern unsigned IOCTL_FDIOCGETOPTS;
extern unsigned IOCTL_FDIOCSETOPTS;
extern unsigned IOCTL_FDIOCSETFORMAT;
extern unsigned IOCTL_FDIOCGETFORMAT;
extern unsigned IOCTL_FDIOCFORMAT_TRACK;
extern unsigned IOCTL_FIOCLEX;
extern unsigned IOCTL_FIONCLEX;
extern unsigned IOCTL_FIOSEEKDATA;
extern unsigned IOCTL_FIOSEEKHOLE;
extern unsigned IOCTL_FIONREAD;
extern unsigned IOCTL_FIONBIO;
extern unsigned IOCTL_FIOASYNC;
extern unsigned IOCTL_FIOSETOWN;
extern unsigned IOCTL_FIOGETOWN;
extern unsigned IOCTL_OFIOGETBMAP;
extern unsigned IOCTL_FIOGETBMAP;
extern unsigned IOCTL_FIONWRITE;
extern unsigned IOCTL_FIONSPACE;
extern unsigned IOCTL_GPIOINFO;
extern unsigned IOCTL_GPIOSET;
extern unsigned IOCTL_GPIOUNSET;
extern unsigned IOCTL_GPIOREAD;
extern unsigned IOCTL_GPIOWRITE;
extern unsigned IOCTL_GPIOTOGGLE;
extern unsigned IOCTL_GPIOATTACH;
extern unsigned IOCTL_PTIOCNETBSD;
extern unsigned IOCTL_PTIOCSUNOS;
extern unsigned IOCTL_PTIOCLINUX;
extern unsigned IOCTL_PTIOCFREEBSD;
extern unsigned IOCTL_PTIOCULTRIX;
extern unsigned IOCTL_TIOCHPCL;
extern unsigned IOCTL_TIOCGETP;
extern unsigned IOCTL_TIOCSETP;
extern unsigned IOCTL_TIOCSETN;
extern unsigned IOCTL_TIOCSETC;
extern unsigned IOCTL_TIOCGETC;
extern unsigned IOCTL_TIOCLBIS;
extern unsigned IOCTL_TIOCLBIC;
extern unsigned IOCTL_TIOCLSET;
extern unsigned IOCTL_TIOCLGET;
extern unsigned IOCTL_TIOCSLTC;
extern unsigned IOCTL_TIOCGLTC;
extern unsigned IOCTL_OTIOCCONS;
extern unsigned IOCTL_JOY_SETTIMEOUT;
extern unsigned IOCTL_JOY_GETTIMEOUT;
extern unsigned IOCTL_JOY_SET_X_OFFSET;
extern unsigned IOCTL_JOY_SET_Y_OFFSET;
extern unsigned IOCTL_JOY_GET_X_OFFSET;
extern unsigned IOCTL_JOY_GET_Y_OFFSET;
extern unsigned IOCTL_OKIOCGSYMBOL;
extern unsigned IOCTL_OKIOCGVALUE;
extern unsigned IOCTL_KIOCGSIZE;
extern unsigned IOCTL_KIOCGVALUE;
extern unsigned IOCTL_KIOCGSYMBOL;
extern unsigned IOCTL_LUAINFO;
extern unsigned IOCTL_LUACREATE;
extern unsigned IOCTL_LUADESTROY;
extern unsigned IOCTL_LUAREQUIRE;
extern unsigned IOCTL_LUALOAD;
extern unsigned IOCTL_MIDI_PRETIME;
extern unsigned IOCTL_MIDI_MPUMODE;
extern unsigned IOCTL_MIDI_MPUCMD;
extern unsigned IOCTL_SEQUENCER_RESET;
extern unsigned IOCTL_SEQUENCER_SYNC;
extern unsigned IOCTL_SEQUENCER_INFO;
extern unsigned IOCTL_SEQUENCER_CTRLRATE;
extern unsigned IOCTL_SEQUENCER_GETOUTCOUNT;
extern unsigned IOCTL_SEQUENCER_GETINCOUNT;
extern unsigned IOCTL_SEQUENCER_RESETSAMPLES;
extern unsigned IOCTL_SEQUENCER_NRSYNTHS;
extern unsigned IOCTL_SEQUENCER_NRMIDIS;
extern unsigned IOCTL_SEQUENCER_THRESHOLD;
extern unsigned IOCTL_SEQUENCER_MEMAVL;
extern unsigned IOCTL_SEQUENCER_PANIC;
extern unsigned IOCTL_SEQUENCER_OUTOFBAND;
extern unsigned IOCTL_SEQUENCER_GETTIME;
extern unsigned IOCTL_SEQUENCER_TMR_TIMEBASE;
extern unsigned IOCTL_SEQUENCER_TMR_START;
extern unsigned IOCTL_SEQUENCER_TMR_STOP;
extern unsigned IOCTL_SEQUENCER_TMR_CONTINUE;
extern unsigned IOCTL_SEQUENCER_TMR_TEMPO;
extern unsigned IOCTL_SEQUENCER_TMR_SOURCE;
extern unsigned IOCTL_SEQUENCER_TMR_METRONOME;
extern unsigned IOCTL_SEQUENCER_TMR_SELECT;
extern unsigned IOCTL_MTIOCTOP;
extern unsigned IOCTL_MTIOCGET;
extern unsigned IOCTL_MTIOCIEOT;
extern unsigned IOCTL_MTIOCEEOT;
extern unsigned IOCTL_MTIOCRDSPOS;
extern unsigned IOCTL_MTIOCRDHPOS;
extern unsigned IOCTL_MTIOCSLOCATE;
extern unsigned IOCTL_MTIOCHLOCATE;
extern unsigned IOCTL_POWER_EVENT_RECVDICT;
extern unsigned IOCTL_POWER_IOC_GET_TYPE;
extern unsigned IOCTL_RIOCGINFO;
extern unsigned IOCTL_RIOCSINFO;
extern unsigned IOCTL_RIOCSSRCH;
extern unsigned IOCTL_RNDGETENTCNT;
extern unsigned IOCTL_RNDGETSRCNUM;
extern unsigned IOCTL_RNDGETSRCNAME;
extern unsigned IOCTL_RNDCTL;
extern unsigned IOCTL_RNDADDDATA;
extern unsigned IOCTL_RNDGETPOOLSTAT;
extern unsigned IOCTL_RNDGETESTNUM;
extern unsigned IOCTL_RNDGETESTNAME;
extern unsigned IOCTL_SCIOCGET;
extern unsigned IOCTL_SCIOCSET;
extern unsigned IOCTL_SCIOCRESTART;
extern unsigned IOCTL_SCIOC_USE_ADF;
extern unsigned IOCTL_SCIOCCOMMAND;
extern unsigned IOCTL_SCIOCDEBUG;
extern unsigned IOCTL_SCIOCIDENTIFY;
extern unsigned IOCTL_OSCIOCIDENTIFY;
extern unsigned IOCTL_SCIOCDECONFIG;
extern unsigned IOCTL_SCIOCRECONFIG;
extern unsigned IOCTL_SCIOCRESET;
extern unsigned IOCTL_SCBUSIOSCAN;
extern unsigned IOCTL_SCBUSIORESET;
extern unsigned IOCTL_SCBUSIODETACH;
extern unsigned IOCTL_SCBUSACCEL;
extern unsigned IOCTL_SCBUSIOLLSCAN;
extern unsigned IOCTL_SIOCSHIWAT;
extern unsigned IOCTL_SIOCGHIWAT;
extern unsigned IOCTL_SIOCSLOWAT;
extern unsigned IOCTL_SIOCGLOWAT;
extern unsigned IOCTL_SIOCATMARK;
extern unsigned IOCTL_SIOCSPGRP;
extern unsigned IOCTL_SIOCGPGRP;
extern unsigned IOCTL_SIOCPEELOFF;
extern unsigned IOCTL_SIOCADDRT;
extern unsigned IOCTL_SIOCDELRT;
extern unsigned IOCTL_SIOCSIFADDR;
extern unsigned IOCTL_SIOCGIFADDR;
extern unsigned IOCTL_SIOCSIFDSTADDR;
extern unsigned IOCTL_SIOCGIFDSTADDR;
extern unsigned IOCTL_SIOCSIFFLAGS;
extern unsigned IOCTL_SIOCGIFFLAGS;
extern unsigned IOCTL_SIOCGIFBRDADDR;
extern unsigned IOCTL_SIOCSIFBRDADDR;
extern unsigned IOCTL_SIOCGIFCONF;
extern unsigned IOCTL_SIOCGIFNETMASK;
extern unsigned IOCTL_SIOCSIFNETMASK;
extern unsigned IOCTL_SIOCGIFMETRIC;
extern unsigned IOCTL_SIOCSIFMETRIC;
extern unsigned IOCTL_SIOCDIFADDR;
extern unsigned IOCTL_SIOCAIFADDR;
extern unsigned IOCTL_SIOCGIFALIAS;
extern unsigned IOCTL_SIOCGIFAFLAG_IN;
extern unsigned IOCTL_SIOCALIFADDR;
extern unsigned IOCTL_SIOCGLIFADDR;
extern unsigned IOCTL_SIOCDLIFADDR;
extern unsigned IOCTL_SIOCSIFADDRPREF;
extern unsigned IOCTL_SIOCGIFADDRPREF;
extern unsigned IOCTL_SIOCADDMULTI;
extern unsigned IOCTL_SIOCDELMULTI;
extern unsigned IOCTL_SIOCGETVIFCNT;
extern unsigned IOCTL_SIOCGETSGCNT;
extern unsigned IOCTL_SIOCSIFMEDIA;
extern unsigned IOCTL_SIOCGIFMEDIA;
extern unsigned IOCTL_SIOCSIFGENERIC;
extern unsigned IOCTL_SIOCGIFGENERIC;
extern unsigned IOCTL_SIOCSIFPHYADDR;
extern unsigned IOCTL_SIOCGIFPSRCADDR;
extern unsigned IOCTL_SIOCGIFPDSTADDR;
extern unsigned IOCTL_SIOCDIFPHYADDR;
extern unsigned IOCTL_SIOCSLIFPHYADDR;
extern unsigned IOCTL_SIOCGLIFPHYADDR;
extern unsigned IOCTL_SIOCSIFMTU;
extern unsigned IOCTL_SIOCGIFMTU;
extern unsigned IOCTL_SIOCSDRVSPEC;
extern unsigned IOCTL_SIOCGDRVSPEC;
extern unsigned IOCTL_SIOCIFCREATE;
extern unsigned IOCTL_SIOCIFDESTROY;
extern unsigned IOCTL_SIOCIFGCLONERS;
extern unsigned IOCTL_SIOCGIFDLT;
extern unsigned IOCTL_SIOCGIFCAP;
extern unsigned IOCTL_SIOCSIFCAP;
extern unsigned IOCTL_SIOCSVH;
extern unsigned IOCTL_SIOCGVH;
extern unsigned IOCTL_SIOCINITIFADDR;
extern unsigned IOCTL_SIOCGIFDATA;
extern unsigned IOCTL_SIOCZIFDATA;
extern unsigned IOCTL_SIOCGLINKSTR;
extern unsigned IOCTL_SIOCSLINKSTR;
extern unsigned IOCTL_SIOCGETHERCAP;
extern unsigned IOCTL_SIOCGIFINDEX;
extern unsigned IOCTL_SIOCSETHERCAP;
extern unsigned IOCTL_SIOCGUMBINFO;
extern unsigned IOCTL_SIOCSUMBPARAM;
extern unsigned IOCTL_SIOCGUMBPARAM;
extern unsigned IOCTL_SIOCSETPFSYNC;
extern unsigned IOCTL_SIOCGETPFSYNC;
extern unsigned IOCTL_PPS_IOC_CREATE;
extern unsigned IOCTL_PPS_IOC_DESTROY;
extern unsigned IOCTL_PPS_IOC_SETPARAMS;
extern unsigned IOCTL_PPS_IOC_GETPARAMS;
extern unsigned IOCTL_PPS_IOC_GETCAP;
extern unsigned IOCTL_PPS_IOC_FETCH;
extern unsigned IOCTL_PPS_IOC_KCBIND;
extern unsigned IOCTL_TIOCEXCL;
extern unsigned IOCTL_TIOCNXCL;
extern unsigned IOCTL_TIOCFLUSH;
extern unsigned IOCTL_TIOCGETA;
extern unsigned IOCTL_TIOCSETA;
extern unsigned IOCTL_TIOCSETAW;
extern unsigned IOCTL_TIOCSETAF;
extern unsigned IOCTL_TIOCGETD;
extern unsigned IOCTL_TIOCSETD;
extern unsigned IOCTL_TIOCGLINED;
extern unsigned IOCTL_TIOCSLINED;
extern unsigned IOCTL_TIOCSBRK;
extern unsigned IOCTL_TIOCCBRK;
extern unsigned IOCTL_TIOCSDTR;
extern unsigned IOCTL_TIOCCDTR;
extern unsigned IOCTL_TIOCGPGRP;
extern unsigned IOCTL_TIOCSPGRP;
extern unsigned IOCTL_TIOCOUTQ;
extern unsigned IOCTL_TIOCSTI;
extern unsigned IOCTL_TIOCNOTTY;
extern unsigned IOCTL_TIOCPKT;
extern unsigned IOCTL_TIOCSTOP;
extern unsigned IOCTL_TIOCSTART;
extern unsigned IOCTL_TIOCMSET;
extern unsigned IOCTL_TIOCMBIS;
extern unsigned IOCTL_TIOCMBIC;
extern unsigned IOCTL_TIOCMGET;
extern unsigned IOCTL_TIOCREMOTE;
extern unsigned IOCTL_TIOCGWINSZ;
extern unsigned IOCTL_TIOCSWINSZ;
extern unsigned IOCTL_TIOCUCNTL;
extern unsigned IOCTL_TIOCSTAT;
extern unsigned IOCTL_TIOCGSID;
extern unsigned IOCTL_TIOCCONS;
extern unsigned IOCTL_TIOCSCTTY;
extern unsigned IOCTL_TIOCEXT;
extern unsigned IOCTL_TIOCSIG;
extern unsigned IOCTL_TIOCDRAIN;
extern unsigned IOCTL_TIOCGFLAGS;
extern unsigned IOCTL_TIOCSFLAGS;
extern unsigned IOCTL_TIOCDCDTIMESTAMP;
extern unsigned IOCTL_TIOCRCVFRAME;
extern unsigned IOCTL_TIOCXMTFRAME;
extern unsigned IOCTL_TIOCPTMGET;
extern unsigned IOCTL_TIOCGRANTPT;
extern unsigned IOCTL_TIOCPTSNAME;
extern unsigned IOCTL_TIOCSQSIZE;
extern unsigned IOCTL_TIOCGQSIZE;
extern unsigned IOCTL_VERIEXEC_LOAD;
extern unsigned IOCTL_VERIEXEC_TABLESIZE;
extern unsigned IOCTL_VERIEXEC_DELETE;
extern unsigned IOCTL_VERIEXEC_QUERY;
extern unsigned IOCTL_VERIEXEC_DUMP;
extern unsigned IOCTL_VERIEXEC_FLUSH;
extern unsigned IOCTL_VIDIOC_QUERYCAP;
extern unsigned IOCTL_VIDIOC_RESERVED;
extern unsigned IOCTL_VIDIOC_ENUM_FMT;
extern unsigned IOCTL_VIDIOC_G_FMT;
extern unsigned IOCTL_VIDIOC_S_FMT;
extern unsigned IOCTL_VIDIOC_REQBUFS;
extern unsigned IOCTL_VIDIOC_QUERYBUF;
extern unsigned IOCTL_VIDIOC_G_FBUF;
extern unsigned IOCTL_VIDIOC_S_FBUF;
extern unsigned IOCTL_VIDIOC_OVERLAY;
extern unsigned IOCTL_VIDIOC_QBUF;
extern unsigned IOCTL_VIDIOC_DQBUF;
extern unsigned IOCTL_VIDIOC_STREAMON;
extern unsigned IOCTL_VIDIOC_STREAMOFF;
extern unsigned IOCTL_VIDIOC_G_PARM;
extern unsigned IOCTL_VIDIOC_S_PARM;
extern unsigned IOCTL_VIDIOC_G_STD;
extern unsigned IOCTL_VIDIOC_S_STD;
extern unsigned IOCTL_VIDIOC_ENUMSTD;
extern unsigned IOCTL_VIDIOC_ENUMINPUT;
extern unsigned IOCTL_VIDIOC_G_CTRL;
extern unsigned IOCTL_VIDIOC_S_CTRL;
extern unsigned IOCTL_VIDIOC_G_TUNER;
extern unsigned IOCTL_VIDIOC_S_TUNER;
extern unsigned IOCTL_VIDIOC_G_AUDIO;
extern unsigned IOCTL_VIDIOC_S_AUDIO;
extern unsigned IOCTL_VIDIOC_QUERYCTRL;
extern unsigned IOCTL_VIDIOC_QUERYMENU;
extern unsigned IOCTL_VIDIOC_G_INPUT;
extern unsigned IOCTL_VIDIOC_S_INPUT;
extern unsigned IOCTL_VIDIOC_G_OUTPUT;
extern unsigned IOCTL_VIDIOC_S_OUTPUT;
extern unsigned IOCTL_VIDIOC_ENUMOUTPUT;
extern unsigned IOCTL_VIDIOC_G_AUDOUT;
extern unsigned IOCTL_VIDIOC_S_AUDOUT;
extern unsigned IOCTL_VIDIOC_G_MODULATOR;
extern unsigned IOCTL_VIDIOC_S_MODULATOR;
extern unsigned IOCTL_VIDIOC_G_FREQUENCY;
extern unsigned IOCTL_VIDIOC_S_FREQUENCY;
extern unsigned IOCTL_VIDIOC_CROPCAP;
extern unsigned IOCTL_VIDIOC_G_CROP;
extern unsigned IOCTL_VIDIOC_S_CROP;
extern unsigned IOCTL_VIDIOC_G_JPEGCOMP;
extern unsigned IOCTL_VIDIOC_S_JPEGCOMP;
extern unsigned IOCTL_VIDIOC_QUERYSTD;
extern unsigned IOCTL_VIDIOC_TRY_FMT;
extern unsigned IOCTL_VIDIOC_ENUMAUDIO;
extern unsigned IOCTL_VIDIOC_ENUMAUDOUT;
extern unsigned IOCTL_VIDIOC_G_PRIORITY;
extern unsigned IOCTL_VIDIOC_S_PRIORITY;
extern unsigned IOCTL_VIDIOC_ENUM_FRAMESIZES;
extern unsigned IOCTL_VIDIOC_ENUM_FRAMEINTERVALS;
extern unsigned IOCTL_WDOGIOC_GMODE;
extern unsigned IOCTL_WDOGIOC_SMODE;
extern unsigned IOCTL_WDOGIOC_WHICH;
extern unsigned IOCTL_WDOGIOC_TICKLE;
extern unsigned IOCTL_WDOGIOC_GTICKLER;
extern unsigned IOCTL_WDOGIOC_GWDOGS;
extern unsigned IOCTL_SNDCTL_DSP_RESET;
extern unsigned IOCTL_SNDCTL_DSP_SYNC;
extern unsigned IOCTL_SNDCTL_DSP_SPEED;
extern unsigned IOCTL_SOUND_PCM_READ_RATE;
extern unsigned IOCTL_SNDCTL_DSP_STEREO;
extern unsigned IOCTL_SNDCTL_DSP_GETBLKSIZE;
extern unsigned IOCTL_SNDCTL_DSP_SETFMT;
extern unsigned IOCTL_SOUND_PCM_READ_BITS;
extern unsigned IOCTL_SNDCTL_DSP_CHANNELS;
extern unsigned IOCTL_SOUND_PCM_READ_CHANNELS;
extern unsigned IOCTL_SOUND_PCM_WRITE_FILTER;
extern unsigned IOCTL_SOUND_PCM_READ_FILTER;
extern unsigned IOCTL_SNDCTL_DSP_POST;
extern unsigned IOCTL_SNDCTL_DSP_SUBDIVIDE;
extern unsigned IOCTL_SNDCTL_DSP_SETFRAGMENT;
extern unsigned IOCTL_SNDCTL_DSP_GETFMTS;
extern unsigned IOCTL_SNDCTL_DSP_GETOSPACE;
extern unsigned IOCTL_SNDCTL_DSP_GETISPACE;
extern unsigned IOCTL_SNDCTL_DSP_NONBLOCK;
extern unsigned IOCTL_SNDCTL_DSP_GETCAPS;
extern unsigned IOCTL_SNDCTL_DSP_GETTRIGGER;
extern unsigned IOCTL_SNDCTL_DSP_SETTRIGGER;
extern unsigned IOCTL_SNDCTL_DSP_GETIPTR;
extern unsigned IOCTL_SNDCTL_DSP_GETOPTR;
extern unsigned IOCTL_SNDCTL_DSP_MAPINBUF;
extern unsigned IOCTL_SNDCTL_DSP_MAPOUTBUF;
extern unsigned IOCTL_SNDCTL_DSP_SETSYNCRO;
extern unsigned IOCTL_SNDCTL_DSP_SETDUPLEX;
extern unsigned IOCTL_SNDCTL_DSP_PROFILE;
extern unsigned IOCTL_SNDCTL_DSP_GETODELAY;
extern unsigned IOCTL_SOUND_MIXER_INFO;
extern unsigned IOCTL_SOUND_OLD_MIXER_INFO;
extern unsigned IOCTL_OSS_GETVERSION;
extern unsigned IOCTL_SNDCTL_SYSINFO;
extern unsigned IOCTL_SNDCTL_AUDIOINFO;
extern unsigned IOCTL_SNDCTL_ENGINEINFO;
extern unsigned IOCTL_SNDCTL_DSP_GETPLAYVOL;
extern unsigned IOCTL_SNDCTL_DSP_SETPLAYVOL;
extern unsigned IOCTL_SNDCTL_DSP_GETRECVOL;
extern unsigned IOCTL_SNDCTL_DSP_SETRECVOL;
extern unsigned IOCTL_SNDCTL_DSP_SKIP;
extern unsigned IOCTL_SNDCTL_DSP_SILENCE;

extern const int si_SEGV_MAPERR;
extern const int si_SEGV_ACCERR;

extern const unsigned SHA1_CTX_sz;
extern const unsigned SHA1_return_length;

extern const unsigned MD4_CTX_sz;
extern const unsigned MD4_return_length;

extern const unsigned RMD160_CTX_sz;
extern const unsigned RMD160_return_length;

extern const unsigned MD5_CTX_sz;
extern const unsigned MD5_return_length;

extern const unsigned fpos_t_sz;

extern const unsigned MD2_CTX_sz;
extern const unsigned MD2_return_length;

#define SHA2_EXTERN(LEN)                          \
  extern const unsigned SHA##LEN##_CTX_sz;        \
  extern const unsigned SHA##LEN##_return_length; \
  extern const unsigned SHA##LEN##_block_length;  \
  extern const unsigned SHA##LEN##_digest_length

SHA2_EXTERN(224);
SHA2_EXTERN(256);
SHA2_EXTERN(384);
SHA2_EXTERN(512);

#undef SHA2_EXTERN

extern const int unvis_valid;
extern const int unvis_validpush;

struct __sanitizer_cdbr {
  void (*unmap)(void *, void *, uptr);
  void *cookie;
  u8 *mmap_base;
  uptr mmap_size;

  u8 *hash_base;
  u8 *offset_base;
  u8 *data_base;

  u32 data_size;
  u32 entries;
  u32 entries_index;
  u32 seed;

  u8 offset_size;
  u8 index_size;

  u32 entries_m;
  u32 entries_index_m;
  u8 entries_s1, entries_s2;
  u8 entries_index_s1, entries_index_s2;
};

struct __sanitizer_cdbw {
  uptr data_counter;
  uptr data_allocated;
  uptr data_size;
  uptr *data_len;
  void **data_ptr;
  uptr hash_size;
  void *hash;
  uptr key_counter;
};
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

#endif  // SANITIZER_NETBSD

#endif
