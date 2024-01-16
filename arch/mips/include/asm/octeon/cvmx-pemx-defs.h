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

#ifndef __CVMX_PEMX_DEFS_H__
#define __CVMX_PEMX_DEFS_H__

#define CVMX_PEMX_BAR1_INDEXX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C00000A8ull) + (((offset) & 15) + ((block_id) & 1) * 0x200000ull) * 8)
#define CVMX_PEMX_BAR2_MASK(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000130ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_BAR_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000128ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_BIST_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000018ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_BIST_STATUS2(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000420ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_CFG_RD(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000030ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_CFG_WR(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000028ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_CPL_LUT_VALID(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000098ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_CTL_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000000ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_DBG_INFO(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000008ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_DBG_INFO_EN(block_id) (CVMX_ADD_IO_SEG(0x00011800C00000A0ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_DIAG_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000020ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_INB_READ_CREDITS(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000138ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_INT_ENB(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000410ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_INT_ENB_INT(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000418ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_INT_SUM(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000408ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_P2N_BAR0_START(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000080ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_P2N_BAR1_START(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000088ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_P2N_BAR2_START(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000090ull) + ((block_id) & 1) * 0x1000000ull)
#define CVMX_PEMX_P2P_BARX_END(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C0000048ull) + (((offset) & 3) + ((block_id) & 1) * 0x100000ull) * 16)
#define CVMX_PEMX_P2P_BARX_START(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C0000040ull) + (((offset) & 3) + ((block_id) & 1) * 0x100000ull) * 16)
#define CVMX_PEMX_TLP_CREDITS(block_id) (CVMX_ADD_IO_SEG(0x00011800C0000038ull) + ((block_id) & 1) * 0x1000000ull)

union cvmx_pemx_bar1_indexx {
	uint64_t u64;
	struct cvmx_pemx_bar1_indexx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t addr_idx:16;
		uint64_t ca:1;
		uint64_t end_swp:2;
		uint64_t addr_v:1;
#else
		uint64_t addr_v:1;
		uint64_t end_swp:2;
		uint64_t ca:1;
		uint64_t addr_idx:16;
		uint64_t reserved_20_63:44;
#endif
	} s;
};

union cvmx_pemx_bar2_mask {
	uint64_t u64;
	struct cvmx_pemx_bar2_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_38_63:26;
		uint64_t mask:35;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t mask:35;
		uint64_t reserved_38_63:26;
#endif
	} s;
};

union cvmx_pemx_bar_ctl {
	uint64_t u64;
	struct cvmx_pemx_bar_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t bar1_siz:3;
		uint64_t bar2_enb:1;
		uint64_t bar2_esx:2;
		uint64_t bar2_cax:1;
#else
		uint64_t bar2_cax:1;
		uint64_t bar2_esx:2;
		uint64_t bar2_enb:1;
		uint64_t bar1_siz:3;
		uint64_t reserved_7_63:57;
#endif
	} s;
};

union cvmx_pemx_bist_status {
	uint64_t u64;
	struct cvmx_pemx_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t retry:1;
		uint64_t rqdata0:1;
		uint64_t rqdata1:1;
		uint64_t rqdata2:1;
		uint64_t rqdata3:1;
		uint64_t rqhdr1:1;
		uint64_t rqhdr0:1;
		uint64_t sot:1;
#else
		uint64_t sot:1;
		uint64_t rqhdr0:1;
		uint64_t rqhdr1:1;
		uint64_t rqdata3:1;
		uint64_t rqdata2:1;
		uint64_t rqdata1:1;
		uint64_t rqdata0:1;
		uint64_t retry:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
};

