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
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <media/videobuf2-cma-sg.h>
#include <media/videobuf2-dma-sg.h>
#include <soc/rockchip/rockchip_iommu.h>

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
	u32 v30_reg[] = {
		ISP3X_VI_ISP_PATH, ISP3X_IMG_EFF_CTRL, ISP3X_CMSK_CTRL0,
		ISP3X_CCM_CTRL, ISP3X_CPROC_CTRL, ISP3X_DUAL_CROP_CTRL,
		ISP3X_GAMMA_OUT_CTRL, ISP3X_SELF_RESIZE_CTRL, ISP3X_MAIN_RESIZE_CTRL,
		ISP3X_LSC_CTRL, ISP3X_DEBAYER_CONTROL, ISP3X_CAC_CTRL,
		ISP3X_YNR_GLOBAL_CTRL, ISP3X_CNR_CTRL, ISP3X_SHARP_EN,
		ISP3X_BAY3D_CTRL, ISP3X_GIC_CONTROL, ISP3X_BLS_CTRL,
		ISP3X_DPCC0_MODE, ISP3X_DPCC1_MODE, ISP3X_DPCC2_MODE,
		ISP3X_HDRMGE_CTRL, ISP3X_DRC_CTRL0, ISP3X_BAYNR_CTRL,
		ISP3X_LDCH_STS, ISP3X_DHAZ_CTRL, ISP3X_3DLUT_CTRL,
		ISP3X_GAIN_CTRL, ISP3X_RAWAE_LITE_CTRL, ISP3X_RAWAE_BIG1_BASE,
		ISP3X_RAWAE_BIG2_BASE, ISP3X_RAWAE_BIG3_BASE, ISP3X_RAWHIST_LITE_CTRL,
		ISP3X_RAWHIST_BIG1_BASE, ISP3X_RAWHIST_BIG2_BASE, ISP3X_RAWHIST_BIG3_BASE,
		ISP3X_RAWAF_CTRL, ISP3X_RAWAWB_CTRL,
	};
	u32 v32_reg[] = {
		ISP3X_VI_ISP_PATH, ISP3X_IMG_EFF_CTRL, ISP3X_CMSK_CTRL0,
		ISP3X_CCM_CTRL, ISP3X_CPROC_CTRL, ISP3X_DUAL_CROP_CTRL,
		ISP3X_GAMMA_OUT_CTRL, ISP3X_SELF_RESIZE_CTRL, ISP3X_MAIN_RESIZE_CTRL,
		ISP32_BP_RESIZE_BASE, ISP3X_MI_BP_WR_CTRL, ISP32_MI_MPDS_WR_CTRL,
		ISP32_MI_BPDS_WR_CTRL, ISP32_MI_WR_WRAP_CTRL,
		ISP3X_LSC_CTRL, ISP3X_DEBAYER_CONTROL, ISP3X_CAC_CTRL,
		ISP3X_YNR_GLOBAL_CTRL, ISP3X_CNR_CTRL, ISP3X_SHARP_EN,
		ISP3X_BAY3D_CTRL, ISP3X_GIC_CONTROL, ISP3X_BLS_CTRL,
		ISP3X_DPCC0_MODE, ISP3X_DPCC1_MODE, ISP3X_DPCC2_MODE,
		ISP3X_HDRMGE_CTRL, ISP3X_DRC_CTRL0, ISP3X_BAYNR_CTRL,
		ISP3X_LDCH_STS, ISP3X_DHAZ_CTRL, ISP3X_3DLUT_CTRL,
		ISP3X_GAIN_CTRL, ISP3X_RAWAE_LITE_CTRL, ISP3X_RAWAE_BIG1_BASE,
		ISP3X_RAWAE_BIG2_BASE, ISP3X_RAWAE_BIG3_BASE, ISP3X_RAWHIST_LITE_CTRL,
		ISP3X_RAWHIST_BIG1_BASE, ISP3X_RAWHIST_BIG2_BASE, ISP3X_RAWHIST_BIG3_BASE,
		ISP3X_RAWAF_CTRL, ISP3X_RAWAWB_CTRL,
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
	case ISP_V30:
		reg = v30_reg;
		size = ARRAY_SIZE(v30_reg);
		break;
	case ISP_V32:
		reg = v32_reg;
		size = ARRAY_SIZE(v32_reg);
		break;
	default:
		return;
	}

	for (i = 0; i < size; i++) {
		flag = dev->sw_base_addr + reg[i] + RKISP_ISP_SW_REG_SIZE;
		*flag = SW_REG_CACHE;
		if (dev->hw_dev->is_unite) {
			flag += RKISP_ISP_SW_MAX_SIZE / 4;
			*flag = SW_REG_CACHE;
		}
	}
}

