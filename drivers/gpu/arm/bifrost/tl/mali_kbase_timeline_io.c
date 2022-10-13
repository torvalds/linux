// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
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

#include "mali_kbase_timeline_priv.h"
#include "mali_kbase_tlstream.h"
#include "mali_kbase_tracepoints.h"
#include "mali_kbase_timeline.h"

#include <device/mali_kbase_device.h>

#include <linux/poll.h>
#include <linux/version_compat_defs.h>
#include <linux/anon_inodes.h>

/* The timeline stream file operations functions. */
static ssize_t kbasep_timeline_io_read(struct file *filp, char __user *buffer,
				       size_t size, loff_t *f_pos);
static __poll_t kbasep_timeline_io_poll(struct file *filp, poll_table *wait);
static int kbasep_timeline_io_release(struct inode *inode, struct file *filp);
static int kbasep_timeline_io_fsync(struct file *filp, loff_t start, loff_t end,
				    int datasync);

/**
 * kbasep_timeline_io_packet_pending - check timeline streams for pending
 *                                     packets
 *
 * @timeline:      Timeline instance
 * @ready_stream:  Pointer to variable where stream will be placed
 * @rb_idx_raw:    Pointer to variable where read buffer index will be placed
 *
 * Function checks all streams for pending packets. It will stop as soon as
 * packet ready to be submitted to user space is detected. Variables under
 * pointers, passed as the parameters to this function will be updated with
 * values pointing to right stream and buffer.
 *
 * Return: non-zero if any of timeline streams has at last one packet ready
 */
static int
kbasep_timeline_io_packet_pending(struct kbase_timeline *timeline,
				  struct kbase_tlstream **ready_stream,
				  unsigned int *rb_idx_raw)
{
	enum tl_stream_type i;

	KBASE_DEBUG_ASSERT(ready_stream);
	KBASE_DEBUG_ASSERT(rb_idx_raw);

	for (i = (enum tl_stream_type)0; i < TL_STREAM_TYPE_COUNT; ++i) {
		struct kbase_tlstream *stream = &timeline->streams[i];
		*rb_idx_raw = atomic_read(&stream->rbi);
		/* Read buffer index may be updated by writer in case of
		 * overflow. Read and write buffer indexes must be
		 * loaded in correct order.
		 */
		smp_rmb();
		if (atomic_read(&stream->wbi) != *rb_idx_raw) {
			*ready_stream = stream;
			return 1;
		}
	}

	return 0;
}

/**
 * kbasep_timeline_has_header_data() - check timeline headers for pending
 *                                     packets
 *
 * @timeline:      Timeline instance
 *
 * Return: non-zero if any of timeline headers has at last one packet ready.
 */
static int kbasep_timeline_has_header_data(struct kbase_timeline *timeline)
{
	return timeline->obj_header_btc || timeline->aux_header_btc
#if MALI_USE_CSF
	       || timeline->csf_tl_reader.tl_header.btc
#endif
		;
}

/**
 * copy_stream_header() - copy timeline stream header.
 *
 * @buffer:      Pointer to the buffer provided by user.
 * @size:        Maximum amount of data that can be stored in the buffer.
 * @copy_len:    Pointer to amount of bytes that has been copied already
 *               within the read system call.
 * @hdr:         Pointer to the stream header.
 * @hdr_size:    Header size.
 * @hdr_btc:     Pointer to the remaining number of bytes to copy.
 *
 * Return: 0 if success, -1 otherwise.
 */
static inline int copy_stream_header(char __user *buffer, size_t size,
				     ssize_t *copy_len, const char *hdr,
				     size_t hdr_size, size_t *hdr_btc)
{
	const size_t offset = hdr_size - *hdr_btc;
	const size_t copy_size = MIN(size - *copy_len, *hdr_btc);

	if (!*hdr_btc)
		return 0;

	if (WARN_ON(*hdr_btc > hdr_size))
		return -1;

	if (copy_to_user(&buffer[*copy_len], &hdr[offset], copy_size))
		return -1;

	*hdr_btc -= copy_size;
	*copy_len += copy_size;

	return 0;
}

/**
 * kbasep_timeline_copy_headers - copy timeline headers to the user
 *
 * @timeline:    Timeline instance
 * @buffer:      Pointer to the buffer provided by user
 * @size:        Maximum amount of data that can be stored in the buffer
 * @copy_len:    Pointer to amount of bytes that has been copied already
 *               within the read system call.
 *
 * This helper function checks if timeline headers have not been sent
 * to the user, and if so, sends them. copy_len is respectively
 * updated.
 *
 * Return: 0 if success, -1 if copy_to_user has failed.
 */
