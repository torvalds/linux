// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2021 ARM Limited. All rights reserved.
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

#include "mali_kbase_kinstr_prfcnt.h"
#include "mali_kbase_hwcnt_virtualizer.h"
#include "mali_kbase_hwcnt_types.h"
#include <uapi/gpu/arm/bifrost/mali_kbase_hwcnt_reader.h>
#include "mali_kbase_hwcnt_gpu.h"
#include <uapi/gpu/arm/bifrost/mali_kbase_ioctl.h>
#include "mali_malisw.h"
#include "mali_kbase_debug.h"

#include <linux/anon_inodes.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/* The minimum allowed interval between dumps, in nanoseconds
 * (equivalent to 10KHz)
 */
#define DUMP_INTERVAL_MIN_NS (100 * NSEC_PER_USEC)

/* The minimum allowed interval between dumps, in microseconds
 * (equivalent to 10KHz)
 */
#define DUMP_INTERVAL_MIN_US (DUMP_INTERVAL_MIN_NS / 1000)

/* The maximum allowed buffers per client */
#define MAX_BUFFER_COUNT 32

/**
 * struct kbase_kinstr_prfcnt_context - IOCTL interface for userspace hardware
 *                                      counters.
 * @hvirt:           Hardware counter virtualizer used by kinstr_prfcnt.
 * @info_item_count: Number of metadata elements.
 * @metadata:        Hardware counter metadata provided by virtualizer.
 * @lock:            Lock protecting kinstr_prfcnt state.
 * @suspend_count:   Suspend reference count. If non-zero, timer and worker
 *                   are prevented from being re-scheduled.
 * @client_count:    Number of kinstr_prfcnt clients.
 * @clients:         List of kinstr_prfcnt clients.
 * @dump_timer:      Timer that enqueues dump_work to a workqueue.
 * @dump_work:       Worker for performing periodic counter dumps.
 */
struct kbase_kinstr_prfcnt_context {
	struct kbase_hwcnt_virtualizer *hvirt;
	u32 info_item_count;
	const struct kbase_hwcnt_metadata *metadata;
	struct mutex lock;
	size_t suspend_count;
	size_t client_count;
	struct list_head clients;
	struct hrtimer dump_timer;
	struct work_struct dump_work;
};

/**
 * struct kbase_kinstr_prfcnt_sample - Buffer and descriptor for sample data.
 * @sample_meta: Pointer to samle metadata.
 * @dump_buf:    Dump buffer containing sample data.
 */
struct kbase_kinstr_prfcnt_sample {
	u64 *sample_meta;
	struct kbase_hwcnt_dump_buffer dump_buf;
};

/**
 * struct kbase_kinstr_prfcnt_sample_array - Array of sample data.
 * @page_addr:    Address of allocated pages. A single allocation is used
 *                for all Dump Buffers in the array.
 * @page_order: The allocation order of the pages.
 * @sample_count: Number of allocated samples.
 * @samples:      Non-NULL pointer to the array of Dump Buffers.
 */
struct kbase_kinstr_prfcnt_sample_array {
	u64 page_addr;
	unsigned int page_order;
	size_t sample_count;
	struct kbase_kinstr_prfcnt_sample *samples;
};

/**
 * struct kbase_kinstr_prfcnt_client_config - Client session configuration.
 * @prfcnt_mode:  Sampling mode: either manual or periodic.
 * @counter_set:  Set of performance counter blocks.
 * @buffer_count: Number of buffers used to store samples.
 * @period_us:    Sampling period, in microseconds, or 0 if manual mode.
 * @phys_em:      Enable map used by the GPU.
 */
struct kbase_kinstr_prfcnt_client_config {
	u8 prfcnt_mode;
	u8 counter_set;
	u16 buffer_count;
	u64 period_us;
	struct kbase_hwcnt_physical_enable_map phys_em;
};

/**
 * struct kbase_kinstr_prfcnt_client - A kinstr_prfcnt client attached
 *                                     to a kinstr_prfcnt context.
 * @kinstr_ctx:        kinstr_prfcnt context client is attached to.
 * @hvcli:             Hardware counter virtualizer client.
 * @node:              Node used to attach this client to list in kinstr_prfcnt
 *                     context.
 * @next_dump_time_ns: Time in ns when this client's next periodic dump must
 *                     occur. If 0, not a periodic client.
 * @dump_interval_ns:  Interval between periodic dumps. If 0, not a periodic
 *                     client.
 * @config:            Configuration of the client session.
 * @enable_map:        Counters enable map.
 * @tmp_buf:           Temporary buffer to use before handing over dump to
 *                     client.
 * @sample_arr:        Array of dump buffers allocated by this client.
 * @dump_bufs_meta:    Metadata of dump buffers.
 * @meta_idx:          Index of metadata being accessed by userspace.
 * @read_idx:          Index of buffer read by userspace.
 * @write_idx:         Index of buffer being written by dump worker.
 * @waitq:             Client's notification queue.
 * @sample_size:       Size of the data required for one sample, in bytes.
 * @sample_count:      Number of samples the client is able to capture.
 */
