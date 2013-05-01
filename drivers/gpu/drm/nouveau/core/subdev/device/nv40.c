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

#include <subdev/device.h>
#include <subdev/bios.h>
#include <subdev/bus.h>
#include <subdev/vm.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>
#include <subdev/clock.h>
#include <subdev/therm.h>
#include <subdev/devinit.h>
#include <subdev/mc.h>
#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/instmem.h>
#include <subdev/vm.h>

#include <engine/dmaobj.h>
#include <engine/fifo.h>
#include <engine/software.h>
#include <engine/graph.h>
#include <engine/mpeg.h>
#include <engine/disp.h>

int
nv40_identify(struct nouveau_device *device)
{
	switch (device->chipset) {
	case 0x40:
		device->cname = "NV40";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv40_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x41:
		device->cname = "NV41";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv41_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv41_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x42:
		device->cname = "NV42";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv41_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv41_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x43:
		device->cname = "NV43";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv41_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv41_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x45:
		device->cname = "NV45";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv40_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv04_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x47:
		device->cname = "G70";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv47_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv41_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x49:
		device->cname = "G71";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv49_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv41_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x4b:
		device->cname = "G73";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv04_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv49_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv41_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x44:
		device->cname = "NV44";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv44_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv44_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv44_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x46:
		device->cname = "G72";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv44_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv46_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv44_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x4a:
		device->cname = "NV44A";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv44_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv44_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv44_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x4c:
		device->cname = "C61";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv44_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv46_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv44_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x4e:
		device->cname = "C51";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv4e_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv44_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv4e_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv44_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x63:
		device->cname = "C73";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv44_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv46_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv44_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x67:
		device->cname = "C67";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv44_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv46_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv44_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	case 0x68:
		device->cname = "C68";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv10_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv04_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nv40_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv40_therm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv1a_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] = &nv44_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] = &nv31_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] = &nv46_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv40_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv44_vmmgr_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv04_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] = &nv40_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] = &nv10_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv40_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv40_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv04_disp_oclass;
		break;
	default:
		nv_fatal(device, "unknown Curie chipset\n");
		return -EINVAL;
	}

	return 0;
}
