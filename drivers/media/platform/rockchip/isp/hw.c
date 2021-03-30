// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Rockchip Electronics Co., Ltd */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>

#include "common.h"
#include "dev.h"
#include "hw.h"
#include "regs.h"

/*
 * rkisp_hw share hardware resource with rkisp virtual device
 * rkisp_device rkisp_device rkisp_device rkisp_device
 *      |            |            |            |
 *      \            |            |            /
 *       --------------------------------------
 *                         |
 *                     rkisp_hw
 */

struct isp_irqs_data {
	const char *name;
	irqreturn_t (*irq_hdl)(int irq, void *ctx);
};

/* using default value if reg no write for multi device */
static void default_sw_reg_flag(struct rkisp_device *dev)
{
	u32 v20_reg[] = {
		CTRL_VI_ISP_PATH, IMG_EFF_CTRL, ISP_CCM_CTRL,
		CPROC_CTRL, DUAL_CROP_CTRL, ISP_GAMMA_OUT_CTRL,
		ISP_LSC_CTRL, ISP_DEBAYER_CONTROL, ISP_WDR_CTRL,
		ISP_GIC_CONTROL, ISP_BLS_CTRL, ISP_DPCC0_MODE,
		ISP_DPCC1_MODE, ISP_DPCC2_MODE, ISP_HDRMGE_CTRL,
		ISP_HDRTMO_CTRL, ISP_RAWNR_CTRL, ISP_LDCH_STS,
		ISP_DHAZ_CTRL, ISP_3DLUT_CTRL, ISP_GAIN_CTRL,
		ISP_AFM_CTRL, ISP_HIST_HIST_CTRL, RAWAE_BIG1_BASE,
		RAWAE_BIG2_BASE, RAWAE_BIG3_BASE, ISP_RAWAE_LITE_CTRL,
		ISP_RAWHIST_LITE_CTRL, ISP_RAWHIST_BIG1_BASE,
		ISP_RAWHIST_BIG2_BASE, ISP_RAWHIST_BIG3_BASE,
		ISP_YUVAE_CTRL, ISP_RAWAF_CTRL, ISP_RAWAWB_CTRL,
	};
	u32 v21_reg[] = {
		CTRL_VI_ISP_PATH, IMG_EFF_CTRL, ISP_CCM_CTRL,
		CPROC_CTRL, DUAL_CROP_CTRL, ISP_GAMMA_OUT_CTRL,
		SELF_RESIZE_CTRL, MAIN_RESIZE_CTRL, ISP_LSC_CTRL,
		ISP_DEBAYER_CONTROL, ISP21_YNR_GLOBAL_CTRL,
		ISP21_CNR_CTRL, ISP21_SHARP_SHARP_EN, ISP_GIC_CONTROL,
		ISP_BLS_CTRL, ISP_DPCC0_MODE, ISP_DPCC1_MODE,
		ISP_HDRMGE_CTRL, ISP21_DRC_CTRL0, ISP21_BAYNR_CTRL,
		ISP21_BAY3D_CTRL, ISP_LDCH_STS, ISP21_DHAZ_CTRL,
		ISP_3DLUT_CTRL, ISP_AFM_CTRL, ISP_HIST_HIST_CTRL,
		RAWAE_BIG1_BASE, RAWAE_BIG2_BASE, RAWAE_BIG3_BASE,
		ISP_RAWAE_LITE_CTRL, ISP_RAWHIST_LITE_CTRL,
		ISP_RAWHIST_BIG1_BASE, ISP_RAWHIST_BIG2_BASE,
		ISP_RAWHIST_BIG3_BASE, ISP_YUVAE_CTRL, ISP_RAWAF_CTRL,
		ISP21_RAWAWB_CTRL,
	};
	u32 i, *flag, *reg, size;

	switch (dev->isp_ver) {
	case ISP_V20:
		reg = v20_reg;
		size = ARRAY_SIZE(v20_reg);
		break;
	case ISP_V21:
		reg = v21_reg;
		size = ARRAY_SIZE(v21_reg);
		break;
	default:
		return;
	}

	for (i = 0; i < size; i++) {
		flag = dev->sw_base_addr + reg[i] + RKISP_ISP_SW_REG_SIZE;
		*flag = SW_REG_CACHE;
	}
}

