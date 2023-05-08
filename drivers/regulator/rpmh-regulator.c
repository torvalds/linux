// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2021, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved. */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/ipc_logging.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/regulator/debug-regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/proxy-consumer.h>

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>

#include <dt-bindings/regulator/qcom,rpmh-regulator-levels.h>

/**
 * enum rpmh_regulator_type - supported RPMh accelerator types
 * %RPMH_REGULATOR_TYPE_VRM:	RPMh VRM accelerator which supports voting on
 *				enable, voltage, mode, and headroom voltage of
 *				LDO, SMPS, VS, and BOB type PMIC regulators.
 * %RPMH_REGULATOR_TYPE_ARC:	RPMh ARC accelerator which supports voting on
 *				the CPR managed voltage level of LDO and SMPS
 *				type PMIC regulators.
 * %RPMH_REGULATOR_TYPE_XOB:	RPMh XOB accelerator which supports voting on
 *				the enable state of PMIC regulators.
 */
enum rpmh_regulator_type {
	RPMH_REGULATOR_TYPE_VRM,
	RPMH_REGULATOR_TYPE_ARC,
	RPMH_REGULATOR_TYPE_XOB,
};

/**
 * enum rpmh_regulator_hw_type - supported PMIC regulator hardware types
 * This enum defines the specific regulator type along with its PMIC family.
 */
enum rpmh_regulator_hw_type {
	RPMH_REGULATOR_HW_TYPE_UNKNOWN,
	RPMH_REGULATOR_HW_TYPE_PMIC4_LDO,
	RPMH_REGULATOR_HW_TYPE_PMIC4_HFSMPS,
	RPMH_REGULATOR_HW_TYPE_PMIC4_FTSMPS,
	RPMH_REGULATOR_HW_TYPE_PMIC4_BOB,
	RPMH_REGULATOR_HW_TYPE_PMIC5_LDO,
	RPMH_REGULATOR_HW_TYPE_PMIC5_HFSMPS,
	RPMH_REGULATOR_HW_TYPE_PMIC5_FTSMPS,
	RPMH_REGULATOR_HW_TYPE_PMIC5_BOB,
	RPMH_REGULATOR_HW_TYPE_MAX,
};

/**
 * enum rpmh_regulator_reg_index - RPMh accelerator register indices
 * %RPMH_REGULATOR_REG_VRM_VOLTAGE:	VRM voltage voting register index
 * %RPMH_REGULATOR_REG_ARC_LEVEL:	ARC voltage level voting register index
 * %RPMH_REGULATOR_REG_VRM_ENABLE:	VRM enable voltage voting register index
 * %RPMH_REGULATOR_REG_ARC_PSEUDO_ENABLE: Place-holder for enable aggregation.
 *					ARC does not have a specific register
 *					for enable voting.  Instead, ARC level
 *					0 corresponds to "disabled" for a given
 *					ARC regulator resource if supported.
 * %RPMH_REGULATOR_REG_XOB_ENABLE:	XOB enable voting register index
 * %RPMH_REGULATOR_REG_ENABLE:		Common enable index used in callback
 *					functions for both ARC and VRM.
 * %RPMH_REGULATOR_REG_VRM_MODE:	VRM regulator mode voting register index
 * %RPMH_REGULATOR_REG_VRM_HEADROOM:	VRM headroom voltage voting register
 *					index
 * %RPMH_REGULATOR_REG_ARC_REAL_MAX:	Upper limit of real existent ARC
 *					register indices
 * %RPMH_REGULATOR_REG_ARC_MAX:		Exclusive upper limit of ARC register
 *					indices
 * %RPMH_REGULATOR_REG_XOB_MAX:		Exclusive upper limit of XOB register
 *					indices
 * %RPMH_REGULATOR_REG_VRM_MAX:		Exclusive upper limit of VRM register
 *					indices
 * %RPMH_REGULATOR_REG_MAX:		Combined exclusive upper limit of ARC
 *					and VRM register indices
 *
 * Register addresses are calculated as: base_addr + sizeof(u32) * reg_index
 */
enum rpmh_regulator_reg_index {
	RPMH_REGULATOR_REG_VRM_VOLTAGE		= 0,
	RPMH_REGULATOR_REG_ARC_LEVEL		= 0,
	RPMH_REGULATOR_REG_VRM_ENABLE		= 1,
	RPMH_REGULATOR_REG_ARC_PSEUDO_ENABLE	= RPMH_REGULATOR_REG_VRM_ENABLE,
	RPMH_REGULATOR_REG_XOB_ENABLE		= RPMH_REGULATOR_REG_VRM_ENABLE,
	RPMH_REGULATOR_REG_ENABLE		= RPMH_REGULATOR_REG_VRM_ENABLE,
	RPMH_REGULATOR_REG_VRM_MODE		= 2,
	RPMH_REGULATOR_REG_VRM_HEADROOM		= 3,
	RPMH_REGULATOR_REG_ARC_REAL_MAX		= 1,
	RPMH_REGULATOR_REG_ARC_MAX		= 2,
	RPMH_REGULATOR_REG_XOB_MAX		= 2,
	RPMH_REGULATOR_REG_VRM_MAX		= 4,
	RPMH_REGULATOR_REG_MAX			= 4,
};

/*
 * This is the number of bytes used for each command DB aux data entry of an
 * ARC resource.
 */
#define RPMH_ARC_LEVEL_SIZE		2

/*
 * This is the maximum number of voltage levels that may be defined for an ARC
 * resource.
 */
#define RPMH_ARC_MAX_LEVELS		16

#define RPMH_REGULATOR_LEVEL_OFF	0

/* Min and max limits of VRM resource request parameters */
#define RPMH_VRM_MIN_UV			0
#define RPMH_VRM_MAX_UV			8191000

#define RPMH_VRM_HEADROOM_MIN_UV	0
#define RPMH_VRM_HEADROOM_MAX_UV	511000

#define RPMH_VRM_MODE_MIN		0
#define RPMH_VRM_MODE_MAX		7

/* XOB voting registers are found in the VRM hardware module */
#define CMD_DB_HW_XOB			CMD_DB_HW_VRM

#define IPC_LOG_PAGES			10

