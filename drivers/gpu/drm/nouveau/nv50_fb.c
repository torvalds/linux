#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv50_fb_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/* Not a clue what this is exactly.  Without pointing it at a
	 * scratch page, VRAM->GART blits with M2MF (as in DDX DFS)
	 * cause IOMMU "read from address 0" errors (rh#561267)
	 */
	nv_wr32(dev, 0x100c08, dev_priv->gart_info.sg_dummy_bus >> 8);

	/* This is needed to get meaningful information from 100c90
	 * on traps. No idea what these values mean exactly. */
	switch (dev_priv->chipset) {
	case 0x50:
		nv_wr32(dev, 0x100c90, 0x0707ff);
		break;
	case 0xa3:
	case 0xa5:
	case 0xa8:
		nv_wr32(dev, 0x100c90, 0x0d0fff);
		break;
	default:
		nv_wr32(dev, 0x100c90, 0x1d07ff);
		break;
	}

	return 0;
}

void
nv50_fb_takedown(struct drm_device *dev)
{
}

void
nv50_fb_vm_trap(struct drm_device *dev, int display, const char *name)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 trap[6], idx, chinst;
	int i, ch;

	idx = nv_rd32(dev, 0x100c90);
	if (!(idx & 0x80000000))
		return;
	idx &= 0x00ffffff;

	for (i = 0; i < 6; i++) {
		nv_wr32(dev, 0x100c90, idx | i << 24);
		trap[i] = nv_rd32(dev, 0x100c94);
	}
	nv_wr32(dev, 0x100c90, idx | 0x80000000);

	if (!display)
		return;

	chinst = (trap[2] << 16) | trap[1];
	for (ch = 0; ch < dev_priv->engine.fifo.channels; ch++) {
		struct nouveau_channel *chan = dev_priv->fifos[ch];

		if (!chan || !chan->ramin)
			continue;

		if (chinst == chan->ramin->vinst >> 12)
			break;
	}

	NV_INFO(dev, "%s - VM: Trapped %s at %02x%04x%04x status %08x "
		     "channel %d (0x%08x)\n",
		name, (trap[5] & 0x100 ? "read" : "write"),
		trap[5] & 0xff, trap[4] & 0xffff, trap[3] & 0xffff,
		trap[0], ch, chinst);
}
