/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
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



#include <linux/anon_inodes.h>
#include <linux/atomic.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/preempt.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <mali_kbase.h>
#include <mali_kbase_hwcnt_reader.h>
#include <mali_kbase_mem_linux.h>

/*****************************************************************************/

/* Hwcnt reader API version */
#define HWCNT_READER_API        1

/* The number of nanoseconds in a second. */
#define NSECS_IN_SEC            1000000000ull /* ns */

/* The time resolution of dumping service. */
#define DUMPING_RESOLUTION      500000ull /* ns */

/* The maximal supported number of dumping buffers. */
#define MAX_BUFFER_COUNT        32

/* Size and number of hw counters blocks. */
#define NR_CNT_BLOCKS_PER_GROUP 8
#define NR_CNT_PER_BLOCK        64
#define NR_BYTES_PER_CNT        4
#define NR_BYTES_PER_HDR        16
#define PRFCNT_EN_MASK_OFFSET   0x8

/*****************************************************************************/

enum {
	SHADER_HWCNT_BM,
	TILER_HWCNT_BM,
	MMU_L2_HWCNT_BM,
	JM_HWCNT_BM
};

/**
 * struct kbase_vinstr_context - vinstr context per device
 * @lock:              protects the entire vinstr context
 * @kbdev:             pointer to kbase device
 * @kctx:              pointer to kbase context
 * @vmap:              vinstr vmap for mapping hwcnt dump buffer
 * @gpu_va:            GPU hwcnt dump buffer address
 * @cpu_va:            the CPU side mapping of the hwcnt dump buffer
 * @dump_size:         size of the dump buffer in bytes
 * @bitmap:            current set of counters monitored, not always in sync
 *                     with hardware
 * @reprogram:         when true, reprogram hwcnt block with the new set of
 *                     counters
 * @suspended:         when true, the context has been suspended
 * @nclients:          number of attached clients, pending or otherwise
 * @waiting_clients:   head of list of clients being periodically sampled
 * @idle_clients:      head of list of clients being idle
 * @suspended_clients: head of list of clients being suspended
 * @thread:            periodic sampling thread
 * @waitq:             notification queue of sampling thread
 * @request_pending:   request for action for sampling thread
 */
struct kbase_vinstr_context {
	struct mutex             lock;
	struct kbase_device      *kbdev;
	struct kbase_context     *kctx;

	struct kbase_vmap_struct vmap;
	u64                      gpu_va;
	void                     *cpu_va;
	size_t                   dump_size;
	u32                      bitmap[4];
	bool                     reprogram;
	bool                     suspended;

	u32                      nclients;
	struct list_head         waiting_clients;
	struct list_head         idle_clients;
	struct list_head         suspended_clients;

	struct task_struct       *thread;
	wait_queue_head_t        waitq;
	atomic_t                 request_pending;
};

/**
 * struct kbase_vinstr_client - a vinstr client attached to a vinstr context
 * @vinstr_ctx:    vinstr context client is attached to
 * @list:          node used to attach this client to list in vinstr context
 * @buffer_count:  number of buffers this client is using
 * @event_mask:    events this client reacts to
 * @dump_size:     size of one dump buffer in bytes
 * @bitmap:        bitmap request for JM, TILER, SHADER and MMU counters
 * @legacy_buffer: userspace hwcnt dump buffer (legacy interface)
 * @accum_buffer:  temporary accumulation buffer for preserving counters
 * @dump_time:     next time this clients shall request hwcnt dump
 * @dump_interval: interval between periodic hwcnt dumps
 * @dump_buffers:  kernel hwcnt dump buffers allocated by this client
 * @dump_buffers_meta: metadata of dump buffers
 * @meta_idx:      index of metadata being accessed by userspace
 * @read_idx:      index of buffer read by userspace
 * @write_idx:     index of buffer being written by dumping service
 * @waitq:         client's notification queue
 * @pending:       when true, client has attached but hwcnt not yet updated
 */
struct kbase_vinstr_client {
	struct kbase_vinstr_context        *vinstr_ctx;
	struct list_head                   list;
	unsigned int                       buffer_count;
	u32                                event_mask;
	size_t                             dump_size;
	u32                                bitmap[4];
	void __user                        *legacy_buffer;
	void                               *accum_buffer;
	u64                                dump_time;
	u32                                dump_interval;
	char                               *dump_buffers;
	struct kbase_hwcnt_reader_metadata *dump_buffers_meta;
	atomic_t                           meta_idx;
	atomic_t                           read_idx;
	atomic_t                           write_idx;
	wait_queue_head_t                  waitq;
	bool                               pending;
};

/**
 * struct kbasep_vinstr_wake_up_timer - vinstr service thread wake up timer
 * @hrtimer:    high resolution timer
 * @vinstr_ctx: vinstr context
 */
struct kbasep_vinstr_wake_up_timer {
	struct hrtimer              hrtimer;
	struct kbase_vinstr_context *vinstr_ctx;
};

/*****************************************************************************/

static int kbasep_vinstr_service_task(void *data);

static unsigned int kbasep_vinstr_hwcnt_reader_poll(
		struct file *filp,
		poll_table  *wait);
static long kbasep_vinstr_hwcnt_reader_ioctl(
		struct file   *filp,
		unsigned int  cmd,
		unsigned long arg);
static int kbasep_vinstr_hwcnt_reader_mmap(
		struct file           *filp,
		struct vm_area_struct *vma);
static int kbasep_vinstr_hwcnt_reader_release(
		struct inode *inode,
		struct file  *filp);

/* The timeline stream file operations structure. */
static const struct file_operations vinstr_client_fops = {
	.poll           = kbasep_vinstr_hwcnt_reader_poll,
	.unlocked_ioctl = kbasep_vinstr_hwcnt_reader_ioctl,
	.compat_ioctl   = kbasep_vinstr_hwcnt_reader_ioctl,
	.mmap           = kbasep_vinstr_hwcnt_reader_mmap,
	.release        = kbasep_vinstr_hwcnt_reader_release,
};

/*****************************************************************************/

static int enable_hwcnt(struct kbase_vinstr_context *vinstr_ctx)
{
	struct kbase_uk_hwcnt_setup setup;

	setup.dump_buffer = vinstr_ctx->gpu_va;
	setup.jm_bm       = vinstr_ctx->bitmap[JM_HWCNT_BM];
	setup.tiler_bm    = vinstr_ctx->bitmap[TILER_HWCNT_BM];
	setup.shader_bm   = vinstr_ctx->bitmap[SHADER_HWCNT_BM];
	setup.mmu_l2_bm   = vinstr_ctx->bitmap[MMU_L2_HWCNT_BM];

	return kbase_instr_hwcnt_enable(vinstr_ctx->kctx, &setup);
}

static void disable_hwcnt(struct kbase_vinstr_context *vinstr_ctx)
{
	kbase_instr_hwcnt_disable(vinstr_ctx->kctx);
}

