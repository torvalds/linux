/*
 * drivers/video/tegra/host/nvhost_3dctx.c
 *
 * Tegra Graphics Host 3d hardware context
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "nvhost_hwctx.h"
#include "dev.h"

#include <linux/slab.h>

const struct hwctx_reginfo ctxsave_regs_3d[] = {
	HWCTX_REGINFO(0xe00, 16, DIRECT),
	HWCTX_REGINFO(0xe10, 16, DIRECT),
	HWCTX_REGINFO(0xe20, 1, DIRECT),
	HWCTX_REGINFO(0xe21, 1, DIRECT),
	HWCTX_REGINFO(0xe22, 1, DIRECT),
	HWCTX_REGINFO(0xe25, 1, DIRECT),
	HWCTX_REGINFO(0xe26, 1, DIRECT),
	HWCTX_REGINFO(0xe28, 2, DIRECT),
	HWCTX_REGINFO(0xe2a, 1, DIRECT),
	HWCTX_REGINFO(0x1, 1, DIRECT),
	HWCTX_REGINFO(0x2, 1, DIRECT),
	HWCTX_REGINFO(0xc, 2, DIRECT),
	HWCTX_REGINFO(0xe, 2, DIRECT),
	HWCTX_REGINFO(0x10, 2, DIRECT),
	HWCTX_REGINFO(0x12, 2, DIRECT),
	HWCTX_REGINFO(0x14, 2, DIRECT),
	HWCTX_REGINFO(0x100, 32, DIRECT),
	HWCTX_REGINFO(0x120, 1, DIRECT),
	HWCTX_REGINFO(0x121, 1, DIRECT),
	HWCTX_REGINFO(0x124, 1, DIRECT),
	HWCTX_REGINFO(0x125, 1, DIRECT),
	HWCTX_REGINFO(0x200, 1, DIRECT),
	HWCTX_REGINFO(0x201, 1, DIRECT),
	HWCTX_REGINFO(0x202, 1, DIRECT),
	HWCTX_REGINFO(0x203, 1, DIRECT),
	HWCTX_REGINFO(0x204, 1, DIRECT),
	HWCTX_REGINFO(0x207, 1024, INDIRECT),
	HWCTX_REGINFO(0x209, 1, DIRECT),
	HWCTX_REGINFO(0x300, 64, DIRECT),
	HWCTX_REGINFO(0x343, 1, DIRECT),
	HWCTX_REGINFO(0x344, 1, DIRECT),
	HWCTX_REGINFO(0x345, 1, DIRECT),
	HWCTX_REGINFO(0x346, 1, DIRECT),
	HWCTX_REGINFO(0x347, 1, DIRECT),
	HWCTX_REGINFO(0x348, 1, DIRECT),
	HWCTX_REGINFO(0x349, 1, DIRECT),
	HWCTX_REGINFO(0x34a, 1, DIRECT),
	HWCTX_REGINFO(0x34b, 1, DIRECT),
	HWCTX_REGINFO(0x34c, 1, DIRECT),
	HWCTX_REGINFO(0x34d, 1, DIRECT),
	HWCTX_REGINFO(0x34e, 1, DIRECT),
	HWCTX_REGINFO(0x34f, 1, DIRECT),
	HWCTX_REGINFO(0x350, 1, DIRECT),
	HWCTX_REGINFO(0x351, 1, DIRECT),
	HWCTX_REGINFO(0x352, 1, DIRECT),
	HWCTX_REGINFO(0x353, 1, DIRECT),
	HWCTX_REGINFO(0x354, 1, DIRECT),
	HWCTX_REGINFO(0x355, 1, DIRECT),
	HWCTX_REGINFO(0x356, 1, DIRECT),
	HWCTX_REGINFO(0x357, 1, DIRECT),
	HWCTX_REGINFO(0x358, 1, DIRECT),
	HWCTX_REGINFO(0x359, 1, DIRECT),
	HWCTX_REGINFO(0x35a, 1, DIRECT),
	HWCTX_REGINFO(0x35b, 1, DIRECT),
	HWCTX_REGINFO(0x363, 1, DIRECT),
	HWCTX_REGINFO(0x364, 1, DIRECT),
	HWCTX_REGINFO(0x400, 2, DIRECT),
	HWCTX_REGINFO(0x402, 1, DIRECT),
	HWCTX_REGINFO(0x403, 1, DIRECT),
	HWCTX_REGINFO(0x404, 1, DIRECT),
	HWCTX_REGINFO(0x405, 1, DIRECT),
	HWCTX_REGINFO(0x406, 1, DIRECT),
	HWCTX_REGINFO(0x407, 1, DIRECT),
	HWCTX_REGINFO(0x408, 1, DIRECT),
	HWCTX_REGINFO(0x409, 1, DIRECT),
	HWCTX_REGINFO(0x40a, 1, DIRECT),
	HWCTX_REGINFO(0x40b, 1, DIRECT),
	HWCTX_REGINFO(0x40c, 1, DIRECT),
	HWCTX_REGINFO(0x40d, 1, DIRECT),
	HWCTX_REGINFO(0x40e, 1, DIRECT),
	HWCTX_REGINFO(0x40f, 1, DIRECT),
	HWCTX_REGINFO(0x411, 1, DIRECT),
	HWCTX_REGINFO(0x500, 1, DIRECT),
	HWCTX_REGINFO(0x501, 1, DIRECT),
	HWCTX_REGINFO(0x502, 1, DIRECT),
	HWCTX_REGINFO(0x503, 1, DIRECT),
	HWCTX_REGINFO(0x520, 32, DIRECT),
	HWCTX_REGINFO(0x540, 64, INDIRECT),
	HWCTX_REGINFO(0x600, 0, INDIRECT_OFFSET),
	HWCTX_REGINFO(0x602, 16, INDIRECT_DATA),
	HWCTX_REGINFO(0x603, 128, INDIRECT),
	HWCTX_REGINFO(0x608, 4, DIRECT),
	HWCTX_REGINFO(0x60e, 1, DIRECT),
	HWCTX_REGINFO(0x700, 64, INDIRECT),
	HWCTX_REGINFO(0x710, 16, DIRECT),
	HWCTX_REGINFO(0x720, 32, DIRECT),
	HWCTX_REGINFO(0x740, 1, DIRECT),
	HWCTX_REGINFO(0x741, 1, DIRECT),
	HWCTX_REGINFO(0x800, 0, INDIRECT_OFFSET),
	HWCTX_REGINFO(0x802, 16, INDIRECT_DATA),
	HWCTX_REGINFO(0x803, 512, INDIRECT),
	HWCTX_REGINFO(0x805, 64, INDIRECT),
	HWCTX_REGINFO(0x820, 32, DIRECT),
	HWCTX_REGINFO(0x900, 64, INDIRECT),
	HWCTX_REGINFO(0x902, 1, DIRECT),
	HWCTX_REGINFO(0x903, 1, DIRECT),
	HWCTX_REGINFO(0xa02, 1, DIRECT),
	HWCTX_REGINFO(0xa03, 1, DIRECT),
	HWCTX_REGINFO(0xa04, 1, DIRECT),
	HWCTX_REGINFO(0xa05, 1, DIRECT),
	HWCTX_REGINFO(0xa06, 1, DIRECT),
	HWCTX_REGINFO(0xa07, 1, DIRECT),
	HWCTX_REGINFO(0xa08, 1, DIRECT),
	HWCTX_REGINFO(0xa09, 1, DIRECT),
	HWCTX_REGINFO(0xa0a, 1, DIRECT),
	HWCTX_REGINFO(0xa0b, 1, DIRECT),
	HWCTX_REGINFO(0x205, 1024, INDIRECT)
};


/*** restore ***/

