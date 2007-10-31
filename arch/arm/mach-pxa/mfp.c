/*
 * linux/arch/arm/mach-pxa/mfp.c
 *
 * PXA3xx Multi-Function Pin Support
 *
 * Copyright (C) 2007 Marvell Internation Ltd.
 *
 * 2007-08-21: eric miao <eric.miao@marvell.com>
 *             initial version
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/hardware.h>
#include <asm/arch/mfp.h>

/* mfp_spin_lock is used to ensure that MFP register configuration
 * (most likely a read-modify-write operation) is atomic, and that
 * mfp_table[] is consistent
 */
static DEFINE_SPINLOCK(mfp_spin_lock);

static void __iomem *mfpr_mmio_base = (void __iomem *)&__REG(MFPR_BASE);
static struct pxa3xx_mfp_pin mfp_table[MFP_PIN_MAX];

#define mfpr_readl(off)			\
	__raw_readl(mfpr_mmio_base + (off))

#define mfpr_writel(off, val)		\
	__raw_writel(val, mfpr_mmio_base + (off))

/*
 * perform a read-back of any MFPR register to make sure the
 * previous writings are finished
 */
#define mfpr_sync()	(void)__raw_readl(mfpr_mmio_base + 0)

static inline void __mfp_config(int pin, unsigned long val)
{
	unsigned long off = mfp_table[pin].mfpr_off;

	mfp_table[pin].mfpr_val = val;
	mfpr_writel(off, val);
}

void pxa3xx_mfp_config(mfp_cfg_t *mfp_cfgs, int num)
{
	int i, pin;
	unsigned long val, flags;
	mfp_cfg_t *mfp_cfg = mfp_cfgs;

	spin_lock_irqsave(&mfp_spin_lock, flags);

	for (i = 0; i < num; i++, mfp_cfg++) {
		pin = MFP_CFG_PIN(*mfp_cfg);
		val = MFP_CFG_VAL(*mfp_cfg);

		BUG_ON(pin >= MFP_PIN_MAX);

		__mfp_config(pin, val);
	}

	mfpr_sync();
	spin_unlock_irqrestore(&mfp_spin_lock, flags);
}

unsigned long pxa3xx_mfp_read(int mfp)
{
	unsigned long val, flags;

	BUG_ON(mfp >= MFP_PIN_MAX);

	spin_lock_irqsave(&mfp_spin_lock, flags);
	val = mfpr_readl(mfp_table[mfp].mfpr_off);
	spin_unlock_irqrestore(&mfp_spin_lock, flags);

	return val;
}

void pxa3xx_mfp_write(int mfp, unsigned long val)
{
	unsigned long flags;

	BUG_ON(mfp >= MFP_PIN_MAX);

	spin_lock_irqsave(&mfp_spin_lock, flags);
	mfpr_writel(mfp_table[mfp].mfpr_off, val);
	mfpr_sync();
	spin_unlock_irqrestore(&mfp_spin_lock, flags);
}

void pxa3xx_mfp_set_afds(int mfp, int af, int ds)
{
	uint32_t mfpr_off, mfpr_val;
	unsigned long flags;

	BUG_ON(mfp >= MFP_PIN_MAX);

	spin_lock_irqsave(&mfp_spin_lock, flags);
	mfpr_off = mfp_table[mfp].mfpr_off;

	mfpr_val = mfpr_readl(mfpr_off);
	mfpr_val &= ~(MFPR_AF_MASK | MFPR_DRV_MASK);
	mfpr_val |= (((af & 0x7) << MFPR_ALT_OFFSET) |
		     ((ds & 0x7) << MFPR_DRV_OFFSET));

	mfpr_writel(mfpr_off, mfpr_val);
	mfpr_sync();

	spin_unlock_irqrestore(&mfp_spin_lock, flags);
}

