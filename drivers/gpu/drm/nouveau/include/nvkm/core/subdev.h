/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_SUBDEV_H__
#define __NVKM_SUBDEV_H__
#include <core/device.h>

enum nvkm_subdev_type {
#define NVKM_LAYOUT_ONCE(t,s,p,...) t,
#define NVKM_LAYOUT_INST NVKM_LAYOUT_ONCE
#include <core/layout.h>
#undef NVKM_LAYOUT_INST
#undef NVKM_LAYOUT_ONCE
	NVKM_SUBDEV_NR
};

struct nvkm_subdev {
	const struct nvkm_subdev_func *func;
	struct nvkm_device *device;
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
