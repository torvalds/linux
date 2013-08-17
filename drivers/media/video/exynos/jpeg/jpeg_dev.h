/* linux/drivers/media/video/exynos/jpeg/jpeg_dev.h
  *
  * Copyright (c) 2012 Samsung Electronics Co., Ltd.
  * http://www.samsung.com/
  *
  * Header file for Samsung Jpeg v2.x Interface driver
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation.
 */

#ifndef __JPEG_DEV_H__
#define __JPEG_DEV_H__

#define JPEG_NAME		"s5p-jpeg"
#define JPEG_ENC_NAME		"video12"
#define JPEG_DEC_NAME		"video11"

#if defined(CONFIG_BUSFREQ_OPP)
#define BUSFREQ_400MHZ	400266
#endif

#endif /*__JPEG_DEV_H__*/
