#ifndef __NV50_I2C_H__
#define __NV50_I2C_H__
#include "priv.h"

struct nv50_i2c_priv {
	struct nvkm_i2c base;
};

struct nv50_i2c_port {
	struct nvkm_i2c_port base;
	u32 addr;
	u32 state;
};

extern const u32 nv50_i2c_addr[];
extern const int nv50_i2c_addr_nr;
int  nv50_i2c_port_init(struct nvkm_object *);
int  nv50_i2c_sense_scl(struct nvkm_i2c_port *);
int  nv50_i2c_sense_sda(struct nvkm_i2c_port *);
void nv50_i2c_drive_scl(struct nvkm_i2c_port *, int state);
void nv50_i2c_drive_sda(struct nvkm_i2c_port *, int state);

int  g94_aux_port_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
void g94_i2c_acquire(struct nvkm_i2c_port *);
void g94_i2c_release(struct nvkm_i2c_port *);

int  gf110_i2c_port_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
#endif
