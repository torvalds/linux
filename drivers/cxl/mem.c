// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "cxlmem.h"
#include "cxlpci.h"

/**
 * DOC: cxl mem
 *
 * CXL memory endpoint devices and switches are CXL capable devices that are
 * participating in CXL.mem protocol. Their functionality builds on top of the
 * CXL.io protocol that allows enumerating and configuring components via
 * standard PCI mechanisms.
 *
 * The cxl_mem driver owns kicking off the enumeration of this CXL.mem
 * capability. With the detection of a CXL capable endpoint, the driver will
 * walk up to find the platform specific port it is connected to, and determine
 * if there are intervening switches in the path. If there are switches, a
 * secondary action is to enumerate those (implemented in cxl_core). Finally the
 * cxl_mem driver adds the device it is bound to as a CXL endpoint-port for use
 * in higher level operations.
 */

static void enable_suspend(void *data)
{
	cxl_mem_active_dec();
}

static void remove_debugfs(void *dentry)
{
	debugfs_remove_recursive(dentry);
}

static int cxl_mem_dpa_show(struct seq_file *file, void *data)
{
	struct device *dev = file->private;
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);

	cxl_dpa_debug(file, cxlmd->cxlds);

	return 0;
}

static int cxl_debugfs_poison_inject(void *data, u64 dpa)
{
	struct cxl_memdev *cxlmd = data;

	return cxl_inject_poison(cxlmd, dpa);
}

DEFINE_DEBUGFS_ATTRIBUTE(cxl_poison_inject_fops, NULL,
			 cxl_debugfs_poison_inject, "%llx\n");

static int cxl_debugfs_poison_clear(void *data, u64 dpa)
{
	struct cxl_memdev *cxlmd = data;

	return cxl_clear_poison(cxlmd, dpa);
}

DEFINE_DEBUGFS_ATTRIBUTE(cxl_poison_clear_fops, NULL,
			 cxl_debugfs_poison_clear, "%llx\n");

static int cxl_mem_probe(struct device *dev)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct device *endpoint_parent;
	struct cxl_dport *dport;
	struct dentry *dentry;
	int rc;

	if (!cxlds->media_ready)
		return -EBUSY;

	/*
	 * Someone is trying to reattach this device after it lost its port
	 * connection (an endpoint port previously registered by this memdev was
	 * disabled). This racy check is ok because if the port is still gone,
	 * no harm done, and if the port hierarchy comes back it will re-trigger
	 * this probe. Port rescan and memdev detach work share the same
	 * single-threaded workqueue.
	 */
	if (work_pending(&cxlmd->detach_work))
		return -EBUSY;

	dentry = cxl_debugfs_create_dir(dev_name(dev));
	debugfs_create_devm_seqfile(dev, "dpamem", dentry, cxl_mem_dpa_show);

	if (test_bit(CXL_POISON_ENABLED_INJECT, mds->poison.enabled_cmds))
		debugfs_create_file("inject_poison", 0200, dentry, cxlmd,
				    &cxl_poison_inject_fops);
	if (test_bit(CXL_POISON_ENABLED_CLEAR, mds->poison.enabled_cmds))
		debugfs_create_file("clear_poison", 0200, dentry, cxlmd,
				    &cxl_poison_clear_fops);

	rc = devm_add_action_or_reset(dev, remove_debugfs, dentry);
	if (rc)
		return rc;

	rc = devm_cxl_enumerate_ports(cxlmd);
	if (rc)
		return rc;

	struct cxl_port *parent_port __free(put_cxl_port) =
		cxl_mem_find_port(cxlmd, &dport);
	if (!parent_port) {
		dev_err(dev, "CXL port topology not found\n");
		return -ENXIO;
	}

	if (cxl_pmem_size(cxlds) && IS_ENABLED(CONFIG_CXL_PMEM)) {
		rc = devm_cxl_add_nvdimm(dev, parent_port, cxlmd);
		if (rc) {
			if (rc == -ENODEV)
				dev_info(dev, "PMEM disabled by platform\n");
			return rc;
		}
	}

	if (dport->rch)
		endpoint_parent = parent_port->uport_dev;
	else
		endpoint_parent = &parent_port->dev;

	scoped_guard(device, endpoint_parent) {
		if (!endpoint_parent->driver) {
			dev_err(dev, "CXL port topology %s not enabled\n",
				dev_name(endpoint_parent));
			return -ENXIO;
		}

		rc = devm_cxl_add_endpoint(endpoint_parent, cxlmd, dport);
		if (rc)
			return rc;
	}

	if (cxlmd->attach) {
		rc = cxlmd->attach->probe(cxlmd);
		if (rc)
			return rc;
	}

	rc = devm_cxl_memdev_edac_register(cxlmd);
	if (rc)
		dev_dbg(dev, "CXL memdev EDAC registration failed rc=%d\n", rc);

	/*
	 * The kernel may be operating out of CXL memory on this device,
	 * there is no spec defined way to determine whether this device
	 * preserves contents over suspend, and there is no simple way
	 * to arrange for the suspend image to avoid CXL memory which
	 * would setup a circular dependency between PCI resume and save
	 * state restoration.
	 *
	 * TODO: support suspend when all the regions this device is
	 * hosting are locked and covered by the system address map,
	 * i.e. platform firmware owns restoring the HDM configuration
	 * that it locked.
	 */
	cxl_mem_active_inc();
	return devm_add_action_or_reset(dev, enable_suspend, NULL);
}