static unsigned int context_restore_size = 0;

static void restore_begin(u32 *ptr, u32 waitbase)
{
	/* set class to host */
	ptr[0] = nvhost_opcode_setclass(NV_HOST1X_CLASS_ID,
					NV_CLASS_HOST_INCR_SYNCPT_BASE, 1);
	/* increment sync point base */
	ptr[1] = nvhost_class_host_incr_syncpt_base(waitbase, 1);
	/* set class to 3D */
	ptr[2] = nvhost_opcode_setclass(NV_GRAPHICS_3D_CLASS_ID, 0, 0);
	/* program PSEQ_QUAD_ID */
	ptr[3] = nvhost_opcode_imm(0x545, 0);
}
#define RESTORE_BEGIN_SIZE 4

static void restore_end(u32 *ptr, u32 syncpt_id)
{
	/* syncpt increment to track restore gather. */
	ptr[0] = nvhost_opcode_imm(0x0, ((1UL << 8) | (u8)(syncpt_id & 0xff)));
}
#define RESTORE_END_SIZE 1

static void restore_direct(u32 *ptr, u32 start_reg, u32 count)
{
	ptr[0] = nvhost_opcode_incr(start_reg, count);
}
#define RESTORE_DIRECT_SIZE 1

static void restore_indoffset(u32 *ptr, u32 offset_reg, u32 offset)
{
	ptr[0] = nvhost_opcode_imm(offset_reg, offset);
}
#define RESTORE_INDOFFSET_SIZE 1

