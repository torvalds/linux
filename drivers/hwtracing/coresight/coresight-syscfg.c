// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/platform_device.h>

#include "coresight-config.h"
#include "coresight-syscfg.h"

/*
 * cscfg_ API manages configurations and features for the entire coresight
 * infrastructure.
 *
 * It allows the loading of configurations and features, and loads these into
 * coresight devices as appropriate.
 */

/* protect the cscsg_data and device */
static DEFINE_MUTEX(cscfg_mutex);

/* only one of these */
static struct cscfg_manager *cscfg_mgr;

/* load features and configuations into the lists */

/* check feature list for a named feature - call with mutex locked. */
static bool cscfg_match_list_feat(const char *name)
{
	struct cscfg_feature_desc *feat_desc;

	list_for_each_entry(feat_desc, &cscfg_mgr->feat_desc_list, item) {
		if (strcmp(feat_desc->name, name) == 0)
			return true;
	}
	return false;
}

/* check all feat needed for cfg are in the list - call with mutex locked. */
static int cscfg_check_feat_for_cfg(struct cscfg_config_desc *config_desc)
{
	int i;

	for (i = 0; i < config_desc->nr_feat_refs; i++)
		if (!cscfg_match_list_feat(config_desc->feat_ref_names[i]))
			return -EINVAL;
	return 0;
}

/*
 * load feature - add to feature list.
 */
static int cscfg_load_feat(struct cscfg_feature_desc *feat_desc)
{
	list_add(&feat_desc->item, &cscfg_mgr->feat_desc_list);

	return 0;
}

/*
 * load config into the system - validate used features exist then add to
 * config list.
 */
static int cscfg_load_config(struct cscfg_config_desc *config_desc)
{
	int err;

	/* validate features are present */
	err = cscfg_check_feat_for_cfg(config_desc);
	if (err)
		return err;

	list_add(&config_desc->item, &cscfg_mgr->config_desc_list);
	return 0;
}

/**
 * cscfg_load_config_sets - API function to load feature and config sets.
 *
 * Take a 0 terminated array of feature descriptors and/or configuration
 * descriptors and load into the system.
 * Features are loaded first to ensure configuration dependencies can be met.
 *
 * @config_descs: 0 terminated array of configuration descriptors.
 * @feat_descs:   0 terminated array of feature descriptors.
 */
int cscfg_load_config_sets(struct cscfg_config_desc **config_descs,
			   struct cscfg_feature_desc **feat_descs)
{
	int err, i = 0;

	mutex_lock(&cscfg_mutex);

	/* load features first */
	if (feat_descs) {
		while (feat_descs[i]) {
			err = cscfg_load_feat(feat_descs[i]);
			if (err) {
				pr_err("coresight-syscfg: Failed to load feature %s\n",
				       feat_descs[i]->name);
				goto exit_unlock;
			}
			i++;
		}
	}

	/* next any configurations to check feature dependencies */
	i = 0;
	if (config_descs) {
		while (config_descs[i]) {
			err = cscfg_load_config(config_descs[i]);
			if (err) {
				pr_err("coresight-syscfg: Failed to load configuration %s\n",
				       config_descs[i]->name);
				goto exit_unlock;
			}
			i++;
		}
	}

exit_unlock:
	mutex_unlock(&cscfg_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(cscfg_load_config_sets);

/* Initialise system configuration management device. */

struct device *cscfg_device(void)
{
	return cscfg_mgr ? &cscfg_mgr->dev : NULL;
}

/* Must have a release function or the kernel will complain on module unload */
static void cscfg_dev_release(struct device *dev)
{
	kfree(cscfg_mgr);
	cscfg_mgr = NULL;
}

/* a device is needed to "own" some kernel elements such as sysfs entries.  */
static int cscfg_create_device(void)
{
	struct device *dev;
	int err = -ENOMEM;

	mutex_lock(&cscfg_mutex);
	if (cscfg_mgr) {
		err = -EINVAL;
		goto create_dev_exit_unlock;
	}

	cscfg_mgr = kzalloc(sizeof(struct cscfg_manager), GFP_KERNEL);
	if (!cscfg_mgr)
		goto create_dev_exit_unlock;

	/* setup the device */
	dev = cscfg_device();
	dev->release = cscfg_dev_release;
	dev->init_name = "cs_system_cfg";

	err = device_register(dev);
	if (err)
		cscfg_dev_release(dev);

create_dev_exit_unlock:
	mutex_unlock(&cscfg_mutex);
	return err;
}

static void cscfg_clear_device(void)
{
	mutex_lock(&cscfg_mutex);
	device_unregister(cscfg_device());
	mutex_unlock(&cscfg_mutex);
}

/* Initialise system config management API device  */
int __init cscfg_init(void)
{
	int err = 0;

	err = cscfg_create_device();
	if (err)
		return err;

	INIT_LIST_HEAD(&cscfg_mgr->csdev_desc_list);
	INIT_LIST_HEAD(&cscfg_mgr->feat_desc_list);
	INIT_LIST_HEAD(&cscfg_mgr->config_desc_list);

	dev_info(cscfg_device(), "CoreSight Configuration manager initialised");
	return 0;
}

void cscfg_exit(void)
{
	cscfg_clear_device();
}
