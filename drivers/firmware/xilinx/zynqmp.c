// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2020 Xilinx, Inc.
 *
 *  Michal Simek <michal.simek@xilinx.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#include <linux/arm-smccc.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/firmware/xlnx-zynqmp.h>
#include "zynqmp-debug.h"

static bool feature_check_enabled;
static u32 zynqmp_pm_features[PM_API_MAX];

static const struct mfd_cell firmware_devs[] = {
	{
		.name = "zynqmp_power_controller",
	},
};

/**
 * zynqmp_pm_ret_code() - Convert PMU-FW error codes to Linux error codes
 * @ret_status:		PMUFW return code
 *
 * Return: corresponding Linux error code
 */
static int zynqmp_pm_ret_code(u32 ret_status)
{
	switch (ret_status) {
	case XST_PM_SUCCESS:
	case XST_PM_DOUBLE_REQ:
		return 0;
	case XST_PM_NO_FEATURE:
		return -ENOTSUPP;
	case XST_PM_NO_ACCESS:
		return -EACCES;
	case XST_PM_ABORT_SUSPEND:
		return -ECANCELED;
	case XST_PM_MULT_USER:
		return -EUSERS;
	case XST_PM_INTERNAL:
	case XST_PM_CONFLICT:
	case XST_PM_INVALID_NODE:
	default:
		return -EINVAL;
	}
}

static noinline int do_fw_call_fail(u64 arg0, u64 arg1, u64 arg2,
				    u32 *ret_payload)
{
	return -ENODEV;
}

/*
 * PM function call wrapper
 * Invoke do_fw_call_smc or do_fw_call_hvc, depending on the configuration
 */
static int (*do_fw_call)(u64, u64, u64, u32 *ret_payload) = do_fw_call_fail;

/**
 * do_fw_call_smc() - Call system-level platform management layer (SMC)
 * @arg0:		Argument 0 to SMC call
 * @arg1:		Argument 1 to SMC call
 * @arg2:		Argument 2 to SMC call
 * @ret_payload:	Returned value array
 *
 * Invoke platform management function via SMC call (no hypervisor present).
 *
 * Return: Returns status, either success or error+reason
 */
static noinline int do_fw_call_smc(u64 arg0, u64 arg1, u64 arg2,
				   u32 *ret_payload)
{
	struct arm_smccc_res res;

	arm_smccc_smc(arg0, arg1, arg2, 0, 0, 0, 0, 0, &res);

	if (ret_payload) {
		ret_payload[0] = lower_32_bits(res.a0);
		ret_payload[1] = upper_32_bits(res.a0);
		ret_payload[2] = lower_32_bits(res.a1);
		ret_payload[3] = upper_32_bits(res.a1);
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)res.a0);
}

/**
 * do_fw_call_hvc() - Call system-level platform management layer (HVC)
 * @arg0:		Argument 0 to HVC call
 * @arg1:		Argument 1 to HVC call
 * @arg2:		Argument 2 to HVC call
 * @ret_payload:	Returned value array
 *
 * Invoke platform management function via HVC
 * HVC-based for communication through hypervisor
 * (no direct communication with ATF).
 *
 * Return: Returns status, either success or error+reason
 */
static noinline int do_fw_call_hvc(u64 arg0, u64 arg1, u64 arg2,
				   u32 *ret_payload)
{
	struct arm_smccc_res res;

	arm_smccc_hvc(arg0, arg1, arg2, 0, 0, 0, 0, 0, &res);

	if (ret_payload) {
		ret_payload[0] = lower_32_bits(res.a0);
		ret_payload[1] = upper_32_bits(res.a0);
		ret_payload[2] = lower_32_bits(res.a1);
		ret_payload[3] = upper_32_bits(res.a1);
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)res.a0);
}

