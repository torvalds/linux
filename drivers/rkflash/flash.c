// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "flash.h"
#include "flash_com.h"
#include "nandc.h"
#include "rkflash_debug.h"

#define FLASH_STRESS_TEST_EN		0

static u8 id_byte[MAX_FLASH_NUM][8];
static u8 die_cs_index[MAX_FLASH_NUM];
static u8 g_nand_max_die;
static u16 g_totle_block;
static u8 g_nand_flash_ecc_bits;
static u8 g_nand_idb_res_blk_num;
static u8 g_nand_ecc_en;

static struct NAND_PARA_INFO_T nand_para = {
	2,
	{0x98, 0xF1, 0, 0, 0, 0},
	TOSHIBA,
	1,
	4,
	64,
	1,
	1,
	1024,
	0x100,
	LSB_0,
	RR_NONE,
	16,
	40,
	1,
	0,
	BBF_1,
	MPM_0,
	{0}
};	/* TC58NVG0S3HTA00 */

void nandc_flash_reset(u8 cs)
{
	nandc_flash_cs(cs);
	nandc_writel(RESET_CMD, NANDC_CHIP_CMD(cs));
	nandc_wait_flash_ready(cs);
	nandc_flash_de_cs(cs);
}

static void flash_read_id_raw(u8 cs, u8 *buf)
{
	u8 *ptr = (u8 *)buf;

	nandc_flash_reset(cs);
	nandc_flash_cs(cs);
	nandc_writel(READ_ID_CMD, NANDC_CHIP_CMD(cs));
	nandc_writel(0x00, NANDC_CHIP_ADDR(cs));
	nandc_delayns(200);

	ptr[0] = nandc_readl(NANDC_CHIP_DATA(cs));
	ptr[1] = nandc_readl(NANDC_CHIP_DATA(cs));
	ptr[2] = nandc_readl(NANDC_CHIP_DATA(cs));
	ptr[3] = nandc_readl(NANDC_CHIP_DATA(cs));
	ptr[4] = nandc_readl(NANDC_CHIP_DATA(cs));
	ptr[5] = nandc_readl(NANDC_CHIP_DATA(cs));
	ptr[6] = nandc_readl(NANDC_CHIP_DATA(cs));
	ptr[7] = nandc_readl(NANDC_CHIP_DATA(cs));

	nandc_flash_de_cs(cs);
	if (ptr[0] != 0xFF && ptr[0] && ptr[1] != 0xFF)
		rkflash_print_error("No.%d FLASH ID:%x %x %x %x %x %x\n",
				    cs + 1, ptr[0], ptr[1], ptr[2],
				    ptr[3], ptr[4], ptr[5]);
}

static void flash_bch_sel(u8 bits)
{
	g_nand_flash_ecc_bits = bits;
	nandc_bch_sel(bits);
}

static void flash_set_sector(u8 num)
{
	nand_para.sec_per_page = num;
}

static __maybe_unused void flash_timing_cfg(u32 ahb_khz)
{
	nandc_time_cfg(nand_para.access_freq);
}

static void flash_read_cmd(u8 cs, u32 page_addr)
{
	nandc_writel(READ_CMD >> 8, NANDC_CHIP_CMD(cs));
	nandc_writel(0x00, NANDC_CHIP_ADDR(cs));
	nandc_writel(0x00, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr & 0x00ff, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr >> 8, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr >> 16, NANDC_CHIP_ADDR(cs));
	nandc_writel(READ_CMD & 0x00ff, NANDC_CHIP_CMD(cs));
}

static void flash_prog_first_cmd(u8 cs, u32 page_addr)
{
	nandc_writel(PAGE_PROG_CMD >> 8, NANDC_CHIP_CMD(cs));
	nandc_writel(0x00, NANDC_CHIP_ADDR(cs));
	nandc_writel(0x00, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr & 0x00ff, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr >> 8, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr >> 16, NANDC_CHIP_ADDR(cs));
}

