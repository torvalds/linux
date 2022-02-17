// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/workqueue.h>
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

static int cxl_device_id(struct device *dev)
{
	if (dev->type == &cxl_nvdimm_bridge_type)
		return CXL_DEVICE_NVDIMM_BRIDGE;
	if (dev->type == &cxl_nvdimm_type)
		return CXL_DEVICE_NVDIMM;
	if (is_cxl_port(dev)) {
		if (is_cxl_root(to_cxl_port(dev)))
			return CXL_DEVICE_ROOT;
		return CXL_DEVICE_PORT;
	}
	if (is_cxl_memdev(dev))
		return CXL_DEVICE_MEMORY_EXPANDER;
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
	u64 start;

	if (is_root_decoder(dev))
		start = cxld->platform_res.start;
	else
		start = cxld->decoder_range.start;

	return sysfs_emit(buf, "%#llx\n", start);
}
static DEVICE_ATTR_ADMIN_RO(start);

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);
	u64 size;

	if (is_root_decoder(dev))
		size = resource_size(&cxld->platform_res);
	else
		size = range_len(&cxld->decoder_range);

	return sysfs_emit(buf, "%#llx\n", size);
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

static ssize_t emit_target_list(struct cxl_decoder *cxld, char *buf)
{
	ssize_t offset = 0;
	int i, rc = 0;

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
			return rc;
		offset += rc;
	}

	return offset;
}

static ssize_t target_list_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);
	ssize_t offset;
	unsigned int seq;
	int rc;

	do {
		seq = read_seqbegin(&cxld->target_lock);
		rc = emit_target_list(cxld, buf);
	} while (read_seqretry(&cxld->target_lock, seq));

	if (rc < 0)
		return rc;
	offset = rc;

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
	&dev_attr_target_list.attr,
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

static void cxl_decoder_release(struct device *dev)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);
	struct cxl_port *port = to_cxl_port(dev->parent);

	ida_free(&port->decoder_ida, cxld->id);
	kfree(cxld);
	put_device(&port->dev);
}

static const struct device_type cxl_decoder_endpoint_type = {
	.name = "cxl_decoder_endpoint",
	.release = cxl_decoder_release,
	.groups = cxl_decoder_endpoint_attribute_groups,
};

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

static bool is_endpoint_decoder(struct device *dev)
{
	return dev->type == &cxl_decoder_endpoint_type;
}

bool is_root_decoder(struct device *dev)
{
	return dev->type == &cxl_decoder_root_type;
}
EXPORT_SYMBOL_NS_GPL(is_root_decoder, CXL);

bool is_cxl_decoder(struct device *dev)
{
	return dev->type && dev->type->release == cxl_decoder_release;
}
EXPORT_SYMBOL_NS_GPL(is_cxl_decoder, CXL);

struct cxl_decoder *to_cxl_decoder(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type->release != cxl_decoder_release,
			  "not a cxl_decoder device\n"))
		return NULL;
	return container_of(dev, struct cxl_decoder, dev);
}
EXPORT_SYMBOL_NS_GPL(to_cxl_decoder, CXL);

static void cxl_ep_release(struct cxl_ep *ep)
{
	if (!ep)
		return;
	list_del(&ep->list);
	put_device(ep->ep);
	kfree(ep);
}

