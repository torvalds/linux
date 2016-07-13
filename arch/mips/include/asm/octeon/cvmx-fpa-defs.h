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

#ifndef __CVMX_FPA_DEFS_H__
#define __CVMX_FPA_DEFS_H__

#define CVMX_FPA_ADDR_RANGE_ERROR (CVMX_ADD_IO_SEG(0x0001180028000458ull))
#define CVMX_FPA_BIST_STATUS (CVMX_ADD_IO_SEG(0x00011800280000E8ull))
#define CVMX_FPA_CTL_STATUS (CVMX_ADD_IO_SEG(0x0001180028000050ull))
#define CVMX_FPA_FPF0_MARKS (CVMX_ADD_IO_SEG(0x0001180028000000ull))
#define CVMX_FPA_FPF0_SIZE (CVMX_ADD_IO_SEG(0x0001180028000058ull))
#define CVMX_FPA_FPF1_MARKS CVMX_FPA_FPFX_MARKS(1)
#define CVMX_FPA_FPF2_MARKS CVMX_FPA_FPFX_MARKS(2)
#define CVMX_FPA_FPF3_MARKS CVMX_FPA_FPFX_MARKS(3)
#define CVMX_FPA_FPF4_MARKS CVMX_FPA_FPFX_MARKS(4)
#define CVMX_FPA_FPF5_MARKS CVMX_FPA_FPFX_MARKS(5)
#define CVMX_FPA_FPF6_MARKS CVMX_FPA_FPFX_MARKS(6)
#define CVMX_FPA_FPF7_MARKS CVMX_FPA_FPFX_MARKS(7)
#define CVMX_FPA_FPF8_MARKS (CVMX_ADD_IO_SEG(0x0001180028000240ull))
#define CVMX_FPA_FPF8_SIZE (CVMX_ADD_IO_SEG(0x0001180028000248ull))
#define CVMX_FPA_FPFX_MARKS(offset) (CVMX_ADD_IO_SEG(0x0001180028000008ull) + ((offset) & 7) * 8 - 8*1)
#define CVMX_FPA_FPFX_SIZE(offset) (CVMX_ADD_IO_SEG(0x0001180028000060ull) + ((offset) & 7) * 8 - 8*1)
#define CVMX_FPA_INT_ENB (CVMX_ADD_IO_SEG(0x0001180028000048ull))
#define CVMX_FPA_INT_SUM (CVMX_ADD_IO_SEG(0x0001180028000040ull))
#define CVMX_FPA_PACKET_THRESHOLD (CVMX_ADD_IO_SEG(0x0001180028000460ull))
#define CVMX_FPA_POOLX_END_ADDR(offset) (CVMX_ADD_IO_SEG(0x0001180028000358ull) + ((offset) & 15) * 8)
#define CVMX_FPA_POOLX_START_ADDR(offset) (CVMX_ADD_IO_SEG(0x0001180028000258ull) + ((offset) & 15) * 8)
#define CVMX_FPA_POOLX_THRESHOLD(offset) (CVMX_ADD_IO_SEG(0x0001180028000140ull) + ((offset) & 15) * 8)
#define CVMX_FPA_QUE0_PAGE_INDEX CVMX_FPA_QUEX_PAGE_INDEX(0)
#define CVMX_FPA_QUE1_PAGE_INDEX CVMX_FPA_QUEX_PAGE_INDEX(1)
#define CVMX_FPA_QUE2_PAGE_INDEX CVMX_FPA_QUEX_PAGE_INDEX(2)
#define CVMX_FPA_QUE3_PAGE_INDEX CVMX_FPA_QUEX_PAGE_INDEX(3)
#define CVMX_FPA_QUE4_PAGE_INDEX CVMX_FPA_QUEX_PAGE_INDEX(4)
#define CVMX_FPA_QUE5_PAGE_INDEX CVMX_FPA_QUEX_PAGE_INDEX(5)
#define CVMX_FPA_QUE6_PAGE_INDEX CVMX_FPA_QUEX_PAGE_INDEX(6)
#define CVMX_FPA_QUE7_PAGE_INDEX CVMX_FPA_QUEX_PAGE_INDEX(7)
#define CVMX_FPA_QUE8_PAGE_INDEX (CVMX_ADD_IO_SEG(0x0001180028000250ull))
#define CVMX_FPA_QUEX_AVAILABLE(offset) (CVMX_ADD_IO_SEG(0x0001180028000098ull) + ((offset) & 15) * 8)
#define CVMX_FPA_QUEX_PAGE_INDEX(offset) (CVMX_ADD_IO_SEG(0x00011800280000F0ull) + ((offset) & 7) * 8)
#define CVMX_FPA_QUE_ACT (CVMX_ADD_IO_SEG(0x0001180028000138ull))
#define CVMX_FPA_QUE_EXP (CVMX_ADD_IO_SEG(0x0001180028000130ull))
#define CVMX_FPA_WART_CTL (CVMX_ADD_IO_SEG(0x00011800280000D8ull))
#define CVMX_FPA_WART_STATUS (CVMX_ADD_IO_SEG(0x00011800280000E0ull))
#define CVMX_FPA_WQE_THRESHOLD (CVMX_ADD_IO_SEG(0x0001180028000468ull))
#define CVMX_FPA_CLK_COUNT (CVMX_ADD_IO_SEG(0x00012800000000F0ull))

union cvmx_fpa_addr_range_error {
	uint64_t u64;
	struct cvmx_fpa_addr_range_error_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_38_63:26;
		uint64_t pool:5;
		uint64_t addr:33;
#else
		uint64_t addr:33;
		uint64_t pool:5;
		uint64_t reserved_38_63:26;
#endif
	} s;
	struct cvmx_fpa_addr_range_error_s cn61xx;
	struct cvmx_fpa_addr_range_error_s cn66xx;
	struct cvmx_fpa_addr_range_error_s cn68xx;
	struct cvmx_fpa_addr_range_error_s cn68xxp1;
	struct cvmx_fpa_addr_range_error_s cnf71xx;
};

union cvmx_fpa_bist_status {
	uint64_t u64;
	struct cvmx_fpa_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t frd:1;
		uint64_t fpf0:1;
		uint64_t fpf1:1;
		uint64_t ffr:1;
		uint64_t fdr:1;
#else
		uint64_t fdr:1;
		uint64_t ffr:1;
		uint64_t fpf1:1;
		uint64_t fpf0:1;
		uint64_t frd:1;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_fpa_bist_status_s cn30xx;
	struct cvmx_fpa_bist_status_s cn31xx;
	struct cvmx_fpa_bist_status_s cn38xx;
	struct cvmx_fpa_bist_status_s cn38xxp2;
	struct cvmx_fpa_bist_status_s cn50xx;
	struct cvmx_fpa_bist_status_s cn52xx;
	struct cvmx_fpa_bist_status_s cn52xxp1;
	struct cvmx_fpa_bist_status_s cn56xx;
	struct cvmx_fpa_bist_status_s cn56xxp1;
	struct cvmx_fpa_bist_status_s cn58xx;
	struct cvmx_fpa_bist_status_s cn58xxp1;
	struct cvmx_fpa_bist_status_s cn61xx;
	struct cvmx_fpa_bist_status_s cn63xx;
	struct cvmx_fpa_bist_status_s cn63xxp1;
	struct cvmx_fpa_bist_status_s cn66xx;
	struct cvmx_fpa_bist_status_s cn68xx;
	struct cvmx_fpa_bist_status_s cn68xxp1;
	struct cvmx_fpa_bist_status_s cnf71xx;
};

