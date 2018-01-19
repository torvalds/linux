/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_SUBDEV_H__
#define __NVKM_SUBDEV_H__
#include <core/device.h>

struct nvkm_subdev {
	const struct nvkm_subdev_func *func;
	struct nvkm_device *device;
	enum nvkm_devidx index;
	struct mutex mutex;
	u32 debug;

	bool oneinit;
};

struct nvkm_subdev_func {
	void *(*dtor)(struct nvkm_subdev *);
	int (*preinit)(struct nvkm_subdev *);
	int (*oneinit)(struct nvkm_subdev *);
	int (*init)(struct nvkm_subdev *);
	int (*fini)(struct nvkm_subdev *, bool suspend);
	void (*intr)(struct nvkm_subdev *);
};

extern const char *nvkm_subdev_name[NVKM_SUBDEV_NR];
void nvkm_subdev_ctor(const struct nvkm_subdev_func *, struct nvkm_device *,
		      int index, struct nvkm_subdev *);
void nvkm_subdev_del(struct nvkm_subdev **);
int  nvkm_subdev_preinit(struct nvkm_subdev *);
int  nvkm_subdev_init(struct nvkm_subdev *);
int  nvkm_subdev_fini(struct nvkm_subdev *, bool suspend);
void nvkm_subdev_intr(struct nvkm_subdev *);

/* subdev logging */
#define nvkm_printk_(s,l,p,f,a...) do {                                        \
	const struct nvkm_subdev *_subdev = (s);                               \
	if (_subdev->debug >= (l)) {                                           \
		dev_##p(_subdev->device->dev, "%s: "f,                         \
			nvkm_subdev_name[_subdev->index], ##a);                \
	}                                                                      \
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
