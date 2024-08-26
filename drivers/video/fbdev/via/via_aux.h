/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * infrastructure for devices connected via I2C
 */

#ifndef __VIA_AUX_H__
#define __VIA_AUX_H__


#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/fb.h>


struct via_aux_bus {
	struct i2c_adapter *adap;	/* the I2C device to access the bus */
	struct list_head drivers;	/* drivers for devices on this bus */
};

struct via_aux_drv {
	struct list_head chain;		/* chain to support multiple drivers */

	struct via_aux_bus *bus;	/* the I2C bus used */
	u8 addr;			/* the I2C target address */

	const char *name;	/* human readable name of the driver */
	void *data;		/* private data of this driver */

	void (*cleanup)(struct via_aux_drv *drv);
	const struct fb_videomode* (*get_preferred_mode)
		(struct via_aux_drv *drv);
};


struct via_aux_bus *via_aux_probe(struct i2c_adapter *adap);
void via_aux_free(struct via_aux_bus *bus);
const struct fb_videomode *via_aux_get_preferred_mode(struct via_aux_bus *bus);


static inline bool via_aux_add(struct via_aux_drv *drv)
{
	struct via_aux_drv *data = kmalloc(sizeof(*data), GFP_KERNEL);

	if (!data)
		return false;

	*data = *drv;
	list_add_tail(&data->chain, &data->bus->drivers);
	return true;
}

static inline bool via_aux_read(struct via_aux_drv *drv, u8 start, u8 *buf,
	u8 len)
{
	struct i2c_msg msg[2] = {
		{.addr = drv->addr, .flags = 0, .len = 1, .buf = &start},
		{.addr = drv->addr, .flags = I2C_M_RD, .len = len, .buf = buf} };

	return i2c_transfer(drv->bus->adap, msg, 2) == 2;
}


/* probe functions of existing drivers - should only be called in via_aux.c */
void via_aux_ch7301_probe(struct via_aux_bus *bus);
void via_aux_edid_probe(struct via_aux_bus *bus);
void via_aux_sii164_probe(struct via_aux_bus *bus);
void via_aux_vt1636_probe(struct via_aux_bus *bus);
void via_aux_vt1632_probe(struct via_aux_bus *bus);
void via_aux_vt1631_probe(struct via_aux_bus *bus);
void via_aux_vt1625_probe(struct via_aux_bus *bus);
void via_aux_vt1622_probe(struct via_aux_bus *bus);
void via_aux_vt1621_probe(struct via_aux_bus *bus);


#endif /* __VIA_AUX_H__ */
