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
#include "outpdp.h"
#include "conn.h"
#include "dport.h"
#include "priv.h"

#include <subdev/i2c.h>

#include <nvif/event.h>

int
nvkm_output_dp_train(struct nvkm_output *base, u32 datarate)
{
	struct nvkm_output_dp *outp = nvkm_output_dp(base);
	bool retrain = true;
	u8 link[2], stat[3];
	u32 linkrate;
	int ret, i;

	mutex_lock(&outp->mutex);

	/* check that the link is trained at a high enough rate */
	ret = nvkm_rdaux(outp->aux, DPCD_LC00_LINK_BW_SET, link, 2);
	if (ret) {
		OUTP_DBG(&outp->base,
			 "failed to read link config, assuming no sink");
		goto done;
	}

	linkrate = link[0] * 27000 * (link[1] & DPCD_LC01_LANE_COUNT_SET);
	linkrate = (linkrate * 8) / 10; /* 8B/10B coding overhead */
	datarate = (datarate + 9) / 10; /* -> decakilobits */
	if (linkrate < datarate) {
		OUTP_DBG(&outp->base, "link not trained at sufficient rate");
		goto done;
	}

	/* check that link is still trained */
	ret = nvkm_rdaux(outp->aux, DPCD_LS02, stat, 3);
	if (ret) {
		OUTP_DBG(&outp->base,
			 "failed to read link status, assuming no sink");
		goto done;
	}

	if (stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE) {
		for (i = 0; i < (link[1] & DPCD_LC01_LANE_COUNT_SET); i++) {
			u8 lane = (stat[i >> 1] >> ((i & 1) * 4)) & 0x0f;
			if (!(lane & DPCD_LS02_LANE0_CR_DONE) ||
			    !(lane & DPCD_LS02_LANE0_CHANNEL_EQ_DONE) ||
			    !(lane & DPCD_LS02_LANE0_SYMBOL_LOCKED)) {
				OUTP_DBG(&outp->base,
					 "lane %d not equalised", lane);
				goto done;
			}
		}
		retrain = false;
	} else {
		OUTP_DBG(&outp->base, "no inter-lane alignment");
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
		nvkm_dp_train(outp);
	}

	mutex_unlock(&outp->mutex);
	return ret;
}

static void
nvkm_output_dp_enable(struct nvkm_output_dp *outp, bool enable)
{
	struct nvkm_i2c_aux *aux = outp->aux;

	if (enable) {
		if (!outp->present) {
			OUTP_DBG(&outp->base, "aux power -> always");
			nvkm_i2c_aux_monitor(aux, true);
			outp->present = true;
		}

		if (!nvkm_rdaux(aux, DPCD_RC00_DPCD_REV, outp->dpcd,
				sizeof(outp->dpcd))) {
			nvkm_output_dp_train(&outp->base, 0);
			return;
		}
	}

	if (outp->present) {
		OUTP_DBG(&outp->base, "aux power -> demand");
		nvkm_i2c_aux_monitor(aux, false);
		outp->present = false;
	}

	atomic_set(&outp->lt.done, 0);
}

static int
nvkm_output_dp_hpd(struct nvkm_notify *notify)
{
	const struct nvkm_i2c_ntfy_rep *line = notify->data;
	struct nvkm_output_dp *outp = container_of(notify, typeof(*outp), hpd);
	struct nvkm_connector *conn = outp->base.conn;
	struct nvkm_disp *disp = outp->base.disp;
	struct nvif_notify_conn_rep_v0 rep = {};

	OUTP_DBG(&outp->base, "HPD: %d", line->mask);
	nvkm_output_dp_enable(outp, true);

	if (line->mask & NVKM_I2C_UNPLUG)
		rep.mask |= NVIF_NOTIFY_CONN_V0_UNPLUG;
	if (line->mask & NVKM_I2C_PLUG)
		rep.mask |= NVIF_NOTIFY_CONN_V0_PLUG;

	nvkm_event_send(&disp->hpd, rep.mask, conn->index, &rep, sizeof(rep));
	return NVKM_NOTIFY_KEEP;
}

static int
nvkm_output_dp_irq(struct nvkm_notify *notify)
{
	const struct nvkm_i2c_ntfy_rep *line = notify->data;
	struct nvkm_output_dp *outp = container_of(notify, typeof(*outp), irq);
	struct nvkm_connector *conn = outp->base.conn;
	struct nvkm_disp *disp = outp->base.disp;
	struct nvif_notify_conn_rep_v0 rep = {
		.mask = NVIF_NOTIFY_CONN_V0_IRQ,
	};

	OUTP_DBG(&outp->base, "IRQ: %d", line->mask);
	nvkm_output_dp_train(&outp->base, 0);

	nvkm_event_send(&disp->hpd, rep.mask, conn->index, &rep, sizeof(rep));
	return NVKM_NOTIFY_KEEP;
}

