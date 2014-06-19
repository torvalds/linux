/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV MMR definitions
 *
 * Copyright (C) 2007-2014 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_X86_UV_UV_MMRS_H
#define _ASM_X86_UV_UV_MMRS_H

/*
 * This file contains MMR definitions for all UV hubs types.
 *
 * To minimize coding differences between hub types, the symbols are
 * grouped by architecture types.
 *
 * UVH  - definitions common to all UV hub types.
 * UVXH - definitions common to all UV eXtended hub types (currently 2 & 3).
 * UV1H - definitions specific to UV type 1 hub.
 * UV2H - definitions specific to UV type 2 hub.
 * UV3H - definitions specific to UV type 3 hub.
 *
 * So in general, MMR addresses and structures are identical on all hubs types.
 * These MMRs are identified as:
 *	#define UVH_xxx		<address>
 *	union uvh_xxx {
 *		unsigned long       v;
 *		struct uvh_int_cmpd_s {
 *		} s;
 *	};
 *
 * If the MMR exists on all hub types but have different addresses:
 *	#define UV1Hxxx	a
 *	#define UV2Hxxx	b
 *	#define UV3Hxxx	c
 *	#define UVHxxx	(is_uv1_hub() ? UV1Hxxx :
 *			(is_uv2_hub() ? UV2Hxxx :
 *					UV3Hxxx))
 *
 * If the MMR exists on all hub types > 1 but have different addresses:
 *	#define UV2Hxxx	b
 *	#define UV3Hxxx	c
 *	#define UVXHxxx (is_uv2_hub() ? UV2Hxxx :
 *					UV3Hxxx))
 *
 *	union uvh_xxx {
 *		unsigned long       v;
 *		struct uvh_xxx_s {	 # Common fields only
 *		} s;
 *		struct uv1h_xxx_s {	 # Full UV1 definition (*)
 *		} s1;
 *		struct uv2h_xxx_s {	 # Full UV2 definition (*)
 *		} s2;
 *		struct uv3h_xxx_s {	 # Full UV3 definition (*)
 *		} s3;
 *	};
 *		(* - if present and different than the common struct)
 *
 * Only essential differences are enumerated. For example, if the address is
 * the same for all UV's, only a single #define is generated. Likewise,
 * if the contents is the same for all hubs, only the "s" structure is
 * generated.
 *
 * If the MMR exists on ONLY 1 type of hub, no generic definition is
 * generated:
 *	#define UVnH_xxx	<uvn address>
 *	union uvnh_xxx {
 *		unsigned long       v;
 *		struct uvh_int_cmpd_s {
 *		} sn;
 *	};
 *
 * (GEN Flags: mflags_opt= undefs=0 UV23=UVXH)
 */

#define UV_MMR_ENABLE		(1UL << 63)

#define UV1_HUB_PART_NUMBER	0x88a5
#define UV2_HUB_PART_NUMBER	0x8eb8
#define UV2_HUB_PART_NUMBER_X	0x1111
#define UV3_HUB_PART_NUMBER	0x9578
#define UV3_HUB_PART_NUMBER_X	0x4321

/* Compat: Indicate which UV Hubs are supported. */
#define UV2_HUB_IS_SUPPORTED	1
#define UV3_HUB_IS_SUPPORTED	1

/* ========================================================================= */
/*                          UVH_BAU_DATA_BROADCAST                           */
/* ========================================================================= */
#define UVH_BAU_DATA_BROADCAST 0x61688UL
#define UVH_BAU_DATA_BROADCAST_32 0x440

#define UVH_BAU_DATA_BROADCAST_ENABLE_SHFT		0
#define UVH_BAU_DATA_BROADCAST_ENABLE_MASK		0x0000000000000001UL

union uvh_bau_data_broadcast_u {
	unsigned long	v;
	struct uvh_bau_data_broadcast_s {
		unsigned long	enable:1;			/* RW */
		unsigned long	rsvd_1_63:63;
	} s;
};

/* ========================================================================= */
/*                           UVH_BAU_DATA_CONFIG                             */
/* ========================================================================= */
#define UVH_BAU_DATA_CONFIG 0x61680UL
#define UVH_BAU_DATA_CONFIG_32 0x438

#define UVH_BAU_DATA_CONFIG_VECTOR_SHFT			0
#define UVH_BAU_DATA_CONFIG_DM_SHFT			8
#define UVH_BAU_DATA_CONFIG_DESTMODE_SHFT		11
#define UVH_BAU_DATA_CONFIG_STATUS_SHFT			12
#define UVH_BAU_DATA_CONFIG_P_SHFT			13
#define UVH_BAU_DATA_CONFIG_T_SHFT			15
#define UVH_BAU_DATA_CONFIG_M_SHFT			16
#define UVH_BAU_DATA_CONFIG_APIC_ID_SHFT		32
#define UVH_BAU_DATA_CONFIG_VECTOR_MASK			0x00000000000000ffUL
#define UVH_BAU_DATA_CONFIG_DM_MASK			0x0000000000000700UL
#define UVH_BAU_DATA_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVH_BAU_DATA_CONFIG_STATUS_MASK			0x0000000000001000UL
#define UVH_BAU_DATA_CONFIG_P_MASK			0x0000000000002000UL
#define UVH_BAU_DATA_CONFIG_T_MASK			0x0000000000008000UL
#define UVH_BAU_DATA_CONFIG_M_MASK			0x0000000000010000UL
#define UVH_BAU_DATA_CONFIG_APIC_ID_MASK		0xffffffff00000000UL

union uvh_bau_data_config_u {
	unsigned long	v;
	struct uvh_bau_data_config_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	dm:3;				/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	status:1;			/* RO */
		unsigned long	p:1;				/* RO */
		unsigned long	rsvd_14:1;
		unsigned long	t:1;				/* RO */
		unsigned long	m:1;				/* RW */
		unsigned long	rsvd_17_31:15;
		unsigned long	apic_id:32;			/* RW */
	} s;
};

/* ========================================================================= */
/*                           UVH_EVENT_OCCURRED0                             */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED0 0x70000UL
#define UVH_EVENT_OCCURRED0_32 0x5e8

#define UVH_EVENT_OCCURRED0_LB_HCERR_SHFT		0
#define UVH_EVENT_OCCURRED0_RH_AOERR0_SHFT		11
#define UVH_EVENT_OCCURRED0_LB_HCERR_MASK		0x0000000000000001UL
#define UVH_EVENT_OCCURRED0_RH_AOERR0_MASK		0x0000000000000800UL

#define UV1H_EVENT_OCCURRED0_GR0_HCERR_SHFT		1
#define UV1H_EVENT_OCCURRED0_GR1_HCERR_SHFT		2
#define UV1H_EVENT_OCCURRED0_LH_HCERR_SHFT		3
#define UV1H_EVENT_OCCURRED0_RH_HCERR_SHFT		4
#define UV1H_EVENT_OCCURRED0_XN_HCERR_SHFT		5
#define UV1H_EVENT_OCCURRED0_SI_HCERR_SHFT		6
#define UV1H_EVENT_OCCURRED0_LB_AOERR0_SHFT		7
#define UV1H_EVENT_OCCURRED0_GR0_AOERR0_SHFT		8
#define UV1H_EVENT_OCCURRED0_GR1_AOERR0_SHFT		9
#define UV1H_EVENT_OCCURRED0_LH_AOERR0_SHFT		10
#define UV1H_EVENT_OCCURRED0_XN_AOERR0_SHFT		12
#define UV1H_EVENT_OCCURRED0_SI_AOERR0_SHFT		13
#define UV1H_EVENT_OCCURRED0_LB_AOERR1_SHFT		14
#define UV1H_EVENT_OCCURRED0_GR0_AOERR1_SHFT		15
#define UV1H_EVENT_OCCURRED0_GR1_AOERR1_SHFT		16
#define UV1H_EVENT_OCCURRED0_LH_AOERR1_SHFT		17
#define UV1H_EVENT_OCCURRED0_RH_AOERR1_SHFT		18
#define UV1H_EVENT_OCCURRED0_XN_AOERR1_SHFT		19
#define UV1H_EVENT_OCCURRED0_SI_AOERR1_SHFT		20
#define UV1H_EVENT_OCCURRED0_RH_VPI_INT_SHFT		21
#define UV1H_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_SHFT	22
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_0_SHFT		23
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_1_SHFT		24
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_2_SHFT		25
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_3_SHFT		26
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_4_SHFT		27
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_5_SHFT		28
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_6_SHFT		29
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_7_SHFT		30
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_8_SHFT		31
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_9_SHFT		32
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_10_SHFT		33
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_11_SHFT		34
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_12_SHFT		35
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_13_SHFT		36
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_14_SHFT		37
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_15_SHFT		38
#define UV1H_EVENT_OCCURRED0_L1_NMI_INT_SHFT		39
#define UV1H_EVENT_OCCURRED0_STOP_CLOCK_SHFT		40
#define UV1H_EVENT_OCCURRED0_ASIC_TO_L1_SHFT		41
#define UV1H_EVENT_OCCURRED0_L1_TO_ASIC_SHFT		42
#define UV1H_EVENT_OCCURRED0_LTC_INT_SHFT		43
#define UV1H_EVENT_OCCURRED0_LA_SEQ_TRIGGER_SHFT	44
#define UV1H_EVENT_OCCURRED0_IPI_INT_SHFT		45
#define UV1H_EVENT_OCCURRED0_EXTIO_INT0_SHFT		46
#define UV1H_EVENT_OCCURRED0_EXTIO_INT1_SHFT		47
#define UV1H_EVENT_OCCURRED0_EXTIO_INT2_SHFT		48
#define UV1H_EVENT_OCCURRED0_EXTIO_INT3_SHFT		49
#define UV1H_EVENT_OCCURRED0_PROFILE_INT_SHFT		50
#define UV1H_EVENT_OCCURRED0_RTC0_SHFT			51
#define UV1H_EVENT_OCCURRED0_RTC1_SHFT			52
#define UV1H_EVENT_OCCURRED0_RTC2_SHFT			53
#define UV1H_EVENT_OCCURRED0_RTC3_SHFT			54
#define UV1H_EVENT_OCCURRED0_BAU_DATA_SHFT		55
#define UV1H_EVENT_OCCURRED0_POWER_MANAGEMENT_REQ_SHFT	56
#define UV1H_EVENT_OCCURRED0_GR0_HCERR_MASK		0x0000000000000002UL
#define UV1H_EVENT_OCCURRED0_GR1_HCERR_MASK		0x0000000000000004UL
#define UV1H_EVENT_OCCURRED0_LH_HCERR_MASK		0x0000000000000008UL
#define UV1H_EVENT_OCCURRED0_RH_HCERR_MASK		0x0000000000000010UL
#define UV1H_EVENT_OCCURRED0_XN_HCERR_MASK		0x0000000000000020UL
#define UV1H_EVENT_OCCURRED0_SI_HCERR_MASK		0x0000000000000040UL
#define UV1H_EVENT_OCCURRED0_LB_AOERR0_MASK		0x0000000000000080UL
#define UV1H_EVENT_OCCURRED0_GR0_AOERR0_MASK		0x0000000000000100UL
#define UV1H_EVENT_OCCURRED0_GR1_AOERR0_MASK		0x0000000000000200UL
#define UV1H_EVENT_OCCURRED0_LH_AOERR0_MASK		0x0000000000000400UL
#define UV1H_EVENT_OCCURRED0_XN_AOERR0_MASK		0x0000000000001000UL
#define UV1H_EVENT_OCCURRED0_SI_AOERR0_MASK		0x0000000000002000UL
#define UV1H_EVENT_OCCURRED0_LB_AOERR1_MASK		0x0000000000004000UL
#define UV1H_EVENT_OCCURRED0_GR0_AOERR1_MASK		0x0000000000008000UL
#define UV1H_EVENT_OCCURRED0_GR1_AOERR1_MASK		0x0000000000010000UL
#define UV1H_EVENT_OCCURRED0_LH_AOERR1_MASK		0x0000000000020000UL
#define UV1H_EVENT_OCCURRED0_RH_AOERR1_MASK		0x0000000000040000UL
#define UV1H_EVENT_OCCURRED0_XN_AOERR1_MASK		0x0000000000080000UL
#define UV1H_EVENT_OCCURRED0_SI_AOERR1_MASK		0x0000000000100000UL
#define UV1H_EVENT_OCCURRED0_RH_VPI_INT_MASK		0x0000000000200000UL
#define UV1H_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_MASK	0x0000000000400000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_0_MASK		0x0000000000800000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_1_MASK		0x0000000001000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_2_MASK		0x0000000002000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_3_MASK		0x0000000004000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_4_MASK		0x0000000008000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_5_MASK		0x0000000010000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_6_MASK		0x0000000020000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_7_MASK		0x0000000040000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_8_MASK		0x0000000080000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_9_MASK		0x0000000100000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_10_MASK		0x0000000200000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_11_MASK		0x0000000400000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_12_MASK		0x0000000800000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_13_MASK		0x0000001000000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_14_MASK		0x0000002000000000UL
#define UV1H_EVENT_OCCURRED0_LB_IRQ_INT_15_MASK		0x0000004000000000UL
#define UV1H_EVENT_OCCURRED0_L1_NMI_INT_MASK		0x0000008000000000UL
#define UV1H_EVENT_OCCURRED0_STOP_CLOCK_MASK		0x0000010000000000UL
#define UV1H_EVENT_OCCURRED0_ASIC_TO_L1_MASK		0x0000020000000000UL
#define UV1H_EVENT_OCCURRED0_L1_TO_ASIC_MASK		0x0000040000000000UL
#define UV1H_EVENT_OCCURRED0_LTC_INT_MASK		0x0000080000000000UL
#define UV1H_EVENT_OCCURRED0_LA_SEQ_TRIGGER_MASK	0x0000100000000000UL
#define UV1H_EVENT_OCCURRED0_IPI_INT_MASK		0x0000200000000000UL
#define UV1H_EVENT_OCCURRED0_EXTIO_INT0_MASK		0x0000400000000000UL
#define UV1H_EVENT_OCCURRED0_EXTIO_INT1_MASK		0x0000800000000000UL
#define UV1H_EVENT_OCCURRED0_EXTIO_INT2_MASK		0x0001000000000000UL
#define UV1H_EVENT_OCCURRED0_EXTIO_INT3_MASK		0x0002000000000000UL
#define UV1H_EVENT_OCCURRED0_PROFILE_INT_MASK		0x0004000000000000UL
#define UV1H_EVENT_OCCURRED0_RTC0_MASK			0x0008000000000000UL
#define UV1H_EVENT_OCCURRED0_RTC1_MASK			0x0010000000000000UL
#define UV1H_EVENT_OCCURRED0_RTC2_MASK			0x0020000000000000UL
#define UV1H_EVENT_OCCURRED0_RTC3_MASK			0x0040000000000000UL
#define UV1H_EVENT_OCCURRED0_BAU_DATA_MASK		0x0080000000000000UL
#define UV1H_EVENT_OCCURRED0_POWER_MANAGEMENT_REQ_MASK	0x0100000000000000UL

