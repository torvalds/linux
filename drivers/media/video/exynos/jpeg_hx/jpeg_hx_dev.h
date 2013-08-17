/* linux/drivers/media/video/exynos/jpeg_hx/jpeg_hx_dev.h
  *
  * Copyright (c) 2012 Samsung Electronics Co., Ltd.
  * http://www.samsung.com/
  *
  * Header file for Samsung Jpeg hx Interface driver
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation.
 */

#ifndef __JPEG_DEV_H__
#define __JPEG_DEV_H__

#define JPEG_HX_NAME		"exynos5-jpeg-hx"
#define JPEG_HX_ENC_NAME		"video14"
#define JPEG_HX_DEC_NAME		"video13"

#if defined(CONFIG_BUSFREQ_OPP)
#define BUSFREQ_400MHZ	400266
#endif

#endif /*__JPEG_DEV_H__*/
