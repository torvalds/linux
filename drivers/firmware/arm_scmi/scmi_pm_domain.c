// SPDX-License-Identifier: GPL-2.0
/*
 * SCMI Generic power domain support.
 *
 * Copyright (C) 2018-2020 ARM Ltd.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/scmi_protocol.h>

static const struct scmi_power_proto_ops *power_ops;

struct scmi_pm_domain {
	struct generic_pm_domain genpd;
	const struct scmi_protocol_handle *ph;
	const char *name;
	u32 domain;
};

#define to_scmi_pd(gpd) container_of(gpd, struct scmi_pm_domain, genpd)

static int scmi_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	int ret;
	u32 state, ret_state;
	struct scmi_pm_domain *pd = to_scmi_pd(domain);

	if (power_on)
		state = SCMI_POWER_STATE_GENERIC_ON;
	else
		state = SCMI_POWER_STATE_GENERIC_OFF;

	ret = power_ops->state_set(pd->ph, pd->domain, state);
	if (!ret)
		ret = power_ops->state_get(pd->ph, pd->domain, &ret_state);
	if (!ret && state != ret_state)
		return -EIO;

	return ret;
}

static int scmi_pd_power_on(struct generic_pm_domain *domain)
{
	return scmi_pd_power(domain, true);
}

static int scmi_pd_power_off(struct generic_pm_domain *domain)
{
	return scmi_pd_power(domain, false);
}

static int scmi_pm_domain_probe(struct scmi_device *sdev)
{
	int num_domains, i;
	struct device *dev = &sdev->dev;
	struct device_node *np = dev->of_node;
	struct scmi_pm_domain *scmi_pd;
	struct genpd_onecell_data *scmi_pd_data;
	struct generic_pm_domain **domains;
	const struct scmi_handle *handle = sdev->handle;
	struct scmi_protocol_handle *ph;

	if (!handle)
		return -ENODEV;

	power_ops = handle->devm_get_protocol(sdev, SCMI_PROTOCOL_POWER, &ph);
	if (IS_ERR(power_ops))
		return PTR_ERR(power_ops);

	num_domains = power_ops->num_domains_get(ph);
	if (num_domains < 0) {
		dev_err(dev, "number of domains not found\n");
		return num_domains;
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
		u32 state;

		if (power_ops->state_get(ph, i, &state)) {
			dev_warn(dev, "failed to get state for domain %d\n", i);
			continue;
		}

		scmi_pd->domain = i;
		scmi_pd->ph = ph;
		scmi_pd->name = power_ops->name_get(ph, i);
		scmi_pd->genpd.name = scmi_pd->name;
		scmi_pd->genpd.power_off = scmi_pd_power_off;
		scmi_pd->genpd.power_on = scmi_pd_power_on;

		pm_genpd_init(&scmi_pd->genpd, NULL,
			      state == SCMI_POWER_STATE_GENERIC_OFF);

		domains[i] = &scmi_pd->genpd;
	}

	scmi_pd_data->domains = domains;
	scmi_pd_data->num_domains = num_domains;

	return of_genpd_add_provider_onecell(np, scmi_pd_data);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_POWER, "genpd" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_power_domain_driver = {
	.name = "scmi-power-domain",
	.probe = scmi_pm_domain_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_power_domain_driver);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCMI power domain driver");
MODULE_LICENSE("GPL v2");
