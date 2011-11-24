/* include/linux/isl29028.h
 *
 * Copyright (C) 2009 Google, Inc.
 * Author: Iliyan Malchev <malchev@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_AL3006_H
#define __LINUX_AL3006_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define PSENSOR_IOCTL_MAGIC 'c'
#define PSENSOR_IOCTL_GET_ENABLED \
		_IOR(PSENSOR_IOCTL_MAGIC, 1, int *)
#define PSENSOR_IOCTL_ENABLE \
		_IOW(PSENSOR_IOCTL_MAGIC, 2, int *)

#define LIGHTSENSOR_IOCTL_MAGIC 'l'
#define LIGHTSENSOR_IOCTL_GET_ENABLED _IOR(LIGHTSENSOR_IOCTL_MAGIC, 1, int *)
#define LIGHTSENSOR_IOCTL_ENABLE _IOW(LIGHTSENSOR_IOCTL_MAGIC, 2, int *)

#endif
