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

#ifndef __CVMX_IOB_DEFS_H__
#define __CVMX_IOB_DEFS_H__

#define CVMX_IOB_BIST_STATUS (CVMX_ADD_IO_SEG(0x00011800F00007F8ull))
#define CVMX_IOB_CTL_STATUS (CVMX_ADD_IO_SEG(0x00011800F0000050ull))
#define CVMX_IOB_DWB_PRI_CNT (CVMX_ADD_IO_SEG(0x00011800F0000028ull))
#define CVMX_IOB_FAU_TIMEOUT (CVMX_ADD_IO_SEG(0x00011800F0000000ull))
#define CVMX_IOB_I2C_PRI_CNT (CVMX_ADD_IO_SEG(0x00011800F0000010ull))
#define CVMX_IOB_INB_CONTROL_MATCH (CVMX_ADD_IO_SEG(0x00011800F0000078ull))
#define CVMX_IOB_INB_CONTROL_MATCH_ENB (CVMX_ADD_IO_SEG(0x00011800F0000088ull))
#define CVMX_IOB_INB_DATA_MATCH (CVMX_ADD_IO_SEG(0x00011800F0000070ull))
#define CVMX_IOB_INB_DATA_MATCH_ENB (CVMX_ADD_IO_SEG(0x00011800F0000080ull))
#define CVMX_IOB_INT_ENB (CVMX_ADD_IO_SEG(0x00011800F0000060ull))
#define CVMX_IOB_INT_SUM (CVMX_ADD_IO_SEG(0x00011800F0000058ull))
#define CVMX_IOB_N2C_L2C_PRI_CNT (CVMX_ADD_IO_SEG(0x00011800F0000020ull))
#define CVMX_IOB_N2C_RSP_PRI_CNT (CVMX_ADD_IO_SEG(0x00011800F0000008ull))
#define CVMX_IOB_OUTB_COM_PRI_CNT (CVMX_ADD_IO_SEG(0x00011800F0000040ull))
#define CVMX_IOB_OUTB_CONTROL_MATCH (CVMX_ADD_IO_SEG(0x00011800F0000098ull))
#define CVMX_IOB_OUTB_CONTROL_MATCH_ENB (CVMX_ADD_IO_SEG(0x00011800F00000A8ull))
#define CVMX_IOB_OUTB_DATA_MATCH (CVMX_ADD_IO_SEG(0x00011800F0000090ull))
#define CVMX_IOB_OUTB_DATA_MATCH_ENB (CVMX_ADD_IO_SEG(0x00011800F00000A0ull))
#define CVMX_IOB_OUTB_FPA_PRI_CNT (CVMX_ADD_IO_SEG(0x00011800F0000048ull))
#define CVMX_IOB_OUTB_REQ_PRI_CNT (CVMX_ADD_IO_SEG(0x00011800F0000038ull))
#define CVMX_IOB_P2C_REQ_PRI_CNT (CVMX_ADD_IO_SEG(0x00011800F0000018ull))
#define CVMX_IOB_PKT_ERR (CVMX_ADD_IO_SEG(0x00011800F0000068ull))
#define CVMX_IOB_TO_CMB_CREDITS (CVMX_ADD_IO_SEG(0x00011800F00000B0ull))
#define CVMX_IOB_TO_NCB_DID_00_CREDITS (CVMX_ADD_IO_SEG(0x00011800F0000800ull))
#define CVMX_IOB_TO_NCB_DID_111_CREDITS (CVMX_ADD_IO_SEG(0x00011800F0000B78ull))
#define CVMX_IOB_TO_NCB_DID_223_CREDITS (CVMX_ADD_IO_SEG(0x00011800F0000EF8ull))
#define CVMX_IOB_TO_NCB_DID_24_CREDITS (CVMX_ADD_IO_SEG(0x00011800F00008C0ull))
#define CVMX_IOB_TO_NCB_DID_32_CREDITS (CVMX_ADD_IO_SEG(0x00011800F0000900ull))
#define CVMX_IOB_TO_NCB_DID_40_CREDITS (CVMX_ADD_IO_SEG(0x00011800F0000940ull))
#define CVMX_IOB_TO_NCB_DID_55_CREDITS (CVMX_ADD_IO_SEG(0x00011800F00009B8ull))
#define CVMX_IOB_TO_NCB_DID_64_CREDITS (CVMX_ADD_IO_SEG(0x00011800F0000A00ull))
#define CVMX_IOB_TO_NCB_DID_79_CREDITS (CVMX_ADD_IO_SEG(0x00011800F0000A78ull))
#define CVMX_IOB_TO_NCB_DID_96_CREDITS (CVMX_ADD_IO_SEG(0x00011800F0000B00ull))
#define CVMX_IOB_TO_NCB_DID_98_CREDITS (CVMX_ADD_IO_SEG(0x00011800F0000B10ull))

