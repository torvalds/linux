/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
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
	int tid, cpu, header_size, available, contiguous, length1, length2, size, count = count_orig & 0x7fffffff;

	if (*offset)
		return -EINVAL;

	if (!collect_annotations) {
		return count_orig;
	}

	cpu = 0; // Annotation only uses a single per-cpu buffer as the data must be in order to the engine

	if (file == NULL) {
		tid = -1; // set the thread id to the kernel thread
	} else {
		tid = current->pid;
	}

	// synchronize between cores
	spin_lock(&annotate_lock);

	// determine total size of the payload
	header_size = MAXSIZE_PACK32 * 3 + MAXSIZE_PACK64;
	available = buffer_bytes_available(cpu, ANNOTATE_BUF) - header_size;
	size = count < available ? count : available;

	if (size <= 0) {
		size = 0;
		goto annotate_write_out;
	}

	// synchronize shared variables annotateBuf and annotatePos
	if (collect_annotations && per_cpu(gator_buffer, cpu)[ANNOTATE_BUF]) {
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, smp_processor_id());
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, tid);
		gator_buffer_write_packed_int64(cpu, ANNOTATE_BUF, gator_get_time());
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
		buffer_check(cpu, ANNOTATE_BUF);
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

	if (per_cpu(gator_buffer, cpu)[ANNOTATE_BUF] && buffer_check_space(cpu, ANNOTATE_BUF, MAXSIZE_PACK64 + 2 * MAXSIZE_PACK32)) {
		uint32_t tid = current->pid;
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, tid);
		gator_buffer_write_packed_int64(cpu, ANNOTATE_BUF, 0); // time
		gator_buffer_write_packed_int(cpu, ANNOTATE_BUF, 0);   // size
	}

	spin_unlock(&annotate_lock);

	return 0;
}

static const struct file_operations annotate_fops = {
	.write		= annotate_write,
	.release	= annotate_release
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
	collect_annotations = false;
}
