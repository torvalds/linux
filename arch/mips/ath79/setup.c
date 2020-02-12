// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Atheros AR71XX/AR724X/AR913X specific setup
 *
 *  Copyright (C) 2010-2011 Jaiganesh Narayanan <jnarayanan@atheros.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15/2.6.31 BSP
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/of_clk.h>
#include <linux/of_fdt.h>
#include <linux/irqchip.h>

#include <asm/bootinfo.h>
#include <asm/idle.h>
#include <asm/time.h>		/* for mips_hpt_frequency */
#include <asm/reboot.h>		/* for _machine_{restart,halt} */
#include <asm/mips_machine.h>
#include <asm/prom.h>
#include <asm/fw/fw.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "common.h"

#define ATH79_SYS_TYPE_LEN	64

static char ath79_sys_type[ATH79_SYS_TYPE_LEN];

static void ath79_restart(char *command)
{
	local_irq_disable();
	ath79_device_reset_set(AR71XX_RESET_FULL_CHIP);
	for (;;)
		if (cpu_wait)
			cpu_wait();
}

static void ath79_halt(void)
{
	while (1)
		cpu_wait();
}

static void __init ath79_detect_sys_type(void)
{
	char *chip = "????";
	u32 id;
	u32 major;
	u32 minor;
	u32 rev = 0;
	u32 ver = 1;

	id = ath79_reset_rr(AR71XX_RESET_REG_REV_ID);
	major = id & REV_ID_MAJOR_MASK;

	switch (major) {
	case REV_ID_MAJOR_AR71XX:
		minor = id & AR71XX_REV_ID_MINOR_MASK;
		rev = id >> AR71XX_REV_ID_REVISION_SHIFT;
		rev &= AR71XX_REV_ID_REVISION_MASK;
		switch (minor) {
		case AR71XX_REV_ID_MINOR_AR7130:
			ath79_soc = ATH79_SOC_AR7130;
			chip = "7130";
			break;

		case AR71XX_REV_ID_MINOR_AR7141:
			ath79_soc = ATH79_SOC_AR7141;
			chip = "7141";
			break;

		case AR71XX_REV_ID_MINOR_AR7161:
			ath79_soc = ATH79_SOC_AR7161;
			chip = "7161";
			break;
		}
		break;

	case REV_ID_MAJOR_AR7240:
		ath79_soc = ATH79_SOC_AR7240;
		chip = "7240";
		rev = id & AR724X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_AR7241:
		ath79_soc = ATH79_SOC_AR7241;
		chip = "7241";
		rev = id & AR724X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_AR7242:
		ath79_soc = ATH79_SOC_AR7242;
		chip = "7242";
		rev = id & AR724X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_AR913X:
		minor = id & AR913X_REV_ID_MINOR_MASK;
		rev = id >> AR913X_REV_ID_REVISION_SHIFT;
		rev &= AR913X_REV_ID_REVISION_MASK;
		switch (minor) {
		case AR913X_REV_ID_MINOR_AR9130:
			ath79_soc = ATH79_SOC_AR9130;
			chip = "9130";
			break;

		case AR913X_REV_ID_MINOR_AR9132:
			ath79_soc = ATH79_SOC_AR9132;
			chip = "9132";
			break;
		}
		break;

	case REV_ID_MAJOR_AR9330:
		ath79_soc = ATH79_SOC_AR9330;
		chip = "9330";
		rev = id & AR933X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_AR9331:
		ath79_soc = ATH79_SOC_AR9331;
		chip = "9331";
		rev = id & AR933X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_AR9341:
		ath79_soc = ATH79_SOC_AR9341;
		chip = "9341";
		rev = id & AR934X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_AR9342:
		ath79_soc = ATH79_SOC_AR9342;
		chip = "9342";
		rev = id & AR934X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_AR9344:
		ath79_soc = ATH79_SOC_AR9344;
		chip = "9344";
		rev = id & AR934X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_QCA9533_V2:
		ver = 2;
		ath79_soc_rev = 2;
		/* fall through */

	case REV_ID_MAJOR_QCA9533:
		ath79_soc = ATH79_SOC_QCA9533;
		chip = "9533";
		rev = id & QCA953X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_QCA9556:
		ath79_soc = ATH79_SOC_QCA9556;
		chip = "9556";
		rev = id & QCA955X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_QCA9558:
		ath79_soc = ATH79_SOC_QCA9558;
		chip = "9558";
		rev = id & QCA955X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_QCA956X:
		ath79_soc = ATH79_SOC_QCA956X;
		chip = "956X";
		rev = id & QCA956X_REV_ID_REVISION_MASK;
		break;

	case REV_ID_MAJOR_TP9343:
		ath79_soc = ATH79_SOC_TP9343;
		chip = "9343";
		rev = id & QCA956X_REV_ID_REVISION_MASK;
		break;

	default:
		panic("ath79: unknown SoC, id:0x%08x", id);
	}

	if (ver == 1)
		ath79_soc_rev = rev;

	if (soc_is_qca953x() || soc_is_qca955x() || soc_is_qca956x())
		sprintf(ath79_sys_type, "Qualcomm Atheros QCA%s ver %u rev %u",
			chip, ver, rev);
	else if (soc_is_tp9343())
		sprintf(ath79_sys_type, "Qualcomm Atheros TP%s rev %u",
			chip, rev);
	else
		sprintf(ath79_sys_type, "Atheros AR%s rev %u", chip, rev);
	pr_info("SoC: %s\n", ath79_sys_type);
}

const char *get_system_type(void)
{
	return ath79_sys_type;
}

unsigned int get_c0_compare_int(void)
{
	return CP0_LEGACY_COMPARE_IRQ;
}

void __init plat_mem_setup(void)
{
	unsigned long fdt_start;

	set_io_port_base(KSEG1);

	/* Get the position of the FDT passed by the bootloader */
	fdt_start = fw_getenvl("fdt_start");
	if (fdt_start)
		__dt_setup_arch((void *)KSEG0ADDR(fdt_start));
	else if (fw_passed_dtb)
		__dt_setup_arch((void *)KSEG0ADDR(fw_passed_dtb));

	ath79_reset_base = ioremap(AR71XX_RESET_BASE,
					   AR71XX_RESET_SIZE);
	ath79_pll_base = ioremap(AR71XX_PLL_BASE,
					 AR71XX_PLL_SIZE);
	ath79_detect_sys_type();
	ath79_ddr_ctrl_init();

	detect_memory_region(0, ATH79_MEM_SIZE_MIN, ATH79_MEM_SIZE_MAX);

	_machine_restart = ath79_restart;
	_machine_halt = ath79_halt;
	pm_power_off = ath79_halt;
}

void __init plat_time_init(void)
{
	struct device_node *np;
	struct clk *clk;
	unsigned long cpu_clk_rate;

	of_clk_init(NULL);

	np = of_get_cpu_node(0, NULL);
	if (!np) {
		pr_err("Failed to get CPU node\n");
		return;
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("Failed to get CPU clock: %ld\n", PTR_ERR(clk));
		return;
	}

	cpu_clk_rate = clk_get_rate(clk);

	pr_info("CPU clock: %lu.%03lu MHz\n",
		cpu_clk_rate / 1000000, (cpu_clk_rate / 1000) % 1000);

	mips_hpt_frequency = cpu_clk_rate / 2;

	clk_put(clk);
}

void __init arch_init_irq(void)
{
	irqchip_init();
}

void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();
}
