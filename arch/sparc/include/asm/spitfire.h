/* spitfire.h: SpitFire/BlackBird/Cheetah inline MMU operations.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC64_SPITFIRE_H
#define _SPARC64_SPITFIRE_H

#ifdef CONFIG_SPARC64

#include <asm/asi.h>

/* The following register addresses are accessible via ASI_DMMU
 * and ASI_IMMU, that is there is a distinct and unique copy of
 * each these registers for each TLB.
 */
#define TSB_TAG_TARGET		0x0000000000000000 /* All chips				*/
#define TLB_SFSR		0x0000000000000018 /* All chips				*/
#define TSB_REG			0x0000000000000028 /* All chips				*/
#define TLB_TAG_ACCESS		0x0000000000000030 /* All chips				*/
#define VIRT_WATCHPOINT		0x0000000000000038 /* All chips				*/
#define PHYS_WATCHPOINT		0x0000000000000040 /* All chips				*/
#define TSB_EXTENSION_P		0x0000000000000048 /* Ultra-III and later		*/
#define TSB_EXTENSION_S		0x0000000000000050 /* Ultra-III and later, D-TLB only	*/
#define TSB_EXTENSION_N		0x0000000000000058 /* Ultra-III and later		*/
#define TLB_TAG_ACCESS_EXT	0x0000000000000060 /* Ultra-III+ and later		*/

/* These registers only exist as one entity, and are accessed
 * via ASI_DMMU only.
 */
#define PRIMARY_CONTEXT		0x0000000000000008
#define SECONDARY_CONTEXT	0x0000000000000010
#define DMMU_SFAR		0x0000000000000020
#define VIRT_WATCHPOINT		0x0000000000000038
#define PHYS_WATCHPOINT		0x0000000000000040

#define SPITFIRE_HIGHEST_LOCKED_TLBENT	(64 - 1)
#define CHEETAH_HIGHEST_LOCKED_TLBENT	(16 - 1)

#define L1DCACHE_SIZE		0x4000

#define SUN4V_CHIP_INVALID	0x00
#define SUN4V_CHIP_NIAGARA1	0x01
#define SUN4V_CHIP_NIAGARA2	0x02
#define SUN4V_CHIP_NIAGARA3	0x03
#define SUN4V_CHIP_NIAGARA4	0x04
#define SUN4V_CHIP_NIAGARA5	0x05
#define SUN4V_CHIP_SPARC64X	0x8a
#define SUN4V_CHIP_UNKNOWN	0xff

#ifndef __ASSEMBLY__

enum ultra_tlb_layout {
	spitfire = 0,
	cheetah = 1,
	cheetah_plus = 2,
	hypervisor = 3,
};

extern enum ultra_tlb_layout tlb_type;

extern int sun4v_chip_type;

extern int cheetah_pcache_forced_on;
void cheetah_enable_pcache(void);

#define sparc64_highest_locked_tlbent()	\
	(tlb_type == spitfire ? \
	 SPITFIRE_HIGHEST_LOCKED_TLBENT : \
	 CHEETAH_HIGHEST_LOCKED_TLBENT)

extern int num_kernel_image_mappings;

/* The data cache is write through, so this just invalidates the
 * specified line.
 */
static inline void spitfire_put_dcache_tag(unsigned long addr, unsigned long tag)
{
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (tag), "r" (addr), "i" (ASI_DCACHE_TAG));
}

/* The instruction cache lines are flushed with this, but note that
 * this does not flush the pipeline.  It is possible for a line to
 * get flushed but stale instructions to still be in the pipeline,
 * a flush instruction (to any address) is sufficient to handle
 * this issue after the line is invalidated.
 */
static inline void spitfire_put_icache_tag(unsigned long addr, unsigned long tag)
{
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (tag), "r" (addr), "i" (ASI_IC_TAG));
}

static inline unsigned long spitfire_get_dtlb_data(int entry)
{
	unsigned long data;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (data)
			     : "r" (entry << 3), "i" (ASI_DTLB_DATA_ACCESS));

	/* Clear TTE diag bits. */
	data &= ~0x0003fe0000000000UL;

	return data;
}

static inline unsigned long spitfire_get_dtlb_tag(int entry)
{
	unsigned long tag;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (tag)
			     : "r" (entry << 3), "i" (ASI_DTLB_TAG_READ));
	return tag;
}

static inline void spitfire_put_dtlb_data(int entry, unsigned long data)
{
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (data), "r" (entry << 3),
			       "i" (ASI_DTLB_DATA_ACCESS));
}

static inline unsigned long spitfire_get_itlb_data(int entry)
{
	unsigned long data;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (data)
			     : "r" (entry << 3), "i" (ASI_ITLB_DATA_ACCESS));

	/* Clear TTE diag bits. */
	data &= ~0x0003fe0000000000UL;

	return data;
}

static inline unsigned long spitfire_get_itlb_tag(int entry)
{
	unsigned long tag;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (tag)
			     : "r" (entry << 3), "i" (ASI_ITLB_TAG_READ));
	return tag;
}

