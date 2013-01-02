/*
 *  LCD/Backlight Driver for Sharp Zaurus Handhelds (various models)
 *
 *  Copyright (c) 2004-2006 Richard Purdie
 *
 *  Based on Sharp's 2.4 Backlight Driver
 *
 *  Copyright (c) 2008 Marvell International Ltd.
 *	Converted to SPI device based LCD/Backlight device driver
 *	by Eric Miao <eric.miao@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/fb.h>
#include <linux/lcd.h>
#include <linux/spi/spi.h>
#include <linux/spi/corgi_lcd.h>
#include <linux/slab.h>
#include <asm/mach/sharpsl_param.h>

#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

/* Register Addresses */
#define RESCTL_ADRS     0x00
#define PHACTRL_ADRS    0x01
#define DUTYCTRL_ADRS   0x02
#define POWERREG0_ADRS  0x03
#define POWERREG1_ADRS  0x04
#define GPOR3_ADRS      0x05
#define PICTRL_ADRS     0x06
#define POLCTRL_ADRS    0x07

/* Register Bit Definitions */
#define RESCTL_QVGA     0x01
#define RESCTL_VGA      0x00

#define POWER1_VW_ON    0x01  /* VW Supply FET ON */
#define POWER1_GVSS_ON  0x02  /* GVSS(-8V) Power Supply ON */
#define POWER1_VDD_ON   0x04  /* VDD(8V),SVSS(-4V) Power Supply ON */

#define POWER1_VW_OFF   0x00  /* VW Supply FET OFF */
#define POWER1_GVSS_OFF 0x00  /* GVSS(-8V) Power Supply OFF */
#define POWER1_VDD_OFF  0x00  /* VDD(8V),SVSS(-4V) Power Supply OFF */

#define POWER0_COM_DCLK 0x01  /* COM Voltage DC Bias DAC Serial Data Clock */
#define POWER0_COM_DOUT 0x02  /* COM Voltage DC Bias DAC Serial Data Out */
#define POWER0_DAC_ON   0x04  /* DAC Power Supply ON */
#define POWER0_COM_ON   0x08  /* COM Power Supply ON */
#define POWER0_VCC5_ON  0x10  /* VCC5 Power Supply ON */

#define POWER0_DAC_OFF  0x00  /* DAC Power Supply OFF */
#define POWER0_COM_OFF  0x00  /* COM Power Supply OFF */
#define POWER0_VCC5_OFF 0x00  /* VCC5 Power Supply OFF */

#define PICTRL_INIT_STATE      0x01
#define PICTRL_INIOFF          0x02
#define PICTRL_POWER_DOWN      0x04
#define PICTRL_COM_SIGNAL_OFF  0x08
#define PICTRL_DAC_SIGNAL_OFF  0x10

#define POLCTRL_SYNC_POL_FALL  0x01
#define POLCTRL_EN_POL_FALL    0x02
#define POLCTRL_DATA_POL_FALL  0x04
#define POLCTRL_SYNC_ACT_H     0x08
#define POLCTRL_EN_ACT_L       0x10

#define POLCTRL_SYNC_POL_RISE  0x00
#define POLCTRL_EN_POL_RISE    0x00
#define POLCTRL_DATA_POL_RISE  0x00
#define POLCTRL_SYNC_ACT_L     0x00
#define POLCTRL_EN_ACT_H       0x00

#define PHACTRL_PHASE_MANUAL   0x01
#define DEFAULT_PHAD_QVGA     (9)
#define DEFAULT_COMADJ        (125)

struct corgi_lcd {
	struct spi_device	*spi_dev;
	struct lcd_device	*lcd_dev;
	struct backlight_device	*bl_dev;

	int	limit_mask;
	int	intensity;
	int	power;
	int	mode;
	char	buf[2];

	int	gpio_backlight_on;
	int	gpio_backlight_cont;
	int	gpio_backlight_cont_inverted;

	void (*kick_battery)(void);
};

