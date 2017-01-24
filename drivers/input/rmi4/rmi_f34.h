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

/* F34 V7 defines */
#define V7_FLASH_STATUS_OFFSET		0
#define V7_PARTITION_ID_OFFSET		1
#define V7_BLOCK_NUMBER_OFFSET		2
#define V7_TRANSFER_LENGTH_OFFSET	3
#define V7_COMMAND_OFFSET		4
#define V7_PAYLOAD_OFFSET		5
#define V7_BOOTLOADER_ID_OFFSET		1

#define IMAGE_HEADER_VERSION_10		0x10

#define CONFIG_ID_SIZE			32
#define PRODUCT_ID_SIZE			10

#define ENABLE_WAIT_MS			(1 * 1000)
#define WRITE_WAIT_MS			(3 * 1000)

#define MIN_SLEEP_TIME_US		50
#define MAX_SLEEP_TIME_US		100

#define HAS_BSR				BIT(5)
#define HAS_CONFIG_ID			BIT(3)
#define HAS_GUEST_CODE			BIT(6)
#define HAS_DISP_CFG			BIT(5)

/* F34 V7 commands */
#define CMD_V7_IDLE			0
#define CMD_V7_ENTER_BL			1
#define CMD_V7_READ			2
#define CMD_V7_WRITE			3
#define CMD_V7_ERASE			4
#define CMD_V7_ERASE_AP			5
#define CMD_V7_SENSOR_ID		6

#define v7_CMD_IDLE			0
#define v7_CMD_WRITE_FW			1
#define v7_CMD_WRITE_CONFIG		2
#define v7_CMD_WRITE_LOCKDOWN		3
#define v7_CMD_WRITE_GUEST_CODE		4
#define v7_CMD_READ_CONFIG		5
#define v7_CMD_ERASE_ALL		6
#define v7_CMD_ERASE_UI_FIRMWARE	7
#define v7_CMD_ERASE_UI_CONFIG		8
#define v7_CMD_ERASE_BL_CONFIG		9
#define v7_CMD_ERASE_DISP_CONFIG	10
#define v7_CMD_ERASE_FLASH_CONFIG	11
#define v7_CMD_ERASE_GUEST_CODE		12
#define v7_CMD_ENABLE_FLASH_PROG	13

#define v7_UI_CONFIG_AREA		0
#define v7_PM_CONFIG_AREA		1
#define v7_BL_CONFIG_AREA		2
#define v7_DP_CONFIG_AREA		3
#define v7_FLASH_CONFIG_AREA		4

/* F34 V7 partition IDs */
#define BOOTLOADER_PARTITION		1
#define DEVICE_CONFIG_PARTITION		2
#define FLASH_CONFIG_PARTITION		3
#define MANUFACTURING_BLOCK_PARTITION	4
#define GUEST_SERIALIZATION_PARTITION	5
#define GLOBAL_PARAMETERS_PARTITION	6
#define CORE_CODE_PARTITION		7
#define CORE_CONFIG_PARTITION		8
#define GUEST_CODE_PARTITION		9
#define DISPLAY_CONFIG_PARTITION	10

/* F34 V7 container IDs */
#define TOP_LEVEL_CONTAINER			0
#define UI_CONTAINER				1
#define UI_CONFIG_CONTAINER			2
#define BL_CONTAINER				3
#define BL_IMAGE_CONTAINER			4
#define BL_CONFIG_CONTAINER			5
#define BL_LOCKDOWN_INFO_CONTAINER		6
#define PERMANENT_CONFIG_CONTAINER		7
#define GUEST_CODE_CONTAINER			8
#define BL_PROTOCOL_DESCRIPTOR_CONTAINER	9
#define UI_PROTOCOL_DESCRIPTOR_CONTAINER	10
#define RMI_SELF_DISCOVERY_CONTAINER		11
#define RMI_PAGE_CONTENT_CONTAINER		12
#define GENERAL_INFORMATION_CONTAINER		13
#define DEVICE_CONFIG_CONTAINER			14
#define FLASH_CONFIG_CONTAINER			15
#define GUEST_SERIALIZATION_CONTAINER		16
#define GLOBAL_PARAMETERS_CONTAINER		17
#define CORE_CODE_CONTAINER			18
#define CORE_CONFIG_CONTAINER			19
#define DISPLAY_CONFIG_CONTAINER		20

