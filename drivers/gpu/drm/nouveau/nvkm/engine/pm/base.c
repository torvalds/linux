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
#include <core/option.h>

#include <nvif/class.h>
#include <nvif/ioctl.h>
#include <nvif/unpack.h>

static u8
nvkm_pm_count_perfdom(struct nvkm_pm *pm)
{
	struct nvkm_perfdom *dom;
	u8 domain_nr = 0;

	list_for_each_entry(dom, &pm->domains, head)
		domain_nr++;
	return domain_nr;
}

static u16
nvkm_perfdom_count_perfsig(struct nvkm_perfdom *dom)
{
	u16 signal_nr = 0;
	int i;

	if (dom) {
		for (i = 0; i < dom->signal_nr; i++) {
			if (dom->signal[i].name)
				signal_nr++;
		}
	}
	return signal_nr;
}

static struct nvkm_perfdom *
nvkm_perfdom_find(struct nvkm_pm *pm, int di)
{
	struct nvkm_perfdom *dom;
	int tmp = 0;

	list_for_each_entry(dom, &pm->domains, head) {
		if (tmp++ == di)
			return dom;
	}
	return NULL;
}

struct nvkm_perfsig *
nvkm_perfsig_find(struct nvkm_pm *pm, u8 di, u8 si, struct nvkm_perfdom **pdom)
{
	struct nvkm_perfdom *dom = *pdom;

	if (dom == NULL) {
		dom = nvkm_perfdom_find(pm, di);
		if (dom == NULL)
			return NULL;
		*pdom = dom;
	}

	if (!dom->signal[si].name)
		return NULL;
	return &dom->signal[si];
}

static u8
nvkm_perfsig_count_perfsrc(struct nvkm_perfsig *sig)
{
	u8 source_nr = 0, i;

	for (i = 0; i < ARRAY_SIZE(sig->source); i++) {
		if (sig->source[i])
			source_nr++;
	}
	return source_nr;
}

static struct nvkm_perfsrc *
nvkm_perfsrc_find(struct nvkm_pm *pm, struct nvkm_perfsig *sig, int si)
{
	struct nvkm_perfsrc *src;
	bool found = false;
	int tmp = 1; /* Sources ID start from 1 */
	u8 i;

	for (i = 0; i < ARRAY_SIZE(sig->source) && sig->source[i]; i++) {
		if (sig->source[i] == si) {
			found = true;
			break;
		}
	}

	if (found) {
		list_for_each_entry(src, &pm->sources, head) {
			if (tmp++ == si)
				return src;
		}
	}

	return NULL;
}

static int
nvkm_perfsrc_enable(struct nvkm_pm *pm, struct nvkm_perfctr *ctr)
{
	struct nvkm_subdev *subdev = &pm->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_perfdom *dom = NULL;
	struct nvkm_perfsig *sig;
	struct nvkm_perfsrc *src;
	u32 mask, value;
	int i, j;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 8 && ctr->source[i][j]; j++) {
			sig = nvkm_perfsig_find(pm, ctr->domain,
						ctr->signal[i], &dom);
			if (!sig)
				return -EINVAL;

			src = nvkm_perfsrc_find(pm, sig, ctr->source[i][j]);
			if (!src)
				return -EINVAL;

			/* set enable bit if needed */
			mask = value = 0x00000000;
			if (src->enable)
				mask = value = 0x80000000;
			mask  |= (src->mask << src->shift);
			value |= ((ctr->source[i][j] >> 32) << src->shift);

			/* enable the source */
			nvkm_mask(device, src->addr, mask, value);
			nvkm_debug(subdev,
				   "enabled source %08x %08x %08x\n",
				   src->addr, mask, value);
		}
	}
	return 0;
}

static int
nvkm_perfsrc_disable(struct nvkm_pm *pm, struct nvkm_perfctr *ctr)
{
	struct nvkm_subdev *subdev = &pm->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_perfdom *dom = NULL;
	struct nvkm_perfsig *sig;
	struct nvkm_perfsrc *src;
	u32 mask;
	int i, j;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 8 && ctr->source[i][j]; j++) {
			sig = nvkm_perfsig_find(pm, ctr->domain,
						ctr->signal[i], &dom);
			if (!sig)
				return -EINVAL;

			src = nvkm_perfsrc_find(pm, sig, ctr->source[i][j]);
			if (!src)
				return -EINVAL;

			/* unset enable bit if needed */
			mask = 0x00000000;
			if (src->enable)
				mask = 0x80000000;
			mask |= (src->mask << src->shift);

			/* disable the source */
			nvkm_mask(device, src->addr, mask, 0);
			nvkm_debug(subdev, "disabled source %08x %08x\n",
				   src->addr, mask);
		}
	}
	return 0;
}

