// SPDX-License-Identifier: (GPL-2.0 OR MIT)

#include <linux/types.h>
#include <linux/kernel.h>

#include "lan966x_vcap_ag_api.h"

/* keyfields */
static const struct vcap_field is1_normal_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_INDEX] = {
		.type = VCAP_FIELD_U32,
		.offset = 1,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 3,
		.width = 9,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_IP_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 14,
		.width = 1,
	},
	[VCAP_KF_8021CB_R_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 16,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_DBL_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 17,
		.width = 1,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 18,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 19,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 31,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 32,
		.width = 3,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 35,
		.width = 48,
	},
	[VCAP_KF_ETYPE_LEN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 83,
		.width = 1,
	},
	[VCAP_KF_ETYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 84,
		.width = 16,
	},
	[VCAP_KF_IP_SNAP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 100,
		.width = 1,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 101,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT] = {
		.type = VCAP_FIELD_BIT,
		.offset = 102,
		.width = 1,
	},
	[VCAP_KF_L3_FRAG_OFS_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 103,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 104,
		.width = 1,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 105,
		.width = 6,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 111,
		.width = 32,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 143,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 144,
		.width = 1,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 145,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 161,
		.width = 8,
	},
};

static const struct vcap_field is1_5tuple_ip4_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_INDEX] = {
		.type = VCAP_FIELD_U32,
		.offset = 1,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 3,
		.width = 9,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_IP_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 14,
		.width = 1,
	},
	[VCAP_KF_8021CB_R_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 16,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_DBL_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 17,
		.width = 1,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 18,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 19,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 31,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 32,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 35,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 36,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 48,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 49,
		.width = 3,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 52,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT] = {
		.type = VCAP_FIELD_BIT,
		.offset = 53,
		.width = 1,
	},
	[VCAP_KF_L3_FRAG_OFS_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 54,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 55,
		.width = 1,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 56,
		.width = 6,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 62,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 94,
		.width = 32,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 126,
		.width = 8,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 134,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 135,
		.width = 1,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 136,
		.width = 8,
	},
	[VCAP_KF_IP_PAYLOAD_5TUPLE] = {
		.type = VCAP_FIELD_U32,
		.offset = 144,
		.width = 32,
	},
};

static const struct vcap_field is1_normal_ip6_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_INDEX] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 9,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 14,
		.width = 1,
	},
	[VCAP_KF_IP_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_8021CB_R_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 16,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 17,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_DBL_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 18,
		.width = 1,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 19,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 32,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 33,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 36,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 37,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 49,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 50,
		.width = 3,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 53,
		.width = 48,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 101,
		.width = 6,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 107,
		.width = 128,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 235,
		.width = 8,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 243,
		.width = 1,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 244,
		.width = 8,
	},
	[VCAP_KF_IP_PAYLOAD_S1_IP6] = {
		.type = VCAP_FIELD_U112,
		.offset = 252,
		.width = 112,
	},
};

static const struct vcap_field is1_7tuple_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_INDEX] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 9,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 14,
		.width = 1,
	},
	[VCAP_KF_IP_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_8021CB_R_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 16,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 17,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_DBL_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 18,
		.width = 1,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 19,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 32,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 33,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 36,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 37,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 49,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 50,
		.width = 3,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 53,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 101,
		.width = 48,
	},
	[VCAP_KF_ETYPE_LEN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 149,
		.width = 1,
	},
	[VCAP_KF_ETYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 150,
		.width = 16,
	},
	[VCAP_KF_IP_SNAP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 166,
		.width = 1,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 167,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT] = {
		.type = VCAP_FIELD_BIT,
		.offset = 168,
		.width = 1,
	},
	[VCAP_KF_L3_FRAG_OFS_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 169,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 170,
		.width = 1,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 171,
		.width = 6,
	},
	[VCAP_KF_L3_IP6_DIP_MSB] = {
		.type = VCAP_FIELD_U32,
		.offset = 177,
		.width = 16,
	},
	[VCAP_KF_L3_IP6_DIP] = {
		.type = VCAP_FIELD_U64,
		.offset = 193,
		.width = 64,
	},
	[VCAP_KF_L3_IP6_SIP_MSB] = {
		.type = VCAP_FIELD_U32,
		.offset = 257,
		.width = 16,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U64,
		.offset = 273,
		.width = 64,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 337,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 338,
		.width = 1,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 339,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 355,
		.width = 8,
	},
};

static const struct vcap_field is1_5tuple_ip6_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_INDEX] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 9,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 14,
		.width = 1,
	},
	[VCAP_KF_IP_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_8021CB_R_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 16,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 17,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_DBL_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 18,
		.width = 1,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 19,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 32,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 33,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 36,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 37,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 49,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 50,
		.width = 3,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 53,
		.width = 6,
	},
	[VCAP_KF_L3_IP6_DIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 59,
		.width = 128,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 187,
		.width = 128,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 315,
		.width = 8,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 323,
		.width = 1,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 324,
		.width = 8,
	},
	[VCAP_KF_IP_PAYLOAD_5TUPLE] = {
		.type = VCAP_FIELD_U32,
		.offset = 332,
		.width = 32,
	},
};

static const struct vcap_field is1_dbl_vid_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_INDEX] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 9,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 14,
		.width = 1,
	},
	[VCAP_KF_IP_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_8021CB_R_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 16,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 17,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_DBL_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 18,
		.width = 1,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 19,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 32,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 33,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 36,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 37,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 49,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 50,
		.width = 3,
	},
	[VCAP_KF_ETYPE_LEN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 53,
		.width = 1,
	},
	[VCAP_KF_ETYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 54,
		.width = 16,
	},
	[VCAP_KF_IP_SNAP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 70,
		.width = 1,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 71,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT] = {
		.type = VCAP_FIELD_BIT,
		.offset = 72,
		.width = 1,
	},
	[VCAP_KF_L3_FRAG_OFS_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 73,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 74,
		.width = 1,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 75,
		.width = 6,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 81,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 82,
		.width = 1,
	},
};

static const struct vcap_field is1_rt_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 2,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 3,
		.width = 3,
	},
	[VCAP_KF_8021CB_R_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 6,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 7,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_DBL_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 8,
		.width = 1,
	},
	[VCAP_KF_L2_MAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 9,
		.width = 48,
	},
	[VCAP_KF_RT_VLAN_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 57,
		.width = 3,
	},
	[VCAP_KF_RT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 60,
		.width = 2,
	},
	[VCAP_KF_RT_FRMID] = {
		.type = VCAP_FIELD_U32,
		.offset = 62,
		.width = 32,
	},
};

static const struct vcap_field is1_dmac_vid_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_INDEX] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 9,
	},
	[VCAP_KF_8021CB_R_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 14,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_DBL_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 16,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 17,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 29,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 30,
		.width = 3,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 33,
		.width = 48,
	},
};

static const struct vcap_field is2_mac_etype_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 4,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 26,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 3,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 43,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 91,
		.width = 48,
	},
	[VCAP_KF_ETYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 139,
		.width = 16,
	},
	[VCAP_KF_L2_FRM_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 155,
		.width = 4,
	},
	[VCAP_KF_L2_PAYLOAD0] = {
		.type = VCAP_FIELD_U32,
		.offset = 159,
		.width = 16,
	},
	[VCAP_KF_L2_PAYLOAD1] = {
		.type = VCAP_FIELD_U32,
		.offset = 175,
		.width = 8,
	},
	[VCAP_KF_L2_PAYLOAD2] = {
		.type = VCAP_FIELD_U32,
		.offset = 183,
		.width = 3,
	},
};

static const struct vcap_field is2_mac_llc_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 4,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 26,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 3,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 43,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 91,
		.width = 48,
	},
	[VCAP_KF_L2_LLC] = {
		.type = VCAP_FIELD_U48,
		.offset = 139,
		.width = 40,
	},
};

