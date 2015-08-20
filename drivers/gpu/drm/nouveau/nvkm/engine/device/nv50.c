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
nv50_identify(struct nvkm_device *device)
{
	switch (device->chipset) {
	case 0x50:
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &nv50_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  nv50_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  nv50_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g94_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g94_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  nv50_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_ENGINE_PM     ] =  gt200_pm_oclass;
		break;
	case 0xaa:
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &g84_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gt215_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gt215_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gt215_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		device->oclass[NVDEV_SUBDEV_GPIO   ] =  g94_gpio_oclass;
		device->oclass[NVDEV_SUBDEV_I2C    ] =  g94_i2c_oclass;
		device->oclass[NVDEV_SUBDEV_FUSE   ] =  &nv50_fuse_oclass;
		device->oclass[NVDEV_SUBDEV_THERM  ] = &gt215_therm_oclass;
		device->oclass[NVDEV_SUBDEV_MXM    ] = &nv50_mxm_oclass;
		device->oclass[NVDEV_SUBDEV_MC     ] =  g98_mc_oclass;
		device->oclass[NVDEV_SUBDEV_TIMER  ] = &nv04_timer_oclass;
		device->oclass[NVDEV_SUBDEV_INSTMEM] =  nv50_instmem_oclass;
		device->oclass[NVDEV_SUBDEV_MMU    ] = &nv50_mmu_oclass;
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
		return -EINVAL;
	}

	return 0;
}
