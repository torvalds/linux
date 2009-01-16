/* ptrace.c: FRV specific parts of process tracing
 *
 * Copyright (C) 2003-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/m68k/kernel/ptrace.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/signal.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/unistd.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	struct user_context *user = task->thread.user;

	if (regno < 0 || regno >= PT__END)
		return 0;

	return ((unsigned long *) user)[regno];
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	struct user_context *user = task->thread.user;

	if (regno < 0 || regno >= PT__END)
		return -EIO;

	switch (regno) {
	case PT_GR(0):
		return 0;
	case PT_PSR:
	case PT__STATUS:
		return -EIO;
	default:
		((unsigned long *) user)[regno] = data;
		return 0;
	}
}

/*
 * check that an address falls within the bounds of the target process's memory
 * mappings
 */
static inline int is_user_addr_valid(struct task_struct *child,
				     unsigned long start, unsigned long len)
{
#ifdef CONFIG_MMU
	if (start >= PAGE_OFFSET || len > PAGE_OFFSET - start)
		return -EIO;
	return 0;
#else
	struct vm_area_struct *vma;

	vma = find_vma(child->mm, start);
	if (vma && start >= vma->vm_start && start + len <= vma->vm_end)
		return 0;

	return -EIO;
#endif
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Control h/w single stepping
 */
void ptrace_disable(struct task_struct *child)
{
	child->thread.frame0->__status &= ~REG__STATUS_STEP;
}

void ptrace_enable(struct task_struct *child)
{
	child->thread.frame0->__status |= REG__STATUS_STEP;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	unsigned long tmp;
	int ret;

	switch (request) {
		/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = -EIO;
		if (is_user_addr_valid(child, addr, sizeof(tmp)) < 0)
			break;
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

		/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		tmp = 0;
		ret = -EIO;
		if ((addr & 3) || addr < 0)
			break;

		ret = 0;
		switch (addr >> 2) {
		case 0 ... PT__END - 1:
			tmp = get_reg(child, addr >> 2);
			break;

		case PT__END + 0:
			tmp = child->mm->end_code - child->mm->start_code;
			break;

		case PT__END + 1:
			tmp = child->mm->end_data - child->mm->start_data;
			break;

		case PT__END + 2:
			tmp = child->mm->start_stack - child->mm->start_brk;
			break;

		case PT__END + 3:
			tmp = child->mm->start_code;
			break;

		case PT__END + 4:
			tmp = child->mm->start_stack;
			break;

		default:
			ret = -EIO;
			break;
		}

		if (ret == 0)
			ret = put_user(tmp, (unsigned long *) data);
		break;
	}

		/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = -EIO;
		if (is_user_addr_valid(child, addr, sizeof(tmp)) < 0)
			break;
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
		ret = -EIO;
		if ((addr & 3) || addr < 0)
			break;

		ret = 0;
		switch (addr >> 2) {
		case 0 ... PT__END-1:
			ret = put_reg(child, addr >> 2, data);
			break;

		default:
			ret = -EIO;
			break;
		}
		break;

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: /* restart after signal. */
		ret = -EIO;
		if (!valid_signal(data))
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		ptrace_disable(child);
		wake_up_process(child);
		ret = 0;
		break;

		/* make the child exit.  Best I can do is send it a sigkill.
		 * perhaps it should be put in the status that it wants to
		 * exit.
		 */
	case PTRACE_KILL:
		ret = 0;
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		clear_tsk_thread_flag(child, TIF_SINGLESTEP);
		ptrace_disable(child);
		wake_up_process(child);
		break;

	case PTRACE_SINGLESTEP:  /* set the trap flag. */
		ret = -EIO;
		if (!valid_signal(data))
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		ptrace_enable(child);
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;

	case PTRACE_DETACH:	/* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

	case PTRACE_GETREGS: { /* Get all integer regs from the child. */
		int i;
		for (i = 0; i < PT__GPEND; i++) {
			tmp = get_reg(child, i);
			if (put_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			}
			data += sizeof(long);
		}
		ret = 0;
		break;
	}

	case PTRACE_SETREGS: { /* Set all integer regs in the child. */
		int i;
		for (i = 0; i < PT__GPEND; i++) {
			if (get_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			}
			put_reg(child, i, tmp);
			data += sizeof(long);
		}
		ret = 0;
		break;
	}

	case PTRACE_GETFPREGS: { /* Get the child FP/Media state. */
		ret = 0;
		if (copy_to_user((void *) data,
				 &child->thread.user->f,
				 sizeof(child->thread.user->f)))
			ret = -EFAULT;
		break;
	}

	case PTRACE_SETFPREGS: { /* Set the child FP/Media state. */
		ret = 0;
		if (copy_from_user(&child->thread.user->f,
				   (void *) data,
				   sizeof(child->thread.user->f)))
			ret = -EFAULT;
		break;
	}

	case PTRACE_GETFDPIC:
		tmp = 0;
		switch (addr) {
		case PTRACE_GETFDPIC_EXEC:
			tmp = child->mm->context.exec_fdpic_loadmap;
			break;
		case PTRACE_GETFDPIC_INTERP:
			tmp = child->mm->context.interp_fdpic_loadmap;
			break;
		default:
			break;
		}

		ret = 0;
		if (put_user(tmp, (unsigned long *) data)) {
			ret = -EFAULT;
			break;
		}
		break;

	default:
		ret = -EIO;
		break;
	}
	return ret;
}

