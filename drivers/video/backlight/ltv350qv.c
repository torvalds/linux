/*
 * Power control for Samsung LTV350QV Quarter VGA LCD Panel
 *
 * Copyright (C) 2006, 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "ltv350qv.h"

#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

struct ltv350qv {
	struct spi_device	*spi;
	u8			*buffer;
	int			power;
	struct lcd_device	*ld;
};

/*
 * The power-on and power-off sequences are taken from the
 * LTV350QV-F04 data sheet from Samsung. The register definitions are
 * taken from the S6F2002 command list also from Samsung. Both
 * documents are distributed with the AVR32 Linux BSP CD from Atmel.
 *
 * There's still some voodoo going on here, but it's a lot better than
 * in the first incarnation of the driver where all we had was the raw
 * numbers from the initialization sequence.
 */
static int ltv350qv_write_reg(struct ltv350qv *lcd, u8 reg, u16 val)
{
	struct spi_message msg;
	struct spi_transfer index_xfer = {
		.len		= 3,
		.cs_change	= 1,
	};
	struct spi_transfer value_xfer = {
		.len		= 3,
	};

	spi_message_init(&msg);

	/* register index */
	lcd->buffer[0] = LTV_OPC_INDEX;
	lcd->buffer[1] = 0x00;
	lcd->buffer[2] = reg & 0x7f;
	index_xfer.tx_buf = lcd->buffer;
	spi_message_add_tail(&index_xfer, &msg);

	/* register value */
	lcd->buffer[4] = LTV_OPC_DATA;
	lcd->buffer[5] = val >> 8;
	lcd->buffer[6] = val;
	value_xfer.tx_buf = lcd->buffer + 4;
	spi_message_add_tail(&value_xfer, &msg);

	return spi_sync(lcd->spi, &msg);
}

/* The comments are taken straight from the data sheet */
static int ltv350qv_power_on(struct ltv350qv *lcd)
{
	int ret;

	/* Power On Reset Display off State */
	if (ltv350qv_write_reg(lcd, LTV_PWRCTL1, 0x0000))
		goto err;
	msleep(15);

	/* Power Setting Function 1 */
	if (ltv350qv_write_reg(lcd, LTV_PWRCTL1, LTV_VCOM_DISABLE))
		goto err;
	if (ltv350qv_write_reg(lcd, LTV_PWRCTL2, LTV_VCOML_ENABLE))
		goto err_power1;

	/* Power Setting Function 2 */
	if (ltv350qv_write_reg(lcd, LTV_PWRCTL1,
			       LTV_VCOM_DISABLE | LTV_DRIVE_CURRENT(5)
			       | LTV_SUPPLY_CURRENT(5)))
		goto err_power2;

	msleep(55);

	/* Instruction Setting */
	ret = ltv350qv_write_reg(lcd, LTV_IFCTL,
				 LTV_NMD | LTV_REV | LTV_NL(0x1d));
	ret |= ltv350qv_write_reg(lcd, LTV_DATACTL,
				  LTV_DS_SAME | LTV_CHS_480
				  | LTV_DF_RGB | LTV_RGB_BGR);
	ret |= ltv350qv_write_reg(lcd, LTV_ENTRY_MODE,
				  LTV_VSPL_ACTIVE_LOW
				  | LTV_HSPL_ACTIVE_LOW
				  | LTV_DPL_SAMPLE_RISING
				  | LTV_EPL_ACTIVE_LOW
				  | LTV_SS_RIGHT_TO_LEFT);
	ret |= ltv350qv_write_reg(lcd, LTV_GATECTL1, LTV_CLW(3));
	ret |= ltv350qv_write_reg(lcd, LTV_GATECTL2,
				  LTV_NW_INV_1LINE | LTV_FWI(3));
	ret |= ltv350qv_write_reg(lcd, LTV_VBP, 0x000a);
	ret |= ltv350qv_write_reg(lcd, LTV_HBP, 0x0021);
	ret |= ltv350qv_write_reg(lcd, LTV_SOTCTL, LTV_SDT(3) | LTV_EQ(0));
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(0), 0x0103);
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(1), 0x0301);
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(2), 0x1f0f);
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(3), 0x1f0f);
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(4), 0x0707);
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(5), 0x0307);
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(6), 0x0707);
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(7), 0x0000);
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(8), 0x0004);
	ret |= ltv350qv_write_reg(lcd, LTV_GAMMA(9), 0x0000);
	if (ret)
		goto err_settings;

	/* Wait more than 2 frames */
	msleep(20);

	/* Display On Sequence */
	ret = ltv350qv_write_reg(lcd, LTV_PWRCTL1,
				 LTV_VCOM_DISABLE | LTV_VCOMOUT_ENABLE
				 | LTV_POWER_ON | LTV_DRIVE_CURRENT(5)
				 | LTV_SUPPLY_CURRENT(5));
	ret |= ltv350qv_write_reg(lcd, LTV_GATECTL2,
				  LTV_NW_INV_1LINE | LTV_DSC | LTV_FWI(3));
	if (ret)
		goto err_disp_on;

	/* Display should now be ON. Phew. */
	return 0;

