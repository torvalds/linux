/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2012 Cavium Networks
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
#define CVMX_IPD_BPIDX_MBUF_TH(offset) (CVMX_ADD_IO_SEG(0x00014F0000002000ull) + ((offset) & 63) * 8)
#define CVMX_IPD_BPID_BP_COUNTERX(offset) (CVMX_ADD_IO_SEG(0x00014F0000003000ull) + ((offset) & 63) * 8)
#define CVMX_IPD_BP_PRT_RED_END (CVMX_ADD_IO_SEG(0x00014F0000000328ull))
#define CVMX_IPD_CLK_COUNT (CVMX_ADD_IO_SEG(0x00014F0000000338ull))
#define CVMX_IPD_CREDITS (CVMX_ADD_IO_SEG(0x00014F0000004410ull))
#define CVMX_IPD_CTL_STATUS (CVMX_ADD_IO_SEG(0x00014F0000000018ull))
#define CVMX_IPD_ECC_CTL (CVMX_ADD_IO_SEG(0x00014F0000004408ull))
#define CVMX_IPD_FREE_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000780ull))
#define CVMX_IPD_FREE_PTR_VALUE (CVMX_ADD_IO_SEG(0x00014F0000000788ull))
#define CVMX_IPD_HOLD_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000790ull))
#define CVMX_IPD_INT_ENB (CVMX_ADD_IO_SEG(0x00014F0000000160ull))
#define CVMX_IPD_INT_SUM (CVMX_ADD_IO_SEG(0x00014F0000000168ull))
#define CVMX_IPD_NEXT_PKT_PTR (CVMX_ADD_IO_SEG(0x00014F00000007A0ull))
#define CVMX_IPD_NEXT_WQE_PTR (CVMX_ADD_IO_SEG(0x00014F00000007A8ull))
#define CVMX_IPD_NOT_1ST_MBUFF_SKIP (CVMX_ADD_IO_SEG(0x00014F0000000008ull))
#define CVMX_IPD_ON_BP_DROP_PKTX(block_id) (CVMX_ADD_IO_SEG(0x00014F0000004100ull))
#define CVMX_IPD_PACKET_MBUFF_SIZE (CVMX_ADD_IO_SEG(0x00014F0000000010ull))
#define CVMX_IPD_PKT_ERR (CVMX_ADD_IO_SEG(0x00014F00000003F0ull))
#define CVMX_IPD_PKT_PTR_VALID (CVMX_ADD_IO_SEG(0x00014F0000000358ull))
#define CVMX_IPD_PORTX_BP_PAGE_CNT(offset) (CVMX_ADD_IO_SEG(0x00014F0000000028ull) + ((offset) & 63) * 8)
#define CVMX_IPD_PORTX_BP_PAGE_CNT2(offset) (CVMX_ADD_IO_SEG(0x00014F0000000368ull) + ((offset) & 63) * 8 - 8*36)
#define CVMX_IPD_PORTX_BP_PAGE_CNT3(offset) (CVMX_ADD_IO_SEG(0x00014F00000003D0ull) + ((offset) & 63) * 8 - 8*40)
#define CVMX_IPD_PORT_BP_COUNTERS2_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000388ull) + ((offset) & 63) * 8 - 8*36)
#define CVMX_IPD_PORT_BP_COUNTERS3_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F00000003B0ull) + ((offset) & 63) * 8 - 8*40)
#define CVMX_IPD_PORT_BP_COUNTERS4_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000410ull) + ((offset) & 63) * 8 - 8*44)
#define CVMX_IPD_PORT_BP_COUNTERS_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F00000001B8ull) + ((offset) & 63) * 8)
#define CVMX_IPD_PORT_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000798ull))
#define CVMX_IPD_PORT_QOS_INTX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000808ull) + ((offset) & 7) * 8)
#define CVMX_IPD_PORT_QOS_INT_ENBX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000848ull) + ((offset) & 7) * 8)
#define CVMX_IPD_PORT_QOS_X_CNT(offset) (CVMX_ADD_IO_SEG(0x00014F0000000888ull) + ((offset) & 511) * 8)
#define CVMX_IPD_PORT_SOPX(block_id) (CVMX_ADD_IO_SEG(0x00014F0000004400ull))
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
#define CVMX_IPD_RED_BPID_ENABLEX(block_id) (CVMX_ADD_IO_SEG(0x00014F0000004200ull))
#define CVMX_IPD_RED_DELAY (CVMX_ADD_IO_SEG(0x00014F0000004300ull))
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
#define CVMX_IPD_REQ_WGT (CVMX_ADD_IO_SEG(0x00014F0000004418ull))
#define CVMX_IPD_SUB_PORT_BP_PAGE_CNT (CVMX_ADD_IO_SEG(0x00014F0000000148ull))
#define CVMX_IPD_SUB_PORT_FCS (CVMX_ADD_IO_SEG(0x00014F0000000170ull))
#define CVMX_IPD_SUB_PORT_QOS_CNT (CVMX_ADD_IO_SEG(0x00014F0000000800ull))
#define CVMX_IPD_WQE_FPA_QUEUE (CVMX_ADD_IO_SEG(0x00014F0000000020ull))
#define CVMX_IPD_WQE_PTR_VALID (CVMX_ADD_IO_SEG(0x00014F0000000360ull))

union cvmx_ipd_1st_mbuff_skip {
	uint64_t u64;
	struct cvmx_ipd_1st_mbuff_skip_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t skip_sz:6;
#else
		uint64_t skip_sz:6;
		uint64_t reserved_6_63:58;
#endif
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
	struct cvmx_ipd_1st_mbuff_skip_s cn61xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn63xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn63xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s cn66xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn68xx;
	struct cvmx_ipd_1st_mbuff_skip_s cn68xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s cnf71xx;
};

union cvmx_ipd_1st_next_ptr_back {
	uint64_t u64;
	struct cvmx_ipd_1st_next_ptr_back_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t back:4;
#else
		uint64_t back:4;
		uint64_t reserved_4_63:60;
#endif
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
	struct cvmx_ipd_1st_next_ptr_back_s cn61xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn63xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn63xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s cn66xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn68xx;
	struct cvmx_ipd_1st_next_ptr_back_s cn68xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s cnf71xx;
};

