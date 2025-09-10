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

	/* GCSSTTR x1, x0 */
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

#else

static inline bool task_gcs_el0_enabled(struct task_struct *task)
{
	return false;
}

static inline void gcs_set_el0_mode(struct task_struct *task) { }
static inline void gcs_free(struct task_struct *task) { }
static inline void gcs_preserve_current_state(void) { }
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

#endif

#endif
