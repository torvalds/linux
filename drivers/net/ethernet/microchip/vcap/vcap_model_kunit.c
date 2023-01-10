// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (C) 2022 Microchip Technology Inc. and its subsidiaries.
 * Microchip VCAP API Test VCAP Model Data
 */

#include <linux/types.h>
#include <linux/kernel.h>

#include "vcap_api.h"
#include "vcap_model_kunit.h"

/* keyfields */
static const struct vcap_field is0_mll_keyfield[] = {
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
		.width = 7,
	},
	[VCAP_KF_8021Q_VLAN_TAGS] = {
		.type = VCAP_FIELD_U32,
		.offset = 10,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 3,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 28,
		.width = 3,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 31,
		.width = 12,
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
	[VCAP_KF_ETYPE_MPLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 139,
		.width = 2,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 141,
		.width = 8,
	},
};

static const struct vcap_field is0_tri_vid_keyfield[] = {
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
		.width = 7,
	},
	[VCAP_KF_LOOKUP_GEN_IDX_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 10,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_GEN_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 12,
		.width = 12,
	},
	[VCAP_KF_8021Q_VLAN_TAGS] = {
		.type = VCAP_FIELD_U32,
		.offset = 24,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 30,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 33,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 34,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 46,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 49,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 52,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 53,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 65,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP2] = {
		.type = VCAP_FIELD_U32,
		.offset = 68,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI2] = {
		.type = VCAP_FIELD_BIT,
		.offset = 71,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 72,
		.width = 12,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 84,
		.width = 8,
	},
	[VCAP_KF_OAM_Y1731_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 92,
		.width = 1,
	},
	[VCAP_KF_OAM_MEL_FLAGS] = {
		.type = VCAP_FIELD_U32,
		.offset = 93,
		.width = 7,
	},
};

static const struct vcap_field is0_ll_full_keyfield[] = {
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
		.width = 7,
	},
	[VCAP_KF_8021Q_VLAN_TAGS] = {
		.type = VCAP_FIELD_U32,
		.offset = 10,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 19,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 32,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 35,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 38,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 39,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 51,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP2] = {
		.type = VCAP_FIELD_U32,
		.offset = 54,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI2] = {
		.type = VCAP_FIELD_BIT,
		.offset = 57,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 58,
		.width = 12,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 70,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 118,
		.width = 48,
	},
	[VCAP_KF_ETYPE_LEN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 166,
		.width = 1,
	},
	[VCAP_KF_ETYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 167,
		.width = 16,
	},
	[VCAP_KF_IP_SNAP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 183,
		.width = 1,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 184,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 185,
		.width = 2,
	},
	[VCAP_KF_L3_FRAG_INVLD_L4_LEN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 187,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 188,
		.width = 1,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 189,
		.width = 6,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 195,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 227,
		.width = 32,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 259,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 260,
		.width = 1,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 261,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 277,
		.width = 8,
	},
};

static const struct vcap_field is0_normal_keyfield[] = {
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
	[VCAP_KF_LOOKUP_GEN_IDX_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 3,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_GEN_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 12,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 17,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U72,
		.offset = 19,
		.width = 65,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 84,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 85,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGS] = {
		.type = VCAP_FIELD_U32,
		.offset = 86,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 89,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 92,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 96,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 108,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 111,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 114,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 115,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 127,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP2] = {
		.type = VCAP_FIELD_U32,
		.offset = 130,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI2] = {
		.type = VCAP_FIELD_BIT,
		.offset = 133,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 134,
		.width = 12,
	},
	[VCAP_KF_DST_ENTRY] = {
		.type = VCAP_FIELD_BIT,
		.offset = 146,
		.width = 1,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 147,
		.width = 48,
	},
	[VCAP_KF_IP_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 195,
		.width = 1,
	},
	[VCAP_KF_ETYPE_LEN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 196,
		.width = 1,
	},
	[VCAP_KF_ETYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 197,
		.width = 16,
	},
	[VCAP_KF_IP_SNAP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 213,
		.width = 1,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 214,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 215,
		.width = 2,
	},
	[VCAP_KF_L3_FRAG_INVLD_L4_LEN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 217,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 218,
		.width = 1,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 219,
		.width = 6,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 225,
		.width = 32,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 257,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 258,
		.width = 1,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 259,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 275,
		.width = 8,
	},
};

static const struct vcap_field is0_normal_7tuple_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 1,
		.width = 1,
	},
	[VCAP_KF_LOOKUP_GEN_IDX_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_GEN_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 12,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U72,
		.offset = 18,
		.width = 65,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 83,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 84,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGS] = {
		.type = VCAP_FIELD_U32,
		.offset = 85,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 88,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 91,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 94,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 95,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 107,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 110,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 113,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 114,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 126,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP2] = {
		.type = VCAP_FIELD_U32,
		.offset = 129,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI2] = {
		.type = VCAP_FIELD_BIT,
		.offset = 132,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 133,
		.width = 12,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 145,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 193,
		.width = 48,
	},
	[VCAP_KF_IP_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 241,
		.width = 1,
	},
	[VCAP_KF_ETYPE_LEN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 242,
		.width = 1,
	},
	[VCAP_KF_ETYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 243,
		.width = 16,
	},
	[VCAP_KF_IP_SNAP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 259,
		.width = 1,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 260,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 261,
		.width = 2,
	},
	[VCAP_KF_L3_FRAG_INVLD_L4_LEN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 263,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 264,
		.width = 1,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 265,
		.width = 6,
	},
	[VCAP_KF_L3_IP6_DIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 271,
		.width = 128,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 399,
		.width = 128,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 527,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 528,
		.width = 1,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 529,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 545,
		.width = 8,
	},
};

static const struct vcap_field is0_normal_5tuple_ip4_keyfield[] = {
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
	[VCAP_KF_LOOKUP_GEN_IDX_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 3,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_GEN_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 12,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 17,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U72,
		.offset = 19,
		.width = 65,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 84,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 85,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGS] = {
		.type = VCAP_FIELD_U32,
		.offset = 86,
		.width = 3,
	},
	[VCAP_KF_8021Q_TPID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 89,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP0] = {
		.type = VCAP_FIELD_U32,
		.offset = 92,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID0] = {
		.type = VCAP_FIELD_U32,
		.offset = 96,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 108,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP1] = {
		.type = VCAP_FIELD_U32,
		.offset = 111,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI1] = {
		.type = VCAP_FIELD_BIT,
		.offset = 114,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID1] = {
		.type = VCAP_FIELD_U32,
		.offset = 115,
		.width = 12,
	},
	[VCAP_KF_8021Q_TPID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 127,
		.width = 3,
	},
	[VCAP_KF_8021Q_PCP2] = {
		.type = VCAP_FIELD_U32,
		.offset = 130,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI2] = {
		.type = VCAP_FIELD_BIT,
		.offset = 133,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID2] = {
		.type = VCAP_FIELD_U32,
		.offset = 134,
		.width = 12,
	},
	[VCAP_KF_IP_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 146,
		.width = 1,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 147,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 148,
		.width = 2,
	},
	[VCAP_KF_L3_FRAG_INVLD_L4_LEN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 150,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 151,
		.width = 1,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 152,
		.width = 6,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 158,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 190,
		.width = 32,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 222,
		.width = 8,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 230,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 231,
		.width = 1,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 232,
		.width = 8,
	},
	[VCAP_KF_IP_PAYLOAD_5TUPLE] = {
		.type = VCAP_FIELD_U32,
		.offset = 240,
		.width = 32,
	},
};

static const struct vcap_field is0_pure_5tuple_ip4_keyfield[] = {
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
	[VCAP_KF_LOOKUP_GEN_IDX_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 3,
		.width = 2,
	},
	[VCAP_KF_LOOKUP_GEN_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 5,
		.width = 12,
	},
	[VCAP_KF_L3_FRAGMENT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 17,
		.width = 2,
	},
	[VCAP_KF_L3_FRAG_INVLD_L4_LEN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 19,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 20,
		.width = 1,
	},
	[VCAP_KF_L3_DSCP] = {
		.type = VCAP_FIELD_U32,
		.offset = 21,
		.width = 6,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 59,
		.width = 32,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 91,
		.width = 8,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 99,
		.width = 8,
	},
	[VCAP_KF_IP_PAYLOAD_5TUPLE] = {
		.type = VCAP_FIELD_U32,
		.offset = 107,
		.width = 32,
	},
};

static const struct vcap_field is0_etag_keyfield[] = {
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
		.width = 7,
	},
	[VCAP_KF_8021BR_E_TAGGED] = {
		.type = VCAP_FIELD_BIT,
		.offset = 10,
		.width = 1,
	},
	[VCAP_KF_8021BR_GRP] = {
		.type = VCAP_FIELD_U32,
		.offset = 11,
		.width = 2,
	},
	[VCAP_KF_8021BR_ECID_EXT] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 8,
	},
	[VCAP_KF_8021BR_ECID_BASE] = {
		.type = VCAP_FIELD_U32,
		.offset = 21,
		.width = 12,
	},
	[VCAP_KF_8021BR_IGR_ECID_EXT] = {
		.type = VCAP_FIELD_U32,
		.offset = 33,
		.width = 8,
	},
	[VCAP_KF_8021BR_IGR_ECID_BASE] = {
		.type = VCAP_FIELD_U32,
		.offset = 41,
		.width = 12,
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
	[VCAP_KF_IF_IGR_PORT_MASK_L3] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 14,
		.width = 4,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 18,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 32,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 52,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 53,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 54,
		.width = 1,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 55,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 56,
		.width = 12,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 68,
		.width = 13,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 81,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 82,
		.width = 3,
	},
	[VCAP_KF_L2_FWD_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 85,
		.width = 1,
	},
	[VCAP_KF_L3_SMAC_SIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 86,
		.width = 1,
	},
	[VCAP_KF_L3_DMAC_DIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 87,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 88,
		.width = 1,
	},
	[VCAP_KF_L3_DST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 89,
		.width = 1,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 90,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 138,
		.width = 48,
	},
	[VCAP_KF_ETYPE_LEN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 186,
		.width = 1,
	},
	[VCAP_KF_ETYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 187,
		.width = 16,
	},
	[VCAP_KF_L2_PAYLOAD_ETYPE] = {
		.type = VCAP_FIELD_U64,
		.offset = 203,
		.width = 64,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 267,
		.width = 16,
	},
	[VCAP_KF_OAM_CCM_CNTS_EQ0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 283,
		.width = 1,
	},
	[VCAP_KF_OAM_Y1731_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 284,
		.width = 1,
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
	[VCAP_KF_IF_IGR_PORT_MASK_L3] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 14,
		.width = 4,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 18,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 32,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 52,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 53,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 54,
		.width = 1,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 55,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 56,
		.width = 12,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 68,
		.width = 13,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 81,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 82,
		.width = 3,
	},
	[VCAP_KF_L2_FWD_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 85,
		.width = 1,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 86,
		.width = 48,
	},
	[VCAP_KF_ARP_ADDR_SPACE_OK_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 134,
		.width = 1,
	},
	[VCAP_KF_ARP_PROTO_SPACE_OK_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 135,
		.width = 1,
	},
	[VCAP_KF_ARP_LEN_OK_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 136,
		.width = 1,
	},
	[VCAP_KF_ARP_TGT_MATCH_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 137,
		.width = 1,
	},
	[VCAP_KF_ARP_SENDER_MATCH_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 138,
		.width = 1,
	},
	[VCAP_KF_ARP_OPCODE_UNKNOWN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 139,
		.width = 1,
	},
	[VCAP_KF_ARP_OPCODE] = {
		.type = VCAP_FIELD_U32,
		.offset = 140,
		.width = 2,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 142,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 174,
		.width = 32,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 206,
		.width = 1,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 207,
		.width = 16,
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
	[VCAP_KF_IF_IGR_PORT_MASK_L3] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 14,
		.width = 4,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 18,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 32,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 52,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 53,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 54,
		.width = 1,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 55,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 56,
		.width = 12,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 68,
		.width = 13,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 81,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 82,
		.width = 3,
	},
	[VCAP_KF_L2_FWD_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 85,
		.width = 1,
	},
	[VCAP_KF_L3_SMAC_SIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 86,
		.width = 1,
	},
	[VCAP_KF_L3_DMAC_DIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 87,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 88,
		.width = 1,
	},
	[VCAP_KF_L3_DST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 89,
		.width = 1,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 90,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 91,
		.width = 2,
	},
	[VCAP_KF_L3_FRAG_INVLD_L4_LEN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 93,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 94,
		.width = 1,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 96,
		.width = 8,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 104,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 136,
		.width = 32,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 168,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 169,
		.width = 1,
	},
	[VCAP_KF_L4_DPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 170,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 186,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 202,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT_EQ_DPORT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 218,
		.width = 1,
	},
	[VCAP_KF_L4_SEQUENCE_EQ0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 219,
		.width = 1,
	},
	[VCAP_KF_L4_FIN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 220,
		.width = 1,
	},
	[VCAP_KF_L4_SYN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 221,
		.width = 1,
	},
	[VCAP_KF_L4_RST] = {
		.type = VCAP_FIELD_BIT,
		.offset = 222,
		.width = 1,
	},
	[VCAP_KF_L4_PSH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 223,
		.width = 1,
	},
	[VCAP_KF_L4_ACK] = {
		.type = VCAP_FIELD_BIT,
		.offset = 224,
		.width = 1,
	},
	[VCAP_KF_L4_URG] = {
		.type = VCAP_FIELD_BIT,
		.offset = 225,
		.width = 1,
	},
	[VCAP_KF_L4_PAYLOAD] = {
		.type = VCAP_FIELD_U64,
		.offset = 226,
		.width = 64,
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
	[VCAP_KF_IF_IGR_PORT_MASK_L3] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 14,
		.width = 4,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 18,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 32,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 52,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 53,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 54,
		.width = 1,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 55,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 56,
		.width = 12,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 68,
		.width = 13,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 81,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 82,
		.width = 3,
	},
	[VCAP_KF_L2_FWD_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 85,
		.width = 1,
	},
	[VCAP_KF_L3_SMAC_SIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 86,
		.width = 1,
	},
	[VCAP_KF_L3_DMAC_DIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 87,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 88,
		.width = 1,
	},
	[VCAP_KF_L3_DST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 89,
		.width = 1,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 90,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 91,
		.width = 2,
	},
	[VCAP_KF_L3_FRAG_INVLD_L4_LEN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 93,
		.width = 1,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 94,
		.width = 1,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 96,
		.width = 8,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 104,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 136,
		.width = 32,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 168,
		.width = 1,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 169,
		.width = 8,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 177,
		.width = 16,
	},
	[VCAP_KF_L3_PAYLOAD] = {
		.type = VCAP_FIELD_U112,
		.offset = 193,
		.width = 96,
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
	[VCAP_KF_IF_IGR_PORT_MASK_L3] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 14,
		.width = 4,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 18,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 32,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 52,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 53,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 54,
		.width = 1,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 55,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 56,
		.width = 12,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 68,
		.width = 13,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 81,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 82,
		.width = 3,
	},
	[VCAP_KF_L2_FWD_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 85,
		.width = 1,
	},
	[VCAP_KF_L3_SMAC_SIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 86,
		.width = 1,
	},
	[VCAP_KF_L3_DMAC_DIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 87,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 88,
		.width = 1,
	},
	[VCAP_KF_L3_DST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 89,
		.width = 1,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 90,
		.width = 1,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 91,
		.width = 128,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 219,
		.width = 1,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 220,
		.width = 8,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 228,
		.width = 16,
	},
	[VCAP_KF_L3_PAYLOAD] = {
		.type = VCAP_FIELD_U48,
		.offset = 244,
		.width = 40,
	},
};

