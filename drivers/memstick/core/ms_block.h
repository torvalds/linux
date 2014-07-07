/*
 *  ms_block.h - Sony MemoryStick (legacy) storage support

 *  Copyright (C) 2013 Maxim Levitsky <maximlevitsky@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Minor portions of the driver are copied from mspro_block.c which is
 * Copyright (C) 2007 Alex Dubov <oakad@yahoo.com>
 *
 * Also ms structures were copied from old broken driver by same author
 * These probably come from MS spec
 *
 */

#ifndef MS_BLOCK_NEW_H
#define MS_BLOCK_NEW_H

#define MS_BLOCK_MAX_SEGS      32
#define MS_BLOCK_MAX_PAGES     ((2 << 16) - 1)

#define MS_BLOCK_MAX_BOOT_ADDR 0x000c
#define MS_BLOCK_BOOT_ID       0x0001
#define MS_BLOCK_INVALID       0xffff
#define MS_MAX_ZONES           16
#define MS_BLOCKS_IN_ZONE      512

#define MS_BLOCK_MAP_LINE_SZ   16
#define MS_BLOCK_PART_SHIFT    3


#define MEMSTICK_UNCORR_ERROR (MEMSTICK_STATUS1_UCFG | \
		MEMSTICK_STATUS1_UCEX | MEMSTICK_STATUS1_UCDT)

#define MEMSTICK_CORR_ERROR (MEMSTICK_STATUS1_FGER | MEMSTICK_STATUS1_EXER | \
	MEMSTICK_STATUS1_DTER)

#define MEMSTICK_INT_ERROR (MEMSTICK_INT_CMDNAK | MEMSTICK_INT_ERR)

#define MEMSTICK_OVERWRITE_FLAG_NORMAL \
	(MEMSTICK_OVERWRITE_PGST1 | \
	MEMSTICK_OVERWRITE_PGST0  | \
	MEMSTICK_OVERWRITE_BKST)

#define MEMSTICK_OV_PG_NORMAL \
	(MEMSTICK_OVERWRITE_PGST1 | MEMSTICK_OVERWRITE_PGST0)

#define MEMSTICK_MANAGMENT_FLAG_NORMAL \
	(MEMSTICK_MANAGEMENT_SYSFLG |  \
	MEMSTICK_MANAGEMENT_SCMS1   |  \
	MEMSTICK_MANAGEMENT_SCMS0)     \

struct ms_boot_header {
	unsigned short block_id;
	unsigned short format_reserved;
	unsigned char  reserved0[184];
	unsigned char  data_entry;
	unsigned char  reserved1[179];
} __packed;


struct ms_system_item {
	unsigned int  start_addr;
	unsigned int  data_size;
	unsigned char data_type_id;
	unsigned char reserved[3];
} __packed;

struct ms_system_entry {
	struct ms_system_item disabled_block;
	struct ms_system_item cis_idi;
	unsigned char         reserved[24];
} __packed;

struct ms_boot_attr_info {
	unsigned char      memorystick_class;
	unsigned char      format_unique_value1;
	unsigned short     block_size;
	unsigned short     number_of_blocks;
	unsigned short     number_of_effective_blocks;
	unsigned short     page_size;
	unsigned char      extra_data_size;
	unsigned char      format_unique_value2;
	unsigned char      assembly_time[8];
	unsigned char      format_unique_value3;
	unsigned char      serial_number[3];
	unsigned char      assembly_manufacturer_code;
	unsigned char      assembly_model_code[3];
	unsigned short     memory_manufacturer_code;
	unsigned short     memory_device_code;
	unsigned short     implemented_capacity;
	unsigned char      format_unique_value4[2];
	unsigned char      vcc;
	unsigned char      vpp;
	unsigned short     controller_number;
	unsigned short     controller_function;
	unsigned char      reserved0[9];
	unsigned char      transfer_supporting;
	unsigned short     format_unique_value5;
	unsigned char      format_type;
	unsigned char      memorystick_application;
	unsigned char      device_type;
	unsigned char      reserved1[22];
	unsigned char      format_uniqure_value6[2];
	unsigned char      reserved2[15];
} __packed;

struct ms_cis_idi {
	unsigned short general_config;
	unsigned short logical_cylinders;
	unsigned short reserved0;
	unsigned short logical_heads;
	unsigned short track_size;
	unsigned short page_size;
	unsigned short pages_per_track;
	unsigned short msw;
	unsigned short lsw;
	unsigned short reserved1;
	unsigned char  serial_number[20];
	unsigned short buffer_type;
	unsigned short buffer_size_increments;
	unsigned short long_command_ecc;
	unsigned char  firmware_version[28];
	unsigned char  model_name[18];
	unsigned short reserved2[5];
	unsigned short pio_mode_number;
	unsigned short dma_mode_number;
	unsigned short field_validity;
	unsigned short current_logical_cylinders;
	unsigned short current_logical_heads;
	unsigned short current_pages_per_track;
	unsigned int   current_page_capacity;
	unsigned short mutiple_page_setting;
	unsigned int   addressable_pages;
	unsigned short single_word_dma;
	unsigned short multi_word_dma;
	unsigned char  reserved3[128];
} __packed;


