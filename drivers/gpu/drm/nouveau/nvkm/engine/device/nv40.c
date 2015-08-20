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
nv40_identify(struct nvkm_device *device)
{
	switch (device->chipset) {
	case 0x40:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x41:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x42:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x43:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x45:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x47:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x49:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x4b:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x44:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x46:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x4a:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x4c:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x4e:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x63:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x67:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	case 0x68:
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv44_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv40_pm_oclass;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
