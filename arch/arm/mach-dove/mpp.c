// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-dove/mpp.c
 *
 * MPP functions for Marvell Dove SoCs
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <plat/mpp.h>
#include <plat/orion-gpio.h>
#include "dove.h"
#include "mpp.h"

struct dove_mpp_grp {
	int start;
	int end;
};

/* Map a group to a range of GPIO pins in that group */
static const struct dove_mpp_grp dove_mpp_grp[] = {
	[MPP_24_39] = {
		.start	= 24,
		.end	= 39,
	},
	[MPP_40_45] = {
		.start	= 40,
		.end	= 45,
	},
	[MPP_46_51] = {
		.start	= 46,
		.end	= 51,
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

/* Enable gpio for a range of pins. mode should be a combination of
   GPIO_OUTPUT_OK | GPIO_INPUT_OK */
static void __init dove_mpp_gpio_mode(int start, int end, int gpio_mode)
{
	int i;

	for (i = start; i <= end; i++)
		orion_gpio_set_valid(i, gpio_mode);
}

/* Dump all the extra MPP registers. The platform code will dump the
   registers for pins 0-23. */
static void __init dove_mpp_dump_regs(void)
{
	pr_debug("PMU_CTRL4_CTRL: %08x\n",
		 readl(DOVE_MPP_CTRL4_VIRT_BASE));

	pr_debug("PMU_MPP_GENERAL_CTRL: %08x\n",
		 readl(DOVE_PMU_MPP_GENERAL_CTRL));

	pr_debug("MPP_GENERAL: %08x\n", readl(DOVE_MPP_GENERAL_VIRT_BASE));
}

static void __init dove_mpp_cfg_nfc(int sel)
{
	u32 mpp_gen_cfg = readl(DOVE_MPP_GENERAL_VIRT_BASE);

	mpp_gen_cfg &= ~0x1;
	mpp_gen_cfg |= sel;
	writel(mpp_gen_cfg, DOVE_MPP_GENERAL_VIRT_BASE);

	dove_mpp_gpio_mode(64, 71, GPIO_OUTPUT_OK);
}

static void __init dove_mpp_cfg_au1(int sel)
{
	u32 mpp_ctrl4 = readl(DOVE_MPP_CTRL4_VIRT_BASE);
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

/* Configure the group registers, enabling GPIO if sel indicates the
   pin is to be used for GPIO */
static void __init dove_mpp_conf_grp(unsigned int *mpp_grp_list)
{
	u32 mpp_ctrl4 = readl(DOVE_MPP_CTRL4_VIRT_BASE);
	int gpio_mode;

	for ( ; *mpp_grp_list; mpp_grp_list++) {
		unsigned int num = MPP_NUM(*mpp_grp_list);
		unsigned int sel = MPP_SEL(*mpp_grp_list);

		if (num > MPP_GRP_MAX) {
			pr_err("dove: invalid MPP GRP number (%u)\n", num);
			continue;
		}

		mpp_ctrl4 &= ~(0x1 << num);
		mpp_ctrl4 |= sel << num;

		gpio_mode = sel ? GPIO_OUTPUT_OK | GPIO_INPUT_OK : 0;
		dove_mpp_gpio_mode(dove_mpp_grp[num].start,
				   dove_mpp_grp[num].end, gpio_mode);
	}
	writel(mpp_ctrl4, DOVE_MPP_CTRL4_VIRT_BASE);
}

/* Configure the various MPP pins on Dove */
void __init dove_mpp_conf(unsigned int *mpp_list,
			  unsigned int *mpp_grp_list,
			  unsigned int grp_au1_52_57,
			  unsigned int grp_nfc_64_71)
{
	dove_mpp_dump_regs();

	/* Use platform code for pins 0-23 */
	orion_mpp_conf(mpp_list, 0, MPP_MAX, DOVE_MPP_VIRT_BASE);

	dove_mpp_conf_grp(mpp_grp_list);
	dove_mpp_cfg_au1(grp_au1_52_57);
	dove_mpp_cfg_nfc(grp_nfc_64_71);

	dove_mpp_dump_regs();
}
