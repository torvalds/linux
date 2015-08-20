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
gk104_identify(struct nvkm_device *device)
{
	switch (device->chipset) {
	case 0xe4:
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
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
		device->oclass[NVDEV_ENGINE_PM     ] = gk104_pm_oclass;
		break;
	case 0xe7:
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
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
		device->oclass[NVDEV_ENGINE_PM     ] = gk104_pm_oclass;
		break;
	case 0xe6:
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
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
		device->oclass[NVDEV_ENGINE_PM     ] = gk104_pm_oclass;
		break;
	case 0xea:
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &gk20a_timer_oclass;
		device->oclass[NVDEV_ENGINE_DMAOBJ ] =  gf110_dmaeng_oclass;
		device->oclass[NVDEV_ENGINE_FIFO   ] =  gk20a_fifo_oclass;
		device->oclass[NVDEV_ENGINE_SW     ] =  gf100_sw_oclass;
		device->oclass[NVDEV_ENGINE_GR     ] =  gk20a_gr_oclass;
		device->oclass[NVDEV_ENGINE_CE2    ] = &gk104_ce2_oclass;
		device->oclass[NVDEV_ENGINE_PM     ] = gk104_pm_oclass;
		device->oclass[NVDEV_SUBDEV_VOLT   ] = &gk20a_volt_oclass;
		device->oclass[NVDEV_SUBDEV_PMU    ] =  gk20a_pmu_oclass;
		break;
	case 0xf0:
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
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
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
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
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
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
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gf110_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
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
		return -EINVAL;
	}

	return 0;
}
