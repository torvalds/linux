/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2010 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

#ifndef __CVMX_IPD_DEFS_H__
#define __CVMX_IPD_DEFS_H__

#define CVMX_IPD_1ST_MBUFF_SKIP (CVMX_ADD_IO_SEG(0x00014F0000000000ull))
#define CVMX_IPD_1st_NEXT_PTR_BACK (CVMX_ADD_IO_SEG(0x00014F0000000150ull))
#define CVMX_IPD_2nd_NEXT_PTR_BACK (CVMX_ADD_IO_SEG(0x00014F0000000158ull))
#define CVMX_IPD_BIST_STATUS (CVMX_ADD_IO_SEG(0x00014F00000007F8ull))
#define CVMX_IPD_BP_PRT_RED_END (CVMX_ADD_IO_SEG(0x00014F0000000328ull))
#define CVMX_IPD_CLK_COUNT (CVMX_ADD_IO_SEG(0x00014F0000000338ull))
#define CVMX_IPD_CTL_STATUS (CVMX_ADD_IO_SEG(0x00014F0000000018ull))
#define CVMX_IPD_INT_ENB (CVMX_ADD_IO_SEG(0x00014F0000000160ull))
#define CVMX_IPD_INT_SUM (CVMX_ADD_IO_SEG(0x00014F0000000168ull))
#define CVMX_IPD_NOT_1ST_MBUFF_SKIP (CVMX_ADD_IO_SEG(0x00014F0000000008ull))
#define CVMX_IPD_PACKET_MBUFF_SIZE (CVMX_ADD_IO_SEG(0x00014F0000000010ull))
#define CVMX_IPD_PKT_PTR_VALID (CVMX_ADD_IO_SEG(0x00014F0000000358ull))
#define CVMX_IPD_PORTX_BP_PAGE_CNT(offset) (CVMX_ADD_IO_SEG(0x00014F0000000028ull) + ((offset) & 63) * 8)
#define CVMX_IPD_PORTX_BP_PAGE_CNT2(offset) (CVMX_ADD_IO_SEG(0x00014F0000000368ull) + ((offset) & 63) * 8 - 8*36)
#define CVMX_IPD_PORTX_BP_PAGE_CNT3(offset) (CVMX_ADD_IO_SEG(0x00014F00000003D0ull) + ((offset) & 63) * 8 - 8*40)
#define CVMX_IPD_PORT_BP_COUNTERS2_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000388ull) + ((offset) & 63) * 8 - 8*36)
#define CVMX_IPD_PORT_BP_COUNTERS3_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F00000003B0ull) + ((offset) & 63) * 8 - 8*40)
#define CVMX_IPD_PORT_BP_COUNTERS_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F00000001B8ull) + ((offset) & 63) * 8)
#define CVMX_IPD_PORT_QOS_INTX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000808ull) + ((offset) & 7) * 8)
#define CVMX_IPD_PORT_QOS_INT_ENBX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000848ull) + ((offset) & 7) * 8)
#define CVMX_IPD_PORT_QOS_X_CNT(offset) (CVMX_ADD_IO_SEG(0x00014F0000000888ull) + ((offset) & 511) * 8)
#define CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000348ull))
#define CVMX_IPD_PRC_PORT_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000350ull))
#define CVMX_IPD_PTR_COUNT (CVMX_ADD_IO_SEG(0x00014F0000000320ull))
#define CVMX_IPD_PWP_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000340ull))
#define CVMX_IPD_QOS0_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(0)
#define CVMX_IPD_QOS1_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(1)
#define CVMX_IPD_QOS2_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(2)
#define CVMX_IPD_QOS3_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(3)
#define CVMX_IPD_QOS4_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(4)
#define CVMX_IPD_QOS5_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(5)
#define CVMX_IPD_QOS6_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(6)
#define CVMX_IPD_QOS7_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(7)
#define CVMX_IPD_QOSX_RED_MARKS(offset) (CVMX_ADD_IO_SEG(0x00014F0000000178ull) + ((offset) & 7) * 8)
#define CVMX_IPD_QUE0_FREE_PAGE_CNT (CVMX_ADD_IO_SEG(0x00014F0000000330ull))
#define CVMX_IPD_RED_PORT_ENABLE (CVMX_ADD_IO_SEG(0x00014F00000002D8ull))
#define CVMX_IPD_RED_PORT_ENABLE2 (CVMX_ADD_IO_SEG(0x00014F00000003A8ull))
#define CVMX_IPD_RED_QUE0_PARAM CVMX_IPD_RED_QUEX_PARAM(0)
#define CVMX_IPD_RED_QUE1_PARAM CVMX_IPD_RED_QUEX_PARAM(1)
#define CVMX_IPD_RED_QUE2_PARAM CVMX_IPD_RED_QUEX_PARAM(2)
#define CVMX_IPD_RED_QUE3_PARAM CVMX_IPD_RED_QUEX_PARAM(3)
#define CVMX_IPD_RED_QUE4_PARAM CVMX_IPD_RED_QUEX_PARAM(4)
#define CVMX_IPD_RED_QUE5_PARAM CVMX_IPD_RED_QUEX_PARAM(5)
#define CVMX_IPD_RED_QUE6_PARAM CVMX_IPD_RED_QUEX_PARAM(6)
#define CVMX_IPD_RED_QUE7_PARAM CVMX_IPD_RED_QUEX_PARAM(7)
#define CVMX_IPD_RED_QUEX_PARAM(offset) (CVMX_ADD_IO_SEG(0x00014F00000002E0ull) + ((offset) & 7) * 8)
#define CVMX_IPD_SUB_PORT_BP_PAGE_CNT (CVMX_ADD_IO_SEG(0x00014F0000000148ull))
#define CVMX_IPD_SUB_PORT_FCS (CVMX_ADD_IO_SEG(0x00014F0000000170ull))
#define CVMX_IPD_SUB_PORT_QOS_CNT (CVMX_ADD_IO_SEG(0x00014F0000000800ull))
#define CVMX_IPD_WQE_FPA_QUEUE (CVMX_ADD_IO_SEG(0x00014F0000000020ull))
#define CVMX_IPD_WQE_PTR_VALID (CVMX_ADD_IO_SEG(0x00014F0000000360ull))

