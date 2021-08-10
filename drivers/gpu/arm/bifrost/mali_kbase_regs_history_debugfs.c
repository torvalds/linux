// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014, 2016, 2019-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include "mali_kbase.h"
#include "mali_kbase_regs_history_debugfs.h"

#if defined(CONFIG_DEBUG_FS) && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)

#include <linux/debugfs.h>

/**
 * kbase_io_history_resize - resize the register access history buffer.
 *
 * @h: Pointer to a valid register history to resize
 * @new_size: Number of accesses the buffer could hold
 *
 * A successful resize will clear all recent register accesses.
 * If resizing fails for any reason (e.g., could not allocate memory, invalid
 * buffer size) then the original buffer will be kept intact.
 *
 * @return 0 if the buffer was resized, failure otherwise
 */
static int kbase_io_history_resize(struct kbase_io_history *h, u16 new_size)
{
	struct kbase_io_access *old_buf;
	struct kbase_io_access *new_buf;
	unsigned long flags;

	if (!new_size)
		goto out_err; /* The new size must not be 0 */

	new_buf = vmalloc(new_size * sizeof(*h->buf));
	if (!new_buf)
		goto out_err;

	spin_lock_irqsave(&h->lock, flags);

	old_buf = h->buf;

	/* Note: we won't bother with copying the old data over. The dumping
	 * logic wouldn't work properly as it relies on 'count' both as a
	 * counter and as an index to the buffer which would have changed with
	 * the new array. This is a corner case that we don't need to support.
	 */
	h->count = 0;
	h->size = new_size;
	h->buf = new_buf;

	spin_unlock_irqrestore(&h->lock, flags);

	vfree(old_buf);

	return 0;

out_err:
	return -1;
}

int kbase_io_history_init(struct kbase_io_history *h, u16 n)
{
	h->enabled = false;
	spin_lock_init(&h->lock);
	h->count = 0;
	h->size = 0;
	h->buf = NULL;
	if (kbase_io_history_resize(h, n))
		return -1;

	return 0;
}

void kbase_io_history_term(struct kbase_io_history *h)
{
	vfree(h->buf);
	h->buf = NULL;
}

void kbase_io_history_add(struct kbase_io_history *h,
		void __iomem const *addr, u32 value, u8 write)
{
	struct kbase_io_access *io;
	unsigned long flags;

	spin_lock_irqsave(&h->lock, flags);

	io = &h->buf[h->count % h->size];
	io->addr = (uintptr_t)addr | write;
	io->value = value;
	++h->count;
	/* If count overflows, move the index by the buffer size so the entire
	 * buffer will still be dumped later
	 */
	if (unlikely(!h->count))
		h->count = h->size;

	spin_unlock_irqrestore(&h->lock, flags);
}

void kbase_io_history_dump(struct kbase_device *kbdev)
{
	struct kbase_io_history *const h = &kbdev->io_history;
	size_t i;
	size_t iters;
	unsigned long flags;

	if (!unlikely(h->enabled))
		return;

	spin_lock_irqsave(&h->lock, flags);

	dev_err(kbdev->dev, "Register IO History:");
	iters = (h->size > h->count) ? h->count : h->size;
	dev_err(kbdev->dev, "Last %zu register accesses of %zu total:\n", iters,
			h->count);
	for (i = 0; i < iters; ++i) {
		struct kbase_io_access *io =
			&h->buf[(h->count - iters + i) % h->size];
		char const access = (io->addr & 1) ? 'w' : 'r';

		dev_err(kbdev->dev, "%6zu: %c: reg 0x%016lx val %08x\n", i,
			access, (unsigned long)(io->addr & ~0x1), io->value);
	}

	spin_unlock_irqrestore(&h->lock, flags);
}

static int regs_history_size_get(void *data, u64 *val)
{
	struct kbase_io_history *const h = data;

	*val = h->size;

	return 0;
}

static int regs_history_size_set(void *data, u64 val)
{
	struct kbase_io_history *const h = data;

	return kbase_io_history_resize(h, (u16)val);
}


DEFINE_SIMPLE_ATTRIBUTE(regs_history_size_fops,
		regs_history_size_get,
		regs_history_size_set,
		"%llu\n");


/**
 * regs_history_show - show callback for the register access history file.
 *
 * @sfile: The debugfs entry
 * @data: Data associated with the entry
 *
 * This function is called to dump all recent accesses to the GPU registers.
 *
 * @return 0 if successfully prints data in debugfs entry file, failure
 * otherwise
 */
static int regs_history_show(struct seq_file *sfile, void *data)
{
	struct kbase_io_history *const h = sfile->private;
	size_t i;
	size_t iters;
	unsigned long flags;

	if (!h->enabled) {
		seq_puts(sfile, "The register access history is disabled\n");
		goto out;
	}

	spin_lock_irqsave(&h->lock, flags);

	iters = (h->size > h->count) ? h->count : h->size;
	seq_printf(sfile, "Last %zu register accesses of %zu total:\n", iters,
			h->count);
	for (i = 0; i < iters; ++i) {
		struct kbase_io_access *io =
			&h->buf[(h->count - iters + i) % h->size];
		char const access = (io->addr & 1) ? 'w' : 'r';

		seq_printf(sfile, "%6zu: %c: reg 0x%016lx val %08x\n", i,
			   access, (unsigned long)(io->addr & ~0x1), io->value);
	}

	spin_unlock_irqrestore(&h->lock, flags);

out:
	return 0;
}

/**
 * regs_history_open - open operation for regs_history debugfs file
 *
 * @in: &struct inode pointer
 * @file: &struct file pointer
 *
 * @return file descriptor
 */
static int regs_history_open(struct inode *in, struct file *file)
{
	return single_open(file, &regs_history_show, in->i_private);
}

static const struct file_operations regs_history_fops = {
	.owner = THIS_MODULE,
	.open = &regs_history_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbasep_regs_history_debugfs_init(struct kbase_device *kbdev)
{
	debugfs_create_bool("regs_history_enabled", S_IRUGO | S_IWUSR,
			kbdev->mali_debugfs_directory,
			&kbdev->io_history.enabled);
	debugfs_create_file("regs_history_size", S_IRUGO | S_IWUSR,
			kbdev->mali_debugfs_directory,
			&kbdev->io_history, &regs_history_size_fops);
	debugfs_create_file("regs_history", S_IRUGO,
			kbdev->mali_debugfs_directory, &kbdev->io_history,
			&regs_history_fops);
}
#endif /* defined(CONFIG_DEBUG_FS) && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */
