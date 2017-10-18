/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef VCHIQ_IOCTLS_H
#define VCHIQ_IOCTLS_H

#include <linux/ioctl.h>
#include "vchiq_if.h"

#define VCHIQ_IOC_MAGIC 0xc4
#define VCHIQ_INVALID_HANDLE (~0)

typedef struct {
	VCHIQ_SERVICE_PARAMS_T params;
	int is_open;
	int is_vchi;
	unsigned int handle;       /* OUT */
} VCHIQ_CREATE_SERVICE_T;

typedef struct {
	unsigned int handle;
	unsigned int count;
	const struct vchiq_element *elements;
} VCHIQ_QUEUE_MESSAGE_T;

typedef struct {
	unsigned int handle;
	void *data;
	unsigned int size;
	void *userdata;
	VCHIQ_BULK_MODE_T mode;
} VCHIQ_QUEUE_BULK_TRANSFER_T;

typedef struct {
	VCHIQ_REASON_T reason;
	VCHIQ_HEADER_T *header;
	void *service_userdata;
	void *bulk_userdata;
} VCHIQ_COMPLETION_DATA_T;

typedef struct {
	unsigned int count;
	VCHIQ_COMPLETION_DATA_T *buf;
	unsigned int msgbufsize;
	unsigned int msgbufcount; /* IN/OUT */
	void **msgbufs;
} VCHIQ_AWAIT_COMPLETION_T;

typedef struct {
	unsigned int handle;
	int blocking;
	unsigned int bufsize;
	void *buf;
} VCHIQ_DEQUEUE_MESSAGE_T;

typedef struct {
	unsigned int config_size;
	VCHIQ_CONFIG_T *pconfig;
} VCHIQ_GET_CONFIG_T;

typedef struct {
	unsigned int handle;
	VCHIQ_SERVICE_OPTION_T option;
	int value;
} VCHIQ_SET_SERVICE_OPTION_T;

typedef struct {
	void     *virt_addr;
	size_t    num_bytes;
} VCHIQ_DUMP_MEM_T;

#define VCHIQ_IOC_CONNECT              _IO(VCHIQ_IOC_MAGIC,   0)
#define VCHIQ_IOC_SHUTDOWN             _IO(VCHIQ_IOC_MAGIC,   1)
#define VCHIQ_IOC_CREATE_SERVICE \
	_IOWR(VCHIQ_IOC_MAGIC, 2, VCHIQ_CREATE_SERVICE_T)
#define VCHIQ_IOC_REMOVE_SERVICE       _IO(VCHIQ_IOC_MAGIC,   3)
#define VCHIQ_IOC_QUEUE_MESSAGE \
	_IOW(VCHIQ_IOC_MAGIC,  4, VCHIQ_QUEUE_MESSAGE_T)
#define VCHIQ_IOC_QUEUE_BULK_TRANSMIT \
	_IOWR(VCHIQ_IOC_MAGIC, 5, VCHIQ_QUEUE_BULK_TRANSFER_T)
#define VCHIQ_IOC_QUEUE_BULK_RECEIVE \
	_IOWR(VCHIQ_IOC_MAGIC, 6, VCHIQ_QUEUE_BULK_TRANSFER_T)
#define VCHIQ_IOC_AWAIT_COMPLETION \
	_IOWR(VCHIQ_IOC_MAGIC, 7, VCHIQ_AWAIT_COMPLETION_T)
#define VCHIQ_IOC_DEQUEUE_MESSAGE \
	_IOWR(VCHIQ_IOC_MAGIC, 8, VCHIQ_DEQUEUE_MESSAGE_T)
#define VCHIQ_IOC_GET_CLIENT_ID        _IO(VCHIQ_IOC_MAGIC,   9)
#define VCHIQ_IOC_GET_CONFIG \
	_IOWR(VCHIQ_IOC_MAGIC, 10, VCHIQ_GET_CONFIG_T)
#define VCHIQ_IOC_CLOSE_SERVICE        _IO(VCHIQ_IOC_MAGIC,   11)
#define VCHIQ_IOC_USE_SERVICE          _IO(VCHIQ_IOC_MAGIC,   12)
#define VCHIQ_IOC_RELEASE_SERVICE      _IO(VCHIQ_IOC_MAGIC,   13)
#define VCHIQ_IOC_SET_SERVICE_OPTION \
	_IOW(VCHIQ_IOC_MAGIC,  14, VCHIQ_SET_SERVICE_OPTION_T)
#define VCHIQ_IOC_DUMP_PHYS_MEM \
	_IOW(VCHIQ_IOC_MAGIC,  15, VCHIQ_DUMP_MEM_T)
#define VCHIQ_IOC_LIB_VERSION          _IO(VCHIQ_IOC_MAGIC,   16)
#define VCHIQ_IOC_CLOSE_DELIVERED      _IO(VCHIQ_IOC_MAGIC,   17)
#define VCHIQ_IOC_MAX                  17

#endif