/**
 * zynqmp_pm_feature() - Check weather given feature is supported or not
 * @api_id:		API ID to check
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_feature(u32 api_id)
{
	int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];
	u64 smc_arg[2];

	if (!feature_check_enabled)
		return 0;

	/* Return value if feature is already checked */
	if (zynqmp_pm_features[api_id] != PM_FEATURE_UNCHECKED)
		return zynqmp_pm_features[api_id];

	smc_arg[0] = PM_SIP_SVC | PM_FEATURE_CHECK;
	smc_arg[1] = api_id;

	ret = do_fw_call(smc_arg[0], smc_arg[1], 0, ret_payload);
	if (ret) {
		zynqmp_pm_features[api_id] = PM_FEATURE_INVALID;
		return PM_FEATURE_INVALID;
	}

	zynqmp_pm_features[api_id] = ret_payload[1];

	return zynqmp_pm_features[api_id];
}

/**
 * zynqmp_pm_invoke_fn() - Invoke the system-level platform management layer
 *			   caller function depending on the configuration
 * @pm_api_id:		Requested PM-API call
 * @arg0:		Argument 0 to requested PM-API call
 * @arg1:		Argument 1 to requested PM-API call
 * @arg2:		Argument 2 to requested PM-API call
 * @arg3:		Argument 3 to requested PM-API call
 * @ret_payload:	Returned value array
 *
 * Invoke platform management function for SMC or HVC call, depending on
 * configuration.
 * Following SMC Calling Convention (SMCCC) for SMC64:
 * Pm Function Identifier,
 * PM_SIP_SVC + PM_API_ID =
 *	((SMC_TYPE_FAST << FUNCID_TYPE_SHIFT)
 *	((SMC_64) << FUNCID_CC_SHIFT)
 *	((SIP_START) << FUNCID_OEN_SHIFT)
 *	((PM_API_ID) & FUNCID_NUM_MASK))
 *
 * PM_SIP_SVC	- Registered ZynqMP SIP Service Call.
 * PM_API_ID	- Platform Management API ID.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_invoke_fn(u32 pm_api_id, u32 arg0, u32 arg1,
			u32 arg2, u32 arg3, u32 *ret_payload)
{
	/*
	 * Added SIP service call Function Identifier
	 * Make sure to stay in x0 register
	 */
	u64 smc_arg[4];

	if (zynqmp_pm_feature(pm_api_id) == PM_FEATURE_INVALID)
		return -ENOTSUPP;

	smc_arg[0] = PM_SIP_SVC | pm_api_id;
	smc_arg[1] = ((u64)arg1 << 32) | arg0;
	smc_arg[2] = ((u64)arg3 << 32) | arg2;

	return do_fw_call(smc_arg[0], smc_arg[1], smc_arg[2], ret_payload);
}

static u32 pm_api_version;
static u32 pm_tz_version;

/**
 * zynqmp_pm_get_api_version() - Get version number of PMU PM firmware
 * @version:	Returned version value
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_api_version(u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!version)
		return -EINVAL;

	/* Check is PM API version already verified */
	if (pm_api_version > 0) {
		*version = pm_api_version;
		return 0;
	}
	ret = zynqmp_pm_invoke_fn(PM_GET_API_VERSION, 0, 0, 0, 0, ret_payload);
	*version = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_api_version);

/**
 * zynqmp_pm_get_chipid - Get silicon ID registers
 * @idcode:     IDCODE register
 * @version:    version register
 *
 * Return:      Returns the status of the operation and the idcode and version
 *              registers in @idcode and @version.
 */
int zynqmp_pm_get_chipid(u32 *idcode, u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!idcode || !version)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_GET_CHIPID, 0, 0, 0, 0, ret_payload);
	*idcode = ret_payload[1];
	*version = ret_payload[2];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_chipid);

/**
 * zynqmp_pm_get_trustzone_version() - Get secure trustzone firmware version
 * @version:	Returned version value
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_get_trustzone_version(u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!version)
		return -EINVAL;

	/* Check is PM trustzone version already verified */
	if (pm_tz_version > 0) {
		*version = pm_tz_version;
		return 0;
	}
	ret = zynqmp_pm_invoke_fn(PM_GET_TRUSTZONE_VERSION, 0, 0,
				  0, 0, ret_payload);
	*version = ret_payload[1];

	return ret;
}

/**
 * get_set_conduit_method() - Choose SMC or HVC based communication
 * @np:		Pointer to the device_node structure
 *
 * Use SMC or HVC-based functions to communicate with EL2/EL3.
 *
 * Return: Returns 0 on success or error code
 */
