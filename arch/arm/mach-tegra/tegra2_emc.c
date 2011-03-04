/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>

#include <mach/iomap.h>

#include "tegra2_emc.h"

#define TEGRA_MRR_DIVLD        (1<<20)
#define TEGRA_EMC_STATUS       0x02b4
#define TEGRA_EMC_MRR          0x00ec
static DEFINE_MUTEX(tegra_emc_mrr_lock);

#ifdef CONFIG_TEGRA_EMC_SCALING_ENABLE
static bool emc_enable = true;
#else
static bool emc_enable;
#endif
module_param(emc_enable, bool, 0644);

static void __iomem *emc = IO_ADDRESS(TEGRA_EMC_BASE);
static const struct tegra_emc_table *tegra_emc_table;
static int tegra_emc_table_size;

static inline void emc_writel(u32 val, unsigned long addr)
{
	writel(val, emc + addr);
}

static inline u32 emc_readl(unsigned long addr)
{
	return readl(emc + addr);
}

/* read LPDDR2 memory modes */
static int tegra_emc_read_mrr(unsigned long addr)
{
	u32 value;
	int count = 100;

	mutex_lock(&tegra_emc_mrr_lock);
	do {
		emc_readl(TEGRA_EMC_MRR);
	} while (--count && (emc_readl(TEGRA_EMC_STATUS) & TEGRA_MRR_DIVLD));
	if (count == 0) {
		pr_err("%s: Failed to read memory type\n", __func__);
		BUG();
	}
	value = (1 << 30) | (addr << 16);
	emc_writel(value, TEGRA_EMC_MRR);

	count = 100;
	while (--count && !(emc_readl(TEGRA_EMC_STATUS) & TEGRA_MRR_DIVLD));
	if (count == 0) {
		pr_err("%s: Failed to read memory type\n", __func__);
		BUG();
	}
	value = emc_readl(TEGRA_EMC_MRR) & 0xFFFF;
	mutex_unlock(&tegra_emc_mrr_lock);

	return value;
}

static const unsigned long emc_reg_addr[TEGRA_EMC_NUM_REGS] = {
	0x2c,	/* RC */
	0x30,	/* RFC */
	0x34,	/* RAS */
	0x38,	/* RP */
	0x3c,	/* R2W */
	0x40,	/* W2R */
	0x44,	/* R2P */
	0x48,	/* W2P */
	0x4c,	/* RD_RCD */
	0x50,	/* WR_RCD */
	0x54,	/* RRD */
	0x58,	/* REXT */
	0x5c,	/* WDV */
	0x60,	/* QUSE */
	0x64,	/* QRST */
	0x68,	/* QSAFE */
	0x6c,	/* RDV */
	0x70,	/* REFRESH */
	0x74,	/* BURST_REFRESH_NUM */
	0x78,	/* PDEX2WR */
	0x7c,	/* PDEX2RD */
	0x80,	/* PCHG2PDEN */
	0x84,	/* ACT2PDEN */
	0x88,	/* AR2PDEN */
	0x8c,	/* RW2PDEN */
	0x90,	/* TXSR */
	0x94,	/* TCKE */
	0x98,	/* TFAW */
	0x9c,	/* TRPAB */
	0xa0,	/* TCLKSTABLE */
	0xa4,	/* TCLKSTOP */
	0xa8,	/* TREFBW */
	0xac,	/* QUSE_EXTRA */
	0x114,	/* FBIO_CFG6 */
	0xb0,	/* ODT_WRITE */
	0xb4,	/* ODT_READ */
	0x104,	/* FBIO_CFG5 */
	0x2bc,	/* CFG_DIG_DLL */
	0x2c0,	/* DLL_XFORM_DQS */
	0x2c4,	/* DLL_XFORM_QUSE */
	0x2e0,	/* ZCAL_REF_CNT */
	0x2e4,	/* ZCAL_WAIT_CNT */
	0x2a8,	/* AUTO_CAL_INTERVAL */
	0x2d0,	/* CFG_CLKTRIM_0 */
	0x2d4,	/* CFG_CLKTRIM_1 */
	0x2d8,	/* CFG_CLKTRIM_2 */
};