static void cxl_port_release(struct device *dev)
{
	struct cxl_port *port = to_cxl_port(dev);
	struct cxl_ep *ep, *_e;

	cxl_device_lock(dev);
	list_for_each_entry_safe(ep, _e, &port->endpoints, list)
		cxl_ep_release(ep);
	cxl_device_unlock(dev);
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

bool is_cxl_port(struct device *dev)
{
	return dev->type == &cxl_port_type;
}
EXPORT_SYMBOL_NS_GPL(is_cxl_port, CXL);

struct cxl_port *to_cxl_port(struct device *dev)
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
	port->uport = NULL;
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
	INIT_LIST_HEAD(&port->endpoints);

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

	if (parent_port)
		port->depth = parent_port->depth + 1;
	dev = &port->dev;
	if (is_cxl_memdev(uport))
		rc = dev_set_name(dev, "endpoint%d", port->id);
	else if (parent_port)
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

/* Find a 2nd level CXL port that has a dport that is an ancestor of @match */
static int match_root_child(struct device *dev, const void *match)
{
	const struct device *iter = NULL;
	struct cxl_dport *dport;
	struct cxl_port *port;

	if (!dev_is_cxl_root_child(dev))
		return 0;

	port = to_cxl_port(dev);
	cxl_device_lock(dev);
	list_for_each_entry(dport, &port->dports, list) {
		iter = match;
		while (iter) {
			if (iter == dport->dport)
				goto out;
			iter = iter->parent;
		}
	}
out:
	cxl_device_unlock(dev);

	return !!iter;
}

struct cxl_port *find_cxl_root(struct device *dev)
{
	struct device *port_dev;
	struct cxl_port *root;

	port_dev = bus_find_device(&cxl_bus_type, NULL, dev, match_root_child);
	if (!port_dev)
		return NULL;

	root = to_cxl_port(port_dev->parent);
	get_device(&root->dev);
	put_device(port_dev);
	return root;
}
EXPORT_SYMBOL_NS_GPL(find_cxl_root, CXL);

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

	device_lock_assert(&port->dev);
	dup = find_dport(port, new->port_id);
	if (dup)
		dev_err(&port->dev,
			"unable to add dport%d-%s non-unique port id (%s)\n",
			new->port_id, dev_name(new->dport),
			dev_name(dup->dport));
	else
		list_add_tail(&new->list, &port->dports);

	return dup ? -EEXIST : 0;
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
		cxl_device_lock(&port->dev);
}

static void cond_cxl_root_unlock(struct cxl_port *port)
{
	if (is_cxl_root(port))
		cxl_device_unlock(&port->dev);
}

static void cxl_dport_remove(void *data)
{
	struct cxl_dport *dport = data;
	struct cxl_port *port = dport->port;

	put_device(dport->dport);
	cond_cxl_root_lock(port);
	list_del(&dport->list);
	cond_cxl_root_unlock(port);
}

static void cxl_dport_unlink(void *data)
{
	struct cxl_dport *dport = data;
	struct cxl_port *port = dport->port;
	char link_name[CXL_TARGET_STRLEN];

	sprintf(link_name, "dport%d", dport->port_id);
	sysfs_remove_link(&port->dev.kobj, link_name);
}

/**
 * devm_cxl_add_dport - append downstream port data to a cxl_port
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

	INIT_LIST_HEAD(&dport->list);
	dport->dport = dport_dev;
	dport->port_id = port_id;
	dport->component_reg_phys = component_reg_phys;
	dport->port = port;

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
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_dport, CXL);

static struct cxl_ep *find_ep(struct cxl_port *port, struct device *ep_dev)
{
	struct cxl_ep *ep;

	device_lock_assert(&port->dev);
	list_for_each_entry(ep, &port->endpoints, list)
		if (ep->ep == ep_dev)
			return ep;
	return NULL;
}

static int add_ep(struct cxl_port *port, struct cxl_ep *new)
{
	struct cxl_ep *dup;

	cxl_device_lock(&port->dev);
	if (port->dead) {
		cxl_device_unlock(&port->dev);
		return -ENXIO;
	}
	dup = find_ep(port, new->ep);
	if (!dup)
		list_add_tail(&new->list, &port->endpoints);
	cxl_device_unlock(&port->dev);

	return dup ? -EEXIST : 0;
}

/**
 * cxl_add_ep - register an endpoint's interest in a port
 * @port: a port in the endpoint's topology ancestry
 * @ep_dev: device representing the endpoint
 *
 * Intermediate CXL ports are scanned based on the arrival of endpoints.
 * When those endpoints depart the port can be destroyed once all
 * endpoints that care about that port have been removed.
 */
