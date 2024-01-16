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
#include <subdev/bios.h>
#include <subdev/bios/bit.h>

int
bit_entry(struct nvkm_bios *bios, u8 id, struct bit_entry *bit)
{
	if (likely(bios->bit_offset)) {
		u8  entries = nvbios_rd08(bios, bios->bit_offset + 10);
		u32 entry   = bios->bit_offset + 12;
		while (entries--) {
			if (nvbios_rd08(bios, entry + 0) == id) {
				bit->id      = nvbios_rd08(bios, entry + 0);
				bit->version = nvbios_rd08(bios, entry + 1);
				bit->length  = nvbios_rd16(bios, entry + 2);
				bit->offset  = nvbios_rd16(bios, entry + 4);
				return 0;
			}

			entry += nvbios_rd08(bios, bios->bit_offset + 9);
		}

		return -ENOENT;
	}

	return -EINVAL;
}