union cvmx_pemx_bist_status2 {
	uint64_t u64;
	struct cvmx_pemx_bist_status2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t e2p_cpl:1;
		uint64_t e2p_n:1;
		uint64_t e2p_p:1;
		uint64_t peai_p2e:1;
		uint64_t pef_tpf1:1;
		uint64_t pef_tpf0:1;
		uint64_t pef_tnf:1;
		uint64_t pef_tcf1:1;
		uint64_t pef_tc0:1;
		uint64_t ppf:1;
#else
		uint64_t ppf:1;
		uint64_t pef_tc0:1;
		uint64_t pef_tcf1:1;
		uint64_t pef_tnf:1;
		uint64_t pef_tpf0:1;
		uint64_t pef_tpf1:1;
		uint64_t peai_p2e:1;
		uint64_t e2p_p:1;
		uint64_t e2p_n:1;
		uint64_t e2p_cpl:1;
		uint64_t reserved_10_63:54;
#endif
	} s;
};

union cvmx_pemx_cfg_rd {
	uint64_t u64;
	struct cvmx_pemx_cfg_rd_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:32;
		uint64_t addr:32;
#else
		uint64_t addr:32;
		uint64_t data:32;
#endif
	} s;
};

union cvmx_pemx_cfg_wr {
	uint64_t u64;
	struct cvmx_pemx_cfg_wr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:32;
		uint64_t addr:32;
#else
		uint64_t addr:32;
		uint64_t data:32;
#endif
	} s;
};

union cvmx_pemx_cpl_lut_valid {
	uint64_t u64;
	struct cvmx_pemx_cpl_lut_valid_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t tag:32;
#else
		uint64_t tag:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_pemx_ctl_status {
	uint64_t u64;
	struct cvmx_pemx_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t auto_sd:1;
		uint64_t dnum:5;
		uint64_t pbus:8;
		uint64_t reserved_32_33:2;
		uint64_t cfg_rtry:16;
		uint64_t reserved_12_15:4;
		uint64_t pm_xtoff:1;
		uint64_t pm_xpme:1;
		uint64_t ob_p_cmd:1;
		uint64_t reserved_7_8:2;
		uint64_t nf_ecrc:1;
		uint64_t dly_one:1;
		uint64_t lnk_enb:1;
		uint64_t ro_ctlp:1;
		uint64_t fast_lm:1;
		uint64_t inv_ecrc:1;
		uint64_t inv_lcrc:1;
#else
		uint64_t inv_lcrc:1;
		uint64_t inv_ecrc:1;
		uint64_t fast_lm:1;
		uint64_t ro_ctlp:1;
		uint64_t lnk_enb:1;
		uint64_t dly_one:1;
		uint64_t nf_ecrc:1;
		uint64_t reserved_7_8:2;
		uint64_t ob_p_cmd:1;
		uint64_t pm_xpme:1;
		uint64_t pm_xtoff:1;
		uint64_t reserved_12_15:4;
		uint64_t cfg_rtry:16;
		uint64_t reserved_32_33:2;
		uint64_t pbus:8;
		uint64_t dnum:5;
		uint64_t auto_sd:1;
		uint64_t reserved_48_63:16;
#endif
	} s;
};

union cvmx_pemx_dbg_info {
	uint64_t u64;
	struct cvmx_pemx_dbg_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t spoison:1;
		uint64_t rtlpmal:1;
		uint64_t rtlplle:1;
		uint64_t recrce:1;
		uint64_t rpoison:1;
		uint64_t rcemrc:1;
		uint64_t rnfemrc:1;
		uint64_t rfemrc:1;
		uint64_t rpmerc:1;
		uint64_t rptamrc:1;
		uint64_t rumep:1;
		uint64_t rvdm:1;
		uint64_t acto:1;
		uint64_t rte:1;
		uint64_t mre:1;
		uint64_t rdwdle:1;
		uint64_t rtwdle:1;
		uint64_t dpeoosd:1;
		uint64_t fcpvwt:1;
		uint64_t rpe:1;
		uint64_t fcuv:1;
		uint64_t rqo:1;
		uint64_t rauc:1;
		uint64_t racur:1;
		uint64_t racca:1;
		uint64_t caar:1;
		uint64_t rarwdns:1;
		uint64_t ramtlp:1;
		uint64_t racpp:1;
		uint64_t rawwpp:1;
		uint64_t ecrc_e:1;
		uint64_t reserved_31_63:33;
#endif
	} s;
};

