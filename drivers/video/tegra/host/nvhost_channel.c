/*
 * drivers/video/tegra/host/nvhost_channel.c
 *
 * Tegra Graphics Host Channel
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

#include "nvhost_channel.h"
#include "dev.h"
#include "nvhost_hwctx.h"

#include <linux/platform_device.h>

#define NVMODMUTEX_2D_FULL   (1)
#define NVMODMUTEX_2D_SIMPLE (2)
#define NVMODMUTEX_2D_SB_A   (3)
#define NVMODMUTEX_2D_SB_B   (4)
#define NVMODMUTEX_3D        (5)
#define NVMODMUTEX_DISPLAYA  (6)
#define NVMODMUTEX_DISPLAYB  (7)
#define NVMODMUTEX_VI        (8)
#define NVMODMUTEX_DSI       (9)

static void power_2d(struct nvhost_module *mod, enum nvhost_power_action action);
static void power_3d(struct nvhost_module *mod, enum nvhost_power_action action);
static void power_mpe(struct nvhost_module *mod, enum nvhost_power_action action);

static const struct nvhost_channeldesc channelmap[] = {
{
	/* channel 0 */
	.name	       = "display",
	.syncpts       = BIT(NVSYNCPT_DISP0) | BIT(NVSYNCPT_DISP1) |
			 BIT(NVSYNCPT_VBLANK0) | BIT(NVSYNCPT_VBLANK1),
	.modulemutexes = BIT(NVMODMUTEX_DISPLAYA) | BIT(NVMODMUTEX_DISPLAYB),
},
{
	/* channel 1 */
	.name	       = "gr3d",
	.syncpts       = BIT(NVSYNCPT_3D),
	.waitbases     = BIT(NVWAITBASE_3D),
	.modulemutexes = BIT(NVMODMUTEX_3D),
	.class	       = NV_GRAPHICS_3D_CLASS_ID,
	.power         = power_3d,
},
{
	/* channel 2 */
	.name	       = "gr2d",
	.syncpts       = BIT(NVSYNCPT_2D_0) | BIT(NVSYNCPT_2D_1),
	.waitbases     = BIT(NVWAITBASE_2D_0) | BIT(NVWAITBASE_2D_1),
	.modulemutexes = BIT(NVMODMUTEX_2D_FULL) | BIT(NVMODMUTEX_2D_SIMPLE) |
			 BIT(NVMODMUTEX_2D_SB_A) | BIT(NVMODMUTEX_2D_SB_B),
	.power         = power_2d,
},
{
	/* channel 3 */
	.name	 = "isp",
	.syncpts = 0,
},
{
	/* channel 4 */
	.name	       = "vi",
	.syncpts       = BIT(NVSYNCPT_VI_ISP_0) | BIT(NVSYNCPT_VI_ISP_1) |
			 BIT(NVSYNCPT_VI_ISP_2) | BIT(NVSYNCPT_VI_ISP_3) |
			 BIT(NVSYNCPT_VI_ISP_4) | BIT(NVSYNCPT_VI_ISP_5),
	.modulemutexes = BIT(NVMODMUTEX_VI),
},
{
	/* channel 5 */
	.name	       = "mpe",
	.syncpts       = BIT(NVSYNCPT_MPE) | BIT(NVSYNCPT_MPE_EBM_EOF) |
			 BIT(NVSYNCPT_MPE_WR_SAFE),
	.waitbases     = BIT(NVWAITBASE_MPE),
	.class	       = NV_VIDEO_ENCODE_MPEG_CLASS_ID,
	.power	       = power_mpe,
},
{
	/* channel 6 */
	.name	       = "dsi",
	.syncpts       = BIT(NVSYNCPT_DSI),
	.modulemutexes = BIT(NVMODMUTEX_DSI),
}};

static inline void __iomem *channel_aperture(void __iomem *p, int ndx)
{
	ndx += NVHOST_CHANNEL_BASE;
	p += NV_HOST1X_CHANNEL0_BASE;
	p += ndx * NV_HOST1X_CHANNEL_MAP_SIZE_BYTES;
	return p;
}

int __init nvhost_channel_init(struct nvhost_channel *ch,
			struct nvhost_master *dev, int index)
{
	BUILD_BUG_ON(NVHOST_NUMCHANNELS != ARRAY_SIZE(channelmap));

	ch->dev = dev;
	ch->desc = &channelmap[index];
	ch->aperture = channel_aperture(dev->aperture, index);
	mutex_init(&ch->reflock);
	mutex_init(&ch->submitlock);

	return nvhost_hwctx_handler_init(&ch->ctxhandler, ch->desc->name);
}

