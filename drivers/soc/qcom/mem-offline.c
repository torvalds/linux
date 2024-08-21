// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023,2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/memory.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/mmu_context.h>
#include <linux/mmzone.h>
#include <linux/ktime.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/qmp.h>
#ifdef CONFIG_MSM_RPM_SMD
#include <soc/qcom/rpm-smd.h>
#endif
#include <linux/migrate.h>
#include <linux/swap.h>
#include <linux/mm_inline.h>
#include <linux/compaction.h>

struct movable_zone_fill_control {
	struct list_head freepages;
	unsigned long start_pfn;
	unsigned long end_pfn;
	unsigned long nr_migrate_pages;
	unsigned long nr_free_pages;
	unsigned long limit;
	int target;
	struct zone *zone;
};

static void fill_movable_zone_fn(struct work_struct *work);
static DECLARE_WORK(fill_movable_zone_work, fill_movable_zone_fn);
static DEFINE_MUTEX(page_migrate_lock);
#define RPM_DDR_REQ 0x726464
#define AOP_MSG_ADDR_MASK		0xffffffff
#define AOP_MSG_ADDR_HIGH_SHIFT		32
#define MAX_LEN				96

/**
 * bypass_send_msg - skip mem offline/online mesg sent to rpm/aop
 */
static bool bypass_send_msg;
module_param(bypass_send_msg, bool, 0644);
MODULE_PARM_DESC(bypass_send_msg,
	"skip mem offline/online mesg sent to rpm/aop.");

static unsigned long start_section_nr, end_section_nr;
static struct kobject *kobj;
static unsigned int sections_per_block;
static atomic_t target_migrate_pages = ATOMIC_INIT(0);
static u32 offline_granule;
static bool is_rpm_controller;
static DECLARE_BITMAP(movable_bitmap, 1024);
static bool has_pend_offline_req;
static struct workqueue_struct *migrate_wq;
static struct timer_list mem_offline_timeout_timer;
static struct task_struct *offline_trig_task;
#define MODULE_CLASS_NAME	"mem-offline"
#define MEMBLOCK_NAME		"memory%lu"
#define SEGMENT_NAME		"segment%lu"
#define BUF_LEN			100
#define MIGRATE_TIMEOUT_SEC	20
#define OFFLINE_TIMEOUT_SEC	7

struct section_stat {
	unsigned long success_count;
	unsigned long fail_count;
	unsigned long avg_time;
	unsigned long best_time;
	unsigned long worst_time;
	unsigned long total_time;
	unsigned long last_recorded_time;
	ktime_t resident_time;
	ktime_t resident_since;
};

enum memory_states {
	MEMORY_ONLINE,
	MEMORY_OFFLINE,
	MAX_STATE,
};

struct segment_info {
	signed long start_addr;
	unsigned long seg_size;
	unsigned long num_kernel_blks;
	unsigned int bitmask_kernel_blk;
	enum memory_states state;
};

#define MAX_NUM_SEGMENTS 16
#define MAX_NUM_DDR_REGIONS 10

struct ddr_region {
	/* region physical address */
	unsigned long start_address;

	/* size of region in bytes */
	unsigned long length;

	/* size of segments in MB (1024 * 1024 bytes) */
	unsigned long granule_size;

	/* index of first full segment in a region */
	unsigned int segments_start_idx;

	/* offset in bytes to first full segment */
	unsigned long segments_start_offset;

};

static struct segment_info *segment_infos;

static struct ddr_region *ddr_regions;

static int differing_segment_sizes;

static int num_ddr_regions, num_segments;

/*
 * start_addr_HIGH, start_addr_LOW,
 * length_HIGH, length_LOW,
 * segment_start_offset_HIGH, segment_start_offset_LOW,
 * segment_start_idx_HIGH, segment_start_idx_LOW,
 * granule_size_HIGH, granule_size_LOW
 */
#define DDR_REGIONS_NUM_CELLS       10

static enum memory_states *mem_sec_state;

static phys_addr_t bootmem_dram_end_addr;

static phys_addr_t offlinable_region_start_addr;

static struct mem_offline_mailbox {
	struct mbox_client cl;
	struct mbox_chan *mbox;
} mailbox;

struct memory_refresh_request {
	u64 start;	/* Lower bit signifies action
			 * 0 - disable self-refresh
			 * 1 - enable self-refresh
			 * upper bits are for base address
			 */
	u32 size;	/* size of memory region */
};

static struct section_stat *mem_info;

static int nopasr;
module_param_named(nopasr, nopasr, uint, 0644);

static void record_stat(unsigned long sec, ktime_t delay, int mode)
{
	unsigned int total_sec = end_section_nr - start_section_nr + 1;
	unsigned int blk_nr = (sec - start_section_nr + mode * total_sec) /
				sections_per_block;
	ktime_t now, delta;

	if (sec > end_section_nr)
		return;

	if (delay < mem_info[blk_nr].best_time || !mem_info[blk_nr].best_time)
		mem_info[blk_nr].best_time = delay;

	if (delay > mem_info[blk_nr].worst_time)
		mem_info[blk_nr].worst_time = delay;

	++mem_info[blk_nr].success_count;
	if (mem_info[blk_nr].fail_count)
		--mem_info[blk_nr].fail_count;

	mem_info[blk_nr].total_time += delay;

	mem_info[blk_nr].avg_time =
		mem_info[blk_nr].total_time / mem_info[blk_nr].success_count;

	mem_info[blk_nr].last_recorded_time = delay;

	now = ktime_get();
	mem_info[blk_nr].resident_since = now;

	/* since other state has gone inactive, update the stats */
	mode = mode ? MEMORY_ONLINE : MEMORY_OFFLINE;
	blk_nr = (sec - start_section_nr + mode * total_sec) /
				sections_per_block;
	delta = ktime_sub(now, mem_info[blk_nr].resident_since);
	mem_info[blk_nr].resident_time =
			ktime_add(mem_info[blk_nr].resident_time, delta);
	mem_info[blk_nr].resident_since = 0;
}

static int mem_region_refresh_control(unsigned long pfn,
				      unsigned long nr_pages,
				      bool enable)
{
#ifdef CONFIG_MSM_RPM_SMD
	struct memory_refresh_request mem_req;
	struct msm_rpm_kvp rpm_kvp;

	mem_req.start = enable;
	mem_req.start |= pfn << PAGE_SHIFT;
	mem_req.size = nr_pages * PAGE_SIZE;

	rpm_kvp.key = RPM_DDR_REQ;
	rpm_kvp.data = (void *)&mem_req;
	rpm_kvp.length = sizeof(mem_req);

	return msm_rpm_send_message(MSM_RPM_CTX_ACTIVE_SET, RPM_DDR_REQ, 0,
				    &rpm_kvp, 1);
#else
	return -EINVAL;
#endif
}

static int aop_send_msg(unsigned long addr, bool online)
{
	struct qmp_pkt pkt;
	char mbox_msg[MAX_LEN];
	unsigned long addr_low, addr_high;

	addr_low = addr & AOP_MSG_ADDR_MASK;
	addr_high = (addr >> AOP_MSG_ADDR_HIGH_SHIFT) & AOP_MSG_ADDR_MASK;

	snprintf(mbox_msg, MAX_LEN,
		 "{class: ddr, event: pasr, addr_hi: 0x%08lx, addr_lo: 0x%08lx, refresh: %s}",
		 addr_high, addr_low, online ? "on" : "off");

	pkt.size = MAX_LEN;
	pkt.data = mbox_msg;
	return mbox_send_message(mailbox.mbox, &pkt);
}

