/*
 * drivers/input/touchscreen/readme.txt
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

touch screen driver 现状描述：
@2011.11.16
	ctp_platform_ops.h: 建立3.0 初始版本；
	1. 新增i2c 相关的detect接口；
	2. 更新set_gpio_irq_mode接口，能实现外部32个中断源的配置；

	1.goodix_touch: 建立3.0 初始版本；
		1.1 支持单点或双点坐标上报方式；
		1.2 采用ctp_platform_ops操作集完成平台相关操作；

	ft5x_ts: 建立3.0 初始版本；
		1. 采用ctp_platform_ops操作集完成平台相关操作；