// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP Generic PM domain support
 *
 *  Copyright (C) 2015-2019 Xilinx, Inc.
 *
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajan.vaja@xilinx.com>
 */

#include <linux/err.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>

#include <linux/firmware/xlnx-zynqmp.h>

#define ZYNQMP_NUM_DOMAINS		(100)
/* Flag stating if PM nodes mapped to the PM domain has been requested */
#define ZYNQMP_PM_DOMAIN_REQUESTED	BIT(0)

static int min_capability;

/**
 * struct zynqmp_pm_domain - Wrapper around struct generic_pm_domain
 * @gpd:		Generic power domain
 * @node_id:		PM node ID corresponding to device inside PM domain
 * @flags:		ZynqMP PM domain flags
 */
struct zynqmp_pm_domain {
	struct generic_pm_domain gpd;
	u32 node_id;
	u8 flags;
};

/**
 * zynqmp_gpd_is_active_wakeup_path() - Check if device is in wakeup source
 *					path
 * @dev:	Device to check for wakeup source path
 * @not_used:	Data member (not required)
 *
 * This function is checks device's child hierarchy and checks if any device is
 * set as wakeup source.
 *
 * Return: 1 if device is in wakeup source path else 0
 */
static int zynqmp_gpd_is_active_wakeup_path(struct device *dev, void *not_used)
{
	int may_wakeup;

	may_wakeup = device_may_wakeup(dev);
	if (may_wakeup)
		return may_wakeup;

	return device_for_each_child(dev, NULL,
			zynqmp_gpd_is_active_wakeup_path);
}

/**
 * zynqmp_gpd_power_on() - Power on PM domain
 * @domain:	Generic PM domain
 *
 * This function is called before devices inside a PM domain are resumed, to
 * power on PM domain.
 *
 * Return: 0 on success, error code otherwise
 */
static int zynqmp_gpd_power_on(struct generic_pm_domain *domain)
{
	int ret;
	struct zynqmp_pm_domain *pd;

	pd = container_of(domain, struct zynqmp_pm_domain, gpd);
	ret = zynqmp_pm_set_requirement(pd->node_id,
					ZYNQMP_PM_CAPABILITY_ACCESS,
					ZYNQMP_PM_MAX_QOS,
					ZYNQMP_PM_REQUEST_ACK_BLOCKING);
	if (ret) {
		pr_err("%s() %s set requirement for node %d failed: %d\n",
		       __func__, domain->name, pd->node_id, ret);
		return ret;
	}

	pr_debug("%s() Powered on %s domain\n", __func__, domain->name);
	return 0;
}

/**
 * zynqmp_gpd_power_off() - Power off PM domain
 * @domain:	Generic PM domain
 *
 * This function is called after devices inside a PM domain are suspended, to
 * power off PM domain.
 *
 * Return: 0 on success, error code otherwise
 */
static int zynqmp_gpd_power_off(struct generic_pm_domain *domain)
{
	int ret;
	struct pm_domain_data *pdd, *tmp;
	struct zynqmp_pm_domain *pd;
	u32 capabilities = min_capability;
	bool may_wakeup;

	pd = container_of(domain, struct zynqmp_pm_domain, gpd);

	/* If domain is already released there is nothing to be done */
	if (!(pd->flags & ZYNQMP_PM_DOMAIN_REQUESTED)) {
		pr_debug("%s() %s domain is already released\n",
			 __func__, domain->name);
		return 0;
	}

	list_for_each_entry_safe(pdd, tmp, &domain->dev_list, list_node) {
		/* If device is in wakeup path, set capability to WAKEUP */
		may_wakeup = zynqmp_gpd_is_active_wakeup_path(pdd->dev, NULL);
		if (may_wakeup) {
			dev_dbg(pdd->dev, "device is in wakeup path in %s\n",
				domain->name);
			capabilities = ZYNQMP_PM_CAPABILITY_WAKEUP;
			break;
		}
	}

	ret = zynqmp_pm_set_requirement(pd->node_id, capabilities, 0,
					ZYNQMP_PM_REQUEST_ACK_NO);
	/**
	 * If powering down of any node inside this domain fails,
	 * report and return the error
	 */
	if (ret) {
		pr_err("%s() %s set requirement for node %d failed: %d\n",
		       __func__, domain->name, pd->node_id, ret);
		return ret;
	}

	pr_debug("%s() Powered off %s domain\n", __func__, domain->name);
	return 0;
}

/**
 * zynqmp_gpd_attach_dev() - Attach device to the PM domain
 * @domain:	Generic PM domain
 * @dev:	Device to attach
 *
 * Return: 0 on success, error code otherwise
 */
static int zynqmp_gpd_attach_dev(struct generic_pm_domain *domain,
				 struct device *dev)
{
	int ret;
	struct zynqmp_pm_domain *pd;

	pd = container_of(domain, struct zynqmp_pm_domain, gpd);

	/* If this is not the first device to attach there is nothing to do */
	if (domain->device_count)
		return 0;

