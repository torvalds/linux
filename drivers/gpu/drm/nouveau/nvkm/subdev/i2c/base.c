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
#include "aux.h"
#include "bus.h"
#include "pad.h"

#include <core/notify.h>
#include <core/option.h>
#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/i2c.h>

static struct nvkm_i2c_pad *
nvkm_i2c_pad_find(struct nvkm_i2c *i2c, int id)
{
	struct nvkm_i2c_pad *pad;

	list_for_each_entry(pad, &i2c->pad, head) {
		if (pad->id == id)
			return pad;
	}

	return NULL;
}

struct nvkm_i2c_bus *
nvkm_i2c_bus_find(struct nvkm_i2c *i2c, int id)
{
	struct nvkm_bios *bios = i2c->subdev.device->bios;
	struct nvkm_i2c_bus *bus;

	if (id == NVKM_I2C_BUS_PRI || id == NVKM_I2C_BUS_SEC) {
		u8  ver, hdr, cnt, len;
		u16 i2c = dcb_i2c_table(bios, &ver, &hdr, &cnt, &len);
		if (i2c && ver >= 0x30) {
			u8 auxidx = nvbios_rd08(bios, i2c + 4);
			if (id == NVKM_I2C_BUS_PRI)
				id = NVKM_I2C_BUS_CCB((auxidx & 0x0f) >> 0);
			else
				id = NVKM_I2C_BUS_CCB((auxidx & 0xf0) >> 4);
		} else {
			id = NVKM_I2C_BUS_CCB(2);
		}
	}

	list_for_each_entry(bus, &i2c->bus, head) {
		if (bus->id == id)
			return bus;
	}

	return NULL;
}

struct nvkm_i2c_aux *
nvkm_i2c_aux_find(struct nvkm_i2c *i2c, int id)
{
	struct nvkm_i2c_aux *aux;

	list_for_each_entry(aux, &i2c->aux, head) {
		if (aux->id == id)
			return aux;
	}

	return NULL;
}

static void
nvkm_i2c_intr_fini(struct nvkm_event *event, int type, int id)
{
	struct nvkm_i2c *i2c = container_of(event, typeof(*i2c), event);
	struct nvkm_i2c_aux *aux = nvkm_i2c_aux_find(i2c, id);
	if (aux)
		i2c->func->aux_mask(i2c, type, aux->intr, 0);
}

static void
nvkm_i2c_intr_init(struct nvkm_event *event, int type, int id)
{
	struct nvkm_i2c *i2c = container_of(event, typeof(*i2c), event);
	struct nvkm_i2c_aux *aux = nvkm_i2c_aux_find(i2c, id);
	if (aux)
		i2c->func->aux_mask(i2c, type, aux->intr, aux->intr);
}

static int
nvkm_i2c_intr_ctor(struct nvkm_object *object, void *data, u32 size,
		   struct nvkm_notify *notify)
{
	struct nvkm_i2c_ntfy_req *req = data;
	if (!WARN_ON(size != sizeof(*req))) {
		notify->size  = sizeof(struct nvkm_i2c_ntfy_rep);
		notify->types = req->mask;
		notify->index = req->port;
		return 0;
	}
	return -EINVAL;
}

static const struct nvkm_event_func
nvkm_i2c_intr_func = {
	.ctor = nvkm_i2c_intr_ctor,
	.init = nvkm_i2c_intr_init,
	.fini = nvkm_i2c_intr_fini,
};

static void
nvkm_i2c_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_i2c *i2c = nvkm_i2c(subdev);
	struct nvkm_i2c_aux *aux;
	u32 hi, lo, rq, tx;

	if (!i2c->func->aux_stat)
		return;

	i2c->func->aux_stat(i2c, &hi, &lo, &rq, &tx);
	if (!hi && !lo && !rq && !tx)
		return;

	list_for_each_entry(aux, &i2c->aux, head) {
		u32 mask = 0;
		if (hi & aux->intr) mask |= NVKM_I2C_PLUG;
		if (lo & aux->intr) mask |= NVKM_I2C_UNPLUG;
		if (rq & aux->intr) mask |= NVKM_I2C_IRQ;
		if (tx & aux->intr) mask |= NVKM_I2C_DONE;
		if (mask) {
			struct nvkm_i2c_ntfy_rep rep = {
				.mask = mask,
			};
			nvkm_event_send(&i2c->event, rep.mask, aux->id,
					&rep, sizeof(rep));
		}
	}
}

