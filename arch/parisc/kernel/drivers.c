// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers.c
 *
 * Copyright (c) 1999 The Puffin Group
 * Copyright (c) 2001 Matthew Wilcox for Hewlett Packard
 * Copyright (c) 2001-2023 Helge Deller <deller@gmx.de>
 * Copyright (c) 2001,2002 Ryan Bradetich 
 * Copyright (c) 2004-2005 Thibaut VARENE <varenet@parisc-linux.org>
 * 
 * The file handles registering devices and drivers, then matching them.
 * It's the closest we get to a dating agency.
 *
 * If you're thinking about modifying this file, here are some gotchas to
 * bear in mind:
 *  - 715/Mirage device paths have a dummy device between Lasi and its children
 *  - The EISA adapter may show up as a sibling or child of Wax
 *  - Dino has an optionally functional serial port.  If firmware enables it,
 *    it shows up as a child of Dino.  If firmware disables it, the buswalk
 *    finds it and it shows up as a child of Cujo
 *  - Dino has both parisc and pci devices as children
 *  - parisc devices are discovered in a random order, including children
 *    before parents in some cases.
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/dma-map-ops.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/pdc.h>
#include <asm/parisc-device.h>
#include <asm/ropes.h>

/* See comments in include/asm-parisc/pci.h */
const struct dma_map_ops *hppa_dma_ops __ro_after_init;
EXPORT_SYMBOL(hppa_dma_ops);

static struct device root = {
	.init_name = "parisc",
};

static inline int check_dev(struct device *dev)
{
	if (dev->bus == &parisc_bus_type) {
		struct parisc_device *pdev;
		pdev = to_parisc_device(dev);
		return pdev->id.hw_type != HPHW_FAULTY;
	}
	return 1;
}

static struct device *
parse_tree_node(struct device *parent, int index, struct hardware_path *modpath);

struct recurse_struct {
	void * obj;
	int (*fn)(struct device *, void *);
};

static int descend_children(struct device * dev, void * data)
{
	struct recurse_struct * recurse_data = (struct recurse_struct *)data;

	if (recurse_data->fn(dev, recurse_data->obj))
		return 1;
	else
		return device_for_each_child(dev, recurse_data, descend_children);
}

/**
 * for_each_padev - Iterate over all devices in the tree
 * @fn: Function to call for each device.
 * @data: Data to pass to the called function.
 *
 * This performs a depth-first traversal of the tree, calling the
 * function passed for each node.  It calls the function for parents
 * before children.
 */

static int for_each_padev(int (*fn)(struct device *, void *), void * data)
{
	struct recurse_struct recurse_data = {
		.obj	= data,
		.fn	= fn,
	};
	return device_for_each_child(&root, &recurse_data, descend_children);
}

/**
 * match_device - Report whether this driver can handle this device
 * @driver: the PA-RISC driver to try
 * @dev: the PA-RISC device to try
 */
static int match_device(const struct parisc_driver *driver, struct parisc_device *dev)
{
	const struct parisc_device_id *ids;

	for (ids = driver->id_table; ids->sversion; ids++) {
		if ((ids->sversion != SVERSION_ANY_ID) &&
		    (ids->sversion != dev->id.sversion))
			continue;

		if ((ids->hw_type != HWTYPE_ANY_ID) &&
		    (ids->hw_type != dev->id.hw_type))
			continue;

		if ((ids->hversion != HVERSION_ANY_ID) &&
		    (ids->hversion != dev->id.hversion))
			continue;

		return 1;
	}
	return 0;
}

static int parisc_driver_probe(struct device *dev)
{
	int rc;
	struct parisc_device *pa_dev = to_parisc_device(dev);
	struct parisc_driver *pa_drv = to_parisc_driver(dev->driver);

	rc = pa_drv->probe(pa_dev);

	if (!rc)
		pa_dev->driver = pa_drv;

	return rc;
}

static void __exit parisc_driver_remove(struct device *dev)
{
	struct parisc_device *pa_dev = to_parisc_device(dev);
	struct parisc_driver *pa_drv = to_parisc_driver(dev->driver);

	if (pa_drv->remove)
		pa_drv->remove(pa_dev);
}
	

/**
 * register_parisc_driver - Register this driver if it can handle a device
 * @driver: the PA-RISC driver to try
 */
