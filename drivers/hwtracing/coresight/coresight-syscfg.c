// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/platform_device.h>
#include <linux/slab.h>

#include "coresight-config.h"
#include "coresight-etm-perf.h"
#include "coresight-syscfg.h"
#include "coresight-syscfg-configfs.h"

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

/* get name feature instance from a coresight device list of features */
static struct cscfg_feature_csdev *
cscfg_get_feat_csdev(struct coresight_device *csdev, const char *name)
{
	struct cscfg_feature_csdev *feat_csdev = NULL;

	list_for_each_entry(feat_csdev, &csdev->feature_csdev_list, node) {
		if (strcmp(feat_csdev->feat_desc->name, name) == 0)
			return feat_csdev;
	}
	return NULL;
}

/* allocate the device config instance - with max number of used features */
static struct cscfg_config_csdev *
cscfg_alloc_csdev_cfg(struct coresight_device *csdev, int nr_feats)
{
	struct cscfg_config_csdev *config_csdev = NULL;
	struct device *dev = csdev->dev.parent;

	/* this is being allocated using the devm for the coresight device */
	config_csdev = devm_kzalloc(dev,
				    offsetof(struct cscfg_config_csdev, feats_csdev[nr_feats]),
				    GFP_KERNEL);
	if (!config_csdev)
		return NULL;

	config_csdev->csdev = csdev;
	return config_csdev;
}

/* Load a config into a device if there are any feature matches between config and device */
static int cscfg_add_csdev_cfg(struct coresight_device *csdev,
			       struct cscfg_config_desc *config_desc)
{
	struct cscfg_config_csdev *config_csdev = NULL;
	struct cscfg_feature_csdev *feat_csdev;
	unsigned long flags;
	int i;

	/* look at each required feature and see if it matches any feature on the device */
	for (i = 0; i < config_desc->nr_feat_refs; i++) {
		/* look for a matching name */
		feat_csdev = cscfg_get_feat_csdev(csdev, config_desc->feat_ref_names[i]);
		if (feat_csdev) {
			/*
			 * At least one feature on this device matches the config
			 * add a config instance to the device and a reference to the feature.
			 */
			if (!config_csdev) {
				config_csdev = cscfg_alloc_csdev_cfg(csdev,
								     config_desc->nr_feat_refs);
				if (!config_csdev)
					return -ENOMEM;
				config_csdev->config_desc = config_desc;
			}
			config_csdev->feats_csdev[config_csdev->nr_feat++] = feat_csdev;
		}
	}
	/* if matched features, add config to device.*/
	if (config_csdev) {
		spin_lock_irqsave(&csdev->cscfg_csdev_lock, flags);
		list_add(&config_csdev->node, &csdev->config_csdev_list);
		spin_unlock_irqrestore(&csdev->cscfg_csdev_lock, flags);
	}

	return 0;
}

/*
 * Add the config to the set of registered devices - call with mutex locked.
 * Iterates through devices - any device that matches one or more of the
 * configuration features will load it, the others will ignore it.
 */
static int cscfg_add_cfg_to_csdevs(struct cscfg_config_desc *config_desc)
{
	struct cscfg_registered_csdev *csdev_item;
	int err;

	list_for_each_entry(csdev_item, &cscfg_mgr->csdev_desc_list, item) {
		err = cscfg_add_csdev_cfg(csdev_item->csdev, config_desc);
		if (err)
			return err;
	}
	return 0;
}

/*
 * Allocate a feature object for load into a csdev.
 * memory allocated using the csdev->dev object using devm managed allocator.
 */
