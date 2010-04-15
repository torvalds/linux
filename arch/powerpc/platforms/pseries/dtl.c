/*
 * Virtual Processor Dispatch Trace Log
 *
 * (C) Copyright IBM Corporation 2009
 *
 * Author: Jeremy Kerr <jk@ozlabs.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/firmware.h>

#include "plpar_wrappers.h"

/*
 * Layout of entries in the hypervisor's DTL buffer. Although we don't
 * actually access the internals of an entry (we only need to know the size),
 * we might as well define it here for reference.
 */
struct dtl_entry {
	u8	dispatch_reason;
	u8	preempt_reason;
	u16	processor_id;
	u32	enqueue_to_dispatch_time;
	u32	ready_to_enqueue_time;
	u32	waiting_to_ready_time;
	u64	timebase;
	u64	fault_addr;
	u64	srr0;
	u64	srr1;
};

struct dtl {
	struct dtl_entry	*buf;
	struct dentry		*file;
	int			cpu;
	int			buf_entries;
	u64			last_idx;
};
static DEFINE_PER_CPU(struct dtl, cpu_dtl);

/*
 * Dispatch trace log event mask:
 * 0x7: 0x1: voluntary virtual processor waits
 *      0x2: time-slice preempts
 *      0x4: virtual partition memory page faults
 */
static u8 dtl_event_mask = 0x7;


/*
 * Size of per-cpu log buffers. Default is just under 16 pages worth.
 */
static int dtl_buf_entries = (16 * 85);


static int dtl_enable(struct dtl *dtl)
{
	unsigned long addr;
	int ret, hwcpu;

	/* only allow one reader */
	if (dtl->buf)
		return -EBUSY;

	/* we need to store the original allocation size for use during read */
	dtl->buf_entries = dtl_buf_entries;

	dtl->buf = kmalloc_node(dtl->buf_entries * sizeof(struct dtl_entry),
			GFP_KERNEL, cpu_to_node(dtl->cpu));
	if (!dtl->buf) {
		printk(KERN_WARNING "%s: buffer alloc failed for cpu %d\n",
				__func__, dtl->cpu);
		return -ENOMEM;
	}

	/* Register our dtl buffer with the hypervisor. The HV expects the
	 * buffer size to be passed in the second word of the buffer */
	((u32 *)dtl->buf)[1] = dtl->buf_entries * sizeof(struct dtl_entry);

	hwcpu = get_hard_smp_processor_id(dtl->cpu);
	addr = __pa(dtl->buf);
	ret = register_dtl(hwcpu, addr);
	if (ret) {
		printk(KERN_WARNING "%s: DTL registration for cpu %d (hw %d) "
		       "failed with %d\n", __func__, dtl->cpu, hwcpu, ret);
		kfree(dtl->buf);
		return -EIO;
	}

	/* set our initial buffer indices */
	dtl->last_idx = lppaca[dtl->cpu].dtl_idx = 0;

	/* ensure that our updates to the lppaca fields have occurred before
	 * we actually enable the logging */
	smp_wmb();

	/* enable event logging */
	lppaca[dtl->cpu].dtl_enable_mask = dtl_event_mask;

	return 0;
}

static void dtl_disable(struct dtl *dtl)
{
	int hwcpu = get_hard_smp_processor_id(dtl->cpu);

	lppaca[dtl->cpu].dtl_enable_mask = 0x0;

	unregister_dtl(hwcpu, __pa(dtl->buf));

	kfree(dtl->buf);
	dtl->buf = NULL;
	dtl->buf_entries = 0;
}

/* file interface */

static int dtl_file_open(struct inode *inode, struct file *filp)
{
	struct dtl *dtl = inode->i_private;
	int rc;

	rc = dtl_enable(dtl);
	if (rc)
		return rc;

	filp->private_data = dtl;
	return 0;
}

static int dtl_file_release(struct inode *inode, struct file *filp)
{
	struct dtl *dtl = inode->i_private;
	dtl_disable(dtl);
	return 0;
}

static ssize_t dtl_file_read(struct file *filp, char __user *buf, size_t len,
		loff_t *pos)
{
	int rc, cur_idx, last_idx, n_read, n_req, read_size;
	struct dtl *dtl;

	if ((len % sizeof(struct dtl_entry)) != 0)
		return -EINVAL;

	dtl = filp->private_data;

	/* requested number of entries to read */
	n_req = len / sizeof(struct dtl_entry);

	/* actual number of entries read */
	n_read = 0;

	cur_idx = lppaca[dtl->cpu].dtl_idx;
	last_idx = dtl->last_idx;

	if (cur_idx - last_idx > dtl->buf_entries) {
		pr_debug("%s: hv buffer overflow for cpu %d, samples lost\n",
				__func__, dtl->cpu);
	}

	cur_idx  %= dtl->buf_entries;
	last_idx %= dtl->buf_entries;

	/* read the tail of the buffer if we've wrapped */
	if (last_idx > cur_idx) {
		read_size = min(n_req, dtl->buf_entries - last_idx);

		rc = copy_to_user(buf, &dtl->buf[last_idx],
				read_size * sizeof(struct dtl_entry));
		if (rc)
			return -EFAULT;

		last_idx = 0;
		n_req -= read_size;
		n_read += read_size;
		buf += read_size * sizeof(struct dtl_entry);
	}

	/* .. and now the head */
	read_size = min(n_req, cur_idx - last_idx);
	rc = copy_to_user(buf, &dtl->buf[last_idx],
			read_size * sizeof(struct dtl_entry));
	if (rc)
		return -EFAULT;

	n_read += read_size;
	dtl->last_idx += n_read;

	return n_read * sizeof(struct dtl_entry);
}

static const struct file_operations dtl_fops = {
	.open		= dtl_file_open,
	.release	= dtl_file_release,
	.read		= dtl_file_read,
	.llseek		= no_llseek,
};

static struct dentry *dtl_dir;

static int dtl_setup_file(struct dtl *dtl)
{
	char name[10];

	sprintf(name, "cpu-%d", dtl->cpu);

	dtl->file = debugfs_create_file(name, 0400, dtl_dir, dtl, &dtl_fops);
	if (!dtl->file)
		return -ENOMEM;

	return 0;
}

static int dtl_init(void)
{
	struct dentry *event_mask_file, *buf_entries_file;
	int rc, i;

	if (!firmware_has_feature(FW_FEATURE_SPLPAR))
		return -ENODEV;

	/* set up common debugfs structure */

	rc = -ENOMEM;
	dtl_dir = debugfs_create_dir("dtl", powerpc_debugfs_root);
	if (!dtl_dir) {
		printk(KERN_WARNING "%s: can't create dtl root dir\n",
				__func__);
		goto err;
	}

	event_mask_file = debugfs_create_x8("dtl_event_mask", 0600,
				dtl_dir, &dtl_event_mask);
	buf_entries_file = debugfs_create_u32("dtl_buf_entries", 0600,
				dtl_dir, &dtl_buf_entries);

	if (!event_mask_file || !buf_entries_file) {
		printk(KERN_WARNING "%s: can't create dtl files\n", __func__);
		goto err_remove_dir;
	}

	/* set up the per-cpu log structures */
	for_each_possible_cpu(i) {
		struct dtl *dtl = &per_cpu(cpu_dtl, i);
		dtl->cpu = i;

		rc = dtl_setup_file(dtl);
		if (rc)
			goto err_remove_dir;
	}

	return 0;

err_remove_dir:
	debugfs_remove_recursive(dtl_dir);
err:
	return rc;
}
arch_initcall(dtl_init);
