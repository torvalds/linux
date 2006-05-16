/*********************************************************************
 *
 *	sir.h:	include file for irda-sir device abstraction layer
 *
 *	Copyright (c) 2002 Martin Diehl
 *
 *	This program is free software; you can redistribute it and/or 
 *	modify it under the terms of the GNU General Public License as 
 *	published by the Free Software Foundation; either version 2 of 
 *	the License, or (at your option) any later version.
 *
 ********************************************************************/

#ifndef IRDA_SIR_H
#define IRDA_SIR_H

#include <linux/netdevice.h>
#include <linux/workqueue.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>		// iobuff_t

struct sir_fsm {
	struct semaphore	sem;
	struct work_struct      work;
	unsigned		state, substate;
	int			param;
	int			result;
};

#define SIRDEV_STATE_WAIT_TX_COMPLETE	0x0100

/* substates for wait_tx_complete */
#define SIRDEV_STATE_WAIT_XMIT		0x0101
#define SIRDEV_STATE_WAIT_UNTIL_SENT	0x0102
#define SIRDEV_STATE_TX_DONE		0x0103

#define SIRDEV_STATE_DONGLE_OPEN		0x0300

/* 0x0301-0x03ff reserved for individual dongle substates */

#define SIRDEV_STATE_DONGLE_CLOSE	0x0400

/* 0x0401-0x04ff reserved for individual dongle substates */

#define SIRDEV_STATE_SET_DTR_RTS		0x0500

#define SIRDEV_STATE_SET_SPEED		0x0700
#define SIRDEV_STATE_DONGLE_CHECK	0x0800
#define SIRDEV_STATE_DONGLE_RESET	0x0900

/* 0x0901-0x09ff reserved for individual dongle substates */

#define SIRDEV_STATE_DONGLE_SPEED	0x0a00
/* 0x0a01-0x0aff reserved for individual dongle substates */

#define SIRDEV_STATE_PORT_SPEED		0x0b00
#define SIRDEV_STATE_DONE		0x0c00
#define SIRDEV_STATE_ERROR		0x0d00
#define SIRDEV_STATE_COMPLETE		0x0e00

#define SIRDEV_STATE_DEAD		0xffff


struct sir_dev;

struct dongle_driver {

	struct module *owner;

	const char *driver_name;

	IRDA_DONGLE type;

	int	(*open)(struct sir_dev *dev);
	int	(*close)(struct sir_dev *dev);
	int	(*reset)(struct sir_dev *dev);
	int	(*set_speed)(struct sir_dev *dev, unsigned speed);

	struct list_head dongle_list;
};

struct sir_driver {

	struct module *owner;

	const char *driver_name;

	int qos_mtt_bits;

	int (*chars_in_buffer)(struct sir_dev *dev);
	void (*wait_until_sent)(struct sir_dev *dev);
	int (*set_speed)(struct sir_dev *dev, unsigned speed);
	int (*set_dtr_rts)(struct sir_dev *dev, int dtr, int rts);

	int (*do_write)(struct sir_dev *dev, const unsigned char *ptr, size_t len);

	int (*start_dev)(struct sir_dev *dev);
	int (*stop_dev)(struct sir_dev *dev);
};


/* exported */

extern int irda_register_dongle(struct dongle_driver *new);
extern int irda_unregister_dongle(struct dongle_driver *drv);

extern struct sir_dev * sirdev_get_instance(const struct sir_driver *drv, const char *name);
extern int sirdev_put_instance(struct sir_dev *self);

extern int sirdev_set_dongle(struct sir_dev *dev, IRDA_DONGLE type);
extern void sirdev_write_complete(struct sir_dev *dev);
extern int sirdev_receive(struct sir_dev *dev, const unsigned char *cp, size_t count);

/* low level helpers for SIR device/dongle setup */
extern int sirdev_raw_write(struct sir_dev *dev, const char *buf, int len);
extern int sirdev_raw_read(struct sir_dev *dev, char *buf, int len);
extern int sirdev_set_dtr_rts(struct sir_dev *dev, int dtr, int rts);

/* not exported */

extern int sirdev_get_dongle(struct sir_dev *self, IRDA_DONGLE type);
extern int sirdev_put_dongle(struct sir_dev *self);

extern void sirdev_enable_rx(struct sir_dev *dev);
extern int sirdev_schedule_request(struct sir_dev *dev, int state, unsigned param);

/* inline helpers */

static inline int sirdev_schedule_speed(struct sir_dev *dev, unsigned speed)
{
	return sirdev_schedule_request(dev, SIRDEV_STATE_SET_SPEED, speed);
}

static inline int sirdev_schedule_dongle_open(struct sir_dev *dev, int dongle_id)
{
	return sirdev_schedule_request(dev, SIRDEV_STATE_DONGLE_OPEN, dongle_id);
}

static inline int sirdev_schedule_dongle_close(struct sir_dev *dev)
{
	return sirdev_schedule_request(dev, SIRDEV_STATE_DONGLE_CLOSE, 0);
}

static inline int sirdev_schedule_dtr_rts(struct sir_dev *dev, int dtr, int rts)
{
	int	dtrrts;

	dtrrts = ((dtr) ? 0x02 : 0x00) | ((rts) ? 0x01 : 0x00);
	return sirdev_schedule_request(dev, SIRDEV_STATE_SET_DTR_RTS, dtrrts);
}

#if 0
static inline int sirdev_schedule_mode(struct sir_dev *dev, int mode)
{
	return sirdev_schedule_request(dev, SIRDEV_STATE_SET_MODE, mode);
}
#endif


struct sir_dev {
	struct net_device *netdev;
	struct net_device_stats stats;

	struct irlap_cb    *irlap;

	struct qos_info qos;

	char hwname[32];

	struct sir_fsm fsm;
	atomic_t enable_rx;
	int raw_tx;
	spinlock_t tx_lock;

	u32 new_speed;
 	u32 flags;

	unsigned	speed;

	iobuff_t tx_buff;          /* Transmit buffer */
	iobuff_t rx_buff;          /* Receive buffer */
	struct sk_buff *tx_skb;

	const struct dongle_driver * dongle_drv;
	const struct sir_driver * drv;
	void *priv;

};

#endif	/* IRDA_SIR_H */
