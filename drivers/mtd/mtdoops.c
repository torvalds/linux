/*
 * MTD Oops/Panic logger
 *
 * Copyright © 2007 Nokia Corporation. All rights reserved.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/kmsg_dump.h>

/* Maximum MTD partition size */
#define MTDOOPS_MAX_MTD_SIZE (8 * 1024 * 1024)

#define MTDOOPS_KERNMSG_MAGIC 0x5d005d00
#define MTDOOPS_HEADER_SIZE   8

static unsigned long record_size = 4096;
module_param(record_size, ulong, 0400);
MODULE_PARM_DESC(record_size,
		"record size for MTD OOPS pages in bytes (default 4096)");

static char mtddev[80];
module_param_string(mtddev, mtddev, 80, 0400);
MODULE_PARM_DESC(mtddev,
		"name or index number of the MTD device to use");

static int dump_oops = 1;
module_param(dump_oops, int, 0600);
MODULE_PARM_DESC(dump_oops,
		"set to 1 to dump oopses, 0 to only dump panics (default 1)");

static struct mtdoops_context {
	struct kmsg_dumper dump;

	int mtd_index;
	struct work_struct work_erase;
	struct work_struct work_write;
	struct mtd_info *mtd;
	int oops_pages;
	int nextpage;
	int nextcount;
	unsigned long *oops_page_used;

	void *oops_buf;
} oops_cxt;

static void mark_page_used(struct mtdoops_context *cxt, int page)
{
	set_bit(page, cxt->oops_page_used);
}

static void mark_page_unused(struct mtdoops_context *cxt, int page)
{
	clear_bit(page, cxt->oops_page_used);
}

static int page_is_used(struct mtdoops_context *cxt, int page)
{
	return test_bit(page, cxt->oops_page_used);
}

static void mtdoops_erase_callback(struct erase_info *done)
{
	wait_queue_head_t *wait_q = (wait_queue_head_t *)done->priv;
	wake_up(wait_q);
}

static int mtdoops_erase_block(struct mtdoops_context *cxt, int offset)
{
	struct mtd_info *mtd = cxt->mtd;
	u32 start_page_offset = mtd_div_by_eb(offset, mtd) * mtd->erasesize;
	u32 start_page = start_page_offset / record_size;
	u32 erase_pages = mtd->erasesize / record_size;
	struct erase_info erase;
	DECLARE_WAITQUEUE(wait, current);
	wait_queue_head_t wait_q;
	int ret;
	int page;

	init_waitqueue_head(&wait_q);
	erase.mtd = mtd;
	erase.callback = mtdoops_erase_callback;
	erase.addr = offset;
	erase.len = mtd->erasesize;
	erase.priv = (u_long)&wait_q;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&wait_q, &wait);

	ret = mtd->erase(mtd, &erase);
	if (ret) {
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&wait_q, &wait);
		printk(KERN_WARNING "mtdoops: erase of region [0x%llx, 0x%llx] on \"%s\" failed\n",
		       (unsigned long long)erase.addr,
		       (unsigned long long)erase.len, mtddev);
		return ret;
	}

	schedule();  /* Wait for erase to finish. */
	remove_wait_queue(&wait_q, &wait);

	/* Mark pages as unused */
	for (page = start_page; page < start_page + erase_pages; page++)
		mark_page_unused(cxt, page);

	return 0;
}

static void mtdoops_inc_counter(struct mtdoops_context *cxt)
{
	cxt->nextpage++;
	if (cxt->nextpage >= cxt->oops_pages)
		cxt->nextpage = 0;
	cxt->nextcount++;
	if (cxt->nextcount == 0xffffffff)
		cxt->nextcount = 0;

	if (page_is_used(cxt, cxt->nextpage)) {
		schedule_work(&cxt->work_erase);
		return;
	}

	printk(KERN_DEBUG "mtdoops: ready %d, %d (no erase)\n",
	       cxt->nextpage, cxt->nextcount);
}

/* Scheduled work - when we can't proceed without erasing a block */
static void mtdoops_workfunc_erase(struct work_struct *work)
{
	struct mtdoops_context *cxt =
			container_of(work, struct mtdoops_context, work_erase);
	struct mtd_info *mtd = cxt->mtd;
	int i = 0, j, ret, mod;

	/* We were unregistered */
	if (!mtd)
		return;

	mod = (cxt->nextpage * record_size) % mtd->erasesize;
	if (mod != 0) {
		cxt->nextpage = cxt->nextpage + ((mtd->erasesize - mod) / record_size);
		if (cxt->nextpage >= cxt->oops_pages)
			cxt->nextpage = 0;
	}

	while (mtd->block_isbad) {
		ret = mtd->block_isbad(mtd, cxt->nextpage * record_size);
		if (!ret)
			break;
		if (ret < 0) {
			printk(KERN_ERR "mtdoops: block_isbad failed, aborting\n");
			return;
		}
badblock:
		printk(KERN_WARNING "mtdoops: bad block at %08lx\n",
		       cxt->nextpage * record_size);
		i++;
		cxt->nextpage = cxt->nextpage + (mtd->erasesize / record_size);
		if (cxt->nextpage >= cxt->oops_pages)
			cxt->nextpage = 0;
		if (i == cxt->oops_pages / (mtd->erasesize / record_size)) {
			printk(KERN_ERR "mtdoops: all blocks bad!\n");
			return;
		}
	}

	for (j = 0, ret = -1; (j < 3) && (ret < 0); j++)
		ret = mtdoops_erase_block(cxt, cxt->nextpage * record_size);

	if (ret >= 0) {
		printk(KERN_DEBUG "mtdoops: ready %d, %d\n",
		       cxt->nextpage, cxt->nextcount);
		return;
	}

	if (mtd->block_markbad && ret == -EIO) {
		ret = mtd->block_markbad(mtd, cxt->nextpage * record_size);
		if (ret < 0) {
			printk(KERN_ERR "mtdoops: block_markbad failed, aborting\n");
			return;
		}
	}
	goto badblock;
}

