// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include "cxl.h"
#include "mem.h"

/**
 * DOC: cxl core
 *
 * The CXL core provides a sysfs hierarchy for control devices and a rendezvous
 * point for cross-device interleave coordination through cxl ports.
 */

static DEFINE_IDA(cxl_port_ida);

static ssize_t devtype_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%s\n", dev->type->name);
}
static DEVICE_ATTR_RO(devtype);

static struct attribute *cxl_base_attributes[] = {
	&dev_attr_devtype.attr,
	NULL,
};

static struct attribute_group cxl_base_attribute_group = {
	.attrs = cxl_base_attributes,
};

static ssize_t start_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);

	return sysfs_emit(buf, "%#llx\n", cxld->range.start);
}
static DEVICE_ATTR_RO(start);

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);

	return sysfs_emit(buf, "%#llx\n", range_len(&cxld->range));
}
static DEVICE_ATTR_RO(size);

#define CXL_DECODER_FLAG_ATTR(name, flag)                            \
static ssize_t name##_show(struct device *dev,                       \
			   struct device_attribute *attr, char *buf) \
{                                                                    \
	struct cxl_decoder *cxld = to_cxl_decoder(dev);              \
                                                                     \
	return sysfs_emit(buf, "%s\n",                               \
			  (cxld->flags & (flag)) ? "1" : "0");       \
}                                                                    \
static DEVICE_ATTR_RO(name)

CXL_DECODER_FLAG_ATTR(cap_pmem, CXL_DECODER_F_PMEM);
CXL_DECODER_FLAG_ATTR(cap_ram, CXL_DECODER_F_RAM);
CXL_DECODER_FLAG_ATTR(cap_type2, CXL_DECODER_F_TYPE2);
CXL_DECODER_FLAG_ATTR(cap_type3, CXL_DECODER_F_TYPE3);
CXL_DECODER_FLAG_ATTR(locked, CXL_DECODER_F_LOCK);

static ssize_t target_type_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);

	switch (cxld->target_type) {
	case CXL_DECODER_ACCELERATOR:
		return sysfs_emit(buf, "accelerator\n");
	case CXL_DECODER_EXPANDER:
		return sysfs_emit(buf, "expander\n");
	}
	return -ENXIO;
}
static DEVICE_ATTR_RO(target_type);

static ssize_t target_list_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);
	ssize_t offset = 0;
	int i, rc = 0;

	device_lock(dev);
	for (i = 0; i < cxld->interleave_ways; i++) {
		struct cxl_dport *dport = cxld->target[i];
		struct cxl_dport *next = NULL;

		if (!dport)
			break;

		if (i + 1 < cxld->interleave_ways)
			next = cxld->target[i + 1];
		rc = sysfs_emit_at(buf, offset, "%d%s", dport->port_id,
				   next ? "," : "");
		if (rc < 0)
			break;
		offset += rc;
	}
	device_unlock(dev);

	if (rc < 0)
		return rc;

	rc = sysfs_emit_at(buf, offset, "\n");
	if (rc < 0)
		return rc;

	return offset + rc;
}
static DEVICE_ATTR_RO(target_list);

static struct attribute *cxl_decoder_base_attrs[] = {
	&dev_attr_start.attr,
	&dev_attr_size.attr,
	&dev_attr_locked.attr,
	&dev_attr_target_list.attr,
	NULL,
};

static struct attribute_group cxl_decoder_base_attribute_group = {
	.attrs = cxl_decoder_base_attrs,
};

static struct attribute *cxl_decoder_root_attrs[] = {
	&dev_attr_cap_pmem.attr,
	&dev_attr_cap_ram.attr,
	&dev_attr_cap_type2.attr,
	&dev_attr_cap_type3.attr,
	NULL,
};

static struct attribute_group cxl_decoder_root_attribute_group = {
	.attrs = cxl_decoder_root_attrs,
};