union cvmx_ipd_2nd_next_ptr_back {
	uint64_t u64;
	struct cvmx_ipd_2nd_next_ptr_back_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t back:4;
#else
		uint64_t back:4;
		uint64_t reserved_4_63:60;
#endif
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
	struct cvmx_ipd_2nd_next_ptr_back_s cn61xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn63xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn63xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s cn66xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn68xx;
	struct cvmx_ipd_2nd_next_ptr_back_s cn68xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s cnf71xx;
};

union cvmx_ipd_bist_status {
	uint64_t u64;
	struct cvmx_ipd_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t iiwo1:1;
		uint64_t iiwo0:1;
		uint64_t iio1:1;
		uint64_t iio0:1;
		uint64_t pbm4:1;
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
#else
		uint64_t pwp:1;
		uint64_t ipd_new:1;
		uint64_t ipd_old:1;
		uint64_t prc_off:1;
		uint64_t pwq0:1;
		uint64_t pwq1:1;
		uint64_t pbm_word:1;
		uint64_t pbm0:1;
		uint64_t pbm1:1;
		uint64_t pbm2:1;
		uint64_t pbm3:1;
		uint64_t ipq_pbe0:1;
		uint64_t ipq_pbe1:1;
		uint64_t pwq_pow:1;
		uint64_t pwq_wp1:1;
		uint64_t pwq_wqed:1;
		uint64_t csr_ncmd:1;
		uint64_t csr_mem:1;
		uint64_t pbm4:1;
		uint64_t iio0:1;
		uint64_t iio1:1;
		uint64_t iiwo0:1;
		uint64_t iiwo1:1;
		uint64_t reserved_23_63:41;
#endif
	} s;
	struct cvmx_ipd_bist_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pwp:1;
		uint64_t ipd_new:1;
		uint64_t ipd_old:1;
		uint64_t prc_off:1;
		uint64_t pwq0:1;
		uint64_t pwq1:1;
		uint64_t pbm_word:1;
		uint64_t pbm0:1;
		uint64_t pbm1:1;
		uint64_t pbm2:1;
		uint64_t pbm3:1;
		uint64_t ipq_pbe0:1;
		uint64_t ipq_pbe1:1;
		uint64_t pwq_pow:1;
		uint64_t pwq_wp1:1;
		uint64_t pwq_wqed:1;
		uint64_t reserved_16_63:48;
#endif
	} cn30xx;
	struct cvmx_ipd_bist_status_cn30xx cn31xx;
	struct cvmx_ipd_bist_status_cn30xx cn38xx;
	struct cvmx_ipd_bist_status_cn30xx cn38xxp2;
	struct cvmx_ipd_bist_status_cn30xx cn50xx;
	struct cvmx_ipd_bist_status_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pwp:1;
		uint64_t ipd_new:1;
		uint64_t ipd_old:1;
		uint64_t prc_off:1;
		uint64_t pwq0:1;
		uint64_t pwq1:1;
		uint64_t pbm_word:1;
		uint64_t pbm0:1;
		uint64_t pbm1:1;
		uint64_t pbm2:1;
		uint64_t pbm3:1;
		uint64_t ipq_pbe0:1;
		uint64_t ipq_pbe1:1;
		uint64_t pwq_pow:1;
		uint64_t pwq_wp1:1;
		uint64_t pwq_wqed:1;
		uint64_t csr_ncmd:1;
		uint64_t csr_mem:1;
		uint64_t reserved_18_63:46;
#endif
	} cn52xx;
	struct cvmx_ipd_bist_status_cn52xx cn52xxp1;
	struct cvmx_ipd_bist_status_cn52xx cn56xx;
	struct cvmx_ipd_bist_status_cn52xx cn56xxp1;
	struct cvmx_ipd_bist_status_cn30xx cn58xx;
	struct cvmx_ipd_bist_status_cn30xx cn58xxp1;
	struct cvmx_ipd_bist_status_cn52xx cn61xx;
	struct cvmx_ipd_bist_status_cn52xx cn63xx;
	struct cvmx_ipd_bist_status_cn52xx cn63xxp1;
	struct cvmx_ipd_bist_status_cn52xx cn66xx;
	struct cvmx_ipd_bist_status_s cn68xx;
	struct cvmx_ipd_bist_status_s cn68xxp1;
	struct cvmx_ipd_bist_status_cn52xx cnf71xx;
};

union cvmx_ipd_bp_prt_red_end {
	uint64_t u64;
	struct cvmx_ipd_bp_prt_red_end_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t prt_enb:48;
#else
		uint64_t prt_enb:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_ipd_bp_prt_red_end_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t prt_enb:36;
#else
		uint64_t prt_enb:36;
		uint64_t reserved_36_63:28;
#endif
	} cn30xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn31xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn38xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn38xxp2;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn50xx;
	struct cvmx_ipd_bp_prt_red_end_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t prt_enb:40;
#else
		uint64_t prt_enb:40;
		uint64_t reserved_40_63:24;
#endif
	} cn52xx;
	struct cvmx_ipd_bp_prt_red_end_cn52xx cn52xxp1;
	struct cvmx_ipd_bp_prt_red_end_cn52xx cn56xx;
	struct cvmx_ipd_bp_prt_red_end_cn52xx cn56xxp1;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn58xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn58xxp1;
	struct cvmx_ipd_bp_prt_red_end_s cn61xx;
	struct cvmx_ipd_bp_prt_red_end_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t prt_enb:44;
#else
		uint64_t prt_enb:44;
		uint64_t reserved_44_63:20;
#endif
	} cn63xx;
	struct cvmx_ipd_bp_prt_red_end_cn63xx cn63xxp1;
	struct cvmx_ipd_bp_prt_red_end_s cn66xx;
	struct cvmx_ipd_bp_prt_red_end_s cnf71xx;
};

union cvmx_ipd_bpidx_mbuf_th {
	uint64_t u64;
	struct cvmx_ipd_bpidx_mbuf_th_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t bp_enb:1;
		uint64_t page_cnt:17;
#else
		uint64_t page_cnt:17;
		uint64_t bp_enb:1;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_ipd_bpidx_mbuf_th_s cn68xx;
	struct cvmx_ipd_bpidx_mbuf_th_s cn68xxp1;
};

