/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * HPE UV MMR definitions
 *
 * (C) Copyright 2020 Hewlett Packard Enterprise Development LP
 * Copyright (C) 2007-2016 Silicon Graphics, Inc. All rights reserved.
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
 * UVXH - definitions common to UVX class (2, 3, 4).
 * UVYH - definitions common to UVY class (5).
 * UV5H - definitions specific to UV type 5 hub.
 * UV4AH - definitions specific to UV type 4A hub.
 * UV4H - definitions specific to UV type 4 hub.
 * UV3H - definitions specific to UV type 3 hub.
 * UV2H - definitions specific to UV type 2 hub.
 *
 * If the MMR exists on all hub types but have different addresses,
 * use a conditional operator to define the value at runtime.  Any
 * that are not defined are blank.
 *	(UV4A variations only generated if different from uv4)
 *	#define UVHxxx (
 *		is_uv(UV5) ? UV5Hxxx value :
 *		is_uv(UV4A) ? UV4AHxxx value :
 *		is_uv(UV4) ? UV4Hxxx value :
 *		is_uv(UV3) ? UV3Hxxx value :
 *		is_uv(UV2) ? UV2Hxxx value :
 *		<ucv> or <undef value>)
 *
 * Class UVX has UVs (2|3|4|4A).
 * Class UVY has UVs (5).
 *
 *	union uvh_xxx {
 *		unsigned long       v;
 *		struct uvh_xxx_s {	 # Common fields only
 *		} s;
 *		struct uv5h_xxx_s {	 # Full UV5 definition (*)
 *		} s5;
 *		struct uv4ah_xxx_s {	 # Full UV4A definition (*)
 *		} s4a;
 *		struct uv4h_xxx_s {	 # Full UV4 definition (*)
 *		} s4;
 *		struct uv3h_xxx_s {	 # Full UV3 definition (*)
 *		} s3;
 *		struct uv2h_xxx_s {	 # Full UV2 definition (*)
 *		} s2;
 *	};
 *		(* - if present and different than the common struct)
 *
 * Only essential differences are enumerated. For example, if the address is
 * the same for all UV's, only a single #define is generated. Likewise,
 * if the contents is the same for all hubs, only the "s" structure is
 * generated.
 *
 * (GEN Flags: undefs=function)
 */

 /* UV bit masks */
#define	UV2	(1 << 0)
#define	UV3	(1 << 1)
#define	UV4	(1 << 2)
#define	UV4A	(1 << 3)
#define	UV5	(1 << 4)
#define	UVX	(UV2|UV3|UV4)
#define	UVY	(UV5)
#define	UV_ANY	(~0)




#define UV_MMR_ENABLE		(1UL << 63)

#define UV1_HUB_PART_NUMBER	0x88a5
#define UV2_HUB_PART_NUMBER	0x8eb8
#define UV2_HUB_PART_NUMBER_X	0x1111
#define UV3_HUB_PART_NUMBER	0x9578
#define UV3_HUB_PART_NUMBER_X	0x4321
#define UV4_HUB_PART_NUMBER	0x99a1
#define UV5_HUB_PART_NUMBER	0xa171

/* Error function to catch undefined references */
extern unsigned long uv_undefined(char *str);

/* ========================================================================= */
/*                           UVH_EVENT_OCCURRED0                             */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED0 0x70000UL

/* UVH common defines*/
#define UVH_EVENT_OCCURRED0_LB_HCERR_SHFT		0
#define UVH_EVENT_OCCURRED0_LB_HCERR_MASK		0x0000000000000001UL

/* UVXH common defines */
#define UVXH_EVENT_OCCURRED0_RH_HCERR_SHFT		2
#define UVXH_EVENT_OCCURRED0_RH_HCERR_MASK		0x0000000000000004UL
#define UVXH_EVENT_OCCURRED0_LH0_HCERR_SHFT		3
#define UVXH_EVENT_OCCURRED0_LH0_HCERR_MASK		0x0000000000000008UL
#define UVXH_EVENT_OCCURRED0_LH1_HCERR_SHFT		4
#define UVXH_EVENT_OCCURRED0_LH1_HCERR_MASK		0x0000000000000010UL
#define UVXH_EVENT_OCCURRED0_GR0_HCERR_SHFT		5
#define UVXH_EVENT_OCCURRED0_GR0_HCERR_MASK		0x0000000000000020UL
#define UVXH_EVENT_OCCURRED0_GR1_HCERR_SHFT		6
#define UVXH_EVENT_OCCURRED0_GR1_HCERR_MASK		0x0000000000000040UL
#define UVXH_EVENT_OCCURRED0_NI0_HCERR_SHFT		7
#define UVXH_EVENT_OCCURRED0_NI0_HCERR_MASK		0x0000000000000080UL
#define UVXH_EVENT_OCCURRED0_NI1_HCERR_SHFT		8
#define UVXH_EVENT_OCCURRED0_NI1_HCERR_MASK		0x0000000000000100UL
#define UVXH_EVENT_OCCURRED0_LB_AOERR0_SHFT		9
#define UVXH_EVENT_OCCURRED0_LB_AOERR0_MASK		0x0000000000000200UL
#define UVXH_EVENT_OCCURRED0_RH_AOERR0_SHFT		11
#define UVXH_EVENT_OCCURRED0_RH_AOERR0_MASK		0x0000000000000800UL
#define UVXH_EVENT_OCCURRED0_LH0_AOERR0_SHFT		12
#define UVXH_EVENT_OCCURRED0_LH0_AOERR0_MASK		0x0000000000001000UL
#define UVXH_EVENT_OCCURRED0_LH1_AOERR0_SHFT		13
#define UVXH_EVENT_OCCURRED0_LH1_AOERR0_MASK		0x0000000000002000UL
#define UVXH_EVENT_OCCURRED0_GR0_AOERR0_SHFT		14
#define UVXH_EVENT_OCCURRED0_GR0_AOERR0_MASK		0x0000000000004000UL
#define UVXH_EVENT_OCCURRED0_GR1_AOERR0_SHFT		15
#define UVXH_EVENT_OCCURRED0_GR1_AOERR0_MASK		0x0000000000008000UL
#define UVXH_EVENT_OCCURRED0_XB_AOERR0_SHFT		16
#define UVXH_EVENT_OCCURRED0_XB_AOERR0_MASK		0x0000000000010000UL

/* UVYH common defines */
#define UVYH_EVENT_OCCURRED0_KT_HCERR_SHFT		1
#define UVYH_EVENT_OCCURRED0_KT_HCERR_MASK		0x0000000000000002UL
#define UVYH_EVENT_OCCURRED0_RH0_HCERR_SHFT		2
#define UVYH_EVENT_OCCURRED0_RH0_HCERR_MASK		0x0000000000000004UL
#define UVYH_EVENT_OCCURRED0_RH1_HCERR_SHFT		3
#define UVYH_EVENT_OCCURRED0_RH1_HCERR_MASK		0x0000000000000008UL
#define UVYH_EVENT_OCCURRED0_LH0_HCERR_SHFT		4
#define UVYH_EVENT_OCCURRED0_LH0_HCERR_MASK		0x0000000000000010UL
#define UVYH_EVENT_OCCURRED0_LH1_HCERR_SHFT		5
#define UVYH_EVENT_OCCURRED0_LH1_HCERR_MASK		0x0000000000000020UL
#define UVYH_EVENT_OCCURRED0_LH2_HCERR_SHFT		6
#define UVYH_EVENT_OCCURRED0_LH2_HCERR_MASK		0x0000000000000040UL
#define UVYH_EVENT_OCCURRED0_LH3_HCERR_SHFT		7
#define UVYH_EVENT_OCCURRED0_LH3_HCERR_MASK		0x0000000000000080UL
#define UVYH_EVENT_OCCURRED0_XB_HCERR_SHFT		8
#define UVYH_EVENT_OCCURRED0_XB_HCERR_MASK		0x0000000000000100UL
#define UVYH_EVENT_OCCURRED0_RDM_HCERR_SHFT		9
#define UVYH_EVENT_OCCURRED0_RDM_HCERR_MASK		0x0000000000000200UL
#define UVYH_EVENT_OCCURRED0_NI0_HCERR_SHFT		10
#define UVYH_EVENT_OCCURRED0_NI0_HCERR_MASK		0x0000000000000400UL
#define UVYH_EVENT_OCCURRED0_NI1_HCERR_SHFT		11
#define UVYH_EVENT_OCCURRED0_NI1_HCERR_MASK		0x0000000000000800UL
#define UVYH_EVENT_OCCURRED0_LB_AOERR0_SHFT		12
#define UVYH_EVENT_OCCURRED0_LB_AOERR0_MASK		0x0000000000001000UL
#define UVYH_EVENT_OCCURRED0_KT_AOERR0_SHFT		13
#define UVYH_EVENT_OCCURRED0_KT_AOERR0_MASK		0x0000000000002000UL
#define UVYH_EVENT_OCCURRED0_RH0_AOERR0_SHFT		14
#define UVYH_EVENT_OCCURRED0_RH0_AOERR0_MASK		0x0000000000004000UL
#define UVYH_EVENT_OCCURRED0_RH1_AOERR0_SHFT		15
#define UVYH_EVENT_OCCURRED0_RH1_AOERR0_MASK		0x0000000000008000UL
#define UVYH_EVENT_OCCURRED0_LH0_AOERR0_SHFT		16
#define UVYH_EVENT_OCCURRED0_LH0_AOERR0_MASK		0x0000000000010000UL
#define UVYH_EVENT_OCCURRED0_LH1_AOERR0_SHFT		17
#define UVYH_EVENT_OCCURRED0_LH1_AOERR0_MASK		0x0000000000020000UL
#define UVYH_EVENT_OCCURRED0_LH2_AOERR0_SHFT		18
#define UVYH_EVENT_OCCURRED0_LH2_AOERR0_MASK		0x0000000000040000UL
#define UVYH_EVENT_OCCURRED0_LH3_AOERR0_SHFT		19
#define UVYH_EVENT_OCCURRED0_LH3_AOERR0_MASK		0x0000000000080000UL
#define UVYH_EVENT_OCCURRED0_XB_AOERR0_SHFT		20
#define UVYH_EVENT_OCCURRED0_XB_AOERR0_MASK		0x0000000000100000UL
#define UVYH_EVENT_OCCURRED0_RDM_AOERR0_SHFT		21
#define UVYH_EVENT_OCCURRED0_RDM_AOERR0_MASK		0x0000000000200000UL
#define UVYH_EVENT_OCCURRED0_RT0_AOERR0_SHFT		22
#define UVYH_EVENT_OCCURRED0_RT0_AOERR0_MASK		0x0000000000400000UL
#define UVYH_EVENT_OCCURRED0_RT1_AOERR0_SHFT		23
#define UVYH_EVENT_OCCURRED0_RT1_AOERR0_MASK		0x0000000000800000UL
#define UVYH_EVENT_OCCURRED0_NI0_AOERR0_SHFT		24
#define UVYH_EVENT_OCCURRED0_NI0_AOERR0_MASK		0x0000000001000000UL
#define UVYH_EVENT_OCCURRED0_NI1_AOERR0_SHFT		25
#define UVYH_EVENT_OCCURRED0_NI1_AOERR0_MASK		0x0000000002000000UL
#define UVYH_EVENT_OCCURRED0_LB_AOERR1_SHFT		26
#define UVYH_EVENT_OCCURRED0_LB_AOERR1_MASK		0x0000000004000000UL
#define UVYH_EVENT_OCCURRED0_KT_AOERR1_SHFT		27
#define UVYH_EVENT_OCCURRED0_KT_AOERR1_MASK		0x0000000008000000UL
#define UVYH_EVENT_OCCURRED0_RH0_AOERR1_SHFT		28
#define UVYH_EVENT_OCCURRED0_RH0_AOERR1_MASK		0x0000000010000000UL
#define UVYH_EVENT_OCCURRED0_RH1_AOERR1_SHFT		29
#define UVYH_EVENT_OCCURRED0_RH1_AOERR1_MASK		0x0000000020000000UL
#define UVYH_EVENT_OCCURRED0_LH0_AOERR1_SHFT		30
#define UVYH_EVENT_OCCURRED0_LH0_AOERR1_MASK		0x0000000040000000UL
#define UVYH_EVENT_OCCURRED0_LH1_AOERR1_SHFT		31
#define UVYH_EVENT_OCCURRED0_LH1_AOERR1_MASK		0x0000000080000000UL
#define UVYH_EVENT_OCCURRED0_LH2_AOERR1_SHFT		32
#define UVYH_EVENT_OCCURRED0_LH2_AOERR1_MASK		0x0000000100000000UL
#define UVYH_EVENT_OCCURRED0_LH3_AOERR1_SHFT		33
#define UVYH_EVENT_OCCURRED0_LH3_AOERR1_MASK		0x0000000200000000UL
#define UVYH_EVENT_OCCURRED0_XB_AOERR1_SHFT		34
#define UVYH_EVENT_OCCURRED0_XB_AOERR1_MASK		0x0000000400000000UL
#define UVYH_EVENT_OCCURRED0_RDM_AOERR1_SHFT		35
#define UVYH_EVENT_OCCURRED0_RDM_AOERR1_MASK		0x0000000800000000UL
#define UVYH_EVENT_OCCURRED0_RT0_AOERR1_SHFT		36
#define UVYH_EVENT_OCCURRED0_RT0_AOERR1_MASK		0x0000001000000000UL
#define UVYH_EVENT_OCCURRED0_RT1_AOERR1_SHFT		37
#define UVYH_EVENT_OCCURRED0_RT1_AOERR1_MASK		0x0000002000000000UL
#define UVYH_EVENT_OCCURRED0_NI0_AOERR1_SHFT		38
#define UVYH_EVENT_OCCURRED0_NI0_AOERR1_MASK		0x0000004000000000UL
#define UVYH_EVENT_OCCURRED0_NI1_AOERR1_SHFT		39
#define UVYH_EVENT_OCCURRED0_NI1_AOERR1_MASK		0x0000008000000000UL
#define UVYH_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_SHFT	40
#define UVYH_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_MASK	0x0000010000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_0_SHFT		41
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_0_MASK		0x0000020000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_1_SHFT		42
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_1_MASK		0x0000040000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_2_SHFT		43
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_2_MASK		0x0000080000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_3_SHFT		44
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_3_MASK		0x0000100000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_4_SHFT		45
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_4_MASK		0x0000200000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_5_SHFT		46
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_5_MASK		0x0000400000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_6_SHFT		47
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_6_MASK		0x0000800000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_7_SHFT		48
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_7_MASK		0x0001000000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_8_SHFT		49
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_8_MASK		0x0002000000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_9_SHFT		50
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_9_MASK		0x0004000000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_10_SHFT		51
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_10_MASK		0x0008000000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_11_SHFT		52
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_11_MASK		0x0010000000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_12_SHFT		53
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_12_MASK		0x0020000000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_13_SHFT		54
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_13_MASK		0x0040000000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_14_SHFT		55
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_14_MASK		0x0080000000000000UL
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_15_SHFT		56
#define UVYH_EVENT_OCCURRED0_LB_IRQ_INT_15_MASK		0x0100000000000000UL
#define UVYH_EVENT_OCCURRED0_L1_NMI_INT_SHFT		57
#define UVYH_EVENT_OCCURRED0_L1_NMI_INT_MASK		0x0200000000000000UL
#define UVYH_EVENT_OCCURRED0_STOP_CLOCK_SHFT		58
#define UVYH_EVENT_OCCURRED0_STOP_CLOCK_MASK		0x0400000000000000UL
#define UVYH_EVENT_OCCURRED0_ASIC_TO_L1_SHFT		59
#define UVYH_EVENT_OCCURRED0_ASIC_TO_L1_MASK		0x0800000000000000UL
#define UVYH_EVENT_OCCURRED0_L1_TO_ASIC_SHFT		60
#define UVYH_EVENT_OCCURRED0_L1_TO_ASIC_MASK		0x1000000000000000UL
#define UVYH_EVENT_OCCURRED0_LA_SEQ_TRIGGER_SHFT	61
#define UVYH_EVENT_OCCURRED0_LA_SEQ_TRIGGER_MASK	0x2000000000000000UL

/* UV4 unique defines */
#define UV4H_EVENT_OCCURRED0_KT_HCERR_SHFT		1
#define UV4H_EVENT_OCCURRED0_KT_HCERR_MASK		0x0000000000000002UL
#define UV4H_EVENT_OCCURRED0_KT_AOERR0_SHFT		10
#define UV4H_EVENT_OCCURRED0_KT_AOERR0_MASK		0x0000000000000400UL
#define UV4H_EVENT_OCCURRED0_RTQ0_AOERR0_SHFT		17
#define UV4H_EVENT_OCCURRED0_RTQ0_AOERR0_MASK		0x0000000000020000UL
#define UV4H_EVENT_OCCURRED0_RTQ1_AOERR0_SHFT		18
#define UV4H_EVENT_OCCURRED0_RTQ1_AOERR0_MASK		0x0000000000040000UL
#define UV4H_EVENT_OCCURRED0_RTQ2_AOERR0_SHFT		19
#define UV4H_EVENT_OCCURRED0_RTQ2_AOERR0_MASK		0x0000000000080000UL
#define UV4H_EVENT_OCCURRED0_RTQ3_AOERR0_SHFT		20
#define UV4H_EVENT_OCCURRED0_RTQ3_AOERR0_MASK		0x0000000000100000UL
#define UV4H_EVENT_OCCURRED0_NI0_AOERR0_SHFT		21
#define UV4H_EVENT_OCCURRED0_NI0_AOERR0_MASK		0x0000000000200000UL
#define UV4H_EVENT_OCCURRED0_NI1_AOERR0_SHFT		22
#define UV4H_EVENT_OCCURRED0_NI1_AOERR0_MASK		0x0000000000400000UL
#define UV4H_EVENT_OCCURRED0_LB_AOERR1_SHFT		23
#define UV4H_EVENT_OCCURRED0_LB_AOERR1_MASK		0x0000000000800000UL
#define UV4H_EVENT_OCCURRED0_KT_AOERR1_SHFT		24
#define UV4H_EVENT_OCCURRED0_KT_AOERR1_MASK		0x0000000001000000UL
#define UV4H_EVENT_OCCURRED0_RH_AOERR1_SHFT		25
#define UV4H_EVENT_OCCURRED0_RH_AOERR1_MASK		0x0000000002000000UL
#define UV4H_EVENT_OCCURRED0_LH0_AOERR1_SHFT		26
#define UV4H_EVENT_OCCURRED0_LH0_AOERR1_MASK		0x0000000004000000UL
#define UV4H_EVENT_OCCURRED0_LH1_AOERR1_SHFT		27
#define UV4H_EVENT_OCCURRED0_LH1_AOERR1_MASK		0x0000000008000000UL
#define UV4H_EVENT_OCCURRED0_GR0_AOERR1_SHFT		28
#define UV4H_EVENT_OCCURRED0_GR0_AOERR1_MASK		0x0000000010000000UL
#define UV4H_EVENT_OCCURRED0_GR1_AOERR1_SHFT		29
#define UV4H_EVENT_OCCURRED0_GR1_AOERR1_MASK		0x0000000020000000UL
#define UV4H_EVENT_OCCURRED0_XB_AOERR1_SHFT		30
#define UV4H_EVENT_OCCURRED0_XB_AOERR1_MASK		0x0000000040000000UL
#define UV4H_EVENT_OCCURRED0_RTQ0_AOERR1_SHFT		31
#define UV4H_EVENT_OCCURRED0_RTQ0_AOERR1_MASK		0x0000000080000000UL
#define UV4H_EVENT_OCCURRED0_RTQ1_AOERR1_SHFT		32
#define UV4H_EVENT_OCCURRED0_RTQ1_AOERR1_MASK		0x0000000100000000UL
#define UV4H_EVENT_OCCURRED0_RTQ2_AOERR1_SHFT		33
#define UV4H_EVENT_OCCURRED0_RTQ2_AOERR1_MASK		0x0000000200000000UL
#define UV4H_EVENT_OCCURRED0_RTQ3_AOERR1_SHFT		34
#define UV4H_EVENT_OCCURRED0_RTQ3_AOERR1_MASK		0x0000000400000000UL
#define UV4H_EVENT_OCCURRED0_NI0_AOERR1_SHFT		35
#define UV4H_EVENT_OCCURRED0_NI0_AOERR1_MASK		0x0000000800000000UL
#define UV4H_EVENT_OCCURRED0_NI1_AOERR1_SHFT		36
#define UV4H_EVENT_OCCURRED0_NI1_AOERR1_MASK		0x0000001000000000UL
#define UV4H_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_SHFT	37
#define UV4H_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_MASK	0x0000002000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_0_SHFT		38
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_0_MASK		0x0000004000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_1_SHFT		39
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_1_MASK		0x0000008000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_2_SHFT		40
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_2_MASK		0x0000010000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_3_SHFT		41
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_3_MASK		0x0000020000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_4_SHFT		42
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_4_MASK		0x0000040000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_5_SHFT		43
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_5_MASK		0x0000080000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_6_SHFT		44
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_6_MASK		0x0000100000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_7_SHFT		45
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_7_MASK		0x0000200000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_8_SHFT		46
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_8_MASK		0x0000400000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_9_SHFT		47
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_9_MASK		0x0000800000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_10_SHFT		48
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_10_MASK		0x0001000000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_11_SHFT		49
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_11_MASK		0x0002000000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_12_SHFT		50
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_12_MASK		0x0004000000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_13_SHFT		51
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_13_MASK		0x0008000000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_14_SHFT		52
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_14_MASK		0x0010000000000000UL
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_15_SHFT		53
#define UV4H_EVENT_OCCURRED0_LB_IRQ_INT_15_MASK		0x0020000000000000UL
#define UV4H_EVENT_OCCURRED0_L1_NMI_INT_SHFT		54
#define UV4H_EVENT_OCCURRED0_L1_NMI_INT_MASK		0x0040000000000000UL
#define UV4H_EVENT_OCCURRED0_STOP_CLOCK_SHFT		55
#define UV4H_EVENT_OCCURRED0_STOP_CLOCK_MASK		0x0080000000000000UL
#define UV4H_EVENT_OCCURRED0_ASIC_TO_L1_SHFT		56
#define UV4H_EVENT_OCCURRED0_ASIC_TO_L1_MASK		0x0100000000000000UL
#define UV4H_EVENT_OCCURRED0_L1_TO_ASIC_SHFT		57
#define UV4H_EVENT_OCCURRED0_L1_TO_ASIC_MASK		0x0200000000000000UL
#define UV4H_EVENT_OCCURRED0_LA_SEQ_TRIGGER_SHFT	58
#define UV4H_EVENT_OCCURRED0_LA_SEQ_TRIGGER_MASK	0x0400000000000000UL
#define UV4H_EVENT_OCCURRED0_IPI_INT_SHFT		59
#define UV4H_EVENT_OCCURRED0_IPI_INT_MASK		0x0800000000000000UL
#define UV4H_EVENT_OCCURRED0_EXTIO_INT0_SHFT		60
#define UV4H_EVENT_OCCURRED0_EXTIO_INT0_MASK		0x1000000000000000UL
#define UV4H_EVENT_OCCURRED0_EXTIO_INT1_SHFT		61
#define UV4H_EVENT_OCCURRED0_EXTIO_INT1_MASK		0x2000000000000000UL
#define UV4H_EVENT_OCCURRED0_EXTIO_INT2_SHFT		62
#define UV4H_EVENT_OCCURRED0_EXTIO_INT2_MASK		0x4000000000000000UL
#define UV4H_EVENT_OCCURRED0_EXTIO_INT3_SHFT		63
#define UV4H_EVENT_OCCURRED0_EXTIO_INT3_MASK		0x8000000000000000UL

