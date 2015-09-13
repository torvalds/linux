/*
 * Copyright 2009 Red Hat Inc.
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
#include "aux.h"
#include "pad.h"

static int
nvkm_i2c_aux_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct nvkm_i2c_aux *aux = container_of(adap, typeof(*aux), i2c);
	struct i2c_msg *msg = msgs;
	int ret, mcnt = num;

	ret = nvkm_i2c_aux_acquire(aux);
	if (ret)
		return ret;

	while (mcnt--) {
		u8 remaining = msg->len;
		u8 *ptr = msg->buf;

		while (remaining) {
			u8 cnt = (remaining > 16) ? 16 : remaining;
			u8 cmd;

			if (msg->flags & I2C_M_RD)
				cmd = 1;
			else
				cmd = 0;

			if (mcnt || remaining > 16)
				cmd |= 4; /* MOT */

			ret = aux->func->xfer(aux, true, cmd, msg->addr, ptr, cnt);
			if (ret < 0) {
				nvkm_i2c_aux_release(aux);
				return ret;
			}

			ptr += cnt;
			remaining -= cnt;
		}

		msg++;
	}

	nvkm_i2c_aux_release(aux);
	return num;
}

static u32
nvkm_i2c_aux_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

const struct i2c_algorithm
nvkm_i2c_aux_i2c_algo = {
	.master_xfer = nvkm_i2c_aux_i2c_xfer,
	.functionality = nvkm_i2c_aux_i2c_func
};

void
nvkm_i2c_aux_monitor(struct nvkm_i2c_aux *aux, bool monitor)
{
	struct nvkm_i2c_pad *pad = aux->pad;
	AUX_TRACE(aux, "monitor: %s", monitor ? "yes" : "no");
	if (monitor)
		nvkm_i2c_pad_mode(pad, NVKM_I2C_PAD_AUX);
	else
		nvkm_i2c_pad_mode(pad, NVKM_I2C_PAD_OFF);
}

void
nvkm_i2c_aux_release(struct nvkm_i2c_aux *aux)
{
	struct nvkm_i2c_pad *pad = aux->pad;
	AUX_TRACE(aux, "release");
	nvkm_i2c_pad_release(pad);
	mutex_unlock(&aux->mutex);
}

int
nvkm_i2c_aux_acquire(struct nvkm_i2c_aux *aux)
{
	struct nvkm_i2c_pad *pad = aux->pad;
	int ret;
	AUX_TRACE(aux, "acquire");
	mutex_lock(&aux->mutex);
	ret = nvkm_i2c_pad_acquire(pad, NVKM_I2C_PAD_AUX);
	if (ret)
		mutex_unlock(&aux->mutex);
	return ret;
}

int
nvkm_i2c_aux_xfer(struct nvkm_i2c_aux *aux, bool retry, u8 type,
		  u32 addr, u8 *data, u8 size)
{
	return aux->func->xfer(aux, retry, type, addr, data, size);
}

int
nvkm_i2c_aux_lnk_ctl(struct nvkm_i2c_aux *aux, int nr, int bw, bool ef)
{
	if (aux->func->lnk_ctl)
		return aux->func->lnk_ctl(aux, nr, bw, ef);
	return -ENODEV;
}

void
nvkm_i2c_aux_del(struct nvkm_i2c_aux **paux)
{
	struct nvkm_i2c_aux *aux = *paux;
	if (aux && !WARN_ON(!aux->func)) {
		AUX_TRACE(aux, "dtor");
		list_del(&aux->head);
		i2c_del_adapter(&aux->i2c);
		kfree(*paux);
		*paux = NULL;
	}
}

int
nvkm_i2c_aux_ctor(const struct nvkm_i2c_aux_func *func,
		  struct nvkm_i2c_pad *pad, int id,
		  struct nvkm_i2c_aux *aux)
{
	struct nvkm_device *device = pad->i2c->subdev.device;

	aux->func = func;
	aux->pad = pad;
	aux->id = id;
	mutex_init(&aux->mutex);
	list_add_tail(&aux->head, &pad->i2c->aux);
	AUX_TRACE(aux, "ctor");

	snprintf(aux->i2c.name, sizeof(aux->i2c.name), "nvkm-%s-aux-%04x",
		 dev_name(device->dev), id);
	aux->i2c.owner = THIS_MODULE;
	aux->i2c.dev.parent = device->dev;
	aux->i2c.algo = &nvkm_i2c_aux_i2c_algo;
	return i2c_add_adapter(&aux->i2c);
}

int
nvkm_i2c_aux_new_(const struct nvkm_i2c_aux_func *func,
		  struct nvkm_i2c_pad *pad, int id,
		  struct nvkm_i2c_aux **paux)
{
	if (!(*paux = kzalloc(sizeof(**paux), GFP_KERNEL)))
		return -ENOMEM;
	return nvkm_i2c_aux_ctor(func, pad, id, *paux);
}
