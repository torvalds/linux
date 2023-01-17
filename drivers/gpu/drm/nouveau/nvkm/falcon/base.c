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
#include "priv.h"

#include <subdev/mc.h>
#include <subdev/timer.h>
#include <subdev/top.h>

static const struct nvkm_falcon_func_dma *
nvkm_falcon_dma(struct nvkm_falcon *falcon, enum nvkm_falcon_mem *mem_type, u32 *mem_base)
{
	switch (*mem_type) {
	case IMEM: return falcon->func->imem_dma;
	case DMEM: return falcon->func->dmem_dma;
	default:
		return NULL;
	}
}

int
nvkm_falcon_dma_wr(struct nvkm_falcon *falcon, const u8 *img, u64 dma_addr, u32 dma_base,
		   enum nvkm_falcon_mem mem_type, u32 mem_base, int len, bool sec)
{
	const struct nvkm_falcon_func_dma *dma = nvkm_falcon_dma(falcon, &mem_type, &mem_base);
	const char *type = nvkm_falcon_mem(mem_type);
	const int dmalen = 256;
	u32 dma_start = 0;
	u32 dst, src, cmd;
	int ret, i;

	if (WARN_ON(!dma->xfer))
		return -EINVAL;

	if (mem_type == DMEM) {
		dma_start = dma_base;
		dma_addr += dma_base;
	}

	FLCN_DBG(falcon, "%s %08x <- %08x bytes at %08x (%010llx %08x)",
		 type, mem_base, len, dma_base, dma_addr - dma_base, dma_start);
	if (WARN_ON(!len || (len & (dmalen - 1))))
		return -EINVAL;

	ret = dma->init(falcon, dma_addr, dmalen, mem_type, sec, &cmd);
	if (ret)
		return ret;

	dst = mem_base;
	src = dma_base;
	if (len) {
		while (len >= dmalen) {
			dma->xfer(falcon, dst, src - dma_start, cmd);

			if (img && nvkm_printk_ok(falcon->owner, falcon->user, NV_DBG_TRACE)) {
				for (i = 0; i < dmalen; i += 4, mem_base += 4) {
					const int w = 8, x = (i / 4) % w;

					if (x == 0)
						printk(KERN_INFO "%s %08x <-", type, mem_base);
					printk(KERN_CONT " %08x", *(u32 *)(img + src + i));
					if (x == (w - 1) || ((i + 4) == dmalen))
						printk(KERN_CONT " <- %08x+%08x", dma_base,
						       src + i - dma_base - (x * 4));
					if (i == (7 * 4))
						printk(KERN_CONT " *");
				}
			}

			if (nvkm_msec(falcon->owner->device, 2000,
				if (dma->done(falcon))
					break;
			) < 0)
				return -ETIMEDOUT;

			src += dmalen;
			dst += dmalen;
			len -= dmalen;
		}
		WARN_ON(len);
	}

	return 0;
}

static const struct nvkm_falcon_func_pio *
nvkm_falcon_pio(struct nvkm_falcon *falcon, enum nvkm_falcon_mem *mem_type, u32 *mem_base)
{
	switch (*mem_type) {
	case IMEM:
		return falcon->func->imem_pio;
	case DMEM:
		if (!falcon->func->emem_addr || *mem_base < falcon->func->emem_addr)
			return falcon->func->dmem_pio;

		*mem_base -= falcon->func->emem_addr;
		fallthrough;
	case EMEM:
		return falcon->func->emem_pio;
	default:
		return NULL;
	}
}

int
nvkm_falcon_pio_rd(struct nvkm_falcon *falcon, u8 port, enum nvkm_falcon_mem mem_type, u32 mem_base,
		   const u8 *img, u32 img_base, int len)
{
	const struct nvkm_falcon_func_pio *pio = nvkm_falcon_pio(falcon, &mem_type, &mem_base);
	const char *type = nvkm_falcon_mem(mem_type);
	int xfer_len;

	if (WARN_ON(!pio || !pio->rd))
		return -EINVAL;

	FLCN_DBG(falcon, "%s %08x -> %08x bytes at %08x", type, mem_base, len, img_base);
	if (WARN_ON(!len || (len & (pio->min - 1))))
		return -EINVAL;

	pio->rd_init(falcon, port, mem_base);
	do {
		xfer_len = min(len, pio->max);
		pio->rd(falcon, port, img, xfer_len);

		if (nvkm_printk_ok(falcon->owner, falcon->user, NV_DBG_TRACE)) {
			for (img_base = 0; img_base < xfer_len; img_base += 4, mem_base += 4) {
				if (((img_base / 4) % 8) == 0)
					printk(KERN_INFO "%s %08x ->", type, mem_base);
				printk(KERN_CONT " %08x", *(u32 *)(img + img_base));
			}
		}

		img += xfer_len;
		len -= xfer_len;
	} while (len);

	return 0;
}

int
nvkm_falcon_pio_wr(struct nvkm_falcon *falcon, const u8 *img, u32 img_base, u8 port,
		   enum nvkm_falcon_mem mem_type, u32 mem_base, int len, u16 tag, bool sec)
{
	const struct nvkm_falcon_func_pio *pio = nvkm_falcon_pio(falcon, &mem_type, &mem_base);
	const char *type = nvkm_falcon_mem(mem_type);
	int xfer_len;