static inline int kbasep_timeline_copy_headers(struct kbase_timeline *timeline,
					       char __user *buffer, size_t size,
					       ssize_t *copy_len)
{
	if (copy_stream_header(buffer, size, copy_len, obj_desc_header,
			       obj_desc_header_size, &timeline->obj_header_btc))
		return -1;

	if (copy_stream_header(buffer, size, copy_len, aux_desc_header,
			       aux_desc_header_size, &timeline->aux_header_btc))
		return -1;
#if MALI_USE_CSF
	if (copy_stream_header(buffer, size, copy_len,
			       timeline->csf_tl_reader.tl_header.data,
			       timeline->csf_tl_reader.tl_header.size,
			       &timeline->csf_tl_reader.tl_header.btc))
		return -1;
#endif
	return 0;
}

/**
 * kbasep_timeline_io_read - copy data from streams to buffer provided by user
 *
 * @filp:   Pointer to file structure
 * @buffer: Pointer to the buffer provided by user
 * @size:   Maximum amount of data that can be stored in the buffer
 * @f_pos:  Pointer to file offset (unused)
 *
 * Return: number of bytes stored in the buffer
 */
static ssize_t kbasep_timeline_io_read(struct file *filp, char __user *buffer,
				       size_t size, loff_t *f_pos)
{
	ssize_t copy_len = 0;
	struct kbase_timeline *timeline;

	KBASE_DEBUG_ASSERT(filp);
	KBASE_DEBUG_ASSERT(f_pos);

	if (WARN_ON(!filp->private_data))
		return -EFAULT;

	timeline = (struct kbase_timeline *)filp->private_data;

	if (!buffer)
		return -EINVAL;

	if (*f_pos < 0)
		return -EINVAL;

	mutex_lock(&timeline->reader_lock);

	while (copy_len < size) {
		struct kbase_tlstream *stream = NULL;
		unsigned int rb_idx_raw = 0;
		unsigned int wb_idx_raw;
		unsigned int rb_idx;
		size_t rb_size;

		if (kbasep_timeline_copy_headers(timeline, buffer, size,
						 &copy_len)) {
			copy_len = -EFAULT;
			break;
		}

		/* If we already read some packets and there is no
		 * packet pending then return back to user.
		 * If we don't have any data yet, wait for packet to be
		 * submitted.
		 */
		if (copy_len > 0) {
			if (!kbasep_timeline_io_packet_pending(
				    timeline, &stream, &rb_idx_raw))
				break;
		} else {
			if (wait_event_interruptible(
				    timeline->event_queue,
				    kbasep_timeline_io_packet_pending(
					    timeline, &stream, &rb_idx_raw))) {
				copy_len = -ERESTARTSYS;
				break;
			}
		}

		if (WARN_ON(!stream)) {
			copy_len = -EFAULT;
			break;
		}

		/* Check if this packet fits into the user buffer.
		 * If so copy its content.
		 */
		rb_idx = rb_idx_raw % PACKET_COUNT;
		rb_size = atomic_read(&stream->buffer[rb_idx].size);
		if (rb_size > size - copy_len)
			break;
		if (copy_to_user(&buffer[copy_len], stream->buffer[rb_idx].data,
				 rb_size)) {
			copy_len = -EFAULT;
			break;
		}

		/* If the distance between read buffer index and write
		 * buffer index became more than PACKET_COUNT, then overflow
		 * happened and we need to ignore the last portion of bytes
		 * that we have just sent to user.
		 */
		smp_rmb();
		wb_idx_raw = atomic_read(&stream->wbi);

		if (wb_idx_raw - rb_idx_raw < PACKET_COUNT) {
			copy_len += rb_size;
			atomic_inc(&stream->rbi);
#if MALI_UNIT_TEST
			atomic_add(rb_size, &timeline->bytes_collected);
#endif /* MALI_UNIT_TEST */

		} else {
			const unsigned int new_rb_idx_raw =
				wb_idx_raw - PACKET_COUNT + 1;
			/* Adjust read buffer index to the next valid buffer */
			atomic_set(&stream->rbi, new_rb_idx_raw);
		}
	}

	mutex_unlock(&timeline->reader_lock);

	return copy_len;
}

