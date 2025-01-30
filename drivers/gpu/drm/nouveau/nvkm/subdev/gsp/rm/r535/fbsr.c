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
#include <subdev/instmem/priv.h>
#include <subdev/gsp.h>

#include <nvhw/drf.h>

#include "nvrm/fbsr.h"
#include "nvrm/rpcfn.h"

struct fbsr_item {
	const char *type;
	u64 addr;
	u64 size;

	struct list_head head;
};

struct fbsr {
	struct list_head items;

	u64 size;
	int regions;

	struct nvkm_gsp_client client;
	struct nvkm_gsp_device device;

	u64 hmemory;
	u64 sys_offset;
};

int
r535_fbsr_memlist(struct nvkm_gsp_device *device, u32 handle, enum nvkm_memory_target aper,
		  u64 phys, u64 size, struct sg_table *sgt, struct nvkm_gsp_object *object)
{
	struct nvkm_gsp_client *client = device->object.client;
	struct nvkm_gsp *gsp = client->gsp;
	const u32 pages = size / GSP_PAGE_SIZE;
	rpc_alloc_memory_v13_01 *rpc;
	int ret;

	rpc = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_ALLOC_MEMORY,
			       sizeof(*rpc) + pages * sizeof(rpc->pteDesc.pte_pde[0]));
	if (IS_ERR(rpc))
		return PTR_ERR(rpc);

	rpc->hClient = client->object.handle;
	rpc->hDevice = device->object.handle;
	rpc->hMemory = handle;
	if (aper == NVKM_MEM_TARGET_HOST) {
		rpc->hClass = NV01_MEMORY_LIST_SYSTEM;
		rpc->flags = NVDEF(NVOS02, FLAGS, PHYSICALITY, NONCONTIGUOUS) |
			     NVDEF(NVOS02, FLAGS, LOCATION, PCI) |
			     NVDEF(NVOS02, FLAGS, MAPPING, NO_MAP);
	} else {
		rpc->hClass = NV01_MEMORY_LIST_FBMEM;
		rpc->flags = NVDEF(NVOS02, FLAGS, PHYSICALITY, CONTIGUOUS) |
			     NVDEF(NVOS02, FLAGS, LOCATION, VIDMEM) |
			     NVDEF(NVOS02, FLAGS, MAPPING, NO_MAP);
		rpc->format = 6; /* NV_MMU_PTE_KIND_GENERIC_MEMORY */
	}
	rpc->pteAdjust = 0;
	rpc->length = size;
	rpc->pageCount = pages;
	rpc->pteDesc.idr = 0;
	rpc->pteDesc.reserved1 = 0;
	rpc->pteDesc.length = pages;

	if (sgt) {
		struct scatterlist *sgl;
		int pte = 0, idx;

		for_each_sgtable_dma_sg(sgt, sgl, idx) {
			for (int i = 0; i < sg_dma_len(sgl) / GSP_PAGE_SIZE; i++)
				rpc->pteDesc.pte_pde[pte++].pte = (sg_dma_address(sgl) >> 12) + i;

		}
	} else {
		for (int i = 0; i < pages; i++)
			rpc->pteDesc.pte_pde[i].pte = (phys >> 12) + i;
	}

	ret = nvkm_gsp_rpc_wr(gsp, rpc, NVKM_GSP_RPC_REPLY_POLL);
	if (ret)
		return ret;

	object->client = device->object.client;
	object->parent = &device->object;
	object->handle = handle;
	return 0;
}

static int
fbsr_send(struct fbsr *fbsr, struct fbsr_item *item)
{
	NV2080_CTRL_INTERNAL_FBSR_SEND_REGION_INFO_PARAMS *ctrl;
	struct nvkm_gsp *gsp = fbsr->client.gsp;
	struct nvkm_gsp_object memlist;
	int ret;

	ret = r535_fbsr_memlist(&fbsr->device, fbsr->hmemory, NVKM_MEM_TARGET_VRAM,
				item->addr, item->size, NULL, &memlist);
	if (ret)
		return ret;

	ctrl = nvkm_gsp_rm_ctrl_get(&gsp->internal.device.subdevice,
				    NV2080_CTRL_CMD_INTERNAL_FBSR_SEND_REGION_INFO,
				    sizeof(*ctrl));
	if (IS_ERR(ctrl)) {
		ret = PTR_ERR(ctrl);
		goto done;
	}

	ctrl->fbsrType = FBSR_TYPE_DMA;
	ctrl->hClient = fbsr->client.object.handle;
	ctrl->hVidMem = fbsr->hmemory++;
	ctrl->vidOffset = 0;
	ctrl->sysOffset = fbsr->sys_offset;
	ctrl->size = item->size;

	ret = nvkm_gsp_rm_ctrl_wr(&gsp->internal.device.subdevice, ctrl);
done:
	nvkm_gsp_rm_free(&memlist);
	if (ret)
		return ret;

	fbsr->sys_offset += item->size;
	return 0;
}