	if (WARN_ON(!pio || !pio->wr))
		return -EINVAL;

	FLCN_DBG(falcon, "%s %08x <- %08x bytes at %08x", type, mem_base, len, img_base);
	if (WARN_ON(!len || (len & (pio->min - 1))))
		return -EINVAL;

	pio->wr_init(falcon, port, sec, mem_base);
	do {
		xfer_len = min(len, pio->max);
		pio->wr(falcon, port, img, xfer_len, tag++);

		if (nvkm_printk_ok(falcon->owner, falcon->user, NV_DBG_TRACE)) {
			for (img_base = 0; img_base < xfer_len; img_base += 4, mem_base += 4) {
				if (((img_base / 4) % 8) == 0)
					printk(KERN_INFO "%s %08x <-", type, mem_base);
				printk(KERN_CONT " %08x", *(u32 *)(img + img_base));
				if ((img_base / 4) == 7 && mem_type == IMEM)
					printk(KERN_CONT " %04x", tag - 1);
			}
		}

		img += xfer_len;
		len -= xfer_len;
	} while (len);

	return 0;
}

void
nvkm_falcon_load_imem(struct nvkm_falcon *falcon, void *data, u32 start,
		      u32 size, u16 tag, u8 port, bool secure)
{
	if (secure && !falcon->secret) {
		nvkm_warn(falcon->user,
			  "writing with secure tag on a non-secure falcon!\n");
		return;
	}

	falcon->func->load_imem(falcon, data, start, size, tag, port,
				secure);
}

void
nvkm_falcon_load_dmem(struct nvkm_falcon *falcon, void *data, u32 start,
		      u32 size, u8 port)
{
	mutex_lock(&falcon->dmem_mutex);

	falcon->func->load_dmem(falcon, data, start, size, port);

	mutex_unlock(&falcon->dmem_mutex);
}

void
nvkm_falcon_start(struct nvkm_falcon *falcon)
{
	falcon->func->start(falcon);
}

int
nvkm_falcon_reset(struct nvkm_falcon *falcon)
{
	int ret;

	ret = falcon->func->disable(falcon);
	if (WARN_ON(ret))
		return ret;

	return nvkm_falcon_enable(falcon);
}

static int
nvkm_falcon_oneinit(struct nvkm_falcon *falcon)
{
	const struct nvkm_falcon_func *func = falcon->func;
	const struct nvkm_subdev *subdev = falcon->owner;
	u32 reg;

	if (!falcon->addr) {
		falcon->addr = nvkm_top_addr(subdev->device, subdev->type, subdev->inst);
		if (WARN_ON(!falcon->addr))
			return -ENODEV;
	}

	reg = nvkm_falcon_rd32(falcon, 0x12c);
	falcon->version = reg & 0xf;
	falcon->secret = (reg >> 4) & 0x3;
	falcon->code.ports = (reg >> 8) & 0xf;
	falcon->data.ports = (reg >> 12) & 0xf;

	reg = nvkm_falcon_rd32(falcon, 0x108);
	falcon->code.limit = (reg & 0x1ff) << 8;
	falcon->data.limit = (reg & 0x3fe00) >> 1;

	if (func->debug) {
		u32 val = nvkm_falcon_rd32(falcon, func->debug);
		falcon->debug = (val >> 20) & 0x1;
	}

	return 0;
}

void
nvkm_falcon_put(struct nvkm_falcon *falcon, struct nvkm_subdev *user)
{
	if (unlikely(!falcon))
		return;

	mutex_lock(&falcon->mutex);
	if (falcon->user == user) {
		nvkm_debug(falcon->user, "released %s falcon\n", falcon->name);
		falcon->user = NULL;
	}
	mutex_unlock(&falcon->mutex);
}

int
nvkm_falcon_get(struct nvkm_falcon *falcon, struct nvkm_subdev *user)
{
	int ret = 0;

	mutex_lock(&falcon->mutex);
	if (falcon->user) {
		nvkm_error(user, "%s falcon already acquired by %s!\n",
			   falcon->name, falcon->user->name);
		mutex_unlock(&falcon->mutex);
		return -EBUSY;
	}

	nvkm_debug(user, "acquired %s falcon\n", falcon->name);
	if (!falcon->oneinit)
		ret = nvkm_falcon_oneinit(falcon);
	falcon->user = user;
	mutex_unlock(&falcon->mutex);
	return ret;
}

void
nvkm_falcon_dtor(struct nvkm_falcon *falcon)
{
}

int
nvkm_falcon_ctor(const struct nvkm_falcon_func *func,
		 struct nvkm_subdev *subdev, const char *name, u32 addr,
		 struct nvkm_falcon *falcon)
{
	falcon->func = func;
	falcon->owner = subdev;
	falcon->name = name;
	falcon->addr = addr;
	falcon->addr2 = func->addr2;
	mutex_init(&falcon->mutex);
	mutex_init(&falcon->dmem_mutex);
	return 0;
}
