// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define SDPM_DRIVER		"sdpm-clk-notify"
#define CSR_MAX_VAL		7
#define CSR_OFFSET		0xF00
#define FREQ_HZ_TO_MHZ(f)	((f) / 1000000)

struct sdpm_clk_instance;
struct sdpm_clk_data {
	struct notifier_block		clk_rate_nb;
	struct clk			*clk;
	const char			*clock_name;
	struct notifier_block		reg_nb;
	struct regulator		*reg;
	uint8_t				reg_enable;
	uint32_t			csr_id;
	unsigned long			last_freq;
	struct mutex			sdpm_mutex;
	struct sdpm_clk_instance	*sdpm_inst;
};
struct sdpm_clk_instance {
	struct device			*dev;
	void __iomem			*regmap;
	uint32_t			clk_ct;
	struct sdpm_clk_data		*clk_data;
};

static void sdpm_csr_write(struct sdpm_clk_data *sdpm_data,
				unsigned long clk_rate)
{
	struct sdpm_clk_instance *sdpm_inst = sdpm_data->sdpm_inst;
	uint32_t val = sdpm_data->reg_enable ? clk_rate : 0;

	sdpm_data->last_freq = clk_rate;

	dev_dbg(sdpm_inst->dev, "clock:%s offset:0x%x frequency:%u\n",
			sdpm_data->clock_name,
			CSR_OFFSET + sdpm_data->csr_id * 4, val);
	writel_relaxed(val,
		sdpm_inst->regmap + CSR_OFFSET + sdpm_data->csr_id * 4);
}

static int sdpm_reg_notifier(struct notifier_block *nb, unsigned long event,
				void *data)
{
	struct sdpm_clk_data *sdpm_data = container_of(nb,
			struct sdpm_clk_data, reg_nb);

	dev_dbg(sdpm_data->sdpm_inst->dev, "reg:%s event:%lu\n",
			sdpm_data->clock_name, event);
	switch (event) {
	case REGULATOR_EVENT_ENABLE:
		mutex_lock(&sdpm_data->sdpm_mutex);
		sdpm_data->reg_enable = 1;
		sdpm_csr_write(sdpm_data, sdpm_data->last_freq);
		mutex_unlock(&sdpm_data->sdpm_mutex);
		return NOTIFY_OK;
	case REGULATOR_EVENT_DISABLE:
		mutex_lock(&sdpm_data->sdpm_mutex);
		sdpm_data->reg_enable = 0;
		sdpm_csr_write(sdpm_data, sdpm_data->last_freq);
		mutex_unlock(&sdpm_data->sdpm_mutex);
		return NOTIFY_OK;
	default:
		return NOTIFY_OK;
	}

	return NOTIFY_OK;
}

static int sdpm_clock_notifier(struct notifier_block *nb,
					unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct sdpm_clk_data *sdpm_data = container_of(nb,
				struct sdpm_clk_data, clk_rate_nb);

	dev_dbg(sdpm_data->sdpm_inst->dev, "clock:%s event:%lu\n",
			sdpm_data->clock_name, event);
	switch (event) {
	case PRE_RATE_CHANGE:
		mutex_lock(&sdpm_data->sdpm_mutex);
		if (ndata->new_rate > ndata->old_rate)
			sdpm_csr_write(sdpm_data,
					FREQ_HZ_TO_MHZ(ndata->new_rate));
		mutex_unlock(&sdpm_data->sdpm_mutex);
		return NOTIFY_DONE;
	case POST_RATE_CHANGE:
		mutex_lock(&sdpm_data->sdpm_mutex);
		if (ndata->new_rate < ndata->old_rate)
			sdpm_csr_write(sdpm_data,
					FREQ_HZ_TO_MHZ(ndata->new_rate));
		mutex_unlock(&sdpm_data->sdpm_mutex);
		return NOTIFY_DONE;
	case ABORT_RATE_CHANGE:
		mutex_lock(&sdpm_data->sdpm_mutex);
		if (ndata->new_rate > ndata->old_rate)
			sdpm_csr_write(sdpm_data,
					FREQ_HZ_TO_MHZ(ndata->old_rate));
		mutex_unlock(&sdpm_data->sdpm_mutex);
		return NOTIFY_DONE;
	default:
		return NOTIFY_DONE;
	}
}

