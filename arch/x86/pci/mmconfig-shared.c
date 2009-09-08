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
#include <linux/sort.h>
#include <asm/e820.h>
#include <asm/pci_x86.h>

/* aperture is up to 256MB but BIOS may reserve less */
#define MMCONFIG_APER_MIN	(2 * 1024*1024)
#define MMCONFIG_APER_MAX	(256 * 1024*1024)

/* Indicate if the mmcfg resources have been placed into the resource table. */
static int __initdata pci_mmcfg_resources_inserted;

static __init int extend_mmcfg(int num)
{
	struct acpi_mcfg_allocation *new;
	int new_num = pci_mmcfg_config_num + num;

	new = kzalloc(sizeof(pci_mmcfg_config[0]) * new_num, GFP_KERNEL);
	if (!new)
		return -1;

	if (pci_mmcfg_config) {
		memcpy(new, pci_mmcfg_config,
			 sizeof(pci_mmcfg_config[0]) * new_num);
		kfree(pci_mmcfg_config);
	}
	pci_mmcfg_config = new;

	return 0;
}

static __init void fill_one_mmcfg(u64 addr, int segment, int start, int end)
{
	int i = pci_mmcfg_config_num;

	pci_mmcfg_config_num++;
	pci_mmcfg_config[i].address = addr;
	pci_mmcfg_config[i].pci_segment = segment;
	pci_mmcfg_config[i].start_bus_number = start;
	pci_mmcfg_config[i].end_bus_number = end;
}

static const char __init *pci_mmcfg_e7520(void)
{
	u32 win;
	raw_pci_ops->read(0, 0, PCI_DEVFN(0, 0), 0xce, 2, &win);

	win = win & 0xf000;
	if (win == 0x0000 || win == 0xf000)
		return NULL;

	if (extend_mmcfg(1) == -1)
		return NULL;

	fill_one_mmcfg(win << 16, 0, 0, 255);

	return "Intel Corporation E7520 Memory Controller Hub";
}

static const char __init *pci_mmcfg_intel_945(void)
{
	u32 pciexbar, mask = 0, len = 0;

	raw_pci_ops->read(0, 0, PCI_DEVFN(0, 0), 0x48, 4, &pciexbar);

	/* Enable bit */
	if (!(pciexbar & 1))
		return NULL;

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
		return NULL;
	}

	/* Errata #2, things break when not aligned on a 256Mb boundary */
	/* Can only happen in 64M/128M mode */

	if ((pciexbar & mask) & 0x0fffffffU)
		return NULL;

	/* Don't hit the APIC registers and their friends */
	if ((pciexbar & mask) >= 0xf0000000U)
		return NULL;

	if (extend_mmcfg(1) == -1)
		return NULL;

	fill_one_mmcfg(pciexbar & mask, 0, 0, (len >> 20) - 1);

	return "Intel Corporation 945G/GZ/P/PL Express Memory Controller Hub";
}

static const char __init *pci_mmcfg_amd_fam10h(void)
{
	u32 low, high, address;
	u64 base, msr;
	int i;
	unsigned segnbits = 0, busnbits;

	if (!(pci_probe & PCI_CHECK_ENABLE_AMD_MMCONF))
		return NULL;

	address = MSR_FAM10H_MMIO_CONF_BASE;
	if (rdmsr_safe(address, &low, &high))
		return NULL;

	msr = high;
	msr <<= 32;
	msr |= low;

	/* mmconfig is not enable */
	if (!(msr & FAM10H_MMIO_CONF_ENABLE))
		return NULL;

	base = msr & (FAM10H_MMIO_CONF_BASE_MASK<<FAM10H_MMIO_CONF_BASE_SHIFT);

	busnbits = (msr >> FAM10H_MMIO_CONF_BUSRANGE_SHIFT) &
			 FAM10H_MMIO_CONF_BUSRANGE_MASK;

	/*
	 * only handle bus 0 ?
	 * need to skip it
	 */
	if (!busnbits)
		return NULL;

	if (busnbits > 8) {
		segnbits = busnbits - 8;
		busnbits = 8;
	}

	if (extend_mmcfg(1 << segnbits) == -1)
		return NULL;

	for (i = 0; i < (1 << segnbits); i++)
		fill_one_mmcfg(base + (1<<28) * i, i, 0, (1 << busnbits) - 1);

	return "AMD Family 10h NB";
}

