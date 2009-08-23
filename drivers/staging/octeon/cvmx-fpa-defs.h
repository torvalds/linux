/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
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

#define CVMX_FPA_BIST_STATUS \
	 CVMX_ADD_IO_SEG(0x00011800280000E8ull)
#define CVMX_FPA_CTL_STATUS \
	 CVMX_ADD_IO_SEG(0x0001180028000050ull)
#define CVMX_FPA_FPF0_MARKS \
	 CVMX_ADD_IO_SEG(0x0001180028000000ull)
#define CVMX_FPA_FPF0_SIZE \
	 CVMX_ADD_IO_SEG(0x0001180028000058ull)
#define CVMX_FPA_FPF1_MARKS \
	 CVMX_ADD_IO_SEG(0x0001180028000008ull)
#define CVMX_FPA_FPF2_MARKS \
	 CVMX_ADD_IO_SEG(0x0001180028000010ull)
#define CVMX_FPA_FPF3_MARKS \
	 CVMX_ADD_IO_SEG(0x0001180028000018ull)
#define CVMX_FPA_FPF4_MARKS \
	 CVMX_ADD_IO_SEG(0x0001180028000020ull)
#define CVMX_FPA_FPF5_MARKS \
	 CVMX_ADD_IO_SEG(0x0001180028000028ull)
#define CVMX_FPA_FPF6_MARKS \
	 CVMX_ADD_IO_SEG(0x0001180028000030ull)
#define CVMX_FPA_FPF7_MARKS \
	 CVMX_ADD_IO_SEG(0x0001180028000038ull)
#define CVMX_FPA_FPFX_MARKS(offset) \
	 CVMX_ADD_IO_SEG(0x0001180028000008ull + (((offset) & 7) * 8) - 8 * 1)
#define CVMX_FPA_FPFX_SIZE(offset) \
	 CVMX_ADD_IO_SEG(0x0001180028000060ull + (((offset) & 7) * 8) - 8 * 1)
#define CVMX_FPA_INT_ENB \
	 CVMX_ADD_IO_SEG(0x0001180028000048ull)
#define CVMX_FPA_INT_SUM \
	 CVMX_ADD_IO_SEG(0x0001180028000040ull)
#define CVMX_FPA_QUE0_PAGE_INDEX \
	 CVMX_ADD_IO_SEG(0x00011800280000F0ull)
#define CVMX_FPA_QUE1_PAGE_INDEX \
	 CVMX_ADD_IO_SEG(0x00011800280000F8ull)
#define CVMX_FPA_QUE2_PAGE_INDEX \
	 CVMX_ADD_IO_SEG(0x0001180028000100ull)
#define CVMX_FPA_QUE3_PAGE_INDEX \
	 CVMX_ADD_IO_SEG(0x0001180028000108ull)
#define CVMX_FPA_QUE4_PAGE_INDEX \
	 CVMX_ADD_IO_SEG(0x0001180028000110ull)
#define CVMX_FPA_QUE5_PAGE_INDEX \
	 CVMX_ADD_IO_SEG(0x0001180028000118ull)
#define CVMX_FPA_QUE6_PAGE_INDEX \
	 CVMX_ADD_IO_SEG(0x0001180028000120ull)
#define CVMX_FPA_QUE7_PAGE_INDEX \
	 CVMX_ADD_IO_SEG(0x0001180028000128ull)
#define CVMX_FPA_QUEX_AVAILABLE(offset) \
	 CVMX_ADD_IO_SEG(0x0001180028000098ull + (((offset) & 7) * 8))
#define CVMX_FPA_QUEX_PAGE_INDEX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800280000F0ull + (((offset) & 7) * 8))
#define CVMX_FPA_QUE_ACT \
	 CVMX_ADD_IO_SEG(0x0001180028000138ull)
#define CVMX_FPA_QUE_EXP \
	 CVMX_ADD_IO_SEG(0x0001180028000130ull)
#define CVMX_FPA_WART_CTL \
	 CVMX_ADD_IO_SEG(0x00011800280000D8ull)
