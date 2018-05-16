// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include "flash_com.h"

struct nand_phy_info	g_nand_phy_info;
struct nand_ops		g_nand_ops;

static u32 check_buf[MAX_FLASH_PAGE_SIZE / 4];
static u32 check_spare_buf[MAX_FLASH_PAGE_SIZE / 8 / 4];
static u32 pg_buf0[MAX_FLASH_PAGE_SIZE / 4];

static u32 l2p_addr_tran(struct nand_req *req, u32 *addr, u32 *p_die)
{
	u16 block_index, page_index;
	u16 blk_per_die = g_nand_phy_info.blk_per_die;
	u32 die_index;

	block_index = (u16)((req[0].page_addr >> BLOCK_ADDR_BITS) &
			    BLOCK_ADDR_MASK);
	page_index = (u16)(req[0].page_addr & PAGE_ADDR_MASK);
	die_index = (u16)((req[0].page_addr >> DIE_ADDR_BITS) &
			  DIE_ADDR_MASK);
	*p_die = die_index;
	*addr = (block_index + blk_per_die * die_index) *
		g_nand_phy_info.page_per_blk + page_index;
	return 0;
}

s32 ftl_flash_prog_pages(void *request, u32 num_req, u32 flash_type, u32 check)
{
	u32 i, cs, status, addr;
	struct nand_req *req = (struct nand_req *)request;

	for (i = 0; i < num_req; i++) {
		l2p_addr_tran(&req[i], &addr, &cs);
		status = g_nand_ops.prog_page(cs,
					      addr,
					      req[i].p_data,
					      req[i].p_spare);
		req[i].status = status;
		if (status != NAND_STS_OK)
			req[i].status = NAND_STS_ECC_ERR;
	}

	if (check == 0)
		return 0;
	for (i = 0; i < num_req; i++) {
		l2p_addr_tran(&req[i], &addr, &cs);
		status = g_nand_ops.read_page(cs,
					      addr,
					      check_buf,
					      check_spare_buf);
		if (status != NAND_STS_ECC_ERR)
			req[i].status = NAND_STS_OK;
		if (check_buf[0] != req[i].p_data[0])
			req[i].status = NAND_STS_ECC_ERR;
	}
	return 0;
}

s32 ftl_flash_read_pages(void *request, u32 num_req, u32 flash_type)
{
	u32 i, cs, status, addr;
	struct nand_req *req = (struct nand_req *)request;

	for (i = 0; i < num_req; i++) {
		l2p_addr_tran(&req[i], &addr, &cs);
		status = g_nand_ops.read_page(cs,
					      addr,
					      req[i].p_data,
					      req[i].p_spare);
		req[i].status = status;
	}
	return OK;
}

s32 ftl_flash_erase_blocks(void *request, u32 num_req)
{
	u32 i, cs, status, addr;
	struct nand_req *req = (struct nand_req *)request;

	for (i = 0; i < num_req; i++) {
		l2p_addr_tran(&req[i], &addr, &cs);
		status = g_nand_ops.erase_blk(cs, addr);
		req[i].status = status;
		if (status != NAND_STS_OK)
			req[i].status = NAND_STS_ECC_ERR;
	}
	return OK;
}

s32 ftl_flash_get_bad_blk_list(u16 *table, u32 die)
{
	return g_nand_ops.get_bad_blk_list(table, die);
}

s32 ftl_flash_test_blk(u16 phy_block)
{
	s32 sts = 0;
	u32 spare[16];
	struct nand_req req;

	req.p_data = pg_buf0;
	req.p_spare = spare;
	memset(spare, 0xA5, 32);
	memset(pg_buf0, 0x5A, 8);
	req.page_addr = phy_block << BLOCK_ADDR_BITS;
	ftl_flash_erase_blocks((void *)&req, 1);
	ftl_flash_prog_pages((void *)&req, 1, SLC, 1);
	if (req.status == NAND_STS_ECC_ERR) {
		PRINT_E("%s %x is bad block\n", __func__, phy_block);
		sts = -1;
	}
	ftl_flash_erase_blocks((void *)&req, 1);

	return sts;
}
