/*
 *
 * (C) COPYRIGHT 2011-2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include "mali_kbase_vinstr.h"
#include "mali_kbase_hwcnt_virtualizer.h"
#include "mali_kbase_hwcnt_types.h"
#include "mali_kbase_hwcnt_reader.h"
#include "mali_kbase_hwcnt_gpu.h"
#include "mali_kbase_ioctl.h"
#include "mali_malisw.h"
#include "mali_kbase_debug.h"

#include <linux/anon_inodes.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/* Hwcnt reader API version */
#define HWCNT_READER_API 1

/* The minimum allowed interval between dumps (equivalent to 10KHz) */
#define DUMP_INTERVAL_MIN_NS (100 * NSEC_PER_USEC)

/* The maximum allowed buffers per client */
#define MAX_BUFFER_COUNT 32

/**
 * struct kbase_vinstr_context - IOCTL interface for userspace hardware
 *                               counters.
 * @hvirt:         Hardware counter virtualizer used by vinstr.
 * @metadata:      Hardware counter metadata provided by virtualizer.
 * @lock:          Lock protecting all vinstr state.
 * @suspend_count: Suspend reference count. If non-zero, timer and worker are
 *                 prevented from being re-scheduled.
 * @client_count:  Number of vinstr clients.
 * @clients:       List of vinstr clients.
 * @dump_timer:    Timer that enqueues dump_work to a workqueue.
 * @dump_work:     Worker for performing periodic counter dumps.
 */
struct kbase_vinstr_context {
	struct kbase_hwcnt_virtualizer *hvirt;
	const struct kbase_hwcnt_metadata *metadata;
	struct mutex lock;
	size_t suspend_count;
	size_t client_count;
	struct list_head clients;
	struct hrtimer dump_timer;
	struct work_struct dump_work;
};

/**
 * struct kbase_vinstr_client - A vinstr client attached to a vinstr context.
 * @vctx:              Vinstr context client is attached to.
 * @hvcli:             Hardware counter virtualizer client.
 * @node:              Node used to attach this client to list in vinstr
 *                     context.
 * @dump_interval_ns:  Interval between periodic dumps. If 0, not a periodic
 *                     client.
 * @next_dump_time_ns: Time in ns when this client's next periodic dump must
 *                     occur. If 0, not a periodic client.
 * @enable_map:        Counters enable map.
 * @dump_bufs:         Array of dump buffers allocated by this client.
 * @dump_bufs_meta:    Metadata of dump buffers.
 * @meta_idx:          Index of metadata being accessed by userspace.
 * @read_idx:          Index of buffer read by userspace.
 * @write_idx:         Index of buffer being written by dump worker.
 * @waitq:             Client's notification queue.
 */
struct kbase_vinstr_client {
	struct kbase_vinstr_context *vctx;
	struct kbase_hwcnt_virtualizer_client *hvcli;
	struct list_head node;
	u64 next_dump_time_ns;
	u32 dump_interval_ns;
	struct kbase_hwcnt_enable_map enable_map;
	struct kbase_hwcnt_dump_buffer_array dump_bufs;
	struct kbase_hwcnt_reader_metadata *dump_bufs_meta;
	atomic_t meta_idx;
	atomic_t read_idx;
	atomic_t write_idx;
	wait_queue_head_t waitq;
};

static unsigned int kbasep_vinstr_hwcnt_reader_poll(
	struct file *filp,
	poll_table *wait);

static long kbasep_vinstr_hwcnt_reader_ioctl(
	struct file *filp,
	unsigned int cmd,
	unsigned long arg);

static int kbasep_vinstr_hwcnt_reader_mmap(
	struct file *filp,
	struct vm_area_struct *vma);

static int kbasep_vinstr_hwcnt_reader_release(
	struct inode *inode,
	struct file *filp);