static int get_set_conduit_method(struct device_node *np)
{
	const char *method;

	if (of_property_read_string(np, "method", &method)) {
		pr_warn("%s missing \"method\" property\n", __func__);
		return -ENXIO;
	}

	if (!strcmp("hvc", method)) {
		do_fw_call = do_fw_call_hvc;
	} else if (!strcmp("smc", method)) {
		do_fw_call = do_fw_call_smc;
	} else {
		pr_warn("%s Invalid \"method\" property: %s\n",
			__func__, method);
		return -EINVAL;
	}

	return 0;
}

/**
 * zynqmp_pm_query_data() - Get query data from firmware
 * @qdata:	Variable to the zynqmp_pm_query_data structure
 * @out:	Returned output value
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_query_data(struct zynqmp_pm_query_data qdata, u32 *out)
{
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_QUERY_DATA, qdata.qid, qdata.arg1,
				  qdata.arg2, qdata.arg3, out);

	/*
	 * For clock name query, all bytes in SMC response are clock name
	 * characters and return code is always success. For invalid clocks,
	 * clock name bytes would be zeros.
	 */
	return qdata.qid == PM_QID_CLOCK_GET_NAME ? 0 : ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_query_data);

/**
 * zynqmp_pm_clock_enable() - Enable the clock for given id
 * @clock_id:	ID of the clock to be enabled
 *
 * This function is used by master to enable the clock
 * including peripherals and PLL clocks.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_enable(u32 clock_id)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_ENABLE, clock_id, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_enable);

/**
 * zynqmp_pm_clock_disable() - Disable the clock for given id
 * @clock_id:	ID of the clock to be disable
 *
 * This function is used by master to disable the clock
 * including peripherals and PLL clocks.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_disable(u32 clock_id)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_DISABLE, clock_id, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_disable);

/**
 * zynqmp_pm_clock_getstate() - Get the clock state for given id
 * @clock_id:	ID of the clock to be queried
 * @state:	1/0 (Enabled/Disabled)
 *
 * This function is used by master to get the state of clock
 * including peripherals and PLL clocks.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_getstate(u32 clock_id, u32 *state)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETSTATE, clock_id, 0,
				  0, 0, ret_payload);
	*state = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_getstate);

/**
 * zynqmp_pm_clock_setdivider() - Set the clock divider for given id
 * @clock_id:	ID of the clock
 * @divider:	divider value
 *
 * This function is used by master to set divider for any clock
 * to achieve desired rate.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_setdivider(u32 clock_id, u32 divider)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_SETDIVIDER, clock_id, divider,
				   0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_setdivider);

/**
 * zynqmp_pm_clock_getdivider() - Get the clock divider for given id
 * @clock_id:	ID of the clock
 * @divider:	divider value
 *
 * This function is used by master to get divider values
 * for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_getdivider(u32 clock_id, u32 *divider)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETDIVIDER, clock_id, 0,
				  0, 0, ret_payload);
	*divider = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_getdivider);

/**
 * zynqmp_pm_clock_setrate() - Set the clock rate for given id
 * @clock_id:	ID of the clock
 * @rate:	rate value in hz
 *
 * This function is used by master to set rate for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_setrate(u32 clock_id, u64 rate)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_SETRATE, clock_id,
				   lower_32_bits(rate),
				   upper_32_bits(rate),
				   0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_setrate);

/**
 * zynqmp_pm_clock_getrate() - Get the clock rate for given id
 * @clock_id:	ID of the clock
 * @rate:	rate value in hz
 *
 * This function is used by master to get rate
 * for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_getrate(u32 clock_id, u64 *rate)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETRATE, clock_id, 0,
				  0, 0, ret_payload);
	*rate = ((u64)ret_payload[2] << 32) | ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_getrate);

/**
 * zynqmp_pm_clock_setparent() - Set the clock parent for given id
 * @clock_id:	ID of the clock
 * @parent_id:	parent id
 *
 * This function is used by master to set parent for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_setparent(u32 clock_id, u32 parent_id)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_SETPARENT, clock_id,
				   parent_id, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_setparent);

/**
 * zynqmp_pm_clock_getparent() - Get the clock parent for given id
 * @clock_id:	ID of the clock
 * @parent_id:	parent id
 *
 * This function is used by master to get parent index
 * for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_getparent(u32 clock_id, u32 *parent_id)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETPARENT, clock_id, 0,
				  0, 0, ret_payload);
	*parent_id = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_getparent);

/**
 * zynqmp_pm_set_pll_frac_mode() - PM API for set PLL mode
 *
 * @clk_id:	PLL clock ID
 * @mode:	PLL mode (PLL_MODE_FRAC/PLL_MODE_INT)
 *
 * This function sets PLL mode
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_set_pll_frac_mode(u32 clk_id, u32 mode)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, 0, IOCTL_SET_PLL_FRAC_MODE,
				   clk_id, mode, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_pll_frac_mode);

/**
 * zynqmp_pm_get_pll_frac_mode() - PM API for get PLL mode
 *
 * @clk_id:	PLL clock ID
 * @mode:	PLL mode
 *
 * This function return current PLL mode
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_pll_frac_mode(u32 clk_id, u32 *mode)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, 0, IOCTL_GET_PLL_FRAC_MODE,
				   clk_id, 0, mode);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_pll_frac_mode);

/**
 * zynqmp_pm_set_pll_frac_data() - PM API for setting pll fraction data
 *
 * @clk_id:	PLL clock ID
 * @data:	fraction data
 *
 * This function sets fraction data.
 * It is valid for fraction mode only.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_set_pll_frac_data(u32 clk_id, u32 data)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, 0, IOCTL_SET_PLL_FRAC_DATA,
				   clk_id, data, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_pll_frac_data);

/**
 * zynqmp_pm_get_pll_frac_data() - PM API for getting pll fraction data
 *
 * @clk_id:	PLL clock ID
 * @data:	fraction data
 *
 * This function returns fraction data value.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_pll_frac_data(u32 clk_id, u32 *data)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, 0, IOCTL_GET_PLL_FRAC_DATA,
				   clk_id, 0, data);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_pll_frac_data);

/**
 * zynqmp_pm_set_sd_tapdelay() -  Set tap delay for the SD device
 *
 * @node_id	Node ID of the device
 * @type	Type of tap delay to set (input/output)
 * @value	Value to set fot the tap delay
 *
 * This function sets input/output tap delay for the SD device.
 *
 * @return	Returns status, either success or error+reason
 */
