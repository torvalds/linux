// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Intel Corporation.
 *
 * Heterogeneous Memory Attributes Table (HMAT) representation
 *
 * This program parses and reports the platform's HMAT tables, and registers
 * the applicable attributes with the node's interfaces.
 */

#define pr_fmt(fmt) "acpi/hmat: " fmt

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/list_sort.h>
#include <linux/memregion.h>
#include <linux/memory.h>
#include <linux/mutex.h>
#include <linux/node.h>
#include <linux/sysfs.h>
#include <linux/dax.h>
#include <linux/memory-tiers.h>

static u8 hmat_revision;
static int hmat_disable __initdata;

void __init disable_hmat(void)
{
	hmat_disable = 1;
}

static LIST_HEAD(targets);
static LIST_HEAD(initiators);
static LIST_HEAD(localities);

static DEFINE_MUTEX(target_lock);

/*
 * The defined enum order is used to prioritize attributes to break ties when
 * selecting the best performing node.
 */
enum locality_types {
	WRITE_LATENCY,
	READ_LATENCY,
	WRITE_BANDWIDTH,
	READ_BANDWIDTH,
};

static struct memory_locality *localities_types[4];

struct target_cache {
	struct list_head node;
	struct node_cache_attrs cache_attrs;
};

struct memory_target {
	struct list_head node;
	unsigned int memory_pxm;
	unsigned int processor_pxm;
	struct resource memregions;
	struct node_hmem_attrs hmem_attrs[2];
	struct list_head caches;
	struct node_cache_attrs cache_attrs;
	bool registered;
};

struct memory_initiator {
	struct list_head node;
	unsigned int processor_pxm;
	bool has_cpu;
};

struct memory_locality {
	struct list_head node;
	struct acpi_hmat_locality *hmat_loc;
};

static struct memory_initiator *find_mem_initiator(unsigned int cpu_pxm)
{
	struct memory_initiator *initiator;

	list_for_each_entry(initiator, &initiators, node)
		if (initiator->processor_pxm == cpu_pxm)
			return initiator;
	return NULL;
}

static struct memory_target *find_mem_target(unsigned int mem_pxm)
{
	struct memory_target *target;

	list_for_each_entry(target, &targets, node)
		if (target->memory_pxm == mem_pxm)
			return target;
	return NULL;
}

static __init void alloc_memory_initiator(unsigned int cpu_pxm)
{
	struct memory_initiator *initiator;

	if (pxm_to_node(cpu_pxm) == NUMA_NO_NODE)
		return;

	initiator = find_mem_initiator(cpu_pxm);
	if (initiator)
		return;

	initiator = kzalloc(sizeof(*initiator), GFP_KERNEL);
	if (!initiator)
		return;

	initiator->processor_pxm = cpu_pxm;
	initiator->has_cpu = node_state(pxm_to_node(cpu_pxm), N_CPU);
	list_add_tail(&initiator->node, &initiators);
}

static __init void alloc_memory_target(unsigned int mem_pxm,
		resource_size_t start, resource_size_t len)
{
	struct memory_target *target;

	target = find_mem_target(mem_pxm);
	if (!target) {
		target = kzalloc(sizeof(*target), GFP_KERNEL);
		if (!target)
			return;
		target->memory_pxm = mem_pxm;
		target->processor_pxm = PXM_INVAL;
		target->memregions = (struct resource) {
			.name	= "ACPI mem",
			.start	= 0,
			.end	= -1,
			.flags	= IORESOURCE_MEM,
		};
		list_add_tail(&target->node, &targets);
		INIT_LIST_HEAD(&target->caches);
	}

	/*
	 * There are potentially multiple ranges per PXM, so record each
	 * in the per-target memregions resource tree.
	 */
	if (!__request_region(&target->memregions, start, len, "memory target",
				IORESOURCE_MEM))
		pr_warn("failed to reserve %#llx - %#llx in pxm: %d\n",
				start, start + len, mem_pxm);
}

