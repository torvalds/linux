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
#include "priv.h"

#include <subdev/bios.h>
#include <subdev/bios/bmp.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/image.h>

static bool
nvbios_addr(struct nvkm_bios *bios, u32 *addr, u8 size)
{
	u32 p = *addr;

	if (*addr > bios->image0_size && bios->imaged_addr) {
		*addr -= bios->image0_size;
		*addr += bios->imaged_addr;
	}

	if (unlikely(*addr + size >= bios->size)) {
		nvkm_error(&bios->subdev, "OOB %d %08x %08x\n", size, p, *addr);
		return false;
	}

	return true;
}

u8
nvbios_rd08(struct nvkm_bios *bios, u32 addr)
{
	if (likely(nvbios_addr(bios, &addr, 1)))
		return bios->data[addr];
	return 0x00;
}

u16
nvbios_rd16(struct nvkm_bios *bios, u32 addr)
{
	if (likely(nvbios_addr(bios, &addr, 2)))
		return get_unaligned_le16(&bios->data[addr]);
	return 0x0000;
}

u32
nvbios_rd32(struct nvkm_bios *bios, u32 addr)
{
	if (likely(nvbios_addr(bios, &addr, 4)))
		return get_unaligned_le32(&bios->data[addr]);
	return 0x00000000;
}

u8
nvbios_checksum(const u8 *data, int size)
{
	u8 sum = 0;
	while (size--)
		sum += *data++;
	return sum;
}

u16
nvbios_findstr(const u8 *data, int size, const char *str, int len)
{
	int i, j;

	for (i = 0; i <= (size - len); i++) {
		for (j = 0; j < len; j++)
			if ((char)data[i + j] != str[j])
				break;
		if (j == len)
			return i;
	}

	return 0;
}

int
nvbios_memcmp(struct nvkm_bios *bios, u32 addr, const char *str, u32 len)
{
	unsigned char c1, c2;

	while (len--) {
		c1 = nvbios_rd08(bios, addr++);
		c2 = *(str++);
		if (c1 != c2)
			return c1 - c2;
	}
	return 0;
}

int
nvbios_extend(struct nvkm_bios *bios, u32 length)
{
	if (bios->size < length) {
		u8 *prev = bios->data;
		if (!(bios->data = kmalloc(length, GFP_KERNEL))) {
			bios->data = prev;
			return -ENOMEM;
		}
		memcpy(bios->data, prev, bios->size);
		bios->size = length;
		kfree(prev);
		return 1;
	}
	return 0;
}

static void *
nvkm_bios_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_bios *bios = nvkm_bios(subdev);
	kfree(bios->data);
	return bios;
}

static const struct nvkm_subdev_func
nvkm_bios = {
	.dtor = nvkm_bios_dtor,
};

int
nvkm_bios_new(struct nvkm_device *device, int index, struct nvkm_bios **pbios)
{
	struct nvkm_bios *bios;
	struct nvbios_image image;
	struct bit_entry bit_i;
	int ret, idx = 0;

	if (!(bios = *pbios = kzalloc(sizeof(*bios), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&nvkm_bios, device, index, &bios->subdev);

	ret = nvbios_shadow(bios);
	if (ret)
		return ret;

	/* Some tables have weird pointers that need adjustment before
	 * they're dereferenced.  I'm not entirely sure why...
	 */
	if (nvbios_image(bios, idx++, &image)) {
		bios->image0_size = image.size;
		while (nvbios_image(bios, idx++, &image)) {
			if (image.type == 0xe0) {
				bios->imaged_addr = image.base;
				break;
			}
		}
	}

	/* detect type of vbios we're dealing with */
	bios->bmp_offset = nvbios_findstr(bios->data, bios->size,
					  "\xff\x7f""NV\0", 5);
	if (bios->bmp_offset) {
		nvkm_debug(&bios->subdev, "BMP version %x.%x\n",
			   bmp_version(bios) >> 8,
			   bmp_version(bios) & 0xff);
	}

	bios->bit_offset = nvbios_findstr(bios->data, bios->size,
					  "\xff\xb8""BIT", 5);
	if (bios->bit_offset)
		nvkm_debug(&bios->subdev, "BIT signature found\n");

	/* determine the vbios version number */
	if (!bit_entry(bios, 'i', &bit_i) && bit_i.length >= 4) {
		bios->version.major = nvbios_rd08(bios, bit_i.offset + 3);
		bios->version.chip  = nvbios_rd08(bios, bit_i.offset + 2);
		bios->version.minor = nvbios_rd08(bios, bit_i.offset + 1);
		bios->version.micro = nvbios_rd08(bios, bit_i.offset + 0);
		bios->version.patch = nvbios_rd08(bios, bit_i.offset + 4);
	} else
	if (bmp_version(bios)) {
		bios->version.major = nvbios_rd08(bios, bios->bmp_offset + 13);
		bios->version.chip  = nvbios_rd08(bios, bios->bmp_offset + 12);
		bios->version.minor = nvbios_rd08(bios, bios->bmp_offset + 11);
		bios->version.micro = nvbios_rd08(bios, bios->bmp_offset + 10);
	}

	nvkm_info(&bios->subdev, "version %02x.%02x.%02x.%02x.%02x\n",
		  bios->version.major, bios->version.chip,
		  bios->version.minor, bios->version.micro, bios->version.patch);
	return 0;
}
