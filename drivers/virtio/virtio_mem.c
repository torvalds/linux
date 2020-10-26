// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtio-mem device driver.
 *
 * Copyright Red Hat, Inc. 2020
 *
 * Author(s): David Hildenbrand <david@redhat.com>
 */

#include <linux/virtio.h>
#include <linux/virtio_mem.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/memory_hotplug.h>
#include <linux/memory.h>
#include <linux/hrtimer.h>
#include <linux/crash_dump.h>
#include <linux/mutex.h>
#include <linux/bitmap.h>
#include <linux/lockdep.h>

#include <acpi/acpi_numa.h>

static bool unplug_online = true;
module_param(unplug_online, bool, 0644);
MODULE_PARM_DESC(unplug_online, "Try to unplug online memory");

enum virtio_mem_mb_state {
	/* Unplugged, not added to Linux. Can be reused later. */
	VIRTIO_MEM_MB_STATE_UNUSED = 0,
	/* (Partially) plugged, not added to Linux. Error on add_memory(). */
	VIRTIO_MEM_MB_STATE_PLUGGED,
	/* Fully plugged, fully added to Linux, offline. */
	VIRTIO_MEM_MB_STATE_OFFLINE,
	/* Partially plugged, fully added to Linux, offline. */
	VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL,
	/* Fully plugged, fully added to Linux, online. */
	VIRTIO_MEM_MB_STATE_ONLINE,
	/* Partially plugged, fully added to Linux, online. */
	VIRTIO_MEM_MB_STATE_ONLINE_PARTIAL,
	VIRTIO_MEM_MB_STATE_COUNT
};

struct virtio_mem {
	struct virtio_device *vdev;

	/* We might first have to unplug all memory when starting up. */
	bool unplug_all_required;

	/* Workqueue that processes the plug/unplug requests. */
	struct work_struct wq;
	atomic_t config_changed;

	/* Virtqueue for guest->host requests. */
	struct virtqueue *vq;

	/* Wait for a host response to a guest request. */
	wait_queue_head_t host_resp;

	/* Space for one guest request and the host response. */
	struct virtio_mem_req req;
	struct virtio_mem_resp resp;

	/* The current size of the device. */
	uint64_t plugged_size;
	/* The requested size of the device. */
	uint64_t requested_size;

	/* The device block size (for communicating with the device). */
	uint64_t device_block_size;
	/* The translated node id. NUMA_NO_NODE in case not specified. */
	int nid;
	/* Physical start address of the memory region. */
	uint64_t addr;
	/* Maximum region size in bytes. */
	uint64_t region_size;

	/* The subblock size. */
	uint64_t subblock_size;
	/* The number of subblocks per memory block. */
	uint32_t nb_sb_per_mb;

	/* Id of the first memory block of this device. */
	unsigned long first_mb_id;
	/* Id of the last memory block of this device. */
	unsigned long last_mb_id;
	/* Id of the last usable memory block of this device. */
	unsigned long last_usable_mb_id;
	/* Id of the next memory bock to prepare when needed. */
	unsigned long next_mb_id;

	/* The parent resource for all memory added via this device. */
	struct resource *parent_resource;
	/*
	 * Copy of "System RAM (virtio_mem)" to be used for
	 * add_memory_driver_managed().
	 */
	const char *resource_name;

	/* Summary of all memory block states. */
	unsigned long nb_mb_state[VIRTIO_MEM_MB_STATE_COUNT];
#define VIRTIO_MEM_NB_OFFLINE_THRESHOLD		10

	/*
	 * One byte state per memory block.
	 *
	 * Allocated via vmalloc(). When preparing new blocks, resized
	 * (alloc+copy+free) when needed (crossing pages with the next mb).
	 * (when crossing pages).
	 *
	 * With 128MB memory blocks, we have states for 512GB of memory in one
	 * page.
	 */
	uint8_t *mb_state;

	/*
	 * $nb_sb_per_mb bit per memory block. Handled similar to mb_state.
	 *
	 * With 4MB subblocks, we manage 128GB of memory in one page.
	 */
	unsigned long *sb_bitmap;

	/*
	 * Mutex that protects the nb_mb_state, mb_state, and sb_bitmap.
	 *
	 * When this lock is held the pointers can't change, ONLINE and
	 * OFFLINE blocks can't change the state and no subblocks will get
	 * plugged/unplugged.
	 */
	struct mutex hotplug_mutex;
	bool hotplug_active;

	/* An error occurred we cannot handle - stop processing requests. */
	bool broken;

	/* The driver is being removed. */
	spinlock_t removal_lock;
	bool removing;

	/* Timer for retrying to plug/unplug memory. */
	struct hrtimer retry_timer;
	unsigned int retry_timer_ms;
#define VIRTIO_MEM_RETRY_TIMER_MIN_MS		50000
#define VIRTIO_MEM_RETRY_TIMER_MAX_MS		300000

	/* Memory notifier (online/offline events). */
	struct notifier_block memory_notifier;

	/* Next device in the list of virtio-mem devices. */
	struct list_head next;
};

/*
 * We have to share a single online_page callback among all virtio-mem
 * devices. We use RCU to iterate the list in the callback.
 */
static DEFINE_MUTEX(virtio_mem_mutex);
static LIST_HEAD(virtio_mem_devices);

static void virtio_mem_online_page_cb(struct page *page, unsigned int order);

/*
 * Register a virtio-mem device so it will be considered for the online_page
 * callback.
 */
static int register_virtio_mem_device(struct virtio_mem *vm)
{
	int rc = 0;

	/* First device registers the callback. */
	mutex_lock(&virtio_mem_mutex);
	if (list_empty(&virtio_mem_devices))
		rc = set_online_page_callback(&virtio_mem_online_page_cb);
	if (!rc)
		list_add_rcu(&vm->next, &virtio_mem_devices);
	mutex_unlock(&virtio_mem_mutex);

	return rc;
}

/*
 * Unregister a virtio-mem device so it will no longer be considered for the
 * online_page callback.
 */
static void unregister_virtio_mem_device(struct virtio_mem *vm)
{
	/* Last device unregisters the callback. */
	mutex_lock(&virtio_mem_mutex);
	list_del_rcu(&vm->next);
	if (list_empty(&virtio_mem_devices))
		restore_online_page_callback(&virtio_mem_online_page_cb);
	mutex_unlock(&virtio_mem_mutex);

	synchronize_rcu();
}

/*
 * Calculate the memory block id of a given address.
 */
static unsigned long virtio_mem_phys_to_mb_id(unsigned long addr)
{
	return addr / memory_block_size_bytes();
}

/*
 * Calculate the physical start address of a given memory block id.
 */
static unsigned long virtio_mem_mb_id_to_phys(unsigned long mb_id)
{
	return mb_id * memory_block_size_bytes();
}

/*
 * Calculate the subblock id of a given address.
 */
static unsigned long virtio_mem_phys_to_sb_id(struct virtio_mem *vm,
					      unsigned long addr)
{
	const unsigned long mb_id = virtio_mem_phys_to_mb_id(addr);
	const unsigned long mb_addr = virtio_mem_mb_id_to_phys(mb_id);

	return (addr - mb_addr) / vm->subblock_size;
}

/*
 * Set the state of a memory block, taking care of the state counter.
 */
static void virtio_mem_mb_set_state(struct virtio_mem *vm, unsigned long mb_id,
				    enum virtio_mem_mb_state state)
{
	const unsigned long idx = mb_id - vm->first_mb_id;
	enum virtio_mem_mb_state old_state;

	old_state = vm->mb_state[idx];
	vm->mb_state[idx] = state;

	BUG_ON(vm->nb_mb_state[old_state] == 0);
	vm->nb_mb_state[old_state]--;
	vm->nb_mb_state[state]++;
}

/*
 * Get the state of a memory block.
 */
static enum virtio_mem_mb_state virtio_mem_mb_get_state(struct virtio_mem *vm,
							unsigned long mb_id)
{
	const unsigned long idx = mb_id - vm->first_mb_id;

	return vm->mb_state[idx];
}

/*
 * Prepare the state array for the next memory block.
 */
