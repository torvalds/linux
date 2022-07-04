// SPDX-License-Identifier: GPL-2.0
/*
 * Memory subsystem support
 *
 * Written by Matt Tolentino <matthew.e.tolentino@intel.com>
 *            Dave Hansen <haveblue@us.ibm.com>
 *
 * This file provides the necessary infrastructure to represent
 * a SPARSEMEM-memory-model system's physical memory in /sysfs.
 * All arch-independent code that assumes MEMORY_HOTPLUG requires
 * SPARSEMEM should be contained here, or in mm/memory_hotplug.c.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/topology.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include <linux/atomic.h>
#include <linux/uaccess.h>

#define MEMORY_CLASS_NAME	"memory"

static const char *const online_type_to_str[] = {
	[MMOP_OFFLINE] = "offline",
	[MMOP_ONLINE] = "online",
	[MMOP_ONLINE_KERNEL] = "online_kernel",
	[MMOP_ONLINE_MOVABLE] = "online_movable",
};

int mhp_online_type_from_str(const char *str)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(online_type_to_str); i++) {
		if (sysfs_streq(str, online_type_to_str[i]))
			return i;
	}
	return -EINVAL;
}

#define to_memory_block(dev) container_of(dev, struct memory_block, dev)

static int sections_per_block;

static inline unsigned long memory_block_id(unsigned long section_nr)
{
	return section_nr / sections_per_block;
}

static inline unsigned long pfn_to_block_id(unsigned long pfn)
{
	return memory_block_id(pfn_to_section_nr(pfn));
}

static inline unsigned long phys_to_block_id(unsigned long phys)
{
	return pfn_to_block_id(PFN_DOWN(phys));
}

static int memory_subsys_online(struct device *dev);
static int memory_subsys_offline(struct device *dev);

static struct bus_type memory_subsys = {
	.name = MEMORY_CLASS_NAME,
	.dev_name = MEMORY_CLASS_NAME,
	.online = memory_subsys_online,
	.offline = memory_subsys_offline,
};

/*
 * Memory blocks are cached in a local radix tree to avoid
 * a costly linear search for the corresponding device on
 * the subsystem bus.
 */
static DEFINE_XARRAY(memory_blocks);

/*
 * Memory groups, indexed by memory group id (mgid).
 */
static DEFINE_XARRAY_FLAGS(memory_groups, XA_FLAGS_ALLOC);
#define MEMORY_GROUP_MARK_DYNAMIC	XA_MARK_1

static BLOCKING_NOTIFIER_HEAD(memory_chain);

int register_memory_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&memory_chain, nb);
}
EXPORT_SYMBOL(register_memory_notifier);

void unregister_memory_notifier(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&memory_chain, nb);
}
EXPORT_SYMBOL(unregister_memory_notifier);

static void memory_block_release(struct device *dev)
{
	struct memory_block *mem = to_memory_block(dev);

	kfree(mem);
}

unsigned long __weak memory_block_size_bytes(void)
{
	return MIN_MEMORY_BLOCK_SIZE;
}
EXPORT_SYMBOL_GPL(memory_block_size_bytes);

/*
 * Show the first physical section index (number) of this memory block.
 */
static ssize_t phys_index_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct memory_block *mem = to_memory_block(dev);
	unsigned long phys_index;

	phys_index = mem->start_section_nr / sections_per_block;

	return sysfs_emit(buf, "%08lx\n", phys_index);
}

/*
 * Legacy interface that we cannot remove. Always indicate "removable"
 * with CONFIG_MEMORY_HOTREMOVE - bad heuristic.
 */
static ssize_t removable_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sysfs_emit(buf, "%d\n", (int)IS_ENABLED(CONFIG_MEMORY_HOTREMOVE));
}

/*
 * online, offline, going offline, etc.
 */
static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct memory_block *mem = to_memory_block(dev);
	const char *output;

	/*
	 * We can probably put these states in a nice little array
	 * so that they're not open-coded
	 */
	switch (mem->state) {
	case MEM_ONLINE:
		output = "online";
		break;
	case MEM_OFFLINE:
		output = "offline";
		break;
	case MEM_GOING_OFFLINE:
		output = "going-offline";
		break;
	default:
		WARN_ON(1);
		return sysfs_emit(buf, "ERROR-UNKNOWN-%ld\n", mem->state);
	}

	return sysfs_emit(buf, "%s\n", output);
}

