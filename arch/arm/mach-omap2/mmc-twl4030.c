/*
 * linux/arch/arm/mach-omap2/mmc-twl4030.c
 *
 * Copyright (C) 2007-2008 Texas Instruments
 * Copyright (C) 2008 Nokia Corporation
 * Author: Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c/twl4030.h>

#include <mach/hardware.h>
#include <mach/control.h>
#include <mach/mmc.h>
#include <mach/board.h>

#include "mmc-twl4030.h"

#if defined(CONFIG_TWL4030_CORE) && \
	(defined(CONFIG_MMC_OMAP_HS) || defined(CONFIG_MMC_OMAP_HS_MODULE))

#define LDO_CLR			0x00
#define VSEL_S2_CLR		0x40

#define VMMC1_DEV_GRP		0x27
#define VMMC1_CLR		0x00
#define VMMC1_315V		0x03
#define VMMC1_300V		0x02
#define VMMC1_285V		0x01
#define VMMC1_185V		0x00
#define VMMC1_DEDICATED		0x2A

#define VMMC2_DEV_GRP		0x2B
#define VMMC2_CLR		0x40
#define VMMC2_315V		0x0c
#define VMMC2_300V		0x0b
#define VMMC2_285V		0x0a
#define VMMC2_260V		0x08
#define VMMC2_185V		0x06
#define VMMC2_DEDICATED		0x2E

#define VMMC_DEV_GRP_P1		0x20

static u16 control_pbias_offset;
static u16 control_devconf1_offset;

#define HSMMC_NAME_LEN	9

static struct twl_mmc_controller {
	struct omap_mmc_platform_data	*mmc;
	u8		twl_vmmc_dev_grp;
	u8		twl_mmc_dedicated;
	char		name[HSMMC_NAME_LEN];
} hsmmc[] = {
	{
		.twl_vmmc_dev_grp		= VMMC1_DEV_GRP,
		.twl_mmc_dedicated		= VMMC1_DEDICATED,
	},
	{
		.twl_vmmc_dev_grp		= VMMC2_DEV_GRP,
		.twl_mmc_dedicated		= VMMC2_DEDICATED,
	},
};

static int twl_mmc_card_detect(int irq)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(hsmmc); i++) {
		struct omap_mmc_platform_data *mmc;

		mmc = hsmmc[i].mmc;
		if (!mmc)
			continue;
		if (irq != mmc->slots[0].card_detect_irq)
			continue;

		/* NOTE: assumes card detect signal is active-low */
		return !gpio_get_value_cansleep(mmc->slots[0].switch_pin);
	}
	return -ENOSYS;
}

static int twl_mmc_get_ro(struct device *dev, int slot)
{
	struct omap_mmc_platform_data *mmc = dev->platform_data;

	/* NOTE: assumes write protect signal is active-high */
	return gpio_get_value_cansleep(mmc->slots[0].gpio_wp);
}

/*
 * MMC Slot Initialization.
 */
static int twl_mmc_late_init(struct device *dev)
{
	struct omap_mmc_platform_data *mmc = dev->platform_data;
	int ret = 0;
	int i;

	ret = gpio_request(mmc->slots[0].switch_pin, "mmc_cd");
	if (ret)
		goto done;
	ret = gpio_direction_input(mmc->slots[0].switch_pin);
	if (ret)
		goto err;

	for (i = 0; i < ARRAY_SIZE(hsmmc); i++) {
		if (hsmmc[i].name == mmc->slots[0].name) {
			hsmmc[i].mmc = mmc;
			break;
		}
	}

	return 0;

err:
	gpio_free(mmc->slots[0].switch_pin);
done:
	mmc->slots[0].card_detect_irq = 0;
	mmc->slots[0].card_detect = NULL;

	dev_err(dev, "err %d configuring card detect\n", ret);
	return ret;
}

static void twl_mmc_cleanup(struct device *dev)
{
	struct omap_mmc_platform_data *mmc = dev->platform_data;

	gpio_free(mmc->slots[0].switch_pin);
}

#ifdef CONFIG_PM

static int twl_mmc_suspend(struct device *dev, int slot)
{
	struct omap_mmc_platform_data *mmc = dev->platform_data;

	disable_irq(mmc->slots[0].card_detect_irq);
	return 0;
}

static int twl_mmc_resume(struct device *dev, int slot)
{
	struct omap_mmc_platform_data *mmc = dev->platform_data;

	enable_irq(mmc->slots[0].card_detect_irq);
	return 0;
}

#else
#define twl_mmc_suspend	NULL
#define twl_mmc_resume	NULL
#endif

/*
 * Sets the MMC voltage in twl4030
 */