static bool __initdata mcp55_checked;
static const char __init *pci_mmcfg_nvidia_mcp55(void)
{
	int bus;
	int mcp55_mmconf_found = 0;

	static const u32 extcfg_regnum		= 0x90;
	static const u32 extcfg_regsize		= 4;
	static const u32 extcfg_enable_mask	= 1<<31;
	static const u32 extcfg_start_mask	= 0xff<<16;
	static const int extcfg_start_shift	= 16;
	static const u32 extcfg_size_mask	= 0x3<<28;
	static const int extcfg_size_shift	= 28;
	static const int extcfg_sizebus[]	= {0x100, 0x80, 0x40, 0x20};
	static const u32 extcfg_base_mask[]	= {0x7ff8, 0x7ffc, 0x7ffe, 0x7fff};
	static const int extcfg_base_lshift	= 25;

	/*
	 * do check if amd fam10h already took over
	 */
	if (!acpi_disabled || pci_mmcfg_config_num || mcp55_checked)
		return NULL;

	mcp55_checked = true;
	for (bus = 0; bus < 256; bus++) {
		u64 base;
		u32 l, extcfg;
		u16 vendor, device;
		int start, size_index, end;

		raw_pci_ops->read(0, bus, PCI_DEVFN(0, 0), 0, 4, &l);
		vendor = l & 0xffff;
		device = (l >> 16) & 0xffff;

		if (PCI_VENDOR_ID_NVIDIA != vendor || 0x0369 != device)
			continue;

		raw_pci_ops->read(0, bus, PCI_DEVFN(0, 0), extcfg_regnum,
				  extcfg_regsize, &extcfg);

		if (!(extcfg & extcfg_enable_mask))
			continue;

		if (extend_mmcfg(1) == -1)
			continue;

		size_index = (extcfg & extcfg_size_mask) >> extcfg_size_shift;
		base = extcfg & extcfg_base_mask[size_index];
		/* base could > 4G */
		base <<= extcfg_base_lshift;
		start = (extcfg & extcfg_start_mask) >> extcfg_start_shift;
		end = start + extcfg_sizebus[size_index] - 1;
		fill_one_mmcfg(base, 0, start, end);
		mcp55_mmconf_found++;
	}

	if (!mcp55_mmconf_found)
		return NULL;

	return "nVidia MCP55";
}

struct pci_mmcfg_hostbridge_probe {
	u32 bus;
	u32 devfn;
	u32 vendor;
	u32 device;
	const char *(*probe)(void);
};

static struct pci_mmcfg_hostbridge_probe pci_mmcfg_probes[] __initdata = {
	{ 0, PCI_DEVFN(0, 0), PCI_VENDOR_ID_INTEL,
	  PCI_DEVICE_ID_INTEL_E7520_MCH, pci_mmcfg_e7520 },
	{ 0, PCI_DEVFN(0, 0), PCI_VENDOR_ID_INTEL,
	  PCI_DEVICE_ID_INTEL_82945G_HB, pci_mmcfg_intel_945 },
	{ 0, PCI_DEVFN(0x18, 0), PCI_VENDOR_ID_AMD,
	  0x1200, pci_mmcfg_amd_fam10h },
	{ 0xff, PCI_DEVFN(0, 0), PCI_VENDOR_ID_AMD,
	  0x1200, pci_mmcfg_amd_fam10h },
	{ 0, PCI_DEVFN(0, 0), PCI_VENDOR_ID_NVIDIA,
	  0x0369, pci_mmcfg_nvidia_mcp55 },
};

static int __init cmp_mmcfg(const void *x1, const void *x2)
{
	const typeof(pci_mmcfg_config[0]) *m1 = x1;
	const typeof(pci_mmcfg_config[0]) *m2 = x2;
	int start1, start2;

	start1 = m1->start_bus_number;
	start2 = m2->start_bus_number;

	return start1 - start2;
}

static void __init pci_mmcfg_check_end_bus_number(void)
{
	int i;
	typeof(pci_mmcfg_config[0]) *cfg, *cfgx;

	/* sort them at first */
	sort(pci_mmcfg_config, pci_mmcfg_config_num,
		 sizeof(pci_mmcfg_config[0]), cmp_mmcfg, NULL);

	/* last one*/
	if (pci_mmcfg_config_num > 0) {
		i = pci_mmcfg_config_num - 1;
		cfg = &pci_mmcfg_config[i];
		if (cfg->end_bus_number < cfg->start_bus_number)
			cfg->end_bus_number = 255;
	}

	/* don't overlap please */
	for (i = 0; i < pci_mmcfg_config_num - 1; i++) {
		cfg = &pci_mmcfg_config[i];
		cfgx = &pci_mmcfg_config[i+1];

		if (cfg->end_bus_number < cfg->start_bus_number)
			cfg->end_bus_number = 255;

		if (cfg->end_bus_number >= cfgx->start_bus_number)
			cfg->end_bus_number = cfgx->start_bus_number - 1;
	}
}

