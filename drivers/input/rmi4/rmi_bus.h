/*
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _RMI_BUS_H
#define _RMI_BUS_H

#include <linux/rmi.h>

struct rmi_device;

/**
 * struct rmi_function - represents the implementation of an RMI4
 * function for a particular device (basically, a driver for that RMI4 function)
 *
 * @fd: The function descriptor of the RMI function
 * @rmi_dev: Pointer to the RMI device associated with this function container
 * @dev: The device associated with this particular function.
 *
 * @num_of_irqs: The number of irqs needed by this function
 * @irq_pos: The position in the irq bitfield this function holds
 * @irq_mask: For convenience, can be used to mask IRQ bits off during ATTN
 * interrupt handling.
 *
 * @node: entry in device's list of functions
 */
struct rmi_function {
	struct rmi_function_descriptor fd;
	struct rmi_device *rmi_dev;
	struct device dev;
	struct list_head node;

	unsigned int num_of_irqs;
	unsigned int irq_pos;
	unsigned long irq_mask[];
};

#define to_rmi_function(d)	container_of(d, struct rmi_function, dev)

bool rmi_is_function_device(struct device *dev);

int __must_check rmi_register_function(struct rmi_function *);
void rmi_unregister_function(struct rmi_function *);

/**
 * struct rmi_function_handler - driver routines for a particular RMI function.
 *
 * @func: The RMI function number
 * @reset: Called when a reset of the touch sensor is detected.  The routine
 * should perform any out-of-the-ordinary reset handling that might be
 * necessary.  Restoring of touch sensor configuration registers should be
 * handled in the config() callback, below.
 * @config: Called when the function container is first initialized, and
 * after a reset is detected.  This routine should write any necessary
 * configuration settings to the device.
 * @attention: Called when the IRQ(s) for the function are set by the touch
 * sensor.
 * @suspend: Should perform any required operations to suspend the particular
 * function.
 * @resume: Should perform any required operations to resume the particular
 * function.
 *
 * All callbacks are expected to return 0 on success, error code on failure.
 */
struct rmi_function_handler {
	struct device_driver driver;

	u8 func;

	int (*probe)(struct rmi_function *fn);
	void (*remove)(struct rmi_function *fn);
	int (*config)(struct rmi_function *fn);
	int (*reset)(struct rmi_function *fn);
	int (*attention)(struct rmi_function *fn, unsigned long *irq_bits);
	int (*suspend)(struct rmi_function *fn);
	int (*resume)(struct rmi_function *fn);
};

#define to_rmi_function_handler(d) \
		container_of(d, struct rmi_function_handler, driver)

int __must_check __rmi_register_function_handler(struct rmi_function_handler *,
						 struct module *, const char *);
#define rmi_register_function_handler(handler) \
	__rmi_register_function_handler(handler, THIS_MODULE, KBUILD_MODNAME)

void rmi_unregister_function_handler(struct rmi_function_handler *);

#define to_rmi_driver(d) \
	container_of(d, struct rmi_driver, driver)

#define to_rmi_device(d) container_of(d, struct rmi_device, dev)

static inline struct rmi_device_platform_data *
rmi_get_platform_data(struct rmi_device *d)
{
	return &d->xport->pdata;
}

bool rmi_is_physical_device(struct device *dev);

/**
 * rmi_read - read a single byte
 * @d: Pointer to an RMI device
 * @addr: The address to read from
 * @buf: The read buffer
 *
 * Reads a single byte of data using the underlying transport protocol
 * into memory pointed by @buf. It returns 0 on success or a negative
 * error code.
 */
static inline int rmi_read(struct rmi_device *d, u16 addr, u8 *buf)
{
	return d->xport->ops->read_block(d->xport, addr, buf, 1);
}

/**
 * rmi_read_block - read a block of bytes
 * @d: Pointer to an RMI device
 * @addr: The start address to read from
 * @buf: The read buffer
 * @len: Length of the read buffer
 *
 * Reads a block of byte data using the underlying transport protocol
 * into memory pointed by @buf. It returns 0 on success or a negative
 * error code.
 */
static inline int rmi_read_block(struct rmi_device *d, u16 addr,
				 void *buf, size_t len)
{
	return d->xport->ops->read_block(d->xport, addr, buf, len);
}

/**
 * rmi_write - write a single byte
 * @d: Pointer to an RMI device
 * @addr: The address to write to
 * @data: The data to write
 *
 * Writes a single byte using the underlying transport protocol. It
 * returns zero on success or a negative error code.
 */
static inline int rmi_write(struct rmi_device *d, u16 addr, u8 data)
{
	return d->xport->ops->write_block(d->xport, addr, &data, 1);
}

/**
 * rmi_write_block - write a block of bytes
 * @d: Pointer to an RMI device
 * @addr: The start address to write to
 * @buf: The write buffer
 * @len: Length of the write buffer
 *
 * Writes a block of byte data from buf using the underlaying transport
 * protocol.  It returns the amount of bytes written or a negative error code.
 */
static inline int rmi_write_block(struct rmi_device *d, u16 addr,
				  const void *buf, size_t len)
{
	return d->xport->ops->write_block(d->xport, addr, buf, len);
}

int rmi_for_each_dev(void *data, int (*func)(struct device *dev, void *data));

extern struct bus_type rmi_bus_type;

int rmi_of_property_read_u32(struct device *dev, u32 *result,
				const char *prop, bool optional);

#define RMI_DEBUG_CORE			BIT(0)
#define RMI_DEBUG_XPORT			BIT(1)
#define RMI_DEBUG_FN			BIT(2)
#define RMI_DEBUG_2D_SENSOR		BIT(3)

void rmi_dbg(int flags, struct device *dev, const char *fmt, ...);
#endif
