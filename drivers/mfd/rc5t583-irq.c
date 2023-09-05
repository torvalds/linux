// SPDX-License-Identifier: GPL-2.0-only
/*
 * Interrupt driver for RICOH583 power management chip.
 *
 * Copyright (c) 2011-2012, NVIDIA CORPORATION.  All rights reserved.
 * Author: Laxman dewangan <ldewangan@nvidia.com>
 *
 * based on code
 *      Copyright (C) 2011 RICOH COMPANY,LTD
 */
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/rc5t583.h>

enum int_type {
	SYS_INT  = 0x1,
	DCDC_INT = 0x2,
	RTC_INT  = 0x4,
	ADC_INT  = 0x8,
	GPIO_INT = 0x10,
};

static int gpedge_add[] = {
	RC5T583_GPIO_GPEDGE2,
	RC5T583_GPIO_GPEDGE2
};

static int irq_en_add[] = {
	RC5T583_INT_EN_SYS1,
	RC5T583_INT_EN_SYS2,
	RC5T583_INT_EN_DCDC,
	RC5T583_INT_EN_RTC,
	RC5T583_INT_EN_ADC1,
	RC5T583_INT_EN_ADC2,
	RC5T583_INT_EN_ADC3,
	RC5T583_GPIO_EN_INT
};

static int irq_mon_add[] = {
	RC5T583_INT_MON_SYS1,
	RC5T583_INT_MON_SYS2,
	RC5T583_INT_MON_DCDC,
	RC5T583_INT_MON_RTC,
	RC5T583_INT_IR_ADCL,
	RC5T583_INT_IR_ADCH,
	RC5T583_INT_IR_ADCEND,
	RC5T583_INT_IR_GPIOF,
	RC5T583_INT_IR_GPIOR
};

static int irq_clr_add[] = {
	RC5T583_INT_IR_SYS1,
	RC5T583_INT_IR_SYS2,
	RC5T583_INT_IR_DCDC,
	RC5T583_INT_IR_RTC,
	RC5T583_INT_IR_ADCL,
	RC5T583_INT_IR_ADCH,
	RC5T583_INT_IR_ADCEND,
	RC5T583_INT_IR_GPIOF,
	RC5T583_INT_IR_GPIOR
};

static int main_int_type[] = {
	SYS_INT,
	SYS_INT,
	DCDC_INT,
	RTC_INT,
	ADC_INT,
	ADC_INT,
	ADC_INT,
	GPIO_INT,
	GPIO_INT,
};

struct rc5t583_irq_data {
	u8	int_type;
	u8	master_bit;
	u8	int_en_bit;
	u8	mask_reg_index;
	int	grp_index;
};

#define RC5T583_IRQ(_int_type, _master_bit, _grp_index, \
			_int_bit, _mask_ind)		\
	{						\
		.int_type	= _int_type,		\
		.master_bit	= _master_bit,		\
		.grp_index	= _grp_index,		\
		.int_en_bit	= _int_bit,		\
		.mask_reg_index	= _mask_ind,		\
	}