union cvmx_iob_bist_status {
	uint64_t u64;
	struct cvmx_iob_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t ibd:1;
		uint64_t icd:1;
#else
		uint64_t icd:1;
		uint64_t ibd:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_iob_bist_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t icnrcb:1;
		uint64_t icr0:1;
		uint64_t icr1:1;
		uint64_t icnr1:1;
		uint64_t icnr0:1;
		uint64_t ibdr0:1;
		uint64_t ibdr1:1;
		uint64_t ibr0:1;
		uint64_t ibr1:1;
		uint64_t icnrt:1;
		uint64_t ibrq0:1;
		uint64_t ibrq1:1;
		uint64_t icrn0:1;
		uint64_t icrn1:1;
		uint64_t icrp0:1;
		uint64_t icrp1:1;
		uint64_t ibd:1;
		uint64_t icd:1;
#else
		uint64_t icd:1;
		uint64_t ibd:1;
		uint64_t icrp1:1;
		uint64_t icrp0:1;
		uint64_t icrn1:1;
		uint64_t icrn0:1;
		uint64_t ibrq1:1;
		uint64_t ibrq0:1;
		uint64_t icnrt:1;
		uint64_t ibr1:1;
		uint64_t ibr0:1;
		uint64_t ibdr1:1;
		uint64_t ibdr0:1;
		uint64_t icnr0:1;
		uint64_t icnr1:1;
		uint64_t icr1:1;
		uint64_t icr0:1;
		uint64_t icnrcb:1;
		uint64_t reserved_18_63:46;
#endif
	} cn30xx;
	struct cvmx_iob_bist_status_cn30xx cn31xx;
	struct cvmx_iob_bist_status_cn30xx cn38xx;
	struct cvmx_iob_bist_status_cn30xx cn38xxp2;
	struct cvmx_iob_bist_status_cn30xx cn50xx;
	struct cvmx_iob_bist_status_cn30xx cn52xx;
	struct cvmx_iob_bist_status_cn30xx cn52xxp1;
	struct cvmx_iob_bist_status_cn30xx cn56xx;
	struct cvmx_iob_bist_status_cn30xx cn56xxp1;
	struct cvmx_iob_bist_status_cn30xx cn58xx;
	struct cvmx_iob_bist_status_cn30xx cn58xxp1;
	struct cvmx_iob_bist_status_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t xmdfif:1;
		uint64_t xmcfif:1;
		uint64_t iorfif:1;
		uint64_t rsdfif:1;
		uint64_t iocfif:1;
		uint64_t icnrcb:1;
		uint64_t icr0:1;
		uint64_t icr1:1;
		uint64_t icnr1:1;
		uint64_t icnr0:1;
		uint64_t ibdr0:1;
		uint64_t ibdr1:1;
		uint64_t ibr0:1;
		uint64_t ibr1:1;
		uint64_t icnrt:1;
		uint64_t ibrq0:1;
		uint64_t ibrq1:1;
		uint64_t icrn0:1;
		uint64_t icrn1:1;
		uint64_t icrp0:1;
		uint64_t icrp1:1;
		uint64_t ibd:1;
		uint64_t icd:1;
#else
		uint64_t icd:1;
		uint64_t ibd:1;
		uint64_t icrp1:1;
		uint64_t icrp0:1;
		uint64_t icrn1:1;
		uint64_t icrn0:1;
		uint64_t ibrq1:1;
		uint64_t ibrq0:1;
		uint64_t icnrt:1;
		uint64_t ibr1:1;
		uint64_t ibr0:1;
		uint64_t ibdr1:1;
		uint64_t ibdr0:1;
		uint64_t icnr0:1;
		uint64_t icnr1:1;
		uint64_t icr1:1;
		uint64_t icr0:1;
		uint64_t icnrcb:1;
		uint64_t iocfif:1;
		uint64_t rsdfif:1;
		uint64_t iorfif:1;
		uint64_t xmcfif:1;
		uint64_t xmdfif:1;
		uint64_t reserved_23_63:41;
#endif
	} cn61xx;
	struct cvmx_iob_bist_status_cn61xx cn63xx;
	struct cvmx_iob_bist_status_cn61xx cn63xxp1;
	struct cvmx_iob_bist_status_cn61xx cn66xx;
	struct cvmx_iob_bist_status_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t xmdfif:1;
		uint64_t xmcfif:1;
		uint64_t iorfif:1;
		uint64_t rsdfif:1;
		uint64_t iocfif:1;
		uint64_t icnrcb:1;
		uint64_t icr0:1;
		uint64_t icr1:1;
		uint64_t icnr0:1;
		uint64_t ibr0:1;
		uint64_t ibr1:1;
		uint64_t icnrt:1;
		uint64_t ibrq0:1;
		uint64_t ibrq1:1;
		uint64_t icrn0:1;
		uint64_t icrn1:1;
		uint64_t ibd:1;
		uint64_t icd:1;
#else
		uint64_t icd:1;
		uint64_t ibd:1;
		uint64_t icrn1:1;
		uint64_t icrn0:1;
		uint64_t ibrq1:1;
		uint64_t ibrq0:1;
		uint64_t icnrt:1;
		uint64_t ibr1:1;
		uint64_t ibr0:1;
		uint64_t icnr0:1;
		uint64_t icr1:1;
		uint64_t icr0:1;
		uint64_t icnrcb:1;
		uint64_t iocfif:1;
		uint64_t rsdfif:1;
		uint64_t iorfif:1;
		uint64_t xmcfif:1;
		uint64_t xmdfif:1;
		uint64_t reserved_18_63:46;
#endif
	} cn68xx;
	struct cvmx_iob_bist_status_cn68xx cn68xxp1;
	struct cvmx_iob_bist_status_cn61xx cnf71xx;
};

union cvmx_iob_ctl_status {
	uint64_t u64;
	struct cvmx_iob_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t fif_dly:1;
		uint64_t xmc_per:4;
		uint64_t reserved_5_5:1;
		uint64_t outb_mat:1;
		uint64_t inb_mat:1;
		uint64_t pko_enb:1;
		uint64_t dwb_enb:1;
		uint64_t fau_end:1;
#else
		uint64_t fau_end:1;
		uint64_t dwb_enb:1;
		uint64_t pko_enb:1;
		uint64_t inb_mat:1;
		uint64_t outb_mat:1;
		uint64_t reserved_5_5:1;
		uint64_t xmc_per:4;
		uint64_t fif_dly:1;
		uint64_t reserved_11_63:53;
#endif
	} s;
	struct cvmx_iob_ctl_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t outb_mat:1;
		uint64_t inb_mat:1;
		uint64_t pko_enb:1;
		uint64_t dwb_enb:1;
		uint64_t fau_end:1;
