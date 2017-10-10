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

#define ONE_LANE_ENABLE_BIT     0x1
#define TWO_LANE_ENABLE_BIT     0x2
#define FOUR_LANE_ENABLE_BIT    0x4

#define MRV_MIPI_BASE           0x1C00
#define MRV_MIPI_CTRL           0x00

/*
 * GRF_SOC_CON14
 * bit 0     dphy_rx0_testclr
 * bit 1     dphy_rx0_testclk
 * bit 2     dphy_rx0_testen
 * bit 3:10 dphy_rx0_testdin
 */
#define GRF_SOC_CON14_OFFSET    (0x027c)
#define DPHY_RX0_TESTCLR_MASK   (0x1 << 16)
#define DPHY_RX0_TESTCLK_MASK   (0x1 << 17)
#define DPHY_RX0_TESTEN_MASK    (0x1 << 18)
#define DPHY_RX0_TESTDIN_MASK   (0xff << 19)

#define DPHY_RX0_TESTCLR         BIT(0)
#define DPHY_RX0_TESTCLK         BIT(1)
#define DPHY_RX0_TESTEN          BIT(2)
#define DPHY_RX0_TESTDIN_OFFSET  (3)

#define DPHY_TX1RX1_ENABLECLK_MASK    (0x1 << 28)
#define DPHY_RX1_SRC_SEL_MASK         (0x1 << 29)
#define DPHY_TX1RX1_MASTERSLAVEZ_MASK (0x1 << 30)
#define DPHY_TX1RX1_BASEDIR_OFFSET    (0x1 << 31)

#define DPHY_TX1RX1_ENABLECLK         (0x1 << 12)
#define DPHY_TX1RX1_DISABLECLK        (0x0 << 12)
#define DPHY_RX1_SRC_SEL_ISP          (0x1 << 13)
#define DPHY_TX1RX1_SLAVEZ            (0x0 << 14)
#define DPHY_TX1RX1_BASEDIR_REC       (0x1 << 15)

/*
 * GRF_SOC_CON6
 * bit 0 grf_con_disable_isp
 * bit 1 grf_con_isp_dphy_sel  1'b0 mipi phy rx0
 */
#define GRF_SOC_CON6_OFFSET            (0x025c)
#define MIPI_PHY_DISABLE_ISP_MASK      (0x1 << 16)
#define MIPI_PHY_DISABLE_ISP           (0x0 << 0)

#define DSI_CSI_TESTBUS_SEL_MASK       (0x1 << 30)
#define DSI_CSI_TESTBUS_SEL_OFFSET_BIT (14)

#define MIPI_PHY_DPHYSEL_OFFSET_MASK   (0x1 << 17)
#define MIPI_PHY_DPHYSEL_OFFSET_BIT    (0x1)

/*
 * GRF_SOC_CON10
 * bit12:15 grf_dphy_rx0_enable
 * bit 0:3 turn disable
 */
#define GRF_SOC_CON10_OFFSET                (0x026c)
#define DPHY_RX0_TURN_DISABLE_MASK          (0xf << 16)
#define DPHY_RX0_TURN_DISABLE_OFFSET_BITS   (0x0)
#define DPHY_RX0_ENABLE_MASK                (0xf << 28)
#define DPHY_RX0_ENABLE_OFFSET_BITS         (12)

/*
 * GRF_SOC_CON9
 * bit12:15 grf_dphy_rx0_enable
 * bit 0:3 turn disable
 */
#define GRF_SOC_CON9_OFFSET                    (0x0268)
#define DPHY_TX1RX1_TURN_DISABLE_MASK          (0xf << 16)
#define DPHY_TX1RX1_TURN_DISABLE_OFFSET_BITS   (0x0)
#define DPHY_TX1RX1_ENABLE_MASK                (0xf << 28)
#define DPHY_TX1RX1_ENABLE_OFFSET_BITS         (12)

/*
 * GRF_SOC_CON15
 * bit 0:3   turn request
 */
#define GRF_SOC_CON15_OFFSET                (0x03a4)
#define DPHY_RX0_TURN_REQUEST_MASK          (0xf << 16)
#define DPHY_RX0_TURN_REQUEST_OFFSET_BITS   (0x0)

#define DPHY_TX1RX1_TURN_REQUEST_MASK          (0xf << 20)
#define DPHY_TX1RX1_TURN_REQUEST_OFFSET_BITS   (0x0)

/*
 * GRF_SOC_STATUS21
 * bit0:7   dphy_rx0_testdout
 */
#define GRF_SOC_STATUS21_OFFSET      (0x2D4)
#define DPHY_RX0_TESTDOUT(a)         ((a) & 0xff)

