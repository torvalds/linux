/*
 * ring_buffer_iterator.c
 *
 * Ring buffer and channel iterators. Get each event of a channel in order. Uses
 * a prio heap for per-cpu buffers, giving a O(log(NR_CPUS)) algorithmic
 * complexity for the "get next event" operation.
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
 *
 * Author:
 *	Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#include "../../wrapper/ringbuffer/iterator.h"
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/module.h>

/*
 * Safety factor taking into account internal kernel interrupt latency.
 * Assuming 250ms worse-case latency.
 */
#define MAX_SYSTEM_LATENCY	250

/*
 * Maximum delta expected between trace clocks. At most 1 jiffy delta.
 */
#define MAX_CLOCK_DELTA		(jiffies_to_usecs(1) * 1000)

/**
 * lib_ring_buffer_get_next_record - Get the next record in a buffer.
 * @chan: channel
 * @buf: buffer
 *
 * Returns the size of the event read, -EAGAIN if buffer is empty, -ENODATA if
 * buffer is empty and finalized. The buffer must already be opened for reading.
 */
ssize_t lib_ring_buffer_get_next_record(struct channel *chan,
					struct lib_ring_buffer *buf)
{
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	struct lib_ring_buffer_iter *iter = &buf->iter;
	int ret;

restart:
	switch (iter->state) {
	case ITER_GET_SUBBUF:
		ret = lib_ring_buffer_get_next_subbuf(buf);
		if (ret && !ACCESS_ONCE(buf->finalized)
		    && config->alloc == RING_BUFFER_ALLOC_GLOBAL) {
			/*
			 * Use "pull" scheme for global buffers. The reader
			 * itself flushes the buffer to "pull" data not visible
			 * to readers yet. Flush current subbuffer and re-try.
			 *
			 * Per-CPU buffers rather use a "push" scheme because
			 * the IPI needed to flush all CPU's buffers is too
			 * costly. In the "push" scheme, the reader waits for
			 * the writer periodic deferrable timer to flush the
			 * buffers (keeping track of a quiescent state
			 * timestamp). Therefore, the writer "pushes" data out
			 * of the buffers rather than letting the reader "pull"
			 * data from the buffer.
			 */
			lib_ring_buffer_switch_slow(buf, SWITCH_ACTIVE);
			ret = lib_ring_buffer_get_next_subbuf(buf);
		}
		if (ret)
			return ret;
		iter->consumed = buf->cons_snapshot;
		iter->data_size = lib_ring_buffer_get_read_data_size(config, buf);
		iter->read_offset = iter->consumed;
		/* skip header */
		iter->read_offset += config->cb.subbuffer_header_size();
		iter->state = ITER_TEST_RECORD;
		goto restart;
	case ITER_TEST_RECORD:
		if (iter->read_offset - iter->consumed >= iter->data_size) {
			iter->state = ITER_PUT_SUBBUF;
		} else {
			CHAN_WARN_ON(chan, !config->cb.record_get);
			config->cb.record_get(config, chan, buf,
					      iter->read_offset,
					      &iter->header_len,
					      &iter->payload_len,
					      &iter->timestamp);
			iter->read_offset += iter->header_len;
			subbuffer_consume_record(config, &buf->backend);
			iter->state = ITER_NEXT_RECORD;
			return iter->payload_len;
		}
		goto restart;
	case ITER_NEXT_RECORD:
		iter->read_offset += iter->payload_len;
		iter->state = ITER_TEST_RECORD;
		goto restart;
	case ITER_PUT_SUBBUF:
		lib_ring_buffer_put_next_subbuf(buf);
		iter->state = ITER_GET_SUBBUF;
		goto restart;
	default:
		CHAN_WARN_ON(chan, 1);	/* Should not happen */
		return -EPERM;
	}
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_get_next_record);

static int buf_is_higher(void *a, void *b)
{
	struct lib_ring_buffer *bufa = a;
	struct lib_ring_buffer *bufb = b;

	/* Consider lowest timestamps to be at the top of the heap */
	return (bufa->iter.timestamp < bufb->iter.timestamp);
}

