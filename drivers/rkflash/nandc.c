// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/delay.h>
#include <linux/kernel.h>

#include "flash.h"
#include "flash_com.h"
#include "nandc.h"
#include "rk_sftl.h"

#define     CPU_DELAY_NS(n)	ndelay(n)

#define	    NANDC_MASTER_EN

void __iomem *nandc_base;
static u8 g_nandc_ver;

static u32 g_nandc_ecc_bits;
#ifdef NANDC_MASTER_EN
static struct MASTER_INFO_T master;
static u32 *g_master_temp_buf;
#endif

u8 nandc_get_version(void)
{
	return g_nandc_ver;
}

void nandc_init(void __iomem *nandc_addr)
{
	union FM_CTL_T ctl_reg;

	nandc_base = nandc_addr;

	ctl_reg.d32 = 0;
	g_nandc_ver = 6;
	if (nandc_readl(NANDC_V9_NANDC_VER) == RK3326_NANDC_VER)
		g_nandc_ver = 9;
	if (g_nandc_ver == 9) {
		ctl_reg.V9.wp = 1;
		ctl_reg.V9.sif_read_delay = 2;
		nandc_writel(ctl_reg.d32, NANDC_V9_FMCTL);
		nandc_writel(0, NANDC_V9_RANDMZ_CFG);
		nandc_writel(0x1041, NANDC_V9_FMWAIT);
	} else {
		ctl_reg.V6.wp = 1;
		nandc_writel(ctl_reg.d32, NANDC_FMCTL);
		nandc_writel(0, NANDC_RANDMZ_CFG);
		nandc_writel(0x1061, NANDC_FMWAIT);
	}
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
	if (g_nandc_ver == 9) {
		if (ns < 36)
			nandc_writel(0x1041, NANDC_V9_FMWAIT);
		else if (ns >= 100)
			nandc_writel(0x2082, NANDC_V9_FMWAIT);
		else
			nandc_writel(0x1061, NANDC_V9_FMWAIT);
	} else {
		if (ns < 36)
			nandc_writel(0x1061, NANDC_FMWAIT);
		else if (ns >= 100)
			nandc_writel(0x2082, NANDC_FMWAIT);
		else
			nandc_writel(0x1081, NANDC_FMWAIT);
	}
}

