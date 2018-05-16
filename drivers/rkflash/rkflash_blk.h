/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RKFLASH_BLK_H
#define __RKFLASH_BLK_H

#include <linux/semaphore.h>

#define MAX_PART_COUNT 32
#define RK_PARTITION_TAG	0x50464B52

enum flash_con_type {
	FLASH_CON_TYPE_NANDC = 0,
	FLASH_CON_TYPE_SFC,
	FLASH_CON_TYPE_MAX,
};

enum flash_type {
	FLASH_TYPE_NANDC_NAND = 0,
	FLASH_TYPE_SFC_NOR,
	FLASH_TYPE_SFC_NAND,
	FLASH_TYPE_MAX,
};

struct flash_boot_ops {
	int id;
	int (*init)(void __iomem *reg_addr);
	int (*read)(u32 sec, u32 n_sec, void *p_data);
	int (*write)(u32 sec, u32 n_sec, void *p_data);
	u32 (*get_capacity)(void);
	void (*deinit)(void);
	int (*resume)(void __iomem *reg_addr);
	int (*vendor_read)(u32 sec, u32 n_sec, void *p_data);
	int (*vendor_write)(u32 sec, u32 n_sec, void *p_data);
};

struct flash_part {
	unsigned char name[32];
	unsigned int offset;
	unsigned int size;
	unsigned char type;
};

struct flash_blk_ops {
	char *name;
	int major;
	int minorbits;
	int last_dev_index;
	struct completion thread_exit;
	int quit;
	int flash_th_quited;
	wait_queue_head_t thread_wq; /* thread wait queue */
	struct request_queue *rq;
	spinlock_t queue_lock; /* queue lock */
	struct list_head devs;
	struct module *owner;
};

struct flash_blk_dev {
	struct flash_blk_ops *blk_ops;
	struct list_head list;
	int devnum;
	unsigned int size;
	unsigned int off_size;
	int readonly;
	int writeonly;
	int disable_access;
	void *blkcore_priv;
};

enum ENUM_PARTITION_TYPE {
	PART_VENDOR = 1 << 0,
	PART_IDBLOCK = 1 << 1,
	PART_KERNEL = 1 << 2,
	PART_BOOT = 1 << 3,
	PART_USER = 1 << 31
};

struct STRUCT_DATETIME {
	unsigned short	year;
	unsigned char	month;
	unsigned char	day;
	unsigned char	hour;
	unsigned char	min;
	unsigned char	sec;
	unsigned char	reserve;
};

struct STRUCT_FW_HEADER {
	unsigned int	ui_fw_tag;	/* "RKFP" */
	struct STRUCT_DATETIME	dt_release_data_time;
	unsigned int	ui_fw_ver;
	unsigned int	ui_size;	/* size of sturct,unit of u8 */
	unsigned int	ui_part_entry_offset;	/* unit of sector */
	unsigned int	ui_backup_part_entry_offset;
	unsigned int	ui_part_entry_size;	/* unit of u8 */
	unsigned int	ui_part_entry_count;
	unsigned int	ui_fw_size;	/* unit of u8 */
	unsigned char	reserved[464];
	unsigned int	ui_part_entry_crc;
	unsigned int	ui_header_crc;
};

struct STRUCT_PART_ENTRY {
	unsigned char	sz_name[32];
	enum ENUM_PARTITION_TYPE em_part_type;
	unsigned int	ui_pt_off;	/* unit of sector */
	unsigned int	ui_pt_sz;	/* unit of sector */
	unsigned int	ui_data_length;	/* unit of u8 */
	unsigned int	ui_part_property;
	unsigned char	reserved[76];
};

struct STRUCT_PART_INFO {
	struct STRUCT_FW_HEADER hdr;	/* 0.5KB */
	struct STRUCT_PART_ENTRY part[12];	/* 1.5KB */
} __packed;

int rkflash_dev_suspend(void);
int rkflash_dev_resume(void __iomem *reg_addr);
void rkflash_dev_shutdown(void);
void rkflash_dev_flush(void);
int rkflash_dev_init(void __iomem *reg_addr, enum flash_con_type type);
int rkflash_dev_exit(void);
#endif
