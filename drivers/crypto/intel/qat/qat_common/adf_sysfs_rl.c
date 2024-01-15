// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#define dev_fmt(fmt) "RateLimiting: " fmt

#include <linux/dev_printk.h>
#include <linux/pci.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "adf_common_drv.h"
#include "adf_rl.h"
#include "adf_sysfs_rl.h"

#define GET_RL_STRUCT(accel_dev) ((accel_dev)->rate_limiting->user_input)

enum rl_ops {
	ADD,
	UPDATE,
	RM,
	RM_ALL,
	GET,
};

enum rl_params {
	RP_MASK,
	ID,
	CIR,
	PIR,
	SRV,
	CAP_REM_SRV,
};

static const char *const rl_services[] = {
	[ADF_SVC_ASYM] = "asym",
	[ADF_SVC_SYM] = "sym",
	[ADF_SVC_DC] = "dc",
};

static const char *const rl_operations[] = {
	[ADD] = "add",
	[UPDATE] = "update",
	[RM] = "rm",
	[RM_ALL] = "rm_all",
	[GET] = "get",
};

static int set_param_u(struct device *dev, enum rl_params param, u64 set)
{
	struct adf_rl_interface_data *data;
	struct adf_accel_dev *accel_dev;
	int ret = 0;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	data = &GET_RL_STRUCT(accel_dev);

	down_write(&data->lock);
	switch (param) {
	case RP_MASK:
		data->input.rp_mask = set;
		break;
	case CIR:
		data->input.cir = set;
		break;
	case PIR:
		data->input.pir = set;
		break;
	case SRV:
		data->input.srv = set;
		break;
	case CAP_REM_SRV:
		data->cap_rem_srv = set;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	up_write(&data->lock);

	return ret;
}

static int set_param_s(struct device *dev, enum rl_params param, int set)
{
	struct adf_rl_interface_data *data;
	struct adf_accel_dev *accel_dev;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev || param != ID)
		return -EINVAL;

	data = &GET_RL_STRUCT(accel_dev);

	down_write(&data->lock);
	data->input.sla_id = set;
	up_write(&data->lock);

	return 0;
}

static int get_param_u(struct device *dev, enum rl_params param, u64 *get)
{
	struct adf_rl_interface_data *data;
	struct adf_accel_dev *accel_dev;
	int ret = 0;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	data = &GET_RL_STRUCT(accel_dev);

	down_read(&data->lock);
	switch (param) {
	case RP_MASK:
		*get = data->input.rp_mask;
		break;
	case CIR:
		*get = data->input.cir;
		break;
	case PIR:
		*get = data->input.pir;
		break;
	case SRV:
		*get = data->input.srv;
		break;
	default:
		ret = -EINVAL;
	}
	up_read(&data->lock);

	return ret;
}

static int get_param_s(struct device *dev, enum rl_params param)
{
	struct adf_rl_interface_data *data;
	struct adf_accel_dev *accel_dev;
	int ret = 0;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	data = &GET_RL_STRUCT(accel_dev);

	down_read(&data->lock);
	if (param == ID)
		ret = data->input.sla_id;
	up_read(&data->lock);

	return ret;
}

static ssize_t rp_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	int ret;
	u64 get;

	ret = get_param_u(dev, RP_MASK, &get);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%#llx\n", get);
}

static ssize_t rp_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	int err;
	u64 val;

	err = kstrtou64(buf, 16, &val);
	if (err)
		return err;

	err = set_param_u(dev, RP_MASK, val);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_RW(rp);

static ssize_t id_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return sysfs_emit(buf, "%d\n", get_param_s(dev, ID));
}

static ssize_t id_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	int err;
	int val;

	err = kstrtoint(buf, 10, &val);
	if (err)
		return err;

	err = set_param_s(dev, ID, val);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_RW(id);

static ssize_t cir_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int ret;
	u64 get;

	ret = get_param_u(dev, CIR, &get);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%llu\n", get);
}

static ssize_t cir_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	unsigned int val;
	int err;

	err = kstrtouint(buf, 10, &val);
	if (err)
		return err;

	err = set_param_u(dev, CIR, val);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_RW(cir);

