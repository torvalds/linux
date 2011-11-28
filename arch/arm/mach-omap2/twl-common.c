/*
 * twl-common.c
 *
 * Copyright (C) 2011 Texas Instruments, Inc..
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/i2c.h>
#include <linux/i2c/twl.h>
#include <linux/gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>

#include <plat/i2c.h>
#include <plat/usb.h>

#include "twl-common.h"

static struct i2c_board_info __initdata pmic_i2c_board_info = {
	.addr		= 0x48,
	.flags		= I2C_CLIENT_WAKE,
};

void __init omap_pmic_init(int bus, u32 clkrate,
			   const char *pmic_type, int pmic_irq,
			   struct twl4030_platform_data *pmic_data)
{
	strncpy(pmic_i2c_board_info.type, pmic_type,
		sizeof(pmic_i2c_board_info.type));
	pmic_i2c_board_info.irq = pmic_irq;
	pmic_i2c_board_info.platform_data = pmic_data;

	omap_register_i2c_bus(bus, clkrate, &pmic_i2c_board_info, 1);
}

#if defined(CONFIG_ARCH_OMAP3)
static struct twl4030_usb_data omap3_usb_pdata = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static int omap3_batt_table[] = {
/* 0 C */
30800, 29500, 28300, 27100,
26000, 24900, 23900, 22900, 22000, 21100, 20300, 19400, 18700, 17900,
17200, 16500, 15900, 15300, 14700, 14100, 13600, 13100, 12600, 12100,
11600, 11200, 10800, 10400, 10000, 9630,  9280,  8950,  8620,  8310,
8020,  7730,  7460,  7200,  6950,  6710,  6470,  6250,  6040,  5830,
5640,  5450,  5260,  5090,  4920,  4760,  4600,  4450,  4310,  4170,
4040,  3910,  3790,  3670,  3550
};

static struct twl4030_bci_platform_data omap3_bci_pdata = {
	.battery_tmp_tbl	= omap3_batt_table,
	.tblsize		= ARRAY_SIZE(omap3_batt_table),
};

static struct twl4030_madc_platform_data omap3_madc_pdata = {
	.irq_line	= 1,
};

static struct twl4030_codec_data omap3_codec;

static struct twl4030_audio_data omap3_audio_pdata = {
	.audio_mclk = 26000000,
	.codec = &omap3_codec,
};

static struct regulator_consumer_supply omap3_vdda_dac_supplies[] = {
	REGULATOR_SUPPLY("vdda_dac", "omapdss_venc"),
};

static struct regulator_init_data omap3_vdac_idata = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(omap3_vdda_dac_supplies),
	.consumer_supplies	= omap3_vdda_dac_supplies,
};

static struct regulator_consumer_supply omap3_vpll2_supplies[] = {
	REGULATOR_SUPPLY("vdds_dsi", "omapdss"),
	REGULATOR_SUPPLY("vdds_dsi", "omapdss_dsi.0"),
};

