/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2012-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_EF10_REGS_H
#define EFX_EF10_REGS_H

/* EF10 hardware architecture definitions have a name prefix following
 * the format:
 *
 *     E<type>_<min-rev><max-rev>_
 *
 * The following <type> strings are used:
 *
 *             MMIO register  Host memory structure
 * -------------------------------------------------------------
 * Address     R
 * Bitfield    RF             SF
 * Enumerator  FE             SE
 *
 * <min-rev> is the first revision to which the definition applies:
 *
 *     D: Huntington A0
 *
 * If the definition has been changed or removed in later revisions
 * then <max-rev> is the last revision to which the definition applies;
 * otherwise it is "Z".
 */

/**************************************************************************
 *
 * EF10 registers and descriptors
 *
 **************************************************************************
 */

/* BIU_HW_REV_ID_REG:  */
#define	ER_DZ_BIU_HW_REV_ID 0x00000000
#define	ERF_DZ_HW_REV_ID_LBN 0
#define	ERF_DZ_HW_REV_ID_WIDTH 32

/* BIU_MC_SFT_STATUS_REG:  */
#define	ER_DZ_BIU_MC_SFT_STATUS 0x00000010
#define	ER_DZ_BIU_MC_SFT_STATUS_STEP 4
#define	ER_DZ_BIU_MC_SFT_STATUS_ROWS 8
#define	ERF_DZ_MC_SFT_STATUS_LBN 0
#define	ERF_DZ_MC_SFT_STATUS_WIDTH 32

/* BIU_INT_ISR_REG:  */
#define	ER_DZ_BIU_INT_ISR 0x00000090
#define	ERF_DZ_ISR_REG_LBN 0
#define	ERF_DZ_ISR_REG_WIDTH 32

/* MC_DB_LWRD_REG:  */
#define	ER_DZ_MC_DB_LWRD 0x00000200
#define	ERF_DZ_MC_DOORBELL_L_LBN 0
#define	ERF_DZ_MC_DOORBELL_L_WIDTH 32

/* MC_DB_HWRD_REG:  */
#define	ER_DZ_MC_DB_HWRD 0x00000204
#define	ERF_DZ_MC_DOORBELL_H_LBN 0
#define	ERF_DZ_MC_DOORBELL_H_WIDTH 32

/* EVQ_RPTR_REG:  */
#define	ER_DZ_EVQ_RPTR 0x00000400
#define	ER_DZ_EVQ_RPTR_STEP 8192
#define	ER_DZ_EVQ_RPTR_ROWS 2048
#define	ERF_DZ_EVQ_RPTR_VLD_LBN 15
#define	ERF_DZ_EVQ_RPTR_VLD_WIDTH 1
#define	ERF_DZ_EVQ_RPTR_LBN 0
#define	ERF_DZ_EVQ_RPTR_WIDTH 15

/* EVQ_TMR_REG:  */
#define	ER_DZ_EVQ_TMR 0x00000420
#define	ER_DZ_EVQ_TMR_STEP 8192
#define	ER_DZ_EVQ_TMR_ROWS 2048
#define	ERF_DZ_TC_TIMER_MODE_LBN 14
#define	ERF_DZ_TC_TIMER_MODE_WIDTH 2
#define	ERF_DZ_TC_TIMER_VAL_LBN 0
#define	ERF_DZ_TC_TIMER_VAL_WIDTH 14

/* RX_DESC_UPD_REG:  */
#define	ER_DZ_RX_DESC_UPD 0x00000830
#define	ER_DZ_RX_DESC_UPD_STEP 8192
#define	ER_DZ_RX_DESC_UPD_ROWS 2048
#define	ERF_DZ_RX_DESC_WPTR_LBN 0
#define	ERF_DZ_RX_DESC_WPTR_WIDTH 12

/* TX_DESC_UPD_REG:  */
#define	ER_DZ_TX_DESC_UPD 0x00000a10
#define	ER_DZ_TX_DESC_UPD_STEP 8192
#define	ER_DZ_TX_DESC_UPD_ROWS 2048
#define	ERF_DZ_RSVD_LBN 76
#define	ERF_DZ_RSVD_WIDTH 20
#define	ERF_DZ_TX_DESC_WPTR_LBN 64
#define	ERF_DZ_TX_DESC_WPTR_WIDTH 12
#define	ERF_DZ_TX_DESC_HWORD_LBN 32
#define	ERF_DZ_TX_DESC_HWORD_WIDTH 32
#define	ERF_DZ_TX_DESC_LWORD_LBN 0
#define	ERF_DZ_TX_DESC_LWORD_WIDTH 32