int memory_notify(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&memory_chain, val, v);
}

static int memory_block_online(struct memory_block *mem)
{
	unsigned long start_pfn = section_nr_to_pfn(mem->start_section_nr);
	unsigned long nr_pages = PAGES_PER_SECTION * sections_per_block;
	unsigned long nr_vmemmap_pages = mem->nr_vmemmap_pages;
	struct zone *zone;
	int ret;

	zone = zone_for_pfn_range(mem->online_type, mem->nid, mem->group,
				  start_pfn, nr_pages);

	/*
	 * Although vmemmap pages have a different lifecycle than the pages
	 * they describe (they remain until the memory is unplugged), doing
	 * their initialization and accounting at memory onlining/offlining
	 * stage helps to keep accounting easier to follow - e.g vmemmaps
	 * belong to the same zone as the memory they backed.
	 */
	if (nr_vmemmap_pages) {
		ret = mhp_init_memmap_on_memory(start_pfn, nr_vmemmap_pages, zone);
		if (ret)
			return ret;
	}

	ret = online_pages(start_pfn + nr_vmemmap_pages,
			   nr_pages - nr_vmemmap_pages, zone, mem->group);
	if (ret) {
		if (nr_vmemmap_pages)
			mhp_deinit_memmap_on_memory(start_pfn, nr_vmemmap_pages);
		return ret;
	}

	/*
	 * Account once onlining succeeded. If the zone was unpopulated, it is
	 * now already properly populated.
	 */
	if (nr_vmemmap_pages)
		adjust_present_page_count(pfn_to_page(start_pfn), mem->group,
					  nr_vmemmap_pages);

	mem->zone = zone;
	return ret;
}

static int memory_block_offline(struct memory_block *mem)
{
	unsigned long start_pfn = section_nr_to_pfn(mem->start_section_nr);
	unsigned long nr_pages = PAGES_PER_SECTION * sections_per_block;
	unsigned long nr_vmemmap_pages = mem->nr_vmemmap_pages;
	int ret;

	if (!mem->zone)
		return -EINVAL;

	/*
	 * Unaccount before offlining, such that unpopulated zone and kthreads
	 * can properly be torn down in offline_pages().
	 */
	if (nr_vmemmap_pages)
		adjust_present_page_count(pfn_to_page(start_pfn), mem->group,
					  -nr_vmemmap_pages);

	ret = offline_pages(start_pfn + nr_vmemmap_pages,
			    nr_pages - nr_vmemmap_pages, mem->zone, mem->group);
	if (ret) {
		/* offline_pages() failed. Account back. */
		if (nr_vmemmap_pages)
			adjust_present_page_count(pfn_to_page(start_pfn),
						  mem->group, nr_vmemmap_pages);
		return ret;
	}

	if (nr_vmemmap_pages)
		mhp_deinit_memmap_on_memory(start_pfn, nr_vmemmap_pages);

	mem->zone = NULL;
	return ret;
}

/*
 * MEMORY_HOTPLUG depends on SPARSEMEM in mm/Kconfig, so it is
 * OK to have direct references to sparsemem variables in here.
 */
static int
memory_block_action(struct memory_block *mem, unsigned long action)
{
	int ret;

	switch (action) {
	case MEM_ONLINE:
		ret = memory_block_online(mem);
		break;
	case MEM_OFFLINE:
		ret = memory_block_offline(mem);
		break;
	default:
		WARN(1, KERN_WARNING "%s(%ld, %ld) unknown action: "
		     "%ld\n", __func__, mem->start_section_nr, action, action);
		ret = -EINVAL;
	}

	return ret;
}

static int memory_block_change_state(struct memory_block *mem,
		unsigned long to_state, unsigned long from_state_req)
{
	int ret = 0;

	if (mem->state != from_state_req)
		return -EINVAL;

	if (to_state == MEM_OFFLINE)
		mem->state = MEM_GOING_OFFLINE;

	ret = memory_block_action(mem, to_state);
	mem->state = ret ? from_state_req : to_state;

	return ret;
}

