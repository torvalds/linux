/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013-2014, 2016-2018 The Linux Foundation. All rights reserved.
 */

#ifndef _RMNET_PRIVATE_H_
#define _RMNET_PRIVATE_H_

#define RMNET_MAX_PACKET_SIZE      16384
#define RMNET_DFLT_PACKET_SIZE     1500
#define RMNET_NEEDED_HEADROOM      16
#define RMNET_TX_QUEUE_LEN         1000

/* Replace skb->dev to a virtual rmnet device and pass up the stack */
#define RMNET_EPMODE_VND (1)
/* Pass the frame directly to another device with dev_queue_xmit() */
#define RMNET_EPMODE_BRIDGE (2)

#endif /* _RMNET_PRIVATE_H_ */
