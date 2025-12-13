/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 ARM Ltd.
 */
#ifndef __ASM_GCS_H
#define __ASM_GCS_H

#include <asm/types.h>
#include <asm/uaccess.h>

struct kernel_clone_args;
struct ksignal;

static inline void gcsb_dsync(void)
{
	asm volatile(".inst 0xd503227f" : : : "memory");
}

static inline void gcsstr(u64 *addr, u64 val)
{
	register u64 *_addr __asm__ ("x0") = addr;
	register long _val __asm__ ("x1") = val;

	/* GCSSTTR x1, [x0] */
	asm volatile(
		".inst 0xd91f1c01\n"
		:
		: "rZ" (_val), "r" (_addr)
		: "memory");
}

static inline void gcsss1(u64 Xt)
{
	asm volatile (
		"sys #3, C7, C7, #2, %0\n"
		:
		: "rZ" (Xt)
		: "memory");
}

static inline u64 gcsss2(void)
{
	u64 Xt;

	asm volatile(
		"SYSL %0, #3, C7, C7, #3\n"
		: "=r" (Xt)
		:
		: "memory");

	return Xt;
}

#define PR_SHADOW_STACK_SUPPORTED_STATUS_MASK \
	(PR_SHADOW_STACK_ENABLE | PR_SHADOW_STACK_WRITE | PR_SHADOW_STACK_PUSH)

#ifdef CONFIG_ARM64_GCS

static inline bool task_gcs_el0_enabled(struct task_struct *task)
{
	return task->thread.gcs_el0_mode & PR_SHADOW_STACK_ENABLE;
}

void gcs_set_el0_mode(struct task_struct *task);
void gcs_free(struct task_struct *task);
void gcs_preserve_current_state(void);
unsigned long gcs_alloc_thread_stack(struct task_struct *tsk,
				     const struct kernel_clone_args *args);

static inline int gcs_check_locked(struct task_struct *task,
				   unsigned long new_val)
{
	unsigned long cur_val = task->thread.gcs_el0_mode;

	cur_val &= task->thread.gcs_el0_locked;
	new_val &= task->thread.gcs_el0_locked;

	if (cur_val != new_val)
		return -EBUSY;

	return 0;
}

static inline int gcssttr(unsigned long __user *addr, unsigned long val)
{
	register unsigned long __user *_addr __asm__ ("x0") = addr;
	register unsigned long _val __asm__ ("x1") = val;
	int err = 0;

	/* GCSSTTR x1, [x0] */
	asm volatile(
		"1: .inst 0xd91f1c01\n"
		"2: \n"
		_ASM_EXTABLE_UACCESS_ERR(1b, 2b, %w0)
		: "+r" (err)
		: "rZ" (_val), "r" (_addr)
		: "memory");

	return err;
}

static inline void put_user_gcs(unsigned long val, unsigned long __user *addr,
				int *err)
{
	int ret;

	if (!access_ok((char __user *)addr, sizeof(u64))) {
		*err = -EFAULT;
		return;
	}

	uaccess_ttbr0_enable();
	ret = gcssttr(addr, val);
	if (ret != 0)
		*err = ret;
	uaccess_ttbr0_disable();
}

static inline void push_user_gcs(unsigned long val, int *err)
{
	u64 gcspr = read_sysreg_s(SYS_GCSPR_EL0);

	gcspr -= sizeof(u64);
	put_user_gcs(val, (unsigned long __user *)gcspr, err);
	if (!*err)
		write_sysreg_s(gcspr, SYS_GCSPR_EL0);
}

/*
 * Unlike put/push_user_gcs() above, get/pop_user_gsc() doesn't
 * validate the GCS permission is set on the page being read.  This
 * differs from how the hardware works when it consumes data stored at
 * GCSPR. Callers should ensure this is acceptable.
 */
static inline u64 get_user_gcs(unsigned long __user *addr, int *err)
{
	unsigned long ret;
	u64 load = 0;

	/* Ensure previous GCS operation are visible before we read the page */
	gcsb_dsync();
	ret = copy_from_user(&load, addr, sizeof(load));
	if (ret != 0)
		*err = ret;
	return load;
}

static inline u64 pop_user_gcs(int *err)
{
	u64 gcspr = read_sysreg_s(SYS_GCSPR_EL0);
	u64 read_val;

	read_val = get_user_gcs((__force unsigned long __user *)gcspr, err);
	if (!*err)
		write_sysreg_s(gcspr + sizeof(u64), SYS_GCSPR_EL0);

	return read_val;
}

#else

static inline bool task_gcs_el0_enabled(struct task_struct *task)
{
	return false;
}

static inline void gcs_set_el0_mode(struct task_struct *task) { }
static inline void gcs_free(struct task_struct *task) { }
static inline void gcs_preserve_current_state(void) { }
static inline void put_user_gcs(unsigned long val, unsigned long __user *addr,
				int *err) { }
static inline void push_user_gcs(unsigned long val, int *err) { }

static inline unsigned long gcs_alloc_thread_stack(struct task_struct *tsk,
						   const struct kernel_clone_args *args)
{
	return -ENOTSUPP;
}
static inline int gcs_check_locked(struct task_struct *task,
				   unsigned long new_val)
{
	return 0;
}
static inline u64 get_user_gcs(unsigned long __user *addr, int *err)
{
	*err = -EFAULT;
	return 0;
}
static inline u64 pop_user_gcs(int *err)
{
	return 0;
}

#endif

#endif