static int sdpm_clk_device_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0, idx = 0, clk_ct = 0, csr = 0, csr_ct = 0;
	struct sdpm_clk_instance *sdpm_clk;
	struct device_node *dev_node = dev->of_node;
	struct resource *res;

	sdpm_clk = devm_kzalloc(dev, sizeof(*sdpm_clk), GFP_KERNEL);
	if (!sdpm_clk)
		return -ENOMEM;
	sdpm_clk->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Couldn't get MEM resource\n");
		return -EINVAL;
	}
	dev_dbg(dev, "sdpm@0x%x size:%d\n", res->start,
			resource_size(res));
	dev_set_drvdata(dev, sdpm_clk);

	sdpm_clk->regmap = devm_ioremap_resource(dev, res);
	if (!sdpm_clk->regmap) {
		dev_err(dev, "Couldn't get regmap\n");
		return -EINVAL;
	}

	ret = of_property_count_strings(dev_node, "clock-names");
	if (ret < 0) {
		dev_err(dev, "Couldn't get clock names. %d\n", ret);
		return ret;
	}
	clk_ct = ret;
	ret = of_property_count_u32_elems(dev_node, "csr-id");
	if (ret <= 0) {
		dev_err(dev, "Couldn't get csr ID array. %d\n", ret);
		return ret;
	}
	csr_ct = ret;
	if (clk_ct != csr_ct) {
		dev_err(dev, "Invalid csr:%d and clk:%d count.\n", csr_ct,
				clk_ct);
		return -EINVAL;
	}
	sdpm_clk->clk_ct = clk_ct;
	sdpm_clk->clk_data = devm_kcalloc(dev, clk_ct,
				sizeof(*sdpm_clk->clk_data), GFP_KERNEL);
	if (!sdpm_clk->clk_data)
		return -ENOMEM;

	for (idx = 0; idx < sdpm_clk->clk_ct; idx++) {
		ret = of_property_read_string_index(dev_node, "clock-names",
				idx, &sdpm_clk->clk_data[idx].clock_name);
		if (ret < 0) {
			dev_err(dev, "Couldn't get clk name index:%d. %d\n",
					idx, ret);
			return ret;
		}

		sdpm_clk->clk_data[idx].clk = devm_clk_get(dev,
				sdpm_clk->clk_data[idx].clock_name);
		if (IS_ERR(sdpm_clk->clk_data[idx].clk))
			return PTR_ERR(sdpm_clk->clk_data[idx].clk);

		ret = of_property_read_u32_index(dev_node, "csr-id", idx, &csr);
		if (ret < 0) {
			dev_err(dev, "Couldn't get CSR for index:%d. %d\n",
					idx, ret);
			return ret;
		}
		if (ret > CSR_MAX_VAL) {
			dev_err(dev, "Invalid CSR %d\n", csr);
			return -EINVAL;
		}
		dev_dbg(dev, "SDPM clock:%s csr:%d initialized\n",
				sdpm_clk->clk_data[idx].clock_name, csr);
		sdpm_clk->clk_data[idx].csr_id = csr;
		sdpm_clk->clk_data[idx].sdpm_inst = sdpm_clk;
		sdpm_clk->clk_data[idx].clk_rate_nb.notifier_call =
			sdpm_clock_notifier;
		sdpm_clk->clk_data[idx].last_freq = FREQ_HZ_TO_MHZ(
				clk_get_rate(sdpm_clk->clk_data[idx].clk));
		sdpm_clk->clk_data[idx].reg_enable = 1;
		sdpm_clk->clk_data[idx].reg = NULL;
		sdpm_csr_write(&sdpm_clk->clk_data[idx],
				sdpm_clk->clk_data[idx].last_freq);
		mutex_init(&sdpm_clk->clk_data[idx].sdpm_mutex);
		clk_notifier_register(sdpm_clk->clk_data[idx].clk,
					&sdpm_clk->clk_data[idx].clk_rate_nb);
		sdpm_clk->clk_data[idx].reg = devm_regulator_get(dev,
					sdpm_clk->clk_data[idx].clock_name);
		if (IS_ERR(sdpm_clk->clk_data[idx].reg)) {
			dev_err(dev, "regulator:%s get err:%d\n",
					sdpm_clk->clk_data[idx].clock_name,
					PTR_ERR(sdpm_clk->clk_data[idx].reg));
			if (PTR_ERR(sdpm_clk->clk_data[idx].reg)
					== -EPROBE_DEFER)
				return PTR_ERR(sdpm_clk->clk_data[idx].reg);
		} else {
			sdpm_clk->clk_data[idx].reg_nb.notifier_call =
				sdpm_reg_notifier;
			regulator_register_notifier(
					sdpm_clk->clk_data[idx].reg,
					&sdpm_clk->clk_data[idx].reg_nb);
		}
	}

	return 0;
}

static int sdpm_clk_device_remove(struct platform_device *pdev)
{
	struct sdpm_clk_instance *sdpm_clk =
		(struct sdpm_clk_instance *)dev_get_drvdata(&pdev->dev);
	int idx = 0;

	for (idx = 0; idx < sdpm_clk->clk_ct; idx++) {
		clk_notifier_unregister(sdpm_clk->clk_data[idx].clk,
					&sdpm_clk->clk_data[idx].clk_rate_nb);
		if (!sdpm_clk->clk_data[idx].reg)
			continue;
		regulator_unregister_notifier(sdpm_clk->clk_data[idx].reg,
					&sdpm_clk->clk_data[idx].reg_nb);
	}

	return 0;
}

static const struct of_device_id sdpm_clk_device_match[] = {
	{.compatible = "qcom,sdpm"},
	{}
};

static struct platform_driver sdpm_clk_device_driver = {
	.probe          = sdpm_clk_device_probe,
	.remove         = sdpm_clk_device_remove,
	.driver         = {
		.name   = SDPM_DRIVER,
		.of_match_table = sdpm_clk_device_match,
	},
};

module_platform_driver(sdpm_clk_device_driver);
MODULE_LICENSE("GPL");
