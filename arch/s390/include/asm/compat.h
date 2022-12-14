/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390X_COMPAT_H
#define _ASM_S390X_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/thread_info.h>
#include <asm/ptrace.h>

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

#define __TYPE_IS_PTR(t) (!__builtin_types_compatible_p( \
				typeof(0?(__force t)0:0ULL), u64))

#define __SC_DELOUSE(t,v) ({ \
	BUILD_BUG_ON(sizeof(t) > 4 && !__TYPE_IS_PTR(t)); \
	(__force t)(__TYPE_IS_PTR(t) ? ((v) & 0x7fffffff) : (v)); \
})

#define PSW32_MASK_USER		0x0000FF00UL

#define PSW32_USER_BITS (PSW32_MASK_DAT | PSW32_MASK_IO | PSW32_MASK_EXT | \
			 PSW32_DEFAULT_KEY | PSW32_MASK_BASE | \
			 PSW32_MASK_MCHECK | PSW32_MASK_PSTATE | \
			 PSW32_ASC_PRIMARY)

#define COMPAT_UTS_MACHINE	"s390\0\0\0\0"

typedef u16		compat_nlink_t;

typedef struct {
	u32 mask;
	u32 addr;
} __aligned(8) psw_compat_t;

typedef struct {
	psw_compat_t psw;
	u32 gprs[NUM_GPRS];
	u32 acrs[NUM_ACRS];
	u32 orig_gpr2;
} s390_compat_regs;

typedef struct {
	u32 gprs_high[NUM_GPRS];
} s390_compat_regs_high;

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

struct compat_statfs {
	u32		f_type;
	u32		f_bsize;
	u32		f_blocks;
	u32		f_bfree;
	u32		f_bavail;
	u32		f_files;
	u32		f_ffree;
	compat_fsid_t	f_fsid;
	u32		f_namelen;
	u32		f_frsize;
	u32		f_flags;
	u32		f_spare[4];
};

struct compat_statfs64 {
	u32		f_type;
	u32		f_bsize;
	u64		f_blocks;
	u64		f_bfree;
	u64		f_bavail;
	u64		f_files;
	u64		f_ffree;
	compat_fsid_t	f_fsid;
	u32		f_namelen;
	u32		f_frsize;
	u32		f_flags;
	u32		f_spare[4];
};

/*
 * A pointer passed in from user mode. This should not
 * be used for syscall parameters, just declare them
 * as pointers because the syscall entry code will have
 * appropriately converted them already.
 */

static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(unsigned long)(uptr & 0x7fffffffUL);
}
#define compat_ptr(uptr) compat_ptr(uptr)

#ifdef CONFIG_COMPAT

static inline int is_compat_task(void)
{
	return test_thread_flag(TIF_31BIT);
}

#endif

#endif /* _ASM_S390X_COMPAT_H */
