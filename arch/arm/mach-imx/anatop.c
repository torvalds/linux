// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include "common.h"
#include "hardware.h"

#define REG_SET		0x4
#define REG_CLR		0x8

#define ANADIG_REG_2P5		0x130
#define ANADIG_REG_CORE		0x140
#define ANADIG_ANA_MISC0	0x150
#define ANADIG_DIGPROG		0x260
#define ANADIG_DIGPROG_IMX6SL	0x280
#define ANADIG_DIGPROG_IMX7D	0x800

#define SRC_SBMR2		0x1c

#define BM_ANADIG_REG_2P5_ENABLE_WEAK_LINREG	0x40000
#define BM_ANADIG_REG_2P5_ENABLE_PULLDOWN	0x8
#define BM_ANADIG_REG_CORE_FET_ODRIVE		0x20000000
#define BM_ANADIG_ANA_MISC0_STOP_MODE_CONFIG	0x1000
/* Below MISC0_DISCON_HIGH_SNVS is only for i.MX6SL */
#define BM_ANADIG_ANA_MISC0_DISCON_HIGH_SNVS	0x2000

static struct regmap *anatop;

static void imx_anatop_enable_weak2p5(bool enable)
{
	u32 reg, val;

	regmap_read(anatop, ANADIG_ANA_MISC0, &val);

	/* can only be enabled when stop_mode_config is clear. */
	reg = ANADIG_REG_2P5;
	reg += (enable && (val & BM_ANADIG_ANA_MISC0_STOP_MODE_CONFIG) == 0) ?
		REG_SET : REG_CLR;
	regmap_write(anatop, reg, BM_ANADIG_REG_2P5_ENABLE_WEAK_LINREG);
}

static void imx_anatop_enable_fet_odrive(bool enable)
{
	regmap_write(anatop, ANADIG_REG_CORE + (enable ? REG_SET : REG_CLR),
		BM_ANADIG_REG_CORE_FET_ODRIVE);
}

static inline void imx_anatop_enable_2p5_pulldown(bool enable)
{
	regmap_write(anatop, ANADIG_REG_2P5 + (enable ? REG_SET : REG_CLR),
		BM_ANADIG_REG_2P5_ENABLE_PULLDOWN);
}

static inline void imx_anatop_disconnect_high_snvs(bool enable)
{
	regmap_write(anatop, ANADIG_ANA_MISC0 + (enable ? REG_SET : REG_CLR),
		BM_ANADIG_ANA_MISC0_DISCON_HIGH_SNVS);
}

void imx_anatop_pre_suspend(void)
{
	if (imx_mmdc_get_ddr_type() == IMX_DDR_TYPE_LPDDR2)
		imx_anatop_enable_2p5_pulldown(true);
	else
		imx_anatop_enable_weak2p5(true);

	imx_anatop_enable_fet_odrive(true);

	if (cpu_is_imx6sl())
		imx_anatop_disconnect_high_snvs(true);
}

void imx_anatop_post_resume(void)
{
	if (imx_mmdc_get_ddr_type() == IMX_DDR_TYPE_LPDDR2)
		imx_anatop_enable_2p5_pulldown(false);
	else
		imx_anatop_enable_weak2p5(false);

	imx_anatop_enable_fet_odrive(false);

	if (cpu_is_imx6sl())
		imx_anatop_disconnect_high_snvs(false);

}

void __init imx_init_revision_from_anatop(void)
{
	struct device_node *np;
	void __iomem *anatop_base;
	unsigned int revision;
	u32 digprog;
	u16 offset = ANADIG_DIGPROG;
	u8 major_part, minor_part;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-anatop");
	anatop_base = of_iomap(np, 0);
	WARN_ON(!anatop_base);
	if (of_device_is_compatible(np, "fsl,imx6sl-anatop"))
		offset = ANADIG_DIGPROG_IMX6SL;
	if (of_device_is_compatible(np, "fsl,imx7d-anatop"))
		offset = ANADIG_DIGPROG_IMX7D;
	digprog = readl_relaxed(anatop_base + offset);
	iounmap(anatop_base);

	/*
	 * On i.MX7D digprog value match linux version format, so
	 * it needn't map again and we can use register value directly.
	 */
	if (of_device_is_compatible(np, "fsl,imx7d-anatop")) {
		revision = digprog & 0xff;
	} else {
		/*
		 * MAJOR: [15:8], the major silicon revison;
		 * MINOR: [7: 0], the minor silicon revison;
		 *
		 * please refer to the i.MX RM for the detailed
		 * silicon revison bit define.
		 * format the major part and minor part to match the
		 * linux kernel soc version format.
		 */
		major_part = (digprog >> 8) & 0xf;
		minor_part = digprog & 0xf;
		revision = ((major_part + 1) << 4) | minor_part;

		if ((digprog >> 16) == MXC_CPU_IMX6ULL) {
			void __iomem *src_base;
			u32 sbmr2;

			np = of_find_compatible_node(NULL, NULL,
						     "fsl,imx6ul-src");
			src_base = of_iomap(np, 0);
			WARN_ON(!src_base);
			sbmr2 = readl_relaxed(src_base + SRC_SBMR2);
			iounmap(src_base);

			/* src_sbmr2 bit 6 is to identify if it is i.MX6ULZ */
			if (sbmr2 & (1 << 6)) {
				digprog &= ~(0xff << 16);
				digprog |= (MXC_CPU_IMX6ULZ << 16);
			}
		}
	}

	mxc_set_cpu_type(digprog >> 16 & 0xff);
	imx_set_soc_revision(revision);
}

void __init imx_anatop_init(void)
{
	anatop = syscon_regmap_lookup_by_compatible("fsl,imx6q-anatop");
	if (IS_ERR(anatop))
		pr_err("%s: failed to find imx6q-anatop regmap!\n", __func__);
}