union cvmx_fpa_ctl_status {
	uint64_t u64;
	struct cvmx_fpa_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_21_63:43;
		uint64_t free_en:1;
		uint64_t ret_off:1;
		uint64_t req_off:1;
		uint64_t reset:1;
		uint64_t use_ldt:1;
		uint64_t use_stt:1;
		uint64_t enb:1;
		uint64_t mem1_err:7;
		uint64_t mem0_err:7;
#else
		uint64_t mem0_err:7;
		uint64_t mem1_err:7;
		uint64_t enb:1;
		uint64_t use_stt:1;
		uint64_t use_ldt:1;
		uint64_t reset:1;
		uint64_t req_off:1;
		uint64_t ret_off:1;
		uint64_t free_en:1;
		uint64_t reserved_21_63:43;
#endif
	} s;
	struct cvmx_fpa_ctl_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t reset:1;
		uint64_t use_ldt:1;
		uint64_t use_stt:1;
		uint64_t enb:1;
		uint64_t mem1_err:7;
		uint64_t mem0_err:7;
#else
		uint64_t mem0_err:7;
		uint64_t mem1_err:7;
		uint64_t enb:1;
		uint64_t use_stt:1;
		uint64_t use_ldt:1;
		uint64_t reset:1;
		uint64_t reserved_18_63:46;
#endif
	} cn30xx;
	struct cvmx_fpa_ctl_status_cn30xx cn31xx;
	struct cvmx_fpa_ctl_status_cn30xx cn38xx;
	struct cvmx_fpa_ctl_status_cn30xx cn38xxp2;
	struct cvmx_fpa_ctl_status_cn30xx cn50xx;
	struct cvmx_fpa_ctl_status_cn30xx cn52xx;
	struct cvmx_fpa_ctl_status_cn30xx cn52xxp1;
	struct cvmx_fpa_ctl_status_cn30xx cn56xx;
	struct cvmx_fpa_ctl_status_cn30xx cn56xxp1;
	struct cvmx_fpa_ctl_status_cn30xx cn58xx;
	struct cvmx_fpa_ctl_status_cn30xx cn58xxp1;
	struct cvmx_fpa_ctl_status_s cn61xx;
	struct cvmx_fpa_ctl_status_s cn63xx;
	struct cvmx_fpa_ctl_status_cn30xx cn63xxp1;
	struct cvmx_fpa_ctl_status_s cn66xx;
	struct cvmx_fpa_ctl_status_s cn68xx;
	struct cvmx_fpa_ctl_status_s cn68xxp1;
	struct cvmx_fpa_ctl_status_s cnf71xx;
};

union cvmx_fpa_fpfx_marks {
	uint64_t u64;
	struct cvmx_fpa_fpfx_marks_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t fpf_wr:11;
		uint64_t fpf_rd:11;
#else
		uint64_t fpf_rd:11;
		uint64_t fpf_wr:11;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_fpa_fpfx_marks_s cn38xx;
	struct cvmx_fpa_fpfx_marks_s cn38xxp2;
	struct cvmx_fpa_fpfx_marks_s cn56xx;
	struct cvmx_fpa_fpfx_marks_s cn56xxp1;
	struct cvmx_fpa_fpfx_marks_s cn58xx;
	struct cvmx_fpa_fpfx_marks_s cn58xxp1;
	struct cvmx_fpa_fpfx_marks_s cn61xx;
	struct cvmx_fpa_fpfx_marks_s cn63xx;
	struct cvmx_fpa_fpfx_marks_s cn63xxp1;
	struct cvmx_fpa_fpfx_marks_s cn66xx;
	struct cvmx_fpa_fpfx_marks_s cn68xx;
	struct cvmx_fpa_fpfx_marks_s cn68xxp1;
	struct cvmx_fpa_fpfx_marks_s cnf71xx;
};

union cvmx_fpa_fpfx_size {
	uint64_t u64;
	struct cvmx_fpa_fpfx_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t fpf_siz:11;
#else
		uint64_t fpf_siz:11;
		uint64_t reserved_11_63:53;
#endif
	} s;
	struct cvmx_fpa_fpfx_size_s cn38xx;
	struct cvmx_fpa_fpfx_size_s cn38xxp2;
	struct cvmx_fpa_fpfx_size_s cn56xx;
	struct cvmx_fpa_fpfx_size_s cn56xxp1;
	struct cvmx_fpa_fpfx_size_s cn58xx;
	struct cvmx_fpa_fpfx_size_s cn58xxp1;
	struct cvmx_fpa_fpfx_size_s cn61xx;
	struct cvmx_fpa_fpfx_size_s cn63xx;
	struct cvmx_fpa_fpfx_size_s cn63xxp1;
	struct cvmx_fpa_fpfx_size_s cn66xx;
	struct cvmx_fpa_fpfx_size_s cn68xx;
	struct cvmx_fpa_fpfx_size_s cn68xxp1;
	struct cvmx_fpa_fpfx_size_s cnf71xx;
};

union cvmx_fpa_fpf0_marks {
	uint64_t u64;
	struct cvmx_fpa_fpf0_marks_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t fpf_wr:12;
		uint64_t fpf_rd:12;
#else
		uint64_t fpf_rd:12;
		uint64_t fpf_wr:12;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_fpa_fpf0_marks_s cn38xx;
	struct cvmx_fpa_fpf0_marks_s cn38xxp2;
	struct cvmx_fpa_fpf0_marks_s cn56xx;
	struct cvmx_fpa_fpf0_marks_s cn56xxp1;
	struct cvmx_fpa_fpf0_marks_s cn58xx;
	struct cvmx_fpa_fpf0_marks_s cn58xxp1;
	struct cvmx_fpa_fpf0_marks_s cn61xx;
	struct cvmx_fpa_fpf0_marks_s cn63xx;
	struct cvmx_fpa_fpf0_marks_s cn63xxp1;
	struct cvmx_fpa_fpf0_marks_s cn66xx;
	struct cvmx_fpa_fpf0_marks_s cn68xx;
	struct cvmx_fpa_fpf0_marks_s cn68xxp1;
	struct cvmx_fpa_fpf0_marks_s cnf71xx;
};

