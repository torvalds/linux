// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2007,2012
 *
 * Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#define KMSG_COMPONENT "sclp_cmd"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/completion.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <asm/ctlreg.h>
#include <asm/chpid.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/sclp.h>
#include <asm/numa.h>
#include <asm/facility.h>

#include "sclp.h"

static void sclp_sync_callback(struct sclp_req *req, void *data)
{
	struct completion *completion = data;

	complete(completion);
}

int sclp_sync_request(sclp_cmdw_t cmd, void *sccb)
{
	return sclp_sync_request_timeout(cmd, sccb, 0);
}

int sclp_sync_request_timeout(sclp_cmdw_t cmd, void *sccb, int timeout)
{
	struct completion completion;
	struct sclp_req *request;
	int rc;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;
	if (timeout)
		request->queue_timeout = timeout;
	request->command = cmd;
	request->sccb = sccb;
	request->status = SCLP_REQ_FILLED;
	request->callback = sclp_sync_callback;
	request->callback_data = &completion;
	init_completion(&completion);

	/* Perform sclp request. */
	rc = sclp_add_request(request);
	if (rc)
		goto out;
	wait_for_completion(&completion);

	/* Check response. */
	if (request->status != SCLP_REQ_DONE) {
		pr_warn("sync request failed (cmd=0x%08x, status=0x%02x)\n",
			cmd, request->status);
		rc = -EIO;
	}
out:
	kfree(request);
	return rc;
}

/*
 * CPU configuration related functions.
 */

#define SCLP_CMDW_CONFIGURE_CPU		0x00110001
#define SCLP_CMDW_DECONFIGURE_CPU	0x00100001

int _sclp_get_core_info(struct sclp_core_info *info)
{
	int rc;
	int length = test_facility(140) ? EXT_SCCB_READ_CPU : PAGE_SIZE;
	struct read_cpu_info_sccb *sccb;

	if (!SCLP_HAS_CPU_INFO)
		return -EOPNOTSUPP;

	sccb = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA | __GFP_ZERO, get_order(length));
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = length;
	sccb->header.control_mask[2] = 0x80;
	rc = sclp_sync_request_timeout(SCLP_CMDW_READ_CPU_INFO, sccb,
				       SCLP_QUEUE_INTERVAL);
	if (rc)
		goto out;
	if (sccb->header.response_code != 0x0010) {
		pr_warn("readcpuinfo failed (response=0x%04x)\n",
			sccb->header.response_code);
		rc = -EIO;
		goto out;
	}
	sclp_fill_core_info(info, sccb);
out:
	free_pages((unsigned long) sccb, get_order(length));
	return rc;
}

struct cpu_configure_sccb {
	struct sccb_header header;
} __attribute__((packed, aligned(8)));

static int do_core_configure(sclp_cmdw_t cmd)
{
	struct cpu_configure_sccb *sccb;
	int rc;

	if (!SCLP_HAS_CPU_RECONFIG)
		return -EOPNOTSUPP;
	/*
	 * This is not going to cross a page boundary since we force
	 * kmalloc to have a minimum alignment of 8 bytes on s390.
	 */
	sccb = kzalloc(sizeof(*sccb), GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = sclp_sync_request_timeout(cmd, sccb, SCLP_QUEUE_INTERVAL);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
	case 0x0120:
		break;
	default:
		pr_warn("configure cpu failed (cmd=0x%08x, response=0x%04x)\n",
			cmd, sccb->header.response_code);
		rc = -EIO;
		break;
	}
out:
	kfree(sccb);
	return rc;
}

int sclp_core_configure(u8 core)
{
	return do_core_configure(SCLP_CMDW_CONFIGURE_CPU | core << 8);
}

int sclp_core_deconfigure(u8 core)
{
	return do_core_configure(SCLP_CMDW_DECONFIGURE_CPU | core << 8);
}

#ifdef CONFIG_MEMORY_HOTPLUG

static DEFINE_MUTEX(sclp_mem_mutex);
static LIST_HEAD(sclp_mem_list);
static u8 sclp_max_storage_id;
static DECLARE_BITMAP(sclp_storage_ids, 256);

struct memory_increment {
	struct list_head list;
	u16 rn;
	int standby;
};

struct assign_storage_sccb {
	struct sccb_header header;
	u16 rn;
} __packed;