int register_parisc_driver(struct parisc_driver *driver)
{
	/* FIXME: we need this because apparently the sti
	 * driver can be registered twice */
	if (driver->drv.name) {
		pr_warn("BUG: skipping previously registered driver %s\n",
			driver->name);
		return 1;
	}

	if (!driver->probe) {
		pr_warn("BUG: driver %s has no probe routine\n", driver->name);
		return 1;
	}

	driver->drv.bus = &parisc_bus_type;

	/* We install our own probe and remove routines */
	WARN_ON(driver->drv.probe != NULL);
	WARN_ON(driver->drv.remove != NULL);

	driver->drv.name = driver->name;

	return driver_register(&driver->drv);
}
EXPORT_SYMBOL(register_parisc_driver);


struct match_count {
	struct parisc_driver * driver;
	int count;
};

static int match_and_count(struct device * dev, void * data)
{
	struct match_count * m = data;
	struct parisc_device * pdev = to_parisc_device(dev);

	if (check_dev(dev)) {
		if (match_device(m->driver, pdev))
			m->count++;
	}
	return 0;
}

/**
 * count_parisc_driver - count # of devices this driver would match
 * @driver: the PA-RISC driver to try
 *
 * Use by IOMMU support to "guess" the right size IOPdir.
 * Formula is something like memsize/(num_iommu * entry_size).
 */
int __init count_parisc_driver(struct parisc_driver *driver)
{
	struct match_count m = {
		.driver	= driver,
		.count	= 0,
	};

	for_each_padev(match_and_count, &m);

	return m.count;
}



/**
 * unregister_parisc_driver - Unregister this driver from the list of drivers
 * @driver: the PA-RISC driver to unregister
 */
int unregister_parisc_driver(struct parisc_driver *driver)
{
	driver_unregister(&driver->drv);
	return 0;
}
EXPORT_SYMBOL(unregister_parisc_driver);

struct find_data {
	unsigned long hpa;
	struct parisc_device * dev;
};

static int find_device(struct device * dev, void * data)
{
	struct parisc_device * pdev = to_parisc_device(dev);
	struct find_data * d = (struct find_data*)data;

	if (check_dev(dev)) {
		if (pdev->hpa.start == d->hpa) {
			d->dev = pdev;
			return 1;
		}
	}
	return 0;
}

static struct parisc_device *find_device_by_addr(unsigned long hpa)
{
	struct find_data d = {
		.hpa	= hpa,
	};
	int ret;

	ret = for_each_padev(find_device, &d);
	return ret ? d.dev : NULL;
}

static int __init is_IKE_device(struct device *dev, void *data)
{
	struct parisc_device *pdev = to_parisc_device(dev);

	if (!check_dev(dev))
		return 0;
	if (pdev->id.hw_type != HPHW_BCPORT)
		return 0;
	if (IS_IKE(pdev) ||
		(pdev->id.hversion == REO_MERCED_PORT) ||
		(pdev->id.hversion == REOG_MERCED_PORT)) {
			return 1;
	}
	return 0;
}

int __init machine_has_merced_bus(void)
{
	int ret;

	ret = for_each_padev(is_IKE_device, NULL);
	return ret ? 1 : 0;
}

/**
 * find_pa_parent_type - Find a parent of a specific type
 * @padev: The device to start searching from
 * @type: The device type to search for.
 *
 * Walks up the device tree looking for a device of the specified type.
 * If it finds it, it returns it.  If not, it returns NULL.
 */
const struct parisc_device *
find_pa_parent_type(const struct parisc_device *padev, int type)
{
	const struct device *dev = &padev->dev;
	while (dev != &root) {
		struct parisc_device *candidate = to_parisc_device(dev);
		if (candidate->id.hw_type == type)
			return candidate;
		dev = dev->parent;
	}

	return NULL;
}

/*
 * get_node_path fills in @path with the firmware path to the device.
 * Note that if @node is a parisc device, we don't fill in the 'mod' field.
 * This is because both callers pass the parent and fill in the mod
 * themselves.  If @node is a PCI device, we do fill it in, even though this
 * is inconsistent.
 */
static void get_node_path(struct device *dev, struct hardware_path *path)
{
	int i = 5;
	memset(&path->bc, -1, 6);

	if (dev_is_pci(dev)) {
		unsigned int devfn = to_pci_dev(dev)->devfn;
		path->mod = PCI_FUNC(devfn);
		path->bc[i--] = PCI_SLOT(devfn);
		dev = dev->parent;
	}

	while (dev != &root) {
		if (dev_is_pci(dev)) {
			unsigned int devfn = to_pci_dev(dev)->devfn;
			path->bc[i--] = PCI_SLOT(devfn) | (PCI_FUNC(devfn)<< 5);
		} else if (dev->bus == &parisc_bus_type) {
			path->bc[i--] = to_parisc_device(dev)->hw_path;
		}
		dev = dev->parent;
	}
}

