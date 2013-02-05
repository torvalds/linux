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
};

/*
 * struct lp55xx_chip
 * @cl         : I2C communication for access registers
 * @pdata      : Platform specific data
 * @lock       : Lock for user-space interface
 * @num_leds   : Number of registered LEDs
 * @cfg        : Device specific configuration data
 */
struct lp55xx_chip {
	struct i2c_client *cl;
	struct lp55xx_platform_data *pdata;
	struct mutex lock;	/* lock for user-space interface */
	int num_leds;
	struct lp55xx_device_config *cfg;
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

/* common device init/deinit functions */
extern int lp55xx_init_device(struct lp55xx_chip *chip);
extern void lp55xx_deinit_device(struct lp55xx_chip *chip);

/* common LED class device functions */
extern int lp55xx_register_leds(struct lp55xx_led *led,
				struct lp55xx_chip *chip);
#endif /* _LEDS_LP55XX_COMMON_H */