static void
nvkm_output_dp_fini(struct nvkm_output *base)
{
	struct nvkm_output_dp *outp = nvkm_output_dp(base);
	nvkm_notify_put(&outp->hpd);
	nvkm_notify_put(&outp->irq);
	nvkm_output_dp_enable(outp, false);
}

static void
nvkm_output_dp_init(struct nvkm_output *base)
{
	struct nvkm_output_dp *outp = nvkm_output_dp(base);
	nvkm_notify_put(&outp->base.conn->hpd);
	nvkm_output_dp_enable(outp, true);
	nvkm_notify_get(&outp->irq);
	nvkm_notify_get(&outp->hpd);
}

static void *
nvkm_output_dp_dtor(struct nvkm_output *base)
{
	struct nvkm_output_dp *outp = nvkm_output_dp(base);
	nvkm_notify_fini(&outp->hpd);
	nvkm_notify_fini(&outp->irq);
	return outp;
}

static const struct nvkm_output_func
nvkm_output_dp_func = {
	.dtor = nvkm_output_dp_dtor,
	.init = nvkm_output_dp_init,
	.fini = nvkm_output_dp_fini,
};

int
nvkm_output_dp_ctor(const struct nvkm_output_dp_func *func,
		    struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
		    struct nvkm_i2c_aux *aux, struct nvkm_output_dp *outp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_bios *bios = device->bios;
	struct nvkm_i2c *i2c = device->i2c;
	u8  hdr, cnt, len;
	u32 data;
	int ret;

	nvkm_output_ctor(&nvkm_output_dp_func, disp, index, dcbE, &outp->base);
	outp->func = func;
	outp->aux = aux;
	if (!outp->aux) {
		OUTP_ERR(&outp->base, "no aux");
		return -ENODEV;
	}

	/* bios data is not optional */
	data = nvbios_dpout_match(bios, outp->base.info.hasht,
				  outp->base.info.hashm, &outp->version,
				  &hdr, &cnt, &len, &outp->info);
	if (!data) {
		OUTP_ERR(&outp->base, "no bios dp data");
		return -ENODEV;
	}

	OUTP_DBG(&outp->base, "bios dp %02x %02x %02x %02x",
		 outp->version, hdr, cnt, len);

	/* link maintenance */
	ret = nvkm_notify_init(NULL, &i2c->event, nvkm_output_dp_irq, true,
			       &(struct nvkm_i2c_ntfy_req) {
				.mask = NVKM_I2C_IRQ,
				.port = outp->aux->id,
			       },
			       sizeof(struct nvkm_i2c_ntfy_req),
			       sizeof(struct nvkm_i2c_ntfy_rep),
			       &outp->irq);
	if (ret) {
		OUTP_ERR(&outp->base, "error monitoring aux irq: %d", ret);
		return ret;
	}

	mutex_init(&outp->mutex);
	atomic_set(&outp->lt.done, 0);

	/* hotplug detect, replaces gpio-based mechanism with aux events */
	ret = nvkm_notify_init(NULL, &i2c->event, nvkm_output_dp_hpd, true,
			       &(struct nvkm_i2c_ntfy_req) {
				.mask = NVKM_I2C_PLUG | NVKM_I2C_UNPLUG,
				.port = outp->aux->id,
			       },
			       sizeof(struct nvkm_i2c_ntfy_req),
			       sizeof(struct nvkm_i2c_ntfy_rep),
			       &outp->hpd);
	if (ret) {
		OUTP_ERR(&outp->base, "error monitoring aux hpd: %d", ret);
		return ret;
	}

	return 0;
}

int
nvkm_output_dp_new_(const struct nvkm_output_dp_func *func,
		    struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
		    struct nvkm_output **poutp)
{
	struct nvkm_i2c *i2c = disp->engine.subdev.device->i2c;
	struct nvkm_i2c_aux *aux = nvkm_i2c_aux_find(i2c, dcbE->i2c_index);
	struct nvkm_output_dp *outp;

	if (!(outp = kzalloc(sizeof(*outp), GFP_KERNEL)))
		return -ENOMEM;
	*poutp = &outp->base;

	return nvkm_output_dp_ctor(func, disp, index, dcbE, aux, outp);
}
