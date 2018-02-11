/*
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2010 Jason Wang <jason77.wang@gmail.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/smsc911x.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include "3ds_debugboard.h"
#include "hardware.h"

/* LAN9217 ethernet base address */
#define LAN9217_BASE_ADDR(n)	(n + 0x0)
/* External UART */
#define UARTA_BASE_ADDR(n)	(n + 0x8000)
#define UARTB_BASE_ADDR(n)	(n + 0x10000)

#define BOARD_IO_ADDR(n)	(n + 0x20000)
/* LED switchs */
#define LED_SWITCH_REG		0x00
/* buttons */
#define SWITCH_BUTTONS_REG	0x08
/* status, interrupt */
#define INTR_STATUS_REG	0x10
#define INTR_MASK_REG		0x38
#define INTR_RESET_REG		0x20
/* magic word for debug CPLD */
#define MAGIC_NUMBER1_REG	0x40
#define MAGIC_NUMBER2_REG	0x48
/* CPLD code version */
#define CPLD_CODE_VER_REG	0x50
/* magic word for debug CPLD */
#define MAGIC_NUMBER3_REG	0x58
/* module reset register*/
#define MODULE_RESET_REG	0x60
/* CPU ID and Personality ID */
#define MCU_BOARD_ID_REG	0x68

#define MXC_MAX_EXP_IO_LINES	16

/* interrupts like external uart , external ethernet etc*/
#define EXPIO_INT_ENET		0
#define EXPIO_INT_XUART_A	1
#define EXPIO_INT_XUART_B	2
#define EXPIO_INT_BUTTON_A	3
#define EXPIO_INT_BUTTON_B	4

static void __iomem *brd_io;
static struct irq_domain *domain;

static struct resource smsc911x_resources[] = {
	{
		.flags = IORESOURCE_MEM,
	} , {
		.flags = IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc911x_config = {
	.irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.flags = SMSC911X_USE_32BIT | SMSC911X_FORCE_INTERNAL_PHY,
};

static struct platform_device smsc_lan9217_device = {
	.name = "smsc911x",
	.id = -1,
	.dev = {
		.platform_data = &smsc911x_config,
	},
	.num_resources = ARRAY_SIZE(smsc911x_resources),
	.resource = smsc911x_resources,
};

static void mxc_expio_irq_handler(struct irq_desc *desc)
{
	u32 imr_val;
	u32 int_valid;
	u32 expio_irq;

	/* irq = gpio irq number */
	desc->irq_data.chip->irq_mask(&desc->irq_data);

	imr_val = imx_readw(brd_io + INTR_MASK_REG);
	int_valid = imx_readw(brd_io + INTR_STATUS_REG) & ~imr_val;

	expio_irq = 0;
	for (; int_valid != 0; int_valid >>= 1, expio_irq++) {
		if ((int_valid & 1) == 0)
			continue;
		generic_handle_irq(irq_find_mapping(domain, expio_irq));
	}

	desc->irq_data.chip->irq_ack(&desc->irq_data);
	desc->irq_data.chip->irq_unmask(&desc->irq_data);
}

/*
 * Disable an expio pin's interrupt by setting the bit in the imr.
 * Irq is an expio virtual irq number
 */
static void expio_mask_irq(struct irq_data *d)
{
	u16 reg;
	u32 expio = d->hwirq;

	reg = imx_readw(brd_io + INTR_MASK_REG);
	reg |= (1 << expio);
	imx_writew(reg, brd_io + INTR_MASK_REG);
}

static void expio_ack_irq(struct irq_data *d)
{
	u32 expio = d->hwirq;

	imx_writew(1 << expio, brd_io + INTR_RESET_REG);
	imx_writew(0, brd_io + INTR_RESET_REG);
	expio_mask_irq(d);
}

static void expio_unmask_irq(struct irq_data *d)
{
	u16 reg;
	u32 expio = d->hwirq;

	reg = imx_readw(brd_io + INTR_MASK_REG);
	reg &= ~(1 << expio);
	imx_writew(reg, brd_io + INTR_MASK_REG);
}

static struct irq_chip expio_irq_chip = {
	.irq_ack = expio_ack_irq,
	.irq_mask = expio_mask_irq,
	.irq_unmask = expio_unmask_irq,
};

static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
};

int __init mxc_expio_init(u32 base, u32 intr_gpio)
{
	u32 p_irq = gpio_to_irq(intr_gpio);
	int irq_base;
	int i;

	brd_io = ioremap(BOARD_IO_ADDR(base), SZ_4K);
	if (brd_io == NULL)
		return -ENOMEM;

	if ((imx_readw(brd_io + MAGIC_NUMBER1_REG) != 0xAAAA) ||
	    (imx_readw(brd_io + MAGIC_NUMBER2_REG) != 0x5555) ||
	    (imx_readw(brd_io + MAGIC_NUMBER3_REG) != 0xCAFE)) {
		pr_info("3-Stack Debug board not detected\n");
		iounmap(brd_io);
		brd_io = NULL;
		return -ENODEV;
	}

	pr_info("3-Stack Debug board detected, rev = 0x%04X\n",
		readw(brd_io + CPLD_CODE_VER_REG));

	/*
	 * Configure INT line as GPIO input
	 */
	gpio_request(intr_gpio, "expio_pirq");
	gpio_direction_input(intr_gpio);

	/* disable the interrupt and clear the status */
	imx_writew(0, brd_io + INTR_MASK_REG);
	imx_writew(0xFFFF, brd_io + INTR_RESET_REG);
	imx_writew(0, brd_io + INTR_RESET_REG);
	imx_writew(0x1F, brd_io + INTR_MASK_REG);

	irq_base = irq_alloc_descs(-1, 0, MXC_MAX_EXP_IO_LINES, numa_node_id());
	WARN_ON(irq_base < 0);

	domain = irq_domain_add_legacy(NULL, MXC_MAX_EXP_IO_LINES, irq_base, 0,
				       &irq_domain_simple_ops, NULL);
	WARN_ON(!domain);

	for (i = irq_base; i < irq_base + MXC_MAX_EXP_IO_LINES; i++) {
		irq_set_chip_and_handler(i, &expio_irq_chip, handle_level_irq);
		irq_clear_status_flags(i, IRQ_NOREQUEST);
	}
	irq_set_irq_type(p_irq, IRQF_TRIGGER_LOW);
	irq_set_chained_handler(p_irq, mxc_expio_irq_handler);

	/* Register Lan device on the debugboard */
	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	smsc911x_resources[0].start = LAN9217_BASE_ADDR(base);
	smsc911x_resources[0].end = LAN9217_BASE_ADDR(base) + 0x100 - 1;
	smsc911x_resources[1].start = irq_find_mapping(domain, EXPIO_INT_ENET);
	smsc911x_resources[1].end = irq_find_mapping(domain, EXPIO_INT_ENET);
	platform_device_register(&smsc_lan9217_device);

	return 0;
}