static int twl_mmc_set_voltage(struct twl_mmc_controller *c, int vdd)
{
	int ret;
	u8 vmmc, dev_grp_val;

	switch (1 << vdd) {
	case MMC_VDD_35_36:
	case MMC_VDD_34_35:
	case MMC_VDD_33_34:
	case MMC_VDD_32_33:
	case MMC_VDD_31_32:
	case MMC_VDD_30_31:
		if (c->twl_vmmc_dev_grp == VMMC1_DEV_GRP)
			vmmc = VMMC1_315V;
		else
			vmmc = VMMC2_315V;
		break;
	case MMC_VDD_29_30:
		if (c->twl_vmmc_dev_grp == VMMC1_DEV_GRP)
			vmmc = VMMC1_315V;
		else
			vmmc = VMMC2_300V;
		break;
	case MMC_VDD_27_28:
	case MMC_VDD_26_27:
		if (c->twl_vmmc_dev_grp == VMMC1_DEV_GRP)
			vmmc = VMMC1_285V;
		else
			vmmc = VMMC2_285V;
		break;
	case MMC_VDD_25_26:
	case MMC_VDD_24_25:
	case MMC_VDD_23_24:
	case MMC_VDD_22_23:
	case MMC_VDD_21_22:
	case MMC_VDD_20_21:
		if (c->twl_vmmc_dev_grp == VMMC1_DEV_GRP)
			vmmc = VMMC1_285V;
		else
			vmmc = VMMC2_260V;
		break;
	case MMC_VDD_165_195:
		if (c->twl_vmmc_dev_grp == VMMC1_DEV_GRP)
			vmmc = VMMC1_185V;
		else
			vmmc = VMMC2_185V;
		break;
	default:
		vmmc = 0;
		break;
	}

	if (vmmc)
		dev_grp_val = VMMC_DEV_GRP_P1;	/* Power up */
	else
		dev_grp_val = LDO_CLR;		/* Power down */

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
					dev_grp_val, c->twl_vmmc_dev_grp);
	if (ret)
		return ret;

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
					vmmc, c->twl_mmc_dedicated);

	return ret;
}

static int twl_mmc1_set_power(struct device *dev, int slot, int power_on,
				int vdd)
{
	u32 reg;
	int ret = 0;
	struct twl_mmc_controller *c = &hsmmc[0];
	struct omap_mmc_platform_data *mmc = dev->platform_data;

	if (power_on) {
		if (cpu_is_omap2430()) {
			reg = omap_ctrl_readl(OMAP243X_CONTROL_DEVCONF1);
			if ((1 << vdd) >= MMC_VDD_30_31)
				reg |= OMAP243X_MMC1_ACTIVE_OVERWRITE;
			else
				reg &= ~OMAP243X_MMC1_ACTIVE_OVERWRITE;
			omap_ctrl_writel(reg, OMAP243X_CONTROL_DEVCONF1);
		}

		if (mmc->slots[0].internal_clock) {
			reg = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
			reg |= OMAP2_MMCSDIO1ADPCLKISEL;
			omap_ctrl_writel(reg, OMAP2_CONTROL_DEVCONF0);
		}

		reg = omap_ctrl_readl(control_pbias_offset);
		reg |= OMAP2_PBIASSPEEDCTRL0;
		reg &= ~OMAP2_PBIASLITEPWRDNZ0;
		omap_ctrl_writel(reg, control_pbias_offset);

		ret = twl_mmc_set_voltage(c, vdd);

		/* 100ms delay required for PBIAS configuration */
		msleep(100);
		reg = omap_ctrl_readl(control_pbias_offset);
		reg |= (OMAP2_PBIASLITEPWRDNZ0 | OMAP2_PBIASSPEEDCTRL0);
		if ((1 << vdd) <= MMC_VDD_165_195)
			reg &= ~OMAP2_PBIASLITEVMODE0;
		else
			reg |= OMAP2_PBIASLITEVMODE0;
		omap_ctrl_writel(reg, control_pbias_offset);
	} else {
		reg = omap_ctrl_readl(control_pbias_offset);
		reg &= ~OMAP2_PBIASLITEPWRDNZ0;
		omap_ctrl_writel(reg, control_pbias_offset);

		ret = twl_mmc_set_voltage(c, 0);

		/* 100ms delay required for PBIAS configuration */
		msleep(100);
		reg = omap_ctrl_readl(control_pbias_offset);
		reg |= (OMAP2_PBIASSPEEDCTRL0 | OMAP2_PBIASLITEPWRDNZ0 |
			OMAP2_PBIASLITEVMODE0);
		omap_ctrl_writel(reg, control_pbias_offset);
	}

	return ret;
}

