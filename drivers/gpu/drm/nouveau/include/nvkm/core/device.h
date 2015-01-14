#ifndef __NOUVEAU_DEVICE_H__
#define __NOUVEAU_DEVICE_H__

#include <core/object.h>
#include <core/subdev.h>
#include <core/engine.h>
#include <core/event.h>

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