/* UV3 unique defines */
#define UV3H_EVENT_OCCURRED0_QP_HCERR_SHFT		1
#define UV3H_EVENT_OCCURRED0_QP_HCERR_MASK		0x0000000000000002UL
#define UV3H_EVENT_OCCURRED0_QP_AOERR0_SHFT		10
#define UV3H_EVENT_OCCURRED0_QP_AOERR0_MASK		0x0000000000000400UL
#define UV3H_EVENT_OCCURRED0_RT_AOERR0_SHFT		17
#define UV3H_EVENT_OCCURRED0_RT_AOERR0_MASK		0x0000000000020000UL
#define UV3H_EVENT_OCCURRED0_NI0_AOERR0_SHFT		18
#define UV3H_EVENT_OCCURRED0_NI0_AOERR0_MASK		0x0000000000040000UL
#define UV3H_EVENT_OCCURRED0_NI1_AOERR0_SHFT		19
#define UV3H_EVENT_OCCURRED0_NI1_AOERR0_MASK		0x0000000000080000UL
#define UV3H_EVENT_OCCURRED0_LB_AOERR1_SHFT		20
#define UV3H_EVENT_OCCURRED0_LB_AOERR1_MASK		0x0000000000100000UL
#define UV3H_EVENT_OCCURRED0_QP_AOERR1_SHFT		21
#define UV3H_EVENT_OCCURRED0_QP_AOERR1_MASK		0x0000000000200000UL
#define UV3H_EVENT_OCCURRED0_RH_AOERR1_SHFT		22
#define UV3H_EVENT_OCCURRED0_RH_AOERR1_MASK		0x0000000000400000UL
#define UV3H_EVENT_OCCURRED0_LH0_AOERR1_SHFT		23
#define UV3H_EVENT_OCCURRED0_LH0_AOERR1_MASK		0x0000000000800000UL
#define UV3H_EVENT_OCCURRED0_LH1_AOERR1_SHFT		24
#define UV3H_EVENT_OCCURRED0_LH1_AOERR1_MASK		0x0000000001000000UL
#define UV3H_EVENT_OCCURRED0_GR0_AOERR1_SHFT		25
#define UV3H_EVENT_OCCURRED0_GR0_AOERR1_MASK		0x0000000002000000UL
#define UV3H_EVENT_OCCURRED0_GR1_AOERR1_SHFT		26
#define UV3H_EVENT_OCCURRED0_GR1_AOERR1_MASK		0x0000000004000000UL
#define UV3H_EVENT_OCCURRED0_XB_AOERR1_SHFT		27
#define UV3H_EVENT_OCCURRED0_XB_AOERR1_MASK		0x0000000008000000UL
#define UV3H_EVENT_OCCURRED0_RT_AOERR1_SHFT		28
#define UV3H_EVENT_OCCURRED0_RT_AOERR1_MASK		0x0000000010000000UL
#define UV3H_EVENT_OCCURRED0_NI0_AOERR1_SHFT		29
#define UV3H_EVENT_OCCURRED0_NI0_AOERR1_MASK		0x0000000020000000UL
#define UV3H_EVENT_OCCURRED0_NI1_AOERR1_SHFT		30
#define UV3H_EVENT_OCCURRED0_NI1_AOERR1_MASK		0x0000000040000000UL
#define UV3H_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_SHFT	31
#define UV3H_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_MASK	0x0000000080000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_0_SHFT		32
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_0_MASK		0x0000000100000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_1_SHFT		33
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_1_MASK		0x0000000200000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_2_SHFT		34
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_2_MASK		0x0000000400000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_3_SHFT		35
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_3_MASK		0x0000000800000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_4_SHFT		36
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_4_MASK		0x0000001000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_5_SHFT		37
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_5_MASK		0x0000002000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_6_SHFT		38
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_6_MASK		0x0000004000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_7_SHFT		39
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_7_MASK		0x0000008000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_8_SHFT		40
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_8_MASK		0x0000010000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_9_SHFT		41
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_9_MASK		0x0000020000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_10_SHFT		42
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_10_MASK		0x0000040000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_11_SHFT		43
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_11_MASK		0x0000080000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_12_SHFT		44
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_12_MASK		0x0000100000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_13_SHFT		45
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_13_MASK		0x0000200000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_14_SHFT		46
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_14_MASK		0x0000400000000000UL
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_15_SHFT		47
#define UV3H_EVENT_OCCURRED0_LB_IRQ_INT_15_MASK		0x0000800000000000UL
#define UV3H_EVENT_OCCURRED0_L1_NMI_INT_SHFT		48
#define UV3H_EVENT_OCCURRED0_L1_NMI_INT_MASK		0x0001000000000000UL
#define UV3H_EVENT_OCCURRED0_STOP_CLOCK_SHFT		49
#define UV3H_EVENT_OCCURRED0_STOP_CLOCK_MASK		0x0002000000000000UL
#define UV3H_EVENT_OCCURRED0_ASIC_TO_L1_SHFT		50
#define UV3H_EVENT_OCCURRED0_ASIC_TO_L1_MASK		0x0004000000000000UL
#define UV3H_EVENT_OCCURRED0_L1_TO_ASIC_SHFT		51
#define UV3H_EVENT_OCCURRED0_L1_TO_ASIC_MASK		0x0008000000000000UL
#define UV3H_EVENT_OCCURRED0_LA_SEQ_TRIGGER_SHFT	52
#define UV3H_EVENT_OCCURRED0_LA_SEQ_TRIGGER_MASK	0x0010000000000000UL
#define UV3H_EVENT_OCCURRED0_IPI_INT_SHFT		53
#define UV3H_EVENT_OCCURRED0_IPI_INT_MASK		0x0020000000000000UL
#define UV3H_EVENT_OCCURRED0_EXTIO_INT0_SHFT		54
#define UV3H_EVENT_OCCURRED0_EXTIO_INT0_MASK		0x0040000000000000UL
#define UV3H_EVENT_OCCURRED0_EXTIO_INT1_SHFT		55
#define UV3H_EVENT_OCCURRED0_EXTIO_INT1_MASK		0x0080000000000000UL
#define UV3H_EVENT_OCCURRED0_EXTIO_INT2_SHFT		56
#define UV3H_EVENT_OCCURRED0_EXTIO_INT2_MASK		0x0100000000000000UL
#define UV3H_EVENT_OCCURRED0_EXTIO_INT3_SHFT		57
#define UV3H_EVENT_OCCURRED0_EXTIO_INT3_MASK		0x0200000000000000UL
#define UV3H_EVENT_OCCURRED0_PROFILE_INT_SHFT		58
#define UV3H_EVENT_OCCURRED0_PROFILE_INT_MASK		0x0400000000000000UL

/* UV2 unique defines */
#define UV2H_EVENT_OCCURRED0_QP_HCERR_SHFT		1
#define UV2H_EVENT_OCCURRED0_QP_HCERR_MASK		0x0000000000000002UL
#define UV2H_EVENT_OCCURRED0_QP_AOERR0_SHFT		10
#define UV2H_EVENT_OCCURRED0_QP_AOERR0_MASK		0x0000000000000400UL
#define UV2H_EVENT_OCCURRED0_RT_AOERR0_SHFT		17
#define UV2H_EVENT_OCCURRED0_RT_AOERR0_MASK		0x0000000000020000UL
#define UV2H_EVENT_OCCURRED0_NI0_AOERR0_SHFT		18
#define UV2H_EVENT_OCCURRED0_NI0_AOERR0_MASK		0x0000000000040000UL
#define UV2H_EVENT_OCCURRED0_NI1_AOERR0_SHFT		19
#define UV2H_EVENT_OCCURRED0_NI1_AOERR0_MASK		0x0000000000080000UL
#define UV2H_EVENT_OCCURRED0_LB_AOERR1_SHFT		20
#define UV2H_EVENT_OCCURRED0_LB_AOERR1_MASK		0x0000000000100000UL
#define UV2H_EVENT_OCCURRED0_QP_AOERR1_SHFT		21
#define UV2H_EVENT_OCCURRED0_QP_AOERR1_MASK		0x0000000000200000UL
#define UV2H_EVENT_OCCURRED0_RH_AOERR1_SHFT		22
#define UV2H_EVENT_OCCURRED0_RH_AOERR1_MASK		0x0000000000400000UL
#define UV2H_EVENT_OCCURRED0_LH0_AOERR1_SHFT		23
#define UV2H_EVENT_OCCURRED0_LH0_AOERR1_MASK		0x0000000000800000UL
#define UV2H_EVENT_OCCURRED0_LH1_AOERR1_SHFT		24
#define UV2H_EVENT_OCCURRED0_LH1_AOERR1_MASK		0x0000000001000000UL
#define UV2H_EVENT_OCCURRED0_GR0_AOERR1_SHFT		25
#define UV2H_EVENT_OCCURRED0_GR0_AOERR1_MASK		0x0000000002000000UL
#define UV2H_EVENT_OCCURRED0_GR1_AOERR1_SHFT		26
#define UV2H_EVENT_OCCURRED0_GR1_AOERR1_MASK		0x0000000004000000UL
#define UV2H_EVENT_OCCURRED0_XB_AOERR1_SHFT		27
#define UV2H_EVENT_OCCURRED0_XB_AOERR1_MASK		0x0000000008000000UL
#define UV2H_EVENT_OCCURRED0_RT_AOERR1_SHFT		28
#define UV2H_EVENT_OCCURRED0_RT_AOERR1_MASK		0x0000000010000000UL
#define UV2H_EVENT_OCCURRED0_NI0_AOERR1_SHFT		29
#define UV2H_EVENT_OCCURRED0_NI0_AOERR1_MASK		0x0000000020000000UL
#define UV2H_EVENT_OCCURRED0_NI1_AOERR1_SHFT		30
#define UV2H_EVENT_OCCURRED0_NI1_AOERR1_MASK		0x0000000040000000UL
#define UV2H_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_SHFT	31
#define UV2H_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_MASK	0x0000000080000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_0_SHFT		32
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_0_MASK		0x0000000100000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_1_SHFT		33
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_1_MASK		0x0000000200000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_2_SHFT		34
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_2_MASK		0x0000000400000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_3_SHFT		35
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_3_MASK		0x0000000800000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_4_SHFT		36
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_4_MASK		0x0000001000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_5_SHFT		37
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_5_MASK		0x0000002000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_6_SHFT		38
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_6_MASK		0x0000004000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_7_SHFT		39
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_7_MASK		0x0000008000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_8_SHFT		40
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_8_MASK		0x0000010000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_9_SHFT		41
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_9_MASK		0x0000020000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_10_SHFT		42
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_10_MASK		0x0000040000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_11_SHFT		43
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_11_MASK		0x0000080000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_12_SHFT		44
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_12_MASK		0x0000100000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_13_SHFT		45
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_13_MASK		0x0000200000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_14_SHFT		46
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_14_MASK		0x0000400000000000UL
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_15_SHFT		47
#define UV2H_EVENT_OCCURRED0_LB_IRQ_INT_15_MASK		0x0000800000000000UL
#define UV2H_EVENT_OCCURRED0_L1_NMI_INT_SHFT		48
#define UV2H_EVENT_OCCURRED0_L1_NMI_INT_MASK		0x0001000000000000UL
#define UV2H_EVENT_OCCURRED0_STOP_CLOCK_SHFT		49
#define UV2H_EVENT_OCCURRED0_STOP_CLOCK_MASK		0x0002000000000000UL
#define UV2H_EVENT_OCCURRED0_ASIC_TO_L1_SHFT		50
#define UV2H_EVENT_OCCURRED0_ASIC_TO_L1_MASK		0x0004000000000000UL
#define UV2H_EVENT_OCCURRED0_L1_TO_ASIC_SHFT		51
#define UV2H_EVENT_OCCURRED0_L1_TO_ASIC_MASK		0x0008000000000000UL
#define UV2H_EVENT_OCCURRED0_LA_SEQ_TRIGGER_SHFT	52
#define UV2H_EVENT_OCCURRED0_LA_SEQ_TRIGGER_MASK	0x0010000000000000UL
#define UV2H_EVENT_OCCURRED0_IPI_INT_SHFT		53
#define UV2H_EVENT_OCCURRED0_IPI_INT_MASK		0x0020000000000000UL
#define UV2H_EVENT_OCCURRED0_EXTIO_INT0_SHFT		54
#define UV2H_EVENT_OCCURRED0_EXTIO_INT0_MASK		0x0040000000000000UL
#define UV2H_EVENT_OCCURRED0_EXTIO_INT1_SHFT		55
#define UV2H_EVENT_OCCURRED0_EXTIO_INT1_MASK		0x0080000000000000UL
#define UV2H_EVENT_OCCURRED0_EXTIO_INT2_SHFT		56
#define UV2H_EVENT_OCCURRED0_EXTIO_INT2_MASK		0x0100000000000000UL
#define UV2H_EVENT_OCCURRED0_EXTIO_INT3_SHFT		57
#define UV2H_EVENT_OCCURRED0_EXTIO_INT3_MASK		0x0200000000000000UL
#define UV2H_EVENT_OCCURRED0_PROFILE_INT_SHFT		58
#define UV2H_EVENT_OCCURRED0_PROFILE_INT_MASK		0x0400000000000000UL

#define UVH_EVENT_OCCURRED0_EXTIO_INT0_MASK (				\
	is_uv(UV4) ? 0x1000000000000000UL :				\
	is_uv(UV3) ? 0x0040000000000000UL :				\
	is_uv(UV2) ? 0x0040000000000000UL :				\
	0)
#define UVH_EVENT_OCCURRED0_EXTIO_INT0_SHFT (				\
	is_uv(UV4) ? 60 :						\
	is_uv(UV3) ? 54 :						\
	is_uv(UV2) ? 54 :						\
	-1)

union uvh_event_occurred0_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_event_occurred0_s {
		unsigned long	lb_hcerr:1;			/* RW */
		unsigned long	rsvd_1_63:63;
	} s;

	/* UVXH common struct */
	struct uvxh_event_occurred0_s {
		unsigned long	lb_hcerr:1;			/* RW */
		unsigned long	rsvd_1:1;
		unsigned long	rh_hcerr:1;			/* RW */
		unsigned long	lh0_hcerr:1;			/* RW */
		unsigned long	lh1_hcerr:1;			/* RW */
		unsigned long	gr0_hcerr:1;			/* RW */
		unsigned long	gr1_hcerr:1;			/* RW */
		unsigned long	ni0_hcerr:1;			/* RW */
		unsigned long	ni1_hcerr:1;			/* RW */
		unsigned long	lb_aoerr0:1;			/* RW */
		unsigned long	rsvd_10:1;
		unsigned long	rh_aoerr0:1;			/* RW */
		unsigned long	lh0_aoerr0:1;			/* RW */
		unsigned long	lh1_aoerr0:1;			/* RW */
		unsigned long	gr0_aoerr0:1;			/* RW */
		unsigned long	gr1_aoerr0:1;			/* RW */
		unsigned long	xb_aoerr0:1;			/* RW */
		unsigned long	rsvd_17_63:47;
	} sx;

	/* UVYH common struct */
	struct uvyh_event_occurred0_s {
		unsigned long	lb_hcerr:1;			/* RW */
		unsigned long	kt_hcerr:1;			/* RW */
		unsigned long	rh0_hcerr:1;			/* RW */
		unsigned long	rh1_hcerr:1;			/* RW */
		unsigned long	lh0_hcerr:1;			/* RW */
		unsigned long	lh1_hcerr:1;			/* RW */
		unsigned long	lh2_hcerr:1;			/* RW */
		unsigned long	lh3_hcerr:1;			/* RW */
		unsigned long	xb_hcerr:1;			/* RW */
		unsigned long	rdm_hcerr:1;			/* RW */
		unsigned long	ni0_hcerr:1;			/* RW */
		unsigned long	ni1_hcerr:1;			/* RW */
		unsigned long	lb_aoerr0:1;			/* RW */
		unsigned long	kt_aoerr0:1;			/* RW */
		unsigned long	rh0_aoerr0:1;			/* RW */
		unsigned long	rh1_aoerr0:1;			/* RW */
		unsigned long	lh0_aoerr0:1;			/* RW */
		unsigned long	lh1_aoerr0:1;			/* RW */
		unsigned long	lh2_aoerr0:1;			/* RW */
		unsigned long	lh3_aoerr0:1;			/* RW */
		unsigned long	xb_aoerr0:1;			/* RW */
		unsigned long	rdm_aoerr0:1;			/* RW */
		unsigned long	rt0_aoerr0:1;			/* RW */
		unsigned long	rt1_aoerr0:1;			/* RW */
		unsigned long	ni0_aoerr0:1;			/* RW */
		unsigned long	ni1_aoerr0:1;			/* RW */
		unsigned long	lb_aoerr1:1;			/* RW */
		unsigned long	kt_aoerr1:1;			/* RW */
		unsigned long	rh0_aoerr1:1;			/* RW */
		unsigned long	rh1_aoerr1:1;			/* RW */
		unsigned long	lh0_aoerr1:1;			/* RW */
		unsigned long	lh1_aoerr1:1;			/* RW */
		unsigned long	lh2_aoerr1:1;			/* RW */
		unsigned long	lh3_aoerr1:1;			/* RW */
		unsigned long	xb_aoerr1:1;			/* RW */
		unsigned long	rdm_aoerr1:1;			/* RW */
		unsigned long	rt0_aoerr1:1;			/* RW */
		unsigned long	rt1_aoerr1:1;			/* RW */
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
		unsigned long	rsvd_62_63:2;
	} sy;

	/* UV5 unique struct */
	struct uv5h_event_occurred0_s {
		unsigned long	lb_hcerr:1;			/* RW */
		unsigned long	kt_hcerr:1;			/* RW */
		unsigned long	rh0_hcerr:1;			/* RW */
		unsigned long	rh1_hcerr:1;			/* RW */
		unsigned long	lh0_hcerr:1;			/* RW */
		unsigned long	lh1_hcerr:1;			/* RW */
		unsigned long	lh2_hcerr:1;			/* RW */
		unsigned long	lh3_hcerr:1;			/* RW */
		unsigned long	xb_hcerr:1;			/* RW */
		unsigned long	rdm_hcerr:1;			/* RW */
		unsigned long	ni0_hcerr:1;			/* RW */
		unsigned long	ni1_hcerr:1;			/* RW */
		unsigned long	lb_aoerr0:1;			/* RW */
		unsigned long	kt_aoerr0:1;			/* RW */
		unsigned long	rh0_aoerr0:1;			/* RW */
		unsigned long	rh1_aoerr0:1;			/* RW */
		unsigned long	lh0_aoerr0:1;			/* RW */
		unsigned long	lh1_aoerr0:1;			/* RW */
		unsigned long	lh2_aoerr0:1;			/* RW */
		unsigned long	lh3_aoerr0:1;			/* RW */
		unsigned long	xb_aoerr0:1;			/* RW */
		unsigned long	rdm_aoerr0:1;			/* RW */
		unsigned long	rt0_aoerr0:1;			/* RW */
		unsigned long	rt1_aoerr0:1;			/* RW */
		unsigned long	ni0_aoerr0:1;			/* RW */
		unsigned long	ni1_aoerr0:1;			/* RW */
		unsigned long	lb_aoerr1:1;			/* RW */
		unsigned long	kt_aoerr1:1;			/* RW */
		unsigned long	rh0_aoerr1:1;			/* RW */
		unsigned long	rh1_aoerr1:1;			/* RW */
		unsigned long	lh0_aoerr1:1;			/* RW */
		unsigned long	lh1_aoerr1:1;			/* RW */
		unsigned long	lh2_aoerr1:1;			/* RW */
		unsigned long	lh3_aoerr1:1;			/* RW */
		unsigned long	xb_aoerr1:1;			/* RW */
		unsigned long	rdm_aoerr1:1;			/* RW */
		unsigned long	rt0_aoerr1:1;			/* RW */
		unsigned long	rt1_aoerr1:1;			/* RW */
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
		unsigned long	rsvd_62_63:2;
	} s5;

	/* UV4 unique struct */
	struct uv4h_event_occurred0_s {
		unsigned long	lb_hcerr:1;			/* RW */
		unsigned long	kt_hcerr:1;			/* RW */
		unsigned long	rh_hcerr:1;			/* RW */
		unsigned long	lh0_hcerr:1;			/* RW */
		unsigned long	lh1_hcerr:1;			/* RW */
		unsigned long	gr0_hcerr:1;			/* RW */
		unsigned long	gr1_hcerr:1;			/* RW */
		unsigned long	ni0_hcerr:1;			/* RW */
		unsigned long	ni1_hcerr:1;			/* RW */
		unsigned long	lb_aoerr0:1;			/* RW */
		unsigned long	kt_aoerr0:1;			/* RW */
		unsigned long	rh_aoerr0:1;			/* RW */
		unsigned long	lh0_aoerr0:1;			/* RW */
		unsigned long	lh1_aoerr0:1;			/* RW */
		unsigned long	gr0_aoerr0:1;			/* RW */
		unsigned long	gr1_aoerr0:1;			/* RW */
		unsigned long	xb_aoerr0:1;			/* RW */
		unsigned long	rtq0_aoerr0:1;			/* RW */
		unsigned long	rtq1_aoerr0:1;			/* RW */
		unsigned long	rtq2_aoerr0:1;			/* RW */
		unsigned long	rtq3_aoerr0:1;			/* RW */
		unsigned long	ni0_aoerr0:1;			/* RW */
		unsigned long	ni1_aoerr0:1;			/* RW */
		unsigned long	lb_aoerr1:1;			/* RW */
		unsigned long	kt_aoerr1:1;			/* RW */
		unsigned long	rh_aoerr1:1;			/* RW */
		unsigned long	lh0_aoerr1:1;			/* RW */
		unsigned long	lh1_aoerr1:1;			/* RW */
		unsigned long	gr0_aoerr1:1;			/* RW */
		unsigned long	gr1_aoerr1:1;			/* RW */
		unsigned long	xb_aoerr1:1;			/* RW */
		unsigned long	rtq0_aoerr1:1;			/* RW */
		unsigned long	rtq1_aoerr1:1;			/* RW */
		unsigned long	rtq2_aoerr1:1;			/* RW */
		unsigned long	rtq3_aoerr1:1;			/* RW */
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
	} s4;

	/* UV3 unique struct */
	struct uv3h_event_occurred0_s {
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
	} s3;

	/* UV2 unique struct */
	struct uv2h_event_occurred0_s {
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
	} s2;
};