/* The device lock serializes operations on memory_subsys_[online|offline] */
static int memory_subsys_online(struct device *dev)
{
	struct memory_block *mem = to_memory_block(dev);
	int ret;

	if (mem->state == MEM_ONLINE)
		return 0;

	/*
	 * When called via device_online() without configuring the online_type,
	 * we want to default to MMOP_ONLINE.
	 */
	if (mem->online_type == MMOP_OFFLINE)
		mem->online_type = MMOP_ONLINE;

	ret = memory_block_change_state(mem, MEM_ONLINE, MEM_OFFLINE);
	mem->online_type = MMOP_OFFLINE;

	return ret;
}

static int memory_subsys_offline(struct device *dev)
{
	struct memory_block *mem = to_memory_block(dev);

	if (mem->state == MEM_OFFLINE)
		return 0;

	return memory_block_change_state(mem, MEM_OFFLINE, MEM_ONLINE);
}

static ssize_t state_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	const int online_type = mhp_online_type_from_str(buf);
	struct memory_block *mem = to_memory_block(dev);
	int ret;

	if (online_type < 0)
		return -EINVAL;

	ret = lock_device_hotplug_sysfs();
	if (ret)
		return ret;

	switch (online_type) {
	case MMOP_ONLINE_KERNEL:
	case MMOP_ONLINE_MOVABLE:
	case MMOP_ONLINE:
		/* mem->online_type is protected by device_hotplug_lock */
		mem->online_type = online_type;
		ret = device_online(&mem->dev);
		break;
	case MMOP_OFFLINE:
		ret = device_offline(&mem->dev);
		break;
	default:
		ret = -EINVAL; /* should never happen */
	}

	unlock_device_hotplug();

	if (ret < 0)
		return ret;
	if (ret)
		return -EINVAL;

	return count;
}

/*
 * Legacy interface that we cannot remove: s390x exposes the storage increment
 * covered by a memory block, allowing for identifying which memory blocks
 * comprise a storage increment. Since a memory block spans complete
 * storage increments nowadays, this interface is basically unused. Other
 * archs never exposed != 0.
 */
static ssize_t phys_device_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct memory_block *mem = to_memory_block(dev);
	unsigned long start_pfn = section_nr_to_pfn(mem->start_section_nr);

	return sysfs_emit(buf, "%d\n",
			  arch_get_memory_phys_device(start_pfn));
}

#ifdef CONFIG_MEMORY_HOTREMOVE
static int print_allowed_zone(char *buf, int len, int nid,
			      struct memory_group *group,
			      unsigned long start_pfn, unsigned long nr_pages,
			      int online_type, struct zone *default_zone)
{
	struct zone *zone;

	zone = zone_for_pfn_range(online_type, nid, group, start_pfn, nr_pages);
	if (zone == default_zone)
		return 0;

	return sysfs_emit_at(buf, len, " %s", zone->name);
}

static ssize_t valid_zones_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct memory_block *mem = to_memory_block(dev);
	unsigned long start_pfn = section_nr_to_pfn(mem->start_section_nr);
	unsigned long nr_pages = PAGES_PER_SECTION * sections_per_block;
	struct memory_group *group = mem->group;
	struct zone *default_zone;
	int nid = mem->nid;
	int len = 0;

	/*
	 * Check the existing zone. Make sure that we do that only on the
	 * online nodes otherwise the page_zone is not reliable
	 */
	if (mem->state == MEM_ONLINE) {
		/*
		 * If !mem->zone, the memory block spans multiple zones and
		 * cannot get offlined.
		 */
		default_zone = mem->zone;
		if (!default_zone)
			return sysfs_emit(buf, "%s\n", "none");
		len += sysfs_emit_at(buf, len, "%s", default_zone->name);
		goto out;
	}

	default_zone = zone_for_pfn_range(MMOP_ONLINE, nid, group,
					  start_pfn, nr_pages);

	len += sysfs_emit_at(buf, len, "%s", default_zone->name);
	len += print_allowed_zone(buf, len, nid, group, start_pfn, nr_pages,
				  MMOP_ONLINE_KERNEL, default_zone);
	len += print_allowed_zone(buf, len, nid, group, start_pfn, nr_pages,
				  MMOP_ONLINE_MOVABLE, default_zone);
out:
	len += sysfs_emit_at(buf, len, "\n");
	return len;
}
static DEVICE_ATTR_RO(valid_zones);
#endif

