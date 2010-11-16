/*---------------------------------------------------------------------------
   FT1000 driver for Flarion Flash OFDM NIC Device

   Copyright (C) 2002 Flarion Technologies, All rights reserved.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option) any
   later version. This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details. You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place -
   Suite 330, Boston, MA 02111-1307, USA.
  --------------------------------------------------------------------------

   Description:  This module will handshake with the DSP bootloader to
                 download the DSP runtime image.

---------------------------------------------------------------------------*/

#define __KERNEL_SYSCALLS__

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>

#include "ft1000_dev.h"
#include "ft1000.h"
#include "boot.h"

#ifdef FT_DEBUG
#define DEBUG(n, args...) printk(KERN_DEBUG args);
#else
#define DEBUG(n, args...)
#endif

#define  MAX_DSP_WAIT_LOOPS      100
#define  DSP_WAIT_SLEEP_TIME     1	/* 1 millisecond */

#define  MAX_LENGTH              0x7f0

#define  DWNLD_MAG_HANDSHAKE_LOC 0x00
#define  DWNLD_MAG_TYPE_LOC      0x01
#define  DWNLD_MAG_SIZE_LOC      0x02
#define  DWNLD_MAG_PS_HDR_LOC    0x03

#define  DWNLD_HANDSHAKE_LOC     0x02
#define  DWNLD_TYPE_LOC          0x04
#define  DWNLD_SIZE_MSW_LOC      0x06
#define  DWNLD_SIZE_LSW_LOC      0x08
#define  DWNLD_PS_HDR_LOC        0x0A

#define  HANDSHAKE_TIMEOUT_VALUE 0xF1F1
#define  HANDSHAKE_RESET_VALUE   0xFEFE	/* When DSP requests startover */
#define  HANDSHAKE_DSP_BL_READY  0xFEFE	/* At start DSP writes this when bootloader ready */
#define  HANDSHAKE_DRIVER_READY  0xFFFF	/* Driver writes after receiving 0xFEFE */
#define  HANDSHAKE_SEND_DATA     0x0000	/* DSP writes this when ready for more data */

#define  HANDSHAKE_REQUEST       0x0001	/* Request from DSP */
#define  HANDSHAKE_RESPONSE      0x0000	/* Satisfied DSP request */

#define  REQUEST_CODE_LENGTH     0x0000
#define  REQUEST_RUN_ADDRESS     0x0001
#define  REQUEST_CODE_SEGMENT    0x0002	/* In WORD count */
#define  REQUEST_DONE_BL         0x0003
#define  REQUEST_DONE_CL         0x0004
#define  REQUEST_VERSION_INFO    0x0005
#define  REQUEST_CODE_BY_VERSION 0x0006
#define  REQUEST_MAILBOX_DATA    0x0007
#define  REQUEST_FILE_CHECKSUM   0x0008

#define  STATE_START_DWNLD       0x01
#define  STATE_BOOT_DWNLD        0x02
#define  STATE_CODE_DWNLD        0x03
#define  STATE_DONE_DWNLD        0x04
#define  STATE_SECTION_PROV      0x05
#define  STATE_DONE_PROV         0x06
#define  STATE_DONE_FILE         0x07

USHORT get_handshake(struct net_device *dev, USHORT expected_value);
void put_handshake(struct net_device *dev, USHORT handshake_value);
USHORT get_request_type(struct net_device *dev);
long get_request_value(struct net_device *dev);
void put_request_value(struct net_device *dev, long lvalue);
USHORT hdr_checksum(PPSEUDO_HDR pHdr);

typedef struct _DSP_FILE_HDR {
	long build_date;
	long dsp_coff_date;
	long loader_code_address;
	long loader_code_size;
	long loader_code_end;
	long dsp_code_address;
	long dsp_code_size;
	long dsp_code_end;
	long reserved[8];
} __attribute__ ((packed)) DSP_FILE_HDR, *PDSP_FILE_HDR;