/* DRIVER_EV */
#define	ESF_DZ_DRV_CODE_LBN 60
#define	ESF_DZ_DRV_CODE_WIDTH 4
#define	ESF_DZ_DRV_SUB_CODE_LBN 56
#define	ESF_DZ_DRV_SUB_CODE_WIDTH 4
#define	ESE_DZ_DRV_TIMER_EV 3
#define	ESE_DZ_DRV_START_UP_EV 2
#define	ESE_DZ_DRV_WAKE_UP_EV 1
#define	ESF_DZ_DRV_SUB_DATA_LBN 0
#define	ESF_DZ_DRV_SUB_DATA_WIDTH 56
#define	ESF_DZ_DRV_EVQ_ID_LBN 0
#define	ESF_DZ_DRV_EVQ_ID_WIDTH 14
#define	ESF_DZ_DRV_TMR_ID_LBN 0
#define	ESF_DZ_DRV_TMR_ID_WIDTH 14

/* EVENT_ENTRY */
#define	ESF_DZ_EV_CODE_LBN 60
#define	ESF_DZ_EV_CODE_WIDTH 4
#define	ESE_DZ_EV_CODE_MCDI_EV 12
#define	ESE_DZ_EV_CODE_DRIVER_EV 5
#define	ESE_DZ_EV_CODE_TX_EV 2
#define	ESE_DZ_EV_CODE_RX_EV 0
#define	ESE_DZ_OTHER other
#define	ESF_DZ_EV_DATA_LBN 0
#define	ESF_DZ_EV_DATA_WIDTH 60

/* MC_EVENT */
#define	ESF_DZ_MC_CODE_LBN 60
#define	ESF_DZ_MC_CODE_WIDTH 4
#define	ESF_DZ_MC_OVERRIDE_HOLDOFF_LBN 59
#define	ESF_DZ_MC_OVERRIDE_HOLDOFF_WIDTH 1
#define	ESF_DZ_MC_DROP_EVENT_LBN 58
#define	ESF_DZ_MC_DROP_EVENT_WIDTH 1
#define	ESF_DZ_MC_SOFT_LBN 0
#define	ESF_DZ_MC_SOFT_WIDTH 58