/* Vinstr client file operations */
static const struct file_operations vinstr_client_fops = {
	.owner = THIS_MODULE,
	.poll           = kbasep_vinstr_hwcnt_reader_poll,
	.unlocked_ioctl = kbasep_vinstr_hwcnt_reader_ioctl,
	.compat_ioctl   = kbasep_vinstr_hwcnt_reader_ioctl,
	.mmap           = kbasep_vinstr_hwcnt_reader_mmap,
	.release        = kbasep_vinstr_hwcnt_reader_release,
};

/**
 * kbasep_vinstr_timestamp_ns() - Get the current time in nanoseconds.
 *
 * Return: Current time in nanoseconds.
 */
static u64 kbasep_vinstr_timestamp_ns(void)
{
	struct timespec ts;

	getrawmonotonic(&ts);
	return (u64)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}

/**
 * kbasep_vinstr_next_dump_time_ns() - Calculate the next periodic dump time.
 * @cur_ts_ns: Current time in nanoseconds.
 * @interval:  Interval between dumps in nanoseconds.
 *
 * Return: 0 if interval is 0 (i.e. a non-periodic client), or the next dump
 *         time that occurs after cur_ts_ns.
 */
static u64 kbasep_vinstr_next_dump_time_ns(u64 cur_ts_ns, u32 interval)
{
	/* Non-periodic client */
	if (interval == 0)
		return 0;

	/*
	 * Return the next interval after the current time relative to t=0.
	 * This means multiple clients with the same period will synchronise,
	 * regardless of when they were started, allowing the worker to be
	 * scheduled less frequently.
	 */
	do_div(cur_ts_ns, interval);
	return (cur_ts_ns + 1) * interval;
}

/**
 * kbasep_vinstr_client_dump() - Perform a dump for a client.
 * @vcli:     Non-NULL pointer to a vinstr client.
 * @event_id: Event type that triggered the dump.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_vinstr_client_dump(
	struct kbase_vinstr_client *vcli,
	enum base_hwcnt_reader_event event_id)
{
	int errcode;
	u64 ts_start_ns;
	u64 ts_end_ns;
	unsigned int write_idx;
	unsigned int read_idx;
	struct kbase_hwcnt_dump_buffer *dump_buf;
	struct kbase_hwcnt_reader_metadata *meta;

	WARN_ON(!vcli);
	lockdep_assert_held(&vcli->vctx->lock);

	write_idx = atomic_read(&vcli->write_idx);
	read_idx = atomic_read(&vcli->read_idx);

	/* Check if there is a place to copy HWC block into. */
	if (write_idx - read_idx == vcli->dump_bufs.buf_cnt)
		return -EBUSY;
	write_idx %= vcli->dump_bufs.buf_cnt;

	dump_buf = &vcli->dump_bufs.bufs[write_idx];
	meta = &vcli->dump_bufs_meta[write_idx];

	errcode = kbase_hwcnt_virtualizer_client_dump(
		vcli->hvcli, &ts_start_ns, &ts_end_ns, dump_buf);
	if (errcode)
		return errcode;

	/* Patch the dump buf headers, to hide the counters that other hwcnt
	 * clients are using.
	 */
	kbase_hwcnt_gpu_patch_dump_headers(dump_buf, &vcli->enable_map);

	/* Zero all non-enabled counters (current values are undefined) */
	kbase_hwcnt_dump_buffer_zero_non_enabled(dump_buf, &vcli->enable_map);

	meta->timestamp = ts_end_ns;
	meta->event_id = event_id;
	meta->buffer_idx = write_idx;

	/* Notify client. Make sure all changes to memory are visible. */
	wmb();
	atomic_inc(&vcli->write_idx);
	wake_up_interruptible(&vcli->waitq);
	return 0;
}

/**
 * kbasep_vinstr_client_clear() - Reset all the client's counters to zero.
 * @vcli: Non-NULL pointer to a vinstr client.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_vinstr_client_clear(struct kbase_vinstr_client *vcli)
{
	u64 ts_start_ns;
	u64 ts_end_ns;

	WARN_ON(!vcli);
	lockdep_assert_held(&vcli->vctx->lock);

	/* A virtualizer dump with a NULL buffer will just clear the virtualizer
	 * client's buffer.
	 */
	return kbase_hwcnt_virtualizer_client_dump(
		vcli->hvcli, &ts_start_ns, &ts_end_ns, NULL);
}

