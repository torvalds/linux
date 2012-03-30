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
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/smsc911x.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>

#include <mach/hardware.h>

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

#define MXC_IRQ_TO_EXPIO(irq)   ((irq) - MXC_BOARD_IRQ_START)
#define MXC_IRQ_TO_GPIO(irq)	((irq) - MXC_INTERNAL_IRQS)

#define MXC_EXP_IO_BASE		(MXC_BOARD_IRQ_START)
#define MXC_MAX_EXP_IO_LINES	16

/* interrupts like external uart , external ethernet etc*/
#define EXPIO_INT_ENET		(MXC_BOARD_IRQ_START + 0)
#define EXPIO_INT_XUART_A	(MXC_BOARD_IRQ_START + 1)
#define EXPIO_INT_XUART_B	(MXC_BOARD_IRQ_START + 2)
#define EXPIO_INT_BUTTON_A	(MXC_BOARD_IRQ_START + 3)
#define EXPIO_INT_BUTTON_B	(MXC_BOARD_IRQ_START + 4)

static void __iomem *brd_io;

static struct resource smsc911x_resources[] = {
	{
		.flags = IORESOURCE_MEM,
	} , {
		.start = EXPIO_INT_ENET,
		.end = EXPIO_INT_ENET,
		.flags = IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc911x_config = {
	.irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.flags = SMSC911X_USE_32BIT | SMSC911X_FORCE_INTERNAL_PHY,
};

static struct platform_device smsc_lan9217_device = {
	.name = "smsc911x",
	.id = 0,
	.dev = {
		.platform_data = &smsc911x_config,
	},
	.num_resources = ARRAY_SIZE(smsc911x_resources),
	.resource = smsc911x_resources,
};

static void mxc_expio_irq_handler(u32 irq, struct irq_desc *desc)
{
	u32 imr_val;
	u32 int_valid;
	u32 expio_irq;

	/* irq = gpio irq number */
	desc->irq_data.chip->irq_mask(&desc->irq_data);

	imr_val = __raw_readw(brd_io + INTR_MASK_REG);
	int_valid = __raw_readw(brd_io + INTR_STATUS_REG) & ~imr_val;

	expio_irq = MXC_BOARD_IRQ_START;
	for (; int_valid != 0; int_valid >>= 1, expio_irq++) {
		if ((int_valid & 1) == 0)
			continue;
		generic_handle_irq(expio_irq);
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
	u32 expio = MXC_IRQ_TO_EXPIO(d->irq);

	reg = __raw_readw(brd_io + INTR_MASK_REG);
	reg |= (1 << expio);
	__raw_writew(reg, brd_io + INTR_MASK_REG);
}

static void expio_ack_irq(struct irq_data *d)
{
	u32 expio = MXC_IRQ_TO_EXPIO(d->irq);

	__raw_writew(1 << expio, brd_io + INTR_RESET_REG);
	__raw_writew(0, brd_io + INTR_RESET_REG);
	expio_mask_irq(d);
}

static void expio_unmask_irq(struct irq_data *d)
{
	u16 reg;
	u32 expio = MXC_IRQ_TO_EXPIO(d->irq);

	reg = __raw_readw(brd_io + INTR_MASK_REG);
	reg &= ~(1 << expio);
	__raw_writew(reg, brd_io + INTR_MASK_REG);
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

int __init mxc_expio_init(u32 base, u32 p_irq)
{
	int i;

	brd_io = ioremap(BOARD_IO_ADDR(base), SZ_4K);
	if (brd_io == NULL)
		return -ENOMEM;

	if ((__raw_readw(brd_io + MAGIC_NUMBER1_REG) != 0xAAAA) ||
	    (__raw_readw(brd_io + MAGIC_NUMBER2_REG) != 0x5555) ||
	    (__raw_readw(brd_io + MAGIC_NUMBER3_REG) != 0xCAFE)) {
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
	gpio_request(MXC_IRQ_TO_GPIO(p_irq), "expio_pirq");
	gpio_direction_input(MXC_IRQ_TO_GPIO(p_irq));

	/* disable the interrupt and clear the status */
	__raw_writew(0, brd_io + INTR_MASK_REG);
	__raw_writew(0xFFFF, brd_io + INTR_RESET_REG);
	__raw_writew(0, brd_io + INTR_RESET_REG);
	__raw_writew(0x1F, brd_io + INTR_MASK_REG);
	for (i = MXC_EXP_IO_BASE;
	     i < (MXC_EXP_IO_BASE + MXC_MAX_EXP_IO_LINES); i++) {
		irq_set_chip_and_handler(i, &expio_irq_chip, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
	irq_set_irq_type(p_irq, IRQF_TRIGGER_LOW);
	irq_set_chained_handler(p_irq, mxc_expio_irq_handler);

	/* Register Lan device on the debugboard */
	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	smsc911x_resources[0].start = LAN9217_BASE_ADDR(base);
	smsc911x_resources[0].end = LAN9217_BASE_ADDR(base) + 0x100 - 1;
	platform_device_register(&smsc_lan9217_device);

	return 0;
}