static const struct vcap_field is2_mac_snap_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 4,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 26,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 3,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 43,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 91,
		.width = 48,
	},
	[VCAP_KF_L2_SNAP] = {
		.type = VCAP_FIELD_U48,
		.offset = 139,
		.width = 40,
	},
};

static const struct vcap_field is2_arp_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 4,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 26,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 3,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 43,
		.width = 48,
	},
	[VCAP_KF_ARP_ADDR_SPACE_OK_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 91,
		.width = 1,
	},
	[VCAP_KF_ARP_PROTO_SPACE_OK_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 92,
		.width = 1,
	},
	[VCAP_KF_ARP_LEN_OK_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 93,
		.width = 1,
	},
	[VCAP_KF_ARP_TGT_MATCH_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 94,
		.width = 1,
	},
	[VCAP_KF_ARP_SENDER_MATCH_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_KF_ARP_OPCODE_UNKNOWN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 96,
		.width = 1,
	},
	[VCAP_KF_ARP_OPCODE] = {
		.type = VCAP_FIELD_U32,
		.offset = 97,
		.width = 2,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 99,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 131,
		.width = 32,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 163,
		.width = 1,
	},
};

static const struct vcap_field is2_ip4_tcp_udp_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 4,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 26,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 3,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 43,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT] = {
		.type = VCAP_FIELD_BIT,
		.offset = 44,
		.width = 1,
	},
	[VCAP_KF_L3_FRAG_OFS_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 45,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 46,
		.width = 1,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 47,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 48,
		.width = 8,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 56,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 88,
		.width = 32,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 120,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 121,
		.width = 1,
	},
	[VCAP_KF_L4_DPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 122,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 138,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 154,
		.width = 8,
	},
	[VCAP_KF_L4_SPORT_EQ_DPORT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 162,
		.width = 1,
	},
	[VCAP_KF_L4_SEQUENCE_EQ0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 163,
		.width = 1,
	},
	[VCAP_KF_L4_FIN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 164,
		.width = 1,
	},
	[VCAP_KF_L4_SYN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 165,
		.width = 1,
	},
	[VCAP_KF_L4_RST] = {
		.type = VCAP_FIELD_BIT,
		.offset = 166,
		.width = 1,
	},
	[VCAP_KF_L4_PSH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 167,
		.width = 1,
	},
	[VCAP_KF_L4_ACK] = {
		.type = VCAP_FIELD_BIT,
		.offset = 168,
		.width = 1,
	},
	[VCAP_KF_L4_URG] = {
		.type = VCAP_FIELD_BIT,
		.offset = 169,
		.width = 1,
	},
	[VCAP_KF_L4_1588_DOM] = {
		.type = VCAP_FIELD_U32,
		.offset = 170,
		.width = 8,
	},
	[VCAP_KF_L4_1588_VER] = {
		.type = VCAP_FIELD_U32,
		.offset = 178,
		.width = 4,
	},
};

static const struct vcap_field is2_ip4_other_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 4,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 26,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 3,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 43,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT] = {
		.type = VCAP_FIELD_BIT,
		.offset = 44,
		.width = 1,
	},
	[VCAP_KF_L3_FRAG_OFS_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 45,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 46,
		.width = 1,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 47,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 48,
		.width = 8,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 56,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 88,
		.width = 32,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 120,
		.width = 1,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 121,
		.width = 8,
	},
	[VCAP_KF_L3_PAYLOAD] = {
		.type = VCAP_FIELD_U56,
		.offset = 129,
		.width = 56,
	},
};

static const struct vcap_field is2_ip6_std_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 4,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 26,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 3,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 43,
		.width = 1,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 44,
		.width = 128,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 172,
		.width = 8,
	},
};

static const struct vcap_field is2_oam_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 4,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 26,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 3,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 43,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 91,
		.width = 48,
	},
	[VCAP_KF_OAM_MEL_FLAGS] = {
		.type = VCAP_FIELD_U32,
		.offset = 139,
		.width = 7,
	},
	[VCAP_KF_OAM_VER] = {
		.type = VCAP_FIELD_U32,
		.offset = 146,
		.width = 5,
	},
	[VCAP_KF_OAM_OPCODE] = {
		.type = VCAP_FIELD_U32,
		.offset = 151,
		.width = 8,
	},
	[VCAP_KF_OAM_FLAGS] = {
		.type = VCAP_FIELD_U32,
		.offset = 159,
		.width = 8,
	},
	[VCAP_KF_OAM_MEPID] = {
		.type = VCAP_FIELD_U32,
		.offset = 167,
		.width = 16,
	},
	[VCAP_KF_OAM_CCM_CNTS_EQ0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 183,
		.width = 1,
	},
	[VCAP_KF_OAM_Y1731_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 184,
		.width = 1,
	},
	[VCAP_KF_OAM_DETECTED] = {
		.type = VCAP_FIELD_BIT,
		.offset = 185,
		.width = 1,
	},
};

static const struct vcap_field is2_ip6_tcp_udp_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 2,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 3,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 11,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 20,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 21,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 25,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 37,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 38,
		.width = 3,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 41,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 42,
		.width = 8,
	},
	[VCAP_KF_L3_IP6_DIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 50,
		.width = 128,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 178,
		.width = 128,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 306,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 307,
		.width = 1,
	},
	[VCAP_KF_L4_DPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 308,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 324,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 340,
		.width = 8,
	},
	[VCAP_KF_L4_SPORT_EQ_DPORT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 348,
		.width = 1,
	},
	[VCAP_KF_L4_SEQUENCE_EQ0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 349,
		.width = 1,
	},
	[VCAP_KF_L4_FIN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 350,
		.width = 1,
	},
	[VCAP_KF_L4_SYN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 351,
		.width = 1,
	},
	[VCAP_KF_L4_RST] = {
		.type = VCAP_FIELD_BIT,
		.offset = 352,
		.width = 1,
	},
	[VCAP_KF_L4_PSH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 353,
		.width = 1,
	},
	[VCAP_KF_L4_ACK] = {
		.type = VCAP_FIELD_BIT,
		.offset = 354,
		.width = 1,
	},
	[VCAP_KF_L4_URG] = {
		.type = VCAP_FIELD_BIT,
		.offset = 355,
		.width = 1,
	},
	[VCAP_KF_L4_1588_DOM] = {
		.type = VCAP_FIELD_U32,
		.offset = 356,
		.width = 8,
	},
	[VCAP_KF_L4_1588_VER] = {
		.type = VCAP_FIELD_U32,
		.offset = 364,
		.width = 4,
	},
};

static const struct vcap_field is2_ip6_other_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 2,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_PAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 3,
		.width = 8,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 11,
		.width = 9,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 20,
		.width = 1,
	},
	[VCAP_KF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 21,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 22,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 24,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 25,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 37,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 38,
		.width = 3,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 41,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 42,
		.width = 8,
	},
	[VCAP_KF_L3_IP6_DIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 50,
		.width = 128,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 178,
		.width = 128,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 306,
		.width = 1,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 307,
		.width = 8,
	},
	[VCAP_KF_L3_PAYLOAD] = {
		.type = VCAP_FIELD_U56,
		.offset = 315,
		.width = 56,
	},
};

static const struct vcap_field is2_smac_sip4_keyfield[] = {
	[VCAP_KF_IF_IGR_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 4,
		.width = 48,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 52,
		.width = 32,
	},
};

static const struct vcap_field is2_smac_sip6_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_IF_IGR_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 4,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 8,
		.width = 48,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 56,
		.width = 128,
	},
};

