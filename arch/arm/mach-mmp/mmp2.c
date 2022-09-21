// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-mmp/mmp2.c
 *
 * code name MMP2
 *
 * Copyright (C) 2009 Marvell International Ltd.
 */
#include <linux/clk/mmp.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/mmp.h>
#include <linux/platform_device.h>

#include <asm/hardware/cache-tauros2.h>

#include <asm/mach/time.h>
#include "addr-map.h"
#include <linux/soc/mmp/cputype.h>
#include <linux/soc/pxa/mfp.h>
#include "mmp2.h"
#include "pm-mmp2.h"

#include "common.h"

#define MFPR_VIRT_BASE	(APB_VIRT_BASE + 0x1e000)

static struct mfp_addr_map mmp2_addr_map[] __initdata = {

	MFP_ADDR_X(GPIO0, GPIO58, 0x54),
	MFP_ADDR_X(GPIO59, GPIO73, 0x280),
	MFP_ADDR_X(GPIO74, GPIO101, 0x170),

	MFP_ADDR(GPIO102, 0x0),
	MFP_ADDR(GPIO103, 0x4),
	MFP_ADDR(GPIO104, 0x1fc),
	MFP_ADDR(GPIO105, 0x1f8),
	MFP_ADDR(GPIO106, 0x1f4),
	MFP_ADDR(GPIO107, 0x1f0),
	MFP_ADDR(GPIO108, 0x21c),
	MFP_ADDR(GPIO109, 0x218),
	MFP_ADDR(GPIO110, 0x214),
	MFP_ADDR(GPIO111, 0x200),
	MFP_ADDR(GPIO112, 0x244),
	MFP_ADDR(GPIO113, 0x25c),
	MFP_ADDR(GPIO114, 0x164),
	MFP_ADDR_X(GPIO115, GPIO122, 0x260),

	MFP_ADDR(GPIO123, 0x148),
	MFP_ADDR_X(GPIO124, GPIO141, 0xc),

	MFP_ADDR(GPIO142, 0x8),
	MFP_ADDR_X(GPIO143, GPIO151, 0x220),
	MFP_ADDR_X(GPIO152, GPIO153, 0x248),
	MFP_ADDR_X(GPIO154, GPIO155, 0x254),
	MFP_ADDR_X(GPIO156, GPIO159, 0x14c),

	MFP_ADDR(GPIO160, 0x250),
	MFP_ADDR(GPIO161, 0x210),
	MFP_ADDR(GPIO162, 0x20c),
	MFP_ADDR(GPIO163, 0x208),
	MFP_ADDR(GPIO164, 0x204),
	MFP_ADDR(GPIO165, 0x1ec),
	MFP_ADDR(GPIO166, 0x1e8),
	MFP_ADDR(GPIO167, 0x1e4),
	MFP_ADDR(GPIO168, 0x1e0),

	MFP_ADDR_X(TWSI1_SCL, TWSI1_SDA, 0x140),
	MFP_ADDR_X(TWSI4_SCL, TWSI4_SDA, 0x2bc),

	MFP_ADDR(PMIC_INT, 0x2c4),
	MFP_ADDR(CLK_REQ, 0x160),

	MFP_ADDR_END,
};

void mmp2_clear_pmic_int(void)
{
	void __iomem *mfpr_pmic;
	unsigned long data;

	mfpr_pmic = APB_VIRT_BASE + 0x1e000 + 0x2c4;
	data = __raw_readl(mfpr_pmic);
	__raw_writel(data | (1 << 6), mfpr_pmic);
	__raw_writel(data, mfpr_pmic);
}

static int __init mmp2_init(void)
{
	if (cpu_is_mmp2()) {
#ifdef CONFIG_CACHE_TAUROS2
		tauros2_init(0);
#endif
		mfp_init_base(MFPR_VIRT_BASE);
		mfp_init_addr(mmp2_addr_map);
		mmp2_clk_init(APB_PHYS_BASE + 0x50000,
			      AXI_PHYS_BASE + 0x82800,
			      APB_PHYS_BASE + 0x15000);
	}

	return 0;
}
postcore_initcall(mmp2_init);