static DEVICE_ATTR_RO(phys_index);
static DEVICE_ATTR_RW(state);
static DEVICE_ATTR_RO(phys_device);
static DEVICE_ATTR_RO(removable);

/*
 * Show the memory block size (shared by all memory blocks).
 */
static ssize_t block_size_bytes_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", memory_block_size_bytes());
}

static DEVICE_ATTR_RO(block_size_bytes);

/*
 * Memory auto online policy.
 */

static ssize_t auto_online_blocks_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  online_type_to_str[mhp_default_online_type]);
}

static ssize_t auto_online_blocks_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	const int online_type = mhp_online_type_from_str(buf);

	if (online_type < 0)
		return -EINVAL;

	mhp_default_online_type = online_type;
	return count;
}

static DEVICE_ATTR_RW(auto_online_blocks);

/*
 * Some architectures will have custom drivers to do this, and
 * will not need to do it from userspace.  The fake hot-add code
 * as well as ppc64 will do all of their discovery in userspace
 * and will require this interface.
 */
#ifdef CONFIG_ARCH_MEMORY_PROBE
static ssize_t probe_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	u64 phys_addr;
	int nid, ret;
	unsigned long pages_per_block = PAGES_PER_SECTION * sections_per_block;

	ret = kstrtoull(buf, 0, &phys_addr);
	if (ret)
		return ret;

	if (phys_addr & ((pages_per_block << PAGE_SHIFT) - 1))
		return -EINVAL;

	ret = lock_device_hotplug_sysfs();
	if (ret)
		return ret;

	nid = memory_add_physaddr_to_nid(phys_addr);
	ret = __add_memory(nid, phys_addr,
			   MIN_MEMORY_BLOCK_SIZE * sections_per_block,
			   MHP_NONE);

	if (ret)
		goto out;

	ret = count;
out:
	unlock_device_hotplug();
	return ret;
}

static DEVICE_ATTR_WO(probe);
#endif

#ifdef CONFIG_MEMORY_FAILURE
/*
 * Support for offlining pages of memory
 */

/* Soft offline a page */
static ssize_t soft_offline_page_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int ret;
	u64 pfn;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (kstrtoull(buf, 0, &pfn) < 0)
		return -EINVAL;
	pfn >>= PAGE_SHIFT;
	ret = soft_offline_page(pfn, 0);
	return ret == 0 ? count : ret;
}

/* Forcibly offline a page, including killing processes. */
static ssize_t hard_offline_page_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int ret;
	u64 pfn;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (kstrtoull(buf, 0, &pfn) < 0)
		return -EINVAL;
	pfn >>= PAGE_SHIFT;
	ret = memory_failure(pfn, 0);
	if (ret == -EOPNOTSUPP)
		ret = 0;
	return ret ? ret : count;
}

static DEVICE_ATTR_WO(soft_offline_page);
static DEVICE_ATTR_WO(hard_offline_page);
#endif

/* See phys_device_show(). */
int __weak arch_get_memory_phys_device(unsigned long start_pfn)
{
	return 0;
}

/*
 * A reference for the returned memory block device is acquired.
 *
 * Called under device_hotplug_lock.
 */
static struct memory_block *find_memory_block_by_id(unsigned long block_id)
{
	struct memory_block *mem;

	mem = xa_load(&memory_blocks, block_id);
	if (mem)
		get_device(&mem->dev);
	return mem;
}

/*
 * Called under device_hotplug_lock.
 */
struct memory_block *find_memory_block(unsigned long section_nr)
{
	unsigned long block_id = memory_block_id(section_nr);

	return find_memory_block_by_id(block_id);
}

static struct attribute *memory_memblk_attrs[] = {
	&dev_attr_phys_index.attr,
	&dev_attr_state.attr,
	&dev_attr_phys_device.attr,
	&dev_attr_removable.attr,
#ifdef CONFIG_MEMORY_HOTREMOVE
	&dev_attr_valid_zones.attr,
#endif
	NULL
};

static const struct attribute_group memory_memblk_attr_group = {
	.attrs = memory_memblk_attrs,
};

static const struct attribute_group *memory_memblk_attr_groups[] = {
	&memory_memblk_attr_group,
	NULL,
};