static irqreturn_t mipi_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	struct rkisp_device *isp = hw_dev->isp[hw_dev->mipi_dev_id];

	if (hw_dev->is_thunderboot)
		return IRQ_HANDLED;

	if (hw_dev->isp_ver == ISP_V13 || hw_dev->isp_ver == ISP_V12) {
		u32 err1, err2, err3;

		err1 = readl(hw_dev->base_addr + CIF_ISP_CSI0_ERR1);
		err2 = readl(hw_dev->base_addr + CIF_ISP_CSI0_ERR2);
		err3 = readl(hw_dev->base_addr + CIF_ISP_CSI0_ERR3);

		if (err1 || err2 || err3)
			rkisp_mipi_v13_isr(err1, err2, err3, isp);
	} else if (hw_dev->isp_ver == ISP_V20 || hw_dev->isp_ver == ISP_V21) {
		u32 phy, packet, overflow, state;

		state = readl(hw_dev->base_addr + CSI2RX_ERR_STAT);
		phy = readl(hw_dev->base_addr + CSI2RX_ERR_PHY);
		packet = readl(hw_dev->base_addr + CSI2RX_ERR_PACKET);
		overflow = readl(hw_dev->base_addr + CSI2RX_ERR_OVERFLOW);
		if (phy | packet | overflow | state) {
			if (hw_dev->isp_ver == ISP_V20)
				rkisp_mipi_v20_isr(phy, packet, overflow, state, isp);
			else
				rkisp_mipi_v21_isr(phy, packet, overflow, state, isp);
		}
	} else {
		u32 mis_val = readl(hw_dev->base_addr + CIF_MIPI_MIS);

		if (mis_val)
			rkisp_mipi_isr(mis_val, isp);
	}

	return IRQ_HANDLED;
}

static irqreturn_t mi_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	struct rkisp_device *isp = hw_dev->isp[hw_dev->cur_dev_id];
	u32 mis_val, tx_isr = MI_RAW0_WR_FRAME | MI_RAW1_WR_FRAME |
		MI_RAW2_WR_FRAME | MI_RAW3_WR_FRAME;

	if (hw_dev->is_thunderboot)
		return IRQ_HANDLED;

	mis_val = readl(hw_dev->base_addr + CIF_MI_MIS);
	if (mis_val) {
		if (mis_val & ~tx_isr)
			rkisp_mi_isr(mis_val & ~tx_isr, isp);
		if (mis_val & tx_isr) {
			isp = hw_dev->isp[hw_dev->mipi_dev_id];
			rkisp_mi_isr(mis_val & tx_isr, isp);
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t isp_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	struct rkisp_device *isp = hw_dev->isp[hw_dev->cur_dev_id];
	unsigned int mis_val, mis_3a = 0;

	if (hw_dev->is_thunderboot)
		return IRQ_HANDLED;

	mis_val = readl(hw_dev->base_addr + CIF_ISP_MIS);
	if (hw_dev->isp_ver == ISP_V20 || hw_dev->isp_ver == ISP_V21)
		mis_3a = readl(hw_dev->base_addr + ISP_ISP3A_MIS);
	if (mis_val || mis_3a)
		rkisp_isp_isr(mis_val, mis_3a, isp);

	return IRQ_HANDLED;
}

static irqreturn_t irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	struct rkisp_device *isp = hw_dev->isp[hw_dev->cur_dev_id];
	unsigned int mis_val, mis_3a = 0;

	mis_val = readl(hw_dev->base_addr + CIF_ISP_MIS);
	if (hw_dev->isp_ver == ISP_V20 || hw_dev->isp_ver == ISP_V21)
		mis_3a = readl(hw_dev->base_addr + ISP_ISP3A_MIS);
	if (mis_val || mis_3a)
		rkisp_isp_isr(mis_val, mis_3a, isp);

	mis_val = readl(hw_dev->base_addr + CIF_MIPI_MIS);
	if (mis_val)
		rkisp_mipi_isr(mis_val, isp);

	mis_val = readl(hw_dev->base_addr + CIF_MI_MIS);
	if (mis_val)
		rkisp_mi_isr(mis_val, isp);

	return IRQ_HANDLED;
}

