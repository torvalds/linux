/*
 * arch/arm/mach-dove/mpp.c
 *
 * MPP functions for Marvell Dove SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <mach/dove.h>

#include "mpp.h"

#define MPP_NR_REGS 4
#define MPP_CTRL(i)	((i) == 3 ?				\
			 DOVE_MPP_CTRL4_VIRT_BASE :		\
			 DOVE_MPP_VIRT_BASE + (i) * 4)
#define PMU_SIG_REGS 2
#define PMU_SIG_CTRL(i)	(DOVE_PMU_SIG_CTRL + (i) * 4)

struct dove_mpp_grp {
	int start;
	int end;
};

static struct dove_mpp_grp dove_mpp_grp[] = {
	[MPP_24_39] = {
		.start	= 24,
		.end	= 39,
	},
	[MPP_40_45] = {
		.start	= 40,
		.end	= 45,
	},
	[MPP_46_51] = {
		.start	= 40,
		.end	= 45,
	},
	[MPP_58_61] = {
		.start	= 58,
		.end	= 61,
	},
	[MPP_62_63] = {
		.start	= 62,
		.end	= 63,
	},
};

static void dove_mpp_gpio_mode(int start, int end, int gpio_mode)
{
	int i;

	for (i = start; i <= end; i++)
		orion_gpio_set_valid(i, gpio_mode);
}

static void dove_mpp_dump_regs(void)
{
#ifdef DEBUG
	int i;

	pr_debug("MPP_CTRL regs:");
	for (i = 0; i < MPP_NR_REGS; i++)
		printk(" %08x", readl(MPP_CTRL(i)));
	printk("\n");

	pr_debug("PMU_SIG_CTRL regs:");
	for (i = 0; i < PMU_SIG_REGS; i++)
		printk(" %08x", readl(PMU_SIG_CTRL(i)));
	printk("\n");

	pr_debug("PMU_MPP_GENERAL_CTRL: %08x\n", readl(DOVE_PMU_MPP_GENERAL_CTRL));
	pr_debug("MPP_GENERAL: %08x\n", readl(DOVE_MPP_GENERAL_VIRT_BASE));
#endif
}

static void dove_mpp_cfg_nfc(int sel)
{
	u32 mpp_gen_cfg = readl(DOVE_MPP_GENERAL_VIRT_BASE);

	mpp_gen_cfg &= ~0x1;
	mpp_gen_cfg |= sel;
	writel(mpp_gen_cfg, DOVE_MPP_GENERAL_VIRT_BASE);

	dove_mpp_gpio_mode(64, 71, GPIO_OUTPUT_OK);
}

static void dove_mpp_cfg_au1(int sel)
{
	u32 mpp_ctrl4		= readl(DOVE_MPP_CTRL4_VIRT_BASE);
	u32 ssp_ctrl1 = readl(DOVE_SSP_CTRL_STATUS_1);
	u32 mpp_gen_ctrl = readl(DOVE_MPP_GENERAL_VIRT_BASE);
	u32 global_cfg_2 = readl(DOVE_GLOBAL_CONFIG_2);

	mpp_ctrl4 &= ~(DOVE_AU1_GPIO_SEL);
	ssp_ctrl1 &= ~(DOVE_SSP_ON_AU1);
	mpp_gen_ctrl &= ~(DOVE_AU1_SPDIFO_GPIO_EN);
	global_cfg_2 &= ~(DOVE_TWSI_OPTION3_GPIO);

	if (!sel || sel == 0x2)
		dove_mpp_gpio_mode(52, 57, 0);
	else
		dove_mpp_gpio_mode(52, 57, GPIO_OUTPUT_OK | GPIO_INPUT_OK);

	if (sel & 0x1) {
		global_cfg_2 |= DOVE_TWSI_OPTION3_GPIO;
		dove_mpp_gpio_mode(56, 57, 0);
	}
	if (sel & 0x2) {
		mpp_gen_ctrl |= DOVE_AU1_SPDIFO_GPIO_EN;
		dove_mpp_gpio_mode(57, 57, GPIO_OUTPUT_OK | GPIO_INPUT_OK);
	}
	if (sel & 0x4) {
		ssp_ctrl1 |= DOVE_SSP_ON_AU1;
		dove_mpp_gpio_mode(52, 55, 0);
	}
	if (sel & 0x8)
		mpp_ctrl4 |= DOVE_AU1_GPIO_SEL;

	writel(mpp_ctrl4, DOVE_MPP_CTRL4_VIRT_BASE);
	writel(ssp_ctrl1, DOVE_SSP_CTRL_STATUS_1);
	writel(mpp_gen_ctrl, DOVE_MPP_GENERAL_VIRT_BASE);
	writel(global_cfg_2, DOVE_GLOBAL_CONFIG_2);
}

static void dove_mpp_conf_grp(int num, int sel, u32 *mpp_ctrl)
{
	int start = dove_mpp_grp[num].start;
	int end = dove_mpp_grp[num].end;
	int gpio_mode = sel ? GPIO_OUTPUT_OK | GPIO_INPUT_OK : 0;

	*mpp_ctrl &= ~(0x1 << num);
	*mpp_ctrl |= sel << num;

	dove_mpp_gpio_mode(start, end, gpio_mode);
}

void __init dove_mpp_conf(unsigned int *mpp_list)
{
	u32 mpp_ctrl[MPP_NR_REGS];
	u32 pmu_mpp_ctrl = 0;
	u32 pmu_sig_ctrl[PMU_SIG_REGS];
	int i;

	for (i = 0; i < MPP_NR_REGS; i++)
		mpp_ctrl[i] = readl(MPP_CTRL(i));

	for (i = 0; i < PMU_SIG_REGS; i++)
		pmu_sig_ctrl[i] = readl(PMU_SIG_CTRL(i));

	pmu_mpp_ctrl = readl(DOVE_PMU_MPP_GENERAL_CTRL);

	dove_mpp_dump_regs();

	for ( ; *mpp_list != MPP_END; mpp_list++) {
		unsigned int num = MPP_NUM(*mpp_list);
		unsigned int sel = MPP_SEL(*mpp_list);
		int shift, gpio_mode;

		if (num > MPP_MAX) {
			pr_err("dove: invalid MPP number (%u)\n", num);
			continue;
		}

		if (*mpp_list & MPP_NFC_MASK) {
			dove_mpp_cfg_nfc(sel);
			continue;
		}

		if (*mpp_list & MPP_AU1_MASK) {
			dove_mpp_cfg_au1(sel);
			continue;
		}

		if (*mpp_list & MPP_GRP_MASK) {
			dove_mpp_conf_grp(num, sel, &mpp_ctrl[3]);
			continue;
		}

		shift = (num & 7) << 2;
		if (*mpp_list & MPP_PMU_MASK) {
			pmu_mpp_ctrl |= (0x1 << num);
			pmu_sig_ctrl[num / 8] &= ~(0xf << shift);
			pmu_sig_ctrl[num / 8] |= 0xf << shift;
			gpio_mode = 0;
		} else {
			mpp_ctrl[num / 8] &= ~(0xf << shift);
			mpp_ctrl[num / 8] |= sel << shift;
			gpio_mode = GPIO_OUTPUT_OK | GPIO_INPUT_OK;
		}

		orion_gpio_set_valid(num, gpio_mode);
	}

	for (i = 0; i < MPP_NR_REGS; i++)
		writel(mpp_ctrl[i], MPP_CTRL(i));

	for (i = 0; i < PMU_SIG_REGS; i++)
		writel(pmu_sig_ctrl[i], PMU_SIG_CTRL(i));

	writel(pmu_mpp_ctrl, DOVE_PMU_MPP_GENERAL_CTRL);

	dove_mpp_dump_regs();
}