static int
nvkm_i2c_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_i2c *i2c = nvkm_i2c(subdev);
	struct nvkm_i2c_pad *pad;
	struct nvkm_i2c_bus *bus;
	struct nvkm_i2c_aux *aux;
	u32 mask;

	list_for_each_entry(aux, &i2c->aux, head) {
		nvkm_i2c_aux_fini(aux);
	}

	list_for_each_entry(bus, &i2c->bus, head) {
		nvkm_i2c_bus_fini(bus);
	}

	if ((mask = (1 << i2c->func->aux) - 1), i2c->func->aux_stat) {
		i2c->func->aux_mask(i2c, NVKM_I2C_ANY, mask, 0);
		i2c->func->aux_stat(i2c, &mask, &mask, &mask, &mask);
	}

	list_for_each_entry(pad, &i2c->pad, head) {
		nvkm_i2c_pad_fini(pad);
	}

	return 0;
}

static int
nvkm_i2c_preinit(struct nvkm_subdev *subdev)
{
	struct nvkm_i2c *i2c = nvkm_i2c(subdev);
	struct nvkm_i2c_bus *bus;
	struct nvkm_i2c_pad *pad;

	/*
	 * We init our i2c busses as early as possible, since they may be
	 * needed by the vbios init scripts on some cards
	 */
	list_for_each_entry(pad, &i2c->pad, head)
		nvkm_i2c_pad_init(pad);
	list_for_each_entry(bus, &i2c->bus, head)
		nvkm_i2c_bus_init(bus);

	return 0;
}

static int
nvkm_i2c_init(struct nvkm_subdev *subdev)
{
	struct nvkm_i2c *i2c = nvkm_i2c(subdev);
	struct nvkm_i2c_bus *bus;
	struct nvkm_i2c_pad *pad;
	struct nvkm_i2c_aux *aux;

	list_for_each_entry(pad, &i2c->pad, head) {
		nvkm_i2c_pad_init(pad);
	}

	list_for_each_entry(bus, &i2c->bus, head) {
		nvkm_i2c_bus_init(bus);
	}

	list_for_each_entry(aux, &i2c->aux, head) {
		nvkm_i2c_aux_init(aux);
	}

	return 0;
}

static void *
nvkm_i2c_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_i2c *i2c = nvkm_i2c(subdev);

	nvkm_event_fini(&i2c->event);

	while (!list_empty(&i2c->aux)) {
		struct nvkm_i2c_aux *aux =
			list_first_entry(&i2c->aux, typeof(*aux), head);
		nvkm_i2c_aux_del(&aux);
	}

	while (!list_empty(&i2c->bus)) {
		struct nvkm_i2c_bus *bus =
			list_first_entry(&i2c->bus, typeof(*bus), head);
		nvkm_i2c_bus_del(&bus);
	}

	while (!list_empty(&i2c->pad)) {
		struct nvkm_i2c_pad *pad =
			list_first_entry(&i2c->pad, typeof(*pad), head);
		nvkm_i2c_pad_del(&pad);
	}

	return i2c;
}

static const struct nvkm_subdev_func
nvkm_i2c = {
	.dtor = nvkm_i2c_dtor,
	.preinit = nvkm_i2c_preinit,
	.init = nvkm_i2c_init,
	.fini = nvkm_i2c_fini,
	.intr = nvkm_i2c_intr,
};

static const struct nvkm_i2c_drv {
	u8 bios;
	u8 addr;
	int (*pad_new)(struct nvkm_i2c_bus *, int id, u8 addr,
		       struct nvkm_i2c_pad **);
}
nvkm_i2c_drv[] = {
	{ 0x0d, 0x39, anx9805_pad_new },
	{ 0x0e, 0x3b, anx9805_pad_new },
	{}
};

