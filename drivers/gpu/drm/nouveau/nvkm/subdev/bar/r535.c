/*
 * Copyright 2023 Red Hat Inc.
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
 */
#include "gf100.h"

#include <core/mm.h>
#include <subdev/fb.h>
#include <subdev/gsp.h>
#include <subdev/instmem.h>
#include <subdev/mmu/vmm.h>

#include <nvrm/nvtypes.h>
#include <nvrm/535.113.01/nvidia/generated/g_rpc-structures.h>
#include <nvrm/535.113.01/nvidia/kernel/inc/vgpu/rpc_global_enums.h>
#include <nvrm/535.113.01/nvidia/kernel/inc/vgpu/rpc_headers.h>

static void
r535_bar_flush(struct nvkm_bar *bar)
{
	ioread32_native(bar->flushBAR2);
}

static void
r535_bar_bar2_wait(struct nvkm_bar *base)
{
}

static int
r535_bar_bar2_update_pde(struct nvkm_gsp *gsp, u64 addr)
{
	rpc_update_bar_pde_v15_00 *rpc;

	rpc = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_UPDATE_BAR_PDE, sizeof(*rpc));
	if (WARN_ON(IS_ERR_OR_NULL(rpc)))
		return -EIO;

	rpc->info.barType = NV_RPC_UPDATE_PDE_BAR_2;
	rpc->info.entryValue = addr ? ((addr >> 4) | 2) : 0; /* PD3 entry format! */
	rpc->info.entryLevelShift = 47; //XXX: probably fetch this from mmu!

	return nvkm_gsp_rpc_wr(gsp, rpc, true);
}

static void
r535_bar_bar2_fini(struct nvkm_bar *bar)
{
	struct nvkm_gsp *gsp = bar->subdev.device->gsp;

	bar->flushBAR2 = bar->flushBAR2PhysMode;
	nvkm_done(bar->flushFBZero);

	WARN_ON(r535_bar_bar2_update_pde(gsp, 0));
}

static void
r535_bar_bar2_init(struct nvkm_bar *bar)
{
	struct nvkm_device *device = bar->subdev.device;
	struct nvkm_vmm *vmm = gf100_bar(bar)->bar[0].vmm;
	struct nvkm_gsp *gsp = device->gsp;

	WARN_ON(r535_bar_bar2_update_pde(gsp, vmm->pd->pde[0]->pt[0]->addr));
	vmm->rm.bar2_pdb = gsp->bar.rm_bar2_pdb;

	if (!bar->flushFBZero) {
		struct nvkm_memory *fbZero;
		int ret;

		ret = nvkm_ram_wrap(device, 0, 0x1000, &fbZero);
		if (ret == 0) {
			ret = nvkm_memory_kmap(fbZero, &bar->flushFBZero);
			nvkm_memory_unref(&fbZero);
		}
		WARN_ON(ret);
	}

	bar->bar2 = true;
	bar->flushBAR2 = nvkm_kmap(bar->flushFBZero);
	WARN_ON(!bar->flushBAR2);
}

static void
r535_bar_bar1_wait(struct nvkm_bar *base)
{
}

static void
r535_bar_bar1_fini(struct nvkm_bar *base)
{
}

static void
r535_bar_bar1_init(struct nvkm_bar *bar)
{
	struct nvkm_device *device = bar->subdev.device;
	struct nvkm_gsp *gsp = device->gsp;
	struct nvkm_vmm *vmm = gf100_bar(bar)->bar[1].vmm;
	struct nvkm_memory *pd3;
	int ret;

	ret = nvkm_ram_wrap(device, gsp->bar.rm_bar1_pdb, 0x1000, &pd3);
	if (WARN_ON(ret))
		return;

	nvkm_memory_unref(&vmm->pd->pt[0]->memory);

	ret = nvkm_memory_kmap(pd3, &vmm->pd->pt[0]->memory);
	nvkm_memory_unref(&pd3);
	if (WARN_ON(ret))
		return;

	vmm->pd->pt[0]->addr = nvkm_memory_addr(vmm->pd->pt[0]->memory);
}

static void *
r535_bar_dtor(struct nvkm_bar *bar)
{
	void *data = gf100_bar_dtor(bar);

	nvkm_memory_unref(&bar->flushFBZero);

	if (bar->flushBAR2PhysMode)
		iounmap(bar->flushBAR2PhysMode);

	kfree(bar->func);
	return data;
}

int
r535_bar_new_(const struct nvkm_bar_func *hw, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_bar **pbar)
{
	struct nvkm_bar_func *rm;
	struct nvkm_bar *bar;
	int ret;

	if (!(rm = kzalloc(sizeof(*rm), GFP_KERNEL)))
		return -ENOMEM;

	rm->dtor = r535_bar_dtor;
	rm->oneinit = hw->oneinit;
	rm->bar1.init = r535_bar_bar1_init;
	rm->bar1.fini = r535_bar_bar1_fini;
	rm->bar1.wait = r535_bar_bar1_wait;
	rm->bar1.vmm = hw->bar1.vmm;
	rm->bar2.init = r535_bar_bar2_init;
	rm->bar2.fini = r535_bar_bar2_fini;
	rm->bar2.wait = r535_bar_bar2_wait;
	rm->bar2.vmm = hw->bar2.vmm;
	rm->flush = r535_bar_flush;

	ret = gf100_bar_new_(rm, device, type, inst, &bar);
	if (ret) {
		kfree(rm);
		return ret;
	}
	*pbar = bar;

	bar->flushBAR2PhysMode = ioremap(device->func->resource_addr(device, 3), PAGE_SIZE);
	if (!bar->flushBAR2PhysMode)
		return -ENOMEM;

	bar->flushBAR2 = bar->flushBAR2PhysMode;

	gf100_bar(*pbar)->bar2_halve = true;
	return 0;
}
