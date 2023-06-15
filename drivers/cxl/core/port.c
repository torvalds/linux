// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/memregion.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <cxlmem.h>
#include <cxlpci.h>
#include <cxl.h>
#include "core.h"

/**
 * DOC: cxl core
 *
 * The CXL core provides a set of interfaces that can be consumed by CXL aware
 * drivers. The interfaces allow for creation, modification, and destruction of
 * regions, memory devices, ports, and decoders. CXL aware drivers must register
 * with the CXL core via these interfaces in order to be able to participate in
 * cross-device interleave coordination. The CXL core also establishes and
 * maintains the bridge to the nvdimm subsystem.
 *
 * CXL core introduces sysfs hierarchy to control the devices that are
 * instantiated by the core.
 */

static DEFINE_IDA(cxl_port_ida);
static DEFINE_XARRAY(cxl_root_buses);

static ssize_t devtype_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%s\n", dev->type->name);
}
static DEVICE_ATTR_RO(devtype);

static int cxl_device_id(const struct device *dev)
{
	if (dev->type == &cxl_nvdimm_bridge_type)
		return CXL_DEVICE_NVDIMM_BRIDGE;
	if (dev->type == &cxl_nvdimm_type)
		return CXL_DEVICE_NVDIMM;
	if (dev->type == CXL_PMEM_REGION_TYPE())
		return CXL_DEVICE_PMEM_REGION;
	if (dev->type == CXL_DAX_REGION_TYPE())
		return CXL_DEVICE_DAX_REGION;
	if (is_cxl_port(dev)) {
		if (is_cxl_root(to_cxl_port(dev)))
			return CXL_DEVICE_ROOT;
		return CXL_DEVICE_PORT;
	}
	if (is_cxl_memdev(dev))
		return CXL_DEVICE_MEMORY_EXPANDER;
	if (dev->type == CXL_REGION_TYPE())
		return CXL_DEVICE_REGION;
	return 0;
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sysfs_emit(buf, CXL_MODALIAS_FMT "\n", cxl_device_id(dev));
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *cxl_base_attributes[] = {
	&dev_attr_devtype.attr,
	&dev_attr_modalias.attr,
	NULL,
};

struct attribute_group cxl_base_attribute_group = {
	.attrs = cxl_base_attributes,
};

static ssize_t start_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);

	return sysfs_emit(buf, "%#llx\n", cxld->hpa_range.start);
}
static DEVICE_ATTR_ADMIN_RO(start);

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);

	return sysfs_emit(buf, "%#llx\n", range_len(&cxld->hpa_range));
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
	case CXL_DECODER_DEVMEM:
		return sysfs_emit(buf, "accelerator\n");
	case CXL_DECODER_HOSTONLYMEM:
		return sysfs_emit(buf, "expander\n");
	}
	return -ENXIO;
}
static DEVICE_ATTR_RO(target_type);

static ssize_t emit_target_list(struct cxl_switch_decoder *cxlsd, char *buf)
{
	struct cxl_decoder *cxld = &cxlsd->cxld;
	ssize_t offset = 0;
	int i, rc = 0;

	for (i = 0; i < cxld->interleave_ways; i++) {
		struct cxl_dport *dport = cxlsd->target[i];
		struct cxl_dport *next = NULL;

		if (!dport)
			break;

		if (i + 1 < cxld->interleave_ways)
			next = cxlsd->target[i + 1];
		rc = sysfs_emit_at(buf, offset, "%d%s", dport->port_id,
				   next ? "," : "");
		if (rc < 0)
			return rc;
		offset += rc;
	}

	return offset;
}

static ssize_t target_list_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cxl_switch_decoder *cxlsd = to_cxl_switch_decoder(dev);
	ssize_t offset;
	unsigned int seq;
	int rc;

	do {
		seq = read_seqbegin(&cxlsd->target_lock);
		rc = emit_target_list(cxlsd, buf);
	} while (read_seqretry(&cxlsd->target_lock, seq));

	if (rc < 0)
		return rc;
	offset = rc;

	rc = sysfs_emit_at(buf, offset, "\n");
	if (rc < 0)
		return rc;

	return offset + rc;
}
static DEVICE_ATTR_RO(target_list);

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct cxl_endpoint_decoder *cxled = to_cxl_endpoint_decoder(dev);

	return sysfs_emit(buf, "%s\n", cxl_decoder_mode_name(cxled->mode));
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{
	struct cxl_endpoint_decoder *cxled = to_cxl_endpoint_decoder(dev);
	enum cxl_decoder_mode mode;
	ssize_t rc;

	if (sysfs_streq(buf, "pmem"))
		mode = CXL_DECODER_PMEM;
	else if (sysfs_streq(buf, "ram"))
		mode = CXL_DECODER_RAM;
	else
		return -EINVAL;

	rc = cxl_dpa_set_mode(cxled, mode);
	if (rc)
		return rc;

	return len;
}
static DEVICE_ATTR_RW(mode);

static ssize_t dpa_resource_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct cxl_endpoint_decoder *cxled = to_cxl_endpoint_decoder(dev);
	u64 base = cxl_dpa_resource_start(cxled);

	return sysfs_emit(buf, "%#llx\n", base);
}
static DEVICE_ATTR_RO(dpa_resource);

static ssize_t dpa_size_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct cxl_endpoint_decoder *cxled = to_cxl_endpoint_decoder(dev);
	resource_size_t size = cxl_dpa_size(cxled);

	return sysfs_emit(buf, "%pa\n", &size);
}

static ssize_t dpa_size_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	struct cxl_endpoint_decoder *cxled = to_cxl_endpoint_decoder(dev);
	unsigned long long size;
	ssize_t rc;

	rc = kstrtoull(buf, 0, &size);
	if (rc)
		return rc;

	if (!IS_ALIGNED(size, SZ_256M))
		return -EINVAL;

	rc = cxl_dpa_free(cxled);
	if (rc)
		return rc;

	if (size == 0)
		return len;

	rc = cxl_dpa_alloc(cxled, size);
	if (rc)
		return rc;

	return len;
}
static DEVICE_ATTR_RW(dpa_size);

static ssize_t interleave_granularity_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);

	return sysfs_emit(buf, "%d\n", cxld->interleave_granularity);
}

static DEVICE_ATTR_RO(interleave_granularity);

static ssize_t interleave_ways_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);

	return sysfs_emit(buf, "%d\n", cxld->interleave_ways);
}

static DEVICE_ATTR_RO(interleave_ways);

static struct attribute *cxl_decoder_base_attrs[] = {
	&dev_attr_start.attr,
	&dev_attr_size.attr,
	&dev_attr_locked.attr,
	&dev_attr_interleave_granularity.attr,
	&dev_attr_interleave_ways.attr,
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
	&dev_attr_target_list.attr,
	SET_CXL_REGION_ATTR(create_pmem_region)
	SET_CXL_REGION_ATTR(create_ram_region)
	SET_CXL_REGION_ATTR(delete_region)
	NULL,
};

