/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: dpc.h
 *
 * Purpose:
 *
 * Author: Jerry Chen
 *
 * Date: Jun. 27, 2002
 *
 */

#ifndef __DPC_H__
#define __DPC_H__

#include "device.h"

int vnt_rx_data(struct vnt_private *priv, struct vnt_rcb *ptr_rcb,
		unsigned long bytes_received);

#endif /* __RXTX_H__ */
