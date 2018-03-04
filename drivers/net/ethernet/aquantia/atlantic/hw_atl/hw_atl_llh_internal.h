/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File hw_atl_llh_internal.h: Preprocessor definitions
 * for Atlantic registers.
 */

#ifndef HW_ATL_LLH_INTERNAL_H
#define HW_ATL_LLH_INTERNAL_H

/* global microprocessor semaphore  definitions
 * base address: 0x000003a0
 * parameter: semaphore {s} | stride size 0x4 | range [0, 15]
 */
#define glb_cpu_sem_adr(semaphore)  (0x000003a0u + (semaphore) * 0x4)
/* register address for bitfield rx dma good octet counter lsw [1f:0] */
#define stats_rx_dma_good_octet_counterlsw__adr 0x00006808
/* register address for bitfield rx dma good packet counter lsw [1f:0] */
#define stats_rx_dma_good_pkt_counterlsw__adr 0x00006800
/* register address for bitfield tx dma good octet counter lsw [1f:0] */
#define stats_tx_dma_good_octet_counterlsw__adr 0x00008808
/* register address for bitfield tx dma good packet counter lsw [1f:0] */
#define stats_tx_dma_good_pkt_counterlsw__adr 0x00008800

/* register address for bitfield rx dma good octet counter msw [3f:20] */
#define stats_rx_dma_good_octet_countermsw__adr 0x0000680c
/* register address for bitfield rx dma good packet counter msw [3f:20] */
#define stats_rx_dma_good_pkt_countermsw__adr 0x00006804
/* register address for bitfield tx dma good octet counter msw [3f:20] */
#define stats_tx_dma_good_octet_countermsw__adr 0x0000880c
/* register address for bitfield tx dma good packet counter msw [3f:20] */
#define stats_tx_dma_good_pkt_countermsw__adr 0x00008804

/* preprocessor definitions for msm rx errors counter register */
#define mac_msm_rx_errs_cnt_adr 0x00000120u

/* preprocessor definitions for msm rx unicast frames counter register */
#define mac_msm_rx_ucst_frm_cnt_adr 0x000000e0u

/* preprocessor definitions for msm rx multicast frames counter register */
#define mac_msm_rx_mcst_frm_cnt_adr 0x000000e8u

/* preprocessor definitions for msm rx broadcast frames counter register */
#define mac_msm_rx_bcst_frm_cnt_adr 0x000000f0u

/* preprocessor definitions for msm rx broadcast octets counter register 1 */
#define mac_msm_rx_bcst_octets_counter1_adr 0x000001b0u

/* preprocessor definitions for msm rx broadcast octets counter register 2 */
#define mac_msm_rx_bcst_octets_counter2_adr 0x000001b4u

/* preprocessor definitions for msm rx unicast octets counter register 0 */
#define mac_msm_rx_ucst_octets_counter0_adr 0x000001b8u

/* preprocessor definitions for rx dma statistics counter 7 */
#define rx_dma_stat_counter7_adr 0x00006818u

/* preprocessor definitions for msm tx unicast frames counter register */
#define mac_msm_tx_ucst_frm_cnt_adr 0x00000108u

/* preprocessor definitions for msm tx multicast frames counter register */
#define mac_msm_tx_mcst_frm_cnt_adr 0x00000110u

/* preprocessor definitions for global mif identification */
#define glb_mif_id_adr 0x0000001cu

/* register address for bitfield iamr_lsw[1f:0] */
#define itr_iamrlsw_adr 0x00002090
/* register address for bitfield rx dma drop packet counter [1f:0] */
#define rpb_rx_dma_drop_pkt_cnt_adr 0x00006818

/* register address for bitfield imcr_lsw[1f:0] */
#define itr_imcrlsw_adr 0x00002070
/* register address for bitfield imsr_lsw[1f:0] */
#define itr_imsrlsw_adr 0x00002060
/* register address for bitfield itr_reg_res_dsbl */
#define itr_reg_res_dsbl_adr 0x00002300
/* bitmask for bitfield itr_reg_res_dsbl */
#define itr_reg_res_dsbl_msk 0x20000000
/* lower bit position of bitfield itr_reg_res_dsbl */
#define itr_reg_res_dsbl_shift 29
/* register address for bitfield iscr_lsw[1f:0] */
#define itr_iscrlsw_adr 0x00002050
/* register address for bitfield isr_lsw[1f:0] */
#define itr_isrlsw_adr 0x00002000
/* register address for bitfield itr_reset */
#define itr_res_adr 0x00002300
/* bitmask for bitfield itr_reset */
#define itr_res_msk 0x80000000
/* lower bit position of bitfield itr_reset */
#define itr_res_shift 31
/* register address for bitfield dca{d}_cpuid[7:0] */
#define rdm_dcadcpuid_adr(dca) (0x00006100 + (dca) * 0x4)
/* bitmask for bitfield dca{d}_cpuid[7:0] */
#define rdm_dcadcpuid_msk 0x000000ff
/* lower bit position of bitfield dca{d}_cpuid[7:0] */
#define rdm_dcadcpuid_shift 0
/* register address for bitfield dca_en */
#define rdm_dca_en_adr 0x00006180

/* rx dca_en bitfield definitions
 * preprocessor definitions for the bitfield "dca_en".
 * port="pif_rdm_dca_en_i"
 */

/* register address for bitfield dca_en */
#define rdm_dca_en_adr 0x00006180
/* bitmask for bitfield dca_en */
#define rdm_dca_en_msk 0x80000000
/* inverted bitmask for bitfield dca_en */
#define rdm_dca_en_mskn 0x7fffffff
/* lower bit position of bitfield dca_en */
#define rdm_dca_en_shift 31
/* width of bitfield dca_en */
#define rdm_dca_en_width 1
/* default value of bitfield dca_en */
#define rdm_dca_en_default 0x1

/* rx dca_mode[3:0] bitfield definitions
 * preprocessor definitions for the bitfield "dca_mode[3:0]".
 * port="pif_rdm_dca_mode_i[3:0]"
 */

/* register address for bitfield dca_mode[3:0] */
#define rdm_dca_mode_adr 0x00006180
/* bitmask for bitfield dca_mode[3:0] */
#define rdm_dca_mode_msk 0x0000000f
/* inverted bitmask for bitfield dca_mode[3:0] */
#define rdm_dca_mode_mskn 0xfffffff0
/* lower bit position of bitfield dca_mode[3:0] */
#define rdm_dca_mode_shift 0
/* width of bitfield dca_mode[3:0] */
#define rdm_dca_mode_width 4
/* default value of bitfield dca_mode[3:0] */
#define rdm_dca_mode_default 0x0

/* rx desc{d}_data_size[4:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_data_size[4:0]".
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 * port="pif_rdm_desc0_data_size_i[4:0]"
 */

/* register address for bitfield desc{d}_data_size[4:0] */
#define rdm_descddata_size_adr(descriptor) (0x00005b18 + (descriptor) * 0x20)
/* bitmask for bitfield desc{d}_data_size[4:0] */
#define rdm_descddata_size_msk 0x0000001f
/* inverted bitmask for bitfield desc{d}_data_size[4:0] */
#define rdm_descddata_size_mskn 0xffffffe0
/* lower bit position of bitfield desc{d}_data_size[4:0] */
#define rdm_descddata_size_shift 0
/* width of bitfield desc{d}_data_size[4:0] */
#define rdm_descddata_size_width 5
/* default value of bitfield desc{d}_data_size[4:0] */
#define rdm_descddata_size_default 0x0

/* rx dca{d}_desc_en bitfield definitions
 * preprocessor definitions for the bitfield "dca{d}_desc_en".
 * parameter: dca {d} | stride size 0x4 | range [0, 31]
 * port="pif_rdm_dca_desc_en_i[0]"
 */

/* register address for bitfield dca{d}_desc_en */
#define rdm_dcaddesc_en_adr(dca) (0x00006100 + (dca) * 0x4)
/* bitmask for bitfield dca{d}_desc_en */
#define rdm_dcaddesc_en_msk 0x80000000
/* inverted bitmask for bitfield dca{d}_desc_en */
#define rdm_dcaddesc_en_mskn 0x7fffffff
/* lower bit position of bitfield dca{d}_desc_en */
#define rdm_dcaddesc_en_shift 31
/* width of bitfield dca{d}_desc_en */
#define rdm_dcaddesc_en_width 1
/* default value of bitfield dca{d}_desc_en */
#define rdm_dcaddesc_en_default 0x0

/* rx desc{d}_en bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_en".
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 * port="pif_rdm_desc_en_i[0]"
 */

/* register address for bitfield desc{d}_en */
#define rdm_descden_adr(descriptor) (0x00005b08 + (descriptor) * 0x20)
/* bitmask for bitfield desc{d}_en */
#define rdm_descden_msk 0x80000000
/* inverted bitmask for bitfield desc{d}_en */
#define rdm_descden_mskn 0x7fffffff
/* lower bit position of bitfield desc{d}_en */
#define rdm_descden_shift 31
/* width of bitfield desc{d}_en */
#define rdm_descden_width 1
/* default value of bitfield desc{d}_en */
#define rdm_descden_default 0x0

/* rx desc{d}_hdr_size[4:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_hdr_size[4:0]".
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 * port="pif_rdm_desc0_hdr_size_i[4:0]"
 */

/* register address for bitfield desc{d}_hdr_size[4:0] */
#define rdm_descdhdr_size_adr(descriptor) (0x00005b18 + (descriptor) * 0x20)
/* bitmask for bitfield desc{d}_hdr_size[4:0] */
#define rdm_descdhdr_size_msk 0x00001f00
/* inverted bitmask for bitfield desc{d}_hdr_size[4:0] */
#define rdm_descdhdr_size_mskn 0xffffe0ff
/* lower bit position of bitfield desc{d}_hdr_size[4:0] */
#define rdm_descdhdr_size_shift 8
/* width of bitfield desc{d}_hdr_size[4:0] */
#define rdm_descdhdr_size_width 5
/* default value of bitfield desc{d}_hdr_size[4:0] */
#define rdm_descdhdr_size_default 0x0

/* rx desc{d}_hdr_split bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_hdr_split".
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 * port="pif_rdm_desc_hdr_split_i[0]"
 */

/* register address for bitfield desc{d}_hdr_split */
#define rdm_descdhdr_split_adr(descriptor) (0x00005b08 + (descriptor) * 0x20)
/* bitmask for bitfield desc{d}_hdr_split */
#define rdm_descdhdr_split_msk 0x10000000
/* inverted bitmask for bitfield desc{d}_hdr_split */
#define rdm_descdhdr_split_mskn 0xefffffff
/* lower bit position of bitfield desc{d}_hdr_split */
#define rdm_descdhdr_split_shift 28
/* width of bitfield desc{d}_hdr_split */
#define rdm_descdhdr_split_width 1
/* default value of bitfield desc{d}_hdr_split */
#define rdm_descdhdr_split_default 0x0

/* rx desc{d}_hd[c:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_hd[c:0]".
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 * port="rdm_pif_desc0_hd_o[12:0]"
 */

/* register address for bitfield desc{d}_hd[c:0] */
#define rdm_descdhd_adr(descriptor) (0x00005b0c + (descriptor) * 0x20)
/* bitmask for bitfield desc{d}_hd[c:0] */
#define rdm_descdhd_msk 0x00001fff
/* inverted bitmask for bitfield desc{d}_hd[c:0] */
#define rdm_descdhd_mskn 0xffffe000
/* lower bit position of bitfield desc{d}_hd[c:0] */
#define rdm_descdhd_shift 0
/* width of bitfield desc{d}_hd[c:0] */
#define rdm_descdhd_width 13

/* rx desc{d}_len[9:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_len[9:0]".
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 * port="pif_rdm_desc0_len_i[9:0]"
 */

/* register address for bitfield desc{d}_len[9:0] */
#define rdm_descdlen_adr(descriptor) (0x00005b08 + (descriptor) * 0x20)
/* bitmask for bitfield desc{d}_len[9:0] */
#define rdm_descdlen_msk 0x00001ff8
/* inverted bitmask for bitfield desc{d}_len[9:0] */
#define rdm_descdlen_mskn 0xffffe007
/* lower bit position of bitfield desc{d}_len[9:0] */
#define rdm_descdlen_shift 3
/* width of bitfield desc{d}_len[9:0] */
#define rdm_descdlen_width 10
/* default value of bitfield desc{d}_len[9:0] */
#define rdm_descdlen_default 0x0

/* rx desc{d}_reset bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_reset".
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 * port="pif_rdm_q_pf_res_i[0]"
 */

/* register address for bitfield desc{d}_reset */
#define rdm_descdreset_adr(descriptor) (0x00005b08 + (descriptor) * 0x20)
/* bitmask for bitfield desc{d}_reset */
#define rdm_descdreset_msk 0x02000000
/* inverted bitmask for bitfield desc{d}_reset */
#define rdm_descdreset_mskn 0xfdffffff
/* lower bit position of bitfield desc{d}_reset */
#define rdm_descdreset_shift 25
/* width of bitfield desc{d}_reset */
#define rdm_descdreset_width 1
/* default value of bitfield desc{d}_reset */
#define rdm_descdreset_default 0x0

/* rx int_desc_wrb_en bitfield definitions
 * preprocessor definitions for the bitfield "int_desc_wrb_en".
 * port="pif_rdm_int_desc_wrb_en_i"
 */

/* register address for bitfield int_desc_wrb_en */
#define rdm_int_desc_wrb_en_adr 0x00005a30
/* bitmask for bitfield int_desc_wrb_en */
#define rdm_int_desc_wrb_en_msk 0x00000004
/* inverted bitmask for bitfield int_desc_wrb_en */
#define rdm_int_desc_wrb_en_mskn 0xfffffffb
/* lower bit position of bitfield int_desc_wrb_en */
#define rdm_int_desc_wrb_en_shift 2
/* width of bitfield int_desc_wrb_en */
#define rdm_int_desc_wrb_en_width 1
/* default value of bitfield int_desc_wrb_en */
#define rdm_int_desc_wrb_en_default 0x0

/* rx dca{d}_hdr_en bitfield definitions
 * preprocessor definitions for the bitfield "dca{d}_hdr_en".
 * parameter: dca {d} | stride size 0x4 | range [0, 31]
 * port="pif_rdm_dca_hdr_en_i[0]"
 */

