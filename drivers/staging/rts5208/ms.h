/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#ifndef __REALTEK_RTSX_MS_H
#define __REALTEK_RTSX_MS_H

#define MS_DELAY_WRITE

#define	MS_MAX_RETRY_COUNT	3

#define	MS_EXTRA_SIZE		0x9

#define	WRT_PRTCT		0x01

/* Error Code */
#define	MS_NO_ERROR				0x00
#define	MS_CRC16_ERROR				0x80
#define	MS_TO_ERROR				0x40
#define	MS_NO_CARD				0x20
#define	MS_NO_MEMORY				0x10
#define	MS_CMD_NK				0x08
#define	MS_FLASH_READ_ERROR			0x04
#define	MS_FLASH_WRITE_ERROR			0x02
#define	MS_BREQ_ERROR				0x01
#define	MS_NOT_FOUND				0x03

/* Transfer Protocol Command */
#define READ_PAGE_DATA				0x02
#define READ_REG				0x04
#define	GET_INT					0x07
#define WRITE_PAGE_DATA				0x0D
#define WRITE_REG				0x0B
#define SET_RW_REG_ADRS				0x08
#define SET_CMD					0x0E

#define	PRO_READ_LONG_DATA			0x02
#define	PRO_READ_SHORT_DATA			0x03
#define PRO_READ_REG				0x04
#define	PRO_READ_QUAD_DATA			0x05
#define PRO_GET_INT				0x07
#define	PRO_WRITE_LONG_DATA			0x0D
#define	PRO_WRITE_SHORT_DATA			0x0C
#define	PRO_WRITE_QUAD_DATA			0x0A
#define PRO_WRITE_REG				0x0B
#define PRO_SET_RW_REG_ADRS			0x08
#define PRO_SET_CMD				0x0E
#define PRO_EX_SET_CMD				0x09

#ifdef SUPPORT_MAGIC_GATE

#define MG_GET_ID		0x40
#define MG_SET_LID		0x41
#define MG_GET_LEKB		0x42
#define MG_SET_RD		0x43
#define MG_MAKE_RMS		0x44
#define MG_MAKE_KSE		0x45
#define MG_SET_IBD		0x46
#define MG_GET_IBD		0x47

#endif

#ifdef XC_POWERCLASS
#define XC_CHG_POWER		0x16
#endif

#define BLOCK_READ	0xAA
#define	BLOCK_WRITE	0x55
#define BLOCK_END	0x33
#define BLOCK_ERASE	0x99
#define FLASH_STOP	0xCC

#define SLEEP		0x5A
#define CLEAR_BUF	0xC3
#define MS_RESET	0x3C

#define PRO_READ_DATA		0x20
#define	PRO_WRITE_DATA		0x21
#define PRO_READ_ATRB		0x24
#define PRO_STOP		0x25
#define PRO_ERASE		0x26
#define	PRO_READ_2K_DATA	0x27
#define	PRO_WRITE_2K_DATA	0x28

#define PRO_FORMAT		0x10
#define PRO_SLEEP		0x11

#define	IntReg			0x01
#define StatusReg0		0x02
#define StatusReg1		0x03

#define SystemParm		0x10
#define BlockAdrs		0x11
#define CMDParm			0x14
#define PageAdrs		0x15

#define OverwriteFlag		0x16
#define ManagemenFlag		0x17
#define LogicalAdrs		0x18
#define ReserveArea		0x1A

#define	Pro_IntReg		0x01
#define Pro_StatusReg		0x02
#define Pro_TypeReg		0x04
#define	Pro_IFModeReg		0x05
#define Pro_CatagoryReg		0x06
#define Pro_ClassReg		0x07

#define Pro_SystemParm		0x10
#define Pro_DataCount1		0x11
#define Pro_DataCount0		0x12
#define Pro_DataAddr3		0x13
#define Pro_DataAddr2		0x14
#define Pro_DataAddr1		0x15
#define Pro_DataAddr0		0x16

#define Pro_TPCParm		0x17
#define Pro_CMDParm		0x18

#define	INT_REG_CED		0x80
#define	INT_REG_ERR		0x40
#define	INT_REG_BREQ		0x20
#define	INT_REG_CMDNK		0x01

