/*
 * Copyright (c) 2011 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/rmi.h>
#include <linux/input.h>
#include <linux/slab.h>
#include "rmi_driver.h"
#define QUERY_BASE_INDEX 1

/* data specific to fn $09 that needs to be kept around */
struct f09_query {
	u8 limit_register_count;
	union {
		struct {
			u8 result_register_count:3;
			u8 reserved:3;
			u8 internal_limits:1;
			u8 host_test_enable:1;
		};
		u8 f09_bist_query1;
	};
};

struct f09_control {
	union {
		struct {
			u8 test1_limit_low:8;
			u8 test1_limit_high:8;
			u8 test1_limit_diff:8;
		};
		u8 f09_control_test1[3];
	};
	union {
		struct {
			u8 test2_limit_low:8;
			u8 test2_limit_high:8;
			u8 test2_limit_diff:8;
		};
		u8 f09_control_test2[3];
	};
};

struct f09_data {
	u8 test_number_control;
	u8 overall_bist_result;
	u8 test_result1;
	u8 test_result2;
	u8 transmitter_number;

	union {
		struct {
			u8 receiver_number:6;
			u8 limit_failure_code:2;
		};
		u8 f09_bist_data2;
	};
};

struct f09_cmd {
	union {
		struct {
			u8 run_bist:1;
		};
		u8 f09_bist_cmd0;
	};
};

struct rmi_fn_09_data {
	struct f09_query query;
	struct f09_data data;
	struct f09_cmd cmd;
	struct f09_control control;
	signed char status;
};


static ssize_t rmi_f09_status_show(struct device *dev,
				     struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_status_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);

static ssize_t rmi_f09_limit_register_count_show(struct device *dev,
				struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_host_test_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_host_test_enable_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);

