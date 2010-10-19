/* linux/arch/arm/mach-s5p64x0/include/mach/i2c.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 I2C configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

extern void s5p6440_i2c0_cfg_gpio(struct platform_device *dev);
extern void s5p6440_i2c1_cfg_gpio(struct platform_device *dev);

extern void s5p6450_i2c0_cfg_gpio(struct platform_device *dev);
extern void s5p6450_i2c1_cfg_gpio(struct platform_device *dev);
