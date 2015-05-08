/*
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

#include <asm/cache.h>
#include <asm/soc.h>

/*
 * Internal Memory Control Registers for caches
 */
#define IMCR_CCFG	  0x0000
#define IMCR_L1PCFG	  0x0020
#define IMCR_L1PCC	  0x0024
#define IMCR_L1DCFG	  0x0040
#define IMCR_L1DCC	  0x0044
#define IMCR_L2ALLOC0	  0x2000
#define IMCR_L2ALLOC1	  0x2004
#define IMCR_L2ALLOC2	  0x2008
#define IMCR_L2ALLOC3	  0x200c
#define IMCR_L2WBAR	  0x4000
#define IMCR_L2WWC	  0x4004
#define IMCR_L2WIBAR	  0x4010
#define IMCR_L2WIWC	  0x4014
#define IMCR_L2IBAR	  0x4018
#define IMCR_L2IWC	  0x401c
#define IMCR_L1PIBAR	  0x4020
#define IMCR_L1PIWC	  0x4024
#define IMCR_L1DWIBAR	  0x4030
#define IMCR_L1DWIWC	  0x4034
#define IMCR_L1DWBAR	  0x4040
#define IMCR_L1DWWC	  0x4044
#define IMCR_L1DIBAR	  0x4048
#define IMCR_L1DIWC	  0x404c
#define IMCR_L2WB	  0x5000
#define IMCR_L2WBINV	  0x5004
#define IMCR_L2INV	  0x5008
#define IMCR_L1PINV	  0x5028
#define IMCR_L1DWB	  0x5040
#define IMCR_L1DWBINV	  0x5044
#define IMCR_L1DINV	  0x5048
#define IMCR_MAR_BASE	  0x8000
#define IMCR_MAR96_111	  0x8180
#define IMCR_MAR128_191   0x8200
#define IMCR_MAR224_239   0x8380
#define IMCR_L2MPFAR	  0xa000
#define IMCR_L2MPFSR	  0xa004
#define IMCR_L2MPFCR	  0xa008
#define IMCR_L2MPLK0	  0xa100
#define IMCR_L2MPLK1	  0xa104
#define IMCR_L2MPLK2	  0xa108
#define IMCR_L2MPLK3	  0xa10c
#define IMCR_L2MPLKCMD	  0xa110
#define IMCR_L2MPLKSTAT   0xa114
#define IMCR_L2MPPA_BASE  0xa200
#define IMCR_L1PMPFAR	  0xa400
#define IMCR_L1PMPFSR	  0xa404
#define IMCR_L1PMPFCR	  0xa408
#define IMCR_L1PMPLK0	  0xa500
#define IMCR_L1PMPLK1	  0xa504
#define IMCR_L1PMPLK2	  0xa508
#define IMCR_L1PMPLK3	  0xa50c
#define IMCR_L1PMPLKCMD   0xa510
#define IMCR_L1PMPLKSTAT  0xa514
#define IMCR_L1PMPPA_BASE 0xa600
#define IMCR_L1DMPFAR	  0xac00
#define IMCR_L1DMPFSR	  0xac04
#define IMCR_L1DMPFCR	  0xac08
#define IMCR_L1DMPLK0	  0xad00
#define IMCR_L1DMPLK1	  0xad04
#define IMCR_L1DMPLK2	  0xad08
#define IMCR_L1DMPLK3	  0xad0c
#define IMCR_L1DMPLKCMD   0xad10
#define IMCR_L1DMPLKSTAT  0xad14
#define IMCR_L1DMPPA_BASE 0xae00
#define IMCR_L2PDWAKE0	  0xc040
#define IMCR_L2PDWAKE1	  0xc044
#define IMCR_L2PDSLEEP0   0xc050
#define IMCR_L2PDSLEEP1   0xc054
#define IMCR_L2PDSTAT0	  0xc060
#define IMCR_L2PDSTAT1	  0xc064

/*
 * CCFG register values and bits
 */
#define L2MODE_0K_CACHE   0x0
#define L2MODE_32K_CACHE  0x1
#define L2MODE_64K_CACHE  0x2
#define L2MODE_128K_CACHE 0x3
#define L2MODE_256K_CACHE 0x7

#define L2PRIO_URGENT     0x0
#define L2PRIO_HIGH       0x1
#define L2PRIO_MEDIUM     0x2
#define L2PRIO_LOW        0x3

#define CCFG_ID           0x100   /* Invalidate L1P bit */
#define CCFG_IP           0x200   /* Invalidate L1D bit */

static void __iomem *cache_base;

/*
 * L1 & L2 caches generic functions
 */