int __nongprelbss kstrace;

static const struct {
	const char	*name;
	unsigned	argmask;
} __syscall_name_table[NR_syscalls] = {
	[0]	= { "restart_syscall"			},
	[1]	= { "exit",		0x000001	},
	[2]	= { "fork",		0xffffff	},
	[3]	= { "read",		0x000141	},
	[4]	= { "write",		0x000141	},
	[5]	= { "open",		0x000235	},
	[6]	= { "close",		0x000001	},
	[7]	= { "waitpid",		0x000141	},
	[8]	= { "creat",		0x000025	},
	[9]	= { "link",		0x000055	},
	[10]	= { "unlink",		0x000005	},
	[11]	= { "execve",		0x000445	},
	[12]	= { "chdir",		0x000005	},
	[13]	= { "time",		0x000004	},
	[14]	= { "mknod",		0x000325	},
	[15]	= { "chmod",		0x000025	},
	[16]	= { "lchown",		0x000025	},
	[17]	= { "break" },
	[18]	= { "oldstat",		0x000045	},
	[19]	= { "lseek",		0x000131	},
	[20]	= { "getpid",		0xffffff	},
	[21]	= { "mount",		0x043555	},
	[22]	= { "umount",		0x000005	},
	[23]	= { "setuid",		0x000001	},
	[24]	= { "getuid",		0xffffff	},
	[25]	= { "stime",		0x000004	},
	[26]	= { "ptrace",		0x004413	},
	[27]	= { "alarm",		0x000001	},
	[28]	= { "oldfstat",		0x000041	},
	[29]	= { "pause",		0xffffff	},
	[30]	= { "utime",		0x000045	},
	[31]	= { "stty" },
	[32]	= { "gtty" },
	[33]	= { "access",		0x000025	},
	[34]	= { "nice",		0x000001	},
	[35]	= { "ftime" },
	[36]	= { "sync",		0xffffff	},
	[37]	= { "kill",		0x000011	},
	[38]	= { "rename",		0x000055	},
	[39]	= { "mkdir",		0x000025	},
	[40]	= { "rmdir",		0x000005	},
	[41]	= { "dup",		0x000001	},
	[42]	= { "pipe",		0x000004	},
	[43]	= { "times",		0x000004	},
	[44]	= { "prof" },
	[45]	= { "brk",		0x000004	},
	[46]	= { "setgid",		0x000001	},
	[47]	= { "getgid",		0xffffff	},
	[48]	= { "signal",		0x000041	},
	[49]	= { "geteuid",		0xffffff	},
	[50]	= { "getegid",		0xffffff	},
	[51]	= { "acct",		0x000005	},
	[52]	= { "umount2",		0x000035	},
	[53]	= { "lock" },
	[54]	= { "ioctl",		0x000331	},
	[55]	= { "fcntl",		0x000331	},
	[56]	= { "mpx" },
	[57]	= { "setpgid",		0x000011	},
	[58]	= { "ulimit" },
	[60]	= { "umask",		0x000002	},
	[61]	= { "chroot",		0x000005	},
	[62]	= { "ustat",		0x000043	},
	[63]	= { "dup2",		0x000011	},
	[64]	= { "getppid",		0xffffff	},
	[65]	= { "getpgrp",		0xffffff	},
	[66]	= { "setsid",		0xffffff	},
	[67]	= { "sigaction" },
	[68]	= { "sgetmask" },
	[69]	= { "ssetmask" },
	[70]	= { "setreuid" },
	[71]	= { "setregid" },
	[72]	= { "sigsuspend" },
	[73]	= { "sigpending" },
	[74]	= { "sethostname" },
	[75]	= { "setrlimit" },
	[76]	= { "getrlimit" },
	[77]	= { "getrusage" },
	[78]	= { "gettimeofday" },
	[79]	= { "settimeofday" },
	[80]	= { "getgroups" },
	[81]	= { "setgroups" },
	[82]	= { "select" },
	[83]	= { "symlink" },
	[84]	= { "oldlstat" },
	[85]	= { "readlink" },
	[86]	= { "uselib" },
	[87]	= { "swapon" },
	[88]	= { "reboot" },
	[89]	= { "readdir" },
	[91]	= { "munmap",		0x000034	},
	[92]	= { "truncate" },
	[93]	= { "ftruncate" },
	[94]	= { "fchmod" },
	[95]	= { "fchown" },
	[96]	= { "getpriority" },
	[97]	= { "setpriority" },
	[99]	= { "statfs" },
	[100]	= { "fstatfs" },
	[102]	= { "socketcall" },
	[103]	= { "syslog" },
	[104]	= { "setitimer" },
	[105]	= { "getitimer" },
	[106]	= { "stat" },
	[107]	= { "lstat" },
	[108]	= { "fstat" },
	[111]	= { "vhangup" },
	[114]	= { "wait4" },
	[115]	= { "swapoff" },
	[116]	= { "sysinfo" },
	[117]	= { "ipc" },
	[118]	= { "fsync" },
	[119]	= { "sigreturn" },
	[120]	= { "clone" },
	[121]	= { "setdomainname" },
	[122]	= { "uname" },
	[123]	= { "modify_ldt" },
	[123]	= { "cacheflush" },
	[124]	= { "adjtimex" },
	[125]	= { "mprotect" },
	[126]	= { "sigprocmask" },
	[127]	= { "create_module" },
	[128]	= { "init_module" },
	[129]	= { "delete_module" },
	[130]	= { "get_kernel_syms" },
	[131]	= { "quotactl" },
	[132]	= { "getpgid" },
	[133]	= { "fchdir" },
	[134]	= { "bdflush" },
	[135]	= { "sysfs" },
	[136]	= { "personality" },
	[137]	= { "afs_syscall" },
	[138]	= { "setfsuid" },
	[139]	= { "setfsgid" },
	[140]	= { "_llseek",			0x014331	},
	[141]	= { "getdents" },
	[142]	= { "_newselect",		0x000141	},
	[143]	= { "flock" },
	[144]	= { "msync" },
	[145]	= { "readv" },
	[146]	= { "writev" },
	[147]	= { "getsid",			0x000001	},
	[148]	= { "fdatasync",		0x000001	},
	[149]	= { "_sysctl",			0x000004	},
	[150]	= { "mlock" },
	[151]	= { "munlock" },
	[152]	= { "mlockall" },
	[153]	= { "munlockall" },
	[154]	= { "sched_setparam" },
	[155]	= { "sched_getparam" },
	[156]	= { "sched_setscheduler" },
	[157]	= { "sched_getscheduler" },
	[158]	= { "sched_yield" },
	[159]	= { "sched_get_priority_max" },
	[160]	= { "sched_get_priority_min" },
	[161]	= { "sched_rr_get_interval" },
	[162]	= { "nanosleep",		0x000044	},
	[163]	= { "mremap" },
	[164]	= { "setresuid" },
	[165]	= { "getresuid" },
	[166]	= { "vm86" },
	[167]	= { "query_module" },
	[168]	= { "poll" },
	[169]	= { "nfsservctl" },
	[170]	= { "setresgid" },
	[171]	= { "getresgid" },
	[172]	= { "prctl",			0x333331	},
	[173]	= { "rt_sigreturn",		0xffffff	},
	[174]	= { "rt_sigaction",		0x001441	},
	[175]	= { "rt_sigprocmask",		0x001441	},
	[176]	= { "rt_sigpending",		0x000014	},
	[177]	= { "rt_sigtimedwait",		0x001444	},
	[178]	= { "rt_sigqueueinfo",		0x000411	},
	[179]	= { "rt_sigsuspend",		0x000014	},
	[180]	= { "pread",			0x003341	},
	[181]	= { "pwrite",			0x003341	},
	[182]	= { "chown",			0x000115	},
	[183]	= { "getcwd" },
	[184]	= { "capget" },
	[185]	= { "capset" },
	[186]	= { "sigaltstack" },
	[187]	= { "sendfile" },
	[188]	= { "getpmsg" },
	[189]	= { "putpmsg" },
	[190]	= { "vfork",			0xffffff	},
	[191]	= { "ugetrlimit" },
	[192]	= { "mmap2",			0x313314	},
	[193]	= { "truncate64" },
	[194]	= { "ftruncate64" },
	[195]	= { "stat64",			0x000045	},
	[196]	= { "lstat64",			0x000045	},
	[197]	= { "fstat64",			0x000041	},
	[198]	= { "lchown32" },
	[199]	= { "getuid32",			0xffffff	},
	[200]	= { "getgid32",			0xffffff	},
	[201]	= { "geteuid32",		0xffffff	},
	[202]	= { "getegid32",		0xffffff	},
	[203]	= { "setreuid32" },
	[204]	= { "setregid32" },
	[205]	= { "getgroups32" },
	[206]	= { "setgroups32" },
	[207]	= { "fchown32" },
	[208]	= { "setresuid32" },
	[209]	= { "getresuid32" },
	[210]	= { "setresgid32" },
	[211]	= { "getresgid32" },
	[212]	= { "chown32" },
	[213]	= { "setuid32" },
	[214]	= { "setgid32" },
	[215]	= { "setfsuid32" },
	[216]	= { "setfsgid32" },
	[217]	= { "pivot_root" },
	[218]	= { "mincore" },
	[219]	= { "madvise" },
	[220]	= { "getdents64" },
	[221]	= { "fcntl64" },
	[223]	= { "security" },
	[224]	= { "gettid" },
	[225]	= { "readahead" },
	[226]	= { "setxattr" },
	[227]	= { "lsetxattr" },
	[228]	= { "fsetxattr" },
	[229]	= { "getxattr" },
	[230]	= { "lgetxattr" },
	[231]	= { "fgetxattr" },
	[232]	= { "listxattr" },
	[233]	= { "llistxattr" },
	[234]	= { "flistxattr" },
	[235]	= { "removexattr" },
	[236]	= { "lremovexattr" },
	[237]	= { "fremovexattr" },
	[238]	= { "tkill" },
	[239]	= { "sendfile64" },
	[240]	= { "futex" },
	[241]	= { "sched_setaffinity" },
	[242]	= { "sched_getaffinity" },
	[243]	= { "set_thread_area" },
	[244]	= { "get_thread_area" },
	[245]	= { "io_setup" },
	[246]	= { "io_destroy" },
	[247]	= { "io_getevents" },
	[248]	= { "io_submit" },
	[249]	= { "io_cancel" },
	[250]	= { "fadvise64" },
	[252]	= { "exit_group",		0x000001	},
	[253]	= { "lookup_dcookie" },
	[254]	= { "epoll_create" },
	[255]	= { "epoll_ctl" },
	[256]	= { "epoll_wait" },
	[257]	= { "remap_file_pages" },
	[258]	= { "set_tid_address" },
	[259]	= { "timer_create" },
	[260]	= { "timer_settime" },
	[261]	= { "timer_gettime" },
	[262]	= { "timer_getoverrun" },
	[263]	= { "timer_delete" },
	[264]	= { "clock_settime" },
	[265]	= { "clock_gettime" },
	[266]	= { "clock_getres" },
	[267]	= { "clock_nanosleep" },
	[268]	= { "statfs64" },
	[269]	= { "fstatfs64" },
	[270]	= { "tgkill" },
	[271]	= { "utimes" },
	[272]	= { "fadvise64_64" },
	[273]	= { "vserver" },
	[274]	= { "mbind" },
	[275]	= { "get_mempolicy" },
	[276]	= { "set_mempolicy" },
	[277]	= { "mq_open" },
	[278]	= { "mq_unlink" },
	[279]	= { "mq_timedsend" },
	[280]	= { "mq_timedreceive" },
	[281]	= { "mq_notify" },
	[282]	= { "mq_getsetattr" },
	[283]	= { "sys_kexec_load" },
};

