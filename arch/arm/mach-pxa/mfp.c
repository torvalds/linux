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
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/arch/mfp.h>
#include <asm/arch/mfp-pxa3xx.h>
#include <asm/arch/pxa3xx-regs.h>

/* mfp_spin_lock is used to ensure that MFP register configuration
 * (most likely a read-modify-write operation) is atomic, and that
 * mfp_table[] is consistent
 */
static DEFINE_SPINLOCK(mfp_spin_lock);

static void __iomem *mfpr_mmio_base = (void __iomem *)&__REG(MFPR_BASE);

struct pxa3xx_mfp_pin {
	unsigned long	config;		/* -1 for not configured */
	unsigned long	mfpr_off;	/* MFPRxx Register offset */
	unsigned long	mfpr_run;	/* Run-Mode Register Value */
	unsigned long	mfpr_lpm;	/* Low Power Mode Register Value */
};

static struct pxa3xx_mfp_pin mfp_table[MFP_PIN_MAX];

/* mapping of MFP_LPM_* definitions to MFPR_LPM_* register bits */
const static unsigned long mfpr_lpm[] = {
	MFPR_LPM_INPUT,
	MFPR_LPM_DRIVE_LOW,
	MFPR_LPM_DRIVE_HIGH,
	MFPR_LPM_PULL_LOW,
	MFPR_LPM_PULL_HIGH,
	MFPR_LPM_FLOAT,
};

/* mapping of MFP_PULL_* definitions to MFPR_PULL_* register bits */
const static unsigned long mfpr_pull[] = {
	MFPR_PULL_NONE,
	MFPR_PULL_LOW,
	MFPR_PULL_HIGH,
	MFPR_PULL_BOTH,
};

/* mapping of MFP_LPM_EDGE_* definitions to MFPR_EDGE_* register bits */
const static unsigned long mfpr_edge[] = {
	MFPR_EDGE_NONE,
	MFPR_EDGE_RISE,
	MFPR_EDGE_FALL,
	MFPR_EDGE_BOTH,
};

#define mfpr_readl(off)			\
	__raw_readl(mfpr_mmio_base + (off))

#define mfpr_writel(off, val)		\
	__raw_writel(val, mfpr_mmio_base + (off))

#define mfp_configured(p)	((p)->config != -1)

/*
 * perform a read-back of any MFPR register to make sure the
 * previous writings are finished
 */
#define mfpr_sync()	(void)__raw_readl(mfpr_mmio_base + 0)

static inline void __mfp_config_run(struct pxa3xx_mfp_pin *p)
{
	if (mfp_configured(p))
		mfpr_writel(p->mfpr_off, p->mfpr_run);
}

static inline void __mfp_config_lpm(struct pxa3xx_mfp_pin *p)
{
	if (mfp_configured(p)) {
		unsigned long mfpr_clr = (p->mfpr_run & ~MFPR_EDGE_BOTH) | MFPR_EDGE_CLEAR;
		if (mfpr_clr != p->mfpr_run)
			mfpr_writel(p->mfpr_off, mfpr_clr);
		if (p->mfpr_lpm != mfpr_clr)
			mfpr_writel(p->mfpr_off, p->mfpr_lpm);
	}
}

void pxa3xx_mfp_config(unsigned long *mfp_cfgs, int num)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&mfp_spin_lock, flags);

	for (i = 0; i < num; i++, mfp_cfgs++) {
		unsigned long tmp, c = *mfp_cfgs;
		struct pxa3xx_mfp_pin *p;
		int pin, af, drv, lpm, edge, pull;

		pin = MFP_PIN(c);
		BUG_ON(pin >= MFP_PIN_MAX);
		p = &mfp_table[pin];

		af  = MFP_AF(c);
		drv = MFP_DS(c);
		lpm = MFP_LPM_STATE(c);
		edge = MFP_LPM_EDGE(c);
		pull = MFP_PULL(c);

		/* run-mode pull settings will conflict with MFPR bits of
		 * low power mode state,  calculate mfpr_run and mfpr_lpm
		 * individually if pull != MFP_PULL_NONE
		 */
		tmp = MFPR_AF_SEL(af) | MFPR_DRIVE(drv);

		if (likely(pull == MFP_PULL_NONE)) {
			p->mfpr_run = tmp | mfpr_lpm[lpm] | mfpr_edge[edge];
			p->mfpr_lpm = p->mfpr_run;
		} else {
			p->mfpr_lpm = tmp | mfpr_lpm[lpm] | mfpr_edge[edge];
			p->mfpr_run = tmp | mfpr_pull[pull];
		}

		p->config = c; __mfp_config_run(p);
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
			mfp_table[i].mfpr_run = 0;
			mfp_table[i].mfpr_lpm = 0;
			offset += 4; i++;
		} while ((i <= p->end) && (p->end != -1));
	}

	spin_unlock_irqrestore(&mfp_spin_lock, flags);
}

void __init pxa3xx_init_mfp(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mfp_table); i++)
		mfp_table[i].config = -1;
}

#ifdef CONFIG_PM
/*
 * Configure the MFPs appropriately for suspend/resume.
 * FIXME: this should probably depend on which system state we're
 * entering - for instance, we might not want to place MFP pins in
 * a pull-down mode if they're an active low chip select, and we're
 * just entering standby.
 */
static int pxa3xx_mfp_suspend(struct sys_device *d, pm_message_t state)
{
	int pin;

	for (pin = 0; pin < ARRAY_SIZE(mfp_table); pin++) {
		struct pxa3xx_mfp_pin *p = &mfp_table[pin];
		__mfp_config_lpm(p);
	}
	return 0;
}

static int pxa3xx_mfp_resume(struct sys_device *d)
{
	int pin;

	for (pin = 0; pin < ARRAY_SIZE(mfp_table); pin++) {
		struct pxa3xx_mfp_pin *p = &mfp_table[pin];
		__mfp_config_run(p);
	}

	/* clear RDH bit when MFP settings are restored
	 *
	 * NOTE: the last 3 bits DxS are write-1-to-clear so carefully
	 * preserve them here in case they will be referenced later
	 */
	ASCR &= ~(ASCR_RDH | ASCR_D1S | ASCR_D2S | ASCR_D3S);

	return 0;
}

static struct sysdev_class mfp_sysclass = {
	.name		= "mfp",
	.suspend	= pxa3xx_mfp_suspend,
	.resume 	= pxa3xx_mfp_resume,
};

static struct sys_device mfp_device = {
	.id		= 0,
	.cls		= &mfp_sysclass,
};

static int __init mfp_init_devicefs(void)
{
	sysdev_class_register(&mfp_sysclass);
	return sysdev_register(&mfp_device);
}
device_initcall(mfp_init_devicefs);
#endif