#define imcr_get(reg) soc_readl(cache_base + (reg))
#define imcr_set(reg, value) \
do {								\
	soc_writel((value), cache_base + (reg));		\
	soc_readl(cache_base + (reg));				\
} while (0)

static void cache_block_operation_wait(unsigned int wc_reg)
{
	/* Wait for completion */
	while (imcr_get(wc_reg))
		cpu_relax();
}

static DEFINE_SPINLOCK(cache_lock);

/*
 * Generic function to perform a block cache operation as
 * invalidate or writeback/invalidate
 */
static void cache_block_operation(unsigned int *start,
				  unsigned int *end,
				  unsigned int bar_reg,
				  unsigned int wc_reg)
{
	unsigned long flags;
	unsigned int wcnt =
		(L2_CACHE_ALIGN_CNT((unsigned int) end)
		 - L2_CACHE_ALIGN_LOW((unsigned int) start)) >> 2;
	unsigned int wc = 0;

	for (; wcnt; wcnt -= wc, start += wc) {
loop:
		spin_lock_irqsave(&cache_lock, flags);

		/*
		 * If another cache operation is occuring
		 */
		if (unlikely(imcr_get(wc_reg))) {
			spin_unlock_irqrestore(&cache_lock, flags);

			/* Wait for previous operation completion */
			cache_block_operation_wait(wc_reg);

			/* Try again */
			goto loop;
		}

		imcr_set(bar_reg, L2_CACHE_ALIGN_LOW((unsigned int) start));

		if (wcnt > 0xffff)
			wc = 0xffff;
		else
			wc = wcnt;

		/* Set word count value in the WC register */
		imcr_set(wc_reg, wc & 0xffff);

		spin_unlock_irqrestore(&cache_lock, flags);

		/* Wait for completion */
		cache_block_operation_wait(wc_reg);
	}
}

static void cache_block_operation_nowait(unsigned int *start,
					 unsigned int *end,
					 unsigned int bar_reg,
					 unsigned int wc_reg)
{
	unsigned long flags;
	unsigned int wcnt =
		(L2_CACHE_ALIGN_CNT((unsigned int) end)
		 - L2_CACHE_ALIGN_LOW((unsigned int) start)) >> 2;
	unsigned int wc = 0;

	for (; wcnt; wcnt -= wc, start += wc) {

		spin_lock_irqsave(&cache_lock, flags);

		imcr_set(bar_reg, L2_CACHE_ALIGN_LOW((unsigned int) start));

		if (wcnt > 0xffff)
			wc = 0xffff;
		else
			wc = wcnt;

		/* Set word count value in the WC register */
		imcr_set(wc_reg, wc & 0xffff);

		spin_unlock_irqrestore(&cache_lock, flags);

		/* Don't wait for completion on last cache operation */
		if (wcnt > 0xffff)
			cache_block_operation_wait(wc_reg);
	}
}

/*
 * L1 caches management
 */

/*
 * Disable L1 caches
 */
void L1_cache_off(void)
{
	unsigned int dummy;

	imcr_set(IMCR_L1PCFG, 0);
	dummy = imcr_get(IMCR_L1PCFG);

	imcr_set(IMCR_L1DCFG, 0);
	dummy = imcr_get(IMCR_L1DCFG);
}

/*
 * Enable L1 caches
 */
void L1_cache_on(void)
{
	unsigned int dummy;

	imcr_set(IMCR_L1PCFG, 7);
	dummy = imcr_get(IMCR_L1PCFG);

	imcr_set(IMCR_L1DCFG, 7);
	dummy = imcr_get(IMCR_L1DCFG);
}

/*
 *  L1P global-invalidate all
 */
void L1P_cache_global_invalidate(void)
{
	unsigned int set = 1;
	imcr_set(IMCR_L1PINV, set);
	while (imcr_get(IMCR_L1PINV) & 1)
		cpu_relax();
}

/*
 *  L1D global-invalidate all
 *
 * Warning: this operation causes all updated data in L1D to
 * be discarded rather than written back to the lower levels of
 * memory
 */
void L1D_cache_global_invalidate(void)
{
	unsigned int set = 1;
	imcr_set(IMCR_L1DINV, set);
	while (imcr_get(IMCR_L1DINV) & 1)
		cpu_relax();
}

void L1D_cache_global_writeback(void)
{
	unsigned int set = 1;
	imcr_set(IMCR_L1DWB, set);
	while (imcr_get(IMCR_L1DWB) & 1)
		cpu_relax();
}

void L1D_cache_global_writeback_invalidate(void)
{
	unsigned int set = 1;
	imcr_set(IMCR_L1DWBINV, set);
	while (imcr_get(IMCR_L1DWBINV) & 1)
		cpu_relax();
}

/*
 * L2 caches management
 */

/*
 * Set L2 operation mode
 */
