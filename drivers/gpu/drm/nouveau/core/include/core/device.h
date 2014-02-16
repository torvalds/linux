#ifndef __NOUVEAU_DEVICE_H__
#define __NOUVEAU_DEVICE_H__

#include <core/object.h>
#include <core/subdev.h>
#include <core/engine.h>

enum nv_subdev_type {
	NVDEV_ENGINE_DEVICE,
	NVDEV_SUBDEV_VBIOS,

	/* All subdevs from DEVINIT to DEVINIT_LAST will be created before
	 * *any* of them are initialised.  This subdev category is used
	 * for any subdevs that the VBIOS init table parsing may call out
	 * to during POST.
	 */
	NVDEV_SUBDEV_DEVINIT,
	NVDEV_SUBDEV_GPIO,
	NVDEV_SUBDEV_I2C,
	NVDEV_SUBDEV_DEVINIT_LAST = NVDEV_SUBDEV_I2C,

	/* This grouping of subdevs are initialised right after they've
	 * been created, and are allowed to assume any subdevs in the
	 * list above them exist and have been initialised.
	 */
	NVDEV_SUBDEV_MXM,
	NVDEV_SUBDEV_MC,
	NVDEV_SUBDEV_BUS,
	NVDEV_SUBDEV_TIMER,
	NVDEV_SUBDEV_FB,
	NVDEV_SUBDEV_LTCG,
	NVDEV_SUBDEV_IBUS,
	NVDEV_SUBDEV_INSTMEM,
	NVDEV_SUBDEV_VM,
	NVDEV_SUBDEV_BAR,
	NVDEV_SUBDEV_PWR,
	NVDEV_SUBDEV_VOLT,
	NVDEV_SUBDEV_THERM,
	NVDEV_SUBDEV_CLOCK,

	NVDEV_ENGINE_FIRST,
	NVDEV_ENGINE_DMAOBJ = NVDEV_ENGINE_FIRST,
	NVDEV_ENGINE_FIFO,
	NVDEV_ENGINE_SW,
	NVDEV_ENGINE_GR,
	NVDEV_ENGINE_MPEG,
	NVDEV_ENGINE_ME,
	NVDEV_ENGINE_VP,
	NVDEV_ENGINE_CRYPT,
	NVDEV_ENGINE_BSP,
	NVDEV_ENGINE_PPP,
	NVDEV_ENGINE_COPY0,
	NVDEV_ENGINE_COPY1,
	NVDEV_ENGINE_COPY2,
	NVDEV_ENGINE_VIC,
	NVDEV_ENGINE_VENC,
	NVDEV_ENGINE_DISP,
	NVDEV_ENGINE_PERFMON,

	NVDEV_SUBDEV_NR,
};

struct nouveau_device {
	struct nouveau_engine base;
	struct list_head head;

	struct pci_dev *pdev;
	u64 handle;

	const char *cfgopt;
	const char *dbgopt;
	const char *name;
	const char *cname;
	u64 disable_mask;

	enum {
		NV_04    = 0x04,
		NV_10    = 0x10,
		NV_11    = 0x11,
		NV_20    = 0x20,
		NV_30    = 0x30,
		NV_40    = 0x40,
		NV_50    = 0x50,
		NV_C0    = 0xc0,
		NV_D0    = 0xd0,
		NV_E0    = 0xe0,
	} card_type;
	u32 chipset;
	u32 crystal;

	struct nouveau_oclass *oclass[NVDEV_SUBDEV_NR];
	struct nouveau_object *subdev[NVDEV_SUBDEV_NR];
};

static inline struct nouveau_device *
nv_device(void *obj)
{
	struct nouveau_object *object = nv_object(obj);
	struct nouveau_object *device = object;

	if (device->engine)
		device = device->engine;
	if (device->parent)
		device = device->parent;

#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(device, NV_SUBDEV_CLASS) ||
		     (nv_hclass(device) & 0xff) != NVDEV_ENGINE_DEVICE)) {
		nv_assert("BAD CAST -> NvDevice, 0x%08x 0x%08x",
			  nv_hclass(object), nv_hclass(device));
	}
#endif

	return (void *)device;
}

static inline struct nouveau_subdev *
nouveau_subdev(void *obj, int sub)
{
	if (nv_device(obj)->subdev[sub])
		return nv_subdev(nv_device(obj)->subdev[sub]);
	return NULL;
}

static inline struct nouveau_engine *
nouveau_engine(void *obj, int sub)
{
	struct nouveau_subdev *subdev = nouveau_subdev(obj, sub);
	if (subdev && nv_iclass(subdev, NV_ENGINE_CLASS))
		return nv_engine(subdev);
	return NULL;
}

static inline bool
nv_device_match(struct nouveau_object *object, u16 dev, u16 ven, u16 sub)
{
	struct nouveau_device *device = nv_device(object);
	return device->pdev->device == dev &&
	       device->pdev->subsystem_vendor == ven &&
	       device->pdev->subsystem_device == sub;
}

#endif
