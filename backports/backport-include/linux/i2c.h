#ifndef __BACKPORT_LINUX_I2C_H
#define __BACKPORT_LINUX_I2C_H
#include_next <linux/i2c.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0) && \
    LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
#include <linux/i2c.h>
/* Unlocked flavor */
#define __i2c_transfer LINUX_BACKPORT(__i2c_transfer)
extern int __i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			  int num);
#endif

/* This backports
 *
 * commit 14674e70119ea01549ce593d8901a797f8a90f74
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 * Date:   Wed May 30 10:55:34 2012 +0200
 *
 *     i2c: Split I2C_M_NOSTART support out of I2C_FUNC_PROTOCOL_MANGLING
 */
#ifndef I2C_FUNC_NOSTART
#define I2C_FUNC_NOSTART 0x00000010 /* I2C_M_NOSTART */
#endif

/* This backports:
 *
 * commit 7c92784a546d2945b6d6973a30f7134be78eb7a4
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 * Date:   Wed Nov 16 10:13:36 2011 +0100
 *
 *     I2C: Add helper macro for i2c_driver boilerplate
 */
#ifndef module_i2c_driver
#define module_i2c_driver(__i2c_driver) \
	module_driver(__i2c_driver, i2c_add_driver, \
			i2c_del_driver)
#endif

#ifndef I2C_CLIENT_SCCB
#define I2C_CLIENT_SCCB	0x9000		/* Use Omnivision SCCB protocol */
					/* Must match I2C_M_STOP|IGNORE_NAK */
#endif

#endif /* __BACKPORT_LINUX_I2C_H */
