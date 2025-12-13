// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/ndctl.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include "nd-core.h"
#include "label.h"
#include "pmem.h"
#include "nd.h"

static DEFINE_IDA(dimm_ida);

/*
 * Retrieve bus and dimm handle and return if this bus supports
 * get_config_data commands
 */
int nvdimm_check_config_data(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	if (!nvdimm->cmd_mask ||
	    !test_bit(ND_CMD_GET_CONFIG_DATA, &nvdimm->cmd_mask)) {
		if (test_bit(NDD_LABELING, &nvdimm->flags))
			return -ENXIO;
		else
			return -ENOTTY;
	}

	return 0;
}

static int validate_dimm(struct nvdimm_drvdata *ndd)
{
	int rc;

	if (!ndd)
		return -EINVAL;

	rc = nvdimm_check_config_data(ndd->dev);
	if (rc)
		dev_dbg(ndd->dev, "%ps: %s error: %d\n",
				__builtin_return_address(0), __func__, rc);
	return rc;
}

/**
 * nvdimm_init_nsarea - determine the geometry of a dimm's namespace area
 * @ndd: dimm to initialize
 *
 * Returns: %0 if the area is already valid, -errno on error
 */
int nvdimm_init_nsarea(struct nvdimm_drvdata *ndd)
{
	struct nd_cmd_get_config_size *cmd = &ndd->nsarea;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(ndd->dev);
	struct nvdimm_bus_descriptor *nd_desc;
	int rc = validate_dimm(ndd);
	int cmd_rc = 0;

	if (rc)
		return rc;

	if (cmd->config_size)
		return 0; /* already valid */

	memset(cmd, 0, sizeof(*cmd));
	nd_desc = nvdimm_bus->nd_desc;
	rc = nd_desc->ndctl(nd_desc, to_nvdimm(ndd->dev),
			ND_CMD_GET_CONFIG_SIZE, cmd, sizeof(*cmd), &cmd_rc);
	if (rc < 0)
		return rc;
	return cmd_rc;
}

int nvdimm_get_config_data(struct nvdimm_drvdata *ndd, void *buf,
			   size_t offset, size_t len)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(ndd->dev);
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;
	int rc = validate_dimm(ndd), cmd_rc = 0;
	struct nd_cmd_get_config_data_hdr *cmd;
	size_t max_cmd_size, buf_offset;

	if (rc)
		return rc;

	if (offset + len > ndd->nsarea.config_size)
		return -ENXIO;

	max_cmd_size = min_t(u32, len, ndd->nsarea.max_xfer);
	cmd = kvzalloc(max_cmd_size + sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	for (buf_offset = 0; len;
	     len -= cmd->in_length, buf_offset += cmd->in_length) {
		size_t cmd_size;

		cmd->in_offset = offset + buf_offset;
		cmd->in_length = min(max_cmd_size, len);

		cmd_size = sizeof(*cmd) + cmd->in_length;

		rc = nd_desc->ndctl(nd_desc, to_nvdimm(ndd->dev),
				ND_CMD_GET_CONFIG_DATA, cmd, cmd_size, &cmd_rc);
		if (rc < 0)
			break;
		if (cmd_rc < 0) {
			rc = cmd_rc;
			break;
		}

		/* out_buf should be valid, copy it into our output buffer */
		memcpy(buf + buf_offset, cmd->out_buf, cmd->in_length);
	}
	kvfree(cmd);

	return rc;
}

