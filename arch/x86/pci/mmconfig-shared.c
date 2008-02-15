/*
 * mmconfig-shared.c - Low-level direct PCI config space access via
 *                     MMCONFIG - common code between i386 and x86-64.
 *
 * This code does:
 * - known chipset handling
 * - ACPI decoding and validation
 *
 * Per-architecture code takes care of the mappings and accesses
 * themselves.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <asm/e820.h>

#include "pci.h"

/* aperture is up to 256MB but BIOS may reserve less */
#define MMCONFIG_APER_MIN	(2 * 1024*1024)
#define MMCONFIG_APER_MAX	(256 * 1024*1024)

/* Indicate if the mmcfg resources have been placed into the resource table. */
static int __initdata pci_mmcfg_resources_inserted;

static const char __init *pci_mmcfg_e7520(void)
{
	u32 win;
	pci_direct_conf1.read(0, 0, PCI_DEVFN(0,0), 0xce, 2, &win);

	win = win & 0xf000;
	if(win == 0x0000 || win == 0xf000)
		pci_mmcfg_config_num = 0;
	else {
		pci_mmcfg_config_num = 1;
		pci_mmcfg_config = kzalloc(sizeof(pci_mmcfg_config[0]), GFP_KERNEL);
		if (!pci_mmcfg_config)
			return NULL;
		pci_mmcfg_config[0].address = win << 16;
		pci_mmcfg_config[0].pci_segment = 0;
		pci_mmcfg_config[0].start_bus_number = 0;
		pci_mmcfg_config[0].end_bus_number = 255;
	}

	return "Intel Corporation E7520 Memory Controller Hub";
}

static const char __init *pci_mmcfg_intel_945(void)
{
	u32 pciexbar, mask = 0, len = 0;

	pci_mmcfg_config_num = 1;

	pci_direct_conf1.read(0, 0, PCI_DEVFN(0,0), 0x48, 4, &pciexbar);

	/* Enable bit */
	if (!(pciexbar & 1))
		pci_mmcfg_config_num = 0;

	/* Size bits */
	switch ((pciexbar >> 1) & 3) {
	case 0:
		mask = 0xf0000000U;
		len  = 0x10000000U;
		break;
	case 1:
		mask = 0xf8000000U;
		len  = 0x08000000U;
		break;
	case 2:
		mask = 0xfc000000U;
		len  = 0x04000000U;
		break;
	default:
		pci_mmcfg_config_num = 0;
	}

	/* Errata #2, things break when not aligned on a 256Mb boundary */
	/* Can only happen in 64M/128M mode */

	if ((pciexbar & mask) & 0x0fffffffU)
		pci_mmcfg_config_num = 0;

	/* Don't hit the APIC registers and their friends */
	if ((pciexbar & mask) >= 0xf0000000U)
		pci_mmcfg_config_num = 0;

	if (pci_mmcfg_config_num) {
		pci_mmcfg_config = kzalloc(sizeof(pci_mmcfg_config[0]), GFP_KERNEL);
		if (!pci_mmcfg_config)
			return NULL;
		pci_mmcfg_config[0].address = pciexbar & mask;
		pci_mmcfg_config[0].pci_segment = 0;
		pci_mmcfg_config[0].start_bus_number = 0;
		pci_mmcfg_config[0].end_bus_number = (len >> 20) - 1;
	}

	return "Intel Corporation 945G/GZ/P/PL Express Memory Controller Hub";
}

struct pci_mmcfg_hostbridge_probe {
	u32 vendor;
	u32 device;
	const char *(*probe)(void);
};

static struct pci_mmcfg_hostbridge_probe pci_mmcfg_probes[] __initdata = {
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_E7520_MCH, pci_mmcfg_e7520 },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82945G_HB, pci_mmcfg_intel_945 },
};

static int __init pci_mmcfg_check_hostbridge(void)
{
	u32 l;
	u16 vendor, device;
	int i;
	const char *name;

	pci_direct_conf1.read(0, 0, PCI_DEVFN(0,0), 0, 4, &l);
	vendor = l & 0xffff;
	device = (l >> 16) & 0xffff;

	pci_mmcfg_config_num = 0;
	pci_mmcfg_config = NULL;
	name = NULL;

	for (i = 0; !name && i < ARRAY_SIZE(pci_mmcfg_probes); i++) {
		if (pci_mmcfg_probes[i].vendor == vendor &&
		    pci_mmcfg_probes[i].device == device)
			name = pci_mmcfg_probes[i].probe();
	}

	if (name) {
		printk(KERN_INFO "PCI: Found %s %s MMCONFIG support.\n",
		       name, pci_mmcfg_config_num ? "with" : "without");
	}

	return name != NULL;
}

