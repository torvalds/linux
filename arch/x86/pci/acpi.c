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

static void validate_resources(struct device *dev, struct list_head *crs_res,
			       unsigned long type)
{
	LIST_HEAD(list);
	struct resource *res1, *res2, *root = NULL;
	struct resource_entry *tmp, *entry, *entry2;

	BUG_ON((type & (IORESOURCE_MEM | IORESOURCE_IO)) == 0);
	root = (type & IORESOURCE_MEM) ? &iomem_resource : &ioport_resource;

	list_splice_init(crs_res, &list);
	resource_list_for_each_entry_safe(entry, tmp, &list) {
		bool free = false;
		resource_size_t end;

		res1 = entry->res;
		if (!(res1->flags & type))
			goto next;

		/* Exclude non-addressable range or non-addressable portion */
		end = min(res1->end, root->end);
		if (end <= res1->start) {
			dev_info(dev, "host bridge window %pR (ignored, not CPU addressable)\n",
				 res1);
			free = true;
			goto next;
		} else if (res1->end != end) {
			dev_info(dev, "host bridge window %pR ([%#llx-%#llx] ignored, not CPU addressable)\n",
				 res1, (unsigned long long)end + 1,
				 (unsigned long long)res1->end);
			res1->end = end;
		}

		resource_list_for_each_entry(entry2, crs_res) {
			res2 = entry2->res;
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
				dev_info(dev, "host bridge window expanded to %pR; %pR ignored\n",
					 res2, res1);
				free = true;
				goto next;
			}
		}

next:
		resource_list_del(entry);
		if (free)
			resource_list_free_entry(entry);
		else
			resource_list_add_tail(entry, crs_res);
	}
}

static void add_resources(struct pci_root_info *info,
			  struct list_head *resources,
			  struct list_head *crs_res)
{
	struct resource_entry *entry, *tmp;
	struct resource *res, *conflict, *root = NULL;

	validate_resources(&info->bridge->dev, crs_res, IORESOURCE_MEM);
	validate_resources(&info->bridge->dev, crs_res, IORESOURCE_IO);

	resource_list_for_each_entry_safe(entry, tmp, crs_res) {
		res = entry->res;
		if (res->flags & IORESOURCE_MEM)
			root = &iomem_resource;
		else if (res->flags & IORESOURCE_IO)
			root = &ioport_resource;
		else
			BUG_ON(res);

		conflict = insert_resource_conflict(root, res);
		if (conflict) {
			dev_info(&info->bridge->dev,
				 "ignoring host bridge window %pR (conflicts with %s %pR)\n",
				 res, conflict->name, conflict);
			resource_list_destroy_entry(entry);
		}
	}

	list_splice_tail(crs_res, resources);
}

static void release_pci_root_info(struct pci_host_bridge *bridge)
{
	struct resource *res;
	struct resource_entry *entry;
	struct pci_root_info *info = bridge->release_data;

	resource_list_for_each_entry(entry, &bridge->windows) {
		res = entry->res;
		if (res->parent &&
		    (res->flags & (IORESOURCE_MEM | IORESOURCE_IO)))
			release_resource(res);
	}

	teardown_mcfg_map(info);
	kfree(info);
}

static void probe_pci_root_info(struct pci_root_info *info,
				struct acpi_device *device,
				int busnum, int domain,
				struct list_head *list)
{
	int ret;
	struct resource_entry *entry;

	sprintf(info->name, "PCI Bus %04x:%02x", domain, busnum);
	info->bridge = device;
	ret = acpi_dev_get_resources(device, list,
				     acpi_dev_filter_resource_type_cb,
				     (void *)(IORESOURCE_IO | IORESOURCE_MEM));
	if (ret < 0)
		dev_warn(&device->dev,
			 "failed to parse _CRS method, error code %d\n", ret);
	else if (ret == 0)
		dev_dbg(&device->dev,
			"no IO and memory resources present in _CRS\n");
	else
		resource_list_for_each_entry(entry, list)
			entry->res->name = info->name;
}

struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root)
{
	struct acpi_device *device = root->device;
	struct pci_root_info *info;
	int domain = root->segment;
	int busnum = root->secondary.start;
	struct resource_entry *res_entry;
	LIST_HEAD(crs_res);
	LIST_HEAD(resources);
	struct pci_bus *bus;
	struct pci_sysdata *sd;
	int node;

	if (pci_ignore_seg)
		domain = 0;

	if (domain && !pci_domains_supported) {
		printk(KERN_WARNING "pci_bus %04x:%02x: "
		       "ignored (multiple domains not supported)\n",
		       domain, busnum);
		return NULL;
	}

	node = acpi_get_node(device->handle);
	if (node == NUMA_NO_NODE) {
		node = x86_pci_root_bus_node(busnum);
		if (node != 0 && node != NUMA_NO_NODE)
			dev_info(&device->dev, FW_BUG "no _PXM; falling back to node %d from hardware (may be inconsistent with ACPI node numbers)\n",
				node);
	}

	if (node != NUMA_NO_NODE && !node_online(node))
		node = NUMA_NO_NODE;

	info = kzalloc_node(sizeof(*info), GFP_KERNEL, node);
	if (!info) {
		printk(KERN_WARNING "pci_bus %04x:%02x: "
		       "ignored (out of memory)\n", domain, busnum);
		return NULL;
	}

	sd = &info->sd;
	sd->domain = domain;
	sd->node = node;
	sd->companion = device;

	bus = pci_find_bus(domain, busnum);
	if (bus) {
		/*
		 * If the desired bus has been scanned already, replace
		 * its bus->sysdata.
		 */
		memcpy(bus->sysdata, sd, sizeof(*sd));
		kfree(info);
	} else {
		/* insert busn res at first */
		pci_add_resource(&resources,  &root->secondary);

		/*
		 * _CRS with no apertures is normal, so only fall back to
		 * defaults or native bridge info if we're ignoring _CRS.
		 */
		probe_pci_root_info(info, device, busnum, domain, &crs_res);
		if (pci_use_crs) {
			add_resources(info, &resources, &crs_res);
		} else {
			resource_list_for_each_entry(res_entry, &crs_res)
				dev_printk(KERN_DEBUG, &device->dev,
					   "host bridge window %pR (ignored)\n",
					   res_entry->res);
			resource_list_free(&crs_res);
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
			resource_list_free(&resources);
			teardown_mcfg_map(info);
			kfree(info);
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

	if (bus && node != NUMA_NO_NODE)
		dev_printk(KERN_DEBUG, &bus->dev, "on NUMA node %d\n", node);

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