int nvdimm_set_config_data(struct nvdimm_drvdata *ndd, size_t offset,
		void *buf, size_t len)
{
	size_t max_cmd_size, buf_offset;
	struct nd_cmd_set_config_hdr *cmd;
	int rc = validate_dimm(ndd), cmd_rc = 0;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(ndd->dev);
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;

	if (rc)
		return rc;

	if (offset + len > ndd->nsarea.config_size)
		return -ENXIO;

	max_cmd_size = min_t(u32, len, ndd->nsarea.max_xfer);
	cmd = kvzalloc(max_cmd_size + sizeof(*cmd) + sizeof(u32), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	for (buf_offset = 0; len; len -= cmd->in_length,
			buf_offset += cmd->in_length) {
		size_t cmd_size;

		cmd->in_offset = offset + buf_offset;
		cmd->in_length = min(max_cmd_size, len);
		memcpy(cmd->in_buf, buf + buf_offset, cmd->in_length);

		/* status is output in the last 4-bytes of the command buffer */
		cmd_size = sizeof(*cmd) + cmd->in_length + sizeof(u32);

		rc = nd_desc->ndctl(nd_desc, to_nvdimm(ndd->dev),
				ND_CMD_SET_CONFIG_DATA, cmd, cmd_size, &cmd_rc);
		if (rc < 0)
			break;
		if (cmd_rc < 0) {
			rc = cmd_rc;
			break;
		}
	}
	kvfree(cmd);

	return rc;
}

void nvdimm_set_labeling(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	set_bit(NDD_LABELING, &nvdimm->flags);
}

void nvdimm_set_locked(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	set_bit(NDD_LOCKED, &nvdimm->flags);
}

void nvdimm_clear_locked(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	clear_bit(NDD_LOCKED, &nvdimm->flags);
}

static void nvdimm_release(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	ida_free(&dimm_ida, nvdimm->id);
	kfree(nvdimm);
}

struct nvdimm *to_nvdimm(struct device *dev)
{
	struct nvdimm *nvdimm = container_of(dev, struct nvdimm, dev);

	WARN_ON(!is_nvdimm(dev));
	return nvdimm;
}
EXPORT_SYMBOL_GPL(to_nvdimm);

struct nvdimm_drvdata *to_ndd(struct nd_mapping *nd_mapping)
{
	struct nvdimm *nvdimm = nd_mapping->nvdimm;

	WARN_ON_ONCE(!is_nvdimm_bus_locked(&nvdimm->dev));

	return dev_get_drvdata(&nvdimm->dev);
}
EXPORT_SYMBOL(to_ndd);

void nvdimm_drvdata_release(struct kref *kref)
{
	struct nvdimm_drvdata *ndd = container_of(kref, typeof(*ndd), kref);
	struct device *dev = ndd->dev;
	struct resource *res, *_r;

	dev_dbg(dev, "trace\n");
	scoped_guard(nvdimm_bus, dev) {
		for_each_dpa_resource_safe(ndd, res, _r)
			nvdimm_free_dpa(ndd, res);
	}

	kvfree(ndd->data);
	kfree(ndd);
	put_device(dev);
}

void get_ndd(struct nvdimm_drvdata *ndd)
{
	kref_get(&ndd->kref);
}

void put_ndd(struct nvdimm_drvdata *ndd)
{
	if (ndd)
		kref_put(&ndd->kref, nvdimm_drvdata_release);
}

const char *nvdimm_name(struct nvdimm *nvdimm)
{
	return dev_name(&nvdimm->dev);
}
EXPORT_SYMBOL_GPL(nvdimm_name);

struct kobject *nvdimm_kobj(struct nvdimm *nvdimm)
{
	return &nvdimm->dev.kobj;
}
EXPORT_SYMBOL_GPL(nvdimm_kobj);

unsigned long nvdimm_cmd_mask(struct nvdimm *nvdimm)
{
	return nvdimm->cmd_mask;
}
EXPORT_SYMBOL_GPL(nvdimm_cmd_mask);

void *nvdimm_provider_data(struct nvdimm *nvdimm)
{
	if (nvdimm)
		return nvdimm->provider_data;
	return NULL;
}
EXPORT_SYMBOL_GPL(nvdimm_provider_data);

static ssize_t commands_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	int cmd, len = 0;

	if (!nvdimm->cmd_mask)
		return sprintf(buf, "\n");

	for_each_set_bit(cmd, &nvdimm->cmd_mask, BITS_PER_LONG)
		len += sprintf(buf + len, "%s ", nvdimm_cmd_name(cmd));
	len += sprintf(buf + len, "\n");
	return len;
}
static DEVICE_ATTR_RO(commands);

static ssize_t flags_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	return sprintf(buf, "%s%s\n",
			test_bit(NDD_LABELING, &nvdimm->flags) ? "label " : "",
			test_bit(NDD_LOCKED, &nvdimm->flags) ? "lock " : "");
}
static DEVICE_ATTR_RO(flags);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	/*
	 * The state may be in the process of changing, userspace should
	 * quiesce probing if it wants a static answer
	 */
	nvdimm_bus_lock(dev);
	nvdimm_bus_unlock(dev);
	return sprintf(buf, "%s\n", atomic_read(&nvdimm->busy)
			? "active" : "idle");
}
static DEVICE_ATTR_RO(state);

