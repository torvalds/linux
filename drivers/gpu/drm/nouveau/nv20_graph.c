#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

/*
 * NV20
 * -----
 * There are 3 families :
 * NV20 is 0x10de:0x020*
 * NV25/28 is 0x10de:0x025* / 0x10de:0x028*
 * NV2A is 0x10de:0x02A0
 *
 * NV30
 * -----
 * There are 3 families :
 * NV30/31 is 0x10de:0x030* / 0x10de:0x031*
 * NV34 is 0x10de:0x032*
 * NV35/36 is 0x10de:0x033* / 0x10de:0x034*
 *
 * Not seen in the wild, no dumps (probably NV35) :
 * NV37 is 0x10de:0x00fc, 0x10de:0x00fd
 * NV38 is 0x10de:0x0333, 0x10de:0x00fe
 *
 */

#define NV20_GRCTX_SIZE (3580*4)
#define NV25_GRCTX_SIZE (3529*4)
#define NV2A_GRCTX_SIZE (3500*4)

#define NV30_31_GRCTX_SIZE (24392)
#define NV34_GRCTX_SIZE    (18140)
#define NV35_36_GRCTX_SIZE (22396)

static void
nv20_graph_context_init(struct drm_device *dev, struct nouveau_gpuobj *ctx)
{
	int i;

	nv_wo32(ctx, 0x033c, 0xffff0000);
	nv_wo32(ctx, 0x03a0, 0x0fff0000);
	nv_wo32(ctx, 0x03a4, 0x0fff0000);
	nv_wo32(ctx, 0x047c, 0x00000101);
	nv_wo32(ctx, 0x0490, 0x00000111);
	nv_wo32(ctx, 0x04a8, 0x44400000);
	for (i = 0x04d4; i <= 0x04e0; i += 4)
		nv_wo32(ctx, i, 0x00030303);
	for (i = 0x04f4; i <= 0x0500; i += 4)
		nv_wo32(ctx, i, 0x00080000);
	for (i = 0x050c; i <= 0x0518; i += 4)
		nv_wo32(ctx, i, 0x01012000);
	for (i = 0x051c; i <= 0x0528; i += 4)
		nv_wo32(ctx, i, 0x000105b8);
	for (i = 0x052c; i <= 0x0538; i += 4)
		nv_wo32(ctx, i, 0x00080008);
	for (i = 0x055c; i <= 0x0598; i += 4)
		nv_wo32(ctx, i, 0x07ff0000);
	nv_wo32(ctx, 0x05a4, 0x4b7fffff);
	nv_wo32(ctx, 0x05fc, 0x00000001);
	nv_wo32(ctx, 0x0604, 0x00004000);
	nv_wo32(ctx, 0x0610, 0x00000001);
	nv_wo32(ctx, 0x0618, 0x00040000);
	nv_wo32(ctx, 0x061c, 0x00010000);
	for (i = 0x1c1c; i <= 0x248c; i += 16) {
		nv_wo32(ctx, (i + 0), 0x10700ff9);
		nv_wo32(ctx, (i + 4), 0x0436086c);
		nv_wo32(ctx, (i + 8), 0x000c001b);
	}
	nv_wo32(ctx, 0x281c, 0x3f800000);
	nv_wo32(ctx, 0x2830, 0x3f800000);
	nv_wo32(ctx, 0x285c, 0x40000000);
	nv_wo32(ctx, 0x2860, 0x3f800000);
	nv_wo32(ctx, 0x2864, 0x3f000000);
	nv_wo32(ctx, 0x286c, 0x40000000);
	nv_wo32(ctx, 0x2870, 0x3f800000);
	nv_wo32(ctx, 0x2878, 0xbf800000);
	nv_wo32(ctx, 0x2880, 0xbf800000);
	nv_wo32(ctx, 0x34a4, 0x000fe000);
	nv_wo32(ctx, 0x3530, 0x000003f8);
	nv_wo32(ctx, 0x3540, 0x002fe000);
	for (i = 0x355c; i <= 0x3578; i += 4)
		nv_wo32(ctx, i, 0x001c527c);
}

