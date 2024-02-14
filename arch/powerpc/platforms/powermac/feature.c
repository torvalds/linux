// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 1996-2001 Paul Mackerras (paulus@cs.anu.edu.au)
 *                          Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  TODO:
 *
 *   - Replace mdelay with some schedule loop if possible
 *   - Shorten some obfuscated delays on some routines (like modem
 *     power)
 *   - Refcount some clocks (see darwin)
 *   - Split split split...
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/spinlock.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/ioport.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <asm/sections.h>
#include <asm/errno.h>
#include <asm/ohare.h>
#include <asm/heathrow.h>
#include <asm/keylargo.h>
#include <asm/uninorth.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/dbdma.h>
#include <asm/pci-bridge.h>
#include <asm/pmac_low_i2c.h>

#include "pmac.h"

#undef DEBUG_FEATURE

#ifdef DEBUG_FEATURE
#define DBG(fmt...) printk(KERN_DEBUG fmt)
#else
#define DBG(fmt...)
#endif

#ifdef CONFIG_PPC_BOOK3S_32
extern int powersave_lowspeed;
#endif

extern int powersave_nap;
extern struct device_node *k2_skiplist[2];

/*
 * We use a single global lock to protect accesses. Each driver has
 * to take care of its own locking
 */
DEFINE_RAW_SPINLOCK(feature_lock);

#define LOCK(flags)	raw_spin_lock_irqsave(&feature_lock, flags);
#define UNLOCK(flags)	raw_spin_unlock_irqrestore(&feature_lock, flags);


/*
 * Instance of some macio stuffs
 */
struct macio_chip macio_chips[MAX_MACIO_CHIPS];

struct macio_chip *macio_find(struct device_node *child, int type)
{
	while(child) {
		int	i;

		for (i=0; i < MAX_MACIO_CHIPS && macio_chips[i].of_node; i++)
			if (child == macio_chips[i].of_node &&
			    (!type || macio_chips[i].type == type))
				return &macio_chips[i];
		child = child->parent;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(macio_find);

static const char *macio_names[] =
{
	"Unknown",
	"Grand Central",
	"OHare",
	"OHareII",
	"Heathrow",
	"Gatwick",
	"Paddington",
	"Keylargo",
	"Pangea",
	"Intrepid",
	"K2",
	"Shasta",
};


struct device_node *uninorth_node;
u32 __iomem *uninorth_base;

static u32 uninorth_rev;
static int uninorth_maj;
static void __iomem *u3_ht_base;

/*
 * For each motherboard family, we have a table of functions pointers
 * that handle the various features.
 */

typedef long (*feature_call)(struct device_node *node, long param, long value);

struct feature_table_entry {
	unsigned int	selector;
	feature_call	function;
};

struct pmac_mb_def
{
	const char*			model_string;
	const char*			model_name;
	int				model_id;
	struct feature_table_entry*	features;
	unsigned long			board_flags;
};
static struct pmac_mb_def pmac_mb;

/*
 * Here are the chip specific feature functions
 */

#ifndef CONFIG_PPC64

static int simple_feature_tweak(struct device_node *node, int type, int reg,
				u32 mask, int value)
{
	struct macio_chip*	macio;
	unsigned long		flags;

	macio = macio_find(node, type);
	if (!macio)
		return -ENODEV;
	LOCK(flags);
	if (value)
		MACIO_BIS(reg, mask);
	else
		MACIO_BIC(reg, mask);
	(void)MACIO_IN32(reg);
	UNLOCK(flags);

	return 0;
}

static long ohare_htw_scc_enable(struct device_node *node, long param,
				 long value)
{
	struct macio_chip*	macio;
	unsigned long		chan_mask;
	unsigned long		fcr;
	unsigned long		flags;
	int			htw, trans;
	unsigned long		rmask;

	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	if (of_node_name_eq(node, "ch-a"))
		chan_mask = MACIO_FLAG_SCCA_ON;
	else if (of_node_name_eq(node, "ch-b"))
		chan_mask = MACIO_FLAG_SCCB_ON;
	else
		return -ENODEV;

	htw = (macio->type == macio_heathrow || macio->type == macio_paddington
		|| macio->type == macio_gatwick);
	/* On these machines, the HRW_SCC_TRANS_EN_N bit mustn't be touched */
	trans = (pmac_mb.model_id != PMAC_TYPE_YOSEMITE &&
		 pmac_mb.model_id != PMAC_TYPE_YIKES);
	if (value) {
#ifdef CONFIG_ADB_PMU
		if ((param & 0xfff) == PMAC_SCC_IRDA)
			pmu_enable_irled(1);
#endif /* CONFIG_ADB_PMU */
		LOCK(flags);
		fcr = MACIO_IN32(OHARE_FCR);
		/* Check if scc cell need enabling */
		if (!(fcr & OH_SCC_ENABLE)) {
			fcr |= OH_SCC_ENABLE;
			if (htw) {
				/* Side effect: this will also power up the
				 * modem, but it's too messy to figure out on which
				 * ports this controls the transceiver and on which
				 * it controls the modem
				 */
				if (trans)
					fcr &= ~HRW_SCC_TRANS_EN_N;
				MACIO_OUT32(OHARE_FCR, fcr);
				fcr |= (rmask = HRW_RESET_SCC);
				MACIO_OUT32(OHARE_FCR, fcr);
			} else {
				fcr |= (rmask = OH_SCC_RESET);
				MACIO_OUT32(OHARE_FCR, fcr);
			}
			UNLOCK(flags);
			(void)MACIO_IN32(OHARE_FCR);
			mdelay(15);
			LOCK(flags);
			fcr &= ~rmask;
			MACIO_OUT32(OHARE_FCR, fcr);
		}
		if (chan_mask & MACIO_FLAG_SCCA_ON)
			fcr |= OH_SCCA_IO;
		if (chan_mask & MACIO_FLAG_SCCB_ON)
			fcr |= OH_SCCB_IO;
		MACIO_OUT32(OHARE_FCR, fcr);
		macio->flags |= chan_mask;
		UNLOCK(flags);
		if (param & PMAC_SCC_FLAG_XMON)
			macio->flags |= MACIO_FLAG_SCC_LOCKED;
	} else {
		if (macio->flags & MACIO_FLAG_SCC_LOCKED)
			return -EPERM;
		LOCK(flags);
		fcr = MACIO_IN32(OHARE_FCR);
		if (chan_mask & MACIO_FLAG_SCCA_ON)
			fcr &= ~OH_SCCA_IO;
		if (chan_mask & MACIO_FLAG_SCCB_ON)
			fcr &= ~OH_SCCB_IO;
		MACIO_OUT32(OHARE_FCR, fcr);
		if ((fcr & (OH_SCCA_IO | OH_SCCB_IO)) == 0) {
			fcr &= ~OH_SCC_ENABLE;
			if (htw && trans)
				fcr |= HRW_SCC_TRANS_EN_N;
			MACIO_OUT32(OHARE_FCR, fcr);
		}
		macio->flags &= ~(chan_mask);
		UNLOCK(flags);
		mdelay(10);
#ifdef CONFIG_ADB_PMU
		if ((param & 0xfff) == PMAC_SCC_IRDA)
			pmu_enable_irled(0);
#endif /* CONFIG_ADB_PMU */
	}
	return 0;
}

static long ohare_floppy_enable(struct device_node *node, long param,
				long value)
{
	return simple_feature_tweak(node, macio_ohare,
		OHARE_FCR, OH_FLOPPY_ENABLE, value);
}

static long ohare_mesh_enable(struct device_node *node, long param, long value)
{
	return simple_feature_tweak(node, macio_ohare,
		OHARE_FCR, OH_MESH_ENABLE, value);
}

static long ohare_ide_enable(struct device_node *node, long param, long value)
{
	switch(param) {
	case 0:
		/* For some reason, setting the bit in set_initial_features()
		 * doesn't stick. I'm still investigating... --BenH.
		 */
		if (value)
			simple_feature_tweak(node, macio_ohare,
				OHARE_FCR, OH_IOBUS_ENABLE, 1);
		return simple_feature_tweak(node, macio_ohare,
			OHARE_FCR, OH_IDE0_ENABLE, value);
	case 1:
		return simple_feature_tweak(node, macio_ohare,
			OHARE_FCR, OH_BAY_IDE_ENABLE, value);
	default:
		return -ENODEV;
	}
}

static long ohare_ide_reset(struct device_node *node, long param, long value)
{
	switch(param) {
	case 0:
		return simple_feature_tweak(node, macio_ohare,
			OHARE_FCR, OH_IDE0_RESET_N, !value);
	case 1:
		return simple_feature_tweak(node, macio_ohare,
			OHARE_FCR, OH_IDE1_RESET_N, !value);
	default:
		return -ENODEV;
	}
}

static long ohare_sleep_state(struct device_node *node, long param, long value)
{
	struct macio_chip*	macio = &macio_chips[0];

	if ((pmac_mb.board_flags & PMAC_MB_CAN_SLEEP) == 0)
		return -EPERM;
	if (value == 1) {
		MACIO_BIC(OHARE_FCR, OH_IOBUS_ENABLE);
	} else if (value == 0) {
		MACIO_BIS(OHARE_FCR, OH_IOBUS_ENABLE);
	}

	return 0;
}

static long heathrow_modem_enable(struct device_node *node, long param,
				  long value)
{
	struct macio_chip*	macio;
	u8			gpio;
	unsigned long		flags;

	macio = macio_find(node, macio_unknown);
	if (!macio)
		return -ENODEV;
	gpio = MACIO_IN8(HRW_GPIO_MODEM_RESET) & ~1;
	if (!value) {
		LOCK(flags);
		MACIO_OUT8(HRW_GPIO_MODEM_RESET, gpio);
		UNLOCK(flags);
		(void)MACIO_IN8(HRW_GPIO_MODEM_RESET);
		mdelay(250);
	}
	if (pmac_mb.model_id != PMAC_TYPE_YOSEMITE &&
	    pmac_mb.model_id != PMAC_TYPE_YIKES) {
		LOCK(flags);
		if (value)
			MACIO_BIC(HEATHROW_FCR, HRW_SCC_TRANS_EN_N);
		else
			MACIO_BIS(HEATHROW_FCR, HRW_SCC_TRANS_EN_N);
		UNLOCK(flags);
		(void)MACIO_IN32(HEATHROW_FCR);
		mdelay(250);
	}
	if (value) {
		LOCK(flags);
		MACIO_OUT8(HRW_GPIO_MODEM_RESET, gpio | 1);
		(void)MACIO_IN8(HRW_GPIO_MODEM_RESET);
		UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(HRW_GPIO_MODEM_RESET, gpio);
		(void)MACIO_IN8(HRW_GPIO_MODEM_RESET);
		UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(HRW_GPIO_MODEM_RESET, gpio | 1);
		(void)MACIO_IN8(HRW_GPIO_MODEM_RESET);
		UNLOCK(flags); mdelay(250);
	}
	return 0;
}

static long heathrow_floppy_enable(struct device_node *node, long param,
				   long value)
{
	return simple_feature_tweak(node, macio_unknown,
		HEATHROW_FCR,
		HRW_SWIM_ENABLE|HRW_BAY_FLOPPY_ENABLE,
		value);
}

static long heathrow_mesh_enable(struct device_node *node, long param,
				 long value)
{
	struct macio_chip*	macio;
	unsigned long		flags;

	macio = macio_find(node, macio_unknown);
	if (!macio)
		return -ENODEV;
	LOCK(flags);
	/* Set clear mesh cell enable */
	if (value)
		MACIO_BIS(HEATHROW_FCR, HRW_MESH_ENABLE);
	else
		MACIO_BIC(HEATHROW_FCR, HRW_MESH_ENABLE);
	(void)MACIO_IN32(HEATHROW_FCR);
	udelay(10);
	/* Set/Clear termination power */
	if (value)
		MACIO_BIC(HEATHROW_MBCR, 0x04000000);
	else
		MACIO_BIS(HEATHROW_MBCR, 0x04000000);
	(void)MACIO_IN32(HEATHROW_MBCR);
	udelay(10);
	UNLOCK(flags);

	return 0;
}

static long heathrow_ide_enable(struct device_node *node, long param,
				long value)
{
	switch(param) {
	case 0:
		return simple_feature_tweak(node, macio_unknown,
			HEATHROW_FCR, HRW_IDE0_ENABLE, value);
	case 1:
		return simple_feature_tweak(node, macio_unknown,
			HEATHROW_FCR, HRW_BAY_IDE_ENABLE, value);
	default:
		return -ENODEV;
	}
}

static long heathrow_ide_reset(struct device_node *node, long param,
			       long value)
{
	switch(param) {
	case 0:
		return simple_feature_tweak(node, macio_unknown,
			HEATHROW_FCR, HRW_IDE0_RESET_N, !value);
	case 1:
		return simple_feature_tweak(node, macio_unknown,
			HEATHROW_FCR, HRW_IDE1_RESET_N, !value);
	default:
		return -ENODEV;
	}
}

static long heathrow_bmac_enable(struct device_node *node, long param,
				 long value)
{
	struct macio_chip*	macio;
	unsigned long		flags;

	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	if (value) {
		LOCK(flags);
		MACIO_BIS(HEATHROW_FCR, HRW_BMAC_IO_ENABLE);
		MACIO_BIS(HEATHROW_FCR, HRW_BMAC_RESET);
		UNLOCK(flags);
		(void)MACIO_IN32(HEATHROW_FCR);
		mdelay(10);
		LOCK(flags);
		MACIO_BIC(HEATHROW_FCR, HRW_BMAC_RESET);
		UNLOCK(flags);
		(void)MACIO_IN32(HEATHROW_FCR);
		mdelay(10);
	} else {
		LOCK(flags);
		MACIO_BIC(HEATHROW_FCR, HRW_BMAC_IO_ENABLE);
		UNLOCK(flags);
	}
	return 0;
}

static long heathrow_sound_enable(struct device_node *node, long param,
				  long value)
{
	struct macio_chip*	macio;
	unsigned long		flags;

	/* B&W G3 and Yikes don't support that properly (the
	 * sound appear to never come back after being shut down).
	 */
	if (pmac_mb.model_id == PMAC_TYPE_YOSEMITE ||
	    pmac_mb.model_id == PMAC_TYPE_YIKES)
		return 0;

	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	if (value) {
		LOCK(flags);
		MACIO_BIS(HEATHROW_FCR, HRW_SOUND_CLK_ENABLE);
		MACIO_BIC(HEATHROW_FCR, HRW_SOUND_POWER_N);
		UNLOCK(flags);
		(void)MACIO_IN32(HEATHROW_FCR);
	} else {
		LOCK(flags);
		MACIO_BIS(HEATHROW_FCR, HRW_SOUND_POWER_N);
		MACIO_BIC(HEATHROW_FCR, HRW_SOUND_CLK_ENABLE);
		UNLOCK(flags);
	}
	return 0;
}

static u32 save_fcr[6];
static u32 save_mbcr;
static struct dbdma_regs save_dbdma[13];
static struct dbdma_regs save_alt_dbdma[13];

static void dbdma_save(struct macio_chip *macio, struct dbdma_regs *save)
{
	int i;

	/* Save state & config of DBDMA channels */
	for (i = 0; i < 13; i++) {
		volatile struct dbdma_regs __iomem * chan = (void __iomem *)
			(macio->base + ((0x8000+i*0x100)>>2));
		save[i].cmdptr_hi = in_le32(&chan->cmdptr_hi);
		save[i].cmdptr = in_le32(&chan->cmdptr);
		save[i].intr_sel = in_le32(&chan->intr_sel);
		save[i].br_sel = in_le32(&chan->br_sel);
		save[i].wait_sel = in_le32(&chan->wait_sel);
	}
}

static void dbdma_restore(struct macio_chip *macio, struct dbdma_regs *save)
{
	int i;

	/* Save state & config of DBDMA channels */
	for (i = 0; i < 13; i++) {
		volatile struct dbdma_regs __iomem * chan = (void __iomem *)
			(macio->base + ((0x8000+i*0x100)>>2));
		out_le32(&chan->control, (ACTIVE|DEAD|WAKE|FLUSH|PAUSE|RUN)<<16);
		while (in_le32(&chan->status) & ACTIVE)
			mb();
		out_le32(&chan->cmdptr_hi, save[i].cmdptr_hi);
		out_le32(&chan->cmdptr, save[i].cmdptr);
		out_le32(&chan->intr_sel, save[i].intr_sel);
		out_le32(&chan->br_sel, save[i].br_sel);
		out_le32(&chan->wait_sel, save[i].wait_sel);
	}
}

static void heathrow_sleep(struct macio_chip *macio, int secondary)
{
	if (secondary) {
		dbdma_save(macio, save_alt_dbdma);
		save_fcr[2] = MACIO_IN32(0x38);
		save_fcr[3] = MACIO_IN32(0x3c);
	} else {
		dbdma_save(macio, save_dbdma);
		save_fcr[0] = MACIO_IN32(0x38);
		save_fcr[1] = MACIO_IN32(0x3c);
		save_mbcr = MACIO_IN32(0x34);
		/* Make sure sound is shut down */
		MACIO_BIS(HEATHROW_FCR, HRW_SOUND_POWER_N);
		MACIO_BIC(HEATHROW_FCR, HRW_SOUND_CLK_ENABLE);
		/* This seems to be necessary as well or the fan
		 * keeps coming up and battery drains fast */
		MACIO_BIC(HEATHROW_FCR, HRW_IOBUS_ENABLE);
		MACIO_BIC(HEATHROW_FCR, HRW_IDE0_RESET_N);
		/* Make sure eth is down even if module or sleep
		 * won't work properly */
		MACIO_BIC(HEATHROW_FCR, HRW_BMAC_IO_ENABLE | HRW_BMAC_RESET);
	}
	/* Make sure modem is shut down */
	MACIO_OUT8(HRW_GPIO_MODEM_RESET,
		MACIO_IN8(HRW_GPIO_MODEM_RESET) & ~1);
	MACIO_BIS(HEATHROW_FCR, HRW_SCC_TRANS_EN_N);
	MACIO_BIC(HEATHROW_FCR, OH_SCCA_IO|OH_SCCB_IO|HRW_SCC_ENABLE);

	/* Let things settle */
	(void)MACIO_IN32(HEATHROW_FCR);
}

static void heathrow_wakeup(struct macio_chip *macio, int secondary)
{
	if (secondary) {
		MACIO_OUT32(0x38, save_fcr[2]);
		(void)MACIO_IN32(0x38);
		mdelay(1);
		MACIO_OUT32(0x3c, save_fcr[3]);
		(void)MACIO_IN32(0x38);
		mdelay(10);
		dbdma_restore(macio, save_alt_dbdma);
	} else {
		MACIO_OUT32(0x38, save_fcr[0] | HRW_IOBUS_ENABLE);
		(void)MACIO_IN32(0x38);
		mdelay(1);
		MACIO_OUT32(0x3c, save_fcr[1]);
		(void)MACIO_IN32(0x38);
		mdelay(1);
		MACIO_OUT32(0x34, save_mbcr);
		(void)MACIO_IN32(0x38);
		mdelay(10);
		dbdma_restore(macio, save_dbdma);
	}
}

static long heathrow_sleep_state(struct device_node *node, long param,
				 long value)
{
	if ((pmac_mb.board_flags & PMAC_MB_CAN_SLEEP) == 0)
		return -EPERM;
	if (value == 1) {
		if (macio_chips[1].type == macio_gatwick)
			heathrow_sleep(&macio_chips[0], 1);
		heathrow_sleep(&macio_chips[0], 0);
	} else if (value == 0) {
		heathrow_wakeup(&macio_chips[0], 0);
		if (macio_chips[1].type == macio_gatwick)
			heathrow_wakeup(&macio_chips[0], 1);
	}
	return 0;
}

static long core99_scc_enable(struct device_node *node, long param, long value)
{
	struct macio_chip*	macio;
	unsigned long		flags;
	unsigned long		chan_mask;
	u32			fcr;

	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	if (of_node_name_eq(node, "ch-a"))
		chan_mask = MACIO_FLAG_SCCA_ON;
	else if (of_node_name_eq(node, "ch-b"))
		chan_mask = MACIO_FLAG_SCCB_ON;
	else
		return -ENODEV;

	if (value) {
		int need_reset_scc = 0;
		int need_reset_irda = 0;

		LOCK(flags);
		fcr = MACIO_IN32(KEYLARGO_FCR0);
		/* Check if scc cell need enabling */
		if (!(fcr & KL0_SCC_CELL_ENABLE)) {
			fcr |= KL0_SCC_CELL_ENABLE;
			need_reset_scc = 1;
		}
		if (chan_mask & MACIO_FLAG_SCCA_ON) {
			fcr |= KL0_SCCA_ENABLE;
			/* Don't enable line drivers for I2S modem */
			if ((param & 0xfff) == PMAC_SCC_I2S1)
				fcr &= ~KL0_SCC_A_INTF_ENABLE;
			else
				fcr |= KL0_SCC_A_INTF_ENABLE;
		}
		if (chan_mask & MACIO_FLAG_SCCB_ON) {
			fcr |= KL0_SCCB_ENABLE;
			/* Perform irda specific inits */
			if ((param & 0xfff) == PMAC_SCC_IRDA) {
				fcr &= ~KL0_SCC_B_INTF_ENABLE;
				fcr |= KL0_IRDA_ENABLE;
				fcr |= KL0_IRDA_CLK32_ENABLE | KL0_IRDA_CLK19_ENABLE;
				fcr |= KL0_IRDA_SOURCE1_SEL;
				fcr &= ~(KL0_IRDA_FAST_CONNECT|KL0_IRDA_DEFAULT1|KL0_IRDA_DEFAULT0);
				fcr &= ~(KL0_IRDA_SOURCE2_SEL|KL0_IRDA_HIGH_BAND);
				need_reset_irda = 1;
			} else
				fcr |= KL0_SCC_B_INTF_ENABLE;
		}
		MACIO_OUT32(KEYLARGO_FCR0, fcr);
		macio->flags |= chan_mask;
		if (need_reset_scc)  {
			MACIO_BIS(KEYLARGO_FCR0, KL0_SCC_RESET);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			UNLOCK(flags);
			mdelay(15);
			LOCK(flags);
			MACIO_BIC(KEYLARGO_FCR0, KL0_SCC_RESET);
		}
		if (need_reset_irda)  {
			MACIO_BIS(KEYLARGO_FCR0, KL0_IRDA_RESET);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			UNLOCK(flags);
			mdelay(15);
			LOCK(flags);
			MACIO_BIC(KEYLARGO_FCR0, KL0_IRDA_RESET);
		}
		UNLOCK(flags);
		if (param & PMAC_SCC_FLAG_XMON)
			macio->flags |= MACIO_FLAG_SCC_LOCKED;
	} else {
		if (macio->flags & MACIO_FLAG_SCC_LOCKED)
			return -EPERM;
		LOCK(flags);
		fcr = MACIO_IN32(KEYLARGO_FCR0);
		if (chan_mask & MACIO_FLAG_SCCA_ON)
			fcr &= ~KL0_SCCA_ENABLE;
		if (chan_mask & MACIO_FLAG_SCCB_ON) {
			fcr &= ~KL0_SCCB_ENABLE;
			/* Perform irda specific clears */
			if ((param & 0xfff) == PMAC_SCC_IRDA) {
				fcr &= ~KL0_IRDA_ENABLE;
				fcr &= ~(KL0_IRDA_CLK32_ENABLE | KL0_IRDA_CLK19_ENABLE);
				fcr &= ~(KL0_IRDA_FAST_CONNECT|KL0_IRDA_DEFAULT1|KL0_IRDA_DEFAULT0);
				fcr &= ~(KL0_IRDA_SOURCE1_SEL|KL0_IRDA_SOURCE2_SEL|KL0_IRDA_HIGH_BAND);
			}
		}
		MACIO_OUT32(KEYLARGO_FCR0, fcr);
		if ((fcr & (KL0_SCCA_ENABLE | KL0_SCCB_ENABLE)) == 0) {
			fcr &= ~KL0_SCC_CELL_ENABLE;
			MACIO_OUT32(KEYLARGO_FCR0, fcr);
		}
		macio->flags &= ~(chan_mask);
		UNLOCK(flags);
		mdelay(10);
	}
	return 0;
}

static long
core99_modem_enable(struct device_node *node, long param, long value)
{
	struct macio_chip*	macio;
	u8			gpio;
	unsigned long		flags;

	/* Hack for internal USB modem */
	if (node == NULL) {
		if (macio_chips[0].type != macio_keylargo)
			return -ENODEV;
		node = macio_chips[0].of_node;
	}
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	gpio = MACIO_IN8(KL_GPIO_MODEM_RESET);
	gpio |= KEYLARGO_GPIO_OUTPUT_ENABLE;
	gpio &= ~KEYLARGO_GPIO_OUTOUT_DATA;

	if (!value) {
		LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio);
		UNLOCK(flags);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		mdelay(250);
	}
	LOCK(flags);
	if (value) {
		MACIO_BIC(KEYLARGO_FCR2, KL2_ALT_DATA_OUT);
		UNLOCK(flags);
		(void)MACIO_IN32(KEYLARGO_FCR2);
		mdelay(250);
	} else {
		MACIO_BIS(KEYLARGO_FCR2, KL2_ALT_DATA_OUT);
		UNLOCK(flags);
	}
	if (value) {
		LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio | KEYLARGO_GPIO_OUTOUT_DATA);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio | KEYLARGO_GPIO_OUTOUT_DATA);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		UNLOCK(flags); mdelay(250);
	}
	return 0;
}