int zynqmp_pm_set_sd_tapdelay(u32 node_id, u32 type, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, node_id, IOCTL_SET_SD_TAPDELAY,
				   type, value, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_sd_tapdelay);

/**
 * zynqmp_pm_sd_dll_reset() - Reset DLL logic
 *
 * @node_id	Node ID of the device
 * @type	Reset type
 *
 * This function resets DLL logic for the SD device.
 *
 * @return	Returns status, either success or error+reason
 */
int zynqmp_pm_sd_dll_reset(u32 node_id, u32 type)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, node_id, IOCTL_SET_SD_TAPDELAY,
				   type, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_sd_dll_reset);

/**
 * zynqmp_pm_write_ggs() - PM API for writing global general storage (ggs)
 * @index	GGS register index
 * @value	Register value to be written
 *
 * This function writes value to GGS register.
 *
 * @return      Returns status, either success or error+reason
 */
int zynqmp_pm_write_ggs(u32 index, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, 0, IOCTL_WRITE_GGS,
				   index, value, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_write_ggs);

/**
 * zynqmp_pm_write_ggs() - PM API for reading global general storage (ggs)
 * @index	GGS register index
 * @value	Register value to be written
 *
 * This function returns GGS register value.
 *
 * @return      Returns status, either success or error+reason
 */
int zynqmp_pm_read_ggs(u32 index, u32 *value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, 0, IOCTL_READ_GGS,
				   index, 0, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_read_ggs);

/**
 * zynqmp_pm_write_pggs() - PM API for writing persistent global general
 *			     storage (pggs)
 * @index	PGGS register index
 * @value	Register value to be written
 *
 * This function writes value to PGGS register.
 *
 * @return      Returns status, either success or error+reason
 */