static int corgi_ssp_lcdtg_send(struct corgi_lcd *lcd, int reg, uint8_t val);

static struct corgi_lcd *the_corgi_lcd;
static unsigned long corgibl_flags;
#define CORGIBL_SUSPENDED     0x01
#define CORGIBL_BATTLOW       0x02

/*
 * This is only a pseudo I2C interface. We can't use the standard kernel
 * routines as the interface is write only. We just assume the data is acked...
 */
static void lcdtg_ssp_i2c_send(struct corgi_lcd *lcd, uint8_t data)
{
	corgi_ssp_lcdtg_send(lcd, POWERREG0_ADRS, data);
	udelay(10);
}

static void lcdtg_i2c_send_bit(struct corgi_lcd *lcd, uint8_t data)
{
	lcdtg_ssp_i2c_send(lcd, data);
	lcdtg_ssp_i2c_send(lcd, data | POWER0_COM_DCLK);
	lcdtg_ssp_i2c_send(lcd, data);
}

static void lcdtg_i2c_send_start(struct corgi_lcd *lcd, uint8_t base)
{
	lcdtg_ssp_i2c_send(lcd, base | POWER0_COM_DCLK | POWER0_COM_DOUT);
	lcdtg_ssp_i2c_send(lcd, base | POWER0_COM_DCLK);
	lcdtg_ssp_i2c_send(lcd, base);
}

static void lcdtg_i2c_send_stop(struct corgi_lcd *lcd, uint8_t base)
{
	lcdtg_ssp_i2c_send(lcd, base);
	lcdtg_ssp_i2c_send(lcd, base | POWER0_COM_DCLK);
	lcdtg_ssp_i2c_send(lcd, base | POWER0_COM_DCLK | POWER0_COM_DOUT);
}

static void lcdtg_i2c_send_byte(struct corgi_lcd *lcd,
				uint8_t base, uint8_t data)
{
	int i;
	for (i = 0; i < 8; i++) {
		if (data & 0x80)
			lcdtg_i2c_send_bit(lcd, base | POWER0_COM_DOUT);
		else
			lcdtg_i2c_send_bit(lcd, base);
		data <<= 1;
	}
}

static void lcdtg_i2c_wait_ack(struct corgi_lcd *lcd, uint8_t base)
{
	lcdtg_i2c_send_bit(lcd, base);
}

static void lcdtg_set_common_voltage(struct corgi_lcd *lcd,
				     uint8_t base_data, uint8_t data)
{
	/* Set Common Voltage to M62332FP via I2C */
	lcdtg_i2c_send_start(lcd, base_data);
	lcdtg_i2c_send_byte(lcd, base_data, 0x9c);
	lcdtg_i2c_wait_ack(lcd, base_data);
	lcdtg_i2c_send_byte(lcd, base_data, 0x00);
	lcdtg_i2c_wait_ack(lcd, base_data);
	lcdtg_i2c_send_byte(lcd, base_data, data);
	lcdtg_i2c_wait_ack(lcd, base_data);
	lcdtg_i2c_send_stop(lcd, base_data);
}

static int corgi_ssp_lcdtg_send(struct corgi_lcd *lcd, int adrs, uint8_t data)
{
	struct spi_message msg;
	struct spi_transfer xfer = {
		.len		= 1,
		.cs_change	= 1,
		.tx_buf		= lcd->buf,
	};

	lcd->buf[0] = ((adrs & 0x07) << 5) | (data & 0x1f);
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(lcd->spi_dev, &msg);
}

/* Set Phase Adjust */
static void lcdtg_set_phadadj(struct corgi_lcd *lcd, int mode)
{
	int adj;

	switch (mode) {
	case CORGI_LCD_MODE_VGA:
		/* Setting for VGA */
		adj = sharpsl_param.phadadj;
		adj = (adj < 0) ? PHACTRL_PHASE_MANUAL :
				  PHACTRL_PHASE_MANUAL | ((adj & 0xf) << 1);
		break;
	case CORGI_LCD_MODE_QVGA:
	default:
		/* Setting for QVGA */
		adj = (DEFAULT_PHAD_QVGA << 1) | PHACTRL_PHASE_MANUAL;
		break;
	}

	corgi_ssp_lcdtg_send(lcd, PHACTRL_ADRS, adj);
}

