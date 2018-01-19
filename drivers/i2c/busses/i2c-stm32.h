/*
 * i2c-stm32.h
 *
 * Copyright (C) M'boumba Cedric Madianga 2017
 * Author: M'boumba Cedric Madianga <cedric.madianga@gmail.com>
 *
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _I2C_STM32_H
#define _I2C_STM32_H

enum stm32_i2c_speed {
	STM32_I2C_SPEED_STANDARD, /* 100 kHz */
	STM32_I2C_SPEED_FAST, /* 400 kHz */
	STM32_I2C_SPEED_FAST_PLUS, /* 1 MHz */
	STM32_I2C_SPEED_END,
};

#endif /* _I2C_STM32_H */
