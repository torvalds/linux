/* include/linux/leds/flashlight.h
 * Header of Flashlight Class Device Driver
 *
 * Copyright (C) 2013 Richtek Technology Corp.
 * Patrick Chang <patrick_chang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LINUX_LEDS_FLASHLIGHT_H
#define LINUX_LEDS_FLASHLIGHT_H

#include <linux/device.h>
#include <linux/mutex.h>

typedef enum flashlight_type {
	FLASHLIGHT_TYPE_XENON = 0,
	FLASHLIGHT_TYPE_LED,
	FLASHLIFHT_TYPE_BULB,
	FLASHLIGHT_TYPE_MAX,
} flashlight_type_t;
typedef enum flashlight_mode {
	FLASHLIGHT_MODE_OFF = 0,
	FLASHLIGHT_MODE_TORCH,
	FLASHLIGHT_MODE_FLASH,
	/* MIXED mode means TORCH + FLASH */
	FLASHLIGHT_MODE_MIXED,
	FLASHLIGHT_MODE_MAX,
} flashlight_mode_t;

struct flashlight_device;

typedef int (*flashlight_charge_event_cb) (void *data, int remains);

struct flashlight_ops {
	int (*set_torch_brightness)(struct flashlight_device *, int);
	int (*set_strobe_brightness)(struct flashlight_device *, int);
	int (*set_strobe_timeout)(struct flashlight_device *, int);
	int (*list_strobe_timeout)(struct flashlight_device *, int);
	int (*set_mode)(struct flashlight_device *, int);
	int (*set_color_temperature)(struct flashlight_device *, int);
	int (*list_color_temperature)(struct flashlight_device *, int);
	int (*strobe_charge)(struct flashlight_device *,
			      flashlight_charge_event_cb, void *, int);
	int (*strobe)(struct flashlight_device *);
	int (*suspend)(struct flashlight_device *, pm_message_t);
	int (*resume)(struct flashlight_device *);
};

struct flashlight_properties {
	/* Flashlight type */
	enum flashlight_type type;
	/* Xenon type flashlight doesn't support torch mode */
	enum flashlight_mode mode;
	/* Color temperature, unit: K, 0 means unknown */
	int color_temperature;
	int torch_brightness;
	int torch_max_brightness;
	int strobe_brightness;
	int strobe_max_brightness;
	int strobe_delay;
	int strobe_timeout;
	const char *alias_name;
};

struct flashlight_device {
	/* Flashlight properties */
	struct flashlight_properties props;
	const struct flashlight_ops *ops;
	struct mutex ops_lock;
	struct device dev;
};

extern struct flashlight_device *flashlight_device_register(const char *name,
							struct device *parent,
							void *devdata,
							const struct
							flashlight_ops * ops,
							const struct
							flashlight_properties
							*props);
extern void flashlight_device_unregister(struct flashlight_device
					 *flashlight_dev);
extern struct flashlight_device *find_flashlight_by_name(char *name);
extern int flashlight_list_color_temperature(struct flashlight_device
					     *flashlight_dev, int selector);
extern int flashlight_set_color_temperature(struct flashlight_device
					    *flashlight_dev, int minK,
					    int maxK);
extern int flashlight_set_torch_brightness(struct flashlight_device
					   *flashlight_dev,
					   int brightness_level);
extern int flashlight_set_strobe_brightness(struct flashlight_device
					    *flashlight_dev,
					    int brightness_level);
extern int flashlight_list_strobe_timeout(struct flashlight_device
					  *flashlight_dev, int selector);
extern int flashlight_set_strobe_timeout(struct flashlight_device
					 *flashlight_dev, int min_ms,
					 int max_ms);
extern int flashlight_set_mode(struct flashlight_device *flashlight_dev,
			       int mode);

extern int flashlight_strobe(struct flashlight_device *flashlight_dev);

/* flashlight_charge_event_cb(void *data, int remains)
 * description :
 *   callback function of flashlight charging progress
 * arguments :
 *  @data : data pass by flashlight_strobe_charge()
 *  @remains : remained time to full chargerd, unit : ms ; 0 means ready
 * return : 0 means succeess, otherwise see definitions in errno.h
 */

/* flashlight_strobe_chargestruct flashlight_device *flashlight_dev,
 *                      flashlight_charge_event_cb cb, void *data, int start)
 * description :
 * flashlight start / stop  charging
 * @flashlight_dev : flashlight devices
 * @flashlight_charge_event_cb : callback function to report progress
 * @data : bypass to callback function
 * @start : 1 means start; 0 means stop
 */
extern int flashlight_strobe_charge(struct flashlight_device *flashlight_dev,
				    flashlight_charge_event_cb cb, void *data,
				    int start);

#define to_flashlight_device(obj) \
	container_of(obj, struct flashlight_device, dev)

static inline void *flashlight_get_data(struct flashlight_device
					*flashlight_dev)
{
	return dev_get_drvdata(&flashlight_dev->dev);
}
#endif /*LINUX_LEDS_FLASHLIGHT_H */
