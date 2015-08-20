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
#include "acpi.h"

#include <core/notify.h>
#include <core/option.h>

#include <subdev/bios.h>

static DEFINE_MUTEX(nv_devices_mutex);
static LIST_HEAD(nv_devices);

struct nvkm_device *
nvkm_device_find(u64 name)
{
	struct nvkm_device *device, *match = NULL;
	mutex_lock(&nv_devices_mutex);
	list_for_each_entry(device, &nv_devices, head) {
		if (device->handle == name) {
			match = device;
			break;
		}
	}
	mutex_unlock(&nv_devices_mutex);
	return match;
}

int
nvkm_device_list(u64 *name, int size)
{
	struct nvkm_device *device;
	int nr = 0;
	mutex_lock(&nv_devices_mutex);
	list_for_each_entry(device, &nv_devices, head) {
		if (nr++ < size)
			name[nr - 1] = device->handle;
	}
	mutex_unlock(&nv_devices_mutex);
	return nr;
}

#include <core/parent.h>

struct nvkm_device *
nv_device(void *obj)
{
	struct nvkm_object *device = nv_object(obj);

	if (device->engine == NULL) {
		while (device && device->parent) {
			if (nv_mclass(device) == 0x0080) {
				struct {
					struct nvkm_parent base;
					struct nvkm_device *device;
				} *udevice = (void *)device;
				return udevice->device;
			}
			device = device->parent;
		}
	} else {
		device = &nv_object(obj)->engine->subdev.object;
		if (device && device->parent)
			device = device->parent;
	}
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	BUG_ON(!device);
#endif
	return (void *)device;
}

static int
nvkm_device_event_ctor(struct nvkm_object *object, void *data, u32 size,
		       struct nvkm_notify *notify)
{
	if (!WARN_ON(size != 0)) {
		notify->size  = 0;
		notify->types = 1;
		notify->index = 0;
		return 0;
	}
	return -EINVAL;
}

static const struct nvkm_event_func
nvkm_device_event_func = {
	.ctor = nvkm_device_event_ctor,
};

int
nvkm_device_fini(struct nvkm_device *device, bool suspend)
{
	struct nvkm_object *subdev;
	int ret, i;

	for (i = NVDEV_SUBDEV_NR - 1; i >= 0; i--) {
		if ((subdev = device->subdev[i])) {
			if (!nv_iclass(subdev, NV_ENGINE_CLASS)) {
				ret = nvkm_object_dec(subdev, suspend);
				if (ret && suspend)
					goto fail;
			}
		}
	}

	ret = nvkm_acpi_fini(device, suspend);
fail:
	for (; ret && i < NVDEV_SUBDEV_NR; i++) {
		if ((subdev = device->subdev[i])) {
			if (!nv_iclass(subdev, NV_ENGINE_CLASS)) {
				ret = nvkm_object_inc(subdev);
				if (ret) {
					/* XXX */
				}
			}
		}
	}

	return ret;
}