static void restore_inddata(u32 *ptr, u32 data_reg, u32 count)
{
	ptr[0] = nvhost_opcode_nonincr(data_reg, count);
}
#define RESTORE_INDDATA_SIZE 1

static void restore_registers_from_fifo(u32 *ptr, unsigned int count,
					struct nvhost_channel *channel,
					unsigned int *pending)
{
	void __iomem *chan_regs = channel->aperture;
	unsigned int entries = *pending;
	while (count) {
		unsigned int num;

		while (!entries) {
			/* query host for number of entries in fifo */
			entries = nvhost_channel_fifostat_outfentries(
				readl(chan_regs + HOST1X_CHANNEL_FIFOSTAT));
			if (!entries)
				cpu_relax();
			/* TODO: [ahowe 2010-06-14] timeout */
		}
		num = min(entries, count);
		entries -= num;
		count -= num;

		while (num & ~0x3) {
			u32 arr[4];
			arr[0] = readl(chan_regs + HOST1X_CHANNEL_INDDATA);
			arr[1] = readl(chan_regs + HOST1X_CHANNEL_INDDATA);
			arr[2] = readl(chan_regs + HOST1X_CHANNEL_INDDATA);
			arr[3] = readl(chan_regs + HOST1X_CHANNEL_INDDATA);
			memcpy(ptr, arr, 4*sizeof(u32));
			ptr += 4;
			num -= 4;
		}
		while (num--)
			*ptr++ = readl(chan_regs + HOST1X_CHANNEL_INDDATA);
	}
	*pending = entries;
}

static void setup_restore(u32 *ptr, u32 waitbase)
{
	const struct hwctx_reginfo *r;
	const struct hwctx_reginfo *rend;

	restore_begin(ptr, waitbase);
	ptr += RESTORE_BEGIN_SIZE;

	r = ctxsave_regs_3d;
	rend = ctxsave_regs_3d + ARRAY_SIZE(ctxsave_regs_3d);
	for ( ; r != rend; ++r) {
		u32 offset = r->offset;
		u32 count = r->count;
		switch (r->type) {
		case HWCTX_REGINFO_DIRECT:
			restore_direct(ptr, offset, count);
			ptr += RESTORE_DIRECT_SIZE;
			break;
		case HWCTX_REGINFO_INDIRECT:
			restore_indoffset(ptr, offset, 0);
			ptr += RESTORE_INDOFFSET_SIZE;
			restore_inddata(ptr, offset + 1, count);
			ptr += RESTORE_INDDATA_SIZE;
			break;
		case HWCTX_REGINFO_INDIRECT_OFFSET:
			restore_indoffset(ptr, offset, count);
			ptr += RESTORE_INDOFFSET_SIZE;
			continue; /* INDIRECT_DATA follows with real count */
		case HWCTX_REGINFO_INDIRECT_DATA:
			restore_inddata(ptr, offset, count);
			ptr += RESTORE_INDDATA_SIZE;
			break;
		}
		ptr += count;
	}

	restore_end(ptr, NVSYNCPT_3D);
	wmb();
}