static char *print_hwpath(struct hardware_path *path, char *output)
{
	int i;
	for (i = 0; i < 6; i++) {
		if (path->bc[i] == -1)
			continue;
		output += sprintf(output, "%u/", (unsigned char) path->bc[i]);
	}
	output += sprintf(output, "%u", (unsigned char) path->mod);
	return output;
}

/**
 * print_pa_hwpath - Returns hardware path for PA devices
 * @dev: The device to return the path for
 * @output: Pointer to a previously-allocated array to place the path in.
 *
 * This function fills in the output array with a human-readable path
 * to a PA device.  This string is compatible with that used by PDC, and
 * may be printed on the outside of the box.
 */
char *print_pa_hwpath(struct parisc_device *dev, char *output)
{
	struct hardware_path path;

	get_node_path(dev->dev.parent, &path);
	path.mod = dev->hw_path;
	return print_hwpath(&path, output);
}
EXPORT_SYMBOL(print_pa_hwpath);

#if defined(CONFIG_PCI) || defined(CONFIG_ISA)
/**
 * get_pci_node_path - Determines the hardware path for a PCI device
 * @pdev: The device to return the path for
 * @path: Pointer to a previously-allocated array to place the path in.
 *
 * This function fills in the hardware_path structure with the route to
 * the specified PCI device.  This structure is suitable for passing to
 * PDC calls.
 */
void get_pci_node_path(struct pci_dev *pdev, struct hardware_path *path)
{
	get_node_path(&pdev->dev, path);
}
EXPORT_SYMBOL(get_pci_node_path);

/**
 * print_pci_hwpath - Returns hardware path for PCI devices
 * @dev: The device to return the path for
 * @output: Pointer to a previously-allocated array to place the path in.
 *
 * This function fills in the output array with a human-readable path
 * to a PCI device.  This string is compatible with that used by PDC, and
 * may be printed on the outside of the box.
 */
char *print_pci_hwpath(struct pci_dev *dev, char *output)
{
	struct hardware_path path;

	get_pci_node_path(dev, &path);
	return print_hwpath(&path, output);
}
EXPORT_SYMBOL(print_pci_hwpath);

#endif /* defined(CONFIG_PCI) || defined(CONFIG_ISA) */

static void setup_bus_id(struct parisc_device *padev)
{
	struct hardware_path path;
	char name[28];
	char *output = name;
	int i;

	get_node_path(padev->dev.parent, &path);

	for (i = 0; i < 6; i++) {
		if (path.bc[i] == -1)
			continue;
		output += sprintf(output, "%u:", (unsigned char) path.bc[i]);
	}
	sprintf(output, "%u", (unsigned char) padev->hw_path);
	dev_set_name(&padev->dev, name);
}

static struct parisc_device * __init create_tree_node(char id,
						      struct device *parent)
{
	struct parisc_device *dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->hw_path = id;
	dev->id.hw_type = HPHW_FAULTY;

	dev->dev.parent = parent;
	setup_bus_id(dev);

	dev->dev.bus = &parisc_bus_type;
	dev->dma_mask = 0xffffffffUL;	/* PARISC devices are 32-bit */

	/* make the generic dma mask a pointer to the parisc one */
	dev->dev.dma_mask = &dev->dma_mask;
	dev->dev.coherent_dma_mask = dev->dma_mask;
	if (device_register(&dev->dev)) {
		kfree(dev);
		return NULL;
	}

	return dev;
}

struct match_id_data {
	char id;
	struct parisc_device * dev;
};

static int match_by_id(struct device * dev, void * data)
{
	struct parisc_device * pdev = to_parisc_device(dev);
	struct match_id_data * d = data;

	if (pdev->hw_path == d->id) {
		d->dev = pdev;
		return 1;
	}
	return 0;
}

/**
 * alloc_tree_node - returns a device entry in the iotree
 * @parent: the parent node in the tree
 * @id: the element of the module path for this entry
 *
 * Checks all the children of @parent for a matching @id.  If none
 * found, it allocates a new device and returns it.
 */