#define UVXH_EVENT_OCCURRED0_QP_HCERR_SHFT		1
#define UVXH_EVENT_OCCURRED0_RH_HCERR_SHFT		2
#define UVXH_EVENT_OCCURRED0_LH0_HCERR_SHFT		3
#define UVXH_EVENT_OCCURRED0_LH1_HCERR_SHFT		4
#define UVXH_EVENT_OCCURRED0_GR0_HCERR_SHFT		5
#define UVXH_EVENT_OCCURRED0_GR1_HCERR_SHFT		6
#define UVXH_EVENT_OCCURRED0_NI0_HCERR_SHFT		7
#define UVXH_EVENT_OCCURRED0_NI1_HCERR_SHFT		8
#define UVXH_EVENT_OCCURRED0_LB_AOERR0_SHFT		9
#define UVXH_EVENT_OCCURRED0_QP_AOERR0_SHFT		10
#define UVXH_EVENT_OCCURRED0_LH0_AOERR0_SHFT		12
#define UVXH_EVENT_OCCURRED0_LH1_AOERR0_SHFT		13
#define UVXH_EVENT_OCCURRED0_GR0_AOERR0_SHFT		14
#define UVXH_EVENT_OCCURRED0_GR1_AOERR0_SHFT		15
#define UVXH_EVENT_OCCURRED0_XB_AOERR0_SHFT		16
#define UVXH_EVENT_OCCURRED0_RT_AOERR0_SHFT		17
#define UVXH_EVENT_OCCURRED0_NI0_AOERR0_SHFT		18
#define UVXH_EVENT_OCCURRED0_NI1_AOERR0_SHFT		19
#define UVXH_EVENT_OCCURRED0_LB_AOERR1_SHFT		20
#define UVXH_EVENT_OCCURRED0_QP_AOERR1_SHFT		21
#define UVXH_EVENT_OCCURRED0_RH_AOERR1_SHFT		22
#define UVXH_EVENT_OCCURRED0_LH0_AOERR1_SHFT		23
#define UVXH_EVENT_OCCURRED0_LH1_AOERR1_SHFT		24
#define UVXH_EVENT_OCCURRED0_GR0_AOERR1_SHFT		25
#define UVXH_EVENT_OCCURRED0_GR1_AOERR1_SHFT		26
#define UVXH_EVENT_OCCURRED0_XB_AOERR1_SHFT		27
#define UVXH_EVENT_OCCURRED0_RT_AOERR1_SHFT		28
#define UVXH_EVENT_OCCURRED0_NI0_AOERR1_SHFT		29
#define UVXH_EVENT_OCCURRED0_NI1_AOERR1_SHFT		30
#define UVXH_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_SHFT	31
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_0_SHFT		32
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_1_SHFT		33
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_2_SHFT		34
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_3_SHFT		35
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_4_SHFT		36
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_5_SHFT		37
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_6_SHFT		38
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_7_SHFT		39
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_8_SHFT		40
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_9_SHFT		41
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_10_SHFT		42
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_11_SHFT		43
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_12_SHFT		44
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_13_SHFT		45
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_14_SHFT		46
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_15_SHFT		47
#define UVXH_EVENT_OCCURRED0_L1_NMI_INT_SHFT		48
#define UVXH_EVENT_OCCURRED0_STOP_CLOCK_SHFT		49
#define UVXH_EVENT_OCCURRED0_ASIC_TO_L1_SHFT		50
#define UVXH_EVENT_OCCURRED0_L1_TO_ASIC_SHFT		51
#define UVXH_EVENT_OCCURRED0_LA_SEQ_TRIGGER_SHFT	52
#define UVXH_EVENT_OCCURRED0_IPI_INT_SHFT		53
#define UVXH_EVENT_OCCURRED0_EXTIO_INT0_SHFT		54
#define UVXH_EVENT_OCCURRED0_EXTIO_INT1_SHFT		55
#define UVXH_EVENT_OCCURRED0_EXTIO_INT2_SHFT		56
#define UVXH_EVENT_OCCURRED0_EXTIO_INT3_SHFT		57
#define UVXH_EVENT_OCCURRED0_PROFILE_INT_SHFT		58
#define UVXH_EVENT_OCCURRED0_QP_HCERR_MASK		0x0000000000000002UL
#define UVXH_EVENT_OCCURRED0_RH_HCERR_MASK		0x0000000000000004UL
#define UVXH_EVENT_OCCURRED0_LH0_HCERR_MASK		0x0000000000000008UL
#define UVXH_EVENT_OCCURRED0_LH1_HCERR_MASK		0x0000000000000010UL
#define UVXH_EVENT_OCCURRED0_GR0_HCERR_MASK		0x0000000000000020UL
#define UVXH_EVENT_OCCURRED0_GR1_HCERR_MASK		0x0000000000000040UL
#define UVXH_EVENT_OCCURRED0_NI0_HCERR_MASK		0x0000000000000080UL
#define UVXH_EVENT_OCCURRED0_NI1_HCERR_MASK		0x0000000000000100UL
#define UVXH_EVENT_OCCURRED0_LB_AOERR0_MASK		0x0000000000000200UL
#define UVXH_EVENT_OCCURRED0_QP_AOERR0_MASK		0x0000000000000400UL
#define UVXH_EVENT_OCCURRED0_LH0_AOERR0_MASK		0x0000000000001000UL
#define UVXH_EVENT_OCCURRED0_LH1_AOERR0_MASK		0x0000000000002000UL
#define UVXH_EVENT_OCCURRED0_GR0_AOERR0_MASK		0x0000000000004000UL
#define UVXH_EVENT_OCCURRED0_GR1_AOERR0_MASK		0x0000000000008000UL
#define UVXH_EVENT_OCCURRED0_XB_AOERR0_MASK		0x0000000000010000UL
#define UVXH_EVENT_OCCURRED0_RT_AOERR0_MASK		0x0000000000020000UL
#define UVXH_EVENT_OCCURRED0_NI0_AOERR0_MASK		0x0000000000040000UL
#define UVXH_EVENT_OCCURRED0_NI1_AOERR0_MASK		0x0000000000080000UL
#define UVXH_EVENT_OCCURRED0_LB_AOERR1_MASK		0x0000000000100000UL
#define UVXH_EVENT_OCCURRED0_QP_AOERR1_MASK		0x0000000000200000UL
#define UVXH_EVENT_OCCURRED0_RH_AOERR1_MASK		0x0000000000400000UL
#define UVXH_EVENT_OCCURRED0_LH0_AOERR1_MASK		0x0000000000800000UL
#define UVXH_EVENT_OCCURRED0_LH1_AOERR1_MASK		0x0000000001000000UL
#define UVXH_EVENT_OCCURRED0_GR0_AOERR1_MASK		0x0000000002000000UL
#define UVXH_EVENT_OCCURRED0_GR1_AOERR1_MASK		0x0000000004000000UL
#define UVXH_EVENT_OCCURRED0_XB_AOERR1_MASK		0x0000000008000000UL
#define UVXH_EVENT_OCCURRED0_RT_AOERR1_MASK		0x0000000010000000UL
#define UVXH_EVENT_OCCURRED0_NI0_AOERR1_MASK		0x0000000020000000UL
#define UVXH_EVENT_OCCURRED0_NI1_AOERR1_MASK		0x0000000040000000UL
#define UVXH_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_MASK	0x0000000080000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_0_MASK		0x0000000100000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_1_MASK		0x0000000200000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_2_MASK		0x0000000400000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_3_MASK		0x0000000800000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_4_MASK		0x0000001000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_5_MASK		0x0000002000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_6_MASK		0x0000004000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_7_MASK		0x0000008000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_8_MASK		0x0000010000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_9_MASK		0x0000020000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_10_MASK		0x0000040000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_11_MASK		0x0000080000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_12_MASK		0x0000100000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_13_MASK		0x0000200000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_14_MASK		0x0000400000000000UL
#define UVXH_EVENT_OCCURRED0_LB_IRQ_INT_15_MASK		0x0000800000000000UL
#define UVXH_EVENT_OCCURRED0_L1_NMI_INT_MASK		0x0001000000000000UL
#define UVXH_EVENT_OCCURRED0_STOP_CLOCK_MASK		0x0002000000000000UL
#define UVXH_EVENT_OCCURRED0_ASIC_TO_L1_MASK		0x0004000000000000UL
#define UVXH_EVENT_OCCURRED0_L1_TO_ASIC_MASK		0x0008000000000000UL
#define UVXH_EVENT_OCCURRED0_LA_SEQ_TRIGGER_MASK	0x0010000000000000UL
#define UVXH_EVENT_OCCURRED0_IPI_INT_MASK		0x0020000000000000UL
#define UVXH_EVENT_OCCURRED0_EXTIO_INT0_MASK		0x0040000000000000UL
#define UVXH_EVENT_OCCURRED0_EXTIO_INT1_MASK		0x0080000000000000UL
#define UVXH_EVENT_OCCURRED0_EXTIO_INT2_MASK		0x0100000000000000UL
#define UVXH_EVENT_OCCURRED0_EXTIO_INT3_MASK		0x0200000000000000UL
#define UVXH_EVENT_OCCURRED0_PROFILE_INT_MASK		0x0400000000000000UL

union uvh_event_occurred0_u {
	unsigned long	v;
	struct uvh_event_occurred0_s {
		unsigned long	lb_hcerr:1;			/* RW, W1C */
		unsigned long	rsvd_1_10:10;
		unsigned long	rh_aoerr0:1;			/* RW, W1C */
		unsigned long	rsvd_12_63:52;
	} s;
	struct uvxh_event_occurred0_s {
		unsigned long	lb_hcerr:1;			/* RW */
		unsigned long	qp_hcerr:1;			/* RW */
		unsigned long	rh_hcerr:1;			/* RW */
		unsigned long	lh0_hcerr:1;			/* RW */
		unsigned long	lh1_hcerr:1;			/* RW */
		unsigned long	gr0_hcerr:1;			/* RW */
		unsigned long	gr1_hcerr:1;			/* RW */
		unsigned long	ni0_hcerr:1;			/* RW */
		unsigned long	ni1_hcerr:1;			/* RW */
		unsigned long	lb_aoerr0:1;			/* RW */
		unsigned long	qp_aoerr0:1;			/* RW */
		unsigned long	rh_aoerr0:1;			/* RW */
		unsigned long	lh0_aoerr0:1;			/* RW */
		unsigned long	lh1_aoerr0:1;			/* RW */
		unsigned long	gr0_aoerr0:1;			/* RW */
		unsigned long	gr1_aoerr0:1;			/* RW */
		unsigned long	xb_aoerr0:1;			/* RW */
		unsigned long	rt_aoerr0:1;			/* RW */
		unsigned long	ni0_aoerr0:1;			/* RW */
		unsigned long	ni1_aoerr0:1;			/* RW */
		unsigned long	lb_aoerr1:1;			/* RW */
		unsigned long	qp_aoerr1:1;			/* RW */
		unsigned long	rh_aoerr1:1;			/* RW */
		unsigned long	lh0_aoerr1:1;			/* RW */
		unsigned long	lh1_aoerr1:1;			/* RW */
		unsigned long	gr0_aoerr1:1;			/* RW */
		unsigned long	gr1_aoerr1:1;			/* RW */
		unsigned long	xb_aoerr1:1;			/* RW */
		unsigned long	rt_aoerr1:1;			/* RW */
		unsigned long	ni0_aoerr1:1;			/* RW */
		unsigned long	ni1_aoerr1:1;			/* RW */
		unsigned long	system_shutdown_int:1;		/* RW */
		unsigned long	lb_irq_int_0:1;			/* RW */
		unsigned long	lb_irq_int_1:1;			/* RW */
		unsigned long	lb_irq_int_2:1;			/* RW */
		unsigned long	lb_irq_int_3:1;			/* RW */
		unsigned long	lb_irq_int_4:1;			/* RW */
		unsigned long	lb_irq_int_5:1;			/* RW */
		unsigned long	lb_irq_int_6:1;			/* RW */
		unsigned long	lb_irq_int_7:1;			/* RW */
		unsigned long	lb_irq_int_8:1;			/* RW */
		unsigned long	lb_irq_int_9:1;			/* RW */
		unsigned long	lb_irq_int_10:1;		/* RW */
		unsigned long	lb_irq_int_11:1;		/* RW */
		unsigned long	lb_irq_int_12:1;		/* RW */
		unsigned long	lb_irq_int_13:1;		/* RW */
		unsigned long	lb_irq_int_14:1;		/* RW */
		unsigned long	lb_irq_int_15:1;		/* RW */
		unsigned long	l1_nmi_int:1;			/* RW */
		unsigned long	stop_clock:1;			/* RW */
		unsigned long	asic_to_l1:1;			/* RW */
		unsigned long	l1_to_asic:1;			/* RW */
		unsigned long	la_seq_trigger:1;		/* RW */
		unsigned long	ipi_int:1;			/* RW */
		unsigned long	extio_int0:1;			/* RW */
		unsigned long	extio_int1:1;			/* RW */
		unsigned long	extio_int2:1;			/* RW */
		unsigned long	extio_int3:1;			/* RW */
		unsigned long	profile_int:1;			/* RW */
		unsigned long	rsvd_59_63:5;
	} sx;
};

/* ========================================================================= */
/*                        UVH_EVENT_OCCURRED0_ALIAS                          */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED0_ALIAS 0x70008UL
#define UVH_EVENT_OCCURRED0_ALIAS_32 0x5f0


/* ========================================================================= */
/*                         UVH_EXTIO_INT0_BROADCAST                          */
/* ========================================================================= */
#define UVH_EXTIO_INT0_BROADCAST 0x61448UL
#define UVH_EXTIO_INT0_BROADCAST_32 0x3f0

#define UVH_EXTIO_INT0_BROADCAST_ENABLE_SHFT		0
#define UVH_EXTIO_INT0_BROADCAST_ENABLE_MASK		0x0000000000000001UL

union uvh_extio_int0_broadcast_u {
	unsigned long	v;
	struct uvh_extio_int0_broadcast_s {
		unsigned long	enable:1;			/* RW */
		unsigned long	rsvd_1_63:63;
	} s;
};

/* ========================================================================= */
/*                         UVH_GR0_TLB_INT0_CONFIG                           */
/* ========================================================================= */
#define UVH_GR0_TLB_INT0_CONFIG 0x61b00UL

#define UVH_GR0_TLB_INT0_CONFIG_VECTOR_SHFT		0
#define UVH_GR0_TLB_INT0_CONFIG_DM_SHFT			8
#define UVH_GR0_TLB_INT0_CONFIG_DESTMODE_SHFT		11
#define UVH_GR0_TLB_INT0_CONFIG_STATUS_SHFT		12
#define UVH_GR0_TLB_INT0_CONFIG_P_SHFT			13
#define UVH_GR0_TLB_INT0_CONFIG_T_SHFT			15
#define UVH_GR0_TLB_INT0_CONFIG_M_SHFT			16
#define UVH_GR0_TLB_INT0_CONFIG_APIC_ID_SHFT		32
#define UVH_GR0_TLB_INT0_CONFIG_VECTOR_MASK		0x00000000000000ffUL
#define UVH_GR0_TLB_INT0_CONFIG_DM_MASK			0x0000000000000700UL
#define UVH_GR0_TLB_INT0_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVH_GR0_TLB_INT0_CONFIG_STATUS_MASK		0x0000000000001000UL
#define UVH_GR0_TLB_INT0_CONFIG_P_MASK			0x0000000000002000UL
#define UVH_GR0_TLB_INT0_CONFIG_T_MASK			0x0000000000008000UL
#define UVH_GR0_TLB_INT0_CONFIG_M_MASK			0x0000000000010000UL
#define UVH_GR0_TLB_INT0_CONFIG_APIC_ID_MASK		0xffffffff00000000UL

union uvh_gr0_tlb_int0_config_u {
	unsigned long	v;
	struct uvh_gr0_tlb_int0_config_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	dm:3;				/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	status:1;			/* RO */
		unsigned long	p:1;				/* RO */
		unsigned long	rsvd_14:1;
		unsigned long	t:1;				/* RO */
		unsigned long	m:1;				/* RW */
		unsigned long	rsvd_17_31:15;
		unsigned long	apic_id:32;			/* RW */
	} s;
};

/* ========================================================================= */
/*                         UVH_GR0_TLB_INT1_CONFIG                           */
/* ========================================================================= */
#define UVH_GR0_TLB_INT1_CONFIG 0x61b40UL

#define UVH_GR0_TLB_INT1_CONFIG_VECTOR_SHFT		0
#define UVH_GR0_TLB_INT1_CONFIG_DM_SHFT			8
#define UVH_GR0_TLB_INT1_CONFIG_DESTMODE_SHFT		11
#define UVH_GR0_TLB_INT1_CONFIG_STATUS_SHFT		12
#define UVH_GR0_TLB_INT1_CONFIG_P_SHFT			13
#define UVH_GR0_TLB_INT1_CONFIG_T_SHFT			15
#define UVH_GR0_TLB_INT1_CONFIG_M_SHFT			16
#define UVH_GR0_TLB_INT1_CONFIG_APIC_ID_SHFT		32
#define UVH_GR0_TLB_INT1_CONFIG_VECTOR_MASK		0x00000000000000ffUL
#define UVH_GR0_TLB_INT1_CONFIG_DM_MASK			0x0000000000000700UL
#define UVH_GR0_TLB_INT1_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVH_GR0_TLB_INT1_CONFIG_STATUS_MASK		0x0000000000001000UL
#define UVH_GR0_TLB_INT1_CONFIG_P_MASK			0x0000000000002000UL
#define UVH_GR0_TLB_INT1_CONFIG_T_MASK			0x0000000000008000UL
#define UVH_GR0_TLB_INT1_CONFIG_M_MASK			0x0000000000010000UL
#define UVH_GR0_TLB_INT1_CONFIG_APIC_ID_MASK		0xffffffff00000000UL

union uvh_gr0_tlb_int1_config_u {
	unsigned long	v;
	struct uvh_gr0_tlb_int1_config_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	dm:3;				/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	status:1;			/* RO */
		unsigned long	p:1;				/* RO */
		unsigned long	rsvd_14:1;
		unsigned long	t:1;				/* RO */
		unsigned long	m:1;				/* RW */
		unsigned long	rsvd_17_31:15;
		unsigned long	apic_id:32;			/* RW */
	} s;
};

/* ========================================================================= */
/*                         UVH_GR0_TLB_MMR_CONTROL                           */
/* ========================================================================= */
#define UV1H_GR0_TLB_MMR_CONTROL 0x401080UL
#define UV2H_GR0_TLB_MMR_CONTROL 0xc01080UL
#define UV3H_GR0_TLB_MMR_CONTROL 0xc01080UL
#define UVH_GR0_TLB_MMR_CONTROL						\
		(is_uv1_hub() ? UV1H_GR0_TLB_MMR_CONTROL :		\
		(is_uv2_hub() ? UV2H_GR0_TLB_MMR_CONTROL :		\
				UV3H_GR0_TLB_MMR_CONTROL))

