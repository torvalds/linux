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



#include <mali_kbase.h>
#include <mali_kbase_mem_linux.h>

#define NR_CNT_BLOCKS_PER_GROUP 8
#define NR_CNT_PER_BLOCK        64
#define NR_BYTES_PER_CNT        4
#define NR_BYTES_PER_HDR        16
#define PRFCNT_EN_MASK_OFFSET   0x8

struct kbase_vinstr_context {
	struct kbase_device *kbdev;
	struct kbase_context *kctx;
	struct kbase_vmap_struct vmap;
	struct mutex lock;
	u64 gpu_va;
	void *cpu_va;
	size_t dump_size;
	u32 nclients;
	struct list_head clients;
};

struct kbase_vinstr_client {
	bool kernel;
	void *dump_buffer;
	u32 bitmap[4];
	void *accum_buffer;
	size_t dump_size;
	struct list_head list;
};

static int map_kernel_dump_buffer(struct kbase_vinstr_context *ctx)
{
	struct kbase_va_region *reg;
	struct kbase_context *kctx = ctx->kctx;
	u64 flags, nr_pages;
	u16 va_align = 0;

	flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_GPU_WR;
	ctx->dump_size = kbase_vinstr_dump_size(ctx);
	nr_pages = PFN_UP(ctx->dump_size);

	reg = kbase_mem_alloc(kctx, nr_pages, nr_pages, 0, &flags,
			&ctx->gpu_va, &va_align);
	if (!reg)
		return -ENOMEM;

	ctx->cpu_va = kbase_vmap(kctx, ctx->gpu_va, ctx->dump_size, &ctx->vmap);
	if (!ctx->cpu_va) {
		kbase_mem_free(kctx, ctx->gpu_va);
		return -ENOMEM;
	}

	return 0;
}

static void unmap_kernel_dump_buffer(struct kbase_vinstr_context *ctx)
{
	struct kbase_context *kctx = ctx->kctx;

	kbase_vunmap(kctx, &ctx->vmap);
	kbase_mem_free(kctx, ctx->gpu_va);
}

static int map_client_accum_buffer(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli)
{
	cli->dump_size = kbase_vinstr_dump_size(ctx);
	cli->accum_buffer = kzalloc(cli->dump_size, GFP_KERNEL);
	return !cli->accum_buffer ? -ENOMEM : 0;
}

static void unmap_client_accum_buffer(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli)
{
	kfree(cli->accum_buffer);
}

static int create_vinstr_kctx(struct kbase_vinstr_context *ctx)
{
	struct kbase_uk_hwcnt_setup setup;
	int err;

	ctx->kctx = kbase_create_context(ctx->kbdev, true);
	if (!ctx->kctx)
		return -ENOMEM;

	/* Map the master kernel dump buffer.  The HW dumps the counters
	 * into this memory region. */
	err = map_kernel_dump_buffer(ctx);
	if (err) {
		kbase_destroy_context(ctx->kctx);
		return err;
	}

	setup.dump_buffer = ctx->gpu_va;
	/* The GPU requires us to disable the prfcnt collection block for
	 * reprogramming it.  This introduces jitter and disrupts existing
	 * clients.  Therefore we enable all of them. */
	setup.jm_bm = 0xffffffff;
	setup.tiler_bm = 0xffffffff;
	setup.shader_bm = 0xffffffff;
	setup.mmu_l2_bm = 0xffffffff;

	err = kbase_instr_hwcnt_enable(ctx->kctx, &setup);
	if (err) {
		unmap_kernel_dump_buffer(ctx);
		kbase_destroy_context(ctx->kctx);
		return err;
	}

	return 0;
}

static void destroy_vinstr_kctx(struct kbase_vinstr_context *ctx)
{
	kbase_instr_hwcnt_disable(ctx->kctx);
	unmap_kernel_dump_buffer(ctx);
	kbase_destroy_context(ctx->kctx);
	ctx->kctx = NULL;
}

struct kbase_vinstr_context *kbase_vinstr_init(struct kbase_device *kbdev)
{
	struct kbase_vinstr_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;
	INIT_LIST_HEAD(&ctx->clients);
	mutex_init(&ctx->lock);
	ctx->kbdev = kbdev;
	return ctx;
}

void kbase_vinstr_term(struct kbase_vinstr_context *ctx)
{
	struct kbase_vinstr_client *cli;

	while (!list_empty(&ctx->clients)) {
		cli = list_first_entry(&ctx->clients,
				struct kbase_vinstr_client, list);
		list_del(&cli->list);
		unmap_client_accum_buffer(ctx, cli);
		kfree(cli);
		ctx->nclients--;
	}
	if (ctx->kctx)
		destroy_vinstr_kctx(ctx);
	kfree(ctx);
}

