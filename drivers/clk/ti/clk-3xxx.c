/*
 * OMAP3 Clock init
 *
 * Copyright (C) 2013 Texas Instruments, Inc
 *     Tero Kristo (t-kristo@ti.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>

#include "clock.h"

#define OMAP3430ES2_ST_DSS_IDLE_SHIFT			1
#define OMAP3430ES2_ST_HSOTGUSB_IDLE_SHIFT		5
#define OMAP3430ES2_ST_SSI_IDLE_SHIFT			8

#define OMAP34XX_CM_IDLEST_VAL				1

/*
 * In AM35xx IPSS, the {ICK,FCK} enable bits for modules are exported
 * in the same register at a bit offset of 0x8. The EN_ACK for ICK is
 * at an offset of 4 from ICK enable bit.
 */
#define AM35XX_IPSS_ICK_MASK			0xF
#define AM35XX_IPSS_ICK_EN_ACK_OFFSET		0x4
#define AM35XX_IPSS_ICK_FCK_OFFSET		0x8
#define AM35XX_IPSS_CLK_IDLEST_VAL		0

#define AM35XX_ST_IPSS_SHIFT			5

/**
 * omap3430es2_clk_ssi_find_idlest - return CM_IDLEST info for SSI
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The OMAP3430ES2 SSI target CM_IDLEST bit is at a different shift
 * from the CM_{I,F}CLKEN bit.  Pass back the correct info via
 * @idlest_reg and @idlest_bit.  No return value.
 */
static void omap3430es2_clk_ssi_find_idlest(struct clk_hw_omap *clk,
					    struct clk_omap_reg *idlest_reg,
					    u8 *idlest_bit,
					    u8 *idlest_val)
{
	memcpy(idlest_reg, &clk->enable_reg, sizeof(*idlest_reg));
	idlest_reg->offset &= ~0xf0;
	idlest_reg->offset |= 0x20;
	*idlest_bit = OMAP3430ES2_ST_SSI_IDLE_SHIFT;
	*idlest_val = OMAP34XX_CM_IDLEST_VAL;
}

const struct clk_hw_omap_ops clkhwops_omap3430es2_iclk_ssi_wait = {
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
	.find_idlest	= omap3430es2_clk_ssi_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};

/**
 * omap3430es2_clk_dss_usbhost_find_idlest - CM_IDLEST info for DSS, USBHOST
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * Some OMAP modules on OMAP3 ES2+ chips have both initiator and
 * target IDLEST bits.  For our purposes, we are concerned with the
 * target IDLEST bits, which exist at a different bit position than
 * the *CLKEN bit position for these modules (DSS and USBHOST) (The
 * default find_idlest code assumes that they are at the same
 * position.)  No return value.
 */
static void
omap3430es2_clk_dss_usbhost_find_idlest(struct clk_hw_omap *clk,
					struct clk_omap_reg *idlest_reg,
					u8 *idlest_bit, u8 *idlest_val)
{
	memcpy(idlest_reg, &clk->enable_reg, sizeof(*idlest_reg));

	idlest_reg->offset &= ~0xf0;
	idlest_reg->offset |= 0x20;
	/* USBHOST_IDLE has same shift */
	*idlest_bit = OMAP3430ES2_ST_DSS_IDLE_SHIFT;
	*idlest_val = OMAP34XX_CM_IDLEST_VAL;
}

const struct clk_hw_omap_ops clkhwops_omap3430es2_dss_usbhost_wait = {
	.find_idlest	= omap3430es2_clk_dss_usbhost_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};

const struct clk_hw_omap_ops clkhwops_omap3430es2_iclk_dss_usbhost_wait = {
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
	.find_idlest	= omap3430es2_clk_dss_usbhost_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};

/**
 * omap3430es2_clk_hsotgusb_find_idlest - return CM_IDLEST info for HSOTGUSB
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The OMAP3430ES2 HSOTGUSB target CM_IDLEST bit is at a different
 * shift from the CM_{I,F}CLKEN bit.  Pass back the correct info via
 * @idlest_reg and @idlest_bit.  No return value.
 */
static void
omap3430es2_clk_hsotgusb_find_idlest(struct clk_hw_omap *clk,
				     struct clk_omap_reg *idlest_reg,
				     u8 *idlest_bit,
				     u8 *idlest_val)
{
	memcpy(idlest_reg, &clk->enable_reg, sizeof(*idlest_reg));
	idlest_reg->offset &= ~0xf0;
	idlest_reg->offset |= 0x20;
	*idlest_bit = OMAP3430ES2_ST_HSOTGUSB_IDLE_SHIFT;
	*idlest_val = OMAP34XX_CM_IDLEST_VAL;
}

