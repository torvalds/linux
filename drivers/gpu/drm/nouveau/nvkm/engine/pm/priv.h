#ifndef __NVKM_PM_PRIV_H__
#define __NVKM_PM_PRIV_H__
#define nvkm_pm(p) container_of((p), struct nvkm_pm, engine)
#include <engine/pm.h>

int nvkm_pm_ctor(const struct nvkm_pm_func *, struct nvkm_device *,
		 int index, struct nvkm_pm *);

struct nvkm_pm_func {
	void (*fini)(struct nvkm_pm *);
};

struct nvkm_perfctr {
	struct list_head head;
	u8 domain;
	u8  signal[4];
	u64 source[4][8];
	int slot;
	u32 logic_op;
	u32 ctr;
};

struct nvkm_specmux {
	u32 mask;
	u8 shift;
	const char *name;
	bool enable;
};

struct nvkm_specsrc {
	u32 addr;
	const struct nvkm_specmux *mux;
	const char *name;
};

struct nvkm_perfsrc {
	struct list_head head;
	char *name;
	u32 addr;
	u32 mask;
	u8 shift;
	bool enable;
};

extern const struct nvkm_specsrc nv50_zcull_sources[];
extern const struct nvkm_specsrc nv50_zrop_sources[];
extern const struct nvkm_specsrc g84_vfetch_sources[];
extern const struct nvkm_specsrc gt200_crop_sources[];
extern const struct nvkm_specsrc gt200_prop_sources[];
extern const struct nvkm_specsrc gt200_tex_sources[];

struct nvkm_specsig {
	u8 signal;
	const char *name;
	const struct nvkm_specsrc *source;
};

struct nvkm_perfsig {
	const char *name;
	u8 source[8];
};

struct nvkm_specdom {
	u16 signal_nr;
	const struct nvkm_specsig *signal;
	const struct nvkm_funcdom *func;
};

#define nvkm_perfdom(p) container_of((p), struct nvkm_perfdom, object)
#include <core/object.h>

struct nvkm_perfdom {
	struct nvkm_object object;
	struct nvkm_perfmon *perfmon;
	struct list_head head;
	struct list_head list;
	const struct nvkm_funcdom *func;
	struct nvkm_perfctr *ctr[4];
	char name[32];
	u32 addr;
	u8  mode;
	u32 clk;
	u16 signal_nr;
	struct nvkm_perfsig signal[];
};

struct nvkm_funcdom {
	void (*init)(struct nvkm_pm *, struct nvkm_perfdom *,
		     struct nvkm_perfctr *);
	void (*read)(struct nvkm_pm *, struct nvkm_perfdom *,
		     struct nvkm_perfctr *);
	void (*next)(struct nvkm_pm *, struct nvkm_perfdom *);
};

int nvkm_perfdom_new(struct nvkm_pm *, const char *, u32, u32, u32, u32,
		     const struct nvkm_specdom *);

#define nvkm_perfmon(p) container_of((p), struct nvkm_perfmon, object)

struct nvkm_perfmon {
	struct nvkm_object object;
	struct nvkm_pm *pm;
};
#endif
