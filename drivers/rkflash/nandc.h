/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __NAND_H
#define __NAND_H

#include <linux/io.h>

#define nandc_writel(v, offs)	writel((v), (offs) + nandc_base)
#define nandc_readl(offs)	readl((offs) + nandc_base)

#define NANDC_READ	0
#define NANDC_WRITE	1
#define RK3326_NANDC_VER	0x56393030

/* INT ID */
enum NANDC_IRQ_NUM_T {
	NC_IRQ_DMA = 0,
	NC_IRQ_FRDY,
	NC_IRQ_BCHERR,
	NC_IRQ_BCHFAIL,
	NC_IRQ_LLP
};

enum ENUM_NANDC_BCH_CFG {
	NC_BCH_70 = 0,
	NC_BCH_24,
	NC_BCH_40,
	NC_BCH_60,
};

union FM_CTL_T {
	u32 d32;
	struct {
		unsigned cs : 8;		/* bits[0:7] */
		unsigned wp : 1;		/* bits[8] */
		unsigned rdy : 1;		/* bits[9] */
		unsigned fifo_empty : 1;	/* bits[10] */
		unsigned reserved11 : 1;	/* bits[11] */
		unsigned dwidth : 1;		/* bits[12] */
		unsigned tm : 1;		/* bits[13] */
		unsigned onficlk_en : 1;	/* bits[14] */
		unsigned toggle_en : 1;		/* bits[15] */
		unsigned flash_abort_en : 1;	/* bits[16] */
		unsigned flash_abort_clear : 1;	/* bits[17] */
		unsigned reserved18_23 : 6;	/* bits[18:23] */
		unsigned read_delay : 3;	/* bits[24:26] */
		unsigned reserved27_31 : 5;	/* bits[27:31] */
	} V6;
	struct	{
		unsigned cs : 8;
		unsigned wp : 1;
		unsigned frdy : 1;
		unsigned fifo_empth_flash : 1;
		unsigned reserved11_12 : 2;
		unsigned tm : 1;
		unsigned syn_clken : 1;
		unsigned syn_mode : 1;
		unsigned flash_abort_en : 1;
		unsigned flash_abort_clear : 1;
		unsigned sif_read_delay : 3;
		unsigned io_mux : 3;
		unsigned reserved24_31 : 8;
	} V9;
};

union FM_WAIT_T {
	u32 d32;
	struct {
		unsigned csrw : 5;
		unsigned rwpw : 6;
		unsigned rdy : 1;
		unsigned rwcs : 6;
		unsigned reserved18_23 : 6;
		unsigned fmw_dly : 6;
		unsigned fmw_dly_en : 1;
		unsigned reserved31_31 : 1;
	} V6;
	struct {
		unsigned rwcs : 5;
		unsigned rwpw : 6;
		unsigned hard_rdy : 1;
		unsigned csrw : 6;
		unsigned wait_frdy_dly : 5;
		unsigned reserved23_23 : 1;
		unsigned fmw_dly : 6;
		unsigned fmw_dly_en : 1;
		unsigned reserved31_31 : 1;
	} V9;
};

union FL_CTL_T {
	u32 d32;
	struct {
		unsigned rst : 1;
		unsigned rdn : 1;
		unsigned start : 1;
		unsigned dma : 1;
		unsigned st_addr : 1;
		unsigned tr_count : 2;
		unsigned rdy_ignore : 1;
		/* unsigned int_clr : 1; */
		/* unsigned int_en : 1; */
		unsigned reserved8_9 : 2;
		unsigned cor_en : 1;
		unsigned lba_en : 1;
		unsigned spare_size : 7;
		unsigned reserved19 : 1;
		unsigned tr_rdy : 1;
		unsigned page_size : 1;
		unsigned page_num : 6;
		unsigned low_power : 1;
		unsigned async_tog_mix : 1;
		unsigned reserved30_31 : 2;
	} V6;
	struct {
		unsigned flash_rst : 1;
		unsigned flash_rdn : 1;
		unsigned flash_st : 1;
		unsigned bypass : 1;
		unsigned st_addr : 1;
		unsigned tr_count : 2;
		unsigned flash_st_mod : 1;
		unsigned not_tran_data : 1;
		unsigned tran_seed : 1;
		unsigned cor_able : 1;
		unsigned lba_en : 1;
		unsigned lba_spare_sel : 1;
		unsigned reserved13_18 : 6;
		unsigned bchst_trans : 1;
		unsigned tr_rdy : 1;
		unsigned page_size : 1;
		unsigned page_num : 6;
		unsigned low_power : 1;
		unsigned async_tog_mix : 1;
		unsigned bypass_fifo_mode : 1;
		unsigned reserved31_31 : 1;
	} V9;
};

