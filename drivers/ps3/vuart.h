/*
 *  PS3 virtual uart
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined(_PS3_VUART_H)
#define _PS3_VUART_H

#include <asm/ps3.h>

struct ps3_vuart_stats {
	unsigned long bytes_written;
	unsigned long bytes_read;
	unsigned long tx_interrupts;
	unsigned long rx_interrupts;
	unsigned long disconnect_interrupts;
};

struct ps3_vuart_work {
	struct work_struct work;
	unsigned long trigger;
	spinlock_t lock;
	struct ps3_vuart_port_device* dev; /* to convert work to device */
};

/**
 * struct ps3_vuart_port_priv - private vuart device data.
 */

struct ps3_vuart_port_priv {
	unsigned int port_number;
	u64 interrupt_mask;

	struct {
		spinlock_t lock;
		struct list_head head;
	} tx_list;
	struct {
		unsigned long bytes_held;
		spinlock_t lock;
		struct list_head head;
	} rx_list;
	struct ps3_vuart_stats stats;
	struct ps3_vuart_work work;
};

/**
 * struct ps3_vuart_port_driver - a driver for a device on a vuart port
 */

struct ps3_vuart_port_driver {
	enum ps3_match_id match_id;
	struct device_driver core;
	int (*probe)(struct ps3_vuart_port_device *);
	int (*remove)(struct ps3_vuart_port_device *);
	void (*shutdown)(struct ps3_vuart_port_device *);
	int (*tx_event)(struct ps3_vuart_port_device *dev);
	int (*rx_event)(struct ps3_vuart_port_device *dev);
	int (*disconnect_event)(struct ps3_vuart_port_device *dev);
	/* int (*suspend)(struct ps3_vuart_port_device *, pm_message_t); */
	/* int (*resume)(struct ps3_vuart_port_device *); */
};

int ps3_vuart_port_driver_register(struct ps3_vuart_port_driver *drv);
void ps3_vuart_port_driver_unregister(struct ps3_vuart_port_driver *drv);

static inline struct ps3_vuart_port_driver *to_ps3_vuart_port_driver(
	struct device_driver *_drv)
{
	return container_of(_drv, struct ps3_vuart_port_driver, core);
}
static inline struct ps3_vuart_port_device *to_ps3_vuart_port_device(
	struct device *_dev)
{
	return container_of(_dev, struct ps3_vuart_port_device, core);
}
static inline struct ps3_vuart_port_device *ps3_vuart_work_to_port_device(
	struct work_struct *_work)
{
	struct ps3_vuart_work *vw = container_of(_work, struct ps3_vuart_work,
		work);
	return vw->dev;
}

int ps3_vuart_write(struct ps3_vuart_port_device *dev, const void* buf,
	unsigned int bytes);
int ps3_vuart_read(struct ps3_vuart_port_device *dev, void* buf,
	unsigned int bytes);
int ps3_vuart_read_async(struct ps3_vuart_port_device *dev, work_func_t func,
	unsigned int bytes);
void ps3_vuart_cancel_async(struct ps3_vuart_port_device *dev);
void ps3_vuart_clear_rx_bytes(struct ps3_vuart_port_device *dev,
	unsigned int bytes);

#endif
