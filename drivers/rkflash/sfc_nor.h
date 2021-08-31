/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef _SFC_NOR_H
#define _SFC_NOR_H

#include "sfc.h"

#define NOR_PAGE_SIZE		256
#define NOR_BLOCK_SIZE		(64 * 1024)
#define NOR_SECS_BLK		(NOR_BLOCK_SIZE / 512)
#define NOR_SECS_PAGE		8

#define FEA_READ_STATUE_MASK	(0x3 << 0)
#define FEA_STATUE_MODE1	0
#define FEA_STATUE_MODE2	1
#define FEA_4BIT_READ		BIT(2)
#define FEA_4BIT_PROG		BIT(3)
#define FEA_4BYTE_ADDR		BIT(4)
#define FEA_4BYTE_ADDR_MODE	BIT(5)

/*Command Set*/
#define CMD_READ_JEDECID        (0x9F)
#define CMD_READ_DATA           (0x03)
#define CMD_READ_STATUS         (0x05)
#define CMD_WRITE_STATUS        (0x01)
#define CMD_PAGE_PROG           (0x02)
#define CMD_SECTOR_ERASE        (0x20)
#define CMD_BLK64K_ERASE        (0xD8)
#define CMD_BLK32K_ERASE        (0x52)
#define CMD_CHIP_ERASE          (0xC7)
#define CMD_WRITE_EN            (0x06)
#define CMD_WRITE_DIS           (0x04)
#define CMD_PAGE_READ           (0x13)
#define CMD_PAGE_FASTREAD4B     (0x0C)
#define CMD_GET_FEATURE         (0x0F)
#define CMD_SET_FEATURE         (0x1F)
#define CMD_PROG_LOAD           (0x02)
#define CMD_PROG_EXEC           (0x10)
#define CMD_BLOCK_ERASE         (0xD8)
#define CMD_READ_DATA_X2        (0x3B)
#define CMD_READ_DATA_X4        (0x6B)
#define CMD_PROG_LOAD_X4        (0x32)
#define CMD_READ_STATUS2        (0x35)
#define CMD_READ_STATUS3        (0x15)
#define CMD_WRITE_STATUS2       (0x31)
#define CMD_WRITE_STATUS3       (0x11)
/* X1 cmd, X1 addr, X1 data */
#define CMD_FAST_READ_X1        (0x0B)
/* X1 cmd, X1 addr, X2 data */
#define CMD_FAST_READ_X2        (0x3B)
/* X1 cmd, X1 addr, X4 data SUPPORT GD MARCONIX WINBOND */
#define CMD_FAST_READ_X4        (0x6B)
/* X1 cmd, X1 addr, X4 data SUPPORT GD MARCONIX WINBOND */
#define CMD_FAST_4READ_X4       (0x6C)
/* X1 cmd, X4 addr, X4 data SUPPORT EON GD MARCONIX WINBOND */
#define CMD_FAST_READ_A4        (0xEB)
/* X1 cmd, X1 addr, X4 data, SUPPORT GD WINBOND */
#define CMD_PAGE_PROG_X4        (0x32)
/* X1 cmd, X4 addr, X4 data, SUPPORT MARCONIX */
#define CMD_PAGE_PROG_A4        (0x38)
/* X1 cmd, X4 addr, X4 data, SUPPORT MARCONIX */
#define CMD_PAGE_PROG_4PP       (0x3E)
#define CMD_RESET_NAND          (0xFF)
#define CMD_ENTER_4BYTE_MODE    (0xB7)
#define CMD_EXIT_4BYTE_MODE     (0xE9)
#define CMD_ENABLE_RESER	(0x66)
#define CMD_RESET_DEVICE	(0x99)
#define CMD_READ_PARAMETER	(0x5A)

enum NOR_ERASE_TYPE {
	ERASE_SECTOR = 0,
	ERASE_BLOCK64K,
	ERASE_CHIP
};

enum SNOR_IO_MODE {
	IO_MODE_SPI = 0,
	IO_MODE_QPI
};

enum SNOR_READ_MODE {
	READ_MODE_NOMAL = 0,
	READ_MODE_FAST
};

enum SNOR_ADDR_MODE {
	ADDR_MODE_3BYTE = 0,
	ADDR_MODE_4BYTE
};

typedef int (*SNOR_WRITE_STATUS)(u32 reg_index, u8 status);

struct SFNOR_DEV {
	u32	capacity;
	u8	manufacturer;
	u8	mem_type;
	u16	page_size;
	u32	blk_size;

	u8	read_cmd;
	u8	prog_cmd;
	u8	sec_erase_cmd;
	u8	blk_erase_cmd;
	u8	QE_bits;

	enum SNOR_READ_MODE  read_mode;
	enum SNOR_ADDR_MODE  addr_mode;
	enum SNOR_IO_MODE    io_mode;

	enum SFC_DATA_LINES read_lines;
	enum SFC_DATA_LINES prog_lines;
	enum SFC_DATA_LINES prog_addr_lines;

	SNOR_WRITE_STATUS write_status;
	u32 max_iosize;
};

struct flash_info {
	u32 id;

	u8 block_size;
	u8 sector_size;
	u8 read_cmd;
	u8 prog_cmd;

	u8 read_cmd_4;
	u8 prog_cmd_4;
	u8 sector_erase_cmd;
	u8 block_erase_cmd;

	u8 feature;
	u8 density;  /* (1 << density) sectors*/
	u8 QE_bits;
	u8 reserved2;
};

/* flash table packet for easy boot */
#define SNOR_INFO_PACKET_ID	0x464E494E
#define SNOR_INFO_PACKET_HEAD_LEN	14

#define SNOR_INFO_PACKET_SPI_MODE_RATE_SHIFT	25

struct snor_info_packet {
	u32 id;
	u32 head_hash; /*hash for head, check by bootrom.*/
	u16 head_len;  /*320 - 16 bytes*/
	u16 version;
	u8 read_cmd;
	u8 prog_cmd;
	u8 read_cmd_4;
	u8 prog_cmd_4;

	u8 sector_erase_cmd;
	u8 block_erase_cmd;
	u8 feature;
	u8 QE_bits;

	u32 spi_mode;
};

int snor_init(struct SFNOR_DEV *p_dev);
u32 snor_get_capacity(struct SFNOR_DEV *p_dev);
int snor_read(struct SFNOR_DEV *p_dev, u32 sec, u32 n_sec, void *p_data);
int snor_write(struct SFNOR_DEV *p_dev, u32 sec, u32 n_sec, void *p_data);
int snor_erase(struct SFNOR_DEV *p_dev,
	       u32 addr,
	       enum NOR_ERASE_TYPE erase_type);
int snor_read_id(u8 *data);
int snor_prog_page(struct SFNOR_DEV *p_dev, u32 addr, void *p_data, u32 size);
int snor_read_data(struct SFNOR_DEV *p_dev, u32 addr, void *p_data, u32 size);
int snor_reset_device(void);
int snor_disable_QE(struct SFNOR_DEV *p_dev);
int snor_reinit_from_table_packet(struct SFNOR_DEV *p_dev,
				  struct snor_info_packet *packet);
#endif