#define UVH_GR0_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UVH_GR0_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UVH_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UVH_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UVH_GR0_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UVH_GR0_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UVH_GR0_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UVH_GR0_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UVH_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UVH_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UVH_GR0_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UVH_GR0_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL

#define UV1H_GR0_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UV1H_GR0_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UV1H_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_CON_SHFT	48
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBRAM_SHFT	52
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBPGSIZE_SHFT	54
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBRREG_SHFT	56
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBLRUV_SHFT	60
#define UV1H_GR0_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UV1H_GR0_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UV1H_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_CON_MASK	0x0001000000000000UL
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBRAM_MASK	0x0010000000000000UL
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBPGSIZE_MASK	0x0040000000000000UL
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBRREG_MASK	0x0100000000000000UL
#define UV1H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBLRUV_MASK	0x1000000000000000UL

#define UVXH_GR0_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UVXH_GR0_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UVXH_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UVXH_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UVXH_GR0_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UVXH_GR0_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UVXH_GR0_TLB_MMR_CONTROL_MMR_OP_DONE_SHFT	32
#define UVXH_GR0_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UVXH_GR0_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UVXH_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UVXH_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UVXH_GR0_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UVXH_GR0_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL
#define UVXH_GR0_TLB_MMR_CONTROL_MMR_OP_DONE_MASK	0x0000000100000000UL

#define UV2H_GR0_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UV2H_GR0_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UV2H_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_OP_DONE_SHFT	32
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_INJ_CON_SHFT	48
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBRAM_SHFT	52
#define UV2H_GR0_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UV2H_GR0_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UV2H_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_OP_DONE_MASK	0x0000000100000000UL
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_INJ_CON_MASK	0x0001000000000000UL
#define UV2H_GR0_TLB_MMR_CONTROL_MMR_INJ_TLBRAM_MASK	0x0010000000000000UL

#define UV3H_GR0_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UV3H_GR0_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UV3H_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UV3H_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UV3H_GR0_TLB_MMR_CONTROL_ECC_SEL_SHFT		21
#define UV3H_GR0_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UV3H_GR0_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UV3H_GR0_TLB_MMR_CONTROL_MMR_OP_DONE_SHFT	32
#define UV3H_GR0_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UV3H_GR0_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UV3H_GR0_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UV3H_GR0_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UV3H_GR0_TLB_MMR_CONTROL_ECC_SEL_MASK		0x0000000000200000UL
#define UV3H_GR0_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UV3H_GR0_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL
#define UV3H_GR0_TLB_MMR_CONTROL_MMR_OP_DONE_MASK	0x0000000100000000UL

union uvh_gr0_tlb_mmr_control_u {
	unsigned long	v;
	struct uvh_gr0_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	rsvd_21_29:9;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	rsvd_32_48:17;
		unsigned long	rsvd_49_51:3;
		unsigned long	rsvd_52_63:12;
	} s;
	struct uv1h_gr0_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	rsvd_21_29:9;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	rsvd_32_47:16;
		unsigned long	mmr_inj_con:1;			/* RW */
		unsigned long	rsvd_49_51:3;
		unsigned long	mmr_inj_tlbram:1;		/* RW */
		unsigned long	rsvd_53:1;
		unsigned long	mmr_inj_tlbpgsize:1;		/* RW */
		unsigned long	rsvd_55:1;
		unsigned long	mmr_inj_tlbrreg:1;		/* RW */
		unsigned long	rsvd_57_59:3;
		unsigned long	mmr_inj_tlblruv:1;		/* RW */
		unsigned long	rsvd_61_63:3;
	} s1;
	struct uvxh_gr0_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	rsvd_21_29:9;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	mmr_op_done:1;			/* RW */
		unsigned long	rsvd_33_47:15;
		unsigned long	rsvd_48:1;
		unsigned long	rsvd_49_51:3;
		unsigned long	rsvd_52:1;
		unsigned long	rsvd_53_63:11;
	} sx;
	struct uv2h_gr0_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	rsvd_21_29:9;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	mmr_op_done:1;			/* RW */
		unsigned long	rsvd_33_47:15;
		unsigned long	mmr_inj_con:1;			/* RW */
		unsigned long	rsvd_49_51:3;
		unsigned long	mmr_inj_tlbram:1;		/* RW */
		unsigned long	rsvd_53_63:11;
	} s2;
	struct uv3h_gr0_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	ecc_sel:1;			/* RW */
		unsigned long	rsvd_22_29:8;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	mmr_op_done:1;			/* RW */
		unsigned long	rsvd_33_47:15;
		unsigned long	undef_48:1;			/* Undefined */
		unsigned long	rsvd_49_51:3;
		unsigned long	undef_52:1;			/* Undefined */
		unsigned long	rsvd_53_63:11;
	} s3;
};

/* ========================================================================= */
/*                       UVH_GR0_TLB_MMR_READ_DATA_HI                        */
/* ========================================================================= */
#define UV1H_GR0_TLB_MMR_READ_DATA_HI 0x4010a0UL
#define UV2H_GR0_TLB_MMR_READ_DATA_HI 0xc010a0UL
#define UV3H_GR0_TLB_MMR_READ_DATA_HI 0xc010a0UL
#define UVH_GR0_TLB_MMR_READ_DATA_HI					\
		(is_uv1_hub() ? UV1H_GR0_TLB_MMR_READ_DATA_HI :		\
		(is_uv2_hub() ? UV2H_GR0_TLB_MMR_READ_DATA_HI :		\
				UV3H_GR0_TLB_MMR_READ_DATA_HI))

#define UVH_GR0_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UVH_GR0_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UVH_GR0_TLB_MMR_READ_DATA_HI_DIRTY_SHFT		43
#define UVH_GR0_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UVH_GR0_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UVH_GR0_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UVH_GR0_TLB_MMR_READ_DATA_HI_DIRTY_MASK		0x0000080000000000UL
#define UVH_GR0_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL

#define UV1H_GR0_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UV1H_GR0_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UV1H_GR0_TLB_MMR_READ_DATA_HI_DIRTY_SHFT	43
#define UV1H_GR0_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UV1H_GR0_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UV1H_GR0_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UV1H_GR0_TLB_MMR_READ_DATA_HI_DIRTY_MASK	0x0000080000000000UL
#define UV1H_GR0_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL

#define UVXH_GR0_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UVXH_GR0_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UVXH_GR0_TLB_MMR_READ_DATA_HI_DIRTY_SHFT	43
#define UVXH_GR0_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UVXH_GR0_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UVXH_GR0_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UVXH_GR0_TLB_MMR_READ_DATA_HI_DIRTY_MASK	0x0000080000000000UL
#define UVXH_GR0_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL

#define UV2H_GR0_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UV2H_GR0_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UV2H_GR0_TLB_MMR_READ_DATA_HI_DIRTY_SHFT	43
#define UV2H_GR0_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UV2H_GR0_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UV2H_GR0_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UV2H_GR0_TLB_MMR_READ_DATA_HI_DIRTY_MASK	0x0000080000000000UL
#define UV2H_GR0_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL

#define UV3H_GR0_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_DIRTY_SHFT	43
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_AA_EXT_SHFT	45
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_WAY_ECC_SHFT	55
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_DIRTY_MASK	0x0000080000000000UL
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_AA_EXT_MASK	0x0000200000000000UL
#define UV3H_GR0_TLB_MMR_READ_DATA_HI_WAY_ECC_MASK	0xff80000000000000UL

union uvh_gr0_tlb_mmr_read_data_hi_u {
	unsigned long	v;
	struct uvh_gr0_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	rsvd_45_63:19;
	} s;
	struct uv1h_gr0_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	rsvd_45_63:19;
	} s1;
	struct uvxh_gr0_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	rsvd_45_63:19;
	} sx;
	struct uv2h_gr0_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	rsvd_45_63:19;
	} s2;
	struct uv3h_gr0_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	aa_ext:1;			/* RO */
		unsigned long	undef_46_54:9;			/* Undefined */
		unsigned long	way_ecc:9;			/* RO */
	} s3;
};

/* ========================================================================= */
/*                       UVH_GR0_TLB_MMR_READ_DATA_LO                        */
/* ========================================================================= */
#define UV1H_GR0_TLB_MMR_READ_DATA_LO 0x4010a8UL
#define UV2H_GR0_TLB_MMR_READ_DATA_LO 0xc010a8UL
#define UV3H_GR0_TLB_MMR_READ_DATA_LO 0xc010a8UL
#define UVH_GR0_TLB_MMR_READ_DATA_LO					\
		(is_uv1_hub() ? UV1H_GR0_TLB_MMR_READ_DATA_LO :		\
		(is_uv2_hub() ? UV2H_GR0_TLB_MMR_READ_DATA_LO :		\
				UV3H_GR0_TLB_MMR_READ_DATA_LO))

#define UVH_GR0_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UVH_GR0_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UVH_GR0_TLB_MMR_READ_DATA_LO_VALID_SHFT		63
#define UVH_GR0_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UVH_GR0_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UVH_GR0_TLB_MMR_READ_DATA_LO_VALID_MASK		0x8000000000000000UL

#define UV1H_GR0_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UV1H_GR0_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UV1H_GR0_TLB_MMR_READ_DATA_LO_VALID_SHFT	63
#define UV1H_GR0_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UV1H_GR0_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UV1H_GR0_TLB_MMR_READ_DATA_LO_VALID_MASK	0x8000000000000000UL

#define UVXH_GR0_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UVXH_GR0_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UVXH_GR0_TLB_MMR_READ_DATA_LO_VALID_SHFT	63
#define UVXH_GR0_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UVXH_GR0_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UVXH_GR0_TLB_MMR_READ_DATA_LO_VALID_MASK	0x8000000000000000UL

#define UV2H_GR0_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UV2H_GR0_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UV2H_GR0_TLB_MMR_READ_DATA_LO_VALID_SHFT	63
#define UV2H_GR0_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UV2H_GR0_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UV2H_GR0_TLB_MMR_READ_DATA_LO_VALID_MASK	0x8000000000000000UL

#define UV3H_GR0_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UV3H_GR0_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UV3H_GR0_TLB_MMR_READ_DATA_LO_VALID_SHFT	63
#define UV3H_GR0_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UV3H_GR0_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UV3H_GR0_TLB_MMR_READ_DATA_LO_VALID_MASK	0x8000000000000000UL

union uvh_gr0_tlb_mmr_read_data_lo_u {
	unsigned long	v;
	struct uvh_gr0_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} s;
	struct uv1h_gr0_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} s1;
	struct uvxh_gr0_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} sx;
	struct uv2h_gr0_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} s2;
	struct uv3h_gr0_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} s3;
};

/* ========================================================================= */
/*                         UVH_GR1_TLB_INT0_CONFIG                           */
/* ========================================================================= */
#define UVH_GR1_TLB_INT0_CONFIG 0x61f00UL

#define UVH_GR1_TLB_INT0_CONFIG_VECTOR_SHFT		0
#define UVH_GR1_TLB_INT0_CONFIG_DM_SHFT			8
#define UVH_GR1_TLB_INT0_CONFIG_DESTMODE_SHFT		11
#define UVH_GR1_TLB_INT0_CONFIG_STATUS_SHFT		12
#define UVH_GR1_TLB_INT0_CONFIG_P_SHFT			13
#define UVH_GR1_TLB_INT0_CONFIG_T_SHFT			15
#define UVH_GR1_TLB_INT0_CONFIG_M_SHFT			16
#define UVH_GR1_TLB_INT0_CONFIG_APIC_ID_SHFT		32
#define UVH_GR1_TLB_INT0_CONFIG_VECTOR_MASK		0x00000000000000ffUL
#define UVH_GR1_TLB_INT0_CONFIG_DM_MASK			0x0000000000000700UL
#define UVH_GR1_TLB_INT0_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVH_GR1_TLB_INT0_CONFIG_STATUS_MASK		0x0000000000001000UL
#define UVH_GR1_TLB_INT0_CONFIG_P_MASK			0x0000000000002000UL
#define UVH_GR1_TLB_INT0_CONFIG_T_MASK			0x0000000000008000UL
#define UVH_GR1_TLB_INT0_CONFIG_M_MASK			0x0000000000010000UL
#define UVH_GR1_TLB_INT0_CONFIG_APIC_ID_MASK		0xffffffff00000000UL

union uvh_gr1_tlb_int0_config_u {
	unsigned long	v;
	struct uvh_gr1_tlb_int0_config_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	dm:3;				/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	status:1;			/* RO */
		unsigned long	p:1;				/* RO */
		unsigned long	rsvd_14:1;
		unsigned long	t:1;				/* RO */
		unsigned long	m:1;				/* RW */
		unsigned long	rsvd_17_31:15;
		unsigned long	apic_id:32;			/* RW */
	} s;
};

/* ========================================================================= */
/*                         UVH_GR1_TLB_INT1_CONFIG                           */
/* ========================================================================= */
#define UVH_GR1_TLB_INT1_CONFIG 0x61f40UL

#define UVH_GR1_TLB_INT1_CONFIG_VECTOR_SHFT		0
#define UVH_GR1_TLB_INT1_CONFIG_DM_SHFT			8
#define UVH_GR1_TLB_INT1_CONFIG_DESTMODE_SHFT		11
#define UVH_GR1_TLB_INT1_CONFIG_STATUS_SHFT		12
#define UVH_GR1_TLB_INT1_CONFIG_P_SHFT			13
#define UVH_GR1_TLB_INT1_CONFIG_T_SHFT			15
#define UVH_GR1_TLB_INT1_CONFIG_M_SHFT			16
#define UVH_GR1_TLB_INT1_CONFIG_APIC_ID_SHFT		32
#define UVH_GR1_TLB_INT1_CONFIG_VECTOR_MASK		0x00000000000000ffUL
#define UVH_GR1_TLB_INT1_CONFIG_DM_MASK			0x0000000000000700UL
#define UVH_GR1_TLB_INT1_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVH_GR1_TLB_INT1_CONFIG_STATUS_MASK		0x0000000000001000UL
#define UVH_GR1_TLB_INT1_CONFIG_P_MASK			0x0000000000002000UL
#define UVH_GR1_TLB_INT1_CONFIG_T_MASK			0x0000000000008000UL
#define UVH_GR1_TLB_INT1_CONFIG_M_MASK			0x0000000000010000UL
#define UVH_GR1_TLB_INT1_CONFIG_APIC_ID_MASK		0xffffffff00000000UL

union uvh_gr1_tlb_int1_config_u {
	unsigned long	v;
	struct uvh_gr1_tlb_int1_config_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	dm:3;				/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	status:1;			/* RO */
		unsigned long	p:1;				/* RO */
		unsigned long	rsvd_14:1;
		unsigned long	t:1;				/* RO */
		unsigned long	m:1;				/* RW */
		unsigned long	rsvd_17_31:15;
		unsigned long	apic_id:32;			/* RW */
	} s;
};

/* ========================================================================= */
/*                         UVH_GR1_TLB_MMR_CONTROL                           */
/* ========================================================================= */
#define UV1H_GR1_TLB_MMR_CONTROL 0x801080UL
#define UV2H_GR1_TLB_MMR_CONTROL 0x1001080UL
#define UV3H_GR1_TLB_MMR_CONTROL 0x1001080UL
#define UVH_GR1_TLB_MMR_CONTROL						\
		(is_uv1_hub() ? UV1H_GR1_TLB_MMR_CONTROL :		\
		(is_uv2_hub() ? UV2H_GR1_TLB_MMR_CONTROL :		\
				UV3H_GR1_TLB_MMR_CONTROL))

#define UVH_GR1_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UVH_GR1_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UVH_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UVH_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UVH_GR1_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UVH_GR1_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UVH_GR1_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UVH_GR1_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UVH_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UVH_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UVH_GR1_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UVH_GR1_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL

#define UV1H_GR1_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UV1H_GR1_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UV1H_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_CON_SHFT	48
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBRAM_SHFT	52
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBPGSIZE_SHFT	54
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBRREG_SHFT	56
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBLRUV_SHFT	60
#define UV1H_GR1_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UV1H_GR1_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UV1H_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_CON_MASK	0x0001000000000000UL
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBRAM_MASK	0x0010000000000000UL
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBPGSIZE_MASK	0x0040000000000000UL
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBRREG_MASK	0x0100000000000000UL
#define UV1H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBLRUV_MASK	0x1000000000000000UL

#define UVXH_GR1_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UVXH_GR1_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UVXH_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UVXH_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UVXH_GR1_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UVXH_GR1_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UVXH_GR1_TLB_MMR_CONTROL_MMR_OP_DONE_SHFT	32
#define UVXH_GR1_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UVXH_GR1_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UVXH_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UVXH_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UVXH_GR1_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UVXH_GR1_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL
#define UVXH_GR1_TLB_MMR_CONTROL_MMR_OP_DONE_MASK	0x0000000100000000UL

