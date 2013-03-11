/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <core/client.h>
#include <core/enum.h>
#include <core/engctx.h>
#include <core/object.h>

#include <subdev/fb.h>
#include <subdev/bios.h>

struct nv50_fb_priv {
	struct nouveau_fb base;
	struct page *r100c08_page;
	dma_addr_t r100c08;
};

static int types[0x80] = {
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 0, 0,
	0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	1, 0, 2, 0, 1, 0, 2, 0, 1, 1, 2, 2, 1, 1, 0, 0
};

static bool
nv50_fb_memtype_valid(struct nouveau_fb *pfb, u32 memtype)
{
	return types[(memtype & 0xff00) >> 8] != 0;
}

static u32
nv50_fb_vram_rblock(struct nouveau_fb *pfb)
{
	int i, parts, colbits, rowbitsa, rowbitsb, banks;
	u64 rowsize, predicted;
	u32 r0, r4, rt, ru, rblock_size;

	r0 = nv_rd32(pfb, 0x100200);
	r4 = nv_rd32(pfb, 0x100204);
	rt = nv_rd32(pfb, 0x100250);
	ru = nv_rd32(pfb, 0x001540);
	nv_debug(pfb, "memcfg 0x%08x 0x%08x 0x%08x 0x%08x\n", r0, r4, rt, ru);

	for (i = 0, parts = 0; i < 8; i++) {
		if (ru & (0x00010000 << i))
			parts++;
	}

	colbits  =  (r4 & 0x0000f000) >> 12;
	rowbitsa = ((r4 & 0x000f0000) >> 16) + 8;
	rowbitsb = ((r4 & 0x00f00000) >> 20) + 8;
	banks    = 1 << (((r4 & 0x03000000) >> 24) + 2);

	rowsize = parts * banks * (1 << colbits) * 8;
	predicted = rowsize << rowbitsa;
	if (r0 & 0x00000004)
		predicted += rowsize << rowbitsb;

	if (predicted != pfb->ram.size) {
		nv_warn(pfb, "memory controller reports %d MiB VRAM\n",
			(u32)(pfb->ram.size >> 20));
	}

	rblock_size = rowsize;
	if (rt & 1)
		rblock_size *= 3;

	nv_debug(pfb, "rblock %d bytes\n", rblock_size);
	return rblock_size;
}

static int
nv50_fb_vram_init(struct nouveau_fb *pfb)
{
	struct nouveau_device *device = nv_device(pfb);
	struct nouveau_bios *bios = nouveau_bios(device);
	const u32 rsvd_head = ( 256 * 1024) >> 12; /* vga memory */
	const u32 rsvd_tail = (1024 * 1024) >> 12; /* vbios etc */
	u32 size, tags = 0;
	int ret;

	pfb->ram.size = nv_rd32(pfb, 0x10020c);
	pfb->ram.size = (pfb->ram.size & 0xffffff00) |
		       ((pfb->ram.size & 0x000000ff) << 32);

	size = (pfb->ram.size >> 12) - rsvd_head - rsvd_tail;
	switch (device->chipset) {
	case 0xaa:
	case 0xac:
	case 0xaf: /* IGPs, no reordering, no real VRAM */
		ret = nouveau_mm_init(&pfb->vram, rsvd_head, size, 1);
		if (ret)
			return ret;

		pfb->ram.type   = NV_MEM_TYPE_STOLEN;
		pfb->ram.stolen = (u64)nv_rd32(pfb, 0x100e10) << 12;
		break;
	default:
		switch (nv_rd32(pfb, 0x100714) & 0x00000007) {
		case 0: pfb->ram.type = NV_MEM_TYPE_DDR1; break;
		case 1:
			if (nouveau_fb_bios_memtype(bios) == NV_MEM_TYPE_DDR3)
				pfb->ram.type = NV_MEM_TYPE_DDR3;
			else
				pfb->ram.type = NV_MEM_TYPE_DDR2;
			break;
		case 2: pfb->ram.type = NV_MEM_TYPE_GDDR3; break;
		case 3: pfb->ram.type = NV_MEM_TYPE_GDDR4; break;
		case 4: pfb->ram.type = NV_MEM_TYPE_GDDR5; break;
		default:
			break;
		}

		ret = nouveau_mm_init(&pfb->vram, rsvd_head, size,
				      nv50_fb_vram_rblock(pfb) >> 12);
		if (ret)
			return ret;

		pfb->ram.ranks = (nv_rd32(pfb, 0x100200) & 0x4) ? 2 : 1;
		tags = nv_rd32(pfb, 0x100320);
		break;
	}

	return tags;
}

