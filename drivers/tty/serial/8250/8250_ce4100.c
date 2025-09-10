// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel CE4100  platform specific setup code
 *
 * (C) Copyright 2010 Intel Corporation
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/types.h>

#include <asm/ce4100.h>
#include <asm/fixmap.h>
#include <asm/page.h>

#include <linux/serial_reg.h>
#include <linux/serial_8250.h>

static unsigned int mem_serial_in(struct uart_port *p, int offset)
{
	offset = offset << p->regshift;
	return readl(p->membase + offset);
}

/*
 * The UART Tx interrupts are not set under some conditions and therefore serial
 * transmission hangs. This is a silicon issue and has not been root caused. The
 * workaround for this silicon issue checks UART_LSR_THRE bit and UART_LSR_TEMT
 * bit of LSR register in interrupt handler to see whether at least one of these
 * two bits is set, if so then process the transmit request. If this workaround
 * is not applied, then the serial transmission may hang. This workaround is for
 * errata number 9 in Errata - B step.
*/
static u32 ce4100_mem_serial_in(struct uart_port *p, unsigned int offset)
{
	u32 ret, ier, lsr;

	ret = mem_serial_in(p, offset);
	if (offset != UART_IIR || !(ret & UART_IIR_NO_INT))
		return ret;

	/* see if the TX interrupt should have really set */
	ier = mem_serial_in(p, UART_IER);
	/* see if the UART's XMIT interrupt is enabled */
	if (!(ier & UART_IER_THRI))
		return ret;

	lsr = mem_serial_in(p, UART_LSR);
	/* now check to see if the UART should be generating an interrupt (but isn't) */
	if (lsr & (UART_LSR_THRE | UART_LSR_TEMT))
		ret &= ~UART_IIR_NO_INT;

	return ret;
}

static void ce4100_mem_serial_out(struct uart_port *p, unsigned int offset, u32 value)
{
	offset <<= p->regshift;
	writel(value, p->membase + offset);
}

static void ce4100_serial_fixup(int port, struct uart_port *up, u32 *capabilities)
{
#ifdef CONFIG_EARLY_PRINTK
	/*
	 * Override the legacy port configuration that comes from
	 * asm/serial.h. Using the ioport driver then switching to the
	 * PCI memmaped driver hangs the IOAPIC.
	 */
	if (up->iotype != UPIO_MEM32) {
		up->uartclk = 14745600;
		up->mapbase = 0xdffe0200;
		set_fixmap_nocache(FIX_EARLYCON_MEM_BASE, up->mapbase & PAGE_MASK);
		up->membase = (void __iomem *)__fix_to_virt(FIX_EARLYCON_MEM_BASE);
		up->membase += up->mapbase & ~PAGE_MASK;
		up->mapbase += port * 0x100;
		up->membase += port * 0x100;
		up->iotype = UPIO_MEM32;
		up->regshift = 2;
		up->irq = 4;
	}
#endif
	up->iobase = 0;
	up->serial_in = ce4100_mem_serial_in;
	up->serial_out = ce4100_mem_serial_out;

	*capabilities |= (1 << 12);
}

void __init sdv_serial_fixup(void)
{
	serial8250_set_isa_configurator(ce4100_serial_fixup);
}
