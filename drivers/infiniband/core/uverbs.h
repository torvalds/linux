/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef UVERBS_H
#define UVERBS_H

#include <linux/kref.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/cdev.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>

#define INIT_UDATA(udata, ibuf, obuf, ilen, olen)			\
	do {								\
		(udata)->inbuf  = (const void __user *) (ibuf);		\
		(udata)->outbuf = (void __user *) (obuf);		\
		(udata)->inlen  = (ilen);				\
		(udata)->outlen = (olen);				\
	} while (0)

#define INIT_UDATA_BUF_OR_NULL(udata, ibuf, obuf, ilen, olen)			\
	do {									\
		(udata)->inbuf  = (ilen) ? (const void __user *) (ibuf) : NULL;	\
		(udata)->outbuf = (olen) ? (void __user *) (obuf) : NULL;	\
		(udata)->inlen  = (ilen);					\
		(udata)->outlen = (olen);					\
	} while (0)

/*
 * Our lifetime rules for these structs are the following:
 *
 * struct ib_uverbs_device: One reference is held by the module and
 * released in ib_uverbs_remove_one().  Another reference is taken by
 * ib_uverbs_open() each time the character special file is opened,
 * and released in ib_uverbs_release_file() when the file is released.
 *
 * struct ib_uverbs_file: One reference is held by the VFS and
 * released when the file is closed.  Another reference is taken when
 * an asynchronous event queue file is created and released when the
 * event file is closed.
 *
 * struct ib_uverbs_event_file: One reference is held by the VFS and
 * released when the file is closed.  For asynchronous event files,
 * another reference is held by the corresponding main context file
 * and released when that file is closed.  For completion event files,
 * a reference is taken when a CQ is created that uses the file, and
 * released when the CQ is destroyed.
 */

struct ib_uverbs_device {
	atomic_t				refcount;
	int					num_comp_vectors;
	struct completion			comp;
	struct device			       *dev;
	struct ib_device	__rcu	       *ib_dev;
	int					devnum;
	struct cdev			        cdev;
	struct rb_root				xrcd_tree;
	struct mutex				xrcd_tree_mutex;
	struct kobject				kobj;
	struct srcu_struct			disassociate_srcu;
	struct mutex				lists_mutex; /* protect lists */
	struct list_head			uverbs_file_list;
	struct list_head			uverbs_events_file_list;
};

struct ib_uverbs_event_file {
	struct kref				ref;
	int					is_async;
	struct ib_uverbs_file		       *uverbs_file;
	spinlock_t				lock;
	int					is_closed;
	wait_queue_head_t			poll_wait;
	struct fasync_struct		       *async_queue;
	struct list_head			event_list;
	struct list_head			list;
};

struct ib_uverbs_file {
	struct kref				ref;
	struct mutex				mutex;
	struct ib_uverbs_device		       *device;
	struct ib_ucontext		       *ucontext;
	struct ib_event_handler			event_handler;
	struct ib_uverbs_event_file	       *async_file;
	struct list_head			list;
	int					is_closed;
};

struct ib_uverbs_event {
	union {
		struct ib_uverbs_async_event_desc	async;
		struct ib_uverbs_comp_event_desc	comp;
	}					desc;
	struct list_head			list;
	struct list_head			obj_list;
	u32				       *counter;
};

struct ib_uverbs_mcast_entry {
	struct list_head	list;
	union ib_gid 		gid;
	u16 			lid;
};

struct ib_uevent_object {
	struct ib_uobject	uobject;
	struct list_head	event_list;
	u32			events_reported;
};

struct ib_uxrcd_object {
	struct ib_uobject	uobject;
	atomic_t		refcnt;
};

struct ib_usrq_object {
	struct ib_uevent_object	uevent;
	struct ib_uxrcd_object *uxrcd;
};

struct ib_uqp_object {
	struct ib_uevent_object	uevent;
	struct list_head 	mcast_list;
	struct ib_uxrcd_object *uxrcd;
};

struct ib_ucq_object {
	struct ib_uobject	uobject;
	struct ib_uverbs_file  *uverbs_file;
	struct list_head	comp_list;
	struct list_head	async_list;
	u32			comp_events_reported;
	u32			async_events_reported;
};

extern spinlock_t ib_uverbs_idr_lock;
extern struct idr ib_uverbs_pd_idr;
extern struct idr ib_uverbs_mr_idr;
extern struct idr ib_uverbs_mw_idr;
extern struct idr ib_uverbs_ah_idr;
extern struct idr ib_uverbs_cq_idr;
extern struct idr ib_uverbs_qp_idr;
extern struct idr ib_uverbs_srq_idr;
extern struct idr ib_uverbs_xrcd_idr;
extern struct idr ib_uverbs_rule_idr;

void idr_remove_uobj(struct idr *idp, struct ib_uobject *uobj);

