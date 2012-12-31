/* driver/input/touchscreen/s5pc210_ts_gpio_i2c.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., LTD.
 *	http://www.samsung.com
 *
 * S5PC210 10.1" Touchscreen gpio i2c information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef	_S5PV310_TS_GPIO_I2C_H_
#define	_S5PV310_TS_GPIO_I2C_H_

extern int s5pv310_ts_write(unsigned char addr, unsigned char *wdata,
				unsigned char wsize);
extern int s5pv310_ts_read(unsigned char *rdata, unsigned char rsize);
extern void s5pv310_ts_port_init(void);

#endif	/*_S5PV310_TS_GPIO_I2C_H_*/