static int twl_mmc2_set_power(struct device *dev, int slot, int power_on, int vdd)
{
	int ret;
	struct twl_mmc_controller *c = &hsmmc[1];
	struct omap_mmc_platform_data *mmc = dev->platform_data;

	if (power_on) {
		if (mmc->slots[0].internal_clock) {
			u32 reg;

			reg = omap_ctrl_readl(control_devconf1_offset);
			reg |= OMAP2_MMCSDIO2ADPCLKISEL;
			omap_ctrl_writel(reg, control_devconf1_offset);
		}
		ret = twl_mmc_set_voltage(c, vdd);
	} else {
		ret = twl_mmc_set_voltage(c, 0);
	}

	return ret;
}

static struct omap_mmc_platform_data *hsmmc_data[OMAP34XX_NR_MMC] __initdata;

void __init twl4030_mmc_init(struct twl4030_hsmmc_info *controllers)
{
	struct twl4030_hsmmc_info *c;
	int nr_hsmmc = ARRAY_SIZE(hsmmc_data);

	if (cpu_is_omap2430()) {
		control_pbias_offset = OMAP243X_CONTROL_PBIAS_LITE;
		control_devconf1_offset = OMAP243X_CONTROL_DEVCONF1;
		nr_hsmmc = 2;
	} else {
		control_pbias_offset = OMAP343X_CONTROL_PBIAS_LITE;
		control_devconf1_offset = OMAP343X_CONTROL_DEVCONF1;
	}

	for (c = controllers; c->mmc; c++) {
		struct twl_mmc_controller *twl = hsmmc + c->mmc - 1;
		struct omap_mmc_platform_data *mmc = hsmmc_data[c->mmc - 1];

		if (!c->mmc || c->mmc > nr_hsmmc) {
			pr_debug("MMC%d: no such controller\n", c->mmc);
			continue;
		}
		if (mmc) {
			pr_debug("MMC%d: already configured\n", c->mmc);
			continue;
		}

		mmc = kzalloc(sizeof(struct omap_mmc_platform_data), GFP_KERNEL);
		if (!mmc) {
			pr_err("Cannot allocate memory for mmc device!\n");
			return;
		}

		sprintf(twl->name, "mmc%islot%i", c->mmc, 1);
		mmc->slots[0].name = twl->name;
		mmc->nr_slots = 1;
		mmc->slots[0].ocr_mask = MMC_VDD_165_195 |
					MMC_VDD_26_27 | MMC_VDD_27_28 |
					MMC_VDD_29_30 |
					MMC_VDD_30_31 | MMC_VDD_31_32;
		mmc->slots[0].wires = c->wires;
		mmc->slots[0].internal_clock = !c->ext_clock;
		mmc->dma_mask = 0xffffffff;

		/* note: twl4030 card detect GPIOs normally switch VMMCx ... */
		if (gpio_is_valid(c->gpio_cd)) {
			mmc->init = twl_mmc_late_init;
			mmc->cleanup = twl_mmc_cleanup;
			mmc->suspend = twl_mmc_suspend;
			mmc->resume = twl_mmc_resume;

			mmc->slots[0].switch_pin = c->gpio_cd;
			mmc->slots[0].card_detect_irq = gpio_to_irq(c->gpio_cd);
			mmc->slots[0].card_detect = twl_mmc_card_detect;
		} else
			mmc->slots[0].switch_pin = -EINVAL;

		/* write protect normally uses an OMAP gpio */
		if (gpio_is_valid(c->gpio_wp)) {
			gpio_request(c->gpio_wp, "mmc_wp");
			gpio_direction_input(c->gpio_wp);

			mmc->slots[0].gpio_wp = c->gpio_wp;
			mmc->slots[0].get_ro = twl_mmc_get_ro;
		} else
			mmc->slots[0].gpio_wp = -EINVAL;

		/* NOTE:  we assume OMAP's MMC1 and MMC2 use
		 * the TWL4030's VMMC1 and VMMC2, respectively;
		 * and that OMAP's MMC3 isn't used.
		 */

		switch (c->mmc) {
		case 1:
			mmc->slots[0].set_power = twl_mmc1_set_power;
			break;
		case 2:
			mmc->slots[0].set_power = twl_mmc2_set_power;
			break;
		default:
			pr_err("MMC%d configuration not supported!\n", c->mmc);
			kfree(mmc);
			continue;
		}
		hsmmc_data[c->mmc - 1] = mmc;
	}

	omap2_init_mmc(hsmmc_data, OMAP34XX_NR_MMC);
}

#endif
