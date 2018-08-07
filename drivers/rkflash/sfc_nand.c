// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/kernel.h>

#include "flash_com.h"
#include "rkflash_debug.h"
#include "rk_sftl.h"
#include "sfc.h"
#include "sfc_nand.h"

static struct nand_info spi_nand_tbl[] = {
	/* TC58CVG0S0HxAIx */
	{0x98C2, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x02, 0xD8, 0x00, 18, 8, 0xB0, 0XFF, 4, 8, NULL},
	/* TC58CVG1S0HxAIx */
	{0x98CB, 4, 64, 2, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x02, 0xD8, 0x00, 19, 8, 0xB0, 0XFF, 4, 8, NULL},
	/* MX35LF1GE4AB */
	{0xC212, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 18, 4, 0xB0, 0, 4, 8, &sfc_nand_ecc_status_sp1},
	/* MX35LF2GE4AB */
	{0xC222, 4, 64, 2, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 19, 4, 0xB0, 0, 4, 8, &sfc_nand_ecc_status_sp1},
	/* MX66L1G45G */
	{0x90AF, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x00, 18, 4, 0xB0, 0, 4, 8, &sfc_nand_ecc_status_sp1},
	/* GD5F1GQ4UAYIG */
	{0xC8F1, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 18, 8, 0xB0, 0, 4, 8, NULL},
	/* MT29F1G01ZAC */
	{0x2C12, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x00, 18, 1, 0xB0, 0, 4, 8, &sfc_nand_ecc_status_sp1},
	/* GD5F2GQ40BY2GR */
	{0xC8D2, 4, 64, 2, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 19, 8, 0xB0, 0, 4, 8, &sfc_nand_ecc_status_sp3},
	/* GD5F1GQ4U */
	{0xC8D1, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 18, 8, 0xB0, 0, 4, 8, &sfc_nand_ecc_status_sp3},
	/* IS37SML01G1 */
	{0xC821, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x00, 18, 1, 0xB0, 0XFF, 8, 12, &sfc_nand_ecc_status_sp1},
	/* W25N01GV */
	{0xEFAA, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 18, 1, 0xFF, 0XFF, 4, 20, &sfc_nand_ecc_status_sp1},
	/* HYF2GQ4UAACAE */
	{0xC952, 4, 64, 2, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 19, 14, 0xB0, 0, 4, 36, NULL},
	/* HYF2GQ4UAACAE */
	{0xC952, 4, 64, 1, 2048, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 19, 14, 0xB0, 0, 4, 36, NULL},
	/* HYF2GQ4UDACAE */
	{0xC922, 4, 64, 1, 2048, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 19, 4, 0xB0, 0, 4, 20, NULL},
	/* HYF2GQ4UHCCAE */
	{0xC95A, 4, 64, 1, 2048, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 19, 14, 0xB0, 0, 4, 36, NULL},
	/* HYF1GQ4UDACAE */
	{0xC921, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 18, 4, 0xB0, 0, 4, 20, NULL},
	/* F50L1G41LB */
	{0xC801, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 18, 1, 0xB0, 0xFF, 20, 36, NULL},
	/* XT26G02A */
	{0x0be2, 4, 64, 1, 2048, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 19, 1, 0xB0, 0x0, 8, 12, &sfc_nand_ecc_status_sp3},
	/* XT26G01A */
	{0x0be1, 4, 64, 1, 1024, 0x13, 0x10, 0x03, 0x02, 0x6B, 0x32, 0xD8, 0x0C, 18, 1, 0xB0, 0x0, 8, 12, &sfc_nand_ecc_status_sp3},
};

static u8 id_byte[8];
static struct nand_info *p_nand_info;
static u32 gp_page_buf[SFC_NAND_PAGE_MAX_SIZE / 4];
static struct SFNAND_DEV sfc_nand_dev;

static struct nand_info *spi_nand_get_info(u8 *nand_id)
{
	u32 i;
	u32 id = (nand_id[0] << 8) | (nand_id[1] << 0);

