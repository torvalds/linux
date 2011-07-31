/*
 * arch/arm/mach-s5pc100/irq-gpio.c
 *
 * Copyright (C) 2009 Samsung Electronics
 *
 * S5PC100 - Interrupt handling for IRQ_GPIO${group}(x)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <mach/map.h>
#include <plat/gpio-cfg.h>

#define S5P_GPIOREG(x)		(S5P_VA_GPIO + (x))

#define CON_OFFSET			0x700
#define MASK_OFFSET			0x900
#define PEND_OFFSET			0xA00
#define CON_OFFSET_2			0xE00
#define MASK_OFFSET_2			0xF00
#define PEND_OFFSET_2			0xF40

#define GPIOINT_LEVEL_LOW		0x0
#define GPIOINT_LEVEL_HIGH		0x1
#define GPIOINT_EDGE_FALLING		0x2
#define GPIOINT_EDGE_RISING		0x3
#define GPIOINT_EDGE_BOTH		0x4

static int group_to_con_offset(int group)
{
	return group << 2;
}

static int group_to_mask_offset(int group)
{
	return group << 2;
}

static int group_to_pend_offset(int group)
{
	return group << 2;
}

static int s5pc100_get_start(unsigned int group)
{
	switch (group) {
	case 0: return S5PC100_GPIO_A0_START;
	case 1: return S5PC100_GPIO_A1_START;
	case 2: return S5PC100_GPIO_B_START;
	case 3: return S5PC100_GPIO_C_START;
	case 4: return S5PC100_GPIO_D_START;
	case 5: return S5PC100_GPIO_E0_START;
	case 6: return S5PC100_GPIO_E1_START;
	case 7: return S5PC100_GPIO_F0_START;
	case 8: return S5PC100_GPIO_F1_START;
	case 9: return S5PC100_GPIO_F2_START;
	case 10: return S5PC100_GPIO_F3_START;
	case 11: return S5PC100_GPIO_G0_START;
	case 12: return S5PC100_GPIO_G1_START;
	case 13: return S5PC100_GPIO_G2_START;
	case 14: return S5PC100_GPIO_G3_START;
	case 15: return S5PC100_GPIO_I_START;
	case 16: return S5PC100_GPIO_J0_START;
	case 17: return S5PC100_GPIO_J1_START;
	case 18: return S5PC100_GPIO_J2_START;
	case 19: return S5PC100_GPIO_J3_START;
	case 20: return S5PC100_GPIO_J4_START;
	default:
		BUG();
	}

	return -EINVAL;
}

static int s5pc100_get_group(unsigned int irq)
{
	irq -= S3C_IRQ_GPIO(0);

	switch (irq) {
	case S5PC100_GPIO_A0_START ... S5PC100_GPIO_A1_START - 1:
		return 0;
	case S5PC100_GPIO_A1_START ... S5PC100_GPIO_B_START - 1:
		return 1;
	case S5PC100_GPIO_B_START ... S5PC100_GPIO_C_START - 1:
		return 2;
	case S5PC100_GPIO_C_START ... S5PC100_GPIO_D_START - 1:
		return 3;
	case S5PC100_GPIO_D_START ... S5PC100_GPIO_E0_START - 1:
		return 4;
	case S5PC100_GPIO_E0_START ... S5PC100_GPIO_E1_START - 1:
		return 5;
	case S5PC100_GPIO_E1_START ... S5PC100_GPIO_F0_START - 1:
		return 6;
	case S5PC100_GPIO_F0_START ... S5PC100_GPIO_F1_START - 1:
		return 7;
	case S5PC100_GPIO_F1_START ... S5PC100_GPIO_F2_START - 1:
		return 8;
	case S5PC100_GPIO_F2_START ... S5PC100_GPIO_F3_START - 1:
		return 9;
	case S5PC100_GPIO_F3_START ... S5PC100_GPIO_G0_START - 1:
		return 10;
	case S5PC100_GPIO_G0_START ... S5PC100_GPIO_G1_START - 1:
		return 11;
	case S5PC100_GPIO_G1_START ... S5PC100_GPIO_G2_START - 1:
		return 12;
	case S5PC100_GPIO_G2_START ... S5PC100_GPIO_G3_START - 1:
		return 13;
	case S5PC100_GPIO_G3_START ... S5PC100_GPIO_H0_START - 1:
		return 14;
	case S5PC100_GPIO_I_START ... S5PC100_GPIO_J0_START - 1:
		return 15;
	case S5PC100_GPIO_J0_START ... S5PC100_GPIO_J1_START - 1:
		return 16;
	case S5PC100_GPIO_J1_START ... S5PC100_GPIO_J2_START - 1:
		return 17;
	case S5PC100_GPIO_J2_START ... S5PC100_GPIO_J3_START - 1:
		return 18;
	case S5PC100_GPIO_J3_START ... S5PC100_GPIO_J4_START - 1:
		return 19;
	case S5PC100_GPIO_J4_START ... S5PC100_GPIO_K0_START - 1:
		return 20;
	default:
		BUG();
	}

	return -EINVAL;
}

static int s5pc100_get_offset(unsigned int irq)
{
	struct gpio_chip *chip = get_irq_data(irq);
	return irq - S3C_IRQ_GPIO(chip->base);
}

static void s5pc100_gpioint_ack(unsigned int irq)
{
	int group, offset, pend_offset;
	unsigned int value;

	group = s5pc100_get_group(irq);
	offset = s5pc100_get_offset(irq);
	pend_offset = group_to_pend_offset(group);

	value = __raw_readl(S5P_GPIOREG(PEND_OFFSET) + pend_offset);
	value |= 1 << offset;
	__raw_writel(value, S5P_GPIOREG(PEND_OFFSET) + pend_offset);
}

static void s5pc100_gpioint_mask(unsigned int irq)
{
	int group, offset, mask_offset;
	unsigned int value;

	group = s5pc100_get_group(irq);
	offset = s5pc100_get_offset(irq);
	mask_offset = group_to_mask_offset(group);

	value = __raw_readl(S5P_GPIOREG(MASK_OFFSET) + mask_offset);
	value |= 1 << offset;
	__raw_writel(value, S5P_GPIOREG(MASK_OFFSET) + mask_offset);
}

static void s5pc100_gpioint_unmask(unsigned int irq)
{
	int group, offset, mask_offset;
	unsigned int value;

	group = s5pc100_get_group(irq);
	offset = s5pc100_get_offset(irq);
	mask_offset = group_to_mask_offset(group);

	value = __raw_readl(S5P_GPIOREG(MASK_OFFSET) + mask_offset);
	value &= ~(1 << offset);
	__raw_writel(value, S5P_GPIOREG(MASK_OFFSET) + mask_offset);
}

static void s5pc100_gpioint_mask_ack(unsigned int irq)
{
	s5pc100_gpioint_mask(irq);
	s5pc100_gpioint_ack(irq);
}

static int s5pc100_gpioint_set_type(unsigned int irq, unsigned int type)
{
	int group, offset, con_offset;
	unsigned int value;

	group = s5pc100_get_group(irq);
	offset = s5pc100_get_offset(irq);
	con_offset = group_to_con_offset(group);

	switch (type) {
	case IRQ_TYPE_NONE:
		printk(KERN_WARNING "No irq type\n");
		return -EINVAL;
	case IRQ_TYPE_EDGE_RISING:
		type = GPIOINT_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		type = GPIOINT_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		type = GPIOINT_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type = GPIOINT_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		type = GPIOINT_LEVEL_LOW;
		break;
	default:
		BUG();
	}


	value = __raw_readl(S5P_GPIOREG(CON_OFFSET) + con_offset);
	value &= ~(0xf << (offset * 0x4));
	value |= (type << (offset * 0x4));
	__raw_writel(value, S5P_GPIOREG(CON_OFFSET) + con_offset);

	return 0;
}

struct irq_chip s5pc100_gpioint = {
	.name		= "GPIO",
	.ack		= s5pc100_gpioint_ack,
	.mask		= s5pc100_gpioint_mask,
	.mask_ack	= s5pc100_gpioint_mask_ack,
	.unmask		= s5pc100_gpioint_unmask,
	.set_type	= s5pc100_gpioint_set_type,
};

void s5pc100_irq_gpioint_handler(unsigned int irq, struct irq_desc *desc)
{
	int group, offset, pend_offset, mask_offset;
	int real_irq, group_end;
	unsigned int pend, mask;

	group_end = 21;

	for (group = 0; group < group_end; group++) {
		pend_offset = group_to_pend_offset(group);
		pend = __raw_readl(S5P_GPIOREG(PEND_OFFSET) + pend_offset);
		if (!pend)
			continue;

		mask_offset = group_to_mask_offset(group);
		mask = __raw_readl(S5P_GPIOREG(MASK_OFFSET) + mask_offset);
		pend &= ~mask;

		for (offset = 0; offset < 8; offset++) {
			if (pend & (1 << offset)) {
				real_irq = s5pc100_get_start(group) + offset;
				generic_handle_irq(S3C_IRQ_GPIO(real_irq));
			}
		}
	}
}
