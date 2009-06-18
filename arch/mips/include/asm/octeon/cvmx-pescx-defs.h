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

#ifndef __CVMX_PESCX_DEFS_H__
#define __CVMX_PESCX_DEFS_H__

#define CVMX_PESCX_BIST_STATUS(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000018ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_BIST_STATUS2(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000418ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_CFG_RD(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000030ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_CFG_WR(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000028ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_CPL_LUT_VALID(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000098ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_CTL_STATUS(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000000ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_CTL_STATUS2(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000400ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_DBG_INFO(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000008ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_DBG_INFO_EN(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C80000A0ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_DIAG_STATUS(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000020ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_P2N_BAR0_START(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000080ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_P2N_BAR1_START(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000088ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_P2N_BAR2_START(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000090ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_P2P_BARX_END(offset, block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000048ull + (((offset) & 3) * 16) + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_P2P_BARX_START(offset, block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000040ull + (((offset) & 3) * 16) + (((block_id) & 1) * 0x8000000ull))
#define CVMX_PESCX_TLP_CREDITS(block_id) \
	 CVMX_ADD_IO_SEG(0x00011800C8000038ull + (((block_id) & 1) * 0x8000000ull))

union cvmx_pescx_bist_status {
	uint64_t u64;
	struct cvmx_pescx_bist_status_s {
		uint64_t reserved_13_63:51;
		uint64_t rqdata5:1;
		uint64_t ctlp_or:1;
		uint64_t ntlp_or:1;
		uint64_t ptlp_or:1;
		uint64_t retry:1;
		uint64_t rqdata0:1;
		uint64_t rqdata1:1;
		uint64_t rqdata2:1;
		uint64_t rqdata3:1;
		uint64_t rqdata4:1;
		uint64_t rqhdr1:1;
		uint64_t rqhdr0:1;
		uint64_t sot:1;
	} s;
	struct cvmx_pescx_bist_status_s cn52xx;
	struct cvmx_pescx_bist_status_cn52xxp1 {
		uint64_t reserved_12_63:52;
		uint64_t ctlp_or:1;
		uint64_t ntlp_or:1;
		uint64_t ptlp_or:1;
		uint64_t retry:1;
		uint64_t rqdata0:1;
		uint64_t rqdata1:1;
		uint64_t rqdata2:1;
		uint64_t rqdata3:1;
		uint64_t rqdata4:1;
		uint64_t rqhdr1:1;
		uint64_t rqhdr0:1;
		uint64_t sot:1;
	} cn52xxp1;
	struct cvmx_pescx_bist_status_s cn56xx;
	struct cvmx_pescx_bist_status_cn52xxp1 cn56xxp1;
};

union cvmx_pescx_bist_status2 {
	uint64_t u64;
	struct cvmx_pescx_bist_status2_s {
		uint64_t reserved_14_63:50;
		uint64_t cto_p2e:1;
		uint64_t e2p_cpl:1;
		uint64_t e2p_n:1;
		uint64_t e2p_p:1;
		uint64_t e2p_rsl:1;
		uint64_t dbg_p2e:1;
		uint64_t peai_p2e:1;
		uint64_t rsl_p2e:1;
		uint64_t pef_tpf1:1;
		uint64_t pef_tpf0:1;
		uint64_t pef_tnf:1;
		uint64_t pef_tcf1:1;
		uint64_t pef_tc0:1;
		uint64_t ppf:1;
	} s;
	struct cvmx_pescx_bist_status2_s cn52xx;
	struct cvmx_pescx_bist_status2_s cn52xxp1;
	struct cvmx_pescx_bist_status2_s cn56xx;
	struct cvmx_pescx_bist_status2_s cn56xxp1;
};

union cvmx_pescx_cfg_rd {
	uint64_t u64;
	struct cvmx_pescx_cfg_rd_s {
		uint64_t data:32;
		uint64_t addr:32;
	} s;
	struct cvmx_pescx_cfg_rd_s cn52xx;
	struct cvmx_pescx_cfg_rd_s cn52xxp1;
	struct cvmx_pescx_cfg_rd_s cn56xx;
	struct cvmx_pescx_cfg_rd_s cn56xxp1;
};

union cvmx_pescx_cfg_wr {
	uint64_t u64;
	struct cvmx_pescx_cfg_wr_s {
		uint64_t data:32;
		uint64_t addr:32;
	} s;
	struct cvmx_pescx_cfg_wr_s cn52xx;
	struct cvmx_pescx_cfg_wr_s cn52xxp1;
	struct cvmx_pescx_cfg_wr_s cn56xx;
	struct cvmx_pescx_cfg_wr_s cn56xxp1;
};

union cvmx_pescx_cpl_lut_valid {
	uint64_t u64;
	struct cvmx_pescx_cpl_lut_valid_s {
		uint64_t reserved_32_63:32;
		uint64_t tag:32;
	} s;
	struct cvmx_pescx_cpl_lut_valid_s cn52xx;
	struct cvmx_pescx_cpl_lut_valid_s cn52xxp1;
	struct cvmx_pescx_cpl_lut_valid_s cn56xx;
	struct cvmx_pescx_cpl_lut_valid_s cn56xxp1;
};

