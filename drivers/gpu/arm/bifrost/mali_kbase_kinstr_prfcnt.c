// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
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
#include "mali_kbase_kinstr_prfcnt.h"
#include "hwcnt/mali_kbase_hwcnt_virtualizer.h"
#include "hwcnt/mali_kbase_hwcnt_gpu.h"
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
#include <linux/overflow.h>
#include <linux/version_compat_defs.h>
#include <linux/workqueue.h>

/* Explicitly include epoll header for old kernels. Not required from 4.16. */
#if KERNEL_VERSION(4, 16, 0) > LINUX_VERSION_CODE
#include <uapi/linux/eventpoll.h>
#endif

/* The minimum allowed interval between dumps, in nanoseconds
 * (equivalent to 10KHz)
 */
#define DUMP_INTERVAL_MIN_NS (100 * NSEC_PER_USEC)

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
 * @sample_meta: Pointer to sample metadata.
 * @dump_buf:    Dump buffer containing sample data.
 */
struct kbase_kinstr_prfcnt_sample {
	struct prfcnt_metadata *sample_meta;
	struct kbase_hwcnt_dump_buffer dump_buf;
};

/**
 * struct kbase_kinstr_prfcnt_sample_array - Array of sample data.
 * @user_buf:     Address of allocated userspace buffer. A single allocation is used
 *                for all Dump Buffers in the array.
 * @sample_count: Number of allocated samples.
 * @samples:      Non-NULL pointer to the array of Dump Buffers.
 */
struct kbase_kinstr_prfcnt_sample_array {
	u8 *user_buf;
	size_t sample_count;
	struct kbase_kinstr_prfcnt_sample *samples;
};

/**
 * struct kbase_kinstr_prfcnt_client_config - Client session configuration.
 * @prfcnt_mode:  Sampling mode: either manual or periodic.
 * @counter_set:  Set of performance counter blocks.
 * @scope:        Scope of performance counters to capture.
 * @buffer_count: Number of buffers used to store samples.
 * @period_ns:    Sampling period, in nanoseconds, or 0 if manual mode.
 * @phys_em:      Enable map used by the GPU.
 */
struct kbase_kinstr_prfcnt_client_config {
	u8 prfcnt_mode;
	u8 counter_set;
	u8 scope;
	u16 buffer_count;
	u64 period_ns;
	struct kbase_hwcnt_physical_enable_map phys_em;
};

/**
 * enum kbase_kinstr_prfcnt_client_init_state - A list of
 *                                              initialisation states that the
 *                                              kinstr_prfcnt client can be at
 *                                              during initialisation. Useful
 *                                              for terminating a partially
 *                                              initialised client.
 *
 * @KINSTR_PRFCNT_UNINITIALISED : Client is uninitialised
 * @KINSTR_PRFCNT_PARSE_SETUP : Parse the setup session
 * @KINSTR_PRFCNT_ENABLE_MAP : Allocate memory for enable map
 * @KINSTR_PRFCNT_DUMP_BUFFER : Allocate memory for dump buffer
 * @KINSTR_PRFCNT_SAMPLE_ARRAY : Allocate memory for and initialise sample array
 * @KINSTR_PRFCNT_VIRTUALIZER_CLIENT : Create virtualizer client
 * @KINSTR_PRFCNT_WAITQ_MUTEX : Create and initialise mutex and waitqueue
 * @KINSTR_PRFCNT_INITIALISED : Client is fully initialised
 */
enum kbase_kinstr_prfcnt_client_init_state {
	KINSTR_PRFCNT_UNINITIALISED,
	KINSTR_PRFCNT_PARSE_SETUP = KINSTR_PRFCNT_UNINITIALISED,
	KINSTR_PRFCNT_ENABLE_MAP,
	KINSTR_PRFCNT_DUMP_BUFFER,
	KINSTR_PRFCNT_SAMPLE_ARRAY,
	KINSTR_PRFCNT_VIRTUALIZER_CLIENT,
	KINSTR_PRFCNT_WAITQ_MUTEX,
	KINSTR_PRFCNT_INITIALISED
};

/**
 * struct kbase_kinstr_prfcnt_client - A kinstr_prfcnt client attached
 *                                     to a kinstr_prfcnt context.
 * @kinstr_ctx:           kinstr_prfcnt context client is attached to.
 * @hvcli:                Hardware counter virtualizer client.
 * @node:                 Node used to attach this client to list in
 *                        kinstr_prfcnt context.
 * @cmd_sync_lock:        Lock coordinating the reader interface for commands.
 * @next_dump_time_ns:    Time in ns when this client's next periodic dump must
 *                        occur. If 0, not a periodic client.
 * @dump_interval_ns:     Interval between periodic dumps. If 0, not a periodic
 *                        client.
 * @sample_flags:         Flags for the current active dumping sample, marking
 *                        the conditions/events during the dump duration.
 * @active:               True if the client has been started.
 * @config:               Configuration of the client session.
 * @enable_map:           Counters enable map.
 * @tmp_buf:              Temporary buffer to use before handing over dump to
 *                        client.
 * @sample_arr:           Array of dump buffers allocated by this client.
 * @read_idx:             Index of buffer read by userspace.
 * @write_idx:            Index of buffer being written by dump worker.
 * @fetch_idx:            Index of buffer being fetched by userspace, but
 *                        pending a confirmation of being read (consumed) if it
 *                        differs from the read_idx.
 * @waitq:                Client's notification queue.
 * @sample_size:          Size of the data required for one sample, in bytes.
 * @sample_count:         Number of samples the client is able to capture.
 * @user_data:            User data associated with the session.
 *                        This is set when the session is started and stopped.
 *                        This value is ignored for control commands that
 *                        provide another value.
 */
struct kbase_kinstr_prfcnt_client {
	struct kbase_kinstr_prfcnt_context *kinstr_ctx;
	struct kbase_hwcnt_virtualizer_client *hvcli;
	struct list_head node;
	struct mutex cmd_sync_lock;
	u64 next_dump_time_ns;
	u32 dump_interval_ns;
	u32 sample_flags;
	bool active;
	struct kbase_kinstr_prfcnt_client_config config;
	struct kbase_hwcnt_enable_map enable_map;
	struct kbase_hwcnt_dump_buffer tmp_buf;
	struct kbase_kinstr_prfcnt_sample_array sample_arr;
	atomic_t read_idx;
	atomic_t write_idx;
	atomic_t fetch_idx;
	wait_queue_head_t waitq;
	size_t sample_size;
	size_t sample_count;
	u64 user_data;
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
 * kbasep_kinstr_prfcnt_hwcnt_reader_poll() - hwcnt reader's poll.
 * @filp: Non-NULL pointer to file structure.
 * @wait: Non-NULL pointer to poll table.
 *
 * Return: EPOLLIN | EPOLLRDNORM if data can be read without blocking, 0 if
 *         data can not be read without blocking, else EPOLLHUP | EPOLLERR.
 */
static __poll_t
kbasep_kinstr_prfcnt_hwcnt_reader_poll(struct file *filp,
				       struct poll_table_struct *wait)
{
	struct kbase_kinstr_prfcnt_client *cli;