#else
		uint64_t fau_end:1;
		uint64_t dwb_enb:1;
		uint64_t pko_enb:1;
		uint64_t inb_mat:1;
		uint64_t outb_mat:1;
		uint64_t reserved_5_63:59;
#endif
	} cn30xx;
	struct cvmx_iob_ctl_status_cn30xx cn31xx;
	struct cvmx_iob_ctl_status_cn30xx cn38xx;
	struct cvmx_iob_ctl_status_cn30xx cn38xxp2;
	struct cvmx_iob_ctl_status_cn30xx cn50xx;
	struct cvmx_iob_ctl_status_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t rr_mode:1;
		uint64_t outb_mat:1;
		uint64_t inb_mat:1;
		uint64_t pko_enb:1;
		uint64_t dwb_enb:1;
		uint64_t fau_end:1;
#else
		uint64_t fau_end:1;
		uint64_t dwb_enb:1;
		uint64_t pko_enb:1;
		uint64_t inb_mat:1;
		uint64_t outb_mat:1;
		uint64_t rr_mode:1;
		uint64_t reserved_6_63:58;
#endif
	} cn52xx;
	struct cvmx_iob_ctl_status_cn30xx cn52xxp1;
	struct cvmx_iob_ctl_status_cn30xx cn56xx;
	struct cvmx_iob_ctl_status_cn30xx cn56xxp1;
	struct cvmx_iob_ctl_status_cn30xx cn58xx;
	struct cvmx_iob_ctl_status_cn30xx cn58xxp1;
	struct cvmx_iob_ctl_status_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t fif_dly:1;
		uint64_t xmc_per:4;
		uint64_t rr_mode:1;
		uint64_t outb_mat:1;
		uint64_t inb_mat:1;
		uint64_t pko_enb:1;
		uint64_t dwb_enb:1;
		uint64_t fau_end:1;
#else
		uint64_t fau_end:1;
		uint64_t dwb_enb:1;
		uint64_t pko_enb:1;
		uint64_t inb_mat:1;
		uint64_t outb_mat:1;
		uint64_t rr_mode:1;
		uint64_t xmc_per:4;
		uint64_t fif_dly:1;
		uint64_t reserved_11_63:53;
#endif
	} cn61xx;
	struct cvmx_iob_ctl_status_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t xmc_per:4;
		uint64_t rr_mode:1;
		uint64_t outb_mat:1;
		uint64_t inb_mat:1;
		uint64_t pko_enb:1;
		uint64_t dwb_enb:1;
		uint64_t fau_end:1;
#else
		uint64_t fau_end:1;
		uint64_t dwb_enb:1;
		uint64_t pko_enb:1;
		uint64_t inb_mat:1;
		uint64_t outb_mat:1;
		uint64_t rr_mode:1;
		uint64_t xmc_per:4;
		uint64_t reserved_10_63:54;
#endif
	} cn63xx;
	struct cvmx_iob_ctl_status_cn63xx cn63xxp1;
	struct cvmx_iob_ctl_status_cn61xx cn66xx;
	struct cvmx_iob_ctl_status_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t fif_dly:1;
		uint64_t xmc_per:4;
		uint64_t rsvr5:1;
		uint64_t outb_mat:1;
		uint64_t inb_mat:1;
		uint64_t pko_enb:1;
		uint64_t dwb_enb:1;
		uint64_t fau_end:1;
#else
		uint64_t fau_end:1;
		uint64_t dwb_enb:1;
		uint64_t pko_enb:1;
		uint64_t inb_mat:1;
		uint64_t outb_mat:1;
		uint64_t rsvr5:1;
		uint64_t xmc_per:4;
		uint64_t fif_dly:1;
		uint64_t reserved_11_63:53;
#endif
	} cn68xx;
	struct cvmx_iob_ctl_status_cn68xx cn68xxp1;
	struct cvmx_iob_ctl_status_cn61xx cnf71xx;
};

union cvmx_iob_dwb_pri_cnt {
	uint64_t u64;
	struct cvmx_iob_dwb_pri_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt_enb:1;
		uint64_t cnt_val:15;
#else
		uint64_t cnt_val:15;
		uint64_t cnt_enb:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_iob_dwb_pri_cnt_s cn38xx;
	struct cvmx_iob_dwb_pri_cnt_s cn38xxp2;
	struct cvmx_iob_dwb_pri_cnt_s cn52xx;
	struct cvmx_iob_dwb_pri_cnt_s cn52xxp1;
	struct cvmx_iob_dwb_pri_cnt_s cn56xx;
	struct cvmx_iob_dwb_pri_cnt_s cn56xxp1;
	struct cvmx_iob_dwb_pri_cnt_s cn58xx;
	struct cvmx_iob_dwb_pri_cnt_s cn58xxp1;
	struct cvmx_iob_dwb_pri_cnt_s cn61xx;
	struct cvmx_iob_dwb_pri_cnt_s cn63xx;
	struct cvmx_iob_dwb_pri_cnt_s cn63xxp1;
	struct cvmx_iob_dwb_pri_cnt_s cn66xx;
	struct cvmx_iob_dwb_pri_cnt_s cnf71xx;
};