static long get_memblk_bits(int seg_idx, unsigned long memblk_addr)
{
	if (seg_idx < 0 || (memblk_addr > segment_infos[seg_idx].start_addr +
			segment_infos[seg_idx].seg_size))
		return -EINVAL;

	return (1 << ((memblk_addr - segment_infos[seg_idx].start_addr) /
				memory_block_size_bytes()));
}

static int get_segment_addr_to_idx(unsigned long addr)
{
	int i;

	for (i = 0; i < num_segments; i++) {
		if (addr >= segment_infos[i].start_addr &&
			addr < segment_infos[i].start_addr + segment_infos[i].seg_size)
			return i;
	}

	return -EINVAL;
}

static int send_msg(struct memory_notify *mn, bool online, int count)
{
	unsigned long segment_size, start, addr, base_addr;
	int ret, i, seg_idx;

	if (bypass_send_msg)
		return 0;

	addr = __pfn_to_phys(SECTION_ALIGN_DOWN(mn->start_pfn));
	seg_idx = get_segment_addr_to_idx(addr);
	base_addr = segment_infos[seg_idx].start_addr;

	for (i = 0; i < count; ++i) {

		seg_idx = get_segment_addr_to_idx(addr);
		segment_size = segment_infos[seg_idx].seg_size;
		start = __phys_to_pfn(segment_infos[seg_idx].start_addr);
		addr = segment_infos[seg_idx].start_addr;

		if (is_rpm_controller)
			ret = mem_region_refresh_control(start,
						 segment_size >> PAGE_SHIFT,
						 online);
		else
			ret = aop_send_msg(__pfn_to_phys(start), online);

		if (ret < 0) {
			pr_err("PASR: %s %s request addr:0x%llx failed and return value from AOP is %d\n",
			       is_rpm_controller ? "RPM" : "AOP",
			       online ? "online" : "offline",
			       __pfn_to_phys(start), ret);
			goto undo;
		}

		pr_info("mem-offline: sent msg successfully to %s segment at phys addr 0x%lx\n",
					online ? "online" : "offline", __pfn_to_phys(start));
		addr += segment_size;
	}

	return 0;
undo:
	addr = base_addr;
	seg_idx = get_segment_addr_to_idx(addr);
	start = __phys_to_pfn(base_addr);

	while (i-- > 0) {
		int ret;

		if (is_rpm_controller)
			ret = mem_region_refresh_control(start,
						 segment_size >> PAGE_SHIFT,
						 !online);
		else
			ret = aop_send_msg(__pfn_to_phys(start), !online);

		if (ret < 0)
			panic("Failed to completely online/offline a hotpluggable segment. A quasi state of memblock can cause random system failures. Return value from AOP is %d",
				ret);
		segment_size = segment_infos[seg_idx].seg_size;
		addr += segment_size;
		seg_idx = get_segment_addr_to_idx(addr);
		start = __phys_to_pfn(__pfn_to_phys(start) + segment_size);
	}

	return ret;
}

static void set_memblk_bitmap_online(unsigned long addr)
{
	int seg_idx;
	long cur_blk_bit;

	seg_idx = get_segment_addr_to_idx(addr);
	cur_blk_bit = get_memblk_bits(seg_idx, addr);

	if (cur_blk_bit < 0) {
		pr_err("mem-offline: couldn't get current block bitmap\n");
		return;
	}

	if (segment_infos[seg_idx].bitmask_kernel_blk & cur_blk_bit) {
		pr_warn("mem-offline: memblk 0x%lx in bitmap already onlined\n", addr);
		return;
	}

	segment_infos[seg_idx].bitmask_kernel_blk |= cur_blk_bit;
}

static void set_memblk_bitmap_offline(unsigned long addr)
{
	int seg_idx;
	long cur_blk_bit;

	seg_idx = get_segment_addr_to_idx(addr);
	cur_blk_bit = get_memblk_bits(seg_idx, addr);

	if (cur_blk_bit < 0) {
		pr_err("mem-offline: couldn't get current block bitmap\n");
		return;
	}

	if (!(segment_infos[seg_idx].bitmask_kernel_blk & cur_blk_bit)) {
		pr_warn("mem-offline: memblk 0x%lx in bitmap already offlined\n", addr);
		return;
	}

	segment_infos[seg_idx].bitmask_kernel_blk &= ~cur_blk_bit;
}

static bool need_to_send_remote_request(struct memory_notify *mn,
					enum memory_states request)
{
	int seg_idx;
	unsigned long addr;
	long cur_blk_bit, mask;

	addr = SECTION_ALIGN_DOWN(mn->start_pfn) << PAGE_SHIFT;
	seg_idx = get_segment_addr_to_idx(addr);

	cur_blk_bit = get_memblk_bits(seg_idx, addr);

	if (cur_blk_bit < 0) {
		pr_err("mem-offline: couldn't get current block bitmap\n");
		return false;
	}
	/*
	 * For MEM_OFFLINE, don't send the request if there are other online
	 * blocks in the segment.
	 * For MEM_ONLINE, don't send the request if there is already one
	 * online block in the segment.
	 */


	/* check if other memblocks are ONLINE, if so then return false */
	mask = segment_infos[seg_idx].bitmask_kernel_blk & (~cur_blk_bit);
	if (mask)
		return false;

	return true;
}

/*
 * This returns the number of hotpluggable segments in a memory block.
 */
static int get_num_memblock_hotplug_segments(unsigned long addr)
{
	unsigned long segment_size;
	unsigned long block_size = memory_block_size_bytes();
	unsigned long end_addr = addr + block_size;
	int seg_idx, count = 0;

	seg_idx = get_segment_addr_to_idx(addr);
	segment_size = segment_infos[seg_idx].seg_size;

	if (segment_size >= block_size)
		return 1;

	while ((addr < end_addr) && (addr + segment_size < end_addr)) {
		if (block_size % segment_size) {
			pr_warn("PASR is unusable. Offline granule size should be in multiples for memory_block_size_bytes.\n");
			return 0;
		}
		addr += segment_size;
		seg_idx = get_segment_addr_to_idx(addr);
		segment_size = segment_infos[seg_idx].seg_size;
		count++;
	}

	return count;
}

static int mem_change_refresh_state(struct memory_notify *mn,
				    enum memory_states state)
{
	int start = SECTION_ALIGN_DOWN(mn->start_pfn);
	unsigned long sec_nr = pfn_to_section_nr(start);
	bool online = (state == MEMORY_ONLINE) ? true : false;
	unsigned long idx = (sec_nr - start_section_nr) / sections_per_block;
	int ret, count;
	unsigned long addr;
	int seg_idx;

	if (mem_sec_state[idx] == state) {
		/* we shouldn't be getting this request */
		pr_warn("mem-offline: state of mem%d block already in %s state. Ignoring refresh state change request\n",
				sec_nr, online ? "online" : "offline");
		return 0;
	}

	addr = __pfn_to_phys(SECTION_ALIGN_DOWN(mn->start_pfn));
	seg_idx = get_segment_addr_to_idx(addr);

	count = get_num_memblock_hotplug_segments(addr);
	if (!count)
		return -EINVAL;

	if (!need_to_send_remote_request(mn, state))
		goto out;

	ret = send_msg(mn, online, count);
	if (ret) {
		/* online failures are critical failures */
		if (online)
			BUG_ON(IS_ENABLED(CONFIG_BUG_ON_HW_MEM_ONLINE_FAIL));
		return -EINVAL;
	}
	segment_infos[seg_idx].state = state;
out:
	mem_sec_state[idx] = state;
	return 0;
}

