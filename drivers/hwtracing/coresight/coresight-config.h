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
 *
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
};

#endif /* _CORESIGHT_CORESIGHT_CONFIG_H */