union cvmx_fpa_fpf0_size {
	uint64_t u64;
	struct cvmx_fpa_fpf0_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t fpf_siz:12;
#else
		uint64_t fpf_siz:12;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_fpa_fpf0_size_s cn38xx;
	struct cvmx_fpa_fpf0_size_s cn38xxp2;
	struct cvmx_fpa_fpf0_size_s cn56xx;
	struct cvmx_fpa_fpf0_size_s cn56xxp1;
	struct cvmx_fpa_fpf0_size_s cn58xx;
	struct cvmx_fpa_fpf0_size_s cn58xxp1;
	struct cvmx_fpa_fpf0_size_s cn61xx;
	struct cvmx_fpa_fpf0_size_s cn63xx;
	struct cvmx_fpa_fpf0_size_s cn63xxp1;
	struct cvmx_fpa_fpf0_size_s cn66xx;
	struct cvmx_fpa_fpf0_size_s cn68xx;
	struct cvmx_fpa_fpf0_size_s cn68xxp1;
	struct cvmx_fpa_fpf0_size_s cnf71xx;
};

union cvmx_fpa_fpf8_marks {
	uint64_t u64;
	struct cvmx_fpa_fpf8_marks_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t fpf_wr:11;
		uint64_t fpf_rd:11;
#else
		uint64_t fpf_rd:11;
		uint64_t fpf_wr:11;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_fpa_fpf8_marks_s cn68xx;
	struct cvmx_fpa_fpf8_marks_s cn68xxp1;
};

union cvmx_fpa_fpf8_size {
	uint64_t u64;
	struct cvmx_fpa_fpf8_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t fpf_siz:12;
#else
		uint64_t fpf_siz:12;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_fpa_fpf8_size_s cn68xx;
	struct cvmx_fpa_fpf8_size_s cn68xxp1;
};

union cvmx_fpa_int_enb {
	uint64_t u64;
	struct cvmx_fpa_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_50_63:14;
		uint64_t paddr_e:1;
		uint64_t reserved_44_48:5;
		uint64_t free7:1;
		uint64_t free6:1;
		uint64_t free5:1;
		uint64_t free4:1;
		uint64_t free3:1;
		uint64_t free2:1;
		uint64_t free1:1;
		uint64_t free0:1;
		uint64_t pool7th:1;
		uint64_t pool6th:1;
		uint64_t pool5th:1;
		uint64_t pool4th:1;
		uint64_t pool3th:1;
		uint64_t pool2th:1;
		uint64_t pool1th:1;
		uint64_t pool0th:1;
		uint64_t q7_perr:1;
		uint64_t q7_coff:1;
		uint64_t q7_und:1;
		uint64_t q6_perr:1;
		uint64_t q6_coff:1;
		uint64_t q6_und:1;
		uint64_t q5_perr:1;
		uint64_t q5_coff:1;
		uint64_t q5_und:1;
		uint64_t q4_perr:1;
		uint64_t q4_coff:1;
		uint64_t q4_und:1;
		uint64_t q3_perr:1;
		uint64_t q3_coff:1;
		uint64_t q3_und:1;
		uint64_t q2_perr:1;
		uint64_t q2_coff:1;
		uint64_t q2_und:1;
		uint64_t q1_perr:1;
		uint64_t q1_coff:1;
		uint64_t q1_und:1;
		uint64_t q0_perr:1;
		uint64_t q0_coff:1;
		uint64_t q0_und:1;
		uint64_t fed1_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed0_sbe:1;
#else
		uint64_t fed0_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed1_dbe:1;
		uint64_t q0_und:1;
		uint64_t q0_coff:1;
		uint64_t q0_perr:1;
		uint64_t q1_und:1;
		uint64_t q1_coff:1;
		uint64_t q1_perr:1;
		uint64_t q2_und:1;
		uint64_t q2_coff:1;
		uint64_t q2_perr:1;
		uint64_t q3_und:1;
		uint64_t q3_coff:1;
		uint64_t q3_perr:1;
		uint64_t q4_und:1;
		uint64_t q4_coff:1;
		uint64_t q4_perr:1;
		uint64_t q5_und:1;
		uint64_t q5_coff:1;
		uint64_t q5_perr:1;
		uint64_t q6_und:1;
		uint64_t q6_coff:1;
		uint64_t q6_perr:1;
		uint64_t q7_und:1;
		uint64_t q7_coff:1;
		uint64_t q7_perr:1;
		uint64_t pool0th:1;
		uint64_t pool1th:1;
		uint64_t pool2th:1;
		uint64_t pool3th:1;
		uint64_t pool4th:1;
		uint64_t pool5th:1;
		uint64_t pool6th:1;
		uint64_t pool7th:1;
		uint64_t free0:1;
		uint64_t free1:1;
		uint64_t free2:1;
		uint64_t free3:1;
		uint64_t free4:1;
		uint64_t free5:1;
		uint64_t free6:1;
		uint64_t free7:1;
		uint64_t reserved_44_48:5;
		uint64_t paddr_e:1;
		uint64_t reserved_50_63:14;
#endif
	} s;
	struct cvmx_fpa_int_enb_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t q7_perr:1;
		uint64_t q7_coff:1;
		uint64_t q7_und:1;
		uint64_t q6_perr:1;
		uint64_t q6_coff:1;
		uint64_t q6_und:1;
		uint64_t q5_perr:1;
		uint64_t q5_coff:1;
		uint64_t q5_und:1;
		uint64_t q4_perr:1;
		uint64_t q4_coff:1;
		uint64_t q4_und:1;
		uint64_t q3_perr:1;
		uint64_t q3_coff:1;
		uint64_t q3_und:1;
		uint64_t q2_perr:1;
		uint64_t q2_coff:1;
		uint64_t q2_und:1;
		uint64_t q1_perr:1;
		uint64_t q1_coff:1;
		uint64_t q1_und:1;
		uint64_t q0_perr:1;
		uint64_t q0_coff:1;
		uint64_t q0_und:1;
		uint64_t fed1_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed0_sbe:1;
#else
		uint64_t fed0_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed1_dbe:1;
		uint64_t q0_und:1;
		uint64_t q0_coff:1;
		uint64_t q0_perr:1;
		uint64_t q1_und:1;
		uint64_t q1_coff:1;
		uint64_t q1_perr:1;
		uint64_t q2_und:1;
		uint64_t q2_coff:1;
		uint64_t q2_perr:1;
		uint64_t q3_und:1;
		uint64_t q3_coff:1;
		uint64_t q3_perr:1;
		uint64_t q4_und:1;
		uint64_t q4_coff:1;
		uint64_t q4_perr:1;
		uint64_t q5_und:1;
		uint64_t q5_coff:1;
		uint64_t q5_perr:1;
		uint64_t q6_und:1;
		uint64_t q6_coff:1;
		uint64_t q6_perr:1;
		uint64_t q7_und:1;
		uint64_t q7_coff:1;
		uint64_t q7_perr:1;
		uint64_t reserved_28_63:36;
