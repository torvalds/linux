/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * isphist.h
 *
 * TI OMAP3 ISP - Histogram module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: David Cohen <dacohen@gmail.com>
 *	     Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 */

#ifndef OMAP3_ISP_HIST_H
#define OMAP3_ISP_HIST_H

#include <linux/omap3isp.h>

#define ISPHIST_IN_BIT_WIDTH_CCDC	10

struct isp_device;

int omap3isp_hist_init(struct isp_device *isp);
void omap3isp_hist_cleanup(struct isp_device *isp);

#endif /* OMAP3_ISP_HIST */
