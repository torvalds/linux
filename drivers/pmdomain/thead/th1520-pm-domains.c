// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 * Copyright (c) 2024 Samsung Electronics Co., Ltd.
 * Author: Michal Wilczynski <m.wilczynski@samsung.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/firmware/thead/thead,th1520-aon.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>

#include <dt-bindings/power/thead,th1520-power.h>

struct th1520_power_domain {
	struct th1520_aon_chan *aon_chan;
	struct generic_pm_domain genpd;
	u32 rsrc;
};

struct th1520_power_info {
	const char *name;
	u32 rsrc;
	bool disabled;
};

/*
 * The AUDIO power domain is marked as disabled to prevent the driver from
 * managing its power state. Direct AON firmware calls to control this power
 * island trigger a firmware bug causing system instability. Until this
 * firmware issue is resolved, the AUDIO power domain must remain disabled
 * to avoid crashes.
 */
static const struct th1520_power_info th1520_pd_ranges[] = {
	[TH1520_AUDIO_PD] = {"audio", TH1520_AON_AUDIO_PD, true },
	[TH1520_VDEC_PD] = { "vdec", TH1520_AON_VDEC_PD, false },
	[TH1520_NPU_PD] = { "npu", TH1520_AON_NPU_PD, false },
	[TH1520_VENC_PD] = { "venc", TH1520_AON_VENC_PD, false },
	[TH1520_GPU_PD] = { "gpu", TH1520_AON_GPU_PD, false },
	[TH1520_DSP0_PD] = { "dsp0", TH1520_AON_DSP0_PD, false },
	[TH1520_DSP1_PD] = { "dsp1", TH1520_AON_DSP1_PD, false }
};

static inline struct th1520_power_domain *
to_th1520_power_domain(struct generic_pm_domain *genpd)
{
	return container_of(genpd, struct th1520_power_domain, genpd);
}

static int th1520_pd_power_on(struct generic_pm_domain *domain)
{
	struct th1520_power_domain *pd = to_th1520_power_domain(domain);

	return th1520_aon_power_update(pd->aon_chan, pd->rsrc, true);
}

static int th1520_pd_power_off(struct generic_pm_domain *domain)
{
	struct th1520_power_domain *pd = to_th1520_power_domain(domain);

	return th1520_aon_power_update(pd->aon_chan, pd->rsrc, false);
}

static struct generic_pm_domain *th1520_pd_xlate(const struct of_phandle_args *spec,
						 void *data)
{
	struct generic_pm_domain *domain = ERR_PTR(-ENOENT);
	struct genpd_onecell_data *pd_data = data;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(th1520_pd_ranges); i++) {
		struct th1520_power_domain *pd;

		if (th1520_pd_ranges[i].disabled)
			continue;

		pd = to_th1520_power_domain(pd_data->domains[i]);
		if (pd->rsrc == spec->args[0]) {
			domain = &pd->genpd;
			break;
		}
	}

	return domain;
}

static struct th1520_power_domain *
th1520_add_pm_domain(struct device *dev, const struct th1520_power_info *pi)
{
	struct th1520_power_domain *pd;
	int ret;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	pd->rsrc = pi->rsrc;
	pd->genpd.power_on = th1520_pd_power_on;
	pd->genpd.power_off = th1520_pd_power_off;
	pd->genpd.name = pi->name;

	ret = pm_genpd_init(&pd->genpd, NULL, true);
	if (ret)
		return ERR_PTR(ret);

	return pd;
}

static void th1520_pd_init_all_off(struct generic_pm_domain **domains,
				   struct device *dev)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(th1520_pd_ranges); i++) {
		struct th1520_power_domain *pd;

		if (th1520_pd_ranges[i].disabled)
			continue;

		pd = to_th1520_power_domain(domains[i]);

		ret = th1520_aon_power_update(pd->aon_chan, pd->rsrc, false);
		if (ret)
			dev_err(dev,
				"Failed to initially power down power domain %s\n",
				pd->genpd.name);
	}
}

