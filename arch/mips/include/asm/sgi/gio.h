/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * gio.h: Definitions for SGI GIO bus
 *
 * Copyright (C) 2002 Ladislav Michl
 */

#ifndef _SGI_GIO_H
#define _SGI_GIO_H

/*
 * GIO bus addresses
 *
 * The Indigo and Indy have two GIO bus connectors. Indigo2 (all models) have
 * three physical connectors, but only two slots, GFX and EXP0.
 *
 * There is 10MB of GIO address space for GIO64 slot devices
 * slot#   slot type address range	      size
 * -----   --------- ----------------------- -----
 *   0	   GFX	     0x1f000000 - 0x1f3fffff   4MB
 *   1	   EXP0	     0x1f400000 - 0x1f5fffff   2MB
 *   2	   EXP1	     0x1f600000 - 0x1f9fffff   4MB
 *
 * There are un-slotted devices, HPC, I/O and misc devices, which are grouped
 * into the HPC address space.
 *   -	   MISC	     0x1fb00000 - 0x1fbfffff   1MB
 *
 * Following space is reserved and unused
 *   -	   RESERVED  0x18000000 - 0x1effffff 112MB
 *
 * GIO bus IDs
 *
 * Each GIO bus device identifies itself to the system by answering a
 * read with an "ID" value. IDs are either 8 or 32 bits long. IDs less
 * than 128 are 8 bits long, with the most significant 24 bits read from
 * the slot undefined.
 *
 * 32-bit IDs are divided into
 *	bits 0:6	the product ID; ranges from 0x00 to 0x7F.
 *	bit 7		0=GIO Product ID is 8 bits wide
 *			1=GIO Product ID is 32 bits wide.
 *	bits 8:15	manufacturer version for the product.
 *	bit 16		0=GIO32 and GIO32-bis, 1=GIO64.
 *	bit 17		0=no ROM present
 *			1=ROM present on this board AND next three words
 *			space define the ROM.
 *	bits 18:31	up to manufacturer.
 *
 * IDs above 0x50/0xd0 are of 3rd party boards.
 *
 * 8-bit IDs
 *	0x01		XPI low cost FDDI
 *	0x02		GTR TokenRing
 *	0x04		Synchronous ISDN
 *	0x05		ATM board [*]
 *	0x06		Canon Interface
 *	0x07		16 bit SCSI Card [*]
 *	0x08		JPEG (Double Wide)
 *	0x09		JPEG (Single Wide)
 *	0x0a		XPI mez. FDDI device 0
 *	0x0b		XPI mez. FDDI device 1
 *	0x0c		SMPTE 259M Video [*]
 *	0x0d		Babblefish Compression [*]
 *	0x0e		E-Plex 8-port Ethernet
 *	0x30		Lyon Lamb IVAS
 *	0xb8		GIO 100BaseTX Fast Ethernet (gfe)
 *
 * [*] Device provide 32-bit ID.
 *
 */

#define GIO_ID(x)		(x & 0x7f)
#define GIO_32BIT_ID		0x80
#define GIO_REV(x)		((x >> 8) & 0xff)
#define GIO_64BIT_IFACE		0x10000
#define GIO_ROM_PRESENT		0x20000
#define GIO_VENDOR_CODE(x)	((x >> 18) & 0x3fff)

#define GIO_SLOT_GFX_BASE	0x1f000000
#define GIO_SLOT_EXP0_BASE	0x1f400000
#define GIO_SLOT_EXP1_BASE	0x1f600000

#endif /* _SGI_GIO_H */
