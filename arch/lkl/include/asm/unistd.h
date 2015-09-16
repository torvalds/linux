#ifndef _ASM_LKL_UNISTD_H
#define _ASM_LKL_UNISTD_H

#include <uapi/asm/unistd.h>

/*
 * Unsupported system calls due to lack of support in LKL (e.g. related to
 * virtual memory, signal, user processes). We also only support 64bit version
 * of system calls where we have two version to keep the same APi across 32 and
 * 64 bit hosts.
 */
#define __NR_restart_syscall 0
#define __NR_exit 0
#define __NR_fork 0
#define __NR_execve 0
#define __NR_ptrace 0
#define __NR_alarm 0
#define __NR_pause 0
#define __NR_kill 0
#define __NR_brk 0
#define __NR_uselib 0
#define __NR_swapon 0
#define __NR_mmap 0
#define __NR_munmap 0
#define __NR_swapoff 0
#define __NR_clone 0
#define __NR_mprotect 0
#define __NR_init_module 0
#define __NR_quotactl 0
#define __NR_msync 0
#define __NR_mlock 0
#define __NR_munlock 0
#define __NR_mlockall 0
#define __NR_munlockall 0
#define __NR_mremap 0
#define __NR_rt_sigreturn 0
#define __NR_rt_sigaction 0
#define __NR_rt_sigprocmask 0
#define __NR_rt_sigpending 0
#define __NR_rt_sigtimedwait 0
#define __NR_rt_sigqueueinfo 0
#define __NR_rt_sigsuspend 0
#define __NR_sigaltstack 0
#define __NR_vfork 0
#define __NR_mincore 0
#define __NR_madvise 0
#define __NR_getdents 0 /* we use the 64 bit counter part instead */
#define __NR_tkill 0
#define __NR_exit_group 0
#define __NR_remap_file_pages 0
#define __NR_statfs 0 /* we use the 64 bit counter part instead */
#define __NR_fstatfs 0 /* we use the 64 bit counter part instead */
#define __NR_fstat 0 /* we use the 64 bit counter part instead */
#define __NR_fadvise64_64 0
#define __NR_mbind 0
#define __NR_get_mempolicy 0
#define __NR_set_mempolicy 0
#define __NR_mq_open 0
#define __NR_mq_unlink 0
#define __NR_mq_timedsend 0
#define __NR_mq_timedreceive 0
#define __NR_mq_0
#define __NR_mq_getsetattr 0
#define __NR_kexec_load 0
#define __NR_migrate_pages 0
#define __NR_unshare 0
#define __NR_set_robust_list 0
#define __NR_get_robust_list 0
#define __NR_sync_file_range 0
#define __NR_vmsplice 0
#define __NR_move_pages 0
#define __NR_mq_notify 0
#define __NR_umount2 0
#define __NR_delete_module 0
#define __NR_signalfd4 0
#define __NR_preadv 0 /* we use the 64 bit counter part instead */
#define __NR_pwritev 0 /* we use the 64 bit counter part instead */
#define __NR_rt_tgsigqueueinfo 0
#define __NR_perf_event_open 0
#define __NR_setns 0
#define __NR_process_vm_readv 0
#define __NR_process_vm_writev 0
#define __NR_kcmp 0
#define __NR_finit_module 0
#define __NR_seccomp 0
#define __NR_memfd_create 0
#define __NR_bpf 0
#define __NR_execveat 0
#define __NR_lseek 0 /* we use the 64 bit counter part instead */

int run_syscalls(void);

#endif
