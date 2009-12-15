/*
 * Base driver for Marvell 88PM8607
 *
 * Copyright (C) 2009 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm860x.h>

char pm860x_backlight_name[][MFD_NAME_SIZE] = {
	"backlight-0",
	"backlight-1",
	"backlight-2",
};
EXPORT_SYMBOL(pm860x_backlight_name);

char pm860x_led_name[][MFD_NAME_SIZE] = {
	"led0-red",
	"led0-green",
	"led0-blue",
	"led1-red",
	"led1-green",
	"led1-blue",
};
EXPORT_SYMBOL(pm860x_led_name);

#define PM8606_BACKLIGHT_RESOURCE(_i, _x)		\
{							\
	.name	= pm860x_backlight_name[_i],		\
	.start	= PM8606_##_x,				\
	.end	= PM8606_##_x,				\
	.flags	= IORESOURCE_IO,			\
}

static struct resource backlight_resources[] = {
	PM8606_BACKLIGHT_RESOURCE(PM8606_BACKLIGHT1, WLED1A),
	PM8606_BACKLIGHT_RESOURCE(PM8606_BACKLIGHT2, WLED2A),
	PM8606_BACKLIGHT_RESOURCE(PM8606_BACKLIGHT3, WLED3A),
};

#define PM8606_BACKLIGHT_DEVS(_i)			\
{							\
	.name		= "88pm860x-backlight",		\
	.num_resources	= 1,				\
	.resources	= &backlight_resources[_i],	\
	.id		= _i,				\
}

static struct mfd_cell backlight_devs[] = {
	PM8606_BACKLIGHT_DEVS(PM8606_BACKLIGHT1),
	PM8606_BACKLIGHT_DEVS(PM8606_BACKLIGHT2),
	PM8606_BACKLIGHT_DEVS(PM8606_BACKLIGHT3),
};

#define PM8606_LED_RESOURCE(_i, _x)			\
{							\
	.name	= pm860x_led_name[_i],			\
	.start	= PM8606_##_x,				\
	.end	= PM8606_##_x,				\
	.flags	= IORESOURCE_IO,			\
}

static struct resource led_resources[] = {
	PM8606_LED_RESOURCE(PM8606_LED1_RED, RGB2B),
	PM8606_LED_RESOURCE(PM8606_LED1_GREEN, RGB2C),
	PM8606_LED_RESOURCE(PM8606_LED1_BLUE, RGB2D),
	PM8606_LED_RESOURCE(PM8606_LED2_RED, RGB1B),
	PM8606_LED_RESOURCE(PM8606_LED2_GREEN, RGB1C),
	PM8606_LED_RESOURCE(PM8606_LED2_BLUE, RGB1D),
};

#define PM8606_LED_DEVS(_i)				\
{							\
	.name		= "88pm860x-led",		\
	.num_resources	= 1,				\
	.resources	= &led_resources[_i],		\
	.id		= _i,				\
}

static struct mfd_cell led_devs[] = {
	PM8606_LED_DEVS(PM8606_LED1_RED),
	PM8606_LED_DEVS(PM8606_LED1_GREEN),
	PM8606_LED_DEVS(PM8606_LED1_BLUE),
	PM8606_LED_DEVS(PM8606_LED2_RED),
	PM8606_LED_DEVS(PM8606_LED2_GREEN),
	PM8606_LED_DEVS(PM8606_LED2_BLUE),
};

static struct resource touch_resources[] = {
	{
		.start	= PM8607_IRQ_PEN,
		.end	= PM8607_IRQ_PEN,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mfd_cell touch_devs[] = {
	{
		.name		= "88pm860x-touch",
		.num_resources	= 1,
		.resources	= &touch_resources[0],
	},
};

#define PM8607_REG_RESOURCE(_start, _end)		\
{							\
	.start	= PM8607_##_start,			\
	.end	= PM8607_##_end,			\
	.flags	= IORESOURCE_IO,			\
}

static struct resource regulator_resources[] = {
	PM8607_REG_RESOURCE(BUCK1, BUCK1),
	PM8607_REG_RESOURCE(BUCK2, BUCK2),
	PM8607_REG_RESOURCE(BUCK3, BUCK3),
	PM8607_REG_RESOURCE(LDO1,  LDO1),
	PM8607_REG_RESOURCE(LDO2,  LDO2),
	PM8607_REG_RESOURCE(LDO3,  LDO3),
	PM8607_REG_RESOURCE(LDO4,  LDO4),
	PM8607_REG_RESOURCE(LDO5,  LDO5),
	PM8607_REG_RESOURCE(LDO6,  LDO6),
	PM8607_REG_RESOURCE(LDO7,  LDO7),
	PM8607_REG_RESOURCE(LDO8,  LDO8),
	PM8607_REG_RESOURCE(LDO9,  LDO9),
	PM8607_REG_RESOURCE(LDO10, LDO10),
	PM8607_REG_RESOURCE(LDO12, LDO12),
	PM8607_REG_RESOURCE(LDO14, LDO14),
};

#define PM8607_REG_DEVS(_name, _id)					\
{									\
	.name		= "88pm8607-" #_name,				\
	.num_resources	= 1,						\
	.resources	= &regulator_resources[PM8607_ID_##_id],	\
	.id		= PM8607_ID_##_id,				\
}

static struct mfd_cell regulator_devs[] = {
	PM8607_REG_DEVS(buck1, BUCK1),
	PM8607_REG_DEVS(buck2, BUCK2),
	PM8607_REG_DEVS(buck3, BUCK3),
	PM8607_REG_DEVS(ldo1,  LDO1),
	PM8607_REG_DEVS(ldo2,  LDO2),
	PM8607_REG_DEVS(ldo3,  LDO3),
	PM8607_REG_DEVS(ldo4,  LDO4),
	PM8607_REG_DEVS(ldo5,  LDO5),
	PM8607_REG_DEVS(ldo6,  LDO6),
	PM8607_REG_DEVS(ldo7,  LDO7),
	PM8607_REG_DEVS(ldo8,  LDO8),
	PM8607_REG_DEVS(ldo9,  LDO9),
	PM8607_REG_DEVS(ldo10, LDO10),
	PM8607_REG_DEVS(ldo12, LDO12),
	PM8607_REG_DEVS(ldo14, LDO14),
};

#define CHECK_IRQ(irq)					\
do {							\
	if ((irq < 0) || (irq >= PM860X_NUM_IRQ))	\
		return -EINVAL;				\
} while (0)

/* IRQs only occur on 88PM8607 */
int pm860x_mask_irq(struct pm860x_chip *chip, int irq)
{
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				: chip->companion;
	int offset, data, ret;

	CHECK_IRQ(irq);

	offset = (irq >> 3) + PM8607_INT_MASK_1;
	data = 1 << (irq % 8);
	ret = pm860x_set_bits(i2c, offset, data, 0);

	return ret;
}
EXPORT_SYMBOL(pm860x_mask_irq);

