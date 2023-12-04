// SPDX-License-Identifier: GPL-2.0
/*
 * SCMI performance domain support.
 *
 * Copyright (C) 2023 Linaro Ltd.
 */

#include <linux/err.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/scmi_protocol.h>
#include <linux/slab.h>

struct scmi_perf_domain {
	struct generic_pm_domain genpd;
	const struct scmi_perf_proto_ops *perf_ops;
	const struct scmi_protocol_handle *ph;
	const struct scmi_perf_domain_info *info;
	u32 domain_id;
};

#define to_scmi_pd(pd) container_of(pd, struct scmi_perf_domain, genpd)

static int
scmi_pd_set_perf_state(struct generic_pm_domain *genpd, unsigned int state)
{
	struct scmi_perf_domain *pd = to_scmi_pd(genpd);
	int ret;

	if (!pd->info->set_perf)
		return 0;

	if (!state)
		return -EINVAL;

	ret = pd->perf_ops->level_set(pd->ph, pd->domain_id, state, false);
	if (ret)
		dev_warn(&genpd->dev, "Failed with %d when trying to set %d perf level",
			 ret, state);

	return ret;
}

static int
scmi_pd_attach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	struct scmi_perf_domain *pd = to_scmi_pd(genpd);
	int ret;

	/*
	 * Allow the device to be attached, but don't add the OPP table unless
	 * the performance level can be changed.
	 */
	if (!pd->info->set_perf)
		return 0;

	ret = pd->perf_ops->device_opps_add(pd->ph, dev, pd->domain_id);
	if (ret)
		dev_warn(dev, "failed to add OPPs for the device\n");

	return ret;
}

static void
scmi_pd_detach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	struct scmi_perf_domain *pd = to_scmi_pd(genpd);

	if (!pd->info->set_perf)
		return;

	dev_pm_opp_remove_all_dynamic(dev);
}

static int scmi_perf_domain_probe(struct scmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	const struct scmi_handle *handle = sdev->handle;
	const struct scmi_perf_proto_ops *perf_ops;
	struct scmi_protocol_handle *ph;
	struct scmi_perf_domain *scmi_pd;
	struct genpd_onecell_data *scmi_pd_data;
	struct generic_pm_domain **domains;
	int num_domains, i, ret = 0;

	if (!handle)
		return -ENODEV;

	/* The OF node must specify us as a power-domain provider. */
	if (!of_find_property(dev->of_node, "#power-domain-cells", NULL))
		return 0;

	perf_ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_PERF, &ph);
	if (IS_ERR(perf_ops))
		return PTR_ERR(perf_ops);

	num_domains = perf_ops->num_domains_get(ph);
	if (num_domains < 0) {
		dev_warn(dev, "Failed with %d when getting num perf domains\n",
			 num_domains);
		return num_domains;
	} else if (!num_domains) {
		return 0;
	}

	scmi_pd = devm_kcalloc(dev, num_domains, sizeof(*scmi_pd), GFP_KERNEL);
	if (!scmi_pd)
		return -ENOMEM;

	scmi_pd_data = devm_kzalloc(dev, sizeof(*scmi_pd_data), GFP_KERNEL);
	if (!scmi_pd_data)
		return -ENOMEM;

	domains = devm_kcalloc(dev, num_domains, sizeof(*domains), GFP_KERNEL);
	if (!domains)
		return -ENOMEM;

	for (i = 0; i < num_domains; i++, scmi_pd++) {
		scmi_pd->info = perf_ops->info_get(ph, i);

		scmi_pd->domain_id = i;
		scmi_pd->perf_ops = perf_ops;
		scmi_pd->ph = ph;
		scmi_pd->genpd.name = scmi_pd->info->name;
		scmi_pd->genpd.flags = GENPD_FLAG_ALWAYS_ON |
				       GENPD_FLAG_OPP_TABLE_FW;
		scmi_pd->genpd.set_performance_state = scmi_pd_set_perf_state;
		scmi_pd->genpd.attach_dev = scmi_pd_attach_dev;
		scmi_pd->genpd.detach_dev = scmi_pd_detach_dev;

		ret = pm_genpd_init(&scmi_pd->genpd, NULL, false);
		if (ret)
			goto err;

		domains[i] = &scmi_pd->genpd;
	}

	scmi_pd_data->domains = domains;
	scmi_pd_data->num_domains = num_domains;

	ret = of_genpd_add_provider_onecell(dev->of_node, scmi_pd_data);
	if (ret)
		goto err;

	dev_set_drvdata(dev, scmi_pd_data);
	dev_info(dev, "Initialized %d performance domains", num_domains);
	return 0;
err:
	for (i--; i >= 0; i--)
		pm_genpd_remove(domains[i]);
	return ret;
}

static void scmi_perf_domain_remove(struct scmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct genpd_onecell_data *scmi_pd_data = dev_get_drvdata(dev);
	int i;

	of_genpd_del_provider(dev->of_node);

	for (i = 0; i < scmi_pd_data->num_domains; i++)
		pm_genpd_remove(scmi_pd_data->domains[i]);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_PERF, "perf" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_perf_domain_driver = {
	.name		= "scmi-perf-domain",
	.probe		= scmi_perf_domain_probe,
	.remove		= scmi_perf_domain_remove,
	.id_table	= scmi_id_table,
};
module_scmi_driver(scmi_perf_domain_driver);

MODULE_AUTHOR("Ulf Hansson <ulf.hansson@linaro.org>");
MODULE_DESCRIPTION("ARM SCMI perf domain driver");
MODULE_LICENSE("GPL v2");
