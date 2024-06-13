/*
 * Copyright 2007 Matthieu CASTET <castet.matthieu@free.fr>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragr) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "nv10.h"

static const struct nvkm_gr_func
nv17_gr = {
	.init = nv10_gr_init,
	.intr = nv10_gr_intr,
	.tile = nv10_gr_tile,
	.chan_new = nv10_gr_chan_new,
	.sclass = {
		{ -1, -1, 0x0012, &nv04_gr_object }, /* beta1 */
		{ -1, -1, 0x0019, &nv04_gr_object }, /* clip */
		{ -1, -1, 0x0030, &nv04_gr_object }, /* null */
		{ -1, -1, 0x0039, &nv04_gr_object }, /* m2mf */
		{ -1, -1, 0x0043, &nv04_gr_object }, /* rop */
		{ -1, -1, 0x0044, &nv04_gr_object }, /* pattern */
		{ -1, -1, 0x004a, &nv04_gr_object }, /* gdi */
		{ -1, -1, 0x0052, &nv04_gr_object }, /* swzsurf */
		{ -1, -1, 0x005f, &nv04_gr_object }, /* blit */
		{ -1, -1, 0x0062, &nv04_gr_object }, /* surf2d */
		{ -1, -1, 0x0072, &nv04_gr_object }, /* beta4 */
		{ -1, -1, 0x0089, &nv04_gr_object }, /* sifm */
		{ -1, -1, 0x008a, &nv04_gr_object }, /* ifc */
		{ -1, -1, 0x009f, &nv04_gr_object }, /* blit */
		{ -1, -1, 0x0093, &nv04_gr_object }, /* surf3d */
		{ -1, -1, 0x0094, &nv04_gr_object }, /* ttri */
		{ -1, -1, 0x0095, &nv04_gr_object }, /* mtri */
		{ -1, -1, 0x0099, &nv04_gr_object },
		{}
	}
};

int
nv17_gr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	return nv10_gr_new_(&nv17_gr, device, type, inst, pgr);
}
