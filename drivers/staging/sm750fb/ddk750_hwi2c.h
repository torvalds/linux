/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DDK750_HWI2C_H__
#define DDK750_HWI2C_H__

/* hwi2c functions */
int sm750_hw_i2c_init(unsigned char bus_speed_mode);
void sm750_hw_i2c_close(void);

unsigned char sm750_hw_i2c_read_reg(unsigned char addr, unsigned char reg);
int sm750_hw_i2c_write_reg(unsigned char addr, unsigned char reg,
			   unsigned char data);
#endif