/* ========================================================================= */
/*                        UVH_EVENT_OCCURRED0_ALIAS                          */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED0_ALIAS 0x70008UL


/* ========================================================================= */
/*                           UVH_EVENT_OCCURRED1                             */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED1 0x70080UL



/* UVYH common defines */
#define UVYH_EVENT_OCCURRED1_IPI_INT_SHFT		0
#define UVYH_EVENT_OCCURRED1_IPI_INT_MASK		0x0000000000000001UL
#define UVYH_EVENT_OCCURRED1_EXTIO_INT0_SHFT		1
#define UVYH_EVENT_OCCURRED1_EXTIO_INT0_MASK		0x0000000000000002UL
#define UVYH_EVENT_OCCURRED1_EXTIO_INT1_SHFT		2
#define UVYH_EVENT_OCCURRED1_EXTIO_INT1_MASK		0x0000000000000004UL
#define UVYH_EVENT_OCCURRED1_EXTIO_INT2_SHFT		3
#define UVYH_EVENT_OCCURRED1_EXTIO_INT2_MASK		0x0000000000000008UL
#define UVYH_EVENT_OCCURRED1_EXTIO_INT3_SHFT		4
#define UVYH_EVENT_OCCURRED1_EXTIO_INT3_MASK		0x0000000000000010UL
#define UVYH_EVENT_OCCURRED1_PROFILE_INT_SHFT		5
#define UVYH_EVENT_OCCURRED1_PROFILE_INT_MASK		0x0000000000000020UL
#define UVYH_EVENT_OCCURRED1_BAU_DATA_SHFT		6
#define UVYH_EVENT_OCCURRED1_BAU_DATA_MASK		0x0000000000000040UL
#define UVYH_EVENT_OCCURRED1_PROC_GENERAL_SHFT		7
#define UVYH_EVENT_OCCURRED1_PROC_GENERAL_MASK		0x0000000000000080UL
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT0_SHFT		8
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT0_MASK		0x0000000000000100UL
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT1_SHFT		9
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT1_MASK		0x0000000000000200UL
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT2_SHFT		10
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT2_MASK		0x0000000000000400UL
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT3_SHFT		11
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT3_MASK		0x0000000000000800UL
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT4_SHFT		12
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT4_MASK		0x0000000000001000UL
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT5_SHFT		13
#define UVYH_EVENT_OCCURRED1_XH_TLB_INT5_MASK		0x0000000000002000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT0_SHFT		14
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT0_MASK		0x0000000000004000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT1_SHFT		15
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT1_MASK		0x0000000000008000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT2_SHFT		16
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT2_MASK		0x0000000000010000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT3_SHFT		17
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT3_MASK		0x0000000000020000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT4_SHFT		18
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT4_MASK		0x0000000000040000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT5_SHFT		19
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT5_MASK		0x0000000000080000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT6_SHFT		20
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT6_MASK		0x0000000000100000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT7_SHFT		21
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT7_MASK		0x0000000000200000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT8_SHFT		22
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT8_MASK		0x0000000000400000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT9_SHFT		23
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT9_MASK		0x0000000000800000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT10_SHFT		24
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT10_MASK		0x0000000001000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT11_SHFT		25
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT11_MASK		0x0000000002000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT12_SHFT		26
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT12_MASK		0x0000000004000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT13_SHFT		27
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT13_MASK		0x0000000008000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT14_SHFT		28
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT14_MASK		0x0000000010000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT15_SHFT		29
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT15_MASK		0x0000000020000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT16_SHFT		30
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT16_MASK		0x0000000040000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT17_SHFT		31
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT17_MASK		0x0000000080000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT18_SHFT		32
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT18_MASK		0x0000000100000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT19_SHFT		33
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT19_MASK		0x0000000200000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT20_SHFT		34
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT20_MASK		0x0000000400000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT21_SHFT		35
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT21_MASK		0x0000000800000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT22_SHFT		36
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT22_MASK		0x0000001000000000UL
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT23_SHFT		37
#define UVYH_EVENT_OCCURRED1_RDM_TLB_INT23_MASK		0x0000002000000000UL

/* UV4 unique defines */
#define UV4H_EVENT_OCCURRED1_PROFILE_INT_SHFT		0
#define UV4H_EVENT_OCCURRED1_PROFILE_INT_MASK		0x0000000000000001UL
#define UV4H_EVENT_OCCURRED1_BAU_DATA_SHFT		1
#define UV4H_EVENT_OCCURRED1_BAU_DATA_MASK		0x0000000000000002UL
#define UV4H_EVENT_OCCURRED1_PROC_GENERAL_SHFT		2
#define UV4H_EVENT_OCCURRED1_PROC_GENERAL_MASK		0x0000000000000004UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT0_SHFT		3
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT0_MASK		0x0000000000000008UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT1_SHFT		4
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT1_MASK		0x0000000000000010UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT2_SHFT		5
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT2_MASK		0x0000000000000020UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT3_SHFT		6
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT3_MASK		0x0000000000000040UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT4_SHFT		7
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT4_MASK		0x0000000000000080UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT5_SHFT		8
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT5_MASK		0x0000000000000100UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT6_SHFT		9
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT6_MASK		0x0000000000000200UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT7_SHFT		10
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT7_MASK		0x0000000000000400UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT8_SHFT		11
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT8_MASK		0x0000000000000800UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT9_SHFT		12
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT9_MASK		0x0000000000001000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT10_SHFT		13
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT10_MASK		0x0000000000002000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT11_SHFT		14
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT11_MASK		0x0000000000004000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT12_SHFT		15
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT12_MASK		0x0000000000008000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT13_SHFT		16
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT13_MASK		0x0000000000010000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT14_SHFT		17
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT14_MASK		0x0000000000020000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT15_SHFT		18
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT15_MASK		0x0000000000040000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT16_SHFT		19
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT16_MASK		0x0000000000080000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT17_SHFT		20
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT17_MASK		0x0000000000100000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT18_SHFT		21
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT18_MASK		0x0000000000200000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT19_SHFT		22
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT19_MASK		0x0000000000400000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT20_SHFT		23
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT20_MASK		0x0000000000800000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT21_SHFT		24
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT21_MASK		0x0000000001000000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT22_SHFT		25
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT22_MASK		0x0000000002000000UL
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT23_SHFT		26
#define UV4H_EVENT_OCCURRED1_GR0_TLB_INT23_MASK		0x0000000004000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT0_SHFT		27
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT0_MASK		0x0000000008000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT1_SHFT		28
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT1_MASK		0x0000000010000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT2_SHFT		29
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT2_MASK		0x0000000020000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT3_SHFT		30
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT3_MASK		0x0000000040000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT4_SHFT		31
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT4_MASK		0x0000000080000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT5_SHFT		32
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT5_MASK		0x0000000100000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT6_SHFT		33
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT6_MASK		0x0000000200000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT7_SHFT		34
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT7_MASK		0x0000000400000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT8_SHFT		35
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT8_MASK		0x0000000800000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT9_SHFT		36
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT9_MASK		0x0000001000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT10_SHFT		37
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT10_MASK		0x0000002000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT11_SHFT		38
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT11_MASK		0x0000004000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT12_SHFT		39
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT12_MASK		0x0000008000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT13_SHFT		40
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT13_MASK		0x0000010000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT14_SHFT		41
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT14_MASK		0x0000020000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT15_SHFT		42
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT15_MASK		0x0000040000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT16_SHFT		43
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT16_MASK		0x0000080000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT17_SHFT		44
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT17_MASK		0x0000100000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT18_SHFT		45
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT18_MASK		0x0000200000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT19_SHFT		46
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT19_MASK		0x0000400000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT20_SHFT		47
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT20_MASK		0x0000800000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT21_SHFT		48
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT21_MASK		0x0001000000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT22_SHFT		49
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT22_MASK		0x0002000000000000UL
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT23_SHFT		50
#define UV4H_EVENT_OCCURRED1_GR1_TLB_INT23_MASK		0x0004000000000000UL

/* UV3 unique defines */
#define UV3H_EVENT_OCCURRED1_BAU_DATA_SHFT		0
#define UV3H_EVENT_OCCURRED1_BAU_DATA_MASK		0x0000000000000001UL
#define UV3H_EVENT_OCCURRED1_POWER_MANAGEMENT_REQ_SHFT	1
#define UV3H_EVENT_OCCURRED1_POWER_MANAGEMENT_REQ_MASK	0x0000000000000002UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT0_SHFT 2
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT0_MASK 0x0000000000000004UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT1_SHFT 3
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT1_MASK 0x0000000000000008UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT2_SHFT 4
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT2_MASK 0x0000000000000010UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT3_SHFT 5
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT3_MASK 0x0000000000000020UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT4_SHFT 6
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT4_MASK 0x0000000000000040UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT5_SHFT 7
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT5_MASK 0x0000000000000080UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT6_SHFT 8
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT6_MASK 0x0000000000000100UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT7_SHFT 9
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT7_MASK 0x0000000000000200UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT8_SHFT 10
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT8_MASK 0x0000000000000400UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT9_SHFT 11
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT9_MASK 0x0000000000000800UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT10_SHFT 12
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT10_MASK 0x0000000000001000UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT11_SHFT 13
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT11_MASK 0x0000000000002000UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT12_SHFT 14
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT12_MASK 0x0000000000004000UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT13_SHFT 15
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT13_MASK 0x0000000000008000UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT14_SHFT 16
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT14_MASK 0x0000000000010000UL
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT15_SHFT 17
#define UV3H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT15_MASK 0x0000000000020000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT0_SHFT		18
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT0_MASK		0x0000000000040000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT1_SHFT		19
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT1_MASK		0x0000000000080000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT2_SHFT		20
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT2_MASK		0x0000000000100000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT3_SHFT		21
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT3_MASK		0x0000000000200000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT4_SHFT		22
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT4_MASK		0x0000000000400000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT5_SHFT		23
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT5_MASK		0x0000000000800000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT6_SHFT		24
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT6_MASK		0x0000000001000000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT7_SHFT		25
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT7_MASK		0x0000000002000000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT8_SHFT		26
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT8_MASK		0x0000000004000000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT9_SHFT		27
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT9_MASK		0x0000000008000000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT10_SHFT		28
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT10_MASK		0x0000000010000000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT11_SHFT		29
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT11_MASK		0x0000000020000000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT12_SHFT		30
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT12_MASK		0x0000000040000000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT13_SHFT		31
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT13_MASK		0x0000000080000000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT14_SHFT		32
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT14_MASK		0x0000000100000000UL
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT15_SHFT		33
#define UV3H_EVENT_OCCURRED1_GR0_TLB_INT15_MASK		0x0000000200000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT0_SHFT		34
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT0_MASK		0x0000000400000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT1_SHFT		35
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT1_MASK		0x0000000800000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT2_SHFT		36
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT2_MASK		0x0000001000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT3_SHFT		37
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT3_MASK		0x0000002000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT4_SHFT		38
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT4_MASK		0x0000004000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT5_SHFT		39
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT5_MASK		0x0000008000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT6_SHFT		40
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT6_MASK		0x0000010000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT7_SHFT		41
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT7_MASK		0x0000020000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT8_SHFT		42
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT8_MASK		0x0000040000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT9_SHFT		43
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT9_MASK		0x0000080000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT10_SHFT		44
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT10_MASK		0x0000100000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT11_SHFT		45
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT11_MASK		0x0000200000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT12_SHFT		46
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT12_MASK		0x0000400000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT13_SHFT		47
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT13_MASK		0x0000800000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT14_SHFT		48
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT14_MASK		0x0001000000000000UL
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT15_SHFT		49
#define UV3H_EVENT_OCCURRED1_GR1_TLB_INT15_MASK		0x0002000000000000UL
#define UV3H_EVENT_OCCURRED1_RTC_INTERVAL_INT_SHFT	50
#define UV3H_EVENT_OCCURRED1_RTC_INTERVAL_INT_MASK	0x0004000000000000UL
#define UV3H_EVENT_OCCURRED1_BAU_DASHBOARD_INT_SHFT	51
#define UV3H_EVENT_OCCURRED1_BAU_DASHBOARD_INT_MASK	0x0008000000000000UL

/* UV2 unique defines */
#define UV2H_EVENT_OCCURRED1_BAU_DATA_SHFT		0
#define UV2H_EVENT_OCCURRED1_BAU_DATA_MASK		0x0000000000000001UL
#define UV2H_EVENT_OCCURRED1_POWER_MANAGEMENT_REQ_SHFT	1
#define UV2H_EVENT_OCCURRED1_POWER_MANAGEMENT_REQ_MASK	0x0000000000000002UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT0_SHFT 2
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT0_MASK 0x0000000000000004UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT1_SHFT 3
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT1_MASK 0x0000000000000008UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT2_SHFT 4
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT2_MASK 0x0000000000000010UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT3_SHFT 5
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT3_MASK 0x0000000000000020UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT4_SHFT 6
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT4_MASK 0x0000000000000040UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT5_SHFT 7
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT5_MASK 0x0000000000000080UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT6_SHFT 8
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT6_MASK 0x0000000000000100UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT7_SHFT 9
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT7_MASK 0x0000000000000200UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT8_SHFT 10
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT8_MASK 0x0000000000000400UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT9_SHFT 11
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT9_MASK 0x0000000000000800UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT10_SHFT 12
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT10_MASK 0x0000000000001000UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT11_SHFT 13
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT11_MASK 0x0000000000002000UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT12_SHFT 14
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT12_MASK 0x0000000000004000UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT13_SHFT 15
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT13_MASK 0x0000000000008000UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT14_SHFT 16
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT14_MASK 0x0000000000010000UL
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT15_SHFT 17
#define UV2H_EVENT_OCCURRED1_MESSAGE_ACCELERATOR_INT15_MASK 0x0000000000020000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT0_SHFT		18
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT0_MASK		0x0000000000040000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT1_SHFT		19
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT1_MASK		0x0000000000080000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT2_SHFT		20
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT2_MASK		0x0000000000100000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT3_SHFT		21
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT3_MASK		0x0000000000200000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT4_SHFT		22
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT4_MASK		0x0000000000400000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT5_SHFT		23
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT5_MASK		0x0000000000800000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT6_SHFT		24
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT6_MASK		0x0000000001000000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT7_SHFT		25
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT7_MASK		0x0000000002000000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT8_SHFT		26
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT8_MASK		0x0000000004000000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT9_SHFT		27
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT9_MASK		0x0000000008000000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT10_SHFT		28
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT10_MASK		0x0000000010000000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT11_SHFT		29
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT11_MASK		0x0000000020000000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT12_SHFT		30
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT12_MASK		0x0000000040000000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT13_SHFT		31
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT13_MASK		0x0000000080000000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT14_SHFT		32
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT14_MASK		0x0000000100000000UL
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT15_SHFT		33
#define UV2H_EVENT_OCCURRED1_GR0_TLB_INT15_MASK		0x0000000200000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT0_SHFT		34
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT0_MASK		0x0000000400000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT1_SHFT		35
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT1_MASK		0x0000000800000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT2_SHFT		36
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT2_MASK		0x0000001000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT3_SHFT		37
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT3_MASK		0x0000002000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT4_SHFT		38
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT4_MASK		0x0000004000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT5_SHFT		39
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT5_MASK		0x0000008000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT6_SHFT		40
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT6_MASK		0x0000010000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT7_SHFT		41
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT7_MASK		0x0000020000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT8_SHFT		42
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT8_MASK		0x0000040000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT9_SHFT		43
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT9_MASK		0x0000080000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT10_SHFT		44
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT10_MASK		0x0000100000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT11_SHFT		45
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT11_MASK		0x0000200000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT12_SHFT		46
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT12_MASK		0x0000400000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT13_SHFT		47
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT13_MASK		0x0000800000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT14_SHFT		48
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT14_MASK		0x0001000000000000UL
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT15_SHFT		49
#define UV2H_EVENT_OCCURRED1_GR1_TLB_INT15_MASK		0x0002000000000000UL
#define UV2H_EVENT_OCCURRED1_RTC_INTERVAL_INT_SHFT	50
#define UV2H_EVENT_OCCURRED1_RTC_INTERVAL_INT_MASK	0x0004000000000000UL
#define UV2H_EVENT_OCCURRED1_BAU_DASHBOARD_INT_SHFT	51
#define UV2H_EVENT_OCCURRED1_BAU_DASHBOARD_INT_MASK	0x0008000000000000UL

#define UVH_EVENT_OCCURRED1_EXTIO_INT0_MASK (				\
	is_uv(UV5) ? 0x0000000000000002UL :				\
	0)
#define UVH_EVENT_OCCURRED1_EXTIO_INT0_SHFT (				\
	is_uv(UV5) ? 1 :						\
	-1)

union uvyh_event_occurred1_u {
	unsigned long	v;

	/* UVYH common struct */
	struct uvyh_event_occurred1_s {
		unsigned long	ipi_int:1;			/* RW */
		unsigned long	extio_int0:1;			/* RW */
		unsigned long	extio_int1:1;			/* RW */
		unsigned long	extio_int2:1;			/* RW */
		unsigned long	extio_int3:1;			/* RW */
		unsigned long	profile_int:1;			/* RW */
		unsigned long	bau_data:1;			/* RW */
		unsigned long	proc_general:1;			/* RW */
		unsigned long	xh_tlb_int0:1;			/* RW */
		unsigned long	xh_tlb_int1:1;			/* RW */
		unsigned long	xh_tlb_int2:1;			/* RW */
		unsigned long	xh_tlb_int3:1;			/* RW */
		unsigned long	xh_tlb_int4:1;			/* RW */
		unsigned long	xh_tlb_int5:1;			/* RW */
		unsigned long	rdm_tlb_int0:1;			/* RW */
		unsigned long	rdm_tlb_int1:1;			/* RW */
		unsigned long	rdm_tlb_int2:1;			/* RW */
		unsigned long	rdm_tlb_int3:1;			/* RW */
		unsigned long	rdm_tlb_int4:1;			/* RW */
		unsigned long	rdm_tlb_int5:1;			/* RW */
		unsigned long	rdm_tlb_int6:1;			/* RW */
		unsigned long	rdm_tlb_int7:1;			/* RW */
		unsigned long	rdm_tlb_int8:1;			/* RW */
		unsigned long	rdm_tlb_int9:1;			/* RW */
		unsigned long	rdm_tlb_int10:1;		/* RW */
		unsigned long	rdm_tlb_int11:1;		/* RW */
		unsigned long	rdm_tlb_int12:1;		/* RW */
		unsigned long	rdm_tlb_int13:1;		/* RW */
		unsigned long	rdm_tlb_int14:1;		/* RW */
		unsigned long	rdm_tlb_int15:1;		/* RW */
		unsigned long	rdm_tlb_int16:1;		/* RW */
		unsigned long	rdm_tlb_int17:1;		/* RW */
		unsigned long	rdm_tlb_int18:1;		/* RW */
		unsigned long	rdm_tlb_int19:1;		/* RW */
		unsigned long	rdm_tlb_int20:1;		/* RW */
		unsigned long	rdm_tlb_int21:1;		/* RW */
		unsigned long	rdm_tlb_int22:1;		/* RW */
		unsigned long	rdm_tlb_int23:1;		/* RW */
		unsigned long	rsvd_38_63:26;
	} sy;

