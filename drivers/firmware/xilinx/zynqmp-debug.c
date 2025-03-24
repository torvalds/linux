// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq MPSoC Firmware layer for debugfs APIs
 *
 *  Copyright (C) 2014-2018 Xilinx, Inc.
 *
 *  Michal Simek <michal.simek@amd.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include <linux/firmware/xlnx-zynqmp.h>
#include "zynqmp-debug.h"

#define PM_API_NAME_LEN			50

struct pm_api_info {
	u32 api_id;
	char api_name[PM_API_NAME_LEN];
	char api_name_len;
};

static char debugfs_buf[PAGE_SIZE];

#define PM_API(id)		 {id, #id, strlen(#id)}
static struct pm_api_info pm_api_list[] = {
	PM_API(PM_FORCE_POWERDOWN),
	PM_API(PM_REQUEST_WAKEUP),
	PM_API(PM_SYSTEM_SHUTDOWN),
	PM_API(PM_REQUEST_NODE),
	PM_API(PM_RELEASE_NODE),
	PM_API(PM_SET_REQUIREMENT),
	PM_API(PM_GET_API_VERSION),
	PM_API(PM_REGISTER_NOTIFIER),
	PM_API(PM_RESET_ASSERT),
	PM_API(PM_RESET_GET_STATUS),
	PM_API(PM_GET_CHIPID),
	PM_API(PM_PINCTRL_SET_FUNCTION),
	PM_API(PM_PINCTRL_CONFIG_PARAM_GET),
	PM_API(PM_PINCTRL_CONFIG_PARAM_SET),
	PM_API(PM_IOCTL),
	PM_API(PM_CLOCK_ENABLE),
	PM_API(PM_CLOCK_DISABLE),
	PM_API(PM_CLOCK_GETSTATE),
	PM_API(PM_CLOCK_SETDIVIDER),
	PM_API(PM_CLOCK_GETDIVIDER),
	PM_API(PM_CLOCK_SETPARENT),
	PM_API(PM_CLOCK_GETPARENT),
	PM_API(PM_QUERY_DATA),
};

static struct dentry *firmware_debugfs_root;

/**
 * zynqmp_pm_ioctl - PM IOCTL for device control and configs
 * @node:	Node ID of the device
 * @ioctl:	ID of the requested IOCTL
 * @arg1:	Argument 1 of requested IOCTL call
 * @arg2:	Argument 2 of requested IOCTL call
 * @arg3:	Argument 3 of requested IOCTL call
 * @out:	Returned output value
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_ioctl(const u32 node, const u32 ioctl, const u32 arg1,
			   const u32 arg2, const u32 arg3, u32 *out)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, out, 5, node, ioctl, arg1, arg2, arg3);
}

/**
 * zynqmp_pm_argument_value() - Extract argument value from a PM-API request
 * @arg:	Entered PM-API argument in string format
 *
 * Return: Argument value in unsigned integer format on success
 *	   0 otherwise
 */
static u64 zynqmp_pm_argument_value(char *arg)
{
	u64 value;

	if (!arg)
		return 0;

	if (!kstrtou64(arg, 0, &value))
		return value;

	return 0;
}

/**
 * get_pm_api_id() - Extract API-ID from a PM-API request
 * @pm_api_req:		Entered PM-API argument in string format
 * @pm_id:		API-ID
 *
 * Return: 0 on success else error code
 */
static int get_pm_api_id(char *pm_api_req, u32 *pm_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pm_api_list) ; i++) {
		if (!strncasecmp(pm_api_req, pm_api_list[i].api_name,
				 pm_api_list[i].api_name_len)) {
			*pm_id = pm_api_list[i].api_id;
			break;
		}
	}

	/* If no name was entered look for PM-API ID instead */
	if (i == ARRAY_SIZE(pm_api_list) && kstrtouint(pm_api_req, 10, pm_id))
		return -EINVAL;

	return 0;
}

