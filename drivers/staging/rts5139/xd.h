/* Driver for Realtek RTS51xx USB card reader
 * Header file
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#ifndef __RTS51X_XD_H
#define __RTS51X_XD_H

/* Error Codes */
#define	XD_NO_ERROR			0x00
#define	XD_NO_MEMORY			0x80
#define	XD_PRG_ERROR			0x40
#define	XD_NO_CARD			0x20
#define	XD_READ_FAIL			0x10
#define	XD_ERASE_FAIL			0x08
#define	XD_WRITE_FAIL			0x04
#define	XD_ECC_ERROR			0x02
#define	XD_TO_ERROR			0x01

/* XD Commands */
#define	READ1_1				0x00
#define	READ1_2				0x01
#define	READ2				0x50
#define READ_ID				0x90
#define RESET				0xff
#define PAGE_PRG_1			0x80
#define PAGE_PRG_2			0x10
#define	BLK_ERASE_1			0x60
#define	BLK_ERASE_2			0xD0
#define READ_STS			0x70
#define READ_xD_ID			0x9A
#define	COPY_BACK_512			0x8A
#define	COPY_BACK_2K			0x85
#define	READ1_1_2			0x30
#define	READ1_1_3			0x35
#define	CHG_DAT_OUT_1			0x05
#define RDM_DAT_OUT_1			0x05
#define	CHG_DAT_OUT_2			0xE0
#define RDM_DAT_OUT_2			0xE0
#define	CHG_DAT_OUT_2			0xE0
#define	CHG_DAT_IN_1			0x85
#define	CACHE_PRG			0x15

/* Redundant Area Related */
#define XD_EXTRA_SIZE			0x10
#define XD_2K_EXTRA_SIZE		0x40

/* Define for XD Status  */
#define	NOT_WRITE_PROTECTED		0x80
#define	READY_STATE			0x40
#define	PROGRAM_ERROR			0x01
#define	PROGRAM_ERROR_N_1		0x02
#define	INTERNAL_READY			0x20
#define	READY_FLAG			0x5F

/* Define for device code */
#define	XD_8M_X8_512			0xE6
#define	XD_16M_X8_512			0x73
#define	XD_32M_X8_512			0x75
#define	XD_64M_X8_512			0x76
#define	XD_128M_X8_512			0x79
#define	XD_256M_X8_512			0x71
#define	XD_128M_X8_2048			0xF1
#define	XD_256M_X8_2048			0xDA
#define	XD_512M_X8			0xDC
#define	XD_128M_X16_2048		0xC1
#define	XD_4M_X8_512_1			0xE3
#define	XD_4M_X8_512_2			0xE5
#define	xD_1G_X8_512			0xD3
#define	xD_2G_X8_512			0xD5

#define	XD_ID_CODE			0xB5

#define	VENDOR_BLOCK			0xEFFF
#define	CIS_BLOCK			0xDFFF

#define	BLK_NOT_FOUND			0xFFFFFFFF

#define	NO_NEW_BLK			0xFFFFFFFF

#define	PAGE_CORRECTABLE		0x0
#define	PAGE_NOTCORRECTABLE		0x1

#define	NO_OFFSET			0x0
#define	WITH_OFFSET			0x1

#define	Sect_Per_Page			4
#define	XD_ADDR_MODE_2C			XD_ADDR_MODE_2A

#define ZONE0_BAD_BLOCK			23
#define NOT_ZONE0_BAD_BLOCK		24

/* Assign address mode */
#define	XD_RW_ADDR			0x01
#define	XD_ERASE_ADDR			0x02

/* Macro Definition */
#define	XD_PAGE_512(xd_card)		\
	do {	\
		(xd_card)->block_shift = 5;	\
		(xd_card)->page_off = 0x1F;	\
	} while (0)

#define	XD_SET_BAD_NEWBLK(xd_card)	((xd_card)->multi_flag |= 0x01)
#define	XD_CLR_BAD_NEWBLK(xd_card)	((xd_card)->multi_flag &= ~0x01)
#define	XD_CHK_BAD_NEWBLK(xd_card)	((xd_card)->multi_flag & 0x01)

#define	XD_SET_BAD_OLDBLK(xd_card)	((xd_card)->multi_flag |= 0x02)
#define	XD_CLR_BAD_OLDBLK(xd_card)	((xd_card)->multi_flag &= ~0x02)
#define	XD_CHK_BAD_OLDBLK(xd_card)	((xd_card)->multi_flag & 0x02)

#define	XD_SET_MBR_FAIL(xd_card)	((xd_card)->multi_flag |= 0x04)
#define	XD_CLR_MBR_FAIL(xd_card)	((xd_card)->multi_flag &= ~0x04)
#define	XD_CHK_MBR_FAIL(xd_card)	((xd_card)->multi_flag & 0x04)

#define	XD_SET_ECC_FLD_ERR(xd_card)	((xd_card)->multi_flag |= 0x08)
#define	XD_CLR_ECC_FLD_ERR(xd_card)	((xd_card)->multi_flag &= ~0x08)
#define	XD_CHK_ECC_FLD_ERR(xd_card)	((xd_card)->multi_flag & 0x08)

#define	XD_SET_4MB(xd_card)		((xd_card)->multi_flag |= 0x10)
#define	XD_CLR_4MB(xd_card)		((xd_card)->multi_flag &= ~0x10)
#define	XD_CHK_4MB(xd_card)		((xd_card)->multi_flag & 0x10)

#define	XD_SET_ECC_ERR(xd_card)		((xd_card)->multi_flag |= 0x40)
#define	XD_CLR_ECC_ERR(xd_card)		((xd_card)->multi_flag &= ~0x40)
#define	XD_CHK_ECC_ERR(xd_card)		((xd_card)->multi_flag & 0x40)

/* Offset in xD redundant buffer */
#define PAGE_STATUS		0
#define BLOCK_STATUS		1
#define BLOCK_ADDR1_L		2
#define BLOCK_ADDR1_H		3
#define BLOCK_ADDR2_L		4
#define BLOCK_ADDR2_H		5
#define RESERVED0		6
#define RESERVED1		7
#define RESERVED2		8
#define RESERVED3		9
#define PARITY			10

/* For CIS block */
#define	CIS0_0			0
#define	CIS0_1			1
#define	CIS0_2			2
#define	CIS0_3			3
#define	CIS0_4			4
#define	CIS0_5			5
#define	CIS0_6			6
#define	CIS0_7			7
#define	CIS0_8			8
#define	CIS0_9			9
#define	CIS1_0			256
#define	CIS1_1			(256 + 1)
#define	CIS1_2			(256 + 2)
#define	CIS1_3			(256 + 3)
#define	CIS1_4			(256 + 4)
#define	CIS1_5			(256 + 5)
#define	CIS1_6			(256 + 6)
#define	CIS1_7			(256 + 7)
#define	CIS1_8			(256 + 8)
#define	CIS1_9			(256 + 9)

int reset_xd_card(struct rts51x_chip *chip);
int xd_rw(struct scsi_cmnd *srb, struct rts51x_chip *chip, u32 start_sector,
	  u16 sector_cnt);
void xd_free_l2p_tbl(struct rts51x_chip *chip);
void xd_cleanup_work(struct rts51x_chip *chip);
int release_xd_card(struct rts51x_chip *chip);

#endif /* __RTS51X_XD_H */