#define UV2H_GR1_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UV2H_GR1_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UV2H_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_OP_DONE_SHFT	32
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_INJ_CON_SHFT	48
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBRAM_SHFT	52
#define UV2H_GR1_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UV2H_GR1_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UV2H_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_OP_DONE_MASK	0x0000000100000000UL
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_INJ_CON_MASK	0x0001000000000000UL
#define UV2H_GR1_TLB_MMR_CONTROL_MMR_INJ_TLBRAM_MASK	0x0010000000000000UL

#define UV3H_GR1_TLB_MMR_CONTROL_INDEX_SHFT		0
#define UV3H_GR1_TLB_MMR_CONTROL_MEM_SEL_SHFT		12
#define UV3H_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_SHFT	16
#define UV3H_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_SHFT	20
#define UV3H_GR1_TLB_MMR_CONTROL_ECC_SEL_SHFT		21
#define UV3H_GR1_TLB_MMR_CONTROL_MMR_WRITE_SHFT		30
#define UV3H_GR1_TLB_MMR_CONTROL_MMR_READ_SHFT		31
#define UV3H_GR1_TLB_MMR_CONTROL_MMR_OP_DONE_SHFT	32
#define UV3H_GR1_TLB_MMR_CONTROL_INDEX_MASK		0x0000000000000fffUL
#define UV3H_GR1_TLB_MMR_CONTROL_MEM_SEL_MASK		0x0000000000003000UL
#define UV3H_GR1_TLB_MMR_CONTROL_AUTO_VALID_EN_MASK	0x0000000000010000UL
#define UV3H_GR1_TLB_MMR_CONTROL_MMR_HASH_INDEX_EN_MASK	0x0000000000100000UL
#define UV3H_GR1_TLB_MMR_CONTROL_ECC_SEL_MASK		0x0000000000200000UL
#define UV3H_GR1_TLB_MMR_CONTROL_MMR_WRITE_MASK		0x0000000040000000UL
#define UV3H_GR1_TLB_MMR_CONTROL_MMR_READ_MASK		0x0000000080000000UL
#define UV3H_GR1_TLB_MMR_CONTROL_MMR_OP_DONE_MASK	0x0000000100000000UL

union uvh_gr1_tlb_mmr_control_u {
	unsigned long	v;
	struct uvh_gr1_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	rsvd_21_29:9;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	rsvd_32_48:17;
		unsigned long	rsvd_49_51:3;
		unsigned long	rsvd_52_63:12;
	} s;
	struct uv1h_gr1_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	rsvd_21_29:9;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	rsvd_32_47:16;
		unsigned long	mmr_inj_con:1;			/* RW */
		unsigned long	rsvd_49_51:3;
		unsigned long	mmr_inj_tlbram:1;		/* RW */
		unsigned long	rsvd_53:1;
		unsigned long	mmr_inj_tlbpgsize:1;		/* RW */
		unsigned long	rsvd_55:1;
		unsigned long	mmr_inj_tlbrreg:1;		/* RW */
		unsigned long	rsvd_57_59:3;
		unsigned long	mmr_inj_tlblruv:1;		/* RW */
		unsigned long	rsvd_61_63:3;
	} s1;
	struct uvxh_gr1_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	rsvd_21_29:9;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	mmr_op_done:1;			/* RW */
		unsigned long	rsvd_33_47:15;
		unsigned long	rsvd_48:1;
		unsigned long	rsvd_49_51:3;
		unsigned long	rsvd_52:1;
		unsigned long	rsvd_53_63:11;
	} sx;
	struct uv2h_gr1_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	rsvd_21_29:9;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	mmr_op_done:1;			/* RW */
		unsigned long	rsvd_33_47:15;
		unsigned long	mmr_inj_con:1;			/* RW */
		unsigned long	rsvd_49_51:3;
		unsigned long	mmr_inj_tlbram:1;		/* RW */
		unsigned long	rsvd_53_63:11;
	} s2;
	struct uv3h_gr1_tlb_mmr_control_s {
		unsigned long	index:12;			/* RW */
		unsigned long	mem_sel:2;			/* RW */
		unsigned long	rsvd_14_15:2;
		unsigned long	auto_valid_en:1;		/* RW */
		unsigned long	rsvd_17_19:3;
		unsigned long	mmr_hash_index_en:1;		/* RW */
		unsigned long	ecc_sel:1;			/* RW */
		unsigned long	rsvd_22_29:8;
		unsigned long	mmr_write:1;			/* WP */
		unsigned long	mmr_read:1;			/* WP */
		unsigned long	mmr_op_done:1;			/* RW */
		unsigned long	rsvd_33_47:15;
		unsigned long	undef_48:1;			/* Undefined */
		unsigned long	rsvd_49_51:3;
		unsigned long	undef_52:1;			/* Undefined */
		unsigned long	rsvd_53_63:11;
	} s3;
};

/* ========================================================================= */
/*                       UVH_GR1_TLB_MMR_READ_DATA_HI                        */
/* ========================================================================= */
#define UV1H_GR1_TLB_MMR_READ_DATA_HI 0x8010a0UL
#define UV2H_GR1_TLB_MMR_READ_DATA_HI 0x10010a0UL
#define UV3H_GR1_TLB_MMR_READ_DATA_HI 0x10010a0UL
#define UVH_GR1_TLB_MMR_READ_DATA_HI					\
		(is_uv1_hub() ? UV1H_GR1_TLB_MMR_READ_DATA_HI :		\
		(is_uv2_hub() ? UV2H_GR1_TLB_MMR_READ_DATA_HI :		\
				UV3H_GR1_TLB_MMR_READ_DATA_HI))

#define UVH_GR1_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UVH_GR1_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UVH_GR1_TLB_MMR_READ_DATA_HI_DIRTY_SHFT		43
#define UVH_GR1_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UVH_GR1_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UVH_GR1_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UVH_GR1_TLB_MMR_READ_DATA_HI_DIRTY_MASK		0x0000080000000000UL
#define UVH_GR1_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL

#define UV1H_GR1_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UV1H_GR1_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UV1H_GR1_TLB_MMR_READ_DATA_HI_DIRTY_SHFT	43
#define UV1H_GR1_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UV1H_GR1_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UV1H_GR1_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UV1H_GR1_TLB_MMR_READ_DATA_HI_DIRTY_MASK	0x0000080000000000UL
#define UV1H_GR1_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL

#define UVXH_GR1_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UVXH_GR1_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UVXH_GR1_TLB_MMR_READ_DATA_HI_DIRTY_SHFT	43
#define UVXH_GR1_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UVXH_GR1_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UVXH_GR1_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UVXH_GR1_TLB_MMR_READ_DATA_HI_DIRTY_MASK	0x0000080000000000UL
#define UVXH_GR1_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL

#define UV2H_GR1_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UV2H_GR1_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UV2H_GR1_TLB_MMR_READ_DATA_HI_DIRTY_SHFT	43
#define UV2H_GR1_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UV2H_GR1_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UV2H_GR1_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UV2H_GR1_TLB_MMR_READ_DATA_HI_DIRTY_MASK	0x0000080000000000UL
#define UV2H_GR1_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL

#define UV3H_GR1_TLB_MMR_READ_DATA_HI_PFN_SHFT		0
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_GAA_SHFT		41
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_DIRTY_SHFT	43
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_LARGER_SHFT	44
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_AA_EXT_SHFT	45
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_WAY_ECC_SHFT	55
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_PFN_MASK		0x000001ffffffffffUL
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_GAA_MASK		0x0000060000000000UL
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_DIRTY_MASK	0x0000080000000000UL
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_LARGER_MASK	0x0000100000000000UL
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_AA_EXT_MASK	0x0000200000000000UL
#define UV3H_GR1_TLB_MMR_READ_DATA_HI_WAY_ECC_MASK	0xff80000000000000UL

union uvh_gr1_tlb_mmr_read_data_hi_u {
	unsigned long	v;
	struct uvh_gr1_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	rsvd_45_63:19;
	} s;
	struct uv1h_gr1_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	rsvd_45_63:19;
	} s1;
	struct uvxh_gr1_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	rsvd_45_63:19;
	} sx;
	struct uv2h_gr1_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	rsvd_45_63:19;
	} s2;
	struct uv3h_gr1_tlb_mmr_read_data_hi_s {
		unsigned long	pfn:41;				/* RO */
		unsigned long	gaa:2;				/* RO */
		unsigned long	dirty:1;			/* RO */
		unsigned long	larger:1;			/* RO */
		unsigned long	aa_ext:1;			/* RO */
		unsigned long	undef_46_54:9;			/* Undefined */
		unsigned long	way_ecc:9;			/* RO */
	} s3;
};

/* ========================================================================= */
/*                       UVH_GR1_TLB_MMR_READ_DATA_LO                        */
/* ========================================================================= */
#define UV1H_GR1_TLB_MMR_READ_DATA_LO 0x8010a8UL
#define UV2H_GR1_TLB_MMR_READ_DATA_LO 0x10010a8UL
#define UV3H_GR1_TLB_MMR_READ_DATA_LO 0x10010a8UL
#define UVH_GR1_TLB_MMR_READ_DATA_LO					\
		(is_uv1_hub() ? UV1H_GR1_TLB_MMR_READ_DATA_LO :		\
		(is_uv2_hub() ? UV2H_GR1_TLB_MMR_READ_DATA_LO :		\
				UV3H_GR1_TLB_MMR_READ_DATA_LO))

#define UVH_GR1_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UVH_GR1_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UVH_GR1_TLB_MMR_READ_DATA_LO_VALID_SHFT		63
#define UVH_GR1_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UVH_GR1_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UVH_GR1_TLB_MMR_READ_DATA_LO_VALID_MASK		0x8000000000000000UL

#define UV1H_GR1_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UV1H_GR1_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UV1H_GR1_TLB_MMR_READ_DATA_LO_VALID_SHFT	63
#define UV1H_GR1_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UV1H_GR1_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UV1H_GR1_TLB_MMR_READ_DATA_LO_VALID_MASK	0x8000000000000000UL

#define UVXH_GR1_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UVXH_GR1_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UVXH_GR1_TLB_MMR_READ_DATA_LO_VALID_SHFT	63
#define UVXH_GR1_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UVXH_GR1_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UVXH_GR1_TLB_MMR_READ_DATA_LO_VALID_MASK	0x8000000000000000UL

#define UV2H_GR1_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UV2H_GR1_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UV2H_GR1_TLB_MMR_READ_DATA_LO_VALID_SHFT	63
#define UV2H_GR1_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UV2H_GR1_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UV2H_GR1_TLB_MMR_READ_DATA_LO_VALID_MASK	0x8000000000000000UL

#define UV3H_GR1_TLB_MMR_READ_DATA_LO_VPN_SHFT		0
#define UV3H_GR1_TLB_MMR_READ_DATA_LO_ASID_SHFT		39
#define UV3H_GR1_TLB_MMR_READ_DATA_LO_VALID_SHFT	63
#define UV3H_GR1_TLB_MMR_READ_DATA_LO_VPN_MASK		0x0000007fffffffffUL
#define UV3H_GR1_TLB_MMR_READ_DATA_LO_ASID_MASK		0x7fffff8000000000UL
#define UV3H_GR1_TLB_MMR_READ_DATA_LO_VALID_MASK	0x8000000000000000UL

union uvh_gr1_tlb_mmr_read_data_lo_u {
	unsigned long	v;
	struct uvh_gr1_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} s;
	struct uv1h_gr1_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} s1;
	struct uvxh_gr1_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} sx;
	struct uv2h_gr1_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} s2;
	struct uv3h_gr1_tlb_mmr_read_data_lo_s {
		unsigned long	vpn:39;				/* RO */
		unsigned long	asid:24;			/* RO */
		unsigned long	valid:1;			/* RO */
	} s3;
};

/* ========================================================================= */
/*                               UVH_INT_CMPB                                */
/* ========================================================================= */
#define UVH_INT_CMPB 0x22080UL

#define UVH_INT_CMPB_REAL_TIME_CMPB_SHFT		0
#define UVH_INT_CMPB_REAL_TIME_CMPB_MASK		0x00ffffffffffffffUL