static __init const char *hmat_data_type(u8 type)
{
	switch (type) {
	case ACPI_HMAT_ACCESS_LATENCY:
		return "Access Latency";
	case ACPI_HMAT_READ_LATENCY:
		return "Read Latency";
	case ACPI_HMAT_WRITE_LATENCY:
		return "Write Latency";
	case ACPI_HMAT_ACCESS_BANDWIDTH:
		return "Access Bandwidth";
	case ACPI_HMAT_READ_BANDWIDTH:
		return "Read Bandwidth";
	case ACPI_HMAT_WRITE_BANDWIDTH:
		return "Write Bandwidth";
	default:
		return "Reserved";
	}
}

static __init const char *hmat_data_type_suffix(u8 type)
{
	switch (type) {
	case ACPI_HMAT_ACCESS_LATENCY:
	case ACPI_HMAT_READ_LATENCY:
	case ACPI_HMAT_WRITE_LATENCY:
		return " nsec";
	case ACPI_HMAT_ACCESS_BANDWIDTH:
	case ACPI_HMAT_READ_BANDWIDTH:
	case ACPI_HMAT_WRITE_BANDWIDTH:
		return " MB/s";
	default:
		return "";
	}
}

static u32 hmat_normalize(u16 entry, u64 base, u8 type)
{
	u32 value;

	/*
	 * Check for invalid and overflow values
	 */
	if (entry == 0xffff || !entry)
		return 0;
	else if (base > (UINT_MAX / (entry)))
		return 0;

	/*
	 * Divide by the base unit for version 1, convert latency from
	 * picosenonds to nanoseconds if revision 2.
	 */
	value = entry * base;
	if (hmat_revision == 1) {
		if (value < 10)
			return 0;
		value = DIV_ROUND_UP(value, 10);
	} else if (hmat_revision == 2) {
		switch (type) {
		case ACPI_HMAT_ACCESS_LATENCY:
		case ACPI_HMAT_READ_LATENCY:
		case ACPI_HMAT_WRITE_LATENCY:
			value = DIV_ROUND_UP(value, 1000);
			break;
		default:
			break;
		}
	}
	return value;
}

static void hmat_update_target_access(struct memory_target *target,
				      u8 type, u32 value, int access)
{
	switch (type) {
	case ACPI_HMAT_ACCESS_LATENCY:
		target->hmem_attrs[access].read_latency = value;
		target->hmem_attrs[access].write_latency = value;
		break;
	case ACPI_HMAT_READ_LATENCY:
		target->hmem_attrs[access].read_latency = value;
		break;
	case ACPI_HMAT_WRITE_LATENCY:
		target->hmem_attrs[access].write_latency = value;
		break;
	case ACPI_HMAT_ACCESS_BANDWIDTH:
		target->hmem_attrs[access].read_bandwidth = value;
		target->hmem_attrs[access].write_bandwidth = value;
		break;
	case ACPI_HMAT_READ_BANDWIDTH:
		target->hmem_attrs[access].read_bandwidth = value;
		break;
	case ACPI_HMAT_WRITE_BANDWIDTH:
		target->hmem_attrs[access].write_bandwidth = value;
		break;
	default:
		break;
	}
}

static __init void hmat_add_locality(struct acpi_hmat_locality *hmat_loc)
{
	struct memory_locality *loc;

	loc = kzalloc(sizeof(*loc), GFP_KERNEL);
	if (!loc) {
		pr_notice_once("Failed to allocate HMAT locality\n");
		return;
	}

	loc->hmat_loc = hmat_loc;
	list_add_tail(&loc->node, &localities);

	switch (hmat_loc->data_type) {
	case ACPI_HMAT_ACCESS_LATENCY:
		localities_types[READ_LATENCY] = loc;
		localities_types[WRITE_LATENCY] = loc;
		break;
	case ACPI_HMAT_READ_LATENCY:
		localities_types[READ_LATENCY] = loc;
		break;
	case ACPI_HMAT_WRITE_LATENCY:
		localities_types[WRITE_LATENCY] = loc;
		break;
	case ACPI_HMAT_ACCESS_BANDWIDTH:
		localities_types[READ_BANDWIDTH] = loc;
		localities_types[WRITE_BANDWIDTH] = loc;
		break;
	case ACPI_HMAT_READ_BANDWIDTH:
		localities_types[READ_BANDWIDTH] = loc;
		break;
	case ACPI_HMAT_WRITE_BANDWIDTH:
		localities_types[WRITE_BANDWIDTH] = loc;
		break;
	default:
		break;
	}
}