static void th1520_pd_pwrseq_unregister_adev(void *adev)
{
	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static int th1520_pd_pwrseq_gpu_init(struct device *dev)
{
	struct auxiliary_device *adev;
	int ret;

	/*
	 * Correctly check only for the property's existence in the DT node.
	 * We don't need to get/claim the reset here; that is the job of
	 * the auxiliary driver that we are about to spawn.
	 */
	if (device_property_match_string(dev, "reset-names", "gpu-clkgen") < 0)
		/*
		 * This is not an error. It simply means the optional sequencer
		 * is not described in the device tree.
		 */
		return 0;

	adev = devm_kzalloc(dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;

	adev->name = "pwrseq-gpu";
	adev->dev.parent = dev;

	ret = auxiliary_device_init(adev);
	if (ret)
		return ret;

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(dev, th1520_pd_pwrseq_unregister_adev,
					adev);
}

static int th1520_pd_reboot_init(struct device *dev,
				 struct th1520_aon_chan *aon_chan)
{
	struct auxiliary_device *adev;

	adev = devm_auxiliary_device_create(dev, "reboot", aon_chan);
	if (!adev)
		return -ENODEV;

	return 0;
}

static int th1520_pd_probe(struct platform_device *pdev)
{
	struct generic_pm_domain **domains;
	struct genpd_onecell_data *pd_data;
	struct th1520_aon_chan *aon_chan;
	struct device *dev = &pdev->dev;
	int i, ret;

	aon_chan = th1520_aon_init(dev);
	if (IS_ERR(aon_chan))
		return dev_err_probe(dev, PTR_ERR(aon_chan),
				     "Failed to get AON channel\n");

	domains = devm_kcalloc(dev, ARRAY_SIZE(th1520_pd_ranges),
			       sizeof(*domains), GFP_KERNEL);
	if (!domains) {
		ret = -ENOMEM;
		goto err_clean_aon;
	}

	pd_data = devm_kzalloc(dev, sizeof(*pd_data), GFP_KERNEL);
	if (!pd_data) {
		ret = -ENOMEM;
		goto err_clean_aon;
	}

	for (i = 0; i < ARRAY_SIZE(th1520_pd_ranges); i++) {
		struct th1520_power_domain *pd;

		if (th1520_pd_ranges[i].disabled)
			continue;

		pd = th1520_add_pm_domain(dev, &th1520_pd_ranges[i]);
		if (IS_ERR(pd)) {
			ret = PTR_ERR(pd);
			goto err_clean_genpd;
		}

		pd->aon_chan = aon_chan;
		domains[i] = &pd->genpd;
		dev_dbg(dev, "added power domain %s\n", pd->genpd.name);
	}

	pd_data->domains = domains;
	pd_data->num_domains = ARRAY_SIZE(th1520_pd_ranges);
	pd_data->xlate = th1520_pd_xlate;

	/*
	 * Initialize all power domains to off to ensure they start in a
	 * low-power state. This allows device drivers to manage power
	 * domains by turning them on or off as needed.
	 */
	th1520_pd_init_all_off(domains, dev);

	ret = of_genpd_add_provider_onecell(dev->of_node, pd_data);
	if (ret)
		goto err_clean_genpd;

	ret = th1520_pd_pwrseq_gpu_init(dev);
	if (ret)
		goto err_clean_provider;

	ret = th1520_pd_reboot_init(dev, aon_chan);
	if (ret)
		goto err_clean_provider;

	return 0;

err_clean_provider:
	of_genpd_del_provider(dev->of_node);
err_clean_genpd:
	for (i--; i >= 0; i--)
		pm_genpd_remove(domains[i]);
err_clean_aon:
	th1520_aon_deinit(aon_chan);

	return ret;
}

static const struct of_device_id th1520_pd_match[] = {
	{ .compatible = "thead,th1520-aon" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, th1520_pd_match);

static struct platform_driver th1520_pd_driver = {
	.driver = {
		.name = "th1520-pd",
		.of_match_table = th1520_pd_match,
		.suppress_bind_attrs = true,
	},
	.probe = th1520_pd_probe,
};
module_platform_driver(th1520_pd_driver);

MODULE_AUTHOR("Michal Wilczynski <m.wilczynski@samsung.com>");
MODULE_DESCRIPTION("T-HEAD TH1520 SoC power domain controller");
MODULE_LICENSE("GPL");