static struct cscfg_feature_csdev *
cscfg_alloc_csdev_feat(struct coresight_device *csdev, struct cscfg_feature_desc *feat_desc)
{
	struct cscfg_feature_csdev *feat_csdev = NULL;
	struct device *dev = csdev->dev.parent;
	int i;

	feat_csdev = devm_kzalloc(dev, sizeof(struct cscfg_feature_csdev), GFP_KERNEL);
	if (!feat_csdev)
		return NULL;

	/* parameters are optional - could be 0 */
	feat_csdev->nr_params = feat_desc->nr_params;

	/*
	 * if we need parameters, zero alloc the space here, the load routine in
	 * the csdev device driver will fill out some information according to
	 * feature descriptor.
	 */
	if (feat_csdev->nr_params) {
		feat_csdev->params_csdev = devm_kcalloc(dev, feat_csdev->nr_params,
							sizeof(struct cscfg_parameter_csdev),
							GFP_KERNEL);
		if (!feat_csdev->params_csdev)
			return NULL;

		/*
		 * fill in the feature reference in the param - other fields
		 * handled by loader in csdev.
		 */
		for (i = 0; i < feat_csdev->nr_params; i++)
			feat_csdev->params_csdev[i].feat_csdev = feat_csdev;
	}

	/*
	 * Always have registers to program - again the load routine in csdev device
	 * will fill out according to feature descriptor and device requirements.
	 */
	feat_csdev->nr_regs = feat_desc->nr_regs;
	feat_csdev->regs_csdev = devm_kcalloc(dev, feat_csdev->nr_regs,
					      sizeof(struct cscfg_regval_csdev),
					      GFP_KERNEL);
	if (!feat_csdev->regs_csdev)
		return NULL;

	/* load the feature default values */
	feat_csdev->feat_desc = feat_desc;
	feat_csdev->csdev = csdev;

	return feat_csdev;
}

/* load one feature into one coresight device */
static int cscfg_load_feat_csdev(struct coresight_device *csdev,
				 struct cscfg_feature_desc *feat_desc,
				 struct cscfg_csdev_feat_ops *ops)
{
	struct cscfg_feature_csdev *feat_csdev;
	unsigned long flags;
	int err;

	if (!ops->load_feat)
		return -EINVAL;

	feat_csdev = cscfg_alloc_csdev_feat(csdev, feat_desc);
	if (!feat_csdev)
		return -ENOMEM;

	/* load the feature into the device */
	err = ops->load_feat(csdev, feat_csdev);
	if (err)
		return err;

	/* add to internal csdev feature list & initialise using reset call */
	cscfg_reset_feat(feat_csdev);
	spin_lock_irqsave(&csdev->cscfg_csdev_lock, flags);
	list_add(&feat_csdev->node, &csdev->feature_csdev_list);
	spin_unlock_irqrestore(&csdev->cscfg_csdev_lock, flags);

	return 0;
}

/*
 * Add feature to any matching devices - call with mutex locked.
 * Iterates through devices - any device that matches the feature will be
 * called to load it.
 */
static int cscfg_add_feat_to_csdevs(struct cscfg_feature_desc *feat_desc)
{
	struct cscfg_registered_csdev *csdev_item;
	int err;

	list_for_each_entry(csdev_item, &cscfg_mgr->csdev_desc_list, item) {
		if (csdev_item->match_flags & feat_desc->match_flags) {
			err = cscfg_load_feat_csdev(csdev_item->csdev, feat_desc, &csdev_item->ops);
			if (err)
				return err;
		}
	}
	return 0;
}

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
	int err;
	struct cscfg_feature_desc *feat_desc_exist;

	/* new feature must have unique name */
	list_for_each_entry(feat_desc_exist, &cscfg_mgr->feat_desc_list, item) {
		if (!strcmp(feat_desc_exist->name, feat_desc->name))
			return -EEXIST;
	}

	/* add feature to any matching registered devices */
	err = cscfg_add_feat_to_csdevs(feat_desc);
	if (err)
		return err;

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
	struct cscfg_config_desc *config_desc_exist;

	/* new configuration must have a unique name */
	list_for_each_entry(config_desc_exist, &cscfg_mgr->config_desc_list, item) {
		if (!strcmp(config_desc_exist->name, config_desc->name))
			return -EEXIST;
	}

	/* validate features are present */
	err = cscfg_check_feat_for_cfg(config_desc);
	if (err)
		return err;

	/* add config to any matching registered device */
	err = cscfg_add_cfg_to_csdevs(config_desc);
	if (err)
		return err;

	/* add config to perf fs to allow selection */
	err = etm_perf_add_symlink_cscfg(cscfg_device(), config_desc);
	if (err)
		return err;

	list_add(&config_desc->item, &cscfg_mgr->config_desc_list);
	atomic_set(&config_desc->active_cnt, 0);
	return 0;
}