	for (i = 0; i < ARRAY_SIZE(spi_nand_tbl); i++) {
		if (spi_nand_tbl[i].id == id)
			return &spi_nand_tbl[i];
	}
	return NULL;
}

static int sfc_nand_write_en(void)
{
	int ret;
	union SFCCMD_DATA sfcmd;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = CMD_WRITE_EN;
	ret = sfc_request(sfcmd.d32, 0, 0, NULL);
	return ret;
}

static int sfc_nand_rw_preset(void)
{
	int ret;
	union SFCCTRL_DATA sfctrl;
	union SFCCMD_DATA sfcmd;
	u8 status = 0xFF;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = 0;
	sfcmd.b.datasize = 1;
	sfcmd.b.rw = SFC_WRITE;

	sfctrl.b.datalines = 2;
	ret = sfc_request(sfcmd.d32, sfctrl.d32, 0, &status);
	return ret;
}

static int sfc_nand_read_feature(u8 addr, u8 *data)
{
	int ret;
	union SFCCMD_DATA sfcmd;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = 0x0F;
	sfcmd.b.datasize = 1;
	sfcmd.b.addrbits = SFC_ADDR_XBITS;
	*data = 0;

	ret = sfc_request(sfcmd.d32, 0x8 << 16, addr, data);
	if (ret != SFC_OK)
		return ret;
	return SFC_OK;
}

static int sfc_nand_write_feature(u32 addr, u8 status)
{
	int ret;
	union SFCCMD_DATA sfcmd;

	sfc_nand_write_en();

	sfcmd.d32 = 0;
	sfcmd.b.cmd = 0x1F;
	sfcmd.b.datasize = 1;
	sfcmd.b.addrbits = SFC_ADDR_XBITS;
	sfcmd.b.rw = SFC_WRITE;

	ret = sfc_request(sfcmd.d32, 0x8 << 16, addr, &status);
	if (ret != SFC_OK)
		return ret;
	return ret;
}

static int sfc_nand_wait_busy(u8 *data, int timeout)
{
	int ret;
	int i;
	u8 status;

	*data = 0;
	for (i = 0; i < timeout; i++) {
		ret = sfc_nand_read_feature(0xC0, &status);
		if (ret != SFC_OK)
			return ret;
		*data = status;
		if (!(status & (1 << 0)))
			return SFC_OK;
		sfc_delay(1);
	}
	return -1;
}

/*
 * ecc default:
 * 0, No bit errors were detected
 * 1, Bit errors were detected and corrected.
 * 2, Multiple bit errors were detected and not corrected.
 * 3, Bits errors were detected and corrected, bit error count
 *	exceed the bit flip detection threshold
 */
static u32 sfc_nand_ecc_status(void)
{
	int ret;
	u32 i;
	u8 ecc;
	u8 status;
	u32 timeout = 1000 * 1000;

	for (i = 0; i < timeout; i++) {
		ret = sfc_nand_read_feature(0xC0, &status);
		if (ret != SFC_OK)
			return SFC_NAND_ECC_ERROR;
		if (!(status & (1 << 0)))
			break;
		sfc_delay(1);
	}

	ecc = (status >> 4) & 0x03;

	if (ecc <= 1)
		ret = SFC_NAND_ECC_OK;
	else if (ecc == 2)
		ret = SFC_NAND_ECC_ERROR;
	else
		ret = SFC_NAND_ECC_REFRESH;

	return ret;
}

/*
 * ecc spectial type1:
 * 0x00, No bit errors were detected;
 * 0x01, Bits errors were detected and corrected, bit error count
 *	may reach the bit flip detection threshold;
 * 0x10, Multiple bit errors were detected and not corrected;
 * 0x11, Reserved.
 */