static void __init pci_mmcfg_insert_resources(unsigned long resource_flags)
{
#define PCI_MMCFG_RESOURCE_NAME_LEN 19
	int i;
	struct resource *res;
	char *names;
	unsigned num_buses;

	res = kcalloc(PCI_MMCFG_RESOURCE_NAME_LEN + sizeof(*res),
			pci_mmcfg_config_num, GFP_KERNEL);
	if (!res) {
		printk(KERN_ERR "PCI: Unable to allocate MMCONFIG resources\n");
		return;
	}

	names = (void *)&res[pci_mmcfg_config_num];
	for (i = 0; i < pci_mmcfg_config_num; i++, res++) {
		struct acpi_mcfg_allocation *cfg = &pci_mmcfg_config[i];
		num_buses = cfg->end_bus_number - cfg->start_bus_number + 1;
		res->name = names;
		snprintf(names, PCI_MMCFG_RESOURCE_NAME_LEN, "PCI MMCONFIG %u",
			 cfg->pci_segment);
		res->start = cfg->address;
		res->end = res->start + (num_buses << 20) - 1;
		res->flags = IORESOURCE_MEM | resource_flags;
		insert_resource(&iomem_resource, res);
		names += PCI_MMCFG_RESOURCE_NAME_LEN;
	}

	/* Mark that the resources have been inserted. */
	pci_mmcfg_resources_inserted = 1;
}

static acpi_status __init check_mcfg_resource(struct acpi_resource *res,
					      void *data)
{
	struct resource *mcfg_res = data;
	struct acpi_resource_address64 address;
	acpi_status status;

	if (res->type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32) {
		struct acpi_resource_fixed_memory32 *fixmem32 =
			&res->data.fixed_memory32;
		if (!fixmem32)
			return AE_OK;
		if ((mcfg_res->start >= fixmem32->address) &&
		    (mcfg_res->end < (fixmem32->address +
				      fixmem32->address_length))) {
			mcfg_res->flags = 1;
			return AE_CTRL_TERMINATE;
		}
	}
	if ((res->type != ACPI_RESOURCE_TYPE_ADDRESS32) &&
	    (res->type != ACPI_RESOURCE_TYPE_ADDRESS64))
		return AE_OK;

	status = acpi_resource_to_address64(res, &address);
	if (ACPI_FAILURE(status) ||
	   (address.address_length <= 0) ||
	   (address.resource_type != ACPI_MEMORY_RANGE))
		return AE_OK;

	if ((mcfg_res->start >= address.minimum) &&
	    (mcfg_res->end < (address.minimum + address.address_length))) {
		mcfg_res->flags = 1;
		return AE_CTRL_TERMINATE;
	}
	return AE_OK;
}

static acpi_status __init find_mboard_resource(acpi_handle handle, u32 lvl,
		void *context, void **rv)
{
	struct resource *mcfg_res = context;

	acpi_walk_resources(handle, METHOD_NAME__CRS,
			    check_mcfg_resource, context);

	if (mcfg_res->flags)
		return AE_CTRL_TERMINATE;

	return AE_OK;
}

static int __init is_acpi_reserved(unsigned long start, unsigned long end)
{
	struct resource mcfg_res;

	mcfg_res.start = start;
	mcfg_res.end = end;
	mcfg_res.flags = 0;

	acpi_get_devices("PNP0C01", find_mboard_resource, &mcfg_res, NULL);

	if (!mcfg_res.flags)
		acpi_get_devices("PNP0C02", find_mboard_resource, &mcfg_res,
				 NULL);

	return mcfg_res.flags;
}