/* get a feature descriptor by name */
const struct cscfg_feature_desc *cscfg_get_named_feat_desc(const char *name)
{
	const struct cscfg_feature_desc *feat_desc = NULL, *feat_desc_item;

	mutex_lock(&cscfg_mutex);

	list_for_each_entry(feat_desc_item, &cscfg_mgr->feat_desc_list, item) {
		if (strcmp(feat_desc_item->name, name) == 0) {
			feat_desc = feat_desc_item;
			break;
		}
	}

	mutex_unlock(&cscfg_mutex);
	return feat_desc;
}

/* called with cscfg_mutex held */
static struct cscfg_feature_csdev *
cscfg_csdev_get_feat_from_desc(struct coresight_device *csdev,
			       struct cscfg_feature_desc *feat_desc)
{
	struct cscfg_feature_csdev *feat_csdev;

	list_for_each_entry(feat_csdev, &csdev->feature_csdev_list, node) {
		if (feat_csdev->feat_desc == feat_desc)
			return feat_csdev;
	}
	return NULL;
}

int cscfg_update_feat_param_val(struct cscfg_feature_desc *feat_desc,
				int param_idx, u64 value)
{
	int err = 0;
	struct cscfg_feature_csdev *feat_csdev;
	struct cscfg_registered_csdev *csdev_item;

	mutex_lock(&cscfg_mutex);

	/* check if any config active & return busy */
	if (atomic_read(&cscfg_mgr->sys_active_cnt)) {
		err = -EBUSY;
		goto unlock_exit;
	}

	/* set the value */
	if ((param_idx < 0) || (param_idx >= feat_desc->nr_params)) {
		err = -EINVAL;
		goto unlock_exit;
	}
	feat_desc->params_desc[param_idx].value = value;

	/* update loaded instances.*/
	list_for_each_entry(csdev_item, &cscfg_mgr->csdev_desc_list, item) {
		feat_csdev = cscfg_csdev_get_feat_from_desc(csdev_item->csdev, feat_desc);
		if (feat_csdev)
			feat_csdev->params_csdev[param_idx].current_value = value;
	}

unlock_exit:
	mutex_unlock(&cscfg_mutex);
	return err;
}

/*
 * Conditionally up reference count on owner to prevent unload.
 *
 * module loaded configs need to be locked in to prevent premature unload.
 */
static int cscfg_owner_get(struct cscfg_load_owner_info *owner_info)
{
	if ((owner_info->type == CSCFG_OWNER_MODULE) &&
	    (!try_module_get(owner_info->owner_handle)))
		return -EINVAL;
	return 0;
}

/* conditionally lower ref count on an owner */
static void cscfg_owner_put(struct cscfg_load_owner_info *owner_info)
{
	if (owner_info->type == CSCFG_OWNER_MODULE)
		module_put(owner_info->owner_handle);
}

static void cscfg_remove_owned_csdev_configs(struct coresight_device *csdev, void *load_owner)
{
	struct cscfg_config_csdev *config_csdev, *tmp;

	if (list_empty(&csdev->config_csdev_list))
		return;

	list_for_each_entry_safe(config_csdev, tmp, &csdev->config_csdev_list, node) {
		if (config_csdev->config_desc->load_owner == load_owner)
			list_del(&config_csdev->node);
	}
}

static void cscfg_remove_owned_csdev_features(struct coresight_device *csdev, void *load_owner)
{
	struct cscfg_feature_csdev *feat_csdev, *tmp;

	if (list_empty(&csdev->feature_csdev_list))
		return;

	list_for_each_entry_safe(feat_csdev, tmp, &csdev->feature_csdev_list, node) {
		if (feat_csdev->feat_desc->load_owner == load_owner)
			list_del(&feat_csdev->node);
	}
}

/*
 * removal is relatively easy - just remove from all lists, anything that
 * matches the owner. Memory for the descriptors will be managed by the owner,
 * memory for the csdev items is devm_ allocated with the individual csdev
 * devices.
 */
