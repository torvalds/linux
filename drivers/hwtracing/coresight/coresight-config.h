/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#ifndef _CORESIGHT_CORESIGHT_CONFIG_H
#define _CORESIGHT_CORESIGHT_CONFIG_H

#include <linux/coresight.h>
#include <linux/types.h>

/* CoreSight Configuration Management - component and system wide configuration */

/*
 * Register type flags for register value descriptor:
 * describe how the value is interpreted, and handled.
 */
#define CS_CFG_REG_TYPE_STD		0x80	/* reg is standard reg */
#define CS_CFG_REG_TYPE_RESOURCE	0x40	/* reg is a resource */
#define CS_CFG_REG_TYPE_VAL_PARAM	0x08	/* reg value uses param */
#define CS_CFG_REG_TYPE_VAL_MASK	0x04	/* reg value bit masked */
#define CS_CFG_REG_TYPE_VAL_64BIT	0x02	/* reg value 64 bit */
#define CS_CFG_REG_TYPE_VAL_SAVE	0x01	/* reg value save on disable */

/*
 * flags defining what device class a feature will match to when processing a
 * system configuration - used by config data and devices.
 */
#define CS_CFG_MATCH_CLASS_SRC_ALL	0x0001	/* match any source */
#define CS_CFG_MATCH_CLASS_SRC_ETM4	0x0002	/* match any ETMv4 device */

/* flags defining device instance matching - used in config match desc data. */
#define CS_CFG_MATCH_INST_ANY		0x80000000 /* any instance of a class */

/*
 * Limit number of presets in a configuration
 * This is related to the number of bits (4) we use to select the preset on
 * the perf command line. Preset 0 is always none selected.
 * See PMU_FORMAT_ATTR(preset, "config:0-3") in coresight-etm-perf.c
 */
#define CS_CFG_CONFIG_PRESET_MAX 15

/**
 * Parameter descriptor for a device feature.
 *
 * @name:  Name of parameter.
 * @value: Initial or default value.
 */
struct cscfg_parameter_desc {
	const char *name;
	u64 value;
};

/**
 * Representation of register value and a descriptor of register usage.
 *
 * Used as a descriptor in the feature descriptors.
 * Used as a value in when in a feature loading into a csdev.
 *
 * Supports full 64 bit register value, or 32 bit value with optional mask
 * value.
 *
 * @type:	define register usage and interpretation.
 * @offset:	the address offset for register in the hardware device (per device specification).
 * @hw_info:	optional hardware device type specific information. (ETM / CTI specific etc)
 * @val64:	64 bit value.
 * @val32:	32 bit value.
 * @mask32:	32 bit mask when using 32 bit value to access device register - if mask type.
 * @param_idx:	parameter index value into parameter array if param type.
 */
struct cscfg_regval_desc {
	struct {
		u32 type:8;
		u32 offset:12;
		u32 hw_info:12;
	};
	union {
		u64 val64;
		struct {
			u32 val32;
			u32 mask32;
		};
		u32 param_idx;
	};
};

/**
 * Device feature descriptor - combination of registers and parameters to
 * program a device to implement a specific complex function.
 *
 * @name:	 feature name.
 * @description: brief description of the feature.
 * @item:	 List entry.
 * @match_flags: matching information if loading into a device
 * @nr_params:   number of parameters used.
 * @params_desc: array of parameters used.
 * @nr_regs:	 number of registers used.
 * @regs_desc:	 array of registers used.
 * @load_owner:	 handle to load owner for dynamic load and unload of features.
 * @fs_group:	 reference to configfs group for dynamic unload.
 */
struct cscfg_feature_desc {
	const char *name;
	const char *description;
	struct list_head item;
	u32 match_flags;
	int nr_params;
	struct cscfg_parameter_desc *params_desc;
	int nr_regs;
	struct cscfg_regval_desc *regs_desc;
	void *load_owner;
	struct config_group *fs_group;
};

/**
 * Configuration descriptor - describes selectable system configuration.
 *
 * A configuration describes device features in use, and may provide preset
 * values for the parameters in those features.
 *
 * A single set of presets is the sum of the parameters declared by
 * all the features in use - this value is @nr_total_params.
 *
 * @name:		name of the configuration - used for selection.
 * @description:	description of the purpose of the configuration.
 * @item:		list entry.
 * @nr_feat_refs:	Number of features used in this configuration.
 * @feat_ref_names:	references to features used in this configuration.
 * @nr_presets:		Number of sets of presets supplied by this configuration.
 * @nr_total_params:	Sum of all parameters declared by used features
 * @presets:		Array of preset values.
 * @event_ea:		Extended attribute for perf event value
 * @active_cnt:		ref count for activate on this configuration.
 * @load_owner:		handle to load owner for dynamic load and unload of configs.
 * @fs_group:		reference to configfs group for dynamic unload.
 * @available:		config can be activated - multi-stage load sets true on completion.
 */
