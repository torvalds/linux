/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_COMPAT_H
#define _ASM_X86_COMPAT_H

/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/processor.h>
#include <asm/user32.h>
#include <asm/unistd.h>

#define compat_mode_t	compat_mode_t
typedef u16		compat_mode_t;

#define __compat_uid_t	__compat_uid_t
typedef u16		__compat_uid_t;
typedef u16		__compat_gid_t;

#define compat_dev_t	compat_dev_t
typedef u16		compat_dev_t;

#define compat_ipc_pid_t compat_ipc_pid_t
typedef u16		 compat_ipc_pid_t;

#define compat_statfs	compat_statfs

#include <asm-generic/compat.h>

#define COMPAT_UTS_MACHINE	"i686\0\0"

typedef u16		compat_nlink_t;

struct compat_stat {
	compat_dev_t	st_dev;
	u16		__pad1;
	compat_ino_t	st_ino;
	compat_mode_t	st_mode;
	compat_nlink_t	st_nlink;
	__compat_uid_t	st_uid;
	__compat_gid_t	st_gid;
	compat_dev_t	st_rdev;
	u16		__pad2;
	u32		st_size;
	u32		st_blksize;
	u32		st_blocks;
	u32		st_atime;
	u32		st_atime_nsec;
	u32		st_mtime;
	u32		st_mtime_nsec;
	u32		st_ctime;
	u32		st_ctime_nsec;
	u32		__unused4;
	u32		__unused5;
};

/*
 * IA32 uses 4 byte alignment for 64 bit quantities, so we need to pack the
 * compat flock64 structure.
 */
#define __ARCH_NEED_COMPAT_FLOCK64_PACKED

struct compat_statfs {
	int		f_type;
	int		f_bsize;
	int		f_blocks;
	int		f_bfree;
	int		f_bavail;
	int		f_files;
	int		f_ffree;
	compat_fsid_t	f_fsid;
	int		f_namelen;	/* SunOS ignores this field. */
	int		f_frsize;
	int		f_flags;
	int		f_spare[4];
};

#ifdef CONFIG_X86_X32_ABI
#define COMPAT_USE_64BIT_TIME \
	(!!(task_pt_regs(current)->orig_ax & __X32_SYSCALL_BIT))
#endif

static inline bool in_x32_syscall(void)
{
#ifdef CONFIG_X86_X32_ABI
	if (task_pt_regs(current)->orig_ax & __X32_SYSCALL_BIT)
		return true;
#endif
	return false;
}

static inline bool in_32bit_syscall(void)
{
	return in_ia32_syscall() || in_x32_syscall();
}

#ifdef CONFIG_COMPAT
static inline bool in_compat_syscall(void)
{
	return in_32bit_syscall();
}
#define in_compat_syscall in_compat_syscall	/* override the generic impl */
#define compat_need_64bit_alignment_fixup in_ia32_syscall
#endif

struct compat_siginfo;

#ifdef CONFIG_X86_X32_ABI
int copy_siginfo_to_user32(struct compat_siginfo __user *to,
		const kernel_siginfo_t *from);
#define copy_siginfo_to_user32 copy_siginfo_to_user32
#endif /* CONFIG_X86_X32_ABI */

#endif /* _ASM_X86_COMPAT_H */