static int reprogram_hwcnt(struct kbase_vinstr_context *vinstr_ctx)
{
	disable_hwcnt(vinstr_ctx);
	return enable_hwcnt(vinstr_ctx);
}

static void hwcnt_bitmap_set(u32 dst[4], u32 src[4])
{
	dst[JM_HWCNT_BM]     = src[JM_HWCNT_BM];
	dst[TILER_HWCNT_BM]  = src[TILER_HWCNT_BM];
	dst[SHADER_HWCNT_BM] = src[SHADER_HWCNT_BM];
	dst[MMU_L2_HWCNT_BM] = src[MMU_L2_HWCNT_BM];
}

static void hwcnt_bitmap_union(u32 dst[4], u32 src[4])
{
	dst[JM_HWCNT_BM]     |= src[JM_HWCNT_BM];
	dst[TILER_HWCNT_BM]  |= src[TILER_HWCNT_BM];
	dst[SHADER_HWCNT_BM] |= src[SHADER_HWCNT_BM];
	dst[MMU_L2_HWCNT_BM] |= src[MMU_L2_HWCNT_BM];
}

static size_t kbasep_vinstr_dump_size(struct kbase_vinstr_context *vinstr_ctx)
{
	struct kbase_device *kbdev = vinstr_ctx->kctx->kbdev;
	size_t dump_size;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_V4)) {
		u32 nr_cg;

		nr_cg = kbdev->gpu_props.num_core_groups;
		dump_size = nr_cg * NR_CNT_BLOCKS_PER_GROUP *
				NR_CNT_PER_BLOCK *
				NR_BYTES_PER_CNT;
	} else {
		/* assume v5 for now */
		base_gpu_props *props = &kbdev->gpu_props.props;
		u32 nr_l2 = props->l2_props.num_l2_slices;
		u64 core_mask = props->coherency_info.group[0].core_mask;
		u32 nr_blocks = fls64(core_mask);

		/* JM and tiler counter blocks are always present */
		dump_size = (2 + nr_l2 + nr_blocks) *
				NR_CNT_PER_BLOCK *
				NR_BYTES_PER_CNT;
	}
	return dump_size;
}

static int kbasep_vinstr_map_kernel_dump_buffer(
		struct kbase_vinstr_context *vinstr_ctx)
{
	struct kbase_va_region *reg;
	struct kbase_context *kctx = vinstr_ctx->kctx;
	u64 flags, nr_pages;
	u16 va_align = 0;

	flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_GPU_WR;
	vinstr_ctx->dump_size = kbasep_vinstr_dump_size(vinstr_ctx);
	nr_pages = PFN_UP(vinstr_ctx->dump_size);

	reg = kbase_mem_alloc(kctx, nr_pages, nr_pages, 0, &flags,
			&vinstr_ctx->gpu_va, &va_align);
	if (!reg)
		return -ENOMEM;

	vinstr_ctx->cpu_va = kbase_vmap(
			kctx,
			vinstr_ctx->gpu_va,
			vinstr_ctx->dump_size,
			&vinstr_ctx->vmap);
	if (!vinstr_ctx->cpu_va) {
		kbase_mem_free(kctx, vinstr_ctx->gpu_va);
		return -ENOMEM;
	}

	return 0;
}

static void kbasep_vinstr_unmap_kernel_dump_buffer(
		struct kbase_vinstr_context *vinstr_ctx)
{
	struct kbase_context *kctx = vinstr_ctx->kctx;

	kbase_vunmap(kctx, &vinstr_ctx->vmap);
	kbase_mem_free(kctx, vinstr_ctx->gpu_va);
}

/**
 * kbasep_vinstr_create_kctx - create kernel context for vinstr
 * @vinstr_ctx: vinstr context
 * Return: zero on success
 */
static int kbasep_vinstr_create_kctx(struct kbase_vinstr_context *vinstr_ctx)
{
	int err;

	vinstr_ctx->kctx = kbase_create_context(vinstr_ctx->kbdev, true);
	if (!vinstr_ctx->kctx)
		return -ENOMEM;

	/* Map the master kernel dump buffer.  The HW dumps the counters
	 * into this memory region. */
	err = kbasep_vinstr_map_kernel_dump_buffer(vinstr_ctx);
	if (err) {
		kbase_destroy_context(vinstr_ctx->kctx);
		vinstr_ctx->kctx = NULL;
		return err;
	}

	err = enable_hwcnt(vinstr_ctx);
	if (err) {
		kbasep_vinstr_unmap_kernel_dump_buffer(vinstr_ctx);
		kbase_destroy_context(vinstr_ctx->kctx);
		vinstr_ctx->kctx = NULL;
		return err;
	}

	vinstr_ctx->thread = kthread_run(
			kbasep_vinstr_service_task,
			vinstr_ctx,
			"mali_vinstr_service");
	if (!vinstr_ctx->thread) {
		disable_hwcnt(vinstr_ctx);
		kbasep_vinstr_unmap_kernel_dump_buffer(vinstr_ctx);
		kbase_destroy_context(vinstr_ctx->kctx);
		vinstr_ctx->kctx = NULL;
		return -EFAULT;
	}

	return 0;
}

/**
 * kbasep_vinstr_destroy_kctx - destroy vinstr's kernel context
 * @vinstr_ctx: vinstr context
 */
static void kbasep_vinstr_destroy_kctx(struct kbase_vinstr_context *vinstr_ctx)
{
	/* Release hw counters dumping resources. */
	vinstr_ctx->thread = NULL;
	disable_hwcnt(vinstr_ctx);
	kbasep_vinstr_unmap_kernel_dump_buffer(vinstr_ctx);
	kbase_destroy_context(vinstr_ctx->kctx);
	vinstr_ctx->kctx = NULL;
}

/**
 * kbasep_vinstr_attach_client - Attach a client to the vinstr core
 * @vinstr_ctx:   vinstr context
 * @buffer_count: requested number of dump buffers
 * @bitmap:       bitmaps describing which counters should be enabled
 * @argp:         pointer where notification descriptor shall be stored
 *
 * Return: vinstr opaque client handle or NULL on failure
 */