/* register address for bitfield dca{d}_hdr_en */
#define rdm_dcadhdr_en_adr(dca) (0x00006100 + (dca) * 0x4)
/* bitmask for bitfield dca{d}_hdr_en */
#define rdm_dcadhdr_en_msk 0x40000000
/* inverted bitmask for bitfield dca{d}_hdr_en */
#define rdm_dcadhdr_en_mskn 0xbfffffff
/* lower bit position of bitfield dca{d}_hdr_en */
#define rdm_dcadhdr_en_shift 30
/* width of bitfield dca{d}_hdr_en */
#define rdm_dcadhdr_en_width 1
/* default value of bitfield dca{d}_hdr_en */
#define rdm_dcadhdr_en_default 0x0

/* rx dca{d}_pay_en bitfield definitions
 * preprocessor definitions for the bitfield "dca{d}_pay_en".
 * parameter: dca {d} | stride size 0x4 | range [0, 31]
 * port="pif_rdm_dca_pay_en_i[0]"
 */

/* register address for bitfield dca{d}_pay_en */
#define rdm_dcadpay_en_adr(dca) (0x00006100 + (dca) * 0x4)
/* bitmask for bitfield dca{d}_pay_en */
#define rdm_dcadpay_en_msk 0x20000000
/* inverted bitmask for bitfield dca{d}_pay_en */
#define rdm_dcadpay_en_mskn 0xdfffffff
/* lower bit position of bitfield dca{d}_pay_en */
#define rdm_dcadpay_en_shift 29
/* width of bitfield dca{d}_pay_en */
#define rdm_dcadpay_en_width 1
/* default value of bitfield dca{d}_pay_en */
#define rdm_dcadpay_en_default 0x0

/* RX rdm_int_rim_en Bitfield Definitions
 * Preprocessor definitions for the bitfield "rdm_int_rim_en".
 * PORT="pif_rdm_int_rim_en_i"
 */

/* Register address for bitfield rdm_int_rim_en */
#define rdm_int_rim_en_adr 0x00005A30
/* Bitmask for bitfield rdm_int_rim_en */
#define rdm_int_rim_en_msk 0x00000008
/* Inverted bitmask for bitfield rdm_int_rim_en */
#define rdm_int_rim_en_mskn 0xFFFFFFF7
/* Lower bit position of bitfield rdm_int_rim_en */
#define rdm_int_rim_en_shift 3
/* Width of bitfield rdm_int_rim_en */
#define rdm_int_rim_en_width 1
/* Default value of bitfield rdm_int_rim_en */
#define rdm_int_rim_en_default 0x0

/* general interrupt mapping register definitions
 * preprocessor definitions for general interrupt mapping register
 * base address: 0x00002180
 * parameter: regidx {f} | stride size 0x4 | range [0, 3]
 */
#define gen_intr_map_adr(regidx) (0x00002180u + (regidx) * 0x4)

/* general interrupt status register definitions
 * preprocessor definitions for general interrupt status register
 * address: 0x000021A0
 */

#define gen_intr_stat_adr 0x000021A4U

/* interrupt global control register  definitions
 * preprocessor definitions for interrupt global control register
 * address: 0x00002300
 */
#define intr_glb_ctl_adr 0x00002300u

/* interrupt throttle register definitions
 * preprocessor definitions for interrupt throttle register
 * base address: 0x00002800
 * parameter: throttle {t} | stride size 0x4 | range [0, 31]
 */
#define intr_thr_adr(throttle) (0x00002800u + (throttle) * 0x4)

/* rx dma descriptor base address lsw definitions
 * preprocessor definitions for rx dma descriptor base address lsw
 * base address: 0x00005b00
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 */
#define rx_dma_desc_base_addrlsw_adr(descriptor) \
(0x00005b00u + (descriptor) * 0x20)

/* rx dma descriptor base address msw definitions
 * preprocessor definitions for rx dma descriptor base address msw
 * base address: 0x00005b04
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 */
#define rx_dma_desc_base_addrmsw_adr(descriptor) \
(0x00005b04u + (descriptor) * 0x20)

/* rx dma descriptor status register definitions
 * preprocessor definitions for rx dma descriptor status register
 * base address: 0x00005b14
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 */
#define rx_dma_desc_stat_adr(descriptor) (0x00005b14u + (descriptor) * 0x20)

/* rx dma descriptor tail pointer register definitions
 * preprocessor definitions for rx dma descriptor tail pointer register
 * base address: 0x00005b10
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 */
#define rx_dma_desc_tail_ptr_adr(descriptor) (0x00005b10u + (descriptor) * 0x20)

/* rx interrupt moderation control register definitions
 * Preprocessor definitions for RX Interrupt Moderation Control Register
 * Base Address: 0x00005A40
 * Parameter: RIM {R} | stride size 0x4 | range [0, 31]
 */
#define rx_intr_moderation_ctl_adr(rim) (0x00005A40u + (rim) * 0x4)

/* rx filter multicast filter mask register definitions
 * preprocessor definitions for rx filter multicast filter mask register
 * address: 0x00005270
 */
#define rx_flr_mcst_flr_msk_adr 0x00005270u

/* rx filter multicast filter register definitions
 * preprocessor definitions for rx filter multicast filter register
 * base address: 0x00005250
 * parameter: filter {f} | stride size 0x4 | range [0, 7]
 */
#define rx_flr_mcst_flr_adr(filter) (0x00005250u + (filter) * 0x4)

/* RX Filter RSS Control Register 1 Definitions
 * Preprocessor definitions for RX Filter RSS Control Register 1
 * Address: 0x000054C0
 */
#define rx_flr_rss_control1_adr 0x000054C0u

/* RX Filter Control Register 2 Definitions
 * Preprocessor definitions for RX Filter Control Register 2
 * Address: 0x00005104
 */
#define rx_flr_control2_adr 0x00005104u

/* tx tx dma debug control [1f:0] bitfield definitions
 * preprocessor definitions for the bitfield "tx dma debug control [1f:0]".
 * port="pif_tdm_debug_cntl_i[31:0]"
 */

/* register address for bitfield tx dma debug control [1f:0] */
#define tdm_tx_dma_debug_ctl_adr 0x00008920
/* bitmask for bitfield tx dma debug control [1f:0] */
#define tdm_tx_dma_debug_ctl_msk 0xffffffff
/* inverted bitmask for bitfield tx dma debug control [1f:0] */
#define tdm_tx_dma_debug_ctl_mskn 0x00000000
/* lower bit position of bitfield tx dma debug control [1f:0] */
#define tdm_tx_dma_debug_ctl_shift 0
/* width of bitfield tx dma debug control [1f:0] */
#define tdm_tx_dma_debug_ctl_width 32
/* default value of bitfield tx dma debug control [1f:0] */
#define tdm_tx_dma_debug_ctl_default 0x0

/* tx dma descriptor base address lsw definitions
 * preprocessor definitions for tx dma descriptor base address lsw
 * base address: 0x00007c00
 * parameter: descriptor {d} | stride size 0x40 | range [0, 31]
 */
#define tx_dma_desc_base_addrlsw_adr(descriptor) \
	(0x00007c00u + (descriptor) * 0x40)

/* tx dma descriptor tail pointer register definitions
 * preprocessor definitions for tx dma descriptor tail pointer register
 * base address: 0x00007c10
 *  parameter: descriptor {d} | stride size 0x40 | range [0, 31]
 */
#define tx_dma_desc_tail_ptr_adr(descriptor) (0x00007c10u + (descriptor) * 0x40)

/* rx dma_sys_loopback bitfield definitions
 * preprocessor definitions for the bitfield "dma_sys_loopback".
 * port="pif_rpb_dma_sys_lbk_i"
 */

/* register address for bitfield dma_sys_loopback */
#define rpb_dma_sys_lbk_adr 0x00005000
/* bitmask for bitfield dma_sys_loopback */
#define rpb_dma_sys_lbk_msk 0x00000040
/* inverted bitmask for bitfield dma_sys_loopback */
#define rpb_dma_sys_lbk_mskn 0xffffffbf
/* lower bit position of bitfield dma_sys_loopback */
#define rpb_dma_sys_lbk_shift 6
/* width of bitfield dma_sys_loopback */
#define rpb_dma_sys_lbk_width 1
/* default value of bitfield dma_sys_loopback */
#define rpb_dma_sys_lbk_default 0x0

/* rx rx_tc_mode bitfield definitions
 * preprocessor definitions for the bitfield "rx_tc_mode".
 * port="pif_rpb_rx_tc_mode_i,pif_rpf_rx_tc_mode_i"
 */

/* register address for bitfield rx_tc_mode */
#define rpb_rpf_rx_tc_mode_adr 0x00005700
/* bitmask for bitfield rx_tc_mode */
#define rpb_rpf_rx_tc_mode_msk 0x00000100
/* inverted bitmask for bitfield rx_tc_mode */
#define rpb_rpf_rx_tc_mode_mskn 0xfffffeff
/* lower bit position of bitfield rx_tc_mode */
#define rpb_rpf_rx_tc_mode_shift 8
/* width of bitfield rx_tc_mode */
#define rpb_rpf_rx_tc_mode_width 1
/* default value of bitfield rx_tc_mode */
#define rpb_rpf_rx_tc_mode_default 0x0

/* rx rx_buf_en bitfield definitions
 * preprocessor definitions for the bitfield "rx_buf_en".
 * port="pif_rpb_rx_buf_en_i"
 */

/* register address for bitfield rx_buf_en */
#define rpb_rx_buf_en_adr 0x00005700
/* bitmask for bitfield rx_buf_en */
#define rpb_rx_buf_en_msk 0x00000001
/* inverted bitmask for bitfield rx_buf_en */
#define rpb_rx_buf_en_mskn 0xfffffffe
/* lower bit position of bitfield rx_buf_en */
#define rpb_rx_buf_en_shift 0
/* width of bitfield rx_buf_en */
#define rpb_rx_buf_en_width 1
/* default value of bitfield rx_buf_en */
#define rpb_rx_buf_en_default 0x0

/* rx rx{b}_hi_thresh[d:0] bitfield definitions
 * preprocessor definitions for the bitfield "rx{b}_hi_thresh[d:0]".
 * parameter: buffer {b} | stride size 0x10 | range [0, 7]
 * port="pif_rpb_rx0_hi_thresh_i[13:0]"
 */

/* register address for bitfield rx{b}_hi_thresh[d:0] */
#define rpb_rxbhi_thresh_adr(buffer) (0x00005714 + (buffer) * 0x10)
/* bitmask for bitfield rx{b}_hi_thresh[d:0] */
#define rpb_rxbhi_thresh_msk 0x3fff0000
/* inverted bitmask for bitfield rx{b}_hi_thresh[d:0] */
#define rpb_rxbhi_thresh_mskn 0xc000ffff
/* lower bit position of bitfield rx{b}_hi_thresh[d:0] */
#define rpb_rxbhi_thresh_shift 16
/* width of bitfield rx{b}_hi_thresh[d:0] */
#define rpb_rxbhi_thresh_width 14
/* default value of bitfield rx{b}_hi_thresh[d:0] */
#define rpb_rxbhi_thresh_default 0x0

/* rx rx{b}_lo_thresh[d:0] bitfield definitions
 * preprocessor definitions for the bitfield "rx{b}_lo_thresh[d:0]".
 * parameter: buffer {b} | stride size 0x10 | range [0, 7]
 * port="pif_rpb_rx0_lo_thresh_i[13:0]"
 */

/* register address for bitfield rx{b}_lo_thresh[d:0] */
#define rpb_rxblo_thresh_adr(buffer) (0x00005714 + (buffer) * 0x10)
/* bitmask for bitfield rx{b}_lo_thresh[d:0] */
#define rpb_rxblo_thresh_msk 0x00003fff
/* inverted bitmask for bitfield rx{b}_lo_thresh[d:0] */
#define rpb_rxblo_thresh_mskn 0xffffc000
/* lower bit position of bitfield rx{b}_lo_thresh[d:0] */
#define rpb_rxblo_thresh_shift 0
/* width of bitfield rx{b}_lo_thresh[d:0] */
#define rpb_rxblo_thresh_width 14
/* default value of bitfield rx{b}_lo_thresh[d:0] */
#define rpb_rxblo_thresh_default 0x0

/* rx rx_fc_mode[1:0] bitfield definitions
 * preprocessor definitions for the bitfield "rx_fc_mode[1:0]".
 * port="pif_rpb_rx_fc_mode_i[1:0]"
 */

/* register address for bitfield rx_fc_mode[1:0] */
#define rpb_rx_fc_mode_adr 0x00005700
/* bitmask for bitfield rx_fc_mode[1:0] */
#define rpb_rx_fc_mode_msk 0x00000030
/* inverted bitmask for bitfield rx_fc_mode[1:0] */
#define rpb_rx_fc_mode_mskn 0xffffffcf
/* lower bit position of bitfield rx_fc_mode[1:0] */
#define rpb_rx_fc_mode_shift 4
/* width of bitfield rx_fc_mode[1:0] */
#define rpb_rx_fc_mode_width 2
/* default value of bitfield rx_fc_mode[1:0] */
#define rpb_rx_fc_mode_default 0x0

/* rx rx{b}_buf_size[8:0] bitfield definitions
 * preprocessor definitions for the bitfield "rx{b}_buf_size[8:0]".
 * parameter: buffer {b} | stride size 0x10 | range [0, 7]
 * port="pif_rpb_rx0_buf_size_i[8:0]"
 */

/* register address for bitfield rx{b}_buf_size[8:0] */
#define rpb_rxbbuf_size_adr(buffer) (0x00005710 + (buffer) * 0x10)
/* bitmask for bitfield rx{b}_buf_size[8:0] */
#define rpb_rxbbuf_size_msk 0x000001ff
/* inverted bitmask for bitfield rx{b}_buf_size[8:0] */
#define rpb_rxbbuf_size_mskn 0xfffffe00
/* lower bit position of bitfield rx{b}_buf_size[8:0] */
#define rpb_rxbbuf_size_shift 0
/* width of bitfield rx{b}_buf_size[8:0] */
#define rpb_rxbbuf_size_width 9
/* default value of bitfield rx{b}_buf_size[8:0] */
#define rpb_rxbbuf_size_default 0x0

/* rx rx{b}_xoff_en bitfield definitions
 * preprocessor definitions for the bitfield "rx{b}_xoff_en".
 * parameter: buffer {b} | stride size 0x10 | range [0, 7]
 * port="pif_rpb_rx_xoff_en_i[0]"
 */

