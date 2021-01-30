/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __SFC_NAND_H
#define __SFC_NAND_H

#include "flash_com.h"
#include "sfc.h"

#define SFC_NAND_WAIT_TIME_OUT		3
#define SFC_NAND_PROG_ERASE_ERROR	2
#define SFC_NAND_HW_ERROR		1
#define SFC_NAND_ECC_ERROR		NAND_ERROR
#define SFC_NAND_ECC_REFRESH		NAND_STS_REFRESH
#define SFC_NAND_ECC_OK			NAND_STS_OK

#define SFC_NAND_PAGE_MAX_SIZE		4224
#define SFC_NAND_SECTOR_FULL_SIZE	528
#define SFC_NAND_SECTOR_SIZE		512

#define FEA_READ_STATUE_MASK    (0x3 << 0)
#define FEA_STATUE_MODE1        0
#define FEA_STATUE_MODE2        1
#define FEA_4BIT_READ           BIT(2)
#define FEA_4BIT_PROG           BIT(3)
#define FEA_4BYTE_ADDR          BIT(4)
#define FEA_4BYTE_ADDR_MODE	BIT(5)
#define FEA_SOFT_QOP_BIT	BIT(6)

/* Command Set */
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
#define CMD_FAST_READ_X1        (0x0B)  /* X1 cmd, X1 addr, X1 data */
#define CMD_FAST_READ_X2        (0x3B)  /* X1 cmd, X1 addr, X2 data */
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
#define CMD_RESET_NAND          (0xFF)

#define CMD_ENTER_4BYTE_MODE    (0xB7)
#define CMD_EXIT_4BYTE_MODE     (0xE9)
#define CMD_ENABLE_RESER	(0x66)
#define CMD_RESET_DEVICE	(0x99)

struct SFNAND_DEV {
	u32 capacity;
	u32 block_size;
	u16 page_size;
	u8 manufacturer;
	u8 mem_type;
	u8 read_lines;
	u8 prog_lines;
	u8 page_read_cmd;
	u8 page_prog_cmd;
	u8 *recheck_buffer;
};

struct nand_mega_area {
	u8 off0;
	u8 off1;
	u8 off2;
	u8 off3;
};

struct nand_info {
	u8 id0;
	u8 id1;
	u8 id2;

	u16 sec_per_page;
	u16 page_per_blk;
	u16 plane_per_die;
	u16 blk_per_plane;

	u8 feature;

	u8 density;  /* (1 << density) sectors*/
	u8 max_ecc_bits;
	u8 has_qe_bits;

	struct nand_mega_area meta;
	u32 (*ecc_status)(void);
};

extern struct nand_phy_info	g_nand_phy_info;
extern struct nand_ops		g_nand_ops;

u32 sfc_nand_init(void);
void sfc_nand_deinit(void);
int sfc_nand_read_id(u8 *buf);
u32 sfc_nand_erase_block(u8 cs, u32 addr);
u32 sfc_nand_prog_page(u8 cs, u32 addr, u32 *p_data, u32 *p_spare);
u32 sfc_nand_read_page(u8 cs, u32 addr, u32 *p_data, u32 *p_spare);
u32 sfc_nand_prog_page_raw(u8 cs, u32 addr, u32 *p_page_buf);
u32 sfc_nand_read_page_raw(u8 cs, u32 addr, u32 *p_page_buf);
u32 sfc_nand_check_bad_block(u8 cs, u32 addr);
u32 sfc_nand_mark_bad_block(u8 cs, u32 addr);
void sfc_nand_ftl_ops_init(void);
struct SFNAND_DEV *sfc_nand_get_private_dev(void);
struct nand_info *sfc_nand_get_nand_info(void);
u32 sfc_nand_read(u32 row, u32 *p_page_buf, u32 column, u32 len);

#endif
