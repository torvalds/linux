/*
 * Copyright 2013 Red Hat Inc.
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

#include <core/client.h>
#include <core/device.h>
#include <core/option.h>

#include <nvif/class.h>
#include <nvif/ioctl.h>
#include <nvif/unpack.h>

#define QUAD_MASK 0x0f
#define QUAD_FREE 0x01

static struct nvkm_perfsig *
nvkm_perfsig_find_(struct nvkm_perfdom *dom, const char *name, u32 size)
{
	char path[64];
	int i;

	if (name[0] != '/') {
		for (i = 0; i < dom->signal_nr; i++) {
			if ( dom->signal[i].name &&
			    !strncmp(name, dom->signal[i].name, size))
				return &dom->signal[i];
		}
	} else {
		for (i = 0; i < dom->signal_nr; i++) {
			snprintf(path, sizeof(path), "/%s/%02x", dom->name, i);
			if (!strncmp(name, path, size))
				return &dom->signal[i];
		}
	}

	return NULL;
}

struct nvkm_perfsig *
nvkm_perfsig_find(struct nvkm_pm *ppm, const char *name, u32 size,
		  struct nvkm_perfdom **pdom)
{
	struct nvkm_perfdom *dom = *pdom;
	struct nvkm_perfsig *sig;

	if (dom == NULL) {
		list_for_each_entry(dom, &ppm->domains, head) {
			sig = nvkm_perfsig_find_(dom, name, size);
			if (sig) {
				*pdom = dom;
				return sig;
			}
		}

		return NULL;
	}

	return nvkm_perfsig_find_(dom, name, size);
}

struct nvkm_perfctr *
nvkm_perfsig_wrap(struct nvkm_pm *ppm, const char *name,
		  struct nvkm_perfdom **pdom)
{
	struct nvkm_perfsig *sig;
	struct nvkm_perfctr *ctr;

	sig = nvkm_perfsig_find(ppm, name, strlen(name), pdom);
	if (!sig)
		return NULL;

	ctr = kzalloc(sizeof(*ctr), GFP_KERNEL);
	if (ctr) {
		ctr->signal[0] = sig;
		ctr->logic_op = 0xaaaa;
	}

	return ctr;
}

/*******************************************************************************
 * Perfmon object classes
 ******************************************************************************/
static int
nvkm_perfctr_query(struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_perfctr_query_v0 v0;
	} *args = data;
	struct nvkm_device *device = nv_device(object);
	struct nvkm_pm *ppm = (void *)object->engine;
	struct nvkm_perfdom *dom = NULL, *chk;
	const bool all = nvkm_boolopt(device->cfgopt, "NvPmShowAll", false);
	const bool raw = nvkm_boolopt(device->cfgopt, "NvPmUnnamed", all);
	const char *name;
	int tmp = 0, di, si;
	int ret;

	nv_ioctl(object, "perfctr query size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "perfctr query vers %d iter %08x\n",
			 args->v0.version, args->v0.iter);
		di = (args->v0.iter & 0xff000000) >> 24;
		si = (args->v0.iter & 0x00ffffff) - 1;
	} else
		return ret;

	list_for_each_entry(chk, &ppm->domains, head) {
		if (tmp++ == di) {
			dom = chk;
			break;
		}
	}

	if (dom == NULL || si >= (int)dom->signal_nr)
		return -EINVAL;

	if (si >= 0) {
		if (raw || !(name = dom->signal[si].name)) {
			snprintf(args->v0.name, sizeof(args->v0.name),
				 "/%s/%02x", dom->name, si);
		} else {
			strncpy(args->v0.name, name, sizeof(args->v0.name));
		}
	}

	do {
		while (++si < dom->signal_nr) {
			if (all || dom->signal[si].name) {
				args->v0.iter = (di << 24) | ++si;
				return 0;
			}
		}
		si = -1;
		di = di + 1;
		dom = list_entry(dom->head.next, typeof(*dom), head);
	} while (&dom->head != &ppm->domains);

	args->v0.iter = 0xffffffff;
	return 0;
}