#define CVMX_FPA_WART_STATUS \
	 CVMX_ADD_IO_SEG(0x00011800280000E0ull)

union cvmx_fpa_bist_status {
	uint64_t u64;
	struct cvmx_fpa_bist_status_s {
		uint64_t reserved_5_63:59;
		uint64_t frd:1;
		uint64_t fpf0:1;
		uint64_t fpf1:1;
		uint64_t ffr:1;
		uint64_t fdr:1;
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
};

union cvmx_fpa_ctl_status {
	uint64_t u64;
	struct cvmx_fpa_ctl_status_s {
		uint64_t reserved_18_63:46;
		uint64_t reset:1;
		uint64_t use_ldt:1;
		uint64_t use_stt:1;
		uint64_t enb:1;
		uint64_t mem1_err:7;
		uint64_t mem0_err:7;
	} s;
	struct cvmx_fpa_ctl_status_s cn30xx;
	struct cvmx_fpa_ctl_status_s cn31xx;
	struct cvmx_fpa_ctl_status_s cn38xx;
	struct cvmx_fpa_ctl_status_s cn38xxp2;
	struct cvmx_fpa_ctl_status_s cn50xx;
	struct cvmx_fpa_ctl_status_s cn52xx;
	struct cvmx_fpa_ctl_status_s cn52xxp1;
	struct cvmx_fpa_ctl_status_s cn56xx;
	struct cvmx_fpa_ctl_status_s cn56xxp1;
	struct cvmx_fpa_ctl_status_s cn58xx;
	struct cvmx_fpa_ctl_status_s cn58xxp1;
};

union cvmx_fpa_fpfx_marks {
	uint64_t u64;
	struct cvmx_fpa_fpfx_marks_s {
		uint64_t reserved_22_63:42;
		uint64_t fpf_wr:11;
		uint64_t fpf_rd:11;
	} s;
	struct cvmx_fpa_fpfx_marks_s cn38xx;
	struct cvmx_fpa_fpfx_marks_s cn38xxp2;
	struct cvmx_fpa_fpfx_marks_s cn56xx;
	struct cvmx_fpa_fpfx_marks_s cn56xxp1;
	struct cvmx_fpa_fpfx_marks_s cn58xx;
	struct cvmx_fpa_fpfx_marks_s cn58xxp1;
};

union cvmx_fpa_fpfx_size {
	uint64_t u64;
	struct cvmx_fpa_fpfx_size_s {
		uint64_t reserved_11_63:53;
		uint64_t fpf_siz:11;
	} s;
	struct cvmx_fpa_fpfx_size_s cn38xx;
	struct cvmx_fpa_fpfx_size_s cn38xxp2;
	struct cvmx_fpa_fpfx_size_s cn56xx;
	struct cvmx_fpa_fpfx_size_s cn56xxp1;
	struct cvmx_fpa_fpfx_size_s cn58xx;
	struct cvmx_fpa_fpfx_size_s cn58xxp1;
};

union cvmx_fpa_fpf0_marks {
	uint64_t u64;
	struct cvmx_fpa_fpf0_marks_s {
		uint64_t reserved_24_63:40;
		uint64_t fpf_wr:12;
		uint64_t fpf_rd:12;
	} s;
	struct cvmx_fpa_fpf0_marks_s cn38xx;
	struct cvmx_fpa_fpf0_marks_s cn38xxp2;
	struct cvmx_fpa_fpf0_marks_s cn56xx;
	struct cvmx_fpa_fpf0_marks_s cn56xxp1;
	struct cvmx_fpa_fpf0_marks_s cn58xx;
	struct cvmx_fpa_fpf0_marks_s cn58xxp1;
};

union cvmx_fpa_fpf0_size {
	uint64_t u64;
	struct cvmx_fpa_fpf0_size_s {
		uint64_t reserved_12_63:52;
		uint64_t fpf_siz:12;
	} s;
	struct cvmx_fpa_fpf0_size_s cn38xx;
	struct cvmx_fpa_fpf0_size_s cn38xxp2;
	struct cvmx_fpa_fpf0_size_s cn56xx;
	struct cvmx_fpa_fpf0_size_s cn56xxp1;
	struct cvmx_fpa_fpf0_size_s cn58xx;
	struct cvmx_fpa_fpf0_size_s cn58xxp1;
};

union cvmx_fpa_int_enb {
	uint64_t u64;
	struct cvmx_fpa_int_enb_s {
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
	} s;
	struct cvmx_fpa_int_enb_s cn30xx;
	struct cvmx_fpa_int_enb_s cn31xx;
	struct cvmx_fpa_int_enb_s cn38xx;
	struct cvmx_fpa_int_enb_s cn38xxp2;
	struct cvmx_fpa_int_enb_s cn50xx;
	struct cvmx_fpa_int_enb_s cn52xx;
	struct cvmx_fpa_int_enb_s cn52xxp1;
	struct cvmx_fpa_int_enb_s cn56xx;
	struct cvmx_fpa_int_enb_s cn56xxp1;
	struct cvmx_fpa_int_enb_s cn58xx;
	struct cvmx_fpa_int_enb_s cn58xxp1;
};

union cvmx_fpa_int_sum {
	uint64_t u64;
	struct cvmx_fpa_int_sum_s {
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
	} s;
	struct cvmx_fpa_int_sum_s cn30xx;
	struct cvmx_fpa_int_sum_s cn31xx;
	struct cvmx_fpa_int_sum_s cn38xx;
	struct cvmx_fpa_int_sum_s cn38xxp2;
	struct cvmx_fpa_int_sum_s cn50xx;
	struct cvmx_fpa_int_sum_s cn52xx;
	struct cvmx_fpa_int_sum_s cn52xxp1;
	struct cvmx_fpa_int_sum_s cn56xx;
	struct cvmx_fpa_int_sum_s cn56xxp1;
	struct cvmx_fpa_int_sum_s cn58xx;
	struct cvmx_fpa_int_sum_s cn58xxp1;
};

union cvmx_fpa_quex_available {
	uint64_t u64;
	struct cvmx_fpa_quex_available_s {
		uint64_t reserved_29_63:35;
		uint64_t que_siz:29;
	} s;
	struct cvmx_fpa_quex_available_s cn30xx;
	struct cvmx_fpa_quex_available_s cn31xx;
	struct cvmx_fpa_quex_available_s cn38xx;
	struct cvmx_fpa_quex_available_s cn38xxp2;
	struct cvmx_fpa_quex_available_s cn50xx;
	struct cvmx_fpa_quex_available_s cn52xx;
	struct cvmx_fpa_quex_available_s cn52xxp1;
	struct cvmx_fpa_quex_available_s cn56xx;
	struct cvmx_fpa_quex_available_s cn56xxp1;
	struct cvmx_fpa_quex_available_s cn58xx;
	struct cvmx_fpa_quex_available_s cn58xxp1;
};

union cvmx_fpa_quex_page_index {
	uint64_t u64;
	struct cvmx_fpa_quex_page_index_s {
		uint64_t reserved_25_63:39;
		uint64_t pg_num:25;
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
};

union cvmx_fpa_que_act {
	uint64_t u64;
	struct cvmx_fpa_que_act_s {
		uint64_t reserved_29_63:35;
		uint64_t act_que:3;
		uint64_t act_indx:26;
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
};

union cvmx_fpa_que_exp {
	uint64_t u64;
	struct cvmx_fpa_que_exp_s {
		uint64_t reserved_29_63:35;
		uint64_t exp_que:3;
		uint64_t exp_indx:26;
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
};

union cvmx_fpa_wart_ctl {
	uint64_t u64;
	struct cvmx_fpa_wart_ctl_s {
		uint64_t reserved_16_63:48;
		uint64_t ctl:16;
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
		uint64_t reserved_32_63:32;
		uint64_t status:32;
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

#endif
