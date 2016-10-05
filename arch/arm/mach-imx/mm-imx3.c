/*
 *  Copyright (C) 1999,2000 Arm Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *    - add MX31 specific definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pinctrl/machine.h>

#include <asm/pgtable.h>
#include <asm/system_misc.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>

#include "common.h"
#include "crmregs-imx3.h"
#include "devices/devices-common.h"
#include "hardware.h"
#include "iomux-v3.h"

void __iomem *mx3_ccm_base;

static void imx3_idle(void)
{
	unsigned long reg = 0;

	__asm__ __volatile__(
		/* disable I and D cache */
		"mrc p15, 0, %0, c1, c0, 0\n"
		"bic %0, %0, #0x00001000\n"
		"bic %0, %0, #0x00000004\n"
		"mcr p15, 0, %0, c1, c0, 0\n"
		/* invalidate I cache */
		"mov %0, #0\n"
		"mcr p15, 0, %0, c7, c5, 0\n"
		/* clear and invalidate D cache */
		"mov %0, #0\n"
		"mcr p15, 0, %0, c7, c14, 0\n"
		/* WFI */
		"mov %0, #0\n"
		"mcr p15, 0, %0, c7, c0, 4\n"
		"nop\n" "nop\n" "nop\n" "nop\n"
		"nop\n" "nop\n" "nop\n"
		/* enable I and D cache */
		"mrc p15, 0, %0, c1, c0, 0\n"
		"orr %0, %0, #0x00001000\n"
		"orr %0, %0, #0x00000004\n"
		"mcr p15, 0, %0, c1, c0, 0\n"
		: "=r" (reg));
}

static void __iomem *imx3_ioremap_caller(phys_addr_t phys_addr, size_t size,
					 unsigned int mtype, void *caller)
{
	if (mtype == MT_DEVICE) {
		/*
		 * Access all peripherals below 0x80000000 as nonshared device
		 * on mx3, but leave l2cc alone.  Otherwise cache corruptions
		 * can occur.
		 */
		if (phys_addr < 0x80000000 &&
				!addr_in_module(phys_addr, MX3x_L2CC))
			mtype = MT_DEVICE_NONSHARED;
	}

	return __arm_ioremap_caller(phys_addr, size, mtype, caller);
}

static void __init imx3_init_l2x0(void)
{
#ifdef CONFIG_CACHE_L2X0
	void __iomem *l2x0_base;
	void __iomem *clkctl_base;

/*
 * First of all, we must repair broken chip settings. There are some
 * i.MX35 CPUs in the wild, comming with bogus L2 cache settings. These
 * misconfigured CPUs will run amok immediately when the L2 cache gets enabled.
 * Workaraound is to setup the correct register setting prior enabling the
 * L2 cache. This should not hurt already working CPUs, as they are using the
 * same value.
 */
#define L2_MEM_VAL 0x10

	clkctl_base = ioremap(MX35_CLKCTL_BASE_ADDR, 4096);
	if (clkctl_base != NULL) {
		writel(0x00000515, clkctl_base + L2_MEM_VAL);
		iounmap(clkctl_base);
	} else {
		pr_err("L2 cache: Cannot fix timing. Trying to continue without\n");
	}

	l2x0_base = ioremap(MX3x_L2CC_BASE_ADDR, 4096);
	if (!l2x0_base) {
		printk(KERN_ERR "remapping L2 cache area failed\n");
		return;
	}

	l2x0_init(l2x0_base, 0x00030024, 0x00000000);
#endif
}

#ifdef CONFIG_SOC_IMX31
static struct map_desc mx31_io_desc[] __initdata = {
	imx_map_entry(MX31, X_MEMC, MT_DEVICE),
	imx_map_entry(MX31, AVIC, MT_DEVICE_NONSHARED),
	imx_map_entry(MX31, AIPS1, MT_DEVICE_NONSHARED),
	imx_map_entry(MX31, AIPS2, MT_DEVICE_NONSHARED),
	imx_map_entry(MX31, SPBA0, MT_DEVICE_NONSHARED),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx31_map_io(void)
{
	iotable_init(mx31_io_desc, ARRAY_SIZE(mx31_io_desc));
}

static void imx31_idle(void)
{
	int reg = imx_readl(mx3_ccm_base + MXC_CCM_CCMR);
	reg &= ~MXC_CCM_CCMR_LPM_MASK;
	imx_writel(reg, mx3_ccm_base + MXC_CCM_CCMR);

	imx3_idle();
}

void __init imx31_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX31);
	arch_ioremap_caller = imx3_ioremap_caller;
	arm_pm_idle = imx31_idle;
	mx3_ccm_base = MX31_IO_ADDRESS(MX31_CCM_BASE_ADDR);
}