/*******************************************************************************
 * Perfdom object classes
 ******************************************************************************/
static int
nvkm_perfdom_init(struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_perfdom_init none;
	} *args = data;
	struct nvkm_pm *pm = (void *)object->engine;
	struct nvkm_perfdom *dom = (void *)object;
	int ret, i;

	nv_ioctl(object, "perfdom init size %d\n", size);
	if (nvif_unvers(args->none)) {
		nv_ioctl(object, "perfdom init\n");
	} else
		return ret;

	for (i = 0; i < 4; i++) {
		if (dom->ctr[i]) {
			dom->func->init(pm, dom, dom->ctr[i]);

			/* enable sources */
			nvkm_perfsrc_enable(pm, dom->ctr[i]);
		}
	}

	/* start next batch of counters for sampling */
	dom->func->next(pm, dom);
	return 0;
}

static int
nvkm_perfdom_sample(struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_perfdom_sample none;
	} *args = data;
	struct nvkm_pm *pm = (void *)object->engine;
	struct nvkm_perfdom *dom;
	int ret;

	nv_ioctl(object, "perfdom sample size %d\n", size);
	if (nvif_unvers(args->none)) {
		nv_ioctl(object, "perfdom sample\n");
	} else
		return ret;
	pm->sequence++;

	/* sample previous batch of counters */
	list_for_each_entry(dom, &pm->domains, head)
		dom->func->next(pm, dom);

	return 0;
}

static int
nvkm_perfdom_read(struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_perfdom_read_v0 v0;
	} *args = data;
	struct nvkm_pm *pm = (void *)object->engine;
	struct nvkm_perfdom *dom = (void *)object;
	int ret, i;

	nv_ioctl(object, "perfdom read size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "perfdom read vers %d\n", args->v0.version);
	} else
		return ret;

	for (i = 0; i < 4; i++) {
		if (dom->ctr[i])
			dom->func->read(pm, dom, dom->ctr[i]);
	}

	if (!dom->clk)
		return -EAGAIN;

	for (i = 0; i < 4; i++)
		if (dom->ctr[i])
			args->v0.ctr[i] = dom->ctr[i]->ctr;
	args->v0.clk = dom->clk;
	return 0;
}

static int
nvkm_perfdom_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	switch (mthd) {
	case NVIF_PERFDOM_V0_INIT:
		return nvkm_perfdom_init(object, data, size);
	case NVIF_PERFDOM_V0_SAMPLE:
		return nvkm_perfdom_sample(object, data, size);
	case NVIF_PERFDOM_V0_READ:
		return nvkm_perfdom_read(object, data, size);
	default:
		break;
	}
	return -EINVAL;
}

static void
nvkm_perfdom_dtor(struct nvkm_object *object)
{
	struct nvkm_pm *pm = (void *)object->engine;
	struct nvkm_perfdom *dom = (void *)object;
	int i;

	for (i = 0; i < 4; i++) {
		struct nvkm_perfctr *ctr = dom->ctr[i];
		if (ctr) {
			nvkm_perfsrc_disable(pm, ctr);
			if (ctr->head.next)
				list_del(&ctr->head);
		}
		kfree(ctr);
	}
	nvkm_object_destroy(&dom->base);
}

static int
nvkm_perfctr_new(struct nvkm_perfdom *dom, int slot, u8 domain,
		 struct nvkm_perfsig *signal[4], u64 source[4][8],
		 u16 logic_op, struct nvkm_perfctr **pctr)
{
	struct nvkm_perfctr *ctr;
	int i, j;

	if (!dom)
		return -EINVAL;

	ctr = *pctr = kzalloc(sizeof(*ctr), GFP_KERNEL);
	if (!ctr)
		return -ENOMEM;

	ctr->domain   = domain;
	ctr->logic_op = logic_op;
	ctr->slot     = slot;
	for (i = 0; i < 4; i++) {
		if (signal[i]) {
			ctr->signal[i] = signal[i] - dom->signal;
			for (j = 0; j < 8; j++)
				ctr->source[i][j] = source[i][j];
		}
	}
	list_add_tail(&ctr->head, &dom->list);

	return 0;
}

