// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2020 Linaro Limited. All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/sysfs.h>
#include "coresight-config.h"
#include "coresight-priv.h"

/*
 * This provides a set of generic functions that operate on configurations
 * and features to manage the handling of parameters, the programming and
 * saving of registers used by features on devices.
 */

/*
 * Write the value held in the register structure into the driver internal memory
 * location.
 */
static void cscfg_set_reg(struct cscfg_regval_csdev *reg_csdev)
{
	u32 *p_val32 = (u32 *)reg_csdev->driver_regval;
	u32 tmp32 = reg_csdev->reg_desc.val32;

	if (reg_csdev->reg_desc.type & CS_CFG_REG_TYPE_VAL_64BIT) {
		*((u64 *)reg_csdev->driver_regval) = reg_csdev->reg_desc.val64;
		return;
	}

	if (reg_csdev->reg_desc.type & CS_CFG_REG_TYPE_VAL_MASK) {
		tmp32 = *p_val32;
		tmp32 &= ~reg_csdev->reg_desc.mask32;
		tmp32 |= reg_csdev->reg_desc.val32 & reg_csdev->reg_desc.mask32;
	}
	*p_val32 = tmp32;
}

/*
 * Read the driver value into the reg if this is marked as one we want to save.
 */
static void cscfg_save_reg(struct cscfg_regval_csdev *reg_csdev)
{
	if (!(reg_csdev->reg_desc.type & CS_CFG_REG_TYPE_VAL_SAVE))
		return;
	if (reg_csdev->reg_desc.type & CS_CFG_REG_TYPE_VAL_64BIT)
		reg_csdev->reg_desc.val64 = *(u64 *)(reg_csdev->driver_regval);
	else
		reg_csdev->reg_desc.val32 = *(u32 *)(reg_csdev->driver_regval);
}

/*
 * Some register values are set from parameters. Initialise these registers
 * from the current parameter values.
 */
static void cscfg_init_reg_param(struct cscfg_feature_csdev *feat_csdev,
				 struct cscfg_regval_desc *reg_desc,
				 struct cscfg_regval_csdev *reg_csdev)
{
	struct cscfg_parameter_csdev *param_csdev;

	/* for param, load routines have validated the index */
	param_csdev = &feat_csdev->params_csdev[reg_desc->param_idx];
	param_csdev->reg_csdev = reg_csdev;
	param_csdev->val64 = reg_csdev->reg_desc.type & CS_CFG_REG_TYPE_VAL_64BIT;

	if (param_csdev->val64)
		reg_csdev->reg_desc.val64 = param_csdev->current_value;
	else
		reg_csdev->reg_desc.val32 = (u32)param_csdev->current_value;
}

/* set values into the driver locations referenced in cscfg_reg_csdev */
static int cscfg_set_on_enable(struct cscfg_feature_csdev *feat_csdev)
{
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(feat_csdev->drv_spinlock, flags);
	for (i = 0; i < feat_csdev->nr_regs; i++)
		cscfg_set_reg(&feat_csdev->regs_csdev[i]);
	raw_spin_unlock_irqrestore(feat_csdev->drv_spinlock, flags);
	dev_dbg(&feat_csdev->csdev->dev, "Feature %s: %s",
		feat_csdev->feat_desc->name, "set on enable");
	return 0;
}

/* copy back values from the driver locations referenced in cscfg_reg_csdev */
static void cscfg_save_on_disable(struct cscfg_feature_csdev *feat_csdev)
{
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(feat_csdev->drv_spinlock, flags);
	for (i = 0; i < feat_csdev->nr_regs; i++)
		cscfg_save_reg(&feat_csdev->regs_csdev[i]);
	raw_spin_unlock_irqrestore(feat_csdev->drv_spinlock, flags);
	dev_dbg(&feat_csdev->csdev->dev, "Feature %s: %s",
		feat_csdev->feat_desc->name, "save on disable");
}

/* default reset - restore default values */
void cscfg_reset_feat(struct cscfg_feature_csdev *feat_csdev)
{
	struct cscfg_regval_desc *reg_desc;
	struct cscfg_regval_csdev *reg_csdev;
	int i;

	/*
	 * set the default values for all parameters and regs from the
	 * relevant static descriptors.
	 */
	for (i = 0; i < feat_csdev->nr_params; i++)
		feat_csdev->params_csdev[i].current_value =
			feat_csdev->feat_desc->params_desc[i].value;

	for (i = 0; i < feat_csdev->nr_regs; i++) {
		reg_desc = &feat_csdev->feat_desc->regs_desc[i];
		reg_csdev = &feat_csdev->regs_csdev[i];
		reg_csdev->reg_desc.type = reg_desc->type;

		/* check if reg set from a parameter otherwise desc default */
		if (reg_desc->type & CS_CFG_REG_TYPE_VAL_PARAM)
			cscfg_init_reg_param(feat_csdev, reg_desc, reg_csdev);
		else
			/*
			 * for normal values the union between val64 & val32 + mask32
			 * allows us to init using the 64 bit value
			 */
			reg_csdev->reg_desc.val64 = reg_desc->val64;
	}
}