u32 sfc_nand_ecc_status_sp1(void)
{
	int ret;
	u32 i;
	u8 ecc;
	u8 status;
	u32 timeout = 1000 * 1000;

	for (i = 0; i < timeout; i++) {
		ret = sfc_nand_read_feature(0xC0, &status);
		if (ret != SFC_OK)
			return SFC_NAND_ECC_ERROR;
		if (!(status & (1 << 0)))
			break;
		sfc_delay(1);
	}

	ecc = (status >> 4) & 0x03;

	if (ecc == 0)
		ret = SFC_NAND_ECC_OK;
	else if (ecc == 1)
		ret = SFC_NAND_ECC_REFRESH;
	else
		ret = SFC_NAND_ECC_ERROR;

	return ret;
}

/*
 * ecc spectial type3:
 * [0x0000, 0x0011], No bit errors were detected;
 * [0x0100, 0x0111], Bit errors were detected and corrected. Not
 *	reach Flipping Bits;
 * [0x1000, 0x1011], Multiple bit errors were detected and
 *	not corrected.
 * [0x1100, 0x1111], Bit error count equals the bit flip
 *	detectionthreshold
 */
u32 sfc_nand_ecc_status_sp3(void)
{
	int ret;
	u32 i;
	u8 ecc;
	u8 status, status1;
	u32 timeout = 1000 * 1000;

	for (i = 0; i < timeout; i++) {
		ret = sfc_nand_read_feature(0xC0, &status);
		if (ret != SFC_OK)
			return SFC_NAND_ECC_ERROR;
		ret = sfc_nand_read_feature(0xF0, &status1);
		if (ret != SFC_OK)
			return SFC_NAND_ECC_ERROR;
		if (!(status & (1 << 0)))
			break;
		sfc_delay(1);
	}

	ecc = (status >> 4) & 0x03;
	ecc = (ecc << 2) | ((status1 >> 4) & 0x03);
	if (ecc < 7)
		ret = SFC_NAND_ECC_OK;
	else if (ecc == 7 || ecc >= 12)
		ret = SFC_NAND_ECC_REFRESH;
	else
		ret = SFC_NAND_ECC_ERROR;

	return ret;
}

static u32 sfc_nand_erase_block(u8 cs, u32 addr)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	u8 status;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = p_nand_info->block_erase_cmd;
	sfcmd.b.addrbits = SFC_ADDR_24BITS;
	sfc_nand_write_en();
	ret = sfc_request(sfcmd.d32, 0, addr, NULL);
	if (ret != SFC_OK)
		return ret;
	ret = sfc_nand_wait_busy(&status, 1000 * 1000);
	if (status & (1 << 2))
		return SFC_NAND_PROG_ERASE_ERROR;
	return ret;
}

static u32 sfc_nand_prog_page(u8 cs, u32 addr, u32 *p_data, u32 *p_spare)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	union SFCCTRL_DATA sfctrl;
	u8 status;
	u32 data_sz = 2048;
	u32 spare_offs_1 = p_nand_info->spare_offs_1;
	u32 spare_offs_2 = p_nand_info->spare_offs_2;

	memcpy(gp_page_buf, p_data, data_sz);
	gp_page_buf[(data_sz + spare_offs_1) / 4] = p_spare[0];
	gp_page_buf[(data_sz + spare_offs_2) / 4] = p_spare[1];

	sfc_nand_write_en();
	if (sfc_nand_dev.prog_lines == DATA_LINES_X4 &&
	    p_nand_info->QE_address == 0xFF &&
	    sfc_get_version() != SFC_VER_3)
		sfc_nand_rw_preset();

	sfcmd.d32 = 0;
	sfcmd.b.cmd = sfc_nand_dev.page_prog_cmd;
	sfcmd.b.addrbits = SFC_ADDR_XBITS;
	sfcmd.b.datasize = SFC_NAND_PAGE_MAX_SIZE;
	sfcmd.b.rw = SFC_WRITE;

	sfctrl.d32 = 0;
	sfctrl.b.datalines = sfc_nand_dev.prog_lines;
	sfctrl.b.addrbits = 16;
	sfc_request(sfcmd.d32, sfctrl.d32, 0, gp_page_buf);

	sfcmd.d32 = 0;
	sfcmd.b.cmd = p_nand_info->page_prog_cmd;
	sfcmd.b.addrbits = SFC_ADDR_24BITS;
	sfcmd.b.datasize = 0;
	sfcmd.b.rw = SFC_WRITE;
	ret = sfc_request(sfcmd.d32, 0, addr, p_data);
	if (ret != SFC_OK)
		return ret;
	ret = sfc_nand_wait_busy(&status, 1000 * 1000);
	if (status & (1 << 3))
		return SFC_NAND_PROG_ERASE_ERROR;
	return ret;
}