static void mtdoops_write(struct mtdoops_context *cxt, int panic)
{
	struct mtd_info *mtd = cxt->mtd;
	size_t retlen;
	u32 *hdr;
	int ret;

	/* Add mtdoops header to the buffer */
	hdr = cxt->oops_buf;
	hdr[0] = cxt->nextcount;
	hdr[1] = MTDOOPS_KERNMSG_MAGIC;

	if (panic)
		ret = mtd->panic_write(mtd, cxt->nextpage * record_size,
					record_size, &retlen, cxt->oops_buf);
	else
		ret = mtd->write(mtd, cxt->nextpage * record_size,
					record_size, &retlen, cxt->oops_buf);

	if (retlen != record_size || ret < 0)
		printk(KERN_ERR "mtdoops: write failure at %ld (%td of %ld written), error %d\n",
		       cxt->nextpage * record_size, retlen, record_size, ret);
	mark_page_used(cxt, cxt->nextpage);
	memset(cxt->oops_buf, 0xff, record_size);

	mtdoops_inc_counter(cxt);
}

static void mtdoops_workfunc_write(struct work_struct *work)
{
	struct mtdoops_context *cxt =
			container_of(work, struct mtdoops_context, work_write);

	mtdoops_write(cxt, 0);
}

static void find_next_position(struct mtdoops_context *cxt)
{
	struct mtd_info *mtd = cxt->mtd;
	int ret, page, maxpos = 0;
	u32 count[2], maxcount = 0xffffffff;
	size_t retlen;

	for (page = 0; page < cxt->oops_pages; page++) {
		if (mtd->block_isbad &&
		    mtd->block_isbad(mtd, page * record_size))
			continue;
		/* Assume the page is used */
		mark_page_used(cxt, page);
		ret = mtd->read(mtd, page * record_size, MTDOOPS_HEADER_SIZE,
				&retlen, (u_char *) &count[0]);
		if (retlen != MTDOOPS_HEADER_SIZE ||
				(ret < 0 && !mtd_is_bitflip(ret))) {
			printk(KERN_ERR "mtdoops: read failure at %ld (%td of %d read), err %d\n",
			       page * record_size, retlen,
			       MTDOOPS_HEADER_SIZE, ret);
			continue;
		}

		if (count[0] == 0xffffffff && count[1] == 0xffffffff)
			mark_page_unused(cxt, page);
		if (count[0] == 0xffffffff)
			continue;
		if (maxcount == 0xffffffff) {
			maxcount = count[0];
			maxpos = page;
		} else if (count[0] < 0x40000000 && maxcount > 0xc0000000) {
			maxcount = count[0];
			maxpos = page;
		} else if (count[0] > maxcount && count[0] < 0xc0000000) {
			maxcount = count[0];
			maxpos = page;
		} else if (count[0] > maxcount && count[0] > 0xc0000000
					&& maxcount > 0x80000000) {
			maxcount = count[0];
			maxpos = page;
		}
	}
	if (maxcount == 0xffffffff) {
		cxt->nextpage = 0;
		cxt->nextcount = 1;
		schedule_work(&cxt->work_erase);
		return;
	}

	cxt->nextpage = maxpos;
	cxt->nextcount = maxcount;

	mtdoops_inc_counter(cxt);
}

static void mtdoops_do_dump(struct kmsg_dumper *dumper,
		enum kmsg_dump_reason reason, const char *s1, unsigned long l1,
		const char *s2, unsigned long l2)
{
	struct mtdoops_context *cxt = container_of(dumper,
			struct mtdoops_context, dump);
	unsigned long s1_start, s2_start;
	unsigned long l1_cpy, l2_cpy;
	char *dst;

	if (reason != KMSG_DUMP_OOPS &&
	    reason != KMSG_DUMP_PANIC &&
	    reason != KMSG_DUMP_KEXEC)
		return;

	/* Only dump oopses if dump_oops is set */
	if (reason == KMSG_DUMP_OOPS && !dump_oops)
		return;

	dst = cxt->oops_buf + MTDOOPS_HEADER_SIZE; /* Skip the header */
	l2_cpy = min(l2, record_size - MTDOOPS_HEADER_SIZE);
	l1_cpy = min(l1, record_size - MTDOOPS_HEADER_SIZE - l2_cpy);

	s2_start = l2 - l2_cpy;
	s1_start = l1 - l1_cpy;

	memcpy(dst, s1 + s1_start, l1_cpy);
	memcpy(dst + l1_cpy, s2 + s2_start, l2_cpy);

	/* Panics must be written immediately */
	if (reason != KMSG_DUMP_OOPS) {
		if (!cxt->mtd->panic_write)
			printk(KERN_ERR "mtdoops: Cannot write from panic without panic_write\n");
		else
			mtdoops_write(cxt, 1);
		return;
	}

	/* For other cases, schedule work to write it "nicely" */
	schedule_work(&cxt->work_write);
}