#endif
	} cn30xx;
	struct cvmx_fpa_int_enb_cn30xx cn31xx;
	struct cvmx_fpa_int_enb_cn30xx cn38xx;
	struct cvmx_fpa_int_enb_cn30xx cn38xxp2;
	struct cvmx_fpa_int_enb_cn30xx cn50xx;
	struct cvmx_fpa_int_enb_cn30xx cn52xx;
	struct cvmx_fpa_int_enb_cn30xx cn52xxp1;
	struct cvmx_fpa_int_enb_cn30xx cn56xx;
	struct cvmx_fpa_int_enb_cn30xx cn56xxp1;
	struct cvmx_fpa_int_enb_cn30xx cn58xx;
	struct cvmx_fpa_int_enb_cn30xx cn58xxp1;
	struct cvmx_fpa_int_enb_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_50_63:14;
		uint64_t paddr_e:1;
		uint64_t res_44:5;
		uint64_t free7:1;
		uint64_t free6:1;
		uint64_t free5:1;
		uint64_t free4:1;
		uint64_t free3:1;
		uint64_t free2:1;
		uint64_t free1:1;
		uint64_t free0:1;
		uint64_t pool7th:1;
		uint64_t pool6th:1;
		uint64_t pool5th:1;
		uint64_t pool4th:1;
		uint64_t pool3th:1;
		uint64_t pool2th:1;
		uint64_t pool1th:1;
		uint64_t pool0th:1;
		uint64_t q7_perr:1;
		uint64_t q7_coff:1;
		uint64_t q7_und:1;
		uint64_t q6_perr:1;
		uint64_t q6_coff:1;
		uint64_t q6_und:1;
		uint64_t q5_perr:1;
		uint64_t q5_coff:1;
		uint64_t q5_und:1;
		uint64_t q4_perr:1;
		uint64_t q4_coff:1;
		uint64_t q4_und:1;
		uint64_t q3_perr:1;
		uint64_t q3_coff:1;
		uint64_t q3_und:1;
		uint64_t q2_perr:1;
		uint64_t q2_coff:1;
		uint64_t q2_und:1;
		uint64_t q1_perr:1;
		uint64_t q1_coff:1;
		uint64_t q1_und:1;
		uint64_t q0_perr:1;
		uint64_t q0_coff:1;
		uint64_t q0_und:1;
		uint64_t fed1_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed0_sbe:1;
#else
		uint64_t fed0_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed1_dbe:1;
		uint64_t q0_und:1;
		uint64_t q0_coff:1;
		uint64_t q0_perr:1;
		uint64_t q1_und:1;
		uint64_t q1_coff:1;
		uint64_t q1_perr:1;
		uint64_t q2_und:1;
		uint64_t q2_coff:1;
		uint64_t q2_perr:1;
		uint64_t q3_und:1;
		uint64_t q3_coff:1;
		uint64_t q3_perr:1;
		uint64_t q4_und:1;
		uint64_t q4_coff:1;
		uint64_t q4_perr:1;
		uint64_t q5_und:1;
		uint64_t q5_coff:1;
		uint64_t q5_perr:1;
		uint64_t q6_und:1;
		uint64_t q6_coff:1;
		uint64_t q6_perr:1;
		uint64_t q7_und:1;
		uint64_t q7_coff:1;
		uint64_t q7_perr:1;
		uint64_t pool0th:1;
		uint64_t pool1th:1;
		uint64_t pool2th:1;
		uint64_t pool3th:1;
		uint64_t pool4th:1;
		uint64_t pool5th:1;
		uint64_t pool6th:1;
		uint64_t pool7th:1;
		uint64_t free0:1;
		uint64_t free1:1;
		uint64_t free2:1;
		uint64_t free3:1;
		uint64_t free4:1;
		uint64_t free5:1;
		uint64_t free6:1;
		uint64_t free7:1;
		uint64_t res_44:5;
		uint64_t paddr_e:1;
		uint64_t reserved_50_63:14;
#endif
	} cn61xx;
	struct cvmx_fpa_int_enb_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t free7:1;
		uint64_t free6:1;
		uint64_t free5:1;
		uint64_t free4:1;
		uint64_t free3:1;
		uint64_t free2:1;
		uint64_t free1:1;
		uint64_t free0:1;
		uint64_t pool7th:1;
		uint64_t pool6th:1;
		uint64_t pool5th:1;
		uint64_t pool4th:1;
		uint64_t pool3th:1;
		uint64_t pool2th:1;
		uint64_t pool1th:1;
		uint64_t pool0th:1;
		uint64_t q7_perr:1;
		uint64_t q7_coff:1;
		uint64_t q7_und:1;
		uint64_t q6_perr:1;
		uint64_t q6_coff:1;
		uint64_t q6_und:1;
		uint64_t q5_perr:1;
		uint64_t q5_coff:1;
		uint64_t q5_und:1;
		uint64_t q4_perr:1;
		uint64_t q4_coff:1;
		uint64_t q4_und:1;
		uint64_t q3_perr:1;
		uint64_t q3_coff:1;
		uint64_t q3_und:1;
		uint64_t q2_perr:1;
		uint64_t q2_coff:1;
		uint64_t q2_und:1;
		uint64_t q1_perr:1;
		uint64_t q1_coff:1;
		uint64_t q1_und:1;
		uint64_t q0_perr:1;
		uint64_t q0_coff:1;
		uint64_t q0_und:1;
		uint64_t fed1_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed0_sbe:1;
#else
		uint64_t fed0_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed1_dbe:1;
		uint64_t q0_und:1;
		uint64_t q0_coff:1;
		uint64_t q0_perr:1;
		uint64_t q1_und:1;
		uint64_t q1_coff:1;
		uint64_t q1_perr:1;
		uint64_t q2_und:1;
		uint64_t q2_coff:1;
		uint64_t q2_perr:1;
		uint64_t q3_und:1;
		uint64_t q3_coff:1;
		uint64_t q3_perr:1;
		uint64_t q4_und:1;
		uint64_t q4_coff:1;
		uint64_t q4_perr:1;
		uint64_t q5_und:1;
		uint64_t q5_coff:1;
		uint64_t q5_perr:1;
		uint64_t q6_und:1;
		uint64_t q6_coff:1;
		uint64_t q6_perr:1;
		uint64_t q7_und:1;
		uint64_t q7_coff:1;
		uint64_t q7_perr:1;
		uint64_t pool0th:1;
		uint64_t pool1th:1;
		uint64_t pool2th:1;
		uint64_t pool3th:1;
		uint64_t pool4th:1;
		uint64_t pool5th:1;
		uint64_t pool6th:1;
		uint64_t pool7th:1;
		uint64_t free0:1;
		uint64_t free1:1;
		uint64_t free2:1;
		uint64_t free3:1;
		uint64_t free4:1;
		uint64_t free5:1;
		uint64_t free6:1;
		uint64_t free7:1;
		uint64_t reserved_44_63:20;
