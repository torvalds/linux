/*******************************************************************************
 * Filename:  target_core_rd.c
 *
 * This file contains the Storage Engine <-> Ramdisk transport
 * specific functions.
 *
 * Copyright (c) 2003, 2004, 2005 PyX Technologies, Inc.
 * Copyright (c) 2005, 2006, 2007 SBE, Inc.
 * Copyright (c) 2007-2010 Rising Tide Systems
 * Copyright (c) 2008-2010 Linux-iSCSI.org
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/version.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <linux/timer.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>

#include <target/target_core_base.h>
#include <target/target_core_device.h>
#include <target/target_core_transport.h>
#include <target/target_core_fabric_ops.h>

#include "target_core_rd.h"

static struct se_subsystem_api rd_dr_template;
static struct se_subsystem_api rd_mcp_template;

/* #define DEBUG_RAMDISK_MCP */
/* #define DEBUG_RAMDISK_DR */

/*	rd_attach_hba(): (Part of se_subsystem_api_t template)
 *
 *
 */
static int rd_attach_hba(struct se_hba *hba, u32 host_id)
{
	struct rd_host *rd_host;

	rd_host = kzalloc(sizeof(struct rd_host), GFP_KERNEL);
	if (!(rd_host)) {
		printk(KERN_ERR "Unable to allocate memory for struct rd_host\n");
		return -ENOMEM;
	}

	rd_host->rd_host_id = host_id;

	atomic_set(&hba->left_queue_depth, RD_HBA_QUEUE_DEPTH);
	atomic_set(&hba->max_queue_depth, RD_HBA_QUEUE_DEPTH);
	hba->hba_ptr = (void *) rd_host;

	printk(KERN_INFO "CORE_HBA[%d] - TCM Ramdisk HBA Driver %s on"
		" Generic Target Core Stack %s\n", hba->hba_id,
		RD_HBA_VERSION, TARGET_CORE_MOD_VERSION);
	printk(KERN_INFO "CORE_HBA[%d] - Attached Ramdisk HBA: %u to Generic"
		" Target Core TCQ Depth: %d MaxSectors: %u\n", hba->hba_id,
		rd_host->rd_host_id, atomic_read(&hba->max_queue_depth),
		RD_MAX_SECTORS);

	return 0;
}

static void rd_detach_hba(struct se_hba *hba)
{
	struct rd_host *rd_host = hba->hba_ptr;

	printk(KERN_INFO "CORE_HBA[%d] - Detached Ramdisk HBA: %u from"
		" Generic Target Core\n", hba->hba_id, rd_host->rd_host_id);

	kfree(rd_host);
	hba->hba_ptr = NULL;
}

/*	rd_release_device_space():
 *
 *
 */
static void rd_release_device_space(struct rd_dev *rd_dev)
{
	u32 i, j, page_count = 0, sg_per_table;
	struct rd_dev_sg_table *sg_table;
	struct page *pg;
	struct scatterlist *sg;

	if (!rd_dev->sg_table_array || !rd_dev->sg_table_count)
		return;

	sg_table = rd_dev->sg_table_array;

	for (i = 0; i < rd_dev->sg_table_count; i++) {
		sg = sg_table[i].sg_table;
		sg_per_table = sg_table[i].rd_sg_count;

		for (j = 0; j < sg_per_table; j++) {
			pg = sg_page(&sg[j]);
			if ((pg)) {
				__free_page(pg);
				page_count++;
			}
		}

		kfree(sg);
	}

	printk(KERN_INFO "CORE_RD[%u] - Released device space for Ramdisk"
		" Device ID: %u, pages %u in %u tables total bytes %lu\n",
		rd_dev->rd_host->rd_host_id, rd_dev->rd_dev_id, page_count,
		rd_dev->sg_table_count, (unsigned long)page_count * PAGE_SIZE);

	kfree(sg_table);
	rd_dev->sg_table_array = NULL;
	rd_dev->sg_table_count = 0;
}


/*	rd_build_device_space():
 *
 *
 */
