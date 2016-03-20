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
 */

#ifndef __DGNC_PCI_H
#define __DGNC_PCI_H

#define PCIMAX 32			/* maximum number of PCI boards */

#define DIGI_VID				0x114F

#define PCI_DEVICE_CLASSIC_4_DID		0x0028
#define PCI_DEVICE_CLASSIC_8_DID		0x0029
#define PCI_DEVICE_CLASSIC_4_422_DID		0x00D0
#define PCI_DEVICE_CLASSIC_8_422_DID		0x00D1
#define PCI_DEVICE_NEO_4_DID			0x00B0
#define PCI_DEVICE_NEO_8_DID			0x00B1
#define PCI_DEVICE_NEO_2DB9_DID			0x00C8
#define PCI_DEVICE_NEO_2DB9PRI_DID		0x00C9
#define PCI_DEVICE_NEO_2RJ45_DID		0x00CA
#define PCI_DEVICE_NEO_2RJ45PRI_DID		0x00CB
#define PCI_DEVICE_NEO_1_422_DID		0x00CC
#define PCI_DEVICE_NEO_1_422_485_DID		0x00CD
#define PCI_DEVICE_NEO_2_422_485_DID		0x00CE
#define PCI_DEVICE_NEO_EXPRESS_8_DID		0x00F0
#define PCI_DEVICE_NEO_EXPRESS_4_DID		0x00F1
#define PCI_DEVICE_NEO_EXPRESS_4RJ45_DID	0x00F2
#define PCI_DEVICE_NEO_EXPRESS_8RJ45_DID	0x00F3
#define PCI_DEVICE_NEO_EXPRESS_4_IBM_DID	0x00F4

#define PCI_DEVICE_CLASSIC_4_PCI_NAME		"ClassicBoard 4 PCI"
#define PCI_DEVICE_CLASSIC_8_PCI_NAME		"ClassicBoard 8 PCI"
#define PCI_DEVICE_CLASSIC_4_422_PCI_NAME	"ClassicBoard 4 422 PCI"
#define PCI_DEVICE_CLASSIC_8_422_PCI_NAME	"ClassicBoard 8 422 PCI"
#define PCI_DEVICE_NEO_4_PCI_NAME		"Neo 4 PCI"
#define PCI_DEVICE_NEO_8_PCI_NAME		"Neo 8 PCI"
#define PCI_DEVICE_NEO_2DB9_PCI_NAME		"Neo 2 - DB9 Universal PCI"
#define PCI_DEVICE_NEO_2DB9PRI_PCI_NAME		"Neo 2 - DB9 Universal PCI - Powered Ring Indicator"
#define PCI_DEVICE_NEO_2RJ45_PCI_NAME		"Neo 2 - RJ45 Universal PCI"
#define PCI_DEVICE_NEO_2RJ45PRI_PCI_NAME	"Neo 2 - RJ45 Universal PCI - Powered Ring Indicator"
#define PCI_DEVICE_NEO_1_422_PCI_NAME		"Neo 1 422 PCI"
#define PCI_DEVICE_NEO_1_422_485_PCI_NAME	"Neo 1 422/485 PCI"
#define PCI_DEVICE_NEO_2_422_485_PCI_NAME	"Neo 2 422/485 PCI"

#define PCI_DEVICE_NEO_EXPRESS_8_PCI_NAME	"Neo 8 PCI Express"
#define PCI_DEVICE_NEO_EXPRESS_4_PCI_NAME	"Neo 4 PCI Express"
#define PCI_DEVICE_NEO_EXPRESS_4RJ45_PCI_NAME	"Neo 4 PCI Express RJ45"
#define PCI_DEVICE_NEO_EXPRESS_8RJ45_PCI_NAME	"Neo 8 PCI Express RJ45"
#define PCI_DEVICE_NEO_EXPRESS_4_IBM_PCI_NAME	"Neo 4 PCI Express IBM"

/* Size of Memory and I/O for PCI (4 K) */
#define PCI_RAM_SIZE				0x1000

/* Size of Memory (2MB) */
#define PCI_MEM_SIZE				0x1000

#endif