static
void lib_ring_buffer_get_empty_buf_records(const struct lib_ring_buffer_config *config,
					   struct channel *chan)
{
	struct lttng_ptr_heap *heap = &chan->iter.heap;
	struct lib_ring_buffer *buf, *tmp;
	ssize_t len;

	list_for_each_entry_safe(buf, tmp, &chan->iter.empty_head,
				 iter.empty_node) {
		len = lib_ring_buffer_get_next_record(chan, buf);

		/*
		 * Deal with -EAGAIN and -ENODATA.
		 * len >= 0 means record contains data.
		 * -EBUSY should never happen, because we support only one
		 * reader.
		 */
		switch (len) {
		case -EAGAIN:
			/* Keep node in empty list */
			break;
		case -ENODATA:
			/*
			 * Buffer is finalized. Don't add to list of empty
			 * buffer, because it has no more data to provide, ever.
			 */
			list_del(&buf->iter.empty_node);
			break;
		case -EBUSY:
			CHAN_WARN_ON(chan, 1);
			break;
		default:
			/*
			 * Insert buffer into the heap, remove from empty buffer
			 * list.
			 */
			CHAN_WARN_ON(chan, len < 0);
			list_del(&buf->iter.empty_node);
			CHAN_WARN_ON(chan, lttng_heap_insert(heap, buf));
		}
	}
}

static
void lib_ring_buffer_wait_for_qs(const struct lib_ring_buffer_config *config,
				 struct channel *chan)
{
	u64 timestamp_qs;
	unsigned long wait_msecs;

	/*
	 * No need to wait if no empty buffers are present.
	 */
	if (list_empty(&chan->iter.empty_head))
		return;

	timestamp_qs = config->cb.ring_buffer_clock_read(chan);
	/*
	 * We need to consider previously empty buffers.
	 * Do a get next buf record on each of them. Add them to
	 * the heap if they have data. If at least one of them
	 * don't have data, we need to wait for
	 * switch_timer_interval + MAX_SYSTEM_LATENCY (so we are sure the
	 * buffers have been switched either by the timer or idle entry) and
	 * check them again, adding them if they have data.
	 */
	lib_ring_buffer_get_empty_buf_records(config, chan);

	/*
	 * No need to wait if no empty buffers are present.
	 */
	if (list_empty(&chan->iter.empty_head))
		return;

	/*
	 * We need to wait for the buffer switch timer to run. If the
	 * CPU is idle, idle entry performed the switch.
	 * TODO: we could optimize further by skipping the sleep if all
	 * empty buffers belong to idle or offline cpus.
	 */
	wait_msecs = jiffies_to_msecs(chan->switch_timer_interval);
	wait_msecs += MAX_SYSTEM_LATENCY;
	msleep(wait_msecs);
	lib_ring_buffer_get_empty_buf_records(config, chan);
	/*
	 * Any buffer still in the empty list here cannot possibly
	 * contain an event with a timestamp prior to "timestamp_qs".
	 * The new quiescent state timestamp is the one we grabbed
	 * before waiting for buffer data.  It is therefore safe to
	 * ignore empty buffers up to last_qs timestamp for fusion
	 * merge.
	 */
	chan->iter.last_qs = timestamp_qs;
}

/**
 * channel_get_next_record - Get the next record in a channel.
 * @chan: channel
 * @ret_buf: the buffer in which the event is located (output)
 *
 * Returns the size of new current event, -EAGAIN if all buffers are empty,
 * -ENODATA if all buffers are empty and finalized. The channel must already be
 * opened for reading.
 */

ssize_t channel_get_next_record(struct channel *chan,
				struct lib_ring_buffer **ret_buf)
{
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	struct lib_ring_buffer *buf;
	struct lttng_ptr_heap *heap;
	ssize_t len;

	if (config->alloc == RING_BUFFER_ALLOC_GLOBAL) {
		*ret_buf = channel_get_ring_buffer(config, chan, 0);
		return lib_ring_buffer_get_next_record(chan, *ret_buf);
	}

	heap = &chan->iter.heap;

