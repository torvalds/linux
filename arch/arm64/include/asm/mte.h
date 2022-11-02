/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#ifndef __ASM_MTE_H
#define __ASM_MTE_H

#include <asm/compiler.h>
#include <asm/mte-def.h>

#ifndef __ASSEMBLY__

#include <linux/bitfield.h>
#include <linux/page-flags.h>
#include <linux/types.h>

#include <asm/pgtable-types.h>

void mte_clear_page_tags(void *addr);
unsigned long mte_copy_tags_from_user(void *to, const void __user *from,
				      unsigned long n);
unsigned long mte_copy_tags_to_user(void __user *to, void *from,
				    unsigned long n);
int mte_save_tags(struct page *page);
void mte_save_page_tags(const void *page_addr, void *tag_storage);
bool mte_restore_tags(swp_entry_t entry, struct page *page);
void mte_restore_page_tags(void *page_addr, const void *tag_storage);
void mte_invalidate_tags(int type, pgoff_t offset);
void mte_invalidate_tags_area(int type);
void *mte_allocate_tag_storage(void);
void mte_free_tag_storage(char *storage);

#ifdef CONFIG_ARM64_MTE

/* track which pages have valid allocation tags */
#define PG_mte_tagged	PG_arch_2

void mte_zero_clear_page_tags(void *addr);
void mte_sync_tags(pte_t old_pte, pte_t pte);
void mte_copy_page_tags(void *kto, const void *kfrom);
void mte_thread_init_user(void);
void mte_thread_switch(struct task_struct *next);
void mte_cpu_setup(void);
void mte_suspend_enter(void);
void mte_suspend_exit(void);
long set_mte_ctrl(struct task_struct *task, unsigned long arg);
long get_mte_ctrl(struct task_struct *task);
int mte_ptrace_copy_tags(struct task_struct *child, long request,
			 unsigned long addr, unsigned long data);

#else /* CONFIG_ARM64_MTE */

/* unused if !CONFIG_ARM64_MTE, silence the compiler */
#define PG_mte_tagged	0

static inline void mte_zero_clear_page_tags(void *addr)
{
}
static inline void mte_sync_tags(pte_t old_pte, pte_t pte)
{
}
static inline void mte_copy_page_tags(void *kto, const void *kfrom)
{
}
static inline void mte_thread_init_user(void)
{
}
static inline void mte_thread_switch(struct task_struct *next)
{
}
static inline void mte_suspend_enter(void)
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
static inline int mte_ptrace_copy_tags(struct task_struct *child,
				       long request, unsigned long addr,
				       unsigned long data)
{
	return -EIO;
}

#endif /* CONFIG_ARM64_MTE */

#ifdef CONFIG_KASAN_HW_TAGS
/* Whether the MTE asynchronous mode is enabled. */
DECLARE_STATIC_KEY_FALSE(mte_async_mode);

static inline bool system_uses_mte_async_mode(void)
{
	return static_branch_unlikely(&mte_async_mode);
}

void mte_check_tfsr_el1(void);

static inline void mte_check_tfsr_entry(void)
{
	if (!system_supports_mte())
		return;

	mte_check_tfsr_el1();
}

static inline void mte_check_tfsr_exit(void)
{
	if (!system_supports_mte())
		return;

	/*
	 * The asynchronous faults are sync'ed automatically with
	 * TFSR_EL1 on kernel entry but for exit an explicit dsb()
	 * is required.
	 */
	dsb(nsh);
	isb();

	mte_check_tfsr_el1();
}
#else
static inline bool system_uses_mte_async_mode(void)
{
	return false;
}
static inline void mte_check_tfsr_el1(void)
{
}
static inline void mte_check_tfsr_entry(void)
{
}
static inline void mte_check_tfsr_exit(void)
{
}
#endif /* CONFIG_KASAN_HW_TAGS */

#endif /* __ASSEMBLY__ */
#endif /* __ASM_MTE_H  */