void nandc_bch_sel(u8 bits)
{
	union BCH_CTL_T tmp;
	union FL_CTL_T fl_reg;
	u8 bch_config;

	fl_reg.d32 = 0;
	fl_reg.V6.rst = 1;
	g_nandc_ecc_bits = bits;
	if (g_nandc_ver == 9) {
		nandc_writel(fl_reg.d32, NANDC_V9_FLCTL);
		if (bits == 70)
			bch_config = 0;
		else if (bits == 60)
			bch_config = 3;
		else if (bits == 40)
			bch_config = 2;
		else
			bch_config = 1;
		tmp.d32 = 0;
		tmp.V9.bchmode = bch_config;
		tmp.V9.bchrst = 1;
		nandc_writel(tmp.d32, NANDC_V9_BCHCTL);
	} else {
		nandc_writel(fl_reg.d32, NANDC_FLCTL);
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
}

/*
 *Nandc xfer data transmission
 *1. set bch register except nandc version equals 9
 *2. set internal transfer control register
 *3. set bus transfer
 *	a. target memory data address
 *	b. ahb setting
 *4. configure register orderly and start transmission
 */
static void nandc_xfer_start(u8 dir, u8 n_sec, u32 *data, u32 *spare)
{
	union BCH_CTL_T bch_reg;
	union FL_CTL_T fl_reg;
	u32 i;
	union MTRANS_CFG_T master_reg;
	u16 *p_spare_tmp = (u16 *)spare;

	fl_reg.d32 = 0;
	if (g_nandc_ver == 9) {
		fl_reg.V9.flash_rdn = dir;
		fl_reg.V9.bypass = 1;
		fl_reg.V9.tr_count = 1;
		fl_reg.V9.async_tog_mix = 1;
		fl_reg.V9.cor_able = 1;
		fl_reg.V9.st_addr = 0;
		fl_reg.V9.page_num = (n_sec + 1) / 2;
		/* dma start transfer data do care flash rdy */
		fl_reg.V9.flash_st_mod = 1;

		if (dir != 0) {
			for (i = 0; i < n_sec / 2; i++) {
				if (spare) {
					master.spare_buf[i] =
						(p_spare_tmp[0]) |
						((u32)p_spare_tmp[1] << 16);
					p_spare_tmp += 2;
				} else {
					master.spare_buf[i] = 0xffffffff;
				}
			}
		} else {
			master.spare_buf[0] = 1;
		}
		master.page_vir = (u32 *)((data == (u32 *)NULL) ?
					  master.page_buf :
					  (u32 *)data);
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
		nandc_writel(master.page_phy, NANDC_V9_MTRANS_SADDR0);
		nandc_writel(master.spare_phy, NANDC_V9_MTRANS_SADDR1);

		master_reg.d32 =  nandc_readl(NANDC_V9_MTRANS_CFG);
		master_reg.V9.incr_num = 16;
		master_reg.V9.burst = 7;
		master_reg.V9.hsize = 2;
		master_reg.V9.bus_mode = 1;
		master_reg.V9.ahb_wr = !dir;
		master_reg.V9.ahb_wr_st = 1;
		master_reg.V9.redundance_size = 0;

		nandc_writel(master_reg.d32, NANDC_V9_MTRANS_CFG);
		nandc_writel(fl_reg.d32, NANDC_V9_FLCTL);
		fl_reg.V9.flash_st = 1;
		nandc_writel(fl_reg.d32, NANDC_V9_FLCTL);
	} else {
		bch_reg.d32 = nandc_readl(NANDC_BCHCTL);
		bch_reg.V6.addr = 0x10;
		bch_reg.V6.power_down = 0;
		bch_reg.V6.region = 0;

		fl_reg.V6.rdn = dir;
		fl_reg.V6.dma = 1;
		fl_reg.V6.tr_count = 1;
		fl_reg.V6.async_tog_mix = 1;
		fl_reg.V6.cor_en = 1;
		fl_reg.V6.st_addr = 0;

		master_reg.d32 = nandc_readl(NANDC_MTRANS_CFG);
		master_reg.V6.bus_mode = 0;
		if (dir != 0) {
			u32 spare_sz = 64;

			for (i = 0; i < n_sec / 2; i++) {
				if (spare) {
					master.spare_buf[i * spare_sz / 4] =
					(p_spare_tmp[0]) |
					((u32)p_spare_tmp[1] << 16);
					p_spare_tmp += 2;
				} else {
					master.spare_buf[i * spare_sz / 4] =
					0xffffffff;
				}
			}
		}
		fl_reg.V6.page_num = (n_sec + 1) / 2;
		master.page_vir = (u32 *)((data == (u32 *)NULL) ?
					  master.page_buf :
					  (u32 *)data);
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
		master_reg.V6.hsize = 2;
		master_reg.V6.bus_mode = 1;
		master_reg.V6.ahb_wr = !dir;
		master_reg.V6.ahb_wr_st = 1;

		nandc_writel(master_reg.d32, NANDC_MTRANS_CFG);
		nandc_writel(bch_reg.d32, NANDC_BCHCTL);
		nandc_writel(fl_reg.d32, NANDC_FLCTL);
		fl_reg.V6.start = 1;
		nandc_writel(fl_reg.d32, NANDC_FLCTL);
	}
}

/*
 * Wait for the end of data transmission
 */
static void nandc_xfer_done(void)
{
	union FL_CTL_T fl_reg;
	union MTRANS_CFG_T master_reg;

	if (g_nandc_ver == 9) {
		union MTRANS_STAT_T stat_reg;

		master_reg.d32 = nandc_readl(NANDC_V9_MTRANS_CFG);
		if (master_reg.V9.ahb_wr != 0) {
			do {
				fl_reg.d32 = nandc_readl(NANDC_V9_FLCTL);
				stat_reg.d32 = nandc_readl(NANDC_V9_MTRANS_STAT);
				usleep_range(20, 30);
			} while (stat_reg.V9.mtrans_cnt < fl_reg.V9.page_num ||
				 fl_reg.V9.tr_rdy == 0);
			udelay(5);
			if (master.mapped) {
				rknandc_dma_unmap_single((u64)master.page_phy,
					fl_reg.V9.page_num * 1024,
					0);
				rknandc_dma_unmap_single((u64)master.spare_phy,
					fl_reg.V9.page_num * 64,
					0);
			}
		} else {
			do {
				fl_reg.d32 = nandc_readl(NANDC_V9_FLCTL);
				usleep_range(20, 30);
			} while (fl_reg.V9.tr_rdy == 0);
			if (master.mapped) {
				rknandc_dma_unmap_single((u64)master.page_phy,
					fl_reg.V9.page_num * 1024,
					1);
				rknandc_dma_unmap_single((u64)master.spare_phy,
					fl_reg.V9.page_num * 64,
					1);
			}
		}
	} else {
		master_reg.d32 = nandc_readl(NANDC_MTRANS_CFG);
		if (master_reg.V6.bus_mode != 0) {
			union MTRANS_STAT_T stat_reg;

		if (master_reg.V6.ahb_wr != 0) {
			do {
				fl_reg.d32 = nandc_readl(NANDC_FLCTL);
				stat_reg.d32 = nandc_readl(NANDC_MTRANS_STAT);
				usleep_range(20, 30);
			} while (stat_reg.V6.mtrans_cnt < fl_reg.V6.page_num ||
				 fl_reg.V6.tr_rdy == 0);

			if (master.mapped) {
				rknandc_dma_unmap_single(
					(unsigned long)(master.page_phy),
					fl_reg.V6.page_num * 1024,
					0);
				rknandc_dma_unmap_single(
					(unsigned long)(master.spare_phy),
					fl_reg.V6.page_num * 64,
					0);
			}
		} else {
			do {
				fl_reg.d32 = nandc_readl(NANDC_FLCTL);
				usleep_range(20, 30);
			} while (fl_reg.V6.tr_rdy == 0);
			if (master.mapped) {
				rknandc_dma_unmap_single(
					(unsigned long)(master.page_phy),
					fl_reg.V6.page_num * 1024, 1);
				rknandc_dma_unmap_single(
					(unsigned long)(master.spare_phy),
					fl_reg.V6.page_num * 64, 1);
			}
		}
		master.mapped = 0;
		} else {
			do {
				fl_reg.d32 = nandc_readl(NANDC_FLCTL);
			} while ((fl_reg.V6.tr_rdy == 0));
		}
	}
}

u32 nandc_xfer_data(u8 chip_sel, u8 dir, u8 n_sec,
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
	nandc_xfer_start(dir, n_sec, p_data, p_spare);
	nandc_xfer_done();
	if (dir == NANDC_READ) {
		if (g_nandc_ver == 9) {
			for (i = 0; i < n_sec / 4; i++) {
				bch_st_reg.d32 = nandc_readl(NANDC_V9_BCHST(i));
				if (n_sec > 2) {
					if (bch_st_reg.V9.fail0 || bch_st_reg.V9.fail1) {
						status = NAND_STS_ECC_ERR;
					} else {
						u32 tmp = max((u32)bch_st_reg.V9.err_bits0,
							      (u32)bch_st_reg.V9.err_bits1);
						status = max(tmp, status);
					}
				} else {
					if (bch_st_reg.V9.fail0)
						status = NAND_STS_ECC_ERR;
					else
						status = bch_st_reg.V9.err_bits0;
				}
			}
			if (p_spare) {
				for (i = 0; i < n_sec / 2; i++)
					p_spare[i] = master.spare_buf[i];
			}
		} else {
			for (i = 0; i < n_sec / 4 ; i++) {
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
			if (p_spare) {
				u32 spare_sz = 64;
				u32 temp_data;
				u8 *p_spare_temp = (u8 *)p_spare;

				for (i = 0; i < n_sec / 2; i++) {
					temp_data = master.spare_buf[i * spare_sz / 4];
					*p_spare_temp++ = (u8)temp_data;
					*p_spare_temp++ = (u8)(temp_data >> 8);
					*p_spare_temp++ = (u8)(temp_data >> 16);
					*p_spare_temp++ = (u8)(temp_data >> 24);
				}
			}
			nandc_writel(0, NANDC_MTRANS_CFG);
		}
	}
	return status;
}

void nandc_clean_irq(void)
{
}