	/*
	 * get next record for topmost buffer.
	 */
	buf = lttng_heap_maximum(heap);
	if (buf) {
		len = lib_ring_buffer_get_next_record(chan, buf);
		/*
		 * Deal with -EAGAIN and -ENODATA.
		 * len >= 0 means record contains data.
		 */
		switch (len) {
		case -EAGAIN:
			buf->iter.timestamp = 0;
			list_add(&buf->iter.empty_node, &chan->iter.empty_head);
			/* Remove topmost buffer from the heap */
			CHAN_WARN_ON(chan, lttng_heap_remove(heap) != buf);
			break;
		case -ENODATA:
			/*
			 * Buffer is finalized. Remove buffer from heap and
			 * don't add to list of empty buffer, because it has no
			 * more data to provide, ever.
			 */
			CHAN_WARN_ON(chan, lttng_heap_remove(heap) != buf);
			break;
		case -EBUSY:
			CHAN_WARN_ON(chan, 1);
			break;
		default:
			/*
			 * Reinsert buffer into the heap. Note that heap can be
			 * partially empty, so we need to use
			 * lttng_heap_replace_max().
			 */
			CHAN_WARN_ON(chan, len < 0);
			CHAN_WARN_ON(chan, lttng_heap_replace_max(heap, buf) != buf);
			break;
		}
	}

	buf = lttng_heap_maximum(heap);
	if (!buf || buf->iter.timestamp > chan->iter.last_qs) {
		/*
		 * Deal with buffers previously showing no data.
		 * Add buffers containing data to the heap, update
		 * last_qs.
		 */
		lib_ring_buffer_wait_for_qs(config, chan);
	}

	*ret_buf = buf = lttng_heap_maximum(heap);
	if (buf) {
		/*
		 * If this warning triggers, you probably need to check your
		 * system interrupt latency. Typical causes: too many printk()
		 * output going to a serial console with interrupts off.
		 * Allow for MAX_CLOCK_DELTA ns timestamp delta going backward.
		 * Observed on SMP KVM setups with trace_clock().
		 */
		if (chan->iter.last_timestamp
		    > (buf->iter.timestamp + MAX_CLOCK_DELTA)) {
			printk(KERN_WARNING "ring_buffer: timestamps going "
			       "backward. Last time %llu ns, cpu %d, "
			       "current time %llu ns, cpu %d, "
			       "delta %llu ns.\n",
			       chan->iter.last_timestamp, chan->iter.last_cpu,
			       buf->iter.timestamp, buf->backend.cpu,
			       chan->iter.last_timestamp - buf->iter.timestamp);
			CHAN_WARN_ON(chan, 1);
		}
		chan->iter.last_timestamp = buf->iter.timestamp;
		chan->iter.last_cpu = buf->backend.cpu;
		return buf->iter.payload_len;
	} else {
		/* Heap is empty */
		if (list_empty(&chan->iter.empty_head))
			return -ENODATA;	/* All buffers finalized */
		else
			return -EAGAIN;		/* Temporarily empty */
	}
}
EXPORT_SYMBOL_GPL(channel_get_next_record);

static
void lib_ring_buffer_iterator_init(struct channel *chan, struct lib_ring_buffer *buf)
{
	if (buf->iter.allocated)
		return;

	buf->iter.allocated = 1;
	if (chan->iter.read_open && !buf->iter.read_open) {
		CHAN_WARN_ON(chan, lib_ring_buffer_open_read(buf) != 0);
		buf->iter.read_open = 1;
	}

	/* Add to list of buffers without any current record */
	if (chan->backend.config.alloc == RING_BUFFER_ALLOC_PER_CPU)
		list_add(&buf->iter.empty_node, &chan->iter.empty_head);
}