int
nvkm_device_init(struct nvkm_device *device)
{
	struct nvkm_object *subdev;
	int ret, i = 0, c;

	ret = nvkm_acpi_init(device);
	if (ret)
		goto fail;

	for (i = 0, c = 0; i < NVDEV_SUBDEV_NR; i++) {
#define _(s,m) case s: if (device->oclass[s] && !device->subdev[s]) {          \
		ret = nvkm_object_ctor(nv_object(device), NULL,                \
				       device->oclass[s], NULL,  (s),          \
				       (struct nvkm_object **)&device->m);     \
		if (ret == -ENODEV) {                                          \
			device->oclass[s] = NULL;                              \
			continue;                                              \
		}                                                              \
		if (ret)                                                       \
			goto fail;                                             \
		device->subdev[s] = (struct nvkm_object *)device->m;           \
} break
		switch (i) {
		_(NVDEV_SUBDEV_BAR    ,     bar);
		_(NVDEV_SUBDEV_VBIOS  ,    bios);
		_(NVDEV_SUBDEV_BUS    ,     bus);
		_(NVDEV_SUBDEV_CLK    ,     clk);
		_(NVDEV_SUBDEV_DEVINIT, devinit);
		_(NVDEV_SUBDEV_FB     ,      fb);
		_(NVDEV_SUBDEV_FUSE   ,    fuse);
		_(NVDEV_SUBDEV_GPIO   ,    gpio);
		_(NVDEV_SUBDEV_I2C    ,     i2c);
		_(NVDEV_SUBDEV_IBUS   ,    ibus);
		_(NVDEV_SUBDEV_INSTMEM,    imem);
		_(NVDEV_SUBDEV_LTC    ,     ltc);
		_(NVDEV_SUBDEV_MC     ,      mc);
		_(NVDEV_SUBDEV_MMU    ,     mmu);
		_(NVDEV_SUBDEV_MXM    ,     mxm);
		_(NVDEV_SUBDEV_PMU    ,     pmu);
		_(NVDEV_SUBDEV_THERM  ,   therm);
		_(NVDEV_SUBDEV_TIMER  ,   timer);
		_(NVDEV_SUBDEV_VOLT   ,    volt);
		_(NVDEV_ENGINE_BSP    ,     bsp);
		_(NVDEV_ENGINE_CE0    ,   ce[0]);
		_(NVDEV_ENGINE_CE1    ,   ce[1]);
		_(NVDEV_ENGINE_CE2    ,   ce[2]);
		_(NVDEV_ENGINE_CIPHER ,  cipher);
		_(NVDEV_ENGINE_DISP   ,    disp);
		_(NVDEV_ENGINE_DMAOBJ ,     dma);
		_(NVDEV_ENGINE_FIFO   ,    fifo);
		_(NVDEV_ENGINE_GR     ,      gr);
		_(NVDEV_ENGINE_IFB    ,     ifb);
		_(NVDEV_ENGINE_ME     ,      me);
		_(NVDEV_ENGINE_MPEG   ,    mpeg);
		_(NVDEV_ENGINE_MSENC  ,   msenc);
		_(NVDEV_ENGINE_MSPDEC ,  mspdec);
		_(NVDEV_ENGINE_MSPPP  ,   msppp);
		_(NVDEV_ENGINE_MSVLD  ,   msvld);
		_(NVDEV_ENGINE_PM     ,      pm);
		_(NVDEV_ENGINE_SEC    ,     sec);
		_(NVDEV_ENGINE_SW     ,      sw);
		_(NVDEV_ENGINE_VIC    ,     vic);
		_(NVDEV_ENGINE_VP     ,      vp);
		default:
			WARN_ON(1);
			continue;
		}
#undef _

		/* note: can't init *any* subdevs until devinit has been run
		 * due to not knowing exactly what the vbios init tables will
		 * mess with.  devinit also can't be run until all of its
		 * dependencies have been created.
		 *
		 * this code delays init of any subdev until all of devinit's
		 * dependencies have been created, and then initialises each
		 * subdev in turn as they're created.
		 */
		while (i >= NVDEV_SUBDEV_DEVINIT_LAST && c <= i) {
			struct nvkm_object *subdev = device->subdev[c++];
			if (subdev && !nv_iclass(subdev, NV_ENGINE_CLASS)) {
				ret = nvkm_object_inc(subdev);
				if (ret)
					goto fail;
			} else
			if (subdev) {
				nvkm_subdev_reset(subdev);
			}
		}
	}

	ret = 0;
fail:
	for (--i; ret && i >= 0; i--) {
		if ((subdev = device->subdev[i])) {
			if (!nv_iclass(subdev, NV_ENGINE_CLASS))
				nvkm_object_dec(subdev, false);
		}
	}

	if (ret)
		nvkm_acpi_fini(device, false);
	return ret;
}

resource_size_t
nv_device_resource_start(struct nvkm_device *device, unsigned int bar)
{
	if (nv_device_is_pci(device)) {
		return pci_resource_start(device->pdev, bar);
	} else {
		struct resource *res;
		res = platform_get_resource(device->platformdev,
					    IORESOURCE_MEM, bar);
		if (!res)
			return 0;
		return res->start;
	}
}

resource_size_t
nv_device_resource_len(struct nvkm_device *device, unsigned int bar)
{
	if (nv_device_is_pci(device)) {
		return pci_resource_len(device->pdev, bar);
	} else {
		struct resource *res;
		res = platform_get_resource(device->platformdev,
					    IORESOURCE_MEM, bar);
		if (!res)
			return 0;
		return resource_size(res);
	}
}

int
nv_device_get_irq(struct nvkm_device *device, bool stall)
{
	if (nv_device_is_pci(device)) {
		return device->pdev->irq;
	} else {
		return platform_get_irq_byname(device->platformdev,
					       stall ? "stall" : "nonstall");
	}
}

static struct nvkm_oclass
nvkm_device_oclass = {
	.ofuncs = &(struct nvkm_ofuncs) {
	},
};

void
nvkm_device_del(struct nvkm_device **pdevice)
{
	struct nvkm_device *device = *pdevice;
	int i;
	if (device) {
		mutex_lock(&nv_devices_mutex);
		for (i = NVDEV_SUBDEV_NR - 1; i >= 0; i--)
			nvkm_object_ref(NULL, &device->subdev[i]);

		nvkm_event_fini(&device->event);

		if (device->pri)
			iounmap(device->pri);
		list_del(&device->head);
		mutex_unlock(&nv_devices_mutex);

		nvkm_engine_destroy(&device->engine);
		*pdevice = NULL;
	}
}

int
nvkm_device_new(void *dev, enum nv_bus_type type, u64 name,
		const char *sname, const char *cfg, const char *dbg,
		bool detect, bool mmio, u64 subdev_mask,
		struct nvkm_device **pdevice)
{
	struct nvkm_device *device;
	u64 mmio_base, mmio_size;
	u32 boot0, strap;
	void __iomem *map;
	int ret = -EEXIST;
	int i;

	mutex_lock(&nv_devices_mutex);
	list_for_each_entry(device, &nv_devices, head) {
		if (device->handle == name)
			goto done;
	}

