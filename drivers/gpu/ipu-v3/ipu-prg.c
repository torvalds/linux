// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2017 Lucas Stach, Pengutronix
 */

#include <drm/drm_fourcc.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <video/imx-ipu-v3.h>

#include "ipu-prv.h"

#define IPU_PRG_CTL				0x00
#define  IPU_PRG_CTL_BYPASS(i)			(1 << (0 + i))
#define  IPU_PRG_CTL_SOFT_ARID_MASK		0x3
#define  IPU_PRG_CTL_SOFT_ARID_SHIFT(i)		(8 + i * 2)
#define  IPU_PRG_CTL_SOFT_ARID(i, v)		((v & 0x3) << (8 + 2 * i))
#define  IPU_PRG_CTL_SO(i)			(1 << (16 + i))
#define  IPU_PRG_CTL_VFLIP(i)			(1 << (19 + i))
#define  IPU_PRG_CTL_BLOCK_MODE(i)		(1 << (22 + i))
#define  IPU_PRG_CTL_CNT_LOAD_EN(i)		(1 << (25 + i))
#define  IPU_PRG_CTL_SOFTRST			(1 << 30)
#define  IPU_PRG_CTL_SHADOW_EN			(1 << 31)

#define IPU_PRG_STATUS				0x04
#define  IPU_PRG_STATUS_BUFFER0_READY(i)	(1 << (0 + i * 2))
#define  IPU_PRG_STATUS_BUFFER1_READY(i)	(1 << (1 + i * 2))

#define IPU_PRG_QOS				0x08
#define  IPU_PRG_QOS_ARID_MASK			0xf
#define  IPU_PRG_QOS_ARID_SHIFT(i)		(0 + i * 4)

#define IPU_PRG_REG_UPDATE			0x0c
#define  IPU_PRG_REG_UPDATE_REG_UPDATE		(1 << 0)

#define IPU_PRG_STRIDE(i)			(0x10 + i * 0x4)
#define  IPU_PRG_STRIDE_STRIDE_MASK		0x3fff

#define IPU_PRG_CROP_LINE			0x1c

#define IPU_PRG_THD				0x20

#define IPU_PRG_BADDR(i)			(0x24 + i * 0x4)

#define IPU_PRG_OFFSET(i)			(0x30 + i * 0x4)

#define IPU_PRG_ILO(i)				(0x3c + i * 0x4)

#define IPU_PRG_HEIGHT(i)			(0x48 + i * 0x4)
#define  IPU_PRG_HEIGHT_PRE_HEIGHT_MASK		0xfff
#define  IPU_PRG_HEIGHT_PRE_HEIGHT_SHIFT	0
#define  IPU_PRG_HEIGHT_IPU_HEIGHT_MASK		0xfff
#define  IPU_PRG_HEIGHT_IPU_HEIGHT_SHIFT	16

struct ipu_prg_channel {
	bool			enabled;
	int			used_pre;
};

struct ipu_prg {
	struct list_head	list;
	struct device		*dev;
	int			id;

	void __iomem		*regs;
	struct clk		*clk_ipg, *clk_axi;
	struct regmap		*iomuxc_gpr;
	struct ipu_pre		*pres[3];

	struct ipu_prg_channel	chan[3];
};

static DEFINE_MUTEX(ipu_prg_list_mutex);
static LIST_HEAD(ipu_prg_list);

struct ipu_prg *
ipu_prg_lookup_by_phandle(struct device *dev, const char *name, int ipu_id)
{
	struct device_node *prg_node = of_parse_phandle(dev->of_node,
							name, 0);
	struct ipu_prg *prg;

	mutex_lock(&ipu_prg_list_mutex);
	list_for_each_entry(prg, &ipu_prg_list, list) {
		if (prg_node == prg->dev->of_node) {
			mutex_unlock(&ipu_prg_list_mutex);
			device_link_add(dev, prg->dev,
					DL_FLAG_AUTOREMOVE_CONSUMER);
			prg->id = ipu_id;
			of_node_put(prg_node);
			return prg;
		}
	}
	mutex_unlock(&ipu_prg_list_mutex);

	of_node_put(prg_node);

	return NULL;
}