#endif
	} cn63xx;
	struct cvmx_fpa_int_enb_cn30xx cn63xxp1;
	struct cvmx_fpa_int_enb_cn61xx cn66xx;
	struct cvmx_fpa_int_enb_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_50_63:14;
		uint64_t paddr_e:1;
		uint64_t pool8th:1;
		uint64_t q8_perr:1;
		uint64_t q8_coff:1;
		uint64_t q8_und:1;
		uint64_t free8:1;
		uint64_t free7:1;
		uint64_t free6:1;
		uint64_t free5:1;
		uint64_t free4:1;
		uint64_t free3:1;
		uint64_t free2:1;
		uint64_t free1:1;
		uint64_t free0:1;
		uint64_t pool7th:1;
		uint64_t pool6th:1;
		uint64_t pool5th:1;
		uint64_t pool4th:1;
		uint64_t pool3th:1;
		uint64_t pool2th:1;
		uint64_t pool1th:1;
		uint64_t pool0th:1;
		uint64_t q7_perr:1;
		uint64_t q7_coff:1;
		uint64_t q7_und:1;
		uint64_t q6_perr:1;
		uint64_t q6_coff:1;
		uint64_t q6_und:1;
		uint64_t q5_perr:1;
		uint64_t q5_coff:1;
		uint64_t q5_und:1;
		uint64_t q4_perr:1;
		uint64_t q4_coff:1;
		uint64_t q4_und:1;
		uint64_t q3_perr:1;
		uint64_t q3_coff:1;
		uint64_t q3_und:1;
		uint64_t q2_perr:1;
		uint64_t q2_coff:1;
		uint64_t q2_und:1;
		uint64_t q1_perr:1;
		uint64_t q1_coff:1;
		uint64_t q1_und:1;
		uint64_t q0_perr:1;
		uint64_t q0_coff:1;
		uint64_t q0_und:1;
		uint64_t fed1_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed0_sbe:1;
#else
		uint64_t fed0_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed1_dbe:1;
		uint64_t q0_und:1;
		uint64_t q0_coff:1;
		uint64_t q0_perr:1;
		uint64_t q1_und:1;
		uint64_t q1_coff:1;
		uint64_t q1_perr:1;
		uint64_t q2_und:1;
		uint64_t q2_coff:1;
		uint64_t q2_perr:1;
		uint64_t q3_und:1;
		uint64_t q3_coff:1;
		uint64_t q3_perr:1;
		uint64_t q4_und:1;
		uint64_t q4_coff:1;
		uint64_t q4_perr:1;
		uint64_t q5_und:1;
		uint64_t q5_coff:1;
		uint64_t q5_perr:1;
		uint64_t q6_und:1;
		uint64_t q6_coff:1;
		uint64_t q6_perr:1;
		uint64_t q7_und:1;
		uint64_t q7_coff:1;
		uint64_t q7_perr:1;
		uint64_t pool0th:1;
		uint64_t pool1th:1;
		uint64_t pool2th:1;
		uint64_t pool3th:1;
		uint64_t pool4th:1;
		uint64_t pool5th:1;
		uint64_t pool6th:1;
		uint64_t pool7th:1;
		uint64_t free0:1;
		uint64_t free1:1;
		uint64_t free2:1;
		uint64_t free3:1;
		uint64_t free4:1;
		uint64_t free5:1;
		uint64_t free6:1;
		uint64_t free7:1;
		uint64_t free8:1;
		uint64_t q8_und:1;
		uint64_t q8_coff:1;
		uint64_t q8_perr:1;
		uint64_t pool8th:1;
		uint64_t paddr_e:1;
		uint64_t reserved_50_63:14;
#endif
	} cn68xx;
	struct cvmx_fpa_int_enb_cn68xx cn68xxp1;
	struct cvmx_fpa_int_enb_cn61xx cnf71xx;
};

union cvmx_fpa_int_sum {
	uint64_t u64;
	struct cvmx_fpa_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_50_63:14;
		uint64_t paddr_e:1;
		uint64_t pool8th:1;
		uint64_t q8_perr:1;
		uint64_t q8_coff:1;
		uint64_t q8_und:1;
		uint64_t free8:1;
		uint64_t free7:1;
		uint64_t free6:1;
		uint64_t free5:1;
		uint64_t free4:1;
		uint64_t free3:1;
		uint64_t free2:1;
		uint64_t free1:1;
		uint64_t free0:1;
		uint64_t pool7th:1;
		uint64_t pool6th:1;
		uint64_t pool5th:1;
		uint64_t pool4th:1;
		uint64_t pool3th:1;
		uint64_t pool2th:1;
		uint64_t pool1th:1;
		uint64_t pool0th:1;
		uint64_t q7_perr:1;
		uint64_t q7_coff:1;
		uint64_t q7_und:1;
		uint64_t q6_perr:1;
		uint64_t q6_coff:1;
		uint64_t q6_und:1;
		uint64_t q5_perr:1;
		uint64_t q5_coff:1;
		uint64_t q5_und:1;
		uint64_t q4_perr:1;
		uint64_t q4_coff:1;
		uint64_t q4_und:1;
		uint64_t q3_perr:1;
		uint64_t q3_coff:1;
		uint64_t q3_und:1;
		uint64_t q2_perr:1;
		uint64_t q2_coff:1;
		uint64_t q2_und:1;
		uint64_t q1_perr:1;
		uint64_t q1_coff:1;
		uint64_t q1_und:1;
		uint64_t q0_perr:1;
		uint64_t q0_coff:1;
		uint64_t q0_und:1;
		uint64_t fed1_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed0_sbe:1;
#else
		uint64_t fed0_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed1_dbe:1;
		uint64_t q0_und:1;
		uint64_t q0_coff:1;
		uint64_t q0_perr:1;
		uint64_t q1_und:1;
		uint64_t q1_coff:1;
		uint64_t q1_perr:1;
		uint64_t q2_und:1;
		uint64_t q2_coff:1;
		uint64_t q2_perr:1;
		uint64_t q3_und:1;
		uint64_t q3_coff:1;
		uint64_t q3_perr:1;
		uint64_t q4_und:1;
		uint64_t q4_coff:1;
		uint64_t q4_perr:1;
		uint64_t q5_und:1;
		uint64_t q5_coff:1;
		uint64_t q5_perr:1;
		uint64_t q6_und:1;
		uint64_t q6_coff:1;
		uint64_t q6_perr:1;
		uint64_t q7_und:1;
		uint64_t q7_coff:1;
		uint64_t q7_perr:1;
		uint64_t pool0th:1;
		uint64_t pool1th:1;
		uint64_t pool2th:1;
		uint64_t pool3th:1;
		uint64_t pool4th:1;
		uint64_t pool5th:1;
		uint64_t pool6th:1;
		uint64_t pool7th:1;
		uint64_t free0:1;
		uint64_t free1:1;
		uint64_t free2:1;
		uint64_t free3:1;
		uint64_t free4:1;
		uint64_t free5:1;
		uint64_t free6:1;
		uint64_t free7:1;
		uint64_t free8:1;
		uint64_t q8_und:1;
		uint64_t q8_coff:1;
		uint64_t q8_perr:1;
		uint64_t pool8th:1;
		uint64_t paddr_e:1;
		uint64_t reserved_50_63:14;
#endif
	} s;
	struct cvmx_fpa_int_sum_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t q7_perr:1;
		uint64_t q7_coff:1;
		uint64_t q7_und:1;
		uint64_t q6_perr:1;
		uint64_t q6_coff:1;
		uint64_t q6_und:1;
		uint64_t q5_perr:1;
		uint64_t q5_coff:1;
		uint64_t q5_und:1;
		uint64_t q4_perr:1;
		uint64_t q4_coff:1;
		uint64_t q4_und:1;
		uint64_t q3_perr:1;
		uint64_t q3_coff:1;
		uint64_t q3_und:1;
		uint64_t q2_perr:1;
		uint64_t q2_coff:1;
		uint64_t q2_und:1;
		uint64_t q1_perr:1;
		uint64_t q1_coff:1;
		uint64_t q1_und:1;
		uint64_t q0_perr:1;
		uint64_t q0_coff:1;
		uint64_t q0_und:1;
		uint64_t fed1_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed0_sbe:1;