static void flash_erase_cmd(u8 cs, u32 page_addr)
{
	nandc_writel(BLOCK_ERASE_CMD >> 8, NANDC_CHIP_CMD(cs));
	nandc_writel(page_addr & 0x00ff, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr >> 8, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr >> 16, NANDC_CHIP_ADDR(cs));
	nandc_writel(BLOCK_ERASE_CMD & 0x00ff, NANDC_CHIP_CMD(cs));
}

static void flash_prog_second_cmd(u8 cs, u32 page_addr)
{
	usleep_range(100, 120);
	nandc_writel(PAGE_PROG_CMD & 0x00ff, NANDC_CHIP_CMD(cs));
}

static u32 flash_read_status(u8 cs, u32 page_addr)
{
	nandc_writel(READ_STATUS_CMD, NANDC_CHIP_CMD(cs));
	nandc_delayns(80);

	return nandc_readl(NANDC_CHIP_DATA(cs));
}

static void flash_read_random_dataout_cmd(u8 cs, u32 col_addr)
{
	nandc_writel(READ_DP_OUT_CMD >> 8, NANDC_CHIP_CMD(cs));
	nandc_writel(col_addr & 0x00ff, NANDC_CHIP_ADDR(cs));
	nandc_writel(col_addr >> 8, NANDC_CHIP_ADDR(cs));
	nandc_writel(READ_DP_OUT_CMD & 0x00ff, NANDC_CHIP_CMD(cs));
}

static u32 flash_read_ecc(u8 cs)
{
	u32 ecc0, ecc1;

	nandc_writel(READ_ECC_STATUS_CMD, NANDC_CHIP_CMD(cs));
	nandc_delayns(80);
	ecc0 = nandc_readl(NANDC_CHIP_DATA(cs)) & 0xF;
	ecc1 = nandc_readl(NANDC_CHIP_DATA(cs)) & 0xF;
	if (ecc1 > ecc0)
		ecc0 = ecc1;
	ecc1 = nandc_readl(NANDC_CHIP_DATA(cs)) & 0xF;
	if (ecc1 > ecc0)
		ecc0 = ecc1;
	ecc1 = nandc_readl(NANDC_CHIP_DATA(cs)) & 0xF;
	if (ecc1 > ecc0)
		ecc0 = ecc1;

	return ecc0;
}

static u32 flash_read_page_raw(u8 cs, u32 page_addr, u32 *p_data, u32 *p_spare)
{
	u32 error_ecc_bits, ret;
	u32 sec_per_page = nand_para.sec_per_page;
	u32 nand_ecc = 0;

	nandc_wait_flash_ready(cs);
	nandc_flash_cs(cs);
	flash_read_cmd(cs, page_addr);
	nandc_wait_flash_ready(cs);
	flash_read_random_dataout_cmd(cs, 0);
	nandc_wait_flash_ready(cs);

	error_ecc_bits = nandc_xfer_data(cs, NANDC_READ, sec_per_page,
					 p_data, p_spare);

	nandc_flash_de_cs(cs);

	if (error_ecc_bits != NAND_STS_ECC_ERR) {
		if (error_ecc_bits >= (u32)nand_para.ecc_bits - 3) {
			ret = NAND_STS_REFRESH;
		} else {
			ret = NAND_STS_OK;
			if (g_nand_ecc_en) {
				nand_ecc = flash_read_ecc(cs);

				if (nand_ecc >= 6) {
					rkflash_print_error("%s nand ecc %x ecc %d\n",
							    __func__, page_addr, nand_ecc);
					ret = NAND_STS_REFRESH;
				}
			}
		}
	} else {
		ret = NAND_STS_ECC_ERR;
	}
	if (nand_ecc > 4 || error_ecc_bits > 4)
		rkflash_print_info("%s %x %x nandc ecc= %d, internal ecc= %d\n",
				   __func__, cs, page_addr, error_ecc_bits, nand_ecc);

	return ret;
}