union cvmx_iob_fau_timeout {
	uint64_t u64;
	struct cvmx_iob_fau_timeout_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t tout_enb:1;
		uint64_t tout_val:12;
#else
		uint64_t tout_val:12;
		uint64_t tout_enb:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_iob_fau_timeout_s cn30xx;
	struct cvmx_iob_fau_timeout_s cn31xx;
	struct cvmx_iob_fau_timeout_s cn38xx;
	struct cvmx_iob_fau_timeout_s cn38xxp2;
	struct cvmx_iob_fau_timeout_s cn50xx;
	struct cvmx_iob_fau_timeout_s cn52xx;
	struct cvmx_iob_fau_timeout_s cn52xxp1;
	struct cvmx_iob_fau_timeout_s cn56xx;
	struct cvmx_iob_fau_timeout_s cn56xxp1;
	struct cvmx_iob_fau_timeout_s cn58xx;
	struct cvmx_iob_fau_timeout_s cn58xxp1;
	struct cvmx_iob_fau_timeout_s cn61xx;
	struct cvmx_iob_fau_timeout_s cn63xx;
	struct cvmx_iob_fau_timeout_s cn63xxp1;
	struct cvmx_iob_fau_timeout_s cn66xx;
	struct cvmx_iob_fau_timeout_s cn68xx;
	struct cvmx_iob_fau_timeout_s cn68xxp1;
	struct cvmx_iob_fau_timeout_s cnf71xx;
};

union cvmx_iob_i2c_pri_cnt {
	uint64_t u64;
	struct cvmx_iob_i2c_pri_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt_enb:1;
		uint64_t cnt_val:15;
#else
		uint64_t cnt_val:15;
		uint64_t cnt_enb:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_iob_i2c_pri_cnt_s cn38xx;
	struct cvmx_iob_i2c_pri_cnt_s cn38xxp2;
	struct cvmx_iob_i2c_pri_cnt_s cn52xx;
	struct cvmx_iob_i2c_pri_cnt_s cn52xxp1;
	struct cvmx_iob_i2c_pri_cnt_s cn56xx;
	struct cvmx_iob_i2c_pri_cnt_s cn56xxp1;
	struct cvmx_iob_i2c_pri_cnt_s cn58xx;
	struct cvmx_iob_i2c_pri_cnt_s cn58xxp1;
	struct cvmx_iob_i2c_pri_cnt_s cn61xx;
	struct cvmx_iob_i2c_pri_cnt_s cn63xx;
	struct cvmx_iob_i2c_pri_cnt_s cn63xxp1;
	struct cvmx_iob_i2c_pri_cnt_s cn66xx;
	struct cvmx_iob_i2c_pri_cnt_s cnf71xx;
};

union cvmx_iob_inb_control_match {
	uint64_t u64;
	struct cvmx_iob_inb_control_match_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t mask:8;
		uint64_t opc:4;
		uint64_t dst:9;
		uint64_t src:8;
#else
		uint64_t src:8;
		uint64_t dst:9;
		uint64_t opc:4;
		uint64_t mask:8;
		uint64_t reserved_29_63:35;
#endif
	} s;
	struct cvmx_iob_inb_control_match_s cn30xx;
	struct cvmx_iob_inb_control_match_s cn31xx;
	struct cvmx_iob_inb_control_match_s cn38xx;
	struct cvmx_iob_inb_control_match_s cn38xxp2;
	struct cvmx_iob_inb_control_match_s cn50xx;
	struct cvmx_iob_inb_control_match_s cn52xx;
	struct cvmx_iob_inb_control_match_s cn52xxp1;
	struct cvmx_iob_inb_control_match_s cn56xx;
	struct cvmx_iob_inb_control_match_s cn56xxp1;
	struct cvmx_iob_inb_control_match_s cn58xx;
	struct cvmx_iob_inb_control_match_s cn58xxp1;
	struct cvmx_iob_inb_control_match_s cn61xx;
	struct cvmx_iob_inb_control_match_s cn63xx;
	struct cvmx_iob_inb_control_match_s cn63xxp1;
	struct cvmx_iob_inb_control_match_s cn66xx;
	struct cvmx_iob_inb_control_match_s cn68xx;
	struct cvmx_iob_inb_control_match_s cn68xxp1;
	struct cvmx_iob_inb_control_match_s cnf71xx;
};

union cvmx_iob_inb_control_match_enb {
	uint64_t u64;
	struct cvmx_iob_inb_control_match_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t mask:8;
		uint64_t opc:4;
		uint64_t dst:9;
		uint64_t src:8;
#else
		uint64_t src:8;
		uint64_t dst:9;
		uint64_t opc:4;
		uint64_t mask:8;
		uint64_t reserved_29_63:35;
#endif
	} s;
	struct cvmx_iob_inb_control_match_enb_s cn30xx;
	struct cvmx_iob_inb_control_match_enb_s cn31xx;
	struct cvmx_iob_inb_control_match_enb_s cn38xx;
	struct cvmx_iob_inb_control_match_enb_s cn38xxp2;
	struct cvmx_iob_inb_control_match_enb_s cn50xx;
	struct cvmx_iob_inb_control_match_enb_s cn52xx;
	struct cvmx_iob_inb_control_match_enb_s cn52xxp1;
	struct cvmx_iob_inb_control_match_enb_s cn56xx;
	struct cvmx_iob_inb_control_match_enb_s cn56xxp1;
	struct cvmx_iob_inb_control_match_enb_s cn58xx;
	struct cvmx_iob_inb_control_match_enb_s cn58xxp1;
	struct cvmx_iob_inb_control_match_enb_s cn61xx;
	struct cvmx_iob_inb_control_match_enb_s cn63xx;
	struct cvmx_iob_inb_control_match_enb_s cn63xxp1;
	struct cvmx_iob_inb_control_match_enb_s cn66xx;
	struct cvmx_iob_inb_control_match_enb_s cn68xx;
	struct cvmx_iob_inb_control_match_enb_s cn68xxp1;
	struct cvmx_iob_inb_control_match_enb_s cnf71xx;
};

