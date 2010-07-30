/*
 * arch/arm/mach-tegra/include/linux/nvfw_ioctl.h
 *
 * structure declarations for nvfw ioctls
 *
 * Copyright (c) 2009, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/ioctl.h>

#if !defined(__KERNEL__)
#define __user
#endif

#ifndef _MACH_TEGRA_NVFW_IOCTL_H_
#define _MACH_TEGRA_NVFW_IOCTL_H_

struct nvfw_load_handle {
	const char *filename;
	int length;
	void *args;
	int argssize;
	int greedy;
	void *handle;
};

struct nvfw_get_proc_address_handle {
	const char *symbolname;
	int length;
	void *address;
	void *handle;
};

#define NVFW_IOC_MAGIC 'N'
#define NVFW_IOC_LOAD_LIBRARY     _IOWR(NVFW_IOC_MAGIC, 0x50, struct nvfw_load_handle)
#define NVFW_IOC_LOAD_LIBRARY_EX  _IOWR(NVFW_IOC_MAGIC, 0x51, struct nvfw_load_handle)
#define NVFW_IOC_FREE_LIBRARY     _IOW (NVFW_IOC_MAGIC, 0x52, struct nvfw_load_handle)
#define NVFW_IOC_GET_PROC_ADDRESS _IOWR(NVFW_IOC_MAGIC, 0x53, struct nvfw_load_handle)

#endif