union cvmx_ipd_1st_mbuff_skip {
	uint64_t u64;
	struct cvmx_ipd_1st_mbuff_skip_s {
		uint64_t reserved_6_63:58;
		uint64_t skip_sz:6;
	} s;
	struct cvmx_ipd_1st_mbuff_skip_s cn30xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn31xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn38xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn38xxp2;
	struct cvmx_ipd_1st_mbuff_skip_s cn50xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn52xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn52xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s cn56xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn56xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s cn58xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn58xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s cn63xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn63xxp1;
};

union cvmx_ipd_1st_next_ptr_back {
	uint64_t u64;
	struct cvmx_ipd_1st_next_ptr_back_s {
		uint64_t reserved_4_63:60;
		uint64_t back:4;
	} s;
	struct cvmx_ipd_1st_next_ptr_back_s cn30xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn31xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn38xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn38xxp2;
	struct cvmx_ipd_1st_next_ptr_back_s cn50xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn52xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn52xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s cn56xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn56xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s cn58xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn58xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s cn63xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn63xxp1;
};

union cvmx_ipd_2nd_next_ptr_back {
	uint64_t u64;
	struct cvmx_ipd_2nd_next_ptr_back_s {
		uint64_t reserved_4_63:60;
		uint64_t back:4;
	} s;
	struct cvmx_ipd_2nd_next_ptr_back_s cn30xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn31xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn38xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn38xxp2;
	struct cvmx_ipd_2nd_next_ptr_back_s cn50xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn52xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn52xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s cn56xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn56xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s cn58xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn58xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s cn63xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn63xxp1;
};

union cvmx_ipd_bist_status {
	uint64_t u64;
	struct cvmx_ipd_bist_status_s {
		uint64_t reserved_18_63:46;
		uint64_t csr_mem:1;
		uint64_t csr_ncmd:1;
		uint64_t pwq_wqed:1;
		uint64_t pwq_wp1:1;
		uint64_t pwq_pow:1;
		uint64_t ipq_pbe1:1;
		uint64_t ipq_pbe0:1;
		uint64_t pbm3:1;
		uint64_t pbm2:1;
		uint64_t pbm1:1;
		uint64_t pbm0:1;
		uint64_t pbm_word:1;
		uint64_t pwq1:1;
		uint64_t pwq0:1;
		uint64_t prc_off:1;
		uint64_t ipd_old:1;
		uint64_t ipd_new:1;
		uint64_t pwp:1;
	} s;
	struct cvmx_ipd_bist_status_cn30xx {
		uint64_t reserved_16_63:48;
		uint64_t pwq_wqed:1;
		uint64_t pwq_wp1:1;
		uint64_t pwq_pow:1;
		uint64_t ipq_pbe1:1;
		uint64_t ipq_pbe0:1;
		uint64_t pbm3:1;
		uint64_t pbm2:1;
		uint64_t pbm1:1;
		uint64_t pbm0:1;
		uint64_t pbm_word:1;
		uint64_t pwq1:1;
		uint64_t pwq0:1;
		uint64_t prc_off:1;
		uint64_t ipd_old:1;
		uint64_t ipd_new:1;
		uint64_t pwp:1;
	} cn30xx;
	struct cvmx_ipd_bist_status_cn30xx cn31xx;
	struct cvmx_ipd_bist_status_cn30xx cn38xx;
	struct cvmx_ipd_bist_status_cn30xx cn38xxp2;
	struct cvmx_ipd_bist_status_cn30xx cn50xx;
	struct cvmx_ipd_bist_status_s cn52xx;
	struct cvmx_ipd_bist_status_s cn52xxp1;
	struct cvmx_ipd_bist_status_s cn56xx;
	struct cvmx_ipd_bist_status_s cn56xxp1;
	struct cvmx_ipd_bist_status_cn30xx cn58xx;
	struct cvmx_ipd_bist_status_cn30xx cn58xxp1;
	struct cvmx_ipd_bist_status_s cn63xx;
	struct cvmx_ipd_bist_status_s cn63xxp1;
};