/*** save ***/

/* the same context save command sequence is used for all contexts. */
static struct nvmap_handle_ref *context_save_buf = NULL;
static u32 context_save_phys = 0;
static u32 *context_save_ptr = NULL;
static unsigned int context_save_size = 0;

static void save_begin(u32 *ptr, u32 syncpt_id, u32 waitbase)
{
	/* set class to the unit to flush */
	ptr[0] = nvhost_opcode_setclass(NV_GRAPHICS_3D_CLASS_ID, 0, 0);
	/*
	 * Flush pipe and signal context read thread to start reading
	 * sync point increment
	 */
	ptr[1] = nvhost_opcode_imm(0, 0x100 | syncpt_id);
	ptr[2] = nvhost_opcode_setclass(NV_HOST1X_CLASS_ID,
					NV_CLASS_HOST_WAIT_SYNCPT_BASE, 1);
	/* wait for base+1 */
	ptr[3] = nvhost_class_host_wait_syncpt_base(syncpt_id, waitbase, 1);
	ptr[4] = nvhost_opcode_setclass(NV_GRAPHICS_3D_CLASS_ID, 0, 0);
	ptr[5] = nvhost_opcode_imm(0, syncpt_id);
	ptr[6] = nvhost_opcode_setclass(NV_HOST1X_CLASS_ID, 0, 0);
}
#define SAVE_BEGIN_SIZE 7

static void save_direct(u32 *ptr, u32 start_reg, u32 count)
{
	ptr[0] = nvhost_opcode_nonincr(NV_CLASS_HOST_INDOFF, 1);
	ptr[1] = nvhost_class_host_indoff_reg_read(NV_HOST_MODULE_GR3D,
						start_reg, true);
	ptr[2] = nvhost_opcode_nonincr(NV_CLASS_HOST_INDDATA, count);
}
#define SAVE_DIRECT_SIZE 3

static void save_indoffset(u32 *ptr, u32 offset_reg, u32 offset)
{
	ptr[0] = nvhost_opcode_nonincr(NV_CLASS_HOST_INDOFF, 1);
	ptr[1] = nvhost_class_host_indoff_reg_write(NV_HOST_MODULE_GR3D,
						offset_reg, true);
	ptr[2] = nvhost_opcode_nonincr(NV_CLASS_HOST_INDDATA, 1);
	ptr[3] = offset;
}
#define SAVE_INDOFFSET_SIZE 4

static inline void save_inddata(u32 *ptr, u32 data_reg, u32 count)
{
	ptr[0] = nvhost_opcode_nonincr(NV_CLASS_HOST_INDOFF, 1);
	ptr[1] = nvhost_class_host_indoff_reg_read(NV_HOST_MODULE_GR3D,
						data_reg, false);
	ptr[2] = nvhost_opcode_nonincr(NV_CLASS_HOST_INDDATA, count);
}
#define SAVE_INDDDATA_SIZE 3

static void save_end(u32 *ptr, u32 syncpt_id, u32 waitbase)
{
	/* Wait for context read service */
	ptr[0] = nvhost_opcode_nonincr(NV_CLASS_HOST_WAIT_SYNCPT_BASE, 1);
	ptr[1] = nvhost_class_host_wait_syncpt_base(syncpt_id, waitbase, 3);
	/* Increment syncpoint base */
	ptr[2] = nvhost_opcode_nonincr(NV_CLASS_HOST_INCR_SYNCPT_BASE, 1);
	ptr[3] = nvhost_class_host_incr_syncpt_base(waitbase, 3);
	/* set class back to the unit */
	ptr[4] = nvhost_opcode_setclass(NV_GRAPHICS_3D_CLASS_ID, 0, 0);
}
#define SAVE_END_SIZE 5