static int virtio_mem_mb_state_prepare_next_mb(struct virtio_mem *vm)
{
	unsigned long old_bytes = vm->next_mb_id - vm->first_mb_id + 1;
	unsigned long new_bytes = vm->next_mb_id - vm->first_mb_id + 2;
	int old_pages = PFN_UP(old_bytes);
	int new_pages = PFN_UP(new_bytes);
	uint8_t *new_mb_state;

	if (vm->mb_state && old_pages == new_pages)
		return 0;

	new_mb_state = vzalloc(new_pages * PAGE_SIZE);
	if (!new_mb_state)
		return -ENOMEM;

	mutex_lock(&vm->hotplug_mutex);
	if (vm->mb_state)
		memcpy(new_mb_state, vm->mb_state, old_pages * PAGE_SIZE);
	vfree(vm->mb_state);
	vm->mb_state = new_mb_state;
	mutex_unlock(&vm->hotplug_mutex);

	return 0;
}

#define virtio_mem_for_each_mb_state(_vm, _mb_id, _state) \
	for (_mb_id = _vm->first_mb_id; \
	     _mb_id < _vm->next_mb_id && _vm->nb_mb_state[_state]; \
	     _mb_id++) \
		if (virtio_mem_mb_get_state(_vm, _mb_id) == _state)

#define virtio_mem_for_each_mb_state_rev(_vm, _mb_id, _state) \
	for (_mb_id = _vm->next_mb_id - 1; \
	     _mb_id >= _vm->first_mb_id && _vm->nb_mb_state[_state]; \
	     _mb_id--) \
		if (virtio_mem_mb_get_state(_vm, _mb_id) == _state)

/*
 * Mark all selected subblocks plugged.
 *
 * Will not modify the state of the memory block.
 */
static void virtio_mem_mb_set_sb_plugged(struct virtio_mem *vm,
					 unsigned long mb_id, int sb_id,
					 int count)
{
	const int bit = (mb_id - vm->first_mb_id) * vm->nb_sb_per_mb + sb_id;

	__bitmap_set(vm->sb_bitmap, bit, count);
}

/*
 * Mark all selected subblocks unplugged.
 *
 * Will not modify the state of the memory block.
 */
static void virtio_mem_mb_set_sb_unplugged(struct virtio_mem *vm,
					   unsigned long mb_id, int sb_id,
					   int count)
{
	const int bit = (mb_id - vm->first_mb_id) * vm->nb_sb_per_mb + sb_id;

	__bitmap_clear(vm->sb_bitmap, bit, count);
}

/*
 * Test if all selected subblocks are plugged.
 */
static bool virtio_mem_mb_test_sb_plugged(struct virtio_mem *vm,
					  unsigned long mb_id, int sb_id,
					  int count)
{
	const int bit = (mb_id - vm->first_mb_id) * vm->nb_sb_per_mb + sb_id;

	if (count == 1)
		return test_bit(bit, vm->sb_bitmap);

	/* TODO: Helper similar to bitmap_set() */
	return find_next_zero_bit(vm->sb_bitmap, bit + count, bit) >=
	       bit + count;
}

/*
 * Test if all selected subblocks are unplugged.
 */
static bool virtio_mem_mb_test_sb_unplugged(struct virtio_mem *vm,
					    unsigned long mb_id, int sb_id,
					    int count)
{
	const int bit = (mb_id - vm->first_mb_id) * vm->nb_sb_per_mb + sb_id;

	/* TODO: Helper similar to bitmap_set() */
	return find_next_bit(vm->sb_bitmap, bit + count, bit) >= bit + count;
}

/*
 * Find the first unplugged subblock. Returns vm->nb_sb_per_mb in case there is
 * none.
 */
static int virtio_mem_mb_first_unplugged_sb(struct virtio_mem *vm,
					    unsigned long mb_id)
{
	const int bit = (mb_id - vm->first_mb_id) * vm->nb_sb_per_mb;

	return find_next_zero_bit(vm->sb_bitmap, bit + vm->nb_sb_per_mb, bit) -
	       bit;
}

/*
 * Prepare the subblock bitmap for the next memory block.
 */
static int virtio_mem_sb_bitmap_prepare_next_mb(struct virtio_mem *vm)
{
	const unsigned long old_nb_mb = vm->next_mb_id - vm->first_mb_id;
	const unsigned long old_nb_bits = old_nb_mb * vm->nb_sb_per_mb;
	const unsigned long new_nb_bits = (old_nb_mb + 1) * vm->nb_sb_per_mb;
	int old_pages = PFN_UP(BITS_TO_LONGS(old_nb_bits) * sizeof(long));
	int new_pages = PFN_UP(BITS_TO_LONGS(new_nb_bits) * sizeof(long));
	unsigned long *new_sb_bitmap, *old_sb_bitmap;

	if (vm->sb_bitmap && old_pages == new_pages)
		return 0;

	new_sb_bitmap = vzalloc(new_pages * PAGE_SIZE);
	if (!new_sb_bitmap)
		return -ENOMEM;

	mutex_lock(&vm->hotplug_mutex);
	if (new_sb_bitmap)
		memcpy(new_sb_bitmap, vm->sb_bitmap, old_pages * PAGE_SIZE);

	old_sb_bitmap = vm->sb_bitmap;
	vm->sb_bitmap = new_sb_bitmap;
	mutex_unlock(&vm->hotplug_mutex);

	vfree(old_sb_bitmap);
	return 0;
}

/*
 * Try to add a memory block to Linux. This will usually only fail
 * if out of memory.
 *
 * Must not be called with the vm->hotplug_mutex held (possible deadlock with
 * onlining code).
 *
 * Will not modify the state of the memory block.
 */
static int virtio_mem_mb_add(struct virtio_mem *vm, unsigned long mb_id)
{
	const uint64_t addr = virtio_mem_mb_id_to_phys(mb_id);
	int nid = vm->nid;

	if (nid == NUMA_NO_NODE)
		nid = memory_add_physaddr_to_nid(addr);

	/*
	 * When force-unloading the driver and we still have memory added to
	 * Linux, the resource name has to stay.
	 */
	if (!vm->resource_name) {
		vm->resource_name = kstrdup_const("System RAM (virtio_mem)",
						  GFP_KERNEL);
		if (!vm->resource_name)
			return -ENOMEM;
	}

	dev_dbg(&vm->vdev->dev, "adding memory block: %lu\n", mb_id);
	return add_memory_driver_managed(nid, addr, memory_block_size_bytes(),
					 vm->resource_name,
					 MEMHP_MERGE_RESOURCE);
}

/*
 * Try to remove a memory block from Linux. Will only fail if the memory block
 * is not offline.
 *
 * Must not be called with the vm->hotplug_mutex held (possible deadlock with
 * onlining code).
 *
 * Will not modify the state of the memory block.
 */
static int virtio_mem_mb_remove(struct virtio_mem *vm, unsigned long mb_id)
{
	const uint64_t addr = virtio_mem_mb_id_to_phys(mb_id);
	int nid = vm->nid;

	if (nid == NUMA_NO_NODE)
		nid = memory_add_physaddr_to_nid(addr);

	dev_dbg(&vm->vdev->dev, "removing memory block: %lu\n", mb_id);
	return remove_memory(nid, addr, memory_block_size_bytes());
}

/*
 * Try to offline and remove a memory block from Linux.
 *
 * Must not be called with the vm->hotplug_mutex held (possible deadlock with
 * onlining code).
 *
 * Will not modify the state of the memory block.
 */
static int virtio_mem_mb_offline_and_remove(struct virtio_mem *vm,
					    unsigned long mb_id)
{
	const uint64_t addr = virtio_mem_mb_id_to_phys(mb_id);
	int nid = vm->nid;

	if (nid == NUMA_NO_NODE)
		nid = memory_add_physaddr_to_nid(addr);

	dev_dbg(&vm->vdev->dev, "offlining and removing memory block: %lu\n",
		mb_id);
	return offline_and_remove_memory(nid, addr, memory_block_size_bytes());
}

/*
 * Trigger the workqueue so the device can perform its magic.
 */
static void virtio_mem_retry(struct virtio_mem *vm)
{
	unsigned long flags;

	spin_lock_irqsave(&vm->removal_lock, flags);
	if (!vm->removing)
		queue_work(system_freezable_wq, &vm->wq);
	spin_unlock_irqrestore(&vm->removal_lock, flags);
}

static int virtio_mem_translate_node_id(struct virtio_mem *vm, uint16_t node_id)
{
	int node = NUMA_NO_NODE;

#if defined(CONFIG_ACPI_NUMA)
	if (virtio_has_feature(vm->vdev, VIRTIO_MEM_F_ACPI_PXM))
		node = pxm_to_node(node_id);
#endif
	return node;
}

