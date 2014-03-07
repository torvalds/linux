#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <asm/numa.h>
#include <asm/pci_x86.h>

struct pci_root_info {
	struct acpi_device *bridge;
	char name[16];
	unsigned int res_num;
	struct resource *res;
	resource_size_t *res_offset;
	struct pci_sysdata sd;
#ifdef	CONFIG_PCI_MMCONFIG
	bool mcfg_added;
	u16 segment;
	u8 start_bus;
	u8 end_bus;
#endif
};

static bool pci_use_crs = true;
static bool pci_ignore_seg = false;

static int __init set_use_crs(const struct dmi_system_id *id)
{
	pci_use_crs = true;
	return 0;
}

static int __init set_nouse_crs(const struct dmi_system_id *id)
{
	pci_use_crs = false;
	return 0;
}

static int __init set_ignore_seg(const struct dmi_system_id *id)
{
	printk(KERN_INFO "PCI: %s detected: ignoring ACPI _SEG\n", id->ident);
	pci_ignore_seg = true;
	return 0;
}

static const struct dmi_system_id pci_crs_quirks[] __initconst = {
	/* http://bugzilla.kernel.org/show_bug.cgi?id=14183 */
	{
		.callback = set_use_crs,
		.ident = "IBM System x3800",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3800"),
		},
	},
	/* https://bugzilla.kernel.org/show_bug.cgi?id=16007 */
	/* 2006 AMD HT/VIA system with two host bridges */
        {
		.callback = set_use_crs,
		.ident = "ASRock ALiveSATA2-GLAN",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "ALiveSATA2-GLAN"),
                },
        },
	/* https://bugzilla.kernel.org/show_bug.cgi?id=30552 */
	/* 2006 AMD HT/VIA system with two host bridges */
	{
		.callback = set_use_crs,
		.ident = "ASUS M2V-MX SE",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "M2V-MX SE"),
			DMI_MATCH(DMI_BIOS_VENDOR, "American Megatrends Inc."),
		},
	},
	/* https://bugzilla.kernel.org/show_bug.cgi?id=42619 */
	{
		.callback = set_use_crs,
		.ident = "MSI MS-7253",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "MICRO-STAR INTERNATIONAL CO., LTD"),
			DMI_MATCH(DMI_BOARD_NAME, "MS-7253"),
			DMI_MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies, LTD"),
		},
	},

	/* Now for the blacklist.. */

	/* https://bugzilla.redhat.com/show_bug.cgi?id=769657 */
	{
		.callback = set_nouse_crs,
		.ident = "Dell Studio 1557",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Studio 1557"),
			DMI_MATCH(DMI_BIOS_VERSION, "A09"),
		},
	},
	/* https://bugzilla.redhat.com/show_bug.cgi?id=769657 */
	{
		.callback = set_nouse_crs,
		.ident = "Thinkpad SL510",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_BOARD_NAME, "2847DFG"),
			DMI_MATCH(DMI_BIOS_VERSION, "6JET85WW (1.43 )"),
		},
	},

	/* https://bugzilla.kernel.org/show_bug.cgi?id=15362 */
	{
		.callback = set_ignore_seg,
		.ident = "HP xw9300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP xw9300 Workstation"),
		},
	},
	{}
};

void __init pci_acpi_crs_quirks(void)
{
	int year;

	if (dmi_get_date(DMI_BIOS_DATE, &year, NULL, NULL) && year < 2008)
		pci_use_crs = false;

	dmi_check_system(pci_crs_quirks);

	/*
	 * If the user specifies "pci=use_crs" or "pci=nocrs" explicitly, that
	 * takes precedence over anything we figured out above.
	 */
	if (pci_probe & PCI_ROOT_NO_CRS)
		pci_use_crs = false;
	else if (pci_probe & PCI_USE__CRS)
		pci_use_crs = true;

	printk(KERN_INFO "PCI: %s host bridge windows from ACPI; "
	       "if necessary, use \"pci=%s\" and report a bug\n",
	       pci_use_crs ? "Using" : "Ignoring",
	       pci_use_crs ? "nocrs" : "use_crs");
}

#ifdef	CONFIG_PCI_MMCONFIG
static int check_segment(u16 seg, struct device *dev, char *estr)
{
	if (seg) {
		dev_err(dev,
			"%s can't access PCI configuration "
			"space under this host bridge.\n",
			estr);
		return -EIO;
	}

	/*
	 * Failure in adding MMCFG information is not fatal,
	 * just can't access extended configuration space of
	 * devices under this host bridge.
	 */
	dev_warn(dev,
		 "%s can't access extended PCI configuration "
		 "space under this bridge.\n",
		 estr);

	return 0;
}