int pm860x_unmask_irq(struct pm860x_chip *chip, int irq)
{
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				: chip->companion;
	int offset, data, ret;

	CHECK_IRQ(irq);

	offset = (irq >> 3) + PM8607_INT_MASK_1;
	data = 1 << (irq % 8);
	ret = pm860x_set_bits(i2c, offset, data, data);

	return ret;
}
EXPORT_SYMBOL(pm860x_unmask_irq);

#define INT_STATUS_NUM		(3)

static irqreturn_t pm8607_irq_thread(int irq, void *data)
{
	DECLARE_BITMAP(irq_status, PM860X_NUM_IRQ);
	struct pm860x_chip *chip = data;
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				: chip->companion;
	unsigned char status_buf[INT_STATUS_NUM << 1];
	unsigned long value;
	int i, ret;

	irq_status[0] = 0;

	/* read out status register */
	ret = pm860x_bulk_read(i2c, PM8607_INT_STATUS1,
				INT_STATUS_NUM << 1, status_buf);
	if (ret < 0)
		goto out;
	if (chip->irq_mode) {
		/* 0, clear by read. 1, clear by write */
		ret = pm860x_bulk_write(i2c, PM8607_INT_STATUS1,
					INT_STATUS_NUM, status_buf);
		if (ret < 0)
			goto out;
	}

	/* clear masked interrupt status */
	for (i = 0, value = 0; i < INT_STATUS_NUM; i++) {
		status_buf[i] &= status_buf[i + INT_STATUS_NUM];
		irq_status[0] |= status_buf[i] << (i * 8);
	}

	while (!bitmap_empty(irq_status, PM860X_NUM_IRQ)) {
		irq = find_first_bit(irq_status, PM860X_NUM_IRQ);
		clear_bit(irq, irq_status);
		dev_dbg(chip->dev, "Servicing IRQ #%d\n", irq);

		mutex_lock(&chip->irq_lock);
		if (chip->irq[irq].handler)
			chip->irq[irq].handler(irq, chip->irq[irq].data);
		else {
			pm860x_mask_irq(chip, irq);
			dev_err(chip->dev, "Nobody cares IRQ %d. "
				"Now mask it.\n", irq);
			for (i = 0; i < (INT_STATUS_NUM << 1); i++) {
				dev_err(chip->dev, "status[%d]:%x\n", i,
					status_buf[i]);
			}
		}
		mutex_unlock(&chip->irq_lock);
	}
out:
	return IRQ_HANDLED;
}