static int
nv50_fb_vram_new(struct nouveau_fb *pfb, u64 size, u32 align, u32 ncmin,
		 u32 memtype, struct nouveau_mem **pmem)
{
	struct nv50_fb_priv *priv = (void *)pfb;
	struct nouveau_mm *heap = &priv->base.vram;
	struct nouveau_mm *tags = &priv->base.tags;
	struct nouveau_mm_node *r;
	struct nouveau_mem *mem;
	int comp = (memtype & 0x300) >> 8;
	int type = (memtype & 0x07f);
	int back = (memtype & 0x800);
	int min, max, ret;

	max = (size >> 12);
	min = ncmin ? (ncmin >> 12) : max;
	align >>= 12;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	mutex_lock(&pfb->base.mutex);
	if (comp) {
		if (align == 16) {
			int n = (max >> 4) * comp;

			ret = nouveau_mm_head(tags, 1, n, n, 1, &mem->tag);
			if (ret)
				mem->tag = NULL;
		}

		if (unlikely(!mem->tag))
			comp = 0;
	}

	INIT_LIST_HEAD(&mem->regions);
	mem->memtype = (comp << 7) | type;
	mem->size = max;

	type = types[type];
	do {
		if (back)
			ret = nouveau_mm_tail(heap, type, max, min, align, &r);
		else
			ret = nouveau_mm_head(heap, type, max, min, align, &r);
		if (ret) {
			mutex_unlock(&pfb->base.mutex);
			pfb->ram.put(pfb, &mem);
			return ret;
		}

		list_add_tail(&r->rl_entry, &mem->regions);
		max -= r->length;
	} while (max);
	mutex_unlock(&pfb->base.mutex);

	r = list_first_entry(&mem->regions, struct nouveau_mm_node, rl_entry);
	mem->offset = (u64)r->offset << 12;
	*pmem = mem;
	return 0;
}

void
nv50_fb_vram_del(struct nouveau_fb *pfb, struct nouveau_mem **pmem)
{
	struct nv50_fb_priv *priv = (void *)pfb;
	struct nouveau_mm_node *this;
	struct nouveau_mem *mem;

	mem = *pmem;
	*pmem = NULL;
	if (unlikely(mem == NULL))
		return;

	mutex_lock(&pfb->base.mutex);
	while (!list_empty(&mem->regions)) {
		this = list_first_entry(&mem->regions, typeof(*this), rl_entry);

		list_del(&this->rl_entry);
		nouveau_mm_free(&priv->base.vram, &this);
	}

	nouveau_mm_free(&priv->base.tags, &mem->tag);
	mutex_unlock(&pfb->base.mutex);

	kfree(mem);
}

static const struct nouveau_enum vm_dispatch_subclients[] = {
	{ 0x00000000, "GRCTX", NULL },
	{ 0x00000001, "NOTIFY", NULL },
	{ 0x00000002, "QUERY", NULL },
	{ 0x00000003, "COND", NULL },
	{ 0x00000004, "M2M_IN", NULL },
	{ 0x00000005, "M2M_OUT", NULL },
	{ 0x00000006, "M2M_NOTIFY", NULL },
	{}
};

static const struct nouveau_enum vm_ccache_subclients[] = {
	{ 0x00000000, "CB", NULL },
	{ 0x00000001, "TIC", NULL },
	{ 0x00000002, "TSC", NULL },
	{}
};

