/*
 * Copyright 2013 Red Hat Inc.
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
#include "nv50.h"
#include "ramseq.h"

#include <core/option.h>
#include <subdev/bios.h>
#include <subdev/bios/perf.h>
#include <subdev/bios/pll.h>
#include <subdev/bios/rammap.h>
#include <subdev/bios/timing.h>
#include <subdev/clk/pll.h>

struct nv50_ramseq {
	struct hwsq base;
	struct hwsq_reg r_0x002504;
	struct hwsq_reg r_0x004008;
	struct hwsq_reg r_0x00400c;
	struct hwsq_reg r_0x00c040;
	struct hwsq_reg r_0x100200;
	struct hwsq_reg r_0x100210;
	struct hwsq_reg r_0x10021c;
	struct hwsq_reg r_0x1002d0;
	struct hwsq_reg r_0x1002d4;
	struct hwsq_reg r_0x1002dc;
	struct hwsq_reg r_0x10053c;
	struct hwsq_reg r_0x1005a0;
	struct hwsq_reg r_0x1005a4;
	struct hwsq_reg r_0x100710;
	struct hwsq_reg r_0x100714;
	struct hwsq_reg r_0x100718;
	struct hwsq_reg r_0x10071c;
	struct hwsq_reg r_0x100da0;
	struct hwsq_reg r_0x100e20;
	struct hwsq_reg r_0x100e24;
	struct hwsq_reg r_0x611200;
	struct hwsq_reg r_timing[9];
	struct hwsq_reg r_mr[4];
};

struct nv50_ram {
	struct nvkm_ram base;
	struct nv50_ramseq hwsq;
};

#define T(t) cfg->timing_10_##t
static int
nv50_ram_timing_calc(struct nvkm_fb *fb, u32 *timing)
{
	struct nv50_ram *ram = (void *)fb->ram;
	struct nvbios_ramcfg *cfg = &ram->base.target.bios;
	u32 cur2, cur4, cur7, cur8;
	u8 unkt3b;

	cur2 = nv_rd32(fb, 0x100228);
	cur4 = nv_rd32(fb, 0x100230);
	cur7 = nv_rd32(fb, 0x10023c);
	cur8 = nv_rd32(fb, 0x100240);

	switch ((!T(CWL)) * ram->base.type) {
	case NV_MEM_TYPE_DDR2:
		T(CWL) = T(CL) - 1;
		break;
	case NV_MEM_TYPE_GDDR3:
		T(CWL) = ((cur2 & 0xff000000) >> 24) + 1;
		break;
	}

	/* XXX: N=1 is not proper statistics */
	if (nv_device(fb)->chipset == 0xa0) {
		unkt3b = 0x19 + ram->base.next->bios.rammap_00_16_40;
		timing[6] = (0x2d + T(CL) - T(CWL) +
				ram->base.next->bios.rammap_00_16_40) << 16 |
			    T(CWL) << 8 |
			    (0x2f + T(CL) - T(CWL));
	} else {
		unkt3b = 0x16;
		timing[6] = (0x2b + T(CL) - T(CWL)) << 16 |
			    max_t(s8, T(CWL) - 2, 1) << 8 |
			    (0x2e + T(CL) - T(CWL));
	}

	timing[0] = (T(RP) << 24 | T(RAS) << 16 | T(RFC) << 8 | T(RC));
	timing[1] = (T(WR) + 1 + T(CWL)) << 24 |
		    max_t(u8, T(18), 1) << 16 |
		    (T(WTR) + 1 + T(CWL)) << 8 |
		    (3 + T(CL) - T(CWL));
	timing[2] = (T(CWL) - 1) << 24 |
		    (T(RRD) << 16) |
		    (T(RCDWR) << 8) |
		    T(RCDRD);
	timing[3] = (unkt3b - 2 + T(CL)) << 24 |
		    unkt3b << 16 |
		    (T(CL) - 1) << 8 |
		    (T(CL) - 1);
	timing[4] = (cur4 & 0xffff0000) |
		    T(13) << 8 |
		    T(13);
	timing[5] = T(RFC) << 24 |
		    max_t(u8, T(RCDRD), T(RCDWR)) << 16 |
		    T(RP);
	/* Timing 6 is already done above */
	timing[7] = (cur7 & 0xff00ffff) | (T(CL) - 1) << 16;
	timing[8] = (cur8 & 0xffffff00);

	/* XXX: P.version == 1 only has DDR2 and GDDR3? */
	if (fb->ram->type == NV_MEM_TYPE_DDR2) {
		timing[5] |= (T(CL) + 3) << 8;
		timing[8] |= (T(CL) - 4);
	} else if (fb->ram->type == NV_MEM_TYPE_GDDR3) {
		timing[5] |= (T(CL) + 2) << 8;
		timing[8] |= (T(CL) - 2);
	}

	nv_debug(fb, " 220: %08x %08x %08x %08x\n",
			timing[0], timing[1], timing[2], timing[3]);
	nv_debug(fb, " 230: %08x %08x %08x %08x\n",
			timing[4], timing[5], timing[6], timing[7]);
	nv_debug(fb, " 240: %08x\n", timing[8]);
	return 0;
}
#undef T