/*
 * Test if a virtio-mem device overlaps with the given range. Can be called
 * from (notifier) callbacks lockless.
 */
static bool virtio_mem_overlaps_range(struct virtio_mem *vm,
				      unsigned long start, unsigned long size)
{
	unsigned long dev_start = virtio_mem_mb_id_to_phys(vm->first_mb_id);
	unsigned long dev_end = virtio_mem_mb_id_to_phys(vm->last_mb_id) +
				memory_block_size_bytes();

	return start < dev_end && dev_start < start + size;
}

/*
 * Test if a virtio-mem device owns a memory block. Can be called from
 * (notifier) callbacks lockless.
 */
static bool virtio_mem_owned_mb(struct virtio_mem *vm, unsigned long mb_id)
{
	return mb_id >= vm->first_mb_id && mb_id <= vm->last_mb_id;
}

static int virtio_mem_notify_going_online(struct virtio_mem *vm,
					  unsigned long mb_id)
{
	switch (virtio_mem_mb_get_state(vm, mb_id)) {
	case VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL:
	case VIRTIO_MEM_MB_STATE_OFFLINE:
		return NOTIFY_OK;
	default:
		break;
	}
	dev_warn_ratelimited(&vm->vdev->dev,
			     "memory block onlining denied\n");
	return NOTIFY_BAD;
}

static void virtio_mem_notify_offline(struct virtio_mem *vm,
				      unsigned long mb_id)
{
	switch (virtio_mem_mb_get_state(vm, mb_id)) {
	case VIRTIO_MEM_MB_STATE_ONLINE_PARTIAL:
		virtio_mem_mb_set_state(vm, mb_id,
					VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL);
		break;
	case VIRTIO_MEM_MB_STATE_ONLINE:
		virtio_mem_mb_set_state(vm, mb_id,
					VIRTIO_MEM_MB_STATE_OFFLINE);
		break;
	default:
		BUG();
		break;
	}

	/*
	 * Trigger the workqueue, maybe we can now unplug memory. Also,
	 * when we offline and remove a memory block, this will re-trigger
	 * us immediately - which is often nice because the removal of
	 * the memory block (e.g., memmap) might have freed up memory
	 * on other memory blocks we manage.
	 */
	virtio_mem_retry(vm);
}

static void virtio_mem_notify_online(struct virtio_mem *vm, unsigned long mb_id)
{
	unsigned long nb_offline;

	switch (virtio_mem_mb_get_state(vm, mb_id)) {
	case VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL:
		virtio_mem_mb_set_state(vm, mb_id,
					VIRTIO_MEM_MB_STATE_ONLINE_PARTIAL);
		break;
	case VIRTIO_MEM_MB_STATE_OFFLINE:
		virtio_mem_mb_set_state(vm, mb_id, VIRTIO_MEM_MB_STATE_ONLINE);
		break;
	default:
		BUG();
		break;
	}
	nb_offline = vm->nb_mb_state[VIRTIO_MEM_MB_STATE_OFFLINE] +
		     vm->nb_mb_state[VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL];

	/* see if we can add new blocks now that we onlined one block */
	if (nb_offline == VIRTIO_MEM_NB_OFFLINE_THRESHOLD - 1)
		virtio_mem_retry(vm);
}

static void virtio_mem_notify_going_offline(struct virtio_mem *vm,
					    unsigned long mb_id)
{
	const unsigned long nr_pages = PFN_DOWN(vm->subblock_size);
	struct page *page;
	unsigned long pfn;
	int sb_id, i;

	for (sb_id = 0; sb_id < vm->nb_sb_per_mb; sb_id++) {
		if (virtio_mem_mb_test_sb_plugged(vm, mb_id, sb_id, 1))
			continue;
		/*
		 * Drop our reference to the pages so the memory can get
		 * offlined and add the unplugged pages to the managed
		 * page counters (so offlining code can correctly subtract
		 * them again).
		 */
		pfn = PFN_DOWN(virtio_mem_mb_id_to_phys(mb_id) +
			       sb_id * vm->subblock_size);
		adjust_managed_page_count(pfn_to_page(pfn), nr_pages);
		for (i = 0; i < nr_pages; i++) {
			page = pfn_to_page(pfn + i);
			if (WARN_ON(!page_ref_dec_and_test(page)))
				dump_page(page, "unplugged page referenced");
		}
	}
}

static void virtio_mem_notify_cancel_offline(struct virtio_mem *vm,
					     unsigned long mb_id)
{
	const unsigned long nr_pages = PFN_DOWN(vm->subblock_size);
	unsigned long pfn;
	int sb_id, i;

	for (sb_id = 0; sb_id < vm->nb_sb_per_mb; sb_id++) {
		if (virtio_mem_mb_test_sb_plugged(vm, mb_id, sb_id, 1))
			continue;
		/*
		 * Get the reference we dropped when going offline and
		 * subtract the unplugged pages from the managed page
		 * counters.
		 */
		pfn = PFN_DOWN(virtio_mem_mb_id_to_phys(mb_id) +
			       sb_id * vm->subblock_size);
		adjust_managed_page_count(pfn_to_page(pfn), -nr_pages);
		for (i = 0; i < nr_pages; i++)
			page_ref_inc(pfn_to_page(pfn + i));
	}
}

/*
 * This callback will either be called synchronously from add_memory() or
 * asynchronously (e.g., triggered via user space). We have to be careful
 * with locking when calling add_memory().
 */
static int virtio_mem_memory_notifier_cb(struct notifier_block *nb,
					 unsigned long action, void *arg)
{
	struct virtio_mem *vm = container_of(nb, struct virtio_mem,
					     memory_notifier);
	struct memory_notify *mhp = arg;
	const unsigned long start = PFN_PHYS(mhp->start_pfn);
	const unsigned long size = PFN_PHYS(mhp->nr_pages);
	const unsigned long mb_id = virtio_mem_phys_to_mb_id(start);
	int rc = NOTIFY_OK;

	if (!virtio_mem_overlaps_range(vm, start, size))
		return NOTIFY_DONE;

	/*
	 * Memory is onlined/offlined in memory block granularity. We cannot
	 * cross virtio-mem device boundaries and memory block boundaries. Bail
	 * out if this ever changes.
	 */
	if (WARN_ON_ONCE(size != memory_block_size_bytes() ||
			 !IS_ALIGNED(start, memory_block_size_bytes())))
		return NOTIFY_BAD;

	/*
	 * Avoid circular locking lockdep warnings. We lock the mutex
	 * e.g., in MEM_GOING_ONLINE and unlock it in MEM_ONLINE. The
	 * blocking_notifier_call_chain() has it's own lock, which gets unlocked
	 * between both notifier calls and will bail out. False positive.
	 */
	lockdep_off();

	switch (action) {
	case MEM_GOING_OFFLINE:
		mutex_lock(&vm->hotplug_mutex);
		if (vm->removing) {
			rc = notifier_from_errno(-EBUSY);
			mutex_unlock(&vm->hotplug_mutex);
			break;
		}
		vm->hotplug_active = true;
		virtio_mem_notify_going_offline(vm, mb_id);
		break;
	case MEM_GOING_ONLINE:
		mutex_lock(&vm->hotplug_mutex);
		if (vm->removing) {
			rc = notifier_from_errno(-EBUSY);
			mutex_unlock(&vm->hotplug_mutex);
			break;
		}
		vm->hotplug_active = true;
		rc = virtio_mem_notify_going_online(vm, mb_id);
		break;
	case MEM_OFFLINE:
		virtio_mem_notify_offline(vm, mb_id);
		vm->hotplug_active = false;
		mutex_unlock(&vm->hotplug_mutex);
		break;
	case MEM_ONLINE:
		virtio_mem_notify_online(vm, mb_id);
		vm->hotplug_active = false;
		mutex_unlock(&vm->hotplug_mutex);
		break;
	case MEM_CANCEL_OFFLINE:
		if (!vm->hotplug_active)
			break;
		virtio_mem_notify_cancel_offline(vm, mb_id);
		vm->hotplug_active = false;
		mutex_unlock(&vm->hotplug_mutex);
		break;
	case MEM_CANCEL_ONLINE:
		if (!vm->hotplug_active)
			break;
		vm->hotplug_active = false;
		mutex_unlock(&vm->hotplug_mutex);
		break;
	default:
		break;
	}

	lockdep_on();

	return rc;
}

/*
 * Set a range of pages PG_offline. Remember pages that were never onlined
 * (via generic_online_page()) using PageDirty().
 */