#else
		uint64_t fed0_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed1_dbe:1;
		uint64_t q0_und:1;
		uint64_t q0_coff:1;
		uint64_t q0_perr:1;
		uint64_t q1_und:1;
		uint64_t q1_coff:1;
		uint64_t q1_perr:1;
		uint64_t q2_und:1;
		uint64_t q2_coff:1;
		uint64_t q2_perr:1;
		uint64_t q3_und:1;
		uint64_t q3_coff:1;
		uint64_t q3_perr:1;
		uint64_t q4_und:1;
		uint64_t q4_coff:1;
		uint64_t q4_perr:1;
		uint64_t q5_und:1;
		uint64_t q5_coff:1;
		uint64_t q5_perr:1;
		uint64_t q6_und:1;
		uint64_t q6_coff:1;
		uint64_t q6_perr:1;
		uint64_t q7_und:1;
		uint64_t q7_coff:1;
		uint64_t q7_perr:1;
		uint64_t reserved_28_63:36;
#endif
	} cn30xx;
	struct cvmx_fpa_int_sum_cn30xx cn31xx;
	struct cvmx_fpa_int_sum_cn30xx cn38xx;
	struct cvmx_fpa_int_sum_cn30xx cn38xxp2;
	struct cvmx_fpa_int_sum_cn30xx cn50xx;
	struct cvmx_fpa_int_sum_cn30xx cn52xx;
	struct cvmx_fpa_int_sum_cn30xx cn52xxp1;
	struct cvmx_fpa_int_sum_cn30xx cn56xx;
	struct cvmx_fpa_int_sum_cn30xx cn56xxp1;
	struct cvmx_fpa_int_sum_cn30xx cn58xx;
	struct cvmx_fpa_int_sum_cn30xx cn58xxp1;
	struct cvmx_fpa_int_sum_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_50_63:14;
		uint64_t paddr_e:1;
		uint64_t reserved_44_48:5;
		uint64_t free7:1;
		uint64_t free6:1;
		uint64_t free5:1;
		uint64_t free4:1;
		uint64_t free3:1;
		uint64_t free2:1;
		uint64_t free1:1;
		uint64_t free0:1;
		uint64_t pool7th:1;
		uint64_t pool6th:1;
		uint64_t pool5th:1;
		uint64_t pool4th:1;
		uint64_t pool3th:1;
		uint64_t pool2th:1;
		uint64_t pool1th:1;
		uint64_t pool0th:1;
		uint64_t q7_perr:1;
		uint64_t q7_coff:1;
		uint64_t q7_und:1;
		uint64_t q6_perr:1;
		uint64_t q6_coff:1;
		uint64_t q6_und:1;
		uint64_t q5_perr:1;
		uint64_t q5_coff:1;
		uint64_t q5_und:1;
		uint64_t q4_perr:1;
		uint64_t q4_coff:1;
		uint64_t q4_und:1;
		uint64_t q3_perr:1;
		uint64_t q3_coff:1;
		uint64_t q3_und:1;
		uint64_t q2_perr:1;
		uint64_t q2_coff:1;
		uint64_t q2_und:1;
		uint64_t q1_perr:1;
		uint64_t q1_coff:1;
		uint64_t q1_und:1;
		uint64_t q0_perr:1;
		uint64_t q0_coff:1;
		uint64_t q0_und:1;
		uint64_t fed1_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed0_sbe:1;
#else
		uint64_t fed0_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed1_dbe:1;
		uint64_t q0_und:1;
		uint64_t q0_coff:1;
		uint64_t q0_perr:1;
		uint64_t q1_und:1;
		uint64_t q1_coff:1;
		uint64_t q1_perr:1;
		uint64_t q2_und:1;
		uint64_t q2_coff:1;
		uint64_t q2_perr:1;
		uint64_t q3_und:1;
		uint64_t q3_coff:1;
		uint64_t q3_perr:1;
		uint64_t q4_und:1;
		uint64_t q4_coff:1;
		uint64_t q4_perr:1;
		uint64_t q5_und:1;
		uint64_t q5_coff:1;
		uint64_t q5_perr:1;
		uint64_t q6_und:1;
		uint64_t q6_coff:1;
		uint64_t q6_perr:1;
		uint64_t q7_und:1;
		uint64_t q7_coff:1;
		uint64_t q7_perr:1;
		uint64_t pool0th:1;
		uint64_t pool1th:1;
		uint64_t pool2th:1;
		uint64_t pool3th:1;
		uint64_t pool4th:1;
		uint64_t pool5th:1;
		uint64_t pool6th:1;
		uint64_t pool7th:1;
		uint64_t free0:1;
		uint64_t free1:1;
		uint64_t free2:1;
		uint64_t free3:1;
		uint64_t free4:1;
		uint64_t free5:1;
		uint64_t free6:1;
		uint64_t free7:1;
		uint64_t reserved_44_48:5;
		uint64_t paddr_e:1;
		uint64_t reserved_50_63:14;