static ssize_t rmi_f09_internal_limits_show(struct device *dev,
				struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_result_register_count_show(struct device *dev,
				struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_overall_bist_result_show(struct device *dev,
				      struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_test_number_control_show(struct device *dev,
				      struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_test_number_control_store(struct device *dev,
				      struct device_attribute *attr,
					const char *buf, size_t count);

static ssize_t rmi_f09_run_bist_show(struct device *dev,
				      struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_run_bist_store(struct device *dev,
				      struct device_attribute *attr,
					const char *buf, size_t count);

static ssize_t rmi_f09_test_result1_show(struct device *dev,
				      struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_test_result2_show(struct device *dev,
				      struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_control_test1_show(struct device *dev,
				      struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_control_test1_store(struct device *dev,
				      struct device_attribute *attr,
					const char *buf, size_t count);

static ssize_t rmi_f09_control_test2_show(struct device *dev,
				      struct device_attribute *attr, char *buf);

static ssize_t rmi_f09_control_test2_store(struct device *dev,
				      struct device_attribute *attr,
					const char *buf, size_t count);

static int rmi_f09_alloc_memory(struct rmi_function_container *fc);

static void rmi_f09_free_memory(struct rmi_function_container *fc);

static int rmi_f09_initialize(struct rmi_function_container *fc);

static int rmi_f09_config(struct rmi_function_container *fc);

static int rmi_f09_reset(struct rmi_function_container *fc);

static int rmi_f09_create_sysfs(struct rmi_function_container *fc);


static struct device_attribute attrs[] = {
	__ATTR(status, RMI_RW_ATTR,
		   rmi_f09_status_show, rmi_f09_status_store),
	__ATTR(limitRegisterCount, RMI_RO_ATTR,
	       rmi_f09_limit_register_count_show, rmi_store_error),
	__ATTR(hostTestEnable, RMI_RW_ATTR,
	       rmi_f09_host_test_enable_show, rmi_f09_host_test_enable_store),
	__ATTR(internalLimits, RMI_RO_ATTR,
	       rmi_f09_internal_limits_show, rmi_store_error),
	__ATTR(resultRegisterCount, RMI_RO_ATTR,
	       rmi_f09_result_register_count_show, rmi_store_error),
	__ATTR(overall_bist_result, RMI_RO_ATTR,
	       rmi_f09_overall_bist_result_show, rmi_store_error),
	__ATTR(test_number_control, RMI_RW_ATTR,
	       rmi_f09_test_number_control_show,
	       rmi_f09_test_number_control_store),
	__ATTR(test_result1, RMI_RO_ATTR,
	       rmi_f09_test_result1_show, rmi_store_error),
	__ATTR(test_result2, RMI_RO_ATTR,
	       rmi_f09_test_result2_show, rmi_store_error),
	__ATTR(run_bist, RMI_RW_ATTR,
	       rmi_f09_run_bist_show, rmi_f09_run_bist_store),
	__ATTR(f09_control_test1, RMI_RW_ATTR,
	       rmi_f09_control_test1_show, rmi_f09_control_test1_store),
	__ATTR(f09_control_test2, RMI_RW_ATTR,
	       rmi_f09_control_test2_show, rmi_f09_control_test2_store),
};

static int rmi_f09_init(struct rmi_function_container *fc)
{
	int rc;

	dev_info(&fc->dev, "Intializing F09 values.");

	rc = rmi_f09_alloc_memory(fc);
	if (rc < 0)
		goto error_exit;

	rc = rmi_f09_initialize(fc);
	if (rc < 0)
		goto error_exit;

	rc = rmi_f09_create_sysfs(fc);
	if (rc < 0)
		goto error_exit;

	return 0;

error_exit:
	rmi_f09_free_memory(fc);

	return rc;
}

static int rmi_f09_alloc_memory(struct rmi_function_container *fc)
{
	struct rmi_fn_09_data *f09;

	f09 = kzalloc(sizeof(struct rmi_fn_09_data), GFP_KERNEL);
	if (!f09) {
		dev_err(&fc->dev, "Failed to allocate rmi_fn_09_data.\n");
		return -ENOMEM;
	}
	fc->data = f09;

	return 0;
}

static void rmi_f09_free_memory(struct rmi_function_container *fc)
{
	kfree(fc->data);
	fc->data = NULL;
}

static int rmi_f09_initialize(struct rmi_function_container *fc)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct rmi_device_platform_data *pdata;
	struct rmi_fn_09_data *f09 = fc->data;
	u8 query_base_addr;
	int rc;


	pdata = to_rmi_platform_data(rmi_dev);
	query_base_addr = fc->fd.query_base_addr;

	/* initial all default values for f09 query here */
	rc = rmi_read_block(rmi_dev, query_base_addr,
		(u8 *)&f09->query, sizeof(f09->query));
	if (rc < 0) {
		dev_err(&fc->dev, "Failed to read query register."
			" from 0x%04x\n", query_base_addr);
		return rc;
	}

	return 0;
}

static int rmi_f09_create_sysfs(struct rmi_function_container *fc)
{
	int attr_count = 0;
	int rc;

	dev_dbg(&fc->dev, "Creating sysfs files.");
	/* Set up sysfs device attributes. */
	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		if (sysfs_create_file
		    (&fc->dev.kobj, &attrs[attr_count].attr) < 0) {
			dev_err(&fc->dev, "Failed to create sysfs file for %s.",
			     attrs[attr_count].attr.name);
			rc = -ENODEV;
			goto err_remove_sysfs;
		}
	}

	return 0;

err_remove_sysfs:
	for (attr_count--; attr_count >= 0; attr_count--)
		sysfs_remove_file(&fc->dev.kobj,
				  &attrs[attr_count].attr);

	return rc;
}

static int rmi_f09_config(struct rmi_function_container *fc)
{
	/*we do nothing here. instead reset should notify the user.*/
	return 0;
}

static int rmi_f09_reset(struct rmi_function_container *fc)
{
	struct  rmi_fn_09_data  *instance_data = fc->data;

	instance_data->status = -ECONNRESET;

	return 0;
}

static void rmi_f09_remove(struct rmi_function_container *fc)
{
	int attr_count;

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++)
		sysfs_remove_file(&fc->dev.kobj,
				  &attrs[attr_count].attr);

	rmi_f09_free_memory(fc);
}

static int rmi_f09_attention(struct rmi_function_container *fc, u8 *irq_bits)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct rmi_fn_09_data *data = fc->data;
	int error;
	error = rmi_read_block(rmi_dev, fc->fd.command_base_addr,
			(u8 *)&data->cmd, sizeof(data->cmd));

	if (error < 0) {
		dev_err(&fc->dev, "Failed to read command register.\n");
		return error;
	}
	/* If the command register is cleared, meaning the value is 0 */
	if (data->status == ECONNRESET) {
		dev_warn(&rmi_dev->dev, "RESET occured: %#04x.\n",
			data->status);
	} else if (data->cmd.run_bist) {
		dev_warn(&rmi_dev->dev,
			"Command register not cleared: %#04x.\n",
			data->cmd.run_bist);
	}
	return 0;
}

static struct rmi_function_handler function_handler = {
	.func = 0x09,
	.init = rmi_f09_init,
	.attention = rmi_f09_attention,
	.config = rmi_f09_config,
	.reset = rmi_f09_reset,
	.remove = rmi_f09_remove
};

static int __init rmi_f09_module_init(void)
{
	int error;

	error = rmi_register_function_driver(&function_handler);
	if (error < 0) {
		pr_err("%s: register failed!\n", __func__);
		return error;
	}

	return 0;
}

static void rmi_f09_module_exit(void)
{
	rmi_unregister_function_driver(&function_handler);
}

static ssize_t rmi_f09_status_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *instance_data;

	fc = to_rmi_function_container(dev);
	instance_data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n", instance_data->status);
}

static ssize_t rmi_f09_status_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *instance_data;

	fc = to_rmi_function_container(dev);
	instance_data = fc->data;

	/* any write to status resets 1 */
	instance_data->status = 0;

	return 0;
}

static ssize_t rmi_f09_limit_register_count_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->query.limit_register_count);
}