static void
nv25_graph_context_init(struct drm_device *dev, struct nouveau_gpuobj *ctx)
{
	int i;

	nv_wo32(ctx, 0x035c, 0xffff0000);
	nv_wo32(ctx, 0x03c0, 0x0fff0000);
	nv_wo32(ctx, 0x03c4, 0x0fff0000);
	nv_wo32(ctx, 0x049c, 0x00000101);
	nv_wo32(ctx, 0x04b0, 0x00000111);
	nv_wo32(ctx, 0x04c8, 0x00000080);
	nv_wo32(ctx, 0x04cc, 0xffff0000);
	nv_wo32(ctx, 0x04d0, 0x00000001);
	nv_wo32(ctx, 0x04e4, 0x44400000);
	nv_wo32(ctx, 0x04fc, 0x4b800000);
	for (i = 0x0510; i <= 0x051c; i += 4)
		nv_wo32(ctx, i, 0x00030303);
	for (i = 0x0530; i <= 0x053c; i += 4)
		nv_wo32(ctx, i, 0x00080000);
	for (i = 0x0548; i <= 0x0554; i += 4)
		nv_wo32(ctx, i, 0x01012000);
	for (i = 0x0558; i <= 0x0564; i += 4)
		nv_wo32(ctx, i, 0x000105b8);
	for (i = 0x0568; i <= 0x0574; i += 4)
		nv_wo32(ctx, i, 0x00080008);
	for (i = 0x0598; i <= 0x05d4; i += 4)
		nv_wo32(ctx, i, 0x07ff0000);
	nv_wo32(ctx, 0x05e0, 0x4b7fffff);
	nv_wo32(ctx, 0x0620, 0x00000080);
	nv_wo32(ctx, 0x0624, 0x30201000);
	nv_wo32(ctx, 0x0628, 0x70605040);
	nv_wo32(ctx, 0x062c, 0xb0a09080);
	nv_wo32(ctx, 0x0630, 0xf0e0d0c0);
	nv_wo32(ctx, 0x0664, 0x00000001);
	nv_wo32(ctx, 0x066c, 0x00004000);
	nv_wo32(ctx, 0x0678, 0x00000001);
	nv_wo32(ctx, 0x0680, 0x00040000);
	nv_wo32(ctx, 0x0684, 0x00010000);
	for (i = 0x1b04; i <= 0x2374; i += 16) {
		nv_wo32(ctx, (i + 0), 0x10700ff9);
		nv_wo32(ctx, (i + 4), 0x0436086c);
		nv_wo32(ctx, (i + 8), 0x000c001b);
	}
	nv_wo32(ctx, 0x2704, 0x3f800000);
	nv_wo32(ctx, 0x2718, 0x3f800000);
	nv_wo32(ctx, 0x2744, 0x40000000);
	nv_wo32(ctx, 0x2748, 0x3f800000);
	nv_wo32(ctx, 0x274c, 0x3f000000);
	nv_wo32(ctx, 0x2754, 0x40000000);
	nv_wo32(ctx, 0x2758, 0x3f800000);
	nv_wo32(ctx, 0x2760, 0xbf800000);
	nv_wo32(ctx, 0x2768, 0xbf800000);
	nv_wo32(ctx, 0x308c, 0x000fe000);
	nv_wo32(ctx, 0x3108, 0x000003f8);
	nv_wo32(ctx, 0x3468, 0x002fe000);
	for (i = 0x3484; i <= 0x34a0; i += 4)
		nv_wo32(ctx, i, 0x001c527c);
}

static void
nv2a_graph_context_init(struct drm_device *dev, struct nouveau_gpuobj *ctx)
{
	int i;

	nv_wo32(ctx, 0x033c, 0xffff0000);
	nv_wo32(ctx, 0x03a0, 0x0fff0000);
	nv_wo32(ctx, 0x03a4, 0x0fff0000);
	nv_wo32(ctx, 0x047c, 0x00000101);
	nv_wo32(ctx, 0x0490, 0x00000111);
	nv_wo32(ctx, 0x04a8, 0x44400000);
	for (i = 0x04d4; i <= 0x04e0; i += 4)
		nv_wo32(ctx, i, 0x00030303);
	for (i = 0x04f4; i <= 0x0500; i += 4)
		nv_wo32(ctx, i, 0x00080000);
	for (i = 0x050c; i <= 0x0518; i += 4)
		nv_wo32(ctx, i, 0x01012000);
	for (i = 0x051c; i <= 0x0528; i += 4)
		nv_wo32(ctx, i, 0x000105b8);
	for (i = 0x052c; i <= 0x0538; i += 4)
		nv_wo32(ctx, i, 0x00080008);
	for (i = 0x055c; i <= 0x0598; i += 4)
		nv_wo32(ctx, i, 0x07ff0000);
	nv_wo32(ctx, 0x05a4, 0x4b7fffff);
	nv_wo32(ctx, 0x05fc, 0x00000001);
	nv_wo32(ctx, 0x0604, 0x00004000);
	nv_wo32(ctx, 0x0610, 0x00000001);
	nv_wo32(ctx, 0x0618, 0x00040000);
	nv_wo32(ctx, 0x061c, 0x00010000);
	for (i = 0x1a9c; i <= 0x22fc; i += 16) { /*XXX: check!! */
		nv_wo32(ctx, (i + 0), 0x10700ff9);
		nv_wo32(ctx, (i + 4), 0x0436086c);
		nv_wo32(ctx, (i + 8), 0x000c001b);
	}
	nv_wo32(ctx, 0x269c, 0x3f800000);
	nv_wo32(ctx, 0x26b0, 0x3f800000);
	nv_wo32(ctx, 0x26dc, 0x40000000);
	nv_wo32(ctx, 0x26e0, 0x3f800000);
	nv_wo32(ctx, 0x26e4, 0x3f000000);
	nv_wo32(ctx, 0x26ec, 0x40000000);
	nv_wo32(ctx, 0x26f0, 0x3f800000);
	nv_wo32(ctx, 0x26f8, 0xbf800000);
	nv_wo32(ctx, 0x2700, 0xbf800000);
	nv_wo32(ctx, 0x3024, 0x000fe000);
	nv_wo32(ctx, 0x30a0, 0x000003f8);
	nv_wo32(ctx, 0x33fc, 0x002fe000);
	for (i = 0x341c; i <= 0x3438; i += 4)
		nv_wo32(ctx, i, 0x001c527c);
}

