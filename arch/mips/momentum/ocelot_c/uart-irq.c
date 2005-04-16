/*
 * Copyright 2002 Momentum Computer
 * Author: mdharm@momenco.com
 *
 * arch/mips/momentum/ocelot_c/uart-irq.c
 *     Interrupt routines for UARTs.  Interrupt numbers are assigned from
 *     80 to 81 (2 interrupt sources).
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <asm/ptrace.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <asm/io.h>
#include <asm/irq.h>
#include "ocelot_c_fpga.h"

static inline int ls1bit8(unsigned int x)
{
        int b = 7, s;

        s =  4; if (((unsigned char)(x << 4)) == 0) s = 0; b -= s; x <<= s;
        s =  2; if (((unsigned char)(x << 2)) == 0) s = 0; b -= s; x <<= s;
        s =  1; if (((unsigned char)(x << 1)) == 0) s = 0; b -= s;

        return b;
}

/* mask off an interrupt -- 0 is enable, 1 is disable */
static inline void mask_uart_irq(unsigned int irq)
{
	uint8_t value;

	value = OCELOT_FPGA_READ(UART_INTMASK);
	value |= 1 << (irq - 74);
	OCELOT_FPGA_WRITE(value, UART_INTMASK);

	/* read the value back to assure that it's really been written */
	value = OCELOT_FPGA_READ(UART_INTMASK);
}

/* unmask an interrupt -- 0 is enable, 1 is disable */
static inline void unmask_uart_irq(unsigned int irq)
{
	uint8_t value;

	value = OCELOT_FPGA_READ(UART_INTMASK);
	value &= ~(1 << (irq - 74));
	OCELOT_FPGA_WRITE(value, UART_INTMASK);

	/* read the value back to assure that it's really been written */
	value = OCELOT_FPGA_READ(UART_INTMASK);
}

/*
 * Enables the IRQ in the FPGA
 */
static void enable_uart_irq(unsigned int irq)
{
	unmask_uart_irq(irq);
}

/*
 * Initialize the IRQ in the FPGA
 */
static unsigned int startup_uart_irq(unsigned int irq)
{
	unmask_uart_irq(irq);
	return 0;
}

/*
 * Disables the IRQ in the FPGA
 */
static void disable_uart_irq(unsigned int irq)
{
	mask_uart_irq(irq);
}

/*
 * Masks and ACKs an IRQ
 */
static void mask_and_ack_uart_irq(unsigned int irq)
{
	mask_uart_irq(irq);
}

/*
 * End IRQ processing
 */
static void end_uart_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		unmask_uart_irq(irq);
}

/*
 * Interrupt handler for interrupts coming from the FPGA chip.
 */
void ll_uart_irq(struct pt_regs *regs)
{
	unsigned int irq_src, irq_mask;

	/* read the interrupt status registers */
	irq_src = OCELOT_FPGA_READ(UART_INTSTAT);
	irq_mask = OCELOT_FPGA_READ(UART_INTMASK);

	/* mask for just the interrupts we want */
	irq_src &= ~irq_mask;

	do_IRQ(ls1bit8(irq_src) + 74, regs);
}

#define shutdown_uart_irq	disable_uart_irq

struct hw_interrupt_type uart_irq_type = {
	"UART/FPGA",
	startup_uart_irq,
	shutdown_uart_irq,
	enable_uart_irq,
	disable_uart_irq,
	mask_and_ack_uart_irq,
	end_uart_irq,
	NULL
};

void uart_irq_init(void)
{
	/* Reset irq handlers pointers to NULL */
	irq_desc[80].status = IRQ_DISABLED;
	irq_desc[80].action = 0;
	irq_desc[80].depth = 2;
	irq_desc[80].handler = &uart_irq_type;

	irq_desc[81].status = IRQ_DISABLED;
	irq_desc[81].action = 0;
	irq_desc[81].depth = 2;
	irq_desc[81].handler = &uart_irq_type;
}