#define rpmh_reg_dbg(fmt, ...) \
	do { \
		ipc_log_string(rpmh_reg_ipc_log, fmt, ##__VA_ARGS__); \
		pr_debug(fmt, ##__VA_ARGS__); \
	} while (0)

static void *rpmh_reg_ipc_log;

/**
 * struct rpmh_regulator_request - rpmh request data
 * @reg:			Array of RPMh accelerator register values
 * @valid:			Bitmask identifying which of the register values
 *				are valid/initialized
 */
struct rpmh_regulator_request {
	u32				reg[RPMH_REGULATOR_REG_MAX];
	u32				valid;
};

/**
 * struct rpmh_regulator_mode - RPMh VRM mode attributes
 * @pmic_mode:			Raw PMIC mode value written into VRM mode voting
 *				register (i.e. RPMH_REGULATOR_MODE_*)
 * @framework_mode:		Regulator framework mode value
 *				(i.e. REGULATOR_MODE_*)
 * @min_load_ua:		The minimum load current in microamps which
 *				would utilize this mode
 *
 * Software selects the lowest mode for which aggr_load_ua >= min_load_ua.
 */
struct rpmh_regulator_mode {
	u32				pmic_mode;
	u32				framework_mode;
	int				min_load_ua;
};

struct rpmh_vreg;

/**
 * struct rpmh_aggr_vreg - top level aggregated rpmh regulator resource data
 *		structure
 * @dev:			Device pointer to the rpmh aggregated regulator
 *				device
 * @resource_name:		Name of rpmh regulator resource which is mapped
 *				to an RPMh accelerator address via command DB.
 *				This name must match to one that is defined by
 *				the bootloader.
 * @addr:			Base address of the regulator resource within
 *				an RPMh accelerator
 * @lock:			Mutex lock used for locking between regulators
 *				common to a single aggregated resource
 * @regulator_type:		RPMh accelerator type for this regulator
 *				resource
 * @regulator_hw_type:		The regulator hardware type (e.g. LDO or SMPS)
 *				along with PMIC family (i.e. PMIC4 or PMIC5)
 * @level:			Mapping from ARC resource specific voltage
 *				levels (0 to RPMH_ARC_MAX_LEVELS - 1) to common
 *				consumer voltage levels (i.e.
 *				RPMH_REGULATOR_LEVEL_*).  These values are read
 *				out of the AUX data found in command DB for a
 *				given ARC resource.
 * @level_count:		The number of valid entries in the level array
 * @always_wait_for_ack:	Boolean flag indicating if a request must always
 *				wait for an ACK from RPMh before continuing even
 *				if it corresponds to a strictly lower power
 *				state (e.g. enabled --> disabled).
 * @next_wait_for_ack:		Boolean flag indicating that the next request
 *				sent must wait for an ACK.  This is used to
 *				ensure that the driver waits for the voltage to
 *				slew down in the case that the requested max_uV
 *				value is lower than the last requested voltage.
 * @sleep_request_sent:		Boolean flag indicating that a sleep set request
 *				has been sent at some point due to it diverging
 *				from the active set request.  After that point,
 *				the sleep set requests must always be sent for
 *				a given resource.
 * @vreg:			Array of rpmh regulator structs representing the
 *				individual regulators sharing the aggregated
 *				regulator resource.
 * @vreg_count:			The number of entries in the vreg array.
 * @mode:			An array of modes supported by an RPMh VRM
 *				regulator resource.
 * @mode_count:			The number of entries in the mode array.
 * @disable_pmic_mode:		PMIC mode optionally set when aggregated regulator resource
 *				gets disabled, overriding existing aggregated mode.
 * @aggr_req_active:		Aggregated active set RPMh accelerator register
 *				request
 * @aggr_req_sleep:		Aggregated sleep set RPMh accelerator register
 *				request
 */
struct rpmh_aggr_vreg {
	struct device			*dev;
	const char			*resource_name;
	u32				addr;
	struct mutex			lock;
	enum rpmh_regulator_type	regulator_type;
	enum rpmh_regulator_hw_type	regulator_hw_type;
	u32				level[RPMH_ARC_MAX_LEVELS];
	int				level_count;
	bool				always_wait_for_ack;
	bool				next_wait_for_ack;
	bool				sleep_request_sent;
	struct rpmh_vreg		*vreg;
	int				vreg_count;
	struct rpmh_regulator_mode	*mode;
	int				mode_count;
	int				disable_pmic_mode;
	struct rpmh_regulator_request	aggr_req_active;
	struct rpmh_regulator_request	aggr_req_sleep;
};

/**
 * struct rpmh_vreg - individual rpmh regulator data structure encapsulating a
 *		regulator framework regulator device and its corresponding
 *		rpmh request
 * @of_node:			Device node pointer for the individual rpmh
 *				regulator
 * @name:			Name of the regulator
 * @rdesc:			Regulator descriptor
 * @rdev:			Regulator device pointer returned by
 *				devm_regulator_register()
 * @aggr_vreg:			Pointer to the aggregated rpmh regulator
 *				resource
 * @set_active:			Boolean flag indicating that requests made by
 *				this regulator should take affect in the active
 *				set
 * @set_sleep:			Boolean flag indicating that requests made by
 *				this regulator should take affect in the sleep
 *				set
 * @req:			RPMh accelerator register request
 * @mode_index:			RPMh VRM regulator mode selected by index into
 *				aggr_vreg->mode
 */
struct rpmh_vreg {
	struct device_node		*of_node;
	struct regulator_desc		rdesc;
	struct regulator_dev		*rdev;
	struct rpmh_aggr_vreg		*aggr_vreg;
	bool				set_active;
	bool				set_sleep;
	struct rpmh_regulator_request	req;
	int				mode_index;
};

#define RPMH_REGULATOR_MODE_COUNT		5

#define RPMH_REGULATOR_MODE_PMIC4_LDO_RM	4
#define RPMH_REGULATOR_MODE_PMIC4_LDO_LPM	5
#define RPMH_REGULATOR_MODE_PMIC4_LDO_HPM	7

#define RPMH_REGULATOR_MODE_PMIC4_SMPS_RM	4
#define RPMH_REGULATOR_MODE_PMIC4_SMPS_PFM	5
#define RPMH_REGULATOR_MODE_PMIC4_SMPS_AUTO	6
#define RPMH_REGULATOR_MODE_PMIC4_SMPS_PWM	7

#define RPMH_REGULATOR_MODE_PMIC4_BOB_PASS	0
#define RPMH_REGULATOR_MODE_PMIC4_BOB_PFM	1
#define RPMH_REGULATOR_MODE_PMIC4_BOB_AUTO	2
#define RPMH_REGULATOR_MODE_PMIC4_BOB_PWM	3

#define RPMH_REGULATOR_MODE_PMIC5_LDO_RM	3
#define RPMH_REGULATOR_MODE_PMIC5_LDO_LPM	4
#define RPMH_REGULATOR_MODE_PMIC5_LDO_HPM	7

#define RPMH_REGULATOR_MODE_PMIC5_HFSMPS_RM	3
#define RPMH_REGULATOR_MODE_PMIC5_HFSMPS_PFM	4
#define RPMH_REGULATOR_MODE_PMIC5_HFSMPS_AUTO	6
#define RPMH_REGULATOR_MODE_PMIC5_HFSMPS_PWM	7

#define RPMH_REGULATOR_MODE_PMIC5_FTSMPS_RM	3
#define RPMH_REGULATOR_MODE_PMIC5_FTSMPS_PWM	7

#define RPMH_REGULATOR_MODE_PMIC5_BOB_PASS	2
#define RPMH_REGULATOR_MODE_PMIC5_BOB_PFM	4
#define RPMH_REGULATOR_MODE_PMIC5_BOB_AUTO	6
#define RPMH_REGULATOR_MODE_PMIC5_BOB_PWM	7

/*
 * Mappings from RPMh generic modes to VRM accelerator modes and regulator
 * framework modes for each regulator type.
 */
static const struct rpmh_regulator_mode
rpmh_regulator_mode_map_pmic4_ldo[RPMH_REGULATOR_MODE_COUNT] = {
	[RPMH_REGULATOR_MODE_RET] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_LDO_RM,
		.framework_mode = REGULATOR_MODE_STANDBY,
	},
	[RPMH_REGULATOR_MODE_LPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_LDO_LPM,
		.framework_mode = REGULATOR_MODE_IDLE,
	},
	[RPMH_REGULATOR_MODE_HPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_LDO_HPM,
		.framework_mode = REGULATOR_MODE_FAST,
	},
};

static const struct rpmh_regulator_mode
rpmh_regulator_mode_map_pmic4_smps[RPMH_REGULATOR_MODE_COUNT] = {
	[RPMH_REGULATOR_MODE_RET] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_SMPS_RM,
		.framework_mode = REGULATOR_MODE_STANDBY,
	},
	[RPMH_REGULATOR_MODE_LPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_SMPS_PFM,
		.framework_mode = REGULATOR_MODE_IDLE,
	},
	[RPMH_REGULATOR_MODE_AUTO] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_SMPS_AUTO,
		.framework_mode = REGULATOR_MODE_NORMAL,
	},
	[RPMH_REGULATOR_MODE_HPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_SMPS_PWM,
		.framework_mode = REGULATOR_MODE_FAST,
	},
};

static const struct rpmh_regulator_mode
rpmh_regulator_mode_map_pmic4_bob[RPMH_REGULATOR_MODE_COUNT] = {
	[RPMH_REGULATOR_MODE_PASS] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_BOB_PASS,
		.framework_mode = REGULATOR_MODE_STANDBY,
	},
	[RPMH_REGULATOR_MODE_LPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_BOB_PFM,
		.framework_mode = REGULATOR_MODE_IDLE,
	},
	[RPMH_REGULATOR_MODE_AUTO] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_BOB_AUTO,
		.framework_mode = REGULATOR_MODE_NORMAL,
	},
	[RPMH_REGULATOR_MODE_HPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC4_BOB_PWM,
		.framework_mode = REGULATOR_MODE_FAST,
	},
};

static const struct rpmh_regulator_mode
rpmh_regulator_mode_map_pmic5_ldo[RPMH_REGULATOR_MODE_COUNT] = {
	[RPMH_REGULATOR_MODE_RET] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_LDO_RM,
		.framework_mode = REGULATOR_MODE_STANDBY,
	},
	[RPMH_REGULATOR_MODE_LPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_LDO_LPM,
		.framework_mode = REGULATOR_MODE_IDLE,
	},
	[RPMH_REGULATOR_MODE_HPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_LDO_HPM,
		.framework_mode = REGULATOR_MODE_FAST,
	},
};

static const struct rpmh_regulator_mode
rpmh_regulator_mode_map_pmic5_hfsmps[RPMH_REGULATOR_MODE_COUNT] = {
	[RPMH_REGULATOR_MODE_RET] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_HFSMPS_RM,
		.framework_mode = REGULATOR_MODE_STANDBY,
	},
	[RPMH_REGULATOR_MODE_LPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_HFSMPS_PFM,
		.framework_mode = REGULATOR_MODE_IDLE,
	},
	[RPMH_REGULATOR_MODE_AUTO] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_HFSMPS_AUTO,
		.framework_mode = REGULATOR_MODE_NORMAL,
	},
	[RPMH_REGULATOR_MODE_HPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_HFSMPS_PWM,
		.framework_mode = REGULATOR_MODE_FAST,
	},
};

static const struct rpmh_regulator_mode
rpmh_regulator_mode_map_pmic5_ftsmps[RPMH_REGULATOR_MODE_COUNT] = {
	[RPMH_REGULATOR_MODE_RET] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_FTSMPS_RM,
		.framework_mode = REGULATOR_MODE_STANDBY,
	},
	[RPMH_REGULATOR_MODE_HPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_FTSMPS_PWM,
		.framework_mode = REGULATOR_MODE_FAST,
	},
};

static const struct rpmh_regulator_mode
rpmh_regulator_mode_map_pmic5_bob[RPMH_REGULATOR_MODE_COUNT] = {
	[RPMH_REGULATOR_MODE_PASS] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_BOB_PASS,
		.framework_mode = REGULATOR_MODE_STANDBY,
	},
	[RPMH_REGULATOR_MODE_LPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_BOB_PFM,
		.framework_mode = REGULATOR_MODE_IDLE,
	},
	[RPMH_REGULATOR_MODE_AUTO] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_BOB_AUTO,
		.framework_mode = REGULATOR_MODE_NORMAL,
	},
	[RPMH_REGULATOR_MODE_HPM] = {
		.pmic_mode = RPMH_REGULATOR_MODE_PMIC5_BOB_PWM,
		.framework_mode = REGULATOR_MODE_FAST,
	},
};

static const struct rpmh_regulator_mode * const
rpmh_regulator_mode_map[RPMH_REGULATOR_HW_TYPE_MAX] = {
	[RPMH_REGULATOR_HW_TYPE_PMIC4_LDO]
		= rpmh_regulator_mode_map_pmic4_ldo,
	[RPMH_REGULATOR_HW_TYPE_PMIC4_HFSMPS]
		= rpmh_regulator_mode_map_pmic4_smps,
	[RPMH_REGULATOR_HW_TYPE_PMIC4_FTSMPS]
		= rpmh_regulator_mode_map_pmic4_smps,
	[RPMH_REGULATOR_HW_TYPE_PMIC4_BOB]
		= rpmh_regulator_mode_map_pmic4_bob,
	[RPMH_REGULATOR_HW_TYPE_PMIC5_LDO]
		= rpmh_regulator_mode_map_pmic5_ldo,
	[RPMH_REGULATOR_HW_TYPE_PMIC5_HFSMPS]
		= rpmh_regulator_mode_map_pmic5_hfsmps,
	[RPMH_REGULATOR_HW_TYPE_PMIC5_FTSMPS]
		= rpmh_regulator_mode_map_pmic5_ftsmps,
	[RPMH_REGULATOR_HW_TYPE_PMIC5_BOB]
		= rpmh_regulator_mode_map_pmic5_bob,
};

