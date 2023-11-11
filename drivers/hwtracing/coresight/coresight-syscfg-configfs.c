// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/configfs.h>

#include "coresight-config.h"
#include "coresight-syscfg-configfs.h"

/* create a default ci_type. */
static inline struct config_item_type *cscfg_create_ci_type(void)
{
	struct config_item_type *ci_type;

	ci_type = devm_kzalloc(cscfg_device(), sizeof(*ci_type), GFP_KERNEL);
	if (ci_type)
		ci_type->ct_owner = THIS_MODULE;

	return ci_type;
}

/* configurations sub-group */

/* attributes for the config view group */
static ssize_t cscfg_cfg_description_show(struct config_item *item, char *page)
{
	struct cscfg_fs_config *fs_config = container_of(to_config_group(item),
							 struct cscfg_fs_config, group);

	return scnprintf(page, PAGE_SIZE, "%s", fs_config->config_desc->description);
}
CONFIGFS_ATTR_RO(cscfg_cfg_, description);

static ssize_t cscfg_cfg_feature_refs_show(struct config_item *item, char *page)
{
	struct cscfg_fs_config *fs_config = container_of(to_config_group(item),
							 struct cscfg_fs_config, group);
	const struct cscfg_config_desc *config_desc = fs_config->config_desc;
	ssize_t ch_used = 0;
	int i;

	for (i = 0; i < config_desc->nr_feat_refs; i++)
		ch_used += scnprintf(page + ch_used, PAGE_SIZE - ch_used,
				     "%s\n", config_desc->feat_ref_names[i]);
	return ch_used;
}
CONFIGFS_ATTR_RO(cscfg_cfg_, feature_refs);

/* list preset values in order of features and params */
static ssize_t cscfg_cfg_values_show(struct config_item *item, char *page)
{
	const struct cscfg_feature_desc *feat_desc;
	const struct cscfg_config_desc *config_desc;
	struct cscfg_fs_preset *fs_preset;
	int i, j, val_idx, preset_idx;
	ssize_t used = 0;

	fs_preset = container_of(to_config_group(item), struct cscfg_fs_preset, group);
	config_desc = fs_preset->config_desc;

	if (!config_desc->nr_presets)
		return 0;

	preset_idx = fs_preset->preset_num - 1;

	/* start index on the correct array line */
	val_idx = config_desc->nr_total_params * preset_idx;

	/*
	 * A set of presets is the sum of all params in used features,
	 * in order of declaration of features and params in the features
	 */
	for (i = 0; i < config_desc->nr_feat_refs; i++) {
		feat_desc = cscfg_get_named_feat_desc(config_desc->feat_ref_names[i]);
		for (j = 0; j < feat_desc->nr_params; j++) {
			used += scnprintf(page + used, PAGE_SIZE - used,
					  "%s.%s = 0x%llx ",
					  feat_desc->name,
					  feat_desc->params_desc[j].name,
					  config_desc->presets[val_idx++]);
		}
	}
	used += scnprintf(page + used, PAGE_SIZE - used, "\n");

	return used;
}
CONFIGFS_ATTR_RO(cscfg_cfg_, values);

static ssize_t cscfg_cfg_enable_show(struct config_item *item, char *page)
{
	struct cscfg_fs_config *fs_config = container_of(to_config_group(item),
							 struct cscfg_fs_config, group);

	return scnprintf(page, PAGE_SIZE, "%d\n", fs_config->active);
}

static ssize_t cscfg_cfg_enable_store(struct config_item *item,
					const char *page, size_t count)
{
	struct cscfg_fs_config *fs_config = container_of(to_config_group(item),
							 struct cscfg_fs_config, group);
	int err;
	bool val;

	err = kstrtobool(page, &val);
	if (!err)
		err = cscfg_config_sysfs_activate(fs_config->config_desc, val);
	if (!err) {
		fs_config->active = val;
		if (val)
			cscfg_config_sysfs_set_preset(fs_config->preset);
	}
	return err ? err : count;
}
CONFIGFS_ATTR(cscfg_cfg_, enable);