union BCH_CTL_T {
	u32 d32;
	struct {
		unsigned rst : 1;
		unsigned reserved : 1;
		unsigned addr_not_care : 1;
		unsigned power_down : 1;
		unsigned bch_mode : 1;	   /* 0-16bit/1KB, 1-24bit/1KB */
		unsigned region : 3;
		unsigned addr : 8;
		unsigned bchpage : 1;
		unsigned reserved17 : 1;
		unsigned bch_mode1 : 1;
		unsigned thres : 8;
		unsigned reserved27_31 : 5;
	} V6;
	struct {
		unsigned bchrst : 1;
		unsigned wcnt_clear : 1;
		unsigned reserved2 : 1;
		unsigned bchepd : 1;
		unsigned reserved4_15 : 12;
		unsigned bchpage : 1;
		unsigned bchthre : 8;
		unsigned bchmode : 3;
		unsigned reserved28_31 : 4;
	} V9;
};

union BCH_ST_T {
	u32 d32;
	struct {
		unsigned errf0 : 1;
		unsigned done0 : 1;
		unsigned fail0 : 1;
		unsigned err_bits0 : 5;
		unsigned err_bits_low0 : 5;
		unsigned errf1 : 1;
		unsigned done1 : 1;
		unsigned fail1 : 1;
		unsigned err_bits1 : 5;
		unsigned err_bits_low1 : 5;
		unsigned rdy : 1;
		/* unsigned cnt : 1; */
		unsigned err_bits0_5 : 1;
		unsigned err_bits_low0_5 : 1;
		unsigned err_bits1_5 : 1;
		unsigned err_bits_low1_5 : 1;
		unsigned reserved31_31 : 1;
	} V6;
	struct {
		unsigned errf0 : 1;
		unsigned done0 : 1;
		unsigned fail0 : 1;
		unsigned err_bits0 : 7;
		unsigned all_f_flag0 : 1;
		unsigned reserved11_15 : 5;
		unsigned errf1 : 1;
		unsigned done1 : 1;
		unsigned fail1 : 1;
		unsigned err_bits1 : 7;
		unsigned all_f_flag1 : 1;
		unsigned reserved27_30 : 4;
		unsigned bch_ready_flag: 1;
	} V9;
};

union MTRANS_CFG_T {
	u32 d32;
	struct {
		unsigned ahb_wr_st : 1;
		unsigned ahb_wr : 1;
		unsigned bus_mode : 1;
		unsigned hsize : 3;
		unsigned burst : 3;
		unsigned incr_num : 5;
		unsigned fl_pwd : 1;
		unsigned ahb_rst : 1;
		unsigned reserved16_31 : 16;
	} V6;
	struct {
		unsigned ahb_wr_st : 1;
		unsigned ahb_wr : 1;
		unsigned bus_mode : 1;
		unsigned hsize : 3;
		unsigned burst : 3;
		unsigned incr_num : 5;
		unsigned fl_pwd : 1;
		unsigned ahb_rst : 1;
		unsigned redundance_size : 11;
		unsigned reserved27_31 : 5;
	} V9;
};

union MTRANS_STAT_T {
	u32 d32;
	struct {
		unsigned bus_err : 16;
		unsigned mtrans_cnt : 5;
		unsigned reserved21_31 : 11;
	} V6;
	struct {
		unsigned bus_err : 16;
		unsigned mtrans_cnt : 6;
		unsigned reserved22_31 : 10;
	} V9;
};