/* register address for bitfield rx{b}_xoff_en */
#define rpb_rxbxoff_en_adr(buffer) (0x00005714 + (buffer) * 0x10)
/* bitmask for bitfield rx{b}_xoff_en */
#define rpb_rxbxoff_en_msk 0x80000000
/* inverted bitmask for bitfield rx{b}_xoff_en */
#define rpb_rxbxoff_en_mskn 0x7fffffff
/* lower bit position of bitfield rx{b}_xoff_en */
#define rpb_rxbxoff_en_shift 31
/* width of bitfield rx{b}_xoff_en */
#define rpb_rxbxoff_en_width 1
/* default value of bitfield rx{b}_xoff_en */
#define rpb_rxbxoff_en_default 0x0

/* rx l2_bc_thresh[f:0] bitfield definitions
 * preprocessor definitions for the bitfield "l2_bc_thresh[f:0]".
 * port="pif_rpf_l2_bc_thresh_i[15:0]"
 */

/* register address for bitfield l2_bc_thresh[f:0] */
#define rpfl2bc_thresh_adr 0x00005100
/* bitmask for bitfield l2_bc_thresh[f:0] */
#define rpfl2bc_thresh_msk 0xffff0000
/* inverted bitmask for bitfield l2_bc_thresh[f:0] */
#define rpfl2bc_thresh_mskn 0x0000ffff
/* lower bit position of bitfield l2_bc_thresh[f:0] */
#define rpfl2bc_thresh_shift 16
/* width of bitfield l2_bc_thresh[f:0] */
#define rpfl2bc_thresh_width 16
/* default value of bitfield l2_bc_thresh[f:0] */
#define rpfl2bc_thresh_default 0x0

/* rx l2_bc_en bitfield definitions
 * preprocessor definitions for the bitfield "l2_bc_en".
 * port="pif_rpf_l2_bc_en_i"
 */

/* register address for bitfield l2_bc_en */
#define rpfl2bc_en_adr 0x00005100
/* bitmask for bitfield l2_bc_en */
#define rpfl2bc_en_msk 0x00000001
/* inverted bitmask for bitfield l2_bc_en */
#define rpfl2bc_en_mskn 0xfffffffe
/* lower bit position of bitfield l2_bc_en */
#define rpfl2bc_en_shift 0
/* width of bitfield l2_bc_en */
#define rpfl2bc_en_width 1
/* default value of bitfield l2_bc_en */
#define rpfl2bc_en_default 0x0

/* rx l2_bc_act[2:0] bitfield definitions
 * preprocessor definitions for the bitfield "l2_bc_act[2:0]".
 * port="pif_rpf_l2_bc_act_i[2:0]"
 */

/* register address for bitfield l2_bc_act[2:0] */
#define rpfl2bc_act_adr 0x00005100
/* bitmask for bitfield l2_bc_act[2:0] */
#define rpfl2bc_act_msk 0x00007000
/* inverted bitmask for bitfield l2_bc_act[2:0] */
#define rpfl2bc_act_mskn 0xffff8fff
/* lower bit position of bitfield l2_bc_act[2:0] */
#define rpfl2bc_act_shift 12
/* width of bitfield l2_bc_act[2:0] */
#define rpfl2bc_act_width 3
/* default value of bitfield l2_bc_act[2:0] */
#define rpfl2bc_act_default 0x0

/* rx l2_mc_en{f} bitfield definitions
 * preprocessor definitions for the bitfield "l2_mc_en{f}".
 * parameter: filter {f} | stride size 0x4 | range [0, 7]
 * port="pif_rpf_l2_mc_en_i[0]"
 */

/* register address for bitfield l2_mc_en{f} */
#define rpfl2mc_enf_adr(filter) (0x00005250 + (filter) * 0x4)
/* bitmask for bitfield l2_mc_en{f} */
#define rpfl2mc_enf_msk 0x80000000
/* inverted bitmask for bitfield l2_mc_en{f} */
#define rpfl2mc_enf_mskn 0x7fffffff
/* lower bit position of bitfield l2_mc_en{f} */
#define rpfl2mc_enf_shift 31
/* width of bitfield l2_mc_en{f} */
#define rpfl2mc_enf_width 1
/* default value of bitfield l2_mc_en{f} */
#define rpfl2mc_enf_default 0x0

/* rx l2_promis_mode bitfield definitions
 * preprocessor definitions for the bitfield "l2_promis_mode".
 * port="pif_rpf_l2_promis_mode_i"
 */

/* register address for bitfield l2_promis_mode */
#define rpfl2promis_mode_adr 0x00005100
/* bitmask for bitfield l2_promis_mode */
#define rpfl2promis_mode_msk 0x00000008
/* inverted bitmask for bitfield l2_promis_mode */
#define rpfl2promis_mode_mskn 0xfffffff7
/* lower bit position of bitfield l2_promis_mode */
#define rpfl2promis_mode_shift 3
/* width of bitfield l2_promis_mode */
#define rpfl2promis_mode_width 1
/* default value of bitfield l2_promis_mode */
#define rpfl2promis_mode_default 0x0

/* rx l2_uc_act{f}[2:0] bitfield definitions
 * preprocessor definitions for the bitfield "l2_uc_act{f}[2:0]".
 * parameter: filter {f} | stride size 0x8 | range [0, 37]
 * port="pif_rpf_l2_uc_act0_i[2:0]"
 */

/* register address for bitfield l2_uc_act{f}[2:0] */
#define rpfl2uc_actf_adr(filter) (0x00005114 + (filter) * 0x8)
/* bitmask for bitfield l2_uc_act{f}[2:0] */
#define rpfl2uc_actf_msk 0x00070000
/* inverted bitmask for bitfield l2_uc_act{f}[2:0] */
#define rpfl2uc_actf_mskn 0xfff8ffff
/* lower bit position of bitfield l2_uc_act{f}[2:0] */
#define rpfl2uc_actf_shift 16
/* width of bitfield l2_uc_act{f}[2:0] */
#define rpfl2uc_actf_width 3
/* default value of bitfield l2_uc_act{f}[2:0] */
#define rpfl2uc_actf_default 0x0

/* rx l2_uc_en{f} bitfield definitions
 * preprocessor definitions for the bitfield "l2_uc_en{f}".
 * parameter: filter {f} | stride size 0x8 | range [0, 37]
 * port="pif_rpf_l2_uc_en_i[0]"
 */

/* register address for bitfield l2_uc_en{f} */
#define rpfl2uc_enf_adr(filter) (0x00005114 + (filter) * 0x8)
/* bitmask for bitfield l2_uc_en{f} */
#define rpfl2uc_enf_msk 0x80000000
/* inverted bitmask for bitfield l2_uc_en{f} */
#define rpfl2uc_enf_mskn 0x7fffffff
/* lower bit position of bitfield l2_uc_en{f} */
#define rpfl2uc_enf_shift 31
/* width of bitfield l2_uc_en{f} */
#define rpfl2uc_enf_width 1
/* default value of bitfield l2_uc_en{f} */
#define rpfl2uc_enf_default 0x0

/* register address for bitfield l2_uc_da{f}_lsw[1f:0] */
#define rpfl2uc_daflsw_adr(filter) (0x00005110 + (filter) * 0x8)
/* register address for bitfield l2_uc_da{f}_msw[f:0] */
#define rpfl2uc_dafmsw_adr(filter) (0x00005114 + (filter) * 0x8)
/* bitmask for bitfield l2_uc_da{f}_msw[f:0] */
#define rpfl2uc_dafmsw_msk 0x0000ffff
/* lower bit position of bitfield l2_uc_da{f}_msw[f:0] */
#define rpfl2uc_dafmsw_shift 0

/* rx l2_mc_accept_all bitfield definitions
 * Preprocessor definitions for the bitfield "l2_mc_accept_all".
 * PORT="pif_rpf_l2_mc_all_accept_i"
 */

/* Register address for bitfield l2_mc_accept_all */
#define rpfl2mc_accept_all_adr 0x00005270
/* Bitmask for bitfield l2_mc_accept_all */
#define rpfl2mc_accept_all_msk 0x00004000
/* Inverted bitmask for bitfield l2_mc_accept_all */
#define rpfl2mc_accept_all_mskn 0xFFFFBFFF
/* Lower bit position of bitfield l2_mc_accept_all */
#define rpfl2mc_accept_all_shift 14
/* Width of bitfield l2_mc_accept_all */
#define rpfl2mc_accept_all_width 1
/* Default value of bitfield l2_mc_accept_all */
#define rpfl2mc_accept_all_default 0x0

/* width of bitfield rx_tc_up{t}[2:0] */
#define rpf_rpb_rx_tc_upt_width 3
/* default value of bitfield rx_tc_up{t}[2:0] */
#define rpf_rpb_rx_tc_upt_default 0x0

/* rx rss_key_addr[4:0] bitfield definitions
 * preprocessor definitions for the bitfield "rss_key_addr[4:0]".
 * port="pif_rpf_rss_key_addr_i[4:0]"
 */

/* register address for bitfield rss_key_addr[4:0] */
#define rpf_rss_key_addr_adr 0x000054d0
/* bitmask for bitfield rss_key_addr[4:0] */
#define rpf_rss_key_addr_msk 0x0000001f
/* inverted bitmask for bitfield rss_key_addr[4:0] */
#define rpf_rss_key_addr_mskn 0xffffffe0
/* lower bit position of bitfield rss_key_addr[4:0] */
#define rpf_rss_key_addr_shift 0
/* width of bitfield rss_key_addr[4:0] */
#define rpf_rss_key_addr_width 5
/* default value of bitfield rss_key_addr[4:0] */
#define rpf_rss_key_addr_default 0x0

/* rx rss_key_wr_data[1f:0] bitfield definitions
 * preprocessor definitions for the bitfield "rss_key_wr_data[1f:0]".
 * port="pif_rpf_rss_key_wr_data_i[31:0]"
 */

/* register address for bitfield rss_key_wr_data[1f:0] */
#define rpf_rss_key_wr_data_adr 0x000054d4
/* bitmask for bitfield rss_key_wr_data[1f:0] */
#define rpf_rss_key_wr_data_msk 0xffffffff
/* inverted bitmask for bitfield rss_key_wr_data[1f:0] */
#define rpf_rss_key_wr_data_mskn 0x00000000
/* lower bit position of bitfield rss_key_wr_data[1f:0] */
#define rpf_rss_key_wr_data_shift 0
/* width of bitfield rss_key_wr_data[1f:0] */
#define rpf_rss_key_wr_data_width 32
/* default value of bitfield rss_key_wr_data[1f:0] */
#define rpf_rss_key_wr_data_default 0x0

/* rx rss_key_wr_en_i bitfield definitions
 * preprocessor definitions for the bitfield "rss_key_wr_en_i".
 * port="pif_rpf_rss_key_wr_en_i"
 */

/* register address for bitfield rss_key_wr_en_i */
#define rpf_rss_key_wr_eni_adr 0x000054d0
/* bitmask for bitfield rss_key_wr_en_i */
#define rpf_rss_key_wr_eni_msk 0x00000020
/* inverted bitmask for bitfield rss_key_wr_en_i */
#define rpf_rss_key_wr_eni_mskn 0xffffffdf
/* lower bit position of bitfield rss_key_wr_en_i */
#define rpf_rss_key_wr_eni_shift 5
/* width of bitfield rss_key_wr_en_i */
#define rpf_rss_key_wr_eni_width 1
/* default value of bitfield rss_key_wr_en_i */
#define rpf_rss_key_wr_eni_default 0x0

/* rx rss_redir_addr[3:0] bitfield definitions
 * preprocessor definitions for the bitfield "rss_redir_addr[3:0]".
 * port="pif_rpf_rss_redir_addr_i[3:0]"
 */

/* register address for bitfield rss_redir_addr[3:0] */
#define rpf_rss_redir_addr_adr 0x000054e0
/* bitmask for bitfield rss_redir_addr[3:0] */
#define rpf_rss_redir_addr_msk 0x0000000f
/* inverted bitmask for bitfield rss_redir_addr[3:0] */
#define rpf_rss_redir_addr_mskn 0xfffffff0
/* lower bit position of bitfield rss_redir_addr[3:0] */
#define rpf_rss_redir_addr_shift 0
/* width of bitfield rss_redir_addr[3:0] */
#define rpf_rss_redir_addr_width 4
/* default value of bitfield rss_redir_addr[3:0] */
#define rpf_rss_redir_addr_default 0x0

/* rx rss_redir_wr_data[f:0] bitfield definitions
 * preprocessor definitions for the bitfield "rss_redir_wr_data[f:0]".
 * port="pif_rpf_rss_redir_wr_data_i[15:0]"
 */

/* register address for bitfield rss_redir_wr_data[f:0] */
#define rpf_rss_redir_wr_data_adr 0x000054e4
/* bitmask for bitfield rss_redir_wr_data[f:0] */
#define rpf_rss_redir_wr_data_msk 0x0000ffff
/* inverted bitmask for bitfield rss_redir_wr_data[f:0] */
#define rpf_rss_redir_wr_data_mskn 0xffff0000
/* lower bit position of bitfield rss_redir_wr_data[f:0] */
#define rpf_rss_redir_wr_data_shift 0
/* width of bitfield rss_redir_wr_data[f:0] */
#define rpf_rss_redir_wr_data_width 16
/* default value of bitfield rss_redir_wr_data[f:0] */
#define rpf_rss_redir_wr_data_default 0x0

/* rx rss_redir_wr_en_i bitfield definitions
 * preprocessor definitions for the bitfield "rss_redir_wr_en_i".
 * port="pif_rpf_rss_redir_wr_en_i"
 */

/* register address for bitfield rss_redir_wr_en_i */
#define rpf_rss_redir_wr_eni_adr 0x000054e0
/* bitmask for bitfield rss_redir_wr_en_i */
#define rpf_rss_redir_wr_eni_msk 0x00000010
/* inverted bitmask for bitfield rss_redir_wr_en_i */
#define rpf_rss_redir_wr_eni_mskn 0xffffffef
/* lower bit position of bitfield rss_redir_wr_en_i */
#define rpf_rss_redir_wr_eni_shift 4
/* width of bitfield rss_redir_wr_en_i */
#define rpf_rss_redir_wr_eni_width 1
/* default value of bitfield rss_redir_wr_en_i */
#define rpf_rss_redir_wr_eni_default 0x0

/* rx tpo_rpf_sys_loopback bitfield definitions
 * preprocessor definitions for the bitfield "tpo_rpf_sys_loopback".
 * port="pif_rpf_tpo_pkt_sys_lbk_i"
 */