/**
 * devm_cxl_add_memdev - Add a CXL memory device
 * @cxlds: CXL device state to associate with the memdev
 * @attach: Caller depends on CXL topology attachment
 *
 * Upon return the device will have had a chance to attach to the
 * cxl_mem driver, but may fail to attach if the CXL topology is not ready
 * (hardware CXL link down, or software platform CXL root not attached).
 *
 * When @attach is NULL it indicates the caller wants the memdev to remain
 * registered even if it does not immediately attach to the CXL hierarchy. When
 * @attach is provided a cxl_mem_probe() failure leads to failure of this routine.
 *
 * The parent of the resulting device and the devm context for allocations is
 * @cxlds->dev.
 */
struct cxl_memdev *devm_cxl_add_memdev(struct cxl_dev_state *cxlds,
				       const struct cxl_memdev_attach *attach)
{
	return __devm_cxl_add_memdev(cxlds, attach);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_memdev, "CXL");

static ssize_t trigger_poison_list_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	bool trigger;
	int rc;

	if (kstrtobool(buf, &trigger) || !trigger)
		return -EINVAL;

	rc = cxl_trigger_poison_list(to_cxl_memdev(dev));

	return rc ? rc : len;
}
static DEVICE_ATTR_WO(trigger_poison_list);

static umode_t cxl_mem_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);

	if (a == &dev_attr_trigger_poison_list.attr)
		if (!test_bit(CXL_POISON_ENABLED_LIST,
			      mds->poison.enabled_cmds))
			return 0;

	return a->mode;
}

static struct attribute *cxl_mem_attrs[] = {
	&dev_attr_trigger_poison_list.attr,
	NULL
};

static struct attribute_group cxl_mem_group = {
	.attrs = cxl_mem_attrs,
	.is_visible = cxl_mem_visible,
};

__ATTRIBUTE_GROUPS(cxl_mem);

static struct cxl_driver cxl_mem_driver = {
	.name = "cxl_mem",
	.probe = cxl_mem_probe,
	.id = CXL_DEVICE_MEMORY_EXPANDER,
	.drv = {
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
		.dev_groups = cxl_mem_groups,
	},
};

module_cxl_driver(cxl_mem_driver);

MODULE_DESCRIPTION("CXL: Memory Expansion");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("CXL");
MODULE_ALIAS_CXL(CXL_DEVICE_MEMORY_EXPANDER);