static const struct attribute_group *cxl_decoder_root_attribute_groups[] = {
	&cxl_decoder_root_attribute_group,
	&cxl_decoder_base_attribute_group,
	&cxl_base_attribute_group,
	NULL,
};

static struct attribute *cxl_decoder_switch_attrs[] = {
	&dev_attr_target_type.attr,
	NULL,
};

static struct attribute_group cxl_decoder_switch_attribute_group = {
	.attrs = cxl_decoder_switch_attrs,
};

static const struct attribute_group *cxl_decoder_switch_attribute_groups[] = {
	&cxl_decoder_switch_attribute_group,
	&cxl_decoder_base_attribute_group,
	&cxl_base_attribute_group,
	NULL,
};

static void cxl_decoder_release(struct device *dev)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);
	struct cxl_port *port = to_cxl_port(dev->parent);

	ida_free(&port->decoder_ida, cxld->id);
	kfree(cxld);
}

static const struct device_type cxl_decoder_switch_type = {
	.name = "cxl_decoder_switch",
	.release = cxl_decoder_release,
	.groups = cxl_decoder_switch_attribute_groups,
};

static const struct device_type cxl_decoder_root_type = {
	.name = "cxl_decoder_root",
	.release = cxl_decoder_release,
	.groups = cxl_decoder_root_attribute_groups,
};

bool is_root_decoder(struct device *dev)
{
	return dev->type == &cxl_decoder_root_type;
}
EXPORT_SYMBOL_GPL(is_root_decoder);

struct cxl_decoder *to_cxl_decoder(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type->release != cxl_decoder_release,
			  "not a cxl_decoder device\n"))
		return NULL;
	return container_of(dev, struct cxl_decoder, dev);
}
EXPORT_SYMBOL_GPL(to_cxl_decoder);

static void cxl_dport_release(struct cxl_dport *dport)
{
	list_del(&dport->list);
	put_device(dport->dport);
	kfree(dport);
}

static void cxl_port_release(struct device *dev)
{
	struct cxl_port *port = to_cxl_port(dev);
	struct cxl_dport *dport, *_d;

	device_lock(dev);
	list_for_each_entry_safe(dport, _d, &port->dports, list)
		cxl_dport_release(dport);
	device_unlock(dev);
	ida_free(&cxl_port_ida, port->id);
	kfree(port);
}

static const struct attribute_group *cxl_port_attribute_groups[] = {
	&cxl_base_attribute_group,
	NULL,
};

static const struct device_type cxl_port_type = {
	.name = "cxl_port",
	.release = cxl_port_release,
	.groups = cxl_port_attribute_groups,
};

struct cxl_port *to_cxl_port(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type != &cxl_port_type,
			  "not a cxl_port device\n"))
		return NULL;
	return container_of(dev, struct cxl_port, dev);
}

static void unregister_port(void *_port)
{
	struct cxl_port *port = _port;
	struct cxl_dport *dport;

	device_lock(&port->dev);
	list_for_each_entry(dport, &port->dports, list) {
		char link_name[CXL_TARGET_STRLEN];

		if (snprintf(link_name, CXL_TARGET_STRLEN, "dport%d",
			     dport->port_id) >= CXL_TARGET_STRLEN)
			continue;
		sysfs_remove_link(&port->dev.kobj, link_name);
	}
	device_unlock(&port->dev);
	device_unregister(&port->dev);
}

static void cxl_unlink_uport(void *_port)
{
	struct cxl_port *port = _port;

	sysfs_remove_link(&port->dev.kobj, "uport");
}

static int devm_cxl_link_uport(struct device *host, struct cxl_port *port)
{
	int rc;

	rc = sysfs_create_link(&port->dev.kobj, &port->uport->kobj, "uport");
	if (rc)
		return rc;
	return devm_add_action_or_reset(host, cxl_unlink_uport, port);
}