static bool can_create_pmem(struct cxl_root_decoder *cxlrd)
{
	unsigned long flags = CXL_DECODER_F_TYPE3 | CXL_DECODER_F_PMEM;

	return (cxlrd->cxlsd.cxld.flags & flags) == flags;
}

static bool can_create_ram(struct cxl_root_decoder *cxlrd)
{
	unsigned long flags = CXL_DECODER_F_TYPE3 | CXL_DECODER_F_RAM;

	return (cxlrd->cxlsd.cxld.flags & flags) == flags;
}

static umode_t cxl_root_decoder_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev);

	if (a == CXL_REGION_ATTR(create_pmem_region) && !can_create_pmem(cxlrd))
		return 0;

	if (a == CXL_REGION_ATTR(create_ram_region) && !can_create_ram(cxlrd))
		return 0;

	if (a == CXL_REGION_ATTR(delete_region) &&
	    !(can_create_pmem(cxlrd) || can_create_ram(cxlrd)))
		return 0;

	return a->mode;
}

static struct attribute_group cxl_decoder_root_attribute_group = {
	.attrs = cxl_decoder_root_attrs,
	.is_visible = cxl_root_decoder_visible,
};

static const struct attribute_group *cxl_decoder_root_attribute_groups[] = {
	&cxl_decoder_root_attribute_group,
	&cxl_decoder_base_attribute_group,
	&cxl_base_attribute_group,
	NULL,
};

static struct attribute *cxl_decoder_switch_attrs[] = {
	&dev_attr_target_type.attr,
	&dev_attr_target_list.attr,
	SET_CXL_REGION_ATTR(region)
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

static struct attribute *cxl_decoder_endpoint_attrs[] = {
	&dev_attr_target_type.attr,
	&dev_attr_mode.attr,
	&dev_attr_dpa_size.attr,
	&dev_attr_dpa_resource.attr,
	SET_CXL_REGION_ATTR(region)
	NULL,
};

static struct attribute_group cxl_decoder_endpoint_attribute_group = {
	.attrs = cxl_decoder_endpoint_attrs,
};

static const struct attribute_group *cxl_decoder_endpoint_attribute_groups[] = {
	&cxl_decoder_base_attribute_group,
	&cxl_decoder_endpoint_attribute_group,
	&cxl_base_attribute_group,
	NULL,
};

static void __cxl_decoder_release(struct cxl_decoder *cxld)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);

	ida_free(&port->decoder_ida, cxld->id);
	put_device(&port->dev);
}

static void cxl_endpoint_decoder_release(struct device *dev)
{
	struct cxl_endpoint_decoder *cxled = to_cxl_endpoint_decoder(dev);

	__cxl_decoder_release(&cxled->cxld);
	kfree(cxled);
}

static void cxl_switch_decoder_release(struct device *dev)
{
	struct cxl_switch_decoder *cxlsd = to_cxl_switch_decoder(dev);

	__cxl_decoder_release(&cxlsd->cxld);
	kfree(cxlsd);
}

struct cxl_root_decoder *to_cxl_root_decoder(struct device *dev)
{
	if (dev_WARN_ONCE(dev, !is_root_decoder(dev),
			  "not a cxl_root_decoder device\n"))
		return NULL;
	return container_of(dev, struct cxl_root_decoder, cxlsd.cxld.dev);
}
EXPORT_SYMBOL_NS_GPL(to_cxl_root_decoder, CXL);

static void cxl_root_decoder_release(struct device *dev)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev);

	if (atomic_read(&cxlrd->region_id) >= 0)
		memregion_free(atomic_read(&cxlrd->region_id));
	__cxl_decoder_release(&cxlrd->cxlsd.cxld);
	kfree(cxlrd);
}

static const struct device_type cxl_decoder_endpoint_type = {
	.name = "cxl_decoder_endpoint",
	.release = cxl_endpoint_decoder_release,
	.groups = cxl_decoder_endpoint_attribute_groups,
};

static const struct device_type cxl_decoder_switch_type = {
	.name = "cxl_decoder_switch",
	.release = cxl_switch_decoder_release,
	.groups = cxl_decoder_switch_attribute_groups,
};

static const struct device_type cxl_decoder_root_type = {
	.name = "cxl_decoder_root",
	.release = cxl_root_decoder_release,
	.groups = cxl_decoder_root_attribute_groups,
};

bool is_endpoint_decoder(struct device *dev)
{
	return dev->type == &cxl_decoder_endpoint_type;
}
EXPORT_SYMBOL_NS_GPL(is_endpoint_decoder, CXL);

bool is_root_decoder(struct device *dev)
{
	return dev->type == &cxl_decoder_root_type;
}
EXPORT_SYMBOL_NS_GPL(is_root_decoder, CXL);

bool is_switch_decoder(struct device *dev)
{
	return is_root_decoder(dev) || dev->type == &cxl_decoder_switch_type;
}
EXPORT_SYMBOL_NS_GPL(is_switch_decoder, CXL);

struct cxl_decoder *to_cxl_decoder(struct device *dev)
{
	if (dev_WARN_ONCE(dev,
			  !is_switch_decoder(dev) && !is_endpoint_decoder(dev),
			  "not a cxl_decoder device\n"))
		return NULL;
	return container_of(dev, struct cxl_decoder, dev);
}
EXPORT_SYMBOL_NS_GPL(to_cxl_decoder, CXL);

struct cxl_endpoint_decoder *to_cxl_endpoint_decoder(struct device *dev)
{
	if (dev_WARN_ONCE(dev, !is_endpoint_decoder(dev),
			  "not a cxl_endpoint_decoder device\n"))
		return NULL;
	return container_of(dev, struct cxl_endpoint_decoder, cxld.dev);
}
EXPORT_SYMBOL_NS_GPL(to_cxl_endpoint_decoder, CXL);

struct cxl_switch_decoder *to_cxl_switch_decoder(struct device *dev)
{
	if (dev_WARN_ONCE(dev, !is_switch_decoder(dev),
			  "not a cxl_switch_decoder device\n"))
		return NULL;
	return container_of(dev, struct cxl_switch_decoder, cxld.dev);
}
EXPORT_SYMBOL_NS_GPL(to_cxl_switch_decoder, CXL);

static void cxl_ep_release(struct cxl_ep *ep)
{
	put_device(ep->ep);
	kfree(ep);
}

static void cxl_ep_remove(struct cxl_port *port, struct cxl_ep *ep)
{
	if (!ep)
		return;
	xa_erase(&port->endpoints, (unsigned long) ep->ep);
	cxl_ep_release(ep);
}

static void cxl_port_release(struct device *dev)
{
	struct cxl_port *port = to_cxl_port(dev);
	unsigned long index;
	struct cxl_ep *ep;

	xa_for_each(&port->endpoints, index, ep)
		cxl_ep_remove(port, ep);
	xa_destroy(&port->endpoints);
	xa_destroy(&port->dports);
	xa_destroy(&port->regions);
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

bool is_cxl_port(const struct device *dev)
{
	return dev->type == &cxl_port_type;
}
EXPORT_SYMBOL_NS_GPL(is_cxl_port, CXL);

struct cxl_port *to_cxl_port(const struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type != &cxl_port_type,
			  "not a cxl_port device\n"))
		return NULL;
	return container_of(dev, struct cxl_port, dev);
}
EXPORT_SYMBOL_NS_GPL(to_cxl_port, CXL);

