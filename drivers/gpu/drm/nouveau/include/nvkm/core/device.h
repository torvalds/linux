/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DEVICE_H__
#define __NVKM_DEVICE_H__
#include <core/oclass.h>
enum nvkm_subdev_type;

enum nvkm_device_type {
	NVKM_DEVICE_PCI,
	NVKM_DEVICE_AGP,
	NVKM_DEVICE_PCIE,
	NVKM_DEVICE_TEGRA,
};

struct nvkm_device {
	const struct nvkm_device_func *func;
	const struct nvkm_device_quirk *quirk;
	struct device *dev;
	enum nvkm_device_type type;
	u64 handle;
	const char *name;
	const char *cfgopt;
	const char *dbgopt;

	struct list_head head;
	struct mutex mutex;
	int refcount;

	void __iomem *pri;

	u32 debug;

	const struct nvkm_device_chip *chip;
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
		GP100    = 0x130,
		GV100    = 0x140,
		TU100    = 0x160,
		GA100    = 0x170,
	} card_type;
	u32 chipset;
	u8  chiprev;
	u32 crystal;

	struct {
		struct notifier_block nb;
	} acpi;

#define NVKM_LAYOUT_ONCE(type,data,ptr) data *ptr;
#define NVKM_LAYOUT_INST(type,data,ptr,cnt) data *ptr[cnt];
#include <core/layout.h>
#undef NVKM_LAYOUT_INST
#undef NVKM_LAYOUT_ONCE
	struct list_head subdev;
};

struct nvkm_subdev *nvkm_device_subdev(struct nvkm_device *, int type, int inst);
struct nvkm_engine *nvkm_device_engine(struct nvkm_device *, int type, int inst);

struct nvkm_device_func {
	struct nvkm_device_pci *(*pci)(struct nvkm_device *);
	struct nvkm_device_tegra *(*tegra)(struct nvkm_device *);
	void *(*dtor)(struct nvkm_device *);
	int (*preinit)(struct nvkm_device *);
	int (*init)(struct nvkm_device *);
	void (*fini)(struct nvkm_device *, bool suspend);
	resource_size_t (*resource_addr)(struct nvkm_device *, unsigned bar);
	resource_size_t (*resource_size)(struct nvkm_device *, unsigned bar);
	bool cpu_coherent;
};

struct nvkm_device_quirk {
	u8 tv_pin_mask;
	u8 tv_gpio;
};

struct nvkm_device_chip {
	const char *name;
#define NVKM_LAYOUT_ONCE(type,data,ptr,...)                                                  \
	struct {                                                                             \
		u32 inst;                                                                    \
		int (*ctor)(struct nvkm_device *, enum nvkm_subdev_type, int inst, data **); \
	} ptr;
#define NVKM_LAYOUT_INST(A...) NVKM_LAYOUT_ONCE(A)
#include <core/layout.h>
#undef NVKM_LAYOUT_INST
#undef NVKM_LAYOUT_ONCE
};

struct nvkm_device *nvkm_device_find(u64 name);
int nvkm_device_list(u64 *name, int size);

/* privileged register interface accessor macros */
#define nvkm_rd08(d,a) ioread8((d)->pri + (a))
#define nvkm_rd16(d,a) ioread16_native((d)->pri + (a))
#define nvkm_rd32(d,a) ioread32_native((d)->pri + (a))
#define nvkm_wr08(d,a,v) iowrite8((v), (d)->pri + (a))
#define nvkm_wr16(d,a,v) iowrite16_native((v), (d)->pri + (a))
#define nvkm_wr32(d,a,v) iowrite32_native((v), (d)->pri + (a))
#define nvkm_mask(d,a,m,v) ({                                                  \
	struct nvkm_device *_device = (d);                                     \
	u32 _addr = (a), _temp = nvkm_rd32(_device, _addr);                    \
	nvkm_wr32(_device, _addr, (_temp & ~(m)) | (v));                       \
	_temp;                                                                 \
})

void nvkm_device_del(struct nvkm_device **);

struct nvkm_device_oclass {
	int (*ctor)(struct nvkm_device *, const struct nvkm_oclass *,
		    void *data, u32 size, struct nvkm_object **);
	struct nvkm_sclass base;
};

extern const struct nvkm_sclass nvkm_udevice_sclass;

/* device logging */
#define nvdev_printk_(d,l,p,f,a...) do {                                       \
	const struct nvkm_device *_device = (d);                               \
	if (_device->debug >= (l))                                             \
		dev_##p(_device->dev, f, ##a);                                 \
} while(0)
#define nvdev_printk(d,l,p,f,a...) nvdev_printk_((d), NV_DBG_##l, p, f, ##a)
#define nvdev_fatal(d,f,a...) nvdev_printk((d), FATAL,   crit, f, ##a)
#define nvdev_error(d,f,a...) nvdev_printk((d), ERROR,    err, f, ##a)
#define nvdev_warn(d,f,a...)  nvdev_printk((d),  WARN, notice, f, ##a)
#define nvdev_info(d,f,a...)  nvdev_printk((d),  INFO,   info, f, ##a)
#define nvdev_debug(d,f,a...) nvdev_printk((d), DEBUG,   info, f, ##a)
#define nvdev_trace(d,f,a...) nvdev_printk((d), TRACE,   info, f, ##a)
#define nvdev_spam(d,f,a...)  nvdev_printk((d),  SPAM,    dbg, f, ##a)
#endif