	/* UV5 unique struct */
	struct uv5h_event_occurred1_s {
		unsigned long	ipi_int:1;			/* RW */
		unsigned long	extio_int0:1;			/* RW */
		unsigned long	extio_int1:1;			/* RW */
		unsigned long	extio_int2:1;			/* RW */
		unsigned long	extio_int3:1;			/* RW */
		unsigned long	profile_int:1;			/* RW */
		unsigned long	bau_data:1;			/* RW */
		unsigned long	proc_general:1;			/* RW */
		unsigned long	xh_tlb_int0:1;			/* RW */
		unsigned long	xh_tlb_int1:1;			/* RW */
		unsigned long	xh_tlb_int2:1;			/* RW */
		unsigned long	xh_tlb_int3:1;			/* RW */
		unsigned long	xh_tlb_int4:1;			/* RW */
		unsigned long	xh_tlb_int5:1;			/* RW */
		unsigned long	rdm_tlb_int0:1;			/* RW */
		unsigned long	rdm_tlb_int1:1;			/* RW */
		unsigned long	rdm_tlb_int2:1;			/* RW */
		unsigned long	rdm_tlb_int3:1;			/* RW */
		unsigned long	rdm_tlb_int4:1;			/* RW */
		unsigned long	rdm_tlb_int5:1;			/* RW */
		unsigned long	rdm_tlb_int6:1;			/* RW */
		unsigned long	rdm_tlb_int7:1;			/* RW */
		unsigned long	rdm_tlb_int8:1;			/* RW */
		unsigned long	rdm_tlb_int9:1;			/* RW */
		unsigned long	rdm_tlb_int10:1;		/* RW */
		unsigned long	rdm_tlb_int11:1;		/* RW */
		unsigned long	rdm_tlb_int12:1;		/* RW */
		unsigned long	rdm_tlb_int13:1;		/* RW */
		unsigned long	rdm_tlb_int14:1;		/* RW */
		unsigned long	rdm_tlb_int15:1;		/* RW */
		unsigned long	rdm_tlb_int16:1;		/* RW */
		unsigned long	rdm_tlb_int17:1;		/* RW */
		unsigned long	rdm_tlb_int18:1;		/* RW */
		unsigned long	rdm_tlb_int19:1;		/* RW */
		unsigned long	rdm_tlb_int20:1;		/* RW */
		unsigned long	rdm_tlb_int21:1;		/* RW */
		unsigned long	rdm_tlb_int22:1;		/* RW */
		unsigned long	rdm_tlb_int23:1;		/* RW */
		unsigned long	rsvd_38_63:26;
	} s5;

	/* UV4 unique struct */
	struct uv4h_event_occurred1_s {
		unsigned long	profile_int:1;			/* RW */
		unsigned long	bau_data:1;			/* RW */
		unsigned long	proc_general:1;			/* RW */
		unsigned long	gr0_tlb_int0:1;			/* RW */
		unsigned long	gr0_tlb_int1:1;			/* RW */
		unsigned long	gr0_tlb_int2:1;			/* RW */
		unsigned long	gr0_tlb_int3:1;			/* RW */
		unsigned long	gr0_tlb_int4:1;			/* RW */
		unsigned long	gr0_tlb_int5:1;			/* RW */
		unsigned long	gr0_tlb_int6:1;			/* RW */
		unsigned long	gr0_tlb_int7:1;			/* RW */
		unsigned long	gr0_tlb_int8:1;			/* RW */
		unsigned long	gr0_tlb_int9:1;			/* RW */
		unsigned long	gr0_tlb_int10:1;		/* RW */
		unsigned long	gr0_tlb_int11:1;		/* RW */
		unsigned long	gr0_tlb_int12:1;		/* RW */
		unsigned long	gr0_tlb_int13:1;		/* RW */
		unsigned long	gr0_tlb_int14:1;		/* RW */
		unsigned long	gr0_tlb_int15:1;		/* RW */
		unsigned long	gr0_tlb_int16:1;		/* RW */
		unsigned long	gr0_tlb_int17:1;		/* RW */
		unsigned long	gr0_tlb_int18:1;		/* RW */
		unsigned long	gr0_tlb_int19:1;		/* RW */
		unsigned long	gr0_tlb_int20:1;		/* RW */
		unsigned long	gr0_tlb_int21:1;		/* RW */
		unsigned long	gr0_tlb_int22:1;		/* RW */
		unsigned long	gr0_tlb_int23:1;		/* RW */
		unsigned long	gr1_tlb_int0:1;			/* RW */
		unsigned long	gr1_tlb_int1:1;			/* RW */
		unsigned long	gr1_tlb_int2:1;			/* RW */
		unsigned long	gr1_tlb_int3:1;			/* RW */
		unsigned long	gr1_tlb_int4:1;			/* RW */
		unsigned long	gr1_tlb_int5:1;			/* RW */
		unsigned long	gr1_tlb_int6:1;			/* RW */
		unsigned long	gr1_tlb_int7:1;			/* RW */
		unsigned long	gr1_tlb_int8:1;			/* RW */
		unsigned long	gr1_tlb_int9:1;			/* RW */
		unsigned long	gr1_tlb_int10:1;		/* RW */
		unsigned long	gr1_tlb_int11:1;		/* RW */
		unsigned long	gr1_tlb_int12:1;		/* RW */
		unsigned long	gr1_tlb_int13:1;		/* RW */
		unsigned long	gr1_tlb_int14:1;		/* RW */
		unsigned long	gr1_tlb_int15:1;		/* RW */
		unsigned long	gr1_tlb_int16:1;		/* RW */
		unsigned long	gr1_tlb_int17:1;		/* RW */
		unsigned long	gr1_tlb_int18:1;		/* RW */
		unsigned long	gr1_tlb_int19:1;		/* RW */
		unsigned long	gr1_tlb_int20:1;		/* RW */
		unsigned long	gr1_tlb_int21:1;		/* RW */
		unsigned long	gr1_tlb_int22:1;		/* RW */
		unsigned long	gr1_tlb_int23:1;		/* RW */
		unsigned long	rsvd_51_63:13;
	} s4;

	/* UV3 unique struct */
	struct uv3h_event_occurred1_s {
		unsigned long	bau_data:1;			/* RW */
		unsigned long	power_management_req:1;		/* RW */
		unsigned long	message_accelerator_int0:1;	/* RW */
		unsigned long	message_accelerator_int1:1;	/* RW */
		unsigned long	message_accelerator_int2:1;	/* RW */
		unsigned long	message_accelerator_int3:1;	/* RW */
		unsigned long	message_accelerator_int4:1;	/* RW */
		unsigned long	message_accelerator_int5:1;	/* RW */
		unsigned long	message_accelerator_int6:1;	/* RW */
		unsigned long	message_accelerator_int7:1;	/* RW */
		unsigned long	message_accelerator_int8:1;	/* RW */
		unsigned long	message_accelerator_int9:1;	/* RW */
		unsigned long	message_accelerator_int10:1;	/* RW */
		unsigned long	message_accelerator_int11:1;	/* RW */
		unsigned long	message_accelerator_int12:1;	/* RW */
		unsigned long	message_accelerator_int13:1;	/* RW */
		unsigned long	message_accelerator_int14:1;	/* RW */
		unsigned long	message_accelerator_int15:1;	/* RW */
		unsigned long	gr0_tlb_int0:1;			/* RW */
		unsigned long	gr0_tlb_int1:1;			/* RW */
		unsigned long	gr0_tlb_int2:1;			/* RW */
		unsigned long	gr0_tlb_int3:1;			/* RW */
		unsigned long	gr0_tlb_int4:1;			/* RW */
		unsigned long	gr0_tlb_int5:1;			/* RW */
		unsigned long	gr0_tlb_int6:1;			/* RW */
		unsigned long	gr0_tlb_int7:1;			/* RW */
		unsigned long	gr0_tlb_int8:1;			/* RW */
		unsigned long	gr0_tlb_int9:1;			/* RW */
		unsigned long	gr0_tlb_int10:1;		/* RW */
		unsigned long	gr0_tlb_int11:1;		/* RW */
		unsigned long	gr0_tlb_int12:1;		/* RW */
		unsigned long	gr0_tlb_int13:1;		/* RW */
		unsigned long	gr0_tlb_int14:1;		/* RW */
		unsigned long	gr0_tlb_int15:1;		/* RW */
		unsigned long	gr1_tlb_int0:1;			/* RW */
		unsigned long	gr1_tlb_int1:1;			/* RW */
		unsigned long	gr1_tlb_int2:1;			/* RW */
		unsigned long	gr1_tlb_int3:1;			/* RW */
		unsigned long	gr1_tlb_int4:1;			/* RW */
		unsigned long	gr1_tlb_int5:1;			/* RW */
		unsigned long	gr1_tlb_int6:1;			/* RW */
		unsigned long	gr1_tlb_int7:1;			/* RW */
		unsigned long	gr1_tlb_int8:1;			/* RW */
		unsigned long	gr1_tlb_int9:1;			/* RW */
		unsigned long	gr1_tlb_int10:1;		/* RW */
		unsigned long	gr1_tlb_int11:1;		/* RW */
		unsigned long	gr1_tlb_int12:1;		/* RW */
		unsigned long	gr1_tlb_int13:1;		/* RW */
		unsigned long	gr1_tlb_int14:1;		/* RW */
		unsigned long	gr1_tlb_int15:1;		/* RW */
		unsigned long	rtc_interval_int:1;		/* RW */
		unsigned long	bau_dashboard_int:1;		/* RW */
		unsigned long	rsvd_52_63:12;
	} s3;

	/* UV2 unique struct */
	struct uv2h_event_occurred1_s {
		unsigned long	bau_data:1;			/* RW */
		unsigned long	power_management_req:1;		/* RW */
		unsigned long	message_accelerator_int0:1;	/* RW */
		unsigned long	message_accelerator_int1:1;	/* RW */
		unsigned long	message_accelerator_int2:1;	/* RW */
		unsigned long	message_accelerator_int3:1;	/* RW */
		unsigned long	message_accelerator_int4:1;	/* RW */
		unsigned long	message_accelerator_int5:1;	/* RW */
		unsigned long	message_accelerator_int6:1;	/* RW */
		unsigned long	message_accelerator_int7:1;	/* RW */
		unsigned long	message_accelerator_int8:1;	/* RW */
		unsigned long	message_accelerator_int9:1;	/* RW */
		unsigned long	message_accelerator_int10:1;	/* RW */
		unsigned long	message_accelerator_int11:1;	/* RW */
		unsigned long	message_accelerator_int12:1;	/* RW */
		unsigned long	message_accelerator_int13:1;	/* RW */
		unsigned long	message_accelerator_int14:1;	/* RW */
		unsigned long	message_accelerator_int15:1;	/* RW */
		unsigned long	gr0_tlb_int0:1;			/* RW */
		unsigned long	gr0_tlb_int1:1;			/* RW */
		unsigned long	gr0_tlb_int2:1;			/* RW */
		unsigned long	gr0_tlb_int3:1;			/* RW */
		unsigned long	gr0_tlb_int4:1;			/* RW */
		unsigned long	gr0_tlb_int5:1;			/* RW */
		unsigned long	gr0_tlb_int6:1;			/* RW */
		unsigned long	gr0_tlb_int7:1;			/* RW */
		unsigned long	gr0_tlb_int8:1;			/* RW */
		unsigned long	gr0_tlb_int9:1;			/* RW */
		unsigned long	gr0_tlb_int10:1;		/* RW */
		unsigned long	gr0_tlb_int11:1;		/* RW */
		unsigned long	gr0_tlb_int12:1;		/* RW */
		unsigned long	gr0_tlb_int13:1;		/* RW */
		unsigned long	gr0_tlb_int14:1;		/* RW */
		unsigned long	gr0_tlb_int15:1;		/* RW */
		unsigned long	gr1_tlb_int0:1;			/* RW */
		unsigned long	gr1_tlb_int1:1;			/* RW */
		unsigned long	gr1_tlb_int2:1;			/* RW */
		unsigned long	gr1_tlb_int3:1;			/* RW */
		unsigned long	gr1_tlb_int4:1;			/* RW */
		unsigned long	gr1_tlb_int5:1;			/* RW */
		unsigned long	gr1_tlb_int6:1;			/* RW */
		unsigned long	gr1_tlb_int7:1;			/* RW */
		unsigned long	gr1_tlb_int8:1;			/* RW */
		unsigned long	gr1_tlb_int9:1;			/* RW */
		unsigned long	gr1_tlb_int10:1;		/* RW */
		unsigned long	gr1_tlb_int11:1;		/* RW */
		unsigned long	gr1_tlb_int12:1;		/* RW */
		unsigned long	gr1_tlb_int13:1;		/* RW */
		unsigned long	gr1_tlb_int14:1;		/* RW */
		unsigned long	gr1_tlb_int15:1;		/* RW */
		unsigned long	rtc_interval_int:1;		/* RW */
		unsigned long	bau_dashboard_int:1;		/* RW */
		unsigned long	rsvd_52_63:12;
	} s2;
};

/* ========================================================================= */
/*                        UVH_EVENT_OCCURRED1_ALIAS                          */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED1_ALIAS 0x70088UL


/* ========================================================================= */
/*                           UVH_EVENT_OCCURRED2                             */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED2 0x70100UL



/* UVYH common defines */
#define UVYH_EVENT_OCCURRED2_RTC_INTERVAL_INT_SHFT	0
#define UVYH_EVENT_OCCURRED2_RTC_INTERVAL_INT_MASK	0x0000000000000001UL
#define UVYH_EVENT_OCCURRED2_BAU_DASHBOARD_INT_SHFT	1
#define UVYH_EVENT_OCCURRED2_BAU_DASHBOARD_INT_MASK	0x0000000000000002UL
#define UVYH_EVENT_OCCURRED2_RTC_0_SHFT			2
#define UVYH_EVENT_OCCURRED2_RTC_0_MASK			0x0000000000000004UL
#define UVYH_EVENT_OCCURRED2_RTC_1_SHFT			3
#define UVYH_EVENT_OCCURRED2_RTC_1_MASK			0x0000000000000008UL
#define UVYH_EVENT_OCCURRED2_RTC_2_SHFT			4
#define UVYH_EVENT_OCCURRED2_RTC_2_MASK			0x0000000000000010UL
#define UVYH_EVENT_OCCURRED2_RTC_3_SHFT			5
#define UVYH_EVENT_OCCURRED2_RTC_3_MASK			0x0000000000000020UL
#define UVYH_EVENT_OCCURRED2_RTC_4_SHFT			6
#define UVYH_EVENT_OCCURRED2_RTC_4_MASK			0x0000000000000040UL
#define UVYH_EVENT_OCCURRED2_RTC_5_SHFT			7
#define UVYH_EVENT_OCCURRED2_RTC_5_MASK			0x0000000000000080UL
#define UVYH_EVENT_OCCURRED2_RTC_6_SHFT			8
#define UVYH_EVENT_OCCURRED2_RTC_6_MASK			0x0000000000000100UL
#define UVYH_EVENT_OCCURRED2_RTC_7_SHFT			9
#define UVYH_EVENT_OCCURRED2_RTC_7_MASK			0x0000000000000200UL
#define UVYH_EVENT_OCCURRED2_RTC_8_SHFT			10
#define UVYH_EVENT_OCCURRED2_RTC_8_MASK			0x0000000000000400UL
#define UVYH_EVENT_OCCURRED2_RTC_9_SHFT			11
#define UVYH_EVENT_OCCURRED2_RTC_9_MASK			0x0000000000000800UL
#define UVYH_EVENT_OCCURRED2_RTC_10_SHFT		12
#define UVYH_EVENT_OCCURRED2_RTC_10_MASK		0x0000000000001000UL
#define UVYH_EVENT_OCCURRED2_RTC_11_SHFT		13
#define UVYH_EVENT_OCCURRED2_RTC_11_MASK		0x0000000000002000UL
#define UVYH_EVENT_OCCURRED2_RTC_12_SHFT		14
#define UVYH_EVENT_OCCURRED2_RTC_12_MASK		0x0000000000004000UL
#define UVYH_EVENT_OCCURRED2_RTC_13_SHFT		15
#define UVYH_EVENT_OCCURRED2_RTC_13_MASK		0x0000000000008000UL
#define UVYH_EVENT_OCCURRED2_RTC_14_SHFT		16
#define UVYH_EVENT_OCCURRED2_RTC_14_MASK		0x0000000000010000UL
#define UVYH_EVENT_OCCURRED2_RTC_15_SHFT		17
#define UVYH_EVENT_OCCURRED2_RTC_15_MASK		0x0000000000020000UL
#define UVYH_EVENT_OCCURRED2_RTC_16_SHFT		18
#define UVYH_EVENT_OCCURRED2_RTC_16_MASK		0x0000000000040000UL
#define UVYH_EVENT_OCCURRED2_RTC_17_SHFT		19
#define UVYH_EVENT_OCCURRED2_RTC_17_MASK		0x0000000000080000UL
#define UVYH_EVENT_OCCURRED2_RTC_18_SHFT		20
#define UVYH_EVENT_OCCURRED2_RTC_18_MASK		0x0000000000100000UL
#define UVYH_EVENT_OCCURRED2_RTC_19_SHFT		21
#define UVYH_EVENT_OCCURRED2_RTC_19_MASK		0x0000000000200000UL
#define UVYH_EVENT_OCCURRED2_RTC_20_SHFT		22
#define UVYH_EVENT_OCCURRED2_RTC_20_MASK		0x0000000000400000UL
#define UVYH_EVENT_OCCURRED2_RTC_21_SHFT		23
#define UVYH_EVENT_OCCURRED2_RTC_21_MASK		0x0000000000800000UL
#define UVYH_EVENT_OCCURRED2_RTC_22_SHFT		24
#define UVYH_EVENT_OCCURRED2_RTC_22_MASK		0x0000000001000000UL
#define UVYH_EVENT_OCCURRED2_RTC_23_SHFT		25
#define UVYH_EVENT_OCCURRED2_RTC_23_MASK		0x0000000002000000UL
#define UVYH_EVENT_OCCURRED2_RTC_24_SHFT		26
#define UVYH_EVENT_OCCURRED2_RTC_24_MASK		0x0000000004000000UL
#define UVYH_EVENT_OCCURRED2_RTC_25_SHFT		27
#define UVYH_EVENT_OCCURRED2_RTC_25_MASK		0x0000000008000000UL
#define UVYH_EVENT_OCCURRED2_RTC_26_SHFT		28
#define UVYH_EVENT_OCCURRED2_RTC_26_MASK		0x0000000010000000UL
#define UVYH_EVENT_OCCURRED2_RTC_27_SHFT		29
#define UVYH_EVENT_OCCURRED2_RTC_27_MASK		0x0000000020000000UL
#define UVYH_EVENT_OCCURRED2_RTC_28_SHFT		30
#define UVYH_EVENT_OCCURRED2_RTC_28_MASK		0x0000000040000000UL
#define UVYH_EVENT_OCCURRED2_RTC_29_SHFT		31
#define UVYH_EVENT_OCCURRED2_RTC_29_MASK		0x0000000080000000UL
#define UVYH_EVENT_OCCURRED2_RTC_30_SHFT		32
#define UVYH_EVENT_OCCURRED2_RTC_30_MASK		0x0000000100000000UL
#define UVYH_EVENT_OCCURRED2_RTC_31_SHFT		33
#define UVYH_EVENT_OCCURRED2_RTC_31_MASK		0x0000000200000000UL