/* Select the closest EMC rate that is higher than the requested rate */
long tegra_emc_round_rate(unsigned long rate)
{
	int i;
	int best = -1;
	unsigned long distance = ULONG_MAX;

	if (!tegra_emc_table)
		return -EINVAL;

	if (!emc_enable)
		return -EINVAL;

	pr_debug("%s: %lu\n", __func__, rate);

	/* The EMC clock rate is twice the bus rate, and the bus rate is
	 * measured in kHz */
	rate = rate / 2 / 1000;

	for (i = 0; i < tegra_emc_table_size; i++) {
		if (tegra_emc_table[i].rate >= rate &&
		    (tegra_emc_table[i].rate - rate) < distance) {
			distance = tegra_emc_table[i].rate - rate;
			best = i;
		}
	}

	if (best < 0)
		return -EINVAL;

	pr_debug("%s: using %lu\n", __func__, tegra_emc_table[best].rate);

	return tegra_emc_table[best].rate * 2 * 1000;
}

/* The EMC registers have shadow registers.  When the EMC clock is updated
 * in the clock controller, the shadow registers are copied to the active
 * registers, allowing glitchless memory bus frequency changes.
 * This function updates the shadow registers for a new clock frequency,
 * and relies on the clock lock on the emc clock to avoid races between
 * multiple frequency changes */
int tegra_emc_set_rate(unsigned long rate)
{
	int i;
	int j;

	if (!tegra_emc_table)
		return -EINVAL;

	/* The EMC clock rate is twice the bus rate, and the bus rate is
	 * measured in kHz */
	rate = rate / 2 / 1000;

	for (i = 0; i < tegra_emc_table_size; i++)
		if (tegra_emc_table[i].rate == rate)
			break;

	if (i >= tegra_emc_table_size)
		return -EINVAL;

	pr_debug("%s: setting to %lu\n", __func__, rate);

	for (j = 0; j < TEGRA_EMC_NUM_REGS; j++)
		emc_writel(tegra_emc_table[i].regs[j], emc_reg_addr[j]);

	emc_readl(tegra_emc_table[i].regs[TEGRA_EMC_NUM_REGS - 1]);

	return 0;
}

void tegra_init_emc(const struct tegra_emc_chip *chips, int chips_size)
{
	int i;
	int vid;
	int rev_id1;
	int rev_id2;
	int pid;
	int chip_matched = -1;

	vid = tegra_emc_read_mrr(5);
	rev_id1 = tegra_emc_read_mrr(6);
	rev_id2 = tegra_emc_read_mrr(7);
	pid = tegra_emc_read_mrr(8);

	for (i = 0; i < chips_size; i++) {
		if (chips[i].mem_manufacturer_id >= 0) {
			if (chips[i].mem_manufacturer_id != vid)
				continue;
		}
		if (chips[i].mem_revision_id1 >= 0) {
			if (chips[i].mem_revision_id1 != rev_id1)
				continue;
		}
		if (chips[i].mem_revision_id2 >= 0) {
			if (chips[i].mem_revision_id2 != rev_id2)
				continue;
		}
		if (chips[i].mem_pid >= 0) {
			if (chips[i].mem_pid != pid)
				continue;
		}

		chip_matched = i;
		break;
	}

	if (chip_matched >= 0) {
		pr_info("%s: %s memory found\n", __func__,
			chips[chip_matched].description);
		tegra_emc_table = chips[chip_matched].table;
		tegra_emc_table_size = chips[chip_matched].table_size;
	} else {
		pr_err("%s: Memory not recognized, memory scaling disabled\n",
			__func__);
		pr_info("%s: Memory vid     = 0x%04x", __func__, vid);
		pr_info("%s: Memory rev_id1 = 0x%04x", __func__, rev_id1);
		pr_info("%s: Memory rev_id2 = 0x%04x", __func__, rev_id2);
		pr_info("%s: Memory pid     = 0x%04x", __func__, pid);
	}
}