const struct clk_hw_omap_ops clkhwops_omap3430es2_iclk_hsotgusb_wait = {
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
	.find_idlest	= omap3430es2_clk_hsotgusb_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};

/**
 * am35xx_clk_find_idlest - return clock ACK info for AM35XX IPSS
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The interface clocks on AM35xx IPSS reflects the clock idle status
 * in the enable register itsel at a bit offset of 4 from the enable
 * bit. A value of 1 indicates that clock is enabled.
 */
static void am35xx_clk_find_idlest(struct clk_hw_omap *clk,
				   struct clk_omap_reg *idlest_reg,
				   u8 *idlest_bit,
				   u8 *idlest_val)
{
	memcpy(idlest_reg, &clk->enable_reg, sizeof(*idlest_reg));
	*idlest_bit = clk->enable_bit + AM35XX_IPSS_ICK_EN_ACK_OFFSET;
	*idlest_val = AM35XX_IPSS_CLK_IDLEST_VAL;
}

/**
 * am35xx_clk_find_companion - find companion clock to @clk
 * @clk: struct clk * to find the companion clock of
 * @other_reg: void __iomem ** to return the companion clock CM_*CLKEN va in
 * @other_bit: u8 ** to return the companion clock bit shift in
 *
 * Some clocks don't have companion clocks.  For example, modules with
 * only an interface clock (such as HECC) don't have a companion
 * clock.  Right now, this code relies on the hardware exporting a bit
 * in the correct companion register that indicates that the
 * nonexistent 'companion clock' is active.  Future patches will
 * associate this type of code with per-module data structures to
 * avoid this issue, and remove the casts.  No return value.
 */
static void am35xx_clk_find_companion(struct clk_hw_omap *clk,
				      struct clk_omap_reg *other_reg,
				      u8 *other_bit)
{
	memcpy(other_reg, &clk->enable_reg, sizeof(*other_reg));
	if (clk->enable_bit & AM35XX_IPSS_ICK_MASK)
		*other_bit = clk->enable_bit + AM35XX_IPSS_ICK_FCK_OFFSET;
	else
	*other_bit = clk->enable_bit - AM35XX_IPSS_ICK_FCK_OFFSET;
}

const struct clk_hw_omap_ops clkhwops_am35xx_ipss_module_wait = {
	.find_idlest	= am35xx_clk_find_idlest,
	.find_companion	= am35xx_clk_find_companion,
};

/**
 * am35xx_clk_ipss_find_idlest - return CM_IDLEST info for IPSS
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The IPSS target CM_IDLEST bit is at a different shift from the
 * CM_{I,F}CLKEN bit.  Pass back the correct info via @idlest_reg
 * and @idlest_bit.  No return value.
 */
static void am35xx_clk_ipss_find_idlest(struct clk_hw_omap *clk,
					struct clk_omap_reg *idlest_reg,
					u8 *idlest_bit,
					u8 *idlest_val)
{
	memcpy(idlest_reg, &clk->enable_reg, sizeof(*idlest_reg));

	idlest_reg->offset &= ~0xf0;
	idlest_reg->offset |= 0x20;
	*idlest_bit = AM35XX_ST_IPSS_SHIFT;
	*idlest_val = OMAP34XX_CM_IDLEST_VAL;
}

const struct clk_hw_omap_ops clkhwops_am35xx_ipss_wait = {
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
	.find_idlest	= am35xx_clk_ipss_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};

