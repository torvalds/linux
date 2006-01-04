/*
 * SPU file system -- file contents
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
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

#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/ptrace.h>

#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/spu.h>
#include <asm/uaccess.h>

#include "spufs.h"


static int
spufs_mem_open(struct inode *inode, struct file *file)
{
	struct spufs_inode_info *i = SPUFS_I(inode);
	file->private_data = i->i_ctx;
	file->f_mapping = i->i_ctx->local_store;
	return 0;
}

static ssize_t
spufs_mem_read(struct file *file, char __user *buffer,
				size_t size, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	char *local_store;
	int ret;

	spu_acquire(ctx);

	local_store = ctx->ops->get_ls(ctx);
	ret = simple_read_from_buffer(buffer, size, pos, local_store, LS_SIZE);

	spu_release(ctx);
	return ret;
}

static ssize_t
spufs_mem_write(struct file *file, const char __user *buffer,
					size_t size, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	char *local_store;
	int ret;

	size = min_t(ssize_t, LS_SIZE - *pos, size);
	if (size <= 0)
		return -EFBIG;
	*pos += size;

	spu_acquire(ctx);

	local_store = ctx->ops->get_ls(ctx);
	ret = copy_from_user(local_store + *pos - size,
			     buffer, size) ? -EFAULT : size;

	spu_release(ctx);
	return ret;
}

#ifdef CONFIG_SPARSEMEM
static struct page *
spufs_mem_mmap_nopage(struct vm_area_struct *vma,
		      unsigned long address, int *type)
{
	struct page *page = NOPAGE_SIGBUS;

	struct spu_context *ctx = vma->vm_file->private_data;
	unsigned long offset = address - vma->vm_start;
	offset += vma->vm_pgoff << PAGE_SHIFT;

	spu_acquire(ctx);

	if (ctx->state == SPU_STATE_SAVED)
		page = vmalloc_to_page(ctx->csa.lscsa->ls + offset);
	else
		page = pfn_to_page((ctx->spu->local_store_phys + offset)
				   >> PAGE_SHIFT);

	spu_release(ctx);

	if (type)
		*type = VM_FAULT_MINOR;

	page_cache_get(page);
	return page;
}

static struct vm_operations_struct spufs_mem_mmap_vmops = {
	.nopage = spufs_mem_mmap_nopage,
};

static int
spufs_mem_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	/* FIXME: */
	vma->vm_page_prot = __pgprot(pgprot_val(vma->vm_page_prot)
				     | _PAGE_NO_CACHE);

	vma->vm_ops = &spufs_mem_mmap_vmops;
	return 0;
}
#endif

static struct file_operations spufs_mem_fops = {
	.open	 = spufs_mem_open,
	.read    = spufs_mem_read,
	.write   = spufs_mem_write,
	.llseek  = generic_file_llseek,
#ifdef CONFIG_SPARSEMEM
	.mmap    = spufs_mem_mmap,
#endif
};

static int
spufs_regs_open(struct inode *inode, struct file *file)
{
	struct spufs_inode_info *i = SPUFS_I(inode);
	file->private_data = i->i_ctx;
	return 0;
}

static ssize_t
spufs_regs_read(struct file *file, char __user *buffer,
		size_t size, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	int ret;

	spu_acquire_saved(ctx);

	ret = simple_read_from_buffer(buffer, size, pos,
				      lscsa->gprs, sizeof lscsa->gprs);

	spu_release(ctx);
	return ret;
}

static ssize_t
spufs_regs_write(struct file *file, const char __user *buffer,
		 size_t size, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	int ret;

	size = min_t(ssize_t, sizeof lscsa->gprs - *pos, size);
	if (size <= 0)
		return -EFBIG;
	*pos += size;

	spu_acquire_saved(ctx);

	ret = copy_from_user(lscsa->gprs + *pos - size,
			     buffer, size) ? -EFAULT : size;

	spu_release(ctx);
	return ret;
}

static struct file_operations spufs_regs_fops = {
	.open	 = spufs_regs_open,
	.read    = spufs_regs_read,
	.write   = spufs_regs_write,
	.llseek  = generic_file_llseek,
};

static ssize_t
spufs_fpcr_read(struct file *file, char __user * buffer,
		size_t size, loff_t * pos)
{
	struct spu_context *ctx = file->private_data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	int ret;

	spu_acquire_saved(ctx);

	ret = simple_read_from_buffer(buffer, size, pos,
				      &lscsa->fpcr, sizeof(lscsa->fpcr));

	spu_release(ctx);
	return ret;
}

