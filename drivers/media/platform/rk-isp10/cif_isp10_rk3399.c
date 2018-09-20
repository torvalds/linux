/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/platform_data/rk_isp10_platform.h>

#define VI_IRCL               0x0014
#define MRV_MIPI_BASE         0x1C00
#define MRV_MIPI_CTRL         0x00
/*
 * GRF_IO_VSEL
 */
#define GRF_IO_VSEL_OFFSET    (0x0900)
#define DVP_V18SEL            ((1 << 1) | (1 << 17))
#define DVP_V33SEL            ((0 << 1) | (1 << 17))
/*
 * GRF_IO_VSEL
 */
#define GRF_GPIO2B_E_OFFSET   (0x0204)
#define CIF_CLKOUT_STRENGTH(a)	\
			((((a) & 0x03) << 3) | (0x03 << 19))
#define GRF_SOC_STATUS1       (0x0e2a4)

#define GRF_SOC_CON9_OFFSET   (0x6224)
#define DPHY_RX0_TURNREQUEST_MASK     (0xF << 16)
#define DPHY_RX0_TURNREQUEST_BIT      (0)

#define GRF_SOC_CON21_OFFSET          (0x6254)
#define DPHY_RX0_FORCERXMODE_MASK     (0xF << 20)
#define DPHY_RX0_FORCERXMODE_BIT      (4)
#define DPHY_RX0_FORCETXSTOPMODE_MASK (0xF << 24)
#define DPHY_RX0_FORCETXSTOPMODE_BIT  (8)
#define DPHY_RX0_TURNDISABLE_MASK     (0xF << 28)
#define DPHY_RX0_TURNDISABLE_BIT      (12)
#define DPHY_RX0_ENABLE_MASK          (0xF << 16)
#define DPHY_RX0_ENABLE_BIT           (0)

#define GRF_SOC_CON23_OFFSET          (0x625c)
#define DPHY_TX1RX1_TURNDISABLE_MASK  (0xF << 28)
#define DPHY_TX1RX1_TURNDISABLE_BIT   (12)
#define DPHY_TX1RX1_FORCERXMODE_MASK  (0xF << 20)
#define DPHY_TX1RX1_FORCERXMODE_BIT   (4)
#define DPHY_TX1RX1_FORCETXSTOPMODE_MASK   (0xF << 24)
#define DPHY_TX1RX1_FORCETXSTOPMODE_BIT    (8)
#define DPHY_TX1RX1_ENABLE_MASK            (0xF << 16)
#define DPHY_TX1RX1_ENABLE_BIT             (0)

#define GRF_SOC_CON24_OFFSET               (0x6260)
#define DPHY_TX1RX1_MASTERSLAVEZ_MASK      (0x1 << 23)
#define DPHY_TX1RX1_MASTERSLAVEZ_BIT       (7)
#define DPHY_TX1RX1_BASEDIR_MASK           (0x1 << 21)
#define DPHY_TX1RX1_BASEDIR_BIT            (5)
#define DPHY_RX1_MASK                      (0x1 << 20)
#define DPHY_RX1_SEL_BIT                   (4)

#define GRF_SOC_CON25_OFFSET               (0x6264)
#define DPHY_RX0_TESTCLK_MASK              (0x1 << 25)
#define DPHY_RX0_TESTCLK_BIT               (9)
#define DPHY_RX0_TESTCLR_MASK              (0x1 << 26)
#define DPHY_RX0_TESTCLR_BIT               (10)
#define DPHY_RX0_TESTDIN_MASK              (0xFF << 16)
#define DPHY_RX0_TESTDIN_BIT               (0)
#define DPHY_RX0_TESTEN_MASK               (0x1 << 24)
#define DPHY_RX0_TESTEN_BIT                (8)

#define DPHY_TX1RX1_TURNREQUEST_MASK       (0xF << 16)
#define DPHY_TX1RX1_TURNREQUEST_BIT        (0)

#define DSIHOST_PHY_SHUTDOWNZ              (0x00a0)
#define DSIHOST_DPHY_RSTZ                  (0x00a0)
#define DSIHOST_PHY_TEST_CTRL0             (0x00b4)
#define DSIHOST_PHY_TEST_CTRL1             (0x00b8)

#define write_cifisp_reg(addr, val)	\
		__raw_writel(val, (addr) + isp_cfg->isp_base)
#define read_cifisp_reg(addr)	\
		__raw_readl((addr) + isp_cfg->isp_base)

#define write_grf_reg(addr, val)	\
		regmap_write(isp_cfg->regmap_grf, addr, val)
