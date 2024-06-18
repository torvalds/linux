/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C - I2C Controller core functions
 */

#ifndef __ASM_ARCH_IIC_CORE_H
#define __ASM_ARCH_IIC_CORE_H __FILE__

/* These functions are only for use with the core support code, such as
 * the cpu specific initialisation code
 */

/* re-define device name depending on support. */
static inline void s3c_i2c0_setname(char *name)
{
	/* currently this device is always compiled in */
	s3c_device_i2c0.name = name;
}

static inline void s3c_i2c1_setname(char *name)
{
#ifdef CONFIG_S3C_DEV_I2C1
	s3c_device_i2c1.name = name;
#endif
}

#endif /* __ASM_ARCH_IIC_H */
