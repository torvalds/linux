/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MMIO/IRQ and platform data for SH7760 I2C channels
 */

#ifndef _I2C_SH7760_H_
#define _I2C_SH7760_H_

#define SH7760_I2C_DEVNAME	"sh7760-i2c"

#define SH7760_I2C0_MMIO	0xFE140000
#define SH7760_I2C0_MMIOEND	0xFE14003B

#define SH7760_I2C1_MMIO	0xFE150000
#define SH7760_I2C1_MMIOEND	0xFE15003B

struct sh7760_i2c_platdata {
	unsigned int speed_khz;
};

#endif