static unsigned long get_section_allocated_memory(unsigned long sec_nr)
{
	unsigned long block_sz = memory_block_size_bytes();
	unsigned long pages_per_blk = block_sz / PAGE_SIZE;
	unsigned long tot_free_pages = 0, pfn, end_pfn;
	unsigned long used;
	struct zone *movable_zone = &NODE_DATA(numa_node_id())->node_zones[ZONE_MOVABLE];
	struct page *page;

	if (!populated_zone(movable_zone))
		return 0;

	pfn = section_nr_to_pfn(sec_nr);
	end_pfn = pfn + pages_per_blk;

	if (!zone_intersects(movable_zone, pfn, pages_per_blk))
		return 0;

	while (pfn < end_pfn) {
		if (!pfn_valid(pfn) || !PageBuddy(pfn_to_page(pfn))) {
			pfn++;
			continue;
		}
		page = pfn_to_page(pfn);
		tot_free_pages += 1 << page_private(page);
		pfn += 1 << page_private(page);
	}

	used = block_sz - (tot_free_pages * PAGE_SIZE);

	return used;
}

static void mem_offline_timeout_cb(struct timer_list *timer)
{
	pr_info("mem-offline: SIGALRM is raised to stop the offline operation\n");
	send_sig_info(SIGALRM, SEND_SIG_PRIV, offline_trig_task);
}

static int mem_event_callback(struct notifier_block *self,
				unsigned long action, void *arg)
{
	struct memory_notify *mn = arg;
	unsigned long start, end, sec_nr;
	static ktime_t cur;
	ktime_t delay = 0;
	phys_addr_t start_addr, end_addr;
	unsigned int idx = end_section_nr - start_section_nr + 1;
	int seg_idx;

	start = SECTION_ALIGN_DOWN(mn->start_pfn);
	end = SECTION_ALIGN_UP(mn->start_pfn + mn->nr_pages);

	if ((start != mn->start_pfn) || (end != mn->start_pfn + mn->nr_pages)) {
		WARN("mem-offline: %s pfn not aligned to section\n", __func__);
		pr_err("mem-offline: start pfn = %lu end pfn = %lu\n",
			mn->start_pfn, mn->start_pfn + mn->nr_pages);
		return -EINVAL;
	}

	start_addr = __pfn_to_phys(start);
	end_addr = __pfn_to_phys(end);
	sec_nr = pfn_to_section_nr(start);

	if (sec_nr > end_section_nr || sec_nr < start_section_nr) {
		if (action == MEM_ONLINE || action == MEM_OFFLINE)
			pr_info("mem-offline: %s mem%ld, but not our block. Not performing any action\n",
				action == MEM_ONLINE ? "Onlined" : "Offlined",
				sec_nr);
		return NOTIFY_OK;
	}

	switch (action) {
	case MEM_GOING_ONLINE:
		pr_debug("mem-offline: MEM_GOING_ONLINE : start = 0x%llx end = 0x%llx\n",
				start_addr, end_addr);
		++mem_info[(sec_nr - start_section_nr + MEMORY_ONLINE *
			   idx) / sections_per_block].fail_count;
		cur = ktime_get();

		if (mem_change_refresh_state(mn, MEMORY_ONLINE))
			return NOTIFY_BAD;

		break;
	case MEM_ONLINE:
		delay = ktime_us_delta(ktime_get(), cur);
		record_stat(sec_nr, delay, MEMORY_ONLINE);
		cur = 0;
		set_memblk_bitmap_online(start_addr);
		pr_info("mem-offline: Onlined memory block mem%pK\n",
			(void *)sec_nr);
		seg_idx = get_segment_addr_to_idx(start_addr);
		pr_debug("mem-offline: Segment %d memblk_bitmap 0x%lx\n",
				seg_idx, segment_infos[seg_idx].bitmask_kernel_blk);
		totalram_pages_add(-(memory_block_size_bytes()/PAGE_SIZE));
		break;
	case MEM_GOING_OFFLINE:
		pr_debug("mem-offline: MEM_GOING_OFFLINE : start = 0x%llx end = 0x%llx\n",
				start_addr, end_addr);
		++mem_info[(sec_nr - start_section_nr + MEMORY_OFFLINE *
			   idx) / sections_per_block].fail_count;
		has_pend_offline_req = true;
		cancel_work_sync(&fill_movable_zone_work);
		offline_trig_task = current;
		mod_timer(&mem_offline_timeout_timer, jiffies + (OFFLINE_TIMEOUT_SEC * HZ));
		cur = ktime_get();
		break;
	case MEM_OFFLINE:
		mem_change_refresh_state(mn, MEMORY_OFFLINE);
		/*
		 * Notifying that something went bad at this stage won't
		 * help since this is the last stage of memory hotplug.
		 */

		delay = ktime_us_delta(ktime_get(), cur);
		record_stat(sec_nr, delay, MEMORY_OFFLINE);
		cur = 0;
		has_pend_offline_req = false;
		set_memblk_bitmap_offline(start_addr);
		pr_info("mem-offline: Offlined memory block mem%pK\n",
			(void *)sec_nr);
		seg_idx = get_segment_addr_to_idx(start_addr);
		pr_debug("mem-offline: Segment %d memblk_bitmap 0x%lx\n",
				seg_idx, segment_infos[seg_idx].bitmask_kernel_blk);
		totalram_pages_add(memory_block_size_bytes()/PAGE_SIZE);
		del_timer_sync(&mem_offline_timeout_timer);
		offline_trig_task = NULL;
		break;
	case MEM_CANCEL_OFFLINE:
		pr_debug("mem-offline: MEM_CANCEL_OFFLINE : start = 0x%llx end = 0x%llx\n",
				start_addr, end_addr);
		del_timer_sync(&mem_offline_timeout_timer);
		offline_trig_task = NULL;
		break;
	case MEM_CANCEL_ONLINE:
		pr_info("mem-offline: MEM_CANCEL_ONLINE: start = 0x%llx end = 0x%llx\n",
				start_addr, end_addr);
		mem_change_refresh_state(mn, MEMORY_OFFLINE);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int mem_online_remaining_blocks(void)
{
	unsigned long memblock_end_pfn = __phys_to_pfn(memblock_end_of_DRAM());
	unsigned long ram_end_pfn = __phys_to_pfn(bootmem_dram_end_addr - 1);
	unsigned long block_size, memblock, pfn;
	unsigned int nid, delta;
	phys_addr_t phys_addr;
	int fail = 0;

	pr_debug("mem-offline: memblock_end_of_DRAM 0x%lx\n", memblock_end_of_DRAM());

	block_size = memory_block_size_bytes();
	sections_per_block = block_size / MIN_MEMORY_BLOCK_SIZE;

	start_section_nr = pfn_to_section_nr(memblock_end_pfn);
	end_section_nr = pfn_to_section_nr(ram_end_pfn);

	if (memblock_end_of_DRAM() >= bootmem_dram_end_addr) {
		pr_info("mem-offline: System booted with no zone movable memory blocks. Cannot perform memory offlining\n");
		return -EINVAL;
	}

	if (memblock_end_of_DRAM() % block_size) {
		delta = block_size - (memblock_end_of_DRAM() % block_size);
		pr_err("mem-offline: !!ERROR!! memblock end of dram address is not aligned to memory block size!\n");
		pr_err("mem-offline: memory%lu could be partially available. %lukB of memory will be missing from RAM!\n",
				start_section_nr, delta / SZ_1K);

		/*
		 * since this section is partially added during boot, we cannot
		 * add the remaining part of section using add_memory since it
		 * won't be size aligned to block size. We have to start the
		 * offlinable region from the next section onwards.
		 */
		start_section_nr += 1;

	}

	if (bootmem_dram_end_addr % block_size) {
		delta = bootmem_dram_end_addr % block_size;
		pr_err("mem-offline: !!ERROR!! bootmem end of dram address is not aligned to memory block size!\n");
		pr_err("mem-offline: memory%lu will not be added. %lukB of memory will be missing from RAM!\n",
				end_section_nr, delta / SZ_1K);

		/*
		 * since this section cannot be added, the last section of offlinable
		 * region will be the previous section.
		 */
		end_section_nr -= 1;
	}

	offlinable_region_start_addr =
		section_nr_to_pfn(__pfn_to_phys(start_section_nr));

	/*
	 * below check holds true if there were only one offlinable section
	 * and that was partially added during boot. In such case, bail out.
	 */
	if (start_section_nr > end_section_nr)
		return 1;

	pr_debug("mem-offline: offlinable_region_start_addr 0X%lx\n",
		offlinable_region_start_addr);

	for (memblock = start_section_nr; memblock <= end_section_nr;
			memblock += sections_per_block) {
		if (!test_bit(memblock - start_section_nr, movable_bitmap))
			continue;

		pfn = section_nr_to_pfn(memblock);
		phys_addr = __pfn_to_phys(pfn);

		if (phys_addr & (((PAGES_PER_SECTION * sections_per_block)
					<< PAGE_SHIFT) - 1)) {
			fail = 1;
			pr_warn("mem-offline: PFN of mem%lu block not aligned to section start. Not adding this memory block\n",
								memblock);
			continue;
		}
		nid = memory_add_physaddr_to_nid(phys_addr);
		if (add_memory(nid, phys_addr,
				 MIN_MEMORY_BLOCK_SIZE * sections_per_block, MHP_NONE)) {
			pr_warn("mem-offline: Adding memory block mem%lu failed\n",
								memblock);
			fail = 1;
		}
	}

	return fail;
}

static ssize_t show_block_allocated_bytes(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned long allocd_mem = 0;
	unsigned long sec_nr;
	int ret;

	ret = sscanf(kobject_name(kobj), MEMBLOCK_NAME, &sec_nr);
	if (ret != 1) {
		pr_err("mem-offline: couldn't get memory block number! ret %d\n", ret);
		return 0;
	}

	allocd_mem = get_section_allocated_memory(sec_nr);

	return scnprintf(buf, BUF_LEN, "%lu\n", allocd_mem);
}

static ssize_t show_seg_memblk_start(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned long memblk_start;
	unsigned long seg_nr;
	int ret;

	ret = sscanf(kobject_name(kobj), SEGMENT_NAME, &seg_nr);
	if (ret != 1) {
		pr_err("mem-offline: couldn't get segment number! ret %d\n", ret);
		return 0;
	}

	memblk_start =
		pfn_to_section_nr(PFN_DOWN(segment_infos[seg_nr].start_addr));

	return scnprintf(buf, BUF_LEN, "%lu\n", memblk_start);
}

static ssize_t show_num_memblks(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned long num_memblks;
	unsigned long seg_nr;
	int ret;

	ret = sscanf(kobject_name(kobj), SEGMENT_NAME, &seg_nr);
	if (ret != 1) {
		pr_err("mem-offline: couldn't get num_memblks! ret %d\n", ret);
		return 0;
	}
	num_memblks = segment_infos[seg_nr].num_kernel_blks;

	return scnprintf(buf, BUF_LEN, "%lu\n", num_memblks);
}

static ssize_t show_seg_size(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned long seg_size;
	unsigned long seg_nr;
	int ret;

	ret = sscanf(kobject_name(kobj), SEGMENT_NAME, &seg_nr);
	if (ret != 1) {
		pr_err("mem-offline: couldn't get segment size! ret %d\n", ret);
		return 0;
	}
	seg_size = segment_infos[seg_nr].seg_size;

	return scnprintf(buf, BUF_LEN, "%lu\n", seg_size);
}

static ssize_t show_mem_offline_granule(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%lu\n",
			(unsigned long)offline_granule * SZ_1M);
}

static ssize_t show_differing_seg_sizes(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%lu\n",
			(unsigned int)differing_segment_sizes);
}