/**
 * kbasep_vinstr_reschedule_worker() - Update next dump times for all periodic
 *                                     vinstr clients, then reschedule the dump
 *                                     worker appropriately.
 * @vctx: Non-NULL pointer to the vinstr context.
 *
 * If there are no periodic clients, then the dump worker will not be
 * rescheduled. Else, the dump worker will be rescheduled for the next periodic
 * client dump.
 */
static void kbasep_vinstr_reschedule_worker(struct kbase_vinstr_context *vctx)
{
	u64 cur_ts_ns;
	u64 earliest_next_ns = U64_MAX;
	struct kbase_vinstr_client *pos;

	WARN_ON(!vctx);
	lockdep_assert_held(&vctx->lock);

	cur_ts_ns = kbasep_vinstr_timestamp_ns();

	/*
	 * Update each client's next dump time, and find the earliest next
	 * dump time if any of the clients have a non-zero interval.
	 */
	list_for_each_entry(pos, &vctx->clients, node) {
		const u64 cli_next_ns =
			kbasep_vinstr_next_dump_time_ns(
				cur_ts_ns, pos->dump_interval_ns);

		/* Non-zero next dump time implies a periodic client */
		if ((cli_next_ns != 0) && (cli_next_ns < earliest_next_ns))
			earliest_next_ns = cli_next_ns;

		pos->next_dump_time_ns = cli_next_ns;
	}

	/* Cancel the timer if it is already pending */
	hrtimer_cancel(&vctx->dump_timer);

	/* Start the timer if there are periodic clients and vinstr is not
	 * suspended.
	 */
	if ((earliest_next_ns != U64_MAX) &&
	    (vctx->suspend_count == 0) &&
	    !WARN_ON(earliest_next_ns < cur_ts_ns))
		hrtimer_start(
			&vctx->dump_timer,
			ns_to_ktime(earliest_next_ns - cur_ts_ns),
			HRTIMER_MODE_REL);
}

/**
 * kbasep_vinstr_dump_worker()- Dump worker, that dumps all periodic clients
 *                              that need to be dumped, then reschedules itself.
 * @work: Work structure.
 */
static void kbasep_vinstr_dump_worker(struct work_struct *work)
{
	struct kbase_vinstr_context *vctx =
		container_of(work, struct kbase_vinstr_context, dump_work);
	struct kbase_vinstr_client *pos;
	u64 cur_time_ns;

	mutex_lock(&vctx->lock);

	cur_time_ns = kbasep_vinstr_timestamp_ns();

	/* Dump all periodic clients whose next dump time is before the current
	 * time.
	 */
	list_for_each_entry(pos, &vctx->clients, node) {
		if ((pos->next_dump_time_ns != 0) &&
			(pos->next_dump_time_ns < cur_time_ns))
			kbasep_vinstr_client_dump(
				pos, BASE_HWCNT_READER_EVENT_PERIODIC);
	}

	/* Update the next dump times of all periodic clients, then reschedule
	 * this worker at the earliest next dump time.
	 */
	kbasep_vinstr_reschedule_worker(vctx);

	mutex_unlock(&vctx->lock);
}

/**
 * kbasep_vinstr_dump_timer() - Dump timer that schedules the dump worker for
 *                              execution as soon as possible.
 * @timer: Timer structure.
 */
static enum hrtimer_restart kbasep_vinstr_dump_timer(struct hrtimer *timer)
{
	struct kbase_vinstr_context *vctx =
		container_of(timer, struct kbase_vinstr_context, dump_timer);

	/* We don't need to check vctx->suspend_count here, as the suspend
	 * function will ensure that any worker enqueued here is immediately
	 * cancelled, and the worker itself won't reschedule this timer if
	 * suspend_count != 0.
	 */
#if KERNEL_VERSION(3, 16, 0) > LINUX_VERSION_CODE
	queue_work(system_wq, &vctx->dump_work);
#else
	queue_work(system_highpri_wq, &vctx->dump_work);
#endif
	return HRTIMER_NORESTART;
}