static int rd_build_device_space(struct rd_dev *rd_dev)
{
	u32 i = 0, j, page_offset = 0, sg_per_table, sg_tables, total_sg_needed;
	u32 max_sg_per_table = (RD_MAX_ALLOCATION_SIZE /
				sizeof(struct scatterlist));
	struct rd_dev_sg_table *sg_table;
	struct page *pg;
	struct scatterlist *sg;

	if (rd_dev->rd_page_count <= 0) {
		printk(KERN_ERR "Illegal page count: %u for Ramdisk device\n",
			rd_dev->rd_page_count);
		return -1;
	}
	total_sg_needed = rd_dev->rd_page_count;

	sg_tables = (total_sg_needed / max_sg_per_table) + 1;

	sg_table = kzalloc(sg_tables * sizeof(struct rd_dev_sg_table), GFP_KERNEL);
	if (!(sg_table)) {
		printk(KERN_ERR "Unable to allocate memory for Ramdisk"
			" scatterlist tables\n");
		return -1;
	}

	rd_dev->sg_table_array = sg_table;
	rd_dev->sg_table_count = sg_tables;

	while (total_sg_needed) {
		sg_per_table = (total_sg_needed > max_sg_per_table) ?
			max_sg_per_table : total_sg_needed;

		sg = kzalloc(sg_per_table * sizeof(struct scatterlist),
				GFP_KERNEL);
		if (!(sg)) {
			printk(KERN_ERR "Unable to allocate scatterlist array"
				" for struct rd_dev\n");
			return -1;
		}

		sg_init_table((struct scatterlist *)&sg[0], sg_per_table);

		sg_table[i].sg_table = sg;
		sg_table[i].rd_sg_count = sg_per_table;
		sg_table[i].page_start_offset = page_offset;
		sg_table[i++].page_end_offset = (page_offset + sg_per_table)
						- 1;

		for (j = 0; j < sg_per_table; j++) {
			pg = alloc_pages(GFP_KERNEL, 0);
			if (!(pg)) {
				printk(KERN_ERR "Unable to allocate scatterlist"
					" pages for struct rd_dev_sg_table\n");
				return -1;
			}
			sg_assign_page(&sg[j], pg);
			sg[j].length = PAGE_SIZE;
		}

		page_offset += sg_per_table;
		total_sg_needed -= sg_per_table;
	}

	printk(KERN_INFO "CORE_RD[%u] - Built Ramdisk Device ID: %u space of"
		" %u pages in %u tables\n", rd_dev->rd_host->rd_host_id,
		rd_dev->rd_dev_id, rd_dev->rd_page_count,
		rd_dev->sg_table_count);

	return 0;
}

static void *rd_allocate_virtdevice(
	struct se_hba *hba,
	const char *name,
	int rd_direct)
{
	struct rd_dev *rd_dev;
	struct rd_host *rd_host = hba->hba_ptr;

	rd_dev = kzalloc(sizeof(struct rd_dev), GFP_KERNEL);
	if (!(rd_dev)) {
		printk(KERN_ERR "Unable to allocate memory for struct rd_dev\n");
		return NULL;
	}

	rd_dev->rd_host = rd_host;
	rd_dev->rd_direct = rd_direct;

	return rd_dev;
}

static void *rd_DIRECT_allocate_virtdevice(struct se_hba *hba, const char *name)
{
	return rd_allocate_virtdevice(hba, name, 1);
}

static void *rd_MEMCPY_allocate_virtdevice(struct se_hba *hba, const char *name)
{
	return rd_allocate_virtdevice(hba, name, 0);
}

/*	rd_create_virtdevice():
 *
 *
 */
static struct se_device *rd_create_virtdevice(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	void *p,
	int rd_direct)
{
	struct se_device *dev;
	struct se_dev_limits dev_limits;
	struct rd_dev *rd_dev = p;
	struct rd_host *rd_host = hba->hba_ptr;
	int dev_flags = 0;
	char prod[16], rev[4];

	memset(&dev_limits, 0, sizeof(struct se_dev_limits));

	if (rd_build_device_space(rd_dev) < 0)
		goto fail;

	snprintf(prod, 16, "RAMDISK-%s", (rd_dev->rd_direct) ? "DR" : "MCP");
	snprintf(rev, 4, "%s", (rd_dev->rd_direct) ? RD_DR_VERSION :
						RD_MCP_VERSION);

	dev_limits.limits.logical_block_size = RD_BLOCKSIZE;
	dev_limits.limits.max_hw_sectors = RD_MAX_SECTORS;
	dev_limits.limits.max_sectors = RD_MAX_SECTORS;
	dev_limits.hw_queue_depth = RD_MAX_DEVICE_QUEUE_DEPTH;
	dev_limits.queue_depth = RD_DEVICE_QUEUE_DEPTH;

	dev = transport_add_device_to_core_hba(hba,
			(rd_dev->rd_direct) ? &rd_dr_template :
			&rd_mcp_template, se_dev, dev_flags, (void *)rd_dev,
			&dev_limits, prod, rev);
	if (!(dev))
		goto fail;

	rd_dev->rd_dev_id = rd_host->rd_host_dev_id_count++;
	rd_dev->rd_queue_depth = dev->queue_depth;

	printk(KERN_INFO "CORE_RD[%u] - Added TCM %s Ramdisk Device ID: %u of"
		" %u pages in %u tables, %lu total bytes\n",
		rd_host->rd_host_id, (!rd_dev->rd_direct) ? "MEMCPY" :
		"DIRECT", rd_dev->rd_dev_id, rd_dev->rd_page_count,
		rd_dev->sg_table_count,
		(unsigned long)(rd_dev->rd_page_count * PAGE_SIZE));

	return dev;

fail:
	rd_release_device_space(rd_dev);
	return NULL;
}

