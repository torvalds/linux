/*
 * arch/sh64/mach-cayman/iomap.c
 *
 * Cayman iomap interface
 *
 * Copyright (C) 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/config.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/cayman.h>

void __iomem *ioport_map(unsigned long port, unsigned int len)
{
	if (port < 0x400)
		return (void __iomem *)((port << 2) | smsc_superio_virt);

	return (void __iomem *)port;
}

