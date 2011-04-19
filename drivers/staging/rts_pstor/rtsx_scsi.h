/* Driver for Realtek PCI-Express card reader
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
 */

#ifndef __REALTEK_RTSX_SCSI_H
#define __REALTEK_RTSX_SCSI_H

#include "rtsx.h"
#include "rtsx_chip.h"

#define MS_SP_CMND		0xFA
#define MS_FORMAT		0xA0
#define GET_MS_INFORMATION	0xB0

#define VENDOR_CMND		0xF0

#define READ_STATUS		0x09

#define READ_EEPROM		0x04
#define WRITE_EEPROM		0x05
#define READ_MEM		0x0D
#define WRITE_MEM		0x0E
#define GET_BUS_WIDTH		0x13
#define GET_SD_CSD		0x14
#define TOGGLE_GPIO		0x15
#define TRACE_MSG		0x18

#define SCSI_APP_CMD		0x10

#define PP_READ10		0x1A
#define PP_WRITE10		0x0A
#define READ_HOST_REG		0x1D
#define WRITE_HOST_REG		0x0D
#define SET_VAR			0x05
#define GET_VAR			0x15
#define DMA_READ		0x16
#define DMA_WRITE		0x06
#define GET_DEV_STATUS		0x10
#define SET_CHIP_MODE		0x27
#define SUIT_CMD		0xE0
#define WRITE_PHY		0x07
#define READ_PHY		0x17
#define WRITE_EEPROM2		0x03
#define READ_EEPROM2		0x13
#define ERASE_EEPROM2		0x23
#define WRITE_EFUSE		0x04
#define READ_EFUSE		0x14
#define WRITE_CFG		0x0E
#define READ_CFG		0x1E

#define SPI_VENDOR_COMMAND		0x1C

#define	SCSI_SPI_GETSTATUS		0x00
#define	SCSI_SPI_SETPARAMETER		0x01
#define	SCSI_SPI_READFALSHID		0x02
#define	SCSI_SPI_READFLASH		0x03
#define	SCSI_SPI_WRITEFLASH		0x04
#define	SCSI_SPI_WRITEFLASHSTATUS	0x05
#define	SCSI_SPI_ERASEFLASH		0x06

#define INIT_BATCHCMD		0x41
#define ADD_BATCHCMD		0x42
#define SEND_BATCHCMD		0x43
#define GET_BATCHRSP		0x44

#define CHIP_NORMALMODE		0x00
#define CHIP_DEBUGMODE		0x01

/* SD Pass Through Command Extension */
#define SD_PASS_THRU_MODE	0xD0
#define SD_EXECUTE_NO_DATA	0xD1
#define SD_EXECUTE_READ		0xD2
#define SD_EXECUTE_WRITE	0xD3
#define SD_GET_RSP		0xD4
#define SD_HW_RST		0xD6

#ifdef SUPPORT_MAGIC_GATE
#define CMD_MSPRO_MG_RKEY	0xA4   /* Report Key Command */
#define CMD_MSPRO_MG_SKEY	0xA3   /* Send Key Command */

/* CBWCB field: key class */
#define KC_MG_R_PRO		0xBE   /* MG-R PRO*/

/* CBWCB field: key format */
#define KF_SET_LEAF_ID		0x31   /* Set Leaf ID */
#define KF_GET_LOC_EKB		0x32   /* Get Local EKB */
#define KF_CHG_HOST		0x33   /* Challenge (host) */
#define KF_RSP_CHG		0x34   /* Response and Challenge (device)  */
#define KF_RSP_HOST		0x35   /* Response (host) */
#define KF_GET_ICV		0x36   /* Get ICV */
#define KF_SET_ICV		0x37   /* SSet ICV */
#endif

/* Sense type */
#define	SENSE_TYPE_NO_SENSE				0
#define	SENSE_TYPE_MEDIA_CHANGE				1
#define	SENSE_TYPE_MEDIA_NOT_PRESENT			2
#define	SENSE_TYPE_MEDIA_LBA_OVER_RANGE			3
#define	SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT		4
#define	SENSE_TYPE_MEDIA_WRITE_PROTECT			5
#define	SENSE_TYPE_MEDIA_INVALID_CMD_FIELD		6
#define	SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR		7
#define	SENSE_TYPE_MEDIA_WRITE_ERR			8
#define SENSE_TYPE_FORMAT_IN_PROGRESS			9
#define SENSE_TYPE_FORMAT_CMD_FAILED			10
#ifdef SUPPORT_MAGIC_GATE
#define SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB		0x0b
#define SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN		0x0c
#define SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM		0x0d
#define SENSE_TYPE_MG_WRITE_ERR				0x0e
#endif
#ifdef SUPPORT_SD_LOCK
#define SENSE_TYPE_MEDIA_READ_FORBIDDEN			0x10  /* FOR Locked SD card*/
#endif

void scsi_show_command(struct scsi_cmnd *srb);
void set_sense_type(struct rtsx_chip *chip, unsigned int lun, int sense_type);
void set_sense_data(struct rtsx_chip *chip, unsigned int lun, u8 err_code, u8 sense_key,
		u32 info, u8 asc, u8 ascq, u8 sns_key_info0, u16 sns_key_info1);
int rtsx_scsi_handler(struct scsi_cmnd *srb, struct rtsx_chip *chip);

#endif   /* __REALTEK_RTSX_SCSI_H */