static irqreturn_t mipi_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	struct rkisp_device *isp = hw_dev->isp[hw_dev->mipi_dev_id];
	void __iomem *base = !hw_dev->is_unite ?
		hw_dev->base_addr : hw_dev->base_next_addr;
	ktime_t t = 0;
	s64 us;

	if (hw_dev->is_thunderboot)
		return IRQ_HANDLED;

	if (rkisp_irq_dbg)
		t = ktime_get();

	if (hw_dev->isp_ver == ISP_V13 || hw_dev->isp_ver == ISP_V12) {
		u32 err1, err2, err3;

		err1 = readl(base + CIF_ISP_CSI0_ERR1);
		err2 = readl(base + CIF_ISP_CSI0_ERR2);
		err3 = readl(base + CIF_ISP_CSI0_ERR3);

		if (err1 || err2 || err3)
			rkisp_mipi_v13_isr(err1, err2, err3, isp);
	} else if (hw_dev->isp_ver == ISP_V20 ||
		   hw_dev->isp_ver == ISP_V21 ||
		   hw_dev->isp_ver == ISP_V30 ||
		   hw_dev->isp_ver == ISP_V32) {
		u32 phy, packet, overflow, state;

		state = readl(base + CSI2RX_ERR_STAT);
		phy = readl(base + CSI2RX_ERR_PHY);
		packet = readl(base + CSI2RX_ERR_PACKET);
		overflow = readl(base + CSI2RX_ERR_OVERFLOW);
		if (phy | packet | overflow | state) {
			if (hw_dev->isp_ver == ISP_V20)
				rkisp_mipi_v20_isr(phy, packet, overflow, state, isp);
			else if (hw_dev->isp_ver == ISP_V21)
				rkisp_mipi_v21_isr(phy, packet, overflow, state, isp);
			else if (hw_dev->isp_ver == ISP_V30)
				rkisp_mipi_v30_isr(phy, packet, overflow, state, isp);
			else
				rkisp_mipi_v32_isr(phy, packet, overflow, state, isp);
		}
	} else {
		u32 mis_val = readl(base + CIF_MIPI_MIS);

		if (mis_val)
			rkisp_mipi_isr(mis_val, isp);
	}

	if (rkisp_irq_dbg) {
		us = ktime_us_delta(ktime_get(), t);
		v4l2_dbg(0, rkisp_debug, &isp->v4l2_dev,
			 "%s %lldus\n", __func__, us);
	}
	return IRQ_HANDLED;
}

static irqreturn_t mi_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	struct rkisp_device *isp = hw_dev->isp[hw_dev->cur_dev_id];
	void __iomem *base = !hw_dev->is_unite ?
		hw_dev->base_addr : hw_dev->base_next_addr;
	u32 mis_val, tx_isr = MI_RAW0_WR_FRAME | MI_RAW1_WR_FRAME |
		MI_RAW2_WR_FRAME | MI_RAW3_WR_FRAME;
	ktime_t t = 0;
	s64 us;

	if (hw_dev->is_thunderboot)
		return IRQ_HANDLED;

	if (rkisp_irq_dbg)
		t = ktime_get();

	mis_val = readl(base + CIF_MI_MIS);
	if (mis_val) {
		if (mis_val & ~tx_isr)
			rkisp_mi_isr(mis_val & ~tx_isr, isp);
		if (mis_val & tx_isr) {
			isp = hw_dev->isp[hw_dev->mipi_dev_id];
			rkisp_mi_isr(mis_val & tx_isr, isp);
		}
	}

	if (rkisp_irq_dbg) {
		us = ktime_us_delta(ktime_get(), t);
		v4l2_dbg(0, rkisp_debug, &isp->v4l2_dev,
			 "%s:0x%x %lldus\n", __func__, mis_val, us);
	}
	return IRQ_HANDLED;
}

