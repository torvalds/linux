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

#ifndef VCHIQ_IF_H
#define VCHIQ_IF_H

#include "interface/vchi/vchi_mh.h"

#define VCHIQ_SERVICE_HANDLE_INVALID 0

#define VCHIQ_SLOT_SIZE     4096
#define VCHIQ_MAX_MSG_SIZE  (VCHIQ_SLOT_SIZE - sizeof(VCHIQ_HEADER_T))
#define VCHIQ_CHANNEL_SIZE  VCHIQ_MAX_MSG_SIZE /* For backwards compatibility */

#define VCHIQ_MAKE_FOURCC(x0, x1, x2, x3) \
			(((x0) << 24) | ((x1) << 16) | ((x2) << 8) | (x3))
#define VCHIQ_GET_SERVICE_USERDATA(service) vchiq_get_service_userdata(service)
#define VCHIQ_GET_SERVICE_FOURCC(service)   vchiq_get_service_fourcc(service)

typedef enum {
	VCHIQ_SERVICE_OPENED,         /* service, -, -             */
	VCHIQ_SERVICE_CLOSED,         /* service, -, -             */
	VCHIQ_MESSAGE_AVAILABLE,      /* service, header, -        */
	VCHIQ_BULK_TRANSMIT_DONE,     /* service, -, bulk_userdata */
	VCHIQ_BULK_RECEIVE_DONE,      /* service, -, bulk_userdata */
	VCHIQ_BULK_TRANSMIT_ABORTED,  /* service, -, bulk_userdata */
	VCHIQ_BULK_RECEIVE_ABORTED    /* service, -, bulk_userdata */
} VCHIQ_REASON_T;

typedef enum {
	VCHIQ_ERROR   = -1,
	VCHIQ_SUCCESS = 0,
	VCHIQ_RETRY   = 1
} VCHIQ_STATUS_T;

typedef enum {
	VCHIQ_BULK_MODE_CALLBACK,
	VCHIQ_BULK_MODE_BLOCKING,
	VCHIQ_BULK_MODE_NOCALLBACK,
	VCHIQ_BULK_MODE_WAITING		/* Reserved for internal use */
} VCHIQ_BULK_MODE_T;

typedef enum {
	VCHIQ_SERVICE_OPTION_AUTOCLOSE,
	VCHIQ_SERVICE_OPTION_SLOT_QUOTA,
	VCHIQ_SERVICE_OPTION_MESSAGE_QUOTA,
	VCHIQ_SERVICE_OPTION_SYNCHRONOUS,
	VCHIQ_SERVICE_OPTION_TRACE
} VCHIQ_SERVICE_OPTION_T;

typedef struct vchiq_header_struct {
	/* The message identifier - opaque to applications. */
	int msgid;

	/* Size of message data. */
	unsigned int size;

	char data[0];           /* message */
} VCHIQ_HEADER_T;

struct vchiq_element {
	const void *data;
	unsigned int size;
};

typedef unsigned int VCHIQ_SERVICE_HANDLE_T;

typedef VCHIQ_STATUS_T (*VCHIQ_CALLBACK_T)(VCHIQ_REASON_T, VCHIQ_HEADER_T *,
	VCHIQ_SERVICE_HANDLE_T, void *);

typedef struct vchiq_service_base_struct {
	int fourcc;
	VCHIQ_CALLBACK_T callback;
	void *userdata;
} VCHIQ_SERVICE_BASE_T;

typedef struct vchiq_service_params_struct {
	int fourcc;
	VCHIQ_CALLBACK_T callback;
	void *userdata;
	short version;       /* Increment for non-trivial changes */
	short version_min;   /* Update for incompatible changes */
} VCHIQ_SERVICE_PARAMS_T;

typedef struct vchiq_config_struct {
	unsigned int max_msg_size;
	unsigned int bulk_threshold; /* The message size above which it
					is better to use a bulk transfer
					(<= max_msg_size) */
	unsigned int max_outstanding_bulks;
	unsigned int max_services;
	short version;      /* The version of VCHIQ */
	short version_min;  /* The minimum compatible version of VCHIQ */
} VCHIQ_CONFIG_T;

typedef struct vchiq_instance_struct *VCHIQ_INSTANCE_T;
typedef void (*VCHIQ_REMOTE_USE_CALLBACK_T)(void *cb_arg);

