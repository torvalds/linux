#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

void
nv40_fb_set_tile_region(struct drm_device *dev, int i)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_tile_reg *tile = &dev_priv->tile.reg[i];

	switch (dev_priv->chipset) {
	case 0x40:
		nv_wr32(dev, NV10_PFB_TLIMIT(i), tile->limit);
		nv_wr32(dev, NV10_PFB_TSIZE(i), tile->pitch);
		nv_wr32(dev, NV10_PFB_TILE(i), tile->addr);
		break;

	default:
		nv_wr32(dev, NV40_PFB_TLIMIT(i), tile->limit);
		nv_wr32(dev, NV40_PFB_TSIZE(i), tile->pitch);
		nv_wr32(dev, NV40_PFB_TILE(i), tile->addr);
		break;
	}
}

static void
nv40_fb_init_gart(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gart = dev_priv->gart_info.sg_ctxdma;

	if (dev_priv->gart_info.type != NOUVEAU_GART_HW) {
		nv_wr32(dev, 0x100800, 0x00000001);
		return;
	}

	nv_wr32(dev, 0x100800, gart->pinst | 0x00000002);
	nv_mask(dev, 0x10008c, 0x00000100, 0x00000100);
	nv_wr32(dev, 0x100820, 0x00000000);
}

static void
nv44_fb_init_gart(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gart = dev_priv->gart_info.sg_ctxdma;
	u32 vinst;

	if (dev_priv->gart_info.type != NOUVEAU_GART_HW) {
		nv_wr32(dev, 0x100850, 0x80000000);
		nv_wr32(dev, 0x100800, 0x00000001);
		return;
	}

	/* calculate vram address of this PRAMIN block, object
	 * must be allocated on 512KiB alignment, and not exceed
	 * a total size of 512KiB for this to work correctly
	 */
	vinst  = nv_rd32(dev, 0x10020c);
	vinst -= ((gart->pinst >> 19) + 1) << 19;

	nv_wr32(dev, 0x100850, 0x80000000);
	nv_wr32(dev, 0x100818, dev_priv->gart_info.dummy.addr);

	nv_wr32(dev, 0x100804, dev_priv->gart_info.aper_size);
	nv_wr32(dev, 0x100850, 0x00008000);
	nv_mask(dev, 0x10008c, 0x00000200, 0x00000200);
	nv_wr32(dev, 0x100820, 0x00000000);
	nv_wr32(dev, 0x10082c, 0x00000001);
	nv_wr32(dev, 0x100800, vinst | 0x00000010);
}

int
nv40_fb_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	uint32_t tmp;
	int i;

	if (dev_priv->chipset != 0x40 && dev_priv->chipset != 0x45) {
		if (nv44_graph_class(dev))
			nv44_fb_init_gart(dev);
		else
			nv40_fb_init_gart(dev);
	}

	switch (dev_priv->chipset) {
	case 0x40:
	case 0x45:
		tmp = nv_rd32(dev, NV10_PFB_CLOSE_PAGE2);
		nv_wr32(dev, NV10_PFB_CLOSE_PAGE2, tmp & ~(1 << 15));
		pfb->num_tiles = NV10_PFB_TILE__SIZE;
		break;
	case 0x46: /* G72 */
	case 0x47: /* G70 */
	case 0x49: /* G71 */
	case 0x4b: /* G73 */
	case 0x4c: /* C51 (G7X version) */
		pfb->num_tiles = NV40_PFB_TILE__SIZE_1;
		break;
	default:
		pfb->num_tiles = NV40_PFB_TILE__SIZE_0;
		break;
	}

	/* Turn all the tiling regions off. */
	for (i = 0; i < pfb->num_tiles; i++)
		pfb->set_tile_region(dev, i);

	return 0;
}

void
nv40_fb_takedown(struct drm_device *dev)
{
}