static u32 sfc_nand_read_page(u8 cs, u32 addr, u32 *p_data, u32 *p_spare)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	union SFCCTRL_DATA sfctrl;
	u32 ecc_result;
	u32 data_sz = 2048;
	u32 spare_offs_1 = p_nand_info->spare_offs_1;
	u32 spare_offs_2 = p_nand_info->spare_offs_2;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = p_nand_info->page_read_cmd;
	sfcmd.b.datasize = 0;
	sfcmd.b.addrbits = SFC_ADDR_24BITS;
	sfc_request(sfcmd.d32, 0, addr, p_data);

	if (p_nand_info->ecc_status)
		ecc_result = p_nand_info->ecc_status();
	else
		ecc_result = sfc_nand_ecc_status();

	if (sfc_nand_dev.read_lines == DATA_LINES_X4 &&
	    p_nand_info->QE_address == 0xFF &&
	    sfc_get_version() != SFC_VER_3)
		sfc_nand_rw_preset();

	sfcmd.d32 = 0;
	sfcmd.b.cmd = sfc_nand_dev.page_read_cmd;
	sfcmd.b.datasize = SFC_NAND_PAGE_MAX_SIZE;
	sfcmd.b.addrbits = SFC_ADDR_24BITS;
	sfctrl.d32 = 0;
	sfctrl.b.datalines = sfc_nand_dev.read_lines;

	memset(gp_page_buf, 0, SFC_NAND_PAGE_MAX_SIZE);
	ret = sfc_request(sfcmd.d32, sfctrl.d32, 0, gp_page_buf);

	memcpy(p_data, gp_page_buf, data_sz);
	p_spare[0] = gp_page_buf[(data_sz + spare_offs_1) / 4];
	p_spare[1] = gp_page_buf[(data_sz + spare_offs_2) / 4];
	if (ret != SFC_OK)
		return SFC_NAND_ECC_ERROR;

	if (ecc_result != SFC_NAND_ECC_OK) {
		PRINT_SFC_E("%s[0x%x], ret=0x%x\n", __func__, addr, ecc_result);
		if (p_data)
			PRINT_SFC_HEX("data:", p_data, 4, 8);
		if (p_spare)
			PRINT_SFC_HEX("spare:", p_spare, 4, 2);
	}
	return ecc_result;
}

static int sfc_nand_read_id_raw(u8 *data)
{
	int ret;
	union SFCCMD_DATA sfcmd;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = CMD_READ_JEDECID;
	sfcmd.b.datasize = 3;
	sfcmd.b.addrbits = SFC_ADDR_XBITS;

	ret = sfc_request(sfcmd.d32, 0x8 << 16, 0, data);

	return ret;
}

/*
 * Read the 1st page's 1st byte of a phy_blk
 * If not FF, it's bad blk
 */
static int sfc_nand_get_bad_block_list(u16 *table, u32 die)
{
	u16 blk;
	u32 bad_cnt, page;
	u32 blk_per_die;
	u32 *pread;
	u32 *pspare_read;

	PRINT_SFC_E("%s\n", __func__);
	pread = ftl_malloc(2048);
	pspare_read = ftl_malloc(8);
	bad_cnt = 0;
	blk_per_die = p_nand_info->plane_per_die *
			p_nand_info->blk_per_plane;
	for (blk = 0; blk < blk_per_die; blk++) {
		page = (blk + blk_per_die * die) *
			p_nand_info->page_per_blk;
		sfc_nand_read_page(0, page, pread, pspare_read);

		if (pread[0] != 0xFFFFFFFF ||
		    pspare_read[0] != 0xFFFFFFFF) {
			table[bad_cnt++] = blk;
			PRINT_SFC_E("die[%d], bad_blk[%d]\n", die, blk);
		}
	}
	ftl_free(pread);
	ftl_free(pspare_read);
	return (int)bad_cnt;
}