/* register address for bitfield tpo_rpf_sys_loopback */
#define rpf_tpo_rpf_sys_lbk_adr 0x00005000
/* bitmask for bitfield tpo_rpf_sys_loopback */
#define rpf_tpo_rpf_sys_lbk_msk 0x00000100
/* inverted bitmask for bitfield tpo_rpf_sys_loopback */
#define rpf_tpo_rpf_sys_lbk_mskn 0xfffffeff
/* lower bit position of bitfield tpo_rpf_sys_loopback */
#define rpf_tpo_rpf_sys_lbk_shift 8
/* width of bitfield tpo_rpf_sys_loopback */
#define rpf_tpo_rpf_sys_lbk_width 1
/* default value of bitfield tpo_rpf_sys_loopback */
#define rpf_tpo_rpf_sys_lbk_default 0x0

/* rx vl_inner_tpid[f:0] bitfield definitions
 * preprocessor definitions for the bitfield "vl_inner_tpid[f:0]".
 * port="pif_rpf_vl_inner_tpid_i[15:0]"
 */

/* register address for bitfield vl_inner_tpid[f:0] */
#define rpf_vl_inner_tpid_adr 0x00005284
/* bitmask for bitfield vl_inner_tpid[f:0] */
#define rpf_vl_inner_tpid_msk 0x0000ffff
/* inverted bitmask for bitfield vl_inner_tpid[f:0] */
#define rpf_vl_inner_tpid_mskn 0xffff0000
/* lower bit position of bitfield vl_inner_tpid[f:0] */
#define rpf_vl_inner_tpid_shift 0
/* width of bitfield vl_inner_tpid[f:0] */
#define rpf_vl_inner_tpid_width 16
/* default value of bitfield vl_inner_tpid[f:0] */
#define rpf_vl_inner_tpid_default 0x8100

/* rx vl_outer_tpid[f:0] bitfield definitions
 * preprocessor definitions for the bitfield "vl_outer_tpid[f:0]".
 * port="pif_rpf_vl_outer_tpid_i[15:0]"
 */

/* register address for bitfield vl_outer_tpid[f:0] */
#define rpf_vl_outer_tpid_adr 0x00005284
/* bitmask for bitfield vl_outer_tpid[f:0] */
#define rpf_vl_outer_tpid_msk 0xffff0000
/* inverted bitmask for bitfield vl_outer_tpid[f:0] */
#define rpf_vl_outer_tpid_mskn 0x0000ffff
/* lower bit position of bitfield vl_outer_tpid[f:0] */
#define rpf_vl_outer_tpid_shift 16
/* width of bitfield vl_outer_tpid[f:0] */
#define rpf_vl_outer_tpid_width 16
/* default value of bitfield vl_outer_tpid[f:0] */
#define rpf_vl_outer_tpid_default 0x88a8

/* rx vl_promis_mode bitfield definitions
 * preprocessor definitions for the bitfield "vl_promis_mode".
 * port="pif_rpf_vl_promis_mode_i"
 */

/* register address for bitfield vl_promis_mode */
#define rpf_vl_promis_mode_adr 0x00005280
/* bitmask for bitfield vl_promis_mode */
#define rpf_vl_promis_mode_msk 0x00000002
/* inverted bitmask for bitfield vl_promis_mode */
#define rpf_vl_promis_mode_mskn 0xfffffffd
/* lower bit position of bitfield vl_promis_mode */
#define rpf_vl_promis_mode_shift 1
/* width of bitfield vl_promis_mode */
#define rpf_vl_promis_mode_width 1
/* default value of bitfield vl_promis_mode */
#define rpf_vl_promis_mode_default 0x0

/* RX vl_accept_untagged_mode Bitfield Definitions
 * Preprocessor definitions for the bitfield "vl_accept_untagged_mode".
 * PORT="pif_rpf_vl_accept_untagged_i"
 */

/* Register address for bitfield vl_accept_untagged_mode */
#define rpf_vl_accept_untagged_mode_adr 0x00005280
/* Bitmask for bitfield vl_accept_untagged_mode */
#define rpf_vl_accept_untagged_mode_msk 0x00000004
/* Inverted bitmask for bitfield vl_accept_untagged_mode */
#define rpf_vl_accept_untagged_mode_mskn 0xFFFFFFFB
/* Lower bit position of bitfield vl_accept_untagged_mode */
#define rpf_vl_accept_untagged_mode_shift 2
/* Width of bitfield vl_accept_untagged_mode */
#define rpf_vl_accept_untagged_mode_width 1
/* Default value of bitfield vl_accept_untagged_mode */
#define rpf_vl_accept_untagged_mode_default 0x0

/* rX vl_untagged_act[2:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "vl_untagged_act[2:0]".
 * PORT="pif_rpf_vl_untagged_act_i[2:0]"
 */

/* Register address for bitfield vl_untagged_act[2:0] */
#define rpf_vl_untagged_act_adr 0x00005280
/* Bitmask for bitfield vl_untagged_act[2:0] */
#define rpf_vl_untagged_act_msk 0x00000038
/* Inverted bitmask for bitfield vl_untagged_act[2:0] */
#define rpf_vl_untagged_act_mskn 0xFFFFFFC7
/* Lower bit position of bitfield vl_untagged_act[2:0] */
#define rpf_vl_untagged_act_shift 3
/* Width of bitfield vl_untagged_act[2:0] */
#define rpf_vl_untagged_act_width 3
/* Default value of bitfield vl_untagged_act[2:0] */
#define rpf_vl_untagged_act_default 0x0

/* RX vl_en{F} Bitfield Definitions
 * Preprocessor definitions for the bitfield "vl_en{F}".
 * Parameter: filter {F} | stride size 0x4 | range [0, 15]
 * PORT="pif_rpf_vl_en_i[0]"
 */

/* Register address for bitfield vl_en{F} */
#define rpf_vl_en_f_adr(filter) (0x00005290 + (filter) * 0x4)
/* Bitmask for bitfield vl_en{F} */
#define rpf_vl_en_f_msk 0x80000000
/* Inverted bitmask for bitfield vl_en{F} */
#define rpf_vl_en_f_mskn 0x7FFFFFFF
/* Lower bit position of bitfield vl_en{F} */
#define rpf_vl_en_f_shift 31
/* Width of bitfield vl_en{F} */
#define rpf_vl_en_f_width 1
/* Default value of bitfield vl_en{F} */
#define rpf_vl_en_f_default 0x0

/* RX vl_act{F}[2:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "vl_act{F}[2:0]".
 * Parameter: filter {F} | stride size 0x4 | range [0, 15]
 * PORT="pif_rpf_vl_act0_i[2:0]"
 */

/* Register address for bitfield vl_act{F}[2:0] */
#define rpf_vl_act_f_adr(filter) (0x00005290 + (filter) * 0x4)
/* Bitmask for bitfield vl_act{F}[2:0] */
#define rpf_vl_act_f_msk 0x00070000
/* Inverted bitmask for bitfield vl_act{F}[2:0] */
#define rpf_vl_act_f_mskn 0xFFF8FFFF
/* Lower bit position of bitfield vl_act{F}[2:0] */
#define rpf_vl_act_f_shift 16
/* Width of bitfield vl_act{F}[2:0] */
#define rpf_vl_act_f_width 3
/* Default value of bitfield vl_act{F}[2:0] */
#define rpf_vl_act_f_default 0x0

/* RX vl_id{F}[B:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "vl_id{F}[B:0]".
 * Parameter: filter {F} | stride size 0x4 | range [0, 15]
 * PORT="pif_rpf_vl_id0_i[11:0]"
 */

/* Register address for bitfield vl_id{F}[B:0] */
#define rpf_vl_id_f_adr(filter) (0x00005290 + (filter) * 0x4)
/* Bitmask for bitfield vl_id{F}[B:0] */
#define rpf_vl_id_f_msk 0x00000FFF
/* Inverted bitmask for bitfield vl_id{F}[B:0] */
#define rpf_vl_id_f_mskn 0xFFFFF000
/* Lower bit position of bitfield vl_id{F}[B:0] */
#define rpf_vl_id_f_shift 0
/* Width of bitfield vl_id{F}[B:0] */
#define rpf_vl_id_f_width 12
/* Default value of bitfield vl_id{F}[B:0] */
#define rpf_vl_id_f_default 0x0

/* RX et_en{F} Bitfield Definitions
 * Preprocessor definitions for the bitfield "et_en{F}".
 * Parameter: filter {F} | stride size 0x4 | range [0, 15]
 * PORT="pif_rpf_et_en_i[0]"
 */

/* Register address for bitfield et_en{F} */
#define rpf_et_en_f_adr(filter) (0x00005300 + (filter) * 0x4)
/* Bitmask for bitfield et_en{F} */
#define rpf_et_en_f_msk 0x80000000
/* Inverted bitmask for bitfield et_en{F} */
#define rpf_et_en_f_mskn 0x7FFFFFFF
/* Lower bit position of bitfield et_en{F} */
#define rpf_et_en_f_shift 31
/* Width of bitfield et_en{F} */
#define rpf_et_en_f_width 1
/* Default value of bitfield et_en{F} */
#define rpf_et_en_f_default 0x0

/* rx et_en{f} bitfield definitions
 * preprocessor definitions for the bitfield "et_en{f}".
 * parameter: filter {f} | stride size 0x4 | range [0, 15]
 * port="pif_rpf_et_en_i[0]"
 */

/* register address for bitfield et_en{f} */
#define rpf_et_enf_adr(filter) (0x00005300 + (filter) * 0x4)
/* bitmask for bitfield et_en{f} */
#define rpf_et_enf_msk 0x80000000
/* inverted bitmask for bitfield et_en{f} */
#define rpf_et_enf_mskn 0x7fffffff
/* lower bit position of bitfield et_en{f} */
#define rpf_et_enf_shift 31
/* width of bitfield et_en{f} */
#define rpf_et_enf_width 1
/* default value of bitfield et_en{f} */
#define rpf_et_enf_default 0x0

/* rx et_up{f}_en bitfield definitions
 * preprocessor definitions for the bitfield "et_up{f}_en".
 * parameter: filter {f} | stride size 0x4 | range [0, 15]
 * port="pif_rpf_et_up_en_i[0]"
 */

/* register address for bitfield et_up{f}_en */
#define rpf_et_upfen_adr(filter) (0x00005300 + (filter) * 0x4)
/* bitmask for bitfield et_up{f}_en */
#define rpf_et_upfen_msk 0x40000000
/* inverted bitmask for bitfield et_up{f}_en */
#define rpf_et_upfen_mskn 0xbfffffff
/* lower bit position of bitfield et_up{f}_en */
#define rpf_et_upfen_shift 30
/* width of bitfield et_up{f}_en */
#define rpf_et_upfen_width 1
/* default value of bitfield et_up{f}_en */
#define rpf_et_upfen_default 0x0

/* rx et_rxq{f}_en bitfield definitions
 * preprocessor definitions for the bitfield "et_rxq{f}_en".
 * parameter: filter {f} | stride size 0x4 | range [0, 15]
 * port="pif_rpf_et_rxq_en_i[0]"
 */

/* register address for bitfield et_rxq{f}_en */
#define rpf_et_rxqfen_adr(filter) (0x00005300 + (filter) * 0x4)
/* bitmask for bitfield et_rxq{f}_en */
#define rpf_et_rxqfen_msk 0x20000000
/* inverted bitmask for bitfield et_rxq{f}_en */
#define rpf_et_rxqfen_mskn 0xdfffffff
/* lower bit position of bitfield et_rxq{f}_en */
#define rpf_et_rxqfen_shift 29
/* width of bitfield et_rxq{f}_en */
#define rpf_et_rxqfen_width 1
/* default value of bitfield et_rxq{f}_en */
#define rpf_et_rxqfen_default 0x0

/* rx et_up{f}[2:0] bitfield definitions
 * preprocessor definitions for the bitfield "et_up{f}[2:0]".
 * parameter: filter {f} | stride size 0x4 | range [0, 15]
 * port="pif_rpf_et_up0_i[2:0]"
 */

/* register address for bitfield et_up{f}[2:0] */
#define rpf_et_upf_adr(filter) (0x00005300 + (filter) * 0x4)
/* bitmask for bitfield et_up{f}[2:0] */
#define rpf_et_upf_msk 0x1c000000
/* inverted bitmask for bitfield et_up{f}[2:0] */
#define rpf_et_upf_mskn 0xe3ffffff
/* lower bit position of bitfield et_up{f}[2:0] */
#define rpf_et_upf_shift 26
/* width of bitfield et_up{f}[2:0] */
#define rpf_et_upf_width 3
/* default value of bitfield et_up{f}[2:0] */
#define rpf_et_upf_default 0x0

/* rx et_rxq{f}[4:0] bitfield definitions
 * preprocessor definitions for the bitfield "et_rxq{f}[4:0]".
 * parameter: filter {f} | stride size 0x4 | range [0, 15]
 * port="pif_rpf_et_rxq0_i[4:0]"
 */

/* register address for bitfield et_rxq{f}[4:0] */
#define rpf_et_rxqf_adr(filter) (0x00005300 + (filter) * 0x4)
/* bitmask for bitfield et_rxq{f}[4:0] */
#define rpf_et_rxqf_msk 0x01f00000
/* inverted bitmask for bitfield et_rxq{f}[4:0] */
#define rpf_et_rxqf_mskn 0xfe0fffff
/* lower bit position of bitfield et_rxq{f}[4:0] */
#define rpf_et_rxqf_shift 20
/* width of bitfield et_rxq{f}[4:0] */
#define rpf_et_rxqf_width 5
/* default value of bitfield et_rxq{f}[4:0] */
#define rpf_et_rxqf_default 0x0

/* rx et_mng_rxq{f} bitfield definitions
 * preprocessor definitions for the bitfield "et_mng_rxq{f}".
 * parameter: filter {f} | stride size 0x4 | range [0, 15]
 * port="pif_rpf_et_mng_rxq_i[0]"
 */

/* register address for bitfield et_mng_rxq{f} */
#define rpf_et_mng_rxqf_adr(filter) (0x00005300 + (filter) * 0x4)
/* bitmask for bitfield et_mng_rxq{f} */
#define rpf_et_mng_rxqf_msk 0x00080000
/* inverted bitmask for bitfield et_mng_rxq{f} */
#define rpf_et_mng_rxqf_mskn 0xfff7ffff
/* lower bit position of bitfield et_mng_rxq{f} */
#define rpf_et_mng_rxqf_shift 19
/* width of bitfield et_mng_rxq{f} */
#define rpf_et_mng_rxqf_width 1
/* default value of bitfield et_mng_rxq{f} */
#define rpf_et_mng_rxqf_default 0x0

