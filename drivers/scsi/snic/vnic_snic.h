/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2014 Cisco Systems, Inc.  All rights reserved. */

#ifndef _VNIC_SNIC_H_
#define _VNIC_SNIC_H_

#define VNIC_SNIC_WQ_DESCS_MIN              64
#define VNIC_SNIC_WQ_DESCS_MAX              1024

#define VNIC_SNIC_MAXDATAFIELDSIZE_MIN      256
#define VNIC_SNIC_MAXDATAFIELDSIZE_MAX      2112

#define VNIC_SNIC_IO_THROTTLE_COUNT_MIN     1
#define VNIC_SNIC_IO_THROTTLE_COUNT_MAX     1024

#define VNIC_SNIC_PORT_DOWN_TIMEOUT_MIN     0
#define VNIC_SNIC_PORT_DOWN_TIMEOUT_MAX     240000

#define VNIC_SNIC_PORT_DOWN_IO_RETRIES_MIN  0
#define VNIC_SNIC_PORT_DOWN_IO_RETRIES_MAX  255

#define VNIC_SNIC_LUNS_PER_TARGET_MIN       1
#define VNIC_SNIC_LUNS_PER_TARGET_MAX       1024

/* Device-specific region: scsi configuration */
struct vnic_snic_config {
	u32 flags;
	u32 wq_enet_desc_count;
	u32 io_throttle_count;
	u32 port_down_timeout;
	u32 port_down_io_retries;
	u32 luns_per_tgt;
	u16 maxdatafieldsize;
	u16 intr_timer;
	u8 intr_timer_type;
	u8 _resvd2;
	u8 xpt_type;
	u8 hid;
};
#endif /* _VNIC_SNIC_H_ */