union uvh_int_cmpb_u {
	unsigned long	v;
	struct uvh_int_cmpb_s {
		unsigned long	real_time_cmpb:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s;
};

/* ========================================================================= */
/*                               UVH_INT_CMPC                                */
/* ========================================================================= */
#define UVH_INT_CMPC 0x22100UL

#define UV1H_INT_CMPC_REAL_TIME_CMPC_SHFT		0
#define UV1H_INT_CMPC_REAL_TIME_CMPC_MASK		0x00ffffffffffffffUL

#define UVXH_INT_CMPC_REAL_TIME_CMP_2_SHFT		0
#define UVXH_INT_CMPC_REAL_TIME_CMP_2_MASK		0x00ffffffffffffffUL

union uvh_int_cmpc_u {
	unsigned long	v;
	struct uvh_int_cmpc_s {
		unsigned long	real_time_cmpc:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s;
};

/* ========================================================================= */
/*                               UVH_INT_CMPD                                */
/* ========================================================================= */
#define UVH_INT_CMPD 0x22180UL

#define UV1H_INT_CMPD_REAL_TIME_CMPD_SHFT		0
#define UV1H_INT_CMPD_REAL_TIME_CMPD_MASK		0x00ffffffffffffffUL

#define UVXH_INT_CMPD_REAL_TIME_CMP_3_SHFT		0
#define UVXH_INT_CMPD_REAL_TIME_CMP_3_MASK		0x00ffffffffffffffUL

union uvh_int_cmpd_u {
	unsigned long	v;
	struct uvh_int_cmpd_s {
		unsigned long	real_time_cmpd:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s;
};

/* ========================================================================= */
/*                               UVH_IPI_INT                                 */
/* ========================================================================= */
#define UVH_IPI_INT 0x60500UL
#define UVH_IPI_INT_32 0x348

#define UVH_IPI_INT_VECTOR_SHFT				0
#define UVH_IPI_INT_DELIVERY_MODE_SHFT			8
#define UVH_IPI_INT_DESTMODE_SHFT			11
#define UVH_IPI_INT_APIC_ID_SHFT			16
#define UVH_IPI_INT_SEND_SHFT				63
#define UVH_IPI_INT_VECTOR_MASK				0x00000000000000ffUL
#define UVH_IPI_INT_DELIVERY_MODE_MASK			0x0000000000000700UL
#define UVH_IPI_INT_DESTMODE_MASK			0x0000000000000800UL
#define UVH_IPI_INT_APIC_ID_MASK			0x0000ffffffff0000UL
#define UVH_IPI_INT_SEND_MASK				0x8000000000000000UL

union uvh_ipi_int_u {
	unsigned long	v;
	struct uvh_ipi_int_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	delivery_mode:3;		/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	rsvd_12_15:4;
		unsigned long	apic_id:32;			/* RW */
		unsigned long	rsvd_48_62:15;
		unsigned long	send:1;				/* WP */
	} s;
};

/* ========================================================================= */
/*                   UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST                     */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST 0x320050UL
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_32 0x9c0

#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_ADDRESS_SHFT 4
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_NODE_ID_SHFT 49
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_ADDRESS_MASK 0x000007fffffffff0UL
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_NODE_ID_MASK 0x7ffe000000000000UL

union uvh_lb_bau_intd_payload_queue_first_u {
	unsigned long	v;
	struct uvh_lb_bau_intd_payload_queue_first_s {
		unsigned long	rsvd_0_3:4;
		unsigned long	address:39;			/* RW */
		unsigned long	rsvd_43_48:6;
		unsigned long	node_id:14;			/* RW */
		unsigned long	rsvd_63:1;
	} s;
};

/* ========================================================================= */
/*                    UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST                     */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST 0x320060UL
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST_32 0x9c8

#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST_ADDRESS_SHFT	4
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST_ADDRESS_MASK	0x000007fffffffff0UL

union uvh_lb_bau_intd_payload_queue_last_u {
	unsigned long	v;
	struct uvh_lb_bau_intd_payload_queue_last_s {
		unsigned long	rsvd_0_3:4;
		unsigned long	address:39;			/* RW */
		unsigned long	rsvd_43_63:21;
	} s;
};

/* ========================================================================= */
/*                    UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL                     */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL 0x320070UL
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL_32 0x9d0

#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL_ADDRESS_SHFT	4
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL_ADDRESS_MASK	0x000007fffffffff0UL

union uvh_lb_bau_intd_payload_queue_tail_u {
	unsigned long	v;
	struct uvh_lb_bau_intd_payload_queue_tail_s {
		unsigned long	rsvd_0_3:4;
		unsigned long	address:39;			/* RW */
		unsigned long	rsvd_43_63:21;
	} s;
};

/* ========================================================================= */
/*                   UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE                    */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE 0x320080UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_32 0xa68

#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_0_SHFT 0
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_1_SHFT 1
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_2_SHFT 2
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_3_SHFT 3
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_4_SHFT 4
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_5_SHFT 5
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_6_SHFT 6
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_7_SHFT 7
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_0_SHFT 8
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_1_SHFT 9
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_2_SHFT 10
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_3_SHFT 11
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_4_SHFT 12
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_5_SHFT 13
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_6_SHFT 14
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_7_SHFT 15
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_0_MASK 0x0000000000000001UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_1_MASK 0x0000000000000002UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_2_MASK 0x0000000000000004UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_3_MASK 0x0000000000000008UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_4_MASK 0x0000000000000010UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_5_MASK 0x0000000000000020UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_6_MASK 0x0000000000000040UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_7_MASK 0x0000000000000080UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_0_MASK 0x0000000000000100UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_1_MASK 0x0000000000000200UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_2_MASK 0x0000000000000400UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_3_MASK 0x0000000000000800UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_4_MASK 0x0000000000001000UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_5_MASK 0x0000000000002000UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_6_MASK 0x0000000000004000UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_7_MASK 0x0000000000008000UL

union uvh_lb_bau_intd_software_acknowledge_u {
	unsigned long	v;
	struct uvh_lb_bau_intd_software_acknowledge_s {
		unsigned long	pending_0:1;			/* RW, W1C */
		unsigned long	pending_1:1;			/* RW, W1C */
		unsigned long	pending_2:1;			/* RW, W1C */
		unsigned long	pending_3:1;			/* RW, W1C */
		unsigned long	pending_4:1;			/* RW, W1C */
		unsigned long	pending_5:1;			/* RW, W1C */
		unsigned long	pending_6:1;			/* RW, W1C */
		unsigned long	pending_7:1;			/* RW, W1C */
		unsigned long	timeout_0:1;			/* RW, W1C */
		unsigned long	timeout_1:1;			/* RW, W1C */
		unsigned long	timeout_2:1;			/* RW, W1C */
		unsigned long	timeout_3:1;			/* RW, W1C */
		unsigned long	timeout_4:1;			/* RW, W1C */
		unsigned long	timeout_5:1;			/* RW, W1C */
		unsigned long	timeout_6:1;			/* RW, W1C */
		unsigned long	timeout_7:1;			/* RW, W1C */
		unsigned long	rsvd_16_63:48;
	} s;
};

/* ========================================================================= */
/*                UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS                 */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS 0x320088UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS_32 0xa70


/* ========================================================================= */
/*                         UVH_LB_BAU_MISC_CONTROL                           */
/* ========================================================================= */
#define UVH_LB_BAU_MISC_CONTROL 0x320170UL
#define UV1H_LB_BAU_MISC_CONTROL 0x320170UL
#define UV2H_LB_BAU_MISC_CONTROL 0x320170UL
#define UV3H_LB_BAU_MISC_CONTROL 0x320170UL
#define UVH_LB_BAU_MISC_CONTROL_32 0xa10
#define UV1H_LB_BAU_MISC_CONTROL_32 0x320170UL
#define UV2H_LB_BAU_MISC_CONTROL_32 0x320170UL
#define UV3H_LB_BAU_MISC_CONTROL_32 0x320170UL

#define UVH_LB_BAU_MISC_CONTROL_REJECTION_DELAY_SHFT	0
#define UVH_LB_BAU_MISC_CONTROL_APIC_MODE_SHFT		8
#define UVH_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_SHFT	9
#define UVH_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_SHFT	10
#define UVH_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_SHFT 11
#define UVH_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_SHFT 14
#define UVH_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_SHFT 15
#define UVH_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_SHFT 16
#define UVH_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_SHFT 20
#define UVH_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_SHFT 21
#define UVH_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_SHFT 22
#define UVH_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_SHFT 23
#define UVH_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_SHFT 24
#define UVH_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_SHFT 27
#define UVH_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_SHFT 28
#define UVH_LB_BAU_MISC_CONTROL_FUN_SHFT		48
#define UVH_LB_BAU_MISC_CONTROL_REJECTION_DELAY_MASK	0x00000000000000ffUL
#define UVH_LB_BAU_MISC_CONTROL_APIC_MODE_MASK		0x0000000000000100UL
#define UVH_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_MASK	0x0000000000000200UL
#define UVH_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_MASK	0x0000000000000400UL
#define UVH_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_MASK 0x0000000000003800UL
#define UVH_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_MASK 0x0000000000004000UL
#define UVH_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_MASK 0x0000000000008000UL
#define UVH_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_MASK 0x00000000000f0000UL
#define UVH_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_MASK 0x0000000000100000UL
#define UVH_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_MASK 0x0000000000200000UL
#define UVH_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_MASK 0x0000000000400000UL
#define UVH_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_MASK 0x0000000000800000UL
#define UVH_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000007000000UL
#define UVH_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_MASK 0x0000000008000000UL
#define UVH_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000010000000UL
#define UVH_LB_BAU_MISC_CONTROL_FUN_MASK		0xffff000000000000UL

#define UV1H_LB_BAU_MISC_CONTROL_REJECTION_DELAY_SHFT	0
#define UV1H_LB_BAU_MISC_CONTROL_APIC_MODE_SHFT		8
#define UV1H_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_SHFT	9
#define UV1H_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_SHFT	10
#define UV1H_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_SHFT 11
#define UV1H_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_SHFT 14
#define UV1H_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_SHFT 15
#define UV1H_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_SHFT 16
#define UV1H_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_SHFT 20
#define UV1H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_SHFT 21
#define UV1H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_SHFT 22
#define UV1H_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_SHFT 23
#define UV1H_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_SHFT 24
#define UV1H_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_SHFT 27
#define UV1H_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_SHFT 28
#define UV1H_LB_BAU_MISC_CONTROL_FUN_SHFT		48
#define UV1H_LB_BAU_MISC_CONTROL_REJECTION_DELAY_MASK	0x00000000000000ffUL
#define UV1H_LB_BAU_MISC_CONTROL_APIC_MODE_MASK		0x0000000000000100UL
#define UV1H_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_MASK	0x0000000000000200UL
#define UV1H_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_MASK	0x0000000000000400UL
#define UV1H_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_MASK 0x0000000000003800UL
#define UV1H_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_MASK 0x0000000000004000UL
#define UV1H_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_MASK 0x0000000000008000UL
#define UV1H_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_MASK 0x00000000000f0000UL
#define UV1H_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_MASK 0x0000000000100000UL
#define UV1H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_MASK 0x0000000000200000UL
#define UV1H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_MASK 0x0000000000400000UL
#define UV1H_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_MASK 0x0000000000800000UL
#define UV1H_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000007000000UL
#define UV1H_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_MASK 0x0000000008000000UL
#define UV1H_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000010000000UL
#define UV1H_LB_BAU_MISC_CONTROL_FUN_MASK		0xffff000000000000UL

#define UVXH_LB_BAU_MISC_CONTROL_REJECTION_DELAY_SHFT	0
#define UVXH_LB_BAU_MISC_CONTROL_APIC_MODE_SHFT		8
#define UVXH_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_SHFT	9
#define UVXH_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_SHFT	10
#define UVXH_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_SHFT 11
#define UVXH_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_SHFT 14
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_SHFT 15
#define UVXH_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_SHFT 16
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_SHFT 20
#define UVXH_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_SHFT 21
#define UVXH_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_SHFT 22
#define UVXH_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_SHFT 23
#define UVXH_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_SHFT 24
#define UVXH_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_SHFT 27
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_SHFT 28
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_AUTOMATIC_APIC_MODE_SELECTION_SHFT 29
#define UVXH_LB_BAU_MISC_CONTROL_APIC_MODE_STATUS_SHFT	30
#define UVXH_LB_BAU_MISC_CONTROL_SUPPRESS_INTERRUPTS_TO_SELF_SHFT 31
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_LOCK_BASED_SYSTEM_FLUSH_SHFT 32
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_EXTENDED_SB_STATUS_SHFT 33
#define UVXH_LB_BAU_MISC_CONTROL_SUPPRESS_INT_PRIO_UDT_TO_SELF_SHFT 34
#define UVXH_LB_BAU_MISC_CONTROL_USE_LEGACY_DESCRIPTOR_FORMATS_SHFT 35
#define UVXH_LB_BAU_MISC_CONTROL_FUN_SHFT		48
#define UVXH_LB_BAU_MISC_CONTROL_REJECTION_DELAY_MASK	0x00000000000000ffUL
#define UVXH_LB_BAU_MISC_CONTROL_APIC_MODE_MASK		0x0000000000000100UL
#define UVXH_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_MASK	0x0000000000000200UL
#define UVXH_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_MASK	0x0000000000000400UL
#define UVXH_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_MASK 0x0000000000003800UL
#define UVXH_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_MASK 0x0000000000004000UL
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_MASK 0x0000000000008000UL
#define UVXH_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_MASK 0x00000000000f0000UL
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_MASK 0x0000000000100000UL
#define UVXH_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_MASK 0x0000000000200000UL
#define UVXH_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_MASK 0x0000000000400000UL
#define UVXH_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_MASK 0x0000000000800000UL
#define UVXH_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000007000000UL
#define UVXH_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_MASK 0x0000000008000000UL
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000010000000UL
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_AUTOMATIC_APIC_MODE_SELECTION_MASK 0x0000000020000000UL
#define UVXH_LB_BAU_MISC_CONTROL_APIC_MODE_STATUS_MASK	0x0000000040000000UL
#define UVXH_LB_BAU_MISC_CONTROL_SUPPRESS_INTERRUPTS_TO_SELF_MASK 0x0000000080000000UL
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_LOCK_BASED_SYSTEM_FLUSH_MASK 0x0000000100000000UL
#define UVXH_LB_BAU_MISC_CONTROL_ENABLE_EXTENDED_SB_STATUS_MASK 0x0000000200000000UL
#define UVXH_LB_BAU_MISC_CONTROL_SUPPRESS_INT_PRIO_UDT_TO_SELF_MASK 0x0000000400000000UL
#define UVXH_LB_BAU_MISC_CONTROL_USE_LEGACY_DESCRIPTOR_FORMATS_MASK 0x0000000800000000UL
#define UVXH_LB_BAU_MISC_CONTROL_FUN_MASK		0xffff000000000000UL

#define UV2H_LB_BAU_MISC_CONTROL_REJECTION_DELAY_SHFT	0
#define UV2H_LB_BAU_MISC_CONTROL_APIC_MODE_SHFT		8
#define UV2H_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_SHFT	9
#define UV2H_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_SHFT	10
#define UV2H_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_SHFT 11
#define UV2H_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_SHFT 14
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_SHFT 15
#define UV2H_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_SHFT 16
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_SHFT 20
#define UV2H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_SHFT 21
#define UV2H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_SHFT 22
#define UV2H_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_SHFT 23
#define UV2H_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_SHFT 24
#define UV2H_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_SHFT 27
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_SHFT 28
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_AUTOMATIC_APIC_MODE_SELECTION_SHFT 29
#define UV2H_LB_BAU_MISC_CONTROL_APIC_MODE_STATUS_SHFT	30
#define UV2H_LB_BAU_MISC_CONTROL_SUPPRESS_INTERRUPTS_TO_SELF_SHFT 31
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_LOCK_BASED_SYSTEM_FLUSH_SHFT 32
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_EXTENDED_SB_STATUS_SHFT 33
#define UV2H_LB_BAU_MISC_CONTROL_SUPPRESS_INT_PRIO_UDT_TO_SELF_SHFT 34
#define UV2H_LB_BAU_MISC_CONTROL_USE_LEGACY_DESCRIPTOR_FORMATS_SHFT 35
#define UV2H_LB_BAU_MISC_CONTROL_FUN_SHFT		48
#define UV2H_LB_BAU_MISC_CONTROL_REJECTION_DELAY_MASK	0x00000000000000ffUL
#define UV2H_LB_BAU_MISC_CONTROL_APIC_MODE_MASK		0x0000000000000100UL
#define UV2H_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_MASK	0x0000000000000200UL
#define UV2H_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_MASK	0x0000000000000400UL
#define UV2H_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_MASK 0x0000000000003800UL
#define UV2H_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_MASK 0x0000000000004000UL
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_MASK 0x0000000000008000UL
#define UV2H_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_MASK 0x00000000000f0000UL
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_MASK 0x0000000000100000UL
#define UV2H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_MASK 0x0000000000200000UL
#define UV2H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_MASK 0x0000000000400000UL
#define UV2H_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_MASK 0x0000000000800000UL
#define UV2H_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000007000000UL
#define UV2H_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_MASK 0x0000000008000000UL
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000010000000UL
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_AUTOMATIC_APIC_MODE_SELECTION_MASK 0x0000000020000000UL
#define UV2H_LB_BAU_MISC_CONTROL_APIC_MODE_STATUS_MASK	0x0000000040000000UL
#define UV2H_LB_BAU_MISC_CONTROL_SUPPRESS_INTERRUPTS_TO_SELF_MASK 0x0000000080000000UL
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_LOCK_BASED_SYSTEM_FLUSH_MASK 0x0000000100000000UL
#define UV2H_LB_BAU_MISC_CONTROL_ENABLE_EXTENDED_SB_STATUS_MASK 0x0000000200000000UL
#define UV2H_LB_BAU_MISC_CONTROL_SUPPRESS_INT_PRIO_UDT_TO_SELF_MASK 0x0000000400000000UL
#define UV2H_LB_BAU_MISC_CONTROL_USE_LEGACY_DESCRIPTOR_FORMATS_MASK 0x0000000800000000UL
#define UV2H_LB_BAU_MISC_CONTROL_FUN_MASK		0xffff000000000000UL

#define UV3H_LB_BAU_MISC_CONTROL_REJECTION_DELAY_SHFT	0
#define UV3H_LB_BAU_MISC_CONTROL_APIC_MODE_SHFT		8
#define UV3H_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_SHFT	9
#define UV3H_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_SHFT	10
#define UV3H_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_SHFT 11
#define UV3H_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_SHFT 14
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_SHFT 15
#define UV3H_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_SHFT 16
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_SHFT 20
#define UV3H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_SHFT 21
#define UV3H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_SHFT 22
#define UV3H_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_SHFT 23
#define UV3H_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_SHFT 24
#define UV3H_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_SHFT 27
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_SHFT 28
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_AUTOMATIC_APIC_MODE_SELECTION_SHFT 29
#define UV3H_LB_BAU_MISC_CONTROL_APIC_MODE_STATUS_SHFT	30
#define UV3H_LB_BAU_MISC_CONTROL_SUPPRESS_INTERRUPTS_TO_SELF_SHFT 31
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_LOCK_BASED_SYSTEM_FLUSH_SHFT 32
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_EXTENDED_SB_STATUS_SHFT 33
#define UV3H_LB_BAU_MISC_CONTROL_SUPPRESS_INT_PRIO_UDT_TO_SELF_SHFT 34
#define UV3H_LB_BAU_MISC_CONTROL_USE_LEGACY_DESCRIPTOR_FORMATS_SHFT 35
#define UV3H_LB_BAU_MISC_CONTROL_SUPPRESS_QUIESCE_MSGS_TO_QPI_SHFT 36
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_INTD_PREFETCH_HINT_SHFT 37
#define UV3H_LB_BAU_MISC_CONTROL_THREAD_KILL_TIMEBASE_SHFT 38
#define UV3H_LB_BAU_MISC_CONTROL_FUN_SHFT		48
#define UV3H_LB_BAU_MISC_CONTROL_REJECTION_DELAY_MASK	0x00000000000000ffUL
#define UV3H_LB_BAU_MISC_CONTROL_APIC_MODE_MASK		0x0000000000000100UL
#define UV3H_LB_BAU_MISC_CONTROL_FORCE_BROADCAST_MASK	0x0000000000000200UL
#define UV3H_LB_BAU_MISC_CONTROL_FORCE_LOCK_NOP_MASK	0x0000000000000400UL
#define UV3H_LB_BAU_MISC_CONTROL_QPI_AGENT_PRESENCE_VECTOR_MASK 0x0000000000003800UL
#define UV3H_LB_BAU_MISC_CONTROL_DESCRIPTOR_FETCH_MODE_MASK 0x0000000000004000UL
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_MASK 0x0000000000008000UL
#define UV3H_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_MASK 0x00000000000f0000UL
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_DUAL_MAPPING_MODE_MASK 0x0000000000100000UL
#define UV3H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_DECODE_ENABLE_MASK 0x0000000000200000UL
#define UV3H_LB_BAU_MISC_CONTROL_VGA_IO_PORT_16_BIT_DECODE_MASK 0x0000000000400000UL
#define UV3H_LB_BAU_MISC_CONTROL_SUPPRESS_DEST_REGISTRATION_MASK 0x0000000000800000UL
#define UV3H_LB_BAU_MISC_CONTROL_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000007000000UL
#define UV3H_LB_BAU_MISC_CONTROL_USE_INCOMING_PRIORITY_MASK 0x0000000008000000UL
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_PROGRAMMED_INITIAL_PRIORITY_MASK 0x0000000010000000UL
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_AUTOMATIC_APIC_MODE_SELECTION_MASK 0x0000000020000000UL
#define UV3H_LB_BAU_MISC_CONTROL_APIC_MODE_STATUS_MASK	0x0000000040000000UL
#define UV3H_LB_BAU_MISC_CONTROL_SUPPRESS_INTERRUPTS_TO_SELF_MASK 0x0000000080000000UL
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_LOCK_BASED_SYSTEM_FLUSH_MASK 0x0000000100000000UL
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_EXTENDED_SB_STATUS_MASK 0x0000000200000000UL
#define UV3H_LB_BAU_MISC_CONTROL_SUPPRESS_INT_PRIO_UDT_TO_SELF_MASK 0x0000000400000000UL
#define UV3H_LB_BAU_MISC_CONTROL_USE_LEGACY_DESCRIPTOR_FORMATS_MASK 0x0000000800000000UL
#define UV3H_LB_BAU_MISC_CONTROL_SUPPRESS_QUIESCE_MSGS_TO_QPI_MASK 0x0000001000000000UL
#define UV3H_LB_BAU_MISC_CONTROL_ENABLE_INTD_PREFETCH_HINT_MASK 0x0000002000000000UL
#define UV3H_LB_BAU_MISC_CONTROL_THREAD_KILL_TIMEBASE_MASK 0x00003fc000000000UL
#define UV3H_LB_BAU_MISC_CONTROL_FUN_MASK		0xffff000000000000UL

union uvh_lb_bau_misc_control_u {
	unsigned long	v;
	struct uvh_lb_bau_misc_control_s {
		unsigned long	rejection_delay:8;		/* RW */
		unsigned long	apic_mode:1;			/* RW */
		unsigned long	force_broadcast:1;		/* RW */
		unsigned long	force_lock_nop:1;		/* RW */
		unsigned long	qpi_agent_presence_vector:3;	/* RW */
		unsigned long	descriptor_fetch_mode:1;	/* RW */
		unsigned long	enable_intd_soft_ack_mode:1;	/* RW */
		unsigned long	intd_soft_ack_timeout_period:4;	/* RW */
		unsigned long	enable_dual_mapping_mode:1;	/* RW */
		unsigned long	vga_io_port_decode_enable:1;	/* RW */
		unsigned long	vga_io_port_16_bit_decode:1;	/* RW */
		unsigned long	suppress_dest_registration:1;	/* RW */
		unsigned long	programmed_initial_priority:3;	/* RW */
		unsigned long	use_incoming_priority:1;	/* RW */
		unsigned long	enable_programmed_initial_priority:1;/* RW */
		unsigned long	rsvd_29_47:19;
		unsigned long	fun:16;				/* RW */
	} s;
	struct uv1h_lb_bau_misc_control_s {
		unsigned long	rejection_delay:8;		/* RW */
		unsigned long	apic_mode:1;			/* RW */
		unsigned long	force_broadcast:1;		/* RW */
		unsigned long	force_lock_nop:1;		/* RW */
		unsigned long	qpi_agent_presence_vector:3;	/* RW */
		unsigned long	descriptor_fetch_mode:1;	/* RW */
		unsigned long	enable_intd_soft_ack_mode:1;	/* RW */
		unsigned long	intd_soft_ack_timeout_period:4;	/* RW */
		unsigned long	enable_dual_mapping_mode:1;	/* RW */
		unsigned long	vga_io_port_decode_enable:1;	/* RW */
		unsigned long	vga_io_port_16_bit_decode:1;	/* RW */
		unsigned long	suppress_dest_registration:1;	/* RW */
		unsigned long	programmed_initial_priority:3;	/* RW */
		unsigned long	use_incoming_priority:1;	/* RW */
		unsigned long	enable_programmed_initial_priority:1;/* RW */
		unsigned long	rsvd_29_47:19;
		unsigned long	fun:16;				/* RW */
	} s1;
	struct uvxh_lb_bau_misc_control_s {
		unsigned long	rejection_delay:8;		/* RW */
		unsigned long	apic_mode:1;			/* RW */
		unsigned long	force_broadcast:1;		/* RW */
		unsigned long	force_lock_nop:1;		/* RW */
		unsigned long	qpi_agent_presence_vector:3;	/* RW */
		unsigned long	descriptor_fetch_mode:1;	/* RW */
		unsigned long	enable_intd_soft_ack_mode:1;	/* RW */
		unsigned long	intd_soft_ack_timeout_period:4;	/* RW */
		unsigned long	enable_dual_mapping_mode:1;	/* RW */
		unsigned long	vga_io_port_decode_enable:1;	/* RW */
		unsigned long	vga_io_port_16_bit_decode:1;	/* RW */
		unsigned long	suppress_dest_registration:1;	/* RW */
		unsigned long	programmed_initial_priority:3;	/* RW */
		unsigned long	use_incoming_priority:1;	/* RW */
		unsigned long	enable_programmed_initial_priority:1;/* RW */
		unsigned long	enable_automatic_apic_mode_selection:1;/* RW */
		unsigned long	apic_mode_status:1;		/* RO */
		unsigned long	suppress_interrupts_to_self:1;	/* RW */
		unsigned long	enable_lock_based_system_flush:1;/* RW */
		unsigned long	enable_extended_sb_status:1;	/* RW */
		unsigned long	suppress_int_prio_udt_to_self:1;/* RW */
		unsigned long	use_legacy_descriptor_formats:1;/* RW */
		unsigned long	rsvd_36_47:12;
		unsigned long	fun:16;				/* RW */
	} sx;
	struct uv2h_lb_bau_misc_control_s {
		unsigned long	rejection_delay:8;		/* RW */
		unsigned long	apic_mode:1;			/* RW */
		unsigned long	force_broadcast:1;		/* RW */
		unsigned long	force_lock_nop:1;		/* RW */
		unsigned long	qpi_agent_presence_vector:3;	/* RW */
		unsigned long	descriptor_fetch_mode:1;	/* RW */
		unsigned long	enable_intd_soft_ack_mode:1;	/* RW */
		unsigned long	intd_soft_ack_timeout_period:4;	/* RW */
		unsigned long	enable_dual_mapping_mode:1;	/* RW */
		unsigned long	vga_io_port_decode_enable:1;	/* RW */
		unsigned long	vga_io_port_16_bit_decode:1;	/* RW */
		unsigned long	suppress_dest_registration:1;	/* RW */
		unsigned long	programmed_initial_priority:3;	/* RW */
		unsigned long	use_incoming_priority:1;	/* RW */
		unsigned long	enable_programmed_initial_priority:1;/* RW */
		unsigned long	enable_automatic_apic_mode_selection:1;/* RW */
		unsigned long	apic_mode_status:1;		/* RO */
		unsigned long	suppress_interrupts_to_self:1;	/* RW */
		unsigned long	enable_lock_based_system_flush:1;/* RW */
		unsigned long	enable_extended_sb_status:1;	/* RW */
		unsigned long	suppress_int_prio_udt_to_self:1;/* RW */
		unsigned long	use_legacy_descriptor_formats:1;/* RW */
		unsigned long	rsvd_36_47:12;
		unsigned long	fun:16;				/* RW */
	} s2;
	struct uv3h_lb_bau_misc_control_s {
		unsigned long	rejection_delay:8;		/* RW */
		unsigned long	apic_mode:1;			/* RW */
		unsigned long	force_broadcast:1;		/* RW */
		unsigned long	force_lock_nop:1;		/* RW */
		unsigned long	qpi_agent_presence_vector:3;	/* RW */
		unsigned long	descriptor_fetch_mode:1;	/* RW */
		unsigned long	enable_intd_soft_ack_mode:1;	/* RW */
		unsigned long	intd_soft_ack_timeout_period:4;	/* RW */
		unsigned long	enable_dual_mapping_mode:1;	/* RW */
		unsigned long	vga_io_port_decode_enable:1;	/* RW */
		unsigned long	vga_io_port_16_bit_decode:1;	/* RW */
		unsigned long	suppress_dest_registration:1;	/* RW */
		unsigned long	programmed_initial_priority:3;	/* RW */
		unsigned long	use_incoming_priority:1;	/* RW */
		unsigned long	enable_programmed_initial_priority:1;/* RW */
		unsigned long	enable_automatic_apic_mode_selection:1;/* RW */
		unsigned long	apic_mode_status:1;		/* RO */
		unsigned long	suppress_interrupts_to_self:1;	/* RW */
		unsigned long	enable_lock_based_system_flush:1;/* RW */
		unsigned long	enable_extended_sb_status:1;	/* RW */
		unsigned long	suppress_int_prio_udt_to_self:1;/* RW */
		unsigned long	use_legacy_descriptor_formats:1;/* RW */
		unsigned long	suppress_quiesce_msgs_to_qpi:1;	/* RW */
		unsigned long	enable_intd_prefetch_hint:1;	/* RW */
		unsigned long	thread_kill_timebase:8;		/* RW */
		unsigned long	rsvd_46_47:2;
		unsigned long	fun:16;				/* RW */
	} s3;
};

/* ========================================================================= */
/*                     UVH_LB_BAU_SB_ACTIVATION_CONTROL                      */
/* ========================================================================= */
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL 0x320020UL
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_32 0x9a8

#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_INDEX_SHFT	0
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_PUSH_SHFT	62
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_INIT_SHFT	63
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_INDEX_MASK	0x000000000000003fUL
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_PUSH_MASK	0x4000000000000000UL
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_INIT_MASK	0x8000000000000000UL

union uvh_lb_bau_sb_activation_control_u {
	unsigned long	v;
	struct uvh_lb_bau_sb_activation_control_s {
		unsigned long	index:6;			/* RW */
		unsigned long	rsvd_6_61:56;
		unsigned long	push:1;				/* WP */
		unsigned long	init:1;				/* WP */
	} s;
};

/* ========================================================================= */
/*                    UVH_LB_BAU_SB_ACTIVATION_STATUS_0                      */
/* ========================================================================= */
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_0 0x320030UL
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_0_32 0x9b0

#define UVH_LB_BAU_SB_ACTIVATION_STATUS_0_STATUS_SHFT	0
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_0_STATUS_MASK	0xffffffffffffffffUL

union uvh_lb_bau_sb_activation_status_0_u {
	unsigned long	v;
	struct uvh_lb_bau_sb_activation_status_0_s {
		unsigned long	status:64;			/* RW */
	} s;
};

/* ========================================================================= */
/*                    UVH_LB_BAU_SB_ACTIVATION_STATUS_1                      */
/* ========================================================================= */
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_1 0x320040UL
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_1_32 0x9b8

#define UVH_LB_BAU_SB_ACTIVATION_STATUS_1_STATUS_SHFT	0
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_1_STATUS_MASK	0xffffffffffffffffUL

union uvh_lb_bau_sb_activation_status_1_u {
	unsigned long	v;
	struct uvh_lb_bau_sb_activation_status_1_s {
		unsigned long	status:64;			/* RW */
	} s;
};

/* ========================================================================= */
/*                      UVH_LB_BAU_SB_DESCRIPTOR_BASE                        */
/* ========================================================================= */
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE 0x320010UL
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_32 0x9a0

#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_PAGE_ADDRESS_SHFT	12
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_NODE_ID_SHFT	49
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_PAGE_ADDRESS_MASK	0x000007fffffff000UL
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_NODE_ID_MASK	0x7ffe000000000000UL

union uvh_lb_bau_sb_descriptor_base_u {
	unsigned long	v;
	struct uvh_lb_bau_sb_descriptor_base_s {
		unsigned long	rsvd_0_11:12;
		unsigned long	page_address:31;		/* RW */
		unsigned long	rsvd_43_48:6;
		unsigned long	node_id:14;			/* RW */
		unsigned long	rsvd_63:1;
	} s;
};

/* ========================================================================= */
/*                               UVH_NODE_ID                                 */
/* ========================================================================= */
#define UVH_NODE_ID 0x0UL
#define UV1H_NODE_ID 0x0UL
#define UV2H_NODE_ID 0x0UL
#define UV3H_NODE_ID 0x0UL

#define UVH_NODE_ID_FORCE1_SHFT				0
#define UVH_NODE_ID_MANUFACTURER_SHFT			1
#define UVH_NODE_ID_PART_NUMBER_SHFT			12
#define UVH_NODE_ID_REVISION_SHFT			28
#define UVH_NODE_ID_NODE_ID_SHFT			32
#define UVH_NODE_ID_FORCE1_MASK				0x0000000000000001UL
#define UVH_NODE_ID_MANUFACTURER_MASK			0x0000000000000ffeUL
#define UVH_NODE_ID_PART_NUMBER_MASK			0x000000000ffff000UL
#define UVH_NODE_ID_REVISION_MASK			0x00000000f0000000UL
#define UVH_NODE_ID_NODE_ID_MASK			0x00007fff00000000UL

#define UV1H_NODE_ID_FORCE1_SHFT			0
#define UV1H_NODE_ID_MANUFACTURER_SHFT			1
#define UV1H_NODE_ID_PART_NUMBER_SHFT			12
#define UV1H_NODE_ID_REVISION_SHFT			28
#define UV1H_NODE_ID_NODE_ID_SHFT			32
#define UV1H_NODE_ID_NODES_PER_BIT_SHFT			48
#define UV1H_NODE_ID_NI_PORT_SHFT			56
#define UV1H_NODE_ID_FORCE1_MASK			0x0000000000000001UL
#define UV1H_NODE_ID_MANUFACTURER_MASK			0x0000000000000ffeUL
#define UV1H_NODE_ID_PART_NUMBER_MASK			0x000000000ffff000UL
#define UV1H_NODE_ID_REVISION_MASK			0x00000000f0000000UL
#define UV1H_NODE_ID_NODE_ID_MASK			0x00007fff00000000UL
#define UV1H_NODE_ID_NODES_PER_BIT_MASK			0x007f000000000000UL
#define UV1H_NODE_ID_NI_PORT_MASK			0x0f00000000000000UL

#define UVXH_NODE_ID_FORCE1_SHFT			0
#define UVXH_NODE_ID_MANUFACTURER_SHFT			1
#define UVXH_NODE_ID_PART_NUMBER_SHFT			12
#define UVXH_NODE_ID_REVISION_SHFT			28
#define UVXH_NODE_ID_NODE_ID_SHFT			32
#define UVXH_NODE_ID_NODES_PER_BIT_SHFT			50
#define UVXH_NODE_ID_NI_PORT_SHFT			57
#define UVXH_NODE_ID_FORCE1_MASK			0x0000000000000001UL
#define UVXH_NODE_ID_MANUFACTURER_MASK			0x0000000000000ffeUL
#define UVXH_NODE_ID_PART_NUMBER_MASK			0x000000000ffff000UL
#define UVXH_NODE_ID_REVISION_MASK			0x00000000f0000000UL
#define UVXH_NODE_ID_NODE_ID_MASK			0x00007fff00000000UL
#define UVXH_NODE_ID_NODES_PER_BIT_MASK			0x01fc000000000000UL
#define UVXH_NODE_ID_NI_PORT_MASK			0x3e00000000000000UL

#define UV2H_NODE_ID_FORCE1_SHFT			0
#define UV2H_NODE_ID_MANUFACTURER_SHFT			1
#define UV2H_NODE_ID_PART_NUMBER_SHFT			12
#define UV2H_NODE_ID_REVISION_SHFT			28
#define UV2H_NODE_ID_NODE_ID_SHFT			32
#define UV2H_NODE_ID_NODES_PER_BIT_SHFT			50
#define UV2H_NODE_ID_NI_PORT_SHFT			57
#define UV2H_NODE_ID_FORCE1_MASK			0x0000000000000001UL
#define UV2H_NODE_ID_MANUFACTURER_MASK			0x0000000000000ffeUL
#define UV2H_NODE_ID_PART_NUMBER_MASK			0x000000000ffff000UL
#define UV2H_NODE_ID_REVISION_MASK			0x00000000f0000000UL
#define UV2H_NODE_ID_NODE_ID_MASK			0x00007fff00000000UL
#define UV2H_NODE_ID_NODES_PER_BIT_MASK			0x01fc000000000000UL
#define UV2H_NODE_ID_NI_PORT_MASK			0x3e00000000000000UL

#define UV3H_NODE_ID_FORCE1_SHFT			0
#define UV3H_NODE_ID_MANUFACTURER_SHFT			1
#define UV3H_NODE_ID_PART_NUMBER_SHFT			12
#define UV3H_NODE_ID_REVISION_SHFT			28
#define UV3H_NODE_ID_NODE_ID_SHFT			32
#define UV3H_NODE_ID_ROUTER_SELECT_SHFT			48
#define UV3H_NODE_ID_RESERVED_2_SHFT			49
#define UV3H_NODE_ID_NODES_PER_BIT_SHFT			50
#define UV3H_NODE_ID_NI_PORT_SHFT			57
#define UV3H_NODE_ID_FORCE1_MASK			0x0000000000000001UL
#define UV3H_NODE_ID_MANUFACTURER_MASK			0x0000000000000ffeUL
#define UV3H_NODE_ID_PART_NUMBER_MASK			0x000000000ffff000UL
#define UV3H_NODE_ID_REVISION_MASK			0x00000000f0000000UL
#define UV3H_NODE_ID_NODE_ID_MASK			0x00007fff00000000UL
#define UV3H_NODE_ID_ROUTER_SELECT_MASK			0x0001000000000000UL
#define UV3H_NODE_ID_RESERVED_2_MASK			0x0002000000000000UL
#define UV3H_NODE_ID_NODES_PER_BIT_MASK			0x01fc000000000000UL
#define UV3H_NODE_ID_NI_PORT_MASK			0x3e00000000000000UL

union uvh_node_id_u {
	unsigned long	v;
	struct uvh_node_id_s {
		unsigned long	force1:1;			/* RO */
		unsigned long	manufacturer:11;		/* RO */
		unsigned long	part_number:16;			/* RO */
		unsigned long	revision:4;			/* RO */
		unsigned long	node_id:15;			/* RW */
		unsigned long	rsvd_47_63:17;
	} s;
	struct uv1h_node_id_s {
		unsigned long	force1:1;			/* RO */
		unsigned long	manufacturer:11;		/* RO */
		unsigned long	part_number:16;			/* RO */
		unsigned long	revision:4;			/* RO */
		unsigned long	node_id:15;			/* RW */
		unsigned long	rsvd_47:1;
		unsigned long	nodes_per_bit:7;		/* RW */
		unsigned long	rsvd_55:1;
		unsigned long	ni_port:4;			/* RO */
		unsigned long	rsvd_60_63:4;
	} s1;
	struct uvxh_node_id_s {
		unsigned long	force1:1;			/* RO */
		unsigned long	manufacturer:11;		/* RO */
		unsigned long	part_number:16;			/* RO */
		unsigned long	revision:4;			/* RO */
		unsigned long	node_id:15;			/* RW */
		unsigned long	rsvd_47_49:3;
		unsigned long	nodes_per_bit:7;		/* RO */
		unsigned long	ni_port:5;			/* RO */
		unsigned long	rsvd_62_63:2;
	} sx;
	struct uv2h_node_id_s {
		unsigned long	force1:1;			/* RO */
		unsigned long	manufacturer:11;		/* RO */
		unsigned long	part_number:16;			/* RO */
		unsigned long	revision:4;			/* RO */
		unsigned long	node_id:15;			/* RW */
		unsigned long	rsvd_47_49:3;
		unsigned long	nodes_per_bit:7;		/* RO */
		unsigned long	ni_port:5;			/* RO */
		unsigned long	rsvd_62_63:2;
	} s2;
	struct uv3h_node_id_s {
		unsigned long	force1:1;			/* RO */
		unsigned long	manufacturer:11;		/* RO */
		unsigned long	part_number:16;			/* RO */
		unsigned long	revision:4;			/* RO */
		unsigned long	node_id:15;			/* RW */
		unsigned long	rsvd_47:1;
		unsigned long	router_select:1;		/* RO */
		unsigned long	rsvd_49:1;
		unsigned long	nodes_per_bit:7;		/* RO */
		unsigned long	ni_port:5;			/* RO */
		unsigned long	rsvd_62_63:2;
	} s3;
};

/* ========================================================================= */
/*                          UVH_NODE_PRESENT_TABLE                           */
/* ========================================================================= */
#define UVH_NODE_PRESENT_TABLE 0x1400UL
#define UVH_NODE_PRESENT_TABLE_DEPTH 16

#define UVH_NODE_PRESENT_TABLE_NODES_SHFT		0
#define UVH_NODE_PRESENT_TABLE_NODES_MASK		0xffffffffffffffffUL

union uvh_node_present_table_u {
	unsigned long	v;
	struct uvh_node_present_table_s {
		unsigned long	nodes:64;			/* RW */
	} s;
};

/* ========================================================================= */
/*                 UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_0_MMR                  */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_0_MMR 0x16000c8UL

#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_0_MMR_BASE_SHFT 24
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_0_MMR_M_ALIAS_SHFT 48
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_0_MMR_ENABLE_SHFT 63
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_0_MMR_BASE_MASK 0x00000000ff000000UL
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_0_MMR_M_ALIAS_MASK 0x001f000000000000UL
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_0_MMR_ENABLE_MASK 0x8000000000000000UL

union uvh_rh_gam_alias210_overlay_config_0_mmr_u {
	unsigned long	v;
	struct uvh_rh_gam_alias210_overlay_config_0_mmr_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s;
};

/* ========================================================================= */
/*                 UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_1_MMR                  */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_1_MMR 0x16000d8UL

#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_1_MMR_BASE_SHFT 24
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_1_MMR_M_ALIAS_SHFT 48
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_1_MMR_ENABLE_SHFT 63
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_1_MMR_BASE_MASK 0x00000000ff000000UL
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_1_MMR_M_ALIAS_MASK 0x001f000000000000UL
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_1_MMR_ENABLE_MASK 0x8000000000000000UL

union uvh_rh_gam_alias210_overlay_config_1_mmr_u {
	unsigned long	v;
	struct uvh_rh_gam_alias210_overlay_config_1_mmr_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s;
};

/* ========================================================================= */
/*                 UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_2_MMR                  */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_2_MMR 0x16000e8UL

#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_2_MMR_BASE_SHFT 24
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_2_MMR_M_ALIAS_SHFT 48
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_2_MMR_ENABLE_SHFT 63
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_2_MMR_BASE_MASK 0x00000000ff000000UL
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_2_MMR_M_ALIAS_MASK 0x001f000000000000UL
#define UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_2_MMR_ENABLE_MASK 0x8000000000000000UL

union uvh_rh_gam_alias210_overlay_config_2_mmr_u {
	unsigned long	v;
	struct uvh_rh_gam_alias210_overlay_config_2_mmr_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s;
};

/* ========================================================================= */
/*                UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR                  */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR 0x16000d0UL

#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR_DEST_BASE_SHFT 24
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR_DEST_BASE_MASK 0x00003fffff000000UL

union uvh_rh_gam_alias210_redirect_config_0_mmr_u {
	unsigned long	v;
	struct uvh_rh_gam_alias210_redirect_config_0_mmr_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s;
};

/* ========================================================================= */
/*                UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR                  */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR 0x16000e0UL

#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR_DEST_BASE_SHFT 24
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR_DEST_BASE_MASK 0x00003fffff000000UL

union uvh_rh_gam_alias210_redirect_config_1_mmr_u {
	unsigned long	v;
	struct uvh_rh_gam_alias210_redirect_config_1_mmr_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s;
};

/* ========================================================================= */
/*                UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR                  */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR 0x16000f0UL

#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR_DEST_BASE_SHFT 24
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR_DEST_BASE_MASK 0x00003fffff000000UL

union uvh_rh_gam_alias210_redirect_config_2_mmr_u {
	unsigned long	v;
	struct uvh_rh_gam_alias210_redirect_config_2_mmr_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s;
};

/* ========================================================================= */
/*                          UVH_RH_GAM_CONFIG_MMR                            */
/* ========================================================================= */
#define UVH_RH_GAM_CONFIG_MMR 0x1600000UL
#define UV1H_RH_GAM_CONFIG_MMR 0x1600000UL
#define UV2H_RH_GAM_CONFIG_MMR 0x1600000UL
#define UV3H_RH_GAM_CONFIG_MMR 0x1600000UL

#define UVH_RH_GAM_CONFIG_MMR_M_SKT_SHFT		0
#define UVH_RH_GAM_CONFIG_MMR_N_SKT_SHFT		6
#define UVH_RH_GAM_CONFIG_MMR_M_SKT_MASK		0x000000000000003fUL
#define UVH_RH_GAM_CONFIG_MMR_N_SKT_MASK		0x00000000000003c0UL

#define UV1H_RH_GAM_CONFIG_MMR_M_SKT_SHFT		0
#define UV1H_RH_GAM_CONFIG_MMR_N_SKT_SHFT		6
#define UV1H_RH_GAM_CONFIG_MMR_MMIOL_CFG_SHFT		12
#define UV1H_RH_GAM_CONFIG_MMR_M_SKT_MASK		0x000000000000003fUL
#define UV1H_RH_GAM_CONFIG_MMR_N_SKT_MASK		0x00000000000003c0UL
#define UV1H_RH_GAM_CONFIG_MMR_MMIOL_CFG_MASK		0x0000000000001000UL

#define UVXH_RH_GAM_CONFIG_MMR_M_SKT_SHFT		0
#define UVXH_RH_GAM_CONFIG_MMR_N_SKT_SHFT		6
#define UVXH_RH_GAM_CONFIG_MMR_M_SKT_MASK		0x000000000000003fUL
#define UVXH_RH_GAM_CONFIG_MMR_N_SKT_MASK		0x00000000000003c0UL

#define UV2H_RH_GAM_CONFIG_MMR_M_SKT_SHFT		0
#define UV2H_RH_GAM_CONFIG_MMR_N_SKT_SHFT		6
#define UV2H_RH_GAM_CONFIG_MMR_M_SKT_MASK		0x000000000000003fUL
#define UV2H_RH_GAM_CONFIG_MMR_N_SKT_MASK		0x00000000000003c0UL

#define UV3H_RH_GAM_CONFIG_MMR_M_SKT_SHFT		0
#define UV3H_RH_GAM_CONFIG_MMR_N_SKT_SHFT		6
#define UV3H_RH_GAM_CONFIG_MMR_M_SKT_MASK		0x000000000000003fUL
#define UV3H_RH_GAM_CONFIG_MMR_N_SKT_MASK		0x00000000000003c0UL

union uvh_rh_gam_config_mmr_u {
	unsigned long	v;
	struct uvh_rh_gam_config_mmr_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_63:54;
	} s;
	struct uv1h_rh_gam_config_mmr_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_11:2;
		unsigned long	mmiol_cfg:1;			/* RW */
		unsigned long	rsvd_13_63:51;
	} s1;
	struct uvxh_rh_gam_config_mmr_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_63:54;
	} sx;
	struct uv2h_rh_gam_config_mmr_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_63:54;
	} s2;
	struct uv3h_rh_gam_config_mmr_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_63:54;
	} s3;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR                      */