static const struct vcap_field es0_vid_keyfield[] = {
	[VCAP_KF_IF_EGR_PORT_NO] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 4,
	},
	[VCAP_KF_IF_IGR_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 4,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 8,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 9,
		.width = 8,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 17,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 18,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 19,
		.width = 12,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 31,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 32,
		.width = 3,
	},
	[VCAP_KF_L3_DPL_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 35,
		.width = 1,
	},
	[VCAP_KF_RTP_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 36,
		.width = 10,
	},
	[VCAP_KF_PDU_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 46,
		.width = 4,
	},
};

/* keyfield_set */
static const struct vcap_set is1_keyfield_set[] = {
	[VCAP_KFS_NORMAL] = {
		.type_id = 0,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_5TUPLE_IP4] = {
		.type_id = 1,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_NORMAL_IP6] = {
		.type_id = 0,
		.sw_per_item = 4,
		.sw_cnt = 1,
	},
	[VCAP_KFS_7TUPLE] = {
		.type_id = 1,
		.sw_per_item = 4,
		.sw_cnt = 1,
	},
	[VCAP_KFS_5TUPLE_IP6] = {
		.type_id = 2,
		.sw_per_item = 4,
		.sw_cnt = 1,
	},
	[VCAP_KFS_DBL_VID] = {
		.type_id = 0,
		.sw_per_item = 1,
		.sw_cnt = 4,
	},
	[VCAP_KFS_RT] = {
		.type_id = 1,
		.sw_per_item = 1,
		.sw_cnt = 4,
	},
	[VCAP_KFS_DMAC_VID] = {
		.type_id = 2,
		.sw_per_item = 1,
		.sw_cnt = 4,
	},
};

static const struct vcap_set is2_keyfield_set[] = {
	[VCAP_KFS_MAC_ETYPE] = {
		.type_id = 0,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_MAC_LLC] = {
		.type_id = 1,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_MAC_SNAP] = {
		.type_id = 2,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_ARP] = {
		.type_id = 3,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP4_TCP_UDP] = {
		.type_id = 4,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP4_OTHER] = {
		.type_id = 5,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP6_STD] = {
		.type_id = 6,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_OAM] = {
		.type_id = 7,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP6_TCP_UDP] = {
		.type_id = 0,
		.sw_per_item = 4,
		.sw_cnt = 1,
	},
	[VCAP_KFS_IP6_OTHER] = {
		.type_id = 1,
		.sw_per_item = 4,
		.sw_cnt = 1,
	},
	[VCAP_KFS_SMAC_SIP4] = {
		.type_id = -1,
		.sw_per_item = 1,
		.sw_cnt = 4,
	},
	[VCAP_KFS_SMAC_SIP6] = {
		.type_id = 8,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
};

static const struct vcap_set es0_keyfield_set[] = {
	[VCAP_KFS_VID] = {
		.type_id = -1,
		.sw_per_item = 1,
		.sw_cnt = 1,
	},
};

/* keyfield_set map */
static const struct vcap_field *is1_keyfield_set_map[] = {
	[VCAP_KFS_NORMAL] = is1_normal_keyfield,
	[VCAP_KFS_5TUPLE_IP4] = is1_5tuple_ip4_keyfield,
	[VCAP_KFS_NORMAL_IP6] = is1_normal_ip6_keyfield,
	[VCAP_KFS_7TUPLE] = is1_7tuple_keyfield,
	[VCAP_KFS_5TUPLE_IP6] = is1_5tuple_ip6_keyfield,
	[VCAP_KFS_DBL_VID] = is1_dbl_vid_keyfield,
	[VCAP_KFS_RT] = is1_rt_keyfield,
	[VCAP_KFS_DMAC_VID] = is1_dmac_vid_keyfield,
};

static const struct vcap_field *is2_keyfield_set_map[] = {
	[VCAP_KFS_MAC_ETYPE] = is2_mac_etype_keyfield,
	[VCAP_KFS_MAC_LLC] = is2_mac_llc_keyfield,
	[VCAP_KFS_MAC_SNAP] = is2_mac_snap_keyfield,
	[VCAP_KFS_ARP] = is2_arp_keyfield,
	[VCAP_KFS_IP4_TCP_UDP] = is2_ip4_tcp_udp_keyfield,
	[VCAP_KFS_IP4_OTHER] = is2_ip4_other_keyfield,
	[VCAP_KFS_IP6_STD] = is2_ip6_std_keyfield,
	[VCAP_KFS_OAM] = is2_oam_keyfield,
	[VCAP_KFS_IP6_TCP_UDP] = is2_ip6_tcp_udp_keyfield,
	[VCAP_KFS_IP6_OTHER] = is2_ip6_other_keyfield,
	[VCAP_KFS_SMAC_SIP4] = is2_smac_sip4_keyfield,
	[VCAP_KFS_SMAC_SIP6] = is2_smac_sip6_keyfield,
};

static const struct vcap_field *es0_keyfield_set_map[] = {
	[VCAP_KFS_VID] = es0_vid_keyfield,
};

/* keyfield_set map sizes */
static int is1_keyfield_set_map_size[] = {
	[VCAP_KFS_NORMAL] = ARRAY_SIZE(is1_normal_keyfield),
	[VCAP_KFS_5TUPLE_IP4] = ARRAY_SIZE(is1_5tuple_ip4_keyfield),
	[VCAP_KFS_NORMAL_IP6] = ARRAY_SIZE(is1_normal_ip6_keyfield),
	[VCAP_KFS_7TUPLE] = ARRAY_SIZE(is1_7tuple_keyfield),
	[VCAP_KFS_5TUPLE_IP6] = ARRAY_SIZE(is1_5tuple_ip6_keyfield),
	[VCAP_KFS_DBL_VID] = ARRAY_SIZE(is1_dbl_vid_keyfield),
	[VCAP_KFS_RT] = ARRAY_SIZE(is1_rt_keyfield),
	[VCAP_KFS_DMAC_VID] = ARRAY_SIZE(is1_dmac_vid_keyfield),
};

static int is2_keyfield_set_map_size[] = {
	[VCAP_KFS_MAC_ETYPE] = ARRAY_SIZE(is2_mac_etype_keyfield),
	[VCAP_KFS_MAC_LLC] = ARRAY_SIZE(is2_mac_llc_keyfield),
	[VCAP_KFS_MAC_SNAP] = ARRAY_SIZE(is2_mac_snap_keyfield),
	[VCAP_KFS_ARP] = ARRAY_SIZE(is2_arp_keyfield),
	[VCAP_KFS_IP4_TCP_UDP] = ARRAY_SIZE(is2_ip4_tcp_udp_keyfield),
	[VCAP_KFS_IP4_OTHER] = ARRAY_SIZE(is2_ip4_other_keyfield),
	[VCAP_KFS_IP6_STD] = ARRAY_SIZE(is2_ip6_std_keyfield),
	[VCAP_KFS_OAM] = ARRAY_SIZE(is2_oam_keyfield),
	[VCAP_KFS_IP6_TCP_UDP] = ARRAY_SIZE(is2_ip6_tcp_udp_keyfield),
	[VCAP_KFS_IP6_OTHER] = ARRAY_SIZE(is2_ip6_other_keyfield),
	[VCAP_KFS_SMAC_SIP4] = ARRAY_SIZE(is2_smac_sip4_keyfield),
	[VCAP_KFS_SMAC_SIP6] = ARRAY_SIZE(is2_smac_sip6_keyfield),
};

static int es0_keyfield_set_map_size[] = {
	[VCAP_KFS_VID] = ARRAY_SIZE(es0_vid_keyfield),
};

/* actionfields */
static const struct vcap_field is1_s1_actionfield[] = {
	[VCAP_AF_TYPE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_AF_DSCP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 1,
		.width = 1,
	},
	[VCAP_AF_DSCP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 6,
	},
	[VCAP_AF_QOS_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 8,
		.width = 1,
	},
	[VCAP_AF_QOS_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 9,
		.width = 3,
	},
	[VCAP_AF_DP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
	},
	[VCAP_AF_DP_VAL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_AF_PAG_OVERRIDE_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 14,
		.width = 8,
	},
	[VCAP_AF_PAG_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 22,
		.width = 8,
	},
	[VCAP_AF_ISDX_REPLACE_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 30,
		.width = 1,
	},
	[VCAP_AF_ISDX_ADD_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 31,
		.width = 8,
	},
	[VCAP_AF_VID_REPLACE_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_AF_VID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 12,
	},
	[VCAP_AF_PCP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 67,
		.width = 1,
	},
	[VCAP_AF_PCP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 68,
		.width = 3,
	},
	[VCAP_AF_DEI_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 71,
		.width = 1,
	},
	[VCAP_AF_DEI_VAL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 72,
		.width = 1,
	},
	[VCAP_AF_VLAN_POP_CNT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 73,
		.width = 1,
	},
	[VCAP_AF_VLAN_POP_CNT] = {
		.type = VCAP_FIELD_U32,
		.offset = 74,
		.width = 2,
	},
	[VCAP_AF_CUSTOM_ACE_TYPE_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 76,
		.width = 4,
	},
	[VCAP_AF_SFID_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 80,
		.width = 1,
	},
	[VCAP_AF_SFID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 81,
		.width = 8,
	},
	[VCAP_AF_SGID_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 89,
		.width = 1,
	},
	[VCAP_AF_SGID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 90,
		.width = 8,
	},
	[VCAP_AF_POLICE_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 98,
		.width = 1,
	},
	[VCAP_AF_POLICE_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 99,
		.width = 9,
	},
	[VCAP_AF_OAM_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 108,
		.width = 3,
	},
	[VCAP_AF_MRP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 111,
		.width = 2,
	},
	[VCAP_AF_DLR_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 113,
		.width = 2,
	},
};

static const struct vcap_field is2_base_type_actionfield[] = {
	[VCAP_AF_HIT_ME_ONCE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_AF_CPU_COPY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 1,
		.width = 1,
	},
	[VCAP_AF_CPU_QUEUE_NUM] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 3,
	},
	[VCAP_AF_MASK_MODE] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 2,
	},
	[VCAP_AF_MIRROR_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 7,
		.width = 1,
	},
	[VCAP_AF_LRN_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 8,
		.width = 1,
	},
	[VCAP_AF_POLICE_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 9,
		.width = 1,
	},
	[VCAP_AF_POLICE_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 10,
		.width = 9,
	},
	[VCAP_AF_POLICE_VCAP_ONLY] = {
		.type = VCAP_FIELD_BIT,
		.offset = 19,
		.width = 1,
	},
	[VCAP_AF_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 8,
	},
	[VCAP_AF_REW_OP] = {
		.type = VCAP_FIELD_U32,
		.offset = 28,
		.width = 16,
	},
	[VCAP_AF_ISDX_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 44,
		.width = 1,
	},
	[VCAP_AF_ACL_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 45,
		.width = 6,
	},
};