/* rx et_act{f}[2:0] bitfield definitions
 * preprocessor definitions for the bitfield "et_act{f}[2:0]".
 * parameter: filter {f} | stride size 0x4 | range [0, 15]
 * port="pif_rpf_et_act0_i[2:0]"
 */

/* register address for bitfield et_act{f}[2:0] */
#define rpf_et_actf_adr(filter) (0x00005300 + (filter) * 0x4)
/* bitmask for bitfield et_act{f}[2:0] */
#define rpf_et_actf_msk 0x00070000
/* inverted bitmask for bitfield et_act{f}[2:0] */
#define rpf_et_actf_mskn 0xfff8ffff
/* lower bit position of bitfield et_act{f}[2:0] */
#define rpf_et_actf_shift 16
/* width of bitfield et_act{f}[2:0] */
#define rpf_et_actf_width 3
/* default value of bitfield et_act{f}[2:0] */
#define rpf_et_actf_default 0x0

/* rx et_val{f}[f:0] bitfield definitions
 * preprocessor definitions for the bitfield "et_val{f}[f:0]".
 * parameter: filter {f} | stride size 0x4 | range [0, 15]
 * port="pif_rpf_et_val0_i[15:0]"
 */

/* register address for bitfield et_val{f}[f:0] */
#define rpf_et_valf_adr(filter) (0x00005300 + (filter) * 0x4)
/* bitmask for bitfield et_val{f}[f:0] */
#define rpf_et_valf_msk 0x0000ffff
/* inverted bitmask for bitfield et_val{f}[f:0] */
#define rpf_et_valf_mskn 0xffff0000
/* lower bit position of bitfield et_val{f}[f:0] */
#define rpf_et_valf_shift 0
/* width of bitfield et_val{f}[f:0] */
#define rpf_et_valf_width 16
/* default value of bitfield et_val{f}[f:0] */
#define rpf_et_valf_default 0x0

/* rx ipv4_chk_en bitfield definitions
 * preprocessor definitions for the bitfield "ipv4_chk_en".
 * port="pif_rpo_ipv4_chk_en_i"
 */

/* register address for bitfield ipv4_chk_en */
#define rpo_ipv4chk_en_adr 0x00005580
/* bitmask for bitfield ipv4_chk_en */
#define rpo_ipv4chk_en_msk 0x00000002
/* inverted bitmask for bitfield ipv4_chk_en */
#define rpo_ipv4chk_en_mskn 0xfffffffd
/* lower bit position of bitfield ipv4_chk_en */
#define rpo_ipv4chk_en_shift 1
/* width of bitfield ipv4_chk_en */
#define rpo_ipv4chk_en_width 1
/* default value of bitfield ipv4_chk_en */
#define rpo_ipv4chk_en_default 0x0

/* rx desc{d}_vl_strip bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_vl_strip".
 * parameter: descriptor {d} | stride size 0x20 | range [0, 31]
 * port="pif_rpo_desc_vl_strip_i[0]"
 */

/* register address for bitfield desc{d}_vl_strip */
#define rpo_descdvl_strip_adr(descriptor) (0x00005b08 + (descriptor) * 0x20)
/* bitmask for bitfield desc{d}_vl_strip */
#define rpo_descdvl_strip_msk 0x20000000
/* inverted bitmask for bitfield desc{d}_vl_strip */
#define rpo_descdvl_strip_mskn 0xdfffffff
/* lower bit position of bitfield desc{d}_vl_strip */
#define rpo_descdvl_strip_shift 29
/* width of bitfield desc{d}_vl_strip */
#define rpo_descdvl_strip_width 1
/* default value of bitfield desc{d}_vl_strip */
#define rpo_descdvl_strip_default 0x0

/* rx l4_chk_en bitfield definitions
 * preprocessor definitions for the bitfield "l4_chk_en".
 * port="pif_rpo_l4_chk_en_i"
 */

/* register address for bitfield l4_chk_en */
#define rpol4chk_en_adr 0x00005580
/* bitmask for bitfield l4_chk_en */
#define rpol4chk_en_msk 0x00000001
/* inverted bitmask for bitfield l4_chk_en */
#define rpol4chk_en_mskn 0xfffffffe
/* lower bit position of bitfield l4_chk_en */
#define rpol4chk_en_shift 0
/* width of bitfield l4_chk_en */
#define rpol4chk_en_width 1
/* default value of bitfield l4_chk_en */
#define rpol4chk_en_default 0x0

/* rx reg_res_dsbl bitfield definitions
 * preprocessor definitions for the bitfield "reg_res_dsbl".
 * port="pif_rx_reg_res_dsbl_i"
 */

/* register address for bitfield reg_res_dsbl */
#define rx_reg_res_dsbl_adr 0x00005000
/* bitmask for bitfield reg_res_dsbl */
#define rx_reg_res_dsbl_msk 0x20000000
/* inverted bitmask for bitfield reg_res_dsbl */
#define rx_reg_res_dsbl_mskn 0xdfffffff
/* lower bit position of bitfield reg_res_dsbl */
#define rx_reg_res_dsbl_shift 29
/* width of bitfield reg_res_dsbl */
#define rx_reg_res_dsbl_width 1
/* default value of bitfield reg_res_dsbl */
#define rx_reg_res_dsbl_default 0x1

/* tx dca{d}_cpuid[7:0] bitfield definitions
 * preprocessor definitions for the bitfield "dca{d}_cpuid[7:0]".
 * parameter: dca {d} | stride size 0x4 | range [0, 31]
 * port="pif_tdm_dca0_cpuid_i[7:0]"
 */

/* register address for bitfield dca{d}_cpuid[7:0] */
#define tdm_dcadcpuid_adr(dca) (0x00008400 + (dca) * 0x4)
/* bitmask for bitfield dca{d}_cpuid[7:0] */
#define tdm_dcadcpuid_msk 0x000000ff
/* inverted bitmask for bitfield dca{d}_cpuid[7:0] */
#define tdm_dcadcpuid_mskn 0xffffff00
/* lower bit position of bitfield dca{d}_cpuid[7:0] */
#define tdm_dcadcpuid_shift 0
/* width of bitfield dca{d}_cpuid[7:0] */
#define tdm_dcadcpuid_width 8
/* default value of bitfield dca{d}_cpuid[7:0] */
#define tdm_dcadcpuid_default 0x0

/* tx lso_en[1f:0] bitfield definitions
 * preprocessor definitions for the bitfield "lso_en[1f:0]".
 * port="pif_tdm_lso_en_i[31:0]"
 */

/* register address for bitfield lso_en[1f:0] */
#define tdm_lso_en_adr 0x00007810
/* bitmask for bitfield lso_en[1f:0] */
#define tdm_lso_en_msk 0xffffffff
/* inverted bitmask for bitfield lso_en[1f:0] */
#define tdm_lso_en_mskn 0x00000000
/* lower bit position of bitfield lso_en[1f:0] */
#define tdm_lso_en_shift 0
/* width of bitfield lso_en[1f:0] */
#define tdm_lso_en_width 32
/* default value of bitfield lso_en[1f:0] */
#define tdm_lso_en_default 0x0

/* tx dca_en bitfield definitions
 * preprocessor definitions for the bitfield "dca_en".
 * port="pif_tdm_dca_en_i"
 */

/* register address for bitfield dca_en */
#define tdm_dca_en_adr 0x00008480
/* bitmask for bitfield dca_en */
#define tdm_dca_en_msk 0x80000000
/* inverted bitmask for bitfield dca_en */
#define tdm_dca_en_mskn 0x7fffffff
/* lower bit position of bitfield dca_en */
#define tdm_dca_en_shift 31
/* width of bitfield dca_en */
#define tdm_dca_en_width 1
/* default value of bitfield dca_en */
#define tdm_dca_en_default 0x1

/* tx dca_mode[3:0] bitfield definitions
 * preprocessor definitions for the bitfield "dca_mode[3:0]".
 * port="pif_tdm_dca_mode_i[3:0]"
 */

/* register address for bitfield dca_mode[3:0] */
#define tdm_dca_mode_adr 0x00008480
/* bitmask for bitfield dca_mode[3:0] */
#define tdm_dca_mode_msk 0x0000000f
/* inverted bitmask for bitfield dca_mode[3:0] */
#define tdm_dca_mode_mskn 0xfffffff0
/* lower bit position of bitfield dca_mode[3:0] */
#define tdm_dca_mode_shift 0
/* width of bitfield dca_mode[3:0] */
#define tdm_dca_mode_width 4
/* default value of bitfield dca_mode[3:0] */
#define tdm_dca_mode_default 0x0

/* tx dca{d}_desc_en bitfield definitions
 * preprocessor definitions for the bitfield "dca{d}_desc_en".
 * parameter: dca {d} | stride size 0x4 | range [0, 31]
 * port="pif_tdm_dca_desc_en_i[0]"
 */

/* register address for bitfield dca{d}_desc_en */
#define tdm_dcaddesc_en_adr(dca) (0x00008400 + (dca) * 0x4)
/* bitmask for bitfield dca{d}_desc_en */
#define tdm_dcaddesc_en_msk 0x80000000
/* inverted bitmask for bitfield dca{d}_desc_en */
#define tdm_dcaddesc_en_mskn 0x7fffffff
/* lower bit position of bitfield dca{d}_desc_en */
#define tdm_dcaddesc_en_shift 31
/* width of bitfield dca{d}_desc_en */
#define tdm_dcaddesc_en_width 1
/* default value of bitfield dca{d}_desc_en */
#define tdm_dcaddesc_en_default 0x0

/* tx desc{d}_en bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_en".
 * parameter: descriptor {d} | stride size 0x40 | range [0, 31]
 * port="pif_tdm_desc_en_i[0]"
 */

/* register address for bitfield desc{d}_en */
#define tdm_descden_adr(descriptor) (0x00007c08 + (descriptor) * 0x40)
/* bitmask for bitfield desc{d}_en */
#define tdm_descden_msk 0x80000000
/* inverted bitmask for bitfield desc{d}_en */
#define tdm_descden_mskn 0x7fffffff
/* lower bit position of bitfield desc{d}_en */
#define tdm_descden_shift 31
/* width of bitfield desc{d}_en */
#define tdm_descden_width 1
/* default value of bitfield desc{d}_en */
#define tdm_descden_default 0x0

/* tx desc{d}_hd[c:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_hd[c:0]".
 * parameter: descriptor {d} | stride size 0x40 | range [0, 31]
 * port="tdm_pif_desc0_hd_o[12:0]"
 */

/* register address for bitfield desc{d}_hd[c:0] */
#define tdm_descdhd_adr(descriptor) (0x00007c0c + (descriptor) * 0x40)
/* bitmask for bitfield desc{d}_hd[c:0] */
#define tdm_descdhd_msk 0x00001fff
/* inverted bitmask for bitfield desc{d}_hd[c:0] */
#define tdm_descdhd_mskn 0xffffe000
/* lower bit position of bitfield desc{d}_hd[c:0] */
#define tdm_descdhd_shift 0
/* width of bitfield desc{d}_hd[c:0] */
#define tdm_descdhd_width 13

/* tx desc{d}_len[9:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_len[9:0]".
 * parameter: descriptor {d} | stride size 0x40 | range [0, 31]
 * port="pif_tdm_desc0_len_i[9:0]"
 */

/* register address for bitfield desc{d}_len[9:0] */
#define tdm_descdlen_adr(descriptor) (0x00007c08 + (descriptor) * 0x40)
/* bitmask for bitfield desc{d}_len[9:0] */
#define tdm_descdlen_msk 0x00001ff8
/* inverted bitmask for bitfield desc{d}_len[9:0] */
#define tdm_descdlen_mskn 0xffffe007
/* lower bit position of bitfield desc{d}_len[9:0] */
#define tdm_descdlen_shift 3
/* width of bitfield desc{d}_len[9:0] */
#define tdm_descdlen_width 10
/* default value of bitfield desc{d}_len[9:0] */
#define tdm_descdlen_default 0x0

/* tx int_desc_wrb_en bitfield definitions
 * preprocessor definitions for the bitfield "int_desc_wrb_en".
 * port="pif_tdm_int_desc_wrb_en_i"
 */

/* register address for bitfield int_desc_wrb_en */
#define tdm_int_desc_wrb_en_adr 0x00007b40
/* bitmask for bitfield int_desc_wrb_en */
#define tdm_int_desc_wrb_en_msk 0x00000002
/* inverted bitmask for bitfield int_desc_wrb_en */
#define tdm_int_desc_wrb_en_mskn 0xfffffffd
/* lower bit position of bitfield int_desc_wrb_en */
#define tdm_int_desc_wrb_en_shift 1
/* width of bitfield int_desc_wrb_en */
#define tdm_int_desc_wrb_en_width 1
/* default value of bitfield int_desc_wrb_en */
#define tdm_int_desc_wrb_en_default 0x0

/* tx desc{d}_wrb_thresh[6:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc{d}_wrb_thresh[6:0]".
 * parameter: descriptor {d} | stride size 0x40 | range [0, 31]
 * port="pif_tdm_desc0_wrb_thresh_i[6:0]"
 */

/* register address for bitfield desc{d}_wrb_thresh[6:0] */
#define tdm_descdwrb_thresh_adr(descriptor) (0x00007c18 + (descriptor) * 0x40)
/* bitmask for bitfield desc{d}_wrb_thresh[6:0] */
#define tdm_descdwrb_thresh_msk 0x00007f00
/* inverted bitmask for bitfield desc{d}_wrb_thresh[6:0] */
#define tdm_descdwrb_thresh_mskn 0xffff80ff
/* lower bit position of bitfield desc{d}_wrb_thresh[6:0] */
#define tdm_descdwrb_thresh_shift 8
/* width of bitfield desc{d}_wrb_thresh[6:0] */
#define tdm_descdwrb_thresh_width 7
/* default value of bitfield desc{d}_wrb_thresh[6:0] */
#define tdm_descdwrb_thresh_default 0x0

/* tx lso_tcp_flag_first[b:0] bitfield definitions
 * preprocessor definitions for the bitfield "lso_tcp_flag_first[b:0]".
 * port="pif_thm_lso_tcp_flag_first_i[11:0]"
 */