asmlinkage void do_syscall_trace(int leaving)
{
#if 0
	unsigned long *argp;
	const char *name;
	unsigned argmask;
	char buffer[16];

	if (!kstrace)
		return;

	if (!current->mm)
		return;

	if (__frame->gr7 == __NR_close)
		return;

#if 0
	if (__frame->gr7 != __NR_mmap2 &&
	    __frame->gr7 != __NR_vfork &&
	    __frame->gr7 != __NR_execve &&
	    __frame->gr7 != __NR_exit)
		return;
#endif

	argmask = 0;
	name = NULL;
	if (__frame->gr7 < NR_syscalls) {
		name = __syscall_name_table[__frame->gr7].name;
		argmask = __syscall_name_table[__frame->gr7].argmask;
	}
	if (!name) {
		sprintf(buffer, "sys_%lx", __frame->gr7);
		name = buffer;
	}

	if (!leaving) {
		if (!argmask) {
			printk(KERN_CRIT "[%d] %s(%lx,%lx,%lx,%lx,%lx,%lx)\n",
			       current->pid,
			       name,
			       __frame->gr8,
			       __frame->gr9,
			       __frame->gr10,
			       __frame->gr11,
			       __frame->gr12,
			       __frame->gr13);
		}
		else if (argmask == 0xffffff) {
			printk(KERN_CRIT "[%d] %s()\n",
			       current->pid,
			       name);
		}
		else {
			printk(KERN_CRIT "[%d] %s(",
			       current->pid,
			       name);

			argp = &__frame->gr8;

			do {
				switch (argmask & 0xf) {
				case 1:
					printk("%ld", (long) *argp);
					break;
				case 2:
					printk("%lo", *argp);
					break;
				case 3:
					printk("%lx", *argp);
					break;
				case 4:
					printk("%p", (void *) *argp);
					break;
				case 5:
					printk("\"%s\"", (char *) *argp);
					break;
				}

				argp++;
				argmask >>= 4;
				if (argmask)
					printk(",");

			} while (argmask);

			printk(")\n");
		}
	}
	else {
		if ((int)__frame->gr8 > -4096 && (int)__frame->gr8 < 4096)
			printk(KERN_CRIT "[%d] %s() = %ld\n", current->pid, name, __frame->gr8);
		else
			printk(KERN_CRIT "[%d] %s() = %lx\n", current->pid, name, __frame->gr8);
	}
	return;
#endif

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;

	if (!(current->ptrace & PT_PTRACED))
		return;

	/* we need to indicate entry or exit to strace */
	if (leaving)
		__frame->__status |= REG__STATUS_SYSC_EXIT;
	else
		__frame->__status |= REG__STATUS_SYSC_ENTRY;

	ptrace_notify(SIGTRAP);

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