static struct se_device *rd_DIRECT_create_virtdevice(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	void *p)
{
	return rd_create_virtdevice(hba, se_dev, p, 1);
}

static struct se_device *rd_MEMCPY_create_virtdevice(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	void *p)
{
	return rd_create_virtdevice(hba, se_dev, p, 0);
}

/*	rd_free_device(): (Part of se_subsystem_api_t template)
 *
 *
 */
static void rd_free_device(void *p)
{
	struct rd_dev *rd_dev = p;

	rd_release_device_space(rd_dev);
	kfree(rd_dev);
}

static inline struct rd_request *RD_REQ(struct se_task *task)
{
	return container_of(task, struct rd_request, rd_task);
}

static struct se_task *
rd_alloc_task(struct se_cmd *cmd)
{
	struct rd_request *rd_req;

	rd_req = kzalloc(sizeof(struct rd_request), GFP_KERNEL);
	if (!rd_req) {
		printk(KERN_ERR "Unable to allocate struct rd_request\n");
		return NULL;
	}
	rd_req->rd_dev = SE_DEV(cmd)->dev_ptr;

	return &rd_req->rd_task;
}

/*	rd_get_sg_table():
 *
 *
 */
static struct rd_dev_sg_table *rd_get_sg_table(struct rd_dev *rd_dev, u32 page)
{
	u32 i;
	struct rd_dev_sg_table *sg_table;

	for (i = 0; i < rd_dev->sg_table_count; i++) {
		sg_table = &rd_dev->sg_table_array[i];
		if ((sg_table->page_start_offset <= page) &&
		    (sg_table->page_end_offset >= page))
			return sg_table;
	}

	printk(KERN_ERR "Unable to locate struct rd_dev_sg_table for page: %u\n",
			page);

	return NULL;
}

/*	rd_MEMCPY_read():
 *
 *
 */