static ssize_t cscfg_cfg_preset_show(struct config_item *item, char *page)
{
	struct cscfg_fs_config *fs_config = container_of(to_config_group(item),
							 struct cscfg_fs_config, group);

	return scnprintf(page, PAGE_SIZE, "%d\n", fs_config->preset);
}

static ssize_t cscfg_cfg_preset_store(struct config_item *item,
					     const char *page, size_t count)
{
	struct cscfg_fs_config *fs_config = container_of(to_config_group(item),
							 struct cscfg_fs_config, group);
	int preset, err;

	err = kstrtoint(page, 0, &preset);
	if (!err) {
		/*
		 * presets start at 1, and go up to max (15),
		 * but the config may provide fewer.
		 */
		if ((preset < 1) || (preset > fs_config->config_desc->nr_presets))
			err = -EINVAL;
	}

	if (!err) {
		/* set new value */
		fs_config->preset = preset;
		/* set on system if active */
		if (fs_config->active)
			cscfg_config_sysfs_set_preset(fs_config->preset);
	}
	return err ? err : count;
}
CONFIGFS_ATTR(cscfg_cfg_, preset);

static struct configfs_attribute *cscfg_config_view_attrs[] = {
	&cscfg_cfg_attr_description,
	&cscfg_cfg_attr_feature_refs,
	&cscfg_cfg_attr_enable,
	&cscfg_cfg_attr_preset,
	NULL,
};

static struct config_item_type cscfg_config_view_type = {
	.ct_owner = THIS_MODULE,
	.ct_attrs = cscfg_config_view_attrs,
};

static struct configfs_attribute *cscfg_config_preset_attrs[] = {
	&cscfg_cfg_attr_values,
	NULL,
};

static struct config_item_type cscfg_config_preset_type = {
	.ct_owner = THIS_MODULE,
	.ct_attrs = cscfg_config_preset_attrs,
};

static int cscfg_add_preset_groups(struct cscfg_fs_config *cfg_view)
{
	int preset_num;
	struct cscfg_fs_preset *cfg_fs_preset;
	struct cscfg_config_desc *config_desc = cfg_view->config_desc;
	char name[CONFIGFS_ITEM_NAME_LEN];

	if (!config_desc->nr_presets)
		return 0;

	for (preset_num = 1; preset_num <= config_desc->nr_presets; preset_num++) {
		cfg_fs_preset = devm_kzalloc(cscfg_device(),
					     sizeof(struct cscfg_fs_preset), GFP_KERNEL);

		if (!cfg_fs_preset)
			return -ENOMEM;

		snprintf(name, CONFIGFS_ITEM_NAME_LEN, "preset%d", preset_num);
		cfg_fs_preset->preset_num = preset_num;
		cfg_fs_preset->config_desc = cfg_view->config_desc;
		config_group_init_type_name(&cfg_fs_preset->group, name,
					    &cscfg_config_preset_type);
		configfs_add_default_group(&cfg_fs_preset->group, &cfg_view->group);
	}
	return 0;
}

static struct config_group *cscfg_create_config_group(struct cscfg_config_desc *config_desc)
{
	struct cscfg_fs_config *cfg_view;
	struct device *dev = cscfg_device();
	int err;

	if (!dev)
		return ERR_PTR(-EINVAL);

	cfg_view = devm_kzalloc(dev, sizeof(struct cscfg_fs_config), GFP_KERNEL);
	if (!cfg_view)
		return ERR_PTR(-ENOMEM);

	cfg_view->config_desc = config_desc;
	config_group_init_type_name(&cfg_view->group, config_desc->name, &cscfg_config_view_type);

	/* add in a preset<n> dir for each preset */
	err = cscfg_add_preset_groups(cfg_view);
	if (err)
		return ERR_PTR(err);

	return &cfg_view->group;
}

/* attributes for features view */

static ssize_t cscfg_feat_description_show(struct config_item *item, char *page)
{
	struct cscfg_fs_feature *fs_feat = container_of(to_config_group(item),
							struct cscfg_fs_feature, group);

	return scnprintf(page, PAGE_SIZE, "%s", fs_feat->feat_desc->description);
}
CONFIGFS_ATTR_RO(cscfg_feat_, description);