static struct regulator_init_data omap3_vpll2_idata = {
	.constraints = {
		.min_uV                 = 1800000,
		.max_uV                 = 1800000,
		.valid_modes_mask       = REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask         = REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies		= ARRAY_SIZE(omap3_vpll2_supplies),
	.consumer_supplies		= omap3_vpll2_supplies,
};

void __init omap3_pmic_get_config(struct twl4030_platform_data *pmic_data,
				  u32 pdata_flags, u32 regulators_flags)
{
	if (!pmic_data->irq_base)
		pmic_data->irq_base = TWL4030_IRQ_BASE;
	if (!pmic_data->irq_end)
		pmic_data->irq_end = TWL4030_IRQ_END;

	/* Common platform data configurations */
	if (pdata_flags & TWL_COMMON_PDATA_USB && !pmic_data->usb)
		pmic_data->usb = &omap3_usb_pdata;

	if (pdata_flags & TWL_COMMON_PDATA_BCI && !pmic_data->bci)
		pmic_data->bci = &omap3_bci_pdata;

	if (pdata_flags & TWL_COMMON_PDATA_MADC && !pmic_data->madc)
		pmic_data->madc = &omap3_madc_pdata;

	if (pdata_flags & TWL_COMMON_PDATA_AUDIO && !pmic_data->audio)
		pmic_data->audio = &omap3_audio_pdata;

	/* Common regulator configurations */
	if (regulators_flags & TWL_COMMON_REGULATOR_VDAC && !pmic_data->vdac)
		pmic_data->vdac = &omap3_vdac_idata;

	if (regulators_flags & TWL_COMMON_REGULATOR_VPLL2 && !pmic_data->vpll2)
		pmic_data->vpll2 = &omap3_vpll2_idata;
}
#endif /* CONFIG_ARCH_OMAP3 */

#if defined(CONFIG_ARCH_OMAP4)
static struct twl4030_usb_data omap4_usb_pdata = {
	.phy_init	= omap4430_phy_init,
	.phy_exit	= omap4430_phy_exit,
	.phy_power	= omap4430_phy_power,
	.phy_set_clock	= omap4430_phy_set_clk,
	.phy_suspend	= omap4430_phy_suspend,
};

static struct regulator_init_data omap4_vdac_idata = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_vaux2_idata = {
	.constraints = {
		.min_uV			= 1200000,
		.max_uV			= 2800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_vaux3_idata = {
	.constraints = {
		.min_uV			= 1000000,
		.max_uV			= 3000000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_consumer_supply omap4_vmmc_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

/* VMMC1 for MMC1 card */
static struct regulator_init_data omap4_vmmc_idata = {
	.constraints = {
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(omap4_vmmc_supply),
	.consumer_supplies      = omap4_vmmc_supply,
};

static struct regulator_init_data omap4_vpp_idata = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 2500000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_vana_idata = {
	.constraints = {
		.min_uV			= 2100000,
		.max_uV			= 2100000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_consumer_supply omap4_vcxio_supply[] = {
	REGULATOR_SUPPLY("vdds_dsi", "omapdss_dss"),
	REGULATOR_SUPPLY("vdds_dsi", "omapdss_dsi.0"),
	REGULATOR_SUPPLY("vdds_dsi", "omapdss_dsi.1"),
};

static struct regulator_init_data omap4_vcxio_idata = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
		.always_on		= true,
	},
	.num_consumer_supplies	= ARRAY_SIZE(omap4_vcxio_supply),
	.consumer_supplies	= omap4_vcxio_supply,
};

static struct regulator_init_data omap4_vusb_idata = {
	.constraints = {
		.min_uV			= 3300000,
		.max_uV			= 3300000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_clk32kg_idata = {
	.constraints = {
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
	},
};

void __init omap4_pmic_get_config(struct twl4030_platform_data *pmic_data,
				  u32 pdata_flags, u32 regulators_flags)
{
	if (!pmic_data->irq_base)
		pmic_data->irq_base = TWL6030_IRQ_BASE;
	if (!pmic_data->irq_end)
		pmic_data->irq_end = TWL6030_IRQ_END;

	/* Common platform data configurations */
	if (pdata_flags & TWL_COMMON_PDATA_USB && !pmic_data->usb)
		pmic_data->usb = &omap4_usb_pdata;

	/* Common regulator configurations */
	if (regulators_flags & TWL_COMMON_REGULATOR_VDAC && !pmic_data->vdac)
		pmic_data->vdac = &omap4_vdac_idata;

	if (regulators_flags & TWL_COMMON_REGULATOR_VAUX2 && !pmic_data->vaux2)
		pmic_data->vaux2 = &omap4_vaux2_idata;

	if (regulators_flags & TWL_COMMON_REGULATOR_VAUX3 && !pmic_data->vaux3)
		pmic_data->vaux3 = &omap4_vaux3_idata;

	if (regulators_flags & TWL_COMMON_REGULATOR_VMMC && !pmic_data->vmmc)
		pmic_data->vmmc = &omap4_vmmc_idata;

	if (regulators_flags & TWL_COMMON_REGULATOR_VPP && !pmic_data->vpp)
		pmic_data->vpp = &omap4_vpp_idata;

	if (regulators_flags & TWL_COMMON_REGULATOR_VANA && !pmic_data->vana)
		pmic_data->vana = &omap4_vana_idata;

	if (regulators_flags & TWL_COMMON_REGULATOR_VCXIO && !pmic_data->vcxio)
		pmic_data->vcxio = &omap4_vcxio_idata;

	if (regulators_flags & TWL_COMMON_REGULATOR_VUSB && !pmic_data->vusb)
		pmic_data->vusb = &omap4_vusb_idata;

	if (regulators_flags & TWL_COMMON_REGULATOR_CLK32KG &&
	    !pmic_data->clk32kg)
		pmic_data->clk32kg = &omap4_clk32kg_idata;
}
#endif /* CONFIG_ARCH_OMAP4 */