union cvmx_ipd_bp_prt_red_end {
	uint64_t u64;
	struct cvmx_ipd_bp_prt_red_end_s {
		uint64_t reserved_44_63:20;
		uint64_t prt_enb:44;
	} s;
	struct cvmx_ipd_bp_prt_red_end_cn30xx {
		uint64_t reserved_36_63:28;
		uint64_t prt_enb:36;
	} cn30xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn31xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn38xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn38xxp2;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn50xx;
	struct cvmx_ipd_bp_prt_red_end_cn52xx {
		uint64_t reserved_40_63:24;
		uint64_t prt_enb:40;
	} cn52xx;
	struct cvmx_ipd_bp_prt_red_end_cn52xx cn52xxp1;
	struct cvmx_ipd_bp_prt_red_end_cn52xx cn56xx;
	struct cvmx_ipd_bp_prt_red_end_cn52xx cn56xxp1;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn58xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn58xxp1;
	struct cvmx_ipd_bp_prt_red_end_s cn63xx;
	struct cvmx_ipd_bp_prt_red_end_s cn63xxp1;
};

union cvmx_ipd_clk_count {
	uint64_t u64;
	struct cvmx_ipd_clk_count_s {
		uint64_t clk_cnt:64;
	} s;
	struct cvmx_ipd_clk_count_s cn30xx;
	struct cvmx_ipd_clk_count_s cn31xx;
	struct cvmx_ipd_clk_count_s cn38xx;
	struct cvmx_ipd_clk_count_s cn38xxp2;
	struct cvmx_ipd_clk_count_s cn50xx;
	struct cvmx_ipd_clk_count_s cn52xx;
	struct cvmx_ipd_clk_count_s cn52xxp1;
	struct cvmx_ipd_clk_count_s cn56xx;
	struct cvmx_ipd_clk_count_s cn56xxp1;
	struct cvmx_ipd_clk_count_s cn58xx;
	struct cvmx_ipd_clk_count_s cn58xxp1;
	struct cvmx_ipd_clk_count_s cn63xx;
	struct cvmx_ipd_clk_count_s cn63xxp1;
};