static ssize_t __available_slots_show(struct nvdimm_drvdata *ndd, char *buf)
{
	struct device *dev;
	u32 nfree;

	if (!ndd)
		return -ENXIO;

	dev = ndd->dev;
	guard(nvdimm_bus)(dev);
	nfree = nd_label_nfree(ndd);
	if (nfree - 1 > nfree) {
		dev_WARN_ONCE(dev, 1, "we ate our last label?\n");
		nfree = 0;
	} else
		nfree--;
	return sprintf(buf, "%d\n", nfree);
}

static ssize_t available_slots_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	ssize_t rc;

	device_lock(dev);
	rc = __available_slots_show(dev_get_drvdata(dev), buf);
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(available_slots);

static ssize_t security_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	/*
	 * For the test version we need to poll the "hardware" in order
	 * to get the updated status for unlock testing.
	 */
	if (IS_ENABLED(CONFIG_NVDIMM_SECURITY_TEST))
		nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);

	if (test_bit(NVDIMM_SECURITY_OVERWRITE, &nvdimm->sec.flags))
		return sprintf(buf, "overwrite\n");
	if (test_bit(NVDIMM_SECURITY_DISABLED, &nvdimm->sec.flags))
		return sprintf(buf, "disabled\n");
	if (test_bit(NVDIMM_SECURITY_UNLOCKED, &nvdimm->sec.flags))
		return sprintf(buf, "unlocked\n");
	if (test_bit(NVDIMM_SECURITY_LOCKED, &nvdimm->sec.flags))
		return sprintf(buf, "locked\n");
	return -ENOTTY;
}

static ssize_t frozen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	return sprintf(buf, "%d\n", test_bit(NVDIMM_SECURITY_FROZEN,
				&nvdimm->sec.flags));
}
static DEVICE_ATTR_RO(frozen);

static ssize_t security_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)

{
	/*
	 * Require all userspace triggered security management to be
	 * done while probing is idle and the DIMM is not in active use
	 * in any region.
	 */
	guard(device)(dev);
	guard(nvdimm_bus)(dev);
	wait_nvdimm_bus_probe_idle(dev);
	return nvdimm_security_store(dev, buf, len);
}
static DEVICE_ATTR_RW(security);

static struct attribute *nvdimm_attributes[] = {
	&dev_attr_state.attr,
	&dev_attr_flags.attr,
	&dev_attr_commands.attr,
	&dev_attr_available_slots.attr,
	&dev_attr_security.attr,
	&dev_attr_frozen.attr,
	NULL,
};

static umode_t nvdimm_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, typeof(*dev), kobj);
	struct nvdimm *nvdimm = to_nvdimm(dev);

	if (a != &dev_attr_security.attr && a != &dev_attr_frozen.attr)
		return a->mode;
	if (!nvdimm->sec.flags)
		return 0;

	if (a == &dev_attr_security.attr) {
		/* Are there any state mutation ops (make writable)? */
		if (nvdimm->sec.ops->freeze || nvdimm->sec.ops->disable
				|| nvdimm->sec.ops->change_key
				|| nvdimm->sec.ops->erase
				|| nvdimm->sec.ops->overwrite)
			return a->mode;
		return 0444;
	}

	if (nvdimm->sec.ops->freeze)
		return a->mode;
	return 0;
}

static const struct attribute_group nvdimm_attribute_group = {
	.attrs = nvdimm_attributes,
	.is_visible = nvdimm_visible,
};

static ssize_t result_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	enum nvdimm_fwa_result result;

	if (!nvdimm->fw_ops)
		return -EOPNOTSUPP;

	guard(nvdimm_bus)(dev);
	result = nvdimm->fw_ops->activate_result(nvdimm);

	switch (result) {
	case NVDIMM_FWA_RESULT_NONE:
		return sprintf(buf, "none\n");
	case NVDIMM_FWA_RESULT_SUCCESS:
		return sprintf(buf, "success\n");
	case NVDIMM_FWA_RESULT_FAIL:
		return sprintf(buf, "fail\n");
	case NVDIMM_FWA_RESULT_NOTSTAGED:
		return sprintf(buf, "not_staged\n");
	case NVDIMM_FWA_RESULT_NEEDRESET:
		return sprintf(buf, "need_reset\n");
	default:
		return -ENXIO;
	}
}
static DEVICE_ATTR_ADMIN_RO(result);