static struct parisc_device * __init alloc_tree_node(
			struct device *parent, char id)
{
	struct match_id_data d = {
		.id = id,
	};
	if (device_for_each_child(parent, &d, match_by_id))
		return d.dev;
	else
		return create_tree_node(id, parent);
}

static struct parisc_device *create_parisc_device(struct hardware_path *modpath)
{
	int i;
	struct device *parent = &root;
	for (i = 0; i < 6; i++) {
		if (modpath->bc[i] == -1)
			continue;
		parent = &alloc_tree_node(parent, modpath->bc[i])->dev;
	}
	return alloc_tree_node(parent, modpath->mod);
}

struct parisc_device * __init
alloc_pa_dev(unsigned long hpa, struct hardware_path *mod_path)
{
	int status;
	unsigned long bytecnt;
	u8 iodc_data[32];
	struct parisc_device *dev;
	const char *name;

	/* Check to make sure this device has not already been added - Ryan */
	if (find_device_by_addr(hpa) != NULL)
		return NULL;

	status = pdc_iodc_read(&bytecnt, hpa, 0, &iodc_data, 32);
	if (status != PDC_OK)
		return NULL;

	dev = create_parisc_device(mod_path);
	if (dev->id.hw_type != HPHW_FAULTY) {
		pr_err("Two devices have hardware path [%s].  IODC data for second device: %7phN\n"
		       "Rearranging GSC cards sometimes helps\n",
			parisc_pathname(dev), iodc_data);
		return NULL;
	}

	dev->id.hw_type = iodc_data[3] & 0x1f;
	dev->id.hversion = (iodc_data[0] << 4) | ((iodc_data[1] & 0xf0) >> 4);
	dev->id.hversion_rev = iodc_data[1] & 0x0f;
	dev->id.sversion = ((iodc_data[4] & 0x0f) << 16) |
			(iodc_data[5] << 8) | iodc_data[6];
	dev->hpa.start = hpa;
	/* This is awkward.  The STI spec says that gfx devices may occupy
	 * 32MB or 64MB.  Unfortunately, we don't know how to tell whether
	 * it's the former or the latter.  Assumptions either way can hurt us.
	 */
	if (hpa == 0xf4000000 || hpa == 0xf8000000) {
		dev->hpa.end = hpa + 0x03ffffff;
	} else if (hpa == 0xf6000000 || hpa == 0xfa000000) {
		dev->hpa.end = hpa + 0x01ffffff;
	} else {
		dev->hpa.end = hpa + 0xfff;
	}
	dev->hpa.flags = IORESOURCE_MEM;
	dev->hpa.name = dev->name;
	name = parisc_hardware_description(&dev->id) ? : "unknown";
	snprintf(dev->name, sizeof(dev->name), "%s [%s]",
		name, parisc_pathname(dev));

	/* Silently fail things like mouse ports which are subsumed within
	 * the keyboard controller
	 */
	if ((hpa & 0xfff) == 0 && insert_resource(&iomem_resource, &dev->hpa))
		pr_warn("Unable to claim HPA %lx for device %s\n", hpa, name);

	return dev;
}

static int parisc_generic_match(struct device *dev, const struct device_driver *drv)
{
	return match_device(to_parisc_driver(drv), to_parisc_device(dev));
}

static ssize_t make_modalias(const struct device *dev, char *buf)
{
	const struct parisc_device *padev = to_parisc_device(dev);
	const struct parisc_device_id *id = &padev->id;

	return sprintf(buf, "parisc:t%02Xhv%04Xrev%02Xsv%08X\n",
		(u8)id->hw_type, (u16)id->hversion, (u8)id->hversion_rev,
		(u32)id->sversion);
}

static int parisc_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct parisc_device *padev;
	char modalias[40];

	if (!dev)
		return -ENODEV;

	padev = to_parisc_device(dev);
	if (!padev)
		return -ENODEV;

	if (add_uevent_var(env, "PARISC_NAME=%s", padev->name))
		return -ENOMEM;

	make_modalias(dev, modalias);
	if (add_uevent_var(env, "MODALIAS=%s", modalias))
		return -ENOMEM;

	return 0;
}

#define pa_dev_attr(name, field, format_string)				\
static ssize_t name##_show(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct parisc_device *padev = to_parisc_device(dev);		\
	return sprintf(buf, format_string, padev->field);		\
}									\
static DEVICE_ATTR_RO(name);