static irqreturn_t isp_irq_hdl(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	struct rkisp_device *isp = hw_dev->isp[hw_dev->cur_dev_id];
	void __iomem *base = !hw_dev->is_unite ?
		hw_dev->base_addr : hw_dev->base_next_addr;
	unsigned int mis_val, mis_3a = 0;
	ktime_t t = 0;
	s64 us;

	if (hw_dev->is_thunderboot)
		return IRQ_HANDLED;

	if (rkisp_irq_dbg)
		t = ktime_get();

	mis_val = readl(base + CIF_ISP_MIS);
	if (hw_dev->isp_ver == ISP_V20 ||
	    hw_dev->isp_ver == ISP_V21 ||
	    hw_dev->isp_ver == ISP_V30 ||
	    hw_dev->isp_ver == ISP_V32)
		mis_3a = readl(base + ISP_ISP3A_MIS);
	if (mis_val || mis_3a)
		rkisp_isp_isr(mis_val, mis_3a, isp);

	if (rkisp_irq_dbg) {
		us = ktime_us_delta(ktime_get(), t);
		v4l2_dbg(0, rkisp_debug, &isp->v4l2_dev,
			 "%s:0x%x %lldus\n", __func__, mis_val, us);
	}
	return IRQ_HANDLED;
}

static irqreturn_t irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	struct rkisp_device *isp = hw_dev->isp[hw_dev->cur_dev_id];
	unsigned int mis_val, mis_3a = 0;

	mis_val = readl(hw_dev->base_addr + CIF_ISP_MIS);
	if (hw_dev->isp_ver == ISP_V20 ||
	    hw_dev->isp_ver == ISP_V21 ||
	    hw_dev->isp_ver == ISP_V30 ||
	    hw_dev->isp_ver == ISP_V32)
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

static const char * const rk3568_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
};

static const char * const rk3588_isp_clks[] = {
	"clk_isp_core",
	"aclk_isp",
	"hclk_isp",
	"clk_isp_core_marvin",
	"clk_isp_core_vicap",
};

static const char * const rk3588_isp_unite_clks[] = {
	"clk_isp_core0",
	"aclk_isp0",
	"hclk_isp0",
	"clk_isp_core_marvin0",
	"clk_isp_core_vicap0",
	"clk_isp_core1",
	"aclk_isp1",
	"hclk_isp1",
	"clk_isp_core_marvin1",
	"clk_isp_core_vicap1",
};

static const char * const rv1106_isp_clks[] = {
	"clk_isp_core",
	"aclk_isp",
	"hclk_isp",
	"clk_isp_core_vicap",
};

static const char * const rv1126_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
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

static const struct isp_clk_info rk3588_isp_clk_rate[] = {
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
	}, {
		.clk_rate = 702,
		.refer_data = 4672,
	}
};