	if (!filp || !wait)
		return EPOLLHUP | EPOLLERR;

	cli = filp->private_data;

	if (!cli)
		return EPOLLHUP | EPOLLERR;

	poll_wait(filp, &cli->waitq, wait);

	if (atomic_read(&cli->write_idx) != atomic_read(&cli->fetch_idx))
		return EPOLLIN | EPOLLRDNORM;

	return (__poll_t)0;
}

/**
 * kbasep_kinstr_prfcnt_next_dump_time_ns() - Calculate the next periodic
 *                                            dump time.
 * @cur_ts_ns: Current time in nanoseconds.
 * @interval:  Interval between dumps in nanoseconds.
 *
 * Return: 0 if interval is 0 (i.e. a non-periodic client), or the next dump
 *         time that occurs after cur_ts_ns.
 */
static u64 kbasep_kinstr_prfcnt_next_dump_time_ns(u64 cur_ts_ns, u32 interval)
{
	/* Non-periodic client */
	if (interval == 0)
		return 0;

	/*
	 * Return the next interval after the current time relative to t=0.
	 * This means multiple clients with the same period will synchronize,
	 * regardless of when they were started, allowing the worker to be
	 * scheduled less frequently.
	 */
	do_div(cur_ts_ns, interval);

	return (cur_ts_ns + 1) * interval;
}

/**
 * kbasep_kinstr_prfcnt_timestamp_ns() - Get the current time in nanoseconds.
 *
 * Return: Current time in nanoseconds.
 */
static u64 kbasep_kinstr_prfcnt_timestamp_ns(void)
{
	return ktime_get_raw_ns();
}

/**
 * kbasep_kinstr_prfcnt_reschedule_worker() - Update next dump times for all
 *                                            periodic kinstr_prfcnt clients,
 *                                            then reschedule the dump worker
 *                                            appropriately.
 * @kinstr_ctx: Non-NULL pointer to the kinstr_prfcnt context.
 *
 * If there are no periodic clients, then the dump worker will not be
 * rescheduled. Else, the dump worker will be rescheduled for the next
 * periodic client dump.
 */
static void kbasep_kinstr_prfcnt_reschedule_worker(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx)
{
	u64 cur_ts_ns;
	u64 shortest_period_ns = U64_MAX;
	struct kbase_kinstr_prfcnt_client *pos;

	WARN_ON(!kinstr_ctx);
	lockdep_assert_held(&kinstr_ctx->lock);
	cur_ts_ns = kbasep_kinstr_prfcnt_timestamp_ns();

	/*
	 * This loop fulfills 2 separate tasks that don't affect each other:
	 *
	 * 1) Determine the shortest period.
	 * 2) Update the next dump time of clients that have already been
	 *    dumped. It's important not to alter the next dump time of clients
	 *    that haven't been dumped yet.
	 *
	 * For the sake of efficiency, the rescheduling decision ignores the time
	 * of the next dump and just uses the shortest period among all periodic
	 * clients. It is more efficient to serve multiple dump requests at once,
	 * rather than trying to reschedule the worker to serve each request
	 * individually.
	 */
	list_for_each_entry(pos, &kinstr_ctx->clients, node) {
		/* Ignore clients that are not periodic or not active. */
		if (pos->active && pos->dump_interval_ns > 0) {
			shortest_period_ns =
				MIN(shortest_period_ns, pos->dump_interval_ns);

			/* Next dump should happen exactly one period after the last dump.
			 * If last dump was overdue and scheduled to happen more than one
			 * period ago, compensate for that by scheduling next dump in the
			 * immediate future.
			 */
			if (pos->next_dump_time_ns < cur_ts_ns)
				pos->next_dump_time_ns =
					MAX(cur_ts_ns + 1,
					    pos->next_dump_time_ns +
						    pos->dump_interval_ns);
		}
	}

	/* Cancel the timer if it is already pending */
	hrtimer_cancel(&kinstr_ctx->dump_timer);

	/* Start the timer if there are periodic clients and kinstr_prfcnt is not
	 * suspended.
	 */
	if ((shortest_period_ns != U64_MAX) &&
	    (kinstr_ctx->suspend_count == 0)) {
		u64 next_schedule_time_ns =
			kbasep_kinstr_prfcnt_next_dump_time_ns(
				cur_ts_ns, shortest_period_ns);
		hrtimer_start(&kinstr_ctx->dump_timer,
			      ns_to_ktime(next_schedule_time_ns - cur_ts_ns),
			      HRTIMER_MODE_REL);
	}
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

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE_UNDEFINED:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC_UNDEFINED:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER_UNDEFINED:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS_UNDEFINED:
	default:
		block_type = PRFCNT_BLOCK_TYPE_RESERVED;
		break;
	}

	return block_type;
}

static bool kbase_kinstr_is_block_type_reserved(const struct kbase_hwcnt_metadata *metadata,
						size_t grp, size_t blk)
{
	enum prfcnt_block_type block_type = kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(
		kbase_hwcnt_metadata_block_type(metadata, grp, blk));

	return block_type == PRFCNT_BLOCK_TYPE_RESERVED;
}

/**
 * kbasep_kinstr_prfcnt_set_block_meta_items() - Populate a sample's block meta
 *                                               item array.
 * @enable_map:      Non-NULL pointer to the map of enabled counters.
 * @dst:             Non-NULL pointer to the sample's dump buffer object.
 * @block_meta_base: Non-NULL double pointer to the start of the block meta
 *                   data items.
 * @base_addr:       Address of allocated pages for array of samples. Used
 *                   to calculate offset of block values.
 * @counter_set:     The SET which blocks represent.
 *
 * Return: 0 on success, else error code.
 */
int kbasep_kinstr_prfcnt_set_block_meta_items(struct kbase_hwcnt_enable_map *enable_map,
					      struct kbase_hwcnt_dump_buffer *dst,
					      struct prfcnt_metadata **block_meta_base,
					      u8 *base_addr, u8 counter_set)
{
	size_t grp, blk, blk_inst;
	struct prfcnt_metadata **ptr_md = block_meta_base;
	const struct kbase_hwcnt_metadata *metadata;
	uint8_t block_idx = 0;

	if (!dst || !*block_meta_base)
		return -EINVAL;

	metadata = dst->metadata;
	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		u8 *dst_blk;

		/* Block indices must be reported with no gaps. */
		if (blk_inst == 0)
			block_idx = 0;

		/* Skip unavailable or non-enabled blocks */
		if (kbase_kinstr_is_block_type_reserved(metadata, grp, blk) ||
		    !kbase_hwcnt_metadata_block_instance_avail(metadata, grp, blk, blk_inst) ||
		    !kbase_hwcnt_enable_map_block_enabled(enable_map, grp, blk, blk_inst))
			continue;

		dst_blk = (u8 *)kbase_hwcnt_dump_buffer_block_instance(dst, grp, blk, blk_inst);
		(*ptr_md)->hdr.item_type = PRFCNT_SAMPLE_META_TYPE_BLOCK;
		(*ptr_md)->hdr.item_version = PRFCNT_READER_API_VERSION;
		(*ptr_md)->u.block_md.block_type =
			kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(
				kbase_hwcnt_metadata_block_type(metadata, grp,
								blk));
		(*ptr_md)->u.block_md.block_idx = block_idx;
		(*ptr_md)->u.block_md.set = counter_set;
		(*ptr_md)->u.block_md.block_state = BLOCK_STATE_UNKNOWN;
		(*ptr_md)->u.block_md.values_offset = (u32)(dst_blk - base_addr);

		/* update the buf meta data block pointer to next item */
		(*ptr_md)++;
		block_idx++;
	}

	return 0;
}

