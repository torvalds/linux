/*
 * LP55XX Common Driver Header
 *
 * Copyright (C) 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * Derived from leds-lp5521.c, leds-lp5523.c
 */

#ifndef _LEDS_LP55XX_COMMON_H
#define _LEDS_LP55XX_COMMON_H

enum lp55xx_engine_index {
	LP55XX_ENGINE_INVALID,
	LP55XX_ENGINE_1,
	LP55XX_ENGINE_2,
	LP55XX_ENGINE_3,
	LP55XX_ENGINE_MAX = LP55XX_ENGINE_3,
};

enum lp55xx_engine_mode {
	LP55XX_ENGINE_DISABLED,
	LP55XX_ENGINE_LOAD,
	LP55XX_ENGINE_RUN,
};

#define LP55XX_DEV_ATTR_RW(name, show, store)	\
	DEVICE_ATTR(name, S_IRUGO | S_IWUSR, show, store)
#define LP55XX_DEV_ATTR_RO(name, show)		\
	DEVICE_ATTR(name, S_IRUGO, show, NULL)
#define LP55XX_DEV_ATTR_WO(name, store)		\
	DEVICE_ATTR(name, S_IWUSR, NULL, store)

#define show_mode(nr)							\
static ssize_t show_engine##nr##_mode(struct device *dev,		\
				    struct device_attribute *attr,	\
				    char *buf)				\
{									\
	return show_engine_mode(dev, attr, buf, nr);			\
}

#define store_mode(nr)							\
static ssize_t store_engine##nr##_mode(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t len)	\
{									\
	return store_engine_mode(dev, attr, buf, len, nr);		\
}

#define show_leds(nr)							\
static ssize_t show_engine##nr##_leds(struct device *dev,		\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	return show_engine_leds(dev, attr, buf, nr);			\
}

#define store_leds(nr)						\
static ssize_t store_engine##nr##_leds(struct device *dev,	\
			     struct device_attribute *attr,	\
			     const char *buf, size_t len)	\
{								\
	return store_engine_leds(dev, attr, buf, len, nr);	\
}

#define store_load(nr)							\
static ssize_t store_engine##nr##_load(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t len)	\
{									\
	return store_engine_load(dev, attr, buf, len, nr);		\
}

struct lp55xx_led;
struct lp55xx_chip;

/*
 * struct lp55xx_reg
 * @addr : Register address
 * @val  : Register value
 */
struct lp55xx_reg {
	u8 addr;
	u8 val;
};

/*
 * struct lp55xx_device_config
 * @reset              : Chip specific reset command
 * @enable             : Chip specific enable command
 * @max_channel        : Maximum number of channels
 * @post_init_device   : Chip specific initialization code
 * @brightness_work_fn : Brightness work function
 * @set_led_current    : LED current set function
 * @firmware_cb        : Call function when the firmware is loaded
 * @run_engine         : Run internal engine for pattern
 * @dev_attr_group     : Device specific attributes
 */
struct lp55xx_device_config {
	const struct lp55xx_reg reset;
	const struct lp55xx_reg enable;
	const int max_channel;

	/* define if the device has specific initialization process */
	int (*post_init_device) (struct lp55xx_chip *chip);

	/* access brightness register */
	void (*brightness_work_fn)(struct work_struct *work);

	/* current setting function */
	void (*set_led_current) (struct lp55xx_led *led, u8 led_current);

	/* access program memory when the firmware is loaded */
	void (*firmware_cb)(struct lp55xx_chip *chip);

	/* used for running firmware LED patterns */
	void (*run_engine) (struct lp55xx_chip *chip, bool start);

	/* additional device specific attributes */
	const struct attribute_group *dev_attr_group;
};

/*
 * struct lp55xx_engine
 * @mode       : Engine mode
 * @led_mux    : Mux bits for LED selection. Only used in LP5523
 */
struct lp55xx_engine {
	enum lp55xx_engine_mode mode;
	u16 led_mux;
};

/*
 * struct lp55xx_chip
 * @cl         : I2C communication for access registers
 * @pdata      : Platform specific data
 * @lock       : Lock for user-space interface
 * @num_leds   : Number of registered LEDs
 * @cfg        : Device specific configuration data
 * @engine_idx : Selected engine number
 * @engines    : Engine structure for the device attribute R/W interface
 * @fw         : Firmware data for running a LED pattern
 */
struct lp55xx_chip {
	struct i2c_client *cl;
	struct clk *clk;
	struct lp55xx_platform_data *pdata;
	struct mutex lock;	/* lock for user-space interface */
	int num_leds;
	struct lp55xx_device_config *cfg;
	enum lp55xx_engine_index engine_idx;
	struct lp55xx_engine engines[LP55XX_ENGINE_MAX];
	const struct firmware *fw;
};

/*
 * struct lp55xx_led
 * @chan_nr         : Channel number
 * @cdev            : LED class device
 * @led_current     : Current setting at each led channel
 * @max_current     : Maximun current at each led channel
 * @brightness_work : Workqueue for brightness control
 * @brightness      : Brightness value
 * @chip            : The lp55xx chip data
 */
struct lp55xx_led {
	int chan_nr;
	struct led_classdev cdev;
	u8 led_current;
	u8 max_current;
	struct work_struct brightness_work;
	u8 brightness;
	struct lp55xx_chip *chip;
};

/* register access */
extern int lp55xx_write(struct lp55xx_chip *chip, u8 reg, u8 val);
extern int lp55xx_read(struct lp55xx_chip *chip, u8 reg, u8 *val);
extern int lp55xx_update_bits(struct lp55xx_chip *chip, u8 reg,
			u8 mask, u8 val);

/* external clock detection */
extern bool lp55xx_is_extclk_used(struct lp55xx_chip *chip);

/* common device init/deinit functions */
extern int lp55xx_init_device(struct lp55xx_chip *chip);
extern void lp55xx_deinit_device(struct lp55xx_chip *chip);

/* common LED class device functions */
extern int lp55xx_register_leds(struct lp55xx_led *led,
				struct lp55xx_chip *chip);
extern void lp55xx_unregister_leds(struct lp55xx_led *led,
				struct lp55xx_chip *chip);

/* common device attributes functions */
extern int lp55xx_register_sysfs(struct lp55xx_chip *chip);
extern void lp55xx_unregister_sysfs(struct lp55xx_chip *chip);

/* common device tree population function */
extern struct lp55xx_platform_data
*lp55xx_of_populate_pdata(struct device *dev, struct device_node *np);

#endif /* _LEDS_LP55XX_COMMON_H */
