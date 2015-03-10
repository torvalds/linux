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

  Description: This module will handshake with the DSP bootloader to
  download the DSP runtime image.

  ---------------------------------------------------------------------------*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define __KERNEL_SYSCALLS__

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "ft1000.h"
#include "boot.h"

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

u16 get_handshake(struct net_device *dev, u16 expected_value);
void put_handshake(struct net_device *dev, u16 handshake_value);
u16 get_request_type(struct net_device *dev);
long get_request_value(struct net_device *dev);
void put_request_value(struct net_device *dev, long lvalue);
u16 hdr_checksum(struct pseudo_hdr *pHdr);

struct dsp_file_hdr {
	u32  version_id;	/* Version ID of this image format. */
	u32  package_id;	/* Package ID of code release. */
	u32  build_date;	/* Date/time stamp when file was built. */
	u32  commands_offset;	/* Offset to attached commands in Pseudo Hdr format. */
	u32  loader_offset;	/* Offset to bootloader code. */
	u32  loader_code_address;	/* Start address of bootloader. */
	u32  loader_code_end;	/* Where bootloader code ends. */
	u32  loader_code_size;
	u32  version_data_offset;	/* Offset were scrambled version data begins. */
	u32  version_data_size;	/* Size, in words, of scrambled version data. */
	u32  nDspImages;	/* Number of DSP images in file. */
} __packed;

struct dsp_image_info {
	u32  coff_date;		/* Date/time when DSP Coff image was built. */
	u32  begin_offset;	/* Offset in file where image begins. */
	u32  end_offset;	/* Offset in file where image begins. */
	u32  run_address;	/* On chip Start address of DSP code. */
	u32  image_size;	/* Size of image. */
	u32  version;		/* Embedded version # of DSP code. */
	unsigned short checksum;	/* Dsp File checksum */
	unsigned short pad1;
} __packed;

void card_bootload(struct net_device *dev)
{
	struct ft1000_info *info = (struct ft1000_info *)netdev_priv(dev);
	unsigned long flags;
	u32 *pdata;
	u32 size;
	u32 i;
	u32 templong;

	netdev_dbg(dev, "card_bootload is called\n");

	pdata = (u32 *)bootimage;
	size = sizeof(bootimage);

	/* check for odd word */
	if (size & 0x0003)
		size += 4;

	/* Provide mutual exclusive access while reading ASIC registers. */
	spin_lock_irqsave(&info->dpram_lock, flags);

	/* need to set i/o base address initially and hardware will autoincrement */
	ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, FT1000_DPRAM_BASE);
	/* write bytes */
	for (i = 0; i < (size >> 2); i++) {
		templong = *pdata++;
		outl(templong, dev->base_addr + FT1000_REG_MAG_DPDATA);
	}

	spin_unlock_irqrestore(&info->dpram_lock, flags);
}

u16 get_handshake(struct net_device *dev, u16 expected_value)
{
	struct ft1000_info *info = (struct ft1000_info *)netdev_priv(dev);
	u16 handshake;
	u32 tempx;
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
			handshake = (u16)tempx;
		}

		if ((handshake == expected_value)
		    || (handshake == HANDSHAKE_RESET_VALUE)) {
			return handshake;
		}
		loopcnt++;
		mdelay(DSP_WAIT_SLEEP_TIME);

	}

	return HANDSHAKE_TIMEOUT_VALUE;

}

void put_handshake(struct net_device *dev, u16 handshake_value)
{
	struct ft1000_info *info = (struct ft1000_info *)netdev_priv(dev);
	u32 tempx;

	if (info->AsicID == ELECTRABUZZ_ID) {
		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 DWNLD_HANDSHAKE_LOC);
		ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, handshake_value);	/* Handshake */
	} else {
		tempx = (u32)handshake_value;
		tempx = ntohl(tempx);
		ft1000_write_dpram_mag_32(dev, DWNLD_MAG_HANDSHAKE_LOC, tempx);	/* Handshake */
	}
}

u16 get_request_type(struct net_device *dev)
{
	struct ft1000_info *info = (struct ft1000_info *)netdev_priv(dev);
	u16 request_type;
	u32 tempx;

	if (info->AsicID == ELECTRABUZZ_ID) {
		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, DWNLD_TYPE_LOC);
		request_type = ft1000_read_reg(dev, FT1000_REG_DPRAM_DATA);
	} else {
		tempx = ft1000_read_dpram_mag_32(dev, DWNLD_MAG_TYPE_LOC);
		tempx = ntohl(tempx);
		request_type = (u16)tempx;
	}

	return request_type;

}