/* UV4 unique defines */
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT0_SHFT 0
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT0_MASK 0x0000000000000001UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT1_SHFT 1
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT1_MASK 0x0000000000000002UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT2_SHFT 2
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT2_MASK 0x0000000000000004UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT3_SHFT 3
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT3_MASK 0x0000000000000008UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT4_SHFT 4
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT4_MASK 0x0000000000000010UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT5_SHFT 5
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT5_MASK 0x0000000000000020UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT6_SHFT 6
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT6_MASK 0x0000000000000040UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT7_SHFT 7
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT7_MASK 0x0000000000000080UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT8_SHFT 8
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT8_MASK 0x0000000000000100UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT9_SHFT 9
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT9_MASK 0x0000000000000200UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT10_SHFT 10
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT10_MASK 0x0000000000000400UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT11_SHFT 11
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT11_MASK 0x0000000000000800UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT12_SHFT 12
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT12_MASK 0x0000000000001000UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT13_SHFT 13
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT13_MASK 0x0000000000002000UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT14_SHFT 14
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT14_MASK 0x0000000000004000UL
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT15_SHFT 15
#define UV4H_EVENT_OCCURRED2_MESSAGE_ACCELERATOR_INT15_MASK 0x0000000000008000UL
#define UV4H_EVENT_OCCURRED2_RTC_INTERVAL_INT_SHFT	16
#define UV4H_EVENT_OCCURRED2_RTC_INTERVAL_INT_MASK	0x0000000000010000UL
#define UV4H_EVENT_OCCURRED2_BAU_DASHBOARD_INT_SHFT	17
#define UV4H_EVENT_OCCURRED2_BAU_DASHBOARD_INT_MASK	0x0000000000020000UL
#define UV4H_EVENT_OCCURRED2_RTC_0_SHFT			18
#define UV4H_EVENT_OCCURRED2_RTC_0_MASK			0x0000000000040000UL
#define UV4H_EVENT_OCCURRED2_RTC_1_SHFT			19
#define UV4H_EVENT_OCCURRED2_RTC_1_MASK			0x0000000000080000UL
#define UV4H_EVENT_OCCURRED2_RTC_2_SHFT			20
#define UV4H_EVENT_OCCURRED2_RTC_2_MASK			0x0000000000100000UL
#define UV4H_EVENT_OCCURRED2_RTC_3_SHFT			21
#define UV4H_EVENT_OCCURRED2_RTC_3_MASK			0x0000000000200000UL
#define UV4H_EVENT_OCCURRED2_RTC_4_SHFT			22
#define UV4H_EVENT_OCCURRED2_RTC_4_MASK			0x0000000000400000UL
#define UV4H_EVENT_OCCURRED2_RTC_5_SHFT			23
#define UV4H_EVENT_OCCURRED2_RTC_5_MASK			0x0000000000800000UL
#define UV4H_EVENT_OCCURRED2_RTC_6_SHFT			24
#define UV4H_EVENT_OCCURRED2_RTC_6_MASK			0x0000000001000000UL
#define UV4H_EVENT_OCCURRED2_RTC_7_SHFT			25
#define UV4H_EVENT_OCCURRED2_RTC_7_MASK			0x0000000002000000UL
#define UV4H_EVENT_OCCURRED2_RTC_8_SHFT			26
#define UV4H_EVENT_OCCURRED2_RTC_8_MASK			0x0000000004000000UL
#define UV4H_EVENT_OCCURRED2_RTC_9_SHFT			27
#define UV4H_EVENT_OCCURRED2_RTC_9_MASK			0x0000000008000000UL
#define UV4H_EVENT_OCCURRED2_RTC_10_SHFT		28
#define UV4H_EVENT_OCCURRED2_RTC_10_MASK		0x0000000010000000UL
#define UV4H_EVENT_OCCURRED2_RTC_11_SHFT		29
#define UV4H_EVENT_OCCURRED2_RTC_11_MASK		0x0000000020000000UL
#define UV4H_EVENT_OCCURRED2_RTC_12_SHFT		30
#define UV4H_EVENT_OCCURRED2_RTC_12_MASK		0x0000000040000000UL
#define UV4H_EVENT_OCCURRED2_RTC_13_SHFT		31
#define UV4H_EVENT_OCCURRED2_RTC_13_MASK		0x0000000080000000UL
#define UV4H_EVENT_OCCURRED2_RTC_14_SHFT		32
#define UV4H_EVENT_OCCURRED2_RTC_14_MASK		0x0000000100000000UL
#define UV4H_EVENT_OCCURRED2_RTC_15_SHFT		33
#define UV4H_EVENT_OCCURRED2_RTC_15_MASK		0x0000000200000000UL
#define UV4H_EVENT_OCCURRED2_RTC_16_SHFT		34
#define UV4H_EVENT_OCCURRED2_RTC_16_MASK		0x0000000400000000UL
#define UV4H_EVENT_OCCURRED2_RTC_17_SHFT		35
#define UV4H_EVENT_OCCURRED2_RTC_17_MASK		0x0000000800000000UL
#define UV4H_EVENT_OCCURRED2_RTC_18_SHFT		36
#define UV4H_EVENT_OCCURRED2_RTC_18_MASK		0x0000001000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_19_SHFT		37
#define UV4H_EVENT_OCCURRED2_RTC_19_MASK		0x0000002000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_20_SHFT		38
#define UV4H_EVENT_OCCURRED2_RTC_20_MASK		0x0000004000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_21_SHFT		39
#define UV4H_EVENT_OCCURRED2_RTC_21_MASK		0x0000008000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_22_SHFT		40
#define UV4H_EVENT_OCCURRED2_RTC_22_MASK		0x0000010000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_23_SHFT		41
#define UV4H_EVENT_OCCURRED2_RTC_23_MASK		0x0000020000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_24_SHFT		42
#define UV4H_EVENT_OCCURRED2_RTC_24_MASK		0x0000040000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_25_SHFT		43
#define UV4H_EVENT_OCCURRED2_RTC_25_MASK		0x0000080000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_26_SHFT		44
#define UV4H_EVENT_OCCURRED2_RTC_26_MASK		0x0000100000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_27_SHFT		45
#define UV4H_EVENT_OCCURRED2_RTC_27_MASK		0x0000200000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_28_SHFT		46
#define UV4H_EVENT_OCCURRED2_RTC_28_MASK		0x0000400000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_29_SHFT		47
#define UV4H_EVENT_OCCURRED2_RTC_29_MASK		0x0000800000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_30_SHFT		48
#define UV4H_EVENT_OCCURRED2_RTC_30_MASK		0x0001000000000000UL
#define UV4H_EVENT_OCCURRED2_RTC_31_SHFT		49
#define UV4H_EVENT_OCCURRED2_RTC_31_MASK		0x0002000000000000UL

/* UV3 unique defines */
#define UV3H_EVENT_OCCURRED2_RTC_0_SHFT			0
#define UV3H_EVENT_OCCURRED2_RTC_0_MASK			0x0000000000000001UL
#define UV3H_EVENT_OCCURRED2_RTC_1_SHFT			1
#define UV3H_EVENT_OCCURRED2_RTC_1_MASK			0x0000000000000002UL
#define UV3H_EVENT_OCCURRED2_RTC_2_SHFT			2
#define UV3H_EVENT_OCCURRED2_RTC_2_MASK			0x0000000000000004UL
#define UV3H_EVENT_OCCURRED2_RTC_3_SHFT			3
#define UV3H_EVENT_OCCURRED2_RTC_3_MASK			0x0000000000000008UL
#define UV3H_EVENT_OCCURRED2_RTC_4_SHFT			4
#define UV3H_EVENT_OCCURRED2_RTC_4_MASK			0x0000000000000010UL
#define UV3H_EVENT_OCCURRED2_RTC_5_SHFT			5
#define UV3H_EVENT_OCCURRED2_RTC_5_MASK			0x0000000000000020UL
#define UV3H_EVENT_OCCURRED2_RTC_6_SHFT			6
#define UV3H_EVENT_OCCURRED2_RTC_6_MASK			0x0000000000000040UL
#define UV3H_EVENT_OCCURRED2_RTC_7_SHFT			7
#define UV3H_EVENT_OCCURRED2_RTC_7_MASK			0x0000000000000080UL
#define UV3H_EVENT_OCCURRED2_RTC_8_SHFT			8
#define UV3H_EVENT_OCCURRED2_RTC_8_MASK			0x0000000000000100UL
#define UV3H_EVENT_OCCURRED2_RTC_9_SHFT			9
#define UV3H_EVENT_OCCURRED2_RTC_9_MASK			0x0000000000000200UL
#define UV3H_EVENT_OCCURRED2_RTC_10_SHFT		10
#define UV3H_EVENT_OCCURRED2_RTC_10_MASK		0x0000000000000400UL
#define UV3H_EVENT_OCCURRED2_RTC_11_SHFT		11
#define UV3H_EVENT_OCCURRED2_RTC_11_MASK		0x0000000000000800UL
#define UV3H_EVENT_OCCURRED2_RTC_12_SHFT		12
#define UV3H_EVENT_OCCURRED2_RTC_12_MASK		0x0000000000001000UL
#define UV3H_EVENT_OCCURRED2_RTC_13_SHFT		13
#define UV3H_EVENT_OCCURRED2_RTC_13_MASK		0x0000000000002000UL
#define UV3H_EVENT_OCCURRED2_RTC_14_SHFT		14
#define UV3H_EVENT_OCCURRED2_RTC_14_MASK		0x0000000000004000UL
#define UV3H_EVENT_OCCURRED2_RTC_15_SHFT		15
#define UV3H_EVENT_OCCURRED2_RTC_15_MASK		0x0000000000008000UL
#define UV3H_EVENT_OCCURRED2_RTC_16_SHFT		16
#define UV3H_EVENT_OCCURRED2_RTC_16_MASK		0x0000000000010000UL
#define UV3H_EVENT_OCCURRED2_RTC_17_SHFT		17
#define UV3H_EVENT_OCCURRED2_RTC_17_MASK		0x0000000000020000UL
#define UV3H_EVENT_OCCURRED2_RTC_18_SHFT		18
#define UV3H_EVENT_OCCURRED2_RTC_18_MASK		0x0000000000040000UL
#define UV3H_EVENT_OCCURRED2_RTC_19_SHFT		19
#define UV3H_EVENT_OCCURRED2_RTC_19_MASK		0x0000000000080000UL
#define UV3H_EVENT_OCCURRED2_RTC_20_SHFT		20
#define UV3H_EVENT_OCCURRED2_RTC_20_MASK		0x0000000000100000UL
#define UV3H_EVENT_OCCURRED2_RTC_21_SHFT		21
#define UV3H_EVENT_OCCURRED2_RTC_21_MASK		0x0000000000200000UL
#define UV3H_EVENT_OCCURRED2_RTC_22_SHFT		22
#define UV3H_EVENT_OCCURRED2_RTC_22_MASK		0x0000000000400000UL
#define UV3H_EVENT_OCCURRED2_RTC_23_SHFT		23
#define UV3H_EVENT_OCCURRED2_RTC_23_MASK		0x0000000000800000UL
#define UV3H_EVENT_OCCURRED2_RTC_24_SHFT		24
#define UV3H_EVENT_OCCURRED2_RTC_24_MASK		0x0000000001000000UL
#define UV3H_EVENT_OCCURRED2_RTC_25_SHFT		25
#define UV3H_EVENT_OCCURRED2_RTC_25_MASK		0x0000000002000000UL
#define UV3H_EVENT_OCCURRED2_RTC_26_SHFT		26
#define UV3H_EVENT_OCCURRED2_RTC_26_MASK		0x0000000004000000UL
#define UV3H_EVENT_OCCURRED2_RTC_27_SHFT		27
#define UV3H_EVENT_OCCURRED2_RTC_27_MASK		0x0000000008000000UL
#define UV3H_EVENT_OCCURRED2_RTC_28_SHFT		28
#define UV3H_EVENT_OCCURRED2_RTC_28_MASK		0x0000000010000000UL
#define UV3H_EVENT_OCCURRED2_RTC_29_SHFT		29
#define UV3H_EVENT_OCCURRED2_RTC_29_MASK		0x0000000020000000UL
#define UV3H_EVENT_OCCURRED2_RTC_30_SHFT		30
#define UV3H_EVENT_OCCURRED2_RTC_30_MASK		0x0000000040000000UL
#define UV3H_EVENT_OCCURRED2_RTC_31_SHFT		31
#define UV3H_EVENT_OCCURRED2_RTC_31_MASK		0x0000000080000000UL

/* UV2 unique defines */
#define UV2H_EVENT_OCCURRED2_RTC_0_SHFT			0
#define UV2H_EVENT_OCCURRED2_RTC_0_MASK			0x0000000000000001UL
#define UV2H_EVENT_OCCURRED2_RTC_1_SHFT			1
#define UV2H_EVENT_OCCURRED2_RTC_1_MASK			0x0000000000000002UL
#define UV2H_EVENT_OCCURRED2_RTC_2_SHFT			2
#define UV2H_EVENT_OCCURRED2_RTC_2_MASK			0x0000000000000004UL
#define UV2H_EVENT_OCCURRED2_RTC_3_SHFT			3
#define UV2H_EVENT_OCCURRED2_RTC_3_MASK			0x0000000000000008UL
#define UV2H_EVENT_OCCURRED2_RTC_4_SHFT			4
#define UV2H_EVENT_OCCURRED2_RTC_4_MASK			0x0000000000000010UL
#define UV2H_EVENT_OCCURRED2_RTC_5_SHFT			5
#define UV2H_EVENT_OCCURRED2_RTC_5_MASK			0x0000000000000020UL
#define UV2H_EVENT_OCCURRED2_RTC_6_SHFT			6
#define UV2H_EVENT_OCCURRED2_RTC_6_MASK			0x0000000000000040UL
#define UV2H_EVENT_OCCURRED2_RTC_7_SHFT			7
#define UV2H_EVENT_OCCURRED2_RTC_7_MASK			0x0000000000000080UL
#define UV2H_EVENT_OCCURRED2_RTC_8_SHFT			8
#define UV2H_EVENT_OCCURRED2_RTC_8_MASK			0x0000000000000100UL
#define UV2H_EVENT_OCCURRED2_RTC_9_SHFT			9
#define UV2H_EVENT_OCCURRED2_RTC_9_MASK			0x0000000000000200UL
#define UV2H_EVENT_OCCURRED2_RTC_10_SHFT		10
#define UV2H_EVENT_OCCURRED2_RTC_10_MASK		0x0000000000000400UL
#define UV2H_EVENT_OCCURRED2_RTC_11_SHFT		11
#define UV2H_EVENT_OCCURRED2_RTC_11_MASK		0x0000000000000800UL
#define UV2H_EVENT_OCCURRED2_RTC_12_SHFT		12
#define UV2H_EVENT_OCCURRED2_RTC_12_MASK		0x0000000000001000UL
#define UV2H_EVENT_OCCURRED2_RTC_13_SHFT		13
#define UV2H_EVENT_OCCURRED2_RTC_13_MASK		0x0000000000002000UL
#define UV2H_EVENT_OCCURRED2_RTC_14_SHFT		14
#define UV2H_EVENT_OCCURRED2_RTC_14_MASK		0x0000000000004000UL
#define UV2H_EVENT_OCCURRED2_RTC_15_SHFT		15
#define UV2H_EVENT_OCCURRED2_RTC_15_MASK		0x0000000000008000UL
#define UV2H_EVENT_OCCURRED2_RTC_16_SHFT		16
#define UV2H_EVENT_OCCURRED2_RTC_16_MASK		0x0000000000010000UL
#define UV2H_EVENT_OCCURRED2_RTC_17_SHFT		17
#define UV2H_EVENT_OCCURRED2_RTC_17_MASK		0x0000000000020000UL
#define UV2H_EVENT_OCCURRED2_RTC_18_SHFT		18
#define UV2H_EVENT_OCCURRED2_RTC_18_MASK		0x0000000000040000UL
#define UV2H_EVENT_OCCURRED2_RTC_19_SHFT		19
#define UV2H_EVENT_OCCURRED2_RTC_19_MASK		0x0000000000080000UL
#define UV2H_EVENT_OCCURRED2_RTC_20_SHFT		20
#define UV2H_EVENT_OCCURRED2_RTC_20_MASK		0x0000000000100000UL
#define UV2H_EVENT_OCCURRED2_RTC_21_SHFT		21
#define UV2H_EVENT_OCCURRED2_RTC_21_MASK		0x0000000000200000UL
#define UV2H_EVENT_OCCURRED2_RTC_22_SHFT		22
#define UV2H_EVENT_OCCURRED2_RTC_22_MASK		0x0000000000400000UL
#define UV2H_EVENT_OCCURRED2_RTC_23_SHFT		23
#define UV2H_EVENT_OCCURRED2_RTC_23_MASK		0x0000000000800000UL
#define UV2H_EVENT_OCCURRED2_RTC_24_SHFT		24
#define UV2H_EVENT_OCCURRED2_RTC_24_MASK		0x0000000001000000UL
#define UV2H_EVENT_OCCURRED2_RTC_25_SHFT		25
#define UV2H_EVENT_OCCURRED2_RTC_25_MASK		0x0000000002000000UL
#define UV2H_EVENT_OCCURRED2_RTC_26_SHFT		26
#define UV2H_EVENT_OCCURRED2_RTC_26_MASK		0x0000000004000000UL
#define UV2H_EVENT_OCCURRED2_RTC_27_SHFT		27
#define UV2H_EVENT_OCCURRED2_RTC_27_MASK		0x0000000008000000UL
#define UV2H_EVENT_OCCURRED2_RTC_28_SHFT		28
#define UV2H_EVENT_OCCURRED2_RTC_28_MASK		0x0000000010000000UL
#define UV2H_EVENT_OCCURRED2_RTC_29_SHFT		29
#define UV2H_EVENT_OCCURRED2_RTC_29_MASK		0x0000000020000000UL
#define UV2H_EVENT_OCCURRED2_RTC_30_SHFT		30
#define UV2H_EVENT_OCCURRED2_RTC_30_MASK		0x0000000040000000UL
#define UV2H_EVENT_OCCURRED2_RTC_31_SHFT		31
#define UV2H_EVENT_OCCURRED2_RTC_31_MASK		0x0000000080000000UL

#define UVH_EVENT_OCCURRED2_RTC_1_MASK (				\
	is_uv(UV5) ? 0x0000000000000008UL :				\
	is_uv(UV4) ? 0x0000000000080000UL :				\
	is_uv(UV3) ? 0x0000000000000002UL :				\
	is_uv(UV2) ? 0x0000000000000002UL :				\
	0)
#define UVH_EVENT_OCCURRED2_RTC_1_SHFT (				\
	is_uv(UV5) ? 3 :						\
	is_uv(UV4) ? 19 :						\
	is_uv(UV3) ? 1 :						\
	is_uv(UV2) ? 1 :						\
	-1)

union uvyh_event_occurred2_u {
	unsigned long	v;

	/* UVYH common struct */
	struct uvyh_event_occurred2_s {
		unsigned long	rtc_interval_int:1;		/* RW */
		unsigned long	bau_dashboard_int:1;		/* RW */
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
		unsigned long	rsvd_34_63:30;
	} sy;

	/* UV5 unique struct */
	struct uv5h_event_occurred2_s {
		unsigned long	rtc_interval_int:1;		/* RW */
		unsigned long	bau_dashboard_int:1;		/* RW */
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
		unsigned long	rsvd_34_63:30;
	} s5;

	/* UV4 unique struct */
	struct uv4h_event_occurred2_s {
		unsigned long	message_accelerator_int0:1;	/* RW */
		unsigned long	message_accelerator_int1:1;	/* RW */
		unsigned long	message_accelerator_int2:1;	/* RW */
		unsigned long	message_accelerator_int3:1;	/* RW */
		unsigned long	message_accelerator_int4:1;	/* RW */
		unsigned long	message_accelerator_int5:1;	/* RW */
		unsigned long	message_accelerator_int6:1;	/* RW */
		unsigned long	message_accelerator_int7:1;	/* RW */
		unsigned long	message_accelerator_int8:1;	/* RW */
		unsigned long	message_accelerator_int9:1;	/* RW */
		unsigned long	message_accelerator_int10:1;	/* RW */
		unsigned long	message_accelerator_int11:1;	/* RW */
		unsigned long	message_accelerator_int12:1;	/* RW */
		unsigned long	message_accelerator_int13:1;	/* RW */
		unsigned long	message_accelerator_int14:1;	/* RW */
		unsigned long	message_accelerator_int15:1;	/* RW */
		unsigned long	rtc_interval_int:1;		/* RW */
		unsigned long	bau_dashboard_int:1;		/* RW */
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
		unsigned long	rsvd_50_63:14;
	} s4;

	/* UV3 unique struct */
	struct uv3h_event_occurred2_s {
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
	} s3;

	/* UV2 unique struct */
	struct uv2h_event_occurred2_s {
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
	} s2;
};

/* ========================================================================= */
/*                        UVH_EVENT_OCCURRED2_ALIAS                          */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED2_ALIAS 0x70108UL


/* ========================================================================= */
/*                         UVH_EXTIO_INT0_BROADCAST                          */
/* ========================================================================= */
#define UVH_EXTIO_INT0_BROADCAST 0x61448UL

/* UVH common defines*/
#define UVH_EXTIO_INT0_BROADCAST_ENABLE_SHFT		0
#define UVH_EXTIO_INT0_BROADCAST_ENABLE_MASK		0x0000000000000001UL


union uvh_extio_int0_broadcast_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_extio_int0_broadcast_s {
		unsigned long	enable:1;			/* RW */
		unsigned long	rsvd_1_63:63;
	} s;

	/* UV5 unique struct */
	struct uv5h_extio_int0_broadcast_s {
		unsigned long	enable:1;			/* RW */
		unsigned long	rsvd_1_63:63;
	} s5;

	/* UV4 unique struct */
	struct uv4h_extio_int0_broadcast_s {
		unsigned long	enable:1;			/* RW */
		unsigned long	rsvd_1_63:63;
	} s4;

	/* UV3 unique struct */
	struct uv3h_extio_int0_broadcast_s {
		unsigned long	enable:1;			/* RW */
		unsigned long	rsvd_1_63:63;
	} s3;

	/* UV2 unique struct */
	struct uv2h_extio_int0_broadcast_s {
		unsigned long	enable:1;			/* RW */
		unsigned long	rsvd_1_63:63;
	} s2;
};

/* ========================================================================= */
/*                          UVH_GR0_GAM_GR_CONFIG                            */
/* ========================================================================= */
#define UVH_GR0_GAM_GR_CONFIG (						\
	is_uv(UV5) ? 0x600028UL :					\
	is_uv(UV4) ? 0x600028UL :					\
	is_uv(UV3) ? 0xc00028UL :					\
	is_uv(UV2) ? 0xc00028UL :					\
	0)



/* UVYH common defines */
#define UVYH_GR0_GAM_GR_CONFIG_SUBSPACE_SHFT		10
#define UVYH_GR0_GAM_GR_CONFIG_SUBSPACE_MASK		0x0000000000000400UL

/* UV4 unique defines */
#define UV4H_GR0_GAM_GR_CONFIG_SUBSPACE_SHFT		10
#define UV4H_GR0_GAM_GR_CONFIG_SUBSPACE_MASK		0x0000000000000400UL

/* UV3 unique defines */
#define UV3H_GR0_GAM_GR_CONFIG_M_SKT_SHFT		0
#define UV3H_GR0_GAM_GR_CONFIG_M_SKT_MASK		0x000000000000003fUL
#define UV3H_GR0_GAM_GR_CONFIG_SUBSPACE_SHFT		10
#define UV3H_GR0_GAM_GR_CONFIG_SUBSPACE_MASK		0x0000000000000400UL

/* UV2 unique defines */
#define UV2H_GR0_GAM_GR_CONFIG_N_GR_SHFT		0
#define UV2H_GR0_GAM_GR_CONFIG_N_GR_MASK		0x000000000000000fUL


union uvyh_gr0_gam_gr_config_u {
	unsigned long	v;

	/* UVYH common struct */
	struct uvyh_gr0_gam_gr_config_s {
		unsigned long	rsvd_0_9:10;
		unsigned long	subspace:1;			/* RW */
		unsigned long	rsvd_11_63:53;
	} sy;

	/* UV5 unique struct */
	struct uv5h_gr0_gam_gr_config_s {
		unsigned long	rsvd_0_9:10;
		unsigned long	subspace:1;			/* RW */
		unsigned long	rsvd_11_63:53;
	} s5;

	/* UV4 unique struct */
	struct uv4h_gr0_gam_gr_config_s {
		unsigned long	rsvd_0_9:10;
		unsigned long	subspace:1;			/* RW */
		unsigned long	rsvd_11_63:53;
	} s4;