static struct ti_dt_clk omap3xxx_clks[] = {
	DT_CLK(NULL, "apb_pclk", "dummy_apb_pclk"),
	DT_CLK(NULL, "omap_32k_fck", "omap_32k_fck"),
	DT_CLK(NULL, "virt_12m_ck", "virt_12m_ck"),
	DT_CLK(NULL, "virt_13m_ck", "virt_13m_ck"),
	DT_CLK(NULL, "virt_19200000_ck", "virt_19200000_ck"),
	DT_CLK(NULL, "virt_26000000_ck", "virt_26000000_ck"),
	DT_CLK(NULL, "virt_38_4m_ck", "virt_38_4m_ck"),
	DT_CLK(NULL, "osc_sys_ck", "osc_sys_ck"),
	DT_CLK("twl", "fck", "osc_sys_ck"),
	DT_CLK(NULL, "sys_ck", "sys_ck"),
	DT_CLK(NULL, "omap_96m_alwon_fck", "omap_96m_alwon_fck"),
	DT_CLK("etb", "emu_core_alwon_ck", "emu_core_alwon_ck"),
	DT_CLK(NULL, "sys_altclk", "sys_altclk"),
	DT_CLK(NULL, "sys_clkout1", "sys_clkout1"),
	DT_CLK(NULL, "dpll1_ck", "dpll1_ck"),
	DT_CLK(NULL, "dpll1_x2_ck", "dpll1_x2_ck"),
	DT_CLK(NULL, "dpll1_x2m2_ck", "dpll1_x2m2_ck"),
	DT_CLK(NULL, "dpll3_ck", "dpll3_ck"),
	DT_CLK(NULL, "core_ck", "core_ck"),
	DT_CLK(NULL, "dpll3_x2_ck", "dpll3_x2_ck"),
	DT_CLK(NULL, "dpll3_m2_ck", "dpll3_m2_ck"),
	DT_CLK(NULL, "dpll3_m2x2_ck", "dpll3_m2x2_ck"),
	DT_CLK(NULL, "dpll3_m3_ck", "dpll3_m3_ck"),
	DT_CLK(NULL, "dpll3_m3x2_ck", "dpll3_m3x2_ck"),
	DT_CLK(NULL, "dpll4_ck", "dpll4_ck"),
	DT_CLK(NULL, "dpll4_x2_ck", "dpll4_x2_ck"),
	DT_CLK(NULL, "omap_96m_fck", "omap_96m_fck"),
	DT_CLK(NULL, "cm_96m_fck", "cm_96m_fck"),
	DT_CLK(NULL, "omap_54m_fck", "omap_54m_fck"),
	DT_CLK(NULL, "omap_48m_fck", "omap_48m_fck"),
	DT_CLK(NULL, "omap_12m_fck", "omap_12m_fck"),
	DT_CLK(NULL, "dpll4_m2_ck", "dpll4_m2_ck"),
	DT_CLK(NULL, "dpll4_m2x2_ck", "dpll4_m2x2_ck"),
	DT_CLK(NULL, "dpll4_m3_ck", "dpll4_m3_ck"),
	DT_CLK(NULL, "dpll4_m3x2_ck", "dpll4_m3x2_ck"),
	DT_CLK(NULL, "dpll4_m4_ck", "dpll4_m4_ck"),
	DT_CLK(NULL, "dpll4_m4x2_ck", "dpll4_m4x2_ck"),
	DT_CLK(NULL, "dpll4_m5_ck", "dpll4_m5_ck"),
	DT_CLK(NULL, "dpll4_m5x2_ck", "dpll4_m5x2_ck"),
	DT_CLK(NULL, "dpll4_m6_ck", "dpll4_m6_ck"),
	DT_CLK(NULL, "dpll4_m6x2_ck", "dpll4_m6x2_ck"),
	DT_CLK("etb", "emu_per_alwon_ck", "emu_per_alwon_ck"),
	DT_CLK(NULL, "clkout2_src_ck", "clkout2_src_ck"),
	DT_CLK(NULL, "sys_clkout2", "sys_clkout2"),
	DT_CLK(NULL, "corex2_fck", "corex2_fck"),
	DT_CLK(NULL, "dpll1_fck", "dpll1_fck"),
	DT_CLK(NULL, "mpu_ck", "mpu_ck"),
	DT_CLK(NULL, "arm_fck", "arm_fck"),
	DT_CLK("etb", "emu_mpu_alwon_ck", "emu_mpu_alwon_ck"),
	DT_CLK(NULL, "l3_ick", "l3_ick"),
	DT_CLK(NULL, "l4_ick", "l4_ick"),
	DT_CLK(NULL, "rm_ick", "rm_ick"),
	DT_CLK(NULL, "gpt10_fck", "gpt10_fck"),
	DT_CLK(NULL, "gpt11_fck", "gpt11_fck"),
	DT_CLK(NULL, "core_96m_fck", "core_96m_fck"),
	DT_CLK(NULL, "mmchs2_fck", "mmchs2_fck"),
	DT_CLK(NULL, "mmchs1_fck", "mmchs1_fck"),
	DT_CLK(NULL, "i2c3_fck", "i2c3_fck"),
	DT_CLK(NULL, "i2c2_fck", "i2c2_fck"),
	DT_CLK(NULL, "i2c1_fck", "i2c1_fck"),
	DT_CLK(NULL, "core_48m_fck", "core_48m_fck"),
	DT_CLK(NULL, "mcspi4_fck", "mcspi4_fck"),
	DT_CLK(NULL, "mcspi3_fck", "mcspi3_fck"),
	DT_CLK(NULL, "mcspi2_fck", "mcspi2_fck"),
	DT_CLK(NULL, "mcspi1_fck", "mcspi1_fck"),
	DT_CLK(NULL, "uart2_fck", "uart2_fck"),
	DT_CLK(NULL, "uart1_fck", "uart1_fck"),
	DT_CLK(NULL, "core_12m_fck", "core_12m_fck"),
	DT_CLK("omap_hdq.0", "fck", "hdq_fck"),
	DT_CLK(NULL, "hdq_fck", "hdq_fck"),
	DT_CLK(NULL, "core_l3_ick", "core_l3_ick"),
	DT_CLK(NULL, "sdrc_ick", "sdrc_ick"),
	DT_CLK(NULL, "gpmc_fck", "gpmc_fck"),
	DT_CLK(NULL, "core_l4_ick", "core_l4_ick"),
	DT_CLK("omap_hsmmc.1", "ick", "mmchs2_ick"),
	DT_CLK("omap_hsmmc.0", "ick", "mmchs1_ick"),
	DT_CLK(NULL, "mmchs2_ick", "mmchs2_ick"),
	DT_CLK(NULL, "mmchs1_ick", "mmchs1_ick"),
	DT_CLK("omap_hdq.0", "ick", "hdq_ick"),
	DT_CLK(NULL, "hdq_ick", "hdq_ick"),
	DT_CLK("omap2_mcspi.4", "ick", "mcspi4_ick"),
	DT_CLK("omap2_mcspi.3", "ick", "mcspi3_ick"),
	DT_CLK("omap2_mcspi.2", "ick", "mcspi2_ick"),
	DT_CLK("omap2_mcspi.1", "ick", "mcspi1_ick"),
	DT_CLK(NULL, "mcspi4_ick", "mcspi4_ick"),
	DT_CLK(NULL, "mcspi3_ick", "mcspi3_ick"),
	DT_CLK(NULL, "mcspi2_ick", "mcspi2_ick"),
	DT_CLK(NULL, "mcspi1_ick", "mcspi1_ick"),
	DT_CLK("omap_i2c.3", "ick", "i2c3_ick"),
	DT_CLK("omap_i2c.2", "ick", "i2c2_ick"),
	DT_CLK("omap_i2c.1", "ick", "i2c1_ick"),
	DT_CLK(NULL, "i2c3_ick", "i2c3_ick"),
	DT_CLK(NULL, "i2c2_ick", "i2c2_ick"),
	DT_CLK(NULL, "i2c1_ick", "i2c1_ick"),
	DT_CLK(NULL, "uart2_ick", "uart2_ick"),
	DT_CLK(NULL, "uart1_ick", "uart1_ick"),
	DT_CLK(NULL, "gpt11_ick", "gpt11_ick"),
	DT_CLK(NULL, "gpt10_ick", "gpt10_ick"),
	DT_CLK(NULL, "omapctrl_ick", "omapctrl_ick"),
	DT_CLK(NULL, "dss_tv_fck", "dss_tv_fck"),
	DT_CLK(NULL, "dss_96m_fck", "dss_96m_fck"),
	DT_CLK(NULL, "dss2_alwon_fck", "dss2_alwon_fck"),
	DT_CLK(NULL, "init_60m_fclk", "dummy_ck"),
	DT_CLK(NULL, "gpt1_fck", "gpt1_fck"),
	DT_CLK(NULL, "aes2_ick", "aes2_ick"),
	DT_CLK(NULL, "wkup_32k_fck", "wkup_32k_fck"),
	DT_CLK(NULL, "gpio1_dbck", "gpio1_dbck"),
	DT_CLK(NULL, "sha12_ick", "sha12_ick"),
	DT_CLK(NULL, "wdt2_fck", "wdt2_fck"),
	DT_CLK("omap_wdt", "ick", "wdt2_ick"),
	DT_CLK(NULL, "wdt2_ick", "wdt2_ick"),
	DT_CLK(NULL, "wdt1_ick", "wdt1_ick"),
	DT_CLK(NULL, "gpio1_ick", "gpio1_ick"),
	DT_CLK(NULL, "omap_32ksync_ick", "omap_32ksync_ick"),
	DT_CLK(NULL, "gpt12_ick", "gpt12_ick"),
	DT_CLK(NULL, "gpt1_ick", "gpt1_ick"),
	DT_CLK(NULL, "per_96m_fck", "per_96m_fck"),
	DT_CLK(NULL, "per_48m_fck", "per_48m_fck"),
	DT_CLK(NULL, "uart3_fck", "uart3_fck"),
	DT_CLK(NULL, "gpt2_fck", "gpt2_fck"),
	DT_CLK(NULL, "gpt3_fck", "gpt3_fck"),
	DT_CLK(NULL, "gpt4_fck", "gpt4_fck"),
	DT_CLK(NULL, "gpt5_fck", "gpt5_fck"),
	DT_CLK(NULL, "gpt6_fck", "gpt6_fck"),
	DT_CLK(NULL, "gpt7_fck", "gpt7_fck"),
	DT_CLK(NULL, "gpt8_fck", "gpt8_fck"),
	DT_CLK(NULL, "gpt9_fck", "gpt9_fck"),
	DT_CLK(NULL, "per_32k_alwon_fck", "per_32k_alwon_fck"),
	DT_CLK(NULL, "gpio6_dbck", "gpio6_dbck"),
	DT_CLK(NULL, "gpio5_dbck", "gpio5_dbck"),
	DT_CLK(NULL, "gpio4_dbck", "gpio4_dbck"),
	DT_CLK(NULL, "gpio3_dbck", "gpio3_dbck"),
	DT_CLK(NULL, "gpio2_dbck", "gpio2_dbck"),
	DT_CLK(NULL, "wdt3_fck", "wdt3_fck"),
	DT_CLK(NULL, "per_l4_ick", "per_l4_ick"),
	DT_CLK(NULL, "gpio6_ick", "gpio6_ick"),
	DT_CLK(NULL, "gpio5_ick", "gpio5_ick"),
	DT_CLK(NULL, "gpio4_ick", "gpio4_ick"),
	DT_CLK(NULL, "gpio3_ick", "gpio3_ick"),
	DT_CLK(NULL, "gpio2_ick", "gpio2_ick"),
	DT_CLK(NULL, "wdt3_ick", "wdt3_ick"),
	DT_CLK(NULL, "uart3_ick", "uart3_ick"),
	DT_CLK(NULL, "gpt9_ick", "gpt9_ick"),
	DT_CLK(NULL, "gpt8_ick", "gpt8_ick"),
	DT_CLK(NULL, "gpt7_ick", "gpt7_ick"),
	DT_CLK(NULL, "gpt6_ick", "gpt6_ick"),
	DT_CLK(NULL, "gpt5_ick", "gpt5_ick"),
	DT_CLK(NULL, "gpt4_ick", "gpt4_ick"),
	DT_CLK(NULL, "gpt3_ick", "gpt3_ick"),
	DT_CLK(NULL, "gpt2_ick", "gpt2_ick"),
	DT_CLK(NULL, "mcbsp_clks", "mcbsp_clks"),
	DT_CLK(NULL, "mcbsp1_ick", "mcbsp1_ick"),
	DT_CLK(NULL, "mcbsp2_ick", "mcbsp2_ick"),
	DT_CLK(NULL, "mcbsp3_ick", "mcbsp3_ick"),
	DT_CLK(NULL, "mcbsp4_ick", "mcbsp4_ick"),
	DT_CLK(NULL, "mcbsp5_ick", "mcbsp5_ick"),
	DT_CLK(NULL, "mcbsp1_fck", "mcbsp1_fck"),
	DT_CLK(NULL, "mcbsp2_fck", "mcbsp2_fck"),
	DT_CLK(NULL, "mcbsp3_fck", "mcbsp3_fck"),
	DT_CLK(NULL, "mcbsp4_fck", "mcbsp4_fck"),
	DT_CLK(NULL, "mcbsp5_fck", "mcbsp5_fck"),
	DT_CLK("etb", "emu_src_ck", "emu_src_ck"),
	DT_CLK(NULL, "emu_src_ck", "emu_src_ck"),
	DT_CLK(NULL, "pclk_fck", "pclk_fck"),
	DT_CLK(NULL, "pclkx2_fck", "pclkx2_fck"),
	DT_CLK(NULL, "atclk_fck", "atclk_fck"),
	DT_CLK(NULL, "traceclk_src_fck", "traceclk_src_fck"),
	DT_CLK(NULL, "traceclk_fck", "traceclk_fck"),
	DT_CLK(NULL, "secure_32k_fck", "secure_32k_fck"),
	DT_CLK(NULL, "gpt12_fck", "gpt12_fck"),
	DT_CLK(NULL, "wdt1_fck", "wdt1_fck"),
	DT_CLK(NULL, "timer_32k_ck", "omap_32k_fck"),
	DT_CLK(NULL, "timer_sys_ck", "sys_ck"),
	DT_CLK(NULL, "cpufreq_ck", "dpll1_ck"),
	{ .node_name = NULL },
};

