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
#include "nv50.h"
#include "channv50.h"

static void
g84_fifo_uevent_fini(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x40000000, 0x00000000);
}

static void
g84_fifo_uevent_init(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x40000000, 0x40000000);
}

static struct nvkm_engine *
g84_fifo_id_engine(struct nvkm_fifo *fifo, int engi)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_engine *engine;
	enum nvkm_subdev_type type;

	switch (engi) {
	case G84_FIFO_ENGN_SW    : type = NVKM_ENGINE_SW; break;
	case G84_FIFO_ENGN_GR    : type = NVKM_ENGINE_GR; break;
	case G84_FIFO_ENGN_MPEG  :
		if ((engine = nvkm_device_engine(device, NVKM_ENGINE_MSPPP, 0)))
			return engine;
		type = NVKM_ENGINE_MPEG;
		break;
	case G84_FIFO_ENGN_ME    :
		if ((engine = nvkm_device_engine(device, NVKM_ENGINE_CE, 0)))
			return engine;
		type = NVKM_ENGINE_ME;
		break;
	case G84_FIFO_ENGN_VP    :
		if ((engine = nvkm_device_engine(device, NVKM_ENGINE_MSPDEC, 0)))
			return engine;
		type = NVKM_ENGINE_VP;
		break;
	case G84_FIFO_ENGN_CIPHER:
		if ((engine = nvkm_device_engine(device, NVKM_ENGINE_VIC, 0)))
			return engine;
		if ((engine = nvkm_device_engine(device, NVKM_ENGINE_SEC, 0)))
			return engine;
		type = NVKM_ENGINE_CIPHER;
		break;
	case G84_FIFO_ENGN_BSP   :
		if ((engine = nvkm_device_engine(device, NVKM_ENGINE_MSVLD, 0)))
			return engine;
		type = NVKM_ENGINE_BSP;
		break;
	case G84_FIFO_ENGN_DMA   : type = NVKM_ENGINE_DMAOBJ; break;
	default:
		WARN_ON(1);
		return NULL;
	}

	return nvkm_device_engine(fifo->engine.subdev.device, type, 0);
}

static int
g84_fifo_engine_id(struct nvkm_fifo *base, struct nvkm_engine *engine)
{
	switch (engine->subdev.type) {
	case NVKM_ENGINE_SW    : return G84_FIFO_ENGN_SW;
	case NVKM_ENGINE_GR    : return G84_FIFO_ENGN_GR;
	case NVKM_ENGINE_MPEG  :
	case NVKM_ENGINE_MSPPP : return G84_FIFO_ENGN_MPEG;
	case NVKM_ENGINE_CE    : return G84_FIFO_ENGN_CE0;
	case NVKM_ENGINE_VP    :
	case NVKM_ENGINE_MSPDEC: return G84_FIFO_ENGN_VP;
	case NVKM_ENGINE_CIPHER:
	case NVKM_ENGINE_SEC   : return G84_FIFO_ENGN_CIPHER;
	case NVKM_ENGINE_BSP   :
	case NVKM_ENGINE_MSVLD : return G84_FIFO_ENGN_BSP;
	case NVKM_ENGINE_DMAOBJ: return G84_FIFO_ENGN_DMA;
	default:
		WARN_ON(1);
		return -1;
	}
}

static const struct nvkm_fifo_func
g84_fifo = {
	.dtor = nv50_fifo_dtor,
	.oneinit = nv50_fifo_oneinit,
	.init = nv50_fifo_init,
	.intr = nv04_fifo_intr,
	.engine_id = g84_fifo_engine_id,
	.id_engine = g84_fifo_id_engine,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.uevent_init = g84_fifo_uevent_init,
	.uevent_fini = g84_fifo_uevent_fini,
	.chan = {
		&g84_fifo_gpfifo_oclass,
		NULL
	},
};

int
g84_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	     struct nvkm_fifo **pfifo)
{
	return nv50_fifo_new_(&g84_fifo, device, type, inst, pfifo);
}