static const struct vcap_field is2_ip_7tuple_keyfield[] = {
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
	[VCAP_KF_IF_IGR_PORT_MASK_L3] = {
		.type = VCAP_FIELD_BIT,
		.offset = 11,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 12,
		.width = 4,
	},
	[VCAP_KF_IF_IGR_PORT_MASK_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 2,
	},
	[VCAP_KF_IF_IGR_PORT_MASK] = {
		.type = VCAP_FIELD_U72,
		.offset = 18,
		.width = 65,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 83,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 84,
		.width = 1,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 85,
		.width = 1,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 86,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 87,
		.width = 12,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 99,
		.width = 13,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 112,
		.width = 1,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 113,
		.width = 3,
	},
	[VCAP_KF_L2_FWD_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 116,
		.width = 1,
	},
	[VCAP_KF_L3_SMAC_SIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 117,
		.width = 1,
	},
	[VCAP_KF_L3_DMAC_DIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 118,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 119,
		.width = 1,
	},
	[VCAP_KF_L3_DST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 120,
		.width = 1,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 121,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 169,
		.width = 48,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 217,
		.width = 1,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 218,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 219,
		.width = 8,
	},
	[VCAP_KF_L3_IP6_DIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 227,
		.width = 128,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 355,
		.width = 128,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 483,
		.width = 1,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 484,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 485,
		.width = 1,
	},
	[VCAP_KF_L4_DPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 486,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 502,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 518,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT_EQ_DPORT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 534,
		.width = 1,
	},
	[VCAP_KF_L4_SEQUENCE_EQ0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 535,
		.width = 1,
	},
	[VCAP_KF_L4_FIN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 536,
		.width = 1,
	},
	[VCAP_KF_L4_SYN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 537,
		.width = 1,
	},
	[VCAP_KF_L4_RST] = {
		.type = VCAP_FIELD_BIT,
		.offset = 538,
		.width = 1,
	},
	[VCAP_KF_L4_PSH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 539,
		.width = 1,
	},
	[VCAP_KF_L4_ACK] = {
		.type = VCAP_FIELD_BIT,
		.offset = 540,
		.width = 1,
	},
	[VCAP_KF_L4_URG] = {
		.type = VCAP_FIELD_BIT,
		.offset = 541,
		.width = 1,
	},
	[VCAP_KF_L4_PAYLOAD] = {
		.type = VCAP_FIELD_U64,
		.offset = 542,
		.width = 64,
	},
};

static const struct vcap_field is2_ip6_vid_keyfield[] = {
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
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 14,
		.width = 12,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 26,
		.width = 13,
	},
	[VCAP_KF_L3_SMAC_SIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_KF_L3_DMAC_DIP_MATCH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 40,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 41,
		.width = 1,
	},
	[VCAP_KF_L3_DST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 42,
		.width = 1,
	},
	[VCAP_KF_L3_IP6_DIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 43,
		.width = 128,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 171,
		.width = 128,
	},
};

static const struct vcap_field es2_mac_etype_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 3,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 3,
		.width = 1,
	},
	[VCAP_KF_ACL_GRP_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 8,
	},
	[VCAP_KF_PROT_ACTIVE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
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
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 12,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 28,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 29,
		.width = 13,
	},
	[VCAP_KF_IF_EGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 42,
		.width = 3,
	},
	[VCAP_KF_IF_EGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 45,
		.width = 32,
	},
	[VCAP_KF_IF_IGR_PORT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 77,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 78,
		.width = 9,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 87,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 90,
		.width = 1,
	},
	[VCAP_KF_COSID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 91,
		.width = 3,
	},
	[VCAP_KF_L3_DPL_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 94,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_KF_ES0_ISDX_KEY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 96,
		.width = 1,
	},
	[VCAP_KF_MIRROR_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 97,
		.width = 2,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 99,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 147,
		.width = 48,
	},
	[VCAP_KF_ETYPE_LEN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 195,
		.width = 1,
	},
	[VCAP_KF_ETYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 196,
		.width = 16,
	},
	[VCAP_KF_L2_PAYLOAD_ETYPE] = {
		.type = VCAP_FIELD_U64,
		.offset = 212,
		.width = 64,
	},
	[VCAP_KF_OAM_CCM_CNTS_EQ0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 276,
		.width = 1,
	},
	[VCAP_KF_OAM_Y1731_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 277,
		.width = 1,
	},
};

static const struct vcap_field es2_arp_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 3,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 3,
		.width = 1,
	},
	[VCAP_KF_ACL_GRP_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 8,
	},
	[VCAP_KF_PROT_ACTIVE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
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
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 12,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 28,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 29,
		.width = 13,
	},
	[VCAP_KF_IF_EGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 42,
		.width = 3,
	},
	[VCAP_KF_IF_EGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 45,
		.width = 32,
	},
	[VCAP_KF_IF_IGR_PORT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 77,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 78,
		.width = 9,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 87,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 90,
		.width = 1,
	},
	[VCAP_KF_COSID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 91,
		.width = 3,
	},
	[VCAP_KF_L3_DPL_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 94,
		.width = 1,
	},
	[VCAP_KF_ES0_ISDX_KEY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_KF_MIRROR_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 96,
		.width = 2,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 98,
		.width = 48,
	},
	[VCAP_KF_ARP_ADDR_SPACE_OK_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 146,
		.width = 1,
	},
	[VCAP_KF_ARP_PROTO_SPACE_OK_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 147,
		.width = 1,
	},
	[VCAP_KF_ARP_LEN_OK_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 148,
		.width = 1,
	},
	[VCAP_KF_ARP_TGT_MATCH_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 149,
		.width = 1,
	},
	[VCAP_KF_ARP_SENDER_MATCH_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 150,
		.width = 1,
	},
	[VCAP_KF_ARP_OPCODE_UNKNOWN_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 151,
		.width = 1,
	},
	[VCAP_KF_ARP_OPCODE] = {
		.type = VCAP_FIELD_U32,
		.offset = 152,
		.width = 2,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 154,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 186,
		.width = 32,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 218,
		.width = 1,
	},
};

static const struct vcap_field es2_ip4_tcp_udp_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 3,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 3,
		.width = 1,
	},
	[VCAP_KF_ACL_GRP_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 8,
	},
	[VCAP_KF_PROT_ACTIVE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
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
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 12,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 28,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 29,
		.width = 13,
	},
	[VCAP_KF_IF_EGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 42,
		.width = 3,
	},
	[VCAP_KF_IF_EGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 45,
		.width = 32,
	},
	[VCAP_KF_IF_IGR_PORT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 77,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 78,
		.width = 9,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 87,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 90,
		.width = 1,
	},
	[VCAP_KF_COSID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 91,
		.width = 3,
	},
	[VCAP_KF_L3_DPL_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 94,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_KF_ES0_ISDX_KEY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 96,
		.width = 1,
	},
	[VCAP_KF_MIRROR_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 97,
		.width = 2,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 99,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 100,
		.width = 2,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 102,
		.width = 1,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 103,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 104,
		.width = 8,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 112,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 144,
		.width = 32,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 176,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 177,
		.width = 1,
	},
	[VCAP_KF_L4_DPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 178,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 194,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 210,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT_EQ_DPORT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 226,
		.width = 1,
	},
	[VCAP_KF_L4_SEQUENCE_EQ0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 227,
		.width = 1,
	},
	[VCAP_KF_L4_FIN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 228,
		.width = 1,
	},
	[VCAP_KF_L4_SYN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 229,
		.width = 1,
	},
	[VCAP_KF_L4_RST] = {
		.type = VCAP_FIELD_BIT,
		.offset = 230,
		.width = 1,
	},
	[VCAP_KF_L4_PSH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 231,
		.width = 1,
	},
	[VCAP_KF_L4_ACK] = {
		.type = VCAP_FIELD_BIT,
		.offset = 232,
		.width = 1,
	},
	[VCAP_KF_L4_URG] = {
		.type = VCAP_FIELD_BIT,
		.offset = 233,
		.width = 1,
	},
	[VCAP_KF_L4_PAYLOAD] = {
		.type = VCAP_FIELD_U64,
		.offset = 234,
		.width = 64,
	},
};