static struct kbase_vinstr_client *kbasep_vinstr_attach_client(
		struct kbase_vinstr_context *vinstr_ctx, u32 buffer_count,
		u32 bitmap[4], void *argp)
{
	struct task_struct         *thread = NULL;
	struct kbase_vinstr_client *cli;

	KBASE_DEBUG_ASSERT(vinstr_ctx);
	KBASE_DEBUG_ASSERT(argp);
	KBASE_DEBUG_ASSERT(buffer_count >= 0);
	KBASE_DEBUG_ASSERT(buffer_count <= MAX_BUFFER_COUNT);
	KBASE_DEBUG_ASSERT(!(buffer_count & (buffer_count - 1)));

	cli = kzalloc(sizeof(*cli), GFP_KERNEL);
	if (!cli)
		return NULL;

	cli->vinstr_ctx   = vinstr_ctx;
	cli->buffer_count = buffer_count;
	cli->event_mask   =
		(1 << BASE_HWCNT_READER_EVENT_MANUAL) |
		(1 << BASE_HWCNT_READER_EVENT_PERIODIC);
	cli->pending      = true;

	hwcnt_bitmap_set(cli->bitmap, bitmap);

	mutex_lock(&vinstr_ctx->lock);

	hwcnt_bitmap_union(vinstr_ctx->bitmap, cli->bitmap);
	vinstr_ctx->reprogram = true;

	/* If this is the first client, create the vinstr kbase
	 * context. This context is permanently resident until the
	 * last client exits. */
	if (!vinstr_ctx->nclients) {
		hwcnt_bitmap_set(vinstr_ctx->bitmap, cli->bitmap);
		if (kbasep_vinstr_create_kctx(vinstr_ctx) < 0)
			goto error;

		vinstr_ctx->reprogram = false;
		cli->pending = false;
	}

	/* The GPU resets the counter block every time there is a request
	 * to dump it. We need a per client kernel buffer for accumulating
	 * the counters. */
	cli->dump_size    = kbasep_vinstr_dump_size(vinstr_ctx);
	cli->accum_buffer = kzalloc(cli->dump_size, GFP_KERNEL);
	if (!cli->accum_buffer)
		goto error;

	/* Prepare buffers. */
	if (cli->buffer_count) {
		int *fd = (int *)argp;
		size_t tmp;

		/* Allocate area for buffers metadata storage. */
		tmp = sizeof(struct kbase_hwcnt_reader_metadata) *
			cli->buffer_count;
		cli->dump_buffers_meta = kmalloc(tmp, GFP_KERNEL);
		if (!cli->dump_buffers_meta)
			goto error;

		/* Allocate required number of dumping buffers. */
		cli->dump_buffers = (char *)__get_free_pages(
				GFP_KERNEL,
				get_order(cli->dump_size * cli->buffer_count));
		if (!cli->dump_buffers)
			goto error;

		/* Create descriptor for user-kernel data exchange. */
		*fd = anon_inode_getfd(
				"[mali_vinstr_desc]",
				&vinstr_client_fops,
				cli,
				O_RDONLY | O_CLOEXEC);
		if (0 > *fd)
			goto error;
	} else {
		cli->legacy_buffer = (void __user *)argp;
	}

	atomic_set(&cli->read_idx, 0);
	atomic_set(&cli->meta_idx, 0);
	atomic_set(&cli->write_idx, 0);
	init_waitqueue_head(&cli->waitq);

	vinstr_ctx->nclients++;
	list_add(&cli->list, &vinstr_ctx->idle_clients);

	mutex_unlock(&vinstr_ctx->lock);

	return cli;

error:
	kfree(cli->dump_buffers_meta);
	if (cli->dump_buffers)
		free_pages(
				(unsigned long)cli->dump_buffers,
				get_order(cli->dump_size * cli->buffer_count));
	kfree(cli->accum_buffer);
	if (!vinstr_ctx->nclients && vinstr_ctx->kctx) {
		thread = vinstr_ctx->thread;
		kbasep_vinstr_destroy_kctx(vinstr_ctx);
	}
	kfree(cli);

	mutex_unlock(&vinstr_ctx->lock);

	/* Thread must be stopped after lock is released. */
	if (thread)
		kthread_stop(thread);

	return NULL;
}

/**
 * kbasep_vinstr_detach_client - Detach a client from the vinstr core
 * @cli: Pointer to vinstr client
 */
static void kbasep_vinstr_detach_client(struct kbase_vinstr_client *cli)
{
	struct kbase_vinstr_context *vinstr_ctx;
	struct kbase_vinstr_client  *iter, *tmp;
	struct task_struct          *thread = NULL;
	u32 zerobitmap[4] = { 0 };
	int cli_found = 0;

	KBASE_DEBUG_ASSERT(cli);
	vinstr_ctx = cli->vinstr_ctx;
	KBASE_DEBUG_ASSERT(vinstr_ctx);

	mutex_lock(&vinstr_ctx->lock);

	list_for_each_entry_safe(iter, tmp, &vinstr_ctx->idle_clients, list) {
		if (iter == cli) {
			vinstr_ctx->reprogram = true;
			cli_found = 1;
			list_del(&iter->list);
			break;
		}
	}
	if (!cli_found) {
		list_for_each_entry_safe(
				iter, tmp, &vinstr_ctx->waiting_clients, list) {
			if (iter == cli) {
				vinstr_ctx->reprogram = true;
				cli_found = 1;
				list_del(&iter->list);
				break;
			}
		}
	}
	KBASE_DEBUG_ASSERT(cli_found);

	kfree(cli->dump_buffers_meta);
	free_pages(
			(unsigned long)cli->dump_buffers,
			get_order(cli->dump_size * cli->buffer_count));
	kfree(cli->accum_buffer);
	kfree(cli);

	vinstr_ctx->nclients--;
	if (!vinstr_ctx->nclients) {
		thread = vinstr_ctx->thread;
		kbasep_vinstr_destroy_kctx(vinstr_ctx);
	}

	/* Rebuild context bitmap now that the client has detached */
	hwcnt_bitmap_set(vinstr_ctx->bitmap, zerobitmap);
	list_for_each_entry(iter, &vinstr_ctx->idle_clients, list)
		hwcnt_bitmap_union(vinstr_ctx->bitmap, iter->bitmap);
	list_for_each_entry(iter, &vinstr_ctx->waiting_clients, list)
		hwcnt_bitmap_union(vinstr_ctx->bitmap, iter->bitmap);

	mutex_unlock(&vinstr_ctx->lock);

	/* Thread must be stopped after lock is released. */
	if (thread)
		kthread_stop(thread);
}

/* Accumulate counters in the dump buffer */
static void accum_dump_buffer(void *dst, void *src, size_t dump_size)
{
	size_t block_size = NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT;
	u32 *d = dst;
	u32 *s = src;
	size_t i, j;

	for (i = 0; i < dump_size; i += block_size) {
		/* skip over the header block */
		d += NR_BYTES_PER_HDR / sizeof(u32);
		s += NR_BYTES_PER_HDR / sizeof(u32);
		for (j = 0; j < (block_size - NR_BYTES_PER_HDR) / sizeof(u32); j++) {
			/* saturate result if addition would result in wraparound */
			if (U32_MAX - *d < *s)
				*d = U32_MAX;
			else
				*d += *s;
			d++;
			s++;
		}
	}
}

/* This is the Midgard v4 patch function.  It copies the headers for each
 * of the defined blocks from the master kernel buffer and then patches up
 * the performance counter enable mask for each of the blocks to exclude
 * counters that were not requested by the client. */