static u32 flash_read_page(u8 cs, u32 page_addr, u32 *p_data, u32 *p_spare)
{
	u32 ret, i = 0;

	ret = flash_read_page_raw(cs, page_addr, p_data, p_spare);
	if (ret == NAND_STS_ECC_ERR) {
		for (; i < 50; i++) {
			ret = flash_read_page_raw(cs, page_addr, p_data, p_spare);
			if (ret != NAND_STS_ECC_ERR) {
				ret = NAND_STS_REFRESH;
				break;
			}
		}
		rkflash_print_error("%s %x err_ecc %d\n",
				    __func__, page_addr, ret);
	}
	rkflash_print_dio("%s %x %x retry=%x\n",
			   __func__, page_addr, p_data[0], i);

	return ret;
}

static u32 flash_prog_page(u8 cs, u32 page_addr, u32 *p_data, u32 *p_spare)
{
	u32 status;
	u32 sec_per_page = nand_para.sec_per_page;

	rkflash_print_dio("%s %x %x\n", __func__, page_addr, p_data[0]);
	nandc_wait_flash_ready(cs);
	nandc_flash_cs(cs);
	flash_prog_first_cmd(cs, page_addr);
	nandc_xfer_data(cs, NANDC_WRITE, sec_per_page, p_data, p_spare);
	flash_prog_second_cmd(cs, page_addr);
	nandc_wait_flash_ready(cs);
	status = flash_read_status(cs, page_addr);
	nandc_flash_de_cs(cs);
	status &= 0x01;
	if (status)
		rkflash_print_info("%s addr=%x status=%x\n",
				   __func__, page_addr, status);

	return status;
}

static u32 flash_erase_block(u8 cs, u32 page_addr)
{
	u32 status;

	rkflash_print_dio("%s %x\n", __func__, page_addr);
	nandc_wait_flash_ready(cs);
	nandc_flash_cs(cs);
	flash_erase_cmd(cs, page_addr);
	nandc_wait_flash_ready(cs);
	status = flash_read_status(cs, page_addr);
	nandc_flash_de_cs(cs);
	status &= 0x01;
	if (status)
		rkflash_print_info("%s pageadd=%x status=%x\n",
				   __func__, page_addr, status);

	return status;
}

static void flash_read_spare(u8 cs, u32 page_addr, u8 *spare)
{
	u32 col = nand_para.sec_per_page << 9;

	nandc_writel(READ_CMD >> 8, NANDC_CHIP_CMD(cs));
	nandc_writel(col, NANDC_CHIP_ADDR(cs));
	nandc_writel(col >> 8, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr & 0x00ff, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr >> 8, NANDC_CHIP_ADDR(cs));
	nandc_writel(page_addr >> 16, NANDC_CHIP_ADDR(cs));
	nandc_writel(READ_CMD & 0x00ff, NANDC_CHIP_CMD(cs));

	nandc_wait_flash_ready(cs);

	*spare = nandc_readl(NANDC_CHIP_DATA(cs));
}

/*
 * Read the 1st page's 1st spare byte of a phy_blk
 * If not FF, it's bad blk
 */
static s32 get_bad_blk_list(u16 *table, u32 die)
{
	u16 blk;
	u32 bad_cnt, page_addr0, page_addr1, page_addr2;
	u32 blk_per_die;
	u8 bad_flag0, bad_flag1, bad_flag2;

	bad_cnt = 0;
	blk_per_die = nand_para.plane_per_die * nand_para.blk_per_plane;
	for (blk = 0; blk < blk_per_die; blk++) {
		bad_flag0 = 0xFF;
		bad_flag1 = 0xFF;
		bad_flag2 = 0xFF;
		page_addr0 = (blk + blk_per_die * die) *
			nand_para.page_per_blk + 0;
		page_addr1 = page_addr0 + 1;
		page_addr2 = page_addr0 + nand_para.page_per_blk - 1;
		flash_read_spare(die, page_addr0, &bad_flag0);
		flash_read_spare(die, page_addr1, &bad_flag1);
		flash_read_spare(die, page_addr2, &bad_flag2);
		if (bad_flag0 != 0xFF ||
		    bad_flag1 != 0xFF ||
		    bad_flag2 != 0xFF) {
			table[bad_cnt++] = blk;
			rkflash_print_error("die[%d], bad_blk[%d]\n", die, blk);
		}
	}
	return bad_cnt;
}