static long
pangea_modem_enable(struct device_node *node, long param, long value)
{
	struct macio_chip*	macio;
	u8			gpio;
	unsigned long		flags;

	/* Hack for internal USB modem */
	if (node == NULL) {
		if (macio_chips[0].type != macio_pangea &&
		    macio_chips[0].type != macio_intrepid)
			return -ENODEV;
		node = macio_chips[0].of_node;
	}
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	gpio = MACIO_IN8(KL_GPIO_MODEM_RESET);
	gpio |= KEYLARGO_GPIO_OUTPUT_ENABLE;
	gpio &= ~KEYLARGO_GPIO_OUTOUT_DATA;

	if (!value) {
		LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio);
		UNLOCK(flags);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		mdelay(250);
	}
	LOCK(flags);
	if (value) {
		MACIO_OUT8(KL_GPIO_MODEM_POWER,
			KEYLARGO_GPIO_OUTPUT_ENABLE);
		UNLOCK(flags);
		(void)MACIO_IN32(KEYLARGO_FCR2);
		mdelay(250);
	} else {
		MACIO_OUT8(KL_GPIO_MODEM_POWER,
			KEYLARGO_GPIO_OUTPUT_ENABLE | KEYLARGO_GPIO_OUTOUT_DATA);
		UNLOCK(flags);
	}
	if (value) {
		LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio | KEYLARGO_GPIO_OUTOUT_DATA);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio | KEYLARGO_GPIO_OUTOUT_DATA);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		UNLOCK(flags); mdelay(250);
	}
	return 0;
}

