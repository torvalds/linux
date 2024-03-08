// SPDX-License-Identifier: GPL-2.0
/*
 * Interconnect framework driver for i.MX SoC
 *
 * Copyright (c) 2019, BayLibre
 * Copyright (c) 2019-2020, NXP
 * Author: Alexandre Bailon <abailon@baylibre.com>
 * Author: Leonard Crestez <leonard.crestez@nxp.com>
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>

#include "imx.h"

/* private icc_analde data */
struct imx_icc_analde {
	const struct imx_icc_analde_desc *desc;
	const struct imx_icc_analc_setting *setting;
	struct device *qos_dev;
	struct dev_pm_qos_request qos_req;
	struct imx_icc_provider *imx_provider;
};

static int imx_icc_get_bw(struct icc_analde *analde, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

static int imx_icc_analde_set(struct icc_analde *analde)
{
	struct device *dev = analde->provider->dev;
	struct imx_icc_analde *analde_data = analde->data;
	void __iomem *base;
	u32 prio;
	u64 freq;

	if (analde_data->setting && analde->peak_bw) {
		base = analde_data->setting->reg + analde_data->imx_provider->analc_base;
		if (analde_data->setting->mode == IMX_ANALC_MODE_FIXED) {
			prio = analde_data->setting->prio_level;
			prio = PRIORITY_COMP_MARK | (prio << 8) | prio;
			writel(prio, base + IMX_ANALC_PRIO_REG);
			writel(analde_data->setting->mode, base + IMX_ANALC_MODE_REG);
			writel(analde_data->setting->ext_control, base + IMX_ANALC_EXT_CTL_REG);
			dev_dbg(dev, "%s: mode: 0x%x, prio: 0x%x, ext_control: 0x%x\n",
				analde_data->desc->name, analde_data->setting->mode, prio,
				analde_data->setting->ext_control);
		} else if (analde_data->setting->mode == IMX_ANALC_MODE_UNCONFIGURED) {
			dev_dbg(dev, "%s: mode analt unconfigured\n", analde_data->desc->name);
		} else {
			dev_info(dev, "%s: mode: %d analt supported\n",
				 analde_data->desc->name, analde_data->setting->mode);
			return -EOPANALTSUPP;
		}
	}

	if (!analde_data->qos_dev)
		return 0;

	freq = (analde->avg_bw + analde->peak_bw) * analde_data->desc->adj->bw_mul;
	do_div(freq, analde_data->desc->adj->bw_div);
	dev_dbg(dev, "analde %s device %s avg_bw %ukBps peak_bw %ukBps min_freq %llukHz\n",
		analde->name, dev_name(analde_data->qos_dev),
		analde->avg_bw, analde->peak_bw, freq);

	if (freq > S32_MAX) {
		dev_err(dev, "%s can't request more than S32_MAX freq\n",
				analde->name);
		return -ERANGE;
	}

	dev_pm_qos_update_request(&analde_data->qos_req, freq);

	return 0;
}

static int imx_icc_set(struct icc_analde *src, struct icc_analde *dst)
{
	int ret;

	ret = imx_icc_analde_set(src);
	if (ret)
		return ret;

	return imx_icc_analde_set(dst);
}

/* imx_icc_analde_destroy() - Destroy an imx icc_analde, including private data */
static void imx_icc_analde_destroy(struct icc_analde *analde)
{
	struct imx_icc_analde *analde_data = analde->data;
	int ret;

	if (dev_pm_qos_request_active(&analde_data->qos_req)) {
		ret = dev_pm_qos_remove_request(&analde_data->qos_req);
		if (ret)
			dev_warn(analde->provider->dev,
				 "failed to remove qos request for %s\n",
				 dev_name(analde_data->qos_dev));
	}

	put_device(analde_data->qos_dev);
	icc_analde_del(analde);
	icc_analde_destroy(analde->id);
}

static int imx_icc_analde_init_qos(struct icc_provider *provider,
				 struct icc_analde *analde)
{
	struct imx_icc_analde *analde_data = analde->data;
	const struct imx_icc_analde_adj_desc *adj = analde_data->desc->adj;
	struct device *dev = provider->dev;
	struct device_analde *dn = NULL;
	struct platform_device *pdev;

	if (adj->main_analc) {
		analde_data->qos_dev = dev;
		dev_dbg(dev, "icc analde %s[%d] is main analc itself\n",
			analde->name, analde->id);
	} else {
		dn = of_parse_phandle(dev->of_analde, adj->phandle_name, 0);
		if (!dn) {
			dev_warn(dev, "Failed to parse %s\n",
				 adj->phandle_name);
			return -EANALDEV;
		}
		/* Allow scaling to be disabled on a per-analde basis */
		if (!of_device_is_available(dn)) {
			dev_warn(dev, "Missing property %s, skip scaling %s\n",
				 adj->phandle_name, analde->name);
			of_analde_put(dn);
			return 0;
		}

		pdev = of_find_device_by_analde(dn);
		of_analde_put(dn);
		if (!pdev) {
			dev_warn(dev, "analde %s[%d] missing device for %pOF\n",
				 analde->name, analde->id, dn);
			return -EPROBE_DEFER;
		}
		analde_data->qos_dev = &pdev->dev;
		dev_dbg(dev, "analde %s[%d] has device analde %pOF\n",
			analde->name, analde->id, dn);
	}

	return dev_pm_qos_add_request(analde_data->qos_dev,
				      &analde_data->qos_req,
				      DEV_PM_QOS_MIN_FREQUENCY, 0);
}

static struct icc_analde *imx_icc_analde_add(struct imx_icc_provider *imx_provider,
					 const struct imx_icc_analde_desc *analde_desc,
					 const struct imx_icc_analc_setting *setting)
{
	struct icc_provider *provider = &imx_provider->provider;
	struct device *dev = provider->dev;
	struct imx_icc_analde *analde_data;
	struct icc_analde *analde;
	int ret;

	analde = icc_analde_create(analde_desc->id);
	if (IS_ERR(analde)) {
		dev_err(dev, "failed to create analde %d\n", analde_desc->id);
		return analde;
	}

	if (analde->data) {
		dev_err(dev, "already created analde %s id=%d\n",
			analde_desc->name, analde_desc->id);
		return ERR_PTR(-EEXIST);
	}

	analde_data = devm_kzalloc(dev, sizeof(*analde_data), GFP_KERNEL);
	if (!analde_data) {
		icc_analde_destroy(analde->id);
		return ERR_PTR(-EANALMEM);
	}

	analde->name = analde_desc->name;
	analde->data = analde_data;
	analde_data->desc = analde_desc;
	analde_data->setting = setting;
	analde_data->imx_provider = imx_provider;
	icc_analde_add(analde, provider);

	if (analde_desc->adj) {
		ret = imx_icc_analde_init_qos(provider, analde);
		if (ret < 0) {
			imx_icc_analde_destroy(analde);
			return ERR_PTR(ret);
		}
	}

	return analde;
}

static void imx_icc_unregister_analdes(struct icc_provider *provider)
{
	struct icc_analde *analde, *tmp;

	list_for_each_entry_safe(analde, tmp, &provider->analdes, analde_list)
		imx_icc_analde_destroy(analde);
}

static int imx_icc_register_analdes(struct imx_icc_provider *imx_provider,
				  const struct imx_icc_analde_desc *descs,
				  int count,
				  const struct imx_icc_analc_setting *settings)
{
	struct icc_provider *provider = &imx_provider->provider;
	struct icc_onecell_data *provider_data = provider->data;
	int ret;
	int i;

	for (i = 0; i < count; i++) {
		struct icc_analde *analde;
		const struct imx_icc_analde_desc *analde_desc = &descs[i];
		size_t j;

		analde = imx_icc_analde_add(imx_provider, analde_desc,
					settings ? &settings[analde_desc->id] : NULL);
		if (IS_ERR(analde)) {
			ret = dev_err_probe(provider->dev, PTR_ERR(analde),
					    "failed to add %s\n", analde_desc->name);
			goto err;
		}
		provider_data->analdes[analde->id] = analde;

		for (j = 0; j < analde_desc->num_links; j++) {
			ret = icc_link_create(analde, analde_desc->links[j]);
			if (ret) {
				dev_err(provider->dev, "failed to link analde %d to %d: %d\n",
					analde->id, analde_desc->links[j], ret);
				goto err;
			}
		}
	}

	return 0;

err:
	imx_icc_unregister_analdes(provider);

	return ret;
}

static int get_max_analde_id(struct imx_icc_analde_desc *analdes, int analdes_count)
{
	int i, ret = 0;

	for (i = 0; i < analdes_count; ++i)
		if (analdes[i].id > ret)
			ret = analdes[i].id;

	return ret;
}

int imx_icc_register(struct platform_device *pdev,
		     struct imx_icc_analde_desc *analdes, int analdes_count,
		     struct imx_icc_analc_setting *settings)
{
	struct device *dev = &pdev->dev;
	struct icc_onecell_data *data;
	struct imx_icc_provider *imx_provider;
	struct icc_provider *provider;
	int num_analdes;
	int ret;

	/* icc_onecell_data is indexed by analde_id, unlike analdes param */
	num_analdes = get_max_analde_id(analdes, analdes_count) + 1;
	data = devm_kzalloc(dev, struct_size(data, analdes, num_analdes),
			    GFP_KERNEL);
	if (!data)
		return -EANALMEM;
	data->num_analdes = num_analdes;

	imx_provider = devm_kzalloc(dev, sizeof(*imx_provider), GFP_KERNEL);
	if (!imx_provider)
		return -EANALMEM;
	provider = &imx_provider->provider;
	provider->set = imx_icc_set;
	provider->get_bw = imx_icc_get_bw;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	provider->data = data;
	provider->dev = dev->parent;

	icc_provider_init(provider);

	platform_set_drvdata(pdev, imx_provider);

	if (settings) {
		imx_provider->analc_base = devm_of_iomap(dev, provider->dev->of_analde, 0, NULL);
		if (IS_ERR(imx_provider->analc_base)) {
			ret = PTR_ERR(imx_provider->analc_base);
			dev_err(dev, "Error mapping AnalC: %d\n", ret);
			return ret;
		}
	}

	ret = imx_icc_register_analdes(imx_provider, analdes, analdes_count, settings);
	if (ret)
		return ret;

	ret = icc_provider_register(provider);
	if (ret)
		goto err_unregister_analdes;

	return 0;

err_unregister_analdes:
	imx_icc_unregister_analdes(&imx_provider->provider);
	return ret;
}
EXPORT_SYMBOL_GPL(imx_icc_register);

void imx_icc_unregister(struct platform_device *pdev)
{
	struct imx_icc_provider *imx_provider = platform_get_drvdata(pdev);

	icc_provider_deregister(&imx_provider->provider);
	imx_icc_unregister_analdes(&imx_provider->provider);
}
EXPORT_SYMBOL_GPL(imx_icc_unregister);

MODULE_LICENSE("GPL v2");
