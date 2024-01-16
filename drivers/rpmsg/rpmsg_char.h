/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022, STMicroelectronics
 */

#ifndef __RPMSG_CHRDEV_H__
#define __RPMSG_CHRDEV_H__

#if IS_ENABLED(CONFIG_RPMSG_CHAR)
/**
 * rpmsg_chrdev_eptdev_create() - register char device based on an endpoint
 * @rpdev:  prepared rpdev to be used for creating endpoints
 * @parent: parent device
 * @chinfo: associated endpoint channel information.
 *
 * This function create a new rpmsg char endpoint device to instantiate a new
 * endpoint based on chinfo information.
 */
int rpmsg_chrdev_eptdev_create(struct rpmsg_device *rpdev, struct device *parent,
			       struct rpmsg_channel_info chinfo);

/**
 * rpmsg_chrdev_eptdev_destroy() - destroy created char device endpoint.
 * @data: private data associated to the endpoint device
 *
 * This function destroys a rpmsg char endpoint device created by the RPMSG_DESTROY_EPT_IOCTL
 * control.
 */
int rpmsg_chrdev_eptdev_destroy(struct device *dev, void *data);

#else  /*IS_ENABLED(CONFIG_RPMSG_CHAR) */

static inline int rpmsg_chrdev_eptdev_create(struct rpmsg_device *rpdev, struct device *parent,
					     struct rpmsg_channel_info chinfo)
{
	return -ENXIO;
}

static inline int rpmsg_chrdev_eptdev_destroy(struct device *dev, void *data)
{
	return -ENXIO;
}

#endif /*IS_ENABLED(CONFIG_RPMSG_CHAR) */

#endif /*__RPMSG_CHRDEV_H__ */