int
nvkm_i2c_new_(const struct nvkm_i2c_func *func, struct nvkm_device *device,
	      int index, struct nvkm_i2c **pi2c)
{
	struct nvkm_bios *bios = device->bios;
	struct nvkm_i2c *i2c;
	struct dcb_i2c_entry ccbE;
	struct dcb_output dcbE;
	u8 ver, hdr;
	int ret, i;

	if (!(i2c = *pi2c = kzalloc(sizeof(*i2c), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_subdev_ctor(&nvkm_i2c, device, index, &i2c->subdev);
	i2c->func = func;
	INIT_LIST_HEAD(&i2c->pad);
	INIT_LIST_HEAD(&i2c->bus);
	INIT_LIST_HEAD(&i2c->aux);

	i = -1;
	while (!dcb_i2c_parse(bios, ++i, &ccbE)) {
		struct nvkm_i2c_pad *pad = NULL;
		struct nvkm_i2c_bus *bus = NULL;
		struct nvkm_i2c_aux *aux = NULL;

		nvkm_debug(&i2c->subdev, "ccb %02x: type %02x drive %02x "
			   "sense %02x share %02x auxch %02x\n", i, ccbE.type,
			   ccbE.drive, ccbE.sense, ccbE.share, ccbE.auxch);

		if (ccbE.share != DCB_I2C_UNUSED) {
			const int id = NVKM_I2C_PAD_HYBRID(ccbE.share);
			if (!(pad = nvkm_i2c_pad_find(i2c, id)))
				ret = func->pad_s_new(i2c, id, &pad);
			else
				ret = 0;
		} else {
			ret = func->pad_x_new(i2c, NVKM_I2C_PAD_CCB(i), &pad);
		}

		if (ret) {
			nvkm_error(&i2c->subdev, "ccb %02x pad, %d\n", i, ret);
			nvkm_i2c_pad_del(&pad);
			continue;
		}

		if (pad->func->bus_new_0 && ccbE.type == DCB_I2C_NV04_BIT) {
			ret = pad->func->bus_new_0(pad, NVKM_I2C_BUS_CCB(i),
						   ccbE.drive,
						   ccbE.sense, &bus);
		} else
		if (pad->func->bus_new_4 &&
		    ( ccbE.type == DCB_I2C_NV4E_BIT ||
		      ccbE.type == DCB_I2C_NVIO_BIT ||
		     (ccbE.type == DCB_I2C_PMGR &&
		      ccbE.drive != DCB_I2C_UNUSED))) {
			ret = pad->func->bus_new_4(pad, NVKM_I2C_BUS_CCB(i),
						   ccbE.drive, &bus);
		}

		if (ret) {
			nvkm_error(&i2c->subdev, "ccb %02x bus, %d\n", i, ret);
			nvkm_i2c_bus_del(&bus);
		}

		if (pad->func->aux_new_6 &&
		    ( ccbE.type == DCB_I2C_NVIO_AUX ||
		     (ccbE.type == DCB_I2C_PMGR &&
		      ccbE.auxch != DCB_I2C_UNUSED))) {
			ret = pad->func->aux_new_6(pad, NVKM_I2C_BUS_CCB(i),
						   ccbE.auxch, &aux);
		} else {
			ret = 0;
		}

		if (ret) {
			nvkm_error(&i2c->subdev, "ccb %02x aux, %d\n", i, ret);
			nvkm_i2c_aux_del(&aux);
		}

		if (ccbE.type != DCB_I2C_UNUSED && !bus && !aux) {
			nvkm_warn(&i2c->subdev, "ccb %02x was ignored\n", i);
			continue;
		}
	}

	i = -1;
	while (dcb_outp_parse(bios, ++i, &ver, &hdr, &dcbE)) {
		const struct nvkm_i2c_drv *drv = nvkm_i2c_drv;
		struct nvkm_i2c_bus *bus;
		struct nvkm_i2c_pad *pad;

		/* internal outputs handled by native i2c busses (above) */
		if (!dcbE.location)
			continue;

		/* we need an i2c bus to talk to the external encoder */
		bus = nvkm_i2c_bus_find(i2c, dcbE.i2c_index);
		if (!bus) {
			nvkm_debug(&i2c->subdev, "dcb %02x no bus\n", i);
			continue;
		}

		/* ... and a driver for it */
		while (drv->pad_new) {
			if (drv->bios == dcbE.extdev)
				break;
			drv++;
		}

		if (!drv->pad_new) {
			nvkm_debug(&i2c->subdev, "dcb %02x drv %02x unknown\n",
				   i, dcbE.extdev);
			continue;
		}

		/* find/create an instance of the driver */
		pad = nvkm_i2c_pad_find(i2c, NVKM_I2C_PAD_EXT(dcbE.extdev));
		if (!pad) {
			const int id = NVKM_I2C_PAD_EXT(dcbE.extdev);
			ret = drv->pad_new(bus, id, drv->addr, &pad);
			if (ret) {
				nvkm_error(&i2c->subdev, "dcb %02x pad, %d\n",
					   i, ret);
				nvkm_i2c_pad_del(&pad);
				continue;
			}
		}

		/* create any i2c bus / aux channel required by the output */
		if (pad->func->aux_new_6 && dcbE.type == DCB_OUTPUT_DP) {
			const int id = NVKM_I2C_AUX_EXT(dcbE.extdev);
			struct nvkm_i2c_aux *aux = NULL;
			ret = pad->func->aux_new_6(pad, id, 0, &aux);
			if (ret) {
				nvkm_error(&i2c->subdev, "dcb %02x aux, %d\n",
					   i, ret);
				nvkm_i2c_aux_del(&aux);
			}
		} else
		if (pad->func->bus_new_4) {
			const int id = NVKM_I2C_BUS_EXT(dcbE.extdev);
			struct nvkm_i2c_bus *bus = NULL;
			ret = pad->func->bus_new_4(pad, id, 0, &bus);
			if (ret) {
				nvkm_error(&i2c->subdev, "dcb %02x bus, %d\n",
					   i, ret);
				nvkm_i2c_bus_del(&bus);
			}
		}
	}

	return nvkm_event_init(&nvkm_i2c_intr_func, 4, i, &i2c->event);
}