	ret = nvkm_engine_create(NULL, NULL, &nvkm_device_oclass, true,
				 "DEVICE", "device", &device);
	*pdevice = device;
	if (ret)
		goto done;

	switch (type) {
	case NVKM_BUS_PCI:
		device->pdev = dev;
		device->dev = &device->pdev->dev;
		break;
	case NVKM_BUS_PLATFORM:
		device->platformdev = dev;
		device->dev = &device->platformdev->dev;
		break;
	}
	device->handle = name;
	device->cfgopt = cfg;
	device->dbgopt = dbg;
	device->name = sname;

	nv_subdev(device)->debug = nvkm_dbgopt(device->dbgopt, "DEVICE");
	list_add_tail(&device->head, &nv_devices);

	ret = nvkm_event_init(&nvkm_device_event_func, 1, 1, &device->event);
	if (ret)
		goto done;

	mmio_base = nv_device_resource_start(device, 0);
	mmio_size = nv_device_resource_len(device, 0);

	/* identify the chipset, and determine classes of subdev/engines */
	if (detect) {
		map = ioremap(mmio_base, 0x102000);
		if (ret = -ENOMEM, map == NULL)
			goto done;

		/* switch mmio to cpu's native endianness */
#ifndef __BIG_ENDIAN
		if (ioread32_native(map + 0x000004) != 0x00000000) {
#else
		if (ioread32_native(map + 0x000004) == 0x00000000) {
#endif
			iowrite32_native(0x01000001, map + 0x000004);
			ioread32_native(map);
		}

		/* read boot0 and strapping information */
		boot0 = ioread32_native(map + 0x000000);
		strap = ioread32_native(map + 0x101000);
		iounmap(map);

		/* determine chipset and derive architecture from it */
		if ((boot0 & 0x1f000000) > 0) {
			device->chipset = (boot0 & 0x1ff00000) >> 20;
			device->chiprev = (boot0 & 0x000000ff);
			switch (device->chipset & 0x1f0) {
			case 0x010: {
				if (0x461 & (1 << (device->chipset & 0xf)))
					device->card_type = NV_10;
				else
					device->card_type = NV_11;
				device->chiprev = 0x00;
				break;
			}
			case 0x020: device->card_type = NV_20; break;
			case 0x030: device->card_type = NV_30; break;
			case 0x040:
			case 0x060: device->card_type = NV_40; break;
			case 0x050:
			case 0x080:
			case 0x090:
			case 0x0a0: device->card_type = NV_50; break;
			case 0x0c0:
			case 0x0d0: device->card_type = NV_C0; break;
			case 0x0e0:
			case 0x0f0:
			case 0x100: device->card_type = NV_E0; break;
			case 0x110:
			case 0x120: device->card_type = GM100; break;
			default:
				break;
			}
		} else
		if ((boot0 & 0xff00fff0) == 0x20004000) {
			if (boot0 & 0x00f00000)
				device->chipset = 0x05;
			else
				device->chipset = 0x04;
			device->card_type = NV_04;
		}

		switch (device->card_type) {
		case NV_04: ret = nv04_identify(device); break;
		case NV_10:
		case NV_11: ret = nv10_identify(device); break;
		case NV_20: ret = nv20_identify(device); break;
		case NV_30: ret = nv30_identify(device); break;
		case NV_40: ret = nv40_identify(device); break;
		case NV_50: ret = nv50_identify(device); break;
		case NV_C0: ret = gf100_identify(device); break;
		case NV_E0: ret = gk104_identify(device); break;
		case GM100: ret = gm100_identify(device); break;
		default:
			ret = -EINVAL;
			break;
		}

		if (ret) {
			nvdev_error(device, "unknown chipset (%08x)\n", boot0);
			goto done;
		}

		nvdev_info(device, "NVIDIA %s (%08x)\n", device->cname, boot0);

		/* determine frequency of timing crystal */
		if ( device->card_type <= NV_10 || device->chipset < 0x17 ||
		    (device->chipset >= 0x20 && device->chipset < 0x25))
			strap &= 0x00000040;
		else
			strap &= 0x00400040;

		switch (strap) {
		case 0x00000000: device->crystal = 13500; break;
		case 0x00000040: device->crystal = 14318; break;
		case 0x00400000: device->crystal = 27000; break;
		case 0x00400040: device->crystal = 25000; break;
		}
	} else {
		device->cname = "NULL";
		device->oclass[NVDEV_SUBDEV_VBIOS] = &nvkm_bios_oclass;
	}

	if (mmio) {
		device->pri = ioremap(mmio_base, mmio_size);
		if (!device->pri) {
			nvdev_error(device, "unable to map PRI\n");
			return -ENOMEM;
		}
	}

	/* disable subdevs that aren't required (used by tools) */
	for (i = 0; i < NVDEV_SUBDEV_NR; i++) {
		if (!(subdev_mask & (1ULL << i)))
			device->oclass[i] = NULL;
	}

	atomic_set(&device->engine.subdev.object.usecount, 2);
	mutex_init(&device->mutex);
done:
	mutex_unlock(&nv_devices_mutex);
	return ret;
}