/**
 * kbasep_kinstr_prfcnt_set_sample_metadata() - Set sample metadata for sample
 *                                              output.
 * @cli:       Non-NULL pointer to a kinstr_prfcnt client.
 * @dump_buf:  Non-NULL pointer to dump buffer where sample is stored.
 * @ptr_md:    Non-NULL pointer to sample metadata.
 */
static void kbasep_kinstr_prfcnt_set_sample_metadata(
	struct kbase_kinstr_prfcnt_client *cli,
	struct kbase_hwcnt_dump_buffer *dump_buf,
	struct prfcnt_metadata *ptr_md)
{
	u8 clk_cnt, i;

	clk_cnt = cli->kinstr_ctx->metadata->clk_cnt;

	/* PRFCNT_SAMPLE_META_TYPE_SAMPLE must be the first item */
	ptr_md->hdr.item_type = PRFCNT_SAMPLE_META_TYPE_SAMPLE;
	ptr_md->hdr.item_version = PRFCNT_READER_API_VERSION;
	ptr_md->u.sample_md.seq = atomic_read(&cli->write_idx);
	ptr_md->u.sample_md.flags = cli->sample_flags;

	/* Place the PRFCNT_SAMPLE_META_TYPE_CLOCK optionally as the 2nd */
	ptr_md++;
	if (clk_cnt > MAX_REPORTED_DOMAINS)
		clk_cnt = MAX_REPORTED_DOMAINS;

	/* Handle the prfcnt_clock_metadata meta item */
	ptr_md->hdr.item_type = PRFCNT_SAMPLE_META_TYPE_CLOCK;
	ptr_md->hdr.item_version = PRFCNT_READER_API_VERSION;
	ptr_md->u.clock_md.num_domains = clk_cnt;
	for (i = 0; i < clk_cnt; i++)
		ptr_md->u.clock_md.cycles[i] = dump_buf->clk_cnt_buf[i];

	/* Dealing with counter blocks */
	ptr_md++;
	if (WARN_ON(kbasep_kinstr_prfcnt_set_block_meta_items(&cli->enable_map, dump_buf, &ptr_md,
							      cli->sample_arr.user_buf,
							      cli->config.counter_set)))
		return;

	/* Handle the last sentinel item */
	ptr_md->hdr.item_type = FLEX_LIST_TYPE_NONE;
	ptr_md->hdr.item_version = 0;
}

/**
 * kbasep_kinstr_prfcnt_client_output_sample() - Assemble a sample for output.
 * @cli:          Non-NULL pointer to a kinstr_prfcnt client.
 * @buf_idx:      The index to the sample array for saving the sample.
 * @user_data:    User data to return to the user.
 * @ts_start_ns:  Time stamp for the start point of the sample dump.
 * @ts_end_ns:    Time stamp for the end point of the sample dump.
 */
static void kbasep_kinstr_prfcnt_client_output_sample(
	struct kbase_kinstr_prfcnt_client *cli, unsigned int buf_idx,
	u64 user_data, u64 ts_start_ns, u64 ts_end_ns)
{
	struct kbase_hwcnt_dump_buffer *dump_buf;
	struct kbase_hwcnt_dump_buffer *tmp_buf = &cli->tmp_buf;
	struct prfcnt_metadata *ptr_md;

	if (WARN_ON(buf_idx >= cli->sample_arr.sample_count))
		return;

	dump_buf = &cli->sample_arr.samples[buf_idx].dump_buf;
	ptr_md = cli->sample_arr.samples[buf_idx].sample_meta;

	/* Patch the dump buf headers, to hide the counters that other hwcnt
	 * clients are using.
	 */
	kbase_hwcnt_gpu_patch_dump_headers(tmp_buf, &cli->enable_map);

	/* Copy the temp buffer to the userspace visible buffer. The strict
	 * variant will explicitly zero any non-enabled counters to ensure
	 * nothing except exactly what the user asked for is made visible.
	 */
	kbase_hwcnt_dump_buffer_copy_strict(dump_buf, tmp_buf,
					    &cli->enable_map);

	/* PRFCNT_SAMPLE_META_TYPE_SAMPLE must be the first item.
	 * Set timestamp and user data for real dump.
	 */
	ptr_md->u.sample_md.timestamp_start = ts_start_ns;
	ptr_md->u.sample_md.timestamp_end = ts_end_ns;
	ptr_md->u.sample_md.user_data = user_data;

	kbasep_kinstr_prfcnt_set_sample_metadata(cli, dump_buf, ptr_md);
}

/**
 * kbasep_kinstr_prfcnt_client_dump() - Perform a dump for a client.
 * @cli:          Non-NULL pointer to a kinstr_prfcnt client.
 * @event_id:     Event type that triggered the dump.
 * @user_data:    User data to return to the user.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_kinstr_prfcnt_client_dump(struct kbase_kinstr_prfcnt_client *cli,
					    enum base_hwcnt_reader_event event_id, u64 user_data)
{
	int ret;
	u64 ts_start_ns = 0;
	u64 ts_end_ns = 0;
	unsigned int write_idx;
	unsigned int read_idx;
	size_t available_samples_count;

	WARN_ON(!cli);
	lockdep_assert_held(&cli->kinstr_ctx->lock);

	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	/* Check if there is a place to copy HWC block into. Calculate the
	 * number of available samples count, by taking into account the type
	 * of dump.
	 */
	available_samples_count = cli->sample_arr.sample_count;
	WARN_ON(available_samples_count < 1);
	/* Reserve one slot to store the implicit sample taken on CMD_STOP */
	available_samples_count -= 1;
	if (write_idx - read_idx == available_samples_count) {
		/* For periodic sampling, the current active dump
		 * will be accumulated in the next sample, when
		 * a buffer becomes available.
		 */
		if (event_id == BASE_HWCNT_READER_EVENT_PERIODIC)
			cli->sample_flags |= SAMPLE_FLAG_OVERFLOW;
		return -EBUSY;
	}

	/* For the rest of the function, use the actual sample_count
	 * that represents the real size of the array.
	 */
	write_idx %= cli->sample_arr.sample_count;

	ret = kbase_hwcnt_virtualizer_client_dump(cli->hvcli, &ts_start_ns, &ts_end_ns,
						  &cli->tmp_buf);
	/* HWC dump error, set the sample with error flag */
	if (ret)
		cli->sample_flags |= SAMPLE_FLAG_ERROR;

	/* Make the sample ready and copy it to the userspace mapped buffer */
	kbasep_kinstr_prfcnt_client_output_sample(cli, write_idx, user_data, ts_start_ns,
						  ts_end_ns);

	/* Notify client. Make sure all changes to memory are visible. */
	wmb();
	atomic_inc(&cli->write_idx);
	wake_up_interruptible(&cli->waitq);
	/* Reset the flags for the next sample dump */
	cli->sample_flags = 0;

	return 0;
}

