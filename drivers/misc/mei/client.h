/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _MEI_CLIENT_H_
#define _MEI_CLIENT_H_

#include <linux/types.h>
#include <linux/poll.h>
#include <linux/mei.h>

#include "mei_dev.h"

/*
 * reference counting base function
 */
void mei_me_cl_init(struct mei_me_client *me_cl);
void mei_me_cl_put(struct mei_me_client *me_cl);
struct mei_me_client *mei_me_cl_get(struct mei_me_client *me_cl);

void mei_me_cl_add(struct mei_device *dev, struct mei_me_client *me_cl);
void mei_me_cl_del(struct mei_device *dev, struct mei_me_client *me_cl);

struct mei_me_client *mei_me_cl_by_uuid(struct mei_device *dev,
					const uuid_le *uuid);
struct mei_me_client *mei_me_cl_by_id(struct mei_device *dev, u8 client_id);
struct mei_me_client *mei_me_cl_by_uuid_id(struct mei_device *dev,
					   const uuid_le *uuid, u8 client_id);
void mei_me_cl_rm_by_uuid(struct mei_device *dev, const uuid_le *uuid);
void mei_me_cl_rm_by_uuid_id(struct mei_device *dev,
			     const uuid_le *uuid, u8 id);
void mei_me_cl_rm_all(struct mei_device *dev);

/**
 * mei_me_cl_is_active - check whether me client is active in the fw
 *
 * @me_cl: me client
 *
 * Return: true if the me client is active in the firmware
 */
static inline bool mei_me_cl_is_active(const struct mei_me_client *me_cl)
{
	return !list_empty_careful(&me_cl->list);
}

/**
 * mei_me_cl_uuid - return me client protocol name (uuid)
 *
 * @me_cl: me client
 *
 * Return: me client protocol name
 */
static inline const uuid_le *mei_me_cl_uuid(const struct mei_me_client *me_cl)
{
	return &me_cl->props.protocol_name;
}

/**
 * mei_me_cl_ver - return me client protocol version
 *
 * @me_cl: me client
 *
 * Return: me client protocol version
 */
static inline u8 mei_me_cl_ver(const struct mei_me_client *me_cl)
{
	return me_cl->props.protocol_version;
}

/*
 * MEI IO Functions
 */
void mei_io_cb_free(struct mei_cl_cb *priv_cb);

/*
 * MEI Host Client Functions
 */

struct mei_cl *mei_cl_allocate(struct mei_device *dev);

int mei_cl_link(struct mei_cl *cl);
int mei_cl_unlink(struct mei_cl *cl);

struct mei_cl *mei_cl_alloc_linked(struct mei_device *dev);

struct mei_cl_cb *mei_cl_read_cb(const struct mei_cl *cl,
				 const struct file *fp);
struct mei_cl_cb *mei_cl_alloc_cb(struct mei_cl *cl, size_t length,
				  enum mei_cb_file_ops type,
				  const struct file *fp);
struct mei_cl_cb *mei_cl_enqueue_ctrl_wr_cb(struct mei_cl *cl, size_t length,
					    enum mei_cb_file_ops type,
					    const struct file *fp);
int mei_cl_flush_queues(struct mei_cl *cl, const struct file *fp);

/*
 *  MEI input output function prototype
 */

/**
 * mei_cl_is_connected - host client is connected
 *
 * @cl: host client
 *
 * Return: true if the host client is connected
 */
static inline bool mei_cl_is_connected(struct mei_cl *cl)
{
	return  cl->state == MEI_FILE_CONNECTED;
}

/**
 * mei_cl_me_id - me client id
 *
 * @cl: host client
 *
 * Return: me client id or 0 if client is not connected
 */
static inline u8 mei_cl_me_id(const struct mei_cl *cl)
{
	return cl->me_cl ? cl->me_cl->client_id : 0;
}

/**
 * mei_cl_mtu - maximal message that client can send and receive
 *
 * @cl: host client
 *
 * Return: mtu
 */
static inline size_t mei_cl_mtu(const struct mei_cl *cl)
{
	return cl->me_cl->props.max_msg_length;
}

/**
 * mei_cl_is_fixed_address - check whether the me client uses fixed address
 *
 * @cl: host client
 *
 * Return: true if the client is connected and it has fixed me address
 */
static inline bool mei_cl_is_fixed_address(const struct mei_cl *cl)
{
	return cl->me_cl && cl->me_cl->props.fixed_address;
}

/**
 * mei_cl_is_single_recv_buf- check whether the me client
 *       uses single receiving buffer
 *
 * @cl: host client
 *
 * Return: true if single_recv_buf == 1; 0 otherwise
 */
static inline bool mei_cl_is_single_recv_buf(const struct mei_cl *cl)
{
	return cl->me_cl->props.single_recv_buf;
}

/**
 * mei_cl_uuid -  client's uuid
 *
 * @cl: host client
 *
 * Return: return uuid of connected me client
 */
static inline const uuid_le *mei_cl_uuid(const struct mei_cl *cl)
{
	return mei_me_cl_uuid(cl->me_cl);
}

/**
 * mei_cl_host_addr - client's host address
 *
 * @cl: host client
 *
 * Return: 0 for fixed address client, host address for dynamic client
 */
static inline u8 mei_cl_host_addr(const struct mei_cl *cl)
{
	return  mei_cl_is_fixed_address(cl) ? 0 : cl->host_client_id;
}

int mei_cl_disconnect(struct mei_cl *cl);
int mei_cl_irq_disconnect(struct mei_cl *cl, struct mei_cl_cb *cb,
			  struct list_head *cmpl_list);
int mei_cl_connect(struct mei_cl *cl, struct mei_me_client *me_cl,
		   const struct file *file);
int mei_cl_irq_connect(struct mei_cl *cl, struct mei_cl_cb *cb,
		       struct list_head *cmpl_list);
int mei_cl_read_start(struct mei_cl *cl, size_t length, const struct file *fp);
int mei_cl_write(struct mei_cl *cl, struct mei_cl_cb *cb);
int mei_cl_irq_write(struct mei_cl *cl, struct mei_cl_cb *cb,
		     struct list_head *cmpl_list);

void mei_cl_complete(struct mei_cl *cl, struct mei_cl_cb *cb);

void mei_host_client_init(struct mei_device *dev);

u8 mei_cl_notify_fop2req(enum mei_cb_file_ops fop);
enum mei_cb_file_ops mei_cl_notify_req2fop(u8 request);
int mei_cl_notify_request(struct mei_cl *cl,
			  const struct file *file, u8 request);
int mei_cl_irq_notify(struct mei_cl *cl, struct mei_cl_cb *cb,
		      struct list_head *cmpl_list);
int mei_cl_notify_get(struct mei_cl *cl, bool block, bool *notify_ev);
void mei_cl_notify(struct mei_cl *cl);

void mei_cl_all_disconnect(struct mei_device *dev);

#define MEI_CL_FMT "cl:host=%02d me=%02d "
#define MEI_CL_PRM(cl) (cl)->host_client_id, mei_cl_me_id(cl)

#define cl_dbg(dev, cl, format, arg...) \
	dev_dbg((dev)->dev, MEI_CL_FMT format, MEI_CL_PRM(cl), ##arg)

#define cl_warn(dev, cl, format, arg...) \
	dev_warn((dev)->dev, MEI_CL_FMT format, MEI_CL_PRM(cl), ##arg)

#define cl_err(dev, cl, format, arg...) \
	dev_err((dev)->dev, MEI_CL_FMT format, MEI_CL_PRM(cl), ##arg)

#endif /* _MEI_CLIENT_H_ */
