/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __BBB_CONFIG_H_INCLUDED__
#define __BBB_CONFIG_H_INCLUDED__
/* This header contains BBB defines common to ISP and host */

#define BFA_MAX_KWAY                (49)
#define BFA_RW_LUT_SIZE             (7)

#define SAD3x3_IN_SHIFT      (2) /* input right shift value for SAD3x3 */
#define SAD3x3_OUT_SHIFT     (2) /* output right shift value for SAD3x3 */

/* XCU and BMA related defines shared between host and ISP
 * also need to be moved here */
#endif /* __BBB_CONFIG_H_INCLUDED__ */
