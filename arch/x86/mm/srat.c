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

/* Callback for SLIT parsing */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
	int i, j;

	for (i = 0; i < slit->locality_count; i++)
		for (j = 0; j < slit->locality_count; j++)
			numa_set_distance(pxm_to_node(i), pxm_to_node(j),
				slit->entry[slit->locality_count * i + j]);
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

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
static void __init handle_movablemem(int node, u64 start, u64 end)
{
	int overlap;
	unsigned long start_pfn, end_pfn;

	start_pfn = PFN_DOWN(start);
	end_pfn = PFN_UP(end);

	/*
	 * For movablecore_map=nn[KMG]@ss[KMG]:
	 *
	 * SRAT:		|_____| |_____| |_________| |_________| ......
	 * node id:		   0       1         1           2
	 * user specified:	          |__|                 |___|
	 * movablemem_map:		  |___| |_________|    |______| ......
	 *
	 * Using movablemem_map, we can prevent memblock from allocating memory
	 * on ZONE_MOVABLE at boot time.
	 */
	overlap = movablemem_map_overlap(start_pfn, end_pfn);
	if (overlap >= 0) {
		/*
		 * If part of this range is in movablemem_map, we need to
		 * add the range after it to extend the range to the end
		 * of the node, because from the min address specified to
		 * the end of the node will be ZONE_MOVABLE.
		 */
		start_pfn = max(start_pfn,
			    movablemem_map.map[overlap].start_pfn);
		insert_movablemem_map(start_pfn, end_pfn);

		/*
		 * Set the nodemask, so that if the address range on one node
		 * is not continuse, we can add the subsequent ranges on the
		 * same node into movablemem_map.
		 */
		node_set(node, movablemem_map.numa_nodes_hotplug);
	} else {
		if (node_isset(node, movablemem_map.numa_nodes_hotplug))
			/*
			 * Insert the range if we already have movable ranges
			 * on the same node.
			 */
			insert_movablemem_map(start_pfn, end_pfn);
	}
}
#else		/* CONFIG_HAVE_MEMBLOCK_NODE_MAP */
static inline void handle_movablemem(int node, u64 start, u64 end)
{
}
#endif		/* CONFIG_HAVE_MEMBLOCK_NODE_MAP */

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

	printk(KERN_INFO "SRAT: Node %u PXM %u [mem %#010Lx-%#010Lx] %s\n",
	       node, pxm,
	       (unsigned long long) start, (unsigned long long) end - 1,
	       hotpluggable ? "Hot Pluggable": "");

	handle_movablemem(node, start, end);

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
