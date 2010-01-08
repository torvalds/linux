/*
 * Base driver for Maxim MAX8925
 *
 * Copyright (C) 2009 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
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
#include <linux/mfd/max8925.h>

#define IRQ_MODE_STATUS		0
#define IRQ_MODE_MASK		1

static int __get_irq_offset(struct max8925_chip *chip, int irq, int mode,
			    int *offset, int *bit)
{
	if (!offset || !bit)
		return -EINVAL;

	switch (chip->chip_id) {
	case MAX8925_GPM:
		*bit = irq % BITS_PER_BYTE;
		if (irq < (BITS_PER_BYTE << 1)) {	/* irq = [0,15] */
			*offset = (mode) ? MAX8925_CHG_IRQ1_MASK
				: MAX8925_CHG_IRQ1;
			if (irq >= BITS_PER_BYTE)
				(*offset)++;
		} else {				/* irq = [16,31] */
			*offset = (mode) ? MAX8925_ON_OFF_IRQ1_MASK
				: MAX8925_ON_OFF_IRQ1;
			if (irq >= (BITS_PER_BYTE * 3))
				(*offset)++;
		}
		break;
	case MAX8925_ADC:
		*bit = irq % BITS_PER_BYTE;
		*offset = (mode) ? MAX8925_TSC_IRQ_MASK : MAX8925_TSC_IRQ;
		break;
	default:
		goto out;
	}
	return 0;
out:
	dev_err(chip->dev, "Wrong irq #%d is assigned\n", irq);
	return -EINVAL;
}

static int __check_irq(int irq)
{
	if ((irq < 0) || (irq >= MAX8925_NUM_IRQ))
		return -EINVAL;
	return 0;
}

int max8925_mask_irq(struct max8925_chip *chip, int irq)
{
	int offset, bit, ret;

	ret = __get_irq_offset(chip, irq, IRQ_MODE_MASK, &offset, &bit);
	if (ret < 0)
		return ret;
	ret = max8925_set_bits(chip->i2c, offset, 1 << bit, 1 << bit);
	return ret;
}

int max8925_unmask_irq(struct max8925_chip *chip, int irq)
{
	int offset, bit, ret;

	ret = __get_irq_offset(chip, irq, IRQ_MODE_MASK, &offset, &bit);
	if (ret < 0)
		return ret;
	ret = max8925_set_bits(chip->i2c, offset, 1 << bit, 0);
	return ret;
}

#define INT_STATUS_NUM		(MAX8925_NUM_IRQ / BITS_PER_BYTE)

static irqreturn_t max8925_irq_thread(int irq, void *data)
{
	struct max8925_chip *chip = data;
	unsigned long irq_status[INT_STATUS_NUM];
	unsigned char status_buf[INT_STATUS_NUM << 1];
	int i, ret;

	memset(irq_status, 0, sizeof(unsigned long) * INT_STATUS_NUM);

	/* all these interrupt status registers are read-only */
	switch (chip->chip_id) {
	case MAX8925_GPM:
		ret = max8925_bulk_read(chip->i2c, MAX8925_CHG_IRQ1,
					4, status_buf);
		if (ret < 0)
			goto out;
		ret = max8925_bulk_read(chip->i2c, MAX8925_ON_OFF_IRQ1,
					2, &status_buf[4]);
		if (ret < 0)
			goto out;
		ret = max8925_bulk_read(chip->i2c, MAX8925_ON_OFF_IRQ2,
					2, &status_buf[6]);
		if (ret < 0)
			goto out;
		/* clear masked interrupt status */
		status_buf[0] &= (~status_buf[2] & CHG_IRQ1_MASK);
		irq_status[0] |= status_buf[0];
		status_buf[1] &= (~status_buf[3] & CHG_IRQ2_MASK);
		irq_status[0] |= (status_buf[1] << BITS_PER_BYTE);
		status_buf[4] &= (~status_buf[5] & ON_OFF_IRQ1_MASK);
		irq_status[0] |= (status_buf[4] << (BITS_PER_BYTE * 2));
		status_buf[6] &= (~status_buf[7] & ON_OFF_IRQ2_MASK);
		irq_status[0] |= (status_buf[6] << (BITS_PER_BYTE * 3));
		break;
	case MAX8925_ADC:
		ret = max8925_bulk_read(chip->i2c, MAX8925_TSC_IRQ,
					2, status_buf);
		if (ret < 0)
			goto out;
		/* clear masked interrupt status */
		status_buf[0] &= (~status_buf[1] & TSC_IRQ_MASK);
		irq_status[0] |= status_buf[0];
		break;
	default:
		goto out;
	}

	for_each_bit(i, &irq_status[0], MAX8925_NUM_IRQ) {
		clear_bit(i, irq_status);
		dev_dbg(chip->dev, "Servicing IRQ #%d in %s\n", i, chip->name);

		mutex_lock(&chip->irq_lock);
		if (chip->irq[i].handler)
			chip->irq[i].handler(i, chip->irq[i].data);
		else {
			max8925_mask_irq(chip, i);
			dev_err(chip->dev, "Noboday cares IRQ #%d in %s. "
				"Now mask it.\n", i, chip->name);
		}
		mutex_unlock(&chip->irq_lock);
	}
out:
	return IRQ_HANDLED;
}

