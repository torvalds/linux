/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV MMR definitions
 *
 * Copyright (C) 2007-2008 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef __ASM_IA64_UV_MMRS__
#define __ASM_IA64_UV_MMRS__

#define UV_MMR_ENABLE		(1UL << 63)

/* ========================================================================= */
/*                           UVH_BAU_DATA_CONFIG                             */
/* ========================================================================= */
#define UVH_BAU_DATA_CONFIG 0x61680UL
#define UVH_BAU_DATA_CONFIG_32 0x0438

#define UVH_BAU_DATA_CONFIG_VECTOR_SHFT 0
#define UVH_BAU_DATA_CONFIG_VECTOR_MASK 0x00000000000000ffUL
#define UVH_BAU_DATA_CONFIG_DM_SHFT 8
#define UVH_BAU_DATA_CONFIG_DM_MASK 0x0000000000000700UL
#define UVH_BAU_DATA_CONFIG_DESTMODE_SHFT 11
#define UVH_BAU_DATA_CONFIG_DESTMODE_MASK 0x0000000000000800UL
#define UVH_BAU_DATA_CONFIG_STATUS_SHFT 12
#define UVH_BAU_DATA_CONFIG_STATUS_MASK 0x0000000000001000UL
#define UVH_BAU_DATA_CONFIG_P_SHFT 13
#define UVH_BAU_DATA_CONFIG_P_MASK 0x0000000000002000UL
#define UVH_BAU_DATA_CONFIG_T_SHFT 15
#define UVH_BAU_DATA_CONFIG_T_MASK 0x0000000000008000UL
#define UVH_BAU_DATA_CONFIG_M_SHFT 16
#define UVH_BAU_DATA_CONFIG_M_MASK 0x0000000000010000UL
#define UVH_BAU_DATA_CONFIG_APIC_ID_SHFT 32
#define UVH_BAU_DATA_CONFIG_APIC_ID_MASK 0xffffffff00000000UL

union uvh_bau_data_config_u {
    unsigned long	v;
    struct uvh_bau_data_config_s {
	unsigned long	vector_  :  8;  /* RW */
	unsigned long	dm       :  3;  /* RW */
	unsigned long	destmode :  1;  /* RW */
	unsigned long	status   :  1;  /* RO */
	unsigned long	p        :  1;  /* RO */
	unsigned long	rsvd_14  :  1;  /*    */
	unsigned long	t        :  1;  /* RO */
	unsigned long	m        :  1;  /* RW */
	unsigned long	rsvd_17_31: 15;  /*    */
	unsigned long	apic_id  : 32;  /* RW */
    } s;
};

/* ========================================================================= */
/*                           UVH_EVENT_OCCURRED0                             */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED0 0x70000UL
#define UVH_EVENT_OCCURRED0_32 0x005e8