static void virtio_mem_set_fake_offline(unsigned long pfn,
					unsigned int nr_pages, bool onlined)
{
	for (; nr_pages--; pfn++) {
		struct page *page = pfn_to_page(pfn);

		__SetPageOffline(page);
		if (!onlined) {
			SetPageDirty(page);
			/* FIXME: remove after cleanups */
			ClearPageReserved(page);
		}
	}
}

/*
 * Clear PG_offline from a range of pages. If the pages were never onlined,
 * (via generic_online_page()), clear PageDirty().
 */
static void virtio_mem_clear_fake_offline(unsigned long pfn,
					  unsigned int nr_pages, bool onlined)
{
	for (; nr_pages--; pfn++) {
		struct page *page = pfn_to_page(pfn);

		__ClearPageOffline(page);
		if (!onlined)
			ClearPageDirty(page);
	}
}

/*
 * Release a range of fake-offline pages to the buddy, effectively
 * fake-onlining them.
 */
static void virtio_mem_fake_online(unsigned long pfn, unsigned int nr_pages)
{
	const int order = MAX_ORDER - 1;
	int i;

	/*
	 * We are always called with subblock granularity, which is at least
	 * aligned to MAX_ORDER - 1.
	 */
	for (i = 0; i < nr_pages; i += 1 << order) {
		struct page *page = pfn_to_page(pfn + i);

		/*
		 * If the page is PageDirty(), it was kept fake-offline when
		 * onlining the memory block. Otherwise, it was allocated
		 * using alloc_contig_range(). All pages in a subblock are
		 * alike.
		 */
		if (PageDirty(page)) {
			virtio_mem_clear_fake_offline(pfn + i, 1 << order,
						      false);
			generic_online_page(page, order);
		} else {
			virtio_mem_clear_fake_offline(pfn + i, 1 << order,
						      true);
			free_contig_range(pfn + i, 1 << order);
			adjust_managed_page_count(page, 1 << order);
		}
	}
}