static void
nv30_31_graph_context_init(struct drm_device *dev, struct nouveau_gpuobj *ctx)
{
	int i;

	nv_wo32(ctx, 0x0410, 0x00000101);
	nv_wo32(ctx, 0x0424, 0x00000111);
	nv_wo32(ctx, 0x0428, 0x00000060);
	nv_wo32(ctx, 0x0444, 0x00000080);
	nv_wo32(ctx, 0x0448, 0xffff0000);
	nv_wo32(ctx, 0x044c, 0x00000001);
	nv_wo32(ctx, 0x0460, 0x44400000);
	nv_wo32(ctx, 0x048c, 0xffff0000);
	for (i = 0x04e0; i < 0x04e8; i += 4)
		nv_wo32(ctx, i, 0x0fff0000);
	nv_wo32(ctx, 0x04ec, 0x00011100);
	for (i = 0x0508; i < 0x0548; i += 4)
		nv_wo32(ctx, i, 0x07ff0000);
	nv_wo32(ctx, 0x0550, 0x4b7fffff);
	nv_wo32(ctx, 0x058c, 0x00000080);
	nv_wo32(ctx, 0x0590, 0x30201000);
	nv_wo32(ctx, 0x0594, 0x70605040);
	nv_wo32(ctx, 0x0598, 0xb8a89888);
	nv_wo32(ctx, 0x059c, 0xf8e8d8c8);
	nv_wo32(ctx, 0x05b0, 0xb0000000);
	for (i = 0x0600; i < 0x0640; i += 4)
		nv_wo32(ctx, i, 0x00010588);
	for (i = 0x0640; i < 0x0680; i += 4)
		nv_wo32(ctx, i, 0x00030303);
	for (i = 0x06c0; i < 0x0700; i += 4)
		nv_wo32(ctx, i, 0x0008aae4);
	for (i = 0x0700; i < 0x0740; i += 4)
		nv_wo32(ctx, i, 0x01012000);
	for (i = 0x0740; i < 0x0780; i += 4)
		nv_wo32(ctx, i, 0x00080008);
	nv_wo32(ctx, 0x085c, 0x00040000);
	nv_wo32(ctx, 0x0860, 0x00010000);
	for (i = 0x0864; i < 0x0874; i += 4)
		nv_wo32(ctx, i, 0x00040004);
	for (i = 0x1f18; i <= 0x3088 ; i += 16) {
		nv_wo32(ctx, i + 0, 0x10700ff9);
		nv_wo32(ctx, i + 1, 0x0436086c);
		nv_wo32(ctx, i + 2, 0x000c001b);
	}
	for (i = 0x30b8; i < 0x30c8; i += 4)
		nv_wo32(ctx, i, 0x0000ffff);
	nv_wo32(ctx, 0x344c, 0x3f800000);
	nv_wo32(ctx, 0x3808, 0x3f800000);
	nv_wo32(ctx, 0x381c, 0x3f800000);
	nv_wo32(ctx, 0x3848, 0x40000000);
	nv_wo32(ctx, 0x384c, 0x3f800000);
	nv_wo32(ctx, 0x3850, 0x3f000000);
	nv_wo32(ctx, 0x3858, 0x40000000);
	nv_wo32(ctx, 0x385c, 0x3f800000);
	nv_wo32(ctx, 0x3864, 0xbf800000);
	nv_wo32(ctx, 0x386c, 0xbf800000);
}