typedef struct _DSP_FILE_HDR_5 {
	long version_id;	// Version ID of this image format.
	long package_id;	// Package ID of code release.
	long build_date;	// Date/time stamp when file was built.
	long commands_offset;	// Offset to attached commands in Pseudo Hdr format.
	long loader_offset;	// Offset to bootloader code.
	long loader_code_address;	// Start address of bootloader.
	long loader_code_end;	// Where bootloader code ends.
	long loader_code_size;
	long version_data_offset;	// Offset were scrambled version data begins.
	long version_data_size;	// Size, in words, of scrambled version data.
	long nDspImages;	// Number of DSP images in file.
} __attribute__ ((packed)) DSP_FILE_HDR_5, *PDSP_FILE_HDR_5;

typedef struct _DSP_IMAGE_INFO {
	long coff_date;		// Date/time when DSP Coff image was built.
	long begin_offset;	// Offset in file where image begins.
	long end_offset;	// Offset in file where image begins.
	long run_address;	// On chip Start address of DSP code.
	long image_size;	// Size of image.
	long version;		// Embedded version # of DSP code.
} __attribute__ ((packed)) DSP_IMAGE_INFO, *PDSP_IMAGE_INFO;

typedef struct _DSP_IMAGE_INFO_V6 {
	long coff_date;		// Date/time when DSP Coff image was built.
	long begin_offset;	// Offset in file where image begins.
	long end_offset;	// Offset in file where image begins.
	long run_address;	// On chip Start address of DSP code.
	long image_size;	// Size of image.
	long version;		// Embedded version # of DSP code.
	unsigned short checksum;	// Dsp File checksum
	unsigned short pad1;
} __attribute__ ((packed)) DSP_IMAGE_INFO_V6, *PDSP_IMAGE_INFO_V6;

void card_bootload(struct net_device *dev)
{
	FT1000_INFO *info = (PFT1000_INFO) netdev_priv(dev);
	unsigned long flags;
	PULONG pdata;
	UINT size;
	UINT i;
	ULONG templong;

	DEBUG(0, "card_bootload is called\n");

	pdata = (PULONG) bootimage;
	size = sizeof(bootimage);

	// check for odd word
	if (size & 0x0003) {
		size += 4;
	}
	// Provide mutual exclusive access while reading ASIC registers.
	spin_lock_irqsave(&info->dpram_lock, flags);

	// need to set i/o base address initially and hardware will autoincrement
	ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, FT1000_DPRAM_BASE);
	// write bytes
	for (i = 0; i < (size >> 2); i++) {
		templong = *pdata++;
		outl(templong, dev->base_addr + FT1000_REG_MAG_DPDATA);
	}

	spin_unlock_irqrestore(&info->dpram_lock, flags);
}

USHORT get_handshake(struct net_device *dev, USHORT expected_value)
{
	FT1000_INFO *info = (PFT1000_INFO) netdev_priv(dev);
	USHORT handshake;
	ULONG tempx;
	int loopcnt;

	loopcnt = 0;
	while (loopcnt < MAX_DSP_WAIT_LOOPS) {
		if (info->AsicID == ELECTRABUZZ_ID) {
			ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
					 DWNLD_HANDSHAKE_LOC);

			handshake = ft1000_read_reg(dev, FT1000_REG_DPRAM_DATA);
		} else {
			tempx =
				ntohl(ft1000_read_dpram_mag_32
				  (dev, DWNLD_MAG_HANDSHAKE_LOC));
			handshake = (USHORT) tempx;
		}

		if ((handshake == expected_value)
			|| (handshake == HANDSHAKE_RESET_VALUE)) {
			return handshake;
		} else {
			loopcnt++;
			mdelay(DSP_WAIT_SLEEP_TIME);
		}

	}

	return HANDSHAKE_TIMEOUT_VALUE;

}

void put_handshake(struct net_device *dev, USHORT handshake_value)
{
	FT1000_INFO *info = (PFT1000_INFO) netdev_priv(dev);
	ULONG tempx;

	if (info->AsicID == ELECTRABUZZ_ID) {
		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 DWNLD_HANDSHAKE_LOC);
		ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, handshake_value);	/* Handshake */
	} else {
		tempx = (ULONG) handshake_value;
		tempx = ntohl(tempx);
		ft1000_write_dpram_mag_32(dev, DWNLD_MAG_HANDSHAKE_LOC, tempx);	/* Handshake */
	}
}

