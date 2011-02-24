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

#ifndef __LINUX_ISL29028_H
#define __LINUX_ISL29028_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define ISL29028_IOCTL_MAGIC 'c'
#define ISL29028_IOCTL_GET_ENABLED \
		_IOR(ISL29028_IOCTL_MAGIC, 1, int *)
#define ISL29028_IOCTL_ENABLE \
		_IOW(ISL29028_IOCTL_MAGIC, 2, int *)

#endif