union cvmx_pescx_ctl_status {
	uint64_t u64;
	struct cvmx_pescx_ctl_status_s {
		uint64_t reserved_28_63:36;
		uint64_t dnum:5;
		uint64_t pbus:8;
		uint64_t qlm_cfg:2;
		uint64_t lane_swp:1;
		uint64_t pm_xtoff:1;
		uint64_t pm_xpme:1;
		uint64_t ob_p_cmd:1;
		uint64_t reserved_7_8:2;
		uint64_t nf_ecrc:1;
		uint64_t dly_one:1;
		uint64_t lnk_enb:1;
		uint64_t ro_ctlp:1;
		uint64_t reserved_2_2:1;
		uint64_t inv_ecrc:1;
		uint64_t inv_lcrc:1;
	} s;
	struct cvmx_pescx_ctl_status_s cn52xx;
	struct cvmx_pescx_ctl_status_s cn52xxp1;
	struct cvmx_pescx_ctl_status_cn56xx {
		uint64_t reserved_28_63:36;
		uint64_t dnum:5;
		uint64_t pbus:8;
		uint64_t qlm_cfg:2;
		uint64_t reserved_12_12:1;
		uint64_t pm_xtoff:1;
		uint64_t pm_xpme:1;
		uint64_t ob_p_cmd:1;
		uint64_t reserved_7_8:2;
		uint64_t nf_ecrc:1;
		uint64_t dly_one:1;
		uint64_t lnk_enb:1;
		uint64_t ro_ctlp:1;
		uint64_t reserved_2_2:1;
		uint64_t inv_ecrc:1;
		uint64_t inv_lcrc:1;
	} cn56xx;
	struct cvmx_pescx_ctl_status_cn56xx cn56xxp1;
};

union cvmx_pescx_ctl_status2 {
	uint64_t u64;
	struct cvmx_pescx_ctl_status2_s {
		uint64_t reserved_2_63:62;
		uint64_t pclk_run:1;
		uint64_t pcierst:1;
	} s;
	struct cvmx_pescx_ctl_status2_s cn52xx;
	struct cvmx_pescx_ctl_status2_cn52xxp1 {
		uint64_t reserved_1_63:63;
		uint64_t pcierst:1;
	} cn52xxp1;
	struct cvmx_pescx_ctl_status2_s cn56xx;
	struct cvmx_pescx_ctl_status2_cn52xxp1 cn56xxp1;
};

union cvmx_pescx_dbg_info {
	uint64_t u64;
	struct cvmx_pescx_dbg_info_s {
		uint64_t reserved_31_63:33;
		uint64_t ecrc_e:1;
		uint64_t rawwpp:1;
		uint64_t racpp:1;
		uint64_t ramtlp:1;
		uint64_t rarwdns:1;
		uint64_t caar:1;
		uint64_t racca:1;
		uint64_t racur:1;
		uint64_t rauc:1;
		uint64_t rqo:1;
		uint64_t fcuv:1;
		uint64_t rpe:1;
		uint64_t fcpvwt:1;
		uint64_t dpeoosd:1;
		uint64_t rtwdle:1;
		uint64_t rdwdle:1;
		uint64_t mre:1;
		uint64_t rte:1;
		uint64_t acto:1;
		uint64_t rvdm:1;
		uint64_t rumep:1;
		uint64_t rptamrc:1;
		uint64_t rpmerc:1;
		uint64_t rfemrc:1;
		uint64_t rnfemrc:1;
		uint64_t rcemrc:1;
		uint64_t rpoison:1;
		uint64_t recrce:1;
		uint64_t rtlplle:1;
		uint64_t rtlpmal:1;
		uint64_t spoison:1;
	} s;
	struct cvmx_pescx_dbg_info_s cn52xx;
	struct cvmx_pescx_dbg_info_s cn52xxp1;
	struct cvmx_pescx_dbg_info_s cn56xx;
	struct cvmx_pescx_dbg_info_s cn56xxp1;
};

union cvmx_pescx_dbg_info_en {
	uint64_t u64;
	struct cvmx_pescx_dbg_info_en_s {
		uint64_t reserved_31_63:33;
		uint64_t ecrc_e:1;
		uint64_t rawwpp:1;
		uint64_t racpp:1;
		uint64_t ramtlp:1;
		uint64_t rarwdns:1;
		uint64_t caar:1;
		uint64_t racca:1;
		uint64_t racur:1;
		uint64_t rauc:1;
		uint64_t rqo:1;
		uint64_t fcuv:1;
		uint64_t rpe:1;
		uint64_t fcpvwt:1;
		uint64_t dpeoosd:1;
		uint64_t rtwdle:1;
		uint64_t rdwdle:1;
		uint64_t mre:1;
		uint64_t rte:1;
		uint64_t acto:1;
		uint64_t rvdm:1;
		uint64_t rumep:1;
		uint64_t rptamrc:1;
		uint64_t rpmerc:1;
		uint64_t rfemrc:1;
		uint64_t rnfemrc:1;
		uint64_t rcemrc:1;
		uint64_t rpoison:1;
		uint64_t recrce:1;
		uint64_t rtlplle:1;
		uint64_t rtlpmal:1;
		uint64_t spoison:1;
	} s;
	struct cvmx_pescx_dbg_info_en_s cn52xx;
	struct cvmx_pescx_dbg_info_en_s cn52xxp1;
	struct cvmx_pescx_dbg_info_en_s cn56xx;
	struct cvmx_pescx_dbg_info_en_s cn56xxp1;
};