static void patch_dump_buffer_hdr_v4(
		struct kbase_vinstr_context *vinstr_ctx,
		struct kbase_vinstr_client *cli)
{
	u32 *mask;
	u8 *dst = cli->accum_buffer;
	u8 *src = vinstr_ctx->cpu_va;
	u32 nr_cg = vinstr_ctx->kctx->kbdev->gpu_props.num_core_groups;
	size_t i, group_size, group;
	enum {
		SC0_BASE    = 0 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT,
		SC1_BASE    = 1 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT,
		SC2_BASE    = 2 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT,
		SC3_BASE    = 3 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT,
		TILER_BASE  = 4 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT,
		MMU_L2_BASE = 5 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT,
		JM_BASE     = 7 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT
	};

	group_size = NR_CNT_BLOCKS_PER_GROUP *
			NR_CNT_PER_BLOCK *
			NR_BYTES_PER_CNT;
	for (i = 0; i < nr_cg; i++) {
		group = i * group_size;
		/* copy shader core headers */
		memcpy(&dst[group + SC0_BASE], &src[group + SC0_BASE],
		       NR_BYTES_PER_HDR);
		memcpy(&dst[group + SC1_BASE], &src[group + SC1_BASE],
		       NR_BYTES_PER_HDR);
		memcpy(&dst[group + SC2_BASE], &src[group + SC2_BASE],
		      NR_BYTES_PER_HDR);
		memcpy(&dst[group + SC3_BASE], &src[group + SC3_BASE],
		      NR_BYTES_PER_HDR);

		/* copy tiler header */
		memcpy(&dst[group + TILER_BASE], &src[group + TILER_BASE],
		      NR_BYTES_PER_HDR);

		/* copy mmu header */
		memcpy(&dst[group + MMU_L2_BASE], &src[group + MMU_L2_BASE],
		      NR_BYTES_PER_HDR);

		/* copy job manager header */
		memcpy(&dst[group + JM_BASE], &src[group + JM_BASE],
		      NR_BYTES_PER_HDR);

		/* patch the shader core enable mask */
		mask = (u32 *)&dst[group + SC0_BASE + PRFCNT_EN_MASK_OFFSET];
		*mask &= cli->bitmap[SHADER_HWCNT_BM];
		mask = (u32 *)&dst[group + SC1_BASE + PRFCNT_EN_MASK_OFFSET];
		*mask &= cli->bitmap[SHADER_HWCNT_BM];
		mask = (u32 *)&dst[group + SC2_BASE + PRFCNT_EN_MASK_OFFSET];
		*mask &= cli->bitmap[SHADER_HWCNT_BM];
		mask = (u32 *)&dst[group + SC3_BASE + PRFCNT_EN_MASK_OFFSET];
		*mask &= cli->bitmap[SHADER_HWCNT_BM];

		/* patch the tiler core enable mask */
		mask = (u32 *)&dst[group + TILER_BASE + PRFCNT_EN_MASK_OFFSET];
		*mask &= cli->bitmap[TILER_HWCNT_BM];

		/* patch the mmu core enable mask */
		mask = (u32 *)&dst[group + MMU_L2_BASE + PRFCNT_EN_MASK_OFFSET];
		*mask &= cli->bitmap[MMU_L2_HWCNT_BM];

		/* patch the job manager enable mask */
		mask = (u32 *)&dst[group + JM_BASE + PRFCNT_EN_MASK_OFFSET];
		*mask &= cli->bitmap[JM_HWCNT_BM];
	}
}

/* This is the Midgard v5 patch function.  It copies the headers for each
 * of the defined blocks from the master kernel buffer and then patches up
 * the performance counter enable mask for each of the blocks to exclude
 * counters that were not requested by the client. */
static void patch_dump_buffer_hdr_v5(
		struct kbase_vinstr_context *vinstr_ctx,
		struct kbase_vinstr_client *cli)
{
	struct kbase_device *kbdev = vinstr_ctx->kctx->kbdev;
	u32 i, nr_l2;
	u64 core_mask;
	u32 *mask;
	u8 *dst = cli->accum_buffer;
	u8 *src = vinstr_ctx->cpu_va;
	size_t block_size = NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT;

	/* copy and patch job manager header */
	memcpy(dst, src, NR_BYTES_PER_HDR);
	mask = (u32 *)&dst[PRFCNT_EN_MASK_OFFSET];
	*mask &= cli->bitmap[JM_HWCNT_BM];
	dst += block_size;
	src += block_size;

	/* copy and patch tiler header */
	memcpy(dst, src, NR_BYTES_PER_HDR);
	mask = (u32 *)&dst[PRFCNT_EN_MASK_OFFSET];
	*mask &= cli->bitmap[TILER_HWCNT_BM];
	dst += block_size;
	src += block_size;

	/* copy and patch MMU/L2C headers */
	nr_l2 = kbdev->gpu_props.props.l2_props.num_l2_slices;
	for (i = 0; i < nr_l2; i++) {
		memcpy(dst, src, NR_BYTES_PER_HDR);
		mask = (u32 *)&dst[PRFCNT_EN_MASK_OFFSET];
		*mask &= cli->bitmap[MMU_L2_HWCNT_BM];
		dst += block_size;
		src += block_size;
	}

	/* copy and patch shader core headers */
	core_mask = kbdev->gpu_props.props.coherency_info.group[0].core_mask;
	while (0ull != core_mask) {
		memcpy(dst, src, NR_BYTES_PER_HDR);
		if (0ull != (core_mask & 1ull)) {
			/* if block is not reserved update header */
			mask = (u32 *)&dst[PRFCNT_EN_MASK_OFFSET];
			*mask &= cli->bitmap[SHADER_HWCNT_BM];
		}
		dst += block_size;
		src += block_size;

		core_mask >>= 1;
	}
}

/**
 * accum_clients - accumulate dumped hw counters for all known clients
 * @vinstr_ctx: vinstr context
 */
static void accum_clients(struct kbase_vinstr_context *vinstr_ctx)
{
	struct kbase_vinstr_client *iter;
	int v4;

	v4 = kbase_hw_has_feature(vinstr_ctx->kbdev, BASE_HW_FEATURE_V4);
	list_for_each_entry(iter, &vinstr_ctx->idle_clients, list) {
		/* Don't bother accumulating clients whose hwcnt requests
		 * have not yet been honoured. */
		if (iter->pending)
			continue;
		if (v4)
			patch_dump_buffer_hdr_v4(vinstr_ctx, iter);
		else
			patch_dump_buffer_hdr_v5(vinstr_ctx, iter);
		accum_dump_buffer(
				iter->accum_buffer,
				vinstr_ctx->cpu_va,
				iter->dump_size);
	}
	list_for_each_entry(iter, &vinstr_ctx->waiting_clients, list) {
		/* Don't bother accumulating clients whose hwcnt requests
		 * have not yet been honoured. */
		if (iter->pending)
			continue;
		if (v4)
			patch_dump_buffer_hdr_v4(vinstr_ctx, iter);
		else
			patch_dump_buffer_hdr_v5(vinstr_ctx, iter);
		accum_dump_buffer(
				iter->accum_buffer,
				vinstr_ctx->cpu_va,
				iter->dump_size);
	}
}