static const struct isp_clk_info rv1106_isp_clk_rate[] = {
	{
		.clk_rate = 200,
		.refer_data = 1920, //width
	}, {
		.clk_rate = 200,
		.refer_data = 2688,
	}, {
		.clk_rate = 350,
		.refer_data = 3072,
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

static struct isp_irqs_data rk3568_isp_irqs[] = {
	{"isp_irq", isp_irq_hdl},
	{"mi_irq", mi_irq_hdl},
	{"mipi_irq", mipi_irq_hdl}
};

static struct isp_irqs_data rk3588_isp_irqs[] = {
	{"isp_irq", isp_irq_hdl},
	{"mi_irq", mi_irq_hdl},
	{"mipi_irq", mipi_irq_hdl}
};

static struct isp_irqs_data rv1106_isp_irqs[] = {
	{"isp_irq", isp_irq_hdl},
	{"mi_irq", mi_irq_hdl},
	{"mipi_irq", mipi_irq_hdl}
};

static struct isp_irqs_data rv1126_isp_irqs[] = {
	{"isp_irq", isp_irq_hdl},
	{"mi_irq", mi_irq_hdl},
	{"mipi_irq", mipi_irq_hdl}
};

static const struct isp_match_data rv1106_isp_match_data = {
	.clks = rv1106_isp_clks,
	.num_clks = ARRAY_SIZE(rv1106_isp_clks),
	.isp_ver = ISP_V32,
	.clk_rate_tbl = rv1106_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rv1106_isp_clk_rate),
	.irqs = rv1106_isp_irqs,
	.num_irqs = ARRAY_SIZE(rv1106_isp_irqs),
	.unite = false,
};

static const struct isp_match_data rv1126_isp_match_data = {
	.clks = rv1126_isp_clks,
	.num_clks = ARRAY_SIZE(rv1126_isp_clks),
	.isp_ver = ISP_V20,
	.clk_rate_tbl = rv1126_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rv1126_isp_clk_rate),
	.irqs = rv1126_isp_irqs,
	.num_irqs = ARRAY_SIZE(rv1126_isp_irqs),
	.unite = false,
};

static const struct isp_match_data rk3568_isp_match_data = {
	.clks = rk3568_isp_clks,
	.num_clks = ARRAY_SIZE(rk3568_isp_clks),
	.isp_ver = ISP_V21,
	.clk_rate_tbl = rk3568_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3568_isp_clk_rate),
	.irqs = rk3568_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3568_isp_irqs),
	.unite = false,
};

static const struct isp_match_data rk3588_isp_match_data = {
	.clks = rk3588_isp_clks,
	.num_clks = ARRAY_SIZE(rk3588_isp_clks),
	.isp_ver = ISP_V30,
	.clk_rate_tbl = rk3588_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3588_isp_clk_rate),
	.irqs = rk3588_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3588_isp_irqs),
	.unite = false,
};

static const struct isp_match_data rk3588_isp_unite_match_data = {
	.clks = rk3588_isp_unite_clks,
	.num_clks = ARRAY_SIZE(rk3588_isp_unite_clks),
	.isp_ver = ISP_V30,
	.clk_rate_tbl = rk3588_isp_clk_rate,
	.num_clk_rate_tbl = ARRAY_SIZE(rk3588_isp_clk_rate),
	.irqs = rk3588_isp_irqs,
	.num_irqs = ARRAY_SIZE(rk3588_isp_irqs),
	.unite = true,
};