static int __add_memory_block(struct memory_block *memory)
{
	int ret;

	memory->dev.bus = &memory_subsys;
	memory->dev.id = memory->start_section_nr / sections_per_block;
	memory->dev.release = memory_block_release;
	memory->dev.groups = memory_memblk_attr_groups;
	memory->dev.offline = memory->state == MEM_OFFLINE;

	ret = device_register(&memory->dev);
	if (ret) {
		put_device(&memory->dev);
		return ret;
	}
	ret = xa_err(xa_store(&memory_blocks, memory->dev.id, memory,
			      GFP_KERNEL));
	if (ret)
		device_unregister(&memory->dev);

	return ret;
}

static struct zone *early_node_zone_for_memory_block(struct memory_block *mem,
						     int nid)
{
	const unsigned long start_pfn = section_nr_to_pfn(mem->start_section_nr);
	const unsigned long nr_pages = PAGES_PER_SECTION * sections_per_block;
	struct zone *zone, *matching_zone = NULL;
	pg_data_t *pgdat = NODE_DATA(nid);
	int i;

	/*
	 * This logic only works for early memory, when the applicable zones
	 * already span the memory block. We don't expect overlapping zones on
	 * a single node for early memory. So if we're told that some PFNs
	 * of a node fall into this memory block, we can assume that all node
	 * zones that intersect with the memory block are actually applicable.
	 * No need to look at the memmap.
	 */
	for (i = 0; i < MAX_NR_ZONES; i++) {
		zone = pgdat->node_zones + i;
		if (!populated_zone(zone))
			continue;
		if (!zone_intersects(zone, start_pfn, nr_pages))
			continue;
		if (!matching_zone) {
			matching_zone = zone;
			continue;
		}
		/* Spans multiple zones ... */
		matching_zone = NULL;
		break;
	}
	return matching_zone;
}

#ifdef CONFIG_NUMA
/**
 * memory_block_add_nid() - Indicate that system RAM falling into this memory
 *			    block device (partially) belongs to the given node.
 * @mem: The memory block device.
 * @nid: The node id.
 * @context: The memory initialization context.
 *
 * Indicate that system RAM falling into this memory block (partially) belongs
 * to the given node. If the context indicates ("early") that we are adding the
 * node during node device subsystem initialization, this will also properly
 * set/adjust mem->zone based on the zone ranges of the given node.
 */
void memory_block_add_nid(struct memory_block *mem, int nid,
			  enum meminit_context context)
{
	if (context == MEMINIT_EARLY && mem->nid != nid) {
		/*
		 * For early memory we have to determine the zone when setting
		 * the node id and handle multiple nodes spanning a single
		 * memory block by indicate via zone == NULL that we're not
		 * dealing with a single zone. So if we're setting the node id
		 * the first time, determine if there is a single zone. If we're
		 * setting the node id a second time to a different node,
		 * invalidate the single detected zone.
		 */
		if (mem->nid == NUMA_NO_NODE)
			mem->zone = early_node_zone_for_memory_block(mem, nid);
		else
			mem->zone = NULL;
	}

	/*
	 * If this memory block spans multiple nodes, we only indicate
	 * the last processed node. If we span multiple nodes (not applicable
	 * to hotplugged memory), zone == NULL will prohibit memory offlining
	 * and consequently unplug.
	 */
	mem->nid = nid;
}
#endif

static int add_memory_block(unsigned long block_id, unsigned long state,
			    unsigned long nr_vmemmap_pages,
			    struct memory_group *group)
{
	struct memory_block *mem;
	int ret = 0;

	mem = find_memory_block_by_id(block_id);
	if (mem) {
		put_device(&mem->dev);
		return -EEXIST;
	}
	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	mem->start_section_nr = block_id * sections_per_block;
	mem->state = state;
	mem->nid = NUMA_NO_NODE;
	mem->nr_vmemmap_pages = nr_vmemmap_pages;
	INIT_LIST_HEAD(&mem->group_next);

#ifndef CONFIG_NUMA
	if (state == MEM_ONLINE)
		/*
		 * MEM_ONLINE at this point implies early memory. With NUMA,
		 * we'll determine the zone when setting the node id via
		 * memory_block_add_nid(). Memory hotplug updated the zone
		 * manually when memory onlining/offlining succeeds.
		 */
		mem->zone = early_node_zone_for_memory_block(mem, NUMA_NO_NODE);
#endif /* CONFIG_NUMA */