static ssize_t activate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	enum nvdimm_fwa_state state;

	if (!nvdimm->fw_ops)
		return -EOPNOTSUPP;

	guard(nvdimm_bus)(dev);
	state = nvdimm->fw_ops->activate_state(nvdimm);

	switch (state) {
	case NVDIMM_FWA_IDLE:
		return sprintf(buf, "idle\n");
	case NVDIMM_FWA_BUSY:
		return sprintf(buf, "busy\n");
	case NVDIMM_FWA_ARMED:
		return sprintf(buf, "armed\n");
	default:
		return -ENXIO;
	}
}

static ssize_t activate_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	enum nvdimm_fwa_trigger arg;
	int rc;

	if (!nvdimm->fw_ops)
		return -EOPNOTSUPP;

	if (sysfs_streq(buf, "arm"))
		arg = NVDIMM_FWA_ARM;
	else if (sysfs_streq(buf, "disarm"))
		arg = NVDIMM_FWA_DISARM;
	else
		return -EINVAL;

	guard(nvdimm_bus)(dev);
	rc = nvdimm->fw_ops->arm(nvdimm, arg);

	if (rc < 0)
		return rc;
	return len;
}
static DEVICE_ATTR_ADMIN_RW(activate);

static struct attribute *nvdimm_firmware_attributes[] = {
	&dev_attr_activate.attr,
	&dev_attr_result.attr,
	NULL,
};

static umode_t nvdimm_firmware_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, typeof(*dev), kobj);
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;
	struct nvdimm *nvdimm = to_nvdimm(dev);
	enum nvdimm_fwa_capability cap;

	if (!nd_desc->fw_ops)
		return 0;
	if (!nvdimm->fw_ops)
		return 0;

	guard(nvdimm_bus)(dev);
	cap = nd_desc->fw_ops->capability(nd_desc);

	if (cap < NVDIMM_FWA_CAP_QUIESCE)
		return 0;

	return a->mode;
}

static const struct attribute_group nvdimm_firmware_attribute_group = {
	.name = "firmware",
	.attrs = nvdimm_firmware_attributes,
	.is_visible = nvdimm_firmware_visible,
};

static const struct attribute_group *nvdimm_attribute_groups[] = {
	&nd_device_attribute_group,
	&nvdimm_attribute_group,
	&nvdimm_firmware_attribute_group,
	NULL,
};

static const struct device_type nvdimm_device_type = {
	.name = "nvdimm",
	.release = nvdimm_release,
	.groups = nvdimm_attribute_groups,
};

bool is_nvdimm(const struct device *dev)
{
	return dev->type == &nvdimm_device_type;
}

static struct lock_class_key nvdimm_key;

struct nvdimm *__nvdimm_create(struct nvdimm_bus *nvdimm_bus,
		void *provider_data, const struct attribute_group **groups,
		unsigned long flags, unsigned long cmd_mask, int num_flush,
		struct resource *flush_wpq, const char *dimm_id,
		const struct nvdimm_security_ops *sec_ops,
		const struct nvdimm_fw_ops *fw_ops)
{
	struct nvdimm *nvdimm = kzalloc(sizeof(*nvdimm), GFP_KERNEL);
	struct device *dev;

	if (!nvdimm)
		return NULL;

	nvdimm->id = ida_alloc(&dimm_ida, GFP_KERNEL);
	if (nvdimm->id < 0) {
		kfree(nvdimm);
		return NULL;
	}

	nvdimm->dimm_id = dimm_id;
	nvdimm->provider_data = provider_data;
	nvdimm->flags = flags;
	nvdimm->cmd_mask = cmd_mask;
	nvdimm->num_flush = num_flush;
	nvdimm->flush_wpq = flush_wpq;
	atomic_set(&nvdimm->busy, 0);
	dev = &nvdimm->dev;
	dev_set_name(dev, "nmem%d", nvdimm->id);
	dev->parent = &nvdimm_bus->dev;
	dev->type = &nvdimm_device_type;
	dev->devt = MKDEV(nvdimm_major, nvdimm->id);
	dev->groups = groups;
	nvdimm->sec.ops = sec_ops;
	nvdimm->fw_ops = fw_ops;
	nvdimm->sec.overwrite_tmo = 0;
	INIT_DELAYED_WORK(&nvdimm->dwork, nvdimm_security_overwrite_query);
	/*
	 * Security state must be initialized before device_add() for
	 * attribute visibility.
	 */
	/* get security state and extended (master) state */
	nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);
	nvdimm->sec.ext_flags = nvdimm_security_flags(nvdimm, NVDIMM_MASTER);
	device_initialize(dev);
	lockdep_set_class(&dev->mutex, &nvdimm_key);
	if (test_bit(NDD_REGISTER_SYNC, &flags))
		nd_device_register_sync(dev);
	else
		nd_device_register(dev);

	return nvdimm;
}
EXPORT_SYMBOL_GPL(__nvdimm_create);