int pm860x_request_irq(struct pm860x_chip *chip, int irq,
		       irq_handler_t handler, void *data)
{
	CHECK_IRQ(irq);
	if (!handler)
		return -EINVAL;

	mutex_lock(&chip->irq_lock);
	chip->irq[irq].handler = handler;
	chip->irq[irq].data = data;
	mutex_unlock(&chip->irq_lock);

	return 0;
}
EXPORT_SYMBOL(pm860x_request_irq);

int pm860x_free_irq(struct pm860x_chip *chip, int irq)
{
	CHECK_IRQ(irq);

	mutex_lock(&chip->irq_lock);
	chip->irq[irq].handler = NULL;
	chip->irq[irq].data = NULL;
	mutex_unlock(&chip->irq_lock);

	return 0;
}
EXPORT_SYMBOL(pm860x_free_irq);

static int __devinit device_gpadc_init(struct pm860x_chip *chip,
				       struct pm860x_platform_data *pdata)
{
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				: chip->companion;
	int use_gpadc = 0, data, ret;

	/* initialize GPADC without activating it */

	if (pdata && pdata->touch) {
		/* set GPADC MISC1 register */
		data = 0;
		data |= (pdata->touch->gpadc_prebias << 1)
			& PM8607_GPADC_PREBIAS_MASK;
		data |= (pdata->touch->slot_cycle << 3)
			& PM8607_GPADC_SLOT_CYCLE_MASK;
		data |= (pdata->touch->off_scale << 5)
			& PM8607_GPADC_OFF_SCALE_MASK;
		data |= (pdata->touch->sw_cal << 7)
			& PM8607_GPADC_SW_CAL_MASK;
		if (data) {
			ret = pm860x_reg_write(i2c, PM8607_GPADC_MISC1, data);
			if (ret < 0)
				goto out;
		}
		/* set tsi prebias time */
		if (pdata->touch->tsi_prebias) {
			data = pdata->touch->tsi_prebias;
			ret = pm860x_reg_write(i2c, PM8607_TSI_PREBIAS, data);
			if (ret < 0)
				goto out;
		}
		/* set prebias & prechg time of pen detect */
		data = 0;
		data |= pdata->touch->pen_prebias & PM8607_PD_PREBIAS_MASK;
		data |= (pdata->touch->pen_prechg << 5)
			& PM8607_PD_PRECHG_MASK;
		if (data) {
			ret = pm860x_reg_write(i2c, PM8607_PD_PREBIAS, data);
			if (ret < 0)
				goto out;
		}

		use_gpadc = 1;
	}

	/* turn on GPADC */
	if (use_gpadc) {
		ret = pm860x_set_bits(i2c, PM8607_GPADC_MISC1,
				      PM8607_GPADC_EN, PM8607_GPADC_EN);
	}
out:
	return ret;
}

static int __devinit device_irq_init(struct pm860x_chip *chip,
				     struct pm860x_platform_data *pdata)
{
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				: chip->companion;
	unsigned char status_buf[INT_STATUS_NUM];
	int data, mask, ret = -EINVAL;

	mutex_init(&chip->irq_lock);

	mask = PM8607_B0_MISC1_INV_INT | PM8607_B0_MISC1_INT_CLEAR
		| PM8607_B0_MISC1_INT_MASK;
	data = 0;
	chip->irq_mode = 0;
	if (pdata && pdata->irq_mode) {
		/*
		 * irq_mode defines the way of clearing interrupt. If it's 1,
		 * clear IRQ by write. Otherwise, clear it by read.
		 * This control bit is valid from 88PM8607 B0 steping.
		 */
		data |= PM8607_B0_MISC1_INT_CLEAR;
		chip->irq_mode = 1;
	}
	ret = pm860x_set_bits(i2c, PM8607_B0_MISC1, mask, data);
	if (ret < 0)
		goto out;

	/* mask all IRQs */
	memset(status_buf, 0, INT_STATUS_NUM);
	ret = pm860x_bulk_write(i2c, PM8607_INT_MASK_1,
				INT_STATUS_NUM, status_buf);
	if (ret < 0)
		goto out;

	if (chip->irq_mode) {
		/* clear interrupt status by write */
		memset(status_buf, 0xFF, INT_STATUS_NUM);
		ret = pm860x_bulk_write(i2c, PM8607_INT_STATUS1,
					INT_STATUS_NUM, status_buf);
	} else {
		/* clear interrupt status by read */
		ret = pm860x_bulk_read(i2c, PM8607_INT_STATUS1,
					INT_STATUS_NUM, status_buf);
	}
	if (ret < 0)
		goto out;