static int rd_MEMCPY_read(struct rd_request *req)
{
	struct se_task *task = &req->rd_task;
	struct rd_dev *dev = req->rd_dev;
	struct rd_dev_sg_table *table;
	struct scatterlist *sg_d, *sg_s;
	void *dst, *src;
	u32 i = 0, j = 0, dst_offset = 0, src_offset = 0;
	u32 length, page_end = 0, table_sg_end;
	u32 rd_offset = req->rd_offset;

	table = rd_get_sg_table(dev, req->rd_page);
	if (!(table))
		return -1;

	table_sg_end = (table->page_end_offset - req->rd_page);
	sg_d = task->task_sg;
	sg_s = &table->sg_table[req->rd_page - table->page_start_offset];
#ifdef DEBUG_RAMDISK_MCP
	printk(KERN_INFO "RD[%u]: Read LBA: %llu, Size: %u Page: %u, Offset:"
		" %u\n", dev->rd_dev_id, task->task_lba, req->rd_size,
		req->rd_page, req->rd_offset);
#endif
	src_offset = rd_offset;

	while (req->rd_size) {
		if ((sg_d[i].length - dst_offset) <
		    (sg_s[j].length - src_offset)) {
			length = (sg_d[i].length - dst_offset);
#ifdef DEBUG_RAMDISK_MCP
			printk(KERN_INFO "Step 1 - sg_d[%d]: %p length: %d"
				" offset: %u sg_s[%d].length: %u\n", i,
				&sg_d[i], sg_d[i].length, sg_d[i].offset, j,
				sg_s[j].length);
			printk(KERN_INFO "Step 1 - length: %u dst_offset: %u"
				" src_offset: %u\n", length, dst_offset,
				src_offset);
#endif
			if (length > req->rd_size)
				length = req->rd_size;

			dst = sg_virt(&sg_d[i++]) + dst_offset;
			if (!dst)
				BUG();

			src = sg_virt(&sg_s[j]) + src_offset;
			if (!src)
				BUG();

			dst_offset = 0;
			src_offset = length;
			page_end = 0;
		} else {
			length = (sg_s[j].length - src_offset);
#ifdef DEBUG_RAMDISK_MCP
			printk(KERN_INFO "Step 2 - sg_d[%d]: %p length: %d"
				" offset: %u sg_s[%d].length: %u\n", i,
				&sg_d[i], sg_d[i].length, sg_d[i].offset,
				j, sg_s[j].length);
			printk(KERN_INFO "Step 2 - length: %u dst_offset: %u"
				" src_offset: %u\n", length, dst_offset,
				src_offset);
#endif
			if (length > req->rd_size)
				length = req->rd_size;

			dst = sg_virt(&sg_d[i]) + dst_offset;
			if (!dst)
				BUG();

			if (sg_d[i].length == length) {
				i++;
				dst_offset = 0;
			} else
				dst_offset = length;

			src = sg_virt(&sg_s[j++]) + src_offset;
			if (!src)
				BUG();

			src_offset = 0;
			page_end = 1;
		}

		memcpy(dst, src, length);

#ifdef DEBUG_RAMDISK_MCP
		printk(KERN_INFO "page: %u, remaining size: %u, length: %u,"
			" i: %u, j: %u\n", req->rd_page,
			(req->rd_size - length), length, i, j);
#endif
		req->rd_size -= length;
		if (!(req->rd_size))
			return 0;

		if (!page_end)
			continue;

		if (++req->rd_page <= table->page_end_offset) {
#ifdef DEBUG_RAMDISK_MCP
			printk(KERN_INFO "page: %u in same page table\n",
				req->rd_page);
#endif
			continue;
		}
#ifdef DEBUG_RAMDISK_MCP
		printk(KERN_INFO "getting new page table for page: %u\n",
				req->rd_page);
#endif
		table = rd_get_sg_table(dev, req->rd_page);
		if (!(table))
			return -1;

		sg_s = &table->sg_table[j = 0];
	}

	return 0;
}

/*	rd_MEMCPY_write():
 *
 *
 */
static int rd_MEMCPY_write(struct rd_request *req)
{
	struct se_task *task = &req->rd_task;
	struct rd_dev *dev = req->rd_dev;
	struct rd_dev_sg_table *table;
	struct scatterlist *sg_d, *sg_s;
	void *dst, *src;
	u32 i = 0, j = 0, dst_offset = 0, src_offset = 0;
	u32 length, page_end = 0, table_sg_end;
	u32 rd_offset = req->rd_offset;

	table = rd_get_sg_table(dev, req->rd_page);
	if (!(table))
		return -1;

	table_sg_end = (table->page_end_offset - req->rd_page);
	sg_d = &table->sg_table[req->rd_page - table->page_start_offset];
	sg_s = task->task_sg;
#ifdef DEBUG_RAMDISK_MCP
	printk(KERN_INFO "RD[%d] Write LBA: %llu, Size: %u, Page: %u,"
		" Offset: %u\n", dev->rd_dev_id, task->task_lba, req->rd_size,
		req->rd_page, req->rd_offset);
#endif
	dst_offset = rd_offset;

	while (req->rd_size) {
		if ((sg_s[i].length - src_offset) <
		    (sg_d[j].length - dst_offset)) {
			length = (sg_s[i].length - src_offset);
#ifdef DEBUG_RAMDISK_MCP
			printk(KERN_INFO "Step 1 - sg_s[%d]: %p length: %d"
				" offset: %d sg_d[%d].length: %u\n", i,
				&sg_s[i], sg_s[i].length, sg_s[i].offset,
				j, sg_d[j].length);
			printk(KERN_INFO "Step 1 - length: %u src_offset: %u"
				" dst_offset: %u\n", length, src_offset,
				dst_offset);
#endif
			if (length > req->rd_size)
				length = req->rd_size;

			src = sg_virt(&sg_s[i++]) + src_offset;
			if (!src)
				BUG();

			dst = sg_virt(&sg_d[j]) + dst_offset;
			if (!dst)
				BUG();

			src_offset = 0;
			dst_offset = length;
			page_end = 0;
		} else {
			length = (sg_d[j].length - dst_offset);
#ifdef DEBUG_RAMDISK_MCP
			printk(KERN_INFO "Step 2 - sg_s[%d]: %p length: %d"
				" offset: %d sg_d[%d].length: %u\n", i,
				&sg_s[i], sg_s[i].length, sg_s[i].offset,
				j, sg_d[j].length);
			printk(KERN_INFO "Step 2 - length: %u src_offset: %u"
				" dst_offset: %u\n", length, src_offset,
				dst_offset);
#endif
			if (length > req->rd_size)
				length = req->rd_size;

			src = sg_virt(&sg_s[i]) + src_offset;
			if (!src)
				BUG();

			if (sg_s[i].length == length) {
				i++;
				src_offset = 0;
			} else
				src_offset = length;

			dst = sg_virt(&sg_d[j++]) + dst_offset;
			if (!dst)
				BUG();

			dst_offset = 0;
			page_end = 1;
		}

		memcpy(dst, src, length);

#ifdef DEBUG_RAMDISK_MCP
		printk(KERN_INFO "page: %u, remaining size: %u, length: %u,"
			" i: %u, j: %u\n", req->rd_page,
			(req->rd_size - length), length, i, j);
#endif
		req->rd_size -= length;
		if (!(req->rd_size))
			return 0;

		if (!page_end)
			continue;

		if (++req->rd_page <= table->page_end_offset) {
#ifdef DEBUG_RAMDISK_MCP
			printk(KERN_INFO "page: %u in same page table\n",
				req->rd_page);
#endif
			continue;
		}
#ifdef DEBUG_RAMDISK_MCP
		printk(KERN_INFO "getting new page table for page: %u\n",
				req->rd_page);
#endif
		table = rd_get_sg_table(dev, req->rd_page);
		if (!(table))
			return -1;

		sg_d = &table->sg_table[j = 0];
	}

	return 0;
}