static int
nvkm_perfctr_sample(struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_perfctr_sample none;
	} *args = data;
	struct nvkm_pm *ppm = (void *)object->engine;
	struct nvkm_perfctr *ctr, *tmp;
	struct nvkm_perfdom *dom;
	int ret;

	nv_ioctl(object, "perfctr sample size %d\n", size);
	if (nvif_unvers(args->none)) {
		nv_ioctl(object, "perfctr sample\n");
	} else
		return ret;
	ppm->sequence++;

	list_for_each_entry(dom, &ppm->domains, head) {
		/* sample previous batch of counters */
		if (dom->quad != QUAD_MASK) {
			dom->func->next(ppm, dom);
			tmp = NULL;
			while (!list_empty(&dom->list)) {
				ctr = list_first_entry(&dom->list,
						       typeof(*ctr), head);
				if (ctr->slot < 0) break;
				if ( tmp && tmp == ctr) break;
				if (!tmp) tmp = ctr;
				dom->func->read(ppm, dom, ctr);
				ctr->slot  = -1;
				list_move_tail(&ctr->head, &dom->list);
			}
		}

		dom->quad = QUAD_MASK;

		/* setup next batch of counters for sampling */
		list_for_each_entry(ctr, &dom->list, head) {
			ctr->slot = ffs(dom->quad) - 1;
			if (ctr->slot < 0)
				break;
			dom->quad &= ~(QUAD_FREE << ctr->slot);
			dom->func->init(ppm, dom, ctr);
		}

		if (dom->quad != QUAD_MASK)
			dom->func->next(ppm, dom);
	}

	return 0;
}

static int
nvkm_perfctr_read(struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_perfctr_read_v0 v0;
	} *args = data;
	struct nvkm_perfctr *ctr = (void *)object;
	int ret;

	nv_ioctl(object, "perfctr read size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "perfctr read vers %d\n", args->v0.version);
	} else
		return ret;

	if (!ctr->clk)
		return -EAGAIN;

	args->v0.clk = ctr->clk;
	args->v0.ctr = ctr->ctr;
	return 0;
}

static int
nvkm_perfctr_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	switch (mthd) {
	case NVIF_PERFCTR_V0_QUERY:
		return nvkm_perfctr_query(object, data, size);
	case NVIF_PERFCTR_V0_SAMPLE:
		return nvkm_perfctr_sample(object, data, size);
	case NVIF_PERFCTR_V0_READ:
		return nvkm_perfctr_read(object, data, size);
	default:
		break;
	}
	return -EINVAL;
}

static void
nvkm_perfctr_dtor(struct nvkm_object *object)
{
	struct nvkm_perfctr *ctr = (void *)object;
	if (ctr->head.next)
		list_del(&ctr->head);
	nvkm_object_destroy(&ctr->base);
}

static int
nvkm_perfctr_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	union {
		struct nvif_perfctr_v0 v0;
	} *args = data;
	struct nvkm_pm *ppm = (void *)engine;
	struct nvkm_perfdom *dom = NULL;
	struct nvkm_perfsig *sig[4] = {};
	struct nvkm_perfctr *ctr;
	int ret, i;

	nv_ioctl(parent, "create perfctr size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create perfctr vers %d logic_op %04x\n",
			 args->v0.version, args->v0.logic_op);
	} else
		return ret;

	for (i = 0; i < ARRAY_SIZE(args->v0.name) && args->v0.name[i][0]; i++) {
		sig[i] = nvkm_perfsig_find(ppm, args->v0.name[i],
					   strnlen(args->v0.name[i],
						   sizeof(args->v0.name[i])),
					   &dom);
		if (!sig[i])
			return -EINVAL;
	}

	ret = nvkm_object_create(parent, engine, oclass, 0, &ctr);
	*pobject = nv_object(ctr);
	if (ret)
		return ret;

	ctr->slot = -1;
	ctr->logic_op = args->v0.logic_op;
	ctr->signal[0] = sig[0];
	ctr->signal[1] = sig[1];
	ctr->signal[2] = sig[2];
	ctr->signal[3] = sig[3];
	if (dom)
		list_add_tail(&ctr->head, &dom->list);
	return 0;
}

static struct nvkm_ofuncs
nvkm_perfctr_ofuncs = {
	.ctor = nvkm_perfctr_ctor,
	.dtor = nvkm_perfctr_dtor,
	.init = nvkm_object_init,
	.fini = nvkm_object_fini,
	.mthd = nvkm_perfctr_mthd,
};