static void corgi_lcd_power_on(struct corgi_lcd *lcd)
{
	int comadj;

	/* Initialize Internal Logic & Port */
	corgi_ssp_lcdtg_send(lcd, PICTRL_ADRS,
			PICTRL_POWER_DOWN | PICTRL_INIOFF |
			PICTRL_INIT_STATE | PICTRL_COM_SIGNAL_OFF |
			PICTRL_DAC_SIGNAL_OFF);

	corgi_ssp_lcdtg_send(lcd, POWERREG0_ADRS,
			POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_OFF |
			POWER0_COM_OFF | POWER0_VCC5_OFF);

	corgi_ssp_lcdtg_send(lcd, POWERREG1_ADRS,
			POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_OFF);

	/* VDD(+8V), SVSS(-4V) ON */
	corgi_ssp_lcdtg_send(lcd, POWERREG1_ADRS,
			POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_ON);
	mdelay(3);

	/* DAC ON */
	corgi_ssp_lcdtg_send(lcd, POWERREG0_ADRS,
			POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_ON |
			POWER0_COM_OFF | POWER0_VCC5_OFF);

	/* INIB = H, INI = L  */
	/* PICTL[0] = H , PICTL[1] = PICTL[2] = PICTL[4] = L */
	corgi_ssp_lcdtg_send(lcd, PICTRL_ADRS,
			PICTRL_INIT_STATE | PICTRL_COM_SIGNAL_OFF);

	/* Set Common Voltage */
	comadj = sharpsl_param.comadj;
	if (comadj < 0)
		comadj = DEFAULT_COMADJ;

	lcdtg_set_common_voltage(lcd, POWER0_DAC_ON | POWER0_COM_OFF |
				 POWER0_VCC5_OFF, comadj);

	/* VCC5 ON, DAC ON */
	corgi_ssp_lcdtg_send(lcd, POWERREG0_ADRS,
			POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_ON |
			POWER0_COM_OFF | POWER0_VCC5_ON);

	/* GVSS(-8V) ON, VDD ON */
	corgi_ssp_lcdtg_send(lcd, POWERREG1_ADRS,
			POWER1_VW_OFF | POWER1_GVSS_ON | POWER1_VDD_ON);
	mdelay(2);

	/* COM SIGNAL ON (PICTL[3] = L) */
	corgi_ssp_lcdtg_send(lcd, PICTRL_ADRS, PICTRL_INIT_STATE);

	/* COM ON, DAC ON, VCC5_ON */
	corgi_ssp_lcdtg_send(lcd, POWERREG0_ADRS,
			POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_ON |
			POWER0_COM_ON | POWER0_VCC5_ON);

	/* VW ON, GVSS ON, VDD ON */
	corgi_ssp_lcdtg_send(lcd, POWERREG1_ADRS,
			POWER1_VW_ON | POWER1_GVSS_ON | POWER1_VDD_ON);

	/* Signals output enable */
	corgi_ssp_lcdtg_send(lcd, PICTRL_ADRS, 0);

	/* Set Phase Adjust */
	lcdtg_set_phadadj(lcd, lcd->mode);

	/* Initialize for Input Signals from ATI */
	corgi_ssp_lcdtg_send(lcd, POLCTRL_ADRS,
			POLCTRL_SYNC_POL_RISE | POLCTRL_EN_POL_RISE |
			POLCTRL_DATA_POL_RISE | POLCTRL_SYNC_ACT_L |
			POLCTRL_EN_ACT_H);
	udelay(1000);

	switch (lcd->mode) {
	case CORGI_LCD_MODE_VGA:
		corgi_ssp_lcdtg_send(lcd, RESCTL_ADRS, RESCTL_VGA);
		break;
	case CORGI_LCD_MODE_QVGA:
	default:
		corgi_ssp_lcdtg_send(lcd, RESCTL_ADRS, RESCTL_QVGA);
		break;
	}
}

