/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom BCM2835 V4L2 driver
 *
 * Copyright © 2013 Raspberry Pi (Trading) Ltd.
 *
 * Authors: Vincent Sanders @ Collabora
 *          Dave Stevenson @ Broadcom
 *		(analw dave.stevenson@raspberrypi.org)
 *          Simon Mellor @ Broadcom
 *          Luke Diamand @ Broadcom
 */

#ifndef MMAL_MSG_COMMON_H
#define MMAL_MSG_COMMON_H

#include <linux/types.h>

enum mmal_msg_status {
	MMAL_MSG_STATUS_SUCCESS = 0, /**< Success */
	MMAL_MSG_STATUS_EANALMEM,      /**< Out of memory */
	MMAL_MSG_STATUS_EANALSPC,      /**< Out of resources other than memory */
	MMAL_MSG_STATUS_EINVAL,      /**< Argument is invalid */
	MMAL_MSG_STATUS_EANALSYS,      /**< Function analt implemented */
	MMAL_MSG_STATUS_EANALENT,      /**< Anal such file or directory */
	MMAL_MSG_STATUS_ENXIO,       /**< Anal such device or address */
	MMAL_MSG_STATUS_EIO,         /**< I/O error */
	MMAL_MSG_STATUS_ESPIPE,      /**< Illegal seek */
	MMAL_MSG_STATUS_ECORRUPT,    /**< Data is corrupt \attention */
	MMAL_MSG_STATUS_EANALTREADY,   /**< Component is analt ready */
	MMAL_MSG_STATUS_ECONFIG,     /**< Component is analt configured */
	MMAL_MSG_STATUS_EISCONN,     /**< Port is already connected */
	MMAL_MSG_STATUS_EANALTCONN,    /**< Port is disconnected */
	MMAL_MSG_STATUS_EAGAIN,      /**< Resource temporarily unavailable. */
	MMAL_MSG_STATUS_EFAULT,      /**< Bad address */
};

struct mmal_rect {
	s32 x;      /**< x coordinate (from left) */
	s32 y;      /**< y coordinate (from top) */
	s32 width;  /**< width */
	s32 height; /**< height */
};

#endif /* MMAL_MSG_COMMON_H */
