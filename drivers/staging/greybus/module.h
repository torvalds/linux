/*
 * Greybus modules
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __MODULE_H
#define __MODULE_H

/* Increase these values if needed */
#define MAX_CPORTS_PER_MODULE	10
#define MAX_STRINGS_PER_MODULE	10

struct gb_module {
	struct device dev;

	struct list_head links;	/* greybus_host_device->modules */
	u8 module_id;		/* Physical location within the Endo */

	struct greybus_descriptor_module module;
	int num_cports;
	int num_strings;
	u16 cport_ids[MAX_CPORTS_PER_MODULE];
	struct gmod_string *string[MAX_STRINGS_PER_MODULE];

	struct greybus_host_device *hd;

	struct gb_i2c_device *gb_i2c_dev;
	struct gb_gpio_device *gb_gpio_dev;
	struct gb_sdio_host *gb_sdio_host;
	struct gb_tty *gb_tty;
	struct gb_usb_device *gb_usb_dev;
	struct gb_battery *gb_battery;
};
#define to_gb_module(d) container_of(d, struct gb_module, dev)

static inline void
gb_module_set_drvdata(struct gb_module *gmod, void *data)
{
	dev_set_drvdata(&gmod->dev, data);
}

static inline void *gb_module_get_drvdata(struct gb_module *gmod)
{
	return dev_get_drvdata(&gmod->dev);
}

const struct greybus_module_id *gb_module_match_id(struct gb_module *gmod,
					const struct greybus_module_id *id);

struct gb_module *gb_module_create(struct greybus_host_device *hd,
					u8 module_id);
void gb_module_destroy(struct gb_module *module);

#endif /* __MODULE_H */