/*	rd_MEMCPY_do_task(): (Part of se_subsystem_api_t template)
 *
 *
 */
static int rd_MEMCPY_do_task(struct se_task *task)
{
	struct se_device *dev = task->se_dev;
	struct rd_request *req = RD_REQ(task);
	unsigned long long lba;
	int ret;

	req->rd_page = (task->task_lba * DEV_ATTRIB(dev)->block_size) / PAGE_SIZE;
	lba = task->task_lba;
	req->rd_offset = (do_div(lba,
			  (PAGE_SIZE / DEV_ATTRIB(dev)->block_size))) *
			   DEV_ATTRIB(dev)->block_size;
	req->rd_size = task->task_size;

	if (task->task_data_direction == DMA_FROM_DEVICE)
		ret = rd_MEMCPY_read(req);
	else
		ret = rd_MEMCPY_write(req);

	if (ret != 0)
		return ret;

	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);

	return PYX_TRANSPORT_SENT_TO_TRANSPORT;
}

/*	rd_DIRECT_with_offset():
 *
 *
 */
static int rd_DIRECT_with_offset(
	struct se_task *task,
	struct list_head *se_mem_list,
	u32 *se_mem_cnt,
	u32 *task_offset)
{
	struct rd_request *req = RD_REQ(task);
	struct rd_dev *dev = req->rd_dev;
	struct rd_dev_sg_table *table;
	struct se_mem *se_mem;
	struct scatterlist *sg_s;
	u32 j = 0, set_offset = 1;
	u32 get_next_table = 0, offset_length, table_sg_end;

	table = rd_get_sg_table(dev, req->rd_page);
	if (!(table))
		return -1;

	table_sg_end = (table->page_end_offset - req->rd_page);
	sg_s = &table->sg_table[req->rd_page - table->page_start_offset];
#ifdef DEBUG_RAMDISK_DR
	printk(KERN_INFO "%s DIRECT LBA: %llu, Size: %u Page: %u, Offset: %u\n",
		(task->task_data_direction == DMA_TO_DEVICE) ?
			"Write" : "Read",
		task->task_lba, req->rd_size, req->rd_page, req->rd_offset);
#endif
	while (req->rd_size) {
		se_mem = kmem_cache_zalloc(se_mem_cache, GFP_KERNEL);
		if (!(se_mem)) {
			printk(KERN_ERR "Unable to allocate struct se_mem\n");
			return -1;
		}
		INIT_LIST_HEAD(&se_mem->se_list);

		if (set_offset) {
			offset_length = sg_s[j].length - req->rd_offset;
			if (offset_length > req->rd_size)
				offset_length = req->rd_size;

			se_mem->se_page = sg_page(&sg_s[j++]);
			se_mem->se_off = req->rd_offset;
			se_mem->se_len = offset_length;

			set_offset = 0;
			get_next_table = (j > table_sg_end);
			goto check_eot;
		}

		offset_length = (req->rd_size < req->rd_offset) ?
			req->rd_size : req->rd_offset;

		se_mem->se_page = sg_page(&sg_s[j]);
		se_mem->se_len = offset_length;

		set_offset = 1;

check_eot:
#ifdef DEBUG_RAMDISK_DR
		printk(KERN_INFO "page: %u, size: %u, offset_length: %u, j: %u"
			" se_mem: %p, se_page: %p se_off: %u se_len: %u\n",
			req->rd_page, req->rd_size, offset_length, j, se_mem,
			se_mem->se_page, se_mem->se_off, se_mem->se_len);
#endif
		list_add_tail(&se_mem->se_list, se_mem_list);
		(*se_mem_cnt)++;

		req->rd_size -= offset_length;
		if (!(req->rd_size))
			goto out;

		if (!set_offset && !get_next_table)
			continue;

		if (++req->rd_page <= table->page_end_offset) {
#ifdef DEBUG_RAMDISK_DR
			printk(KERN_INFO "page: %u in same page table\n",
					req->rd_page);
#endif
			continue;
		}
#ifdef DEBUG_RAMDISK_DR
		printk(KERN_INFO "getting new page table for page: %u\n",
				req->rd_page);
#endif
		table = rd_get_sg_table(dev, req->rd_page);
		if (!(table))
			return -1;

		sg_s = &table->sg_table[j = 0];
	}

out:
	T_TASK(task->task_se_cmd)->t_tasks_se_num += *se_mem_cnt;
#ifdef DEBUG_RAMDISK_DR
	printk(KERN_INFO "RD_DR - Allocated %u struct se_mem segments for task\n",
			*se_mem_cnt);
#endif
	return 0;
}