long get_request_value(struct net_device *dev)
{
	struct ft1000_info *info = (struct ft1000_info *)netdev_priv(dev);
	long value;
	u16 w_val;

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
	struct ft1000_info *info = (struct ft1000_info *)netdev_priv(dev);
	u16 size;
	u32 tempx;

	if (info->AsicID == ELECTRABUZZ_ID) {
		size = (u16) (lvalue >> 16);

		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 DWNLD_SIZE_MSW_LOC);

		ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, size);

		size = (u16) (lvalue);

		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 DWNLD_SIZE_LSW_LOC);

		ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, size);
	} else {
		tempx = ntohl(lvalue);
		ft1000_write_dpram_mag_32(dev, DWNLD_MAG_SIZE_LOC, tempx);	/* Handshake */
	}

}

u16 hdr_checksum(struct pseudo_hdr *pHdr)
{
	u16 *usPtr = (u16 *)pHdr;
	u16 chksum;

	chksum = ((((((usPtr[0] ^ usPtr[1]) ^ usPtr[2]) ^ usPtr[3]) ^
		    usPtr[4]) ^ usPtr[5]) ^ usPtr[6]);

	return chksum;
}

int card_download(struct net_device *dev, const u8 *pFileStart,
		  size_t FileLength)
{
	struct ft1000_info *info = (struct ft1000_info *)netdev_priv(dev);
	int Status = SUCCESS;
	u32 uiState;
	u16 handshake;
	struct pseudo_hdr *pHdr;
	u16 usHdrLength;
	long word_length;
	u16 request;
	u16 temp;
	struct prov_record *pprov_record;
	u8 *pbuffer;
	struct dsp_file_hdr *pFileHdr5;
	struct dsp_image_info *pDspImageInfoV6 = NULL;
	long requested_version;
	bool bGoodVersion = false;
	struct drv_msg *pMailBoxData;
	u16 *pUsData = NULL;
	u16 *pUsFile = NULL;
	u8 *pUcFile = NULL;
	u8 *pBootEnd = NULL;
	u8 *pCodeEnd = NULL;
	int imageN;
	long file_version;
	long loader_code_address = 0;
	long loader_code_size = 0;
	long run_address = 0;
	long run_size = 0;
	unsigned long flags;
	unsigned long templong;
	unsigned long image_chksum = 0;

	file_version = *(long *)pFileStart;
	if (file_version != 6) {
		pr_err("unsupported firmware version %ld\n", file_version);
		Status = FAILURE;
	}

	uiState = STATE_START_DWNLD;

	pFileHdr5 = (struct dsp_file_hdr *)pFileStart;

	pUsFile = (u16 *) ((long)pFileStart + pFileHdr5->loader_offset);
	pUcFile = (u8 *) ((long)pFileStart + pFileHdr5->loader_offset);
	pBootEnd = (u8 *) ((long)pFileStart + pFileHdr5->loader_code_end);
	loader_code_address = pFileHdr5->loader_code_address;
	loader_code_size = pFileHdr5->loader_code_size;
	bGoodVersion = false;

	while ((Status == SUCCESS) && (uiState != STATE_DONE_FILE)) {

		switch (uiState) {
		case STATE_START_DWNLD:

			handshake = get_handshake(dev, HANDSHAKE_DSP_BL_READY);

			if (handshake == HANDSHAKE_DSP_BL_READY)
				put_handshake(dev, HANDSHAKE_DRIVER_READY);
			else
				Status = FAILURE;

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
					pUsFile = (u16 *) ((long)pBootEnd);
					pUcFile = (u8 *) ((long)pBootEnd);
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
					/* Provide mutual exclusive access while reading ASIC registers. */
					spin_lock_irqsave(&info->dpram_lock,
							  flags);
					/*
					 * Position ASIC DPRAM auto-increment pointer.
					 */
					outw(DWNLD_MAG_PS_HDR_LOC,
					     dev->base_addr +
					     FT1000_REG_DPRAM_ADDR);
					if (word_length & 0x01)
						word_length++;
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
					netdev_dbg(dev,
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
					pUsFile = (u16 *) ((long)pFileStart + pFileHdr5->commands_offset);
					pUcFile = (u8 *) ((long)pFileStart + pFileHdr5->commands_offset);
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
					/*
					 * Position ASIC DPRAM auto-increment pointer.
					 */
					outw(DWNLD_MAG_PS_HDR_LOC,
					     dev->base_addr +
					     FT1000_REG_DPRAM_ADDR);
					if (word_length & 0x01)
						word_length++;
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
					break;

				case REQUEST_MAILBOX_DATA:
					/* Convert length from byte count to word count. Make sure we round up. */
					word_length =
						(long)(info->DSPInfoBlklen + 1) / 2;
					put_request_value(dev, word_length);
					pMailBoxData =
						(struct drv_msg *)&info->DSPInfoBlk[0];
					pUsData =
						(u16 *)&pMailBoxData->data[0];
					/* Provide mutual exclusive access while reading ASIC registers. */
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
						if (word_length & 0x01)
							word_length++;

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
						(u16 *) ((long)pFileStart +
							 pFileHdr5->
							 version_data_offset);
					/* Provide mutual exclusive access while reading ASIC registers. */
					spin_lock_irqsave(&info->dpram_lock,
							  flags);
					/*
					 * Position ASIC DPRAM auto-increment pointer.
					 */
					outw(DWNLD_MAG_PS_HDR_LOC,
					     dev->base_addr +
					     FT1000_REG_DPRAM_ADDR);
					if (word_length & 0x01)
						word_length++;
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
					spin_unlock_irqrestore(&info->
							       dpram_lock,
							       flags);
					break;

				case REQUEST_CODE_BY_VERSION:
					bGoodVersion = false;
					requested_version =
						get_request_value(dev);
					pDspImageInfoV6 =
						(struct dsp_image_info *) ((long)
									   pFileStart
									   +
									   sizeof
									   (struct dsp_file_hdr));
					for (imageN = 0;
					     imageN <
						     pFileHdr5->nDspImages;
					     imageN++) {
						temp = (u16)
							(pDspImageInfoV6->
							 version);
						templong = temp;
						temp = (u16)
							(pDspImageInfoV6->
							 version >> 16);
						templong |=
							(temp << 16);
						if (templong ==
						    requested_version) {
							bGoodVersion =
								true;
							pUsFile =
								(u16
								 *) ((long)
								     pFileStart
								     +
								     pDspImageInfoV6->
								     begin_offset);
							pUcFile =
								(u8
								 *) ((long)
								     pFileStart
								     +
								     pDspImageInfoV6->
								     begin_offset);
							pCodeEnd =
								(u8
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
								(u32)
								pDspImageInfoV6->
								checksum;
							netdev_dbg(dev,
								   "ft1000_dnld: image_chksum = 0x%8x\n",
								   (unsigned
								    int)
								   image_chksum);
							break;
						}
						pDspImageInfoV6++;
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
			if (((unsigned long)(pUcFile) - (unsigned long) pFileStart) >=
			    (unsigned long)FileLength) {
				uiState = STATE_DONE_FILE;
				break;
			}

			pHdr = (struct pseudo_hdr *)pUsFile;

			if (pHdr->portdest == 0x80	/* DspOAM */
			    && (pHdr->portsrc == 0x00	/* Driver */
				|| pHdr->portsrc == 0x10 /* FMM */)) {
				uiState = STATE_SECTION_PROV;
			} else {
				netdev_dbg(dev,
					   "Download error: Bad Port IDs in Pseudo Record\n");
				netdev_dbg(dev, "\t Port Source = 0x%2.2x\n",
					   pHdr->portsrc);
				netdev_dbg(dev, "\t Port Destination = 0x%2.2x\n",
					   pHdr->portdest);
				Status = FAILURE;
			}

			break;

		case STATE_SECTION_PROV:

			pHdr = (struct pseudo_hdr *)pUcFile;

			if (pHdr->checksum == hdr_checksum(pHdr)) {
				if (pHdr->portdest != 0x80 /* Dsp OAM */) {
					uiState = STATE_DONE_PROV;
					break;
				}
				usHdrLength = ntohs(pHdr->length);	/* Byte length for PROV records */

				/* Get buffer for provisioning data */
				pbuffer =
					kmalloc(usHdrLength + sizeof(struct pseudo_hdr),
						GFP_ATOMIC);
				if (pbuffer) {
					memcpy(pbuffer, pUcFile,
					       (u32) (usHdrLength +
						      sizeof(struct pseudo_hdr)));
					/* link provisioning data */
					pprov_record =
						kmalloc(sizeof(struct prov_record),
							GFP_ATOMIC);
					if (pprov_record) {
						pprov_record->pprov_data =
							pbuffer;
						list_add_tail(&pprov_record->
							      list,
							      &info->prov_list);
						/* Move to next entry if available */
						pUcFile =
							(u8 *)((unsigned long) pUcFile +
							       (unsigned long) ((usHdrLength + 1) & 0xFFFFFFFE) + sizeof(struct pseudo_hdr));
						if ((unsigned long) (pUcFile) -
						    (unsigned long) (pFileStart) >=
						    (unsigned long)FileLength) {
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