#if FLASH_STRESS_TEST_EN

#define FLASH_PAGE_SIZE	2048
#define FLASH_SPARE_SIZE	8

static u16 bad_blk_list[1024];
static u32 *pwrite;
static u32 *pread;
static u32 pspare_write[FLASH_SPARE_SIZE / 4];
static u32 pspare_read[FLASH_SPARE_SIZE / 4];
static u32 bad_blk_num;
static u32 bad_page_num;

static void flash_test(void)
{
	u32 i, blk, page, bad_cnt, page_addr;
	int ret;
	u32 pages_num = 64;
	u32 blk_addr = 64;
	u32 is_bad_blk = 0;

	pwrite = kzalloc(FLASH_PAGE_SIZE, GFP_KERNEL | GFP_DMA);
	pread = kzalloc(FLASH_PAGE_SIZE, GFP_KERNEL | GFP_DMA);

	rkflash_print_error("%s\n", __func__);
	bad_blk_num = 0;
	bad_page_num = 0;
	bad_cnt	= get_bad_blk_list(bad_blk_list, 0);

	for (blk = 0; blk < 1024; blk++) {
		for (i = 0; i < bad_cnt; i++) {
			if (bad_blk_list[i] == blk)
				break;
		}
		if (i < bad_cnt)
			continue;
		is_bad_blk = 0;
		rkflash_print_error("Flash prog block: %x\n", blk);
		flash_erase_block(0, blk * blk_addr);
		for (page = 0; page < pages_num; page++) {
			page_addr = blk * blk_addr + page;
			for (i = 0; i < 512; i++)
				pwrite[i] = (page_addr << 16) + i;
			pspare_write[0] = pwrite[0] + 0x5AF0;
			pspare_write[1] = pspare_write[0] + 1;
			flash_prog_page(0, page_addr, pwrite, pspare_write);
			memset(pread, 0, 2048);
			memset(pspare_read, 0, 8);
			ret = flash_read_page(0, page_addr, pread,
					      pspare_read);
			if (ret != NAND_STS_OK)
				is_bad_blk = 1;
			for (i = 0; i < 512; i++) {
				if (pwrite[i] != pread[i]) {
					is_bad_blk = 1;
					break;
				}
			}
			for (i = 0; i < 2; i++) {
				if (pspare_write[i] != pspare_read[i]) {
					is_bad_blk = 1;
					break;
				}
			}
			if (is_bad_blk) {
				bad_page_num++;
				rkflash_print_error("ERR:page %x, ret= %x\n",
						    page_addr,
						    ret);
				rkflash_print_hex("w data:", pwrite, 4, 512);
				rkflash_print_hex("w spare:", pspare_write, 4, 4);
				rkflash_print_hex("r data:", pread, 4, 512);
				rkflash_print_hex("r spare:", pspare_read, 4, 5);
				while (1)
					;
			}
		}
		flash_erase_block(0, blk * blk_addr);
		if (is_bad_blk)
			bad_blk_num++;
	}
	rkflash_print_error("bad_blk_num = %d, bad_page_num = %d\n",
			    bad_blk_num, bad_page_num);

	rkflash_print_error("Flash Test Finish!!!\n");
	kfree(pwrite);
	kfree(pread);
	while (1)
		;
}
#endif

