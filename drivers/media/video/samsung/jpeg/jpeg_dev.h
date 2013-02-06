/* linux/drivers/media/video/samsung/jpeg/jpeg_dev.h
  *
  * Copyright (c) 2010 Samsung Electronics Co., Ltd.
  * http://www.samsung.com/
  *
  * Header file for Samsung Jpeg Interface driver
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation.
 */


#ifndef __JPEG_DEV_H__
#define __JPEG_DEV_H__


#define JPEG_MINOR_NUMBER	254
#define JPEG_NAME		"s5p-jpeg"
#define JPEG_MAX_INSTANCE	1

#define JPEG_IOCTL_MAGIC 'J'

#define IOCTL_JPEG_DEC_EXE			_IO(JPEG_IOCTL_MAGIC, 1)
#define IOCTL_JPEG_ENC_EXE			_IO(JPEG_IOCTL_MAGIC, 2)
#define IOCTL_GET_DEC_IN_BUF			_IO(JPEG_IOCTL_MAGIC, 3)
#define IOCTL_GET_DEC_OUT_BUF			_IO(JPEG_IOCTL_MAGIC, 4)
#define IOCTL_GET_ENC_IN_BUF			_IO(JPEG_IOCTL_MAGIC, 5)
#define IOCTL_GET_ENC_OUT_BUF			_IO(JPEG_IOCTL_MAGIC, 6)
#define IOCTL_SET_DEC_PARAM			_IO(JPEG_IOCTL_MAGIC, 7)
#define IOCTL_SET_ENC_PARAM			_IO(JPEG_IOCTL_MAGIC, 8)
#define IOCTL_GET_PHYADDR			_IO(JPEG_IOCTL_MAGIC, 9)
#define IOCTL_GET_PHYMEM_BASE		_IOR(JPEG_IOCTL_MAGIC, 10, unsigned int)
#define IOCTL_GET_PHYMEM_SIZE		_IOR(JPEG_IOCTL_MAGIC, 11, unsigned int)
#endif /*__JPEG_DEV_H__*/

