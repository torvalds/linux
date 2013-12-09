/*
 * ring_buffer_vfs.c
 *
 * Ring Buffer VFS file operations.
 *
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/compat.h>

#include "../../wrapper/ringbuffer/backend.h"
#include "../../wrapper/ringbuffer/frontend.h"
#include "../../wrapper/ringbuffer/vfs.h"
#include "../../wrapper/poll.h"

static int put_ulong(unsigned long val, unsigned long arg)
{
	return put_user(val, (unsigned long __user *)arg);
}

#ifdef CONFIG_COMPAT
static int compat_put_ulong(compat_ulong_t val, unsigned long arg)
{
	return put_user(val, (compat_ulong_t __user *)compat_ptr(arg));
}
#endif

/*
 * This is not used by anonymous file descriptors. This code is left
 * there if we ever want to implement an inode with open() operation.
 */
int lib_ring_buffer_open(struct inode *inode, struct file *file,
		struct lib_ring_buffer *buf)
{
	int ret;

	if (!buf)
		return -EINVAL;

	ret = lib_ring_buffer_open_read(buf);
	if (ret)
		return ret;

	ret = nonseekable_open(inode, file);
	if (ret)
		goto release_read;
	return 0;

release_read:
	lib_ring_buffer_release_read(buf);
	return ret;
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_open);

/**
 *	vfs_lib_ring_buffer_open - ring buffer open file operation
 *	@inode: opened inode
 *	@file: opened file
 *
 *	Open implementation. Makes sure only one open instance of a buffer is
 *	done at a given moment.
 */
static
int vfs_lib_ring_buffer_open(struct inode *inode, struct file *file)
{
	struct lib_ring_buffer *buf = inode->i_private;

	file->private_data = buf;
	return lib_ring_buffer_open(inode, file, buf);
}

int lib_ring_buffer_release(struct inode *inode, struct file *file,
		struct lib_ring_buffer *buf)
{
	lib_ring_buffer_release_read(buf);

	return 0;
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_release);

/**
 *	vfs_lib_ring_buffer_release - ring buffer release file operation
 *	@inode: opened inode
 *	@file: opened file
 *
 *	Release implementation.
 */
static
int vfs_lib_ring_buffer_release(struct inode *inode, struct file *file)
{
	struct lib_ring_buffer *buf = file->private_data;

	return lib_ring_buffer_release(inode, file, buf);
}

unsigned int lib_ring_buffer_poll(struct file *filp, poll_table *wait,
		struct lib_ring_buffer *buf)
{
	unsigned int mask = 0;
	struct channel *chan = buf->backend.chan;
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	int finalized, disabled;

	if (filp->f_mode & FMODE_READ) {
		poll_wait_set_exclusive(wait);
		poll_wait(filp, &buf->read_wait, wait);

		finalized = lib_ring_buffer_is_finalized(config, buf);
		disabled = lib_ring_buffer_channel_is_disabled(chan);

		/*
		 * lib_ring_buffer_is_finalized() contains a smp_rmb() ordering
		 * finalized load before offsets loads.
		 */
		WARN_ON(atomic_long_read(&buf->active_readers) != 1);
retry:
		if (disabled)
			return POLLERR;

		if (subbuf_trunc(lib_ring_buffer_get_offset(config, buf), chan)
		  - subbuf_trunc(lib_ring_buffer_get_consumed(config, buf), chan)
		  == 0) {
			if (finalized)
				return POLLHUP;
			else {
				/*
				 * The memory barriers
				 * __wait_event()/wake_up_interruptible() take
				 * care of "raw_spin_is_locked" memory ordering.
				 */
				if (raw_spin_is_locked(&buf->raw_tick_nohz_spinlock))
					goto retry;
				else
					return 0;
			}
		} else {
			if (subbuf_trunc(lib_ring_buffer_get_offset(config, buf),
					 chan)
			  - subbuf_trunc(lib_ring_buffer_get_consumed(config, buf),
					 chan)
			  >= chan->backend.buf_size)
				return POLLPRI | POLLRDBAND;
			else
				return POLLIN | POLLRDNORM;
		}
	}
	return mask;
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_poll);

/**
 *	vfs_lib_ring_buffer_poll - ring buffer poll file operation
 *	@filp: the file
 *	@wait: poll table
 *
 *	Poll implementation.
 */
static
unsigned int vfs_lib_ring_buffer_poll(struct file *filp, poll_table *wait)
{
	struct lib_ring_buffer *buf = filp->private_data;

	return lib_ring_buffer_poll(filp, wait, buf);
}

