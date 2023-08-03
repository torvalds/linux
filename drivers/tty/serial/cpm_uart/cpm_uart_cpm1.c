// SPDX-License-Identifier: GPL-2.0+
/*
 *  Driver for CPM (SCC/SMC) serial ports; CPM1 definitions
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
#include <linux/gfp.h>
#include <linux/ioport.h>
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

#include <linux/of.h>
#include <linux/of_address.h>

#include "cpm_uart.h"

/**************************************************************/

void __iomem *cpm_uart_map_pram(struct uart_cpm_port *port,
				struct device_node *np)
{
	return of_iomap(np, 1);
}

void cpm_uart_unmap_pram(struct uart_cpm_port *port, void __iomem *pram)
{
	iounmap(pram);
}
