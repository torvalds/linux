/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DISP_H__
#define __NVKM_DISP_H__
#define nvkm_disp(p) container_of((p), struct nvkm_disp, engine)
#include <core/engine.h>
#include <core/object.h>
#include <core/event.h>
#include <subdev/gsp.h>

struct nvkm_disp {
	const struct nvkm_disp_func *func;
	struct nvkm_engine engine;

	struct {
		struct nvkm_gsp_client client;
		struct nvkm_gsp_device device;

		struct nvkm_gsp_object objcom;
		struct nvkm_gsp_object object;

#define NVKM_DPYID_PLUG   BIT(0)
#define NVKM_DPYID_UNPLUG BIT(1)
#define NVKM_DPYID_IRQ    BIT(2)
		struct nvkm_event event;
		struct nvkm_gsp_event hpd;
		struct nvkm_gsp_event irq;

		u32 assigned_sors;
	} rm;

	struct list_head heads;
	struct list_head iors;
	struct list_head outps;
	struct list_head conns;

	struct nvkm_event hpd;
#define NVKM_DISP_HEAD_EVENT_VBLANK BIT(0)
	struct nvkm_event vblank;

	struct {
		struct workqueue_struct *wq;
		struct work_struct work;
		u32 pending;
		struct mutex mutex;
	} super;

#define NVKM_DISP_EVENT_CHAN_AWAKEN BIT(0)
	struct nvkm_event uevent;

	struct {
		unsigned long mask;
		int nr;
	} wndw, head, dac, sor;

	struct {
		unsigned long mask;
		int nr;
		u8 type[3];
	} pior;

	struct nvkm_gpuobj *inst;
	struct nvkm_ramht *ramht;

	struct nvkm_disp_chan *chan[81];

	struct {
		spinlock_t lock;
		struct nvkm_object object;
	} client;
};

int nv04_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int nv50_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int g84_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gt200_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int g94_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int mcp77_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gt215_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int mcp89_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gf119_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gk104_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gk110_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gm107_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gm200_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gp100_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gp102_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int gv100_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int tu102_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int ga102_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
int ad102_disp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_disp **);
#endif