int zynqmp_pm_write_pggs(u32 index, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, 0, IOCTL_WRITE_PGGS, index, value,
				   NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_write_pggs);

/**
 * zynqmp_pm_write_pggs() - PM API for reading persistent global general
 *			     storage (pggs)
 * @index	PGGS register index
 * @value	Register value to be written
 *
 * This function returns PGGS register value.
 *
 * @return      Returns status, either success or error+reason
 */
int zynqmp_pm_read_pggs(u32 index, u32 *value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, 0, IOCTL_READ_PGGS, index, 0,
				   value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_read_pggs);

/**
 * zynqmp_pm_set_boot_health_status() - PM API for setting healthy boot status
 * @value	Status value to be written
 *
 * This function sets healthy bit value to indicate boot health status
 * to firmware.
 *
 * @return      Returns status, either success or error+reason
 */
int zynqmp_pm_set_boot_health_status(u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, 0, IOCTL_SET_BOOT_HEALTH_STATUS,
				   value, 0, NULL);
}

/**
 * zynqmp_pm_reset_assert - Request setting of reset (1 - assert, 0 - release)
 * @reset:		Reset to be configured
 * @assert_flag:	Flag stating should reset be asserted (1) or
 *			released (0)
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_reset_assert(const enum zynqmp_pm_reset reset,
			   const enum zynqmp_pm_reset_action assert_flag)
{
	return zynqmp_pm_invoke_fn(PM_RESET_ASSERT, reset, assert_flag,
				   0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_reset_assert);

/**
 * zynqmp_pm_reset_get_status - Get status of the reset
 * @reset:      Reset whose status should be returned
 * @status:     Returned status
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_reset_get_status(const enum zynqmp_pm_reset reset, u32 *status)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!status)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_RESET_GET_STATUS, reset, 0,
				  0, 0, ret_payload);
	*status = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_reset_get_status);

/**
 * zynqmp_pm_fpga_load - Perform the fpga load
 * @address:	Address to write to
 * @size:	pl bitstream size
 * @flags:	Bitstream type
 *	-XILINX_ZYNQMP_PM_FPGA_FULL:  FPGA full reconfiguration
 *	-XILINX_ZYNQMP_PM_FPGA_PARTIAL: FPGA partial reconfiguration
 *
 * This function provides access to pmufw. To transfer
 * the required bitstream into PL.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_fpga_load(const u64 address, const u32 size, const u32 flags)
{
	return zynqmp_pm_invoke_fn(PM_FPGA_LOAD, lower_32_bits(address),
				   upper_32_bits(address), size, flags, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_load);

/**
 * zynqmp_pm_fpga_get_status - Read value from PCAP status register
 * @value: Value to read
 *
 * This function provides access to the pmufw to get the PCAP
 * status
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_fpga_get_status(u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_FPGA_GET_STATUS, 0, 0, 0, 0, ret_payload);
	*value = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_get_status);

/**
 * zynqmp_pm_init_finalize() - PM call to inform firmware that the caller
 *			       master has initialized its own power management
 *
 * This API function is to be used for notify the power management controller
 * about the completed power management initialization.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_init_finalize(void)
{
	return zynqmp_pm_invoke_fn(PM_PM_INIT_FINALIZE, 0, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_init_finalize);

/**
 * zynqmp_pm_set_suspend_mode()	- Set system suspend mode
 * @mode:	Mode to set for system suspend
 *
 * This API function is used to set mode of system suspend.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_set_suspend_mode(u32 mode)
{
	return zynqmp_pm_invoke_fn(PM_SET_SUSPEND_MODE, mode, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_suspend_mode);

/**
 * zynqmp_pm_request_node() - Request a node with specific capabilities
 * @node:		Node ID of the slave
 * @capabilities:	Requested capabilities of the slave
 * @qos:		Quality of service (not supported)
 * @ack:		Flag to specify whether acknowledge is requested
 *
 * This function is used by master to request particular node from firmware.
 * Every master must request node before using it.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_request_node(const u32 node, const u32 capabilities,
			   const u32 qos, const enum zynqmp_pm_request_ack ack)
{
	return zynqmp_pm_invoke_fn(PM_REQUEST_NODE, node, capabilities,
				   qos, ack, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_request_node);

/**
 * zynqmp_pm_release_node() - Release a node
 * @node:	Node ID of the slave
 *
 * This function is used by master to inform firmware that master
 * has released node. Once released, master must not use that node
 * without re-request.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_release_node(const u32 node)
{
	return zynqmp_pm_invoke_fn(PM_RELEASE_NODE, node, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_release_node);

/**
 * zynqmp_pm_set_requirement() - PM call to set requirement for PM slaves
 * @node:		Node ID of the slave
 * @capabilities:	Requested capabilities of the slave
 * @qos:		Quality of service (not supported)
 * @ack:		Flag to specify whether acknowledge is requested
 *
 * This API function is to be used for slaves a PU already has requested
 * to change its capabilities.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_set_requirement(const u32 node, const u32 capabilities,
			      const u32 qos,
			      const enum zynqmp_pm_request_ack ack)
{
	return zynqmp_pm_invoke_fn(PM_SET_REQUIREMENT, node, capabilities,
				   qos, ack, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_requirement);

/**
 * zynqmp_pm_aes - Access AES hardware to encrypt/decrypt the data using
 * AES-GCM core.
 * @address:	Address of the AesParams structure.
 * @out:	Returned output value
 *
 * Return:	Returns status, either success or error code.
 */