#define UVH_EVENT_OCCURRED0_LB_HCERR_SHFT 0
#define UVH_EVENT_OCCURRED0_LB_HCERR_MASK 0x0000000000000001UL
#define UVH_EVENT_OCCURRED0_GR0_HCERR_SHFT 1
#define UVH_EVENT_OCCURRED0_GR0_HCERR_MASK 0x0000000000000002UL
#define UVH_EVENT_OCCURRED0_GR1_HCERR_SHFT 2
#define UVH_EVENT_OCCURRED0_GR1_HCERR_MASK 0x0000000000000004UL
#define UVH_EVENT_OCCURRED0_LH_HCERR_SHFT 3
#define UVH_EVENT_OCCURRED0_LH_HCERR_MASK 0x0000000000000008UL
#define UVH_EVENT_OCCURRED0_RH_HCERR_SHFT 4
#define UVH_EVENT_OCCURRED0_RH_HCERR_MASK 0x0000000000000010UL
#define UVH_EVENT_OCCURRED0_XN_HCERR_SHFT 5
#define UVH_EVENT_OCCURRED0_XN_HCERR_MASK 0x0000000000000020UL
#define UVH_EVENT_OCCURRED0_SI_HCERR_SHFT 6
#define UVH_EVENT_OCCURRED0_SI_HCERR_MASK 0x0000000000000040UL
#define UVH_EVENT_OCCURRED0_LB_AOERR0_SHFT 7
#define UVH_EVENT_OCCURRED0_LB_AOERR0_MASK 0x0000000000000080UL
#define UVH_EVENT_OCCURRED0_GR0_AOERR0_SHFT 8
#define UVH_EVENT_OCCURRED0_GR0_AOERR0_MASK 0x0000000000000100UL
#define UVH_EVENT_OCCURRED0_GR1_AOERR0_SHFT 9
#define UVH_EVENT_OCCURRED0_GR1_AOERR0_MASK 0x0000000000000200UL
#define UVH_EVENT_OCCURRED0_LH_AOERR0_SHFT 10
#define UVH_EVENT_OCCURRED0_LH_AOERR0_MASK 0x0000000000000400UL
#define UVH_EVENT_OCCURRED0_RH_AOERR0_SHFT 11
#define UVH_EVENT_OCCURRED0_RH_AOERR0_MASK 0x0000000000000800UL
#define UVH_EVENT_OCCURRED0_XN_AOERR0_SHFT 12
#define UVH_EVENT_OCCURRED0_XN_AOERR0_MASK 0x0000000000001000UL
#define UVH_EVENT_OCCURRED0_SI_AOERR0_SHFT 13
#define UVH_EVENT_OCCURRED0_SI_AOERR0_MASK 0x0000000000002000UL
#define UVH_EVENT_OCCURRED0_LB_AOERR1_SHFT 14
#define UVH_EVENT_OCCURRED0_LB_AOERR1_MASK 0x0000000000004000UL
#define UVH_EVENT_OCCURRED0_GR0_AOERR1_SHFT 15
#define UVH_EVENT_OCCURRED0_GR0_AOERR1_MASK 0x0000000000008000UL
#define UVH_EVENT_OCCURRED0_GR1_AOERR1_SHFT 16
#define UVH_EVENT_OCCURRED0_GR1_AOERR1_MASK 0x0000000000010000UL
#define UVH_EVENT_OCCURRED0_LH_AOERR1_SHFT 17
#define UVH_EVENT_OCCURRED0_LH_AOERR1_MASK 0x0000000000020000UL
#define UVH_EVENT_OCCURRED0_RH_AOERR1_SHFT 18
#define UVH_EVENT_OCCURRED0_RH_AOERR1_MASK 0x0000000000040000UL
#define UVH_EVENT_OCCURRED0_XN_AOERR1_SHFT 19
#define UVH_EVENT_OCCURRED0_XN_AOERR1_MASK 0x0000000000080000UL
#define UVH_EVENT_OCCURRED0_SI_AOERR1_SHFT 20
#define UVH_EVENT_OCCURRED0_SI_AOERR1_MASK 0x0000000000100000UL
#define UVH_EVENT_OCCURRED0_RH_VPI_INT_SHFT 21
#define UVH_EVENT_OCCURRED0_RH_VPI_INT_MASK 0x0000000000200000UL
#define UVH_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_SHFT 22
#define UVH_EVENT_OCCURRED0_SYSTEM_SHUTDOWN_INT_MASK 0x0000000000400000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_0_SHFT 23
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_0_MASK 0x0000000000800000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_1_SHFT 24
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_1_MASK 0x0000000001000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_2_SHFT 25
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_2_MASK 0x0000000002000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_3_SHFT 26
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_3_MASK 0x0000000004000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_4_SHFT 27
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_4_MASK 0x0000000008000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_5_SHFT 28
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_5_MASK 0x0000000010000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_6_SHFT 29
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_6_MASK 0x0000000020000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_7_SHFT 30
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_7_MASK 0x0000000040000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_8_SHFT 31
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_8_MASK 0x0000000080000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_9_SHFT 32
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_9_MASK 0x0000000100000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_10_SHFT 33
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_10_MASK 0x0000000200000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_11_SHFT 34
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_11_MASK 0x0000000400000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_12_SHFT 35
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_12_MASK 0x0000000800000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_13_SHFT 36
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_13_MASK 0x0000001000000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_14_SHFT 37
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_14_MASK 0x0000002000000000UL
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_15_SHFT 38
#define UVH_EVENT_OCCURRED0_LB_IRQ_INT_15_MASK 0x0000004000000000UL
#define UVH_EVENT_OCCURRED0_L1_NMI_INT_SHFT 39
#define UVH_EVENT_OCCURRED0_L1_NMI_INT_MASK 0x0000008000000000UL
#define UVH_EVENT_OCCURRED0_STOP_CLOCK_SHFT 40
#define UVH_EVENT_OCCURRED0_STOP_CLOCK_MASK 0x0000010000000000UL
#define UVH_EVENT_OCCURRED0_ASIC_TO_L1_SHFT 41
#define UVH_EVENT_OCCURRED0_ASIC_TO_L1_MASK 0x0000020000000000UL
#define UVH_EVENT_OCCURRED0_L1_TO_ASIC_SHFT 42
#define UVH_EVENT_OCCURRED0_L1_TO_ASIC_MASK 0x0000040000000000UL
#define UVH_EVENT_OCCURRED0_LTC_INT_SHFT 43
#define UVH_EVENT_OCCURRED0_LTC_INT_MASK 0x0000080000000000UL
#define UVH_EVENT_OCCURRED0_LA_SEQ_TRIGGER_SHFT 44
#define UVH_EVENT_OCCURRED0_LA_SEQ_TRIGGER_MASK 0x0000100000000000UL
#define UVH_EVENT_OCCURRED0_IPI_INT_SHFT 45
#define UVH_EVENT_OCCURRED0_IPI_INT_MASK 0x0000200000000000UL
#define UVH_EVENT_OCCURRED0_EXTIO_INT0_SHFT 46
#define UVH_EVENT_OCCURRED0_EXTIO_INT0_MASK 0x0000400000000000UL
#define UVH_EVENT_OCCURRED0_EXTIO_INT1_SHFT 47
#define UVH_EVENT_OCCURRED0_EXTIO_INT1_MASK 0x0000800000000000UL
#define UVH_EVENT_OCCURRED0_EXTIO_INT2_SHFT 48
#define UVH_EVENT_OCCURRED0_EXTIO_INT2_MASK 0x0001000000000000UL
#define UVH_EVENT_OCCURRED0_EXTIO_INT3_SHFT 49
#define UVH_EVENT_OCCURRED0_EXTIO_INT3_MASK 0x0002000000000000UL
#define UVH_EVENT_OCCURRED0_PROFILE_INT_SHFT 50
#define UVH_EVENT_OCCURRED0_PROFILE_INT_MASK 0x0004000000000000UL
#define UVH_EVENT_OCCURRED0_RTC0_SHFT 51
#define UVH_EVENT_OCCURRED0_RTC0_MASK 0x0008000000000000UL
#define UVH_EVENT_OCCURRED0_RTC1_SHFT 52
#define UVH_EVENT_OCCURRED0_RTC1_MASK 0x0010000000000000UL
#define UVH_EVENT_OCCURRED0_RTC2_SHFT 53
#define UVH_EVENT_OCCURRED0_RTC2_MASK 0x0020000000000000UL
#define UVH_EVENT_OCCURRED0_RTC3_SHFT 54
#define UVH_EVENT_OCCURRED0_RTC3_MASK 0x0040000000000000UL
#define UVH_EVENT_OCCURRED0_BAU_DATA_SHFT 55
#define UVH_EVENT_OCCURRED0_BAU_DATA_MASK 0x0080000000000000UL
#define UVH_EVENT_OCCURRED0_POWER_MANAGEMENT_REQ_SHFT 56
#define UVH_EVENT_OCCURRED0_POWER_MANAGEMENT_REQ_MASK 0x0100000000000000UL
union uvh_event_occurred0_u {
    unsigned long	v;
    struct uvh_event_occurred0_s {
	unsigned long	lb_hcerr             :  1;  /* RW, W1C */
	unsigned long	gr0_hcerr            :  1;  /* RW, W1C */
	unsigned long	gr1_hcerr            :  1;  /* RW, W1C */
	unsigned long	lh_hcerr             :  1;  /* RW, W1C */
	unsigned long	rh_hcerr             :  1;  /* RW, W1C */
	unsigned long	xn_hcerr             :  1;  /* RW, W1C */
	unsigned long	si_hcerr             :  1;  /* RW, W1C */
	unsigned long	lb_aoerr0            :  1;  /* RW, W1C */
	unsigned long	gr0_aoerr0           :  1;  /* RW, W1C */
	unsigned long	gr1_aoerr0           :  1;  /* RW, W1C */
	unsigned long	lh_aoerr0            :  1;  /* RW, W1C */
	unsigned long	rh_aoerr0            :  1;  /* RW, W1C */
	unsigned long	xn_aoerr0            :  1;  /* RW, W1C */
	unsigned long	si_aoerr0            :  1;  /* RW, W1C */
	unsigned long	lb_aoerr1            :  1;  /* RW, W1C */
	unsigned long	gr0_aoerr1           :  1;  /* RW, W1C */
	unsigned long	gr1_aoerr1           :  1;  /* RW, W1C */
	unsigned long	lh_aoerr1            :  1;  /* RW, W1C */
	unsigned long	rh_aoerr1            :  1;  /* RW, W1C */
	unsigned long	xn_aoerr1            :  1;  /* RW, W1C */
	unsigned long	si_aoerr1            :  1;  /* RW, W1C */
	unsigned long	rh_vpi_int           :  1;  /* RW, W1C */
	unsigned long	system_shutdown_int  :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_0         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_1         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_2         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_3         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_4         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_5         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_6         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_7         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_8         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_9         :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_10        :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_11        :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_12        :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_13        :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_14        :  1;  /* RW, W1C */
	unsigned long	lb_irq_int_15        :  1;  /* RW, W1C */
	unsigned long	l1_nmi_int           :  1;  /* RW, W1C */
	unsigned long	stop_clock           :  1;  /* RW, W1C */
	unsigned long	asic_to_l1           :  1;  /* RW, W1C */
	unsigned long	l1_to_asic           :  1;  /* RW, W1C */
	unsigned long	ltc_int              :  1;  /* RW, W1C */
	unsigned long	la_seq_trigger       :  1;  /* RW, W1C */
	unsigned long	ipi_int              :  1;  /* RW, W1C */
	unsigned long	extio_int0           :  1;  /* RW, W1C */
	unsigned long	extio_int1           :  1;  /* RW, W1C */
	unsigned long	extio_int2           :  1;  /* RW, W1C */
	unsigned long	extio_int3           :  1;  /* RW, W1C */
	unsigned long	profile_int          :  1;  /* RW, W1C */
	unsigned long	rtc0                 :  1;  /* RW, W1C */
	unsigned long	rtc1                 :  1;  /* RW, W1C */
	unsigned long	rtc2                 :  1;  /* RW, W1C */
	unsigned long	rtc3                 :  1;  /* RW, W1C */
	unsigned long	bau_data             :  1;  /* RW, W1C */
	unsigned long	power_management_req :  1;  /* RW, W1C */
	unsigned long	rsvd_57_63           :  7;  /*    */
    } s;
};