#define pa_dev_attr_id(field, format) pa_dev_attr(field, id.field, format)

pa_dev_attr(irq, irq, "%u\n");
pa_dev_attr_id(hw_type, "0x%02x\n");
pa_dev_attr(rev, id.hversion_rev, "0x%x\n");
pa_dev_attr_id(hversion, "0x%03x\n");
pa_dev_attr_id(sversion, "0x%05x\n");

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return make_modalias(dev, buf);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *parisc_device_attrs[] = {
	&dev_attr_irq.attr,
	&dev_attr_hw_type.attr,
	&dev_attr_rev.attr,
	&dev_attr_hversion.attr,
	&dev_attr_sversion.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(parisc_device);

const struct bus_type parisc_bus_type = {
	.name = "parisc",
	.match = parisc_generic_match,
	.uevent = parisc_uevent,
	.dev_groups = parisc_device_groups,
	.probe = parisc_driver_probe,
	.remove = __exit_p(parisc_driver_remove),
};

/**
 * register_parisc_device - Locate a driver to manage this device.
 * @dev: The parisc device.
 *
 * Search the driver list for a driver that is willing to manage
 * this device.
 */
int __init register_parisc_device(struct parisc_device *dev)
{
	if (!dev)
		return 0;

	if (dev->driver)
		return 1;

	return 0;
}

/**
 * match_pci_device - Matches a pci device against a given hardware path
 * entry.
 * @dev: the generic device (known to be contained by a pci_dev).
 * @index: the current BC index
 * @modpath: the hardware path.
 * @return: true if the device matches the hardware path.
 */
static int match_pci_device(struct device *dev, int index,
		struct hardware_path *modpath)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int id;

	if (index == 5) {
		/* we are at the end of the path, and on the actual device */
		unsigned int devfn = pdev->devfn;
		return ((modpath->bc[5] == PCI_SLOT(devfn)) &&
					(modpath->mod == PCI_FUNC(devfn)));
	}

	/* index might be out of bounds for bc[] */
	if (index >= 6)
		return 0;

	id = PCI_SLOT(pdev->devfn) | (PCI_FUNC(pdev->devfn) << 5);
	return (modpath->bc[index] == id);
}

/**
 * match_parisc_device - Matches a parisc device against a given hardware
 * path entry.
 * @dev: the generic device (known to be contained by a parisc_device).
 * @index: the current BC index
 * @modpath: the hardware path.
 * @return: true if the device matches the hardware path.
 */
static int match_parisc_device(struct device *dev, int index,
		struct hardware_path *modpath)
{
	struct parisc_device *curr = to_parisc_device(dev);
	char id = (index == 6) ? modpath->mod : modpath->bc[index];

	return (curr->hw_path == id);
}

struct parse_tree_data {
	int index;
	struct hardware_path * modpath;
	struct device * dev;
};

static int check_parent(struct device * dev, void * data)
{
	struct parse_tree_data * d = data;

	if (check_dev(dev)) {
		if (dev->bus == &parisc_bus_type) {
			if (match_parisc_device(dev, d->index, d->modpath))
				d->dev = dev;
		} else if (dev_is_pci(dev)) {
			if (match_pci_device(dev, d->index, d->modpath))
				d->dev = dev;
		} else if (dev->bus == NULL) {
			/* we are on a bus bridge */
			struct device *new = parse_tree_node(dev, d->index, d->modpath);
			if (new)
				d->dev = new;
		}
	}
	return d->dev != NULL;
}

/**
 * parse_tree_node - returns a device entry in the iotree
 * @parent: the parent node in the tree
 * @index: the current BC index
 * @modpath: the hardware_path struct to match a device against
 * @return: The corresponding device if found, NULL otherwise.
 *
 * Checks all the children of @parent for a matching @id.  If none
 * found, it returns NULL.
 */
static struct device *
parse_tree_node(struct device *parent, int index, struct hardware_path *modpath)
{
	struct parse_tree_data d = {
		.index          = index,
		.modpath        = modpath,
	};

	struct recurse_struct recurse_data = {
		.obj	= &d,
		.fn	= check_parent,
	};

	if (device_for_each_child(parent, &recurse_data, descend_children))
		{ /* nothing */ }

	return d.dev;
}

/**
 * hwpath_to_device - Finds the generic device corresponding to a given hardware path.
 * @modpath: the hardware path.
 * @return: The target device, NULL if not found.
 */
