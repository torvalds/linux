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
#include "conn.h"
#include "outp.h"
#include "priv.h"

#include <subdev/gpio.h>

#include <nvif/event.h>

static int
nvkm_connector_hpd(struct nvkm_notify *notify)
{
	struct nvkm_connector *conn = container_of(notify, typeof(*conn), hpd);
	struct nvkm_disp *disp = conn->disp;
	struct nvkm_gpio *gpio = disp->engine.subdev.device->gpio;
	const struct nvkm_gpio_ntfy_rep *line = notify->data;
	struct nvif_notify_conn_rep_v0 rep;
	int index = conn->index;

	CONN_DBG(conn, "HPD: %d", line->mask);

	if (!nvkm_gpio_get(gpio, 0, DCB_GPIO_UNUSED, conn->hpd.index))
		rep.mask = NVIF_NOTIFY_CONN_V0_UNPLUG;
	else
		rep.mask = NVIF_NOTIFY_CONN_V0_PLUG;
	rep.version = 0;

	nvkm_event_send(&disp->hpd, rep.mask, index, &rep, sizeof(rep));
	return NVKM_NOTIFY_KEEP;
}

void
nvkm_connector_fini(struct nvkm_connector *conn)
{
	nvkm_notify_put(&conn->hpd);
}

void
nvkm_connector_init(struct nvkm_connector *conn)
{
	nvkm_notify_get(&conn->hpd);
}

void
nvkm_connector_del(struct nvkm_connector **pconn)
{
	struct nvkm_connector *conn = *pconn;
	if (conn) {
		nvkm_notify_fini(&conn->hpd);
		kfree(*pconn);
		*pconn = NULL;
	}
}

static void
nvkm_connector_ctor(struct nvkm_disp *disp, int index,
		    struct nvbios_connE *info, struct nvkm_connector *conn)
{
	static const u8 hpd[] = { 0x07, 0x08, 0x51, 0x52, 0x5e, 0x5f, 0x60 };
	struct nvkm_gpio *gpio = disp->engine.subdev.device->gpio;
	struct dcb_gpio_func func;
	int ret;

	conn->disp = disp;
	conn->index = index;
	conn->info = *info;

	CONN_DBG(conn, "type %02x loc %d hpd %02x dp %x di %x sr %x lcdid %x",
		 info->type, info->location, info->hpd, info->dp,
		 info->di, info->sr, info->lcdid);

	if ((info->hpd = ffs(info->hpd))) {
		if (--info->hpd >= ARRAY_SIZE(hpd)) {
			CONN_ERR(conn, "hpd %02x unknown", info->hpd);
			return;
		}
		info->hpd = hpd[info->hpd];

		ret = nvkm_gpio_find(gpio, 0, info->hpd, DCB_GPIO_UNUSED, &func);
		if (ret) {
			CONN_ERR(conn, "func %02x lookup failed, %d",
				 info->hpd, ret);
			return;
		}

		ret = nvkm_notify_init(NULL, &gpio->event, nvkm_connector_hpd,
				       true, &(struct nvkm_gpio_ntfy_req) {
					.mask = NVKM_GPIO_TOGGLED,
					.line = func.line,
				       },
				       sizeof(struct nvkm_gpio_ntfy_req),
				       sizeof(struct nvkm_gpio_ntfy_rep),
				       &conn->hpd);
		if (ret) {
			CONN_ERR(conn, "func %02x failed, %d", info->hpd, ret);
		} else {
			CONN_DBG(conn, "func %02x (HPD)", info->hpd);
		}
	}
}

int
nvkm_connector_new(struct nvkm_disp *disp, int index,
		   struct nvbios_connE *info, struct nvkm_connector **pconn)
{
	if (!(*pconn = kzalloc(sizeof(**pconn), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_connector_ctor(disp, index, info, *pconn);
	return 0;
}
