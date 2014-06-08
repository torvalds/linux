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

#include <core/object.h>
#include <core/device.h>
#include <core/client.h>
#include <core/option.h>

#include <core/class.h>

#include "priv.h"

static DEFINE_MUTEX(nv_devices_mutex);
static LIST_HEAD(nv_devices);

struct nouveau_device *
nouveau_device_find(u64 name)
{
	struct nouveau_device *device, *match = NULL;
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

/******************************************************************************
 * nouveau_devobj (0x0080): class implementation
 *****************************************************************************/
struct nouveau_devobj {
	struct nouveau_parent base;
	struct nouveau_object *subdev[NVDEV_SUBDEV_NR];
};

static const u64 disable_map[] = {
	[NVDEV_SUBDEV_VBIOS]	= NV_DEVICE_DISABLE_VBIOS,
	[NVDEV_SUBDEV_DEVINIT]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_GPIO]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_I2C]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_CLOCK]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_MXM]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_MC]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_BUS]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_TIMER]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_FB]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_LTCG]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_IBUS]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_INSTMEM]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_VM]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_BAR]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_VOLT]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_THERM]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_SUBDEV_PWR]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_ENGINE_DMAOBJ]	= NV_DEVICE_DISABLE_CORE,
	[NVDEV_ENGINE_PERFMON]  = NV_DEVICE_DISABLE_CORE,
	[NVDEV_ENGINE_FIFO]	= NV_DEVICE_DISABLE_FIFO,
	[NVDEV_ENGINE_SW]	= NV_DEVICE_DISABLE_FIFO,
	[NVDEV_ENGINE_GR]	= NV_DEVICE_DISABLE_GRAPH,
	[NVDEV_ENGINE_MPEG]	= NV_DEVICE_DISABLE_MPEG,
	[NVDEV_ENGINE_ME]	= NV_DEVICE_DISABLE_ME,
	[NVDEV_ENGINE_VP]	= NV_DEVICE_DISABLE_VP,
	[NVDEV_ENGINE_CRYPT]	= NV_DEVICE_DISABLE_CRYPT,
	[NVDEV_ENGINE_BSP]	= NV_DEVICE_DISABLE_BSP,
	[NVDEV_ENGINE_PPP]	= NV_DEVICE_DISABLE_PPP,
	[NVDEV_ENGINE_COPY0]	= NV_DEVICE_DISABLE_COPY0,
	[NVDEV_ENGINE_COPY1]	= NV_DEVICE_DISABLE_COPY1,
	[NVDEV_ENGINE_VIC]	= NV_DEVICE_DISABLE_VIC,
	[NVDEV_ENGINE_VENC]	= NV_DEVICE_DISABLE_VENC,
	[NVDEV_ENGINE_DISP]	= NV_DEVICE_DISABLE_DISP,
	[NVDEV_SUBDEV_NR]	= 0,
};