int rkisp_register_irq(struct rkisp_hw_dev *hw_dev)
{
	const struct isp_match_data *match_data = hw_dev->match_data;
	struct platform_device *pdev = hw_dev->pdev;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int i, ret, irq;

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
					   match_data->irqs[0].name);
	if (res) {
		/* there are irq names in dts */
		for (i = 0; i < match_data->num_irqs; i++) {
			irq = platform_get_irq_byname(pdev, match_data->irqs[i].name);
			if (irq < 0) {
				dev_err(dev, "no irq %s in dts\n",
					match_data->irqs[i].name);
				return irq;
			}

			if (!strcmp(match_data->irqs[i].name, "mipi_irq"))
				hw_dev->mipi_irq = irq;

			ret = devm_request_irq(dev, irq,
					       match_data->irqs[i].irq_hdl,
					       IRQF_SHARED,
					       dev_driver_string(dev),
					       dev);
			if (ret < 0) {
				dev_err(dev, "request %s failed: %d\n",
					match_data->irqs[i].name, ret);
				return ret;
			}

			if (hw_dev->mipi_irq == irq &&
			    (hw_dev->isp_ver == ISP_V12 ||
			     hw_dev->isp_ver == ISP_V13))
				disable_irq(hw_dev->mipi_irq);
		}
	} else {
		/* no irq names in dts */
		irq = platform_get_irq(pdev, 0);
		if (irq < 0) {
			dev_err(dev, "no isp irq in dts\n");
			return irq;
		}

		ret = devm_request_irq(dev, irq,
				       irq_handler,
				       IRQF_SHARED,
				       dev_driver_string(dev),
				       dev);
		if (ret < 0) {
			dev_err(dev, "request irq failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const char * const rk1808_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"pclk_isp",
};

static const char * const rk3288_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"pclk_isp_in",
	"sclk_isp_jpe",
};

static const char * const rk3326_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"pclk_isp",
};

static const char * const rk3368_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"pclk_isp",
};

static const char * const rk3399_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"aclk_isp_wrap",
	"hclk_isp_wrap",
	"pclk_isp_wrap"
};

static const char * const rk3568_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
};

static const char * const rv1126_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
};

/* isp clock adjustment table (MHz) */
static const struct isp_clk_info rk1808_isp_clk_rate[] = {
	{300, }, {400, }, {500, }, {600, }
};

/* isp clock adjustment table (MHz) */
static const struct isp_clk_info rk3288_isp_clk_rate[] = {
	{150, }, {384, }, {500, }, {594, }
};

/* isp clock adjustment table (MHz) */
static const struct isp_clk_info rk3326_isp_clk_rate[] = {
	{300, }, {347, }, {400, }, {520, }, {600, }
};

/* isp clock adjustment table (MHz) */
static const struct isp_clk_info rk3368_isp_clk_rate[] = {
	{300, }, {400, }, {600, }
};

/* isp clock adjustment table (MHz) */
static const struct isp_clk_info rk3399_isp_clk_rate[] = {
	{300, }, {400, }, {600, }
};

static const struct isp_clk_info rk3568_isp_clk_rate[] = {
	{
		.clk_rate = 300,
		.refer_data = 1920, //width
	}, {
		.clk_rate = 400,
		.refer_data = 2688,
	}, {
		.clk_rate = 500,
		.refer_data = 3072,
	}, {
		.clk_rate = 600,
		.refer_data = 3840,
	}
};

static const struct isp_clk_info rv1126_isp_clk_rate[] = {
	{
		.clk_rate = 20,
		.refer_data = 0,
	}, {
		.clk_rate = 300,
		.refer_data = 1920, //width
	}, {
		.clk_rate = 400,
		.refer_data = 2688,
	}, {
		.clk_rate = 500,
		.refer_data = 3072,
	}, {
		.clk_rate = 600,
		.refer_data = 3840,
	}
};