union cvmx_iob_inb_data_match {
	uint64_t u64;
	struct cvmx_iob_inb_data_match_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_iob_inb_data_match_s cn30xx;
	struct cvmx_iob_inb_data_match_s cn31xx;
	struct cvmx_iob_inb_data_match_s cn38xx;
	struct cvmx_iob_inb_data_match_s cn38xxp2;
	struct cvmx_iob_inb_data_match_s cn50xx;
	struct cvmx_iob_inb_data_match_s cn52xx;
	struct cvmx_iob_inb_data_match_s cn52xxp1;
	struct cvmx_iob_inb_data_match_s cn56xx;
	struct cvmx_iob_inb_data_match_s cn56xxp1;
	struct cvmx_iob_inb_data_match_s cn58xx;
	struct cvmx_iob_inb_data_match_s cn58xxp1;
	struct cvmx_iob_inb_data_match_s cn61xx;
	struct cvmx_iob_inb_data_match_s cn63xx;
	struct cvmx_iob_inb_data_match_s cn63xxp1;
	struct cvmx_iob_inb_data_match_s cn66xx;
	struct cvmx_iob_inb_data_match_s cn68xx;
	struct cvmx_iob_inb_data_match_s cn68xxp1;
	struct cvmx_iob_inb_data_match_s cnf71xx;
};

union cvmx_iob_inb_data_match_enb {
	uint64_t u64;
	struct cvmx_iob_inb_data_match_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_iob_inb_data_match_enb_s cn30xx;
	struct cvmx_iob_inb_data_match_enb_s cn31xx;
	struct cvmx_iob_inb_data_match_enb_s cn38xx;
	struct cvmx_iob_inb_data_match_enb_s cn38xxp2;
	struct cvmx_iob_inb_data_match_enb_s cn50xx;
	struct cvmx_iob_inb_data_match_enb_s cn52xx;
	struct cvmx_iob_inb_data_match_enb_s cn52xxp1;
	struct cvmx_iob_inb_data_match_enb_s cn56xx;
	struct cvmx_iob_inb_data_match_enb_s cn56xxp1;
	struct cvmx_iob_inb_data_match_enb_s cn58xx;
	struct cvmx_iob_inb_data_match_enb_s cn58xxp1;
	struct cvmx_iob_inb_data_match_enb_s cn61xx;
	struct cvmx_iob_inb_data_match_enb_s cn63xx;
	struct cvmx_iob_inb_data_match_enb_s cn63xxp1;
	struct cvmx_iob_inb_data_match_enb_s cn66xx;
	struct cvmx_iob_inb_data_match_enb_s cn68xx;
	struct cvmx_iob_inb_data_match_enb_s cn68xxp1;
	struct cvmx_iob_inb_data_match_enb_s cnf71xx;
};

union cvmx_iob_int_enb {
	uint64_t u64;
	struct cvmx_iob_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t p_dat:1;
		uint64_t np_dat:1;
		uint64_t p_eop:1;
		uint64_t p_sop:1;
		uint64_t np_eop:1;
		uint64_t np_sop:1;
#else
		uint64_t np_sop:1;
		uint64_t np_eop:1;
		uint64_t p_sop:1;
		uint64_t p_eop:1;
		uint64_t np_dat:1;
		uint64_t p_dat:1;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_iob_int_enb_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t p_eop:1;
		uint64_t p_sop:1;
		uint64_t np_eop:1;
		uint64_t np_sop:1;
#else
		uint64_t np_sop:1;
		uint64_t np_eop:1;
		uint64_t p_sop:1;
		uint64_t p_eop:1;
		uint64_t reserved_4_63:60;
#endif
	} cn30xx;
	struct cvmx_iob_int_enb_cn30xx cn31xx;
	struct cvmx_iob_int_enb_cn30xx cn38xx;
	struct cvmx_iob_int_enb_cn30xx cn38xxp2;
	struct cvmx_iob_int_enb_s cn50xx;
	struct cvmx_iob_int_enb_s cn52xx;
	struct cvmx_iob_int_enb_s cn52xxp1;
	struct cvmx_iob_int_enb_s cn56xx;
	struct cvmx_iob_int_enb_s cn56xxp1;
	struct cvmx_iob_int_enb_s cn58xx;
	struct cvmx_iob_int_enb_s cn58xxp1;
	struct cvmx_iob_int_enb_s cn61xx;
	struct cvmx_iob_int_enb_s cn63xx;
	struct cvmx_iob_int_enb_s cn63xxp1;
	struct cvmx_iob_int_enb_s cn66xx;
	struct cvmx_iob_int_enb_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} cn68xx;
	struct cvmx_iob_int_enb_cn68xx cn68xxp1;
	struct cvmx_iob_int_enb_s cnf71xx;
};

