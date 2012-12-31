/* linux/drivers/media/video/samsung/tvout/s5p_tvout_v4l2.h
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Video4Linux API header file. file for Samsung TVOut driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _S5P_TVOUT_V4L2_H_
#define _S5P_TVOUT_V4L2_H_

extern int s5p_tvout_v4l2_constructor(struct platform_device *pdev);
extern void s5p_tvout_v4l2_destructor(void);

#endif /* _LINUX_S5P_TVOUT_V4L2_H_ */