static struct isp_irqs_data rk1808_isp_irqs[] = {
	{"isp_irq", isp_irq_hdl},
	{"mi_irq", mi_irq_hdl},
	{"mipi_irq", mipi_irq_hdl}
};

static struct isp_irqs_data rk3288_isp_irqs[] = {
	{"isp_irq", irq_handler}
};

static struct isp_irqs_data rk3326_isp_irqs[] = {
	{"isp_irq", isp_irq_hdl},
	{"mi_irq", mi_irq_hdl},
	{"mipi_irq", mipi_irq_hdl}
};

static struct isp_irqs_data rk3368_isp_irqs[] = {
	{"isp_irq", irq_handler}
};

static struct isp_irqs_data rk3399_isp_irqs[] = {
	{"isp_irq", irq_handler}
};

static struct isp_irqs_data rk3568_isp_irqs[] = {
	{"isp_irq", isp_irq_hdl},
	{"mi_irq", mi_irq_hdl},
	{"mipi_irq", mipi_irq_hdl}
};

static struct isp_irqs_data rv1126_isp_irqs[] = {
	{"isp_irq", isp_irq_hdl},
	{"mi_irq", mi_irq_hdl},
	{"mipi_irq", mipi_irq_hdl}
};

static const struct isp_match_data rv1126_isp_match_data = {
	.clks = rv1126_isp_clks,
	.num_clks = ARRAY_SIZE(rv1126_isp_clks),
	.isp_ver = ISP_V20,
	.clk_rate_tbl = rv1126_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rv1126_isp_clk_rate),
	.irqs = rv1126_isp_irqs,
	.num_irqs = ARRAY_SIZE(rv1126_isp_irqs)
};

static const struct isp_match_data rk1808_isp_match_data = {
	.clks = rk1808_isp_clks,
	.num_clks = ARRAY_SIZE(rk1808_isp_clks),
	.isp_ver = ISP_V13,
	.clk_rate_tbl = rk1808_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk1808_isp_clk_rate),
	.irqs = rk1808_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk1808_isp_irqs)
};

static const struct isp_match_data rk3288_isp_match_data = {
	.clks = rk3288_isp_clks,
	.num_clks = ARRAY_SIZE(rk3288_isp_clks),
	.isp_ver = ISP_V10,
	.clk_rate_tbl = rk3288_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3288_isp_clk_rate),
	.irqs = rk3288_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3288_isp_irqs)
};

static const struct isp_match_data rk3326_isp_match_data = {
	.clks = rk3326_isp_clks,
	.num_clks = ARRAY_SIZE(rk3326_isp_clks),
	.isp_ver = ISP_V12,
	.clk_rate_tbl = rk3326_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3326_isp_clk_rate),
	.irqs = rk3326_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3326_isp_irqs)
};

static const struct isp_match_data rk3368_isp_match_data = {
	.clks = rk3368_isp_clks,
	.num_clks = ARRAY_SIZE(rk3368_isp_clks),
	.isp_ver = ISP_V10_1,
	.clk_rate_tbl = rk3368_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3368_isp_clk_rate),
	.irqs = rk3368_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3368_isp_irqs)
};

static const struct isp_match_data rk3399_isp_match_data = {
	.clks = rk3399_isp_clks,
	.num_clks = ARRAY_SIZE(rk3399_isp_clks),
	.isp_ver = ISP_V10,
	.clk_rate_tbl = rk3399_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3399_isp_clk_rate),
	.irqs = rk3399_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3399_isp_irqs)
};

static const struct isp_match_data rk3568_isp_match_data = {
	.clks = rk3568_isp_clks,
	.num_clks = ARRAY_SIZE(rk3568_isp_clks),
	.isp_ver = ISP_V21,
	.clk_rate_tbl = rk3568_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3568_isp_clk_rate),
	.irqs = rk3568_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3568_isp_irqs)
};