static void unregister_port(void *_port)
{
	struct cxl_port *port = _port;
	struct cxl_port *parent;
	struct device *lock_dev;

	if (is_cxl_root(port))
		parent = NULL;
	else
		parent = to_cxl_port(port->dev.parent);

	/*
	 * CXL root port's and the first level of ports are unregistered
	 * under the platform firmware device lock, all other ports are
	 * unregistered while holding their parent port lock.
	 */
	if (!parent)
		lock_dev = port->uport;
	else if (is_cxl_root(parent))
		lock_dev = parent->uport;
	else
		lock_dev = &parent->dev;

	device_lock_assert(lock_dev);
	port->dead = true;
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

static void cxl_unlink_parent_dport(void *_port)
{
	struct cxl_port *port = _port;

	sysfs_remove_link(&port->dev.kobj, "parent_dport");
}

static int devm_cxl_link_parent_dport(struct device *host,
				      struct cxl_port *port,
				      struct cxl_dport *parent_dport)
{
	int rc;

	if (!parent_dport)
		return 0;

	rc = sysfs_create_link(&port->dev.kobj, &parent_dport->dport->kobj,
			       "parent_dport");
	if (rc)
		return rc;
	return devm_add_action_or_reset(host, cxl_unlink_parent_dport, port);
}

static struct lock_class_key cxl_port_key;

static struct cxl_port *cxl_port_alloc(struct device *uport,
				       resource_size_t component_reg_phys,
				       struct cxl_dport *parent_dport)
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
	port->uport = uport;

	/*
	 * The top-level cxl_port "cxl_root" does not have a cxl_port as
	 * its parent and it does not have any corresponding component
	 * registers as its decode is described by a fixed platform
	 * description.
	 */
	dev = &port->dev;
	if (parent_dport) {
		struct cxl_port *parent_port = parent_dport->port;
		struct cxl_port *iter;

		dev->parent = &parent_port->dev;
		port->depth = parent_port->depth + 1;
		port->parent_dport = parent_dport;

		/*
		 * walk to the host bridge, or the first ancestor that knows
		 * the host bridge
		 */
		iter = port;
		while (!iter->host_bridge &&
		       !is_cxl_root(to_cxl_port(iter->dev.parent)))
			iter = to_cxl_port(iter->dev.parent);
		if (iter->host_bridge)
			port->host_bridge = iter->host_bridge;
		else if (parent_dport->rch)
			port->host_bridge = parent_dport->dport;
		else
			port->host_bridge = iter->uport;
		dev_dbg(uport, "host-bridge: %s\n", dev_name(port->host_bridge));
	} else
		dev->parent = uport;

	port->component_reg_phys = component_reg_phys;
	ida_init(&port->decoder_ida);
	port->hdm_end = -1;
	port->commit_end = -1;
	xa_init(&port->dports);
	xa_init(&port->endpoints);
	xa_init(&port->regions);

	device_initialize(dev);
	lockdep_set_class_and_subclass(&dev->mutex, &cxl_port_key, port->depth);
	device_set_pm_not_required(dev);
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_port_type;

	return port;

err:
	kfree(port);
	return ERR_PTR(rc);
}

static struct cxl_port *__devm_cxl_add_port(struct device *host,
					    struct device *uport,
					    resource_size_t component_reg_phys,
					    struct cxl_dport *parent_dport)
{
	struct cxl_port *port;
	struct device *dev;
	int rc;

	port = cxl_port_alloc(uport, component_reg_phys, parent_dport);
	if (IS_ERR(port))
		return port;

	dev = &port->dev;
	if (is_cxl_memdev(uport))
		rc = dev_set_name(dev, "endpoint%d", port->id);
	else if (parent_dport)
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

	rc = devm_cxl_link_parent_dport(host, port, parent_dport);
	if (rc)
		return ERR_PTR(rc);

	return port;

err:
	put_device(dev);
	return ERR_PTR(rc);
}

/**
 * devm_cxl_add_port - register a cxl_port in CXL memory decode hierarchy
 * @host: host device for devm operations
 * @uport: "physical" device implementing this upstream port
 * @component_reg_phys: (optional) for configurable cxl_port instances
 * @parent_dport: next hop up in the CXL memory decode hierarchy
 */
struct cxl_port *devm_cxl_add_port(struct device *host, struct device *uport,
				   resource_size_t component_reg_phys,
				   struct cxl_dport *parent_dport)
{
	struct cxl_port *port, *parent_port;

	port = __devm_cxl_add_port(host, uport, component_reg_phys,
				   parent_dport);

	parent_port = parent_dport ? parent_dport->port : NULL;
	if (IS_ERR(port)) {
		dev_dbg(uport, "Failed to add%s%s%s: %ld\n",
			parent_port ? " port to " : "",
			parent_port ? dev_name(&parent_port->dev) : "",
			parent_port ? "" : " root port",
			PTR_ERR(port));
	} else {
		dev_dbg(uport, "%s added%s%s%s\n",
			dev_name(&port->dev),
			parent_port ? " to " : "",
			parent_port ? dev_name(&parent_port->dev) : "",
			parent_port ? "" : " (root port)");
	}

	return port;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_port, CXL);

struct pci_bus *cxl_port_to_pci_bus(struct cxl_port *port)
{
	/* There is no pci_bus associated with a CXL platform-root port */
	if (is_cxl_root(port))
		return NULL;

	if (dev_is_pci(port->uport)) {
		struct pci_dev *pdev = to_pci_dev(port->uport);

		return pdev->subordinate;
	}

	return xa_load(&cxl_root_buses, (unsigned long)port->uport);
}
EXPORT_SYMBOL_NS_GPL(cxl_port_to_pci_bus, CXL);

static void unregister_pci_bus(void *uport)
{
	xa_erase(&cxl_root_buses, (unsigned long)uport);
}

int devm_cxl_register_pci_bus(struct device *host, struct device *uport,
			      struct pci_bus *bus)
{
	int rc;

	if (dev_is_pci(uport))
		return -EINVAL;

	rc = xa_insert(&cxl_root_buses, (unsigned long)uport, bus, GFP_KERNEL);
	if (rc)
		return rc;
	return devm_add_action_or_reset(host, unregister_pci_bus, uport);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_register_pci_bus, CXL);

static bool dev_is_cxl_root_child(struct device *dev)
{
	struct cxl_port *port, *parent;

	if (!is_cxl_port(dev))
		return false;

	port = to_cxl_port(dev);
	if (is_cxl_root(port))
		return false;

	parent = to_cxl_port(port->dev.parent);
	if (is_cxl_root(parent))
		return true;

	return false;
}

struct cxl_port *find_cxl_root(struct cxl_port *port)
{
	struct cxl_port *iter = port;

	while (iter && !is_cxl_root(iter))
		iter = to_cxl_port(iter->dev.parent);

