/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  PS3 virtual uart
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
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
	struct ps3_system_bus_device *dev; /* to convert work to device */
};

/**
 * struct ps3_vuart_port_driver - a driver for a device on a vuart port
 */

struct ps3_vuart_port_driver {
	struct ps3_system_bus_driver core;
	int (*probe)(struct ps3_system_bus_device *);
	int (*remove)(struct ps3_system_bus_device *);
	void (*shutdown)(struct ps3_system_bus_device *);
	void (*work)(struct ps3_system_bus_device *);
	/* int (*tx_event)(struct ps3_system_bus_device *dev); */
	/* int (*rx_event)(struct ps3_system_bus_device *dev); */
	/* int (*disconnect_event)(struct ps3_system_bus_device *dev); */
	/* int (*suspend)(struct ps3_system_bus_device *, pm_message_t); */
	/* int (*resume)(struct ps3_system_bus_device *); */
};

int ps3_vuart_port_driver_register(struct ps3_vuart_port_driver *drv);
void ps3_vuart_port_driver_unregister(struct ps3_vuart_port_driver *drv);

static inline struct ps3_vuart_port_driver *
	ps3_system_bus_dev_to_vuart_drv(struct ps3_system_bus_device *_dev)
{
	struct ps3_system_bus_driver *sbd =
		ps3_system_bus_dev_to_system_bus_drv(_dev);
	BUG_ON(!sbd);
	return container_of(sbd, struct ps3_vuart_port_driver, core);
}
static inline struct ps3_system_bus_device *ps3_vuart_work_to_system_bus_dev(
	struct work_struct *_work)
{
	struct ps3_vuart_work *vw = container_of(_work, struct ps3_vuart_work,
		work);
	return vw->dev;
}

int ps3_vuart_write(struct ps3_system_bus_device *dev, const void *buf,
	unsigned int bytes);
int ps3_vuart_read(struct ps3_system_bus_device *dev, void *buf,
	unsigned int bytes);
int ps3_vuart_read_async(struct ps3_system_bus_device *dev, unsigned int bytes);
void ps3_vuart_cancel_async(struct ps3_system_bus_device *dev);
void ps3_vuart_clear_rx_bytes(struct ps3_system_bus_device *dev,
	unsigned int bytes);

struct vuart_triggers {
	unsigned long rx;
	unsigned long tx;
};

int ps3_vuart_get_triggers(struct ps3_system_bus_device *dev,
	struct vuart_triggers *trig);
int ps3_vuart_set_triggers(struct ps3_system_bus_device *dev, unsigned int tx,
	unsigned int rx);
int ps3_vuart_enable_interrupt_tx(struct ps3_system_bus_device *dev);
int ps3_vuart_disable_interrupt_tx(struct ps3_system_bus_device *dev);
int ps3_vuart_enable_interrupt_rx(struct ps3_system_bus_device *dev);
int ps3_vuart_disable_interrupt_rx(struct ps3_system_bus_device *dev);
int ps3_vuart_enable_interrupt_disconnect(struct ps3_system_bus_device *dev);
int ps3_vuart_disable_interrupt_disconnect(struct ps3_system_bus_device *dev);

#endif
