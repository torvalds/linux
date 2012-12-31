/* driver/input/touchscreen/s5pc210_ts_sysfs.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., LTD.
 *	http://www.samsung.com
 *
 * S5PC210 10.1" Touchscreen sysfs information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef	_S5PV310_TS_SYSFS_H_
#define	_S5PV310_TS_SYSFS_H_

extern int s5pv310_ts_sysfs_create(struct platform_device *pdev);
extern void s5pv310_ts_sysfs_remove(struct platform_device *pdev);

#endif	/* _S5PV310_TS_SYSFS_H_ */
