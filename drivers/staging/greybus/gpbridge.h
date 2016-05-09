/*
 * Greybus GPBridge phy driver
 *
 * Copyright 2016 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __GPBRIDGE_H
#define __GPBRIDGE_H

struct gpbridge_device {
	u32 id;
	struct greybus_descriptor_cport *cport_desc;
	struct gb_bundle *bundle;
	struct list_head list;
	struct device dev;
};
#define to_gpbridge_dev(d) container_of(d, struct gpbridge_device, dev)

static inline void *gb_gpbridge_get_data(struct gpbridge_device *gdev)
{
	return dev_get_drvdata(&gdev->dev);
}

static inline void gb_gpbridge_set_data(struct gpbridge_device *gdev, void *data)
{
	dev_set_drvdata(&gdev->dev, data);
}

struct gpbridge_device_id {
	__u8 protocol_id;
};

#define GPBRIDGE_PROTOCOL(p)		\
	.protocol_id	= (p),

struct gpbridge_driver {
	const char *name;
	int (*probe)(struct gpbridge_device *,
		     const struct gpbridge_device_id *id);
	void (*remove)(struct gpbridge_device *);
	const struct gpbridge_device_id *id_table;

	struct device_driver driver;
};
#define to_gpbridge_driver(d) container_of(d, struct gpbridge_driver, driver)

int gb_gpbridge_get_version(struct gb_connection *connection);
int gb_gpbridge_register_driver(struct gpbridge_driver *driver,
			     struct module *owner, const char *mod_name);
void gb_gpbridge_deregister_driver(struct gpbridge_driver *driver);

#define gb_gpbridge_register(driver) \
	gb_gpbridge_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)
#define gb_gpbridge_deregister(driver) \
	gb_gpbridge_deregister_driver(driver)

#define gb_gpbridge_builtin_driver(__driver)		\
	int __init gb_##__driver##_init(void)		\
{							\
	return gb_gpbridge_register(&__driver);		\
}							\
void gb_##__driver##_exit(void)				\
{							\
	gb_gpbridge_deregister(&__driver);			\
}

extern int gb_gpio_driver_init(void);
extern void gb_gpio_driver_exit(void);

extern int gb_pwm_driver_init(void);
extern void gb_pwm_driver_exit(void);

extern int gb_uart_driver_init(void);
extern void gb_uart_driver_exit(void);

extern int gb_sdio_driver_init(void);
extern void gb_sdio_driver_exit(void);

extern int gb_usb_driver_init(void);
extern void gb_usb_driver_exit(void);

extern int gb_i2c_driver_init(void);
extern void gb_i2c_driver_exit(void);

extern int gb_spi_driver_init(void);
extern void gb_spi_driver_exit(void);

/**
 * module_gpbridge_driver() - Helper macro for registering a gpbridge driver
 * @__gpbridge_driver: gpbridge_driver structure
 *
 * Helper macro for gpbridge drivers to set up proper module init / exit
 * functions.  Replaces module_init() and module_exit() and keeps people from
 * printing pointless things to the kernel log when their driver is loaded.
 */
#define module_gpbridge_driver(__gpbridge_driver)	\
	module_driver(__gpbridge_driver, gb_gpbridge_register, gb_gpbridge_deregister)

#endif /* __GPBRIDGE_H */