static ssize_t show_num_segments(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%lu\n",
			(unsigned long)num_segments);
}

static unsigned int print_blk_residency_percentage(char *buf, size_t sz,
			unsigned int tot_blks, ktime_t *total_time,
			enum memory_states mode)
{
	unsigned int i;
	unsigned int c = 0;
	int percent;
	unsigned int idx = tot_blks + 1;

	for (i = 0; i <= tot_blks; i++) {
		percent = (int)ktime_divns(total_time[i + mode * idx] * 100,
			ktime_add(total_time[i + MEMORY_ONLINE * idx],
					total_time[i + MEMORY_OFFLINE * idx]));

		c += scnprintf(buf + c, sz - c, "%d%%\t\t", percent);
	}
	return c;
}
static unsigned int print_blk_residency_times(char *buf, size_t sz,
			unsigned int tot_blks, ktime_t *total_time,
			enum memory_states mode)
{
	unsigned int i;
	unsigned int c = 0;
	ktime_t now, delta;
	unsigned int idx = tot_blks + 1;

	now = ktime_get();
	for (i = 0; i <= tot_blks; i++) {
		if (mem_sec_state[i] == mode)
			delta = ktime_sub(now,
				mem_info[i + mode * idx].resident_since);
		else
			delta = 0;
		delta = ktime_add(delta,
			mem_info[i + mode * idx].resident_time);
		c += scnprintf(buf + c, sz - c, "%lus\t\t",
				ktime_to_us(delta) / USEC_PER_SEC);
		total_time[i + mode * idx] = delta;
	}
	return c;
}