static void virtio_mem_online_page_cb(struct page *page, unsigned int order)
{
	const unsigned long addr = page_to_phys(page);
	const unsigned long mb_id = virtio_mem_phys_to_mb_id(addr);
	struct virtio_mem *vm;
	int sb_id;

	/*
	 * We exploit here that subblocks have at least MAX_ORDER - 1
	 * size/alignment and that this callback is is called with such a
	 * size/alignment. So we cannot cross subblocks and therefore
	 * also not memory blocks.
	 */
	rcu_read_lock();
	list_for_each_entry_rcu(vm, &virtio_mem_devices, next) {
		if (!virtio_mem_owned_mb(vm, mb_id))
			continue;

		sb_id = virtio_mem_phys_to_sb_id(vm, addr);
		/*
		 * If plugged, online the pages, otherwise, set them fake
		 * offline (PageOffline).
		 */
		if (virtio_mem_mb_test_sb_plugged(vm, mb_id, sb_id, 1))
			generic_online_page(page, order);
		else
			virtio_mem_set_fake_offline(PFN_DOWN(addr), 1 << order,
						    false);
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();

	/* not virtio-mem memory, but e.g., a DIMM. online it */
	generic_online_page(page, order);
}

static uint64_t virtio_mem_send_request(struct virtio_mem *vm,
					const struct virtio_mem_req *req)
{
	struct scatterlist *sgs[2], sg_req, sg_resp;
	unsigned int len;
	int rc;

	/* don't use the request residing on the stack (vaddr) */
	vm->req = *req;

	/* out: buffer for request */
	sg_init_one(&sg_req, &vm->req, sizeof(vm->req));
	sgs[0] = &sg_req;

	/* in: buffer for response */
	sg_init_one(&sg_resp, &vm->resp, sizeof(vm->resp));
	sgs[1] = &sg_resp;

	rc = virtqueue_add_sgs(vm->vq, sgs, 1, 1, vm, GFP_KERNEL);
	if (rc < 0)
		return rc;

	virtqueue_kick(vm->vq);

	/* wait for a response */
	wait_event(vm->host_resp, virtqueue_get_buf(vm->vq, &len));

	return virtio16_to_cpu(vm->vdev, vm->resp.type);
}

static int virtio_mem_send_plug_request(struct virtio_mem *vm, uint64_t addr,
					uint64_t size)
{
	const uint64_t nb_vm_blocks = size / vm->device_block_size;
	const struct virtio_mem_req req = {
		.type = cpu_to_virtio16(vm->vdev, VIRTIO_MEM_REQ_PLUG),
		.u.plug.addr = cpu_to_virtio64(vm->vdev, addr),
		.u.plug.nb_blocks = cpu_to_virtio16(vm->vdev, nb_vm_blocks),
	};

	if (atomic_read(&vm->config_changed))
		return -EAGAIN;

	switch (virtio_mem_send_request(vm, &req)) {
	case VIRTIO_MEM_RESP_ACK:
		vm->plugged_size += size;
		return 0;
	case VIRTIO_MEM_RESP_NACK:
		return -EAGAIN;
	case VIRTIO_MEM_RESP_BUSY:
		return -ETXTBSY;
	case VIRTIO_MEM_RESP_ERROR:
		return -EINVAL;
	default:
		return -ENOMEM;
	}
}

static int virtio_mem_send_unplug_request(struct virtio_mem *vm, uint64_t addr,
					  uint64_t size)
{
	const uint64_t nb_vm_blocks = size / vm->device_block_size;
	const struct virtio_mem_req req = {
		.type = cpu_to_virtio16(vm->vdev, VIRTIO_MEM_REQ_UNPLUG),
		.u.unplug.addr = cpu_to_virtio64(vm->vdev, addr),
		.u.unplug.nb_blocks = cpu_to_virtio16(vm->vdev, nb_vm_blocks),
	};

	if (atomic_read(&vm->config_changed))
		return -EAGAIN;

	switch (virtio_mem_send_request(vm, &req)) {
	case VIRTIO_MEM_RESP_ACK:
		vm->plugged_size -= size;
		return 0;
	case VIRTIO_MEM_RESP_BUSY:
		return -ETXTBSY;
	case VIRTIO_MEM_RESP_ERROR:
		return -EINVAL;
	default:
		return -ENOMEM;
	}
}

static int virtio_mem_send_unplug_all_request(struct virtio_mem *vm)
{
	const struct virtio_mem_req req = {
		.type = cpu_to_virtio16(vm->vdev, VIRTIO_MEM_REQ_UNPLUG_ALL),
	};

	switch (virtio_mem_send_request(vm, &req)) {
	case VIRTIO_MEM_RESP_ACK:
		vm->unplug_all_required = false;
		vm->plugged_size = 0;
		/* usable region might have shrunk */
		atomic_set(&vm->config_changed, 1);
		return 0;
	case VIRTIO_MEM_RESP_BUSY:
		return -ETXTBSY;
	default:
		return -ENOMEM;
	}
}

/*
 * Plug selected subblocks. Updates the plugged state, but not the state
 * of the memory block.
 */
static int virtio_mem_mb_plug_sb(struct virtio_mem *vm, unsigned long mb_id,
				 int sb_id, int count)
{
	const uint64_t addr = virtio_mem_mb_id_to_phys(mb_id) +
			      sb_id * vm->subblock_size;
	const uint64_t size = count * vm->subblock_size;
	int rc;

	dev_dbg(&vm->vdev->dev, "plugging memory block: %lu : %i - %i\n", mb_id,
		sb_id, sb_id + count - 1);

	rc = virtio_mem_send_plug_request(vm, addr, size);
	if (!rc)
		virtio_mem_mb_set_sb_plugged(vm, mb_id, sb_id, count);
	return rc;
}

/*
 * Unplug selected subblocks. Updates the plugged state, but not the state
 * of the memory block.
 */
static int virtio_mem_mb_unplug_sb(struct virtio_mem *vm, unsigned long mb_id,
				   int sb_id, int count)
{
	const uint64_t addr = virtio_mem_mb_id_to_phys(mb_id) +
			      sb_id * vm->subblock_size;
	const uint64_t size = count * vm->subblock_size;
	int rc;

	dev_dbg(&vm->vdev->dev, "unplugging memory block: %lu : %i - %i\n",
		mb_id, sb_id, sb_id + count - 1);

	rc = virtio_mem_send_unplug_request(vm, addr, size);
	if (!rc)
		virtio_mem_mb_set_sb_unplugged(vm, mb_id, sb_id, count);
	return rc;
}

/*
 * Unplug the desired number of plugged subblocks of a offline or not-added
 * memory block. Will fail if any subblock cannot get unplugged (instead of
 * skipping it).
 *
 * Will not modify the state of the memory block.
 *
 * Note: can fail after some subblocks were unplugged.
 */
static int virtio_mem_mb_unplug_any_sb(struct virtio_mem *vm,
				       unsigned long mb_id, uint64_t *nb_sb)
{
	int sb_id, count;
	int rc;

	sb_id = vm->nb_sb_per_mb - 1;
	while (*nb_sb) {
		/* Find the next candidate subblock */
		while (sb_id >= 0 &&
		       virtio_mem_mb_test_sb_unplugged(vm, mb_id, sb_id, 1))
			sb_id--;
		if (sb_id < 0)
			break;
		/* Try to unplug multiple subblocks at a time */
		count = 1;
		while (count < *nb_sb && sb_id > 0 &&
		       virtio_mem_mb_test_sb_plugged(vm, mb_id, sb_id - 1, 1)) {
			count++;
			sb_id--;
		}

		rc = virtio_mem_mb_unplug_sb(vm, mb_id, sb_id, count);
		if (rc)
			return rc;
		*nb_sb -= count;
		sb_id--;
	}

	return 0;
}

/*
 * Unplug all plugged subblocks of an offline or not-added memory block.
 *
 * Will not modify the state of the memory block.
 *
 * Note: can fail after some subblocks were unplugged.
 */
static int virtio_mem_mb_unplug(struct virtio_mem *vm, unsigned long mb_id)
{
	uint64_t nb_sb = vm->nb_sb_per_mb;

	return virtio_mem_mb_unplug_any_sb(vm, mb_id, &nb_sb);
}

/*
 * Prepare tracking data for the next memory block.
 */
static int virtio_mem_prepare_next_mb(struct virtio_mem *vm,
				      unsigned long *mb_id)
{
	int rc;

	if (vm->next_mb_id > vm->last_usable_mb_id)
		return -ENOSPC;

	/* Resize the state array if required. */
	rc = virtio_mem_mb_state_prepare_next_mb(vm);
	if (rc)
		return rc;

	/* Resize the subblock bitmap if required. */
	rc = virtio_mem_sb_bitmap_prepare_next_mb(vm);
	if (rc)
		return rc;

	vm->nb_mb_state[VIRTIO_MEM_MB_STATE_UNUSED]++;
	*mb_id = vm->next_mb_id++;
	return 0;
}

/*
 * Don't add too many blocks that are not onlined yet to avoid running OOM.
 */
static bool virtio_mem_too_many_mb_offline(struct virtio_mem *vm)
{
	unsigned long nb_offline;

	nb_offline = vm->nb_mb_state[VIRTIO_MEM_MB_STATE_OFFLINE] +
		     vm->nb_mb_state[VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL];
	return nb_offline >= VIRTIO_MEM_NB_OFFLINE_THRESHOLD;
}

/*
 * Try to plug the desired number of subblocks and add the memory block
 * to Linux.
 *
 * Will modify the state of the memory block.
 */
static int virtio_mem_mb_plug_and_add(struct virtio_mem *vm,
				      unsigned long mb_id,
				      uint64_t *nb_sb)
{
	const int count = min_t(int, *nb_sb, vm->nb_sb_per_mb);
	int rc, rc2;

	if (WARN_ON_ONCE(!count))
		return -EINVAL;

	/*
	 * Plug the requested number of subblocks before adding it to linux,
	 * so that onlining will directly online all plugged subblocks.
	 */
	rc = virtio_mem_mb_plug_sb(vm, mb_id, 0, count);
	if (rc)
		return rc;

	/*
	 * Mark the block properly offline before adding it to Linux,
	 * so the memory notifiers will find the block in the right state.
	 */
	if (count == vm->nb_sb_per_mb)
		virtio_mem_mb_set_state(vm, mb_id,
					VIRTIO_MEM_MB_STATE_OFFLINE);
	else
		virtio_mem_mb_set_state(vm, mb_id,
					VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL);

	/* Add the memory block to linux - if that fails, try to unplug. */
	rc = virtio_mem_mb_add(vm, mb_id);
	if (rc) {
		enum virtio_mem_mb_state new_state = VIRTIO_MEM_MB_STATE_UNUSED;

		dev_err(&vm->vdev->dev,
			"adding memory block %lu failed with %d\n", mb_id, rc);
		rc2 = virtio_mem_mb_unplug_sb(vm, mb_id, 0, count);

		/*
		 * TODO: Linux MM does not properly clean up yet in all cases
		 * where adding of memory failed - especially on -ENOMEM.
		 */
		if (rc2)
			new_state = VIRTIO_MEM_MB_STATE_PLUGGED;
		virtio_mem_mb_set_state(vm, mb_id, new_state);
		return rc;
	}

	*nb_sb -= count;
	return 0;
}

/*
 * Try to plug the desired number of subblocks of a memory block that
 * is already added to Linux.
 *
 * Will modify the state of the memory block.
 *
 * Note: Can fail after some subblocks were successfully plugged.
 */
static int virtio_mem_mb_plug_any_sb(struct virtio_mem *vm, unsigned long mb_id,
				     uint64_t *nb_sb, bool online)
{
	unsigned long pfn, nr_pages;
	int sb_id, count;
	int rc;

	if (WARN_ON_ONCE(!*nb_sb))
		return -EINVAL;

	while (*nb_sb) {
		sb_id = virtio_mem_mb_first_unplugged_sb(vm, mb_id);
		if (sb_id >= vm->nb_sb_per_mb)
			break;
		count = 1;
		while (count < *nb_sb &&
		       sb_id + count < vm->nb_sb_per_mb &&
		       !virtio_mem_mb_test_sb_plugged(vm, mb_id, sb_id + count,
						      1))
			count++;

		rc = virtio_mem_mb_plug_sb(vm, mb_id, sb_id, count);
		if (rc)
			return rc;
		*nb_sb -= count;
		if (!online)
			continue;

		/* fake-online the pages if the memory block is online */
		pfn = PFN_DOWN(virtio_mem_mb_id_to_phys(mb_id) +
			       sb_id * vm->subblock_size);
		nr_pages = PFN_DOWN(count * vm->subblock_size);
		virtio_mem_fake_online(pfn, nr_pages);
	}

	if (virtio_mem_mb_test_sb_plugged(vm, mb_id, 0, vm->nb_sb_per_mb)) {
		if (online)
			virtio_mem_mb_set_state(vm, mb_id,
						VIRTIO_MEM_MB_STATE_ONLINE);
		else
			virtio_mem_mb_set_state(vm, mb_id,
						VIRTIO_MEM_MB_STATE_OFFLINE);
	}

	return 0;
}

/*
 * Try to plug the requested amount of memory.
 */
static int virtio_mem_plug_request(struct virtio_mem *vm, uint64_t diff)
{
	uint64_t nb_sb = diff / vm->subblock_size;
	unsigned long mb_id;
	int rc;

	if (!nb_sb)
		return 0;

	/* Don't race with onlining/offlining */
	mutex_lock(&vm->hotplug_mutex);

	/* Try to plug subblocks of partially plugged online blocks. */
	virtio_mem_for_each_mb_state(vm, mb_id,
				     VIRTIO_MEM_MB_STATE_ONLINE_PARTIAL) {
		rc = virtio_mem_mb_plug_any_sb(vm, mb_id, &nb_sb, true);
		if (rc || !nb_sb)
			goto out_unlock;
		cond_resched();
	}

	/* Try to plug subblocks of partially plugged offline blocks. */
	virtio_mem_for_each_mb_state(vm, mb_id,
				     VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL) {
		rc = virtio_mem_mb_plug_any_sb(vm, mb_id, &nb_sb, false);
		if (rc || !nb_sb)
			goto out_unlock;
		cond_resched();
	}

	/*
	 * We won't be working on online/offline memory blocks from this point,
	 * so we can't race with memory onlining/offlining. Drop the mutex.
	 */
	mutex_unlock(&vm->hotplug_mutex);

	/* Try to plug and add unused blocks */
	virtio_mem_for_each_mb_state(vm, mb_id, VIRTIO_MEM_MB_STATE_UNUSED) {
		if (virtio_mem_too_many_mb_offline(vm))
			return -ENOSPC;

		rc = virtio_mem_mb_plug_and_add(vm, mb_id, &nb_sb);
		if (rc || !nb_sb)
			return rc;
		cond_resched();
	}

	/* Try to prepare, plug and add new blocks */
	while (nb_sb) {
		if (virtio_mem_too_many_mb_offline(vm))
			return -ENOSPC;

		rc = virtio_mem_prepare_next_mb(vm, &mb_id);
		if (rc)
			return rc;
		rc = virtio_mem_mb_plug_and_add(vm, mb_id, &nb_sb);
		if (rc)
			return rc;
		cond_resched();
	}

	return 0;
out_unlock:
	mutex_unlock(&vm->hotplug_mutex);
	return rc;
}

/*
 * Unplug the desired number of plugged subblocks of an offline memory block.
 * Will fail if any subblock cannot get unplugged (instead of skipping it).
 *
 * Will modify the state of the memory block. Might temporarily drop the
 * hotplug_mutex.
 *
 * Note: Can fail after some subblocks were successfully unplugged.
 */
static int virtio_mem_mb_unplug_any_sb_offline(struct virtio_mem *vm,
					       unsigned long mb_id,
					       uint64_t *nb_sb)
{
	int rc;

	rc = virtio_mem_mb_unplug_any_sb(vm, mb_id, nb_sb);

	/* some subblocks might have been unplugged even on failure */
	if (!virtio_mem_mb_test_sb_plugged(vm, mb_id, 0, vm->nb_sb_per_mb))
		virtio_mem_mb_set_state(vm, mb_id,
					VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL);
	if (rc)
		return rc;

	if (virtio_mem_mb_test_sb_unplugged(vm, mb_id, 0, vm->nb_sb_per_mb)) {
		/*
		 * Remove the block from Linux - this should never fail.
		 * Hinder the block from getting onlined by marking it
		 * unplugged. Temporarily drop the mutex, so
		 * any pending GOING_ONLINE requests can be serviced/rejected.
		 */
		virtio_mem_mb_set_state(vm, mb_id,
					VIRTIO_MEM_MB_STATE_UNUSED);

		mutex_unlock(&vm->hotplug_mutex);
		rc = virtio_mem_mb_remove(vm, mb_id);
		BUG_ON(rc);
		mutex_lock(&vm->hotplug_mutex);
	}
	return 0;
}

/*
 * Unplug the given plugged subblocks of an online memory block.
 *
 * Will modify the state of the memory block.
 */
static int virtio_mem_mb_unplug_sb_online(struct virtio_mem *vm,
					  unsigned long mb_id, int sb_id,
					  int count)
{
	const unsigned long nr_pages = PFN_DOWN(vm->subblock_size) * count;
	unsigned long start_pfn;
	int rc;

	start_pfn = PFN_DOWN(virtio_mem_mb_id_to_phys(mb_id) +
			     sb_id * vm->subblock_size);
	rc = alloc_contig_range(start_pfn, start_pfn + nr_pages,
				MIGRATE_MOVABLE, GFP_KERNEL);
	if (rc == -ENOMEM)
		/* whoops, out of memory */
		return rc;
	if (rc)
		return -EBUSY;

	/* Mark it as fake-offline before unplugging it */
	virtio_mem_set_fake_offline(start_pfn, nr_pages, true);
	adjust_managed_page_count(pfn_to_page(start_pfn), -nr_pages);

	/* Try to unplug the allocated memory */
	rc = virtio_mem_mb_unplug_sb(vm, mb_id, sb_id, count);
	if (rc) {
		/* Return the memory to the buddy. */
		virtio_mem_fake_online(start_pfn, nr_pages);
		return rc;
	}

	virtio_mem_mb_set_state(vm, mb_id,
				VIRTIO_MEM_MB_STATE_ONLINE_PARTIAL);
	return 0;
}

/*
 * Unplug the desired number of plugged subblocks of an online memory block.
 * Will skip subblock that are busy.
 *
 * Will modify the state of the memory block. Might temporarily drop the
 * hotplug_mutex.
 *
 * Note: Can fail after some subblocks were successfully unplugged. Can
 *       return 0 even if subblocks were busy and could not get unplugged.
 */
static int virtio_mem_mb_unplug_any_sb_online(struct virtio_mem *vm,
					      unsigned long mb_id,
					      uint64_t *nb_sb)
{
	int rc, sb_id;

	/* If possible, try to unplug the complete block in one shot. */
	if (*nb_sb >= vm->nb_sb_per_mb &&
	    virtio_mem_mb_test_sb_plugged(vm, mb_id, 0, vm->nb_sb_per_mb)) {
		rc = virtio_mem_mb_unplug_sb_online(vm, mb_id, 0,
						    vm->nb_sb_per_mb);
		if (!rc) {
			*nb_sb -= vm->nb_sb_per_mb;
			goto unplugged;
		} else if (rc != -EBUSY)
			return rc;
	}

	/* Fallback to single subblocks. */
	for (sb_id = vm->nb_sb_per_mb - 1; sb_id >= 0 && *nb_sb; sb_id--) {
		/* Find the next candidate subblock */
		while (sb_id >= 0 &&
		       !virtio_mem_mb_test_sb_plugged(vm, mb_id, sb_id, 1))
			sb_id--;
		if (sb_id < 0)
			break;

		rc = virtio_mem_mb_unplug_sb_online(vm, mb_id, sb_id, 1);
		if (rc == -EBUSY)
			continue;
		else if (rc)
			return rc;
		*nb_sb -= 1;
	}

unplugged:
	/*
	 * Once all subblocks of a memory block were unplugged, offline and
	 * remove it. This will usually not fail, as no memory is in use
	 * anymore - however some other notifiers might NACK the request.
	 */
	if (virtio_mem_mb_test_sb_unplugged(vm, mb_id, 0, vm->nb_sb_per_mb)) {
		mutex_unlock(&vm->hotplug_mutex);
		rc = virtio_mem_mb_offline_and_remove(vm, mb_id);
		mutex_lock(&vm->hotplug_mutex);
		if (!rc)
			virtio_mem_mb_set_state(vm, mb_id,
						VIRTIO_MEM_MB_STATE_UNUSED);
	}

	return 0;
}

/*
 * Try to unplug the requested amount of memory.
 */
static int virtio_mem_unplug_request(struct virtio_mem *vm, uint64_t diff)
{
	uint64_t nb_sb = diff / vm->subblock_size;
	unsigned long mb_id;
	int rc;

	if (!nb_sb)
		return 0;

	/*
	 * We'll drop the mutex a couple of times when it is safe to do so.
	 * This might result in some blocks switching the state (online/offline)
	 * and we could miss them in this run - we will retry again later.
	 */
	mutex_lock(&vm->hotplug_mutex);

	/* Try to unplug subblocks of partially plugged offline blocks. */
	virtio_mem_for_each_mb_state_rev(vm, mb_id,
					 VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL) {
		rc = virtio_mem_mb_unplug_any_sb_offline(vm, mb_id,
							 &nb_sb);
		if (rc || !nb_sb)
			goto out_unlock;
		cond_resched();
	}

	/* Try to unplug subblocks of plugged offline blocks. */
	virtio_mem_for_each_mb_state_rev(vm, mb_id,
					 VIRTIO_MEM_MB_STATE_OFFLINE) {
		rc = virtio_mem_mb_unplug_any_sb_offline(vm, mb_id,
							 &nb_sb);
		if (rc || !nb_sb)
			goto out_unlock;
		cond_resched();
	}

	if (!unplug_online) {
		mutex_unlock(&vm->hotplug_mutex);
		return 0;
	}

	/* Try to unplug subblocks of partially plugged online blocks. */
	virtio_mem_for_each_mb_state_rev(vm, mb_id,
					 VIRTIO_MEM_MB_STATE_ONLINE_PARTIAL) {
		rc = virtio_mem_mb_unplug_any_sb_online(vm, mb_id,
							&nb_sb);
		if (rc || !nb_sb)
			goto out_unlock;
		mutex_unlock(&vm->hotplug_mutex);
		cond_resched();
		mutex_lock(&vm->hotplug_mutex);
	}

	/* Try to unplug subblocks of plugged online blocks. */
	virtio_mem_for_each_mb_state_rev(vm, mb_id,
					 VIRTIO_MEM_MB_STATE_ONLINE) {
		rc = virtio_mem_mb_unplug_any_sb_online(vm, mb_id,
							&nb_sb);
		if (rc || !nb_sb)
			goto out_unlock;
		mutex_unlock(&vm->hotplug_mutex);
		cond_resched();
		mutex_lock(&vm->hotplug_mutex);
	}

	mutex_unlock(&vm->hotplug_mutex);
	return nb_sb ? -EBUSY : 0;
out_unlock:
	mutex_unlock(&vm->hotplug_mutex);
	return rc;
}

/*
 * Try to unplug all blocks that couldn't be unplugged before, for example,
 * because the hypervisor was busy.
 */
static int virtio_mem_unplug_pending_mb(struct virtio_mem *vm)
{
	unsigned long mb_id;
	int rc;

	virtio_mem_for_each_mb_state(vm, mb_id, VIRTIO_MEM_MB_STATE_PLUGGED) {
		rc = virtio_mem_mb_unplug(vm, mb_id);
		if (rc)
			return rc;
		virtio_mem_mb_set_state(vm, mb_id, VIRTIO_MEM_MB_STATE_UNUSED);
	}

	return 0;
}

/*
 * Update all parts of the config that could have changed.
 */
static void virtio_mem_refresh_config(struct virtio_mem *vm)
{
	const uint64_t phys_limit = 1UL << MAX_PHYSMEM_BITS;
	uint64_t new_plugged_size, usable_region_size, end_addr;

	/* the plugged_size is just a reflection of what _we_ did previously */
	virtio_cread_le(vm->vdev, struct virtio_mem_config, plugged_size,
			&new_plugged_size);
	if (WARN_ON_ONCE(new_plugged_size != vm->plugged_size))
		vm->plugged_size = new_plugged_size;

	/* calculate the last usable memory block id */
	virtio_cread_le(vm->vdev, struct virtio_mem_config,
			usable_region_size, &usable_region_size);
	end_addr = vm->addr + usable_region_size;
	end_addr = min(end_addr, phys_limit);
	vm->last_usable_mb_id = virtio_mem_phys_to_mb_id(end_addr) - 1;

	/* see if there is a request to change the size */
	virtio_cread_le(vm->vdev, struct virtio_mem_config, requested_size,
			&vm->requested_size);

	dev_info(&vm->vdev->dev, "plugged size: 0x%llx", vm->plugged_size);
	dev_info(&vm->vdev->dev, "requested size: 0x%llx", vm->requested_size);
}

/*
 * Workqueue function for handling plug/unplug requests and config updates.
 */
static void virtio_mem_run_wq(struct work_struct *work)
{
	struct virtio_mem *vm = container_of(work, struct virtio_mem, wq);
	uint64_t diff;
	int rc;

	hrtimer_cancel(&vm->retry_timer);

	if (vm->broken)
		return;

retry:
	rc = 0;

	/* Make sure we start with a clean state if there are leftovers. */
	if (unlikely(vm->unplug_all_required))
		rc = virtio_mem_send_unplug_all_request(vm);

	if (atomic_read(&vm->config_changed)) {
		atomic_set(&vm->config_changed, 0);
		virtio_mem_refresh_config(vm);
	}

	/* Unplug any leftovers from previous runs */
	if (!rc)
		rc = virtio_mem_unplug_pending_mb(vm);

	if (!rc && vm->requested_size != vm->plugged_size) {
		if (vm->requested_size > vm->plugged_size) {
			diff = vm->requested_size - vm->plugged_size;
			rc = virtio_mem_plug_request(vm, diff);
		} else {
			diff = vm->plugged_size - vm->requested_size;
			rc = virtio_mem_unplug_request(vm, diff);
		}
	}

	switch (rc) {
	case 0:
		vm->retry_timer_ms = VIRTIO_MEM_RETRY_TIMER_MIN_MS;
		break;
	case -ENOSPC:
		/*
		 * We cannot add any more memory (alignment, physical limit)
		 * or we have too many offline memory blocks.
		 */
		break;
	case -ETXTBSY:
		/*
		 * The hypervisor cannot process our request right now
		 * (e.g., out of memory, migrating);
		 */
	case -EBUSY:
		/*
		 * We cannot free up any memory to unplug it (all plugged memory
		 * is busy).
		 */
	case -ENOMEM:
		/* Out of memory, try again later. */
		hrtimer_start(&vm->retry_timer, ms_to_ktime(vm->retry_timer_ms),
			      HRTIMER_MODE_REL);
		break;
	case -EAGAIN:
		/* Retry immediately (e.g., the config changed). */
		goto retry;
	default:
		/* Unknown error, mark as broken */
		dev_err(&vm->vdev->dev,
			"unknown error, marking device broken: %d\n", rc);
		vm->broken = true;
	}
}

static enum hrtimer_restart virtio_mem_timer_expired(struct hrtimer *timer)
{
	struct virtio_mem *vm = container_of(timer, struct virtio_mem,
					     retry_timer);

	virtio_mem_retry(vm);
	vm->retry_timer_ms = min_t(unsigned int, vm->retry_timer_ms * 2,
				   VIRTIO_MEM_RETRY_TIMER_MAX_MS);
	return HRTIMER_NORESTART;
}

static void virtio_mem_handle_response(struct virtqueue *vq)
{
	struct virtio_mem *vm = vq->vdev->priv;

	wake_up(&vm->host_resp);
}

static int virtio_mem_init_vq(struct virtio_mem *vm)
{
	struct virtqueue *vq;

	vq = virtio_find_single_vq(vm->vdev, virtio_mem_handle_response,
				   "guest-request");
	if (IS_ERR(vq))
		return PTR_ERR(vq);
	vm->vq = vq;

	return 0;
}

static int virtio_mem_init(struct virtio_mem *vm)
{
	const uint64_t phys_limit = 1UL << MAX_PHYSMEM_BITS;
	uint16_t node_id;

	if (!vm->vdev->config->get) {
		dev_err(&vm->vdev->dev, "config access disabled\n");
		return -EINVAL;
	}

	/*
	 * We don't want to (un)plug or reuse any memory when in kdump. The
	 * memory is still accessible (but not mapped).
	 */
	if (is_kdump_kernel()) {
		dev_warn(&vm->vdev->dev, "disabled in kdump kernel\n");
		return -EBUSY;
	}

	/* Fetch all properties that can't change. */
	virtio_cread_le(vm->vdev, struct virtio_mem_config, plugged_size,
			&vm->plugged_size);
	virtio_cread_le(vm->vdev, struct virtio_mem_config, block_size,
			&vm->device_block_size);
	virtio_cread_le(vm->vdev, struct virtio_mem_config, node_id,
			&node_id);
	vm->nid = virtio_mem_translate_node_id(vm, node_id);
	virtio_cread_le(vm->vdev, struct virtio_mem_config, addr, &vm->addr);
	virtio_cread_le(vm->vdev, struct virtio_mem_config, region_size,
			&vm->region_size);

	/*
	 * We always hotplug memory in memory block granularity. This way,
	 * we have to wait for exactly one memory block to online.
	 */
	if (vm->device_block_size > memory_block_size_bytes()) {
		dev_err(&vm->vdev->dev,
			"The block size is not supported (too big).\n");
		return -EINVAL;
	}

	/* bad device setup - warn only */
	if (!IS_ALIGNED(vm->addr, memory_block_size_bytes()))
		dev_warn(&vm->vdev->dev,
			 "The alignment of the physical start address can make some memory unusable.\n");
	if (!IS_ALIGNED(vm->addr + vm->region_size, memory_block_size_bytes()))
		dev_warn(&vm->vdev->dev,
			 "The alignment of the physical end address can make some memory unusable.\n");
	if (vm->addr + vm->region_size > phys_limit)
		dev_warn(&vm->vdev->dev,
			 "Some memory is not addressable. This can make some memory unusable.\n");

	/*
	 * Calculate the subblock size:
	 * - At least MAX_ORDER - 1 / pageblock_order.
	 * - At least the device block size.
	 * In the worst case, a single subblock per memory block.
	 */
	vm->subblock_size = PAGE_SIZE * 1ul << max_t(uint32_t, MAX_ORDER - 1,
						     pageblock_order);
	vm->subblock_size = max_t(uint64_t, vm->device_block_size,
				  vm->subblock_size);
	vm->nb_sb_per_mb = memory_block_size_bytes() / vm->subblock_size;

	/* Round up to the next full memory block */
	vm->first_mb_id = virtio_mem_phys_to_mb_id(vm->addr - 1 +
						   memory_block_size_bytes());
	vm->next_mb_id = vm->first_mb_id;
	vm->last_mb_id = virtio_mem_phys_to_mb_id(vm->addr +
			 vm->region_size) - 1;

	dev_info(&vm->vdev->dev, "start address: 0x%llx", vm->addr);
	dev_info(&vm->vdev->dev, "region size: 0x%llx", vm->region_size);
	dev_info(&vm->vdev->dev, "device block size: 0x%llx",
		 (unsigned long long)vm->device_block_size);
	dev_info(&vm->vdev->dev, "memory block size: 0x%lx",
		 memory_block_size_bytes());
	dev_info(&vm->vdev->dev, "subblock size: 0x%llx",
		 (unsigned long long)vm->subblock_size);
	if (vm->nid != NUMA_NO_NODE)
		dev_info(&vm->vdev->dev, "nid: %d", vm->nid);

	return 0;
}

static int virtio_mem_create_resource(struct virtio_mem *vm)
{
	/*
	 * When force-unloading the driver and removing the device, we
	 * could have a garbage pointer. Duplicate the string.
	 */
	const char *name = kstrdup(dev_name(&vm->vdev->dev), GFP_KERNEL);

	if (!name)
		return -ENOMEM;

	vm->parent_resource = __request_mem_region(vm->addr, vm->region_size,
						   name, IORESOURCE_SYSTEM_RAM);
	if (!vm->parent_resource) {
		kfree(name);
		dev_warn(&vm->vdev->dev, "could not reserve device region\n");
		dev_info(&vm->vdev->dev,
			 "reloading the driver is not supported\n");
		return -EBUSY;
	}

	/* The memory is not actually busy - make add_memory() work. */
	vm->parent_resource->flags &= ~IORESOURCE_BUSY;
	return 0;
}

static void virtio_mem_delete_resource(struct virtio_mem *vm)
{
	const char *name;

	if (!vm->parent_resource)
		return;

	name = vm->parent_resource->name;
	release_resource(vm->parent_resource);
	kfree(vm->parent_resource);
	kfree(name);
	vm->parent_resource = NULL;
}

static int virtio_mem_probe(struct virtio_device *vdev)
{
	struct virtio_mem *vm;
	int rc;

	BUILD_BUG_ON(sizeof(struct virtio_mem_req) != 24);
	BUILD_BUG_ON(sizeof(struct virtio_mem_resp) != 10);

	vdev->priv = vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return -ENOMEM;

	init_waitqueue_head(&vm->host_resp);
	vm->vdev = vdev;
	INIT_WORK(&vm->wq, virtio_mem_run_wq);
	mutex_init(&vm->hotplug_mutex);
	INIT_LIST_HEAD(&vm->next);
	spin_lock_init(&vm->removal_lock);
	hrtimer_init(&vm->retry_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vm->retry_timer.function = virtio_mem_timer_expired;
	vm->retry_timer_ms = VIRTIO_MEM_RETRY_TIMER_MIN_MS;

	/* register the virtqueue */
	rc = virtio_mem_init_vq(vm);
	if (rc)
		goto out_free_vm;

	/* initialize the device by querying the config */
	rc = virtio_mem_init(vm);
	if (rc)
		goto out_del_vq;

	/* create the parent resource for all memory */
	rc = virtio_mem_create_resource(vm);
	if (rc)
		goto out_del_vq;

	/*
	 * If we still have memory plugged, we have to unplug all memory first.
	 * Registering our parent resource makes sure that this memory isn't
	 * actually in use (e.g., trying to reload the driver).
	 */
	if (vm->plugged_size) {
		vm->unplug_all_required = 1;
		dev_info(&vm->vdev->dev, "unplugging all memory is required\n");
	}

	/* register callbacks */
	vm->memory_notifier.notifier_call = virtio_mem_memory_notifier_cb;
	rc = register_memory_notifier(&vm->memory_notifier);
	if (rc)
		goto out_del_resource;
	rc = register_virtio_mem_device(vm);
	if (rc)
		goto out_unreg_mem;

	virtio_device_ready(vdev);

	/* trigger a config update to start processing the requested_size */
	atomic_set(&vm->config_changed, 1);
	queue_work(system_freezable_wq, &vm->wq);

	return 0;
out_unreg_mem:
	unregister_memory_notifier(&vm->memory_notifier);
out_del_resource:
	virtio_mem_delete_resource(vm);
out_del_vq:
	vdev->config->del_vqs(vdev);
out_free_vm:
	kfree(vm);
	vdev->priv = NULL;

	return rc;
}

static void virtio_mem_remove(struct virtio_device *vdev)
{
	struct virtio_mem *vm = vdev->priv;
	unsigned long mb_id;
	int rc;

	/*
	 * Make sure the workqueue won't be triggered anymore and no memory
	 * blocks can be onlined/offlined until we're finished here.
	 */
	mutex_lock(&vm->hotplug_mutex);
	spin_lock_irq(&vm->removal_lock);
	vm->removing = true;
	spin_unlock_irq(&vm->removal_lock);
	mutex_unlock(&vm->hotplug_mutex);

	/* wait until the workqueue stopped */
	cancel_work_sync(&vm->wq);
	hrtimer_cancel(&vm->retry_timer);

	/*
	 * After we unregistered our callbacks, user space can online partially
	 * plugged offline blocks. Make sure to remove them.
	 */
	virtio_mem_for_each_mb_state(vm, mb_id,
				     VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL) {
		rc = virtio_mem_mb_remove(vm, mb_id);
		BUG_ON(rc);
		virtio_mem_mb_set_state(vm, mb_id, VIRTIO_MEM_MB_STATE_UNUSED);
	}
	/*
	 * After we unregistered our callbacks, user space can no longer
	 * offline partially plugged online memory blocks. No need to worry
	 * about them.
	 */

	/* unregister callbacks */
	unregister_virtio_mem_device(vm);
	unregister_memory_notifier(&vm->memory_notifier);

	/*
	 * There is no way we could reliably remove all memory we have added to
	 * the system. And there is no way to stop the driver/device from going
	 * away. Warn at least.
	 */
	if (vm->nb_mb_state[VIRTIO_MEM_MB_STATE_OFFLINE] ||
	    vm->nb_mb_state[VIRTIO_MEM_MB_STATE_OFFLINE_PARTIAL] ||
	    vm->nb_mb_state[VIRTIO_MEM_MB_STATE_ONLINE] ||
	    vm->nb_mb_state[VIRTIO_MEM_MB_STATE_ONLINE_PARTIAL]) {
		dev_warn(&vdev->dev, "device still has system memory added\n");
	} else {
		virtio_mem_delete_resource(vm);
		kfree_const(vm->resource_name);
	}

	/* remove all tracking data - no locking needed */
	vfree(vm->mb_state);
	vfree(vm->sb_bitmap);

	/* reset the device and cleanup the queues */
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);

	kfree(vm);
	vdev->priv = NULL;
}

static void virtio_mem_config_changed(struct virtio_device *vdev)
{
	struct virtio_mem *vm = vdev->priv;

	atomic_set(&vm->config_changed, 1);
	virtio_mem_retry(vm);
}

#ifdef CONFIG_PM_SLEEP
static int virtio_mem_freeze(struct virtio_device *vdev)
{
	/*
	 * When restarting the VM, all memory is usually unplugged. Don't
	 * allow to suspend/hibernate.
	 */
	dev_err(&vdev->dev, "save/restore not supported.\n");
	return -EPERM;
}

static int virtio_mem_restore(struct virtio_device *vdev)
{
	return -EPERM;
}
#endif

static unsigned int virtio_mem_features[] = {
#if defined(CONFIG_NUMA) && defined(CONFIG_ACPI_NUMA)
	VIRTIO_MEM_F_ACPI_PXM,
#endif
};

static const struct virtio_device_id virtio_mem_id_table[] = {
	{ VIRTIO_ID_MEM, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_mem_driver = {
	.feature_table = virtio_mem_features,
	.feature_table_size = ARRAY_SIZE(virtio_mem_features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = virtio_mem_id_table,
	.probe = virtio_mem_probe,
	.remove = virtio_mem_remove,
	.config_changed = virtio_mem_config_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze	=	virtio_mem_freeze,
	.restore =	virtio_mem_restore,
#endif
};

module_virtio_driver(virtio_mem_driver);
MODULE_DEVICE_TABLE(virtio, virtio_mem_id_table);
MODULE_AUTHOR("David Hildenbrand <david@redhat.com>");
MODULE_DESCRIPTION("Virtio-mem driver");
MODULE_LICENSE("GPL");
