/*
 * Copyright 2011, Netlogic Microsystems.
 * Copyright 2004, Matt Porter <mporter@kernel.crashing.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>

#include <asm/netlogic/xlr/iomap.h>
#include <asm/netlogic/xlr/pic.h>
#include <asm/netlogic/xlr/xlr.h>

unsigned int nlm_xlr_uart_in(struct uart_port *p, int offset)
{
	nlm_reg_t *mmio;
	unsigned int value;

	/* XLR uart does not need any mapping of regs */
	mmio = (nlm_reg_t *)(p->membase + (offset << p->regshift));
	value = netlogic_read_reg(mmio, 0);

	/* See XLR/XLS errata */
	if (offset == UART_MSR)
		value ^= 0xF0;
	else if (offset == UART_MCR)
		value ^= 0x3;

	return value;
}

void nlm_xlr_uart_out(struct uart_port *p, int offset, int value)
{
	nlm_reg_t *mmio;

	/* XLR uart does not need any mapping of regs */
	mmio = (nlm_reg_t *)(p->membase + (offset << p->regshift));

	/* See XLR/XLS errata */
	if (offset == UART_MSR)
		value ^= 0xF0;
	else if (offset == UART_MCR)
		value ^= 0x3;

	netlogic_write_reg(mmio, 0, value);
}

#define PORT(_irq)					\
	{						\
		.irq		= _irq,			\
		.regshift	= 2,			\
		.iotype		= UPIO_MEM32,		\
		.flags		= (UPF_SKIP_TEST |	\
			 UPF_FIXED_TYPE | UPF_BOOT_AUTOCONF),\
		.uartclk	= PIC_CLKS_PER_SEC,	\
		.type		= PORT_16550A,		\
		.serial_in	= nlm_xlr_uart_in,	\
		.serial_out	= nlm_xlr_uart_out,	\
	}

static struct plat_serial8250_port xlr_uart_data[] = {
	PORT(PIC_UART_0_IRQ),
	PORT(PIC_UART_1_IRQ),
	{},
};

static struct platform_device uart_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = xlr_uart_data,
	},
};

static int __init nlm_uart_init(void)
{
	nlm_reg_t *mmio;

	mmio = netlogic_io_mmio(NETLOGIC_IO_UART_0_OFFSET);
	xlr_uart_data[0].membase = (void __iomem *)mmio;
	xlr_uart_data[0].mapbase = CPHYSADDR((unsigned long)mmio);

	mmio = netlogic_io_mmio(NETLOGIC_IO_UART_1_OFFSET);
	xlr_uart_data[1].membase = (void __iomem *)mmio;
	xlr_uart_data[1].mapbase = CPHYSADDR((unsigned long)mmio);

	return platform_device_register(&uart_device);
}

arch_initcall(nlm_uart_init);
