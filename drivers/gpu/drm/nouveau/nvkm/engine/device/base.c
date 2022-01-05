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
#include "acpi.h"

#include <core/notify.h>
#include <core/option.h>

#include <subdev/bios.h>
#include <subdev/therm.h>

static DEFINE_MUTEX(nv_devices_mutex);
static LIST_HEAD(nv_devices);

static struct nvkm_device *
nvkm_device_find_locked(u64 handle)
{
	struct nvkm_device *device;
	list_for_each_entry(device, &nv_devices, head) {
		if (device->handle == handle)
			return device;
	}
	return NULL;
}

struct nvkm_device *
nvkm_device_find(u64 handle)
{
	struct nvkm_device *device;
	mutex_lock(&nv_devices_mutex);
	device = nvkm_device_find_locked(handle);
	mutex_unlock(&nv_devices_mutex);
	return device;
}

int
nvkm_device_list(u64 *name, int size)
{
	struct nvkm_device *device;
	int nr = 0;
	mutex_lock(&nv_devices_mutex);
	list_for_each_entry(device, &nv_devices, head) {
		if (nr++ < size)
			name[nr - 1] = device->handle;
	}
	mutex_unlock(&nv_devices_mutex);
	return nr;
}

static const struct nvkm_device_chip
null_chipset = {
	.name = "NULL",
	.bios     = { 0x00000001, nvkm_bios_new },
};

static const struct nvkm_device_chip
nv4_chipset = {
	.name = "NV04",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv04_devinit_new },
	.fb       = { 0x00000001, nv04_fb_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv04_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv04_fifo_new },
	.gr       = { 0x00000001, nv04_gr_new },
	.sw       = { 0x00000001, nv04_sw_new },
};

static const struct nvkm_device_chip
nv5_chipset = {
	.name = "NV05",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv05_devinit_new },
	.fb       = { 0x00000001, nv04_fb_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv04_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv04_fifo_new },
	.gr       = { 0x00000001, nv04_gr_new },
	.sw       = { 0x00000001, nv04_sw_new },
};

static const struct nvkm_device_chip
nv10_chipset = {
	.name = "NV10",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv10_devinit_new },
	.fb       = { 0x00000001, nv10_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv04_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.gr       = { 0x00000001, nv10_gr_new },
};

