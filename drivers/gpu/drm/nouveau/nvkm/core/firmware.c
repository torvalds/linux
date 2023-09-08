/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <core/device.h>
#include <core/firmware.h>

#include <subdev/fb.h>
#include <subdev/mmu.h>

int
nvkm_firmware_load_name(const struct nvkm_subdev *subdev, const char *base,
			const char *name, int ver, const struct firmware **pfw)
{
	char path[64];
	int ret;

	snprintf(path, sizeof(path), "%s%s", base, name);
	ret = nvkm_firmware_get(subdev, path, ver, pfw);
	if (ret < 0)
		return ret;

	return 0;
}

int
nvkm_firmware_load_blob(const struct nvkm_subdev *subdev, const char *base,
			const char *name, int ver, struct nvkm_blob *blob)
{
	const struct firmware *fw;
	int ret;

	ret = nvkm_firmware_load_name(subdev, base, name, ver, &fw);
	if (ret == 0) {
		blob->data = kmemdup(fw->data, fw->size, GFP_KERNEL);
		blob->size = fw->size;
		nvkm_firmware_put(fw);
		if (!blob->data)
			return -ENOMEM;
	}

	return ret;
}

/**
 * nvkm_firmware_get - load firmware from the official nvidia/chip/ directory
 * @subdev:	subdevice that will use that firmware
 * @fwname:	name of firmware file to load
 * @ver:	firmware version to load
 * @fw:		firmware structure to load to
 *
 * Use this function to load firmware files in the form nvidia/chip/fwname.bin.
 * Firmware files released by NVIDIA will always follow this format.
 */
int
nvkm_firmware_get(const struct nvkm_subdev *subdev, const char *fwname, int ver,
		  const struct firmware **fw)
{
	struct nvkm_device *device = subdev->device;
	char f[64];
	char cname[16];
	int i;

	/* Convert device name to lowercase */
	strncpy(cname, device->chip->name, sizeof(cname));
	cname[sizeof(cname) - 1] = '\0';
	i = strlen(cname);
	while (i) {
		--i;
		cname[i] = tolower(cname[i]);
	}

	if (ver != 0)
		snprintf(f, sizeof(f), "nvidia/%s/%s-%d.bin", cname, fwname, ver);
	else
		snprintf(f, sizeof(f), "nvidia/%s/%s.bin", cname, fwname);

	if (!firmware_request_nowarn(fw, f, device->dev)) {
		nvkm_debug(subdev, "firmware \"%s\" loaded - %zu byte(s)\n",
			   f, (*fw)->size);
		return 0;
	}

	nvkm_debug(subdev, "firmware \"%s\" unavailable\n", f);
	return -ENOENT;
}

/*
 * nvkm_firmware_put - release firmware loaded with nvkm_firmware_get
 */
void
nvkm_firmware_put(const struct firmware *fw)
{
	release_firmware(fw);
}

#define nvkm_firmware_mem(p) container_of((p), struct nvkm_firmware, mem.memory)

static int
nvkm_firmware_mem_map(struct nvkm_memory *memory, u64 offset, struct nvkm_vmm *vmm,
		      struct nvkm_vma *vma, void *argv, u32 argc)
{
	struct nvkm_firmware *fw = nvkm_firmware_mem(memory);
	struct nvkm_vmm_map map = {
		.memory = &fw->mem.memory,
		.offset = offset,
		.sgl = &fw->mem.sgl,
	};

	if (WARN_ON(fw->func->type != NVKM_FIRMWARE_IMG_DMA))
		return -ENOSYS;

	return nvkm_vmm_map(vmm, vma, argv, argc, &map);
}

static u64
nvkm_firmware_mem_size(struct nvkm_memory *memory)
{
	return sg_dma_len(&nvkm_firmware_mem(memory)->mem.sgl);
}

static u64
nvkm_firmware_mem_addr(struct nvkm_memory *memory)
{
	return nvkm_firmware_mem(memory)->phys;
}

static u8
nvkm_firmware_mem_page(struct nvkm_memory *memory)
{
	return PAGE_SHIFT;
}

static enum nvkm_memory_target
nvkm_firmware_mem_target(struct nvkm_memory *memory)
{
	if (nvkm_firmware_mem(memory)->device->func->tegra)
		return NVKM_MEM_TARGET_NCOH;

	return NVKM_MEM_TARGET_HOST;
}

static void *
nvkm_firmware_mem_dtor(struct nvkm_memory *memory)
{
	return NULL;
}

static const struct nvkm_memory_func
nvkm_firmware_mem = {
	.dtor = nvkm_firmware_mem_dtor,
	.target = nvkm_firmware_mem_target,
	.page = nvkm_firmware_mem_page,
	.addr = nvkm_firmware_mem_addr,
	.size = nvkm_firmware_mem_size,
	.map = nvkm_firmware_mem_map,
};

void
nvkm_firmware_dtor(struct nvkm_firmware *fw)
{
	struct nvkm_memory *memory = &fw->mem.memory;

	if (!fw->img)
		return;

	switch (fw->func->type) {
	case NVKM_FIRMWARE_IMG_RAM:
		kfree(fw->img);
		break;
	case NVKM_FIRMWARE_IMG_DMA:
		nvkm_memory_unref(&memory);
		dma_free_coherent(fw->device->dev, sg_dma_len(&fw->mem.sgl), fw->img, fw->phys);
		break;
	default:
		WARN_ON(1);
		break;
	}

	fw->img = NULL;
}

int
nvkm_firmware_ctor(const struct nvkm_firmware_func *func, const char *name,
		   struct nvkm_device *device, const void *src, int len, struct nvkm_firmware *fw)
{
	fw->func = func;
	fw->name = name;
	fw->device = device;
	fw->len = len;

	switch (fw->func->type) {
	case NVKM_FIRMWARE_IMG_RAM:
		fw->img = kmemdup(src, fw->len, GFP_KERNEL);
		break;
	case NVKM_FIRMWARE_IMG_DMA: {
		dma_addr_t addr;

		len = ALIGN(fw->len, PAGE_SIZE);

		fw->img = dma_alloc_coherent(fw->device->dev, len, &addr, GFP_KERNEL);
		if (fw->img) {
			memcpy(fw->img, src, fw->len);
			fw->phys = addr;
		}

		sg_init_one(&fw->mem.sgl, fw->img, len);
		sg_dma_address(&fw->mem.sgl) = fw->phys;
		sg_dma_len(&fw->mem.sgl) = len;
	}
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	if (!fw->img)
		return -ENOMEM;

	nvkm_memory_ctor(&nvkm_firmware_mem, &fw->mem.memory);
	return 0;
}