	if (!iter)
		return NULL;
	get_device(&iter->dev);
	return iter;
}
EXPORT_SYMBOL_NS_GPL(find_cxl_root, CXL);

static struct cxl_dport *find_dport(struct cxl_port *port, int id)
{
	struct cxl_dport *dport;
	unsigned long index;

	device_lock_assert(&port->dev);
	xa_for_each(&port->dports, index, dport)
		if (dport->port_id == id)
			return dport;
	return NULL;
}

static int add_dport(struct cxl_port *port, struct cxl_dport *new)
{
	struct cxl_dport *dup;
	int rc;

	device_lock_assert(&port->dev);
	dup = find_dport(port, new->port_id);
	if (dup) {
		dev_err(&port->dev,
			"unable to add dport%d-%s non-unique port id (%s)\n",
			new->port_id, dev_name(new->dport),
			dev_name(dup->dport));
		return -EBUSY;
	}

	rc = xa_insert(&port->dports, (unsigned long)new->dport, new,
		       GFP_KERNEL);
	if (rc)
		return rc;

	port->nr_dports++;
	return 0;
}

/*
 * Since root-level CXL dports cannot be enumerated by PCI they are not
 * enumerated by the common port driver that acquires the port lock over
 * dport add/remove. Instead, root dports are manually added by a
 * platform driver and cond_cxl_root_lock() is used to take the missing
 * port lock in that case.
 */
static void cond_cxl_root_lock(struct cxl_port *port)
{
	if (is_cxl_root(port))
		device_lock(&port->dev);
}

static void cond_cxl_root_unlock(struct cxl_port *port)
{
	if (is_cxl_root(port))
		device_unlock(&port->dev);
}

static void cxl_dport_remove(void *data)
{
	struct cxl_dport *dport = data;
	struct cxl_port *port = dport->port;

	xa_erase(&port->dports, (unsigned long) dport->dport);
	put_device(dport->dport);
}

static void cxl_dport_unlink(void *data)
{
	struct cxl_dport *dport = data;
	struct cxl_port *port = dport->port;
	char link_name[CXL_TARGET_STRLEN];

	sprintf(link_name, "dport%d", dport->port_id);
	sysfs_remove_link(&port->dev.kobj, link_name);
}

static struct cxl_dport *
__devm_cxl_add_dport(struct cxl_port *port, struct device *dport_dev,
		     int port_id, resource_size_t component_reg_phys,
		     resource_size_t rcrb)
{
	char link_name[CXL_TARGET_STRLEN];
	struct cxl_dport *dport;
	struct device *host;
	int rc;

	if (is_cxl_root(port))
		host = port->uport;
	else
		host = &port->dev;

	if (!host->driver) {
		dev_WARN_ONCE(&port->dev, 1, "dport:%s bad devm context\n",
			      dev_name(dport_dev));
		return ERR_PTR(-ENXIO);
	}

	if (snprintf(link_name, CXL_TARGET_STRLEN, "dport%d", port_id) >=
	    CXL_TARGET_STRLEN)
		return ERR_PTR(-EINVAL);

	dport = devm_kzalloc(host, sizeof(*dport), GFP_KERNEL);
	if (!dport)
		return ERR_PTR(-ENOMEM);

	dport->dport = dport_dev;
	dport->port_id = port_id;
	dport->component_reg_phys = component_reg_phys;
	dport->port = port;
	if (rcrb != CXL_RESOURCE_NONE)
		dport->rch = true;
	dport->rcrb = rcrb;

	cond_cxl_root_lock(port);
	rc = add_dport(port, dport);
	cond_cxl_root_unlock(port);
	if (rc)
		return ERR_PTR(rc);

	get_device(dport_dev);
	rc = devm_add_action_or_reset(host, cxl_dport_remove, dport);
	if (rc)
		return ERR_PTR(rc);

	rc = sysfs_create_link(&port->dev.kobj, &dport_dev->kobj, link_name);
	if (rc)
		return ERR_PTR(rc);

	rc = devm_add_action_or_reset(host, cxl_dport_unlink, dport);
	if (rc)
		return ERR_PTR(rc);

	return dport;
}

/**
 * devm_cxl_add_dport - append VH downstream port data to a cxl_port
 * @port: the cxl_port that references this dport
 * @dport_dev: firmware or PCI device representing the dport
 * @port_id: identifier for this dport in a decoder's target list
 * @component_reg_phys: optional location of CXL component registers
 *
 * Note that dports are appended to the devm release action's of the
 * either the port's host (for root ports), or the port itself (for
 * switch ports)
 */
struct cxl_dport *devm_cxl_add_dport(struct cxl_port *port,
				     struct device *dport_dev, int port_id,
				     resource_size_t component_reg_phys)
{
	struct cxl_dport *dport;

	dport = __devm_cxl_add_dport(port, dport_dev, port_id,
				     component_reg_phys, CXL_RESOURCE_NONE);
	if (IS_ERR(dport)) {
		dev_dbg(dport_dev, "failed to add dport to %s: %ld\n",
			dev_name(&port->dev), PTR_ERR(dport));
	} else {
		dev_dbg(dport_dev, "dport added to %s\n",
			dev_name(&port->dev));
	}

	return dport;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_dport, CXL);

/**
 * devm_cxl_add_rch_dport - append RCH downstream port data to a cxl_port
 * @port: the cxl_port that references this dport
 * @dport_dev: firmware or PCI device representing the dport
 * @port_id: identifier for this dport in a decoder's target list
 * @component_reg_phys: optional location of CXL component registers
 * @rcrb: mandatory location of a Root Complex Register Block
 *
 * See CXL 3.0 9.11.8 CXL Devices Attached to an RCH
 */
struct cxl_dport *devm_cxl_add_rch_dport(struct cxl_port *port,
					 struct device *dport_dev, int port_id,
					 resource_size_t component_reg_phys,
					 resource_size_t rcrb)
{
	struct cxl_dport *dport;

	if (rcrb == CXL_RESOURCE_NONE) {
		dev_dbg(&port->dev, "failed to add RCH dport, missing RCRB\n");
		return ERR_PTR(-EINVAL);
	}

	dport = __devm_cxl_add_dport(port, dport_dev, port_id,
				     component_reg_phys, rcrb);
	if (IS_ERR(dport)) {
		dev_dbg(dport_dev, "failed to add RCH dport to %s: %ld\n",
			dev_name(&port->dev), PTR_ERR(dport));
	} else {
		dev_dbg(dport_dev, "RCH dport added to %s\n",
			dev_name(&port->dev));
	}

	return dport;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_rch_dport, CXL);

static int add_ep(struct cxl_ep *new)
{
	struct cxl_port *port = new->dport->port;
	int rc;

	device_lock(&port->dev);
	if (port->dead) {
		device_unlock(&port->dev);
		return -ENXIO;
	}
	rc = xa_insert(&port->endpoints, (unsigned long)new->ep, new,
		       GFP_KERNEL);
	device_unlock(&port->dev);

	return rc;
}