#define read_grf_reg(addr, val)	\
		regmap_read(isp_cfg->regmap_grf, addr, val)

#define write_dsihost_reg(addr, val)	\
		__raw_writel(val, (addr) + isp_cfg->dsihost_base)
#define read_dsihost_reg(addr)	\
		__raw_readl((addr) + isp_cfg->dsihost_base)

enum cif_isp10_isp_idx {
	CIF_ISP10_ISP0 = 0,
	CIF_ISP10_ISP1 = 1
};

struct cif_isp10_clk_rst_rk3399 {
	struct clk      *hclk_isp0_noc;
	struct clk      *hclk_isp0_wrapper;
	struct clk      *hclk_isp1_noc;
	struct clk      *hclk_isp1_wrapper;
	struct clk      *aclk_isp0_noc;
	struct clk      *aclk_isp0_wrapper;
	struct clk      *aclk_isp1_noc;
	struct clk      *aclk_isp1_wrapper;
	struct clk      *clk_isp0;
	struct clk      *clk_isp1;
	struct clk      *pclkin_isp1;
	struct clk      *pclk_dphy_ref;
	struct clk      *pclk_dphytxrx;
	struct clk      *pclk_dphyrx;
	struct clk      *cif_clk_out;
	struct clk      *cif_clk_pll;
	struct clk      *cif_clk_mipi_dsi;
	struct clk      *cif_clk_mipi_dphy_cfg;
};

struct cif_isp10_rk3399 {
	struct regmap *regmap_grf;
	void __iomem *dsihost_base;
	void __iomem *isp_base;
	struct cif_isp10_clk_rst_rk3399 clk_rst;
	struct cif_isp10_device *cif_isp10;
	enum cif_isp10_isp_idx isp_idx;
};

struct mipi_dphy_hsfreqrange {
	unsigned int range_l;
	unsigned int range_h;
	unsigned char cfg_bit;
};

static struct mipi_dphy_hsfreqrange mipi_dphy_hsfreq_range[] = {
	{80, 90, 0x00},
	{90, 100, 0x10},
	{100, 110, 0x20},
	{110, 130, 0x01},
	{130, 140, 0x11},
	{140, 150, 0x21},
	{150, 170, 0x02},
	{170, 180, 0x12},
	{180, 200, 0x22},
	{200, 220, 0x03},
	{220, 240, 0x13},
	{240, 250, 0x23},
	{250, 270, 0x4},
	{270, 300, 0x14},
	{300, 330, 0x5},
	{330, 360, 0x15},
	{360, 400, 0x25},
	{400, 450, 0x06},
	{450, 500, 0x16},
	{500, 550, 0x07},
	{550, 600, 0x17},
	{600, 650, 0x08},
	{650, 700, 0x18},
	{700, 750, 0x09},
	{750, 800, 0x19},
	{800, 850, 0x29},
	{850, 900, 0x39},
	{900, 950, 0x0a},
	{950, 1000, 0x1a},
	{1000, 1050, 0x2a},
	{1100, 1150, 0x3a},
	{1150, 1200, 0x0b},
	{1200, 1250, 0x1b},
	{1250, 1300, 0x2b},
	{1300, 1350, 0x0c},
	{1350, 1400, 0x1c},
	{1400, 1450, 0x2c},
	{1450, 1500, 0x3c}
};

static int mipi_dphy0_wr_reg(struct cif_isp10_rk3399 *isp_cfg, unsigned char addr, unsigned char data)
{
	/*
	 * TESTCLK=1
	 * TESTEN =1,TESTDIN=addr
	 * TESTCLK=0
	 */
	write_grf_reg(GRF_SOC_CON25_OFFSET,
		DPHY_RX0_TESTCLK_MASK | (1 << DPHY_RX0_TESTCLK_BIT));
	write_grf_reg(GRF_SOC_CON25_OFFSET,
		((addr << DPHY_RX0_TESTDIN_BIT) | DPHY_RX0_TESTDIN_MASK
		 | (1 << DPHY_RX0_TESTEN_BIT) | DPHY_RX0_TESTEN_MASK));
	write_grf_reg(GRF_SOC_CON25_OFFSET, DPHY_RX0_TESTCLK_MASK);

	/*
	 * write data:
	 * TESTEN =0,TESTDIN=data
	 * TESTCLK=1
	 */
	if (data != 0xff) {
		write_grf_reg(GRF_SOC_CON25_OFFSET,
			((data << DPHY_RX0_TESTDIN_BIT) |
			DPHY_RX0_TESTDIN_MASK | DPHY_RX0_TESTEN_MASK));
		write_grf_reg(GRF_SOC_CON25_OFFSET,
			DPHY_RX0_TESTCLK_MASK | (1 << DPHY_RX0_TESTCLK_BIT));
	}
	return 0;
}