/**
 * kbasep_vinstr_client_destroy() - Destroy a vinstr client.
 * @vcli: vinstr client. Must not be attached to a vinstr context.
 */
static void kbasep_vinstr_client_destroy(struct kbase_vinstr_client *vcli)
{
	if (!vcli)
		return;

	kbase_hwcnt_virtualizer_client_destroy(vcli->hvcli);
	kfree(vcli->dump_bufs_meta);
	kbase_hwcnt_dump_buffer_array_free(&vcli->dump_bufs);
	kbase_hwcnt_enable_map_free(&vcli->enable_map);
	kfree(vcli);
}

/**
 * kbasep_vinstr_client_create() - Create a vinstr client. Does not attach to
 *                                 the vinstr context.
 * @vctx:     Non-NULL pointer to vinstr context.
 * @setup:    Non-NULL pointer to hardware counter ioctl setup structure.
 *            setup->buffer_count must not be 0.
 * @out_vcli: Non-NULL pointer to where created client will be stored on
 *            success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_vinstr_client_create(
	struct kbase_vinstr_context *vctx,
	struct kbase_ioctl_hwcnt_reader_setup *setup,
	struct kbase_vinstr_client **out_vcli)
{
	int errcode;
	struct kbase_vinstr_client *vcli;
	struct kbase_hwcnt_physical_enable_map phys_em;

	WARN_ON(!vctx);
	WARN_ON(!setup);
	WARN_ON(setup->buffer_count == 0);

	vcli = kzalloc(sizeof(*vcli), GFP_KERNEL);
	if (!vcli)
		return -ENOMEM;

	vcli->vctx = vctx;

	errcode = kbase_hwcnt_enable_map_alloc(
		vctx->metadata, &vcli->enable_map);
	if (errcode)
		goto error;

	phys_em.jm_bm = setup->jm_bm;
	phys_em.shader_bm = setup->shader_bm;
	phys_em.tiler_bm = setup->tiler_bm;
	phys_em.mmu_l2_bm = setup->mmu_l2_bm;
	kbase_hwcnt_gpu_enable_map_from_physical(&vcli->enable_map, &phys_em);

	errcode = kbase_hwcnt_dump_buffer_array_alloc(
		vctx->metadata, setup->buffer_count, &vcli->dump_bufs);
	if (errcode)
		goto error;

	errcode = -ENOMEM;
	vcli->dump_bufs_meta = kmalloc_array(
		setup->buffer_count, sizeof(*vcli->dump_bufs_meta), GFP_KERNEL);
	if (!vcli->dump_bufs_meta)
		goto error;

	errcode = kbase_hwcnt_virtualizer_client_create(
		vctx->hvirt, &vcli->enable_map, &vcli->hvcli);
	if (errcode)
		goto error;

	init_waitqueue_head(&vcli->waitq);

	*out_vcli = vcli;
	return 0;
error:
	kbasep_vinstr_client_destroy(vcli);
	return errcode;
}

int kbase_vinstr_init(
	struct kbase_hwcnt_virtualizer *hvirt,
	struct kbase_vinstr_context **out_vctx)
{
	struct kbase_vinstr_context *vctx;
	const struct kbase_hwcnt_metadata *metadata;

	if (!hvirt || !out_vctx)
		return -EINVAL;

	metadata = kbase_hwcnt_virtualizer_metadata(hvirt);
	if (!metadata)
		return -EINVAL;

	vctx = kzalloc(sizeof(*vctx), GFP_KERNEL);
	if (!vctx)
		return -ENOMEM;

	vctx->hvirt = hvirt;
	vctx->metadata = metadata;

	mutex_init(&vctx->lock);
	INIT_LIST_HEAD(&vctx->clients);
	hrtimer_init(&vctx->dump_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vctx->dump_timer.function = kbasep_vinstr_dump_timer;
	INIT_WORK(&vctx->dump_work, kbasep_vinstr_dump_worker);

	*out_vctx = vctx;
	return 0;
}

void kbase_vinstr_term(struct kbase_vinstr_context *vctx)
{
	if (!vctx)
		return;

	cancel_work_sync(&vctx->dump_work);

	/* Non-zero client count implies client leak */
	if (WARN_ON(vctx->client_count != 0)) {
		struct kbase_vinstr_client *pos, *n;

		list_for_each_entry_safe(pos, n, &vctx->clients, node) {
			list_del(&pos->node);
			vctx->client_count--;
			kbasep_vinstr_client_destroy(pos);
		}
	}

	WARN_ON(vctx->client_count != 0);
	kfree(vctx);
}