int ipu_prg_max_active_channels(void)
{
	return ipu_pre_get_available_count();
}
EXPORT_SYMBOL_GPL(ipu_prg_max_active_channels);

bool ipu_prg_present(struct ipu_soc *ipu)
{
	if (ipu->prg_priv)
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(ipu_prg_present);

bool ipu_prg_format_supported(struct ipu_soc *ipu, uint32_t format,
			      uint64_t modifier)
{
	const struct drm_format_info *info = drm_format_info(format);

	if (info->num_planes != 1)
		return false;

	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case DRM_FORMAT_MOD_VIVANTE_TILED:
	case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(ipu_prg_format_supported);

int ipu_prg_enable(struct ipu_soc *ipu)
{
	struct ipu_prg *prg = ipu->prg_priv;

	if (!prg)
		return 0;

	return pm_runtime_get_sync(prg->dev);
}
EXPORT_SYMBOL_GPL(ipu_prg_enable);

void ipu_prg_disable(struct ipu_soc *ipu)
{
	struct ipu_prg *prg = ipu->prg_priv;

	if (!prg)
		return;

	pm_runtime_put(prg->dev);
}
EXPORT_SYMBOL_GPL(ipu_prg_disable);

/*
 * The channel configuartion functions below are not thread safe, as they
 * must be only called from the atomic commit path in the DRM driver, which
 * is properly serialized.
 */
static int ipu_prg_ipu_to_prg_chan(int ipu_chan)
{
	/*
	 * This isn't clearly documented in the RM, but IPU to PRG channel
	 * assignment is fixed, as only with this mapping the control signals
	 * match up.
	 */
	switch (ipu_chan) {
	case IPUV3_CHANNEL_MEM_BG_SYNC:
		return 0;
	case IPUV3_CHANNEL_MEM_FG_SYNC:
		return 1;
	case IPUV3_CHANNEL_MEM_DC_SYNC:
		return 2;
	default:
		return -EINVAL;
	}
}

static int ipu_prg_get_pre(struct ipu_prg *prg, int prg_chan)
{
	int i, ret;

	/* channel 0 is special as it is hardwired to one of the PREs */
	if (prg_chan == 0) {
		ret = ipu_pre_get(prg->pres[0]);
		if (ret)
			goto fail;
		prg->chan[prg_chan].used_pre = 0;
		return 0;
	}

	for (i = 1; i < 3; i++) {
		ret = ipu_pre_get(prg->pres[i]);
		if (!ret) {
			u32 val, mux;
			int shift;

			prg->chan[prg_chan].used_pre = i;

			/* configure the PRE to PRG channel mux */
			shift = (i == 1) ? 12 : 14;
			mux = (prg->id << 1) | (prg_chan - 1);
			regmap_update_bits(prg->iomuxc_gpr, IOMUXC_GPR5,
					   0x3 << shift, mux << shift);

			/* check other mux, must not point to same channel */
			shift = (i == 1) ? 14 : 12;
			regmap_read(prg->iomuxc_gpr, IOMUXC_GPR5, &val);
			if (((val >> shift) & 0x3) == mux) {
				regmap_update_bits(prg->iomuxc_gpr, IOMUXC_GPR5,
						   0x3 << shift,
						   (mux ^ 0x1) << shift);
			}

			return 0;
		}
	}

fail:
	dev_err(prg->dev, "could not get PRE for PRG chan %d", prg_chan);
	return ret;
}

static void ipu_prg_put_pre(struct ipu_prg *prg, int prg_chan)
{
	struct ipu_prg_channel *chan = &prg->chan[prg_chan];

	ipu_pre_put(prg->pres[chan->used_pre]);
	chan->used_pre = -1;
}

void ipu_prg_channel_disable(struct ipuv3_channel *ipu_chan)
{
	int prg_chan = ipu_prg_ipu_to_prg_chan(ipu_chan->num);
	struct ipu_prg *prg = ipu_chan->ipu->prg_priv;
	struct ipu_prg_channel *chan;
	u32 val;

	if (prg_chan < 0)
		return;

	chan = &prg->chan[prg_chan];
	if (!chan->enabled)
		return;

	pm_runtime_get_sync(prg->dev);

	val = readl(prg->regs + IPU_PRG_CTL);
	val |= IPU_PRG_CTL_BYPASS(prg_chan);
	writel(val, prg->regs + IPU_PRG_CTL);

	val = IPU_PRG_REG_UPDATE_REG_UPDATE;
	writel(val, prg->regs + IPU_PRG_REG_UPDATE);

	pm_runtime_put(prg->dev);

	ipu_prg_put_pre(prg, prg_chan);

	chan->enabled = false;
}
EXPORT_SYMBOL_GPL(ipu_prg_channel_disable);

int ipu_prg_channel_configure(struct ipuv3_channel *ipu_chan,
			      unsigned int axi_id, unsigned int width,
			      unsigned int height, unsigned int stride,
			      u32 format, uint64_t modifier, unsigned long *eba)
{
	int prg_chan = ipu_prg_ipu_to_prg_chan(ipu_chan->num);
	struct ipu_prg *prg = ipu_chan->ipu->prg_priv;
	struct ipu_prg_channel *chan;
	u32 val;
	int ret;

	if (prg_chan < 0)
		return prg_chan;

	chan = &prg->chan[prg_chan];

	if (chan->enabled) {
		ipu_pre_update(prg->pres[chan->used_pre], *eba);
		return 0;
	}

	ret = ipu_prg_get_pre(prg, prg_chan);
	if (ret)
		return ret;

	ipu_pre_configure(prg->pres[chan->used_pre],
			  width, height, stride, format, modifier, *eba);


	pm_runtime_get_sync(prg->dev);

	val = (stride - 1) & IPU_PRG_STRIDE_STRIDE_MASK;
	writel(val, prg->regs + IPU_PRG_STRIDE(prg_chan));

	val = ((height & IPU_PRG_HEIGHT_PRE_HEIGHT_MASK) <<
	       IPU_PRG_HEIGHT_PRE_HEIGHT_SHIFT) |
	      ((height & IPU_PRG_HEIGHT_IPU_HEIGHT_MASK) <<
	       IPU_PRG_HEIGHT_IPU_HEIGHT_SHIFT);
	writel(val, prg->regs + IPU_PRG_HEIGHT(prg_chan));

	val = ipu_pre_get_baddr(prg->pres[chan->used_pre]);
	*eba = val;
	writel(val, prg->regs + IPU_PRG_BADDR(prg_chan));

	val = readl(prg->regs + IPU_PRG_CTL);
	/* config AXI ID */
	val &= ~(IPU_PRG_CTL_SOFT_ARID_MASK <<
		 IPU_PRG_CTL_SOFT_ARID_SHIFT(prg_chan));
	val |= IPU_PRG_CTL_SOFT_ARID(prg_chan, axi_id);
	/* enable channel */
	val &= ~IPU_PRG_CTL_BYPASS(prg_chan);
	writel(val, prg->regs + IPU_PRG_CTL);

	val = IPU_PRG_REG_UPDATE_REG_UPDATE;
	writel(val, prg->regs + IPU_PRG_REG_UPDATE);

	/* wait for both double buffers to be filled */
	readl_poll_timeout(prg->regs + IPU_PRG_STATUS, val,
			   (val & IPU_PRG_STATUS_BUFFER0_READY(prg_chan)) &&
			   (val & IPU_PRG_STATUS_BUFFER1_READY(prg_chan)),
			   5, 1000);

	pm_runtime_put(prg->dev);

	chan->enabled = true;
	return 0;
}
EXPORT_SYMBOL_GPL(ipu_prg_channel_configure);

bool ipu_prg_channel_configure_pending(struct ipuv3_channel *ipu_chan)
{
	int prg_chan = ipu_prg_ipu_to_prg_chan(ipu_chan->num);
	struct ipu_prg *prg = ipu_chan->ipu->prg_priv;
	struct ipu_prg_channel *chan;

	if (prg_chan < 0)
		return false;

	chan = &prg->chan[prg_chan];
	WARN_ON(!chan->enabled);

	return ipu_pre_update_pending(prg->pres[chan->used_pre]);
}
EXPORT_SYMBOL_GPL(ipu_prg_channel_configure_pending);

static int ipu_prg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ipu_prg *prg;
	u32 val;
	int i, ret;

	prg = devm_kzalloc(dev, sizeof(*prg), GFP_KERNEL);
	if (!prg)
		return -ENOMEM;

	prg->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(prg->regs))
		return PTR_ERR(prg->regs);

	prg->clk_ipg = devm_clk_get(dev, "ipg");
	if (IS_ERR(prg->clk_ipg))
		return PTR_ERR(prg->clk_ipg);

	prg->clk_axi = devm_clk_get(dev, "axi");
	if (IS_ERR(prg->clk_axi))
		return PTR_ERR(prg->clk_axi);

	prg->iomuxc_gpr =
		syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (IS_ERR(prg->iomuxc_gpr))
		return PTR_ERR(prg->iomuxc_gpr);

	for (i = 0; i < 3; i++) {
		prg->pres[i] = ipu_pre_lookup_by_phandle(dev, "fsl,pres", i);
		if (!prg->pres[i])
			return -EPROBE_DEFER;
	}

	ret = clk_prepare_enable(prg->clk_ipg);
	if (ret)
		return ret;

	ret = clk_prepare_enable(prg->clk_axi);
	if (ret) {
		clk_disable_unprepare(prg->clk_ipg);
		return ret;
	}

	/* init to free running mode */
	val = readl(prg->regs + IPU_PRG_CTL);
	val |= IPU_PRG_CTL_SHADOW_EN;
	writel(val, prg->regs + IPU_PRG_CTL);

	/* disable address threshold */
	writel(0xffffffff, prg->regs + IPU_PRG_THD);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	prg->dev = dev;
	platform_set_drvdata(pdev, prg);
	mutex_lock(&ipu_prg_list_mutex);
	list_add(&prg->list, &ipu_prg_list);
	mutex_unlock(&ipu_prg_list_mutex);

	return 0;
}