struct kbase_kinstr_prfcnt_client {
	struct kbase_kinstr_prfcnt_context *kinstr_ctx;
	struct kbase_hwcnt_virtualizer_client *hvcli;
	struct list_head node;
	u64 next_dump_time_ns;
	u32 dump_interval_ns;
	struct kbase_kinstr_prfcnt_client_config config;
	struct kbase_hwcnt_enable_map enable_map;
	struct kbase_hwcnt_dump_buffer tmp_buf;
	struct kbase_kinstr_prfcnt_sample_array sample_arr;
	struct kbase_hwcnt_reader_metadata *dump_bufs_meta;
	atomic_t meta_idx;
	atomic_t read_idx;
	atomic_t write_idx;
	wait_queue_head_t waitq;
	size_t sample_size;
	size_t sample_count;
};

static struct prfcnt_enum_item kinstr_prfcnt_supported_requests[] = {
	{
		/* Request description for MODE request */
		.hdr = {
				.item_type = PRFCNT_ENUM_TYPE_REQUEST,
				.item_version = PRFCNT_READER_API_VERSION,
		},
		.u.request = {
				.request_item_type = PRFCNT_REQUEST_MODE,
				.versions_mask = 0x1,
		},
	},
	{
		/* Request description for ENABLE request */
		.hdr = {
				.item_type = PRFCNT_ENUM_TYPE_REQUEST,
				.item_version = PRFCNT_READER_API_VERSION,
		},
		.u.request = {
				.request_item_type = PRFCNT_REQUEST_ENABLE,
				.versions_mask = 0x1,
		},
	},
};

/**
 * kbasep_kinstr_prfcnt_hwcnt_reader_buffer_ready() - Check if client has ready
 *                                                    buffers.
 * @cli: Non-NULL pointer to kinstr_prfcnt client.
 *
 * Return: Non-zero if client has at least one dumping buffer filled that was
 *         not notified to user yet.
 */
static int kbasep_kinstr_prfcnt_hwcnt_reader_buffer_ready(
	struct kbase_kinstr_prfcnt_client *cli)
{
	WARN_ON(!cli);
	return atomic_read(&cli->write_idx) != atomic_read(&cli->meta_idx);
}

/**
 * kbasep_kinstr_prfcnt_hwcnt_reader_poll() - hwcnt reader's poll.
 * @filp: Non-NULL pointer to file structure.
 * @wait: Non-NULL pointer to poll table.
 *
 * Return: POLLIN if data can be read without blocking, 0 if data can not be
 *         read without blocking, else error code.
 */
static unsigned int kbasep_kinstr_prfcnt_hwcnt_reader_poll(struct file *filp,
							   poll_table *wait)
{
	struct kbase_kinstr_prfcnt_client *cli;

	if (!filp || !wait)
		return -EINVAL;

	cli = filp->private_data;

	if (!cli)
		return -EINVAL;

	poll_wait(filp, &cli->waitq, wait);

	if (kbasep_kinstr_prfcnt_hwcnt_reader_buffer_ready(cli))
		return POLLIN;

	return 0;
}

/**
 * kbasep_kinstr_prfcnt_hwcnt_reader_ioctl() - hwcnt reader's ioctl.
 * @filp:   Non-NULL pointer to file structure.
 * @cmd:    User command.
 * @arg:    Command's argument.
 *
 * Return: 0 on success, else error code.
 */
static long kbasep_kinstr_prfcnt_hwcnt_reader_ioctl(struct file *filp,
						    unsigned int cmd,
						    unsigned long arg)
{
	long rcode;
	struct kbase_kinstr_prfcnt_client *cli;

	if (!filp || (_IOC_TYPE(cmd) != KBASE_HWCNT_READER))
		return -EINVAL;

	cli = filp->private_data;

	if (!cli)
		return -EINVAL;

	switch (_IOC_NR(cmd)) {
	default:
		pr_warn("Unknown HWCNT ioctl 0x%x nr:%d", cmd, _IOC_NR(cmd));
		rcode = -EINVAL;
		break;
	}

	return rcode;
}

/**
 * kbasep_kinstr_prfcnt_hwcnt_reader_mmap() - hwcnt reader's mmap.
 * @filp: Non-NULL pointer to file structure.
 * @vma:  Non-NULL pointer to vma structure.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_kinstr_prfcnt_hwcnt_reader_mmap(struct file *filp,
						  struct vm_area_struct *vma)
{
	struct kbase_kinstr_prfcnt_client *cli;
	unsigned long vm_size, size, addr, pfn, offset;

	if (!filp || !vma)
		return -EINVAL;

	cli = filp->private_data;

	if (!cli)
		return -EINVAL;

	vm_size = vma->vm_end - vma->vm_start;

	/* The mapping is allowed to span the entirety of the page allocation,
	 * not just the chunk where the dump buffers are allocated.
	 * This accommodates the corner case where the combined size of the
	 * dump buffers is smaller than a single page.
	 * This does not pose a security risk as the pages are zeroed on
	 * allocation, and anything out of bounds of the dump buffers is never
	 * written to.
	 */
	size = (1ull << cli->sample_arr.page_order) * PAGE_SIZE;

	if (vma->vm_pgoff > (size >> PAGE_SHIFT))
		return -EINVAL;

	offset = vma->vm_pgoff << PAGE_SHIFT;

	if (vm_size > size - offset)
		return -EINVAL;

	addr = __pa(cli->sample_arr.page_addr + offset);
	pfn = addr >> PAGE_SHIFT;

	return remap_pfn_range(vma, vma->vm_start, pfn, vm_size,
			       vma->vm_page_prot);
}

