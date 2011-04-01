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

struct nv20_graph_engine {
	struct nouveau_exec_engine base;
	struct nouveau_gpuobj *ctxtab;
	void (*grctx_init)(struct nouveau_gpuobj *);
	u32 grctx_size;
	u32 grctx_user;
};

#define NV20_GRCTX_SIZE (3580*4)
#define NV25_GRCTX_SIZE (3529*4)
#define NV2A_GRCTX_SIZE (3500*4)

#define NV30_31_GRCTX_SIZE (24392)
#define NV34_GRCTX_SIZE    (18140)
#define NV35_36_GRCTX_SIZE (22396)

int
nv20_graph_unload_context(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_channel *chan;
	struct nouveau_gpuobj *grctx;
	u32 tmp;

	chan = nv10_graph_channel(dev);
	if (!chan)
		return 0;
	grctx = chan->engctx[NVOBJ_ENGINE_GR];

	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_POINTER, grctx->pinst >> 4);
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

static void
nv20_graph_context_init(struct nouveau_gpuobj *ctx)
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
nv25_graph_context_init(struct nouveau_gpuobj *ctx)
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
nv2a_graph_context_init(struct nouveau_gpuobj *ctx)
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
nv30_31_graph_context_init(struct nouveau_gpuobj *ctx)
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
nv34_graph_context_init(struct nouveau_gpuobj *ctx)
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
nv35_36_graph_context_init(struct nouveau_gpuobj *ctx)
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
nv20_graph_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv20_graph_engine *pgraph = nv_engine(chan->dev, engine);
	struct nouveau_gpuobj *grctx = NULL;
	struct drm_device *dev = chan->dev;
	int ret;

	ret = nouveau_gpuobj_new(dev, NULL, pgraph->grctx_size, 16,
				 NVOBJ_FLAG_ZERO_ALLOC, &grctx);
	if (ret)
		return ret;

	/* Initialise default context values */
	pgraph->grctx_init(grctx);

	/* nv20: nv_wo32(dev, chan->ramin_grctx->gpuobj, 10, chan->id<<24); */
	/* CTX_USER */
	nv_wo32(grctx, pgraph->grctx_user, (chan->id << 24) | 0x1);

	nv_wo32(pgraph->ctxtab, chan->id * 4, grctx->pinst >> 4);
	chan->engctx[engine] = grctx;
	return 0;
}

void
nv20_graph_context_del(struct nouveau_channel *chan, int engine)
{
	struct nv20_graph_engine *pgraph = nv_engine(chan->dev, engine);
	struct nouveau_gpuobj *grctx = chan->engctx[engine];
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv04_graph_fifo_access(dev, false);

	/* Unload the context if it's the currently active one */
	if (nv10_graph_channel(dev) == chan)
		nv20_graph_unload_context(dev);

	nv04_graph_fifo_access(dev, true);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	/* Free the context resources */
	nv_wo32(pgraph->ctxtab, chan->id * 4, 0);

	nouveau_gpuobj_ref(NULL, &grctx);
	chan->engctx[engine] = NULL;
}

static void
nv20_graph_set_tile_region(struct drm_device *dev, int i)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_tile_reg *tile = &dev_priv->tile.reg[i];

	nv_wr32(dev, NV20_PGRAPH_TLIMIT(i), tile->limit);
	nv_wr32(dev, NV20_PGRAPH_TSIZE(i), tile->pitch);
	nv_wr32(dev, NV20_PGRAPH_TILE(i), tile->addr);

	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0030 + 4 * i);
	nv_wr32(dev, NV10_PGRAPH_RDI_DATA, tile->limit);
	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0050 + 4 * i);
	nv_wr32(dev, NV10_PGRAPH_RDI_DATA, tile->pitch);
	nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00EA0010 + 4 * i);
	nv_wr32(dev, NV10_PGRAPH_RDI_DATA, tile->addr);

	if (dev_priv->card_type == NV_20) {
		nv_wr32(dev, NV20_PGRAPH_ZCOMP(i), tile->zcomp);
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00ea0090 + 4 * i);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA, tile->zcomp);
	}
}