/* ========================================================================= */
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR 0x1600010UL
#define UV1H_RH_GAM_GRU_OVERLAY_CONFIG_MMR 0x1600010UL
#define UV2H_RH_GAM_GRU_OVERLAY_CONFIG_MMR 0x1600010UL
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MMR 0x1600010UL

#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT	28
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_SHFT	52
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffff0000000UL
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_MASK	0x00f0000000000000UL
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

#define UV1H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT	28
#define UV1H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_GR4_SHFT	48
#define UV1H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_SHFT	52
#define UV1H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UV1H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffff0000000UL
#define UV1H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_GR4_MASK	0x0001000000000000UL
#define UV1H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_MASK	0x00f0000000000000UL
#define UV1H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT	28
#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_SHFT	52
#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffff0000000UL
#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_MASK	0x00f0000000000000UL
#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

#define UV2H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT	28
#define UV2H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_SHFT	52
#define UV2H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UV2H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffff0000000UL
#define UV2H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_MASK	0x00f0000000000000UL
#define UV2H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT	28
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_SHFT	52
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_MODE_SHFT	62
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffff0000000UL
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_MASK	0x00f0000000000000UL
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_MODE_MASK	0x4000000000000000UL
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

union uvh_rh_gam_gru_overlay_config_mmr_u {
	unsigned long	v;
	struct uvh_rh_gam_gru_overlay_config_mmr_s {
		unsigned long	rsvd_0_27:28;
		unsigned long	base:18;			/* RW */
		unsigned long	rsvd_46_51:6;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s;
	struct uv1h_rh_gam_gru_overlay_config_mmr_s {
		unsigned long	rsvd_0_27:28;
		unsigned long	base:18;			/* RW */
		unsigned long	rsvd_46_47:2;
		unsigned long	gr4:1;				/* RW */
		unsigned long	rsvd_49_51:3;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s1;
	struct uvxh_rh_gam_gru_overlay_config_mmr_s {
		unsigned long	rsvd_0_27:28;
		unsigned long	base:18;			/* RW */
		unsigned long	rsvd_46_51:6;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} sx;
	struct uv2h_rh_gam_gru_overlay_config_mmr_s {
		unsigned long	rsvd_0_27:28;
		unsigned long	base:18;			/* RW */
		unsigned long	rsvd_46_51:6;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s2;
	struct uv3h_rh_gam_gru_overlay_config_mmr_s {
		unsigned long	rsvd_0_27:28;
		unsigned long	base:18;			/* RW */
		unsigned long	rsvd_46_51:6;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_61:6;
		unsigned long	mode:1;				/* RW */
		unsigned long	enable:1;			/* RW */
	} s3;
};

/* ========================================================================= */
/*                   UVH_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR                     */
/* ========================================================================= */
#define UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR 0x1600030UL
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR 0x1600030UL

#define UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_BASE_SHFT	30
#define UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_M_IO_SHFT	46
#define UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_N_IO_SHFT	52
#define UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_ENABLE_SHFT 63
#define UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003fffc0000000UL
#define UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_M_IO_MASK	0x000fc00000000000UL
#define UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_N_IO_MASK	0x00f0000000000000UL
#define UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_ENABLE_MASK 0x8000000000000000UL

#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_BASE_SHFT	27
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_M_IO_SHFT	46
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_N_IO_SHFT	52
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_ENABLE_SHFT 63
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffff8000000UL
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_M_IO_MASK	0x000fc00000000000UL
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_N_IO_MASK	0x00f0000000000000UL
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_ENABLE_MASK 0x8000000000000000UL

union uvh_rh_gam_mmioh_overlay_config_mmr_u {
	unsigned long	v;
	struct uv1h_rh_gam_mmioh_overlay_config_mmr_s {
		unsigned long	rsvd_0_29:30;
		unsigned long	base:16;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;				/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s1;
	struct uv2h_rh_gam_mmioh_overlay_config_mmr_s {
		unsigned long	rsvd_0_26:27;
		unsigned long	base:19;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;				/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s2;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR                      */
/* ========================================================================= */
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR 0x1600028UL
#define UV1H_RH_GAM_MMR_OVERLAY_CONFIG_MMR 0x1600028UL
#define UV2H_RH_GAM_MMR_OVERLAY_CONFIG_MMR 0x1600028UL
#define UV3H_RH_GAM_MMR_OVERLAY_CONFIG_MMR 0x1600028UL

#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT	26
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffffc000000UL
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

#define UV1H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT	26
#define UV1H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_DUAL_HUB_SHFT 46
#define UV1H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UV1H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffffc000000UL
#define UV1H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_DUAL_HUB_MASK 0x0000400000000000UL
#define UV1H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

#define UVXH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT	26
#define UVXH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UVXH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffffc000000UL
#define UVXH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

#define UV2H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT	26
#define UV2H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UV2H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffffc000000UL
#define UV2H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

#define UV3H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT	26
#define UV3H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_SHFT	63
#define UV3H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_MASK	0x00003ffffc000000UL
#define UV3H_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_MASK	0x8000000000000000UL

union uvh_rh_gam_mmr_overlay_config_mmr_u {
	unsigned long	v;
	struct uvh_rh_gam_mmr_overlay_config_mmr_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_62:17;
		unsigned long	enable:1;			/* RW */
	} s;
	struct uv1h_rh_gam_mmr_overlay_config_mmr_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	dual_hub:1;			/* RW */
		unsigned long	rsvd_47_62:16;
		unsigned long	enable:1;			/* RW */
	} s1;
	struct uvxh_rh_gam_mmr_overlay_config_mmr_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_62:17;
		unsigned long	enable:1;			/* RW */
	} sx;
	struct uv2h_rh_gam_mmr_overlay_config_mmr_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_62:17;
		unsigned long	enable:1;			/* RW */
	} s2;
	struct uv3h_rh_gam_mmr_overlay_config_mmr_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_62:17;
		unsigned long	enable:1;			/* RW */
	} s3;
};

/* ========================================================================= */
/*                                 UVH_RTC                                   */
/* ========================================================================= */
#define UVH_RTC 0x340000UL

#define UVH_RTC_REAL_TIME_CLOCK_SHFT			0
#define UVH_RTC_REAL_TIME_CLOCK_MASK			0x00ffffffffffffffUL

union uvh_rtc_u {
	unsigned long	v;
	struct uvh_rtc_s {
		unsigned long	real_time_clock:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s;
};

/* ========================================================================= */
/*                           UVH_RTC1_INT_CONFIG                             */
/* ========================================================================= */
#define UVH_RTC1_INT_CONFIG 0x615c0UL

#define UVH_RTC1_INT_CONFIG_VECTOR_SHFT			0
#define UVH_RTC1_INT_CONFIG_DM_SHFT			8
#define UVH_RTC1_INT_CONFIG_DESTMODE_SHFT		11
#define UVH_RTC1_INT_CONFIG_STATUS_SHFT			12
#define UVH_RTC1_INT_CONFIG_P_SHFT			13
#define UVH_RTC1_INT_CONFIG_T_SHFT			15
#define UVH_RTC1_INT_CONFIG_M_SHFT			16
#define UVH_RTC1_INT_CONFIG_APIC_ID_SHFT		32
#define UVH_RTC1_INT_CONFIG_VECTOR_MASK			0x00000000000000ffUL
#define UVH_RTC1_INT_CONFIG_DM_MASK			0x0000000000000700UL
#define UVH_RTC1_INT_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVH_RTC1_INT_CONFIG_STATUS_MASK			0x0000000000001000UL
#define UVH_RTC1_INT_CONFIG_P_MASK			0x0000000000002000UL
#define UVH_RTC1_INT_CONFIG_T_MASK			0x0000000000008000UL
#define UVH_RTC1_INT_CONFIG_M_MASK			0x0000000000010000UL
#define UVH_RTC1_INT_CONFIG_APIC_ID_MASK		0xffffffff00000000UL

union uvh_rtc1_int_config_u {
	unsigned long	v;
	struct uvh_rtc1_int_config_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	dm:3;				/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	status:1;			/* RO */
		unsigned long	p:1;				/* RO */
		unsigned long	rsvd_14:1;
		unsigned long	t:1;				/* RO */
		unsigned long	m:1;				/* RW */
		unsigned long	rsvd_17_31:15;
		unsigned long	apic_id:32;			/* RW */
	} s;
};

/* ========================================================================= */
/*                               UVH_SCRATCH5                                */
/* ========================================================================= */
#define UVH_SCRATCH5 0x2d0200UL
#define UVH_SCRATCH5_32 0x778

#define UVH_SCRATCH5_SCRATCH5_SHFT			0
#define UVH_SCRATCH5_SCRATCH5_MASK			0xffffffffffffffffUL

union uvh_scratch5_u {
	unsigned long	v;
	struct uvh_scratch5_s {
		unsigned long	scratch5:64;			/* RW, W1CS */
	} s;
};

/* ========================================================================= */
/*                            UVH_SCRATCH5_ALIAS                             */
/* ========================================================================= */
#define UVH_SCRATCH5_ALIAS 0x2d0208UL
#define UVH_SCRATCH5_ALIAS_32 0x780


/* ========================================================================= */
/*                           UVH_SCRATCH5_ALIAS_2                            */
/* ========================================================================= */
#define UVH_SCRATCH5_ALIAS_2 0x2d0210UL
#define UVH_SCRATCH5_ALIAS_2_32 0x788


/* ========================================================================= */
/*                          UVXH_EVENT_OCCURRED2                             */
/* ========================================================================= */
#define UVXH_EVENT_OCCURRED2 0x70100UL
#define UVXH_EVENT_OCCURRED2_32 0xb68

#define UVXH_EVENT_OCCURRED2_RTC_0_SHFT			0
#define UVXH_EVENT_OCCURRED2_RTC_1_SHFT			1
#define UVXH_EVENT_OCCURRED2_RTC_2_SHFT			2
#define UVXH_EVENT_OCCURRED2_RTC_3_SHFT			3
#define UVXH_EVENT_OCCURRED2_RTC_4_SHFT			4
#define UVXH_EVENT_OCCURRED2_RTC_5_SHFT			5
#define UVXH_EVENT_OCCURRED2_RTC_6_SHFT			6
#define UVXH_EVENT_OCCURRED2_RTC_7_SHFT			7
#define UVXH_EVENT_OCCURRED2_RTC_8_SHFT			8
#define UVXH_EVENT_OCCURRED2_RTC_9_SHFT			9
#define UVXH_EVENT_OCCURRED2_RTC_10_SHFT		10
#define UVXH_EVENT_OCCURRED2_RTC_11_SHFT		11
#define UVXH_EVENT_OCCURRED2_RTC_12_SHFT		12
#define UVXH_EVENT_OCCURRED2_RTC_13_SHFT		13
#define UVXH_EVENT_OCCURRED2_RTC_14_SHFT		14
#define UVXH_EVENT_OCCURRED2_RTC_15_SHFT		15
#define UVXH_EVENT_OCCURRED2_RTC_16_SHFT		16
#define UVXH_EVENT_OCCURRED2_RTC_17_SHFT		17
#define UVXH_EVENT_OCCURRED2_RTC_18_SHFT		18
#define UVXH_EVENT_OCCURRED2_RTC_19_SHFT		19
#define UVXH_EVENT_OCCURRED2_RTC_20_SHFT		20
#define UVXH_EVENT_OCCURRED2_RTC_21_SHFT		21
#define UVXH_EVENT_OCCURRED2_RTC_22_SHFT		22
#define UVXH_EVENT_OCCURRED2_RTC_23_SHFT		23
#define UVXH_EVENT_OCCURRED2_RTC_24_SHFT		24
#define UVXH_EVENT_OCCURRED2_RTC_25_SHFT		25
#define UVXH_EVENT_OCCURRED2_RTC_26_SHFT		26
#define UVXH_EVENT_OCCURRED2_RTC_27_SHFT		27
#define UVXH_EVENT_OCCURRED2_RTC_28_SHFT		28
#define UVXH_EVENT_OCCURRED2_RTC_29_SHFT		29
#define UVXH_EVENT_OCCURRED2_RTC_30_SHFT		30
#define UVXH_EVENT_OCCURRED2_RTC_31_SHFT		31
#define UVXH_EVENT_OCCURRED2_RTC_0_MASK			0x0000000000000001UL
#define UVXH_EVENT_OCCURRED2_RTC_1_MASK			0x0000000000000002UL
#define UVXH_EVENT_OCCURRED2_RTC_2_MASK			0x0000000000000004UL
#define UVXH_EVENT_OCCURRED2_RTC_3_MASK			0x0000000000000008UL
#define UVXH_EVENT_OCCURRED2_RTC_4_MASK			0x0000000000000010UL
#define UVXH_EVENT_OCCURRED2_RTC_5_MASK			0x0000000000000020UL
#define UVXH_EVENT_OCCURRED2_RTC_6_MASK			0x0000000000000040UL
#define UVXH_EVENT_OCCURRED2_RTC_7_MASK			0x0000000000000080UL
#define UVXH_EVENT_OCCURRED2_RTC_8_MASK			0x0000000000000100UL
#define UVXH_EVENT_OCCURRED2_RTC_9_MASK			0x0000000000000200UL
#define UVXH_EVENT_OCCURRED2_RTC_10_MASK		0x0000000000000400UL
#define UVXH_EVENT_OCCURRED2_RTC_11_MASK		0x0000000000000800UL
#define UVXH_EVENT_OCCURRED2_RTC_12_MASK		0x0000000000001000UL
#define UVXH_EVENT_OCCURRED2_RTC_13_MASK		0x0000000000002000UL
#define UVXH_EVENT_OCCURRED2_RTC_14_MASK		0x0000000000004000UL
#define UVXH_EVENT_OCCURRED2_RTC_15_MASK		0x0000000000008000UL
#define UVXH_EVENT_OCCURRED2_RTC_16_MASK		0x0000000000010000UL
#define UVXH_EVENT_OCCURRED2_RTC_17_MASK		0x0000000000020000UL
#define UVXH_EVENT_OCCURRED2_RTC_18_MASK		0x0000000000040000UL
#define UVXH_EVENT_OCCURRED2_RTC_19_MASK		0x0000000000080000UL
#define UVXH_EVENT_OCCURRED2_RTC_20_MASK		0x0000000000100000UL
#define UVXH_EVENT_OCCURRED2_RTC_21_MASK		0x0000000000200000UL
#define UVXH_EVENT_OCCURRED2_RTC_22_MASK		0x0000000000400000UL
#define UVXH_EVENT_OCCURRED2_RTC_23_MASK		0x0000000000800000UL
#define UVXH_EVENT_OCCURRED2_RTC_24_MASK		0x0000000001000000UL
#define UVXH_EVENT_OCCURRED2_RTC_25_MASK		0x0000000002000000UL
#define UVXH_EVENT_OCCURRED2_RTC_26_MASK		0x0000000004000000UL
#define UVXH_EVENT_OCCURRED2_RTC_27_MASK		0x0000000008000000UL
#define UVXH_EVENT_OCCURRED2_RTC_28_MASK		0x0000000010000000UL
#define UVXH_EVENT_OCCURRED2_RTC_29_MASK		0x0000000020000000UL
#define UVXH_EVENT_OCCURRED2_RTC_30_MASK		0x0000000040000000UL
#define UVXH_EVENT_OCCURRED2_RTC_31_MASK		0x0000000080000000UL

union uvxh_event_occurred2_u {
	unsigned long	v;
	struct uvxh_event_occurred2_s {
		unsigned long	rtc_0:1;			/* RW */
		unsigned long	rtc_1:1;			/* RW */
		unsigned long	rtc_2:1;			/* RW */
		unsigned long	rtc_3:1;			/* RW */
		unsigned long	rtc_4:1;			/* RW */
		unsigned long	rtc_5:1;			/* RW */
		unsigned long	rtc_6:1;			/* RW */
		unsigned long	rtc_7:1;			/* RW */
		unsigned long	rtc_8:1;			/* RW */
		unsigned long	rtc_9:1;			/* RW */
		unsigned long	rtc_10:1;			/* RW */
		unsigned long	rtc_11:1;			/* RW */
		unsigned long	rtc_12:1;			/* RW */
		unsigned long	rtc_13:1;			/* RW */
		unsigned long	rtc_14:1;			/* RW */
		unsigned long	rtc_15:1;			/* RW */
		unsigned long	rtc_16:1;			/* RW */
		unsigned long	rtc_17:1;			/* RW */
		unsigned long	rtc_18:1;			/* RW */
		unsigned long	rtc_19:1;			/* RW */
		unsigned long	rtc_20:1;			/* RW */
		unsigned long	rtc_21:1;			/* RW */
		unsigned long	rtc_22:1;			/* RW */
		unsigned long	rtc_23:1;			/* RW */
		unsigned long	rtc_24:1;			/* RW */
		unsigned long	rtc_25:1;			/* RW */
		unsigned long	rtc_26:1;			/* RW */
		unsigned long	rtc_27:1;			/* RW */
		unsigned long	rtc_28:1;			/* RW */
		unsigned long	rtc_29:1;			/* RW */
		unsigned long	rtc_30:1;			/* RW */
		unsigned long	rtc_31:1;			/* RW */
		unsigned long	rsvd_32_63:32;
	} sx;
};

/* ========================================================================= */
/*                       UVXH_EVENT_OCCURRED2_ALIAS                          */
/* ========================================================================= */
#define UVXH_EVENT_OCCURRED2_ALIAS 0x70108UL
#define UVXH_EVENT_OCCURRED2_ALIAS_32 0xb70


/* ========================================================================= */
/*                   UVXH_LB_BAU_SB_ACTIVATION_STATUS_2                      */
/* ========================================================================= */
#define UVXH_LB_BAU_SB_ACTIVATION_STATUS_2 0x320130UL
#define UV2H_LB_BAU_SB_ACTIVATION_STATUS_2 0x320130UL
#define UV3H_LB_BAU_SB_ACTIVATION_STATUS_2 0x320130UL
#define UVXH_LB_BAU_SB_ACTIVATION_STATUS_2_32 0x9f0
#define UV2H_LB_BAU_SB_ACTIVATION_STATUS_2_32 0x320130UL
#define UV3H_LB_BAU_SB_ACTIVATION_STATUS_2_32 0x320130UL

#define UVXH_LB_BAU_SB_ACTIVATION_STATUS_2_AUX_ERROR_SHFT 0
#define UVXH_LB_BAU_SB_ACTIVATION_STATUS_2_AUX_ERROR_MASK 0xffffffffffffffffUL

#define UV2H_LB_BAU_SB_ACTIVATION_STATUS_2_AUX_ERROR_SHFT 0
#define UV2H_LB_BAU_SB_ACTIVATION_STATUS_2_AUX_ERROR_MASK 0xffffffffffffffffUL

#define UV3H_LB_BAU_SB_ACTIVATION_STATUS_2_AUX_ERROR_SHFT 0
#define UV3H_LB_BAU_SB_ACTIVATION_STATUS_2_AUX_ERROR_MASK 0xffffffffffffffffUL

union uvxh_lb_bau_sb_activation_status_2_u {
	unsigned long	v;
	struct uvxh_lb_bau_sb_activation_status_2_s {
		unsigned long	aux_error:64;			/* RW */
	} sx;
	struct uv2h_lb_bau_sb_activation_status_2_s {
		unsigned long	aux_error:64;			/* RW */
	} s2;
	struct uv3h_lb_bau_sb_activation_status_2_s {
		unsigned long	aux_error:64;			/* RW */
	} s3;
};

/* ========================================================================= */
/*                   UV1H_LB_TARGET_PHYSICAL_APIC_ID_MASK                    */
/* ========================================================================= */
#define UV1H_LB_TARGET_PHYSICAL_APIC_ID_MASK		0x320130UL
#define UV1H_LB_TARGET_PHYSICAL_APIC_ID_MASK_32		0x9f0

#define UV1H_LB_TARGET_PHYSICAL_APIC_ID_MASK_BIT_ENABLES_SHFT 0
#define UV1H_LB_TARGET_PHYSICAL_APIC_ID_MASK_BIT_ENABLES_MASK 0x00000000ffffffffUL

union uv1h_lb_target_physical_apic_id_mask_u {
	unsigned long	v;
	struct uv1h_lb_target_physical_apic_id_mask_s {
		unsigned long	bit_enables:32;			/* RW */
		unsigned long	rsvd_32_63:32;
	} s1;
};

/* ========================================================================= */
/*                          UV3H_GR0_GAM_GR_CONFIG                           */
/* ========================================================================= */
#define UV3H_GR0_GAM_GR_CONFIG				0xc00028UL

#define UV3H_GR0_GAM_GR_CONFIG_M_SKT_SHFT		0
#define UV3H_GR0_GAM_GR_CONFIG_SUBSPACE_SHFT		10
#define UV3H_GR0_GAM_GR_CONFIG_M_SKT_MASK		0x000000000000003fUL
#define UV3H_GR0_GAM_GR_CONFIG_SUBSPACE_MASK		0x0000000000000400UL

union uv3h_gr0_gam_gr_config_u {
	unsigned long	v;
	struct uv3h_gr0_gam_gr_config_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	undef_6_9:4;			/* Undefined */
		unsigned long	subspace:1;			/* RW */
		unsigned long	reserved:53;
	} s3;
};