/*
 * For the selected presets, we set the register associated with the parameter, to
 * the value of the preset index associated with the parameter.
 */
static int cscfg_update_presets(struct cscfg_config_csdev *config_csdev, int preset)
{
	int i, j, val_idx = 0, nr_cfg_params;
	struct cscfg_parameter_csdev *param_csdev;
	struct cscfg_feature_csdev *feat_csdev;
	const struct cscfg_config_desc *config_desc = config_csdev->config_desc;
	const char *name;
	const u64 *preset_base;
	u64 val;

	/* preset in range 1 to nr_presets */
	if (preset < 1 || preset > config_desc->nr_presets)
		return -EINVAL;
	/*
	 * Go through the array of features, assigning preset values to
	 * feature parameters in the order they appear.
	 * There should be precisely the same number of preset values as the
	 * sum of number of parameters over all the features - but we will
	 * ensure there is no overrun.
	 */
	nr_cfg_params = config_desc->nr_total_params;
	preset_base = &config_desc->presets[(preset - 1) * nr_cfg_params];
	for (i = 0; i < config_csdev->nr_feat; i++) {
		feat_csdev = config_csdev->feats_csdev[i];
		if (!feat_csdev->nr_params)
			continue;

		for (j = 0; j < feat_csdev->nr_params; j++) {
			param_csdev = &feat_csdev->params_csdev[j];
			name = feat_csdev->feat_desc->params_desc[j].name;
			val = preset_base[val_idx++];
			if (param_csdev->val64) {
				dev_dbg(&config_csdev->csdev->dev,
					"set param %s (%lld)", name, val);
				param_csdev->reg_csdev->reg_desc.val64 = val;
			} else {
				param_csdev->reg_csdev->reg_desc.val32 = (u32)val;
				dev_dbg(&config_csdev->csdev->dev,
					"set param %s (%d)", name, (u32)val);
			}
		}

		/* exit early if all params filled */
		if (val_idx >= nr_cfg_params)
			break;
	}
	return 0;
}

/*
 * if we are not using a preset, then need to update the feature params
 * with current values. This sets the register associated with the parameter
 * with the current value of that parameter.
 */
static int cscfg_update_curr_params(struct cscfg_config_csdev *config_csdev)
{
	int i, j;
	struct cscfg_feature_csdev *feat_csdev;
	struct cscfg_parameter_csdev *param_csdev;
	const char *name;
	u64 val;

	for (i = 0; i < config_csdev->nr_feat; i++) {
		feat_csdev = config_csdev->feats_csdev[i];
		if (!feat_csdev->nr_params)
			continue;
		for (j = 0; j < feat_csdev->nr_params; j++) {
			param_csdev = &feat_csdev->params_csdev[j];
			name = feat_csdev->feat_desc->params_desc[j].name;
			val = param_csdev->current_value;
			if (param_csdev->val64) {
				dev_dbg(&config_csdev->csdev->dev,
					"set param %s (%lld)", name, val);
				param_csdev->reg_csdev->reg_desc.val64 = val;
			} else {
				param_csdev->reg_csdev->reg_desc.val32 = (u32)val;
				dev_dbg(&config_csdev->csdev->dev,
					"set param %s (%d)", name, (u32)val);
			}
		}
	}
	return 0;
}

/*
 * Configuration values will be programmed into the driver locations if enabling, or read
 * from relevant locations on disable.
 */
static int cscfg_prog_config(struct cscfg_config_csdev *config_csdev, bool enable)
{
	int i, err = 0;
	struct cscfg_feature_csdev *feat_csdev;
	struct coresight_device *csdev;

	for (i = 0; i < config_csdev->nr_feat; i++) {
		feat_csdev = config_csdev->feats_csdev[i];
		csdev = feat_csdev->csdev;
		dev_dbg(&csdev->dev, "cfg %s;  %s feature:%s", config_csdev->config_desc->name,
			enable ? "enable" : "disable", feat_csdev->feat_desc->name);

		if (enable)
			err = cscfg_set_on_enable(feat_csdev);
		else
			cscfg_save_on_disable(feat_csdev);

		if (err)
			break;
	}
	return err;
}

/*
 * Enable configuration for the device. Will result in the internal driver data
 * being updated ready for programming into the device.
 *
 * @config_csdev:	config_csdev to set.
 * @preset:		preset values to use - 0 for default.
 */
int cscfg_csdev_enable_config(struct cscfg_config_csdev *config_csdev, int preset)
{
	int err = 0;

	if (preset)
		err = cscfg_update_presets(config_csdev, preset);
	else
		err = cscfg_update_curr_params(config_csdev);
	if (!err)
		err = cscfg_prog_config(config_csdev, true);
	return err;
}

void cscfg_csdev_disable_config(struct cscfg_config_csdev *config_csdev)
{
	cscfg_prog_config(config_csdev, false);
}
