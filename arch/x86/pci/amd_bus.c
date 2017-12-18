// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/range.h>

#include <asm/amd_nb.h>
#include <asm/pci_x86.h>

#include <asm/pci-direct.h>

#include "bus_numa.h"

#define AMD_NB_F0_NODE_ID			0x60
#define AMD_NB_F0_UNIT_ID			0x64
#define AMD_NB_F1_CONFIG_MAP_REG		0xe0

#define RANGE_NUM				16
#define AMD_NB_F1_CONFIG_MAP_RANGES		4

struct amd_hostbridge {
	u32 bus;
	u32 slot;
	u32 device;
};

/*
 * IMPORTANT NOTE:
 * hb_probes[] and early_root_info_init() is in maintenance mode.
 * It only supports K8, Fam10h, Fam11h, and Fam15h_00h-0fh .
 * Future processor will rely on information in ACPI.
 */
static struct amd_hostbridge hb_probes[] __initdata = {
	{ 0, 0x18, 0x1100 }, /* K8 */
	{ 0, 0x18, 0x1200 }, /* Family10h */
	{ 0xff, 0, 0x1200 }, /* Family10h */
	{ 0, 0x18, 0x1300 }, /* Family11h */
	{ 0, 0x18, 0x1600 }, /* Family15h */
};

static struct pci_root_info __init *find_pci_root_info(int node, int link)
{
	struct pci_root_info *info;

	/* find the position */
	list_for_each_entry(info, &pci_root_infos, list)
		if (info->node == node && info->link == link)
			return info;

	return NULL;
}

/**
 * early_root_info_init()
 * called before pcibios_scan_root and pci_scan_bus
 * fills the mp_bus_to_cpumask array based according
 * to the LDT Bus Number Registers found in the northbridge.
 */