	ret = __add_memory_block(mem);
	if (ret)
		return ret;

	if (group) {
		mem->group = group;
		list_add(&mem->group_next, &group->memory_blocks);
	}

	return 0;
}

static int __init add_boot_memory_block(unsigned long base_section_nr)
{
	int section_count = 0;
	unsigned long nr;

	for (nr = base_section_nr; nr < base_section_nr + sections_per_block;
	     nr++)
		if (present_section_nr(nr))
			section_count++;

	if (section_count == 0)
		return 0;
	return add_memory_block(memory_block_id(base_section_nr),
				MEM_ONLINE, 0,  NULL);
}

static int add_hotplug_memory_block(unsigned long block_id,
				    unsigned long nr_vmemmap_pages,
				    struct memory_group *group)
{
	return add_memory_block(block_id, MEM_OFFLINE, nr_vmemmap_pages, group);
}

static void remove_memory_block(struct memory_block *memory)
{
	if (WARN_ON_ONCE(memory->dev.bus != &memory_subsys))
		return;

	WARN_ON(xa_erase(&memory_blocks, memory->dev.id) == NULL);

	if (memory->group) {
		list_del(&memory->group_next);
		memory->group = NULL;
	}

	/* drop the ref. we got via find_memory_block() */
	put_device(&memory->dev);
	device_unregister(&memory->dev);
}

/*
 * Create memory block devices for the given memory area. Start and size
 * have to be aligned to memory block granularity. Memory block devices
 * will be initialized as offline.
 *
 * Called under device_hotplug_lock.
 */
int create_memory_block_devices(unsigned long start, unsigned long size,
				unsigned long vmemmap_pages,
				struct memory_group *group)
{
	const unsigned long start_block_id = pfn_to_block_id(PFN_DOWN(start));
	unsigned long end_block_id = pfn_to_block_id(PFN_DOWN(start + size));
	struct memory_block *mem;
	unsigned long block_id;
	int ret = 0;

	if (WARN_ON_ONCE(!IS_ALIGNED(start, memory_block_size_bytes()) ||
			 !IS_ALIGNED(size, memory_block_size_bytes())))
		return -EINVAL;

	for (block_id = start_block_id; block_id != end_block_id; block_id++) {
		ret = add_hotplug_memory_block(block_id, vmemmap_pages, group);
		if (ret)
			break;
	}
	if (ret) {
		end_block_id = block_id;
		for (block_id = start_block_id; block_id != end_block_id;
		     block_id++) {
			mem = find_memory_block_by_id(block_id);
			if (WARN_ON_ONCE(!mem))
				continue;
			remove_memory_block(mem);
		}
	}
	return ret;
}

/*
 * Remove memory block devices for the given memory area. Start and size
 * have to be aligned to memory block granularity. Memory block devices
 * have to be offline.
 *
 * Called under device_hotplug_lock.
 */
void remove_memory_block_devices(unsigned long start, unsigned long size)
{
	const unsigned long start_block_id = pfn_to_block_id(PFN_DOWN(start));
	const unsigned long end_block_id = pfn_to_block_id(PFN_DOWN(start + size));
	struct memory_block *mem;
	unsigned long block_id;

	if (WARN_ON_ONCE(!IS_ALIGNED(start, memory_block_size_bytes()) ||
			 !IS_ALIGNED(size, memory_block_size_bytes())))
		return;

	for (block_id = start_block_id; block_id != end_block_id; block_id++) {
		mem = find_memory_block_by_id(block_id);
		if (WARN_ON_ONCE(!mem))
			continue;
		unregister_memory_block_under_nodes(mem);
		remove_memory_block(mem);
	}
}

/* return true if the memory block is offlined, otherwise, return false */
bool is_memblock_offlined(struct memory_block *mem)
{
	return mem->state == MEM_OFFLINE;
}

static struct attribute *memory_root_attrs[] = {
#ifdef CONFIG_ARCH_MEMORY_PROBE
	&dev_attr_probe.attr,
#endif

#ifdef CONFIG_MEMORY_FAILURE
	&dev_attr_soft_offline_page.attr,
	&dev_attr_hard_offline_page.attr,
#endif