struct cscfg_config_desc {
	const char *name;
	const char *description;
	struct list_head item;
	int nr_feat_refs;
	const char **feat_ref_names;
	int nr_presets;
	int nr_total_params;
	const u64 *presets; /* nr_presets * nr_total_params */
	struct dev_ext_attribute *event_ea;
	atomic_t active_cnt;
	void *load_owner;
	struct config_group *fs_group;
	bool available;
};

/**
 * config register instance - part of a loaded feature.
 *                            maps register values to csdev driver structures
 *
 * @reg_desc:		value to use when setting feature on device / store for
 *			readback of volatile values.
 * @driver_regval:	pointer to internal driver element used to set the value
 *			in hardware.
 */
struct cscfg_regval_csdev {
	struct cscfg_regval_desc reg_desc;
	void *driver_regval;
};

/**
 * config parameter instance - part of a loaded feature.
 *
 * @feat_csdev:		parent feature
 * @reg_csdev:		register value updated by this parameter.
 * @current_value:	current value of parameter - may be set by user via
 *			sysfs, or modified during device operation.
 * @val64:		true if 64 bit value
 */
struct cscfg_parameter_csdev {
	struct cscfg_feature_csdev *feat_csdev;
	struct cscfg_regval_csdev *reg_csdev;
	u64 current_value;
	bool val64;
};

/**
 * Feature instance loaded into a CoreSight device.
 *
 * When a feature is loaded into a specific device, then this structure holds
 * the connections between the register / parameter values used and the
 * internal data structures that are written when the feature is enabled.
 *
 * Since applying a feature modifies internal data structures in the device,
 * then we have a reference to the device spinlock to protect access to these
 * structures (@drv_spinlock).
 *
 * @feat_desc:		pointer to the static descriptor for this feature.
 * @csdev:		parent CoreSight device instance.
 * @node:		list entry into feature list for this device.
 * @drv_spinlock:	device spinlock for access to driver register data.
 * @nr_params:		number of parameters.
 * @params_csdev:	current parameter values on this device
 * @nr_regs:		number of registers to be programmed.
 * @regs_csdev:		Programming details for the registers
 */
struct cscfg_feature_csdev {
	const struct cscfg_feature_desc *feat_desc;
	struct coresight_device *csdev;
	struct list_head node;
	spinlock_t *drv_spinlock;
	int nr_params;
	struct cscfg_parameter_csdev *params_csdev;
	int nr_regs;
	struct cscfg_regval_csdev *regs_csdev;
};

/**
 * Configuration instance when loaded into a CoreSight device.
 *
 * The instance contains references to loaded features on this device that are
 * used by the configuration.
 *
 * @config_desc:reference to the descriptor for this configuration
 * @csdev:	parent coresight device for this configuration instance.
 * @enabled:	true if configuration is enabled on this device.
 * @node:	list entry within the coresight device
 * @nr_feat:	Number of features on this device that are used in the
 *		configuration.
 * @feats_csdev:references to the device features to enable.
 */
struct cscfg_config_csdev {
	const struct cscfg_config_desc *config_desc;
	struct coresight_device *csdev;
	bool enabled;
	struct list_head node;
	int nr_feat;
	struct cscfg_feature_csdev *feats_csdev[];
};

/**
 * Coresight device operations.
 *
 * Registered coresight devices provide these operations to manage feature
 * instances compatible with the device hardware and drivers
 *
 * @load_feat:	Pass a feature descriptor into the device and create the
 *		loaded feature instance (struct cscfg_feature_csdev).
 */
struct cscfg_csdev_feat_ops {
	int (*load_feat)(struct coresight_device *csdev,
			 struct cscfg_feature_csdev *feat_csdev);
};

/* coresight config helper functions*/

/* enable / disable config on a device - called with appropriate locks set.*/
int cscfg_csdev_enable_config(struct cscfg_config_csdev *config_csdev, int preset);
void cscfg_csdev_disable_config(struct cscfg_config_csdev *config_csdev);

/* reset a feature to default values */
void cscfg_reset_feat(struct cscfg_feature_csdev *feat_csdev);

#endif /* _CORESIGHT_CORESIGHT_CONFIG_H */