static const struct vcap_field es2_ip4_other_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 3,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 3,
		.width = 1,
	},
	[VCAP_KF_ACL_GRP_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 8,
	},
	[VCAP_KF_PROT_ACTIVE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
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
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 12,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 28,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 29,
		.width = 13,
	},
	[VCAP_KF_IF_EGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 42,
		.width = 3,
	},
	[VCAP_KF_IF_EGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 45,
		.width = 32,
	},
	[VCAP_KF_IF_IGR_PORT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 77,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 78,
		.width = 9,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 87,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 90,
		.width = 1,
	},
	[VCAP_KF_COSID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 91,
		.width = 3,
	},
	[VCAP_KF_L3_DPL_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 94,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_KF_ES0_ISDX_KEY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 96,
		.width = 1,
	},
	[VCAP_KF_MIRROR_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 97,
		.width = 2,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 99,
		.width = 1,
	},
	[VCAP_KF_L3_FRAGMENT_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 100,
		.width = 2,
	},
	[VCAP_KF_L3_OPTIONS_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 102,
		.width = 1,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 103,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 104,
		.width = 8,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 112,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 144,
		.width = 32,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 176,
		.width = 1,
	},
	[VCAP_KF_L3_IP_PROTO] = {
		.type = VCAP_FIELD_U32,
		.offset = 177,
		.width = 8,
	},
	[VCAP_KF_L3_PAYLOAD] = {
		.type = VCAP_FIELD_U112,
		.offset = 185,
		.width = 96,
	},
};

static const struct vcap_field es2_ip_7tuple_keyfield[] = {
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_KF_ACL_GRP_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 1,
		.width = 8,
	},
	[VCAP_KF_PROT_ACTIVE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 9,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 10,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 11,
		.width = 1,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 12,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 26,
		.width = 13,
	},
	[VCAP_KF_IF_EGR_PORT_MASK_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 39,
		.width = 3,
	},
	[VCAP_KF_IF_EGR_PORT_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 42,
		.width = 32,
	},
	[VCAP_KF_IF_IGR_PORT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 74,
		.width = 1,
	},
	[VCAP_KF_IF_IGR_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 75,
		.width = 9,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 84,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 87,
		.width = 1,
	},
	[VCAP_KF_COSID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 88,
		.width = 3,
	},
	[VCAP_KF_L3_DPL_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 91,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 92,
		.width = 1,
	},
	[VCAP_KF_ES0_ISDX_KEY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 93,
		.width = 1,
	},
	[VCAP_KF_MIRROR_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 94,
		.width = 2,
	},
	[VCAP_KF_L2_DMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 96,
		.width = 48,
	},
	[VCAP_KF_L2_SMAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 144,
		.width = 48,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 192,
		.width = 1,
	},
	[VCAP_KF_L3_TTL_GT0] = {
		.type = VCAP_FIELD_BIT,
		.offset = 193,
		.width = 1,
	},
	[VCAP_KF_L3_TOS] = {
		.type = VCAP_FIELD_U32,
		.offset = 194,
		.width = 8,
	},
	[VCAP_KF_L3_IP6_DIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 202,
		.width = 128,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 330,
		.width = 128,
	},
	[VCAP_KF_L3_DIP_EQ_SIP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 458,
		.width = 1,
	},
	[VCAP_KF_TCP_UDP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 459,
		.width = 1,
	},
	[VCAP_KF_TCP_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 460,
		.width = 1,
	},
	[VCAP_KF_L4_DPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 461,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 477,
		.width = 16,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 493,
		.width = 16,
	},
	[VCAP_KF_L4_SPORT_EQ_DPORT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 509,
		.width = 1,
	},
	[VCAP_KF_L4_SEQUENCE_EQ0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 510,
		.width = 1,
	},
	[VCAP_KF_L4_FIN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 511,
		.width = 1,
	},
	[VCAP_KF_L4_SYN] = {
		.type = VCAP_FIELD_BIT,
		.offset = 512,
		.width = 1,
	},
	[VCAP_KF_L4_RST] = {
		.type = VCAP_FIELD_BIT,
		.offset = 513,
		.width = 1,
	},
	[VCAP_KF_L4_PSH] = {
		.type = VCAP_FIELD_BIT,
		.offset = 514,
		.width = 1,
	},
	[VCAP_KF_L4_ACK] = {
		.type = VCAP_FIELD_BIT,
		.offset = 515,
		.width = 1,
	},
	[VCAP_KF_L4_URG] = {
		.type = VCAP_FIELD_BIT,
		.offset = 516,
		.width = 1,
	},
	[VCAP_KF_L4_PAYLOAD] = {
		.type = VCAP_FIELD_U64,
		.offset = 517,
		.width = 64,
	},
};

static const struct vcap_field es2_ip4_vid_keyfield[] = {
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_KF_ACL_GRP_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 1,
		.width = 8,
	},
	[VCAP_KF_PROT_ACTIVE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 9,
		.width = 1,
	},
	[VCAP_KF_L2_MC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 10,
		.width = 1,
	},
	[VCAP_KF_L2_BC_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 11,
		.width = 1,
	},
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 12,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 25,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 26,
		.width = 13,
	},
	[VCAP_KF_8021Q_PCP_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 39,
		.width = 3,
	},
	[VCAP_KF_8021Q_DEI_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 42,
		.width = 1,
	},
	[VCAP_KF_COSID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 43,
		.width = 3,
	},
	[VCAP_KF_L3_DPL_CLS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 46,
		.width = 1,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 47,
		.width = 1,
	},
	[VCAP_KF_ES0_ISDX_KEY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 48,
		.width = 1,
	},
	[VCAP_KF_MIRROR_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 49,
		.width = 2,
	},
	[VCAP_KF_IP4_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 51,
		.width = 1,
	},
	[VCAP_KF_L3_IP4_DIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 52,
		.width = 32,
	},
	[VCAP_KF_L3_IP4_SIP] = {
		.type = VCAP_FIELD_U32,
		.offset = 84,
		.width = 32,
	},
	[VCAP_KF_L4_RNG] = {
		.type = VCAP_FIELD_U32,
		.offset = 116,
		.width = 16,
	},
};