static void
nvkm_sddr2_dll_reset(struct nv50_ramseq *hwsq)
{
	ram_mask(hwsq, mr[0], 0x100, 0x100);
	ram_mask(hwsq, mr[0], 0x100, 0x000);
	ram_nsec(hwsq, 24000);
}

static int
nv50_ram_calc(struct nvkm_fb *fb, u32 freq)
{
	struct nvkm_bios *bios = nvkm_bios(fb);
	struct nv50_ram *ram = (void *)fb->ram;
	struct nv50_ramseq *hwsq = &ram->hwsq;
	struct nvbios_perfE perfE;
	struct nvbios_pll mpll;
	struct nvkm_ram_data *next;
	u8  ver, hdr, cnt, len, strap, size;
	u32 data;
	u32 r100da0, r004008, unk710, unk714, unk718, unk71c;
	int N1, M1, N2, M2, P;
	int ret, i;
	u32 timing[9];

	next = &ram->base.target;
	next->freq = freq;
	ram->base.next = next;

	/* lookup closest matching performance table entry for frequency */
	i = 0;
	do {
		data = nvbios_perfEp(bios, i++, &ver, &hdr, &cnt,
					    &size, &perfE);
		if (!data || (ver < 0x25 || ver >= 0x40) ||
		    (size < 2)) {
			nv_error(fb, "invalid/missing perftab entry\n");
			return -EINVAL;
		}
	} while (perfE.memory < freq);

	nvbios_rammapEp_from_perf(bios, data, hdr, &next->bios);

	/* locate specific data set for the attached memory */
	strap = nvbios_ramcfg_index(nv_subdev(fb));
	if (strap >= cnt) {
		nv_error(fb, "invalid ramcfg strap\n");
		return -EINVAL;
	}

	data = nvbios_rammapSp_from_perf(bios, data + hdr, size, strap,
			&next->bios);
	if (!data) {
		nv_error(fb, "invalid/missing rammap entry ");
		return -EINVAL;
	}

	/* lookup memory timings, if bios says they're present */
	if (next->bios.ramcfg_timing != 0xff) {
		data = nvbios_timingEp(bios, next->bios.ramcfg_timing,
					&ver, &hdr, &cnt, &len, &next->bios);
		if (!data || ver != 0x10 || hdr < 0x12) {
			nv_error(fb, "invalid/missing timing entry "
				 "%02x %04x %02x %02x\n",
				 strap, data, ver, hdr);
			return -EINVAL;
		}
	}

	nv50_ram_timing_calc(fb, timing);

	ret = ram_init(hwsq, nv_subdev(fb));
	if (ret)
		return ret;

	/* Determine ram-specific MR values */
	ram->base.mr[0] = ram_rd32(hwsq, mr[0]);
	ram->base.mr[1] = ram_rd32(hwsq, mr[1]);
	ram->base.mr[2] = ram_rd32(hwsq, mr[2]);

	switch (ram->base.type) {
	case NV_MEM_TYPE_GDDR3:
		ret = nvkm_gddr3_calc(&ram->base);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	if (ret)
		return ret;

	/* Always disable this bit during reclock */
	ram_mask(hwsq, 0x100200, 0x00000800, 0x00000000);

	ram_wait(hwsq, 0x01, 0x00); /* wait for !vblank */
	ram_wait(hwsq, 0x01, 0x01); /* wait for vblank */
	ram_wr32(hwsq, 0x611200, 0x00003300);
	ram_wr32(hwsq, 0x002504, 0x00000001); /* block fifo */
	ram_nsec(hwsq, 8000);
	ram_setf(hwsq, 0x10, 0x00); /* disable fb */
	ram_wait(hwsq, 0x00, 0x01); /* wait for fb disabled */
	ram_nsec(hwsq, 2000);

	ram_wr32(hwsq, 0x1002d4, 0x00000001); /* precharge */
	ram_wr32(hwsq, 0x1002d0, 0x00000001); /* refresh */
	ram_wr32(hwsq, 0x1002d0, 0x00000001); /* refresh */
	ram_wr32(hwsq, 0x100210, 0x00000000); /* disable auto-refresh */
	ram_wr32(hwsq, 0x1002dc, 0x00000001); /* enable self-refresh */

	ret = nvbios_pll_parse(bios, 0x004008, &mpll);
	mpll.vco2.max_freq = 0;
	if (ret >= 0) {
		ret = nv04_pll_calc(nv_subdev(fb), &mpll, freq,
				    &N1, &M1, &N2, &M2, &P);
		if (ret <= 0)
			ret = -EINVAL;
	}

	if (ret < 0)
		return ret;

	/* XXX: 750MHz seems rather arbitrary */
	if (freq <= 750000) {
		r100da0 = 0x00000010;
		r004008 = 0x90000000;
	} else {
		r100da0 = 0x00000000;
		r004008 = 0x80000000;
	}

	r004008 |= (mpll.bias_p << 19) | (P << 22) | (P << 16);

	ram_mask(hwsq, 0x00c040, 0xc000c000, 0x0000c000);
	/* XXX: Is rammap_00_16_40 the DLL bit we've seen in GT215? Why does
	 * it have a different rammap bit from DLLoff? */
	ram_mask(hwsq, 0x004008, 0x00004200, 0x00000200 |
			next->bios.rammap_00_16_40 << 14);
	ram_mask(hwsq, 0x00400c, 0x0000ffff, (N1 << 8) | M1);
	ram_mask(hwsq, 0x004008, 0x91ff0000, r004008);
	if (nv_device(fb)->chipset >= 0x96)
		ram_wr32(hwsq, 0x100da0, r100da0);
	ram_nsec(hwsq, 64000); /*XXX*/
	ram_nsec(hwsq, 32000); /*XXX*/

	ram_mask(hwsq, 0x004008, 0x00002200, 0x00002000);

	ram_wr32(hwsq, 0x1002dc, 0x00000000); /* disable self-refresh */
	ram_wr32(hwsq, 0x1002d4, 0x00000001); /* disable self-refresh */
	ram_wr32(hwsq, 0x100210, 0x80000000); /* enable auto-refresh */

	ram_nsec(hwsq, 12000);

	switch (ram->base.type) {
	case NV_MEM_TYPE_DDR2:
		ram_nuke(hwsq, mr[0]); /* force update */
		ram_mask(hwsq, mr[0], 0x000, 0x000);
		break;
	case NV_MEM_TYPE_GDDR3:
		ram_nuke(hwsq, mr[1]); /* force update */
		ram_wr32(hwsq, mr[1], ram->base.mr[1]);
		ram_nuke(hwsq, mr[0]); /* force update */
		ram_wr32(hwsq, mr[0], ram->base.mr[0]);
		break;
	default:
		break;
	}

	ram_mask(hwsq, timing[3], 0xffffffff, timing[3]);
	ram_mask(hwsq, timing[1], 0xffffffff, timing[1]);
	ram_mask(hwsq, timing[6], 0xffffffff, timing[6]);
	ram_mask(hwsq, timing[7], 0xffffffff, timing[7]);
	ram_mask(hwsq, timing[8], 0xffffffff, timing[8]);
	ram_mask(hwsq, timing[0], 0xffffffff, timing[0]);
	ram_mask(hwsq, timing[2], 0xffffffff, timing[2]);
	ram_mask(hwsq, timing[4], 0xffffffff, timing[4]);
	ram_mask(hwsq, timing[5], 0xffffffff, timing[5]);

	if (!next->bios.ramcfg_00_03_02)
		ram_mask(hwsq, 0x10021c, 0x00010000, 0x00000000);
	ram_mask(hwsq, 0x100200, 0x00001000, !next->bios.ramcfg_00_04_02 << 12);

	/* XXX: A lot of this could be "chipset"/"ram type" specific stuff */
	unk710  = ram_rd32(hwsq, 0x100710) & ~0x00000101;
	unk714  = ram_rd32(hwsq, 0x100714) & ~0xf0000020;
	unk718  = ram_rd32(hwsq, 0x100718) & ~0x00000100;
	unk71c  = ram_rd32(hwsq, 0x10071c) & ~0x00000100;

	if ( next->bios.ramcfg_00_03_01)
		unk71c |= 0x00000100;
	if ( next->bios.ramcfg_00_03_02)
		unk710 |= 0x00000100;
	if (!next->bios.ramcfg_00_03_08) {
		unk710 |= 0x1;
		unk714 |= 0x20;
	}
	if ( next->bios.ramcfg_00_04_04)
		unk714 |= 0x70000000;
	if ( next->bios.ramcfg_00_04_20)
		unk718 |= 0x00000100;

	ram_mask(hwsq, 0x100714, 0xffffffff, unk714);
	ram_mask(hwsq, 0x10071c, 0xffffffff, unk71c);
	ram_mask(hwsq, 0x100718, 0xffffffff, unk718);
	ram_mask(hwsq, 0x100710, 0xffffffff, unk710);

	if (next->bios.rammap_00_16_20) {
		ram_wr32(hwsq, 0x1005a0, next->bios.ramcfg_00_07 << 16 |
					 next->bios.ramcfg_00_06 << 8 |
					 next->bios.ramcfg_00_05);
		ram_wr32(hwsq, 0x1005a4, next->bios.ramcfg_00_09 << 8 |
					 next->bios.ramcfg_00_08);
		ram_mask(hwsq, 0x10053c, 0x00001000, 0x00000000);
	} else {
		ram_mask(hwsq, 0x10053c, 0x00001000, 0x00001000);
	}
	ram_mask(hwsq, mr[1], 0xffffffff, ram->base.mr[1]);

	/* Reset DLL */
	if (!next->bios.ramcfg_DLLoff)
		nvkm_sddr2_dll_reset(hwsq);

	ram_setf(hwsq, 0x10, 0x01); /* enable fb */
	ram_wait(hwsq, 0x00, 0x00); /* wait for fb enabled */
	ram_wr32(hwsq, 0x611200, 0x00003330);
	ram_wr32(hwsq, 0x002504, 0x00000000); /* un-block fifo */

	if (next->bios.rammap_00_17_02)
		ram_mask(hwsq, 0x100200, 0x00000800, 0x00000800);
	if (!next->bios.rammap_00_16_40)
		ram_mask(hwsq, 0x004008, 0x00004000, 0x00000000);
	if (next->bios.ramcfg_00_03_02)
		ram_mask(hwsq, 0x10021c, 0x00010000, 0x00010000);

	return 0;
}

static int
nv50_ram_prog(struct nvkm_fb *fb)
{
	struct nvkm_device *device = nv_device(fb);
	struct nv50_ram *ram = (void *)fb->ram;
	struct nv50_ramseq *hwsq = &ram->hwsq;

	ram_exec(hwsq, nvkm_boolopt(device->cfgopt, "NvMemExec", true));
	return 0;
}

static void
nv50_ram_tidy(struct nvkm_fb *fb)
{
	struct nv50_ram *ram = (void *)fb->ram;
	struct nv50_ramseq *hwsq = &ram->hwsq;
	ram_exec(hwsq, false);
}

void
__nv50_ram_put(struct nvkm_fb *fb, struct nvkm_mem *mem)
{
	struct nvkm_mm_node *this;

	while (!list_empty(&mem->regions)) {
		this = list_first_entry(&mem->regions, typeof(*this), rl_entry);

		list_del(&this->rl_entry);
		nvkm_mm_free(&fb->vram, &this);
	}

	nvkm_mm_free(&fb->tags, &mem->tag);
}

void
nv50_ram_put(struct nvkm_fb *fb, struct nvkm_mem **pmem)
{
	struct nvkm_mem *mem = *pmem;

	*pmem = NULL;
	if (unlikely(mem == NULL))
		return;

	mutex_lock(&fb->subdev.mutex);
	__nv50_ram_put(fb, mem);
	mutex_unlock(&fb->subdev.mutex);

	kfree(mem);
}

int
nv50_ram_get(struct nvkm_fb *fb, u64 size, u32 align, u32 ncmin,
	     u32 memtype, struct nvkm_mem **pmem)
{
	struct nvkm_mm *heap = &fb->vram;
	struct nvkm_mm *tags = &fb->tags;
	struct nvkm_mm_node *r;
	struct nvkm_mem *mem;
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

	mutex_lock(&fb->subdev.mutex);
	if (comp) {
		if (align == 16) {
			int n = (max >> 4) * comp;

			ret = nvkm_mm_head(tags, 0, 1, n, n, 1, &mem->tag);
			if (ret)
				mem->tag = NULL;
		}

		if (unlikely(!mem->tag))
			comp = 0;
	}

	INIT_LIST_HEAD(&mem->regions);
	mem->memtype = (comp << 7) | type;
	mem->size = max;

	type = nv50_fb_memtype[type];
	do {
		if (back)
			ret = nvkm_mm_tail(heap, 0, type, max, min, align, &r);
		else
			ret = nvkm_mm_head(heap, 0, type, max, min, align, &r);
		if (ret) {
			mutex_unlock(&fb->subdev.mutex);
			fb->ram->put(fb, &mem);
			return ret;
		}

		list_add_tail(&r->rl_entry, &mem->regions);
		max -= r->length;
	} while (max);
	mutex_unlock(&fb->subdev.mutex);

	r = list_first_entry(&mem->regions, struct nvkm_mm_node, rl_entry);
	mem->offset = (u64)r->offset << 12;
	*pmem = mem;
	return 0;
}

static u32
nv50_fb_vram_rblock(struct nvkm_fb *fb, struct nvkm_ram *ram)
{
	int colbits, rowbitsa, rowbitsb, banks;
	u64 rowsize, predicted;
	u32 r0, r4, rt, rblock_size;

	r0 = nv_rd32(fb, 0x100200);
	r4 = nv_rd32(fb, 0x100204);
	rt = nv_rd32(fb, 0x100250);
	nv_debug(fb, "memcfg 0x%08x 0x%08x 0x%08x 0x%08x\n",
		 r0, r4, rt, nv_rd32(fb, 0x001540));

	colbits  =  (r4 & 0x0000f000) >> 12;
	rowbitsa = ((r4 & 0x000f0000) >> 16) + 8;
	rowbitsb = ((r4 & 0x00f00000) >> 20) + 8;
	banks    = 1 << (((r4 & 0x03000000) >> 24) + 2);

	rowsize = ram->parts * banks * (1 << colbits) * 8;
	predicted = rowsize << rowbitsa;
	if (r0 & 0x00000004)
		predicted += rowsize << rowbitsb;

	if (predicted != ram->size) {
		nv_warn(fb, "memory controller reports %d MiB VRAM\n",
			(u32)(ram->size >> 20));
	}

	rblock_size = rowsize;
	if (rt & 1)
		rblock_size *= 3;

	nv_debug(fb, "rblock %d bytes\n", rblock_size);
	return rblock_size;
}

int
nv50_ram_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		 struct nvkm_oclass *oclass, int length, void **pobject)
{
	const u32 rsvd_head = ( 256 * 1024) >> 12; /* vga memory */
	const u32 rsvd_tail = (1024 * 1024) >> 12; /* vbios etc */
	struct nvkm_bios *bios = nvkm_bios(parent);
	struct nvkm_fb *fb = nvkm_fb(parent);
	struct nvkm_ram *ram;
	int ret;

	ret = nvkm_ram_create_(parent, engine, oclass, length, pobject);
	ram = *pobject;
	if (ret)
		return ret;

	ram->size = nv_rd32(fb, 0x10020c);
	ram->size = (ram->size & 0xffffff00) | ((ram->size & 0x000000ff) << 32);

	ram->part_mask = (nv_rd32(fb, 0x001540) & 0x00ff0000) >> 16;
	ram->parts = hweight8(ram->part_mask);

	switch (nv_rd32(fb, 0x100714) & 0x00000007) {
	case 0: ram->type = NV_MEM_TYPE_DDR1; break;
	case 1:
		if (nvkm_fb_bios_memtype(bios) == NV_MEM_TYPE_DDR3)
			ram->type = NV_MEM_TYPE_DDR3;
		else
			ram->type = NV_MEM_TYPE_DDR2;
		break;
	case 2: ram->type = NV_MEM_TYPE_GDDR3; break;
	case 3: ram->type = NV_MEM_TYPE_GDDR4; break;
	case 4: ram->type = NV_MEM_TYPE_GDDR5; break;
	default:
		break;
	}

	ret = nvkm_mm_init(&fb->vram, rsvd_head, (ram->size >> 12) -
			   (rsvd_head + rsvd_tail),
			   nv50_fb_vram_rblock(fb, ram) >> 12);
	if (ret)
		return ret;

	ram->ranks = (nv_rd32(fb, 0x100200) & 0x4) ? 2 : 1;
	ram->tags  =  nv_rd32(fb, 0x100320);
	ram->get = nv50_ram_get;
	ram->put = nv50_ram_put;
	return 0;
}

