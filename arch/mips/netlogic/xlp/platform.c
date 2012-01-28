/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/pci.h>
#include <linux/serial_reg.h>
#include <linux/spinlock.h>

#include <asm/time.h>
#include <asm/addrspace.h>
#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/pic.h>
#include <asm/netlogic/xlp-hal/uart.h>

static unsigned int nlm_xlp_uart_in(struct uart_port *p, int offset)
{
	 return nlm_read_reg(p->iobase, offset);
}

static void nlm_xlp_uart_out(struct uart_port *p, int offset, int value)
{
	nlm_write_reg(p->iobase, offset, value);
}

#define PORT(_irq)					\
	{						\
		.irq		= _irq,			\
		.regshift	= 2,			\
		.iotype		= UPIO_MEM32,		\
		.flags		= (UPF_SKIP_TEST|UPF_FIXED_TYPE|\
					UPF_BOOT_AUTOCONF),	\
		.uartclk	= XLP_IO_CLK,		\
		.type		= PORT_16550A,		\
		.serial_in	= nlm_xlp_uart_in,	\
		.serial_out	= nlm_xlp_uart_out,	\
	}

static struct plat_serial8250_port xlp_uart_data[] = {
	PORT(PIC_UART_0_IRQ),
	PORT(PIC_UART_1_IRQ),
	{},
};

static struct platform_device uart_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = xlp_uart_data,
	},
};

static int __init nlm_platform_uart_init(void)
{
	unsigned long mmio;

	mmio = (unsigned long)nlm_get_uart_regbase(0, 0);
	xlp_uart_data[0].iobase = mmio;
	xlp_uart_data[0].membase = (void __iomem *)mmio;
	xlp_uart_data[0].mapbase = mmio;

	mmio = (unsigned long)nlm_get_uart_regbase(0, 1);
	xlp_uart_data[1].iobase = mmio;
	xlp_uart_data[1].membase = (void __iomem *)mmio;
	xlp_uart_data[1].mapbase = mmio;

	return platform_device_register(&uart_device);
}

arch_initcall(nlm_platform_uart_init);
