/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_SUBDEV_H__
#define __NVKM_SUBDEV_H__
#include <core/device.h>

enum nvkm_subdev_type {
	NVKM_SUBDEV_PCI,
	NVKM_SUBDEV_VBIOS,
	NVKM_SUBDEV_DEVINIT,
	NVKM_SUBDEV_TOP,
	NVKM_SUBDEV_IBUS,
	NVKM_SUBDEV_GPIO,
	NVKM_SUBDEV_I2C,
	NVKM_SUBDEV_FUSE,
	NVKM_SUBDEV_MXM,
	NVKM_SUBDEV_MC,
	NVKM_SUBDEV_BUS,
	NVKM_SUBDEV_TIMER,
	NVKM_SUBDEV_INSTMEM,
	NVKM_SUBDEV_FB,
	NVKM_SUBDEV_LTC,
	NVKM_SUBDEV_MMU,
	NVKM_SUBDEV_BAR,
	NVKM_SUBDEV_FAULT,
	NVKM_SUBDEV_ACR,
	NVKM_SUBDEV_PMU,
	NVKM_SUBDEV_VOLT,
	NVKM_SUBDEV_ICCSENSE,
	NVKM_SUBDEV_THERM,
	NVKM_SUBDEV_CLK,
	NVKM_SUBDEV_GSP,

	NVKM_ENGINE_BSP,

	NVKM_ENGINE_CE0,
	NVKM_ENGINE_CE = NVKM_ENGINE_CE0,
	NVKM_ENGINE_CE1,
	NVKM_ENGINE_CE2,
	NVKM_ENGINE_CE3,
	NVKM_ENGINE_CE4,
	NVKM_ENGINE_CE5,
	NVKM_ENGINE_CE6,
	NVKM_ENGINE_CE7,
	NVKM_ENGINE_CE8,
	NVKM_ENGINE_CE_LAST = NVKM_ENGINE_CE8,

	NVKM_ENGINE_CIPHER,
	NVKM_ENGINE_DISP,
	NVKM_ENGINE_DMAOBJ,
	NVKM_ENGINE_FIFO,
	NVKM_ENGINE_GR,
	NVKM_ENGINE_IFB,
	NVKM_ENGINE_ME,
	NVKM_ENGINE_MPEG,
	NVKM_ENGINE_MSENC,
	NVKM_ENGINE_MSPDEC,
	NVKM_ENGINE_MSPPP,
	NVKM_ENGINE_MSVLD,

	NVKM_ENGINE_NVENC0,
	NVKM_ENGINE_NVENC = NVKM_ENGINE_NVENC0,
	NVKM_ENGINE_NVENC1,
	NVKM_ENGINE_NVENC2,
	NVKM_ENGINE_NVENC_LAST = NVKM_ENGINE_NVENC2,

	NVKM_ENGINE_NVDEC0,
	NVKM_ENGINE_NVDEC = NVKM_ENGINE_NVDEC0,
	NVKM_ENGINE_NVDEC1,
	NVKM_ENGINE_NVDEC2,
	NVKM_ENGINE_NVDEC_LAST = NVKM_ENGINE_NVDEC2,

	NVKM_ENGINE_PM,
	NVKM_ENGINE_SEC,
	NVKM_ENGINE_SEC2,
	NVKM_ENGINE_SW,
	NVKM_ENGINE_VIC,
	NVKM_ENGINE_VP,

	NVKM_SUBDEV_NR
};

struct nvkm_subdev {
	const struct nvkm_subdev_func *func;
	struct nvkm_device *device;
	enum nvkm_devidx index;
	enum nvkm_subdev_type type;
	int inst;
	char name[16];
	u32 debug;
	struct list_head head;

	void **pself;
	bool oneinit;
};

struct nvkm_subdev_func {
	void *(*dtor)(struct nvkm_subdev *);
	int (*preinit)(struct nvkm_subdev *);
	int (*oneinit)(struct nvkm_subdev *);
	int (*info)(struct nvkm_subdev *, u64 mthd, u64 *data);
	int (*init)(struct nvkm_subdev *);
	int (*fini)(struct nvkm_subdev *, bool suspend);
	void (*intr)(struct nvkm_subdev *);
};

extern const char *nvkm_subdev_type[NVKM_SUBDEV_NR];
int nvkm_subdev_new_(const struct nvkm_subdev_func *, struct nvkm_device *, enum nvkm_subdev_type,
		     int inst, struct nvkm_subdev **);
void nvkm_subdev_ctor(const struct nvkm_subdev_func *, struct nvkm_device *,
		      enum nvkm_subdev_type, int inst, struct nvkm_subdev *);
void nvkm_subdev_disable(struct nvkm_device *, enum nvkm_subdev_type, int inst);
void nvkm_subdev_del(struct nvkm_subdev **);
int  nvkm_subdev_preinit(struct nvkm_subdev *);
int  nvkm_subdev_init(struct nvkm_subdev *);
int  nvkm_subdev_fini(struct nvkm_subdev *, bool suspend);
int  nvkm_subdev_info(struct nvkm_subdev *, u64, u64 *);
void nvkm_subdev_intr(struct nvkm_subdev *);

/* subdev logging */
#define nvkm_printk_(s,l,p,f,a...) do {                                        \
	const struct nvkm_subdev *_subdev = (s);                               \
	if (CONFIG_NOUVEAU_DEBUG >= (l) && _subdev->debug >= (l))              \
		dev_##p(_subdev->device->dev, "%s: "f, _subdev->name, ##a);    \
} while(0)
#define nvkm_printk(s,l,p,f,a...) nvkm_printk_((s), NV_DBG_##l, p, f, ##a)
#define nvkm_fatal(s,f,a...) nvkm_printk((s), FATAL,   crit, f, ##a)
#define nvkm_error(s,f,a...) nvkm_printk((s), ERROR,    err, f, ##a)
#define nvkm_warn(s,f,a...)  nvkm_printk((s),  WARN, notice, f, ##a)
#define nvkm_info(s,f,a...)  nvkm_printk((s),  INFO,   info, f, ##a)
#define nvkm_debug(s,f,a...) nvkm_printk((s), DEBUG,   info, f, ##a)
#define nvkm_trace(s,f,a...) nvkm_printk((s), TRACE,   info, f, ##a)
#define nvkm_spam(s,f,a...)  nvkm_printk((s),  SPAM,    dbg, f, ##a)
#endif
