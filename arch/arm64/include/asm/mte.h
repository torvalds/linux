/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#ifndef __ASM_MTE_H
#define __ASM_MTE_H

#ifndef __ASSEMBLY__

#include <linux/page-flags.h>

#include <asm/pgtable-types.h>

void mte_clear_page_tags(void *addr);

#ifdef CONFIG_ARM64_MTE

/* track which pages have valid allocation tags */
#define PG_mte_tagged	PG_arch_2

void mte_sync_tags(pte_t *ptep, pte_t pte);
void mte_copy_page_tags(void *kto, const void *kfrom);
void flush_mte_state(void);
void mte_thread_switch(struct task_struct *next);
void mte_suspend_exit(void);
long set_mte_ctrl(struct task_struct *task, unsigned long arg);
long get_mte_ctrl(struct task_struct *task);

#else

/* unused if !CONFIG_ARM64_MTE, silence the compiler */
#define PG_mte_tagged	0

static inline void mte_sync_tags(pte_t *ptep, pte_t pte)
{
}
static inline void mte_copy_page_tags(void *kto, const void *kfrom)
{
}
static inline void flush_mte_state(void)
{
}
static inline void mte_thread_switch(struct task_struct *next)
{
}
static inline void mte_suspend_exit(void)
{
}
static inline long set_mte_ctrl(struct task_struct *task, unsigned long arg)
{
	return 0;
}
static inline long get_mte_ctrl(struct task_struct *task)
{
	return 0;
}

#endif

#endif /* __ASSEMBLY__ */
#endif /* __ASM_MTE_H  */