static void cscfg_unload_owned_cfgs_feats(void *load_owner)
{
	struct cscfg_config_desc *config_desc, *cfg_tmp;
	struct cscfg_feature_desc *feat_desc, *feat_tmp;
	struct cscfg_registered_csdev *csdev_item;

	/* remove from each csdev instance feature and config lists */
	list_for_each_entry(csdev_item, &cscfg_mgr->csdev_desc_list, item) {
		/*
		 * for each csdev, check the loaded lists and remove if
		 * referenced descriptor is owned
		 */
		cscfg_remove_owned_csdev_configs(csdev_item->csdev, load_owner);
		cscfg_remove_owned_csdev_features(csdev_item->csdev, load_owner);
	}

	/* remove from the config descriptor lists */
	list_for_each_entry_safe(config_desc, cfg_tmp, &cscfg_mgr->config_desc_list, item) {
		if (config_desc->load_owner == load_owner) {
			cscfg_configfs_del_config(config_desc);
			etm_perf_del_symlink_cscfg(config_desc);
			list_del(&config_desc->item);
		}
	}

	/* remove from the feature descriptor lists */
	list_for_each_entry_safe(feat_desc, feat_tmp, &cscfg_mgr->feat_desc_list, item) {
		if (feat_desc->load_owner == load_owner) {
			cscfg_configfs_del_feature(feat_desc);
			list_del(&feat_desc->item);
		}
	}
}

/**
 * cscfg_load_config_sets - API function to load feature and config sets.
 *
 * Take a 0 terminated array of feature descriptors and/or configuration
 * descriptors and load into the system.
 * Features are loaded first to ensure configuration dependencies can be met.
 *
 * To facilitate dynamic loading and unloading, features and configurations
 * have a "load_owner", to allow later unload by the same owner. An owner may
 * be a loadable module or configuration dynamically created via configfs.
 * As later loaded configurations can use earlier loaded features, creating load
 * dependencies, a load order list is maintained. Unload is strictly in the
 * reverse order to load.
 *
 * @config_descs: 0 terminated array of configuration descriptors.
 * @feat_descs:   0 terminated array of feature descriptors.
 * @owner_info:	  Information on the owner of this set.
 */
int cscfg_load_config_sets(struct cscfg_config_desc **config_descs,
			   struct cscfg_feature_desc **feat_descs,
			   struct cscfg_load_owner_info *owner_info)
{
	int err = 0, i = 0;

	mutex_lock(&cscfg_mutex);

	/* load features first */
	if (feat_descs) {
		while (feat_descs[i]) {
			err = cscfg_load_feat(feat_descs[i]);
			if (!err)
				err = cscfg_configfs_add_feature(feat_descs[i]);
			if (err) {
				pr_err("coresight-syscfg: Failed to load feature %s\n",
				       feat_descs[i]->name);
				cscfg_unload_owned_cfgs_feats(owner_info);
				goto exit_unlock;
			}
			feat_descs[i]->load_owner = owner_info;
			i++;
		}
	}

	/* next any configurations to check feature dependencies */
	i = 0;
	if (config_descs) {
		while (config_descs[i]) {
			err = cscfg_load_config(config_descs[i]);
			if (!err)
				err = cscfg_configfs_add_config(config_descs[i]);
			if (err) {
				pr_err("coresight-syscfg: Failed to load configuration %s\n",
				       config_descs[i]->name);
				cscfg_unload_owned_cfgs_feats(owner_info);
				goto exit_unlock;
			}
			config_descs[i]->load_owner = owner_info;
			i++;
		}
	}