union cvmx_ipd_bpid_bp_counterx {
	uint64_t u64;
	struct cvmx_ipd_bpid_bp_counterx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t cnt_val:25;
#else
		uint64_t cnt_val:25;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_ipd_bpid_bp_counterx_s cn68xx;
	struct cvmx_ipd_bpid_bp_counterx_s cn68xxp1;
};

union cvmx_ipd_clk_count {
	uint64_t u64;
	struct cvmx_ipd_clk_count_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t clk_cnt:64;
#else
		uint64_t clk_cnt:64;
#endif
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
	struct cvmx_ipd_clk_count_s cn61xx;
	struct cvmx_ipd_clk_count_s cn63xx;
	struct cvmx_ipd_clk_count_s cn63xxp1;
	struct cvmx_ipd_clk_count_s cn66xx;
	struct cvmx_ipd_clk_count_s cn68xx;
	struct cvmx_ipd_clk_count_s cn68xxp1;
	struct cvmx_ipd_clk_count_s cnf71xx;
};

union cvmx_ipd_credits {
	uint64_t u64;
	struct cvmx_ipd_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t iob_wrc:8;
		uint64_t iob_wr:8;
#else
		uint64_t iob_wr:8;
		uint64_t iob_wrc:8;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ipd_credits_s cn68xx;
	struct cvmx_ipd_credits_s cn68xxp1;
};

union cvmx_ipd_ctl_status {
	uint64_t u64;
	struct cvmx_ipd_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t ipd_en:1;
		uint64_t opc_mode:2;
		uint64_t pbp_en:1;
		uint64_t wqe_lend:1;
		uint64_t pkt_lend:1;
		uint64_t naddbuf:1;
		uint64_t addpkt:1;
		uint64_t reset:1;
		uint64_t len_m8:1;
		uint64_t pkt_off:1;
		uint64_t ipd_full:1;
		uint64_t pq_nabuf:1;
		uint64_t pq_apkt:1;
		uint64_t no_wptr:1;
		uint64_t clken:1;
		uint64_t rst_done:1;
		uint64_t use_sop:1;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_ipd_ctl_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t ipd_en:1;
		uint64_t opc_mode:2;
		uint64_t pbp_en:1;
		uint64_t wqe_lend:1;
		uint64_t pkt_lend:1;
		uint64_t naddbuf:1;
		uint64_t addpkt:1;
		uint64_t reset:1;
		uint64_t len_m8:1;
		uint64_t reserved_10_63:54;
#endif
	} cn30xx;
	struct cvmx_ipd_ctl_status_cn30xx cn31xx;
	struct cvmx_ipd_ctl_status_cn30xx cn38xx;
	struct cvmx_ipd_ctl_status_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
#else
		uint64_t ipd_en:1;
		uint64_t opc_mode:2;
		uint64_t pbp_en:1;
		uint64_t wqe_lend:1;
		uint64_t pkt_lend:1;
		uint64_t naddbuf:1;
		uint64_t addpkt:1;
		uint64_t reset:1;
		uint64_t reserved_9_63:55;
#endif
	} cn38xxp2;
	struct cvmx_ipd_ctl_status_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t ipd_en:1;
		uint64_t opc_mode:2;
		uint64_t pbp_en:1;
		uint64_t wqe_lend:1;
		uint64_t pkt_lend:1;
		uint64_t naddbuf:1;
		uint64_t addpkt:1;
		uint64_t reset:1;
		uint64_t len_m8:1;
		uint64_t pkt_off:1;
		uint64_t ipd_full:1;
		uint64_t pq_nabuf:1;
		uint64_t pq_apkt:1;
		uint64_t no_wptr:1;
		uint64_t reserved_15_63:49;
#endif
	} cn50xx;
	struct cvmx_ipd_ctl_status_cn50xx cn52xx;
	struct cvmx_ipd_ctl_status_cn50xx cn52xxp1;
	struct cvmx_ipd_ctl_status_cn50xx cn56xx;
	struct cvmx_ipd_ctl_status_cn50xx cn56xxp1;
	struct cvmx_ipd_ctl_status_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t ipd_en:1;
		uint64_t opc_mode:2;
		uint64_t pbp_en:1;
		uint64_t wqe_lend:1;
		uint64_t pkt_lend:1;
		uint64_t naddbuf:1;
		uint64_t addpkt:1;
		uint64_t reset:1;
		uint64_t len_m8:1;
		uint64_t pkt_off:1;
		uint64_t ipd_full:1;
		uint64_t reserved_12_63:52;
#endif
	} cn58xx;
	struct cvmx_ipd_ctl_status_cn58xx cn58xxp1;
	struct cvmx_ipd_ctl_status_s cn61xx;
	struct cvmx_ipd_ctl_status_s cn63xx;
	struct cvmx_ipd_ctl_status_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t ipd_en:1;
		uint64_t opc_mode:2;
		uint64_t pbp_en:1;
		uint64_t wqe_lend:1;
		uint64_t pkt_lend:1;
		uint64_t naddbuf:1;
		uint64_t addpkt:1;
		uint64_t reset:1;
		uint64_t len_m8:1;
		uint64_t pkt_off:1;
		uint64_t ipd_full:1;
		uint64_t pq_nabuf:1;
		uint64_t pq_apkt:1;
		uint64_t no_wptr:1;
		uint64_t clken:1;
		uint64_t reserved_16_63:48;
#endif
	} cn63xxp1;
	struct cvmx_ipd_ctl_status_s cn66xx;
	struct cvmx_ipd_ctl_status_s cn68xx;
	struct cvmx_ipd_ctl_status_s cn68xxp1;
	struct cvmx_ipd_ctl_status_s cnf71xx;
};

union cvmx_ipd_ecc_ctl {
	uint64_t u64;
	struct cvmx_ipd_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t pm3_syn:2;
		uint64_t pm2_syn:2;
		uint64_t pm1_syn:2;
		uint64_t pm0_syn:2;
#else
		uint64_t pm0_syn:2;
		uint64_t pm1_syn:2;
		uint64_t pm2_syn:2;
		uint64_t pm3_syn:2;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_ipd_ecc_ctl_s cn68xx;
	struct cvmx_ipd_ecc_ctl_s cn68xxp1;
};

union cvmx_ipd_free_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_free_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t max_cnts:7;
		uint64_t wraddr:8;
		uint64_t praddr:8;
		uint64_t cena:1;
		uint64_t raddr:8;