/**
 * cxl_add_ep - register an endpoint's interest in a port
 * @dport: the dport that routes to @ep_dev
 * @ep_dev: device representing the endpoint
 *
 * Intermediate CXL ports are scanned based on the arrival of endpoints.
 * When those endpoints depart the port can be destroyed once all
 * endpoints that care about that port have been removed.
 */
static int cxl_add_ep(struct cxl_dport *dport, struct device *ep_dev)
{
	struct cxl_ep *ep;
	int rc;

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	ep->ep = get_device(ep_dev);
	ep->dport = dport;

	rc = add_ep(ep);
	if (rc)
		cxl_ep_release(ep);
	return rc;
}

struct cxl_find_port_ctx {
	const struct device *dport_dev;
	const struct cxl_port *parent_port;
	struct cxl_dport **dport;
};

static int match_port_by_dport(struct device *dev, const void *data)
{
	const struct cxl_find_port_ctx *ctx = data;
	struct cxl_dport *dport;
	struct cxl_port *port;

	if (!is_cxl_port(dev))
		return 0;
	if (ctx->parent_port && dev->parent != &ctx->parent_port->dev)
		return 0;

	port = to_cxl_port(dev);
	dport = cxl_find_dport_by_dev(port, ctx->dport_dev);
	if (ctx->dport)
		*ctx->dport = dport;
	return dport != NULL;
}

static struct cxl_port *__find_cxl_port(struct cxl_find_port_ctx *ctx)
{
	struct device *dev;

	if (!ctx->dport_dev)
		return NULL;

	dev = bus_find_device(&cxl_bus_type, NULL, ctx, match_port_by_dport);
	if (dev)
		return to_cxl_port(dev);
	return NULL;
}

static struct cxl_port *find_cxl_port(struct device *dport_dev,
				      struct cxl_dport **dport)
{
	struct cxl_find_port_ctx ctx = {
		.dport_dev = dport_dev,
		.dport = dport,
	};
	struct cxl_port *port;

	port = __find_cxl_port(&ctx);
	return port;
}

static struct cxl_port *find_cxl_port_at(struct cxl_port *parent_port,
					 struct device *dport_dev,
					 struct cxl_dport **dport)
{
	struct cxl_find_port_ctx ctx = {
		.dport_dev = dport_dev,
		.parent_port = parent_port,
		.dport = dport,
	};
	struct cxl_port *port;

	port = __find_cxl_port(&ctx);
	return port;
}

/*
 * All users of grandparent() are using it to walk PCIe-like switch port
 * hierarchy. A PCIe switch is comprised of a bridge device representing the
 * upstream switch port and N bridges representing downstream switch ports. When
 * bridges stack the grand-parent of a downstream switch port is another
 * downstream switch port in the immediate ancestor switch.
 */
static struct device *grandparent(struct device *dev)
{
	if (dev && dev->parent)
		return dev->parent->parent;
	return NULL;
}

static void delete_endpoint(void *data)
{
	struct cxl_memdev *cxlmd = data;
	struct cxl_port *endpoint = dev_get_drvdata(&cxlmd->dev);
	struct cxl_port *parent_port;
	struct device *parent;

	parent_port = cxl_mem_find_port(cxlmd, NULL);
	if (!parent_port)
		goto out;
	parent = &parent_port->dev;

	device_lock(parent);
	if (parent->driver && !endpoint->dead) {
		devm_release_action(parent, cxl_unlink_parent_dport, endpoint);
		devm_release_action(parent, cxl_unlink_uport, endpoint);
		devm_release_action(parent, unregister_port, endpoint);
	}
	device_unlock(parent);
	put_device(parent);
out:
	put_device(&endpoint->dev);
}

int cxl_endpoint_autoremove(struct cxl_memdev *cxlmd, struct cxl_port *endpoint)
{
	struct device *dev = &cxlmd->dev;

	get_device(&endpoint->dev);
	dev_set_drvdata(dev, endpoint);
	cxlmd->depth = endpoint->depth;
	return devm_add_action_or_reset(dev, delete_endpoint, cxlmd);
}
EXPORT_SYMBOL_NS_GPL(cxl_endpoint_autoremove, CXL);

/*
 * The natural end of life of a non-root 'cxl_port' is when its parent port goes
 * through a ->remove() event ("top-down" unregistration). The unnatural trigger
 * for a port to be unregistered is when all memdevs beneath that port have gone
 * through ->remove(). This "bottom-up" removal selectively removes individual
 * child ports manually. This depends on devm_cxl_add_port() to not change is
 * devm action registration order, and for dports to have already been
 * destroyed by reap_dports().
 */
static void delete_switch_port(struct cxl_port *port)
{
	devm_release_action(port->dev.parent, cxl_unlink_parent_dport, port);
	devm_release_action(port->dev.parent, cxl_unlink_uport, port);
	devm_release_action(port->dev.parent, unregister_port, port);
}

static void reap_dports(struct cxl_port *port)
{
	struct cxl_dport *dport;
	unsigned long index;

	device_lock_assert(&port->dev);

	xa_for_each(&port->dports, index, dport) {
		devm_release_action(&port->dev, cxl_dport_unlink, dport);
		devm_release_action(&port->dev, cxl_dport_remove, dport);
		devm_kfree(&port->dev, dport);
	}
}

struct detach_ctx {
	struct cxl_memdev *cxlmd;
	int depth;
};

static int port_has_memdev(struct device *dev, const void *data)
{
	const struct detach_ctx *ctx = data;
	struct cxl_port *port;

	if (!is_cxl_port(dev))
		return 0;

	port = to_cxl_port(dev);
	if (port->depth != ctx->depth)
		return 0;

	return !!cxl_ep_load(port, ctx->cxlmd);
}

static void cxl_detach_ep(void *data)
{
	struct cxl_memdev *cxlmd = data;

	for (int i = cxlmd->depth - 1; i >= 1; i--) {
		struct cxl_port *port, *parent_port;
		struct detach_ctx ctx = {
			.cxlmd = cxlmd,
			.depth = i,
		};
		struct device *dev;
		struct cxl_ep *ep;
		bool died = false;

		dev = bus_find_device(&cxl_bus_type, NULL, &ctx,
				      port_has_memdev);
		if (!dev)
			continue;
		port = to_cxl_port(dev);

		parent_port = to_cxl_port(port->dev.parent);
		device_lock(&parent_port->dev);
		device_lock(&port->dev);
		ep = cxl_ep_load(port, cxlmd);
		dev_dbg(&cxlmd->dev, "disconnect %s from %s\n",
			ep ? dev_name(ep->ep) : "", dev_name(&port->dev));
		cxl_ep_remove(port, ep);
		if (ep && !port->dead && xa_empty(&port->endpoints) &&
		    !is_cxl_root(parent_port) && parent_port->dev.driver) {
			/*
			 * This was the last ep attached to a dynamically
			 * enumerated port. Block new cxl_add_ep() and garbage
			 * collect the port.
			 */
			died = true;
			port->dead = true;
			reap_dports(port);
		}
		device_unlock(&port->dev);

		if (died) {
			dev_dbg(&cxlmd->dev, "delete %s\n",
				dev_name(&port->dev));
			delete_switch_port(port);
		}
		put_device(&port->dev);
		device_unlock(&parent_port->dev);
	}
}