/*
 * This voltage in uV is returned by get_voltage functions when there is no way
 * to determine the current voltage level.  It is needed because the regulator
 * framework treats a 0 uV voltage as an error.
 */
#define VOLTAGE_UNKNOWN 1

#define vreg_err(vreg, message, ...) \
	pr_err("%s: " message, (vreg)->rdesc.name, ##__VA_ARGS__)
#define vreg_info(vreg, message, ...) \
	pr_info("%s: " message, (vreg)->rdesc.name, ##__VA_ARGS__)
#define vreg_debug(vreg, message, ...) \
	pr_debug("%s: " message, (vreg)->rdesc.name, ##__VA_ARGS__)

#define aggr_vreg_err(aggr_vreg, message, ...) \
	pr_err("%s: " message, (aggr_vreg)->resource_name, ##__VA_ARGS__)
#define aggr_vreg_info(aggr_vreg, message, ...) \
	pr_info("%s: " message, (aggr_vreg)->resource_name, ##__VA_ARGS__)
#define aggr_vreg_debug(aggr_vreg, message, ...) \
	pr_debug("%s: " message, (aggr_vreg)->resource_name, ##__VA_ARGS__)

#define DEBUG_PRINT_BUFFER_SIZE 256
static const char *const rpmh_regulator_state_names[] = {
	[RPMH_SLEEP_STATE]		= "sleep ",
	[RPMH_WAKE_ONLY_STATE]		= "wake  ",
	[RPMH_ACTIVE_ONLY_STATE]	= "active",
};

static const char *const rpmh_regulator_vrm_param_names[] = {
	[RPMH_REGULATOR_REG_VRM_VOLTAGE]	= "mv",
	[RPMH_REGULATOR_REG_VRM_ENABLE]		= "en",
	[RPMH_REGULATOR_REG_VRM_MODE]		= "mode",
	[RPMH_REGULATOR_REG_VRM_HEADROOM]	= "hr_mv",
};

static const char *const rpmh_regulator_arc_param_names[] = {
	[RPMH_REGULATOR_REG_ARC_LEVEL]		= "hlvl",
};

static const char *const rpmh_regulator_xob_param_names[] = {
	[RPMH_REGULATOR_REG_XOB_ENABLE]		= "en",
};

static const int max_reg_index_map[] = {
	[RPMH_REGULATOR_TYPE_VRM] = RPMH_REGULATOR_REG_VRM_MAX,
	[RPMH_REGULATOR_TYPE_ARC] = RPMH_REGULATOR_REG_ARC_MAX,
	[RPMH_REGULATOR_TYPE_XOB] = RPMH_REGULATOR_REG_XOB_MAX,
};

/**
 * rpmh_regulator_get_max_reg_index() - return the number of registers
 *		associated with the rpmh resource
 * @aggr_vreg:		Pointer to the aggregated rpmh regulator resource
 *
 * Return: max number of registers for the aggr_vreg rpmh resource
 */
static int rpmh_regulator_get_max_reg_index(struct rpmh_aggr_vreg *aggr_vreg)
{
	if (aggr_vreg->regulator_type >= ARRAY_SIZE(max_reg_index_map))
		return -EINVAL;
	else
		return max_reg_index_map[aggr_vreg->regulator_type];
}

/**
 * rpmh_regulator_req() - print the rpmh regulator request to the kernel log
 * @vreg:		Pointer to the RPMh regulator
 * @current_req:	Pointer to the new request
 * @prev_req:		Pointer to the last request
 * @sent_mask:		Bitmask which specifies the parameters sent in this
 *			request
 * @state:		The rpmh state that the request was sent for
 *
 * Return: none
 */
static void rpmh_regulator_req(struct rpmh_vreg *vreg,
		struct rpmh_regulator_request *current_req,
		struct rpmh_regulator_request *prev_req,
		u32 sent_mask,
		enum rpmh_state state)
{
	struct rpmh_aggr_vreg *aggr_vreg = vreg->aggr_vreg;
	char buf[DEBUG_PRINT_BUFFER_SIZE];
	size_t buflen = DEBUG_PRINT_BUFFER_SIZE;
	const char *const *param_name;
	int i, max_reg_index;
	int pos = 0;
	u32 valid;
	bool first;

	switch (aggr_vreg->regulator_type) {
	case RPMH_REGULATOR_TYPE_VRM:
		max_reg_index = RPMH_REGULATOR_REG_VRM_MAX;
		param_name = rpmh_regulator_vrm_param_names;
		break;
	case RPMH_REGULATOR_TYPE_ARC:
		max_reg_index = RPMH_REGULATOR_REG_ARC_REAL_MAX;
		param_name = rpmh_regulator_arc_param_names;
		break;
	case RPMH_REGULATOR_TYPE_XOB:
		max_reg_index = RPMH_REGULATOR_REG_XOB_MAX;
		param_name = rpmh_regulator_xob_param_names;
		break;
	default:
		return;
	}

	pos += scnprintf(buf + pos, buflen - pos,
			"%s (%s), addr=0x%05X: s=%s; sent: ",
			aggr_vreg->resource_name, vreg->rdesc.name,
			aggr_vreg->addr, rpmh_regulator_state_names[state]);

	valid = sent_mask;
	first = true;
	for (i = 0; i < max_reg_index; i++) {
		if (valid & BIT(i)) {
			pos += scnprintf(buf + pos, buflen - pos, "%s%s=%u",
					(first ? "" : ", "), param_name[i],
					current_req->reg[i]);
			first = false;
			if (aggr_vreg->regulator_type
				== RPMH_REGULATOR_TYPE_ARC
			    && i == RPMH_REGULATOR_REG_ARC_LEVEL)
				pos += scnprintf(buf + pos, buflen - pos,
					" (vlvl=%u)",
					aggr_vreg->level[current_req->reg[i]]);
		}
	}

	valid = prev_req->valid & ~sent_mask;

	if (valid)
		pos += scnprintf(buf + pos, buflen - pos, "; prev: ");
	first = true;
	for (i = 0; i < max_reg_index; i++) {
		if (valid & BIT(i)) {
			pos += scnprintf(buf + pos, buflen - pos, "%s%s=%u",
					(first ? "" : ", "), param_name[i],
					current_req->reg[i]);
			first = false;
			if (aggr_vreg->regulator_type
				== RPMH_REGULATOR_TYPE_ARC
			    && i == RPMH_REGULATOR_REG_ARC_LEVEL)
				pos += scnprintf(buf + pos, buflen - pos,
					" (vlvl=%u)",
					aggr_vreg->level[current_req->reg[i]]);
		}
	}

	rpmh_reg_dbg("%s\n", buf);
}

/**
 * rpmh_regulator_handle_arc_enable() - handle masking of the voltage level
 *		request based on the pseudo-enable value
 * @aggr_vreg:		Pointer to the aggregated rpmh regulator resource
 * @req			Pointer to the newly aggregated request
 *
 * Return: none
 */
static void rpmh_regulator_handle_arc_enable(struct rpmh_aggr_vreg *aggr_vreg,
					     struct rpmh_regulator_request *req)
{
	if (aggr_vreg->regulator_type != RPMH_REGULATOR_TYPE_ARC)
		return;

	/*
	 * Mask the voltage level if "off" level is supported and the regulator
	 * has not been enabled.
	 */
	if (aggr_vreg->level[0] == RPMH_REGULATOR_LEVEL_OFF) {
		if (req->valid & BIT(RPMH_REGULATOR_REG_ARC_PSEUDO_ENABLE)) {
			if (!req->reg[RPMH_REGULATOR_REG_ARC_PSEUDO_ENABLE])
				req->reg[RPMH_REGULATOR_REG_ARC_LEVEL] = 0;
		} else {
			/* Invalidate voltage level if enable is invalid. */
			req->valid &= ~BIT(RPMH_REGULATOR_REG_ARC_LEVEL);
		}
	}

	/*
	 * Mark the pseudo enable bit as invalid so that it is not accidentally
	 * included in an RPMh command.
	 */
	req->valid &= ~BIT(RPMH_REGULATOR_REG_ARC_PSEUDO_ENABLE);
}

static void rpmh_regulator_handle_disable_mode(struct rpmh_aggr_vreg *aggr_vreg,
					     struct rpmh_regulator_request *req)
{
	if (aggr_vreg->regulator_type != RPMH_REGULATOR_TYPE_VRM
	   || aggr_vreg->disable_pmic_mode <= 0)
		return;

	if ((req->valid & BIT(RPMH_REGULATOR_REG_ENABLE))
	   && !req->reg[RPMH_REGULATOR_REG_ENABLE]) {
		req->reg[RPMH_REGULATOR_REG_VRM_MODE] = aggr_vreg->disable_pmic_mode;
		req->valid |= RPMH_REGULATOR_REG_VRM_MODE;
	}
}

/**
 * rpmh_regulator_aggregate_requests() - aggregate the requests from all
 *		regulators associated with an RPMh resource
 * @aggr_vreg:		Pointer to the aggregated rpmh regulator resource
 * @req_active:		Pointer to active set request output
 * @req_sleep:		Pointer to sleep set request output
 *
 * This function aggregates the requests from the different regulators
 * associated with the aggr_vreg resource independently in both the active set
 * and sleep set.  The aggregated results are stored in req_active and
 * req_sleep.
 *
 * Return: none
 */
static void rpmh_regulator_aggregate_requests(struct rpmh_aggr_vreg *aggr_vreg,
				struct rpmh_regulator_request *req_active,
				struct rpmh_regulator_request *req_sleep)
{
	int i, j, max_reg_index;

	max_reg_index = rpmh_regulator_get_max_reg_index(aggr_vreg);
	/*
	 * Perform max aggregration of each register value across all regulators
	 * which use this RPMh resource.
	 */
	for (i = 0; i < aggr_vreg->vreg_count; i++) {
		if (aggr_vreg->vreg[i].set_active) {
			for (j = 0; j < max_reg_index; j++)
				req_active->reg[j] = max(req_active->reg[j],
						aggr_vreg->vreg[i].req.reg[j]);
			req_active->valid |= aggr_vreg->vreg[i].req.valid;
		}
		if (aggr_vreg->vreg[i].set_sleep) {
			for (j = 0; j < max_reg_index; j++)
				req_sleep->reg[j] = max(req_sleep->reg[j],
						aggr_vreg->vreg[i].req.reg[j]);
			req_sleep->valid |= aggr_vreg->vreg[i].req.valid;
		}
	}

	rpmh_regulator_handle_arc_enable(aggr_vreg, req_active);
	rpmh_regulator_handle_arc_enable(aggr_vreg, req_sleep);
	rpmh_regulator_handle_disable_mode(aggr_vreg, req_active);
	rpmh_regulator_handle_disable_mode(aggr_vreg, req_sleep);
}

static void swap_cmds(struct tcs_cmd *cmd1, struct tcs_cmd *cmd2)
{
	struct tcs_cmd cmd_temp;

	if (cmd1 == cmd2)
		return;

	cmd_temp = *cmd1;
	*cmd1 = *cmd2;
	*cmd2 = cmd_temp;
}

/**
 * rpmh_regulator_reorder_cmds() - reorder tcs commands to ensure safe regulator
 *		state transitions
 * @aggr_vreg:		Pointer to the aggregated rpmh regulator resource
 * @cmd:		TCS command array
 * @len:		Number of elements in 'cmd' array
 *
 * LDO regulators can accidentally trigger over-current protection (OCP) when
 * RPMh regulator requests are processed if the commands in the request are in
 * an order that temporarily places the LDO in an incorrect state.  For example,
 * if an LDO is disabled with mode=LPM, then executing the request
 * [en=ON, mode=HPM] can trigger OCP after the first command is executed since
 * it will result in the overall state: en=ON, mode=LPM.  This issue can be
 * avoided by reordering the commands: [mode=HPM, en=ON].
 *
 * This function reorders the request command sequence to avoid invalid
 * transient LDO states.
 *
 * Return: none
 */
static void rpmh_regulator_reorder_cmds(struct rpmh_aggr_vreg *aggr_vreg,
					struct tcs_cmd *cmd, int len)
{
	enum rpmh_regulator_reg_index reg_index;
	int i;

	if (len == 1 || aggr_vreg->regulator_type != RPMH_REGULATOR_TYPE_VRM)
		return;

	for (i = 0; i < len; i++) {
		reg_index = (cmd[i].addr - aggr_vreg->addr) >> 2;

		if (reg_index == RPMH_REGULATOR_REG_VRM_ENABLE) {
			if (cmd[i].data) {
				/* Move enable command to end */
				swap_cmds(&cmd[i], &cmd[len - 1]);
			} else {
				/* Move disable command to start */
				swap_cmds(&cmd[i], &cmd[0]);
			}
			break;
		}
	}
}

/**
 * rpmh_regulator_send_aggregate_requests() - aggregate the requests from all
 *		regulators associated with an RPMh resource and send the request
 *		to RPMh
 * @vreg:		Pointer to the RPMh regulator
 *
 * This function aggregates the requests from the different regulators
 * associated with the aggr_vreg resource independently in both the active set
 * and sleep set.  The requests are only sent for the sleep set if they differ,
 * or have differed in the past, from those of the active set.
 *
 * Return: 0 on success, errno on failure
 */
static int
rpmh_regulator_send_aggregate_requests(struct rpmh_vreg *vreg)
{
	struct rpmh_aggr_vreg *aggr_vreg = vreg->aggr_vreg;
	struct rpmh_regulator_request req_active = { {0} };
	struct rpmh_regulator_request req_sleep = { {0} };
	struct tcs_cmd cmd[RPMH_REGULATOR_REG_MAX] = { {0} };
	bool sleep_set_differs = aggr_vreg->sleep_request_sent;
	bool wait_for_ack = aggr_vreg->always_wait_for_ack
				|| aggr_vreg->next_wait_for_ack;
	bool resend_active = false;
	int i, j, max_reg_index, rc;
	enum rpmh_state state;
	u32 sent_mask;

	max_reg_index = rpmh_regulator_get_max_reg_index(aggr_vreg);

	rpmh_regulator_aggregate_requests(aggr_vreg, &req_active, &req_sleep);

	/*
	 * Check if the aggregated sleep set parameter values differ from the
	 * aggregated active set parameter values.
	 */
	if (!aggr_vreg->sleep_request_sent) {
		for (i = 0; i < max_reg_index; i++) {
			if ((req_active.reg[i] != req_sleep.reg[i])
			    && (req_sleep.valid & BIT(i))) {
				sleep_set_differs = true;
				/*
				 * Resend full active set request so that
				 * all parameters are specified in the wake-only
				 * state request.
				 */
				resend_active = true;
				break;
			}
		}
	}

	if (sleep_set_differs) {
		/*
		 * Generate an rpmh command consisting of only those registers
		 * which have new values or which have never been touched before
		 * (i.e. those that were previously not valid).
		 */
		sent_mask = 0;
		for (i = 0, j = 0; i < max_reg_index; i++) {
			if ((req_sleep.valid & BIT(i))
			    && (!(aggr_vreg->aggr_req_sleep.valid & BIT(i))
				|| aggr_vreg->aggr_req_sleep.reg[i]
					!= req_sleep.reg[i])) {
				cmd[j].addr = aggr_vreg->addr + i * 4;
				cmd[j].data = req_sleep.reg[i];
				cmd[j].wait = true;
				j++;
				sent_mask |= BIT(i);
			}
		}

		/* Send the rpmh command if any register values differ. */
		if (j > 0) {
			rpmh_regulator_reorder_cmds(aggr_vreg, cmd, j);

			rc = rpmh_write_async(aggr_vreg->dev,
					RPMH_SLEEP_STATE, cmd, j);
			if (rc) {
				aggr_vreg_err(aggr_vreg, "sleep state rpmh_write_async() failed, rc=%d\n",
					rc);
				return rc;
			}
			rpmh_regulator_req(vreg, &req_sleep,
				&aggr_vreg->aggr_req_sleep,
				sent_mask,
				RPMH_SLEEP_STATE);
			aggr_vreg->sleep_request_sent = true;
			aggr_vreg->aggr_req_sleep = req_sleep;
		}
	}

	/*
	 * Generate an rpmh command consisting of only those registers
	 * which have new values or which have never been touched before
	 * (i.e. those that were previously not valid).
	 */
	sent_mask = 0;
	for (i = 0, j = 0; i < max_reg_index; i++) {
		if ((req_active.valid & BIT(i))
		    && (!(aggr_vreg->aggr_req_active.valid & BIT(i))
			|| aggr_vreg->aggr_req_active.reg[i]
				!= req_active.reg[i] || resend_active)) {
			cmd[j].addr = aggr_vreg->addr + i * 4;
			cmd[j].data = req_active.reg[i];
			cmd[j].wait = true;
			j++;
			sent_mask |= BIT(i);

			/*
			 * Must wait for ACK from RPMh if power state is
			 * increasing
			 */
			if (req_active.reg[i]
			    > aggr_vreg->aggr_req_active.reg[i])
				wait_for_ack = true;
		}
	}

	/* Send the rpmh command if any register values differ. */
	if (j > 0) {
		rpmh_regulator_reorder_cmds(aggr_vreg, cmd, j);

		if (sleep_set_differs) {
			state = RPMH_WAKE_ONLY_STATE;
			rc = rpmh_write_async(aggr_vreg->dev, state, cmd, j);
			if (rc) {
				aggr_vreg_err(aggr_vreg, "%s state rpmh_write_async() failed, rc=%d\n",
					rpmh_regulator_state_names[state], rc);
				return rc;
			}
			rpmh_regulator_req(vreg, &req_active,
				&aggr_vreg->aggr_req_active, sent_mask, state);
		}

		state = RPMH_ACTIVE_ONLY_STATE;
		if (wait_for_ack)
			rc = rpmh_write(aggr_vreg->dev, state, cmd, j);
		else
			rc = rpmh_write_async(aggr_vreg->dev, state,
						cmd, j);
		if (rc) {
			aggr_vreg_err(aggr_vreg, "%s state rpmh_write() failed, rc=%d\n",
				rpmh_regulator_state_names[state], rc);
			return rc;
		}
		rpmh_regulator_req(vreg, &req_active,
				&aggr_vreg->aggr_req_active, sent_mask, state);

		aggr_vreg->aggr_req_active = req_active;
		aggr_vreg->next_wait_for_ack = false;
	}

	return 0;
}

/**
 * rpmh_regulator_set_reg() - set a register value within the request for an
 *		RPMh regulator and return the previous value
 * @vreg:		Pointer to the RPMh regulator
 * @reg_index:		Index of the register value to update
 * @value:		New register value to set
 *
 * Return: old register value
 */
static u32 rpmh_regulator_set_reg(struct rpmh_vreg *vreg, int reg_index,
				u32 value)
{
	u32 old_value;

	old_value = vreg->req.reg[reg_index];
	vreg->req.reg[reg_index] = value;
	vreg->req.valid |= BIT(reg_index);

	return old_value;
}

/**
 * rpmh_regulator_check_param_max() - sets if the next request must wait for
 *		an ACK based on the previously sent reg[index] value and the new
 *		max value
 * @aggr_vreg:		Pointer to the aggregated rpmh regulator resource
 * @index:		Register index
 * @new_max:		Newly requested maximum allowed value for the parameter
 *
 * This function is used to handle the case when a consumer makes a new
 * (min_uv, max_uv) range request in which the new max_uv is lower than the
 * previously requested min_uv.  In this case, the driver must wait for an ACK
 * from RPMh to ensure that the voltage has completed reducing to the new min_uv
 * value since the consumer cannot operate at the old min_uv value.
 *
 * Return: none
 */
static void rpmh_regulator_check_param_max(struct rpmh_aggr_vreg *aggr_vreg,
					int index, u32 new_max)
{
	if ((aggr_vreg->aggr_req_active.valid & BIT(index))
	    && aggr_vreg->aggr_req_active.reg[index] > new_max)
		aggr_vreg->next_wait_for_ack = true;
}

/**
 * rpmh_regulator_is_enabled() - return the enable state of the RPMh
 *		regulator
 * @rdev:		Regulator device pointer for the rpmh-regulator
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each rpmh-regulator device.
 *
 * Note that for ARC resources, this value is effectively a flag indicating if
 * the requested voltage level is masked or unmasked since "disabled" = voltage
 * level 0 (if supported).
 *
 * Return: true if regulator is enabled, false if regulator is disabled
 */
static int rpmh_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);

	return !!vreg->req.reg[RPMH_REGULATOR_REG_ENABLE];
}

/**
 * rpmh_regulator_enable() - enable the RPMh regulator
 * @rdev:		Regulator device pointer for the rpmh-regulator
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each rpmh-regulator device.
 *
 * Note that for ARC devices the enable state is handled via the voltage level
 * parameter.  Therefore, this enable value effectively masks or unmasks the
 * enabled voltage level.
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_enable(struct regulator_dev *rdev)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	u32 prev_enable;
	int rc;

	mutex_lock(&vreg->aggr_vreg->lock);

	prev_enable
	       = rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_ENABLE, 1);

	rc = rpmh_regulator_send_aggregate_requests(vreg);
	if (rc) {
		vreg_err(vreg, "enable failed, rc=%d\n", rc);
		rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_ENABLE,
					prev_enable);
	}

	mutex_unlock(&vreg->aggr_vreg->lock);

	return rc;
}

/**
 * rpmh_regulator_disable() - disable the RPMh regulator
 * @rdev:		Regulator device pointer for the rpmh-regulator
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each rpmh-regulator device.
 *
 * Note that for ARC devices the enable state is handled via the voltage level
 * parameter.  Therefore, this enable value effectively masks or unmasks the
 * enabled voltage level.
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_disable(struct regulator_dev *rdev)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	u32 prev_enable;
	int rc;

	mutex_lock(&vreg->aggr_vreg->lock);

	prev_enable
	       = rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_ENABLE, 0);

	rc = rpmh_regulator_send_aggregate_requests(vreg);
	if (rc) {
		vreg_err(vreg, "disable failed, rc=%d\n", rc);
		rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_ENABLE,
					prev_enable);
	}

	mutex_unlock(&vreg->aggr_vreg->lock);

	return rc;
}

/**
 * rpmh_regulator_vrm_set_voltage() - set the voltage of the VRM rpmh-regulator
 * @rdev:		Regulator device pointer for the rpmh-regulator
 * @min_uv:		New voltage in microvolts to set
 * @max_uv:		Maximum voltage in microvolts allowed
 * @selector:		Unused
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each VRM rpmh-regulator device.
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_vrm_set_voltage(struct regulator_dev *rdev,
				int min_uv, int max_uv, unsigned int *selector)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	u32 prev_voltage;
	int mv;
	int rc = 0;

	mv = DIV_ROUND_UP(min_uv, 1000);
	if (mv * 1000 > max_uv) {
		vreg_err(vreg, "no set points available in range %d-%d uV\n",
			min_uv, max_uv);
		return -EINVAL;
	}

	mutex_lock(&vreg->aggr_vreg->lock);

	prev_voltage
	     = rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_VRM_VOLTAGE, mv);
	rpmh_regulator_check_param_max(vreg->aggr_vreg,
				RPMH_REGULATOR_REG_VRM_VOLTAGE, max_uv);

	rc = rpmh_regulator_send_aggregate_requests(vreg);
	if (rc) {
		vreg_err(vreg, "set voltage=%d mV failed, rc=%d\n", mv, rc);
		rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_VRM_VOLTAGE,
					prev_voltage);
	}

	mutex_unlock(&vreg->aggr_vreg->lock);

	return rc;
}

/**
 * rpmh_regulator_vrm_get_voltage() - get the voltage of the VRM rpmh-regulator
 * @rdev:		Regulator device pointer for the rpmh-regulator
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each VRM rpmh-regulator device.
 *
 * Return: regulator voltage in microvolts
 */
static int rpmh_regulator_vrm_get_voltage(struct regulator_dev *rdev)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	int uv;

	uv = vreg->req.reg[RPMH_REGULATOR_REG_VRM_VOLTAGE] * 1000;
	if (uv == 0)
		uv = VOLTAGE_UNKNOWN;

	return uv;
}

/**
 * rpmh_regulator_vrm_set_mode_index() - set the mode of a VRM regulator to the
 *		mode mapped to mode_index
 * @vreg:		Pointer to the RPMh regulator
 * @mode_index:		Index into aggr_vreg->mode[] array
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_vrm_set_mode_index(struct rpmh_vreg *vreg,
					     int mode_index)
{
	u32 prev_mode;
	int rc;

	mutex_lock(&vreg->aggr_vreg->lock);

	prev_mode = rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_VRM_MODE,
				vreg->aggr_vreg->mode[mode_index].pmic_mode);

	rc = rpmh_regulator_send_aggregate_requests(vreg);
	if (rc) {
		vreg_err(vreg, "set mode=%u failed, rc=%d\n",
			vreg->req.reg[RPMH_REGULATOR_REG_VRM_MODE],
			rc);
		rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_VRM_MODE,
					prev_mode);
	} else {
		vreg->mode_index = mode_index;
	}

	mutex_unlock(&vreg->aggr_vreg->lock);

	return rc;
}

/**
 * rpmh_regulator_vrm_set_mode() - set the mode of the VRM rpmh-regulator
 * @rdev:		Regulator device pointer for the rpmh-regulator
 * @mode:		The regulator framework mode to set
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each VRM rpmh-regulator device.
 *
 * This function sets the PMIC mode corresponding to the specified framework
 * mode.  The set of PMIC modes allowed is defined in device tree for a given
 * RPMh regulator resource.  The full mapping from generic modes to PMIC modes
 * and framework modes is defined in the rpmh_regulator_mode_map[] array.  The
 * RPMh resource specific mapping is defined in the aggr_vreg->mode[] array.
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_vrm_set_mode(struct regulator_dev *rdev,
					unsigned int mode)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	int i;

	for (i = 0; i < vreg->aggr_vreg->mode_count; i++)
		if (vreg->aggr_vreg->mode[i].framework_mode == mode)
			break;
	if (i >= vreg->aggr_vreg->mode_count) {
		vreg_err(vreg, "invalid mode=%u\n", mode);
		return -EINVAL;
	}

	return rpmh_regulator_vrm_set_mode_index(vreg, i);
}

/**
 * rpmh_regulator_vrm_get_mode() - get the mode of the VRM rpmh-regulator
 * @rdev:		Regulator device pointer for the rpmh-regulator
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each VRM rpmh-regulator device.
 *
 * Return: the regulator framework mode of the regulator
 */
static unsigned int rpmh_regulator_vrm_get_mode(struct regulator_dev *rdev)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->aggr_vreg->mode[vreg->mode_index].framework_mode;
}

/**
 * rpmh_regulator_vrm_set_load() - set the PMIC mode based upon the maximum load
 *		required from the VRM rpmh-regulator
 * @rdev:		Regulator device pointer for the rpmh-regulator
 * @load_ua:		Maximum current required from all consumers in microamps
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each VRM rpmh-regulator device.
 *
 * This function sets the mode of the regulator to that which has the highest
 * min support load less than or equal to load_ua.  Example:
 *	mode_count = 3
 *	mode[].min_load_ua = 0, 100000, 6000000
 *
 *	load_ua = 10000   --> mode_index = 0
 *	load_ua = 250000  --> mode_index = 1
 *	load_ua = 7000000 --> mode_index = 2
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_vrm_set_load(struct regulator_dev *rdev, int load_ua)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	int i;

	/* No need to check element 0 as it will be the default. */
	for (i = vreg->aggr_vreg->mode_count - 1; i > 0; i--)
		if (vreg->aggr_vreg->mode[i].min_load_ua <= load_ua)
			break;

	return rpmh_regulator_vrm_set_mode_index(vreg, i);
}

/**
 * rpmh_regulator_arc_set_voltage_sel() - set the voltage level of the ARC
 *		rpmh-regulator device
 * @rdev:		Regulator device pointer for the rpmh-regulator
 * @selector:		ARC voltage level to set
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each ARC rpmh-regulator device.
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_arc_set_voltage_sel(struct regulator_dev *rdev,
						unsigned int selector)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);
	u32 prev_level;
	int rc;

	mutex_lock(&vreg->aggr_vreg->lock);

	prev_level = rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_ARC_LEVEL,
						selector);

	rc = rpmh_regulator_send_aggregate_requests(vreg);
	if (rc) {
		vreg_err(vreg, "set level=%d failed, rc=%d\n",
			vreg->req.reg[RPMH_REGULATOR_REG_ARC_LEVEL],
			rc);
		rpmh_regulator_set_reg(vreg, RPMH_REGULATOR_REG_ARC_LEVEL,
					prev_level);
	}

	mutex_unlock(&vreg->aggr_vreg->lock);

	return rc;
}

/**
 * rpmh_regulator_arc_get_voltage_sel() - get the voltage level of the ARC
 *		rpmh-regulator device
 * @rdev:		Regulator device pointer for the rpmh-regulator
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each ARC rpmh-regulator device.
 *
 * Return: ARC voltage level
 */
static int rpmh_regulator_arc_get_voltage_sel(struct regulator_dev *rdev)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->req.reg[RPMH_REGULATOR_REG_ARC_LEVEL];
}

