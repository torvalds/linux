/*
 * Copyright 2015 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "pad.h"

static void
nvkm_i2c_pad_mode_locked(struct nvkm_i2c_pad *pad, enum nvkm_i2c_pad_mode mode)
{
	PAD_TRACE(pad, "-> %s", (mode == NVKM_I2C_PAD_AUX) ? "aux" :
			      (mode == NVKM_I2C_PAD_I2C) ? "i2c" : "off");
	if (pad->func->mode)
		pad->func->mode(pad, mode);
}

void
nvkm_i2c_pad_mode(struct nvkm_i2c_pad *pad, enum nvkm_i2c_pad_mode mode)
{
	PAD_TRACE(pad, "mode %d", mode);
	mutex_lock(&pad->mutex);
	nvkm_i2c_pad_mode_locked(pad, mode);
	pad->mode = mode;
	mutex_unlock(&pad->mutex);
}

void
nvkm_i2c_pad_release(struct nvkm_i2c_pad *pad)
{
	PAD_TRACE(pad, "release");
	if (pad->mode == NVKM_I2C_PAD_OFF)
		nvkm_i2c_pad_mode_locked(pad, pad->mode);
	mutex_unlock(&pad->mutex);
}

int
nvkm_i2c_pad_acquire(struct nvkm_i2c_pad *pad, enum nvkm_i2c_pad_mode mode)
{
	PAD_TRACE(pad, "acquire");
	mutex_lock(&pad->mutex);
	if (pad->mode != mode) {
		if (pad->mode != NVKM_I2C_PAD_OFF) {
			mutex_unlock(&pad->mutex);
			return -EBUSY;
		}
		nvkm_i2c_pad_mode_locked(pad, mode);
	}
	return 0;
}

void
nvkm_i2c_pad_fini(struct nvkm_i2c_pad *pad)
{
	PAD_TRACE(pad, "fini");
	nvkm_i2c_pad_mode_locked(pad, NVKM_I2C_PAD_OFF);
}

void
nvkm_i2c_pad_init(struct nvkm_i2c_pad *pad)
{
	PAD_TRACE(pad, "init");
	nvkm_i2c_pad_mode_locked(pad, pad->mode);
}

void
nvkm_i2c_pad_del(struct nvkm_i2c_pad **ppad)
{
	struct nvkm_i2c_pad *pad = *ppad;
	if (pad) {
		PAD_TRACE(pad, "dtor");
		list_del(&pad->head);
		kfree(pad);
		pad = NULL;
	}
}

void
nvkm_i2c_pad_ctor(const struct nvkm_i2c_pad_func *func, struct nvkm_i2c *i2c,
		  int id, struct nvkm_i2c_pad *pad)
{
	pad->func = func;
	pad->i2c = i2c;
	pad->id = id;
	pad->mode = NVKM_I2C_PAD_OFF;
	mutex_init(&pad->mutex);
	list_add_tail(&pad->head, &i2c->pad);
	PAD_TRACE(pad, "ctor");
}

int
nvkm_i2c_pad_new_(const struct nvkm_i2c_pad_func *func, struct nvkm_i2c *i2c,
		  int id, struct nvkm_i2c_pad **ppad)
{
	if (!(*ppad = kzalloc(sizeof(**ppad), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_i2c_pad_ctor(func, i2c, id, *ppad);
	return 0;
}
