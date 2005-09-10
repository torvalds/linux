/*
 * linux/arch/arm/mach-h720x/cpu-h7202.c
 *
 * Copyright (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *               2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *               2004 Sascha Hauer    <s.hauer@pengutronix.de>
 *
 * processor specific stuff for the Hynix h7202
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/irqs.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <linux/device.h>
#include <linux/serial_8250.h>
#include "common.h"

static struct resource h7202ps2_resources[] = {
	[0] = {
		.start	= 0x8002c000,
		.end	= 0x8002c040,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_PS2,
		.end	= IRQ_PS2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device h7202ps2_device = {
	.name		= "h7202ps2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(h7202ps2_resources),
	.resource	= h7202ps2_resources,
};

static struct plat_serial8250_port serial_platform_data[] = {
	{
		.membase	= (void*)SERIAL0_VIRT,
		.mapbase	= SERIAL0_BASE,
		.irq		= IRQ_UART0,
		.uartclk	= 2*1843200,
		.regshift	= 2,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{
		.membase	= (void*)SERIAL1_VIRT,
		.mapbase	= SERIAL1_BASE,
		.irq		= IRQ_UART1,
		.uartclk	= 2*1843200,
		.regshift	= 2,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
#ifdef CONFIG_H7202_SERIAL23
	{
		.membase	= (void*)SERIAL2_VIRT,
		.mapbase	= SERIAL2_BASE,
		.irq		= IRQ_UART2,
		.uartclk	= 2*1843200,
		.regshift	= 2,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{
		.membase	= (void*)SERIAL3_VIRT,
		.mapbase	= SERIAL3_BASE,
		.irq		= IRQ_UART3,
		.uartclk	= 2*1843200,
		.regshift	= 2,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
#endif
	{ },
};

static struct platform_device serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= serial_platform_data,
	},
};

static struct platform_device *devices[] __initdata = {
	&h7202ps2_device,
	&serial_device,
};

/* Although we have two interrupt lines for the timers, we only have one
 * status register which clears all pending timer interrupts on reading. So
 * we have to handle all timer interrupts in one place.
 */
static void
h7202_timerx_demux_handler(unsigned int irq_unused, struct irqdesc *desc,
			struct pt_regs *regs)
{
	unsigned int mask, irq;

	mask = CPU_REG (TIMER_VIRT, TIMER_TOPSTAT);

	if ( mask & TSTAT_T0INT ) {
		write_seqlock(&xtime_lock);
		timer_tick(regs);
		write_sequnlock(&xtime_lock);
		if( mask == TSTAT_T0INT )
			return;
	}

	mask >>= 1;
	irq = IRQ_TIMER1;
	desc = irq_desc + irq;
	while (mask) {
		if (mask & 1)
			desc_handle_irq(irq, desc, regs);
		irq++;
		desc++;
		mask >>= 1;
	}
}

/*
 * Timer interrupt handler
 */
static irqreturn_t
h7202_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	h7202_timerx_demux_handler(0, NULL, regs);
	return IRQ_HANDLED;
}

/*
 * mask multiplexed timer irq's
 */
static void inline mask_timerx_irq (u32 irq)
{
	unsigned int bit;
	bit = 2 << ((irq == IRQ_TIMER64B) ? 4 : (irq - IRQ_TIMER1));
	CPU_REG (TIMER_VIRT, TIMER_TOPCTRL) &= ~bit;
}

/*
 * unmask multiplexed timer irq's
 */
static void inline unmask_timerx_irq (u32 irq)
{
	unsigned int bit;
	bit = 2 << ((irq == IRQ_TIMER64B) ? 4 : (irq - IRQ_TIMER1));
	CPU_REG (TIMER_VIRT, TIMER_TOPCTRL) |= bit;
}

static struct irqchip h7202_timerx_chip = {
	.ack = mask_timerx_irq,
	.mask = mask_timerx_irq,
	.unmask = unmask_timerx_irq,
};

static struct irqaction h7202_timer_irq = {
	.name		= "h7202 Timer Tick",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= h7202_timer_interrupt,
};

/*
 * Setup TIMER0 as system timer
 */
void __init h7202_init_time(void)
{
	CPU_REG (TIMER_VIRT, TM0_PERIOD) = LATCH;
	CPU_REG (TIMER_VIRT, TM0_CTRL) = TM_RESET;
	CPU_REG (TIMER_VIRT, TM0_CTRL) = TM_REPEAT | TM_START;
	CPU_REG (TIMER_VIRT, TIMER_TOPCTRL) = ENABLE_TM0_INTR | TIMER_ENABLE_BIT;

	setup_irq(IRQ_TIMER0, &h7202_timer_irq);
}

struct sys_timer h7202_timer = {
	.init		= h7202_init_time,
	.offset		= h720x_gettimeoffset,
};

void __init h7202_init_irq (void)
{
	int 	irq;

	CPU_REG (GPIO_E_VIRT, GPIO_MASK) = 0x0;

	for (irq = IRQ_TIMER1;
	                  irq < IRQ_CHAINED_TIMERX(NR_TIMERX_IRQS); irq++) {
		mask_timerx_irq(irq);
		set_irq_chip(irq, &h7202_timerx_chip);
		set_irq_handler(irq, do_edge_IRQ);
		set_irq_flags(irq, IRQF_VALID );
	}
	set_irq_chained_handler(IRQ_TIMERX, h7202_timerx_demux_handler);

	h720x_init_irq();
}

void __init init_hw_h7202(void)
{
	/* Enable clocks */
	CPU_REG (PMU_BASE, PMU_PLL_CTRL) |= PLL_2_EN | PLL_1_EN | PLL_3_MUTE;

	CPU_REG (SERIAL0_VIRT, SERIAL_ENABLE) = SERIAL_ENABLE_EN;
	CPU_REG (SERIAL1_VIRT, SERIAL_ENABLE) = SERIAL_ENABLE_EN;
#ifdef CONFIG_H7202_SERIAL23
	CPU_REG (SERIAL2_VIRT, SERIAL_ENABLE) = SERIAL_ENABLE_EN;
	CPU_REG (SERIAL3_VIRT, SERIAL_ENABLE) = SERIAL_ENABLE_EN;
	CPU_IO (GPIO_AMULSEL) = AMULSEL_USIN2 | AMULSEL_USOUT2 |
	                        AMULSEL_USIN3 | AMULSEL_USOUT3;
#endif
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));
}