static ssize_t show_mem_stats(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{

	unsigned int blk_start = start_section_nr / sections_per_block;
	unsigned int blk_end = end_section_nr / sections_per_block;
	unsigned int tot_blks = blk_end - blk_start;
	ktime_t *total_time;
	unsigned int idx = tot_blks + 1;
	unsigned int c = 0;
	unsigned int i, j;

	size_t sz = PAGE_SIZE;
	ktime_t total = 0, total_online = 0, total_offline = 0;

	total_time = kcalloc(idx * MAX_STATE, sizeof(*total_time), GFP_KERNEL);

	if (!total_time)
		return -ENOMEM;

	for (j = 0; j < MAX_STATE; j++) {
		c += scnprintf(buf + c, sz - c,
			"\n\t%s\n\t\t\t", j == 0 ? "ONLINE" : "OFFLINE");
		for (i = blk_start; i <= blk_end; i++)
			c += scnprintf(buf + c, sz - c,
							"%s%d\t\t", "mem", i);
		c += scnprintf(buf + c, sz - c, "\n");
		c += scnprintf(buf + c, sz - c,
							"\tLast recd time:\t");
		for (i = 0; i <= tot_blks; i++)
			c += scnprintf(buf + c, sz - c, "%luus\t\t",
				mem_info[i + j * idx].last_recorded_time);
		c += scnprintf(buf + c, sz - c, "\n");
		c += scnprintf(buf + c, sz - c,
							"\tAvg time:\t");
		for (i = 0; i <= tot_blks; i++)
			c += scnprintf(buf + c, sz - c,
				"%luus\t\t", mem_info[i + j * idx].avg_time);
		c += scnprintf(buf + c, sz - c, "\n");
		c += scnprintf(buf + c, sz - c,
							"\tBest time:\t");
		for (i = 0; i <= tot_blks; i++)
			c += scnprintf(buf + c, sz - c,
				"%luus\t\t", mem_info[i + j * idx].best_time);
		c += scnprintf(buf + c, sz - c, "\n");
		c += scnprintf(buf + c, sz - c,
							"\tWorst time:\t");
		for (i = 0; i <= tot_blks; i++)
			c += scnprintf(buf + c, sz - c,
				"%luus\t\t", mem_info[i + j * idx].worst_time);
		c += scnprintf(buf + c, sz - c, "\n");
		c += scnprintf(buf + c, sz - c,
							"\tSuccess count:\t");
		for (i = 0; i <= tot_blks; i++)
			c += scnprintf(buf + c, sz - c,
				"%lu\t\t", mem_info[i + j * idx].success_count);
		c += scnprintf(buf + c, sz - c, "\n");
		c += scnprintf(buf + c, sz - c,
							"\tFail count:\t");
		for (i = 0; i <= tot_blks; i++)
			c += scnprintf(buf + c, sz - c,
				"%lu\t\t", mem_info[i + j * idx].fail_count);
		c += scnprintf(buf + c, sz - c, "\n");
	}

	c += scnprintf(buf + c, sz - c, "\n");
	c += scnprintf(buf + c, sz - c, "\tState:\t\t");
	for (i = 0; i <= tot_blks; i++) {
		c += scnprintf(buf + c, sz - c, "%s\t\t",
			mem_sec_state[i] == MEMORY_ONLINE ?
			"Online" : "Offline");
	}
	c += scnprintf(buf + c, sz - c, "\n");

	c += scnprintf(buf + c, sz - c, "\n");
	c += scnprintf(buf + c, sz - c, "\tOnline time:\t");
	c += print_blk_residency_times(buf + c, sz - c,
			tot_blks, total_time, MEMORY_ONLINE);


	c += scnprintf(buf + c, sz - c, "\n");
	c += scnprintf(buf + c, sz - c, "\tOffline time:\t");
	c += print_blk_residency_times(buf + c, sz - c,
			tot_blks, total_time, MEMORY_OFFLINE);

	c += scnprintf(buf + c, sz - c, "\n");

	c += scnprintf(buf + c, sz - c, "\n");
	c += scnprintf(buf + c, sz - c, "\tOnline %%:\t");
	c += print_blk_residency_percentage(buf + c, sz - c,
			tot_blks, total_time, MEMORY_ONLINE);

	c += scnprintf(buf + c, sz - c, "\n");
	c += scnprintf(buf + c, sz - c, "\tOffline %%:\t");
	c += print_blk_residency_percentage(buf + c, sz - c,
			tot_blks, total_time, MEMORY_OFFLINE);
	c += scnprintf(buf + c, sz - c, "\n");
	c += scnprintf(buf + c, sz - c, "\n");

	for (i = 0; i <= tot_blks; i++)
		total = ktime_add(total,
			ktime_add(total_time[i + MEMORY_ONLINE * idx],
					total_time[i + MEMORY_OFFLINE * idx]));

	for (i = 0; i <= tot_blks; i++)
		total_online =  ktime_add(total_online,
				total_time[i + MEMORY_ONLINE * idx]);

	total_offline = ktime_sub(total, total_online);

	c += scnprintf(buf + c, sz - c,
					"\tAvg Online %%:\t%d%%\n",
					((int)total_online * 100) / total);
	c += scnprintf(buf + c, sz - c,
					"\tAvg Offline %%:\t%d%%\n",
					((int)total_offline * 100) / total);

	c += scnprintf(buf + c, sz - c, "\n");
	kfree(total_time);
	return c;
}

static ssize_t show_anon_migrate(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%lu\n",
				atomic_read(&target_migrate_pages));
}

static ssize_t store_anon_migrate(struct kobject *kobj,
				struct kobj_attribute *attr, const char *buf,
				size_t size)
{
	int val = 0, ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	atomic_add(val, &target_migrate_pages);

	if (!work_pending(&fill_movable_zone_work))
		queue_work(migrate_wq, &fill_movable_zone_work);

	return size;
}

static unsigned long get_anon_movable_pages(
			struct movable_zone_fill_control *fc,
			unsigned long start_pfn,
			unsigned long end_pfn, struct list_head *list)
{
	int found = 0, pfn, ret;
	int limit = min_t(int, fc->target, (int)pageblock_nr_pages);

	fc->nr_migrate_pages = 0;
	for (pfn = start_pfn; pfn < end_pfn && found < limit; ++pfn) {
		struct page *page = pfn_to_page(pfn);

		if (!pfn_valid(pfn))
			continue;

		if (PageCompound(page)) {
			struct page *head = compound_head(page);
			int skip;

			skip = (1 << compound_order(head)) - (page - head);
			pfn += skip - 1;
			continue;
		}

		if (PageBuddy(page)) {
			unsigned long freepage_order;

			freepage_order = READ_ONCE(page_private(page));
			if (freepage_order > 0 && freepage_order < MAX_ORDER)
				pfn += (1 << page_private(page)) - 1;
			continue;
		}

		if (!(pfn % pageblock_nr_pages) &&
			get_pageblock_migratetype(page) == MIGRATE_CMA) {
			pfn += pageblock_nr_pages - 1;
			continue;
		}

		ret = isolate_anon_lru_page(page);
		if (ret)
			continue;

		list_add_tail(&page->lru, list);
		inc_node_page_state(page, NR_ISOLATED_ANON +
			page_is_file_lru(page));
		found++;
		++fc->nr_migrate_pages;
	}

	return pfn;
}

static void prepare_fc(struct movable_zone_fill_control *fc)
{
	struct zone *zone;

	zone = &(NODE_DATA(0)->node_zones[ZONE_MOVABLE]);
	fc->zone = zone;
	fc->start_pfn = ALIGN(zone->zone_start_pfn, pageblock_nr_pages);
	fc->end_pfn = zone_end_pfn(zone);
	fc->limit = atomic64_read(&zone->managed_pages);
	INIT_LIST_HEAD(&fc->freepages);
}


static void release_freepages(struct list_head *freelist)
{
	struct page *page, *next;

	list_for_each_entry_safe(page, next, freelist, lru) {
		list_del(&page->lru);
		__free_page(page);
	}
}

static void isolate_free_pages(struct movable_zone_fill_control *fc)
{
	struct page *page;
	unsigned long flags;
	unsigned long start_pfn = fc->start_pfn;
	unsigned long end_pfn = fc->end_pfn;
	LIST_HEAD(tmp);
	struct zone *dst_zone;

	if (!(start_pfn < end_pfn))
		return;

	dst_zone = page_zone(pfn_to_page(start_pfn));
	if (zone_page_state(dst_zone, NR_FREE_PAGES) < high_wmark_pages(dst_zone))
		return;

	spin_lock_irqsave(&fc->zone->lock, flags);
	for (; start_pfn < end_pfn; start_pfn++) {
		unsigned long isolated;

		if (!pfn_valid(start_pfn))
			continue;

		page = pfn_to_page(start_pfn);
		if (!page)
			continue;

		if (PageCompound(page)) {
			struct page *head = compound_head(page);
			int skip;

			skip = (1 << compound_order(head)) - (page - head);
			start_pfn += skip - 1;
			continue;
		}

		if (!(start_pfn % pageblock_nr_pages) &&
			get_pageblock_migratetype(page) == MIGRATE_ISOLATE) {
			start_pfn += pageblock_nr_pages - 1;
			continue;
		}
		/*
		 * Make sure that the zone->lock is not held for long by
		 * returning once we have SWAP_CLUSTER_MAX pages in the
		 * free list for migration.
		 */
		if (!(start_pfn % pageblock_nr_pages) &&
			(fc->nr_free_pages >= SWAP_CLUSTER_MAX ||
			 has_pend_offline_req))
			break;

		if (!PageBuddy(page))
			continue;

		INIT_LIST_HEAD(&tmp);
		isolated = isolate_and_split_free_page(page, &tmp);
		if (!isolated) {
			fc->start_pfn = ALIGN(fc->start_pfn, pageblock_nr_pages);
			goto out;
		}

		list_splice(&tmp, &fc->freepages);
		fc->nr_free_pages += isolated;
		start_pfn += isolated - 1;
	}
	fc->start_pfn = start_pfn;
out:
	spin_unlock_irqrestore(&fc->zone->lock, flags);
}