static int
fbsr_init(struct fbsr *fbsr, struct sg_table *sgt, u64 items_size)
{
	NV2080_CTRL_INTERNAL_FBSR_INIT_PARAMS *ctrl;
	struct nvkm_gsp *gsp = fbsr->client.gsp;
	struct nvkm_gsp_object memlist;
	int ret;

	ret = r535_fbsr_memlist(&fbsr->device, fbsr->hmemory, NVKM_MEM_TARGET_HOST,
				0, fbsr->size, sgt, &memlist);
	if (ret)
		return ret;

	ctrl = nvkm_gsp_rm_ctrl_get(&gsp->internal.device.subdevice,
				    NV2080_CTRL_CMD_INTERNAL_FBSR_INIT, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->fbsrType = FBSR_TYPE_DMA;
	ctrl->numRegions = fbsr->regions;
	ctrl->hClient = fbsr->client.object.handle;
	ctrl->hSysMem = fbsr->hmemory++;
	ctrl->gspFbAllocsSysOffset = items_size;

	ret = nvkm_gsp_rm_ctrl_wr(&gsp->internal.device.subdevice, ctrl);
	if (ret)
		return ret;

	nvkm_gsp_rm_free(&memlist);
	return 0;
}

static bool
fbsr_vram(struct fbsr *fbsr, const char *type, u64 addr, u64 size)
{
	struct fbsr_item *item;

	if (!(item = kzalloc(sizeof(*item), GFP_KERNEL)))
		return false;

	item->type = type;
	item->addr = addr;
	item->size = size;
	list_add_tail(&item->head, &fbsr->items);
	return true;
}

static bool
fbsr_inst(struct fbsr *fbsr, const char *type, struct nvkm_memory *memory)
{
	return fbsr_vram(fbsr, type, nvkm_memory_addr(memory), nvkm_memory_size(memory));
}

void
r535_fbsr_resume(struct nvkm_gsp *gsp)
{
	/* RM has restored VRAM contents already, so just need to free the sysmem buffer. */
	nvkm_gsp_sg_free(gsp->subdev.device, &gsp->sr.fbsr);
}

static int
r535_fbsr_suspend(struct nvkm_gsp *gsp)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_instmem *imem = device->imem;
	struct nvkm_instobj *iobj;
	struct fbsr fbsr = {};
	struct fbsr_item *item, *temp;
	u64 items_size;
	int ret;

	INIT_LIST_HEAD(&fbsr.items);
	fbsr.hmemory = 0xcaf00003;

	/* Create a list of all regions we need RM to save during suspend. */
	list_for_each_entry(iobj, &imem->list, head) {
		if (iobj->preserve) {
			if (!fbsr_inst(&fbsr, "inst", &iobj->memory))
				return -ENOMEM;
		}
	}

	list_for_each_entry(iobj, &imem->boot, head) {
		if (!fbsr_inst(&fbsr, "boot", &iobj->memory))
			return -ENOMEM;
	}

	if (!fbsr_vram(&fbsr, "gsp-non-wpr", gsp->fb.heap.addr, gsp->fb.heap.size))
		return -ENOMEM;

	/* Determine memory requirements. */
	list_for_each_entry(item, &fbsr.items, head) {
		nvkm_debug(subdev, "fbsr: %016llx %016llx %s\n",
			   item->addr, item->size, item->type);
		fbsr.size += item->size;
		fbsr.regions++;
	}

	items_size = fbsr.size;
	nvkm_debug(subdev, "fbsr: %d regions (0x%llx bytes)\n", fbsr.regions, items_size);

	fbsr.size += gsp->fb.rsvd_size;
	fbsr.size += gsp->fb.bios.vga_workspace.size;
	nvkm_debug(subdev, "fbsr: size: 0x%llx bytes\n", fbsr.size);

	ret = nvkm_gsp_sg(gsp->subdev.device, fbsr.size, &gsp->sr.fbsr);
	if (ret)
		goto done;

	/* Tell RM about the sysmem which will hold VRAM contents across suspend. */
	ret = nvkm_gsp_client_device_ctor(gsp, &fbsr.client, &fbsr.device);
	if (ret)
		goto done_sgt;

	ret = fbsr_init(&fbsr, &gsp->sr.fbsr, items_size);
	if (WARN_ON(ret))
		goto done_sgt;

	/* Send VRAM regions that need saving. */
	list_for_each_entry(item, &fbsr.items, head) {
		ret = fbsr_send(&fbsr, item);
		if (WARN_ON(ret))
			goto done_sgt;
	}

	/* Cleanup everything except the sysmem backup, which will be removed after resume. */
done_sgt:
	if (ret) /* ... unless we failed already. */
		nvkm_gsp_sg_free(device, &gsp->sr.fbsr);
done:
	list_for_each_entry_safe(item, temp, &fbsr.items, head) {
		list_del(&item->head);
		kfree(item);
	}

	nvkm_gsp_device_dtor(&fbsr.device);
	nvkm_gsp_client_dtor(&fbsr.client);
	return ret;
}

const struct nvkm_rm_api_fbsr
r535_fbsr = {
	.suspend = r535_fbsr_suspend,
	.resume = r535_fbsr_resume,
};

static void *
r535_instmem_dtor(struct nvkm_instmem *imem)
{
	kfree(imem->func);
	return imem;
}

int
r535_instmem_new(const struct nvkm_instmem_func *hw,
		 struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		 struct nvkm_instmem **pinstmem)
{
	struct nvkm_instmem_func *rm;
	int ret;

	if (!(rm = kzalloc(sizeof(*rm), GFP_KERNEL)))
		return -ENOMEM;

	rm->dtor = r535_instmem_dtor;
	rm->fini = hw->fini;
	rm->memory_new = hw->memory_new;
	rm->memory_wrap = hw->memory_wrap;
	rm->zero = false;
	rm->set_bar0_window_addr = hw->set_bar0_window_addr;

	ret = nv50_instmem_new_(rm, device, type, inst, pinstmem);
	if (ret)
		kfree(rm);

	return ret;
}