static struct cxl_port *cxl_port_alloc(struct device *uport,
				       resource_size_t component_reg_phys,
				       struct cxl_port *parent_port)
{
	struct cxl_port *port;
	struct device *dev;
	int rc;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	rc = ida_alloc(&cxl_port_ida, GFP_KERNEL);
	if (rc < 0)
		goto err;
	port->id = rc;

	/*
	 * The top-level cxl_port "cxl_root" does not have a cxl_port as
	 * its parent and it does not have any corresponding component
	 * registers as its decode is described by a fixed platform
	 * description.
	 */
	dev = &port->dev;
	if (parent_port)
		dev->parent = &parent_port->dev;
	else
		dev->parent = uport;

	port->uport = uport;
	port->component_reg_phys = component_reg_phys;
	ida_init(&port->decoder_ida);
	INIT_LIST_HEAD(&port->dports);

	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_port_type;

	return port;

err:
	kfree(port);
	return ERR_PTR(rc);
}

/**
 * devm_cxl_add_port - register a cxl_port in CXL memory decode hierarchy
 * @host: host device for devm operations
 * @uport: "physical" device implementing this upstream port
 * @component_reg_phys: (optional) for configurable cxl_port instances
 * @parent_port: next hop up in the CXL memory decode hierarchy
 */
struct cxl_port *devm_cxl_add_port(struct device *host, struct device *uport,
				   resource_size_t component_reg_phys,
				   struct cxl_port *parent_port)
{
	struct cxl_port *port;
	struct device *dev;
	int rc;

	port = cxl_port_alloc(uport, component_reg_phys, parent_port);
	if (IS_ERR(port))
		return port;

	dev = &port->dev;
	if (parent_port)
		rc = dev_set_name(dev, "port%d", port->id);
	else
		rc = dev_set_name(dev, "root%d", port->id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(host, unregister_port, port);
	if (rc)
		return ERR_PTR(rc);

	rc = devm_cxl_link_uport(host, port);
	if (rc)
		return ERR_PTR(rc);

	return port;

err:
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_cxl_add_port);

static struct cxl_dport *find_dport(struct cxl_port *port, int id)
{
	struct cxl_dport *dport;