static void __init setup_save(
	u32 *ptr, unsigned int *words_save, unsigned int *words_restore,
	u32 syncpt_id, u32 waitbase)
{
	const struct hwctx_reginfo *r;
	const struct hwctx_reginfo *rend;
	unsigned int save = SAVE_BEGIN_SIZE + SAVE_END_SIZE;
	unsigned int restore = RESTORE_BEGIN_SIZE + RESTORE_END_SIZE;

	if (ptr) {
		save_begin(ptr, syncpt_id, waitbase);
		ptr += SAVE_BEGIN_SIZE;
	}

	r = ctxsave_regs_3d;
	rend = ctxsave_regs_3d + ARRAY_SIZE(ctxsave_regs_3d);
	for ( ; r != rend; ++r) {
		u32 offset = r->offset;
		u32 count = r->count;
		switch (r->type) {
		case HWCTX_REGINFO_DIRECT:
			if (ptr) {
				save_direct(ptr, offset, count);
				ptr += SAVE_DIRECT_SIZE;
			}
			save += SAVE_DIRECT_SIZE;
			restore += RESTORE_DIRECT_SIZE;
			break;
		case HWCTX_REGINFO_INDIRECT:
			if (ptr) {
				save_indoffset(ptr, offset, 0);
				ptr += SAVE_INDOFFSET_SIZE;
				save_inddata(ptr, offset + 1, count);
				ptr += SAVE_INDDDATA_SIZE;
			}
			save += SAVE_INDOFFSET_SIZE;
			restore += RESTORE_INDOFFSET_SIZE;
			save += SAVE_INDDDATA_SIZE;
			restore += RESTORE_INDDATA_SIZE;
			break;
		case HWCTX_REGINFO_INDIRECT_OFFSET:
			if (ptr) {
				save_indoffset(ptr, offset, count);
				ptr += SAVE_INDOFFSET_SIZE;
			}
			save += SAVE_INDOFFSET_SIZE;
			restore += RESTORE_INDOFFSET_SIZE;
			continue; /* INDIRECT_DATA follows with real count */
		case HWCTX_REGINFO_INDIRECT_DATA:
			if (ptr) {
				save_inddata(ptr, offset, count);
				ptr += SAVE_INDDDATA_SIZE;
			}
			save += SAVE_INDDDATA_SIZE;
			restore += RESTORE_INDDATA_SIZE;
			break;
		}
		if (ptr) {
			memset(ptr, 0, count * 4);
			ptr += count;
		}
		save += count;
		restore += count;
	}

	if (ptr)
		save_end(ptr, syncpt_id, waitbase);

	if (words_save)
		*words_save = save;
	if (words_restore)
		*words_restore = restore;
	wmb();
}

/*** ctx3d ***/

static struct nvhost_hwctx *ctx3d_alloc(struct nvhost_channel *ch)
{
	struct nvhost_hwctx *ctx;
	struct nvmap_client *nvmap = ch->dev->nvmap;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;
	ctx->restore = nvmap_alloc(nvmap, context_restore_size * 4, 32,
				   NVMAP_HANDLE_WRITE_COMBINE);

	if (IS_ERR_OR_NULL(ctx->restore)) {
		kfree(ctx);
		return NULL;
	}

	ctx->save_cpu_data = nvmap_mmap(ctx->restore);
	if (!ctx->save_cpu_data) {
		nvmap_free(nvmap, ctx->restore);
		kfree(ctx);
		return NULL;
	}

	setup_restore(ctx->save_cpu_data, NVWAITBASE_3D);
	ctx->channel = ch;
	ctx->restore_phys = nvmap_pin(nvmap, ctx->restore);
	ctx->restore_size = context_restore_size;
	ctx->save = context_save_buf;
	ctx->save_phys = context_save_phys;
	ctx->save_size = context_save_size;
	ctx->save_incrs = 3;
	ctx->restore_incrs = 1;
	ctx->valid = false;
	kref_init(&ctx->ref);
	return ctx;
}

