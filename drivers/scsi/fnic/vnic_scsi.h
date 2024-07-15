/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _VNIC_SCSI_H_
#define _VNIC_SCSI_H_

#define VNIC_FNIC_WQ_COPY_COUNT_MIN         1
#define VNIC_FNIC_WQ_COPY_COUNT_MAX         1

#define VNIC_FNIC_WQ_DESCS_MIN              64
#define VNIC_FNIC_WQ_DESCS_MAX              128

#define VNIC_FNIC_WQ_COPY_DESCS_MIN         64
#define VNIC_FNIC_WQ_COPY_DESCS_MAX         512

#define VNIC_FNIC_RQ_DESCS_MIN              64
#define VNIC_FNIC_RQ_DESCS_MAX              128

#define VNIC_FNIC_EDTOV_MIN                 1000
#define VNIC_FNIC_EDTOV_MAX                 255000
#define VNIC_FNIC_EDTOV_DEF                 2000

#define VNIC_FNIC_RATOV_MIN                 1000
#define VNIC_FNIC_RATOV_MAX                 255000

#define VNIC_FNIC_MAXDATAFIELDSIZE_MIN      256
#define VNIC_FNIC_MAXDATAFIELDSIZE_MAX      2048

#define VNIC_FNIC_FLOGI_RETRIES_MIN         0
#define VNIC_FNIC_FLOGI_RETRIES_MAX         0xffffffff
#define VNIC_FNIC_FLOGI_RETRIES_DEF         0xffffffff

#define VNIC_FNIC_FLOGI_TIMEOUT_MIN         1000
#define VNIC_FNIC_FLOGI_TIMEOUT_MAX         255000

#define VNIC_FNIC_PLOGI_RETRIES_MIN         0
#define VNIC_FNIC_PLOGI_RETRIES_MAX         255
#define VNIC_FNIC_PLOGI_RETRIES_DEF         8

#define VNIC_FNIC_PLOGI_TIMEOUT_MIN         1000
#define VNIC_FNIC_PLOGI_TIMEOUT_MAX         255000

#define VNIC_FNIC_IO_THROTTLE_COUNT_MIN     1
#define VNIC_FNIC_IO_THROTTLE_COUNT_MAX     2048

#define VNIC_FNIC_LINK_DOWN_TIMEOUT_MIN     0
#define VNIC_FNIC_LINK_DOWN_TIMEOUT_MAX     240000

#define VNIC_FNIC_PORT_DOWN_TIMEOUT_MIN     0
#define VNIC_FNIC_PORT_DOWN_TIMEOUT_MAX     240000

#define VNIC_FNIC_PORT_DOWN_IO_RETRIES_MIN  0
#define VNIC_FNIC_PORT_DOWN_IO_RETRIES_MAX  255

#define VNIC_FNIC_LUNS_PER_TARGET_MIN       1
#define VNIC_FNIC_LUNS_PER_TARGET_MAX       4096

/* Device-specific region: scsi configuration */
struct vnic_fc_config {
	u64 node_wwn;
	u64 port_wwn;
	u32 flags;
	u32 wq_enet_desc_count;
	u32 wq_copy_desc_count;
	u32 rq_desc_count;
	u32 flogi_retries;
	u32 flogi_timeout;
	u32 plogi_retries;
	u32 plogi_timeout;
	u32 io_throttle_count;
	u32 link_down_timeout;
	u32 port_down_timeout;
	u32 port_down_io_retries;
	u32 luns_per_tgt;
	u16 maxdatafieldsize;
	u16 ed_tov;
	u16 ra_tov;
	u16 intr_timer;
	u8 intr_timer_type;
	u8 intr_mode;
	u8 lun_queue_depth;
	u8 io_timeout_retry;
	u16 wq_copy_count;
};

#define VFCF_FCP_SEQ_LVL_ERR	0x1	/* Enable FCP-2 Error Recovery */
#define VFCF_PERBI		0x2	/* persistent binding info available */
#define VFCF_FIP_CAPABLE	0x4	/* firmware can handle FIP */

#define VFCF_FC_INITIATOR         0x20    /* FC Initiator Mode */
#define VFCF_FC_TARGET            0x40    /* FC Target Mode */
#define VFCF_FC_NVME_INITIATOR    0x80    /* FC-NVMe Initiator Mode */
#define VFCF_FC_NVME_TARGET       0x100   /* FC-NVMe Target Mode */

#endif /* _VNIC_SCSI_H_ */
