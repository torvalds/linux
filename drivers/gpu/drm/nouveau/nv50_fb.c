#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

struct nv50_fb_priv {
	struct page *r100c08_page;
	dma_addr_t r100c08;
};

static void
nv50_fb_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	struct nv50_fb_priv *priv = pfb->priv;

	if (drm_mm_initialized(&pfb->tag_heap))
		drm_mm_takedown(&pfb->tag_heap);

	if (priv->r100c08_page) {
		pci_unmap_page(dev->pdev, priv->r100c08, PAGE_SIZE,
			       PCI_DMA_BIDIRECTIONAL);
		__free_page(priv->r100c08_page);
	}

	kfree(priv);
	pfb->priv = NULL;
}

static int
nv50_fb_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	struct nv50_fb_priv *priv;
	u32 tagmem;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	pfb->priv = priv;

	priv->r100c08_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!priv->r100c08_page) {
		nv50_fb_destroy(dev);
		return -ENOMEM;
	}

	priv->r100c08 = pci_map_page(dev->pdev, priv->r100c08_page, 0,
				     PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(dev->pdev, priv->r100c08)) {
		nv50_fb_destroy(dev);
		return -EFAULT;
	}

	tagmem = nv_rd32(dev, 0x100320);
	NV_DEBUG(dev, "%d tags available\n", tagmem);
	ret = drm_mm_init(&pfb->tag_heap, 0, tagmem);
	if (ret) {
		nv50_fb_destroy(dev);
		return ret;
	}

	return 0;
}

int
nv50_fb_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_fb_priv *priv;
	int ret;

	if (!dev_priv->engine.fb.priv) {
		ret = nv50_fb_create(dev);
		if (ret)
			return ret;
	}
	priv = dev_priv->engine.fb.priv;

	/* Not a clue what this is exactly.  Without pointing it at a
	 * scratch page, VRAM->GART blits with M2MF (as in DDX DFS)
	 * cause IOMMU "read from address 0" errors (rh#561267)
	 */
	nv_wr32(dev, 0x100c08, priv->r100c08 >> 8);

	/* This is needed to get meaningful information from 100c90
	 * on traps. No idea what these values mean exactly. */
	switch (dev_priv->chipset) {
	case 0x50:
		nv_wr32(dev, 0x100c90, 0x000707ff);
		break;
	case 0xa3:
	case 0xa5:
	case 0xa8:
		nv_wr32(dev, 0x100c90, 0x000d0fff);
		break;
	case 0xaf:
		nv_wr32(dev, 0x100c90, 0x089d1fff);
		break;
	default:
		nv_wr32(dev, 0x100c90, 0x001d07ff);
		break;
	}

	return 0;
}

void
nv50_fb_takedown(struct drm_device *dev)
{
	nv50_fb_destroy(dev);
}

static struct nouveau_enum vm_dispatch_subclients[] = {
	{ 0x00000000, "GRCTX", NULL },
	{ 0x00000001, "NOTIFY", NULL },
	{ 0x00000002, "QUERY", NULL },
	{ 0x00000003, "COND", NULL },
	{ 0x00000004, "M2M_IN", NULL },
	{ 0x00000005, "M2M_OUT", NULL },
	{ 0x00000006, "M2M_NOTIFY", NULL },
	{}
};

static struct nouveau_enum vm_ccache_subclients[] = {
	{ 0x00000000, "CB", NULL },
	{ 0x00000001, "TIC", NULL },
	{ 0x00000002, "TSC", NULL },
	{}
};

static struct nouveau_enum vm_prop_subclients[] = {
	{ 0x00000000, "RT0", NULL },
	{ 0x00000001, "RT1", NULL },
	{ 0x00000002, "RT2", NULL },
	{ 0x00000003, "RT3", NULL },
	{ 0x00000004, "RT4", NULL },
	{ 0x00000005, "RT5", NULL },
	{ 0x00000006, "RT6", NULL },
	{ 0x00000007, "RT7", NULL },
	{ 0x00000008, "ZETA", NULL },
	{ 0x00000009, "LOCAL", NULL },
	{ 0x0000000a, "GLOBAL", NULL },
	{ 0x0000000b, "STACK", NULL },
	{ 0x0000000c, "DST2D", NULL },
	{}
};

static struct nouveau_enum vm_pfifo_subclients[] = {
	{ 0x00000000, "PUSHBUF", NULL },
	{ 0x00000001, "SEMAPHORE", NULL },
	{}
};

static struct nouveau_enum vm_bar_subclients[] = {
	{ 0x00000000, "FB", NULL },
	{ 0x00000001, "IN", NULL },
	{}
};