union cvmx_ipd_ctl_status {
	uint64_t u64;
	struct cvmx_ipd_ctl_status_s {
		uint64_t reserved_18_63:46;
		uint64_t use_sop:1;
		uint64_t rst_done:1;
		uint64_t clken:1;
		uint64_t no_wptr:1;
		uint64_t pq_apkt:1;
		uint64_t pq_nabuf:1;
		uint64_t ipd_full:1;
		uint64_t pkt_off:1;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} s;
	struct cvmx_ipd_ctl_status_cn30xx {
		uint64_t reserved_10_63:54;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn30xx;
	struct cvmx_ipd_ctl_status_cn30xx cn31xx;
	struct cvmx_ipd_ctl_status_cn30xx cn38xx;
	struct cvmx_ipd_ctl_status_cn38xxp2 {
		uint64_t reserved_9_63:55;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn38xxp2;
	struct cvmx_ipd_ctl_status_cn50xx {
		uint64_t reserved_15_63:49;
		uint64_t no_wptr:1;
		uint64_t pq_apkt:1;
		uint64_t pq_nabuf:1;
		uint64_t ipd_full:1;
		uint64_t pkt_off:1;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn50xx;
	struct cvmx_ipd_ctl_status_cn50xx cn52xx;
	struct cvmx_ipd_ctl_status_cn50xx cn52xxp1;
	struct cvmx_ipd_ctl_status_cn50xx cn56xx;
	struct cvmx_ipd_ctl_status_cn50xx cn56xxp1;
	struct cvmx_ipd_ctl_status_cn58xx {
		uint64_t reserved_12_63:52;
		uint64_t ipd_full:1;
		uint64_t pkt_off:1;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn58xx;
	struct cvmx_ipd_ctl_status_cn58xx cn58xxp1;
	struct cvmx_ipd_ctl_status_s cn63xx;
	struct cvmx_ipd_ctl_status_cn63xxp1 {
		uint64_t reserved_16_63:48;
		uint64_t clken:1;
		uint64_t no_wptr:1;
		uint64_t pq_apkt:1;
		uint64_t pq_nabuf:1;
		uint64_t ipd_full:1;
		uint64_t pkt_off:1;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn63xxp1;
};

union cvmx_ipd_int_enb {
	uint64_t u64;
	struct cvmx_ipd_int_enb_s {
		uint64_t reserved_12_63:52;
		uint64_t pq_sub:1;
		uint64_t pq_add:1;
		uint64_t bc_ovr:1;
		uint64_t d_coll:1;
		uint64_t c_coll:1;
		uint64_t cc_ovr:1;
		uint64_t dc_ovr:1;
		uint64_t bp_sub:1;
		uint64_t prc_par3:1;
		uint64_t prc_par2:1;
		uint64_t prc_par1:1;
		uint64_t prc_par0:1;
	} s;
	struct cvmx_ipd_int_enb_cn30xx {
		uint64_t reserved_5_63:59;
		uint64_t bp_sub:1;
		uint64_t prc_par3:1;
		uint64_t prc_par2:1;
		uint64_t prc_par1:1;
		uint64_t prc_par0:1;
	} cn30xx;
	struct cvmx_ipd_int_enb_cn30xx cn31xx;
	struct cvmx_ipd_int_enb_cn38xx {
		uint64_t reserved_10_63:54;
		uint64_t bc_ovr:1;
		uint64_t d_coll:1;
		uint64_t c_coll:1;
		uint64_t cc_ovr:1;
		uint64_t dc_ovr:1;
		uint64_t bp_sub:1;
		uint64_t prc_par3:1;
		uint64_t prc_par2:1;
		uint64_t prc_par1:1;
		uint64_t prc_par0:1;
	} cn38xx;
	struct cvmx_ipd_int_enb_cn30xx cn38xxp2;
	struct cvmx_ipd_int_enb_cn38xx cn50xx;
	struct cvmx_ipd_int_enb_s cn52xx;
	struct cvmx_ipd_int_enb_s cn52xxp1;
	struct cvmx_ipd_int_enb_s cn56xx;
	struct cvmx_ipd_int_enb_s cn56xxp1;
	struct cvmx_ipd_int_enb_cn38xx cn58xx;
	struct cvmx_ipd_int_enb_cn38xx cn58xxp1;
	struct cvmx_ipd_int_enb_s cn63xx;
	struct cvmx_ipd_int_enb_s cn63xxp1;
};

union cvmx_ipd_int_sum {
	uint64_t u64;
	struct cvmx_ipd_int_sum_s {
		uint64_t reserved_12_63:52;
		uint64_t pq_sub:1;
		uint64_t pq_add:1;
		uint64_t bc_ovr:1;
		uint64_t d_coll:1;
		uint64_t c_coll:1;
		uint64_t cc_ovr:1;
		uint64_t dc_ovr:1;
		uint64_t bp_sub:1;
		uint64_t prc_par3:1;
		uint64_t prc_par2:1;
		uint64_t prc_par1:1;
		uint64_t prc_par0:1;
	} s;
	struct cvmx_ipd_int_sum_cn30xx {
		uint64_t reserved_5_63:59;
		uint64_t bp_sub:1;
		uint64_t prc_par3:1;
		uint64_t prc_par2:1;
		uint64_t prc_par1:1;
		uint64_t prc_par0:1;
	} cn30xx;
	struct cvmx_ipd_int_sum_cn30xx cn31xx;
	struct cvmx_ipd_int_sum_cn38xx {
		uint64_t reserved_10_63:54;
		uint64_t bc_ovr:1;
		uint64_t d_coll:1;
		uint64_t c_coll:1;
		uint64_t cc_ovr:1;
		uint64_t dc_ovr:1;
		uint64_t bp_sub:1;
		uint64_t prc_par3:1;
		uint64_t prc_par2:1;
		uint64_t prc_par1:1;
		uint64_t prc_par0:1;
	} cn38xx;
	struct cvmx_ipd_int_sum_cn30xx cn38xxp2;
	struct cvmx_ipd_int_sum_cn38xx cn50xx;
	struct cvmx_ipd_int_sum_s cn52xx;
	struct cvmx_ipd_int_sum_s cn52xxp1;
	struct cvmx_ipd_int_sum_s cn56xx;
	struct cvmx_ipd_int_sum_s cn56xxp1;
	struct cvmx_ipd_int_sum_cn38xx cn58xx;
	struct cvmx_ipd_int_sum_cn38xx cn58xxp1;
	struct cvmx_ipd_int_sum_s cn63xx;
	struct cvmx_ipd_int_sum_s cn63xxp1;
};

union cvmx_ipd_not_1st_mbuff_skip {
	uint64_t u64;
	struct cvmx_ipd_not_1st_mbuff_skip_s {
		uint64_t reserved_6_63:58;
		uint64_t skip_sz:6;
	} s;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn30xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn31xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn38xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn38xxp2;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn50xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn52xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn52xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn56xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn56xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn58xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn58xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn63xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn63xxp1;
};

union cvmx_ipd_packet_mbuff_size {
	uint64_t u64;
	struct cvmx_ipd_packet_mbuff_size_s {
		uint64_t reserved_12_63:52;
		uint64_t mb_size:12;
	} s;
	struct cvmx_ipd_packet_mbuff_size_s cn30xx;
	struct cvmx_ipd_packet_mbuff_size_s cn31xx;
	struct cvmx_ipd_packet_mbuff_size_s cn38xx;
	struct cvmx_ipd_packet_mbuff_size_s cn38xxp2;
	struct cvmx_ipd_packet_mbuff_size_s cn50xx;
	struct cvmx_ipd_packet_mbuff_size_s cn52xx;
	struct cvmx_ipd_packet_mbuff_size_s cn52xxp1;
	struct cvmx_ipd_packet_mbuff_size_s cn56xx;
	struct cvmx_ipd_packet_mbuff_size_s cn56xxp1;
	struct cvmx_ipd_packet_mbuff_size_s cn58xx;
	struct cvmx_ipd_packet_mbuff_size_s cn58xxp1;
	struct cvmx_ipd_packet_mbuff_size_s cn63xx;
	struct cvmx_ipd_packet_mbuff_size_s cn63xxp1;
};

union cvmx_ipd_pkt_ptr_valid {
	uint64_t u64;
	struct cvmx_ipd_pkt_ptr_valid_s {
		uint64_t reserved_29_63:35;
		uint64_t ptr:29;
	} s;
	struct cvmx_ipd_pkt_ptr_valid_s cn30xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn31xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn38xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn50xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn52xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn52xxp1;
	struct cvmx_ipd_pkt_ptr_valid_s cn56xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn56xxp1;
	struct cvmx_ipd_pkt_ptr_valid_s cn58xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn58xxp1;
	struct cvmx_ipd_pkt_ptr_valid_s cn63xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn63xxp1;
};

union cvmx_ipd_portx_bp_page_cnt {
	uint64_t u64;
	struct cvmx_ipd_portx_bp_page_cnt_s {
		uint64_t reserved_18_63:46;
		uint64_t bp_enb:1;
		uint64_t page_cnt:17;
	} s;
	struct cvmx_ipd_portx_bp_page_cnt_s cn30xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn31xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn38xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn38xxp2;
	struct cvmx_ipd_portx_bp_page_cnt_s cn50xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn52xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn52xxp1;
	struct cvmx_ipd_portx_bp_page_cnt_s cn56xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn56xxp1;
	struct cvmx_ipd_portx_bp_page_cnt_s cn58xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn58xxp1;
	struct cvmx_ipd_portx_bp_page_cnt_s cn63xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn63xxp1;
};

union cvmx_ipd_portx_bp_page_cnt2 {
	uint64_t u64;
	struct cvmx_ipd_portx_bp_page_cnt2_s {
		uint64_t reserved_18_63:46;
		uint64_t bp_enb:1;
		uint64_t page_cnt:17;
	} s;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn52xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn52xxp1;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn56xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn56xxp1;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn63xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn63xxp1;
};

union cvmx_ipd_portx_bp_page_cnt3 {
	uint64_t u64;
	struct cvmx_ipd_portx_bp_page_cnt3_s {
		uint64_t reserved_18_63:46;
		uint64_t bp_enb:1;
		uint64_t page_cnt:17;
	} s;
	struct cvmx_ipd_portx_bp_page_cnt3_s cn63xx;
	struct cvmx_ipd_portx_bp_page_cnt3_s cn63xxp1;
};

union cvmx_ipd_port_bp_counters2_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters2_pairx_s {
		uint64_t reserved_25_63:39;
		uint64_t cnt_val:25;
	} s;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn52xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn52xxp1;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn56xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn56xxp1;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn63xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn63xxp1;
};

union cvmx_ipd_port_bp_counters3_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters3_pairx_s {
		uint64_t reserved_25_63:39;
		uint64_t cnt_val:25;
	} s;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn63xx;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn63xxp1;
};

union cvmx_ipd_port_bp_counters_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters_pairx_s {
		uint64_t reserved_25_63:39;
		uint64_t cnt_val:25;
	} s;
	struct cvmx_ipd_port_bp_counters_pairx_s cn30xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn31xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn38xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn38xxp2;
	struct cvmx_ipd_port_bp_counters_pairx_s cn50xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn52xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn52xxp1;
	struct cvmx_ipd_port_bp_counters_pairx_s cn56xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn56xxp1;
	struct cvmx_ipd_port_bp_counters_pairx_s cn58xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn58xxp1;
	struct cvmx_ipd_port_bp_counters_pairx_s cn63xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn63xxp1;
};

union cvmx_ipd_port_qos_x_cnt {
	uint64_t u64;
	struct cvmx_ipd_port_qos_x_cnt_s {
		uint64_t wmark:32;
		uint64_t cnt:32;
	} s;
	struct cvmx_ipd_port_qos_x_cnt_s cn52xx;
	struct cvmx_ipd_port_qos_x_cnt_s cn52xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s cn56xx;
	struct cvmx_ipd_port_qos_x_cnt_s cn56xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s cn63xx;
	struct cvmx_ipd_port_qos_x_cnt_s cn63xxp1;
};

union cvmx_ipd_port_qos_intx {
	uint64_t u64;
	struct cvmx_ipd_port_qos_intx_s {
		uint64_t intr:64;
	} s;
	struct cvmx_ipd_port_qos_intx_s cn52xx;
	struct cvmx_ipd_port_qos_intx_s cn52xxp1;
	struct cvmx_ipd_port_qos_intx_s cn56xx;
	struct cvmx_ipd_port_qos_intx_s cn56xxp1;
	struct cvmx_ipd_port_qos_intx_s cn63xx;
	struct cvmx_ipd_port_qos_intx_s cn63xxp1;
};

union cvmx_ipd_port_qos_int_enbx {
	uint64_t u64;
	struct cvmx_ipd_port_qos_int_enbx_s {
		uint64_t enb:64;
	} s;
	struct cvmx_ipd_port_qos_int_enbx_s cn52xx;
	struct cvmx_ipd_port_qos_int_enbx_s cn52xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s cn56xx;
	struct cvmx_ipd_port_qos_int_enbx_s cn56xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s cn63xx;
	struct cvmx_ipd_port_qos_int_enbx_s cn63xxp1;
};

union cvmx_ipd_prc_hold_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s {
		uint64_t reserved_39_63:25;
		uint64_t max_pkt:3;
		uint64_t praddr:3;
		uint64_t ptr:29;
		uint64_t cena:1;
		uint64_t raddr:3;
	} s;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn30xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn31xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn38xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn50xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn52xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn52xxp1;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn56xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn56xxp1;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn58xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn58xxp1;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn63xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn63xxp1;
};

union cvmx_ipd_prc_port_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s {
		uint64_t reserved_44_63:20;
		uint64_t max_pkt:7;
		uint64_t ptr:29;
		uint64_t cena:1;
		uint64_t raddr:7;
	} s;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn30xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn31xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn38xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn50xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn52xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn52xxp1;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn56xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn56xxp1;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn58xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn58xxp1;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn63xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn63xxp1;
};

