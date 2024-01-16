/* SPDX-License-Identifier: GPL-2.0 */
/*
 * remote processor messaging bus internals
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 */

#ifndef __RPMSG_INTERNAL_H__
#define __RPMSG_INTERNAL_H__

#include <linux/rpmsg.h>
#include <linux/poll.h>

#define to_rpmsg_device(d) container_of(d, struct rpmsg_device, dev)
#define to_rpmsg_driver(d) container_of(d, struct rpmsg_driver, drv)

extern struct class *rpmsg_class;

/**
 * struct rpmsg_device_ops - indirection table for the rpmsg_device operations
 * @create_channel:	create backend-specific channel, optional
 * @release_channel:	release backend-specific channel, optional
 * @create_ept:		create backend-specific endpoint, required
 * @announce_create:	announce presence of new channel, optional
 * @announce_destroy:	announce destruction of channel, optional
 *
 * Indirection table for the operations that a rpmsg backend should implement.
 * @announce_create and @announce_destroy are optional as the backend might
 * advertise new channels implicitly by creating the endpoints.
 */
struct rpmsg_device_ops {
	struct rpmsg_device *(*create_channel)(struct rpmsg_device *rpdev,
					       struct rpmsg_channel_info *chinfo);
	int (*release_channel)(struct rpmsg_device *rpdev,
			       struct rpmsg_channel_info *chinfo);
	struct rpmsg_endpoint *(*create_ept)(struct rpmsg_device *rpdev,
					    rpmsg_rx_cb_t cb, void *priv,
					    struct rpmsg_channel_info chinfo);

	int (*announce_create)(struct rpmsg_device *rpdev);
	int (*announce_destroy)(struct rpmsg_device *rpdev);
};

/**
 * struct rpmsg_endpoint_ops - indirection table for rpmsg_endpoint operations
 * @destroy_ept:	see @rpmsg_destroy_ept(), required
 * @send:		see @rpmsg_send(), required
 * @sendto:		see @rpmsg_sendto(), optional
 * @send_offchannel:	see @rpmsg_send_offchannel(), optional
 * @trysend:		see @rpmsg_trysend(), required
 * @trysendto:		see @rpmsg_trysendto(), optional
 * @trysend_offchannel:	see @rpmsg_trysend_offchannel(), optional
 * @poll:		see @rpmsg_poll(), optional
 * @get_mtu:		see @rpmsg_get_mtu(), optional
 *
 * Indirection table for the operations that a rpmsg backend should implement.
 * In addition to @destroy_ept, the backend must at least implement @send and
 * @trysend, while the variants sending data off-channel are optional.
 */
struct rpmsg_endpoint_ops {
	void (*destroy_ept)(struct rpmsg_endpoint *ept);

	int (*send)(struct rpmsg_endpoint *ept, void *data, int len);
	int (*sendto)(struct rpmsg_endpoint *ept, void *data, int len, u32 dst);
	int (*send_offchannel)(struct rpmsg_endpoint *ept, u32 src, u32 dst,
				  void *data, int len);

	int (*trysend)(struct rpmsg_endpoint *ept, void *data, int len);
	int (*trysendto)(struct rpmsg_endpoint *ept, void *data, int len, u32 dst);
	int (*trysend_offchannel)(struct rpmsg_endpoint *ept, u32 src, u32 dst,
			     void *data, int len);
	__poll_t (*poll)(struct rpmsg_endpoint *ept, struct file *filp,
			     poll_table *wait);
	ssize_t (*get_mtu)(struct rpmsg_endpoint *ept);
};

struct device *rpmsg_find_device(struct device *parent,
				 struct rpmsg_channel_info *chinfo);

struct rpmsg_device *rpmsg_create_channel(struct rpmsg_device *rpdev,
					  struct rpmsg_channel_info *chinfo);
int rpmsg_release_channel(struct rpmsg_device *rpdev,
			  struct rpmsg_channel_info *chinfo);
/**
 * rpmsg_ctrldev_register_device() - register a char device for control based on rpdev
 * @rpdev:	prepared rpdev to be used for creating endpoints
 *
 * This function wraps rpmsg_register_device() preparing the rpdev for use as
 * basis for the rpmsg chrdev.
 */
static inline int rpmsg_ctrldev_register_device(struct rpmsg_device *rpdev)
{
	return rpmsg_register_device_override(rpdev, "rpmsg_ctrl");
}

#endif
