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
#include <subdev/ltc.h>
#include <subdev/ibus.h>
#include <subdev/instmem.h>
#include <subdev/mmu.h>
#include <subdev/bar.h>
#include <subdev/pmu.h>
#include <subdev/volt.h>

#include <engine/dmaobj.h>
#include <engine/fifo.h>
#include <engine/sw.h>
#include <engine/gr.h>
#include <engine/disp.h>
#include <engine/ce.h>
#include <engine/bsp.h>
#include <engine/msvld.h>
#include <engine/mspdec.h>
#include <engine/msppp.h>
#include <engine/pm.h>

int
gk104_identify(struct nvkm_device *device)
{
	switch (device->chipset) {
	case 0xe4:
		device->cname = "GK104";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  gk104_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  gk104_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gk104_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gf100_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  gf106_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  gf100_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gk104_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gk104_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &gk104_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &gf100_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &gf100_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gk104_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  gf110_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk104_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gk104_gr_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gk104_disp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gk104_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &gk104_ce1_oclass;
		device->oclass[NVDEV_ENGINE_CE2    ] = &gk104_ce2_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &gk104_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &gk104_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &gf100_msppp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &gk104_pm_oclass;
		break;
	case 0xe7:
		device->cname = "GK107";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  gk104_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  gk104_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gk104_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gf100_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  gf106_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  gf100_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gk104_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gk104_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &gk104_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &gf100_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &gf100_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gf110_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  gf110_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk104_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gk104_gr_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gk104_disp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gk104_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &gk104_ce1_oclass;
		device->oclass[NVDEV_ENGINE_CE2    ] = &gk104_ce2_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &gk104_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &gk104_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &gf100_msppp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &gk104_pm_oclass;
		break;
	case 0xe6:
		device->cname = "GK106";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  gk104_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  gk104_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gk104_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gf100_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  gf106_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  gf100_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gk104_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gk104_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &gk104_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &gf100_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &gf100_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gk104_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  gf110_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk104_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gk104_gr_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gk104_disp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gk104_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &gk104_ce1_oclass;
		device->oclass[NVDEV_ENGINE_CE2    ] = &gk104_ce2_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &gk104_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &gk104_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &gf100_msppp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &gk104_pm_oclass;
		break;
	case 0xea:
		device->cname = "GK20A";
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gk20a_clk_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  gk20a_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  gf100_bus_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &gk20a_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gk20a_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gk104_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &gk20a_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] = gk20a_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &gf100_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &gk20a_bar_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  gf110_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk20a_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gk20a_gr_oclass;
		device->oclass[NVDEV_ENGINE_CE2    ] = &gk104_ce2_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &gk104_pm_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &gk20a_volt_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gk20a_pmu_oclass;
		break;
	case 0xf0:
		device->cname = "GK110";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  gk104_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  gk104_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gk104_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gf100_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  gf106_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  gf100_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gk104_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gk104_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &gk104_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &gf100_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &gf100_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gk110_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  gf110_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk104_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gk110_gr_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gk110_disp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gk104_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &gk104_ce1_oclass;
		device->oclass[NVDEV_ENGINE_CE2    ] = &gk104_ce2_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &gk104_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &gk104_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &gf100_msppp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &gk110_pm_oclass;
		break;
	case 0xf1:
		device->cname = "GK110B";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  gk104_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  gf110_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gk104_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gf100_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  gf106_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  gf100_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gk104_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gk104_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &gk104_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &gf100_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &gf100_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gk110_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  gf110_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk104_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gk110b_gr_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gk110_disp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gk104_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &gk104_ce1_oclass;
		device->oclass[NVDEV_ENGINE_CE2    ] = &gk104_ce2_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &gk104_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &gk104_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &gf100_msppp_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = &gk110_pm_oclass;
		break;
	case 0x106:
		device->cname = "GK208B";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  gk104_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  gk104_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gk104_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gf100_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  gk20a_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  gf100_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gk104_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gk104_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &gk104_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &gf100_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &gf100_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gk208_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  gf110_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk208_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gk208_gr_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gk110_disp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gk104_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &gk104_ce1_oclass;
		device->oclass[NVDEV_ENGINE_CE2    ] = &gk104_ce2_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &gk104_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &gk104_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &gf100_msppp_oclass;
		break;
	case 0x108:
		device->cname = "GK208";
		device->oclass[NVDEV_SUBDEV_VBIOS  ] = &nvkm_bios_oclass;
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  gk104_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  gk104_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] = &gf100_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_CLK    ] = &gk104_clk_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_DEVINIT] =  gf100_devinit_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  gk20a_mc_oclass;
		device->oclass[NVDEV_SUBDEV_BUS    ] =  gf100_bus_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_FB     ] =  gk104_fb_oclass;
		device->oclass[NVDEV_SUBDEV_LTC    ] =  gk104_ltc_oclass;
		device->oclass[NVDEV_SUBDEV_IBUS   ] = &gk104_ibus_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &gf100_mmu_oclass;
		device->oclass[NVDEV_SUBDEV_BAR    ] = &gf100_bar_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gk208_pmu_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &nv40_volt_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  gf110_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk208_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gk208_gr_oclass;
		device->oclass[NVDEV_ENGINE_DISP   ] =  gk110_disp_oclass;
		device->oclass[NVDEV_ENGINE_CE0    ] = &gk104_ce0_oclass;
		device->oclass[NVDEV_ENGINE_CE1    ] = &gk104_ce1_oclass;
		device->oclass[NVDEV_ENGINE_CE2    ] = &gk104_ce2_oclass;
		device->oclass[NVDEV_ENGINE_MSVLD  ] = &gk104_msvld_oclass;
		device->oclass[NVDEV_ENGINE_MSPDEC ] = &gk104_mspdec_oclass;
		device->oclass[NVDEV_ENGINE_MSPPP  ] = &gf100_msppp_oclass;
		break;
	default:
		nv_fatal(device, "unknown Kepler chipset\n");
		return -EINVAL;
	}

	return 0;
}
