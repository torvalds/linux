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
#include <linux/poll.h>

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
	return 0;
}

static ssize_t
spufs_mem_read(struct file *file, char __user *buffer,
				size_t size, loff_t *pos)
{
	struct spu *spu;
	struct spu_context *ctx;
	int ret;

	ctx = file->private_data;
	spu = ctx->spu;

	down_read(&ctx->backing_sema);
	if (spu->number & 0/*1*/) {
		ret = generic_file_read(file, buffer, size, pos);
		goto out;
	}

	ret = simple_read_from_buffer(buffer, size, pos,
					spu->local_store, LS_SIZE);
out:
	up_read(&ctx->backing_sema);
	return ret;
}

static ssize_t
spufs_mem_write(struct file *file, const char __user *buffer,
					size_t size, loff_t *pos)
{
	struct spu_context *ctx = file->private_data;
	struct spu *spu = ctx->spu;

	if (spu->number & 0) //1)
		return generic_file_write(file, buffer, size, pos);

	size = min_t(ssize_t, LS_SIZE - *pos, size);
	if (size <= 0)
		return -EFBIG;
	*pos += size;
	return copy_from_user(spu->local_store + *pos - size,
				buffer, size) ? -EFAULT : size;
}

static int
spufs_mem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct spu_context *ctx = file->private_data;
	struct spu *spu = ctx->spu;
	unsigned long pfn;

	if (spu->number & 0) //1)
		return generic_file_mmap(file, vma);

	vma->vm_flags |= VM_RESERVED;
	vma->vm_page_prot = __pgprot(pgprot_val (vma->vm_page_prot)
							| _PAGE_NO_CACHE);
	pfn = spu->local_store_phys >> PAGE_SHIFT;
	/*
	 * This will work for actual SPUs, but not for vmalloc memory:
	 */
	if (remap_pfn_range(vma, vma->vm_start, pfn,
				vma->vm_end-vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static struct file_operations spufs_mem_fops = {
	.open	 = spufs_mem_open,
	.read    = spufs_mem_read,
	.write   = spufs_mem_write,
	.mmap    = spufs_mem_mmap,
	.llseek  = generic_file_llseek,
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
	struct spu_context *ctx;
	struct spu_problem __iomem *prob;
	u32 mbox_stat;
	u32 mbox_data;

	if (len < 4)
		return -EINVAL;

	ctx = file->private_data;
	prob = ctx->spu->problem;
	mbox_stat = in_be32(&prob->mb_stat_R);
	if (!(mbox_stat & 0x0000ff))
		return -EAGAIN;

	mbox_data = in_be32(&prob->pu_mb_R);

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
	struct spu_context *ctx;
	u32 mbox_stat;

	if (len < 4)
		return -EINVAL;

	ctx = file->private_data;
	mbox_stat = in_be32(&ctx->spu->problem->mb_stat_R) & 0xff;

	if (copy_to_user(buf, &mbox_stat, sizeof mbox_stat))
		return -EFAULT;

	return 4;
}

static struct file_operations spufs_mbox_stat_fops = {
	.open	= spufs_pipe_open,
	.read	= spufs_mbox_stat_read,
};

/* low-level ibox access function */
size_t spu_ibox_read(struct spu *spu, u32 *data)
{
	int ret;

	spin_lock_irq(&spu->register_lock);

	if (in_be32(&spu->problem->mb_stat_R) & 0xff0000) {
		/* read the first available word */
		*data = in_be64(&spu->priv2->puint_mb_R);
		ret = 4;
	} else {
		/* make sure we get woken up by the interrupt */
		out_be64(&spu->priv1->int_mask_class2_RW,
			in_be64(&spu->priv1->int_mask_class2_RW) | 0x1);
		ret = 0;
	}

	spin_unlock_irq(&spu->register_lock);
	return ret;
}
EXPORT_SYMBOL(spu_ibox_read);

static int spufs_ibox_fasync(int fd, struct file *file, int on)
{
	struct spu_context *ctx;
	ctx = file->private_data;
	return fasync_helper(fd, file, on, &ctx->spu->ibox_fasync);
}

static ssize_t spufs_ibox_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx;
	u32 ibox_data;
	ssize_t ret;

	if (len < 4)
		return -EINVAL;

	ctx = file->private_data;

	ret = 0;
	if (file->f_flags & O_NONBLOCK) {
		if (!spu_ibox_read(ctx->spu, &ibox_data))
			ret = -EAGAIN;
	} else {
		ret = wait_event_interruptible(ctx->spu->ibox_wq,
				 spu_ibox_read(ctx->spu, &ibox_data));
	}

	if (ret)
		return ret;

	ret = 4;
	if (copy_to_user(buf, &ibox_data, sizeof ibox_data))
		ret = -EFAULT;

	return ret;
}

static unsigned int spufs_ibox_poll(struct file *file, poll_table *wait)
{
	struct spu_context *ctx;
	struct spu_problem __iomem *prob;
	u32 mbox_stat;
	unsigned int mask;

	ctx = file->private_data;
	prob = ctx->spu->problem;
	mbox_stat = in_be32(&prob->mb_stat_R);

	poll_wait(file, &ctx->spu->ibox_wq, wait);

	mask = 0;
	if (mbox_stat & 0xff0000)
		mask |= POLLIN | POLLRDNORM;

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
	struct spu_context *ctx;
	u32 ibox_stat;

	if (len < 4)
		return -EINVAL;

	ctx = file->private_data;
	ibox_stat = (in_be32(&ctx->spu->problem->mb_stat_R) >> 16) & 0xff;

	if (copy_to_user(buf, &ibox_stat, sizeof ibox_stat))
		return -EFAULT;

	return 4;
}

static struct file_operations spufs_ibox_stat_fops = {
	.open	= spufs_pipe_open,
	.read	= spufs_ibox_stat_read,
};

/* low-level mailbox write */
size_t spu_wbox_write(struct spu *spu, u32 data)
{
	int ret;

	spin_lock_irq(&spu->register_lock);

	if (in_be32(&spu->problem->mb_stat_R) & 0x00ff00) {
		/* we have space to write wbox_data to */
		out_be32(&spu->problem->spu_mb_W, data);
		ret = 4;
	} else {
		/* make sure we get woken up by the interrupt when space
		   becomes available */
		out_be64(&spu->priv1->int_mask_class2_RW,
			in_be64(&spu->priv1->int_mask_class2_RW) | 0x10);
		ret = 0;
	}

	spin_unlock_irq(&spu->register_lock);
	return ret;
}
EXPORT_SYMBOL(spu_wbox_write);

static int spufs_wbox_fasync(int fd, struct file *file, int on)
{
	struct spu_context *ctx;
	ctx = file->private_data;
	return fasync_helper(fd, file, on, &ctx->spu->wbox_fasync);
}

static ssize_t spufs_wbox_write(struct file *file, const char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx;
	u32 wbox_data;
	int ret;

	if (len < 4)
		return -EINVAL;

	ctx = file->private_data;

	if (copy_from_user(&wbox_data, buf, sizeof wbox_data))
		return -EFAULT;

	ret = 0;
	if (file->f_flags & O_NONBLOCK) {
		if (!spu_wbox_write(ctx->spu, wbox_data))
			ret = -EAGAIN;
	} else {
		ret = wait_event_interruptible(ctx->spu->wbox_wq,
			spu_wbox_write(ctx->spu, wbox_data));
	}

	return ret ? ret : sizeof wbox_data;
}

static unsigned int spufs_wbox_poll(struct file *file, poll_table *wait)
{
	struct spu_context *ctx;
	struct spu_problem __iomem *prob;
	u32 mbox_stat;
	unsigned int mask;

	ctx = file->private_data;
	prob = ctx->spu->problem;
	mbox_stat = in_be32(&prob->mb_stat_R);

	poll_wait(file, &ctx->spu->wbox_wq, wait);

	mask = 0;
	if (mbox_stat & 0x00ff00)
		mask = POLLOUT | POLLWRNORM;

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
	struct spu_context *ctx;
	u32 wbox_stat;

	if (len < 4)
		return -EINVAL;

	ctx = file->private_data;
	wbox_stat = (in_be32(&ctx->spu->problem->mb_stat_R) >> 8) & 0xff;

	if (copy_to_user(buf, &wbox_stat, sizeof wbox_stat))
		return -EFAULT;

	return 4;
}

static struct file_operations spufs_wbox_stat_fops = {
	.open	= spufs_pipe_open,
	.read	= spufs_wbox_stat_read,
};

long spufs_run_spu(struct file *file, struct spu_context *ctx,
		u32 *npc, u32 *status)
{
	struct spu_problem __iomem *prob;
	int ret;

	if (file->f_flags & O_NONBLOCK) {
		ret = -EAGAIN;
		if (!down_write_trylock(&ctx->backing_sema))
			goto out;
	} else {
		down_write(&ctx->backing_sema);
	}

	prob = ctx->spu->problem;
	out_be32(&prob->spu_npc_RW, *npc);

	ret = spu_run(ctx->spu);

	*status = in_be32(&prob->spu_status_R);
	*npc = in_be32(&prob->spu_npc_RW);

	up_write(&ctx->backing_sema);

out:
	return ret;
}

static ssize_t spufs_signal1_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx;
	struct spu_problem *prob;
	u32 data;

	ctx = file->private_data;
	prob = ctx->spu->problem;

	if (len < 4)
		return -EINVAL;

	data = in_be32(&prob->signal_notify1);
	if (copy_to_user(buf, &data, 4))
		return -EFAULT;

	return 4;
}