static void kbasep_kinstr_prfcnt_sample_array_free(
	struct kbase_kinstr_prfcnt_sample_array *sample_arr)
{
	if (!sample_arr)
		return;

	kfree((void *)sample_arr->samples);
	kfree((void *)(size_t)sample_arr->page_addr);
	memset(sample_arr, 0, sizeof(*sample_arr));
}

/**
 * kbasep_kinstr_prfcnt_client_destroy() - Destroy a kinstr_prfcnt client.
 * @cli: kinstr_prfcnt client. Must not be attached to a kinstr_prfcnt context.
 */
static void
kbasep_kinstr_prfcnt_client_destroy(struct kbase_kinstr_prfcnt_client *cli)
{
	if (!cli)
		return;

	kbase_hwcnt_virtualizer_client_destroy(cli->hvcli);
	kfree(cli->dump_bufs_meta);
	kbasep_kinstr_prfcnt_sample_array_free(&cli->sample_arr);
	kbase_hwcnt_dump_buffer_free(&cli->tmp_buf);
	kbase_hwcnt_enable_map_free(&cli->enable_map);
	kfree(cli);
}

/**
 * kbasep_kinstr_prfcnt_hwcnt_reader_release() - hwcnt reader's release.
 * @inode: Non-NULL pointer to inode structure.
 * @filp:  Non-NULL pointer to file structure.
 *
 * Return: 0 always.
 */
static int kbasep_kinstr_prfcnt_hwcnt_reader_release(struct inode *inode,
						     struct file *filp)
{
	struct kbase_kinstr_prfcnt_client *cli = filp->private_data;

	mutex_lock(&cli->kinstr_ctx->lock);

	WARN_ON(cli->kinstr_ctx->client_count == 0);
	if (cli->kinstr_ctx->client_count > 0)
		cli->kinstr_ctx->client_count--;
	list_del(&cli->node);

	mutex_unlock(&cli->kinstr_ctx->lock);

	kbasep_kinstr_prfcnt_client_destroy(cli);

	return 0;
}

/* kinstr_prfcnt client file operations */
static const struct file_operations kinstr_prfcnt_client_fops = {
	.owner = THIS_MODULE,
	.poll = kbasep_kinstr_prfcnt_hwcnt_reader_poll,
	.unlocked_ioctl = kbasep_kinstr_prfcnt_hwcnt_reader_ioctl,
	.compat_ioctl = kbasep_kinstr_prfcnt_hwcnt_reader_ioctl,
	.mmap = kbasep_kinstr_prfcnt_hwcnt_reader_mmap,
	.release = kbasep_kinstr_prfcnt_hwcnt_reader_release,
};

static size_t kbasep_kinstr_prfcnt_get_sample_size(
	const struct kbase_hwcnt_metadata *metadata,
	struct kbase_hwcnt_dump_buffer *dump_buf)
{
	size_t dump_buf_bytes;
	size_t clk_cnt_buf_bytes;
	size_t sample_meta_bytes;
	size_t block_count = 0;
	size_t grp, blk, blk_inst;

	if (!metadata)
		return 0;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst)
		block_count++;

	/* Reserve one for last sentinel item. */
	block_count++;

	sample_meta_bytes = sizeof(struct prfcnt_metadata) * block_count;
	dump_buf_bytes = metadata->dump_buf_bytes;
	clk_cnt_buf_bytes = sizeof(*dump_buf->clk_cnt_buf) * metadata->clk_cnt;

	return (sample_meta_bytes + dump_buf_bytes + clk_cnt_buf_bytes);
}

/**
 * kbasep_kinstr_prfcnt_dump_worker()- Dump worker, that dumps all periodic
 *                                     clients that need to be dumped, then
 *                                     reschedules itself.
 * @work: Work structure.
 */
static void kbasep_kinstr_prfcnt_dump_worker(struct work_struct *work)
{
	/* Do nothing. */
}

/**
 * kbasep_kinstr_prfcnt_dump_timer() - Dump timer that schedules the dump worker for
 *                              execution as soon as possible.
 * @timer: Timer structure.
 */
static enum hrtimer_restart
kbasep_kinstr_prfcnt_dump_timer(struct hrtimer *timer)
{
	return HRTIMER_NORESTART;
}

int kbase_kinstr_prfcnt_init(struct kbase_hwcnt_virtualizer *hvirt,
			     struct kbase_kinstr_prfcnt_context **out_kinstr_ctx)
{
	struct kbase_kinstr_prfcnt_context *kinstr_ctx;
	const struct kbase_hwcnt_metadata *metadata;

	if (!hvirt || !out_kinstr_ctx)
		return -EINVAL;