/**
 * rpmh_regulator_arc_list_voltage() - return the consumer voltage level mapped
 *		to a given ARC voltage level
 * @rdev:		Regulator device pointer for the rpmh-regulator
 * @selector:		ARC voltage level
 *
 * This function is passed as a callback function into the regulator ops that
 * are registered for each ARC rpmh-regulator device.
 *
 * Data ranges:
 * ARC voltage level:      0 - 15 (fixed in hardware)
 * Consumer voltage level: 1 - 513 (could be expanded to larger values)
 *
 * Return: consumer voltage level
 */
static int rpmh_regulator_arc_list_voltage(struct regulator_dev *rdev,
						unsigned int selector)
{
	struct rpmh_vreg *vreg = rdev_get_drvdata(rdev);

	if (selector >= vreg->aggr_vreg->level_count)
		return 0;

	return vreg->aggr_vreg->level[selector];
}

static const struct regulator_ops rpmh_regulator_vrm_ops = {
	.enable			= rpmh_regulator_enable,
	.disable		= rpmh_regulator_disable,
	.is_enabled		= rpmh_regulator_is_enabled,
	.set_voltage		= rpmh_regulator_vrm_set_voltage,
	.get_voltage		= rpmh_regulator_vrm_get_voltage,
	.set_mode		= rpmh_regulator_vrm_set_mode,
	.get_mode		= rpmh_regulator_vrm_get_mode,
	.set_load		= rpmh_regulator_vrm_set_load,
};