static ssize_t cscfg_feat_matches_show(struct config_item *item, char *page)
{
	struct cscfg_fs_feature *fs_feat = container_of(to_config_group(item),
							struct cscfg_fs_feature, group);
	u32 match_flags = fs_feat->feat_desc->match_flags;
	int used = 0;

	if (match_flags & CS_CFG_MATCH_CLASS_SRC_ALL)
		used = scnprintf(page, PAGE_SIZE, "SRC_ALL ");

	if (match_flags & CS_CFG_MATCH_CLASS_SRC_ETM4)
		used += scnprintf(page + used, PAGE_SIZE - used, "SRC_ETMV4 ");

	used += scnprintf(page + used, PAGE_SIZE - used, "\n");
	return used;
}
CONFIGFS_ATTR_RO(cscfg_feat_, matches);

static ssize_t cscfg_feat_nr_params_show(struct config_item *item, char *page)
{
	struct cscfg_fs_feature *fs_feat = container_of(to_config_group(item),
							struct cscfg_fs_feature, group);

	return scnprintf(page, PAGE_SIZE, "%d\n", fs_feat->feat_desc->nr_params);
}
CONFIGFS_ATTR_RO(cscfg_feat_, nr_params);

/* base feature desc attrib structures */
static struct configfs_attribute *cscfg_feature_view_attrs[] = {
	&cscfg_feat_attr_description,
	&cscfg_feat_attr_matches,
	&cscfg_feat_attr_nr_params,
	NULL,
};

static struct config_item_type cscfg_feature_view_type = {
	.ct_owner = THIS_MODULE,
	.ct_attrs = cscfg_feature_view_attrs,
};

static ssize_t cscfg_param_value_show(struct config_item *item, char *page)
{
	struct cscfg_fs_param *param_item = container_of(to_config_group(item),
							 struct cscfg_fs_param, group);
	u64 value = param_item->feat_desc->params_desc[param_item->param_idx].value;

	return scnprintf(page, PAGE_SIZE, "0x%llx\n", value);
}

static ssize_t cscfg_param_value_store(struct config_item *item,
				       const char *page, size_t size)
{
	struct cscfg_fs_param *param_item = container_of(to_config_group(item),
							 struct cscfg_fs_param, group);
	struct cscfg_feature_desc *feat_desc = param_item->feat_desc;
	int param_idx = param_item->param_idx;
	u64 value;
	int err;

	err = kstrtoull(page, 0, &value);
	if (!err)
		err = cscfg_update_feat_param_val(feat_desc, param_idx, value);

	return err ? err : size;
}
CONFIGFS_ATTR(cscfg_param_, value);

static struct configfs_attribute *cscfg_param_view_attrs[] = {
	&cscfg_param_attr_value,
	NULL,
};

static struct config_item_type cscfg_param_view_type = {
	.ct_owner = THIS_MODULE,
	.ct_attrs = cscfg_param_view_attrs,
};

/*
 * configfs has far less functionality provided to add attributes dynamically than sysfs,
 * and the show and store fns pass the enclosing config_item so the actual attribute cannot
 * be determined. Therefore we add each item as a group directory, with a value attribute.
 */
static int cscfg_create_params_group_items(struct cscfg_feature_desc *feat_desc,
					   struct config_group *params_group)
{
	struct device *dev = cscfg_device();
	struct cscfg_fs_param *param_item;
	int i;

	/* parameter items - as groups with default_value attribute */
	for (i = 0; i < feat_desc->nr_params; i++) {
		param_item = devm_kzalloc(dev, sizeof(struct cscfg_fs_param), GFP_KERNEL);
		if (!param_item)
			return -ENOMEM;
		param_item->feat_desc = feat_desc;
		param_item->param_idx = i;
		config_group_init_type_name(&param_item->group,
					    feat_desc->params_desc[i].name,
					    &cscfg_param_view_type);
		configfs_add_default_group(&param_item->group, params_group);
	}
	return 0;
}

static struct config_group *cscfg_create_feature_group(struct cscfg_feature_desc *feat_desc)
{
	struct cscfg_fs_feature *feat_view;
	struct config_item_type *params_group_type;
	struct config_group *params_group = NULL;
	struct device *dev = cscfg_device();
	int item_err;