#define	BLOCK_BOOT		0xC0
#define	BLOCK_OK		0x80
#define	PAGE_OK			0x60
#define	DATA_COMPL		0x10

#define	NOT_BOOT_BLOCK		0x4
#define	NOT_TRANSLATION_TABLE	0x8

#define	HEADER_ID0		PPBUF_BASE2
#define	HEADER_ID1		(PPBUF_BASE2 + 1)
#define	DISABLED_BLOCK0		(PPBUF_BASE2 + 0x170 + 4)
#define	DISABLED_BLOCK1		(PPBUF_BASE2 + 0x170 + 5)
#define	DISABLED_BLOCK2		(PPBUF_BASE2 + 0x170 + 6)
#define	DISABLED_BLOCK3		(PPBUF_BASE2 + 0x170 + 7)
#define	BLOCK_SIZE_0		(PPBUF_BASE2 + 0x1a0 + 2)
#define	BLOCK_SIZE_1		(PPBUF_BASE2 + 0x1a0 + 3)
#define	BLOCK_COUNT_0		(PPBUF_BASE2 + 0x1a0 + 4)
#define	BLOCK_COUNT_1		(PPBUF_BASE2 + 0x1a0 + 5)
#define	EBLOCK_COUNT_0		(PPBUF_BASE2 + 0x1a0 + 6)
#define	EBLOCK_COUNT_1		(PPBUF_BASE2 + 0x1a0 + 7)
#define	PAGE_SIZE_0		(PPBUF_BASE2 + 0x1a0 + 8)
#define	PAGE_SIZE_1		(PPBUF_BASE2 + 0x1a0 + 9)

#define MS_Device_Type		(PPBUF_BASE2 + 0x1D8)

#define	MS_4bit_Support		(PPBUF_BASE2 + 0x1D3)

#define setPS_NG	1
#define setPS_Error	0

#define	PARALLEL_8BIT_IF	0x40
#define	PARALLEL_4BIT_IF	0x00
#define	SERIAL_IF		0x80

#define BUF_FULL	0x10
#define BUF_EMPTY	0x20

#define	MEDIA_BUSY	0x80
#define	FLASH_BUSY	0x40
#define	DATA_ERROR	0x20
#define	STS_UCDT	0x10
#define	EXTRA_ERROR	0x08
#define	STS_UCEX	0x04
#define	FLAG_ERROR	0x02
#define	STS_UCFG	0x01

#define MS_SHORT_DATA_LEN	32

#define FORMAT_SUCCESS		0
#define FORMAT_FAIL		1
#define FORMAT_IN_PROGRESS	2

#define	MS_SET_BAD_BLOCK_FLG(ms_card)	((ms_card)->multi_flag |= 0x80)
#define MS_CLR_BAD_BLOCK_FLG(ms_card)	((ms_card)->multi_flag &= 0x7F)
#define MS_TST_BAD_BLOCK_FLG(ms_card)	((ms_card)->multi_flag & 0x80)

void mspro_polling_format_status(struct rtsx_chip *chip);

void mspro_stop_seq_mode(struct rtsx_chip *chip);
int reset_ms_card(struct rtsx_chip *chip);
int ms_rw(struct scsi_cmnd *srb, struct rtsx_chip *chip,
	  u32 start_sector, u16 sector_cnt);
int mspro_format(struct scsi_cmnd *srb, struct rtsx_chip *chip,
		 int short_data_len, bool quick_format);
void ms_free_l2p_tbl(struct rtsx_chip *chip);
void ms_cleanup_work(struct rtsx_chip *chip);
int ms_power_off_card3v3(struct rtsx_chip *chip);
int release_ms_card(struct rtsx_chip *chip);
#ifdef MS_DELAY_WRITE
int ms_delay_write(struct rtsx_chip *chip);
#endif

#ifdef SUPPORT_MAGIC_GATE
int mg_set_leaf_id(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int mg_get_local_EKB(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int mg_chg(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int mg_get_rsp_chg(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int mg_rsp(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int mg_get_ICV(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int mg_set_ICV(struct scsi_cmnd *srb, struct rtsx_chip *chip);
#endif

#endif  /* __REALTEK_RTSX_MS_H */
