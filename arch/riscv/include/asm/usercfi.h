/* SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2024 Rivos, Inc.
 * Deepak Gupta <debug@rivosinc.com>
 */
#ifndef _ASM_RISCV_USERCFI_H
#define _ASM_RISCV_USERCFI_H

#define CMDLINE_DISABLE_RISCV_USERCFI_FCFI	1
#define CMDLINE_DISABLE_RISCV_USERCFI_BCFI	2
#define CMDLINE_DISABLE_RISCV_USERCFI		3

#ifndef __ASSEMBLER__
#include <linux/types.h>
#include <linux/prctl.h>
#include <linux/errno.h>

struct task_struct;
struct kernel_clone_args;

extern unsigned long riscv_nousercfi;

#ifdef CONFIG_RISCV_USER_CFI
struct cfi_state {
	unsigned long ubcfi_en : 1; /* Enable for backward cfi. */
	unsigned long ubcfi_locked : 1;
	unsigned long ufcfi_en : 1; /* Enable for forward cfi. Note that ELP goes in sstatus */
	unsigned long ufcfi_locked : 1;
	unsigned long user_shdw_stk; /* Current user shadow stack pointer */
	unsigned long shdw_stk_base; /* Base address of shadow stack */
	unsigned long shdw_stk_size; /* size of shadow stack */
};

unsigned long shstk_alloc_thread_stack(struct task_struct *tsk,
				       const struct kernel_clone_args *args);
void shstk_release(struct task_struct *tsk);
void set_shstk_base(struct task_struct *task, unsigned long shstk_addr, unsigned long size);
unsigned long get_shstk_base(struct task_struct *task, unsigned long *size);
void set_active_shstk(struct task_struct *task, unsigned long shstk_addr);
bool is_shstk_enabled(struct task_struct *task);
bool is_shstk_locked(struct task_struct *task);
bool is_shstk_allocated(struct task_struct *task);
void set_shstk_lock(struct task_struct *task);
void set_shstk_status(struct task_struct *task, bool enable);
unsigned long get_active_shstk(struct task_struct *task);
int restore_user_shstk(struct task_struct *tsk, unsigned long shstk_ptr);
int save_user_shstk(struct task_struct *tsk, unsigned long *saved_shstk_ptr);
bool is_indir_lp_enabled(struct task_struct *task);
bool is_indir_lp_locked(struct task_struct *task);
void set_indir_lp_status(struct task_struct *task, bool enable);
void set_indir_lp_lock(struct task_struct *task);

#define PR_SHADOW_STACK_SUPPORTED_STATUS_MASK (PR_SHADOW_STACK_ENABLE)

#else

#define shstk_alloc_thread_stack(tsk, args) 0

#define shstk_release(tsk)

#define get_shstk_base(task, size) 0UL

#define set_shstk_base(task, shstk_addr, size) do {} while (0)

#define set_active_shstk(task, shstk_addr) do {} while (0)

#define is_shstk_enabled(task) false

#define is_shstk_locked(task) false

#define is_shstk_allocated(task) false

#define set_shstk_lock(task) do {} while (0)

#define set_shstk_status(task, enable) do {} while (0)

#define is_indir_lp_enabled(task) false

#define is_indir_lp_locked(task) false

#define set_indir_lp_status(task, enable) do {} while (0)

#define set_indir_lp_lock(task) do {} while (0)

#define restore_user_shstk(tsk, shstk_ptr) -EINVAL

#define save_user_shstk(tsk, saved_shstk_ptr) -EINVAL

#define get_active_shstk(task) 0UL

#endif /* CONFIG_RISCV_USER_CFI */

bool is_user_shstk_enabled(void);
bool is_user_lpad_enabled(void);

#endif /* __ASSEMBLER__ */

#endif /* _ASM_RISCV_USERCFI_H */
