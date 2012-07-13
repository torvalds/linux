#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"
#include <engine/fifo.h>
#include <core/ramht.h>

#include "nv04.h"

int nv40_instmem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_instmem_priv *priv;
	u32 vs, rsvd;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_priv->engine.instmem.priv = priv;

	/* PRAMIN aperture maps over the end of vram, reserve enough space
	 * to fit graphics contexts for every channel, the magics come
	 * from engine/graph/nv40.c
	 */
	vs = hweight8((nv_rd32(dev, 0x001540) & 0x0000ff00) >> 8);
	if      (dev_priv->chipset == 0x40) rsvd = 0x6aa0 * vs;
	else if (dev_priv->chipset  < 0x43) rsvd = 0x4f00 * vs;
	else if (nv44_graph_class(dev))	    rsvd = 0x4980 * vs;
	else				    rsvd = 0x4a40 * vs;
	rsvd += 16 * 1024;
	rsvd *= 32;		/* per-channel */
	rsvd += 512 * 1024;	/* pci(e)gart table */
	rsvd += 512 * 1024;	/* object storage */
	dev_priv->ramin_rsvd_vram = round_up(rsvd, 4096);
	dev_priv->ramin_available = true;

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

	/* 0x18000-0x18200: reserve for RAMRO
	 * 0x18200-0x20000: padding
	 */
	ret = nouveau_gpuobj_new(dev, NULL, 0x08000, 0, 0, &priv->ramro);
	if (ret)
		return ret;

	/* 0x20000-0x21000: reserve for RAMFC
	 * 0x21000-0x40000: padding + some unknown stuff (see below)
	 *
	 * It appears something is controlled by 0x2220/0x2230 on certain
	 * NV4x chipsets as well as RAMFC.  When 0x2230 == 0 ("new style"
	 * control) the upper 16-bits of 0x2220 points at this other
	 * mysterious table that's clobbering important things.
	 *
	 * We're now pointing this at RAMIN+0x30000 to avoid RAMFC getting
	 * smashed to pieces on us, so reserve 0x30000-0x40000 too..
	 */
	ret = nouveau_gpuobj_new(dev, NULL, 0x20000, 0, NVOBJ_FLAG_ZERO_ALLOC,
				&priv->ramfc);
	if (ret)
		return ret;

	ret = nouveau_ramht_new(dev, priv->ramht, &dev_priv->ramht);
	if (ret)
		return ret;

	return 0;
}

void
nv40_instmem_takedown(struct drm_device *dev)
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
nv40_instmem_suspend(struct drm_device *dev)
{
	return 0;
}

void
nv40_instmem_resume(struct drm_device *dev)
{
}

int
nv40_instmem_get(struct nouveau_gpuobj *gpuobj, struct nouveau_channel *chan,
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
nv40_instmem_put(struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;

	spin_lock(&dev_priv->ramin_lock);
	drm_mm_put_block(gpuobj->node);
	gpuobj->node = NULL;
	spin_unlock(&dev_priv->ramin_lock);
}

int
nv40_instmem_map(struct nouveau_gpuobj *gpuobj)
{
	gpuobj->pinst = gpuobj->vinst;
	return 0;
}

void
nv40_instmem_unmap(struct nouveau_gpuobj *gpuobj)
{
}

void
nv40_instmem_flush(struct drm_device *dev)
{
}