static void
nv34_graph_context_init(struct drm_device *dev, struct nouveau_gpuobj *ctx)
{
	int i;

	nv_wo32(ctx, 0x040c, 0x01000101);
	nv_wo32(ctx, 0x0420, 0x00000111);
	nv_wo32(ctx, 0x0424, 0x00000060);
	nv_wo32(ctx, 0x0440, 0x00000080);
	nv_wo32(ctx, 0x0444, 0xffff0000);
	nv_wo32(ctx, 0x0448, 0x00000001);
	nv_wo32(ctx, 0x045c, 0x44400000);
	nv_wo32(ctx, 0x0480, 0xffff0000);
	for (i = 0x04d4; i < 0x04dc; i += 4)
		nv_wo32(ctx, i, 0x0fff0000);
	nv_wo32(ctx, 0x04e0, 0x00011100);
	for (i = 0x04fc; i < 0x053c; i += 4)
		nv_wo32(ctx, i, 0x07ff0000);
	nv_wo32(ctx, 0x0544, 0x4b7fffff);
	nv_wo32(ctx, 0x057c, 0x00000080);
	nv_wo32(ctx, 0x0580, 0x30201000);
	nv_wo32(ctx, 0x0584, 0x70605040);
	nv_wo32(ctx, 0x0588, 0xb8a89888);
	nv_wo32(ctx, 0x058c, 0xf8e8d8c8);
	nv_wo32(ctx, 0x05a0, 0xb0000000);
	for (i = 0x05f0; i < 0x0630; i += 4)
		nv_wo32(ctx, i, 0x00010588);
	for (i = 0x0630; i < 0x0670; i += 4)
		nv_wo32(ctx, i, 0x00030303);
	for (i = 0x06b0; i < 0x06f0; i += 4)
		nv_wo32(ctx, i, 0x0008aae4);
	for (i = 0x06f0; i < 0x0730; i += 4)
		nv_wo32(ctx, i, 0x01012000);
	for (i = 0x0730; i < 0x0770; i += 4)
		nv_wo32(ctx, i, 0x00080008);
	nv_wo32(ctx, 0x0850, 0x00040000);
	nv_wo32(ctx, 0x0854, 0x00010000);
	for (i = 0x0858; i < 0x0868; i += 4)
		nv_wo32(ctx, i, 0x00040004);
	for (i = 0x15ac; i <= 0x271c ; i += 16) {
		nv_wo32(ctx, i + 0, 0x10700ff9);
		nv_wo32(ctx, i + 1, 0x0436086c);
		nv_wo32(ctx, i + 2, 0x000c001b);
	}
	for (i = 0x274c; i < 0x275c; i += 4)
		nv_wo32(ctx, i, 0x0000ffff);
	nv_wo32(ctx, 0x2ae0, 0x3f800000);
	nv_wo32(ctx, 0x2e9c, 0x3f800000);
	nv_wo32(ctx, 0x2eb0, 0x3f800000);
	nv_wo32(ctx, 0x2edc, 0x40000000);
	nv_wo32(ctx, 0x2ee0, 0x3f800000);
	nv_wo32(ctx, 0x2ee4, 0x3f000000);
	nv_wo32(ctx, 0x2eec, 0x40000000);
	nv_wo32(ctx, 0x2ef0, 0x3f800000);
	nv_wo32(ctx, 0x2ef8, 0xbf800000);
	nv_wo32(ctx, 0x2f00, 0xbf800000);
}

