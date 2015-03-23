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

#include <subdev/bios.h>
#include <subdev/bus.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>
#include <subdev/fuse.h>
#include <subdev/clk.h>
#include <subdev/therm.h>
#include <subdev/mxm.h>
#include <subdev/devinit.h>
#include <subdev/mc.h>
#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/instmem.h>
#include <subdev/mmu.h>
#include <subdev/bar.h>
#include <subdev/pmu.h>
#include <subdev/volt.h>

#include <engine/dmaobj.h>
#include <engine/fifo.h>
#include <engine/sw.h>
#include <engine/gr.h>
#include <engine/mpeg.h>
#include <engine/vp.h>
#include <engine/cipher.h>
#include <engine/sec.h>
#include <engine/bsp.h>
#include <engine/msvld.h>
#include <engine/mspdec.h>
#include <engine/msppp.h>
#include <engine/ce.h>
#include <engine/disp.h>
#include <engine/pm.h>

int
nv50_identify(struct nvkm_device *device)
{
	switch (device->chipset) {
	case 0x50:
		device->cname = "G80";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  nv50_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv50_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nv50_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv50_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nv50_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nv50_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &nv50_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nv50_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  nv50_pm_oclass;
		break;
	case 0x84:
		device->cname = "G84";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  g84_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  g84_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv50_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  g84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &g84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &g84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CIPHER ] = &g84_cipher_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &g84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  g84_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  g84_pm_oclass;
		break;
	case 0x86:
		device->cname = "G86";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  g84_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  g84_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv50_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  g84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &g84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &g84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CIPHER ] = &g84_cipher_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &g84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  g84_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  g84_pm_oclass;
		break;
	case 0x92:
		device->cname = "G92";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  g84_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  g84_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nv50_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  g84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &g84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &g84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CIPHER ] = &g84_cipher_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &g84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  g84_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  g84_pm_oclass;
		break;
	case 0x94:
		device->cname = "G94";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  g84_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  g84_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g94_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  g84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &g84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &g84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CIPHER ] = &g84_cipher_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &g84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  g94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  g84_pm_oclass;
		break;
	case 0x96:
		device->cname = "G96";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  g84_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  g84_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g94_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  g84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &g84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &g84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CIPHER ] = &g84_cipher_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &g84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  g94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  g84_pm_oclass;
		break;
	case 0x98:
		device->cname = "G98";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  g84_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  g98_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  g84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &g98_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_SEC    ] = &g98_sec_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &g98_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &g98_msppp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  g94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  g84_pm_oclass;
		break;
	case 0xa0:
		device->cname = "G200";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  g84_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  g84_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  g84_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &g84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &g84_vp_oclass;
		device->oclass[NVDEV_ENGINE_CIPHER ] = &g84_cipher_oclass;
		device->oclass[NVDEV_ENGINE_BSP    ] = &g84_bsp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gt200_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  g84_pm_oclass;
		break;
	case 0xaa:
		device->cname = "MCP77/MCP78";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  mcp77_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  g98_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  mcp77_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &g98_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_SEC    ] = &g98_sec_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &g98_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &g98_msppp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  g94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  g84_pm_oclass;
		break;
	case 0xac:
		device->cname = "MCP79/MCP7A";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] =  mcp77_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  g98_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  mcp77_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &g98_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_SEC    ] = &g98_sec_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &g98_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &g98_msppp_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  g94_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  g84_pm_oclass;
		break;
	case 0xa3:
		device->cname = "GT215";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gt215_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gt215_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gt215_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gt215_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gt215_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MPEG   ] = &g84_mpeg_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &g98_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &g98_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &g98_msppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gt215_ce_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gt215_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  gt215_pm_oclass;
		break;
	case 0xa5:
		device->cname = "GT216";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gt215_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gt215_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gt215_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gt215_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gt215_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &g98_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &g98_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &g98_msppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gt215_ce_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gt215_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  gt215_pm_oclass;
		break;
	case 0xa8:
		device->cname = "GT218";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gt215_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gt215_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gt215_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gt215_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gt215_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &g98_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &g98_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &g98_msppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gt215_ce_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gt215_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  gt215_pm_oclass;
		break;
	case 0xaf:
		device->cname = "MCP89";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gt215_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gt215_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  mcp89_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  g94_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  mcp89_fb_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nv50_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gt215_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nv50_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  g84_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nv50_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] = &nv50_gr_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &g98_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &g98_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &g98_msppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gt215_ce_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gt215_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] =  gt215_pm_oclass;
		break;
	default:
		nv_fatal(device, "unknown Tesla chipset\n");
		return -EINVAL;
	}

	return 0;
}