static int
kbasep_kinstr_prfcnt_client_start(struct kbase_kinstr_prfcnt_client *cli,
				  u64 user_data)
{
	int ret;
	u64 tm_start, tm_end;
	unsigned int write_idx;
	unsigned int read_idx;
	size_t available_samples_count;

	WARN_ON(!cli);
	lockdep_assert_held(&cli->cmd_sync_lock);

	/* If the client is already started, the command is a no-op */
	if (cli->active)
		return 0;

	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	/* Check whether there is space to store atleast an implicit sample
	 * corresponding to CMD_STOP.
	 */
	available_samples_count = cli->sample_count - (write_idx - read_idx);
	if (!available_samples_count)
		return -EBUSY;

	kbase_hwcnt_gpu_enable_map_from_physical(&cli->enable_map,
						 &cli->config.phys_em);

	/* Enable all the available clk_enable_map. */
	cli->enable_map.clk_enable_map = (1ull << cli->kinstr_ctx->metadata->clk_cnt) - 1;

	mutex_lock(&cli->kinstr_ctx->lock);
	/* Enable HWC from the configuration of the client creation */
	ret = kbase_hwcnt_virtualizer_client_set_counters(
		cli->hvcli, &cli->enable_map, &tm_start, &tm_end, NULL);

	if (!ret) {
		cli->active = true;
		cli->user_data = user_data;
		cli->sample_flags = 0;

		if (cli->dump_interval_ns)
			kbasep_kinstr_prfcnt_reschedule_worker(cli->kinstr_ctx);
	}

	mutex_unlock(&cli->kinstr_ctx->lock);

	return ret;
}

static int
kbasep_kinstr_prfcnt_client_stop(struct kbase_kinstr_prfcnt_client *cli,
				 u64 user_data)
{
	int ret;
	u64 tm_start = 0;
	u64 tm_end = 0;
	struct kbase_hwcnt_physical_enable_map phys_em;
	size_t available_samples_count;
	unsigned int write_idx;
	unsigned int read_idx;

	WARN_ON(!cli);
	lockdep_assert_held(&cli->cmd_sync_lock);

	/* If the client is not started, the command is invalid */
	if (!cli->active)
		return -EINVAL;

	mutex_lock(&cli->kinstr_ctx->lock);

	/* Disable counters under the lock, so we do not race with the
	 * sampling thread.
	 */
	phys_em.fe_bm = 0;
	phys_em.tiler_bm = 0;
	phys_em.mmu_l2_bm = 0;
	phys_em.shader_bm = 0;

	kbase_hwcnt_gpu_enable_map_from_physical(&cli->enable_map, &phys_em);

	/* Check whether one has the buffer to hold the last sample */
	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	available_samples_count = cli->sample_count - (write_idx - read_idx);

	ret = kbase_hwcnt_virtualizer_client_set_counters(cli->hvcli,
							  &cli->enable_map,
							  &tm_start, &tm_end,
							  &cli->tmp_buf);
	/* If the last stop sample is in error, set the sample flag */
	if (ret)
		cli->sample_flags |= SAMPLE_FLAG_ERROR;

	/* There must be a place to save the last stop produced sample */
	if (!WARN_ON(!available_samples_count)) {
		write_idx %= cli->sample_arr.sample_count;
		/* Handle the last stop sample */
		kbase_hwcnt_gpu_enable_map_from_physical(&cli->enable_map,
							 &cli->config.phys_em);
		/* As this is a stop sample, mark it as MANUAL */
		kbasep_kinstr_prfcnt_client_output_sample(
			cli, write_idx, user_data, tm_start, tm_end);
		/* Notify client. Make sure all changes to memory are visible. */
		wmb();
		atomic_inc(&cli->write_idx);
		wake_up_interruptible(&cli->waitq);
	}

	cli->active = false;
	cli->user_data = user_data;

	if (cli->dump_interval_ns)
		kbasep_kinstr_prfcnt_reschedule_worker(cli->kinstr_ctx);

	mutex_unlock(&cli->kinstr_ctx->lock);

	return 0;
}

static int
kbasep_kinstr_prfcnt_client_sync_dump(struct kbase_kinstr_prfcnt_client *cli,
				      u64 user_data)
{
	int ret;

	lockdep_assert_held(&cli->cmd_sync_lock);

	/* If the client is not started, or not manual, the command invalid */
	if (!cli->active || cli->dump_interval_ns)
		return -EINVAL;

	mutex_lock(&cli->kinstr_ctx->lock);

	ret = kbasep_kinstr_prfcnt_client_dump(cli, BASE_HWCNT_READER_EVENT_MANUAL, user_data);

	mutex_unlock(&cli->kinstr_ctx->lock);

	return ret;
}

static int
kbasep_kinstr_prfcnt_client_discard(struct kbase_kinstr_prfcnt_client *cli)
{
	unsigned int write_idx;

	WARN_ON(!cli);
	lockdep_assert_held(&cli->cmd_sync_lock);

	mutex_lock(&cli->kinstr_ctx->lock);

	write_idx = atomic_read(&cli->write_idx);

	/* Discard (clear) all internally buffered samples. Note, if there
	 * is a fetched sample in flight, one should not touch the read index,
	 * leaving it alone for the put-sample operation to update it. The
	 * consistency between the read_idx and the fetch_idx is coordinated by
	 * holding the cli->cmd_sync_lock.
	 */
	if (atomic_read(&cli->fetch_idx) != atomic_read(&cli->read_idx)) {
		atomic_set(&cli->fetch_idx, write_idx);
	} else {
		atomic_set(&cli->fetch_idx, write_idx);
		atomic_set(&cli->read_idx, write_idx);
	}

	mutex_unlock(&cli->kinstr_ctx->lock);

	return 0;
}

int kbasep_kinstr_prfcnt_cmd(struct kbase_kinstr_prfcnt_client *cli,
			     struct prfcnt_control_cmd *control_cmd)
{
	int ret = 0;

	mutex_lock(&cli->cmd_sync_lock);

	switch (control_cmd->cmd) {
	case PRFCNT_CONTROL_CMD_START:
		ret = kbasep_kinstr_prfcnt_client_start(cli,
							control_cmd->user_data);
		break;
	case PRFCNT_CONTROL_CMD_STOP:
		ret = kbasep_kinstr_prfcnt_client_stop(cli,
						       control_cmd->user_data);
		break;
	case PRFCNT_CONTROL_CMD_SAMPLE_SYNC:
		ret = kbasep_kinstr_prfcnt_client_sync_dump(
			cli, control_cmd->user_data);
		break;
	case PRFCNT_CONTROL_CMD_DISCARD:
		ret = kbasep_kinstr_prfcnt_client_discard(cli);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&cli->cmd_sync_lock);