static const struct vcap_field es2_ip6_vid_keyfield[] = {
	[VCAP_KF_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 0,
		.width = 3,
	},
	[VCAP_KF_LOOKUP_FIRST_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 3,
		.width = 1,
	},
	[VCAP_KF_ACL_GRP_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 8,
	},
	[VCAP_KF_PROT_ACTIVE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
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
	[VCAP_KF_ISDX_GT0_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_KF_ISDX_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 12,
	},
	[VCAP_KF_8021Q_VLAN_TAGGED_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 28,
		.width = 1,
	},
	[VCAP_KF_8021Q_VID_CLS] = {
		.type = VCAP_FIELD_U32,
		.offset = 29,
		.width = 13,
	},
	[VCAP_KF_L3_RT_IS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 42,
		.width = 1,
	},
	[VCAP_KF_L3_IP6_DIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 43,
		.width = 128,
	},
	[VCAP_KF_L3_IP6_SIP] = {
		.type = VCAP_FIELD_U128,
		.offset = 171,
		.width = 128,
	},
};

/* keyfield_set */
static const struct vcap_set is0_keyfield_set[] = {
	[VCAP_KFS_MLL] = {
		.type_id = 0,
		.sw_per_item = 3,
		.sw_cnt = 4,
	},
	[VCAP_KFS_TRI_VID] = {
		.type_id = 0,
		.sw_per_item = 2,
		.sw_cnt = 6,
	},
	[VCAP_KFS_LL_FULL] = {
		.type_id = 0,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_NORMAL] = {
		.type_id = 1,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_NORMAL_7TUPLE] = {
		.type_id = 0,
		.sw_per_item = 12,
		.sw_cnt = 1,
	},
	[VCAP_KFS_NORMAL_5TUPLE_IP4] = {
		.type_id = 2,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_PURE_5TUPLE_IP4] = {
		.type_id = 2,
		.sw_per_item = 3,
		.sw_cnt = 4,
	},
	[VCAP_KFS_ETAG] = {
		.type_id = 3,
		.sw_per_item = 2,
		.sw_cnt = 6,
	},
};

static const struct vcap_set is2_keyfield_set[] = {
	[VCAP_KFS_MAC_ETYPE] = {
		.type_id = 0,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_ARP] = {
		.type_id = 3,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP4_TCP_UDP] = {
		.type_id = 4,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP4_OTHER] = {
		.type_id = 5,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP6_STD] = {
		.type_id = 6,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP_7TUPLE] = {
		.type_id = 1,
		.sw_per_item = 12,
		.sw_cnt = 1,
	},
	[VCAP_KFS_IP6_VID] = {
		.type_id = 9,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
};

static const struct vcap_set es2_keyfield_set[] = {
	[VCAP_KFS_MAC_ETYPE] = {
		.type_id = 0,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_ARP] = {
		.type_id = 1,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP4_TCP_UDP] = {
		.type_id = 2,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP4_OTHER] = {
		.type_id = 3,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
	[VCAP_KFS_IP_7TUPLE] = {
		.type_id = -1,
		.sw_per_item = 12,
		.sw_cnt = 1,
	},
	[VCAP_KFS_IP4_VID] = {
		.type_id = -1,
		.sw_per_item = 3,
		.sw_cnt = 4,
	},
	[VCAP_KFS_IP6_VID] = {
		.type_id = 5,
		.sw_per_item = 6,
		.sw_cnt = 2,
	},
};

/* keyfield_set map */
static const struct vcap_field *is0_keyfield_set_map[] = {
	[VCAP_KFS_MLL] = is0_mll_keyfield,
	[VCAP_KFS_TRI_VID] = is0_tri_vid_keyfield,
	[VCAP_KFS_LL_FULL] = is0_ll_full_keyfield,
	[VCAP_KFS_NORMAL] = is0_normal_keyfield,
	[VCAP_KFS_NORMAL_7TUPLE] = is0_normal_7tuple_keyfield,
	[VCAP_KFS_NORMAL_5TUPLE_IP4] = is0_normal_5tuple_ip4_keyfield,
	[VCAP_KFS_PURE_5TUPLE_IP4] = is0_pure_5tuple_ip4_keyfield,
	[VCAP_KFS_ETAG] = is0_etag_keyfield,
};

static const struct vcap_field *is2_keyfield_set_map[] = {
	[VCAP_KFS_MAC_ETYPE] = is2_mac_etype_keyfield,
	[VCAP_KFS_ARP] = is2_arp_keyfield,
	[VCAP_KFS_IP4_TCP_UDP] = is2_ip4_tcp_udp_keyfield,
	[VCAP_KFS_IP4_OTHER] = is2_ip4_other_keyfield,
	[VCAP_KFS_IP6_STD] = is2_ip6_std_keyfield,
	[VCAP_KFS_IP_7TUPLE] = is2_ip_7tuple_keyfield,
	[VCAP_KFS_IP6_VID] = is2_ip6_vid_keyfield,
};

static const struct vcap_field *es2_keyfield_set_map[] = {
	[VCAP_KFS_MAC_ETYPE] = es2_mac_etype_keyfield,
	[VCAP_KFS_ARP] = es2_arp_keyfield,
	[VCAP_KFS_IP4_TCP_UDP] = es2_ip4_tcp_udp_keyfield,
	[VCAP_KFS_IP4_OTHER] = es2_ip4_other_keyfield,
	[VCAP_KFS_IP_7TUPLE] = es2_ip_7tuple_keyfield,
	[VCAP_KFS_IP4_VID] = es2_ip4_vid_keyfield,
	[VCAP_KFS_IP6_VID] = es2_ip6_vid_keyfield,
};

/* keyfield_set map sizes */
static int is0_keyfield_set_map_size[] = {
	[VCAP_KFS_MLL] = ARRAY_SIZE(is0_mll_keyfield),
	[VCAP_KFS_TRI_VID] = ARRAY_SIZE(is0_tri_vid_keyfield),
	[VCAP_KFS_LL_FULL] = ARRAY_SIZE(is0_ll_full_keyfield),
	[VCAP_KFS_NORMAL] = ARRAY_SIZE(is0_normal_keyfield),
	[VCAP_KFS_NORMAL_7TUPLE] = ARRAY_SIZE(is0_normal_7tuple_keyfield),
	[VCAP_KFS_NORMAL_5TUPLE_IP4] = ARRAY_SIZE(is0_normal_5tuple_ip4_keyfield),
	[VCAP_KFS_PURE_5TUPLE_IP4] = ARRAY_SIZE(is0_pure_5tuple_ip4_keyfield),
	[VCAP_KFS_ETAG] = ARRAY_SIZE(is0_etag_keyfield),
};

static int is2_keyfield_set_map_size[] = {
	[VCAP_KFS_MAC_ETYPE] = ARRAY_SIZE(is2_mac_etype_keyfield),
	[VCAP_KFS_ARP] = ARRAY_SIZE(is2_arp_keyfield),
	[VCAP_KFS_IP4_TCP_UDP] = ARRAY_SIZE(is2_ip4_tcp_udp_keyfield),
	[VCAP_KFS_IP4_OTHER] = ARRAY_SIZE(is2_ip4_other_keyfield),
	[VCAP_KFS_IP6_STD] = ARRAY_SIZE(is2_ip6_std_keyfield),
	[VCAP_KFS_IP_7TUPLE] = ARRAY_SIZE(is2_ip_7tuple_keyfield),
	[VCAP_KFS_IP6_VID] = ARRAY_SIZE(is2_ip6_vid_keyfield),
};

static int es2_keyfield_set_map_size[] = {
	[VCAP_KFS_MAC_ETYPE] = ARRAY_SIZE(es2_mac_etype_keyfield),
	[VCAP_KFS_ARP] = ARRAY_SIZE(es2_arp_keyfield),
	[VCAP_KFS_IP4_TCP_UDP] = ARRAY_SIZE(es2_ip4_tcp_udp_keyfield),
	[VCAP_KFS_IP4_OTHER] = ARRAY_SIZE(es2_ip4_other_keyfield),
	[VCAP_KFS_IP_7TUPLE] = ARRAY_SIZE(es2_ip_7tuple_keyfield),
	[VCAP_KFS_IP4_VID] = ARRAY_SIZE(es2_ip4_vid_keyfield),
	[VCAP_KFS_IP6_VID] = ARRAY_SIZE(es2_ip6_vid_keyfield),
};

/* actionfields */
static const struct vcap_field is0_mlbs_actionfield[] = {
	[VCAP_AF_TYPE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_AF_COSID_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 1,
		.width = 1,
	},
	[VCAP_AF_COSID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 3,
	},
	[VCAP_AF_QOS_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 5,
		.width = 1,
	},
	[VCAP_AF_QOS_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 6,
		.width = 3,
	},
	[VCAP_AF_DP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 9,
		.width = 1,
	},
	[VCAP_AF_DP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 10,
		.width = 2,
	},
	[VCAP_AF_MAP_LOOKUP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 12,
		.width = 2,
	},
	[VCAP_AF_MAP_KEY] = {
		.type = VCAP_FIELD_U32,
		.offset = 14,
		.width = 3,
	},
	[VCAP_AF_MAP_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 17,
		.width = 9,
	},
	[VCAP_AF_CLS_VID_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 26,
		.width = 3,
	},
	[VCAP_AF_GVID_ADD_REPLACE_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 29,
		.width = 3,
	},
	[VCAP_AF_VID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 32,
		.width = 13,
	},
	[VCAP_AF_ISDX_ADD_REPLACE_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 45,
		.width = 1,
	},
	[VCAP_AF_ISDX_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 46,
		.width = 12,
	},
	[VCAP_AF_FWD_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 58,
		.width = 1,
	},
	[VCAP_AF_CPU_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 59,
		.width = 1,
	},
	[VCAP_AF_CPU_Q] = {
		.type = VCAP_FIELD_U32,
		.offset = 60,
		.width = 3,
	},
	[VCAP_AF_OAM_Y1731_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 63,
		.width = 3,
	},
	[VCAP_AF_OAM_TWAMP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 66,
		.width = 1,
	},
	[VCAP_AF_OAM_IP_BFD_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 67,
		.width = 1,
	},
	[VCAP_AF_TC_LABEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 68,
		.width = 3,
	},
	[VCAP_AF_TTL_LABEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 71,
		.width = 3,
	},
	[VCAP_AF_NUM_VLD_LABELS] = {
		.type = VCAP_FIELD_U32,
		.offset = 74,
		.width = 2,
	},
	[VCAP_AF_FWD_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 76,
		.width = 3,
	},
	[VCAP_AF_MPLS_OAM_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 79,
		.width = 3,
	},
	[VCAP_AF_MPLS_MEP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 82,
		.width = 1,
	},
	[VCAP_AF_MPLS_MIP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 83,
		.width = 1,
	},
	[VCAP_AF_MPLS_OAM_FLAVOR] = {
		.type = VCAP_FIELD_BIT,
		.offset = 84,
		.width = 1,
	},
	[VCAP_AF_MPLS_IP_CTRL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 85,
		.width = 1,
	},
	[VCAP_AF_PAG_OVERRIDE_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 86,
		.width = 8,
	},
	[VCAP_AF_PAG_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 94,
		.width = 8,
	},
	[VCAP_AF_S2_KEY_SEL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 102,
		.width = 1,
	},
	[VCAP_AF_S2_KEY_SEL_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 103,
		.width = 6,
	},
	[VCAP_AF_PIPELINE_FORCE_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 109,
		.width = 2,
	},
	[VCAP_AF_PIPELINE_ACT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 111,
		.width = 1,
	},
	[VCAP_AF_PIPELINE_PT] = {
		.type = VCAP_FIELD_U32,
		.offset = 112,
		.width = 5,
	},
	[VCAP_AF_NXT_KEY_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 117,
		.width = 5,
	},
	[VCAP_AF_NXT_NORM_W16_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 122,
		.width = 5,
	},
	[VCAP_AF_NXT_OFFSET_FROM_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 127,
		.width = 2,
	},
	[VCAP_AF_NXT_TYPE_AFTER_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 129,
		.width = 2,
	},
	[VCAP_AF_NXT_NORMALIZE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 131,
		.width = 1,
	},
	[VCAP_AF_NXT_IDX_CTRL] = {
		.type = VCAP_FIELD_U32,
		.offset = 132,
		.width = 3,
	},
	[VCAP_AF_NXT_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 135,
		.width = 12,
	},
};

static const struct vcap_field is0_mlbs_reduced_actionfield[] = {
	[VCAP_AF_TYPE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_AF_COSID_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 1,
		.width = 1,
	},
	[VCAP_AF_COSID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 3,
	},
	[VCAP_AF_QOS_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 5,
		.width = 1,
	},
	[VCAP_AF_QOS_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 6,
		.width = 3,
	},
	[VCAP_AF_DP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 9,
		.width = 1,
	},
	[VCAP_AF_DP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 10,
		.width = 2,
	},
	[VCAP_AF_MAP_LOOKUP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 12,
		.width = 2,
	},
	[VCAP_AF_ISDX_ADD_REPLACE_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 14,
		.width = 1,
	},
	[VCAP_AF_ISDX_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 15,
		.width = 12,
	},
	[VCAP_AF_FWD_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 27,
		.width = 1,
	},
	[VCAP_AF_CPU_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 28,
		.width = 1,
	},
	[VCAP_AF_CPU_Q] = {
		.type = VCAP_FIELD_U32,
		.offset = 29,
		.width = 3,
	},
	[VCAP_AF_TC_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 32,
		.width = 1,
	},
	[VCAP_AF_TTL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 33,
		.width = 1,
	},
	[VCAP_AF_FWD_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 34,
		.width = 3,
	},
	[VCAP_AF_MPLS_OAM_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 37,
		.width = 3,
	},
	[VCAP_AF_MPLS_MEP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 40,
		.width = 1,
	},
	[VCAP_AF_MPLS_MIP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 41,
		.width = 1,
	},
	[VCAP_AF_MPLS_OAM_FLAVOR] = {
		.type = VCAP_FIELD_BIT,
		.offset = 42,
		.width = 1,
	},
	[VCAP_AF_MPLS_IP_CTRL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 43,
		.width = 1,
	},
	[VCAP_AF_PIPELINE_FORCE_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 44,
		.width = 2,
	},
	[VCAP_AF_PIPELINE_ACT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 46,
		.width = 1,
	},
	[VCAP_AF_PIPELINE_PT_REDUCED] = {
		.type = VCAP_FIELD_U32,
		.offset = 47,
		.width = 3,
	},
	[VCAP_AF_NXT_KEY_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 50,
		.width = 5,
	},
	[VCAP_AF_NXT_NORM_W32_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 55,
		.width = 2,
	},
	[VCAP_AF_NXT_TYPE_AFTER_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 57,
		.width = 2,
	},
	[VCAP_AF_NXT_NORMALIZE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 59,
		.width = 1,
	},
	[VCAP_AF_NXT_IDX_CTRL] = {
		.type = VCAP_FIELD_U32,
		.offset = 60,
		.width = 3,
	},
	[VCAP_AF_NXT_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 63,
		.width = 12,
	},
};

static const struct vcap_field is0_classification_actionfield[] = {
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
	[VCAP_AF_COSID_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 8,
		.width = 1,
	},
	[VCAP_AF_COSID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 9,
		.width = 3,
	},
	[VCAP_AF_QOS_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 12,
		.width = 1,
	},
	[VCAP_AF_QOS_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 13,
		.width = 3,
	},
	[VCAP_AF_DP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 16,
		.width = 1,
	},
	[VCAP_AF_DP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 17,
		.width = 2,
	},
	[VCAP_AF_DEI_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 19,
		.width = 1,
	},
	[VCAP_AF_DEI_VAL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 20,
		.width = 1,
	},
	[VCAP_AF_PCP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 21,
		.width = 1,
	},
	[VCAP_AF_PCP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 22,
		.width = 3,
	},
	[VCAP_AF_MAP_LOOKUP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 25,
		.width = 2,
	},
	[VCAP_AF_MAP_KEY] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 3,
	},
	[VCAP_AF_MAP_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 30,
		.width = 9,
	},
	[VCAP_AF_CLS_VID_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 39,
		.width = 3,
	},
	[VCAP_AF_GVID_ADD_REPLACE_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 42,
		.width = 3,
	},
	[VCAP_AF_VID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 45,
		.width = 13,
	},
	[VCAP_AF_VLAN_POP_CNT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 58,
		.width = 1,
	},
	[VCAP_AF_VLAN_POP_CNT] = {
		.type = VCAP_FIELD_U32,
		.offset = 59,
		.width = 2,
	},
	[VCAP_AF_VLAN_PUSH_CNT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 61,
		.width = 1,
	},
	[VCAP_AF_VLAN_PUSH_CNT] = {
		.type = VCAP_FIELD_U32,
		.offset = 62,
		.width = 2,
	},
	[VCAP_AF_TPID_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 64,
		.width = 2,
	},
	[VCAP_AF_VLAN_WAS_TAGGED] = {
		.type = VCAP_FIELD_U32,
		.offset = 66,
		.width = 2,
	},
	[VCAP_AF_ISDX_ADD_REPLACE_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 68,
		.width = 1,
	},
	[VCAP_AF_ISDX_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 69,
		.width = 12,
	},
	[VCAP_AF_RT_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 81,
		.width = 2,
	},
	[VCAP_AF_LPM_AFFIX_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 83,
		.width = 1,
	},
	[VCAP_AF_LPM_AFFIX_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 84,
		.width = 10,
	},
	[VCAP_AF_RLEG_DMAC_CHK_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 94,
		.width = 1,
	},
	[VCAP_AF_TTL_DECR_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 95,
		.width = 1,
	},
	[VCAP_AF_L3_MAC_UPDATE_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 96,
		.width = 1,
	},
	[VCAP_AF_FWD_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 97,
		.width = 1,
	},
	[VCAP_AF_CPU_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 98,
		.width = 1,
	},
	[VCAP_AF_CPU_Q] = {
		.type = VCAP_FIELD_U32,
		.offset = 99,
		.width = 3,
	},
	[VCAP_AF_MIP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 102,
		.width = 2,
	},
	[VCAP_AF_OAM_Y1731_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 104,
		.width = 3,
	},
	[VCAP_AF_OAM_TWAMP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 107,
		.width = 1,
	},
	[VCAP_AF_OAM_IP_BFD_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 108,
		.width = 1,
	},
	[VCAP_AF_PAG_OVERRIDE_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 109,
		.width = 8,
	},
	[VCAP_AF_PAG_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 117,
		.width = 8,
	},
	[VCAP_AF_S2_KEY_SEL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 125,
		.width = 1,
	},
	[VCAP_AF_S2_KEY_SEL_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 126,
		.width = 6,
	},
	[VCAP_AF_INJ_MASQ_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 132,
		.width = 1,
	},
	[VCAP_AF_INJ_MASQ_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 133,
		.width = 7,
	},
	[VCAP_AF_LPORT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 140,
		.width = 1,
	},
	[VCAP_AF_INJ_MASQ_LPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 141,
		.width = 7,
	},
	[VCAP_AF_PIPELINE_FORCE_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 148,
		.width = 2,
	},
	[VCAP_AF_PIPELINE_ACT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 150,
		.width = 1,
	},
	[VCAP_AF_PIPELINE_PT] = {
		.type = VCAP_FIELD_U32,
		.offset = 151,
		.width = 5,
	},
	[VCAP_AF_NXT_KEY_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 156,
		.width = 5,
	},
	[VCAP_AF_NXT_NORM_W16_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 161,
		.width = 5,
	},
	[VCAP_AF_NXT_OFFSET_FROM_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 166,
		.width = 2,
	},
	[VCAP_AF_NXT_TYPE_AFTER_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 168,
		.width = 2,
	},
	[VCAP_AF_NXT_NORMALIZE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 170,
		.width = 1,
	},
	[VCAP_AF_NXT_IDX_CTRL] = {
		.type = VCAP_FIELD_U32,
		.offset = 171,
		.width = 3,
	},
	[VCAP_AF_NXT_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 174,
		.width = 12,
	},
};

static const struct vcap_field is0_full_actionfield[] = {
	[VCAP_AF_DSCP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_AF_DSCP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 1,
		.width = 6,
	},
	[VCAP_AF_COSID_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 7,
		.width = 1,
	},
	[VCAP_AF_COSID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 8,
		.width = 3,
	},
	[VCAP_AF_QOS_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 11,
		.width = 1,
	},
	[VCAP_AF_QOS_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 12,
		.width = 3,
	},
	[VCAP_AF_DP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_AF_DP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 16,
		.width = 2,
	},
	[VCAP_AF_DEI_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 18,
		.width = 1,
	},
	[VCAP_AF_DEI_VAL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 19,
		.width = 1,
	},
	[VCAP_AF_PCP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 20,
		.width = 1,
	},
	[VCAP_AF_PCP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 21,
		.width = 3,
	},
	[VCAP_AF_MAP_LOOKUP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 24,
		.width = 2,
	},
	[VCAP_AF_MAP_KEY] = {
		.type = VCAP_FIELD_U32,
		.offset = 26,
		.width = 3,
	},
	[VCAP_AF_MAP_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 29,
		.width = 9,
	},
	[VCAP_AF_CLS_VID_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 38,
		.width = 3,
	},
	[VCAP_AF_GVID_ADD_REPLACE_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 41,
		.width = 3,
	},
	[VCAP_AF_VID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 44,
		.width = 13,
	},
	[VCAP_AF_VLAN_POP_CNT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 57,
		.width = 1,
	},
	[VCAP_AF_VLAN_POP_CNT] = {
		.type = VCAP_FIELD_U32,
		.offset = 58,
		.width = 2,
	},
	[VCAP_AF_VLAN_PUSH_CNT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 60,
		.width = 1,
	},
	[VCAP_AF_VLAN_PUSH_CNT] = {
		.type = VCAP_FIELD_U32,
		.offset = 61,
		.width = 2,
	},
	[VCAP_AF_TPID_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 63,
		.width = 2,
	},
	[VCAP_AF_VLAN_WAS_TAGGED] = {
		.type = VCAP_FIELD_U32,
		.offset = 65,
		.width = 2,
	},
	[VCAP_AF_ISDX_ADD_REPLACE_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 67,
		.width = 1,
	},
	[VCAP_AF_ISDX_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 68,
		.width = 12,
	},
	[VCAP_AF_MASK_MODE] = {
		.type = VCAP_FIELD_U32,
		.offset = 80,
		.width = 3,
	},
	[VCAP_AF_PORT_MASK] = {
		.type = VCAP_FIELD_U72,
		.offset = 83,
		.width = 65,
	},
	[VCAP_AF_RT_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 148,
		.width = 2,
	},
	[VCAP_AF_LPM_AFFIX_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 150,
		.width = 1,
	},
	[VCAP_AF_LPM_AFFIX_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 151,
		.width = 10,
	},
	[VCAP_AF_RLEG_DMAC_CHK_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 161,
		.width = 1,
	},
	[VCAP_AF_TTL_DECR_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 162,
		.width = 1,
	},
	[VCAP_AF_L3_MAC_UPDATE_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 163,
		.width = 1,
	},
	[VCAP_AF_CPU_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 164,
		.width = 1,
	},
	[VCAP_AF_CPU_Q] = {
		.type = VCAP_FIELD_U32,
		.offset = 165,
		.width = 3,
	},
	[VCAP_AF_MIP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 168,
		.width = 2,
	},
	[VCAP_AF_OAM_Y1731_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 170,
		.width = 3,
	},
	[VCAP_AF_OAM_TWAMP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 173,
		.width = 1,
	},
	[VCAP_AF_OAM_IP_BFD_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 174,
		.width = 1,
	},
	[VCAP_AF_RSVD_LBL_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 175,
		.width = 4,
	},
	[VCAP_AF_TC_LABEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 179,
		.width = 3,
	},
	[VCAP_AF_TTL_LABEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 182,
		.width = 3,
	},
	[VCAP_AF_NUM_VLD_LABELS] = {
		.type = VCAP_FIELD_U32,
		.offset = 185,
		.width = 2,
	},
	[VCAP_AF_FWD_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 187,
		.width = 3,
	},
	[VCAP_AF_MPLS_OAM_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 190,
		.width = 3,
	},
	[VCAP_AF_MPLS_MEP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 193,
		.width = 1,
	},
	[VCAP_AF_MPLS_MIP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 194,
		.width = 1,
	},
	[VCAP_AF_MPLS_OAM_FLAVOR] = {
		.type = VCAP_FIELD_BIT,
		.offset = 195,
		.width = 1,
	},
	[VCAP_AF_MPLS_IP_CTRL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 196,
		.width = 1,
	},
	[VCAP_AF_CUSTOM_ACE_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 197,
		.width = 5,
	},
	[VCAP_AF_CUSTOM_ACE_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 202,
		.width = 2,
	},
	[VCAP_AF_PAG_OVERRIDE_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 204,
		.width = 8,
	},
	[VCAP_AF_PAG_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 212,
		.width = 8,
	},
	[VCAP_AF_S2_KEY_SEL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 220,
		.width = 1,
	},
	[VCAP_AF_S2_KEY_SEL_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 221,
		.width = 6,
	},
	[VCAP_AF_INJ_MASQ_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 227,
		.width = 1,
	},
	[VCAP_AF_INJ_MASQ_PORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 228,
		.width = 7,
	},
	[VCAP_AF_LPORT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 235,
		.width = 1,
	},
	[VCAP_AF_INJ_MASQ_LPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 236,
		.width = 7,
	},
	[VCAP_AF_MATCH_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 243,
		.width = 16,
	},
	[VCAP_AF_MATCH_ID_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 259,
		.width = 16,
	},
	[VCAP_AF_PIPELINE_FORCE_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 275,
		.width = 2,
	},
	[VCAP_AF_PIPELINE_ACT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 277,
		.width = 1,
	},
	[VCAP_AF_PIPELINE_PT] = {
		.type = VCAP_FIELD_U32,
		.offset = 278,
		.width = 5,
	},
	[VCAP_AF_NXT_KEY_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 283,
		.width = 5,
	},
	[VCAP_AF_NXT_NORM_W16_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 288,
		.width = 5,
	},
	[VCAP_AF_NXT_OFFSET_FROM_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 293,
		.width = 2,
	},
	[VCAP_AF_NXT_TYPE_AFTER_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 295,
		.width = 2,
	},
	[VCAP_AF_NXT_NORMALIZE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 297,
		.width = 1,
	},
	[VCAP_AF_NXT_IDX_CTRL] = {
		.type = VCAP_FIELD_U32,
		.offset = 298,
		.width = 3,
	},
	[VCAP_AF_NXT_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 301,
		.width = 12,
	},
};

static const struct vcap_field is0_class_reduced_actionfield[] = {
	[VCAP_AF_TYPE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_AF_COSID_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 1,
		.width = 1,
	},
	[VCAP_AF_COSID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 3,
	},
	[VCAP_AF_QOS_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 5,
		.width = 1,
	},
	[VCAP_AF_QOS_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 6,
		.width = 3,
	},
	[VCAP_AF_DP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 9,
		.width = 1,
	},
	[VCAP_AF_DP_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 10,
		.width = 2,
	},
	[VCAP_AF_MAP_LOOKUP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 12,
		.width = 2,
	},
	[VCAP_AF_MAP_KEY] = {
		.type = VCAP_FIELD_U32,
		.offset = 14,
		.width = 3,
	},
	[VCAP_AF_CLS_VID_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 17,
		.width = 3,
	},
	[VCAP_AF_GVID_ADD_REPLACE_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 3,
	},
	[VCAP_AF_VID_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 23,
		.width = 13,
	},
	[VCAP_AF_VLAN_POP_CNT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 36,
		.width = 1,
	},
	[VCAP_AF_VLAN_POP_CNT] = {
		.type = VCAP_FIELD_U32,
		.offset = 37,
		.width = 2,
	},
	[VCAP_AF_VLAN_PUSH_CNT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 39,
		.width = 1,
	},
	[VCAP_AF_VLAN_PUSH_CNT] = {
		.type = VCAP_FIELD_U32,
		.offset = 40,
		.width = 2,
	},
	[VCAP_AF_TPID_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 42,
		.width = 2,
	},
	[VCAP_AF_VLAN_WAS_TAGGED] = {
		.type = VCAP_FIELD_U32,
		.offset = 44,
		.width = 2,
	},
	[VCAP_AF_ISDX_ADD_REPLACE_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 46,
		.width = 1,
	},
	[VCAP_AF_ISDX_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 47,
		.width = 12,
	},
	[VCAP_AF_FWD_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 59,
		.width = 1,
	},
	[VCAP_AF_CPU_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 60,
		.width = 1,
	},
	[VCAP_AF_CPU_Q] = {
		.type = VCAP_FIELD_U32,
		.offset = 61,
		.width = 3,
	},
	[VCAP_AF_MIP_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 64,
		.width = 2,
	},
	[VCAP_AF_OAM_Y1731_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 66,
		.width = 3,
	},
	[VCAP_AF_LPORT_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 69,
		.width = 1,
	},
	[VCAP_AF_INJ_MASQ_LPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 70,
		.width = 7,
	},
	[VCAP_AF_PIPELINE_FORCE_ENA] = {
		.type = VCAP_FIELD_U32,
		.offset = 77,
		.width = 2,
	},
	[VCAP_AF_PIPELINE_ACT_SEL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 79,
		.width = 1,
	},
	[VCAP_AF_PIPELINE_PT] = {
		.type = VCAP_FIELD_U32,
		.offset = 80,
		.width = 5,
	},
	[VCAP_AF_NXT_KEY_TYPE] = {
		.type = VCAP_FIELD_U32,
		.offset = 85,
		.width = 5,
	},
	[VCAP_AF_NXT_IDX_CTRL] = {
		.type = VCAP_FIELD_U32,
		.offset = 90,
		.width = 3,
	},
	[VCAP_AF_NXT_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 93,
		.width = 12,
	},
};

static const struct vcap_field is2_base_type_actionfield[] = {
	[VCAP_AF_IS_INNER_ACL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_AF_PIPELINE_FORCE_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 1,
		.width = 1,
	},
	[VCAP_AF_PIPELINE_PT] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 5,
	},
	[VCAP_AF_HIT_ME_ONCE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 7,
		.width = 1,
	},
	[VCAP_AF_INTR_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 8,
		.width = 1,
	},
	[VCAP_AF_CPU_COPY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 9,
		.width = 1,
	},
	[VCAP_AF_CPU_QUEUE_NUM] = {
		.type = VCAP_FIELD_U32,
		.offset = 10,
		.width = 3,
	},
	[VCAP_AF_CPU_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 13,
		.width = 1,
	},
	[VCAP_AF_LRN_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 14,
		.width = 1,
	},
	[VCAP_AF_RT_DIS] = {
		.type = VCAP_FIELD_BIT,
		.offset = 15,
		.width = 1,
	},
	[VCAP_AF_POLICE_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 16,
		.width = 1,
	},
	[VCAP_AF_POLICE_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 17,
		.width = 6,
	},
	[VCAP_AF_IGNORE_PIPELINE_CTRL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 23,
		.width = 1,
	},
	[VCAP_AF_DLB_OFFSET] = {
		.type = VCAP_FIELD_U32,
		.offset = 24,
		.width = 3,
	},
	[VCAP_AF_MASK_MODE] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 3,
	},
	[VCAP_AF_PORT_MASK] = {
		.type = VCAP_FIELD_U72,
		.offset = 30,
		.width = 68,
	},
	[VCAP_AF_RSDX_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 98,
		.width = 1,
	},
	[VCAP_AF_RSDX_VAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 99,
		.width = 12,
	},
	[VCAP_AF_MIRROR_PROBE] = {
		.type = VCAP_FIELD_U32,
		.offset = 111,
		.width = 2,
	},
	[VCAP_AF_REW_CMD] = {
		.type = VCAP_FIELD_U32,
		.offset = 113,
		.width = 11,
	},
	[VCAP_AF_TTL_UPDATE_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 124,
		.width = 1,
	},
	[VCAP_AF_SAM_SEQ_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 125,
		.width = 1,
	},
	[VCAP_AF_TCP_UDP_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 126,
		.width = 1,
	},
	[VCAP_AF_TCP_UDP_DPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 127,
		.width = 16,
	},
	[VCAP_AF_TCP_UDP_SPORT] = {
		.type = VCAP_FIELD_U32,
		.offset = 143,
		.width = 16,
	},
	[VCAP_AF_MATCH_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 159,
		.width = 16,
	},
	[VCAP_AF_MATCH_ID_MASK] = {
		.type = VCAP_FIELD_U32,
		.offset = 175,
		.width = 16,
	},
	[VCAP_AF_CNT_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 191,
		.width = 12,
	},
	[VCAP_AF_SWAP_MAC_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 203,
		.width = 1,
	},
	[VCAP_AF_ACL_RT_MODE] = {
		.type = VCAP_FIELD_U32,
		.offset = 204,
		.width = 4,
	},
	[VCAP_AF_ACL_MAC] = {
		.type = VCAP_FIELD_U48,
		.offset = 208,
		.width = 48,
	},
	[VCAP_AF_DMAC_OFFSET_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 256,
		.width = 1,
	},
	[VCAP_AF_PTP_MASTER_SEL] = {
		.type = VCAP_FIELD_U32,
		.offset = 257,
		.width = 2,
	},
	[VCAP_AF_LOG_MSG_INTERVAL] = {
		.type = VCAP_FIELD_U32,
		.offset = 259,
		.width = 4,
	},
	[VCAP_AF_SIP_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 263,
		.width = 5,
	},
	[VCAP_AF_RLEG_STAT_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 268,
		.width = 3,
	},
	[VCAP_AF_IGR_ACL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 271,
		.width = 1,
	},
	[VCAP_AF_EGR_ACL_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 272,
		.width = 1,
	},
};