static int
nvkm_perfdom_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	union {
		struct nvif_perfdom_v0 v0;
	} *args = data;
	struct nvkm_pm *pm = (void *)engine;
	struct nvkm_perfdom *sdom = NULL;
	struct nvkm_perfctr *ctr[4] = {};
	struct nvkm_perfdom *dom;
	int c, s, m;
	int ret;

	nv_ioctl(parent, "create perfdom size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create perfdom vers %d dom %d mode %02x\n",
			 args->v0.version, args->v0.domain, args->v0.mode);
	} else
		return ret;

	for (c = 0; c < ARRAY_SIZE(args->v0.ctr); c++) {
		struct nvkm_perfsig *sig[4] = {};
		u64 src[4][8] = {};

		for (s = 0; s < ARRAY_SIZE(args->v0.ctr[c].signal); s++) {
			sig[s] = nvkm_perfsig_find(pm, args->v0.domain,
						   args->v0.ctr[c].signal[s],
						   &sdom);
			if (args->v0.ctr[c].signal[s] && !sig[s])
				return -EINVAL;

			for (m = 0; m < 8; m++) {
				src[s][m] = args->v0.ctr[c].source[s][m];
				if (src[s][m] && !nvkm_perfsrc_find(pm, sig[s],
							            src[s][m]))
					return -EINVAL;
			}
		}

		ret = nvkm_perfctr_new(sdom, c, args->v0.domain, sig, src,
				       args->v0.ctr[c].logic_op, &ctr[c]);
		if (ret)
			return ret;
	}

	if (!sdom)
		return -EINVAL;

	ret = nvkm_object_create(parent, engine, oclass, 0, &dom);
	*pobject = nv_object(dom);
	if (ret)
		return ret;

	dom->func = sdom->func;
	dom->addr = sdom->addr;
	dom->mode = args->v0.mode;
	for (c = 0; c < ARRAY_SIZE(ctr); c++)
		dom->ctr[c] = ctr[c];
	return 0;
}

static struct nvkm_ofuncs
nvkm_perfdom_ofuncs = {
	.ctor = nvkm_perfdom_ctor,
	.dtor = nvkm_perfdom_dtor,
	.init = nvkm_object_init,
	.fini = nvkm_object_fini,
	.mthd = nvkm_perfdom_mthd,
};

/*******************************************************************************
 * Perfmon object classes
 ******************************************************************************/
static int
nvkm_perfmon_mthd_query_domain(struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_perfmon_query_domain_v0 v0;
	} *args = data;
	struct nvkm_pm *pm = (void *)object->engine;
	struct nvkm_perfdom *dom;
	u8 domain_nr;
	int di, ret;

	nv_ioctl(object, "perfmon query domain size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "perfmon domain vers %d iter %02x\n",
			 args->v0.version, args->v0.iter);
		di = (args->v0.iter & 0xff) - 1;
	} else
		return ret;

	domain_nr = nvkm_pm_count_perfdom(pm);
	if (di >= (int)domain_nr)
		return -EINVAL;

	if (di >= 0) {
		dom = nvkm_perfdom_find(pm, di);
		if (dom == NULL)
			return -EINVAL;

		args->v0.id         = di;
		args->v0.signal_nr  = nvkm_perfdom_count_perfsig(dom);
		strncpy(args->v0.name, dom->name, sizeof(args->v0.name));

		/* Currently only global counters (PCOUNTER) are implemented
		 * but this will be different for local counters (MP). */
		args->v0.counter_nr = 4;
	}

	if (++di < domain_nr) {
		args->v0.iter = ++di;
		return 0;
	}

	args->v0.iter = 0xff;
	return 0;
}