struct nvhost_channel *nvhost_getchannel(struct nvhost_channel *ch)
{
	int err = 0;
	mutex_lock(&ch->reflock);
	if (ch->refcount == 0) {
		err = nvhost_module_init(&ch->mod, ch->desc->name,
					ch->desc->power, &ch->dev->mod,
					&ch->dev->pdev->dev);
		if (!err) {
			err = nvhost_cdma_init(&ch->cdma);
			if (err)
				nvhost_module_deinit(&ch->mod);
		}
	}
	if (!err) {
		ch->refcount++;
	}
	mutex_unlock(&ch->reflock);

	return err ? NULL : ch;
}

void nvhost_putchannel(struct nvhost_channel *ch, struct nvhost_hwctx *ctx)
{
	if (ctx) {
		mutex_lock(&ch->submitlock);
		if (ch->cur_ctx == ctx)
			ch->cur_ctx = NULL;
		mutex_unlock(&ch->submitlock);
	}

	mutex_lock(&ch->reflock);
	if (ch->refcount == 1) {
		nvhost_module_deinit(&ch->mod);
		/* cdma may already be stopped, that's ok */
		nvhost_cdma_stop(&ch->cdma);
		nvhost_cdma_deinit(&ch->cdma);
	}
	ch->refcount--;
	mutex_unlock(&ch->reflock);
}

void nvhost_channel_suspend(struct nvhost_channel *ch)
{
	mutex_lock(&ch->reflock);
	BUG_ON(nvhost_module_powered(&ch->mod));
	nvhost_cdma_stop(&ch->cdma);
	mutex_unlock(&ch->reflock);
}

void nvhost_channel_submit(struct nvhost_channel *ch,
                           struct nvmap_client *user_nvmap,
                           struct nvhost_op_pair *ops, int num_pairs,
                           struct nvhost_cpuinterrupt *intrs, int num_intrs,
                           struct nvmap_handle **unpins, int num_unpins,
                           u32 syncpt_id, u32 syncpt_val)
{
	int i;
	struct nvhost_op_pair* p;

	/* schedule interrupts */
	for (i = 0; i < num_intrs; i++) {
		nvhost_intr_add_action(&ch->dev->intr, syncpt_id, intrs[i].syncpt_val,
				NVHOST_INTR_ACTION_CTXSAVE, intrs[i].intr_data, NULL);
	}

	/* begin a CDMA submit */
	nvhost_cdma_begin(&ch->cdma);

	/* push ops */
	for (i = 0, p = ops; i < num_pairs; i++, p++)
		nvhost_cdma_push(&ch->cdma, p->op1, p->op2);

	/* end CDMA submit & stash pinned hMems into sync queue for later cleanup */
	nvhost_cdma_end(user_nvmap, &ch->cdma, syncpt_id, syncpt_val,
                        unpins, num_unpins);
}

static void power_2d(struct nvhost_module *mod, enum nvhost_power_action action)
{
	/* TODO: [ahatala 2010-06-17] reimplement EPP hang war */
	if (action == NVHOST_POWER_ACTION_OFF) {
		/* TODO: [ahatala 2010-06-17] reset EPP */
	}
}

static void power_3d(struct nvhost_module *mod, enum nvhost_power_action action)
{
	struct nvhost_channel *ch = container_of(mod, struct nvhost_channel, mod);

	if (action == NVHOST_POWER_ACTION_OFF) {
		mutex_lock(&ch->submitlock);
		if (ch->cur_ctx) {
			DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);
			struct nvhost_op_pair save;
			struct nvhost_cpuinterrupt ctxsw;
			u32 syncval;
			syncval = nvhost_syncpt_incr_max(&ch->dev->syncpt,
							NVSYNCPT_3D,
							ch->cur_ctx->save_incrs);
			save.op1 = nvhost_opcode_gather(0, ch->cur_ctx->save_size);
			save.op2 = ch->cur_ctx->save_phys;
			ctxsw.intr_data = ch->cur_ctx;
			ctxsw.syncpt_val = syncval - 1;
			ch->cur_ctx->valid = true;
			ch->ctxhandler.get(ch->cur_ctx);
			ch->cur_ctx = NULL;

			nvhost_channel_submit(ch, ch->dev->nvmap,
					      &save, 1, &ctxsw, 1, NULL, 0,
					      NVSYNCPT_3D, syncval);

			nvhost_intr_add_action(&ch->dev->intr, NVSYNCPT_3D,
					       syncval,
					       NVHOST_INTR_ACTION_WAKEUP,
					       &wq, NULL);
			wait_event(wq,
				   nvhost_syncpt_min_cmp(&ch->dev->syncpt,
							 NVSYNCPT_3D, syncval));
			nvhost_cdma_update(&ch->cdma);
		}
		mutex_unlock(&ch->submitlock);
	}
}

static void power_mpe(struct nvhost_module *mod, enum nvhost_power_action action)
{
}