static ssize_t rmi_f09_host_test_enable_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->query.host_test_enable);
}

static ssize_t rmi_f09_host_test_enable_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;
	unsigned int new_value;
	int result;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	if (sscanf(buf, "%u", &new_value) != 1) {
		dev_err(dev,
			"%s: Error - hostTestEnable has an invalid length.\n",
			__func__);
		return -EINVAL;
	}

	if (new_value < 0 || new_value > 1) {
		dev_err(dev, "%s: Invalid hostTestEnable bit %s.",
			__func__, buf);
		return -EINVAL;
	}
	data->query.host_test_enable = new_value;
	result = rmi_write(fc->rmi_dev, fc->fd.query_base_addr,
		data->query.host_test_enable);
	if (result < 0) {
		dev_err(dev, "%s: Could not write hostTestEnable to 0x%x\n",
				__func__, fc->fd.query_base_addr);
		return result;
	}

	return count;

}

static ssize_t rmi_f09_internal_limits_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->query.internal_limits);
}

static ssize_t rmi_f09_result_register_count_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->query.result_register_count);
}

static ssize_t rmi_f09_overall_bist_result_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->data.overall_bist_result);
}

static ssize_t rmi_f09_test_result1_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->data.test_result1);
}

static ssize_t rmi_f09_test_result2_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->data.test_result2);
}

static ssize_t rmi_f09_run_bist_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->cmd.run_bist);
}

static ssize_t rmi_f09_run_bist_store(struct device *dev,
				      struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;
	unsigned int new_value;
	int result;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	if (sscanf(buf, "%u", &new_value) != 1) {
		dev_err(dev,
		"%s: Error - run_bist_store has an "
		"invalid len.\n",
		__func__);
		return -EINVAL;
	}

	if (new_value < 0 || new_value > 1) {
		dev_err(dev, "%s: Invalid run_bist bit %s.", __func__, buf);
		return -EINVAL;
	}
	data->cmd.run_bist = new_value;
	result = rmi_write(fc->rmi_dev, fc->fd.command_base_addr,
		data->cmd.run_bist);
	if (result < 0) {
		dev_err(dev, "%s : Could not write run_bist_store to 0x%x\n",
				__func__, fc->fd.command_base_addr);
		return result;
	}

	return count;

}

static ssize_t rmi_f09_control_test1_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u %u %u\n",
			data->control.test1_limit_low,
			data->control.test1_limit_high,
			data->control.test1_limit_diff);
}

