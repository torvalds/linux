/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifndef __ASSEMBLER__

#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/pgtable.h>

#define KASAN_SHADOW_SCALE_SHIFT 3
#define KASAN_SHADOW_OFFSET	_AC(CONFIG_KASAN_SHADOW_OFFSET, UL)

#define XRANGE_SHIFT (48)

/* Valid address length */
#define XRANGE_SHADOW_SHIFT	min(cpu_vabits, VA_BITS)
/* Used for taking out the valid address */
#define XRANGE_SHADOW_MASK	GENMASK_ULL(XRANGE_SHADOW_SHIFT - 1, 0)
/* One segment whole address space size */
#define XRANGE_SIZE		(XRANGE_SHADOW_MASK + 1)

/* 64-bit segment value. */
#define XKPRANGE_UC_SEG		(0x8000)
#define XKPRANGE_CC_SEG		(0x9000)
#define XKPRANGE_WC_SEG		(0xa000)
#define XKVRANGE_VC_SEG		(0xffff)

/* Cached */
#define XKPRANGE_CC_START		CACHE_BASE
#define XKPRANGE_CC_SIZE		XRANGE_SIZE
#define XKPRANGE_CC_KASAN_OFFSET	(0)
#define XKPRANGE_CC_SHADOW_SIZE		(XKPRANGE_CC_SIZE >> KASAN_SHADOW_SCALE_SHIFT)
#define XKPRANGE_CC_SHADOW_END		(XKPRANGE_CC_KASAN_OFFSET + XKPRANGE_CC_SHADOW_SIZE)

/* UnCached */
#define XKPRANGE_UC_START		UNCACHE_BASE
#define XKPRANGE_UC_SIZE		XRANGE_SIZE
#define XKPRANGE_UC_KASAN_OFFSET	XKPRANGE_CC_SHADOW_END
#define XKPRANGE_UC_SHADOW_SIZE		(XKPRANGE_UC_SIZE >> KASAN_SHADOW_SCALE_SHIFT)
#define XKPRANGE_UC_SHADOW_END		(XKPRANGE_UC_KASAN_OFFSET + XKPRANGE_UC_SHADOW_SIZE)

/* WriteCombine */
#define XKPRANGE_WC_START		WRITECOMBINE_BASE
#define XKPRANGE_WC_SIZE		XRANGE_SIZE
#define XKPRANGE_WC_KASAN_OFFSET	XKPRANGE_UC_SHADOW_END
#define XKPRANGE_WC_SHADOW_SIZE		(XKPRANGE_WC_SIZE >> KASAN_SHADOW_SCALE_SHIFT)
#define XKPRANGE_WC_SHADOW_END		(XKPRANGE_WC_KASAN_OFFSET + XKPRANGE_WC_SHADOW_SIZE)

/* VMALLOC (Cached or UnCached)  */
#define XKVRANGE_VC_START		MODULES_VADDR
#define XKVRANGE_VC_SIZE		round_up(KFENCE_AREA_END - MODULES_VADDR + 1, PGDIR_SIZE)
#define XKVRANGE_VC_KASAN_OFFSET	XKPRANGE_WC_SHADOW_END
#define XKVRANGE_VC_SHADOW_SIZE		(XKVRANGE_VC_SIZE >> KASAN_SHADOW_SCALE_SHIFT)
#define XKVRANGE_VC_SHADOW_END		(XKVRANGE_VC_KASAN_OFFSET + XKVRANGE_VC_SHADOW_SIZE)

/* KAsan shadow memory start right after vmalloc. */
#define KASAN_SHADOW_START		round_up(KFENCE_AREA_END, PGDIR_SIZE)
#define KASAN_SHADOW_SIZE		(XKVRANGE_VC_SHADOW_END - XKPRANGE_CC_KASAN_OFFSET)
#define KASAN_SHADOW_END		(round_up(KASAN_SHADOW_START + KASAN_SHADOW_SIZE, PGDIR_SIZE) - 1)

#define XKPRANGE_CC_SHADOW_OFFSET	(KASAN_SHADOW_START + XKPRANGE_CC_KASAN_OFFSET)
#define XKPRANGE_UC_SHADOW_OFFSET	(KASAN_SHADOW_START + XKPRANGE_UC_KASAN_OFFSET)
#define XKPRANGE_WC_SHADOW_OFFSET	(KASAN_SHADOW_START + XKPRANGE_WC_KASAN_OFFSET)
#define XKVRANGE_VC_SHADOW_OFFSET	(KASAN_SHADOW_START + XKVRANGE_VC_KASAN_OFFSET)

extern unsigned char kasan_early_shadow_page[PAGE_SIZE];

#define kasan_mem_to_shadow kasan_mem_to_shadow
void *kasan_mem_to_shadow(const void *addr);

#define kasan_shadow_to_mem kasan_shadow_to_mem
const void *kasan_shadow_to_mem(const void *shadow_addr);

#define addr_has_metadata addr_has_metadata
static __always_inline bool addr_has_metadata(const void *addr)
{
	return (kasan_mem_to_shadow((void *)addr) != NULL);
}

void kasan_init(void);
asmlinkage void kasan_early_init(void);

#endif
#endif
