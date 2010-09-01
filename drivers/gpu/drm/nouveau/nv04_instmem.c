#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_ramht.h"

/* returns the size of fifo context */
static int
nouveau_fifo_ctx_size(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->chipset >= 0x40)
		return 128;
	else
	if (dev_priv->chipset >= 0x17)
		return 64;

	return 32;
}

static void
nv04_instmem_configure_fixed_tables(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;

	/* FIFO hash table (RAMHT)
	 *   use 4k hash table at RAMIN+0x10000
	 *   TODO: extend the hash table
	 */
	dev_priv->ramht_offset = 0x10000;
	dev_priv->ramht_bits   = 9;
	dev_priv->ramht_size   = (1 << dev_priv->ramht_bits); /* nr entries */
	dev_priv->ramht_size  *= 8; /* 2 32-bit values per entry in RAMHT */
	NV_DEBUG(dev, "RAMHT offset=0x%x, size=%d\n", dev_priv->ramht_offset,
						      dev_priv->ramht_size);

	/* FIFO runout table (RAMRO) - 512k at 0x11200 */
	dev_priv->ramro_offset = 0x11200;
	dev_priv->ramro_size   = 512;
	NV_DEBUG(dev, "RAMRO offset=0x%x, size=%d\n", dev_priv->ramro_offset,
						      dev_priv->ramro_size);

	/* FIFO context table (RAMFC)
	 *   NV40  : Not sure exactly how to position RAMFC on some cards,
	 *           0x30002 seems to position it at RAMIN+0x20000 on these
	 *           cards.  RAMFC is 4kb (32 fifos, 128byte entries).
	 *   Others: Position RAMFC at RAMIN+0x11400
	 */
	dev_priv->ramfc_size = engine->fifo.channels *
						nouveau_fifo_ctx_size(dev);
	switch (dev_priv->card_type) {
	case NV_40:
		dev_priv->ramfc_offset = 0x20000;
		break;
	case NV_30:
	case NV_20:
	case NV_10:
	case NV_04:
	default:
		dev_priv->ramfc_offset = 0x11400;
		break;
	}
	NV_DEBUG(dev, "RAMFC offset=0x%x, size=%d\n", dev_priv->ramfc_offset,
						      dev_priv->ramfc_size);
}

int nv04_instmem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramht = NULL;
	uint32_t offset;
	int ret;

	nv04_instmem_configure_fixed_tables(dev);

	/* Setup shared RAMHT */
	ret = nouveau_gpuobj_new_fake(dev, dev_priv->ramht_offset, ~0,
				      dev_priv->ramht_size,
				      NVOBJ_FLAG_ZERO_ALLOC, &ramht);
	if (ret)
		return ret;

	ret = nouveau_ramht_new(dev, ramht, &dev_priv->ramht);
	nouveau_gpuobj_ref(NULL, &ramht);
	if (ret)
		return ret;

	/* Create a heap to manage RAMIN allocations, we don't allocate
	 * the space that was reserved for RAMHT/FC/RO.
	 */
	offset = dev_priv->ramfc_offset + dev_priv->ramfc_size;

	/* It appears RAMRO (or something?) is controlled by 0x2220/0x2230
	 * on certain NV4x chipsets as well as RAMFC.  When 0x2230 == 0
	 * ("new style" control) the upper 16-bits of 0x2220 points at this
	 * other mysterious table that's clobbering important things.
	 *
	 * We're now pointing this at RAMIN+0x30000 to avoid RAMFC getting
	 * smashed to pieces on us, so reserve 0x30000-0x40000 too..
	 */
	if (dev_priv->card_type >= NV_40) {
		if (offset < 0x40000)
			offset = 0x40000;
	}

	ret = drm_mm_init(&dev_priv->ramin_heap, offset,
			  dev_priv->ramin_rsvd_vram - offset);
	if (ret) {
		NV_ERROR(dev, "Failed to init RAMIN heap: %d\n", ret);
		return ret;
	}

	dev_priv->ramin_available = true;
	return 0;
}

void
nv04_instmem_takedown(struct drm_device *dev)
{
}

int
nv04_instmem_populate(struct drm_device *dev, struct nouveau_gpuobj *gpuobj,
		      uint32_t *sz)
{
	return 0;
}

void
nv04_instmem_clear(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
}

int
nv04_instmem_bind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	return 0;
}

int
nv04_instmem_unbind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	return 0;
}

void
nv04_instmem_flush(struct drm_device *dev)
{
}

int
nv04_instmem_suspend(struct drm_device *dev)
{
	return 0;
}

void
nv04_instmem_resume(struct drm_device *dev)
{
}