static int
nouveau_devobj_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nouveau_client *client = nv_client(parent);
	struct nouveau_device *device;
	struct nouveau_devobj *devobj;
	struct nv_device_class *args = data;
	u32 boot0, strap;
	u64 disable, mmio_base, mmio_size;
	void __iomem *map;
	int ret, i, c;

	if (size < sizeof(struct nv_device_class))
		return -EINVAL;

	/* find the device subdev that matches what the client requested */
	device = nv_device(client->device);
	if (args->device != ~0) {
		device = nouveau_device_find(args->device);
		if (!device)
			return -ENODEV;
	}

	ret = nouveau_parent_create(parent, nv_object(device), oclass, 0,
				    nouveau_control_oclass,
				    (1ULL << NVDEV_ENGINE_DMAOBJ) |
				    (1ULL << NVDEV_ENGINE_FIFO) |
				    (1ULL << NVDEV_ENGINE_DISP) |
				    (1ULL << NVDEV_ENGINE_PERFMON), &devobj);
	*pobject = nv_object(devobj);
	if (ret)
		return ret;

	mmio_base = nv_device_resource_start(device, 0);
	mmio_size = nv_device_resource_len(device, 0);

	/* translate api disable mask into internal mapping */
	disable = args->debug0;
	for (i = 0; i < NVDEV_SUBDEV_NR; i++) {
		if (args->disable & disable_map[i])
			disable |= (1ULL << i);
	}

	/* identify the chipset, and determine classes of subdev/engines */
	if (!(args->disable & NV_DEVICE_DISABLE_IDENTIFY) &&
	    !device->card_type) {
		map = ioremap(mmio_base, 0x102000);
		if (map == NULL)
			return -ENOMEM;

		/* switch mmio to cpu's native endianness */
#ifndef __BIG_ENDIAN
		if (ioread32_native(map + 0x000004) != 0x00000000)
#else
		if (ioread32_native(map + 0x000004) == 0x00000000)
#endif
			iowrite32_native(0x01000001, map + 0x000004);

		/* read boot0 and strapping information */
		boot0 = ioread32_native(map + 0x000000);
		strap = ioread32_native(map + 0x101000);
		iounmap(map);

		/* determine chipset and derive architecture from it */
		if ((boot0 & 0x1f000000) > 0) {
			device->chipset = (boot0 & 0x1ff00000) >> 20;
			switch (device->chipset & 0x1f0) {
			case 0x010: {
				if (0x461 & (1 << (device->chipset & 0xf)))
					device->card_type = NV_10;
				else
					device->card_type = NV_11;
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
			case 0x0c0: device->card_type = NV_C0; break;
			case 0x0d0: device->card_type = NV_D0; break;
			case 0x0e0:
			case 0x0f0:
			case 0x100: device->card_type = NV_E0; break;
			case 0x110: device->card_type = GM100; break;
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
		case NV_C0:
		case NV_D0: ret = nvc0_identify(device); break;
		case NV_E0: ret = nve0_identify(device); break;
		case GM100: ret = gm100_identify(device); break;
		default:
			ret = -EINVAL;
			break;
		}

		if (ret) {
			nv_error(device, "unknown chipset, 0x%08x\n", boot0);
			return ret;
		}

		nv_info(device, "BOOT0  : 0x%08x\n", boot0);
		nv_info(device, "Chipset: %s (NV%02X)\n",
			device->cname, device->chipset);
		nv_info(device, "Family : NV%02X\n", device->card_type);

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

		nv_debug(device, "crystal freq: %dKHz\n", device->crystal);
	}

	if (!(args->disable & NV_DEVICE_DISABLE_MMIO) &&
	    !nv_subdev(device)->mmio) {
		nv_subdev(device)->mmio  = ioremap(mmio_base, mmio_size);
		if (!nv_subdev(device)->mmio) {
			nv_error(device, "unable to map device registers\n");
			return -ENOMEM;
		}
	}

	/* ensure requested subsystems are available for use */
	for (i = 1, c = 1; i < NVDEV_SUBDEV_NR; i++) {
		if (!(oclass = device->oclass[i]) || (disable & (1ULL << i)))
			continue;

		if (device->subdev[i]) {
			nouveau_object_ref(device->subdev[i],
					  &devobj->subdev[i]);
			continue;
		}

		ret = nouveau_object_ctor(nv_object(device), NULL,
					  oclass, NULL, i,
					  &devobj->subdev[i]);
		if (ret == -ENODEV)
			continue;
		if (ret)
			return ret;

		device->subdev[i] = devobj->subdev[i];

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
			struct nouveau_object *subdev = devobj->subdev[c++];
			if (subdev && !nv_iclass(subdev, NV_ENGINE_CLASS)) {
				ret = nouveau_object_inc(subdev);
				if (ret)
					return ret;
				atomic_dec(&nv_object(device)->usecount);
			} else
			if (subdev) {
				nouveau_subdev_reset(subdev);
			}
		}
	}

	return 0;
}

static void
nouveau_devobj_dtor(struct nouveau_object *object)
{
	struct nouveau_devobj *devobj = (void *)object;
	int i;

	for (i = NVDEV_SUBDEV_NR - 1; i >= 0; i--)
		nouveau_object_ref(NULL, &devobj->subdev[i]);

	nouveau_parent_destroy(&devobj->base);
}

static u8
nouveau_devobj_rd08(struct nouveau_object *object, u64 addr)
{
	return nv_rd08(object->engine, addr);
}

static u16
nouveau_devobj_rd16(struct nouveau_object *object, u64 addr)
{
	return nv_rd16(object->engine, addr);
}

static u32
nouveau_devobj_rd32(struct nouveau_object *object, u64 addr)
{
	return nv_rd32(object->engine, addr);
}

static void
nouveau_devobj_wr08(struct nouveau_object *object, u64 addr, u8 data)
{
	nv_wr08(object->engine, addr, data);
}

static void
nouveau_devobj_wr16(struct nouveau_object *object, u64 addr, u16 data)
{
	nv_wr16(object->engine, addr, data);
}

static void
nouveau_devobj_wr32(struct nouveau_object *object, u64 addr, u32 data)
{
	nv_wr32(object->engine, addr, data);
}

static struct nouveau_ofuncs
nouveau_devobj_ofuncs = {
	.ctor = nouveau_devobj_ctor,
	.dtor = nouveau_devobj_dtor,
	.init = _nouveau_parent_init,
	.fini = _nouveau_parent_fini,
	.rd08 = nouveau_devobj_rd08,
	.rd16 = nouveau_devobj_rd16,
	.rd32 = nouveau_devobj_rd32,
	.wr08 = nouveau_devobj_wr08,
	.wr16 = nouveau_devobj_wr16,
	.wr32 = nouveau_devobj_wr32,
};

/******************************************************************************
 * nouveau_device: engine functions
 *****************************************************************************/