#else
		uint64_t raddr:8;
		uint64_t cena:1;
		uint64_t praddr:8;
		uint64_t wraddr:8;
		uint64_t max_cnts:7;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ipd_free_ptr_fifo_ctl_s cn68xx;
	struct cvmx_ipd_free_ptr_fifo_ctl_s cn68xxp1;
};

union cvmx_ipd_free_ptr_value {
	uint64_t u64;
	struct cvmx_ipd_free_ptr_value_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t ptr:33;
#else
		uint64_t ptr:33;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_ipd_free_ptr_value_s cn68xx;
	struct cvmx_ipd_free_ptr_value_s cn68xxp1;
};

union cvmx_ipd_hold_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_hold_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_43_63:21;
		uint64_t ptr:33;
		uint64_t max_pkt:3;
		uint64_t praddr:3;
		uint64_t cena:1;
		uint64_t raddr:3;
#else
		uint64_t raddr:3;
		uint64_t cena:1;
		uint64_t praddr:3;
		uint64_t max_pkt:3;
		uint64_t ptr:33;
		uint64_t reserved_43_63:21;
#endif
	} s;
	struct cvmx_ipd_hold_ptr_fifo_ctl_s cn68xx;
	struct cvmx_ipd_hold_ptr_fifo_ctl_s cn68xxp1;
};

union cvmx_ipd_int_enb {
	uint64_t u64;
	struct cvmx_ipd_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t pw3_dbe:1;
		uint64_t pw3_sbe:1;
		uint64_t pw2_dbe:1;
		uint64_t pw2_sbe:1;
		uint64_t pw1_dbe:1;
		uint64_t pw1_sbe:1;
		uint64_t pw0_dbe:1;
		uint64_t pw0_sbe:1;
		uint64_t dat:1;
		uint64_t eop:1;
		uint64_t sop:1;
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
#else
		uint64_t prc_par0:1;
		uint64_t prc_par1:1;
		uint64_t prc_par2:1;
		uint64_t prc_par3:1;
		uint64_t bp_sub:1;
		uint64_t dc_ovr:1;
		uint64_t cc_ovr:1;
		uint64_t c_coll:1;
		uint64_t d_coll:1;
		uint64_t bc_ovr:1;
		uint64_t pq_add:1;
		uint64_t pq_sub:1;
		uint64_t sop:1;
		uint64_t eop:1;
		uint64_t dat:1;
		uint64_t pw0_sbe:1;
		uint64_t pw0_dbe:1;
		uint64_t pw1_sbe:1;
		uint64_t pw1_dbe:1;
		uint64_t pw2_sbe:1;
		uint64_t pw2_dbe:1;
		uint64_t pw3_sbe:1;
		uint64_t pw3_dbe:1;
		uint64_t reserved_23_63:41;
#endif
	} s;
	struct cvmx_ipd_int_enb_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t bp_sub:1;
		uint64_t prc_par3:1;
		uint64_t prc_par2:1;
		uint64_t prc_par1:1;
		uint64_t prc_par0:1;
#else
		uint64_t prc_par0:1;
		uint64_t prc_par1:1;
		uint64_t prc_par2:1;
		uint64_t prc_par3:1;
		uint64_t bp_sub:1;
		uint64_t reserved_5_63:59;
#endif
	} cn30xx;
	struct cvmx_ipd_int_enb_cn30xx cn31xx;
	struct cvmx_ipd_int_enb_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t prc_par0:1;
		uint64_t prc_par1:1;
		uint64_t prc_par2:1;
		uint64_t prc_par3:1;
		uint64_t bp_sub:1;
		uint64_t dc_ovr:1;
		uint64_t cc_ovr:1;
		uint64_t c_coll:1;
		uint64_t d_coll:1;
		uint64_t bc_ovr:1;
		uint64_t reserved_10_63:54;
#endif
	} cn38xx;
	struct cvmx_ipd_int_enb_cn30xx cn38xxp2;
	struct cvmx_ipd_int_enb_cn38xx cn50xx;
	struct cvmx_ipd_int_enb_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t prc_par0:1;
		uint64_t prc_par1:1;
		uint64_t prc_par2:1;
		uint64_t prc_par3:1;
		uint64_t bp_sub:1;
		uint64_t dc_ovr:1;
		uint64_t cc_ovr:1;
		uint64_t c_coll:1;
		uint64_t d_coll:1;
		uint64_t bc_ovr:1;
		uint64_t pq_add:1;
		uint64_t pq_sub:1;
		uint64_t reserved_12_63:52;
#endif
	} cn52xx;
	struct cvmx_ipd_int_enb_cn52xx cn52xxp1;
	struct cvmx_ipd_int_enb_cn52xx cn56xx;
	struct cvmx_ipd_int_enb_cn52xx cn56xxp1;
	struct cvmx_ipd_int_enb_cn38xx cn58xx;
	struct cvmx_ipd_int_enb_cn38xx cn58xxp1;
	struct cvmx_ipd_int_enb_cn52xx cn61xx;
	struct cvmx_ipd_int_enb_cn52xx cn63xx;
	struct cvmx_ipd_int_enb_cn52xx cn63xxp1;
	struct cvmx_ipd_int_enb_cn52xx cn66xx;
	struct cvmx_ipd_int_enb_s cn68xx;
	struct cvmx_ipd_int_enb_s cn68xxp1;
	struct cvmx_ipd_int_enb_cn52xx cnf71xx;
};

union cvmx_ipd_int_sum {
	uint64_t u64;
	struct cvmx_ipd_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t pw3_dbe:1;
		uint64_t pw3_sbe:1;
		uint64_t pw2_dbe:1;
		uint64_t pw2_sbe:1;
		uint64_t pw1_dbe:1;
		uint64_t pw1_sbe:1;
		uint64_t pw0_dbe:1;
		uint64_t pw0_sbe:1;
		uint64_t dat:1;
		uint64_t eop:1;
		uint64_t sop:1;
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
#else
		uint64_t prc_par0:1;
		uint64_t prc_par1:1;
		uint64_t prc_par2:1;
		uint64_t prc_par3:1;
		uint64_t bp_sub:1;
		uint64_t dc_ovr:1;
		uint64_t cc_ovr:1;
		uint64_t c_coll:1;
		uint64_t d_coll:1;
		uint64_t bc_ovr:1;
		uint64_t pq_add:1;
		uint64_t pq_sub:1;
		uint64_t sop:1;
		uint64_t eop:1;
		uint64_t dat:1;
		uint64_t pw0_sbe:1;
		uint64_t pw0_dbe:1;
		uint64_t pw1_sbe:1;
		uint64_t pw1_dbe:1;
		uint64_t pw2_sbe:1;
		uint64_t pw2_dbe:1;
		uint64_t pw3_sbe:1;
		uint64_t pw3_dbe:1;
		uint64_t reserved_23_63:41;
#endif
	} s;
	struct cvmx_ipd_int_sum_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t bp_sub:1;
		uint64_t prc_par3:1;
		uint64_t prc_par2:1;
		uint64_t prc_par1:1;
		uint64_t prc_par0:1;