/* RX_EVENT */
#define	ESF_DZ_RX_CODE_LBN 60
#define	ESF_DZ_RX_CODE_WIDTH 4
#define	ESF_DZ_RX_OVERRIDE_HOLDOFF_LBN 59
#define	ESF_DZ_RX_OVERRIDE_HOLDOFF_WIDTH 1
#define	ESF_DZ_RX_DROP_EVENT_LBN 58
#define	ESF_DZ_RX_DROP_EVENT_WIDTH 1
#define	ESF_DZ_RX_EV_RSVD2_LBN 54
#define	ESF_DZ_RX_EV_RSVD2_WIDTH 4
#define	ESF_DZ_RX_EV_SOFT2_LBN 52
#define	ESF_DZ_RX_EV_SOFT2_WIDTH 2
#define	ESF_DZ_RX_DSC_PTR_LBITS_LBN 48
#define	ESF_DZ_RX_DSC_PTR_LBITS_WIDTH 4
#define	ESF_DZ_RX_L4_CLASS_LBN 45
#define	ESF_DZ_RX_L4_CLASS_WIDTH 3
#define	ESE_DZ_L4_CLASS_RSVD7 7
#define	ESE_DZ_L4_CLASS_RSVD6 6
#define	ESE_DZ_L4_CLASS_RSVD5 5
#define	ESE_DZ_L4_CLASS_RSVD4 4
#define	ESE_DZ_L4_CLASS_RSVD3 3
#define	ESE_DZ_L4_CLASS_UDP 2
#define	ESE_DZ_L4_CLASS_TCP 1
#define	ESE_DZ_L4_CLASS_UNKNOWN 0
#define	ESF_DZ_RX_L3_CLASS_LBN 42
#define	ESF_DZ_RX_L3_CLASS_WIDTH 3
#define	ESE_DZ_L3_CLASS_RSVD7 7
#define	ESE_DZ_L3_CLASS_IP6_FRAG 6
#define	ESE_DZ_L3_CLASS_ARP 5
#define	ESE_DZ_L3_CLASS_IP4_FRAG 4
#define	ESE_DZ_L3_CLASS_FCOE 3
#define	ESE_DZ_L3_CLASS_IP6 2
#define	ESE_DZ_L3_CLASS_IP4 1
#define	ESE_DZ_L3_CLASS_UNKNOWN 0
#define	ESF_DZ_RX_ETH_TAG_CLASS_LBN 39
#define	ESF_DZ_RX_ETH_TAG_CLASS_WIDTH 3
#define	ESE_DZ_ETH_TAG_CLASS_RSVD7 7
#define	ESE_DZ_ETH_TAG_CLASS_RSVD6 6
#define	ESE_DZ_ETH_TAG_CLASS_RSVD5 5
#define	ESE_DZ_ETH_TAG_CLASS_RSVD4 4
#define	ESE_DZ_ETH_TAG_CLASS_RSVD3 3
#define	ESE_DZ_ETH_TAG_CLASS_VLAN2 2
#define	ESE_DZ_ETH_TAG_CLASS_VLAN1 1
#define	ESE_DZ_ETH_TAG_CLASS_NONE 0
#define	ESF_DZ_RX_ETH_BASE_CLASS_LBN 36
#define	ESF_DZ_RX_ETH_BASE_CLASS_WIDTH 3
#define	ESE_DZ_ETH_BASE_CLASS_LLC_SNAP 2
#define	ESE_DZ_ETH_BASE_CLASS_LLC 1
#define	ESE_DZ_ETH_BASE_CLASS_ETH2 0
#define	ESF_DZ_RX_MAC_CLASS_LBN 35
#define	ESF_DZ_RX_MAC_CLASS_WIDTH 1
#define	ESE_DZ_MAC_CLASS_MCAST 1
#define	ESE_DZ_MAC_CLASS_UCAST 0
#define	ESF_DZ_RX_EV_SOFT1_LBN 32
#define	ESF_DZ_RX_EV_SOFT1_WIDTH 3
#define	ESF_DZ_RX_EV_RSVD1_LBN 31
#define	ESF_DZ_RX_EV_RSVD1_WIDTH 1
#define	ESF_DZ_RX_ABORT_LBN 30
#define	ESF_DZ_RX_ABORT_WIDTH 1
#define	ESF_DZ_RX_ECC_ERR_LBN 29
#define	ESF_DZ_RX_ECC_ERR_WIDTH 1
#define	ESF_DZ_RX_CRC1_ERR_LBN 28
#define	ESF_DZ_RX_CRC1_ERR_WIDTH 1
#define	ESF_DZ_RX_CRC0_ERR_LBN 27
#define	ESF_DZ_RX_CRC0_ERR_WIDTH 1
#define	ESF_DZ_RX_TCPUDP_CKSUM_ERR_LBN 26
#define	ESF_DZ_RX_TCPUDP_CKSUM_ERR_WIDTH 1
#define	ESF_DZ_RX_IPCKSUM_ERR_LBN 25
#define	ESF_DZ_RX_IPCKSUM_ERR_WIDTH 1
#define	ESF_DZ_RX_ECRC_ERR_LBN 24
#define	ESF_DZ_RX_ECRC_ERR_WIDTH 1
#define	ESF_DZ_RX_QLABEL_LBN 16
#define	ESF_DZ_RX_QLABEL_WIDTH 5
#define	ESF_DZ_RX_PARSE_INCOMPLETE_LBN 15
#define	ESF_DZ_RX_PARSE_INCOMPLETE_WIDTH 1
#define	ESF_DZ_RX_CONT_LBN 14
#define	ESF_DZ_RX_CONT_WIDTH 1
#define	ESF_DZ_RX_BYTES_LBN 0
#define	ESF_DZ_RX_BYTES_WIDTH 14

/* RX_KER_DESC */
#define	ESF_DZ_RX_KER_RESERVED_LBN 62
#define	ESF_DZ_RX_KER_RESERVED_WIDTH 2
#define	ESF_DZ_RX_KER_BYTE_CNT_LBN 48
#define	ESF_DZ_RX_KER_BYTE_CNT_WIDTH 14
#define	ESF_DZ_RX_KER_BUF_ADDR_LBN 0
#define	ESF_DZ_RX_KER_BUF_ADDR_WIDTH 48