/*
 * GRF_IO_VSEL
 */
#define GRF_IO_VSEL_OFFSET		(0x0380)
#define DVP_V18SEL			((1 << 1) | (1 << 17))
#define DVP_V33SEL			((0 << 1) | (1 << 17))

/*
 * GRF_IO_VSEL
 */
#define GRF_GPIO2B_E_OFFSET       (0x0380)
#define CIF_CLKOUT_STRENGTH(a)    ((((a) & 0x03) << 3) | (0x03 << 19))

/*
 * CSI HOST
 */

#define CSIHOST_PHY_TEST_CTRL0            (0x30)
#define CSIHOST_PHY_TEST_CTRL1            (0x34)
#define CSIHOST_PHY_SHUTDOWNZ             (0x08)
#define CSIHOST_DPHY_RSTZ                 (0x0c)
#define CSIHOST_N_LANES                   (0x04)
#define CSIHOST_CSI2_RESETN               (0x10)
#define CSIHOST_PHY_STATE                 (0x14)
#define CSIHOST_DATA_IDS1                 (0x18)
#define CSIHOST_DATA_IDS2                 (0x1C)
#define CSIHOST_ERR1                      (0x20)
#define CSIHOST_ERR2                      (0x24)

#define write_cifisp_reg(addr, val)	\
		__raw_writel(val, (addr) + rk3288->isp_base)
#define read_cifisp_reg(addr)	\
		__raw_readl((addr) + rk3288->isp_base)

#define write_grf_reg(addr, val)	\
		regmap_write(rk3288->regmap_grf, addr, val)
#define read_grf_reg(addr, val)	regmap_read(rk3288->regmap_grf, addr, val)

#define write_csihost_reg(addr, val)	\
		__raw_writel(val, (addr) + rk3288->csihost_base)
#define read_csihost_reg(addr)	__raw_readl((addr) + rk3288->csihost_base)

struct cif_isp10_clk_rst_rk3288 {
	struct clk	*aclk_isp;
	struct clk	*hclk_isp;
	struct clk	*sclk_isp;
	struct clk	*sclk_isp_jpe;
	struct clk *sclk_mipidsi_24m;
	struct clk *pclk_mipi_csi;
	struct clk *pclk_isp_in;
	struct reset_control *isp_rst;
};

struct cif_isp10_rk3288 {
	struct regmap *regmap_grf;
	void __iomem *csihost_base;
	void __iomem *isp_base;
	struct cif_isp10_clk_rst_rk3288 clk_rst;
	struct cif_isp10_device *cif_isp10;
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
	{950, 1000, 0x1a}
};

static struct cif_isp10_rk3288 *rk3288;
static int mipi_dphy0_wr_reg(unsigned char addr, unsigned char data)
{
	/*
	 * TESTCLK=1
	 * TESTEN =1,TESTDIN=addr
	 * TESTCLK=0
	 */
	write_grf_reg(GRF_SOC_CON14_OFFSET,
		DPHY_RX0_TESTCLK_MASK | DPHY_RX0_TESTCLK);
	write_grf_reg(GRF_SOC_CON14_OFFSET,
		((addr << DPHY_RX0_TESTDIN_OFFSET) | DPHY_RX0_TESTDIN_MASK
		 | DPHY_RX0_TESTEN | DPHY_RX0_TESTEN_MASK));
	write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK);

	/*
	 * write data:
	 * TESTEN =0,TESTDIN=data
	 * TESTCLK=1
	 */
	if (data != 0xff) {
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			((data << DPHY_RX0_TESTDIN_OFFSET) |
			DPHY_RX0_TESTDIN_MASK | DPHY_RX0_TESTEN_MASK));
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			DPHY_RX0_TESTCLK_MASK | DPHY_RX0_TESTCLK);
	}
	return 0;
}

static int mipi_dphy1_wr_reg(unsigned char addr, unsigned char data)
{
	/*
	 * TESTEN =1,TESTDIN=addr
	 * TESTCLK=0
	 * TESTEN =0,TESTDIN=data
	 * TESTCLK=1
	 */
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL1, (0x00010000 | addr));
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000000);
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL1, (0x00000000 | data));
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000002);

	return 0;
}

static int mipi_dphy1_rd_reg(unsigned char addr)
{
	return (read_csihost_reg(((CSIHOST_PHY_TEST_CTRL1) & 0xff00)) >> 8);
}

