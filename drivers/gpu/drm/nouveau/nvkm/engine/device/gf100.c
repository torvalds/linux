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
gf100_identify(struct nvkm_device *device)
{
	switch (device->chipset) {
	case 0xc0:
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gf100_gr_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gf100_pm_oclass;
		break;
	case 0xc4:
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gf104_gr_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gf100_pm_oclass;
		break;
	case 0xc3:
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gf104_gr_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gf100_pm_oclass;
		break;
	case 0xce:
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gf104_gr_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gf100_pm_oclass;
		break;
	case 0xcf:
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gf104_gr_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gf100_pm_oclass;
		break;
	case 0xc1:
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gf108_gr_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gf108_pm_oclass;
		break;
	case 0xc8:
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gf110_gr_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gf100_pm_oclass;
		break;
	case 0xd9:
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gf119_gr_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gf117_pm_oclass;
		break;
	case 0xd7:
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gf117_gr_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gf117_pm_oclass;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