void kbase_vinstr_suspend(struct kbase_vinstr_context *vctx)
{
	if (WARN_ON(!vctx))
		return;

	mutex_lock(&vctx->lock);

	if (!WARN_ON(vctx->suspend_count == SIZE_MAX))
		vctx->suspend_count++;

	mutex_unlock(&vctx->lock);

	/* Always sync cancel the timer and then the worker, regardless of the
	 * new suspend count.
	 *
	 * This ensures concurrent calls to kbase_vinstr_suspend() always block
	 * until vinstr is fully suspended.
	 *
	 * The timer is cancelled before the worker, as the timer
	 * unconditionally re-enqueues the worker, but the worker checks the
	 * suspend_count that we just incremented before rescheduling the timer.
	 *
	 * Therefore if we cancel the worker first, the timer might re-enqueue
	 * the worker before we cancel the timer, but the opposite is not
	 * possible.
	 */
	hrtimer_cancel(&vctx->dump_timer);
	cancel_work_sync(&vctx->dump_work);
}

void kbase_vinstr_resume(struct kbase_vinstr_context *vctx)
{
	if (WARN_ON(!vctx))
		return;

	mutex_lock(&vctx->lock);

	if (!WARN_ON(vctx->suspend_count == 0)) {
		vctx->suspend_count--;

		/* Last resume, so re-enqueue the worker if we have any periodic
		 * clients.
		 */
		if (vctx->suspend_count == 0) {
			struct kbase_vinstr_client *pos;
			bool has_periodic_clients = false;

			list_for_each_entry(pos, &vctx->clients, node) {
				if (pos->dump_interval_ns != 0) {
					has_periodic_clients = true;
					break;
				}
			}

			if (has_periodic_clients)
#if KERNEL_VERSION(3, 16, 0) > LINUX_VERSION_CODE
				queue_work(system_wq, &vctx->dump_work);
#else
				queue_work(system_highpri_wq, &vctx->dump_work);
#endif
		}
	}

	mutex_unlock(&vctx->lock);
}

int kbase_vinstr_hwcnt_reader_setup(
	struct kbase_vinstr_context *vctx,
	struct kbase_ioctl_hwcnt_reader_setup *setup)
{
	int errcode;
	int fd;
	struct kbase_vinstr_client *vcli = NULL;

	if (!vctx || !setup ||
	    (setup->buffer_count == 0) ||
	    (setup->buffer_count > MAX_BUFFER_COUNT))
		return -EINVAL;

	errcode = kbasep_vinstr_client_create(vctx, setup, &vcli);
	if (errcode)
		goto error;

	errcode = anon_inode_getfd(
		"[mali_vinstr_desc]",
		&vinstr_client_fops,
		vcli,
		O_RDONLY | O_CLOEXEC);
	if (errcode < 0)
		goto error;

	fd = errcode;

	/* Add the new client. No need to reschedule worker, as not periodic */
	mutex_lock(&vctx->lock);

	vctx->client_count++;
	list_add(&vcli->node, &vctx->clients);

	mutex_unlock(&vctx->lock);

	return fd;
error:
	kbasep_vinstr_client_destroy(vcli);
	return errcode;
}