int
nv20_graph_init(struct drm_device *dev, int engine)
{
	struct nv20_graph_engine *pgraph = nv_engine(dev, engine);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t tmp, vramsz;
	int i;

	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) & ~NV_PMC_ENABLE_PGRAPH);
	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) |  NV_PMC_ENABLE_PGRAPH);

	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_TABLE, pgraph->ctxtab->pinst >> 4);

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
		nv_wr32(dev, 0x400890, 0x00a8cfff);
		nv_wr32(dev, 0x400610, 0x304B1FB6);
		nv_wr32(dev, 0x400B80, 0x1cbd3883);
		nv_wr32(dev, 0x400B84, 0x44000000);
		nv_wr32(dev, 0x400098, 0x40000080);
		nv_wr32(dev, 0x400B88, 0x000000ff);

	} else {
		nv_wr32(dev, 0x400880, 0x0008c7df);
		nv_wr32(dev, 0x400094, 0x00000005);
		nv_wr32(dev, 0x400B80, 0x45eae20e);
		nv_wr32(dev, 0x400B84, 0x24000000);
		nv_wr32(dev, 0x400098, 0x00000040);
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00E00038);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA , 0x00000030);
		nv_wr32(dev, NV10_PGRAPH_RDI_INDEX, 0x00E10038);
		nv_wr32(dev, NV10_PGRAPH_RDI_DATA , 0x00000030);
	}

	/* Turn all the tiling regions off. */
	for (i = 0; i < NV10_PFB_TILE__SIZE; i++)
		nv20_graph_set_tile_region(dev, i);

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

int
nv30_graph_init(struct drm_device *dev, int engine)
{
	struct nv20_graph_engine *pgraph = nv_engine(dev, engine);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i;

	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) & ~NV_PMC_ENABLE_PGRAPH);
	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) |  NV_PMC_ENABLE_PGRAPH);

	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_TABLE, pgraph->ctxtab->pinst >> 4);

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
		nv20_graph_set_tile_region(dev, i);

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

int
nv20_graph_fini(struct drm_device *dev, int engine)
{
	nv20_graph_unload_context(dev);
	nv_wr32(dev, NV03_PGRAPH_INTR_EN, 0x00000000);
	return 0;
}

static void
nv20_graph_isr(struct drm_device *dev)
{
	u32 stat;

	while ((stat = nv_rd32(dev, NV03_PGRAPH_INTR))) {
		u32 nsource = nv_rd32(dev, NV03_PGRAPH_NSOURCE);
		u32 nstatus = nv_rd32(dev, NV03_PGRAPH_NSTATUS);
		u32 addr = nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR);
		u32 chid = (addr & 0x01f00000) >> 20;
		u32 subc = (addr & 0x00070000) >> 16;
		u32 mthd = (addr & 0x00001ffc);
		u32 data = nv_rd32(dev, NV04_PGRAPH_TRAPPED_DATA);
		u32 class = nv_rd32(dev, 0x400160 + subc * 4) & 0xfff;
		u32 show = stat;

		if (stat & NV_PGRAPH_INTR_ERROR) {
			if (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD) {
				if (!nouveau_gpuobj_mthd_call2(dev, chid, class, mthd, data))
					show &= ~NV_PGRAPH_INTR_ERROR;
			}
		}

		nv_wr32(dev, NV03_PGRAPH_INTR, stat);
		nv_wr32(dev, NV04_PGRAPH_FIFO, 0x00000001);

		if (show && nouveau_ratelimit()) {
			NV_INFO(dev, "PGRAPH -");
			nouveau_bitfield_print(nv10_graph_intr, show);
			printk(" nsource:");
			nouveau_bitfield_print(nv04_graph_nsource, nsource);
			printk(" nstatus:");
			nouveau_bitfield_print(nv10_graph_nstatus, nstatus);
			printk("\n");
			NV_INFO(dev, "PGRAPH - ch %d/%d class 0x%04x "
				     "mthd 0x%04x data 0x%08x\n",
				chid, subc, class, mthd, data);
		}
	}
}

static void
nv20_graph_destroy(struct drm_device *dev, int engine)
{
	struct nv20_graph_engine *pgraph = nv_engine(dev, engine);

	nouveau_irq_unregister(dev, 12);
	nouveau_gpuobj_ref(NULL, &pgraph->ctxtab);

	NVOBJ_ENGINE_DEL(dev, GR);
	kfree(pgraph);
}