int max8925_request_irq(struct max8925_chip *chip, int irq,
			irq_handler_t handler, void *data)
{
	if ((__check_irq(irq) < 0) || !handler)
		return -EINVAL;

	mutex_lock(&chip->irq_lock);
	chip->irq[irq].handler = handler;
	chip->irq[irq].data = data;
	mutex_unlock(&chip->irq_lock);
	return 0;
}
EXPORT_SYMBOL(max8925_request_irq);

int max8925_free_irq(struct max8925_chip *chip, int irq)
{
	if (__check_irq(irq) < 0)
		return -EINVAL;

	mutex_lock(&chip->irq_lock);
	chip->irq[irq].handler = NULL;
	chip->irq[irq].data = NULL;
	mutex_unlock(&chip->irq_lock);
	return 0;
}
EXPORT_SYMBOL(max8925_free_irq);

static int __devinit device_gpm_init(struct max8925_chip *chip,
				      struct i2c_client *i2c,
				      struct max8925_platform_data *pdata)
{
	int ret;

	/* mask all IRQs */
	ret = max8925_set_bits(i2c, MAX8925_CHG_IRQ1_MASK, 0x7, 0x7);
	if (ret < 0)
		goto out;
	ret = max8925_set_bits(i2c, MAX8925_CHG_IRQ2_MASK, 0xff, 0xff);
	if (ret < 0)
		goto out;
	ret = max8925_set_bits(i2c, MAX8925_ON_OFF_IRQ1_MASK, 0xff, 0xff);
	if (ret < 0)
		goto out;
	ret = max8925_set_bits(i2c, MAX8925_ON_OFF_IRQ2_MASK, 0x3, 0x3);
	if (ret < 0)
		goto out;

	chip->name = "GPM";
	memset(chip->irq, 0, sizeof(struct max8925_irq) * MAX8925_NUM_IRQ);
	ret = request_threaded_irq(i2c->irq, NULL, max8925_irq_thread,
				IRQF_ONESHOT | IRQF_TRIGGER_LOW,
				"max8925-gpm", chip);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to request IRQ #%d.\n", i2c->irq);
		goto out;
	}
	chip->chip_irq = i2c->irq;

	/* enable hard-reset for ONKEY power-off */
	max8925_set_bits(i2c, MAX8925_SYSENSEL, 0x80, 0x80);
out:
	return ret;
}

static int __devinit device_adc_init(struct max8925_chip *chip,
				     struct i2c_client *i2c,
				     struct max8925_platform_data *pdata)
{
	int ret;

	/* mask all IRQs */
	ret = max8925_set_bits(i2c, MAX8925_TSC_IRQ_MASK, 3, 3);

	chip->name = "ADC";
	memset(chip->irq, 0, sizeof(struct max8925_irq) * MAX8925_NUM_IRQ);
	ret = request_threaded_irq(i2c->irq, NULL, max8925_irq_thread,
				IRQF_ONESHOT | IRQF_TRIGGER_LOW,
				"max8925-adc", chip);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to request IRQ #%d.\n", i2c->irq);
		goto out;
	}
	chip->chip_irq = i2c->irq;
out:
	return ret;
}

int __devinit max8925_device_init(struct max8925_chip *chip,
				  struct max8925_platform_data *pdata)
{
	switch (chip->chip_id) {
	case MAX8925_GPM:
		device_gpm_init(chip, chip->i2c, pdata);
		break;
	case MAX8925_ADC:
		device_adc_init(chip, chip->i2c, pdata);
		break;
	}
	return 0;
}

void max8925_device_exit(struct max8925_chip *chip)
{
	if (chip->chip_irq >= 0)
		free_irq(chip->chip_irq, chip);
}

MODULE_DESCRIPTION("PMIC Driver for Maxim MAX8925");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com");
MODULE_LICENSE("GPL");