/* ========================================================================= */
/*                        UVH_EVENT_OCCURRED0_ALIAS                          */
/* ========================================================================= */
#define UVH_EVENT_OCCURRED0_ALIAS 0x0000000000070008UL
#define UVH_EVENT_OCCURRED0_ALIAS_32 0x005f0

/* ========================================================================= */
/*                               UVH_INT_CMPB                                */
/* ========================================================================= */
#define UVH_INT_CMPB 0x22080UL

#define UVH_INT_CMPB_REAL_TIME_CMPB_SHFT 0
#define UVH_INT_CMPB_REAL_TIME_CMPB_MASK 0x00ffffffffffffffUL

union uvh_int_cmpb_u {
    unsigned long	v;
    struct uvh_int_cmpb_s {
	unsigned long	real_time_cmpb : 56;  /* RW */
	unsigned long	rsvd_56_63     :  8;  /*    */
    } s;
};

/* ========================================================================= */
/*                               UVH_INT_CMPC                                */
/* ========================================================================= */
#define UVH_INT_CMPC 0x22100UL

#define UVH_INT_CMPC_REAL_TIME_CMPC_SHFT 0
#define UVH_INT_CMPC_REAL_TIME_CMPC_MASK 0x00ffffffffffffffUL

union uvh_int_cmpc_u {
    unsigned long	v;
    struct uvh_int_cmpc_s {
	unsigned long	real_time_cmpc : 56;  /* RW */
	unsigned long	rsvd_56_63     :  8;  /*    */
    } s;
};