/* ========================================================================= */
/*                          UV3H_GR1_GAM_GR_CONFIG                           */
/* ========================================================================= */
#define UV3H_GR1_GAM_GR_CONFIG				0x1000028UL

#define UV3H_GR1_GAM_GR_CONFIG_M_SKT_SHFT		0
#define UV3H_GR1_GAM_GR_CONFIG_SUBSPACE_SHFT		10
#define UV3H_GR1_GAM_GR_CONFIG_M_SKT_MASK		0x000000000000003fUL
#define UV3H_GR1_GAM_GR_CONFIG_SUBSPACE_MASK		0x0000000000000400UL

union uv3h_gr1_gam_gr_config_u {
	unsigned long	v;
	struct uv3h_gr1_gam_gr_config_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	undef_6_9:4;			/* Undefined */
		unsigned long	subspace:1;			/* RW */
		unsigned long	reserved:53;
	} s3;
};

/* ========================================================================= */
/*                   UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR                   */
/* ========================================================================= */
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR		0x1603000UL

#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR_BASE_SHFT	26
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR_M_IO_SHFT	46
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR_ENABLE_SHFT 63
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR_BASE_MASK	0x00003ffffc000000UL
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR_M_IO_MASK	0x000fc00000000000UL
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR_ENABLE_MASK 0x8000000000000000UL