static int mipi_dphy_cfg(struct pltfrm_cam_mipi_config *para)
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
		write_grf_reg(GRF_SOC_CON6_OFFSET,
			MIPI_PHY_DPHYSEL_OFFSET_MASK |
			(input_sel << MIPI_PHY_DPHYSEL_OFFSET_BIT));
		/* set lane num */
		write_grf_reg(GRF_SOC_CON10_OFFSET,
			DPHY_RX0_ENABLE_MASK |
			(datalane_en << DPHY_RX0_ENABLE_OFFSET_BITS));
		/* set lan turndisab as 1 */
		write_grf_reg(GRF_SOC_CON10_OFFSET,
			DPHY_RX0_TURN_DISABLE_MASK |
			(0xf << DPHY_RX0_TURN_DISABLE_OFFSET_BITS));
		write_grf_reg(GRF_SOC_CON10_OFFSET,
		(0x0 << 4) | (0xf << 20));
		/* set lan turnrequest as 0 */
		write_grf_reg(GRF_SOC_CON15_OFFSET,
			DPHY_RX0_TURN_REQUEST_MASK |
			(0x0 << DPHY_RX0_TURN_REQUEST_OFFSET_BITS));

		/* phy start */
		/*
		 * TESTCLK=1
		 * TESTCLR=1
		 * delay 100us
		 * TESTCLR=0
		 */
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			DPHY_RX0_TESTCLK_MASK | DPHY_RX0_TESTCLK);
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			DPHY_RX0_TESTCLR_MASK | DPHY_RX0_TESTCLR);
		usleep_range(100, 150);
		write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLR_MASK);
		usleep_range(100, 150);

		/* set clock lane */
		mipi_dphy0_wr_reg(0x34, 0x15);
		if (datalane_en == ONE_LANE_ENABLE_BIT) {
			mipi_dphy0_wr_reg(0x44, hsfreqrange);
		} else if (datalane_en == TWO_LANE_ENABLE_BIT) {
			mipi_dphy0_wr_reg(0x44, hsfreqrange);
			mipi_dphy0_wr_reg(0x54, hsfreqrange);
		} else if (datalane_en == FOUR_LANE_ENABLE_BIT) {
			mipi_dphy0_wr_reg(0x44, hsfreqrange);
			mipi_dphy0_wr_reg(0x54, hsfreqrange);
			mipi_dphy0_wr_reg(0x84, hsfreqrange);
			mipi_dphy0_wr_reg(0x94, hsfreqrange);
		}

		/* Normal operation */
		/*
		 * TESTCLK=1
		 * TESTEN =0
		 */
		mipi_dphy0_wr_reg(0x0, -1);
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			DPHY_RX0_TESTCLK_MASK | DPHY_RX0_TESTCLK);
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			(DPHY_RX0_TESTEN_MASK));

		write_cifisp_reg((MRV_MIPI_BASE + MRV_MIPI_CTRL),
			read_cifisp_reg(MRV_MIPI_BASE + MRV_MIPI_CTRL) |
			(0x0f << 8));

	} else if (input_sel == 1) {
		write_grf_reg(GRF_SOC_CON6_OFFSET,
			MIPI_PHY_DPHYSEL_OFFSET_MASK |
			(input_sel << MIPI_PHY_DPHYSEL_OFFSET_BIT));
		write_grf_reg(GRF_SOC_CON6_OFFSET,
			DSI_CSI_TESTBUS_SEL_MASK |
			(1 << DSI_CSI_TESTBUS_SEL_OFFSET_BIT));

		write_grf_reg(GRF_SOC_CON14_OFFSET,
			DPHY_RX1_SRC_SEL_ISP | DPHY_RX1_SRC_SEL_MASK);
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			DPHY_TX1RX1_SLAVEZ | DPHY_TX1RX1_MASTERSLAVEZ_MASK);
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			DPHY_TX1RX1_BASEDIR_REC | DPHY_TX1RX1_BASEDIR_OFFSET);

		/* set lane num */
		write_grf_reg(GRF_SOC_CON9_OFFSET,
			DPHY_TX1RX1_ENABLE_MASK |
			(datalane_en << DPHY_TX1RX1_ENABLE_OFFSET_BITS));
		/* set lan turndisab as 1 */
		write_grf_reg(GRF_SOC_CON9_OFFSET,
			DPHY_TX1RX1_TURN_DISABLE_MASK |
			(0xf << DPHY_TX1RX1_TURN_DISABLE_OFFSET_BITS));
		/* set lan turnrequest as 0   */
		write_grf_reg(GRF_SOC_CON15_OFFSET,
			DPHY_TX1RX1_TURN_REQUEST_MASK |
			(0x0 << DPHY_TX1RX1_TURN_REQUEST_OFFSET_BITS));

		/* phy1 start */
		/*
		 * SHUTDOWNZ=0
		 * RSTZ=0
		 * TESTCLK=1
		 * TESTCLR=1 TESTCLK=1
		 * TESTCLR=0 TESTCLK=1
		 */
		write_csihost_reg(CSIHOST_PHY_SHUTDOWNZ, 0x00000000);
		write_csihost_reg(CSIHOST_DPHY_RSTZ, 0x00000000);
		write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000002);
		write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000003);
		usleep_range(100, 150);
		write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000002);
		usleep_range(100, 150);

		/* set clock lane */
		mipi_dphy1_wr_reg(0x34, 0x15);

		if (datalane_en == ONE_LANE_ENABLE_BIT) {
			mipi_dphy1_wr_reg(0x44, hsfreqrange);
		} else if (datalane_en == TWO_LANE_ENABLE_BIT) {
			mipi_dphy1_wr_reg(0x44, hsfreqrange);
			mipi_dphy1_wr_reg(0x54, hsfreqrange);
		} else if (datalane_en == FOUR_LANE_ENABLE_BIT) {
			mipi_dphy1_wr_reg(0x44, hsfreqrange);
			mipi_dphy1_wr_reg(0x54, hsfreqrange);
			mipi_dphy1_wr_reg(0x84, hsfreqrange);
			mipi_dphy1_wr_reg(0x94, hsfreqrange);
		}

		mipi_dphy1_rd_reg(0x0);
		/*
		 * TESTCLK=1
		 * TESTEN =0
		 * SHUTDOWNZ=1
		 * RSTZ=1
		 */
		write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000002);
		write_csihost_reg(CSIHOST_PHY_TEST_CTRL1, 0x00000000);
		write_csihost_reg(CSIHOST_PHY_SHUTDOWNZ, 0x00000001);
		write_csihost_reg(CSIHOST_DPHY_RSTZ, 0x00000001);
	} else {
		goto fail;
	}

	return 0;