int arch_get_memory_phys_device(unsigned long start_pfn)
{
	if (!sclp.rzm)
		return 0;
	return PFN_PHYS(start_pfn) >> ilog2(sclp.rzm);
}

static unsigned long long rn2addr(u16 rn)
{
	return (unsigned long long) (rn - 1) * sclp.rzm;
}

static int do_assign_storage(sclp_cmdw_t cmd, u16 rn)
{
	struct assign_storage_sccb *sccb;
	int rc;

	sccb = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = PAGE_SIZE;
	sccb->rn = rn;
	rc = sclp_sync_request_timeout(cmd, sccb, SCLP_QUEUE_INTERVAL);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
	case 0x0120:
		break;
	default:
		pr_warn("assign storage failed (cmd=0x%08x, response=0x%04x, rn=0x%04x)\n",
			cmd, sccb->header.response_code, rn);
		rc = -EIO;
		break;
	}
out:
	free_page((unsigned long) sccb);
	return rc;
}

static int sclp_assign_storage(u16 rn)
{
	unsigned long long start;
	int rc;

	rc = do_assign_storage(0x000d0001, rn);
	if (rc)
		return rc;
	start = rn2addr(rn);
	storage_key_init_range(start, start + sclp.rzm);
	return 0;
}

static int sclp_unassign_storage(u16 rn)
{
	return do_assign_storage(0x000c0001, rn);
}

struct attach_storage_sccb {
	struct sccb_header header;
	u16 :16;
	u16 assigned;
	u32 :32;
	u32 entries[];
} __packed;

static int sclp_attach_storage(u8 id)
{
	struct attach_storage_sccb *sccb;
	int rc;
	int i;

	sccb = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = PAGE_SIZE;
	sccb->header.function_code = 0x40;
	rc = sclp_sync_request_timeout(0x00080001 | id << 8, sccb,
				       SCLP_QUEUE_INTERVAL);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
		set_bit(id, sclp_storage_ids);
		for (i = 0; i < sccb->assigned; i++) {
			if (sccb->entries[i])
				sclp_unassign_storage(sccb->entries[i] >> 16);
		}
		break;
	default:
		rc = -EIO;
		break;
	}
out:
	free_page((unsigned long) sccb);
	return rc;
}

static int sclp_mem_change_state(unsigned long start, unsigned long size,
				 int online)
{
	struct memory_increment *incr;
	unsigned long long istart;
	int rc = 0;

	list_for_each_entry(incr, &sclp_mem_list, list) {
		istart = rn2addr(incr->rn);
		if (start + size - 1 < istart)
			break;
		if (start > istart + sclp.rzm - 1)
			continue;
		if (online)
			rc |= sclp_assign_storage(incr->rn);
		else
			sclp_unassign_storage(incr->rn);
		if (rc == 0)
			incr->standby = online ? 0 : 1;
	}
	return rc ? -EIO : 0;
}

static bool contains_standby_increment(unsigned long start, unsigned long end)
{
	struct memory_increment *incr;
	unsigned long istart;

	list_for_each_entry(incr, &sclp_mem_list, list) {
		istart = rn2addr(incr->rn);
		if (end - 1 < istart)
			continue;
		if (start > istart + sclp.rzm - 1)
			continue;
		if (incr->standby)
			return true;
	}
	return false;
}

static int sclp_mem_notifier(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	unsigned long start, size;
	struct memory_notify *arg;
	unsigned char id;
	int rc = 0;

	arg = data;
	start = arg->start_pfn << PAGE_SHIFT;
	size = arg->nr_pages << PAGE_SHIFT;
	mutex_lock(&sclp_mem_mutex);
	for_each_clear_bit(id, sclp_storage_ids, sclp_max_storage_id + 1)
		sclp_attach_storage(id);
	switch (action) {
	case MEM_GOING_OFFLINE:
		/*
		 * We do not allow to set memory blocks offline that contain
		 * standby memory. This is done to simplify the "memory online"
		 * case.
		 */
		if (contains_standby_increment(start, start + size))
			rc = -EPERM;
		break;
	case MEM_ONLINE:
	case MEM_CANCEL_OFFLINE:
		break;
	case MEM_GOING_ONLINE:
		rc = sclp_mem_change_state(start, size, 1);
		break;
	case MEM_CANCEL_ONLINE:
		sclp_mem_change_state(start, size, 0);
		break;
	case MEM_OFFLINE:
		sclp_mem_change_state(start, size, 0);
		break;
	default:
		break;
	}
	mutex_unlock(&sclp_mem_mutex);
	return rc ? NOTIFY_BAD : NOTIFY_OK;
}