#else
		uint64_t prc_par0:1;
		uint64_t prc_par1:1;
		uint64_t prc_par2:1;
		uint64_t prc_par3:1;
		uint64_t bp_sub:1;
		uint64_t reserved_5_63:59;
#endif
	} cn30xx;
	struct cvmx_ipd_int_sum_cn30xx cn31xx;
	struct cvmx_ipd_int_sum_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t prc_par0:1;
		uint64_t prc_par1:1;
		uint64_t prc_par2:1;
		uint64_t prc_par3:1;
		uint64_t bp_sub:1;
		uint64_t dc_ovr:1;
		uint64_t cc_ovr:1;
		uint64_t c_coll:1;
		uint64_t d_coll:1;
		uint64_t bc_ovr:1;
		uint64_t reserved_10_63:54;
#endif
	} cn38xx;
	struct cvmx_ipd_int_sum_cn30xx cn38xxp2;
	struct cvmx_ipd_int_sum_cn38xx cn50xx;
	struct cvmx_ipd_int_sum_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t prc_par0:1;
		uint64_t prc_par1:1;
		uint64_t prc_par2:1;
		uint64_t prc_par3:1;
		uint64_t bp_sub:1;
		uint64_t dc_ovr:1;
		uint64_t cc_ovr:1;
		uint64_t c_coll:1;
		uint64_t d_coll:1;
		uint64_t bc_ovr:1;
		uint64_t pq_add:1;
		uint64_t pq_sub:1;
		uint64_t reserved_12_63:52;
#endif
	} cn52xx;
	struct cvmx_ipd_int_sum_cn52xx cn52xxp1;
	struct cvmx_ipd_int_sum_cn52xx cn56xx;
	struct cvmx_ipd_int_sum_cn52xx cn56xxp1;
	struct cvmx_ipd_int_sum_cn38xx cn58xx;
	struct cvmx_ipd_int_sum_cn38xx cn58xxp1;
	struct cvmx_ipd_int_sum_cn52xx cn61xx;
	struct cvmx_ipd_int_sum_cn52xx cn63xx;
	struct cvmx_ipd_int_sum_cn52xx cn63xxp1;
	struct cvmx_ipd_int_sum_cn52xx cn66xx;
	struct cvmx_ipd_int_sum_s cn68xx;
	struct cvmx_ipd_int_sum_s cn68xxp1;
	struct cvmx_ipd_int_sum_cn52xx cnf71xx;
};

union cvmx_ipd_next_pkt_ptr {
	uint64_t u64;
	struct cvmx_ipd_next_pkt_ptr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t ptr:33;
#else
		uint64_t ptr:33;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_ipd_next_pkt_ptr_s cn68xx;
	struct cvmx_ipd_next_pkt_ptr_s cn68xxp1;
};

union cvmx_ipd_next_wqe_ptr {
	uint64_t u64;
	struct cvmx_ipd_next_wqe_ptr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t ptr:33;
#else
		uint64_t ptr:33;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_ipd_next_wqe_ptr_s cn68xx;
	struct cvmx_ipd_next_wqe_ptr_s cn68xxp1;
};

union cvmx_ipd_not_1st_mbuff_skip {
	uint64_t u64;
	struct cvmx_ipd_not_1st_mbuff_skip_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t skip_sz:6;
#else
		uint64_t skip_sz:6;
		uint64_t reserved_6_63:58;
#endif
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
	struct cvmx_ipd_not_1st_mbuff_skip_s cn61xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn63xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn63xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn66xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn68xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s cn68xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s cnf71xx;
};

union cvmx_ipd_on_bp_drop_pktx {
	uint64_t u64;
	struct cvmx_ipd_on_bp_drop_pktx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t prt_enb:64;
#else
		uint64_t prt_enb:64;
#endif
	} s;
	struct cvmx_ipd_on_bp_drop_pktx_s cn68xx;
	struct cvmx_ipd_on_bp_drop_pktx_s cn68xxp1;
};

union cvmx_ipd_packet_mbuff_size {
	uint64_t u64;
	struct cvmx_ipd_packet_mbuff_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t mb_size:12;
#else
		uint64_t mb_size:12;
		uint64_t reserved_12_63:52;
#endif
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
	struct cvmx_ipd_packet_mbuff_size_s cn61xx;
	struct cvmx_ipd_packet_mbuff_size_s cn63xx;
	struct cvmx_ipd_packet_mbuff_size_s cn63xxp1;
	struct cvmx_ipd_packet_mbuff_size_s cn66xx;
	struct cvmx_ipd_packet_mbuff_size_s cn68xx;
	struct cvmx_ipd_packet_mbuff_size_s cn68xxp1;
	struct cvmx_ipd_packet_mbuff_size_s cnf71xx;
};

union cvmx_ipd_pkt_err {
	uint64_t u64;
	struct cvmx_ipd_pkt_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t reasm:6;
#else
		uint64_t reasm:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_ipd_pkt_err_s cn68xx;
	struct cvmx_ipd_pkt_err_s cn68xxp1;
};

union cvmx_ipd_pkt_ptr_valid {
	uint64_t u64;
	struct cvmx_ipd_pkt_ptr_valid_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t ptr:29;
#else
		uint64_t ptr:29;
		uint64_t reserved_29_63:35;
#endif
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
	struct cvmx_ipd_pkt_ptr_valid_s cn61xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn63xx;
	struct cvmx_ipd_pkt_ptr_valid_s cn63xxp1;
	struct cvmx_ipd_pkt_ptr_valid_s cn66xx;
	struct cvmx_ipd_pkt_ptr_valid_s cnf71xx;
};