	&dev_attr_block_size_bytes.attr,
	&dev_attr_auto_online_blocks.attr,
	NULL
};

static const struct attribute_group memory_root_attr_group = {
	.attrs = memory_root_attrs,
};

static const struct attribute_group *memory_root_attr_groups[] = {
	&memory_root_attr_group,
	NULL,
};

/*
 * Initialize the sysfs support for memory devices. At the time this function
 * is called, we cannot have concurrent creation/deletion of memory block
 * devices, the device_hotplug_lock is not needed.
 */
void __init memory_dev_init(void)
{
	int ret;
	unsigned long block_sz, nr;

	/* Validate the configured memory block size */
	block_sz = memory_block_size_bytes();
	if (!is_power_of_2(block_sz) || block_sz < MIN_MEMORY_BLOCK_SIZE)
		panic("Memory block size not suitable: 0x%lx\n", block_sz);
	sections_per_block = block_sz / MIN_MEMORY_BLOCK_SIZE;

	ret = subsys_system_register(&memory_subsys, memory_root_attr_groups);
	if (ret)
		panic("%s() failed to register subsystem: %d\n", __func__, ret);

	/*
	 * Create entries for memory sections that were found
	 * during boot and have been initialized
	 */
	for (nr = 0; nr <= __highest_present_section_nr;
	     nr += sections_per_block) {
		ret = add_boot_memory_block(nr);
		if (ret)
			panic("%s() failed to add memory block: %d\n", __func__,
			      ret);
	}
}

/**
 * walk_memory_blocks - walk through all present memory blocks overlapped
 *			by the range [start, start + size)
 *
 * @start: start address of the memory range
 * @size: size of the memory range
 * @arg: argument passed to func
 * @func: callback for each memory section walked
 *
 * This function walks through all present memory blocks overlapped by the
 * range [start, start + size), calling func on each memory block.
 *
 * In case func() returns an error, walking is aborted and the error is
 * returned.
 *
 * Called under device_hotplug_lock.
 */
int walk_memory_blocks(unsigned long start, unsigned long size,
		       void *arg, walk_memory_blocks_func_t func)
{
	const unsigned long start_block_id = phys_to_block_id(start);
	const unsigned long end_block_id = phys_to_block_id(start + size - 1);
	struct memory_block *mem;
	unsigned long block_id;
	int ret = 0;

	if (!size)
		return 0;

	for (block_id = start_block_id; block_id <= end_block_id; block_id++) {
		mem = find_memory_block_by_id(block_id);
		if (!mem)
			continue;

		ret = func(mem, arg);
		put_device(&mem->dev);
		if (ret)
			break;
	}
	return ret;
}

struct for_each_memory_block_cb_data {
	walk_memory_blocks_func_t func;
	void *arg;
};

static int for_each_memory_block_cb(struct device *dev, void *data)
{
	struct memory_block *mem = to_memory_block(dev);
	struct for_each_memory_block_cb_data *cb_data = data;

	return cb_data->func(mem, cb_data->arg);
}

/**
 * for_each_memory_block - walk through all present memory blocks
 *
 * @arg: argument passed to func
 * @func: callback for each memory block walked
 *
 * This function walks through all present memory blocks, calling func on
 * each memory block.
 *
 * In case func() returns an error, walking is aborted and the error is
 * returned.
 */
int for_each_memory_block(void *arg, walk_memory_blocks_func_t func)
{
	struct for_each_memory_block_cb_data cb_data = {
		.func = func,
		.arg = arg,
	};

	return bus_for_each_dev(&memory_subsys, NULL, &cb_data,
				for_each_memory_block_cb);
}

/*
 * This is an internal helper to unify allocation and initialization of
 * memory groups. Note that the passed memory group will be copied to a
 * dynamically allocated memory group. After this call, the passed
 * memory group should no longer be used.
 */
static int memory_group_register(struct memory_group group)
{
	struct memory_group *new_group;
	uint32_t mgid;
	int ret;

	if (!node_possible(group.nid))
		return -EINVAL;

	new_group = kzalloc(sizeof(group), GFP_KERNEL);
	if (!new_group)
		return -ENOMEM;
	*new_group = group;
	INIT_LIST_HEAD(&new_group->memory_blocks);

	ret = xa_alloc(&memory_groups, &mgid, new_group, xa_limit_31b,
		       GFP_KERNEL);
	if (ret) {
		kfree(new_group);
		return ret;
	} else if (group.is_dynamic) {
		xa_set_mark(&memory_groups, mgid, MEMORY_GROUP_MARK_DYNAMIC);
	}
	return mgid;
}