	if (!dev)
		return ERR_PTR(-EINVAL);

	feat_view = devm_kzalloc(dev, sizeof(struct cscfg_fs_feature), GFP_KERNEL);
	if (!feat_view)
		return ERR_PTR(-ENOMEM);

	if (feat_desc->nr_params) {
		params_group = devm_kzalloc(dev, sizeof(struct config_group), GFP_KERNEL);
		if (!params_group)
			return ERR_PTR(-ENOMEM);

		params_group_type = cscfg_create_ci_type();
		if (!params_group_type)
			return ERR_PTR(-ENOMEM);
	}

	feat_view->feat_desc = feat_desc;
	config_group_init_type_name(&feat_view->group,
				    feat_desc->name,
				    &cscfg_feature_view_type);
	if (params_group) {
		config_group_init_type_name(params_group, "params", params_group_type);
		configfs_add_default_group(params_group, &feat_view->group);
		item_err = cscfg_create_params_group_items(feat_desc, params_group);
		if (item_err)
			return ERR_PTR(item_err);
	}
	return &feat_view->group;
}

static struct config_item_type cscfg_configs_type = {
	.ct_owner = THIS_MODULE,
};

static struct config_group cscfg_configs_grp = {
	.cg_item = {
		.ci_namebuf = "configurations",
		.ci_type = &cscfg_configs_type,
	},
};

/* add configuration to configurations group */
int cscfg_configfs_add_config(struct cscfg_config_desc *config_desc)
{
	struct config_group *new_group;
	int err;

	new_group = cscfg_create_config_group(config_desc);
	if (IS_ERR(new_group))
		return PTR_ERR(new_group);
	err =  configfs_register_group(&cscfg_configs_grp, new_group);
	if (!err)
		config_desc->fs_group = new_group;
	return err;
}

void cscfg_configfs_del_config(struct cscfg_config_desc *config_desc)
{
	if (config_desc->fs_group) {
		configfs_unregister_group(config_desc->fs_group);
		config_desc->fs_group = NULL;
	}
}

static struct config_item_type cscfg_features_type = {
	.ct_owner = THIS_MODULE,
};

static struct config_group cscfg_features_grp = {
	.cg_item = {
		.ci_namebuf = "features",
		.ci_type = &cscfg_features_type,
	},
};

/* add feature to features group */
int cscfg_configfs_add_feature(struct cscfg_feature_desc *feat_desc)
{
	struct config_group *new_group;
	int err;

	new_group = cscfg_create_feature_group(feat_desc);
	if (IS_ERR(new_group))
		return PTR_ERR(new_group);
	err =  configfs_register_group(&cscfg_features_grp, new_group);
	if (!err)
		feat_desc->fs_group = new_group;
	return err;
}

void cscfg_configfs_del_feature(struct cscfg_feature_desc *feat_desc)
{
	if (feat_desc->fs_group) {
		configfs_unregister_group(feat_desc->fs_group);
		feat_desc->fs_group = NULL;
	}
}

int cscfg_configfs_init(struct cscfg_manager *cscfg_mgr)
{
	struct configfs_subsystem *subsys;
	struct config_item_type *ci_type;

	if (!cscfg_mgr)
		return -EINVAL;

	ci_type = cscfg_create_ci_type();
	if (!ci_type)
		return -ENOMEM;

	subsys = &cscfg_mgr->cfgfs_subsys;
	config_item_set_name(&subsys->su_group.cg_item, CSCFG_FS_SUBSYS_NAME);
	subsys->su_group.cg_item.ci_type = ci_type;

	config_group_init(&subsys->su_group);
	mutex_init(&subsys->su_mutex);

	/* Add default groups to subsystem */
	config_group_init(&cscfg_configs_grp);
	configfs_add_default_group(&cscfg_configs_grp, &subsys->su_group);

	config_group_init(&cscfg_features_grp);
	configfs_add_default_group(&cscfg_features_grp, &subsys->su_group);

	return configfs_register_subsystem(subsys);
}

void cscfg_configfs_release(struct cscfg_manager *cscfg_mgr)
{
	configfs_unregister_subsystem(&cscfg_mgr->cfgfs_subsys);
}
