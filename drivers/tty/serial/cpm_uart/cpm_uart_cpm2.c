// SPDX-License-Identifier: GPL-2.0+
/*
 *  Driver for CPM (SCC/SMC) serial ports; CPM2 definitions
 *
 *  Maintainer: Kumar Gala (galak@kernel.crashing.org) (CPM2)
 *              Pantelis Antoniou (panto@intracom.gr) (CPM1)
 *
 *  Copyright (C) 2004 Freescale Semiconductor, Inc.
 *            (C) 2004 Intracom, S.A.
 *            (C) 2006 MontaVista Software, Inc.
 *		Vitaly Bordug <vbordug@ru.mvista.com>
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/memblock.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/fs_pd.h>

#include <linux/serial_core.h>
#include <linux/kernel.h>

#include "cpm_uart.h"

/**************************************************************/

void __iomem *cpm_uart_map_pram(struct uart_cpm_port *port,
				struct device_node *np)
{
	void __iomem *pram;
	unsigned long offset;
	struct resource res;
	resource_size_t len;

	/* Don't remap parameter RAM if it has already been initialized
	 * during console setup.
	 */
	if (IS_SMC(port) && port->smcup)
		return port->smcup;
	else if (!IS_SMC(port) && port->sccup)
		return port->sccup;

	if (of_address_to_resource(np, 1, &res))
		return NULL;

	len = resource_size(&res);
	pram = ioremap(res.start, len);
	if (!pram)
		return NULL;

	if (!IS_SMC(port))
		return pram;

	if (len != 2) {
		printk(KERN_WARNING "cpm_uart[%d]: device tree references "
			"SMC pram, using boot loader/wrapper pram mapping. "
			"Please fix your device tree to reference the pram "
			"base register instead.\n",
			port->port.line);
		return pram;
	}

	offset = cpm_dpalloc(PROFF_SMC_SIZE, 64);
	out_be16(pram, offset);
	iounmap(pram);
	return cpm_muram_addr(offset);
}

void cpm_uart_unmap_pram(struct uart_cpm_port *port, void __iomem *pram)
{
	if (!IS_SMC(port))
		iounmap(pram);
}