union cvmx_ipd_ptr_count {
	uint64_t u64;
	struct cvmx_ipd_ptr_count_s {
		uint64_t reserved_19_63:45;
		uint64_t pktv_cnt:1;
		uint64_t wqev_cnt:1;
		uint64_t pfif_cnt:3;
		uint64_t pkt_pcnt:7;
		uint64_t wqe_pcnt:7;
	} s;
	struct cvmx_ipd_ptr_count_s cn30xx;
	struct cvmx_ipd_ptr_count_s cn31xx;
	struct cvmx_ipd_ptr_count_s cn38xx;
	struct cvmx_ipd_ptr_count_s cn38xxp2;
	struct cvmx_ipd_ptr_count_s cn50xx;
	struct cvmx_ipd_ptr_count_s cn52xx;
	struct cvmx_ipd_ptr_count_s cn52xxp1;
	struct cvmx_ipd_ptr_count_s cn56xx;
	struct cvmx_ipd_ptr_count_s cn56xxp1;
	struct cvmx_ipd_ptr_count_s cn58xx;
	struct cvmx_ipd_ptr_count_s cn58xxp1;
	struct cvmx_ipd_ptr_count_s cn63xx;
	struct cvmx_ipd_ptr_count_s cn63xxp1;
};

union cvmx_ipd_pwp_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s {
		uint64_t reserved_61_63:3;
		uint64_t max_cnts:7;
		uint64_t wraddr:8;
		uint64_t praddr:8;
		uint64_t ptr:29;
		uint64_t cena:1;
		uint64_t raddr:8;
	} s;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn30xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn31xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn38xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn50xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn52xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn52xxp1;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn56xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn56xxp1;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn58xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn58xxp1;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn63xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn63xxp1;
};