#if SFC_NAND_STRESS_TEST_EN

#define SFC_NAND_PAGE_SIZE	2048
#define SFC_NAND_SPARE_SIZE	8

static u16 bad_blk_list[1024];
static u32 pwrite[SFC_NAND_PAGE_SIZE / 4];
static u32 pread[SFC_NAND_PAGE_SIZE / 4];
static u32 pspare_write[SFC_NAND_SPARE_SIZE / 4];
static u32 pspare_read[SFC_NAND_SPARE_SIZE / 4];
static u32 bad_blk_num;
static u32 bad_page_num;

static void sfc_nand_test(void)
{
	u32 i, blk, page, bad_cnt, page_addr;
	int ret;
	u32 pages_num = 64;
	u32 blk_addr = 64;
	u32 is_bad_blk = 0;

	PRINT_SFC_E("%s\n", __func__);

	bad_blk_num = 0;
	bad_page_num = 0;
	bad_cnt	= sfc_nand_get_bad_block_list(bad_blk_list, 0);

	for (blk = 0; blk < 1024; blk++) {
		for (i = 0; i < bad_cnt; i++) {
			if (bad_blk_list[i] == blk)
				break;
		}
		if (i < bad_cnt)
			continue;
		is_bad_blk = 0;
		PRINT_SFC_E("Flash prog block: %x\n", blk);
		sfc_nand_erase_block(0, blk * blk_addr);
		for (page = 0; page < pages_num; page++) {
			page_addr = blk * blk_addr + page;
			for (i = 0; i < 512; i++)
				pwrite[i] = (page_addr << 16) + i;
			pspare_write[0] = pwrite[0] + 0x5AF0;
			pspare_write[1] = pspare_write[0] + 1;
			sfc_nand_prog_page(0, page_addr, pwrite, pspare_write);
			memset(pread, 0, 2048);
			memset(pspare_read, 0, 8);
			ret = sfc_nand_read_page(0, page_addr, pread,
						 pspare_read);
			if (ret != SFC_NAND_ECC_OK)
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
				PRINT_SFC_E("ERR:page%x, ret=%x\n",
					    page_addr, ret);
				PRINT_SFC_HEX("data:", pread, 4, 8);
				PRINT_SFC_HEX("spare:", pspare_read, 4, 2);
			}
		}
		sfc_nand_erase_block(0, blk * blk_addr);
		if (is_bad_blk)
			bad_blk_num++;
	}
	PRINT_SFC_E("bad_blk_num = %d, bad_page_num = %d\n",
		    bad_blk_num, bad_page_num);

	PRINT_SFC_E("Flash Test Finish!!!\n");
	while (1)
		;
}
#endif

static void ftl_flash_init(void)
{
	/* para init */
	g_nand_phy_info.nand_type	= 1;
	g_nand_phy_info.die_num		= 1;
	g_nand_phy_info.plane_per_die	= p_nand_info->plane_per_die;
	g_nand_phy_info.blk_per_plane	= p_nand_info->blk_per_plane;
	g_nand_phy_info.page_per_blk	= p_nand_info->page_per_blk;
	g_nand_phy_info.page_per_slc_blk = p_nand_info->page_per_blk;
	g_nand_phy_info.byte_per_sec	= 512;
	g_nand_phy_info.sec_per_page	= p_nand_info->sec_per_page;
	g_nand_phy_info.sec_per_blk	= p_nand_info->sec_per_page *
					  p_nand_info->page_per_blk;
	g_nand_phy_info.reserved_blk	= 8;
	g_nand_phy_info.blk_per_die	= p_nand_info->plane_per_die *
					  p_nand_info->blk_per_plane;
	g_nand_phy_info.ecc_bits	= p_nand_info->max_ecc_bits;

	/* driver register */
	g_nand_ops.get_bad_blk_list	= sfc_nand_get_bad_block_list;
	g_nand_ops.erase_blk		= sfc_nand_erase_block;
	g_nand_ops.prog_page		= sfc_nand_prog_page;
	g_nand_ops.read_page		= sfc_nand_read_page;
}