static const struct regulator_ops rpmh_regulator_arc_ops = {
	.enable			= rpmh_regulator_enable,
	.disable		= rpmh_regulator_disable,
	.is_enabled		= rpmh_regulator_is_enabled,
	.set_voltage_sel	= rpmh_regulator_arc_set_voltage_sel,
	.get_voltage_sel	= rpmh_regulator_arc_get_voltage_sel,
	.list_voltage		= rpmh_regulator_arc_list_voltage,
};

static const struct regulator_ops rpmh_regulator_xob_ops = {
	.enable			= rpmh_regulator_enable,
	.disable		= rpmh_regulator_disable,
	.is_enabled		= rpmh_regulator_is_enabled,
};

static const struct regulator_ops *rpmh_regulator_ops[] = {
	[RPMH_REGULATOR_TYPE_VRM]	= &rpmh_regulator_vrm_ops,
	[RPMH_REGULATOR_TYPE_ARC]	= &rpmh_regulator_arc_ops,
	[RPMH_REGULATOR_TYPE_XOB]	= &rpmh_regulator_xob_ops,
};

/**
 * rpmh_regulator_load_arc_level_mapping() - load the RPMh ARC resource's
 *		voltage level mapping from command db
 * @aggr_vreg:		Pointer to the aggregated rpmh regulator resource
 *
 * The set of supported RPMH_REGULATOR_LEVEL_* voltage levels (0 - ~512) that
 * map to ARC operating levels (0 - 15) is defined in aux data per ARC resource
 * in the command db SMEM data structure.  It is in a u16 array with 1 to 16
 * elements.  Note that the aux data array may be zero padded at the end for
 * data alignment purposes.  Such padding entries are invalid and must be
 * ignored.
 *
 * Return: 0 on success, errno on failure
 */
