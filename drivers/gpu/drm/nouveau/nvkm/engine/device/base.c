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
	.bios = nvkm_bios_new,
};

static const struct nvkm_device_chip
nv4_chipset = {
	.name = "NV04",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv04_devinit_new,
	.fb = nv04_fb_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv04_fifo_new,
	.gr = nv04_gr_new,
	.sw = nv04_sw_new,
};

static const struct nvkm_device_chip
nv5_chipset = {
	.name = "NV05",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv05_devinit_new,
	.fb = nv04_fb_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv04_fifo_new,
	.gr = nv04_gr_new,
	.sw = nv04_sw_new,
};

static const struct nvkm_device_chip
nv10_chipset = {
	.name = "NV10",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv10_devinit_new,
	.fb = nv10_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.gr = nv10_gr_new,
};

static const struct nvkm_device_chip
nv11_chipset = {
	.name = "NV11",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv10_devinit_new,
	.fb = nv10_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv10_fifo_new,
	.gr = nv15_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv15_chipset = {
	.name = "NV15",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv10_devinit_new,
	.fb = nv10_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv10_fifo_new,
	.gr = nv15_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv17_chipset = {
	.name = "NV17",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv10_devinit_new,
	.fb = nv10_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv17_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv18_chipset = {
	.name = "NV18",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv10_devinit_new,
	.fb = nv10_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv17_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv1a_chipset = {
	.name = "nForce",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv1a_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv10_fifo_new,
	.gr = nv15_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv1f_chipset = {
	.name = "nForce2",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv1a_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv17_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv20_chipset = {
	.name = "NV20",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv20_devinit_new,
	.fb = nv20_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv20_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv25_chipset = {
	.name = "NV25",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv20_devinit_new,
	.fb = nv25_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv25_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv28_chipset = {
	.name = "NV28",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv20_devinit_new,
	.fb = nv25_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv25_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv2a_chipset = {
	.name = "NV2A",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv20_devinit_new,
	.fb = nv25_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv2a_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv30_chipset = {
	.name = "NV30",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv20_devinit_new,
	.fb = nv30_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv30_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv31_chipset = {
	.name = "NV31",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv20_devinit_new,
	.fb = nv30_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv30_gr_new,
	.mpeg = nv31_mpeg_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv34_chipset = {
	.name = "NV34",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv10_devinit_new,
	.fb = nv10_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv34_gr_new,
	.mpeg = nv31_mpeg_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv35_chipset = {
	.name = "NV35",
	.bios = nvkm_bios_new,
	.bus = nv04_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv20_devinit_new,
	.fb = nv35_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv35_gr_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv36_chipset = {
	.name = "NV36",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv04_clk_new,
	.devinit = nv20_devinit_new,
	.fb = nv36_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv04_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv04_pci_new,
	.timer = nv04_timer_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv17_fifo_new,
	.gr = nv35_gr_new,
	.mpeg = nv31_mpeg_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv40_chipset = {
	.name = "NV40",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv40_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv40_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv40_gr_new,
	.mpeg = nv40_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv41_chipset = {
	.name = "NV41",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv41_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv41_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv40_gr_new,
	.mpeg = nv40_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv42_chipset = {
	.name = "NV42",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv41_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv41_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv40_gr_new,
	.mpeg = nv40_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv43_chipset = {
	.name = "NV43",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv41_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv41_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv40_gr_new,
	.mpeg = nv40_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv44_chipset = {
	.name = "NV44",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv44_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv44_mc_new,
	.mmu = nv44_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv44_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv45_chipset = {
	.name = "NV45",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv40_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv04_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv40_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv46_chipset = {
	.name = "G72",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv46_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv44_mc_new,
	.mmu = nv44_mmu_new,
	.pci = nv46_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv44_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv47_chipset = {
	.name = "G70",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv47_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv41_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv40_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv49_chipset = {
	.name = "G71",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv49_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv41_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv40_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv4a_chipset = {
	.name = "NV44A",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv44_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv44_mc_new,
	.mmu = nv44_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv44_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv4b_chipset = {
	.name = "G73",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv49_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv04_mc_new,
	.mmu = nv41_mmu_new,
	.pci = nv40_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv40_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv4c_chipset = {
	.name = "C61",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv46_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv44_mc_new,
	.mmu = nv44_mmu_new,
	.pci = nv4c_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv44_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv4e_chipset = {
	.name = "C51",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv4e_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv4e_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv44_mc_new,
	.mmu = nv44_mmu_new,
	.pci = nv4c_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv44_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv50_chipset = {
	.name = "G80",
	.bar = nv50_bar_new,
	.bios = nvkm_bios_new,
	.bus = nv50_bus_new,
	.clk = nv50_clk_new,
	.devinit = nv50_devinit_new,
	.fb = nv50_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = nv50_gpio_new,
	.i2c = nv50_i2c_new,
	.imem = nv50_instmem_new,
	.mc = nv50_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = nv46_pci_new,
	.therm = nv50_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv50_disp_new,
	.dma = nv50_dma_new,
	.fifo = nv50_fifo_new,
	.gr = nv50_gr_new,
	.mpeg = nv50_mpeg_new,
	.pm = nv50_pm_new,
	.sw = nv50_sw_new,
};

static const struct nvkm_device_chip
nv63_chipset = {
	.name = "C73",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv46_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv44_mc_new,
	.mmu = nv44_mmu_new,
	.pci = nv4c_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv44_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv67_chipset = {
	.name = "C67",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv46_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv44_mc_new,
	.mmu = nv44_mmu_new,
	.pci = nv4c_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv44_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv68_chipset = {
	.name = "C68",
	.bios = nvkm_bios_new,
	.bus = nv31_bus_new,
	.clk = nv40_clk_new,
	.devinit = nv1a_devinit_new,
	.fb = nv46_fb_new,
	.gpio = nv10_gpio_new,
	.i2c = nv04_i2c_new,
	.imem = nv40_instmem_new,
	.mc = nv44_mc_new,
	.mmu = nv44_mmu_new,
	.pci = nv4c_pci_new,
	.therm = nv40_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = nv04_disp_new,
	.dma = nv04_dma_new,
	.fifo = nv40_fifo_new,
	.gr = nv44_gr_new,
	.mpeg = nv44_mpeg_new,
	.pm = nv40_pm_new,
	.sw = nv10_sw_new,
};

static const struct nvkm_device_chip
nv84_chipset = {
	.name = "G84",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = nv50_bus_new,
	.clk = g84_clk_new,
	.devinit = g84_devinit_new,
	.fb = g84_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = nv50_gpio_new,
	.i2c = nv50_i2c_new,
	.imem = nv50_instmem_new,
	.mc = g84_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g84_pci_new,
	.therm = g84_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.bsp = g84_bsp_new,
	.cipher = g84_cipher_new,
	.disp = g84_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = g84_gr_new,
	.mpeg = g84_mpeg_new,
	.pm = g84_pm_new,
	.sw = nv50_sw_new,
	.vp = g84_vp_new,
};

static const struct nvkm_device_chip
nv86_chipset = {
	.name = "G86",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = nv50_bus_new,
	.clk = g84_clk_new,
	.devinit = g84_devinit_new,
	.fb = g84_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = nv50_gpio_new,
	.i2c = nv50_i2c_new,
	.imem = nv50_instmem_new,
	.mc = g84_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g84_pci_new,
	.therm = g84_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.bsp = g84_bsp_new,
	.cipher = g84_cipher_new,
	.disp = g84_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = g84_gr_new,
	.mpeg = g84_mpeg_new,
	.pm = g84_pm_new,
	.sw = nv50_sw_new,
	.vp = g84_vp_new,
};

static const struct nvkm_device_chip
nv92_chipset = {
	.name = "G92",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = nv50_bus_new,
	.clk = g84_clk_new,
	.devinit = g84_devinit_new,
	.fb = g84_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = nv50_gpio_new,
	.i2c = nv50_i2c_new,
	.imem = nv50_instmem_new,
	.mc = g84_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g84_pci_new,
	.therm = g84_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.bsp = g84_bsp_new,
	.cipher = g84_cipher_new,
	.disp = g84_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = g84_gr_new,
	.mpeg = g84_mpeg_new,
	.pm = g84_pm_new,
	.sw = nv50_sw_new,
	.vp = g84_vp_new,
};

static const struct nvkm_device_chip
nv94_chipset = {
	.name = "G94",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = g84_clk_new,
	.devinit = g84_devinit_new,
	.fb = g84_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.imem = nv50_instmem_new,
	.mc = g84_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.therm = g84_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.bsp = g84_bsp_new,
	.cipher = g84_cipher_new,
	.disp = g94_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = g84_gr_new,
	.mpeg = g84_mpeg_new,
	.pm = g84_pm_new,
	.sw = nv50_sw_new,
	.vp = g84_vp_new,
};

static const struct nvkm_device_chip
nv96_chipset = {
	.name = "G96",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = g84_clk_new,
	.devinit = g84_devinit_new,
	.fb = g84_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.imem = nv50_instmem_new,
	.mc = g84_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.therm = g84_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.bsp = g84_bsp_new,
	.cipher = g84_cipher_new,
	.disp = g94_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = g84_gr_new,
	.mpeg = g84_mpeg_new,
	.pm = g84_pm_new,
	.sw = nv50_sw_new,
	.vp = g84_vp_new,
};

static const struct nvkm_device_chip
nv98_chipset = {
	.name = "G98",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = g84_clk_new,
	.devinit = g98_devinit_new,
	.fb = g84_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.imem = nv50_instmem_new,
	.mc = g98_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.therm = g84_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = g94_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = g84_gr_new,
	.mspdec = g98_mspdec_new,
	.msppp = g98_msppp_new,
	.msvld = g98_msvld_new,
	.pm = g84_pm_new,
	.sec = g98_sec_new,
	.sw = nv50_sw_new,
};

static const struct nvkm_device_chip
nva0_chipset = {
	.name = "GT200",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = g84_clk_new,
	.devinit = g84_devinit_new,
	.fb = g84_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = nv50_i2c_new,
	.imem = nv50_instmem_new,
	.mc = g84_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.therm = g84_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.bsp = g84_bsp_new,
	.cipher = g84_cipher_new,
	.disp = gt200_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = gt200_gr_new,
	.mpeg = g84_mpeg_new,
	.pm = gt200_pm_new,
	.sw = nv50_sw_new,
	.vp = g84_vp_new,
};

static const struct nvkm_device_chip
nva3_chipset = {
	.name = "GT215",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = gt215_clk_new,
	.devinit = gt215_devinit_new,
	.fb = gt215_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.imem = nv50_instmem_new,
	.mc = gt215_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.pmu = gt215_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gt215_ce_new,
	.disp = gt215_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = gt215_gr_new,
	.mpeg = g84_mpeg_new,
	.mspdec = gt215_mspdec_new,
	.msppp = gt215_msppp_new,
	.msvld = gt215_msvld_new,
	.pm = gt215_pm_new,
	.sw = nv50_sw_new,
};

static const struct nvkm_device_chip
nva5_chipset = {
	.name = "GT216",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = gt215_clk_new,
	.devinit = gt215_devinit_new,
	.fb = gt215_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.imem = nv50_instmem_new,
	.mc = gt215_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.pmu = gt215_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gt215_ce_new,
	.disp = gt215_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = gt215_gr_new,
	.mspdec = gt215_mspdec_new,
	.msppp = gt215_msppp_new,
	.msvld = gt215_msvld_new,
	.pm = gt215_pm_new,
	.sw = nv50_sw_new,
};

static const struct nvkm_device_chip
nva8_chipset = {
	.name = "GT218",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = gt215_clk_new,
	.devinit = gt215_devinit_new,
	.fb = gt215_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.imem = nv50_instmem_new,
	.mc = gt215_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.pmu = gt215_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gt215_ce_new,
	.disp = gt215_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = gt215_gr_new,
	.mspdec = gt215_mspdec_new,
	.msppp = gt215_msppp_new,
	.msvld = gt215_msvld_new,
	.pm = gt215_pm_new,
	.sw = nv50_sw_new,
};

static const struct nvkm_device_chip
nvaa_chipset = {
	.name = "MCP77/MCP78",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = mcp77_clk_new,
	.devinit = g98_devinit_new,
	.fb = mcp77_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.imem = nv50_instmem_new,
	.mc = g98_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.therm = g84_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = g94_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = gt200_gr_new,
	.mspdec = g98_mspdec_new,
	.msppp = g98_msppp_new,
	.msvld = g98_msvld_new,
	.pm = g84_pm_new,
	.sec = g98_sec_new,
	.sw = nv50_sw_new,
};

static const struct nvkm_device_chip
nvac_chipset = {
	.name = "MCP79/MCP7A",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = mcp77_clk_new,
	.devinit = g98_devinit_new,
	.fb = mcp77_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.imem = nv50_instmem_new,
	.mc = g98_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.therm = g84_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.disp = g94_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = mcp79_gr_new,
	.mspdec = g98_mspdec_new,
	.msppp = g98_msppp_new,
	.msvld = g98_msvld_new,
	.pm = g84_pm_new,
	.sec = g98_sec_new,
	.sw = nv50_sw_new,
};

static const struct nvkm_device_chip
nvaf_chipset = {
	.name = "MCP89",
	.bar = g84_bar_new,
	.bios = nvkm_bios_new,
	.bus = g94_bus_new,
	.clk = gt215_clk_new,
	.devinit = mcp89_devinit_new,
	.fb = mcp89_fb_new,
	.fuse = nv50_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.imem = nv50_instmem_new,
	.mc = gt215_mc_new,
	.mmu = nv50_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = g94_pci_new,
	.pmu = gt215_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gt215_ce_new,
	.disp = gt215_disp_new,
	.dma = nv50_dma_new,
	.fifo = g84_fifo_new,
	.gr = mcp89_gr_new,
	.mspdec = gt215_mspdec_new,
	.msppp = gt215_msppp_new,
	.msvld = mcp89_msvld_new,
	.pm = gt215_pm_new,
	.sw = nv50_sw_new,
};

static const struct nvkm_device_chip
nvc0_chipset = {
	.name = "GF100",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gf100_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gf100_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.ibus = gf100_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gf100_ltc_new,
	.mc = gf100_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gf100_pci_new,
	.pmu = gf100_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gf100_ce_new,
	.ce[1] = gf100_ce_new,
	.disp = gt215_disp_new,
	.dma = gf100_dma_new,
	.fifo = gf100_fifo_new,
	.gr = gf100_gr_new,
	.mspdec = gf100_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gf100_msvld_new,
	.pm = gf100_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvc1_chipset = {
	.name = "GF108",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gf100_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gf100_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.ibus = gf100_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gf100_ltc_new,
	.mc = gf100_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gf106_pci_new,
	.pmu = gf100_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gf100_ce_new,
	.disp = gt215_disp_new,
	.dma = gf100_dma_new,
	.fifo = gf100_fifo_new,
	.gr = gf108_gr_new,
	.mspdec = gf100_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gf100_msvld_new,
	.pm = gf108_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvc3_chipset = {
	.name = "GF106",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gf100_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gf100_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.ibus = gf100_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gf100_ltc_new,
	.mc = gf100_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gf106_pci_new,
	.pmu = gf100_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gf100_ce_new,
	.disp = gt215_disp_new,
	.dma = gf100_dma_new,
	.fifo = gf100_fifo_new,
	.gr = gf104_gr_new,
	.mspdec = gf100_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gf100_msvld_new,
	.pm = gf100_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvc4_chipset = {
	.name = "GF104",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gf100_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gf100_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.ibus = gf100_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gf100_ltc_new,
	.mc = gf100_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gf100_pci_new,
	.pmu = gf100_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gf100_ce_new,
	.ce[1] = gf100_ce_new,
	.disp = gt215_disp_new,
	.dma = gf100_dma_new,
	.fifo = gf100_fifo_new,
	.gr = gf104_gr_new,
	.mspdec = gf100_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gf100_msvld_new,
	.pm = gf100_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvc8_chipset = {
	.name = "GF110",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gf100_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gf100_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.ibus = gf100_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gf100_ltc_new,
	.mc = gf100_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gf100_pci_new,
	.pmu = gf100_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gf100_ce_new,
	.ce[1] = gf100_ce_new,
	.disp = gt215_disp_new,
	.dma = gf100_dma_new,
	.fifo = gf100_fifo_new,
	.gr = gf110_gr_new,
	.mspdec = gf100_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gf100_msvld_new,
	.pm = gf100_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvce_chipset = {
	.name = "GF114",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gf100_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gf100_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.ibus = gf100_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gf100_ltc_new,
	.mc = gf100_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gf100_pci_new,
	.pmu = gf100_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gf100_ce_new,
	.ce[1] = gf100_ce_new,
	.disp = gt215_disp_new,
	.dma = gf100_dma_new,
	.fifo = gf100_fifo_new,
	.gr = gf104_gr_new,
	.mspdec = gf100_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gf100_msvld_new,
	.pm = gf100_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvcf_chipset = {
	.name = "GF116",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gf100_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gf100_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = g94_gpio_new,
	.i2c = g94_i2c_new,
	.ibus = gf100_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gf100_ltc_new,
	.mc = gf100_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gf106_pci_new,
	.pmu = gf100_pmu_new,
	.therm = gt215_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gf100_ce_new,
	.disp = gt215_disp_new,
	.dma = gf100_dma_new,
	.fifo = gf100_fifo_new,
	.gr = gf104_gr_new,
	.mspdec = gf100_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gf100_msvld_new,
	.pm = gf100_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvd7_chipset = {
	.name = "GF117",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gf100_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gf100_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = gf119_gpio_new,
	.i2c = gf117_i2c_new,
	.ibus = gf117_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gf100_ltc_new,
	.mc = gf100_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gf106_pci_new,
	.therm = gf119_therm_new,
	.timer = nv41_timer_new,
	.ce[0] = gf100_ce_new,
	.disp = gf119_disp_new,
	.dma = gf119_dma_new,
	.fifo = gf100_fifo_new,
	.gr = gf117_gr_new,
	.mspdec = gf100_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gf100_msvld_new,
	.pm = gf117_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvd9_chipset = {
	.name = "GF119",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gf100_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gf100_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = gf119_gpio_new,
	.i2c = gf119_i2c_new,
	.ibus = gf117_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gf100_ltc_new,
	.mc = gf100_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gf106_pci_new,
	.pmu = gf119_pmu_new,
	.therm = gf119_therm_new,
	.timer = nv41_timer_new,
	.volt = nv40_volt_new,
	.ce[0] = gf100_ce_new,
	.disp = gf119_disp_new,
	.dma = gf119_dma_new,
	.fifo = gf100_fifo_new,
	.gr = gf119_gr_new,
	.mspdec = gf100_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gf100_msvld_new,
	.pm = gf117_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nve4_chipset = {
	.name = "GK104",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gk104_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gk104_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gk104_i2c_new,
	.ibus = gk104_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gk104_ltc_new,
	.mc = gk104_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gk104_pmu_new,
	.therm = gf119_therm_new,
	.timer = nv41_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gk104_ce_new,
	.ce[1] = gk104_ce_new,
	.ce[2] = gk104_ce_new,
	.disp = gk104_disp_new,
	.dma = gf119_dma_new,
	.fifo = gk104_fifo_new,
	.gr = gk104_gr_new,
	.mspdec = gk104_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gk104_msvld_new,
	.pm = gk104_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nve6_chipset = {
	.name = "GK106",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gk104_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gk104_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gk104_i2c_new,
	.ibus = gk104_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gk104_ltc_new,
	.mc = gk104_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gk104_pmu_new,
	.therm = gf119_therm_new,
	.timer = nv41_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gk104_ce_new,
	.ce[1] = gk104_ce_new,
	.ce[2] = gk104_ce_new,
	.disp = gk104_disp_new,
	.dma = gf119_dma_new,
	.fifo = gk104_fifo_new,
	.gr = gk104_gr_new,
	.mspdec = gk104_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gk104_msvld_new,
	.pm = gk104_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nve7_chipset = {
	.name = "GK107",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gk104_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gk104_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gk104_i2c_new,
	.ibus = gk104_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gk104_ltc_new,
	.mc = gk104_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gk104_pmu_new,
	.therm = gf119_therm_new,
	.timer = nv41_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gk104_ce_new,
	.ce[1] = gk104_ce_new,
	.ce[2] = gk104_ce_new,
	.disp = gk104_disp_new,
	.dma = gf119_dma_new,
	.fifo = gk104_fifo_new,
	.gr = gk104_gr_new,
	.mspdec = gk104_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gk104_msvld_new,
	.pm = gk104_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvea_chipset = {
	.name = "GK20A",
	.bar = gk20a_bar_new,
	.bus = gf100_bus_new,
	.clk = gk20a_clk_new,
	.fb = gk20a_fb_new,
	.fuse = gf100_fuse_new,
	.ibus = gk20a_ibus_new,
	.imem = gk20a_instmem_new,
	.ltc = gk104_ltc_new,
	.mc = gk20a_mc_new,
	.mmu = gf100_mmu_new,
	.pmu = gk20a_pmu_new,
	.timer = gk20a_timer_new,
	.top = gk104_top_new,
	.volt = gk20a_volt_new,
	.ce[2] = gk104_ce_new,
	.dma = gf119_dma_new,
	.fifo = gk20a_fifo_new,
	.gr = gk20a_gr_new,
	.pm = gk104_pm_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvf0_chipset = {
	.name = "GK110",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gk104_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gk104_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gk104_i2c_new,
	.ibus = gk104_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gk104_ltc_new,
	.mc = gk104_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gk110_pmu_new,
	.therm = gf119_therm_new,
	.timer = nv41_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gk104_ce_new,
	.ce[1] = gk104_ce_new,
	.ce[2] = gk104_ce_new,
	.disp = gk110_disp_new,
	.dma = gf119_dma_new,
	.fifo = gk110_fifo_new,
	.gr = gk110_gr_new,
	.mspdec = gk104_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gk104_msvld_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nvf1_chipset = {
	.name = "GK110B",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gk104_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gk104_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gf119_i2c_new,
	.ibus = gk104_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gk104_ltc_new,
	.mc = gk104_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gk110_pmu_new,
	.therm = gf119_therm_new,
	.timer = nv41_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gk104_ce_new,
	.ce[1] = gk104_ce_new,
	.ce[2] = gk104_ce_new,
	.disp = gk110_disp_new,
	.dma = gf119_dma_new,
	.fifo = gk110_fifo_new,
	.gr = gk110b_gr_new,
	.mspdec = gk104_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gk104_msvld_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nv106_chipset = {
	.name = "GK208B",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gk104_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gk104_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gk104_i2c_new,
	.ibus = gk104_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gk104_ltc_new,
	.mc = gk20a_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gk208_pmu_new,
	.therm = gf119_therm_new,
	.timer = nv41_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gk104_ce_new,
	.ce[1] = gk104_ce_new,
	.ce[2] = gk104_ce_new,
	.disp = gk110_disp_new,
	.dma = gf119_dma_new,
	.fifo = gk208_fifo_new,
	.gr = gk208_gr_new,
	.mspdec = gk104_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gk104_msvld_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nv108_chipset = {
	.name = "GK208",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gk104_clk_new,
	.devinit = gf100_devinit_new,
	.fb = gk104_fb_new,
	.fuse = gf100_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gk104_i2c_new,
	.ibus = gk104_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gk104_ltc_new,
	.mc = gk20a_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gk208_pmu_new,
	.therm = gf119_therm_new,
	.timer = nv41_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gk104_ce_new,
	.ce[1] = gk104_ce_new,
	.ce[2] = gk104_ce_new,
	.disp = gk110_disp_new,
	.dma = gf119_dma_new,
	.fifo = gk208_fifo_new,
	.gr = gk208_gr_new,
	.mspdec = gk104_mspdec_new,
	.msppp = gf100_msppp_new,
	.msvld = gk104_msvld_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nv117_chipset = {
	.name = "GM107",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.clk = gk104_clk_new,
	.devinit = gm107_devinit_new,
	.fb = gm107_fb_new,
	.fuse = gm107_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gf119_i2c_new,
	.ibus = gk104_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gm107_ltc_new,
	.mc = gk20a_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gm107_pmu_new,
	.therm = gm107_therm_new,
	.timer = gk20a_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gm107_ce_new,
	.ce[2] = gm107_ce_new,
	.disp = gm107_disp_new,
	.dma = gf119_dma_new,
	.fifo = gm107_fifo_new,
	.gr = gm107_gr_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nv120_chipset = {
	.name = "GM200",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.devinit = gm200_devinit_new,
	.fb = gm107_fb_new,
	.fuse = gm107_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gm200_i2c_new,
	.ibus = gm200_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gm200_ltc_new,
	.mc = gk20a_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gm107_pmu_new,
	.secboot = gm200_secboot_new,
	.timer = gk20a_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gm200_ce_new,
	.ce[1] = gm200_ce_new,
	.ce[2] = gm200_ce_new,
	.disp = gm200_disp_new,
	.dma = gf119_dma_new,
	.fifo = gm200_fifo_new,
	.gr = gm200_gr_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nv124_chipset = {
	.name = "GM204",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.devinit = gm200_devinit_new,
	.fb = gm107_fb_new,
	.fuse = gm107_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gm200_i2c_new,
	.ibus = gm200_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gm200_ltc_new,
	.mc = gk20a_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gm107_pmu_new,
	.secboot = gm200_secboot_new,
	.timer = gk20a_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gm200_ce_new,
	.ce[1] = gm200_ce_new,
	.ce[2] = gm200_ce_new,
	.disp = gm200_disp_new,
	.dma = gf119_dma_new,
	.fifo = gm200_fifo_new,
	.gr = gm200_gr_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nv126_chipset = {
	.name = "GM206",
	.bar = gf100_bar_new,
	.bios = nvkm_bios_new,
	.bus = gf100_bus_new,
	.devinit = gm200_devinit_new,
	.fb = gm107_fb_new,
	.fuse = gm107_fuse_new,
	.gpio = gk104_gpio_new,
	.i2c = gm200_i2c_new,
	.ibus = gm200_ibus_new,
	.iccsense = gf100_iccsense_new,
	.imem = nv50_instmem_new,
	.ltc = gm200_ltc_new,
	.mc = gk20a_mc_new,
	.mmu = gf100_mmu_new,
	.mxm = nv50_mxm_new,
	.pci = gk104_pci_new,
	.pmu = gm107_pmu_new,
	.secboot = gm200_secboot_new,
	.timer = gk20a_timer_new,
	.top = gk104_top_new,
	.volt = gk104_volt_new,
	.ce[0] = gm200_ce_new,
	.ce[1] = gm200_ce_new,
	.ce[2] = gm200_ce_new,
	.disp = gm200_disp_new,
	.dma = gf119_dma_new,
	.fifo = gm200_fifo_new,
	.gr = gm200_gr_new,
	.sw = gf100_sw_new,
};

static const struct nvkm_device_chip
nv12b_chipset = {
	.name = "GM20B",
	.bar = gk20a_bar_new,
	.bus = gf100_bus_new,
	.clk = gm20b_clk_new,
	.fb = gk20a_fb_new,
	.fuse = gm107_fuse_new,
	.ibus = gk20a_ibus_new,
	.imem = gk20a_instmem_new,
	.ltc = gm200_ltc_new,
	.mc = gk20a_mc_new,
	.mmu = gf100_mmu_new,
	.secboot = gm20b_secboot_new,
	.timer = gk20a_timer_new,
	.top = gk104_top_new,
	.ce[2] = gm200_ce_new,
	.volt = gm20b_volt_new,
	.dma = gf119_dma_new,
	.fifo = gm20b_fifo_new,
	.gr = gm20b_gr_new,
	.sw = gf100_sw_new,
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
nvkm_device_subdev(struct nvkm_device *device, int index)
{
	struct nvkm_engine *engine;

	if (device->disable_mask & (1ULL << index))
		return NULL;

	switch (index) {
#define _(n,p,m) case NVKM_SUBDEV_##n: if (p) return (m); break
	_(BAR     , device->bar     , &device->bar->subdev);
	_(VBIOS   , device->bios    , &device->bios->subdev);
	_(BUS     , device->bus     , &device->bus->subdev);
	_(CLK     , device->clk     , &device->clk->subdev);
	_(DEVINIT , device->devinit , &device->devinit->subdev);
	_(FB      , device->fb      , &device->fb->subdev);
	_(FUSE    , device->fuse    , &device->fuse->subdev);
	_(GPIO    , device->gpio    , &device->gpio->subdev);
	_(I2C     , device->i2c     , &device->i2c->subdev);
	_(IBUS    , device->ibus    ,  device->ibus);
	_(ICCSENSE, device->iccsense, &device->iccsense->subdev);
	_(INSTMEM , device->imem    , &device->imem->subdev);
	_(LTC     , device->ltc     , &device->ltc->subdev);
	_(MC      , device->mc      , &device->mc->subdev);
	_(MMU     , device->mmu     , &device->mmu->subdev);
	_(MXM     , device->mxm     ,  device->mxm);
	_(PCI     , device->pci     , &device->pci->subdev);
	_(PMU     , device->pmu     , &device->pmu->subdev);
	_(SECBOOT , device->secboot , &device->secboot->subdev);
	_(THERM   , device->therm   , &device->therm->subdev);
	_(TIMER   , device->timer   , &device->timer->subdev);
	_(TOP     , device->top     , &device->top->subdev);
	_(VOLT    , device->volt    , &device->volt->subdev);
#undef _
	default:
		engine = nvkm_device_engine(device, index);
		if (engine)
			return &engine->subdev;
		break;
	}
	return NULL;
}

struct nvkm_engine *
nvkm_device_engine(struct nvkm_device *device, int index)
{
	if (device->disable_mask & (1ULL << index))
		return NULL;

	switch (index) {
#define _(n,p,m) case NVKM_ENGINE_##n: if (p) return (m); break
	_(BSP    , device->bsp     ,  device->bsp);
	_(CE0    , device->ce[0]   ,  device->ce[0]);
	_(CE1    , device->ce[1]   ,  device->ce[1]);
	_(CE2    , device->ce[2]   ,  device->ce[2]);
	_(CIPHER , device->cipher  ,  device->cipher);
	_(DISP   , device->disp    , &device->disp->engine);
	_(DMAOBJ , device->dma     , &device->dma->engine);
	_(FIFO   , device->fifo    , &device->fifo->engine);
	_(GR     , device->gr      , &device->gr->engine);
	_(IFB    , device->ifb     ,  device->ifb);
	_(ME     , device->me      ,  device->me);
	_(MPEG   , device->mpeg    ,  device->mpeg);
	_(MSENC  , device->msenc   ,  device->msenc);
	_(MSPDEC , device->mspdec  ,  device->mspdec);
	_(MSPPP  , device->msppp   ,  device->msppp);
	_(MSVLD  , device->msvld   ,  device->msvld);
	_(NVENC0 , device->nvenc[0],  device->nvenc[0]);
	_(NVENC1 , device->nvenc[1],  device->nvenc[1]);
	_(NVDEC  , device->nvdec   ,  device->nvdec);
	_(PM     , device->pm      , &device->pm->engine);
	_(SEC    , device->sec     ,  device->sec);
	_(SW     , device->sw      , &device->sw->engine);
	_(VIC    , device->vic     ,  device->vic);
	_(VP     , device->vp      ,  device->vp);
#undef _
	default:
		WARN_ON(1);
		break;
	}
	return NULL;
}

int
nvkm_device_fini(struct nvkm_device *device, bool suspend)
{
	const char *action = suspend ? "suspend" : "fini";
	struct nvkm_subdev *subdev;
	int ret, i;
	s64 time;

	nvdev_trace(device, "%s running...\n", action);
	time = ktime_to_us(ktime_get());

	nvkm_acpi_fini(device);

	for (i = NVKM_SUBDEV_NR - 1; i >= 0; i--) {
		if ((subdev = nvkm_device_subdev(device, i))) {
			ret = nvkm_subdev_fini(subdev, suspend);
			if (ret && suspend)
				goto fail;
		}
	}


	if (device->func->fini)
		device->func->fini(device, suspend);

	time = ktime_to_us(ktime_get()) - time;
	nvdev_trace(device, "%s completed in %lldus...\n", action, time);
	return 0;

fail:
	do {
		if ((subdev = nvkm_device_subdev(device, i))) {
			int rret = nvkm_subdev_init(subdev);
			if (rret)
				nvkm_fatal(subdev, "failed restart, %d\n", ret);
		}
	} while (++i < NVKM_SUBDEV_NR);

	nvdev_trace(device, "%s failed with %d\n", action, ret);
	return ret;
}

static int
nvkm_device_preinit(struct nvkm_device *device)
{
	struct nvkm_subdev *subdev;
	int ret, i;
	s64 time;

	nvdev_trace(device, "preinit running...\n");
	time = ktime_to_us(ktime_get());

	if (device->func->preinit) {
		ret = device->func->preinit(device);
		if (ret)
			goto fail;
	}

	for (i = 0; i < NVKM_SUBDEV_NR; i++) {
		if ((subdev = nvkm_device_subdev(device, i))) {
			ret = nvkm_subdev_preinit(subdev);
			if (ret)
				goto fail;
		}
	}

	ret = nvkm_devinit_post(device->devinit, &device->disable_mask);
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
	int ret, i;
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

	for (i = 0; i < NVKM_SUBDEV_NR; i++) {
		if ((subdev = nvkm_device_subdev(device, i))) {
			ret = nvkm_subdev_init(subdev);
			if (ret)
				goto fail_subdev;
		}
	}

	nvkm_acpi_init(device);

	time = ktime_to_us(ktime_get()) - time;
	nvdev_trace(device, "init completed in %lldus\n", time);
	return 0;

fail_subdev:
	do {
		if ((subdev = nvkm_device_subdev(device, i)))
			nvkm_subdev_fini(subdev, false);
	} while (--i >= 0);

fail:
	nvkm_device_fini(device, false);

	nvdev_error(device, "init failed with %d\n", ret);
	return ret;
}

void
nvkm_device_del(struct nvkm_device **pdevice)
{
	struct nvkm_device *device = *pdevice;
	int i;
	if (device) {
		mutex_lock(&nv_devices_mutex);
		device->disable_mask = 0;
		for (i = NVKM_SUBDEV_NR - 1; i >= 0; i--) {
			struct nvkm_subdev *subdev =
				nvkm_device_subdev(device, i);
			nvkm_subdev_del(&subdev);
		}

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
	u32 boot0, strap;
	void __iomem *map;
	int ret = -EEXIST;
	int i;

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

	ret = nvkm_event_init(&nvkm_device_event_func, 1, 1, &device->event);
	if (ret)
		goto done;

	mmio_base = device->func->resource_addr(device, 0);
	mmio_size = device->func->resource_size(device, 0);

	/* identify the chipset, and determine classes of subdev/engines */
	if (detect) {
		map = ioremap(mmio_base, 0x102000);
		if (ret = -ENOMEM, map == NULL)
			goto done;

		/* switch mmio to cpu's native endianness */
#ifndef __BIG_ENDIAN
		if (ioread32_native(map + 0x000004) != 0x00000000) {
#else
		if (ioread32_native(map + 0x000004) == 0x00000000) {
#endif
			iowrite32_native(0x01000001, map + 0x000004);
			ioread32_native(map);
		}

		/* read boot0 and strapping information */
		boot0 = ioread32_native(map + 0x000000);
		strap = ioread32_native(map + 0x101000);
		iounmap(map);

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
		case 0x120: device->chip = &nv120_chipset; break;
		case 0x124: device->chip = &nv124_chipset; break;
		case 0x126: device->chip = &nv126_chipset; break;
		case 0x12b: device->chip = &nv12b_chipset; break;
		default:
			nvdev_error(device, "unknown chipset (%08x)\n", boot0);
			goto done;
		}

		nvdev_info(device, "NVIDIA %s (%08x)\n",
			   device->chip->name, boot0);

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

	if (mmio) {
		device->pri = ioremap(mmio_base, mmio_size);
		if (!device->pri) {
			nvdev_error(device, "unable to map PRI\n");
			ret = -ENOMEM;
			goto done;
		}
	}

	mutex_init(&device->mutex);

	for (i = 0; i < NVKM_SUBDEV_NR; i++) {
#define _(s,m) case s:                                                         \
	if (device->chip->m && (subdev_mask & (1ULL << (s)))) {                \
		ret = device->chip->m(device, (s), &device->m);                \
		if (ret) {                                                     \
			subdev = nvkm_device_subdev(device, (s));              \
			nvkm_subdev_del(&subdev);                              \
			device->m = NULL;                                      \
			if (ret != -ENODEV) {                                  \
				nvdev_error(device, "%s ctor failed, %d\n",    \
					    nvkm_subdev_name[s], ret);         \
				goto done;                                     \
			}                                                      \
		}                                                              \
	}                                                                      \
	break
		switch (i) {
		_(NVKM_SUBDEV_BAR     ,      bar);
		_(NVKM_SUBDEV_VBIOS   ,     bios);
		_(NVKM_SUBDEV_BUS     ,      bus);
		_(NVKM_SUBDEV_CLK     ,      clk);
		_(NVKM_SUBDEV_DEVINIT ,  devinit);
		_(NVKM_SUBDEV_FB      ,       fb);
		_(NVKM_SUBDEV_FUSE    ,     fuse);
		_(NVKM_SUBDEV_GPIO    ,     gpio);
		_(NVKM_SUBDEV_I2C     ,      i2c);
		_(NVKM_SUBDEV_IBUS    ,     ibus);
		_(NVKM_SUBDEV_ICCSENSE, iccsense);
		_(NVKM_SUBDEV_INSTMEM ,     imem);
		_(NVKM_SUBDEV_LTC     ,      ltc);
		_(NVKM_SUBDEV_MC      ,       mc);
		_(NVKM_SUBDEV_MMU     ,      mmu);
		_(NVKM_SUBDEV_MXM     ,      mxm);
		_(NVKM_SUBDEV_PCI     ,      pci);
		_(NVKM_SUBDEV_PMU     ,      pmu);
		_(NVKM_SUBDEV_SECBOOT ,  secboot);
		_(NVKM_SUBDEV_THERM   ,    therm);
		_(NVKM_SUBDEV_TIMER   ,    timer);
		_(NVKM_SUBDEV_TOP     ,      top);
		_(NVKM_SUBDEV_VOLT    ,     volt);
		_(NVKM_ENGINE_BSP     ,      bsp);
		_(NVKM_ENGINE_CE0     ,    ce[0]);
		_(NVKM_ENGINE_CE1     ,    ce[1]);
		_(NVKM_ENGINE_CE2     ,    ce[2]);
		_(NVKM_ENGINE_CIPHER  ,   cipher);
		_(NVKM_ENGINE_DISP    ,     disp);
		_(NVKM_ENGINE_DMAOBJ  ,      dma);
		_(NVKM_ENGINE_FIFO    ,     fifo);
		_(NVKM_ENGINE_GR      ,       gr);
		_(NVKM_ENGINE_IFB     ,      ifb);
		_(NVKM_ENGINE_ME      ,       me);
		_(NVKM_ENGINE_MPEG    ,     mpeg);
		_(NVKM_ENGINE_MSENC   ,    msenc);
		_(NVKM_ENGINE_MSPDEC  ,   mspdec);
		_(NVKM_ENGINE_MSPPP   ,    msppp);
		_(NVKM_ENGINE_MSVLD   ,    msvld);
		_(NVKM_ENGINE_NVENC0  , nvenc[0]);
		_(NVKM_ENGINE_NVENC1  , nvenc[1]);
		_(NVKM_ENGINE_NVDEC   ,    nvdec);
		_(NVKM_ENGINE_PM      ,       pm);
		_(NVKM_ENGINE_SEC     ,      sec);
		_(NVKM_ENGINE_SW      ,       sw);
		_(NVKM_ENGINE_VIC     ,      vic);
		_(NVKM_ENGINE_VP      ,       vp);
		default:
			WARN_ON(1);
			continue;
		}
#undef _
	}

	ret = 0;
done:
	mutex_unlock(&nv_devices_mutex);
	return ret;
}
