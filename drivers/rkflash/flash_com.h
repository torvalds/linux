/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __FLASH_COM_H
#define __FLASH_COM_H

#include "typedef.h"

#define NAND_ERROR			INVALID_UINT32
#define NAND_OK				0

#define NAND_STS_OK                     0	/* bit 0 ecc error or ok */
#define NAND_STS_REFRESH                256	/* need refresh */
#define NAND_STS_EMPTY                  512	/* page is not proged */
#define NAND_STS_ECC_ERR                NAND_ERROR

#define NAND_IDB_START    64 /* 32 KB*/
#define NAND_IDB_SIZE    512 /* 256 KB*/
#define NAND_IDB_END    (NAND_IDB_START + NAND_IDB_SIZE - 1)
#define DEFAULT_IDB_RESERVED_BLOCK 16

#define FULL_SLC			0
#define SLC				1

#define NAND_FLASH_MLC_PAGE_TAG         0xFFFF
#define MAX_FLASH_PAGE_SIZE		0x1000 /* 4KB */

#define PAGE_ADDR_BITS			0
#define PAGE_ADDR_MASK			((1u << 11) - 1)
#define BLOCK_ADDR_BITS			11
#define BLOCK_ADDR_MASK			((1u << 14) - 1)
#define DIE_ADDR_BITS			25
#define DIE_ADDR_MASK			((1u << 3) - 1)
#define FLAG_ADDR_BITS			28
#define FLAG_ADDR_MASK			((1u << 4) - 1)
#define PHY_BLK_DIE_ADDR_BITS		14

struct nand_req {
	u32 status;
	u32 page_addr;   /* 31:28 flag, 27:25: die, 24:11 block, 10:0 page */
	u32 *p_data;
	u32 *p_spare;
	u32 lpa;
};

struct nand_phy_info {
	u16	nand_type;		/* SLC,MLC,TLC */
	u16	die_num;		/* number of LUNs */
	u16	plane_per_die;
	u16	blk_per_plane;
	u16	blk_per_die;
	u16	page_per_blk;		/* in MLC mode */
	u16	page_per_slc_blk;	/* in SLC mode */
	u16	sec_per_page;		/* physical page data size */
	u16	sec_per_blk;		/* physical page data size */
	u16	byte_per_sec;		/* size of logical sectors */
	u16	reserved_blk;		/* reserved for boot loader in die 0*/
	u8	ecc_bits;
};

struct nand_ops {
	s32 (*get_bad_blk_list)(u16 *table, u32 die);
	u32 (*erase_blk)(u8 cs, u32 page_addr);
	u32 (*prog_page)(u8 cs, u32 page_addr, u32 *data, u32 *spare);
	u32 (*read_page)(u8 cs, u32 page_addr, u32 *data, u32 *spare);
	void (*bch_sel)(u8 bits);
	void (*set_sec_num)(u8 num);
};

s32 ftl_flash_prog_pages(void *req, u32 num_req, u32 flash_type, u32 check);
s32 ftl_flash_read_pages(void *req, u32 num_req, u32 flash_type);
s32 ftl_flash_erase_blocks(void *req, u32 num_req);
s32 ftl_flash_test_blk(u16 phy_block);
s32 ftl_flash_get_bad_blk_list(u16 *table, u32 die);

#endif