#endif
	} cn61xx;
	struct cvmx_fpa_int_sum_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t free7:1;
		uint64_t free6:1;
		uint64_t free5:1;
		uint64_t free4:1;
		uint64_t free3:1;
		uint64_t free2:1;
		uint64_t free1:1;
		uint64_t free0:1;
		uint64_t pool7th:1;
		uint64_t pool6th:1;
		uint64_t pool5th:1;
		uint64_t pool4th:1;
		uint64_t pool3th:1;
		uint64_t pool2th:1;
		uint64_t pool1th:1;
		uint64_t pool0th:1;
		uint64_t q7_perr:1;
		uint64_t q7_coff:1;
		uint64_t q7_und:1;
		uint64_t q6_perr:1;
		uint64_t q6_coff:1;
		uint64_t q6_und:1;
		uint64_t q5_perr:1;
		uint64_t q5_coff:1;
		uint64_t q5_und:1;
		uint64_t q4_perr:1;
		uint64_t q4_coff:1;
		uint64_t q4_und:1;
		uint64_t q3_perr:1;
		uint64_t q3_coff:1;
		uint64_t q3_und:1;
		uint64_t q2_perr:1;
		uint64_t q2_coff:1;
		uint64_t q2_und:1;
		uint64_t q1_perr:1;
		uint64_t q1_coff:1;
		uint64_t q1_und:1;
		uint64_t q0_perr:1;
		uint64_t q0_coff:1;
		uint64_t q0_und:1;
		uint64_t fed1_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed0_sbe:1;
#else
		uint64_t fed0_sbe:1;
		uint64_t fed0_dbe:1;
		uint64_t fed1_sbe:1;
		uint64_t fed1_dbe:1;
		uint64_t q0_und:1;
		uint64_t q0_coff:1;
		uint64_t q0_perr:1;
		uint64_t q1_und:1;
		uint64_t q1_coff:1;
		uint64_t q1_perr:1;
		uint64_t q2_und:1;
		uint64_t q2_coff:1;
		uint64_t q2_perr:1;
		uint64_t q3_und:1;
		uint64_t q3_coff:1;
		uint64_t q3_perr:1;
		uint64_t q4_und:1;
		uint64_t q4_coff:1;
		uint64_t q4_perr:1;
		uint64_t q5_und:1;
		uint64_t q5_coff:1;
		uint64_t q5_perr:1;
		uint64_t q6_und:1;
		uint64_t q6_coff:1;
		uint64_t q6_perr:1;
		uint64_t q7_und:1;
		uint64_t q7_coff:1;
		uint64_t q7_perr:1;
		uint64_t pool0th:1;
		uint64_t pool1th:1;
		uint64_t pool2th:1;
		uint64_t pool3th:1;
		uint64_t pool4th:1;
		uint64_t pool5th:1;
		uint64_t pool6th:1;
		uint64_t pool7th:1;
		uint64_t free0:1;
		uint64_t free1:1;
		uint64_t free2:1;
		uint64_t free3:1;
		uint64_t free4:1;
		uint64_t free5:1;
		uint64_t free6:1;
		uint64_t free7:1;
		uint64_t reserved_44_63:20;
#endif
	} cn63xx;
	struct cvmx_fpa_int_sum_cn30xx cn63xxp1;
	struct cvmx_fpa_int_sum_cn61xx cn66xx;
	struct cvmx_fpa_int_sum_s cn68xx;
	struct cvmx_fpa_int_sum_s cn68xxp1;
	struct cvmx_fpa_int_sum_cn61xx cnf71xx;
};

union cvmx_fpa_packet_threshold {
	uint64_t u64;
	struct cvmx_fpa_packet_threshold_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t thresh:32;
#else
		uint64_t thresh:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_fpa_packet_threshold_s cn61xx;
	struct cvmx_fpa_packet_threshold_s cn63xx;
	struct cvmx_fpa_packet_threshold_s cn66xx;
	struct cvmx_fpa_packet_threshold_s cn68xx;
	struct cvmx_fpa_packet_threshold_s cn68xxp1;
	struct cvmx_fpa_packet_threshold_s cnf71xx;
};

union cvmx_fpa_poolx_end_addr {
	uint64_t u64;
	struct cvmx_fpa_poolx_end_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t addr:33;
#else
		uint64_t addr:33;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_fpa_poolx_end_addr_s cn61xx;
	struct cvmx_fpa_poolx_end_addr_s cn66xx;
	struct cvmx_fpa_poolx_end_addr_s cn68xx;
	struct cvmx_fpa_poolx_end_addr_s cn68xxp1;
	struct cvmx_fpa_poolx_end_addr_s cnf71xx;
};

union cvmx_fpa_poolx_start_addr {
	uint64_t u64;
	struct cvmx_fpa_poolx_start_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t addr:33;
#else
		uint64_t addr:33;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_fpa_poolx_start_addr_s cn61xx;
	struct cvmx_fpa_poolx_start_addr_s cn66xx;
	struct cvmx_fpa_poolx_start_addr_s cn68xx;
	struct cvmx_fpa_poolx_start_addr_s cn68xxp1;
	struct cvmx_fpa_poolx_start_addr_s cnf71xx;
};

union cvmx_fpa_poolx_threshold {
	uint64_t u64;
	struct cvmx_fpa_poolx_threshold_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t thresh:32;
#else
		uint64_t thresh:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_fpa_poolx_threshold_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t thresh:29;
#else
		uint64_t thresh:29;
		uint64_t reserved_29_63:35;
#endif
	} cn61xx;
	struct cvmx_fpa_poolx_threshold_cn61xx cn63xx;
	struct cvmx_fpa_poolx_threshold_cn61xx cn66xx;
	struct cvmx_fpa_poolx_threshold_s cn68xx;
	struct cvmx_fpa_poolx_threshold_s cn68xxp1;
	struct cvmx_fpa_poolx_threshold_cn61xx cnf71xx;
};

union cvmx_fpa_quex_available {
	uint64_t u64;
	struct cvmx_fpa_quex_available_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t que_siz:32;
#else
		uint64_t que_siz:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_fpa_quex_available_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t que_siz:29;
#else
		uint64_t que_siz:29;
		uint64_t reserved_29_63:35;
#endif
	} cn30xx;
	struct cvmx_fpa_quex_available_cn30xx cn31xx;
	struct cvmx_fpa_quex_available_cn30xx cn38xx;
	struct cvmx_fpa_quex_available_cn30xx cn38xxp2;
	struct cvmx_fpa_quex_available_cn30xx cn50xx;
	struct cvmx_fpa_quex_available_cn30xx cn52xx;
	struct cvmx_fpa_quex_available_cn30xx cn52xxp1;
	struct cvmx_fpa_quex_available_cn30xx cn56xx;
	struct cvmx_fpa_quex_available_cn30xx cn56xxp1;
	struct cvmx_fpa_quex_available_cn30xx cn58xx;
	struct cvmx_fpa_quex_available_cn30xx cn58xxp1;
	struct cvmx_fpa_quex_available_cn30xx cn61xx;
	struct cvmx_fpa_quex_available_cn30xx cn63xx;
	struct cvmx_fpa_quex_available_cn30xx cn63xxp1;
	struct cvmx_fpa_quex_available_cn30xx cn66xx;
	struct cvmx_fpa_quex_available_s cn68xx;
	struct cvmx_fpa_quex_available_s cn68xxp1;
	struct cvmx_fpa_quex_available_cn30xx cnf71xx;
};

