/*
 *  Driver for the Auvitek AU0828 USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <media/tveeprom.h>

/* DVB */
#include "demux.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"
#include "dvbdev.h"

#include "au0828-reg.h"
#include "au0828-cards.h"

#define DRIVER_NAME "au0828"
#define URB_COUNT   16
#define URB_BUFSIZE (0xe522)

struct au0828_board {
	char *name;
};

struct au0828_dvb {
	struct mutex lock;
	struct dvb_adapter adapter;
	struct dvb_frontend *frontend;
	struct dvb_demux demux;
	struct dmxdev dmxdev;
	struct dmx_frontend fe_hw;
	struct dmx_frontend fe_mem;
	struct dvb_net net;
	int feeding;
};

struct au0828_dev {
	struct mutex mutex;
	struct usb_device	*usbdev;
	int			board;
	u8			ctrlmsg[64];

	/* I2C */
	struct i2c_adapter		i2c_adap;
	struct i2c_algo_bit_data	i2c_algo;
	struct i2c_client		i2c_client;
	u32 				i2c_rc;

	/* Digital */
	struct au0828_dvb		dvb;

	/* USB / URB Related */
	int		urb_streaming;
	struct urb	*urbs[URB_COUNT];

};

struct au0828_buff {
	struct au0828_dev	*dev;
	struct urb		*purb;
	struct list_head	buff_list;
};

/* ----------------------------------------------------------- */
#define au0828_read(dev, reg) au0828_readreg(dev, reg)
#define au0828_write(dev, reg, value) au0828_writereg(dev, reg, value)
#define au0828_andor(dev, reg, mask, value) 				\
	 au0828_writereg(dev, reg, 					\
	(au0828_readreg(dev, reg) & ~(mask)) | ((value) & (mask)))

#define au0828_set(dev, reg, bit) au0828_andor(dev, (reg), (bit), (bit))
#define au0828_clear(dev, reg, bit) au0828_andor(dev, (reg), (bit), 0)

/* ----------------------------------------------------------- */
/* au0828-core.c */
extern u32 au0828_read(struct au0828_dev *dev, u16 reg);
extern u32 au0828_write(struct au0828_dev *dev, u16 reg, u32 val);
extern int au0828_debug;

/* ----------------------------------------------------------- */
/* au0828-cards.c */
extern struct au0828_board au0828_boards[];
extern struct usb_device_id au0828_usb_id_table[];
extern void au0828_gpio_setup(struct au0828_dev *dev);
extern int au0828_tuner_callback(void *priv, int component,
				 int command, int arg);
extern void au0828_card_setup(struct au0828_dev *dev);

/* ----------------------------------------------------------- */
/* au0828-i2c.c */
extern int au0828_i2c_register(struct au0828_dev *dev);
extern int au0828_i2c_unregister(struct au0828_dev *dev);
extern void au0828_call_i2c_clients(struct au0828_dev *dev,
	unsigned int cmd, void *arg);

/* ----------------------------------------------------------- */
/* au0828-dvb.c */
extern int au0828_dvb_register(struct au0828_dev *dev);
extern void au0828_dvb_unregister(struct au0828_dev *dev);

#define dprintk(level, fmt, arg...)\
	do { if (au0828_debug & level)\
		printk(KERN_DEBUG DRIVER_NAME "/0: " fmt, ## arg);\
	} while (0)