union cvmx_ipd_portx_bp_page_cnt {
	uint64_t u64;
	struct cvmx_ipd_portx_bp_page_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t bp_enb:1;
		uint64_t page_cnt:17;
#else
		uint64_t page_cnt:17;
		uint64_t bp_enb:1;
		uint64_t reserved_18_63:46;
#endif
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
	struct cvmx_ipd_portx_bp_page_cnt_s cn61xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn63xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cn63xxp1;
	struct cvmx_ipd_portx_bp_page_cnt_s cn66xx;
	struct cvmx_ipd_portx_bp_page_cnt_s cnf71xx;
};

union cvmx_ipd_portx_bp_page_cnt2 {
	uint64_t u64;
	struct cvmx_ipd_portx_bp_page_cnt2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t bp_enb:1;
		uint64_t page_cnt:17;
#else
		uint64_t page_cnt:17;
		uint64_t bp_enb:1;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn52xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn52xxp1;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn56xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn56xxp1;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn61xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn63xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn63xxp1;
	struct cvmx_ipd_portx_bp_page_cnt2_s cn66xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s cnf71xx;
};

union cvmx_ipd_portx_bp_page_cnt3 {
	uint64_t u64;
	struct cvmx_ipd_portx_bp_page_cnt3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t bp_enb:1;
		uint64_t page_cnt:17;
#else
		uint64_t page_cnt:17;
		uint64_t bp_enb:1;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_ipd_portx_bp_page_cnt3_s cn61xx;
	struct cvmx_ipd_portx_bp_page_cnt3_s cn63xx;
	struct cvmx_ipd_portx_bp_page_cnt3_s cn63xxp1;
	struct cvmx_ipd_portx_bp_page_cnt3_s cn66xx;
	struct cvmx_ipd_portx_bp_page_cnt3_s cnf71xx;
};

union cvmx_ipd_port_bp_counters2_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters2_pairx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t cnt_val:25;
#else
		uint64_t cnt_val:25;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn52xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn52xxp1;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn56xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn56xxp1;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn61xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn63xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn63xxp1;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn66xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cnf71xx;
};

union cvmx_ipd_port_bp_counters3_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters3_pairx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t cnt_val:25;
#else
		uint64_t cnt_val:25;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn61xx;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn63xx;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn63xxp1;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn66xx;
	struct cvmx_ipd_port_bp_counters3_pairx_s cnf71xx;
};

union cvmx_ipd_port_bp_counters4_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters4_pairx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t cnt_val:25;
#else
		uint64_t cnt_val:25;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_ipd_port_bp_counters4_pairx_s cn61xx;
	struct cvmx_ipd_port_bp_counters4_pairx_s cn66xx;
	struct cvmx_ipd_port_bp_counters4_pairx_s cnf71xx;
};

union cvmx_ipd_port_bp_counters_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters_pairx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t cnt_val:25;
#else
		uint64_t cnt_val:25;
		uint64_t reserved_25_63:39;
#endif
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
	struct cvmx_ipd_port_bp_counters_pairx_s cn61xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn63xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn63xxp1;
	struct cvmx_ipd_port_bp_counters_pairx_s cn66xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cnf71xx;
};

union cvmx_ipd_port_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_port_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t ptr:33;
		uint64_t max_pkt:7;
		uint64_t cena:1;
		uint64_t raddr:7;
#else
		uint64_t raddr:7;
		uint64_t cena:1;
		uint64_t max_pkt:7;
		uint64_t ptr:33;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_ipd_port_ptr_fifo_ctl_s cn68xx;
	struct cvmx_ipd_port_ptr_fifo_ctl_s cn68xxp1;
};

union cvmx_ipd_port_qos_x_cnt {
	uint64_t u64;
	struct cvmx_ipd_port_qos_x_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t wmark:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t wmark:32;
#endif
	} s;
	struct cvmx_ipd_port_qos_x_cnt_s cn52xx;
	struct cvmx_ipd_port_qos_x_cnt_s cn52xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s cn56xx;
	struct cvmx_ipd_port_qos_x_cnt_s cn56xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s cn61xx;
	struct cvmx_ipd_port_qos_x_cnt_s cn63xx;
	struct cvmx_ipd_port_qos_x_cnt_s cn63xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s cn66xx;
	struct cvmx_ipd_port_qos_x_cnt_s cn68xx;
	struct cvmx_ipd_port_qos_x_cnt_s cn68xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s cnf71xx;
};

union cvmx_ipd_port_qos_intx {
	uint64_t u64;
	struct cvmx_ipd_port_qos_intx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t intr:64;
#else
		uint64_t intr:64;
#endif
	} s;
	struct cvmx_ipd_port_qos_intx_s cn52xx;
	struct cvmx_ipd_port_qos_intx_s cn52xxp1;
	struct cvmx_ipd_port_qos_intx_s cn56xx;
	struct cvmx_ipd_port_qos_intx_s cn56xxp1;
	struct cvmx_ipd_port_qos_intx_s cn61xx;
	struct cvmx_ipd_port_qos_intx_s cn63xx;
	struct cvmx_ipd_port_qos_intx_s cn63xxp1;
	struct cvmx_ipd_port_qos_intx_s cn66xx;
	struct cvmx_ipd_port_qos_intx_s cn68xx;
	struct cvmx_ipd_port_qos_intx_s cn68xxp1;
	struct cvmx_ipd_port_qos_intx_s cnf71xx;
};

union cvmx_ipd_port_qos_int_enbx {
	uint64_t u64;
	struct cvmx_ipd_port_qos_int_enbx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t enb:64;
#else
		uint64_t enb:64;
#endif
	} s;
	struct cvmx_ipd_port_qos_int_enbx_s cn52xx;
	struct cvmx_ipd_port_qos_int_enbx_s cn52xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s cn56xx;
	struct cvmx_ipd_port_qos_int_enbx_s cn56xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s cn61xx;
	struct cvmx_ipd_port_qos_int_enbx_s cn63xx;
	struct cvmx_ipd_port_qos_int_enbx_s cn63xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s cn66xx;
	struct cvmx_ipd_port_qos_int_enbx_s cn68xx;
	struct cvmx_ipd_port_qos_int_enbx_s cn68xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s cnf71xx;
};