int zynqmp_pm_aes_engine(const u64 address, u32 *out)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!out)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_SECURE_AES, upper_32_bits(address),
				  lower_32_bits(address),
				  0, 0, ret_payload);
	*out = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_aes_engine);

/**
 * zynqmp_pm_system_shutdown - PM call to request a system shutdown or restart
 * @type:	Shutdown or restart? 0 for shutdown, 1 for restart
 * @subtype:	Specifies which system should be restarted or shut down
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype)
{
	return zynqmp_pm_invoke_fn(PM_SYSTEM_SHUTDOWN, type, subtype,
				   0, 0, NULL);
}

/**
 * struct zynqmp_pm_shutdown_scope - Struct for shutdown scope
 * @subtype:	Shutdown subtype
 * @name:	Matching string for scope argument
 *
 * This struct encapsulates mapping between shutdown scope ID and string.
 */
struct zynqmp_pm_shutdown_scope {
	const enum zynqmp_pm_shutdown_subtype subtype;
	const char *name;
};

static struct zynqmp_pm_shutdown_scope shutdown_scopes[] = {
	[ZYNQMP_PM_SHUTDOWN_SUBTYPE_SUBSYSTEM] = {
		.subtype = ZYNQMP_PM_SHUTDOWN_SUBTYPE_SUBSYSTEM,
		.name = "subsystem",
	},
	[ZYNQMP_PM_SHUTDOWN_SUBTYPE_PS_ONLY] = {
		.subtype = ZYNQMP_PM_SHUTDOWN_SUBTYPE_PS_ONLY,
		.name = "ps_only",
	},
	[ZYNQMP_PM_SHUTDOWN_SUBTYPE_SYSTEM] = {
		.subtype = ZYNQMP_PM_SHUTDOWN_SUBTYPE_SYSTEM,
		.name = "system",
	},
};

static struct zynqmp_pm_shutdown_scope *selected_scope =
		&shutdown_scopes[ZYNQMP_PM_SHUTDOWN_SUBTYPE_SYSTEM];

/**
 * zynqmp_pm_is_shutdown_scope_valid - Check if shutdown scope string is valid
 * @scope_string:	Shutdown scope string
 *
 * Return:		Return pointer to matching shutdown scope struct from
 *			array of available options in system if string is valid,
 *			otherwise returns NULL.
 */
static struct zynqmp_pm_shutdown_scope*
		zynqmp_pm_is_shutdown_scope_valid(const char *scope_string)
{
	int count;

	for (count = 0; count < ARRAY_SIZE(shutdown_scopes); count++)
		if (sysfs_streq(scope_string, shutdown_scopes[count].name))
			return &shutdown_scopes[count];

	return NULL;
}