	/* add the load owner to the load order list */
	list_add_tail(&owner_info->item, &cscfg_mgr->load_order_list);
	if (!list_is_singular(&cscfg_mgr->load_order_list)) {
		/* lock previous item in load order list */
		err = cscfg_owner_get(list_prev_entry(owner_info, item));
		if (err) {
			cscfg_unload_owned_cfgs_feats(owner_info);
			list_del(&owner_info->item);
		}
	}

exit_unlock:
	mutex_unlock(&cscfg_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(cscfg_load_config_sets);

/**
 * cscfg_unload_config_sets - unload a set of configurations by owner.
 *
 * Dynamic unload of configuration and feature sets is done on the basis of
 * the load owner of that set. Later loaded configurations can depend on
 * features loaded earlier.
 *
 * Therefore, unload is only possible if:-
 * 1) no configurations are active.
 * 2) the set being unloaded was the last to be loaded to maintain dependencies.
 *
 * @owner_info:	Information on owner for set being unloaded.
 */
int cscfg_unload_config_sets(struct cscfg_load_owner_info *owner_info)
{
	int err = 0;
	struct cscfg_load_owner_info *load_list_item = NULL;

	mutex_lock(&cscfg_mutex);

	/* cannot unload if anything is active */
	if (atomic_read(&cscfg_mgr->sys_active_cnt)) {
		err = -EBUSY;
		goto exit_unlock;
	}

	/* cannot unload if not last loaded in load order */
	if (!list_empty(&cscfg_mgr->load_order_list)) {
		load_list_item = list_last_entry(&cscfg_mgr->load_order_list,
						 struct cscfg_load_owner_info, item);
		if (load_list_item != owner_info)
			load_list_item = NULL;
	}

	if (!load_list_item) {
		err = -EINVAL;
		goto exit_unlock;
	}

	/* unload all belonging to load_owner */
	cscfg_unload_owned_cfgs_feats(owner_info);

	/* remove from load order list */
	if (!list_is_singular(&cscfg_mgr->load_order_list)) {
		/* unlock previous item in load order list */
		cscfg_owner_put(list_prev_entry(owner_info, item));
	}
	list_del(&owner_info->item);

exit_unlock:
	mutex_unlock(&cscfg_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(cscfg_unload_config_sets);

/* Handle coresight device registration and add configs and features to devices */

/* iterate through config lists and load matching configs to device */
static int cscfg_add_cfgs_csdev(struct coresight_device *csdev)
{
	struct cscfg_config_desc *config_desc;
	int err = 0;

	list_for_each_entry(config_desc, &cscfg_mgr->config_desc_list, item) {
		err = cscfg_add_csdev_cfg(csdev, config_desc);
		if (err)
			break;
	}
	return err;
}

/* iterate through feature lists and load matching features to device */
static int cscfg_add_feats_csdev(struct coresight_device *csdev,
				 u32 match_flags,
				 struct cscfg_csdev_feat_ops *ops)
{
	struct cscfg_feature_desc *feat_desc;
	int err = 0;

	if (!ops->load_feat)
		return -EINVAL;

	list_for_each_entry(feat_desc, &cscfg_mgr->feat_desc_list, item) {
		if (feat_desc->match_flags & match_flags) {
			err = cscfg_load_feat_csdev(csdev, feat_desc, ops);
			if (err)
				break;
		}
	}
	return err;
}

/* Add coresight device to list and copy its matching info */
static int cscfg_list_add_csdev(struct coresight_device *csdev,
				u32 match_flags,
				struct cscfg_csdev_feat_ops *ops)
{
	struct cscfg_registered_csdev *csdev_item;

	/* allocate the list entry structure */
	csdev_item = kzalloc(sizeof(struct cscfg_registered_csdev), GFP_KERNEL);
	if (!csdev_item)
		return -ENOMEM;

	csdev_item->csdev = csdev;
	csdev_item->match_flags = match_flags;
	csdev_item->ops.load_feat = ops->load_feat;
	list_add(&csdev_item->item, &cscfg_mgr->csdev_desc_list);

	INIT_LIST_HEAD(&csdev->feature_csdev_list);
	INIT_LIST_HEAD(&csdev->config_csdev_list);
	spin_lock_init(&csdev->cscfg_csdev_lock);

	return 0;
}

/* remove a coresight device from the list and free data */
static void cscfg_list_remove_csdev(struct coresight_device *csdev)
{
	struct cscfg_registered_csdev *csdev_item, *tmp;

	list_for_each_entry_safe(csdev_item, tmp, &cscfg_mgr->csdev_desc_list, item) {
		if (csdev_item->csdev == csdev) {
			list_del(&csdev_item->item);
			kfree(csdev_item);
			break;
		}
	}
}

/**
 * cscfg_register_csdev - register a coresight device with the syscfg manager.
 *
 * Registers the coresight device with the system. @match_flags used to check
 * if the device is a match for registered features. Any currently registered
 * configurations and features that match the device will be loaded onto it.
 *
 * @csdev:		The coresight device to register.
 * @match_flags:	Matching information to load features.
 * @ops:		Standard operations supported by the device.
 */
int cscfg_register_csdev(struct coresight_device *csdev,
			 u32 match_flags,
			 struct cscfg_csdev_feat_ops *ops)
{
	int ret = 0;

	mutex_lock(&cscfg_mutex);

	/* add device to list of registered devices  */
	ret = cscfg_list_add_csdev(csdev, match_flags, ops);
	if (ret)
		goto reg_csdev_unlock;

	/* now load any registered features and configs matching the device. */
	ret = cscfg_add_feats_csdev(csdev, match_flags, ops);
	if (ret) {
		cscfg_list_remove_csdev(csdev);
		goto reg_csdev_unlock;
	}

	ret = cscfg_add_cfgs_csdev(csdev);
	if (ret) {
		cscfg_list_remove_csdev(csdev);
		goto reg_csdev_unlock;
	}

	pr_info("CSCFG registered %s", dev_name(&csdev->dev));

reg_csdev_unlock:
	mutex_unlock(&cscfg_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(cscfg_register_csdev);

/**
 * cscfg_unregister_csdev - remove coresight device from syscfg manager.
 *
 * @csdev: Device to remove.
 */
void cscfg_unregister_csdev(struct coresight_device *csdev)
{
	mutex_lock(&cscfg_mutex);
	cscfg_list_remove_csdev(csdev);
	mutex_unlock(&cscfg_mutex);
}
EXPORT_SYMBOL_GPL(cscfg_unregister_csdev);

/**
 * cscfg_csdev_reset_feats - reset features for a CoreSight device.
 *
 * Resets all parameters and register values for any features loaded
 * into @csdev to their default values.
 *
 * @csdev: The CoreSight device.
 */
void cscfg_csdev_reset_feats(struct coresight_device *csdev)
{
	struct cscfg_feature_csdev *feat_csdev;
	unsigned long flags;

	spin_lock_irqsave(&csdev->cscfg_csdev_lock, flags);
	if (list_empty(&csdev->feature_csdev_list))
		goto unlock_exit;

	list_for_each_entry(feat_csdev, &csdev->feature_csdev_list, node)
		cscfg_reset_feat(feat_csdev);

unlock_exit:
	spin_unlock_irqrestore(&csdev->cscfg_csdev_lock, flags);
}
EXPORT_SYMBOL_GPL(cscfg_csdev_reset_feats);

/*
 * This activate configuration for either perf or sysfs. Perf can have multiple
 * active configs, selected per event, sysfs is limited to one.
 *
 * Increments the configuration descriptor active count and the global active
 * count.
 *
 * @cfg_hash: Hash value of the selected configuration name.
 */
static int _cscfg_activate_config(unsigned long cfg_hash)
{
	struct cscfg_config_desc *config_desc;
	int err = -EINVAL;

	list_for_each_entry(config_desc, &cscfg_mgr->config_desc_list, item) {
		if ((unsigned long)config_desc->event_ea->var == cfg_hash) {
			/* must ensure that config cannot be unloaded in use */
			err = cscfg_owner_get(config_desc->load_owner);
			if (err)
				break;
			/*
			 * increment the global active count - control changes to
			 * active configurations
			 */
			atomic_inc(&cscfg_mgr->sys_active_cnt);

			/*
			 * mark the descriptor as active so enable config on a
			 * device instance will use it
			 */
			atomic_inc(&config_desc->active_cnt);

			err = 0;
			dev_dbg(cscfg_device(), "Activate config %s.\n", config_desc->name);
			break;
		}
	}
	return err;
}

static void _cscfg_deactivate_config(unsigned long cfg_hash)
{
	struct cscfg_config_desc *config_desc;

	list_for_each_entry(config_desc, &cscfg_mgr->config_desc_list, item) {
		if ((unsigned long)config_desc->event_ea->var == cfg_hash) {
			atomic_dec(&config_desc->active_cnt);
			atomic_dec(&cscfg_mgr->sys_active_cnt);
			cscfg_owner_put(config_desc->load_owner);
			dev_dbg(cscfg_device(), "Deactivate config %s.\n", config_desc->name);
			break;
		}
	}
}

/*
 * called from configfs to set/clear the active configuration for use when
 * using sysfs to control trace.
 */
int cscfg_config_sysfs_activate(struct cscfg_config_desc *config_desc, bool activate)
{
	unsigned long cfg_hash;
	int err = 0;

	mutex_lock(&cscfg_mutex);

	cfg_hash = (unsigned long)config_desc->event_ea->var;

	if (activate) {
		/* cannot be a current active value to activate this */
		if (cscfg_mgr->sysfs_active_config) {
			err = -EBUSY;
			goto exit_unlock;
		}
		err = _cscfg_activate_config(cfg_hash);
		if (!err)
			cscfg_mgr->sysfs_active_config = cfg_hash;
	} else {
		/* disable if matching current value */
		if (cscfg_mgr->sysfs_active_config == cfg_hash) {
			_cscfg_deactivate_config(cfg_hash);
			cscfg_mgr->sysfs_active_config = 0;
		} else
			err = -EINVAL;
	}

exit_unlock:
	mutex_unlock(&cscfg_mutex);
	return err;
}

/* set the sysfs preset value */
void cscfg_config_sysfs_set_preset(int preset)
{
	mutex_lock(&cscfg_mutex);
	cscfg_mgr->sysfs_active_preset = preset;
	mutex_unlock(&cscfg_mutex);
}

/*
 * Used by a device to get the config and preset selected as active in configfs,
 * when using sysfs to control trace.
 */
void cscfg_config_sysfs_get_active_cfg(unsigned long *cfg_hash, int *preset)
{
	mutex_lock(&cscfg_mutex);
	*preset = cscfg_mgr->sysfs_active_preset;
	*cfg_hash = cscfg_mgr->sysfs_active_config;
	mutex_unlock(&cscfg_mutex);
}
EXPORT_SYMBOL_GPL(cscfg_config_sysfs_get_active_cfg);

/**
 * cscfg_activate_config -  Mark a configuration descriptor as active.
 *
 * This will be seen when csdev devices are enabled in the system.
 * Only activated configurations can be enabled on individual devices.
 * Activation protects the configuration from alteration or removal while
 * active.
 *
 * Selection by hash value - generated from the configuration name when it
 * was loaded and added to the cs_etm/configurations file system for selection
 * by perf.
 *
 * @cfg_hash: Hash value of the selected configuration name.
 */
int cscfg_activate_config(unsigned long cfg_hash)
{
	int err = 0;

	mutex_lock(&cscfg_mutex);
	err = _cscfg_activate_config(cfg_hash);
	mutex_unlock(&cscfg_mutex);

	return err;
}
EXPORT_SYMBOL_GPL(cscfg_activate_config);

/**
 * cscfg_deactivate_config -  Mark a config descriptor as inactive.
 *
 * Decrement the configuration and global active counts.
 *
 * @cfg_hash: Hash value of the selected configuration name.
 */
void cscfg_deactivate_config(unsigned long cfg_hash)
{
	mutex_lock(&cscfg_mutex);
	_cscfg_deactivate_config(cfg_hash);
	mutex_unlock(&cscfg_mutex);
}
EXPORT_SYMBOL_GPL(cscfg_deactivate_config);

/**
 * cscfg_csdev_enable_active_config - Enable matching active configuration for device.
 *
 * Enables the configuration selected by @cfg_hash if the configuration is supported
 * on the device and has been activated.
 *
 * If active and supported the CoreSight device @csdev will be programmed with the
 * configuration, using @preset parameters.
 *
 * Should be called before driver hardware enable for the requested device, prior to
 * programming and enabling the physical hardware.
 *
 * @csdev:	CoreSight device to program.
 * @cfg_hash:	Selector for the configuration.
 * @preset:	Preset parameter values to use, 0 for current / default values.
 */
int cscfg_csdev_enable_active_config(struct coresight_device *csdev,
				     unsigned long cfg_hash, int preset)
{
	struct cscfg_config_csdev *config_csdev_active = NULL, *config_csdev_item;
	const struct cscfg_config_desc *config_desc;
	unsigned long flags;
	int err = 0;

	/* quickly check global count */
	if (!atomic_read(&cscfg_mgr->sys_active_cnt))
		return 0;

	/*
	 * Look for matching configuration - set the active configuration
	 * context if found.
	 */
	spin_lock_irqsave(&csdev->cscfg_csdev_lock, flags);
	list_for_each_entry(config_csdev_item, &csdev->config_csdev_list, node) {
		config_desc = config_csdev_item->config_desc;
		if ((atomic_read(&config_desc->active_cnt)) &&
		    ((unsigned long)config_desc->event_ea->var == cfg_hash)) {
			config_csdev_active = config_csdev_item;
			csdev->active_cscfg_ctxt = (void *)config_csdev_active;
			break;
		}
	}
	spin_unlock_irqrestore(&csdev->cscfg_csdev_lock, flags);

	/*
	 * If found, attempt to enable
	 */
	if (config_csdev_active) {
		/*
		 * Call the generic routine that will program up the internal
		 * driver structures prior to programming up the hardware.
		 * This routine takes the driver spinlock saved in the configs.
		 */
		err = cscfg_csdev_enable_config(config_csdev_active, preset);
		if (!err) {
			/*
			 * Successful programming. Check the active_cscfg_ctxt
			 * pointer to ensure no pre-emption disabled it via
			 * cscfg_csdev_disable_active_config() before
			 * we could start.
			 *
			 * Set enabled if OK, err if not.
			 */
			spin_lock_irqsave(&csdev->cscfg_csdev_lock, flags);
			if (csdev->active_cscfg_ctxt)
				config_csdev_active->enabled = true;
			else
				err = -EBUSY;
			spin_unlock_irqrestore(&csdev->cscfg_csdev_lock, flags);
		}
	}
	return err;
}
EXPORT_SYMBOL_GPL(cscfg_csdev_enable_active_config);

/**
 * cscfg_csdev_disable_active_config - disable an active config on the device.
 *
 * Disables the active configuration on the CoreSight device @csdev.
 * Disable will save the values of any registers marked in the configurations
 * as save on disable.
 *
 * Should be called after driver hardware disable for the requested device,
 * after disabling the physical hardware and reading back registers.
 *
 * @csdev: The CoreSight device.
 */
void cscfg_csdev_disable_active_config(struct coresight_device *csdev)
{
	struct cscfg_config_csdev *config_csdev;
	unsigned long flags;

	/*
	 * Check if we have an active config, and that it was successfully enabled.
	 * If it was not enabled, we have no work to do, otherwise mark as disabled.
	 * Clear the active config pointer.
	 */
	spin_lock_irqsave(&csdev->cscfg_csdev_lock, flags);
	config_csdev = (struct cscfg_config_csdev *)csdev->active_cscfg_ctxt;
	if (config_csdev) {
		if (!config_csdev->enabled)
			config_csdev = NULL;
		else
			config_csdev->enabled = false;
	}
	csdev->active_cscfg_ctxt = NULL;
	spin_unlock_irqrestore(&csdev->cscfg_csdev_lock, flags);

	/* true if there was an enabled active config */
	if (config_csdev)
		cscfg_csdev_disable_config(config_csdev);
}
EXPORT_SYMBOL_GPL(cscfg_csdev_disable_active_config);

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
	struct cscfg_config_desc *cfg_desc;

	mutex_lock(&cscfg_mutex);
	list_for_each_entry(cfg_desc, &cscfg_mgr->config_desc_list, item) {
		etm_perf_del_symlink_cscfg(cfg_desc);
	}
	cscfg_configfs_release(cscfg_mgr);
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

	err = cscfg_configfs_init(cscfg_mgr);
	if (err)
		goto exit_err;

	INIT_LIST_HEAD(&cscfg_mgr->csdev_desc_list);
	INIT_LIST_HEAD(&cscfg_mgr->feat_desc_list);
	INIT_LIST_HEAD(&cscfg_mgr->config_desc_list);
	INIT_LIST_HEAD(&cscfg_mgr->load_order_list);
	atomic_set(&cscfg_mgr->sys_active_cnt, 0);

	/* preload built-in configurations */
	err = cscfg_preload(THIS_MODULE);
	if (err)
		goto exit_err;

	dev_info(cscfg_device(), "CoreSight Configuration manager initialised");
	return 0;

exit_err:
	cscfg_clear_device();
	return err;
}

void cscfg_exit(void)
{
	cscfg_clear_device();
}
