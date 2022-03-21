// SPDX-License-Identifier: GPL-2.0-only
/*
 * SCPI Generic power domain support.
 *
 * Copyright (C) 2016 ARM Ltd.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pm_domain.h>
#include <linux/scpi_protocol.h>

struct scpi_pm_domain {
	struct generic_pm_domain genpd;
	struct scpi_ops *ops;
	u32 domain;
};

/*
 * These device power state values are not well-defined in the specification.
 * In case, different implementations use different values, we can make these
 * specific to compatibles rather than getting these values from device tree.
 */
enum scpi_power_domain_state {
	SCPI_PD_STATE_ON = 0,
	SCPI_PD_STATE_OFF = 3,
};

#define to_scpi_pd(gpd) container_of(gpd, struct scpi_pm_domain, genpd)

static int scpi_pd_power(struct scpi_pm_domain *pd, bool power_on)
{
	int ret;
	enum scpi_power_domain_state state;

	if (power_on)
		state = SCPI_PD_STATE_ON;
	else
		state = SCPI_PD_STATE_OFF;

	ret = pd->ops->device_set_power_state(pd->domain, state);
	if (ret)
		return ret;

	return !(state == pd->ops->device_get_power_state(pd->domain));
}

static int scpi_pd_power_on(struct generic_pm_domain *domain)
{
	struct scpi_pm_domain *pd = to_scpi_pd(domain);

	return scpi_pd_power(pd, true);
}

static int scpi_pd_power_off(struct generic_pm_domain *domain)
{
	struct scpi_pm_domain *pd = to_scpi_pd(domain);

	return scpi_pd_power(pd, false);
}

static int scpi_pm_domain_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct scpi_pm_domain *scpi_pd;
	struct genpd_onecell_data *scpi_pd_data;
	struct generic_pm_domain **domains;
	struct scpi_ops *scpi_ops;
	int ret, num_domains, i;

	scpi_ops = get_scpi_ops();
	if (!scpi_ops)
		return -EPROBE_DEFER;

	if (!np) {
		dev_err(dev, "device tree node not found\n");
		return -ENODEV;
	}

	if (!scpi_ops->device_set_power_state ||
	    !scpi_ops->device_get_power_state) {
		dev_err(dev, "power domains not supported in the firmware\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "num-domains", &num_domains);
	if (ret) {
		dev_err(dev, "number of domains not found\n");
		return -EINVAL;
	}

	scpi_pd = devm_kcalloc(dev, num_domains, sizeof(*scpi_pd), GFP_KERNEL);
	if (!scpi_pd)
		return -ENOMEM;

	scpi_pd_data = devm_kzalloc(dev, sizeof(*scpi_pd_data), GFP_KERNEL);
	if (!scpi_pd_data)
		return -ENOMEM;

	domains = devm_kcalloc(dev, num_domains, sizeof(*domains), GFP_KERNEL);
	if (!domains)
		return -ENOMEM;

	for (i = 0; i < num_domains; i++, scpi_pd++) {
		domains[i] = &scpi_pd->genpd;

		scpi_pd->domain = i;
		scpi_pd->ops = scpi_ops;
		scpi_pd->genpd.name = devm_kasprintf(dev, GFP_KERNEL,
						     "%pOFn.%d", np, i);
		if (!scpi_pd->genpd.name) {
			dev_err(dev, "Failed to allocate genpd name:%pOFn.%d\n",
				np, i);
			continue;
		}
		scpi_pd->genpd.power_off = scpi_pd_power_off;
		scpi_pd->genpd.power_on = scpi_pd_power_on;

		/*
		 * Treat all power domains as off at boot.
		 *
		 * The SCP firmware itself may have switched on some domains,
		 * but for reference counting purpose, keep it this way.
		 */
		pm_genpd_init(&scpi_pd->genpd, NULL, true);
	}

	scpi_pd_data->domains = domains;
	scpi_pd_data->num_domains = num_domains;

	of_genpd_add_provider_onecell(np, scpi_pd_data);

	return 0;
}

static const struct of_device_id scpi_power_domain_ids[] = {
	{ .compatible = "arm,scpi-power-domains", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, scpi_power_domain_ids);

static struct platform_driver scpi_power_domain_driver = {
	.driver	= {
		.name = "scpi_power_domain",
		.of_match_table = scpi_power_domain_ids,
	},
	.probe = scpi_pm_domain_probe,
};
module_platform_driver(scpi_power_domain_driver);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCPI power domain driver");
MODULE_LICENSE("GPL v2");