	return ret;
}

static int
kbasep_kinstr_prfcnt_get_sample(struct kbase_kinstr_prfcnt_client *cli,
				struct prfcnt_sample_access *sample_access)
{
	unsigned int write_idx;
	unsigned int read_idx;
	unsigned int fetch_idx;
	u64 sample_offset_bytes;
	struct prfcnt_metadata *sample_meta;
	int err = 0;

	mutex_lock(&cli->cmd_sync_lock);
	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	if (write_idx == read_idx) {
		err = -EINVAL;
		goto error_out;
	}

	/* If the client interface has already had a sample been fetched,
	 * reflected by the fetch index not equal to read_idx, i.e., typically
	 *   read_idx + 1 == fetch_idx,
	 * further fetch is not allowed until the previously fetched buffer
	 * is put back (which brings the read_idx == fetch_idx). As a design,
	 * the above add one equal condition (i.e. typical cases) may only be
	 * untrue if there had been an interface operation on sample discard,
	 * after the sample in question already been fetched, in which case,
	 * the fetch_idx could have a delta larger than 1 relative to the
	 * read_idx.
	 */
	fetch_idx = atomic_read(&cli->fetch_idx);
	if (read_idx != fetch_idx) {
		err = -EBUSY;
		goto error_out;
	}

	read_idx %= cli->sample_arr.sample_count;
	sample_meta = cli->sample_arr.samples[read_idx].sample_meta;
	sample_offset_bytes = (u8 *)sample_meta - cli->sample_arr.user_buf;

	sample_access->sequence = sample_meta->u.sample_md.seq;
	sample_access->sample_offset_bytes = sample_offset_bytes;

	/* Marking a sample has been fetched by advancing the fetch index */
	atomic_inc(&cli->fetch_idx);

error_out:
	mutex_unlock(&cli->cmd_sync_lock);
	return err;
}

static int
kbasep_kinstr_prfcnt_put_sample(struct kbase_kinstr_prfcnt_client *cli,
				struct prfcnt_sample_access *sample_access)
{
	unsigned int write_idx;
	unsigned int read_idx;
	unsigned int fetch_idx;
	u64 sample_offset_bytes;
	int err = 0;

	mutex_lock(&cli->cmd_sync_lock);
	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	if (write_idx == read_idx || sample_access->sequence != read_idx) {
		err = -EINVAL;
		goto error_out;
	}

	read_idx %= cli->sample_arr.sample_count;
	sample_offset_bytes =
		(u8 *)cli->sample_arr.samples[read_idx].sample_meta - cli->sample_arr.user_buf;

	if (sample_access->sample_offset_bytes != sample_offset_bytes) {
		err = -EINVAL;
		goto error_out;
	}

	fetch_idx = atomic_read(&cli->fetch_idx);
	WARN_ON(read_idx == fetch_idx);
	/* Setting the read_idx matching the fetch_idx, signals no in-flight
	 * fetched sample.
	 */
	atomic_set(&cli->read_idx, fetch_idx);

error_out:
	mutex_unlock(&cli->cmd_sync_lock);
	return err;
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
	long rcode = 0;
	struct kbase_kinstr_prfcnt_client *cli;
	void __user *uarg = (void __user *)arg;

	if (!filp)
		return -EINVAL;

	cli = filp->private_data;

	if (!cli)
		return -EINVAL;

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(KBASE_IOCTL_KINSTR_PRFCNT_CMD): {
		struct prfcnt_control_cmd control_cmd;
		int err;

		err = copy_from_user(&control_cmd, uarg, sizeof(control_cmd));
		if (err)
			return -EFAULT;
		rcode = kbasep_kinstr_prfcnt_cmd(cli, &control_cmd);
	} break;
	case _IOC_NR(KBASE_IOCTL_KINSTR_PRFCNT_GET_SAMPLE): {
		struct prfcnt_sample_access sample_access;
		int err;

		memset(&sample_access, 0, sizeof(sample_access));
		rcode = kbasep_kinstr_prfcnt_get_sample(cli, &sample_access);
		err = copy_to_user(uarg, &sample_access, sizeof(sample_access));
		if (err)
			return -EFAULT;
	} break;
	case _IOC_NR(KBASE_IOCTL_KINSTR_PRFCNT_PUT_SAMPLE): {
		struct prfcnt_sample_access sample_access;
		int err;

		err = copy_from_user(&sample_access, uarg,
				     sizeof(sample_access));
		if (err)
			return -EFAULT;
		rcode = kbasep_kinstr_prfcnt_put_sample(cli, &sample_access);
	} break;
	default:
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

	if (!filp || !vma)
		return -EINVAL;

	cli = filp->private_data;
	if (!cli)
		return -EINVAL;

	return remap_vmalloc_range(vma, cli->sample_arr.user_buf, 0);
}

static void kbasep_kinstr_prfcnt_sample_array_free(
	struct kbase_kinstr_prfcnt_sample_array *sample_arr)
{
	if (!sample_arr)
		return;

	kfree(sample_arr->samples);
	vfree(sample_arr->user_buf);
	memset(sample_arr, 0, sizeof(*sample_arr));
}

static void
kbasep_kinstr_prfcnt_client_destroy_partial(struct kbase_kinstr_prfcnt_client *cli,
					    enum kbase_kinstr_prfcnt_client_init_state init_state)
{
	if (!cli)
		return;

	while (init_state-- > KINSTR_PRFCNT_UNINITIALISED) {
		switch (init_state) {
		case KINSTR_PRFCNT_INITIALISED:
			/* This shouldn't be reached */
			break;
		case KINSTR_PRFCNT_WAITQ_MUTEX:
			mutex_destroy(&cli->cmd_sync_lock);
			break;
		case KINSTR_PRFCNT_VIRTUALIZER_CLIENT:
			kbase_hwcnt_virtualizer_client_destroy(cli->hvcli);
			break;
		case KINSTR_PRFCNT_SAMPLE_ARRAY:
			kbasep_kinstr_prfcnt_sample_array_free(&cli->sample_arr);
			break;
		case KINSTR_PRFCNT_DUMP_BUFFER:
			kbase_hwcnt_dump_buffer_free(&cli->tmp_buf);
			break;
		case KINSTR_PRFCNT_ENABLE_MAP:
			kbase_hwcnt_enable_map_free(&cli->enable_map);
			break;
		case KINSTR_PRFCNT_PARSE_SETUP:
			/* Nothing to do here */
			break;
		}
	}
	kfree(cli);
}