long lib_ring_buffer_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg, struct lib_ring_buffer *buf)
{
	struct channel *chan = buf->backend.chan;
	const struct lib_ring_buffer_config *config = &chan->backend.config;

	if (lib_ring_buffer_channel_is_disabled(chan))
		return -EIO;

	switch (cmd) {
	case RING_BUFFER_SNAPSHOT:
		return lib_ring_buffer_snapshot(buf, &buf->cons_snapshot,
					    &buf->prod_snapshot);
	case RING_BUFFER_SNAPSHOT_GET_CONSUMED:
		return put_ulong(buf->cons_snapshot, arg);
	case RING_BUFFER_SNAPSHOT_GET_PRODUCED:
		return put_ulong(buf->prod_snapshot, arg);
	case RING_BUFFER_GET_SUBBUF:
	{
		unsigned long uconsume;
		long ret;

		ret = get_user(uconsume, (unsigned long __user *) arg);
		if (ret)
			return ret; /* will return -EFAULT */
		ret = lib_ring_buffer_get_subbuf(buf, uconsume);
		if (!ret) {
			/* Set file position to zero at each successful "get" */
			filp->f_pos = 0;
		}
		return ret;
	}
	case RING_BUFFER_PUT_SUBBUF:
		lib_ring_buffer_put_subbuf(buf);
		return 0;

	case RING_BUFFER_GET_NEXT_SUBBUF:
	{
		long ret;

		ret = lib_ring_buffer_get_next_subbuf(buf);
		if (!ret) {
			/* Set file position to zero at each successful "get" */
			filp->f_pos = 0;
		}
		return ret;
	}
	case RING_BUFFER_PUT_NEXT_SUBBUF:
		lib_ring_buffer_put_next_subbuf(buf);
		return 0;
	case RING_BUFFER_GET_SUBBUF_SIZE:
		return put_ulong(lib_ring_buffer_get_read_data_size(config, buf),
				 arg);
	case RING_BUFFER_GET_PADDED_SUBBUF_SIZE:
	{
		unsigned long size;

		size = lib_ring_buffer_get_read_data_size(config, buf);
		size = PAGE_ALIGN(size);
		return put_ulong(size, arg);
	}
	case RING_BUFFER_GET_MAX_SUBBUF_SIZE:
		return put_ulong(chan->backend.subbuf_size, arg);
	case RING_BUFFER_GET_MMAP_LEN:
	{
		unsigned long mmap_buf_len;

		if (config->output != RING_BUFFER_MMAP)
			return -EINVAL;
		mmap_buf_len = chan->backend.buf_size;
		if (chan->backend.extra_reader_sb)
			mmap_buf_len += chan->backend.subbuf_size;
		if (mmap_buf_len > INT_MAX)
			return -EFBIG;
		return put_ulong(mmap_buf_len, arg);
	}
	case RING_BUFFER_GET_MMAP_READ_OFFSET:
	{
		unsigned long sb_bindex;

		if (config->output != RING_BUFFER_MMAP)
			return -EINVAL;
		sb_bindex = subbuffer_id_get_index(config,
						   buf->backend.buf_rsb.id);
		return put_ulong(buf->backend.array[sb_bindex]->mmap_offset,
				 arg);
	}
	case RING_BUFFER_FLUSH:
		lib_ring_buffer_switch_remote(buf);
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_ioctl);

/**
 *	vfs_lib_ring_buffer_ioctl - control ring buffer reader synchronization
 *
 *	@filp: the file
 *	@cmd: the command
 *	@arg: command arg
 *
 *	This ioctl implements commands necessary for producer/consumer
 *	and flight recorder reader interaction :
 *	RING_BUFFER_GET_NEXT_SUBBUF
 *		Get the next sub-buffer that can be read. It never blocks.
 *	RING_BUFFER_PUT_NEXT_SUBBUF
 *		Release the currently read sub-buffer.
 *	RING_BUFFER_GET_SUBBUF_SIZE
 *		returns the size of the current sub-buffer.
 *	RING_BUFFER_GET_MAX_SUBBUF_SIZE
 *		returns the maximum size for sub-buffers.
 *	RING_BUFFER_GET_NUM_SUBBUF
 *		returns the number of reader-visible sub-buffers in the per cpu
 *              channel (for mmap).
 *      RING_BUFFER_GET_MMAP_READ_OFFSET
 *              returns the offset of the subbuffer belonging to the reader.
 *              Should only be used for mmap clients.
 */
static
long vfs_lib_ring_buffer_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct lib_ring_buffer *buf = filp->private_data;

	return lib_ring_buffer_ioctl(filp, cmd, arg, buf);
}