static void corgi_lcd_power_off(struct corgi_lcd *lcd)
{
	/* 60Hz x 2 frame = 16.7msec x 2 = 33.4 msec */
	msleep(34);

	/* (1)VW OFF */
	corgi_ssp_lcdtg_send(lcd, POWERREG1_ADRS,
			POWER1_VW_OFF | POWER1_GVSS_ON | POWER1_VDD_ON);

	/* (2)COM OFF */
	corgi_ssp_lcdtg_send(lcd, PICTRL_ADRS, PICTRL_COM_SIGNAL_OFF);
	corgi_ssp_lcdtg_send(lcd, POWERREG0_ADRS,
			POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_ON);

	/* (3)Set Common Voltage Bias 0V */
	lcdtg_set_common_voltage(lcd, POWER0_DAC_ON | POWER0_COM_OFF |
			POWER0_VCC5_ON, 0);

	/* (4)GVSS OFF */
	corgi_ssp_lcdtg_send(lcd, POWERREG1_ADRS,
			POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_ON);

	/* (5)VCC5 OFF */
	corgi_ssp_lcdtg_send(lcd, POWERREG0_ADRS,
			POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_OFF);

	/* (6)Set PDWN, INIOFF, DACOFF */
	corgi_ssp_lcdtg_send(lcd, PICTRL_ADRS,
			PICTRL_INIOFF | PICTRL_DAC_SIGNAL_OFF |
			PICTRL_POWER_DOWN | PICTRL_COM_SIGNAL_OFF);

	/* (7)DAC OFF */
	corgi_ssp_lcdtg_send(lcd, POWERREG0_ADRS,
			POWER0_DAC_OFF | POWER0_COM_OFF | POWER0_VCC5_OFF);

	/* (8)VDD OFF */
	corgi_ssp_lcdtg_send(lcd, POWERREG1_ADRS,
			POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_OFF);
}

static int corgi_lcd_set_mode(struct lcd_device *ld, struct fb_videomode *m)
{
	struct corgi_lcd *lcd = dev_get_drvdata(&ld->dev);
	int mode = CORGI_LCD_MODE_QVGA;

	if (m->xres == 640 || m->xres == 480)
		mode = CORGI_LCD_MODE_VGA;

	if (lcd->mode == mode)
		return 0;

	lcdtg_set_phadadj(lcd, mode);

	switch (mode) {
	case CORGI_LCD_MODE_VGA:
		corgi_ssp_lcdtg_send(lcd, RESCTL_ADRS, RESCTL_VGA);
		break;
	case CORGI_LCD_MODE_QVGA:
	default:
		corgi_ssp_lcdtg_send(lcd, RESCTL_ADRS, RESCTL_QVGA);
		break;
	}

	lcd->mode = mode;
	return 0;
}

static int corgi_lcd_set_power(struct lcd_device *ld, int power)
{
	struct corgi_lcd *lcd = dev_get_drvdata(&ld->dev);

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		corgi_lcd_power_on(lcd);

	if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		corgi_lcd_power_off(lcd);

	lcd->power = power;
	return 0;
}

static int corgi_lcd_get_power(struct lcd_device *ld)
{
	struct corgi_lcd *lcd = dev_get_drvdata(&ld->dev);

	return lcd->power;
}

static struct lcd_ops corgi_lcd_ops = {
	.get_power	= corgi_lcd_get_power,
	.set_power	= corgi_lcd_set_power,
	.set_mode	= corgi_lcd_set_mode,
};

static int corgi_bl_get_intensity(struct backlight_device *bd)
{
	struct corgi_lcd *lcd = dev_get_drvdata(&bd->dev);

	return lcd->intensity;
}