static int __init early_root_info_init(void)
{
	int i;
	unsigned bus;
	unsigned slot;
	int node;
	int link;
	int def_node;
	int def_link;
	struct pci_root_info *info;
	u32 reg;
	u64 start;
	u64 end;
	struct range range[RANGE_NUM];
	u64 val;
	u32 address;
	bool found;
	struct resource fam10h_mmconf_res, *fam10h_mmconf;
	u64 fam10h_mmconf_start;
	u64 fam10h_mmconf_end;

	if (!early_pci_allowed())
		return -1;

	found = false;
	for (i = 0; i < ARRAY_SIZE(hb_probes); i++) {
		u32 id;
		u16 device;
		u16 vendor;

		bus = hb_probes[i].bus;
		slot = hb_probes[i].slot;
		id = read_pci_config(bus, slot, 0, PCI_VENDOR_ID);
		vendor = id & 0xffff;
		device = (id>>16) & 0xffff;

		if (vendor != PCI_VENDOR_ID_AMD)
			continue;

		if (hb_probes[i].device == device) {
			found = true;
			break;
		}
	}

	if (!found)
		return 0;

	/*
	 * We should learn topology and routing information from _PXM and
	 * _CRS methods in the ACPI namespace.  We extract node numbers
	 * here to work around BIOSes that don't supply _PXM.
	 */
	for (i = 0; i < AMD_NB_F1_CONFIG_MAP_RANGES; i++) {
		int min_bus;
		int max_bus;
		reg = read_pci_config(bus, slot, 1,
				AMD_NB_F1_CONFIG_MAP_REG + (i << 2));

		/* Check if that register is enabled for bus range */
		if ((reg & 7) != 3)
			continue;

		min_bus = (reg >> 16) & 0xff;
		max_bus = (reg >> 24) & 0xff;
		node = (reg >> 4) & 0x07;
		link = (reg >> 8) & 0x03;

		info = alloc_pci_root_info(min_bus, max_bus, node, link);
	}

	/*
	 * The following code extracts routing information for use on old
	 * systems where Linux doesn't automatically use host bridge _CRS
	 * methods (or when the user specifies "pci=nocrs").
	 *
	 * We only do this through Fam11h, because _CRS should be enough on
	 * newer systems.
	 */
	if (boot_cpu_data.x86 > 0x11)
		return 0;

	/* get the default node and link for left over res */
	reg = read_pci_config(bus, slot, 0, AMD_NB_F0_NODE_ID);
	def_node = (reg >> 8) & 0x07;
	reg = read_pci_config(bus, slot, 0, AMD_NB_F0_UNIT_ID);
	def_link = (reg >> 8) & 0x03;

	memset(range, 0, sizeof(range));
	add_range(range, RANGE_NUM, 0, 0, 0xffff + 1);
	/* io port resource */
	for (i = 0; i < 4; i++) {
		reg = read_pci_config(bus, slot, 1, 0xc0 + (i << 3));
		if (!(reg & 3))
			continue;

		start = reg & 0xfff000;
		reg = read_pci_config(bus, slot, 1, 0xc4 + (i << 3));
		node = reg & 0x07;
		link = (reg >> 4) & 0x03;
		end = (reg & 0xfff000) | 0xfff;

		info = find_pci_root_info(node, link);
		if (!info)
			continue; /* not found */

		printk(KERN_DEBUG "node %d link %d: io port [%llx, %llx]\n",
		       node, link, start, end);

		/* kernel only handle 16 bit only */
		if (end > 0xffff)
			end = 0xffff;
		update_res(info, start, end, IORESOURCE_IO, 1);
		subtract_range(range, RANGE_NUM, start, end + 1);
	}
	/* add left over io port range to def node/link, [0, 0xffff] */
	/* find the position */
	info = find_pci_root_info(def_node, def_link);
	if (info) {
		for (i = 0; i < RANGE_NUM; i++) {
			if (!range[i].end)
				continue;

			update_res(info, range[i].start, range[i].end - 1,
				   IORESOURCE_IO, 1);
		}
	}

	memset(range, 0, sizeof(range));
	/* 0xfd00000000-0xffffffffff for HT */
	end = cap_resource((0xfdULL<<32) - 1);
	end++;
	add_range(range, RANGE_NUM, 0, 0, end);

	/* need to take out [0, TOM) for RAM*/
	address = MSR_K8_TOP_MEM1;
	rdmsrl(address, val);
	end = (val & 0xffffff800000ULL);
	printk(KERN_INFO "TOM: %016llx aka %lldM\n", end, end>>20);
	if (end < (1ULL<<32))
		subtract_range(range, RANGE_NUM, 0, end);

	/* get mmconfig */
	fam10h_mmconf = amd_get_mmconfig_range(&fam10h_mmconf_res);
	/* need to take out mmconf range */
	if (fam10h_mmconf) {
		printk(KERN_DEBUG "Fam 10h mmconf %pR\n", fam10h_mmconf);
		fam10h_mmconf_start = fam10h_mmconf->start;
		fam10h_mmconf_end = fam10h_mmconf->end;
		subtract_range(range, RANGE_NUM, fam10h_mmconf_start,
				 fam10h_mmconf_end + 1);
	} else {
		fam10h_mmconf_start = 0;
		fam10h_mmconf_end = 0;
	}

	/* mmio resource */
	for (i = 0; i < 8; i++) {
		reg = read_pci_config(bus, slot, 1, 0x80 + (i << 3));
		if (!(reg & 3))
			continue;

		start = reg & 0xffffff00; /* 39:16 on 31:8*/
		start <<= 8;
		reg = read_pci_config(bus, slot, 1, 0x84 + (i << 3));
		node = reg & 0x07;
		link = (reg >> 4) & 0x03;
		end = (reg & 0xffffff00);
		end <<= 8;
		end |= 0xffff;

		info = find_pci_root_info(node, link);

		if (!info)
			continue;

		printk(KERN_DEBUG "node %d link %d: mmio [%llx, %llx]",
		       node, link, start, end);
		/*
		 * some sick allocation would have range overlap with fam10h
		 * mmconf range, so need to update start and end.
		 */
		if (fam10h_mmconf_end) {
			int changed = 0;
			u64 endx = 0;
			if (start >= fam10h_mmconf_start &&
			    start <= fam10h_mmconf_end) {
				start = fam10h_mmconf_end + 1;
				changed = 1;
			}

			if (end >= fam10h_mmconf_start &&
			    end <= fam10h_mmconf_end) {
				end = fam10h_mmconf_start - 1;
				changed = 1;
			}

			if (start < fam10h_mmconf_start &&
			    end > fam10h_mmconf_end) {
				/* we got a hole */
				endx = fam10h_mmconf_start - 1;
				update_res(info, start, endx, IORESOURCE_MEM, 0);
				subtract_range(range, RANGE_NUM, start,
						 endx + 1);
				printk(KERN_CONT " ==> [%llx, %llx]", start, endx);
				start = fam10h_mmconf_end + 1;
				changed = 1;
			}
			if (changed) {
				if (start <= end) {
					printk(KERN_CONT " %s [%llx, %llx]", endx ? "and" : "==>", start, end);
				} else {
					printk(KERN_CONT "%s\n", endx?"":" ==> none");
					continue;
				}
			}
		}

		update_res(info, cap_resource(start), cap_resource(end),
				 IORESOURCE_MEM, 1);
		subtract_range(range, RANGE_NUM, start, end + 1);
		printk(KERN_CONT "\n");
	}

	/* need to take out [4G, TOM2) for RAM*/
	/* SYS_CFG */
	address = MSR_K8_SYSCFG;
	rdmsrl(address, val);
	/* TOP_MEM2 is enabled? */
	if (val & (1<<21)) {
		/* TOP_MEM2 */
		address = MSR_K8_TOP_MEM2;
		rdmsrl(address, val);
		end = (val & 0xffffff800000ULL);
		printk(KERN_INFO "TOM2: %016llx aka %lldM\n", end, end>>20);
		subtract_range(range, RANGE_NUM, 1ULL<<32, end);
	}

	/*
	 * add left over mmio range to def node/link ?
	 * that is tricky, just record range in from start_min to 4G
	 */
	info = find_pci_root_info(def_node, def_link);
	if (info) {
		for (i = 0; i < RANGE_NUM; i++) {
			if (!range[i].end)
				continue;

			update_res(info, cap_resource(range[i].start),
				   cap_resource(range[i].end - 1),
				   IORESOURCE_MEM, 1);
		}
	}

	list_for_each_entry(info, &pci_root_infos, list) {
		int busnum;
		struct pci_root_res *root_res;

		busnum = info->busn.start;
		printk(KERN_DEBUG "bus: %pR on node %x link %x\n",
		       &info->busn, info->node, info->link);
		list_for_each_entry(root_res, &info->resources, list)
			printk(KERN_DEBUG "bus: %02x %pR\n",
				       busnum, &root_res->res);
	}

	return 0;
}