	metadata = kbase_hwcnt_virtualizer_metadata(hvirt);

	if (!metadata)
		return -EINVAL;

	kinstr_ctx = kzalloc(sizeof(*kinstr_ctx), GFP_KERNEL);

	if (!kinstr_ctx)
		return -ENOMEM;

	kinstr_ctx->hvirt = hvirt;
	kinstr_ctx->metadata = metadata;

	mutex_init(&kinstr_ctx->lock);
	INIT_LIST_HEAD(&kinstr_ctx->clients);
	hrtimer_init(&kinstr_ctx->dump_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	kinstr_ctx->dump_timer.function = kbasep_kinstr_prfcnt_dump_timer;
	INIT_WORK(&kinstr_ctx->dump_work, kbasep_kinstr_prfcnt_dump_worker);

	*out_kinstr_ctx = kinstr_ctx;
	return 0;
}

void kbase_kinstr_prfcnt_term(struct kbase_kinstr_prfcnt_context *kinstr_ctx)
{
	if (!kinstr_ctx)
		return;

	cancel_work_sync(&kinstr_ctx->dump_work);

	/* Non-zero client count implies client leak */
	if (WARN_ON(kinstr_ctx->client_count > 0)) {
		struct kbase_kinstr_prfcnt_client *pos, *n;

		list_for_each_entry_safe(pos, n, &kinstr_ctx->clients, node) {
			list_del(&pos->node);
			kinstr_ctx->client_count--;
			kbasep_kinstr_prfcnt_client_destroy(pos);
		}
	}

	WARN_ON(kinstr_ctx->client_count > 0);
	kfree(kinstr_ctx);
}

void kbase_kinstr_prfcnt_suspend(struct kbase_kinstr_prfcnt_context *kinstr_ctx)
{
	if (WARN_ON(!kinstr_ctx))
		return;

	mutex_lock(&kinstr_ctx->lock);

	if (!WARN_ON(kinstr_ctx->suspend_count == SIZE_MAX))
		kinstr_ctx->suspend_count++;

	mutex_unlock(&kinstr_ctx->lock);

	/* Always sync cancel the timer and then the worker, regardless of the
	 * new suspend count.
	 *
	 * This ensures concurrent calls to kbase_kinstr_prfcnt_suspend() always block
	 * until kinstr_prfcnt is fully suspended.
	 *
	 * The timer is canceled before the worker, as the timer
	 * unconditionally re-enqueues the worker, but the worker checks the
	 * suspend_count that we just incremented before rescheduling the timer.
	 *
	 * Therefore if we cancel the worker first, the timer might re-enqueue
	 * the worker before we cancel the timer, but the opposite is not
	 * possible.
	 */
	hrtimer_cancel(&kinstr_ctx->dump_timer);
	cancel_work_sync(&kinstr_ctx->dump_work);
}

void kbase_kinstr_prfcnt_resume(struct kbase_kinstr_prfcnt_context *kinstr_ctx)
{
	if (WARN_ON(!kinstr_ctx))
		return;

	mutex_lock(&kinstr_ctx->lock);

	if (!WARN_ON(kinstr_ctx->suspend_count == 0)) {
		kinstr_ctx->suspend_count--;

		/* Last resume, so re-enqueue the worker if we have any periodic
		 * clients.
		 */
		if (kinstr_ctx->suspend_count == 0) {
			struct kbase_kinstr_prfcnt_client *pos;
			bool has_periodic_clients = false;

			list_for_each_entry(pos, &kinstr_ctx->clients, node) {
				if (pos->dump_interval_ns != 0) {
					has_periodic_clients = true;
					break;
				}
			}

			if (has_periodic_clients)
				kbase_hwcnt_virtualizer_queue_work(
					kinstr_ctx->hvirt,
					&kinstr_ctx->dump_work);
		}
	}

	mutex_unlock(&kinstr_ctx->lock);
}

static int kbasep_kinstr_prfcnt_sample_array_alloc(
	const struct kbase_hwcnt_metadata *metadata, size_t n,
	struct kbase_kinstr_prfcnt_sample_array *sample_arr)
{
	struct kbase_kinstr_prfcnt_sample *samples;
	size_t sample_idx;
	u64 addr;
	unsigned int order;
	size_t dump_buf_bytes;
	size_t clk_cnt_buf_bytes;
	size_t sample_meta_bytes;
	size_t block_count = 0;
	size_t sample_size;
	size_t grp, blk, blk_inst;

	if (!metadata || !sample_arr)
		return -EINVAL;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst)
		block_count++;

	/* Reserve one for last sentinel item. */
	block_count++;

	sample_meta_bytes = sizeof(struct prfcnt_metadata) * block_count;
	dump_buf_bytes = metadata->dump_buf_bytes;
	clk_cnt_buf_bytes =
		sizeof(*samples->dump_buf.clk_cnt_buf) * metadata->clk_cnt;
	sample_size = sample_meta_bytes + dump_buf_bytes + clk_cnt_buf_bytes;

	samples = kmalloc_array(n, sizeof(*samples), GFP_KERNEL);

	if (!samples)
		return -ENOMEM;

