/*
 * Private include for xenbus communications.
 *
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
 * Copyright (C) 2005 XenSource Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _XENBUS_XENBUS_H
#define _XENBUS_XENBUS_H

#include <linux/mutex.h>
#include <linux/uio.h>
#include <xen/xenbus.h>

#define XEN_BUS_ID_SIZE			20

struct xen_bus_type {
	char *root;
	unsigned int levels;
	int (*get_bus_id)(char bus_id[XEN_BUS_ID_SIZE], const char *nodename);
	int (*probe)(struct xen_bus_type *bus, const char *type,
		     const char *dir);
	void (*otherend_changed)(struct xenbus_watch *watch, const char *path,
				 const char *token);
	struct bus_type bus;
};

enum xenstore_init {
	XS_UNKNOWN,
	XS_PV,
	XS_HVM,
	XS_LOCAL,
};

struct xs_watch_event {
	struct list_head list;
	unsigned int len;
	struct xenbus_watch *handle;
	const char *path;
	const char *token;
	char body[];
};

enum xb_req_state {
	xb_req_state_queued,
	xb_req_state_wait_reply,
	xb_req_state_got_reply,
	xb_req_state_aborted
};

struct xb_req_data {
	struct list_head list;
	wait_queue_head_t wq;
	struct xsd_sockmsg msg;
	uint32_t caller_req_id;
	enum xsd_sockmsg_type type;
	char *body;
	const struct kvec *vec;
	int num_vecs;
	int err;
	enum xb_req_state state;
	void (*cb)(struct xb_req_data *);
	void *par;
};

extern enum xenstore_init xen_store_domain_type;
extern const struct attribute_group *xenbus_dev_groups[];
extern struct mutex xs_response_mutex;
extern struct list_head xs_reply_list;
extern struct list_head xb_write_list;
extern wait_queue_head_t xb_waitq;
extern struct mutex xb_write_mutex;

int xs_init(void);
int xb_init_comms(void);
void xb_deinit_comms(void);
int xs_watch_msg(struct xs_watch_event *event);
void xs_request_exit(struct xb_req_data *req);

int xenbus_match(struct device *_dev, struct device_driver *_drv);
int xenbus_dev_probe(struct device *_dev);
int xenbus_dev_remove(struct device *_dev);
int xenbus_register_driver_common(struct xenbus_driver *drv,
				  struct xen_bus_type *bus,
				  struct module *owner,
				  const char *mod_name);
int xenbus_probe_node(struct xen_bus_type *bus,
		      const char *type,
		      const char *nodename);
int xenbus_probe_devices(struct xen_bus_type *bus);

void xenbus_dev_changed(const char *node, struct xen_bus_type *bus);

void xenbus_dev_shutdown(struct device *_dev);

int xenbus_dev_suspend(struct device *dev);
int xenbus_dev_resume(struct device *dev);
int xenbus_dev_cancel(struct device *dev);

void xenbus_otherend_changed(struct xenbus_watch *watch,
			     const char *path, const char *token,
			     int ignore_on_shutdown);

int xenbus_read_otherend_details(struct xenbus_device *xendev,
				 char *id_node, char *path_node);

void xenbus_ring_ops_init(void);

int xenbus_dev_request_and_reply(struct xsd_sockmsg *msg, void *par);
void xenbus_dev_queue_reply(struct xb_req_data *req);

#endif