static const struct nouveau_enum vm_prop_subclients[] = {
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

static const struct nouveau_enum vm_pfifo_subclients[] = {
	{ 0x00000000, "PUSHBUF", NULL },
	{ 0x00000001, "SEMAPHORE", NULL },
	{}
};

static const struct nouveau_enum vm_bar_subclients[] = {
	{ 0x00000000, "FB", NULL },
	{ 0x00000001, "IN", NULL },
	{}
};

static const struct nouveau_enum vm_client[] = {
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

static const struct nouveau_enum vm_engine[] = {
	{ 0x00000000, "PGRAPH", NULL, NVDEV_ENGINE_GR },
	{ 0x00000001, "PVP", NULL, NVDEV_ENGINE_VP },
	{ 0x00000004, "PEEPHOLE", NULL },
	{ 0x00000005, "PFIFO", vm_pfifo_subclients, NVDEV_ENGINE_FIFO },
	{ 0x00000006, "BAR", vm_bar_subclients },
	{ 0x00000008, "PPPP", NULL, NVDEV_ENGINE_PPP },
	{ 0x00000008, "PMPEG", NULL, NVDEV_ENGINE_MPEG },
	{ 0x00000009, "PBSP", NULL, NVDEV_ENGINE_BSP },
	{ 0x0000000a, "PCRYPT", NULL, NVDEV_ENGINE_CRYPT },
	{ 0x0000000b, "PCOUNTER", NULL },
	{ 0x0000000c, "SEMAPHORE_BG", NULL },
	{ 0x0000000d, "PCOPY", NULL, NVDEV_ENGINE_COPY0 },
	{ 0x0000000e, "PDAEMON", NULL },
	{}
};

static const struct nouveau_enum vm_fault[] = {
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

static void
nv50_fb_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_device *device = nv_device(subdev);
	struct nouveau_engine *engine;
	struct nv50_fb_priv *priv = (void *)subdev;
	const struct nouveau_enum *en, *cl;
	struct nouveau_object *engctx = NULL;
	u32 trap[6], idx, chan;
	u8 st0, st1, st2, st3;
	int i;

	idx = nv_rd32(priv, 0x100c90);
	if (!(idx & 0x80000000))
		return;
	idx &= 0x00ffffff;

	for (i = 0; i < 6; i++) {
		nv_wr32(priv, 0x100c90, idx | i << 24);
		trap[i] = nv_rd32(priv, 0x100c94);
	}
	nv_wr32(priv, 0x100c90, idx | 0x80000000);

	/* decode status bits into something more useful */
	if (device->chipset  < 0xa3 ||
	    device->chipset == 0xaa || device->chipset == 0xac) {
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
	chan = (trap[2] << 16) | trap[1];

	en = nouveau_enum_find(vm_engine, st0);

	if (en && en->data2) {
		const struct nouveau_enum *orig_en = en;
		while (en->name && en->value == st0 && en->data2) {
			engine = nouveau_engine(subdev, en->data2);
			if (engine) {
				engctx = nouveau_engctx_get(engine, chan);
				if (engctx)
					break;
			}
			en++;
		}
		if (!engctx)
			en = orig_en;
	}

	nv_error(priv, "trapped %s at 0x%02x%04x%04x on channel 0x%08x [%s] ",
		 (trap[5] & 0x00000100) ? "read" : "write",
		 trap[5] & 0xff, trap[4] & 0xffff, trap[3] & 0xffff, chan,
		 nouveau_client_name(engctx));

	nouveau_engctx_put(engctx);

	if (en)
		pr_cont("%s/", en->name);
	else
		pr_cont("%02x/", st0);

	cl = nouveau_enum_find(vm_client, st2);
	if (cl)
		pr_cont("%s/", cl->name);
	else
		pr_cont("%02x/", st2);

	if      (cl && cl->data) cl = nouveau_enum_find(cl->data, st3);
	else if (en && en->data) cl = nouveau_enum_find(en->data, st3);
	else                     cl = NULL;
	if (cl)
		pr_cont("%s", cl->name);
	else
		pr_cont("%02x", st3);

	pr_cont(" reason: ");
	en = nouveau_enum_find(vm_fault, st1);
	if (en)
		pr_cont("%s\n", en->name);
	else
		pr_cont("0x%08x\n", st1);
}

static int
nv50_fb_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nv50_fb_priv *priv;
	int ret;

	ret = nouveau_fb_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->r100c08_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (priv->r100c08_page) {
		priv->r100c08 = pci_map_page(device->pdev, priv->r100c08_page,
					     0, PAGE_SIZE,
					     PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(device->pdev, priv->r100c08))
			nv_warn(priv, "failed 0x100c08 page map\n");
	} else {
		nv_warn(priv, "failed 0x100c08 page alloc\n");
	}

	priv->base.memtype_valid = nv50_fb_memtype_valid;
	priv->base.ram.init = nv50_fb_vram_init;
	priv->base.ram.get = nv50_fb_vram_new;
	priv->base.ram.put = nv50_fb_vram_del;
	nv_subdev(priv)->intr = nv50_fb_intr;
	return nouveau_fb_preinit(&priv->base);
}

static void
nv50_fb_dtor(struct nouveau_object *object)
{
	struct nouveau_device *device = nv_device(object);
	struct nv50_fb_priv *priv = (void *)object;

	if (priv->r100c08_page) {
		pci_unmap_page(device->pdev, priv->r100c08, PAGE_SIZE,
			       PCI_DMA_BIDIRECTIONAL);
		__free_page(priv->r100c08_page);
	}

	nouveau_fb_destroy(&priv->base);
}

static int
nv50_fb_init(struct nouveau_object *object)
{
	struct nouveau_device *device = nv_device(object);
	struct nv50_fb_priv *priv = (void *)object;
	int ret;

	ret = nouveau_fb_init(&priv->base);
	if (ret)
		return ret;

	/* Not a clue what this is exactly.  Without pointing it at a
	 * scratch page, VRAM->GART blits with M2MF (as in DDX DFS)
	 * cause IOMMU "read from address 0" errors (rh#561267)
	 */
	nv_wr32(priv, 0x100c08, priv->r100c08 >> 8);

	/* This is needed to get meaningful information from 100c90
	 * on traps. No idea what these values mean exactly. */
	switch (device->chipset) {
	case 0x50:
		nv_wr32(priv, 0x100c90, 0x000707ff);
		break;
	case 0xa3:
	case 0xa5:
	case 0xa8:
		nv_wr32(priv, 0x100c90, 0x000d0fff);
		break;
	case 0xaf:
		nv_wr32(priv, 0x100c90, 0x089d1fff);
		break;
	default:
		nv_wr32(priv, 0x100c90, 0x001d07ff);
		break;
	}

	return 0;
}

struct nouveau_oclass
nv50_fb_oclass = {
	.handle = NV_SUBDEV(FB, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_fb_ctor,
		.dtor = nv50_fb_dtor,
		.init = nv50_fb_init,
		.fini = _nouveau_fb_fini,
	},
};
