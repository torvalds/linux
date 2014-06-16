/*
 * Copyright 2014 Red Hat Inc.
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

#include <subdev/i2c.h>

#include "outpdp.h"
#include "conn.h"
#include "dport.h"

int
nvkm_output_dp_train(struct nvkm_output *base, u32 datarate, bool wait)
{
	struct nvkm_output_dp *outp = (void *)base;
	bool retrain = true;
	u8 link[2], stat[3];
	u32 rate;
	int ret, i;

	/* check that the link is trained at a high enough rate */
	ret = nv_rdaux(outp->base.edid, DPCD_LC00_LINK_BW_SET, link, 2);
	if (ret) {
		DBG("failed to read link config, assuming no sink\n");
		goto done;
	}

	rate = link[0] * 27000 * (link[1] & DPCD_LC01_LANE_COUNT_SET);
	if (rate < ((datarate / 8) * 10)) {
		DBG("link not trained at sufficient rate\n");
		goto done;
	}

	/* check that link is still trained */
	ret = nv_rdaux(outp->base.edid, DPCD_LS02, stat, 3);
	if (ret) {
		DBG("failed to read link status, assuming no sink\n");
		goto done;
	}

	if (stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE) {
		for (i = 0; i < (link[1] & DPCD_LC01_LANE_COUNT_SET); i++) {
			u8 lane = (stat[i >> 1] >> ((i & 1) * 4)) & 0x0f;
			if (!(lane & DPCD_LS02_LANE0_CR_DONE) ||
			    !(lane & DPCD_LS02_LANE0_CHANNEL_EQ_DONE) ||
			    !(lane & DPCD_LS02_LANE0_SYMBOL_LOCKED)) {
				DBG("lane %d not equalised\n", lane);
				goto done;
			}
		}
		retrain = false;
	} else {
		DBG("no inter-lane alignment\n");
	}

done:
	if (retrain || !atomic_read(&outp->lt.done)) {
		/* no sink, but still need to configure source */
		if (outp->dpcd[DPCD_RC00_DPCD_REV] == 0x00) {
			outp->dpcd[DPCD_RC01_MAX_LINK_RATE] =
				outp->base.info.dpconf.link_bw;
			outp->dpcd[DPCD_RC02] =
				outp->base.info.dpconf.link_nr;
		}
		atomic_set(&outp->lt.done, 0);
		schedule_work(&outp->lt.work);
	} else {
		nouveau_event_get(outp->irq);
	}

	if (wait) {
		if (!wait_event_timeout(outp->lt.wait,
					atomic_read(&outp->lt.done),
					msecs_to_jiffies(2000)))
			ret = -ETIMEDOUT;
	}

	return ret;
}

static void
nvkm_output_dp_enable(struct nvkm_output_dp *outp, bool present)
{
	struct nouveau_i2c_port *port = outp->base.edid;
	if (present) {
		if (!outp->present) {
			nouveau_i2c(port)->acquire_pad(port, 0);
			DBG("aux power -> always\n");
			outp->present = true;
		}
		nvkm_output_dp_train(&outp->base, 0, true);
	} else {
		if (outp->present) {
			nouveau_i2c(port)->release_pad(port);
			DBG("aux power -> demand\n");
			outp->present = false;
		}
		atomic_set(&outp->lt.done, 0);
	}
}

static void
nvkm_output_dp_detect(struct nvkm_output_dp *outp)
{
	struct nouveau_i2c_port *port = outp->base.edid;
	int ret = nouveau_i2c(port)->acquire_pad(port, 0);
	if (ret == 0) {
		ret = nv_rdaux(outp->base.edid, DPCD_RC00_DPCD_REV,
			       outp->dpcd, sizeof(outp->dpcd));
		nvkm_output_dp_enable(outp, ret == 0);
		nouveau_i2c(port)->release_pad(port);
	}
}