static ssize_t shutdown_scope_show(struct device *device,
				   struct device_attribute *attr,
				   char *buf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(shutdown_scopes); i++) {
		if (&shutdown_scopes[i] == selected_scope) {
			strcat(buf, "[");
			strcat(buf, shutdown_scopes[i].name);
			strcat(buf, "]");
		} else {
			strcat(buf, shutdown_scopes[i].name);
		}
		strcat(buf, " ");
	}
	strcat(buf, "\n");

	return strlen(buf);
}

static ssize_t shutdown_scope_store(struct device *device,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int ret;
	struct zynqmp_pm_shutdown_scope *scope;

	scope = zynqmp_pm_is_shutdown_scope_valid(buf);
	if (!scope)
		return -EINVAL;

	ret = zynqmp_pm_system_shutdown(ZYNQMP_PM_SHUTDOWN_TYPE_SETSCOPE_ONLY,
					scope->subtype);
	if (ret) {
		pr_err("unable to set shutdown scope %s\n", buf);
		return ret;
	}

	selected_scope = scope;

	return count;
}

static DEVICE_ATTR_RW(shutdown_scope);

static ssize_t health_status_store(struct device *device,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret;
	unsigned int value;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;

	ret = zynqmp_pm_set_boot_health_status(value);
	if (ret) {
		dev_err(device, "unable to set healthy bit value to %u\n",
			value);
		return ret;
	}

	return count;
}

static DEVICE_ATTR_WO(health_status);

static ssize_t ggs_show(struct device *device,
			struct device_attribute *attr,
			char *buf,
			u32 reg)
{
	int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];

	ret = zynqmp_pm_read_ggs(reg, ret_payload);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", ret_payload[1]);
}

static ssize_t ggs_store(struct device *device,
			 struct device_attribute *attr,
			 const char *buf, size_t count,
			 u32 reg)
{
	long value;
	int ret;

	if (reg >= GSS_NUM_REGS)
		return -EINVAL;

	ret = kstrtol(buf, 16, &value);
	if (ret) {
		count = -EFAULT;
		goto err;
	}

	ret = zynqmp_pm_write_ggs(reg, value);
	if (ret)
		count = -EFAULT;
err:
	return count;
}

/* GGS register show functions */
#define GGS0_SHOW(N)						\
	ssize_t ggs##N##_show(struct device *device,		\
			      struct device_attribute *attr,	\
			      char *buf)			\
	{							\
		return ggs_show(device, attr, buf, N);		\
	}

static GGS0_SHOW(0);
static GGS0_SHOW(1);
static GGS0_SHOW(2);
static GGS0_SHOW(3);

/* GGS register store function */
#define GGS0_STORE(N)						\
	ssize_t ggs##N##_store(struct device *device,		\
			       struct device_attribute *attr,	\
			       const char *buf,			\
			       size_t count)			\
	{							\
		return ggs_store(device, attr, buf, count, N);	\
	}

static GGS0_STORE(0);
static GGS0_STORE(1);
static GGS0_STORE(2);
static GGS0_STORE(3);

static ssize_t pggs_show(struct device *device,
			 struct device_attribute *attr,
			 char *buf,
			 u32 reg)
{
	int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];

	ret = zynqmp_pm_read_pggs(reg, ret_payload);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", ret_payload[1]);
}

static ssize_t pggs_store(struct device *device,
			  struct device_attribute *attr,
			  const char *buf, size_t count,
			  u32 reg)
{
	long value;
	int ret;

	if (reg >= GSS_NUM_REGS)
		return -EINVAL;

	ret = kstrtol(buf, 16, &value);
	if (ret) {
		count = -EFAULT;
		goto err;
	}

	ret = zynqmp_pm_write_pggs(reg, value);
	if (ret)
		count = -EFAULT;

err:
	return count;
}

#define PGGS0_SHOW(N)						\
	ssize_t pggs##N##_show(struct device *device,		\
			       struct device_attribute *attr,	\
			       char *buf)			\
	{							\
		return pggs_show(device, attr, buf, N);		\
	}

#define PGGS0_STORE(N)						\
	ssize_t pggs##N##_store(struct device *device,		\
				struct device_attribute *attr,	\
				const char *buf,		\
				size_t count)			\
	{							\
		return pggs_store(device, attr, buf, count, N);	\
	}