static __init int hmat_parse_locality(union acpi_subtable_headers *header,
				      const unsigned long end)
{
	struct acpi_hmat_locality *hmat_loc = (void *)header;
	struct memory_target *target;
	unsigned int init, targ, total_size, ipds, tpds;
	u32 *inits, *targs, value;
	u16 *entries;
	u8 type, mem_hier;

	if (hmat_loc->header.length < sizeof(*hmat_loc)) {
		pr_notice("Unexpected locality header length: %u\n",
			 hmat_loc->header.length);
		return -EINVAL;
	}

	type = hmat_loc->data_type;
	mem_hier = hmat_loc->flags & ACPI_HMAT_MEMORY_HIERARCHY;
	ipds = hmat_loc->number_of_initiator_Pds;
	tpds = hmat_loc->number_of_target_Pds;
	total_size = sizeof(*hmat_loc) + sizeof(*entries) * ipds * tpds +
		     sizeof(*inits) * ipds + sizeof(*targs) * tpds;
	if (hmat_loc->header.length < total_size) {
		pr_notice("Unexpected locality header length:%u, minimum required:%u\n",
			 hmat_loc->header.length, total_size);
		return -EINVAL;
	}

	pr_info("Locality: Flags:%02x Type:%s Initiator Domains:%u Target Domains:%u Base:%lld\n",
		hmat_loc->flags, hmat_data_type(type), ipds, tpds,
		hmat_loc->entry_base_unit);

	inits = (u32 *)(hmat_loc + 1);
	targs = inits + ipds;
	entries = (u16 *)(targs + tpds);
	for (init = 0; init < ipds; init++) {
		alloc_memory_initiator(inits[init]);
		for (targ = 0; targ < tpds; targ++) {
			value = hmat_normalize(entries[init * tpds + targ],
					       hmat_loc->entry_base_unit,
					       type);
			pr_info("  Initiator-Target[%u-%u]:%u%s\n",
				inits[init], targs[targ], value,
				hmat_data_type_suffix(type));

			if (mem_hier == ACPI_HMAT_MEMORY) {
				target = find_mem_target(targs[targ]);
				if (target && target->processor_pxm == inits[init]) {
					hmat_update_target_access(target, type, value, 0);
					/* If the node has a CPU, update access 1 */
					if (node_state(pxm_to_node(inits[init]), N_CPU))
						hmat_update_target_access(target, type, value, 1);
				}
			}
		}
	}

	if (mem_hier == ACPI_HMAT_MEMORY)
		hmat_add_locality(hmat_loc);

	return 0;
}

