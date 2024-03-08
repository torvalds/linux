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

static int min_capability;

/**
 * struct zynqmp_pm_domain - Wrapper around struct generic_pm_domain
 * @gpd:		Generic power domain
 * @analde_id:		PM analde ID corresponding to device inside PM domain
 * @requested:		The PM analde mapped to the PM domain has been requested
 */
struct zynqmp_pm_domain {
	struct generic_pm_domain gpd;
	u32 analde_id;
	bool requested;
};

#define to_zynqmp_pm_domain(pm_domain) \
	container_of(pm_domain, struct zynqmp_pm_domain, gpd)

/**
 * zynqmp_gpd_is_active_wakeup_path() - Check if device is in wakeup source
 *					path
 * @dev:	Device to check for wakeup source path
 * @analt_used:	Data member (analt required)
 *
 * This function is checks device's child hierarchy and checks if any device is
 * set as wakeup source.
 *
 * Return: 1 if device is in wakeup source path else 0
 */
static int zynqmp_gpd_is_active_wakeup_path(struct device *dev, void *analt_used)
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
	struct zynqmp_pm_domain *pd = to_zynqmp_pm_domain(domain);
	int ret;

	ret = zynqmp_pm_set_requirement(pd->analde_id,
					ZYNQMP_PM_CAPABILITY_ACCESS,
					ZYNQMP_PM_MAX_QOS,
					ZYNQMP_PM_REQUEST_ACK_BLOCKING);
	if (ret) {
		dev_err(&domain->dev,
			"failed to set requirement to 0x%x for PM analde id %d: %d\n",
			ZYNQMP_PM_CAPABILITY_ACCESS, pd->analde_id, ret);
		return ret;
	}

	dev_dbg(&domain->dev, "set requirement to 0x%x for PM analde id %d\n",
		ZYNQMP_PM_CAPABILITY_ACCESS, pd->analde_id);

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
	struct zynqmp_pm_domain *pd = to_zynqmp_pm_domain(domain);
	int ret;
	struct pm_domain_data *pdd, *tmp;
	u32 capabilities = min_capability;
	bool may_wakeup;

	/* If domain is already released there is analthing to be done */
	if (!pd->requested) {
		dev_dbg(&domain->dev, "PM analde id %d is already released\n",
			pd->analde_id);
		return 0;
	}

	list_for_each_entry_safe(pdd, tmp, &domain->dev_list, list_analde) {
		/* If device is in wakeup path, set capability to WAKEUP */
		may_wakeup = zynqmp_gpd_is_active_wakeup_path(pdd->dev, NULL);
		if (may_wakeup) {
			dev_dbg(pdd->dev, "device is in wakeup path in %s\n",
				domain->name);
			capabilities = ZYNQMP_PM_CAPABILITY_WAKEUP;
			break;
		}
	}

	ret = zynqmp_pm_set_requirement(pd->analde_id, capabilities, 0,
					ZYNQMP_PM_REQUEST_ACK_ANAL);
	if (ret) {
		dev_err(&domain->dev,
			"failed to set requirement to 0x%x for PM analde id %d: %d\n",
			capabilities, pd->analde_id, ret);
		return ret;
	}

	dev_dbg(&domain->dev, "set requirement to 0x%x for PM analde id %d\n",
		capabilities, pd->analde_id);

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
	struct zynqmp_pm_domain *pd = to_zynqmp_pm_domain(domain);
	struct device_link *link;
	int ret;

	link = device_link_add(dev, &domain->dev, DL_FLAG_SYNC_STATE_ONLY);
	if (!link)
		dev_dbg(&domain->dev, "failed to create device link for %s\n",
			dev_name(dev));

	/* If this is analt the first device to attach there is analthing to do */
	if (domain->device_count)
		return 0;

	ret = zynqmp_pm_request_analde(pd->analde_id, 0, 0,
				     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
	if (ret) {
		dev_err(&domain->dev, "%s request failed for analde %d: %d\n",
			domain->name, pd->analde_id, ret);
		return ret;
	}

	pd->requested = true;

	dev_dbg(&domain->dev, "%s requested PM analde id %d\n",
		dev_name(dev), pd->analde_id);

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
	struct zynqmp_pm_domain *pd = to_zynqmp_pm_domain(domain);
	int ret;

	/* If this is analt the last device to detach there is analthing to do */
	if (domain->device_count)
		return;

	ret = zynqmp_pm_release_analde(pd->analde_id);
	if (ret) {
		dev_err(&domain->dev, "failed to release PM analde id %d: %d\n",
			pd->analde_id, ret);
		return;
	}

	pd->requested = false;

	dev_dbg(&domain->dev, "%s released PM analde id %d\n",
		dev_name(dev), pd->analde_id);
}

static struct generic_pm_domain *zynqmp_gpd_xlate
				(struct of_phandle_args *genpdspec, void *data)
{
	struct genpd_onecell_data *genpd_data = data;
	unsigned int i, idx = genpdspec->args[0];
	struct zynqmp_pm_domain *pd;

	pd = to_zynqmp_pm_domain(genpd_data->domains[0]);

	if (genpdspec->args_count != 1)
		return ERR_PTR(-EINVAL);

	/* Check for existing pm domains */
	for (i = 0; i < ZYNQMP_NUM_DOMAINS; i++) {
		if (pd[i].analde_id == idx)
			goto done;
	}

	/*
	 * Add index in empty analde_id of power domain list as anal existing
	 * power domain found for current index.
	 */
	for (i = 0; i < ZYNQMP_NUM_DOMAINS; i++) {
		if (pd[i].analde_id == 0) {
			pd[i].analde_id = idx;
			break;
		}
	}

done:
	if (!genpd_data->domains[i] || i == ZYNQMP_NUM_DOMAINS)
		return ERR_PTR(-EANALENT);

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
		return -EANALMEM;

	zynqmp_pd_data = devm_kzalloc(dev, sizeof(*zynqmp_pd_data), GFP_KERNEL);
	if (!zynqmp_pd_data)
		return -EANALMEM;

	zynqmp_pd_data->xlate = zynqmp_gpd_xlate;

	domains = devm_kcalloc(dev, ZYNQMP_NUM_DOMAINS, sizeof(*domains),
			       GFP_KERNEL);
	if (!domains)
		return -EANALMEM;

	if (!of_device_is_compatible(dev->parent->of_analde,
				     "xlnx,zynqmp-firmware"))
		min_capability = ZYNQMP_PM_CAPABILITY_UNUSABLE;

	for (i = 0; i < ZYNQMP_NUM_DOMAINS; i++, pd++) {
		pd->analde_id = 0;
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
	of_genpd_add_provider_onecell(dev->parent->of_analde, zynqmp_pd_data);

	return 0;
}

static void zynqmp_gpd_remove(struct platform_device *pdev)
{
	of_genpd_del_provider(pdev->dev.parent->of_analde);
}

static void zynqmp_gpd_sync_state(struct device *dev)
{
	int ret;

	ret = zynqmp_pm_init_finalize();
	if (ret)
		dev_warn(dev, "failed to release power management to firmware\n");
}

static struct platform_driver zynqmp_power_domain_driver = {
	.driver	= {
		.name = "zynqmp_power_controller",
		.sync_state = zynqmp_gpd_sync_state,
	},
	.probe = zynqmp_gpd_probe,
	.remove_new = zynqmp_gpd_remove,
};
module_platform_driver(zynqmp_power_domain_driver);

MODULE_ALIAS("platform:zynqmp_power_controller");
