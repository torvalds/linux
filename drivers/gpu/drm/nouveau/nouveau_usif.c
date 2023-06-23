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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include "nouveau_drv.h"
#include "nouveau_usif.h"
#include "nouveau_abi16.h"

#include <nvif/unpack.h>
#include <nvif/client.h>
#include <nvif/ioctl.h>

#include <nvif/class.h>
#include <nvif/cl0080.h>

struct usif_object {
	struct list_head head;
	u8  route;
	u64 token;
};

static void
usif_object_dtor(struct usif_object *object)
{
	list_del(&object->head);
	kfree(object);
}

static int
usif_object_new(struct drm_file *f, void *data, u32 size, void *argv, u32 argc, bool parent_abi16)
{
	struct nouveau_cli *cli = nouveau_cli(f);
	struct nvif_client *client = &cli->base;
	union {
		struct nvif_ioctl_new_v0 v0;
	} *args = data;
	struct usif_object *object;
	int ret = -ENOSYS;

	if ((ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true)))
		return ret;

	switch (args->v0.oclass) {
	case NV_DMA_FROM_MEMORY:
	case NV_DMA_TO_MEMORY:
	case NV_DMA_IN_MEMORY:
		return -EINVAL;
	case NV_DEVICE: {
		union {
			struct nv_device_v0 v0;
		} *args = data;

		if ((ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false)))
			return ret;

		args->v0.priv = false;
		break;
	}
	default:
		if (!parent_abi16)
			return -EINVAL;
		break;
	}

	if (!(object = kmalloc(sizeof(*object), GFP_KERNEL)))
		return -ENOMEM;
	list_add(&object->head, &cli->objects);

	object->route = args->v0.route;
	object->token = args->v0.token;
	args->v0.route = NVDRM_OBJECT_USIF;
	args->v0.token = (unsigned long)(void *)object;
	ret = nvif_client_ioctl(client, argv, argc);
	if (ret) {
		usif_object_dtor(object);
		return ret;
	}

	args->v0.token = object->token;
	args->v0.route = object->route;
	return 0;
}

int
usif_ioctl(struct drm_file *filp, void __user *user, u32 argc)
{
	struct nouveau_cli *cli = nouveau_cli(filp);
	struct nvif_client *client = &cli->base;
	void *data = kmalloc(argc, GFP_KERNEL);
	u32   size = argc;
	union {
		struct nvif_ioctl_v0 v0;
	} *argv = data;
	struct usif_object *object;
	bool abi16 = false;
	u8 owner;
	int ret;

	if (ret = -ENOMEM, !argv)
		goto done;
	if (ret = -EFAULT, copy_from_user(argv, user, size))
		goto done;

	if (!(ret = nvif_unpack(-ENOSYS, &data, &size, argv->v0, 0, 0, true))) {
		/* block access to objects not created via this interface */
		owner = argv->v0.owner;
		if (argv->v0.object == 0ULL &&
		    argv->v0.type != NVIF_IOCTL_V0_DEL)
			argv->v0.owner = NVDRM_OBJECT_ANY; /* except client */
		else
			argv->v0.owner = NVDRM_OBJECT_USIF;
	} else
		goto done;

	/* USIF slightly abuses some return-only ioctl members in order
	 * to provide interoperability with the older ABI16 objects
	 */
	mutex_lock(&cli->mutex);
	if (argv->v0.route) {
		if (ret = -EINVAL, argv->v0.route == 0xff)
			ret = nouveau_abi16_usif(filp, argv, argc);
		if (ret) {
			mutex_unlock(&cli->mutex);
			goto done;
		}

		abi16 = true;
	}

	switch (argv->v0.type) {
	case NVIF_IOCTL_V0_NEW:
		ret = usif_object_new(filp, data, size, argv, argc, abi16);
		break;
	case NVIF_IOCTL_V0_NTFY_NEW:
	case NVIF_IOCTL_V0_NTFY_DEL:
	case NVIF_IOCTL_V0_NTFY_GET:
	case NVIF_IOCTL_V0_NTFY_PUT:
		ret = -ENOSYS;
		break;
	default:
		ret = nvif_client_ioctl(client, argv, argc);
		break;
	}
	if (argv->v0.route == NVDRM_OBJECT_USIF) {
		object = (void *)(unsigned long)argv->v0.token;
		argv->v0.route = object->route;
		argv->v0.token = object->token;
		if (ret == 0 && argv->v0.type == NVIF_IOCTL_V0_DEL) {
			list_del(&object->head);
			kfree(object);
		}
	} else {
		argv->v0.route = NVIF_IOCTL_V0_ROUTE_HIDDEN;
		argv->v0.token = 0;
	}
	argv->v0.owner = owner;
	mutex_unlock(&cli->mutex);

	if (copy_to_user(user, argv, argc))
		ret = -EFAULT;
done:
	kfree(argv);
	return ret;
}

void
usif_client_fini(struct nouveau_cli *cli)
{
	struct usif_object *object, *otemp;

	list_for_each_entry_safe(object, otemp, &cli->objects, head) {
		usif_object_dtor(object);
	}
}

void
usif_client_init(struct nouveau_cli *cli)
{
	INIT_LIST_HEAD(&cli->objects);
}
