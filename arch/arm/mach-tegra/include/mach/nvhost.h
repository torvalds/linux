/*
 * include/linux/nvhost.h
 *
 * Tegra graphics host driver
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
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

#ifndef __LINUX_NVHOST_H
#define __LINUX_NVHOST_H

#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/types.h>

struct nvhost_master;

struct nvhost_device {
	const char		*name;
	struct device		dev;
	int			id;
	u32			num_resources;
	struct resource		*resource;

	struct nvhost_master	*host;
};

extern int nvhost_device_register(struct nvhost_device *);
extern void nvhost_device_unregister(struct nvhost_device *);

extern struct bus_type nvhost_bus_type;

struct nvhost_driver {
	int (*probe)(struct nvhost_device *);
	int (*remove)(struct nvhost_device *);
	void (*shutdown)(struct nvhost_device *);
	int (*suspend)(struct nvhost_device *, pm_message_t state);
	int (*resume)(struct nvhost_device *);
	struct device_driver driver;
};

extern int nvhost_driver_register(struct nvhost_driver *);
extern void nvhost_driver_unregister(struct nvhost_driver *);
extern struct resource *nvhost_get_resource(struct nvhost_device *, unsigned int, unsigned int);
extern int nvhost_get_irq(struct nvhost_device *, unsigned int);
extern struct resource *nvhost_get_resource_byname(struct nvhost_device *, unsigned int, const char *);
extern int nvhost_get_irq_byname(struct nvhost_device *, const char *);

#define to_nvhost_device(x) container_of((x), struct nvhost_device, dev)
#define to_nvhost_driver(drv)	(container_of((drv), struct nvhost_driver, \
				 driver))

#define nvhost_get_drvdata(_dev)	dev_get_drvdata(&(_dev)->dev)
#define nvhost_set_drvdata(_dev,data)	dev_set_drvdata(&(_dev)->dev, (data))

int nvhost_bus_register(struct nvhost_master *host);

#if !defined(__KERNEL__)
#define __user
#endif

#define NVHOST_NO_TIMEOUT (-1)
#define NVHOST_IOCTL_MAGIC 'H'

struct nvhost_submit_hdr {
	__u32 syncpt_id;
	__u32 syncpt_incrs;
	__u32 num_cmdbufs;
	__u32 num_relocs;
};

struct nvhost_cmdbuf {
	__u32 mem;
	__u32 offset;
	__u32 words;
};

struct nvhost_reloc {
	__u32 cmdbuf_mem;
	__u32 cmdbuf_offset;
	__u32 target;
	__u32 target_offset;
};

struct nvhost_get_param_args {
	__u32 value;
};

struct nvhost_set_nvmap_fd_args {
	__u32 fd;
};

#define NVHOST_IOCTL_CHANNEL_FLUSH		\
	_IOR(NVHOST_IOCTL_MAGIC, 1, struct nvhost_get_param_args)
#define NVHOST_IOCTL_CHANNEL_GET_SYNCPOINTS	\
	_IOR(NVHOST_IOCTL_MAGIC, 2, struct nvhost_get_param_args)
#define NVHOST_IOCTL_CHANNEL_GET_WAITBASES	\
	_IOR(NVHOST_IOCTL_MAGIC, 3, struct nvhost_get_param_args)
#define NVHOST_IOCTL_CHANNEL_GET_MODMUTEXES	\
	_IOR(NVHOST_IOCTL_MAGIC, 4, struct nvhost_get_param_args)
#define NVHOST_IOCTL_CHANNEL_SET_NVMAP_FD	\
	_IOW(NVHOST_IOCTL_MAGIC, 5, struct nvhost_set_nvmap_fd_args)
#define NVHOST_IOCTL_CHANNEL_LAST		\
	_IOC_NR(NVHOST_IOCTL_CHANNEL_SET_NVMAP_FD)
#define NVHOST_IOCTL_CHANNEL_MAX_ARG_SIZE sizeof(struct nvhost_get_param_args)

struct nvhost_ctrl_syncpt_read_args {
	__u32 id;
	__u32 value;
};

struct nvhost_ctrl_syncpt_incr_args {
	__u32 id;
};

struct nvhost_ctrl_syncpt_wait_args {
	__u32 id;
	__u32 thresh;
	__s32 timeout;
};

struct nvhost_ctrl_module_mutex_args {
	__u32 id;
	__u32 lock;
};

struct nvhost_ctrl_module_regrdwr_args {
	__u32 id;
	__u32 num_offsets;
	__u32 block_size;
	__u32 *offsets;
	__u32 *values;
	__u32 write;
};

#define NVHOST_IOCTL_CTRL_SYNCPT_READ		\
	_IOWR(NVHOST_IOCTL_MAGIC, 1, struct nvhost_ctrl_syncpt_read_args)
#define NVHOST_IOCTL_CTRL_SYNCPT_INCR		\
	_IOW(NVHOST_IOCTL_MAGIC, 2, struct nvhost_ctrl_syncpt_incr_args)
#define NVHOST_IOCTL_CTRL_SYNCPT_WAIT		\
	_IOW(NVHOST_IOCTL_MAGIC, 3, struct nvhost_ctrl_syncpt_wait_args)

#define NVHOST_IOCTL_CTRL_MODULE_MUTEX		\
	_IOWR(NVHOST_IOCTL_MAGIC, 4, struct nvhost_ctrl_module_mutex_args)
#define NVHOST_IOCTL_CTRL_MODULE_REGRDWR	\
	_IOWR(NVHOST_IOCTL_MAGIC, 5, struct nvhost_ctrl_module_regrdwr_args)

#define NVHOST_IOCTL_CTRL_LAST			\
	_IOC_NR(NVHOST_IOCTL_CTRL_MODULE_REGRDWR)
#define NVHOST_IOCTL_CTRL_MAX_ARG_SIZE sizeof(struct nvhost_ctrl_module_regrdwr_args)

#endif
