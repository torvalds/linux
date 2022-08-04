/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#ifndef __ASM_MTE_KASAN_H
#define __ASM_MTE_KASAN_H

#include <asm/mte-def.h>

#ifndef __ASSEMBLY__

#include <linux/types.h>

/*
 * The functions below are meant to be used only for the
 * KASAN_HW_TAGS interface defined in asm/memory.h.
 */
#ifdef CONFIG_ARM64_MTE

static inline u8 mte_get_ptr_tag(void *ptr)
{
	/* Note: The format of KASAN tags is 0xF<x> */
	u8 tag = 0xF0 | (u8)(((u64)(ptr)) >> MTE_TAG_SHIFT);

	return tag;
}

u8 mte_get_mem_tag(void *addr);
u8 mte_get_random_tag(void);
void *mte_set_mem_tag_range(void *addr, size_t size, u8 tag);

void mte_enable_kernel(void);
void mte_init_tags(u64 max_tag);

#else /* CONFIG_ARM64_MTE */

static inline u8 mte_get_ptr_tag(void *ptr)
{
	return 0xFF;
}

static inline u8 mte_get_mem_tag(void *addr)
{
	return 0xFF;
}
static inline u8 mte_get_random_tag(void)
{
	return 0xFF;
}
static inline void *mte_set_mem_tag_range(void *addr, size_t size, u8 tag)
{
	return addr;
}

static inline void mte_enable_kernel(void)
{
}

static inline void mte_init_tags(u64 max_tag)
{
}

#endif /* CONFIG_ARM64_MTE */

#endif /* __ASSEMBLY__ */

#endif /* __ASM_MTE_KASAN_H  */
