/*
 * arch/arm/mach-ns9xxx/board-a9m9750dev.c
 *
 * Copyright (C) 2006,2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/irq.h>

#include <asm/mach/map.h>
#include <asm/gpio.h>

#include <mach/board.h>
#include <mach/processor-ns9360.h>
#include <mach/regs-sys-ns9360.h>
#include <mach/regs-mem.h>
#include <mach/regs-bbu.h>
#include <mach/regs-board-a9m9750dev.h>

#include "board-a9m9750dev.h"

static struct map_desc board_a9m9750dev_io_desc[] __initdata = {
	{ /* FPGA on CS0 */
		.virtual = io_p2v(NS9XXX_CSxSTAT_PHYS(0)),
		.pfn = __phys_to_pfn(NS9XXX_CSxSTAT_PHYS(0)),
		.length = NS9XXX_CS0STAT_LENGTH,
		.type = MT_DEVICE,
	},
};

void __init board_a9m9750dev_map_io(void)
{
	iotable_init(board_a9m9750dev_io_desc,
		     ARRAY_SIZE(board_a9m9750dev_io_desc));
}

static void a9m9750dev_fpga_ack_irq(struct irq_data *d)
{
	/* nothing */
}

static void a9m9750dev_fpga_mask_irq(struct irq_data *d)
{
	u8 ier;

	ier = __raw_readb(FPGA_IER);

	ier &= ~(1 << (d->irq - FPGA_IRQ(0)));

	__raw_writeb(ier, FPGA_IER);
}

static void a9m9750dev_fpga_maskack_irq(struct irq_data *d)
{
	a9m9750dev_fpga_mask_irq(d);
	a9m9750dev_fpga_ack_irq(d);
}

static void a9m9750dev_fpga_unmask_irq(struct irq_data *d)
{
	u8 ier;

	ier = __raw_readb(FPGA_IER);

	ier |= 1 << (d->irq - FPGA_IRQ(0));

	__raw_writeb(ier, FPGA_IER);
}

static struct irq_chip a9m9750dev_fpga_chip = {
	.irq_ack	= a9m9750dev_fpga_ack_irq,
	.irq_mask	= a9m9750dev_fpga_mask_irq,
	.irq_mask_ack	= a9m9750dev_fpga_maskack_irq,
	.irq_unmask	= a9m9750dev_fpga_unmask_irq,
};

static void a9m9750dev_fpga_demux_handler(unsigned int irq,
		struct irq_desc *desc)
{
	u8 stat = __raw_readb(FPGA_ISR);

	desc->irq_data.chip->irq_mask_ack(&desc->irq_data);

	while (stat != 0) {
		int irqno = fls(stat) - 1;

		stat &= ~(1 << irqno);

		generic_handle_irq(FPGA_IRQ(irqno));
	}

	desc->irq_data.chip->irq_unmask(&desc->irq_data);
}

void __init board_a9m9750dev_init_irq(void)
{
	u32 eic;
	int i;

	if (gpio_request(11, "board a9m9750dev extirq2") == 0)
		ns9360_gpio_configure(11, 0, 1);
	else
		printk(KERN_ERR "%s: cannot get gpio 11 for IRQ_NS9XXX_EXT2\n",
				__func__);

	for (i = FPGA_IRQ(0); i <= FPGA_IRQ(7); ++i) {
		set_irq_chip(i, &a9m9750dev_fpga_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	/* IRQ_NS9XXX_EXT2: level sensitive + active low */
	eic = __raw_readl(SYS_EIC(2));
	REGSET(eic, SYS_EIC, PLTY, AL);
	REGSET(eic, SYS_EIC, LVEDG, LEVEL);
	__raw_writel(eic, SYS_EIC(2));

	set_irq_chained_handler(IRQ_NS9XXX_EXT2,
			a9m9750dev_fpga_demux_handler);
}

void __init board_a9m9750dev_init_machine(void)
{
	u32 reg;

	/* setup static CS0: memory base ... */
	reg = __raw_readl(SYS_SMCSSMB(0));
	REGSETIM(reg, SYS_SMCSSMB, CSxB, NS9XXX_CSxSTAT_PHYS(0) >> 12);
	__raw_writel(reg, SYS_SMCSSMB(0));

	/* ... and mask */
	reg = __raw_readl(SYS_SMCSSMM(0));
	REGSETIM(reg, SYS_SMCSSMM, CSxM, 0xfffff);
	REGSET(reg, SYS_SMCSSMM, CSEx, EN);
	__raw_writel(reg, SYS_SMCSSMM(0));

	/* setup static CS0: memory configuration */
	reg = __raw_readl(MEM_SMC(0));
	REGSET(reg, MEM_SMC, PSMC, OFF);
	REGSET(reg, MEM_SMC, BSMC, OFF);
	REGSET(reg, MEM_SMC, EW, OFF);
	REGSET(reg, MEM_SMC, PB, 1);
	REGSET(reg, MEM_SMC, PC, AL);
	REGSET(reg, MEM_SMC, PM, DIS);
	REGSET(reg, MEM_SMC, MW, 8);
	__raw_writel(reg, MEM_SMC(0));

	/* setup static CS0: timing */
	__raw_writel(0x2, MEM_SMWED(0));
	__raw_writel(0x2, MEM_SMOED(0));
	__raw_writel(0x6, MEM_SMRD(0));
	__raw_writel(0x6, MEM_SMWD(0));
}
