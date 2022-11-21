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

	struct {
		refcount_t refcount;
		struct mutex mutex;
		bool enabled;
	} use;

	struct nvkm_inth inth;

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
void __nvkm_subdev_ctor(const struct nvkm_subdev_func *, struct nvkm_device *,
			enum nvkm_subdev_type, int inst, struct nvkm_subdev *);

static inline void
nvkm_subdev_ctor(const struct nvkm_subdev_func *func, struct nvkm_device *device,
		 enum nvkm_subdev_type type, int inst, struct nvkm_subdev *subdev)
{
	__nvkm_subdev_ctor(func, device, type, inst, subdev);
	mutex_init(&subdev->use.mutex);
}

void nvkm_subdev_disable(struct nvkm_device *, enum nvkm_subdev_type, int inst);
void nvkm_subdev_del(struct nvkm_subdev **);
int  nvkm_subdev_ref(struct nvkm_subdev *);
void nvkm_subdev_unref(struct nvkm_subdev *);
int  nvkm_subdev_preinit(struct nvkm_subdev *);
int  nvkm_subdev_oneinit(struct nvkm_subdev *);
int  nvkm_subdev_init(struct nvkm_subdev *);
int  nvkm_subdev_fini(struct nvkm_subdev *, bool suspend);
int  nvkm_subdev_info(struct nvkm_subdev *, u64, u64 *);
void nvkm_subdev_intr(struct nvkm_subdev *);

/* subdev logging */
#define nvkm_printk_ok(s,u,l)                                                                \
	((CONFIG_NOUVEAU_DEBUG >= (l)) && ((s)->debug >= (l) || ((u) && (u)->debug >= (l))))
#define nvkm_printk___(s,u,l,p,f,a...) do {                                                  \
	if (nvkm_printk_ok((s), (u), (l))) {                                                 \
		if ((u) && (u) != (s))                                                       \
			dev_##p((s)->device->dev, "%s(%s):"f, (s)->name, (u)->name, ##a);    \
		else                                                                         \
			dev_##p((s)->device->dev, "%s:"f, (s)->name, ##a);                   \
	}                                                                                    \
} while(0)
#define nvkm_printk__(s,l,p,f,a...) nvkm_printk___((s), (s), (l), p, f, ##a)
#define nvkm_printk_(s,l,p,f,a...) nvkm_printk__((s), (l), p, " "f, ##a)
#define nvkm_printk(s,l,p,f,a...) nvkm_printk_((s), NV_DBG_##l, p, f, ##a)
#define nvkm_fatal(s,f,a...) nvkm_printk((s), FATAL,   crit, f, ##a)
#define nvkm_error(s,f,a...) nvkm_printk((s), ERROR,    err, f, ##a)
#define nvkm_warn(s,f,a...)  nvkm_printk((s),  WARN, notice, f, ##a)
#define nvkm_info(s,f,a...)  nvkm_printk((s),  INFO,   info, f, ##a)
#define nvkm_debug(s,f,a...) nvkm_printk((s), DEBUG,   info, f, ##a)
#define nvkm_trace(s,f,a...) nvkm_printk((s), TRACE,   info, f, ##a)
#define nvkm_spam(s,f,a...)  nvkm_printk((s),  SPAM,    dbg, f, ##a)

#define nvkm_error_ratelimited(s,f,a...) nvkm_printk((s), ERROR, err_ratelimited, f, ##a)
#endif