static long
core99_ata100_enable(struct device_node *node, long value)
{
	unsigned long flags;
	struct pci_dev *pdev = NULL;
	u8 pbus, pid;
	int rc;

	if (uninorth_rev < 0x24)
		return -ENODEV;

	LOCK(flags);
	if (value)
		UN_BIS(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_ATA100);
	else
		UN_BIC(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_ATA100);
	(void)UN_IN(UNI_N_CLOCK_CNTL);
	UNLOCK(flags);
	udelay(20);

	if (value) {
		if (pci_device_from_OF_node(node, &pbus, &pid) == 0)
			pdev = pci_get_domain_bus_and_slot(0, pbus, pid);
		if (pdev == NULL)
			return 0;
		rc = pci_enable_device(pdev);
		if (rc == 0)
			pci_set_master(pdev);
		pci_dev_put(pdev);
		if (rc)
			return rc;
	}
	return 0;
}

static long
core99_ide_enable(struct device_node *node, long param, long value)
{
	/* Bus ID 0 to 2 are KeyLargo based IDE, busID 3 is U2
	 * based ata-100
	 */
	switch(param) {
	    case 0:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_EIDE0_ENABLE, value);
	    case 1:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_EIDE1_ENABLE, value);
	    case 2:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_UIDE_ENABLE, value);
	    case 3:
		return core99_ata100_enable(node, value);
	    default:
		return -ENODEV;
	}
}

static long
core99_ide_reset(struct device_node *node, long param, long value)
{
	switch(param) {
	    case 0:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_EIDE0_RESET_N, !value);
	    case 1:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_EIDE1_RESET_N, !value);
	    case 2:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_UIDE_RESET_N, !value);
	    default:
		return -ENODEV;
	}
}

static long
core99_gmac_enable(struct device_node *node, long param, long value)
{
	unsigned long flags;

	LOCK(flags);
	if (value)
		UN_BIS(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_GMAC);
	else
		UN_BIC(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_GMAC);
	(void)UN_IN(UNI_N_CLOCK_CNTL);
	UNLOCK(flags);
	udelay(20);

	return 0;
}

static long
core99_gmac_phy_reset(struct device_node *node, long param, long value)
{
	unsigned long flags;
	struct macio_chip *macio;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea &&
	    macio->type != macio_intrepid)
		return -ENODEV;

	LOCK(flags);
	MACIO_OUT8(KL_GPIO_ETH_PHY_RESET, KEYLARGO_GPIO_OUTPUT_ENABLE);
	(void)MACIO_IN8(KL_GPIO_ETH_PHY_RESET);
	UNLOCK(flags);
	mdelay(10);
	LOCK(flags);
	MACIO_OUT8(KL_GPIO_ETH_PHY_RESET, /*KEYLARGO_GPIO_OUTPUT_ENABLE | */
		KEYLARGO_GPIO_OUTOUT_DATA);
	UNLOCK(flags);
	mdelay(10);

	return 0;
}

static long
core99_sound_chip_enable(struct device_node *node, long param, long value)
{
	struct macio_chip*	macio;
	unsigned long		flags;

	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;

	/* Do a better probe code, screamer G4 desktops &
	 * iMacs can do that too, add a recalibrate  in
	 * the driver as well
	 */
	if (pmac_mb.model_id == PMAC_TYPE_PISMO ||
	    pmac_mb.model_id == PMAC_TYPE_TITANIUM) {
		LOCK(flags);
		if (value)
			MACIO_OUT8(KL_GPIO_SOUND_POWER,
				KEYLARGO_GPIO_OUTPUT_ENABLE |
				KEYLARGO_GPIO_OUTOUT_DATA);
		else
			MACIO_OUT8(KL_GPIO_SOUND_POWER,
				KEYLARGO_GPIO_OUTPUT_ENABLE);
		(void)MACIO_IN8(KL_GPIO_SOUND_POWER);
		UNLOCK(flags);
	}
	return 0;
}

static long
core99_airport_enable(struct device_node *node, long param, long value)
{
	struct macio_chip*	macio;
	unsigned long		flags;
	int			state;

	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;

	/* Hint: we allow passing of macio itself for the sake of the
	 * sleep code
	 */
	if (node != macio->of_node &&
	    (!node->parent || node->parent != macio->of_node))
		return -ENODEV;
	state = (macio->flags & MACIO_FLAG_AIRPORT_ON) != 0;
	if (value == state)
		return 0;
	if (value) {
		/* This code is a reproduction of OF enable-cardslot
		 * and init-wireless methods, slightly hacked until
		 * I got it working.
		 */
		LOCK(flags);
		MACIO_OUT8(KEYLARGO_GPIO_0+0xf, 5);
		(void)MACIO_IN8(KEYLARGO_GPIO_0+0xf);
		UNLOCK(flags);
		mdelay(10);
		LOCK(flags);
		MACIO_OUT8(KEYLARGO_GPIO_0+0xf, 4);
		(void)MACIO_IN8(KEYLARGO_GPIO_0+0xf);
		UNLOCK(flags);

		mdelay(10);

		LOCK(flags);
		MACIO_BIC(KEYLARGO_FCR2, KL2_CARDSEL_16);
		(void)MACIO_IN32(KEYLARGO_FCR2);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_EXTINT_0+0xb, 0);
		(void)MACIO_IN8(KEYLARGO_GPIO_EXTINT_0+0xb);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_EXTINT_0+0xa, 0x28);
		(void)MACIO_IN8(KEYLARGO_GPIO_EXTINT_0+0xa);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_EXTINT_0+0xd, 0x28);
		(void)MACIO_IN8(KEYLARGO_GPIO_EXTINT_0+0xd);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_0+0xd, 0x28);
		(void)MACIO_IN8(KEYLARGO_GPIO_0+0xd);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_0+0xe, 0x28);
		(void)MACIO_IN8(KEYLARGO_GPIO_0+0xe);
		UNLOCK(flags);
		udelay(10);
		MACIO_OUT32(0x1c000, 0);
		mdelay(1);
		MACIO_OUT8(0x1a3e0, 0x41);
		(void)MACIO_IN8(0x1a3e0);
		udelay(10);
		LOCK(flags);
		MACIO_BIS(KEYLARGO_FCR2, KL2_CARDSEL_16);
		(void)MACIO_IN32(KEYLARGO_FCR2);
		UNLOCK(flags);
		mdelay(100);

		macio->flags |= MACIO_FLAG_AIRPORT_ON;
	} else {
		LOCK(flags);
		MACIO_BIC(KEYLARGO_FCR2, KL2_CARDSEL_16);
		(void)MACIO_IN32(KEYLARGO_FCR2);
		MACIO_OUT8(KL_GPIO_AIRPORT_0, 0);
		MACIO_OUT8(KL_GPIO_AIRPORT_1, 0);
		MACIO_OUT8(KL_GPIO_AIRPORT_2, 0);
		MACIO_OUT8(KL_GPIO_AIRPORT_3, 0);
		MACIO_OUT8(KL_GPIO_AIRPORT_4, 0);
		(void)MACIO_IN8(KL_GPIO_AIRPORT_4);
		UNLOCK(flags);

		macio->flags &= ~MACIO_FLAG_AIRPORT_ON;
	}
	return 0;
}

#ifdef CONFIG_SMP
static long
core99_reset_cpu(struct device_node *node, long param, long value)
{
	unsigned int reset_io = 0;
	unsigned long flags;
	struct macio_chip *macio;
	struct device_node *np;
	const int dflt_reset_lines[] = {	KL_GPIO_RESET_CPU0,
						KL_GPIO_RESET_CPU1,
						KL_GPIO_RESET_CPU2,
						KL_GPIO_RESET_CPU3 };

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo)
		return -ENODEV;

	for_each_of_cpu_node(np) {
		const u32 *rst = of_get_property(np, "soft-reset", NULL);
		if (!rst)
			continue;
		if (param == of_get_cpu_hwid(np, 0)) {
			of_node_put(np);
			reset_io = *rst;
			break;
		}
	}
	if (np == NULL || reset_io == 0)
		reset_io = dflt_reset_lines[param];

	LOCK(flags);
	MACIO_OUT8(reset_io, KEYLARGO_GPIO_OUTPUT_ENABLE);
	(void)MACIO_IN8(reset_io);
	udelay(1);
	MACIO_OUT8(reset_io, 0);
	(void)MACIO_IN8(reset_io);
	UNLOCK(flags);

	return 0;
}
#endif /* CONFIG_SMP */

static long
core99_usb_enable(struct device_node *node, long param, long value)
{
	struct macio_chip *macio;
	unsigned long flags;
	const char *prop;
	int number;
	u32 reg;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea &&
	    macio->type != macio_intrepid)
		return -ENODEV;

	prop = of_get_property(node, "AAPL,clock-id", NULL);
	if (!prop)
		return -ENODEV;
	if (strncmp(prop, "usb0u048", 8) == 0)
		number = 0;
	else if (strncmp(prop, "usb1u148", 8) == 0)
		number = 2;
	else if (strncmp(prop, "usb2u248", 8) == 0)
		number = 4;
	else
		return -ENODEV;

	/* Sorry for the brute-force locking, but this is only used during
	 * sleep and the timing seem to be critical
	 */
	LOCK(flags);
	if (value) {
		/* Turn ON */
		if (number == 0) {
			MACIO_BIC(KEYLARGO_FCR0, (KL0_USB0_PAD_SUSPEND0 | KL0_USB0_PAD_SUSPEND1));
			(void)MACIO_IN32(KEYLARGO_FCR0);
			UNLOCK(flags);
			mdelay(1);
			LOCK(flags);
			MACIO_BIS(KEYLARGO_FCR0, KL0_USB0_CELL_ENABLE);
		} else if (number == 2) {
			MACIO_BIC(KEYLARGO_FCR0, (KL0_USB1_PAD_SUSPEND0 | KL0_USB1_PAD_SUSPEND1));
			UNLOCK(flags);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			mdelay(1);
			LOCK(flags);
			MACIO_BIS(KEYLARGO_FCR0, KL0_USB1_CELL_ENABLE);
		} else if (number == 4) {
			MACIO_BIC(KEYLARGO_FCR1, (KL1_USB2_PAD_SUSPEND0 | KL1_USB2_PAD_SUSPEND1));
			UNLOCK(flags);
			(void)MACIO_IN32(KEYLARGO_FCR1);
			mdelay(1);
			LOCK(flags);
			MACIO_BIS(KEYLARGO_FCR1, KL1_USB2_CELL_ENABLE);
		}
		if (number < 4) {
			reg = MACIO_IN32(KEYLARGO_FCR4);
			reg &=	~(KL4_PORT_WAKEUP_ENABLE(number) | KL4_PORT_RESUME_WAKE_EN(number) |
				KL4_PORT_CONNECT_WAKE_EN(number) | KL4_PORT_DISCONNECT_WAKE_EN(number));
			reg &=	~(KL4_PORT_WAKEUP_ENABLE(number+1) | KL4_PORT_RESUME_WAKE_EN(number+1) |
				KL4_PORT_CONNECT_WAKE_EN(number+1) | KL4_PORT_DISCONNECT_WAKE_EN(number+1));
			MACIO_OUT32(KEYLARGO_FCR4, reg);
			(void)MACIO_IN32(KEYLARGO_FCR4);
			udelay(10);
		} else {
			reg = MACIO_IN32(KEYLARGO_FCR3);
			reg &=	~(KL3_IT_PORT_WAKEUP_ENABLE(0) | KL3_IT_PORT_RESUME_WAKE_EN(0) |
				KL3_IT_PORT_CONNECT_WAKE_EN(0) | KL3_IT_PORT_DISCONNECT_WAKE_EN(0));
			reg &=	~(KL3_IT_PORT_WAKEUP_ENABLE(1) | KL3_IT_PORT_RESUME_WAKE_EN(1) |
				KL3_IT_PORT_CONNECT_WAKE_EN(1) | KL3_IT_PORT_DISCONNECT_WAKE_EN(1));
			MACIO_OUT32(KEYLARGO_FCR3, reg);
			(void)MACIO_IN32(KEYLARGO_FCR3);
			udelay(10);
		}
		if (macio->type == macio_intrepid) {
			/* wait for clock stopped bits to clear */
			u32 test0 = 0, test1 = 0;
			u32 status0, status1;
			int timeout = 1000;

			UNLOCK(flags);
			switch (number) {
			case 0:
				test0 = UNI_N_CLOCK_STOPPED_USB0;
				test1 = UNI_N_CLOCK_STOPPED_USB0PCI;
				break;
			case 2:
				test0 = UNI_N_CLOCK_STOPPED_USB1;
				test1 = UNI_N_CLOCK_STOPPED_USB1PCI;
				break;
			case 4:
				test0 = UNI_N_CLOCK_STOPPED_USB2;
				test1 = UNI_N_CLOCK_STOPPED_USB2PCI;
				break;
			}
			do {
				if (--timeout <= 0) {
					printk(KERN_ERR "core99_usb_enable: "
					       "Timeout waiting for clocks\n");
					break;
				}
				mdelay(1);
				status0 = UN_IN(UNI_N_CLOCK_STOP_STATUS0);
				status1 = UN_IN(UNI_N_CLOCK_STOP_STATUS1);
			} while ((status0 & test0) | (status1 & test1));
			LOCK(flags);
		}
	} else {
		/* Turn OFF */
		if (number < 4) {
			reg = MACIO_IN32(KEYLARGO_FCR4);
			reg |=	KL4_PORT_WAKEUP_ENABLE(number) | KL4_PORT_RESUME_WAKE_EN(number) |
				KL4_PORT_CONNECT_WAKE_EN(number) | KL4_PORT_DISCONNECT_WAKE_EN(number);
			reg |=	KL4_PORT_WAKEUP_ENABLE(number+1) | KL4_PORT_RESUME_WAKE_EN(number+1) |
				KL4_PORT_CONNECT_WAKE_EN(number+1) | KL4_PORT_DISCONNECT_WAKE_EN(number+1);
			MACIO_OUT32(KEYLARGO_FCR4, reg);
			(void)MACIO_IN32(KEYLARGO_FCR4);
			udelay(1);
		} else {
			reg = MACIO_IN32(KEYLARGO_FCR3);
			reg |=	KL3_IT_PORT_WAKEUP_ENABLE(0) | KL3_IT_PORT_RESUME_WAKE_EN(0) |
				KL3_IT_PORT_CONNECT_WAKE_EN(0) | KL3_IT_PORT_DISCONNECT_WAKE_EN(0);
			reg |=	KL3_IT_PORT_WAKEUP_ENABLE(1) | KL3_IT_PORT_RESUME_WAKE_EN(1) |
				KL3_IT_PORT_CONNECT_WAKE_EN(1) | KL3_IT_PORT_DISCONNECT_WAKE_EN(1);
			MACIO_OUT32(KEYLARGO_FCR3, reg);
			(void)MACIO_IN32(KEYLARGO_FCR3);
			udelay(1);
		}
		if (number == 0) {
			if (macio->type != macio_intrepid)
				MACIO_BIC(KEYLARGO_FCR0, KL0_USB0_CELL_ENABLE);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			udelay(1);
			MACIO_BIS(KEYLARGO_FCR0, (KL0_USB0_PAD_SUSPEND0 | KL0_USB0_PAD_SUSPEND1));
			(void)MACIO_IN32(KEYLARGO_FCR0);
		} else if (number == 2) {
			if (macio->type != macio_intrepid)
				MACIO_BIC(KEYLARGO_FCR0, KL0_USB1_CELL_ENABLE);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			udelay(1);
			MACIO_BIS(KEYLARGO_FCR0, (KL0_USB1_PAD_SUSPEND0 | KL0_USB1_PAD_SUSPEND1));
			(void)MACIO_IN32(KEYLARGO_FCR0);
		} else if (number == 4) {
			udelay(1);
			MACIO_BIS(KEYLARGO_FCR1, (KL1_USB2_PAD_SUSPEND0 | KL1_USB2_PAD_SUSPEND1));
			(void)MACIO_IN32(KEYLARGO_FCR1);
		}
		udelay(1);
	}
	UNLOCK(flags);

	return 0;
}