static const struct of_device_id rkisp_hw_of_match[] = {
#ifdef CONFIG_CPU_RK3568
	{
		.compatible = "rockchip,rk3568-rkisp",
		.data = &rk3568_isp_match_data,
	},
#endif
#ifdef CONFIG_CPU_RK3588
	{
		.compatible = "rockchip,rk3588-rkisp",
		.data = &rk3588_isp_match_data,
	}, {
		.compatible = "rockchip,rk3588-rkisp-unite",
		.data = &rk3588_isp_unite_match_data,
	},
#endif
#ifdef CONFIG_CPU_RV1106
	{
		.compatible = "rockchip,rv1106-rkisp",
		.data = &rv1106_isp_match_data,
	},
#endif
#ifdef CONFIG_CPU_RV1126
	{
		.compatible = "rockchip,rv1126-rkisp",
		.data = &rv1126_isp_match_data,
	},
#endif
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
	u32 val, iccl0, iccl1, clk_ctrl0, clk_ctrl1;

	/* record clk config and recover */
	iccl0 = readl(base + CIF_ICCL);
	clk_ctrl0 = readl(base + CTRL_VI_ISP_CLK_CTRL);
	if (dev->is_unite) {
		iccl1 = readl(dev->base_next_addr + CIF_ICCL);
		clk_ctrl1 = readl(dev->base_next_addr + CTRL_VI_ISP_CLK_CTRL);
	}

	if (is_secure) {
		/* if isp working, cru reset isn't secure.
		 * isp soft reset first to protect isp reset.
		 */
		writel(0xffff, base + CIF_IRCL);
		if (dev->is_unite)
			writel(0xffff, dev->base_next_addr + CIF_IRCL);
		udelay(10);
	}

	if (dev->reset) {
		reset_control_assert(dev->reset);
		udelay(10);
		reset_control_deassert(dev->reset);
		udelay(10);
	}

	/* reset for Dehaze */
	if (dev->isp_ver == ISP_V20)
		writel(CIF_ISP_CTRL_ISP_MODE_BAYER_ITU601, base + CIF_ISP_CTRL);
	val = 0xffff;
	if (dev->isp_ver == ISP_V32) {
		val = 0x3fffffff;
		rv1106_sdmmc_get_lock();
	}
	writel(val, base + CIF_IRCL);
	if (dev->isp_ver == ISP_V32)
		rv1106_sdmmc_put_lock();
	if (dev->is_unite)
		writel(0xffff, dev->base_next_addr + CIF_IRCL);
	udelay(10);

	/* refresh iommu after reset */
	if (dev->is_mmu) {
		rockchip_iommu_disable(dev->dev);
		rockchip_iommu_enable(dev->dev);
	}

	writel(iccl0, base + CIF_ICCL);
	writel(clk_ctrl0, base + CTRL_VI_ISP_CLK_CTRL);
	if (dev->is_unite) {
		writel(iccl1, dev->base_next_addr + CIF_ICCL);
		writel(clk_ctrl1, dev->base_next_addr + CTRL_VI_ISP_CLK_CTRL);
	}

	/* default config */
	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
		/* disable csi_rx interrupt */
		writel(0, dev->base_addr + CIF_ISP_CSI0_CTRL0);
		writel(0, dev->base_addr + CIF_ISP_CSI0_MASK1);
		writel(0, dev->base_addr + CIF_ISP_CSI0_MASK2);
		writel(0, dev->base_addr + CIF_ISP_CSI0_MASK3);
	} else if (dev->isp_ver == ISP_V32) {
		/* disable down samplling default */
		writel(ISP32_DS_DS_DIS, dev->base_addr + ISP32_MI_MPDS_WR_CTRL);
		writel(ISP32_DS_DS_DIS, dev->base_addr + ISP32_MI_BPDS_WR_CTRL);

		writel(0, dev->base_addr + ISP32_BLS_ISP_OB_PREDGAIN);
		writel(0x37, dev->base_addr + ISP32_MI_WR_WRAP_CTRL);
	}
}