static const struct of_device_id rkisp_hw_of_match[] = {
	{
		.compatible = "rockchip,rk1808-rkisp1",
		.data = &rk1808_isp_match_data,
	}, {
		.compatible = "rockchip,rk3288-rkisp1",
		.data = &rk3288_isp_match_data,
	}, {
		.compatible = "rockchip,rk3326-rkisp1",
		.data = &rk3326_isp_match_data,
	}, {
		.compatible = "rockchip,rk3368-rkisp1",
		.data = &rk3368_isp_match_data,
	}, {
		.compatible = "rockchip,rk3399-rkisp1",
		.data = &rk3399_isp_match_data,
	}, {
		.compatible = "rockchip,rk3568-rkisp",
		.data = &rk3568_isp_match_data,
	}, {
		.compatible = "rockchip,rv1126-rkisp",
		.data = &rv1126_isp_match_data,
	},
	{},
};

static inline bool is_iommu_enable(struct device *dev)
{
	struct device_node *iommu;

	iommu = of_parse_phandle(dev->of_node, "iommus", 0);
	if (!iommu) {
		dev_info(dev, "no iommu attached, using non-iommu buffers\n");
		return false;
	} else if (!of_device_is_available(iommu)) {
		dev_info(dev, "iommu is disabled, using non-iommu buffers\n");
		of_node_put(iommu);
		return false;
	}
	of_node_put(iommu);

	return true;
}

void rkisp_soft_reset(struct rkisp_hw_dev *dev, bool is_secure)
{
	void __iomem *base = dev->base_addr;
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev->dev);

	if (domain)
		iommu_detach_device(domain, dev->dev);

	if (is_secure) {
		/* if isp working, cru reset isn't secure.
		 * isp soft reset first to protect isp reset.
		 */
		writel(0xffff, base + CIF_IRCL);
		udelay(10);
	}

	if (dev->reset) {
		reset_control_assert(dev->reset);
		udelay(10);
		reset_control_deassert(dev->reset);
		udelay(10);
	}

	if (dev->isp_ver == ISP_V20) {
		/* reset for Dehaze */
		writel(CIF_ISP_CTRL_ISP_MODE_BAYER_ITU601, base + CIF_ISP_CTRL);
		writel(0xffff, base + CIF_IRCL);
		udelay(10);
	}

	if (domain)
		iommu_attach_device(domain, dev->dev);
}

static void isp_config_clk(struct rkisp_hw_dev *dev, int on)
{
	u32 val = !on ? 0 :
		CIF_ICCL_ISP_CLK | CIF_ICCL_CP_CLK | CIF_ICCL_MRSZ_CLK |
		CIF_ICCL_SRSZ_CLK | CIF_ICCL_JPEG_CLK | CIF_ICCL_MI_CLK |
		CIF_ICCL_IE_CLK | CIF_ICCL_MIPI_CLK | CIF_ICCL_DCROP_CLK;

	if (dev->isp_ver == ISP_V20 && on)
		val |= ICCL_MPFBC_CLK;

	writel(val, dev->base_addr + CIF_ICCL);

	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
		val = !on ? 0 :
		      CIF_CLK_CTRL_MI_Y12 | CIF_CLK_CTRL_MI_SP |
		      CIF_CLK_CTRL_MI_RAW0 | CIF_CLK_CTRL_MI_RAW1 |
		      CIF_CLK_CTRL_MI_READ | CIF_CLK_CTRL_MI_RAWRD |
		      CIF_CLK_CTRL_CP | CIF_CLK_CTRL_IE;

		writel(val, dev->base_addr + CIF_VI_ISP_CLK_CTRL_V12);
	} else if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) {
		val = !on ? 0 :
		      CLK_CTRL_MI_LDC | CLK_CTRL_MI_MP |
		      CLK_CTRL_MI_JPEG | CLK_CTRL_MI_DP |
		      CLK_CTRL_MI_Y12 | CLK_CTRL_MI_SP |
		      CLK_CTRL_MI_RAW0 | CLK_CTRL_MI_RAW1 |
		      CLK_CTRL_MI_READ | CLK_CTRL_MI_RAWRD |
		      CLK_CTRL_ISP_RAW;

		if (dev->isp_ver == ISP_V20 && on)
			val |= CLK_CTRL_ISP_3A;
		writel(val, dev->base_addr + CTRL_VI_ISP_CLK_CTRL);
	}
}