static const struct rc5t583_irq_data rc5t583_irqs[RC5T583_MAX_IRQS] = {
	[RC5T583_IRQ_ONKEY]		= RC5T583_IRQ(SYS_INT,  0, 0, 0, 0),
	[RC5T583_IRQ_ACOK]		= RC5T583_IRQ(SYS_INT,  0, 1, 1, 0),
	[RC5T583_IRQ_LIDOPEN]		= RC5T583_IRQ(SYS_INT,  0, 2, 2, 0),
	[RC5T583_IRQ_PREOT]		= RC5T583_IRQ(SYS_INT,  0, 3, 3, 0),
	[RC5T583_IRQ_CLKSTP]		= RC5T583_IRQ(SYS_INT,  0, 4, 4, 0),
	[RC5T583_IRQ_ONKEY_OFF]		= RC5T583_IRQ(SYS_INT,  0, 5, 5, 0),
	[RC5T583_IRQ_WD]		= RC5T583_IRQ(SYS_INT,  0, 7, 7, 0),
	[RC5T583_IRQ_EN_PWRREQ1]	= RC5T583_IRQ(SYS_INT,  0, 8, 0, 1),
	[RC5T583_IRQ_EN_PWRREQ2]	= RC5T583_IRQ(SYS_INT,  0, 9, 1, 1),
	[RC5T583_IRQ_PRE_VINDET]	= RC5T583_IRQ(SYS_INT,  0, 10, 2, 1),

	[RC5T583_IRQ_DC0LIM]		= RC5T583_IRQ(DCDC_INT, 1, 0, 0, 2),
	[RC5T583_IRQ_DC1LIM]		= RC5T583_IRQ(DCDC_INT, 1, 1, 1, 2),
	[RC5T583_IRQ_DC2LIM]		= RC5T583_IRQ(DCDC_INT, 1, 2, 2, 2),
	[RC5T583_IRQ_DC3LIM]		= RC5T583_IRQ(DCDC_INT, 1, 3, 3, 2),

	[RC5T583_IRQ_CTC]		= RC5T583_IRQ(RTC_INT,  2, 0, 0, 3),
	[RC5T583_IRQ_YALE]		= RC5T583_IRQ(RTC_INT,  2, 5, 5, 3),
	[RC5T583_IRQ_DALE]		= RC5T583_IRQ(RTC_INT,  2, 6, 6, 3),
	[RC5T583_IRQ_WALE]		= RC5T583_IRQ(RTC_INT,  2, 7, 7, 3),

	[RC5T583_IRQ_AIN1L]		= RC5T583_IRQ(ADC_INT,  3, 0, 0, 4),
	[RC5T583_IRQ_AIN2L]		= RC5T583_IRQ(ADC_INT,  3, 1, 1, 4),
	[RC5T583_IRQ_AIN3L]		= RC5T583_IRQ(ADC_INT,  3, 2, 2, 4),
	[RC5T583_IRQ_VBATL]		= RC5T583_IRQ(ADC_INT,  3, 3, 3, 4),
	[RC5T583_IRQ_VIN3L]		= RC5T583_IRQ(ADC_INT,  3, 4, 4, 4),
	[RC5T583_IRQ_VIN8L]		= RC5T583_IRQ(ADC_INT,  3, 5, 5, 4),
	[RC5T583_IRQ_AIN1H]		= RC5T583_IRQ(ADC_INT,  3, 6, 0, 5),
	[RC5T583_IRQ_AIN2H]		= RC5T583_IRQ(ADC_INT,  3, 7, 1, 5),
	[RC5T583_IRQ_AIN3H]		= RC5T583_IRQ(ADC_INT,  3, 8, 2, 5),
	[RC5T583_IRQ_VBATH]		= RC5T583_IRQ(ADC_INT,  3, 9, 3, 5),
	[RC5T583_IRQ_VIN3H]		= RC5T583_IRQ(ADC_INT,  3, 10, 4, 5),
	[RC5T583_IRQ_VIN8H]		= RC5T583_IRQ(ADC_INT,  3, 11, 5, 5),
	[RC5T583_IRQ_ADCEND]		= RC5T583_IRQ(ADC_INT,  3, 12, 0, 6),

	[RC5T583_IRQ_GPIO0]		= RC5T583_IRQ(GPIO_INT, 4, 0, 0, 7),
	[RC5T583_IRQ_GPIO1]		= RC5T583_IRQ(GPIO_INT, 4, 1, 1, 7),
	[RC5T583_IRQ_GPIO2]		= RC5T583_IRQ(GPIO_INT, 4, 2, 2, 7),
	[RC5T583_IRQ_GPIO3]		= RC5T583_IRQ(GPIO_INT, 4, 3, 3, 7),
	[RC5T583_IRQ_GPIO4]		= RC5T583_IRQ(GPIO_INT, 4, 4, 4, 7),
	[RC5T583_IRQ_GPIO5]		= RC5T583_IRQ(GPIO_INT, 4, 5, 5, 7),
	[RC5T583_IRQ_GPIO6]		= RC5T583_IRQ(GPIO_INT, 4, 6, 6, 7),
	[RC5T583_IRQ_GPIO7]		= RC5T583_IRQ(GPIO_INT, 4, 7, 7, 7),
};

static void rc5t583_irq_lock(struct irq_data *irq_data)
{
	struct rc5t583 *rc5t583 = irq_data_get_irq_chip_data(irq_data);
	mutex_lock(&rc5t583->irq_lock);
}

static void rc5t583_irq_unmask(struct irq_data *irq_data)
{
	struct rc5t583 *rc5t583 = irq_data_get_irq_chip_data(irq_data);
	unsigned int __irq = irq_data->irq - rc5t583->irq_base;
	const struct rc5t583_irq_data *data = &rc5t583_irqs[__irq];

	rc5t583->group_irq_en[data->grp_index] |= 1 << data->grp_index;
	rc5t583->intc_inten_reg |= 1 << data->master_bit;
	rc5t583->irq_en_reg[data->mask_reg_index] |= 1 << data->int_en_bit;
}