static const struct vcap_field es2_base_type_actionfield[] = {
	[VCAP_AF_HIT_ME_ONCE] = {
		.type = VCAP_FIELD_BIT,
		.offset = 0,
		.width = 1,
	},
	[VCAP_AF_INTR_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 1,
		.width = 1,
	},
	[VCAP_AF_FWD_MODE] = {
		.type = VCAP_FIELD_U32,
		.offset = 2,
		.width = 2,
	},
	[VCAP_AF_COPY_QUEUE_NUM] = {
		.type = VCAP_FIELD_U32,
		.offset = 4,
		.width = 16,
	},
	[VCAP_AF_COPY_PORT_NUM] = {
		.type = VCAP_FIELD_U32,
		.offset = 20,
		.width = 7,
	},
	[VCAP_AF_MIRROR_PROBE_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 27,
		.width = 2,
	},
	[VCAP_AF_CPU_COPY_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 29,
		.width = 1,
	},
	[VCAP_AF_CPU_QUEUE_NUM] = {
		.type = VCAP_FIELD_U32,
		.offset = 30,
		.width = 3,
	},
	[VCAP_AF_POLICE_ENA] = {
		.type = VCAP_FIELD_BIT,
		.offset = 33,
		.width = 1,
	},
	[VCAP_AF_POLICE_REMARK] = {
		.type = VCAP_FIELD_BIT,
		.offset = 34,
		.width = 1,
	},
	[VCAP_AF_POLICE_IDX] = {
		.type = VCAP_FIELD_U32,
		.offset = 35,
		.width = 6,
	},
	[VCAP_AF_ES2_REW_CMD] = {
		.type = VCAP_FIELD_U32,
		.offset = 41,
		.width = 3,
	},
	[VCAP_AF_CNT_ID] = {
		.type = VCAP_FIELD_U32,
		.offset = 44,
		.width = 11,
	},
	[VCAP_AF_IGNORE_PIPELINE_CTRL] = {
		.type = VCAP_FIELD_BIT,
		.offset = 55,
		.width = 1,
	},
};

