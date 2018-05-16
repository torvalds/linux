// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/delay.h>
#include <linux/kernel.h>

#include "flash.h"
#include "flash_com.h"
#include "nandc.h"
#include "typedef.h"

#define     CPU_DELAY_NS(n)	ndelay(n)

#define	    NANDC_MASTER_EN

void __iomem *nandc_base;

static u32 g_nandc_ecc_bits;
#ifdef NANDC_MASTER_EN
static struct MASTER_INFO_T master;
static u32 *g_master_temp_buf;
#endif

void nandc_init(void __iomem *nandc_addr)
{
	union FM_CTL_T ctl_reg;

	nandc_base = nandc_addr;

	ctl_reg.d32 = 0;
	ctl_reg.V6.wp = 1;
	nandc_writel(ctl_reg.d32, NANDC_FMCTL);
	nandc_writel(0, NANDC_RANDMZ_CFG);
	nandc_time_cfg(40);

#ifdef NANDC_MASTER_EN
	if (!g_master_temp_buf)
		g_master_temp_buf = (u32 *)ftl_malloc(MAX_FLASH_PAGE_SIZE +
					      MAX_FLASH_PAGE_SIZE / 8);
	master.page_buf = &g_master_temp_buf[0];
	master.spare_buf = &g_master_temp_buf[MAX_FLASH_PAGE_SIZE / 4];
	master.mapped = 0;
#endif
}

void nandc_flash_cs(u8 chip_sel)
{
	union FM_CTL_T tmp;

	tmp.d32 = nandc_readl(NANDC_FMCTL);
	tmp.V6.cs = 0x01 << chip_sel;
	nandc_writel(tmp.d32, NANDC_FMCTL);
}

void nandc_flash_de_cs(u8 chip_sel)
{
	union FM_CTL_T tmp;

	tmp.d32 = nandc_readl(NANDC_FMCTL);
	tmp.V6.cs = 0;
	tmp.V6.flash_abort_clear = 0;
	nandc_writel(tmp.d32, NANDC_FMCTL);
}

u32 nandc_delayns(u32 count)
{
	CPU_DELAY_NS(count);
	return 0;
}

u32 nandc_wait_flash_ready(u8 chip_sel)
{
	union FM_CTL_T tmp;
	u32 status;
	u32 i;

	status = 0;
	for (i = 0; i < 100000; i++) {
		nandc_delayns(100);
		tmp.d32 = nandc_readl(NANDC_FMCTL);
		if (tmp.V6.rdy != 0)
			break;
	}

	if (i >= 100000)
		status = -1;
	return status;
}

void nandc_randmz_sel(u8 chip_sel, u32 randmz_seed)
{
	nandc_writel(randmz_seed, NANDC_RANDMZ_CFG);
}

void nandc_time_cfg(u32 ns)
{
	if (ns < 36)
		nandc_writel(0x1061, NANDC_FMWAIT);
	else if (ns >= 100)
		nandc_writel(0x2082, NANDC_FMWAIT);
	else
		nandc_writel(0x1081, NANDC_FMWAIT);
}

void nandc_bch_sel(u8 bits)
{
	union BCH_CTL_T tmp;
	union FL_CTL_T fl_reg;

	fl_reg.d32 = 0;
	fl_reg.V6.rst = 1;
	nandc_writel(fl_reg.d32, NANDC_FLCTL);
	g_nandc_ecc_bits = bits;
	tmp.d32 = 0;
	tmp.V6.addr = 0x10;
	tmp.V6.bch_mode1 = 0;
	if (bits == 16) {
		tmp.V6.bch_mode = 0;
	} else if (bits == 24) {
		tmp.V6.bch_mode = 1;
	} else {
		tmp.V6.bch_mode1 = 1;
		tmp.V6.bch_mode = 1;
		if (bits == 40)
			tmp.V6.bch_mode = 0;
	}
	tmp.V6.rst = 1;
	nandc_writel(tmp.d32, NANDC_BCHCTL);
}