static const struct vcap_field is2_smac_sip_actionfield[] = {
	[VCAP_AF_CPU_COPY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_AF_CPU_QUEUE_NUM] = {
		.type = VCAP_FIELD_U32,
		.offset = 1,
		.width = 3,
	},
	[VCAP_AF_FWD_KILL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 4,
		.width = 1,
	},
	[VCAP_AF_HOST_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 5,
		.width = 1,
	},
};

static const struct vcap_field es0_vid_actionfield[] = {
	[VCAP_AF_PUSH_OUTER_TAG] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 2,
	},
	[VCAP_AF_PUSH_INNER_TAG] = {
		.type = VCAP_FIELD_BIT,
		.offset = 2,
		.width = 1,
	},
	[VCAP_AF_TAG_A_TPID_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 3,
		.width = 2,
	},
	[VCAP_AF_TAG_A_VID_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 5,
		.width = 1,
	},
	[VCAP_AF_TAG_A_PCP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 6,
		.width = 2,
	},
	[VCAP_AF_TAG_A_DEI_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 8,
		.width = 2,
	},
	[VCAP_AF_TAG_B_TPID_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 10,
		.width = 2,
	},
	[VCAP_AF_TAG_B_VID_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
	},
	[VCAP_AF_TAG_B_PCP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 2,
	},
	[VCAP_AF_TAG_B_DEI_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 15,
		.width = 2,
	},
	[VCAP_AF_VID_A_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 17,
		.width = 12,
	},
	[VCAP_AF_PCP_A_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 29,
		.width = 3,
	},
	[VCAP_AF_DEI_A_VAL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 32,
		.width = 1,
	},
	[VCAP_AF_VID_B_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 33,
		.width = 12,
	},
	[VCAP_AF_PCP_B_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 45,
		.width = 3,
	},
	[VCAP_AF_DEI_B_VAL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 48,
		.width = 1,
	},
	[VCAP_AF_ESDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 49,
		.width = 8,
	},
};

/* actionfield_set */
static const struct vcap_set is1_actionfield_set[] = {
	[VCAP_AFS_S1] = {
		.type_id = 0,
		.sw_per_item = 1,
		.sw_cnt = 4,
	},
};

static const struct vcap_set is2_actionfield_set[] = {
	[VCAP_AFS_BASE_TYPE] = {
		.type_id = -1,
		.sw_per_item = 2,
		.sw_cnt = 2,
	},
	[VCAP_AFS_SMAC_SIP] = {
		.type_id = -1,
		.sw_per_item = 1,
		.sw_cnt = 4,
	},
};

static const struct vcap_set es0_actionfield_set[] = {
	[VCAP_AFS_VID] = {
		.type_id = -1,
		.sw_per_item = 1,
		.sw_cnt = 1,
	},
};

/* actionfield_set map */
static const struct vcap_field *is1_actionfield_set_map[] = {
	[VCAP_AFS_S1] = is1_s1_actionfield,
};

static const struct vcap_field *is2_actionfield_set_map[] = {
	[VCAP_AFS_BASE_TYPE] = is2_base_type_actionfield,
	[VCAP_AFS_SMAC_SIP] = is2_smac_sip_actionfield,
};

static const struct vcap_field *es0_actionfield_set_map[] = {
	[VCAP_AFS_VID] = es0_vid_actionfield,
};

/* actionfield_set map size */
static int is1_actionfield_set_map_size[] = {
	[VCAP_AFS_S1] = ARRAY_SIZE(is1_s1_actionfield),
};

static int is2_actionfield_set_map_size[] = {
	[VCAP_AFS_BASE_TYPE] = ARRAY_SIZE(is2_base_type_actionfield),
	[VCAP_AFS_SMAC_SIP] = ARRAY_SIZE(is2_smac_sip_actionfield),
};

static int es0_actionfield_set_map_size[] = {
	[VCAP_AFS_VID] = ARRAY_SIZE(es0_vid_actionfield),
};

/* Type Groups */
static const struct vcap_typegroup is1_x4_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 3,
		.value = 4,
	},
	{
		.offset = 96,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 192,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 288,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is1_x2_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 2,
		.value = 2,
	},
	{
		.offset = 96,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is1_x1_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 1,
		.value = 1,
	},
	{}
};

static const struct vcap_typegroup is2_x4_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 3,
		.value = 4,
	},
	{
		.offset = 96,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 192,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 288,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is2_x2_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 2,
		.value = 2,
	},
	{
		.offset = 96,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is2_x1_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 1,
		.value = 1,
	},
	{}
};

static const struct vcap_typegroup es0_x1_keyfield_set_typegroups[] = {
	{}
};