/*	rd_DIRECT_without_offset():
 *
 *
 */
static int rd_DIRECT_without_offset(
	struct se_task *task,
	struct list_head *se_mem_list,
	u32 *se_mem_cnt,
	u32 *task_offset)
{
	struct rd_request *req = RD_REQ(task);
	struct rd_dev *dev = req->rd_dev;
	struct rd_dev_sg_table *table;
	struct se_mem *se_mem;
	struct scatterlist *sg_s;
	u32 length, j = 0;

	table = rd_get_sg_table(dev, req->rd_page);
	if (!(table))
		return -1;

	sg_s = &table->sg_table[req->rd_page - table->page_start_offset];
#ifdef DEBUG_RAMDISK_DR
	printk(KERN_INFO "%s DIRECT LBA: %llu, Size: %u, Page: %u\n",
		(task->task_data_direction == DMA_TO_DEVICE) ?
			"Write" : "Read",
		task->task_lba, req->rd_size, req->rd_page);
#endif
	while (req->rd_size) {
		se_mem = kmem_cache_zalloc(se_mem_cache, GFP_KERNEL);
		if (!(se_mem)) {
			printk(KERN_ERR "Unable to allocate struct se_mem\n");
			return -1;
		}
		INIT_LIST_HEAD(&se_mem->se_list);

		length = (req->rd_size < sg_s[j].length) ?
			req->rd_size : sg_s[j].length;

		se_mem->se_page = sg_page(&sg_s[j++]);
		se_mem->se_len = length;

#ifdef DEBUG_RAMDISK_DR
		printk(KERN_INFO "page: %u, size: %u, j: %u se_mem: %p,"
			" se_page: %p se_off: %u se_len: %u\n", req->rd_page,
			req->rd_size, j, se_mem, se_mem->se_page,
			se_mem->se_off, se_mem->se_len);
#endif
		list_add_tail(&se_mem->se_list, se_mem_list);
		(*se_mem_cnt)++;

		req->rd_size -= length;
		if (!(req->rd_size))
			goto out;

		if (++req->rd_page <= table->page_end_offset) {
#ifdef DEBUG_RAMDISK_DR
			printk("page: %u in same page table\n",
				req->rd_page);
#endif
			continue;
		}
#ifdef DEBUG_RAMDISK_DR
		printk(KERN_INFO "getting new page table for page: %u\n",
				req->rd_page);
#endif
		table = rd_get_sg_table(dev, req->rd_page);
		if (!(table))
			return -1;

		sg_s = &table->sg_table[j = 0];
	}

out:
	T_TASK(task->task_se_cmd)->t_tasks_se_num += *se_mem_cnt;
#ifdef DEBUG_RAMDISK_DR
	printk(KERN_INFO "RD_DR - Allocated %u struct se_mem segments for task\n",
			*se_mem_cnt);
#endif
	return 0;
}