	order = get_order(sample_size * n);
	addr = (u64)(uintptr_t)kzalloc(sample_size * n, GFP_KERNEL);

	if (!addr) {
		kfree((void *)samples);
		return -ENOMEM;
	}

	sample_arr->page_addr = addr;
	sample_arr->page_order = order;
	sample_arr->sample_count = n;
	sample_arr->samples = samples;

	for (sample_idx = 0; sample_idx < n; sample_idx++) {
		const size_t sample_meta_offset = sample_size * sample_idx;
		const size_t dump_buf_offset =
			sample_meta_offset + sample_meta_bytes;
		const size_t clk_cnt_buf_offset =
			dump_buf_offset + dump_buf_bytes;

		/* Internal layout in a sample buffer: [sample metadata, dump_buf, clk_cnt_buf]. */
		samples[sample_idx].dump_buf.metadata = metadata;
		samples[sample_idx].sample_meta =
			(u64 *)(uintptr_t)(addr + sample_meta_offset);
		samples[sample_idx].dump_buf.dump_buf =
			(u64 *)(uintptr_t)(addr + dump_buf_offset);
		samples[sample_idx].dump_buf.clk_cnt_buf =
			(u64 *)(uintptr_t)(addr + clk_cnt_buf_offset);
	}

	return 0;
}

static bool prfcnt_mode_supported(u8 mode)
{
	return (mode == PRFCNT_MODE_MANUAL) || (mode == PRFCNT_MODE_PERIODIC);
}

static void
kbasep_kinstr_prfcnt_block_enable_to_physical(uint32_t *phys_em,
					      const uint64_t *enable_mask)
{
	*phys_em |= kbase_hwcnt_backend_gpu_block_map_to_physical(
		enable_mask[0], enable_mask[1]);
}

/**
 * kbasep_kinstr_prfcnt_parse_request_enable - Parse an enable request
 * @req_enable: Performance counters enable request to parse.
 * @config:     Client object the session configuration should be written to.
 *
 * This function parses a performance counters enable request.
 * This type of request specifies a bitmask of HW counters to enable
 * for one performance counters block type. In addition to that,
 * a performance counters enable request may also set "global"
 * configuration properties that affect the whole session, like the
 * performance counters set, which shall be compatible with the same value
 * set by other performance request items.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_kinstr_prfcnt_parse_request_enable(
	const struct prfcnt_request_enable *req_enable,
	struct kbase_kinstr_prfcnt_client_config *config)
{
	int err = 0;
	u8 req_set = KBASE_HWCNT_SET_UNDEFINED, default_set;

	switch (req_enable->set) {
	case PRFCNT_SET_PRIMARY:
		req_set = KBASE_HWCNT_SET_PRIMARY;
		break;
	case PRFCNT_SET_SECONDARY:
		req_set = KBASE_HWCNT_SET_SECONDARY;
		break;
	case PRFCNT_SET_TERTIARY:
		req_set = KBASE_HWCNT_SET_TERTIARY;
		break;
	default:
		err = -EINVAL;
		break;
	}

	/* The performance counter set is a "global" property that affects
	 * the whole session. Either this is the first request that sets
	 * the value, or it shall be identical to all previous requests.
	 */
	if (!err) {
		if (config->counter_set == KBASE_HWCNT_SET_UNDEFINED)
			config->counter_set = req_set;
		else if (config->counter_set != req_set)
			err = -EINVAL;
	}

	/* Temporarily, the requested set cannot be different from the default
	 * set because it's the only one to be supported. This will change in
	 * the future.
	 */
#if defined(CONFIG_MALI_BIFROST_PRFCNT_SET_SECONDARY)
	default_set = KBASE_HWCNT_SET_SECONDARY;
#elif defined(CONFIG_MALI_PRFCNT_SET_TERTIARY)
	default_set = KBASE_HWCNT_SET_TERTIARY;
#else
	/* Default to primary */
	default_set = KBASE_HWCNT_SET_PRIMARY;