static int corgi_bl_set_intensity(struct corgi_lcd *lcd, int intensity)
{
	int cont;

	if (intensity > 0x10)
		intensity += 0x10;

	corgi_ssp_lcdtg_send(lcd, DUTYCTRL_ADRS, intensity);

	/* Bit 5 via GPIO_BACKLIGHT_CONT */
	cont = !!(intensity & 0x20) ^ lcd->gpio_backlight_cont_inverted;

	if (gpio_is_valid(lcd->gpio_backlight_cont))
		gpio_set_value_cansleep(lcd->gpio_backlight_cont, cont);

	if (gpio_is_valid(lcd->gpio_backlight_on))
		gpio_set_value_cansleep(lcd->gpio_backlight_on, intensity);

	if (lcd->kick_battery)
		lcd->kick_battery();

	lcd->intensity = intensity;
	return 0;
}

static int corgi_bl_update_status(struct backlight_device *bd)
{
	struct corgi_lcd *lcd = dev_get_drvdata(&bd->dev);
	int intensity = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;

	if (bd->props.fb_blank != FB_BLANK_UNBLANK)
		intensity = 0;

	if (corgibl_flags & CORGIBL_SUSPENDED)
		intensity = 0;

	if ((corgibl_flags & CORGIBL_BATTLOW) && intensity > lcd->limit_mask)
		intensity = lcd->limit_mask;

	return corgi_bl_set_intensity(lcd, intensity);
}

void corgi_lcd_limit_intensity(int limit)
{
	if (limit)
		corgibl_flags |= CORGIBL_BATTLOW;
	else
		corgibl_flags &= ~CORGIBL_BATTLOW;

	backlight_update_status(the_corgi_lcd->bl_dev);
}
EXPORT_SYMBOL(corgi_lcd_limit_intensity);

static const struct backlight_ops corgi_bl_ops = {
	.get_brightness	= corgi_bl_get_intensity,
	.update_status  = corgi_bl_update_status,
};

#ifdef CONFIG_PM
static int corgi_lcd_suspend(struct spi_device *spi, pm_message_t state)
{
	struct corgi_lcd *lcd = dev_get_drvdata(&spi->dev);

	corgibl_flags |= CORGIBL_SUSPENDED;
	corgi_bl_set_intensity(lcd, 0);
	corgi_lcd_set_power(lcd->lcd_dev, FB_BLANK_POWERDOWN);
	return 0;
}

static int corgi_lcd_resume(struct spi_device *spi)
{
	struct corgi_lcd *lcd = dev_get_drvdata(&spi->dev);

	corgibl_flags &= ~CORGIBL_SUSPENDED;
	corgi_lcd_set_power(lcd->lcd_dev, FB_BLANK_UNBLANK);
	backlight_update_status(lcd->bl_dev);
	return 0;
}
#else
#define corgi_lcd_suspend	NULL
#define corgi_lcd_resume	NULL
#endif

static int setup_gpio_backlight(struct corgi_lcd *lcd,
				struct corgi_lcd_platform_data *pdata)
{
	struct spi_device *spi = lcd->spi_dev;
	int err;

	lcd->gpio_backlight_on = -1;
	lcd->gpio_backlight_cont = -1;

	if (gpio_is_valid(pdata->gpio_backlight_on)) {
		err = devm_gpio_request(&spi->dev, pdata->gpio_backlight_on,
					"BL_ON");
		if (err) {
			dev_err(&spi->dev,
				"failed to request GPIO%d for backlight_on\n",
				pdata->gpio_backlight_on);
			return err;
		}

		lcd->gpio_backlight_on = pdata->gpio_backlight_on;
		gpio_direction_output(lcd->gpio_backlight_on, 0);
	}

