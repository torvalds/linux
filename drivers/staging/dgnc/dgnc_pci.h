/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 */

#ifndef _DGNC_PCI_H
#define _DGNC_PCI_H

/* Maximum number of PCI boards */
#define PCIMAX 32

#define DIGI_VID				0x114F

#define PCI_DEVICE_CLASSIC_4_DID		0x0028
#define PCI_DEVICE_CLASSIC_8_DID		0x0029
#define PCI_DEVICE_CLASSIC_4_422_DID		0x00D0
#define PCI_DEVICE_CLASSIC_8_422_DID		0x00D1

#define PCI_DEVICE_CLASSIC_4_PCI_NAME		"ClassicBoard 4 PCI"
#define PCI_DEVICE_CLASSIC_8_PCI_NAME		"ClassicBoard 8 PCI"
#define PCI_DEVICE_CLASSIC_4_422_PCI_NAME	"ClassicBoard 4 422 PCI"
#define PCI_DEVICE_CLASSIC_8_422_PCI_NAME	"ClassicBoard 8 422 PCI"

/* Size of memory and I/O for PCI (4 K) */
#define PCI_RAM_SIZE				0x1000

/* Size of memory (2MB) */
#define PCI_MEM_SIZE				0x1000

#endif	/* _DGNC_PCI_H */