static struct ti_dt_clk omap34xx_omap36xx_clks[] = {
	DT_CLK(NULL, "aes1_ick", "aes1_ick"),
	DT_CLK("omap_rng", "ick", "rng_ick"),
	DT_CLK("omap3-rom-rng", "ick", "rng_ick"),
	DT_CLK(NULL, "sha11_ick", "sha11_ick"),
	DT_CLK(NULL, "des1_ick", "des1_ick"),
	DT_CLK(NULL, "cam_mclk", "cam_mclk"),
	DT_CLK(NULL, "cam_ick", "cam_ick"),
	DT_CLK(NULL, "csi2_96m_fck", "csi2_96m_fck"),
	DT_CLK(NULL, "security_l3_ick", "security_l3_ick"),
	DT_CLK(NULL, "pka_ick", "pka_ick"),
	DT_CLK(NULL, "icr_ick", "icr_ick"),
	DT_CLK("omap-aes", "ick", "aes2_ick"),
	DT_CLK("omap-sham", "ick", "sha12_ick"),
	DT_CLK(NULL, "des2_ick", "des2_ick"),
	DT_CLK(NULL, "mspro_ick", "mspro_ick"),
	DT_CLK(NULL, "mailboxes_ick", "mailboxes_ick"),
	DT_CLK(NULL, "ssi_l4_ick", "ssi_l4_ick"),
	DT_CLK(NULL, "sr1_fck", "sr1_fck"),
	DT_CLK(NULL, "sr2_fck", "sr2_fck"),
	DT_CLK(NULL, "sr_l4_ick", "sr_l4_ick"),
	DT_CLK(NULL, "security_l4_ick2", "security_l4_ick2"),
	DT_CLK(NULL, "wkup_l4_ick", "wkup_l4_ick"),
	DT_CLK(NULL, "dpll2_fck", "dpll2_fck"),
	DT_CLK(NULL, "iva2_ck", "iva2_ck"),
	DT_CLK(NULL, "modem_fck", "modem_fck"),
	DT_CLK(NULL, "sad2d_ick", "sad2d_ick"),
	DT_CLK(NULL, "mad2d_ick", "mad2d_ick"),
	DT_CLK(NULL, "mspro_fck", "mspro_fck"),
	DT_CLK(NULL, "dpll2_ck", "dpll2_ck"),
	DT_CLK(NULL, "dpll2_m2_ck", "dpll2_m2_ck"),
	{ .node_name = NULL },
};

