/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 */

/* $Id: dgap_pci.h,v 1.1 2009/10/23 14:01:57 markh Exp $ */

#ifndef __DGAP_PCI_H
#define __DGAP_PCI_H

#define PCIMAX 32			/* maximum number of PCI boards */

#define DIGI_VID		0x114F

#define PCI_DEVICE_EPC_DID	0x0002
#define PCI_DEVICE_XEM_DID	0x0004
#define PCI_DEVICE_XR_DID	0x0005
#define PCI_DEVICE_CX_DID	0x0006
#define PCI_DEVICE_XRJ_DID	0x0009	/* PLX-based Xr adapter */
#define PCI_DEVICE_XR_IBM_DID	0x0011	/* IBM 8-port Async Adapter */
#define PCI_DEVICE_XR_BULL_DID	0x0013	/* BULL 8-port Async Adapter */
#define PCI_DEVICE_XR_SAIP_DID	0x001c	/* SAIP card - Xr adapter */
#define PCI_DEVICE_XR_422_DID	0x0012	/* Xr-422 */
#define PCI_DEVICE_920_2_DID	0x0034	/* XR-Plus 920 K, 2 port */
#define PCI_DEVICE_920_4_DID	0x0026	/* XR-Plus 920 K, 4 port */
#define PCI_DEVICE_920_8_DID	0x0027	/* XR-Plus 920 K, 8 port */
#define PCI_DEVICE_EPCJ_DID	0x000a	/* PLX 9060 chip for PCI  */
#define PCI_DEVICE_CX_IBM_DID	0x001b	/* IBM 128-port Async Adapter */
#define PCI_DEVICE_920_8_HP_DID	0x0058	/* HP XR-Plus 920 K, 8 port */
#define PCI_DEVICE_XEM_HP_DID	0x0059  /* HP Xem PCI */

#define PCI_DEVICE_XEM_NAME	"AccelePort XEM"
#define PCI_DEVICE_CX_NAME	"AccelePort CX"
#define PCI_DEVICE_XR_NAME	"AccelePort Xr"
#define PCI_DEVICE_XRJ_NAME	"AccelePort Xr (PLX)"
#define PCI_DEVICE_XR_SAIP_NAME	"AccelePort Xr (SAIP)"
#define PCI_DEVICE_920_2_NAME	"AccelePort Xr920 2 port"
#define PCI_DEVICE_920_4_NAME	"AccelePort Xr920 4 port"
#define PCI_DEVICE_920_8_NAME	"AccelePort Xr920 8 port"
#define PCI_DEVICE_XR_422_NAME	"AccelePort Xr 422"
#define PCI_DEVICE_EPCJ_NAME	"AccelePort EPC (PLX)"
#define PCI_DEVICE_XR_BULL_NAME	"AccelePort Xr (BULL)"
#define PCI_DEVICE_XR_IBM_NAME	"AccelePort Xr (IBM)"
#define PCI_DEVICE_CX_IBM_NAME	"AccelePort CX (IBM)"
#define PCI_DEVICE_920_8_HP_NAME "AccelePort Xr920 8 port (HP)"
#define PCI_DEVICE_XEM_HP_NAME	"AccelePort XEM (HP)"


/*
 * On the PCI boards, there is no IO space allocated
 * The I/O registers will be in the first 3 bytes of the
 * upper 2MB of the 4MB memory space.  The board memory
 * will be mapped into the low 2MB of the 4MB memory space
 */

/* Potential location of PCI Bios from E0000 to FFFFF*/
#define PCI_BIOS_SIZE		0x00020000

/* Size of Memory and I/O for PCI (4MB) */
#define PCI_RAM_SIZE		0x00400000

/* Size of Memory (2MB) */
#define PCI_MEM_SIZE		0x00200000

/* Max PCI Window Size (2MB) */
#define PCI_WIN_SIZE		0x00200000

#define PCI_WIN_SHIFT		21 /* 21 bits max */

/* Offset of I/0 in Memory (2MB) */
#define PCI_IO_OFFSET		0x00200000

/* Size of IO (2MB) */
#define PCI_IO_SIZE		0x00200000

#endif
