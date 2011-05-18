/* The industrial I/O core, trigger handling functions
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/irq.h>

#ifndef _IIO_TRIGGER_H_
#define _IIO_TRIGGER_H_

struct iio_subirq {
	bool enabled;
};

/**
 * struct iio_trigger - industrial I/O trigger device
 *
 * @id:			[INTERN] unique id number
 * @name:		[DRIVER] unique name
 * @dev:		[DRIVER] associated device (if relevant)
 * @private_data:	[DRIVER] device specific data
 * @list:		[INTERN] used in maintenance of global trigger list
 * @alloc_list:		[DRIVER] used for driver specific trigger list
 * @control_attrs:	[DRIVER] sysfs attributes relevant to trigger type
 * @owner:		[DRIVER] used to monitor usage count of the trigger.
 * @use_count:		use count for the trigger
 * @set_trigger_state:	[DRIVER] switch on/off the trigger on demand
 * @try_reenable:	function to reenable the trigger when the
 *			use count is zero (may be NULL)
 **/
struct iio_trigger {
	int				id;
	const char			*name;
	struct device			dev;

	void				*private_data;
	struct list_head		list;
	struct list_head		alloc_list;
	const struct attribute_group	*control_attrs;
	struct module			*owner;
	int use_count;

	int (*set_trigger_state)(struct iio_trigger *trig, bool state);
	int (*try_reenable)(struct iio_trigger *trig);

	struct irq_chip			subirq_chip;
	int				subirq_base;

	struct iio_subirq subirqs[CONFIG_IIO_CONSUMERS_PER_TRIGGER];
	unsigned long pool[BITS_TO_LONGS(CONFIG_IIO_CONSUMERS_PER_TRIGGER)];
	struct mutex			pool_lock;
};

static inline struct iio_trigger *to_iio_trigger(struct device *d)
{
	return container_of(d, struct iio_trigger, dev);
};

static inline void iio_put_trigger(struct iio_trigger *trig)
{
	put_device(&trig->dev);
	module_put(trig->owner);
};

static inline void iio_get_trigger(struct iio_trigger *trig)
{
	__module_get(trig->owner);
	get_device(&trig->dev);
};

/**
 * iio_trigger_read_name() - sysfs access function to get the trigger name
 * @dev: the system device
 * @attr: device attributes for the device
 * @buf: output buffer to store the trigger name
 **/
ssize_t iio_trigger_read_name(struct device *dev,
			      struct device_attribute *attr,
			      char *buf);

#define IIO_TRIGGER_NAME_ATTR DEVICE_ATTR(name, S_IRUGO,		\
					  iio_trigger_read_name,	\
					  NULL);

/**
 * iio_trigger_register() - register a trigger with the IIO core
 * @trig_info:	trigger to be registered
 **/
int iio_trigger_register(struct iio_trigger *trig_info);

/**
 * iio_trigger_unregister() - unregister a trigger from the core
 * @trig_info:	trigger to be unregistered
 **/
void iio_trigger_unregister(struct iio_trigger *trig_info);

/**
 * iio_trigger_attach_poll_func() - add a function pair to be run on trigger
 * @trig:	trigger to which the function pair are being added
 * @pf:		poll function pair
 **/
int iio_trigger_attach_poll_func(struct iio_trigger *trig,
				 struct iio_poll_func *pf);

/**
 * iio_trigger_dettach_poll_func() -	remove function pair from those to be
 *					run on trigger
 * @trig:	trigger from which the function is being removed
 * @pf:		poll function pair
 **/
int iio_trigger_dettach_poll_func(struct iio_trigger *trig,
				  struct iio_poll_func *pf);

/**
 * iio_trigger_poll() - called on a trigger occurring
 * @trig: trigger which occurred
 *
 * Typically called in relevant hardware interrupt handler.
 **/
void iio_trigger_poll(struct iio_trigger *trig, s64 time);
void iio_trigger_poll_chained(struct iio_trigger *trig, s64 time);
void iio_trigger_notify_done(struct iio_trigger *trig);

irqreturn_t iio_trigger_generic_data_rdy_poll(int irq, void *private);

static inline int iio_trigger_get_irq(struct iio_trigger *trig)
{
	int ret;
	mutex_lock(&trig->pool_lock);
	ret = bitmap_find_free_region(trig->pool,
				      CONFIG_IIO_CONSUMERS_PER_TRIGGER,
				      ilog2(1));
	mutex_unlock(&trig->pool_lock);
	if (ret >= 0)
		ret += trig->subirq_base;

	return ret;
};

static inline void iio_trigger_put_irq(struct iio_trigger *trig, int irq)
{
	mutex_lock(&trig->pool_lock);
	clear_bit(irq - trig->subirq_base, trig->pool);
	mutex_unlock(&trig->pool_lock);
};

/**
 * struct iio_poll_func - poll function pair
 *
 * @private_data:		data specific to device (passed into poll func)
 * @h:				the function that is actually run on trigger
 * @thread:			threaded interrupt part
 * @type:			the type of interrupt (basically if oneshot)
 * @irq:			the corresponding irq as allocated from the
 *				trigger pool
 * @timestamp:			some devices need a timestamp grabbed as soon
 *				as possible after the trigger - hence handler
 *				passes it via here.
 **/
struct iio_poll_func {
	void				*private_data;
	irqreturn_t (*h)(int irq, void *p);
	irqreturn_t (*thread)(int irq, void *p);
	int type;
	char *name;
	int irq;
	s64 timestamp;
};

irqreturn_t iio_pollfunc_store_time(int irq, void *p);

/*
 * Two functions for common case where all that happens is a pollfunc
 * is attached and detached from a trigger
 */
int iio_triggered_ring_postenable(struct iio_dev *indio_dev);
int iio_triggered_ring_predisable(struct iio_dev *indio_dev);

struct iio_trigger *iio_allocate_trigger(void);
struct iio_trigger *iio_allocate_trigger_named(const char *name);
void iio_free_trigger(struct iio_trigger *trig);

#endif /* _IIO_TRIGGER_H_ */