static const struct vcap_typegroup *is1_keyfield_set_typegroups[] = {
	[4] = is1_x4_keyfield_set_typegroups,
	[2] = is1_x2_keyfield_set_typegroups,
	[1] = is1_x1_keyfield_set_typegroups,
	[5] = NULL,
};

static const struct vcap_typegroup *is2_keyfield_set_typegroups[] = {
	[4] = is2_x4_keyfield_set_typegroups,
	[2] = is2_x2_keyfield_set_typegroups,
	[1] = is2_x1_keyfield_set_typegroups,
	[5] = NULL,
};

static const struct vcap_typegroup *es0_keyfield_set_typegroups[] = {
	[1] = es0_x1_keyfield_set_typegroups,
	[2] = NULL,
};

static const struct vcap_typegroup is1_x1_actionfield_set_typegroups[] = {
	{}
};

static const struct vcap_typegroup is2_x2_actionfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 2,
		.value = 2,
	},
	{
		.offset = 31,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is2_x1_actionfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 1,
		.value = 1,
	},
	{}
};

static const struct vcap_typegroup es0_x1_actionfield_set_typegroups[] = {
	{}
};

static const struct vcap_typegroup *is1_actionfield_set_typegroups[] = {
	[1] = is1_x1_actionfield_set_typegroups,
	[5] = NULL,
};

static const struct vcap_typegroup *is2_actionfield_set_typegroups[] = {
	[2] = is2_x2_actionfield_set_typegroups,
	[1] = is2_x1_actionfield_set_typegroups,
	[5] = NULL,
};

static const struct vcap_typegroup *es0_actionfield_set_typegroups[] = {
	[1] = es0_x1_actionfield_set_typegroups,
	[2] = NULL,
};

/* Keyfieldset names */
static const char * const vcap_keyfield_set_names[] = {
	[VCAP_KFS_NO_VALUE]                      =  "(None)",
	[VCAP_KFS_5TUPLE_IP4]                    =  "VCAP_KFS_5TUPLE_IP4",
	[VCAP_KFS_5TUPLE_IP6]                    =  "VCAP_KFS_5TUPLE_IP6",
	[VCAP_KFS_7TUPLE]                        =  "VCAP_KFS_7TUPLE",
	[VCAP_KFS_ARP]                           =  "VCAP_KFS_ARP",
	[VCAP_KFS_DBL_VID]                       =  "VCAP_KFS_DBL_VID",
	[VCAP_KFS_DMAC_VID]                      =  "VCAP_KFS_DMAC_VID",
	[VCAP_KFS_ETAG]                          =  "VCAP_KFS_ETAG",
	[VCAP_KFS_IP4_OTHER]                     =  "VCAP_KFS_IP4_OTHER",
	[VCAP_KFS_IP4_TCP_UDP]                   =  "VCAP_KFS_IP4_TCP_UDP",
	[VCAP_KFS_IP4_VID]                       =  "VCAP_KFS_IP4_VID",
	[VCAP_KFS_IP6_OTHER]                     =  "VCAP_KFS_IP6_OTHER",
	[VCAP_KFS_IP6_STD]                       =  "VCAP_KFS_IP6_STD",
	[VCAP_KFS_IP6_TCP_UDP]                   =  "VCAP_KFS_IP6_TCP_UDP",
	[VCAP_KFS_IP6_VID]                       =  "VCAP_KFS_IP6_VID",
	[VCAP_KFS_IP_7TUPLE]                     =  "VCAP_KFS_IP_7TUPLE",
	[VCAP_KFS_ISDX]                          =  "VCAP_KFS_ISDX",
	[VCAP_KFS_LL_FULL]                       =  "VCAP_KFS_LL_FULL",
	[VCAP_KFS_MAC_ETYPE]                     =  "VCAP_KFS_MAC_ETYPE",
	[VCAP_KFS_MAC_LLC]                       =  "VCAP_KFS_MAC_LLC",
	[VCAP_KFS_MAC_SNAP]                      =  "VCAP_KFS_MAC_SNAP",
	[VCAP_KFS_NORMAL]                        =  "VCAP_KFS_NORMAL",
	[VCAP_KFS_NORMAL_5TUPLE_IP4]             =  "VCAP_KFS_NORMAL_5TUPLE_IP4",
	[VCAP_KFS_NORMAL_7TUPLE]                 =  "VCAP_KFS_NORMAL_7TUPLE",
	[VCAP_KFS_NORMAL_IP6]                    =  "VCAP_KFS_NORMAL_IP6",
	[VCAP_KFS_OAM]                           =  "VCAP_KFS_OAM",
	[VCAP_KFS_PURE_5TUPLE_IP4]               =  "VCAP_KFS_PURE_5TUPLE_IP4",
	[VCAP_KFS_RT]                            =  "VCAP_KFS_RT",
	[VCAP_KFS_SMAC_SIP4]                     =  "VCAP_KFS_SMAC_SIP4",
	[VCAP_KFS_SMAC_SIP6]                     =  "VCAP_KFS_SMAC_SIP6",
	[VCAP_KFS_VID]                           =  "VCAP_KFS_VID",
};

/* Actionfieldset names */
static const char * const vcap_actionfield_set_names[] = {
	[VCAP_AFS_NO_VALUE]                      =  "(None)",
	[VCAP_AFS_BASE_TYPE]                     =  "VCAP_AFS_BASE_TYPE",
	[VCAP_AFS_CLASSIFICATION]                =  "VCAP_AFS_CLASSIFICATION",
	[VCAP_AFS_CLASS_REDUCED]                 =  "VCAP_AFS_CLASS_REDUCED",
	[VCAP_AFS_ES0]                           =  "VCAP_AFS_ES0",
	[VCAP_AFS_FULL]                          =  "VCAP_AFS_FULL",
	[VCAP_AFS_S1]                            =  "VCAP_AFS_S1",
	[VCAP_AFS_SMAC_SIP]                      =  "VCAP_AFS_SMAC_SIP",
	[VCAP_AFS_VID]                           =  "VCAP_AFS_VID",
};