extern VCHIQ_STATUS_T vchiq_initialise(VCHIQ_INSTANCE_T *pinstance);
extern VCHIQ_STATUS_T vchiq_shutdown(VCHIQ_INSTANCE_T instance);
extern VCHIQ_STATUS_T vchiq_connect(VCHIQ_INSTANCE_T instance);
extern VCHIQ_STATUS_T vchiq_add_service(VCHIQ_INSTANCE_T instance,
	const VCHIQ_SERVICE_PARAMS_T *params,
	VCHIQ_SERVICE_HANDLE_T *pservice);
extern VCHIQ_STATUS_T vchiq_open_service(VCHIQ_INSTANCE_T instance,
	const VCHIQ_SERVICE_PARAMS_T *params,
	VCHIQ_SERVICE_HANDLE_T *pservice);
extern VCHIQ_STATUS_T vchiq_close_service(VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T vchiq_remove_service(VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T vchiq_use_service(VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T vchiq_use_service_no_resume(
	VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T vchiq_release_service(VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T
vchiq_queue_message(VCHIQ_SERVICE_HANDLE_T handle,
		    ssize_t (*copy_callback)(void *context, void *dest,
					     size_t offset, size_t maxsize),
		    void *context,
		    size_t size);
extern void           vchiq_release_message(VCHIQ_SERVICE_HANDLE_T service,
	VCHIQ_HEADER_T *header);
extern VCHIQ_STATUS_T vchiq_queue_bulk_transmit(VCHIQ_SERVICE_HANDLE_T service,
	const void *data, unsigned int size, void *userdata);
extern VCHIQ_STATUS_T vchiq_queue_bulk_receive(VCHIQ_SERVICE_HANDLE_T service,
	void *data, unsigned int size, void *userdata);
extern VCHIQ_STATUS_T vchiq_queue_bulk_transmit_handle(
	VCHIQ_SERVICE_HANDLE_T service, VCHI_MEM_HANDLE_T handle,
	const void *offset, unsigned int size, void *userdata);
extern VCHIQ_STATUS_T vchiq_queue_bulk_receive_handle(
	VCHIQ_SERVICE_HANDLE_T service, VCHI_MEM_HANDLE_T handle,
	void *offset, unsigned int size, void *userdata);
extern VCHIQ_STATUS_T vchiq_bulk_transmit(VCHIQ_SERVICE_HANDLE_T service,
	const void *data, unsigned int size, void *userdata,
	VCHIQ_BULK_MODE_T mode);
extern VCHIQ_STATUS_T vchiq_bulk_receive(VCHIQ_SERVICE_HANDLE_T service,
	void *data, unsigned int size, void *userdata,
	VCHIQ_BULK_MODE_T mode);
extern VCHIQ_STATUS_T vchiq_bulk_transmit_handle(VCHIQ_SERVICE_HANDLE_T service,
	VCHI_MEM_HANDLE_T handle, const void *offset, unsigned int size,
	void *userdata,	VCHIQ_BULK_MODE_T mode);
extern VCHIQ_STATUS_T vchiq_bulk_receive_handle(VCHIQ_SERVICE_HANDLE_T service,
	VCHI_MEM_HANDLE_T handle, void *offset, unsigned int size,
	void *userdata, VCHIQ_BULK_MODE_T mode);
extern int   vchiq_get_client_id(VCHIQ_SERVICE_HANDLE_T service);
extern void *vchiq_get_service_userdata(VCHIQ_SERVICE_HANDLE_T service);
extern int   vchiq_get_service_fourcc(VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T vchiq_get_config(VCHIQ_INSTANCE_T instance,
	int config_size, VCHIQ_CONFIG_T *pconfig);
extern VCHIQ_STATUS_T vchiq_set_service_option(VCHIQ_SERVICE_HANDLE_T service,
	VCHIQ_SERVICE_OPTION_T option, int value);

extern VCHIQ_STATUS_T vchiq_remote_use(VCHIQ_INSTANCE_T instance,
	VCHIQ_REMOTE_USE_CALLBACK_T callback, void *cb_arg);
extern VCHIQ_STATUS_T vchiq_remote_release(VCHIQ_INSTANCE_T instance);

extern VCHIQ_STATUS_T vchiq_dump_phys_mem(VCHIQ_SERVICE_HANDLE_T service,
	void *ptr, size_t num_bytes);

extern VCHIQ_STATUS_T vchiq_get_peer_version(VCHIQ_SERVICE_HANDLE_T handle,
      short *peer_version);

#endif /* VCHIQ_IF_H */