#endif

	if (req_set != default_set)
		err = -EINVAL;

	if (err < 0)
		return err;

	/* Enable the performance counters based on the bitmask provided
	 * by the user space client.
	 * It is possible to receive multiple requests for the same counter
	 * block, in which case the bitmask will be a logical OR of all the
	 * bitmasks given by the client.
	 */
	switch (req_enable->block_type) {
	case PRFCNT_BLOCK_TYPE_FE:
		kbasep_kinstr_prfcnt_block_enable_to_physical(
			&config->phys_em.fe_bm, req_enable->enable_mask);
		break;
	case PRFCNT_BLOCK_TYPE_TILER:
		kbasep_kinstr_prfcnt_block_enable_to_physical(
			&config->phys_em.tiler_bm, req_enable->enable_mask);
		break;
	case PRFCNT_BLOCK_TYPE_MEMORY:
		kbasep_kinstr_prfcnt_block_enable_to_physical(
			&config->phys_em.mmu_l2_bm, req_enable->enable_mask);
		break;
	case PRFCNT_BLOCK_TYPE_SHADER_CORE:
		kbasep_kinstr_prfcnt_block_enable_to_physical(
			&config->phys_em.shader_bm, req_enable->enable_mask);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

/**
 * kbasep_kinstr_prfcnt_parse_setup - Parse session setup
 * @kinstr_ctx: Pointer to the kinstr_prfcnt context.
 * @setup:      Session setup information to parse.
 * @config:     Client object the session configuration should be written to.
 *
 * This function parses the list of "request" items sent by the user space
 * client, and writes the configuration for the new client to be created
 * for the session.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_kinstr_prfcnt_parse_setup(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	union kbase_ioctl_kinstr_prfcnt_setup *setup,
	struct kbase_kinstr_prfcnt_client_config *config)
{
	uint32_t i;
	struct prfcnt_request_item *req_arr;
	int err = 0;

	if (!setup->in.requests_ptr || (setup->in.request_item_count == 0) ||
	    (setup->in.request_item_size == 0)) {
		return -EINVAL;
	}

	req_arr =
		(struct prfcnt_request_item *)(uintptr_t)setup->in.requests_ptr;

	if (req_arr[setup->in.request_item_count - 1].hdr.item_type !=
	    FLEX_LIST_TYPE_NONE) {
		return -EINVAL;
	}

	if (req_arr[setup->in.request_item_count - 1].hdr.item_version != 0)
		return -EINVAL;

	/* The session configuration can only feature one value for some
	 * properties (like capture mode and block counter set), but the client
	 * may potential issue multiple requests and try to set more than one
	 * value for those properties. While issuing multiple requests for the
	 * same property is allowed by the protocol, asking for different values
	 * is illegal. Leaving these properties as undefined is illegal, too.
	 */
	config->prfcnt_mode = PRFCNT_MODE_RESERVED;
	config->counter_set = KBASE_HWCNT_SET_UNDEFINED;

	for (i = 0; i < setup->in.request_item_count - 1; i++) {
		if (req_arr[i].hdr.item_version > PRFCNT_READER_API_VERSION) {
			err = -EINVAL;
			break;
		}

		switch (req_arr[i].hdr.item_type) {
		/* Capture mode is initialized as undefined.
		 * The first request of this type sets the capture mode.
		 * The protocol allows the client to send redundant requests,
		 * but only if they replicate the same value that has already
		 * been set by the first request.
		 */
		case PRFCNT_REQUEST_TYPE_MODE:
			if (!prfcnt_mode_supported(req_arr[i].u.req_mode.mode))
				err = -EINVAL;
			else if (config->prfcnt_mode == PRFCNT_MODE_RESERVED)
				config->prfcnt_mode =
					req_arr[i].u.req_mode.mode;
			else if (req_arr[i].u.req_mode.mode !=
				 config->prfcnt_mode)
				err = -EINVAL;

			if (err < 0)
				break;

			if (config->prfcnt_mode == PRFCNT_MODE_PERIODIC) {
				config->period_us =
					req_arr[i]
						.u.req_mode.mode_config.periodic
						.period_us;

				if ((config->period_us != 0) &&
				    (config->period_us <
				     DUMP_INTERVAL_MIN_US)) {
					config->period_us =
						DUMP_INTERVAL_MIN_US;
				}
			}
			break;

		case PRFCNT_REQUEST_TYPE_ENABLE:
			err = kbasep_kinstr_prfcnt_parse_request_enable(
				&req_arr[i].u.req_enable, config);
			break;

		default:
			err = -EINVAL;
			break;
		}

		if (err < 0)
			break;
	}

	/* Verify that properties (like capture mode and block counter set)
	 * have been defined by the user space client.
	 */
	if (config->prfcnt_mode == PRFCNT_MODE_RESERVED)
		err = -EINVAL;

	if (config->counter_set == KBASE_HWCNT_SET_UNDEFINED)
		err = -EINVAL;

	return err;
}