fail:
	return -1;
}

static int soc_clk_enable(void)
{
	struct cif_isp10_clk_rst_rk3288 *clk_rst = &rk3288->clk_rst;

	clk_prepare_enable(clk_rst->hclk_isp);
	clk_prepare_enable(clk_rst->aclk_isp);
	clk_prepare_enable(clk_rst->sclk_isp);
	clk_prepare_enable(clk_rst->sclk_isp_jpe);
	clk_prepare_enable(clk_rst->sclk_mipidsi_24m);
	clk_prepare_enable(clk_rst->pclk_isp_in);
	clk_prepare_enable(clk_rst->pclk_mipi_csi);
	return 0;
}

static int soc_clk_disable(void)
{
	struct cif_isp10_clk_rst_rk3288 *clk_rst = &rk3288->clk_rst;

	clk_disable_unprepare(clk_rst->hclk_isp);
	clk_disable_unprepare(clk_rst->aclk_isp);
	clk_disable_unprepare(clk_rst->sclk_isp);
	clk_disable_unprepare(clk_rst->sclk_isp_jpe);
	clk_disable_unprepare(clk_rst->sclk_mipidsi_24m);
	clk_disable_unprepare(clk_rst->pclk_isp_in);
	clk_disable_unprepare(clk_rst->pclk_mipi_csi);
	return 0;
}