static ssize_t rmi_f09_control_test1_store(struct device *dev,
				      struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;
	unsigned int new_low, new_high, new_diff;
	int result;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	if (sscanf(buf, "%u %u %u", &new_low, &new_high, &new_diff) != 3) {
		dev_err(dev,
		"%s: Error - f09_control_test1_store has an "
		"invalid len.\n",
		__func__);
		return -EINVAL;
	}

	if (new_low < 0 || new_low > 1 || new_high < 0 || new_high
			|| new_diff < 0 || new_diff) {
		dev_err(dev, "%s: Invalid f09_control_test1_diff bit %s.",
			__func__, buf);
		return -EINVAL;
	}
	data->control.test1_limit_low = new_low;
	data->control.test1_limit_high = new_high;
	data->control.test1_limit_diff = new_diff;
	result = rmi_write(fc->rmi_dev, fc->fd.control_base_addr,
		data->control.test1_limit_low);
	if (result < 0) {
		dev_err(dev, "%s : Could not write f09_control_test1_limit_low to 0x%x\n",
				__func__, fc->fd.control_base_addr);
		return result;
	}

	result = rmi_write(fc->rmi_dev, fc->fd.control_base_addr,
		data->control.test1_limit_high);
	if (result < 0) {
		dev_err(dev, "%s : Could not write f09_control_test1_limit_high to 0x%x\n",
				__func__, fc->fd.control_base_addr);
		return result;
	}

	result = rmi_write(fc->rmi_dev, fc->fd.control_base_addr,
		data->control.test1_limit_diff);
	if (result < 0) {
		dev_err(dev, "%s : Could not write f09_control_test1_limit_diff to 0x%x\n",
				__func__, fc->fd.control_base_addr);
		return result;
	}

	return count;

}

static ssize_t rmi_f09_control_test2_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u %u %u\n",
			data->control.test2_limit_low,
			data->control.test2_limit_high,
			data->control.test2_limit_diff);
}

static ssize_t rmi_f09_control_test2_store(struct device *dev,
				      struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;
	unsigned int new_low, new_high, new_diff;
	int result;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	if (sscanf(buf, "%u %u %u", &new_low, &new_high, &new_diff) != 3) {
		dev_err(dev,
		"%s: Error - f09_control_test1_store has an "
		"invalid len.\n",
		__func__);
		return -EINVAL;
	}

	if (new_low < 0 || new_low > 1 || new_high < 0 || new_high > 1 ||
			new_diff < 0 || new_diff > 1) {
		dev_err(dev, "%s: Invalid f09_control_test2_diff bit %s.",
			__func__, buf);
		return -EINVAL;
	}
	data->control.test2_limit_low = new_low;
	data->control.test2_limit_high = new_high;
	data->control.test2_limit_diff = new_diff;
	result = rmi_write(fc->rmi_dev, fc->fd.control_base_addr,
		data->control.test2_limit_low);
	if (result < 0) {
		dev_err(dev, "%s : Could not write f09_control_test2_limit_low to 0x%x\n",
				__func__, fc->fd.control_base_addr);
		return result;
	}

	result = rmi_write(fc->rmi_dev, fc->fd.control_base_addr,
		data->control.test2_limit_high);
	if (result < 0) {
		dev_err(dev, "%s : Could not write f09_control_test2_limit_high to 0x%x\n",
				__func__, fc->fd.control_base_addr);
		return result;
	}

	result = rmi_write(fc->rmi_dev, fc->fd.control_base_addr,
		data->control.test2_limit_diff);
	if (result < 0) {
		dev_err(dev, "%s : Could not write f09_control_test2_limit_diff to 0x%x\n",
				__func__, fc->fd.control_base_addr);
		return result;
	}

	return count;

}


static ssize_t rmi_f09_test_number_control_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->data.test_number_control);
}

static ssize_t rmi_f09_test_number_control_store(struct device *dev,
				      struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct rmi_fn_09_data *data;
	unsigned int new_value;
	int result;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	if (sscanf(buf, "%u", &new_value) != 1) {
		dev_err(dev,
		"%s: Error - test_number_control_store has an "
		"invalid len.\n",
		__func__);
		return -EINVAL;
	}

	if (new_value < 0 || new_value > 1) {
		dev_err(dev, "%s: Invalid test_number_control bit %s.",
			__func__, buf);
		return -EINVAL;
	}
	data->data.test_number_control = new_value;
	result = rmi_write(fc->rmi_dev, fc->fd.control_base_addr,
		data->data.test_number_control);
	if (result < 0) {
		dev_err(dev, "%s : Could not write "
				"test_number_control_store to 0x%x\n",
				__func__, fc->fd.data_base_addr);
		return result;
	}

	return count;
}

module_init(rmi_f09_module_init);
module_exit(rmi_f09_module_exit);

MODULE_AUTHOR("Allie Xiong <axiong@Synaptics.com>");
MODULE_DESCRIPTION("RMI F09 module");
MODULE_LICENSE("GPL");
MODULE_VERSION(RMI_DRIVER_VERSION);
