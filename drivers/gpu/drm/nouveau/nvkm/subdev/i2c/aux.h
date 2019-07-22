/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_I2C_AUX_H__
#define __NVKM_I2C_AUX_H__
#include "pad.h"

struct nvkm_i2c_aux_func {
	bool address_only;
	int  (*xfer)(struct nvkm_i2c_aux *, bool retry, u8 type,
		     u32 addr, u8 *data, u8 *size);
	int  (*lnk_ctl)(struct nvkm_i2c_aux *, int link_nr, int link_bw,
			bool enhanced_framing);
};

int nvkm_i2c_aux_ctor(const struct nvkm_i2c_aux_func *, struct nvkm_i2c_pad *,
		      int id, struct nvkm_i2c_aux *);
int nvkm_i2c_aux_new_(const struct nvkm_i2c_aux_func *, struct nvkm_i2c_pad *,
		      int id, struct nvkm_i2c_aux **);
void nvkm_i2c_aux_del(struct nvkm_i2c_aux **);
void nvkm_i2c_aux_init(struct nvkm_i2c_aux *);
void nvkm_i2c_aux_fini(struct nvkm_i2c_aux *);
int nvkm_i2c_aux_xfer(struct nvkm_i2c_aux *, bool retry, u8 type,
		      u32 addr, u8 *data, u8 *size);

int g94_i2c_aux_new_(const struct nvkm_i2c_aux_func *, struct nvkm_i2c_pad *,
		     int, u8, struct nvkm_i2c_aux **);

int g94_i2c_aux_new(struct nvkm_i2c_pad *, int, u8, struct nvkm_i2c_aux **);
int g94_i2c_aux_xfer(struct nvkm_i2c_aux *, bool, u8, u32, u8 *, u8 *);
int gf119_i2c_aux_new(struct nvkm_i2c_pad *, int, u8, struct nvkm_i2c_aux **);
int gm200_i2c_aux_new(struct nvkm_i2c_pad *, int, u8, struct nvkm_i2c_aux **);

#define AUX_MSG(b,l,f,a...) do {                                               \
	struct nvkm_i2c_aux *_aux = (b);                                       \
	nvkm_##l(&_aux->pad->i2c->subdev, "aux %04x: "f"\n", _aux->id, ##a);   \
} while(0)
#define AUX_ERR(b,f,a...) AUX_MSG((b), error, f, ##a)
#define AUX_DBG(b,f,a...) AUX_MSG((b), debug, f, ##a)
#define AUX_TRACE(b,f,a...) AUX_MSG((b), trace, f, ##a)
#endif
