/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHIQ_H
#define VCHIQ_H

#define VCHIQ_MAKE_FOURCC(x0, x1, x2, x3) \
			(((x0) << 24) | ((x1) << 16) | ((x2) << 8) | (x3))

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

	char data[];           /* message */
};

struct vchiq_element {
	const void __user *data;
	unsigned int size;
};

struct vchiq_instance;

struct vchiq_service_base {
	int fourcc;
	enum vchiq_status (*callback)(struct vchiq_instance *instance,
				      enum vchiq_reason reason,
				      struct vchiq_header *header,
				      unsigned int handle,
				      void *bulk_userdata);
	void *userdata;
};

struct vchiq_completion_data_kernel {
	enum vchiq_reason reason;
	struct vchiq_header *header;
	void *service_userdata;
	void *bulk_userdata;
};

struct vchiq_service_params_kernel {
	int fourcc;
	enum vchiq_status (*callback)(struct vchiq_instance *instance,
				      enum vchiq_reason reason,
				      struct vchiq_header *header,
				      unsigned int handle,
				      void *bulk_userdata);
	void *userdata;
	short version;       /* Increment for non-trivial changes */
	short version_min;   /* Update for incompatible changes */
};

struct vchiq_instance;

extern int vchiq_initialise(struct vchiq_instance **pinstance);
extern enum vchiq_status vchiq_shutdown(struct vchiq_instance *instance);
extern enum vchiq_status vchiq_connect(struct vchiq_instance *instance);
extern enum vchiq_status vchiq_open_service(struct vchiq_instance *instance,
	const struct vchiq_service_params_kernel *params,
	unsigned int *pservice);
extern enum vchiq_status vchiq_close_service(struct vchiq_instance *instance,
					     unsigned int service);
extern enum vchiq_status vchiq_use_service(struct vchiq_instance *instance, unsigned int service);
extern enum vchiq_status vchiq_release_service(struct vchiq_instance *instance,
					       unsigned int service);
extern void vchiq_msg_queue_push(struct vchiq_instance *instance, unsigned int handle,
				 struct vchiq_header *header);
extern void vchiq_release_message(struct vchiq_instance *instance, unsigned int service,
				  struct vchiq_header *header);
extern int vchiq_queue_kernel_message(struct vchiq_instance *instance, unsigned int handle,
				      void *data, unsigned int size);
extern enum vchiq_status vchiq_bulk_transmit(struct vchiq_instance *instance, unsigned int service,
					     const void *data, unsigned int size, void *userdata,
					     enum vchiq_bulk_mode mode);
extern enum vchiq_status vchiq_bulk_receive(struct vchiq_instance *instance, unsigned int service,
					    void *data, unsigned int size, void *userdata,
					    enum vchiq_bulk_mode mode);
extern void *vchiq_get_service_userdata(struct vchiq_instance *instance, unsigned int service);
extern enum vchiq_status vchiq_get_peer_version(struct vchiq_instance *instance,
						unsigned int handle,
						short *peer_version);
extern struct vchiq_header *vchiq_msg_hold(struct vchiq_instance *instance, unsigned int handle);

#endif /* VCHIQ_H */
