/*
 * Copyright 2016 Red Hat Inc.
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
#include <nvif/driver.h>
#include <nvif/client.h>

static const struct nvif_driver *
nvif_driver[] = {
#ifdef __KERNEL__
	&nvif_driver_nvkm,
#else
	&nvif_driver_drm,
	&nvif_driver_lib,
	&nvif_driver_null,
#endif
	NULL
};

int
nvif_driver_init(const char *drv, const char *cfg, const char *dbg,
		 const char *name, u64 device, struct nvif_client *client)
{
	int ret = -EINVAL, i;

	for (i = 0; (client->driver = nvif_driver[i]); i++) {
		if (!drv || !strcmp(client->driver->name, drv)) {
			ret = client->driver->init(name, device, cfg, dbg,
						   &client->object.priv);
			if (ret == 0)
				break;
			client->driver->fini(client->object.priv);
		}
	}

	if (ret == 0)
		ret = nvif_client_ctor(client, name, device, client);
	return ret;
}