static struct ti_dt_clk omap36xx_omap3430es2plus_clks[] = {
	DT_CLK(NULL, "ssi_ssr_fck", "ssi_ssr_fck_3430es2"),
	DT_CLK(NULL, "ssi_sst_fck", "ssi_sst_fck_3430es2"),
	DT_CLK("musb-omap2430", "ick", "hsotgusb_ick_3430es2"),
	DT_CLK(NULL, "hsotgusb_ick", "hsotgusb_ick_3430es2"),
	DT_CLK(NULL, "ssi_ick", "ssi_ick_3430es2"),
	DT_CLK(NULL, "usim_fck", "usim_fck"),
	DT_CLK(NULL, "usim_ick", "usim_ick"),
	{ .node_name = NULL },
};

static struct ti_dt_clk omap3430es1_clks[] = {
	DT_CLK(NULL, "gfx_l3_ck", "gfx_l3_ck"),
	DT_CLK(NULL, "gfx_l3_fck", "gfx_l3_fck"),
	DT_CLK(NULL, "gfx_l3_ick", "gfx_l3_ick"),
	DT_CLK(NULL, "gfx_cg1_ck", "gfx_cg1_ck"),
	DT_CLK(NULL, "gfx_cg2_ck", "gfx_cg2_ck"),
	DT_CLK(NULL, "d2d_26m_fck", "d2d_26m_fck"),
	DT_CLK(NULL, "fshostusb_fck", "fshostusb_fck"),
	DT_CLK(NULL, "ssi_ssr_fck", "ssi_ssr_fck_3430es1"),
	DT_CLK(NULL, "ssi_sst_fck", "ssi_sst_fck_3430es1"),
	DT_CLK("musb-omap2430", "ick", "hsotgusb_ick_3430es1"),
	DT_CLK(NULL, "hsotgusb_ick", "hsotgusb_ick_3430es1"),
	DT_CLK(NULL, "fac_ick", "fac_ick"),
	DT_CLK(NULL, "ssi_ick", "ssi_ick_3430es1"),
	DT_CLK(NULL, "usb_l4_ick", "usb_l4_ick"),
	DT_CLK(NULL, "dss1_alwon_fck", "dss1_alwon_fck_3430es1"),
	DT_CLK("omapdss_dss", "ick", "dss_ick_3430es1"),
	DT_CLK(NULL, "dss_ick", "dss_ick_3430es1"),
	{ .node_name = NULL },
};