#ifdef CONFIG_COMPAT
long lib_ring_buffer_compat_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg, struct lib_ring_buffer *buf)
{
	struct channel *chan = buf->backend.chan;
	const struct lib_ring_buffer_config *config = &chan->backend.config;

	if (lib_ring_buffer_channel_is_disabled(chan))
		return -EIO;

	switch (cmd) {
	case RING_BUFFER_COMPAT_SNAPSHOT:
		return lib_ring_buffer_snapshot(buf, &buf->cons_snapshot,
						&buf->prod_snapshot);
	case RING_BUFFER_COMPAT_SNAPSHOT_GET_CONSUMED:
		return compat_put_ulong(buf->cons_snapshot, arg);
	case RING_BUFFER_COMPAT_SNAPSHOT_GET_PRODUCED:
		return compat_put_ulong(buf->prod_snapshot, arg);
	case RING_BUFFER_COMPAT_GET_SUBBUF:
	{
		__u32 uconsume;
		unsigned long consume;
		long ret;

		ret = get_user(uconsume, (__u32 __user *) arg);
		if (ret)
			return ret; /* will return -EFAULT */
		consume = buf->cons_snapshot;
		consume &= ~0xFFFFFFFFL;
		consume |= uconsume;
		ret = lib_ring_buffer_get_subbuf(buf, consume);
		if (!ret) {
			/* Set file position to zero at each successful "get" */
			filp->f_pos = 0;
		}
		return ret;
	}
	case RING_BUFFER_COMPAT_PUT_SUBBUF:
		lib_ring_buffer_put_subbuf(buf);
		return 0;

	case RING_BUFFER_COMPAT_GET_NEXT_SUBBUF:
	{
		long ret;

		ret = lib_ring_buffer_get_next_subbuf(buf);
		if (!ret) {
			/* Set file position to zero at each successful "get" */
			filp->f_pos = 0;
		}
		return ret;
	}
	case RING_BUFFER_COMPAT_PUT_NEXT_SUBBUF:
		lib_ring_buffer_put_next_subbuf(buf);
		return 0;
	case RING_BUFFER_COMPAT_GET_SUBBUF_SIZE:
	{
		unsigned long data_size;

		data_size = lib_ring_buffer_get_read_data_size(config, buf);
		if (data_size > UINT_MAX)
			return -EFBIG;
		return compat_put_ulong(data_size, arg);
	}
	case RING_BUFFER_COMPAT_GET_PADDED_SUBBUF_SIZE:
	{
		unsigned long size;

		size = lib_ring_buffer_get_read_data_size(config, buf);
		size = PAGE_ALIGN(size);
		if (size > UINT_MAX)
			return -EFBIG;
		return compat_put_ulong(size, arg);
	}
	case RING_BUFFER_COMPAT_GET_MAX_SUBBUF_SIZE:
		if (chan->backend.subbuf_size > UINT_MAX)
			return -EFBIG;
		return compat_put_ulong(chan->backend.subbuf_size, arg);
	case RING_BUFFER_COMPAT_GET_MMAP_LEN:
	{
		unsigned long mmap_buf_len;

		if (config->output != RING_BUFFER_MMAP)
			return -EINVAL;
		mmap_buf_len = chan->backend.buf_size;
		if (chan->backend.extra_reader_sb)
			mmap_buf_len += chan->backend.subbuf_size;
		if (mmap_buf_len > UINT_MAX)
			return -EFBIG;
		return compat_put_ulong(mmap_buf_len, arg);
	}
	case RING_BUFFER_COMPAT_GET_MMAP_READ_OFFSET:
	{
		unsigned long sb_bindex, read_offset;

		if (config->output != RING_BUFFER_MMAP)
			return -EINVAL;
		sb_bindex = subbuffer_id_get_index(config,
						   buf->backend.buf_rsb.id);
		read_offset = buf->backend.array[sb_bindex]->mmap_offset;
		if (read_offset > UINT_MAX)
			return -EINVAL;
		return compat_put_ulong(read_offset, arg);
	}
	case RING_BUFFER_COMPAT_FLUSH:
		lib_ring_buffer_switch_remote(buf);
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_compat_ioctl);

static
long vfs_lib_ring_buffer_compat_ioctl(struct file *filp, unsigned int cmd,
				  unsigned long arg)
{
	struct lib_ring_buffer *buf = filp->private_data;

	return lib_ring_buffer_compat_ioctl(filp, cmd, arg, buf);
}
#endif

const struct file_operations lib_ring_buffer_file_operations = {
	.owner = THIS_MODULE,
	.open = vfs_lib_ring_buffer_open,
	.release = vfs_lib_ring_buffer_release,
	.poll = vfs_lib_ring_buffer_poll,
	.splice_read = vfs_lib_ring_buffer_splice_read,
	.mmap = vfs_lib_ring_buffer_mmap,
	.unlocked_ioctl = vfs_lib_ring_buffer_ioctl,
	.llseek = vfs_lib_ring_buffer_no_llseek,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vfs_lib_ring_buffer_compat_ioctl,
#endif
};
EXPORT_SYMBOL_GPL(lib_ring_buffer_file_operations);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Ring Buffer Library VFS");