/* actionfield_set */
static const struct vcap_set is0_actionfield_set[] = {
	[VCAP_AFS_MLBS] = {
		.type_id = 0,
		.sw_per_item = 2,
		.sw_cnt = 6,
	},
	[VCAP_AFS_MLBS_REDUCED] = {
		.type_id = 0,
		.sw_per_item = 1,
		.sw_cnt = 12,
	},
	[VCAP_AFS_CLASSIFICATION] = {
		.type_id = 1,
		.sw_per_item = 2,
		.sw_cnt = 6,
	},
	[VCAP_AFS_FULL] = {
		.type_id = -1,
		.sw_per_item = 3,
		.sw_cnt = 4,
	},
	[VCAP_AFS_CLASS_REDUCED] = {
		.type_id = 1,
		.sw_per_item = 1,
		.sw_cnt = 12,
	},
};

static const struct vcap_set is2_actionfield_set[] = {
	[VCAP_AFS_BASE_TYPE] = {
		.type_id = -1,
		.sw_per_item = 3,
		.sw_cnt = 4,
	},
};

static const struct vcap_set es2_actionfield_set[] = {
	[VCAP_AFS_BASE_TYPE] = {
		.type_id = -1,
		.sw_per_item = 3,
		.sw_cnt = 4,
	},
};

/* actionfield_set map */
static const struct vcap_field *is0_actionfield_set_map[] = {
	[VCAP_AFS_MLBS] = is0_mlbs_actionfield,
	[VCAP_AFS_MLBS_REDUCED] = is0_mlbs_reduced_actionfield,
	[VCAP_AFS_CLASSIFICATION] = is0_classification_actionfield,
	[VCAP_AFS_FULL] = is0_full_actionfield,
	[VCAP_AFS_CLASS_REDUCED] = is0_class_reduced_actionfield,
};

static const struct vcap_field *is2_actionfield_set_map[] = {
	[VCAP_AFS_BASE_TYPE] = is2_base_type_actionfield,
};

static const struct vcap_field *es2_actionfield_set_map[] = {
	[VCAP_AFS_BASE_TYPE] = es2_base_type_actionfield,
};

/* actionfield_set map size */
static int is0_actionfield_set_map_size[] = {
	[VCAP_AFS_MLBS] = ARRAY_SIZE(is0_mlbs_actionfield),
	[VCAP_AFS_MLBS_REDUCED] = ARRAY_SIZE(is0_mlbs_reduced_actionfield),
	[VCAP_AFS_CLASSIFICATION] = ARRAY_SIZE(is0_classification_actionfield),
	[VCAP_AFS_FULL] = ARRAY_SIZE(is0_full_actionfield),
	[VCAP_AFS_CLASS_REDUCED] = ARRAY_SIZE(is0_class_reduced_actionfield),
};

static int is2_actionfield_set_map_size[] = {
	[VCAP_AFS_BASE_TYPE] = ARRAY_SIZE(is2_base_type_actionfield),
};

static int es2_actionfield_set_map_size[] = {
	[VCAP_AFS_BASE_TYPE] = ARRAY_SIZE(es2_base_type_actionfield),
};

/* Type Groups */
static const struct vcap_typegroup is0_x12_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 5,
		.value = 16,
	},
	{
		.offset = 52,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 104,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 156,
		.width = 3,
		.value = 0,
	},
	{
		.offset = 208,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 260,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 312,
		.width = 4,
		.value = 0,
	},
	{
		.offset = 364,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 416,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 468,
		.width = 3,
		.value = 0,
	},
	{
		.offset = 520,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 572,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is0_x6_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 4,
		.value = 8,
	},
	{
		.offset = 52,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 104,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 156,
		.width = 3,
		.value = 0,
	},
	{
		.offset = 208,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 260,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is0_x3_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 3,
		.value = 4,
	},
	{
		.offset = 52,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 104,
		.width = 2,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is0_x2_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 2,
		.value = 2,
	},
	{
		.offset = 52,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is0_x1_keyfield_set_typegroups[] = {
	{}
};

static const struct vcap_typegroup is2_x12_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 3,
		.value = 4,
	},
	{
		.offset = 156,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 312,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 468,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is2_x6_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 2,
		.value = 2,
	},
	{
		.offset = 156,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is2_x3_keyfield_set_typegroups[] = {
	{}
};

static const struct vcap_typegroup is2_x1_keyfield_set_typegroups[] = {
	{}
};

static const struct vcap_typegroup es2_x12_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 3,
		.value = 4,
	},
	{
		.offset = 156,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 312,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 468,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup es2_x6_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 2,
		.value = 2,
	},
	{
		.offset = 156,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup es2_x3_keyfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 1,
		.value = 1,
	},
	{}
};

static const struct vcap_typegroup es2_x1_keyfield_set_typegroups[] = {
	{}
};

static const struct vcap_typegroup *is0_keyfield_set_typegroups[] = {
	[12] = is0_x12_keyfield_set_typegroups,
	[6] = is0_x6_keyfield_set_typegroups,
	[3] = is0_x3_keyfield_set_typegroups,
	[2] = is0_x2_keyfield_set_typegroups,
	[1] = is0_x1_keyfield_set_typegroups,
	[13] = NULL,
};

static const struct vcap_typegroup *is2_keyfield_set_typegroups[] = {
	[12] = is2_x12_keyfield_set_typegroups,
	[6] = is2_x6_keyfield_set_typegroups,
	[3] = is2_x3_keyfield_set_typegroups,
	[1] = is2_x1_keyfield_set_typegroups,
	[13] = NULL,
};

static const struct vcap_typegroup *es2_keyfield_set_typegroups[] = {
	[12] = es2_x12_keyfield_set_typegroups,
	[6] = es2_x6_keyfield_set_typegroups,
	[3] = es2_x3_keyfield_set_typegroups,
	[1] = es2_x1_keyfield_set_typegroups,
	[13] = NULL,
};

static const struct vcap_typegroup is0_x3_actionfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 3,
		.value = 4,
	},
	{
		.offset = 110,
		.width = 2,
		.value = 0,
	},
	{
		.offset = 220,
		.width = 2,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is0_x2_actionfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 2,
		.value = 2,
	},
	{
		.offset = 110,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is0_x1_actionfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 1,
		.value = 1,
	},
	{}
};

