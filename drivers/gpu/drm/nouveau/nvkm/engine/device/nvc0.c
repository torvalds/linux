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
#include <subdev/fuse.h>
#include <subdev/clk.h>
#include <subdev/therm.h>
#include <subdev/mxm.h>
#include <subdev/devinit.h>
#include <subdev/mc.h>
#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/ltc.h>
#include <subdev/ibus.h>
#include <subdev/instmem.h>
#include <subdev/mmu.h>
#include <subdev/bar.h>
#include <subdev/pmu.h>
#include <subdev/volt.h>

#include <engine/device.h>
#include <engine/dmaobj.h>
#include <engine/fifo.h>
#include <engine/software.h>
#include <engine/gr.h>
#include <engine/vp.h>
#include <engine/bsp.h>
#include <engine/msvld.h>
#include <engine/ppp.h>
#include <engine/ce.h>
#include <engine/disp.h>
#include <engine/pm.h>

int
nvc0_identify(struct nouveau_device *device)
{
	switch (device->chipset) {
	case 0xc0:
		device->cname = "GF100";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &nvc0_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nvc0_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc0_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvc0_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gf100_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nvc0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nvc0_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  nvc0_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nvc0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nvc0_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  nvc0_gr_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nvc0_vp_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &nvc0_msvld_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &nvc0_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &nvc0_ce1_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &nvc0_pm_oclass;
		break;
	case 0xc4:
		device->cname = "GF104";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &nvc0_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nvc0_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc0_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvc0_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gf100_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nvc0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nvc0_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  nvc0_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nvc0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nvc0_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  nvc4_gr_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nvc0_vp_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &nvc0_msvld_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &nvc0_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &nvc0_ce1_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &nvc0_pm_oclass;
		break;
	case 0xc3:
		device->cname = "GF106";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &nvc0_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nvc0_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc3_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvc0_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gf100_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nvc0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nvc0_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  nvc0_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nvc0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nvc0_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  nvc4_gr_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nvc0_vp_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &nvc0_msvld_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &nvc0_ce0_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &nvc0_pm_oclass;
		break;
	case 0xce:
		device->cname = "GF114";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &nvc0_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nvc0_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc0_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvc0_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gf100_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nvc0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nvc0_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  nvc0_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nvc0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nvc0_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  nvc4_gr_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nvc0_vp_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &nvc0_msvld_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &nvc0_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &nvc0_ce1_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &nvc0_pm_oclass;
		break;
	case 0xcf:
		device->cname = "GF116";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &nvc0_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nvc0_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc3_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvc0_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gf100_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nvc0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nvc0_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  nvc0_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nvc0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nvc0_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  nvc4_gr_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nvc0_vp_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &nvc0_msvld_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &nvc0_ce0_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &nvc0_pm_oclass;
		break;
	case 0xc1:
		device->cname = "GF108";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &nvc0_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nvc0_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc3_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvc0_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gf100_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nvc0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nvc0_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  nvc0_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nvc0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nvc0_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  nvc1_gr_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nvc0_vp_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &nvc0_msvld_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &nvc0_ce0_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &nvc0_pm_oclass;
		break;
	case 0xc8:
		device->cname = "GF110";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &nvc0_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nva3_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nvc0_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc0_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvc0_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gf100_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nvc0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nvc0_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  nvc0_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nvc0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nvc0_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  nvc8_gr_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nvc0_vp_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &nvc0_msvld_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &nvc0_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &nvc0_ce1_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nva3_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &nvc0_pm_oclass;
		break;
	case 0xd9:
		device->cname = "GF119";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nvd0_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nvd0_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &nvc0_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nvd0_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nvc0_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc3_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvc0_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gf100_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nvc0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nvc0_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  nvd0_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nvd0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nvc0_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  nvd9_gr_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nvc0_vp_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &nvc0_msvld_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &nvc0_ce0_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nvd0_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &nvc0_pm_oclass;
		break;
	case 0xd7:
		device->cname = "GF117";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nouveau_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nvd0_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  gf117_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &nvc0_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nvd0_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  nvc0_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nvc3_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  nvc0_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  nvc0_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gf100_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &nvc0_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nvc0_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &nvc0_bar_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  nvd0_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  nvc0_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  nvc0_software_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  nvd7_gr_oclass;
		device->oclass[NVDEV_ENGINE_VP     ] = &nvc0_vp_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &nvc0_msvld_oclass;
		device->oclass[NVDEV_ENGINE_PPP    ] = &nvc0_ppp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &nvc0_ce0_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  nvd0_disp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &nvc0_pm_oclass;
		break;
	default:
		nv_fatal(device, "unknown Fermi chipset\n");
		return -EINVAL;
	}

	return 0;
	}
