// SPDX-License-Identifier: GPL-2.0
/*
 * acpi.c - Architecture-Specific Low-Level ACPI Boot Support
 *
 * Author: Jianmin Lv <lvjianmin@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/efi-bgrt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/serial_core.h>
#include <asm/io.h>
#include <asm/numa.h>
#include <asm/loongson.h>

int acpi_disabled;
EXPORT_SYMBOL(acpi_disabled);
int acpi_noirq;
int acpi_pci_disabled;
EXPORT_SYMBOL(acpi_pci_disabled);
int acpi_strict = 1; /* We have no workarounds on LoongArch */
int num_processors;
int disabled_cpus;

u64 acpi_saved_sp;

#define PREFIX			"ACPI: "

struct acpi_madt_core_pic acpi_core_pic[MAX_CORE_PIC];

void __init __iomem * __acpi_map_table(unsigned long phys, unsigned long size)
{

	if (!phys || !size)
		return NULL;

	return early_memremap(phys, size);
}
void __init __acpi_unmap_table(void __iomem *map, unsigned long size)
{
	if (!map || !size)
		return;

	early_memunmap(map, size);
}

void __iomem *acpi_os_ioremap(acpi_physical_address phys, acpi_size size)
{
	if (!memblock_is_memory(phys))
		return ioremap(phys, size);
	else
		return ioremap_cache(phys, size);
}

#ifdef CONFIG_SMP
static int set_processor_mask(u32 id, u32 pass)
{
	int cpu = -1, cpuid = id;

	if (num_processors >= NR_CPUS) {
		pr_warn(PREFIX "nr_cpus limit of %i reached."
			" processor 0x%x ignored.\n", NR_CPUS, cpuid);

		return -ENODEV;

	}

	if (cpuid == loongson_sysconf.boot_cpu_id)
		cpu = 0;

	switch (pass) {
	case 1: /* Pass 1 handle enabled processors */
		if (cpu < 0)
			cpu = find_first_zero_bit(cpumask_bits(cpu_present_mask), NR_CPUS);
		num_processors++;
		set_cpu_present(cpu, true);
		break;
	case 2: /* Pass 2 handle disabled processors */
		if (cpu < 0)
			cpu = find_first_zero_bit(cpumask_bits(cpu_possible_mask), NR_CPUS);
		disabled_cpus++;
		break;
	default:
		return cpu;
	}

	set_cpu_possible(cpu, true);
	__cpu_number_map[cpuid] = cpu;
	__cpu_logical_map[cpu] = cpuid;

	return cpu;
}
#endif

static int __init
acpi_parse_p1_processor(union acpi_subtable_headers *header, const unsigned long end)
{
	struct acpi_madt_core_pic *processor = NULL;

	processor = (struct acpi_madt_core_pic *)header;
	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(&header->common);
#ifdef CONFIG_SMP
	acpi_core_pic[processor->core_id] = *processor;
	if (processor->flags & ACPI_MADT_ENABLED)
		set_processor_mask(processor->core_id, 1);
#endif

	return 0;
}

static int __init
acpi_parse_p2_processor(union acpi_subtable_headers *header, const unsigned long end)
{
	struct acpi_madt_core_pic *processor = NULL;

	processor = (struct acpi_madt_core_pic *)header;
	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

#ifdef CONFIG_SMP
	if (!(processor->flags & ACPI_MADT_ENABLED))
		set_processor_mask(processor->core_id, 2);
#endif

	return 0;
}
static int __init
acpi_parse_eio_master(union acpi_subtable_headers *header, const unsigned long end)
{
	static int core = 0;
	struct acpi_madt_eio_pic *eiointc = NULL;

	eiointc = (struct acpi_madt_eio_pic *)header;
	if (BAD_MADT_ENTRY(eiointc, end))
		return -EINVAL;

	core = eiointc->node * CORES_PER_EIO_NODE;
	set_bit(core, loongson_sysconf.cores_io_master);

	return 0;
}

static void __init acpi_process_madt(void)
{
#ifdef CONFIG_SMP
	int i;

	for (i = 0; i < NR_CPUS; i++) {
		__cpu_number_map[i] = -1;
		__cpu_logical_map[i] = -1;
	}
#endif
	acpi_table_parse_madt(ACPI_MADT_TYPE_CORE_PIC,
			acpi_parse_p1_processor, MAX_CORE_PIC);

	acpi_table_parse_madt(ACPI_MADT_TYPE_CORE_PIC,
			acpi_parse_p2_processor, MAX_CORE_PIC);

	acpi_table_parse_madt(ACPI_MADT_TYPE_EIO_PIC,
			acpi_parse_eio_master, MAX_IO_PICS);

	loongson_sysconf.nr_cpus = num_processors;
}

int pptt_enabled;

int __init parse_acpi_topology(void)
{
	int cpu, topology_id;

	for_each_possible_cpu(cpu) {
		topology_id = find_acpi_cpu_topology(cpu, 0);
		if (topology_id < 0) {
			pr_warn("Invalid BIOS PPTT\n");
			return -ENOENT;
		}

		if (acpi_pptt_cpu_is_thread(cpu) <= 0)
			cpu_data[cpu].core = topology_id;
		else {
			topology_id = find_acpi_cpu_topology(cpu, 1);
			if (topology_id < 0)
				return -ENOENT;

			cpu_data[cpu].core = topology_id;
		}
	}

	pptt_enabled = 1;

	return 0;
}

