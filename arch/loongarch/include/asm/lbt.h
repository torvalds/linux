/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Qi Hu <huqi@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */
#ifndef _ASM_LBT_H
#define _ASM_LBT_H

#include <asm/cpu.h>
#include <asm/current.h>
#include <asm/loongarch.h>
#include <asm/processor.h>

extern void _init_lbt(void);
extern void _save_lbt(struct loongarch_lbt *);
extern void _restore_lbt(struct loongarch_lbt *);

static inline int is_lbt_enabled(void)
{
	if (!cpu_has_lbt)
		return 0;

	return (csr_read32(LOONGARCH_CSR_EUEN) & CSR_EUEN_LBTEN) ?
		1 : 0;
}

static inline int is_lbt_owner(void)
{
	return test_thread_flag(TIF_USEDLBT);
}

#ifdef CONFIG_CPU_HAS_LBT

static inline void enable_lbt(void)
{
	if (cpu_has_lbt)
		csr_xchg32(CSR_EUEN_LBTEN, CSR_EUEN_LBTEN, LOONGARCH_CSR_EUEN);
}

static inline void disable_lbt(void)
{
	if (cpu_has_lbt)
		csr_xchg32(0, CSR_EUEN_LBTEN, LOONGARCH_CSR_EUEN);
}

static inline void __own_lbt(void)
{
	enable_lbt();
	set_thread_flag(TIF_USEDLBT);
	KSTK_EUEN(current) |= CSR_EUEN_LBTEN;
}

static inline void own_lbt_inatomic(int restore)
{
	if (cpu_has_lbt && !is_lbt_owner()) {
		__own_lbt();
		if (restore)
			_restore_lbt(&current->thread.lbt);
	}
}

static inline void own_lbt(int restore)
{
	preempt_disable();
	own_lbt_inatomic(restore);
	preempt_enable();
}

static inline void lose_lbt_inatomic(int save, struct task_struct *tsk)
{
	if (cpu_has_lbt && is_lbt_owner()) {
		if (save)
			_save_lbt(&tsk->thread.lbt);

		disable_lbt();
		clear_tsk_thread_flag(tsk, TIF_USEDLBT);
	}
	KSTK_EUEN(tsk) &= ~(CSR_EUEN_LBTEN);
}

static inline void lose_lbt(int save)
{
	preempt_disable();
	lose_lbt_inatomic(save, current);
	preempt_enable();
}

static inline void init_lbt(void)
{
	__own_lbt();
	_init_lbt();
}
#else
static inline void own_lbt_inatomic(int restore) {}
static inline void lose_lbt_inatomic(int save, struct task_struct *tsk) {}
static inline void init_lbt(void) {}
static inline void lose_lbt(int save) {}
#endif

static inline int thread_lbt_context_live(void)
{
	if (!cpu_has_lbt)
		return 0;

	return test_thread_flag(TIF_LBT_CTX_LIVE);
}

#endif /* _ASM_LBT_H */