/**
 * kbasep_kinstr_prfcnt_client_create() - Create a kinstr_prfcnt client.
 *                                        Does not attach to the kinstr_prfcnt
 *                                        context.
 * @kinstr_ctx: Non-NULL pointer to kinstr_prfcnt context.
 * @setup:      Non-NULL pointer to hardware counter ioctl setup structure.
 * @out_vcli:   Non-NULL pointer to where created client will be stored on
 *              success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_kinstr_prfcnt_client_create(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	union kbase_ioctl_kinstr_prfcnt_setup *setup,
	struct kbase_kinstr_prfcnt_client **out_vcli)
{
	int err;
	struct kbase_kinstr_prfcnt_client *cli;
	struct kbase_hwcnt_physical_enable_map phys_em;

	WARN_ON(!kinstr_ctx);
	WARN_ON(!setup);

	cli = kzalloc(sizeof(*cli), GFP_KERNEL);

	if (!cli)
		return -ENOMEM;

	cli->kinstr_ctx = kinstr_ctx;
	err = kbasep_kinstr_prfcnt_parse_setup(kinstr_ctx, setup, &cli->config);

	if (err < 0)
		goto error;

	cli->config.buffer_count = MAX_BUFFER_COUNT;
	cli->dump_interval_ns = cli->config.period_us * NSEC_PER_USEC;
	cli->next_dump_time_ns = 0;
	err = kbase_hwcnt_enable_map_alloc(kinstr_ctx->metadata,
					   &cli->enable_map);

	if (err < 0)
		goto error;

	phys_em.fe_bm = 0;
	phys_em.shader_bm = 0;
	phys_em.tiler_bm = 0;
	phys_em.mmu_l2_bm = 0;

	kbase_hwcnt_gpu_enable_map_from_physical(&cli->enable_map, &phys_em);

	cli->sample_count = cli->config.buffer_count;
	cli->sample_size = kbasep_kinstr_prfcnt_get_sample_size(
		kinstr_ctx->metadata, &cli->tmp_buf);

	/* Use virtualizer's metadata to alloc tmp buffer which interacts with
	 * the HWC virtualizer.
	 */
	err = kbase_hwcnt_dump_buffer_alloc(kinstr_ctx->metadata,
					    &cli->tmp_buf);

	if (err < 0)
		goto error;

	/* Enable all the available clk_enable_map. */
	cli->enable_map.clk_enable_map =
		(1ull << kinstr_ctx->metadata->clk_cnt) - 1;

	/* Use metadata from virtualizer to allocate dump buffers  if
	 * kinstr_prfcnt doesn't have the truncated metadata.
	 */
	err = kbasep_kinstr_prfcnt_sample_array_alloc(kinstr_ctx->metadata,
						      cli->config.buffer_count,
						      &cli->sample_arr);

	if (err < 0)
		goto error;

	err = -ENOMEM;

	cli->dump_bufs_meta =
		kmalloc_array(cli->config.buffer_count,
			      sizeof(*cli->dump_bufs_meta), GFP_KERNEL);

	if (!cli->dump_bufs_meta)
		goto error;

	err = kbase_hwcnt_virtualizer_client_create(
		kinstr_ctx->hvirt, &cli->enable_map, &cli->hvcli);

	if (err < 0)
		goto error;

	init_waitqueue_head(&cli->waitq);
	*out_vcli = cli;

	return 0;

error:
	kbasep_kinstr_prfcnt_client_destroy(cli);
	return err;
}

static size_t kbasep_kinstr_prfcnt_get_block_info_count(
	const struct kbase_hwcnt_metadata *metadata)
{
	size_t grp;
	size_t block_info_count = 0;

	if (!metadata)
		return 0;

	for (grp = 0; grp < kbase_hwcnt_metadata_group_count(metadata); grp++) {
		block_info_count +=
			kbase_hwcnt_metadata_block_count(metadata, grp);
	}

	return block_info_count;
}

static void kbasep_kinstr_prfcnt_get_request_info_list(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	struct prfcnt_enum_item *item_arr, size_t *arr_idx)
{
	memcpy(&item_arr[*arr_idx], kinstr_prfcnt_supported_requests,
	       sizeof(kinstr_prfcnt_supported_requests));
	*arr_idx += ARRAY_SIZE(kinstr_prfcnt_supported_requests);
}

static enum prfcnt_block_type
kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(u64 type)
{
	enum prfcnt_block_type block_type;

	switch (type) {
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE2:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE3:
		block_type = PRFCNT_BLOCK_TYPE_FE;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER:
		block_type = PRFCNT_BLOCK_TYPE_TILER;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3:
		block_type = PRFCNT_BLOCK_TYPE_SHADER_CORE;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2:
		block_type = PRFCNT_BLOCK_TYPE_MEMORY;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_UNDEFINED:
	default:
		block_type = PRFCNT_BLOCK_TYPE_RESERVED;
		break;
	}

	return block_type;
}

static int kbasep_kinstr_prfcnt_get_block_info_list(
	const struct kbase_hwcnt_metadata *metadata, size_t block_set,
	struct prfcnt_enum_item *item_arr, size_t *arr_idx)
{
	size_t grp;
	size_t blk;

	if (!metadata || !item_arr || !arr_idx)
		return -EINVAL;

	for (grp = 0; grp < kbase_hwcnt_metadata_group_count(metadata); grp++) {
		for (blk = 0;
		     blk < kbase_hwcnt_metadata_block_count(metadata, grp);
		     blk++, (*arr_idx)++) {
			item_arr[*arr_idx].hdr.item_type =
				PRFCNT_ENUM_TYPE_BLOCK;
			item_arr[*arr_idx].hdr.item_version =
				PRFCNT_READER_API_VERSION;
			item_arr[*arr_idx].u.block_counter.set = block_set;

			item_arr[*arr_idx].u.block_counter.block_type =
				kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(
					kbase_hwcnt_metadata_block_type(
						metadata, grp, blk));
			item_arr[*arr_idx].u.block_counter.num_instances =
				kbase_hwcnt_metadata_block_instance_count(
					metadata, grp, blk);
			item_arr[*arr_idx].u.block_counter.num_values =
				kbase_hwcnt_metadata_block_values_count(
					metadata, grp, blk);

			/* The bitmask of available counters should be dynamic.
			 * Temporarily, it is set to U64_MAX, waiting for the
			 * required functionality to be available in the future.
			 */
			item_arr[*arr_idx].u.block_counter.counter_mask[0] =
				U64_MAX;
			item_arr[*arr_idx].u.block_counter.counter_mask[1] =
				U64_MAX;
		}
	}