void kbasep_kinstr_prfcnt_client_destroy(struct kbase_kinstr_prfcnt_client *cli)
{
	kbasep_kinstr_prfcnt_client_destroy_partial(cli, KINSTR_PRFCNT_INITIALISED);
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

size_t kbasep_kinstr_prfcnt_get_sample_md_count(const struct kbase_hwcnt_metadata *metadata,
						struct kbase_hwcnt_enable_map *enable_map)
{
	size_t grp, blk, blk_inst;
	size_t md_count = 0;

	if (!metadata)
		return 0;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		/* Skip unavailable, non-enabled or reserved blocks */
		if (kbase_kinstr_is_block_type_reserved(metadata, grp, blk) ||
		    !kbase_hwcnt_metadata_block_instance_avail(metadata, grp, blk, blk_inst) ||
		    !kbase_hwcnt_enable_map_block_enabled(enable_map, grp, blk, blk_inst))
			continue;

		md_count++;
	}

	/* add counts for clock_meta and sample meta, respectively */
	md_count += 2;

	/* Reserve one for last sentinel item. */
	md_count++;

	return md_count;
}

static size_t kbasep_kinstr_prfcnt_get_sample_size(struct kbase_kinstr_prfcnt_client *cli,
						   const struct kbase_hwcnt_metadata *metadata)
{
	size_t dump_buf_bytes;
	size_t clk_cnt_buf_bytes;
	size_t sample_meta_bytes;
	struct kbase_hwcnt_dump_buffer *dump_buf = &cli->tmp_buf;
	size_t md_count = kbasep_kinstr_prfcnt_get_sample_md_count(metadata, &cli->enable_map);

	if (!metadata)
		return 0;

	sample_meta_bytes = sizeof(struct prfcnt_metadata) * md_count;
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
	struct kbase_kinstr_prfcnt_context *kinstr_ctx = container_of(
		work, struct kbase_kinstr_prfcnt_context, dump_work);
	struct kbase_kinstr_prfcnt_client *pos;
	u64 cur_time_ns;

	mutex_lock(&kinstr_ctx->lock);

	cur_time_ns = kbasep_kinstr_prfcnt_timestamp_ns();

	list_for_each_entry(pos, &kinstr_ctx->clients, node) {
		if (pos->active && (pos->next_dump_time_ns != 0) &&
		    (pos->next_dump_time_ns < cur_time_ns))
			kbasep_kinstr_prfcnt_client_dump(pos, BASE_HWCNT_READER_EVENT_PERIODIC,
							 pos->user_data);
	}

	kbasep_kinstr_prfcnt_reschedule_worker(kinstr_ctx);

	mutex_unlock(&kinstr_ctx->lock);
}

/**
 * kbasep_kinstr_prfcnt_dump_timer() - Dump timer that schedules the dump worker for
 *                              execution as soon as possible.
 * @timer: Timer structure.
 *
 * Return: HRTIMER_NORESTART always.
 */