static __init int hmat_parse_cache(union acpi_subtable_headers *header,
				   const unsigned long end)
{
	struct acpi_hmat_cache *cache = (void *)header;
	struct memory_target *target;
	struct target_cache *tcache;
	u32 attrs;

	if (cache->header.length < sizeof(*cache)) {
		pr_notice("Unexpected cache header length: %u\n",
			 cache->header.length);
		return -EINVAL;
	}

	attrs = cache->cache_attributes;
	pr_info("Cache: Domain:%u Size:%llu Attrs:%08x SMBIOS Handles:%d\n",
		cache->memory_PD, cache->cache_size, attrs,
		cache->number_of_SMBIOShandles);

	target = find_mem_target(cache->memory_PD);
	if (!target)
		return 0;

	tcache = kzalloc(sizeof(*tcache), GFP_KERNEL);
	if (!tcache) {
		pr_notice_once("Failed to allocate HMAT cache info\n");
		return 0;
	}

	tcache->cache_attrs.size = cache->cache_size;
	tcache->cache_attrs.level = (attrs & ACPI_HMAT_CACHE_LEVEL) >> 4;
	tcache->cache_attrs.line_size = (attrs & ACPI_HMAT_CACHE_LINE_SIZE) >> 16;

	switch ((attrs & ACPI_HMAT_CACHE_ASSOCIATIVITY) >> 8) {
	case ACPI_HMAT_CA_DIRECT_MAPPED:
		tcache->cache_attrs.indexing = NODE_CACHE_DIRECT_MAP;
		break;
	case ACPI_HMAT_CA_COMPLEX_CACHE_INDEXING:
		tcache->cache_attrs.indexing = NODE_CACHE_INDEXED;
		break;
	case ACPI_HMAT_CA_NONE:
	default:
		tcache->cache_attrs.indexing = NODE_CACHE_OTHER;
		break;
	}

	switch ((attrs & ACPI_HMAT_WRITE_POLICY) >> 12) {
	case ACPI_HMAT_CP_WB:
		tcache->cache_attrs.write_policy = NODE_CACHE_WRITE_BACK;
		break;
	case ACPI_HMAT_CP_WT:
		tcache->cache_attrs.write_policy = NODE_CACHE_WRITE_THROUGH;
		break;
	case ACPI_HMAT_CP_NONE:
	default:
		tcache->cache_attrs.write_policy = NODE_CACHE_WRITE_OTHER;
		break;
	}
	list_add_tail(&tcache->node, &target->caches);

	return 0;
}

static int __init hmat_parse_proximity_domain(union acpi_subtable_headers *header,
					      const unsigned long end)
{
	struct acpi_hmat_proximity_domain *p = (void *)header;
	struct memory_target *target = NULL;

	if (p->header.length != sizeof(*p)) {
		pr_notice("Unexpected address range header length: %u\n",
			 p->header.length);
		return -EINVAL;
	}

	if (hmat_revision == 1)
		pr_info("Memory (%#llx length %#llx) Flags:%04x Processor Domain:%u Memory Domain:%u\n",
			p->reserved3, p->reserved4, p->flags, p->processor_PD,
			p->memory_PD);
	else
		pr_info("Memory Flags:%04x Processor Domain:%u Memory Domain:%u\n",
			p->flags, p->processor_PD, p->memory_PD);

	if ((hmat_revision == 1 && p->flags & ACPI_HMAT_MEMORY_PD_VALID) ||
	    hmat_revision > 1) {
		target = find_mem_target(p->memory_PD);
		if (!target) {
			pr_debug("Memory Domain missing from SRAT\n");
			return -EINVAL;
		}
	}
	if (target && p->flags & ACPI_HMAT_PROCESSOR_PD_VALID) {
		int p_node = pxm_to_node(p->processor_PD);

		if (p_node == NUMA_NO_NODE) {
			pr_debug("Invalid Processor Domain\n");
			return -EINVAL;
		}
		target->processor_pxm = p->processor_PD;
	}

	return 0;
}

static int __init hmat_parse_subtable(union acpi_subtable_headers *header,
				      const unsigned long end)
{
	struct acpi_hmat_structure *hdr = (void *)header;

	if (!hdr)
		return -EINVAL;

	switch (hdr->type) {
	case ACPI_HMAT_TYPE_PROXIMITY:
		return hmat_parse_proximity_domain(header, end);
	case ACPI_HMAT_TYPE_LOCALITY:
		return hmat_parse_locality(header, end);
	case ACPI_HMAT_TYPE_CACHE:
		return hmat_parse_cache(header, end);
	default:
		return -EINVAL;
	}
}

static __init int srat_parse_mem_affinity(union acpi_subtable_headers *header,
					  const unsigned long end)
{
	struct acpi_srat_mem_affinity *ma = (void *)header;

	if (!ma)
		return -EINVAL;
	if (!(ma->flags & ACPI_SRAT_MEM_ENABLED))
		return 0;
	alloc_memory_target(ma->proximity_domain, ma->base_address, ma->length);
	return 0;
}

