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
#include <subdev/devinit.h>
#include <subdev/mc.h>
#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/instmem.h>
#include <subdev/vm.h>

#include <engine/device.h>
#include <engine/dmaobj.h>
#include <engine/fifo.h>
#include <engine/software.h>
#include <engine/graph.h>
#include <engine/disp.h>

int
nv10_identify(struct nouveau_device *device)
{
	switch (device->chipset) {
	case 0x10:
		device->cname = "NV10";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv04_clock_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nv10_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv04_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv10_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv04_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv10_graph_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nv04_disp_oclass;
		break;
	case 0x15:
		device->cname = "NV15";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv04_clock_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nv10_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv04_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv10_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv04_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv10_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv10_graph_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nv04_disp_oclass;
		break;
	case 0x16:
		device->cname = "NV16";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv04_clock_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nv10_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv04_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv10_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv04_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv10_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv10_graph_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nv04_disp_oclass;
		break;
	case 0x1a:
		device->cname = "nForce";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv04_clock_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv04_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv1a_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv04_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv10_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv10_graph_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nv04_disp_oclass;
		break;
	case 0x11:
		device->cname = "NV11";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv04_clock_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nv10_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv04_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv10_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv04_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv10_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv10_graph_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nv04_disp_oclass;
		break;
	case 0x17:
		device->cname = "NV17";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv04_clock_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nv10_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv04_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv10_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv04_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv17_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv10_graph_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nv04_disp_oclass;
		break;
	case 0x1f:
		device->cname = "nForce2";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv04_clock_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv04_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv1a_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv04_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv17_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv10_graph_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nv04_disp_oclass;
		break;
	case 0x18:
		device->cname = "NV18";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv04_clock_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nv10_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv04_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv10_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv04_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv17_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv10_graph_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nv04_disp_oclass;
		break;
	default:
		nv_fatal(device, "unknown Celsius chipset\n");
		return -EINVAL;
	}

	return 0;
}