static void
nv35_36_graph_context_init(struct drm_device *dev, struct nouveau_gpuobj *ctx)
{
	int i;

	nv_wo32(ctx, 0x040c, 0x00000101);
	nv_wo32(ctx, 0x0420, 0x00000111);
	nv_wo32(ctx, 0x0424, 0x00000060);
	nv_wo32(ctx, 0x0440, 0x00000080);
	nv_wo32(ctx, 0x0444, 0xffff0000);
	nv_wo32(ctx, 0x0448, 0x00000001);
	nv_wo32(ctx, 0x045c, 0x44400000);
	nv_wo32(ctx, 0x0488, 0xffff0000);
	for (i = 0x04dc; i < 0x04e4; i += 4)
		nv_wo32(ctx, i, 0x0fff0000);
	nv_wo32(ctx, 0x04e8, 0x00011100);
	for (i = 0x0504; i < 0x0544; i += 4)
		nv_wo32(ctx, i, 0x07ff0000);
	nv_wo32(ctx, 0x054c, 0x4b7fffff);
	nv_wo32(ctx, 0x0588, 0x00000080);
	nv_wo32(ctx, 0x058c, 0x30201000);
	nv_wo32(ctx, 0x0590, 0x70605040);
	nv_wo32(ctx, 0x0594, 0xb8a89888);
	nv_wo32(ctx, 0x0598, 0xf8e8d8c8);
	nv_wo32(ctx, 0x05ac, 0xb0000000);
	for (i = 0x0604; i < 0x0644; i += 4)
		nv_wo32(ctx, i, 0x00010588);
	for (i = 0x0644; i < 0x0684; i += 4)
		nv_wo32(ctx, i, 0x00030303);
	for (i = 0x06c4; i < 0x0704; i += 4)
		nv_wo32(ctx, i, 0x0008aae4);
	for (i = 0x0704; i < 0x0744; i += 4)
		nv_wo32(ctx, i, 0x01012000);
	for (i = 0x0744; i < 0x0784; i += 4)
		nv_wo32(ctx, i, 0x00080008);
	nv_wo32(ctx, 0x0860, 0x00040000);
	nv_wo32(ctx, 0x0864, 0x00010000);
	for (i = 0x0868; i < 0x0878; i += 4)
		nv_wo32(ctx, i, 0x00040004);
	for (i = 0x1f1c; i <= 0x308c ; i += 16) {
		nv_wo32(ctx, i + 0, 0x10700ff9);
		nv_wo32(ctx, i + 4, 0x0436086c);
		nv_wo32(ctx, i + 8, 0x000c001b);
	}
	for (i = 0x30bc; i < 0x30cc; i += 4)
		nv_wo32(ctx, i, 0x0000ffff);
	nv_wo32(ctx, 0x3450, 0x3f800000);
	nv_wo32(ctx, 0x380c, 0x3f800000);
	nv_wo32(ctx, 0x3820, 0x3f800000);
	nv_wo32(ctx, 0x384c, 0x40000000);
	nv_wo32(ctx, 0x3850, 0x3f800000);
	nv_wo32(ctx, 0x3854, 0x3f000000);
	nv_wo32(ctx, 0x385c, 0x40000000);
	nv_wo32(ctx, 0x3860, 0x3f800000);
	nv_wo32(ctx, 0x3868, 0xbf800000);
	nv_wo32(ctx, 0x3870, 0xbf800000);
}

int
nv20_graph_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	void (*ctx_init)(struct drm_device *, struct nouveau_gpuobj *);
	unsigned int idoffs = 0x28;
	int ret;

	switch (dev_priv->chipset) {
	case 0x20:
		ctx_init = nv20_graph_context_init;
		idoffs = 0;
		break;
	case 0x25:
	case 0x28:
		ctx_init = nv25_graph_context_init;
		break;
	case 0x2a:
		ctx_init = nv2a_graph_context_init;
		idoffs = 0;
		break;
	case 0x30:
	case 0x31:
		ctx_init = nv30_31_graph_context_init;
		break;
	case 0x34:
		ctx_init = nv34_graph_context_init;
		break;
	case 0x35:
	case 0x36:
		ctx_init = nv35_36_graph_context_init;
		break;
	default:
		BUG_ON(1);
	}

	ret = nouveau_gpuobj_new(dev, chan, pgraph->grctx_size, 16,
				 NVOBJ_FLAG_ZERO_ALLOC, &chan->ramin_grctx);
	if (ret)
		return ret;

	/* Initialise default context values */
	ctx_init(dev, chan->ramin_grctx);

	/* nv20: nv_wo32(dev, chan->ramin_grctx->gpuobj, 10, chan->id<<24); */
	nv_wo32(chan->ramin_grctx, idoffs,
		(chan->id << 24) | 0x1); /* CTX_USER */

	nv_wo32(pgraph->ctx_table, chan->id * 4, chan->ramin_grctx->pinst >> 4);
	return 0;
}

void
nv20_graph_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	pgraph->fifo_access(dev, false);

	/* Unload the context if it's the currently active one */
	if (pgraph->channel(dev) == chan)
		pgraph->unload_context(dev);

	pgraph->fifo_access(dev, true);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	/* Free the context resources */
	nv_wo32(pgraph->ctx_table, chan->id * 4, 0);
	nouveau_gpuobj_ref(NULL, &chan->ramin_grctx);
}

int
nv20_graph_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	uint32_t inst;

	if (!chan->ramin_grctx)
		return -EINVAL;
	inst = chan->ramin_grctx->pinst >> 4;

	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_POINTER, inst);
	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_XFER,
		     NV20_PGRAPH_CHANNEL_CTX_XFER_LOAD);
	nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x10010100);

	nouveau_wait_for_idle(dev);
	return 0;
}

int
nv20_graph_unload_context(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_channel *chan;
	uint32_t inst, tmp;

	chan = pgraph->channel(dev);
	if (!chan)
		return 0;
	inst = chan->ramin_grctx->pinst >> 4;

	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_POINTER, inst);
	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_XFER,
		     NV20_PGRAPH_CHANNEL_CTX_XFER_SAVE);

	nouveau_wait_for_idle(dev);

	nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x10000000);
	tmp  = nv_rd32(dev, NV10_PGRAPH_CTX_USER) & 0x00ffffff;
	tmp |= (pfifo->channels - 1) << 24;
	nv_wr32(dev, NV10_PGRAPH_CTX_USER, tmp);
	return 0;
}

