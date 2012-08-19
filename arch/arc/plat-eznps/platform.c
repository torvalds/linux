/*
 * Copyright(c) 2015 EZchip Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 */

#include <linux/init.h>
#include <linux/io.h>
#include <asm/mach_desc.h>
#include <plat/mtm.h>

static void __init eznps_configure_msu(void)
{
	int cpu;
	struct nps_host_reg_msu_en_cfg msu_en_cfg = {.value = 0};

	msu_en_cfg.msu_en = 1;
	msu_en_cfg.ipi_en = 1;
	msu_en_cfg.gim_0_en = 1;
	msu_en_cfg.gim_1_en = 1;

	/* enable IPI and GIM messages on all clusters */
	for (cpu = 0 ; cpu < eznps_max_cpus; cpu += eznps_cpus_per_cluster)
		iowrite32be(msu_en_cfg.value,
			    nps_host_reg(cpu, NPS_MSU_BLKID, NPS_MSU_EN_CFG));
}

static void __init eznps_configure_gim(void)
{
	u32 reg_value;
	u32 gim_int_lines;
	struct nps_host_reg_gim_p_int_dst gim_p_int_dst = {.value = 0};

	gim_int_lines = NPS_GIM_UART_LINE;
	gim_int_lines |= NPS_GIM_DBG_LAN_EAST_TX_DONE_LINE;
	gim_int_lines |= NPS_GIM_DBG_LAN_EAST_RX_RDY_LINE;
	gim_int_lines |= NPS_GIM_DBG_LAN_WEST_TX_DONE_LINE;
	gim_int_lines |= NPS_GIM_DBG_LAN_WEST_RX_RDY_LINE;

	/*
	 * IRQ polarity
	 * low or high level
	 * negative or positive edge
	 */
	reg_value = ioread32be(REG_GIM_P_INT_POL_0);
	reg_value &= ~gim_int_lines;
	iowrite32be(reg_value, REG_GIM_P_INT_POL_0);

	/* IRQ type level or edge */
	reg_value = ioread32be(REG_GIM_P_INT_SENS_0);
	reg_value |= NPS_GIM_DBG_LAN_EAST_TX_DONE_LINE;
	reg_value |= NPS_GIM_DBG_LAN_WEST_TX_DONE_LINE;
	iowrite32be(reg_value, REG_GIM_P_INT_SENS_0);

	/*
	 * GIM interrupt select type for
	 * dbg_lan TX and RX interrupts
	 * should be type 1
	 * type 0 = IRQ line 6
	 * type 1 = IRQ line 7
	 */
	gim_p_int_dst.is = 1;
	iowrite32be(gim_p_int_dst.value, REG_GIM_P_INT_DST_10);
	iowrite32be(gim_p_int_dst.value, REG_GIM_P_INT_DST_11);
	iowrite32be(gim_p_int_dst.value, REG_GIM_P_INT_DST_25);
	iowrite32be(gim_p_int_dst.value, REG_GIM_P_INT_DST_26);

	/*
	 * CTOP IRQ lines should be defined
	 * as blocking in GIM
	*/
	iowrite32be(gim_int_lines, REG_GIM_P_INT_BLK_0);

	/* enable CTOP IRQ lines in GIM */
	iowrite32be(gim_int_lines, REG_GIM_P_INT_EN_0);
}

static void __init eznps_early_init(void)
{
	eznps_configure_msu();
	eznps_configure_gim();
}

static const char *eznps_compat[] __initconst = {
	"ezchip,arc-nps",
	NULL,
};

MACHINE_START(NPS, "nps")
	.dt_compat	= eznps_compat,
	.init_early	= eznps_early_init,
MACHINE_END