static int
rpmh_regulator_load_arc_level_mapping(struct rpmh_aggr_vreg *aggr_vreg)
{
	size_t len = 0;
	int i, j;
	const u8 *buf;

	buf = cmd_db_read_aux_data(aggr_vreg->resource_name, &len);
	if (IS_ERR(buf)) {
		aggr_vreg_err(aggr_vreg, "could not retrieve ARC aux data, rc=%ld\n",
				PTR_ERR(buf));
		return PTR_ERR(buf);
	} else if (len == 0) {
		aggr_vreg_err(aggr_vreg, "ARC level mapping data missing in command db\n");
		return -EINVAL;
	} else if (len > RPMH_ARC_MAX_LEVELS * RPMH_ARC_LEVEL_SIZE) {
		aggr_vreg_err(aggr_vreg, "more ARC levels defined than allowed: %zd > %d\n",
			len, RPMH_ARC_MAX_LEVELS * RPMH_ARC_LEVEL_SIZE);
		return -EINVAL;
	} else if (len % RPMH_ARC_LEVEL_SIZE) {
		aggr_vreg_err(aggr_vreg, "invalid ARC aux data size: %zd\n",
			len);
		return -EINVAL;
	}

	aggr_vreg->level_count = len / RPMH_ARC_LEVEL_SIZE;

	for (i = 0; i < aggr_vreg->level_count; i++) {
		for (j = 0; j < RPMH_ARC_LEVEL_SIZE; j++)
			aggr_vreg->level[i] |=
				buf[i * RPMH_ARC_LEVEL_SIZE + j] << (8 * j);

		/*
		 * The AUX data may be zero padded.  These 0 valued entries at
		 * the end of the map must be ignored.
		 */
		if (i > 0 && aggr_vreg->level[i] == 0) {
			aggr_vreg->level_count = i;
			break;
		}

		aggr_vreg_debug(aggr_vreg, "ARC hlvl=%2d --> vlvl=%4u\n",
				i, aggr_vreg->level[i]);
	}

	return 0;
}

/**
 * rpmh_regulator_parse_vrm_modes() - parse the supported mode configurations
 *		for a VRM RPMh resource from device tree
 * @aggr_vreg:		Pointer to the aggregated rpmh regulator resource
 *
 * This function initializes the mode[] array of aggr_vreg based upon the values
 * of optional device tree properties.
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_parse_vrm_modes(struct rpmh_aggr_vreg *aggr_vreg)
{
	struct device_node *node = aggr_vreg->dev->of_node;
	const char *type = "";
	const struct rpmh_regulator_mode *map;
	const char *prop;
	int i, len, rc;
	u32 *buf, disable_mode;

	aggr_vreg->regulator_hw_type = RPMH_REGULATOR_HW_TYPE_UNKNOWN;
	aggr_vreg->disable_pmic_mode = -EINVAL;

	/* qcom,regulator-type is optional */
	prop = "qcom,regulator-type";
	if (!of_find_property(node, prop, &len))
		return 0;

	rc = of_property_read_string(node, prop, &type);
	if (rc) {
		aggr_vreg_err(aggr_vreg, "unable to read %s, rc=%d\n",
				prop, rc);
		return rc;
	}

	if (!strcmp(type, "pmic4-ldo")) {
		aggr_vreg->regulator_hw_type
			= RPMH_REGULATOR_HW_TYPE_PMIC4_LDO;
	} else if (!strcmp(type, "pmic4-hfsmps")) {
		aggr_vreg->regulator_hw_type
			= RPMH_REGULATOR_HW_TYPE_PMIC4_HFSMPS;
	} else if (!strcmp(type, "pmic4-ftsmps")) {
		aggr_vreg->regulator_hw_type
			= RPMH_REGULATOR_HW_TYPE_PMIC4_FTSMPS;
	} else if (!strcmp(type, "pmic4-bob")) {
		aggr_vreg->regulator_hw_type
			= RPMH_REGULATOR_HW_TYPE_PMIC4_BOB;
	} else if (!strcmp(type, "pmic5-ldo")) {
		aggr_vreg->regulator_hw_type
			= RPMH_REGULATOR_HW_TYPE_PMIC5_LDO;
	} else if (!strcmp(type, "pmic5-hfsmps")) {
		aggr_vreg->regulator_hw_type
			= RPMH_REGULATOR_HW_TYPE_PMIC5_HFSMPS;
	} else if (!strcmp(type, "pmic5-ftsmps")) {
		aggr_vreg->regulator_hw_type
			= RPMH_REGULATOR_HW_TYPE_PMIC5_FTSMPS;
	} else if (!strcmp(type, "pmic5-bob")) {
		aggr_vreg->regulator_hw_type
			= RPMH_REGULATOR_HW_TYPE_PMIC5_BOB;
	} else {
		aggr_vreg_err(aggr_vreg, "unknown %s = %s\n",
				prop, type);
		return -EINVAL;
	}

	map = rpmh_regulator_mode_map[aggr_vreg->regulator_hw_type];

	prop = "qcom,disable-mode";
	if (!of_property_read_u32(node, prop, &disable_mode)) {
		if (disable_mode >= RPMH_REGULATOR_MODE_COUNT) {
			aggr_vreg_err(aggr_vreg, "qcom,disable-mode value %u is invalid\n",
				disable_mode);
			return -EINVAL;
		}

		if (!map[disable_mode].framework_mode) {
			aggr_vreg_err(aggr_vreg, "qcom,disable-mode value %u is invalid for regulator type = %s\n",
				disable_mode, type);
			return -EINVAL;
		}

		aggr_vreg->disable_pmic_mode = map[disable_mode].pmic_mode;
	}

	/* qcom,supported-modes is optional */
	prop = "qcom,supported-modes";
	if (!of_find_property(node, prop, &len))
		return 0;

	len /= sizeof(u32);
	aggr_vreg->mode = devm_kcalloc(aggr_vreg->dev, len,
					sizeof(*aggr_vreg->mode), GFP_KERNEL);
	if (!aggr_vreg->mode)
		return -ENOMEM;
	aggr_vreg->mode_count = len;


	buf = kcalloc(len, sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = of_property_read_u32_array(node, prop, buf, len);
	if (rc) {
		aggr_vreg_err(aggr_vreg, "unable to read %s, rc=%d\n",
			prop, rc);
		goto done;
	}

	for (i = 0; i < len; i++) {
		if (buf[i] >= RPMH_REGULATOR_MODE_COUNT) {
			aggr_vreg_err(aggr_vreg, "element %d of %s = %u is invalid\n",
				i, prop, buf[i]);
			rc = -EINVAL;
			goto done;
		}

		if (!map[buf[i]].framework_mode) {
			aggr_vreg_err(aggr_vreg, "element %d of %s = %u is invalid for regulator type = %s\n",
				i, prop, buf[i], type);
			rc = -EINVAL;
			goto done;
		}

		aggr_vreg->mode[i].pmic_mode = map[buf[i]].pmic_mode;
		aggr_vreg->mode[i].framework_mode = map[buf[i]].framework_mode;

		if (i > 0 && aggr_vreg->mode[i].pmic_mode
				<= aggr_vreg->mode[i - 1].pmic_mode) {
			aggr_vreg_err(aggr_vreg, "%s elements are not in ascending order\n",
				prop);
			rc = -EINVAL;
			goto done;
		}
	}

	prop = "qcom,mode-threshold-currents";

	rc = of_property_read_u32_array(node, prop, buf, len);
	if (rc) {
		aggr_vreg_err(aggr_vreg, "unable to read %s, rc=%d\n",
			prop, rc);
		goto done;
	}

	for (i = 0; i < len; i++) {
		aggr_vreg->mode[i].min_load_ua = buf[i];

		if (i > 0 && aggr_vreg->mode[i].min_load_ua
				<= aggr_vreg->mode[i - 1].min_load_ua) {
			aggr_vreg_err(aggr_vreg, "%s elements are not in ascending order\n",
				prop);
			rc = -EINVAL;
			goto done;
		}
	}

done:
	kfree(buf);
	return rc;
}