static int mipi_dphy0_rd_reg(struct cif_isp10_rk3399 *isp_cfg, unsigned char addr)
{
	int val = 0;
	/*TESTCLK=1*/
	write_grf_reg(GRF_SOC_CON25_OFFSET, DPHY_RX0_TESTCLK_MASK |
				(1 << DPHY_RX0_TESTCLK_BIT));
	/*TESTEN =1,TESTDIN=addr*/
	write_grf_reg(GRF_SOC_CON25_OFFSET,
				((addr << DPHY_RX0_TESTDIN_BIT) |
				DPHY_RX0_TESTDIN_MASK |
				(1 << DPHY_RX0_TESTEN_BIT) |
				DPHY_RX0_TESTEN_MASK));
	/*TESTCLK=0*/
	write_grf_reg(GRF_SOC_CON25_OFFSET, DPHY_RX0_TESTCLK_MASK);
	read_grf_reg(GRF_SOC_STATUS1, &val);
	return val & 0xff;
}

static int mipi_dphy1_wr_reg(struct cif_isp10_rk3399 *isp_cfg, unsigned char addr, unsigned char data)
{
	/*
	 * TESTEN =1,TESTDIN=addr
	 * TESTCLK=0
	 * TESTEN =0,TESTDIN=data
	 * TESTCLK=1
	 */
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL1, (0x00010000 | addr));
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000000);
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL1, (0x00000000 | data));
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000002);

	return 0;
}

static int mipi_dphy1_rd_reg(struct cif_isp10_rk3399 *isp_cfg, unsigned char addr)
{
	/* TESTEN =1,TESTDIN=addr */
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL1, (0x00010000 | addr));
	/* TESTCLK=0 */
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000000);
	return ((read_dsihost_reg(DSIHOST_PHY_TEST_CTRL1) & 0xff00) >> 8);
}