/**
 * kbasep_timeline_io_poll - poll timeline stream for packets
 * @filp: Pointer to file structure
 * @wait: Pointer to poll table
 *
 * Return: EPOLLIN | EPOLLRDNORM if data can be read without blocking,
 *         otherwise zero, or EPOLLHUP | EPOLLERR on error.
 */
static __poll_t kbasep_timeline_io_poll(struct file *filp, poll_table *wait)
{
	struct kbase_tlstream *stream;
	unsigned int rb_idx;
	struct kbase_timeline *timeline;

	KBASE_DEBUG_ASSERT(filp);
	KBASE_DEBUG_ASSERT(wait);

	if (WARN_ON(!filp->private_data))
		return EPOLLHUP | EPOLLERR;

	timeline = (struct kbase_timeline *)filp->private_data;

	/* If there are header bytes to copy, read will not block */
	if (kbasep_timeline_has_header_data(timeline))
		return EPOLLIN | EPOLLRDNORM;

	poll_wait(filp, &timeline->event_queue, wait);
	if (kbasep_timeline_io_packet_pending(timeline, &stream, &rb_idx))
		return EPOLLIN | EPOLLRDNORM;

	return (__poll_t)0;
}

int kbase_timeline_io_acquire(struct kbase_device *kbdev, u32 flags)
{
	/* The timeline stream file operations structure. */
	static const struct file_operations kbasep_tlstream_fops = {
		.owner = THIS_MODULE,
		.release = kbasep_timeline_io_release,
		.read = kbasep_timeline_io_read,
		.poll = kbasep_timeline_io_poll,
		.fsync = kbasep_timeline_io_fsync,
	};
	int err;

	if (WARN_ON(!kbdev) || (flags & ~BASE_TLSTREAM_FLAGS_MASK))
		return -EINVAL;

	err = kbase_timeline_acquire(kbdev, flags);
	if (err)
		return err;

	err = anon_inode_getfd("[mali_tlstream]", &kbasep_tlstream_fops, kbdev->timeline,
			       O_RDONLY | O_CLOEXEC);
	if (err < 0)
		kbase_timeline_release(kbdev->timeline);

	return err;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int kbasep_timeline_io_open(struct inode *in, struct file *file)
{
	struct kbase_device *const kbdev = in->i_private;

	if (WARN_ON(!kbdev))
		return -EFAULT;

	file->private_data = kbdev->timeline;
	return kbase_timeline_acquire(kbdev, BASE_TLSTREAM_FLAGS_MASK &
						     ~BASE_TLSTREAM_JOB_DUMPING_ENABLED);
}

void kbase_timeline_io_debugfs_init(struct kbase_device *const kbdev)
{
	static const struct file_operations kbasep_tlstream_debugfs_fops = {
		.owner = THIS_MODULE,
		.open = kbasep_timeline_io_open,
		.release = kbasep_timeline_io_release,
		.read = kbasep_timeline_io_read,
		.poll = kbasep_timeline_io_poll,
		.fsync = kbasep_timeline_io_fsync,
	};
	struct dentry *file;

	if (WARN_ON(!kbdev) || WARN_ON(IS_ERR_OR_NULL(kbdev->mali_debugfs_directory)))
		return;

	file = debugfs_create_file("tlstream", 0444, kbdev->mali_debugfs_directory, kbdev,
				   &kbasep_tlstream_debugfs_fops);

	if (IS_ERR_OR_NULL(file))
		dev_warn(kbdev->dev, "Unable to create timeline debugfs entry");
}
#else
/*
 * Stub function for when debugfs is disabled
 */
void kbase_timeline_io_debugfs_init(struct kbase_device *const kbdev)
{
}
#endif

/**
 * kbasep_timeline_io_release - release timeline stream descriptor
 * @inode: Pointer to inode structure
 * @filp:  Pointer to file structure
 *
 * Return: always return zero
 */
static int kbasep_timeline_io_release(struct inode *inode, struct file *filp)
{
	CSTD_UNUSED(inode);

	kbase_timeline_release(filp->private_data);
	return 0;
}

static int kbasep_timeline_io_fsync(struct file *filp, loff_t start, loff_t end,
				    int datasync)
{
	CSTD_UNUSED(start);
	CSTD_UNUSED(end);
	CSTD_UNUSED(datasync);

	return kbase_timeline_streams_flush(filp->private_data);
}
