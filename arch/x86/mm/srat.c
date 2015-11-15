/*
 * ACPI 3.0 based NUMA setup
 * Copyright 2004 Andi Kleen, SuSE Labs.
 *
 * Reads the ACPI SRAT table to figure out what memory belongs to which CPUs.
 *
 * Called from acpi_numa_init while reading the SRAT and SLIT tables.
 * Assumes all memory regions belonging to a single proximity domain
 * are in one chunk. Holes between them will be included in the node.
 */

#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/mmzone.h>
#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/topology.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <asm/proto.h>
#include <asm/numa.h>
#include <asm/e820.h>
#include <asm/apic.h>
#include <asm/uv/uv.h>

int acpi_numa __initdata;

static __init int setup_node(int pxm)
{
	return acpi_map_pxm_to_node(pxm);
}

static __init void bad_srat(void)
{
	printk(KERN_ERR "SRAT: SRAT not used.\n");
	acpi_numa = -1;
}

static __init inline int srat_disabled(void)
{
	return acpi_numa < 0;
}

/*
 * Callback for SLIT parsing.  pxm_to_node() returns NUMA_NO_NODE for
 * I/O localities since SRAT does not list them.  I/O localities are
 * not supported at this point.
 */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
	int i, j;

	for (i = 0; i < slit->locality_count; i++) {
		const int from_node = pxm_to_node(i);

		if (from_node == NUMA_NO_NODE)
			continue;

		for (j = 0; j < slit->locality_count; j++) {
			const int to_node = pxm_to_node(j);

			if (to_node == NUMA_NO_NODE)
				continue;

			numa_set_distance(from_node, to_node,
				slit->entry[slit->locality_count * i + j]);
		}
	}
}

/* Callback for Proximity Domain -> x2APIC mapping */
void __init
acpi_numa_x2apic_affinity_init(struct acpi_srat_x2apic_cpu_affinity *pa)
{
	int pxm, node;
	int apic_id;

	if (srat_disabled())
		return;
	if (pa->header.length < sizeof(struct acpi_srat_x2apic_cpu_affinity)) {
		bad_srat();
		return;
	}
	if ((pa->flags & ACPI_SRAT_CPU_ENABLED) == 0)
		return;
	pxm = pa->proximity_domain;
	apic_id = pa->apic_id;
	if (!apic->apic_id_valid(apic_id)) {
		printk(KERN_INFO "SRAT: PXM %u -> X2APIC 0x%04x ignored\n",
			 pxm, apic_id);
		return;
	}
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains %x\n", pxm);
		bad_srat();
		return;
	}

	if (apic_id >= MAX_LOCAL_APIC) {
		printk(KERN_INFO "SRAT: PXM %u -> APIC 0x%04x -> Node %u skipped apicid that is too big\n", pxm, apic_id, node);
		return;
	}
	set_apicid_to_node(apic_id, node);
	node_set(node, numa_nodes_parsed);
	acpi_numa = 1;
	printk(KERN_INFO "SRAT: PXM %u -> APIC 0x%04x -> Node %u\n",
	       pxm, apic_id, node);
}

/* Callback for Proximity Domain -> LAPIC mapping */
void __init
acpi_numa_processor_affinity_init(struct acpi_srat_cpu_affinity *pa)
{
	int pxm, node;
	int apic_id;

	if (srat_disabled())
		return;
	if (pa->header.length != sizeof(struct acpi_srat_cpu_affinity)) {
		bad_srat();
		return;
	}
	if ((pa->flags & ACPI_SRAT_CPU_ENABLED) == 0)
		return;
	pxm = pa->proximity_domain_lo;
	if (acpi_srat_revision >= 2)
		pxm |= *((unsigned int*)pa->proximity_domain_hi) << 8;
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains %x\n", pxm);
		bad_srat();
		return;
	}

	if (get_uv_system_type() >= UV_X2APIC)
		apic_id = (pa->apic_id << 8) | pa->local_sapic_eid;
	else
		apic_id = pa->apic_id;

	if (apic_id >= MAX_LOCAL_APIC) {
		printk(KERN_INFO "SRAT: PXM %u -> APIC 0x%02x -> Node %u skipped apicid that is too big\n", pxm, apic_id, node);
		return;
	}

	set_apicid_to_node(apic_id, node);
	node_set(node, numa_nodes_parsed);
	acpi_numa = 1;
	printk(KERN_INFO "SRAT: PXM %u -> APIC 0x%02x -> Node %u\n",
	       pxm, apic_id, node);
}

#ifdef CONFIG_MEMORY_HOTPLUG
static inline int save_add_info(void) {return 1;}
#else
static inline int save_add_info(void) {return 0;}
#endif

/* Callback for parsing of the Proximity Domain <-> Memory Area mappings */
int __init
acpi_numa_memory_affinity_init(struct acpi_srat_mem_affinity *ma)
{
	u64 start, end;
	u32 hotpluggable;
	int node, pxm;

	if (srat_disabled())
		goto out_err;
	if (ma->header.length != sizeof(struct acpi_srat_mem_affinity))
		goto out_err_bad_srat;
	if ((ma->flags & ACPI_SRAT_MEM_ENABLED) == 0)
		goto out_err;
	hotpluggable = ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE;
	if (hotpluggable && !save_add_info())
		goto out_err;

	start = ma->base_address;
	end = start + ma->length;
	pxm = ma->proximity_domain;
	if (acpi_srat_revision <= 1)
		pxm &= 0xff;

	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains.\n");
		goto out_err_bad_srat;
	}

	if (numa_add_memblk(node, start, end) < 0)
		goto out_err_bad_srat;

	node_set(node, numa_nodes_parsed);

	pr_info("SRAT: Node %u PXM %u [mem %#010Lx-%#010Lx]%s%s\n",
		node, pxm,
		(unsigned long long) start, (unsigned long long) end - 1,
		hotpluggable ? " hotplug" : "",
		ma->flags & ACPI_SRAT_MEM_NON_VOLATILE ? " non-volatile" : "");

	/* Mark hotplug range in memblock. */
	if (hotpluggable && memblock_mark_hotplug(start, ma->length))
		pr_warn("SRAT: Failed to mark hotplug range [mem %#010Lx-%#010Lx] in memblock\n",
			(unsigned long long)start, (unsigned long long)end - 1);

	return 0;
out_err_bad_srat:
	bad_srat();
out_err:
	return -1;
}

void __init acpi_numa_arch_fixup(void) {}

int __init x86_acpi_numa_init(void)
{
	int ret;

	ret = acpi_numa_init();
	if (ret < 0)
		return ret;
	return srat_disabled() ? -EINVAL : 0;
}
