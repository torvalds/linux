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
#include "priv.h"

static int
nvkm_fannil_get(struct nvkm_therm *therm)
{
	return -ENODEV;
}

static int
nvkm_fannil_set(struct nvkm_therm *therm, int percent)
{
	return -ENODEV;
}

int
nvkm_fannil_create(struct nvkm_therm *therm)
{
	struct nvkm_therm_priv *tpriv = (void *)therm;
	struct nvkm_fan *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	tpriv->fan = priv;
	if (!priv)
		return -ENOMEM;

	priv->type = "none / external";
	priv->get = nvkm_fannil_get;
	priv->set = nvkm_fannil_set;
	return 0;
}