struct f34v7_query_1_7 {
	u8 bl_minor_revision;			/* query 1 */
	u8 bl_major_revision;
	__le32 bl_fw_id;			/* query 2 */
	u8 minimum_write_size;			/* query 3 */
	__le16 block_size;
	__le16 flash_page_size;
	__le16 adjustable_partition_area_size;	/* query 4 */
	__le16 flash_config_length;		/* query 5 */
	__le16 payload_length;			/* query 6 */
	u8 partition_support[4];		/* query 7 */
} __packed;

struct f34v7_data_1_5 {
	u8 partition_id;
	__le16 block_offset;
	__le16 transfer_length;
	u8 command;
	u8 payload[2];
} __packed;

struct block_data {
	const void *data;
	int size;
};

struct partition_table {
	u8 partition_id;
	u8 byte_1_reserved;
	__le16 partition_length;
	__le16 start_physical_address;
	__le16 partition_properties;
} __packed;

struct physical_address {
	u16 ui_firmware;
	u16 ui_config;
	u16 dp_config;
	u16 guest_code;
};

struct container_descriptor {
	__le32 content_checksum;
	__le16 container_id;
	u8 minor_version;
	u8 major_version;
	u8 reserved_08;
	u8 reserved_09;
	u8 reserved_0a;
	u8 reserved_0b;
	u8 container_option_flags[4];
	__le32 content_options_length;
	__le32 content_options_address;
	__le32 content_length;
	__le32 content_address;
} __packed;

struct block_count {
	u16 ui_firmware;
	u16 ui_config;
	u16 dp_config;
	u16 fl_config;
	u16 pm_config;
	u16 bl_config;
	u16 lockdown;
	u16 guest_code;
};

struct image_header_10 {
	__le32 checksum;
	u8 reserved_04;
	u8 reserved_05;
	u8 minor_header_version;
	u8 major_header_version;
	u8 reserved_08;
	u8 reserved_09;
	u8 reserved_0a;
	u8 reserved_0b;
	__le32 top_level_container_start_addr;
};

struct image_metadata {
	bool contains_firmware_id;
	bool contains_bootloader;
	bool contains_display_cfg;
	bool contains_guest_code;
	bool contains_flash_config;
	unsigned int firmware_id;
	unsigned int checksum;
	unsigned int bootloader_size;
	unsigned int display_cfg_offset;
	unsigned char bl_version;
	unsigned char product_id[PRODUCT_ID_SIZE + 1];
	unsigned char cstmr_product_id[PRODUCT_ID_SIZE + 1];
	struct block_data bootloader;
	struct block_data ui_firmware;
	struct block_data ui_config;
	struct block_data dp_config;
	struct block_data fl_config;
	struct block_data bl_config;
	struct block_data guest_code;
	struct block_data lockdown;
	struct block_count blkcount;
	struct physical_address phyaddr;
};

struct register_offset {
	u8 properties;
	u8 properties_2;
	u8 block_size;
	u8 block_count;
	u8 gc_block_count;
	u8 flash_status;
	u8 partition_id;
	u8 block_number;
	u8 transfer_length;
	u8 flash_cmd;
	u8 payload;
};

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

struct f34v7_data {
	bool has_display_cfg;
	bool has_guest_code;
	bool force_update;
	bool in_bl_mode;
	u8 *read_config_buf;
	size_t read_config_buf_size;
	u8 command;
	u8 flash_status;
	u16 block_size;
	u16 config_block_count;
	u16 config_size;
	u16 config_area;
	u16 flash_config_length;
	u16 payload_length;
	u8 partitions;
	u16 partition_table_bytes;
	bool new_partition_table;

	struct register_offset off;
	struct block_count blkcount;
	struct physical_address phyaddr;
	struct image_metadata img;

	const void *config_data;
	const void *image;
};

struct f34_data {
	struct rmi_function *fn;

	u8 bl_version;
	unsigned char bootloader_id[5];
	unsigned char configuration_id[CONFIG_ID_SIZE*2 + 1];

	union {
		struct f34v5_data v5;
		struct f34v7_data v7;
	};
};

int rmi_f34v7_start_reflash(struct f34_data *f34, const struct firmware *fw);
int rmi_f34v7_do_reflash(struct f34_data *f34, const struct firmware *fw);
int rmi_f34v7_probe(struct f34_data *f34);

#endif /* _RMI_F34_H */
