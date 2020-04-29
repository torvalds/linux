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

enum vchiq_reason {
	VCHIQ_SERVICE_OPENED,         /* service, -, -             */
	VCHIQ_SERVICE_CLOSED,         /* service, -, -             */
	VCHIQ_MESSAGE_AVAILABLE,      /* service, header, -        */
	VCHIQ_BULK_TRANSMIT_DONE,     /* service, -, bulk_userdata */
	VCHIQ_BULK_RECEIVE_DONE,      /* service, -, bulk_userdata */
	VCHIQ_BULK_TRANSMIT_ABORTED,  /* service, -, bulk_userdata */
	VCHIQ_BULK_RECEIVE_ABORTED    /* service, -, bulk_userdata */
};

enum vchiq_status {
	VCHIQ_ERROR   = -1,
	VCHIQ_SUCCESS = 0,
	VCHIQ_RETRY   = 1
};

enum vchiq_bulk_mode {
	VCHIQ_BULK_MODE_CALLBACK,
	VCHIQ_BULK_MODE_BLOCKING,
	VCHIQ_BULK_MODE_NOCALLBACK,
	VCHIQ_BULK_MODE_WAITING		/* Reserved for internal use */
};

enum vchiq_service_option {
	VCHIQ_SERVICE_OPTION_AUTOCLOSE,
	VCHIQ_SERVICE_OPTION_SLOT_QUOTA,
	VCHIQ_SERVICE_OPTION_MESSAGE_QUOTA,
	VCHIQ_SERVICE_OPTION_SYNCHRONOUS,
	VCHIQ_SERVICE_OPTION_TRACE
};

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

typedef enum vchiq_status (*vchiq_callback)(enum vchiq_reason,
					    struct vchiq_header *,
					    unsigned int, void *);

struct vchiq_service_base {
	int fourcc;
	vchiq_callback callback;
	void *userdata;
};

struct vchiq_service_params {
	int fourcc;
	vchiq_callback callback;
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

struct vchiq_instance;
typedef void (*vchiq_remote_callback)(void *cb_arg);

extern enum vchiq_status vchiq_initialise(struct vchiq_instance **pinstance);
extern enum vchiq_status vchiq_shutdown(struct vchiq_instance *instance);
extern enum vchiq_status vchiq_connect(struct vchiq_instance *instance);
extern enum vchiq_status vchiq_add_service(struct vchiq_instance *instance,
	const struct vchiq_service_params *params,
	unsigned int *pservice);
extern enum vchiq_status vchiq_open_service(struct vchiq_instance *instance,
	const struct vchiq_service_params *params,
	unsigned int *pservice);
extern enum vchiq_status vchiq_close_service(unsigned int service);
extern enum vchiq_status vchiq_remove_service(unsigned int service);
extern enum vchiq_status vchiq_use_service(unsigned int service);
extern enum vchiq_status vchiq_release_service(unsigned int service);
extern enum vchiq_status
vchiq_queue_message(unsigned int handle,
		    ssize_t (*copy_callback)(void *context, void *dest,
					     size_t offset, size_t maxsize),
		    void *context,
		    size_t size);
extern void           vchiq_release_message(unsigned int service,
	struct vchiq_header *header);
extern enum vchiq_status vchiq_bulk_transmit(unsigned int service,
	const void *data, unsigned int size, void *userdata,
	enum vchiq_bulk_mode mode);
extern enum vchiq_status vchiq_bulk_receive(unsigned int service,
	void *data, unsigned int size, void *userdata,
	enum vchiq_bulk_mode mode);
extern enum vchiq_status vchiq_bulk_transmit_handle(unsigned int service,
	const void *offset, unsigned int size,
	void *userdata,	enum vchiq_bulk_mode mode);
extern enum vchiq_status vchiq_bulk_receive_handle(unsigned int service,
	void *offset, unsigned int size, void *userdata,
	enum vchiq_bulk_mode mode);
extern int   vchiq_get_client_id(unsigned int service);
extern void *vchiq_get_service_userdata(unsigned int service);
extern void vchiq_get_config(struct vchiq_config *config);
extern enum vchiq_status vchiq_set_service_option(unsigned int service,
	enum vchiq_service_option option, int value);

extern enum vchiq_status vchiq_remote_use(struct vchiq_instance *instance,
	vchiq_remote_callback callback, void *cb_arg);
extern enum vchiq_status vchiq_remote_release(struct vchiq_instance *instance);

extern enum vchiq_status vchiq_dump_phys_mem(unsigned int service,
	void *ptr, size_t num_bytes);

extern enum vchiq_status vchiq_get_peer_version(unsigned int handle,
      short *peer_version);

#endif /* VCHIQ_IF_H */