	if (gpio_is_valid(pdata->gpio_backlight_cont)) {
		err = devm_gpio_request(&spi->dev, pdata->gpio_backlight_cont,
					"BL_CONT");
		if (err) {
			dev_err(&spi->dev,
				"failed to request GPIO%d for backlight_cont\n",
				pdata->gpio_backlight_cont);
			return err;
		}

		lcd->gpio_backlight_cont = pdata->gpio_backlight_cont;

		/* spitz and akita use both GPIOs for backlight, and
		 * have inverted polarity of GPIO_BACKLIGHT_CONT
		 */
		if (gpio_is_valid(lcd->gpio_backlight_on)) {
			lcd->gpio_backlight_cont_inverted = 1;
			gpio_direction_output(lcd->gpio_backlight_cont, 1);
		} else {
			lcd->gpio_backlight_cont_inverted = 0;
			gpio_direction_output(lcd->gpio_backlight_cont, 0);
		}
	}
	return 0;
}

static int corgi_lcd_probe(struct spi_device *spi)
{
	struct backlight_properties props;
	struct corgi_lcd_platform_data *pdata = spi->dev.platform_data;
	struct corgi_lcd *lcd;
	int ret = 0;

	if (pdata == NULL) {
		dev_err(&spi->dev, "platform data not available\n");
		return -EINVAL;
	}

	lcd = devm_kzalloc(&spi->dev, sizeof(struct corgi_lcd), GFP_KERNEL);
	if (!lcd) {
		dev_err(&spi->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	lcd->spi_dev = spi;

	lcd->lcd_dev = lcd_device_register("corgi_lcd", &spi->dev,
					lcd, &corgi_lcd_ops);
	if (IS_ERR(lcd->lcd_dev))
		return PTR_ERR(lcd->lcd_dev);

	lcd->power = FB_BLANK_POWERDOWN;
	lcd->mode = (pdata) ? pdata->init_mode : CORGI_LCD_MODE_VGA;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = pdata->max_intensity;
	lcd->bl_dev = backlight_device_register("corgi_bl", &spi->dev, lcd,
						&corgi_bl_ops, &props);
	if (IS_ERR(lcd->bl_dev)) {
		ret = PTR_ERR(lcd->bl_dev);
		goto err_unregister_lcd;
	}
	lcd->bl_dev->props.brightness = pdata->default_intensity;
	lcd->bl_dev->props.power = FB_BLANK_UNBLANK;

	ret = setup_gpio_backlight(lcd, pdata);
	if (ret)
		goto err_unregister_bl;

	lcd->kick_battery = pdata->kick_battery;

	dev_set_drvdata(&spi->dev, lcd);
	corgi_lcd_set_power(lcd->lcd_dev, FB_BLANK_UNBLANK);
	backlight_update_status(lcd->bl_dev);

	lcd->limit_mask = pdata->limit_mask;
	the_corgi_lcd = lcd;
	return 0;

err_unregister_bl:
	backlight_device_unregister(lcd->bl_dev);
err_unregister_lcd:
	lcd_device_unregister(lcd->lcd_dev);
	return ret;
}

static int corgi_lcd_remove(struct spi_device *spi)
{
	struct corgi_lcd *lcd = dev_get_drvdata(&spi->dev);

	lcd->bl_dev->props.power = FB_BLANK_UNBLANK;
	lcd->bl_dev->props.brightness = 0;
	backlight_update_status(lcd->bl_dev);
	backlight_device_unregister(lcd->bl_dev);

	corgi_lcd_set_power(lcd->lcd_dev, FB_BLANK_POWERDOWN);
	lcd_device_unregister(lcd->lcd_dev);

	return 0;
}

static struct spi_driver corgi_lcd_driver = {
	.driver		= {
		.name	= "corgi-lcd",
		.owner	= THIS_MODULE,
	},
	.probe		= corgi_lcd_probe,
	.remove		= corgi_lcd_remove,
	.suspend	= corgi_lcd_suspend,
	.resume		= corgi_lcd_resume,
};

module_spi_driver(corgi_lcd_driver);

MODULE_DESCRIPTION("LCD and backlight driver for SHARP C7x0/Cxx00");
MODULE_AUTHOR("Eric Miao <eric.miao@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:corgi-lcd");