static void
nv20_graph_rdi(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i, writecount = 32;
	uint32_t rdi_index = 0x2c80000;

	if (dev_priv->chipset == 0x20) {
		rdi_index = 0x3d0000;
		writecount = 15;
	}

	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, rdi_index);
	for (i = 0; i < writecount; i++)
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA, 0);

	nouveau_wait_for_idle(dev);
}

void
nv20_graph_set_region_tiling(struct drm_device *dev, int i, uint32_t addr,
			     uint32_t size, uint32_t pitch)
{
	uint32_t limit = max(1u, addr + size) - 1;

	if (pitch)
		addr |= 1;

	nv_wr32(dev, NV20_PGRAPH_TLIMIT(i), limit);
	nv_wr32(dev, NV20_PGRAPH_TSIZE(i), pitch);
	nv_wr32(dev, NV20_PGRAPH_TILE(i), addr);

	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0030 + 4 * i);
	nv_wr32(dev, NV10_PGRAPH_RDI_DATA, limit);
	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0050 + 4 * i);
	nv_wr32(dev, NV10_PGRAPH_RDI_DATA, pitch);
	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0010 + 4 * i);
	nv_wr32(dev, NV10_PGRAPH_RDI_DATA, addr);
}

int
nv20_graph_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	uint32_t tmp, vramsz;
	int ret, i;

	switch (dev_priv->chipset) {
	case 0x20:
		pgraph->grctx_size = NV20_GRCTX_SIZE;
		break;
	case 0x25:
	case 0x28:
		pgraph->grctx_size = NV25_GRCTX_SIZE;
		break;
	case 0x2a:
		pgraph->grctx_size = NV2A_GRCTX_SIZE;
		break;
	default:
		NV_ERROR(dev, "unknown chipset, disabling acceleration\n");
		pgraph->accel_blocked = true;
		return 0;
	}

	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) & ~NV_PMC_ENABLE_PGRAPH);
	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) |  NV_PMC_ENABLE_PGRAPH);

	if (!pgraph->ctx_table) {
		/* Create Context Pointer Table */
		ret = nouveau_gpuobj_new(dev, NULL, 32 * 4, 16,
					 NVOBJ_FLAG_ZERO_ALLOC,
					 &pgraph->ctx_table);
		if (ret)
			return ret;
	}

	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_TABLE,
		     pgraph->ctx_table->pinst >> 4);

	nv20_graph_rdi(dev);

	nv_wr32(dev, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nv_wr32(dev, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_1, 0x00118700);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_3, 0xF3CE0475); /* 0x4 = auto ctx switch */
	nv_wr32(dev, NV10_PGRAPH_DEBUG_4, 0x00000000);
	nv_wr32(dev, 0x40009C           , 0x00000040);

	if (dev_priv->chipset >= 0x25) {
		nv_wr32(dev, 0x400890, 0x00080000);
		nv_wr32(dev, 0x400610, 0x304B1FB6);
		nv_wr32(dev, 0x400B80, 0x18B82880);
		nv_wr32(dev, 0x400B84, 0x44000000);
		nv_wr32(dev, 0x400098, 0x40000080);
		nv_wr32(dev, 0x400B88, 0x000000ff);
	} else {
		nv_wr32(dev, 0x400880, 0x00080000); /* 0x0008c7df */
		nv_wr32(dev, 0x400094, 0x00000005);
		nv_wr32(dev, 0x400B80, 0x45CAA208); /* 0x45eae20e */
		nv_wr32(dev, 0x400B84, 0x24000000);
		nv_wr32(dev, 0x400098, 0x00000040);
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00E00038);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA , 0x00000030);
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00E10038);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA , 0x00000030);
	}

	/* Turn all the tiling regions off. */
	for (i = 0; i < NV10_PFB_TILE__SIZE; i++)
		nv20_graph_set_region_tiling(dev, i, 0, 0, 0);

	for (i = 0; i < 8; i++) {
		nv_wr32(dev, 0x400980 + i * 4, nv_rd32(dev, 0x100300 + i * 4));
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0090 + i * 4);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA,
					nv_rd32(dev, 0x100300 + i * 4));
	}
	nv_wr32(dev, 0x4009a0, nv_rd32(dev, 0x100324));
	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA000C);
	nv_wr32(dev, NV10_PGRAPH_RDI_DATA, nv_rd32(dev, 0x100324));

	nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x10000100);
	nv_wr32(dev, NV10_PGRAPH_STATE      , 0xFFFFFFFF);

	tmp = nv_rd32(dev, NV10_PGRAPH_SURFACE) & 0x0007ff00;
	nv_wr32(dev, NV10_PGRAPH_SURFACE, tmp);
	tmp = nv_rd32(dev, NV10_PGRAPH_SURFACE) | 0x00020100;
	nv_wr32(dev, NV10_PGRAPH_SURFACE, tmp);

	/* begin RAM config */
	vramsz = pci_resource_len(dev->pdev, 0) - 1;
	nv_wr32(dev, 0x4009A4, nv_rd32(dev, NV04_PFB_CFG0));
	nv_wr32(dev, 0x4009A8, nv_rd32(dev, NV04_PFB_CFG1));
	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0000);
	nv_wr32(dev, NV10_PGRAPH_RDI_DATA , nv_rd32(dev, NV04_PFB_CFG0));
	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0004);
	nv_wr32(dev, NV10_PGRAPH_RDI_DATA , nv_rd32(dev, NV04_PFB_CFG1));
	nv_wr32(dev, 0x400820, 0);
	nv_wr32(dev, 0x400824, 0);
	nv_wr32(dev, 0x400864, vramsz - 1);
	nv_wr32(dev, 0x400868, vramsz - 1);

	/* interesting.. the below overwrites some of the tile setup above.. */
	nv_wr32(dev, 0x400B20, 0x00000000);
	nv_wr32(dev, 0x400B04, 0xFFFFFFFF);

	nv_wr32(dev, NV03_PGRAPH_ABS_UCLIP_XMIN, 0);
	nv_wr32(dev, NV03_PGRAPH_ABS_UCLIP_YMIN, 0);
	nv_wr32(dev, NV03_PGRAPH_ABS_UCLIP_XMAX, 0x7fff);
	nv_wr32(dev, NV03_PGRAPH_ABS_UCLIP_YMAX, 0x7fff);

	return 0;
}