/* register address for bitfield lso_tcp_flag_first[b:0] */
#define thm_lso_tcp_flag_first_adr 0x00007820
/* bitmask for bitfield lso_tcp_flag_first[b:0] */
#define thm_lso_tcp_flag_first_msk 0x00000fff
/* inverted bitmask for bitfield lso_tcp_flag_first[b:0] */
#define thm_lso_tcp_flag_first_mskn 0xfffff000
/* lower bit position of bitfield lso_tcp_flag_first[b:0] */
#define thm_lso_tcp_flag_first_shift 0
/* width of bitfield lso_tcp_flag_first[b:0] */
#define thm_lso_tcp_flag_first_width 12
/* default value of bitfield lso_tcp_flag_first[b:0] */
#define thm_lso_tcp_flag_first_default 0x0

/* tx lso_tcp_flag_last[b:0] bitfield definitions
 * preprocessor definitions for the bitfield "lso_tcp_flag_last[b:0]".
 * port="pif_thm_lso_tcp_flag_last_i[11:0]"
 */

/* register address for bitfield lso_tcp_flag_last[b:0] */
#define thm_lso_tcp_flag_last_adr 0x00007824
/* bitmask for bitfield lso_tcp_flag_last[b:0] */
#define thm_lso_tcp_flag_last_msk 0x00000fff
/* inverted bitmask for bitfield lso_tcp_flag_last[b:0] */
#define thm_lso_tcp_flag_last_mskn 0xfffff000
/* lower bit position of bitfield lso_tcp_flag_last[b:0] */
#define thm_lso_tcp_flag_last_shift 0
/* width of bitfield lso_tcp_flag_last[b:0] */
#define thm_lso_tcp_flag_last_width 12
/* default value of bitfield lso_tcp_flag_last[b:0] */
#define thm_lso_tcp_flag_last_default 0x0

/* tx lso_tcp_flag_mid[b:0] bitfield definitions
 * preprocessor definitions for the bitfield "lso_tcp_flag_mid[b:0]".
 * port="pif_thm_lso_tcp_flag_mid_i[11:0]"
 */

/* Register address for bitfield lro_rsc_max[1F:0] */
#define rpo_lro_rsc_max_adr 0x00005598
/* Bitmask for bitfield lro_rsc_max[1F:0] */
#define rpo_lro_rsc_max_msk 0xFFFFFFFF
/* Inverted bitmask for bitfield lro_rsc_max[1F:0] */
#define rpo_lro_rsc_max_mskn 0x00000000
/* Lower bit position of bitfield lro_rsc_max[1F:0] */
#define rpo_lro_rsc_max_shift 0
/* Width of bitfield lro_rsc_max[1F:0] */
#define rpo_lro_rsc_max_width 32
/* Default value of bitfield lro_rsc_max[1F:0] */
#define rpo_lro_rsc_max_default 0x0

/* RX lro_en[1F:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "lro_en[1F:0]".
 * PORT="pif_rpo_lro_en_i[31:0]"
 */

/* Register address for bitfield lro_en[1F:0] */
#define rpo_lro_en_adr 0x00005590
/* Bitmask for bitfield lro_en[1F:0] */
#define rpo_lro_en_msk 0xFFFFFFFF
/* Inverted bitmask for bitfield lro_en[1F:0] */
#define rpo_lro_en_mskn 0x00000000
/* Lower bit position of bitfield lro_en[1F:0] */
#define rpo_lro_en_shift 0
/* Width of bitfield lro_en[1F:0] */
#define rpo_lro_en_width 32
/* Default value of bitfield lro_en[1F:0] */
#define rpo_lro_en_default 0x0

/* RX lro_ptopt_en Bitfield Definitions
 * Preprocessor definitions for the bitfield "lro_ptopt_en".
 * PORT="pif_rpo_lro_ptopt_en_i"
 */

/* Register address for bitfield lro_ptopt_en */
#define rpo_lro_ptopt_en_adr 0x00005594
/* Bitmask for bitfield lro_ptopt_en */
#define rpo_lro_ptopt_en_msk 0x00008000
/* Inverted bitmask for bitfield lro_ptopt_en */
#define rpo_lro_ptopt_en_mskn 0xFFFF7FFF
/* Lower bit position of bitfield lro_ptopt_en */
#define rpo_lro_ptopt_en_shift 15
/* Width of bitfield lro_ptopt_en */
#define rpo_lro_ptopt_en_width 1
/* Default value of bitfield lro_ptopt_en */
#define rpo_lro_ptopt_en_defalt 0x1

/* RX lro_q_ses_lmt Bitfield Definitions
 * Preprocessor definitions for the bitfield "lro_q_ses_lmt".
 * PORT="pif_rpo_lro_q_ses_lmt_i[1:0]"
 */

/* Register address for bitfield lro_q_ses_lmt */
#define rpo_lro_qses_lmt_adr 0x00005594
/* Bitmask for bitfield lro_q_ses_lmt */
#define rpo_lro_qses_lmt_msk 0x00003000
/* Inverted bitmask for bitfield lro_q_ses_lmt */
#define rpo_lro_qses_lmt_mskn 0xFFFFCFFF
/* Lower bit position of bitfield lro_q_ses_lmt */
#define rpo_lro_qses_lmt_shift 12
/* Width of bitfield lro_q_ses_lmt */
#define rpo_lro_qses_lmt_width 2
/* Default value of bitfield lro_q_ses_lmt */
#define rpo_lro_qses_lmt_default 0x1

/* RX lro_tot_dsc_lmt[1:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "lro_tot_dsc_lmt[1:0]".
 * PORT="pif_rpo_lro_tot_dsc_lmt_i[1:0]"
 */

/* Register address for bitfield lro_tot_dsc_lmt[1:0] */
#define rpo_lro_tot_dsc_lmt_adr 0x00005594
/* Bitmask for bitfield lro_tot_dsc_lmt[1:0] */
#define rpo_lro_tot_dsc_lmt_msk 0x00000060
/* Inverted bitmask for bitfield lro_tot_dsc_lmt[1:0] */
#define rpo_lro_tot_dsc_lmt_mskn 0xFFFFFF9F
/* Lower bit position of bitfield lro_tot_dsc_lmt[1:0] */
#define rpo_lro_tot_dsc_lmt_shift 5
/* Width of bitfield lro_tot_dsc_lmt[1:0] */
#define rpo_lro_tot_dsc_lmt_width 2
/* Default value of bitfield lro_tot_dsc_lmt[1:0] */
#define rpo_lro_tot_dsc_lmt_defalt 0x1

/* RX lro_pkt_min[4:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "lro_pkt_min[4:0]".
 * PORT="pif_rpo_lro_pkt_min_i[4:0]"
 */

/* Register address for bitfield lro_pkt_min[4:0] */
#define rpo_lro_pkt_min_adr 0x00005594
/* Bitmask for bitfield lro_pkt_min[4:0] */
#define rpo_lro_pkt_min_msk 0x0000001F
/* Inverted bitmask for bitfield lro_pkt_min[4:0] */
#define rpo_lro_pkt_min_mskn 0xFFFFFFE0
/* Lower bit position of bitfield lro_pkt_min[4:0] */
#define rpo_lro_pkt_min_shift 0
/* Width of bitfield lro_pkt_min[4:0] */
#define rpo_lro_pkt_min_width 5
/* Default value of bitfield lro_pkt_min[4:0] */
#define rpo_lro_pkt_min_default 0x8

/* Width of bitfield lro{L}_des_max[1:0] */
#define rpo_lro_ldes_max_width 2
/* Default value of bitfield lro{L}_des_max[1:0] */
#define rpo_lro_ldes_max_default 0x0

/* RX lro_tb_div[11:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "lro_tb_div[11:0]".
 * PORT="pif_rpo_lro_tb_div_i[11:0]"
 */

/* Register address for bitfield lro_tb_div[11:0] */
#define rpo_lro_tb_div_adr 0x00005620
/* Bitmask for bitfield lro_tb_div[11:0] */
#define rpo_lro_tb_div_msk 0xFFF00000
/* Inverted bitmask for bitfield lro_tb_div[11:0] */
#define rpo_lro_tb_div_mskn 0x000FFFFF
/* Lower bit position of bitfield lro_tb_div[11:0] */
#define rpo_lro_tb_div_shift 20
/* Width of bitfield lro_tb_div[11:0] */
#define rpo_lro_tb_div_width 12
/* Default value of bitfield lro_tb_div[11:0] */
#define rpo_lro_tb_div_default 0xC35

/* RX lro_ina_ival[9:0] Bitfield Definitions
 *   Preprocessor definitions for the bitfield "lro_ina_ival[9:0]".
 *   PORT="pif_rpo_lro_ina_ival_i[9:0]"
 */

/* Register address for bitfield lro_ina_ival[9:0] */
#define rpo_lro_ina_ival_adr 0x00005620
/* Bitmask for bitfield lro_ina_ival[9:0] */
#define rpo_lro_ina_ival_msk 0x000FFC00
/* Inverted bitmask for bitfield lro_ina_ival[9:0] */
#define rpo_lro_ina_ival_mskn 0xFFF003FF
/* Lower bit position of bitfield lro_ina_ival[9:0] */
#define rpo_lro_ina_ival_shift 10
/* Width of bitfield lro_ina_ival[9:0] */
#define rpo_lro_ina_ival_width 10
/* Default value of bitfield lro_ina_ival[9:0] */
#define rpo_lro_ina_ival_default 0xA

/* RX lro_max_ival[9:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "lro_max_ival[9:0]".
 * PORT="pif_rpo_lro_max_ival_i[9:0]"
 */

/* Register address for bitfield lro_max_ival[9:0] */
#define rpo_lro_max_ival_adr 0x00005620
/* Bitmask for bitfield lro_max_ival[9:0] */
#define rpo_lro_max_ival_msk 0x000003FF
/* Inverted bitmask for bitfield lro_max_ival[9:0] */
#define rpo_lro_max_ival_mskn 0xFFFFFC00
/* Lower bit position of bitfield lro_max_ival[9:0] */
#define rpo_lro_max_ival_shift 0
/* Width of bitfield lro_max_ival[9:0] */
#define rpo_lro_max_ival_width 10
/* Default value of bitfield lro_max_ival[9:0] */
#define rpo_lro_max_ival_default 0x19

/* TX dca{D}_cpuid[7:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "dca{D}_cpuid[7:0]".
 * Parameter: DCA {D} | stride size 0x4 | range [0, 31]
 * PORT="pif_tdm_dca0_cpuid_i[7:0]"
 */

/* Register address for bitfield dca{D}_cpuid[7:0] */
#define tdm_dca_dcpuid_adr(dca) (0x00008400 + (dca) * 0x4)
/* Bitmask for bitfield dca{D}_cpuid[7:0] */
#define tdm_dca_dcpuid_msk 0x000000FF
/* Inverted bitmask for bitfield dca{D}_cpuid[7:0] */
#define tdm_dca_dcpuid_mskn 0xFFFFFF00
/* Lower bit position of bitfield dca{D}_cpuid[7:0] */
#define tdm_dca_dcpuid_shift 0
/* Width of bitfield dca{D}_cpuid[7:0] */
#define tdm_dca_dcpuid_width 8
/* Default value of bitfield dca{D}_cpuid[7:0] */
#define tdm_dca_dcpuid_default 0x0

/* TX dca{D}_desc_en Bitfield Definitions
 * Preprocessor definitions for the bitfield "dca{D}_desc_en".
 * Parameter: DCA {D} | stride size 0x4 | range [0, 31]
 * PORT="pif_tdm_dca_desc_en_i[0]"
 */

/* Register address for bitfield dca{D}_desc_en */
#define tdm_dca_ddesc_en_adr(dca) (0x00008400 + (dca) * 0x4)
/* Bitmask for bitfield dca{D}_desc_en */
#define tdm_dca_ddesc_en_msk 0x80000000
/* Inverted bitmask for bitfield dca{D}_desc_en */
#define tdm_dca_ddesc_en_mskn 0x7FFFFFFF
/* Lower bit position of bitfield dca{D}_desc_en */
#define tdm_dca_ddesc_en_shift 31
/* Width of bitfield dca{D}_desc_en */
#define tdm_dca_ddesc_en_width 1
/* Default value of bitfield dca{D}_desc_en */
#define tdm_dca_ddesc_en_default 0x0

/* TX desc{D}_en Bitfield Definitions
 * Preprocessor definitions for the bitfield "desc{D}_en".
 * Parameter: descriptor {D} | stride size 0x40 | range [0, 31]
 * PORT="pif_tdm_desc_en_i[0]"
 */

/* Register address for bitfield desc{D}_en */
#define tdm_desc_den_adr(descriptor) (0x00007C08 + (descriptor) * 0x40)
/* Bitmask for bitfield desc{D}_en */
#define tdm_desc_den_msk 0x80000000
/* Inverted bitmask for bitfield desc{D}_en */
#define tdm_desc_den_mskn 0x7FFFFFFF
/* Lower bit position of bitfield desc{D}_en */
#define tdm_desc_den_shift 31
/* Width of bitfield desc{D}_en */
#define tdm_desc_den_width 1
/* Default value of bitfield desc{D}_en */
#define tdm_desc_den_default 0x0

/* TX desc{D}_hd[C:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "desc{D}_hd[C:0]".
 * Parameter: descriptor {D} | stride size 0x40 | range [0, 31]
 * PORT="tdm_pif_desc0_hd_o[12:0]"
 */

/* Register address for bitfield desc{D}_hd[C:0] */
#define tdm_desc_dhd_adr(descriptor) (0x00007C0C + (descriptor) * 0x40)
/* Bitmask for bitfield desc{D}_hd[C:0] */
#define tdm_desc_dhd_msk 0x00001FFF
/* Inverted bitmask for bitfield desc{D}_hd[C:0] */
#define tdm_desc_dhd_mskn 0xFFFFE000
/* Lower bit position of bitfield desc{D}_hd[C:0] */
#define tdm_desc_dhd_shift 0
/* Width of bitfield desc{D}_hd[C:0] */
#define tdm_desc_dhd_width 13

/* TX desc{D}_len[9:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "desc{D}_len[9:0]".
 * Parameter: descriptor {D} | stride size 0x40 | range [0, 31]
 * PORT="pif_tdm_desc0_len_i[9:0]"
 */