static inline void spitfire_put_itlb_data(int entry, unsigned long data)
{
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (data), "r" (entry << 3),
			       "i" (ASI_ITLB_DATA_ACCESS));
}

static inline void spitfire_flush_dtlb_nucleus_page(unsigned long page)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (page | 0x20), "i" (ASI_DMMU_DEMAP));
}

static inline void spitfire_flush_itlb_nucleus_page(unsigned long page)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (page | 0x20), "i" (ASI_IMMU_DEMAP));
}

/* Cheetah has "all non-locked" tlb flushes. */
static inline void cheetah_flush_dtlb_all(void)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (0x80), "i" (ASI_DMMU_DEMAP));
}

static inline void cheetah_flush_itlb_all(void)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (0x80), "i" (ASI_IMMU_DEMAP));
}

/* Cheetah has a 4-tlb layout so direct access is a bit different.
 * The first two TLBs are fully assosciative, hold 16 entries, and are
 * used only for locked and >8K sized translations.  One exists for
 * data accesses and one for instruction accesses.
 *
 * The third TLB is for data accesses to 8K non-locked translations, is
 * 2 way assosciative, and holds 512 entries.  The fourth TLB is for
 * instruction accesses to 8K non-locked translations, is 2 way
 * assosciative, and holds 128 entries.
 *
 * Cheetah has some bug where bogus data can be returned from
 * ASI_{D,I}TLB_DATA_ACCESS loads, doing the load twice fixes
 * the problem for me. -DaveM
 */
static inline unsigned long cheetah_get_ldtlb_data(int entry)
{
	unsigned long data;

	__asm__ __volatile__("ldxa	[%1] %2, %%g0\n\t"
			     "ldxa	[%1] %2, %0"
			     : "=r" (data)
			     : "r" ((0 << 16) | (entry << 3)),
			     "i" (ASI_DTLB_DATA_ACCESS));

	return data;
}

static inline unsigned long cheetah_get_litlb_data(int entry)
{
	unsigned long data;

	__asm__ __volatile__("ldxa	[%1] %2, %%g0\n\t"
			     "ldxa	[%1] %2, %0"
			     : "=r" (data)
			     : "r" ((0 << 16) | (entry << 3)),
			     "i" (ASI_ITLB_DATA_ACCESS));

	return data;
}

static inline unsigned long cheetah_get_ldtlb_tag(int entry)
{
	unsigned long tag;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (tag)
			     : "r" ((0 << 16) | (entry << 3)),
			     "i" (ASI_DTLB_TAG_READ));

	return tag;
}

static inline unsigned long cheetah_get_litlb_tag(int entry)
{
	unsigned long tag;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (tag)
			     : "r" ((0 << 16) | (entry << 3)),
			     "i" (ASI_ITLB_TAG_READ));

	return tag;
}

static inline void cheetah_put_ldtlb_data(int entry, unsigned long data)
{
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (data),
			       "r" ((0 << 16) | (entry << 3)),
			       "i" (ASI_DTLB_DATA_ACCESS));
}

static inline void cheetah_put_litlb_data(int entry, unsigned long data)
{
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (data),
			       "r" ((0 << 16) | (entry << 3)),
			       "i" (ASI_ITLB_DATA_ACCESS));
}

static inline unsigned long cheetah_get_dtlb_data(int entry, int tlb)
{
	unsigned long data;

	__asm__ __volatile__("ldxa	[%1] %2, %%g0\n\t"
			     "ldxa	[%1] %2, %0"
			     : "=r" (data)
			     : "r" ((tlb << 16) | (entry << 3)), "i" (ASI_DTLB_DATA_ACCESS));

	return data;
}

static inline unsigned long cheetah_get_dtlb_tag(int entry, int tlb)
{
	unsigned long tag;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (tag)
			     : "r" ((tlb << 16) | (entry << 3)), "i" (ASI_DTLB_TAG_READ));
	return tag;
}

static inline void cheetah_put_dtlb_data(int entry, unsigned long data, int tlb)
{
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (data),
			       "r" ((tlb << 16) | (entry << 3)),
			       "i" (ASI_DTLB_DATA_ACCESS));
}

static inline unsigned long cheetah_get_itlb_data(int entry)
{
	unsigned long data;

	__asm__ __volatile__("ldxa	[%1] %2, %%g0\n\t"
			     "ldxa	[%1] %2, %0"
			     : "=r" (data)
			     : "r" ((2 << 16) | (entry << 3)),
                               "i" (ASI_ITLB_DATA_ACCESS));

	return data;
}

static inline unsigned long cheetah_get_itlb_tag(int entry)
{
	unsigned long tag;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (tag)
			     : "r" ((2 << 16) | (entry << 3)), "i" (ASI_ITLB_TAG_READ));
	return tag;
}

static inline void cheetah_put_itlb_data(int entry, unsigned long data)
{
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* No outputs */
			     : "r" (data), "r" ((2 << 16) | (entry << 3)),
			       "i" (ASI_ITLB_DATA_ACCESS));
}

#endif /* !(__ASSEMBLY__) */
#endif /* CONFIG_SPARC64 */
#endif /* !(_SPARC64_SPITFIRE_H) */