static struct page *movable_page_alloc(struct page *page, unsigned long data)
{
	struct movable_zone_fill_control *fc;
	struct page *freepage;

	fc = (struct movable_zone_fill_control *)data;
	if (list_empty(&fc->freepages)) {
		isolate_free_pages(fc);
		if (list_empty(&fc->freepages))
			return NULL;
	}

	freepage = list_entry(fc->freepages.next, struct page, lru);
	list_del(&freepage->lru);
	fc->nr_free_pages--;

	return freepage;
}

static void movable_page_free(struct page *page, unsigned long data)
{
	struct movable_zone_fill_control *fc;

	fc = (struct movable_zone_fill_control *)data;
	list_add(&page->lru, &fc->freepages);
	fc->nr_free_pages++;
}


static void fill_movable_zone_fn(struct work_struct *work)
{
	unsigned long start_pfn, end_pfn;
	unsigned long movable_highmark;
	struct zone *normal_zone = &(NODE_DATA(0)->node_zones[ZONE_NORMAL]);
	struct zone *movable_zone = &(NODE_DATA(0)->node_zones[ZONE_MOVABLE]);
	LIST_HEAD(source);
	int ret, free;
	struct movable_zone_fill_control fc = { {0} };
	unsigned long timeout = MIGRATE_TIMEOUT_SEC * HZ, expire;

	start_pfn = normal_zone->zone_start_pfn;
	end_pfn = zone_end_pfn(normal_zone);
	movable_highmark = high_wmark_pages(movable_zone);

	if (has_pend_offline_req)
		return;

	if (!mutex_trylock(&page_migrate_lock))
		return;
	prepare_fc(&fc);
	if (!fc.limit)
		goto out;
	expire = jiffies + timeout;
restart:
	fc.target = atomic_xchg(&target_migrate_pages, 0);
	if (!fc.target)
		goto out;
repeat:
	cond_resched();
	if (time_after(jiffies, expire))
		goto out;
	free = zone_page_state(movable_zone, NR_FREE_PAGES);
	if (free - fc.target <= movable_highmark)
		fc.target = free - movable_highmark;
	if (fc.target <= 0)
		goto out;

	start_pfn = get_anon_movable_pages(&fc, start_pfn, end_pfn, &source);
	if (list_empty(&source) && start_pfn < end_pfn)
		goto repeat;

	ret = migrate_pages(&source, movable_page_alloc, movable_page_free,
		(unsigned long) &fc, MIGRATE_ASYNC, MR_MEMORY_HOTPLUG, NULL);
	if (ret)
		putback_movable_pages(&source);

	fc.target -= fc.nr_migrate_pages;
	if (ret == -ENOMEM || start_pfn >= end_pfn || has_pend_offline_req)
		goto out;
	else if (fc.target <= 0)
		goto restart;

	goto repeat;
out:
	if (fc.nr_free_pages > 0)
		release_freepages(&fc.freepages);
	mutex_unlock(&page_migrate_lock);
}

static struct kobj_attribute stats_attr =
		__ATTR(stats, 0444, show_mem_stats, NULL);

static struct kobj_attribute offline_granule_attr =
		__ATTR(offline_granule, 0444, show_mem_offline_granule, NULL);

static struct kobj_attribute anon_migration_size_attr =
		__ATTR(anon_migrate, 0644, show_anon_migrate, store_anon_migrate);

static struct attribute *mem_root_attrs[] = {
		&stats_attr.attr,
		&offline_granule_attr.attr,
		&anon_migration_size_attr.attr,
		NULL,
};

static struct attribute_group mem_attr_group = {
	.attrs = mem_root_attrs,
};

/* memblock allocated bytes attribute group */
static struct kobj_attribute block_allocated_bytes_attr =
		__ATTR(allocated_bytes, 0444, show_block_allocated_bytes, NULL);

static struct attribute *mem_block_attrs[] = {
		&block_allocated_bytes_attr.attr,
		NULL,
};

static struct attribute_group mem_block_attr_group = {
	.attrs = mem_block_attrs,
};

/* differing segment attribute group */
static struct kobj_attribute differing_seg_sizes_attr =
		__ATTR(differing_seg_sizes, 0444, show_differing_seg_sizes, NULL);

static struct kobj_attribute num_segments_attr =
		__ATTR(num_segment, 0444, show_num_segments, NULL);

static struct attribute *differing_segments_attrs[] = {
		&differing_seg_sizes_attr.attr,
		&num_segments_attr.attr,
		NULL,
};

static struct attribute_group differing_segments_attr_group = {
	.attrs = differing_segments_attrs,
};

/* segment info attribute group */
static struct kobj_attribute seg_memblk_start_attr =
		__ATTR(memblk_start, 0444, show_seg_memblk_start, NULL);

static struct kobj_attribute seg_num_memblks_attr =
		__ATTR(num_memblks, 0444, show_num_memblks, NULL);

static struct kobj_attribute seg_size_attr =
		__ATTR(seg_size, 0444, show_seg_size, NULL);

static struct attribute *seg_info_attrs[] = {
		&seg_memblk_start_attr.attr,
		&seg_num_memblks_attr.attr,
		&seg_size_attr.attr,
		NULL,
};

static struct attribute_group seg_info_attr_group = {
	.attrs = seg_info_attrs,
};

static int mem_sysfs_create_seginfo(struct kobject *parent_kobj)
{
	struct kobject *segment_kobj, *seg_info_kobj;
	char segmentstr[BUF_LEN];
	unsigned long segnum;
	int ret;

	if (sysfs_create_group(parent_kobj, &differing_segments_attr_group)) {
		kobject_put(kobj);
		return -EINVAL;
	}

	ret = scnprintf(segmentstr, sizeof(segmentstr), "seg_info");
	seg_info_kobj = kobject_create_and_add(segmentstr, parent_kobj);
	if (!seg_info_kobj)
		return -ENOMEM;

	for (segnum = 0; segnum < num_segments; segnum++) {
		ret = scnprintf(segmentstr, sizeof(segmentstr), SEGMENT_NAME, segnum);
		segment_kobj = kobject_create_and_add(segmentstr, seg_info_kobj);
		if (!segment_kobj)
			return -ENOMEM;

		if (sysfs_create_group(segment_kobj, &seg_info_attr_group))
			kobject_put(segment_kobj);
	}

	return 0;
}
static int mem_sysfs_create_memblocks(struct kobject *parent_kobj)
{
	struct kobject *memblk_kobj;
	char memblkstr[BUF_LEN];
	unsigned long memblock;
	int ret;

	for (memblock = start_section_nr; memblock <= end_section_nr;
			memblock += sections_per_block) {
		ret = scnprintf(memblkstr, sizeof(memblkstr), MEMBLOCK_NAME, memblock);
		if (ret <= 0)
			return -EINVAL;
		memblk_kobj = kobject_create_and_add(memblkstr, parent_kobj);
		if (!memblk_kobj)
			return -ENOMEM;
		if (sysfs_create_group(memblk_kobj, &mem_block_attr_group))
			kobject_put(memblk_kobj);
	}

	return 0;
}