struct kbase_vinstr_client *kbase_vinstr_attach_client(struct kbase_vinstr_context *ctx,
		bool kernel, u64 dump_buffer, u32 bitmap[4])
{
	struct kbase_vinstr_client *cli;

	cli = kmalloc(sizeof(*cli), GFP_KERNEL);
	if (!cli)
		return NULL;

	cli->kernel = kernel;
	cli->dump_buffer = (void *)(uintptr_t)dump_buffer;
	cli->bitmap[SHADER_HWCNT_BM] = bitmap[SHADER_HWCNT_BM];
	cli->bitmap[TILER_HWCNT_BM] = bitmap[TILER_HWCNT_BM];
	cli->bitmap[MMU_L2_HWCNT_BM] = bitmap[MMU_L2_HWCNT_BM];
	cli->bitmap[JM_HWCNT_BM] = bitmap[JM_HWCNT_BM];

	mutex_lock(&ctx->lock);
	/* If this is the first client, create the vinstr kbase
	 * context.  This context is permanently resident until the
	 * last client exits. */
	if (!ctx->nclients) {
		if (create_vinstr_kctx(ctx) < 0) {
			kfree(cli);
			mutex_unlock(&ctx->lock);
			return NULL;
		}
	}

	/* The GPU resets the counter block every time there is a request
	 * to dump it.  We need a per client kernel buffer for accumulating
	 * the counters. */
	if (map_client_accum_buffer(ctx, cli) < 0) {
		kfree(cli);
		if (!ctx->nclients)
			destroy_vinstr_kctx(ctx);
		mutex_unlock(&ctx->lock);
		return NULL;
	}

	ctx->nclients++;
	list_add(&cli->list, &ctx->clients);
	mutex_unlock(&ctx->lock);

	return cli;
}

void kbase_vinstr_detach_client(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli)
{
	struct kbase_vinstr_client *iter, *tmp;

	mutex_lock(&ctx->lock);
	list_for_each_entry_safe(iter, tmp, &ctx->clients, list) {
		if (iter == cli) {
			list_del(&iter->list);
			unmap_client_accum_buffer(ctx, cli);
			kfree(iter);
			ctx->nclients--;
			if (!ctx->nclients)
				destroy_vinstr_kctx(ctx);
			break;
		}
	}
	mutex_unlock(&ctx->lock);
}

size_t kbase_vinstr_dump_size(struct kbase_vinstr_context *ctx)
{
	struct kbase_device *kbdev = ctx->kctx->kbdev;
	size_t dump_size;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_V4)) {
		u32 nr_cg;

		nr_cg = kbdev->gpu_props.num_core_groups;
		dump_size = nr_cg * NR_CNT_BLOCKS_PER_GROUP *
				NR_CNT_PER_BLOCK *
				NR_BYTES_PER_CNT;
	} else {
		/* assume v5 for now */
		u32 nr_l2, nr_sc;

		nr_l2 = kbdev->gpu_props.props.l2_props.num_l2_slices;
		nr_sc = kbdev->gpu_props.props.coherency_info.group[0].num_cores;
		/* JM and tiler counter blocks are always present */
		dump_size = (2 + nr_l2 + nr_sc) *
				NR_CNT_PER_BLOCK *
				NR_BYTES_PER_CNT;
	}
	return dump_size;
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
static void patch_dump_buffer_hdr_v4(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli)
{
	u32 *mask;
	u8 *dst = cli->accum_buffer;
	u8 *src = ctx->cpu_va;
	u32 nr_cg = ctx->kctx->kbdev->gpu_props.num_core_groups;
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
static void patch_dump_buffer_hdr_v5(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli)
{
	struct kbase_device *kbdev = ctx->kctx->kbdev;
	u32 i, nr_l2, nr_sc;
	u32 *mask;
	u8 *dst = cli->accum_buffer;
	u8 *src = ctx->cpu_va;
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
	nr_sc = kbdev->gpu_props.props.coherency_info.group[0].num_cores;
	for (i = 0; i < nr_sc; i++) {
		memcpy(dst, src, NR_BYTES_PER_HDR);
		mask = (u32 *)&dst[PRFCNT_EN_MASK_OFFSET];
		*mask &= cli->bitmap[SHADER_HWCNT_BM];
		dst += block_size;
		src += block_size;
	}
}

static void accum_clients(struct kbase_vinstr_context *ctx)
{
	struct kbase_vinstr_client *iter;
	int v4;

	v4 = kbase_hw_has_feature(ctx->kbdev, BASE_HW_FEATURE_V4);
	list_for_each_entry(iter, &ctx->clients, list) {
		if (v4)
			patch_dump_buffer_hdr_v4(ctx, iter);
		else
			patch_dump_buffer_hdr_v5(ctx, iter);
		accum_dump_buffer(iter->accum_buffer, ctx->cpu_va,
				iter->dump_size);
	}
}

int kbase_vinstr_dump(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli)
{
	int err = 0;

	if (!cli)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	err = kbase_instr_hwcnt_request_dump(ctx->kctx);
	if (err)
		goto out;

	err = kbase_instr_hwcnt_wait_for_dump(ctx->kctx);
	if (err)
		goto out;

	accum_clients(ctx);

	if (!cli->kernel) {
		if (copy_to_user((void __user *)cli->dump_buffer,
				cli->accum_buffer, cli->dump_size)) {
			err = -EFAULT;
			goto out;
		}
	} else {
		memcpy(cli->dump_buffer, cli->accum_buffer, cli->dump_size);
	}

	memset(cli->accum_buffer, 0, cli->dump_size);
out:
	mutex_unlock(&ctx->lock);
	return err;
}

int kbase_vinstr_clear(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli)
{
	int err = 0;

	if (!cli)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	err = kbase_instr_hwcnt_request_dump(ctx->kctx);
	if (err)
		goto out;

	err = kbase_instr_hwcnt_wait_for_dump(ctx->kctx);
	if (err)
		goto out;

	err = kbase_instr_hwcnt_clear(ctx->kctx);
	if (err)
		goto out;

	accum_clients(ctx);

	memset(cli->accum_buffer, 0, cli->dump_size);
out:
	mutex_unlock(&ctx->lock);
	return err;
}
