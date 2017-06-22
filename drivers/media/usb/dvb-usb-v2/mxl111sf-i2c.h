/*
 *  mxl111sf-i2c.h - driver for the MaxLinear MXL111SF
 *
 *  Copyright (C) 2010-2014 Michael Krufky <mkrufky@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef _DVB_USB_MXL111SF_I2C_H_
#define _DVB_USB_MXL111SF_I2C_H_

#include <linux/i2c.h>

int mxl111sf_i2c_xfer(struct i2c_adapter *adap,
		      struct i2c_msg msg[], int num);

#endif /* _DVB_USB_MXL111SF_I2C_H_ */
