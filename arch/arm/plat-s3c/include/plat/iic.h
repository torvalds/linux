/* arch/arm/plat-s3c/include/plat/iic.h
 *
 * Copyright 2004,2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C - I2C Controller platform_device info
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_IIC_H
#define __ASM_ARCH_IIC_H __FILE__

#define S3C_IICFLG_FILTER	(1<<0)	/* enable s3c2440 filter */

/**
 *	struct s3c2410_platform_i2c - Platform data for s3c I2C.
 *	@bus_num: The bus number to use (if possible).
 *	@flags: Any flags for the I2C bus (E.g. S3C_IICFLK_FILTER).
 *	@slave_addr: The I2C address for the slave device (if enabled).
 *	@frequency: The desired frequency in Hz of the bus.  This is
 *                  guaranteed to not be exceeded.  If the caller does
 *                  not care, use zero and the driver will select a
 *                  useful default.
 *	@sda_delay: The delay (in ns) applied to SDA edges.
 *	@cfg_gpio: A callback to configure the pins for I2C operation.
 */
struct s3c2410_platform_i2c {
	int		bus_num;
	unsigned int	flags;
	unsigned int	slave_addr;
	unsigned long	frequency;
	unsigned int	sda_delay;

	void	(*cfg_gpio)(struct platform_device *dev);
};

/**
 * s3c_i2c0_set_platdata - set platform data for i2c0 device
 * @i2c: The platform data to set, or NULL for default data.
 *
 * Register the given platform data for use with the i2c0 device. This
 * call copies the platform data, so the caller can use __initdata for
 * their copy.
 *
 * This call will set cfg_gpio if is null to the default platform
 * implementation.
 *
 * Any user of s3c_device_i2c0 should call this, even if it is with
 * NULL to ensure that the device is given the default platform data
 * as the driver will no longer carry defaults.
 */
extern void s3c_i2c0_set_platdata(struct s3c2410_platform_i2c *i2c);
extern void s3c_i2c1_set_platdata(struct s3c2410_platform_i2c *i2c);

/* defined by architecture to configure gpio */
extern void s3c_i2c0_cfg_gpio(struct platform_device *dev);
extern void s3c_i2c1_cfg_gpio(struct platform_device *dev);

#endif /* __ASM_ARCH_IIC_H */
