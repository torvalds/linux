#include <linux/init.h>
#include <linux/pci.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <asm/pci_x86.h>

#ifdef CONFIG_X86_64
#include <asm/pci-direct.h>
#include <asm/mpspec.h>
#include <linux/cpumask.h>
#endif

/*
 * This discovers the pcibus <-> node mapping on AMD K8.
 * also get peer root bus resource for io,mmio
 */

#ifdef CONFIG_NUMA

#define BUS_NR 256

#ifdef CONFIG_X86_64

static int mp_bus_to_node[BUS_NR];

void set_mp_bus_to_node(int busnum, int node)
{
	if (busnum >= 0 &&  busnum < BUS_NR)
		mp_bus_to_node[busnum] = node;
}

int get_mp_bus_to_node(int busnum)
{
	int node = -1;

	if (busnum < 0 || busnum > (BUS_NR - 1))
		return node;

	node = mp_bus_to_node[busnum];

	/*
	 * let numa_node_id to decide it later in dma_alloc_pages
	 * if there is no ram on that node
	 */
	if (node != -1 && !node_online(node))
		node = -1;

	return node;
}

#else /* CONFIG_X86_32 */

static unsigned char mp_bus_to_node[BUS_NR];

void set_mp_bus_to_node(int busnum, int node)
{
	if (busnum >= 0 &&  busnum < BUS_NR)
	mp_bus_to_node[busnum] = (unsigned char) node;
}

int get_mp_bus_to_node(int busnum)
{
	int node;

	if (busnum < 0 || busnum > (BUS_NR - 1))
		return 0;
	node = mp_bus_to_node[busnum];
	return node;
}

#endif /* CONFIG_X86_32 */

#endif /* CONFIG_NUMA */

#ifdef CONFIG_X86_64

/*
 * sub bus (transparent) will use entres from 3 to store extra from root,
 * so need to make sure have enought slot there, increase PCI_BUS_NUM_RESOURCES?
 */
#define RES_NUM 16
struct pci_root_info {
	char name[12];
	unsigned int res_num;
	struct resource res[RES_NUM];
	int bus_min;
	int bus_max;
	int node;
	int link;
};

/* 4 at this time, it may become to 32 */
#define PCI_ROOT_NR 4
static int pci_root_num;
static struct pci_root_info pci_root_info[PCI_ROOT_NR];

void x86_pci_root_bus_res_quirks(struct pci_bus *b)
{
	int i;
	int j;
	struct pci_root_info *info;

	/* don't go for it if _CRS is used already */
	if (b->resource[0] != &ioport_resource ||
	    b->resource[1] != &iomem_resource)
		return;

	/* if only one root bus, don't need to anything */
	if (pci_root_num < 2)
		return;

	for (i = 0; i < pci_root_num; i++) {
		if (pci_root_info[i].bus_min == b->number)
			break;
	}

	if (i == pci_root_num)
		return;

	printk(KERN_DEBUG "PCI: peer root bus %02x res updated from pci conf\n",
			b->number);

	info = &pci_root_info[i];
	for (j = 0; j < info->res_num; j++) {
		struct resource *res;
		struct resource *root;

		res = &info->res[j];
		b->resource[j] = res;
		if (res->flags & IORESOURCE_IO)
			root = &ioport_resource;
		else
			root = &iomem_resource;
		insert_resource(root, res);
	}
}

#define RANGE_NUM 16

struct res_range {
	size_t start;
	size_t end;
};

static void __init update_range(struct res_range *range, size_t start,
				size_t end)
{
	int i;
	int j;

	for (j = 0; j < RANGE_NUM; j++) {
		if (!range[j].end)
			continue;

		if (start <= range[j].start && end >= range[j].end) {
			range[j].start = 0;
			range[j].end = 0;
			continue;
		}

		if (start <= range[j].start && end < range[j].end && range[j].start < end + 1) {
			range[j].start = end + 1;
			continue;
		}


		if (start > range[j].start && end >= range[j].end && range[j].end > start - 1) {
			range[j].end = start - 1;
			continue;
		}

		if (start > range[j].start && end < range[j].end) {
			/* find the new spare */
			for (i = 0; i < RANGE_NUM; i++) {
				if (range[i].end == 0)
					break;
			}
			if (i < RANGE_NUM) {
				range[i].end = range[j].end;
				range[i].start = end + 1;
			} else {
				printk(KERN_ERR "run of slot in ranges\n");
			}
			range[j].end = start - 1;
			continue;
		}
	}
}

static void __init update_res(struct pci_root_info *info, size_t start,
			      size_t end, unsigned long flags, int merge)
{
	int i;
	struct resource *res;

	if (!merge)
		goto addit;

	/* try to merge it with old one */
	for (i = 0; i < info->res_num; i++) {
		size_t final_start, final_end;
		size_t common_start, common_end;

		res = &info->res[i];
		if (res->flags != flags)
			continue;

		common_start = max((size_t)res->start, start);
		common_end = min((size_t)res->end, end);
		if (common_start > common_end + 1)
			continue;

		final_start = min((size_t)res->start, start);
		final_end = max((size_t)res->end, end);

		res->start = final_start;
		res->end = final_end;
		return;
	}

addit:

	/* need to add that */
	if (info->res_num >= RES_NUM)
		return;

	res = &info->res[info->res_num];
	res->name = info->name;
	res->flags = flags;
	res->start = start;
	res->end = end;
	res->child = NULL;
	info->res_num++;
}

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