static u32 hmat_initiator_perf(struct memory_target *target,
			       struct memory_initiator *initiator,
			       struct acpi_hmat_locality *hmat_loc)
{
	unsigned int ipds, tpds, i, idx = 0, tdx = 0;
	u32 *inits, *targs;
	u16 *entries;

	ipds = hmat_loc->number_of_initiator_Pds;
	tpds = hmat_loc->number_of_target_Pds;
	inits = (u32 *)(hmat_loc + 1);
	targs = inits + ipds;
	entries = (u16 *)(targs + tpds);

	for (i = 0; i < ipds; i++) {
		if (inits[i] == initiator->processor_pxm) {
			idx = i;
			break;
		}
	}

	if (i == ipds)
		return 0;

	for (i = 0; i < tpds; i++) {
		if (targs[i] == target->memory_pxm) {
			tdx = i;
			break;
		}
	}
	if (i == tpds)
		return 0;

	return hmat_normalize(entries[idx * tpds + tdx],
			      hmat_loc->entry_base_unit,
			      hmat_loc->data_type);
}

static bool hmat_update_best(u8 type, u32 value, u32 *best)
{
	bool updated = false;

	if (!value)
		return false;

	switch (type) {
	case ACPI_HMAT_ACCESS_LATENCY:
	case ACPI_HMAT_READ_LATENCY:
	case ACPI_HMAT_WRITE_LATENCY:
		if (!*best || *best > value) {
			*best = value;
			updated = true;
		}
		break;
	case ACPI_HMAT_ACCESS_BANDWIDTH:
	case ACPI_HMAT_READ_BANDWIDTH:
	case ACPI_HMAT_WRITE_BANDWIDTH:
		if (!*best || *best < value) {
			*best = value;
			updated = true;
		}
		break;
	}

	return updated;
}

static int initiator_cmp(void *priv, const struct list_head *a,
			 const struct list_head *b)
{
	struct memory_initiator *ia;
	struct memory_initiator *ib;

	ia = list_entry(a, struct memory_initiator, node);
	ib = list_entry(b, struct memory_initiator, node);

	return ia->processor_pxm - ib->processor_pxm;
}

static int initiators_to_nodemask(unsigned long *p_nodes)
{
	struct memory_initiator *initiator;

	if (list_empty(&initiators))
		return -ENXIO;

	list_for_each_entry(initiator, &initiators, node)
		set_bit(initiator->processor_pxm, p_nodes);

	return 0;
}

static void hmat_update_target_attrs(struct memory_target *target,
				     unsigned long *p_nodes, int access)
{
	struct memory_initiator *initiator;
	unsigned int cpu_nid;
	struct memory_locality *loc = NULL;
	u32 best = 0;
	int i;

	bitmap_zero(p_nodes, MAX_NUMNODES);
	/*
	 * If the Address Range Structure provides a local processor pxm, set
	 * only that one. Otherwise, find the best performance attributes and
	 * collect all initiators that match.
	 */
	if (target->processor_pxm != PXM_INVAL) {
		cpu_nid = pxm_to_node(target->processor_pxm);
		if (access == 0 || node_state(cpu_nid, N_CPU)) {
			set_bit(target->processor_pxm, p_nodes);
			return;
		}
	}

	if (list_empty(&localities))
		return;

	/*
	 * We need the initiator list sorted so we can use bitmap_clear for
	 * previously set initiators when we find a better memory accessor.
	 * We'll also use the sorting to prime the candidate nodes with known
	 * initiators.
	 */
	list_sort(NULL, &initiators, initiator_cmp);
	if (initiators_to_nodemask(p_nodes) < 0)
		return;

	for (i = WRITE_LATENCY; i <= READ_BANDWIDTH; i++) {
		loc = localities_types[i];
		if (!loc)
			continue;

		best = 0;
		list_for_each_entry(initiator, &initiators, node) {
			u32 value;

			if (access == 1 && !initiator->has_cpu) {
				clear_bit(initiator->processor_pxm, p_nodes);
				continue;
			}
			if (!test_bit(initiator->processor_pxm, p_nodes))
				continue;

			value = hmat_initiator_perf(target, initiator, loc->hmat_loc);
			if (hmat_update_best(loc->hmat_loc->data_type, value, &best))
				bitmap_clear(p_nodes, 0, initiator->processor_pxm);
			if (value != best)
				clear_bit(initiator->processor_pxm, p_nodes);
		}
		if (best)
			hmat_update_target_access(target, loc->hmat_loc->data_type, best, access);
	}
}

