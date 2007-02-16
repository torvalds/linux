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
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/irq.h>

#include <asm/mach/map.h>

#include <asm/arch-ns9xxx/board.h>
#include <asm/arch-ns9xxx/regs-sys.h>
#include <asm/arch-ns9xxx/regs-mem.h>
#include <asm/arch-ns9xxx/regs-bbu.h>
#include <asm/arch-ns9xxx/regs-board-a9m9750dev.h>

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

static void a9m9750dev_fpga_ack_irq(unsigned int irq)
{
	/* nothing */
}

static void a9m9750dev_fpga_mask_irq(unsigned int irq)
{
	FPGA_IER &= ~(1 << (irq - FPGA_IRQ(0)));
}

static void a9m9750dev_fpga_maskack_irq(unsigned int irq)
{
	a9m9750dev_fpga_mask_irq(irq);
	a9m9750dev_fpga_ack_irq(irq);
}

static void a9m9750dev_fpga_unmask_irq(unsigned int irq)
{
	FPGA_IER |= 1 << (irq - FPGA_IRQ(0));
}

static struct irq_chip a9m9750dev_fpga_chip = {
	.ack		= a9m9750dev_fpga_ack_irq,
	.mask		= a9m9750dev_fpga_mask_irq,
	.mask_ack	= a9m9750dev_fpga_maskack_irq,
	.unmask		= a9m9750dev_fpga_unmask_irq,
};

static void a9m9750dev_fpga_demux_handler(unsigned int irq,
		struct irq_desc *desc)
{
	int stat = FPGA_ISR;

	while (stat != 0) {
		int irqno = fls(stat) - 1;

		stat &= ~(1 << irqno);

		desc = irq_desc + FPGA_IRQ(irqno);

		desc_handle_irq(irqno, desc);
	}
}

void __init board_a9m9750dev_init_irq(void)
{
	u32 reg;
	int i;

	/*
	 * configure gpio for IRQ_EXT2
	 * use GPIO 11, because GPIO 32 is used for the LCD
	 */
	/* XXX: proper GPIO handling */
	BBU_GC(2) &= ~0x2000;

	for (i = FPGA_IRQ(0); i <= FPGA_IRQ(7); ++i) {
		set_irq_chip(i, &a9m9750dev_fpga_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	/* IRQ_EXT2: level sensitive + active low */
	reg = SYS_EIC(2);
	REGSET(reg, SYS_EIC, PLTY, AL);
	REGSET(reg, SYS_EIC, LVEDG, LEVEL);
	SYS_EIC(2) = reg;

	set_irq_chained_handler(IRQ_EXT2,
			a9m9750dev_fpga_demux_handler);
}

static struct plat_serial8250_port board_a9m9750dev_serial8250_port[] = {
	{
		.iobase         = FPGA_UARTA_BASE,
		.membase        = (unsigned char*)FPGA_UARTA_BASE,
		.mapbase        = FPGA_UARTA_BASE,
		.irq            = IRQ_FPGA_UARTA,
		.iotype         = UPIO_MEM,
		.uartclk        = 18432000,
		.regshift       = 0,
		.flags          = UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ,
	}, {
		.iobase         = FPGA_UARTB_BASE,
		.membase        = (unsigned char*)FPGA_UARTB_BASE,
		.mapbase        = FPGA_UARTB_BASE,
		.irq            = IRQ_FPGA_UARTB,
		.iotype         = UPIO_MEM,
		.uartclk        = 18432000,
		.regshift       = 0,
		.flags          = UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ,
	}, {
		.iobase         = FPGA_UARTC_BASE,
		.membase        = (unsigned char*)FPGA_UARTC_BASE,
		.mapbase        = FPGA_UARTC_BASE,
		.irq            = IRQ_FPGA_UARTC,
		.iotype         = UPIO_MEM,
		.uartclk        = 18432000,
		.regshift       = 0,
		.flags          = UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ,
	}, {
		.iobase         = FPGA_UARTD_BASE,
		.membase        = (unsigned char*)FPGA_UARTD_BASE,
		.mapbase        = FPGA_UARTD_BASE,
		.irq            = IRQ_FPGA_UARTD,
		.iotype         = UPIO_MEM,
		.uartclk        = 18432000,
		.regshift       = 0,
		.flags          = UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ,
	}, {
		/* end marker */
	},
};

static struct platform_device board_a9m9750dev_serial_device = {
	.name = "serial8250",
	.dev = {
		.platform_data = board_a9m9750dev_serial8250_port,
	},
};

static struct platform_device *board_a9m9750dev_devices[] __initdata = {
	&board_a9m9750dev_serial_device,
};

void __init board_a9m9750dev_init_machine(void)
{
	u32 reg;

	/* setup static CS0: memory base ... */
	REGSETIM(SYS_SMCSSMB(0), SYS_SMCSSMB, CSxB,
			NS9XXX_CSxSTAT_PHYS(0) >> 12);

	/* ... and mask */
	reg = SYS_SMCSSMM(0);
	REGSETIM(reg, SYS_SMCSSMM, CSxM, 0xfffff);
	REGSET(reg, SYS_SMCSSMM, CSEx, EN);
	SYS_SMCSSMM(0) = reg;

	/* setup static CS0: memory configuration */
	reg = MEM_SMC(0);
	REGSET(reg, MEM_SMC, WSMC, OFF);
	REGSET(reg, MEM_SMC, BSMC, OFF);
	REGSET(reg, MEM_SMC, EW, OFF);
	REGSET(reg, MEM_SMC, PB, 1);
	REGSET(reg, MEM_SMC, PC, AL);
	REGSET(reg, MEM_SMC, PM, DIS);
	REGSET(reg, MEM_SMC, MW, 8);
	MEM_SMC(0) = reg;

	/* setup static CS0: timing */
	MEM_SMWED(0) = 0x2;
	MEM_SMOED(0) = 0x2;
	MEM_SMRD(0) = 0x6;
	MEM_SMWD(0) = 0x6;

	platform_add_devices(board_a9m9750dev_devices,
			ARRAY_SIZE(board_a9m9750dev_devices));
}

