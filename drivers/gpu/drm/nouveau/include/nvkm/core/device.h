#ifndef __NOUVEAU_DEVICE_H__
#define __NOUVEAU_DEVICE_H__

#include <core/object.h>
#include <core/subdev.h>
#include <core/engine.h>
#include <core/event.h>

enum nv_subdev_type {
	NVDEV_ENGINE_DEVICE,
	NVDEV_SUBDEV_VBIOS,

	/* All subdevs from DEVINIT to DEVINIT_LAST will be created before
	 * *any* of them are initialised.  This subdev category is used
	 * for any subdevs that the VBIOS init table parsing may call out
	 * to during POST.
	 */
	NVDEV_SUBDEV_DEVINIT,
	NVDEV_SUBDEV_IBUS,
	NVDEV_SUBDEV_GPIO,
	NVDEV_SUBDEV_I2C,
	NVDEV_SUBDEV_DEVINIT_LAST = NVDEV_SUBDEV_I2C,

	/* This grouping of subdevs are initialised right after they've
	 * been created, and are allowed to assume any subdevs in the
	 * list above them exist and have been initialised.
	 */
	NVDEV_SUBDEV_FUSE,
	NVDEV_SUBDEV_MXM,
	NVDEV_SUBDEV_MC,
	NVDEV_SUBDEV_BUS,
	NVDEV_SUBDEV_TIMER,
	NVDEV_SUBDEV_FB,
	NVDEV_SUBDEV_LTC,
	NVDEV_SUBDEV_INSTMEM,
	NVDEV_SUBDEV_MMU,
	NVDEV_SUBDEV_BAR,
	NVDEV_SUBDEV_PMU,
	NVDEV_SUBDEV_VOLT,
	NVDEV_SUBDEV_THERM,
	NVDEV_SUBDEV_CLK,

	NVDEV_ENGINE_FIRST,
	NVDEV_ENGINE_DMAOBJ = NVDEV_ENGINE_FIRST,
	NVDEV_ENGINE_IFB,
	NVDEV_ENGINE_FIFO,
	NVDEV_ENGINE_SW,
	NVDEV_ENGINE_GR,
	NVDEV_ENGINE_MPEG,
	NVDEV_ENGINE_ME,
	NVDEV_ENGINE_VP,
	NVDEV_ENGINE_CIPHER,
	NVDEV_ENGINE_BSP,
	NVDEV_ENGINE_MSPPP,
	NVDEV_ENGINE_CE0,
	NVDEV_ENGINE_CE1,
	NVDEV_ENGINE_CE2,
	NVDEV_ENGINE_VIC,
	NVDEV_ENGINE_MSENC,
	NVDEV_ENGINE_DISP,
	NVDEV_ENGINE_PM,
	NVDEV_ENGINE_MSVLD,
	NVDEV_ENGINE_SEC,

	NVDEV_SUBDEV_NR,
};

struct nouveau_device {
	struct nouveau_engine engine;
	struct list_head head;

	struct pci_dev *pdev;
	struct platform_device *platformdev;
	u64 handle;

	struct nvkm_event event;

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
		NV_E0    = 0xe0,
		GM100    = 0x110,
	} card_type;
	u32 chipset;
	u8  chiprev;
	u32 crystal;

	struct nouveau_oclass *oclass[NVDEV_SUBDEV_NR];
	struct nouveau_object *subdev[NVDEV_SUBDEV_NR];

	struct {
		struct notifier_block nb;
	} acpi;
};

int nouveau_device_list(u64 *name, int size);

struct nouveau_device *nv_device(void *obj);

static inline bool
nv_device_match(struct nouveau_object *object, u16 dev, u16 ven, u16 sub)
{
	struct nouveau_device *device = nv_device(object);
	return device->pdev->device == dev &&
	       device->pdev->subsystem_vendor == ven &&
	       device->pdev->subsystem_device == sub;
}

static inline bool
nv_device_is_pci(struct nouveau_device *device)
{
	return device->pdev != NULL;
}

static inline bool
nv_device_is_cpu_coherent(struct nouveau_device *device)
{
	return (!IS_ENABLED(CONFIG_ARM) && nv_device_is_pci(device));
}

static inline struct device *
nv_device_base(struct nouveau_device *device)
{
	return nv_device_is_pci(device) ? &device->pdev->dev :
					  &device->platformdev->dev;
}

resource_size_t
nv_device_resource_start(struct nouveau_device *device, unsigned int bar);

resource_size_t
nv_device_resource_len(struct nouveau_device *device, unsigned int bar);

int
nv_device_get_irq(struct nouveau_device *device, bool stall);

#endif