struct file *ib_uverbs_alloc_event_file(struct ib_uverbs_file *uverbs_file,
					struct ib_device *ib_dev,
					int is_async);
void ib_uverbs_free_async_event_file(struct ib_uverbs_file *uverbs_file);
struct ib_uverbs_event_file *ib_uverbs_lookup_comp_file(int fd);

void ib_uverbs_release_ucq(struct ib_uverbs_file *file,
			   struct ib_uverbs_event_file *ev_file,
			   struct ib_ucq_object *uobj);
void ib_uverbs_release_uevent(struct ib_uverbs_file *file,
			      struct ib_uevent_object *uobj);

void ib_uverbs_comp_handler(struct ib_cq *cq, void *cq_context);
void ib_uverbs_cq_event_handler(struct ib_event *event, void *context_ptr);
void ib_uverbs_qp_event_handler(struct ib_event *event, void *context_ptr);
void ib_uverbs_srq_event_handler(struct ib_event *event, void *context_ptr);
void ib_uverbs_event_handler(struct ib_event_handler *handler,
			     struct ib_event *event);
void ib_uverbs_dealloc_xrcd(struct ib_uverbs_device *dev, struct ib_xrcd *xrcd);

int uverbs_dealloc_mw(struct ib_mw *mw);

struct ib_uverbs_flow_spec {
	union {
		union {
			struct ib_uverbs_flow_spec_hdr hdr;
			struct {
				__u32 type;
				__u16 size;
				__u16 reserved;
			};
		};
		struct ib_uverbs_flow_spec_eth     eth;
		struct ib_uverbs_flow_spec_ipv4    ipv4;
		struct ib_uverbs_flow_spec_tcp_udp tcp_udp;
	};
};

#define IB_UVERBS_DECLARE_CMD(name)					\
	ssize_t ib_uverbs_##name(struct ib_uverbs_file *file,		\
				 struct ib_device *ib_dev,              \
				 const char __user *buf, int in_len,	\
				 int out_len)

IB_UVERBS_DECLARE_CMD(get_context);
IB_UVERBS_DECLARE_CMD(query_device);
IB_UVERBS_DECLARE_CMD(query_port);
IB_UVERBS_DECLARE_CMD(alloc_pd);
IB_UVERBS_DECLARE_CMD(dealloc_pd);
IB_UVERBS_DECLARE_CMD(reg_mr);
IB_UVERBS_DECLARE_CMD(rereg_mr);
IB_UVERBS_DECLARE_CMD(dereg_mr);
IB_UVERBS_DECLARE_CMD(alloc_mw);
IB_UVERBS_DECLARE_CMD(dealloc_mw);
IB_UVERBS_DECLARE_CMD(create_comp_channel);
IB_UVERBS_DECLARE_CMD(create_cq);
IB_UVERBS_DECLARE_CMD(resize_cq);
IB_UVERBS_DECLARE_CMD(poll_cq);
IB_UVERBS_DECLARE_CMD(req_notify_cq);
IB_UVERBS_DECLARE_CMD(destroy_cq);
IB_UVERBS_DECLARE_CMD(create_qp);
IB_UVERBS_DECLARE_CMD(open_qp);
IB_UVERBS_DECLARE_CMD(query_qp);
IB_UVERBS_DECLARE_CMD(modify_qp);
IB_UVERBS_DECLARE_CMD(destroy_qp);
IB_UVERBS_DECLARE_CMD(post_send);
IB_UVERBS_DECLARE_CMD(post_recv);
IB_UVERBS_DECLARE_CMD(post_srq_recv);
IB_UVERBS_DECLARE_CMD(create_ah);
IB_UVERBS_DECLARE_CMD(destroy_ah);
IB_UVERBS_DECLARE_CMD(attach_mcast);
IB_UVERBS_DECLARE_CMD(detach_mcast);
IB_UVERBS_DECLARE_CMD(create_srq);
IB_UVERBS_DECLARE_CMD(modify_srq);
IB_UVERBS_DECLARE_CMD(query_srq);
IB_UVERBS_DECLARE_CMD(destroy_srq);
IB_UVERBS_DECLARE_CMD(create_xsrq);
IB_UVERBS_DECLARE_CMD(open_xrcd);
IB_UVERBS_DECLARE_CMD(close_xrcd);

#define IB_UVERBS_DECLARE_EX_CMD(name)				\
	int ib_uverbs_ex_##name(struct ib_uverbs_file *file,	\
				struct ib_device *ib_dev,		\
				struct ib_udata *ucore,		\
				struct ib_udata *uhw)

IB_UVERBS_DECLARE_EX_CMD(create_flow);
IB_UVERBS_DECLARE_EX_CMD(destroy_flow);
IB_UVERBS_DECLARE_EX_CMD(query_device);
IB_UVERBS_DECLARE_EX_CMD(create_cq);
IB_UVERBS_DECLARE_EX_CMD(create_qp);

#endif /* UVERBS_H */