static int
nv50_ram_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 datasize,
	      struct nvkm_object **pobject)
{
	struct nv50_ram *ram;
	int ret, i;

	ret = nv50_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	switch (ram->base.type) {
	case NV_MEM_TYPE_GDDR3:
		ram->base.calc = nv50_ram_calc;
		ram->base.prog = nv50_ram_prog;
		ram->base.tidy = nv50_ram_tidy;
		break;
	case NV_MEM_TYPE_DDR2:
	default:
		nv_warn(ram, "reclocking of this ram type unsupported\n");
		return 0;
	}

	ram->hwsq.r_0x002504 = hwsq_reg(0x002504);
	ram->hwsq.r_0x00c040 = hwsq_reg(0x00c040);
	ram->hwsq.r_0x004008 = hwsq_reg(0x004008);
	ram->hwsq.r_0x00400c = hwsq_reg(0x00400c);
	ram->hwsq.r_0x100200 = hwsq_reg(0x100200);
	ram->hwsq.r_0x100210 = hwsq_reg(0x100210);
	ram->hwsq.r_0x10021c = hwsq_reg(0x10021c);
	ram->hwsq.r_0x1002d0 = hwsq_reg(0x1002d0);
	ram->hwsq.r_0x1002d4 = hwsq_reg(0x1002d4);
	ram->hwsq.r_0x1002dc = hwsq_reg(0x1002dc);
	ram->hwsq.r_0x10053c = hwsq_reg(0x10053c);
	ram->hwsq.r_0x1005a0 = hwsq_reg(0x1005a0);
	ram->hwsq.r_0x1005a4 = hwsq_reg(0x1005a4);
	ram->hwsq.r_0x100710 = hwsq_reg(0x100710);
	ram->hwsq.r_0x100714 = hwsq_reg(0x100714);
	ram->hwsq.r_0x100718 = hwsq_reg(0x100718);
	ram->hwsq.r_0x10071c = hwsq_reg(0x10071c);
	ram->hwsq.r_0x100da0 = hwsq_stride(0x100da0, 4, ram->base.part_mask);
	ram->hwsq.r_0x100e20 = hwsq_reg(0x100e20);
	ram->hwsq.r_0x100e24 = hwsq_reg(0x100e24);
	ram->hwsq.r_0x611200 = hwsq_reg(0x611200);

	for (i = 0; i < 9; i++)
		ram->hwsq.r_timing[i] = hwsq_reg(0x100220 + (i * 0x04));

	if (ram->base.ranks > 1) {
		ram->hwsq.r_mr[0] = hwsq_reg2(0x1002c0, 0x1002c8);
		ram->hwsq.r_mr[1] = hwsq_reg2(0x1002c4, 0x1002cc);
		ram->hwsq.r_mr[2] = hwsq_reg2(0x1002e0, 0x1002e8);
		ram->hwsq.r_mr[3] = hwsq_reg2(0x1002e4, 0x1002ec);
	} else {
		ram->hwsq.r_mr[0] = hwsq_reg(0x1002c0);
		ram->hwsq.r_mr[1] = hwsq_reg(0x1002c4);
		ram->hwsq.r_mr[2] = hwsq_reg(0x1002e0);
		ram->hwsq.r_mr[3] = hwsq_reg(0x1002e4);
	}

	return 0;
}

struct nvkm_oclass
nv50_ram_oclass = {
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_ram_ctor,
		.dtor = _nvkm_ram_dtor,
		.init = _nvkm_ram_init,
		.fini = _nvkm_ram_fini,
	}
};