static int
nvkm_perfmon_mthd_query_signal(struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_perfmon_query_signal_v0 v0;
	} *args = data;
	struct nvkm_device *device = nv_device(object);
	struct nvkm_pm *pm = (void *)object->engine;
	struct nvkm_perfdom *dom;
	struct nvkm_perfsig *sig;
	const bool all = nvkm_boolopt(device->cfgopt, "NvPmShowAll", false);
	const bool raw = nvkm_boolopt(device->cfgopt, "NvPmUnnamed", all);
	int ret, si;

	nv_ioctl(object, "perfmon query signal size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object,
			 "perfmon query signal vers %d dom %d iter %04x\n",
			 args->v0.version, args->v0.domain, args->v0.iter);
		si = (args->v0.iter & 0xffff) - 1;
	} else
		return ret;

	dom = nvkm_perfdom_find(pm, args->v0.domain);
	if (dom == NULL || si >= (int)dom->signal_nr)
		return -EINVAL;

	if (si >= 0) {
		sig = &dom->signal[si];
		if (raw || !sig->name) {
			snprintf(args->v0.name, sizeof(args->v0.name),
				 "/%s/%02x", dom->name, si);
		} else {
			strncpy(args->v0.name, sig->name,
				sizeof(args->v0.name));
		}

		args->v0.signal = si;
		args->v0.source_nr = nvkm_perfsig_count_perfsrc(sig);
	}

	while (++si < dom->signal_nr) {
		if (all || dom->signal[si].name) {
			args->v0.iter = ++si;
			return 0;
		}
	}

	args->v0.iter = 0xffff;
	return 0;
}

static int
nvkm_perfmon_mthd_query_source(struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_perfmon_query_source_v0 v0;
	} *args = data;
	struct nvkm_pm *pm = (void *)object->engine;
	struct nvkm_perfdom *dom = NULL;
	struct nvkm_perfsig *sig;
	struct nvkm_perfsrc *src;
	u8 source_nr = 0;
	int si, ret;

	nv_ioctl(object, "perfmon query source size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object,
			 "perfmon source vers %d dom %d sig %02x iter %02x\n",
			 args->v0.version, args->v0.domain, args->v0.signal,
			 args->v0.iter);
		si = (args->v0.iter & 0xff) - 1;
	} else
		return ret;

	sig = nvkm_perfsig_find(pm, args->v0.domain, args->v0.signal, &dom);
	if (!sig)
		return -EINVAL;

	source_nr = nvkm_perfsig_count_perfsrc(sig);
	if (si >= (int)source_nr)
		return -EINVAL;

	if (si >= 0) {
		src = nvkm_perfsrc_find(pm, sig, sig->source[si]);
		if (!src)
			return -EINVAL;

		args->v0.source = sig->source[si];
		args->v0.mask   = src->mask;
		strncpy(args->v0.name, src->name, sizeof(args->v0.name));
	}

	if (++si < source_nr) {
		args->v0.iter = ++si;
		return 0;
	}

	args->v0.iter = 0xff;
	return 0;
}

static int
nvkm_perfmon_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	switch (mthd) {
	case NVIF_PERFMON_V0_QUERY_DOMAIN:
		return nvkm_perfmon_mthd_query_domain(object, data, size);
	case NVIF_PERFMON_V0_QUERY_SIGNAL:
		return nvkm_perfmon_mthd_query_signal(object, data, size);
	case NVIF_PERFMON_V0_QUERY_SOURCE:
		return nvkm_perfmon_mthd_query_source(object, data, size);
	default:
		break;
	}
	return -EINVAL;
}

static struct nvkm_oclass
nvkm_perfmon_sclass[] = {
	{ .handle = NVIF_IOCTL_NEW_V0_PERFDOM,
	  .ofuncs = &nvkm_perfdom_ofuncs,
	},
	{}
};

static int
nvkm_perfmon_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nvkm_parent *perfmon;
	int ret = nvkm_parent_create(parent, engine, oclass, 0,
				     nvkm_perfmon_sclass, 0, &perfmon);
	*pobject = perfmon ? &perfmon->object : NULL;
	return ret;
}

static struct nvkm_ofuncs
nvkm_perfmon_ofuncs = {
	.ctor = nvkm_perfmon_ctor,
	.dtor = _nvkm_parent_dtor,
	.init = _nvkm_parent_init,
	.fini = _nvkm_parent_fini,
	.mthd = nvkm_perfmon_mthd,
};

struct nvkm_oclass
nvkm_pm_sclass[] = {
	{
	  .handle = NVIF_IOCTL_NEW_V0_PERFMON,
	  .ofuncs = &nvkm_perfmon_ofuncs,
	},
	{},
};

/*******************************************************************************
 * PPM context
 ******************************************************************************/
static void
nvkm_perfctx_dtor(struct nvkm_object *object)
{
	struct nvkm_pm *pm = (void *)object->engine;
	struct nvkm_perfctx *ctx = (void *)object;

	mutex_lock(&nv_subdev(pm)->mutex);
	nvkm_engctx_destroy(&ctx->base);
	if (pm->context == ctx)
		pm->context = NULL;
	mutex_unlock(&nv_subdev(pm)->mutex);
}

