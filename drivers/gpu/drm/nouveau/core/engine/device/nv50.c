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
#include <engine/mpeg.h>
#include <engine/vp.h>
#include <engine/crypt.h>
#include <engine/bsp.h>
#include <engine/ppp.h>
#include <engine/copy.h>
#include <engine/disp.h>
#include <engine/perfmon.h>

int
nv50_identify(struct nouveau_device *device)
{
	switch (device->chipset) {
	case 0x50:
		device->cname = "G80";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv50_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv50_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv50_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv50_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv50_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv50_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv50_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv50_perfmon_oclass;
		break;
	case 0x84:
		device->cname = "G84";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv84_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv50_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CRYPT  ] = &nv84_crypt_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv84_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv84_perfmon_oclass;
		break;
	case 0x86:
		device->cname = "G86";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv84_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv50_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CRYPT  ] = &nv84_crypt_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv84_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv84_perfmon_oclass;
		break;
	case 0x92:
		device->cname = "G92";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv84_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv50_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CRYPT  ] = &nv84_crypt_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv84_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv84_perfmon_oclass;
		break;
	case 0x94:
		device->cname = "G94";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv84_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv94_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CRYPT  ] = &nv84_crypt_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv84_perfmon_oclass;
		break;
	case 0x96:
		device->cname = "G96";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv84_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv94_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CRYPT  ] = &nv84_crypt_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv84_perfmon_oclass;
		break;
	case 0x98:
		device->cname = "G98";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv84_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv98_vp_oclass;
		device->oclass[NVDEV_ENGINE_CRYPT  ] = &nv98_crypt_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv98_bsp_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nv98_ppp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv84_perfmon_oclass;
		break;
	case 0xa0:
		device->cname = "G200";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv84_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CRYPT  ] = &nv84_crypt_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nva0_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv84_perfmon_oclass;
		break;
	case 0xaa:
		device->cname = "MCP77/MCP78";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv84_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvaa_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv98_vp_oclass;
		device->oclass[NVDEV_ENGINE_CRYPT  ] = &nv98_crypt_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv98_bsp_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nv98_ppp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv84_perfmon_oclass;
		break;
	case 0xac:
		device->cname = "MCP79/MCP7A";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] =  nv84_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvaa_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv98_vp_oclass;
		device->oclass[NVDEV_ENGINE_CRYPT  ] = &nv98_crypt_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv98_bsp_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nv98_ppp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nv94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nv84_perfmon_oclass;
		break;
	case 0xa3:
		device->cname = "GT215";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nva3_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nva3_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nva3_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PWR    ] = &nva3_pwr_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv98_vp_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv98_bsp_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nv98_ppp_oclass;
		device->oclass[NVDEV_ENGINE_COPY0  ] = &nva3_copy_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nva3_perfmon_oclass;
		break;
	case 0xa5:
		device->cname = "GT216";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nva3_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nva3_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nva3_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PWR    ] = &nva3_pwr_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv98_vp_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv98_bsp_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nv98_ppp_oclass;
		device->oclass[NVDEV_ENGINE_COPY0  ] = &nva3_copy_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nva3_perfmon_oclass;
		break;
	case 0xa8:
		device->cname = "GT218";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nva3_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nva3_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nva3_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PWR    ] = &nva3_pwr_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv98_vp_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv98_bsp_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nv98_ppp_oclass;
		device->oclass[NVDEV_ENGINE_COPY0  ] = &nva3_copy_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nva3_perfmon_oclass;
		break;
	case 0xaf:
		device->cname = "MCP89";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] = &nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] = &nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_CLOCK  ] = &nva3_clock_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] = &nva3_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvaf_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = &nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_VM     ] = &nv50_vmmgr_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PWR    ] = &nva3_pwr_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] = &nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_graph_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nv98_vp_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &nv98_bsp_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nv98_ppp_oclass;
		device->oclass[NVDEV_ENGINE_COPY0  ] = &nva3_copy_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] = &nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PERFMON] =  nva3_perfmon_oclass;
		break;
	default:
		nv_fatal(device, "unknown Tesla chipset\n");
		return -EINVAL;
	}

	return 0;
}
