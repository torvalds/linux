#ifndef __NVKM_I2C_BUS_H__
#define __NVKM_I2C_BUS_H__
#include "pad.h"

struct nvkm_i2c_bus_func {
	void (*init)(struct nvkm_i2c_bus *);
	void (*drive_scl)(struct nvkm_i2c_bus *, int state);
	void (*drive_sda)(struct nvkm_i2c_bus *, int state);
	int (*sense_scl)(struct nvkm_i2c_bus *);
	int (*sense_sda)(struct nvkm_i2c_bus *);
	int (*xfer)(struct nvkm_i2c_bus *, struct i2c_msg *, int num);
};

int nvkm_i2c_bus_ctor(const struct nvkm_i2c_bus_func *, struct nvkm_i2c_pad *,
		      int id, struct nvkm_i2c_bus *);
int nvkm_i2c_bus_new_(const struct nvkm_i2c_bus_func *, struct nvkm_i2c_pad *,
		      int id, struct nvkm_i2c_bus **);
void nvkm_i2c_bus_del(struct nvkm_i2c_bus **);
void nvkm_i2c_bus_init(struct nvkm_i2c_bus *);

int nvkm_i2c_bit_xfer(struct nvkm_i2c_bus *, struct i2c_msg *, int);

int nv04_i2c_bus_new(struct nvkm_i2c_pad *, int, u8, u8,
		     struct nvkm_i2c_bus **);

int nv4e_i2c_bus_new(struct nvkm_i2c_pad *, int, u8, struct nvkm_i2c_bus **);
int nv50_i2c_bus_new(struct nvkm_i2c_pad *, int, u8, struct nvkm_i2c_bus **);
int gf119_i2c_bus_new(struct nvkm_i2c_pad *, int, u8, struct nvkm_i2c_bus **);

#define BUS_MSG(b,l,f,a...) do {                                               \
	struct nvkm_i2c_bus *_bus = (b);                                       \
	nvkm_##l(&_bus->pad->i2c->subdev, "bus %04x: "f"\n", _bus->id, ##a);   \
} while(0)
#define BUS_ERR(b,f,a...) BUS_MSG((b), error, f, ##a)
#define BUS_DBG(b,f,a...) BUS_MSG((b), debug, f, ##a)
#define BUS_TRACE(b,f,a...) BUS_MSG((b), trace, f, ##a)
#endif
