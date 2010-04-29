#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"

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
nv04_instmem_determine_amount(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i;

	/* Figure out how much instance memory we need */
	if (dev_priv->card_type >= NV_40) {
		/* We'll want more instance memory than this on some NV4x cards.
		 * There's a 16MB aperture to play with that maps onto the end
		 * of vram.  For now, only reserve a small piece until we know
		 * more about what each chipset requires.
		 */
		switch (dev_priv->chipset) {
		case 0x40:
		case 0x47:
		case 0x49:
		case 0x4b:
			dev_priv->ramin_rsvd_vram = (2 * 1024 * 1024);
			break;
		default:
			dev_priv->ramin_rsvd_vram = (1 * 1024 * 1024);
			break;
		}
	} else {
		/*XXX: what *are* the limits on <NV40 cards?
		 */
		dev_priv->ramin_rsvd_vram = (512 * 1024);
	}
	NV_DEBUG(dev, "RAMIN size: %dKiB\n", dev_priv->ramin_rsvd_vram >> 10);

	/* Clear all of it, except the BIOS image that's in the first 64KiB */
	dev_priv->engine.instmem.prepare_access(dev, true);
	for (i = 64 * 1024; i < dev_priv->ramin_rsvd_vram; i += 4)
		nv_wi32(dev, i, 0x00000000);
	dev_priv->engine.instmem.finish_access(dev);
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
	uint32_t offset;
	int ret = 0;

	nv04_instmem_determine_amount(dev);
	nv04_instmem_configure_fixed_tables(dev);

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

	ret = nouveau_mem_init_heap(&dev_priv->ramin_heap,
				    offset, dev_priv->ramin_rsvd_vram - offset);
	if (ret) {
		dev_priv->ramin_heap = NULL;
		NV_ERROR(dev, "Failed to init RAMIN heap\n");
	}

	return ret;
}

void
nv04_instmem_takedown(struct drm_device *dev)
{
}

int
nv04_instmem_populate(struct drm_device *dev, struct nouveau_gpuobj *gpuobj, uint32_t *sz)
{
	if (gpuobj->im_backing)
		return -EINVAL;

	return 0;
}

void
nv04_instmem_clear(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (gpuobj && gpuobj->im_backing) {
		if (gpuobj->im_bound)
			dev_priv->engine.instmem.unbind(dev, gpuobj);
		gpuobj->im_backing = NULL;
	}
}

int
nv04_instmem_bind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	if (!gpuobj->im_pramin || gpuobj->im_bound)
		return -EINVAL;

	gpuobj->im_bound = 1;
	return 0;
}

int
nv04_instmem_unbind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	if (gpuobj->im_bound == 0)
		return -EINVAL;

	gpuobj->im_bound = 0;
	return 0;
}

void
nv04_instmem_prepare_access(struct drm_device *dev, bool write)
{
}

void
nv04_instmem_finish_access(struct drm_device *dev)
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