#define ENABLE_CF8_EXT_CFG      (1ULL << 46)

static int amd_bus_cpu_online(unsigned int cpu)
{
	u64 reg;

	rdmsrl(MSR_AMD64_NB_CFG, reg);
	if (!(reg & ENABLE_CF8_EXT_CFG)) {
		reg |= ENABLE_CF8_EXT_CFG;
		wrmsrl(MSR_AMD64_NB_CFG, reg);
	}
	return 0;
}

static void __init pci_enable_pci_io_ecs(void)
{
#ifdef CONFIG_AMD_NB
	unsigned int i, n;

	for (n = i = 0; !n && amd_nb_bus_dev_ranges[i].dev_limit; ++i) {
		u8 bus = amd_nb_bus_dev_ranges[i].bus;
		u8 slot = amd_nb_bus_dev_ranges[i].dev_base;
		u8 limit = amd_nb_bus_dev_ranges[i].dev_limit;

		for (; slot < limit; ++slot) {
			u32 val = read_pci_config(bus, slot, 3, 0);

			if (!early_is_amd_nb(val))
				continue;

			val = read_pci_config(bus, slot, 3, 0x8c);
			if (!(val & (ENABLE_CF8_EXT_CFG >> 32))) {
				val |= ENABLE_CF8_EXT_CFG >> 32;
				write_pci_config(bus, slot, 3, 0x8c, val);
			}
			++n;
		}
	}
#endif
}

static int __init pci_io_ecs_init(void)
{
	int ret;

	/* assume all cpus from fam10h have IO ECS */
	if (boot_cpu_data.x86 < 0x10)
		return 0;

	/* Try the PCI method first. */
	if (early_pci_allowed())
		pci_enable_pci_io_ecs();

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "pci/amd_bus:online",
				amd_bus_cpu_online, NULL);
	WARN_ON(ret < 0);

	pci_probe |= PCI_HAS_IO_ECS;

	return 0;
}

static int __init amd_postcore_init(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return 0;

	early_root_info_init();
	pci_io_ecs_init();

	return 0;
}

postcore_initcall(amd_postcore_init);