static int soc_init(struct pltfrm_soc_init_para *init)
{
	struct cif_isp10_clk_rst_rk3288 *clk_rst;
	struct platform_device *pdev = init->pdev;
	struct device_node *np = pdev->dev.of_node, *node;
	struct resource *res;
	int err;

	rk3288 = (struct cif_isp10_rk3288 *)devm_kzalloc(
				&pdev->dev,
				sizeof(struct cif_isp10_rk3288),
				GFP_KERNEL);
	if (!rk3288) {
		dev_err(&pdev->dev, "Can't allocate cif_isp10_rk3288\n");
		err = -ENOMEM;
		goto alloc_failed;
	}

	node = of_parse_phandle(np, "rockchip,grf", 0);
	if (node) {
		rk3288->regmap_grf = syscon_node_to_regmap(node);
		if (IS_ERR(rk3288->regmap_grf)) {
			dev_err(&pdev->dev, "Can't allocate cif_isp10_rk3288\n");
			err = -ENODEV;
			goto regmap_failed;
		}
	}

	res = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, "csihost-register");
	if (!res) {
		dev_err(&pdev->dev,
			"platform_get_resource_byname csihost-register failed\n");
		err = -ENODEV;
		goto regmap_failed;
	}
	rk3288->csihost_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR_OR_NULL(rk3288->csihost_base)) {
		dev_err(&pdev->dev, "devm_ioremap_resource failed\n");
		if (IS_ERR(rk3288->csihost_base))
			err = PTR_ERR(rk3288->csihost_base);
		else
			err = -ENODEV;
		goto regmap_failed;
	}

	clk_rst = &rk3288->clk_rst;
	clk_rst->aclk_isp = devm_clk_get(&pdev->dev, "aclk_isp");
	clk_rst->hclk_isp = devm_clk_get(&pdev->dev, "hclk_isp");
	clk_rst->sclk_isp = devm_clk_get(&pdev->dev, "sclk_isp");
	clk_rst->sclk_isp_jpe = devm_clk_get(&pdev->dev, "sclk_isp_jpe");
	clk_rst->sclk_mipidsi_24m =
			devm_clk_get(&pdev->dev, "sclk_mipidsi_24m");
	clk_rst->pclk_mipi_csi = devm_clk_get(&pdev->dev, "pclk_mipi_csi");
	clk_rst->isp_rst = devm_reset_control_get(&pdev->dev, "rst_isp");
	clk_rst->pclk_isp_in = devm_clk_get(&pdev->dev, "pclk_isp_in");

	if (IS_ERR_OR_NULL(clk_rst->aclk_isp) ||
		IS_ERR_OR_NULL(clk_rst->hclk_isp) ||
		IS_ERR_OR_NULL(clk_rst->sclk_isp) ||
		IS_ERR_OR_NULL(clk_rst->sclk_isp_jpe) ||
		IS_ERR_OR_NULL(clk_rst->pclk_mipi_csi) ||
		IS_ERR_OR_NULL(clk_rst->isp_rst) ||
		IS_ERR_OR_NULL(clk_rst->pclk_isp_in) ||
		IS_ERR_OR_NULL(clk_rst->sclk_mipidsi_24m)) {
		dev_err(&pdev->dev, "Get rk3288 cif isp10 clock resouce failed !\n");
		err = -EINVAL;
		goto clk_failed;
	}

	clk_set_rate(clk_rst->sclk_isp, 400000000);
	clk_set_rate(clk_rst->sclk_isp_jpe, 400000000);
	reset_control_deassert(clk_rst->isp_rst);

	rk3288->isp_base = init->isp_base;
	return 0;

clk_failed:
	if (!IS_ERR_OR_NULL(clk_rst->aclk_isp))
		devm_clk_put(&pdev->dev, clk_rst->aclk_isp);
	if (!IS_ERR_OR_NULL(clk_rst->hclk_isp))
		devm_clk_put(&pdev->dev, clk_rst->hclk_isp);
	if (!IS_ERR_OR_NULL(clk_rst->sclk_isp))
		devm_clk_put(&pdev->dev, clk_rst->sclk_isp);
	if (!IS_ERR_OR_NULL(clk_rst->sclk_isp_jpe))
		devm_clk_put(&pdev->dev, clk_rst->sclk_isp_jpe);
	if (!IS_ERR_OR_NULL(clk_rst->pclk_mipi_csi))
		devm_clk_put(&pdev->dev, clk_rst->pclk_mipi_csi);
	if (!IS_ERR_OR_NULL(clk_rst->pclk_isp_in))
		devm_clk_put(&pdev->dev, clk_rst->pclk_isp_in);
	if (!IS_ERR_OR_NULL(clk_rst->sclk_mipidsi_24m))
		devm_clk_put(&pdev->dev, clk_rst->sclk_mipidsi_24m);

	if (!IS_ERR_OR_NULL(clk_rst->isp_rst))
		reset_control_put(clk_rst->isp_rst);

regmap_failed:

alloc_failed:

	return err;
}

int pltfrm_rk3288_cfg(struct pltfrm_soc_cfg_para *cfg)
{
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
		break;
	}
	case PLTFRM_MIPI_DPHY_CFG:
		mipi_dphy_cfg((struct pltfrm_cam_mipi_config *)cfg->cfg_para);
		break;

	case PLTFRM_CLKEN:
		soc_clk_enable();
		break;

	case PLTFRM_CLKDIS:
		soc_clk_disable();
		break;

	case PLTFRM_CLKRST:
		reset_control_assert(rk3288->clk_rst.isp_rst);
		usleep_range(10, 15);
		reset_control_deassert(rk3288->clk_rst.isp_rst);
		break;

	case PLTFRM_SOC_INIT:
		soc_init((struct pltfrm_soc_init_para *)cfg->cfg_para);
		break;

	default:
		break;
	}

	return 0;
}