union cvmx_pemx_dbg_info_en {
	uint64_t u64;
	struct cvmx_pemx_dbg_info_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t spoison:1;
		uint64_t rtlpmal:1;
		uint64_t rtlplle:1;
		uint64_t recrce:1;
		uint64_t rpoison:1;
		uint64_t rcemrc:1;
		uint64_t rnfemrc:1;
		uint64_t rfemrc:1;
		uint64_t rpmerc:1;
		uint64_t rptamrc:1;
		uint64_t rumep:1;
		uint64_t rvdm:1;
		uint64_t acto:1;
		uint64_t rte:1;
		uint64_t mre:1;
		uint64_t rdwdle:1;
		uint64_t rtwdle:1;
		uint64_t dpeoosd:1;
		uint64_t fcpvwt:1;
		uint64_t rpe:1;
		uint64_t fcuv:1;
		uint64_t rqo:1;
		uint64_t rauc:1;
		uint64_t racur:1;
		uint64_t racca:1;
		uint64_t caar:1;
		uint64_t rarwdns:1;
		uint64_t ramtlp:1;
		uint64_t racpp:1;
		uint64_t rawwpp:1;
		uint64_t ecrc_e:1;
		uint64_t reserved_31_63:33;
#endif
	} s;
};

union cvmx_pemx_diag_status {
	uint64_t u64;
	struct cvmx_pemx_diag_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t pm_dst:1;
		uint64_t pm_stat:1;
		uint64_t pm_en:1;
		uint64_t aux_en:1;
#else
		uint64_t aux_en:1;
		uint64_t pm_en:1;
		uint64_t pm_stat:1;
		uint64_t pm_dst:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

union cvmx_pemx_inb_read_credits {
	uint64_t u64;
	struct cvmx_pemx_inb_read_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t num:6;
#else
		uint64_t num:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
};

union cvmx_pemx_int_enb {
	uint64_t u64;
	struct cvmx_pemx_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t crs_dr:1;
		uint64_t crs_er:1;
		uint64_t rdlk:1;
		uint64_t exc:1;
		uint64_t un_bx:1;
		uint64_t un_b2:1;
		uint64_t un_b1:1;
		uint64_t up_bx:1;
		uint64_t up_b2:1;
		uint64_t up_b1:1;
		uint64_t pmem:1;
		uint64_t pmei:1;
		uint64_t se:1;
		uint64_t aeri:1;
#else
		uint64_t aeri:1;
		uint64_t se:1;
		uint64_t pmei:1;
		uint64_t pmem:1;
		uint64_t up_b1:1;
		uint64_t up_b2:1;
		uint64_t up_bx:1;
		uint64_t un_b1:1;
		uint64_t un_b2:1;
		uint64_t un_bx:1;
		uint64_t exc:1;
		uint64_t rdlk:1;
		uint64_t crs_er:1;
		uint64_t crs_dr:1;
		uint64_t reserved_14_63:50;
#endif
	} s;
};

union cvmx_pemx_int_enb_int {
	uint64_t u64;
	struct cvmx_pemx_int_enb_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t crs_dr:1;
		uint64_t crs_er:1;
		uint64_t rdlk:1;
		uint64_t exc:1;
		uint64_t un_bx:1;
		uint64_t un_b2:1;
		uint64_t un_b1:1;
		uint64_t up_bx:1;
		uint64_t up_b2:1;
		uint64_t up_b1:1;
		uint64_t pmem:1;
		uint64_t pmei:1;
		uint64_t se:1;
		uint64_t aeri:1;
#else
		uint64_t aeri:1;
		uint64_t se:1;
		uint64_t pmei:1;
		uint64_t pmem:1;
		uint64_t up_b1:1;
		uint64_t up_b2:1;
		uint64_t up_bx:1;
		uint64_t un_b1:1;
		uint64_t un_b2:1;
		uint64_t un_bx:1;
		uint64_t exc:1;
		uint64_t rdlk:1;
		uint64_t crs_er:1;
		uint64_t crs_dr:1;
		uint64_t reserved_14_63:50;
#endif
	} s;
};