static void disable_sys_clk(struct rkisp_hw_dev *dev)
{
	int i;

	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
		if (dev->mipi_irq >= 0)
			disable_irq(dev->mipi_irq);
	}

	isp_config_clk(dev, false);

	for (i = dev->num_clks - 1; i >= 0; i--)
		if (!IS_ERR(dev->clks[i]))
			clk_disable_unprepare(dev->clks[i]);
}

static int enable_sys_clk(struct rkisp_hw_dev *dev)
{
	int i, ret = -EINVAL;

	for (i = 0; i < dev->num_clks; i++) {
		if (!IS_ERR(dev->clks[i])) {
			ret = clk_prepare_enable(dev->clks[i]);
			if (ret < 0)
				goto err;
		}
	}

	rkisp_set_clk_rate(dev->clks[0],
			   dev->clk_rate_tbl[0].clk_rate * 1000000UL);
	rkisp_soft_reset(dev, false);
	isp_config_clk(dev, true);

	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
		/* disable csi_rx interrupt */
		writel(0, dev->base_addr + CIF_ISP_CSI0_CTRL0);
		writel(0, dev->base_addr + CIF_ISP_CSI0_MASK1);
		writel(0, dev->base_addr + CIF_ISP_CSI0_MASK2);
		writel(0, dev->base_addr + CIF_ISP_CSI0_MASK3);
	}

	return 0;
err:
	for (--i; i >= 0; --i)
		if (!IS_ERR(dev->clks[i]))
			clk_disable_unprepare(dev->clks[i]);
	return ret;
}

static int rkisp_hw_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct isp_match_data *match_data;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct rkisp_hw_dev *hw_dev;
	struct resource *res;
	int i, ret;

	match = of_match_node(rkisp_hw_of_match, node);
	if (IS_ERR(match))
		return PTR_ERR(match);

	hw_dev = devm_kzalloc(dev, sizeof(*hw_dev), GFP_KERNEL);
	if (!hw_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, hw_dev);
	hw_dev->dev = dev;
	hw_dev->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	dev_info(dev, "is_thunderboot: %d\n", hw_dev->is_thunderboot);
	hw_dev->max_in.w = 0;
	hw_dev->max_in.h = 0;
	hw_dev->max_in.fps = 0;
	of_property_read_u32_array(node, "max-input", &hw_dev->max_in.w, 3);
	dev_info(dev, "max input:%dx%d@%dfps\n",
		 hw_dev->max_in.w, hw_dev->max_in.h, hw_dev->max_in.fps);
	hw_dev->grf = syscon_regmap_lookup_by_phandle(node, "rockchip,grf");
	if (IS_ERR(hw_dev->grf))
		dev_warn(dev, "Missing rockchip,grf property\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "get resource failed\n");
		ret = -EINVAL;
		goto err;
	}
	hw_dev->base_addr = devm_ioremap_resource(dev, res);
	if (PTR_ERR(hw_dev->base_addr) == -EBUSY) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);

		hw_dev->base_addr = devm_ioremap(dev, offset, size);
	}
	if (IS_ERR(hw_dev->base_addr)) {
		dev_err(dev, "ioremap failed\n");
		ret = PTR_ERR(hw_dev->base_addr);
		goto err;
	}

	rkisp_monitor = device_property_read_bool(dev, "rockchip,restart-monitor-en");

	match_data = match->data;
	hw_dev->mipi_irq = -1;

	hw_dev->pdev = pdev;
	hw_dev->match_data = match_data;
	if (!hw_dev->is_thunderboot)
		rkisp_register_irq(hw_dev);

	for (i = 0; i < match_data->num_clks; i++) {
		struct clk *clk = devm_clk_get(dev, match_data->clks[i]);

		if (IS_ERR(clk))
			dev_dbg(dev, "failed to get %s\n", match_data->clks[i]);
		hw_dev->clks[i] = clk;
	}
	hw_dev->num_clks = match_data->num_clks;
	hw_dev->clk_rate_tbl = match_data->clk_rate_tbl;
	hw_dev->num_clk_rate_tbl = match_data->num_clk_rate_tbl;

	hw_dev->reset = devm_reset_control_array_get(dev, false, false);
	if (IS_ERR(hw_dev->reset)) {
		dev_dbg(dev, "failed to get reset\n");
		hw_dev->reset = NULL;
	}

	ret = of_property_read_u64(node, "rockchip,iq-feature", &hw_dev->iq_feature);
	if (!ret)
		hw_dev->is_feature_on = true;
	else
		hw_dev->is_feature_on = false;

	hw_dev->dev_num = 0;
	hw_dev->cur_dev_id = 0;
	hw_dev->mipi_dev_id = 0;
	hw_dev->isp_ver = match_data->isp_ver;
	mutex_init(&hw_dev->dev_lock);
	spin_lock_init(&hw_dev->rdbk_lock);
	atomic_set(&hw_dev->refcnt, 0);
	spin_lock_init(&hw_dev->buf_lock);
	INIT_LIST_HEAD(&hw_dev->list);
	INIT_LIST_HEAD(&hw_dev->rpt_list);
	hw_dev->buf_init_cnt = 0;
	hw_dev->is_idle = true;
	hw_dev->is_single = true;
	hw_dev->is_mi_update = false;
	hw_dev->is_dma_contig = true;
	hw_dev->is_buf_init = false;
	hw_dev->is_shutdown = false;
	hw_dev->is_mmu = is_iommu_enable(dev);
	ret = of_reserved_mem_device_init(dev);
	if (ret) {
		if (!hw_dev->is_mmu)
			dev_warn(dev, "No reserved memory region. default cma area!\n");
		else
			hw_dev->is_dma_contig = false;
	}
	if (!hw_dev->is_mmu)
		hw_dev->mem_ops = &vb2_dma_contig_memops;
	else if (!hw_dev->is_dma_contig)
		hw_dev->mem_ops = &vb2_dma_sg_memops;
	else
		hw_dev->mem_ops = &vb2_rdma_sg_memops;

	pm_runtime_enable(dev);

	return 0;