union cvmx_fpa_quex_page_index {
	uint64_t u64;
	struct cvmx_fpa_quex_page_index_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t pg_num:25;
#else
		uint64_t pg_num:25;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_fpa_quex_page_index_s cn30xx;
	struct cvmx_fpa_quex_page_index_s cn31xx;
	struct cvmx_fpa_quex_page_index_s cn38xx;
	struct cvmx_fpa_quex_page_index_s cn38xxp2;
	struct cvmx_fpa_quex_page_index_s cn50xx;
	struct cvmx_fpa_quex_page_index_s cn52xx;
	struct cvmx_fpa_quex_page_index_s cn52xxp1;
	struct cvmx_fpa_quex_page_index_s cn56xx;
	struct cvmx_fpa_quex_page_index_s cn56xxp1;
	struct cvmx_fpa_quex_page_index_s cn58xx;
	struct cvmx_fpa_quex_page_index_s cn58xxp1;
	struct cvmx_fpa_quex_page_index_s cn61xx;
	struct cvmx_fpa_quex_page_index_s cn63xx;
	struct cvmx_fpa_quex_page_index_s cn63xxp1;
	struct cvmx_fpa_quex_page_index_s cn66xx;
	struct cvmx_fpa_quex_page_index_s cn68xx;
	struct cvmx_fpa_quex_page_index_s cn68xxp1;
	struct cvmx_fpa_quex_page_index_s cnf71xx;
};

union cvmx_fpa_que8_page_index {
	uint64_t u64;
	struct cvmx_fpa_que8_page_index_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t pg_num:25;
#else
		uint64_t pg_num:25;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_fpa_que8_page_index_s cn68xx;
	struct cvmx_fpa_que8_page_index_s cn68xxp1;
};

union cvmx_fpa_que_act {
	uint64_t u64;
	struct cvmx_fpa_que_act_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t act_que:3;
		uint64_t act_indx:26;
#else
		uint64_t act_indx:26;
		uint64_t act_que:3;
		uint64_t reserved_29_63:35;
#endif
	} s;
	struct cvmx_fpa_que_act_s cn30xx;
	struct cvmx_fpa_que_act_s cn31xx;
	struct cvmx_fpa_que_act_s cn38xx;
	struct cvmx_fpa_que_act_s cn38xxp2;
	struct cvmx_fpa_que_act_s cn50xx;
	struct cvmx_fpa_que_act_s cn52xx;
	struct cvmx_fpa_que_act_s cn52xxp1;
	struct cvmx_fpa_que_act_s cn56xx;
	struct cvmx_fpa_que_act_s cn56xxp1;
	struct cvmx_fpa_que_act_s cn58xx;
	struct cvmx_fpa_que_act_s cn58xxp1;
	struct cvmx_fpa_que_act_s cn61xx;
	struct cvmx_fpa_que_act_s cn63xx;
	struct cvmx_fpa_que_act_s cn63xxp1;
	struct cvmx_fpa_que_act_s cn66xx;
	struct cvmx_fpa_que_act_s cn68xx;
	struct cvmx_fpa_que_act_s cn68xxp1;
	struct cvmx_fpa_que_act_s cnf71xx;
};

union cvmx_fpa_que_exp {
	uint64_t u64;
	struct cvmx_fpa_que_exp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t exp_que:3;
		uint64_t exp_indx:26;
#else
		uint64_t exp_indx:26;
		uint64_t exp_que:3;
		uint64_t reserved_29_63:35;
#endif
	} s;
	struct cvmx_fpa_que_exp_s cn30xx;
	struct cvmx_fpa_que_exp_s cn31xx;
	struct cvmx_fpa_que_exp_s cn38xx;
	struct cvmx_fpa_que_exp_s cn38xxp2;
	struct cvmx_fpa_que_exp_s cn50xx;
	struct cvmx_fpa_que_exp_s cn52xx;
	struct cvmx_fpa_que_exp_s cn52xxp1;
	struct cvmx_fpa_que_exp_s cn56xx;
	struct cvmx_fpa_que_exp_s cn56xxp1;
	struct cvmx_fpa_que_exp_s cn58xx;
	struct cvmx_fpa_que_exp_s cn58xxp1;
	struct cvmx_fpa_que_exp_s cn61xx;
	struct cvmx_fpa_que_exp_s cn63xx;
	struct cvmx_fpa_que_exp_s cn63xxp1;
	struct cvmx_fpa_que_exp_s cn66xx;
	struct cvmx_fpa_que_exp_s cn68xx;
	struct cvmx_fpa_que_exp_s cn68xxp1;
	struct cvmx_fpa_que_exp_s cnf71xx;
};

union cvmx_fpa_wart_ctl {
	uint64_t u64;
	struct cvmx_fpa_wart_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t ctl:16;
#else
		uint64_t ctl:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_fpa_wart_ctl_s cn30xx;
	struct cvmx_fpa_wart_ctl_s cn31xx;
	struct cvmx_fpa_wart_ctl_s cn38xx;
	struct cvmx_fpa_wart_ctl_s cn38xxp2;
	struct cvmx_fpa_wart_ctl_s cn50xx;
	struct cvmx_fpa_wart_ctl_s cn52xx;
	struct cvmx_fpa_wart_ctl_s cn52xxp1;
	struct cvmx_fpa_wart_ctl_s cn56xx;
	struct cvmx_fpa_wart_ctl_s cn56xxp1;
	struct cvmx_fpa_wart_ctl_s cn58xx;
	struct cvmx_fpa_wart_ctl_s cn58xxp1;
};

union cvmx_fpa_wart_status {
	uint64_t u64;
	struct cvmx_fpa_wart_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t status:32;
#else
		uint64_t status:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_fpa_wart_status_s cn30xx;
	struct cvmx_fpa_wart_status_s cn31xx;
	struct cvmx_fpa_wart_status_s cn38xx;
	struct cvmx_fpa_wart_status_s cn38xxp2;
	struct cvmx_fpa_wart_status_s cn50xx;
	struct cvmx_fpa_wart_status_s cn52xx;
	struct cvmx_fpa_wart_status_s cn52xxp1;
	struct cvmx_fpa_wart_status_s cn56xx;
	struct cvmx_fpa_wart_status_s cn56xxp1;
	struct cvmx_fpa_wart_status_s cn58xx;
	struct cvmx_fpa_wart_status_s cn58xxp1;
};

union cvmx_fpa_wqe_threshold {
	uint64_t u64;
	struct cvmx_fpa_wqe_threshold_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t thresh:32;
#else
		uint64_t thresh:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_fpa_wqe_threshold_s cn61xx;
	struct cvmx_fpa_wqe_threshold_s cn63xx;
	struct cvmx_fpa_wqe_threshold_s cn66xx;
	struct cvmx_fpa_wqe_threshold_s cn68xx;
	struct cvmx_fpa_wqe_threshold_s cn68xxp1;
	struct cvmx_fpa_wqe_threshold_s cnf71xx;
};

#endif