	return 0;
}

static int kbasep_kinstr_prfcnt_enum_info_count(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	struct kbase_ioctl_kinstr_prfcnt_enum_info *enum_info)
{
	int err = 0;
	uint32_t count = 0;
	size_t block_info_count = 0;
	const struct kbase_hwcnt_metadata *metadata;

	count = ARRAY_SIZE(kinstr_prfcnt_supported_requests);
	metadata = kbase_hwcnt_virtualizer_metadata(kinstr_ctx->hvirt);
	block_info_count = kbasep_kinstr_prfcnt_get_block_info_count(metadata);
	count += block_info_count;

	/* Reserve one for the last sentinel item. */
	count++;
	enum_info->info_item_count = count;
	enum_info->info_item_size = sizeof(struct prfcnt_enum_item);
	kinstr_ctx->info_item_count = count;

	return err;
}

static int kbasep_kinstr_prfcnt_enum_info_list(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	struct kbase_ioctl_kinstr_prfcnt_enum_info *enum_info)
{
	struct prfcnt_enum_item *prfcnt_item_arr;
	size_t arr_idx = 0;
	int err = 0;
	size_t block_info_count = 0;
	const struct kbase_hwcnt_metadata *metadata;

	if ((enum_info->info_item_size == 0) ||
	    (enum_info->info_item_count == 0) || !enum_info->info_list_ptr)
		return -EINVAL;

	if (enum_info->info_item_count != kinstr_ctx->info_item_count)
		return -EINVAL;

	prfcnt_item_arr =
		(struct prfcnt_enum_item *)(uintptr_t)enum_info->info_list_ptr;
	kbasep_kinstr_prfcnt_get_request_info_list(kinstr_ctx, prfcnt_item_arr,
						   &arr_idx);
	metadata = kbase_hwcnt_virtualizer_metadata(kinstr_ctx->hvirt);
	block_info_count = kbasep_kinstr_prfcnt_get_block_info_count(metadata);

	if (arr_idx + block_info_count >= enum_info->info_item_count)
		err = -EINVAL;

	if (!err) {
		size_t counter_set;

#if defined(CONFIG_MALI_BIFROST_PRFCNT_SET_SECONDARY)
		counter_set = KBASE_HWCNT_SET_SECONDARY;
#elif defined(CONFIG_MALI_PRFCNT_SET_TERTIARY)
		counter_set = KBASE_HWCNT_SET_TERTIARY;
#else
		/* Default to primary */
		counter_set = KBASE_HWCNT_SET_PRIMARY;
#endif
		kbasep_kinstr_prfcnt_get_block_info_list(
			metadata, counter_set, prfcnt_item_arr, &arr_idx);
		if (arr_idx != enum_info->info_item_count - 1)
			err = -EINVAL;
	}

	/* The last sentinel item. */
	prfcnt_item_arr[enum_info->info_item_count - 1].hdr.item_type =
		FLEX_LIST_TYPE_NONE;
	prfcnt_item_arr[enum_info->info_item_count - 1].hdr.item_version = 0;

	return err;
}

int kbase_kinstr_prfcnt_enum_info(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	struct kbase_ioctl_kinstr_prfcnt_enum_info *enum_info)
{
	int err;

	if (!kinstr_ctx || !enum_info)
		return -EINVAL;

	if (!enum_info->info_list_ptr)
		err = kbasep_kinstr_prfcnt_enum_info_count(kinstr_ctx,
							   enum_info);
	else
		err = kbasep_kinstr_prfcnt_enum_info_list(kinstr_ctx,
							  enum_info);

	return err;
}

int kbase_kinstr_prfcnt_setup(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
			      union kbase_ioctl_kinstr_prfcnt_setup *setup)
{
	int err;
	struct kbase_kinstr_prfcnt_client *cli = NULL;

	if (!kinstr_ctx || !setup)
		return -EINVAL;

	err = kbasep_kinstr_prfcnt_client_create(kinstr_ctx, setup, &cli);

	if (err < 0)
		goto error;

	mutex_lock(&kinstr_ctx->lock);
	kinstr_ctx->client_count++;
	list_add(&cli->node, &kinstr_ctx->clients);
	mutex_unlock(&kinstr_ctx->lock);

	setup->out.prfcnt_metadata_item_size = sizeof(struct prfcnt_metadata);
	setup->out.prfcnt_mmap_size_bytes =
		cli->sample_size * cli->sample_count;

	/* Expose to user-space only once the client is fully initialized */
	err = anon_inode_getfd("[mali_kinstr_prfcnt_desc]",
			       &kinstr_prfcnt_client_fops, cli,
			       O_RDONLY | O_CLOEXEC);

	if (err < 0)
		goto client_installed_error;

	return err;

client_installed_error:
	mutex_lock(&kinstr_ctx->lock);
	kinstr_ctx->client_count--;
	list_del(&cli->node);
	mutex_unlock(&kinstr_ctx->lock);
error:
	kbasep_kinstr_prfcnt_client_destroy(cli);
	return err;
}