err:
	return ret;
}

static int rkisp_hw_remove(struct platform_device *pdev)
{
	struct rkisp_hw_dev *hw_dev = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	mutex_destroy(&hw_dev->dev_lock);
	return 0;
}

static void rkisp_hw_shutdown(struct platform_device *pdev)
{
	struct rkisp_hw_dev *hw_dev = platform_get_drvdata(pdev);

	hw_dev->is_shutdown = true;
	if (pm_runtime_active(&pdev->dev))
		writel(0xffff, hw_dev->base_addr + CIF_IRCL);
	dev_info(&pdev->dev, "%s\n", __func__);
}

static int __maybe_unused rkisp_runtime_suspend(struct device *dev)
{
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);

	disable_sys_clk(hw_dev);
	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused rkisp_runtime_resume(struct device *dev)
{
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	void __iomem *base = hw_dev->base_addr;
	int ret, i;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;

	enable_sys_clk(hw_dev);

	for (i = 0; i < hw_dev->dev_num; i++) {
		void *buf = hw_dev->isp[i]->sw_base_addr;

		memset(buf, 0, RKISP_ISP_SW_MAX_SIZE);
		memcpy_fromio(buf, base, RKISP_ISP_SW_REG_SIZE);
		default_sw_reg_flag(hw_dev->isp[i]);
	}
	hw_dev->monitor.is_en = rkisp_monitor;
	return 0;
}

static const struct dev_pm_ops rkisp_hw_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkisp_runtime_suspend,
			   rkisp_runtime_resume, NULL)
};

static struct platform_driver rkisp_hw_drv = {
	.driver = {
		.name = "rkisp_hw",
		.of_match_table = of_match_ptr(rkisp_hw_of_match),
		.pm = &rkisp_hw_pm_ops,
	},
	.probe = rkisp_hw_probe,
	.remove = rkisp_hw_remove,
	.shutdown = rkisp_hw_shutdown,
};

static int __init rkisp_hw_drv_init(void)
{
	int ret;

	ret = platform_driver_register(&rkisp_hw_drv);
	if (!ret)
		ret = platform_driver_register(&rkisp_plat_drv);
#if IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISP) && IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISPP)
	if (!ret)
		ret = rkispp_hw_drv_init();
#endif
	return ret;
}

module_init(rkisp_hw_drv_init);