static int
nvkm_perfctx_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nvkm_pm *pm = (void *)engine;
	struct nvkm_perfctx *ctx;
	int ret;

	/* no context needed for perfdom objects... */
	if (nv_mclass(parent) != NV_DEVICE) {
		atomic_inc(&parent->refcount);
		*pobject = parent;
		return 1;
	}

	ret = nvkm_engctx_create(parent, engine, oclass, NULL, 0, 0, 0, &ctx);
	*pobject = nv_object(ctx);
	if (ret)
		return ret;

	mutex_lock(&nv_subdev(pm)->mutex);
	if (pm->context == NULL)
		pm->context = ctx;
	if (ctx != pm->context)
		ret = -EBUSY;
	mutex_unlock(&nv_subdev(pm)->mutex);

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
nvkm_perfsrc_new(struct nvkm_pm *pm, struct nvkm_perfsig *sig,
		 const struct nvkm_specsrc *spec)
{
	const struct nvkm_specsrc *ssrc;
	const struct nvkm_specmux *smux;
	struct nvkm_perfsrc *src;
	u8 source_nr = 0;

	if (!spec) {
		/* No sources are defined for this signal. */
		return 0;
	}

	ssrc = spec;
	while (ssrc->name) {
		smux = ssrc->mux;
		while (smux->name) {
			bool found = false;
			u8 source_id = 0;
			u32 len;

			list_for_each_entry(src, &pm->sources, head) {
				if (src->addr == ssrc->addr &&
				    src->shift == smux->shift) {
					found = true;
					break;
				}
				source_id++;
			}

			if (!found) {
				src = kzalloc(sizeof(*src), GFP_KERNEL);
				if (!src)
					return -ENOMEM;

				src->addr   = ssrc->addr;
				src->mask   = smux->mask;
				src->shift  = smux->shift;
				src->enable = smux->enable;

				len = strlen(ssrc->name) +
				      strlen(smux->name) + 2;
				src->name = kzalloc(len, GFP_KERNEL);
				if (!src->name) {
					kfree(src);
					return -ENOMEM;
				}
				snprintf(src->name, len, "%s_%s", ssrc->name,
					 smux->name);

				list_add_tail(&src->head, &pm->sources);
			}

			sig->source[source_nr++] = source_id + 1;
			smux++;
		}
		ssrc++;
	}

	return 0;
}

int
nvkm_perfdom_new(struct nvkm_pm *pm, const char *name, u32 mask,
		 u32 base, u32 size_unit, u32 size_domain,
		 const struct nvkm_specdom *spec)
{
	const struct nvkm_specdom *sdom;
	const struct nvkm_specsig *ssig;
	struct nvkm_perfdom *dom;
	int ret, i;

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

			list_add_tail(&dom->head, &pm->domains);
			INIT_LIST_HEAD(&dom->list);
			dom->func = sdom->func;
			dom->addr = addr;
			dom->signal_nr = sdom->signal_nr;

			ssig = (sdom++)->signal;
			while (ssig->name) {
				struct nvkm_perfsig *sig =
					&dom->signal[ssig->signal];
				sig->name = ssig->name;
				ret = nvkm_perfsrc_new(pm, sig, ssig->source);
				if (ret)
					return ret;
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
	struct nvkm_pm *pm = (void *)object;
	return nvkm_engine_fini(&pm->engine, suspend);
}

int
_nvkm_pm_init(struct nvkm_object *object)
{
	struct nvkm_pm *pm = (void *)object;
	return nvkm_engine_init(&pm->engine);
}

void
_nvkm_pm_dtor(struct nvkm_object *object)
{
	struct nvkm_pm *pm = (void *)object;
	struct nvkm_perfdom *dom, *next_dom;
	struct nvkm_perfsrc *src, *next_src;

	list_for_each_entry_safe(dom, next_dom, &pm->domains, head) {
		list_del(&dom->head);
		kfree(dom);
	}

	list_for_each_entry_safe(src, next_src, &pm->sources, head) {
		list_del(&src->head);
		kfree(src->name);
		kfree(src);
	}

	nvkm_engine_destroy(&pm->engine);
}

int
nvkm_pm_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_pm *pm;
	int ret;

	ret = nvkm_engine_create_(parent, engine, oclass, true, "PPM",
				  "pm", length, pobject);
	pm = *pobject;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&pm->domains);
	INIT_LIST_HEAD(&pm->sources);
	return 0;
}
