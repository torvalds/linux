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

#include "subdev/fb.h"
#include "subdev/bios.h"
#include "subdev/bios/bit.h"

int
nouveau_fb_bios_memtype(struct nouveau_bios *bios)
{
	struct bit_entry M;
	u8 ramcfg;

	ramcfg = (nv_rd32(bios, 0x101000) & 0x0000003c) >> 2;
	if (!bit_entry(bios, 'M', &M) && M.version == 2 && M.length >= 5) {
		u16 table   = nv_ro16(bios, M.offset + 3);
		u8  version = nv_ro08(bios, table + 0);
		u8  header  = nv_ro08(bios, table + 1);
		u8  record  = nv_ro08(bios, table + 2);
		u8  entries = nv_ro08(bios, table + 3);
		if (table && version == 0x10 && ramcfg < entries) {
			u16 entry = table + header + (ramcfg * record);
			switch (nv_ro08(bios, entry) & 0x0f) {
			case 0: return NV_MEM_TYPE_DDR2;
			case 1: return NV_MEM_TYPE_DDR3;
			case 2: return NV_MEM_TYPE_GDDR3;
			case 3: return NV_MEM_TYPE_GDDR5;
			default:
				break;
			}

		}
	}

	return NV_MEM_TYPE_UNKNOWN;
}

int
nouveau_fb_init(struct nouveau_fb *pfb)
{
	int ret, i;

	ret = nouveau_subdev_init(&pfb->base);
	if (ret)
		return ret;

	for (i = 0; i < pfb->tile.regions; i++)
		pfb->tile.prog(pfb, i, &pfb->tile.region[i]);

	return 0;
}

int
_nouveau_fb_init(struct nouveau_object *object)
{
	struct nouveau_fb *pfb = (void *)object;
	return nouveau_fb_init(pfb);
}

void
nouveau_fb_destroy(struct nouveau_fb *pfb)
{
	int i;

	for (i = 0; i < pfb->tile.regions; i++)
		pfb->tile.fini(pfb, i, &pfb->tile.region[i]);

	if (pfb->tags.block_size)
		nouveau_mm_fini(&pfb->tags);

	if (pfb->vram.block_size)
		nouveau_mm_fini(&pfb->vram);

	nouveau_subdev_destroy(&pfb->base);
}

void
_nouveau_fb_dtor(struct nouveau_object *object)
{
	struct nouveau_fb *pfb = (void *)object;
	nouveau_fb_destroy(pfb);
}

int
nouveau_fb_created(struct nouveau_fb *pfb)
{
	static const char *name[] = {
		[NV_MEM_TYPE_UNKNOWN] = "unknown",
		[NV_MEM_TYPE_STOLEN ] = "stolen system memory",
		[NV_MEM_TYPE_SGRAM  ] = "SGRAM",
		[NV_MEM_TYPE_SDRAM  ] = "SDRAM",
		[NV_MEM_TYPE_DDR1   ] = "DDR1",
		[NV_MEM_TYPE_DDR2   ] = "DDR2",
		[NV_MEM_TYPE_DDR3   ] = "DDR3",
		[NV_MEM_TYPE_GDDR2  ] = "GDDR2",
		[NV_MEM_TYPE_GDDR3  ] = "GDDR3",
		[NV_MEM_TYPE_GDDR4  ] = "GDDR4",
		[NV_MEM_TYPE_GDDR5  ] = "GDDR5",
	};

	if (pfb->ram.size == 0) {
		nv_fatal(pfb, "no vram detected!!\n");
		return -ERANGE;
	}

	nv_info(pfb, "RAM type: %s\n", name[pfb->ram.type]);
	nv_info(pfb, "RAM size: %d MiB\n", (int)(pfb->ram.size >> 20));
	return 0;
}