	memset(chip->irq, 0, sizeof(struct pm860x_irq) * PM860X_NUM_IRQ);

	ret = request_threaded_irq(i2c->irq, NULL, pm8607_irq_thread,
				IRQF_ONESHOT | IRQF_TRIGGER_LOW,
				"88PM8607", chip);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to request IRQ #%d.\n", i2c->irq);
		goto out;
	}
	chip->chip_irq = i2c->irq;
	return 0;
out:
	return ret;
}

static void __devexit device_irq_exit(struct pm860x_chip *chip)
{
	if (chip->chip_irq >= 0)
		free_irq(chip->chip_irq, chip);
}

static void __devinit device_8606_init(struct pm860x_chip *chip,
				       struct i2c_client *i2c,
				       struct pm860x_platform_data *pdata)
{
	int ret;

	if (pdata && pdata->backlight) {
		ret = mfd_add_devices(chip->dev, 0, &backlight_devs[0],
				      ARRAY_SIZE(backlight_devs),
				      &backlight_resources[0], 0);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add backlight "
				"subdev\n");
			goto out_dev;
		}
	}

	if (pdata && pdata->led) {
		ret = mfd_add_devices(chip->dev, 0, &led_devs[0],
				      ARRAY_SIZE(led_devs),
				      &led_resources[0], 0);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add led "
				"subdev\n");
			goto out_dev;
		}
	}
	return;
out_dev:
	mfd_remove_devices(chip->dev);
	device_irq_exit(chip);
}

static void __devinit device_8607_init(struct pm860x_chip *chip,
				       struct i2c_client *i2c,
				       struct pm860x_platform_data *pdata)
{
	int data, ret;

	ret = pm860x_reg_read(i2c, PM8607_CHIP_ID);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read CHIP ID: %d\n", ret);
		goto out;
	}
	if ((ret & PM8607_VERSION_MASK) == PM8607_VERSION)
		dev_info(chip->dev, "Marvell 88PM8607 (ID: %02x) detected\n",
			 ret);
	else {
		dev_err(chip->dev, "Failed to detect Marvell 88PM8607. "
			"Chip ID: %02x\n", ret);
		goto out;
	}

	ret = pm860x_reg_read(i2c, PM8607_BUCK3);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read BUCK3 register: %d\n", ret);
		goto out;
	}
	if (ret & PM8607_BUCK3_DOUBLE)
		chip->buck3_double = 1;

	ret = pm860x_reg_read(i2c, PM8607_B0_MISC1);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read MISC1 register: %d\n", ret);
		goto out;
	}

	if (pdata && (pdata->i2c_port == PI2C_PORT))
		data = PM8607_B0_MISC1_PI2C;
	else
		data = 0;
	ret = pm860x_set_bits(i2c, PM8607_B0_MISC1, PM8607_B0_MISC1_PI2C, data);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to access MISC1:%d\n", ret);
		goto out;
	}

	ret = device_gpadc_init(chip, pdata);
	if (ret < 0)
		goto out;

	ret = device_irq_init(chip, pdata);
	if (ret < 0)
		goto out;

	ret = mfd_add_devices(chip->dev, 0, &regulator_devs[0],
			      ARRAY_SIZE(regulator_devs),
			      &regulator_resources[0], 0);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add regulator subdev\n");
		goto out_dev;
	}

	if (pdata && pdata->touch) {
		ret = mfd_add_devices(chip->dev, 0, &touch_devs[0],
				      ARRAY_SIZE(touch_devs),
				      &touch_resources[0], 0);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add touch "
				"subdev\n");
			goto out_dev;
		}
	}
	return;
out_dev:
	mfd_remove_devices(chip->dev);
	device_irq_exit(chip);
out:
	return;
}

int pm860x_device_init(struct pm860x_chip *chip,
		       struct pm860x_platform_data *pdata)
{
	chip->chip_irq = -EINVAL;

	switch (chip->id) {
	case CHIP_PM8606:
		device_8606_init(chip, chip->client, pdata);
		break;
	case CHIP_PM8607:
		device_8607_init(chip, chip->client, pdata);
		break;
	}

	if (chip->companion) {
		switch (chip->id) {
		case CHIP_PM8607:
			device_8606_init(chip, chip->companion, pdata);
			break;
		case CHIP_PM8606:
			device_8607_init(chip, chip->companion, pdata);
			break;
		}
	}

	return 0;
}

void pm860x_device_exit(struct pm860x_chip *chip)
{
	device_irq_exit(chip);
	mfd_remove_devices(chip->dev);
}

MODULE_DESCRIPTION("PMIC Driver for Marvell 88PM860x");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