static void
nvkm_output_dp_service_work(struct work_struct *work)
{
	struct nvkm_output_dp *outp = container_of(work, typeof(*outp), work);
	struct nouveau_disp *disp = nouveau_disp(outp);
	int type = atomic_xchg(&outp->pending, 0);
	u32 send = 0;

	if (type & (NVKM_I2C_PLUG | NVKM_I2C_UNPLUG)) {
		nvkm_output_dp_detect(outp);
		if (type & NVKM_I2C_UNPLUG)
			send |= NVKM_HPD_UNPLUG;
		if (type & NVKM_I2C_PLUG)
			send |= NVKM_HPD_PLUG;
		nouveau_event_get(outp->base.conn->hpd.event);
	}

	if (type & NVKM_I2C_IRQ) {
		nvkm_output_dp_train(&outp->base, 0, true);
		send |= NVKM_HPD_IRQ;
	}

	nouveau_event_trigger(disp->hpd, send, outp->base.info.connector);
}

static int
nvkm_output_dp_service(void *data, u32 type, int index)
{
	struct nvkm_output_dp *outp = data;
	DBG("HPD: %d\n", type);
	atomic_or(type, &outp->pending);
	schedule_work(&outp->work);
	return NVKM_EVENT_DROP;
}

int
_nvkm_output_dp_fini(struct nouveau_object *object, bool suspend)
{
	struct nvkm_output_dp *outp = (void *)object;
	nouveau_event_put(outp->irq);
	nvkm_output_dp_enable(outp, false);
	return nvkm_output_fini(&outp->base, suspend);
}

int
_nvkm_output_dp_init(struct nouveau_object *object)
{
	struct nvkm_output_dp *outp = (void *)object;
	nvkm_output_dp_detect(outp);
	return nvkm_output_init(&outp->base);
}

void
_nvkm_output_dp_dtor(struct nouveau_object *object)
{
	struct nvkm_output_dp *outp = (void *)object;
	nouveau_event_ref(NULL, &outp->irq);
	nvkm_output_destroy(&outp->base);
}

int
nvkm_output_dp_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass,
		       struct dcb_output *info, int index,
		       int length, void **pobject)
{
	struct nouveau_bios *bios = nouveau_bios(parent);
	struct nouveau_i2c *i2c = nouveau_i2c(parent);
	struct nvkm_output_dp *outp;
	u8  hdr, cnt, len;
	u32 data;
	int ret;

	ret = nvkm_output_create_(parent, engine, oclass, info, index,
				  length, pobject);
	outp = *pobject;
	if (ret)
		return ret;

	nouveau_event_ref(NULL, &outp->base.conn->hpd.event);

	/* access to the aux channel is not optional... */
	if (!outp->base.edid) {
		ERR("aux channel not found\n");
		return -ENODEV;
	}

	/* nor is the bios data for this output... */
	data = nvbios_dpout_match(bios, outp->base.info.hasht,
				  outp->base.info.hashm, &outp->version,
				  &hdr, &cnt, &len, &outp->info);
	if (!data) {
		ERR("no bios dp data\n");
		return -ENODEV;
	}

	DBG("bios dp %02x %02x %02x %02x\n", outp->version, hdr, cnt, len);

	/* link training */
	INIT_WORK(&outp->lt.work, nouveau_dp_train);
	init_waitqueue_head(&outp->lt.wait);
	atomic_set(&outp->lt.done, 0);

	/* link maintenance */
	ret = nouveau_event_new(i2c->ntfy, NVKM_I2C_IRQ, outp->base.edid->index,
				nvkm_output_dp_service, outp, &outp->irq);
	if (ret) {
		ERR("error monitoring aux irq event: %d\n", ret);
		return ret;
	}

	INIT_WORK(&outp->work, nvkm_output_dp_service_work);

	/* hotplug detect, replaces gpio-based mechanism with aux events */
	ret = nouveau_event_new(i2c->ntfy, NVKM_I2C_PLUG | NVKM_I2C_UNPLUG,
				outp->base.edid->index,
				nvkm_output_dp_service, outp,
			       &outp->base.conn->hpd.event);
	if (ret) {
		ERR("error monitoring aux hpd events: %d\n", ret);
		return ret;
	}

	return 0;
}

int
_nvkm_output_dp_ctor(struct nouveau_object *parent,
		     struct nouveau_object *engine,
		     struct nouveau_oclass *oclass, void *info, u32 index,
		     struct nouveau_object **pobject)
{
	struct nvkm_output_dp *outp;
	int ret;

	ret = nvkm_output_dp_create(parent, engine, oclass, info, index, &outp);
	*pobject = nv_object(outp);
	if (ret)
		return ret;

	return 0;
}