static struct nouveau_enum vm_client[] = {
	{ 0x00000000, "STRMOUT", NULL },
	{ 0x00000003, "DISPATCH", vm_dispatch_subclients },
	{ 0x00000004, "PFIFO_WRITE", NULL },
	{ 0x00000005, "CCACHE", vm_ccache_subclients },
	{ 0x00000006, "PPPP", NULL },
	{ 0x00000007, "CLIPID", NULL },
	{ 0x00000008, "PFIFO_READ", NULL },
	{ 0x00000009, "VFETCH", NULL },
	{ 0x0000000a, "TEXTURE", NULL },
	{ 0x0000000b, "PROP", vm_prop_subclients },
	{ 0x0000000c, "PVP", NULL },
	{ 0x0000000d, "PBSP", NULL },
	{ 0x0000000e, "PCRYPT", NULL },
	{ 0x0000000f, "PCOUNTER", NULL },
	{ 0x00000011, "PDAEMON", NULL },
	{}
};

static struct nouveau_enum vm_engine[] = {
	{ 0x00000000, "PGRAPH", NULL },
	{ 0x00000001, "PVP", NULL },
	{ 0x00000004, "PEEPHOLE", NULL },
	{ 0x00000005, "PFIFO", vm_pfifo_subclients },
	{ 0x00000006, "BAR", vm_bar_subclients },
	{ 0x00000008, "PPPP", NULL },
	{ 0x00000009, "PBSP", NULL },
	{ 0x0000000a, "PCRYPT", NULL },
	{ 0x0000000b, "PCOUNTER", NULL },
	{ 0x0000000c, "SEMAPHORE_BG", NULL },
	{ 0x0000000d, "PCOPY", NULL },
	{ 0x0000000e, "PDAEMON", NULL },
	{}
};

static struct nouveau_enum vm_fault[] = {
	{ 0x00000000, "PT_NOT_PRESENT", NULL },
	{ 0x00000001, "PT_TOO_SHORT", NULL },
	{ 0x00000002, "PAGE_NOT_PRESENT", NULL },
	{ 0x00000003, "PAGE_SYSTEM_ONLY", NULL },
	{ 0x00000004, "PAGE_READ_ONLY", NULL },
	{ 0x00000006, "NULL_DMAOBJ", NULL },
	{ 0x00000007, "WRONG_MEMTYPE", NULL },
	{ 0x0000000b, "VRAM_LIMIT", NULL },
	{ 0x0000000f, "DMAOBJ_LIMIT", NULL },
	{}
};

void
nv50_fb_vm_trap(struct drm_device *dev, int display)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const struct nouveau_enum *en, *cl;
	unsigned long flags;
	u32 trap[6], idx, chinst;
	u8 st0, st1, st2, st3;
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

	/* lookup channel id */
	chinst = (trap[2] << 16) | trap[1];
	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	for (ch = 0; ch < dev_priv->engine.fifo.channels; ch++) {
		struct nouveau_channel *chan = dev_priv->channels.ptr[ch];

		if (!chan || !chan->ramin)
			continue;

		if (chinst == chan->ramin->vinst >> 12)
			break;
	}
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);

	/* decode status bits into something more useful */
	if (dev_priv->chipset  < 0xa3 ||
	    dev_priv->chipset == 0xaa || dev_priv->chipset == 0xac) {
		st0 = (trap[0] & 0x0000000f) >> 0;
		st1 = (trap[0] & 0x000000f0) >> 4;
		st2 = (trap[0] & 0x00000f00) >> 8;
		st3 = (trap[0] & 0x0000f000) >> 12;
	} else {
		st0 = (trap[0] & 0x000000ff) >> 0;
		st1 = (trap[0] & 0x0000ff00) >> 8;
		st2 = (trap[0] & 0x00ff0000) >> 16;
		st3 = (trap[0] & 0xff000000) >> 24;
	}

	NV_INFO(dev, "VM: trapped %s at 0x%02x%04x%04x on ch %d [0x%08x] ",
		(trap[5] & 0x00000100) ? "read" : "write",
		trap[5] & 0xff, trap[4] & 0xffff, trap[3] & 0xffff, ch, chinst);

	en = nouveau_enum_find(vm_engine, st0);
	if (en)
		printk("%s/", en->name);
	else
		printk("%02x/", st0);

	cl = nouveau_enum_find(vm_client, st2);
	if (cl)
		printk("%s/", cl->name);
	else
		printk("%02x/", st2);

	if      (cl && cl->data) cl = nouveau_enum_find(cl->data, st3);
	else if (en && en->data) cl = nouveau_enum_find(en->data, st3);
	else                     cl = NULL;
	if (cl)
		printk("%s", cl->name);
	else
		printk("%02x", st3);

	printk(" reason: ");
	en = nouveau_enum_find(vm_fault, st1);
	if (en)
		printk("%s\n", en->name);
	else
		printk("0x%08x\n", st1);
}