void __init mx31_init_irq(void)
{
	mxc_init_irq(MX31_IO_ADDRESS(MX31_AVIC_BASE_ADDR));
}

static struct sdma_script_start_addrs imx31_to1_sdma_script __initdata = {
	.per_2_per_addr = 1677,
};

static struct sdma_script_start_addrs imx31_to2_sdma_script __initdata = {
	.ap_2_ap_addr = 423,
	.ap_2_bp_addr = 829,
	.bp_2_ap_addr = 1029,
};

static struct sdma_platform_data imx31_sdma_pdata __initdata = {
	.fw_name = "sdma-imx31-to2.bin",
	.script_addrs = &imx31_to2_sdma_script,
};

static const struct resource imx31_audmux_res[] __initconst = {
	DEFINE_RES_MEM(MX31_AUDMUX_BASE_ADDR, SZ_16K),
};

static const struct resource imx31_rnga_res[] __initconst = {
	DEFINE_RES_MEM(MX31_RNGA_BASE_ADDR, SZ_16K),
};

void __init imx31_soc_init(void)
{
	int to_version = mx31_revision() >> 4;

	imx3_init_l2x0();

	mxc_arch_reset_init(MX31_IO_ADDRESS(MX31_WDOG_BASE_ADDR));
	mxc_device_init();

	mxc_register_gpio("imx31-gpio", 0, MX31_GPIO1_BASE_ADDR, SZ_16K, MX31_INT_GPIO1, 0);
	mxc_register_gpio("imx31-gpio", 1, MX31_GPIO2_BASE_ADDR, SZ_16K, MX31_INT_GPIO2, 0);
	mxc_register_gpio("imx31-gpio", 2, MX31_GPIO3_BASE_ADDR, SZ_16K, MX31_INT_GPIO3, 0);

	pinctrl_provide_dummies();

	if (to_version == 1) {
		strncpy(imx31_sdma_pdata.fw_name, "sdma-imx31-to1.bin",
			strlen(imx31_sdma_pdata.fw_name));
		imx31_sdma_pdata.script_addrs = &imx31_to1_sdma_script;
	}

	imx_add_imx_sdma("imx31-sdma", MX31_SDMA_BASE_ADDR, MX31_INT_SDMA, &imx31_sdma_pdata);

	imx_set_aips(MX31_IO_ADDRESS(MX31_AIPS1_BASE_ADDR));
	imx_set_aips(MX31_IO_ADDRESS(MX31_AIPS2_BASE_ADDR));

	platform_device_register_simple("imx31-audmux", 0, imx31_audmux_res,
					ARRAY_SIZE(imx31_audmux_res));
	platform_device_register_simple("mxc_rnga", -1, imx31_rnga_res,
					ARRAY_SIZE(imx31_rnga_res));
}
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
static struct map_desc mx35_io_desc[] __initdata = {
	imx_map_entry(MX35, X_MEMC, MT_DEVICE),
	imx_map_entry(MX35, AVIC, MT_DEVICE_NONSHARED),
	imx_map_entry(MX35, AIPS1, MT_DEVICE_NONSHARED),
	imx_map_entry(MX35, AIPS2, MT_DEVICE_NONSHARED),
	imx_map_entry(MX35, SPBA0, MT_DEVICE_NONSHARED),
};

void __init mx35_map_io(void)
{
	iotable_init(mx35_io_desc, ARRAY_SIZE(mx35_io_desc));
}