static struct nouveau_oclass
nouveau_device_sclass[] = {
	{ 0x0080, &nouveau_devobj_ofuncs },
	{}
};

static int
nouveau_device_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_device *device = (void *)object;
	struct nouveau_object *subdev;
	int ret, i;

	for (i = NVDEV_SUBDEV_NR - 1; i >= 0; i--) {
		if ((subdev = device->subdev[i])) {
			if (!nv_iclass(subdev, NV_ENGINE_CLASS)) {
				ret = nouveau_object_dec(subdev, suspend);
				if (ret && suspend)
					goto fail;
			}
		}
	}

	ret = 0;
fail:
	for (; ret && i < NVDEV_SUBDEV_NR; i++) {
		if ((subdev = device->subdev[i])) {
			if (!nv_iclass(subdev, NV_ENGINE_CLASS)) {
				ret = nouveau_object_inc(subdev);
				if (ret) {
					/* XXX */
				}
			}
		}
	}

	return ret;
}

static int
nouveau_device_init(struct nouveau_object *object)
{
	struct nouveau_device *device = (void *)object;
	struct nouveau_object *subdev;
	int ret, i;

	for (i = 0; i < NVDEV_SUBDEV_NR; i++) {
		if ((subdev = device->subdev[i])) {
			if (!nv_iclass(subdev, NV_ENGINE_CLASS)) {
				ret = nouveau_object_inc(subdev);
				if (ret)
					goto fail;
			} else {
				nouveau_subdev_reset(subdev);
			}
		}
	}

	ret = 0;
fail:
	for (--i; ret && i >= 0; i--) {
		if ((subdev = device->subdev[i])) {
			if (!nv_iclass(subdev, NV_ENGINE_CLASS))
				nouveau_object_dec(subdev, false);
		}
	}

	return ret;
}

static void
nouveau_device_dtor(struct nouveau_object *object)
{
	struct nouveau_device *device = (void *)object;

	mutex_lock(&nv_devices_mutex);
	list_del(&device->head);
	mutex_unlock(&nv_devices_mutex);

	if (nv_subdev(device)->mmio)
		iounmap(nv_subdev(device)->mmio);

	nouveau_engine_destroy(&device->base);
}

resource_size_t
nv_device_resource_start(struct nouveau_device *device, unsigned int bar)
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
nv_device_resource_len(struct nouveau_device *device, unsigned int bar)
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

dma_addr_t
nv_device_map_page(struct nouveau_device *device, struct page *page)
{
	dma_addr_t ret;

	if (nv_device_is_pci(device)) {
		ret = pci_map_page(device->pdev, page, 0, PAGE_SIZE,
				   PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(device->pdev, ret))
			ret = 0;
	} else {
		ret = page_to_phys(page);
	}

	return ret;
}

void
nv_device_unmap_page(struct nouveau_device *device, dma_addr_t addr)
{
	if (nv_device_is_pci(device))
		pci_unmap_page(device->pdev, addr, PAGE_SIZE,
			       PCI_DMA_BIDIRECTIONAL);
}

int
nv_device_get_irq(struct nouveau_device *device, bool stall)
{
	if (nv_device_is_pci(device)) {
		return device->pdev->irq;
	} else {
		return platform_get_irq_byname(device->platformdev,
					       stall ? "stall" : "nonstall");
	}
}

static struct nouveau_oclass
nouveau_device_oclass = {
	.handle = NV_ENGINE(DEVICE, 0x00),
	.ofuncs = &(struct nouveau_ofuncs) {
		.dtor = nouveau_device_dtor,
		.init = nouveau_device_init,
		.fini = nouveau_device_fini,
	},
};

int
nouveau_device_create_(void *dev, enum nv_bus_type type, u64 name,
		       const char *sname, const char *cfg, const char *dbg,
		       int length, void **pobject)
{
	struct nouveau_device *device;
	int ret = -EEXIST;

	mutex_lock(&nv_devices_mutex);
	list_for_each_entry(device, &nv_devices, head) {
		if (device->handle == name)
			goto done;
	}

	ret = nouveau_engine_create_(NULL, NULL, &nouveau_device_oclass, true,
				     "DEVICE", "device", length, pobject);
	device = *pobject;
	if (ret)
		goto done;

	switch (type) {
	case NOUVEAU_BUS_PCI:
		device->pdev = dev;
		break;
	case NOUVEAU_BUS_PLATFORM:
		device->platformdev = dev;
		break;
	}
	device->handle = name;
	device->cfgopt = cfg;
	device->dbgopt = dbg;
	device->name = sname;

	nv_subdev(device)->debug = nouveau_dbgopt(device->dbgopt, "DEVICE");
	nv_engine(device)->sclass = nouveau_device_sclass;
	list_add(&device->head, &nv_devices);
done:
	mutex_unlock(&nv_devices_mutex);
	return ret;
}