static int mipi_dphy_cfg(struct cif_isp10_rk3399 *isp_cfg, struct pltfrm_cam_mipi_config *para)
{
	unsigned char hsfreqrange = 0xff, i;
	struct mipi_dphy_hsfreqrange *hsfreqrange_p;
	unsigned char datalane_en, input_sel;

	hsfreqrange_p = mipi_dphy_hsfreq_range;
	for (i = 0;
		i < (sizeof(mipi_dphy_hsfreq_range) /
		sizeof(struct mipi_dphy_hsfreqrange));
		i++) {
		if ((para->bit_rate > hsfreqrange_p->range_l) &&
			 (para->bit_rate <= hsfreqrange_p->range_h)) {
			hsfreqrange = hsfreqrange_p->cfg_bit;
			break;
		}
		hsfreqrange_p++;
	}

	if (hsfreqrange == 0xff)
		hsfreqrange = 0x00;

	hsfreqrange <<= 1;

	input_sel = para->dphy_index;
	datalane_en = 0;
	for (i = 0; i < para->nb_lanes; i++)
		datalane_en |= (1 << i);

	if (input_sel == 0) {
		/*
		 * According to the sequence of RK3399_TXRX_DPHY, the setting of isp0 mipi
		 * will affect txrx dphy in default state of grf_soc_con24.
		 */
		write_grf_reg(GRF_SOC_CON24_OFFSET,
			DPHY_TX1RX1_MASTERSLAVEZ_MASK |
			(0x0 << DPHY_TX1RX1_MASTERSLAVEZ_BIT) |
			DPHY_TX1RX1_BASEDIR_MASK |
			(0x1 << DPHY_TX1RX1_BASEDIR_BIT) |
			DPHY_RX1_MASK | 0x0 << DPHY_RX1_SEL_BIT);

		write_grf_reg(GRF_SOC_CON21_OFFSET,
			DPHY_RX0_FORCERXMODE_MASK |
			(0x0 << DPHY_RX0_FORCERXMODE_BIT) |
			DPHY_RX0_FORCETXSTOPMODE_MASK |
			(0x0 << DPHY_RX0_FORCETXSTOPMODE_BIT));

		/* set lane num */
		write_grf_reg(GRF_SOC_CON21_OFFSET,
			DPHY_RX0_ENABLE_MASK |
			(datalane_en << DPHY_RX0_ENABLE_BIT));

		/* set lan turndisab as 1 */
		write_grf_reg(GRF_SOC_CON21_OFFSET,
			DPHY_RX0_TURNDISABLE_MASK |
			(0xf << DPHY_RX0_TURNDISABLE_BIT));
		write_grf_reg(GRF_SOC_CON21_OFFSET, (0x0 << 4) | (0xf << 20));

		/* set lan turnrequest as 0 */
		write_grf_reg(GRF_SOC_CON9_OFFSET,
			DPHY_RX0_TURNREQUEST_MASK |
			(0x0 << DPHY_RX0_TURNREQUEST_BIT));

		/* phy start */
		/*
		 * TESTCLK=1
		 * TESTCLR=1
		 * delay 100us
		 * TESTCLR=0
		 */
		write_grf_reg(GRF_SOC_CON25_OFFSET,
			DPHY_RX0_TESTCLK_MASK |
			(0x1 << DPHY_RX0_TESTCLK_BIT)); /* TESTCLK=1 */
		write_grf_reg(GRF_SOC_CON25_OFFSET,
			DPHY_RX0_TESTCLR_MASK |
			(0x1 << DPHY_RX0_TESTCLR_BIT));   /* TESTCLR=1 */
		usleep_range(100, 150);
		/* TESTCLR=0  zyc */
		write_grf_reg(GRF_SOC_CON25_OFFSET,
			DPHY_RX0_TESTCLR_MASK);
		usleep_range(100, 150);

		/* set clock lane */
		mipi_dphy0_wr_reg
			(isp_cfg, 0x34, 0);
		/* HS hsfreqrange & lane 0  settle bypass */
		mipi_dphy0_wr_reg(isp_cfg, 0x44, hsfreqrange);
		mipi_dphy0_wr_reg(isp_cfg, 0x54, 0);
		mipi_dphy0_wr_reg(isp_cfg, 0x84, 0);
		mipi_dphy0_wr_reg(isp_cfg, 0x94, 0);
		mipi_dphy0_wr_reg(isp_cfg, 0x75, 0x04);
		mipi_dphy0_rd_reg(isp_cfg, 0x75);

		/* Normal operation */
		/*
		 * TESTCLK=1
		 * TESTEN =0
		 */
		mipi_dphy0_wr_reg(isp_cfg, 0x0, -1);
		write_grf_reg(GRF_SOC_CON25_OFFSET,
			DPHY_RX0_TESTCLK_MASK | (1 << DPHY_RX0_TESTCLK_BIT));
		write_grf_reg(GRF_SOC_CON25_OFFSET,
			(DPHY_RX0_TESTEN_MASK));

		write_cifisp_reg((MRV_MIPI_BASE + MRV_MIPI_CTRL),
			read_cifisp_reg(MRV_MIPI_BASE + MRV_MIPI_CTRL)
			| (0x0f << 8));

	} else if (input_sel == 1) {
		write_grf_reg(GRF_SOC_CON23_OFFSET,
			DPHY_RX0_FORCERXMODE_MASK |
			(0x0 << DPHY_RX0_FORCERXMODE_BIT) |
			DPHY_RX0_FORCETXSTOPMODE_MASK |
			(0x0 << DPHY_RX0_FORCETXSTOPMODE_BIT));
		write_grf_reg(GRF_SOC_CON24_OFFSET,
			DPHY_TX1RX1_MASTERSLAVEZ_MASK |
			(0x0 << DPHY_TX1RX1_MASTERSLAVEZ_BIT) |
			DPHY_TX1RX1_BASEDIR_MASK |
			(0x1 << DPHY_TX1RX1_BASEDIR_BIT) |
			DPHY_RX1_MASK | 0x0 << DPHY_RX1_SEL_BIT);

		/* set lane num */
		write_grf_reg(GRF_SOC_CON23_OFFSET,
			DPHY_TX1RX1_ENABLE_MASK |
			(datalane_en << DPHY_TX1RX1_ENABLE_BIT));

		/* set lan turndisab as 1 */
		write_grf_reg(GRF_SOC_CON23_OFFSET,
			DPHY_TX1RX1_TURNDISABLE_MASK |
			(0xf << DPHY_TX1RX1_TURNDISABLE_BIT));
		write_grf_reg(GRF_SOC_CON23_OFFSET, (0x0 << 4) | (0xf << 20));

		/* set lan turnrequest as 0   */
		write_grf_reg(GRF_SOC_CON24_OFFSET,
			DPHY_TX1RX1_TURNREQUEST_MASK |
			(0x0 << DPHY_TX1RX1_TURNREQUEST_BIT));

		/* phy1 start */
		/*
		 * SHUTDOWNZ=0
		 * RSTZ=0
		 * TESTCLK=1
		 * TESTCLR=1 TESTCLK=1
		 * TESTCLR=0 TESTCLK=1
		 */
		write_dsihost_reg(DSIHOST_PHY_SHUTDOWNZ, 0x00000000);
		write_dsihost_reg(DSIHOST_DPHY_RSTZ, 0x00000000);
		write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000002);
		write_dsihost_reg(DSIHOST_PHY_TEST_CTRL1, 0x00000003);
		usleep_range(100, 150);
		write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000002);
		usleep_range(100, 150);

		/* set clock lane */
		mipi_dphy1_wr_reg(isp_cfg, 0x34, 0x00);
		mipi_dphy1_wr_reg(isp_cfg, 0x44, hsfreqrange);
		mipi_dphy1_wr_reg(isp_cfg, 0x54, 0);
		mipi_dphy1_wr_reg(isp_cfg, 0x84, 0);
		mipi_dphy1_wr_reg(isp_cfg, 0x94, 0);
		mipi_dphy1_wr_reg(isp_cfg, 0x75, 0x04);

		mipi_dphy1_rd_reg(isp_cfg, 0x0);
		/*
		 * TESTCLK=1
		 * TESTEN =0
		 * SHUTDOWNZ=1
		 * RSTZ=1
		 */
		write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000002);
		write_dsihost_reg(DSIHOST_PHY_TEST_CTRL1, 0x00000000);
		/*SHUTDOWNZ=1,  RSTZ=1*/
		write_dsihost_reg(DSIHOST_DPHY_RSTZ, 0x00000003);

		write_cifisp_reg((MRV_MIPI_BASE + MRV_MIPI_CTRL),
			read_cifisp_reg(MRV_MIPI_BASE + MRV_MIPI_CTRL)
			| (0x0f << 8));
	} else {
		goto fail;
	}

	return 0;