/* Keyfield names */
static const char * const vcap_keyfield_names[] = {
	[VCAP_KF_NO_VALUE]                       =  "(None)",
	[VCAP_KF_8021BR_ECID_BASE]               =  "8021BR_ECID_BASE",
	[VCAP_KF_8021BR_ECID_EXT]                =  "8021BR_ECID_EXT",
	[VCAP_KF_8021BR_E_TAGGED]                =  "8021BR_E_TAGGED",
	[VCAP_KF_8021BR_GRP]                     =  "8021BR_GRP",
	[VCAP_KF_8021BR_IGR_ECID_BASE]           =  "8021BR_IGR_ECID_BASE",
	[VCAP_KF_8021BR_IGR_ECID_EXT]            =  "8021BR_IGR_ECID_EXT",
	[VCAP_KF_8021CB_R_TAGGED_IS]             =  "8021CB_R_TAGGED_IS",
	[VCAP_KF_8021Q_DEI0]                     =  "8021Q_DEI0",
	[VCAP_KF_8021Q_DEI1]                     =  "8021Q_DEI1",
	[VCAP_KF_8021Q_DEI2]                     =  "8021Q_DEI2",
	[VCAP_KF_8021Q_DEI_CLS]                  =  "8021Q_DEI_CLS",
	[VCAP_KF_8021Q_PCP0]                     =  "8021Q_PCP0",
	[VCAP_KF_8021Q_PCP1]                     =  "8021Q_PCP1",
	[VCAP_KF_8021Q_PCP2]                     =  "8021Q_PCP2",
	[VCAP_KF_8021Q_PCP_CLS]                  =  "8021Q_PCP_CLS",
	[VCAP_KF_8021Q_TPID]                     =  "8021Q_TPID",
	[VCAP_KF_8021Q_TPID0]                    =  "8021Q_TPID0",
	[VCAP_KF_8021Q_TPID1]                    =  "8021Q_TPID1",
	[VCAP_KF_8021Q_TPID2]                    =  "8021Q_TPID2",
	[VCAP_KF_8021Q_VID0]                     =  "8021Q_VID0",
	[VCAP_KF_8021Q_VID1]                     =  "8021Q_VID1",
	[VCAP_KF_8021Q_VID2]                     =  "8021Q_VID2",
	[VCAP_KF_8021Q_VID_CLS]                  =  "8021Q_VID_CLS",
	[VCAP_KF_8021Q_VLAN_DBL_TAGGED_IS]       =  "8021Q_VLAN_DBL_TAGGED_IS",
	[VCAP_KF_8021Q_VLAN_TAGGED_IS]           =  "8021Q_VLAN_TAGGED_IS",
	[VCAP_KF_8021Q_VLAN_TAGS]                =  "8021Q_VLAN_TAGS",
	[VCAP_KF_ACL_GRP_ID]                     =  "ACL_GRP_ID",
	[VCAP_KF_ARP_ADDR_SPACE_OK_IS]           =  "ARP_ADDR_SPACE_OK_IS",
	[VCAP_KF_ARP_LEN_OK_IS]                  =  "ARP_LEN_OK_IS",
	[VCAP_KF_ARP_OPCODE]                     =  "ARP_OPCODE",
	[VCAP_KF_ARP_OPCODE_UNKNOWN_IS]          =  "ARP_OPCODE_UNKNOWN_IS",
	[VCAP_KF_ARP_PROTO_SPACE_OK_IS]          =  "ARP_PROTO_SPACE_OK_IS",
	[VCAP_KF_ARP_SENDER_MATCH_IS]            =  "ARP_SENDER_MATCH_IS",
	[VCAP_KF_ARP_TGT_MATCH_IS]               =  "ARP_TGT_MATCH_IS",
	[VCAP_KF_COSID_CLS]                      =  "COSID_CLS",
	[VCAP_KF_ES0_ISDX_KEY_ENA]               =  "ES0_ISDX_KEY_ENA",
	[VCAP_KF_ETYPE]                          =  "ETYPE",
	[VCAP_KF_ETYPE_LEN_IS]                   =  "ETYPE_LEN_IS",
	[VCAP_KF_HOST_MATCH]                     =  "HOST_MATCH",
	[VCAP_KF_IF_EGR_PORT_MASK]               =  "IF_EGR_PORT_MASK",
	[VCAP_KF_IF_EGR_PORT_MASK_RNG]           =  "IF_EGR_PORT_MASK_RNG",
	[VCAP_KF_IF_EGR_PORT_NO]                 =  "IF_EGR_PORT_NO",
	[VCAP_KF_IF_IGR_PORT]                    =  "IF_IGR_PORT",
	[VCAP_KF_IF_IGR_PORT_MASK]               =  "IF_IGR_PORT_MASK",
	[VCAP_KF_IF_IGR_PORT_MASK_L3]            =  "IF_IGR_PORT_MASK_L3",
	[VCAP_KF_IF_IGR_PORT_MASK_RNG]           =  "IF_IGR_PORT_MASK_RNG",
	[VCAP_KF_IF_IGR_PORT_MASK_SEL]           =  "IF_IGR_PORT_MASK_SEL",
	[VCAP_KF_IF_IGR_PORT_SEL]                =  "IF_IGR_PORT_SEL",
	[VCAP_KF_IP4_IS]                         =  "IP4_IS",
	[VCAP_KF_IP_MC_IS]                       =  "IP_MC_IS",
	[VCAP_KF_IP_PAYLOAD_5TUPLE]              =  "IP_PAYLOAD_5TUPLE",
	[VCAP_KF_IP_PAYLOAD_S1_IP6]              =  "IP_PAYLOAD_S1_IP6",
	[VCAP_KF_IP_SNAP_IS]                     =  "IP_SNAP_IS",
	[VCAP_KF_ISDX_CLS]                       =  "ISDX_CLS",
	[VCAP_KF_ISDX_GT0_IS]                    =  "ISDX_GT0_IS",
	[VCAP_KF_L2_BC_IS]                       =  "L2_BC_IS",
	[VCAP_KF_L2_DMAC]                        =  "L2_DMAC",
	[VCAP_KF_L2_FRM_TYPE]                    =  "L2_FRM_TYPE",
	[VCAP_KF_L2_FWD_IS]                      =  "L2_FWD_IS",
	[VCAP_KF_L2_LLC]                         =  "L2_LLC",
	[VCAP_KF_L2_MAC]                         =  "L2_MAC",
	[VCAP_KF_L2_MC_IS]                       =  "L2_MC_IS",
	[VCAP_KF_L2_PAYLOAD0]                    =  "L2_PAYLOAD0",
	[VCAP_KF_L2_PAYLOAD1]                    =  "L2_PAYLOAD1",
	[VCAP_KF_L2_PAYLOAD2]                    =  "L2_PAYLOAD2",
	[VCAP_KF_L2_PAYLOAD_ETYPE]               =  "L2_PAYLOAD_ETYPE",
	[VCAP_KF_L2_SMAC]                        =  "L2_SMAC",
	[VCAP_KF_L2_SNAP]                        =  "L2_SNAP",
	[VCAP_KF_L3_DIP_EQ_SIP_IS]               =  "L3_DIP_EQ_SIP_IS",
	[VCAP_KF_L3_DPL_CLS]                     =  "L3_DPL_CLS",
	[VCAP_KF_L3_DSCP]                        =  "L3_DSCP",
	[VCAP_KF_L3_DST_IS]                      =  "L3_DST_IS",
	[VCAP_KF_L3_FRAGMENT]                    =  "L3_FRAGMENT",
	[VCAP_KF_L3_FRAGMENT_TYPE]               =  "L3_FRAGMENT_TYPE",
	[VCAP_KF_L3_FRAG_INVLD_L4_LEN]           =  "L3_FRAG_INVLD_L4_LEN",
	[VCAP_KF_L3_FRAG_OFS_GT0]                =  "L3_FRAG_OFS_GT0",
	[VCAP_KF_L3_IP4_DIP]                     =  "L3_IP4_DIP",
	[VCAP_KF_L3_IP4_SIP]                     =  "L3_IP4_SIP",
	[VCAP_KF_L3_IP6_DIP]                     =  "L3_IP6_DIP",
	[VCAP_KF_L3_IP6_DIP_MSB]                 =  "L3_IP6_DIP_MSB",
	[VCAP_KF_L3_IP6_SIP]                     =  "L3_IP6_SIP",
	[VCAP_KF_L3_IP6_SIP_MSB]                 =  "L3_IP6_SIP_MSB",
	[VCAP_KF_L3_IP_PROTO]                    =  "L3_IP_PROTO",
	[VCAP_KF_L3_OPTIONS_IS]                  =  "L3_OPTIONS_IS",
	[VCAP_KF_L3_PAYLOAD]                     =  "L3_PAYLOAD",
	[VCAP_KF_L3_RT_IS]                       =  "L3_RT_IS",
	[VCAP_KF_L3_TOS]                         =  "L3_TOS",
	[VCAP_KF_L3_TTL_GT0]                     =  "L3_TTL_GT0",
	[VCAP_KF_L4_1588_DOM]                    =  "L4_1588_DOM",
	[VCAP_KF_L4_1588_VER]                    =  "L4_1588_VER",
	[VCAP_KF_L4_ACK]                         =  "L4_ACK",
	[VCAP_KF_L4_DPORT]                       =  "L4_DPORT",
	[VCAP_KF_L4_FIN]                         =  "L4_FIN",
	[VCAP_KF_L4_PAYLOAD]                     =  "L4_PAYLOAD",
	[VCAP_KF_L4_PSH]                         =  "L4_PSH",
	[VCAP_KF_L4_RNG]                         =  "L4_RNG",
	[VCAP_KF_L4_RST]                         =  "L4_RST",
	[VCAP_KF_L4_SEQUENCE_EQ0_IS]             =  "L4_SEQUENCE_EQ0_IS",
	[VCAP_KF_L4_SPORT]                       =  "L4_SPORT",
	[VCAP_KF_L4_SPORT_EQ_DPORT_IS]           =  "L4_SPORT_EQ_DPORT_IS",
	[VCAP_KF_L4_SYN]                         =  "L4_SYN",
	[VCAP_KF_L4_URG]                         =  "L4_URG",
	[VCAP_KF_LOOKUP_FIRST_IS]                =  "LOOKUP_FIRST_IS",
	[VCAP_KF_LOOKUP_GEN_IDX]                 =  "LOOKUP_GEN_IDX",
	[VCAP_KF_LOOKUP_GEN_IDX_SEL]             =  "LOOKUP_GEN_IDX_SEL",
	[VCAP_KF_LOOKUP_INDEX]                   =  "LOOKUP_INDEX",
	[VCAP_KF_LOOKUP_PAG]                     =  "LOOKUP_PAG",
	[VCAP_KF_MIRROR_PROBE]                   =  "MIRROR_PROBE",
	[VCAP_KF_OAM_CCM_CNTS_EQ0]               =  "OAM_CCM_CNTS_EQ0",
	[VCAP_KF_OAM_DETECTED]                   =  "OAM_DETECTED",
	[VCAP_KF_OAM_FLAGS]                      =  "OAM_FLAGS",
	[VCAP_KF_OAM_MEL_FLAGS]                  =  "OAM_MEL_FLAGS",
	[VCAP_KF_OAM_MEPID]                      =  "OAM_MEPID",
	[VCAP_KF_OAM_OPCODE]                     =  "OAM_OPCODE",
	[VCAP_KF_OAM_VER]                        =  "OAM_VER",
	[VCAP_KF_OAM_Y1731_IS]                   =  "OAM_Y1731_IS",
	[VCAP_KF_PDU_TYPE]                       =  "PDU_TYPE",
	[VCAP_KF_PROT_ACTIVE]                    =  "PROT_ACTIVE",
	[VCAP_KF_RTP_ID]                         =  "RTP_ID",
	[VCAP_KF_RT_FRMID]                       =  "RT_FRMID",
	[VCAP_KF_RT_TYPE]                        =  "RT_TYPE",
	[VCAP_KF_RT_VLAN_IDX]                    =  "RT_VLAN_IDX",
	[VCAP_KF_TCP_IS]                         =  "TCP_IS",
	[VCAP_KF_TCP_UDP_IS]                     =  "TCP_UDP_IS",
	[VCAP_KF_TYPE]                           =  "TYPE",
};