static int __init pci_mmcfg_check_hostbridge(void)
{
	u32 l;
	u32 bus, devfn;
	u16 vendor, device;
	int i;
	const char *name;

	if (!raw_pci_ops)
		return 0;

	pci_mmcfg_config_num = 0;
	pci_mmcfg_config = NULL;

	for (i = 0; i < ARRAY_SIZE(pci_mmcfg_probes); i++) {
		bus =  pci_mmcfg_probes[i].bus;
		devfn = pci_mmcfg_probes[i].devfn;
		raw_pci_ops->read(0, bus, devfn, 0, 4, &l);
		vendor = l & 0xffff;
		device = (l >> 16) & 0xffff;

		name = NULL;
		if (pci_mmcfg_probes[i].vendor == vendor &&
		    pci_mmcfg_probes[i].device == device)
			name = pci_mmcfg_probes[i].probe();

		if (name)
			printk(KERN_INFO "PCI: Found %s with MMCONFIG support.\n",
			       name);
	}

	/* some end_bus_number is crazy, fix it */
	pci_mmcfg_check_end_bus_number();

	return pci_mmcfg_config_num != 0;
}

static void __init pci_mmcfg_insert_resources(void)
{
#define PCI_MMCFG_RESOURCE_NAME_LEN 24
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
		snprintf(names, PCI_MMCFG_RESOURCE_NAME_LEN,
			 "PCI MMCONFIG %u [%02x-%02x]", cfg->pci_segment,
			 cfg->start_bus_number, cfg->end_bus_number);
		res->start = cfg->address + (cfg->start_bus_number << 20);
		res->end = res->start + (num_buses << 20) - 1;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
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

static int __init is_acpi_reserved(u64 start, u64 end, unsigned not_used)
{
	struct resource mcfg_res;

	mcfg_res.start = start;
	mcfg_res.end = end - 1;
	mcfg_res.flags = 0;

	acpi_get_devices("PNP0C01", find_mboard_resource, &mcfg_res, NULL);

	if (!mcfg_res.flags)
		acpi_get_devices("PNP0C02", find_mboard_resource, &mcfg_res,
				 NULL);

	return mcfg_res.flags;
}

typedef int (*check_reserved_t)(u64 start, u64 end, unsigned type);

static int __init is_mmconf_reserved(check_reserved_t is_reserved,
		u64 addr, u64 size, int i,
		typeof(pci_mmcfg_config[0]) *cfg, int with_e820)
{
	u64 old_size = size;
	int valid = 0;

	while (!is_reserved(addr, addr + size, E820_RESERVED)) {
		size >>= 1;
		if (size < (16UL<<20))
			break;
	}

	if (size >= (16UL<<20) || size == old_size) {
		printk(KERN_NOTICE
		       "PCI: MCFG area at %Lx reserved in %s\n",
			addr, with_e820?"E820":"ACPI motherboard resources");
		valid = 1;

		if (old_size != size) {
			/* update end_bus_number */
			cfg->end_bus_number = cfg->start_bus_number + ((size>>20) - 1);
			printk(KERN_NOTICE "PCI: updated MCFG configuration %d: base %lx "
			       "segment %hu buses %u - %u\n",
			       i, (unsigned long)cfg->address, cfg->pci_segment,
			       (unsigned int)cfg->start_bus_number,
			       (unsigned int)cfg->end_bus_number);
		}
	}

	return valid;
}

static void __init pci_mmcfg_reject_broken(int early)
{
	typeof(pci_mmcfg_config[0]) *cfg;
	int i;

	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].address == 0))
		return;

	for (i = 0; i < pci_mmcfg_config_num; i++) {
		int valid = 0;
		u64 addr, size;

		cfg = &pci_mmcfg_config[i];
		addr = cfg->start_bus_number;
		addr <<= 20;
		addr += cfg->address;
		size = cfg->end_bus_number + 1 - cfg->start_bus_number;
		size <<= 20;
		printk(KERN_NOTICE "PCI: MCFG configuration %d: base %lx "
		       "segment %hu buses %u - %u\n",
		       i, (unsigned long)cfg->address, cfg->pci_segment,
		       (unsigned int)cfg->start_bus_number,
		       (unsigned int)cfg->end_bus_number);

		if (!early)
			valid = is_mmconf_reserved(is_acpi_reserved, addr, size, i, cfg, 0);

		if (valid)
			continue;

		if (!early)
			printk(KERN_ERR "PCI: BIOS Bug: MCFG area at %Lx is not"
			       " reserved in ACPI motherboard resources\n",
			       cfg->address);

		/* Don't try to do this check unless configuration
		   type 1 is available. how about type 2 ?*/
		if (raw_pci_ops)
			valid = is_mmconf_reserved(e820_all_mapped, addr, size, i, cfg, 1);

		if (!valid)
			goto reject;
	}

	return;