static ssize_t
spufs_fpcr_write(struct file *file, const char __user * buffer,
		 size_t size, loff_t * pos)
{
	struct spu_context *ctx = file->private_data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	int ret;

	size = min_t(ssize_t, sizeof(lscsa->fpcr) - *pos, size);
	if (size <= 0)
		return -EFBIG;
	*pos += size;

	spu_acquire_saved(ctx);

	ret = copy_from_user((char *)&lscsa->fpcr + *pos - size,
			     buffer, size) ? -EFAULT : size;

	spu_release(ctx);
	return ret;
}

static struct file_operations spufs_fpcr_fops = {
	.open = spufs_regs_open,
	.read = spufs_fpcr_read,
	.write = spufs_fpcr_write,
	.llseek = generic_file_llseek,
};

/* generic open function for all pipe-like files */
static int spufs_pipe_open(struct inode *inode, struct file *file)
{
	struct spufs_inode_info *i = SPUFS_I(inode);
	file->private_data = i->i_ctx;

	return nonseekable_open(inode, file);
}

static ssize_t spufs_mbox_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	u32 mbox_data;
	int ret;

	if (len < 4)
		return -EINVAL;

	spu_acquire(ctx);
	ret = ctx->ops->mbox_read(ctx, &mbox_data);
	spu_release(ctx);

	if (!ret)
		return -EAGAIN;

	if (copy_to_user(buf, &mbox_data, sizeof mbox_data))
		return -EFAULT;

	return 4;
}

static struct file_operations spufs_mbox_fops = {
	.open	= spufs_pipe_open,
	.read	= spufs_mbox_read,
};

static ssize_t spufs_mbox_stat_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	u32 mbox_stat;

	if (len < 4)
		return -EINVAL;

	spu_acquire(ctx);

	mbox_stat = ctx->ops->mbox_stat_read(ctx) & 0xff;

	spu_release(ctx);

	if (copy_to_user(buf, &mbox_stat, sizeof mbox_stat))
		return -EFAULT;

	return 4;
}

static struct file_operations spufs_mbox_stat_fops = {
	.open	= spufs_pipe_open,
	.read	= spufs_mbox_stat_read,
};

/* low-level ibox access function */
size_t spu_ibox_read(struct spu_context *ctx, u32 *data)
{
	return ctx->ops->ibox_read(ctx, data);
}

static int spufs_ibox_fasync(int fd, struct file *file, int on)
{
	struct spu_context *ctx = file->private_data;

	return fasync_helper(fd, file, on, &ctx->ibox_fasync);
}

/* interrupt-level ibox callback function. */
void spufs_ibox_callback(struct spu *spu)
{
	struct spu_context *ctx = spu->ctx;

	wake_up_all(&ctx->ibox_wq);
	kill_fasync(&ctx->ibox_fasync, SIGIO, POLLIN);
}

static ssize_t spufs_ibox_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	u32 ibox_data;
	ssize_t ret;

	if (len < 4)
		return -EINVAL;

	spu_acquire(ctx);

	ret = 0;
	if (file->f_flags & O_NONBLOCK) {
		if (!spu_ibox_read(ctx, &ibox_data))
			ret = -EAGAIN;
	} else {
		ret = spufs_wait(ctx->ibox_wq, spu_ibox_read(ctx, &ibox_data));
	}

	spu_release(ctx);

	if (ret)
		return ret;

	ret = 4;
	if (copy_to_user(buf, &ibox_data, sizeof ibox_data))
		ret = -EFAULT;

	return ret;
}

static unsigned int spufs_ibox_poll(struct file *file, poll_table *wait)
{
	struct spu_context *ctx = file->private_data;
	unsigned int mask;

	poll_wait(file, &ctx->ibox_wq, wait);

	spu_acquire(ctx);
	mask = ctx->ops->mbox_stat_poll(ctx, POLLIN | POLLRDNORM);
	spu_release(ctx);

	return mask;
}

static struct file_operations spufs_ibox_fops = {
	.open	= spufs_pipe_open,
	.read	= spufs_ibox_read,
	.poll	= spufs_ibox_poll,
	.fasync	= spufs_ibox_fasync,
};

static ssize_t spufs_ibox_stat_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	u32 ibox_stat;

	if (len < 4)
		return -EINVAL;

	spu_acquire(ctx);
	ibox_stat = (ctx->ops->mbox_stat_read(ctx) >> 16) & 0xff;
	spu_release(ctx);

	if (copy_to_user(buf, &ibox_stat, sizeof ibox_stat))
		return -EFAULT;

	return 4;
}

static struct file_operations spufs_ibox_stat_fops = {
	.open	= spufs_pipe_open,
	.read	= spufs_ibox_stat_read,
};

/* low-level mailbox write */
size_t spu_wbox_write(struct spu_context *ctx, u32 data)
{
	return ctx->ops->wbox_write(ctx, data);
}

static int spufs_wbox_fasync(int fd, struct file *file, int on)
{
	struct spu_context *ctx = file->private_data;
	int ret;

	ret = fasync_helper(fd, file, on, &ctx->wbox_fasync);

	return ret;
}

