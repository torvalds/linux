/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_INTR_H__
#define __NVKM_INTR_H__
#include <core/os.h>
struct nvkm_device;
struct nvkm_subdev;

enum nvkm_intr_prio {
	NVKM_INTR_PRIO_VBLANK = 0,
	NVKM_INTR_PRIO_NORMAL,
	NVKM_INTR_PRIO_NR
};

enum nvkm_intr_type {
	NVKM_INTR_SUBDEV   = -1, /* lookup vector by requesting subdev, in mapping table. */
	NVKM_INTR_VECTOR_0 = 0,
};

struct nvkm_intr {
	const struct nvkm_intr_func {
		bool (*pending)(struct nvkm_intr *);
		void (*unarm)(struct nvkm_intr *);
		void (*rearm)(struct nvkm_intr *);
		void (*block)(struct nvkm_intr *, int leaf, u32 mask);
		void (*allow)(struct nvkm_intr *, int leaf, u32 mask);
		void (*reset)(struct nvkm_intr *, int leaf, u32 mask);
	} *func;
	const struct nvkm_intr_data {
		int type; /* enum nvkm_subdev_type (+ve), enum nvkm_intr_type (-ve) */
		int inst;
		int leaf;
		u32 mask; /* 0-terminated. */
		bool legacy; /* auto-create "legacy" nvkm_subdev_intr() handler */
	} *data;

	struct nvkm_subdev *subdev;
	int leaves;
	u32 *stat;
	u32 *mask;

	struct list_head head;
};

void nvkm_intr_ctor(struct nvkm_device *);
void nvkm_intr_dtor(struct nvkm_device *);
int nvkm_intr_install(struct nvkm_device *);
void nvkm_intr_unarm(struct nvkm_device *);
void nvkm_intr_rearm(struct nvkm_device *);

int nvkm_intr_add(const struct nvkm_intr_func *, const struct nvkm_intr_data *,
		  struct nvkm_subdev *, int leaves, struct nvkm_intr *);
void nvkm_intr_block(struct nvkm_subdev *, enum nvkm_intr_type);
void nvkm_intr_allow(struct nvkm_subdev *, enum nvkm_intr_type);

struct nvkm_inth;
typedef irqreturn_t (*nvkm_inth_func)(struct nvkm_inth *);

struct nvkm_inth {
	struct nvkm_intr *intr;
	int leaf;
	u32 mask;
	nvkm_inth_func func;

	atomic_t allowed;

	struct list_head head;
};

int nvkm_inth_add(struct nvkm_intr *, enum nvkm_intr_type, enum nvkm_intr_prio,
		  struct nvkm_subdev *, nvkm_inth_func, struct nvkm_inth *);
void nvkm_inth_allow(struct nvkm_inth *);
void nvkm_inth_block(struct nvkm_inth *);
#endif