void nvdimm_delete(struct nvdimm *nvdimm)
{
	struct device *dev = &nvdimm->dev;
	bool dev_put = false;

	/* We are shutting down. Make state frozen artificially. */
	scoped_guard(nvdimm_bus, dev) {
		set_bit(NVDIMM_SECURITY_FROZEN, &nvdimm->sec.flags);
		dev_put = test_and_clear_bit(NDD_WORK_PENDING, &nvdimm->flags);
	}
	cancel_delayed_work_sync(&nvdimm->dwork);
	if (dev_put)
		put_device(dev);
	nd_device_unregister(dev, ND_SYNC);
}
EXPORT_SYMBOL_GPL(nvdimm_delete);

static void shutdown_security_notify(void *data)
{
	struct nvdimm *nvdimm = data;

	sysfs_put(nvdimm->sec.overwrite_state);
}

int nvdimm_security_setup_events(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	if (!nvdimm->sec.flags || !nvdimm->sec.ops
			|| !nvdimm->sec.ops->overwrite)
		return 0;
	nvdimm->sec.overwrite_state = sysfs_get_dirent(dev->kobj.sd, "security");
	if (!nvdimm->sec.overwrite_state)
		return -ENOMEM;

	return devm_add_action_or_reset(dev, shutdown_security_notify, nvdimm);
}
EXPORT_SYMBOL_GPL(nvdimm_security_setup_events);

int nvdimm_in_overwrite(struct nvdimm *nvdimm)
{
	return test_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags);
}
EXPORT_SYMBOL_GPL(nvdimm_in_overwrite);

int nvdimm_security_freeze(struct nvdimm *nvdimm)
{
	int rc;

	WARN_ON_ONCE(!is_nvdimm_bus_locked(&nvdimm->dev));

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->freeze)
		return -EOPNOTSUPP;

	if (!nvdimm->sec.flags)
		return -EIO;

	if (test_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags)) {
		dev_warn(&nvdimm->dev, "Overwrite operation in progress.\n");
		return -EBUSY;
	}

	rc = nvdimm->sec.ops->freeze(nvdimm);
	nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);

	return rc;
}

static unsigned long dpa_align(struct nd_region *nd_region)
{
	struct device *dev = &nd_region->dev;

	if (dev_WARN_ONCE(dev, !is_nvdimm_bus_locked(dev),
				"bus lock required for capacity provision\n"))
		return 0;
	if (dev_WARN_ONCE(dev, !nd_region->ndr_mappings || nd_region->align
				% nd_region->ndr_mappings,
				"invalid region align %#lx mappings: %d\n",
				nd_region->align, nd_region->ndr_mappings))
		return 0;
	return nd_region->align / nd_region->ndr_mappings;
}

/**
 * nd_pmem_max_contiguous_dpa - For the given dimm+region, return the max
 *			   contiguous unallocated dpa range.
 * @nd_region: constrain available space check to this reference region
 * @nd_mapping: container of dpa-resource-root + labels
 *
 * Returns: %0 if there is an alignment error, otherwise the max
 *		unallocated dpa range
 */
resource_size_t nd_pmem_max_contiguous_dpa(struct nd_region *nd_region,
					   struct nd_mapping *nd_mapping)
{
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct nvdimm_bus *nvdimm_bus;
	resource_size_t max = 0;
	struct resource *res;
	unsigned long align;

	/* if a dimm is disabled the available capacity is zero */
	if (!ndd)
		return 0;

	align = dpa_align(nd_region);
	if (!align)
		return 0;