#ifndef CONFIG_SUSPEND
int (*acpi_suspend_lowlevel)(void);
#else
int (*acpi_suspend_lowlevel)(void) = loongarch_acpi_suspend;
#endif

void __init acpi_boot_table_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 */
	if (acpi_disabled)
		goto fdt_earlycon;

	/*
	 * Initialize the ACPI boot-time table parser.
	 */
	if (acpi_table_init()) {
		disable_acpi();
		goto fdt_earlycon;
	}

	loongson_sysconf.boot_cpu_id = read_csr_cpuid();

	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	acpi_process_madt();

	/* Do not enable ACPI SPCR console by default */
	acpi_parse_spcr(earlycon_acpi_spcr_enable, false);

	if (IS_ENABLED(CONFIG_ACPI_BGRT))
		acpi_table_parse(ACPI_SIG_BGRT, acpi_parse_bgrt);

	return;

fdt_earlycon:
	if (earlycon_acpi_spcr_enable)
		early_init_dt_scan_chosen_stdout();
}

#ifdef CONFIG_ACPI_NUMA

static __init int setup_node(int pxm)
{
	return acpi_map_pxm_to_node(pxm);
}

/*
 * Callback for SLIT parsing.  pxm_to_node() returns NUMA_NO_NODE for
 * I/O localities since SRAT does not list them.  I/O localities are
 * not supported at this point.
 */
unsigned int numa_distance_cnt;

static inline unsigned int get_numa_distances_cnt(struct acpi_table_slit *slit)
{
	return slit->locality_count;
}

void __init numa_set_distance(int from, int to, int distance)
{
	if ((u8)distance != distance || (from == to && distance != LOCAL_DISTANCE)) {
		pr_warn_once("Warning: invalid distance parameter, from=%d to=%d distance=%d\n",
				from, to, distance);
		return;
	}

	node_distances[from][to] = distance;
}

/* Callback for Proximity Domain -> CPUID mapping */
void __init
acpi_numa_processor_affinity_init(struct acpi_srat_cpu_affinity *pa)
{
	int pxm, node;

	if (srat_disabled())
		return;
	if (pa->header.length != sizeof(struct acpi_srat_cpu_affinity)) {
		bad_srat();
		return;
	}
	if ((pa->flags & ACPI_SRAT_CPU_ENABLED) == 0)
		return;
	pxm = pa->proximity_domain_lo;
	if (acpi_srat_revision >= 2) {
		pxm |= (pa->proximity_domain_hi[0] << 8);
		pxm |= (pa->proximity_domain_hi[1] << 16);
		pxm |= (pa->proximity_domain_hi[2] << 24);
	}
	node = setup_node(pxm);
	if (node < 0) {
		pr_err("SRAT: Too many proximity domains %x\n", pxm);
		bad_srat();
		return;
	}

	if (pa->apic_id >= CONFIG_NR_CPUS) {
		pr_info("SRAT: PXM %u -> CPU 0x%02x -> Node %u skipped apicid that is too big\n",
				pxm, pa->apic_id, node);
		return;
	}

	early_numa_add_cpu(pa->apic_id, node);

	set_cpuid_to_node(pa->apic_id, node);
	node_set(node, numa_nodes_parsed);
	pr_info("SRAT: PXM %u -> CPU 0x%02x -> Node %u\n", pxm, pa->apic_id, node);
}

#endif

void __init arch_reserve_mem_area(acpi_physical_address addr, size_t size)
{
	memblock_reserve(addr, size);
}

#ifdef CONFIG_ACPI_HOTPLUG_CPU

#include <acpi/processor.h>

static int __ref acpi_map_cpu2node(acpi_handle handle, int cpu, int physid)
{
#ifdef CONFIG_ACPI_NUMA
	int nid;

	nid = acpi_get_node(handle);

	if (nid != NUMA_NO_NODE)
		nid = early_cpu_to_node(cpu);

	if (nid != NUMA_NO_NODE) {
		set_cpuid_to_node(physid, nid);
		node_set(nid, numa_nodes_parsed);
		set_cpu_numa_node(cpu, nid);
		cpumask_set_cpu(cpu, cpumask_of_node(nid));
	}
#endif
	return 0;
}

int acpi_map_cpu(acpi_handle handle, phys_cpuid_t physid, u32 acpi_id, int *pcpu)
{
	int cpu;

	cpu = cpu_number_map(physid);
	if (cpu < 0 || cpu >= nr_cpu_ids) {
		pr_info(PREFIX "Unable to map lapic to logical cpu number\n");
		return -ERANGE;
	}

	num_processors++;
	set_cpu_present(cpu, true);
	acpi_map_cpu2node(handle, cpu, physid);

	*pcpu = cpu;

	return 0;
}
EXPORT_SYMBOL(acpi_map_cpu);

int acpi_unmap_cpu(int cpu)
{
#ifdef CONFIG_ACPI_NUMA
	set_cpuid_to_node(cpu_logical_map(cpu), NUMA_NO_NODE);
#endif
	set_cpu_present(cpu, false);
	num_processors--;

	pr_info("cpu%d hot remove!\n", cpu);

	return 0;
}
EXPORT_SYMBOL(acpi_unmap_cpu);

#endif /* CONFIG_ACPI_HOTPLUG_CPU */