static void isp_config_clk(struct rkisp_hw_dev *dev, int on)
{
	u32 val = !on ? 0 :
		CIF_ICCL_ISP_CLK | CIF_ICCL_CP_CLK | CIF_ICCL_MRSZ_CLK |
		CIF_ICCL_SRSZ_CLK | CIF_ICCL_JPEG_CLK | CIF_ICCL_MI_CLK |
		CIF_ICCL_IE_CLK | CIF_ICCL_MIPI_CLK | CIF_ICCL_DCROP_CLK;

	if ((dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V30 || dev->isp_ver == ISP_V32) && on)
		val |= ICCL_MPFBC_CLK;
	if (dev->isp_ver == ISP_V32) {
		val |= ISP32_BRSZ_CLK_ENABLE | BIT(0) | BIT(16);
		rv1106_sdmmc_get_lock();
	}
	writel(val, dev->base_addr + CIF_ICCL);
	if (dev->isp_ver == ISP_V32)
		rv1106_sdmmc_put_lock();
	if (dev->is_unite)
		writel(val, dev->base_next_addr + CIF_ICCL);

	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
		val = !on ? 0 :
		      CIF_CLK_CTRL_MI_Y12 | CIF_CLK_CTRL_MI_SP |
		      CIF_CLK_CTRL_MI_RAW0 | CIF_CLK_CTRL_MI_RAW1 |
		      CIF_CLK_CTRL_MI_READ | CIF_CLK_CTRL_MI_RAWRD |
		      CIF_CLK_CTRL_CP | CIF_CLK_CTRL_IE;

		writel(val, dev->base_addr + CIF_VI_ISP_CLK_CTRL_V12);
	} else if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21 ||
		   dev->isp_ver == ISP_V30 || dev->isp_ver == ISP_V32) {
		val = !on ? 0 :
		      CLK_CTRL_MI_LDC | CLK_CTRL_MI_MP |
		      CLK_CTRL_MI_JPEG | CLK_CTRL_MI_DP |
		      CLK_CTRL_MI_Y12 | CLK_CTRL_MI_SP |
		      CLK_CTRL_MI_RAW0 | CLK_CTRL_MI_RAW1 |
		      CLK_CTRL_MI_READ | CLK_CTRL_MI_RAWRD |
		      CLK_CTRL_ISP_RAW;

		if (dev->isp_ver == ISP_V30 || dev->isp_ver == ISP_V32)
			val = 0;

		if ((dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V30) && on)
			val |= CLK_CTRL_ISP_3A;
		if (dev->isp_ver == ISP_V32)
			rv1106_sdmmc_get_lock();
		writel(val, dev->base_addr + CTRL_VI_ISP_CLK_CTRL);
		if (dev->isp_ver == ISP_V32)
			rv1106_sdmmc_put_lock();
		if (dev->is_unite)
			writel(val, dev->base_next_addr + CTRL_VI_ISP_CLK_CTRL);
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
	unsigned long rate;

	for (i = 0; i < dev->num_clks; i++) {
		if (!IS_ERR(dev->clks[i])) {
			ret = clk_prepare_enable(dev->clks[i]);
			if (ret < 0)
				goto err;
		}
	}

	rate = dev->clk_rate_tbl[0].clk_rate * 1000000UL;
	rkisp_set_clk_rate(dev->clks[0], rate);
	if (dev->is_unite)
		rkisp_set_clk_rate(dev->clks[5], rate);
	rkisp_soft_reset(dev, false);
	isp_config_clk(dev, true);
	return 0;
err:
	for (--i; i >= 0; --i)
		if (!IS_ERR(dev->clks[i]))
			clk_disable_unprepare(dev->clks[i]);
	return ret;
}

