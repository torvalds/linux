// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/ioport.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 * Copyright (C) 2005 - 2007 Paul Mundt
 */
#include <linux/module.h>
#include <linux/io.h>
#include <asm/io_trapped.h>

unsigned long sh_io_port_base __read_mostly = -1;
EXPORT_SYMBOL(sh_io_port_base);

void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	void __iomem *ret;

	ret = __ioport_map_trapped(port, nr);
	if (ret)
		return ret;

	return (void __iomem *)(port + sh_io_port_base);
}
EXPORT_SYMBOL(ioport_map);

void ioport_unmap(void __iomem *addr)
{
}
EXPORT_SYMBOL(ioport_unmap);