fail:
	return -1;
}

static int soc_clk_enable(struct cif_isp10_rk3399 *isp_cfg)
{
	struct cif_isp10_clk_rst_rk3399 *clk_rst = &isp_cfg->clk_rst;

	if (isp_cfg->isp_idx == CIF_ISP10_ISP0) {
		clk_prepare_enable(clk_rst->hclk_isp0_noc);
		clk_prepare_enable(clk_rst->hclk_isp0_wrapper);
		clk_prepare_enable(clk_rst->aclk_isp0_noc);
		clk_prepare_enable(clk_rst->aclk_isp0_wrapper);
		clk_prepare_enable(clk_rst->clk_isp0);
		clk_prepare_enable(clk_rst->pclk_dphyrx);
		clk_prepare_enable(clk_rst->cif_clk_out);
		clk_prepare_enable(clk_rst->pclk_dphy_ref);
	} else {
		clk_prepare_enable(clk_rst->hclk_isp1_noc);
		clk_prepare_enable(clk_rst->hclk_isp1_wrapper);
		clk_prepare_enable(clk_rst->aclk_isp1_noc);
		clk_prepare_enable(clk_rst->aclk_isp1_wrapper);
		clk_prepare_enable(clk_rst->clk_isp1);
		clk_prepare_enable(clk_rst->pclkin_isp1);
		clk_prepare_enable(clk_rst->pclk_dphytxrx);
		clk_prepare_enable(clk_rst->cif_clk_mipi_dsi);
		clk_prepare_enable(clk_rst->cif_clk_mipi_dphy_cfg);
		clk_prepare_enable(clk_rst->cif_clk_out);
		clk_prepare_enable(clk_rst->pclk_dphy_ref);
	}

	return 0;
}