union cvmx_iob_int_sum {
	uint64_t u64;
	struct cvmx_iob_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t p_dat:1;
		uint64_t np_dat:1;
		uint64_t p_eop:1;
		uint64_t p_sop:1;
		uint64_t np_eop:1;
		uint64_t np_sop:1;
#else
		uint64_t np_sop:1;
		uint64_t np_eop:1;
		uint64_t p_sop:1;
		uint64_t p_eop:1;
		uint64_t np_dat:1;
		uint64_t p_dat:1;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_iob_int_sum_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t p_eop:1;
		uint64_t p_sop:1;
		uint64_t np_eop:1;
		uint64_t np_sop:1;
#else
		uint64_t np_sop:1;
		uint64_t np_eop:1;
		uint64_t p_sop:1;
		uint64_t p_eop:1;
		uint64_t reserved_4_63:60;
#endif
	} cn30xx;
	struct cvmx_iob_int_sum_cn30xx cn31xx;
	struct cvmx_iob_int_sum_cn30xx cn38xx;
	struct cvmx_iob_int_sum_cn30xx cn38xxp2;
	struct cvmx_iob_int_sum_s cn50xx;
	struct cvmx_iob_int_sum_s cn52xx;
	struct cvmx_iob_int_sum_s cn52xxp1;
	struct cvmx_iob_int_sum_s cn56xx;
	struct cvmx_iob_int_sum_s cn56xxp1;
	struct cvmx_iob_int_sum_s cn58xx;
	struct cvmx_iob_int_sum_s cn58xxp1;
	struct cvmx_iob_int_sum_s cn61xx;
	struct cvmx_iob_int_sum_s cn63xx;
	struct cvmx_iob_int_sum_s cn63xxp1;
	struct cvmx_iob_int_sum_s cn66xx;
	struct cvmx_iob_int_sum_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} cn68xx;
	struct cvmx_iob_int_sum_cn68xx cn68xxp1;
	struct cvmx_iob_int_sum_s cnf71xx;
};

union cvmx_iob_n2c_l2c_pri_cnt {
	uint64_t u64;
	struct cvmx_iob_n2c_l2c_pri_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt_enb:1;
		uint64_t cnt_val:15;
#else
		uint64_t cnt_val:15;
		uint64_t cnt_enb:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn38xx;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn38xxp2;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn52xx;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn52xxp1;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn56xx;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn56xxp1;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn58xx;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn58xxp1;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn61xx;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn63xx;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn63xxp1;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cn66xx;
	struct cvmx_iob_n2c_l2c_pri_cnt_s cnf71xx;
};

union cvmx_iob_n2c_rsp_pri_cnt {
	uint64_t u64;
	struct cvmx_iob_n2c_rsp_pri_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt_enb:1;
		uint64_t cnt_val:15;
#else
		uint64_t cnt_val:15;
		uint64_t cnt_enb:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn38xx;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn38xxp2;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn52xx;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn52xxp1;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn56xx;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn56xxp1;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn58xx;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn58xxp1;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn61xx;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn63xx;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn63xxp1;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cn66xx;
	struct cvmx_iob_n2c_rsp_pri_cnt_s cnf71xx;
};

union cvmx_iob_outb_com_pri_cnt {
	uint64_t u64;
	struct cvmx_iob_outb_com_pri_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt_enb:1;
		uint64_t cnt_val:15;
#else
		uint64_t cnt_val:15;
		uint64_t cnt_enb:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_iob_outb_com_pri_cnt_s cn38xx;
	struct cvmx_iob_outb_com_pri_cnt_s cn38xxp2;
	struct cvmx_iob_outb_com_pri_cnt_s cn52xx;
	struct cvmx_iob_outb_com_pri_cnt_s cn52xxp1;
	struct cvmx_iob_outb_com_pri_cnt_s cn56xx;
	struct cvmx_iob_outb_com_pri_cnt_s cn56xxp1;
	struct cvmx_iob_outb_com_pri_cnt_s cn58xx;
	struct cvmx_iob_outb_com_pri_cnt_s cn58xxp1;
	struct cvmx_iob_outb_com_pri_cnt_s cn61xx;
	struct cvmx_iob_outb_com_pri_cnt_s cn63xx;
	struct cvmx_iob_outb_com_pri_cnt_s cn63xxp1;
	struct cvmx_iob_outb_com_pri_cnt_s cn66xx;
	struct cvmx_iob_outb_com_pri_cnt_s cn68xx;
	struct cvmx_iob_outb_com_pri_cnt_s cn68xxp1;
	struct cvmx_iob_outb_com_pri_cnt_s cnf71xx;
};

union cvmx_iob_outb_control_match {
	uint64_t u64;
	struct cvmx_iob_outb_control_match_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_26_63:38;
		uint64_t mask:8;
		uint64_t eot:1;
		uint64_t dst:8;
		uint64_t src:9;
#else
		uint64_t src:9;
		uint64_t dst:8;
		uint64_t eot:1;
		uint64_t mask:8;
		uint64_t reserved_26_63:38;
#endif
	} s;
	struct cvmx_iob_outb_control_match_s cn30xx;
	struct cvmx_iob_outb_control_match_s cn31xx;
	struct cvmx_iob_outb_control_match_s cn38xx;
	struct cvmx_iob_outb_control_match_s cn38xxp2;
	struct cvmx_iob_outb_control_match_s cn50xx;
	struct cvmx_iob_outb_control_match_s cn52xx;
	struct cvmx_iob_outb_control_match_s cn52xxp1;
	struct cvmx_iob_outb_control_match_s cn56xx;
	struct cvmx_iob_outb_control_match_s cn56xxp1;
	struct cvmx_iob_outb_control_match_s cn58xx;
	struct cvmx_iob_outb_control_match_s cn58xxp1;
	struct cvmx_iob_outb_control_match_s cn61xx;
	struct cvmx_iob_outb_control_match_s cn63xx;
	struct cvmx_iob_outb_control_match_s cn63xxp1;
	struct cvmx_iob_outb_control_match_s cn66xx;
	struct cvmx_iob_outb_control_match_s cn68xx;
	struct cvmx_iob_outb_control_match_s cn68xxp1;
	struct cvmx_iob_outb_control_match_s cnf71xx;
};