static struct ti_dt_clk omap36xx_am35xx_omap3430es2plus_clks[] = {
	DT_CLK(NULL, "virt_16_8m_ck", "virt_16_8m_ck"),
	DT_CLK(NULL, "dpll5_ck", "dpll5_ck"),
	DT_CLK(NULL, "dpll5_m2_ck", "dpll5_m2_ck"),
	DT_CLK(NULL, "sgx_fck", "sgx_fck"),
	DT_CLK(NULL, "sgx_ick", "sgx_ick"),
	DT_CLK(NULL, "cpefuse_fck", "cpefuse_fck"),
	DT_CLK(NULL, "ts_fck", "ts_fck"),
	DT_CLK(NULL, "usbtll_fck", "usbtll_fck"),
	DT_CLK(NULL, "usbtll_ick", "usbtll_ick"),
	DT_CLK("omap_hsmmc.2", "ick", "mmchs3_ick"),
	DT_CLK(NULL, "mmchs3_ick", "mmchs3_ick"),
	DT_CLK(NULL, "mmchs3_fck", "mmchs3_fck"),
	DT_CLK(NULL, "dss1_alwon_fck", "dss1_alwon_fck_3430es2"),
	DT_CLK("omapdss_dss", "ick", "dss_ick_3430es2"),
	DT_CLK(NULL, "dss_ick", "dss_ick_3430es2"),
	DT_CLK(NULL, "usbhost_120m_fck", "usbhost_120m_fck"),
	DT_CLK(NULL, "usbhost_48m_fck", "usbhost_48m_fck"),
	DT_CLK(NULL, "usbhost_ick", "usbhost_ick"),
	{ .node_name = NULL },
};

