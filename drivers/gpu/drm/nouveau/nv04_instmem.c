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

int nv04_instmem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramht = NULL;
	u32 offset, length;
	int ret;

	/* RAMIN always available */
	dev_priv->ramin_available = true;

	/* Setup shared RAMHT */
	ret = nouveau_gpuobj_new_fake(dev, 0x10000, ~0, 4096,
				      NVOBJ_FLAG_ZERO_ALLOC, &ramht);
	if (ret)
		return ret;

	ret = nouveau_ramht_new(dev, ramht, &dev_priv->ramht);
	nouveau_gpuobj_ref(NULL, &ramht);
	if (ret)
		return ret;

	/* And RAMRO */
	ret = nouveau_gpuobj_new_fake(dev, 0x11200, ~0, 512,
				      NVOBJ_FLAG_ZERO_ALLOC, &dev_priv->ramro);
	if (ret)
		return ret;

	/* And RAMFC */
	length = dev_priv->engine.fifo.channels * nouveau_fifo_ctx_size(dev);
	switch (dev_priv->card_type) {
	case NV_40:
		offset = 0x20000;
		break;
	default:
		offset = 0x11400;
		break;
	}

	ret = nouveau_gpuobj_new_fake(dev, offset, ~0, length,
				      NVOBJ_FLAG_ZERO_ALLOC, &dev_priv->ramfc);
	if (ret)
		return ret;

	/* Only allow space after RAMFC to be used for object allocation */
	offset += length;

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

	return 0;
}

void
nv04_instmem_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nouveau_ramht_ref(NULL, &dev_priv->ramht, NULL);
	nouveau_gpuobj_ref(NULL, &dev_priv->ramro);
	nouveau_gpuobj_ref(NULL, &dev_priv->ramfc);

	if (drm_mm_initialized(&dev_priv->ramin_heap))
		drm_mm_takedown(&dev_priv->ramin_heap);
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

int
nv04_instmem_get(struct nouveau_gpuobj *gpuobj, u32 size, u32 align)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;
	struct drm_mm_node *ramin = NULL;

	do {
		if (drm_mm_pre_get(&dev_priv->ramin_heap))
			return -ENOMEM;

		spin_lock(&dev_priv->ramin_lock);
		ramin = drm_mm_search_free(&dev_priv->ramin_heap, size, align, 0);
		if (ramin == NULL) {
			spin_unlock(&dev_priv->ramin_lock);
			return -ENOMEM;
		}

		ramin = drm_mm_get_block_atomic(ramin, size, align);
		spin_unlock(&dev_priv->ramin_lock);
	} while (ramin == NULL);

	gpuobj->node  = ramin;
	gpuobj->vinst = ramin->start;
	return 0;
}

void
nv04_instmem_put(struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;

	spin_lock(&dev_priv->ramin_lock);
	drm_mm_put_block(gpuobj->node);
	gpuobj->node = NULL;
	spin_unlock(&dev_priv->ramin_lock);
}

int
nv04_instmem_map(struct nouveau_gpuobj *gpuobj)
{
	gpuobj->pinst = gpuobj->vinst;
	return 0;
}

void
nv04_instmem_unmap(struct nouveau_gpuobj *gpuobj)
{
}

void
nv04_instmem_flush(struct drm_device *dev)
{
}
