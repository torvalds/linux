/*
 *  Copyright (C) by Basler Vision Technologies AG
 *  Author: Thomas Koeller <thomas.koeller@baslereb.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/rm9k-ocd.h>

#include <excite.h>

extern asmlinkage void excite_handle_int(void);

/*
 * Initialize the interrupt handler
 */
void __init arch_init_irq(void)
{
	mips_cpu_irq_init(0);
	rm7k_cpu_irq_init(8);
	rm9k_cpu_irq_init(12);

#ifdef CONFIG_KGDB
	excite_kgdb_init();
#endif
}

asmlinkage void plat_irq_dispatch(void)
{
	const u32
		interrupts = read_c0_cause() >> 8,
		mask = ((read_c0_status() >> 8) & 0x000000ff) |
		       (read_c0_intcontrol() & 0x0000ff00),
		pending = interrupts & mask;
	u32 msgintflags, msgintmask, msgint;

	/* process timer interrupt */
	if (pending & (1 << TIMER_IRQ)) {
		do_IRQ(TIMER_IRQ);
		return;
	}

	/* Process PCI interrupts */
#if USB_IRQ < 10
	msgintflags = ocd_readl(INTP0Status0 + (USB_MSGINT / 0x20 * 0x10));
	msgintmask  = ocd_readl(INTP0Mask0 + (USB_MSGINT / 0x20 * 0x10));
	msgint	    = msgintflags & msgintmask & (0x1 << (USB_MSGINT % 0x20));
	if ((pending & (1 << USB_IRQ)) && msgint) {
#else
	if (pending & (1 << USB_IRQ)) {
#endif
		do_IRQ(USB_IRQ);
		return;
	}

	/* Process TITAN interrupts */
	msgintflags = ocd_readl(INTP0Status0 + (TITAN_MSGINT / 0x20 * 0x10));
	msgintmask  = ocd_readl(INTP0Mask0 + (TITAN_MSGINT / 0x20 * 0x10));
	msgint	    = msgintflags & msgintmask & (0x1 << (TITAN_MSGINT % 0x20));
	if ((pending & (1 << TITAN_IRQ)) && msgint) {
		ocd_writel(msgint, INTP0Clear0 + (TITAN_MSGINT / 0x20 * 0x10));
#if defined(CONFIG_KGDB)
		excite_kgdb_inthdl();
#endif
		do_IRQ(TITAN_IRQ);
		return;
	}

	/* Process FPGA line #0 interrupts */
	msgintflags = ocd_readl(INTP0Status0 + (FPGA0_MSGINT / 0x20 * 0x10));
	msgintmask  = ocd_readl(INTP0Mask0 + (FPGA0_MSGINT / 0x20 * 0x10));
	msgint	    = msgintflags & msgintmask & (0x1 << (FPGA0_MSGINT % 0x20));
	if ((pending & (1 << FPGA0_IRQ)) && msgint) {
		do_IRQ(FPGA0_IRQ);
		return;
	}

	/* Process FPGA line #1 interrupts */
	msgintflags = ocd_readl(INTP0Status0 + (FPGA1_MSGINT / 0x20 * 0x10));
	msgintmask  = ocd_readl(INTP0Mask0 + (FPGA1_MSGINT / 0x20 * 0x10));
	msgint	    = msgintflags & msgintmask & (0x1 << (FPGA1_MSGINT % 0x20));
	if ((pending & (1 << FPGA1_IRQ)) && msgint) {
		do_IRQ(FPGA1_IRQ);
		return;
	}

	/* Process PHY interrupts */
	msgintflags = ocd_readl(INTP0Status0 + (PHY_MSGINT / 0x20 * 0x10));
	msgintmask  = ocd_readl(INTP0Mask0 + (PHY_MSGINT / 0x20 * 0x10));
	msgint	    = msgintflags & msgintmask & (0x1 << (PHY_MSGINT % 0x20));
	if ((pending & (1 << PHY_IRQ)) && msgint) {
		do_IRQ(PHY_IRQ);
		return;
	}

	/* Process spurious interrupts */
	spurious_interrupt();
}