static int rkisp_get_sram(struct rkisp_hw_dev *hw_dev)
{
	struct device *dev = hw_dev->dev;
	struct rkisp_sram *sram = &hw_dev->sram;
	struct device_node *np;
	struct resource res;
	int ret, size;

	sram->size = 0;
	np = of_parse_phandle(dev->of_node, "rockchip,sram", 0);
	if (!np) {
		dev_warn(dev, "no find phandle sram\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(np, 0, &res);
	of_node_put(np);
	if (ret) {
		dev_err(dev, "get sram res error\n");
		return ret;
	}
	size = resource_size(&res);
	sram->dma_addr = dma_map_resource(dev, res.start, size, DMA_BIDIRECTIONAL, 0);
	if (dma_mapping_error(dev, sram->dma_addr))
		return -ENOMEM;
	sram->size = size;
	dev_info(dev, "get sram size:%d\n", size);
	return 0;
}

static void rkisp_put_sram(struct rkisp_hw_dev *hw_dev)
{
	if (hw_dev->sram.size)
		dma_unmap_resource(hw_dev->dev, hw_dev->sram.dma_addr,
				   hw_dev->sram.size, DMA_BIDIRECTIONAL, 0);
	hw_dev->sram.size = 0;
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
	bool is_mem_reserved = true;

	match = of_match_node(rkisp_hw_of_match, node);
	if (IS_ERR(match))
		return PTR_ERR(match);

	hw_dev = devm_kzalloc(dev, sizeof(*hw_dev), GFP_KERNEL);
	if (!hw_dev)
		return -ENOMEM;

	match_data = match->data;
	hw_dev->is_unite = match_data->unite;
	dev_set_drvdata(dev, hw_dev);
	hw_dev->dev = dev;
	hw_dev->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	dev_info(dev, "is_thunderboot: %d\n", hw_dev->is_thunderboot);
	memset(&hw_dev->max_in, 0, sizeof(hw_dev->max_in));
	if (!of_property_read_u32_array(node, "max-input", &hw_dev->max_in.w, 3)) {
		hw_dev->max_in.is_fix = true;
		if (hw_dev->is_unite) {
			hw_dev->max_in.w /= 2;
			hw_dev->max_in.w += RKMOUDLE_UNITE_EXTEND_PIXEL;
		}
	}
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

	hw_dev->base_next_addr = NULL;
	if (match_data->unite) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!res) {
			dev_err(dev, "get next resource failed\n");
			ret = -EINVAL;
			goto err;
		}
		hw_dev->base_next_addr = devm_ioremap_resource(dev, res);
		if (PTR_ERR(hw_dev->base_next_addr) == -EBUSY) {
			resource_size_t offset = res->start;
			resource_size_t size = resource_size(res);

			hw_dev->base_next_addr = devm_ioremap(dev, offset, size);
		}

		if (IS_ERR(hw_dev->base_next_addr)) {
			dev_err(dev, "ioremap next failed\n");
			ret = PTR_ERR(hw_dev->base_next_addr);
			goto err;
		}
	}

	rkisp_monitor = device_property_read_bool(dev, "rockchip,restart-monitor-en");
	hw_dev->mipi_irq = -1;

	hw_dev->pdev = pdev;
	hw_dev->match_data = match_data;
	if (!hw_dev->is_thunderboot)
		rkisp_register_irq(hw_dev);

	for (i = 0; i < match_data->num_clks; i++) {
		struct clk *clk = devm_clk_get(dev, match_data->clks[i]);

		if (IS_ERR(clk)) {
			dev_err(dev, "failed to get %s\n", match_data->clks[i]);
			ret = PTR_ERR(clk);
			goto err;
		}
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

	rkisp_get_sram(hw_dev);

	hw_dev->dev_num = 0;
	hw_dev->dev_link_num = 0;
	hw_dev->cur_dev_id = 0;
	hw_dev->mipi_dev_id = 0;
	hw_dev->pre_dev_id = 0;
	hw_dev->is_multi_overflow = false;
	hw_dev->isp_ver = match_data->isp_ver;
	hw_dev->is_unite = match_data->unite;
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
	hw_dev->is_dma_sg_ops = true;
	hw_dev->is_buf_init = false;
	hw_dev->is_shutdown = false;
	hw_dev->is_mmu = is_iommu_enable(dev);
	ret = of_reserved_mem_device_init(dev);
	if (ret) {
		is_mem_reserved = false;
		if (!hw_dev->is_mmu)
			dev_info(dev, "No reserved memory region. default cma area!\n");
	}
	if (hw_dev->is_mmu && !is_mem_reserved)
		hw_dev->is_dma_contig = false;
	hw_dev->mem_ops = &vb2_cma_sg_memops;

	pm_runtime_enable(dev);

	return 0;
err:
	return ret;
}

static int rkisp_hw_remove(struct platform_device *pdev)
{
	struct rkisp_hw_dev *hw_dev = platform_get_drvdata(pdev);

	rkisp_put_sram(hw_dev);
	pm_runtime_disable(&pdev->dev);
	mutex_destroy(&hw_dev->dev_lock);
	return 0;
}

static void rkisp_hw_shutdown(struct platform_device *pdev)
{
	struct rkisp_hw_dev *hw_dev = platform_get_drvdata(pdev);

	hw_dev->is_shutdown = true;
	if (pm_runtime_active(&pdev->dev)) {
		writel(0xffff, hw_dev->base_addr + CIF_IRCL);
		if (hw_dev->is_unite)
			writel(0xffff, hw_dev->base_next_addr + CIF_IRCL);
	}
	dev_info(&pdev->dev, "%s\n", __func__);
}

static int __maybe_unused rkisp_runtime_suspend(struct device *dev)
{
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);

	hw_dev->dev_link_num = 0;
	hw_dev->is_single = true;
	hw_dev->is_multi_overflow = false;
	disable_sys_clk(hw_dev);
	return pinctrl_pm_select_sleep_state(dev);
}

