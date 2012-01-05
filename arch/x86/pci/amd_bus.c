#include <linux/init.h>
#include <linux/pci.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/range.h>

#include <asm/amd_nb.h>
#include <asm/pci_x86.h>

#include <asm/pci-direct.h>

#include "bus_numa.h"

/*
 * This discovers the pcibus <-> node mapping on AMD K8.
 * also get peer root bus resource for io,mmio
 */

struct pci_hostbridge_probe {
	u32 bus;
	u32 slot;
	u32 vendor;
	u32 device;
};

static struct pci_hostbridge_probe pci_probes[] __initdata = {
	{ 0, 0x18, PCI_VENDOR_ID_AMD, 0x1100 },
	{ 0, 0x18, PCI_VENDOR_ID_AMD, 0x1200 },
	{ 0xff, 0, PCI_VENDOR_ID_AMD, 0x1200 },
	{ 0, 0x18, PCI_VENDOR_ID_AMD, 0x1300 },
};

#define RANGE_NUM 16

/**
 * early_fill_mp_bus_to_node()
 * called before pcibios_scan_root and pci_scan_bus
 * fills the mp_bus_to_cpumask array based according to the LDT Bus Number
 * Registers found in the K8 northbridge
 */
static int __init early_fill_mp_bus_info(void)
{
	int i;
	int j;
	unsigned bus;
	unsigned slot;
	int node;
	int link;
	int def_node;
	int def_link;
	struct pci_root_info *info;
	u32 reg;
	struct resource *res;
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
	for (i = 0; i < ARRAY_SIZE(pci_probes); i++) {
		u32 id;
		u16 device;
		u16 vendor;

		bus = pci_probes[i].bus;
		slot = pci_probes[i].slot;
		id = read_pci_config(bus, slot, 0, PCI_VENDOR_ID);

		vendor = id & 0xffff;
		device = (id>>16) & 0xffff;
		if (pci_probes[i].vendor == vendor &&
		    pci_probes[i].device == device) {
			found = true;
			break;
		}
	}

	if (!found)
		return 0;

	pci_root_num = 0;
	for (i = 0; i < 4; i++) {
		int min_bus;
		int max_bus;
		reg = read_pci_config(bus, slot, 1, 0xe0 + (i << 2));

		/* Check if that register is enabled for bus range */
		if ((reg & 7) != 3)
			continue;

		min_bus = (reg >> 16) & 0xff;
		max_bus = (reg >> 24) & 0xff;
		node = (reg >> 4) & 0x07;
#ifdef CONFIG_NUMA
		for (j = min_bus; j <= max_bus; j++)
			set_mp_bus_to_node(j, node);
#endif
		link = (reg >> 8) & 0x03;

		info = &pci_root_info[pci_root_num];
		info->bus_min = min_bus;
		info->bus_max = max_bus;
		info->node = node;
		info->link = link;
		sprintf(info->name, "PCI Bus #%02x", min_bus);
		pci_root_num++;
	}

	/* get the default node and link for left over res */
	reg = read_pci_config(bus, slot, 0, 0x60);
	def_node = (reg >> 8) & 0x07;
	reg = read_pci_config(bus, slot, 0, 0x64);
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

		/* find the position */
		for (j = 0; j < pci_root_num; j++) {
			info = &pci_root_info[j];
			if (info->node == node && info->link == link)
				break;
		}
		if (j == pci_root_num)
			continue; /* not found */

		info = &pci_root_info[j];
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
	for (j = 0; j < pci_root_num; j++) {
		info = &pci_root_info[j];
		if (info->node == def_node && info->link == def_link)
			break;
	}
	if (j < pci_root_num) {
		info = &pci_root_info[j];
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

		/* find the position */
		for (j = 0; j < pci_root_num; j++) {
			info = &pci_root_info[j];
			if (info->node == node && info->link == link)
				break;
		}
		if (j == pci_root_num)
			continue; /* not found */

		info = &pci_root_info[j];

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
	for (j = 0; j < pci_root_num; j++) {
		info = &pci_root_info[j];
		if (info->node == def_node && info->link == def_link)
			break;
	}
	if (j < pci_root_num) {
		info = &pci_root_info[j];

		for (i = 0; i < RANGE_NUM; i++) {
			if (!range[i].end)
				continue;

			update_res(info, cap_resource(range[i].start),
				   cap_resource(range[i].end - 1),
				   IORESOURCE_MEM, 1);
		}
	}

	for (i = 0; i < pci_root_num; i++) {
		int res_num;
		int busnum;

		info = &pci_root_info[i];
		res_num = info->res_num;
		busnum = info->bus_min;
		printk(KERN_DEBUG "bus: [%02x, %02x] on node %x link %x\n",
		       info->bus_min, info->bus_max, info->node, info->link);
		for (j = 0; j < res_num; j++) {
			res = &info->res[j];
			printk(KERN_DEBUG "bus: %02x index %x %pR\n",
				       busnum, j, res);
		}
	}

	return 0;
}

#define ENABLE_CF8_EXT_CFG      (1ULL << 46)

static void __cpuinit enable_pci_io_ecs(void *unused)
{
	u64 reg;
	rdmsrl(MSR_AMD64_NB_CFG, reg);
	if (!(reg & ENABLE_CF8_EXT_CFG)) {
		reg |= ENABLE_CF8_EXT_CFG;
		wrmsrl(MSR_AMD64_NB_CFG, reg);
	}
}

static int __cpuinit amd_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	int cpu = (long)hcpu;
	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		smp_call_function_single(cpu, enable_pci_io_ecs, NULL, 0);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata amd_cpu_notifier = {
	.notifier_call	= amd_cpu_notify,
};

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
	pr_info("Extended Config Space enabled on %u nodes\n", n);
#endif
}

static int __init pci_io_ecs_init(void)
{
	int cpu;

	/* assume all cpus from fam10h have IO ECS */
        if (boot_cpu_data.x86 < 0x10)
		return 0;

	/* Try the PCI method first. */
	if (early_pci_allowed())
		pci_enable_pci_io_ecs();

	register_cpu_notifier(&amd_cpu_notifier);
	for_each_online_cpu(cpu)
		amd_cpu_notify(&amd_cpu_notifier, (unsigned long)CPU_ONLINE,
			       (void *)(long)cpu);
	pci_probe |= PCI_HAS_IO_ECS;

	return 0;
}

static int __init amd_postcore_init(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return 0;

	early_fill_mp_bus_info();
	pci_io_ecs_init();

	return 0;
}

postcore_initcall(amd_postcore_init);