static void flash_die_info_init(void)
{
	u32 cs;

	g_nand_max_die = 0;
	for (cs = 0; cs < MAX_FLASH_NUM; cs++) {
		if (nand_para.nand_id[1] == id_byte[cs][1]) {
			die_cs_index[g_nand_max_die] = cs;
			g_nand_max_die++;
		}
	}
	g_totle_block = g_nand_max_die *  nand_para.plane_per_die *
			nand_para.blk_per_plane;
}

static void flash_rkflash_print_info(void)
{
	rkflash_print_info("No.0 FLASH ID: %x %x %x %x %x %x\n",
			   nand_para.nand_id[0],
			   nand_para.nand_id[1],
			   nand_para.nand_id[2],
			   nand_para.nand_id[3],
			   nand_para.nand_id[4],
			   nand_para.nand_id[5]);
	rkflash_print_info("die_per_chip: %x\n", nand_para.die_per_chip);
	rkflash_print_info("sec_per_page: %x\n", nand_para.sec_per_page);
	rkflash_print_info("page_per_blk: %x\n", nand_para.page_per_blk);
	rkflash_print_info("cell: %x\n", nand_para.cell);
	rkflash_print_info("plane_per_die: %x\n", nand_para.plane_per_die);
	rkflash_print_info("blk_per_plane: %x\n", nand_para.blk_per_plane);
	rkflash_print_info("TotleBlock: %x\n", g_totle_block);
	rkflash_print_info("die gap: %x\n", nand_para.die_gap);
	rkflash_print_info("lsb_mode: %x\n", nand_para.lsb_mode);
	rkflash_print_info("read_retry_mode: %x\n", nand_para.read_retry_mode);
	rkflash_print_info("ecc_bits: %x\n", nand_para.ecc_bits);
	rkflash_print_info("Use ecc_bits: %x\n", g_nand_flash_ecc_bits);
	rkflash_print_info("access_freq: %x\n", nand_para.access_freq);
	rkflash_print_info("opt_mode: %x\n", nand_para.opt_mode);

	rkflash_print_info("Cache read enable: %x\n",
			   nand_para.operation_opt & NAND_CACHE_READ_EN ? 1 : 0);
	rkflash_print_info("Cache random read enable: %x\n",
			   nand_para.operation_opt &
			   NAND_CACHE_RANDOM_READ_EN ? 1 : 0);
	rkflash_print_info("Cache prog enable: %x\n",
			   nand_para.operation_opt & NAND_CACHE_PROG_EN ? 1 : 0);
	rkflash_print_info("multi read enable: %x\n",
			   nand_para.operation_opt & NAND_MULTI_READ_EN ? 1 : 0);

	rkflash_print_info("multi prog enable: %x\n",
			   nand_para.operation_opt & NAND_MULTI_PROG_EN ? 1 : 0);
	rkflash_print_info("interleave enable: %x\n",
			   nand_para.operation_opt & NAND_INTERLEAVE_EN ? 1 : 0);

	rkflash_print_info("read retry enable: %x\n",
			   nand_para.operation_opt & NAND_READ_RETRY_EN ? 1 : 0);
	rkflash_print_info("randomizer enable: %x\n",
			   nand_para.operation_opt & NAND_RANDOMIZER_EN ? 1 : 0);

	rkflash_print_info("SDR enable: %x\n",
			   nand_para.operation_opt & NAND_SDR_EN ? 1 : 0);
	rkflash_print_info("ONFI enable: %x\n",
			   nand_para.operation_opt & NAND_ONFI_EN ? 1 : 0);
	rkflash_print_info("TOGGLE enable: %x\n",
			   nand_para.operation_opt & NAND_TOGGLE_EN ? 1 : 0);

	rkflash_print_info("g_nand_idb_res_blk_num: %x\n", g_nand_idb_res_blk_num);
}

