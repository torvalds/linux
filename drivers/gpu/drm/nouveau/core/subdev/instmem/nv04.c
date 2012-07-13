#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"
#include <engine/fifo.h>
#include <core/ramht.h>

#include "nv04.h"

int
nv04_instmem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_instmem_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_priv->engine.instmem.priv = priv;

	/* PRAMIN aperture maps over the end of vram, reserve the space */
	dev_priv->ramin_available = true;
	dev_priv->ramin_rsvd_vram = 512 * 1024;

	ret = drm_mm_init(&dev_priv->ramin_heap, 0, dev_priv->ramin_rsvd_vram);
	if (ret)
		return ret;

	/* 0x00000-0x10000: reserve for probable vbios image */
	ret = nouveau_gpuobj_new(dev, NULL, 0x10000, 0, 0, &priv->vbios);
	if (ret)
		return ret;

	/* 0x10000-0x18000: reserve for RAMHT */
	ret = nouveau_gpuobj_new(dev, NULL, 0x08000, 0, NVOBJ_FLAG_ZERO_ALLOC,
				&priv->ramht);
	if (ret)
		return ret;

	/* 0x18000-0x18200: reserve for RAMRO */
	ret = nouveau_gpuobj_new(dev, NULL, 0x00200, 0, 0, &priv->ramro);
	if (ret)
		return ret;

	/* 0x18200-0x18a00: reserve for RAMFC (enough for 32 nv30 channels) */
	ret = nouveau_gpuobj_new(dev, NULL, 0x00800, 0, NVOBJ_FLAG_ZERO_ALLOC,
				&priv->ramfc);
	if (ret)
		return ret;

	ret = nouveau_ramht_new(dev, priv->ramht, &dev_priv->ramht);
	if (ret)
		return ret;

	return 0;
}

void
nv04_instmem_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_instmem_priv *priv = dev_priv->engine.instmem.priv;

	nouveau_ramht_ref(NULL, &dev_priv->ramht, NULL);
	nouveau_gpuobj_ref(NULL, &priv->ramfc);
	nouveau_gpuobj_ref(NULL, &priv->ramro);
	nouveau_gpuobj_ref(NULL, &priv->ramht);

	if (drm_mm_initialized(&dev_priv->ramin_heap))
		drm_mm_takedown(&dev_priv->ramin_heap);

	kfree(priv);
	dev_priv->engine.instmem.priv = NULL;
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
nv04_instmem_get(struct nouveau_gpuobj *gpuobj, struct nouveau_channel *chan,
		 u32 size, u32 align)
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