static ssize_t spufs_signal1_write(struct file *file, const char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx;
	struct spu_problem *prob;
	u32 data;

	ctx = file->private_data;
	prob = ctx->spu->problem;

	if (len < 4)
		return -EINVAL;

	if (copy_from_user(&data, buf, 4))
		return -EFAULT;

	out_be32(&prob->signal_notify1, data);

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
	struct spu_problem *prob;
	u32 data;

	ctx = file->private_data;
	prob = ctx->spu->problem;

	if (len < 4)
		return -EINVAL;

	data = in_be32(&prob->signal_notify2);
	if (copy_to_user(buf, &data, 4))
		return -EFAULT;

	return 4;
}

static ssize_t spufs_signal2_write(struct file *file, const char __user *buf,
			size_t len, loff_t *pos)
{
	struct spu_context *ctx;
	struct spu_problem *prob;
	u32 data;

	ctx = file->private_data;
	prob = ctx->spu->problem;

	if (len < 4)
		return -EINVAL;

	if (copy_from_user(&data, buf, 4))
		return -EFAULT;

	out_be32(&prob->signal_notify2, data);

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
	struct spu_priv2 *priv2 = ctx->spu->priv2;
	u64 tmp;

	spin_lock_irq(&ctx->spu->register_lock);
	tmp = in_be64(&priv2->spu_cfg_RW);
	if (val)
		tmp |= 1;
	else
		tmp &= ~1;
	out_be64(&priv2->spu_cfg_RW, tmp);
	spin_unlock_irq(&ctx->spu->register_lock);
}

