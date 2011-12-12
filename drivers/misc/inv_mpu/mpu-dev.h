/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */


#ifndef __MPU_DEV_H__
#define __MPU_DEV_H__

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mpu.h>

int inv_mpu_register_slave(struct module *slave_module,
			struct i2c_client *client,
			struct ext_slave_platform_data *pdata,
			struct ext_slave_descr *(*slave_descr)(void));

void inv_mpu_unregister_slave(struct i2c_client *client,
			struct ext_slave_platform_data *pdata,
			struct ext_slave_descr *(*slave_descr)(void));
#endif