static struct ti_dt_clk am35xx_clks[] = {
	DT_CLK(NULL, "ipss_ick", "ipss_ick"),
	DT_CLK(NULL, "rmii_ck", "rmii_ck"),
	DT_CLK(NULL, "pclk_ck", "pclk_ck"),
	DT_CLK(NULL, "emac_ick", "emac_ick"),
	DT_CLK(NULL, "emac_fck", "emac_fck"),
	DT_CLK("davinci_emac.0", NULL, "emac_ick"),
	DT_CLK("davinci_mdio.0", NULL, "emac_fck"),
	DT_CLK("vpfe-capture", "master", "vpfe_ick"),
	DT_CLK("vpfe-capture", "slave", "vpfe_fck"),
	DT_CLK(NULL, "hsotgusb_ick", "hsotgusb_ick_am35xx"),
	DT_CLK(NULL, "hsotgusb_fck", "hsotgusb_fck_am35xx"),
	DT_CLK(NULL, "hecc_ck", "hecc_ck"),
	DT_CLK(NULL, "uart4_ick", "uart4_ick_am35xx"),
	DT_CLK(NULL, "uart4_fck", "uart4_fck_am35xx"),
	{ .node_name = NULL },
};

static struct ti_dt_clk omap36xx_clks[] = {
	DT_CLK(NULL, "omap_192m_alwon_fck", "omap_192m_alwon_fck"),
	DT_CLK(NULL, "uart4_fck", "uart4_fck"),
	DT_CLK(NULL, "uart4_ick", "uart4_ick"),
	{ .node_name = NULL },
};