static u64 spufs_signal1_type_get(void *data)
{
	struct spu_context *ctx = data;
	return (in_be64(&ctx->spu->priv2->spu_cfg_RW) & 1) != 0;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_signal1_type, spufs_signal1_type_get,
					spufs_signal1_type_set, "%llu");

static void spufs_signal2_type_set(void *data, u64 val)
{
	struct spu_context *ctx = data;
	struct spu_priv2 *priv2 = ctx->spu->priv2;
	u64 tmp;

	spin_lock_irq(&ctx->spu->register_lock);
	tmp = in_be64(&priv2->spu_cfg_RW);
	if (val)
		tmp |= 2;
	else
		tmp &= ~2;
	out_be64(&priv2->spu_cfg_RW, tmp);
	spin_unlock_irq(&ctx->spu->register_lock);
}

static u64 spufs_signal2_type_get(void *data)
{
	struct spu_context *ctx = data;
	return (in_be64(&ctx->spu->priv2->spu_cfg_RW) & 2) != 0;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_signal2_type, spufs_signal2_type_get,
					spufs_signal2_type_set, "%llu");

static void spufs_npc_set(void *data, u64 val)
{
	struct spu_context *ctx = data;
	out_be32(&ctx->spu->problem->spu_npc_RW, val);
}

static u64 spufs_npc_get(void *data)
{
	struct spu_context *ctx = data;
	u64 ret;
	ret = in_be32(&ctx->spu->problem->spu_npc_RW);
	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(spufs_npc_ops, spufs_npc_get, spufs_npc_set, "%llx\n")

struct tree_descr spufs_dir_contents[] = {
	{ "mem",  &spufs_mem_fops,  0666, },
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
	{},
};