USHORT get_request_type(struct net_device *dev)
{
	FT1000_INFO *info = (PFT1000_INFO) netdev_priv(dev);
	USHORT request_type;
	ULONG tempx;

	if (info->AsicID == ELECTRABUZZ_ID) {
		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, DWNLD_TYPE_LOC);
		request_type = ft1000_read_reg(dev, FT1000_REG_DPRAM_DATA);
	} else {
		tempx = ft1000_read_dpram_mag_32(dev, DWNLD_MAG_TYPE_LOC);
		tempx = ntohl(tempx);
		request_type = (USHORT) tempx;
	}

	return request_type;

}

long get_request_value(struct net_device *dev)
{
	FT1000_INFO *info = (PFT1000_INFO) netdev_priv(dev);
	long value;
	USHORT w_val;

	if (info->AsicID == ELECTRABUZZ_ID) {
		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 DWNLD_SIZE_MSW_LOC);

		w_val = ft1000_read_reg(dev, FT1000_REG_DPRAM_DATA);

		value = (long)(w_val << 16);

		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 DWNLD_SIZE_LSW_LOC);

		w_val = ft1000_read_reg(dev, FT1000_REG_DPRAM_DATA);

		value = (long)(value | w_val);
	} else {
		value = ft1000_read_dpram_mag_32(dev, DWNLD_MAG_SIZE_LOC);
		value = ntohl(value);
	}

	return value;

}

void put_request_value(struct net_device *dev, long lvalue)
{
	FT1000_INFO *info = (PFT1000_INFO) netdev_priv(dev);
	USHORT size;
	ULONG tempx;

	if (info->AsicID == ELECTRABUZZ_ID) {
		size = (USHORT) (lvalue >> 16);

		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 DWNLD_SIZE_MSW_LOC);

		ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, size);

		size = (USHORT) (lvalue);

		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 DWNLD_SIZE_LSW_LOC);

		ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, size);
	} else {
		tempx = ntohl(lvalue);
		ft1000_write_dpram_mag_32(dev, DWNLD_MAG_SIZE_LOC, tempx);	/* Handshake */
	}

}

USHORT hdr_checksum(PPSEUDO_HDR pHdr)
{
	USHORT *usPtr = (USHORT *) pHdr;
	USHORT chksum;

	chksum = ((((((usPtr[0] ^ usPtr[1]) ^ usPtr[2]) ^ usPtr[3]) ^
			usPtr[4]) ^ usPtr[5]) ^ usPtr[6]);

	return chksum;
}