static int spi_nand_enable_QE(void)
{
	int ret = SFC_OK;
	u8 status;
	int bit_offset = p_nand_info->QE_bits;

	if (bit_offset == 0xFF)
		return SFC_OK;

	ret = sfc_nand_read_feature(p_nand_info->QE_address, &status);
	if (ret != SFC_OK)
		return ret;

	if (status & (1 << bit_offset))   /* is QE bit set */
		return SFC_OK;

	status |= (1 << bit_offset);
		return sfc_nand_write_feature(p_nand_info->QE_address, status);

	return ret;
}

u32 sfc_nand_init(void)
{
	PRINT_SFC_I("...%s enter...\n", __func__);

	sfc_nand_read_id_raw(id_byte);
	PRINT_SFC_E("sfc_nand id: %x %x %x\n",
		    id_byte[0], id_byte[1], id_byte[2]);
	if (id_byte[0] == 0xFF || id_byte[0] == 0x00)
		return FTL_NO_FLASH;

	p_nand_info = spi_nand_get_info(id_byte);
	if (!p_nand_info)
		return FTL_UNSUPPORTED_FLASH;

	sfc_nand_dev.manufacturer = id_byte[0];
	sfc_nand_dev.mem_type = id_byte[1];

	/* disable block lock */
	sfc_nand_write_feature(0xA0, 0);
	sfc_nand_dev.read_lines = DATA_LINES_X1;
	sfc_nand_dev.prog_lines = DATA_LINES_X1;
	sfc_nand_dev.page_read_cmd = p_nand_info->read_cache_cmd_1;
	sfc_nand_dev.page_prog_cmd = p_nand_info->prog_cache_cmd_1;
	if (p_nand_info->feature & FEA_4BIT_READ) {
		if (spi_nand_enable_QE() == SFC_OK) {
			sfc_nand_dev.read_lines = DATA_LINES_X4;
			sfc_nand_dev.page_read_cmd =
				p_nand_info->read_cache_cmd_4;
		}
	}

	if (p_nand_info->feature & FEA_4BIT_PROG &&
	    sfc_nand_dev.read_lines == DATA_LINES_X4) {
		sfc_nand_dev.prog_lines = DATA_LINES_X4;
		sfc_nand_dev.page_prog_cmd = p_nand_info->prog_cache_cmd_4;
	}

	if (1) {
		u8 status;

		sfc_nand_read_feature(0xA0, &status);
		PRINT_SFC_I("sfc_nand A0 = 0x%x\n", status);
		sfc_nand_read_feature(0xB0, &status);
		PRINT_SFC_I("sfc_nand B0 = 0x%x\n", status);
		sfc_nand_read_feature(0xC0, &status);
		PRINT_SFC_I("sfc_nand C0 = 0x%x\n", status);
		PRINT_SFC_I("read_lines = %x\n", sfc_nand_dev.read_lines);
		PRINT_SFC_I("prog_lines = %x\n", sfc_nand_dev.prog_lines);
		PRINT_SFC_I("page_read_cmd = %x\n", sfc_nand_dev.page_read_cmd);
		PRINT_SFC_I("page_prog_cmd = %x\n", sfc_nand_dev.page_prog_cmd);
	}
	ftl_flash_init();

	#if SFC_NAND_STRESS_TEST_EN
	sfc_nand_test();
	#endif

	return SFC_OK;
}

void sfc_nand_deinit(void)
{
}

int sfc_nand_read_id(u8 *data)
{
	memcpy(data, id_byte, 3);
	return 0;
}
