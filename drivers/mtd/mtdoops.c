/*
 * MTD Oops/Panic logger
 *
 * Copyright (C) 2007 Nokia Corporation. All rights reserved.
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
#include <linux/mtd/mtd.h>

#define OOPS_PAGE_SIZE 4096

static struct mtdoops_context {
	int mtd_index;
	struct work_struct work;
	struct mtd_info *mtd;
	int oops_pages;
	int nextpage;
	int nextcount;

	void *oops_buf;
	int ready;
	int writecount;
} oops_cxt;

static void mtdoops_erase_callback(struct erase_info *done)
{
	wait_queue_head_t *wait_q = (wait_queue_head_t *)done->priv;
	wake_up(wait_q);
}

static int mtdoops_erase_block(struct mtd_info *mtd, int offset)
{
	struct erase_info erase;
	DECLARE_WAITQUEUE(wait, current);
	wait_queue_head_t wait_q;
	int ret;

	init_waitqueue_head(&wait_q);
	erase.mtd = mtd;
	erase.callback = mtdoops_erase_callback;
	erase.addr = offset;
	if (mtd->erasesize < OOPS_PAGE_SIZE)
		erase.len = OOPS_PAGE_SIZE;
	else
		erase.len = mtd->erasesize;
	erase.priv = (u_long)&wait_q;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&wait_q, &wait);

	ret = mtd->erase(mtd, &erase);
	if (ret) {
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&wait_q, &wait);
		printk (KERN_WARNING "mtdoops: erase of region [0x%x, 0x%x] "
				     "on \"%s\" failed\n",
			erase.addr, erase.len, mtd->name);
		return ret;
	}

	schedule();  /* Wait for erase to finish. */
	remove_wait_queue(&wait_q, &wait);

	return 0;
}

static int mtdoops_inc_counter(struct mtdoops_context *cxt)
{
	struct mtd_info *mtd = cxt->mtd;
	size_t retlen;
	u32 count;
	int ret;

	cxt->nextpage++;
	if (cxt->nextpage > cxt->oops_pages)
		cxt->nextpage = 0;
	cxt->nextcount++;
	if (cxt->nextcount == 0xffffffff)
		cxt->nextcount = 0;

	ret = mtd->read(mtd, cxt->nextpage * OOPS_PAGE_SIZE, 4,
			&retlen, (u_char *) &count);
	if ((retlen != 4) || (ret < 0)) {
		printk(KERN_ERR "mtdoops: Read failure at %d (%td of 4 read)"
				", err %d.\n", cxt->nextpage * OOPS_PAGE_SIZE,
				retlen, ret);
		return 1;
	}

	/* See if we need to erase the next block */
	if (count != 0xffffffff)
		return 1;

	printk(KERN_DEBUG "mtdoops: Ready %d, %d (no erase)\n",
			cxt->nextpage, cxt->nextcount);
	cxt->ready = 1;
	return 0;
}

static void mtdoops_prepare(struct mtdoops_context *cxt)
{
	struct mtd_info *mtd = cxt->mtd;
	int i = 0, j, ret, mod;

	/* We were unregistered */
	if (!mtd)
		return;

	mod = (cxt->nextpage * OOPS_PAGE_SIZE) % mtd->erasesize;
	if (mod != 0) {
		cxt->nextpage = cxt->nextpage + ((mtd->erasesize - mod) / OOPS_PAGE_SIZE);
		if (cxt->nextpage > cxt->oops_pages)
			cxt->nextpage = 0;
	}

	while (mtd->block_isbad &&
			mtd->block_isbad(mtd, cxt->nextpage * OOPS_PAGE_SIZE)) {
badblock:
		printk(KERN_WARNING "mtdoops: Bad block at %08x\n",
				cxt->nextpage * OOPS_PAGE_SIZE);
		i++;
		cxt->nextpage = cxt->nextpage + (mtd->erasesize / OOPS_PAGE_SIZE);
		if (cxt->nextpage > cxt->oops_pages)
			cxt->nextpage = 0;
		if (i == (cxt->oops_pages / (mtd->erasesize / OOPS_PAGE_SIZE))) {
			printk(KERN_ERR "mtdoops: All blocks bad!\n");
			return;
		}
	}

	for (j = 0, ret = -1; (j < 3) && (ret < 0); j++)
		ret = mtdoops_erase_block(mtd, cxt->nextpage * OOPS_PAGE_SIZE);

	if (ret < 0) {
		if (mtd->block_markbad)
			mtd->block_markbad(mtd, cxt->nextpage * OOPS_PAGE_SIZE);
		goto badblock;
	}

	printk(KERN_DEBUG "mtdoops: Ready %d, %d \n", cxt->nextpage, cxt->nextcount);

	cxt->ready = 1;
}

static void mtdoops_workfunc(struct work_struct *work)
{
	struct mtdoops_context *cxt =
			container_of(work, struct mtdoops_context, work);

	mtdoops_prepare(cxt);
}