static void nandc_xfer_start(u8 chip_sel,
			     u8 dir,
			     u8 sector_count,
			     u8 st_buf,
			     u32 *p_data,
			     u32 *p_spare)
{
	union BCH_CTL_T bch_reg;
	union FL_CTL_T fl_reg;
	u8 bus_mode = (p_spare || p_data);
	u32 i;
	union MTRANS_CFG_T master_reg;
	u16 *p_spare_tmp = (u16 *)p_spare;

	fl_reg.d32 = 0;
	bch_reg.d32 = nandc_readl(NANDC_BCHCTL);
	bch_reg.V6.addr = 0x10;
	bch_reg.V6.power_down = 0;
	bch_reg.V6.region = chip_sel;

	fl_reg.V6.rdn = dir;
	fl_reg.V6.dma = 1;
	fl_reg.V6.tr_count = 1;
	fl_reg.V6.async_tog_mix = 1;
	fl_reg.V6.cor_en = 1;
	fl_reg.V6.st_addr = st_buf / 2;

	master_reg.d32 = nandc_readl(NANDC_MTRANS_CFG);
	master_reg.V6.bus_mode = 0;
	#ifdef NANDC_MASTER_EN
	if (bus_mode != 0 && dir != 0) {
		u32 spare_sz = 64;

		for (i = 0; i < sector_count / 2; i++) {
			if (p_spare) {
				master.spare_buf[i * spare_sz / 4] =
				(p_spare_tmp[0]) | ((u32)p_spare_tmp[1] << 16);
				p_spare_tmp += 2;
			} else{
				master.spare_buf[i * spare_sz / 4] =
				0xffffffff;
			}
		}
	}
	fl_reg.V6.page_num = (sector_count + 1) / 2;
	master.page_vir = (u32 *)((p_data == (u32 *)NULL) ?
				  master.page_buf :
				  (u32 *)p_data);
	master.spare_vir = (u32 *)master.spare_buf;
	master.page_phy =
	(u32)rknandc_dma_map_single((unsigned long)master.page_vir,
				    fl_reg.V6.page_num * 1024,
				    dir);
	master.spare_phy =
	(u32)rknandc_dma_map_single((unsigned long)master.spare_vir,
				    fl_reg.V6.page_num * 64,
				    dir);
	master.mapped = 1;
	nandc_writel(master.page_phy, NANDC_MTRANS_SADDR0);
	nandc_writel(master.spare_phy, NANDC_MTRANS_SADDR1);
	master_reg.d32 = 0;
	master_reg.V6.incr_num = 16;
	master_reg.V6.burst = 7;
	if ((((unsigned long)p_data) & 0x03) == 0)
		master_reg.V6.hsize = 2;
	master_reg.V6.bus_mode = 1;
	master_reg.V6.ahb_wr = !dir;
	master_reg.V6.ahb_wr_st = 1;
	#endif

	nandc_writel(master_reg.d32, NANDC_MTRANS_CFG);
	nandc_writel(bch_reg.d32, NANDC_BCHCTL);
	nandc_writel(fl_reg.d32, NANDC_FLCTL);
	fl_reg.V6.start = 1;
	nandc_writel(fl_reg.d32, NANDC_FLCTL);
}

static void nandc_xfer_comp(u8 chip_sel)
{
	union FL_CTL_T fl_reg;
	union MTRANS_CFG_T master_reg;

	master_reg.d32 = nandc_readl(NANDC_MTRANS_CFG);
	if (master_reg.V6.bus_mode != 0) {
		union MTRANS_STAT_T stat_reg;

		if (master_reg.V6.ahb_wr != 0) {
			do {
				fl_reg.d32 = nandc_readl(NANDC_FLCTL);
				stat_reg.d32 = nandc_readl(NANDC_MTRANS_STAT);
			} while (stat_reg.V6.mtrans_cnt < fl_reg.V6.page_num);

			if (master.mapped) {
				rknandc_dma_unmap_single((u64)master.page_phy,
						fl_reg.V6.page_num * 1024,
						0);
				rknandc_dma_unmap_single((u64)master.spare_phy,
						fl_reg.V6.page_num * 64,
						0);
			}
		} else {
			do {
				fl_reg.d32 = nandc_readl(NANDC_FLCTL);
			} while (fl_reg.V6.tr_rdy == 0);
		}
	} else {
		do {
			fl_reg.d32 = nandc_readl(NANDC_FLCTL);
		} while ((fl_reg.V6.tr_rdy == 0));
	}
}

u32 nandc_xfer_data(u8 chip_sel, u8 dir, u8 sector_count,
		    u32 *p_data, u32 *p_spare)
{
	u32 status = NAND_STS_OK;
	u32 i;
	u32 spare[16];
	union BCH_ST_T bch_st_reg;

	if (dir == NANDC_WRITE && !p_spare) {
		p_spare = (u32 *)spare;
		memset(spare, 0xFF, sizeof(spare));
	}
	nandc_xfer_start(chip_sel, dir, sector_count, 0, p_data, p_spare);
	nandc_xfer_comp(chip_sel);
	if (dir == NANDC_READ) {
		if (p_spare) {
			u32 spare_sz = 64;
			u32 temp_data;
			u8 *p_spare_temp = (u8 *)p_spare;

			for (i = 0; i < sector_count / 2; i++) {
				temp_data = master.spare_buf[i * spare_sz / 4];
				*p_spare_temp++ = (u8)temp_data;
				*p_spare_temp++ = (u8)(temp_data >> 8);
				*p_spare_temp++ = (u8)(temp_data >> 16);
				*p_spare_temp++ = (u8)(temp_data >> 24);
			}
		}
		for (i = 0; i < sector_count / 4 ; i++) {
			bch_st_reg.d32 = nandc_readl(NANDC_BCHST(i));
			if (bch_st_reg.V6.fail0 || bch_st_reg.V6.fail1) {
				status = NAND_STS_ECC_ERR;
			} else {
				u32 tmp = 0;

				tmp =
				max(bch_st_reg.V6.err_bits0 |
				    ((u32)bch_st_reg.V6.err_bits0_5 << 5),
				    bch_st_reg.V6.err_bits1 |
				    ((u32)bch_st_reg.V6.err_bits1_5 << 5));
				status = max(tmp, status);
			}
		}
	}
	nandc_writel(0, NANDC_MTRANS_CFG);
	return status;
}

void nandc_clean_irq(void)
{
}