static void ctx3d_free(struct kref *ref)
{
	struct nvhost_hwctx *ctx = container_of(ref, struct nvhost_hwctx, ref);
	struct nvmap_client *nvmap = ctx->channel->dev->nvmap;

	nvmap_munmap(ctx->restore, ctx->save_cpu_data);
	nvmap_unpin(nvmap, ctx->restore);
	nvmap_free(nvmap, ctx->restore);
	kfree(ctx);
}

static void ctx3d_get(struct nvhost_hwctx *ctx)
{
	kref_get(&ctx->ref);
}

static void ctx3d_put(struct nvhost_hwctx *ctx)
{
	kref_put(&ctx->ref, ctx3d_free);
}

static void ctx3d_save_service(struct nvhost_hwctx *ctx)
{
	const struct hwctx_reginfo *r;
	const struct hwctx_reginfo *rend;
	unsigned int pending = 0;
	u32 *ptr = (u32 *)ctx->save_cpu_data + RESTORE_BEGIN_SIZE;

	BUG_ON(!ctx->save_cpu_data);

	r = ctxsave_regs_3d;
	rend = ctxsave_regs_3d + ARRAY_SIZE(ctxsave_regs_3d);
	for ( ; r != rend; ++r) {
		u32 count = r->count;
		switch (r->type) {
		case HWCTX_REGINFO_DIRECT:
			ptr += RESTORE_DIRECT_SIZE;
			break;
		case HWCTX_REGINFO_INDIRECT:
			ptr += RESTORE_INDOFFSET_SIZE + RESTORE_INDDATA_SIZE;
			break;
		case HWCTX_REGINFO_INDIRECT_OFFSET:
			ptr += RESTORE_INDOFFSET_SIZE;
			continue; /* INDIRECT_DATA follows with real count */
		case HWCTX_REGINFO_INDIRECT_DATA:
			ptr += RESTORE_INDDATA_SIZE;
			break;
		}
		restore_registers_from_fifo(ptr, count, ctx->channel, &pending);
		ptr += count;
	}

	BUG_ON((u32)((ptr + RESTORE_END_SIZE) - (u32*)ctx->save_cpu_data)
		!= context_restore_size);

	wmb();
	nvhost_syncpt_cpu_incr(&ctx->channel->dev->syncpt, NVSYNCPT_3D);
}


/*** nvhost_3dctx ***/

int __init nvhost_3dctx_handler_init(struct nvhost_hwctx_handler *h)
{
	struct nvhost_channel *ch;
	struct nvmap_client *nvmap;

	ch = container_of(h, struct nvhost_channel, ctxhandler);
	nvmap = ch->dev->nvmap;

	setup_save(NULL, &context_save_size, &context_restore_size, 0, 0);

	context_save_buf = nvmap_alloc(nvmap, context_save_size * 4, 32,
				       NVMAP_HANDLE_WRITE_COMBINE);

	if (IS_ERR(context_save_buf)) {
		int err = PTR_ERR(context_save_buf);
		context_save_buf = NULL;
		return err;
	}

	context_save_ptr = nvmap_mmap(context_save_buf);
	if (!context_save_ptr) {
		nvmap_free(nvmap, context_save_buf);
		context_save_buf = NULL;
		return -ENOMEM;
	}

	context_save_phys = nvmap_pin(nvmap, context_save_buf);
	setup_save(context_save_ptr, NULL, NULL, NVSYNCPT_3D, NVWAITBASE_3D);

	h->alloc = ctx3d_alloc;
	h->get = ctx3d_get;
	h->put = ctx3d_put;
	h->save_service = ctx3d_save_service;
	return 0;
}

/* TODO: [ahatala 2010-05-27] */
int __init nvhost_mpectx_handler_init(struct nvhost_hwctx_handler *h)
{
	return 0;
}
