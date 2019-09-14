/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHIQ_IF_H
#define VCHIQ_IF_H

#define VCHIQ_SERVICE_HANDLE_INVALID 0

#define VCHIQ_SLOT_SIZE     4096
#define VCHIQ_MAX_MSG_SIZE  (VCHIQ_SLOT_SIZE - sizeof(struct vchiq_header))
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

struct vchiq_header {
	/* The message identifier - opaque to applications. */
	int msgid;

	/* Size of message data. */
	unsigned int size;

	char data[0];           /* message */
};

struct vchiq_element {
	const void __user *data;
	unsigned int size;
};

typedef unsigned int VCHIQ_SERVICE_HANDLE_T;

typedef VCHIQ_STATUS_T (*VCHIQ_CALLBACK_T)(VCHIQ_REASON_T,
					   struct vchiq_header *,
					   VCHIQ_SERVICE_HANDLE_T, void *);

struct vchiq_service_base {
	int fourcc;
	VCHIQ_CALLBACK_T callback;
	void *userdata;
};

struct vchiq_service_params {
	int fourcc;
	VCHIQ_CALLBACK_T callback;
	void *userdata;
	short version;       /* Increment for non-trivial changes */
	short version_min;   /* Update for incompatible changes */
};

struct vchiq_config {
	unsigned int max_msg_size;
	unsigned int bulk_threshold; /* The message size above which it
					is better to use a bulk transfer
					(<= max_msg_size) */
	unsigned int max_outstanding_bulks;
	unsigned int max_services;
	short version;      /* The version of VCHIQ */
	short version_min;  /* The minimum compatible version of VCHIQ */
};

typedef struct vchiq_instance_struct *VCHIQ_INSTANCE_T;
typedef void (*VCHIQ_REMOTE_USE_CALLBACK_T)(void *cb_arg);

extern VCHIQ_STATUS_T vchiq_initialise(VCHIQ_INSTANCE_T *pinstance);
extern VCHIQ_STATUS_T vchiq_shutdown(VCHIQ_INSTANCE_T instance);
extern VCHIQ_STATUS_T vchiq_connect(VCHIQ_INSTANCE_T instance);
extern VCHIQ_STATUS_T vchiq_add_service(VCHIQ_INSTANCE_T instance,
	const struct vchiq_service_params *params,
	VCHIQ_SERVICE_HANDLE_T *pservice);
extern VCHIQ_STATUS_T vchiq_open_service(VCHIQ_INSTANCE_T instance,
	const struct vchiq_service_params *params,
	VCHIQ_SERVICE_HANDLE_T *pservice);
extern VCHIQ_STATUS_T vchiq_close_service(VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T vchiq_remove_service(VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T vchiq_use_service(VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T vchiq_release_service(VCHIQ_SERVICE_HANDLE_T service);
extern VCHIQ_STATUS_T
vchiq_queue_message(VCHIQ_SERVICE_HANDLE_T handle,
		    ssize_t (*copy_callback)(void *context, void *dest,
					     size_t offset, size_t maxsize),
		    void *context,
		    size_t size);
extern void           vchiq_release_message(VCHIQ_SERVICE_HANDLE_T service,
	struct vchiq_header *header);
extern VCHIQ_STATUS_T vchiq_bulk_transmit(VCHIQ_SERVICE_HANDLE_T service,
	const void *data, unsigned int size, void *userdata,
	VCHIQ_BULK_MODE_T mode);
extern VCHIQ_STATUS_T vchiq_bulk_receive(VCHIQ_SERVICE_HANDLE_T service,
	void *data, unsigned int size, void *userdata,
	VCHIQ_BULK_MODE_T mode);
extern VCHIQ_STATUS_T vchiq_bulk_transmit_handle(VCHIQ_SERVICE_HANDLE_T service,
	const void *offset, unsigned int size,
	void *userdata,	VCHIQ_BULK_MODE_T mode);
extern VCHIQ_STATUS_T vchiq_bulk_receive_handle(VCHIQ_SERVICE_HANDLE_T service,
	void *offset, unsigned int size, void *userdata,
	VCHIQ_BULK_MODE_T mode);
extern int   vchiq_get_client_id(VCHIQ_SERVICE_HANDLE_T service);
extern void *vchiq_get_service_userdata(VCHIQ_SERVICE_HANDLE_T service);
extern int   vchiq_get_service_fourcc(VCHIQ_SERVICE_HANDLE_T service);
extern void vchiq_get_config(struct vchiq_config *config);
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
