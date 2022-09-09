/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_TIMER_PRIV_H__
#define __NVKM_TIMER_PRIV_H__
#define nvkm_timer(p) container_of((p), struct nvkm_timer, subdev)
#include <subdev/timer.h>

int nvkm_timer_new_(const struct nvkm_timer_func *, struct nvkm_device *, enum nvkm_subdev_type,
		    int, struct nvkm_timer **);

struct nvkm_timer_func {
	void (*init)(struct nvkm_timer *);
	void (*intr)(struct nvkm_timer *);
	u64 (*read)(struct nvkm_timer *);
	void (*time)(struct nvkm_timer *, u64 time);
	void (*alarm_init)(struct nvkm_timer *, u32 time);
	void (*alarm_fini)(struct nvkm_timer *);
};

void nvkm_timer_alarm_trigger(struct nvkm_timer *);

void nv04_timer_fini(struct nvkm_timer *);
void nv04_timer_intr(struct nvkm_timer *);
void nv04_timer_time(struct nvkm_timer *, u64);
u64 nv04_timer_read(struct nvkm_timer *);
void nv04_timer_alarm_init(struct nvkm_timer *, u32);
void nv04_timer_alarm_fini(struct nvkm_timer *);
#endif