union cvmx_ipd_qosx_red_marks {
	uint64_t u64;
	struct cvmx_ipd_qosx_red_marks_s {
		uint64_t drop:32;
		uint64_t pass:32;
	} s;
	struct cvmx_ipd_qosx_red_marks_s cn30xx;
	struct cvmx_ipd_qosx_red_marks_s cn31xx;
	struct cvmx_ipd_qosx_red_marks_s cn38xx;
	struct cvmx_ipd_qosx_red_marks_s cn38xxp2;
	struct cvmx_ipd_qosx_red_marks_s cn50xx;
	struct cvmx_ipd_qosx_red_marks_s cn52xx;
	struct cvmx_ipd_qosx_red_marks_s cn52xxp1;
	struct cvmx_ipd_qosx_red_marks_s cn56xx;
	struct cvmx_ipd_qosx_red_marks_s cn56xxp1;
	struct cvmx_ipd_qosx_red_marks_s cn58xx;
	struct cvmx_ipd_qosx_red_marks_s cn58xxp1;
	struct cvmx_ipd_qosx_red_marks_s cn63xx;
	struct cvmx_ipd_qosx_red_marks_s cn63xxp1;
};

union cvmx_ipd_que0_free_page_cnt {
	uint64_t u64;
	struct cvmx_ipd_que0_free_page_cnt_s {
		uint64_t reserved_32_63:32;
		uint64_t q0_pcnt:32;
	} s;
	struct cvmx_ipd_que0_free_page_cnt_s cn30xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn31xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn38xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn38xxp2;
	struct cvmx_ipd_que0_free_page_cnt_s cn50xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn52xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn52xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s cn56xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn56xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s cn58xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn58xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s cn63xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn63xxp1;
};