union cvmx_iob_outb_control_match_enb {
	uint64_t u64;
	struct cvmx_iob_outb_control_match_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_26_63:38;
		uint64_t mask:8;
		uint64_t eot:1;
		uint64_t dst:8;
		uint64_t src:9;
#else
		uint64_t src:9;
		uint64_t dst:8;
		uint64_t eot:1;
		uint64_t mask:8;
		uint64_t reserved_26_63:38;
#endif
	} s;
	struct cvmx_iob_outb_control_match_enb_s cn30xx;
	struct cvmx_iob_outb_control_match_enb_s cn31xx;
	struct cvmx_iob_outb_control_match_enb_s cn38xx;
	struct cvmx_iob_outb_control_match_enb_s cn38xxp2;
	struct cvmx_iob_outb_control_match_enb_s cn50xx;
	struct cvmx_iob_outb_control_match_enb_s cn52xx;
	struct cvmx_iob_outb_control_match_enb_s cn52xxp1;
	struct cvmx_iob_outb_control_match_enb_s cn56xx;
	struct cvmx_iob_outb_control_match_enb_s cn56xxp1;
	struct cvmx_iob_outb_control_match_enb_s cn58xx;
	struct cvmx_iob_outb_control_match_enb_s cn58xxp1;
	struct cvmx_iob_outb_control_match_enb_s cn61xx;
	struct cvmx_iob_outb_control_match_enb_s cn63xx;
	struct cvmx_iob_outb_control_match_enb_s cn63xxp1;
	struct cvmx_iob_outb_control_match_enb_s cn66xx;
	struct cvmx_iob_outb_control_match_enb_s cn68xx;
	struct cvmx_iob_outb_control_match_enb_s cn68xxp1;
	struct cvmx_iob_outb_control_match_enb_s cnf71xx;
};

union cvmx_iob_outb_data_match {
	uint64_t u64;
	struct cvmx_iob_outb_data_match_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_iob_outb_data_match_s cn30xx;
	struct cvmx_iob_outb_data_match_s cn31xx;
	struct cvmx_iob_outb_data_match_s cn38xx;
	struct cvmx_iob_outb_data_match_s cn38xxp2;
	struct cvmx_iob_outb_data_match_s cn50xx;
	struct cvmx_iob_outb_data_match_s cn52xx;
	struct cvmx_iob_outb_data_match_s cn52xxp1;
	struct cvmx_iob_outb_data_match_s cn56xx;
	struct cvmx_iob_outb_data_match_s cn56xxp1;
	struct cvmx_iob_outb_data_match_s cn58xx;
	struct cvmx_iob_outb_data_match_s cn58xxp1;
	struct cvmx_iob_outb_data_match_s cn61xx;
	struct cvmx_iob_outb_data_match_s cn63xx;
	struct cvmx_iob_outb_data_match_s cn63xxp1;
	struct cvmx_iob_outb_data_match_s cn66xx;
	struct cvmx_iob_outb_data_match_s cn68xx;
	struct cvmx_iob_outb_data_match_s cn68xxp1;
	struct cvmx_iob_outb_data_match_s cnf71xx;
};

union cvmx_iob_outb_data_match_enb {
	uint64_t u64;
	struct cvmx_iob_outb_data_match_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_iob_outb_data_match_enb_s cn30xx;
	struct cvmx_iob_outb_data_match_enb_s cn31xx;
	struct cvmx_iob_outb_data_match_enb_s cn38xx;
	struct cvmx_iob_outb_data_match_enb_s cn38xxp2;
	struct cvmx_iob_outb_data_match_enb_s cn50xx;
	struct cvmx_iob_outb_data_match_enb_s cn52xx;
	struct cvmx_iob_outb_data_match_enb_s cn52xxp1;
	struct cvmx_iob_outb_data_match_enb_s cn56xx;
	struct cvmx_iob_outb_data_match_enb_s cn56xxp1;
	struct cvmx_iob_outb_data_match_enb_s cn58xx;
	struct cvmx_iob_outb_data_match_enb_s cn58xxp1;
	struct cvmx_iob_outb_data_match_enb_s cn61xx;
	struct cvmx_iob_outb_data_match_enb_s cn63xx;
	struct cvmx_iob_outb_data_match_enb_s cn63xxp1;
	struct cvmx_iob_outb_data_match_enb_s cn66xx;
	struct cvmx_iob_outb_data_match_enb_s cn68xx;
	struct cvmx_iob_outb_data_match_enb_s cn68xxp1;
	struct cvmx_iob_outb_data_match_enb_s cnf71xx;
};

union cvmx_iob_outb_fpa_pri_cnt {
	uint64_t u64;
	struct cvmx_iob_outb_fpa_pri_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt_enb:1;
		uint64_t cnt_val:15;
#else
		uint64_t cnt_val:15;
		uint64_t cnt_enb:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn38xx;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn38xxp2;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn52xx;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn52xxp1;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn56xx;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn56xxp1;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn58xx;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn58xxp1;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn61xx;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn63xx;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn63xxp1;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn66xx;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn68xx;
	struct cvmx_iob_outb_fpa_pri_cnt_s cn68xxp1;
	struct cvmx_iob_outb_fpa_pri_cnt_s cnf71xx;
};

union cvmx_iob_outb_req_pri_cnt {
	uint64_t u64;
	struct cvmx_iob_outb_req_pri_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt_enb:1;
		uint64_t cnt_val:15;
#else
		uint64_t cnt_val:15;
		uint64_t cnt_enb:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_iob_outb_req_pri_cnt_s cn38xx;
	struct cvmx_iob_outb_req_pri_cnt_s cn38xxp2;
	struct cvmx_iob_outb_req_pri_cnt_s cn52xx;
	struct cvmx_iob_outb_req_pri_cnt_s cn52xxp1;
	struct cvmx_iob_outb_req_pri_cnt_s cn56xx;
	struct cvmx_iob_outb_req_pri_cnt_s cn56xxp1;
	struct cvmx_iob_outb_req_pri_cnt_s cn58xx;
	struct cvmx_iob_outb_req_pri_cnt_s cn58xxp1;
	struct cvmx_iob_outb_req_pri_cnt_s cn61xx;
	struct cvmx_iob_outb_req_pri_cnt_s cn63xx;
	struct cvmx_iob_outb_req_pri_cnt_s cn63xxp1;
	struct cvmx_iob_outb_req_pri_cnt_s cn66xx;
	struct cvmx_iob_outb_req_pri_cnt_s cn68xx;
	struct cvmx_iob_outb_req_pri_cnt_s cn68xxp1;
	struct cvmx_iob_outb_req_pri_cnt_s cnf71xx;
};