/* ========================================================================= */
/*                               UVH_INT_CMPD                                */
/* ========================================================================= */
#define UVH_INT_CMPD 0x22180UL

#define UVH_INT_CMPD_REAL_TIME_CMPD_SHFT 0
#define UVH_INT_CMPD_REAL_TIME_CMPD_MASK 0x00ffffffffffffffUL

union uvh_int_cmpd_u {
    unsigned long	v;
    struct uvh_int_cmpd_s {
	unsigned long	real_time_cmpd : 56;  /* RW */
	unsigned long	rsvd_56_63     :  8;  /*    */
    } s;
};

/* ========================================================================= */
/*                               UVH_NODE_ID                                 */
/* ========================================================================= */
#define UVH_NODE_ID 0x0UL

#define UVH_NODE_ID_FORCE1_SHFT 0
#define UVH_NODE_ID_FORCE1_MASK 0x0000000000000001UL
#define UVH_NODE_ID_MANUFACTURER_SHFT 1
#define UVH_NODE_ID_MANUFACTURER_MASK 0x0000000000000ffeUL
#define UVH_NODE_ID_PART_NUMBER_SHFT 12
#define UVH_NODE_ID_PART_NUMBER_MASK 0x000000000ffff000UL
#define UVH_NODE_ID_REVISION_SHFT 28
#define UVH_NODE_ID_REVISION_MASK 0x00000000f0000000UL
#define UVH_NODE_ID_NODE_ID_SHFT 32
#define UVH_NODE_ID_NODE_ID_MASK 0x00007fff00000000UL
#define UVH_NODE_ID_NODES_PER_BIT_SHFT 48
#define UVH_NODE_ID_NODES_PER_BIT_MASK 0x007f000000000000UL
#define UVH_NODE_ID_NI_PORT_SHFT 56
#define UVH_NODE_ID_NI_PORT_MASK 0x0f00000000000000UL