union cvmx_ipd_port_sopx {
	uint64_t u64;
	struct cvmx_ipd_port_sopx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t sop:64;
#else
		uint64_t sop:64;
#endif
	} s;
	struct cvmx_ipd_port_sopx_s cn68xx;
	struct cvmx_ipd_port_sopx_s cn68xxp1;
};

union cvmx_ipd_prc_hold_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_39_63:25;
		uint64_t max_pkt:3;
		uint64_t praddr:3;
		uint64_t ptr:29;
		uint64_t cena:1;
		uint64_t raddr:3;
#else
		uint64_t raddr:3;
		uint64_t cena:1;
		uint64_t ptr:29;
		uint64_t praddr:3;
		uint64_t max_pkt:3;
		uint64_t reserved_39_63:25;
#endif
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
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn61xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn63xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn63xxp1;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn66xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cnf71xx;
};

union cvmx_ipd_prc_port_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t max_pkt:7;
		uint64_t ptr:29;
		uint64_t cena:1;
		uint64_t raddr:7;
#else
		uint64_t raddr:7;
		uint64_t cena:1;
		uint64_t ptr:29;
		uint64_t max_pkt:7;
		uint64_t reserved_44_63:20;
#endif
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
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn61xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn63xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn63xxp1;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn66xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cnf71xx;
};

union cvmx_ipd_ptr_count {
	uint64_t u64;
	struct cvmx_ipd_ptr_count_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t pktv_cnt:1;
		uint64_t wqev_cnt:1;
		uint64_t pfif_cnt:3;
		uint64_t pkt_pcnt:7;
		uint64_t wqe_pcnt:7;
#else
		uint64_t wqe_pcnt:7;
		uint64_t pkt_pcnt:7;
		uint64_t pfif_cnt:3;
		uint64_t wqev_cnt:1;
		uint64_t pktv_cnt:1;
		uint64_t reserved_19_63:45;
#endif
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
	struct cvmx_ipd_ptr_count_s cn61xx;
	struct cvmx_ipd_ptr_count_s cn63xx;
	struct cvmx_ipd_ptr_count_s cn63xxp1;
	struct cvmx_ipd_ptr_count_s cn66xx;
	struct cvmx_ipd_ptr_count_s cn68xx;
	struct cvmx_ipd_ptr_count_s cn68xxp1;
	struct cvmx_ipd_ptr_count_s cnf71xx;
};

union cvmx_ipd_pwp_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t max_cnts:7;
		uint64_t wraddr:8;
		uint64_t praddr:8;
		uint64_t ptr:29;
		uint64_t cena:1;
		uint64_t raddr:8;
#else
		uint64_t raddr:8;
		uint64_t cena:1;
		uint64_t ptr:29;
		uint64_t praddr:8;
		uint64_t wraddr:8;
		uint64_t max_cnts:7;
		uint64_t reserved_61_63:3;
#endif
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
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn61xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn63xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn63xxp1;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cn66xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s cnf71xx;
};

union cvmx_ipd_qosx_red_marks {
	uint64_t u64;
	struct cvmx_ipd_qosx_red_marks_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t drop:32;
		uint64_t pass:32;
#else
		uint64_t pass:32;
		uint64_t drop:32;
#endif
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
	struct cvmx_ipd_qosx_red_marks_s cn61xx;
	struct cvmx_ipd_qosx_red_marks_s cn63xx;
	struct cvmx_ipd_qosx_red_marks_s cn63xxp1;
	struct cvmx_ipd_qosx_red_marks_s cn66xx;
	struct cvmx_ipd_qosx_red_marks_s cn68xx;
	struct cvmx_ipd_qosx_red_marks_s cn68xxp1;
	struct cvmx_ipd_qosx_red_marks_s cnf71xx;
};

union cvmx_ipd_que0_free_page_cnt {
	uint64_t u64;
	struct cvmx_ipd_que0_free_page_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t q0_pcnt:32;
#else
		uint64_t q0_pcnt:32;
		uint64_t reserved_32_63:32;
#endif
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
	struct cvmx_ipd_que0_free_page_cnt_s cn61xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn63xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn63xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s cn66xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn68xx;
	struct cvmx_ipd_que0_free_page_cnt_s cn68xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s cnf71xx;
};

union cvmx_ipd_red_bpid_enablex {
	uint64_t u64;
	struct cvmx_ipd_red_bpid_enablex_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t prt_enb:64;
#else
		uint64_t prt_enb:64;
#endif
	} s;
	struct cvmx_ipd_red_bpid_enablex_s cn68xx;
	struct cvmx_ipd_red_bpid_enablex_s cn68xxp1;
};

union cvmx_ipd_red_delay {
	uint64_t u64;
	struct cvmx_ipd_red_delay_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t prb_dly:14;
		uint64_t avg_dly:14;
#else
		uint64_t avg_dly:14;
		uint64_t prb_dly:14;
		uint64_t reserved_28_63:36;
#endif
	} s;
	struct cvmx_ipd_red_delay_s cn68xx;
	struct cvmx_ipd_red_delay_s cn68xxp1;
};

union cvmx_ipd_red_port_enable {
	uint64_t u64;
	struct cvmx_ipd_red_port_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t prb_dly:14;
		uint64_t avg_dly:14;
		uint64_t prt_enb:36;
#else
		uint64_t prt_enb:36;
		uint64_t avg_dly:14;
		uint64_t prb_dly:14;
#endif
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
	struct cvmx_ipd_red_port_enable_s cn61xx;
	struct cvmx_ipd_red_port_enable_s cn63xx;
	struct cvmx_ipd_red_port_enable_s cn63xxp1;
	struct cvmx_ipd_red_port_enable_s cn66xx;
	struct cvmx_ipd_red_port_enable_s cnf71xx;
};

union cvmx_ipd_red_port_enable2 {
	uint64_t u64;
	struct cvmx_ipd_red_port_enable2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t prt_enb:12;
#else
		uint64_t prt_enb:12;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_ipd_red_port_enable2_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t prt_enb:4;
#else
		uint64_t prt_enb:4;
		uint64_t reserved_4_63:60;
#endif
	} cn52xx;
	struct cvmx_ipd_red_port_enable2_cn52xx cn52xxp1;
	struct cvmx_ipd_red_port_enable2_cn52xx cn56xx;
	struct cvmx_ipd_red_port_enable2_cn52xx cn56xxp1;
	struct cvmx_ipd_red_port_enable2_s cn61xx;
	struct cvmx_ipd_red_port_enable2_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t prt_enb:8;
