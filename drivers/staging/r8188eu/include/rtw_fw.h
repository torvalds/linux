/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_FW_H__
#define __RTW_FW_H__

#include "drv_types.h"

#define MAX_REG_BOLCK_SIZE	196
#define FW_8188E_START_ADDRESS	0x1000
#define MAX_PAGE_SIZE		4096

#define IS_FW_HEADER_EXIST(_fwhdr)				\
	((le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x92C0 ||	\
	(le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x88C0 ||	\
	(le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x2300 ||	\
	(le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x88E0)

/*  This structure must be careful with byte-ordering */

struct rt_firmware_hdr {
	/*  8-byte alinment required */
	/*  LONG WORD 0 ---- */
	__le16		Signature;	/* 92C0: test chip; 92C,
					 * 88C0: test chip; 88C1: MP A-cut;
					 * 92C1: MP A-cut */
	u8		Category;	/*  AP/NIC and USB/PCI */
	u8		Function;	/*  Reserved for different FW function
					 *  indcation, for further use when
					 *  driver needs to download different
					 *  FW for different conditions */
	__le16		Version;	/*  FW Version */
	u8		Subversion;	/*  FW Subversion, default 0x00 */
	u16		Rsvd1;

	/*  LONG WORD 1 ---- */
	u8		Month;	/*  Release time Month field */
	u8		Date;	/*  Release time Date field */
	u8		Hour;	/*  Release time Hour field */
	u8		Minute;	/*  Release time Minute field */
	__le16		RamCodeSize;	/*  The size of RAM code */
	u8		Foundry;
	u8		Rsvd2;

	/*  LONG WORD 2 ---- */
	__le32		SvnIdx;	/*  The SVN entry index */
	u32		Rsvd3;

	/*  LONG WORD 3 ---- */
	u32		Rsvd4;
	u32		Rsvd5;
};

int rtl8188e_firmware_download(struct adapter *padapter);
void rtw_reset_8051(struct adapter *padapter);

#endif
