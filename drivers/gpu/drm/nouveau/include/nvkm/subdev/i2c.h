/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_I2C_H__
#define __NVKM_I2C_H__
#include <core/subdev.h>
#include <core/event.h>

#include <subdev/bios.h>
#include <subdev/bios/i2c.h>

struct nvkm_i2c_bus_probe {
	struct i2c_board_info dev;
	u8 udelay; /* set to 0 to use the standard delay */
};

struct nvkm_i2c_bus {
	const struct nvkm_i2c_bus_func *func;
	struct nvkm_i2c_pad *pad;
#define NVKM_I2C_BUS_CCB(n) /* 'n' is ccb index */                           (n)
#define NVKM_I2C_BUS_EXT(n) /* 'n' is dcb external encoder type */ ((n) + 0x100)
#define NVKM_I2C_BUS_PRI /* ccb primary comm. port */                        -1
#define NVKM_I2C_BUS_SEC /* ccb secondary comm. port */                      -2
	int id;

	struct mutex mutex;
	struct list_head head;
	struct i2c_adapter i2c;
	u8 enabled;
};

int nvkm_i2c_bus_acquire(struct nvkm_i2c_bus *);
void nvkm_i2c_bus_release(struct nvkm_i2c_bus *);
int nvkm_i2c_bus_probe(struct nvkm_i2c_bus *, const char *,
		       struct nvkm_i2c_bus_probe *,
		       bool (*)(struct nvkm_i2c_bus *,
			        struct i2c_board_info *, void *), void *);

struct nvkm_i2c_aux {
	const struct nvkm_i2c_aux_func *func;
	struct nvkm_i2c_pad *pad;
#define NVKM_I2C_AUX_CCB(n) /* 'n' is ccb index */                           (n)
#define NVKM_I2C_AUX_EXT(n) /* 'n' is dcb external encoder type */ ((n) + 0x100)
	int id;

	struct mutex mutex;
	struct list_head head;
	struct i2c_adapter i2c;
	u8 enabled;

	u32 intr;
};

void nvkm_i2c_aux_monitor(struct nvkm_i2c_aux *, bool monitor);
int nvkm_i2c_aux_acquire(struct nvkm_i2c_aux *);
void nvkm_i2c_aux_release(struct nvkm_i2c_aux *);
int nvkm_i2c_aux_xfer(struct nvkm_i2c_aux *, bool retry, u8 type,
		      u32 addr, u8 *data, u8 *size);
int nvkm_i2c_aux_lnk_ctl(struct nvkm_i2c_aux *, int link_nr, int link_bw,
			 bool enhanced_framing);

struct nvkm_i2c {
	const struct nvkm_i2c_func *func;
	struct nvkm_subdev subdev;

	struct list_head pad;
	struct list_head bus;
	struct list_head aux;

#define NVKM_I2C_PLUG   BIT(0)
#define NVKM_I2C_UNPLUG BIT(1)
#define NVKM_I2C_IRQ    BIT(2)
#define NVKM_I2C_DONE   BIT(3)
#define NVKM_I2C_ANY   (NVKM_I2C_PLUG | NVKM_I2C_UNPLUG | NVKM_I2C_IRQ | NVKM_I2C_DONE)
	struct nvkm_event event;
};

struct nvkm_i2c_bus *nvkm_i2c_bus_find(struct nvkm_i2c *, int);
struct nvkm_i2c_aux *nvkm_i2c_aux_find(struct nvkm_i2c *, int);

int nv04_i2c_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_i2c **);
int nv4e_i2c_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_i2c **);
int nv50_i2c_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_i2c **);
int g94_i2c_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_i2c **);
int gf117_i2c_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_i2c **);
int gf119_i2c_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_i2c **);
int gk104_i2c_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_i2c **);
int gk110_i2c_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_i2c **);
int gm200_i2c_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_i2c **);

static inline int
nvkm_rdi2cr(struct i2c_adapter *adap, u8 addr, u8 reg)
{
	u8 val;
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = &val },
	};

	int ret = i2c_transfer(adap, msgs, ARRAY_SIZE(msgs));
	if (ret != 2)
		return -EIO;

	return val;
}

static inline int
nv_rd16i2cr(struct i2c_adapter *adap, u8 addr, u8 reg)
{
	u8 val[2];
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = 2, .buf = val },
	};

	int ret = i2c_transfer(adap, msgs, ARRAY_SIZE(msgs));
	if (ret != 2)
		return -EIO;

	return val[0] << 8 | val[1];
}

static inline int
nvkm_wri2cr(struct i2c_adapter *adap, u8 addr, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 2, .buf = buf },
	};

	int ret = i2c_transfer(adap, msgs, ARRAY_SIZE(msgs));
	if (ret != 1)
		return -EIO;

	return 0;
}

static inline int
nv_wr16i2cr(struct i2c_adapter *adap, u8 addr, u8 reg, u16 val)
{
	u8 buf[3] = { reg, val >> 8, val & 0xff};
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 3, .buf = buf },
	};

	int ret = i2c_transfer(adap, msgs, ARRAY_SIZE(msgs));
	if (ret != 1)
		return -EIO;

	return 0;
}

static inline bool
nvkm_probe_i2c(struct i2c_adapter *adap, u8 addr)
{
	return nvkm_rdi2cr(adap, addr, 0) >= 0;
}

static inline int
nvkm_rdaux(struct nvkm_i2c_aux *aux, u32 addr, u8 *data, u8 size)
{
	const u8 xfer = size;
	int ret = nvkm_i2c_aux_acquire(aux);
	if (ret == 0) {
		ret = nvkm_i2c_aux_xfer(aux, true, 9, addr, data, &size);
		WARN_ON(!ret && size != xfer);
		nvkm_i2c_aux_release(aux);
	}
	return ret;
}

static inline int
nvkm_wraux(struct nvkm_i2c_aux *aux, u32 addr, u8 *data, u8 size)
{
	int ret = nvkm_i2c_aux_acquire(aux);
	if (ret == 0) {
		ret = nvkm_i2c_aux_xfer(aux, true, 8, addr, data, &size);
		nvkm_i2c_aux_release(aux);
	}
	return ret;
}
#endif
