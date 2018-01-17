/*
 *
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include "mali_kbase.h"

#include "mali_kbase_regs_history_debugfs.h"

#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_MALI_NO_MALI)

#include <linux/debugfs.h>


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
	u16 i;
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

		seq_printf(sfile, "%6i: %c: reg 0x%p val %08x\n", i, access,
				(void *)(io->addr & ~0x1), io->value);
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


#endif /* CONFIG_DEBUG_FS */
