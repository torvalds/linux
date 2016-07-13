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
#include "gf100.h"
#include "ctxgf100.h"
#include "fuc/os.h"

#include <core/client.h>
#include <core/option.h>
#include <core/firmware.h>
#include <subdev/secboot.h>
#include <subdev/fb.h>
#include <subdev/mc.h>
#include <subdev/pmu.h>
#include <subdev/timer.h>
#include <engine/fifo.h>

#include <nvif/class.h>
#include <nvif/cl9097.h>
#include <nvif/unpack.h>

/*******************************************************************************
 * Zero Bandwidth Clear
 ******************************************************************************/

static void
gf100_gr_zbc_clear_color(struct gf100_gr *gr, int zbc)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	if (gr->zbc_color[zbc].format) {
		nvkm_wr32(device, 0x405804, gr->zbc_color[zbc].ds[0]);
		nvkm_wr32(device, 0x405808, gr->zbc_color[zbc].ds[1]);
		nvkm_wr32(device, 0x40580c, gr->zbc_color[zbc].ds[2]);
		nvkm_wr32(device, 0x405810, gr->zbc_color[zbc].ds[3]);
	}
	nvkm_wr32(device, 0x405814, gr->zbc_color[zbc].format);
	nvkm_wr32(device, 0x405820, zbc);
	nvkm_wr32(device, 0x405824, 0x00000004); /* TRIGGER | WRITE | COLOR */
}

static int
gf100_gr_zbc_color_get(struct gf100_gr *gr, int format,
		       const u32 ds[4], const u32 l2[4])
{
	struct nvkm_ltc *ltc = gr->base.engine.subdev.device->ltc;
	int zbc = -ENOSPC, i;

	for (i = ltc->zbc_min; i <= ltc->zbc_max; i++) {
		if (gr->zbc_color[i].format) {
			if (gr->zbc_color[i].format != format)
				continue;
			if (memcmp(gr->zbc_color[i].ds, ds, sizeof(
				   gr->zbc_color[i].ds)))
				continue;
			if (memcmp(gr->zbc_color[i].l2, l2, sizeof(
				   gr->zbc_color[i].l2))) {
				WARN_ON(1);
				return -EINVAL;
			}
			return i;
		} else {
			zbc = (zbc < 0) ? i : zbc;
		}
	}

	if (zbc < 0)
		return zbc;

	memcpy(gr->zbc_color[zbc].ds, ds, sizeof(gr->zbc_color[zbc].ds));
	memcpy(gr->zbc_color[zbc].l2, l2, sizeof(gr->zbc_color[zbc].l2));
	gr->zbc_color[zbc].format = format;
	nvkm_ltc_zbc_color_get(ltc, zbc, l2);
	gf100_gr_zbc_clear_color(gr, zbc);
	return zbc;
}

static void
gf100_gr_zbc_clear_depth(struct gf100_gr *gr, int zbc)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	if (gr->zbc_depth[zbc].format)
		nvkm_wr32(device, 0x405818, gr->zbc_depth[zbc].ds);
	nvkm_wr32(device, 0x40581c, gr->zbc_depth[zbc].format);
	nvkm_wr32(device, 0x405820, zbc);
	nvkm_wr32(device, 0x405824, 0x00000005); /* TRIGGER | WRITE | DEPTH */
}

static int
gf100_gr_zbc_depth_get(struct gf100_gr *gr, int format,
		       const u32 ds, const u32 l2)
{
	struct nvkm_ltc *ltc = gr->base.engine.subdev.device->ltc;
	int zbc = -ENOSPC, i;

	for (i = ltc->zbc_min; i <= ltc->zbc_max; i++) {
		if (gr->zbc_depth[i].format) {
			if (gr->zbc_depth[i].format != format)
				continue;
			if (gr->zbc_depth[i].ds != ds)
				continue;
			if (gr->zbc_depth[i].l2 != l2) {
				WARN_ON(1);
				return -EINVAL;
			}
			return i;
		} else {
			zbc = (zbc < 0) ? i : zbc;
		}
	}

	if (zbc < 0)
		return zbc;

	gr->zbc_depth[zbc].format = format;
	gr->zbc_depth[zbc].ds = ds;
	gr->zbc_depth[zbc].l2 = l2;
	nvkm_ltc_zbc_depth_get(ltc, zbc, l2);
	gf100_gr_zbc_clear_depth(gr, zbc);
	return zbc;
}

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/
#define gf100_gr_object(p) container_of((p), struct gf100_gr_object, object)

struct gf100_gr_object {
	struct nvkm_object object;
	struct gf100_gr_chan *chan;
};

static int
gf100_fermi_mthd_zbc_color(struct nvkm_object *object, void *data, u32 size)
{
	struct gf100_gr *gr = gf100_gr(nvkm_gr(object->engine));
	union {
		struct fermi_a_zbc_color_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		switch (args->v0.format) {
		case FERMI_A_ZBC_COLOR_V0_FMT_ZERO:
		case FERMI_A_ZBC_COLOR_V0_FMT_UNORM_ONE:
		case FERMI_A_ZBC_COLOR_V0_FMT_RF32_GF32_BF32_AF32:
		case FERMI_A_ZBC_COLOR_V0_FMT_R16_G16_B16_A16:
		case FERMI_A_ZBC_COLOR_V0_FMT_RN16_GN16_BN16_AN16:
		case FERMI_A_ZBC_COLOR_V0_FMT_RS16_GS16_BS16_AS16:
		case FERMI_A_ZBC_COLOR_V0_FMT_RU16_GU16_BU16_AU16:
		case FERMI_A_ZBC_COLOR_V0_FMT_RF16_GF16_BF16_AF16:
		case FERMI_A_ZBC_COLOR_V0_FMT_A8R8G8B8:
		case FERMI_A_ZBC_COLOR_V0_FMT_A8RL8GL8BL8:
		case FERMI_A_ZBC_COLOR_V0_FMT_A2B10G10R10:
		case FERMI_A_ZBC_COLOR_V0_FMT_AU2BU10GU10RU10:
		case FERMI_A_ZBC_COLOR_V0_FMT_A8B8G8R8:
		case FERMI_A_ZBC_COLOR_V0_FMT_A8BL8GL8RL8:
		case FERMI_A_ZBC_COLOR_V0_FMT_AN8BN8GN8RN8:
		case FERMI_A_ZBC_COLOR_V0_FMT_AS8BS8GS8RS8:
		case FERMI_A_ZBC_COLOR_V0_FMT_AU8BU8GU8RU8:
		case FERMI_A_ZBC_COLOR_V0_FMT_A2R10G10B10:
		case FERMI_A_ZBC_COLOR_V0_FMT_BF10GF11RF11:
			ret = gf100_gr_zbc_color_get(gr, args->v0.format,
							   args->v0.ds,
							   args->v0.l2);
			if (ret >= 0) {
				args->v0.index = ret;
				return 0;
			}
			break;
		default:
			return -EINVAL;
		}
	}

	return ret;
}

static int
gf100_fermi_mthd_zbc_depth(struct nvkm_object *object, void *data, u32 size)
{
	struct gf100_gr *gr = gf100_gr(nvkm_gr(object->engine));
	union {
		struct fermi_a_zbc_depth_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		switch (args->v0.format) {
		case FERMI_A_ZBC_DEPTH_V0_FMT_FP32:
			ret = gf100_gr_zbc_depth_get(gr, args->v0.format,
							   args->v0.ds,
							   args->v0.l2);
			return (ret >= 0) ? 0 : -ENOSPC;
		default:
			return -EINVAL;
		}
	}

	return ret;
}

static int
gf100_fermi_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	nvif_ioctl(object, "fermi mthd %08x\n", mthd);
	switch (mthd) {
	case FERMI_A_ZBC_COLOR:
		return gf100_fermi_mthd_zbc_color(object, data, size);
	case FERMI_A_ZBC_DEPTH:
		return gf100_fermi_mthd_zbc_depth(object, data, size);
	default:
		break;
	}
	return -EINVAL;
}

const struct nvkm_object_func
gf100_fermi = {
	.mthd = gf100_fermi_mthd,
};

static void
gf100_gr_mthd_set_shader_exceptions(struct nvkm_device *device, u32 data)
{
	nvkm_wr32(device, 0x419e44, data ? 0xffffffff : 0x00000000);
	nvkm_wr32(device, 0x419e4c, data ? 0xffffffff : 0x00000000);
}

static bool
gf100_gr_mthd_sw(struct nvkm_device *device, u16 class, u32 mthd, u32 data)
{
	switch (class & 0x00ff) {
	case 0x97:
	case 0xc0:
		switch (mthd) {
		case 0x1528:
			gf100_gr_mthd_set_shader_exceptions(device, data);
			return true;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return false;
}

static const struct nvkm_object_func
gf100_gr_object_func = {
};

static int
gf100_gr_object_new(const struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	struct gf100_gr_chan *chan = gf100_gr_chan(oclass->parent);
	struct gf100_gr_object *object;

	if (!(object = kzalloc(sizeof(*object), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &object->object;

	nvkm_object_ctor(oclass->base.func ? oclass->base.func :
			 &gf100_gr_object_func, oclass, &object->object);
	object->chan = chan;
	return 0;
}

static int
gf100_gr_object_get(struct nvkm_gr *base, int index, struct nvkm_sclass *sclass)
{
	struct gf100_gr *gr = gf100_gr(base);
	int c = 0;

	while (gr->func->sclass[c].oclass) {
		if (c++ == index) {
			*sclass = gr->func->sclass[index];
			sclass->ctor = gf100_gr_object_new;
			return index;
		}
	}

	return c;
}

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
gf100_gr_chan_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
		   int align, struct nvkm_gpuobj **pgpuobj)
{
	struct gf100_gr_chan *chan = gf100_gr_chan(object);
	struct gf100_gr *gr = chan->gr;
	int ret, i;

	ret = nvkm_gpuobj_new(gr->base.engine.subdev.device, gr->size,
			      align, false, parent, pgpuobj);
	if (ret)
		return ret;

	nvkm_kmap(*pgpuobj);
	for (i = 0; i < gr->size; i += 4)
		nvkm_wo32(*pgpuobj, i, gr->data[i / 4]);

	if (!gr->firmware) {
		nvkm_wo32(*pgpuobj, 0x00, chan->mmio_nr / 2);
		nvkm_wo32(*pgpuobj, 0x04, chan->mmio_vma.offset >> 8);
	} else {
		nvkm_wo32(*pgpuobj, 0xf4, 0);
		nvkm_wo32(*pgpuobj, 0xf8, 0);
		nvkm_wo32(*pgpuobj, 0x10, chan->mmio_nr / 2);
		nvkm_wo32(*pgpuobj, 0x14, lower_32_bits(chan->mmio_vma.offset));
		nvkm_wo32(*pgpuobj, 0x18, upper_32_bits(chan->mmio_vma.offset));
		nvkm_wo32(*pgpuobj, 0x1c, 1);
		nvkm_wo32(*pgpuobj, 0x20, 0);
		nvkm_wo32(*pgpuobj, 0x28, 0);
		nvkm_wo32(*pgpuobj, 0x2c, 0);
	}
	nvkm_done(*pgpuobj);
	return 0;
}

static void *
gf100_gr_chan_dtor(struct nvkm_object *object)
{
	struct gf100_gr_chan *chan = gf100_gr_chan(object);
	int i;

	for (i = 0; i < ARRAY_SIZE(chan->data); i++) {
		if (chan->data[i].vma.node) {
			nvkm_vm_unmap(&chan->data[i].vma);
			nvkm_vm_put(&chan->data[i].vma);
		}
		nvkm_memory_del(&chan->data[i].mem);
	}

	if (chan->mmio_vma.node) {
		nvkm_vm_unmap(&chan->mmio_vma);
		nvkm_vm_put(&chan->mmio_vma);
	}
	nvkm_memory_del(&chan->mmio);
	return chan;
}

static const struct nvkm_object_func
gf100_gr_chan = {
	.dtor = gf100_gr_chan_dtor,
	.bind = gf100_gr_chan_bind,
};

static int
gf100_gr_chan_new(struct nvkm_gr *base, struct nvkm_fifo_chan *fifoch,
		  const struct nvkm_oclass *oclass,
		  struct nvkm_object **pobject)
{
	struct gf100_gr *gr = gf100_gr(base);
	struct gf100_gr_data *data = gr->mmio_data;
	struct gf100_gr_mmio *mmio = gr->mmio_list;
	struct gf100_gr_chan *chan;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int ret, i;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_object_ctor(&gf100_gr_chan, oclass, &chan->object);
	chan->gr = gr;
	*pobject = &chan->object;

	/* allocate memory for a "mmio list" buffer that's used by the HUB
	 * fuc to modify some per-context register settings on first load
	 * of the context.
	 */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x1000, 0x100,
			      false, &chan->mmio);
	if (ret)
		return ret;

	ret = nvkm_vm_get(fifoch->vm, 0x1000, 12, NV_MEM_ACCESS_RW |
			  NV_MEM_ACCESS_SYS, &chan->mmio_vma);
	if (ret)
		return ret;

	nvkm_memory_map(chan->mmio, &chan->mmio_vma, 0);

	/* allocate buffers referenced by mmio list */
	for (i = 0; data->size && i < ARRAY_SIZE(gr->mmio_data); i++) {
		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
				      data->size, data->align, false,
				      &chan->data[i].mem);
		if (ret)
			return ret;

		ret = nvkm_vm_get(fifoch->vm,
				  nvkm_memory_size(chan->data[i].mem), 12,
				  data->access, &chan->data[i].vma);
		if (ret)
			return ret;

		nvkm_memory_map(chan->data[i].mem, &chan->data[i].vma, 0);
		data++;
	}

	/* finally, fill in the mmio list and point the context at it */
	nvkm_kmap(chan->mmio);
	for (i = 0; mmio->addr && i < ARRAY_SIZE(gr->mmio_list); i++) {
		u32 addr = mmio->addr;
		u32 data = mmio->data;

		if (mmio->buffer >= 0) {
			u64 info = chan->data[mmio->buffer].vma.offset;
			data |= info >> mmio->shift;
		}

		nvkm_wo32(chan->mmio, chan->mmio_nr++ * 4, addr);
		nvkm_wo32(chan->mmio, chan->mmio_nr++ * 4, data);
		mmio++;
	}
	nvkm_done(chan->mmio);
	return 0;
}

/*******************************************************************************
 * PGRAPH register lists
 ******************************************************************************/

const struct gf100_gr_init
gf100_gr_init_main_0[] = {
	{ 0x400080,   1, 0x04, 0x003083c2 },
	{ 0x400088,   1, 0x04, 0x00006fe7 },
	{ 0x40008c,   1, 0x04, 0x00000000 },
	{ 0x400090,   1, 0x04, 0x00000030 },
	{ 0x40013c,   1, 0x04, 0x013901f7 },
	{ 0x400140,   1, 0x04, 0x00000100 },
	{ 0x400144,   1, 0x04, 0x00000000 },
	{ 0x400148,   1, 0x04, 0x00000110 },
	{ 0x400138,   1, 0x04, 0x00000000 },
	{ 0x400130,   2, 0x04, 0x00000000 },
	{ 0x400124,   1, 0x04, 0x00000002 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_fe_0[] = {
	{ 0x40415c,   1, 0x04, 0x00000000 },
	{ 0x404170,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_pri_0[] = {
	{ 0x404488,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_rstr2d_0[] = {
	{ 0x407808,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_pd_0[] = {
	{ 0x406024,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_ds_0[] = {
	{ 0x405844,   1, 0x04, 0x00ffffff },
	{ 0x405850,   1, 0x04, 0x00000000 },
	{ 0x405908,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_scc_0[] = {
	{ 0x40803c,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_prop_0[] = {
	{ 0x4184a0,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_gpc_unk_0[] = {
	{ 0x418604,   1, 0x04, 0x00000000 },
	{ 0x418680,   1, 0x04, 0x00000000 },
	{ 0x418714,   1, 0x04, 0x80000000 },
	{ 0x418384,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_setup_0[] = {
	{ 0x418814,   3, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_crstr_0[] = {
	{ 0x418b04,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_setup_1[] = {
	{ 0x4188c8,   1, 0x04, 0x80000000 },
	{ 0x4188cc,   1, 0x04, 0x00000000 },
	{ 0x4188d0,   1, 0x04, 0x00010000 },
	{ 0x4188d4,   1, 0x04, 0x00000001 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_zcull_0[] = {
	{ 0x418910,   1, 0x04, 0x00010001 },
	{ 0x418914,   1, 0x04, 0x00000301 },
	{ 0x418918,   1, 0x04, 0x00800000 },
	{ 0x418980,   1, 0x04, 0x77777770 },
	{ 0x418984,   3, 0x04, 0x77777777 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_gpm_0[] = {
	{ 0x418c04,   1, 0x04, 0x00000000 },
	{ 0x418c88,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_gpc_unk_1[] = {
	{ 0x418d00,   1, 0x04, 0x00000000 },
	{ 0x418f08,   1, 0x04, 0x00000000 },
	{ 0x418e00,   1, 0x04, 0x00000050 },
	{ 0x418e08,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_gcc_0[] = {
	{ 0x41900c,   1, 0x04, 0x00000000 },
	{ 0x419018,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_tpccs_0[] = {
	{ 0x419d08,   2, 0x04, 0x00000000 },
	{ 0x419d10,   1, 0x04, 0x00000014 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_tex_0[] = {
	{ 0x419ab0,   1, 0x04, 0x00000000 },
	{ 0x419ab8,   1, 0x04, 0x000000e7 },
	{ 0x419abc,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_pe_0[] = {
	{ 0x41980c,   3, 0x04, 0x00000000 },
	{ 0x419844,   1, 0x04, 0x00000000 },
	{ 0x41984c,   1, 0x04, 0x00005bc5 },
	{ 0x419850,   4, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_l1c_0[] = {
	{ 0x419c98,   1, 0x04, 0x00000000 },
	{ 0x419ca8,   1, 0x04, 0x80000000 },
	{ 0x419cb4,   1, 0x04, 0x00000000 },
	{ 0x419cb8,   1, 0x04, 0x00008bf4 },
	{ 0x419cbc,   1, 0x04, 0x28137606 },
	{ 0x419cc0,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_wwdx_0[] = {
	{ 0x419bd4,   1, 0x04, 0x00800000 },
	{ 0x419bdc,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_tpccs_1[] = {
	{ 0x419d2c,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_mpc_0[] = {
	{ 0x419c0c,   1, 0x04, 0x00000000 },
	{}
};

static const struct gf100_gr_init
gf100_gr_init_sm_0[] = {
	{ 0x419e00,   1, 0x04, 0x00000000 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x00001100 },
	{ 0x419eac,   1, 0x04, 0x11100702 },
	{ 0x419eb0,   1, 0x04, 0x00000003 },
	{ 0x419eb4,   4, 0x04, 0x00000000 },
	{ 0x419ec8,   1, 0x04, 0x06060618 },
	{ 0x419ed0,   1, 0x04, 0x0eff0e38 },
	{ 0x419ed4,   1, 0x04, 0x011104f1 },
	{ 0x419edc,   1, 0x04, 0x00000000 },
	{ 0x419f00,   1, 0x04, 0x00000000 },
	{ 0x419f2c,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_be_0[] = {
	{ 0x40880c,   1, 0x04, 0x00000000 },
	{ 0x408910,   9, 0x04, 0x00000000 },
	{ 0x408950,   1, 0x04, 0x00000000 },
	{ 0x408954,   1, 0x04, 0x0000ffff },
	{ 0x408984,   1, 0x04, 0x00000000 },
	{ 0x408988,   1, 0x04, 0x08040201 },
	{ 0x40898c,   1, 0x04, 0x80402010 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_fe_1[] = {
	{ 0x4040f0,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf100_gr_init_pe_1[] = {
	{ 0x419880,   1, 0x04, 0x00000002 },
	{}
};

static const struct gf100_gr_pack
gf100_gr_pack_mmio[] = {
	{ gf100_gr_init_main_0 },
	{ gf100_gr_init_fe_0 },
	{ gf100_gr_init_pri_0 },
	{ gf100_gr_init_rstr2d_0 },
	{ gf100_gr_init_pd_0 },
	{ gf100_gr_init_ds_0 },
	{ gf100_gr_init_scc_0 },
	{ gf100_gr_init_prop_0 },
	{ gf100_gr_init_gpc_unk_0 },
	{ gf100_gr_init_setup_0 },
	{ gf100_gr_init_crstr_0 },
	{ gf100_gr_init_setup_1 },
	{ gf100_gr_init_zcull_0 },
	{ gf100_gr_init_gpm_0 },
	{ gf100_gr_init_gpc_unk_1 },
	{ gf100_gr_init_gcc_0 },
	{ gf100_gr_init_tpccs_0 },
	{ gf100_gr_init_tex_0 },
	{ gf100_gr_init_pe_0 },
	{ gf100_gr_init_l1c_0 },
	{ gf100_gr_init_wwdx_0 },
	{ gf100_gr_init_tpccs_1 },
	{ gf100_gr_init_mpc_0 },
	{ gf100_gr_init_sm_0 },
	{ gf100_gr_init_be_0 },
	{ gf100_gr_init_fe_1 },
	{ gf100_gr_init_pe_1 },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

int
gf100_gr_rops(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	return (nvkm_rd32(device, 0x409604) & 0x001f0000) >> 16;
}

void
gf100_gr_zbc_init(struct gf100_gr *gr)
{
	const u32  zero[] = { 0x00000000, 0x00000000, 0x00000000, 0x00000000,
			      0x00000000, 0x00000000, 0x00000000, 0x00000000 };
	const u32   one[] = { 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000,
			      0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
	const u32 f32_0[] = { 0x00000000, 0x00000000, 0x00000000, 0x00000000,
			      0x00000000, 0x00000000, 0x00000000, 0x00000000 };
	const u32 f32_1[] = { 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000,
			      0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000 };
	struct nvkm_ltc *ltc = gr->base.engine.subdev.device->ltc;
	int index;

	if (!gr->zbc_color[0].format) {
		gf100_gr_zbc_color_get(gr, 1,  & zero[0],   &zero[4]);
		gf100_gr_zbc_color_get(gr, 2,  &  one[0],    &one[4]);
		gf100_gr_zbc_color_get(gr, 4,  &f32_0[0],  &f32_0[4]);
		gf100_gr_zbc_color_get(gr, 4,  &f32_1[0],  &f32_1[4]);
		gf100_gr_zbc_depth_get(gr, 1, 0x00000000, 0x00000000);
		gf100_gr_zbc_depth_get(gr, 1, 0x3f800000, 0x3f800000);
	}

	for (index = ltc->zbc_min; index <= ltc->zbc_max; index++)
		gf100_gr_zbc_clear_color(gr, index);
	for (index = ltc->zbc_min; index <= ltc->zbc_max; index++)
		gf100_gr_zbc_clear_depth(gr, index);
}

/**
 * Wait until GR goes idle. GR is considered idle if it is disabled by the
 * MC (0x200) register, or GR is not busy and a context switch is not in
 * progress.
 */
int
gf100_gr_wait_idle(struct gf100_gr *gr)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	unsigned long end_jiffies = jiffies + msecs_to_jiffies(2000);
	bool gr_enabled, ctxsw_active, gr_busy;

	do {
		/*
		 * required to make sure FIFO_ENGINE_STATUS (0x2640) is
		 * up-to-date
		 */
		nvkm_rd32(device, 0x400700);

		gr_enabled = nvkm_rd32(device, 0x200) & 0x1000;
		ctxsw_active = nvkm_rd32(device, 0x2640) & 0x8000;
		gr_busy = nvkm_rd32(device, 0x40060c) & 0x1;

		if (!gr_enabled || (!gr_busy && !ctxsw_active))
			return 0;
	} while (time_before(jiffies, end_jiffies));

	nvkm_error(subdev,
		   "wait for idle timeout (en: %d, ctxsw: %d, busy: %d)\n",
		   gr_enabled, ctxsw_active, gr_busy);
	return -EAGAIN;
}

void
gf100_gr_mmio(struct gf100_gr *gr, const struct gf100_gr_pack *p)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const struct gf100_gr_pack *pack;
	const struct gf100_gr_init *init;

	pack_for_each_init(init, pack, p) {
		u32 next = init->addr + init->count * init->pitch;
		u32 addr = init->addr;
		while (addr < next) {
			nvkm_wr32(device, addr, init->data);
			addr += init->pitch;
		}
	}
}

void
gf100_gr_icmd(struct gf100_gr *gr, const struct gf100_gr_pack *p)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const struct gf100_gr_pack *pack;
	const struct gf100_gr_init *init;
	u32 data = 0;

	nvkm_wr32(device, 0x400208, 0x80000000);

	pack_for_each_init(init, pack, p) {
		u32 next = init->addr + init->count * init->pitch;
		u32 addr = init->addr;

		if ((pack == p && init == p->init) || data != init->data) {
			nvkm_wr32(device, 0x400204, init->data);
			data = init->data;
		}

		while (addr < next) {
			nvkm_wr32(device, 0x400200, addr);
			/**
			 * Wait for GR to go idle after submitting a
			 * GO_IDLE bundle
			 */
			if ((addr & 0xffff) == 0xe100)
				gf100_gr_wait_idle(gr);
			nvkm_msec(device, 2000,
				if (!(nvkm_rd32(device, 0x400700) & 0x00000004))
					break;
			);
			addr += init->pitch;
		}
	}

	nvkm_wr32(device, 0x400208, 0x00000000);
}

void
gf100_gr_mthd(struct gf100_gr *gr, const struct gf100_gr_pack *p)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const struct gf100_gr_pack *pack;
	const struct gf100_gr_init *init;
	u32 data = 0;

	pack_for_each_init(init, pack, p) {
		u32 ctrl = 0x80000000 | pack->type;
		u32 next = init->addr + init->count * init->pitch;
		u32 addr = init->addr;

		if ((pack == p && init == p->init) || data != init->data) {
			nvkm_wr32(device, 0x40448c, init->data);
			data = init->data;
		}

		while (addr < next) {
			nvkm_wr32(device, 0x404488, ctrl | (addr << 14));
			addr += init->pitch;
		}
	}
}

u64
gf100_gr_units(struct nvkm_gr *base)
{
	struct gf100_gr *gr = gf100_gr(base);
	u64 cfg;

	cfg  = (u32)gr->gpc_nr;
	cfg |= (u32)gr->tpc_total << 8;
	cfg |= (u64)gr->rop_nr << 32;

	return cfg;
}

static const struct nvkm_bitfield gf100_dispatch_error[] = {
	{ 0x00000001, "INJECTED_BUNDLE_ERROR" },
	{ 0x00000002, "CLASS_SUBCH_MISMATCH" },
	{ 0x00000004, "SUBCHSW_DURING_NOTIFY" },
	{}
};

static const struct nvkm_bitfield gf100_m2mf_error[] = {
	{ 0x00000001, "PUSH_TOO_MUCH_DATA" },
	{ 0x00000002, "PUSH_NOT_ENOUGH_DATA" },
	{}
};

static const struct nvkm_bitfield gf100_unk6_error[] = {
	{ 0x00000001, "TEMP_TOO_SMALL" },
	{}
};

static const struct nvkm_bitfield gf100_ccache_error[] = {
	{ 0x00000001, "INTR" },
	{ 0x00000002, "LDCONST_OOB" },
	{}
};

static const struct nvkm_bitfield gf100_macro_error[] = {
	{ 0x00000001, "TOO_FEW_PARAMS" },
	{ 0x00000002, "TOO_MANY_PARAMS" },
	{ 0x00000004, "ILLEGAL_OPCODE" },
	{ 0x00000008, "DOUBLE_BRANCH" },
	{ 0x00000010, "WATCHDOG" },
	{}
};

static const struct nvkm_bitfield gk104_sked_error[] = {
	{ 0x00000040, "CTA_RESUME" },
	{ 0x00000080, "CONSTANT_BUFFER_SIZE" },
	{ 0x00000200, "LOCAL_MEMORY_SIZE_POS" },
	{ 0x00000400, "LOCAL_MEMORY_SIZE_NEG" },
	{ 0x00000800, "WARP_CSTACK_SIZE" },
	{ 0x00001000, "TOTAL_TEMP_SIZE" },
	{ 0x00002000, "REGISTER_COUNT" },
	{ 0x00040000, "TOTAL_THREADS" },
	{ 0x00100000, "PROGRAM_OFFSET" },
	{ 0x00200000, "SHARED_MEMORY_SIZE" },
	{ 0x00800000, "CTA_THREAD_DIMENSION_ZERO" },
	{ 0x01000000, "MEMORY_WINDOW_OVERLAP" },
	{ 0x02000000, "SHARED_CONFIG_TOO_SMALL" },
	{ 0x04000000, "TOTAL_REGISTER_COUNT" },
	{}
};

static const struct nvkm_bitfield gf100_gpc_rop_error[] = {
	{ 0x00000002, "RT_PITCH_OVERRUN" },
	{ 0x00000010, "RT_WIDTH_OVERRUN" },
	{ 0x00000020, "RT_HEIGHT_OVERRUN" },
	{ 0x00000080, "ZETA_STORAGE_TYPE_MISMATCH" },
	{ 0x00000100, "RT_STORAGE_TYPE_MISMATCH" },
	{ 0x00000400, "RT_LINEAR_MISMATCH" },
	{}
};

static void
gf100_gr_trap_gpc_rop(struct gf100_gr *gr, int gpc)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	char error[128];
	u32 trap[4];

	trap[0] = nvkm_rd32(device, GPC_UNIT(gpc, 0x0420)) & 0x3fffffff;
	trap[1] = nvkm_rd32(device, GPC_UNIT(gpc, 0x0434));
	trap[2] = nvkm_rd32(device, GPC_UNIT(gpc, 0x0438));
	trap[3] = nvkm_rd32(device, GPC_UNIT(gpc, 0x043c));

	nvkm_snprintbf(error, sizeof(error), gf100_gpc_rop_error, trap[0]);

	nvkm_error(subdev, "GPC%d/PROP trap: %08x [%s] x = %u, y = %u, "
			   "format = %x, storage type = %x\n",
		   gpc, trap[0], error, trap[1] & 0xffff, trap[1] >> 16,
		   (trap[2] >> 8) & 0x3f, trap[3] & 0xff);
	nvkm_wr32(device, GPC_UNIT(gpc, 0x0420), 0xc0000000);
}

static const struct nvkm_enum gf100_mp_warp_error[] = {
	{ 0x01, "STACK_ERROR" },
	{ 0x02, "API_STACK_ERROR" },
	{ 0x03, "RET_EMPTY_STACK_ERROR" },
	{ 0x04, "PC_WRAP" },
	{ 0x05, "MISALIGNED_PC" },
	{ 0x06, "PC_OVERFLOW" },
	{ 0x07, "MISALIGNED_IMMC_ADDR" },
	{ 0x08, "MISALIGNED_REG" },
	{ 0x09, "ILLEGAL_INSTR_ENCODING" },
	{ 0x0a, "ILLEGAL_SPH_INSTR_COMBO" },
	{ 0x0b, "ILLEGAL_INSTR_PARAM" },
	{ 0x0c, "INVALID_CONST_ADDR" },
	{ 0x0d, "OOR_REG" },
	{ 0x0e, "OOR_ADDR" },
	{ 0x0f, "MISALIGNED_ADDR" },
	{ 0x10, "INVALID_ADDR_SPACE" },
	{ 0x11, "ILLEGAL_INSTR_PARAM2" },
	{ 0x12, "INVALID_CONST_ADDR_LDC" },
	{ 0x13, "GEOMETRY_SM_ERROR" },
	{ 0x14, "DIVERGENT" },
	{ 0x15, "WARP_EXIT" },
	{}
};

static const struct nvkm_bitfield gf100_mp_global_error[] = {
	{ 0x00000001, "SM_TO_SM_FAULT" },
	{ 0x00000002, "L1_ERROR" },
	{ 0x00000004, "MULTIPLE_WARP_ERRORS" },
	{ 0x00000008, "PHYSICAL_STACK_OVERFLOW" },
	{ 0x00000010, "BPT_INT" },
	{ 0x00000020, "BPT_PAUSE" },
	{ 0x00000040, "SINGLE_STEP_COMPLETE" },
	{ 0x20000000, "ECC_SEC_ERROR" },
	{ 0x40000000, "ECC_DED_ERROR" },
	{ 0x80000000, "TIMEOUT" },
	{}
};

static void
gf100_gr_trap_mp(struct gf100_gr *gr, int gpc, int tpc)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 werr = nvkm_rd32(device, TPC_UNIT(gpc, tpc, 0x648));
	u32 gerr = nvkm_rd32(device, TPC_UNIT(gpc, tpc, 0x650));
	const struct nvkm_enum *warp;
	char glob[128];

	nvkm_snprintbf(glob, sizeof(glob), gf100_mp_global_error, gerr);
	warp = nvkm_enum_find(gf100_mp_warp_error, werr & 0xffff);

	nvkm_error(subdev, "GPC%i/TPC%i/MP trap: "
			   "global %08x [%s] warp %04x [%s]\n",
		   gpc, tpc, gerr, glob, werr, warp ? warp->name : "");

	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x648), 0x00000000);
	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x650), gerr);
}

static void
gf100_gr_trap_tpc(struct gf100_gr *gr, int gpc, int tpc)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, TPC_UNIT(gpc, tpc, 0x0508));

	if (stat & 0x00000001) {
		u32 trap = nvkm_rd32(device, TPC_UNIT(gpc, tpc, 0x0224));
		nvkm_error(subdev, "GPC%d/TPC%d/TEX: %08x\n", gpc, tpc, trap);
		nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x0224), 0xc0000000);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000002) {
		gf100_gr_trap_mp(gr, gpc, tpc);
		stat &= ~0x00000002;
	}

	if (stat & 0x00000004) {
		u32 trap = nvkm_rd32(device, TPC_UNIT(gpc, tpc, 0x0084));
		nvkm_error(subdev, "GPC%d/TPC%d/POLY: %08x\n", gpc, tpc, trap);
		nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x0084), 0xc0000000);
		stat &= ~0x00000004;
	}

	if (stat & 0x00000008) {
		u32 trap = nvkm_rd32(device, TPC_UNIT(gpc, tpc, 0x048c));
		nvkm_error(subdev, "GPC%d/TPC%d/L1C: %08x\n", gpc, tpc, trap);
		nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x048c), 0xc0000000);
		stat &= ~0x00000008;
	}

	if (stat) {
		nvkm_error(subdev, "GPC%d/TPC%d/%08x: unknown\n", gpc, tpc, stat);
	}
}

static void
gf100_gr_trap_gpc(struct gf100_gr *gr, int gpc)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, GPC_UNIT(gpc, 0x2c90));
	int tpc;

	if (stat & 0x00000001) {
		gf100_gr_trap_gpc_rop(gr, gpc);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000002) {
		u32 trap = nvkm_rd32(device, GPC_UNIT(gpc, 0x0900));
		nvkm_error(subdev, "GPC%d/ZCULL: %08x\n", gpc, trap);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		stat &= ~0x00000002;
	}

	if (stat & 0x00000004) {
		u32 trap = nvkm_rd32(device, GPC_UNIT(gpc, 0x1028));
		nvkm_error(subdev, "GPC%d/CCACHE: %08x\n", gpc, trap);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		stat &= ~0x00000004;
	}

	if (stat & 0x00000008) {
		u32 trap = nvkm_rd32(device, GPC_UNIT(gpc, 0x0824));
		nvkm_error(subdev, "GPC%d/ESETUP: %08x\n", gpc, trap);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		stat &= ~0x00000009;
	}

	for (tpc = 0; tpc < gr->tpc_nr[gpc]; tpc++) {
		u32 mask = 0x00010000 << tpc;
		if (stat & mask) {
			gf100_gr_trap_tpc(gr, gpc, tpc);
			nvkm_wr32(device, GPC_UNIT(gpc, 0x2c90), mask);
			stat &= ~mask;
		}
	}

	if (stat) {
		nvkm_error(subdev, "GPC%d/%08x: unknown\n", gpc, stat);
	}
}

static void
gf100_gr_trap_intr(struct gf100_gr *gr)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	char error[128];
	u32 trap = nvkm_rd32(device, 0x400108);
	int rop, gpc;

	if (trap & 0x00000001) {
		u32 stat = nvkm_rd32(device, 0x404000);

		nvkm_snprintbf(error, sizeof(error), gf100_dispatch_error,
			       stat & 0x3fffffff);
		nvkm_error(subdev, "DISPATCH %08x [%s]\n", stat, error);
		nvkm_wr32(device, 0x404000, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x00000001);
		trap &= ~0x00000001;
	}

	if (trap & 0x00000002) {
		u32 stat = nvkm_rd32(device, 0x404600);

		nvkm_snprintbf(error, sizeof(error), gf100_m2mf_error,
			       stat & 0x3fffffff);
		nvkm_error(subdev, "M2MF %08x [%s]\n", stat, error);

		nvkm_wr32(device, 0x404600, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x00000002);
		trap &= ~0x00000002;
	}

	if (trap & 0x00000008) {
		u32 stat = nvkm_rd32(device, 0x408030);

		nvkm_snprintbf(error, sizeof(error), gf100_m2mf_error,
			       stat & 0x3fffffff);
		nvkm_error(subdev, "CCACHE %08x [%s]\n", stat, error);
		nvkm_wr32(device, 0x408030, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x00000008);
		trap &= ~0x00000008;
	}

	if (trap & 0x00000010) {
		u32 stat = nvkm_rd32(device, 0x405840);
		nvkm_error(subdev, "SHADER %08x, sph: 0x%06x, stage: 0x%02x\n",
			   stat, stat & 0xffffff, (stat >> 24) & 0x3f);
		nvkm_wr32(device, 0x405840, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x00000010);
		trap &= ~0x00000010;
	}

	if (trap & 0x00000040) {
		u32 stat = nvkm_rd32(device, 0x40601c);

		nvkm_snprintbf(error, sizeof(error), gf100_unk6_error,
			       stat & 0x3fffffff);
		nvkm_error(subdev, "UNK6 %08x [%s]\n", stat, error);

		nvkm_wr32(device, 0x40601c, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x00000040);
		trap &= ~0x00000040;
	}

	if (trap & 0x00000080) {
		u32 stat = nvkm_rd32(device, 0x404490);
		u32 pc = nvkm_rd32(device, 0x404494);
		u32 op = nvkm_rd32(device, 0x40449c);

		nvkm_snprintbf(error, sizeof(error), gf100_macro_error,
			       stat & 0x1fffffff);
		nvkm_error(subdev, "MACRO %08x [%s], pc: 0x%03x%s, op: 0x%08x\n",
			   stat, error, pc & 0x7ff,
			   (pc & 0x10000000) ? "" : " (invalid)",
			   op);

		nvkm_wr32(device, 0x404490, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x00000080);
		trap &= ~0x00000080;
	}

	if (trap & 0x00000100) {
		u32 stat = nvkm_rd32(device, 0x407020) & 0x3fffffff;

		nvkm_snprintbf(error, sizeof(error), gk104_sked_error, stat);
		nvkm_error(subdev, "SKED: %08x [%s]\n", stat, error);

		if (stat)
			nvkm_wr32(device, 0x407020, 0x40000000);
		nvkm_wr32(device, 0x400108, 0x00000100);
		trap &= ~0x00000100;
	}

	if (trap & 0x01000000) {
		u32 stat = nvkm_rd32(device, 0x400118);
		for (gpc = 0; stat && gpc < gr->gpc_nr; gpc++) {
			u32 mask = 0x00000001 << gpc;
			if (stat & mask) {
				gf100_gr_trap_gpc(gr, gpc);
				nvkm_wr32(device, 0x400118, mask);
				stat &= ~mask;
			}
		}
		nvkm_wr32(device, 0x400108, 0x01000000);
		trap &= ~0x01000000;
	}

	if (trap & 0x02000000) {
		for (rop = 0; rop < gr->rop_nr; rop++) {
			u32 statz = nvkm_rd32(device, ROP_UNIT(rop, 0x070));
			u32 statc = nvkm_rd32(device, ROP_UNIT(rop, 0x144));
			nvkm_error(subdev, "ROP%d %08x %08x\n",
				 rop, statz, statc);
			nvkm_wr32(device, ROP_UNIT(rop, 0x070), 0xc0000000);
			nvkm_wr32(device, ROP_UNIT(rop, 0x144), 0xc0000000);
		}
		nvkm_wr32(device, 0x400108, 0x02000000);
		trap &= ~0x02000000;
	}

	if (trap) {
		nvkm_error(subdev, "TRAP UNHANDLED %08x\n", trap);
		nvkm_wr32(device, 0x400108, trap);
	}
}

static void
gf100_gr_ctxctl_debug_unit(struct gf100_gr *gr, u32 base)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	nvkm_error(subdev, "%06x - done %08x\n", base,
		   nvkm_rd32(device, base + 0x400));
	nvkm_error(subdev, "%06x - stat %08x %08x %08x %08x\n", base,
		   nvkm_rd32(device, base + 0x800),
		   nvkm_rd32(device, base + 0x804),
		   nvkm_rd32(device, base + 0x808),
		   nvkm_rd32(device, base + 0x80c));
	nvkm_error(subdev, "%06x - stat %08x %08x %08x %08x\n", base,
		   nvkm_rd32(device, base + 0x810),
		   nvkm_rd32(device, base + 0x814),
		   nvkm_rd32(device, base + 0x818),
		   nvkm_rd32(device, base + 0x81c));
}

void
gf100_gr_ctxctl_debug(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	u32 gpcnr = nvkm_rd32(device, 0x409604) & 0xffff;
	u32 gpc;

	gf100_gr_ctxctl_debug_unit(gr, 0x409000);
	for (gpc = 0; gpc < gpcnr; gpc++)
		gf100_gr_ctxctl_debug_unit(gr, 0x502000 + (gpc * 0x8000));
}

static void
gf100_gr_ctxctl_isr(struct gf100_gr *gr)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x409c18);

	if (stat & 0x00000001) {
		u32 code = nvkm_rd32(device, 0x409814);
		if (code == E_BAD_FWMTHD) {
			u32 class = nvkm_rd32(device, 0x409808);
			u32  addr = nvkm_rd32(device, 0x40980c);
			u32  subc = (addr & 0x00070000) >> 16;
			u32  mthd = (addr & 0x00003ffc);
			u32  data = nvkm_rd32(device, 0x409810);

			nvkm_error(subdev, "FECS MTHD subc %d class %04x "
					   "mthd %04x data %08x\n",
				   subc, class, mthd, data);

			nvkm_wr32(device, 0x409c20, 0x00000001);
			stat &= ~0x00000001;
		} else {
			nvkm_error(subdev, "FECS ucode error %d\n", code);
		}
	}

	if (stat & 0x00080000) {
		nvkm_error(subdev, "FECS watchdog timeout\n");
		gf100_gr_ctxctl_debug(gr);
		nvkm_wr32(device, 0x409c20, 0x00080000);
		stat &= ~0x00080000;
	}

	if (stat) {
		nvkm_error(subdev, "FECS %08x\n", stat);
		gf100_gr_ctxctl_debug(gr);
		nvkm_wr32(device, 0x409c20, stat);
	}
}

static void
gf100_gr_intr(struct nvkm_gr *base)
{
	struct gf100_gr *gr = gf100_gr(base);
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	u64 inst = nvkm_rd32(device, 0x409b00) & 0x0fffffff;
	u32 stat = nvkm_rd32(device, 0x400100);
	u32 addr = nvkm_rd32(device, 0x400704);
	u32 mthd = (addr & 0x00003ffc);
	u32 subc = (addr & 0x00070000) >> 16;
	u32 data = nvkm_rd32(device, 0x400708);
	u32 code = nvkm_rd32(device, 0x400110);
	u32 class;
	const char *name = "unknown";
	int chid = -1;

	chan = nvkm_fifo_chan_inst(device->fifo, (u64)inst << 12, &flags);
	if (chan) {
		name = chan->object.client->name;
		chid = chan->chid;
	}

	if (device->card_type < NV_E0 || subc < 4)
		class = nvkm_rd32(device, 0x404200 + (subc * 4));
	else
		class = 0x0000;

	if (stat & 0x00000001) {
		/*
		 * notifier interrupt, only needed for cyclestats
		 * can be safely ignored
		 */
		nvkm_wr32(device, 0x400100, 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000010) {
		if (!gf100_gr_mthd_sw(device, class, mthd, data)) {
			nvkm_error(subdev, "ILLEGAL_MTHD ch %d [%010llx %s] "
				   "subc %d class %04x mthd %04x data %08x\n",
				   chid, inst << 12, name, subc,
				   class, mthd, data);
		}
		nvkm_wr32(device, 0x400100, 0x00000010);
		stat &= ~0x00000010;
	}

	if (stat & 0x00000020) {
		nvkm_error(subdev, "ILLEGAL_CLASS ch %d [%010llx %s] "
			   "subc %d class %04x mthd %04x data %08x\n",
			   chid, inst << 12, name, subc, class, mthd, data);
		nvkm_wr32(device, 0x400100, 0x00000020);
		stat &= ~0x00000020;
	}

	if (stat & 0x00100000) {
		const struct nvkm_enum *en =
			nvkm_enum_find(nv50_data_error_names, code);
		nvkm_error(subdev, "DATA_ERROR %08x [%s] ch %d [%010llx %s] "
				   "subc %d class %04x mthd %04x data %08x\n",
			   code, en ? en->name : "", chid, inst << 12,
			   name, subc, class, mthd, data);
		nvkm_wr32(device, 0x400100, 0x00100000);
		stat &= ~0x00100000;
	}

	if (stat & 0x00200000) {
		nvkm_error(subdev, "TRAP ch %d [%010llx %s]\n",
			   chid, inst << 12, name);
		gf100_gr_trap_intr(gr);
		nvkm_wr32(device, 0x400100, 0x00200000);
		stat &= ~0x00200000;
	}

	if (stat & 0x00080000) {
		gf100_gr_ctxctl_isr(gr);
		nvkm_wr32(device, 0x400100, 0x00080000);
		stat &= ~0x00080000;
	}

	if (stat) {
		nvkm_error(subdev, "intr %08x\n", stat);
		nvkm_wr32(device, 0x400100, stat);
	}

	nvkm_wr32(device, 0x400500, 0x00010001);
	nvkm_fifo_chan_put(device->fifo, flags, &chan);
}

void
gf100_gr_init_fw(struct gf100_gr *gr, u32 fuc_base,
		 struct gf100_gr_fuc *code, struct gf100_gr_fuc *data)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int i;

	nvkm_wr32(device, fuc_base + 0x01c0, 0x01000000);
	for (i = 0; i < data->size / 4; i++)
		nvkm_wr32(device, fuc_base + 0x01c4, data->data[i]);

	nvkm_wr32(device, fuc_base + 0x0180, 0x01000000);
	for (i = 0; i < code->size / 4; i++) {
		if ((i & 0x3f) == 0)
			nvkm_wr32(device, fuc_base + 0x0188, i >> 6);
		nvkm_wr32(device, fuc_base + 0x0184, code->data[i]);
	}

	/* code must be padded to 0x40 words */
	for (; i & 0x3f; i++)
		nvkm_wr32(device, fuc_base + 0x0184, 0);
}

static void
gf100_gr_init_csdata(struct gf100_gr *gr,
		     const struct gf100_gr_pack *pack,
		     u32 falcon, u32 starstar, u32 base)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const struct gf100_gr_pack *iter;
	const struct gf100_gr_init *init;
	u32 addr = ~0, prev = ~0, xfer = 0;
	u32 star, temp;

	nvkm_wr32(device, falcon + 0x01c0, 0x02000000 + starstar);
	star = nvkm_rd32(device, falcon + 0x01c4);
	temp = nvkm_rd32(device, falcon + 0x01c4);
	if (temp > star)
		star = temp;
	nvkm_wr32(device, falcon + 0x01c0, 0x01000000 + star);

	pack_for_each_init(init, iter, pack) {
		u32 head = init->addr - base;
		u32 tail = head + init->count * init->pitch;
		while (head < tail) {
			if (head != prev + 4 || xfer >= 32) {
				if (xfer) {
					u32 data = ((--xfer << 26) | addr);
					nvkm_wr32(device, falcon + 0x01c4, data);
					star += 4;
				}
				addr = head;
				xfer = 0;
			}
			prev = head;
			xfer = xfer + 1;
			head = head + init->pitch;
		}
	}

	nvkm_wr32(device, falcon + 0x01c4, (--xfer << 26) | addr);
	nvkm_wr32(device, falcon + 0x01c0, 0x01000004 + starstar);
	nvkm_wr32(device, falcon + 0x01c4, star + 4);
}

int
gf100_gr_init_ctxctl(struct gf100_gr *gr)
{
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_secboot *sb = device->secboot;
	int i;

	if (gr->firmware) {
		/* load fuc microcode */
		nvkm_mc_unk260(device->mc, 0);

		/* securely-managed falcons must be reset using secure boot */
		if (nvkm_secboot_is_managed(sb, NVKM_SECBOOT_FALCON_FECS))
			nvkm_secboot_reset(sb, NVKM_SECBOOT_FALCON_FECS);
		else
			gf100_gr_init_fw(gr, 0x409000, &gr->fuc409c,
					 &gr->fuc409d);
		if (nvkm_secboot_is_managed(sb, NVKM_SECBOOT_FALCON_GPCCS))
			nvkm_secboot_reset(sb, NVKM_SECBOOT_FALCON_GPCCS);
		else
			gf100_gr_init_fw(gr, 0x41a000, &gr->fuc41ac,
					 &gr->fuc41ad);

		nvkm_mc_unk260(device->mc, 1);

		/* start both of them running */
		nvkm_wr32(device, 0x409840, 0xffffffff);
		nvkm_wr32(device, 0x41a10c, 0x00000000);
		nvkm_wr32(device, 0x40910c, 0x00000000);

		if (nvkm_secboot_is_managed(sb, NVKM_SECBOOT_FALCON_GPCCS))
			nvkm_secboot_start(sb, NVKM_SECBOOT_FALCON_GPCCS);
		else
			nvkm_wr32(device, 0x41a100, 0x00000002);
		if (nvkm_secboot_is_managed(sb, NVKM_SECBOOT_FALCON_FECS))
			nvkm_secboot_start(sb, NVKM_SECBOOT_FALCON_FECS);
		else
			nvkm_wr32(device, 0x409100, 0x00000002);
		if (nvkm_msec(device, 2000,
			if (nvkm_rd32(device, 0x409800) & 0x00000001)
				break;
		) < 0)
			return -EBUSY;

		nvkm_wr32(device, 0x409840, 0xffffffff);
		nvkm_wr32(device, 0x409500, 0x7fffffff);
		nvkm_wr32(device, 0x409504, 0x00000021);

		nvkm_wr32(device, 0x409840, 0xffffffff);
		nvkm_wr32(device, 0x409500, 0x00000000);
		nvkm_wr32(device, 0x409504, 0x00000010);
		if (nvkm_msec(device, 2000,
			if ((gr->size = nvkm_rd32(device, 0x409800)))
				break;
		) < 0)
			return -EBUSY;

		nvkm_wr32(device, 0x409840, 0xffffffff);
		nvkm_wr32(device, 0x409500, 0x00000000);
		nvkm_wr32(device, 0x409504, 0x00000016);
		if (nvkm_msec(device, 2000,
			if (nvkm_rd32(device, 0x409800))
				break;
		) < 0)
			return -EBUSY;

		nvkm_wr32(device, 0x409840, 0xffffffff);
		nvkm_wr32(device, 0x409500, 0x00000000);
		nvkm_wr32(device, 0x409504, 0x00000025);
		if (nvkm_msec(device, 2000,
			if (nvkm_rd32(device, 0x409800))
				break;
		) < 0)
			return -EBUSY;

		if (device->chipset >= 0xe0) {
			nvkm_wr32(device, 0x409800, 0x00000000);
			nvkm_wr32(device, 0x409500, 0x00000001);
			nvkm_wr32(device, 0x409504, 0x00000030);
			if (nvkm_msec(device, 2000,
				if (nvkm_rd32(device, 0x409800))
					break;
			) < 0)
				return -EBUSY;

			nvkm_wr32(device, 0x409810, 0xb00095c8);
			nvkm_wr32(device, 0x409800, 0x00000000);
			nvkm_wr32(device, 0x409500, 0x00000001);
			nvkm_wr32(device, 0x409504, 0x00000031);
			if (nvkm_msec(device, 2000,
				if (nvkm_rd32(device, 0x409800))
					break;
			) < 0)
				return -EBUSY;

			nvkm_wr32(device, 0x409810, 0x00080420);
			nvkm_wr32(device, 0x409800, 0x00000000);
			nvkm_wr32(device, 0x409500, 0x00000001);
			nvkm_wr32(device, 0x409504, 0x00000032);
			if (nvkm_msec(device, 2000,
				if (nvkm_rd32(device, 0x409800))
					break;
			) < 0)
				return -EBUSY;

			nvkm_wr32(device, 0x409614, 0x00000070);
			nvkm_wr32(device, 0x409614, 0x00000770);
			nvkm_wr32(device, 0x40802c, 0x00000001);
		}

		if (gr->data == NULL) {
			int ret = gf100_grctx_generate(gr);
			if (ret) {
				nvkm_error(subdev, "failed to construct context\n");
				return ret;
			}
		}

		return 0;
	} else
	if (!gr->func->fecs.ucode) {
		return -ENOSYS;
	}

	/* load HUB microcode */
	nvkm_mc_unk260(device->mc, 0);
	nvkm_wr32(device, 0x4091c0, 0x01000000);
	for (i = 0; i < gr->func->fecs.ucode->data.size / 4; i++)
		nvkm_wr32(device, 0x4091c4, gr->func->fecs.ucode->data.data[i]);

	nvkm_wr32(device, 0x409180, 0x01000000);
	for (i = 0; i < gr->func->fecs.ucode->code.size / 4; i++) {
		if ((i & 0x3f) == 0)
			nvkm_wr32(device, 0x409188, i >> 6);
		nvkm_wr32(device, 0x409184, gr->func->fecs.ucode->code.data[i]);
	}

	/* load GPC microcode */
	nvkm_wr32(device, 0x41a1c0, 0x01000000);
	for (i = 0; i < gr->func->gpccs.ucode->data.size / 4; i++)
		nvkm_wr32(device, 0x41a1c4, gr->func->gpccs.ucode->data.data[i]);

	nvkm_wr32(device, 0x41a180, 0x01000000);
	for (i = 0; i < gr->func->gpccs.ucode->code.size / 4; i++) {
		if ((i & 0x3f) == 0)
			nvkm_wr32(device, 0x41a188, i >> 6);
		nvkm_wr32(device, 0x41a184, gr->func->gpccs.ucode->code.data[i]);
	}
	nvkm_mc_unk260(device->mc, 1);

	/* load register lists */
	gf100_gr_init_csdata(gr, grctx->hub, 0x409000, 0x000, 0x000000);
	gf100_gr_init_csdata(gr, grctx->gpc, 0x41a000, 0x000, 0x418000);
	gf100_gr_init_csdata(gr, grctx->tpc, 0x41a000, 0x004, 0x419800);
	gf100_gr_init_csdata(gr, grctx->ppc, 0x41a000, 0x008, 0x41be00);

	/* start HUB ucode running, it'll init the GPCs */
	nvkm_wr32(device, 0x40910c, 0x00000000);
	nvkm_wr32(device, 0x409100, 0x00000002);
	if (nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x409800) & 0x80000000)
			break;
	) < 0) {
		gf100_gr_ctxctl_debug(gr);
		return -EBUSY;
	}

	gr->size = nvkm_rd32(device, 0x409804);
	if (gr->data == NULL) {
		int ret = gf100_grctx_generate(gr);
		if (ret) {
			nvkm_error(subdev, "failed to construct context\n");
			return ret;
		}
	}

	return 0;
}

static int
gf100_gr_oneinit(struct nvkm_gr *base)
{
	struct gf100_gr *gr = gf100_gr(base);
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int i, j;

	nvkm_pmu_pgob(device->pmu, false);

	gr->rop_nr = gr->func->rops(gr);
	gr->gpc_nr = nvkm_rd32(device, 0x409604) & 0x0000001f;
	for (i = 0; i < gr->gpc_nr; i++) {
		gr->tpc_nr[i]  = nvkm_rd32(device, GPC_UNIT(i, 0x2608));
		gr->tpc_total += gr->tpc_nr[i];
		gr->ppc_nr[i]  = gr->func->ppc_nr;
		for (j = 0; j < gr->ppc_nr[i]; j++) {
			u8 mask = nvkm_rd32(device, GPC_UNIT(i, 0x0c30 + (j * 4)));
			if (mask)
				gr->ppc_mask[i] |= (1 << j);
			gr->ppc_tpc_nr[i][j] = hweight8(mask);
		}
	}

	/*XXX: these need figuring out... though it might not even matter */
	switch (device->chipset) {
	case 0xc0:
		if (gr->tpc_total == 11) { /* 465, 3/4/4/0, 4 */
			gr->screen_tile_row_offset = 0x07;
		} else
		if (gr->tpc_total == 14) { /* 470, 3/3/4/4, 5 */
			gr->screen_tile_row_offset = 0x05;
		} else
		if (gr->tpc_total == 15) { /* 480, 3/4/4/4, 6 */
			gr->screen_tile_row_offset = 0x06;
		}
		break;
	case 0xc3: /* 450, 4/0/0/0, 2 */
		gr->screen_tile_row_offset = 0x03;
		break;
	case 0xc4: /* 460, 3/4/0/0, 4 */
		gr->screen_tile_row_offset = 0x01;
		break;
	case 0xc1: /* 2/0/0/0, 1 */
		gr->screen_tile_row_offset = 0x01;
		break;
	case 0xc8: /* 4/4/3/4, 5 */
		gr->screen_tile_row_offset = 0x06;
		break;
	case 0xce: /* 4/4/0/0, 4 */
		gr->screen_tile_row_offset = 0x03;
		break;
	case 0xcf: /* 4/0/0/0, 3 */
		gr->screen_tile_row_offset = 0x03;
		break;
	case 0xd7:
	case 0xd9: /* 1/0/0/0, 1 */
	case 0xea: /* gk20a */
	case 0x12b: /* gm20b */
		gr->screen_tile_row_offset = 0x01;
		break;
	}

	return 0;
}

int
gf100_gr_init_(struct nvkm_gr *base)
{
	struct gf100_gr *gr = gf100_gr(base);
	nvkm_pmu_pgob(gr->base.engine.subdev.device->pmu, false);
	return gr->func->init(gr);
}

void
gf100_gr_dtor_fw(struct gf100_gr_fuc *fuc)
{
	kfree(fuc->data);
	fuc->data = NULL;
}

static void
gf100_gr_dtor_init(struct gf100_gr_pack *pack)
{
	vfree(pack);
}

void *
gf100_gr_dtor(struct nvkm_gr *base)
{
	struct gf100_gr *gr = gf100_gr(base);

	if (gr->func->dtor)
		gr->func->dtor(gr);
	kfree(gr->data);

	gf100_gr_dtor_fw(&gr->fuc409c);
	gf100_gr_dtor_fw(&gr->fuc409d);
	gf100_gr_dtor_fw(&gr->fuc41ac);
	gf100_gr_dtor_fw(&gr->fuc41ad);

	gf100_gr_dtor_init(gr->fuc_bundle);
	gf100_gr_dtor_init(gr->fuc_method);
	gf100_gr_dtor_init(gr->fuc_sw_ctx);
	gf100_gr_dtor_init(gr->fuc_sw_nonctx);

	return gr;
}

static const struct nvkm_gr_func
gf100_gr_ = {
	.dtor = gf100_gr_dtor,
	.oneinit = gf100_gr_oneinit,
	.init = gf100_gr_init_,
	.intr = gf100_gr_intr,
	.units = gf100_gr_units,
	.chan_new = gf100_gr_chan_new,
	.object_get = gf100_gr_object_get,
};

int
gf100_gr_ctor_fw(struct gf100_gr *gr, const char *fwname,
		 struct gf100_gr_fuc *fuc)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	const struct firmware *fw;
	int ret;

	ret = nvkm_firmware_get(device, fwname, &fw);
	if (ret) {
		nvkm_error(subdev, "failed to load %s\n", fwname);
		return ret;
	}

	fuc->size = fw->size;
	fuc->data = kmemdup(fw->data, fuc->size, GFP_KERNEL);
	nvkm_firmware_put(fw);
	return (fuc->data != NULL) ? 0 : -ENOMEM;
}

int
gf100_gr_ctor(const struct gf100_gr_func *func, struct nvkm_device *device,
	      int index, struct gf100_gr *gr)
{
	int ret;

	gr->func = func;
	gr->firmware = nvkm_boolopt(device->cfgopt, "NvGrUseFW",
				    func->fecs.ucode == NULL);

	ret = nvkm_gr_ctor(&gf100_gr_, device, index,
			   gr->firmware || func->fecs.ucode != NULL,
			   &gr->base);
	if (ret)
		return ret;

	return 0;
}

int
gf100_gr_new_(const struct gf100_gr_func *func, struct nvkm_device *device,
	      int index, struct nvkm_gr **pgr)
{
	struct gf100_gr *gr;
	int ret;

	if (!(gr = kzalloc(sizeof(*gr), GFP_KERNEL)))
		return -ENOMEM;
	*pgr = &gr->base;

	ret = gf100_gr_ctor(func, device, index, gr);
	if (ret)
		return ret;

	if (gr->firmware) {
		if (gf100_gr_ctor_fw(gr, "fecs_inst", &gr->fuc409c) ||
		    gf100_gr_ctor_fw(gr, "fecs_data", &gr->fuc409d) ||
		    gf100_gr_ctor_fw(gr, "gpccs_inst", &gr->fuc41ac) ||
		    gf100_gr_ctor_fw(gr, "gpccs_data", &gr->fuc41ad))
			return -ENODEV;
	}

	return 0;
}

int
gf100_gr_init(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fb *fb = device->fb;
	const u32 magicgpc918 = DIV_ROUND_UP(0x00800000, gr->tpc_total);
	u32 data[TPC_MAX / 8] = {};
	u8  tpcnr[GPC_MAX];
	int gpc, tpc, rop;
	int i;

	nvkm_wr32(device, GPC_BCAST(0x0880), 0x00000000);
	nvkm_wr32(device, GPC_BCAST(0x08a4), 0x00000000);
	nvkm_wr32(device, GPC_BCAST(0x0888), 0x00000000);
	nvkm_wr32(device, GPC_BCAST(0x088c), 0x00000000);
	nvkm_wr32(device, GPC_BCAST(0x0890), 0x00000000);
	nvkm_wr32(device, GPC_BCAST(0x0894), 0x00000000);
	nvkm_wr32(device, GPC_BCAST(0x08b4), nvkm_memory_addr(fb->mmu_wr) >> 8);
	nvkm_wr32(device, GPC_BCAST(0x08b8), nvkm_memory_addr(fb->mmu_rd) >> 8);

	gf100_gr_mmio(gr, gr->func->mmio);

	nvkm_mask(device, TPC_UNIT(0, 0, 0x05c), 0x00000001, 0x00000001);

	memcpy(tpcnr, gr->tpc_nr, sizeof(gr->tpc_nr));
	for (i = 0, gpc = -1; i < gr->tpc_total; i++) {
		do {
			gpc = (gpc + 1) % gr->gpc_nr;
		} while (!tpcnr[gpc]);
		tpc = gr->tpc_nr[gpc] - tpcnr[gpc]--;

		data[i / 8] |= tpc << ((i % 8) * 4);
	}

	nvkm_wr32(device, GPC_BCAST(0x0980), data[0]);
	nvkm_wr32(device, GPC_BCAST(0x0984), data[1]);
	nvkm_wr32(device, GPC_BCAST(0x0988), data[2]);
	nvkm_wr32(device, GPC_BCAST(0x098c), data[3]);

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0914),
			  gr->screen_tile_row_offset << 8 | gr->tpc_nr[gpc]);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0910), 0x00040000 |
							 gr->tpc_total);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0918), magicgpc918);
	}

	if (device->chipset != 0xd7)
		nvkm_wr32(device, GPC_BCAST(0x1bd4), magicgpc918);
	else
		nvkm_wr32(device, GPC_BCAST(0x3fd4), magicgpc918);

	nvkm_wr32(device, GPC_BCAST(0x08ac), nvkm_rd32(device, 0x100800));

	nvkm_wr32(device, 0x400500, 0x00010001);

	nvkm_wr32(device, 0x400100, 0xffffffff);
	nvkm_wr32(device, 0x40013c, 0xffffffff);

	nvkm_wr32(device, 0x409c24, 0x000f0000);
	nvkm_wr32(device, 0x404000, 0xc0000000);
	nvkm_wr32(device, 0x404600, 0xc0000000);
	nvkm_wr32(device, 0x408030, 0xc0000000);
	nvkm_wr32(device, 0x40601c, 0xc0000000);
	nvkm_wr32(device, 0x404490, 0xc0000000);
	nvkm_wr32(device, 0x406018, 0xc0000000);
	nvkm_wr32(device, 0x405840, 0xc0000000);
	nvkm_wr32(device, 0x405844, 0x00ffffff);
	nvkm_mask(device, 0x419cc0, 0x00000008, 0x00000008);
	nvkm_mask(device, 0x419eb4, 0x00001000, 0x00001000);

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0420), 0xc0000000);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		for (tpc = 0; tpc < gr->tpc_nr[gpc]; tpc++) {
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x508), 0xffffffff);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x50c), 0xffffffff);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x224), 0xc0000000);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x48c), 0xc0000000);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x084), 0xc0000000);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x644), 0x001ffffe);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x64c), 0x0000000f);
		}
		nvkm_wr32(device, GPC_UNIT(gpc, 0x2c90), 0xffffffff);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x2c94), 0xffffffff);
	}

	for (rop = 0; rop < gr->rop_nr; rop++) {
		nvkm_wr32(device, ROP_UNIT(rop, 0x144), 0xc0000000);
		nvkm_wr32(device, ROP_UNIT(rop, 0x070), 0xc0000000);
		nvkm_wr32(device, ROP_UNIT(rop, 0x204), 0xffffffff);
		nvkm_wr32(device, ROP_UNIT(rop, 0x208), 0xffffffff);
	}

	nvkm_wr32(device, 0x400108, 0xffffffff);
	nvkm_wr32(device, 0x400138, 0xffffffff);
	nvkm_wr32(device, 0x400118, 0xffffffff);
	nvkm_wr32(device, 0x400130, 0xffffffff);
	nvkm_wr32(device, 0x40011c, 0xffffffff);
	nvkm_wr32(device, 0x400134, 0xffffffff);

	nvkm_wr32(device, 0x400054, 0x34ce3464);

	gf100_gr_zbc_init(gr);

	return gf100_gr_init_ctxctl(gr);
}