static ssize_t pir_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int ret;
	u64 get;

	ret = get_param_u(dev, PIR, &get);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%llu\n", get);
}

static ssize_t pir_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	unsigned int val;
	int err;

	err = kstrtouint(buf, 10, &val);
	if (err)
		return err;

	err = set_param_u(dev, PIR, val);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_RW(pir);

static ssize_t srv_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int ret;
	u64 get;

	ret = get_param_u(dev, SRV, &get);
	if (ret)
		return ret;

	if (get == ADF_SVC_NONE)
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", rl_services[get]);
}

static ssize_t srv_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	unsigned int val;
	int ret;

	ret = sysfs_match_string(rl_services, buf);
	if (ret < 0)
		return ret;

	val = ret;
	ret = set_param_u(dev, SRV, val);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(srv);

static ssize_t cap_rem_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adf_rl_interface_data *data;
	struct adf_accel_dev *accel_dev;
	int ret, rem_cap;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	data = &GET_RL_STRUCT(accel_dev);

	down_read(&data->lock);
	rem_cap = adf_rl_get_capability_remaining(accel_dev, data->cap_rem_srv,
						  RL_SLA_EMPTY_ID);
	up_read(&data->lock);
	if (rem_cap < 0)
		return rem_cap;

	ret = sysfs_emit(buf, "%u\n", rem_cap);

	return ret;
}

static ssize_t cap_rem_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	unsigned int val;
	int ret;

	ret = sysfs_match_string(rl_services, buf);
	if (ret < 0)
		return ret;

	val = ret;
	ret = set_param_u(dev, CAP_REM_SRV, val);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(cap_rem);

static ssize_t sla_op_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct adf_rl_interface_data *data;
	struct adf_accel_dev *accel_dev;
	int ret;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	data = &GET_RL_STRUCT(accel_dev);

	ret = sysfs_match_string(rl_operations, buf);
	if (ret < 0)
		return ret;

	down_write(&data->lock);
	switch (ret) {
	case ADD:
		data->input.parent_id = RL_PARENT_DEFAULT_ID;
		data->input.type = RL_LEAF;
		data->input.sla_id = 0;
		ret = adf_rl_add_sla(accel_dev, &data->input);
		if (ret)
			goto err_free_lock;
		break;
	case UPDATE:
		ret = adf_rl_update_sla(accel_dev, &data->input);
		if (ret)
			goto err_free_lock;
		break;
	case RM:
		ret = adf_rl_remove_sla(accel_dev, data->input.sla_id);
		if (ret)
			goto err_free_lock;
		break;
	case RM_ALL:
		adf_rl_remove_sla_all(accel_dev, false);
		break;
	case GET:
		ret = adf_rl_get_sla(accel_dev, &data->input);
		if (ret)
			goto err_free_lock;
		break;
	default:
		ret = -EINVAL;
		goto err_free_lock;
	}
	up_write(&data->lock);

	return count;

err_free_lock:
	up_write(&data->lock);

	return ret;
}
static DEVICE_ATTR_WO(sla_op);

static struct attribute *qat_rl_attrs[] = {
	&dev_attr_rp.attr,
	&dev_attr_id.attr,
	&dev_attr_cir.attr,
	&dev_attr_pir.attr,
	&dev_attr_srv.attr,
	&dev_attr_cap_rem.attr,
	&dev_attr_sla_op.attr,
	NULL,
};

static struct attribute_group qat_rl_group = {
	.attrs = qat_rl_attrs,
	.name = "qat_rl",
};

int adf_sysfs_rl_add(struct adf_accel_dev *accel_dev)
{
	struct adf_rl_interface_data *data;
	int ret;

	data = &GET_RL_STRUCT(accel_dev);

	ret = device_add_group(&GET_DEV(accel_dev), &qat_rl_group);
	if (ret)
		dev_err(&GET_DEV(accel_dev),
			"Failed to create qat_rl attribute group\n");

	data->cap_rem_srv = ADF_SVC_NONE;
	data->input.srv = ADF_SVC_NONE;

	return ret;
}

void adf_sysfs_rl_rm(struct adf_accel_dev *accel_dev)
{
	device_remove_group(&GET_DEV(accel_dev), &qat_rl_group);
}
