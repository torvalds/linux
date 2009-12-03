/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  pcifw.h PCI FW related headers
 */

#ifndef __PCIFW_H__
#define __PCIFW_H__

#pragma pack(1)

struct pnp_hdr_s{
  u32	signature;	/* "$PnP" */
  u8	rev;		/* Struct revision */
  u8 	len;		/* Header structure len in multiples
				 * of 16 bytes */
  u16  off;		/* Offset to next header 00 if none */
  u8	rsvd;		/* Reserved byte */
  u8	cksum;		/* 8-bit checksum for this header */
  u32	pnp_dev_id;	/* PnP Device Id */
  u16  mfstr;		/* Pointer to manufacturer string */
  u16	prstr;		/* Pointer to product string */
  u8	devtype[3];	/* Device Type Code */
  u8	devind;		/* Device Indicator */
  u16	bcventr;	/* Bootstrap entry vector */
  u16	rsvd2;		/* Reserved */
  u16  sriv;		/* Static resource information vector */
};

struct pci_3_0_ds_s{
 u32	sig;   		/* Signature "PCIR" */
 u16	vendid;		/* Vendor ID */
 u16	devid;		/* Device ID */
 u16	devlistoff;	/* Device List Offset */
 u16	len;		/* PCI Data Structure Length */
 u8	rev;		/* PCI Data Structure Revision */
 u8	clcode[3];	/* Class Code */
 u16	imglen;		/* Code image length in multiples of
				 * 512 bytes */
 u16	coderev;	/* Revision level of code/data */
 u8	codetype;	/* Code type 0x00 - BIOS */
 u8	indr;		/* Last image indicator */
 u16	mrtimglen;	/* Max Run Time Image Length */
 u16	cuoff;		/* Config Utility Code Header Offset */
 u16	dmtfclp;	/* DMTF CLP entry point offset */
};

struct pci_optrom_hdr_s{
 u16	sig;		/* Signature 0x55AA */
 u8	len;		/* Option ROM length in units of 512 bytes */
 u8	inivec[3];	/* Initialization vector */
 u8	rsvd[16];	/* Reserved field */
 u16	verptr;		/* Pointer to version string - private */
 u16	pcids;		/* Pointer to PCI data structure */
 u16	pnphdr;		/* Pointer to PnP expansion header */
};

#pragma pack()

#endif