static void rc5t583_irq_mask(struct irq_data *irq_data)
{
	struct rc5t583 *rc5t583 = irq_data_get_irq_chip_data(irq_data);
	unsigned int __irq = irq_data->irq - rc5t583->irq_base;
	const struct rc5t583_irq_data *data = &rc5t583_irqs[__irq];

	rc5t583->group_irq_en[data->grp_index] &= ~(1 << data->grp_index);
	if (!rc5t583->group_irq_en[data->grp_index])
		rc5t583->intc_inten_reg &= ~(1 << data->master_bit);

	rc5t583->irq_en_reg[data->mask_reg_index] &= ~(1 << data->int_en_bit);
}

static int rc5t583_irq_set_type(struct irq_data *irq_data, unsigned int type)
{
	struct rc5t583 *rc5t583 = irq_data_get_irq_chip_data(irq_data);
	unsigned int __irq = irq_data->irq - rc5t583->irq_base;
	const struct rc5t583_irq_data *data = &rc5t583_irqs[__irq];
	int val = 0;
	int gpedge_index;
	int gpedge_bit_pos;

	/* Supporting only trigger level inetrrupt */
	if ((data->int_type & GPIO_INT) && (type & IRQ_TYPE_EDGE_BOTH)) {
		gpedge_index = data->int_en_bit / 4;
		gpedge_bit_pos = data->int_en_bit % 4;

		if (type & IRQ_TYPE_EDGE_FALLING)
			val |= 0x2;

		if (type & IRQ_TYPE_EDGE_RISING)
			val |= 0x1;

		rc5t583->gpedge_reg[gpedge_index] &= ~(3 << gpedge_bit_pos);
		rc5t583->gpedge_reg[gpedge_index] |= (val << gpedge_bit_pos);
		rc5t583_irq_unmask(irq_data);
		return 0;
	}
	return -EINVAL;
}

static void rc5t583_irq_sync_unlock(struct irq_data *irq_data)
{
	struct rc5t583 *rc5t583 = irq_data_get_irq_chip_data(irq_data);
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(rc5t583->gpedge_reg); i++) {
		ret = rc5t583_write(rc5t583->dev, gpedge_add[i],
				rc5t583->gpedge_reg[i]);
		if (ret < 0)
			dev_warn(rc5t583->dev,
				"Error in writing reg 0x%02x error: %d\n",
				gpedge_add[i], ret);
	}

	for (i = 0; i < ARRAY_SIZE(rc5t583->irq_en_reg); i++) {
		ret = rc5t583_write(rc5t583->dev, irq_en_add[i],
					rc5t583->irq_en_reg[i]);
		if (ret < 0)
			dev_warn(rc5t583->dev,
				"Error in writing reg 0x%02x error: %d\n",
				irq_en_add[i], ret);
	}

	ret = rc5t583_write(rc5t583->dev, RC5T583_INTC_INTEN,
				rc5t583->intc_inten_reg);
	if (ret < 0)
		dev_warn(rc5t583->dev,
			"Error in writing reg 0x%02x error: %d\n",
			RC5T583_INTC_INTEN, ret);

	mutex_unlock(&rc5t583->irq_lock);
}

static int rc5t583_irq_set_wake(struct irq_data *irq_data, unsigned int on)
{
	struct rc5t583 *rc5t583 = irq_data_get_irq_chip_data(irq_data);
	return irq_set_irq_wake(rc5t583->chip_irq, on);
}