void pxa3xx_mfp_set_rdh(int mfp, int rdh)
{
	uint32_t mfpr_off, mfpr_val;
	unsigned long flags;

	BUG_ON(mfp >= MFP_PIN_MAX);

	spin_lock_irqsave(&mfp_spin_lock, flags);

	mfpr_off = mfp_table[mfp].mfpr_off;

	mfpr_val = mfpr_readl(mfpr_off);
	mfpr_val &= ~MFPR_RDH_MASK;

	if (likely(rdh))
		mfpr_val |= (1u << MFPR_SS_OFFSET);

	mfpr_writel(mfpr_off, mfpr_val);
	mfpr_sync();

	spin_unlock_irqrestore(&mfp_spin_lock, flags);
}

void pxa3xx_mfp_set_lpm(int mfp, int lpm)
{
	uint32_t mfpr_off, mfpr_val;
	unsigned long flags;

	BUG_ON(mfp >= MFP_PIN_MAX);

	spin_lock_irqsave(&mfp_spin_lock, flags);

	mfpr_off = mfp_table[mfp].mfpr_off;
	mfpr_val = mfpr_readl(mfpr_off);
	mfpr_val &= ~MFPR_LPM_MASK;

	if (lpm & 0x1) mfpr_val |= 1u << MFPR_SON_OFFSET;
	if (lpm & 0x2) mfpr_val |= 1u << MFPR_SD_OFFSET;
	if (lpm & 0x4) mfpr_val |= 1u << MFPR_PU_OFFSET;
	if (lpm & 0x8) mfpr_val |= 1u << MFPR_PD_OFFSET;
	if (lpm &0x10) mfpr_val |= 1u << MFPR_PS_OFFSET;

	mfpr_writel(mfpr_off, mfpr_val);
	mfpr_sync();

	spin_unlock_irqrestore(&mfp_spin_lock, flags);
}

void pxa3xx_mfp_set_pull(int mfp, int pull)
{
	uint32_t mfpr_off, mfpr_val;
	unsigned long flags;

	BUG_ON(mfp >= MFP_PIN_MAX);

	spin_lock_irqsave(&mfp_spin_lock, flags);

	mfpr_off = mfp_table[mfp].mfpr_off;
	mfpr_val = mfpr_readl(mfpr_off);
	mfpr_val &= ~MFPR_PULL_MASK;
	mfpr_val |= ((pull & 0x7u) << MFPR_PD_OFFSET);

	mfpr_writel(mfpr_off, mfpr_val);
	mfpr_sync();

	spin_unlock_irqrestore(&mfp_spin_lock, flags);
}

void pxa3xx_mfp_set_edge(int mfp, int edge)
{
	uint32_t mfpr_off, mfpr_val;
	unsigned long flags;

	BUG_ON(mfp >= MFP_PIN_MAX);

	spin_lock_irqsave(&mfp_spin_lock, flags);

	mfpr_off = mfp_table[mfp].mfpr_off;
	mfpr_val = mfpr_readl(mfpr_off);

	mfpr_val &= ~MFPR_EDGE_MASK;
	mfpr_val |= (edge & 0x3u) << MFPR_ERE_OFFSET;
	mfpr_val |= (!edge & 0x1) << MFPR_EC_OFFSET;

	mfpr_writel(mfpr_off, mfpr_val);
	mfpr_sync();

	spin_unlock_irqrestore(&mfp_spin_lock, flags);
}

void __init pxa3xx_mfp_init_addr(struct pxa3xx_mfp_addr_map *map)
{
	struct pxa3xx_mfp_addr_map *p;
	unsigned long offset, flags;
	int i;

	spin_lock_irqsave(&mfp_spin_lock, flags);

	for (p = map; p->start != MFP_PIN_INVALID; p++) {
		offset = p->offset;
		i = p->start;

		do {
			mfp_table[i].mfpr_off = offset;
			mfp_table[i].mfpr_val = 0;
			offset += 4; i++;
		} while ((i <= p->end) && (p->end != -1));
	}

	spin_unlock_irqrestore(&mfp_spin_lock, flags);
}

void __init pxa3xx_init_mfp(void)
{
	memset(mfp_table, 0, sizeof(mfp_table));
}