	device_lock_assert(&port->dev);
	list_for_each_entry (dport, &port->dports, list)
		if (dport->port_id == id)
			return dport;
	return NULL;
}

static int add_dport(struct cxl_port *port, struct cxl_dport *new)
{
	struct cxl_dport *dup;

	device_lock(&port->dev);
	dup = find_dport(port, new->port_id);
	if (dup)
		dev_err(&port->dev,
			"unable to add dport%d-%s non-unique port id (%s)\n",
			new->port_id, dev_name(new->dport),
			dev_name(dup->dport));
	else
		list_add_tail(&new->list, &port->dports);
	device_unlock(&port->dev);

	return dup ? -EEXIST : 0;
}

/**
 * cxl_add_dport - append downstream port data to a cxl_port
 * @port: the cxl_port that references this dport
 * @dport_dev: firmware or PCI device representing the dport
 * @port_id: identifier for this dport in a decoder's target list
 * @component_reg_phys: optional location of CXL component registers
 *
 * Note that all allocations and links are undone by cxl_port deletion
 * and release.
 */
int cxl_add_dport(struct cxl_port *port, struct device *dport_dev, int port_id,
		  resource_size_t component_reg_phys)
{
	char link_name[CXL_TARGET_STRLEN];
	struct cxl_dport *dport;
	int rc;

	if (snprintf(link_name, CXL_TARGET_STRLEN, "dport%d", port_id) >=
	    CXL_TARGET_STRLEN)
		return -EINVAL;

	dport = kzalloc(sizeof(*dport), GFP_KERNEL);
	if (!dport)
		return -ENOMEM;

	INIT_LIST_HEAD(&dport->list);
	dport->dport = get_device(dport_dev);
	dport->port_id = port_id;
	dport->component_reg_phys = component_reg_phys;
	dport->port = port;

	rc = add_dport(port, dport);
	if (rc)
		goto err;

	rc = sysfs_create_link(&port->dev.kobj, &dport_dev->kobj, link_name);
	if (rc)
		goto err;

	return 0;
err:
	cxl_dport_release(dport);
	return rc;
}
EXPORT_SYMBOL_GPL(cxl_add_dport);

static struct cxl_decoder *
cxl_decoder_alloc(struct cxl_port *port, int nr_targets, resource_size_t base,
		  resource_size_t len, int interleave_ways,
		  int interleave_granularity, enum cxl_decoder_type type,
		  unsigned long flags)
{
	struct cxl_decoder *cxld;
	struct device *dev;
	int rc = 0;

	if (interleave_ways < 1)
		return ERR_PTR(-EINVAL);

	device_lock(&port->dev);
	if (list_empty(&port->dports))
		rc = -EINVAL;
	device_unlock(&port->dev);
	if (rc)
		return ERR_PTR(rc);

	cxld = kzalloc(struct_size(cxld, target, nr_targets), GFP_KERNEL);
	if (!cxld)
		return ERR_PTR(-ENOMEM);

	rc = ida_alloc(&port->decoder_ida, GFP_KERNEL);
	if (rc < 0)
		goto err;

	*cxld = (struct cxl_decoder) {
		.id = rc,
		.range = {
			.start = base,
			.end = base + len - 1,
		},
		.flags = flags,
		.interleave_ways = interleave_ways,
		.interleave_granularity = interleave_granularity,
		.target_type = type,
	};

	/* handle implied target_list */
	if (interleave_ways == 1)
		cxld->target[0] =
			list_first_entry(&port->dports, struct cxl_dport, list);
	dev = &cxld->dev;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = &port->dev;
	dev->bus = &cxl_bus_type;

	/* root ports do not have a cxl_port_type parent */
	if (port->dev.parent->type == &cxl_port_type)
		dev->type = &cxl_decoder_switch_type;
	else
		dev->type = &cxl_decoder_root_type;

	return cxld;
err:
	kfree(cxld);
	return ERR_PTR(rc);
}

static void unregister_dev(void *dev)
{
	device_unregister(dev);
}

struct cxl_decoder *
devm_cxl_add_decoder(struct device *host, struct cxl_port *port, int nr_targets,
		     resource_size_t base, resource_size_t len,
		     int interleave_ways, int interleave_granularity,
		     enum cxl_decoder_type type, unsigned long flags)
{
	struct cxl_decoder *cxld;
	struct device *dev;
	int rc;

	cxld = cxl_decoder_alloc(port, nr_targets, base, len, interleave_ways,
				 interleave_granularity, type, flags);
	if (IS_ERR(cxld))
		return cxld;

	dev = &cxld->dev;
	rc = dev_set_name(dev, "decoder%d.%d", port->id, cxld->id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(host, unregister_dev, dev);
	if (rc)
		return ERR_PTR(rc);
	return cxld;

err:
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_cxl_add_decoder);

/**
 * cxl_probe_component_regs() - Detect CXL Component register blocks
 * @dev: Host device of the @base mapping
 * @base: Mapping containing the HDM Decoder Capability Header
 * @map: Map object describing the register block information found
 *
 * See CXL 2.0 8.2.4 Component Register Layout and Definition
 * See CXL 2.0 8.2.5.5 CXL Device Register Interface
 *
 * Probe for component register information and return it in map object.
 */
void cxl_probe_component_regs(struct device *dev, void __iomem *base,
			      struct cxl_component_reg_map *map)
{
	int cap, cap_count;
	u64 cap_array;

	*map = (struct cxl_component_reg_map) { 0 };

	/*
	 * CXL.cache and CXL.mem registers are at offset 0x1000 as defined in
	 * CXL 2.0 8.2.4 Table 141.
	 */
	base += CXL_CM_OFFSET;