static void __hmat_register_target_initiators(struct memory_target *target,
					      unsigned long *p_nodes,
					      int access)
{
	unsigned int mem_nid, cpu_nid;
	int i;

	mem_nid = pxm_to_node(target->memory_pxm);
	hmat_update_target_attrs(target, p_nodes, access);
	for_each_set_bit(i, p_nodes, MAX_NUMNODES) {
		cpu_nid = pxm_to_node(i);
		register_memory_node_under_compute_node(mem_nid, cpu_nid, access);
	}
}

static void hmat_register_target_initiators(struct memory_target *target)
{
	static DECLARE_BITMAP(p_nodes, MAX_NUMNODES);

	__hmat_register_target_initiators(target, p_nodes, 0);
	__hmat_register_target_initiators(target, p_nodes, 1);
}

static void hmat_register_target_cache(struct memory_target *target)
{
	unsigned mem_nid = pxm_to_node(target->memory_pxm);
	struct target_cache *tcache;

	list_for_each_entry(tcache, &target->caches, node)
		node_add_cache(mem_nid, &tcache->cache_attrs);
}

static void hmat_register_target_perf(struct memory_target *target, int access)
{
	unsigned mem_nid = pxm_to_node(target->memory_pxm);
	node_set_perf_attrs(mem_nid, &target->hmem_attrs[access], access);
}

static void hmat_register_target_devices(struct memory_target *target)
{
	struct resource *res;

	/*
	 * Do not bother creating devices if no driver is available to
	 * consume them.
	 */
	if (!IS_ENABLED(CONFIG_DEV_DAX_HMEM))
		return;

	for (res = target->memregions.child; res; res = res->sibling) {
		int target_nid = pxm_to_node(target->memory_pxm);

		hmem_register_resource(target_nid, res);
	}
}

static void hmat_register_target(struct memory_target *target)
{
	int nid = pxm_to_node(target->memory_pxm);

	/*
	 * Devices may belong to either an offline or online
	 * node, so unconditionally add them.
	 */
	hmat_register_target_devices(target);

	/*
	 * Skip offline nodes. This can happen when memory
	 * marked EFI_MEMORY_SP, "specific purpose", is applied
	 * to all the memory in a proximity domain leading to
	 * the node being marked offline / unplugged, or if
	 * memory-only "hotplug" node is offline.
	 */
	if (nid == NUMA_NO_NODE || !node_online(nid))
		return;

	mutex_lock(&target_lock);
	if (!target->registered) {
		hmat_register_target_initiators(target);
		hmat_register_target_cache(target);
		hmat_register_target_perf(target, 0);
		hmat_register_target_perf(target, 1);
		target->registered = true;
	}
	mutex_unlock(&target_lock);
}

static void hmat_register_targets(void)
{
	struct memory_target *target;

	list_for_each_entry(target, &targets, node)
		hmat_register_target(target);
}

static int hmat_callback(struct notifier_block *self,
			 unsigned long action, void *arg)
{
	struct memory_target *target;
	struct memory_notify *mnb = arg;
	int pxm, nid = mnb->status_change_nid;

	if (nid == NUMA_NO_NODE || action != MEM_ONLINE)
		return NOTIFY_OK;

	pxm = node_to_pxm(nid);
	target = find_mem_target(pxm);
	if (!target)
		return NOTIFY_OK;

	hmat_register_target(target);
	return NOTIFY_OK;
}

static int hmat_set_default_dram_perf(void)
{
	int rc;
	int nid, pxm;
	struct memory_target *target;
	struct node_hmem_attrs *attrs;

	if (!default_dram_type)
		return -EIO;

	for_each_node_mask(nid, default_dram_type->nodes) {
		pxm = node_to_pxm(nid);
		target = find_mem_target(pxm);
		if (!target)
			continue;
		attrs = &target->hmem_attrs[1];
		rc = mt_set_default_dram_perf(nid, attrs, "ACPI HMAT");
		if (rc)
			return rc;
	}

	return 0;
}

