/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _FIP_H_
#define _FIP_H_

#include "fdls_fc.h"
#include "fnic_fdls.h"
#include <scsi/fc/fc_fip.h>

/* Drop the cast from the standard definition */
#define FCOE_ALL_FCFS_MAC {0x01, 0x10, 0x18, 0x01, 0x00, 0x02}
#define FCOE_MAX_SIZE 0x082E

#define FCOE_CTLR_FIPVLAN_TOV (3*1000)
#define FCOE_CTLR_FCS_TOV     (3*1000)
#define FCOE_CTLR_MAX_SOL      (5*1000)

#define FIP_DISC_SOL_LEN (6)
#define FIP_VLAN_REQ_LEN (2)
#define FIP_ENODE_KA_LEN (2)
#define FIP_VN_KA_LEN (7)
#define FIP_FLOGI_LEN (38)

enum fdls_vlan_state {
	FIP_VLAN_AVAIL,
	FIP_VLAN_SENT
};

enum fdls_fip_state {
	FDLS_FIP_INIT,
	FDLS_FIP_VLAN_DISCOVERY_STARTED,
	FDLS_FIP_FCF_DISCOVERY_STARTED,
	FDLS_FIP_FLOGI_STARTED,
	FDLS_FIP_FLOGI_COMPLETE,
};

/*
 * VLAN entry.
 */
struct fcoe_vlan {
	struct list_head list;
	uint16_t vid;		/* vlan ID */
	uint16_t sol_count;	/* no. of sols sent */
	uint16_t state;		/* state */
};

struct fip_vlan_req {
	struct ethhdr eth;
	struct fip_header fip;
	struct fip_mac_desc mac_desc;
} __packed;

struct fip_vlan_notif {
	struct fip_header fip;
	struct fip_vlan_desc vlans_desc[];
} __packed;

struct fip_vn_port_ka {
	struct ethhdr eth;
	struct fip_header fip;
	struct fip_mac_desc mac_desc;
	struct fip_vn_desc vn_port_desc;
} __packed;

struct fip_enode_ka {
	struct ethhdr eth;
	struct fip_header fip;
	struct fip_mac_desc mac_desc;
} __packed;

struct fip_cvl {
	struct fip_header fip;
	struct fip_mac_desc fcf_mac_desc;
	struct fip_wwn_desc name_desc;
	struct fip_vn_desc vn_ports_desc[];
} __packed;

struct fip_flogi_desc {
	struct fip_desc fd_desc;
	uint16_t rsvd;
	struct fc_std_flogi flogi;
} __packed;

struct fip_flogi_rsp_desc {
	struct fip_desc fd_desc;
	uint16_t rsvd;
	struct fc_std_flogi flogi;
} __packed;

struct fip_flogi {
	struct ethhdr eth;
	struct fip_header fip;
	struct fip_flogi_desc flogi_desc;
	struct fip_mac_desc mac_desc;
} __packed;

struct fip_flogi_rsp {
	struct fip_header fip;
	struct fip_flogi_rsp_desc rsp_desc;
	struct fip_mac_desc mac_desc;
} __packed;

struct fip_discovery {
	struct ethhdr eth;
	struct fip_header fip;
	struct fip_mac_desc mac_desc;
	struct fip_wwn_desc name_desc;
	struct fip_size_desc fcoe_desc;
} __packed;

struct fip_disc_adv {
	struct fip_header fip;
	struct fip_pri_desc prio_desc;
	struct fip_mac_desc mac_desc;
	struct fip_wwn_desc name_desc;
	struct fip_fab_desc fabric_desc;
	struct fip_fka_desc fka_adv_desc;
} __packed;

void fnic_fcoe_process_vlan_resp(struct fnic *fnic, struct fip_header *fiph);
void fnic_fcoe_fip_discovery_resp(struct fnic *fnic, struct fip_header *fiph);
void fnic_fcoe_process_flogi_resp(struct fnic *fnic, struct fip_header *fiph);
void fnic_work_on_fip_timer(struct work_struct *work);
void fnic_work_on_fcs_ka_timer(struct work_struct *work);
void fnic_fcoe_send_vlan_req(struct fnic *fnic);
void fnic_fcoe_start_fcf_discovery(struct fnic *fnic);
void fnic_fcoe_start_flogi(struct fnic *fnic);
void fnic_fcoe_process_cvl(struct fnic *fnic, struct fip_header *fiph);
void fnic_vlan_discovery_timeout(struct fnic *fnic);

extern struct workqueue_struct *fnic_fip_queue;

#ifdef FNIC_DEBUG
static inline void
fnic_debug_dump_fip_frame(struct fnic *fnic, struct ethhdr *eth,
				int len, char *pfx)
{
	struct fip_header *fiph = (struct fip_header *)(eth + 1);
	u16 op = be16_to_cpu(fiph->fip_op);
	u8 sub = fiph->fip_subcode;

	FNIC_FCS_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		"FIP %s packet contents: op: 0x%x sub: 0x%x (len = %d)",
		pfx, op, sub, len);

	fnic_debug_dump(fnic, (uint8_t *)eth, len);
}

#else /* FNIC_DEBUG */

static inline void
fnic_debug_dump_fip_frame(struct fnic *fnic, struct ethhdr *eth,
				int len, char *pfx) {}
#endif /* FNIC_DEBUG */

#endif	/* _FIP_H_ */
