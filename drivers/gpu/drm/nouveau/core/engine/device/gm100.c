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
#include <subdev/bus.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>
#include <subdev/clock.h>
#include <subdev/therm.h>
#include <subdev/mxm.h>
#include <subdev/devinit.h>
#include <subdev/mc.h>
#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/ltcg.h>
#include <subdev/ibus.h>
#include <subdev/instmem.h>
#include <subdev/vm.h>
#include <subdev/bar.h>
#include <subdev/pwr.h>
#include <subdev/volt.h>

#include <engine/device.h>
#include <engine/dmaobj.h>
#include <engine/fifo.h>
#include <engine/software.h>
#include <engine/graph.h>
#include <engine/disp.h>
#include <engine/copy.h>
#include <engine/bsp.h>
#include <engine/vp.h>
#include <engine/ppp.h>
#include <engine/perfmon.h>

int
gm100_identify(struct nouveau_device *device)
{
	switch (device->chipset) {
	case 0x117:
		device->cname = "GM107";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nve0_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nvd0_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nve0_clock_oclass;
#if 0
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nvd0_therm_oclass;
#endif
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gm107_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc3_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &gk20a_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gm107_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTCG   ] =  gm107_ltcg_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nve0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nvc0_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
#if 0
		device->oclass[NVDEV_SUBDEV_PWR    ] = &nv108_pwr_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
#endif
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nvd0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv108_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gm107_graph_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gm107_disp_oclass;
		device->oclass[NVDEV_ENGINE_COPY0  ] = &nve0_copy0_oclass;
#if 0
		device->oclass[NVDEV_ENGINE_COPY1  ] = &nve0_copy1_oclass;
#endif
		device->oclass[NVDEV_ENGINE_COPY2  ] = &nve0_copy2_oclass;
#if 0
		device->oclass[NVDEV_ENGINE_BSP    ] = &nve0_bsp_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nve0_vp_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
#endif
		break;
	default:
		nv_fatal(device, "unknown Maxwell chipset\n");
		return -EINVAL;
	}

	return 0;
}