union cvmx_ipd_red_port_enable {
	uint64_t u64;
	struct cvmx_ipd_red_port_enable_s {
		uint64_t prb_dly:14;
		uint64_t avg_dly:14;
		uint64_t prt_enb:36;
	} s;
	struct cvmx_ipd_red_port_enable_s cn30xx;
	struct cvmx_ipd_red_port_enable_s cn31xx;
	struct cvmx_ipd_red_port_enable_s cn38xx;
	struct cvmx_ipd_red_port_enable_s cn38xxp2;
	struct cvmx_ipd_red_port_enable_s cn50xx;
	struct cvmx_ipd_red_port_enable_s cn52xx;
	struct cvmx_ipd_red_port_enable_s cn52xxp1;
	struct cvmx_ipd_red_port_enable_s cn56xx;
	struct cvmx_ipd_red_port_enable_s cn56xxp1;
	struct cvmx_ipd_red_port_enable_s cn58xx;
	struct cvmx_ipd_red_port_enable_s cn58xxp1;
	struct cvmx_ipd_red_port_enable_s cn63xx;
	struct cvmx_ipd_red_port_enable_s cn63xxp1;
};

union cvmx_ipd_red_port_enable2 {
	uint64_t u64;
	struct cvmx_ipd_red_port_enable2_s {
		uint64_t reserved_8_63:56;
		uint64_t prt_enb:8;
	} s;
	struct cvmx_ipd_red_port_enable2_cn52xx {
		uint64_t reserved_4_63:60;
		uint64_t prt_enb:4;
	} cn52xx;
	struct cvmx_ipd_red_port_enable2_cn52xx cn52xxp1;
	struct cvmx_ipd_red_port_enable2_cn52xx cn56xx;
	struct cvmx_ipd_red_port_enable2_cn52xx cn56xxp1;
	struct cvmx_ipd_red_port_enable2_s cn63xx;
	struct cvmx_ipd_red_port_enable2_s cn63xxp1;
};