union uvh_node_id_u {
    unsigned long	v;
    struct uvh_node_id_s {
	unsigned long	force1        :  1;  /* RO */
	unsigned long	manufacturer  : 11;  /* RO */
	unsigned long	part_number   : 16;  /* RO */
	unsigned long	revision      :  4;  /* RO */
	unsigned long	node_id       : 15;  /* RW */
	unsigned long	rsvd_47       :  1;  /*    */
	unsigned long	nodes_per_bit :  7;  /* RW */
	unsigned long	rsvd_55       :  1;  /*    */
	unsigned long	ni_port       :  4;  /* RO */
	unsigned long	rsvd_60_63    :  4;  /*    */
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
	unsigned long	rsvd_0_23 : 24;  /*    */
	unsigned long	dest_base : 22;  /* RW */
	unsigned long	rsvd_46_63: 18;  /*    */
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
	unsigned long	rsvd_0_23 : 24;  /*    */
	unsigned long	dest_base : 22;  /* RW */
	unsigned long	rsvd_46_63: 18;  /*    */
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
	unsigned long	rsvd_0_23 : 24;  /*    */
	unsigned long	dest_base : 22;  /* RW */
	unsigned long	rsvd_46_63: 18;  /*    */
    } s;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR                      */
/* ========================================================================= */
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR 0x1600010UL

#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT 28
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_MASK 0x00003ffff0000000UL
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_GR4_SHFT 48
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_GR4_MASK 0x0001000000000000UL
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_SHFT 52
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_MASK 0x00f0000000000000UL
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_SHFT 63
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_MASK 0x8000000000000000UL

union uvh_rh_gam_gru_overlay_config_mmr_u {
    unsigned long	v;
    struct uvh_rh_gam_gru_overlay_config_mmr_s {
	unsigned long	rsvd_0_27: 28;  /*    */
	unsigned long	base   : 18;  /* RW */
	unsigned long	rsvd_46_47:  2;  /*    */
	unsigned long	gr4    :  1;  /* RW */
	unsigned long	rsvd_49_51:  3;  /*    */
	unsigned long	n_gru  :  4;  /* RW */
	unsigned long	rsvd_56_62:  7;  /*    */
	unsigned long	enable :  1;  /* RW */
    } s;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR                      */
/* ========================================================================= */
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR 0x1600028UL

#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT 26
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_MASK 0x00003ffffc000000UL
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_DUAL_HUB_SHFT 46
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_DUAL_HUB_MASK 0x0000400000000000UL
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_SHFT 63
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_MASK 0x8000000000000000UL

union uvh_rh_gam_mmr_overlay_config_mmr_u {
    unsigned long	v;
    struct uvh_rh_gam_mmr_overlay_config_mmr_s {
	unsigned long	rsvd_0_25: 26;  /*    */
	unsigned long	base     : 20;  /* RW */
	unsigned long	dual_hub :  1;  /* RW */
	unsigned long	rsvd_47_62: 16;  /*    */
	unsigned long	enable   :  1;  /* RW */
    } s;
};

/* ========================================================================= */
/*                                 UVH_RTC                                   */
/* ========================================================================= */
#define UVH_RTC 0x340000UL

#define UVH_RTC_REAL_TIME_CLOCK_SHFT 0
#define UVH_RTC_REAL_TIME_CLOCK_MASK 0x00ffffffffffffffUL

union uvh_rtc_u {
    unsigned long	v;
    struct uvh_rtc_s {
	unsigned long	real_time_clock : 56;  /* RW */
	unsigned long	rsvd_56_63      :  8;  /*    */
    } s;
};

/* ========================================================================= */
/*                           UVH_RTC1_INT_CONFIG                             */
/* ========================================================================= */
#define UVH_RTC1_INT_CONFIG 0x615c0UL

#define UVH_RTC1_INT_CONFIG_VECTOR_SHFT 0
#define UVH_RTC1_INT_CONFIG_VECTOR_MASK 0x00000000000000ffUL
#define UVH_RTC1_INT_CONFIG_DM_SHFT 8
#define UVH_RTC1_INT_CONFIG_DM_MASK 0x0000000000000700UL
#define UVH_RTC1_INT_CONFIG_DESTMODE_SHFT 11
#define UVH_RTC1_INT_CONFIG_DESTMODE_MASK 0x0000000000000800UL
#define UVH_RTC1_INT_CONFIG_STATUS_SHFT 12
#define UVH_RTC1_INT_CONFIG_STATUS_MASK 0x0000000000001000UL
#define UVH_RTC1_INT_CONFIG_P_SHFT 13
#define UVH_RTC1_INT_CONFIG_P_MASK 0x0000000000002000UL
#define UVH_RTC1_INT_CONFIG_T_SHFT 15
#define UVH_RTC1_INT_CONFIG_T_MASK 0x0000000000008000UL
#define UVH_RTC1_INT_CONFIG_M_SHFT 16
#define UVH_RTC1_INT_CONFIG_M_MASK 0x0000000000010000UL
#define UVH_RTC1_INT_CONFIG_APIC_ID_SHFT 32
#define UVH_RTC1_INT_CONFIG_APIC_ID_MASK 0xffffffff00000000UL

union uvh_rtc1_int_config_u {
    unsigned long	v;
    struct uvh_rtc1_int_config_s {
	unsigned long	vector_  :  8;  /* RW */
	unsigned long	dm       :  3;  /* RW */
	unsigned long	destmode :  1;  /* RW */
	unsigned long	status   :  1;  /* RO */
	unsigned long	p        :  1;  /* RO */
	unsigned long	rsvd_14  :  1;  /*    */
	unsigned long	t        :  1;  /* RO */
	unsigned long	m        :  1;  /* RW */
	unsigned long	rsvd_17_31: 15;  /*    */
	unsigned long	apic_id  : 32;  /* RW */
    } s;
};

/* ========================================================================= */
/*                           UVH_RTC2_INT_CONFIG                             */
/* ========================================================================= */
#define UVH_RTC2_INT_CONFIG 0x61600UL

#define UVH_RTC2_INT_CONFIG_VECTOR_SHFT 0
#define UVH_RTC2_INT_CONFIG_VECTOR_MASK 0x00000000000000ffUL
#define UVH_RTC2_INT_CONFIG_DM_SHFT 8
#define UVH_RTC2_INT_CONFIG_DM_MASK 0x0000000000000700UL
#define UVH_RTC2_INT_CONFIG_DESTMODE_SHFT 11
#define UVH_RTC2_INT_CONFIG_DESTMODE_MASK 0x0000000000000800UL
#define UVH_RTC2_INT_CONFIG_STATUS_SHFT 12
#define UVH_RTC2_INT_CONFIG_STATUS_MASK 0x0000000000001000UL
#define UVH_RTC2_INT_CONFIG_P_SHFT 13
#define UVH_RTC2_INT_CONFIG_P_MASK 0x0000000000002000UL
#define UVH_RTC2_INT_CONFIG_T_SHFT 15
#define UVH_RTC2_INT_CONFIG_T_MASK 0x0000000000008000UL
#define UVH_RTC2_INT_CONFIG_M_SHFT 16
#define UVH_RTC2_INT_CONFIG_M_MASK 0x0000000000010000UL
#define UVH_RTC2_INT_CONFIG_APIC_ID_SHFT 32
#define UVH_RTC2_INT_CONFIG_APIC_ID_MASK 0xffffffff00000000UL

union uvh_rtc2_int_config_u {
    unsigned long	v;
    struct uvh_rtc2_int_config_s {
	unsigned long	vector_  :  8;  /* RW */
	unsigned long	dm       :  3;  /* RW */
	unsigned long	destmode :  1;  /* RW */
	unsigned long	status   :  1;  /* RO */
	unsigned long	p        :  1;  /* RO */
	unsigned long	rsvd_14  :  1;  /*    */
	unsigned long	t        :  1;  /* RO */
	unsigned long	m        :  1;  /* RW */
	unsigned long	rsvd_17_31: 15;  /*    */
	unsigned long	apic_id  : 32;  /* RW */
    } s;
};

/* ========================================================================= */
/*                           UVH_RTC3_INT_CONFIG                             */
/* ========================================================================= */
#define UVH_RTC3_INT_CONFIG 0x61640UL

#define UVH_RTC3_INT_CONFIG_VECTOR_SHFT 0
#define UVH_RTC3_INT_CONFIG_VECTOR_MASK 0x00000000000000ffUL
#define UVH_RTC3_INT_CONFIG_DM_SHFT 8
#define UVH_RTC3_INT_CONFIG_DM_MASK 0x0000000000000700UL
#define UVH_RTC3_INT_CONFIG_DESTMODE_SHFT 11
#define UVH_RTC3_INT_CONFIG_DESTMODE_MASK 0x0000000000000800UL
#define UVH_RTC3_INT_CONFIG_STATUS_SHFT 12
#define UVH_RTC3_INT_CONFIG_STATUS_MASK 0x0000000000001000UL
#define UVH_RTC3_INT_CONFIG_P_SHFT 13
#define UVH_RTC3_INT_CONFIG_P_MASK 0x0000000000002000UL
#define UVH_RTC3_INT_CONFIG_T_SHFT 15
#define UVH_RTC3_INT_CONFIG_T_MASK 0x0000000000008000UL
#define UVH_RTC3_INT_CONFIG_M_SHFT 16
#define UVH_RTC3_INT_CONFIG_M_MASK 0x0000000000010000UL
#define UVH_RTC3_INT_CONFIG_APIC_ID_SHFT 32
#define UVH_RTC3_INT_CONFIG_APIC_ID_MASK 0xffffffff00000000UL

union uvh_rtc3_int_config_u {
    unsigned long	v;
    struct uvh_rtc3_int_config_s {
	unsigned long	vector_  :  8;  /* RW */
	unsigned long	dm       :  3;  /* RW */
	unsigned long	destmode :  1;  /* RW */
	unsigned long	status   :  1;  /* RO */
	unsigned long	p        :  1;  /* RO */
	unsigned long	rsvd_14  :  1;  /*    */
	unsigned long	t        :  1;  /* RO */
	unsigned long	m        :  1;  /* RW */
	unsigned long	rsvd_17_31: 15;  /*    */
	unsigned long	apic_id  : 32;  /* RW */
    } s;
};

/* ========================================================================= */
/*                            UVH_RTC_INC_RATIO                              */
/* ========================================================================= */
#define UVH_RTC_INC_RATIO 0x350000UL

#define UVH_RTC_INC_RATIO_FRACTION_SHFT 0
#define UVH_RTC_INC_RATIO_FRACTION_MASK 0x00000000000fffffUL
#define UVH_RTC_INC_RATIO_RATIO_SHFT 20
#define UVH_RTC_INC_RATIO_RATIO_MASK 0x0000000000700000UL

union uvh_rtc_inc_ratio_u {
    unsigned long	v;
    struct uvh_rtc_inc_ratio_s {
	unsigned long	fraction : 20;  /* RW */
	unsigned long	ratio    :  3;  /* RW */
	unsigned long	rsvd_23_63: 41;  /*    */
    } s;
};

/* ========================================================================= */
/*                          UVH_SI_ADDR_MAP_CONFIG                           */
/* ========================================================================= */
#define UVH_SI_ADDR_MAP_CONFIG 0xc80000UL

#define UVH_SI_ADDR_MAP_CONFIG_M_SKT_SHFT 0
#define UVH_SI_ADDR_MAP_CONFIG_M_SKT_MASK 0x000000000000003fUL
#define UVH_SI_ADDR_MAP_CONFIG_N_SKT_SHFT 8
#define UVH_SI_ADDR_MAP_CONFIG_N_SKT_MASK 0x0000000000000f00UL

union uvh_si_addr_map_config_u {
    unsigned long	v;
    struct uvh_si_addr_map_config_s {
	unsigned long	m_skt :  6;  /* RW */
	unsigned long	rsvd_6_7:  2;  /*    */
	unsigned long	n_skt :  4;  /* RW */
	unsigned long	rsvd_12_63: 52;  /*    */
    } s;
};

/* ========================================================================= */
/*                       UVH_SI_ALIAS0_OVERLAY_CONFIG                        */
/* ========================================================================= */
#define UVH_SI_ALIAS0_OVERLAY_CONFIG 0xc80008UL

#define UVH_SI_ALIAS0_OVERLAY_CONFIG_BASE_SHFT 24
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_BASE_MASK 0x00000000ff000000UL
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_M_ALIAS_SHFT 48
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_M_ALIAS_MASK 0x001f000000000000UL
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_ENABLE_SHFT 63
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_ENABLE_MASK 0x8000000000000000UL

union uvh_si_alias0_overlay_config_u {
    unsigned long	v;
    struct uvh_si_alias0_overlay_config_s {
	unsigned long	rsvd_0_23: 24;  /*    */
	unsigned long	base    :  8;  /* RW */
	unsigned long	rsvd_32_47: 16;  /*    */
	unsigned long	m_alias :  5;  /* RW */
	unsigned long	rsvd_53_62: 10;  /*    */
	unsigned long	enable  :  1;  /* RW */
    } s;
};

/* ========================================================================= */
/*                       UVH_SI_ALIAS1_OVERLAY_CONFIG                        */
/* ========================================================================= */
#define UVH_SI_ALIAS1_OVERLAY_CONFIG 0xc80010UL

#define UVH_SI_ALIAS1_OVERLAY_CONFIG_BASE_SHFT 24
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_BASE_MASK 0x00000000ff000000UL
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_M_ALIAS_SHFT 48
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_M_ALIAS_MASK 0x001f000000000000UL
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_ENABLE_SHFT 63
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_ENABLE_MASK 0x8000000000000000UL

union uvh_si_alias1_overlay_config_u {
    unsigned long	v;
    struct uvh_si_alias1_overlay_config_s {
	unsigned long	rsvd_0_23: 24;  /*    */
	unsigned long	base    :  8;  /* RW */
	unsigned long	rsvd_32_47: 16;  /*    */
	unsigned long	m_alias :  5;  /* RW */
	unsigned long	rsvd_53_62: 10;  /*    */
	unsigned long	enable  :  1;  /* RW */
    } s;
};

/* ========================================================================= */
/*                       UVH_SI_ALIAS2_OVERLAY_CONFIG                        */
/* ========================================================================= */
#define UVH_SI_ALIAS2_OVERLAY_CONFIG 0xc80018UL

#define UVH_SI_ALIAS2_OVERLAY_CONFIG_BASE_SHFT 24
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_BASE_MASK 0x00000000ff000000UL
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_M_ALIAS_SHFT 48
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_M_ALIAS_MASK 0x001f000000000000UL
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_ENABLE_SHFT 63
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_ENABLE_MASK 0x8000000000000000UL

union uvh_si_alias2_overlay_config_u {
    unsigned long	v;
    struct uvh_si_alias2_overlay_config_s {
	unsigned long	rsvd_0_23: 24;  /*    */
	unsigned long	base    :  8;  /* RW */
	unsigned long	rsvd_32_47: 16;  /*    */
	unsigned long	m_alias :  5;  /* RW */
	unsigned long	rsvd_53_62: 10;  /*    */
	unsigned long	enable  :  1;  /* RW */
    } s;
};


#endif /* __ASM_IA64_UV_MMRS__ */