	nvdimm_bus = walk_to_nvdimm_bus(ndd->dev);
	if (__reserve_free_pmem(&nd_region->dev, nd_mapping->nvdimm))
		return 0;
	for_each_dpa_resource(ndd, res) {
		resource_size_t start, end;

		if (strcmp(res->name, "pmem-reserve") != 0)
			continue;
		/* trim free space relative to current alignment setting */
		start = ALIGN(res->start, align);
		end = ALIGN_DOWN(res->end + 1, align) - 1;
		if (end < start)
			continue;
		if (end - start + 1 > max)
			max = end - start + 1;
	}
	release_free_pmem(nvdimm_bus, nd_mapping);
	return max;
}

/**
 * nd_pmem_available_dpa - for the given dimm+region account unallocated dpa
 * @nd_mapping: container of dpa-resource-root + labels
 * @nd_region: constrain available space check to this reference region
 *
 * Validate that a PMEM label, if present, aligns with the start of an
 * interleave set.
 *
 * Returns: %0 if there is an alignment error, otherwise the unallocated dpa
 */
resource_size_t nd_pmem_available_dpa(struct nd_region *nd_region,
				      struct nd_mapping *nd_mapping)
{
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	resource_size_t map_start, map_end, busy = 0;
	struct resource *res;
	unsigned long align;

	if (!ndd)
		return 0;

	align = dpa_align(nd_region);
	if (!align)
		return 0;

	map_start = nd_mapping->start;
	map_end = map_start + nd_mapping->size - 1;
	for_each_dpa_resource(ndd, res) {
		resource_size_t start, end;

		start = ALIGN_DOWN(res->start, align);
		end = ALIGN(res->end + 1, align) - 1;
		if (start >= map_start && start < map_end) {
			if (end > map_end) {
				nd_dbg_dpa(nd_region, ndd, res,
					   "misaligned to iset\n");
				return 0;
			}
			busy += end - start + 1;
		} else if (end >= map_start && end <= map_end) {
			busy += end - start + 1;
		} else if (map_start > start && map_start < end) {
			/* total eclipse of the mapping */
			busy += nd_mapping->size;
		}
	}

	if (busy < nd_mapping->size)
		return ALIGN_DOWN(nd_mapping->size - busy, align);
	return 0;
}

void nvdimm_free_dpa(struct nvdimm_drvdata *ndd, struct resource *res)
{
	WARN_ON_ONCE(!is_nvdimm_bus_locked(ndd->dev));
	kfree(res->name);
	__release_region(&ndd->dpa, res->start, resource_size(res));
}

struct resource *nvdimm_allocate_dpa(struct nvdimm_drvdata *ndd,
		struct nd_label_id *label_id, resource_size_t start,
		resource_size_t n)
{
	char *name = kmemdup(label_id, sizeof(*label_id), GFP_KERNEL);
	struct resource *res;

	if (!name)
		return NULL;

	WARN_ON_ONCE(!is_nvdimm_bus_locked(ndd->dev));
	res = __request_region(&ndd->dpa, start, n, name, 0);
	if (!res)
		kfree(name);
	return res;
}

/**
 * nvdimm_allocated_dpa - sum up the dpa currently allocated to this label_id
 * @ndd: container of dpa-resource-root + labels
 * @label_id: dpa resource name of the form pmem-<human readable uuid>
 *
 * Returns: sum of the dpa allocated to the label_id
 */
resource_size_t nvdimm_allocated_dpa(struct nvdimm_drvdata *ndd,
		struct nd_label_id *label_id)
{
	resource_size_t allocated = 0;
	struct resource *res;

	for_each_dpa_resource(ndd, res)
		if (strcmp(res->name, label_id->id) == 0)
			allocated += resource_size(res);

	return allocated;
}

static int count_dimms(struct device *dev, void *c)
{
	int *count = c;

	if (is_nvdimm(dev))
		(*count)++;
	return 0;
}

int nvdimm_bus_check_dimm_count(struct nvdimm_bus *nvdimm_bus, int dimm_count)
{
	int count = 0;
	/* Flush any possible dimm registration failures */
	nd_synchronize();

	device_for_each_child(&nvdimm_bus->dev, &count, count_dimms);
	dev_dbg(&nvdimm_bus->dev, "count: %d\n", count);
	if (count != dimm_count)
		return -ENXIO;
	return 0;
}
EXPORT_SYMBOL_GPL(nvdimm_bus_check_dimm_count);

void __exit nvdimm_devs_exit(void)
{
	ida_destroy(&dimm_ida);
}