static int soc_clk_disable(struct cif_isp10_rk3399 *isp_cfg)
{
	struct cif_isp10_clk_rst_rk3399 *clk_rst = &isp_cfg->clk_rst;

	if (isp_cfg->isp_idx == CIF_ISP10_ISP0) {
		clk_disable_unprepare(clk_rst->hclk_isp0_noc);
		clk_disable_unprepare(clk_rst->hclk_isp0_wrapper);
		clk_disable_unprepare(clk_rst->aclk_isp0_noc);
		clk_disable_unprepare(clk_rst->aclk_isp0_wrapper);
		clk_disable_unprepare(clk_rst->clk_isp0);
		clk_disable_unprepare(clk_rst->pclk_dphyrx);
		if (!IS_ERR_OR_NULL(clk_rst->cif_clk_pll))
			clk_set_parent(clk_rst->cif_clk_out,
				clk_rst->cif_clk_pll);
		clk_disable_unprepare(clk_rst->cif_clk_out);
		clk_disable_unprepare(clk_rst->pclk_dphy_ref);
	} else {
		clk_disable_unprepare(clk_rst->hclk_isp1_noc);
		clk_disable_unprepare(clk_rst->hclk_isp1_wrapper);
		clk_disable_unprepare(clk_rst->aclk_isp1_noc);
		clk_disable_unprepare(clk_rst->aclk_isp1_wrapper);
		clk_disable_unprepare(clk_rst->clk_isp1);
		clk_disable_unprepare(clk_rst->pclkin_isp1);
		clk_disable_unprepare(clk_rst->pclk_dphytxrx);
		clk_disable_unprepare(clk_rst->cif_clk_mipi_dsi);
		clk_disable_unprepare(clk_rst->cif_clk_mipi_dphy_cfg);
		if (!IS_ERR_OR_NULL(clk_rst->cif_clk_pll))
			clk_set_parent(clk_rst->cif_clk_out,
				clk_rst->cif_clk_pll);
		clk_disable_unprepare(clk_rst->cif_clk_out);
		clk_disable_unprepare(clk_rst->pclk_dphy_ref);
	}

	return 0;
}