#ifdef CONFIG_HOTPLUG_CPU
static
int channel_iterator_cpu_hotplug(struct notifier_block *nb,
					   unsigned long action,
					   void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct channel *chan = container_of(nb, struct channel,
					    hp_iter_notifier);
	struct lib_ring_buffer *buf = per_cpu_ptr(chan->backend.buf, cpu);
	const struct lib_ring_buffer_config *config = &chan->backend.config;

	if (!chan->hp_iter_enable)
		return NOTIFY_DONE;

	CHAN_WARN_ON(chan, config->alloc == RING_BUFFER_ALLOC_GLOBAL);

	switch (action) {
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		lib_ring_buffer_iterator_init(chan, buf);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}
#endif

int channel_iterator_init(struct channel *chan)
{
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	struct lib_ring_buffer *buf;

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU) {
		int cpu, ret;

		INIT_LIST_HEAD(&chan->iter.empty_head);
		ret = lttng_heap_init(&chan->iter.heap,
				num_possible_cpus(),
				GFP_KERNEL, buf_is_higher);
		if (ret)
			return ret;
		/*
		 * In case of non-hotplug cpu, if the ring-buffer is allocated
		 * in early initcall, it will not be notified of secondary cpus.
		 * In that off case, we need to allocate for all possible cpus.
		 */
#ifdef CONFIG_HOTPLUG_CPU
		chan->hp_iter_notifier.notifier_call =
			channel_iterator_cpu_hotplug;
		chan->hp_iter_notifier.priority = 10;
		register_cpu_notifier(&chan->hp_iter_notifier);
		get_online_cpus();
		for_each_online_cpu(cpu) {
			buf = per_cpu_ptr(chan->backend.buf, cpu);
			lib_ring_buffer_iterator_init(chan, buf);
		}
		chan->hp_iter_enable = 1;
		put_online_cpus();
#else
		for_each_possible_cpu(cpu) {
			buf = per_cpu_ptr(chan->backend.buf, cpu);
			lib_ring_buffer_iterator_init(chan, buf);
		}
#endif
	} else {
		buf = channel_get_ring_buffer(config, chan, 0);
		lib_ring_buffer_iterator_init(chan, buf);
	}
	return 0;
}

void channel_iterator_unregister_notifiers(struct channel *chan)
{
	const struct lib_ring_buffer_config *config = &chan->backend.config;

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU) {
		chan->hp_iter_enable = 0;
		unregister_cpu_notifier(&chan->hp_iter_notifier);
	}
}

void channel_iterator_free(struct channel *chan)
{
	const struct lib_ring_buffer_config *config = &chan->backend.config;

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU)
		lttng_heap_free(&chan->iter.heap);
}

int lib_ring_buffer_iterator_open(struct lib_ring_buffer *buf)
{
	struct channel *chan = buf->backend.chan;
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	CHAN_WARN_ON(chan, config->output != RING_BUFFER_ITERATOR);
	return lib_ring_buffer_open_read(buf);
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_iterator_open);

/*
 * Note: Iterators must not be mixed with other types of outputs, because an
 * iterator can leave the buffer in "GET" state, which is not consistent with
 * other types of output (mmap, splice, raw data read).
 */
void lib_ring_buffer_iterator_release(struct lib_ring_buffer *buf)
{
	lib_ring_buffer_release_read(buf);
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_iterator_release);

int channel_iterator_open(struct channel *chan)
{
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	struct lib_ring_buffer *buf;
	int ret = 0, cpu;

	CHAN_WARN_ON(chan, config->output != RING_BUFFER_ITERATOR);

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU) {
		get_online_cpus();
		/* Allow CPU hotplug to keep track of opened reader */
		chan->iter.read_open = 1;
		for_each_channel_cpu(cpu, chan) {
			buf = channel_get_ring_buffer(config, chan, cpu);
			ret = lib_ring_buffer_iterator_open(buf);
			if (ret)
				goto error;
			buf->iter.read_open = 1;
		}
		put_online_cpus();
	} else {
		buf = channel_get_ring_buffer(config, chan, 0);
		ret = lib_ring_buffer_iterator_open(buf);
	}
	return ret;
error:
	/* Error should always happen on CPU 0, hence no close is required. */
	CHAN_WARN_ON(chan, cpu != 0);
	put_online_cpus();
	return ret;
}
EXPORT_SYMBOL_GPL(channel_iterator_open);

void channel_iterator_release(struct channel *chan)
{
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	struct lib_ring_buffer *buf;
	int cpu;

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU) {
		get_online_cpus();
		for_each_channel_cpu(cpu, chan) {
			buf = channel_get_ring_buffer(config, chan, cpu);
			if (buf->iter.read_open) {
				lib_ring_buffer_iterator_release(buf);
				buf->iter.read_open = 0;
			}
		}
		chan->iter.read_open = 0;
		put_online_cpus();
	} else {
		buf = channel_get_ring_buffer(config, chan, 0);
		lib_ring_buffer_iterator_release(buf);
	}
}
EXPORT_SYMBOL_GPL(channel_iterator_release);

