/*
 * arch/sh/kernel/ioport.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 * Copyright (C) 2005 - 2007 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/io.h>

const unsigned long sh_io_port_base __read_mostly = -1;
EXPORT_SYMBOL(sh_io_port_base);

void __iomem *__ioport_map(unsigned long addr, unsigned int size)
{
	if (sh_mv.mv_ioport_map)
		return sh_mv.mv_ioport_map(addr, size);

	return (void __iomem *)(addr + sh_io_port_base);
}
EXPORT_SYMBOL(__ioport_map);

void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	void __iomem *ret;

	ret = __ioport_map_trapped(port, nr);
	if (ret)
		return ret;

	return __ioport_map(port, nr);
}
EXPORT_SYMBOL(ioport_map);

void ioport_unmap(void __iomem *addr)
{
	if (sh_mv.mv_ioport_unmap)
		sh_mv.mv_ioport_unmap(addr);
}
EXPORT_SYMBOL(ioport_unmap);
