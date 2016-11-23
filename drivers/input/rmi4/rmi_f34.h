/*
 * Copyright (c) 2007-2016, Synaptics Incorporated
 * Copyright (C) 2016 Zodiac Inflight Innovations
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _RMI_F34_H
#define _RMI_F34_H

/* F34 image file offsets. */
#define F34_FW_IMAGE_OFFSET	0x100

/* F34 register offsets. */
#define F34_BLOCK_DATA_OFFSET	2

/* F34 commands */
#define F34_WRITE_FW_BLOCK	0x2
#define F34_ERASE_ALL		0x3
#define F34_READ_CONFIG_BLOCK	0x5
#define F34_WRITE_CONFIG_BLOCK	0x6
#define F34_ERASE_CONFIG	0x7
#define F34_ENABLE_FLASH_PROG	0xf

#define F34_STATUS_IN_PROGRESS	0xff
#define F34_STATUS_IDLE		0x80

#define F34_IDLE_WAIT_MS	500
#define F34_ENABLE_WAIT_MS	300
#define F34_ERASE_WAIT_MS	5000

#define F34_BOOTLOADER_ID_LEN	2

struct rmi_f34_firmware {
	__le32 checksum;
	u8 pad1[3];
	u8 bootloader_version;
	__le32 image_size;
	__le32 config_size;
	u8 product_id[10];
	u8 product_info[2];
	u8 pad2[228];
	u8 data[];
};

struct f34v5_data {
	u16 block_size;
	u16 fw_blocks;
	u16 config_blocks;
	u16 ctrl_address;
	u8 status;

	struct completion cmd_done;
	struct mutex flash_mutex;
};

struct f34_data {
	struct rmi_function *fn;

	unsigned char bootloader_id[5];
	unsigned char configuration_id[9];

	struct f34v5_data v5;
};

#endif /* _RMI_F34_H */