	ret = zynqmp_pm_request_node(pd->node_id, 0, 0,
				     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
	/* If requesting a node fails print and return the error */
	if (ret) {
		pr_err("%s() %s request failed for node %d: %d\n",
		       __func__, domain->name, pd->node_id, ret);
		return ret;
	}

	pd->flags |= ZYNQMP_PM_DOMAIN_REQUESTED;

	pr_debug("%s() %s attached to %s domain\n", __func__,
		 dev_name(dev), domain->name);
	return 0;
}

/**
 * zynqmp_gpd_detach_dev() - Detach device from the PM domain
 * @domain:	Generic PM domain
 * @dev:	Device to detach
 */
static void zynqmp_gpd_detach_dev(struct generic_pm_domain *domain,
				  struct device *dev)
{
	int ret;
	struct zynqmp_pm_domain *pd;

	pd = container_of(domain, struct zynqmp_pm_domain, gpd);

	/* If this is not the last device to detach there is nothing to do */
	if (domain->device_count)
		return;

	ret = zynqmp_pm_release_node(pd->node_id);
	/* If releasing a node fails print the error and return */
	if (ret) {
		pr_err("%s() %s release failed for node %d: %d\n",
		       __func__, domain->name, pd->node_id, ret);
		return;
	}

	pd->flags &= ~ZYNQMP_PM_DOMAIN_REQUESTED;

	pr_debug("%s() %s detached from %s domain\n", __func__,
		 dev_name(dev), domain->name);
}

static struct generic_pm_domain *zynqmp_gpd_xlate
				(struct of_phandle_args *genpdspec, void *data)
{
	struct genpd_onecell_data *genpd_data = data;
	unsigned int i, idx = genpdspec->args[0];
	struct zynqmp_pm_domain *pd;

	pd = container_of(genpd_data->domains[0], struct zynqmp_pm_domain, gpd);

	if (genpdspec->args_count != 1)
		return ERR_PTR(-EINVAL);

	/* Check for existing pm domains */
	for (i = 0; i < ZYNQMP_NUM_DOMAINS; i++) {
		if (pd[i].node_id == idx)
			goto done;
	}

	/**
	 * Add index in empty node_id of power domain list as no existing
	 * power domain found for current index.
	 */
	for (i = 0; i < ZYNQMP_NUM_DOMAINS; i++) {
		if (pd[i].node_id == 0) {
			pd[i].node_id = idx;
			break;
		}
	}

done:
	if (!genpd_data->domains[i] || i == ZYNQMP_NUM_DOMAINS)
		return ERR_PTR(-ENOENT);

	return genpd_data->domains[i];
}

static int zynqmp_gpd_probe(struct platform_device *pdev)
{
	int i;
	struct genpd_onecell_data *zynqmp_pd_data;
	struct generic_pm_domain **domains;
	struct zynqmp_pm_domain *pd;
	struct device *dev = &pdev->dev;

	pd = devm_kcalloc(dev, ZYNQMP_NUM_DOMAINS, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	zynqmp_pd_data = devm_kzalloc(dev, sizeof(*zynqmp_pd_data), GFP_KERNEL);
	if (!zynqmp_pd_data)
		return -ENOMEM;

	zynqmp_pd_data->xlate = zynqmp_gpd_xlate;

	domains = devm_kcalloc(dev, ZYNQMP_NUM_DOMAINS, sizeof(*domains),
			       GFP_KERNEL);
	if (!domains)
		return -ENOMEM;

	if (!of_device_is_compatible(dev->parent->of_node,
				     "xlnx,zynqmp-firmware"))
		min_capability = ZYNQMP_PM_CAPABILITY_UNUSABLE;

	for (i = 0; i < ZYNQMP_NUM_DOMAINS; i++, pd++) {
		pd->node_id = 0;
		pd->gpd.name = kasprintf(GFP_KERNEL, "domain%d", i);
		pd->gpd.power_off = zynqmp_gpd_power_off;
		pd->gpd.power_on = zynqmp_gpd_power_on;
		pd->gpd.attach_dev = zynqmp_gpd_attach_dev;
		pd->gpd.detach_dev = zynqmp_gpd_detach_dev;

		domains[i] = &pd->gpd;

		/* Mark all PM domains as initially powered off */
		pm_genpd_init(&pd->gpd, NULL, true);
	}

	zynqmp_pd_data->domains = domains;
	zynqmp_pd_data->num_domains = ZYNQMP_NUM_DOMAINS;
	of_genpd_add_provider_onecell(dev->parent->of_node, zynqmp_pd_data);

	return 0;
}

static int zynqmp_gpd_remove(struct platform_device *pdev)
{
	of_genpd_del_provider(pdev->dev.parent->of_node);

	return 0;
}

static struct platform_driver zynqmp_power_domain_driver = {
	.driver	= {
		.name = "zynqmp_power_controller",
	},
	.probe = zynqmp_gpd_probe,
	.remove = zynqmp_gpd_remove,
};
module_platform_driver(zynqmp_power_domain_driver);

MODULE_ALIAS("platform:zynqmp_power_controller");