void lib_ring_buffer_iterator_reset(struct lib_ring_buffer *buf)
{
	struct channel *chan = buf->backend.chan;

	if (buf->iter.state != ITER_GET_SUBBUF)
		lib_ring_buffer_put_next_subbuf(buf);
	buf->iter.state = ITER_GET_SUBBUF;
	/* Remove from heap (if present). */
	if (lttng_heap_cherrypick(&chan->iter.heap, buf))
		list_add(&buf->iter.empty_node, &chan->iter.empty_head);
	buf->iter.timestamp = 0;
	buf->iter.header_len = 0;
	buf->iter.payload_len = 0;
	buf->iter.consumed = 0;
	buf->iter.read_offset = 0;
	buf->iter.data_size = 0;
	/* Don't reset allocated and read_open */
}

void channel_iterator_reset(struct channel *chan)
{
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	struct lib_ring_buffer *buf;
	int cpu;

	/* Empty heap, put into empty_head */
	while ((buf = lttng_heap_remove(&chan->iter.heap)) != NULL)
		list_add(&buf->iter.empty_node, &chan->iter.empty_head);

	for_each_channel_cpu(cpu, chan) {
		buf = channel_get_ring_buffer(config, chan, cpu);
		lib_ring_buffer_iterator_reset(buf);
	}
	/* Don't reset read_open */
	chan->iter.last_qs = 0;
	chan->iter.last_timestamp = 0;
	chan->iter.last_cpu = 0;
	chan->iter.len_left = 0;
}

/*
 * Ring buffer payload extraction read() implementation.
 */
static
ssize_t channel_ring_buffer_file_read(struct file *filp,
				      char __user *user_buf,
				      size_t count,
				      loff_t *ppos,
				      struct channel *chan,
				      struct lib_ring_buffer *buf,
				      int fusionmerge)
{
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	size_t read_count = 0, read_offset;
	ssize_t len;

	might_sleep();
	if (!access_ok(VERIFY_WRITE, user_buf, count))
		return -EFAULT;

	/* Finish copy of previous record */
	if (*ppos != 0) {
		if (read_count < count) {
			len = chan->iter.len_left;
			read_offset = *ppos;
			if (config->alloc == RING_BUFFER_ALLOC_PER_CPU
			    && fusionmerge)
				buf = lttng_heap_maximum(&chan->iter.heap);
			CHAN_WARN_ON(chan, !buf);
			goto skip_get_next;
		}
	}

	while (read_count < count) {
		size_t copy_len, space_left;

		if (fusionmerge)
			len = channel_get_next_record(chan, &buf);
		else
			len = lib_ring_buffer_get_next_record(chan, buf);
len_test:
		if (len < 0) {
			/*
			 * Check if buffer is finalized (end of file).
			 */
			if (len == -ENODATA) {
				/* A 0 read_count will tell about end of file */
				goto nodata;
			}
			if (filp->f_flags & O_NONBLOCK) {
				if (!read_count)
					read_count = -EAGAIN;
				goto nodata;
			} else {
				int error;

				/*
				 * No data available at the moment, return what
				 * we got.
				 */
				if (read_count)
					goto nodata;

				/*
				 * Wait for returned len to be >= 0 or -ENODATA.
				 */
				if (fusionmerge)
					error = wait_event_interruptible(
					  chan->read_wait,
					  ((len = channel_get_next_record(chan,
						&buf)), len != -EAGAIN));
				else
					error = wait_event_interruptible(
					  buf->read_wait,
					  ((len = lib_ring_buffer_get_next_record(
						  chan, buf)), len != -EAGAIN));
				CHAN_WARN_ON(chan, len == -EBUSY);
				if (error) {
					read_count = error;
					goto nodata;
				}
				CHAN_WARN_ON(chan, len < 0 && len != -ENODATA);
				goto len_test;
			}
		}
		read_offset = buf->iter.read_offset;
skip_get_next:
		space_left = count - read_count;
		if (len <= space_left) {
			copy_len = len;
			chan->iter.len_left = 0;
			*ppos = 0;
		} else {
			copy_len = space_left;
			chan->iter.len_left = len - copy_len;
			*ppos = read_offset + copy_len;
		}
		if (__lib_ring_buffer_copy_to_user(&buf->backend, read_offset,
					       &user_buf[read_count],
					       copy_len)) {
			/*
			 * Leave the len_left and ppos values at their current
			 * state, as we currently have a valid event to read.
			 */
			return -EFAULT;
		}
		read_count += copy_len;
	};
	return read_count;

nodata:
	*ppos = 0;
	chan->iter.len_left = 0;
	return read_count;
}