/* NANDC Registers */
#define NANDC_FMCTL		0x0
#define NANDC_FMWAIT		0x4
#define NANDC_FLCTL		0x8
#define NANDC_BCHCTL		0xc
#define NANDC_MTRANS_CFG	0x10
#define NANDC_MTRANS_SADDR0	0x14
#define NANDC_MTRANS_SADDR1	0x18
#define NANDC_MTRANS_STAT	0x1c
#define NANDC_DLL_CTL_REG0	0x130
#define NANDC_DLL_CTL_REG1	0x134
#define NANDC_DLL_OBS_REG0	0x138
#define NANDC_RANDMZ_CFG	0x150
#define NANDC_EBI_EN		0x154
#define NANDC_FMWAIT_SYN	0x158
#define NANDC_MTRANS_STAT2	0x15c
#define NANDC_NANDC_VER		0x160
#define NANDC_LLP_CTL		0x164
#define NANDC_LLP_STAT		0x168
#define NANDC_INTEN		0x16c
#define NANDC_INTCLR		0x170
#define NANDC_INTST		0x174
#define NANDC_SPARE0		0x200
#define NANDC_SPARE1		0x230

#define NANDC_BCHST(i)		({		\
	u32 x = (i);				\
	4 * x + x < 8 ? 0x20 : 0x520; })

#define NANDC_CHIP_DATA(id)	(0x800 + (id) * 0x100)
#define NANDC_CHIP_ADDR(id)	(0x800 + (id) * 0x100 + 0x4)
#define NANDC_CHIP_CMD(id)	(0x800 + (id) * 0x100 + 0x8)

#define NANDC_V9_FMCTL		0x0
#define NANDC_V9_FMWAIT		0x4
#define NANDC_V9_FLCTL		0x10
#define NANDC_V9_BCHCTL		0x20
#define NANDC_V9_MTRANS_CFG	0x30
#define NANDC_V9_MTRANS_SADDR0	0x34
#define NANDC_V9_MTRANS_SADDR1	0x38
#define NANDC_V9_MTRANS_STAT	0x40
#define NANDC_V9_MTRANS_STAT2	0x44
#define NANDC_V9_NANDC_VER	0x80

#define NANDC_V9_INTEN		0x120
#define NANDC_V9_INTCLR		0x124
#define NANDC_V9_INTST		0x128
#define NANDC_V9_SPARE0		0x200
#define NANDC_V9_SPARE1		0x204
#define NANDC_V9_RANDMZ_CFG	0x208
#define NANDC_V9_BCHST(i)	(0x150 + (i) * 4)

#define NANDC_V9_CHIP_DATA(id)	(0x800 + (id) * 0x100)
#define NANDC_V9_CHIP_ADDR(id)	(0x800 + (id) * 0x100 + 0x4)
#define NANDC_V9_CHIP_CMD(id)	(0x800 + (id) * 0x100 + 0x8)

struct MASTER_INFO_T {
	u32  *page_buf;		/* [DATA_LEN]; */
	u32  *spare_buf;	/* [DATA_LEN / (1024/128)]; */
	u32  *page_vir;	/* page_buf_vir_addr */
	u32  *spare_vir;	/* spare_buf_vir_addr */
	u32  page_phy;		/* page_buf_phy_addr */
	u32  spare_phy;	/* spare_buf_phy_addr*/
	u32  mapped;
	u32  cnt;
};

struct CHIP_MAP_INFO_T {
	u32  *nandc_addr;
	u32  chip_num;
};

unsigned long rknandc_dma_map_single(unsigned long ptr,
				     int size,
				     int dir);
void rknandc_dma_unmap_single(unsigned long ptr,
			      int size,
			      int dir);

void nandc_init(void __iomem *nandc_addr);
void nandc_flash_cs(u8 chip_sel);
void nandc_flash_de_cs(u8 chip_sel);
u32 nandc_wait_flash_ready(u8 chip_sel);
u32 nandc_delayns(u32 count);
u32 nandc_xfer_data(u8 chip_sel,
		    u8 dir,
		    u8 sector_count,
		    u32 *p_data,
		    u32 *p_spare);
void nandc_randmz_sel(u8 chip_sel, u32 randmz_seed);
void nandc_bch_sel(u8 bits);
void nandc_read_not_case_busy_en(u8 en);
void nandc_time_cfg(u32 ns);
void nandc_clean_irq(void);
u8 nandc_get_version(void);

#endif