static struct notifier_block sclp_mem_nb = {
	.notifier_call = sclp_mem_notifier,
};

static void __init align_to_block_size(unsigned long long *start,
				       unsigned long long *size,
				       unsigned long long alignment)
{
	unsigned long long start_align, size_align;

	start_align = roundup(*start, alignment);
	size_align = rounddown(*start + *size, alignment) - start_align;

	pr_info("Standby memory at 0x%llx (%lluM of %lluM usable)\n",
		*start, size_align >> 20, *size >> 20);
	*start = start_align;
	*size = size_align;
}

static void __init add_memory_merged(u16 rn)
{
	unsigned long long start, size, addr, block_size;
	static u16 first_rn, num;

	if (rn && first_rn && (first_rn + num == rn)) {
		num++;
		return;
	}
	if (!first_rn)
		goto skip_add;
	start = rn2addr(first_rn);
	size = (unsigned long long) num * sclp.rzm;
	if (start >= ident_map_size)
		goto skip_add;
	if (start + size > ident_map_size)
		size = ident_map_size - start;
	block_size = memory_block_size_bytes();
	align_to_block_size(&start, &size, block_size);
	if (!size)
		goto skip_add;
	for (addr = start; addr < start + size; addr += block_size)
		add_memory(0, addr, block_size, MHP_NONE);
skip_add:
	first_rn = rn;
	num = 1;
}

static void __init sclp_add_standby_memory(void)
{
	struct memory_increment *incr;

	list_for_each_entry(incr, &sclp_mem_list, list)
		if (incr->standby)
			add_memory_merged(incr->rn);
	add_memory_merged(0);
}

static void __init insert_increment(u16 rn, int standby, int assigned)
{
	struct memory_increment *incr, *new_incr;
	struct list_head *prev;
	u16 last_rn;

	new_incr = kzalloc(sizeof(*new_incr), GFP_KERNEL);
	if (!new_incr)
		return;
	new_incr->rn = rn;
	new_incr->standby = standby;
	last_rn = 0;
	prev = &sclp_mem_list;
	list_for_each_entry(incr, &sclp_mem_list, list) {
		if (assigned && incr->rn > rn)
			break;
		if (!assigned && incr->rn - last_rn > 1)
			break;
		last_rn = incr->rn;
		prev = &incr->list;
	}
	if (!assigned)
		new_incr->rn = last_rn + 1;
	if (new_incr->rn > sclp.rnmax) {
		kfree(new_incr);
		return;
	}
	list_add(&new_incr->list, prev);
}

static int __init sclp_detect_standby_memory(void)
{
	struct read_storage_sccb *sccb;
	int i, id, assigned, rc;

	if (oldmem_data.start) /* No standby memory in kdump mode */
		return 0;
	if ((sclp.facilities & 0xe00000000000ULL) != 0xe00000000000ULL)
		return 0;
	rc = -ENOMEM;
	sccb = (void *) __get_free_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		goto out;
	assigned = 0;
	for (id = 0; id <= sclp_max_storage_id; id++) {
		memset(sccb, 0, PAGE_SIZE);
		sccb->header.length = PAGE_SIZE;
		rc = sclp_sync_request(SCLP_CMDW_READ_STORAGE_INFO | id << 8, sccb);
		if (rc)
			goto out;
		switch (sccb->header.response_code) {
		case 0x0010:
			set_bit(id, sclp_storage_ids);
			for (i = 0; i < sccb->assigned; i++) {
				if (!sccb->entries[i])
					continue;
				assigned++;
				insert_increment(sccb->entries[i] >> 16, 0, 1);
			}
			break;
		case 0x0310:
			break;
		case 0x0410:
			for (i = 0; i < sccb->assigned; i++) {
				if (!sccb->entries[i])
					continue;
				assigned++;
				insert_increment(sccb->entries[i] >> 16, 1, 1);
			}
			break;
		default:
			rc = -EIO;
			break;
		}
		if (!rc)
			sclp_max_storage_id = sccb->max_id;
	}
	if (rc || list_empty(&sclp_mem_list))
		goto out;
	for (i = 1; i <= sclp.rnmax - assigned; i++)
		insert_increment(0, 1, 0);
	rc = register_memory_notifier(&sclp_mem_nb);
	if (rc)
		goto out;
	sclp_add_standby_memory();
out:
	free_page((unsigned long) sccb);
	return rc;
}
__initcall(sclp_detect_standby_memory);