union cvmx_iob_p2c_req_pri_cnt {
	uint64_t u64;
	struct cvmx_iob_p2c_req_pri_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt_enb:1;
		uint64_t cnt_val:15;
#else
		uint64_t cnt_val:15;
		uint64_t cnt_enb:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_iob_p2c_req_pri_cnt_s cn38xx;
	struct cvmx_iob_p2c_req_pri_cnt_s cn38xxp2;
	struct cvmx_iob_p2c_req_pri_cnt_s cn52xx;
	struct cvmx_iob_p2c_req_pri_cnt_s cn52xxp1;
	struct cvmx_iob_p2c_req_pri_cnt_s cn56xx;
	struct cvmx_iob_p2c_req_pri_cnt_s cn56xxp1;
	struct cvmx_iob_p2c_req_pri_cnt_s cn58xx;
	struct cvmx_iob_p2c_req_pri_cnt_s cn58xxp1;
	struct cvmx_iob_p2c_req_pri_cnt_s cn61xx;
	struct cvmx_iob_p2c_req_pri_cnt_s cn63xx;
	struct cvmx_iob_p2c_req_pri_cnt_s cn63xxp1;
	struct cvmx_iob_p2c_req_pri_cnt_s cn66xx;
	struct cvmx_iob_p2c_req_pri_cnt_s cnf71xx;
};

union cvmx_iob_pkt_err {
	uint64_t u64;
	struct cvmx_iob_pkt_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t vport:6;
		uint64_t port:6;
#else
		uint64_t port:6;
		uint64_t vport:6;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_iob_pkt_err_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t port:6;
#else
		uint64_t port:6;
		uint64_t reserved_6_63:58;
#endif
	} cn30xx;
	struct cvmx_iob_pkt_err_cn30xx cn31xx;
	struct cvmx_iob_pkt_err_cn30xx cn38xx;
	struct cvmx_iob_pkt_err_cn30xx cn38xxp2;
	struct cvmx_iob_pkt_err_cn30xx cn50xx;
	struct cvmx_iob_pkt_err_cn30xx cn52xx;
	struct cvmx_iob_pkt_err_cn30xx cn52xxp1;
	struct cvmx_iob_pkt_err_cn30xx cn56xx;
	struct cvmx_iob_pkt_err_cn30xx cn56xxp1;
	struct cvmx_iob_pkt_err_cn30xx cn58xx;
	struct cvmx_iob_pkt_err_cn30xx cn58xxp1;
	struct cvmx_iob_pkt_err_s cn61xx;
	struct cvmx_iob_pkt_err_s cn63xx;
	struct cvmx_iob_pkt_err_s cn63xxp1;
	struct cvmx_iob_pkt_err_s cn66xx;
	struct cvmx_iob_pkt_err_s cnf71xx;
};

union cvmx_iob_to_cmb_credits {
	uint64_t u64;
	struct cvmx_iob_to_cmb_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t ncb_rd:3;
		uint64_t ncb_wr:3;
#else
		uint64_t ncb_wr:3;
		uint64_t ncb_rd:3;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_iob_to_cmb_credits_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t pko_rd:3;
		uint64_t ncb_rd:3;
		uint64_t ncb_wr:3;
#else
		uint64_t ncb_wr:3;
		uint64_t ncb_rd:3;
		uint64_t pko_rd:3;
		uint64_t reserved_9_63:55;
#endif
	} cn52xx;
	struct cvmx_iob_to_cmb_credits_cn52xx cn61xx;
	struct cvmx_iob_to_cmb_credits_cn52xx cn63xx;
	struct cvmx_iob_to_cmb_credits_cn52xx cn63xxp1;
	struct cvmx_iob_to_cmb_credits_cn52xx cn66xx;
	struct cvmx_iob_to_cmb_credits_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t dwb:3;
		uint64_t ncb_rd:3;
		uint64_t ncb_wr:3;
#else
		uint64_t ncb_wr:3;
		uint64_t ncb_rd:3;
		uint64_t dwb:3;
		uint64_t reserved_9_63:55;
#endif
	} cn68xx;
	struct cvmx_iob_to_cmb_credits_cn68xx cn68xxp1;
	struct cvmx_iob_to_cmb_credits_cn52xx cnf71xx;
};

union cvmx_iob_to_ncb_did_00_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_00_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_00_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_00_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_111_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_111_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_111_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_111_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_223_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_223_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_223_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_223_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_24_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_24_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_24_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_24_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_32_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_32_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_32_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_32_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_40_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_40_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_40_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_40_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_55_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_55_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_55_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_55_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_64_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_64_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_64_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_64_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_79_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_79_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_79_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_79_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_96_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_96_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_96_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_96_credits_s cn68xxp1;
};

union cvmx_iob_to_ncb_did_98_credits {
	uint64_t u64;
	struct cvmx_iob_to_ncb_did_98_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t crd:7;
#else
		uint64_t crd:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_iob_to_ncb_did_98_credits_s cn68xx;
	struct cvmx_iob_to_ncb_did_98_credits_s cn68xxp1;
};

#endif