err_disp_on:
	/*
	 * Try to recover. Error handling probably isn't very useful
	 * at this point, just make a best effort to switch the panel
	 * off.
	 */
	ltv350qv_write_reg(lcd, LTV_PWRCTL1,
			   LTV_VCOM_DISABLE | LTV_DRIVE_CURRENT(5)
			   | LTV_SUPPLY_CURRENT(5));
	ltv350qv_write_reg(lcd, LTV_GATECTL2,
			   LTV_NW_INV_1LINE | LTV_FWI(3));
err_settings:
err_power2:
err_power1:
	ltv350qv_write_reg(lcd, LTV_PWRCTL2, 0x0000);
	msleep(1);
err:
	ltv350qv_write_reg(lcd, LTV_PWRCTL1, LTV_VCOM_DISABLE);
	return -EIO;
}

static int ltv350qv_power_off(struct ltv350qv *lcd)
{
	int ret;

	/* Display Off Sequence */
	ret = ltv350qv_write_reg(lcd, LTV_PWRCTL1,
				 LTV_VCOM_DISABLE
				 | LTV_DRIVE_CURRENT(5)
				 | LTV_SUPPLY_CURRENT(5));
	ret |= ltv350qv_write_reg(lcd, LTV_GATECTL2,
				  LTV_NW_INV_1LINE | LTV_FWI(3));

	/* Power down setting 1 */
	ret |= ltv350qv_write_reg(lcd, LTV_PWRCTL2, 0x0000);

	/* Wait at least 1 ms */
	msleep(1);

	/* Power down setting 2 */
	ret |= ltv350qv_write_reg(lcd, LTV_PWRCTL1, LTV_VCOM_DISABLE);

	/*
	 * No point in trying to recover here. If we can't switch the
	 * panel off, what are we supposed to do other than inform the
	 * user about the failure?
	 */
	if (ret)
		return -EIO;

	/* Display power should now be OFF */
	return 0;
}

static int ltv350qv_power(struct ltv350qv *lcd, int power)
{
	int ret = 0;

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		ret = ltv350qv_power_on(lcd);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		ret = ltv350qv_power_off(lcd);

	if (!ret)
		lcd->power = power;

	return ret;
}

static int ltv350qv_set_power(struct lcd_device *ld, int power)
{
	struct ltv350qv *lcd = lcd_get_data(ld);

	return ltv350qv_power(lcd, power);
}

static int ltv350qv_get_power(struct lcd_device *ld)
{
	struct ltv350qv *lcd = lcd_get_data(ld);

	return lcd->power;
}

static struct lcd_ops ltv_ops = {
	.get_power	= ltv350qv_get_power,
	.set_power	= ltv350qv_set_power,
};

static int __devinit ltv350qv_probe(struct spi_device *spi)
{
	struct ltv350qv *lcd;
	struct lcd_device *ld;
	int ret;

	lcd = kzalloc(sizeof(struct ltv350qv), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	lcd->spi = spi;
	lcd->power = FB_BLANK_POWERDOWN;
	lcd->buffer = kzalloc(8, GFP_KERNEL);
	if (!lcd->buffer) {
		ret = -ENOMEM;
		goto out_free_lcd;
	}

	ld = lcd_device_register("ltv350qv", &spi->dev, lcd, &ltv_ops);
	if (IS_ERR(ld)) {
		ret = PTR_ERR(ld);
		goto out_free_buffer;
	}
	lcd->ld = ld;

	ret = ltv350qv_power(lcd, FB_BLANK_UNBLANK);
	if (ret)
		goto out_unregister;

	dev_set_drvdata(&spi->dev, lcd);

	return 0;

out_unregister:
	lcd_device_unregister(ld);
out_free_buffer:
	kfree(lcd->buffer);
out_free_lcd:
	kfree(lcd);
	return ret;
}

static int __devexit ltv350qv_remove(struct spi_device *spi)
{
	struct ltv350qv *lcd = dev_get_drvdata(&spi->dev);

	ltv350qv_power(lcd, FB_BLANK_POWERDOWN);
	lcd_device_unregister(lcd->ld);
	kfree(lcd->buffer);
	kfree(lcd);

	return 0;
}

#ifdef CONFIG_PM
static int ltv350qv_suspend(struct spi_device *spi, pm_message_t state)
{
	struct ltv350qv *lcd = dev_get_drvdata(&spi->dev);

	return ltv350qv_power(lcd, FB_BLANK_POWERDOWN);
}

static int ltv350qv_resume(struct spi_device *spi)
{
	struct ltv350qv *lcd = dev_get_drvdata(&spi->dev);

	return ltv350qv_power(lcd, FB_BLANK_UNBLANK);
}
#else
#define ltv350qv_suspend	NULL
#define ltv350qv_resume		NULL
#endif

/* Power down all displays on reboot, poweroff or halt */
static void ltv350qv_shutdown(struct spi_device *spi)
{
	struct ltv350qv *lcd = dev_get_drvdata(&spi->dev);

	ltv350qv_power(lcd, FB_BLANK_POWERDOWN);
}

static struct spi_driver ltv350qv_driver = {
	.driver = {
		.name		= "ltv350qv",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= ltv350qv_probe,
	.remove		= __devexit_p(ltv350qv_remove),
	.shutdown	= ltv350qv_shutdown,
	.suspend	= ltv350qv_suspend,
	.resume		= ltv350qv_resume,
};

module_spi_driver(ltv350qv_driver);

MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_DESCRIPTION("Samsung LTV350QV LCD Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:ltv350qv");
