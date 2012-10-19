/*
 * OMAP2+ specific gpio initialization
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Author:
 *	Charulatha V <charu@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_data/gpio-omap.h>

#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>
#include <plat/omap-pm.h>

#include "powerdomain.h"

static int __init omap2_gpio_dev_init(struct omap_hwmod *oh, void *unused)
{
	struct platform_device *pdev;
	struct omap_gpio_platform_data *pdata;
	struct omap_gpio_dev_attr *dev_attr;
	char *name = "omap_gpio";
	int id;
	struct powerdomain *pwrdm;

	/*
	 * extract the device id from name field available in the
	 * hwmod database and use the same for constructing ids for
	 * gpio devices.
	 * CAUTION: Make sure the name in the hwmod database does
	 * not change. If changed, make corresponding change here
	 * or make use of static variable mechanism to handle this.
	 */
	sscanf(oh->name, "gpio%d", &id);

	pdata = kzalloc(sizeof(struct omap_gpio_platform_data), GFP_KERNEL);
	if (!pdata) {
		pr_err("gpio%d: Memory allocation failed\n", id);
		return -ENOMEM;
	}

	dev_attr = (struct omap_gpio_dev_attr *)oh->dev_attr;
	pdata->bank_width = dev_attr->bank_width;
	pdata->dbck_flag = dev_attr->dbck_flag;
	pdata->get_context_loss_count = omap_pm_get_dev_context_loss_count;
	pdata->regs = kzalloc(sizeof(struct omap_gpio_reg_offs), GFP_KERNEL);
	if (!pdata->regs) {
		pr_err("gpio%d: Memory allocation failed\n", id);
		kfree(pdata);
		return -ENOMEM;
	}

	switch (oh->class->rev) {
	case 0:
		if (id == 1)
			/* non-wakeup GPIO pins for OMAP2 Bank1 */
			pdata->non_wakeup_gpios = 0xe203ffc0;
		else if (id == 2)
			/* non-wakeup GPIO pins for OMAP2 Bank2 */
			pdata->non_wakeup_gpios = 0x08700040;
		/* fall through */

	case 1:
		pdata->regs->revision = OMAP24XX_GPIO_REVISION;
		pdata->regs->direction = OMAP24XX_GPIO_OE;
		pdata->regs->datain = OMAP24XX_GPIO_DATAIN;
		pdata->regs->dataout = OMAP24XX_GPIO_DATAOUT;
		pdata->regs->set_dataout = OMAP24XX_GPIO_SETDATAOUT;
		pdata->regs->clr_dataout = OMAP24XX_GPIO_CLEARDATAOUT;
		pdata->regs->irqstatus = OMAP24XX_GPIO_IRQSTATUS1;
		pdata->regs->irqstatus2 = OMAP24XX_GPIO_IRQSTATUS2;
		pdata->regs->irqenable = OMAP24XX_GPIO_IRQENABLE1;
		pdata->regs->irqenable2 = OMAP24XX_GPIO_IRQENABLE2;
		pdata->regs->set_irqenable = OMAP24XX_GPIO_SETIRQENABLE1;
		pdata->regs->clr_irqenable = OMAP24XX_GPIO_CLEARIRQENABLE1;
		pdata->regs->debounce = OMAP24XX_GPIO_DEBOUNCE_VAL;
		pdata->regs->debounce_en = OMAP24XX_GPIO_DEBOUNCE_EN;
		pdata->regs->ctrl = OMAP24XX_GPIO_CTRL;
		pdata->regs->wkup_en = OMAP24XX_GPIO_WAKE_EN;
		pdata->regs->leveldetect0 = OMAP24XX_GPIO_LEVELDETECT0;
		pdata->regs->leveldetect1 = OMAP24XX_GPIO_LEVELDETECT1;
		pdata->regs->risingdetect = OMAP24XX_GPIO_RISINGDETECT;
		pdata->regs->fallingdetect = OMAP24XX_GPIO_FALLINGDETECT;
		break;
	case 2:
		pdata->regs->revision = OMAP4_GPIO_REVISION;
		pdata->regs->direction = OMAP4_GPIO_OE;
		pdata->regs->datain = OMAP4_GPIO_DATAIN;
		pdata->regs->dataout = OMAP4_GPIO_DATAOUT;
		pdata->regs->set_dataout = OMAP4_GPIO_SETDATAOUT;
		pdata->regs->clr_dataout = OMAP4_GPIO_CLEARDATAOUT;
		pdata->regs->irqstatus_raw0 = OMAP4_GPIO_IRQSTATUSRAW0;
		pdata->regs->irqstatus_raw1 = OMAP4_GPIO_IRQSTATUSRAW1;
		pdata->regs->irqstatus = OMAP4_GPIO_IRQSTATUS0;
		pdata->regs->irqstatus2 = OMAP4_GPIO_IRQSTATUS1;
		pdata->regs->irqenable = OMAP4_GPIO_IRQSTATUSSET0;
		pdata->regs->irqenable2 = OMAP4_GPIO_IRQSTATUSSET1;
		pdata->regs->set_irqenable = OMAP4_GPIO_IRQSTATUSSET0;
		pdata->regs->clr_irqenable = OMAP4_GPIO_IRQSTATUSCLR0;
		pdata->regs->debounce = OMAP4_GPIO_DEBOUNCINGTIME;
		pdata->regs->debounce_en = OMAP4_GPIO_DEBOUNCENABLE;
		pdata->regs->ctrl = OMAP4_GPIO_CTRL;
		pdata->regs->wkup_en = OMAP4_GPIO_IRQWAKEN0;
		pdata->regs->leveldetect0 = OMAP4_GPIO_LEVELDETECT0;
		pdata->regs->leveldetect1 = OMAP4_GPIO_LEVELDETECT1;
		pdata->regs->risingdetect = OMAP4_GPIO_RISINGDETECT;
		pdata->regs->fallingdetect = OMAP4_GPIO_FALLINGDETECT;
		break;
	default:
		WARN(1, "Invalid gpio bank_type\n");
		kfree(pdata->regs);
		kfree(pdata);
		return -EINVAL;
	}

	pwrdm = omap_hwmod_get_pwrdm(oh);
	pdata->loses_context = pwrdm_can_ever_lose_context(pwrdm);

	pdev = omap_device_build(name, id - 1, oh, pdata,
				sizeof(*pdata),	NULL, 0, false);
	kfree(pdata);

	if (IS_ERR(pdev)) {
		WARN(1, "Can't build omap_device for %s:%s.\n",
					name, oh->name);
		return PTR_ERR(pdev);
	}

	return 0;
}

/*
 * gpio_init needs to be done before
 * machine_init functions access gpio APIs.
 * Hence gpio_init is a postcore_initcall.
 */
static int __init omap2_gpio_init(void)
{
	/* If dtb is there, the devices will be created dynamically */
	if (of_have_populated_dt())
		return -ENODEV;

	return omap_hwmod_for_each_by_class("gpio", omap2_gpio_dev_init, NULL);
}
postcore_initcall(omap2_gpio_init);