union cvmx_pemx_int_sum {
	uint64_t u64;
	struct cvmx_pemx_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t crs_dr:1;
		uint64_t crs_er:1;
		uint64_t rdlk:1;
		uint64_t exc:1;
		uint64_t un_bx:1;
		uint64_t un_b2:1;
		uint64_t un_b1:1;
		uint64_t up_bx:1;
		uint64_t up_b2:1;
		uint64_t up_b1:1;
		uint64_t pmem:1;
		uint64_t pmei:1;
		uint64_t se:1;
		uint64_t aeri:1;
#else
		uint64_t aeri:1;
		uint64_t se:1;
		uint64_t pmei:1;
		uint64_t pmem:1;
		uint64_t up_b1:1;
		uint64_t up_b2:1;
		uint64_t up_bx:1;
		uint64_t un_b1:1;
		uint64_t un_b2:1;
		uint64_t un_bx:1;
		uint64_t exc:1;
		uint64_t rdlk:1;
		uint64_t crs_er:1;
		uint64_t crs_dr:1;
		uint64_t reserved_14_63:50;
#endif
	} s;
};

union cvmx_pemx_p2n_bar0_start {
	uint64_t u64;
	struct cvmx_pemx_p2n_bar0_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t addr:50;
		uint64_t reserved_0_13:14;
#else
		uint64_t reserved_0_13:14;
		uint64_t addr:50;
#endif
	} s;
};

union cvmx_pemx_p2n_bar1_start {
	uint64_t u64;
	struct cvmx_pemx_p2n_bar1_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t addr:38;
		uint64_t reserved_0_25:26;
#else
		uint64_t reserved_0_25:26;
		uint64_t addr:38;
#endif
	} s;
};

union cvmx_pemx_p2n_bar2_start {
	uint64_t u64;
	struct cvmx_pemx_p2n_bar2_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t addr:23;
		uint64_t reserved_0_40:41;
#else
		uint64_t reserved_0_40:41;
		uint64_t addr:23;
#endif
	} s;
};

union cvmx_pemx_p2p_barx_end {
	uint64_t u64;
	struct cvmx_pemx_p2p_barx_end_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t addr:52;
		uint64_t reserved_0_11:12;
#else
		uint64_t reserved_0_11:12;
		uint64_t addr:52;
#endif
	} s;
};

union cvmx_pemx_p2p_barx_start {
	uint64_t u64;
	struct cvmx_pemx_p2p_barx_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t addr:52;
		uint64_t reserved_0_11:12;
#else
		uint64_t reserved_0_11:12;
		uint64_t addr:52;
#endif
	} s;
};

union cvmx_pemx_tlp_credits {
	uint64_t u64;
	struct cvmx_pemx_tlp_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t peai_ppf:8;
		uint64_t pem_cpl:8;
		uint64_t pem_np:8;
		uint64_t pem_p:8;
		uint64_t sli_cpl:8;
		uint64_t sli_np:8;
		uint64_t sli_p:8;
#else
		uint64_t sli_p:8;
		uint64_t sli_np:8;
		uint64_t sli_cpl:8;
		uint64_t pem_p:8;
		uint64_t pem_np:8;
		uint64_t pem_cpl:8;
		uint64_t peai_ppf:8;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_pemx_tlp_credits_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t peai_ppf:8;
		uint64_t reserved_24_47:24;
		uint64_t sli_cpl:8;
		uint64_t sli_np:8;
		uint64_t sli_p:8;
#else
		uint64_t sli_p:8;
		uint64_t sli_np:8;
		uint64_t sli_cpl:8;
		uint64_t reserved_24_47:24;
		uint64_t peai_ppf:8;
		uint64_t reserved_56_63:8;
#endif
	} cn61xx;
};

#endif
