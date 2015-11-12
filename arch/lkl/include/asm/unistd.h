#ifndef _ASM_LKL_UNISTD_H
#define _ASM_LKL_UNISTD_H

#include <uapi/asm/unistd.h>

/*
 * Unsupported system calls due to lack of support in LKL (e.g. related to
 * virtual memory, signal, user processes). We also only support 64bit version
 * of system calls where we have two version to keep the same API across 32 and
 * 64 bit hosts.
 */
#define __IGNORE_restart_syscall
#define __IGNORE_exit
#define __IGNORE_fork
#define __IGNORE_execve
#define __IGNORE_ptrace
#define __IGNORE_alarm
#define __IGNORE_pause
#define __IGNORE_kill
#define __IGNORE_brk
#define __IGNORE_uselib
#define __IGNORE_swapon
#define __IGNORE_mmap
#define __IGNORE_munmap
#define __IGNORE_swapoff
#define __IGNORE_clone
#define __IGNORE_mprotect
#define __IGNORE_init_module
#define __IGNORE_quotactl
#define __IGNORE_msync
#define __IGNORE_mlock
#define __IGNORE_munlock
#define __IGNORE_mlockall
#define __IGNORE_munlockall
#define __IGNORE_mremap
#define __IGNORE_rt_sigreturn
#define __IGNORE_rt_sigaction
#define __IGNORE_rt_sigprocmask
#define __IGNORE_rt_sigpending
#define __IGNORE_rt_sigtimedwait
#define __IGNORE_rt_sigqueueinfo
#define __IGNORE_rt_sigsuspend
#define __IGNORE_sigaltstack
#define __IGNORE_vfork
#define __IGNORE_mincore
#define __IGNORE_madvise
#define __IGNORE_getdents /* we use the 64 bit counter part instead */
#define __IGNORE_tkill
#define __IGNORE_exit_group
#define __IGNORE_remap_file_pages
#define __IGNORE_statfs /* we use the 64 bit counter part instead */
#define __IGNORE_fstatfs /* we use the 64 bit counter part instead */
#define __IGNORE_fstat /* we use the 64 bit counter part instead */
#define __IGNORE_fadvise64_64
#define __IGNORE_mbind
#define __IGNORE_get_mempolicy
#define __IGNORE_set_mempolicy
#define __IGNORE_mq_open
#define __IGNORE_mq_unlink
#define __IGNORE_mq_timedsend
#define __IGNORE_mq_timedreceive
#define __IGNORE_mq
#define __IGNORE_mq_getsetattr
#define __IGNORE_kexec_load
#define __IGNORE_migrate_pages
#define __IGNORE_unshare
#define __IGNORE_set_robust_list
#define __IGNORE_get_robust_list
#define __IGNORE_sync_file_range
#define __IGNORE_vmsplice
#define __IGNORE_move_pages
#define __IGNORE_mq_notify
#define __IGNORE_umount2
#define __IGNORE_delete_module
#define __IGNORE_signalfd4
#define __IGNORE_preadv /* we use the 64 bit counter part instead */
#define __IGNORE_pwritev /* we use the 64 bit counter part instead */
#define __IGNORE_rt_tgsigqueueinfo
#define __IGNORE_perf_event_open
#define __IGNORE_setns
#define __IGNORE_process_vm_readv
#define __IGNORE_process_vm_writev
#define __IGNORE_kcmp
#define __IGNORE_finit_module
#define __IGNORE_seccomp
#define __IGNORE_memfd_create
#define __IGNORE_bpf
#define __IGNORE_execveat
#define __IGNORE_lseek /* we use the 64 bit counter part instead */

int run_syscalls(void);

#endif