struct device *hwpath_to_device(struct hardware_path *modpath)
{
	int i;
	struct device *parent = &root;
	for (i = 0; i < 6; i++) {
		if (modpath->bc[i] == -1)
			continue;
		parent = parse_tree_node(parent, i, modpath);
		if (!parent)
			return NULL;
	}
	if (dev_is_pci(parent)) /* pci devices already parse MOD */
		return parent;
	else
		return parse_tree_node(parent, 6, modpath);
}
EXPORT_SYMBOL(hwpath_to_device);

/**
 * device_to_hwpath - Populates the hwpath corresponding to the given device.
 * @dev: the target device
 * @path: pointer to a previously allocated hwpath struct to be filled in
 */
void device_to_hwpath(struct device *dev, struct hardware_path *path)
{
	struct parisc_device *padev;
	if (dev->bus == &parisc_bus_type) {
		padev = to_parisc_device(dev);
		get_node_path(dev->parent, path);
		path->mod = padev->hw_path;
	} else if (dev_is_pci(dev)) {
		get_node_path(dev, path);
	}
}
EXPORT_SYMBOL(device_to_hwpath);

#define BC_PORT_MASK 0x8
#define BC_LOWER_PORT 0x8

#define BUS_CONVERTER(dev) \
        ((dev->id.hw_type == HPHW_IOA) || (dev->id.hw_type == HPHW_BCPORT))

#define IS_LOWER_PORT(dev) \
        ((gsc_readl(dev->hpa.start + offsetof(struct bc_module, io_status)) \
                & BC_PORT_MASK) == BC_LOWER_PORT)

#define MAX_NATIVE_DEVICES 64
#define NATIVE_DEVICE_OFFSET 0x1000

#define FLEX_MASK 	F_EXTEND(0xfffc0000)
#define IO_IO_LOW	offsetof(struct bc_module, io_io_low)
#define IO_IO_HIGH	offsetof(struct bc_module, io_io_high)
#define READ_IO_IO_LOW(dev)  (unsigned long)(signed int)gsc_readl(dev->hpa.start + IO_IO_LOW)
#define READ_IO_IO_HIGH(dev) (unsigned long)(signed int)gsc_readl(dev->hpa.start + IO_IO_HIGH)

static void walk_native_bus(unsigned long io_io_low, unsigned long io_io_high,
                            struct device *parent);

static void __init walk_lower_bus(struct parisc_device *dev)
{
	unsigned long io_io_low, io_io_high;

	if (!BUS_CONVERTER(dev) || IS_LOWER_PORT(dev))
		return;

	if (dev->id.hw_type == HPHW_IOA) {
		io_io_low = (unsigned long)(signed int)(READ_IO_IO_LOW(dev) << 16);
		io_io_high = io_io_low + MAX_NATIVE_DEVICES * NATIVE_DEVICE_OFFSET;
	} else {
		io_io_low = (READ_IO_IO_LOW(dev) + ~FLEX_MASK) & FLEX_MASK;
		io_io_high = (READ_IO_IO_HIGH(dev)+ ~FLEX_MASK) & FLEX_MASK;
	}

	walk_native_bus(io_io_low, io_io_high, &dev->dev);
}

/**
 * walk_native_bus -- Probe a bus for devices
 * @io_io_low: Base address of this bus.
 * @io_io_high: Last address of this bus.
 * @parent: The parent bus device.
 * 
 * A native bus (eg Runway or GSC) may have up to 64 devices on it,
 * spaced at intervals of 0x1000 bytes.  PDC may not inform us of these
 * devices, so we have to probe for them.  Unfortunately, we may find
 * devices which are not physically connected (such as extra serial &
 * keyboard ports).  This problem is not yet solved.
 */
static void __init walk_native_bus(unsigned long io_io_low,
	unsigned long io_io_high, struct device *parent)
{
	int i, devices_found = 0;
	unsigned long hpa = io_io_low;
	struct hardware_path path;

	get_node_path(parent, &path);
	do {
		for(i = 0; i < MAX_NATIVE_DEVICES; i++, hpa += NATIVE_DEVICE_OFFSET) {
			struct parisc_device *dev;

			/* Was the device already added by Firmware? */
			dev = find_device_by_addr(hpa);
			if (!dev) {
				path.mod = i;
				dev = alloc_pa_dev(hpa, &path);
				if (!dev)
					continue;

				register_parisc_device(dev);
				devices_found++;
			}
			walk_lower_bus(dev);
		}
	} while(!devices_found && hpa < io_io_high);
}