#endif /* CONFIG_MEMORY_HOTPLUG */

/*
 * Channel path configuration related functions.
 */

#define SCLP_CMDW_CONFIGURE_CHPATH		0x000f0001
#define SCLP_CMDW_DECONFIGURE_CHPATH		0x000e0001
#define SCLP_CMDW_READ_CHPATH_INFORMATION	0x00030001

struct chp_cfg_sccb {
	struct sccb_header header;
	u8 ccm;
	u8 reserved[6];
	u8 cssid;
} __attribute__((packed));

static int do_chp_configure(sclp_cmdw_t cmd)
{
	struct chp_cfg_sccb *sccb;
	int rc;

	if (!SCLP_HAS_CHP_RECONFIG)
		return -EOPNOTSUPP;
	/* Prepare sccb. */
	sccb = (struct chp_cfg_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = sclp_sync_request(cmd, sccb);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
	case 0x0120:
	case 0x0440:
	case 0x0450:
		break;
	default:
		pr_warn("configure channel-path failed (cmd=0x%08x, response=0x%04x)\n",
			cmd, sccb->header.response_code);
		rc = -EIO;
		break;
	}
out:
	free_page((unsigned long) sccb);
	return rc;
}

/**
 * sclp_chp_configure - perform configure channel-path sclp command
 * @chpid: channel-path ID
 *
 * Perform configure channel-path command sclp command for specified chpid.
 * Return 0 after command successfully finished, non-zero otherwise.
 */
int sclp_chp_configure(struct chp_id chpid)
{
	return do_chp_configure(SCLP_CMDW_CONFIGURE_CHPATH | chpid.id << 8);
}

/**
 * sclp_chp_deconfigure - perform deconfigure channel-path sclp command
 * @chpid: channel-path ID
 *
 * Perform deconfigure channel-path command sclp command for specified chpid
 * and wait for completion. On success return 0. Return non-zero otherwise.
 */
int sclp_chp_deconfigure(struct chp_id chpid)
{
	return do_chp_configure(SCLP_CMDW_DECONFIGURE_CHPATH | chpid.id << 8);
}

struct chp_info_sccb {
	struct sccb_header header;
	u8 recognized[SCLP_CHP_INFO_MASK_SIZE];
	u8 standby[SCLP_CHP_INFO_MASK_SIZE];
	u8 configured[SCLP_CHP_INFO_MASK_SIZE];
	u8 ccm;
	u8 reserved[6];
	u8 cssid;
} __attribute__((packed));

/**
 * sclp_chp_read_info - perform read channel-path information sclp command
 * @info: resulting channel-path information data
 *
 * Perform read channel-path information sclp command and wait for completion.
 * On success, store channel-path information in @info and return 0. Return
 * non-zero otherwise.
 */
int sclp_chp_read_info(struct sclp_chp_info *info)
{
	struct chp_info_sccb *sccb;
	int rc;

	if (!SCLP_HAS_CHP_INFO)
		return -EOPNOTSUPP;
	/* Prepare sccb. */
	sccb = (struct chp_info_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = sclp_sync_request(SCLP_CMDW_READ_CHPATH_INFORMATION, sccb);
	if (rc)
		goto out;
	if (sccb->header.response_code != 0x0010) {
		pr_warn("read channel-path info failed (response=0x%04x)\n",
			sccb->header.response_code);
		rc = -EIO;
		goto out;
	}
	memcpy(info->recognized, sccb->recognized, SCLP_CHP_INFO_MASK_SIZE);
	memcpy(info->standby, sccb->standby, SCLP_CHP_INFO_MASK_SIZE);
	memcpy(info->configured, sccb->configured, SCLP_CHP_INFO_MASK_SIZE);
out:
	free_page((unsigned long) sccb);
	return rc;
}