/*****************************************************************************/

/**
 * kbasep_vinstr_get_timestamp - return timestamp
 *
 * Function returns timestamp value based on raw monotonic timer. Value will
 * wrap around zero in case of overflow.
 *
 * Return: timestamp value
 */
static u64 kbasep_vinstr_get_timestamp(void)
{
	struct timespec ts;

	getrawmonotonic(&ts);
	return (u64)ts.tv_sec * NSECS_IN_SEC + ts.tv_nsec;
}

/**
 * kbasep_vinstr_add_dump_request - register client's dumping request
 * @cli:             requesting client
 * @waiting_clients: list of pending dumping requests
 */
static void kbasep_vinstr_add_dump_request(
		struct kbase_vinstr_client *cli,
		struct list_head *waiting_clients)
{
	struct kbase_vinstr_client *tmp;

	if (list_empty(waiting_clients)) {
		list_add(&cli->list, waiting_clients);
		return;
	}
	list_for_each_entry(tmp, waiting_clients, list) {
		if (tmp->dump_time > cli->dump_time) {
			list_add_tail(&cli->list, &tmp->list);
			return;
		}
	}
	list_add_tail(&cli->list, waiting_clients);
}

/**
 * kbasep_vinstr_collect_and_accumulate - collect hw counters via low level
 *                                        dump and accumulate them for known
 *                                        clients
 * @vinstr_ctx: vinstr context
 * @timestamp: pointer where collection timestamp will be recorded
 *
 * Return: zero on success
 */
static int kbasep_vinstr_collect_and_accumulate(
		struct kbase_vinstr_context *vinstr_ctx, u64 *timestamp)
{
	int rcode;

	/* Request HW counters dump.
	 * Disable preemption to make dump timestamp more accurate. */
	preempt_disable();
	*timestamp = kbasep_vinstr_get_timestamp();
	rcode = kbase_instr_hwcnt_request_dump(vinstr_ctx->kctx);
	preempt_enable();

	if (!rcode)
		rcode = kbase_instr_hwcnt_wait_for_dump(vinstr_ctx->kctx);
	WARN_ON(rcode);

	/* Accumulate values of collected counters. */
	if (!rcode)
		accum_clients(vinstr_ctx);

	return rcode;
}

/**
 * kbasep_vinstr_fill_dump_buffer - copy accumulated counters to empty kernel
 *                                  buffer
 * @cli:       requesting client
 * @timestamp: timestamp when counters were collected
 * @event_id:  id of event that caused triggered counters collection
 *
 * Return: zero on success
 */
static int kbasep_vinstr_fill_dump_buffer(
		struct kbase_vinstr_client *cli, u64 timestamp,
		enum base_hwcnt_reader_event event_id)
{
	unsigned int write_idx = atomic_read(&cli->write_idx);
	unsigned int read_idx  = atomic_read(&cli->read_idx);

	struct kbase_hwcnt_reader_metadata *meta;
	void                               *buffer;

	/* Check if there is a place to copy HWC block into. */
	if (write_idx - read_idx == cli->buffer_count)
		return -1;
	write_idx %= cli->buffer_count;

	/* Fill in dump buffer and its metadata. */
	buffer = &cli->dump_buffers[write_idx * cli->dump_size];
	meta   = &cli->dump_buffers_meta[write_idx];
	meta->timestamp  = timestamp;
	meta->event_id   = event_id;
	meta->buffer_idx = write_idx;
	memcpy(buffer, cli->accum_buffer, cli->dump_size);
	return 0;
}

/**
 * kbasep_vinstr_fill_dump_buffer_legacy - copy accumulated counters to buffer
 *                                         allocated in userspace
 * @cli: requesting client
 *
 * Return: zero on success
 *
 * This is part of legacy ioctl interface.
 */
static int kbasep_vinstr_fill_dump_buffer_legacy(
		struct kbase_vinstr_client *cli)
{
	void __user  *buffer = cli->legacy_buffer;
	int          rcode;

	/* Copy data to user buffer. */
	rcode = copy_to_user(buffer, cli->accum_buffer, cli->dump_size);
	if (rcode)
		pr_warn("error while copying buffer to user\n");
	return rcode;
}

/**
 * kbasep_vinstr_reprogram - reprogram hwcnt set collected by inst
 * @vinstr_ctx: vinstr context
 */
static void kbasep_vinstr_reprogram(
		struct kbase_vinstr_context *vinstr_ctx)
{
	if (vinstr_ctx->reprogram) {
		struct kbase_vinstr_client *iter;

		if (!reprogram_hwcnt(vinstr_ctx)) {
			vinstr_ctx->reprogram = false;
			list_for_each_entry(
					iter,
					&vinstr_ctx->idle_clients,
					list)
				iter->pending = false;
			list_for_each_entry(
					iter,
					&vinstr_ctx->waiting_clients,
					list)
				iter->pending = false;
		}
	}
}

/**
 * kbasep_vinstr_update_client - copy accumulated counters to user readable
 *                               buffer and notify the user
 * @cli:       requesting client
 * @timestamp: timestamp when counters were collected
 * @event_id:  id of event that caused triggered counters collection
 *
 * Return: zero on success
 */
static int kbasep_vinstr_update_client(
		struct kbase_vinstr_client *cli, u64 timestamp,
		enum base_hwcnt_reader_event event_id)
{
	int rcode = 0;

	/* Copy collected counters to user readable buffer. */
	if (cli->buffer_count)
		rcode = kbasep_vinstr_fill_dump_buffer(
				cli, timestamp, event_id);
	else
		rcode = kbasep_vinstr_fill_dump_buffer_legacy(cli);

	if (rcode)
		goto exit;


	/* Notify client. Make sure all changes to memory are visible. */
	wmb();
	atomic_inc(&cli->write_idx);
	wake_up_interruptible(&cli->waitq);

	/* Prepare for next request. */
	memset(cli->accum_buffer, 0, cli->dump_size);

exit:
	return rcode;
}

/**
 * kbasep_vinstr_wake_up_callback - vinstr wake up timer wake up function
 *
 * @hrtimer: high resolution timer
 *
 * Return: High resolution timer restart enum.
 */
static enum hrtimer_restart kbasep_vinstr_wake_up_callback(
		struct hrtimer *hrtimer)
{
	struct kbasep_vinstr_wake_up_timer *timer =
		container_of(
			hrtimer,
			struct kbasep_vinstr_wake_up_timer,
			hrtimer);

	KBASE_DEBUG_ASSERT(timer);

	atomic_set(&timer->vinstr_ctx->request_pending, 1);
	wake_up_all(&timer->vinstr_ctx->waitq);

	return HRTIMER_NORESTART;
}

