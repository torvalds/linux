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