#define CENTRAL_BUS_ADDR F_EXTEND(0xfff80000)

/**
 * walk_central_bus - Find devices attached to the central bus
 *
 * PDC doesn't tell us about all devices in the system.  This routine
 * finds devices connected to the central bus.
 */
void __init walk_central_bus(void)
{
	walk_native_bus(CENTRAL_BUS_ADDR,
			CENTRAL_BUS_ADDR + (MAX_NATIVE_DEVICES * NATIVE_DEVICE_OFFSET),
			&root);
}

static __init void print_parisc_device(struct parisc_device *dev)
{
	static int count __initdata;

	pr_info("%d. %s at %pap { type:%d, hv:%#x, sv:%#x, rev:%#x }",
		++count, dev->name, &(dev->hpa.start), dev->id.hw_type,
		dev->id.hversion, dev->id.sversion, dev->id.hversion_rev);

	if (dev->num_addrs) {
		int k;
		pr_cont(", additional addresses: ");
		for (k = 0; k < dev->num_addrs; k++)
			pr_cont("0x%lx ", dev->addr[k]);
	}
	pr_cont("\n");
}

/**
 * init_parisc_bus - Some preparation to be done before inventory
 */
void __init init_parisc_bus(void)
{
	if (bus_register(&parisc_bus_type))
		panic("Could not register PA-RISC bus type\n");
	if (device_register(&root))
		panic("Could not register PA-RISC root device\n");
	get_device(&root);
}