static u64 __initdata fam10h_mmconf_start;
static u64 __initdata fam10h_mmconf_end;
static void __init get_pci_mmcfg_amd_fam10h_range(void)
{
	u32 address;
	u64 base, msr;
	unsigned segn_busn_bits;

	/* assume all cpus from fam10h have mmconf */
        if (boot_cpu_data.x86 < 0x10)
		return;

	address = MSR_FAM10H_MMIO_CONF_BASE;
	rdmsrl(address, msr);

	/* mmconfig is not enable */
	if (!(msr & FAM10H_MMIO_CONF_ENABLE))
		return;

	base = msr & (FAM10H_MMIO_CONF_BASE_MASK<<FAM10H_MMIO_CONF_BASE_SHIFT);

	segn_busn_bits = (msr >> FAM10H_MMIO_CONF_BUSRANGE_SHIFT) &
			 FAM10H_MMIO_CONF_BUSRANGE_MASK;

	fam10h_mmconf_start = base;
	fam10h_mmconf_end = base + (1ULL<<(segn_busn_bits + 20)) - 1;
}

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
	int found;
	int node;
	int link;
	int def_node;
	int def_link;
	struct pci_root_info *info;
	u32 reg;
	struct resource *res;
	size_t start;
	size_t end;
	struct res_range range[RANGE_NUM];
	u64 val;
	u32 address;

#ifdef CONFIG_NUMA
	for (i = 0; i < BUS_NR; i++)
		mp_bus_to_node[i] = -1;
#endif

	if (!early_pci_allowed())
		return -1;

	found = 0;
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
			found = 1;
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
			mp_bus_to_node[j] = (unsigned char) node;
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
	range[0].end = 0xffff;
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
		       node, link, (u64)start, (u64)end);

		/* kernel only handle 16 bit only */
		if (end > 0xffff)
			end = 0xffff;
		update_res(info, start, end, IORESOURCE_IO, 1);
		update_range(range, start, end);
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

			update_res(info, range[i].start, range[i].end,
				   IORESOURCE_IO, 1);
		}
	}

	memset(range, 0, sizeof(range));
	/* 0xfd00000000-0xffffffffff for HT */
	range[0].end = (0xfdULL<<32) - 1;

	/* need to take out [0, TOM) for RAM*/
	address = MSR_K8_TOP_MEM1;
	rdmsrl(address, val);
	end = (val & 0xffffff800000ULL);
	printk(KERN_INFO "TOM: %016lx aka %ldM\n", end, end>>20);
	if (end < (1ULL<<32))
		update_range(range, 0, end - 1);

	/* get mmconfig */
	get_pci_mmcfg_amd_fam10h_range();
	/* need to take out mmconf range */
	if (fam10h_mmconf_end) {
		printk(KERN_DEBUG "Fam 10h mmconf [%llx, %llx]\n", fam10h_mmconf_start, fam10h_mmconf_end);
		update_range(range, fam10h_mmconf_start, fam10h_mmconf_end);
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
		       node, link, (u64)start, (u64)end);
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
				update_range(range, start, endx);
				printk(KERN_CONT " ==> [%llx, %llx]", (u64)start, endx);
				start = fam10h_mmconf_end + 1;
				changed = 1;
			}
			if (changed) {
				if (start <= end) {
					printk(KERN_CONT " %s [%llx, %llx]", endx?"and":"==>", (u64)start, (u64)end);
				} else {
					printk(KERN_CONT "%s\n", endx?"":" ==> none");
					continue;
				}
			}
		}

		update_res(info, start, end, IORESOURCE_MEM, 1);
		update_range(range, start, end);
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
		printk(KERN_INFO "TOM2: %016lx aka %ldM\n", end, end>>20);
		update_range(range, 1ULL<<32, end - 1);
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

			update_res(info, range[i].start, range[i].end,
				   IORESOURCE_MEM, 1);
		}
	}

	for (i = 0; i < pci_root_num; i++) {
		int res_num;
		int busnum;

		info = &pci_root_info[i];
		res_num = info->res_num;
		busnum = info->bus_min;
		printk(KERN_DEBUG "bus: [%02x,%02x] on node %x link %x\n",
		       info->bus_min, info->bus_max, info->node, info->link);
		for (j = 0; j < res_num; j++) {
			res = &info->res[j];
			printk(KERN_DEBUG "bus: %02x index %x %s: [%llx, %llx]\n",
			       busnum, j,
			       (res->flags & IORESOURCE_IO)?"io port":"mmio",
			       res->start, res->end);
		}
	}

	return 0;
}

#else  /* !CONFIG_X86_64 */

static int __init early_fill_mp_bus_info(void) { return 0; }

#endif /* !CONFIG_X86_64 */

/* common 32/64 bit code */

#define ENABLE_CF8_EXT_CFG      (1ULL << 46)

static void enable_pci_io_ecs(void *unused)
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

static int __init pci_io_ecs_init(void)
{
	int cpu;

	/* assume all cpus from fam10h have IO ECS */
        if (boot_cpu_data.x86 < 0x10)
		return 0;

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
