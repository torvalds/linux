/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#ifndef _FNIC_FIP_H_
#define _FNIC_FIP_H_


#define FCOE_CTLR_START_DELAY    2000    /* ms after first adv. to choose FCF */
#define FCOE_CTLR_FIPVLAN_TOV    2000    /* ms after FIP VLAN disc */
#define FCOE_CTLR_MAX_SOL        8

#define FINC_MAX_FLOGI_REJECTS   8

struct vlan {
	__be16 vid;
	__be16 type;
};

/*
 * VLAN entry.
 */
struct fcoe_vlan {
	struct list_head list;
	u16 vid;		/* vlan ID */
	u16 sol_count;		/* no. of sols sent */
	u16 state;		/* state */
};

enum fip_vlan_state {
	FIP_VLAN_AVAIL  = 0,	/* don't do anything */
	FIP_VLAN_SENT   = 1,	/* sent */
	FIP_VLAN_USED   = 2,	/* succeed */
	FIP_VLAN_FAILED = 3,	/* failed to response */
};

struct fip_vlan {
	struct ethhdr eth;
	struct fip_header fip;
	struct {
		struct fip_mac_desc mac;
		struct fip_wwn_desc wwnn;
	} desc;
};

#endif  /* __FINC_FIP_H_ */