/* Register address for bitfield desc{D}_len[9:0] */
#define tdm_desc_dlen_adr(descriptor) (0x00007C08 + (descriptor) * 0x40)
/* Bitmask for bitfield desc{D}_len[9:0] */
#define tdm_desc_dlen_msk 0x00001FF8
/* Inverted bitmask for bitfield desc{D}_len[9:0] */
#define tdm_desc_dlen_mskn 0xFFFFE007
/* Lower bit position of bitfield desc{D}_len[9:0] */
#define tdm_desc_dlen_shift 3
/* Width of bitfield desc{D}_len[9:0] */
#define tdm_desc_dlen_width 10
/* Default value of bitfield desc{D}_len[9:0] */
#define tdm_desc_dlen_default 0x0

/* TX desc{D}_wrb_thresh[6:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "desc{D}_wrb_thresh[6:0]".
 * Parameter: descriptor {D} | stride size 0x40 | range [0, 31]
 * PORT="pif_tdm_desc0_wrb_thresh_i[6:0]"
 */

/* Register address for bitfield desc{D}_wrb_thresh[6:0] */
#define tdm_desc_dwrb_thresh_adr(descriptor) \
	(0x00007C18 + (descriptor) * 0x40)
/* Bitmask for bitfield desc{D}_wrb_thresh[6:0] */
#define tdm_desc_dwrb_thresh_msk 0x00007F00
/* Inverted bitmask for bitfield desc{D}_wrb_thresh[6:0] */
#define tdm_desc_dwrb_thresh_mskn 0xFFFF80FF
/* Lower bit position of bitfield desc{D}_wrb_thresh[6:0] */
#define tdm_desc_dwrb_thresh_shift 8
/* Width of bitfield desc{D}_wrb_thresh[6:0] */
#define tdm_desc_dwrb_thresh_width 7
/* Default value of bitfield desc{D}_wrb_thresh[6:0] */
#define tdm_desc_dwrb_thresh_default 0x0

/* TX tdm_int_mod_en Bitfield Definitions
 * Preprocessor definitions for the bitfield "tdm_int_mod_en".
 * PORT="pif_tdm_int_mod_en_i"
 */

/* Register address for bitfield tdm_int_mod_en */
#define tdm_int_mod_en_adr 0x00007B40
/* Bitmask for bitfield tdm_int_mod_en */
#define tdm_int_mod_en_msk 0x00000010
/* Inverted bitmask for bitfield tdm_int_mod_en */
#define tdm_int_mod_en_mskn 0xFFFFFFEF
/* Lower bit position of bitfield tdm_int_mod_en */
#define tdm_int_mod_en_shift 4
/* Width of bitfield tdm_int_mod_en */
#define tdm_int_mod_en_width 1
/* Default value of bitfield tdm_int_mod_en */
#define tdm_int_mod_en_default 0x0

/* TX lso_tcp_flag_mid[B:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "lso_tcp_flag_mid[B:0]".
 * PORT="pif_thm_lso_tcp_flag_mid_i[11:0]"
 */
/* register address for bitfield lso_tcp_flag_mid[b:0] */
#define thm_lso_tcp_flag_mid_adr 0x00007820
/* bitmask for bitfield lso_tcp_flag_mid[b:0] */
#define thm_lso_tcp_flag_mid_msk 0x0fff0000
/* inverted bitmask for bitfield lso_tcp_flag_mid[b:0] */
#define thm_lso_tcp_flag_mid_mskn 0xf000ffff
/* lower bit position of bitfield lso_tcp_flag_mid[b:0] */
#define thm_lso_tcp_flag_mid_shift 16
/* width of bitfield lso_tcp_flag_mid[b:0] */
#define thm_lso_tcp_flag_mid_width 12
/* default value of bitfield lso_tcp_flag_mid[b:0] */
#define thm_lso_tcp_flag_mid_default 0x0

/* tx tx_buf_en bitfield definitions
 * preprocessor definitions for the bitfield "tx_buf_en".
 * port="pif_tpb_tx_buf_en_i"
 */

/* register address for bitfield tx_buf_en */
#define tpb_tx_buf_en_adr 0x00007900
/* bitmask for bitfield tx_buf_en */
#define tpb_tx_buf_en_msk 0x00000001
/* inverted bitmask for bitfield tx_buf_en */
#define tpb_tx_buf_en_mskn 0xfffffffe
/* lower bit position of bitfield tx_buf_en */
#define tpb_tx_buf_en_shift 0
/* width of bitfield tx_buf_en */
#define tpb_tx_buf_en_width 1
/* default value of bitfield tx_buf_en */
#define tpb_tx_buf_en_default 0x0

/* tx tx{b}_hi_thresh[c:0] bitfield definitions
 * preprocessor definitions for the bitfield "tx{b}_hi_thresh[c:0]".
 * parameter: buffer {b} | stride size 0x10 | range [0, 7]
 * port="pif_tpb_tx0_hi_thresh_i[12:0]"
 */

/* register address for bitfield tx{b}_hi_thresh[c:0] */
#define tpb_txbhi_thresh_adr(buffer) (0x00007914 + (buffer) * 0x10)
/* bitmask for bitfield tx{b}_hi_thresh[c:0] */
#define tpb_txbhi_thresh_msk 0x1fff0000
/* inverted bitmask for bitfield tx{b}_hi_thresh[c:0] */
#define tpb_txbhi_thresh_mskn 0xe000ffff
/* lower bit position of bitfield tx{b}_hi_thresh[c:0] */
#define tpb_txbhi_thresh_shift 16
/* width of bitfield tx{b}_hi_thresh[c:0] */
#define tpb_txbhi_thresh_width 13
/* default value of bitfield tx{b}_hi_thresh[c:0] */
#define tpb_txbhi_thresh_default 0x0

/* tx tx{b}_lo_thresh[c:0] bitfield definitions
 * preprocessor definitions for the bitfield "tx{b}_lo_thresh[c:0]".
 * parameter: buffer {b} | stride size 0x10 | range [0, 7]
 * port="pif_tpb_tx0_lo_thresh_i[12:0]"
 */

/* register address for bitfield tx{b}_lo_thresh[c:0] */
#define tpb_txblo_thresh_adr(buffer) (0x00007914 + (buffer) * 0x10)
/* bitmask for bitfield tx{b}_lo_thresh[c:0] */
#define tpb_txblo_thresh_msk 0x00001fff
/* inverted bitmask for bitfield tx{b}_lo_thresh[c:0] */
#define tpb_txblo_thresh_mskn 0xffffe000
/* lower bit position of bitfield tx{b}_lo_thresh[c:0] */
#define tpb_txblo_thresh_shift 0
/* width of bitfield tx{b}_lo_thresh[c:0] */
#define tpb_txblo_thresh_width 13
/* default value of bitfield tx{b}_lo_thresh[c:0] */
#define tpb_txblo_thresh_default 0x0

/* tx dma_sys_loopback bitfield definitions
 * preprocessor definitions for the bitfield "dma_sys_loopback".
 * port="pif_tpb_dma_sys_lbk_i"
 */

/* register address for bitfield dma_sys_loopback */
#define tpb_dma_sys_lbk_adr 0x00007000
/* bitmask for bitfield dma_sys_loopback */
#define tpb_dma_sys_lbk_msk 0x00000040
/* inverted bitmask for bitfield dma_sys_loopback */
#define tpb_dma_sys_lbk_mskn 0xffffffbf
/* lower bit position of bitfield dma_sys_loopback */
#define tpb_dma_sys_lbk_shift 6
/* width of bitfield dma_sys_loopback */
#define tpb_dma_sys_lbk_width 1
/* default value of bitfield dma_sys_loopback */
#define tpb_dma_sys_lbk_default 0x0

/* tx tx{b}_buf_size[7:0] bitfield definitions
 * preprocessor definitions for the bitfield "tx{b}_buf_size[7:0]".
 * parameter: buffer {b} | stride size 0x10 | range [0, 7]
 * port="pif_tpb_tx0_buf_size_i[7:0]"
 */

/* register address for bitfield tx{b}_buf_size[7:0] */
#define tpb_txbbuf_size_adr(buffer) (0x00007910 + (buffer) * 0x10)
/* bitmask for bitfield tx{b}_buf_size[7:0] */
#define tpb_txbbuf_size_msk 0x000000ff
/* inverted bitmask for bitfield tx{b}_buf_size[7:0] */
#define tpb_txbbuf_size_mskn 0xffffff00
/* lower bit position of bitfield tx{b}_buf_size[7:0] */
#define tpb_txbbuf_size_shift 0
/* width of bitfield tx{b}_buf_size[7:0] */
#define tpb_txbbuf_size_width 8
/* default value of bitfield tx{b}_buf_size[7:0] */
#define tpb_txbbuf_size_default 0x0

/* tx tx_scp_ins_en bitfield definitions
 * preprocessor definitions for the bitfield "tx_scp_ins_en".
 * port="pif_tpb_scp_ins_en_i"
 */

/* register address for bitfield tx_scp_ins_en */
#define tpb_tx_scp_ins_en_adr 0x00007900
/* bitmask for bitfield tx_scp_ins_en */
#define tpb_tx_scp_ins_en_msk 0x00000004
/* inverted bitmask for bitfield tx_scp_ins_en */
#define tpb_tx_scp_ins_en_mskn 0xfffffffb
/* lower bit position of bitfield tx_scp_ins_en */
#define tpb_tx_scp_ins_en_shift 2
/* width of bitfield tx_scp_ins_en */
#define tpb_tx_scp_ins_en_width 1
/* default value of bitfield tx_scp_ins_en */
#define tpb_tx_scp_ins_en_default 0x0

/* tx ipv4_chk_en bitfield definitions
 * preprocessor definitions for the bitfield "ipv4_chk_en".
 * port="pif_tpo_ipv4_chk_en_i"
 */

/* register address for bitfield ipv4_chk_en */
#define tpo_ipv4chk_en_adr 0x00007800
/* bitmask for bitfield ipv4_chk_en */
#define tpo_ipv4chk_en_msk 0x00000002
/* inverted bitmask for bitfield ipv4_chk_en */
#define tpo_ipv4chk_en_mskn 0xfffffffd
/* lower bit position of bitfield ipv4_chk_en */
#define tpo_ipv4chk_en_shift 1
/* width of bitfield ipv4_chk_en */
#define tpo_ipv4chk_en_width 1
/* default value of bitfield ipv4_chk_en */
#define tpo_ipv4chk_en_default 0x0

/* tx l4_chk_en bitfield definitions
 * preprocessor definitions for the bitfield "l4_chk_en".
 * port="pif_tpo_l4_chk_en_i"
 */

/* register address for bitfield l4_chk_en */
#define tpol4chk_en_adr 0x00007800
/* bitmask for bitfield l4_chk_en */
#define tpol4chk_en_msk 0x00000001
/* inverted bitmask for bitfield l4_chk_en */
#define tpol4chk_en_mskn 0xfffffffe
/* lower bit position of bitfield l4_chk_en */
#define tpol4chk_en_shift 0
/* width of bitfield l4_chk_en */
#define tpol4chk_en_width 1
/* default value of bitfield l4_chk_en */
#define tpol4chk_en_default 0x0

/* tx pkt_sys_loopback bitfield definitions
 * preprocessor definitions for the bitfield "pkt_sys_loopback".
 * port="pif_tpo_pkt_sys_lbk_i"
 */

/* register address for bitfield pkt_sys_loopback */
#define tpo_pkt_sys_lbk_adr 0x00007000
/* bitmask for bitfield pkt_sys_loopback */
#define tpo_pkt_sys_lbk_msk 0x00000080
/* inverted bitmask for bitfield pkt_sys_loopback */
#define tpo_pkt_sys_lbk_mskn 0xffffff7f
/* lower bit position of bitfield pkt_sys_loopback */
#define tpo_pkt_sys_lbk_shift 7
/* width of bitfield pkt_sys_loopback */
#define tpo_pkt_sys_lbk_width 1
/* default value of bitfield pkt_sys_loopback */
#define tpo_pkt_sys_lbk_default 0x0

/* tx data_tc_arb_mode bitfield definitions
 * preprocessor definitions for the bitfield "data_tc_arb_mode".
 * port="pif_tps_data_tc_arb_mode_i"
 */

/* register address for bitfield data_tc_arb_mode */
#define tps_data_tc_arb_mode_adr 0x00007100
/* bitmask for bitfield data_tc_arb_mode */
#define tps_data_tc_arb_mode_msk 0x00000001
/* inverted bitmask for bitfield data_tc_arb_mode */
#define tps_data_tc_arb_mode_mskn 0xfffffffe
/* lower bit position of bitfield data_tc_arb_mode */
#define tps_data_tc_arb_mode_shift 0
/* width of bitfield data_tc_arb_mode */
#define tps_data_tc_arb_mode_width 1
/* default value of bitfield data_tc_arb_mode */
#define tps_data_tc_arb_mode_default 0x0

/* tx desc_rate_ta_rst bitfield definitions
 * preprocessor definitions for the bitfield "desc_rate_ta_rst".
 * port="pif_tps_desc_rate_ta_rst_i"
 */

/* register address for bitfield desc_rate_ta_rst */
#define tps_desc_rate_ta_rst_adr 0x00007310
/* bitmask for bitfield desc_rate_ta_rst */
#define tps_desc_rate_ta_rst_msk 0x80000000
/* inverted bitmask for bitfield desc_rate_ta_rst */
#define tps_desc_rate_ta_rst_mskn 0x7fffffff
/* lower bit position of bitfield desc_rate_ta_rst */
#define tps_desc_rate_ta_rst_shift 31
/* width of bitfield desc_rate_ta_rst */
#define tps_desc_rate_ta_rst_width 1
/* default value of bitfield desc_rate_ta_rst */
#define tps_desc_rate_ta_rst_default 0x0

/* tx desc_rate_limit[a:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc_rate_limit[a:0]".
 * port="pif_tps_desc_rate_lim_i[10:0]"
 */

/* register address for bitfield desc_rate_limit[a:0] */
#define tps_desc_rate_lim_adr 0x00007310
/* bitmask for bitfield desc_rate_limit[a:0] */
#define tps_desc_rate_lim_msk 0x000007ff
/* inverted bitmask for bitfield desc_rate_limit[a:0] */
#define tps_desc_rate_lim_mskn 0xfffff800
/* lower bit position of bitfield desc_rate_limit[a:0] */
#define tps_desc_rate_lim_shift 0
/* width of bitfield desc_rate_limit[a:0] */
#define tps_desc_rate_lim_width 11
/* default value of bitfield desc_rate_limit[a:0] */
#define tps_desc_rate_lim_default 0x0

/* tx desc_tc_arb_mode[1:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc_tc_arb_mode[1:0]".
 * port="pif_tps_desc_tc_arb_mode_i[1:0]"
 */