static int soc_init(struct cif_isp10_rk3399 **isp_cfg, struct pltfrm_soc_init_para *init)
{
	struct cif_isp10_clk_rst_rk3399 *clk_rst;
	struct platform_device *pdev = init->pdev;
	struct device_node *np = pdev->dev.of_node, *node;
	struct resource *res;
	struct cif_isp10_rk3399 *isp_cfg_tmp;
	int err;

	*isp_cfg = NULL;

	isp_cfg_tmp = (struct cif_isp10_rk3399 *)devm_kzalloc(
				&pdev->dev,
				sizeof(struct cif_isp10_rk3399),
				GFP_KERNEL);
	if (!isp_cfg_tmp) {
		dev_err(&pdev->dev, "Can't allocate cif_isp10_rk3399\n");
		err = -ENOMEM;
		goto alloc_failed;
	}

	node = of_parse_phandle(np, "rockchip,grf", 0);
	if (node) {
		isp_cfg_tmp->regmap_grf = syscon_node_to_regmap(node);
		if (IS_ERR(isp_cfg_tmp->regmap_grf)) {
			dev_err(&pdev->dev, "Can't allocate cif_isp10_rk3399\n");
			err = -ENODEV;
			goto regmap_failed;
		}
	}

	clk_rst = &isp_cfg_tmp->clk_rst;

	if (strcmp(pdev->name, "ff910000.cif_isp") == 0) {
		clk_rst->hclk_isp0_noc	   =
			devm_clk_get(&pdev->dev, "hclk_isp0_noc");
		clk_rst->hclk_isp0_wrapper =
			devm_clk_get(&pdev->dev, "hclk_isp0_wrapper");
		clk_rst->aclk_isp0_noc	   =
			devm_clk_get(&pdev->dev, "aclk_isp0_noc");
		clk_rst->aclk_isp0_wrapper =
			devm_clk_get(&pdev->dev, "aclk_isp0_wrapper");
		clk_rst->clk_isp0		   =
			devm_clk_get(&pdev->dev, "clk_isp0");
		clk_rst->pclk_dphyrx	   =
			devm_clk_get(&pdev->dev, "pclk_dphyrx");
		clk_rst->cif_clk_out	   =
			devm_clk_get(&pdev->dev, "clk_cif_out");
		clk_rst->cif_clk_pll	   =
			devm_clk_get(&pdev->dev, "clk_cif_pll");
		clk_rst->pclk_dphy_ref	   =
			devm_clk_get(&pdev->dev, "pclk_dphy_ref");

		if (IS_ERR_OR_NULL(clk_rst->hclk_isp0_noc)		 ||
			IS_ERR_OR_NULL(clk_rst->hclk_isp0_wrapper)	 ||
			IS_ERR_OR_NULL(clk_rst->aclk_isp0_noc)		 ||
			IS_ERR_OR_NULL(clk_rst->aclk_isp0_wrapper)	 ||
			IS_ERR_OR_NULL(clk_rst->clk_isp0)		 ||
			IS_ERR_OR_NULL(clk_rst->pclk_dphyrx)		 ||
			IS_ERR_OR_NULL(clk_rst->cif_clk_out)		 ||
			IS_ERR_OR_NULL(clk_rst->pclk_dphy_ref)) {
			dev_err(&pdev->dev, "Get rk3399 cif isp10 clock resouce failed !\n");
			err = -EINVAL;
			goto clk_failed;
		}

		clk_set_rate(clk_rst->clk_isp0, 420000000);
		isp_cfg_tmp->isp_idx = CIF_ISP10_ISP0;
	} else {
		res = platform_get_resource_byname(pdev,
					IORESOURCE_MEM, "dsihost-register");
		if (!res) {
			dev_err(&pdev->dev,
				"platform_get_resource_byname dsihost-register failed\n");
			err = -ENODEV;
			goto regmap_failed;
		}
		isp_cfg_tmp->dsihost_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR_OR_NULL(isp_cfg_tmp->dsihost_base)) {
			dev_err(&pdev->dev, "devm_ioremap_resource failed\n");
			if (IS_ERR(isp_cfg_tmp->dsihost_base))
				err = PTR_ERR(isp_cfg_tmp->dsihost_base);
			else
				err = -ENODEV;
			goto regmap_failed;
		}

		clk_rst->hclk_isp1_noc	   =
			devm_clk_get(&pdev->dev, "hclk_isp1_noc");
		clk_rst->hclk_isp1_wrapper =
			devm_clk_get(&pdev->dev, "hclk_isp1_wrapper");
		clk_rst->aclk_isp1_noc	   =
			devm_clk_get(&pdev->dev, "aclk_isp1_noc");
		clk_rst->aclk_isp1_wrapper =
			devm_clk_get(&pdev->dev, "aclk_isp1_wrapper");
		clk_rst->clk_isp1		   =
			devm_clk_get(&pdev->dev, "clk_isp1");
		clk_rst->pclkin_isp1	   =
			devm_clk_get(&pdev->dev, "pclkin_isp1");
		clk_rst->pclk_dphytxrx	   =
			devm_clk_get(&pdev->dev, "pclk_dphytxrx");

		clk_rst->cif_clk_mipi_dsi	   =
			devm_clk_get(&pdev->dev, "pclk_mipi_dsi");
		clk_rst->cif_clk_mipi_dphy_cfg	   =
			devm_clk_get(&pdev->dev, "mipi_dphy_cfg");

		clk_rst->cif_clk_out	   =
			devm_clk_get(&pdev->dev, "clk_cif_out");
		clk_rst->cif_clk_pll	   =
			devm_clk_get(&pdev->dev, "clk_cif_pll");
		clk_rst->pclk_dphy_ref	   =
			devm_clk_get(&pdev->dev, "pclk_dphy_ref");

		if (IS_ERR_OR_NULL(clk_rst->hclk_isp1_noc)		 ||
			IS_ERR_OR_NULL(clk_rst->hclk_isp1_wrapper)	 ||
			IS_ERR_OR_NULL(clk_rst->aclk_isp1_noc)		 ||
			IS_ERR_OR_NULL(clk_rst->aclk_isp1_wrapper)	 ||
			IS_ERR_OR_NULL(clk_rst->clk_isp1)		 ||
			IS_ERR_OR_NULL(clk_rst->pclkin_isp1)		 ||
			IS_ERR_OR_NULL(clk_rst->pclk_dphytxrx)		 ||
			IS_ERR_OR_NULL(clk_rst->cif_clk_mipi_dsi)	 ||
			IS_ERR_OR_NULL(clk_rst->cif_clk_mipi_dphy_cfg)	 ||
			IS_ERR_OR_NULL(clk_rst->cif_clk_out)		 ||
			IS_ERR_OR_NULL(clk_rst->pclk_dphy_ref)) {
			dev_err(&pdev->dev, "Get rk3399 cif isp10 clock resouce failed !\n");
			err = -EINVAL;
			goto clk_failed;
		}

		clk_set_rate(clk_rst->clk_isp1, 420000000);
		isp_cfg_tmp->isp_idx = CIF_ISP10_ISP1;
	}

	isp_cfg_tmp->isp_base = init->isp_base;
	*isp_cfg = isp_cfg_tmp;

	return 0;