static const struct nvkm_device_chip
nv11_chipset = {
	.name = "NV11",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv10_devinit_new },
	.fb       = { 0x00000001, nv10_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv11_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv10_fifo_new },
	.gr       = { 0x00000001, nv15_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv15_chipset = {
	.name = "NV15",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv10_devinit_new },
	.fb       = { 0x00000001, nv10_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv04_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv10_fifo_new },
	.gr       = { 0x00000001, nv15_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv17_chipset = {
	.name = "NV17",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv10_devinit_new },
	.fb       = { 0x00000001, nv10_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv17_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv18_chipset = {
	.name = "NV18",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv10_devinit_new },
	.fb       = { 0x00000001, nv10_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv17_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv1a_chipset = {
	.name = "nForce",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv1a_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv04_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv10_fifo_new },
	.gr       = { 0x00000001, nv15_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv1f_chipset = {
	.name = "nForce2",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv1a_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv17_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv20_chipset = {
	.name = "NV20",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv20_devinit_new },
	.fb       = { 0x00000001, nv20_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv20_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv25_chipset = {
	.name = "NV25",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv20_devinit_new },
	.fb       = { 0x00000001, nv25_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv25_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv28_chipset = {
	.name = "NV28",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv20_devinit_new },
	.fb       = { 0x00000001, nv25_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv25_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv2a_chipset = {
	.name = "NV2A",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv20_devinit_new },
	.fb       = { 0x00000001, nv25_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv2a_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv30_chipset = {
	.name = "NV30",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv20_devinit_new },
	.fb       = { 0x00000001, nv30_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv30_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv31_chipset = {
	.name = "NV31",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv20_devinit_new },
	.fb       = { 0x00000001, nv30_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv30_gr_new },
	.mpeg     = { 0x00000001, nv31_mpeg_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv34_chipset = {
	.name = "NV34",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv10_devinit_new },
	.fb       = { 0x00000001, nv10_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv34_gr_new },
	.mpeg     = { 0x00000001, nv31_mpeg_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv35_chipset = {
	.name = "NV35",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv04_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv20_devinit_new },
	.fb       = { 0x00000001, nv35_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv35_gr_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv36_chipset = {
	.name = "NV36",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv04_clk_new },
	.devinit  = { 0x00000001, nv20_devinit_new },
	.fb       = { 0x00000001, nv36_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv04_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv04_pci_new },
	.timer    = { 0x00000001, nv04_timer_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv17_fifo_new },
	.gr       = { 0x00000001, nv35_gr_new },
	.mpeg     = { 0x00000001, nv31_mpeg_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv40_chipset = {
	.name = "NV40",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv40_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv40_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv40_gr_new },
	.mpeg     = { 0x00000001, nv40_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv41_chipset = {
	.name = "NV41",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv41_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv41_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv40_gr_new },
	.mpeg     = { 0x00000001, nv40_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv42_chipset = {
	.name = "NV42",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv41_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv41_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv40_gr_new },
	.mpeg     = { 0x00000001, nv40_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv43_chipset = {
	.name = "NV43",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv41_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv41_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv40_gr_new },
	.mpeg     = { 0x00000001, nv40_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv44_chipset = {
	.name = "NV44",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv44_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv44_mc_new },
	.mmu      = { 0x00000001, nv44_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv44_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv45_chipset = {
	.name = "NV45",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv40_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv40_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv46_chipset = {
	.name = "G72",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv46_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv44_mc_new },
	.mmu      = { 0x00000001, nv44_mmu_new },
	.pci      = { 0x00000001, nv46_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv44_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv47_chipset = {
	.name = "G70",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv47_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv41_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv40_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv49_chipset = {
	.name = "G71",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv49_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv41_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv40_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv4a_chipset = {
	.name = "NV44A",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv44_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv44_mc_new },
	.mmu      = { 0x00000001, nv04_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv44_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv4b_chipset = {
	.name = "G73",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv49_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv17_mc_new },
	.mmu      = { 0x00000001, nv41_mmu_new },
	.pci      = { 0x00000001, nv40_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv40_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv4c_chipset = {
	.name = "C61",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv46_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv44_mc_new },
	.mmu      = { 0x00000001, nv44_mmu_new },
	.pci      = { 0x00000001, nv4c_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv44_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv4e_chipset = {
	.name = "C51",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv4e_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv4e_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv44_mc_new },
	.mmu      = { 0x00000001, nv44_mmu_new },
	.pci      = { 0x00000001, nv4c_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv44_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv50_chipset = {
	.name = "G80",
	.bar      = { 0x00000001, nv50_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv50_bus_new },
	.clk      = { 0x00000001, nv50_clk_new },
	.devinit  = { 0x00000001, nv50_devinit_new },
	.fb       = { 0x00000001, nv50_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, nv50_gpio_new },
	.i2c      = { 0x00000001, nv50_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, nv50_mc_new },
	.mmu      = { 0x00000001, nv50_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, nv46_pci_new },
	.therm    = { 0x00000001, nv50_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv50_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, nv50_fifo_new },
	.gr       = { 0x00000001, nv50_gr_new },
	.mpeg     = { 0x00000001, nv50_mpeg_new },
	.pm       = { 0x00000001, nv50_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
};

static const struct nvkm_device_chip
nv63_chipset = {
	.name = "C73",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv46_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv44_mc_new },
	.mmu      = { 0x00000001, nv44_mmu_new },
	.pci      = { 0x00000001, nv4c_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv44_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv67_chipset = {
	.name = "C67",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv46_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv44_mc_new },
	.mmu      = { 0x00000001, nv44_mmu_new },
	.pci      = { 0x00000001, nv4c_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv44_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv68_chipset = {
	.name = "C68",
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv31_bus_new },
	.clk      = { 0x00000001, nv40_clk_new },
	.devinit  = { 0x00000001, nv1a_devinit_new },
	.fb       = { 0x00000001, nv46_fb_new },
	.gpio     = { 0x00000001, nv10_gpio_new },
	.i2c      = { 0x00000001, nv04_i2c_new },
	.imem     = { 0x00000001, nv40_instmem_new },
	.mc       = { 0x00000001, nv44_mc_new },
	.mmu      = { 0x00000001, nv44_mmu_new },
	.pci      = { 0x00000001, nv4c_pci_new },
	.therm    = { 0x00000001, nv40_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, nv04_disp_new },
	.dma      = { 0x00000001, nv04_dma_new },
	.fifo     = { 0x00000001, nv40_fifo_new },
	.gr       = { 0x00000001, nv44_gr_new },
	.mpeg     = { 0x00000001, nv44_mpeg_new },
	.pm       = { 0x00000001, nv40_pm_new },
	.sw       = { 0x00000001, nv10_sw_new },
};

static const struct nvkm_device_chip
nv84_chipset = {
	.name = "G84",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv50_bus_new },
	.clk      = { 0x00000001, g84_clk_new },
	.devinit  = { 0x00000001, g84_devinit_new },
	.fb       = { 0x00000001, g84_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, nv50_gpio_new },
	.i2c      = { 0x00000001, nv50_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, g84_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g84_pci_new },
	.therm    = { 0x00000001, g84_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.bsp      = { 0x00000001, g84_bsp_new },
	.cipher   = { 0x00000001, g84_cipher_new },
	.disp     = { 0x00000001, g84_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, g84_gr_new },
	.mpeg     = { 0x00000001, g84_mpeg_new },
	.pm       = { 0x00000001, g84_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
	.vp       = { 0x00000001, g84_vp_new },
};

static const struct nvkm_device_chip
nv86_chipset = {
	.name = "G86",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv50_bus_new },
	.clk      = { 0x00000001, g84_clk_new },
	.devinit  = { 0x00000001, g84_devinit_new },
	.fb       = { 0x00000001, g84_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, nv50_gpio_new },
	.i2c      = { 0x00000001, nv50_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, g84_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g84_pci_new },
	.therm    = { 0x00000001, g84_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.bsp      = { 0x00000001, g84_bsp_new },
	.cipher   = { 0x00000001, g84_cipher_new },
	.disp     = { 0x00000001, g84_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, g84_gr_new },
	.mpeg     = { 0x00000001, g84_mpeg_new },
	.pm       = { 0x00000001, g84_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
	.vp       = { 0x00000001, g84_vp_new },
};

static const struct nvkm_device_chip
nv92_chipset = {
	.name = "G92",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, nv50_bus_new },
	.clk      = { 0x00000001, g84_clk_new },
	.devinit  = { 0x00000001, g84_devinit_new },
	.fb       = { 0x00000001, g84_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, nv50_gpio_new },
	.i2c      = { 0x00000001, nv50_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, g84_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g92_pci_new },
	.therm    = { 0x00000001, g84_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.bsp      = { 0x00000001, g84_bsp_new },
	.cipher   = { 0x00000001, g84_cipher_new },
	.disp     = { 0x00000001, g84_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, g84_gr_new },
	.mpeg     = { 0x00000001, g84_mpeg_new },
	.pm       = { 0x00000001, g84_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
	.vp       = { 0x00000001, g84_vp_new },
};

static const struct nvkm_device_chip
nv94_chipset = {
	.name = "G94",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, g84_clk_new },
	.devinit  = { 0x00000001, g84_devinit_new },
	.fb       = { 0x00000001, g84_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, g84_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.therm    = { 0x00000001, g84_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.bsp      = { 0x00000001, g84_bsp_new },
	.cipher   = { 0x00000001, g84_cipher_new },
	.disp     = { 0x00000001, g94_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, g84_gr_new },
	.mpeg     = { 0x00000001, g84_mpeg_new },
	.pm       = { 0x00000001, g84_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
	.vp       = { 0x00000001, g84_vp_new },
};

static const struct nvkm_device_chip
nv96_chipset = {
	.name = "G96",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, g84_clk_new },
	.devinit  = { 0x00000001, g84_devinit_new },
	.fb       = { 0x00000001, g84_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, g84_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.therm    = { 0x00000001, g84_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.bsp      = { 0x00000001, g84_bsp_new },
	.cipher   = { 0x00000001, g84_cipher_new },
	.disp     = { 0x00000001, g94_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, g84_gr_new },
	.mpeg     = { 0x00000001, g84_mpeg_new },
	.pm       = { 0x00000001, g84_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
	.vp       = { 0x00000001, g84_vp_new },
};

static const struct nvkm_device_chip
nv98_chipset = {
	.name = "G98",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, g84_clk_new },
	.devinit  = { 0x00000001, g98_devinit_new },
	.fb       = { 0x00000001, g84_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, g98_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.therm    = { 0x00000001, g84_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, g94_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, g84_gr_new },
	.mspdec   = { 0x00000001, g98_mspdec_new },
	.msppp    = { 0x00000001, g98_msppp_new },
	.msvld    = { 0x00000001, g98_msvld_new },
	.pm       = { 0x00000001, g84_pm_new },
	.sec      = { 0x00000001, g98_sec_new },
	.sw       = { 0x00000001, nv50_sw_new },
};

static const struct nvkm_device_chip
nva0_chipset = {
	.name = "GT200",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, g84_clk_new },
	.devinit  = { 0x00000001, g84_devinit_new },
	.fb       = { 0x00000001, g84_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, nv50_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, g84_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.therm    = { 0x00000001, g84_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.bsp      = { 0x00000001, g84_bsp_new },
	.cipher   = { 0x00000001, g84_cipher_new },
	.disp     = { 0x00000001, gt200_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, gt200_gr_new },
	.mpeg     = { 0x00000001, g84_mpeg_new },
	.pm       = { 0x00000001, gt200_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
	.vp       = { 0x00000001, g84_vp_new },
};

static const struct nvkm_device_chip
nva3_chipset = {
	.name = "GT215",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, gt215_clk_new },
	.devinit  = { 0x00000001, gt215_devinit_new },
	.fb       = { 0x00000001, gt215_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, gt215_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.pmu      = { 0x00000001, gt215_pmu_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.ce       = { 0x00000001, gt215_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, gt215_gr_new },
	.mpeg     = { 0x00000001, g84_mpeg_new },
	.mspdec   = { 0x00000001, gt215_mspdec_new },
	.msppp    = { 0x00000001, gt215_msppp_new },
	.msvld    = { 0x00000001, gt215_msvld_new },
	.pm       = { 0x00000001, gt215_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
};

static const struct nvkm_device_chip
nva5_chipset = {
	.name = "GT216",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, gt215_clk_new },
	.devinit  = { 0x00000001, gt215_devinit_new },
	.fb       = { 0x00000001, gt215_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, gt215_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.pmu      = { 0x00000001, gt215_pmu_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.ce       = { 0x00000001, gt215_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, gt215_gr_new },
	.mspdec   = { 0x00000001, gt215_mspdec_new },
	.msppp    = { 0x00000001, gt215_msppp_new },
	.msvld    = { 0x00000001, gt215_msvld_new },
	.pm       = { 0x00000001, gt215_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
};

static const struct nvkm_device_chip
nva8_chipset = {
	.name = "GT218",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, gt215_clk_new },
	.devinit  = { 0x00000001, gt215_devinit_new },
	.fb       = { 0x00000001, gt215_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, gt215_mc_new },
	.mmu      = { 0x00000001, g84_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.pmu      = { 0x00000001, gt215_pmu_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.ce       = { 0x00000001, gt215_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, gt215_gr_new },
	.mspdec   = { 0x00000001, gt215_mspdec_new },
	.msppp    = { 0x00000001, gt215_msppp_new },
	.msvld    = { 0x00000001, gt215_msvld_new },
	.pm       = { 0x00000001, gt215_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
};

static const struct nvkm_device_chip
nvaa_chipset = {
	.name = "MCP77/MCP78",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, mcp77_clk_new },
	.devinit  = { 0x00000001, g98_devinit_new },
	.fb       = { 0x00000001, mcp77_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, g98_mc_new },
	.mmu      = { 0x00000001, mcp77_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.therm    = { 0x00000001, g84_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, mcp77_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, gt200_gr_new },
	.mspdec   = { 0x00000001, g98_mspdec_new },
	.msppp    = { 0x00000001, g98_msppp_new },
	.msvld    = { 0x00000001, g98_msvld_new },
	.pm       = { 0x00000001, g84_pm_new },
	.sec      = { 0x00000001, g98_sec_new },
	.sw       = { 0x00000001, nv50_sw_new },
};

static const struct nvkm_device_chip
nvac_chipset = {
	.name = "MCP79/MCP7A",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, mcp77_clk_new },
	.devinit  = { 0x00000001, g98_devinit_new },
	.fb       = { 0x00000001, mcp77_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, g98_mc_new },
	.mmu      = { 0x00000001, mcp77_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.therm    = { 0x00000001, g84_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.disp     = { 0x00000001, mcp77_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, mcp79_gr_new },
	.mspdec   = { 0x00000001, g98_mspdec_new },
	.msppp    = { 0x00000001, g98_msppp_new },
	.msvld    = { 0x00000001, g98_msvld_new },
	.pm       = { 0x00000001, g84_pm_new },
	.sec      = { 0x00000001, g98_sec_new },
	.sw       = { 0x00000001, nv50_sw_new },
};

static const struct nvkm_device_chip
nvaf_chipset = {
	.name = "MCP89",
	.bar      = { 0x00000001, g84_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, g94_bus_new },
	.clk      = { 0x00000001, gt215_clk_new },
	.devinit  = { 0x00000001, mcp89_devinit_new },
	.fb       = { 0x00000001, mcp89_fb_new },
	.fuse     = { 0x00000001, nv50_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, gt215_mc_new },
	.mmu      = { 0x00000001, mcp77_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, g94_pci_new },
	.pmu      = { 0x00000001, gt215_pmu_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, nv40_volt_new },
	.ce       = { 0x00000001, gt215_ce_new },
	.disp     = { 0x00000001, mcp89_disp_new },
	.dma      = { 0x00000001, nv50_dma_new },
	.fifo     = { 0x00000001, g84_fifo_new },
	.gr       = { 0x00000001, mcp89_gr_new },
	.mspdec   = { 0x00000001, gt215_mspdec_new },
	.msppp    = { 0x00000001, gt215_msppp_new },
	.msvld    = { 0x00000001, mcp89_msvld_new },
	.pm       = { 0x00000001, gt215_pm_new },
	.sw       = { 0x00000001, nv50_sw_new },
};

static const struct nvkm_device_chip
nvc0_chipset = {
	.name = "GF100",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gf100_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gf100_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gf100_ltc_new },
	.mc       = { 0x00000001, gf100_mc_new },
	.mmu      = { 0x00000001, gf100_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gf100_pci_new },
	.pmu      = { 0x00000001, gf100_pmu_new },
	.privring = { 0x00000001, gf100_privring_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, gf100_volt_new },
	.ce       = { 0x00000003, gf100_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, gf100_dma_new },
	.fifo     = { 0x00000001, gf100_fifo_new },
	.gr       = { 0x00000001, gf100_gr_new },
	.mspdec   = { 0x00000001, gf100_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gf100_msvld_new },
	.pm       = { 0x00000001, gf100_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvc1_chipset = {
	.name = "GF108",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gf100_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gf108_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gf100_ltc_new },
	.mc       = { 0x00000001, gf100_mc_new },
	.mmu      = { 0x00000001, gf100_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gf106_pci_new },
	.pmu      = { 0x00000001, gf100_pmu_new },
	.privring = { 0x00000001, gf100_privring_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, gf100_volt_new },
	.ce       = { 0x00000001, gf100_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, gf100_dma_new },
	.fifo     = { 0x00000001, gf100_fifo_new },
	.gr       = { 0x00000001, gf108_gr_new },
	.mspdec   = { 0x00000001, gf100_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gf100_msvld_new },
	.pm       = { 0x00000001, gf108_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvc3_chipset = {
	.name = "GF106",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gf100_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gf100_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gf100_ltc_new },
	.mc       = { 0x00000001, gf100_mc_new },
	.mmu      = { 0x00000001, gf100_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gf106_pci_new },
	.pmu      = { 0x00000001, gf100_pmu_new },
	.privring = { 0x00000001, gf100_privring_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, gf100_volt_new },
	.ce       = { 0x00000001, gf100_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, gf100_dma_new },
	.fifo     = { 0x00000001, gf100_fifo_new },
	.gr       = { 0x00000001, gf104_gr_new },
	.mspdec   = { 0x00000001, gf100_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gf100_msvld_new },
	.pm       = { 0x00000001, gf100_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvc4_chipset = {
	.name = "GF104",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gf100_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gf100_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gf100_ltc_new },
	.mc       = { 0x00000001, gf100_mc_new },
	.mmu      = { 0x00000001, gf100_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gf100_pci_new },
	.pmu      = { 0x00000001, gf100_pmu_new },
	.privring = { 0x00000001, gf100_privring_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, gf100_volt_new },
	.ce       = { 0x00000003, gf100_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, gf100_dma_new },
	.fifo     = { 0x00000001, gf100_fifo_new },
	.gr       = { 0x00000001, gf104_gr_new },
	.mspdec   = { 0x00000001, gf100_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gf100_msvld_new },
	.pm       = { 0x00000001, gf100_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvc8_chipset = {
	.name = "GF110",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gf100_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gf100_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gf100_ltc_new },
	.mc       = { 0x00000001, gf100_mc_new },
	.mmu      = { 0x00000001, gf100_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gf100_pci_new },
	.pmu      = { 0x00000001, gf100_pmu_new },
	.privring = { 0x00000001, gf100_privring_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, gf100_volt_new },
	.ce       = { 0x00000003, gf100_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, gf100_dma_new },
	.fifo     = { 0x00000001, gf100_fifo_new },
	.gr       = { 0x00000001, gf110_gr_new },
	.mspdec   = { 0x00000001, gf100_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gf100_msvld_new },
	.pm       = { 0x00000001, gf100_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvce_chipset = {
	.name = "GF114",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gf100_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gf100_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gf100_ltc_new },
	.mc       = { 0x00000001, gf100_mc_new },
	.mmu      = { 0x00000001, gf100_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gf100_pci_new },
	.pmu      = { 0x00000001, gf100_pmu_new },
	.privring = { 0x00000001, gf100_privring_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, gf100_volt_new },
	.ce       = { 0x00000003, gf100_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, gf100_dma_new },
	.fifo     = { 0x00000001, gf100_fifo_new },
	.gr       = { 0x00000001, gf104_gr_new },
	.mspdec   = { 0x00000001, gf100_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gf100_msvld_new },
	.pm       = { 0x00000001, gf100_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvcf_chipset = {
	.name = "GF116",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gf100_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gf100_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, g94_gpio_new },
	.i2c      = { 0x00000001, g94_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gf100_ltc_new },
	.mc       = { 0x00000001, gf100_mc_new },
	.mmu      = { 0x00000001, gf100_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gf106_pci_new },
	.pmu      = { 0x00000001, gf100_pmu_new },
	.privring = { 0x00000001, gf100_privring_new },
	.therm    = { 0x00000001, gt215_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, gf100_volt_new },
	.ce       = { 0x00000001, gf100_ce_new },
	.disp     = { 0x00000001, gt215_disp_new },
	.dma      = { 0x00000001, gf100_dma_new },
	.fifo     = { 0x00000001, gf100_fifo_new },
	.gr       = { 0x00000001, gf104_gr_new },
	.mspdec   = { 0x00000001, gf100_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gf100_msvld_new },
	.pm       = { 0x00000001, gf100_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvd7_chipset = {
	.name = "GF117",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gf100_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gf100_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, gf119_gpio_new },
	.i2c      = { 0x00000001, gf117_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gf100_ltc_new },
	.mc       = { 0x00000001, gf100_mc_new },
	.mmu      = { 0x00000001, gf100_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gf106_pci_new },
	.privring = { 0x00000001, gf117_privring_new },
	.therm    = { 0x00000001, gf119_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, gf117_volt_new },
	.ce       = { 0x00000001, gf100_ce_new },
	.disp     = { 0x00000001, gf119_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gf100_fifo_new },
	.gr       = { 0x00000001, gf117_gr_new },
	.mspdec   = { 0x00000001, gf100_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gf100_msvld_new },
	.pm       = { 0x00000001, gf117_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvd9_chipset = {
	.name = "GF119",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gf100_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gf100_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, gf119_gpio_new },
	.i2c      = { 0x00000001, gf119_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gf100_ltc_new },
	.mc       = { 0x00000001, gf100_mc_new },
	.mmu      = { 0x00000001, gf100_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gf106_pci_new },
	.pmu      = { 0x00000001, gf119_pmu_new },
	.privring = { 0x00000001, gf117_privring_new },
	.therm    = { 0x00000001, gf119_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.volt     = { 0x00000001, gf100_volt_new },
	.ce       = { 0x00000001, gf100_ce_new },
	.disp     = { 0x00000001, gf119_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gf100_fifo_new },
	.gr       = { 0x00000001, gf119_gr_new },
	.mspdec   = { 0x00000001, gf100_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gf100_msvld_new },
	.pm       = { 0x00000001, gf117_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nve4_chipset = {
	.name = "GK104",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk104_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gk104_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gk104_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gk104_ltc_new },
	.mc       = { 0x00000001, gk104_mc_new },
	.mmu      = { 0x00000001, gk104_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gk104_pmu_new },
	.privring = { 0x00000001, gk104_privring_new },
	.therm    = { 0x00000001, gk104_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gk104_ce_new },
	.disp     = { 0x00000001, gk104_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gk104_fifo_new },
	.gr       = { 0x00000001, gk104_gr_new },
	.mspdec   = { 0x00000001, gk104_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gk104_msvld_new },
	.pm       = { 0x00000001, gk104_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nve6_chipset = {
	.name = "GK106",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk104_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gk104_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gk104_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gk104_ltc_new },
	.mc       = { 0x00000001, gk104_mc_new },
	.mmu      = { 0x00000001, gk104_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gk104_pmu_new },
	.privring = { 0x00000001, gk104_privring_new },
	.therm    = { 0x00000001, gk104_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gk104_ce_new },
	.disp     = { 0x00000001, gk104_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gk104_fifo_new },
	.gr       = { 0x00000001, gk104_gr_new },
	.mspdec   = { 0x00000001, gk104_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gk104_msvld_new },
	.pm       = { 0x00000001, gk104_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nve7_chipset = {
	.name = "GK107",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk104_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gk104_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gk104_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gk104_ltc_new },
	.mc       = { 0x00000001, gk104_mc_new },
	.mmu      = { 0x00000001, gk104_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gk104_pmu_new },
	.privring = { 0x00000001, gk104_privring_new },
	.therm    = { 0x00000001, gk104_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gk104_ce_new },
	.disp     = { 0x00000001, gk104_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gk104_fifo_new },
	.gr       = { 0x00000001, gk104_gr_new },
	.mspdec   = { 0x00000001, gk104_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gk104_msvld_new },
	.pm       = { 0x00000001, gk104_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvea_chipset = {
	.name = "GK20A",
	.bar      = { 0x00000001, gk20a_bar_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk20a_clk_new },
	.fb       = { 0x00000001, gk20a_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.imem     = { 0x00000001, gk20a_instmem_new },
	.ltc      = { 0x00000001, gk104_ltc_new },
	.mc       = { 0x00000001, gk20a_mc_new },
	.mmu      = { 0x00000001, gk20a_mmu_new },
	.pmu      = { 0x00000001, gk20a_pmu_new },
	.privring = { 0x00000001, gk20a_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk20a_volt_new },
	.ce       = { 0x00000004, gk104_ce_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gk20a_fifo_new },
	.gr       = { 0x00000001, gk20a_gr_new },
	.pm       = { 0x00000001, gk104_pm_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvf0_chipset = {
	.name = "GK110",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk104_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gk110_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gk110_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gk104_ltc_new },
	.mc       = { 0x00000001, gk104_mc_new },
	.mmu      = { 0x00000001, gk104_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gk110_pmu_new },
	.privring = { 0x00000001, gk104_privring_new },
	.therm    = { 0x00000001, gk104_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gk104_ce_new },
	.disp     = { 0x00000001, gk110_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gk110_fifo_new },
	.gr       = { 0x00000001, gk110_gr_new },
	.mspdec   = { 0x00000001, gk104_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gk104_msvld_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nvf1_chipset = {
	.name = "GK110B",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk104_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gk110_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gk110_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gk104_ltc_new },
	.mc       = { 0x00000001, gk104_mc_new },
	.mmu      = { 0x00000001, gk104_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gk110_pmu_new },
	.privring = { 0x00000001, gk104_privring_new },
	.therm    = { 0x00000001, gk104_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gk104_ce_new },
	.disp     = { 0x00000001, gk110_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gk110_fifo_new },
	.gr       = { 0x00000001, gk110b_gr_new },
	.mspdec   = { 0x00000001, gk104_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gk104_msvld_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv106_chipset = {
	.name = "GK208B",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk104_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gk110_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gk110_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gk104_ltc_new },
	.mc       = { 0x00000001, gk20a_mc_new },
	.mmu      = { 0x00000001, gk104_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gk208_pmu_new },
	.privring = { 0x00000001, gk104_privring_new },
	.therm    = { 0x00000001, gk104_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gk104_ce_new },
	.disp     = { 0x00000001, gk110_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gk208_fifo_new },
	.gr       = { 0x00000001, gk208_gr_new },
	.mspdec   = { 0x00000001, gk104_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gk104_msvld_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv108_chipset = {
	.name = "GK208",
	.bar      = { 0x00000001, gf100_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk104_clk_new },
	.devinit  = { 0x00000001, gf100_devinit_new },
	.fb       = { 0x00000001, gk110_fb_new },
	.fuse     = { 0x00000001, gf100_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gk110_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gk104_ltc_new },
	.mc       = { 0x00000001, gk20a_mc_new },
	.mmu      = { 0x00000001, gk104_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gk208_pmu_new },
	.privring = { 0x00000001, gk104_privring_new },
	.therm    = { 0x00000001, gk104_therm_new },
	.timer    = { 0x00000001, nv41_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gk104_ce_new },
	.disp     = { 0x00000001, gk110_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gk208_fifo_new },
	.gr       = { 0x00000001, gk208_gr_new },
	.mspdec   = { 0x00000001, gk104_mspdec_new },
	.msppp    = { 0x00000001, gf100_msppp_new },
	.msvld    = { 0x00000001, gk104_msvld_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv117_chipset = {
	.name = "GM107",
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk104_clk_new },
	.devinit  = { 0x00000001, gm107_devinit_new },
	.fb       = { 0x00000001, gm107_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gk110_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gm107_ltc_new },
	.mc       = { 0x00000001, gk20a_mc_new },
	.mmu      = { 0x00000001, gk104_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gm107_pmu_new },
	.privring = { 0x00000001, gk104_privring_new },
	.therm    = { 0x00000001, gm107_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000005, gm107_ce_new },
	.disp     = { 0x00000001, gm107_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gm107_fifo_new },
	.gr       = { 0x00000001, gm107_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000001, gm107_nvenc_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv118_chipset = {
	.name = "GM108",
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gk104_clk_new },
	.devinit  = { 0x00000001, gm107_devinit_new },
	.fb       = { 0x00000001, gm107_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gk110_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gm107_ltc_new },
	.mc       = { 0x00000001, gk20a_mc_new },
	.mmu      = { 0x00000001, gk104_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gm107_pmu_new },
	.privring = { 0x00000001, gk104_privring_new },
	.therm    = { 0x00000001, gm107_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000005, gm107_ce_new },
	.disp     = { 0x00000001, gm107_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gm107_fifo_new },
	.gr       = { 0x00000001, gm107_gr_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv120_chipset = {
	.name = "GM200",
	.acr      = { 0x00000001, gm200_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gm200_devinit_new },
	.fb       = { 0x00000001, gm200_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gm200_ltc_new },
	.mc       = { 0x00000001, gk20a_mc_new },
	.mmu      = { 0x00000001, gm200_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gm200_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.therm    = { 0x00000001, gm200_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gm200_ce_new },
	.disp     = { 0x00000001, gm200_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gm200_fifo_new },
	.gr       = { 0x00000001, gm200_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000003, gm107_nvenc_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv124_chipset = {
	.name = "GM204",
	.acr      = { 0x00000001, gm200_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gm200_devinit_new },
	.fb       = { 0x00000001, gm200_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gm200_ltc_new },
	.mc       = { 0x00000001, gk20a_mc_new },
	.mmu      = { 0x00000001, gm200_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gm200_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.therm    = { 0x00000001, gm200_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gm200_ce_new },
	.disp     = { 0x00000001, gm200_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gm200_fifo_new },
	.gr       = { 0x00000001, gm200_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000003, gm107_nvenc_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv126_chipset = {
	.name = "GM206",
	.acr      = { 0x00000001, gm200_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gm200_devinit_new },
	.fb       = { 0x00000001, gm200_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.iccsense = { 0x00000001, gf100_iccsense_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gm200_ltc_new },
	.mc       = { 0x00000001, gk20a_mc_new },
	.mmu      = { 0x00000001, gm200_mmu_new },
	.mxm      = { 0x00000001, nv50_mxm_new },
	.pci      = { 0x00000001, gk104_pci_new },
	.pmu      = { 0x00000001, gm200_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.therm    = { 0x00000001, gm200_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gk104_volt_new },
	.ce       = { 0x00000007, gm200_ce_new },
	.disp     = { 0x00000001, gm200_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gm200_fifo_new },
	.gr       = { 0x00000001, gm200_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000001, gm107_nvenc_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv12b_chipset = {
	.name = "GM20B",
	.acr      = { 0x00000001, gm20b_acr_new },
	.bar      = { 0x00000001, gm20b_bar_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.clk      = { 0x00000001, gm20b_clk_new },
	.fb       = { 0x00000001, gm20b_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.imem     = { 0x00000001, gk20a_instmem_new },
	.ltc      = { 0x00000001, gm200_ltc_new },
	.mc       = { 0x00000001, gk20a_mc_new },
	.mmu      = { 0x00000001, gm20b_mmu_new },
	.pmu      = { 0x00000001, gm20b_pmu_new },
	.privring = { 0x00000001, gk20a_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.volt     = { 0x00000001, gm20b_volt_new },
	.ce       = { 0x00000004, gm200_ce_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gm20b_fifo_new },
	.gr       = { 0x00000001, gm20b_gr_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv130_chipset = {
	.name = "GP100",
	.acr      = { 0x00000001, gm200_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gm200_devinit_new },
	.fault    = { 0x00000001, gp100_fault_new },
	.fb       = { 0x00000001, gp100_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp100_ltc_new },
	.mc       = { 0x00000001, gp100_mc_new },
	.mmu      = { 0x00000001, gp100_mmu_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gm200_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000003f, gp100_ce_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.disp     = { 0x00000001, gp100_disp_new },
	.fifo     = { 0x00000001, gp100_fifo_new },
	.gr       = { 0x00000001, gp100_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000007, gm107_nvenc_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv132_chipset = {
	.name = "GP102",
	.acr      = { 0x00000001, gp102_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gm200_devinit_new },
	.fault    = { 0x00000001, gp100_fault_new },
	.fb       = { 0x00000001, gp102_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, gp100_mc_new },
	.mmu      = { 0x00000001, gp100_mmu_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000000f, gp102_ce_new },
	.disp     = { 0x00000001, gp102_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gp100_fifo_new },
	.gr       = { 0x00000001, gp102_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000003, gm107_nvenc_new },
	.sec2     = { 0x00000001, gp102_sec2_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv134_chipset = {
	.name = "GP104",
	.acr      = { 0x00000001, gp102_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gm200_devinit_new },
	.fault    = { 0x00000001, gp100_fault_new },
	.fb       = { 0x00000001, gp102_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, gp100_mc_new },
	.mmu      = { 0x00000001, gp100_mmu_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000000f, gp102_ce_new },
	.disp     = { 0x00000001, gp102_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gp100_fifo_new },
	.gr       = { 0x00000001, gp104_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000003, gm107_nvenc_new },
	.sec2     = { 0x00000001, gp102_sec2_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv136_chipset = {
	.name = "GP106",
	.acr      = { 0x00000001, gp102_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gm200_devinit_new },
	.fault    = { 0x00000001, gp100_fault_new },
	.fb       = { 0x00000001, gp102_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, gp100_mc_new },
	.mmu      = { 0x00000001, gp100_mmu_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000000f, gp102_ce_new },
	.disp     = { 0x00000001, gp102_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gp100_fifo_new },
	.gr       = { 0x00000001, gp104_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000001, gm107_nvenc_new },
	.sec2     = { 0x00000001, gp102_sec2_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv137_chipset = {
	.name = "GP107",
	.acr      = { 0x00000001, gp102_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gm200_devinit_new },
	.fault    = { 0x00000001, gp100_fault_new },
	.fb       = { 0x00000001, gp102_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, gp100_mc_new },
	.mmu      = { 0x00000001, gp100_mmu_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000000f, gp102_ce_new },
	.disp     = { 0x00000001, gp102_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gp100_fifo_new },
	.gr       = { 0x00000001, gp107_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000003, gm107_nvenc_new },
	.sec2     = { 0x00000001, gp102_sec2_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv138_chipset = {
	.name = "GP108",
	.acr      = { 0x00000001, gp108_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gm200_devinit_new },
	.fault    = { 0x00000001, gp100_fault_new },
	.fb       = { 0x00000001, gp102_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, gp100_mc_new },
	.mmu      = { 0x00000001, gp100_mmu_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000000f, gp102_ce_new },
	.disp     = { 0x00000001, gp102_disp_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gp100_fifo_new },
	.gr       = { 0x00000001, gp108_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.sec2     = { 0x00000001, gp108_sec2_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv13b_chipset = {
	.name = "GP10B",
	.acr      = { 0x00000001, gp10b_acr_new },
	.bar      = { 0x00000001, gm20b_bar_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.fault    = { 0x00000001, gp10b_fault_new },
	.fb       = { 0x00000001, gp10b_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.imem     = { 0x00000001, gk20a_instmem_new },
	.ltc      = { 0x00000001, gp10b_ltc_new },
	.mc       = { 0x00000001, gp10b_mc_new },
	.mmu      = { 0x00000001, gp10b_mmu_new },
	.pmu      = { 0x00000001, gp10b_pmu_new },
	.privring = { 0x00000001, gp10b_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x00000001, gp100_ce_new },
	.dma      = { 0x00000001, gf119_dma_new },
	.fifo     = { 0x00000001, gp10b_fifo_new },
	.gr       = { 0x00000001, gp10b_gr_new },
	.sw       = { 0x00000001, gf100_sw_new },
};

static const struct nvkm_device_chip
nv140_chipset = {
	.name = "GV100",
	.acr      = { 0x00000001, gp108_acr_new },
	.bar      = { 0x00000001, gm107_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, gv100_devinit_new },
	.fault    = { 0x00000001, gv100_fault_new },
	.fb       = { 0x00000001, gv100_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.gsp      = { 0x00000001, gv100_gsp_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, gp100_mc_new },
	.mmu      = { 0x00000001, gv100_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x000001ff, gv100_ce_new },
	.disp     = { 0x00000001, gv100_disp_new },
	.dma      = { 0x00000001, gv100_dma_new },
	.fifo     = { 0x00000001, gv100_fifo_new },
	.gr       = { 0x00000001, gv100_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000007, gm107_nvenc_new },
	.sec2     = { 0x00000001, gp108_sec2_new },
};

static const struct nvkm_device_chip
nv162_chipset = {
	.name = "TU102",
	.acr      = { 0x00000001, tu102_acr_new },
	.bar      = { 0x00000001, tu102_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, tu102_devinit_new },
	.fault    = { 0x00000001, tu102_fault_new },
	.fb       = { 0x00000001, gv100_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.gsp      = { 0x00000001, gv100_gsp_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, tu102_mc_new },
	.mmu      = { 0x00000001, tu102_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000001f, tu102_ce_new },
	.disp     = { 0x00000001, tu102_disp_new },
	.dma      = { 0x00000001, gv100_dma_new },
	.fifo     = { 0x00000001, tu102_fifo_new },
	.gr       = { 0x00000001, tu102_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000001, gm107_nvenc_new },
	.sec2     = { 0x00000001, tu102_sec2_new },
};

static const struct nvkm_device_chip
nv164_chipset = {
	.name = "TU104",
	.acr      = { 0x00000001, tu102_acr_new },
	.bar      = { 0x00000001, tu102_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, tu102_devinit_new },
	.fault    = { 0x00000001, tu102_fault_new },
	.fb       = { 0x00000001, gv100_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.gsp      = { 0x00000001, gv100_gsp_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, tu102_mc_new },
	.mmu      = { 0x00000001, tu102_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000001f, tu102_ce_new },
	.disp     = { 0x00000001, tu102_disp_new },
	.dma      = { 0x00000001, gv100_dma_new },
	.fifo     = { 0x00000001, tu102_fifo_new },
	.gr       = { 0x00000001, tu102_gr_new },
	.nvdec    = { 0x00000003, gm107_nvdec_new },
	.nvenc    = { 0x00000001, gm107_nvenc_new },
	.sec2     = { 0x00000001, tu102_sec2_new },
};

static const struct nvkm_device_chip
nv166_chipset = {
	.name = "TU106",
	.acr      = { 0x00000001, tu102_acr_new },
	.bar      = { 0x00000001, tu102_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, tu102_devinit_new },
	.fault    = { 0x00000001, tu102_fault_new },
	.fb       = { 0x00000001, gv100_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.gsp      = { 0x00000001, gv100_gsp_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, tu102_mc_new },
	.mmu      = { 0x00000001, tu102_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000001f, tu102_ce_new },
	.disp     = { 0x00000001, tu102_disp_new },
	.dma      = { 0x00000001, gv100_dma_new },
	.fifo     = { 0x00000001, tu102_fifo_new },
	.gr       = { 0x00000001, tu102_gr_new },
	.nvdec    = { 0x00000007, gm107_nvdec_new },
	.nvenc    = { 0x00000001, gm107_nvenc_new },
	.sec2     = { 0x00000001, tu102_sec2_new },
};

static const struct nvkm_device_chip
nv167_chipset = {
	.name = "TU117",
	.acr      = { 0x00000001, tu102_acr_new },
	.bar      = { 0x00000001, tu102_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, tu102_devinit_new },
	.fault    = { 0x00000001, tu102_fault_new },
	.fb       = { 0x00000001, gv100_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.gsp      = { 0x00000001, gv100_gsp_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, tu102_mc_new },
	.mmu      = { 0x00000001, tu102_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000001f, tu102_ce_new },
	.disp     = { 0x00000001, tu102_disp_new },
	.dma      = { 0x00000001, gv100_dma_new },
	.fifo     = { 0x00000001, tu102_fifo_new },
	.gr       = { 0x00000001, tu102_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000001, gm107_nvenc_new },
	.sec2     = { 0x00000001, tu102_sec2_new },
};

static const struct nvkm_device_chip
nv168_chipset = {
	.name = "TU116",
	.acr      = { 0x00000001, tu102_acr_new },
	.bar      = { 0x00000001, tu102_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.bus      = { 0x00000001, gf100_bus_new },
	.devinit  = { 0x00000001, tu102_devinit_new },
	.fault    = { 0x00000001, tu102_fault_new },
	.fb       = { 0x00000001, gv100_fb_new },
	.fuse     = { 0x00000001, gm107_fuse_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.gsp      = { 0x00000001, gv100_gsp_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.ltc      = { 0x00000001, gp102_ltc_new },
	.mc       = { 0x00000001, tu102_mc_new },
	.mmu      = { 0x00000001, tu102_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.pmu      = { 0x00000001, gp102_pmu_new },
	.privring = { 0x00000001, gm200_privring_new },
	.therm    = { 0x00000001, gp100_therm_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, gk104_top_new },
	.ce       = { 0x0000001f, tu102_ce_new },
	.disp     = { 0x00000001, tu102_disp_new },
	.dma      = { 0x00000001, gv100_dma_new },
	.fifo     = { 0x00000001, tu102_fifo_new },
	.gr       = { 0x00000001, tu102_gr_new },
	.nvdec    = { 0x00000001, gm107_nvdec_new },
	.nvenc    = { 0x00000001, gm107_nvenc_new },
	.sec2     = { 0x00000001, tu102_sec2_new },
};

static const struct nvkm_device_chip
nv170_chipset = {
	.name = "GA100",
	.bar      = { 0x00000001, tu102_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.devinit  = { 0x00000001, ga100_devinit_new },
	.fb       = { 0x00000001, ga100_fb_new },
	.gpio     = { 0x00000001, gk104_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, ga100_mc_new },
	.mmu      = { 0x00000001, tu102_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, ga100_top_new },
};

static const struct nvkm_device_chip
nv172_chipset = {
	.name = "GA102",
	.bar      = { 0x00000001, tu102_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.devinit  = { 0x00000001, ga100_devinit_new },
	.fb       = { 0x00000001, ga102_fb_new },
	.gpio     = { 0x00000001, ga102_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, ga100_mc_new },
	.mmu      = { 0x00000001, tu102_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, ga100_top_new },
	.disp     = { 0x00000001, ga102_disp_new },
	.dma      = { 0x00000001, gv100_dma_new },
	.fifo     = { 0x00000001, ga102_fifo_new },
};

static const struct nvkm_device_chip
nv174_chipset = {
	.name = "GA104",
	.bar      = { 0x00000001, tu102_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.devinit  = { 0x00000001, ga100_devinit_new },
	.fb       = { 0x00000001, ga102_fb_new },
	.gpio     = { 0x00000001, ga102_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, ga100_mc_new },
	.mmu      = { 0x00000001, tu102_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, ga100_top_new },
	.disp     = { 0x00000001, ga102_disp_new },
	.dma      = { 0x00000001, gv100_dma_new },
	.fifo     = { 0x00000001, ga102_fifo_new },
};

static const struct nvkm_device_chip
nv177_chipset = {
	.name = "GA107",
	.bar      = { 0x00000001, tu102_bar_new },
	.bios     = { 0x00000001, nvkm_bios_new },
	.devinit  = { 0x00000001, ga100_devinit_new },
	.fb       = { 0x00000001, ga102_fb_new },
	.gpio     = { 0x00000001, ga102_gpio_new },
	.i2c      = { 0x00000001, gm200_i2c_new },
	.imem     = { 0x00000001, nv50_instmem_new },
	.mc       = { 0x00000001, ga100_mc_new },
	.mmu      = { 0x00000001, tu102_mmu_new },
	.pci      = { 0x00000001, gp100_pci_new },
	.privring = { 0x00000001, gm200_privring_new },
	.timer    = { 0x00000001, gk20a_timer_new },
	.top      = { 0x00000001, ga100_top_new },
	.disp     = { 0x00000001, ga102_disp_new },
	.dma      = { 0x00000001, gv100_dma_new },
	.fifo     = { 0x00000001, ga102_fifo_new },
};

static int
nvkm_device_event_ctor(struct nvkm_object *object, void *data, u32 size,
		       struct nvkm_notify *notify)
{
	if (!WARN_ON(size != 0)) {
		notify->size  = 0;
		notify->types = 1;
		notify->index = 0;
		return 0;
	}
	return -EINVAL;
}

static const struct nvkm_event_func
nvkm_device_event_func = {
	.ctor = nvkm_device_event_ctor,
};

struct nvkm_subdev *
nvkm_device_subdev(struct nvkm_device *device, int type, int inst)
{
	struct nvkm_subdev *subdev;

	list_for_each_entry(subdev, &device->subdev, head) {
		if (subdev->type == type && subdev->inst == inst)
			return subdev;
	}

	return NULL;
}

struct nvkm_engine *
nvkm_device_engine(struct nvkm_device *device, int type, int inst)
{
	struct nvkm_subdev *subdev = nvkm_device_subdev(device, type, inst);
	if (subdev && subdev->func == &nvkm_engine)
		return container_of(subdev, struct nvkm_engine, subdev);
	return NULL;
}

int
nvkm_device_fini(struct nvkm_device *device, bool suspend)
{
	const char *action = suspend ? "suspend" : "fini";
	struct nvkm_subdev *subdev;
	int ret;
	s64 time;

	nvdev_trace(device, "%s running...\n", action);
	time = ktime_to_us(ktime_get());

	nvkm_acpi_fini(device);

	list_for_each_entry_reverse(subdev, &device->subdev, head) {
		ret = nvkm_subdev_fini(subdev, suspend);
		if (ret && suspend)
			goto fail;
	}

	nvkm_therm_clkgate_fini(device->therm, suspend);

	if (device->func->fini)
		device->func->fini(device, suspend);

	time = ktime_to_us(ktime_get()) - time;
	nvdev_trace(device, "%s completed in %lldus...\n", action, time);
	return 0;

fail:
	list_for_each_entry_from(subdev, &device->subdev, head) {
		int rret = nvkm_subdev_init(subdev);
		if (rret)
			nvkm_fatal(subdev, "failed restart, %d\n", ret);
	}

	nvdev_trace(device, "%s failed with %d\n", action, ret);
	return ret;
}

static int
nvkm_device_preinit(struct nvkm_device *device)
{
	struct nvkm_subdev *subdev;
	int ret;
	s64 time;

	nvdev_trace(device, "preinit running...\n");
	time = ktime_to_us(ktime_get());

	if (device->func->preinit) {
		ret = device->func->preinit(device);
		if (ret)
			goto fail;
	}

	list_for_each_entry(subdev, &device->subdev, head) {
		ret = nvkm_subdev_preinit(subdev);
		if (ret)
			goto fail;
	}

	ret = nvkm_devinit_post(device->devinit);
	if (ret)
		goto fail;

	time = ktime_to_us(ktime_get()) - time;
	nvdev_trace(device, "preinit completed in %lldus\n", time);
	return 0;

fail:
	nvdev_error(device, "preinit failed with %d\n", ret);
	return ret;
}

int
nvkm_device_init(struct nvkm_device *device)
{
	struct nvkm_subdev *subdev;
	int ret;
	s64 time;

	ret = nvkm_device_preinit(device);
	if (ret)
		return ret;

	nvkm_device_fini(device, false);

	nvdev_trace(device, "init running...\n");
	time = ktime_to_us(ktime_get());

	if (device->func->init) {
		ret = device->func->init(device);
		if (ret)
			goto fail;
	}

	list_for_each_entry(subdev, &device->subdev, head) {
		ret = nvkm_subdev_init(subdev);
		if (ret)
			goto fail_subdev;
	}

	nvkm_acpi_init(device);
	nvkm_therm_clkgate_enable(device->therm);

	time = ktime_to_us(ktime_get()) - time;
	nvdev_trace(device, "init completed in %lldus\n", time);
	return 0;

fail_subdev:
	list_for_each_entry_from(subdev, &device->subdev, head)
		nvkm_subdev_fini(subdev, false);
fail:
	nvkm_device_fini(device, false);

	nvdev_error(device, "init failed with %d\n", ret);
	return ret;
}

void
nvkm_device_del(struct nvkm_device **pdevice)
{
	struct nvkm_device *device = *pdevice;
	struct nvkm_subdev *subdev, *subtmp;
	if (device) {
		mutex_lock(&nv_devices_mutex);

		list_for_each_entry_safe_reverse(subdev, subtmp, &device->subdev, head)
			nvkm_subdev_del(&subdev);

		nvkm_event_fini(&device->event);

		if (device->pri)
			iounmap(device->pri);
		list_del(&device->head);

		if (device->func->dtor)
			*pdevice = device->func->dtor(device);
		mutex_unlock(&nv_devices_mutex);

		kfree(*pdevice);
		*pdevice = NULL;
	}
}

/* returns true if the GPU is in the CPU native byte order */
static inline bool
nvkm_device_endianness(struct nvkm_device *device)
{
#ifdef __BIG_ENDIAN
	const bool big_endian = true;
#else
	const bool big_endian = false;
#endif

	/* Read NV_PMC_BOOT_1, and assume non-functional endian switch if it
	 * doesn't contain the expected values.
	 */
	u32 pmc_boot_1 = nvkm_rd32(device, 0x000004);
	if (pmc_boot_1 && pmc_boot_1 != 0x01000001)
		return !big_endian; /* Assume GPU is LE in this case. */

	/* 0 means LE and 0x01000001 means BE GPU. Condition is true when
	 * GPU/CPU endianness don't match.
	 */
	if (big_endian == !pmc_boot_1) {
		nvkm_wr32(device, 0x000004, 0x01000001);
		nvkm_rd32(device, 0x000000);
		if (nvkm_rd32(device, 0x000004) != (big_endian ? 0x01000001 : 0x00000000))
			return !big_endian; /* Assume GPU is LE on any unexpected read-back. */
	}

	/* CPU/GPU endianness should (hopefully) match. */
	return true;
}

int
nvkm_device_ctor(const struct nvkm_device_func *func,
		 const struct nvkm_device_quirk *quirk,
		 struct device *dev, enum nvkm_device_type type, u64 handle,
		 const char *name, const char *cfg, const char *dbg,
		 bool detect, bool mmio, u64 subdev_mask,
		 struct nvkm_device *device)
{
	struct nvkm_subdev *subdev;
	u64 mmio_base, mmio_size;
	u32 boot0, boot1, strap;
	int ret = -EEXIST, j;
	unsigned chipset;

	mutex_lock(&nv_devices_mutex);
	if (nvkm_device_find_locked(handle))
		goto done;

	device->func = func;
	device->quirk = quirk;
	device->dev = dev;
	device->type = type;
	device->handle = handle;
	device->cfgopt = cfg;
	device->dbgopt = dbg;
	device->name = name;
	list_add_tail(&device->head, &nv_devices);
	device->debug = nvkm_dbgopt(device->dbgopt, "device");
	INIT_LIST_HEAD(&device->subdev);

	ret = nvkm_event_init(&nvkm_device_event_func, 1, 1, &device->event);
	if (ret)
		goto done;

	mmio_base = device->func->resource_addr(device, 0);
	mmio_size = device->func->resource_size(device, 0);

	if (detect || mmio) {
		device->pri = ioremap(mmio_base, mmio_size);
		if (device->pri == NULL) {
			nvdev_error(device, "unable to map PRI\n");
			ret = -ENOMEM;
			goto done;
		}
	}

	/* identify the chipset, and determine classes of subdev/engines */
	if (detect) {
		/* switch mmio to cpu's native endianness */
		if (!nvkm_device_endianness(device)) {
			nvdev_error(device,
				    "Couldn't switch GPU to CPUs endianess\n");
			ret = -ENOSYS;
			goto done;
		}

		boot0 = nvkm_rd32(device, 0x000000);

		/* chipset can be overridden for devel/testing purposes */
		chipset = nvkm_longopt(device->cfgopt, "NvChipset", 0);
		if (chipset) {
			u32 override_boot0;

			if (chipset >= 0x10) {
				override_boot0  = ((chipset & 0x1ff) << 20);
				override_boot0 |= 0x000000a1;
			} else {
				if (chipset != 0x04)
					override_boot0 = 0x20104000;
				else
					override_boot0 = 0x20004000;
			}

			nvdev_warn(device, "CHIPSET OVERRIDE: %08x -> %08x\n",
				   boot0, override_boot0);
			boot0 = override_boot0;
		}

		/* determine chipset and derive architecture from it */
		if ((boot0 & 0x1f000000) > 0) {
			device->chipset = (boot0 & 0x1ff00000) >> 20;
			device->chiprev = (boot0 & 0x000000ff);
			switch (device->chipset & 0x1f0) {
			case 0x010: {
				if (0x461 & (1 << (device->chipset & 0xf)))
					device->card_type = NV_10;
				else
					device->card_type = NV_11;
				device->chiprev = 0x00;
				break;
			}
			case 0x020: device->card_type = NV_20; break;
			case 0x030: device->card_type = NV_30; break;
			case 0x040:
			case 0x060: device->card_type = NV_40; break;
			case 0x050:
			case 0x080:
			case 0x090:
			case 0x0a0: device->card_type = NV_50; break;
			case 0x0c0:
			case 0x0d0: device->card_type = NV_C0; break;
			case 0x0e0:
			case 0x0f0:
			case 0x100: device->card_type = NV_E0; break;
			case 0x110:
			case 0x120: device->card_type = GM100; break;
			case 0x130: device->card_type = GP100; break;
			case 0x140: device->card_type = GV100; break;
			case 0x160: device->card_type = TU100; break;
			case 0x170: device->card_type = GA100; break;
			default:
				break;
			}
		} else
		if ((boot0 & 0xff00fff0) == 0x20004000) {
			if (boot0 & 0x00f00000)
				device->chipset = 0x05;
			else
				device->chipset = 0x04;
			device->card_type = NV_04;
		}

		switch (device->chipset) {
		case 0x004: device->chip = &nv4_chipset; break;
		case 0x005: device->chip = &nv5_chipset; break;
		case 0x010: device->chip = &nv10_chipset; break;
		case 0x011: device->chip = &nv11_chipset; break;
		case 0x015: device->chip = &nv15_chipset; break;
		case 0x017: device->chip = &nv17_chipset; break;
		case 0x018: device->chip = &nv18_chipset; break;
		case 0x01a: device->chip = &nv1a_chipset; break;
		case 0x01f: device->chip = &nv1f_chipset; break;
		case 0x020: device->chip = &nv20_chipset; break;
		case 0x025: device->chip = &nv25_chipset; break;
		case 0x028: device->chip = &nv28_chipset; break;
		case 0x02a: device->chip = &nv2a_chipset; break;
		case 0x030: device->chip = &nv30_chipset; break;
		case 0x031: device->chip = &nv31_chipset; break;
		case 0x034: device->chip = &nv34_chipset; break;
		case 0x035: device->chip = &nv35_chipset; break;
		case 0x036: device->chip = &nv36_chipset; break;
		case 0x040: device->chip = &nv40_chipset; break;
		case 0x041: device->chip = &nv41_chipset; break;
		case 0x042: device->chip = &nv42_chipset; break;
		case 0x043: device->chip = &nv43_chipset; break;
		case 0x044: device->chip = &nv44_chipset; break;
		case 0x045: device->chip = &nv45_chipset; break;
		case 0x046: device->chip = &nv46_chipset; break;
		case 0x047: device->chip = &nv47_chipset; break;
		case 0x049: device->chip = &nv49_chipset; break;
		case 0x04a: device->chip = &nv4a_chipset; break;
		case 0x04b: device->chip = &nv4b_chipset; break;
		case 0x04c: device->chip = &nv4c_chipset; break;
		case 0x04e: device->chip = &nv4e_chipset; break;
		case 0x050: device->chip = &nv50_chipset; break;
		case 0x063: device->chip = &nv63_chipset; break;
		case 0x067: device->chip = &nv67_chipset; break;
		case 0x068: device->chip = &nv68_chipset; break;
		case 0x084: device->chip = &nv84_chipset; break;
		case 0x086: device->chip = &nv86_chipset; break;
		case 0x092: device->chip = &nv92_chipset; break;
		case 0x094: device->chip = &nv94_chipset; break;
		case 0x096: device->chip = &nv96_chipset; break;
		case 0x098: device->chip = &nv98_chipset; break;
		case 0x0a0: device->chip = &nva0_chipset; break;
		case 0x0a3: device->chip = &nva3_chipset; break;
		case 0x0a5: device->chip = &nva5_chipset; break;
		case 0x0a8: device->chip = &nva8_chipset; break;
		case 0x0aa: device->chip = &nvaa_chipset; break;
		case 0x0ac: device->chip = &nvac_chipset; break;
		case 0x0af: device->chip = &nvaf_chipset; break;
		case 0x0c0: device->chip = &nvc0_chipset; break;
		case 0x0c1: device->chip = &nvc1_chipset; break;
		case 0x0c3: device->chip = &nvc3_chipset; break;
		case 0x0c4: device->chip = &nvc4_chipset; break;
		case 0x0c8: device->chip = &nvc8_chipset; break;
		case 0x0ce: device->chip = &nvce_chipset; break;
		case 0x0cf: device->chip = &nvcf_chipset; break;
		case 0x0d7: device->chip = &nvd7_chipset; break;
		case 0x0d9: device->chip = &nvd9_chipset; break;
		case 0x0e4: device->chip = &nve4_chipset; break;
		case 0x0e6: device->chip = &nve6_chipset; break;
		case 0x0e7: device->chip = &nve7_chipset; break;
		case 0x0ea: device->chip = &nvea_chipset; break;
		case 0x0f0: device->chip = &nvf0_chipset; break;
		case 0x0f1: device->chip = &nvf1_chipset; break;
		case 0x106: device->chip = &nv106_chipset; break;
		case 0x108: device->chip = &nv108_chipset; break;
		case 0x117: device->chip = &nv117_chipset; break;
		case 0x118: device->chip = &nv118_chipset; break;
		case 0x120: device->chip = &nv120_chipset; break;
		case 0x124: device->chip = &nv124_chipset; break;
		case 0x126: device->chip = &nv126_chipset; break;
		case 0x12b: device->chip = &nv12b_chipset; break;
		case 0x130: device->chip = &nv130_chipset; break;
		case 0x132: device->chip = &nv132_chipset; break;
		case 0x134: device->chip = &nv134_chipset; break;
		case 0x136: device->chip = &nv136_chipset; break;
		case 0x137: device->chip = &nv137_chipset; break;
		case 0x138: device->chip = &nv138_chipset; break;
		case 0x13b: device->chip = &nv13b_chipset; break;
		case 0x140: device->chip = &nv140_chipset; break;
		case 0x162: device->chip = &nv162_chipset; break;
		case 0x164: device->chip = &nv164_chipset; break;
		case 0x166: device->chip = &nv166_chipset; break;
		case 0x167: device->chip = &nv167_chipset; break;
		case 0x168: device->chip = &nv168_chipset; break;
		case 0x172: device->chip = &nv172_chipset; break;
		case 0x174: device->chip = &nv174_chipset; break;
		case 0x177: device->chip = &nv177_chipset; break;
		default:
			if (nvkm_boolopt(device->cfgopt, "NvEnableUnsupportedChipsets", false)) {
				switch (device->chipset) {
				case 0x170: device->chip = &nv170_chipset; break;
				default:
					break;
				}
			}

			if (!device->chip) {
				nvdev_error(device, "unknown chipset (%08x)\n", boot0);
				ret = -ENODEV;
				goto done;
			}
			break;
		}

		nvdev_info(device, "NVIDIA %s (%08x)\n",
			   device->chip->name, boot0);

		/* vGPU detection */
		boot1 = nvkm_rd32(device, 0x0000004);
		if (device->card_type >= TU100 && (boot1 & 0x00030000)) {
			nvdev_info(device, "vGPUs are not supported\n");
			ret = -ENODEV;
			goto done;
		}

		/* read strapping information */
		strap = nvkm_rd32(device, 0x101000);

		/* determine frequency of timing crystal */
		if ( device->card_type <= NV_10 || device->chipset < 0x17 ||
		    (device->chipset >= 0x20 && device->chipset < 0x25))
			strap &= 0x00000040;
		else
			strap &= 0x00400040;

		switch (strap) {
		case 0x00000000: device->crystal = 13500; break;
		case 0x00000040: device->crystal = 14318; break;
		case 0x00400000: device->crystal = 27000; break;
		case 0x00400040: device->crystal = 25000; break;
		}
	} else {
		device->chip = &null_chipset;
	}

	if (!device->name)
		device->name = device->chip->name;

	mutex_init(&device->mutex);

#define NVKM_LAYOUT_ONCE(type,data,ptr)                                                      \
	if (device->chip->ptr.inst && (subdev_mask & (BIT_ULL(type)))) {                     \
		WARN_ON(device->chip->ptr.inst != 0x00000001);                               \
		ret = device->chip->ptr.ctor(device, (type), -1, &device->ptr);              \
		subdev = nvkm_device_subdev(device, (type), 0);                              \
		if (ret) {                                                                   \
			nvkm_subdev_del(&subdev);                                            \
			device->ptr = NULL;                                                  \
			if (ret != -ENODEV) {                                                \
				nvdev_error(device, "%s ctor failed: %d\n",                  \
					    nvkm_subdev_type[(type)], ret);                  \
				goto done;                                                   \
			}                                                                    \
		} else {                                                                     \
			subdev->pself = (void **)&device->ptr;                               \
		}                                                                            \
	}
#define NVKM_LAYOUT_INST(type,data,ptr,cnt)                                                  \
	WARN_ON(device->chip->ptr.inst & ~((1 << ARRAY_SIZE(device->ptr)) - 1));             \
	for (j = 0; device->chip->ptr.inst && j < ARRAY_SIZE(device->ptr); j++) {            \
		if ((device->chip->ptr.inst & BIT(j)) && (subdev_mask & BIT_ULL(type))) {    \
			ret = device->chip->ptr.ctor(device, (type), (j), &device->ptr[j]);  \
			subdev = nvkm_device_subdev(device, (type), (j));                    \
			if (ret) {                                                           \
				nvkm_subdev_del(&subdev);                                    \
				device->ptr[j] = NULL;                                       \
				if (ret != -ENODEV) {                                        \
					nvdev_error(device, "%s%d ctor failed: %d\n",        \
						    nvkm_subdev_type[(type)], (j), ret);     \
					goto done;                                           \
				}                                                            \
			} else {                                                             \
				subdev->pself = (void **)&device->ptr[j];                    \
			}                                                                    \
		}                                                                            \
	}
#include <core/layout.h>
#undef NVKM_LAYOUT_INST
#undef NVKM_LAYOUT_ONCE

	ret = 0;
done:
	if (device->pri && (!mmio || ret)) {
		iounmap(device->pri);
		device->pri = NULL;
	}
	mutex_unlock(&nv_devices_mutex);
	return ret;
}