static int find_next_position(struct mtdoops_context *cxt)
{
	struct mtd_info *mtd = cxt->mtd;
	int page, maxpos = 0;
	u32 count, maxcount = 0xffffffff;
	size_t retlen;

	for (page = 0; page < cxt->oops_pages; page++) {
		mtd->read(mtd, page * OOPS_PAGE_SIZE, 4, &retlen, (u_char *) &count);
		if (count == 0xffffffff)
			continue;
		if (maxcount == 0xffffffff) {
			maxcount = count;
			maxpos = page;
		} else if ((count < 0x40000000) && (maxcount > 0xc0000000)) {
			maxcount = count;
			maxpos = page;
		} else if ((count > maxcount) && (count < 0xc0000000)) {
			maxcount = count;
			maxpos = page;
		} else if ((count > maxcount) && (count > 0xc0000000)
					&& (maxcount > 0x80000000)) {
			maxcount = count;
			maxpos = page;
		}
	}
	if (maxcount == 0xffffffff) {
		cxt->nextpage = 0;
		cxt->nextcount = 1;
		cxt->ready = 1;
		printk(KERN_DEBUG "mtdoops: Ready %d, %d (first init)\n",
				cxt->nextpage, cxt->nextcount);
		return 0;
	}

	cxt->nextpage = maxpos;
	cxt->nextcount = maxcount;

	return mtdoops_inc_counter(cxt);
}


static void mtdoops_notify_add(struct mtd_info *mtd)
{
	struct mtdoops_context *cxt = &oops_cxt;
	int ret;

	if ((mtd->index != cxt->mtd_index) || cxt->mtd_index < 0)
		return;

	if (mtd->size < (mtd->erasesize * 2)) {
		printk(KERN_ERR "MTD partition %d not big enough for mtdoops\n",
				mtd->index);
		return;
	}

	cxt->mtd = mtd;
	cxt->oops_pages = mtd->size / OOPS_PAGE_SIZE;

	ret = find_next_position(cxt);
	if (ret == 1)
		mtdoops_prepare(cxt);

	printk(KERN_DEBUG "mtdoops: Attached to MTD device %d\n", mtd->index);
}

static void mtdoops_notify_remove(struct mtd_info *mtd)
{
	struct mtdoops_context *cxt = &oops_cxt;

	if ((mtd->index != cxt->mtd_index) || cxt->mtd_index < 0)
		return;

	cxt->mtd = NULL;
	flush_scheduled_work();
}

static void mtdoops_console_sync(void)
{
	struct mtdoops_context *cxt = &oops_cxt;
	struct mtd_info *mtd = cxt->mtd;
	size_t retlen;
	int ret;

	if (!cxt->ready || !mtd)
		return;

	if (cxt->writecount == 0)
		return;

	if (cxt->writecount < OOPS_PAGE_SIZE)
		memset(cxt->oops_buf + cxt->writecount, 0xff,
					OOPS_PAGE_SIZE - cxt->writecount);

	ret = mtd->write(mtd, cxt->nextpage * OOPS_PAGE_SIZE,
					OOPS_PAGE_SIZE, &retlen, cxt->oops_buf);
	cxt->ready = 0;
	cxt->writecount = 0;

	if ((retlen != OOPS_PAGE_SIZE) || (ret < 0))
		printk(KERN_ERR "mtdoops: Write failure at %d (%td of %d written), err %d.\n",
			cxt->nextpage * OOPS_PAGE_SIZE, retlen,	OOPS_PAGE_SIZE, ret);

	ret = mtdoops_inc_counter(cxt);
	if (ret == 1)
		schedule_work(&cxt->work);
}

static void
mtdoops_console_write(struct console *co, const char *s, unsigned int count)
{
	struct mtdoops_context *cxt = co->data;
	struct mtd_info *mtd = cxt->mtd;
	int i;

	if (!oops_in_progress) {
		mtdoops_console_sync();
		return;
	}

	if (!cxt->ready || !mtd)
		return;

	if (cxt->writecount == 0) {
		u32 *stamp = cxt->oops_buf;
		*stamp = cxt->nextcount;
		cxt->writecount = 4;
	}

	if ((count + cxt->writecount) > OOPS_PAGE_SIZE)
		count = OOPS_PAGE_SIZE - cxt->writecount;

	for (i = 0; i < count; i++, s++)
		*((char *)(cxt->oops_buf) + cxt->writecount + i) = *s;

	cxt->writecount = cxt->writecount + count;
}

static int __init mtdoops_console_setup(struct console *co, char *options)
{
	struct mtdoops_context *cxt = co->data;

	if (cxt->mtd_index != -1)
		return -EBUSY;
	if (co->index == -1)
		return -EINVAL;

	cxt->mtd_index = co->index;
	return 0;
}

static struct mtd_notifier mtdoops_notifier = {
	.add	= mtdoops_notify_add,
	.remove	= mtdoops_notify_remove,
};

static struct console mtdoops_console = {
	.name		= "ttyMTD",
	.write		= mtdoops_console_write,
	.setup		= mtdoops_console_setup,
	.unblank	= mtdoops_console_sync,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &oops_cxt,
};

static int __init mtdoops_console_init(void)
{
	struct mtdoops_context *cxt = &oops_cxt;

	cxt->mtd_index = -1;
	cxt->oops_buf = vmalloc(OOPS_PAGE_SIZE);

	if (!cxt->oops_buf) {
		printk(KERN_ERR "Failed to allocate oops buffer workspace\n");
		return -ENOMEM;
	}

	INIT_WORK(&cxt->work, mtdoops_workfunc);

	register_console(&mtdoops_console);
	register_mtd_user(&mtdoops_notifier);
	return 0;
}

static void __exit mtdoops_console_exit(void)
{
	struct mtdoops_context *cxt = &oops_cxt;

	unregister_mtd_user(&mtdoops_notifier);
	unregister_console(&mtdoops_console);
	vfree(cxt->oops_buf);
}


subsys_initcall(mtdoops_console_init);
module_exit(mtdoops_console_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("MTD Oops/Panic console logger/driver");