	/* UV3 unique struct */
	struct uv3h_gr0_gam_gr_config_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	undef_6_9:4;			/* Undefined */
		unsigned long	subspace:1;			/* RW */
		unsigned long	reserved:53;
	} s3;

	/* UV2 unique struct */
	struct uv2h_gr0_gam_gr_config_s {
		unsigned long	n_gr:4;				/* RW */
		unsigned long	reserved:60;
	} s2;
};

/* ========================================================================= */
/*                         UVH_GR0_TLB_INT0_CONFIG                           */
/* ========================================================================= */
#define UVH_GR0_TLB_INT0_CONFIG (					\
	is_uv(UV4) ? 0x61b00UL :					\
	is_uv(UV3) ? 0x61b00UL :					\
	is_uv(UV2) ? 0x61b00UL :					\
	uv_undefined("UVH_GR0_TLB_INT0_CONFIG"))


/* UVXH common defines */
#define UVXH_GR0_TLB_INT0_CONFIG_VECTOR_SHFT		0
#define UVXH_GR0_TLB_INT0_CONFIG_VECTOR_MASK		0x00000000000000ffUL
#define UVXH_GR0_TLB_INT0_CONFIG_DM_SHFT		8
#define UVXH_GR0_TLB_INT0_CONFIG_DM_MASK		0x0000000000000700UL
#define UVXH_GR0_TLB_INT0_CONFIG_DESTMODE_SHFT		11
#define UVXH_GR0_TLB_INT0_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVXH_GR0_TLB_INT0_CONFIG_STATUS_SHFT		12
#define UVXH_GR0_TLB_INT0_CONFIG_STATUS_MASK		0x0000000000001000UL
#define UVXH_GR0_TLB_INT0_CONFIG_P_SHFT			13
#define UVXH_GR0_TLB_INT0_CONFIG_P_MASK			0x0000000000002000UL
#define UVXH_GR0_TLB_INT0_CONFIG_T_SHFT			15
#define UVXH_GR0_TLB_INT0_CONFIG_T_MASK			0x0000000000008000UL
#define UVXH_GR0_TLB_INT0_CONFIG_M_SHFT			16
#define UVXH_GR0_TLB_INT0_CONFIG_M_MASK			0x0000000000010000UL
#define UVXH_GR0_TLB_INT0_CONFIG_APIC_ID_SHFT		32
#define UVXH_GR0_TLB_INT0_CONFIG_APIC_ID_MASK		0xffffffff00000000UL


union uvh_gr0_tlb_int0_config_u {
	unsigned long	v;

	/* UVH common struct */
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

	/* UVXH common struct */
	struct uvxh_gr0_tlb_int0_config_s {
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
	} sx;

	/* UV4 unique struct */
	struct uv4h_gr0_tlb_int0_config_s {
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
	} s4;

	/* UV3 unique struct */
	struct uv3h_gr0_tlb_int0_config_s {
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
	} s3;

	/* UV2 unique struct */
	struct uv2h_gr0_tlb_int0_config_s {
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
	} s2;
};

/* ========================================================================= */
/*                         UVH_GR0_TLB_INT1_CONFIG                           */
/* ========================================================================= */
#define UVH_GR0_TLB_INT1_CONFIG (					\
	is_uv(UV4) ? 0x61b40UL :					\
	is_uv(UV3) ? 0x61b40UL :					\
	is_uv(UV2) ? 0x61b40UL :					\
	uv_undefined("UVH_GR0_TLB_INT1_CONFIG"))


/* UVXH common defines */
#define UVXH_GR0_TLB_INT1_CONFIG_VECTOR_SHFT		0
#define UVXH_GR0_TLB_INT1_CONFIG_VECTOR_MASK		0x00000000000000ffUL
#define UVXH_GR0_TLB_INT1_CONFIG_DM_SHFT		8
#define UVXH_GR0_TLB_INT1_CONFIG_DM_MASK		0x0000000000000700UL
#define UVXH_GR0_TLB_INT1_CONFIG_DESTMODE_SHFT		11
#define UVXH_GR0_TLB_INT1_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVXH_GR0_TLB_INT1_CONFIG_STATUS_SHFT		12
#define UVXH_GR0_TLB_INT1_CONFIG_STATUS_MASK		0x0000000000001000UL
#define UVXH_GR0_TLB_INT1_CONFIG_P_SHFT			13
#define UVXH_GR0_TLB_INT1_CONFIG_P_MASK			0x0000000000002000UL
#define UVXH_GR0_TLB_INT1_CONFIG_T_SHFT			15
#define UVXH_GR0_TLB_INT1_CONFIG_T_MASK			0x0000000000008000UL
#define UVXH_GR0_TLB_INT1_CONFIG_M_SHFT			16
#define UVXH_GR0_TLB_INT1_CONFIG_M_MASK			0x0000000000010000UL
#define UVXH_GR0_TLB_INT1_CONFIG_APIC_ID_SHFT		32
#define UVXH_GR0_TLB_INT1_CONFIG_APIC_ID_MASK		0xffffffff00000000UL


union uvh_gr0_tlb_int1_config_u {
	unsigned long	v;

	/* UVH common struct */
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

	/* UVXH common struct */
	struct uvxh_gr0_tlb_int1_config_s {
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
	} sx;

	/* UV4 unique struct */
	struct uv4h_gr0_tlb_int1_config_s {
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
	} s4;

	/* UV3 unique struct */
	struct uv3h_gr0_tlb_int1_config_s {
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
	} s3;

	/* UV2 unique struct */
	struct uv2h_gr0_tlb_int1_config_s {
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
	} s2;
};

/* ========================================================================= */
/*                         UVH_GR1_TLB_INT0_CONFIG                           */
/* ========================================================================= */
#define UVH_GR1_TLB_INT0_CONFIG (					\
	is_uv(UV4) ? 0x62100UL :					\
	is_uv(UV3) ? 0x61f00UL :					\
	is_uv(UV2) ? 0x61f00UL :					\
	uv_undefined("UVH_GR1_TLB_INT0_CONFIG"))


/* UVXH common defines */
#define UVXH_GR1_TLB_INT0_CONFIG_VECTOR_SHFT		0
#define UVXH_GR1_TLB_INT0_CONFIG_VECTOR_MASK		0x00000000000000ffUL
#define UVXH_GR1_TLB_INT0_CONFIG_DM_SHFT		8
#define UVXH_GR1_TLB_INT0_CONFIG_DM_MASK		0x0000000000000700UL
#define UVXH_GR1_TLB_INT0_CONFIG_DESTMODE_SHFT		11
#define UVXH_GR1_TLB_INT0_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVXH_GR1_TLB_INT0_CONFIG_STATUS_SHFT		12
#define UVXH_GR1_TLB_INT0_CONFIG_STATUS_MASK		0x0000000000001000UL
#define UVXH_GR1_TLB_INT0_CONFIG_P_SHFT			13
#define UVXH_GR1_TLB_INT0_CONFIG_P_MASK			0x0000000000002000UL
#define UVXH_GR1_TLB_INT0_CONFIG_T_SHFT			15
#define UVXH_GR1_TLB_INT0_CONFIG_T_MASK			0x0000000000008000UL
#define UVXH_GR1_TLB_INT0_CONFIG_M_SHFT			16
#define UVXH_GR1_TLB_INT0_CONFIG_M_MASK			0x0000000000010000UL
#define UVXH_GR1_TLB_INT0_CONFIG_APIC_ID_SHFT		32
#define UVXH_GR1_TLB_INT0_CONFIG_APIC_ID_MASK		0xffffffff00000000UL


union uvh_gr1_tlb_int0_config_u {
	unsigned long	v;

	/* UVH common struct */
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

	/* UVXH common struct */
	struct uvxh_gr1_tlb_int0_config_s {
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
	} sx;

	/* UV4 unique struct */
	struct uv4h_gr1_tlb_int0_config_s {
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
	} s4;

	/* UV3 unique struct */
	struct uv3h_gr1_tlb_int0_config_s {
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
	} s3;

	/* UV2 unique struct */
	struct uv2h_gr1_tlb_int0_config_s {
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
	} s2;
};

/* ========================================================================= */
/*                         UVH_GR1_TLB_INT1_CONFIG                           */
/* ========================================================================= */
#define UVH_GR1_TLB_INT1_CONFIG (					\
	is_uv(UV4) ? 0x62140UL :					\
	is_uv(UV3) ? 0x61f40UL :					\
	is_uv(UV2) ? 0x61f40UL :					\
	uv_undefined("UVH_GR1_TLB_INT1_CONFIG"))


/* UVXH common defines */
#define UVXH_GR1_TLB_INT1_CONFIG_VECTOR_SHFT		0
#define UVXH_GR1_TLB_INT1_CONFIG_VECTOR_MASK		0x00000000000000ffUL
#define UVXH_GR1_TLB_INT1_CONFIG_DM_SHFT		8
#define UVXH_GR1_TLB_INT1_CONFIG_DM_MASK		0x0000000000000700UL
#define UVXH_GR1_TLB_INT1_CONFIG_DESTMODE_SHFT		11
#define UVXH_GR1_TLB_INT1_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVXH_GR1_TLB_INT1_CONFIG_STATUS_SHFT		12
#define UVXH_GR1_TLB_INT1_CONFIG_STATUS_MASK		0x0000000000001000UL
#define UVXH_GR1_TLB_INT1_CONFIG_P_SHFT			13
#define UVXH_GR1_TLB_INT1_CONFIG_P_MASK			0x0000000000002000UL
#define UVXH_GR1_TLB_INT1_CONFIG_T_SHFT			15
#define UVXH_GR1_TLB_INT1_CONFIG_T_MASK			0x0000000000008000UL
#define UVXH_GR1_TLB_INT1_CONFIG_M_SHFT			16
#define UVXH_GR1_TLB_INT1_CONFIG_M_MASK			0x0000000000010000UL
#define UVXH_GR1_TLB_INT1_CONFIG_APIC_ID_SHFT		32
#define UVXH_GR1_TLB_INT1_CONFIG_APIC_ID_MASK		0xffffffff00000000UL


union uvh_gr1_tlb_int1_config_u {
	unsigned long	v;

	/* UVH common struct */
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

	/* UVXH common struct */
	struct uvxh_gr1_tlb_int1_config_s {
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
	} sx;

	/* UV4 unique struct */
	struct uv4h_gr1_tlb_int1_config_s {
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
	} s4;

	/* UV3 unique struct */
	struct uv3h_gr1_tlb_int1_config_s {
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
	} s3;

	/* UV2 unique struct */
	struct uv2h_gr1_tlb_int1_config_s {
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
	} s2;
};

/* ========================================================================= */
/*                               UVH_INT_CMPB                                */
/* ========================================================================= */
#define UVH_INT_CMPB 0x22080UL

/* UVH common defines*/
#define UVH_INT_CMPB_REAL_TIME_CMPB_SHFT		0
#define UVH_INT_CMPB_REAL_TIME_CMPB_MASK		0x00ffffffffffffffUL


union uvh_int_cmpb_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_int_cmpb_s {
		unsigned long	real_time_cmpb:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s;

	/* UV5 unique struct */
	struct uv5h_int_cmpb_s {
		unsigned long	real_time_cmpb:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s5;

	/* UV4 unique struct */
	struct uv4h_int_cmpb_s {
		unsigned long	real_time_cmpb:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s4;

	/* UV3 unique struct */
	struct uv3h_int_cmpb_s {
		unsigned long	real_time_cmpb:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s3;

	/* UV2 unique struct */
	struct uv2h_int_cmpb_s {
		unsigned long	real_time_cmpb:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s2;
};

/* ========================================================================= */
/*                               UVH_IPI_INT                                 */
/* ========================================================================= */
#define UVH_IPI_INT 0x60500UL

/* UVH common defines*/
#define UVH_IPI_INT_VECTOR_SHFT				0
#define UVH_IPI_INT_VECTOR_MASK				0x00000000000000ffUL
#define UVH_IPI_INT_DELIVERY_MODE_SHFT			8
#define UVH_IPI_INT_DELIVERY_MODE_MASK			0x0000000000000700UL
#define UVH_IPI_INT_DESTMODE_SHFT			11
#define UVH_IPI_INT_DESTMODE_MASK			0x0000000000000800UL
#define UVH_IPI_INT_APIC_ID_SHFT			16
#define UVH_IPI_INT_APIC_ID_MASK			0x0000ffffffff0000UL
#define UVH_IPI_INT_SEND_SHFT				63
#define UVH_IPI_INT_SEND_MASK				0x8000000000000000UL


union uvh_ipi_int_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_ipi_int_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	delivery_mode:3;		/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	rsvd_12_15:4;
		unsigned long	apic_id:32;			/* RW */
		unsigned long	rsvd_48_62:15;
		unsigned long	send:1;				/* WP */
	} s;

	/* UV5 unique struct */
	struct uv5h_ipi_int_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	delivery_mode:3;		/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	rsvd_12_15:4;
		unsigned long	apic_id:32;			/* RW */
		unsigned long	rsvd_48_62:15;
		unsigned long	send:1;				/* WP */
	} s5;

	/* UV4 unique struct */
	struct uv4h_ipi_int_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	delivery_mode:3;		/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	rsvd_12_15:4;
		unsigned long	apic_id:32;			/* RW */
		unsigned long	rsvd_48_62:15;
		unsigned long	send:1;				/* WP */
	} s4;

	/* UV3 unique struct */
	struct uv3h_ipi_int_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	delivery_mode:3;		/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	rsvd_12_15:4;
		unsigned long	apic_id:32;			/* RW */
		unsigned long	rsvd_48_62:15;
		unsigned long	send:1;				/* WP */
	} s3;

	/* UV2 unique struct */
	struct uv2h_ipi_int_s {
		unsigned long	vector_:8;			/* RW */
		unsigned long	delivery_mode:3;		/* RW */
		unsigned long	destmode:1;			/* RW */
		unsigned long	rsvd_12_15:4;
		unsigned long	apic_id:32;			/* RW */
		unsigned long	rsvd_48_62:15;
		unsigned long	send:1;				/* WP */
	} s2;
};

/* ========================================================================= */
/*                               UVH_NODE_ID                                 */
/* ========================================================================= */
#define UVH_NODE_ID 0x0UL

/* UVH common defines*/
#define UVH_NODE_ID_FORCE1_SHFT				0
#define UVH_NODE_ID_FORCE1_MASK				0x0000000000000001UL
#define UVH_NODE_ID_MANUFACTURER_SHFT			1
#define UVH_NODE_ID_MANUFACTURER_MASK			0x0000000000000ffeUL
#define UVH_NODE_ID_PART_NUMBER_SHFT			12
#define UVH_NODE_ID_PART_NUMBER_MASK			0x000000000ffff000UL
#define UVH_NODE_ID_REVISION_SHFT			28
#define UVH_NODE_ID_REVISION_MASK			0x00000000f0000000UL
#define UVH_NODE_ID_NODE_ID_SHFT			32
#define UVH_NODE_ID_NI_PORT_SHFT			57

/* UVXH common defines */
#define UVXH_NODE_ID_NODE_ID_MASK			0x00007fff00000000UL
#define UVXH_NODE_ID_NODES_PER_BIT_SHFT			50
#define UVXH_NODE_ID_NODES_PER_BIT_MASK			0x01fc000000000000UL
#define UVXH_NODE_ID_NI_PORT_MASK			0x3e00000000000000UL

/* UVYH common defines */
#define UVYH_NODE_ID_NODE_ID_MASK			0x0000007f00000000UL
#define UVYH_NODE_ID_NI_PORT_MASK			0x7e00000000000000UL

/* UV4 unique defines */
#define UV4H_NODE_ID_ROUTER_SELECT_SHFT			48
#define UV4H_NODE_ID_ROUTER_SELECT_MASK			0x0001000000000000UL
#define UV4H_NODE_ID_RESERVED_2_SHFT			49
#define UV4H_NODE_ID_RESERVED_2_MASK			0x0002000000000000UL

/* UV3 unique defines */
#define UV3H_NODE_ID_ROUTER_SELECT_SHFT			48
#define UV3H_NODE_ID_ROUTER_SELECT_MASK			0x0001000000000000UL
#define UV3H_NODE_ID_RESERVED_2_SHFT			49
#define UV3H_NODE_ID_RESERVED_2_MASK			0x0002000000000000UL


union uvh_node_id_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_node_id_s {
		unsigned long	force1:1;			/* RO */
		unsigned long	manufacturer:11;		/* RO */
		unsigned long	part_number:16;			/* RO */
		unsigned long	revision:4;			/* RO */
		unsigned long	rsvd_32_63:32;
	} s;

	/* UVXH common struct */
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

	/* UVYH common struct */
	struct uvyh_node_id_s {
		unsigned long	force1:1;			/* RO */
		unsigned long	manufacturer:11;		/* RO */
		unsigned long	part_number:16;			/* RO */
		unsigned long	revision:4;			/* RO */
		unsigned long	node_id:7;			/* RW */
		unsigned long	rsvd_39_56:18;
		unsigned long	ni_port:6;			/* RO */
		unsigned long	rsvd_63:1;
	} sy;

	/* UV5 unique struct */
	struct uv5h_node_id_s {
		unsigned long	force1:1;			/* RO */
		unsigned long	manufacturer:11;		/* RO */
		unsigned long	part_number:16;			/* RO */
		unsigned long	revision:4;			/* RO */
		unsigned long	node_id:7;			/* RW */
		unsigned long	rsvd_39_56:18;
		unsigned long	ni_port:6;			/* RO */
		unsigned long	rsvd_63:1;
	} s5;

	/* UV4 unique struct */
	struct uv4h_node_id_s {
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
	} s4;

	/* UV3 unique struct */
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

	/* UV2 unique struct */
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
};

/* ========================================================================= */
/*                            UVH_NODE_PRESENT_0                             */
/* ========================================================================= */
#define UVH_NODE_PRESENT_0 (						\
	is_uv(UV5) ? 0x1400UL :						\
	0)


/* UVYH common defines */
#define UVYH_NODE_PRESENT_0_NODES_SHFT			0
#define UVYH_NODE_PRESENT_0_NODES_MASK			0xffffffffffffffffUL


union uvh_node_present_0_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_node_present_0_s {
		unsigned long	nodes:64;			/* RW */
	} s;

	/* UVYH common struct */
	struct uvyh_node_present_0_s {
		unsigned long	nodes:64;			/* RW */
	} sy;

	/* UV5 unique struct */
	struct uv5h_node_present_0_s {
		unsigned long	nodes:64;			/* RW */
	} s5;
};

/* ========================================================================= */
/*                            UVH_NODE_PRESENT_1                             */
/* ========================================================================= */
#define UVH_NODE_PRESENT_1 (						\
	is_uv(UV5) ? 0x1408UL :						\
	0)


/* UVYH common defines */
#define UVYH_NODE_PRESENT_1_NODES_SHFT			0
#define UVYH_NODE_PRESENT_1_NODES_MASK			0xffffffffffffffffUL


union uvh_node_present_1_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_node_present_1_s {
		unsigned long	nodes:64;			/* RW */
	} s;

	/* UVYH common struct */
	struct uvyh_node_present_1_s {
		unsigned long	nodes:64;			/* RW */
	} sy;

	/* UV5 unique struct */
	struct uv5h_node_present_1_s {
		unsigned long	nodes:64;			/* RW */
	} s5;
};

/* ========================================================================= */
/*                          UVH_NODE_PRESENT_TABLE                           */
/* ========================================================================= */
#define UVH_NODE_PRESENT_TABLE (					\
	is_uv(UV4) ? 0x1400UL :						\
	is_uv(UV3) ? 0x1400UL :						\
	is_uv(UV2) ? 0x1400UL :						\
	0)

#define UVH_NODE_PRESENT_TABLE_DEPTH (					\
	is_uv(UV4) ? 4 :						\
	is_uv(UV3) ? 16 :						\
	is_uv(UV2) ? 16 :						\
	0)


/* UVXH common defines */
#define UVXH_NODE_PRESENT_TABLE_NODES_SHFT		0
#define UVXH_NODE_PRESENT_TABLE_NODES_MASK		0xffffffffffffffffUL


union uvh_node_present_table_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_node_present_table_s {
		unsigned long	nodes:64;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_node_present_table_s {
		unsigned long	nodes:64;			/* RW */
	} sx;

	/* UV4 unique struct */
	struct uv4h_node_present_table_s {
		unsigned long	nodes:64;			/* RW */
	} s4;

	/* UV3 unique struct */
	struct uv3h_node_present_table_s {
		unsigned long	nodes:64;			/* RW */
	} s3;

	/* UV2 unique struct */
	struct uv2h_node_present_table_s {
		unsigned long	nodes:64;			/* RW */
	} s2;
};

/* ========================================================================= */
/*                       UVH_RH10_GAM_ADDR_MAP_CONFIG                        */
/* ========================================================================= */
#define UVH_RH10_GAM_ADDR_MAP_CONFIG (					\
	is_uv(UV5) ? 0x470000UL :					\
	0)


/* UVYH common defines */
#define UVYH_RH10_GAM_ADDR_MAP_CONFIG_N_SKT_SHFT	6
#define UVYH_RH10_GAM_ADDR_MAP_CONFIG_N_SKT_MASK	0x00000000000001c0UL
#define UVYH_RH10_GAM_ADDR_MAP_CONFIG_LS_ENABLE_SHFT	12
#define UVYH_RH10_GAM_ADDR_MAP_CONFIG_LS_ENABLE_MASK	0x0000000000001000UL
#define UVYH_RH10_GAM_ADDR_MAP_CONFIG_MK_TME_KEYID_BITS_SHFT 16
#define UVYH_RH10_GAM_ADDR_MAP_CONFIG_MK_TME_KEYID_BITS_MASK 0x00000000000f0000UL