/*	rd_DIRECT_do_se_mem_map():
 *
 *
 */
static int rd_DIRECT_do_se_mem_map(
	struct se_task *task,
	struct list_head *se_mem_list,
	void *in_mem,
	struct se_mem *in_se_mem,
	struct se_mem **out_se_mem,
	u32 *se_mem_cnt,
	u32 *task_offset_in)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct rd_request *req = RD_REQ(task);
	u32 task_offset = *task_offset_in;
	unsigned long long lba;
	int ret;

	req->rd_page = ((task->task_lba * DEV_ATTRIB(task->se_dev)->block_size) /
			PAGE_SIZE);
	lba = task->task_lba;
	req->rd_offset = (do_div(lba,
			  (PAGE_SIZE / DEV_ATTRIB(task->se_dev)->block_size))) *
			   DEV_ATTRIB(task->se_dev)->block_size;
	req->rd_size = task->task_size;

	if (req->rd_offset)
		ret = rd_DIRECT_with_offset(task, se_mem_list, se_mem_cnt,
				task_offset_in);
	else
		ret = rd_DIRECT_without_offset(task, se_mem_list, se_mem_cnt,
				task_offset_in);

	if (ret < 0)
		return ret;

	if (CMD_TFO(cmd)->task_sg_chaining == 0)
		return 0;
	/*
	 * Currently prevent writers from multiple HW fabrics doing
	 * pci_map_sg() to RD_DR's internal scatterlist memory.
	 */
	if (cmd->data_direction == DMA_TO_DEVICE) {
		printk(KERN_ERR "DMA_TO_DEVICE not supported for"
				" RAMDISK_DR with task_sg_chaining=1\n");
		return -1;
	}
	/*
	 * Special case for if task_sg_chaining is enabled, then
	 * we setup struct se_task->task_sg[], as it will be used by
	 * transport_do_task_sg_chain() for creating chainged SGLs
	 * across multiple struct se_task->task_sg[].
	 */
	if (!(transport_calc_sg_num(task,
			list_entry(T_TASK(cmd)->t_mem_list->next,
				   struct se_mem, se_list),
			task_offset)))
		return -1;

	return transport_map_mem_to_sg(task, se_mem_list, task->task_sg,
			list_entry(T_TASK(cmd)->t_mem_list->next,
				   struct se_mem, se_list),
			out_se_mem, se_mem_cnt, task_offset_in);
}

/*	rd_DIRECT_do_task(): (Part of se_subsystem_api_t template)
 *
 *
 */
static int rd_DIRECT_do_task(struct se_task *task)
{
	/*
	 * At this point the locally allocated RD tables have been mapped
	 * to struct se_mem elements in rd_DIRECT_do_se_mem_map().
	 */
	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);

	return PYX_TRANSPORT_SENT_TO_TRANSPORT;
}

/*	rd_free_task(): (Part of se_subsystem_api_t template)
 *
 *
 */
static void rd_free_task(struct se_task *task)
{
	kfree(RD_REQ(task));
}

enum {
	Opt_rd_pages, Opt_err
};

static match_table_t tokens = {
	{Opt_rd_pages, "rd_pages=%d"},
	{Opt_err, NULL}
};