static resource_size_t find_component_registers(struct device *dev)
{
	struct cxl_register_map map;
	struct pci_dev *pdev;

	/*
	 * Theoretically, CXL component registers can be hosted on a
	 * non-PCI device, in practice, only cxl_test hits this case.
	 */
	if (!dev_is_pci(dev))
		return CXL_RESOURCE_NONE;

	pdev = to_pci_dev(dev);

	cxl_find_regblock(pdev, CXL_REGLOC_RBI_COMPONENT, &map);
	return map.resource;
}

static int add_port_attach_ep(struct cxl_memdev *cxlmd,
			      struct device *uport_dev,
			      struct device *dport_dev)
{
	struct device *dparent = grandparent(dport_dev);
	struct cxl_port *port, *parent_port = NULL;
	struct cxl_dport *dport, *parent_dport;
	resource_size_t component_reg_phys;
	int rc;

	if (!dparent) {
		/*
		 * The iteration reached the topology root without finding the
		 * CXL-root 'cxl_port' on a previous iteration, fail for now to
		 * be re-probed after platform driver attaches.
		 */
		dev_dbg(&cxlmd->dev, "%s is a root dport\n",
			dev_name(dport_dev));
		return -ENXIO;
	}

	parent_port = find_cxl_port(dparent, &parent_dport);
	if (!parent_port) {
		/* iterate to create this parent_port */
		return -EAGAIN;
	}

	device_lock(&parent_port->dev);
	if (!parent_port->dev.driver) {
		dev_warn(&cxlmd->dev,
			 "port %s:%s disabled, failed to enumerate CXL.mem\n",
			 dev_name(&parent_port->dev), dev_name(uport_dev));
		port = ERR_PTR(-ENXIO);
		goto out;
	}

	port = find_cxl_port_at(parent_port, dport_dev, &dport);
	if (!port) {
		component_reg_phys = find_component_registers(uport_dev);
		port = devm_cxl_add_port(&parent_port->dev, uport_dev,
					 component_reg_phys, parent_dport);
		/* retry find to pick up the new dport information */
		if (!IS_ERR(port))
			port = find_cxl_port_at(parent_port, dport_dev, &dport);
	}
out:
	device_unlock(&parent_port->dev);

	if (IS_ERR(port))
		rc = PTR_ERR(port);
	else {
		dev_dbg(&cxlmd->dev, "add to new port %s:%s\n",
			dev_name(&port->dev), dev_name(port->uport));
		rc = cxl_add_ep(dport, &cxlmd->dev);
		if (rc == -EBUSY) {
			/*
			 * "can't" happen, but this error code means
			 * something to the caller, so translate it.
			 */
			rc = -ENXIO;
		}
		put_device(&port->dev);
	}

	put_device(&parent_port->dev);
	return rc;
}