/**
 * kbasep_vinstr_hwcnt_reader_buffer_ready() - Check if client has ready
 *                                             buffers.
 * @cli: Non-NULL pointer to vinstr client.
 *
 * Return: Non-zero if client has at least one dumping buffer filled that was
 *         not notified to user yet.
 */
static int kbasep_vinstr_hwcnt_reader_buffer_ready(
	struct kbase_vinstr_client *cli)
{
	WARN_ON(!cli);
	return atomic_read(&cli->write_idx) != atomic_read(&cli->meta_idx);
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_dump() - Dump ioctl command.
 * @cli: Non-NULL pointer to vinstr client.
 *
 * Return: 0 on success, else error code.
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_dump(
	struct kbase_vinstr_client *cli)
{
	int errcode;

	mutex_lock(&cli->vctx->lock);

	errcode = kbasep_vinstr_client_dump(
		cli, BASE_HWCNT_READER_EVENT_MANUAL);

	mutex_unlock(&cli->vctx->lock);
	return errcode;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_clear() - Clear ioctl command.
 * @cli: Non-NULL pointer to vinstr client.
 *
 * Return: 0 on success, else error code.
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_clear(
	struct kbase_vinstr_client *cli)
{
	int errcode;

	mutex_lock(&cli->vctx->lock);

	errcode = kbasep_vinstr_client_clear(cli);