static void ftl_flash_init(void)
{
	u8 nandc_ver = nandc_get_version();

	/* para init */
	g_nand_phy_info.nand_type	= nand_para.cell;
	g_nand_phy_info.die_num		= nand_para.die_per_chip;
	g_nand_phy_info.plane_per_die	= nand_para.plane_per_die;
	g_nand_phy_info.blk_per_plane	= nand_para.blk_per_plane;
	g_nand_phy_info.page_per_blk	= nand_para.page_per_blk;
	g_nand_phy_info.page_per_slc_blk	= nand_para.page_per_blk /
						  nand_para.cell;
	g_nand_phy_info.byte_per_sec	= 512;
	g_nand_phy_info.sec_per_page	= nand_para.sec_per_page;
	g_nand_phy_info.sec_per_blk	= nand_para.sec_per_page *
					  nand_para.page_per_blk;
	g_nand_phy_info.reserved_blk	= 8;
	g_nand_phy_info.blk_per_die	= nand_para.plane_per_die *
					  nand_para.blk_per_plane;
	g_nand_phy_info.ecc_bits	= nand_para.ecc_bits;

	/* driver register */
	g_nand_ops.get_bad_blk_list	= get_bad_blk_list;
	g_nand_ops.erase_blk		= flash_erase_block;
	g_nand_ops.prog_page		= flash_prog_page;
	g_nand_ops.read_page		= flash_read_page;
	if (nandc_ver == 9) {
		g_nand_ops.bch_sel = flash_bch_sel;
		g_nand_ops.set_sec_num = flash_set_sector;
	}
}

u32 nandc_flash_init(void __iomem *nandc_addr)
{
	u32 cs;

	rkflash_print_info("...%s enter...\n", __func__);
	g_nand_idb_res_blk_num = MAX_IDB_RESERVED_BLOCK;
	g_nand_ecc_en = 0;

	nandc_init(nandc_addr);

	for (cs = 0; cs < MAX_FLASH_NUM; cs++) {
		flash_read_id_raw(cs, id_byte[cs]);
		if (cs == 0) {
			if (id_byte[0][0] == 0xFF ||
			    id_byte[0][0] == 0 ||
			    id_byte[0][1] == 0xFF)
				return FTL_NO_FLASH;
			if (id_byte[0][1] != 0xF1 &&
			    id_byte[0][1] != 0xDA &&
			    id_byte[0][1] != 0xD1 &&
			    id_byte[0][1] != 0x95 &&
			    id_byte[0][1] != 0xDC &&
			    id_byte[0][1] != 0x48)

				return FTL_UNSUPPORTED_FLASH;
		}
	}
	if (id_byte[0][0] == 0x98 && (id_byte[0][4] & 0x80))
		g_nand_ecc_en = 1;
	nand_para.nand_id[1] = id_byte[0][1];
	if (id_byte[0][1] == 0xDA) {
		nand_para.plane_per_die = 2;
		nand_para.nand_id[1] = 0xDA;
	} else if (id_byte[0][1] == 0xDC) {
		nand_para.nand_id[1] = 0xDC;
		if (id_byte[0][0] == 0x2C && id_byte[0][3] == 0xA6) {
			nand_para.plane_per_die = 2;
			nand_para.sec_per_page = 8;
		} else if (id_byte[0][0] == 0x98 && id_byte[0][3] == 0x26) {
			nand_para.blk_per_plane = 1024;
			nand_para.sec_per_page = 8;
			nand_para.plane_per_die = 2;
		} else {
			nand_para.plane_per_die = 2;
			nand_para.blk_per_plane = 2048;
		}
	} else if (id_byte[0][1] == 0x48) {
		nand_para.sec_per_page = 8;
		nand_para.page_per_blk = 128;
		nand_para.plane_per_die = 2;
		nand_para.blk_per_plane = 2048;
	}
	flash_die_info_init();
	flash_bch_sel(nand_para.ecc_bits);
	flash_rkflash_print_info();
	ftl_flash_init();

	#if FLASH_STRESS_TEST_EN
	flash_test();
	#endif

	return 0;
}

void nandc_flash_get_id(u8 cs, void *buf)
{
	memcpy(buf, id_byte[cs], 5);
}

u32 nandc_flash_deinit(void)
{
	return 0;
}