static irqreturn_t rc5t583_irq(int irq, void *data)
{
	struct rc5t583 *rc5t583 = data;
	uint8_t int_sts[RC5T583_MAX_INTERRUPT_MASK_REGS];
	uint8_t master_int = 0;
	int i;
	int ret;
	unsigned int rtc_int_sts = 0;

	/* Clear the status */
	for (i = 0; i < RC5T583_MAX_INTERRUPT_MASK_REGS; i++)
		int_sts[i] = 0;

	ret  = rc5t583_read(rc5t583->dev, RC5T583_INTC_INTMON, &master_int);
	if (ret < 0) {
		dev_err(rc5t583->dev,
			"Error in reading reg 0x%02x error: %d\n",
			RC5T583_INTC_INTMON, ret);
		return IRQ_HANDLED;
	}

	for (i = 0; i < RC5T583_MAX_INTERRUPT_MASK_REGS; ++i) {
		if (!(master_int & main_int_type[i]))
			continue;

		ret = rc5t583_read(rc5t583->dev, irq_mon_add[i], &int_sts[i]);
		if (ret < 0) {
			dev_warn(rc5t583->dev,
				"Error in reading reg 0x%02x error: %d\n",
				irq_mon_add[i], ret);
			int_sts[i] = 0;
			continue;
		}

		if (main_int_type[i] & RTC_INT) {
			rtc_int_sts = 0;
			if (int_sts[i] & 0x1)
				rtc_int_sts |= BIT(6);
			if (int_sts[i] & 0x2)
				rtc_int_sts |= BIT(7);
			if (int_sts[i] & 0x4)
				rtc_int_sts |= BIT(0);
			if (int_sts[i] & 0x8)
				rtc_int_sts |= BIT(5);
		}

		ret = rc5t583_write(rc5t583->dev, irq_clr_add[i],
				~int_sts[i]);
		if (ret < 0)
			dev_warn(rc5t583->dev,
				"Error in reading reg 0x%02x error: %d\n",
				irq_clr_add[i], ret);

		if (main_int_type[i] & RTC_INT)
			int_sts[i] = rtc_int_sts;
	}

	/* Merge gpio interrupts for rising and falling case*/
	int_sts[7] |= int_sts[8];

	/* Call interrupt handler if enabled */
	for (i = 0; i < RC5T583_MAX_IRQS; ++i) {
		const struct rc5t583_irq_data *data = &rc5t583_irqs[i];
		if ((int_sts[data->mask_reg_index] & (1 << data->int_en_bit)) &&
			(rc5t583->group_irq_en[data->master_bit] &
					(1 << data->grp_index)))
			handle_nested_irq(rc5t583->irq_base + i);
	}

	return IRQ_HANDLED;
}

static struct irq_chip rc5t583_irq_chip = {
	.name = "rc5t583-irq",
	.irq_mask = rc5t583_irq_mask,
	.irq_unmask = rc5t583_irq_unmask,
	.irq_bus_lock = rc5t583_irq_lock,
	.irq_bus_sync_unlock = rc5t583_irq_sync_unlock,
	.irq_set_type = rc5t583_irq_set_type,
	.irq_set_wake = pm_sleep_ptr(rc5t583_irq_set_wake),
};

int rc5t583_irq_init(struct rc5t583 *rc5t583, int irq, int irq_base)
{
	int i, ret;

	if (!irq_base) {
		dev_warn(rc5t583->dev, "No interrupt support on IRQ base\n");
		return -EINVAL;
	}

	mutex_init(&rc5t583->irq_lock);

	/* Initailize all int register to 0 */
	for (i = 0; i < RC5T583_MAX_INTERRUPT_EN_REGS; i++)  {
		ret = rc5t583_write(rc5t583->dev, irq_en_add[i],
				rc5t583->irq_en_reg[i]);
		if (ret < 0)
			dev_warn(rc5t583->dev,
				"Error in writing reg 0x%02x error: %d\n",
				irq_en_add[i], ret);
	}

	for (i = 0; i < RC5T583_MAX_GPEDGE_REG; i++)  {
		ret = rc5t583_write(rc5t583->dev, gpedge_add[i],
				rc5t583->gpedge_reg[i]);
		if (ret < 0)
			dev_warn(rc5t583->dev,
				"Error in writing reg 0x%02x error: %d\n",
				gpedge_add[i], ret);
	}

	ret = rc5t583_write(rc5t583->dev, RC5T583_INTC_INTEN, 0x0);
	if (ret < 0)
		dev_warn(rc5t583->dev,
			"Error in writing reg 0x%02x error: %d\n",
			RC5T583_INTC_INTEN, ret);

	/* Clear all interrupts in case they woke up active. */
	for (i = 0; i < RC5T583_MAX_INTERRUPT_MASK_REGS; i++)  {
		ret = rc5t583_write(rc5t583->dev, irq_clr_add[i], 0);
		if (ret < 0)
			dev_warn(rc5t583->dev,
				"Error in writing reg 0x%02x error: %d\n",
				irq_clr_add[i], ret);
	}

	rc5t583->irq_base = irq_base;
	rc5t583->chip_irq = irq;

	for (i = 0; i < RC5T583_MAX_IRQS; i++) {
		int __irq = i + rc5t583->irq_base;
		irq_set_chip_data(__irq, rc5t583);
		irq_set_chip_and_handler(__irq, &rc5t583_irq_chip,
					 handle_simple_irq);
		irq_set_nested_thread(__irq, 1);
		irq_clear_status_flags(__irq, IRQ_NOREQUEST);
	}

	ret = devm_request_threaded_irq(rc5t583->dev, irq, NULL, rc5t583_irq,
					IRQF_ONESHOT, "rc5t583", rc5t583);
	if (ret < 0)
		dev_err(rc5t583->dev,
			"Error in registering interrupt error: %d\n", ret);
	return ret;
}