void
nv20_graph_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;

	nouveau_gpuobj_ref(NULL, &pgraph->ctx_table);
}

int
nv30_graph_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	int ret, i;

	switch (dev_priv->chipset) {
	case 0x30:
	case 0x31:
		pgraph->grctx_size = NV30_31_GRCTX_SIZE;
		break;
	case 0x34:
		pgraph->grctx_size = NV34_GRCTX_SIZE;
		break;
	case 0x35:
	case 0x36:
		pgraph->grctx_size = NV35_36_GRCTX_SIZE;
		break;
	default:
		NV_ERROR(dev, "unknown chipset, disabling acceleration\n");
		pgraph->accel_blocked = true;
		return 0;
	}

	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) & ~NV_PMC_ENABLE_PGRAPH);
	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) |  NV_PMC_ENABLE_PGRAPH);

	if (!pgraph->ctx_table) {
		/* Create Context Pointer Table */
		ret = nouveau_gpuobj_new(dev, NULL, 32 * 4, 16,
					 NVOBJ_FLAG_ZERO_ALLOC,
					 &pgraph->ctx_table);
		if (ret)
			return ret;
	}

	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_TABLE,
		     pgraph->ctx_table->pinst >> 4);

	nv_wr32(dev, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nv_wr32(dev, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_1, 0x401287c0);
	nv_wr32(dev, 0x400890, 0x01b463ff);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_3, 0xf2de0475);
	nv_wr32(dev, NV10_PGRAPH_DEBUG_4, 0x00008000);
	nv_wr32(dev, NV04_PGRAPH_LIMIT_VIOL_PIX, 0xf04bdff6);
	nv_wr32(dev, 0x400B80, 0x1003d888);
	nv_wr32(dev, 0x400B84, 0x0c000000);
	nv_wr32(dev, 0x400098, 0x00000000);
	nv_wr32(dev, 0x40009C, 0x0005ad00);
	nv_wr32(dev, 0x400B88, 0x62ff00ff); /* suspiciously like PGRAPH_DEBUG_2 */
	nv_wr32(dev, 0x4000a0, 0x00000000);
	nv_wr32(dev, 0x4000a4, 0x00000008);
	nv_wr32(dev, 0x4008a8, 0xb784a400);
	nv_wr32(dev, 0x400ba0, 0x002f8685);
	nv_wr32(dev, 0x400ba4, 0x00231f3f);
	nv_wr32(dev, 0x4008a4, 0x40000020);

	if (dev_priv->chipset == 0x34) {
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0004);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA , 0x00200201);
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0008);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA , 0x00000008);
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0000);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA , 0x00000032);
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00E00004);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA , 0x00000002);
	}

	nv_wr32(dev, 0x4000c0, 0x00000016);

	/* Turn all the tiling regions off. */
	for (i = 0; i < NV10_PFB_TILE__SIZE; i++)
		nv20_graph_set_region_tiling(dev, i, 0, 0, 0);

	nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x10000100);
	nv_wr32(dev, NV10_PGRAPH_STATE      , 0xFFFFFFFF);
	nv_wr32(dev, 0x0040075c             , 0x00000001);

	/* begin RAM config */
	/* vramsz = pci_resource_len(dev->pdev, 0) - 1; */
	nv_wr32(dev, 0x4009A4, nv_rd32(dev, NV04_PFB_CFG0));
	nv_wr32(dev, 0x4009A8, nv_rd32(dev, NV04_PFB_CFG1));
	if (dev_priv->chipset != 0x34) {
		nv_wr32(dev, 0x400750, 0x00EA0000);
		nv_wr32(dev, 0x400754, nv_rd32(dev, NV04_PFB_CFG0));
		nv_wr32(dev, 0x400750, 0x00EA0004);
		nv_wr32(dev, 0x400754, nv_rd32(dev, NV04_PFB_CFG1));
	}

	return 0;
}