static int hmat_calculate_adistance(struct notifier_block *self,
				    unsigned long nid, void *data)
{
	static DECLARE_BITMAP(p_nodes, MAX_NUMNODES);
	struct memory_target *target;
	struct node_hmem_attrs *perf;
	int *adist = data;
	int pxm;

	pxm = node_to_pxm(nid);
	target = find_mem_target(pxm);
	if (!target)
		return NOTIFY_OK;

	mutex_lock(&target_lock);
	hmat_update_target_attrs(target, p_nodes, 1);
	mutex_unlock(&target_lock);

	perf = &target->hmem_attrs[1];

	if (mt_perf_to_adistance(perf, adist))
		return NOTIFY_OK;

	return NOTIFY_STOP;
}

static struct notifier_block hmat_adist_nb __meminitdata = {
	.notifier_call = hmat_calculate_adistance,
	.priority = 100,
};

static __init void hmat_free_structures(void)
{
	struct memory_target *target, *tnext;
	struct memory_locality *loc, *lnext;
	struct memory_initiator *initiator, *inext;
	struct target_cache *tcache, *cnext;

	list_for_each_entry_safe(target, tnext, &targets, node) {
		struct resource *res, *res_next;

		list_for_each_entry_safe(tcache, cnext, &target->caches, node) {
			list_del(&tcache->node);
			kfree(tcache);
		}

		list_del(&target->node);
		res = target->memregions.child;
		while (res) {
			res_next = res->sibling;
			__release_region(&target->memregions, res->start,
					resource_size(res));
			res = res_next;
		}
		kfree(target);
	}

	list_for_each_entry_safe(initiator, inext, &initiators, node) {
		list_del(&initiator->node);
		kfree(initiator);
	}

	list_for_each_entry_safe(loc, lnext, &localities, node) {
		list_del(&loc->node);
		kfree(loc);
	}
}

static __init int hmat_init(void)
{
	struct acpi_table_header *tbl;
	enum acpi_hmat_type i;
	acpi_status status;

	if (srat_disabled() || hmat_disable)
		return 0;

	status = acpi_get_table(ACPI_SIG_SRAT, 0, &tbl);
	if (ACPI_FAILURE(status))
		return 0;

	if (acpi_table_parse_entries(ACPI_SIG_SRAT,
				sizeof(struct acpi_table_srat),
				ACPI_SRAT_TYPE_MEMORY_AFFINITY,
				srat_parse_mem_affinity, 0) < 0)
		goto out_put;
	acpi_put_table(tbl);

	status = acpi_get_table(ACPI_SIG_HMAT, 0, &tbl);
	if (ACPI_FAILURE(status))
		goto out_put;

	hmat_revision = tbl->revision;
	switch (hmat_revision) {
	case 1:
	case 2:
		break;
	default:
		pr_notice("Ignoring: Unknown revision:%d\n", hmat_revision);
		goto out_put;
	}

	for (i = ACPI_HMAT_TYPE_PROXIMITY; i < ACPI_HMAT_TYPE_RESERVED; i++) {
		if (acpi_table_parse_entries(ACPI_SIG_HMAT,
					     sizeof(struct acpi_table_hmat), i,
					     hmat_parse_subtable, 0) < 0) {
			pr_notice("Ignoring: Invalid table");
			goto out_put;
		}
	}
	hmat_register_targets();

	/* Keep the table and structures if the notifier may use them */
	if (hotplug_memory_notifier(hmat_callback, HMAT_CALLBACK_PRI))
		goto out_put;

	if (!hmat_set_default_dram_perf())
		register_mt_adistance_algorithm(&hmat_adist_nb);

	return 0;
out_put:
	hmat_free_structures();
	acpi_put_table(tbl);
	return 0;
}
subsys_initcall(hmat_init);