/* PGGS register show functions */
static PGGS0_SHOW(0);
static PGGS0_SHOW(1);
static PGGS0_SHOW(2);
static PGGS0_SHOW(3);

/* PGGS register store functions */
static PGGS0_STORE(0);
static PGGS0_STORE(1);
static PGGS0_STORE(2);
static PGGS0_STORE(3);

/* GGS register attributes */
static DEVICE_ATTR_RW(ggs0);
static DEVICE_ATTR_RW(ggs1);
static DEVICE_ATTR_RW(ggs2);
static DEVICE_ATTR_RW(ggs3);

/* PGGS register attributes */
static DEVICE_ATTR_RW(pggs0);
static DEVICE_ATTR_RW(pggs1);
static DEVICE_ATTR_RW(pggs2);
static DEVICE_ATTR_RW(pggs3);

static struct attribute *zynqmp_firmware_attrs[] = {
	&dev_attr_ggs0.attr,
	&dev_attr_ggs1.attr,
	&dev_attr_ggs2.attr,
	&dev_attr_ggs3.attr,
	&dev_attr_pggs0.attr,
	&dev_attr_pggs1.attr,
	&dev_attr_pggs2.attr,
	&dev_attr_pggs3.attr,
	&dev_attr_shutdown_scope.attr,
	&dev_attr_health_status.attr,
	NULL,
};

ATTRIBUTE_GROUPS(zynqmp_firmware);

static int zynqmp_firmware_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynqmp");
	if (!np) {
		np = of_find_compatible_node(NULL, NULL, "xlnx,versal");
		if (!np)
			return 0;

		feature_check_enabled = true;
	}
	of_node_put(np);

	ret = get_set_conduit_method(dev->of_node);
	if (ret)
		return ret;

	/* Check PM API version number */
	zynqmp_pm_get_api_version(&pm_api_version);
	if (pm_api_version < ZYNQMP_PM_VERSION) {
		panic("%s Platform Management API version error. Expected: v%d.%d - Found: v%d.%d\n",
		      __func__,
		      ZYNQMP_PM_VERSION_MAJOR, ZYNQMP_PM_VERSION_MINOR,
		      pm_api_version >> 16, pm_api_version & 0xFFFF);
	}

	pr_info("%s Platform Management API v%d.%d\n", __func__,
		pm_api_version >> 16, pm_api_version & 0xFFFF);

	/* Check trustzone version number */
	ret = zynqmp_pm_get_trustzone_version(&pm_tz_version);
	if (ret)
		panic("Legacy trustzone found without version support\n");

	if (pm_tz_version < ZYNQMP_TZ_VERSION)
		panic("%s Trustzone version error. Expected: v%d.%d - Found: v%d.%d\n",
		      __func__,
		      ZYNQMP_TZ_VERSION_MAJOR, ZYNQMP_TZ_VERSION_MINOR,
		      pm_tz_version >> 16, pm_tz_version & 0xFFFF);

	pr_info("%s Trustzone version v%d.%d\n", __func__,
		pm_tz_version >> 16, pm_tz_version & 0xFFFF);

	ret = mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE, firmware_devs,
			      ARRAY_SIZE(firmware_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to add MFD devices %d\n", ret);
		return ret;
	}

	zynqmp_pm_api_debugfs_init();

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
}

static int zynqmp_firmware_remove(struct platform_device *pdev)
{
	mfd_remove_devices(&pdev->dev);
	zynqmp_pm_api_debugfs_exit();

	return 0;
}

static const struct of_device_id zynqmp_firmware_of_match[] = {
	{.compatible = "xlnx,zynqmp-firmware"},
	{.compatible = "xlnx,versal-firmware"},
	{},
};
MODULE_DEVICE_TABLE(of, zynqmp_firmware_of_match);

static struct platform_driver zynqmp_firmware_driver = {
	.driver = {
		.name = "zynqmp_firmware",
		.of_match_table = zynqmp_firmware_of_match,
		.dev_groups = zynqmp_firmware_groups,
	},
	.probe = zynqmp_firmware_probe,
	.remove = zynqmp_firmware_remove,
};
module_platform_driver(zynqmp_firmware_driver);