	cap_array = readq(base + CXL_CM_CAP_HDR_OFFSET);

	if (FIELD_GET(CXL_CM_CAP_HDR_ID_MASK, cap_array) != CM_CAP_HDR_CAP_ID) {
		dev_err(dev,
			"Couldn't locate the CXL.cache and CXL.mem capability array header./n");
		return;
	}

	/* It's assumed that future versions will be backward compatible */
	cap_count = FIELD_GET(CXL_CM_CAP_HDR_ARRAY_SIZE_MASK, cap_array);

	for (cap = 1; cap <= cap_count; cap++) {
		void __iomem *register_block;
		u32 hdr;
		int decoder_cnt;
		u16 cap_id, offset;
		u32 length;

		hdr = readl(base + cap * 0x4);

		cap_id = FIELD_GET(CXL_CM_CAP_HDR_ID_MASK, hdr);
		offset = FIELD_GET(CXL_CM_CAP_PTR_MASK, hdr);
		register_block = base + offset;

		switch (cap_id) {
		case CXL_CM_CAP_CAP_ID_HDM:
			dev_dbg(dev, "found HDM decoder capability (0x%x)\n",
				offset);

			hdr = readl(register_block);

			decoder_cnt = cxl_hdm_decoder_count(hdr);
			length = 0x20 * decoder_cnt + 0x10;

			map->hdm_decoder.valid = true;
			map->hdm_decoder.offset = CXL_CM_OFFSET + offset;
			map->hdm_decoder.size = length;
			break;
		default:
			dev_dbg(dev, "Unknown CM cap ID: %d (0x%x)\n", cap_id,
				offset);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(cxl_probe_component_regs);

static void cxl_nvdimm_bridge_release(struct device *dev)
{
	struct cxl_nvdimm_bridge *cxl_nvb = to_cxl_nvdimm_bridge(dev);

	kfree(cxl_nvb);
}

static const struct attribute_group *cxl_nvdimm_bridge_attribute_groups[] = {
	&cxl_base_attribute_group,
	NULL,
};

static const struct device_type cxl_nvdimm_bridge_type = {
	.name = "cxl_nvdimm_bridge",
	.release = cxl_nvdimm_bridge_release,
	.groups = cxl_nvdimm_bridge_attribute_groups,
};

struct cxl_nvdimm_bridge *to_cxl_nvdimm_bridge(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type != &cxl_nvdimm_bridge_type,
			  "not a cxl_nvdimm_bridge device\n"))
		return NULL;
	return container_of(dev, struct cxl_nvdimm_bridge, dev);
}
EXPORT_SYMBOL_GPL(to_cxl_nvdimm_bridge);

static struct cxl_nvdimm_bridge *
cxl_nvdimm_bridge_alloc(struct cxl_port *port)
{
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct device *dev;

	cxl_nvb = kzalloc(sizeof(*cxl_nvb), GFP_KERNEL);
	if (!cxl_nvb)
		return ERR_PTR(-ENOMEM);

	dev = &cxl_nvb->dev;
	cxl_nvb->port = port;
	cxl_nvb->state = CXL_NVB_NEW;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = &port->dev;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_nvdimm_bridge_type;

	return cxl_nvb;
}

static void unregister_nvb(void *_cxl_nvb)
{
	struct cxl_nvdimm_bridge *cxl_nvb = _cxl_nvb;
	bool flush;

	/*
	 * If the bridge was ever activated then there might be in-flight state
	 * work to flush. Once the state has been changed to 'dead' then no new
	 * work can be queued by user-triggered bind.
	 */
	device_lock(&cxl_nvb->dev);
	flush = cxl_nvb->state != CXL_NVB_NEW;
	cxl_nvb->state = CXL_NVB_DEAD;
	device_unlock(&cxl_nvb->dev);

	/*
	 * Even though the device core will trigger device_release_driver()
	 * before the unregister, it does not know about the fact that
	 * cxl_nvdimm_bridge_driver defers ->remove() work. So, do the driver
	 * release not and flush it before tearing down the nvdimm device
	 * hierarchy.
	 */
	device_release_driver(&cxl_nvb->dev);
	if (flush)
		flush_work(&cxl_nvb->state_work);
	device_unregister(&cxl_nvb->dev);
}

struct cxl_nvdimm_bridge *devm_cxl_add_nvdimm_bridge(struct device *host,
						     struct cxl_port *port)
{
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct device *dev;
	int rc;