/**
 * memory_group_register_static() - Register a static memory group.
 * @nid: The node id.
 * @max_pages: The maximum number of pages we'll have in this static memory
 *	       group.
 *
 * Register a new static memory group and return the memory group id.
 * All memory in the group belongs to a single unit, such as a DIMM. All
 * memory belonging to a static memory group is added in one go to be removed
 * in one go -- it's static.
 *
 * Returns an error if out of memory, if the node id is invalid, if no new
 * memory groups can be registered, or if max_pages is invalid (0). Otherwise,
 * returns the new memory group id.
 */
int memory_group_register_static(int nid, unsigned long max_pages)
{
	struct memory_group group = {
		.nid = nid,
		.s = {
			.max_pages = max_pages,
		},
	};

	if (!max_pages)
		return -EINVAL;
	return memory_group_register(group);
}
EXPORT_SYMBOL_GPL(memory_group_register_static);

/**
 * memory_group_register_dynamic() - Register a dynamic memory group.
 * @nid: The node id.
 * @unit_pages: Unit in pages in which is memory added/removed in this dynamic
 *		memory group.
 *
 * Register a new dynamic memory group and return the memory group id.
 * Memory within a dynamic memory group is added/removed dynamically
 * in unit_pages.
 *
 * Returns an error if out of memory, if the node id is invalid, if no new
 * memory groups can be registered, or if unit_pages is invalid (0, not a
 * power of two, smaller than a single memory block). Otherwise, returns the
 * new memory group id.
 */
int memory_group_register_dynamic(int nid, unsigned long unit_pages)
{
	struct memory_group group = {
		.nid = nid,
		.is_dynamic = true,
		.d = {
			.unit_pages = unit_pages,
		},
	};

	if (!unit_pages || !is_power_of_2(unit_pages) ||
	    unit_pages < PHYS_PFN(memory_block_size_bytes()))
		return -EINVAL;
	return memory_group_register(group);
}
EXPORT_SYMBOL_GPL(memory_group_register_dynamic);

/**
 * memory_group_unregister() - Unregister a memory group.
 * @mgid: the memory group id
 *
 * Unregister a memory group. If any memory block still belongs to this
 * memory group, unregistering will fail.
 *
 * Returns -EINVAL if the memory group id is invalid, returns -EBUSY if some
 * memory blocks still belong to this memory group and returns 0 if
 * unregistering succeeded.
 */
int memory_group_unregister(int mgid)
{
	struct memory_group *group;

	if (mgid < 0)
		return -EINVAL;

	group = xa_load(&memory_groups, mgid);
	if (!group)
		return -EINVAL;
	if (!list_empty(&group->memory_blocks))
		return -EBUSY;
	xa_erase(&memory_groups, mgid);
	kfree(group);
	return 0;
}
EXPORT_SYMBOL_GPL(memory_group_unregister);

/*
 * This is an internal helper only to be used in core memory hotplug code to
 * lookup a memory group. We don't care about locking, as we don't expect a
 * memory group to get unregistered while adding memory to it -- because
 * the group and the memory is managed by the same driver.
 */
struct memory_group *memory_group_find_by_id(int mgid)
{
	return xa_load(&memory_groups, mgid);
}

/*
 * This is an internal helper only to be used in core memory hotplug code to
 * walk all dynamic memory groups excluding a given memory group, either
 * belonging to a specific node, or belonging to any node.
 */
int walk_dynamic_memory_groups(int nid, walk_memory_groups_func_t func,
			       struct memory_group *excluded, void *arg)
{
	struct memory_group *group;
	unsigned long index;
	int ret = 0;

	xa_for_each_marked(&memory_groups, index, group,
			   MEMORY_GROUP_MARK_DYNAMIC) {
		if (group == excluded)
			continue;
#ifdef CONFIG_NUMA
		if (nid != NUMA_NO_NODE && group->nid != nid)
			continue;
#endif /* CONFIG_NUMA */
		ret = func(group, arg);
		if (ret)
			break;
	}
	return ret;
}
