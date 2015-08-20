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

int
gm100_identify(struct nvkm_device *device)
{
	switch (device->chipset) {
	case 0x117:

#if 0
#endif
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk208_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gm107_gr_oclass;
#if 0
#endif
#if 0
#endif
		break;
	case 0x124:
#if 0
		/* looks to be some non-trivial changes */
		/* priv ring says no to 0x10eb14 writes */
#endif
#if 0
#endif
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gm204_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gm204_gr_oclass;
#if 0
#endif
		break;
	case 0x126:
#if 0
		/* looks to be some non-trivial changes */
		/* priv ring says no to 0x10eb14 writes */
#endif
#if 0
#endif
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gm204_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gm206_gr_oclass;
#if 0
#endif
		break;
	case 0x12b:

		device->oclass[NVDEV_ENGINE_FIFO   ] =  gm20b_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gm20b_gr_oclass;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
