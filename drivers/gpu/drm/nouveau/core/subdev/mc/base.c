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

#include <subdev/mc.h>

void
nouveau_mc_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_mc *pmc = nouveau_mc(subdev);
	const struct nouveau_mc_intr *map = pmc->intr_map;
	struct nouveau_subdev *unit;
	u32 stat, intr;

	intr = stat = nv_rd32(pmc, 0x000100);
	while (stat && map->stat) {
		if (stat & map->stat) {
			unit = nouveau_subdev(subdev, map->unit);
			if (unit && unit->intr)
				unit->intr(unit);
			intr &= ~map->stat;
		}
		map++;
	}

	if (intr) {
		nv_error(pmc, "unknown intr 0x%08x\n", stat);
	}
}