int
nv20_graph_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv20_graph_engine *pgraph;
	int ret;

	pgraph = kzalloc(sizeof(*pgraph), GFP_KERNEL);
	if (!pgraph)
		return -ENOMEM;

	pgraph->base.destroy = nv20_graph_destroy;
	pgraph->base.fini = nv20_graph_fini;
	pgraph->base.context_new = nv20_graph_context_new;
	pgraph->base.context_del = nv20_graph_context_del;
	pgraph->base.object_new = nv04_graph_object_new;
	pgraph->base.set_tile_region = nv20_graph_set_tile_region;

	pgraph->grctx_user = 0x0028;
	if (dev_priv->card_type == NV_20) {
		pgraph->base.init = nv20_graph_init;
		switch (dev_priv->chipset) {
		case 0x20:
			pgraph->grctx_init = nv20_graph_context_init;
			pgraph->grctx_size = NV20_GRCTX_SIZE;
			pgraph->grctx_user = 0x0000;
			break;
		case 0x25:
		case 0x28:
			pgraph->grctx_init = nv25_graph_context_init;
			pgraph->grctx_size = NV25_GRCTX_SIZE;
			break;
		case 0x2a:
			pgraph->grctx_init = nv2a_graph_context_init;
			pgraph->grctx_size = NV2A_GRCTX_SIZE;
			pgraph->grctx_user = 0x0000;
			break;
		default:
			NV_ERROR(dev, "PGRAPH: unknown chipset\n");
			return 0;
		}
	} else {
		pgraph->base.init = nv30_graph_init;
		switch (dev_priv->chipset) {
		case 0x30:
		case 0x31:
			pgraph->grctx_init = nv30_31_graph_context_init;
			pgraph->grctx_size = NV30_31_GRCTX_SIZE;
			break;
		case 0x34:
			pgraph->grctx_init = nv34_graph_context_init;
			pgraph->grctx_size = NV34_GRCTX_SIZE;
			break;
		case 0x35:
		case 0x36:
			pgraph->grctx_init = nv35_36_graph_context_init;
			pgraph->grctx_size = NV35_36_GRCTX_SIZE;
			break;
		default:
			NV_ERROR(dev, "PGRAPH: unknown chipset\n");
			return 0;
		}
	}

	/* Create Context Pointer Table */
	ret = nouveau_gpuobj_new(dev, NULL, 32 * 4, 16, NVOBJ_FLAG_ZERO_ALLOC,
				 &pgraph->ctxtab);
	if (ret) {
		kfree(pgraph);
		return ret;
	}

	NVOBJ_ENGINE_ADD(dev, GR, &pgraph->base);
	nouveau_irq_register(dev, 12, nv20_graph_isr);

	/* nvsw */
	NVOBJ_CLASS(dev, 0x506e, SW);
	NVOBJ_MTHD (dev, 0x506e, 0x0500, nv04_graph_mthd_page_flip);

	NVOBJ_CLASS(dev, 0x0030, GR); /* null */
	NVOBJ_CLASS(dev, 0x0039, GR); /* m2mf */
	NVOBJ_CLASS(dev, 0x004a, GR); /* gdirect */
	NVOBJ_CLASS(dev, 0x009f, GR); /* imageblit (nv12) */
	NVOBJ_CLASS(dev, 0x008a, GR); /* ifc */
	NVOBJ_CLASS(dev, 0x0089, GR); /* sifm */
	NVOBJ_CLASS(dev, 0x0062, GR); /* surf2d */
	NVOBJ_CLASS(dev, 0x0043, GR); /* rop */
	NVOBJ_CLASS(dev, 0x0012, GR); /* beta1 */
	NVOBJ_CLASS(dev, 0x0072, GR); /* beta4 */
	NVOBJ_CLASS(dev, 0x0019, GR); /* cliprect */
	NVOBJ_CLASS(dev, 0x0044, GR); /* pattern */
	if (dev_priv->card_type == NV_20) {
		NVOBJ_CLASS(dev, 0x009e, GR); /* swzsurf */
		NVOBJ_CLASS(dev, 0x0096, GR); /* celcius */

		/* kelvin */
		if (dev_priv->chipset < 0x25)
			NVOBJ_CLASS(dev, 0x0097, GR);
		else
			NVOBJ_CLASS(dev, 0x0597, GR);
	} else {
		NVOBJ_CLASS(dev, 0x038a, GR); /* ifc (nv30) */
		NVOBJ_CLASS(dev, 0x0389, GR); /* sifm (nv30) */
		NVOBJ_CLASS(dev, 0x0362, GR); /* surf2d (nv30) */
		NVOBJ_CLASS(dev, 0x039e, GR); /* swzsurf */

		/* rankine */
		if (0x00000003 & (1 << (dev_priv->chipset & 0x0f)))
			NVOBJ_CLASS(dev, 0x0397, GR);
		else
		if (0x00000010 & (1 << (dev_priv->chipset & 0x0f)))
			NVOBJ_CLASS(dev, 0x0697, GR);
		else
		if (0x000001e0 & (1 << (dev_priv->chipset & 0x0f)))
			NVOBJ_CLASS(dev, 0x0497, GR);
	}

	return 0;
}