union uv3h_rh_gam_mmioh_overlay_config0_mmr_u {
	unsigned long	v;
	struct uv3h_rh_gam_mmioh_overlay_config0_mmr_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s3;
};

/* ========================================================================= */
/*                   UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_MMR                   */
/* ========================================================================= */
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_MMR		0x1604000UL

#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_MMR_BASE_SHFT	26
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_MMR_M_IO_SHFT	46
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_MMR_ENABLE_SHFT 63
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_MMR_BASE_MASK	0x00003ffffc000000UL
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_MMR_M_IO_MASK	0x000fc00000000000UL
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_MMR_ENABLE_MASK 0x8000000000000000UL

union uv3h_rh_gam_mmioh_overlay_config1_mmr_u {
	unsigned long	v;
	struct uv3h_rh_gam_mmioh_overlay_config1_mmr_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s3;
};

/* ========================================================================= */
/*                  UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_MMR                   */
/* ========================================================================= */
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_MMR		0x1603800UL
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_MMR_DEPTH	128

#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_MMR_NASID_SHFT 0
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_MMR_NASID_MASK 0x0000000000007fffUL

union uv3h_rh_gam_mmioh_redirect_config0_mmr_u {
	unsigned long	v;
	struct uv3h_rh_gam_mmioh_redirect_config0_mmr_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} s3;
};

/* ========================================================================= */
/*                  UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG1_MMR                   */
/* ========================================================================= */
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG1_MMR		0x1604800UL
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG1_MMR_DEPTH	128

#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG1_MMR_NASID_SHFT 0
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG1_MMR_NASID_MASK 0x0000000000007fffUL

union uv3h_rh_gam_mmioh_redirect_config1_mmr_u {
	unsigned long	v;
	struct uv3h_rh_gam_mmioh_redirect_config1_mmr_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} s3;
};


#endif /* _ASM_X86_UV_UV_MMRS_H */