/* register address for bitfield desc_tc_arb_mode[1:0] */
#define tps_desc_tc_arb_mode_adr 0x00007200
/* bitmask for bitfield desc_tc_arb_mode[1:0] */
#define tps_desc_tc_arb_mode_msk 0x00000003
/* inverted bitmask for bitfield desc_tc_arb_mode[1:0] */
#define tps_desc_tc_arb_mode_mskn 0xfffffffc
/* lower bit position of bitfield desc_tc_arb_mode[1:0] */
#define tps_desc_tc_arb_mode_shift 0
/* width of bitfield desc_tc_arb_mode[1:0] */
#define tps_desc_tc_arb_mode_width 2
/* default value of bitfield desc_tc_arb_mode[1:0] */
#define tps_desc_tc_arb_mode_default 0x0

/* tx desc_tc{t}_credit_max[b:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc_tc{t}_credit_max[b:0]".
 * parameter: tc {t} | stride size 0x4 | range [0, 7]
 * port="pif_tps_desc_tc0_credit_max_i[11:0]"
 */

/* register address for bitfield desc_tc{t}_credit_max[b:0] */
#define tps_desc_tctcredit_max_adr(tc) (0x00007210 + (tc) * 0x4)
/* bitmask for bitfield desc_tc{t}_credit_max[b:0] */
#define tps_desc_tctcredit_max_msk 0x0fff0000
/* inverted bitmask for bitfield desc_tc{t}_credit_max[b:0] */
#define tps_desc_tctcredit_max_mskn 0xf000ffff
/* lower bit position of bitfield desc_tc{t}_credit_max[b:0] */
#define tps_desc_tctcredit_max_shift 16
/* width of bitfield desc_tc{t}_credit_max[b:0] */
#define tps_desc_tctcredit_max_width 12
/* default value of bitfield desc_tc{t}_credit_max[b:0] */
#define tps_desc_tctcredit_max_default 0x0

/* tx desc_tc{t}_weight[8:0] bitfield definitions
 * preprocessor definitions for the bitfield "desc_tc{t}_weight[8:0]".
 * parameter: tc {t} | stride size 0x4 | range [0, 7]
 * port="pif_tps_desc_tc0_weight_i[8:0]"
 */

/* register address for bitfield desc_tc{t}_weight[8:0] */
#define tps_desc_tctweight_adr(tc) (0x00007210 + (tc) * 0x4)
/* bitmask for bitfield desc_tc{t}_weight[8:0] */
#define tps_desc_tctweight_msk 0x000001ff
/* inverted bitmask for bitfield desc_tc{t}_weight[8:0] */
#define tps_desc_tctweight_mskn 0xfffffe00
/* lower bit position of bitfield desc_tc{t}_weight[8:0] */
#define tps_desc_tctweight_shift 0
/* width of bitfield desc_tc{t}_weight[8:0] */
#define tps_desc_tctweight_width 9
/* default value of bitfield desc_tc{t}_weight[8:0] */
#define tps_desc_tctweight_default 0x0

/* tx desc_vm_arb_mode bitfield definitions
 * preprocessor definitions for the bitfield "desc_vm_arb_mode".
 * port="pif_tps_desc_vm_arb_mode_i"
 */

/* register address for bitfield desc_vm_arb_mode */
#define tps_desc_vm_arb_mode_adr 0x00007300
/* bitmask for bitfield desc_vm_arb_mode */
#define tps_desc_vm_arb_mode_msk 0x00000001
/* inverted bitmask for bitfield desc_vm_arb_mode */
#define tps_desc_vm_arb_mode_mskn 0xfffffffe
/* lower bit position of bitfield desc_vm_arb_mode */
#define tps_desc_vm_arb_mode_shift 0
/* width of bitfield desc_vm_arb_mode */
#define tps_desc_vm_arb_mode_width 1
/* default value of bitfield desc_vm_arb_mode */
#define tps_desc_vm_arb_mode_default 0x0

/* tx data_tc{t}_credit_max[b:0] bitfield definitions
 * preprocessor definitions for the bitfield "data_tc{t}_credit_max[b:0]".
 * parameter: tc {t} | stride size 0x4 | range [0, 7]
 * port="pif_tps_data_tc0_credit_max_i[11:0]"
 */

/* register address for bitfield data_tc{t}_credit_max[b:0] */
#define tps_data_tctcredit_max_adr(tc) (0x00007110 + (tc) * 0x4)
/* bitmask for bitfield data_tc{t}_credit_max[b:0] */
#define tps_data_tctcredit_max_msk 0x0fff0000
/* inverted bitmask for bitfield data_tc{t}_credit_max[b:0] */
#define tps_data_tctcredit_max_mskn 0xf000ffff
/* lower bit position of bitfield data_tc{t}_credit_max[b:0] */
#define tps_data_tctcredit_max_shift 16
/* width of bitfield data_tc{t}_credit_max[b:0] */
#define tps_data_tctcredit_max_width 12
/* default value of bitfield data_tc{t}_credit_max[b:0] */
#define tps_data_tctcredit_max_default 0x0

/* tx data_tc{t}_weight[8:0] bitfield definitions
 * preprocessor definitions for the bitfield "data_tc{t}_weight[8:0]".
 * parameter: tc {t} | stride size 0x4 | range [0, 7]
 * port="pif_tps_data_tc0_weight_i[8:0]"
 */

/* register address for bitfield data_tc{t}_weight[8:0] */
#define tps_data_tctweight_adr(tc) (0x00007110 + (tc) * 0x4)
/* bitmask for bitfield data_tc{t}_weight[8:0] */
#define tps_data_tctweight_msk 0x000001ff
/* inverted bitmask for bitfield data_tc{t}_weight[8:0] */
#define tps_data_tctweight_mskn 0xfffffe00
/* lower bit position of bitfield data_tc{t}_weight[8:0] */
#define tps_data_tctweight_shift 0
/* width of bitfield data_tc{t}_weight[8:0] */
#define tps_data_tctweight_width 9
/* default value of bitfield data_tc{t}_weight[8:0] */
#define tps_data_tctweight_default 0x0

/* tx reg_res_dsbl bitfield definitions
 * preprocessor definitions for the bitfield "reg_res_dsbl".
 * port="pif_tx_reg_res_dsbl_i"
 */

/* register address for bitfield reg_res_dsbl */
#define tx_reg_res_dsbl_adr 0x00007000
/* bitmask for bitfield reg_res_dsbl */
#define tx_reg_res_dsbl_msk 0x20000000
/* inverted bitmask for bitfield reg_res_dsbl */
#define tx_reg_res_dsbl_mskn 0xdfffffff
/* lower bit position of bitfield reg_res_dsbl */
#define tx_reg_res_dsbl_shift 29
/* width of bitfield reg_res_dsbl */
#define tx_reg_res_dsbl_width 1
/* default value of bitfield reg_res_dsbl */
#define tx_reg_res_dsbl_default 0x1

/* mac_phy register access busy bitfield definitions
 * preprocessor definitions for the bitfield "register access busy".
 * port="msm_pif_reg_busy_o"
 */

/* register address for bitfield register access busy */
#define msm_reg_access_busy_adr 0x00004400
/* bitmask for bitfield register access busy */
#define msm_reg_access_busy_msk 0x00001000
/* inverted bitmask for bitfield register access busy */
#define msm_reg_access_busy_mskn 0xffffefff
/* lower bit position of bitfield register access busy */
#define msm_reg_access_busy_shift 12
/* width of bitfield register access busy */
#define msm_reg_access_busy_width 1

/* mac_phy msm register address[7:0] bitfield definitions
 * preprocessor definitions for the bitfield "msm register address[7:0]".
 * port="pif_msm_reg_addr_i[7:0]"
 */

/* register address for bitfield msm register address[7:0] */
#define msm_reg_addr_adr 0x00004400
/* bitmask for bitfield msm register address[7:0] */
#define msm_reg_addr_msk 0x000000ff
/* inverted bitmask for bitfield msm register address[7:0] */
#define msm_reg_addr_mskn 0xffffff00
/* lower bit position of bitfield msm register address[7:0] */
#define msm_reg_addr_shift 0
/* width of bitfield msm register address[7:0] */
#define msm_reg_addr_width 8
/* default value of bitfield msm register address[7:0] */
#define msm_reg_addr_default 0x0

/* mac_phy register read strobe bitfield definitions
 * preprocessor definitions for the bitfield "register read strobe".
 * port="pif_msm_reg_rden_i"
 */

/* register address for bitfield register read strobe */
#define msm_reg_rd_strobe_adr 0x00004400
/* bitmask for bitfield register read strobe */
#define msm_reg_rd_strobe_msk 0x00000200
/* inverted bitmask for bitfield register read strobe */
#define msm_reg_rd_strobe_mskn 0xfffffdff
/* lower bit position of bitfield register read strobe */
#define msm_reg_rd_strobe_shift 9
/* width of bitfield register read strobe */
#define msm_reg_rd_strobe_width 1
/* default value of bitfield register read strobe */
#define msm_reg_rd_strobe_default 0x0

/* mac_phy msm register read data[31:0] bitfield definitions
 * preprocessor definitions for the bitfield "msm register read data[31:0]".
 * port="msm_pif_reg_rd_data_o[31:0]"
 */

/* register address for bitfield msm register read data[31:0] */
#define msm_reg_rd_data_adr 0x00004408
/* bitmask for bitfield msm register read data[31:0] */
#define msm_reg_rd_data_msk 0xffffffff
/* inverted bitmask for bitfield msm register read data[31:0] */
#define msm_reg_rd_data_mskn 0x00000000
/* lower bit position of bitfield msm register read data[31:0] */
#define msm_reg_rd_data_shift 0
/* width of bitfield msm register read data[31:0] */
#define msm_reg_rd_data_width 32

/* mac_phy msm register write data[31:0] bitfield definitions
 * preprocessor definitions for the bitfield "msm register write data[31:0]".
 * port="pif_msm_reg_wr_data_i[31:0]"
 */

/* register address for bitfield msm register write data[31:0] */
#define msm_reg_wr_data_adr 0x00004404
/* bitmask for bitfield msm register write data[31:0] */
#define msm_reg_wr_data_msk 0xffffffff
/* inverted bitmask for bitfield msm register write data[31:0] */
#define msm_reg_wr_data_mskn 0x00000000
/* lower bit position of bitfield msm register write data[31:0] */
#define msm_reg_wr_data_shift 0
/* width of bitfield msm register write data[31:0] */
#define msm_reg_wr_data_width 32
/* default value of bitfield msm register write data[31:0] */
#define msm_reg_wr_data_default 0x0

/* mac_phy register write strobe bitfield definitions
 * preprocessor definitions for the bitfield "register write strobe".
 * port="pif_msm_reg_wren_i"
 */

/* register address for bitfield register write strobe */
#define msm_reg_wr_strobe_adr 0x00004400
/* bitmask for bitfield register write strobe */
#define msm_reg_wr_strobe_msk 0x00000100
/* inverted bitmask for bitfield register write strobe */
#define msm_reg_wr_strobe_mskn 0xfffffeff
/* lower bit position of bitfield register write strobe */
#define msm_reg_wr_strobe_shift 8
/* width of bitfield register write strobe */
#define msm_reg_wr_strobe_width 1
/* default value of bitfield register write strobe */
#define msm_reg_wr_strobe_default 0x0

/* mif soft reset bitfield definitions
 * preprocessor definitions for the bitfield "soft reset".
 * port="pif_glb_res_i"
 */

/* register address for bitfield soft reset */
#define glb_soft_res_adr 0x00000000
/* bitmask for bitfield soft reset */
#define glb_soft_res_msk 0x00008000
/* inverted bitmask for bitfield soft reset */
#define glb_soft_res_mskn 0xffff7fff
/* lower bit position of bitfield soft reset */
#define glb_soft_res_shift 15
/* width of bitfield soft reset */
#define glb_soft_res_width 1
/* default value of bitfield soft reset */
#define glb_soft_res_default 0x0

/* mif register reset disable bitfield definitions
 * preprocessor definitions for the bitfield "register reset disable".
 * port="pif_glb_reg_res_dsbl_i"
 */

/* register address for bitfield register reset disable */
#define glb_reg_res_dis_adr 0x00000000
/* bitmask for bitfield register reset disable */
#define glb_reg_res_dis_msk 0x00004000
/* inverted bitmask for bitfield register reset disable */
#define glb_reg_res_dis_mskn 0xffffbfff
/* lower bit position of bitfield register reset disable */
#define glb_reg_res_dis_shift 14
/* width of bitfield register reset disable */
#define glb_reg_res_dis_width 1
/* default value of bitfield register reset disable */
#define glb_reg_res_dis_default 0x1

/* tx dma debug control definitions */
#define tx_dma_debug_ctl_adr 0x00008920u

/* tx dma descriptor base address msw definitions */
#define tx_dma_desc_base_addrmsw_adr(descriptor) \
			(0x00007c04u + (descriptor) * 0x40)

/* tx dma total request limit */
#define tx_dma_total_req_limit_adr 0x00007b20u

/* tx interrupt moderation control register definitions
 * Preprocessor definitions for TX Interrupt Moderation Control Register
 * Base Address: 0x00008980
 * Parameter: queue {Q} | stride size 0x4 | range [0, 31]
 */

#define tx_intr_moderation_ctl_adr(queue) (0x00008980u + (queue) * 0x4)

/* pcie reg_res_dsbl bitfield definitions
 * preprocessor definitions for the bitfield "reg_res_dsbl".
 * port="pif_pci_reg_res_dsbl_i"
 */

/* register address for bitfield reg_res_dsbl */
#define pci_reg_res_dsbl_adr 0x00001000
/* bitmask for bitfield reg_res_dsbl */
#define pci_reg_res_dsbl_msk 0x20000000
/* inverted bitmask for bitfield reg_res_dsbl */
#define pci_reg_res_dsbl_mskn 0xdfffffff
/* lower bit position of bitfield reg_res_dsbl */
#define pci_reg_res_dsbl_shift 29
/* width of bitfield reg_res_dsbl */
#define pci_reg_res_dsbl_width 1
/* default value of bitfield reg_res_dsbl */
#define pci_reg_res_dsbl_default 0x1

/* PCI core control register */
#define pci_reg_control6_adr 0x1014u

/* global microprocessor scratch pad definitions */
#define glb_cpu_scratch_scp_adr(scratch_scp) (0x00000300u + (scratch_scp) * 0x4)

#endif /* HW_ATL_LLH_INTERNAL_H */