union cvmx_pescx_diag_status {
	uint64_t u64;
	struct cvmx_pescx_diag_status_s {
		uint64_t reserved_4_63:60;
		uint64_t pm_dst:1;
		uint64_t pm_stat:1;
		uint64_t pm_en:1;
		uint64_t aux_en:1;
	} s;
	struct cvmx_pescx_diag_status_s cn52xx;
	struct cvmx_pescx_diag_status_s cn52xxp1;
	struct cvmx_pescx_diag_status_s cn56xx;
	struct cvmx_pescx_diag_status_s cn56xxp1;
};

union cvmx_pescx_p2n_bar0_start {
	uint64_t u64;
	struct cvmx_pescx_p2n_bar0_start_s {
		uint64_t addr:50;
		uint64_t reserved_0_13:14;
	} s;
	struct cvmx_pescx_p2n_bar0_start_s cn52xx;
	struct cvmx_pescx_p2n_bar0_start_s cn52xxp1;
	struct cvmx_pescx_p2n_bar0_start_s cn56xx;
	struct cvmx_pescx_p2n_bar0_start_s cn56xxp1;
};

union cvmx_pescx_p2n_bar1_start {
	uint64_t u64;
	struct cvmx_pescx_p2n_bar1_start_s {
		uint64_t addr:38;
		uint64_t reserved_0_25:26;
	} s;
	struct cvmx_pescx_p2n_bar1_start_s cn52xx;
	struct cvmx_pescx_p2n_bar1_start_s cn52xxp1;
	struct cvmx_pescx_p2n_bar1_start_s cn56xx;
	struct cvmx_pescx_p2n_bar1_start_s cn56xxp1;
};

union cvmx_pescx_p2n_bar2_start {
	uint64_t u64;
	struct cvmx_pescx_p2n_bar2_start_s {
		uint64_t addr:25;
		uint64_t reserved_0_38:39;
	} s;
	struct cvmx_pescx_p2n_bar2_start_s cn52xx;
	struct cvmx_pescx_p2n_bar2_start_s cn52xxp1;
	struct cvmx_pescx_p2n_bar2_start_s cn56xx;
	struct cvmx_pescx_p2n_bar2_start_s cn56xxp1;
};

union cvmx_pescx_p2p_barx_end {
	uint64_t u64;
	struct cvmx_pescx_p2p_barx_end_s {
		uint64_t addr:52;
		uint64_t reserved_0_11:12;
	} s;
	struct cvmx_pescx_p2p_barx_end_s cn52xx;
	struct cvmx_pescx_p2p_barx_end_s cn52xxp1;
	struct cvmx_pescx_p2p_barx_end_s cn56xx;
	struct cvmx_pescx_p2p_barx_end_s cn56xxp1;
};

union cvmx_pescx_p2p_barx_start {
	uint64_t u64;
	struct cvmx_pescx_p2p_barx_start_s {
		uint64_t addr:52;
		uint64_t reserved_0_11:12;
	} s;
	struct cvmx_pescx_p2p_barx_start_s cn52xx;
	struct cvmx_pescx_p2p_barx_start_s cn52xxp1;
	struct cvmx_pescx_p2p_barx_start_s cn56xx;
	struct cvmx_pescx_p2p_barx_start_s cn56xxp1;
};

union cvmx_pescx_tlp_credits {
	uint64_t u64;
	struct cvmx_pescx_tlp_credits_s {
		uint64_t reserved_0_63:64;
	} s;
	struct cvmx_pescx_tlp_credits_cn52xx {
		uint64_t reserved_56_63:8;
		uint64_t peai_ppf:8;
		uint64_t pesc_cpl:8;
		uint64_t pesc_np:8;
		uint64_t pesc_p:8;
		uint64_t npei_cpl:8;
		uint64_t npei_np:8;
		uint64_t npei_p:8;
	} cn52xx;
	struct cvmx_pescx_tlp_credits_cn52xxp1 {
		uint64_t reserved_38_63:26;
		uint64_t peai_ppf:8;
		uint64_t pesc_cpl:5;
		uint64_t pesc_np:5;
		uint64_t pesc_p:5;
		uint64_t npei_cpl:5;
		uint64_t npei_np:5;
		uint64_t npei_p:5;
	} cn52xxp1;
	struct cvmx_pescx_tlp_credits_cn52xx cn56xx;
	struct cvmx_pescx_tlp_credits_cn52xxp1 cn56xxp1;
};

#endif