/* interrupt-level wbox callback function. */
void spufs_wbox_callback(struct spu *spu)
{
	struct spu_context *ctx = spu->ctx;

	wake_up_all(&ctx->wbox_wq);
	kill_fasync(&ctx->wbox_fasync, SIGIO, POLLOUT);
}

static ssize_t spufs_wbox_write(struct file *file, const char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	u32 wbox_data;
	int ret;

	if (len < 4)
		return -EINVAL;

	if (copy_from_user(&wbox_data, buf, sizeof wbox_data))
		return -EFAULT;

	spu_acquire(ctx);

	ret = 0;
	if (file->f_flags & O_NONBLOCK) {
		if (!spu_wbox_write(ctx, wbox_data))
			ret = -EAGAIN;
	} else {
		ret = spufs_wait(ctx->wbox_wq, spu_wbox_write(ctx, wbox_data));
	}

	spu_release(ctx);

	return ret ? ret : sizeof wbox_data;
}

static unsigned int spufs_wbox_poll(struct file *file, poll_table *wait)
{
	struct spu_context *ctx = file->private_data;
	unsigned int mask;

	poll_wait(file, &ctx->wbox_wq, wait);

	spu_acquire(ctx);
	mask = ctx->ops->mbox_stat_poll(ctx, POLLOUT | POLLWRNORM);
	spu_release(ctx);

	return mask;
}

static struct file_operations spufs_wbox_fops = {
	.open	= spufs_pipe_open,
	.write	= spufs_wbox_write,
	.poll	= spufs_wbox_poll,
	.fasync	= spufs_wbox_fasync,
};

static ssize_t spufs_wbox_stat_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	u32 wbox_stat;

	if (len < 4)
		return -EINVAL;

	spu_acquire(ctx);
	wbox_stat = (ctx->ops->mbox_stat_read(ctx) >> 8) & 0xff;
	spu_release(ctx);

	if (copy_to_user(buf, &wbox_stat, sizeof wbox_stat))
		return -EFAULT;

	return 4;
}

static struct file_operations spufs_wbox_stat_fops = {
	.open	= spufs_pipe_open,
	.read	= spufs_wbox_stat_read,
};

static ssize_t spufs_signal1_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	u32 data;

	if (len < 4)
		return -EINVAL;

	spu_acquire(ctx);
	data = ctx->ops->signal1_read(ctx);
	spu_release(ctx);

	if (copy_to_user(buf, &data, 4))
		return -EFAULT;

	return 4;
}

static ssize_t spufs_signal1_write(struct file *file, const char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx;
	u32 data;

	ctx = file->private_data;

	if (len < 4)
		return -EINVAL;

	if (copy_from_user(&data, buf, 4))
		return -EFAULT;

	spu_acquire(ctx);
	ctx->ops->signal1_write(ctx, data);
	spu_release(ctx);

	return 4;
}

static struct file_operations spufs_signal1_fops = {
	.open = spufs_pipe_open,
	.read = spufs_signal1_read,
	.write = spufs_signal1_write,
};

static ssize_t spufs_signal2_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx;
	u32 data;

	ctx = file->private_data;

	if (len < 4)
		return -EINVAL;

	spu_acquire(ctx);
	data = ctx->ops->signal2_read(ctx);
	spu_release(ctx);

	if (copy_to_user(buf, &data, 4))
		return -EFAULT;

	return 4;
}

static ssize_t spufs_signal2_write(struct file *file, const char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx;
	u32 data;

	ctx = file->private_data;

	if (len < 4)
		return -EINVAL;

	if (copy_from_user(&data, buf, 4))
		return -EFAULT;

	spu_acquire(ctx);
	ctx->ops->signal2_write(ctx, data);
	spu_release(ctx);

	return 4;
}

static struct file_operations spufs_signal2_fops = {
	.open = spufs_pipe_open,
	.read = spufs_signal2_read,
	.write = spufs_signal2_write,
};

static void spufs_signal1_type_set(void *data, u64 val)
{
	struct spu_context *ctx = data;

	spu_acquire(ctx);
	ctx->ops->signal1_type_set(ctx, val);
	spu_release(ctx);
}

static u64 spufs_signal1_type_get(void *data)
{
	struct spu_context *ctx = data;
	u64 ret;

	spu_acquire(ctx);
	ret = ctx->ops->signal1_type_get(ctx);
	spu_release(ctx);

	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_signal1_type, spufs_signal1_type_get,
					spufs_signal1_type_set, "%llu");

static void spufs_signal2_type_set(void *data, u64 val)
{
	struct spu_context *ctx = data;

	spu_acquire(ctx);
	ctx->ops->signal2_type_set(ctx, val);
	spu_release(ctx);
}