union uvh_rh10_gam_addr_map_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh10_gam_addr_map_config_s {
		unsigned long	undef_0_5:6;			/* Undefined */
		unsigned long	n_skt:3;			/* RW */
		unsigned long	undef_9_11:3;			/* Undefined */
		unsigned long	ls_enable:1;			/* RW */
		unsigned long	undef_13_15:3;			/* Undefined */
		unsigned long	mk_tme_keyid_bits:4;		/* RW */
		unsigned long	rsvd_20_63:44;
	} s;

	/* UVYH common struct */
	struct uvyh_rh10_gam_addr_map_config_s {
		unsigned long	undef_0_5:6;			/* Undefined */
		unsigned long	n_skt:3;			/* RW */
		unsigned long	undef_9_11:3;			/* Undefined */
		unsigned long	ls_enable:1;			/* RW */
		unsigned long	undef_13_15:3;			/* Undefined */
		unsigned long	mk_tme_keyid_bits:4;		/* RW */
		unsigned long	rsvd_20_63:44;
	} sy;

	/* UV5 unique struct */
	struct uv5h_rh10_gam_addr_map_config_s {
		unsigned long	undef_0_5:6;			/* Undefined */
		unsigned long	n_skt:3;			/* RW */
		unsigned long	undef_9_11:3;			/* Undefined */
		unsigned long	ls_enable:1;			/* RW */
		unsigned long	undef_13_15:3;			/* Undefined */
		unsigned long	mk_tme_keyid_bits:4;		/* RW */
	} s5;
};

/* ========================================================================= */
/*                     UVH_RH10_GAM_GRU_OVERLAY_CONFIG                       */
/* ========================================================================= */
#define UVH_RH10_GAM_GRU_OVERLAY_CONFIG (				\
	is_uv(UV5) ? 0x4700b0UL :					\
	0)


/* UVYH common defines */
#define UVYH_RH10_GAM_GRU_OVERLAY_CONFIG_BASE_SHFT	25
#define UVYH_RH10_GAM_GRU_OVERLAY_CONFIG_BASE_MASK	0x000ffffffe000000UL
#define UVYH_RH10_GAM_GRU_OVERLAY_CONFIG_N_GRU_SHFT	52
#define UVYH_RH10_GAM_GRU_OVERLAY_CONFIG_N_GRU_MASK	0x0070000000000000UL
#define UVYH_RH10_GAM_GRU_OVERLAY_CONFIG_ENABLE_SHFT	63
#define UVYH_RH10_GAM_GRU_OVERLAY_CONFIG_ENABLE_MASK	0x8000000000000000UL

#define UVH_RH10_GAM_GRU_OVERLAY_CONFIG_BASE_MASK (			\
	is_uv(UV5) ? 0x000ffffffe000000UL :				\
	0)
#define UVH_RH10_GAM_GRU_OVERLAY_CONFIG_BASE_SHFT (			\
	is_uv(UV5) ? 25 :						\
	-1)

union uvh_rh10_gam_gru_overlay_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh10_gam_gru_overlay_config_s {
		unsigned long	undef_0_24:25;			/* Undefined */
		unsigned long	base:27;			/* RW */
		unsigned long	n_gru:3;			/* RW */
		unsigned long	undef_55_62:8;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVYH common struct */
	struct uvyh_rh10_gam_gru_overlay_config_s {
		unsigned long	undef_0_24:25;			/* Undefined */
		unsigned long	base:27;			/* RW */
		unsigned long	n_gru:3;			/* RW */
		unsigned long	undef_55_62:8;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} sy;

	/* UV5 unique struct */
	struct uv5h_rh10_gam_gru_overlay_config_s {
		unsigned long	undef_0_24:25;			/* Undefined */
		unsigned long	base:27;			/* RW */
		unsigned long	n_gru:3;			/* RW */
		unsigned long	undef_55_62:8;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s5;
};

/* ========================================================================= */
/*                    UVH_RH10_GAM_MMIOH_OVERLAY_CONFIG0                     */
/* ========================================================================= */
#define UVH_RH10_GAM_MMIOH_OVERLAY_CONFIG0 (				\
	is_uv(UV5) ? 0x473000UL :					\
	0)


/* UVYH common defines */
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG0_BASE_SHFT	26
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG0_BASE_MASK	0x000ffffffc000000UL
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG0_M_IO_SHFT	52
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG0_M_IO_MASK	0x03f0000000000000UL
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG0_ENABLE_SHFT	63
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG0_ENABLE_MASK	0x8000000000000000UL

#define UVH_RH10_GAM_MMIOH_OVERLAY_CONFIG0_BASE_MASK (			\
	is_uv(UV5) ? 0x000ffffffc000000UL :				\
	0)
#define UVH_RH10_GAM_MMIOH_OVERLAY_CONFIG0_BASE_SHFT (			\
	is_uv(UV5) ? 26 :						\
	-1)

union uvh_rh10_gam_mmioh_overlay_config0_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh10_gam_mmioh_overlay_config0_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:26;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	undef_62:1;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVYH common struct */
	struct uvyh_rh10_gam_mmioh_overlay_config0_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:26;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	undef_62:1;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} sy;

	/* UV5 unique struct */
	struct uv5h_rh10_gam_mmioh_overlay_config0_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:26;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	undef_62:1;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s5;
};

/* ========================================================================= */
/*                    UVH_RH10_GAM_MMIOH_OVERLAY_CONFIG1                     */
/* ========================================================================= */
#define UVH_RH10_GAM_MMIOH_OVERLAY_CONFIG1 (				\
	is_uv(UV5) ? 0x474000UL :					\
	0)


/* UVYH common defines */
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG1_BASE_SHFT	26
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG1_BASE_MASK	0x000ffffffc000000UL
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG1_M_IO_SHFT	52
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG1_M_IO_MASK	0x03f0000000000000UL
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG1_ENABLE_SHFT	63
#define UVYH_RH10_GAM_MMIOH_OVERLAY_CONFIG1_ENABLE_MASK	0x8000000000000000UL

#define UVH_RH10_GAM_MMIOH_OVERLAY_CONFIG1_BASE_MASK (			\
	is_uv(UV5) ? 0x000ffffffc000000UL :				\
	0)
#define UVH_RH10_GAM_MMIOH_OVERLAY_CONFIG1_BASE_SHFT (			\
	is_uv(UV5) ? 26 :						\
	-1)

union uvh_rh10_gam_mmioh_overlay_config1_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh10_gam_mmioh_overlay_config1_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:26;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	undef_62:1;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVYH common struct */
	struct uvyh_rh10_gam_mmioh_overlay_config1_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:26;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	undef_62:1;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} sy;

	/* UV5 unique struct */
	struct uv5h_rh10_gam_mmioh_overlay_config1_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:26;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	undef_62:1;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s5;
};

/* ========================================================================= */
/*                   UVH_RH10_GAM_MMIOH_REDIRECT_CONFIG0                     */
/* ========================================================================= */
#define UVH_RH10_GAM_MMIOH_REDIRECT_CONFIG0 (				\
	is_uv(UV5) ? 0x473800UL :					\
	0)

#define UVH_RH10_GAM_MMIOH_REDIRECT_CONFIG0_DEPTH (			\
	is_uv(UV5) ? 128 :						\
	0)


/* UVYH common defines */
#define UVYH_RH10_GAM_MMIOH_REDIRECT_CONFIG0_NASID_SHFT	0
#define UVYH_RH10_GAM_MMIOH_REDIRECT_CONFIG0_NASID_MASK	0x000000000000007fUL


union uvh_rh10_gam_mmioh_redirect_config0_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh10_gam_mmioh_redirect_config0_s {
		unsigned long	nasid:7;			/* RW */
		unsigned long	rsvd_7_63:57;
	} s;

	/* UVYH common struct */
	struct uvyh_rh10_gam_mmioh_redirect_config0_s {
		unsigned long	nasid:7;			/* RW */
		unsigned long	rsvd_7_63:57;
	} sy;

	/* UV5 unique struct */
	struct uv5h_rh10_gam_mmioh_redirect_config0_s {
		unsigned long	nasid:7;			/* RW */
		unsigned long	rsvd_7_63:57;
	} s5;
};

/* ========================================================================= */
/*                   UVH_RH10_GAM_MMIOH_REDIRECT_CONFIG1                     */
/* ========================================================================= */
#define UVH_RH10_GAM_MMIOH_REDIRECT_CONFIG1 (				\
	is_uv(UV5) ? 0x474800UL :					\
	0)

#define UVH_RH10_GAM_MMIOH_REDIRECT_CONFIG1_DEPTH (			\
	is_uv(UV5) ? 128 :						\
	0)


/* UVYH common defines */
#define UVYH_RH10_GAM_MMIOH_REDIRECT_CONFIG1_NASID_SHFT	0
#define UVYH_RH10_GAM_MMIOH_REDIRECT_CONFIG1_NASID_MASK	0x000000000000007fUL


union uvh_rh10_gam_mmioh_redirect_config1_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh10_gam_mmioh_redirect_config1_s {
		unsigned long	nasid:7;			/* RW */
		unsigned long	rsvd_7_63:57;
	} s;

	/* UVYH common struct */
	struct uvyh_rh10_gam_mmioh_redirect_config1_s {
		unsigned long	nasid:7;			/* RW */
		unsigned long	rsvd_7_63:57;
	} sy;

	/* UV5 unique struct */
	struct uv5h_rh10_gam_mmioh_redirect_config1_s {
		unsigned long	nasid:7;			/* RW */
		unsigned long	rsvd_7_63:57;
	} s5;
};

/* ========================================================================= */
/*                     UVH_RH10_GAM_MMR_OVERLAY_CONFIG                       */
/* ========================================================================= */
#define UVH_RH10_GAM_MMR_OVERLAY_CONFIG (				\
	is_uv(UV5) ? 0x470090UL :					\
	0)


/* UVYH common defines */
#define UVYH_RH10_GAM_MMR_OVERLAY_CONFIG_BASE_SHFT	25
#define UVYH_RH10_GAM_MMR_OVERLAY_CONFIG_BASE_MASK	0x000ffffffe000000UL
#define UVYH_RH10_GAM_MMR_OVERLAY_CONFIG_ENABLE_SHFT	63
#define UVYH_RH10_GAM_MMR_OVERLAY_CONFIG_ENABLE_MASK	0x8000000000000000UL

#define UVH_RH10_GAM_MMR_OVERLAY_CONFIG_BASE_MASK (			\
	is_uv(UV5) ? 0x000ffffffe000000UL :				\
	0)
#define UVH_RH10_GAM_MMR_OVERLAY_CONFIG_BASE_SHFT (			\
	is_uv(UV5) ? 25 :						\
	-1)

union uvh_rh10_gam_mmr_overlay_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh10_gam_mmr_overlay_config_s {
		unsigned long	undef_0_24:25;			/* Undefined */
		unsigned long	base:27;			/* RW */
		unsigned long	undef_52_62:11;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVYH common struct */
	struct uvyh_rh10_gam_mmr_overlay_config_s {
		unsigned long	undef_0_24:25;			/* Undefined */
		unsigned long	base:27;			/* RW */
		unsigned long	undef_52_62:11;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} sy;

	/* UV5 unique struct */
	struct uv5h_rh10_gam_mmr_overlay_config_s {
		unsigned long	undef_0_24:25;			/* Undefined */
		unsigned long	base:27;			/* RW */
		unsigned long	undef_52_62:11;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s5;
};

/* ========================================================================= */
/*                        UVH_RH_GAM_ADDR_MAP_CONFIG                         */
/* ========================================================================= */
#define UVH_RH_GAM_ADDR_MAP_CONFIG (					\
	is_uv(UV4) ? 0x480000UL :					\
	is_uv(UV3) ? 0x1600000UL :					\
	is_uv(UV2) ? 0x1600000UL :					\
	0)


/* UVXH common defines */
#define UVXH_RH_GAM_ADDR_MAP_CONFIG_N_SKT_SHFT		6
#define UVXH_RH_GAM_ADDR_MAP_CONFIG_N_SKT_MASK		0x00000000000003c0UL

/* UV3 unique defines */
#define UV3H_RH_GAM_ADDR_MAP_CONFIG_M_SKT_SHFT		0
#define UV3H_RH_GAM_ADDR_MAP_CONFIG_M_SKT_MASK		0x000000000000003fUL

/* UV2 unique defines */
#define UV2H_RH_GAM_ADDR_MAP_CONFIG_M_SKT_SHFT		0
#define UV2H_RH_GAM_ADDR_MAP_CONFIG_M_SKT_MASK		0x000000000000003fUL


union uvh_rh_gam_addr_map_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_addr_map_config_s {
		unsigned long	rsvd_0_5:6;
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_63:54;
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_addr_map_config_s {
		unsigned long	rsvd_0_5:6;
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_63:54;
	} sx;

	/* UV4 unique struct */
	struct uv4h_rh_gam_addr_map_config_s {
		unsigned long	rsvd_0_5:6;
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_63:54;
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_addr_map_config_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_63:54;
	} s3;

	/* UV2 unique struct */
	struct uv2h_rh_gam_addr_map_config_s {
		unsigned long	m_skt:6;			/* RW */
		unsigned long	n_skt:4;			/* RW */
		unsigned long	rsvd_10_63:54;
	} s2;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_ALIAS_0_OVERLAY_CONFIG                      */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS_0_OVERLAY_CONFIG (				\
	is_uv(UV4) ? 0x4800c8UL :					\
	is_uv(UV3) ? 0x16000c8UL :					\
	is_uv(UV2) ? 0x16000c8UL :					\
	0)


/* UVXH common defines */
#define UVXH_RH_GAM_ALIAS_0_OVERLAY_CONFIG_BASE_SHFT	24
#define UVXH_RH_GAM_ALIAS_0_OVERLAY_CONFIG_BASE_MASK	0x00000000ff000000UL
#define UVXH_RH_GAM_ALIAS_0_OVERLAY_CONFIG_M_ALIAS_SHFT	48
#define UVXH_RH_GAM_ALIAS_0_OVERLAY_CONFIG_M_ALIAS_MASK	0x001f000000000000UL
#define UVXH_RH_GAM_ALIAS_0_OVERLAY_CONFIG_ENABLE_SHFT	63
#define UVXH_RH_GAM_ALIAS_0_OVERLAY_CONFIG_ENABLE_MASK	0x8000000000000000UL


union uvh_rh_gam_alias_0_overlay_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_alias_0_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_alias_0_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} sx;

	/* UV4 unique struct */
	struct uv4h_rh_gam_alias_0_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_alias_0_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s3;

	/* UV2 unique struct */
	struct uv2h_rh_gam_alias_0_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s2;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_ALIAS_0_REDIRECT_CONFIG                     */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS_0_REDIRECT_CONFIG (				\
	is_uv(UV4) ? 0x4800d0UL :					\
	is_uv(UV3) ? 0x16000d0UL :					\
	is_uv(UV2) ? 0x16000d0UL :					\
	0)


/* UVXH common defines */
#define UVXH_RH_GAM_ALIAS_0_REDIRECT_CONFIG_DEST_BASE_SHFT 24
#define UVXH_RH_GAM_ALIAS_0_REDIRECT_CONFIG_DEST_BASE_MASK 0x00003fffff000000UL


union uvh_rh_gam_alias_0_redirect_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_alias_0_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_alias_0_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} sx;

	/* UV4 unique struct */
	struct uv4h_rh_gam_alias_0_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_alias_0_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s3;

	/* UV2 unique struct */
	struct uv2h_rh_gam_alias_0_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s2;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_ALIAS_1_OVERLAY_CONFIG                      */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS_1_OVERLAY_CONFIG (				\
	is_uv(UV4) ? 0x4800d8UL :					\
	is_uv(UV3) ? 0x16000d8UL :					\
	is_uv(UV2) ? 0x16000d8UL :					\
	0)


/* UVXH common defines */
#define UVXH_RH_GAM_ALIAS_1_OVERLAY_CONFIG_BASE_SHFT	24
#define UVXH_RH_GAM_ALIAS_1_OVERLAY_CONFIG_BASE_MASK	0x00000000ff000000UL
#define UVXH_RH_GAM_ALIAS_1_OVERLAY_CONFIG_M_ALIAS_SHFT	48
#define UVXH_RH_GAM_ALIAS_1_OVERLAY_CONFIG_M_ALIAS_MASK	0x001f000000000000UL
#define UVXH_RH_GAM_ALIAS_1_OVERLAY_CONFIG_ENABLE_SHFT	63
#define UVXH_RH_GAM_ALIAS_1_OVERLAY_CONFIG_ENABLE_MASK	0x8000000000000000UL


union uvh_rh_gam_alias_1_overlay_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_alias_1_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_alias_1_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} sx;

	/* UV4 unique struct */
	struct uv4h_rh_gam_alias_1_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_alias_1_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s3;

	/* UV2 unique struct */
	struct uv2h_rh_gam_alias_1_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s2;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_ALIAS_1_REDIRECT_CONFIG                     */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS_1_REDIRECT_CONFIG (				\
	is_uv(UV4) ? 0x4800e0UL :					\
	is_uv(UV3) ? 0x16000e0UL :					\
	is_uv(UV2) ? 0x16000e0UL :					\
	0)


/* UVXH common defines */
#define UVXH_RH_GAM_ALIAS_1_REDIRECT_CONFIG_DEST_BASE_SHFT 24
#define UVXH_RH_GAM_ALIAS_1_REDIRECT_CONFIG_DEST_BASE_MASK 0x00003fffff000000UL


union uvh_rh_gam_alias_1_redirect_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_alias_1_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_alias_1_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} sx;

	/* UV4 unique struct */
	struct uv4h_rh_gam_alias_1_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_alias_1_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s3;

	/* UV2 unique struct */
	struct uv2h_rh_gam_alias_1_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s2;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_ALIAS_2_OVERLAY_CONFIG                      */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS_2_OVERLAY_CONFIG (				\
	is_uv(UV4) ? 0x4800e8UL :					\
	is_uv(UV3) ? 0x16000e8UL :					\
	is_uv(UV2) ? 0x16000e8UL :					\
	0)


/* UVXH common defines */
#define UVXH_RH_GAM_ALIAS_2_OVERLAY_CONFIG_BASE_SHFT	24
#define UVXH_RH_GAM_ALIAS_2_OVERLAY_CONFIG_BASE_MASK	0x00000000ff000000UL
#define UVXH_RH_GAM_ALIAS_2_OVERLAY_CONFIG_M_ALIAS_SHFT	48
#define UVXH_RH_GAM_ALIAS_2_OVERLAY_CONFIG_M_ALIAS_MASK	0x001f000000000000UL
#define UVXH_RH_GAM_ALIAS_2_OVERLAY_CONFIG_ENABLE_SHFT	63
#define UVXH_RH_GAM_ALIAS_2_OVERLAY_CONFIG_ENABLE_MASK	0x8000000000000000UL


union uvh_rh_gam_alias_2_overlay_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_alias_2_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_alias_2_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} sx;

	/* UV4 unique struct */
	struct uv4h_rh_gam_alias_2_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_alias_2_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s3;

	/* UV2 unique struct */
	struct uv2h_rh_gam_alias_2_overlay_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	base:8;				/* RW */
		unsigned long	rsvd_32_47:16;
		unsigned long	m_alias:5;			/* RW */
		unsigned long	rsvd_53_62:10;
		unsigned long	enable:1;			/* RW */
	} s2;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_ALIAS_2_REDIRECT_CONFIG                     */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS_2_REDIRECT_CONFIG (				\
	is_uv(UV4) ? 0x4800f0UL :					\
	is_uv(UV3) ? 0x16000f0UL :					\
	is_uv(UV2) ? 0x16000f0UL :					\
	0)


/* UVXH common defines */
#define UVXH_RH_GAM_ALIAS_2_REDIRECT_CONFIG_DEST_BASE_SHFT 24
#define UVXH_RH_GAM_ALIAS_2_REDIRECT_CONFIG_DEST_BASE_MASK 0x00003fffff000000UL


union uvh_rh_gam_alias_2_redirect_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_alias_2_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_alias_2_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} sx;

	/* UV4 unique struct */
	struct uv4h_rh_gam_alias_2_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_alias_2_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s3;

	/* UV2 unique struct */
	struct uv2h_rh_gam_alias_2_redirect_config_s {
		unsigned long	rsvd_0_23:24;
		unsigned long	dest_base:22;			/* RW */
		unsigned long	rsvd_46_63:18;
	} s2;
};

/* ========================================================================= */
/*                      UVH_RH_GAM_GRU_OVERLAY_CONFIG                        */
/* ========================================================================= */
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG (					\
	is_uv(UV4) ? 0x480010UL :					\
	is_uv(UV3) ? 0x1600010UL :					\
	is_uv(UV2) ? 0x1600010UL :					\
	0)


/* UVXH common defines */
#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_N_GRU_SHFT	52
#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_N_GRU_MASK	0x00f0000000000000UL
#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_ENABLE_SHFT	63
#define UVXH_RH_GAM_GRU_OVERLAY_CONFIG_ENABLE_MASK	0x8000000000000000UL

/* UV4A unique defines */
#define UV4AH_RH_GAM_GRU_OVERLAY_CONFIG_BASE_SHFT	26
#define UV4AH_RH_GAM_GRU_OVERLAY_CONFIG_BASE_MASK	0x000ffffffc000000UL

/* UV4 unique defines */
#define UV4H_RH_GAM_GRU_OVERLAY_CONFIG_BASE_SHFT	26
#define UV4H_RH_GAM_GRU_OVERLAY_CONFIG_BASE_MASK	0x00003ffffc000000UL

/* UV3 unique defines */
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_BASE_SHFT	28
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_BASE_MASK	0x00003ffff0000000UL
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MODE_SHFT	62
#define UV3H_RH_GAM_GRU_OVERLAY_CONFIG_MODE_MASK	0x4000000000000000UL

/* UV2 unique defines */
#define UV2H_RH_GAM_GRU_OVERLAY_CONFIG_BASE_SHFT	28
#define UV2H_RH_GAM_GRU_OVERLAY_CONFIG_BASE_MASK	0x00003ffff0000000UL

#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_BASE_MASK (			\
	is_uv(UV4A) ? 0x000ffffffc000000UL :				\
	is_uv(UV4) ? 0x00003ffffc000000UL :				\
	is_uv(UV3) ? 0x00003ffff0000000UL :				\
	is_uv(UV2) ? 0x00003ffff0000000UL :				\
	0)
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_BASE_SHFT (			\
	is_uv(UV4) ? 26 :						\
	is_uv(UV3) ? 28 :						\
	is_uv(UV2) ? 28 :						\
	-1)

