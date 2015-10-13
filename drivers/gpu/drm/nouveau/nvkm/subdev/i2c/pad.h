#ifndef __NVKM_I2C_PAD_H__
#define __NVKM_I2C_PAD_H__
#include <subdev/i2c.h>

struct nvkm_i2c_pad {
	const struct nvkm_i2c_pad_func *func;
	struct nvkm_i2c *i2c;
#define NVKM_I2C_PAD_HYBRID(n) /* 'n' is hw pad index */                     (n)
#define NVKM_I2C_PAD_CCB(n) /* 'n' is ccb index */                 ((n) + 0x100)
#define NVKM_I2C_PAD_EXT(n) /* 'n' is dcb external encoder type */ ((n) + 0x200)
	int id;

	enum nvkm_i2c_pad_mode {
		NVKM_I2C_PAD_OFF,
		NVKM_I2C_PAD_I2C,
		NVKM_I2C_PAD_AUX,
	} mode;
	struct mutex mutex;
	struct list_head head;
};

struct nvkm_i2c_pad_func {
	int (*bus_new_0)(struct nvkm_i2c_pad *, int id, u8 drive, u8 sense,
			 struct nvkm_i2c_bus **);
	int (*bus_new_4)(struct nvkm_i2c_pad *, int id, u8 drive,
			 struct nvkm_i2c_bus **);

	int (*aux_new_6)(struct nvkm_i2c_pad *, int id, u8 drive,
			 struct nvkm_i2c_aux **);

	void (*mode)(struct nvkm_i2c_pad *, enum nvkm_i2c_pad_mode);
};

void nvkm_i2c_pad_ctor(const struct nvkm_i2c_pad_func *, struct nvkm_i2c *,
		       int id, struct nvkm_i2c_pad *);
int nvkm_i2c_pad_new_(const struct nvkm_i2c_pad_func *, struct nvkm_i2c *,
		      int id, struct nvkm_i2c_pad **);
void nvkm_i2c_pad_del(struct nvkm_i2c_pad **);
void nvkm_i2c_pad_init(struct nvkm_i2c_pad *);
void nvkm_i2c_pad_fini(struct nvkm_i2c_pad *);
void nvkm_i2c_pad_mode(struct nvkm_i2c_pad *, enum nvkm_i2c_pad_mode);
int nvkm_i2c_pad_acquire(struct nvkm_i2c_pad *, enum nvkm_i2c_pad_mode);
void nvkm_i2c_pad_release(struct nvkm_i2c_pad *);

void g94_i2c_pad_mode(struct nvkm_i2c_pad *, enum nvkm_i2c_pad_mode);

int nv04_i2c_pad_new(struct nvkm_i2c *, int, struct nvkm_i2c_pad **);
int nv4e_i2c_pad_new(struct nvkm_i2c *, int, struct nvkm_i2c_pad **);
int nv50_i2c_pad_new(struct nvkm_i2c *, int, struct nvkm_i2c_pad **);
int g94_i2c_pad_x_new(struct nvkm_i2c *, int, struct nvkm_i2c_pad **);
int gf119_i2c_pad_x_new(struct nvkm_i2c *, int, struct nvkm_i2c_pad **);
int gm204_i2c_pad_x_new(struct nvkm_i2c *, int, struct nvkm_i2c_pad **);

int g94_i2c_pad_s_new(struct nvkm_i2c *, int, struct nvkm_i2c_pad **);
int gf119_i2c_pad_s_new(struct nvkm_i2c *, int, struct nvkm_i2c_pad **);
int gm204_i2c_pad_s_new(struct nvkm_i2c *, int, struct nvkm_i2c_pad **);

int anx9805_pad_new(struct nvkm_i2c_bus *, int, u8, struct nvkm_i2c_pad **);

#define PAD_MSG(p,l,f,a...) do {                                               \
	struct nvkm_i2c_pad *_pad = (p);                                       \
	nvkm_##l(&_pad->i2c->subdev, "pad %04x: "f"\n", _pad->id, ##a);        \
} while(0)
#define PAD_ERR(p,f,a...) PAD_MSG((p), error, f, ##a)
#define PAD_DBG(p,f,a...) PAD_MSG((p), debug, f, ##a)
#define PAD_TRACE(p,f,a...) PAD_MSG((p), trace, f, ##a)
#endif
