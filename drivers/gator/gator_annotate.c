/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <asm/current.h>
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(annotate_lock);
static bool collect_annotations = false;

static int annotate_copy(struct file *file, char const __user *buf, size_t count)
{
	int cpu = 0;
	int write = per_cpu(gator_buffer_write, cpu)[ANNOTATE_BUF];

	if (file == NULL) {
		// copy from kernel
		memcpy(&per_cpu(gator_buffer, cpu)[ANNOTATE_BUF][write], buf, count);
	} else {
		// copy from user space
		if (copy_from_user(&per_cpu(gator_buffer, cpu)[ANNOTATE_BUF][write], buf, count) != 0)
			return -1;
	}
	per_cpu(gator_buffer_write, cpu)[ANNOTATE_BUF] = (write + count) & gator_buffer_mask[ANNOTATE_BUF];

	return 0;
}

static ssize_t annotate_write(struct file *file, char const __user *buf, size_t count_orig, loff_t *offset)
{
	int pid, cpu, header_size, available, contiguous, length1, length2, size, count = count_orig & 0x7fffffff;

	if (*offset) {
		return -EINVAL;
	}

	// Annotations are not supported in interrupt context
	if (in_interrupt()) {
		printk(KERN_WARNING "gator: Annotations are not supported in interrupt context\n");
		return -EINVAL;
	}

 retry:
	// synchronize between cores and with collect_annotations
	spin_lock(&annotate_lock);

	if (!collect_annotations) {
		// Not collecting annotations, tell the caller everything was written
		size = count_orig;
		goto annotate_write_out;
	}

	// Annotation only uses a single per-cpu buffer as the data must be in order to the engine
	cpu = 0;

	if (current == NULL) {
		pid = 0;
	} else {
		pid = current->pid;
	}

	// determine total size of the payload
	header_size = MAXSIZE_PACK32 * 3 + MAXSIZE_PACK64;
	available = buffer_bytes_available(cpu, ANNOTATE_BUF) - header_size;
	size = count < available ? count : available;

	if (size <= 0) {
		// Buffer is full, wait until space is available
		spin_unlock(&annotate_lock);
		wait_event_interruptible(gator_annotate_wait, buffer_bytes_available(cpu, ANNOTATE_BUF) > header_size || !collect_annotations);
		goto retry;
	}

	// synchronize shared variables annotateBuf and annotatePos
	if (per_cpu(gator_buffer, cpu)[ANNOTATE_BUF]) {
		u64 time = gator_get_time();
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, get_physical_cpu());
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, pid);
		gator_buffer_write_packed_int64(cpu, ANNOTATE_BUF, time);
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, size);

		// determine the sizes to capture, length1 + length2 will equal size
		contiguous = contiguous_space_available(cpu, ANNOTATE_BUF);
		if (size < contiguous) {
			length1 = size;
			length2 = 0;
		} else {
			length1 = contiguous;
			length2 = size - contiguous;
		}

		if (annotate_copy(file, buf, length1) != 0) {
			size = -EINVAL;
			goto annotate_write_out;
		}

		if (length2 > 0 && annotate_copy(file, &buf[length1], length2) != 0) {
			size = -EINVAL;
			goto annotate_write_out;
		}

		// Check and commit; commit is set to occur once buffer is 3/4 full
		buffer_check(cpu, ANNOTATE_BUF, time);
	}

annotate_write_out:
	spin_unlock(&annotate_lock);

	// return the number of bytes written
	return size;
}

#include "gator_annotate_kernel.c"

static int annotate_release(struct inode *inode, struct file *file)
{
	int cpu = 0;

	// synchronize between cores
	spin_lock(&annotate_lock);

	if (per_cpu(gator_buffer, cpu)[ANNOTATE_BUF] && buffer_check_space(cpu, ANNOTATE_BUF, MAXSIZE_PACK64 + 3 * MAXSIZE_PACK32)) {
		uint32_t pid = current->pid;
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, get_physical_cpu());
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, pid);
		gator_buffer_write_packed_int64(cpu, ANNOTATE_BUF, 0);	// time
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, 0);	// size
	}

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, ANNOTATE_BUF, gator_get_time());

	spin_unlock(&annotate_lock);

	return 0;
}

static const struct file_operations annotate_fops = {
	.write = annotate_write,
	.release = annotate_release
};

static int gator_annotate_create_files(struct super_block *sb, struct dentry *root)
{
	return gatorfs_create_file_perm(sb, root, "annotate", &annotate_fops, 0666);
}

static int gator_annotate_start(void)
{
	collect_annotations = true;
	return 0;
}

static void gator_annotate_stop(void)
{
	// the spinlock here will ensure that when this function exits, we are not in the middle of an annotation
	spin_lock(&annotate_lock);
	collect_annotations = false;
	wake_up(&gator_annotate_wait);
	spin_unlock(&annotate_lock);
}
