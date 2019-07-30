/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2014 Broadcom. All rights reserved. */

#ifndef VCHIQ_CFG_H
#define VCHIQ_CFG_H

#define VCHIQ_MAGIC              VCHIQ_MAKE_FOURCC('V', 'C', 'H', 'I')
/* The version of VCHIQ - change with any non-trivial change */
#define VCHIQ_VERSION            8
/* The minimum compatible version - update to match VCHIQ_VERSION with any
** incompatible change */
#define VCHIQ_VERSION_MIN        3

/* The version that introduced the VCHIQ_IOC_LIB_VERSION ioctl */
#define VCHIQ_VERSION_LIB_VERSION 7

/* The version that introduced the VCHIQ_IOC_CLOSE_DELIVERED ioctl */
#define VCHIQ_VERSION_CLOSE_DELIVERED 7

/* The version that made it safe to use SYNCHRONOUS mode */
#define VCHIQ_VERSION_SYNCHRONOUS_MODE 8

#define VCHIQ_MAX_STATES         1
#define VCHIQ_MAX_SERVICES       4096
#define VCHIQ_MAX_SLOTS          128
#define VCHIQ_MAX_SLOTS_PER_SIDE 64

#define VCHIQ_NUM_CURRENT_BULKS        32
#define VCHIQ_NUM_SERVICE_BULKS        4

#ifndef VCHIQ_ENABLE_DEBUG
#define VCHIQ_ENABLE_DEBUG             1
#endif

#ifndef VCHIQ_ENABLE_STATS
#define VCHIQ_ENABLE_STATS             1
#endif

#endif /* VCHIQ_CFG_H */