struct ms_boot_page {
	struct ms_boot_header    header;
	struct ms_system_entry   entry;
	struct ms_boot_attr_info attr;
} __packed;

struct msb_data {
	unsigned int			usage_count;
	struct memstick_dev		*card;
	struct gendisk			*disk;
	struct request_queue		*queue;
	spinlock_t			q_lock;
	struct hd_geometry		geometry;
	struct attribute_group		attr_group;
	struct request			*req;
	int				caps;
	int				disk_id;

	/* IO */
	struct workqueue_struct		*io_queue;
	bool				io_queue_stopped;
	struct work_struct		io_work;
	bool				card_dead;

	/* Media properties */
	struct ms_boot_page		*boot_page;
	u16				boot_block_locations[2];
	int				boot_block_count;

	bool				read_only;
	unsigned short			page_size;
	int				block_size;
	int				pages_in_block;
	int				zone_count;
	int				block_count;
	int				logical_block_count;

	/* FTL tables */
	unsigned long			*used_blocks_bitmap;
	unsigned long			*erased_blocks_bitmap;
	u16				*lba_to_pba_table;
	int				free_block_count[MS_MAX_ZONES];
	bool				ftl_initialized;

	/* Cache */
	unsigned char			*cache;
	unsigned long			valid_cache_bitmap;
	int				cache_block_lba;
	bool				need_flush_cache;
	struct timer_list		cache_flush_timer;

	/* Preallocated buffers */
	unsigned char			*block_buffer;
	struct scatterlist		prealloc_sg[MS_BLOCK_MAX_SEGS+1];


	/* handler's local data */
	struct ms_register_addr		reg_addr;
	bool				addr_valid;

	u8				command_value;
	bool				command_need_oob;
	struct scatterlist		*current_sg;
	int				current_sg_offset;

	struct ms_register		regs;
	int				current_page;

	int				state;
	int				exit_error;
	bool				int_polling;
	unsigned long			int_timeout;

};

enum msb_readpage_states {
	MSB_RP_SEND_BLOCK_ADDRESS = 0,
	MSB_RP_SEND_READ_COMMAND,

	MSB_RP_SEND_INT_REQ,
	MSB_RP_RECEIVE_INT_REQ_RESULT,

	MSB_RP_SEND_READ_STATUS_REG,
	MSB_RP_RECEIVE_STATUS_REG,

	MSB_RP_SEND_OOB_READ,
	MSB_RP_RECEIVE_OOB_READ,

	MSB_RP_SEND_READ_DATA,
	MSB_RP_RECEIVE_READ_DATA,
};

enum msb_write_block_states {
	MSB_WB_SEND_WRITE_PARAMS = 0,
	MSB_WB_SEND_WRITE_OOB,
	MSB_WB_SEND_WRITE_COMMAND,

	MSB_WB_SEND_INT_REQ,
	MSB_WB_RECEIVE_INT_REQ,

	MSB_WB_SEND_WRITE_DATA,
	MSB_WB_RECEIVE_WRITE_CONFIRMATION,
};

enum msb_send_command_states {
	MSB_SC_SEND_WRITE_PARAMS,
	MSB_SC_SEND_WRITE_OOB,
	MSB_SC_SEND_COMMAND,

	MSB_SC_SEND_INT_REQ,
	MSB_SC_RECEIVE_INT_REQ,

};

enum msb_reset_states {
	MSB_RS_SEND,
	MSB_RS_CONFIRM,
};

enum msb_par_switch_states {
	MSB_PS_SEND_SWITCH_COMMAND,
	MSB_PS_SWICH_HOST,
	MSB_PS_CONFIRM,
};

struct chs_entry {
	unsigned long size;
	unsigned char sec;
	unsigned short cyl;
	unsigned char head;
};

static int msb_reset(struct msb_data *msb, bool full);

static int h_msb_default_bad(struct memstick_dev *card,
						struct memstick_request **mrq);

#define __dbg(level, format, ...) \
	do { \
		if (debug >= level) \
			pr_err(format "\n", ## __VA_ARGS__); \
	} while (0)


#define dbg(format, ...)		__dbg(1, format, ## __VA_ARGS__)
#define dbg_verbose(format, ...)	__dbg(2, format, ## __VA_ARGS__)

#endif
