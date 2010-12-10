/*
 * linux/arch/sh/boards/superh/microdev/io.c
 *
 * Copyright (C) 2003 Sean McGoogan (Sean.McGoogan@superh.com)
 * Copyright (C) 2003, 2004 SuperH, Inc.
 * Copyright (C) 2004 Paul Mundt
 *
 * SuperH SH4-202 MicroDev board support.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <asm/io.h>
#include <mach/microdev.h>

	/*
	 *	we need to have a 'safe' address to re-direct all I/O requests
	 *	that we do not explicitly wish to handle. This safe address
	 *	must have the following properies:
	 *
	 *		* writes are ignored (no exception)
	 *		* reads are benign (no side-effects)
	 *		* accesses of width 1, 2 and 4-bytes are all valid.
	 *
	 *	The Processor Version Register (PVR) has these properties.
	 */
#define	PVR	0xff000030	/* Processor Version Register */


#define	IO_IDE2_BASE		0x170ul	/* I/O base for SMSC FDC37C93xAPM IDE #2 */
#define	IO_IDE1_BASE		0x1f0ul	/* I/O base for SMSC FDC37C93xAPM IDE #1 */
#define IO_ISP1161_BASE		0x290ul /* I/O port for Philips ISP1161x USB chip */
#define IO_SERIAL2_BASE		0x2f8ul /* I/O base for SMSC FDC37C93xAPM Serial #2 */
#define	IO_LAN91C111_BASE	0x300ul	/* I/O base for SMSC LAN91C111 Ethernet chip */
#define	IO_IDE2_MISC		0x376ul	/* I/O misc for SMSC FDC37C93xAPM IDE #2 */
#define IO_SUPERIO_BASE		0x3f0ul /* I/O base for SMSC FDC37C93xAPM SuperIO chip */
#define	IO_IDE1_MISC		0x3f6ul	/* I/O misc for SMSC FDC37C93xAPM IDE #1 */
#define IO_SERIAL1_BASE		0x3f8ul /* I/O base for SMSC FDC37C93xAPM Serial #1 */

#define	IO_ISP1161_EXTENT	0x04ul	/* I/O extent for Philips ISP1161x USB chip */
#define	IO_LAN91C111_EXTENT	0x10ul	/* I/O extent for SMSC LAN91C111 Ethernet chip */
#define	IO_SUPERIO_EXTENT	0x02ul	/* I/O extent for SMSC FDC37C93xAPM SuperIO chip */
#define	IO_IDE_EXTENT		0x08ul	/* I/O extent for IDE Task Register set */
#define IO_SERIAL_EXTENT	0x10ul

#define	IO_LAN91C111_PHYS	0xa7500000ul	/* Physical address of SMSC LAN91C111 Ethernet chip */
#define	IO_ISP1161_PHYS		0xa7700000ul	/* Physical address of Philips ISP1161x USB chip */
#define	IO_SUPERIO_PHYS		0xa7800000ul	/* Physical address of SMSC FDC37C93xAPM SuperIO chip */

/*
 * map I/O ports to memory-mapped addresses
 */
void __iomem *microdev_ioport_map(unsigned long offset, unsigned int len)
{
	unsigned long result;

	if ((offset >= IO_LAN91C111_BASE) &&
	    (offset <  IO_LAN91C111_BASE + IO_LAN91C111_EXTENT)) {
			/*
			 *	SMSC LAN91C111 Ethernet chip
			 */
		result = IO_LAN91C111_PHYS + offset - IO_LAN91C111_BASE;
	} else if ((offset >= IO_SUPERIO_BASE) &&
		   (offset <  IO_SUPERIO_BASE + IO_SUPERIO_EXTENT)) {
			/*
			 *	SMSC FDC37C93xAPM SuperIO chip
			 *
			 *	Configuration Registers
			 */
		result = IO_SUPERIO_PHYS + (offset << 1);
	} else if (((offset >= IO_IDE1_BASE) &&
		    (offset <  IO_IDE1_BASE + IO_IDE_EXTENT)) ||
		    (offset == IO_IDE1_MISC)) {
			/*
			 *	SMSC FDC37C93xAPM SuperIO chip
			 *
			 *	IDE #1
			 */
	        result = IO_SUPERIO_PHYS + (offset << 1);
	} else if (((offset >= IO_IDE2_BASE) &&
		    (offset <  IO_IDE2_BASE + IO_IDE_EXTENT)) ||
		    (offset == IO_IDE2_MISC)) {
			/*
			 *	SMSC FDC37C93xAPM SuperIO chip
			 *
			 *	IDE #2
			 */
	        result = IO_SUPERIO_PHYS + (offset << 1);
	} else if ((offset >= IO_SERIAL1_BASE) &&
		   (offset <  IO_SERIAL1_BASE + IO_SERIAL_EXTENT)) {
			/*
			 *	SMSC FDC37C93xAPM SuperIO chip
			 *
			 *	Serial #1
			 */
		result = IO_SUPERIO_PHYS + (offset << 1);
	} else if ((offset >= IO_SERIAL2_BASE) &&
		   (offset <  IO_SERIAL2_BASE + IO_SERIAL_EXTENT)) {
			/*
			 *	SMSC FDC37C93xAPM SuperIO chip
			 *
			 *	Serial #2
			 */
		result = IO_SUPERIO_PHYS + (offset << 1);
	} else if ((offset >= IO_ISP1161_BASE) &&
		   (offset < IO_ISP1161_BASE + IO_ISP1161_EXTENT)) {
			/*
			 *	Philips USB ISP1161x chip
			 */
		result = IO_ISP1161_PHYS + offset - IO_ISP1161_BASE;
	} else {
			/*
			 *	safe default.
			 */
		printk("Warning: unexpected port in %s( offset = 0x%lx )\n",
		       __func__, offset);
		result = PVR;
	}

	return (void __iomem *)result;
}