clk_failed:
	if (!IS_ERR_OR_NULL(clk_rst->hclk_isp0_noc))
		devm_clk_put(&pdev->dev, clk_rst->hclk_isp0_noc);

	if (!IS_ERR_OR_NULL(clk_rst->hclk_isp0_wrapper))
		devm_clk_put(&pdev->dev, clk_rst->hclk_isp0_wrapper);

	if (!IS_ERR_OR_NULL(clk_rst->aclk_isp0_noc))
		devm_clk_put(&pdev->dev, clk_rst->aclk_isp0_noc);

	if (!IS_ERR_OR_NULL(clk_rst->aclk_isp0_wrapper))
		devm_clk_put(&pdev->dev, clk_rst->aclk_isp0_wrapper);

	if (!IS_ERR_OR_NULL(clk_rst->clk_isp0))
		devm_clk_put(&pdev->dev, clk_rst->clk_isp0);

	if (!IS_ERR_OR_NULL(clk_rst->pclk_dphyrx))
		devm_clk_put(&pdev->dev, clk_rst->pclk_dphyrx);

	if (!IS_ERR_OR_NULL(clk_rst->hclk_isp1_noc))
		devm_clk_put(&pdev->dev, clk_rst->hclk_isp1_noc);

	if (!IS_ERR_OR_NULL(clk_rst->hclk_isp1_wrapper))
		devm_clk_put(&pdev->dev, clk_rst->hclk_isp1_wrapper);

	if (!IS_ERR_OR_NULL(clk_rst->aclk_isp1_noc))
		devm_clk_put(&pdev->dev, clk_rst->aclk_isp1_noc);

	if (!IS_ERR_OR_NULL(clk_rst->aclk_isp1_wrapper))
		devm_clk_put(&pdev->dev, clk_rst->aclk_isp1_wrapper);

	if (!IS_ERR_OR_NULL(clk_rst->clk_isp1))
		devm_clk_put(&pdev->dev, clk_rst->clk_isp1);

	if (!IS_ERR_OR_NULL(clk_rst->pclkin_isp1))
		devm_clk_put(&pdev->dev, clk_rst->pclkin_isp1);

	if (!IS_ERR_OR_NULL(clk_rst->pclk_dphytxrx))
		devm_clk_put(&pdev->dev, clk_rst->pclk_dphytxrx);

	if (!IS_ERR_OR_NULL(clk_rst->cif_clk_mipi_dsi))
		devm_clk_put(&pdev->dev, clk_rst->cif_clk_mipi_dsi);

	if (!IS_ERR_OR_NULL(clk_rst->cif_clk_mipi_dphy_cfg))
		devm_clk_put(&pdev->dev, clk_rst->cif_clk_mipi_dphy_cfg);

	if (!IS_ERR_OR_NULL(clk_rst->cif_clk_out))
		devm_clk_put(&pdev->dev, clk_rst->cif_clk_out);

	if (!IS_ERR_OR_NULL(clk_rst->pclk_dphy_ref))
		devm_clk_put(&pdev->dev, clk_rst->pclk_dphy_ref);

regmap_failed:

alloc_failed:

	return err;
}

int pltfrm_rk3399_cfg(struct pltfrm_soc_cfg_para *cfg)
{
	int ret = -1;
	struct cif_isp10_rk3399 *isp_cfg = NULL;

	if (cfg->isp_config == NULL) {
		return -1;
	} else {
		isp_cfg = (struct cif_isp10_rk3399 *)(*cfg->isp_config);
		if (isp_cfg == NULL  && cfg->cmd != PLTFRM_SOC_INIT)
			return -1;
	}

	switch (cfg->cmd) {
	case PLTFRM_MCLK_CFG: {
		struct pltfrm_soc_mclk_para *mclk_para;

		mclk_para = (struct pltfrm_soc_mclk_para *)cfg->cfg_para;
		if (mclk_para->io_voltage == PLTFRM_IO_1V8)
			write_grf_reg(GRF_IO_VSEL_OFFSET, DVP_V18SEL);
		else
			write_grf_reg(GRF_IO_VSEL_OFFSET, DVP_V33SEL);

		write_grf_reg(GRF_GPIO2B_E_OFFSET,
			CIF_CLKOUT_STRENGTH(mclk_para->drv_strength));
		ret = 0;
		break;
	}
	case PLTFRM_MIPI_DPHY_CFG:
		ret = mipi_dphy_cfg(isp_cfg, (struct pltfrm_cam_mipi_config *)cfg->cfg_para);
		break;

	case PLTFRM_CLKEN:
		ret = soc_clk_enable(isp_cfg);
		break;

	case PLTFRM_CLKDIS:
		ret = soc_clk_disable(isp_cfg);
		break;

	case PLTFRM_CLKRST:
		write_cifisp_reg(VI_IRCL, 0xf7f);
		usleep_range(10, 15);
		write_cifisp_reg(VI_IRCL, 0x00);
		ret = 0;
		break;

	case PLTFRM_SOC_INIT:
		ret = soc_init((struct cif_isp10_rk3399 **)cfg->isp_config, (struct pltfrm_soc_init_para *)cfg->cfg_para);
		break;

	default:
		break;
	}

	return ret;
}