static long
core99_firewire_enable(struct device_node *node, long param, long value)
{
	unsigned long flags;
	struct macio_chip *macio;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea &&
	    macio->type != macio_intrepid)
		return -ENODEV;
	if (!(macio->flags & MACIO_FLAG_FW_SUPPORTED))
		return -ENODEV;

	LOCK(flags);
	if (value) {
		UN_BIS(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_FW);
		(void)UN_IN(UNI_N_CLOCK_CNTL);
	} else {
		UN_BIC(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_FW);
		(void)UN_IN(UNI_N_CLOCK_CNTL);
	}
	UNLOCK(flags);
	mdelay(1);

	return 0;
}

static long
core99_firewire_cable_power(struct device_node *node, long param, long value)
{
	unsigned long flags;
	struct macio_chip *macio;

	/* Trick: we allow NULL node */
	if ((pmac_mb.board_flags & PMAC_MB_HAS_FW_POWER) == 0)
		return -ENODEV;
	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea &&
	    macio->type != macio_intrepid)
		return -ENODEV;
	if (!(macio->flags & MACIO_FLAG_FW_SUPPORTED))
		return -ENODEV;

	LOCK(flags);
	if (value) {
		MACIO_OUT8(KL_GPIO_FW_CABLE_POWER , 0);
		MACIO_IN8(KL_GPIO_FW_CABLE_POWER);
		udelay(10);
	} else {
		MACIO_OUT8(KL_GPIO_FW_CABLE_POWER , 4);
		MACIO_IN8(KL_GPIO_FW_CABLE_POWER); udelay(10);
	}
	UNLOCK(flags);
	mdelay(1);

	return 0;
}

static long
intrepid_aack_delay_enable(struct device_node *node, long param, long value)
{
	unsigned long flags;

	if (uninorth_rev < 0xd2)
		return -ENODEV;

	LOCK(flags);
	if (param)
		UN_BIS(UNI_N_AACK_DELAY, UNI_N_AACK_DELAY_ENABLE);
	else
		UN_BIC(UNI_N_AACK_DELAY, UNI_N_AACK_DELAY_ENABLE);
	UNLOCK(flags);

	return 0;
}


#endif /* CONFIG_PPC64 */

static long
core99_read_gpio(struct device_node *node, long param, long value)
{
	struct macio_chip *macio = &macio_chips[0];

	return MACIO_IN8(param);
}


static long
core99_write_gpio(struct device_node *node, long param, long value)
{
	struct macio_chip *macio = &macio_chips[0];

	MACIO_OUT8(param, (u8)(value & 0xff));
	return 0;
}

#ifdef CONFIG_PPC64
static long g5_gmac_enable(struct device_node *node, long param, long value)
{
	struct macio_chip *macio = &macio_chips[0];
	unsigned long flags;

	if (node == NULL)
		return -ENODEV;

	LOCK(flags);
	if (value) {
		MACIO_BIS(KEYLARGO_FCR1, K2_FCR1_GMAC_CLK_ENABLE);
		mb();
		k2_skiplist[0] = NULL;
	} else {
		k2_skiplist[0] = node;
		mb();
		MACIO_BIC(KEYLARGO_FCR1, K2_FCR1_GMAC_CLK_ENABLE);
	}
	
	UNLOCK(flags);
	mdelay(1);

	return 0;
}

static long g5_fw_enable(struct device_node *node, long param, long value)
{
	struct macio_chip *macio = &macio_chips[0];
	unsigned long flags;

	if (node == NULL)
		return -ENODEV;

	LOCK(flags);
	if (value) {
		MACIO_BIS(KEYLARGO_FCR1, K2_FCR1_FW_CLK_ENABLE);
		mb();
		k2_skiplist[1] = NULL;
	} else {
		k2_skiplist[1] = node;
		mb();
		MACIO_BIC(KEYLARGO_FCR1, K2_FCR1_FW_CLK_ENABLE);
	}
	
	UNLOCK(flags);
	mdelay(1);

	return 0;
}

static long g5_mpic_enable(struct device_node *node, long param, long value)
{
	unsigned long flags;
	struct device_node *parent = of_get_parent(node);
	int is_u3;

	if (parent == NULL)
		return 0;
	is_u3 = of_node_name_eq(parent, "u3") || of_node_name_eq(parent, "u4");
	of_node_put(parent);
	if (!is_u3)
		return 0;

	LOCK(flags);
	UN_BIS(U3_TOGGLE_REG, U3_MPIC_RESET | U3_MPIC_OUTPUT_ENABLE);
	UNLOCK(flags);

	return 0;
}

static long g5_eth_phy_reset(struct device_node *node, long param, long value)
{
	struct macio_chip *macio = &macio_chips[0];
	struct device_node *phy;
	int need_reset;

	/*
	 * We must not reset the combo PHYs, only the BCM5221 found in
	 * the iMac G5.
	 */
	phy = of_get_next_child(node, NULL);
	if (!phy)
		return -ENODEV;
	need_reset = of_device_is_compatible(phy, "B5221");
	of_node_put(phy);
	if (!need_reset)
		return 0;

	/* PHY reset is GPIO 29, not in device-tree unfortunately */
	MACIO_OUT8(K2_GPIO_EXTINT_0 + 29,
		   KEYLARGO_GPIO_OUTPUT_ENABLE | KEYLARGO_GPIO_OUTOUT_DATA);
	/* Thankfully, this is now always called at a time when we can
	 * schedule by sungem.
	 */
	msleep(10);
	MACIO_OUT8(K2_GPIO_EXTINT_0 + 29, 0);

	return 0;
}

static long g5_i2s_enable(struct device_node *node, long param, long value)
{
	/* Very crude implementation for now */
	struct macio_chip *macio = &macio_chips[0];
	unsigned long flags;
	int cell;
	u32 fcrs[3][3] = {
		{ 0,
		  K2_FCR1_I2S0_CELL_ENABLE |
		  K2_FCR1_I2S0_CLK_ENABLE_BIT | K2_FCR1_I2S0_ENABLE,
		  KL3_I2S0_CLK18_ENABLE
		},
		{ KL0_SCC_A_INTF_ENABLE,
		  K2_FCR1_I2S1_CELL_ENABLE |
		  K2_FCR1_I2S1_CLK_ENABLE_BIT | K2_FCR1_I2S1_ENABLE,
		  KL3_I2S1_CLK18_ENABLE
		},
		{ KL0_SCC_B_INTF_ENABLE,
		  SH_FCR1_I2S2_CELL_ENABLE |
		  SH_FCR1_I2S2_CLK_ENABLE_BIT | SH_FCR1_I2S2_ENABLE,
		  SH_FCR3_I2S2_CLK18_ENABLE
		},
	};

	if (macio->type != macio_keylargo2 && macio->type != macio_shasta)
		return -ENODEV;
	if (strncmp(node->name, "i2s-", 4))
		return -ENODEV;
	cell = node->name[4] - 'a';
	switch(cell) {
	case 0:
	case 1:
		break;
	case 2:
		if (macio->type == macio_shasta)
			break;
		fallthrough;
	default:
		return -ENODEV;
	}

	LOCK(flags);
	if (value) {
		MACIO_BIC(KEYLARGO_FCR0, fcrs[cell][0]);
		MACIO_BIS(KEYLARGO_FCR1, fcrs[cell][1]);
		MACIO_BIS(KEYLARGO_FCR3, fcrs[cell][2]);
	} else {
		MACIO_BIC(KEYLARGO_FCR3, fcrs[cell][2]);
		MACIO_BIC(KEYLARGO_FCR1, fcrs[cell][1]);
		MACIO_BIS(KEYLARGO_FCR0, fcrs[cell][0]);
	}
	udelay(10);
	UNLOCK(flags);

	return 0;
}


#ifdef CONFIG_SMP
static long g5_reset_cpu(struct device_node *node, long param, long value)
{
	unsigned int reset_io = 0;
	unsigned long flags;
	struct macio_chip *macio;
	struct device_node *np;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo2 && macio->type != macio_shasta)
		return -ENODEV;

	for_each_of_cpu_node(np) {
		const u32 *rst = of_get_property(np, "soft-reset", NULL);
		if (!rst)
			continue;
		if (param == of_get_cpu_hwid(np, 0)) {
			of_node_put(np);
			reset_io = *rst;
			break;
		}
	}
	if (np == NULL || reset_io == 0)
		return -ENODEV;

	LOCK(flags);
	MACIO_OUT8(reset_io, KEYLARGO_GPIO_OUTPUT_ENABLE);
	(void)MACIO_IN8(reset_io);
	udelay(1);
	MACIO_OUT8(reset_io, 0);
	(void)MACIO_IN8(reset_io);
	UNLOCK(flags);

	return 0;
}
#endif /* CONFIG_SMP */

/*
 * This can be called from pmac_smp so isn't static
 *
 * This takes the second CPU off the bus on dual CPU machines
 * running UP
 */
void __init g5_phy_disable_cpu1(void)
{
	if (uninorth_maj == 3)
		UN_OUT(U3_API_PHY_CONFIG_1, 0);
}
#endif /* CONFIG_PPC64 */

#ifndef CONFIG_PPC64


#ifdef CONFIG_PM
static u32 save_gpio_levels[2];
static u8 save_gpio_extint[KEYLARGO_GPIO_EXTINT_CNT];
static u8 save_gpio_normal[KEYLARGO_GPIO_CNT];
static u32 save_unin_clock_ctl;

static void keylargo_shutdown(struct macio_chip *macio, int sleep_mode)
{
	u32 temp;

	if (sleep_mode) {
		mdelay(1);
		MACIO_BIS(KEYLARGO_FCR0, KL0_USB_REF_SUSPEND);
		(void)MACIO_IN32(KEYLARGO_FCR0);
		mdelay(1);
	}

	MACIO_BIC(KEYLARGO_FCR0,KL0_SCCA_ENABLE | KL0_SCCB_ENABLE |
				KL0_SCC_CELL_ENABLE |
				KL0_IRDA_ENABLE | KL0_IRDA_CLK32_ENABLE |
				KL0_IRDA_CLK19_ENABLE);

	MACIO_BIC(KEYLARGO_MBCR, KL_MBCR_MB0_DEV_MASK);
	MACIO_BIS(KEYLARGO_MBCR, KL_MBCR_MB0_IDE_ENABLE);

	MACIO_BIC(KEYLARGO_FCR1,
		KL1_AUDIO_SEL_22MCLK | KL1_AUDIO_CLK_ENABLE_BIT |
		KL1_AUDIO_CLK_OUT_ENABLE | KL1_AUDIO_CELL_ENABLE |
		KL1_I2S0_CELL_ENABLE | KL1_I2S0_CLK_ENABLE_BIT |
		KL1_I2S0_ENABLE | KL1_I2S1_CELL_ENABLE |
		KL1_I2S1_CLK_ENABLE_BIT | KL1_I2S1_ENABLE |
		KL1_EIDE0_ENABLE | KL1_EIDE0_RESET_N |
		KL1_EIDE1_ENABLE | KL1_EIDE1_RESET_N |
		KL1_UIDE_ENABLE);

	MACIO_BIS(KEYLARGO_FCR2, KL2_ALT_DATA_OUT);
	MACIO_BIC(KEYLARGO_FCR2, KL2_IOBUS_ENABLE);

	temp = MACIO_IN32(KEYLARGO_FCR3);
	if (macio->rev >= 2) {
		temp |= KL3_SHUTDOWN_PLL2X;
		if (sleep_mode)
			temp |= KL3_SHUTDOWN_PLL_TOTAL;
	}

	temp |= KL3_SHUTDOWN_PLLKW6 | KL3_SHUTDOWN_PLLKW4 |
		KL3_SHUTDOWN_PLLKW35;
	if (sleep_mode)
		temp |= KL3_SHUTDOWN_PLLKW12;
	temp &= ~(KL3_CLK66_ENABLE | KL3_CLK49_ENABLE | KL3_CLK45_ENABLE
		| KL3_CLK31_ENABLE | KL3_I2S1_CLK18_ENABLE | KL3_I2S0_CLK18_ENABLE);
	if (sleep_mode)
		temp &= ~(KL3_TIMER_CLK18_ENABLE | KL3_VIA_CLK16_ENABLE);
	MACIO_OUT32(KEYLARGO_FCR3, temp);

	/* Flush posted writes & wait a bit */
	(void)MACIO_IN32(KEYLARGO_FCR0); mdelay(1);
}