static int mem_sysfs_init(void)
{
	if (start_section_nr == end_section_nr)
		return -EINVAL;

	kobj = kobject_create_and_add(MODULE_CLASS_NAME, kernel_kobj);
	if (!kobj)
		return -ENOMEM;

	if (sysfs_create_group(kobj, &mem_attr_group))
		kobject_put(kobj);

	if (mem_sysfs_create_memblocks(kobj)) {
		pr_err("mem-offline: failed to create memblock sysfs nodes\n");
		return -EINVAL;
	}

	/* create sysfs nodes for segment info if ddr has differing segment sizes */
	if (differing_segment_sizes && mem_sysfs_create_seginfo(kobj)) {
		pr_err("mem-offline: failed to create seginfo sysfs nodes\n");
		return -EINVAL;
	}

	return 0;
}

static int mem_parse_dt(struct platform_device *pdev)
{
	const __be32 *val;
	struct device_node *node = pdev->dev.of_node;

	val = of_get_property(node, "granule", NULL);
	if (!val) {
		pr_err("mem-offine: granule property not found in DT\n");
		return -EINVAL;
	}
	if (!*val) {
		pr_err("mem-offine: invalid granule property\n");
		return -EINVAL;
	}
	offline_granule = be32_to_cpup(val);
	if (!offline_granule || (offline_granule & (offline_granule - 1)) ||
		((offline_granule * SZ_1M < MIN_MEMORY_BLOCK_SIZE) &&
		 (MIN_MEMORY_BLOCK_SIZE % (offline_granule * SZ_1M)))) {
		pr_err("mem-offine: invalid granule property\n");
		return -EINVAL;
	}

	if (!of_find_property(node, "mboxes", NULL)) {
		is_rpm_controller = true;
		return 0;
	}

	mailbox.cl.dev = &pdev->dev;
	mailbox.cl.tx_block = true;
	mailbox.cl.tx_tout = 1000;
	mailbox.cl.knows_txdone = false;

	mailbox.mbox = mbox_request_channel(&mailbox.cl, 0);
	if (IS_ERR(mailbox.mbox)) {
		if (PTR_ERR(mailbox.mbox) != -EPROBE_DEFER)
			pr_err("mem-offline: failed to get mailbox channel %pK %ld\n",
				mailbox.mbox, PTR_ERR(mailbox.mbox));
		return PTR_ERR(mailbox.mbox);
	}

	return 0;
}

static struct notifier_block hotplug_memory_callback_nb = {
	.notifier_call = mem_event_callback,
	.priority = 0,
};

static unsigned int get_num_offlinable_segments(void)
{
	uint8_t r = 0; // region index
	unsigned long region_end, segment_start, segment_size, addr;
	unsigned int count = 0;

	/* iterate through regions */
	for (r = 0; r < num_ddr_regions; r++) {

		region_end = ddr_regions[r].start_address + ddr_regions[r].length;

		/* Calculate segment starting address */
		segment_start = ddr_regions[r].start_address +
				ddr_regions[r].segments_start_offset;

		/* If DDR region granule_size is 0, this region cannot be offlined */
		if (!ddr_regions[r].granule_size)
			continue;

		/* Calculate size of segments in bytes */
		segment_size  = ddr_regions[r].granule_size << 20;

		/* now iterate through segments within the region */
		for (addr = segment_start; addr < region_end; addr += segment_size) {

			/* Check if segment extends beyond region */
			if ((addr + segment_size) > region_end)
				break;

			/* populate segment info only for ones in offlinable region */
			if (addr < offlinable_region_start_addr)
				continue;

			count++;
		}
	}

	return count;
}

static int get_segment_region_info(void)
{
	uint8_t r = 0; // region index
	unsigned long region_end, segment_start, segment_size, r0_segment_size;
	unsigned long num_kernel_blks, addr;
	int i, seg_idx = 0;

	num_segments = get_num_offlinable_segments();

	segment_infos = kcalloc(num_segments, sizeof(*segment_infos), GFP_KERNEL);
	if (!segment_infos)
		return -ENOMEM;

	for (i = 0; i < num_segments; i++)
		segment_infos[i].start_addr = -1;

	r0_segment_size = ddr_regions[0].granule_size << 20;

	/* iterate through regions */
	for (r = 0; r < num_ddr_regions; r++) {

		region_end = ddr_regions[r].start_address + ddr_regions[r].length;

		/* Calculate segment starting address */
		segment_start = ddr_regions[r].start_address +
				ddr_regions[r].segments_start_offset;

		/* Calculate size of segments in bytes */
		segment_size  = ddr_regions[r].granule_size << 20;

		/* If DDR region granule_size is 0, this region cannot be offlined */
		if (!segment_size)
			continue;

		/* Check if we have diferring segment sizes */
		if (r0_segment_size != segment_size)
			differing_segment_sizes = 1;

		/* now iterate through segments within the region */
		for (addr = segment_start; addr < region_end; addr += segment_size) {

			/* Check if segment extends beyond region */
			if ((addr + segment_size) > region_end)
				break;

			/* populate segment info only for ones in offlinable region */
			if (addr < offlinable_region_start_addr)
				continue;

			if (segment_size > memory_block_size_bytes())
				num_kernel_blks = segment_size / memory_block_size_bytes();
			else
				num_kernel_blks = 1;

			segment_infos[seg_idx].start_addr = addr;
			segment_infos[seg_idx].seg_size = segment_size;
			segment_infos[seg_idx].num_kernel_blks = num_kernel_blks;

			segment_infos[seg_idx].bitmask_kernel_blk =
				GENMASK_ULL(num_kernel_blks - 1, 0);

			seg_idx++;
		}
	}

	if (differing_segment_sizes)
		pr_info("mem-offline: system has DDR type of differing segment sizes\n");

	return 0;
}

static unsigned int get_num_ddr_regions(struct device_node *node)
{
	int i, len;
	char str[20];

	for (i = 0; i < MAX_NUM_DDR_REGIONS; i++) {

		snprintf(str, sizeof(str), "region%d", i);
		if (!of_find_property(node, str, &len))
			break;
	}

	return i;
}

static int get_ddr_regions_info(void)
{
	struct device_node *node;
	struct property *prop;
	int len, num_cells;
	u64 val;
	int nr_address_cells;
	const __be32 *pos;
	char str[20];
	int i;

	node = of_find_node_by_name(of_root, "ddr-regions");
	if (!node) {
		pr_err("mem-offine: ddr-regions node not found in DT\n");
		return -EINVAL;
	}

	num_ddr_regions = get_num_ddr_regions(node);

	if (!num_ddr_regions) {
		pr_err("mem-offine: num_ddr_regions is %d\n", num_ddr_regions);
		return -EINVAL;
	}

	ddr_regions = kcalloc(num_ddr_regions, sizeof(*ddr_regions), GFP_KERNEL);
	if (!ddr_regions)
		return -ENOMEM;

	nr_address_cells = of_n_addr_cells(of_root);

	for (i = 0; i < num_ddr_regions; i++) {

		snprintf(str, sizeof(str), "region%d", i);
		prop = of_find_property(node, str, &len);

		if (!prop)
			return -EINVAL;

		num_cells = len / sizeof(__be32);
		if (num_cells != DDR_REGIONS_NUM_CELLS)
			return -EINVAL;

		pos = prop->value;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].start_address = val;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].length = val;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].segments_start_offset = val;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].segments_start_idx = val;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].granule_size = val;
	}

	for (i = 0; i < num_ddr_regions; i++) {

		pr_info("region%d: seg_start 0x%lx len 0x%lx granule 0x%lx seg_start_offset 0x%lx seg_start_idx 0x%lx\n",
				i, ddr_regions[i].start_address, ddr_regions[i].length,
				ddr_regions[i].granule_size,
				ddr_regions[i].segments_start_offset,
				ddr_regions[i].segments_start_idx);
	}

	return 0;
}