/* Actionfield names */
static const char * const vcap_actionfield_names[] = {
	[VCAP_AF_NO_VALUE]                       =  "(None)",
	[VCAP_AF_ACL_ID]                         =  "ACL_ID",
	[VCAP_AF_CLS_VID_SEL]                    =  "CLS_VID_SEL",
	[VCAP_AF_CNT_ID]                         =  "CNT_ID",
	[VCAP_AF_COPY_PORT_NUM]                  =  "COPY_PORT_NUM",
	[VCAP_AF_COPY_QUEUE_NUM]                 =  "COPY_QUEUE_NUM",
	[VCAP_AF_CPU_COPY_ENA]                   =  "CPU_COPY_ENA",
	[VCAP_AF_CPU_QU]                         =  "CPU_QU",
	[VCAP_AF_CPU_QUEUE_NUM]                  =  "CPU_QUEUE_NUM",
	[VCAP_AF_CUSTOM_ACE_TYPE_ENA]            =  "CUSTOM_ACE_TYPE_ENA",
	[VCAP_AF_DEI_A_VAL]                      =  "DEI_A_VAL",
	[VCAP_AF_DEI_B_VAL]                      =  "DEI_B_VAL",
	[VCAP_AF_DEI_C_VAL]                      =  "DEI_C_VAL",
	[VCAP_AF_DEI_ENA]                        =  "DEI_ENA",
	[VCAP_AF_DEI_VAL]                        =  "DEI_VAL",
	[VCAP_AF_DLR_SEL]                        =  "DLR_SEL",
	[VCAP_AF_DP_ENA]                         =  "DP_ENA",
	[VCAP_AF_DP_VAL]                         =  "DP_VAL",
	[VCAP_AF_DSCP_ENA]                       =  "DSCP_ENA",
	[VCAP_AF_DSCP_SEL]                       =  "DSCP_SEL",
	[VCAP_AF_DSCP_VAL]                       =  "DSCP_VAL",
	[VCAP_AF_ES2_REW_CMD]                    =  "ES2_REW_CMD",
	[VCAP_AF_ESDX]                           =  "ESDX",
	[VCAP_AF_FWD_KILL_ENA]                   =  "FWD_KILL_ENA",
	[VCAP_AF_FWD_MODE]                       =  "FWD_MODE",
	[VCAP_AF_FWD_SEL]                        =  "FWD_SEL",
	[VCAP_AF_HIT_ME_ONCE]                    =  "HIT_ME_ONCE",
	[VCAP_AF_HOST_MATCH]                     =  "HOST_MATCH",
	[VCAP_AF_IGNORE_PIPELINE_CTRL]           =  "IGNORE_PIPELINE_CTRL",
	[VCAP_AF_INTR_ENA]                       =  "INTR_ENA",
	[VCAP_AF_ISDX_ADD_REPLACE_SEL]           =  "ISDX_ADD_REPLACE_SEL",
	[VCAP_AF_ISDX_ADD_VAL]                   =  "ISDX_ADD_VAL",
	[VCAP_AF_ISDX_ENA]                       =  "ISDX_ENA",
	[VCAP_AF_ISDX_REPLACE_ENA]               =  "ISDX_REPLACE_ENA",
	[VCAP_AF_ISDX_VAL]                       =  "ISDX_VAL",
	[VCAP_AF_LOOP_ENA]                       =  "LOOP_ENA",
	[VCAP_AF_LRN_DIS]                        =  "LRN_DIS",
	[VCAP_AF_MAP_IDX]                        =  "MAP_IDX",
	[VCAP_AF_MAP_KEY]                        =  "MAP_KEY",
	[VCAP_AF_MAP_LOOKUP_SEL]                 =  "MAP_LOOKUP_SEL",
	[VCAP_AF_MASK_MODE]                      =  "MASK_MODE",
	[VCAP_AF_MATCH_ID]                       =  "MATCH_ID",
	[VCAP_AF_MATCH_ID_MASK]                  =  "MATCH_ID_MASK",
	[VCAP_AF_MIRROR_ENA]                     =  "MIRROR_ENA",
	[VCAP_AF_MIRROR_PROBE]                   =  "MIRROR_PROBE",
	[VCAP_AF_MIRROR_PROBE_ID]                =  "MIRROR_PROBE_ID",
	[VCAP_AF_MRP_SEL]                        =  "MRP_SEL",
	[VCAP_AF_NXT_IDX]                        =  "NXT_IDX",
	[VCAP_AF_NXT_IDX_CTRL]                   =  "NXT_IDX_CTRL",
	[VCAP_AF_OAM_SEL]                        =  "OAM_SEL",
	[VCAP_AF_PAG_OVERRIDE_MASK]              =  "PAG_OVERRIDE_MASK",
	[VCAP_AF_PAG_VAL]                        =  "PAG_VAL",
	[VCAP_AF_PCP_A_VAL]                      =  "PCP_A_VAL",
	[VCAP_AF_PCP_B_VAL]                      =  "PCP_B_VAL",
	[VCAP_AF_PCP_C_VAL]                      =  "PCP_C_VAL",
	[VCAP_AF_PCP_ENA]                        =  "PCP_ENA",
	[VCAP_AF_PCP_VAL]                        =  "PCP_VAL",
	[VCAP_AF_PIPELINE_ACT]                   =  "PIPELINE_ACT",
	[VCAP_AF_PIPELINE_FORCE_ENA]             =  "PIPELINE_FORCE_ENA",
	[VCAP_AF_PIPELINE_PT]                    =  "PIPELINE_PT",
	[VCAP_AF_POLICE_ENA]                     =  "POLICE_ENA",
	[VCAP_AF_POLICE_IDX]                     =  "POLICE_IDX",
	[VCAP_AF_POLICE_REMARK]                  =  "POLICE_REMARK",
	[VCAP_AF_POLICE_VCAP_ONLY]               =  "POLICE_VCAP_ONLY",
	[VCAP_AF_POP_VAL]                        =  "POP_VAL",
	[VCAP_AF_PORT_MASK]                      =  "PORT_MASK",
	[VCAP_AF_PUSH_CUSTOMER_TAG]              =  "PUSH_CUSTOMER_TAG",
	[VCAP_AF_PUSH_INNER_TAG]                 =  "PUSH_INNER_TAG",
	[VCAP_AF_PUSH_OUTER_TAG]                 =  "PUSH_OUTER_TAG",
	[VCAP_AF_QOS_ENA]                        =  "QOS_ENA",
	[VCAP_AF_QOS_VAL]                        =  "QOS_VAL",
	[VCAP_AF_REW_OP]                         =  "REW_OP",
	[VCAP_AF_RT_DIS]                         =  "RT_DIS",
	[VCAP_AF_SFID_ENA]                       =  "SFID_ENA",
	[VCAP_AF_SFID_VAL]                       =  "SFID_VAL",
	[VCAP_AF_SGID_ENA]                       =  "SGID_ENA",
	[VCAP_AF_SGID_VAL]                       =  "SGID_VAL",
	[VCAP_AF_SWAP_MACS_ENA]                  =  "SWAP_MACS_ENA",
	[VCAP_AF_TAG_A_DEI_SEL]                  =  "TAG_A_DEI_SEL",
	[VCAP_AF_TAG_A_PCP_SEL]                  =  "TAG_A_PCP_SEL",
	[VCAP_AF_TAG_A_TPID_SEL]                 =  "TAG_A_TPID_SEL",
	[VCAP_AF_TAG_A_VID_SEL]                  =  "TAG_A_VID_SEL",
	[VCAP_AF_TAG_B_DEI_SEL]                  =  "TAG_B_DEI_SEL",
	[VCAP_AF_TAG_B_PCP_SEL]                  =  "TAG_B_PCP_SEL",
	[VCAP_AF_TAG_B_TPID_SEL]                 =  "TAG_B_TPID_SEL",
	[VCAP_AF_TAG_B_VID_SEL]                  =  "TAG_B_VID_SEL",
	[VCAP_AF_TAG_C_DEI_SEL]                  =  "TAG_C_DEI_SEL",
	[VCAP_AF_TAG_C_PCP_SEL]                  =  "TAG_C_PCP_SEL",
	[VCAP_AF_TAG_C_TPID_SEL]                 =  "TAG_C_TPID_SEL",
	[VCAP_AF_TAG_C_VID_SEL]                  =  "TAG_C_VID_SEL",
	[VCAP_AF_TYPE]                           =  "TYPE",
	[VCAP_AF_UNTAG_VID_ENA]                  =  "UNTAG_VID_ENA",
	[VCAP_AF_VID_A_VAL]                      =  "VID_A_VAL",
	[VCAP_AF_VID_B_VAL]                      =  "VID_B_VAL",
	[VCAP_AF_VID_C_VAL]                      =  "VID_C_VAL",
	[VCAP_AF_VID_REPLACE_ENA]                =  "VID_REPLACE_ENA",
	[VCAP_AF_VID_VAL]                        =  "VID_VAL",
	[VCAP_AF_VLAN_POP_CNT]                   =  "VLAN_POP_CNT",
	[VCAP_AF_VLAN_POP_CNT_ENA]               =  "VLAN_POP_CNT_ENA",
};

