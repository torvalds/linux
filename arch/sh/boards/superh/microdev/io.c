/*
 * linux/arch/sh/kernel/io_microdev.c
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

#include <linux/config.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <asm/io.h>
#include <asm/mach/io.h>

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

#define PORT2ADDR(x) (microdev_isa_port2addr(x))


static inline void delay(void)
{
#if defined(CONFIG_PCI)
	/* System board present, just make a dummy SRAM access.  (CS0 will be
	   mapped to PCI memory, probably good to avoid it.) */
	ctrl_inw(0xa6800000);
#else
	/* CS0 will be mapped to flash, ROM etc so safe to access it. */
	ctrl_inw(0xa0000000);
#endif
}

unsigned char microdev_inb(unsigned long port)
{
#ifdef CONFIG_PCI
	if (port >= PCIBIOS_MIN_IO)
		return microdev_pci_inb(port);
#endif
	return *(volatile unsigned char*)PORT2ADDR(port);
}

unsigned short microdev_inw(unsigned long port)
{
#ifdef CONFIG_PCI
	if (port >= PCIBIOS_MIN_IO)
		return microdev_pci_inw(port);
#endif
	return *(volatile unsigned short*)PORT2ADDR(port);
}

unsigned int microdev_inl(unsigned long port)
{
#ifdef CONFIG_PCI
	if (port >= PCIBIOS_MIN_IO)
		return microdev_pci_inl(port);
#endif
	return *(volatile unsigned int*)PORT2ADDR(port);
}

void microdev_outb(unsigned char b, unsigned long port)
{
#ifdef CONFIG_PCI
	if (port >= PCIBIOS_MIN_IO) {
		microdev_pci_outb(b, port);
		return;
	}
#endif

	/*
	 *	There is a board feature with the current SH4-202 MicroDev in
	 *	that the 2 byte enables (nBE0 and nBE1) are tied together (and
	 *	to the Chip Select Line (Ethernet_CS)). Due to this conectivity,
	 *	it is not possible to safely perform 8-bit writes to the
	 *	Ethernet registers, as 16-bits will be consumed from the Data
	 *	lines (corrupting the other byte).  Hence, this function is
	 *	written to impliment 16-bit read/modify/write for all byte-wide
	 *	acceses.
	 *
	 *	Note: there is no problem with byte READS (even or odd).
	 *
	 *			Sean McGoogan - 16th June 2003.
	 */
	if ((port >= IO_LAN91C111_BASE) &&
	    (port <  IO_LAN91C111_BASE + IO_LAN91C111_EXTENT)) {
			/*
			 * Then are trying to perform a byte-write to the
			 * LAN91C111.  This needs special care.
			 */
		if (port % 2 == 1) {	/* is the port odd ? */
			/* unset bit-0, i.e. make even */
			const unsigned long evenPort = port-1;
			unsigned short word;

			/*
			 * do a 16-bit read/write to write to 'port',
			 * preserving even byte.
			 *
			 *	Even addresses are bits 0-7
			 *	Odd  addresses are bits 8-15
			 */
			word = microdev_inw(evenPort);
			word = (word & 0xffu) | (b << 8);
			microdev_outw(word, evenPort);
		} else {
			/* else, we are trying to do an even byte write */
			unsigned short word;

			/*
			 * do a 16-bit read/write to write to 'port',
			 * preserving odd byte.
			 *
			 *	Even addresses are bits 0-7
			 *	Odd  addresses are bits 8-15
			 */
			word = microdev_inw(port);
			word = (word & 0xff00u) | (b);
			microdev_outw(word, port);
		}
	} else {
		*(volatile unsigned char*)PORT2ADDR(port) = b;
	}
}

void microdev_outw(unsigned short b, unsigned long port)
{
#ifdef CONFIG_PCI
	if (port >= PCIBIOS_MIN_IO) {
		microdev_pci_outw(b, port);
		return;
	}
#endif
	*(volatile unsigned short*)PORT2ADDR(port) = b;
}

void microdev_outl(unsigned int b, unsigned long port)
{
#ifdef CONFIG_PCI
	if (port >= PCIBIOS_MIN_IO) {
		microdev_pci_outl(b, port);
		return;
	}
#endif
	*(volatile unsigned int*)PORT2ADDR(port) = b;
}

unsigned char microdev_inb_p(unsigned long port)
{
	unsigned char v = microdev_inb(port);
	delay();
	return v;
}

unsigned short microdev_inw_p(unsigned long port)
{
	unsigned short v = microdev_inw(port);
	delay();
	return v;
}

unsigned int microdev_inl_p(unsigned long port)
{
	unsigned int v = microdev_inl(port);
	delay();
	return v;
}

void microdev_outb_p(unsigned char b, unsigned long port)
{
	microdev_outb(b, port);
	delay();
}

void microdev_outw_p(unsigned short b, unsigned long port)
{
	microdev_outw(b, port);
	delay();
}

void microdev_outl_p(unsigned int b, unsigned long port)
{
	microdev_outl(b, port);
	delay();
}

void microdev_insb(unsigned long port, void *buffer, unsigned long count)
{
	volatile unsigned char *port_addr;
	unsigned char *buf = buffer;

	port_addr = (volatile unsigned char *)PORT2ADDR(port);

	while (count--)
		*buf++ = *port_addr;
}

void microdev_insw(unsigned long port, void *buffer, unsigned long count)
{
	volatile unsigned short *port_addr;
	unsigned short *buf = buffer;

	port_addr = (volatile unsigned short *)PORT2ADDR(port);

	while (count--)
		*buf++ = *port_addr;
}

void microdev_insl(unsigned long port, void *buffer, unsigned long count)
{
	volatile unsigned long *port_addr;
	unsigned int *buf = buffer;

	port_addr = (volatile unsigned long *)PORT2ADDR(port);

	while (count--)
		*buf++ = *port_addr;
}

void microdev_outsb(unsigned long port, const void *buffer, unsigned long count)
{
	volatile unsigned char *port_addr;
	const unsigned char *buf = buffer;

	port_addr = (volatile unsigned char *)PORT2ADDR(port);

	while (count--)
		*port_addr = *buf++;
}

void microdev_outsw(unsigned long port, const void *buffer, unsigned long count)
{
	volatile unsigned short *port_addr;
	const unsigned short *buf = buffer;

	port_addr = (volatile unsigned short *)PORT2ADDR(port);

	while (count--)
		*port_addr = *buf++;
}

void microdev_outsl(unsigned long port, const void *buffer, unsigned long count)
{
	volatile unsigned long *port_addr;
	const unsigned int *buf = buffer;

	port_addr = (volatile unsigned long *)PORT2ADDR(port);

	while (count--)
		*port_addr = *buf++;
}

/*
 * map I/O ports to memory-mapped addresses
 */
unsigned long microdev_isa_port2addr(unsigned long offset)
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
#if 0
	} else if (offset == KBD_DATA_REG || offset == KBD_CNTL_REG ||
		   offset == KBD_STATUS_REG) {
			/*
			 *	SMSC FDC37C93xAPM SuperIO chip
			 *
			 *	PS/2 Keyboard + Mouse (ports 0x60 and 0x64).
			 */
	        result = IO_SUPERIO_PHYS + (offset << 1);
#endif
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
		       __FUNCTION__, offset);
		result = PVR;
	}

	return result;
}

