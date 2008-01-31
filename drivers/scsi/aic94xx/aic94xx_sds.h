/*
 * Aic94xx SAS/SATA driver hardware interface header file.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Gilbert Wu <gilbert_wu@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This file is part of the aic94xx driver.
 *
 * The aic94xx driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * The aic94xx driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the aic94xx driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef _AIC94XX_SDS_H_
#define _AIC94XX_SDS_H_

enum {
	FLASH_METHOD_UNKNOWN,
	FLASH_METHOD_A,
	FLASH_METHOD_B
};

#define FLASH_MANUF_ID_AMD              0x01
#define FLASH_MANUF_ID_ST               0x20
#define FLASH_MANUF_ID_FUJITSU          0x04
#define FLASH_MANUF_ID_MACRONIX         0xC2
#define FLASH_MANUF_ID_INTEL            0x89
#define FLASH_MANUF_ID_UNKNOWN          0xFF

#define FLASH_DEV_ID_AM29LV008BT        0x3E
#define FLASH_DEV_ID_AM29LV800DT        0xDA
#define FLASH_DEV_ID_STM29W800DT        0xD7
#define FLASH_DEV_ID_STM29LV640         0xDE
#define FLASH_DEV_ID_STM29008           0xEA
#define FLASH_DEV_ID_MBM29LV800TE       0xDA
#define FLASH_DEV_ID_MBM29DL800TA       0x4A
#define FLASH_DEV_ID_MBM29LV008TA       0x3E
#define FLASH_DEV_ID_AM29LV640MT        0x7E
#define FLASH_DEV_ID_AM29F800B          0xD6
#define FLASH_DEV_ID_MX29LV800BT        0xDA
#define FLASH_DEV_ID_MX29LV008CT        0xDA
#define FLASH_DEV_ID_I28LV00TAT         0x3E
#define FLASH_DEV_ID_UNKNOWN            0xFF

/* status bit mask values */
#define FLASH_STATUS_BIT_MASK_DQ6       0x40
#define FLASH_STATUS_BIT_MASK_DQ5       0x20
#define FLASH_STATUS_BIT_MASK_DQ2       0x04

/* minimum value in micro seconds needed for checking status */
#define FLASH_STATUS_ERASE_DELAY_COUNT  50
#define FLASH_STATUS_WRITE_DELAY_COUNT  25

#define FLASH_SECTOR_SIZE               0x010000
#define FLASH_SECTOR_SIZE_MASK          0xffff0000

#define FLASH_OK                        0x000000
#define FAIL_OPEN_BIOS_FILE             0x000100
#define FAIL_CHECK_PCI_ID               0x000200
#define FAIL_CHECK_SUM                  0x000300
#define FAIL_UNKNOWN                    0x000400
#define FAIL_VERIFY                     0x000500
#define FAIL_RESET_FLASH                0x000600
#define FAIL_FIND_FLASH_ID              0x000700
#define FAIL_ERASE_FLASH                0x000800
#define FAIL_WRITE_FLASH                0x000900
#define FAIL_FILE_SIZE                  0x000a00
#define FAIL_PARAMETERS                 0x000b00
#define FAIL_OUT_MEMORY                 0x000c00
#define FLASH_IN_PROGRESS               0x001000

struct controller_id {
	u32 vendor;     /* PCI Vendor ID */
	u32 device;     /* PCI Device ID */
	u32 sub_vendor; /* PCI Subvendor ID */
	u32 sub_device; /* PCI Subdevice ID */
};

struct image_info {
	u32 ImageId;       /* Identifies the image */
	u32 ImageOffset;   /* Offset the beginning of the file */
	u32 ImageLength;   /* length of the image */
	u32 ImageChecksum; /* Image checksum */
	u32 ImageVersion;  /* Version of the image, could be build number */
};

struct bios_file_header {
	u8 signature[32]; /* Signature/Cookie to identify the file */
	u32 checksum;	  /*Entire file checksum with this field zero */
	u32 antidote;	  /* Entire file checksum with this field 0xFFFFFFFF */
	struct controller_id contrl_id; /*PCI id to identify the controller */
	u32 filelen;      /*Length of the entire file*/
	u32 chunk_num;	  /*The chunk/part number for multiple Image files */
	u32 total_chunks; /*Total number of chunks/parts in the image file */
	u32 num_images;   /* Number of images in the file */
	u32 build_num;    /* Build number of this image */
	struct image_info image_header;
};

int asd_verify_flash_seg(struct asd_ha_struct *asd_ha,
		void *src, u32 dest_offset, u32 bytes_to_verify);
int asd_write_flash_seg(struct asd_ha_struct *asd_ha,
		void *src, u32 dest_offset, u32 bytes_to_write);
int asd_chk_write_status(struct asd_ha_struct *asd_ha,
		u32 sector_addr, u8 erase_flag);
int asd_check_flash_type(struct asd_ha_struct *asd_ha);
int asd_erase_nv_sector(struct asd_ha_struct *asd_ha,
		u32 flash_addr, u32 size);
#endif