/**
 * rpmh_regulator_allocate_vreg() - allocate space for the regulators associated
 *		with the RPMh regulator resource and initialize important
 *		pointers for each regulator
 * @aggr_vreg:		Pointer to the aggregated rpmh regulator resource
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_allocate_vreg(struct rpmh_aggr_vreg *aggr_vreg)
{
	struct device_node *node;
	int i, rc;

	aggr_vreg->vreg_count = 0;

	for_each_available_child_of_node(aggr_vreg->dev->of_node, node) {
		/* Skip child nodes handled by other drivers. */
		if (of_find_property(node, "compatible", NULL))
			continue;
		aggr_vreg->vreg_count++;
	}

	if (aggr_vreg->vreg_count == 0) {
		aggr_vreg_err(aggr_vreg, "could not find any regulator subnodes\n");
		return -ENODEV;
	}

	aggr_vreg->vreg = devm_kcalloc(aggr_vreg->dev, aggr_vreg->vreg_count,
			sizeof(*aggr_vreg->vreg), GFP_KERNEL);
	if (!aggr_vreg->vreg)
		return -ENOMEM;

	i = 0;
	for_each_available_child_of_node(aggr_vreg->dev->of_node, node) {
		/* Skip child nodes handled by other drivers. */
		if (of_find_property(node, "compatible", NULL))
			continue;

		aggr_vreg->vreg[i].of_node = node;
		aggr_vreg->vreg[i].aggr_vreg = aggr_vreg;

		rc = of_property_read_string(node, "regulator-name",
						&aggr_vreg->vreg[i].rdesc.name);
		if (rc) {
			aggr_vreg_err(aggr_vreg, "could not read regulator-name property, rc=%d\n",
				rc);
			return rc;
		}

		i++;
	}

	return 0;
}

/**
 * rpmh_regulator_load_default_parameters() - initialize the RPMh resource
 *		request for this regulator based on optional device tree
 *		properties
 * @vreg:		Pointer to the RPMh regulator
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_load_default_parameters(struct rpmh_vreg *vreg)
{
	enum rpmh_regulator_type type = vreg->aggr_vreg->regulator_type;
	const struct rpmh_regulator_mode *map;
	const char *prop;
	int i, rc;
	u32 temp;

	if (type == RPMH_REGULATOR_TYPE_ARC) {
		prop = "qcom,init-voltage-level";
		rc = of_property_read_u32(vreg->of_node, prop, &temp);
		if (!rc) {
			for (i = 0; i < vreg->aggr_vreg->level_count; i++)
				if (temp <= vreg->aggr_vreg->level[i])
					break;
			if (i < vreg->aggr_vreg->level_count) {
				rpmh_regulator_set_reg(vreg,
					RPMH_REGULATOR_REG_ARC_LEVEL, i);
			} else {
				vreg_err(vreg, "%s=%u is invalid\n",
					prop, temp);
				return -EINVAL;
			}
		}

		prop = "qcom,min-dropout-voltage-level";
		rc = of_property_read_u32(vreg->of_node, prop, &temp);
		if (!rc)
			vreg->rdesc.min_dropout_uV = temp;
	} else if (type == RPMH_REGULATOR_TYPE_VRM) {
		prop = "qcom,init-enable";
		rc = of_property_read_u32(vreg->of_node, prop, &temp);
		if (!rc)
			rpmh_regulator_set_reg(vreg,
						RPMH_REGULATOR_REG_VRM_ENABLE,
						!!temp);

		prop = "qcom,init-voltage";
		rc = of_property_read_u32(vreg->of_node, prop, &temp);
		if (!rc) {
			if (temp < RPMH_VRM_MIN_UV || temp > RPMH_VRM_MAX_UV) {
				vreg_err(vreg, "%s=%u is invalid\n",
					prop, temp);
				return -EINVAL;
			}
			rpmh_regulator_set_reg(vreg,
						RPMH_REGULATOR_REG_VRM_VOLTAGE,
						temp / 1000);
		}

		prop = "qcom,init-mode";
		rc = of_property_read_u32(vreg->of_node, prop, &temp);
		if (!rc) {
			if (temp >= RPMH_REGULATOR_MODE_COUNT) {
				vreg_err(vreg, "%s=%u is invalid\n",
					prop, temp);
				return -EINVAL;
			} else if (vreg->aggr_vreg->regulator_hw_type
					== RPMH_REGULATOR_HW_TYPE_UNKNOWN) {
				vreg_err(vreg, "qcom,regulator-type missing so %s cannot be used\n",
					prop);
				return -EINVAL;
			}

			map = rpmh_regulator_mode_map[
					vreg->aggr_vreg->regulator_hw_type];
			if (!map[temp].framework_mode) {
				vreg_err(vreg, "%s=%u is not supported by type = %d\n",
					prop, temp,
					vreg->aggr_vreg->regulator_hw_type);
				return -EINVAL;
			}

			rpmh_regulator_set_reg(vreg,
						RPMH_REGULATOR_REG_VRM_MODE,
						map[temp].pmic_mode);
			for (i = 0; i < vreg->aggr_vreg->mode_count; i++) {
				if (vreg->aggr_vreg->mode[i].pmic_mode
				    == map[temp].pmic_mode) {
					vreg->mode_index = i;
					break;
				}
			}
		}

		prop = "qcom,init-headroom-voltage";
		rc = of_property_read_u32(vreg->of_node, prop, &temp);
		if (!rc) {
			if (temp < RPMH_VRM_HEADROOM_MIN_UV ||
			    temp > RPMH_VRM_HEADROOM_MAX_UV) {
				vreg_err(vreg, "%s=%u is invalid\n",
					prop, temp);
				return -EINVAL;
			}
			rpmh_regulator_set_reg(vreg,
						RPMH_REGULATOR_REG_VRM_HEADROOM,
						temp / 1000);
		}

		prop = "qcom,min-dropout-voltage";
		rc = of_property_read_u32(vreg->of_node, prop, &temp);
		if (!rc)
			vreg->rdesc.min_dropout_uV = temp;
	} else if (type == RPMH_REGULATOR_TYPE_XOB) {
		prop = "qcom,init-enable";
		rc = of_property_read_u32(vreg->of_node, prop, &temp);
		if (!rc)
			rpmh_regulator_set_reg(vreg,
						RPMH_REGULATOR_REG_XOB_ENABLE,
						!!temp);
	}

	return 0;
}

/**
 * rpmh_regulator_init_vreg_supply() - initialize the regulator's parent supply
 *		mapping based on optional DT parent supply property
 * @vreg:		Pointer to the RPMh regulator
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_init_vreg_supply(struct rpmh_vreg *vreg)
{
	char *buf;
	size_t len;

	len = strlen(vreg->rdesc.name) + 16;
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	scnprintf(buf, len, "%s-parent-supply", vreg->rdesc.name);

	if (of_find_property(vreg->aggr_vreg->dev->of_node, buf, NULL) ||
	    of_find_property(vreg->of_node, buf, NULL)) {
		kfree(buf);

		len = strlen(vreg->rdesc.name) + 10;
		buf = devm_kzalloc(vreg->aggr_vreg->dev, len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		scnprintf(buf, len, "%s-parent", vreg->rdesc.name);

		vreg->rdesc.supply_name = buf;
	} else if (of_find_property(vreg->of_node, "vin-supply", NULL)) {
		kfree(buf);
		vreg->rdesc.supply_name = "vin";
	} else {
		kfree(buf);
	}

	return 0;
}

/**
 * rpmh_regulator_init_vreg() - initialize all abbributes of an rpmh-regulator
 * @vreg:		Pointer to the RPMh regulator
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_init_vreg(struct rpmh_vreg *vreg)
{
	struct device *dev = vreg->aggr_vreg->dev;
	enum rpmh_regulator_type type = vreg->aggr_vreg->regulator_type;
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data;
	struct regulator_ops *ops;
	int rc, i;
	u32 set;

	ops = devm_kzalloc(dev, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	*ops			= *rpmh_regulator_ops[type];
	vreg->rdesc.owner	= THIS_MODULE;
	vreg->rdesc.type	= REGULATOR_VOLTAGE;
	vreg->rdesc.ops		= ops;

	init_data = of_get_regulator_init_data(dev,
						vreg->of_node, &vreg->rdesc);
	if (init_data == NULL)
		return -ENOMEM;

	init_data->constraints.input_uV = init_data->constraints.max_uV;
	if (type == RPMH_REGULATOR_TYPE_VRM) {
		init_data->constraints.min_uV
			= max(init_data->constraints.min_uV, RPMH_VRM_MIN_UV);
		init_data->constraints.min_uV
			= min(init_data->constraints.min_uV, RPMH_VRM_MAX_UV);
		init_data->constraints.max_uV
			= max(init_data->constraints.max_uV, RPMH_VRM_MIN_UV);
		init_data->constraints.max_uV
			= min(init_data->constraints.max_uV, RPMH_VRM_MAX_UV);
	}

	if (ops->set_voltage || ops->set_voltage_sel)
		init_data->constraints.valid_ops_mask
			|= REGULATOR_CHANGE_VOLTAGE;

	if (type == RPMH_REGULATOR_TYPE_XOB
	    && init_data->constraints.min_uV == init_data->constraints.max_uV)
		vreg->rdesc.fixed_uV = init_data->constraints.min_uV;

	if (vreg->aggr_vreg->mode_count) {
		init_data->constraints.valid_ops_mask
			|= REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_DRMS;
		for (i = 0; i < vreg->aggr_vreg->mode_count; i++)
			init_data->constraints.valid_modes_mask
				|= vreg->aggr_vreg->mode[i].framework_mode;
	} else {
		ops->get_mode = NULL;
		ops->set_mode = NULL;
		ops->set_load = NULL;
	}

	/*
	 * Remove enable state control if the ARC resource does not support the
	 * off level.
	 */
	if (type == RPMH_REGULATOR_TYPE_ARC
	    && vreg->aggr_vreg->level[0] != RPMH_REGULATOR_LEVEL_OFF) {
		ops->enable = NULL;
		ops->disable = NULL;
		ops->is_enabled = NULL;
	}
	if (ops->enable)
		init_data->constraints.valid_ops_mask
			|= REGULATOR_CHANGE_STATUS;

	switch (type) {
	case RPMH_REGULATOR_TYPE_VRM:
		vreg->rdesc.n_voltages = 2;
		break;
	case RPMH_REGULATOR_TYPE_ARC:
		vreg->rdesc.n_voltages = vreg->aggr_vreg->level_count;
		break;
	case RPMH_REGULATOR_TYPE_XOB:
		vreg->rdesc.n_voltages = 1;
		break;
	default:
		return -EINVAL;
	}

	rc = of_property_read_u32(vreg->of_node, "qcom,set", &set);
	if (rc) {
		vreg_err(vreg, "qcom,set property missing, rc=%d\n", rc);
		return rc;
	} else if (!(set & RPMH_REGULATOR_SET_ALL)) {
		vreg_err(vreg, "qcom,set=%u property is invalid\n", set);
		return rc;
	}

	vreg->set_active = !!(set & RPMH_REGULATOR_SET_ACTIVE);
	vreg->set_sleep = !!(set & RPMH_REGULATOR_SET_SLEEP);

	rc = rpmh_regulator_init_vreg_supply(vreg);
	if (rc) {
		vreg_err(vreg, "unable to initialize regulator supply name, rc=%d\n",
			rc);
		return rc;
	}

	reg_config.dev			= dev;
	reg_config.init_data		= init_data;
	reg_config.of_node		= vreg->of_node;
	reg_config.driver_data		= vreg;

	rc = rpmh_regulator_load_default_parameters(vreg);
	if (rc) {
		vreg_err(vreg, "unable to load default parameters, rc=%d\n",
			rc);
		return rc;
	}

	vreg->rdev = devm_regulator_register(dev, &vreg->rdesc, &reg_config);
	if (IS_ERR(vreg->rdev)) {
		rc = PTR_ERR(vreg->rdev);
		vreg->rdev = NULL;
		vreg_err(vreg, "devm_regulator_register() failed, rc=%d\n", rc);
		return rc;
	}

	rc = devm_regulator_proxy_consumer_register(dev, vreg->of_node);
	if (rc)
		vreg_err(vreg, "failed to register proxy consumer, rc=%d\n",
			rc);

	rc = devm_regulator_debug_register(dev, vreg->rdev);
	if (rc)
		vreg_err(vreg, "failed to register debug regulator, rc=%d\n",
			rc);

	vreg_debug(vreg, "successfully registered; set=%s\n",
		vreg->set_active && vreg->set_sleep
			? "active + sleep"
			: vreg->set_active ? "active" : "sleep");

	return 0;
}