struct nouveau_pgraph_object_class nv20_graph_grclass[] = {
	{ 0x0030, NVOBJ_ENGINE_GR, NULL }, /* null */
	{ 0x0039, NVOBJ_ENGINE_GR, NULL }, /* m2mf */
	{ 0x004a, NVOBJ_ENGINE_GR, NULL }, /* gdirect */
	{ 0x009f, NVOBJ_ENGINE_GR, NULL }, /* imageblit (nv12) */
	{ 0x008a, NVOBJ_ENGINE_GR, NULL }, /* ifc */
	{ 0x0089, NVOBJ_ENGINE_GR, NULL }, /* sifm */
	{ 0x0062, NVOBJ_ENGINE_GR, NULL }, /* surf2d */
	{ 0x0043, NVOBJ_ENGINE_GR, NULL }, /* rop */
	{ 0x0012, NVOBJ_ENGINE_GR, NULL }, /* beta1 */
	{ 0x0072, NVOBJ_ENGINE_GR, NULL }, /* beta4 */
	{ 0x0019, NVOBJ_ENGINE_GR, NULL }, /* cliprect */
	{ 0x0044, NVOBJ_ENGINE_GR, NULL }, /* pattern */
	{ 0x009e, NVOBJ_ENGINE_GR, NULL }, /* swzsurf */
	{ 0x0096, NVOBJ_ENGINE_GR, NULL }, /* celcius */
	{ 0x0097, NVOBJ_ENGINE_GR, NULL }, /* kelvin (nv20) */
	{ 0x0597, NVOBJ_ENGINE_GR, NULL }, /* kelvin (nv25) */
	{}
};

struct nouveau_pgraph_object_class nv30_graph_grclass[] = {
	{ 0x0030, NVOBJ_ENGINE_GR, NULL }, /* null */
	{ 0x0039, NVOBJ_ENGINE_GR, NULL }, /* m2mf */
	{ 0x004a, NVOBJ_ENGINE_GR, NULL }, /* gdirect */
	{ 0x009f, NVOBJ_ENGINE_GR, NULL }, /* imageblit (nv12) */
	{ 0x008a, NVOBJ_ENGINE_GR, NULL }, /* ifc */
	{ 0x038a, NVOBJ_ENGINE_GR, NULL }, /* ifc (nv30) */
	{ 0x0089, NVOBJ_ENGINE_GR, NULL }, /* sifm */
	{ 0x0389, NVOBJ_ENGINE_GR, NULL }, /* sifm (nv30) */
	{ 0x0062, NVOBJ_ENGINE_GR, NULL }, /* surf2d */
	{ 0x0362, NVOBJ_ENGINE_GR, NULL }, /* surf2d (nv30) */
	{ 0x0043, NVOBJ_ENGINE_GR, NULL }, /* rop */
	{ 0x0012, NVOBJ_ENGINE_GR, NULL }, /* beta1 */
	{ 0x0072, NVOBJ_ENGINE_GR, NULL }, /* beta4 */
	{ 0x0019, NVOBJ_ENGINE_GR, NULL }, /* cliprect */
	{ 0x0044, NVOBJ_ENGINE_GR, NULL }, /* pattern */
	{ 0x039e, NVOBJ_ENGINE_GR, NULL }, /* swzsurf */
	{ 0x0397, NVOBJ_ENGINE_GR, NULL }, /* rankine (nv30) */
	{ 0x0497, NVOBJ_ENGINE_GR, NULL }, /* rankine (nv35) */
	{ 0x0697, NVOBJ_ENGINE_GR, NULL }, /* rankine (nv34) */
	{}
};