static void __init pci_mmcfg_reject_broken(void)
{
	typeof(pci_mmcfg_config[0]) *cfg;
	int i;

	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].address == 0))
		return;

	cfg = &pci_mmcfg_config[0];

	/*
	 * Handle more broken MCFG tables on Asus etc.
	 * They only contain a single entry for bus 0-0.
	 */
	if (pci_mmcfg_config_num == 1 &&
	    cfg->pci_segment == 0 &&
	    (cfg->start_bus_number | cfg->end_bus_number) == 0) {
		printk(KERN_ERR "PCI: start and end of bus number is 0. "
		       "Rejected as broken MCFG.\n");
		goto reject;
	}

	for (i = 0; i < pci_mmcfg_config_num; i++) {
		u32 size = (cfg->end_bus_number + 1) << 20;
		cfg = &pci_mmcfg_config[i];
		printk(KERN_NOTICE "PCI: MCFG configuration %d: base %lu "
		       "segment %hu buses %u - %u\n",
		       i, (unsigned long)cfg->address, cfg->pci_segment,
		       (unsigned int)cfg->start_bus_number,
		       (unsigned int)cfg->end_bus_number);
		if (is_acpi_reserved(cfg->address, cfg->address + size - 1)) {
			printk(KERN_NOTICE "PCI: MCFG area at %Lx reserved "
			       "in ACPI motherboard resources\n",
			       cfg->address);
		} else {
			printk(KERN_ERR "PCI: BIOS Bug: MCFG area at %Lx is not"
			       " reserved in ACPI motherboard resources\n",
			       cfg->address);
			/* Don't try to do this check unless configuration
			   type 1 is available. */
			if ((pci_probe & PCI_PROBE_CONF1) &&
			    e820_all_mapped(cfg->address,
					    cfg->address + size - 1,
					    E820_RESERVED))
				printk(KERN_NOTICE
				       "PCI: MCFG area at %Lx reserved in "
				       "E820\n",
				       cfg->address);
			else
				goto reject;
		}
	}

	return;

reject:
	printk(KERN_ERR "PCI: Not using MMCONFIG.\n");
	pci_mmcfg_arch_free();
	kfree(pci_mmcfg_config);
	pci_mmcfg_config = NULL;
	pci_mmcfg_config_num = 0;
}

void __init pci_mmcfg_early_init(int type)
{
	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		return;

	/* If type 1 access is available, no need to enable MMCONFIG yet, we can
	   defer until later when the ACPI interpreter is available to better
	   validate things. */
	if (type == 1)
		return;

	acpi_table_parse(ACPI_SIG_MCFG, acpi_parse_mcfg);

	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].address == 0))
		return;

	if (pci_mmcfg_arch_init())
		pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;
}

void __init pci_mmcfg_late_init(void)
{
	int known_bridge = 0;

	/* MMCONFIG disabled */
	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		return;

	/* MMCONFIG already enabled */
	if (!(pci_probe & PCI_PROBE_MASK & ~PCI_PROBE_MMCONF))
		return;

	if ((pci_probe & PCI_PROBE_CONF1) && pci_mmcfg_check_hostbridge())
		known_bridge = 1;
	else
		acpi_table_parse(ACPI_SIG_MCFG, acpi_parse_mcfg);

	pci_mmcfg_reject_broken();

	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].address == 0))
		return;

	if (pci_mmcfg_arch_init()) {
		if (known_bridge)
			pci_mmcfg_insert_resources(IORESOURCE_BUSY);
		pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;
	} else {
		/*
		 * Signal not to attempt to insert mmcfg resources because
		 * the architecture mmcfg setup could not initialize.
		 */
		pci_mmcfg_resources_inserted = 1;
	}
}

static int __init pci_mmcfg_late_insert_resources(void)
{
	/*
	 * If resources are already inserted or we are not using MMCONFIG,
	 * don't insert the resources.
	 */
	if ((pci_mmcfg_resources_inserted == 1) ||
	    (pci_probe & PCI_PROBE_MMCONF) == 0 ||
	    (pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].address == 0))
		return 1;

	/*
	 * Attempt to insert the mmcfg resources but not with the busy flag
	 * marked so it won't cause request errors when __request_region is
	 * called.
	 */
	pci_mmcfg_insert_resources(0);

	return 0;
}

/*
 * Perform MMCONFIG resource insertion after PCI initialization to allow for
 * misprogrammed MCFG tables that state larger sizes but actually conflict
 * with other system resources.
 */
late_initcall(pci_mmcfg_late_insert_resources);