static ssize_t rd_set_configfs_dev_params(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	const char *page,
	ssize_t count)
{
	struct rd_dev *rd_dev = se_dev->se_dev_su_ptr;
	char *orig, *ptr, *opts;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0, arg, token;

	opts = kstrdup(page, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	orig = opts;

	while ((ptr = strsep(&opts, ",")) != NULL) {
		if (!*ptr)
			continue;

		token = match_token(ptr, tokens, args);
		switch (token) {
		case Opt_rd_pages:
			match_int(args, &arg);
			rd_dev->rd_page_count = arg;
			printk(KERN_INFO "RAMDISK: Referencing Page"
				" Count: %u\n", rd_dev->rd_page_count);
			rd_dev->rd_flags |= RDF_HAS_PAGE_COUNT;
			break;
		default:
			break;
		}
	}

	kfree(orig);
	return (!ret) ? count : ret;
}

static ssize_t rd_check_configfs_dev_params(struct se_hba *hba, struct se_subsystem_dev *se_dev)
{
	struct rd_dev *rd_dev = se_dev->se_dev_su_ptr;

	if (!(rd_dev->rd_flags & RDF_HAS_PAGE_COUNT)) {
		printk(KERN_INFO "Missing rd_pages= parameter\n");
		return -1;
	}

	return 0;
}

static ssize_t rd_show_configfs_dev_params(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	char *b)
{
	struct rd_dev *rd_dev = se_dev->se_dev_su_ptr;
	ssize_t bl = sprintf(b, "TCM RamDisk ID: %u  RamDisk Makeup: %s\n",
			rd_dev->rd_dev_id, (rd_dev->rd_direct) ?
			"rd_direct" : "rd_mcp");
	bl += sprintf(b + bl, "        PAGES/PAGE_SIZE: %u*%lu"
			"  SG_table_count: %u\n", rd_dev->rd_page_count,
			PAGE_SIZE, rd_dev->sg_table_count);
	return bl;
}

/*	rd_get_cdb(): (Part of se_subsystem_api_t template)
 *
 *
 */
static unsigned char *rd_get_cdb(struct se_task *task)
{
	struct rd_request *req = RD_REQ(task);

	return req->rd_scsi_cdb;
}

static u32 rd_get_device_rev(struct se_device *dev)
{
	return SCSI_SPC_2; /* Returns SPC-3 in Initiator Data */
}

static u32 rd_get_device_type(struct se_device *dev)
{
	return TYPE_DISK;
}

static sector_t rd_get_blocks(struct se_device *dev)
{
	struct rd_dev *rd_dev = dev->dev_ptr;
	unsigned long long blocks_long = ((rd_dev->rd_page_count * PAGE_SIZE) /
			DEV_ATTRIB(dev)->block_size) - 1;

	return blocks_long;
}

static struct se_subsystem_api rd_dr_template = {
	.name			= "rd_dr",
	.transport_type		= TRANSPORT_PLUGIN_VHBA_VDEV,
	.attach_hba		= rd_attach_hba,
	.detach_hba		= rd_detach_hba,
	.allocate_virtdevice	= rd_DIRECT_allocate_virtdevice,
	.create_virtdevice	= rd_DIRECT_create_virtdevice,
	.free_device		= rd_free_device,
	.alloc_task		= rd_alloc_task,
	.do_task		= rd_DIRECT_do_task,
	.free_task		= rd_free_task,
	.check_configfs_dev_params = rd_check_configfs_dev_params,
	.set_configfs_dev_params = rd_set_configfs_dev_params,
	.show_configfs_dev_params = rd_show_configfs_dev_params,
	.get_cdb		= rd_get_cdb,
	.get_device_rev		= rd_get_device_rev,
	.get_device_type	= rd_get_device_type,
	.get_blocks		= rd_get_blocks,
	.do_se_mem_map		= rd_DIRECT_do_se_mem_map,
};

static struct se_subsystem_api rd_mcp_template = {
	.name			= "rd_mcp",
	.transport_type		= TRANSPORT_PLUGIN_VHBA_VDEV,
	.attach_hba		= rd_attach_hba,
	.detach_hba		= rd_detach_hba,
	.allocate_virtdevice	= rd_MEMCPY_allocate_virtdevice,
	.create_virtdevice	= rd_MEMCPY_create_virtdevice,
	.free_device		= rd_free_device,
	.alloc_task		= rd_alloc_task,
	.do_task		= rd_MEMCPY_do_task,
	.free_task		= rd_free_task,
	.check_configfs_dev_params = rd_check_configfs_dev_params,
	.set_configfs_dev_params = rd_set_configfs_dev_params,
	.show_configfs_dev_params = rd_show_configfs_dev_params,
	.get_cdb		= rd_get_cdb,
	.get_device_rev		= rd_get_device_rev,
	.get_device_type	= rd_get_device_type,
	.get_blocks		= rd_get_blocks,
};

int __init rd_module_init(void)
{
	int ret;

	ret = transport_subsystem_register(&rd_dr_template);
	if (ret < 0)
		return ret;

	ret = transport_subsystem_register(&rd_mcp_template);
	if (ret < 0) {
		transport_subsystem_release(&rd_dr_template);
		return ret;
	}

	return 0;
}

void rd_module_exit(void)
{
	transport_subsystem_release(&rd_dr_template);
	transport_subsystem_release(&rd_mcp_template);
}