reject:
	printk(KERN_INFO "PCI: Not using MMCONFIG.\n");
	pci_mmcfg_arch_free();
	kfree(pci_mmcfg_config);
	pci_mmcfg_config = NULL;
	pci_mmcfg_config_num = 0;
}

static int __initdata known_bridge;

static int acpi_mcfg_64bit_base_addr __initdata = FALSE;

/* The physical address of the MMCONFIG aperture.  Set from ACPI tables. */
struct acpi_mcfg_allocation *pci_mmcfg_config;
int pci_mmcfg_config_num;

static int __init acpi_mcfg_oem_check(struct acpi_table_mcfg *mcfg)
{
	if (!strcmp(mcfg->header.oem_id, "SGI"))
		acpi_mcfg_64bit_base_addr = TRUE;

	return 0;
}

static int __init pci_parse_mcfg(struct acpi_table_header *header)
{
	struct acpi_table_mcfg *mcfg;
	unsigned long i;
	int config_size;

	if (!header)
		return -EINVAL;

	mcfg = (struct acpi_table_mcfg *)header;

	/* how many config structures do we have */
	pci_mmcfg_config_num = 0;
	i = header->length - sizeof(struct acpi_table_mcfg);
	while (i >= sizeof(struct acpi_mcfg_allocation)) {
		++pci_mmcfg_config_num;
		i -= sizeof(struct acpi_mcfg_allocation);
	};
	if (pci_mmcfg_config_num == 0) {
		printk(KERN_ERR PREFIX "MMCONFIG has no entries\n");
		return -ENODEV;
	}

	config_size = pci_mmcfg_config_num * sizeof(*pci_mmcfg_config);
	pci_mmcfg_config = kmalloc(config_size, GFP_KERNEL);
	if (!pci_mmcfg_config) {
		printk(KERN_WARNING PREFIX
		       "No memory for MCFG config tables\n");
		return -ENOMEM;
	}

	memcpy(pci_mmcfg_config, &mcfg[1], config_size);

	acpi_mcfg_oem_check(mcfg);

	for (i = 0; i < pci_mmcfg_config_num; ++i) {
		if ((pci_mmcfg_config[i].address > 0xFFFFFFFF) &&
		    !acpi_mcfg_64bit_base_addr) {
			printk(KERN_ERR PREFIX
			       "MMCONFIG not in low 4GB of memory\n");
			kfree(pci_mmcfg_config);
			pci_mmcfg_config_num = 0;
			return -ENODEV;
		}
	}

	return 0;
}

static void __init __pci_mmcfg_init(int early)
{
	/* MMCONFIG disabled */
	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		return;

	/* MMCONFIG already enabled */
	if (!early && !(pci_probe & PCI_PROBE_MASK & ~PCI_PROBE_MMCONF))
		return;

	/* for late to exit */
	if (known_bridge)
		return;

	if (early) {
		if (pci_mmcfg_check_hostbridge())
			known_bridge = 1;
	}

	if (!known_bridge)
		acpi_table_parse(ACPI_SIG_MCFG, pci_parse_mcfg);

	pci_mmcfg_reject_broken(early);

	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].address == 0))
		return;

	if (pci_mmcfg_arch_init())
		pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;
	else {
		/*
		 * Signal not to attempt to insert mmcfg resources because
		 * the architecture mmcfg setup could not initialize.
		 */
		pci_mmcfg_resources_inserted = 1;
	}
}

void __init pci_mmcfg_early_init(void)
{
	__pci_mmcfg_init(1);
}

void __init pci_mmcfg_late_init(void)
{
	__pci_mmcfg_init(0);
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
	pci_mmcfg_insert_resources();

	return 0;
}

/*
 * Perform MMCONFIG resource insertion after PCI initialization to allow for
 * misprogrammed MCFG tables that state larger sizes but actually conflict
 * with other system resources.
 */
late_initcall(pci_mmcfg_late_insert_resources);