void rkisp_hw_enum_isp_size(struct rkisp_hw_dev *hw_dev)
{
	struct rkisp_device *isp;
	u32 w, h, i;

	memset(hw_dev->isp_size, 0, sizeof(hw_dev->isp_size));
	if (!hw_dev->max_in.is_fix) {
		hw_dev->max_in.w = 0;
		hw_dev->max_in.h = 0;
	}
	hw_dev->dev_link_num = 0;
	for (i = 0; i < hw_dev->dev_num; i++) {
		isp = hw_dev->isp[i];
		if (!isp || (isp && !isp->is_hw_link))
			continue;
		if (hw_dev->dev_link_num++)
			hw_dev->is_single = false;
		w = isp->isp_sdev.in_crop.width;
		h = isp->isp_sdev.in_crop.height;
		if (hw_dev->is_unite)
			w = w / 2 + RKMOUDLE_UNITE_EXTEND_PIXEL;
		hw_dev->isp_size[i].w = w;
		hw_dev->isp_size[i].h = h;
		hw_dev->isp_size[i].size = w * h;
		if (!hw_dev->max_in.is_fix) {
			if (hw_dev->max_in.w < w)
				hw_dev->max_in.w = w;
			if (hw_dev->max_in.h < h)
				hw_dev->max_in.h = h;
		}
	}
	for (i = 0; i < hw_dev->dev_num; i++) {
		isp = hw_dev->isp[i];
		if (!isp || (isp && !isp->is_hw_link))
			continue;
		rkisp_params_check_bigmode(&isp->params_vdev);
	}
}

static int __maybe_unused rkisp_runtime_resume(struct device *dev)
{
	struct rkisp_hw_dev *hw_dev = dev_get_drvdata(dev);
	void __iomem *base = hw_dev->base_addr;
	struct rkisp_device *isp;
	int mult = hw_dev->is_unite ? 2 : 1;
	int ret, i;
	void *buf;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;

	enable_sys_clk(hw_dev);
	for (i = 0; i < hw_dev->dev_num; i++) {
		isp = hw_dev->isp[i];
		if (!isp)
			continue;
		buf = isp->sw_base_addr;
		memset(buf, 0, RKISP_ISP_SW_MAX_SIZE * mult);
		memcpy_fromio(buf, base, RKISP_ISP_SW_REG_SIZE);
		if (hw_dev->is_unite) {
			buf += RKISP_ISP_SW_MAX_SIZE;
			base = hw_dev->base_next_addr;
			memcpy_fromio(buf, base, RKISP_ISP_SW_REG_SIZE);
		}
		default_sw_reg_flag(hw_dev->isp[i]);
	}
	rkisp_hw_enum_isp_size(hw_dev);
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

static void __exit rkisp_hw_drv_exit(void)
{
	platform_driver_unregister(&rkisp_plat_drv);
	platform_driver_unregister(&rkisp_hw_drv);
}

#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
subsys_initcall(rkisp_hw_drv_init);
#else
module_init(rkisp_hw_drv_init);
#endif
module_exit(rkisp_hw_drv_exit);
