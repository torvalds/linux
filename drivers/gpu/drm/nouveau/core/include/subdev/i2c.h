#ifndef __NOUVEAU_I2C_H__
#define __NOUVEAU_I2C_H__

#include <core/subdev.h>
#include <core/device.h>

#include <subdev/bios.h>
#include <subdev/bios/i2c.h>

#define NV_I2C_PORT(n)    (0x00 + (n))
#define NV_I2C_DEFAULT(n) (0x80 + (n))

#define NV_I2C_TYPE_DCBI2C(n) (0x0000 | (n))
#define NV_I2C_TYPE_EXTDDC(e) (0x0005 | (e) << 8)
#define NV_I2C_TYPE_EXTAUX(e) (0x0006 | (e) << 8)

struct nouveau_i2c_port {
	struct nouveau_object base;
	struct i2c_adapter adapter;

	struct list_head head;
	u8  index;

	const struct nouveau_i2c_func *func;
};

struct nouveau_i2c_func {
	void (*acquire)(struct nouveau_i2c_port *);
	void (*release)(struct nouveau_i2c_port *);

	void (*drive_scl)(struct nouveau_i2c_port *, int);
	void (*drive_sda)(struct nouveau_i2c_port *, int);
	int  (*sense_scl)(struct nouveau_i2c_port *);
	int  (*sense_sda)(struct nouveau_i2c_port *);

	int  (*aux)(struct nouveau_i2c_port *, u8, u32, u8 *, u8);
	int  (*pattern)(struct nouveau_i2c_port *, int pattern);
	int  (*lnk_ctl)(struct nouveau_i2c_port *, int nr, int bw, bool enh);
	int  (*drv_ctl)(struct nouveau_i2c_port *, int lane, int sw, int pe);
};

#define nouveau_i2c_port_create(p,e,o,i,a,d)                                   \
	nouveau_i2c_port_create_((p), (e), (o), (i), (a),                      \
				 sizeof(**d), (void **)d)
#define nouveau_i2c_port_destroy(p) ({                                         \
	struct nouveau_i2c_port *port = (p);                                   \
	_nouveau_i2c_port_dtor(nv_object(i2c));                                \
})
#define nouveau_i2c_port_init(p)                                               \
	nouveau_object_init(&(p)->base)
#define nouveau_i2c_port_fini(p,s)                                             \
	nouveau_object_fini(&(p)->base, (s))

int nouveau_i2c_port_create_(struct nouveau_object *, struct nouveau_object *,
			     struct nouveau_oclass *, u8,
			     const struct i2c_algorithm *, int, void **);
void _nouveau_i2c_port_dtor(struct nouveau_object *);
#define _nouveau_i2c_port_init nouveau_object_init
#define _nouveau_i2c_port_fini nouveau_object_fini

struct nouveau_i2c {
	struct nouveau_subdev base;

	struct nouveau_i2c_port *(*find)(struct nouveau_i2c *, u8 index);
	struct nouveau_i2c_port *(*find_type)(struct nouveau_i2c *, u16 type);
	int (*identify)(struct nouveau_i2c *, int index,
			const char *what, struct i2c_board_info *,
			bool (*match)(struct nouveau_i2c_port *,
				      struct i2c_board_info *));
	struct list_head ports;
};

static inline struct nouveau_i2c *
nouveau_i2c(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_I2C];
}

#define nouveau_i2c_create(p,e,o,s,d)                                          \
	nouveau_i2c_create_((p), (e), (o), (s), sizeof(**d), (void **)d)
#define nouveau_i2c_destroy(p) ({                                              \
	struct nouveau_i2c *i2c = (p);                                         \
	_nouveau_i2c_dtor(nv_object(i2c));                                     \
})
#define nouveau_i2c_init(p) ({                                                 \
	struct nouveau_i2c *i2c = (p);                                         \
	_nouveau_i2c_init(nv_object(i2c));                                     \
})
#define nouveau_i2c_fini(p,s) ({                                               \
	struct nouveau_i2c *i2c = (p);                                         \
	_nouveau_i2c_fini(nv_object(i2c), (s));                                \
})

int nouveau_i2c_create_(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, struct nouveau_oclass *,
			int, void **);
void _nouveau_i2c_dtor(struct nouveau_object *);
int  _nouveau_i2c_init(struct nouveau_object *);
int  _nouveau_i2c_fini(struct nouveau_object *, bool);

extern struct nouveau_oclass nv04_i2c_oclass;
extern struct nouveau_oclass nv4e_i2c_oclass;
extern struct nouveau_oclass nv50_i2c_oclass;
extern struct nouveau_oclass nv94_i2c_oclass;
extern struct nouveau_oclass nvd0_i2c_oclass;
extern struct nouveau_oclass nouveau_anx9805_sclass[];

extern const struct i2c_algorithm nouveau_i2c_bit_algo;
extern const struct i2c_algorithm nouveau_i2c_aux_algo;

static inline int
nv_rdi2cr(struct nouveau_i2c_port *port, u8 addr, u8 reg)
{
	u8 val;
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = &val },
	};

	int ret = i2c_transfer(&port->adapter, msgs, 2);
	if (ret != 2)
		return -EIO;

	return val;
}

static inline int
nv_wri2cr(struct nouveau_i2c_port *port, u8 addr, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 2, .buf = buf },
	};

	int ret = i2c_transfer(&port->adapter, msgs, 1);
	if (ret != 1)
		return -EIO;

	return 0;
}

static inline bool
nv_probe_i2c(struct nouveau_i2c_port *port, u8 addr)
{
	return nv_rdi2cr(port, addr, 0) >= 0;
}

int nv_rdaux(struct nouveau_i2c_port *, u32 addr, u8 *data, u8 size);
int nv_wraux(struct nouveau_i2c_port *, u32 addr, u8 *data, u8 size);

#endif