void L2_cache_set_mode(unsigned int mode)
{
	unsigned int ccfg = imcr_get(IMCR_CCFG);

	/* Clear and set the L2MODE bits in CCFG */
	ccfg &= ~7;
	ccfg |= (mode & 7);
	imcr_set(IMCR_CCFG, ccfg);
	ccfg = imcr_get(IMCR_CCFG);
}

/*
 *  L2 global-writeback and global-invalidate all
 */
void L2_cache_global_writeback_invalidate(void)
{
	imcr_set(IMCR_L2WBINV, 1);
	while (imcr_get(IMCR_L2WBINV))
		cpu_relax();
}

/*
 *  L2 global-writeback all
 */
void L2_cache_global_writeback(void)
{
	imcr_set(IMCR_L2WB, 1);
	while (imcr_get(IMCR_L2WB))
		cpu_relax();
}

/*
 * Cacheability controls
 */
void enable_caching(unsigned long start, unsigned long end)
{
	unsigned int mar = IMCR_MAR_BASE + ((start >> 24) << 2);
	unsigned int mar_e = IMCR_MAR_BASE + ((end >> 24) << 2);

	for (; mar <= mar_e; mar += 4)
		imcr_set(mar, imcr_get(mar) | 1);
}

void disable_caching(unsigned long start, unsigned long end)
{
	unsigned int mar = IMCR_MAR_BASE + ((start >> 24) << 2);
	unsigned int mar_e = IMCR_MAR_BASE + ((end >> 24) << 2);

	for (; mar <= mar_e; mar += 4)
		imcr_set(mar, imcr_get(mar) & ~1);
}


/*
 *  L1 block operations
 */
void L1P_cache_block_invalidate(unsigned int start, unsigned int end)
{
	cache_block_operation((unsigned int *) start,
			      (unsigned int *) end,
			      IMCR_L1PIBAR, IMCR_L1PIWC);
}
EXPORT_SYMBOL(L1P_cache_block_invalidate);

void L1D_cache_block_invalidate(unsigned int start, unsigned int end)
{
	cache_block_operation((unsigned int *) start,
			      (unsigned int *) end,
			      IMCR_L1DIBAR, IMCR_L1DIWC);
}

void L1D_cache_block_writeback_invalidate(unsigned int start, unsigned int end)
{
	cache_block_operation((unsigned int *) start,
			      (unsigned int *) end,
			      IMCR_L1DWIBAR, IMCR_L1DWIWC);
}

void L1D_cache_block_writeback(unsigned int start, unsigned int end)
{
	cache_block_operation((unsigned int *) start,
			      (unsigned int *) end,
			      IMCR_L1DWBAR, IMCR_L1DWWC);
}
EXPORT_SYMBOL(L1D_cache_block_writeback);

/*
 *  L2 block operations
 */
void L2_cache_block_invalidate(unsigned int start, unsigned int end)
{
	cache_block_operation((unsigned int *) start,
			      (unsigned int *) end,
			      IMCR_L2IBAR, IMCR_L2IWC);
}

void L2_cache_block_writeback(unsigned int start, unsigned int end)
{
	cache_block_operation((unsigned int *) start,
			      (unsigned int *) end,
			      IMCR_L2WBAR, IMCR_L2WWC);
}

void L2_cache_block_writeback_invalidate(unsigned int start, unsigned int end)
{
	cache_block_operation((unsigned int *) start,
			      (unsigned int *) end,
			      IMCR_L2WIBAR, IMCR_L2WIWC);
}

void L2_cache_block_invalidate_nowait(unsigned int start, unsigned int end)
{
	cache_block_operation_nowait((unsigned int *) start,
				     (unsigned int *) end,
				     IMCR_L2IBAR, IMCR_L2IWC);
}

void L2_cache_block_writeback_nowait(unsigned int start, unsigned int end)
{
	cache_block_operation_nowait((unsigned int *) start,
				     (unsigned int *) end,
				     IMCR_L2WBAR, IMCR_L2WWC);
}

void L2_cache_block_writeback_invalidate_nowait(unsigned int start,
						unsigned int end)
{
	cache_block_operation_nowait((unsigned int *) start,
				     (unsigned int *) end,
				     IMCR_L2WIBAR, IMCR_L2WIWC);
}


/*
 * L1 and L2 caches configuration
 */
void __init c6x_cache_init(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "ti,c64x+cache");
	if (!node)
		return;

	cache_base = of_iomap(node, 0);

	of_node_put(node);

	if (!cache_base)
		return;

	/* Set L2 caches on the the whole L2 SRAM memory */
	L2_cache_set_mode(L2MODE_SIZE);

	/* Enable L1 */
	L1_cache_on();
}