#else
		uint64_t prt_enb:8;
		uint64_t reserved_8_63:56;
#endif
	} cn63xx;
	struct cvmx_ipd_red_port_enable2_cn63xx cn63xxp1;
	struct cvmx_ipd_red_port_enable2_s cn66xx;
	struct cvmx_ipd_red_port_enable2_s cnf71xx;
};

union cvmx_ipd_red_quex_param {
	uint64_t u64;
	struct cvmx_ipd_red_quex_param_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t use_pcnt:1;
		uint64_t new_con:8;
		uint64_t avg_con:8;
		uint64_t prb_con:32;
#else
		uint64_t prb_con:32;
		uint64_t avg_con:8;
		uint64_t new_con:8;
		uint64_t use_pcnt:1;
		uint64_t reserved_49_63:15;
#endif
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
	struct cvmx_ipd_red_quex_param_s cn61xx;
	struct cvmx_ipd_red_quex_param_s cn63xx;
	struct cvmx_ipd_red_quex_param_s cn63xxp1;
	struct cvmx_ipd_red_quex_param_s cn66xx;
	struct cvmx_ipd_red_quex_param_s cn68xx;
	struct cvmx_ipd_red_quex_param_s cn68xxp1;
	struct cvmx_ipd_red_quex_param_s cnf71xx;
};

union cvmx_ipd_req_wgt {
	uint64_t u64;
	struct cvmx_ipd_req_wgt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t wgt7:8;
		uint64_t wgt6:8;
		uint64_t wgt5:8;
		uint64_t wgt4:8;
		uint64_t wgt3:8;
		uint64_t wgt2:8;
		uint64_t wgt1:8;
		uint64_t wgt0:8;
#else
		uint64_t wgt0:8;
		uint64_t wgt1:8;
		uint64_t wgt2:8;
		uint64_t wgt3:8;
		uint64_t wgt4:8;
		uint64_t wgt5:8;
		uint64_t wgt6:8;
		uint64_t wgt7:8;
#endif
	} s;
	struct cvmx_ipd_req_wgt_s cn68xx;
};

union cvmx_ipd_sub_port_bp_page_cnt {
	uint64_t u64;
	struct cvmx_ipd_sub_port_bp_page_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t port:6;
		uint64_t page_cnt:25;
#else
		uint64_t page_cnt:25;
		uint64_t port:6;
		uint64_t reserved_31_63:33;
#endif
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
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn61xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn63xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn63xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn66xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn68xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn68xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cnf71xx;
};

union cvmx_ipd_sub_port_fcs {
	uint64_t u64;
	struct cvmx_ipd_sub_port_fcs_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t port_bit2:4;
		uint64_t reserved_32_35:4;
		uint64_t port_bit:32;
#else
		uint64_t port_bit:32;
		uint64_t reserved_32_35:4;
		uint64_t port_bit2:4;
		uint64_t reserved_40_63:24;
#endif
	} s;
	struct cvmx_ipd_sub_port_fcs_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t port_bit:3;
#else
		uint64_t port_bit:3;
		uint64_t reserved_3_63:61;
#endif
	} cn30xx;
	struct cvmx_ipd_sub_port_fcs_cn30xx cn31xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t port_bit:32;
#else
		uint64_t port_bit:32;
		uint64_t reserved_32_63:32;
#endif
	} cn38xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx cn38xxp2;
	struct cvmx_ipd_sub_port_fcs_cn30xx cn50xx;
	struct cvmx_ipd_sub_port_fcs_s cn52xx;
	struct cvmx_ipd_sub_port_fcs_s cn52xxp1;
	struct cvmx_ipd_sub_port_fcs_s cn56xx;
	struct cvmx_ipd_sub_port_fcs_s cn56xxp1;
	struct cvmx_ipd_sub_port_fcs_cn38xx cn58xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx cn58xxp1;
	struct cvmx_ipd_sub_port_fcs_s cn61xx;
	struct cvmx_ipd_sub_port_fcs_s cn63xx;
	struct cvmx_ipd_sub_port_fcs_s cn63xxp1;
	struct cvmx_ipd_sub_port_fcs_s cn66xx;
	struct cvmx_ipd_sub_port_fcs_s cnf71xx;
};

union cvmx_ipd_sub_port_qos_cnt {
	uint64_t u64;
	struct cvmx_ipd_sub_port_qos_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_41_63:23;
		uint64_t port_qos:9;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t port_qos:9;
		uint64_t reserved_41_63:23;
#endif
	} s;
	struct cvmx_ipd_sub_port_qos_cnt_s cn52xx;
	struct cvmx_ipd_sub_port_qos_cnt_s cn52xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s cn56xx;
	struct cvmx_ipd_sub_port_qos_cnt_s cn56xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s cn61xx;
	struct cvmx_ipd_sub_port_qos_cnt_s cn63xx;
	struct cvmx_ipd_sub_port_qos_cnt_s cn63xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s cn66xx;
	struct cvmx_ipd_sub_port_qos_cnt_s cn68xx;
	struct cvmx_ipd_sub_port_qos_cnt_s cn68xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s cnf71xx;
};

union cvmx_ipd_wqe_fpa_queue {
	uint64_t u64;
	struct cvmx_ipd_wqe_fpa_queue_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t wqe_pool:3;
#else
		uint64_t wqe_pool:3;
		uint64_t reserved_3_63:61;
#endif
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
	struct cvmx_ipd_wqe_fpa_queue_s cn61xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn63xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn63xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s cn66xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn68xx;
	struct cvmx_ipd_wqe_fpa_queue_s cn68xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s cnf71xx;
};

union cvmx_ipd_wqe_ptr_valid {
	uint64_t u64;
	struct cvmx_ipd_wqe_ptr_valid_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t ptr:29;
#else
		uint64_t ptr:29;
		uint64_t reserved_29_63:35;
#endif
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
	struct cvmx_ipd_wqe_ptr_valid_s cn61xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn63xx;
	struct cvmx_ipd_wqe_ptr_valid_s cn63xxp1;
	struct cvmx_ipd_wqe_ptr_valid_s cn66xx;
	struct cvmx_ipd_wqe_ptr_valid_s cnf71xx;
};

#endif