static u64 spufs_signal2_type_get(void *data)
{
	struct spu_context *ctx = data;
	u64 ret;

	spu_acquire(ctx);
	ret = ctx->ops->signal2_type_get(ctx);
	spu_release(ctx);

	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_signal2_type, spufs_signal2_type_get,
					spufs_signal2_type_set, "%llu");

static void spufs_npc_set(void *data, u64 val)
{
	struct spu_context *ctx = data;
	spu_acquire(ctx);
	ctx->ops->npc_write(ctx, val);
	spu_release(ctx);
}

static u64 spufs_npc_get(void *data)
{
	struct spu_context *ctx = data;
	u64 ret;
	spu_acquire(ctx);
	ret = ctx->ops->npc_read(ctx);
	spu_release(ctx);
	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_npc_ops, spufs_npc_get, spufs_npc_set, "%llx\n")

static void spufs_decr_set(void *data, u64 val)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	spu_acquire_saved(ctx);
	lscsa->decr.slot[0] = (u32) val;
	spu_release(ctx);
}

static u64 spufs_decr_get(void *data)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	u64 ret;
	spu_acquire_saved(ctx);
	ret = lscsa->decr.slot[0];
	spu_release(ctx);
	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_decr_ops, spufs_decr_get, spufs_decr_set,
			"%llx\n")

static void spufs_decr_status_set(void *data, u64 val)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	spu_acquire_saved(ctx);
	lscsa->decr_status.slot[0] = (u32) val;
	spu_release(ctx);
}

static u64 spufs_decr_status_get(void *data)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	u64 ret;
	spu_acquire_saved(ctx);
	ret = lscsa->decr_status.slot[0];
	spu_release(ctx);
	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_decr_status_ops, spufs_decr_status_get,
			spufs_decr_status_set, "%llx\n")

static void spufs_spu_tag_mask_set(void *data, u64 val)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	spu_acquire_saved(ctx);
	lscsa->tag_mask.slot[0] = (u32) val;
	spu_release(ctx);
}

static u64 spufs_spu_tag_mask_get(void *data)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	u64 ret;
	spu_acquire_saved(ctx);
	ret = lscsa->tag_mask.slot[0];
	spu_release(ctx);
	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_spu_tag_mask_ops, spufs_spu_tag_mask_get,
			spufs_spu_tag_mask_set, "%llx\n")

static void spufs_event_mask_set(void *data, u64 val)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	spu_acquire_saved(ctx);
	lscsa->event_mask.slot[0] = (u32) val;
	spu_release(ctx);
}

static u64 spufs_event_mask_get(void *data)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	u64 ret;
	spu_acquire_saved(ctx);
	ret = lscsa->event_mask.slot[0];
	spu_release(ctx);
	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_event_mask_ops, spufs_event_mask_get,
			spufs_event_mask_set, "%llx\n")

static void spufs_srr0_set(void *data, u64 val)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	spu_acquire_saved(ctx);
	lscsa->srr0.slot[0] = (u32) val;
	spu_release(ctx);
}

static u64 spufs_srr0_get(void *data)
{
	struct spu_context *ctx = data;
	struct spu_lscsa *lscsa = ctx->csa.lscsa;
	u64 ret;
	spu_acquire_saved(ctx);
	ret = lscsa->srr0.slot[0];
	spu_release(ctx);
	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_srr0_ops, spufs_srr0_get, spufs_srr0_set,
			"%llx\n")

struct tree_descr spufs_dir_contents[] = {
	{ "mem",  &spufs_mem_fops,  0666, },
	{ "regs", &spufs_regs_fops,  0666, },
	{ "mbox", &spufs_mbox_fops, 0444, },
	{ "ibox", &spufs_ibox_fops, 0444, },
	{ "wbox", &spufs_wbox_fops, 0222, },
	{ "mbox_stat", &spufs_mbox_stat_fops, 0444, },
	{ "ibox_stat", &spufs_ibox_stat_fops, 0444, },
	{ "wbox_stat", &spufs_wbox_stat_fops, 0444, },
	{ "signal1", &spufs_signal1_fops, 0666, },
	{ "signal2", &spufs_signal2_fops, 0666, },
	{ "signal1_type", &spufs_signal1_type, 0666, },
	{ "signal2_type", &spufs_signal2_type, 0666, },
	{ "npc", &spufs_npc_ops, 0666, },
	{ "fpcr", &spufs_fpcr_fops, 0666, },
	{ "decr", &spufs_decr_ops, 0666, },
	{ "decr_status", &spufs_decr_status_ops, 0666, },
	{ "spu_tag_mask", &spufs_spu_tag_mask_ops, 0666, },
	{ "event_mask", &spufs_event_mask_ops, 0666, },
	{ "srr0", &spufs_srr0_ops, 0666, },
	{},
};
