/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_I2C_PRIV_H__
#define __NVKM_I2C_PRIV_H__
#define nvkm_i2c(p) container_of((p), struct nvkm_i2c, subdev)
#include <subdev/i2c.h>

int nvkm_i2c_new_(const struct nvkm_i2c_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  struct nvkm_i2c **);

struct nvkm_i2c_func {
	int (*pad_x_new)(struct nvkm_i2c *, int id, struct nvkm_i2c_pad **);
	int (*pad_s_new)(struct nvkm_i2c *, int id, struct nvkm_i2c_pad **);

	/* number of native dp aux channels present */
	int aux;

	/* read and ack pending interrupts, returning only data
	 * for ports that have not been masked off, while still
	 * performing the ack for anything that was pending.
	 */
	void (*aux_stat)(struct nvkm_i2c *, u32 *, u32 *, u32 *, u32 *);

	/* mask on/off interrupt types for a given set of auxch
	 */
	void (*aux_mask)(struct nvkm_i2c *, u32, u32, u32);

	/* enable/disable HW-initiated DPCD reads
	 */
	void (*aux_autodpcd)(struct nvkm_i2c *, int aux, bool enable);
};

void g94_aux_stat(struct nvkm_i2c *, u32 *, u32 *, u32 *, u32 *);
void g94_aux_mask(struct nvkm_i2c *, u32, u32, u32);

void gk104_aux_stat(struct nvkm_i2c *, u32 *, u32 *, u32 *, u32 *);
void gk104_aux_mask(struct nvkm_i2c *, u32, u32, u32);
#endif
