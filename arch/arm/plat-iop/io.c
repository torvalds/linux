/*
 * iop3xx custom ioremap implementation
 * Copyright (c) 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <mach/hardware.h>
#include <asm/io.h>

void * __iomem __iop3xx_ioremap(unsigned long cookie, size_t size,
	unsigned int mtype)
{
	void __iomem * retval;

	switch (cookie) {
	case IOP3XX_PCI_LOWER_IO_PA ... IOP3XX_PCI_UPPER_IO_PA:
		retval = (void *) IOP3XX_PCI_IO_PHYS_TO_VIRT(cookie);
		break;
	case IOP3XX_PERIPHERAL_PHYS_BASE ... IOP3XX_PERIPHERAL_UPPER_PA:
		retval = (void *) IOP3XX_PMMR_PHYS_TO_VIRT(cookie);
		break;
	default:
		retval = __arm_ioremap(cookie, size, mtype);
	}

	return retval;
}
EXPORT_SYMBOL(__iop3xx_ioremap);

void __iop3xx_iounmap(void __iomem *addr)
{
	extern void __iounmap(volatile void __iomem *addr);

	switch ((u32) addr) {
	case IOP3XX_PCI_LOWER_IO_VA ... IOP3XX_PCI_UPPER_IO_VA:
	case IOP3XX_PERIPHERAL_VIRT_BASE ... IOP3XX_PERIPHERAL_UPPER_VA:
		goto skip;
	}
	__iounmap(addr);

skip:
	return;
}
EXPORT_SYMBOL(__iop3xx_iounmap);