	if (!IS_ENABLED(CONFIG_CXL_PMEM))
		return ERR_PTR(-ENXIO);

	cxl_nvb = cxl_nvdimm_bridge_alloc(port);
	if (IS_ERR(cxl_nvb))
		return cxl_nvb;

	dev = &cxl_nvb->dev;
	rc = dev_set_name(dev, "nvdimm-bridge");
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(host, unregister_nvb, cxl_nvb);
	if (rc)
		return ERR_PTR(rc);

	return cxl_nvb;

err:
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_cxl_add_nvdimm_bridge);

static void cxl_nvdimm_release(struct device *dev)
{
	struct cxl_nvdimm *cxl_nvd = to_cxl_nvdimm(dev);

	kfree(cxl_nvd);
}

static const struct attribute_group *cxl_nvdimm_attribute_groups[] = {
	&cxl_base_attribute_group,
	NULL,
};

static const struct device_type cxl_nvdimm_type = {
	.name = "cxl_nvdimm",
	.release = cxl_nvdimm_release,
	.groups = cxl_nvdimm_attribute_groups,
};

bool is_cxl_nvdimm(struct device *dev)
{
	return dev->type == &cxl_nvdimm_type;
}
EXPORT_SYMBOL_GPL(is_cxl_nvdimm);

struct cxl_nvdimm *to_cxl_nvdimm(struct device *dev)
{
	if (dev_WARN_ONCE(dev, !is_cxl_nvdimm(dev),
			  "not a cxl_nvdimm device\n"))
		return NULL;
	return container_of(dev, struct cxl_nvdimm, dev);
}
EXPORT_SYMBOL_GPL(to_cxl_nvdimm);

static struct cxl_nvdimm *cxl_nvdimm_alloc(struct cxl_memdev *cxlmd)
{
	struct cxl_nvdimm *cxl_nvd;
	struct device *dev;

	cxl_nvd = kzalloc(sizeof(*cxl_nvd), GFP_KERNEL);
	if (!cxl_nvd)
		return ERR_PTR(-ENOMEM);

	dev = &cxl_nvd->dev;
	cxl_nvd->cxlmd = cxlmd;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = &cxlmd->dev;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_nvdimm_type;