static int setup_mcfg_map(struct pci_root_info *info, u16 seg, u8 start,
			  u8 end, phys_addr_t addr)
{
	int result;
	struct device *dev = &info->bridge->dev;

	info->start_bus = start;
	info->end_bus = end;
	info->mcfg_added = false;

	/* return success if MMCFG is not in use */
	if (raw_pci_ext_ops && raw_pci_ext_ops != &pci_mmcfg)
		return 0;

	if (!(pci_probe & PCI_PROBE_MMCONF))
		return check_segment(seg, dev, "MMCONFIG is disabled,");

	result = pci_mmconfig_insert(dev, seg, start, end, addr);
	if (result == 0) {
		/* enable MMCFG if it hasn't been enabled yet */
		if (raw_pci_ext_ops == NULL)
			raw_pci_ext_ops = &pci_mmcfg;
		info->mcfg_added = true;
	} else if (result != -EEXIST)
		return check_segment(seg, dev,
			 "fail to add MMCONFIG information,");

	return 0;
}

static void teardown_mcfg_map(struct pci_root_info *info)
{
	if (info->mcfg_added) {
		pci_mmconfig_delete(info->segment, info->start_bus,
				    info->end_bus);
		info->mcfg_added = false;
	}
}
#else
static int setup_mcfg_map(struct pci_root_info *info,
				    u16 seg, u8 start, u8 end,
				    phys_addr_t addr)
{
	return 0;
}
static void teardown_mcfg_map(struct pci_root_info *info)
{
}
#endif

static acpi_status
resource_to_addr(struct acpi_resource *resource,
			struct acpi_resource_address64 *addr)
{
	acpi_status status;
	struct acpi_resource_memory24 *memory24;
	struct acpi_resource_memory32 *memory32;
	struct acpi_resource_fixed_memory32 *fixed_memory32;

	memset(addr, 0, sizeof(*addr));
	switch (resource->type) {
	case ACPI_RESOURCE_TYPE_MEMORY24:
		memory24 = &resource->data.memory24;
		addr->resource_type = ACPI_MEMORY_RANGE;
		addr->minimum = memory24->minimum;
		addr->address_length = memory24->address_length;
		addr->maximum = addr->minimum + addr->address_length - 1;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_MEMORY32:
		memory32 = &resource->data.memory32;
		addr->resource_type = ACPI_MEMORY_RANGE;
		addr->minimum = memory32->minimum;
		addr->address_length = memory32->address_length;
		addr->maximum = addr->minimum + addr->address_length - 1;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		fixed_memory32 = &resource->data.fixed_memory32;
		addr->resource_type = ACPI_MEMORY_RANGE;
		addr->minimum = fixed_memory32->address;
		addr->address_length = fixed_memory32->address_length;
		addr->maximum = addr->minimum + addr->address_length - 1;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_ADDRESS16:
	case ACPI_RESOURCE_TYPE_ADDRESS32:
	case ACPI_RESOURCE_TYPE_ADDRESS64:
		status = acpi_resource_to_address64(resource, addr);
		if (ACPI_SUCCESS(status) &&
		    (addr->resource_type == ACPI_MEMORY_RANGE ||
		    addr->resource_type == ACPI_IO_RANGE) &&
		    addr->address_length > 0) {
			return AE_OK;
		}
		break;
	}
	return AE_ERROR;
}

static acpi_status
count_resource(struct acpi_resource *acpi_res, void *data)
{
	struct pci_root_info *info = data;
	struct acpi_resource_address64 addr;
	acpi_status status;

	status = resource_to_addr(acpi_res, &addr);
	if (ACPI_SUCCESS(status))
		info->res_num++;
	return AE_OK;
}

