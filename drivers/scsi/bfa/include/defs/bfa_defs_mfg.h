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
#ifndef __BFA_DEFS_MFG_H__
#define __BFA_DEFS_MFG_H__

#include <bfa_os_inc.h>

/**
 * Manufacturing block version
 */
#define BFA_MFG_VERSION				2

/**
 * Manufacturing block encrypted version
 */
#define BFA_MFG_ENC_VER				2

/**
 * Manufacturing block version 1 length
 */
#define BFA_MFG_VER1_LEN			128

/**
 * Manufacturing block header length
 */
#define BFA_MFG_HDR_LEN				4

/**
 * Checksum size
 */
#define BFA_MFG_CHKSUM_SIZE			16

/**
 * Manufacturing block format
 */
#define BFA_MFG_SERIALNUM_SIZE			11
#define BFA_MFG_PARTNUM_SIZE			14
#define BFA_MFG_SUPPLIER_ID_SIZE		10
#define BFA_MFG_SUPPLIER_PARTNUM_SIZE		20
#define BFA_MFG_SUPPLIER_SERIALNUM_SIZE		20
#define BFA_MFG_SUPPLIER_REVISION_SIZE		4
#define STRSZ(_n)	(((_n) + 4) & ~3)

/**
 * Manufacturing card type
 */
enum {
	BFA_MFG_TYPE_CB_MAX  = 825,      /*  Crossbow card type max	*/
	BFA_MFG_TYPE_FC8P2   = 825,      /*  8G 2port FC card		*/
	BFA_MFG_TYPE_FC8P1   = 815,      /*  8G 1port FC card		*/
	BFA_MFG_TYPE_FC4P2   = 425,      /*  4G 2port FC card		*/
	BFA_MFG_TYPE_FC4P1   = 415,      /*  4G 1port FC card		*/
	BFA_MFG_TYPE_CNA10P2 = 1020,     /*  10G 2port CNA card	*/
	BFA_MFG_TYPE_CNA10P1 = 1010,     /*  10G 1port CNA card	*/
	BFA_MFG_TYPE_JAYHAWK = 804,      /*  Jayhawk mezz card */
	BFA_MFG_TYPE_WANCHESE = 1007,    /*  Wanchese mezz card */
	BFA_MFG_TYPE_INVALID = 0,        /*  Invalid card type */
};

#pragma pack(1)

/**
 * Card type to port number conversion
 */
#define bfa_mfg_type2port_num(card_type) (((card_type) / 10) % 10)

/**
 * Check if Mezz card
 */
#define bfa_mfg_is_mezz(type) (( \
	(type) == BFA_MFG_TYPE_JAYHAWK || \
	(type) == BFA_MFG_TYPE_WANCHESE))

/**
 * Check if card type valid
 */
#define bfa_mfg_is_card_type_valid(type) (( \
	(type) == BFA_MFG_TYPE_FC8P2 || \
	(type) == BFA_MFG_TYPE_FC8P1 || \
	(type) == BFA_MFG_TYPE_FC4P2 || \
	(type) == BFA_MFG_TYPE_FC4P1 || \
	(type) == BFA_MFG_TYPE_CNA10P2 || \
	(type) == BFA_MFG_TYPE_CNA10P1 || \
	bfa_mfg_is_mezz(type)))

/**
 * All numerical fields are in big-endian format.
 */
struct bfa_mfg_block_s {
};

/**
 * VPD data length
 */
#define BFA_MFG_VPD_LEN		512

#define BFA_MFG_VPD_PCI_HDR_OFF		137
#define BFA_MFG_VPD_PCI_VER_MASK	0x07	/*  version mask 3 bits */
#define BFA_MFG_VPD_PCI_VDR_MASK	0xf8	/*  vendor mask 5 bits */

/**
 * VPD vendor tag
 */
enum {
	BFA_MFG_VPD_UNKNOWN	= 0,     /*  vendor unknown 		*/
	BFA_MFG_VPD_IBM 	= 1,     /*  vendor IBM 		*/
	BFA_MFG_VPD_HP  	= 2,     /*  vendor HP  		*/
	BFA_MFG_VPD_DELL        = 3,	 /*  vendor DELL                */
	BFA_MFG_VPD_PCI_IBM 	= 0x08,  /*  PCI VPD IBM     		*/
	BFA_MFG_VPD_PCI_HP  	= 0x10,  /*  PCI VPD HP			*/
	BFA_MFG_VPD_PCI_DELL    = 0x20,  /*  PCI VPD DELL           	*/
	BFA_MFG_VPD_PCI_BRCD 	= 0xf8,  /*  PCI VPD Brocade 		*/
};

/**
 * All numerical fields are in big-endian format.
 */
struct bfa_mfg_vpd_s {
	u8		version;	/*  vpd data version */
	u8		vpd_sig[3];	/*  characters 'V', 'P', 'D' */
	u8		chksum;		/*  u8 checksum */
	u8		vendor;		/*  vendor */
	u8 	len;		/*  vpd data length excluding header */
	u8 	rsv;
	u8		data[BFA_MFG_VPD_LEN];	/*  vpd data */
};

#pragma pack()

#endif /* __BFA_DEFS_MFG_H__ */