static const char *enable_init_clks[] = {
	"sdrc_ick",
	"gpmc_fck",
	"omapctrl_ick",
};

enum {
	OMAP3_SOC_AM35XX,
	OMAP3_SOC_OMAP3430_ES1,
	OMAP3_SOC_OMAP3430_ES2_PLUS,
	OMAP3_SOC_OMAP3630,
};

/**
 * omap3_clk_lock_dpll5 - locks DPLL5
 *
 * Locks DPLL5 to a pre-defined frequency. This is required for proper
 * operation of USB.
 */
void __init omap3_clk_lock_dpll5(void)
{
	struct clk *dpll5_clk;
	struct clk *dpll5_m2_clk;

	/*
	 * Errata sprz319f advisory 2.1 documents a USB host clock drift issue
	 * that can be worked around using specially crafted dpll5 settings
	 * with a dpll5_m2 divider set to 8. Set the dpll5 rate to 8x the USB
	 * host clock rate, its .set_rate handler() will detect that frequency
	 * and use the errata settings.
	 */
	dpll5_clk = clk_get(NULL, "dpll5_ck");
	clk_set_rate(dpll5_clk, OMAP3_DPLL5_FREQ_FOR_USBHOST * 8);
	clk_prepare_enable(dpll5_clk);

	/* Program dpll5_m2_clk divider */
	dpll5_m2_clk = clk_get(NULL, "dpll5_m2_ck");
	clk_prepare_enable(dpll5_m2_clk);
	clk_set_rate(dpll5_m2_clk, OMAP3_DPLL5_FREQ_FOR_USBHOST);

	clk_disable_unprepare(dpll5_m2_clk);
	clk_disable_unprepare(dpll5_clk);
}

static int __init omap3xxx_dt_clk_init(int soc_type)
{
	if (soc_type == OMAP3_SOC_AM35XX || soc_type == OMAP3_SOC_OMAP3630 ||
	    soc_type == OMAP3_SOC_OMAP3430_ES1 ||
	    soc_type == OMAP3_SOC_OMAP3430_ES2_PLUS)
		ti_dt_clocks_register(omap3xxx_clks);

	if (soc_type == OMAP3_SOC_AM35XX)
		ti_dt_clocks_register(am35xx_clks);

	if (soc_type == OMAP3_SOC_OMAP3630 || soc_type == OMAP3_SOC_AM35XX ||
	    soc_type == OMAP3_SOC_OMAP3430_ES2_PLUS)
		ti_dt_clocks_register(omap36xx_am35xx_omap3430es2plus_clks);

	if (soc_type == OMAP3_SOC_OMAP3430_ES1)
		ti_dt_clocks_register(omap3430es1_clks);

	if (soc_type == OMAP3_SOC_OMAP3430_ES2_PLUS ||
	    soc_type == OMAP3_SOC_OMAP3630)
		ti_dt_clocks_register(omap36xx_omap3430es2plus_clks);

	if (soc_type == OMAP3_SOC_OMAP3430_ES1 ||
	    soc_type == OMAP3_SOC_OMAP3430_ES2_PLUS ||
	    soc_type == OMAP3_SOC_OMAP3630)
		ti_dt_clocks_register(omap34xx_omap36xx_clks);

	if (soc_type == OMAP3_SOC_OMAP3630)
		ti_dt_clocks_register(omap36xx_clks);

	omap2_clk_disable_autoidle_all();

	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	pr_info("Clocking rate (Crystal/Core/MPU): %ld.%01ld/%ld/%ld MHz\n",
		(clk_get_rate(clk_get_sys(NULL, "osc_sys_ck")) / 1000000),
		(clk_get_rate(clk_get_sys(NULL, "osc_sys_ck")) / 100000) % 10,
		(clk_get_rate(clk_get_sys(NULL, "core_ck")) / 1000000),
		(clk_get_rate(clk_get_sys(NULL, "arm_fck")) / 1000000));

	if (soc_type != OMAP3_SOC_OMAP3430_ES1)
		omap3_clk_lock_dpll5();

	return 0;
}

int __init omap3430_dt_clk_init(void)
{
	return omap3xxx_dt_clk_init(OMAP3_SOC_OMAP3430_ES2_PLUS);
}

int __init omap3630_dt_clk_init(void)
{
	return omap3xxx_dt_clk_init(OMAP3_SOC_OMAP3630);
}

int __init am35xx_dt_clk_init(void)
{
	return omap3xxx_dt_clk_init(OMAP3_SOC_AM35XX);
}