union cvmx_ipd_red_quex_param {
	uint64_t u64;
	struct cvmx_ipd_red_quex_param_s {
		uint64_t reserved_49_63:15;
		uint64_t use_pcnt:1;
		uint64_t new_con:8;
		uint64_t avg_con:8;
		uint64_t prb_con:32;
	} s;
	struct cvmx_ipd_red_quex_param_s cn30xx;
	struct cvmx_ipd_red_quex_param_s cn31xx;
	struct cvmx_ipd_red_quex_param_s cn38xx;
	struct cvmx_ipd_red_quex_param_s cn38xxp2;
	struct cvmx_ipd_red_quex_param_s cn50xx;
	struct cvmx_ipd_red_quex_param_s cn52xx;
	struct cvmx_ipd_red_quex_param_s cn52xxp1;
	struct cvmx_ipd_red_quex_param_s cn56xx;
	struct cvmx_ipd_red_quex_param_s cn56xxp1;
	struct cvmx_ipd_red_quex_param_s cn58xx;
	struct cvmx_ipd_red_quex_param_s cn58xxp1;
	struct cvmx_ipd_red_quex_param_s cn63xx;
	struct cvmx_ipd_red_quex_param_s cn63xxp1;
};

union cvmx_ipd_sub_port_bp_page_cnt {
	uint64_t u64;
	struct cvmx_ipd_sub_port_bp_page_cnt_s {
		uint64_t reserved_31_63:33;
		uint64_t port:6;
		uint64_t page_cnt:25;
	} s;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn30xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn31xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn38xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn38xxp2;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn50xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn52xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn52xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn56xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn56xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn58xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn58xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn63xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn63xxp1;
};

union cvmx_ipd_sub_port_fcs {
	uint64_t u64;
	struct cvmx_ipd_sub_port_fcs_s {
		uint64_t reserved_40_63:24;
		uint64_t port_bit2:4;
		uint64_t reserved_32_35:4;
		uint64_t port_bit:32;
	} s;
	struct cvmx_ipd_sub_port_fcs_cn30xx {
		uint64_t reserved_3_63:61;
		uint64_t port_bit:3;
	} cn30xx;
	struct cvmx_ipd_sub_port_fcs_cn30xx cn31xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx {
		uint64_t reserved_32_63:32;
		uint64_t port_bit:32;
	} cn38xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx cn38xxp2;
	struct cvmx_ipd_sub_port_fcs_cn30xx cn50xx;
	struct cvmx_ipd_sub_port_fcs_s cn52xx;
	struct cvmx_ipd_sub_port_fcs_s cn52xxp1;
	struct cvmx_ipd_sub_port_fcs_s cn56xx;
	struct cvmx_ipd_sub_port_fcs_s cn56xxp1;
	struct cvmx_ipd_sub_port_fcs_cn38xx cn58xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx cn58xxp1;
	struct cvmx_ipd_sub_port_fcs_s cn63xx;
	struct cvmx_ipd_sub_port_fcs_s cn63xxp1;
};

union cvmx_ipd_sub_port_qos_cnt {
	uint64_t u64;
	struct cvmx_ipd_sub_port_qos_cnt_s {
		uint64_t reserved_41_63:23;
		uint64_t port_qos:9;
		uint64_t cnt:32;
	} s;
	struct cvmx_ipd_sub_port_qos_cnt_s cn52xx;
	struct cvmx_ipd_sub_port_qos_cnt_s cn52xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s cn56xx;
	struct cvmx_ipd_sub_port_qos_cnt_s cn56xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s cn63xx;
	struct cvmx_ipd_sub_port_qos_cnt_s cn63xxp1;
};

union cvmx_ipd_wqe_fpa_queue {
	uint64_t u64;
	struct cvmx_ipd_wqe_fpa_queue_s {
		uint64_t reserved_3_63:61;
		uint64_t wqe_pool:3;
	} s;
	struct cvmx_ipd_wqe_fpa_queue_s cn30xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn31xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn38xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn38xxp2;
	struct cvmx_ipd_wqe_fpa_queue_s cn50xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn52xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn52xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s cn56xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn56xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s cn58xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn58xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s cn63xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn63xxp1;
};

union cvmx_ipd_wqe_ptr_valid {
	uint64_t u64;
	struct cvmx_ipd_wqe_ptr_valid_s {
		uint64_t reserved_29_63:35;
		uint64_t ptr:29;
	} s;
	struct cvmx_ipd_wqe_ptr_valid_s cn30xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn31xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn38xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn50xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn52xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn52xxp1;
	struct cvmx_ipd_wqe_ptr_valid_s cn56xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn56xxp1;
	struct cvmx_ipd_wqe_ptr_valid_s cn58xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn58xxp1;
	struct cvmx_ipd_wqe_ptr_valid_s cn63xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn63xxp1;
};

#endif
