// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-mmp/pxa168.c
 *
 *  Code specific to PXA168
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk/mmp.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <asm/mach/time.h>
#include <asm/system_misc.h>

#include "addr-map.h"
#include "common.h"
#include <linux/soc/mmp/cputype.h>
#include <linux/soc/pxa/mfp.h>
#include "devices.h"
#include "pxa168.h"

#define MFPR_VIRT_BASE	(APB_VIRT_BASE + 0x1e000)

static struct mfp_addr_map pxa168_mfp_addr_map[] __initdata =
{
	MFP_ADDR_X(GPIO0,   GPIO36,  0x04c),
	MFP_ADDR_X(GPIO37,  GPIO55,  0x000),
	MFP_ADDR_X(GPIO56,  GPIO123, 0x0e0),
	MFP_ADDR_X(GPIO124, GPIO127, 0x0f4),

	MFP_ADDR_END,
};

static int __init pxa168_init(void)
{
	if (cpu_is_pxa168()) {
		mfp_init_base(MFPR_VIRT_BASE);
		mfp_init_addr(pxa168_mfp_addr_map);
		pxa168_clk_init(APB_PHYS_BASE + 0x50000,
				AXI_PHYS_BASE + 0x82800,
				APB_PHYS_BASE + 0x15000);
	}

	return 0;
}
postcore_initcall(pxa168_init);