union uvh_rh_gam_gru_overlay_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_gru_overlay_config_s {
		unsigned long	rsvd_0_45:46;
		unsigned long	rsvd_46_51:6;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_gru_overlay_config_s {
		unsigned long	rsvd_0_45:46;
		unsigned long	rsvd_46_51:6;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} sx;

	/* UV4A unique struct */
	struct uv4ah_rh_gam_gru_overlay_config_s {
		unsigned long	rsvd_0_24:25;
		unsigned long	undef_25:1;			/* Undefined */
		unsigned long	base:26;			/* RW */
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s4a;

	/* UV4 unique struct */
	struct uv4h_rh_gam_gru_overlay_config_s {
		unsigned long	rsvd_0_24:25;
		unsigned long	undef_25:1;			/* Undefined */
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_51:6;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_gru_overlay_config_s {
		unsigned long	rsvd_0_27:28;
		unsigned long	base:18;			/* RW */
		unsigned long	rsvd_46_51:6;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_61:6;
		unsigned long	mode:1;				/* RW */
		unsigned long	enable:1;			/* RW */
	} s3;

	/* UV2 unique struct */
	struct uv2h_rh_gam_gru_overlay_config_s {
		unsigned long	rsvd_0_27:28;
		unsigned long	base:18;			/* RW */
		unsigned long	rsvd_46_51:6;
		unsigned long	n_gru:4;			/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s2;
};

/* ========================================================================= */
/*                     UVH_RH_GAM_MMIOH_OVERLAY_CONFIG                       */
/* ========================================================================= */
#define UVH_RH_GAM_MMIOH_OVERLAY_CONFIG (				\
	is_uv(UV2) ? 0x1600030UL :					\
	0)



/* UV2 unique defines */
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_BASE_SHFT	27
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_BASE_MASK	0x00003ffff8000000UL
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_M_IO_SHFT	46
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_M_IO_MASK	0x000fc00000000000UL
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_N_IO_SHFT	52
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_N_IO_MASK	0x00f0000000000000UL
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_ENABLE_SHFT	63
#define UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_ENABLE_MASK	0x8000000000000000UL

#define UVH_RH_GAM_MMIOH_OVERLAY_CONFIG_BASE_SHFT (			\
	is_uv(UV2) ? 27 :						\
	uv_undefined("UVH_RH_GAM_MMIOH_OVERLAY_CONFIG_BASE_SHFT"))

union uvh_rh_gam_mmioh_overlay_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_mmioh_overlay_config_s {
		unsigned long	rsvd_0_26:27;
		unsigned long	base:19;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;				/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_mmioh_overlay_config_s {
		unsigned long	rsvd_0_26:27;
		unsigned long	base:19;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;				/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} sx;

	/* UV2 unique struct */
	struct uv2h_rh_gam_mmioh_overlay_config_s {
		unsigned long	rsvd_0_26:27;
		unsigned long	base:19;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;				/* RW */
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s2;
};

/* ========================================================================= */
/*                     UVH_RH_GAM_MMIOH_OVERLAY_CONFIG0                      */
/* ========================================================================= */
#define UVH_RH_GAM_MMIOH_OVERLAY_CONFIG0 (				\
	is_uv(UV4) ? 0x483000UL :					\
	is_uv(UV3) ? 0x1603000UL :					\
	0)

/* UV4A unique defines */
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG0_BASE_SHFT	26
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG0_BASE_MASK	0x000ffffffc000000UL
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG0_M_IO_SHFT	52
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG0_M_IO_MASK	0x03f0000000000000UL
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG0_ENABLE_SHFT	63
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG0_ENABLE_MASK	0x8000000000000000UL

/* UV4 unique defines */
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG0_BASE_SHFT	26
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG0_BASE_MASK	0x00003ffffc000000UL
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG0_M_IO_SHFT	46
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG0_M_IO_MASK	0x000fc00000000000UL
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG0_ENABLE_SHFT	63
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG0_ENABLE_MASK	0x8000000000000000UL

/* UV3 unique defines */
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_BASE_SHFT	26
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_BASE_MASK	0x00003ffffc000000UL
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_M_IO_SHFT	46
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_M_IO_MASK	0x000fc00000000000UL
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_ENABLE_SHFT	63
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_ENABLE_MASK	0x8000000000000000UL

#define UVH_RH_GAM_MMIOH_OVERLAY_CONFIG0_BASE_MASK (			\
	is_uv(UV4A) ? 0x000ffffffc000000UL :				\
	is_uv(UV4) ? 0x00003ffffc000000UL :				\
	is_uv(UV3) ? 0x00003ffffc000000UL :				\
	0)
#define UVH_RH_GAM_MMIOH_OVERLAY_CONFIG0_BASE_SHFT (			\
	is_uv(UV4) ? 26 :						\
	is_uv(UV3) ? 26 :						\
	-1)

union uvh_rh_gam_mmioh_overlay_config0_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_mmioh_overlay_config0_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_mmioh_overlay_config0_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} sx;

	/* UV4A unique struct */
	struct uv4ah_rh_gam_mmioh_overlay_config0_mmr_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:26;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	undef_62:1;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s4a;

	/* UV4 unique struct */
	struct uv4h_rh_gam_mmioh_overlay_config0_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_mmioh_overlay_config0_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s3;
};

/* ========================================================================= */
/*                     UVH_RH_GAM_MMIOH_OVERLAY_CONFIG1                      */
/* ========================================================================= */
#define UVH_RH_GAM_MMIOH_OVERLAY_CONFIG1 (				\
	is_uv(UV4) ? 0x484000UL :					\
	is_uv(UV3) ? 0x1604000UL :					\
	0)

/* UV4A unique defines */
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG1_BASE_SHFT	26
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG1_BASE_MASK	0x000ffffffc000000UL
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG1_M_IO_SHFT	52
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG1_M_IO_MASK	0x03f0000000000000UL
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG1_ENABLE_SHFT	63
#define UV4AH_RH_GAM_MMIOH_OVERLAY_CONFIG1_ENABLE_MASK	0x8000000000000000UL

/* UV4 unique defines */
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG1_BASE_SHFT	26
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG1_BASE_MASK	0x00003ffffc000000UL
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG1_M_IO_SHFT	46
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG1_M_IO_MASK	0x000fc00000000000UL
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG1_ENABLE_SHFT	63
#define UV4H_RH_GAM_MMIOH_OVERLAY_CONFIG1_ENABLE_MASK	0x8000000000000000UL

/* UV3 unique defines */
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_BASE_SHFT	26
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_BASE_MASK	0x00003ffffc000000UL
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_M_IO_SHFT	46
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_M_IO_MASK	0x000fc00000000000UL
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_ENABLE_SHFT	63
#define UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_ENABLE_MASK	0x8000000000000000UL

#define UVH_RH_GAM_MMIOH_OVERLAY_CONFIG1_BASE_MASK (			\
	is_uv(UV4A) ? 0x000ffffffc000000UL : \
	is_uv(UV4) ? 0x00003ffffc000000UL :				\
	is_uv(UV3) ? 0x00003ffffc000000UL :				\
	0)
#define UVH_RH_GAM_MMIOH_OVERLAY_CONFIG1_BASE_SHFT (			\
	is_uv(UV4) ? 26 :						\
	is_uv(UV3) ? 26 :						\
	-1)

union uvh_rh_gam_mmioh_overlay_config1_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_mmioh_overlay_config1_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_mmioh_overlay_config1_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} sx;

	/* UV4A unique struct */
	struct uv4ah_rh_gam_mmioh_overlay_config1_mmr_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:26;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	undef_62:1;			/* Undefined */
		unsigned long	enable:1;			/* RW */
	} s4a;

	/* UV4 unique struct */
	struct uv4h_rh_gam_mmioh_overlay_config1_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_mmioh_overlay_config1_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	m_io:6;				/* RW */
		unsigned long	n_io:4;
		unsigned long	rsvd_56_62:7;
		unsigned long	enable:1;			/* RW */
	} s3;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_MMIOH_REDIRECT_CONFIG0                      */
/* ========================================================================= */
#define UVH_RH_GAM_MMIOH_REDIRECT_CONFIG0 (				\
	is_uv(UV4) ? 0x483800UL :					\
	is_uv(UV3) ? 0x1603800UL :					\
	0)

#define UVH_RH_GAM_MMIOH_REDIRECT_CONFIG0_DEPTH (			\
	is_uv(UV4) ? 128 :						\
	is_uv(UV3) ? 128 :						\
	0)

/* UV4A unique defines */
#define UV4AH_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_SHFT	0
#define UV4AH_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_MASK	0x0000000000000fffUL

/* UV4 unique defines */
#define UV4H_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_SHFT	0
#define UV4H_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_MASK	0x0000000000007fffUL

/* UV3 unique defines */
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_SHFT	0
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_MASK	0x0000000000007fffUL

/* UVH common defines */
#define UVH_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_MASK (			\
	is_uv(UV4A) ? UV4AH_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_MASK :	\
	is_uv(UV4)  ?  UV4H_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_MASK :	\
	is_uv(UV3)  ?  UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_NASID_MASK :	\
	0)


union uvh_rh_gam_mmioh_redirect_config0_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_mmioh_redirect_config0_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_mmioh_redirect_config0_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} sx;

	struct uv4ah_rh_gam_mmioh_redirect_config0_s {
		unsigned long	nasid:12;			/* RW */
		unsigned long	rsvd_12_63:52;
	} s4a;

	/* UV4 unique struct */
	struct uv4h_rh_gam_mmioh_redirect_config0_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_mmioh_redirect_config0_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} s3;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_MMIOH_REDIRECT_CONFIG1                      */
/* ========================================================================= */
#define UVH_RH_GAM_MMIOH_REDIRECT_CONFIG1 (				\
	is_uv(UV4) ? 0x484800UL :					\
	is_uv(UV3) ? 0x1604800UL :					\
	0)

#define UVH_RH_GAM_MMIOH_REDIRECT_CONFIG1_DEPTH (			\
	is_uv(UV4) ? 128 :						\
	is_uv(UV3) ? 128 :						\
	0)

/* UV4A unique defines */
#define UV4AH_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_SHFT	0
#define UV4AH_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_MASK	0x0000000000000fffUL

/* UV4 unique defines */
#define UV4H_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_SHFT	0
#define UV4H_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_MASK	0x0000000000007fffUL

/* UV3 unique defines */
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_SHFT	0
#define UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_MASK	0x0000000000007fffUL

/* UVH common defines */
#define UVH_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_MASK (			\
	is_uv(UV4A) ? UV4AH_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_MASK :	\
	is_uv(UV4)  ?  UV4H_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_MASK :	\
	is_uv(UV3)  ?  UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG1_NASID_MASK :	\
	0)


union uvh_rh_gam_mmioh_redirect_config1_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_mmioh_redirect_config1_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_mmioh_redirect_config1_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} sx;

	struct uv4ah_rh_gam_mmioh_redirect_config1_s {
		unsigned long	nasid:12;			/* RW */
		unsigned long	rsvd_12_63:52;
	} s4a;

	/* UV4 unique struct */
	struct uv4h_rh_gam_mmioh_redirect_config1_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_mmioh_redirect_config1_s {
		unsigned long	nasid:15;			/* RW */
		unsigned long	rsvd_15_63:49;
	} s3;
};

/* ========================================================================= */
/*                      UVH_RH_GAM_MMR_OVERLAY_CONFIG                        */
/* ========================================================================= */
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG (					\
	is_uv(UV4) ? 0x480028UL :					\
	is_uv(UV3) ? 0x1600028UL :					\
	is_uv(UV2) ? 0x1600028UL :					\
	0)


/* UVXH common defines */
#define UVXH_RH_GAM_MMR_OVERLAY_CONFIG_BASE_SHFT	26
#define UVXH_RH_GAM_MMR_OVERLAY_CONFIG_BASE_MASK (			\
	is_uv(UV4A) ? 0x000ffffffc000000UL :				\
	is_uv(UV4) ? 0x00003ffffc000000UL :				\
	is_uv(UV3) ? 0x00003ffffc000000UL :				\
	is_uv(UV2) ? 0x00003ffffc000000UL :				\
	0)
#define UVXH_RH_GAM_MMR_OVERLAY_CONFIG_ENABLE_SHFT	63
#define UVXH_RH_GAM_MMR_OVERLAY_CONFIG_ENABLE_MASK	0x8000000000000000UL

/* UV4A unique defines */
#define UV4AH_RH_GAM_GRU_OVERLAY_CONFIG_BASE_SHFT	26
#define UV4AH_RH_GAM_GRU_OVERLAY_CONFIG_BASE_MASK	0x000ffffffc000000UL

#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_BASE_MASK (			\
	is_uv(UV4A) ? 0x000ffffffc000000UL :				\
	is_uv(UV4) ? 0x00003ffffc000000UL :				\
	is_uv(UV3) ? 0x00003ffffc000000UL :				\
	is_uv(UV2) ? 0x00003ffffc000000UL :				\
	0)

#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_BASE_SHFT (			\
	is_uv(UV4) ? 26 :						\
	is_uv(UV3) ? 26 :						\
	is_uv(UV2) ? 26 :						\
	-1)

union uvh_rh_gam_mmr_overlay_config_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rh_gam_mmr_overlay_config_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_62:17;
		unsigned long	enable:1;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_rh_gam_mmr_overlay_config_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_62:17;
		unsigned long	enable:1;			/* RW */
	} sx;

	/* UV4 unique struct */
	struct uv4h_rh_gam_mmr_overlay_config_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_62:17;
		unsigned long	enable:1;			/* RW */
	} s4;

	/* UV3 unique struct */
	struct uv3h_rh_gam_mmr_overlay_config_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_62:17;
		unsigned long	enable:1;			/* RW */
	} s3;

	/* UV2 unique struct */
	struct uv2h_rh_gam_mmr_overlay_config_s {
		unsigned long	rsvd_0_25:26;
		unsigned long	base:20;			/* RW */
		unsigned long	rsvd_46_62:17;
		unsigned long	enable:1;			/* RW */
	} s2;
};

/* ========================================================================= */
/*                                 UVH_RTC                                   */
/* ========================================================================= */
#define UVH_RTC (							\
	is_uv(UV5) ? 0xe0000UL :					\
	is_uv(UV4) ? 0xe0000UL :					\
	is_uv(UV3) ? 0x340000UL :					\
	is_uv(UV2) ? 0x340000UL :					\
	0)

/* UVH common defines*/
#define UVH_RTC_REAL_TIME_CLOCK_SHFT			0
#define UVH_RTC_REAL_TIME_CLOCK_MASK			0x00ffffffffffffffUL


union uvh_rtc_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_rtc_s {
		unsigned long	real_time_clock:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s;

	/* UV5 unique struct */
	struct uv5h_rtc_s {
		unsigned long	real_time_clock:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s5;

	/* UV4 unique struct */
	struct uv4h_rtc_s {
		unsigned long	real_time_clock:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s4;

	/* UV3 unique struct */
	struct uv3h_rtc_s {
		unsigned long	real_time_clock:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s3;

	/* UV2 unique struct */
	struct uv2h_rtc_s {
		unsigned long	real_time_clock:56;		/* RW */
		unsigned long	rsvd_56_63:8;
	} s2;
};

/* ========================================================================= */
/*                           UVH_RTC1_INT_CONFIG                             */
/* ========================================================================= */
#define UVH_RTC1_INT_CONFIG 0x615c0UL

/* UVH common defines*/
#define UVH_RTC1_INT_CONFIG_VECTOR_SHFT			0
#define UVH_RTC1_INT_CONFIG_VECTOR_MASK			0x00000000000000ffUL
#define UVH_RTC1_INT_CONFIG_DM_SHFT			8
#define UVH_RTC1_INT_CONFIG_DM_MASK			0x0000000000000700UL
#define UVH_RTC1_INT_CONFIG_DESTMODE_SHFT		11
#define UVH_RTC1_INT_CONFIG_DESTMODE_MASK		0x0000000000000800UL
#define UVH_RTC1_INT_CONFIG_STATUS_SHFT			12
#define UVH_RTC1_INT_CONFIG_STATUS_MASK			0x0000000000001000UL
#define UVH_RTC1_INT_CONFIG_P_SHFT			13
#define UVH_RTC1_INT_CONFIG_P_MASK			0x0000000000002000UL
#define UVH_RTC1_INT_CONFIG_T_SHFT			15
#define UVH_RTC1_INT_CONFIG_T_MASK			0x0000000000008000UL
#define UVH_RTC1_INT_CONFIG_M_SHFT			16
#define UVH_RTC1_INT_CONFIG_M_MASK			0x0000000000010000UL
#define UVH_RTC1_INT_CONFIG_APIC_ID_SHFT		32
#define UVH_RTC1_INT_CONFIG_APIC_ID_MASK		0xffffffff00000000UL


union uvh_rtc1_int_config_u {
	unsigned long	v;

	/* UVH common struct */
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

	/* UV5 unique struct */
	struct uv5h_rtc1_int_config_s {
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
	} s5;

	/* UV4 unique struct */
	struct uv4h_rtc1_int_config_s {
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
	} s4;

	/* UV3 unique struct */
	struct uv3h_rtc1_int_config_s {
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
	} s3;

	/* UV2 unique struct */
	struct uv2h_rtc1_int_config_s {
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
	} s2;
};

/* ========================================================================= */
/*                               UVH_SCRATCH5                                */
/* ========================================================================= */
#define UVH_SCRATCH5 (							\
	is_uv(UV5) ? 0xb0200UL :					\
	is_uv(UV4) ? 0xb0200UL :					\
	is_uv(UV3) ? 0x2d0200UL :					\
	is_uv(UV2) ? 0x2d0200UL :					\
	0)
#define UV5H_SCRATCH5 0xb0200UL
#define UV4H_SCRATCH5 0xb0200UL
#define UV3H_SCRATCH5 0x2d0200UL
#define UV2H_SCRATCH5 0x2d0200UL

/* UVH common defines*/
#define UVH_SCRATCH5_SCRATCH5_SHFT			0
#define UVH_SCRATCH5_SCRATCH5_MASK			0xffffffffffffffffUL

/* UVXH common defines */
#define UVXH_SCRATCH5_SCRATCH5_SHFT			0
#define UVXH_SCRATCH5_SCRATCH5_MASK			0xffffffffffffffffUL

/* UVYH common defines */
#define UVYH_SCRATCH5_SCRATCH5_SHFT			0
#define UVYH_SCRATCH5_SCRATCH5_MASK			0xffffffffffffffffUL

/* UV5 unique defines */
#define UV5H_SCRATCH5_SCRATCH5_SHFT			0
#define UV5H_SCRATCH5_SCRATCH5_MASK			0xffffffffffffffffUL

/* UV4 unique defines */
#define UV4H_SCRATCH5_SCRATCH5_SHFT			0
#define UV4H_SCRATCH5_SCRATCH5_MASK			0xffffffffffffffffUL

/* UV3 unique defines */
#define UV3H_SCRATCH5_SCRATCH5_SHFT			0
#define UV3H_SCRATCH5_SCRATCH5_MASK			0xffffffffffffffffUL

/* UV2 unique defines */
#define UV2H_SCRATCH5_SCRATCH5_SHFT			0
#define UV2H_SCRATCH5_SCRATCH5_MASK			0xffffffffffffffffUL


union uvh_scratch5_u {
	unsigned long	v;

	/* UVH common struct */
	struct uvh_scratch5_s {
		unsigned long	scratch5:64;			/* RW */
	} s;

	/* UVXH common struct */
	struct uvxh_scratch5_s {
		unsigned long	scratch5:64;			/* RW */
	} sx;

	/* UVYH common struct */
	struct uvyh_scratch5_s {
		unsigned long	scratch5:64;			/* RW */
	} sy;

	/* UV5 unique struct */
	struct uv5h_scratch5_s {
		unsigned long	scratch5:64;			/* RW */
	} s5;

	/* UV4 unique struct */
	struct uv4h_scratch5_s {
		unsigned long	scratch5:64;			/* RW */
	} s4;

	/* UV3 unique struct */
	struct uv3h_scratch5_s {
		unsigned long	scratch5:64;			/* RW */
	} s3;

	/* UV2 unique struct */
	struct uv2h_scratch5_s {
		unsigned long	scratch5:64;			/* RW */
	} s2;
};

/* ========================================================================= */
/*                            UVH_SCRATCH5_ALIAS                             */
/* ========================================================================= */
#define UVH_SCRATCH5_ALIAS (						\
	is_uv(UV5) ? 0xb0208UL :					\
	is_uv(UV4) ? 0xb0208UL :					\
	is_uv(UV3) ? 0x2d0208UL :					\
	is_uv(UV2) ? 0x2d0208UL :					\
	0)
#define UV5H_SCRATCH5_ALIAS 0xb0208UL
#define UV4H_SCRATCH5_ALIAS 0xb0208UL
#define UV3H_SCRATCH5_ALIAS 0x2d0208UL
#define UV2H_SCRATCH5_ALIAS 0x2d0208UL


/* ========================================================================= */
/*                           UVH_SCRATCH5_ALIAS_2                            */
/* ========================================================================= */
#define UVH_SCRATCH5_ALIAS_2 (						\
	is_uv(UV5) ? 0xb0210UL :					\
	is_uv(UV4) ? 0xb0210UL :					\
	is_uv(UV3) ? 0x2d0210UL :					\
	is_uv(UV2) ? 0x2d0210UL :					\
	0)
#define UV5H_SCRATCH5_ALIAS_2 0xb0210UL
#define UV4H_SCRATCH5_ALIAS_2 0xb0210UL
#define UV3H_SCRATCH5_ALIAS_2 0x2d0210UL
#define UV2H_SCRATCH5_ALIAS_2 0x2d0210UL



#endif /* _ASM_X86_UV_UV_MMRS_H */
