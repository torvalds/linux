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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include <nvif/client.h>
#include <nvif/driver.h>
#include <nvif/ioctl.h>

int
nvif_client_ioctl(struct nvif_client *client, void *data, u32 size)
{
	return client->driver->ioctl(client->object.priv, client->super, data, size, NULL);
}

int
nvif_client_suspend(struct nvif_client *client)
{
	return client->driver->suspend(client->object.priv);
}

int
nvif_client_resume(struct nvif_client *client)
{
	return client->driver->resume(client->object.priv);
}

void
nvif_client_fini(struct nvif_client *client)
{
	if (client->driver) {
		client->driver->fini(client->object.priv);
		client->driver = NULL;
		client->object.client = NULL;
		nvif_object_fini(&client->object);
	}
}

static const struct nvif_driver *
nvif_drivers[] = {
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
nvif_client_init(const char *driver, const char *name, u64 device,
		 const char *cfg, const char *dbg, struct nvif_client *client)
{
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_nop_v0 nop;
	} args = {};
	int ret, i;

	ret = nvif_object_init(NULL, 0, 0, NULL, 0, &client->object);
	if (ret)
		return ret;

	client->object.client = client;
	client->object.handle = ~0;
	client->route = NVIF_IOCTL_V0_ROUTE_NVIF;
	client->super = true;

	for (i = 0, ret = -EINVAL; (client->driver = nvif_drivers[i]); i++) {
		if (!driver || !strcmp(client->driver->name, driver)) {
			ret = client->driver->init(name, device, cfg, dbg,
						  &client->object.priv);
			if (!ret || driver)
				break;
		}
	}

	if (ret == 0) {
		ret = nvif_client_ioctl(client, &args, sizeof(args));
		client->version = args.nop.version;
	}

	if (ret)
		nvif_client_fini(client);
	return ret;
}
