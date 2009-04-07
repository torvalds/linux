/*
 * i2c.h - i.MX I2C driver header file
 *
 * Copyright (c) 2008, Darius Augulis <augulis.darius@gmail.com>
 *
 * This file is released under the GPLv2
 */

#ifndef __ASM_ARCH_I2C_H_
#define __ASM_ARCH_I2C_H_

/**
 * struct imxi2c_platform_data - structure of platform data for MXC I2C driver
 * @init:	Initialise gpio's and other board specific things
 * @exit:	Free everything initialised by @init
 * @bitrate:	Bus speed measured in Hz
 *
 **/
struct imxi2c_platform_data {
	int (*init)(struct device *dev);
	void (*exit)(struct device *dev);
	int bitrate;
};

#endif /* __ASM_ARCH_I2C_H_ */