static const struct of_device_id rpmh_regulator_match_table[] = {
	{
		.compatible = "qcom,rpmh-vrm-regulator",
		.data = (void *)(uintptr_t)RPMH_REGULATOR_TYPE_VRM,
	},
	{
		.compatible = "qcom,rpmh-arc-regulator",
		.data = (void *)(uintptr_t)RPMH_REGULATOR_TYPE_ARC,
	},
	{
		.compatible = "qcom,rpmh-xob-regulator",
		.data = (void *)(uintptr_t)RPMH_REGULATOR_TYPE_XOB,
	},
	{}
};

/**
 * rpmh_regulator_probe() - probe an aggregated RPMh regulator resource and
 *		register regulators for each of the regulator nodes associated
 *		with it
 * @pdev:		Pointer to the platform device of the aggregated rpmh
 *			regulator resource
 *
 * Return: 0 on success, errno on failure
 */
static int rpmh_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct rpmh_aggr_vreg *aggr_vreg;
	struct device_node *node;
	int rc, i, sid;

	node = dev->of_node;

	if (!node) {
		dev_err(dev, "Device tree node is missing\n");
		return -EINVAL;
	}

	aggr_vreg = devm_kzalloc(dev, sizeof(*aggr_vreg), GFP_KERNEL);
	if (!aggr_vreg)
		return -ENOMEM;

	aggr_vreg->dev = dev;
	mutex_init(&aggr_vreg->lock);

	match = of_match_node(rpmh_regulator_match_table, node);
	if (match) {
		aggr_vreg->regulator_type = (uintptr_t)match->data;
	} else {
		dev_err(dev, "could not find compatible string match\n");
		return -ENODEV;
	}

	rc = of_property_read_string(node, "qcom,resource-name",
				     &aggr_vreg->resource_name);
	if (rc) {
		dev_err(dev, "qcom,resource-name missing in DT node\n");
		return rc;
	}

	aggr_vreg->addr = cmd_db_read_addr(aggr_vreg->resource_name);
	if (!aggr_vreg->addr) {
		aggr_vreg_err(aggr_vreg, "could not find RPMh address for resource\n");
		return -ENODEV;
	}

	sid = cmd_db_read_slave_id(aggr_vreg->resource_name);
	if (sid < 0) {
		aggr_vreg_err(aggr_vreg, "could not find RPMh slave id for resource, rc=%d\n",
			sid);
		return sid;
	}

	/* Confirm slave ID listed in command DB matches DT configuration. */
	if ((aggr_vreg->regulator_type == RPMH_REGULATOR_TYPE_ARC
			&& sid != CMD_DB_HW_ARC)
	    || (aggr_vreg->regulator_type == RPMH_REGULATOR_TYPE_VRM
			&& sid != CMD_DB_HW_VRM)
	    || (aggr_vreg->regulator_type == RPMH_REGULATOR_TYPE_XOB
			&& sid != CMD_DB_HW_XOB)) {
		aggr_vreg_err(aggr_vreg, "RPMh slave ID mismatch; config=%d (%s) != cmd-db=%d\n",
			aggr_vreg->regulator_type,
			aggr_vreg->regulator_type == RPMH_REGULATOR_TYPE_ARC
				? "ARC" : (aggr_vreg->regulator_type
						== RPMH_REGULATOR_TYPE_VRM
					  ? "VRM" : "XOB"),
			sid);
		return -EINVAL;
	}

	if (aggr_vreg->regulator_type == RPMH_REGULATOR_TYPE_ARC) {
		rc = rpmh_regulator_load_arc_level_mapping(aggr_vreg);
		if (rc) {
			aggr_vreg_err(aggr_vreg, "could not load arc level mapping, rc=%d\n",
				rc);
			return rc;
		}
	} else if (aggr_vreg->regulator_type == RPMH_REGULATOR_TYPE_VRM) {
		rc = rpmh_regulator_parse_vrm_modes(aggr_vreg);
		if (rc) {
			aggr_vreg_err(aggr_vreg, "could not parse vrm mode mapping, rc=%d\n",
				rc);
			return rc;
		}
	}

	aggr_vreg->always_wait_for_ack
		= of_property_read_bool(node, "qcom,always-wait-for-ack");

	rc = rpmh_regulator_allocate_vreg(aggr_vreg);
	if (rc) {
		aggr_vreg_err(aggr_vreg, "failed to allocate regulator subnode array, rc=%d\n",
			rc);
		return rc;
	}

	for (i = 0; i < aggr_vreg->vreg_count; i++) {
		rc = rpmh_regulator_init_vreg(&aggr_vreg->vreg[i]);
		if (rc) {
			pr_err("unable to initialize rpmh-regulator vreg %s for resource %s, rc=%d\n",
				aggr_vreg->vreg[i].rdesc.name,
				aggr_vreg->resource_name, rc);
			return rc;
		}
	}

	if (of_property_read_bool(node, "qcom,send-defaults")) {
		mutex_lock(&aggr_vreg->lock);
		rc = rpmh_regulator_send_aggregate_requests(
					&aggr_vreg->vreg[0]);
		if (rc) {
			aggr_vreg_err(aggr_vreg, "error while sending default request, rc=%d\n",
				rc);
			mutex_unlock(&aggr_vreg->lock);
			return rc;
		}
		mutex_unlock(&aggr_vreg->lock);
	}

	of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	platform_set_drvdata(pdev, aggr_vreg);

	aggr_vreg_debug(aggr_vreg, "successfully probed; addr=0x%05X, type=%s\n",
			aggr_vreg->addr,
			aggr_vreg->regulator_type == RPMH_REGULATOR_TYPE_ARC
				? "ARC"
				: (aggr_vreg->regulator_type
						== RPMH_REGULATOR_TYPE_VRM
					? "VRM" : "XOB"));

	return rc;
}

static struct platform_driver rpmh_regulator_driver = {
	.driver = {
		.name		= "qcom,rpmh-regulator",
		.of_match_table	= rpmh_regulator_match_table,
		.sync_state	= regulator_proxy_consumer_sync_state,
	},
	.probe = rpmh_regulator_probe,
};

static int rpmh_regulator_init(void)
{
	rpmh_reg_ipc_log = ipc_log_context_create(IPC_LOG_PAGES, "rpmh_regulator", 0);
	return platform_driver_register(&rpmh_regulator_driver);
}

static void rpmh_regulator_exit(void)
{
	if (rpmh_reg_ipc_log)
		ipc_log_context_destroy(rpmh_reg_ipc_log);
	platform_driver_unregister(&rpmh_regulator_driver);
}

MODULE_DESCRIPTION("RPMh regulator driver");
MODULE_LICENSE("GPL v2");

arch_initcall(rpmh_regulator_init);
module_exit(rpmh_regulator_exit);
