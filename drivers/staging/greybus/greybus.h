/*
 * Greybus driver and device API
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __LINUX_GREYBUS_H
#define __LINUX_GREYBUS_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/module.h>
#include "greybus_id.h"
#include "greybus_desc.h"


#define GREYBUS_DEVICE_ID_MATCH_DEVICE \
		(GREYBUS_DEVICE_ID_MATCH_VENDOR | GREYBUS_DEVICE_ID_MATCH_PRODUCT)

#define GREYBUS_DEVICE(v, p)					\
	.match_flags	= GREYBUS_DEVICE_ID_MATCH_DEVICE,	\
	.vendor		= (v),					\
	.product	= (p),

#define GREYBUS_DEVICE_SERIAL(s)				\
	.match_flags	= GREYBUS_DEVICE_ID_MATCH_SERIAL,	\
	.serial_number	= (s),



struct gbuf;

struct cport {
	u16	number;
	// FIXME, what else?
};

typedef void (*gbuf_complete_t)(struct gbuf *gbuf);

struct gbuf {
	struct kref kref;
	void *hcpriv;

	struct list_head anchor_list;
	struct gbuf_anchor *anchor;	// FIXME do we need?

	struct greybus_device *gdev;
	struct cport *cport;
	int status;
	void *transfer_buffer;
	u32 transfer_flags;		/* flags for the transfer buffer */
	u32 transfer_buffer_length;
	u32 actual_length;

	struct scatterlist *sg;		// FIXME do we need?
	int num_sgs;

	void *context;
	gbuf_complete_t complete;
};

/*
 * gbuf->transfer_flags
 */
#define GBUF_FREE_BUFFER	BIT(0)	/* Free the transfer buffer with the gbuf */

/* For SP1 hardware, we are going to "hardcode" each device to have all logical
 * blocks in order to be able to address them as one unified "unit".  Then
 * higher up layers will then be able to talk to them as one logical block and
 * properly know how they are hooked together (i.e. which i2c port is on the
 * same module as the gpio pins, etc.)
 *
 * So, put the "private" data structures here in greybus.h and link to them off
 * of the "main" greybus_device structure.
 */

struct gb_i2c_device;
struct gb_gpio_device;
struct gb_sdio_host;
struct gb_tty;
struct gb_usb_device;

struct greybus_device {
	struct device dev;
	struct greybus_descriptor_function function;
	struct greybus_descriptor_module_id module_id;
	struct greybus_descriptor_serial_number serial_number;
	int num_cport;
	struct cport cport[0];

	struct gb_i2c_device *gb_i2c_dev;
	struct gb_gpio_device *gb_gpio_dev;
	struct gb_sdio_host *gb_sdio_host;
	struct gb_tty *gb_tty;
	struct gb_usb_device *gb_usb_dev;
};
#define to_greybus_device(d) container_of(d, struct greybus_device, dev)

struct gbuf *greybus_alloc_gbuf(struct greybus_device *gdev,
				struct cport *cport,
				gfp_t mem_flags);
void greybus_free_gbuf(struct gbuf *gbuf);

int greybus_submit_gbuf(struct gbuf *gbuf, gfp_t mem_flags);
int greybus_kill_gbuf(struct gbuf *gbuf);


struct greybus_driver {
	const char *name;

	int (*probe)(struct greybus_device *gdev,
		     const struct greybus_module_id *id);
	void (*disconnect)(struct greybus_device *gdev);

	int (*suspend)(struct greybus_device *gdev, pm_message_t message);
	int (*resume)(struct greybus_device *gdev);

	const struct greybus_module_id *id_table;

	struct device_driver driver;
};
#define to_greybus_driver(d) container_of(d, struct greybus_driver, driver)

static inline void greybus_set_drvdata(struct greybus_device *gdev, void *data)
{
	dev_set_drvdata(&gdev->dev, data);
}

static inline void *greybus_get_drvdata(struct greybus_device *gdev)
{
	return dev_get_drvdata(&gdev->dev);
}

/* Don't call these directly, use the module_greybus_driver() macro instead */
int greybus_register_driver(struct greybus_driver *driver,
			    struct module *module, const char *mod_name);
void greybus_deregister(struct greybus_driver *driver);

/* define to get proper THIS_MODULE and KBUILD_MODNAME values */
#define greybus_register(driver) \
	greybus_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)

/**
 * module_greybus_driver() - Helper macro for registering a Greybus driver
 * @__greybus_driver: greybus_driver structure
 *
 * Helper macro for Greybus drivers to set up proper module init / exit
 * functions.  Replaces module_init() and module_exit() and keeps people from
 * printing pointless things to the kernel log when their driver is loaded.
 */
#define module_greybus_driver(__greybus_driver)	\
	module_driver(__greybus_driver, greybus_register, greybus_deregister)

int greybus_disabled(void);


/* Internal functions to gb module, move to internal .h file eventually. */

int gb_new_ap_msg(u8 *data, int length);
int gb_thread_init(void);
void gb_thread_destroy(void);
int gb_debugfs_init(void);
void gb_debugfs_cleanup(void);

/*
 * Because we are allocating a data structure per "type" in the greybus device,
 * we have static functions for this, not "dynamic" drivers like we really
 * should in the end.
 */
int gb_i2c_probe(struct greybus_device *gdev, const struct greybus_module_id *id);
void gb_i2c_disconnect(struct greybus_device *gdev);
int gb_gpio_probe(struct greybus_device *gdev, const struct greybus_module_id *id);
void gb_gpio_disconnect(struct greybus_device *gdev);
int gb_sdio_probe(struct greybus_device *gdev, const struct greybus_module_id *id);
void gb_sdio_disconnect(struct greybus_device *gdev);
int gb_tty_probe(struct greybus_device *gdev, const struct greybus_module_id *id);
void gb_tty_disconnect(struct greybus_device *gdev);

int gb_tty_init(void);
void gb_tty_exit(void);



#endif /* __KERNEL__ */
#endif /* __LINUX_GREYBUS_H */