static acpi_status
setup_resource(struct acpi_resource *acpi_res, void *data)
{
	struct pci_root_info *info = data;
	struct resource *res;
	struct acpi_resource_address64 addr;
	acpi_status status;
	unsigned long flags;
	u64 start, orig_end, end;

	status = resource_to_addr(acpi_res, &addr);
	if (!ACPI_SUCCESS(status))
		return AE_OK;

	if (addr.resource_type == ACPI_MEMORY_RANGE) {
		flags = IORESOURCE_MEM;
		if (addr.info.mem.caching == ACPI_PREFETCHABLE_MEMORY)
			flags |= IORESOURCE_PREFETCH;
	} else if (addr.resource_type == ACPI_IO_RANGE) {
		flags = IORESOURCE_IO;
	} else
		return AE_OK;

	start = addr.minimum + addr.translation_offset;
	orig_end = end = addr.maximum + addr.translation_offset;

	/* Exclude non-addressable range or non-addressable portion of range */
	end = min(end, (u64)iomem_resource.end);
	if (end <= start) {
		dev_info(&info->bridge->dev,
			"host bridge window [%#llx-%#llx] "
			"(ignored, not CPU addressable)\n", start, orig_end);
		return AE_OK;
	} else if (orig_end != end) {
		dev_info(&info->bridge->dev,
			"host bridge window [%#llx-%#llx] "
			"([%#llx-%#llx] ignored, not CPU addressable)\n", 
			start, orig_end, end + 1, orig_end);
	}

	res = &info->res[info->res_num];
	res->name = info->name;
	res->flags = flags;
	res->start = start;
	res->end = end;
	info->res_offset[info->res_num] = addr.translation_offset;
	info->res_num++;

	if (!pci_use_crs)
		dev_printk(KERN_DEBUG, &info->bridge->dev,
			   "host bridge window %pR (ignored)\n", res);

	return AE_OK;
}

static void coalesce_windows(struct pci_root_info *info, unsigned long type)
{
	int i, j;
	struct resource *res1, *res2;

	for (i = 0; i < info->res_num; i++) {
		res1 = &info->res[i];
		if (!(res1->flags & type))
			continue;

		for (j = i + 1; j < info->res_num; j++) {
			res2 = &info->res[j];
			if (!(res2->flags & type))
				continue;

			/*
			 * I don't like throwing away windows because then
			 * our resources no longer match the ACPI _CRS, but
			 * the kernel resource tree doesn't allow overlaps.
			 */
			if (resource_overlaps(res1, res2)) {
				res2->start = min(res1->start, res2->start);
				res2->end = max(res1->end, res2->end);
				dev_info(&info->bridge->dev,
					 "host bridge window expanded to %pR; %pR ignored\n",
					 res2, res1);
				res1->flags = 0;
			}
		}
	}
}

static void add_resources(struct pci_root_info *info,
			  struct list_head *resources)
{
	int i;
	struct resource *res, *root, *conflict;

	coalesce_windows(info, IORESOURCE_MEM);
	coalesce_windows(info, IORESOURCE_IO);

	for (i = 0; i < info->res_num; i++) {
		res = &info->res[i];

		if (res->flags & IORESOURCE_MEM)
			root = &iomem_resource;
		else if (res->flags & IORESOURCE_IO)
			root = &ioport_resource;
		else
			continue;

		conflict = insert_resource_conflict(root, res);
		if (conflict)
			dev_info(&info->bridge->dev,
				 "ignoring host bridge window %pR (conflicts with %s %pR)\n",
				 res, conflict->name, conflict);
		else
			pci_add_resource_offset(resources, res,
					info->res_offset[i]);
	}
}

static void free_pci_root_info_res(struct pci_root_info *info)
{
	kfree(info->res);
	info->res = NULL;
	kfree(info->res_offset);
	info->res_offset = NULL;
	info->res_num = 0;
}

static void __release_pci_root_info(struct pci_root_info *info)
{
	int i;
	struct resource *res;

	for (i = 0; i < info->res_num; i++) {
		res = &info->res[i];

		if (!res->parent)
			continue;

		if (!(res->flags & (IORESOURCE_MEM | IORESOURCE_IO)))
			continue;

		release_resource(res);
	}

	free_pci_root_info_res(info);

	teardown_mcfg_map(info);

	kfree(info);
}

static void release_pci_root_info(struct pci_host_bridge *bridge)
{
	struct pci_root_info *info = bridge->release_data;

	__release_pci_root_info(info);
}

static void
probe_pci_root_info(struct pci_root_info *info, struct acpi_device *device,
		    int busnum, int domain)
{
	size_t size;

	sprintf(info->name, "PCI Bus %04x:%02x", domain, busnum);
	info->bridge = device;

	info->res_num = 0;
	acpi_walk_resources(device->handle, METHOD_NAME__CRS, count_resource,
				info);
	if (!info->res_num)
		return;

	size = sizeof(*info->res) * info->res_num;
	info->res = kzalloc(size, GFP_KERNEL);
	if (!info->res) {
		info->res_num = 0;
		return;
	}

	size = sizeof(*info->res_offset) * info->res_num;
	info->res_num = 0;
	info->res_offset = kzalloc(size, GFP_KERNEL);
	if (!info->res_offset) {
		kfree(info->res);
		info->res = NULL;
		return;
	}

	acpi_walk_resources(device->handle, METHOD_NAME__CRS, setup_resource,
				info);
}

struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root)
{
	struct acpi_device *device = root->device;
	struct pci_root_info *info = NULL;
	int domain = root->segment;
	int busnum = root->secondary.start;
	LIST_HEAD(resources);
	struct pci_bus *bus = NULL;
	struct pci_sysdata *sd;
	int node;
#ifdef CONFIG_ACPI_NUMA
	int pxm;
#endif

	if (pci_ignore_seg)
		domain = 0;

	if (domain && !pci_domains_supported) {
		printk(KERN_WARNING "pci_bus %04x:%02x: "
		       "ignored (multiple domains not supported)\n",
		       domain, busnum);
		return NULL;
	}

	node = -1;
#ifdef CONFIG_ACPI_NUMA
	pxm = acpi_get_pxm(device->handle);
	if (pxm >= 0)
		node = pxm_to_node(pxm);
	if (node != -1)
		set_mp_bus_to_node(busnum, node);
	else
#endif
		node = get_mp_bus_to_node(busnum);

	if (node != -1 && !node_online(node))
		node = -1;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		printk(KERN_WARNING "pci_bus %04x:%02x: "
		       "ignored (out of memory)\n", domain, busnum);
		return NULL;
	}

	sd = &info->sd;
	sd->domain = domain;
	sd->node = node;
	sd->companion = device;
	/*
	 * Maybe the desired pci bus has been already scanned. In such case
	 * it is unnecessary to scan the pci bus with the given domain,busnum.
	 */
	bus = pci_find_bus(domain, busnum);
	if (bus) {
		/*
		 * If the desired bus exits, the content of bus->sysdata will
		 * be replaced by sd.
		 */
		memcpy(bus->sysdata, sd, sizeof(*sd));
		kfree(info);
	} else {
		probe_pci_root_info(info, device, busnum, domain);

		/* insert busn res at first */
		pci_add_resource(&resources,  &root->secondary);
		/*
		 * _CRS with no apertures is normal, so only fall back to
		 * defaults or native bridge info if we're ignoring _CRS.
		 */
		if (pci_use_crs)
			add_resources(info, &resources);
		else {
			free_pci_root_info_res(info);
			x86_pci_root_bus_resources(busnum, &resources);
		}

		if (!setup_mcfg_map(info, domain, (u8)root->secondary.start,
				    (u8)root->secondary.end, root->mcfg_addr))
			bus = pci_create_root_bus(NULL, busnum, &pci_root_ops,
						  sd, &resources);

		if (bus) {
			pci_scan_child_bus(bus);
			pci_set_host_bridge_release(
				to_pci_host_bridge(bus->bridge),
				release_pci_root_info, info);
		} else {
			pci_free_resource_list(&resources);
			__release_pci_root_info(info);
		}
	}

	/* After the PCI-E bus has been walked and all devices discovered,
	 * configure any settings of the fabric that might be necessary.
	 */
	if (bus) {
		struct pci_bus *child;
		list_for_each_entry(child, &bus->children, node)
			pcie_bus_configure_settings(child);
	}

	if (bus && node != -1) {
#ifdef CONFIG_ACPI_NUMA
		if (pxm >= 0)
			dev_printk(KERN_DEBUG, &bus->dev,
				   "on NUMA node %d (pxm %d)\n", node, pxm);
#else
		dev_printk(KERN_DEBUG, &bus->dev, "on NUMA node %d\n", node);
#endif
	}

	return bus;
}

int pcibios_root_bridge_prepare(struct pci_host_bridge *bridge)
{
	struct pci_sysdata *sd = bridge->bus->sysdata;

	ACPI_COMPANION_SET(&bridge->dev, sd->companion);
	return 0;
}

int __init pci_acpi_init(void)
{
	struct pci_dev *dev = NULL;

	if (acpi_noirq)
		return -ENODEV;

	printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");
	acpi_irq_penalty_init();
	pcibios_enable_irq = acpi_pci_irq_enable;
	pcibios_disable_irq = acpi_pci_irq_disable;
	x86_init.pci.init_irq = x86_init_noop;

	if (pci_routeirq) {
		/*
		 * PCI IRQ routing is set up by pci_enable_device(), but we
		 * also do it here in case there are still broken drivers that
		 * don't use pci_enable_device().
		 */
		printk(KERN_INFO "PCI: Routing PCI interrupts for all devices because \"pci=routeirq\" specified\n");
		for_each_pci_dev(dev)
			acpi_pci_irq_enable(dev);
	}

	return 0;
}
