/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LP55XX Common Driver Header
 *
 * Copyright (C) 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * Derived from leds-lp5521.c, leds-lp5523.c
 */

#ifndef _LEDS_LP55XX_COMMON_H
#define _LEDS_LP55XX_COMMON_H

#include <linux/led-class-multicolor.h>

#define LP55xx_BYTES_PER_PAGE		32      /* bytes */

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

#define LP55XX_DEV_ATTR_ENGINE_MODE(nr)					\
static ssize_t show_engine##nr##_mode(struct device *dev,		\
				      struct device_attribute *attr,	\
				      char *buf)				\
{									\
	return lp55xx_show_engine_mode(dev, attr, buf, nr);		\
}									\
static ssize_t store_engine##nr##_mode(struct device *dev,		\
				       struct device_attribute *attr,	\
				       const char *buf, size_t len)	\
{									\
	return lp55xx_store_engine_mode(dev, attr, buf, len, nr);	\
}									\
static LP55XX_DEV_ATTR_RW(engine##nr##_mode, show_engine##nr##_mode,	\
			  store_engine##nr##_mode)

#define LP55XX_DEV_ATTR_ENGINE_LEDS(nr)					\
static ssize_t show_engine##nr##_leds(struct device *dev,		\
				      struct device_attribute *attr,	\
				      char *buf)			\
{									\
	return lp55xx_show_engine_leds(dev, attr, buf, nr);		\
}									\
static ssize_t store_engine##nr##_leds(struct device *dev,		\
				       struct device_attribute *attr,	\
				       const char *buf, size_t len)	\
{									\
	return lp55xx_store_engine_leds(dev, attr, buf, len, nr);	\
}									\
static LP55XX_DEV_ATTR_RW(engine##nr##_leds, show_engine##nr##_leds,	\
			  store_engine##nr##_leds)

#define LP55XX_DEV_ATTR_ENGINE_LOAD(nr)					\
static ssize_t store_engine##nr##_load(struct device *dev,		\
				       struct device_attribute *attr,	\
				       const char *buf, size_t len)	\
{									\
	return lp55xx_store_engine_load(dev, attr, buf, len, nr);	\
}									\
static LP55XX_DEV_ATTR_WO(engine##nr##_load, store_engine##nr##_load)

#define LP55XX_DEV_ATTR_MASTER_FADER(nr)				\
static ssize_t show_master_fader##nr(struct device *dev,		\
				     struct device_attribute *attr,	\
				     char *buf)				\
{									\
	return lp55xx_show_master_fader(dev, attr, buf, nr);		\
}									\
static ssize_t store_master_fader##nr(struct device *dev,		\
				      struct device_attribute *attr,	\
				      const char *buf, size_t len)	\
{									\
	return lp55xx_store_master_fader(dev, attr, buf, len, nr);	\
}									\
static LP55XX_DEV_ATTR_RW(master_fader##nr, show_master_fader##nr,	\
			  store_master_fader##nr)

struct lp55xx_led;
struct lp55xx_chip;

/*
 * struct lp55xx_reg
 * @addr : Register address
 * @val  : Register value (can also used as mask or shift)
 */
struct lp55xx_reg {
	u8 addr;
	union {
		u8 val;
		u8 mask;
		u8 shift;
	};
};

/*
 * struct lp55xx_device_config
 * @reg_op_mode        : Chip specific OP MODE reg addr
 * @engine_busy        : Chip specific engine busy
 *			 (if not supported 153 us sleep)
 * @reset              : Chip specific reset command
 * @enable             : Chip specific enable command
 * @prog_mem_base      : Chip specific base reg address for chip SMEM programming
 * @reg_led_pwm_base   : Chip specific base reg address for LED PWM conf
 * @reg_led_current_base : Chip specific base reg address for LED current conf
 * @reg_master_fader_base : Chip specific base reg address for master fader base
 * @reg_led_ctrl_base  : Chip specific base reg address for LED ctrl base
 * @pages_per_engine   : Assigned pages for each engine
 *                       (if not set chip doesn't support pages)
 * @max_channel        : Maximum number of channels
 * @post_init_device   : Chip specific initialization code
 * @brightness_fn      : Brightness function
 * @multicolor_brightness_fn : Multicolor brightness function
 * @set_led_current    : LED current set function
 * @firmware_cb        : Call function when the firmware is loaded
 * @run_engine         : Run internal engine for pattern
 * @dev_attr_group     : Device specific attributes
 */
struct lp55xx_device_config {
	const struct lp55xx_reg reg_op_mode; /* addr, shift */
	const struct lp55xx_reg reg_exec; /* addr, shift */
	const struct lp55xx_reg engine_busy; /* addr, mask */
	const struct lp55xx_reg reset;
	const struct lp55xx_reg enable;
	const struct lp55xx_reg prog_mem_base;
	const struct lp55xx_reg reg_led_pwm_base;
	const struct lp55xx_reg reg_led_current_base;
	const struct lp55xx_reg reg_master_fader_base;
	const struct lp55xx_reg reg_led_ctrl_base;
	const int pages_per_engine;
	const int max_channel;

	/* define if the device has specific initialization process */
	int (*post_init_device) (struct lp55xx_chip *chip);

	/* set LED brightness */
	int (*brightness_fn)(struct lp55xx_led *led);

	/* set multicolor LED brightness */
	int (*multicolor_brightness_fn)(struct lp55xx_led *led);

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
	struct lp55xx_platform_data *pdata;
	struct mutex lock;	/* lock for user-space interface */
	int num_leds;
	const struct lp55xx_device_config *cfg;
	enum lp55xx_engine_index engine_idx;
	struct lp55xx_engine engines[LP55XX_ENGINE_MAX];
	const struct firmware *fw;
};

/*
 * struct lp55xx_led
 * @chan_nr         : Channel number
 * @cdev            : LED class device
 * @mc_cdev         : Multi color class device
 * @color_components: Multi color LED map information
 * @led_current     : Current setting at each led channel
 * @max_current     : Maximun current at each led channel
 * @brightness      : Brightness value
 * @chip            : The lp55xx chip data
 */
struct lp55xx_led {
	int chan_nr;
	struct led_classdev cdev;
	struct led_classdev_mc mc_cdev;
	u8 led_current;
	u8 max_current;
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

/* common chip functions */
extern void lp55xx_stop_all_engine(struct lp55xx_chip *chip);
extern void lp55xx_load_engine(struct lp55xx_chip *chip);
extern int lp55xx_run_engine_common(struct lp55xx_chip *chip);
extern int lp55xx_update_program_memory(struct lp55xx_chip *chip,
					const u8 *data, size_t size);
extern void lp55xx_firmware_loaded_cb(struct lp55xx_chip *chip);
extern int lp55xx_led_brightness(struct lp55xx_led *led);
extern int lp55xx_multicolor_brightness(struct lp55xx_led *led);
extern void lp55xx_set_led_current(struct lp55xx_led *led, u8 led_current);
extern void lp55xx_turn_off_channels(struct lp55xx_chip *chip);
extern void lp55xx_stop_engine(struct lp55xx_chip *chip);

/* common probe/remove function */
extern int lp55xx_probe(struct i2c_client *client);
extern void lp55xx_remove(struct i2c_client *client);

/* common sysfs function */
extern ssize_t lp55xx_show_engine_mode(struct device *dev,
				       struct device_attribute *attr,
				       char *buf, int nr);
extern ssize_t lp55xx_store_engine_mode(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len, int nr);
extern ssize_t lp55xx_store_engine_load(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len, int nr);
extern ssize_t lp55xx_show_engine_leds(struct device *dev,
				       struct device_attribute *attr,
				       char *buf, int nr);
extern ssize_t lp55xx_store_engine_leds(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len, int nr);
extern ssize_t lp55xx_show_master_fader(struct device *dev,
					struct device_attribute *attr,
					char *buf, int nr);
extern ssize_t lp55xx_store_master_fader(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len, int nr);
extern ssize_t lp55xx_show_master_fader_leds(struct device *dev,
					     struct device_attribute *attr,
					     char *buf);
extern ssize_t lp55xx_store_master_fader_leds(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t len);

#endif /* _LEDS_LP55XX_COMMON_H */