/* VCAPs */
const struct vcap_info lan966x_vcaps[] = {
	[VCAP_TYPE_IS1] = {
		.name = "is1",
		.rows = 192,
		.sw_count = 4,
		.sw_width = 96,
		.sticky_width = 32,
		.act_width = 123,
		.default_cnt = 0,
		.require_cnt_dis = 1,
		.version = 1,
		.keyfield_set = is1_keyfield_set,
		.keyfield_set_size = ARRAY_SIZE(is1_keyfield_set),
		.actionfield_set = is1_actionfield_set,
		.actionfield_set_size = ARRAY_SIZE(is1_actionfield_set),
		.keyfield_set_map = is1_keyfield_set_map,
		.keyfield_set_map_size = is1_keyfield_set_map_size,
		.actionfield_set_map = is1_actionfield_set_map,
		.actionfield_set_map_size = is1_actionfield_set_map_size,
		.keyfield_set_typegroups = is1_keyfield_set_typegroups,
		.actionfield_set_typegroups = is1_actionfield_set_typegroups,
	},
	[VCAP_TYPE_IS2] = {
		.name = "is2",
		.rows = 64,
		.sw_count = 4,
		.sw_width = 96,
		.sticky_width = 32,
		.act_width = 31,
		.default_cnt = 11,
		.require_cnt_dis = 1,
		.version = 1,
		.keyfield_set = is2_keyfield_set,
		.keyfield_set_size = ARRAY_SIZE(is2_keyfield_set),
		.actionfield_set = is2_actionfield_set,
		.actionfield_set_size = ARRAY_SIZE(is2_actionfield_set),
		.keyfield_set_map = is2_keyfield_set_map,
		.keyfield_set_map_size = is2_keyfield_set_map_size,
		.actionfield_set_map = is2_actionfield_set_map,
		.actionfield_set_map_size = is2_actionfield_set_map_size,
		.keyfield_set_typegroups = is2_keyfield_set_typegroups,
		.actionfield_set_typegroups = is2_actionfield_set_typegroups,
	},
	[VCAP_TYPE_ES0] = {
		.name = "es0",
		.rows = 256,
		.sw_count = 1,
		.sw_width = 96,
		.sticky_width = 1,
		.act_width = 65,
		.default_cnt = 8,
		.require_cnt_dis = 0,
		.version = 1,
		.keyfield_set = es0_keyfield_set,
		.keyfield_set_size = ARRAY_SIZE(es0_keyfield_set),
		.actionfield_set = es0_actionfield_set,
		.actionfield_set_size = ARRAY_SIZE(es0_actionfield_set),
		.keyfield_set_map = es0_keyfield_set_map,
		.keyfield_set_map_size = es0_keyfield_set_map_size,
		.actionfield_set_map = es0_actionfield_set_map,
		.actionfield_set_map_size = es0_actionfield_set_map_size,
		.keyfield_set_typegroups = es0_keyfield_set_typegroups,
		.actionfield_set_typegroups = es0_actionfield_set_typegroups,
	},
};

const struct vcap_statistics lan966x_vcap_stats = {
	.name = "lan966x",
	.count = 3,
	.keyfield_set_names = vcap_keyfield_set_names,
	.actionfield_set_names = vcap_actionfield_set_names,
	.keyfield_names = vcap_keyfield_names,
	.actionfield_names = vcap_actionfield_names,
};