#include "fuc/hubgf100.fuc3.h"

struct gf100_gr_ucode
gf100_gr_fecs_ucode = {
	.code.data = gf100_grhub_code,
	.code.size = sizeof(gf100_grhub_code),
	.data.data = gf100_grhub_data,
	.data.size = sizeof(gf100_grhub_data),
};

#include "fuc/gpcgf100.fuc3.h"

struct gf100_gr_ucode
gf100_gr_gpccs_ucode = {
	.code.data = gf100_grgpc_code,
	.code.size = sizeof(gf100_grgpc_code),
	.data.data = gf100_grgpc_data,
	.data.size = sizeof(gf100_grgpc_data),
};

static const struct gf100_gr_func
gf100_gr = {
	.init = gf100_gr_init,
	.mmio = gf100_gr_pack_mmio,
	.fecs.ucode = &gf100_gr_fecs_ucode,
	.gpccs.ucode = &gf100_gr_gpccs_ucode,
	.rops = gf100_gr_rops,
	.grctx = &gf100_grctx,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, FERMI_MEMORY_TO_MEMORY_FORMAT_A },
		{ -1, -1, FERMI_A, &gf100_fermi },
		{ -1, -1, FERMI_COMPUTE_A },
		{}
	}
};

int
gf100_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(&gf100_gr, device, index, pgr);
}
