/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define REG_SET		0x4
#define REG_CLR		0x8

#define ANADIG_REG_CORE		0x140
#define ANADIG_USB1_CHRG_DETECT	0x1b0
#define ANADIG_USB2_CHRG_DETECT	0x210
#define ANADIG_DIGPROG		0x260

#define BM_ANADIG_REG_CORE_FET_ODRIVE		0x20000000
#define BM_ANADIG_USB_CHRG_DETECT_CHK_CHRG_B	0x80000
#define BM_ANADIG_USB_CHRG_DETECT_EN_B		0x100000

static struct regmap *anatop;

static void imx_anatop_enable_fet_odrive(bool enable)
{
	regmap_write(anatop, ANADIG_REG_CORE + (enable ? REG_SET : REG_CLR),
		BM_ANADIG_REG_CORE_FET_ODRIVE);
}

void imx_anatop_pre_suspend(void)
{
	imx_anatop_enable_fet_odrive(true);
}

void imx_anatop_post_resume(void)
{
	imx_anatop_enable_fet_odrive(false);
}

void imx_anatop_usb_chrg_detect_disable(void)
{
	regmap_write(anatop, ANADIG_USB1_CHRG_DETECT,
		BM_ANADIG_USB_CHRG_DETECT_EN_B
		| BM_ANADIG_USB_CHRG_DETECT_CHK_CHRG_B);
	regmap_write(anatop, ANADIG_USB2_CHRG_DETECT,
		BM_ANADIG_USB_CHRG_DETECT_EN_B |
		BM_ANADIG_USB_CHRG_DETECT_CHK_CHRG_B);
}

u32 imx_anatop_get_digprog(void)
{
	u32  val;

	regmap_read(anatop, ANADIG_DIGPROG, &val);
	return val;
}

void __init imx_anatop_init(void)
{
	anatop = syscon_regmap_lookup_by_compatible("fsl,imx6q-anatop");
	if (IS_ERR(anatop)) {
		pr_err("%s: failed to find imx6q-anatop regmap!\n", __func__);
		return;
	}
}