int devm_cxl_enumerate_ports(struct cxl_memdev *cxlmd)
{
	struct device *dev = &cxlmd->dev;
	struct device *iter;
	int rc;

	/*
	 * Skip intermediate port enumeration in the RCH case, there
	 * are no ports in between a host bridge and an endpoint.
	 */
	if (cxlmd->cxlds->rcd)
		return 0;

	rc = devm_add_action_or_reset(&cxlmd->dev, cxl_detach_ep, cxlmd);
	if (rc)
		return rc;

	/*
	 * Scan for and add all cxl_ports in this device's ancestry.
	 * Repeat until no more ports are added. Abort if a port add
	 * attempt fails.
	 */
retry:
	for (iter = dev; iter; iter = grandparent(iter)) {
		struct device *dport_dev = grandparent(iter);
		struct device *uport_dev;
		struct cxl_dport *dport;
		struct cxl_port *port;

		if (!dport_dev)
			return 0;

		uport_dev = dport_dev->parent;
		if (!uport_dev) {
			dev_warn(dev, "at %s no parent for dport: %s\n",
				 dev_name(iter), dev_name(dport_dev));
			return -ENXIO;
		}

		dev_dbg(dev, "scan: iter: %s dport_dev: %s parent: %s\n",
			dev_name(iter), dev_name(dport_dev),
			dev_name(uport_dev));
		port = find_cxl_port(dport_dev, &dport);
		if (port) {
			dev_dbg(&cxlmd->dev,
				"found already registered port %s:%s\n",
				dev_name(&port->dev), dev_name(port->uport));
			rc = cxl_add_ep(dport, &cxlmd->dev);

			/*
			 * If the endpoint already exists in the port's list,
			 * that's ok, it was added on a previous pass.
			 * Otherwise, retry in add_port_attach_ep() after taking
			 * the parent_port lock as the current port may be being
			 * reaped.
			 */
			if (rc && rc != -EBUSY) {
				put_device(&port->dev);
				return rc;
			}

			/* Any more ports to add between this one and the root? */
			if (!dev_is_cxl_root_child(&port->dev)) {
				put_device(&port->dev);
				continue;
			}

			put_device(&port->dev);
			return 0;
		}

		rc = add_port_attach_ep(cxlmd, uport_dev, dport_dev);
		/* port missing, try to add parent */
		if (rc == -EAGAIN)
			continue;
		/* failed to add ep or port */
		if (rc)
			return rc;
		/* port added, new descendants possible, start over */
		goto retry;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_enumerate_ports, CXL);

struct cxl_port *cxl_mem_find_port(struct cxl_memdev *cxlmd,
				   struct cxl_dport **dport)
{
	return find_cxl_port(grandparent(&cxlmd->dev), dport);
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_find_port, CXL);

static int decoder_populate_targets(struct cxl_switch_decoder *cxlsd,
				    struct cxl_port *port, int *target_map)
{
	int i, rc = 0;

	if (!target_map)
		return 0;

	device_lock_assert(&port->dev);

	if (xa_empty(&port->dports))
		return -EINVAL;

	write_seqlock(&cxlsd->target_lock);
	for (i = 0; i < cxlsd->nr_targets; i++) {
		struct cxl_dport *dport = find_dport(port, target_map[i]);

		if (!dport) {
			rc = -ENXIO;
			break;
		}
		cxlsd->target[i] = dport;
	}
	write_sequnlock(&cxlsd->target_lock);

	return rc;
}

struct cxl_dport *cxl_hb_modulo(struct cxl_root_decoder *cxlrd, int pos)
{
	struct cxl_switch_decoder *cxlsd = &cxlrd->cxlsd;
	struct cxl_decoder *cxld = &cxlsd->cxld;
	int iw;

	iw = cxld->interleave_ways;
	if (dev_WARN_ONCE(&cxld->dev, iw != cxlsd->nr_targets,
			  "misconfigured root decoder\n"))
		return NULL;

	return cxlrd->cxlsd.target[pos % iw];
}
EXPORT_SYMBOL_NS_GPL(cxl_hb_modulo, CXL);

static struct lock_class_key cxl_decoder_key;

/**
 * cxl_decoder_init - Common decoder setup / initialization
 * @port: owning port of this decoder
 * @cxld: common decoder properties to initialize
 *
 * A port may contain one or more decoders. Each of those decoders
 * enable some address space for CXL.mem utilization. A decoder is
 * expected to be configured by the caller before registering via
 * cxl_decoder_add()
 */
static int cxl_decoder_init(struct cxl_port *port, struct cxl_decoder *cxld)
{
	struct device *dev;
	int rc;

	rc = ida_alloc(&port->decoder_ida, GFP_KERNEL);
	if (rc < 0)
		return rc;

	/* need parent to stick around to release the id */
	get_device(&port->dev);
	cxld->id = rc;

	dev = &cxld->dev;
	device_initialize(dev);
	lockdep_set_class(&dev->mutex, &cxl_decoder_key);
	device_set_pm_not_required(dev);
	dev->parent = &port->dev;
	dev->bus = &cxl_bus_type;

	/* Pre initialize an "empty" decoder */
	cxld->interleave_ways = 1;
	cxld->interleave_granularity = PAGE_SIZE;
	cxld->target_type = CXL_DECODER_HOSTONLYMEM;
	cxld->hpa_range = (struct range) {
		.start = 0,
		.end = -1,
	};

	return 0;
}

static int cxl_switch_decoder_init(struct cxl_port *port,
				   struct cxl_switch_decoder *cxlsd,
				   int nr_targets)
{
	if (nr_targets > CXL_DECODER_MAX_INTERLEAVE)
		return -EINVAL;

	cxlsd->nr_targets = nr_targets;
	seqlock_init(&cxlsd->target_lock);
	return cxl_decoder_init(port, &cxlsd->cxld);
}

/**
 * cxl_root_decoder_alloc - Allocate a root level decoder
 * @port: owning CXL root of this decoder
 * @nr_targets: static number of downstream targets
 * @calc_hb: which host bridge covers the n'th position by granularity
 *
 * Return: A new cxl decoder to be registered by cxl_decoder_add(). A
 * 'CXL root' decoder is one that decodes from a top-level / static platform
 * firmware description of CXL resources into a CXL standard decode
 * topology.
 */
struct cxl_root_decoder *cxl_root_decoder_alloc(struct cxl_port *port,
						unsigned int nr_targets,
						cxl_calc_hb_fn calc_hb)
{
	struct cxl_root_decoder *cxlrd;
	struct cxl_switch_decoder *cxlsd;
	struct cxl_decoder *cxld;
	int rc;

	if (!is_cxl_root(port))
		return ERR_PTR(-EINVAL);

	cxlrd = kzalloc(struct_size(cxlrd, cxlsd.target, nr_targets),
			GFP_KERNEL);
	if (!cxlrd)
		return ERR_PTR(-ENOMEM);

	cxlsd = &cxlrd->cxlsd;
	rc = cxl_switch_decoder_init(port, cxlsd, nr_targets);
	if (rc) {
		kfree(cxlrd);
		return ERR_PTR(rc);
	}

	cxlrd->calc_hb = calc_hb;
	mutex_init(&cxlrd->range_lock);

	cxld = &cxlsd->cxld;
	cxld->dev.type = &cxl_decoder_root_type;
	/*
	 * cxl_root_decoder_release() special cases negative ids to
	 * detect memregion_alloc() failures.
	 */
	atomic_set(&cxlrd->region_id, -1);
	rc = memregion_alloc(GFP_KERNEL);
	if (rc < 0) {
		put_device(&cxld->dev);
		return ERR_PTR(rc);
	}

	atomic_set(&cxlrd->region_id, rc);
	return cxlrd;
}
EXPORT_SYMBOL_NS_GPL(cxl_root_decoder_alloc, CXL);

/**
 * cxl_switch_decoder_alloc - Allocate a switch level decoder
 * @port: owning CXL switch port of this decoder
 * @nr_targets: max number of dynamically addressable downstream targets
 *
 * Return: A new cxl decoder to be registered by cxl_decoder_add(). A
 * 'switch' decoder is any decoder that can be enumerated by PCIe
 * topology and the HDM Decoder Capability. This includes the decoders
 * that sit between Switch Upstream Ports / Switch Downstream Ports and
 * Host Bridges / Root Ports.
 */
struct cxl_switch_decoder *cxl_switch_decoder_alloc(struct cxl_port *port,
						    unsigned int nr_targets)
{
	struct cxl_switch_decoder *cxlsd;
	struct cxl_decoder *cxld;
	int rc;

	if (is_cxl_root(port) || is_cxl_endpoint(port))
		return ERR_PTR(-EINVAL);

	cxlsd = kzalloc(struct_size(cxlsd, target, nr_targets), GFP_KERNEL);
	if (!cxlsd)
		return ERR_PTR(-ENOMEM);

	rc = cxl_switch_decoder_init(port, cxlsd, nr_targets);
	if (rc) {
		kfree(cxlsd);
		return ERR_PTR(rc);
	}

	cxld = &cxlsd->cxld;
	cxld->dev.type = &cxl_decoder_switch_type;
	return cxlsd;
}
EXPORT_SYMBOL_NS_GPL(cxl_switch_decoder_alloc, CXL);

/**
 * cxl_endpoint_decoder_alloc - Allocate an endpoint decoder
 * @port: owning port of this decoder
 *
 * Return: A new cxl decoder to be registered by cxl_decoder_add()
 */
struct cxl_endpoint_decoder *cxl_endpoint_decoder_alloc(struct cxl_port *port)
{
	struct cxl_endpoint_decoder *cxled;
	struct cxl_decoder *cxld;
	int rc;

	if (!is_cxl_endpoint(port))
		return ERR_PTR(-EINVAL);

	cxled = kzalloc(sizeof(*cxled), GFP_KERNEL);
	if (!cxled)
		return ERR_PTR(-ENOMEM);

	cxled->pos = -1;
	cxld = &cxled->cxld;
	rc = cxl_decoder_init(port, cxld);
	if (rc)	 {
		kfree(cxled);
		return ERR_PTR(rc);
	}

	cxld->dev.type = &cxl_decoder_endpoint_type;
	return cxled;
}
EXPORT_SYMBOL_NS_GPL(cxl_endpoint_decoder_alloc, CXL);

/**
 * cxl_decoder_add_locked - Add a decoder with targets
 * @cxld: The cxl decoder allocated by cxl_<type>_decoder_alloc()
 * @target_map: A list of downstream ports that this decoder can direct memory
 *              traffic to. These numbers should correspond with the port number
 *              in the PCIe Link Capabilities structure.
 *
 * Certain types of decoders may not have any targets. The main example of this
 * is an endpoint device. A more awkward example is a hostbridge whose root
 * ports get hot added (technically possible, though unlikely).
 *
 * This is the locked variant of cxl_decoder_add().
 *
 * Context: Process context. Expects the device lock of the port that owns the
 *	    @cxld to be held.
 *
 * Return: Negative error code if the decoder wasn't properly configured; else
 *	   returns 0.
 */
int cxl_decoder_add_locked(struct cxl_decoder *cxld, int *target_map)
{
	struct cxl_port *port;
	struct device *dev;
	int rc;

	if (WARN_ON_ONCE(!cxld))
		return -EINVAL;

	if (WARN_ON_ONCE(IS_ERR(cxld)))
		return PTR_ERR(cxld);

	if (cxld->interleave_ways < 1)
		return -EINVAL;

	dev = &cxld->dev;

	port = to_cxl_port(cxld->dev.parent);
	if (!is_endpoint_decoder(dev)) {
		struct cxl_switch_decoder *cxlsd = to_cxl_switch_decoder(dev);

		rc = decoder_populate_targets(cxlsd, port, target_map);
		if (rc && (cxld->flags & CXL_DECODER_F_ENABLE)) {
			dev_err(&port->dev,
				"Failed to populate active decoder targets\n");
			return rc;
		}
	}

	rc = dev_set_name(dev, "decoder%d.%d", port->id, cxld->id);
	if (rc)
		return rc;

	return device_add(dev);
}
EXPORT_SYMBOL_NS_GPL(cxl_decoder_add_locked, CXL);

/**
 * cxl_decoder_add - Add a decoder with targets
 * @cxld: The cxl decoder allocated by cxl_<type>_decoder_alloc()
 * @target_map: A list of downstream ports that this decoder can direct memory
 *              traffic to. These numbers should correspond with the port number
 *              in the PCIe Link Capabilities structure.
 *
 * This is the unlocked variant of cxl_decoder_add_locked().
 * See cxl_decoder_add_locked().
 *
 * Context: Process context. Takes and releases the device lock of the port that
 *	    owns the @cxld.
 */
int cxl_decoder_add(struct cxl_decoder *cxld, int *target_map)
{
	struct cxl_port *port;
	int rc;

	if (WARN_ON_ONCE(!cxld))
		return -EINVAL;

	if (WARN_ON_ONCE(IS_ERR(cxld)))
		return PTR_ERR(cxld);

	port = to_cxl_port(cxld->dev.parent);

	device_lock(&port->dev);
	rc = cxl_decoder_add_locked(cxld, target_map);
	device_unlock(&port->dev);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_decoder_add, CXL);

static void cxld_unregister(void *dev)
{
	struct cxl_endpoint_decoder *cxled;

	if (is_endpoint_decoder(dev)) {
		cxled = to_cxl_endpoint_decoder(dev);
		cxl_decoder_kill_region(cxled);
	}

	device_unregister(dev);
}

int cxl_decoder_autoremove(struct device *host, struct cxl_decoder *cxld)
{
	return devm_add_action_or_reset(host, cxld_unregister, &cxld->dev);
}
EXPORT_SYMBOL_NS_GPL(cxl_decoder_autoremove, CXL);

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
EXPORT_SYMBOL_NS_GPL(__cxl_driver_register, CXL);

void cxl_driver_unregister(struct cxl_driver *cxl_drv)
{
	driver_unregister(&cxl_drv->drv);
}
EXPORT_SYMBOL_NS_GPL(cxl_driver_unregister, CXL);

static int cxl_bus_uevent(const struct device *dev, struct kobj_uevent_env *env)
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
	int rc;

	rc = to_cxl_drv(dev->driver)->probe(dev);
	dev_dbg(dev, "probe: %d\n", rc);
	return rc;
}