static int check_segment_granule_alignment(void)
{
	int seg_idx;
	unsigned long granule_size;

	for (seg_idx = 0; seg_idx < num_segments; seg_idx++) {
		granule_size = segment_infos[seg_idx].seg_size;

		if (granule_size & (granule_size - 1)) {
			pr_err("mem-offline: invalid granule property for segment %d granule_size 0x%lx\n",
					seg_idx, granule_size);
			return -EINVAL;
		}

		/* check for granule size alignment */
		if (((granule_size < MIN_MEMORY_BLOCK_SIZE) &&
			(MIN_MEMORY_BLOCK_SIZE % granule_size)) ||
			((granule_size > MIN_MEMORY_BLOCK_SIZE) &&
			(granule_size % MIN_MEMORY_BLOCK_SIZE))) {
			pr_err("mem-offline: granule size for segment %d granule_size 0x%lx is not aligned to memblock size\n",
					seg_idx, granule_size);
			return -EINVAL;
		}
	}

	return 0;
}

static int update_dram_end_address_and_movable_bitmap(phys_addr_t *bootmem_dram_end_addr)
{
	struct device_node *node;
	struct property *prop;
	int len, num_cells, num_entries;
	u64 addr = 0, max_base = 0;
	u64 size, base, end, section_size;
	u64 movable_start;
	int nr_address_cells, nr_size_cells;
	const __be32 *pos;

	node = of_find_node_by_name(of_root, "memory");
	if (!node) {
		pr_err("mem-offine: memory node not found in DT\n");
		return -EINVAL;
	}

	nr_address_cells = of_n_addr_cells(of_root);
	nr_size_cells = of_n_size_cells(of_root);

	prop = of_find_property(node, "reg", &len);
	if (!prop) {
		pr_err("mem-offine: reg node not found in DT\n");
		return -EINVAL;
	}

	num_cells = len / sizeof(__be32);
	num_entries = num_cells / (nr_address_cells + nr_size_cells);

	pos = prop->value;

	section_size = MIN_MEMORY_BLOCK_SIZE;
	movable_start = memblock_end_of_DRAM();

	while (num_entries--) {
		base = of_read_number(pos, nr_address_cells);
		size = of_read_number(pos + nr_address_cells, nr_size_cells);
		pos += nr_address_cells + nr_size_cells;

		if (base > max_base) {
			max_base = base;
			addr = base + size;
		}
	}

	*bootmem_dram_end_addr = addr;
	pr_debug("mem-offline: bootmem_dram_end_addr 0x%lx\n", *bootmem_dram_end_addr);

	num_entries = num_cells / (nr_address_cells + nr_size_cells);
	pos = prop->value;
	while (num_entries--) {
		u64 new_base, new_end;
		u64 new_start_bitmap, bitmap_size;

		base = of_read_number(pos, nr_address_cells);
		size = of_read_number(pos + nr_address_cells, nr_size_cells);
		pos += nr_address_cells + nr_size_cells;
		end = base + size;

		if (end <= movable_start)
			continue;

		if (base < movable_start)
			new_base = movable_start;
		else
			new_base = base;
		new_end = end;

		new_start_bitmap = (new_base - movable_start) / section_size;
		bitmap_size = (new_end - new_base) / section_size;
		bitmap_set(movable_bitmap, new_start_bitmap, bitmap_size);
	}

	pr_debug("mem-offline: movable_bitmap is %lx\n", *movable_bitmap);
	return 0;
}

static int mem_offline_driver_probe(struct platform_device *pdev)
{
	unsigned int total_blks;
	int ret, i;
	ktime_t now;

	if (nopasr) {
		pr_info("mem-offline: nopasr mode enabled. Skipping probe\n");
		return 0;
	}

	ret = mem_parse_dt(pdev);
	if (ret)
		return ret;

	ret = update_dram_end_address_and_movable_bitmap(&bootmem_dram_end_addr);
	if (ret)
		return ret;

	ret = mem_online_remaining_blocks();
	if (ret < 0)
		return -ENODEV;

	if (ret > 0)
		pr_err("mem-offline: !!ERROR!! Auto onlining some memory blocks failed. System could run with less RAM\n");

	ret = get_ddr_regions_info();
	if (ret)
		return ret;

	ret = get_segment_region_info();
	if (ret)
		return ret;

	ret = check_segment_granule_alignment();
	if (ret)
		return ret;

	pr_info("mem-offline: num_ddr_regions %d num_segments %d\n",
			num_ddr_regions, num_segments);

	total_blks = (end_section_nr - start_section_nr + 1) /
			sections_per_block;
	mem_info = kcalloc(total_blks * MAX_STATE, sizeof(*mem_info),
			   GFP_KERNEL);
	if (!mem_info)
		return -ENOMEM;

	/* record time of online for all blocks */
	now = ktime_get();
	for (i = 0; i < total_blks; i++)
		mem_info[i].resident_since = now;

	mem_sec_state = kcalloc(total_blks, sizeof(*mem_sec_state), GFP_KERNEL);
	if (!mem_sec_state) {
		ret = -ENOMEM;
		goto err_free_mem_info;
	}

	/* we assume that hardware state of mem blocks are online after boot */
	for (i = 0; i < total_blks; i++)
		mem_sec_state[i] = MEMORY_ONLINE;

	if (mem_sysfs_init()) {
		ret = -ENODEV;
		goto err_free_mem_sec_state;
	}

	if (register_hotmemory_notifier(&hotplug_memory_callback_nb)) {
		pr_err("mem-offline: Registering memory hotplug notifier failed\n");
		ret = -ENODEV;
		goto err_sysfs_remove_group;
	}
	pr_info("mem-offline: Added memory blocks ranging from mem%lu - mem%lu\n",
			start_section_nr, end_section_nr);

	if (bypass_send_msg)
		pr_info("mem-offline: bypass mode\n");

	migrate_wq = alloc_workqueue("reverse_migrate_wq",
			WQ_UNBOUND | WQ_FREEZABLE, 0);
	if (!migrate_wq) {
		pr_err("Failed to create the worker for reverse migration\n");
		ret = -ENOMEM;
		goto err_sysfs_remove_group;
	}

	return 0;

err_sysfs_remove_group:
	sysfs_remove_group(kobj, &mem_attr_group);
	kobject_put(kobj);
err_free_mem_sec_state:
	kfree(mem_sec_state);
err_free_mem_info:
	kfree(mem_info);
	return ret;
}

static const struct of_device_id mem_offline_match_table[] = {
	{.compatible = "qcom,mem-offline"},
	{}
};

MODULE_DEVICE_TABLE(of, mem_offline_match_table);

static struct platform_driver mem_offline_driver = {
	.probe = mem_offline_driver_probe,
	.driver = {
		.name = "mem_offline",
		.of_match_table = mem_offline_match_table,
	},
};

static int __init mem_module_init(void)
{
	timer_setup(&mem_offline_timeout_timer, mem_offline_timeout_cb, 0);
	return platform_driver_register(&mem_offline_driver);
}
subsys_initcall(mem_module_init);

static void __exit mem_module_exit(void)
{
	del_timer_sync(&mem_offline_timeout_timer);
	platform_driver_unregister(&mem_offline_driver);
}
module_exit(mem_module_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Memory Offlining Driver");
MODULE_LICENSE("GPL");