static enum hrtimer_restart
kbasep_kinstr_prfcnt_dump_timer(struct hrtimer *timer)
{
	struct kbase_kinstr_prfcnt_context *kinstr_ctx = container_of(
		timer, struct kbase_kinstr_prfcnt_context, dump_timer);

	/* We don't need to check kinstr_ctx->suspend_count here.
	 * Suspend and resume functions already ensure that the worker
	 * is cancelled when the driver is suspended, and resumed when
	 * the suspend_count reaches 0.
	 */
	kbase_hwcnt_virtualizer_queue_work(kinstr_ctx->hvirt,
					   &kinstr_ctx->dump_work);

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

	/* Non-zero client count implies client leak */
	if (WARN_ON(kinstr_ctx->client_count > 0)) {
		struct kbase_kinstr_prfcnt_client *pos, *n;

		list_for_each_entry_safe (pos, n, &kinstr_ctx->clients, node) {
			list_del(&pos->node);
			kinstr_ctx->client_count--;
			kbasep_kinstr_prfcnt_client_destroy(pos);
		}
	}

	cancel_work_sync(&kinstr_ctx->dump_work);

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

			list_for_each_entry (pos, &kinstr_ctx->clients, node) {
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

static int kbasep_kinstr_prfcnt_sample_array_alloc(struct kbase_kinstr_prfcnt_client *cli,
						   const struct kbase_hwcnt_metadata *metadata)
{
	struct kbase_kinstr_prfcnt_sample_array *sample_arr = &cli->sample_arr;
	struct kbase_kinstr_prfcnt_sample *samples;
	size_t sample_idx;
	size_t dump_buf_bytes;
	size_t clk_cnt_buf_bytes;
	size_t sample_meta_bytes;
	size_t md_count;
	size_t sample_size;
	size_t buffer_count = cli->config.buffer_count;

	if (!metadata || !sample_arr)
		return -EINVAL;

	md_count = kbasep_kinstr_prfcnt_get_sample_md_count(metadata, &cli->enable_map);
	sample_meta_bytes = sizeof(struct prfcnt_metadata) * md_count;
	dump_buf_bytes = metadata->dump_buf_bytes;
	clk_cnt_buf_bytes =
		sizeof(*samples->dump_buf.clk_cnt_buf) * metadata->clk_cnt;
	sample_size = sample_meta_bytes + dump_buf_bytes + clk_cnt_buf_bytes;

	samples = kmalloc_array(buffer_count, sizeof(*samples), GFP_KERNEL);

	if (!samples)
		return -ENOMEM;

	sample_arr->user_buf = vmalloc_user(sample_size * buffer_count);

	if (!sample_arr->user_buf) {
		kfree(samples);
		return -ENOMEM;
	}

	sample_arr->sample_count = buffer_count;
	sample_arr->samples = samples;

	for (sample_idx = 0; sample_idx < buffer_count; sample_idx++) {
		const size_t sample_meta_offset = sample_size * sample_idx;
		const size_t dump_buf_offset =
			sample_meta_offset + sample_meta_bytes;
		const size_t clk_cnt_buf_offset =
			dump_buf_offset + dump_buf_bytes;

		/* Internal layout in a sample buffer: [sample metadata, dump_buf, clk_cnt_buf]. */
		samples[sample_idx].dump_buf.metadata = metadata;
		samples[sample_idx].sample_meta =
			(struct prfcnt_metadata *)(sample_arr->user_buf + sample_meta_offset);
		samples[sample_idx].dump_buf.dump_buf =
			(u64 *)(sample_arr->user_buf + dump_buf_offset);
		samples[sample_idx].dump_buf.clk_cnt_buf =
			(u64 *)(sample_arr->user_buf + clk_cnt_buf_offset);
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
 * kbasep_kinstr_prfcnt_parse_request_scope - Parse a scope request
 * @req_scope: Performance counters scope request to parse.
 * @config:    Client object the session configuration should be written to.
 *
 * This function parses a performance counters scope request.
 * There are only 2 acceptable outcomes: either the client leaves the scope
 * as undefined, or all the scope requests are set to the same value.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_kinstr_prfcnt_parse_request_scope(
	const struct prfcnt_request_scope *req_scope,
	struct kbase_kinstr_prfcnt_client_config *config)
{
	int err = 0;

	if (config->scope == PRFCNT_SCOPE_RESERVED)
		config->scope = req_scope->scope;
	else if (config->scope != req_scope->scope)
		err = -EINVAL;

	return err;
}

/**
 * kbasep_kinstr_prfcnt_parse_setup - Parse session setup
 * @kinstr_ctx: Pointer to the kinstr_prfcnt context.
 * @setup:      Session setup information to parse.
 * @config:     Client object the session configuration should be written to.
 * @req_arr:    Pointer to array of request items for client session.
 *
 * This function parses the list of "request" items sent by the user space
 * client, and writes the configuration for the new client to be created
 * for the session.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_kinstr_prfcnt_parse_setup(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
					    union kbase_ioctl_kinstr_prfcnt_setup *setup,
					    struct kbase_kinstr_prfcnt_client_config *config,
					    struct prfcnt_request_item *req_arr)
{
	uint32_t i;
	unsigned int item_count = setup->in.request_item_count;
	int err = 0;

	if (req_arr[item_count - 1].hdr.item_type != FLEX_LIST_TYPE_NONE ||
	    req_arr[item_count - 1].hdr.item_version != 0) {
		return -EINVAL;
	}

	/* The session configuration can only feature one value for some
	 * properties (like capture mode, block counter set and scope), but the
	 * client may potential issue multiple requests and try to set more than
	 * one value for those properties. While issuing multiple requests for the
	 * same property is allowed by the protocol, asking for different values
	 * is illegal. Leaving these properties as undefined is illegal, too.
	 */
	config->prfcnt_mode = PRFCNT_MODE_RESERVED;
	config->counter_set = KBASE_HWCNT_SET_UNDEFINED;
	config->scope = PRFCNT_SCOPE_RESERVED;

	for (i = 0; i < item_count - 1; i++) {
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
				config->period_ns =
					req_arr[i]
						.u.req_mode.mode_config.periodic
						.period_ns;

				if ((config->period_ns != 0) &&
				    (config->period_ns <
				     DUMP_INTERVAL_MIN_NS)) {
					config->period_ns =
						DUMP_INTERVAL_MIN_NS;
				}

				if (config->period_ns == 0)
					err = -EINVAL;
			}
			break;

		case PRFCNT_REQUEST_TYPE_ENABLE:
			err = kbasep_kinstr_prfcnt_parse_request_enable(
				&req_arr[i].u.req_enable, config);
			break;

		case PRFCNT_REQUEST_TYPE_SCOPE:
			err = kbasep_kinstr_prfcnt_parse_request_scope(
				&req_arr[i].u.req_scope, config);
			break;

		default:
			err = -EINVAL;
			break;
		}

		if (err < 0)
			break;
	}

	if (!err) {
		/* Verify that properties (like capture mode and block counter
		 * set) have been defined by the user space client.
		 */
		if (config->prfcnt_mode == PRFCNT_MODE_RESERVED)
			err = -EINVAL;

		if (config->counter_set == KBASE_HWCNT_SET_UNDEFINED)
			err = -EINVAL;
	}

	return err;
}

int kbasep_kinstr_prfcnt_client_create(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
				       union kbase_ioctl_kinstr_prfcnt_setup *setup,
				       struct kbase_kinstr_prfcnt_client **out_vcli,
				       struct prfcnt_request_item *req_arr)
{
	int err;
	struct kbase_kinstr_prfcnt_client *cli;
	enum kbase_kinstr_prfcnt_client_init_state init_state;

	if (WARN_ON(!kinstr_ctx))
		return -EINVAL;

	if (WARN_ON(!setup))
		return -EINVAL;

	if (WARN_ON(!req_arr))
		return -EINVAL;

	cli = kzalloc(sizeof(*cli), GFP_KERNEL);

	if (!cli)
		return -ENOMEM;

	for (init_state = KINSTR_PRFCNT_UNINITIALISED; init_state < KINSTR_PRFCNT_INITIALISED;
	     init_state++) {
		err = 0;
		switch (init_state) {
		case KINSTR_PRFCNT_PARSE_SETUP:
			cli->kinstr_ctx = kinstr_ctx;
			err = kbasep_kinstr_prfcnt_parse_setup(kinstr_ctx, setup, &cli->config,
							       req_arr);

			break;

		case KINSTR_PRFCNT_ENABLE_MAP:
			cli->config.buffer_count = MAX_BUFFER_COUNT;
			cli->dump_interval_ns = cli->config.period_ns;
			cli->next_dump_time_ns = 0;
			cli->active = false;
			atomic_set(&cli->write_idx, 0);
			atomic_set(&cli->read_idx, 0);
			atomic_set(&cli->fetch_idx, 0);

			err = kbase_hwcnt_enable_map_alloc(kinstr_ctx->metadata, &cli->enable_map);
			break;

		case KINSTR_PRFCNT_DUMP_BUFFER:
			kbase_hwcnt_gpu_enable_map_from_physical(&cli->enable_map,
								 &cli->config.phys_em);

			cli->sample_count = cli->config.buffer_count;
			cli->sample_size =
				kbasep_kinstr_prfcnt_get_sample_size(cli, kinstr_ctx->metadata);

			/* Use virtualizer's metadata to alloc tmp buffer which interacts with
			 * the HWC virtualizer.
			 */
			err = kbase_hwcnt_dump_buffer_alloc(kinstr_ctx->metadata, &cli->tmp_buf);
			break;

		case KINSTR_PRFCNT_SAMPLE_ARRAY:
			/* Disable clock map in setup, and enable clock map when start */
			cli->enable_map.clk_enable_map = 0;

			/* Use metadata from virtualizer to allocate dump buffers  if
			 * kinstr_prfcnt doesn't have the truncated metadata.
			 */
			err = kbasep_kinstr_prfcnt_sample_array_alloc(cli, kinstr_ctx->metadata);

			break;

		case KINSTR_PRFCNT_VIRTUALIZER_CLIENT:
			/* Set enable map to be 0 to prevent virtualizer to init and kick the
			 * backend to count.
			 */
			kbase_hwcnt_gpu_enable_map_from_physical(
				&cli->enable_map, &(struct kbase_hwcnt_physical_enable_map){ 0 });

			err = kbase_hwcnt_virtualizer_client_create(kinstr_ctx->hvirt,
								    &cli->enable_map, &cli->hvcli);
			break;

		case KINSTR_PRFCNT_WAITQ_MUTEX:
			init_waitqueue_head(&cli->waitq);
			mutex_init(&cli->cmd_sync_lock);
			break;

		case KINSTR_PRFCNT_INITIALISED:
			/* This shouldn't be reached */
			break;
		}

		if (err < 0) {
			kbasep_kinstr_prfcnt_client_destroy_partial(cli, init_state);
			return err;
		}
	}
	*out_vcli = cli;

	return 0;

}

static size_t kbasep_kinstr_prfcnt_get_block_info_count(
	const struct kbase_hwcnt_metadata *metadata)
{
	size_t grp, blk;
	size_t block_info_count = 0;

	if (!metadata)
		return 0;

	for (grp = 0; grp < kbase_hwcnt_metadata_group_count(metadata); grp++) {
		for (blk = 0; blk < kbase_hwcnt_metadata_block_count(metadata, grp); blk++) {
			if (!kbase_kinstr_is_block_type_reserved(metadata, grp, blk))
				block_info_count++;
		}
	}

	return block_info_count;
}

static void kbasep_kinstr_prfcnt_get_request_info_list(
	struct prfcnt_enum_item *item_arr, size_t *arr_idx)
{
	memcpy(&item_arr[*arr_idx], kinstr_prfcnt_supported_requests,
	       sizeof(kinstr_prfcnt_supported_requests));
	*arr_idx += ARRAY_SIZE(kinstr_prfcnt_supported_requests);
}

static void kbasep_kinstr_prfcnt_get_sample_info_item(const struct kbase_hwcnt_metadata *metadata,
						      struct prfcnt_enum_item *item_arr,
						      size_t *arr_idx)
{
	struct prfcnt_enum_item sample_info = {
		.hdr = {
				.item_type = PRFCNT_ENUM_TYPE_SAMPLE_INFO,
				.item_version = PRFCNT_READER_API_VERSION,
			},
		.u.sample_info = {
				.num_clock_domains = metadata->clk_cnt,
			},
	};

	item_arr[*arr_idx] = sample_info;
	*arr_idx += 1;
}

int kbasep_kinstr_prfcnt_get_block_info_list(const struct kbase_hwcnt_metadata *metadata,
					     size_t block_set, struct prfcnt_enum_item *item_arr,
					     size_t *arr_idx)
{
	size_t grp, blk;

	if (!metadata || !item_arr || !arr_idx)
		return -EINVAL;

	for (grp = 0; grp < kbase_hwcnt_metadata_group_count(metadata); grp++) {
		for (blk = 0; blk < kbase_hwcnt_metadata_block_count(metadata, grp); blk++) {
			size_t blk_inst;
			size_t unused_blk_inst_count = 0;
			size_t blk_inst_count =
				kbase_hwcnt_metadata_block_instance_count(metadata, grp, blk);
			enum prfcnt_block_type block_type =
				kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(
					kbase_hwcnt_metadata_block_type(metadata, grp, blk));

			if (block_type == PRFCNT_BLOCK_TYPE_RESERVED)
				continue;

			/* Count number of unused blocks to updated number of instances */
			for (blk_inst = 0; blk_inst < blk_inst_count; blk_inst++) {
				if (!kbase_hwcnt_metadata_block_instance_avail(metadata, grp, blk,
									       blk_inst))
					unused_blk_inst_count++;
			}

			item_arr[(*arr_idx)++] = (struct prfcnt_enum_item){
				.hdr = {
					.item_type = PRFCNT_ENUM_TYPE_BLOCK,
					.item_version = PRFCNT_READER_API_VERSION,
				},
				.u.block_counter = {
					.set = block_set,
					.block_type = block_type,
					.num_instances = blk_inst_count - unused_blk_inst_count,
					.num_values = kbase_hwcnt_metadata_block_values_count(
						metadata, grp, blk),
					/* The bitmask of available counters should be dynamic.
					 * Temporarily, it is set to U64_MAX, waiting for the
					 * required functionality to be available in the future.
					 */
					.counter_mask = {U64_MAX, U64_MAX},
				},
			};
		}
	}

	return 0;
}

static int kbasep_kinstr_prfcnt_enum_info_count(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	struct kbase_ioctl_kinstr_prfcnt_enum_info *enum_info)
{
	uint32_t count = 0;
	size_t block_info_count = 0;
	const struct kbase_hwcnt_metadata *metadata;

	count = ARRAY_SIZE(kinstr_prfcnt_supported_requests);
	metadata = kbase_hwcnt_virtualizer_metadata(kinstr_ctx->hvirt);

	/* Add the sample_info (clock domain) descriptive item */
	count++;

	/* Other blocks based on meta data */
	block_info_count = kbasep_kinstr_prfcnt_get_block_info_count(metadata);
	count += block_info_count;

	/* Reserve one for the last sentinel item. */
	count++;
	enum_info->info_item_count = count;
	enum_info->info_item_size = sizeof(struct prfcnt_enum_item);
	kinstr_ctx->info_item_count = count;

	return 0;
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

	prfcnt_item_arr = kcalloc(enum_info->info_item_count,
				  sizeof(*prfcnt_item_arr), GFP_KERNEL);
	if (!prfcnt_item_arr)
		return -ENOMEM;

	kbasep_kinstr_prfcnt_get_request_info_list(prfcnt_item_arr, &arr_idx);

	metadata = kbase_hwcnt_virtualizer_metadata(kinstr_ctx->hvirt);
	/* Place the sample_info item */
	kbasep_kinstr_prfcnt_get_sample_info_item(metadata, prfcnt_item_arr, &arr_idx);

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

	if (!err) {
		unsigned long bytes =
			enum_info->info_item_count * sizeof(*prfcnt_item_arr);

		if (copy_to_user(u64_to_user_ptr(enum_info->info_list_ptr),
				 prfcnt_item_arr, bytes))
			err = -EFAULT;
	}

	kfree(prfcnt_item_arr);
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
	size_t item_count;
	size_t bytes;
	struct prfcnt_request_item *req_arr = NULL;
	struct kbase_kinstr_prfcnt_client *cli = NULL;
	const size_t max_bytes = 32 * sizeof(*req_arr);

	if (!kinstr_ctx || !setup)
		return -EINVAL;

	item_count = setup->in.request_item_count;

	/* Limiting the request items to 2x of the expected: accommodating
	 * moderate duplications but rejecting excessive abuses.
	 */
	if (!setup->in.requests_ptr || (item_count < 2) || (setup->in.request_item_size == 0) ||
	    item_count > 2 * kinstr_ctx->info_item_count) {
		return -EINVAL;
	}

	if (check_mul_overflow(item_count, sizeof(*req_arr), &bytes))
		return -EINVAL;

	/* Further limiting the max bytes to copy from userspace by setting it in the following
	 * fashion: a maximum of 1 mode item, 4 types of 3 sets for a total of 12 enable items,
	 * each currently at the size of prfcnt_request_item.
	 *
	 * Note: if more request types get added, this max limit needs to be updated.
	 */
	if (bytes > max_bytes)
		return -EINVAL;

	req_arr = memdup_user(u64_to_user_ptr(setup->in.requests_ptr), bytes);

	if (IS_ERR(req_arr))
		return PTR_ERR(req_arr);

	err = kbasep_kinstr_prfcnt_client_create(kinstr_ctx, setup, &cli, req_arr);

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

	goto free_buf;

client_installed_error:
	mutex_lock(&kinstr_ctx->lock);
	kinstr_ctx->client_count--;
	list_del(&cli->node);
	mutex_unlock(&kinstr_ctx->lock);
error:
	kbasep_kinstr_prfcnt_client_destroy(cli);
free_buf:
	kfree(req_arr);
	return err;
}