int card_download(struct net_device *dev, void *pFileStart, UINT FileLength)
{
	FT1000_INFO *info = (PFT1000_INFO) netdev_priv(dev);
	int Status = SUCCESS;
	USHORT DspWordCnt = 0;
	UINT uiState;
	USHORT handshake;
	PPSEUDO_HDR pHdr;
	USHORT usHdrLength;
	PDSP_FILE_HDR pFileHdr;
	long word_length;
	USHORT request;
	USHORT temp;
	PPROV_RECORD pprov_record;
	PUCHAR pbuffer;
	PDSP_FILE_HDR_5 pFileHdr5;
	PDSP_IMAGE_INFO pDspImageInfo = NULL;
	PDSP_IMAGE_INFO_V6 pDspImageInfoV6 = NULL;
	long requested_version;
	BOOLEAN bGoodVersion = 0;
	PDRVMSG pMailBoxData;
	USHORT *pUsData = NULL;
	USHORT *pUsFile = NULL;
	UCHAR *pUcFile = NULL;
	UCHAR *pBootEnd = NULL;
	UCHAR *pCodeEnd = NULL;
	int imageN;
	long file_version;
	long loader_code_address = 0;
	long loader_code_size = 0;
	long run_address = 0;
	long run_size = 0;
	unsigned long flags;
	unsigned long templong;
	unsigned long image_chksum = 0;

	//
	// Get version id of file, at first 4 bytes of file, for newer files.
	//
	file_version = *(long *)pFileStart;

	uiState = STATE_START_DWNLD;

	pFileHdr = (PDSP_FILE_HDR) pFileStart;
	pFileHdr5 = (PDSP_FILE_HDR_5) pFileStart;

	switch (file_version) {
	case 5:
	case 6:
		pUsFile =
			(USHORT *) ((long)pFileStart + pFileHdr5->loader_offset);
		pUcFile =
			(UCHAR *) ((long)pFileStart + pFileHdr5->loader_offset);

		pBootEnd =
			(UCHAR *) ((long)pFileStart + pFileHdr5->loader_code_end);

		loader_code_address = pFileHdr5->loader_code_address;
		loader_code_size = pFileHdr5->loader_code_size;
		bGoodVersion = FALSE;
		break;

	default:
		Status = FAILURE;
		break;
	}

	while ((Status == SUCCESS) && (uiState != STATE_DONE_FILE)) {

		switch (uiState) {
		case STATE_START_DWNLD:

			handshake = get_handshake(dev, HANDSHAKE_DSP_BL_READY);

			if (handshake == HANDSHAKE_DSP_BL_READY) {
				put_handshake(dev, HANDSHAKE_DRIVER_READY);
			} else {
				Status = FAILURE;
			}

			uiState = STATE_BOOT_DWNLD;

			break;

		case STATE_BOOT_DWNLD:
			handshake = get_handshake(dev, HANDSHAKE_REQUEST);
			if (handshake == HANDSHAKE_REQUEST) {
				/*
				 * Get type associated with the request.
				 */
				request = get_request_type(dev);
				switch (request) {
				case REQUEST_RUN_ADDRESS:
					put_request_value(dev,
							  loader_code_address);
					break;
				case REQUEST_CODE_LENGTH:
					put_request_value(dev,
							  loader_code_size);
					break;
				case REQUEST_DONE_BL:
					/* Reposition ptrs to beginning of code section */
					pUsFile = (USHORT *) ((long)pBootEnd);
					pUcFile = (UCHAR *) ((long)pBootEnd);
					uiState = STATE_CODE_DWNLD;
					break;
				case REQUEST_CODE_SEGMENT:
					word_length = get_request_value(dev);
					if (word_length > MAX_LENGTH) {
						Status = FAILURE;
						break;
					}
					if ((word_length * 2 + (long)pUcFile) >
						(long)pBootEnd) {
						/*
						 * Error, beyond boot code range.
						 */
						Status = FAILURE;
						break;
					}
					// Provide mutual exclusive access while reading ASIC registers.
					spin_lock_irqsave(&info->dpram_lock,
							  flags);
					if (file_version == 5) {
						/*
						 * Position ASIC DPRAM auto-increment pointer.
						 */
						ft1000_write_reg(dev,
								 FT1000_REG_DPRAM_ADDR,
								 DWNLD_PS_HDR_LOC);

						for (; word_length > 0; word_length--) {	/* In words */
							//temp = *pUsFile;
							//temp = RtlUshortByteSwap(temp);
							ft1000_write_reg(dev,
									 FT1000_REG_DPRAM_DATA,
									 *pUsFile);
							pUsFile++;
							pUcFile += 2;
							DspWordCnt++;
						}
					} else {
						/*
						 * Position ASIC DPRAM auto-increment pointer.
						 */
						outw(DWNLD_MAG_PS_HDR_LOC,
							 dev->base_addr +
							 FT1000_REG_DPRAM_ADDR);
						if (word_length & 0x01) {
							word_length++;
						}
						word_length = word_length / 2;

						for (; word_length > 0; word_length--) {	/* In words */
							templong = *pUsFile++;
							templong |=
								(*pUsFile++ << 16);
							pUcFile += 4;
							outl(templong,
								 dev->base_addr +
								 FT1000_REG_MAG_DPDATAL);
						}
					}
					spin_unlock_irqrestore(&info->
								   dpram_lock,
								   flags);
					break;
				default:
					Status = FAILURE;
					break;
				}
				put_handshake(dev, HANDSHAKE_RESPONSE);
			} else {
				Status = FAILURE;
			}

			break;

		case STATE_CODE_DWNLD:
			handshake = get_handshake(dev, HANDSHAKE_REQUEST);
			if (handshake == HANDSHAKE_REQUEST) {
				/*
				 * Get type associated with the request.
				 */
				request = get_request_type(dev);
				switch (request) {
				case REQUEST_FILE_CHECKSUM:
					DEBUG(0,
						  "ft1000_dnld: REQUEST_FOR_CHECKSUM\n");
					put_request_value(dev, image_chksum);
					break;
				case REQUEST_RUN_ADDRESS:
					if (bGoodVersion) {
						put_request_value(dev,
								  run_address);
					} else {
						Status = FAILURE;
						break;
					}
					break;
				case REQUEST_CODE_LENGTH:
					if (bGoodVersion) {
						put_request_value(dev,
								  run_size);
					} else {
						Status = FAILURE;
						break;
					}
					break;
				case REQUEST_DONE_CL:
					/* Reposition ptrs to beginning of provisioning section */
					switch (file_version) {
					case 5:
					case 6:
						pUsFile =
							(USHORT *) ((long)pFileStart
								+
								pFileHdr5->
								commands_offset);
						pUcFile =
							(UCHAR *) ((long)pFileStart
								   +
								   pFileHdr5->
								   commands_offset);
						break;
					default:
						Status = FAILURE;
						break;
					}
					uiState = STATE_DONE_DWNLD;
					break;
				case REQUEST_CODE_SEGMENT:
					if (!bGoodVersion) {
						Status = FAILURE;
						break;
					}
					word_length = get_request_value(dev);
					if (word_length > MAX_LENGTH) {
						Status = FAILURE;
						break;
					}
					if ((word_length * 2 + (long)pUcFile) >
						(long)pCodeEnd) {
						/*
						 * Error, beyond boot code range.
						 */
						Status = FAILURE;
						break;
					}
					if (file_version == 5) {
						/*
						 * Position ASIC DPRAM auto-increment pointer.
						 */
						ft1000_write_reg(dev,
								 FT1000_REG_DPRAM_ADDR,
								 DWNLD_PS_HDR_LOC);

						for (; word_length > 0; word_length--) {	/* In words */
							//temp = *pUsFile;
							//temp = RtlUshortByteSwap(temp);
							ft1000_write_reg(dev,
									 FT1000_REG_DPRAM_DATA,
									 *pUsFile);
							pUsFile++;
							pUcFile += 2;
							DspWordCnt++;
						}
					} else {
						/*
						 * Position ASIC DPRAM auto-increment pointer.
						 */
						outw(DWNLD_MAG_PS_HDR_LOC,
							 dev->base_addr +
							 FT1000_REG_DPRAM_ADDR);
						if (word_length & 0x01) {
							word_length++;
						}
						word_length = word_length / 2;

						for (; word_length > 0; word_length--) {	/* In words */
							templong = *pUsFile++;
							templong |=
								(*pUsFile++ << 16);
							pUcFile += 4;
							outl(templong,
								 dev->base_addr +
								 FT1000_REG_MAG_DPDATAL);
						}
					}
					break;

				case REQUEST_MAILBOX_DATA:
					// Convert length from byte count to word count. Make sure we round up.
					word_length =
						(long)(info->DSPInfoBlklen + 1) / 2;
					put_request_value(dev, word_length);
					pMailBoxData =
						(PDRVMSG) & info->DSPInfoBlk[0];
					pUsData =
						(USHORT *) & pMailBoxData->data[0];
					// Provide mutual exclusive access while reading ASIC registers.
					spin_lock_irqsave(&info->dpram_lock,
							  flags);
					if (file_version == 5) {
						/*
						 * Position ASIC DPRAM auto-increment pointer.
						 */
						ft1000_write_reg(dev,
								 FT1000_REG_DPRAM_ADDR,
								 DWNLD_PS_HDR_LOC);

						for (; word_length > 0; word_length--) {	/* In words */
							temp = ntohs(*pUsData);
							ft1000_write_reg(dev,
									 FT1000_REG_DPRAM_DATA,
									 temp);
							pUsData++;
						}
					} else {
						/*
						 * Position ASIC DPRAM auto-increment pointer.
						 */
						outw(DWNLD_MAG_PS_HDR_LOC,
							 dev->base_addr +
							 FT1000_REG_DPRAM_ADDR);
						if (word_length & 0x01) {
							word_length++;
						}
						word_length = word_length / 2;

						for (; word_length > 0; word_length--) {	/* In words */
							templong = *pUsData++;
							templong |=
								(*pUsData++ << 16);
							outl(templong,
								 dev->base_addr +
								 FT1000_REG_MAG_DPDATAL);
						}
					}
					spin_unlock_irqrestore(&info->
								   dpram_lock,
								   flags);
					break;

				case REQUEST_VERSION_INFO:
					word_length =
						pFileHdr5->version_data_size;
					put_request_value(dev, word_length);
					pUsFile =
						(USHORT *) ((long)pFileStart +
							pFileHdr5->
							version_data_offset);
					// Provide mutual exclusive access while reading ASIC registers.
					spin_lock_irqsave(&info->dpram_lock,
							  flags);
					if (file_version == 5) {
						/*
						 * Position ASIC DPRAM auto-increment pointer.
						 */
						ft1000_write_reg(dev,
								 FT1000_REG_DPRAM_ADDR,
								 DWNLD_PS_HDR_LOC);

						for (; word_length > 0; word_length--) {	/* In words */
							ft1000_write_reg(dev,
									 FT1000_REG_DPRAM_DATA,
									 *pUsFile
									 /*temp */
								);
							pUsFile++;
						}
					} else {
						/*
						 * Position ASIC DPRAM auto-increment pointer.
						 */
						outw(DWNLD_MAG_PS_HDR_LOC,
							 dev->base_addr +
							 FT1000_REG_DPRAM_ADDR);
						if (word_length & 0x01) {
							word_length++;
						}
						word_length = word_length / 2;

						for (; word_length > 0; word_length--) {	/* In words */
							templong =
								ntohs(*pUsFile++);
							temp =
								ntohs(*pUsFile++);
							templong |=
								(temp << 16);
							outl(templong,
								 dev->base_addr +
								 FT1000_REG_MAG_DPDATAL);
						}
					}
					spin_unlock_irqrestore(&info->
								   dpram_lock,
								   flags);
					break;

				case REQUEST_CODE_BY_VERSION:
					bGoodVersion = FALSE;
					requested_version =
						get_request_value(dev);
					if (file_version == 5) {
						pDspImageInfo =
							(PDSP_IMAGE_INFO) ((long)
									   pFileStart
									   +
									   sizeof
									   (DSP_FILE_HDR_5));
						for (imageN = 0;
							 imageN <
							 pFileHdr5->nDspImages;
							 imageN++) {
							if (pDspImageInfo->
								version ==
								requested_version) {
								bGoodVersion =
									TRUE;
								pUsFile =
									(USHORT
									 *) ((long)
									 pFileStart
									 +
									 pDspImageInfo->
									 begin_offset);
								pUcFile =
									(UCHAR
									 *) ((long)
									 pFileStart
									 +
									 pDspImageInfo->
									 begin_offset);
								pCodeEnd =
									(UCHAR
									 *) ((long)
									 pFileStart
									 +
									 pDspImageInfo->
									 end_offset);
								run_address =
									pDspImageInfo->
									run_address;
								run_size =
									pDspImageInfo->
									image_size;
								break;
							}
							pDspImageInfo++;
						}
					} else {
						pDspImageInfoV6 =
							(PDSP_IMAGE_INFO_V6) ((long)
									  pFileStart
									  +
									  sizeof
									  (DSP_FILE_HDR_5));
						for (imageN = 0;
							 imageN <
							 pFileHdr5->nDspImages;
							 imageN++) {
							temp = (USHORT)
								(pDspImageInfoV6->
								 version);
							templong = temp;
							temp = (USHORT)
								(pDspImageInfoV6->
								 version >> 16);
							templong |=
								(temp << 16);
							if (templong ==
								requested_version) {
								bGoodVersion =
									TRUE;
								pUsFile =
									(USHORT
									 *) ((long)
									 pFileStart
									 +
									 pDspImageInfoV6->
									 begin_offset);
								pUcFile =
									(UCHAR
									 *) ((long)
									 pFileStart
									 +
									 pDspImageInfoV6->
									 begin_offset);
								pCodeEnd =
									(UCHAR
									 *) ((long)
									 pFileStart
									 +
									 pDspImageInfoV6->
									 end_offset);
								run_address =
									pDspImageInfoV6->
									run_address;
								run_size =
									pDspImageInfoV6->
									image_size;
								image_chksum =
									(ULONG)
									pDspImageInfoV6->
									checksum;
								DEBUG(0,
									  "ft1000_dnld: image_chksum = 0x%8x\n",
									  (unsigned
									   int)
									  image_chksum);
								break;
							}
							pDspImageInfoV6++;
						}
					}
					if (!bGoodVersion) {
						/*
						 * Error, beyond boot code range.
						 */
						Status = FAILURE;
						break;
					}
					break;

				default:
					Status = FAILURE;
					break;
				}
				put_handshake(dev, HANDSHAKE_RESPONSE);
			} else {
				Status = FAILURE;
			}

			break;

		case STATE_DONE_DWNLD:
			if (((UINT) (pUcFile) - (UINT) pFileStart) >=
				(UINT) FileLength) {
				uiState = STATE_DONE_FILE;
				break;
			}

			pHdr = (PPSEUDO_HDR) pUsFile;

			if (pHdr->portdest == 0x80	/* DspOAM */
				&& (pHdr->portsrc == 0x00	/* Driver */
				|| pHdr->portsrc == 0x10 /* FMM */ )) {
				uiState = STATE_SECTION_PROV;
			} else {
				DEBUG(1,
					  "FT1000:download:Download error: Bad Port IDs in Pseudo Record\n");
				DEBUG(1, "\t Port Source = 0x%2.2x\n",
					  pHdr->portsrc);
				DEBUG(1, "\t Port Destination = 0x%2.2x\n",
					  pHdr->portdest);
				Status = FAILURE;
			}

			break;

		case STATE_SECTION_PROV:

			pHdr = (PPSEUDO_HDR) pUcFile;

			if (pHdr->checksum == hdr_checksum(pHdr)) {
				if (pHdr->portdest != 0x80 /* Dsp OAM */ ) {
					uiState = STATE_DONE_PROV;
					break;
				}
				usHdrLength = ntohs(pHdr->length);	/* Byte length for PROV records */

				// Get buffer for provisioning data
				pbuffer =
					kmalloc((usHdrLength + sizeof(PSEUDO_HDR)),
						GFP_ATOMIC);
				if (pbuffer) {
					memcpy(pbuffer, (void *)pUcFile,
						   (UINT) (usHdrLength +
							   sizeof(PSEUDO_HDR)));
					// link provisioning data
					pprov_record =
						kmalloc(sizeof(PROV_RECORD),
							GFP_ATOMIC);
					if (pprov_record) {
						pprov_record->pprov_data =
							pbuffer;
						list_add_tail(&pprov_record->
								  list,
								  &info->prov_list);
						// Move to next entry if available
						pUcFile =
							(UCHAR *) ((UINT) pUcFile +
								   (UINT) ((usHdrLength + 1) & 0xFFFFFFFE) + sizeof(PSEUDO_HDR));
						if ((UINT) (pUcFile) -
							(UINT) (pFileStart) >=
							(UINT) FileLength) {
							uiState =
								STATE_DONE_FILE;
						}
					} else {
						kfree(pbuffer);
						Status = FAILURE;
					}
				} else {
					Status = FAILURE;
				}
			} else {
				/* Checksum did not compute */
				Status = FAILURE;
			}

			break;

		case STATE_DONE_PROV:
			uiState = STATE_DONE_FILE;
			break;

		default:
			Status = FAILURE;
			break;
		}		/* End Switch */

	}			/* End while */

	return Status;

}
