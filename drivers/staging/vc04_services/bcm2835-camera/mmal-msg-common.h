/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom BM2835 V4L2 driver
 *
 * Copyright © 2013 Raspberry Pi (Trading) Ltd.
 *
 * Authors: Vincent Sanders <vincent.sanders@collabora.co.uk>
 *          Dave Stevenson <dsteve@broadcom.com>
 *          Simon Mellor <simellor@broadcom.com>
 *          Luke Diamand <luked@broadcom.com>
 */

#ifndef MMAL_MSG_COMMON_H
#define MMAL_MSG_COMMON_H

enum mmal_msg_status {
	MMAL_MSG_STATUS_SUCCESS = 0, /**< Success */
	MMAL_MSG_STATUS_ENOMEM,      /**< Out of memory */
	MMAL_MSG_STATUS_ENOSPC,      /**< Out of resources other than memory */
	MMAL_MSG_STATUS_EINVAL,      /**< Argument is invalid */
	MMAL_MSG_STATUS_ENOSYS,      /**< Function not implemented */
	MMAL_MSG_STATUS_ENOENT,      /**< No such file or directory */
	MMAL_MSG_STATUS_ENXIO,       /**< No such device or address */
	MMAL_MSG_STATUS_EIO,         /**< I/O error */
	MMAL_MSG_STATUS_ESPIPE,      /**< Illegal seek */
	MMAL_MSG_STATUS_ECORRUPT,    /**< Data is corrupt \attention */
	MMAL_MSG_STATUS_ENOTREADY,   /**< Component is not ready */
	MMAL_MSG_STATUS_ECONFIG,     /**< Component is not configured */
	MMAL_MSG_STATUS_EISCONN,     /**< Port is already connected */
	MMAL_MSG_STATUS_ENOTCONN,    /**< Port is disconnected */
	MMAL_MSG_STATUS_EAGAIN,      /**< Resource temporarily unavailable. */
	MMAL_MSG_STATUS_EFAULT,      /**< Bad address */
};

struct mmal_rect {
	s32 x;      /**< x coordinate (from left) */
	s32 y;      /**< y coordinate (from top) */
	s32 width;  /**< width */
	s32 height; /**< height */
};

struct mmal_rational {
	s32 num;    /**< Numerator */
	s32 den;    /**< Denominator */
};

#endif /* MMAL_MSG_COMMON_H */