	return cxl_nvd;
}

int devm_cxl_add_nvdimm(struct device *host, struct cxl_memdev *cxlmd)
{
	struct cxl_nvdimm *cxl_nvd;
	struct device *dev;
	int rc;

	cxl_nvd = cxl_nvdimm_alloc(cxlmd);
	if (IS_ERR(cxl_nvd))
		return PTR_ERR(cxl_nvd);

	dev = &cxl_nvd->dev;
	rc = dev_set_name(dev, "pmem%d", cxlmd->id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	dev_dbg(host, "%s: register %s\n", dev_name(dev->parent),
		dev_name(dev));

	return devm_add_action_or_reset(host, unregister_dev, dev);

err:
	put_device(dev);
	return rc;
}
EXPORT_SYMBOL_GPL(devm_cxl_add_nvdimm);

/**
 * cxl_probe_device_regs() - Detect CXL Device register blocks
 * @dev: Host device of the @base mapping
 * @base: Mapping of CXL 2.0 8.2.8 CXL Device Register Interface
 * @map: Map object describing the register block information found
 *
 * Probe for device register information and return it in map object.
 */
void cxl_probe_device_regs(struct device *dev, void __iomem *base,
			   struct cxl_device_reg_map *map)
{
	int cap, cap_count;
	u64 cap_array;

	*map = (struct cxl_device_reg_map){ 0 };

	cap_array = readq(base + CXLDEV_CAP_ARRAY_OFFSET);
	if (FIELD_GET(CXLDEV_CAP_ARRAY_ID_MASK, cap_array) !=
	    CXLDEV_CAP_ARRAY_CAP_ID)
		return;

	cap_count = FIELD_GET(CXLDEV_CAP_ARRAY_COUNT_MASK, cap_array);

	for (cap = 1; cap <= cap_count; cap++) {
		u32 offset, length;
		u16 cap_id;

		cap_id = FIELD_GET(CXLDEV_CAP_HDR_CAP_ID_MASK,
				   readl(base + cap * 0x10));
		offset = readl(base + cap * 0x10 + 0x4);
		length = readl(base + cap * 0x10 + 0x8);

		switch (cap_id) {
		case CXLDEV_CAP_CAP_ID_DEVICE_STATUS:
			dev_dbg(dev, "found Status capability (0x%x)\n", offset);

			map->status.valid = true;
			map->status.offset = offset;
			map->status.size = length;
			break;
		case CXLDEV_CAP_CAP_ID_PRIMARY_MAILBOX:
			dev_dbg(dev, "found Mailbox capability (0x%x)\n", offset);
			map->mbox.valid = true;
			map->mbox.offset = offset;
			map->mbox.size = length;
			break;
		case CXLDEV_CAP_CAP_ID_SECONDARY_MAILBOX:
			dev_dbg(dev, "found Secondary Mailbox capability (0x%x)\n", offset);
			break;
		case CXLDEV_CAP_CAP_ID_MEMDEV:
			dev_dbg(dev, "found Memory Device capability (0x%x)\n", offset);
			map->memdev.valid = true;
			map->memdev.offset = offset;
			map->memdev.size = length;
			break;
		default:
			if (cap_id >= 0x8000)
				dev_dbg(dev, "Vendor cap ID: %#x offset: %#x\n", cap_id, offset);
			else
				dev_dbg(dev, "Unknown cap ID: %#x offset: %#x\n", cap_id, offset);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(cxl_probe_device_regs);

static void __iomem *devm_cxl_iomap_block(struct device *dev,
					  resource_size_t addr,
					  resource_size_t length)
{
	void __iomem *ret_val;
	struct resource *res;

	res = devm_request_mem_region(dev, addr, length, dev_name(dev));
	if (!res) {
		resource_size_t end = addr + length - 1;

		dev_err(dev, "Failed to request region %pa-%pa\n", &addr, &end);
		return NULL;
	}

	ret_val = devm_ioremap(dev, addr, length);
	if (!ret_val)
		dev_err(dev, "Failed to map region %pr\n", res);

	return ret_val;
}

int cxl_map_component_regs(struct pci_dev *pdev,
			   struct cxl_component_regs *regs,
			   struct cxl_register_map *map)
{
	struct device *dev = &pdev->dev;
	resource_size_t phys_addr;
	resource_size_t length;

	phys_addr = pci_resource_start(pdev, map->barno);
	phys_addr += map->block_offset;

	phys_addr += map->component_map.hdm_decoder.offset;
	length = map->component_map.hdm_decoder.size;
	regs->hdm_decoder = devm_cxl_iomap_block(dev, phys_addr, length);
	if (!regs->hdm_decoder)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_map_component_regs);

int cxl_map_device_regs(struct pci_dev *pdev,
			struct cxl_device_regs *regs,
			struct cxl_register_map *map)
{
	struct device *dev = &pdev->dev;
	resource_size_t phys_addr;

	phys_addr = pci_resource_start(pdev, map->barno);
	phys_addr += map->block_offset;

	if (map->device_map.status.valid) {
		resource_size_t addr;
		resource_size_t length;

		addr = phys_addr + map->device_map.status.offset;
		length = map->device_map.status.size;
		regs->status = devm_cxl_iomap_block(dev, addr, length);
		if (!regs->status)
			return -ENOMEM;
	}

	if (map->device_map.mbox.valid) {
		resource_size_t addr;
		resource_size_t length;

		addr = phys_addr + map->device_map.mbox.offset;
		length = map->device_map.mbox.size;
		regs->mbox = devm_cxl_iomap_block(dev, addr, length);
		if (!regs->mbox)
			return -ENOMEM;
	}

	if (map->device_map.memdev.valid) {
		resource_size_t addr;
		resource_size_t length;

		addr = phys_addr + map->device_map.memdev.offset;
		length = map->device_map.memdev.size;
		regs->memdev = devm_cxl_iomap_block(dev, addr, length);
		if (!regs->memdev)
			return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_map_device_regs);

/**
 * __cxl_driver_register - register a driver for the cxl bus
 * @cxl_drv: cxl driver structure to attach
 * @owner: owning module/driver
 * @modname: KBUILD_MODNAME for parent driver
 */
int __cxl_driver_register(struct cxl_driver *cxl_drv, struct module *owner,
			  const char *modname)
{
	if (!cxl_drv->probe) {
		pr_debug("%s ->probe() must be specified\n", modname);
		return -EINVAL;
	}

	if (!cxl_drv->name) {
		pr_debug("%s ->name must be specified\n", modname);
		return -EINVAL;
	}

	if (!cxl_drv->id) {
		pr_debug("%s ->id must be specified\n", modname);
		return -EINVAL;
	}

	cxl_drv->drv.bus = &cxl_bus_type;
	cxl_drv->drv.owner = owner;
	cxl_drv->drv.mod_name = modname;
	cxl_drv->drv.name = cxl_drv->name;

	return driver_register(&cxl_drv->drv);
}
EXPORT_SYMBOL_GPL(__cxl_driver_register);

void cxl_driver_unregister(struct cxl_driver *cxl_drv)
{
	driver_unregister(&cxl_drv->drv);
}
EXPORT_SYMBOL_GPL(cxl_driver_unregister);

static int cxl_device_id(struct device *dev)
{
	if (dev->type == &cxl_nvdimm_bridge_type)
		return CXL_DEVICE_NVDIMM_BRIDGE;
	if (dev->type == &cxl_nvdimm_type)
		return CXL_DEVICE_NVDIMM;
	return 0;
}

static int cxl_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return add_uevent_var(env, "MODALIAS=" CXL_MODALIAS_FMT,
			      cxl_device_id(dev));
}

static int cxl_bus_match(struct device *dev, struct device_driver *drv)
{
	return cxl_device_id(dev) == to_cxl_drv(drv)->id;
}

static int cxl_bus_probe(struct device *dev)
{
	return to_cxl_drv(dev->driver)->probe(dev);
}

static int cxl_bus_remove(struct device *dev)
{
	struct cxl_driver *cxl_drv = to_cxl_drv(dev->driver);

	if (cxl_drv->remove)
		cxl_drv->remove(dev);
	return 0;
}

struct bus_type cxl_bus_type = {
	.name = "cxl",
	.uevent = cxl_bus_uevent,
	.match = cxl_bus_match,
	.probe = cxl_bus_probe,
	.remove = cxl_bus_remove,
};
EXPORT_SYMBOL_GPL(cxl_bus_type);

static __init int cxl_core_init(void)
{
	return bus_register(&cxl_bus_type);
}

static void cxl_core_exit(void)
{
	bus_unregister(&cxl_bus_type);
}

module_init(cxl_core_init);
module_exit(cxl_core_exit);
MODULE_LICENSE("GPL v2");