static const struct vcap_typegroup is2_x3_actionfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 2,
		.value = 2,
	},
	{
		.offset = 110,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 220,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup is2_x1_actionfield_set_typegroups[] = {
	{}
};

static const struct vcap_typegroup es2_x3_actionfield_set_typegroups[] = {
	{
		.offset = 0,
		.width = 2,
		.value = 2,
	},
	{
		.offset = 21,
		.width = 1,
		.value = 0,
	},
	{
		.offset = 42,
		.width = 1,
		.value = 0,
	},
	{}
};

static const struct vcap_typegroup es2_x1_actionfield_set_typegroups[] = {
	{}
};

static const struct vcap_typegroup *is0_actionfield_set_typegroups[] = {
	[3] = is0_x3_actionfield_set_typegroups,
	[2] = is0_x2_actionfield_set_typegroups,
	[1] = is0_x1_actionfield_set_typegroups,
	[13] = NULL,
};

static const struct vcap_typegroup *is2_actionfield_set_typegroups[] = {
	[3] = is2_x3_actionfield_set_typegroups,
	[1] = is2_x1_actionfield_set_typegroups,
	[13] = NULL,
};

static const struct vcap_typegroup *es2_actionfield_set_typegroups[] = {
	[3] = es2_x3_actionfield_set_typegroups,
	[1] = es2_x1_actionfield_set_typegroups,
	[13] = NULL,
};

/* Keyfieldset names */
static const char * const vcap_keyfield_set_names[] = {
	[VCAP_KFS_NO_VALUE]                      =  "(None)",
	[VCAP_KFS_ARP]                           =  "VCAP_KFS_ARP",
	[VCAP_KFS_ETAG]                          =  "VCAP_KFS_ETAG",
	[VCAP_KFS_IP4_OTHER]                     =  "VCAP_KFS_IP4_OTHER",
	[VCAP_KFS_IP4_TCP_UDP]                   =  "VCAP_KFS_IP4_TCP_UDP",
	[VCAP_KFS_IP4_VID]                       =  "VCAP_KFS_IP4_VID",
	[VCAP_KFS_IP6_STD]                       =  "VCAP_KFS_IP6_STD",
	[VCAP_KFS_IP6_VID]                       =  "VCAP_KFS_IP6_VID",
	[VCAP_KFS_IP_7TUPLE]                     =  "VCAP_KFS_IP_7TUPLE",
	[VCAP_KFS_LL_FULL]                       =  "VCAP_KFS_LL_FULL",
	[VCAP_KFS_MAC_ETYPE]                     =  "VCAP_KFS_MAC_ETYPE",
	[VCAP_KFS_MLL]                           =  "VCAP_KFS_MLL",
	[VCAP_KFS_NORMAL]                        =  "VCAP_KFS_NORMAL",
	[VCAP_KFS_NORMAL_5TUPLE_IP4]             =  "VCAP_KFS_NORMAL_5TUPLE_IP4",
	[VCAP_KFS_NORMAL_7TUPLE]                 =  "VCAP_KFS_NORMAL_7TUPLE",
	[VCAP_KFS_PURE_5TUPLE_IP4]               =  "VCAP_KFS_PURE_5TUPLE_IP4",
	[VCAP_KFS_TRI_VID]                       =  "VCAP_KFS_TRI_VID",
};

/* Actionfieldset names */
static const char * const vcap_actionfield_set_names[] = {
	[VCAP_AFS_NO_VALUE]                      =  "(None)",
	[VCAP_AFS_BASE_TYPE]                     =  "VCAP_AFS_BASE_TYPE",
	[VCAP_AFS_CLASSIFICATION]                =  "VCAP_AFS_CLASSIFICATION",
	[VCAP_AFS_CLASS_REDUCED]                 =  "VCAP_AFS_CLASS_REDUCED",
	[VCAP_AFS_FULL]                          =  "VCAP_AFS_FULL",
	[VCAP_AFS_MLBS]                          =  "VCAP_AFS_MLBS",
	[VCAP_AFS_MLBS_REDUCED]                  =  "VCAP_AFS_MLBS_REDUCED",
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
	[VCAP_KF_8021Q_DEI0]                     =  "8021Q_DEI0",
	[VCAP_KF_8021Q_DEI1]                     =  "8021Q_DEI1",
	[VCAP_KF_8021Q_DEI2]                     =  "8021Q_DEI2",
	[VCAP_KF_8021Q_DEI_CLS]                  =  "8021Q_DEI_CLS",
	[VCAP_KF_8021Q_PCP0]                     =  "8021Q_PCP0",
	[VCAP_KF_8021Q_PCP1]                     =  "8021Q_PCP1",
	[VCAP_KF_8021Q_PCP2]                     =  "8021Q_PCP2",
	[VCAP_KF_8021Q_PCP_CLS]                  =  "8021Q_PCP_CLS",
	[VCAP_KF_8021Q_TPID0]                    =  "8021Q_TPID0",
	[VCAP_KF_8021Q_TPID1]                    =  "8021Q_TPID1",
	[VCAP_KF_8021Q_TPID2]                    =  "8021Q_TPID2",
	[VCAP_KF_8021Q_VID0]                     =  "8021Q_VID0",
	[VCAP_KF_8021Q_VID1]                     =  "8021Q_VID1",
	[VCAP_KF_8021Q_VID2]                     =  "8021Q_VID2",
	[VCAP_KF_8021Q_VID_CLS]                  =  "8021Q_VID_CLS",
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
	[VCAP_KF_DST_ENTRY]                      =  "DST_ENTRY",
	[VCAP_KF_ES0_ISDX_KEY_ENA]               =  "ES0_ISDX_KEY_ENA",
	[VCAP_KF_ETYPE]                          =  "ETYPE",
	[VCAP_KF_ETYPE_LEN_IS]                   =  "ETYPE_LEN_IS",
	[VCAP_KF_ETYPE_MPLS]                     =  "ETYPE_MPLS",
	[VCAP_KF_IF_EGR_PORT_MASK]               =  "IF_EGR_PORT_MASK",
	[VCAP_KF_IF_EGR_PORT_MASK_RNG]           =  "IF_EGR_PORT_MASK_RNG",
	[VCAP_KF_IF_IGR_PORT]                    =  "IF_IGR_PORT",
	[VCAP_KF_IF_IGR_PORT_MASK]               =  "IF_IGR_PORT_MASK",
	[VCAP_KF_IF_IGR_PORT_MASK_L3]            =  "IF_IGR_PORT_MASK_L3",
	[VCAP_KF_IF_IGR_PORT_MASK_RNG]           =  "IF_IGR_PORT_MASK_RNG",
	[VCAP_KF_IF_IGR_PORT_MASK_SEL]           =  "IF_IGR_PORT_MASK_SEL",
	[VCAP_KF_IF_IGR_PORT_SEL]                =  "IF_IGR_PORT_SEL",
	[VCAP_KF_IP4_IS]                         =  "IP4_IS",
	[VCAP_KF_IP_MC_IS]                       =  "IP_MC_IS",
	[VCAP_KF_IP_PAYLOAD_5TUPLE]              =  "IP_PAYLOAD_5TUPLE",
	[VCAP_KF_IP_SNAP_IS]                     =  "IP_SNAP_IS",
	[VCAP_KF_ISDX_CLS]                       =  "ISDX_CLS",
	[VCAP_KF_ISDX_GT0_IS]                    =  "ISDX_GT0_IS",
	[VCAP_KF_L2_BC_IS]                       =  "L2_BC_IS",
	[VCAP_KF_L2_DMAC]                        =  "L2_DMAC",
	[VCAP_KF_L2_FWD_IS]                      =  "L2_FWD_IS",
	[VCAP_KF_L2_MC_IS]                       =  "L2_MC_IS",
	[VCAP_KF_L2_PAYLOAD_ETYPE]               =  "L2_PAYLOAD_ETYPE",
	[VCAP_KF_L2_SMAC]                        =  "L2_SMAC",
	[VCAP_KF_L3_DIP_EQ_SIP_IS]               =  "L3_DIP_EQ_SIP_IS",
	[VCAP_KF_L3_DMAC_DIP_MATCH]              =  "L3_DMAC_DIP_MATCH",
	[VCAP_KF_L3_DPL_CLS]                     =  "L3_DPL_CLS",
	[VCAP_KF_L3_DSCP]                        =  "L3_DSCP",
	[VCAP_KF_L3_DST_IS]                      =  "L3_DST_IS",
	[VCAP_KF_L3_FRAGMENT_TYPE]               =  "L3_FRAGMENT_TYPE",
	[VCAP_KF_L3_FRAG_INVLD_L4_LEN]           =  "L3_FRAG_INVLD_L4_LEN",
	[VCAP_KF_L3_IP4_DIP]                     =  "L3_IP4_DIP",
	[VCAP_KF_L3_IP4_SIP]                     =  "L3_IP4_SIP",
	[VCAP_KF_L3_IP6_DIP]                     =  "L3_IP6_DIP",
	[VCAP_KF_L3_IP6_SIP]                     =  "L3_IP6_SIP",
	[VCAP_KF_L3_IP_PROTO]                    =  "L3_IP_PROTO",
	[VCAP_KF_L3_OPTIONS_IS]                  =  "L3_OPTIONS_IS",
	[VCAP_KF_L3_PAYLOAD]                     =  "L3_PAYLOAD",
	[VCAP_KF_L3_RT_IS]                       =  "L3_RT_IS",
	[VCAP_KF_L3_SMAC_SIP_MATCH]              =  "L3_SMAC_SIP_MATCH",
	[VCAP_KF_L3_TOS]                         =  "L3_TOS",
	[VCAP_KF_L3_TTL_GT0]                     =  "L3_TTL_GT0",
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
	[VCAP_KF_LOOKUP_PAG]                     =  "LOOKUP_PAG",
	[VCAP_KF_MIRROR_ENA]                     =  "MIRROR_ENA",
	[VCAP_KF_OAM_CCM_CNTS_EQ0]               =  "OAM_CCM_CNTS_EQ0",
	[VCAP_KF_OAM_MEL_FLAGS]                  =  "OAM_MEL_FLAGS",
	[VCAP_KF_OAM_Y1731_IS]                   =  "OAM_Y1731_IS",
	[VCAP_KF_PROT_ACTIVE]                    =  "PROT_ACTIVE",
	[VCAP_KF_TCP_IS]                         =  "TCP_IS",
	[VCAP_KF_TCP_UDP_IS]                     =  "TCP_UDP_IS",
	[VCAP_KF_TYPE]                           =  "TYPE",
};

/* Actionfield names */
static const char * const vcap_actionfield_names[] = {
	[VCAP_AF_NO_VALUE]                       =  "(None)",
	[VCAP_AF_ACL_MAC]                        =  "ACL_MAC",
	[VCAP_AF_ACL_RT_MODE]                    =  "ACL_RT_MODE",
	[VCAP_AF_CLS_VID_SEL]                    =  "CLS_VID_SEL",
	[VCAP_AF_CNT_ID]                         =  "CNT_ID",
	[VCAP_AF_COPY_PORT_NUM]                  =  "COPY_PORT_NUM",
	[VCAP_AF_COPY_QUEUE_NUM]                 =  "COPY_QUEUE_NUM",
	[VCAP_AF_COSID_ENA]                      =  "COSID_ENA",
	[VCAP_AF_COSID_VAL]                      =  "COSID_VAL",
	[VCAP_AF_CPU_COPY_ENA]                   =  "CPU_COPY_ENA",
	[VCAP_AF_CPU_DIS]                        =  "CPU_DIS",
	[VCAP_AF_CPU_ENA]                        =  "CPU_ENA",
	[VCAP_AF_CPU_Q]                          =  "CPU_Q",
	[VCAP_AF_CPU_QUEUE_NUM]                  =  "CPU_QUEUE_NUM",
	[VCAP_AF_CUSTOM_ACE_ENA]                 =  "CUSTOM_ACE_ENA",
	[VCAP_AF_CUSTOM_ACE_OFFSET]              =  "CUSTOM_ACE_OFFSET",
	[VCAP_AF_DEI_ENA]                        =  "DEI_ENA",
	[VCAP_AF_DEI_VAL]                        =  "DEI_VAL",
	[VCAP_AF_DLB_OFFSET]                     =  "DLB_OFFSET",
	[VCAP_AF_DMAC_OFFSET_ENA]                =  "DMAC_OFFSET_ENA",
	[VCAP_AF_DP_ENA]                         =  "DP_ENA",
	[VCAP_AF_DP_VAL]                         =  "DP_VAL",
	[VCAP_AF_DSCP_ENA]                       =  "DSCP_ENA",
	[VCAP_AF_DSCP_VAL]                       =  "DSCP_VAL",
	[VCAP_AF_EGR_ACL_ENA]                    =  "EGR_ACL_ENA",
	[VCAP_AF_ES2_REW_CMD]                    =  "ES2_REW_CMD",
	[VCAP_AF_FWD_DIS]                        =  "FWD_DIS",
	[VCAP_AF_FWD_MODE]                       =  "FWD_MODE",
	[VCAP_AF_FWD_TYPE]                       =  "FWD_TYPE",
	[VCAP_AF_GVID_ADD_REPLACE_SEL]           =  "GVID_ADD_REPLACE_SEL",
	[VCAP_AF_HIT_ME_ONCE]                    =  "HIT_ME_ONCE",
	[VCAP_AF_IGNORE_PIPELINE_CTRL]           =  "IGNORE_PIPELINE_CTRL",
	[VCAP_AF_IGR_ACL_ENA]                    =  "IGR_ACL_ENA",
	[VCAP_AF_INJ_MASQ_ENA]                   =  "INJ_MASQ_ENA",
	[VCAP_AF_INJ_MASQ_LPORT]                 =  "INJ_MASQ_LPORT",
	[VCAP_AF_INJ_MASQ_PORT]                  =  "INJ_MASQ_PORT",
	[VCAP_AF_INTR_ENA]                       =  "INTR_ENA",
	[VCAP_AF_ISDX_ADD_REPLACE_SEL]           =  "ISDX_ADD_REPLACE_SEL",
	[VCAP_AF_ISDX_VAL]                       =  "ISDX_VAL",
	[VCAP_AF_IS_INNER_ACL]                   =  "IS_INNER_ACL",
	[VCAP_AF_L3_MAC_UPDATE_DIS]              =  "L3_MAC_UPDATE_DIS",
	[VCAP_AF_LOG_MSG_INTERVAL]               =  "LOG_MSG_INTERVAL",
	[VCAP_AF_LPM_AFFIX_ENA]                  =  "LPM_AFFIX_ENA",
	[VCAP_AF_LPM_AFFIX_VAL]                  =  "LPM_AFFIX_VAL",
	[VCAP_AF_LPORT_ENA]                      =  "LPORT_ENA",
	[VCAP_AF_LRN_DIS]                        =  "LRN_DIS",
	[VCAP_AF_MAP_IDX]                        =  "MAP_IDX",
	[VCAP_AF_MAP_KEY]                        =  "MAP_KEY",
	[VCAP_AF_MAP_LOOKUP_SEL]                 =  "MAP_LOOKUP_SEL",
	[VCAP_AF_MASK_MODE]                      =  "MASK_MODE",
	[VCAP_AF_MATCH_ID]                       =  "MATCH_ID",
	[VCAP_AF_MATCH_ID_MASK]                  =  "MATCH_ID_MASK",
	[VCAP_AF_MIP_SEL]                        =  "MIP_SEL",
	[VCAP_AF_MIRROR_PROBE]                   =  "MIRROR_PROBE",
	[VCAP_AF_MIRROR_PROBE_ID]                =  "MIRROR_PROBE_ID",
	[VCAP_AF_MPLS_IP_CTRL_ENA]               =  "MPLS_IP_CTRL_ENA",
	[VCAP_AF_MPLS_MEP_ENA]                   =  "MPLS_MEP_ENA",
	[VCAP_AF_MPLS_MIP_ENA]                   =  "MPLS_MIP_ENA",
	[VCAP_AF_MPLS_OAM_FLAVOR]                =  "MPLS_OAM_FLAVOR",
	[VCAP_AF_MPLS_OAM_TYPE]                  =  "MPLS_OAM_TYPE",
	[VCAP_AF_NUM_VLD_LABELS]                 =  "NUM_VLD_LABELS",
	[VCAP_AF_NXT_IDX]                        =  "NXT_IDX",
	[VCAP_AF_NXT_IDX_CTRL]                   =  "NXT_IDX_CTRL",
	[VCAP_AF_NXT_KEY_TYPE]                   =  "NXT_KEY_TYPE",
	[VCAP_AF_NXT_NORMALIZE]                  =  "NXT_NORMALIZE",
	[VCAP_AF_NXT_NORM_W16_OFFSET]            =  "NXT_NORM_W16_OFFSET",
	[VCAP_AF_NXT_NORM_W32_OFFSET]            =  "NXT_NORM_W32_OFFSET",
	[VCAP_AF_NXT_OFFSET_FROM_TYPE]           =  "NXT_OFFSET_FROM_TYPE",
	[VCAP_AF_NXT_TYPE_AFTER_OFFSET]          =  "NXT_TYPE_AFTER_OFFSET",
	[VCAP_AF_OAM_IP_BFD_ENA]                 =  "OAM_IP_BFD_ENA",
	[VCAP_AF_OAM_TWAMP_ENA]                  =  "OAM_TWAMP_ENA",
	[VCAP_AF_OAM_Y1731_SEL]                  =  "OAM_Y1731_SEL",
	[VCAP_AF_PAG_OVERRIDE_MASK]              =  "PAG_OVERRIDE_MASK",
	[VCAP_AF_PAG_VAL]                        =  "PAG_VAL",
	[VCAP_AF_PCP_ENA]                        =  "PCP_ENA",
	[VCAP_AF_PCP_VAL]                        =  "PCP_VAL",
	[VCAP_AF_PIPELINE_ACT_SEL]               =  "PIPELINE_ACT_SEL",
	[VCAP_AF_PIPELINE_FORCE_ENA]             =  "PIPELINE_FORCE_ENA",
	[VCAP_AF_PIPELINE_PT]                    =  "PIPELINE_PT",
	[VCAP_AF_PIPELINE_PT_REDUCED]            =  "PIPELINE_PT_REDUCED",
	[VCAP_AF_POLICE_ENA]                     =  "POLICE_ENA",
	[VCAP_AF_POLICE_IDX]                     =  "POLICE_IDX",
	[VCAP_AF_POLICE_REMARK]                  =  "POLICE_REMARK",
	[VCAP_AF_PORT_MASK]                      =  "PORT_MASK",
	[VCAP_AF_PTP_MASTER_SEL]                 =  "PTP_MASTER_SEL",
	[VCAP_AF_QOS_ENA]                        =  "QOS_ENA",
	[VCAP_AF_QOS_VAL]                        =  "QOS_VAL",
	[VCAP_AF_REW_CMD]                        =  "REW_CMD",
	[VCAP_AF_RLEG_DMAC_CHK_DIS]              =  "RLEG_DMAC_CHK_DIS",
	[VCAP_AF_RLEG_STAT_IDX]                  =  "RLEG_STAT_IDX",
	[VCAP_AF_RSDX_ENA]                       =  "RSDX_ENA",
	[VCAP_AF_RSDX_VAL]                       =  "RSDX_VAL",
	[VCAP_AF_RSVD_LBL_VAL]                   =  "RSVD_LBL_VAL",
	[VCAP_AF_RT_DIS]                         =  "RT_DIS",
	[VCAP_AF_RT_SEL]                         =  "RT_SEL",
	[VCAP_AF_S2_KEY_SEL_ENA]                 =  "S2_KEY_SEL_ENA",
	[VCAP_AF_S2_KEY_SEL_IDX]                 =  "S2_KEY_SEL_IDX",
	[VCAP_AF_SAM_SEQ_ENA]                    =  "SAM_SEQ_ENA",
	[VCAP_AF_SIP_IDX]                        =  "SIP_IDX",
	[VCAP_AF_SWAP_MAC_ENA]                   =  "SWAP_MAC_ENA",
	[VCAP_AF_TCP_UDP_DPORT]                  =  "TCP_UDP_DPORT",
	[VCAP_AF_TCP_UDP_ENA]                    =  "TCP_UDP_ENA",
	[VCAP_AF_TCP_UDP_SPORT]                  =  "TCP_UDP_SPORT",
	[VCAP_AF_TC_ENA]                         =  "TC_ENA",
	[VCAP_AF_TC_LABEL]                       =  "TC_LABEL",
	[VCAP_AF_TPID_SEL]                       =  "TPID_SEL",
	[VCAP_AF_TTL_DECR_DIS]                   =  "TTL_DECR_DIS",
	[VCAP_AF_TTL_ENA]                        =  "TTL_ENA",
	[VCAP_AF_TTL_LABEL]                      =  "TTL_LABEL",
	[VCAP_AF_TTL_UPDATE_ENA]                 =  "TTL_UPDATE_ENA",
	[VCAP_AF_TYPE]                           =  "TYPE",
	[VCAP_AF_VID_VAL]                        =  "VID_VAL",
	[VCAP_AF_VLAN_POP_CNT]                   =  "VLAN_POP_CNT",
	[VCAP_AF_VLAN_POP_CNT_ENA]               =  "VLAN_POP_CNT_ENA",
	[VCAP_AF_VLAN_PUSH_CNT]                  =  "VLAN_PUSH_CNT",
	[VCAP_AF_VLAN_PUSH_CNT_ENA]              =  "VLAN_PUSH_CNT_ENA",
	[VCAP_AF_VLAN_WAS_TAGGED]                =  "VLAN_WAS_TAGGED",
};

/* VCAPs */
const struct vcap_info kunit_test_vcaps[] = {
	[VCAP_TYPE_IS0] = {
		.name = "is0",
		.rows = 1024,
		.sw_count = 12,
		.sw_width = 52,
		.sticky_width = 1,
		.act_width = 110,
		.default_cnt = 140,
		.require_cnt_dis = 0,
		.version = 1,
		.keyfield_set = is0_keyfield_set,
		.keyfield_set_size = ARRAY_SIZE(is0_keyfield_set),
		.actionfield_set = is0_actionfield_set,
		.actionfield_set_size = ARRAY_SIZE(is0_actionfield_set),
		.keyfield_set_map = is0_keyfield_set_map,
		.keyfield_set_map_size = is0_keyfield_set_map_size,
		.actionfield_set_map = is0_actionfield_set_map,
		.actionfield_set_map_size = is0_actionfield_set_map_size,
		.keyfield_set_typegroups = is0_keyfield_set_typegroups,
		.actionfield_set_typegroups = is0_actionfield_set_typegroups,
	},
	[VCAP_TYPE_IS2] = {
		.name = "is2",
		.rows = 256,
		.sw_count = 12,
		.sw_width = 52,
		.sticky_width = 1,
		.act_width = 110,
		.default_cnt = 73,
		.require_cnt_dis = 0,
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
	[VCAP_TYPE_ES2] = {
		.name = "es2",
		.rows = 1024,
		.sw_count = 12,
		.sw_width = 52,
		.sticky_width = 1,
		.act_width = 21,
		.default_cnt = 74,
		.require_cnt_dis = 0,
		.version = 1,
		.keyfield_set = es2_keyfield_set,
		.keyfield_set_size = ARRAY_SIZE(es2_keyfield_set),
		.actionfield_set = es2_actionfield_set,
		.actionfield_set_size = ARRAY_SIZE(es2_actionfield_set),
		.keyfield_set_map = es2_keyfield_set_map,
		.keyfield_set_map_size = es2_keyfield_set_map_size,
		.actionfield_set_map = es2_actionfield_set_map,
		.actionfield_set_map_size = es2_actionfield_set_map_size,
		.keyfield_set_typegroups = es2_keyfield_set_typegroups,
		.actionfield_set_typegroups = es2_actionfield_set_typegroups,
	},
};

const struct vcap_statistics kunit_test_vcap_stats = {
	.name = "kunit_test",
	.count = 3,
	.keyfield_set_names = vcap_keyfield_set_names,
	.actionfield_set_names = vcap_actionfield_set_names,
	.keyfield_names = vcap_keyfield_names,
	.actionfield_names = vcap_actionfield_names,
};