static void pangea_shutdown(struct macio_chip *macio, int sleep_mode)
{
	u32 temp;

	MACIO_BIC(KEYLARGO_FCR0,KL0_SCCA_ENABLE | KL0_SCCB_ENABLE |
				KL0_SCC_CELL_ENABLE |
				KL0_USB0_CELL_ENABLE | KL0_USB1_CELL_ENABLE);

	MACIO_BIC(KEYLARGO_FCR1,
		KL1_AUDIO_SEL_22MCLK | KL1_AUDIO_CLK_ENABLE_BIT |
		KL1_AUDIO_CLK_OUT_ENABLE | KL1_AUDIO_CELL_ENABLE |
		KL1_I2S0_CELL_ENABLE | KL1_I2S0_CLK_ENABLE_BIT |
		KL1_I2S0_ENABLE | KL1_I2S1_CELL_ENABLE |
		KL1_I2S1_CLK_ENABLE_BIT | KL1_I2S1_ENABLE |
		KL1_UIDE_ENABLE);
	if (pmac_mb.board_flags & PMAC_MB_MOBILE)
		MACIO_BIC(KEYLARGO_FCR1, KL1_UIDE_RESET_N);

	MACIO_BIS(KEYLARGO_FCR2, KL2_ALT_DATA_OUT);

	temp = MACIO_IN32(KEYLARGO_FCR3);
	temp |= KL3_SHUTDOWN_PLLKW6 | KL3_SHUTDOWN_PLLKW4 |
		KL3_SHUTDOWN_PLLKW35;
	temp &= ~(KL3_CLK49_ENABLE | KL3_CLK45_ENABLE | KL3_CLK31_ENABLE
		| KL3_I2S0_CLK18_ENABLE | KL3_I2S1_CLK18_ENABLE);
	if (sleep_mode)
		temp &= ~(KL3_VIA_CLK16_ENABLE | KL3_TIMER_CLK18_ENABLE);
	MACIO_OUT32(KEYLARGO_FCR3, temp);

	/* Flush posted writes & wait a bit */
	(void)MACIO_IN32(KEYLARGO_FCR0); mdelay(1);
}

static void intrepid_shutdown(struct macio_chip *macio, int sleep_mode)
{
	u32 temp;

	MACIO_BIC(KEYLARGO_FCR0,KL0_SCCA_ENABLE | KL0_SCCB_ENABLE |
		  KL0_SCC_CELL_ENABLE);

	MACIO_BIC(KEYLARGO_FCR1,
		KL1_I2S0_CELL_ENABLE | KL1_I2S0_CLK_ENABLE_BIT |
		KL1_I2S0_ENABLE | KL1_I2S1_CELL_ENABLE |
		KL1_I2S1_CLK_ENABLE_BIT | KL1_I2S1_ENABLE |
		KL1_EIDE0_ENABLE);
	if (pmac_mb.board_flags & PMAC_MB_MOBILE)
		MACIO_BIC(KEYLARGO_FCR1, KL1_UIDE_RESET_N);

	temp = MACIO_IN32(KEYLARGO_FCR3);
	temp &= ~(KL3_CLK49_ENABLE | KL3_CLK45_ENABLE |
		  KL3_I2S1_CLK18_ENABLE | KL3_I2S0_CLK18_ENABLE);
	if (sleep_mode)
		temp &= ~(KL3_TIMER_CLK18_ENABLE | KL3_IT_VIA_CLK32_ENABLE);
	MACIO_OUT32(KEYLARGO_FCR3, temp);

	/* Flush posted writes & wait a bit */
	(void)MACIO_IN32(KEYLARGO_FCR0);
	mdelay(10);
}


static int
core99_sleep(void)
{
	struct macio_chip *macio;
	int i;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea &&
	    macio->type != macio_intrepid)
		return -ENODEV;

	/* We power off the wireless slot in case it was not done
	 * by the driver. We don't power it on automatically however
	 */
	if (macio->flags & MACIO_FLAG_AIRPORT_ON)
		core99_airport_enable(macio->of_node, 0, 0);

	/* We power off the FW cable. Should be done by the driver... */
	if (macio->flags & MACIO_FLAG_FW_SUPPORTED) {
		core99_firewire_enable(NULL, 0, 0);
		core99_firewire_cable_power(NULL, 0, 0);
	}

	/* We make sure int. modem is off (in case driver lost it) */
	if (macio->type == macio_keylargo)
		core99_modem_enable(macio->of_node, 0, 0);
	else
		pangea_modem_enable(macio->of_node, 0, 0);

	/* We make sure the sound is off as well */
	core99_sound_chip_enable(macio->of_node, 0, 0);

	/*
	 * Save various bits of KeyLargo
	 */

	/* Save the state of the various GPIOs */
	save_gpio_levels[0] = MACIO_IN32(KEYLARGO_GPIO_LEVELS0);
	save_gpio_levels[1] = MACIO_IN32(KEYLARGO_GPIO_LEVELS1);
	for (i=0; i<KEYLARGO_GPIO_EXTINT_CNT; i++)
		save_gpio_extint[i] = MACIO_IN8(KEYLARGO_GPIO_EXTINT_0+i);
	for (i=0; i<KEYLARGO_GPIO_CNT; i++)
		save_gpio_normal[i] = MACIO_IN8(KEYLARGO_GPIO_0+i);

	/* Save the FCRs */
	if (macio->type == macio_keylargo)
		save_mbcr = MACIO_IN32(KEYLARGO_MBCR);
	save_fcr[0] = MACIO_IN32(KEYLARGO_FCR0);
	save_fcr[1] = MACIO_IN32(KEYLARGO_FCR1);
	save_fcr[2] = MACIO_IN32(KEYLARGO_FCR2);
	save_fcr[3] = MACIO_IN32(KEYLARGO_FCR3);
	save_fcr[4] = MACIO_IN32(KEYLARGO_FCR4);
	if (macio->type == macio_pangea || macio->type == macio_intrepid)
		save_fcr[5] = MACIO_IN32(KEYLARGO_FCR5);

	/* Save state & config of DBDMA channels */
	dbdma_save(macio, save_dbdma);

	/*
	 * Turn off as much as we can
	 */
	if (macio->type == macio_pangea)
		pangea_shutdown(macio, 1);
	else if (macio->type == macio_intrepid)
		intrepid_shutdown(macio, 1);
	else if (macio->type == macio_keylargo)
		keylargo_shutdown(macio, 1);

	/*
	 * Put the host bridge to sleep
	 */

	save_unin_clock_ctl = UN_IN(UNI_N_CLOCK_CNTL);
	/* Note: do not switch GMAC off, driver does it when necessary, WOL must keep it
	 * enabled !
	 */
	UN_OUT(UNI_N_CLOCK_CNTL, save_unin_clock_ctl &
	       ~(/*UNI_N_CLOCK_CNTL_GMAC|*/UNI_N_CLOCK_CNTL_FW/*|UNI_N_CLOCK_CNTL_PCI*/));
	udelay(100);
	UN_OUT(UNI_N_HWINIT_STATE, UNI_N_HWINIT_STATE_SLEEPING);
	UN_OUT(UNI_N_POWER_MGT, UNI_N_POWER_MGT_SLEEP);
	mdelay(10);

	/*
	 * FIXME: A bit of black magic with OpenPIC (don't ask me why)
	 */
	if (pmac_mb.model_id == PMAC_TYPE_SAWTOOTH) {
		MACIO_BIS(0x506e0, 0x00400000);
		MACIO_BIS(0x506e0, 0x80000000);
	}
	return 0;
}

static int
core99_wake_up(void)
{
	struct macio_chip *macio;
	int i;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea &&
	    macio->type != macio_intrepid)
		return -ENODEV;

	/*
	 * Wakeup the host bridge
	 */
	UN_OUT(UNI_N_POWER_MGT, UNI_N_POWER_MGT_NORMAL);
	udelay(10);
	UN_OUT(UNI_N_HWINIT_STATE, UNI_N_HWINIT_STATE_RUNNING);
	udelay(10);

	/*
	 * Restore KeyLargo
	 */

	if (macio->type == macio_keylargo) {
		MACIO_OUT32(KEYLARGO_MBCR, save_mbcr);
		(void)MACIO_IN32(KEYLARGO_MBCR); udelay(10);
	}
	MACIO_OUT32(KEYLARGO_FCR0, save_fcr[0]);
	(void)MACIO_IN32(KEYLARGO_FCR0); udelay(10);
	MACIO_OUT32(KEYLARGO_FCR1, save_fcr[1]);
	(void)MACIO_IN32(KEYLARGO_FCR1); udelay(10);
	MACIO_OUT32(KEYLARGO_FCR2, save_fcr[2]);
	(void)MACIO_IN32(KEYLARGO_FCR2); udelay(10);
	MACIO_OUT32(KEYLARGO_FCR3, save_fcr[3]);
	(void)MACIO_IN32(KEYLARGO_FCR3); udelay(10);
	MACIO_OUT32(KEYLARGO_FCR4, save_fcr[4]);
	(void)MACIO_IN32(KEYLARGO_FCR4); udelay(10);
	if (macio->type == macio_pangea || macio->type == macio_intrepid) {
		MACIO_OUT32(KEYLARGO_FCR5, save_fcr[5]);
		(void)MACIO_IN32(KEYLARGO_FCR5); udelay(10);
	}

	dbdma_restore(macio, save_dbdma);

	MACIO_OUT32(KEYLARGO_GPIO_LEVELS0, save_gpio_levels[0]);
	MACIO_OUT32(KEYLARGO_GPIO_LEVELS1, save_gpio_levels[1]);
	for (i=0; i<KEYLARGO_GPIO_EXTINT_CNT; i++)
		MACIO_OUT8(KEYLARGO_GPIO_EXTINT_0+i, save_gpio_extint[i]);
	for (i=0; i<KEYLARGO_GPIO_CNT; i++)
		MACIO_OUT8(KEYLARGO_GPIO_0+i, save_gpio_normal[i]);

	/* FIXME more black magic with OpenPIC ... */
	if (pmac_mb.model_id == PMAC_TYPE_SAWTOOTH) {
		MACIO_BIC(0x506e0, 0x00400000);
		MACIO_BIC(0x506e0, 0x80000000);
	}

	UN_OUT(UNI_N_CLOCK_CNTL, save_unin_clock_ctl);
	udelay(100);

	return 0;
}

#endif /* CONFIG_PM */

static long
core99_sleep_state(struct device_node *node, long param, long value)
{
	/* Param == 1 means to enter the "fake sleep" mode that is
	 * used for CPU speed switch
	 */
	if (param == 1) {
		if (value == 1) {
			UN_OUT(UNI_N_HWINIT_STATE, UNI_N_HWINIT_STATE_SLEEPING);
			UN_OUT(UNI_N_POWER_MGT, UNI_N_POWER_MGT_IDLE2);
		} else {
			UN_OUT(UNI_N_POWER_MGT, UNI_N_POWER_MGT_NORMAL);
			udelay(10);
			UN_OUT(UNI_N_HWINIT_STATE, UNI_N_HWINIT_STATE_RUNNING);
			udelay(10);
		}
		return 0;
	}
	if ((pmac_mb.board_flags & PMAC_MB_CAN_SLEEP) == 0)
		return -EPERM;

#ifdef CONFIG_PM
	if (value == 1)
		return core99_sleep();
	else if (value == 0)
		return core99_wake_up();

#endif /* CONFIG_PM */
	return 0;
}

#endif /* CONFIG_PPC64 */

static long
generic_dev_can_wake(struct device_node *node, long param, long value)
{
	/* Todo: eventually check we are really dealing with on-board
	 * video device ...
	 */

	if (pmac_mb.board_flags & PMAC_MB_MAY_SLEEP)
		pmac_mb.board_flags |= PMAC_MB_CAN_SLEEP;
	return 0;
}

static long generic_get_mb_info(struct device_node *node, long param, long value)
{
	switch(param) {
		case PMAC_MB_INFO_MODEL:
			return pmac_mb.model_id;
		case PMAC_MB_INFO_FLAGS:
			return pmac_mb.board_flags;
		case PMAC_MB_INFO_NAME:
			/* hack hack hack... but should work */
			*((const char **)value) = pmac_mb.model_name;
			return 0;
	}
	return -EINVAL;
}


/*
 * Table definitions
 */

/* Used on any machine
 */
static struct feature_table_entry any_features[] = {
	{ PMAC_FTR_GET_MB_INFO,		generic_get_mb_info },
	{ PMAC_FTR_DEVICE_CAN_WAKE,	generic_dev_can_wake },
	{ 0, NULL }
};

#ifndef CONFIG_PPC64

/* OHare based motherboards. Currently, we only use these on the
 * 2400,3400 and 3500 series powerbooks. Some older desktops seem
 * to have issues with turning on/off those asic cells
 */
static struct feature_table_entry ohare_features[] = {
	{ PMAC_FTR_SCC_ENABLE,		ohare_htw_scc_enable },
	{ PMAC_FTR_SWIM3_ENABLE,	ohare_floppy_enable },
	{ PMAC_FTR_MESH_ENABLE,		ohare_mesh_enable },
	{ PMAC_FTR_IDE_ENABLE,		ohare_ide_enable},
	{ PMAC_FTR_IDE_RESET,		ohare_ide_reset},
	{ PMAC_FTR_SLEEP_STATE,		ohare_sleep_state },
	{ 0, NULL }
};