	mutex_unlock(&cli->vctx->lock);
	return errcode;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_get_buffer() - Get buffer ioctl command.
 * @cli:    Non-NULL pointer to vinstr client.
 * @buffer: Non-NULL pointer to userspace buffer.
 * @size:   Size of buffer.
 *
 * Return: 0 on success, else error code.
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_get_buffer(
	struct kbase_vinstr_client *cli,
	void __user *buffer,
	size_t size)
{
	unsigned int meta_idx = atomic_read(&cli->meta_idx);
	unsigned int idx = meta_idx % cli->dump_bufs.buf_cnt;

	struct kbase_hwcnt_reader_metadata *meta = &cli->dump_bufs_meta[idx];

	/* Metadata sanity check. */
	WARN_ON(idx != meta->buffer_idx);

	if (sizeof(struct kbase_hwcnt_reader_metadata) != size)
		return -EINVAL;

	/* Check if there is any buffer available. */
	if (atomic_read(&cli->write_idx) == meta_idx)
		return -EAGAIN;

	/* Check if previously taken buffer was put back. */
	if (atomic_read(&cli->read_idx) != meta_idx)
		return -EBUSY;

	/* Copy next available buffer's metadata to user. */
	if (copy_to_user(buffer, meta, size))
		return -EFAULT;

	atomic_inc(&cli->meta_idx);

	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_put_buffer() - Put buffer ioctl command.
 * @cli:    Non-NULL pointer to vinstr client.
 * @buffer: Non-NULL pointer to userspace buffer.
 * @size:   Size of buffer.
 *
 * Return: 0 on success, else error code.
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_put_buffer(
	struct kbase_vinstr_client *cli,
	void __user *buffer,
	size_t size)
{
	unsigned int read_idx = atomic_read(&cli->read_idx);
	unsigned int idx = read_idx % cli->dump_bufs.buf_cnt;

	struct kbase_hwcnt_reader_metadata meta;

	if (sizeof(struct kbase_hwcnt_reader_metadata) != size)
		return -EINVAL;

	/* Check if any buffer was taken. */
	if (atomic_read(&cli->meta_idx) == read_idx)
		return -EPERM;

	/* Check if correct buffer is put back. */
	if (copy_from_user(&meta, buffer, size))
		return -EFAULT;
	if (idx != meta.buffer_idx)
		return -EINVAL;

	atomic_inc(&cli->read_idx);

	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_set_interval() - Set interval ioctl command.
 * @cli:      Non-NULL pointer to vinstr client.
 * @interval: Periodic dumping interval (disable periodic dumping if 0).
 *
 * Return: 0 always.
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_set_interval(
	struct kbase_vinstr_client *cli,
	u32 interval)
{
	mutex_lock(&cli->vctx->lock);

	if ((interval != 0) && (interval < DUMP_INTERVAL_MIN_NS))
		interval = DUMP_INTERVAL_MIN_NS;
	/* Update the interval, and put in a dummy next dump time */
	cli->dump_interval_ns = interval;
	cli->next_dump_time_ns = 0;

	/*
	 * If it's a periodic client, kick off the worker early to do a proper
	 * timer reschedule. Return value is ignored, as we don't care if the
	 * worker is already queued.
	 */
	if ((interval != 0) && (cli->vctx->suspend_count == 0))
#if KERNEL_VERSION(3, 16, 0) > LINUX_VERSION_CODE
		queue_work(system_wq, &cli->vctx->dump_work);
#else
		queue_work(system_highpri_wq, &cli->vctx->dump_work);
#endif

	mutex_unlock(&cli->vctx->lock);

	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_enable_event() - Enable event ioctl command.
 * @cli:      Non-NULL pointer to vinstr client.
 * @event_id: ID of event to enable.
 *
 * Return: 0 always.
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_enable_event(
		struct kbase_vinstr_client *cli,
		enum base_hwcnt_reader_event event_id)
{
	/* No-op, as events aren't supported */
	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_disable_event() - Disable event ioctl
 *                                                    command.
 * @cli:      Non-NULL pointer to vinstr client.
 * @event_id: ID of event to disable.
 *
 * Return: 0 always.
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_disable_event(
	struct kbase_vinstr_client *cli,
	enum base_hwcnt_reader_event event_id)
{
	/* No-op, as events aren't supported */
	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_get_hwver() - Get HW version ioctl command.
 * @cli:   Non-NULL pointer to vinstr client.
 * @hwver: Non-NULL pointer to user buffer where HW version will be stored.
 *
 * Return: 0 on success, else error code.
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_get_hwver(
	struct kbase_vinstr_client *cli,
	u32 __user *hwver)
{
	u32 ver = 0;
	const enum kbase_hwcnt_gpu_group_type type =
		kbase_hwcnt_metadata_group_type(cli->vctx->metadata, 0);

	switch (type) {
	case KBASE_HWCNT_GPU_GROUP_TYPE_V4:
		ver = 4;
		break;
	case KBASE_HWCNT_GPU_GROUP_TYPE_V5:
		ver = 5;
		break;
	default:
		WARN_ON(true);
	}

	if (ver != 0) {
		return put_user(ver, hwver);
	} else {
		return -EINVAL;
	}
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl() - hwcnt reader's ioctl.
 * @filp:   Non-NULL pointer to file structure.
 * @cmd:    User command.
 * @arg:    Command's argument.
 *
 * Return: 0 on success, else error code.
 */
static long kbasep_vinstr_hwcnt_reader_ioctl(
	struct file *filp,
	unsigned int cmd,
	unsigned long arg)
{
	long rcode;
	struct kbase_vinstr_client *cli;

	if (!filp || (_IOC_TYPE(cmd) != KBASE_HWCNT_READER))
		return -EINVAL;

	cli = filp->private_data;
	if (!cli)
		return -EINVAL;

	switch (cmd) {
	case KBASE_HWCNT_READER_GET_API_VERSION:
		rcode = put_user(HWCNT_READER_API, (u32 __user *)arg);
		break;
	case KBASE_HWCNT_READER_GET_HWVER:
		rcode = kbasep_vinstr_hwcnt_reader_ioctl_get_hwver(
			cli, (u32 __user *)arg);
		break;
	case KBASE_HWCNT_READER_GET_BUFFER_SIZE:
		rcode = put_user(
			(u32)cli->vctx->metadata->dump_buf_bytes,
			(u32 __user *)arg);
		break;
	case KBASE_HWCNT_READER_DUMP:
		rcode = kbasep_vinstr_hwcnt_reader_ioctl_dump(cli);
		break;
	case KBASE_HWCNT_READER_CLEAR:
		rcode = kbasep_vinstr_hwcnt_reader_ioctl_clear(cli);
		break;
	case KBASE_HWCNT_READER_GET_BUFFER:
		rcode = kbasep_vinstr_hwcnt_reader_ioctl_get_buffer(
			cli, (void __user *)arg, _IOC_SIZE(cmd));
		break;
	case KBASE_HWCNT_READER_PUT_BUFFER:
		rcode = kbasep_vinstr_hwcnt_reader_ioctl_put_buffer(
			cli, (void __user *)arg, _IOC_SIZE(cmd));
		break;
	case KBASE_HWCNT_READER_SET_INTERVAL:
		rcode = kbasep_vinstr_hwcnt_reader_ioctl_set_interval(
			cli, (u32)arg);
		break;
	case KBASE_HWCNT_READER_ENABLE_EVENT:
		rcode = kbasep_vinstr_hwcnt_reader_ioctl_enable_event(
			cli, (enum base_hwcnt_reader_event)arg);
		break;
	case KBASE_HWCNT_READER_DISABLE_EVENT:
		rcode = kbasep_vinstr_hwcnt_reader_ioctl_disable_event(
			cli, (enum base_hwcnt_reader_event)arg);
		break;
	default:
		WARN_ON(true);
		rcode = -EINVAL;
		break;
	}

	return rcode;
}

/**
 * kbasep_vinstr_hwcnt_reader_poll() - hwcnt reader's poll.
 * @filp: Non-NULL pointer to file structure.
 * @wait: Non-NULL pointer to poll table.
 *
 * Return: POLLIN if data can be read without blocking, 0 if data can not be
 *         read without blocking, else error code.
 */
static unsigned int kbasep_vinstr_hwcnt_reader_poll(
	struct file *filp,
	poll_table *wait)
{
	struct kbase_vinstr_client *cli;

	if (!filp || !wait)
		return -EINVAL;

	cli = filp->private_data;
	if (!cli)
		return -EINVAL;

	poll_wait(filp, &cli->waitq, wait);
	if (kbasep_vinstr_hwcnt_reader_buffer_ready(cli))
		return POLLIN;
	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_mmap() - hwcnt reader's mmap.
 * @filp: Non-NULL pointer to file structure.
 * @vma:  Non-NULL pointer to vma structure.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_vinstr_hwcnt_reader_mmap(
	struct file *filp,
	struct vm_area_struct *vma)
{
	struct kbase_vinstr_client *cli;
	unsigned long vm_size, size, addr, pfn, offset;

	if (!filp || !vma)
		return -EINVAL;

	cli = filp->private_data;
	if (!cli)
		return -EINVAL;

	vm_size = vma->vm_end - vma->vm_start;
	size = cli->dump_bufs.buf_cnt * cli->vctx->metadata->dump_buf_bytes;

	if (vma->vm_pgoff > (size >> PAGE_SHIFT))
		return -EINVAL;

	offset = vma->vm_pgoff << PAGE_SHIFT;
	if (vm_size > size - offset)
		return -EINVAL;

	addr = __pa(cli->dump_bufs.page_addr + offset);
	pfn = addr >> PAGE_SHIFT;

	return remap_pfn_range(
		vma, vma->vm_start, pfn, vm_size, vma->vm_page_prot);
}

/**
 * kbasep_vinstr_hwcnt_reader_release() - hwcnt reader's release.
 * @inode: Non-NULL pointer to inode structure.
 * @filp:  Non-NULL pointer to file structure.
 *
 * Return: 0 always.
 */
static int kbasep_vinstr_hwcnt_reader_release(struct inode *inode,
	struct file *filp)
{
	struct kbase_vinstr_client *vcli = filp->private_data;

	mutex_lock(&vcli->vctx->lock);

	vcli->vctx->client_count--;
	list_del(&vcli->node);

	mutex_unlock(&vcli->vctx->lock);

	kbasep_vinstr_client_destroy(vcli);

	return 0;
}
