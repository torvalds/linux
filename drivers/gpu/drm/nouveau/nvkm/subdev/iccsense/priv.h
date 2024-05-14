/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_ICCSENSE_PRIV_H__
#define __NVKM_ICCSENSE_PRIV_H__
#define nvkm_iccsense(p) container_of((p), struct nvkm_iccsense, subdev)
#include <subdev/iccsense.h>
#include <subdev/bios/extdev.h>

struct nvkm_iccsense_sensor {
	struct list_head head;
	int id;
	enum nvbios_extdev_type type;
	struct i2c_adapter *i2c;
	u8 addr;
	u16 config;
};

struct nvkm_iccsense_rail {
	struct list_head head;
	int (*read)(struct nvkm_iccsense *, struct nvkm_iccsense_rail *);
	struct nvkm_iccsense_sensor *sensor;
	u8 idx;
	u8 mohm;
};

void nvkm_iccsense_ctor(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_iccsense *);
int nvkm_iccsense_new_(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_iccsense **);
#endif