/**
 * lib_ring_buffer_file_read - Read buffer record payload.
 * @filp: file structure pointer.
 * @buffer: user buffer to read data into.
 * @count: number of bytes to read.
 * @ppos: file read position.
 *
 * Returns a negative value on error, or the number of bytes read on success.
 * ppos is used to save the position _within the current record_ between calls
 * to read().
 */
static
ssize_t lib_ring_buffer_file_read(struct file *filp,
				  char __user *user_buf,
			          size_t count,
			          loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct lib_ring_buffer *buf = inode->i_private;
	struct channel *chan = buf->backend.chan;

	return channel_ring_buffer_file_read(filp, user_buf, count, ppos,
					     chan, buf, 0);
}

/**
 * channel_file_read - Read channel record payload.
 * @filp: file structure pointer.
 * @buffer: user buffer to read data into.
 * @count: number of bytes to read.
 * @ppos: file read position.
 *
 * Returns a negative value on error, or the number of bytes read on success.
 * ppos is used to save the position _within the current record_ between calls
 * to read().
 */
static
ssize_t channel_file_read(struct file *filp,
			  char __user *user_buf,
			  size_t count,
			  loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct channel *chan = inode->i_private;
	const struct lib_ring_buffer_config *config = &chan->backend.config;

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU)
		return channel_ring_buffer_file_read(filp, user_buf, count,
						     ppos, chan, NULL, 1);
	else {
		struct lib_ring_buffer *buf =
			channel_get_ring_buffer(config, chan, 0);
		return channel_ring_buffer_file_read(filp, user_buf, count,
						     ppos, chan, buf, 0);
	}
}

static
int lib_ring_buffer_file_open(struct inode *inode, struct file *file)
{
	struct lib_ring_buffer *buf = inode->i_private;
	int ret;

	ret = lib_ring_buffer_iterator_open(buf);
	if (ret)
		return ret;

	file->private_data = buf;
	ret = nonseekable_open(inode, file);
	if (ret)
		goto release_iter;
	return 0;

release_iter:
	lib_ring_buffer_iterator_release(buf);
	return ret;
}

static
int lib_ring_buffer_file_release(struct inode *inode, struct file *file)
{
	struct lib_ring_buffer *buf = inode->i_private;

	lib_ring_buffer_iterator_release(buf);
	return 0;
}

static
int channel_file_open(struct inode *inode, struct file *file)
{
	struct channel *chan = inode->i_private;
	int ret;

	ret = channel_iterator_open(chan);
	if (ret)
		return ret;

	file->private_data = chan;
	ret = nonseekable_open(inode, file);
	if (ret)
		goto release_iter;
	return 0;

release_iter:
	channel_iterator_release(chan);
	return ret;
}

static
int channel_file_release(struct inode *inode, struct file *file)
{
	struct channel *chan = inode->i_private;

	channel_iterator_release(chan);
	return 0;
}

const struct file_operations channel_payload_file_operations = {
	.owner = THIS_MODULE,
	.open = channel_file_open,
	.release = channel_file_release,
	.read = channel_file_read,
	.llseek = vfs_lib_ring_buffer_no_llseek,
};
EXPORT_SYMBOL_GPL(channel_payload_file_operations);

const struct file_operations lib_ring_buffer_payload_file_operations = {
	.owner = THIS_MODULE,
	.open = lib_ring_buffer_file_open,
	.release = lib_ring_buffer_file_release,
	.read = lib_ring_buffer_file_read,
	.llseek = vfs_lib_ring_buffer_no_llseek,
};
EXPORT_SYMBOL_GPL(lib_ring_buffer_payload_file_operations);