/* Heathrow desktop machines (Beige G3).
 * Separated as some features couldn't be properly tested
 * and the serial port control bits appear to confuse it.
 */
static struct feature_table_entry heathrow_desktop_features[] = {
	{ PMAC_FTR_SWIM3_ENABLE,	heathrow_floppy_enable },
	{ PMAC_FTR_MESH_ENABLE,		heathrow_mesh_enable },
	{ PMAC_FTR_IDE_ENABLE,		heathrow_ide_enable },
	{ PMAC_FTR_IDE_RESET,		heathrow_ide_reset },
	{ PMAC_FTR_BMAC_ENABLE,		heathrow_bmac_enable },
	{ 0, NULL }
};

/* Heathrow based laptop, that is the Wallstreet and mainstreet
 * powerbooks.
 */
static struct feature_table_entry heathrow_laptop_features[] = {
	{ PMAC_FTR_SCC_ENABLE,		ohare_htw_scc_enable },
	{ PMAC_FTR_MODEM_ENABLE,	heathrow_modem_enable },
	{ PMAC_FTR_SWIM3_ENABLE,	heathrow_floppy_enable },
	{ PMAC_FTR_MESH_ENABLE,		heathrow_mesh_enable },
	{ PMAC_FTR_IDE_ENABLE,		heathrow_ide_enable },
	{ PMAC_FTR_IDE_RESET,		heathrow_ide_reset },
	{ PMAC_FTR_BMAC_ENABLE,		heathrow_bmac_enable },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	heathrow_sound_enable },
	{ PMAC_FTR_SLEEP_STATE,		heathrow_sleep_state },
	{ 0, NULL }
};

/* Paddington based machines
 * The lombard (101) powerbook, first iMac models, B&W G3 and Yikes G4.
 */
static struct feature_table_entry paddington_features[] = {
	{ PMAC_FTR_SCC_ENABLE,		ohare_htw_scc_enable },
	{ PMAC_FTR_MODEM_ENABLE,	heathrow_modem_enable },
	{ PMAC_FTR_SWIM3_ENABLE,	heathrow_floppy_enable },
	{ PMAC_FTR_MESH_ENABLE,		heathrow_mesh_enable },
	{ PMAC_FTR_IDE_ENABLE,		heathrow_ide_enable },
	{ PMAC_FTR_IDE_RESET,		heathrow_ide_reset },
	{ PMAC_FTR_BMAC_ENABLE,		heathrow_bmac_enable },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	heathrow_sound_enable },
	{ PMAC_FTR_SLEEP_STATE,		heathrow_sleep_state },
	{ 0, NULL }
};

/* Core99 & MacRISC 2 machines (all machines released since the
 * iBook (included), that is all AGP machines, except pangea
 * chipset. The pangea chipset is the "combo" UniNorth/KeyLargo
 * used on iBook2 & iMac "flow power".
 */
static struct feature_table_entry core99_features[] = {
	{ PMAC_FTR_SCC_ENABLE,		core99_scc_enable },
	{ PMAC_FTR_MODEM_ENABLE,	core99_modem_enable },
	{ PMAC_FTR_IDE_ENABLE,		core99_ide_enable },
	{ PMAC_FTR_IDE_RESET,		core99_ide_reset },
	{ PMAC_FTR_GMAC_ENABLE,		core99_gmac_enable },
	{ PMAC_FTR_GMAC_PHY_RESET,	core99_gmac_phy_reset },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	core99_sound_chip_enable },
	{ PMAC_FTR_AIRPORT_ENABLE,	core99_airport_enable },
	{ PMAC_FTR_USB_ENABLE,		core99_usb_enable },
	{ PMAC_FTR_1394_ENABLE,		core99_firewire_enable },
	{ PMAC_FTR_1394_CABLE_POWER,	core99_firewire_cable_power },
#ifdef CONFIG_PM
	{ PMAC_FTR_SLEEP_STATE,		core99_sleep_state },
#endif
#ifdef CONFIG_SMP
	{ PMAC_FTR_RESET_CPU,		core99_reset_cpu },
#endif /* CONFIG_SMP */
	{ PMAC_FTR_READ_GPIO,		core99_read_gpio },
	{ PMAC_FTR_WRITE_GPIO,		core99_write_gpio },
	{ 0, NULL }
};

/* RackMac
 */
static struct feature_table_entry rackmac_features[] = {
	{ PMAC_FTR_SCC_ENABLE,		core99_scc_enable },
	{ PMAC_FTR_IDE_ENABLE,		core99_ide_enable },
	{ PMAC_FTR_IDE_RESET,		core99_ide_reset },
	{ PMAC_FTR_GMAC_ENABLE,		core99_gmac_enable },
	{ PMAC_FTR_GMAC_PHY_RESET,	core99_gmac_phy_reset },
	{ PMAC_FTR_USB_ENABLE,		core99_usb_enable },
	{ PMAC_FTR_1394_ENABLE,		core99_firewire_enable },
	{ PMAC_FTR_1394_CABLE_POWER,	core99_firewire_cable_power },
	{ PMAC_FTR_SLEEP_STATE,		core99_sleep_state },
#ifdef CONFIG_SMP
	{ PMAC_FTR_RESET_CPU,		core99_reset_cpu },
#endif /* CONFIG_SMP */
	{ PMAC_FTR_READ_GPIO,		core99_read_gpio },
	{ PMAC_FTR_WRITE_GPIO,		core99_write_gpio },
	{ 0, NULL }
};

/* Pangea features
 */
static struct feature_table_entry pangea_features[] = {
	{ PMAC_FTR_SCC_ENABLE,		core99_scc_enable },
	{ PMAC_FTR_MODEM_ENABLE,	pangea_modem_enable },
	{ PMAC_FTR_IDE_ENABLE,		core99_ide_enable },
	{ PMAC_FTR_IDE_RESET,		core99_ide_reset },
	{ PMAC_FTR_GMAC_ENABLE,		core99_gmac_enable },
	{ PMAC_FTR_GMAC_PHY_RESET,	core99_gmac_phy_reset },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	core99_sound_chip_enable },
	{ PMAC_FTR_AIRPORT_ENABLE,	core99_airport_enable },
	{ PMAC_FTR_USB_ENABLE,		core99_usb_enable },
	{ PMAC_FTR_1394_ENABLE,		core99_firewire_enable },
	{ PMAC_FTR_1394_CABLE_POWER,	core99_firewire_cable_power },
	{ PMAC_FTR_SLEEP_STATE,		core99_sleep_state },
	{ PMAC_FTR_READ_GPIO,		core99_read_gpio },
	{ PMAC_FTR_WRITE_GPIO,		core99_write_gpio },
	{ 0, NULL }
};

/* Intrepid features
 */
static struct feature_table_entry intrepid_features[] = {
	{ PMAC_FTR_SCC_ENABLE,		core99_scc_enable },
	{ PMAC_FTR_MODEM_ENABLE,	pangea_modem_enable },
	{ PMAC_FTR_IDE_ENABLE,		core99_ide_enable },
	{ PMAC_FTR_IDE_RESET,		core99_ide_reset },
	{ PMAC_FTR_GMAC_ENABLE,		core99_gmac_enable },
	{ PMAC_FTR_GMAC_PHY_RESET,	core99_gmac_phy_reset },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	core99_sound_chip_enable },
	{ PMAC_FTR_AIRPORT_ENABLE,	core99_airport_enable },
	{ PMAC_FTR_USB_ENABLE,		core99_usb_enable },
	{ PMAC_FTR_1394_ENABLE,		core99_firewire_enable },
	{ PMAC_FTR_1394_CABLE_POWER,	core99_firewire_cable_power },
	{ PMAC_FTR_SLEEP_STATE,		core99_sleep_state },
	{ PMAC_FTR_READ_GPIO,		core99_read_gpio },
	{ PMAC_FTR_WRITE_GPIO,		core99_write_gpio },
	{ PMAC_FTR_AACK_DELAY_ENABLE,	intrepid_aack_delay_enable },
	{ 0, NULL }
};

#else /* CONFIG_PPC64 */

/* G5 features
 */
static struct feature_table_entry g5_features[] = {
	{ PMAC_FTR_GMAC_ENABLE,		g5_gmac_enable },
	{ PMAC_FTR_1394_ENABLE,		g5_fw_enable },
	{ PMAC_FTR_ENABLE_MPIC,		g5_mpic_enable },
	{ PMAC_FTR_GMAC_PHY_RESET,	g5_eth_phy_reset },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	g5_i2s_enable },
#ifdef CONFIG_SMP
	{ PMAC_FTR_RESET_CPU,		g5_reset_cpu },
#endif /* CONFIG_SMP */
	{ PMAC_FTR_READ_GPIO,		core99_read_gpio },
	{ PMAC_FTR_WRITE_GPIO,		core99_write_gpio },
	{ 0, NULL }
};

#endif /* CONFIG_PPC64 */

static struct pmac_mb_def pmac_mb_defs[] = {
#ifndef CONFIG_PPC64
	/*
	 * Desktops
	 */

	{	"AAPL,8500",			"PowerMac 8500/8600",
		PMAC_TYPE_PSURGE,		NULL,
		0
	},
	{	"AAPL,9500",			"PowerMac 9500/9600",
		PMAC_TYPE_PSURGE,		NULL,
		0
	},
	{	"AAPL,7200",			"PowerMac 7200",
		PMAC_TYPE_PSURGE,		NULL,
		0
	},
	{	"AAPL,7300",			"PowerMac 7200/7300",
		PMAC_TYPE_PSURGE,		NULL,
		0
	},
	{	"AAPL,7500",			"PowerMac 7500",
		PMAC_TYPE_PSURGE,		NULL,
		0
	},
	{	"AAPL,ShinerESB",		"Apple Network Server",
		PMAC_TYPE_ANS,			NULL,
		0
	},
	{	"AAPL,e407",			"Alchemy",
		PMAC_TYPE_ALCHEMY,		NULL,
		0
	},
	{	"AAPL,e411",			"Gazelle",
		PMAC_TYPE_GAZELLE,		NULL,
		0
	},
	{	"AAPL,Gossamer",		"PowerMac G3 (Gossamer)",
		PMAC_TYPE_GOSSAMER,		heathrow_desktop_features,
		0
	},
	{	"AAPL,PowerMac G3",		"PowerMac G3 (Silk)",
		PMAC_TYPE_SILK,			heathrow_desktop_features,
		0
	},
	{	"PowerMac1,1",			"Blue&White G3",
		PMAC_TYPE_YOSEMITE,		paddington_features,
		0
	},
	{	"PowerMac1,2",			"PowerMac G4 PCI Graphics",
		PMAC_TYPE_YIKES,		paddington_features,
		0
	},
	{	"PowerMac2,1",			"iMac FireWire",
		PMAC_TYPE_FW_IMAC,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_OLD_CORE99
	},
	{	"PowerMac2,2",			"iMac FireWire",
		PMAC_TYPE_FW_IMAC,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_OLD_CORE99
	},
	{	"PowerMac3,1",			"PowerMac G4 AGP Graphics",
		PMAC_TYPE_SAWTOOTH,		core99_features,
		PMAC_MB_OLD_CORE99
	},
	{	"PowerMac3,2",			"PowerMac G4 AGP Graphics",
		PMAC_TYPE_SAWTOOTH,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_OLD_CORE99
	},
	{	"PowerMac3,3",			"PowerMac G4 AGP Graphics",
		PMAC_TYPE_SAWTOOTH,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_OLD_CORE99
	},
	{	"PowerMac3,4",			"PowerMac G4 Silver",
		PMAC_TYPE_QUICKSILVER,		core99_features,
		PMAC_MB_MAY_SLEEP
	},
	{	"PowerMac3,5",			"PowerMac G4 Silver",
		PMAC_TYPE_QUICKSILVER,		core99_features,
		PMAC_MB_MAY_SLEEP
	},
	{	"PowerMac3,6",			"PowerMac G4 Windtunnel",
		PMAC_TYPE_WINDTUNNEL,		core99_features,
		PMAC_MB_MAY_SLEEP,
	},
	{	"PowerMac4,1",			"iMac \"Flower Power\"",
		PMAC_TYPE_PANGEA_IMAC,		pangea_features,
		PMAC_MB_MAY_SLEEP
	},
	{	"PowerMac4,2",			"Flat panel iMac",
		PMAC_TYPE_FLAT_PANEL_IMAC,	pangea_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"PowerMac4,4",			"eMac",
		PMAC_TYPE_EMAC,			core99_features,
		PMAC_MB_MAY_SLEEP
	},
	{	"PowerMac5,1",			"PowerMac G4 Cube",
		PMAC_TYPE_CUBE,			core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_OLD_CORE99
	},
	{	"PowerMac6,1",			"Flat panel iMac",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP,
	},
	{	"PowerMac6,3",			"Flat panel iMac",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP,
	},
	{	"PowerMac6,4",			"eMac",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP,
	},
	{	"PowerMac10,1",			"Mac mini",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP,
	},
	{       "PowerMac10,2",                 "Mac mini (Late 2005)",
		PMAC_TYPE_UNKNOWN_INTREPID,     intrepid_features,
		PMAC_MB_MAY_SLEEP,
	},
 	{	"iMac,1",			"iMac (first generation)",
		PMAC_TYPE_ORIG_IMAC,		paddington_features,
		0
	},

	/*
	 * Xserve's
	 */

	{	"RackMac1,1",			"XServe",
		PMAC_TYPE_RACKMAC,		rackmac_features,
		0,
	},
	{	"RackMac1,2",			"XServe rev. 2",
		PMAC_TYPE_RACKMAC,		rackmac_features,
		0,
	},

	/*
	 * Laptops
	 */