static __init void qemu_header(void)
{
	int num;
	unsigned long *p;

	pr_info("--- cut here ---\n");
	pr_info("/* AUTO-GENERATED HEADER FILE FOR SEABIOS FIRMWARE */\n");
	pr_cont("/* generated with Linux kernel */\n");
	pr_cont("/* search for PARISC_QEMU_MACHINE_HEADER in Linux */\n\n");

	pr_info("#define PARISC_MODEL \"%s\"\n\n",
			boot_cpu_data.pdc.sys_model_name);

	#define p ((unsigned long *)&boot_cpu_data.pdc.model)
	pr_info("#define PARISC_PDC_MODEL 0x%lx, 0x%lx, 0x%lx, "
		"0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx\n\n",
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
	#undef p

	pr_info("#define PARISC_PDC_VERSION 0x%04lx\n\n",
			boot_cpu_data.pdc.versions);

	pr_info("#define PARISC_PDC_CPUID 0x%04lx\n\n",
			boot_cpu_data.pdc.cpuid);

	pr_info("#define PARISC_PDC_CAPABILITIES 0x%04lx\n\n",
			boot_cpu_data.pdc.capabilities);

	pr_info("#define PARISC_PDC_ENTRY_ORG 0x%04lx\n\n",
#ifdef CONFIG_64BIT
		(unsigned long)(PAGE0->mem_pdc_hi) << 32 |
#endif
		(unsigned long)PAGE0->mem_pdc);

	pr_info("#define PARISC_PDC_CACHE_INFO");
	p = (unsigned long *) &cache_info;
	for (num = 0; num < sizeof(cache_info); num += sizeof(unsigned long)) {
		if (((num % 5) == 0)) {
			pr_cont(" \\\n");
			pr_info("\t");
		}
		pr_cont("%s0x%04lx",
			num?", ":"", *p++);
	}
	pr_cont("\n\n");
}

static __init int qemu_print_hpa(struct device *lin_dev, void *data)
{
	struct parisc_device *dev = to_parisc_device(lin_dev);
	unsigned long hpa = dev->hpa.start;

	pr_cont("\t{\t.hpa = 0x%08lx,\\\n", hpa);
	pr_cont("\t\t.iodc = &iodc_data_hpa_%08lx,\\\n", hpa);
	pr_cont("\t\t.mod_info = &mod_info_hpa_%08lx,\\\n", hpa);
	pr_cont("\t\t.mod_path = &mod_path_hpa_%08lx,\\\n", hpa);
	pr_cont("\t\t.num_addr = HPA_%08lx_num_addr,\\\n", hpa);
	pr_cont("\t\t.add_addr = { HPA_%08lx_add_addr } },\\\n", hpa);
	return 0;
}


static __init void qemu_footer(void)
{
	pr_info("\n\n#define PARISC_DEVICE_LIST \\\n");
	for_each_padev(qemu_print_hpa, NULL);
	pr_cont("\t{ 0, }\n");
	pr_info("--- cut here ---\n");
}

/* print iodc data of the various hpa modules for qemu inclusion */
static __init int qemu_print_iodc_data(struct device *lin_dev, void *data)
{
	struct parisc_device *dev = to_parisc_device(lin_dev);
	unsigned long count;
	unsigned long hpa = dev->hpa.start;
	int status;
	struct pdc_iodc iodc_data;

	int mod_index;
	struct pdc_system_map_mod_info pdc_mod_info;
	struct pdc_module_path mod_path;

	status = pdc_iodc_read(&count, hpa, 0,
		&iodc_data, sizeof(iodc_data));
	if (status != PDC_OK) {
		pr_info("No IODC data for hpa 0x%08lx\n", hpa);
		return 0;
	}

	pr_info("\n");

	/* Prevent hung task messages when printing on serial console */
	cond_resched();

	pr_info("#define HPA_%08lx_DESCRIPTION \"%s\"\n",
		hpa, parisc_hardware_description(&dev->id));

	mod_index = 0;
	do {
		status = pdc_system_map_find_mods(&pdc_mod_info,
				&mod_path, mod_index++);
	} while (status == PDC_OK && pdc_mod_info.mod_addr != hpa);

	pr_info("static struct pdc_system_map_mod_info"
		" mod_info_hpa_%08lx = {\n", hpa);
	#define DO(member) \
		pr_cont("\t." #member " = 0x%x,\n", \
			(unsigned int)pdc_mod_info.member)
	DO(mod_addr);
	DO(mod_pgs);
	DO(add_addrs);
	pr_cont("};\n");
	#undef DO
	pr_info("static struct pdc_module_path "
		"mod_path_hpa_%08lx = {\n", hpa);
	pr_cont("\t.path = { ");
	pr_cont(".flags = 0x%x, ", mod_path.path.flags);
	pr_cont(".bc = { 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x }, ",
		(unsigned char)mod_path.path.bc[0],
		(unsigned char)mod_path.path.bc[1],
		(unsigned char)mod_path.path.bc[2],
		(unsigned char)mod_path.path.bc[3],
		(unsigned char)mod_path.path.bc[4],
		(unsigned char)mod_path.path.bc[5]);
	pr_cont(".mod = 0x%x ", mod_path.path.mod);
	pr_cont(" },\n");
	pr_cont("\t.layers = { 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x }\n",
		mod_path.layers[0], mod_path.layers[1], mod_path.layers[2],
		mod_path.layers[3], mod_path.layers[4], mod_path.layers[5]);
	pr_cont("};\n");

	pr_info("static struct pdc_iodc iodc_data_hpa_%08lx = {\n", hpa);
	#define DO(member) \
		pr_cont("\t." #member " = 0x%04lx,\n", \
			(unsigned long)iodc_data.member)
	DO(hversion_model);
	DO(hversion);
	DO(spa);
	DO(type);
	DO(sversion_rev);
	DO(sversion_model);
	DO(sversion_opt);
	DO(rev);
	DO(dep);
	DO(features);
	DO(checksum);
	DO(length);
	#undef DO
	pr_cont("\t/* pad: 0x%04x, 0x%04x */\n",
		iodc_data.pad[0], iodc_data.pad[1]);
	pr_cont("};\n");

	pr_info("#define HPA_%08lx_num_addr %d\n", hpa, dev->num_addrs);
	pr_info("#define HPA_%08lx_add_addr ", hpa);
	count = 0;
	if (dev->num_addrs == 0)
		pr_cont("0");
	while (count < dev->num_addrs) {
		pr_cont("0x%08lx, ", dev->addr[count]);
		count++;
	}
	pr_cont("\n\n");

	return 0;
}



static __init int print_one_device(struct device * dev, void * data)
{
	struct parisc_device * pdev = to_parisc_device(dev);

	if (check_dev(dev))
		print_parisc_device(pdev);
	return 0;
}

/**
 * print_parisc_devices - Print out a list of devices found in this system
 */
void __init print_parisc_devices(void)
{
	for_each_padev(print_one_device, NULL);
	#define PARISC_QEMU_MACHINE_HEADER 0
	if (PARISC_QEMU_MACHINE_HEADER) {
		qemu_header();
		for_each_padev(qemu_print_iodc_data, NULL);
		qemu_footer();
	}
}
