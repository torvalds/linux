/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHIQ_IOCTLS_H
#define VCHIQ_IOCTLS_H

#include <linux/ioctl.h>

#include "../../include/linux/raspberrypi/vchiq.h"

#define VCHIQ_IOC_MAGIC 0xc4
#define VCHIQ_INVALID_HANDLE (~0)

struct vchiq_service_params {
	int fourcc;
	int __user (*callback)(enum vchiq_reason reason,
			       struct vchiq_header *header,
			       unsigned int handle,
			       void *bulk_userdata);
	void __user *userdata;
	short version;       /* Increment for non-trivial changes */
	short version_min;   /* Update for incompatible changes */
};

struct vchiq_create_service {
	struct vchiq_service_params params;
	int is_open;
	int is_vchi;
	unsigned int handle;       /* OUT */
};

struct vchiq_queue_message {
	unsigned int handle;
	unsigned int count;
	const struct vchiq_element __user *elements;
};

struct vchiq_queue_bulk_transfer {
	unsigned int handle;
	void __user *data;
	unsigned int size;
	void __user *userdata;
	enum vchiq_bulk_mode mode;
};

struct vchiq_completion_data {
	enum vchiq_reason reason;
	struct vchiq_header __user *header;
	void __user *service_userdata;
	void __user *cb_userdata;
};

struct vchiq_await_completion {
	unsigned int count;
	struct vchiq_completion_data __user *buf;
	unsigned int msgbufsize;
	unsigned int msgbufcount; /* IN/OUT */
	void * __user *msgbufs;
};

struct vchiq_dequeue_message {
	unsigned int handle;
	int blocking;
	unsigned int bufsize;
	void __user *buf;
};

struct vchiq_get_config {
	unsigned int config_size;
	struct vchiq_config __user *pconfig;
};

struct vchiq_set_service_option {
	unsigned int handle;
	enum vchiq_service_option option;
	int value;
};

struct vchiq_dump_mem {
	void     __user *virt_addr;
	size_t    num_bytes;
};

#define VCHIQ_IOC_CONNECT              _IO(VCHIQ_IOC_MAGIC,   0)
#define VCHIQ_IOC_SHUTDOWN             _IO(VCHIQ_IOC_MAGIC,   1)
#define VCHIQ_IOC_CREATE_SERVICE \
	_IOWR(VCHIQ_IOC_MAGIC, 2, struct vchiq_create_service)
#define VCHIQ_IOC_REMOVE_SERVICE       _IO(VCHIQ_IOC_MAGIC,   3)
#define VCHIQ_IOC_QUEUE_MESSAGE \
	_IOW(VCHIQ_IOC_MAGIC,  4, struct vchiq_queue_message)
#define VCHIQ_IOC_QUEUE_BULK_TRANSMIT \
	_IOWR(VCHIQ_IOC_MAGIC, 5, struct vchiq_queue_bulk_transfer)
#define VCHIQ_IOC_QUEUE_BULK_RECEIVE \
	_IOWR(VCHIQ_IOC_MAGIC, 6, struct vchiq_queue_bulk_transfer)
#define VCHIQ_IOC_AWAIT_COMPLETION \
	_IOWR(VCHIQ_IOC_MAGIC, 7, struct vchiq_await_completion)
#define VCHIQ_IOC_DEQUEUE_MESSAGE \
	_IOWR(VCHIQ_IOC_MAGIC, 8, struct vchiq_dequeue_message)
#define VCHIQ_IOC_GET_CLIENT_ID        _IO(VCHIQ_IOC_MAGIC,   9)
#define VCHIQ_IOC_GET_CONFIG \
	_IOWR(VCHIQ_IOC_MAGIC, 10, struct vchiq_get_config)
#define VCHIQ_IOC_CLOSE_SERVICE        _IO(VCHIQ_IOC_MAGIC,   11)
#define VCHIQ_IOC_USE_SERVICE          _IO(VCHIQ_IOC_MAGIC,   12)
#define VCHIQ_IOC_RELEASE_SERVICE      _IO(VCHIQ_IOC_MAGIC,   13)
#define VCHIQ_IOC_SET_SERVICE_OPTION \
	_IOW(VCHIQ_IOC_MAGIC,  14, struct vchiq_set_service_option)
#define VCHIQ_IOC_DUMP_PHYS_MEM \
	_IOW(VCHIQ_IOC_MAGIC,  15, struct vchiq_dump_mem)
#define VCHIQ_IOC_LIB_VERSION          _IO(VCHIQ_IOC_MAGIC,   16)
#define VCHIQ_IOC_CLOSE_DELIVERED      _IO(VCHIQ_IOC_MAGIC,   17)
#define VCHIQ_IOC_MAX                  17

#endif