static void cxl_bus_remove(struct device *dev)
{
	struct cxl_driver *cxl_drv = to_cxl_drv(dev->driver);

	if (cxl_drv->remove)
		cxl_drv->remove(dev);
}

static struct workqueue_struct *cxl_bus_wq;

static void cxl_bus_rescan_queue(struct work_struct *w)
{
	int rc = bus_rescan_devices(&cxl_bus_type);

	pr_debug("CXL bus rescan result: %d\n", rc);
}

void cxl_bus_rescan(void)
{
	static DECLARE_WORK(rescan_work, cxl_bus_rescan_queue);

	queue_work(cxl_bus_wq, &rescan_work);
}
EXPORT_SYMBOL_NS_GPL(cxl_bus_rescan, CXL);

void cxl_bus_drain(void)
{
	drain_workqueue(cxl_bus_wq);
}
EXPORT_SYMBOL_NS_GPL(cxl_bus_drain, CXL);

bool schedule_cxl_memdev_detach(struct cxl_memdev *cxlmd)
{
	return queue_work(cxl_bus_wq, &cxlmd->detach_work);
}
EXPORT_SYMBOL_NS_GPL(schedule_cxl_memdev_detach, CXL);

/* for user tooling to ensure port disable work has completed */
static ssize_t flush_store(const struct bus_type *bus, const char *buf, size_t count)
{
	if (sysfs_streq(buf, "1")) {
		flush_workqueue(cxl_bus_wq);
		return count;
	}

	return -EINVAL;
}

static BUS_ATTR_WO(flush);

static struct attribute *cxl_bus_attributes[] = {
	&bus_attr_flush.attr,
	NULL,
};

static struct attribute_group cxl_bus_attribute_group = {
	.attrs = cxl_bus_attributes,
};

static const struct attribute_group *cxl_bus_attribute_groups[] = {
	&cxl_bus_attribute_group,
	NULL,
};

struct bus_type cxl_bus_type = {
	.name = "cxl",
	.uevent = cxl_bus_uevent,
	.match = cxl_bus_match,
	.probe = cxl_bus_probe,
	.remove = cxl_bus_remove,
	.bus_groups = cxl_bus_attribute_groups,
};
EXPORT_SYMBOL_NS_GPL(cxl_bus_type, CXL);

static struct dentry *cxl_debugfs;

struct dentry *cxl_debugfs_create_dir(const char *dir)
{
	return debugfs_create_dir(dir, cxl_debugfs);
}
EXPORT_SYMBOL_NS_GPL(cxl_debugfs_create_dir, CXL);

static __init int cxl_core_init(void)
{
	int rc;

	cxl_debugfs = debugfs_create_dir("cxl", NULL);

	cxl_mbox_init();

	rc = cxl_memdev_init();
	if (rc)
		return rc;

	cxl_bus_wq = alloc_ordered_workqueue("cxl_port", 0);
	if (!cxl_bus_wq) {
		rc = -ENOMEM;
		goto err_wq;
	}

	rc = bus_register(&cxl_bus_type);
	if (rc)
		goto err_bus;

	rc = cxl_region_init();
	if (rc)
		goto err_region;

	return 0;

err_region:
	bus_unregister(&cxl_bus_type);
err_bus:
	destroy_workqueue(cxl_bus_wq);
err_wq:
	cxl_memdev_exit();
	return rc;
}

static void cxl_core_exit(void)
{
	cxl_region_exit();
	bus_unregister(&cxl_bus_type);
	destroy_workqueue(cxl_bus_wq);
	cxl_memdev_exit();
	debugfs_remove_recursive(cxl_debugfs);
}

subsys_initcall(cxl_core_init);
module_exit(cxl_core_exit);
MODULE_LICENSE("GPL v2");
