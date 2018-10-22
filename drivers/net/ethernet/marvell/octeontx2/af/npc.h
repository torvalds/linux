/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NPC_H
#define NPC_H

enum NPC_LID_E {
	NPC_LID_LA = 0,
	NPC_LID_LB,
	NPC_LID_LC,
	NPC_LID_LD,
	NPC_LID_LE,
	NPC_LID_LF,
	NPC_LID_LG,
	NPC_LID_LH,
};

#define NPC_LT_NA 0

enum npc_kpu_la_ltype {
	NPC_LT_LA_8023 = 1,
	NPC_LT_LA_ETHER,
};

enum npc_kpu_lb_ltype {
	NPC_LT_LB_ETAG = 1,
	NPC_LT_LB_CTAG,
	NPC_LT_LB_STAG,
	NPC_LT_LB_BTAG,
	NPC_LT_LB_QINQ,
	NPC_LT_LB_ITAG,
};

enum npc_kpu_lc_ltype {
	NPC_LT_LC_IP = 1,
	NPC_LT_LC_IP6,
	NPC_LT_LC_ARP,
	NPC_LT_LC_RARP,
	NPC_LT_LC_MPLS,
	NPC_LT_LC_NSH,
	NPC_LT_LC_PTP,
	NPC_LT_LC_FCOE,
};

/* Don't modify Ltypes upto SCTP, otherwise it will
 * effect flow tag calculation and thus RSS.
 */
enum npc_kpu_ld_ltype {
	NPC_LT_LD_TCP = 1,
	NPC_LT_LD_UDP,
	NPC_LT_LD_ICMP,
	NPC_LT_LD_SCTP,
	NPC_LT_LD_IGMP,
	NPC_LT_LD_ICMP6,
	NPC_LT_LD_ESP,
	NPC_LT_LD_AH,
	NPC_LT_LD_GRE,
	NPC_LT_LD_GRE_MPLS,
	NPC_LT_LD_GRE_NSH,
	NPC_LT_LD_TU_MPLS,
};

enum npc_kpu_le_ltype {
	NPC_LT_LE_TU_ETHER = 1,
	NPC_LT_LE_TU_PPP,
	NPC_LT_LE_TU_MPLS_IN_NSH,
	NPC_LT_LE_TU_3RD_NSH,
};

enum npc_kpu_lf_ltype {
	NPC_LT_LF_TU_IP = 1,
	NPC_LT_LF_TU_IP6,
	NPC_LT_LF_TU_ARP,
	NPC_LT_LF_TU_MPLS_IP,
	NPC_LT_LF_TU_MPLS_IP6,
	NPC_LT_LF_TU_MPLS_ETHER,
};

enum npc_kpu_lg_ltype {
	NPC_LT_LG_TU_TCP = 1,
	NPC_LT_LG_TU_UDP,
	NPC_LT_LG_TU_SCTP,
	NPC_LT_LG_TU_ICMP,
	NPC_LT_LG_TU_IGMP,
	NPC_LT_LG_TU_ICMP6,
	NPC_LT_LG_TU_ESP,
	NPC_LT_LG_TU_AH,
};

enum npc_kpu_lh_ltype {
	NPC_LT_LH_TCP_DATA = 1,
	NPC_LT_LH_HTTP_DATA,
	NPC_LT_LH_HTTPS_DATA,
	NPC_LT_LH_PPTP_DATA,
	NPC_LT_LH_UDP_DATA,
};

struct npc_kpu_profile_cam {
	u8 state;
	u8 state_mask;
	u16 dp0;
	u16 dp0_mask;
	u16 dp1;
	u16 dp1_mask;
	u16 dp2;
	u16 dp2_mask;
};

struct npc_kpu_profile_action {
	u8 errlev;
	u8 errcode;
	u8 dp0_offset;
	u8 dp1_offset;
	u8 dp2_offset;
	u8 bypass_count;
	u8 parse_done;
	u8 next_state;
	u8 ptr_advance;
	u8 cap_ena;
	u8 lid;
	u8 ltype;
	u8 flags;
	u8 offset;
	u8 mask;
	u8 right;
	u8 shift;
};

struct npc_kpu_profile {
	int cam_entries;
	int action_entries;
	struct npc_kpu_profile_cam *cam;
	struct npc_kpu_profile_action *action;
};

#endif /* NPC_H */