static int cxl_add_ep(struct cxl_port *port, struct device *ep_dev)
{
	struct cxl_ep *ep;
	int rc;

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	INIT_LIST_HEAD(&ep->list);
	ep->ep = get_device(ep_dev);

	rc = add_ep(port, ep);
	if (rc)
		cxl_ep_release(ep);
	return rc;
}

struct cxl_find_port_ctx {
	const struct device *dport_dev;
	const struct cxl_port *parent_port;
};

static int match_port_by_dport(struct device *dev, const void *data)
{
	const struct cxl_find_port_ctx *ctx = data;
	struct cxl_port *port;

	if (!is_cxl_port(dev))
		return 0;
	if (ctx->parent_port && dev->parent != &ctx->parent_port->dev)
		return 0;

	port = to_cxl_port(dev);
	return cxl_find_dport_by_dev(port, ctx->dport_dev) != NULL;
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

static struct cxl_port *find_cxl_port(struct device *dport_dev)
{
	struct cxl_find_port_ctx ctx = {
		.dport_dev = dport_dev,
	};

	return __find_cxl_port(&ctx);
}

static struct cxl_port *find_cxl_port_at(struct cxl_port *parent_port,
					 struct device *dport_dev)
{
	struct cxl_find_port_ctx ctx = {
		.dport_dev = dport_dev,
		.parent_port = parent_port,
	};

	return __find_cxl_port(&ctx);
}

/*
 * All users of grandparent() are using it to walk PCIe-like swich port
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

	parent_port = cxl_mem_find_port(cxlmd);
	if (!parent_port)
		goto out;
	parent = &parent_port->dev;

	cxl_device_lock(parent);
	if (parent->driver && endpoint->uport) {
		devm_release_action(parent, cxl_unlink_uport, endpoint);
		devm_release_action(parent, unregister_port, endpoint);
	}
	cxl_device_unlock(parent);
	put_device(parent);
out:
	put_device(&endpoint->dev);
}

int cxl_endpoint_autoremove(struct cxl_memdev *cxlmd, struct cxl_port *endpoint)
{
	struct device *dev = &cxlmd->dev;

	get_device(&endpoint->dev);
	dev_set_drvdata(dev, endpoint);
	return devm_add_action_or_reset(dev, delete_endpoint, cxlmd);
}
EXPORT_SYMBOL_NS_GPL(cxl_endpoint_autoremove, CXL);

/*
 * The natural end of life of a non-root 'cxl_port' is when its parent port goes
 * through a ->remove() event ("top-down" unregistration). The unnatural trigger
 * for a port to be unregistered is when all memdevs beneath that port have gone
 * through ->remove(). This "bottom-up" removal selectively removes individual
 * child ports manually. This depends on devm_cxl_add_port() to not change is
 * devm action registration order.
 */
static void delete_switch_port(struct cxl_port *port, struct list_head *dports)
{
	struct cxl_dport *dport, *_d;

	list_for_each_entry_safe(dport, _d, dports, list) {
		devm_release_action(&port->dev, cxl_dport_unlink, dport);
		devm_release_action(&port->dev, cxl_dport_remove, dport);
		devm_kfree(&port->dev, dport);
	}
	devm_release_action(port->dev.parent, cxl_unlink_uport, port);
	devm_release_action(port->dev.parent, unregister_port, port);
}

static void cxl_detach_ep(void *data)
{
	struct cxl_memdev *cxlmd = data;
	struct device *iter;

	for (iter = &cxlmd->dev; iter; iter = grandparent(iter)) {
		struct device *dport_dev = grandparent(iter);
		struct cxl_port *port, *parent_port;
		LIST_HEAD(reap_dports);
		struct cxl_ep *ep;

		if (!dport_dev)
			break;

		port = find_cxl_port(dport_dev);
		if (!port || is_cxl_root(port)) {
			put_device(&port->dev);
			continue;
		}

		parent_port = to_cxl_port(port->dev.parent);
		cxl_device_lock(&parent_port->dev);
		if (!parent_port->dev.driver) {
			/*
			 * The bottom-up race to delete the port lost to a
			 * top-down port disable, give up here, because the
			 * parent_port ->remove() will have cleaned up all
			 * descendants.
			 */
			cxl_device_unlock(&parent_port->dev);
			put_device(&port->dev);
			continue;
		}

		cxl_device_lock(&port->dev);
		ep = find_ep(port, &cxlmd->dev);
		dev_dbg(&cxlmd->dev, "disconnect %s from %s\n",
			ep ? dev_name(ep->ep) : "", dev_name(&port->dev));
		cxl_ep_release(ep);
		if (ep && !port->dead && list_empty(&port->endpoints) &&
		    !is_cxl_root(parent_port)) {
			/*
			 * This was the last ep attached to a dynamically
			 * enumerated port. Block new cxl_add_ep() and garbage
			 * collect the port.
			 */
			port->dead = true;
			list_splice_init(&port->dports, &reap_dports);
		}
		cxl_device_unlock(&port->dev);

		if (!list_empty(&reap_dports)) {
			dev_dbg(&cxlmd->dev, "delete %s\n",
				dev_name(&port->dev));
			delete_switch_port(port, &reap_dports);
		}
		put_device(&port->dev);
		cxl_device_unlock(&parent_port->dev);
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
	return cxl_regmap_to_base(pdev, &map);
}

static int add_port_attach_ep(struct cxl_memdev *cxlmd,
			      struct device *uport_dev,
			      struct device *dport_dev)
{
	struct device *dparent = grandparent(dport_dev);
	struct cxl_port *port, *parent_port = NULL;
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

	parent_port = find_cxl_port(dparent);
	if (!parent_port) {
		/* iterate to create this parent_port */
		return -EAGAIN;
	}

	cxl_device_lock(&parent_port->dev);
	if (!parent_port->dev.driver) {
		dev_warn(&cxlmd->dev,
			 "port %s:%s disabled, failed to enumerate CXL.mem\n",
			 dev_name(&parent_port->dev), dev_name(uport_dev));
		port = ERR_PTR(-ENXIO);
		goto out;
	}

	port = find_cxl_port_at(parent_port, dport_dev);
	if (!port) {
		component_reg_phys = find_component_registers(uport_dev);
		port = devm_cxl_add_port(&parent_port->dev, uport_dev,
					 component_reg_phys, parent_port);
		if (!IS_ERR(port))
			get_device(&port->dev);
	}
out:
	cxl_device_unlock(&parent_port->dev);

	if (IS_ERR(port))
		rc = PTR_ERR(port);
	else {
		dev_dbg(&cxlmd->dev, "add to new port %s:%s\n",
			dev_name(&port->dev), dev_name(port->uport));
		rc = cxl_add_ep(port, &cxlmd->dev);
		if (rc == -EEXIST) {
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
		port = find_cxl_port(dport_dev);
		if (port) {
			dev_dbg(&cxlmd->dev,
				"found already registered port %s:%s\n",
				dev_name(&port->dev), dev_name(port->uport));
			rc = cxl_add_ep(port, &cxlmd->dev);

			/*
			 * If the endpoint already exists in the port's list,
			 * that's ok, it was added on a previous pass.
			 * Otherwise, retry in add_port_attach_ep() after taking
			 * the parent_port lock as the current port may be being
			 * reaped.
			 */
			if (rc && rc != -EEXIST) {
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

struct cxl_port *cxl_mem_find_port(struct cxl_memdev *cxlmd)
{
	return find_cxl_port(grandparent(&cxlmd->dev));
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_find_port, CXL);

struct cxl_dport *cxl_find_dport_by_dev(struct cxl_port *port,
					const struct device *dev)
{
	struct cxl_dport *dport;

	cxl_device_lock(&port->dev);
	list_for_each_entry(dport, &port->dports, list)
		if (dport->dport == dev) {
			cxl_device_unlock(&port->dev);
			return dport;
		}

	cxl_device_unlock(&port->dev);
	return NULL;
}
EXPORT_SYMBOL_NS_GPL(cxl_find_dport_by_dev, CXL);

static int decoder_populate_targets(struct cxl_decoder *cxld,
				    struct cxl_port *port, int *target_map)
{
	int i, rc = 0;

	if (!target_map)
		return 0;

	device_lock_assert(&port->dev);

	if (list_empty(&port->dports))
		return -EINVAL;

	write_seqlock(&cxld->target_lock);
	for (i = 0; i < cxld->nr_targets; i++) {
		struct cxl_dport *dport = find_dport(port, target_map[i]);

		if (!dport) {
			rc = -ENXIO;
			break;
		}
		cxld->target[i] = dport;
	}
	write_sequnlock(&cxld->target_lock);

	return rc;
}

/**
 * cxl_decoder_alloc - Allocate a new CXL decoder
 * @port: owning port of this decoder
 * @nr_targets: downstream targets accessible by this decoder. All upstream
 *		ports and root ports must have at least 1 target. Endpoint
 *		devices will have 0 targets. Callers wishing to register an
 *		endpoint device should specify 0.
 *
 * A port should contain one or more decoders. Each of those decoders enable
 * some address space for CXL.mem utilization. A decoder is expected to be
 * configured by the caller before registering.
 *
 * Return: A new cxl decoder to be registered by cxl_decoder_add(). The decoder
 *	   is initialized to be a "passthrough" decoder.
 */
static struct cxl_decoder *cxl_decoder_alloc(struct cxl_port *port,
					     unsigned int nr_targets)
{
	struct cxl_decoder *cxld;
	struct device *dev;
	int rc = 0;

	if (nr_targets > CXL_DECODER_MAX_INTERLEAVE)
		return ERR_PTR(-EINVAL);

	cxld = kzalloc(struct_size(cxld, target, nr_targets), GFP_KERNEL);
	if (!cxld)
		return ERR_PTR(-ENOMEM);

	rc = ida_alloc(&port->decoder_ida, GFP_KERNEL);
	if (rc < 0)
		goto err;

	/* need parent to stick around to release the id */
	get_device(&port->dev);
	cxld->id = rc;

	cxld->nr_targets = nr_targets;
	seqlock_init(&cxld->target_lock);
	dev = &cxld->dev;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = &port->dev;
	dev->bus = &cxl_bus_type;
	if (is_cxl_root(port))
		cxld->dev.type = &cxl_decoder_root_type;
	else if (is_cxl_endpoint(port))
		cxld->dev.type = &cxl_decoder_endpoint_type;
	else
		cxld->dev.type = &cxl_decoder_switch_type;

	/* Pre initialize an "empty" decoder */
	cxld->interleave_ways = 1;
	cxld->interleave_granularity = PAGE_SIZE;
	cxld->target_type = CXL_DECODER_EXPANDER;
	cxld->platform_res = (struct resource)DEFINE_RES_MEM(0, 0);

	return cxld;
err:
	kfree(cxld);
	return ERR_PTR(rc);
}

/**
 * cxl_root_decoder_alloc - Allocate a root level decoder
 * @port: owning CXL root of this decoder
 * @nr_targets: static number of downstream targets
 *
 * Return: A new cxl decoder to be registered by cxl_decoder_add(). A
 * 'CXL root' decoder is one that decodes from a top-level / static platform
 * firmware description of CXL resources into a CXL standard decode
 * topology.
 */
struct cxl_decoder *cxl_root_decoder_alloc(struct cxl_port *port,
					   unsigned int nr_targets)
{
	if (!is_cxl_root(port))
		return ERR_PTR(-EINVAL);

	return cxl_decoder_alloc(port, nr_targets);
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
struct cxl_decoder *cxl_switch_decoder_alloc(struct cxl_port *port,
					     unsigned int nr_targets)
{
	if (is_cxl_root(port) || is_cxl_endpoint(port))
		return ERR_PTR(-EINVAL);

	return cxl_decoder_alloc(port, nr_targets);
}
EXPORT_SYMBOL_NS_GPL(cxl_switch_decoder_alloc, CXL);

/**
 * cxl_endpoint_decoder_alloc - Allocate an endpoint decoder
 * @port: owning port of this decoder
 *
 * Return: A new cxl decoder to be registered by cxl_decoder_add()
 */
struct cxl_decoder *cxl_endpoint_decoder_alloc(struct cxl_port *port)
{
	if (!is_cxl_endpoint(port))
		return ERR_PTR(-EINVAL);

	return cxl_decoder_alloc(port, 0);
}
EXPORT_SYMBOL_NS_GPL(cxl_endpoint_decoder_alloc, CXL);

/**
 * cxl_decoder_add_locked - Add a decoder with targets
 * @cxld: The cxl decoder allocated by cxl_decoder_alloc()
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
		rc = decoder_populate_targets(cxld, port, target_map);
		if (rc && (cxld->flags & CXL_DECODER_F_ENABLE)) {
			dev_err(&port->dev,
				"Failed to populate active decoder targets\n");
			return rc;
		}
	}

	rc = dev_set_name(dev, "decoder%d.%d", port->id, cxld->id);
	if (rc)
		return rc;

	/*
	 * Platform decoder resources should show up with a reasonable name. All
	 * other resources are just sub ranges within the main decoder resource.
	 */
	if (is_root_decoder(dev))
		cxld->platform_res.name = dev_name(dev);

	return device_add(dev);
}
EXPORT_SYMBOL_NS_GPL(cxl_decoder_add_locked, CXL);

/**
 * cxl_decoder_add - Add a decoder with targets
 * @cxld: The cxl decoder allocated by cxl_decoder_alloc()
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

	cxl_device_lock(&port->dev);
	rc = cxl_decoder_add_locked(cxld, target_map);
	cxl_device_unlock(&port->dev);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_decoder_add, CXL);

static void cxld_unregister(void *dev)
{
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
	int rc;

	/*
	 * Take the CXL nested lock since the driver core only holds
	 * @dev->mutex and not @dev->lockdep_mutex.
	 */
	cxl_nested_lock(dev);
	rc = to_cxl_drv(dev->driver)->probe(dev);
	cxl_nested_unlock(dev);

	dev_dbg(dev, "probe: %d\n", rc);
	return rc;
}

static void cxl_bus_remove(struct device *dev)
{
	struct cxl_driver *cxl_drv = to_cxl_drv(dev->driver);

	cxl_nested_lock(dev);
	if (cxl_drv->remove)
		cxl_drv->remove(dev);
	cxl_nested_unlock(dev);
}

static struct workqueue_struct *cxl_bus_wq;

int cxl_bus_rescan(void)
{
	return bus_rescan_devices(&cxl_bus_type);
}
EXPORT_SYMBOL_NS_GPL(cxl_bus_rescan, CXL);

bool schedule_cxl_memdev_detach(struct cxl_memdev *cxlmd)
{
	return queue_work(cxl_bus_wq, &cxlmd->detach_work);
}
EXPORT_SYMBOL_NS_GPL(schedule_cxl_memdev_detach, CXL);

/* for user tooling to ensure port disable work has completed */
static ssize_t flush_store(struct bus_type *bus, const char *buf, size_t count)
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

static __init int cxl_core_init(void)
{
	int rc;

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

	return 0;

err_bus:
	destroy_workqueue(cxl_bus_wq);
err_wq:
	cxl_memdev_exit();
	cxl_mbox_exit();
	return rc;
}

static void cxl_core_exit(void)
{
	bus_unregister(&cxl_bus_type);
	destroy_workqueue(cxl_bus_wq);
	cxl_memdev_exit();
	cxl_mbox_exit();
}

module_init(cxl_core_init);
module_exit(cxl_core_exit);
MODULE_LICENSE("GPL v2");