/**
 * kbasep_vinstr_service_task - HWC dumping service thread
 *
 * @data: Pointer to vinstr context structure.
 *
 * Return: Always returns zero.
 */
static int kbasep_vinstr_service_task(void *data)
{
	struct kbase_vinstr_context        *vinstr_ctx = data;
	struct kbasep_vinstr_wake_up_timer timer;

	KBASE_DEBUG_ASSERT(vinstr_ctx);

	hrtimer_init(&timer.hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer.hrtimer.function = kbasep_vinstr_wake_up_callback;
	timer.vinstr_ctx       = vinstr_ctx;

	while (!kthread_should_stop()) {
		struct kbase_vinstr_client *cli = NULL;
		struct kbase_vinstr_client *tmp;

		u64              timestamp = kbasep_vinstr_get_timestamp();
		u64              dump_time = 0;
		struct list_head expired_requests;

		/* Hold lock while performing operations on lists of clients. */
		mutex_lock(&vinstr_ctx->lock);

		/* Closing thread must not interact with client requests. */
		if (current == vinstr_ctx->thread) {
			atomic_set(&vinstr_ctx->request_pending, 0);

			if (!list_empty(&vinstr_ctx->waiting_clients)) {
				cli = list_first_entry(
						&vinstr_ctx->waiting_clients,
						struct kbase_vinstr_client,
						list);
				dump_time = cli->dump_time;
			}
		}

		if (!cli || ((s64)timestamp - (s64)dump_time < 0ll)) {
			mutex_unlock(&vinstr_ctx->lock);

			/* Sleep until next dumping event or service request. */
			if (cli) {
				u64 diff = dump_time - timestamp;

				hrtimer_start(
						&timer.hrtimer,
						ns_to_ktime(diff),
						HRTIMER_MODE_REL);
			}
			wait_event(
					vinstr_ctx->waitq,
					atomic_read(
						&vinstr_ctx->request_pending) ||
					kthread_should_stop());
			hrtimer_cancel(&timer.hrtimer);
			continue;
		}

		kbasep_vinstr_collect_and_accumulate(vinstr_ctx, &timestamp);

		INIT_LIST_HEAD(&expired_requests);

		/* Find all expired requests. */
		list_for_each_entry_safe(
				cli,
				tmp,
				&vinstr_ctx->waiting_clients,
				list) {
			s64 tdiff =
				(s64)(timestamp + DUMPING_RESOLUTION) -
				(s64)cli->dump_time;
			if (tdiff >= 0ll) {
				list_del(&cli->list);
				list_add(&cli->list, &expired_requests);
			} else {
				break;
			}
		}

		/* Fill data for each request found. */
		list_for_each_entry_safe(cli, tmp, &expired_requests, list) {
			/* Ensure that legacy buffer will not be used from
			 * this kthread context. */
			BUG_ON(0 == cli->buffer_count);
			/* Expect only periodically sampled clients. */
			BUG_ON(0 == cli->dump_interval);

			kbasep_vinstr_update_client(
					cli,
					timestamp,
					BASE_HWCNT_READER_EVENT_PERIODIC);

			/* Set new dumping time. Drop missed probing times. */
			do {
				cli->dump_time += cli->dump_interval;
			} while (cli->dump_time < timestamp);

			list_del(&cli->list);
			kbasep_vinstr_add_dump_request(
					cli,
					&vinstr_ctx->waiting_clients);
		}

		/* Reprogram counters set if required. */
		kbasep_vinstr_reprogram(vinstr_ctx);

		mutex_unlock(&vinstr_ctx->lock);
	}

	return 0;
}

/*****************************************************************************/

/**
 * kbasep_vinstr_hwcnt_reader_buffer_ready - check if client has ready buffers
 * @cli: pointer to vinstr client structure
 *
 * Return: non-zero if client has at least one dumping buffer filled that was
 *         not notified to user yet
 */
static int kbasep_vinstr_hwcnt_reader_buffer_ready(
		struct kbase_vinstr_client *cli)
{
	KBASE_DEBUG_ASSERT(cli);
	return atomic_read(&cli->write_idx) != atomic_read(&cli->meta_idx);
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_get_buffer - hwcnt reader's ioctl command
 * @cli:    pointer to vinstr client structure
 * @buffer: pointer to userspace buffer
 * @size:   size of buffer
 *
 * Return: zero on success
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_get_buffer(
		struct kbase_vinstr_client *cli, void __user *buffer,
		size_t size)
{
	unsigned int meta_idx = atomic_read(&cli->meta_idx);
	unsigned int idx = meta_idx % cli->buffer_count;

	struct kbase_hwcnt_reader_metadata *meta = &cli->dump_buffers_meta[idx];

	/* Metadata sanity check. */
	KBASE_DEBUG_ASSERT(idx == meta->buffer_idx);

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
 * kbasep_vinstr_hwcnt_reader_ioctl_put_buffer - hwcnt reader's ioctl command
 * @cli:    pointer to vinstr client structure
 * @buffer: pointer to userspace buffer
 * @size:   size of buffer
 *
 * Return: zero on success
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_put_buffer(
		struct kbase_vinstr_client *cli, void __user *buffer,
		size_t size)
{
	unsigned int read_idx = atomic_read(&cli->read_idx);
	unsigned int idx = read_idx % cli->buffer_count;

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
 * kbasep_vinstr_hwcnt_reader_ioctl_set_interval - hwcnt reader's ioctl command
 * @cli:      pointer to vinstr client structure
 * @interval: periodic dumping interval (disable periodic dumping if zero)
 *
 * Return: zero on success
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_set_interval(
		struct kbase_vinstr_client *cli, u32 interval)
{
	struct kbase_vinstr_context *vinstr_ctx = cli->vinstr_ctx;

	KBASE_DEBUG_ASSERT(vinstr_ctx);

	mutex_lock(&vinstr_ctx->lock);

	if (vinstr_ctx->suspended) {
		mutex_unlock(&vinstr_ctx->lock);
		return -EBUSY;
	}

	list_del(&cli->list);

	cli->dump_interval = interval;

	/* If interval is non-zero, enable periodic dumping for this client. */
	if (cli->dump_interval) {
		if (DUMPING_RESOLUTION > cli->dump_interval)
			cli->dump_interval = DUMPING_RESOLUTION;
		cli->dump_time =
			kbasep_vinstr_get_timestamp() + cli->dump_interval;

		kbasep_vinstr_add_dump_request(
				cli, &vinstr_ctx->waiting_clients);

		atomic_set(&vinstr_ctx->request_pending, 1);
		wake_up_all(&vinstr_ctx->waitq);
	} else {
		list_add(&cli->list, &vinstr_ctx->idle_clients);
	}

	mutex_unlock(&vinstr_ctx->lock);

	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_event_mask - return event mask for event id
 * @event_id: id of event
 * Return: event_mask or zero if event is not supported or maskable
 */
static u32 kbasep_vinstr_hwcnt_reader_event_mask(
		enum base_hwcnt_reader_event event_id)
{
	u32 event_mask = 0;

	switch (event_id) {
	case BASE_HWCNT_READER_EVENT_PREJOB:
	case BASE_HWCNT_READER_EVENT_POSTJOB:
		/* These event are maskable. */
		event_mask = (1 << event_id);
		break;

	case BASE_HWCNT_READER_EVENT_MANUAL:
	case BASE_HWCNT_READER_EVENT_PERIODIC:
		/* These event are non-maskable. */
	default:
		/* These event are not supported. */
		break;
	}

	return event_mask;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_enable_event - hwcnt reader's ioctl command
 * @cli:      pointer to vinstr client structure
 * @event_id: id of event to enable
 *
 * Return: zero on success
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_enable_event(
		struct kbase_vinstr_client *cli,
		enum base_hwcnt_reader_event event_id)
{
	struct kbase_vinstr_context *vinstr_ctx = cli->vinstr_ctx;
	u32                         event_mask;

	KBASE_DEBUG_ASSERT(vinstr_ctx);

	event_mask = kbasep_vinstr_hwcnt_reader_event_mask(event_id);
	if (!event_mask)
		return -EINVAL;

	mutex_lock(&vinstr_ctx->lock);
	cli->event_mask |= event_mask;
	mutex_unlock(&vinstr_ctx->lock);

	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_disable_event - hwcnt reader's ioctl command
 * @cli:      pointer to vinstr client structure
 * @event_id: id of event to disable
 *
 * Return: zero on success
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_disable_event(
		struct kbase_vinstr_client *cli,
		enum base_hwcnt_reader_event event_id)
{
	struct kbase_vinstr_context *vinstr_ctx = cli->vinstr_ctx;
	u32                         event_mask;

	KBASE_DEBUG_ASSERT(vinstr_ctx);

	event_mask = kbasep_vinstr_hwcnt_reader_event_mask(event_id);
	if (!event_mask)
		return -EINVAL;

	mutex_lock(&vinstr_ctx->lock);
	cli->event_mask &= ~event_mask;
	mutex_unlock(&vinstr_ctx->lock);

	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl_get_hwver - hwcnt reader's ioctl command
 * @cli:   pointer to vinstr client structure
 * @hwver: pointer to user buffer where hw version will be stored
 *
 * Return: zero on success
 */
static long kbasep_vinstr_hwcnt_reader_ioctl_get_hwver(
		struct kbase_vinstr_client *cli, u32 __user *hwver)
{
	struct kbase_vinstr_context *vinstr_ctx = cli->vinstr_ctx;
	u32                         ver;

	KBASE_DEBUG_ASSERT(vinstr_ctx);

	ver = 4;
	if (!kbase_hw_has_feature(vinstr_ctx->kbdev, BASE_HW_FEATURE_V4))
		ver = 5;
	return put_user(ver, hwver);
}

/**
 * kbasep_vinstr_hwcnt_reader_ioctl - hwcnt reader's ioctl
 * @filp:   pointer to file structure
 * @cmd:    user command
 * @arg:    command's argument
 *
 * Return: zero on success
 */
static long kbasep_vinstr_hwcnt_reader_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	long                       rcode = 0;
	struct kbase_vinstr_client *cli;

	KBASE_DEBUG_ASSERT(filp);

	cli = filp->private_data;
	KBASE_DEBUG_ASSERT(cli);

	if (unlikely(KBASE_HWCNT_READER != _IOC_TYPE(cmd)))
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
		KBASE_DEBUG_ASSERT(cli->vinstr_ctx);
		rcode = put_user(
				(u32)cli->vinstr_ctx->dump_size,
				(u32 __user *)arg);
		break;
	case KBASE_HWCNT_READER_DUMP:
		rcode = kbase_vinstr_hwc_dump(
				cli, BASE_HWCNT_READER_EVENT_MANUAL);
		break;
	case KBASE_HWCNT_READER_CLEAR:
		rcode = kbase_vinstr_hwc_clear(cli);
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
		rcode = -EINVAL;
		break;
	}

	return rcode;
}

/**
 * kbasep_vinstr_hwcnt_reader_poll - hwcnt reader's poll
 * @filp: pointer to file structure
 * @wait: pointer to poll table
 * Return: POLLIN if data can be read without blocking, otherwise zero
 */
static unsigned int kbasep_vinstr_hwcnt_reader_poll(struct file *filp,
		poll_table *wait)
{
	struct kbase_vinstr_client *cli;

	KBASE_DEBUG_ASSERT(filp);
	KBASE_DEBUG_ASSERT(wait);

	cli = filp->private_data;
	KBASE_DEBUG_ASSERT(cli);

	poll_wait(filp, &cli->waitq, wait);
	if (kbasep_vinstr_hwcnt_reader_buffer_ready(cli))
		return POLLIN;
	return 0;
}

/**
 * kbasep_vinstr_hwcnt_reader_mmap - hwcnt reader's mmap
 * @filp: pointer to file structure
 * @vma:  pointer to vma structure
 * Return: zero on success
 */
static int kbasep_vinstr_hwcnt_reader_mmap(struct file *filp,
		struct vm_area_struct *vma)
{
	struct kbase_vinstr_client *cli;
	size_t                     size;

	KBASE_DEBUG_ASSERT(filp);
	KBASE_DEBUG_ASSERT(vma);

	cli = filp->private_data;
	KBASE_DEBUG_ASSERT(cli);

	size = cli->buffer_count * cli->dump_size;
	if (vma->vm_end - vma->vm_start > size)
		return -ENOMEM;

	return remap_pfn_range(
			vma,
			vma->vm_start,
			__pa((unsigned long)cli->dump_buffers) >> PAGE_SHIFT,
			size,
			vma->vm_page_prot);
}

/**
 * kbasep_vinstr_hwcnt_reader_release - hwcnt reader's release
 * @inode: pointer to inode structure
 * @filp:  pointer to file structure
 * Return always return zero
 */
static int kbasep_vinstr_hwcnt_reader_release(struct inode *inode,
		struct file *filp)
{
	struct kbase_vinstr_client *cli;

	KBASE_DEBUG_ASSERT(inode);
	KBASE_DEBUG_ASSERT(filp);

	cli = filp->private_data;
	KBASE_DEBUG_ASSERT(cli);

	kbasep_vinstr_detach_client(cli);
	return 0;
}

/*****************************************************************************/

struct kbase_vinstr_context *kbase_vinstr_init(struct kbase_device *kbdev)
{
	struct kbase_vinstr_context *vinstr_ctx;

	vinstr_ctx = kzalloc(sizeof(*vinstr_ctx), GFP_KERNEL);
	if (!vinstr_ctx)
		return NULL;

	INIT_LIST_HEAD(&vinstr_ctx->idle_clients);
	INIT_LIST_HEAD(&vinstr_ctx->waiting_clients);
	mutex_init(&vinstr_ctx->lock);
	vinstr_ctx->kbdev = kbdev;
	vinstr_ctx->thread = NULL;

	atomic_set(&vinstr_ctx->request_pending, 0);
	init_waitqueue_head(&vinstr_ctx->waitq);

	return vinstr_ctx;
}

void kbase_vinstr_term(struct kbase_vinstr_context *vinstr_ctx)
{
	struct kbase_vinstr_client *cli;

	/* Stop service thread first. */
	if (vinstr_ctx->thread)
		kthread_stop(vinstr_ctx->thread);

	while (1) {
		struct list_head *list = &vinstr_ctx->idle_clients;

		if (list_empty(list)) {
			list = &vinstr_ctx->waiting_clients;
			if (list_empty(list))
				break;
		}

		cli = list_first_entry(list, struct kbase_vinstr_client, list);
		list_del(&cli->list);
		kfree(cli->accum_buffer);
		kfree(cli);
		vinstr_ctx->nclients--;
	}
	KBASE_DEBUG_ASSERT(!vinstr_ctx->nclients);
	if (vinstr_ctx->kctx)
		kbasep_vinstr_destroy_kctx(vinstr_ctx);
	kfree(vinstr_ctx);
}

int kbase_vinstr_hwcnt_reader_setup(struct kbase_vinstr_context *vinstr_ctx,
		struct kbase_uk_hwcnt_reader_setup *setup)
{
	struct kbase_vinstr_client  *cli;
	u32                         bitmap[4];

	KBASE_DEBUG_ASSERT(vinstr_ctx);
	KBASE_DEBUG_ASSERT(setup);
	KBASE_DEBUG_ASSERT(setup->buffer_count);

	bitmap[SHADER_HWCNT_BM] = setup->shader_bm;
	bitmap[TILER_HWCNT_BM]  = setup->tiler_bm;
	bitmap[MMU_L2_HWCNT_BM] = setup->mmu_l2_bm;
	bitmap[JM_HWCNT_BM]     = setup->jm_bm;

	cli = kbasep_vinstr_attach_client(
			vinstr_ctx,
			setup->buffer_count,
			bitmap,
			&setup->fd);

	if (!cli)
		return -ENOMEM;

	return 0;
}

int kbase_vinstr_legacy_hwc_setup(
		struct kbase_vinstr_context *vinstr_ctx,
		struct kbase_vinstr_client  **cli,
		struct kbase_uk_hwcnt_setup *setup)
{
	KBASE_DEBUG_ASSERT(vinstr_ctx);
	KBASE_DEBUG_ASSERT(setup);
	KBASE_DEBUG_ASSERT(cli);

	if (setup->dump_buffer) {
		u32 bitmap[4];

		bitmap[SHADER_HWCNT_BM] = setup->shader_bm;
		bitmap[TILER_HWCNT_BM]  = setup->tiler_bm;
		bitmap[MMU_L2_HWCNT_BM] = setup->mmu_l2_bm;
		bitmap[JM_HWCNT_BM]     = setup->jm_bm;

		if (*cli)
			return -EBUSY;

		*cli = kbasep_vinstr_attach_client(
				vinstr_ctx,
				0,
				bitmap,
				(void *)(long)setup->dump_buffer);

		if (!(*cli))
			return -ENOMEM;
	} else {
		if (!*cli)
			return -EINVAL;

		kbasep_vinstr_detach_client(*cli);
		*cli = NULL;
	}

	return 0;
}

int kbase_vinstr_hwc_dump(struct kbase_vinstr_client *cli,
		enum base_hwcnt_reader_event event_id)
{
	int                         rcode = 0;
	struct kbase_vinstr_context *vinstr_ctx;
	u64                         timestamp;
	u32                         event_mask;

	if (!cli)
		return -EINVAL;

	vinstr_ctx = cli->vinstr_ctx;
	KBASE_DEBUG_ASSERT(vinstr_ctx);

	KBASE_DEBUG_ASSERT(event_id < BASE_HWCNT_READER_EVENT_COUNT);
	event_mask = 1 << event_id;

	mutex_lock(&vinstr_ctx->lock);

	if (vinstr_ctx->suspended) {
		rcode = -EBUSY;
		goto exit;
	}

	if (event_mask & cli->event_mask) {
		rcode = kbasep_vinstr_collect_and_accumulate(
				vinstr_ctx,
				&timestamp);
		if (rcode)
			goto exit;

		rcode = kbasep_vinstr_update_client(cli, timestamp, event_id);
		if (rcode)
			goto exit;

		kbasep_vinstr_reprogram(vinstr_ctx);
	}

exit:
	mutex_unlock(&vinstr_ctx->lock);

	return rcode;
}

int kbase_vinstr_hwc_clear(struct kbase_vinstr_client *cli)
{
	struct kbase_vinstr_context *vinstr_ctx;
	int                         rcode;
	u64                         unused;

	if (!cli)
		return -EINVAL;

	vinstr_ctx = cli->vinstr_ctx;
	KBASE_DEBUG_ASSERT(vinstr_ctx);

	mutex_lock(&vinstr_ctx->lock);

	if (vinstr_ctx->suspended) {
		rcode = -EBUSY;
		goto exit;
	}

	rcode = kbasep_vinstr_collect_and_accumulate(vinstr_ctx, &unused);
	if (rcode)
		goto exit;
	rcode = kbase_instr_hwcnt_clear(vinstr_ctx->kctx);
	if (rcode)
		goto exit;
	memset(cli->accum_buffer, 0, cli->dump_size);

	kbasep_vinstr_reprogram(vinstr_ctx);

exit:
	mutex_unlock(&vinstr_ctx->lock);

	return rcode;
}

void kbase_vinstr_hwc_suspend(struct kbase_vinstr_context *vinstr_ctx)
{
	u64 unused;

	KBASE_DEBUG_ASSERT(vinstr_ctx);

	mutex_lock(&vinstr_ctx->lock);
	kbasep_vinstr_collect_and_accumulate(vinstr_ctx, &unused);
	vinstr_ctx->suspended = true;
	vinstr_ctx->suspended_clients = vinstr_ctx->waiting_clients;
	INIT_LIST_HEAD(&vinstr_ctx->waiting_clients);
	mutex_unlock(&vinstr_ctx->lock);
}

void kbase_vinstr_hwc_resume(struct kbase_vinstr_context *vinstr_ctx)
{
	KBASE_DEBUG_ASSERT(vinstr_ctx);

	mutex_lock(&vinstr_ctx->lock);
	vinstr_ctx->suspended = false;
	vinstr_ctx->waiting_clients = vinstr_ctx->suspended_clients;
	vinstr_ctx->reprogram = true;
	kbasep_vinstr_reprogram(vinstr_ctx);
	atomic_set(&vinstr_ctx->request_pending, 1);
	wake_up_all(&vinstr_ctx->waitq);
	mutex_unlock(&vinstr_ctx->lock);
}