static int process_api_request(u32 pm_id, u64 *pm_api_arg, u32 *pm_api_ret)
{
	u32 pm_api_version;
	int ret;
	struct zynqmp_pm_query_data qdata = {0};

	switch (pm_id) {
	case PM_GET_API_VERSION:
		ret = zynqmp_pm_get_api_version(&pm_api_version);
		sprintf(debugfs_buf, "PM-API Version = %d.%d\n",
			pm_api_version >> 16, pm_api_version & 0xffff);
		break;
	case PM_FORCE_POWERDOWN:
		ret = zynqmp_pm_force_pwrdwn(pm_api_arg[0],
					     pm_api_arg[1] ? pm_api_arg[1] :
					     ZYNQMP_PM_REQUEST_ACK_NO);
		break;
	case PM_REQUEST_WAKEUP:
		ret = zynqmp_pm_request_wake(pm_api_arg[0],
					     pm_api_arg[1], pm_api_arg[2],
					     pm_api_arg[3] ? pm_api_arg[3] :
					     ZYNQMP_PM_REQUEST_ACK_NO);
		break;
	case PM_SYSTEM_SHUTDOWN:
		ret = zynqmp_pm_system_shutdown(pm_api_arg[0], pm_api_arg[1]);
		break;
	case PM_REQUEST_NODE:
		ret = zynqmp_pm_request_node(pm_api_arg[0],
					     pm_api_arg[1] ? pm_api_arg[1] :
					     ZYNQMP_PM_CAPABILITY_ACCESS,
					     pm_api_arg[2] ? pm_api_arg[2] : 0,
					     pm_api_arg[3] ? pm_api_arg[3] :
					     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		break;
	case PM_RELEASE_NODE:
		ret = zynqmp_pm_release_node(pm_api_arg[0]);
		break;
	case PM_SET_REQUIREMENT:
		ret = zynqmp_pm_set_requirement(pm_api_arg[0],
						pm_api_arg[1] ? pm_api_arg[1] :
						ZYNQMP_PM_CAPABILITY_CONTEXT,
						pm_api_arg[2] ?
						pm_api_arg[2] : 0,
						pm_api_arg[3] ? pm_api_arg[3] :
						ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		break;
	case PM_REGISTER_NOTIFIER:
		ret = zynqmp_pm_register_notifier(pm_api_arg[0],
						  pm_api_arg[1] ?
						  pm_api_arg[1] : 0,
						  pm_api_arg[2] ?
						  pm_api_arg[2] : 0,
						  pm_api_arg[3] ?
						  pm_api_arg[3] : 0);
		break;
	case PM_RESET_ASSERT:
		ret = zynqmp_pm_reset_assert(pm_api_arg[0], pm_api_arg[1]);
		break;
	case PM_RESET_GET_STATUS:
		ret = zynqmp_pm_reset_get_status(pm_api_arg[0], &pm_api_ret[0]);
		if (!ret)
			sprintf(debugfs_buf, "Reset status: %u\n",
				pm_api_ret[0]);
		break;
	case PM_GET_CHIPID:
		ret = zynqmp_pm_get_chipid(&pm_api_ret[0], &pm_api_ret[1]);
		if (!ret)
			sprintf(debugfs_buf, "Idcode: %#x, Version:%#x\n",
				pm_api_ret[0], pm_api_ret[1]);
		break;
	case PM_PINCTRL_SET_FUNCTION:
		ret = zynqmp_pm_pinctrl_set_function(pm_api_arg[0],
						     pm_api_arg[1]);
		break;
	case PM_PINCTRL_CONFIG_PARAM_GET:
		ret = zynqmp_pm_pinctrl_get_config(pm_api_arg[0], pm_api_arg[1],
						   &pm_api_ret[0]);
		if (!ret)
			sprintf(debugfs_buf,
				"Pin: %llu, Param: %llu, Value: %u\n",
				pm_api_arg[0], pm_api_arg[1],
				pm_api_ret[0]);
		break;
	case PM_PINCTRL_CONFIG_PARAM_SET:
		ret = zynqmp_pm_pinctrl_set_config(pm_api_arg[0],
						   pm_api_arg[1],
						   pm_api_arg[2]);
		break;
	case PM_IOCTL:
		ret = zynqmp_pm_ioctl(pm_api_arg[0], pm_api_arg[1],
				      pm_api_arg[2], pm_api_arg[3],
				      pm_api_arg[4], &pm_api_ret[0]);
		if (!ret && (pm_api_arg[1] == IOCTL_GET_RPU_OPER_MODE ||
			     pm_api_arg[1] == IOCTL_GET_PLL_FRAC_MODE ||
			     pm_api_arg[1] == IOCTL_GET_PLL_FRAC_DATA ||
			     pm_api_arg[1] == IOCTL_READ_GGS ||
			     pm_api_arg[1] == IOCTL_READ_PGGS ||
			     pm_api_arg[1] == IOCTL_READ_REG))
			sprintf(debugfs_buf, "IOCTL return value: %u\n",
				pm_api_ret[1]);
		if (!ret && pm_api_arg[1] == IOCTL_GET_QOS)
			sprintf(debugfs_buf, "Default QoS: %u\nCurrent QoS: %u\n",
				pm_api_ret[1], pm_api_ret[2]);
		break;
	case PM_CLOCK_ENABLE:
		ret = zynqmp_pm_clock_enable(pm_api_arg[0]);
		break;
	case PM_CLOCK_DISABLE:
		ret = zynqmp_pm_clock_disable(pm_api_arg[0]);
		break;
	case PM_CLOCK_GETSTATE:
		ret = zynqmp_pm_clock_getstate(pm_api_arg[0], &pm_api_ret[0]);
		if (!ret)
			sprintf(debugfs_buf, "Clock state: %u\n",
				pm_api_ret[0]);
		break;
	case PM_CLOCK_SETDIVIDER:
		ret = zynqmp_pm_clock_setdivider(pm_api_arg[0], pm_api_arg[1]);
		break;
	case PM_CLOCK_GETDIVIDER:
		ret = zynqmp_pm_clock_getdivider(pm_api_arg[0], &pm_api_ret[0]);
		if (!ret)
			sprintf(debugfs_buf, "Divider Value: %d\n",
				pm_api_ret[0]);
		break;
	case PM_CLOCK_SETPARENT:
		ret = zynqmp_pm_clock_setparent(pm_api_arg[0], pm_api_arg[1]);
		break;
	case PM_CLOCK_GETPARENT:
		ret = zynqmp_pm_clock_getparent(pm_api_arg[0], &pm_api_ret[0]);
		if (!ret)
			sprintf(debugfs_buf,
				"Clock parent Index: %u\n", pm_api_ret[0]);
		break;
	case PM_QUERY_DATA:
		qdata.qid = pm_api_arg[0];
		qdata.arg1 = pm_api_arg[1];
		qdata.arg2 = pm_api_arg[2];
		qdata.arg3 = pm_api_arg[3];

		ret = zynqmp_pm_query_data(qdata, pm_api_ret);
		if (ret)
			break;

		switch (qdata.qid) {
		case PM_QID_CLOCK_GET_NAME:
			sprintf(debugfs_buf, "Clock name = %s\n",
				(char *)pm_api_ret);
			break;
		case PM_QID_CLOCK_GET_FIXEDFACTOR_PARAMS:
			sprintf(debugfs_buf, "Multiplier = %d, Divider = %d\n",
				pm_api_ret[1], pm_api_ret[2]);
			break;
		default:
			sprintf(debugfs_buf,
				"data[0] = 0x%08x\ndata[1] = 0x%08x\n data[2] = 0x%08x\ndata[3] = 0x%08x\n",
				pm_api_ret[0], pm_api_ret[1],
				pm_api_ret[2], pm_api_ret[3]);
		}
		break;
	default:
		sprintf(debugfs_buf, "Unsupported PM-API request\n");
		ret = -EINVAL;
	}

	return ret;
}

/**
 * zynqmp_pm_debugfs_api_write() - debugfs write function
 * @file:	User file
 * @ptr:	User entered PM-API string
 * @len:	Length of the userspace buffer
 * @off:	Offset within the file
 *
 * Used for triggering pm api functions by writing
 * echo <pm_api_id>	> /sys/kernel/debug/zynqmp_pm/power or
 * echo <pm_api_name>	> /sys/kernel/debug/zynqmp_pm/power
 *
 * Return: Number of bytes copied if PM-API request succeeds,
 *	   the corresponding error code otherwise
 */
static ssize_t zynqmp_pm_debugfs_api_write(struct file *file,
					   const char __user *ptr, size_t len,
					   loff_t *off)
{
	char *kern_buff, *tmp_buff;
	char *pm_api_req;
	u32 pm_id = 0;
	u64 pm_api_arg[5] = {0, 0, 0, 0, 0};
	/* Return values from PM APIs calls */
	u32 pm_api_ret[4] = {0, 0, 0, 0};

	int ret;
	int i = 0;

	strcpy(debugfs_buf, "");

	if (*off != 0 || len <= 1 || len > PAGE_SIZE - 1)
		return -EINVAL;

	kern_buff = memdup_user_nul(ptr, len);
	if (IS_ERR(kern_buff))
		return PTR_ERR(kern_buff);
	tmp_buff = kern_buff;

	/* Read the API name from a user request */
	pm_api_req = strsep(&kern_buff, " ");

	ret = get_pm_api_id(pm_api_req, &pm_id);
	if (ret < 0)
		goto err;

	/* Read node_id and arguments from the PM-API request */
	pm_api_req = strsep(&kern_buff, " ");
	while ((i < ARRAY_SIZE(pm_api_arg)) && pm_api_req) {
		pm_api_arg[i++] = zynqmp_pm_argument_value(pm_api_req);
		pm_api_req = strsep(&kern_buff, " ");
	}

	ret = process_api_request(pm_id, pm_api_arg, pm_api_ret);

err:
	kfree(tmp_buff);
	if (ret)
		return ret;

	return len;
}

/**
 * zynqmp_pm_debugfs_api_read() - debugfs read function
 * @file:	User file
 * @ptr:	Requested pm_api_version string
 * @len:	Length of the userspace buffer
 * @off:	Offset within the file
 *
 * Return: Length of the version string on success
 *	   else error code
 */
static ssize_t zynqmp_pm_debugfs_api_read(struct file *file, char __user *ptr,
					  size_t len, loff_t *off)
{
	return simple_read_from_buffer(ptr, len, off, debugfs_buf,
				       strlen(debugfs_buf));
}

/* Setup debugfs fops */
static const struct file_operations fops_zynqmp_pm_dbgfs = {
	.owner = THIS_MODULE,
	.write = zynqmp_pm_debugfs_api_write,
	.read = zynqmp_pm_debugfs_api_read,
};

/**
 * zynqmp_pm_api_debugfs_init - Initialize debugfs interface
 *
 * Return:	None
 */
void zynqmp_pm_api_debugfs_init(void)
{
	/* Initialize debugfs interface */
	firmware_debugfs_root = debugfs_create_dir("zynqmp-firmware", NULL);
	debugfs_create_file("pm", 0660, firmware_debugfs_root, NULL,
			    &fops_zynqmp_pm_dbgfs);
}

/**
 * zynqmp_pm_api_debugfs_exit - Remove debugfs interface
 *
 * Return:	None
 */
void zynqmp_pm_api_debugfs_exit(void)
{
	debugfs_remove_recursive(firmware_debugfs_root);
}