static int ipu_prg_remove(struct platform_device *pdev)
{
	struct ipu_prg *prg = platform_get_drvdata(pdev);

	mutex_lock(&ipu_prg_list_mutex);
	list_del(&prg->list);
	mutex_unlock(&ipu_prg_list_mutex);

	return 0;
}

#ifdef CONFIG_PM
static int prg_suspend(struct device *dev)
{
	struct ipu_prg *prg = dev_get_drvdata(dev);

	clk_disable_unprepare(prg->clk_axi);
	clk_disable_unprepare(prg->clk_ipg);

	return 0;
}

static int prg_resume(struct device *dev)
{
	struct ipu_prg *prg = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(prg->clk_ipg);
	if (ret)
		return ret;

	ret = clk_prepare_enable(prg->clk_axi);
	if (ret) {
		clk_disable_unprepare(prg->clk_ipg);
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops prg_pm_ops = {
	SET_RUNTIME_PM_OPS(prg_suspend, prg_resume, NULL)
};

static const struct of_device_id ipu_prg_dt_ids[] = {
	{ .compatible = "fsl,imx6qp-prg", },
	{ /* sentinel */ },
};

struct platform_driver ipu_prg_drv = {
	.probe		= ipu_prg_probe,
	.remove		= ipu_prg_remove,
	.driver		= {
		.name	= "imx-ipu-prg",
		.pm	= &prg_pm_ops,
		.of_match_table = ipu_prg_dt_ids,
	},
};