static void imx35_idle(void)
{
	int reg = imx_readl(mx3_ccm_base + MXC_CCM_CCMR);
	reg &= ~MXC_CCM_CCMR_LPM_MASK;
	reg |= MXC_CCM_CCMR_LPM_WAIT_MX35;
	imx_writel(reg, mx3_ccm_base + MXC_CCM_CCMR);

	imx3_idle();
}

void __init imx35_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX35);
	mxc_iomux_v3_init(MX35_IO_ADDRESS(MX35_IOMUXC_BASE_ADDR));
	arm_pm_idle = imx35_idle;
	arch_ioremap_caller = imx3_ioremap_caller;
	mx3_ccm_base = MX35_IO_ADDRESS(MX35_CCM_BASE_ADDR);
}

void __init mx35_init_irq(void)
{
	mxc_init_irq(MX35_IO_ADDRESS(MX35_AVIC_BASE_ADDR));
}

static struct sdma_script_start_addrs imx35_to1_sdma_script __initdata = {
	.ap_2_ap_addr = 642,
	.uart_2_mcu_addr = 817,
	.mcu_2_app_addr = 747,
	.uartsh_2_mcu_addr = 1183,
	.per_2_shp_addr = 1033,
	.mcu_2_shp_addr = 961,
	.ata_2_mcu_addr = 1333,
	.mcu_2_ata_addr = 1252,
	.app_2_mcu_addr = 683,
	.shp_2_per_addr = 1111,
	.shp_2_mcu_addr = 892,
};

static struct sdma_script_start_addrs imx35_to2_sdma_script __initdata = {
	.ap_2_ap_addr = 729,
	.uart_2_mcu_addr = 904,
	.per_2_app_addr = 1597,
	.mcu_2_app_addr = 834,
	.uartsh_2_mcu_addr = 1270,
	.per_2_shp_addr = 1120,
	.mcu_2_shp_addr = 1048,
	.ata_2_mcu_addr = 1429,
	.mcu_2_ata_addr = 1339,
	.app_2_per_addr = 1531,
	.app_2_mcu_addr = 770,
	.shp_2_per_addr = 1198,
	.shp_2_mcu_addr = 979,
};

static struct sdma_platform_data imx35_sdma_pdata __initdata = {
	.fw_name = "sdma-imx35-to2.bin",
	.script_addrs = &imx35_to2_sdma_script,
};

static const struct resource imx35_audmux_res[] __initconst = {
	DEFINE_RES_MEM(MX35_AUDMUX_BASE_ADDR, SZ_16K),
};

void __init imx35_soc_init(void)
{
	int to_version = mx35_revision() >> 4;

	imx3_init_l2x0();

	mxc_arch_reset_init(MX35_IO_ADDRESS(MX35_WDOG_BASE_ADDR));
	mxc_device_init();

	mxc_register_gpio("imx35-gpio", 0, MX35_GPIO1_BASE_ADDR, SZ_16K, MX35_INT_GPIO1, 0);
	mxc_register_gpio("imx35-gpio", 1, MX35_GPIO2_BASE_ADDR, SZ_16K, MX35_INT_GPIO2, 0);
	mxc_register_gpio("imx35-gpio", 2, MX35_GPIO3_BASE_ADDR, SZ_16K, MX35_INT_GPIO3, 0);

	pinctrl_provide_dummies();
	if (to_version == 1) {
		strncpy(imx35_sdma_pdata.fw_name, "sdma-imx35-to1.bin",
			strlen(imx35_sdma_pdata.fw_name));
		imx35_sdma_pdata.script_addrs = &imx35_to1_sdma_script;
	}

	imx_add_imx_sdma("imx35-sdma", MX35_SDMA_BASE_ADDR, MX35_INT_SDMA, &imx35_sdma_pdata);

	/* Setup AIPS registers */
	imx_set_aips(MX35_IO_ADDRESS(MX35_AIPS1_BASE_ADDR));
	imx_set_aips(MX35_IO_ADDRESS(MX35_AIPS2_BASE_ADDR));

	/* i.mx35 has the i.mx31 type audmux */
	platform_device_register_simple("imx31-audmux", 0, imx35_audmux_res,
					ARRAY_SIZE(imx35_audmux_res));
}
#endif /* ifdef CONFIG_SOC_IMX35 */
