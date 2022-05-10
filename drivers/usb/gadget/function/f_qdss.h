/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _F_QDSS_H
#define _F_QDSS_H

#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/ipc_logging.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>
#include <linux/usb/usb_qdss.h>

enum qti_port_type {
	QTI_PORT_RMNET,
	QTI_PORT_DPL,
	QTI_NUM_PORTS
};

struct usb_qdss_ch {
	const char *name;
	struct list_head list;
	void (*notify)(void *priv, unsigned int event,
		struct qdss_request *d_req, struct usb_qdss_ch *ch);
	void *priv;
	int ch_type;
};

struct gqdss {
	struct usb_function function;
	struct usb_ep *ctrl_out;
	struct usb_ep *ctrl_in;
	struct usb_ep *data;
	int (*send_encap_cmd)(enum qti_port_type qport, void *buf, size_t len);
	void (*notify_modem)(void *g, enum qti_port_type qport, int cbits);
};

/* struct f_qdss - USB qdss function driver private structure */
struct f_qdss {
	struct gqdss port;
	struct usb_gadget *gadget;
	short int port_num;
	u8 ctrl_iface_id;
	u8 data_iface_id;
	int usb_connected;
	bool debug_inface_enabled;
	struct usb_request *endless_req;
	struct usb_qdss_ch ch;

	/* for mdm channel SW path */
	struct list_head data_write_pool;
	struct list_head queued_data_pool;
	struct list_head dequeued_data_pool;

	struct work_struct connect_w;
	struct work_struct disconnect_w;
	spinlock_t lock;
	unsigned int data_enabled:1;
	unsigned int ctrl_in_enabled:1;
	unsigned int ctrl_out_enabled:1;
	struct workqueue_struct *wq;

	struct mutex mutex;
	bool opened;	/* protected by 'mutex' */
	struct completion dequeue_done;
};

struct usb_qdss_opts {
	struct usb_function_instance func_inst;
	struct f_qdss *usb_qdss;
	char *channel_name;
};

struct qdss_req {
	struct usb_request *usb_req;
	struct qdss_request *qdss_req;
	struct list_head list;
};

int set_qdss_data_connection(struct f_qdss *qdss, int enable);
int alloc_hw_req(struct usb_ep *data_ep);
#endif