/* TX_CSUM_TSTAMP_DESC */
#define	ESF_DZ_TX_DESC_IS_OPT_LBN 63
#define	ESF_DZ_TX_DESC_IS_OPT_WIDTH 1
#define	ESF_DZ_TX_OPTION_TYPE_LBN 60
#define	ESF_DZ_TX_OPTION_TYPE_WIDTH 3
#define	ESE_DZ_TX_OPTION_DESC_TSO 7
#define	ESE_DZ_TX_OPTION_DESC_VLAN 6
#define	ESE_DZ_TX_OPTION_DESC_CRC_CSUM 0
#define	ESF_DZ_TX_TIMESTAMP_LBN 5
#define	ESF_DZ_TX_TIMESTAMP_WIDTH 1
#define	ESF_DZ_TX_OPTION_CRC_MODE_LBN 2
#define	ESF_DZ_TX_OPTION_CRC_MODE_WIDTH 3
#define	ESE_DZ_TX_OPTION_CRC_FCOIP_MPA 5
#define	ESE_DZ_TX_OPTION_CRC_FCOIP_FCOE 4
#define	ESE_DZ_TX_OPTION_CRC_ISCSI_HDR_AND_PYLD 3
#define	ESE_DZ_TX_OPTION_CRC_ISCSI_HDR 2
#define	ESE_DZ_TX_OPTION_CRC_FCOE 1
#define	ESE_DZ_TX_OPTION_CRC_OFF 0
#define	ESF_DZ_TX_OPTION_UDP_TCP_CSUM_LBN 1
#define	ESF_DZ_TX_OPTION_UDP_TCP_CSUM_WIDTH 1
#define	ESF_DZ_TX_OPTION_IP_CSUM_LBN 0
#define	ESF_DZ_TX_OPTION_IP_CSUM_WIDTH 1

/* TX_EVENT */
#define	ESF_DZ_TX_CODE_LBN 60
#define	ESF_DZ_TX_CODE_WIDTH 4
#define	ESF_DZ_TX_OVERRIDE_HOLDOFF_LBN 59
#define	ESF_DZ_TX_OVERRIDE_HOLDOFF_WIDTH 1
#define	ESF_DZ_TX_DROP_EVENT_LBN 58
#define	ESF_DZ_TX_DROP_EVENT_WIDTH 1
#define	ESF_DZ_TX_EV_RSVD_LBN 48
#define	ESF_DZ_TX_EV_RSVD_WIDTH 10
#define	ESF_DZ_TX_SOFT2_LBN 32
#define	ESF_DZ_TX_SOFT2_WIDTH 16
#define	ESF_DZ_TX_CAN_MERGE_LBN 31
#define	ESF_DZ_TX_CAN_MERGE_WIDTH 1
#define	ESF_DZ_TX_SOFT1_LBN 24
#define	ESF_DZ_TX_SOFT1_WIDTH 7
#define	ESF_DZ_TX_QLABEL_LBN 16
#define	ESF_DZ_TX_QLABEL_WIDTH 5
#define	ESF_DZ_TX_DESCR_INDX_LBN 0
#define	ESF_DZ_TX_DESCR_INDX_WIDTH 16

/* TX_KER_DESC */
#define	ESF_DZ_TX_KER_TYPE_LBN 63
#define	ESF_DZ_TX_KER_TYPE_WIDTH 1
#define	ESF_DZ_TX_KER_CONT_LBN 62
#define	ESF_DZ_TX_KER_CONT_WIDTH 1
#define	ESF_DZ_TX_KER_BYTE_CNT_LBN 48
#define	ESF_DZ_TX_KER_BYTE_CNT_WIDTH 14
#define	ESF_DZ_TX_KER_BUF_ADDR_LBN 0
#define	ESF_DZ_TX_KER_BUF_ADDR_WIDTH 48

/* TX_PIO_DESC */
#define	ESF_DZ_TX_PIO_TYPE_LBN 63
#define	ESF_DZ_TX_PIO_TYPE_WIDTH 1
#define	ESF_DZ_TX_PIO_OPT_LBN 60
#define	ESF_DZ_TX_PIO_OPT_WIDTH 3
#define	ESE_DZ_TX_OPTION_DESC_PIO 1
#define	ESF_DZ_TX_PIO_CONT_LBN 59
#define	ESF_DZ_TX_PIO_CONT_WIDTH 1
#define	ESF_DZ_TX_PIO_BYTE_CNT_LBN 32
#define	ESF_DZ_TX_PIO_BYTE_CNT_WIDTH 12
#define	ESF_DZ_TX_PIO_BUF_ADDR_LBN 0
#define	ESF_DZ_TX_PIO_BUF_ADDR_WIDTH 12