static void mtdoops_notify_add(struct mtd_info *mtd)
{
	struct mtdoops_context *cxt = &oops_cxt;
	u64 mtdoops_pages = div_u64(mtd->size, record_size);
	int err;

	if (!strcmp(mtd->name, mtddev))
		cxt->mtd_index = mtd->index;

	if (mtd->index != cxt->mtd_index || cxt->mtd_index < 0)
		return;

	if (mtd->size < mtd->erasesize * 2) {
		printk(KERN_ERR "mtdoops: MTD partition %d not big enough for mtdoops\n",
		       mtd->index);
		return;
	}
	if (mtd->erasesize < record_size) {
		printk(KERN_ERR "mtdoops: eraseblock size of MTD partition %d too small\n",
		       mtd->index);
		return;
	}
	if (mtd->size > MTDOOPS_MAX_MTD_SIZE) {
		printk(KERN_ERR "mtdoops: mtd%d is too large (limit is %d MiB)\n",
		       mtd->index, MTDOOPS_MAX_MTD_SIZE / 1024 / 1024);
		return;
	}

	/* oops_page_used is a bit field */
	cxt->oops_page_used = vmalloc(DIV_ROUND_UP(mtdoops_pages,
			BITS_PER_LONG) * sizeof(unsigned long));
	if (!cxt->oops_page_used) {
		printk(KERN_ERR "mtdoops: could not allocate page array\n");
		return;
	}

	cxt->dump.dump = mtdoops_do_dump;
	err = kmsg_dump_register(&cxt->dump);
	if (err) {
		printk(KERN_ERR "mtdoops: registering kmsg dumper failed, error %d\n", err);
		vfree(cxt->oops_page_used);
		cxt->oops_page_used = NULL;
		return;
	}

	cxt->mtd = mtd;
	cxt->oops_pages = (int)mtd->size / record_size;
	find_next_position(cxt);
	printk(KERN_INFO "mtdoops: Attached to MTD device %d\n", mtd->index);
}

static void mtdoops_notify_remove(struct mtd_info *mtd)
{
	struct mtdoops_context *cxt = &oops_cxt;

	if (mtd->index != cxt->mtd_index || cxt->mtd_index < 0)
		return;

	if (kmsg_dump_unregister(&cxt->dump) < 0)
		printk(KERN_WARNING "mtdoops: could not unregister kmsg_dumper\n");

	cxt->mtd = NULL;
	flush_work_sync(&cxt->work_erase);
	flush_work_sync(&cxt->work_write);
}


static struct mtd_notifier mtdoops_notifier = {
	.add	= mtdoops_notify_add,
	.remove	= mtdoops_notify_remove,
};

static int __init mtdoops_init(void)
{
	struct mtdoops_context *cxt = &oops_cxt;
	int mtd_index;
	char *endp;

	if (strlen(mtddev) == 0) {
		printk(KERN_ERR "mtdoops: mtd device (mtddev=name/number) must be supplied\n");
		return -EINVAL;
	}
	if ((record_size & 4095) != 0) {
		printk(KERN_ERR "mtdoops: record_size must be a multiple of 4096\n");
		return -EINVAL;
	}
	if (record_size < 4096) {
		printk(KERN_ERR "mtdoops: record_size must be over 4096 bytes\n");
		return -EINVAL;
	}

	/* Setup the MTD device to use */
	cxt->mtd_index = -1;
	mtd_index = simple_strtoul(mtddev, &endp, 0);
	if (*endp == '\0')
		cxt->mtd_index = mtd_index;

	cxt->oops_buf = vmalloc(record_size);
	if (!cxt->oops_buf) {
		printk(KERN_ERR "mtdoops: failed to allocate buffer workspace\n");
		return -ENOMEM;
	}
	memset(cxt->oops_buf, 0xff, record_size);

	INIT_WORK(&cxt->work_erase, mtdoops_workfunc_erase);
	INIT_WORK(&cxt->work_write, mtdoops_workfunc_write);

	register_mtd_user(&mtdoops_notifier);
	return 0;
}

static void __exit mtdoops_exit(void)
{
	struct mtdoops_context *cxt = &oops_cxt;

	unregister_mtd_user(&mtdoops_notifier);
	vfree(cxt->oops_buf);
	vfree(cxt->oops_page_used);
}


module_init(mtdoops_init);
module_exit(mtdoops_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("MTD Oops/Panic console logger/driver");