struct nvkm_oclass
nvkm_pm_sclass[] = {
	{ .handle = NVIF_IOCTL_NEW_V0_PERFCTR,
	  .ofuncs = &nvkm_perfctr_ofuncs,
	},
	{},
};

/*******************************************************************************
 * PPM context
 ******************************************************************************/
static void
nvkm_perfctx_dtor(struct nvkm_object *object)
{
	struct nvkm_pm *ppm = (void *)object->engine;
	struct nvkm_perfctx *ctx = (void *)object;

	mutex_lock(&nv_subdev(ppm)->mutex);
	nvkm_engctx_destroy(&ctx->base);
	if (ppm->context == ctx)
		ppm->context = NULL;
	mutex_unlock(&nv_subdev(ppm)->mutex);
}

static int
nvkm_perfctx_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nvkm_pm *ppm = (void *)engine;
	struct nvkm_perfctx *ctx;
	int ret;

	ret = nvkm_engctx_create(parent, engine, oclass, NULL, 0, 0, 0, &ctx);
	*pobject = nv_object(ctx);
	if (ret)
		return ret;

	mutex_lock(&nv_subdev(ppm)->mutex);
	if (ppm->context == NULL)
		ppm->context = ctx;
	if (ctx != ppm->context)
		ret = -EBUSY;
	mutex_unlock(&nv_subdev(ppm)->mutex);

	return ret;
}

struct nvkm_oclass
nvkm_pm_cclass = {
	.handle = NV_ENGCTX(PM, 0x00),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nvkm_perfctx_ctor,
		.dtor = nvkm_perfctx_dtor,
		.init = _nvkm_engctx_init,
		.fini = _nvkm_engctx_fini,
	},
};

/*******************************************************************************
 * PPM engine/subdev functions
 ******************************************************************************/
int
nvkm_perfdom_new(struct nvkm_pm *ppm, const char *name, u32 mask,
		 u32 base, u32 size_unit, u32 size_domain,
		 const struct nvkm_specdom *spec)
{
	const struct nvkm_specdom *sdom;
	const struct nvkm_specsig *ssig;
	struct nvkm_perfdom *dom;
	int i;

	for (i = 0; i == 0 || mask; i++) {
		u32 addr = base + (i * size_unit);
		if (i && !(mask & (1 << i)))
			continue;

		sdom = spec;
		while (sdom->signal_nr) {
			dom = kzalloc(sizeof(*dom) + sdom->signal_nr *
				      sizeof(*dom->signal), GFP_KERNEL);
			if (!dom)
				return -ENOMEM;

			if (mask) {
				snprintf(dom->name, sizeof(dom->name),
					 "%s/%02x/%02x", name, i,
					 (int)(sdom - spec));
			} else {
				snprintf(dom->name, sizeof(dom->name),
					 "%s/%02x", name, (int)(sdom - spec));
			}

			list_add_tail(&dom->head, &ppm->domains);
			INIT_LIST_HEAD(&dom->list);
			dom->func = sdom->func;
			dom->addr = addr;
			dom->quad = QUAD_MASK;
			dom->signal_nr = sdom->signal_nr;

			ssig = (sdom++)->signal;
			while (ssig->name) {
				dom->signal[ssig->signal].name = ssig->name;
				ssig++;
			}

			addr += size_domain;
		}

		mask &= ~(1 << i);
	}

	return 0;
}

int
_nvkm_pm_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_pm *ppm = (void *)object;
	return nvkm_engine_fini(&ppm->base, suspend);
}

int
_nvkm_pm_init(struct nvkm_object *object)
{
	struct nvkm_pm *ppm = (void *)object;
	return nvkm_engine_init(&ppm->base);
}

void
_nvkm_pm_dtor(struct nvkm_object *object)
{
	struct nvkm_pm *ppm = (void *)object;
	struct nvkm_perfdom *dom, *tmp;

	list_for_each_entry_safe(dom, tmp, &ppm->domains, head) {
		list_del(&dom->head);
		kfree(dom);
	}

	nvkm_engine_destroy(&ppm->base);
}

int
nvkm_pm_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_pm *ppm;
	int ret;

	ret = nvkm_engine_create_(parent, engine, oclass, true, "PPM",
				  "pm", length, pobject);
	ppm = *pobject;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&ppm->domains);
	return 0;
}