/* TX_TSO_DESC */
#define	ESF_DZ_TX_DESC_IS_OPT_LBN 63
#define	ESF_DZ_TX_DESC_IS_OPT_WIDTH 1
#define	ESF_DZ_TX_OPTION_TYPE_LBN 60
#define	ESF_DZ_TX_OPTION_TYPE_WIDTH 3
#define	ESE_DZ_TX_OPTION_DESC_TSO 7
#define	ESE_DZ_TX_OPTION_DESC_VLAN 6
#define	ESE_DZ_TX_OPTION_DESC_CRC_CSUM 0
#define	ESF_DZ_TX_TSO_TCP_FLAGS_LBN 48
#define	ESF_DZ_TX_TSO_TCP_FLAGS_WIDTH 8
#define	ESF_DZ_TX_TSO_IP_ID_LBN 32
#define	ESF_DZ_TX_TSO_IP_ID_WIDTH 16
#define	ESF_DZ_TX_TSO_TCP_SEQNO_LBN 0
#define	ESF_DZ_TX_TSO_TCP_SEQNO_WIDTH 32

/*************************************************************************/

/* TX_DESC_UPD_REG: Transmit descriptor update register.
 * We may write just one dword of these registers.
 */
#define ER_DZ_TX_DESC_UPD_DWORD		(ER_DZ_TX_DESC_UPD + 2 * 4)
#define ERF_DZ_TX_DESC_WPTR_DWORD_LBN	(ERF_DZ_TX_DESC_WPTR_LBN - 2 * 32)
#define ERF_DZ_TX_DESC_WPTR_DWORD_WIDTH	ERF_DZ_TX_DESC_WPTR_WIDTH

/* The workaround for bug 35388 requires multiplexing writes through
 * the TX_DESC_UPD_DWORD address.
 * TX_DESC_UPD: 0ppppppppppp               (bit 11 lost)
 * EVQ_RPTR:    1000hhhhhhhh, 1001llllllll (split into high and low bits)
 * EVQ_TMR:     11mmvvvvvvvv               (bits 8:13 of value lost)
 */
#define ER_DD_EVQ_INDIRECT		ER_DZ_TX_DESC_UPD_DWORD
#define ERF_DD_EVQ_IND_RPTR_FLAGS_LBN	8
#define ERF_DD_EVQ_IND_RPTR_FLAGS_WIDTH	4
#define EFE_DD_EVQ_IND_RPTR_FLAGS_HIGH	8
#define EFE_DD_EVQ_IND_RPTR_FLAGS_LOW	9
#define ERF_DD_EVQ_IND_RPTR_LBN		0
#define ERF_DD_EVQ_IND_RPTR_WIDTH	8
#define ERF_DD_EVQ_IND_TIMER_FLAGS_LBN	10
#define ERF_DD_EVQ_IND_TIMER_FLAGS_WIDTH 2
#define EFE_DD_EVQ_IND_TIMER_FLAGS	3
#define ERF_DD_EVQ_IND_TIMER_MODE_LBN	8
#define ERF_DD_EVQ_IND_TIMER_MODE_WIDTH	2
#define ERF_DD_EVQ_IND_TIMER_VAL_LBN	0
#define ERF_DD_EVQ_IND_TIMER_VAL_WIDTH	8

/* TX_PIOBUF
 * PIO buffer aperture (paged)
 */
#define ER_DZ_TX_PIOBUF 4096
#define ER_DZ_TX_PIOBUF_SIZE 2048

/* RX packet prefix */
#define ES_DZ_RX_PREFIX_HASH_OFST 0
#define ES_DZ_RX_PREFIX_VLAN1_OFST 4
#define ES_DZ_RX_PREFIX_VLAN2_OFST 6
#define ES_DZ_RX_PREFIX_PKTLEN_OFST 8
#define ES_DZ_RX_PREFIX_TSTAMP_OFST 10
#define ES_DZ_RX_PREFIX_SIZE 14

#endif /* EFX_EF10_REGS_H */