	{	"AAPL,3400/2400",		"PowerBook 3400",
		PMAC_TYPE_HOOPER,		ohare_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_MOBILE
	},
	{	"AAPL,3500",			"PowerBook 3500",
		PMAC_TYPE_KANGA,		ohare_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_MOBILE
	},
	{	"AAPL,PowerBook1998",		"PowerBook Wallstreet",
		PMAC_TYPE_WALLSTREET,		heathrow_laptop_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_MOBILE
	},
	{	"PowerBook1,1",			"PowerBook 101 (Lombard)",
		PMAC_TYPE_101_PBOOK,		paddington_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_MOBILE
	},
	{	"PowerBook2,1",			"iBook (first generation)",
		PMAC_TYPE_ORIG_IBOOK,		core99_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_OLD_CORE99 | PMAC_MB_MOBILE
	},
	{	"PowerBook2,2",			"iBook FireWire",
		PMAC_TYPE_FW_IBOOK,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER |
		PMAC_MB_OLD_CORE99 | PMAC_MB_MOBILE
	},
	{	"PowerBook3,1",			"PowerBook Pismo",
		PMAC_TYPE_PISMO,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER |
		PMAC_MB_OLD_CORE99 | PMAC_MB_MOBILE
	},
	{	"PowerBook3,2",			"PowerBook Titanium",
		PMAC_TYPE_TITANIUM,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE
	},
	{	"PowerBook3,3",			"PowerBook Titanium II",
		PMAC_TYPE_TITANIUM2,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE
	},
	{	"PowerBook3,4",			"PowerBook Titanium III",
		PMAC_TYPE_TITANIUM3,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE
	},
	{	"PowerBook3,5",			"PowerBook Titanium IV",
		PMAC_TYPE_TITANIUM4,		core99_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE
	},
	{	"PowerBook4,1",			"iBook 2",
		PMAC_TYPE_IBOOK2,		pangea_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE
	},
	{	"PowerBook4,2",			"iBook 2",
		PMAC_TYPE_IBOOK2,		pangea_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE
	},
	{	"PowerBook4,3",			"iBook 2 rev. 2",
		PMAC_TYPE_IBOOK2,		pangea_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE
	},
	{	"PowerBook5,1",			"PowerBook G4 17\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook5,2",			"PowerBook G4 15\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook5,3",			"PowerBook G4 17\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook5,4",			"PowerBook G4 15\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook5,5",			"PowerBook G4 17\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook5,6",			"PowerBook G4 15\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook5,7",			"PowerBook G4 17\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook5,8",			"PowerBook G4 15\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP  | PMAC_MB_MOBILE,
	},
	{	"PowerBook5,9",			"PowerBook G4 17\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_MOBILE,
	},
	{	"PowerBook6,1",			"PowerBook G4 12\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook6,2",			"PowerBook G4",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook6,3",			"iBook G4",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook6,4",			"PowerBook G4 12\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook6,5",			"iBook G4",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook6,7",			"iBook G4",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
	{	"PowerBook6,8",			"PowerBook G4 12\"",
		PMAC_TYPE_UNKNOWN_INTREPID,	intrepid_features,
		PMAC_MB_MAY_SLEEP | PMAC_MB_HAS_FW_POWER | PMAC_MB_MOBILE,
	},
#else /* CONFIG_PPC64 */
	{	"PowerMac7,2",			"PowerMac G5",
		PMAC_TYPE_POWERMAC_G5,		g5_features,
		0,
	},
#ifdef CONFIG_PPC64
	{	"PowerMac7,3",			"PowerMac G5",
		PMAC_TYPE_POWERMAC_G5,		g5_features,
		0,
	},
	{	"PowerMac8,1",			"iMac G5",
		PMAC_TYPE_IMAC_G5,		g5_features,
		0,
	},
	{	"PowerMac9,1",			"PowerMac G5",
		PMAC_TYPE_POWERMAC_G5_U3L,	g5_features,
		0,
	},
	{	"PowerMac11,2",			"PowerMac G5 Dual Core",
		PMAC_TYPE_POWERMAC_G5_U3L,	g5_features,
		0,
	},
	{	"PowerMac12,1",			"iMac G5 (iSight)",
		PMAC_TYPE_POWERMAC_G5_U3L,	g5_features,
		0,
	},
	{       "RackMac3,1",                   "XServe G5",
		PMAC_TYPE_XSERVE_G5,		g5_features,
		0,
	},
#endif /* CONFIG_PPC64 */
#endif /* CONFIG_PPC64 */
};

/*
 * The toplevel feature_call callback
 */
long pmac_do_feature_call(unsigned int selector, ...)
{
	struct device_node *node;
	long param, value;
	int i;
	feature_call func = NULL;
	va_list args;

	if (pmac_mb.features)
		for (i=0; pmac_mb.features[i].function; i++)
			if (pmac_mb.features[i].selector == selector) {
				func = pmac_mb.features[i].function;
				break;
			}
	if (!func)
		for (i=0; any_features[i].function; i++)
			if (any_features[i].selector == selector) {
				func = any_features[i].function;
				break;
			}
	if (!func)
		return -ENODEV;

	va_start(args, selector);
	node = (struct device_node*)va_arg(args, void*);
	param = va_arg(args, long);
	value = va_arg(args, long);
	va_end(args);

	return func(node, param, value);
}

static int __init probe_motherboard(void)
{
	int i;
	struct macio_chip *macio = &macio_chips[0];
	const char *model = NULL;
	struct device_node *dt;
	int ret = 0;

	/* Lookup known motherboard type in device-tree. First try an
	 * exact match on the "model" property, then try a "compatible"
	 * match is none is found.
	 */
	dt = of_find_node_by_name(NULL, "device-tree");
	if (dt != NULL)
		model = of_get_property(dt, "model", NULL);
	for(i=0; model && i<ARRAY_SIZE(pmac_mb_defs); i++) {
	    if (strcmp(model, pmac_mb_defs[i].model_string) == 0) {
		pmac_mb = pmac_mb_defs[i];
		goto found;
	    }
	}
	for(i=0; i<ARRAY_SIZE(pmac_mb_defs); i++) {
	    if (of_machine_is_compatible(pmac_mb_defs[i].model_string)) {
		pmac_mb = pmac_mb_defs[i];
		goto found;
	    }
	}

	/* Fallback to selection depending on mac-io chip type */
	switch(macio->type) {
#ifndef CONFIG_PPC64
	    case macio_grand_central:
		pmac_mb.model_id = PMAC_TYPE_PSURGE;
		pmac_mb.model_name = "Unknown PowerSurge";
		break;
	    case macio_ohare:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_OHARE;
		pmac_mb.model_name = "Unknown OHare-based";
		break;
	    case macio_heathrow:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_HEATHROW;
		pmac_mb.model_name = "Unknown Heathrow-based";
		pmac_mb.features = heathrow_desktop_features;
		break;
	    case macio_paddington:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_PADDINGTON;
		pmac_mb.model_name = "Unknown Paddington-based";
		pmac_mb.features = paddington_features;
		break;
	    case macio_keylargo:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_CORE99;
		pmac_mb.model_name = "Unknown Keylargo-based";
		pmac_mb.features = core99_features;
		break;
	    case macio_pangea:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_PANGEA;
		pmac_mb.model_name = "Unknown Pangea-based";
		pmac_mb.features = pangea_features;
		break;
	    case macio_intrepid:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_INTREPID;
		pmac_mb.model_name = "Unknown Intrepid-based";
		pmac_mb.features = intrepid_features;
		break;
#else /* CONFIG_PPC64 */
	case macio_keylargo2:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_K2;
		pmac_mb.model_name = "Unknown K2-based";
		pmac_mb.features = g5_features;
		break;
	case macio_shasta:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_SHASTA;
		pmac_mb.model_name = "Unknown Shasta-based";
		pmac_mb.features = g5_features;
		break;
#endif /* CONFIG_PPC64 */
	default:
		ret = -ENODEV;
		goto done;
	}
found:
#ifndef CONFIG_PPC64
	/* Fixup Hooper vs. Comet */
	if (pmac_mb.model_id == PMAC_TYPE_HOOPER) {
		u32 __iomem * mach_id_ptr = ioremap(0xf3000034, 4);
		if (!mach_id_ptr) {
			ret = -ENODEV;
			goto done;
		}
		/* Here, I used to disable the media-bay on comet. It
		 * appears this is wrong, the floppy connector is actually
		 * a kind of media-bay and works with the current driver.
		 */
		if (__raw_readl(mach_id_ptr) & 0x20000000UL)
			pmac_mb.model_id = PMAC_TYPE_COMET;
		iounmap(mach_id_ptr);
	}

	/* Set default value of powersave_nap on machines that support it.
	 * It appears that uninorth rev 3 has a problem with it, we don't
	 * enable it on those. In theory, the flush-on-lock property is
	 * supposed to be set when not supported, but I'm not very confident
	 * that all Apple OF revs did it properly, I do it the paranoid way.
	 */
	if (uninorth_base && uninorth_rev > 3) {
		struct device_node *np;

		for_each_of_cpu_node(np) {
			int cpu_count = 1;

			/* Nap mode not supported on SMP */
			if (of_property_read_bool(np, "flush-on-lock") ||
			    (cpu_count > 1)) {
				powersave_nap = 0;
				of_node_put(np);
				break;
			}

			cpu_count++;
			powersave_nap = 1;
		}
	}
	if (powersave_nap)
		printk(KERN_DEBUG "Processor NAP mode on idle enabled.\n");

	/* On CPUs that support it (750FX), lowspeed by default during
	 * NAP mode
	 */
	powersave_lowspeed = 1;

#else /* CONFIG_PPC64 */
	powersave_nap = 1;
#endif  /* CONFIG_PPC64 */

	/* Check for "mobile" machine */
	if (model && (strncmp(model, "PowerBook", 9) == 0
		   || strncmp(model, "iBook", 5) == 0))
		pmac_mb.board_flags |= PMAC_MB_MOBILE;


	printk(KERN_INFO "PowerMac motherboard: %s\n", pmac_mb.model_name);
done:
	of_node_put(dt);
	return ret;
}

/* Initialize the Core99 UniNorth host bridge and memory controller
 */
static void __init probe_uninorth(void)
{
	struct resource res;
	unsigned long actrl;

	/* Locate core99 Uni-N */
	uninorth_node = of_find_node_by_name(NULL, "uni-n");
	uninorth_maj = 1;

	/* Locate G5 u3 */
	if (uninorth_node == NULL) {
		uninorth_node = of_find_node_by_name(NULL, "u3");
		uninorth_maj = 3;
	}
	/* Locate G5 u4 */
	if (uninorth_node == NULL) {
		uninorth_node = of_find_node_by_name(NULL, "u4");
		uninorth_maj = 4;
	}
	if (uninorth_node == NULL) {
		uninorth_maj = 0;
		return;
	}

	if (of_address_to_resource(uninorth_node, 0, &res))
		return;

	uninorth_base = ioremap(res.start, 0x40000);
	if (uninorth_base == NULL)
		return;
	uninorth_rev = in_be32(UN_REG(UNI_N_VERSION));
	if (uninorth_maj == 3 || uninorth_maj == 4) {
		u3_ht_base = ioremap(res.start + U3_HT_CONFIG_BASE, 0x1000);
		if (u3_ht_base == NULL) {
			iounmap(uninorth_base);
			return;
		}
	}

	printk(KERN_INFO "Found %s memory controller & host bridge"
	       " @ 0x%08x revision: 0x%02x\n", uninorth_maj == 3 ? "U3" :
	       uninorth_maj == 4 ? "U4" : "UniNorth",
	       (unsigned int)res.start, uninorth_rev);
	printk(KERN_INFO "Mapped at 0x%08lx\n", (unsigned long)uninorth_base);

	/* Set the arbitrer QAck delay according to what Apple does
	 */
	if (uninorth_rev < 0x11) {
		actrl = UN_IN(UNI_N_ARB_CTRL) & ~UNI_N_ARB_CTRL_QACK_DELAY_MASK;
		actrl |= ((uninorth_rev < 3) ? UNI_N_ARB_CTRL_QACK_DELAY105 :
			UNI_N_ARB_CTRL_QACK_DELAY) <<
			UNI_N_ARB_CTRL_QACK_DELAY_SHIFT;
		UN_OUT(UNI_N_ARB_CTRL, actrl);
	}

	/* Some more magic as done by them in recent MacOS X on UniNorth
	 * revs 1.5 to 2.O and Pangea. Seem to toggle the UniN Maxbus/PCI
	 * memory timeout
	 */
	if ((uninorth_rev >= 0x11 && uninorth_rev <= 0x24) ||
	    uninorth_rev == 0xc0)
		UN_OUT(0x2160, UN_IN(0x2160) & 0x00ffffff);
}

static void __init probe_one_macio(const char *name, const char *compat, int type)
{
	struct device_node*	node;
	int			i;
	volatile u32 __iomem	*base;
	const __be32		*addrp;
	const u32		*revp;
	phys_addr_t		addr;
	u64			size;

	for_each_node_by_name(node, name) {
		if (!compat)
			break;
		if (of_device_is_compatible(node, compat))
			break;
	}
	if (!node)
		return;
	for(i=0; i<MAX_MACIO_CHIPS; i++) {
		if (!macio_chips[i].of_node)
			break;
		if (macio_chips[i].of_node == node)
			goto out_put;
	}

	if (i >= MAX_MACIO_CHIPS) {
		printk(KERN_ERR "pmac_feature: Please increase MAX_MACIO_CHIPS !\n");
		printk(KERN_ERR "pmac_feature: %pOF skipped\n", node);
		goto out_put;
	}
	addrp = of_get_pci_address(node, 0, &size, NULL);
	if (addrp == NULL) {
		printk(KERN_ERR "pmac_feature: %pOF: can't find base !\n",
		       node);
		goto out_put;
	}
	addr = of_translate_address(node, addrp);
	if (addr == 0) {
		printk(KERN_ERR "pmac_feature: %pOF, can't translate base !\n",
		       node);
		goto out_put;
	}
	base = ioremap(addr, (unsigned long)size);
	if (!base) {
		printk(KERN_ERR "pmac_feature: %pOF, can't map mac-io chip !\n",
		       node);
		goto out_put;
	}
	if (type == macio_keylargo || type == macio_keylargo2) {
		const u32 *did = of_get_property(node, "device-id", NULL);
		if (*did == 0x00000025)
			type = macio_pangea;
		if (*did == 0x0000003e)
			type = macio_intrepid;
		if (*did == 0x0000004f)
			type = macio_shasta;
	}
	macio_chips[i].of_node	= node;
	macio_chips[i].type	= type;
	macio_chips[i].base	= base;
	macio_chips[i].flags	= MACIO_FLAG_SCCA_ON | MACIO_FLAG_SCCB_ON;
	macio_chips[i].name	= macio_names[type];
	revp = of_get_property(node, "revision-id", NULL);
	if (revp)
		macio_chips[i].rev = *revp;
	printk(KERN_INFO "Found a %s mac-io controller, rev: %d, mapped at 0x%p\n",
		macio_names[type], macio_chips[i].rev, macio_chips[i].base);

	return;

out_put:
	of_node_put(node);
}

static int __init
probe_macios(void)
{
	/* Warning, ordering is important */
	probe_one_macio("gc", NULL, macio_grand_central);
	probe_one_macio("ohare", NULL, macio_ohare);
	probe_one_macio("pci106b,7", NULL, macio_ohareII);
	probe_one_macio("mac-io", "keylargo", macio_keylargo);
	probe_one_macio("mac-io", "paddington", macio_paddington);
	probe_one_macio("mac-io", "gatwick", macio_gatwick);
	probe_one_macio("mac-io", "heathrow", macio_heathrow);
	probe_one_macio("mac-io", "K2-Keylargo", macio_keylargo2);

	/* Make sure the "main" macio chip appear first */
	if (macio_chips[0].type == macio_gatwick
	    && macio_chips[1].type == macio_heathrow) {
		struct macio_chip temp = macio_chips[0];
		macio_chips[0] = macio_chips[1];
		macio_chips[1] = temp;
	}
	if (macio_chips[0].type == macio_ohareII
	    && macio_chips[1].type == macio_ohare) {
		struct macio_chip temp = macio_chips[0];
		macio_chips[0] = macio_chips[1];
		macio_chips[1] = temp;
	}
	macio_chips[0].lbus.index = 0;
	macio_chips[1].lbus.index = 1;

	return (macio_chips[0].of_node == NULL) ? -ENODEV : 0;
}

static void __init
initial_serial_shutdown(struct device_node *np)
{
	int len;
	const struct slot_names_prop {
		int	count;
		char	name[1];
	} *slots;
	const char *conn;
	int port_type = PMAC_SCC_ASYNC;
	int modem = 0;

	slots = of_get_property(np, "slot-names", &len);
	conn = of_get_property(np, "AAPL,connector", &len);
	if (conn && (strcmp(conn, "infrared") == 0))
		port_type = PMAC_SCC_IRDA;
	else if (of_device_is_compatible(np, "cobalt"))
		modem = 1;
	else if (slots && slots->count > 0) {
		if (strcmp(slots->name, "IrDA") == 0)
			port_type = PMAC_SCC_IRDA;
		else if (strcmp(slots->name, "Modem") == 0)
			modem = 1;
	}
	if (modem)
		pmac_call_feature(PMAC_FTR_MODEM_ENABLE, np, 0, 0);
	pmac_call_feature(PMAC_FTR_SCC_ENABLE, np, port_type, 0);
}

static void __init
set_initial_features(void)
{
	struct device_node *np;

	/* That hack appears to be necessary for some StarMax motherboards
	 * but I'm not too sure it was audited for side-effects on other
	 * ohare based machines...
	 * Since I still have difficulties figuring the right way to
	 * differentiate them all and since that hack was there for a long
	 * time, I'll keep it around
	 */
	if (macio_chips[0].type == macio_ohare) {
		struct macio_chip *macio = &macio_chips[0];
		np = of_find_node_by_name(NULL, "via-pmu");
		if (np)
			MACIO_BIS(OHARE_FCR, OH_IOBUS_ENABLE);
		else
			MACIO_OUT32(OHARE_FCR, STARMAX_FEATURES);
		of_node_put(np);
	} else if (macio_chips[1].type == macio_ohare) {
		struct macio_chip *macio = &macio_chips[1];
		MACIO_BIS(OHARE_FCR, OH_IOBUS_ENABLE);
	}

#ifdef CONFIG_PPC64
	if (macio_chips[0].type == macio_keylargo2 ||
	    macio_chips[0].type == macio_shasta) {
#ifndef CONFIG_SMP
		/* On SMP machines running UP, we have the second CPU eating
		 * bus cycles. We need to take it off the bus. This is done
		 * from pmac_smp for SMP kernels running on one CPU
		 */
		np = of_find_node_by_type(NULL, "cpu");
		if (np != NULL)
			np = of_find_node_by_type(np, "cpu");
		if (np != NULL) {
			g5_phy_disable_cpu1();
			of_node_put(np);
		}
#endif /* CONFIG_SMP */
		/* Enable GMAC for now for PCI probing. It will be disabled
		 * later on after PCI probe
		 */
		for_each_node_by_name(np, "ethernet")
			if (of_device_is_compatible(np, "K2-GMAC"))
				g5_gmac_enable(np, 0, 1);

		/* Enable FW before PCI probe. Will be disabled later on
		 * Note: We should have a batter way to check that we are
		 * dealing with uninorth internal cell and not a PCI cell
		 * on the external PCI. The code below works though.
		 */
		for_each_node_by_name(np, "firewire") {
			if (of_device_is_compatible(np, "pci106b,5811")) {
				macio_chips[0].flags |= MACIO_FLAG_FW_SUPPORTED;
				g5_fw_enable(np, 0, 1);
			}
		}
	}
#else /* CONFIG_PPC64 */

	if (macio_chips[0].type == macio_keylargo ||
	    macio_chips[0].type == macio_pangea ||
	    macio_chips[0].type == macio_intrepid) {
		/* Enable GMAC for now for PCI probing. It will be disabled
		 * later on after PCI probe
		 */
		for_each_node_by_name(np, "ethernet") {
			if (np->parent
			    && of_device_is_compatible(np->parent, "uni-north")
			    && of_device_is_compatible(np, "gmac"))
				core99_gmac_enable(np, 0, 1);
		}

		/* Enable FW before PCI probe. Will be disabled later on
		 * Note: We should have a batter way to check that we are
		 * dealing with uninorth internal cell and not a PCI cell
		 * on the external PCI. The code below works though.
		 */
		for_each_node_by_name(np, "firewire") {
			if (np->parent
			    && of_device_is_compatible(np->parent, "uni-north")
			    && (of_device_is_compatible(np, "pci106b,18") ||
			        of_device_is_compatible(np, "pci106b,30") ||
			        of_device_is_compatible(np, "pci11c1,5811"))) {
				macio_chips[0].flags |= MACIO_FLAG_FW_SUPPORTED;
				core99_firewire_enable(np, 0, 1);
			}
		}

		/* Enable ATA-100 before PCI probe. */
		for_each_node_by_name(np, "ata-6") {
			if (np->parent
			    && of_device_is_compatible(np->parent, "uni-north")
			    && of_device_is_compatible(np, "kauai-ata")) {
				core99_ata100_enable(np, 1);
			}
		}

		/* Switch airport off */
		for_each_node_by_name(np, "radio") {
			if (np->parent == macio_chips[0].of_node) {
				macio_chips[0].flags |= MACIO_FLAG_AIRPORT_ON;
				core99_airport_enable(np, 0, 0);
			}
		}
	}

	/* On all machines that support sound PM, switch sound off */
	if (macio_chips[0].of_node)
		pmac_do_feature_call(PMAC_FTR_SOUND_CHIP_ENABLE,
			macio_chips[0].of_node, 0, 0);

	/* While on some desktop G3s, we turn it back on */
	if (macio_chips[0].of_node && macio_chips[0].type == macio_heathrow
		&& (pmac_mb.model_id == PMAC_TYPE_GOSSAMER ||
		    pmac_mb.model_id == PMAC_TYPE_SILK)) {
		struct macio_chip *macio = &macio_chips[0];
		MACIO_BIS(HEATHROW_FCR, HRW_SOUND_CLK_ENABLE);
		MACIO_BIC(HEATHROW_FCR, HRW_SOUND_POWER_N);
	}

#endif /* CONFIG_PPC64 */

	/* On all machines, switch modem & serial ports off */
	for_each_node_by_name(np, "ch-a")
		initial_serial_shutdown(np);
	for_each_node_by_name(np, "ch-b")
		initial_serial_shutdown(np);
}

void __init
pmac_feature_init(void)
{
	/* Detect the UniNorth memory controller */
	probe_uninorth();

	/* Probe mac-io controllers */
	if (probe_macios()) {
		printk(KERN_WARNING "No mac-io chip found\n");
		return;
	}

	/* Probe machine type */
	if (probe_motherboard())
		printk(KERN_WARNING "Unknown PowerMac !\n");

	/* Set some initial features (turn off some chips that will
	 * be later turned on)
	 */
	set_initial_features();
}

#if 0
static void dump_HT_speeds(char *name, u32 cfg, u32 frq)
{
	int	freqs[16] = { 200,300,400,500,600,800,1000,0,0,0,0,0,0,0,0,0 };
	int	bits[8] = { 8,16,0,32,2,4,0,0 };
	int	freq = (frq >> 8) & 0xf;

	if (freqs[freq] == 0)
		printk("%s: Unknown HT link frequency %x\n", name, freq);
	else
		printk("%s: %d MHz on main link, (%d in / %d out) bits width\n",
		       name, freqs[freq],
		       bits[(cfg >> 28) & 0x7], bits[(cfg >> 24) & 0x7]);
}

void __init pmac_check_ht_link(void)
{
	u32	ufreq, freq, ucfg, cfg;
	struct device_node *pcix_node;
	u8	px_bus, px_devfn;
	struct pci_controller *px_hose;

	(void)in_be32(u3_ht_base + U3_HT_LINK_COMMAND);
	ucfg = cfg = in_be32(u3_ht_base + U3_HT_LINK_CONFIG);
	ufreq = freq = in_be32(u3_ht_base + U3_HT_LINK_FREQ);
	dump_HT_speeds("U3 HyperTransport", cfg, freq);

	pcix_node = of_find_compatible_node(NULL, "pci", "pci-x");
	if (pcix_node == NULL) {
		printk("No PCI-X bridge found\n");
		return;
	}
	if (pci_device_from_OF_node(pcix_node, &px_bus, &px_devfn) != 0) {
		printk("PCI-X bridge found but not matched to pci\n");
		return;
	}
	px_hose = pci_find_hose_for_OF_device(pcix_node);
	if (px_hose == NULL) {
		printk("PCI-X bridge found but not matched to host\n");
		return;
	}	
	early_read_config_dword(px_hose, px_bus, px_devfn, 0xc4, &cfg);
	early_read_config_dword(px_hose, px_bus, px_devfn, 0xcc, &freq);
	dump_HT_speeds("PCI-X HT Uplink", cfg, freq);
	early_read_config_dword(px_hose, px_bus, px_devfn, 0xc8, &cfg);
	early_read_config_dword(px_hose, px_bus, px_devfn, 0xd0, &freq);
	dump_HT_speeds("PCI-X HT Downlink", cfg, freq);
}
#endif /* 0 */

/*
 * Early video resume hook
 */

static void (*pmac_early_vresume_proc)(void *data);
static void *pmac_early_vresume_data;

void pmac_set_early_video_resume(void (*proc)(void *data), void *data)
{
	if (!machine_is(powermac))
		return;
	preempt_disable();
	pmac_early_vresume_proc = proc;
	pmac_early_vresume_data = data;
	preempt_enable();
}
EXPORT_SYMBOL(pmac_set_early_video_resume);

void pmac_call_early_video_resume(void)
{
	if (pmac_early_vresume_proc)
		pmac_early_vresume_proc(pmac_early_vresume_data);
}

/*
 * AGP related suspend/resume code
 */

static struct pci_dev *pmac_agp_bridge;
static int (*pmac_agp_suspend)(struct pci_dev *bridge);
static int (*pmac_agp_resume)(struct pci_dev *bridge);

void pmac_register_agp_pm(struct pci_dev *bridge,
				 int (*suspend)(struct pci_dev *bridge),
				 int (*resume)(struct pci_dev *bridge))
{
	if (suspend || resume) {
		pmac_agp_bridge = bridge;
		pmac_agp_suspend = suspend;
		pmac_agp_resume = resume;
		return;
	}
	if (bridge != pmac_agp_bridge)
		return;
	pmac_agp_suspend = pmac_agp_resume = NULL;
	return;
}
EXPORT_SYMBOL(pmac_register_agp_pm);

void pmac_suspend_agp_for_card(struct pci_dev *dev)
{
	if (pmac_agp_bridge == NULL || pmac_agp_suspend == NULL)
		return;
	if (pmac_agp_bridge->bus != dev->bus)
		return;
	pmac_agp_suspend(pmac_agp_bridge);
}
EXPORT_SYMBOL(pmac_suspend_agp_for_card);

void pmac_resume_agp_for_card(struct pci_dev *dev)
{
	if (pmac_agp_bridge == NULL || pmac_agp_resume == NULL)
		return;
	if (pmac_agp_bridge->bus != dev->bus)
		return;
	pmac_agp_resume(pmac_agp_bridge);
}
EXPORT_SYMBOL(pmac_resume_agp_for_card);

int pmac_get_uninorth_variant(void)
{
	return uninorth_maj;
}
