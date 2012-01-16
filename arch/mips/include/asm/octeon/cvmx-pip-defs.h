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

#ifndef __CVMX_PIP_DEFS_H__
#define __CVMX_PIP_DEFS_H__

/*
 * Enumeration representing the amount of packet processing
 * and validation performed by the input hardware.
 */
enum cvmx_pip_port_parse_mode {
	/*
	 * Packet input doesn't perform any processing of the input
	 * packet.
	 */
	CVMX_PIP_PORT_CFG_MODE_NONE = 0ull,
	/*
	 * Full packet processing is performed with pointer starting
	 * at the L2 (ethernet MAC) header.
	 */
	CVMX_PIP_PORT_CFG_MODE_SKIPL2 = 1ull,
	/*
	 * Input packets are assumed to be IP.  Results from non IP
	 * packets is undefined. Pointers reference the beginning of
	 * the IP header.
	 */
	CVMX_PIP_PORT_CFG_MODE_SKIPIP = 2ull
};

#define CVMX_PIP_BCK_PRS \
	 CVMX_ADD_IO_SEG(0x00011800A0000038ull)
#define CVMX_PIP_BIST_STATUS \
	 CVMX_ADD_IO_SEG(0x00011800A0000000ull)
#define CVMX_PIP_CRC_CTLX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000040ull + (((offset) & 1) * 8))
#define CVMX_PIP_CRC_IVX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000050ull + (((offset) & 1) * 8))
#define CVMX_PIP_DEC_IPSECX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000080ull + (((offset) & 3) * 8))
#define CVMX_PIP_DSA_SRC_GRP \
	 CVMX_ADD_IO_SEG(0x00011800A0000190ull)
#define CVMX_PIP_DSA_VID_GRP \
	 CVMX_ADD_IO_SEG(0x00011800A0000198ull)
#define CVMX_PIP_FRM_LEN_CHKX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000180ull + (((offset) & 1) * 8))
#define CVMX_PIP_GBL_CFG \
	 CVMX_ADD_IO_SEG(0x00011800A0000028ull)
#define CVMX_PIP_GBL_CTL \
	 CVMX_ADD_IO_SEG(0x00011800A0000020ull)
#define CVMX_PIP_HG_PRI_QOS \
	 CVMX_ADD_IO_SEG(0x00011800A00001A0ull)
#define CVMX_PIP_INT_EN \
	 CVMX_ADD_IO_SEG(0x00011800A0000010ull)
#define CVMX_PIP_INT_REG \
	 CVMX_ADD_IO_SEG(0x00011800A0000008ull)
#define CVMX_PIP_IP_OFFSET \
	 CVMX_ADD_IO_SEG(0x00011800A0000060ull)
#define CVMX_PIP_PRT_CFGX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000200ull + (((offset) & 63) * 8))
#define CVMX_PIP_PRT_TAGX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000400ull + (((offset) & 63) * 8))
#define CVMX_PIP_QOS_DIFFX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000600ull + (((offset) & 63) * 8))
#define CVMX_PIP_QOS_VLANX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A00000C0ull + (((offset) & 7) * 8))
#define CVMX_PIP_QOS_WATCHX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000100ull + (((offset) & 7) * 8))
#define CVMX_PIP_RAW_WORD \
	 CVMX_ADD_IO_SEG(0x00011800A00000B0ull)
#define CVMX_PIP_SFT_RST \
	 CVMX_ADD_IO_SEG(0x00011800A0000030ull)
#define CVMX_PIP_STAT0_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000800ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT1_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000808ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT2_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000810ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT3_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000818ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT4_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000820ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT5_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000828ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT6_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000830ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT7_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000838ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT8_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000840ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT9_PRTX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0000848ull + (((offset) & 63) * 80))
#define CVMX_PIP_STAT_CTL \
	 CVMX_ADD_IO_SEG(0x00011800A0000018ull)
#define CVMX_PIP_STAT_INB_ERRSX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0001A10ull + (((offset) & 63) * 32))
#define CVMX_PIP_STAT_INB_OCTSX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0001A08ull + (((offset) & 63) * 32))
#define CVMX_PIP_STAT_INB_PKTSX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0001A00ull + (((offset) & 63) * 32))
#define CVMX_PIP_TAG_INCX(offset) \
	 CVMX_ADD_IO_SEG(0x00011800A0001800ull + (((offset) & 63) * 8))
#define CVMX_PIP_TAG_MASK \
	 CVMX_ADD_IO_SEG(0x00011800A0000070ull)
#define CVMX_PIP_TAG_SECRET \
	 CVMX_ADD_IO_SEG(0x00011800A0000068ull)
#define CVMX_PIP_TODO_ENTRY \
	 CVMX_ADD_IO_SEG(0x00011800A0000078ull)

union cvmx_pip_bck_prs {
	uint64_t u64;
	struct cvmx_pip_bck_prs_s {
		uint64_t bckprs:1;
		uint64_t reserved_13_62:50;
		uint64_t hiwater:5;
		uint64_t reserved_5_7:3;
		uint64_t lowater:5;
	} s;
	struct cvmx_pip_bck_prs_s cn38xx;
	struct cvmx_pip_bck_prs_s cn38xxp2;
	struct cvmx_pip_bck_prs_s cn56xx;
	struct cvmx_pip_bck_prs_s cn56xxp1;
	struct cvmx_pip_bck_prs_s cn58xx;
	struct cvmx_pip_bck_prs_s cn58xxp1;
};

union cvmx_pip_bist_status {
	uint64_t u64;
	struct cvmx_pip_bist_status_s {
		uint64_t reserved_18_63:46;
		uint64_t bist:18;
	} s;
	struct cvmx_pip_bist_status_s cn30xx;
	struct cvmx_pip_bist_status_s cn31xx;
	struct cvmx_pip_bist_status_s cn38xx;
	struct cvmx_pip_bist_status_s cn38xxp2;
	struct cvmx_pip_bist_status_cn50xx {
		uint64_t reserved_17_63:47;
		uint64_t bist:17;
	} cn50xx;
	struct cvmx_pip_bist_status_s cn52xx;
	struct cvmx_pip_bist_status_s cn52xxp1;
	struct cvmx_pip_bist_status_s cn56xx;
	struct cvmx_pip_bist_status_s cn56xxp1;
	struct cvmx_pip_bist_status_s cn58xx;
	struct cvmx_pip_bist_status_s cn58xxp1;
};

union cvmx_pip_crc_ctlx {
	uint64_t u64;
	struct cvmx_pip_crc_ctlx_s {
		uint64_t reserved_2_63:62;
		uint64_t invres:1;
		uint64_t reflect:1;
	} s;
	struct cvmx_pip_crc_ctlx_s cn38xx;
	struct cvmx_pip_crc_ctlx_s cn38xxp2;
	struct cvmx_pip_crc_ctlx_s cn58xx;
	struct cvmx_pip_crc_ctlx_s cn58xxp1;
};

union cvmx_pip_crc_ivx {
	uint64_t u64;
	struct cvmx_pip_crc_ivx_s {
		uint64_t reserved_32_63:32;
		uint64_t iv:32;
	} s;
	struct cvmx_pip_crc_ivx_s cn38xx;
	struct cvmx_pip_crc_ivx_s cn38xxp2;
	struct cvmx_pip_crc_ivx_s cn58xx;
	struct cvmx_pip_crc_ivx_s cn58xxp1;
};

union cvmx_pip_dec_ipsecx {
	uint64_t u64;
	struct cvmx_pip_dec_ipsecx_s {
		uint64_t reserved_18_63:46;
		uint64_t tcp:1;
		uint64_t udp:1;
		uint64_t dprt:16;
	} s;
	struct cvmx_pip_dec_ipsecx_s cn30xx;
	struct cvmx_pip_dec_ipsecx_s cn31xx;
	struct cvmx_pip_dec_ipsecx_s cn38xx;
	struct cvmx_pip_dec_ipsecx_s cn38xxp2;
	struct cvmx_pip_dec_ipsecx_s cn50xx;
	struct cvmx_pip_dec_ipsecx_s cn52xx;
	struct cvmx_pip_dec_ipsecx_s cn52xxp1;
	struct cvmx_pip_dec_ipsecx_s cn56xx;
	struct cvmx_pip_dec_ipsecx_s cn56xxp1;
	struct cvmx_pip_dec_ipsecx_s cn58xx;
	struct cvmx_pip_dec_ipsecx_s cn58xxp1;
};

union cvmx_pip_dsa_src_grp {
	uint64_t u64;
	struct cvmx_pip_dsa_src_grp_s {
		uint64_t map15:4;
		uint64_t map14:4;
		uint64_t map13:4;
		uint64_t map12:4;
		uint64_t map11:4;
		uint64_t map10:4;
		uint64_t map9:4;
		uint64_t map8:4;
		uint64_t map7:4;
		uint64_t map6:4;
		uint64_t map5:4;
		uint64_t map4:4;
		uint64_t map3:4;
		uint64_t map2:4;
		uint64_t map1:4;
		uint64_t map0:4;
	} s;
	struct cvmx_pip_dsa_src_grp_s cn52xx;
	struct cvmx_pip_dsa_src_grp_s cn52xxp1;
	struct cvmx_pip_dsa_src_grp_s cn56xx;
};

union cvmx_pip_dsa_vid_grp {
	uint64_t u64;
	struct cvmx_pip_dsa_vid_grp_s {
		uint64_t map15:4;
		uint64_t map14:4;
		uint64_t map13:4;
		uint64_t map12:4;
		uint64_t map11:4;
		uint64_t map10:4;
		uint64_t map9:4;
		uint64_t map8:4;
		uint64_t map7:4;
		uint64_t map6:4;
		uint64_t map5:4;
		uint64_t map4:4;
		uint64_t map3:4;
		uint64_t map2:4;
		uint64_t map1:4;
		uint64_t map0:4;
	} s;
	struct cvmx_pip_dsa_vid_grp_s cn52xx;
	struct cvmx_pip_dsa_vid_grp_s cn52xxp1;
	struct cvmx_pip_dsa_vid_grp_s cn56xx;
};

union cvmx_pip_frm_len_chkx {
	uint64_t u64;
	struct cvmx_pip_frm_len_chkx_s {
		uint64_t reserved_32_63:32;
		uint64_t maxlen:16;
		uint64_t minlen:16;
	} s;
	struct cvmx_pip_frm_len_chkx_s cn50xx;
	struct cvmx_pip_frm_len_chkx_s cn52xx;
	struct cvmx_pip_frm_len_chkx_s cn52xxp1;
	struct cvmx_pip_frm_len_chkx_s cn56xx;
	struct cvmx_pip_frm_len_chkx_s cn56xxp1;
};

union cvmx_pip_gbl_cfg {
	uint64_t u64;
	struct cvmx_pip_gbl_cfg_s {
		uint64_t reserved_19_63:45;
		uint64_t tag_syn:1;
		uint64_t ip6_udp:1;
		uint64_t max_l2:1;
		uint64_t reserved_11_15:5;
		uint64_t raw_shf:3;
		uint64_t reserved_3_7:5;
		uint64_t nip_shf:3;
	} s;
	struct cvmx_pip_gbl_cfg_s cn30xx;
	struct cvmx_pip_gbl_cfg_s cn31xx;
	struct cvmx_pip_gbl_cfg_s cn38xx;
	struct cvmx_pip_gbl_cfg_s cn38xxp2;
	struct cvmx_pip_gbl_cfg_s cn50xx;
	struct cvmx_pip_gbl_cfg_s cn52xx;
	struct cvmx_pip_gbl_cfg_s cn52xxp1;
	struct cvmx_pip_gbl_cfg_s cn56xx;
	struct cvmx_pip_gbl_cfg_s cn56xxp1;
	struct cvmx_pip_gbl_cfg_s cn58xx;
	struct cvmx_pip_gbl_cfg_s cn58xxp1;
};

union cvmx_pip_gbl_ctl {
	uint64_t u64;
	struct cvmx_pip_gbl_ctl_s {
		uint64_t reserved_27_63:37;
		uint64_t dsa_grp_tvid:1;
		uint64_t dsa_grp_scmd:1;
		uint64_t dsa_grp_sid:1;
		uint64_t reserved_21_23:3;
		uint64_t ring_en:1;
		uint64_t reserved_17_19:3;
		uint64_t ignrs:1;
		uint64_t vs_wqe:1;
		uint64_t vs_qos:1;
		uint64_t l2_mal:1;
		uint64_t tcp_flag:1;
		uint64_t l4_len:1;
		uint64_t l4_chk:1;
		uint64_t l4_prt:1;
		uint64_t l4_mal:1;
		uint64_t reserved_6_7:2;
		uint64_t ip6_eext:2;
		uint64_t ip4_opts:1;
		uint64_t ip_hop:1;
		uint64_t ip_mal:1;
		uint64_t ip_chk:1;
	} s;
	struct cvmx_pip_gbl_ctl_cn30xx {
		uint64_t reserved_17_63:47;
		uint64_t ignrs:1;
		uint64_t vs_wqe:1;
		uint64_t vs_qos:1;
		uint64_t l2_mal:1;
		uint64_t tcp_flag:1;
		uint64_t l4_len:1;
		uint64_t l4_chk:1;
		uint64_t l4_prt:1;
		uint64_t l4_mal:1;
		uint64_t reserved_6_7:2;
		uint64_t ip6_eext:2;
		uint64_t ip4_opts:1;
		uint64_t ip_hop:1;
		uint64_t ip_mal:1;
		uint64_t ip_chk:1;
	} cn30xx;
	struct cvmx_pip_gbl_ctl_cn30xx cn31xx;
	struct cvmx_pip_gbl_ctl_cn30xx cn38xx;
	struct cvmx_pip_gbl_ctl_cn30xx cn38xxp2;
	struct cvmx_pip_gbl_ctl_cn30xx cn50xx;
	struct cvmx_pip_gbl_ctl_s cn52xx;
	struct cvmx_pip_gbl_ctl_s cn52xxp1;
	struct cvmx_pip_gbl_ctl_s cn56xx;
	struct cvmx_pip_gbl_ctl_cn56xxp1 {
		uint64_t reserved_21_63:43;
		uint64_t ring_en:1;
		uint64_t reserved_17_19:3;
		uint64_t ignrs:1;
		uint64_t vs_wqe:1;
		uint64_t vs_qos:1;
		uint64_t l2_mal:1;
		uint64_t tcp_flag:1;
		uint64_t l4_len:1;
		uint64_t l4_chk:1;
		uint64_t l4_prt:1;
		uint64_t l4_mal:1;
		uint64_t reserved_6_7:2;
		uint64_t ip6_eext:2;
		uint64_t ip4_opts:1;
		uint64_t ip_hop:1;
		uint64_t ip_mal:1;
		uint64_t ip_chk:1;
	} cn56xxp1;
	struct cvmx_pip_gbl_ctl_cn30xx cn58xx;
	struct cvmx_pip_gbl_ctl_cn30xx cn58xxp1;
};

union cvmx_pip_hg_pri_qos {
	uint64_t u64;
	struct cvmx_pip_hg_pri_qos_s {
		uint64_t reserved_11_63:53;
		uint64_t qos:3;
		uint64_t reserved_6_7:2;
		uint64_t pri:6;
	} s;
	struct cvmx_pip_hg_pri_qos_s cn52xx;
	struct cvmx_pip_hg_pri_qos_s cn52xxp1;
	struct cvmx_pip_hg_pri_qos_s cn56xx;
};

union cvmx_pip_int_en {
	uint64_t u64;
	struct cvmx_pip_int_en_s {
		uint64_t reserved_13_63:51;
		uint64_t punyerr:1;
		uint64_t lenerr:1;
		uint64_t maxerr:1;
		uint64_t minerr:1;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t crcerr:1;
		uint64_t pktdrp:1;
	} s;
	struct cvmx_pip_int_en_cn30xx {
		uint64_t reserved_9_63:55;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t crcerr:1;
		uint64_t pktdrp:1;
	} cn30xx;
	struct cvmx_pip_int_en_cn30xx cn31xx;
	struct cvmx_pip_int_en_cn30xx cn38xx;
	struct cvmx_pip_int_en_cn30xx cn38xxp2;
	struct cvmx_pip_int_en_cn50xx {
		uint64_t reserved_12_63:52;
		uint64_t lenerr:1;
		uint64_t maxerr:1;
		uint64_t minerr:1;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t reserved_1_1:1;
		uint64_t pktdrp:1;
	} cn50xx;
	struct cvmx_pip_int_en_cn52xx {
		uint64_t reserved_13_63:51;
		uint64_t punyerr:1;
		uint64_t lenerr:1;
		uint64_t maxerr:1;
		uint64_t minerr:1;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t reserved_1_1:1;
		uint64_t pktdrp:1;
	} cn52xx;
	struct cvmx_pip_int_en_cn52xx cn52xxp1;
	struct cvmx_pip_int_en_s cn56xx;
	struct cvmx_pip_int_en_cn56xxp1 {
		uint64_t reserved_12_63:52;
		uint64_t lenerr:1;
		uint64_t maxerr:1;
		uint64_t minerr:1;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t crcerr:1;
		uint64_t pktdrp:1;
	} cn56xxp1;
	struct cvmx_pip_int_en_cn58xx {
		uint64_t reserved_13_63:51;
		uint64_t punyerr:1;
		uint64_t reserved_9_11:3;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t crcerr:1;
		uint64_t pktdrp:1;
	} cn58xx;
	struct cvmx_pip_int_en_cn30xx cn58xxp1;
};

union cvmx_pip_int_reg {
	uint64_t u64;
	struct cvmx_pip_int_reg_s {
		uint64_t reserved_13_63:51;
		uint64_t punyerr:1;
		uint64_t lenerr:1;
		uint64_t maxerr:1;
		uint64_t minerr:1;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t crcerr:1;
		uint64_t pktdrp:1;
	} s;
	struct cvmx_pip_int_reg_cn30xx {
		uint64_t reserved_9_63:55;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t crcerr:1;
		uint64_t pktdrp:1;
	} cn30xx;
	struct cvmx_pip_int_reg_cn30xx cn31xx;
	struct cvmx_pip_int_reg_cn30xx cn38xx;
	struct cvmx_pip_int_reg_cn30xx cn38xxp2;
	struct cvmx_pip_int_reg_cn50xx {
		uint64_t reserved_12_63:52;
		uint64_t lenerr:1;
		uint64_t maxerr:1;
		uint64_t minerr:1;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t reserved_1_1:1;
		uint64_t pktdrp:1;
	} cn50xx;
	struct cvmx_pip_int_reg_cn52xx {
		uint64_t reserved_13_63:51;
		uint64_t punyerr:1;
		uint64_t lenerr:1;
		uint64_t maxerr:1;
		uint64_t minerr:1;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t reserved_1_1:1;
		uint64_t pktdrp:1;
	} cn52xx;
	struct cvmx_pip_int_reg_cn52xx cn52xxp1;
	struct cvmx_pip_int_reg_s cn56xx;
	struct cvmx_pip_int_reg_cn56xxp1 {
		uint64_t reserved_12_63:52;
		uint64_t lenerr:1;
		uint64_t maxerr:1;
		uint64_t minerr:1;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t crcerr:1;
		uint64_t pktdrp:1;
	} cn56xxp1;
	struct cvmx_pip_int_reg_cn58xx {
		uint64_t reserved_13_63:51;
		uint64_t punyerr:1;
		uint64_t reserved_9_11:3;
		uint64_t beperr:1;
		uint64_t feperr:1;
		uint64_t todoovr:1;
		uint64_t skprunt:1;
		uint64_t badtag:1;
		uint64_t prtnxa:1;
		uint64_t bckprs:1;
		uint64_t crcerr:1;
		uint64_t pktdrp:1;
	} cn58xx;
	struct cvmx_pip_int_reg_cn30xx cn58xxp1;
};

union cvmx_pip_ip_offset {
	uint64_t u64;
	struct cvmx_pip_ip_offset_s {
		uint64_t reserved_3_63:61;
		uint64_t offset:3;
	} s;
	struct cvmx_pip_ip_offset_s cn30xx;
	struct cvmx_pip_ip_offset_s cn31xx;
	struct cvmx_pip_ip_offset_s cn38xx;
	struct cvmx_pip_ip_offset_s cn38xxp2;
	struct cvmx_pip_ip_offset_s cn50xx;
	struct cvmx_pip_ip_offset_s cn52xx;
	struct cvmx_pip_ip_offset_s cn52xxp1;
	struct cvmx_pip_ip_offset_s cn56xx;
	struct cvmx_pip_ip_offset_s cn56xxp1;
	struct cvmx_pip_ip_offset_s cn58xx;
	struct cvmx_pip_ip_offset_s cn58xxp1;
};

union cvmx_pip_prt_cfgx {
	uint64_t u64;
	struct cvmx_pip_prt_cfgx_s {
		uint64_t reserved_53_63:11;
		uint64_t pad_len:1;
		uint64_t vlan_len:1;
		uint64_t lenerr_en:1;
		uint64_t maxerr_en:1;
		uint64_t minerr_en:1;
		uint64_t grp_wat_47:4;
		uint64_t qos_wat_47:4;
		uint64_t reserved_37_39:3;
		uint64_t rawdrp:1;
		uint64_t tag_inc:2;
		uint64_t dyn_rs:1;
		uint64_t inst_hdr:1;
		uint64_t grp_wat:4;
		uint64_t hg_qos:1;
		uint64_t qos:3;
		uint64_t qos_wat:4;
		uint64_t qos_vsel:1;
		uint64_t qos_vod:1;
		uint64_t qos_diff:1;
		uint64_t qos_vlan:1;
		uint64_t reserved_13_15:3;
		uint64_t crc_en:1;
		uint64_t higig_en:1;
		uint64_t dsa_en:1;
		uint64_t mode:2;
		uint64_t reserved_7_7:1;
		uint64_t skip:7;
	} s;
	struct cvmx_pip_prt_cfgx_cn30xx {
		uint64_t reserved_37_63:27;
		uint64_t rawdrp:1;
		uint64_t tag_inc:2;
		uint64_t dyn_rs:1;
		uint64_t inst_hdr:1;
		uint64_t grp_wat:4;
		uint64_t reserved_27_27:1;
		uint64_t qos:3;
		uint64_t qos_wat:4;
		uint64_t reserved_18_19:2;
		uint64_t qos_diff:1;
		uint64_t qos_vlan:1;
		uint64_t reserved_10_15:6;
		uint64_t mode:2;
		uint64_t reserved_7_7:1;
		uint64_t skip:7;
	} cn30xx;
	struct cvmx_pip_prt_cfgx_cn30xx cn31xx;
	struct cvmx_pip_prt_cfgx_cn38xx {
		uint64_t reserved_37_63:27;
		uint64_t rawdrp:1;
		uint64_t tag_inc:2;
		uint64_t dyn_rs:1;
		uint64_t inst_hdr:1;
		uint64_t grp_wat:4;
		uint64_t reserved_27_27:1;
		uint64_t qos:3;
		uint64_t qos_wat:4;
		uint64_t reserved_18_19:2;
		uint64_t qos_diff:1;
		uint64_t qos_vlan:1;
		uint64_t reserved_13_15:3;
		uint64_t crc_en:1;
		uint64_t reserved_10_11:2;
		uint64_t mode:2;
		uint64_t reserved_7_7:1;
		uint64_t skip:7;
	} cn38xx;
	struct cvmx_pip_prt_cfgx_cn38xx cn38xxp2;
	struct cvmx_pip_prt_cfgx_cn50xx {
		uint64_t reserved_53_63:11;
		uint64_t pad_len:1;
		uint64_t vlan_len:1;
		uint64_t lenerr_en:1;
		uint64_t maxerr_en:1;
		uint64_t minerr_en:1;
		uint64_t grp_wat_47:4;
		uint64_t qos_wat_47:4;
		uint64_t reserved_37_39:3;
		uint64_t rawdrp:1;
		uint64_t tag_inc:2;
		uint64_t dyn_rs:1;
		uint64_t inst_hdr:1;
		uint64_t grp_wat:4;
		uint64_t reserved_27_27:1;
		uint64_t qos:3;
		uint64_t qos_wat:4;
		uint64_t reserved_19_19:1;
		uint64_t qos_vod:1;
		uint64_t qos_diff:1;
		uint64_t qos_vlan:1;
		uint64_t reserved_13_15:3;
		uint64_t crc_en:1;
		uint64_t reserved_10_11:2;
		uint64_t mode:2;
		uint64_t reserved_7_7:1;
		uint64_t skip:7;
	} cn50xx;
	struct cvmx_pip_prt_cfgx_s cn52xx;
	struct cvmx_pip_prt_cfgx_s cn52xxp1;
	struct cvmx_pip_prt_cfgx_s cn56xx;
	struct cvmx_pip_prt_cfgx_cn50xx cn56xxp1;
	struct cvmx_pip_prt_cfgx_cn58xx {
		uint64_t reserved_37_63:27;
		uint64_t rawdrp:1;
		uint64_t tag_inc:2;
		uint64_t dyn_rs:1;
		uint64_t inst_hdr:1;
		uint64_t grp_wat:4;
		uint64_t reserved_27_27:1;
		uint64_t qos:3;
		uint64_t qos_wat:4;
		uint64_t reserved_19_19:1;
		uint64_t qos_vod:1;
		uint64_t qos_diff:1;
		uint64_t qos_vlan:1;
		uint64_t reserved_13_15:3;
		uint64_t crc_en:1;
		uint64_t reserved_10_11:2;
		uint64_t mode:2;
		uint64_t reserved_7_7:1;
		uint64_t skip:7;
	} cn58xx;
	struct cvmx_pip_prt_cfgx_cn58xx cn58xxp1;
};

union cvmx_pip_prt_tagx {
	uint64_t u64;
	struct cvmx_pip_prt_tagx_s {
		uint64_t reserved_40_63:24;
		uint64_t grptagbase:4;
		uint64_t grptagmask:4;
		uint64_t grptag:1;
		uint64_t grptag_mskip:1;
		uint64_t tag_mode:2;
		uint64_t inc_vs:2;
		uint64_t inc_vlan:1;
		uint64_t inc_prt_flag:1;
		uint64_t ip6_dprt_flag:1;
		uint64_t ip4_dprt_flag:1;
		uint64_t ip6_sprt_flag:1;
		uint64_t ip4_sprt_flag:1;
		uint64_t ip6_nxth_flag:1;
		uint64_t ip4_pctl_flag:1;
		uint64_t ip6_dst_flag:1;
		uint64_t ip4_dst_flag:1;
		uint64_t ip6_src_flag:1;
		uint64_t ip4_src_flag:1;
		uint64_t tcp6_tag_type:2;
		uint64_t tcp4_tag_type:2;
		uint64_t ip6_tag_type:2;
		uint64_t ip4_tag_type:2;
		uint64_t non_tag_type:2;
		uint64_t grp:4;
	} s;
	struct cvmx_pip_prt_tagx_cn30xx {
		uint64_t reserved_40_63:24;
		uint64_t grptagbase:4;
		uint64_t grptagmask:4;
		uint64_t grptag:1;
		uint64_t reserved_30_30:1;
		uint64_t tag_mode:2;
		uint64_t inc_vs:2;
		uint64_t inc_vlan:1;
		uint64_t inc_prt_flag:1;
		uint64_t ip6_dprt_flag:1;
		uint64_t ip4_dprt_flag:1;
		uint64_t ip6_sprt_flag:1;
		uint64_t ip4_sprt_flag:1;
		uint64_t ip6_nxth_flag:1;
		uint64_t ip4_pctl_flag:1;
		uint64_t ip6_dst_flag:1;
		uint64_t ip4_dst_flag:1;
		uint64_t ip6_src_flag:1;
		uint64_t ip4_src_flag:1;
		uint64_t tcp6_tag_type:2;
		uint64_t tcp4_tag_type:2;
		uint64_t ip6_tag_type:2;
		uint64_t ip4_tag_type:2;
		uint64_t non_tag_type:2;
		uint64_t grp:4;
	} cn30xx;
	struct cvmx_pip_prt_tagx_cn30xx cn31xx;
	struct cvmx_pip_prt_tagx_cn30xx cn38xx;
	struct cvmx_pip_prt_tagx_cn30xx cn38xxp2;
	struct cvmx_pip_prt_tagx_s cn50xx;
	struct cvmx_pip_prt_tagx_s cn52xx;
	struct cvmx_pip_prt_tagx_s cn52xxp1;
	struct cvmx_pip_prt_tagx_s cn56xx;
	struct cvmx_pip_prt_tagx_s cn56xxp1;
	struct cvmx_pip_prt_tagx_cn30xx cn58xx;
	struct cvmx_pip_prt_tagx_cn30xx cn58xxp1;
};

union cvmx_pip_qos_diffx {
	uint64_t u64;
	struct cvmx_pip_qos_diffx_s {
		uint64_t reserved_3_63:61;
		uint64_t qos:3;
	} s;
	struct cvmx_pip_qos_diffx_s cn30xx;
	struct cvmx_pip_qos_diffx_s cn31xx;
	struct cvmx_pip_qos_diffx_s cn38xx;
	struct cvmx_pip_qos_diffx_s cn38xxp2;
	struct cvmx_pip_qos_diffx_s cn50xx;
	struct cvmx_pip_qos_diffx_s cn52xx;
	struct cvmx_pip_qos_diffx_s cn52xxp1;
	struct cvmx_pip_qos_diffx_s cn56xx;
	struct cvmx_pip_qos_diffx_s cn56xxp1;
	struct cvmx_pip_qos_diffx_s cn58xx;
	struct cvmx_pip_qos_diffx_s cn58xxp1;
};

union cvmx_pip_qos_vlanx {
	uint64_t u64;
	struct cvmx_pip_qos_vlanx_s {
		uint64_t reserved_7_63:57;
		uint64_t qos1:3;
		uint64_t reserved_3_3:1;
		uint64_t qos:3;
	} s;
	struct cvmx_pip_qos_vlanx_cn30xx {
		uint64_t reserved_3_63:61;
		uint64_t qos:3;
	} cn30xx;
	struct cvmx_pip_qos_vlanx_cn30xx cn31xx;
	struct cvmx_pip_qos_vlanx_cn30xx cn38xx;
	struct cvmx_pip_qos_vlanx_cn30xx cn38xxp2;
	struct cvmx_pip_qos_vlanx_cn30xx cn50xx;
	struct cvmx_pip_qos_vlanx_s cn52xx;
	struct cvmx_pip_qos_vlanx_s cn52xxp1;
	struct cvmx_pip_qos_vlanx_s cn56xx;
	struct cvmx_pip_qos_vlanx_cn30xx cn56xxp1;
	struct cvmx_pip_qos_vlanx_cn30xx cn58xx;
	struct cvmx_pip_qos_vlanx_cn30xx cn58xxp1;
};

union cvmx_pip_qos_watchx {
	uint64_t u64;
	struct cvmx_pip_qos_watchx_s {
		uint64_t reserved_48_63:16;
		uint64_t mask:16;
		uint64_t reserved_28_31:4;
		uint64_t grp:4;
		uint64_t reserved_23_23:1;
		uint64_t qos:3;
		uint64_t reserved_19_19:1;
		uint64_t match_type:3;
		uint64_t match_value:16;
	} s;
	struct cvmx_pip_qos_watchx_cn30xx {
		uint64_t reserved_48_63:16;
		uint64_t mask:16;
		uint64_t reserved_28_31:4;
		uint64_t grp:4;
		uint64_t reserved_23_23:1;
		uint64_t qos:3;
		uint64_t reserved_18_19:2;
		uint64_t match_type:2;
		uint64_t match_value:16;
	} cn30xx;
	struct cvmx_pip_qos_watchx_cn30xx cn31xx;
	struct cvmx_pip_qos_watchx_cn30xx cn38xx;
	struct cvmx_pip_qos_watchx_cn30xx cn38xxp2;
	struct cvmx_pip_qos_watchx_s cn50xx;
	struct cvmx_pip_qos_watchx_s cn52xx;
	struct cvmx_pip_qos_watchx_s cn52xxp1;
	struct cvmx_pip_qos_watchx_s cn56xx;
	struct cvmx_pip_qos_watchx_s cn56xxp1;
	struct cvmx_pip_qos_watchx_cn30xx cn58xx;
	struct cvmx_pip_qos_watchx_cn30xx cn58xxp1;
};

union cvmx_pip_raw_word {
	uint64_t u64;
	struct cvmx_pip_raw_word_s {
		uint64_t reserved_56_63:8;
		uint64_t word:56;
	} s;
	struct cvmx_pip_raw_word_s cn30xx;
	struct cvmx_pip_raw_word_s cn31xx;
	struct cvmx_pip_raw_word_s cn38xx;
	struct cvmx_pip_raw_word_s cn38xxp2;
	struct cvmx_pip_raw_word_s cn50xx;
	struct cvmx_pip_raw_word_s cn52xx;
	struct cvmx_pip_raw_word_s cn52xxp1;
	struct cvmx_pip_raw_word_s cn56xx;
	struct cvmx_pip_raw_word_s cn56xxp1;
	struct cvmx_pip_raw_word_s cn58xx;
	struct cvmx_pip_raw_word_s cn58xxp1;
};

union cvmx_pip_sft_rst {
	uint64_t u64;
	struct cvmx_pip_sft_rst_s {
		uint64_t reserved_1_63:63;
		uint64_t rst:1;
	} s;
	struct cvmx_pip_sft_rst_s cn30xx;
	struct cvmx_pip_sft_rst_s cn31xx;
	struct cvmx_pip_sft_rst_s cn38xx;
	struct cvmx_pip_sft_rst_s cn50xx;
	struct cvmx_pip_sft_rst_s cn52xx;
	struct cvmx_pip_sft_rst_s cn52xxp1;
	struct cvmx_pip_sft_rst_s cn56xx;
	struct cvmx_pip_sft_rst_s cn56xxp1;
	struct cvmx_pip_sft_rst_s cn58xx;
	struct cvmx_pip_sft_rst_s cn58xxp1;
};

union cvmx_pip_stat0_prtx {
	uint64_t u64;
	struct cvmx_pip_stat0_prtx_s {
		uint64_t drp_pkts:32;
		uint64_t drp_octs:32;
	} s;
	struct cvmx_pip_stat0_prtx_s cn30xx;
	struct cvmx_pip_stat0_prtx_s cn31xx;
	struct cvmx_pip_stat0_prtx_s cn38xx;
	struct cvmx_pip_stat0_prtx_s cn38xxp2;
	struct cvmx_pip_stat0_prtx_s cn50xx;
	struct cvmx_pip_stat0_prtx_s cn52xx;
	struct cvmx_pip_stat0_prtx_s cn52xxp1;
	struct cvmx_pip_stat0_prtx_s cn56xx;
	struct cvmx_pip_stat0_prtx_s cn56xxp1;
	struct cvmx_pip_stat0_prtx_s cn58xx;
	struct cvmx_pip_stat0_prtx_s cn58xxp1;
};

union cvmx_pip_stat1_prtx {
	uint64_t u64;
	struct cvmx_pip_stat1_prtx_s {
		uint64_t reserved_48_63:16;
		uint64_t octs:48;
	} s;
	struct cvmx_pip_stat1_prtx_s cn30xx;
	struct cvmx_pip_stat1_prtx_s cn31xx;
	struct cvmx_pip_stat1_prtx_s cn38xx;
	struct cvmx_pip_stat1_prtx_s cn38xxp2;
	struct cvmx_pip_stat1_prtx_s cn50xx;
	struct cvmx_pip_stat1_prtx_s cn52xx;
	struct cvmx_pip_stat1_prtx_s cn52xxp1;
	struct cvmx_pip_stat1_prtx_s cn56xx;
	struct cvmx_pip_stat1_prtx_s cn56xxp1;
	struct cvmx_pip_stat1_prtx_s cn58xx;
	struct cvmx_pip_stat1_prtx_s cn58xxp1;
};

union cvmx_pip_stat2_prtx {
	uint64_t u64;
	struct cvmx_pip_stat2_prtx_s {
		uint64_t pkts:32;
		uint64_t raw:32;
	} s;
	struct cvmx_pip_stat2_prtx_s cn30xx;
	struct cvmx_pip_stat2_prtx_s cn31xx;
	struct cvmx_pip_stat2_prtx_s cn38xx;
	struct cvmx_pip_stat2_prtx_s cn38xxp2;
	struct cvmx_pip_stat2_prtx_s cn50xx;
	struct cvmx_pip_stat2_prtx_s cn52xx;
	struct cvmx_pip_stat2_prtx_s cn52xxp1;
	struct cvmx_pip_stat2_prtx_s cn56xx;
	struct cvmx_pip_stat2_prtx_s cn56xxp1;
	struct cvmx_pip_stat2_prtx_s cn58xx;
	struct cvmx_pip_stat2_prtx_s cn58xxp1;
};

union cvmx_pip_stat3_prtx {
	uint64_t u64;
	struct cvmx_pip_stat3_prtx_s {
		uint64_t bcst:32;
		uint64_t mcst:32;
	} s;
	struct cvmx_pip_stat3_prtx_s cn30xx;
	struct cvmx_pip_stat3_prtx_s cn31xx;
	struct cvmx_pip_stat3_prtx_s cn38xx;
	struct cvmx_pip_stat3_prtx_s cn38xxp2;
	struct cvmx_pip_stat3_prtx_s cn50xx;
	struct cvmx_pip_stat3_prtx_s cn52xx;
	struct cvmx_pip_stat3_prtx_s cn52xxp1;
	struct cvmx_pip_stat3_prtx_s cn56xx;
	struct cvmx_pip_stat3_prtx_s cn56xxp1;
	struct cvmx_pip_stat3_prtx_s cn58xx;
	struct cvmx_pip_stat3_prtx_s cn58xxp1;
};

union cvmx_pip_stat4_prtx {
	uint64_t u64;
	struct cvmx_pip_stat4_prtx_s {
		uint64_t h65to127:32;
		uint64_t h64:32;
	} s;
	struct cvmx_pip_stat4_prtx_s cn30xx;
	struct cvmx_pip_stat4_prtx_s cn31xx;
	struct cvmx_pip_stat4_prtx_s cn38xx;
	struct cvmx_pip_stat4_prtx_s cn38xxp2;
	struct cvmx_pip_stat4_prtx_s cn50xx;
	struct cvmx_pip_stat4_prtx_s cn52xx;
	struct cvmx_pip_stat4_prtx_s cn52xxp1;
	struct cvmx_pip_stat4_prtx_s cn56xx;
	struct cvmx_pip_stat4_prtx_s cn56xxp1;
	struct cvmx_pip_stat4_prtx_s cn58xx;
	struct cvmx_pip_stat4_prtx_s cn58xxp1;
};

union cvmx_pip_stat5_prtx {
	uint64_t u64;
	struct cvmx_pip_stat5_prtx_s {
		uint64_t h256to511:32;
		uint64_t h128to255:32;
	} s;
	struct cvmx_pip_stat5_prtx_s cn30xx;
	struct cvmx_pip_stat5_prtx_s cn31xx;
	struct cvmx_pip_stat5_prtx_s cn38xx;
	struct cvmx_pip_stat5_prtx_s cn38xxp2;
	struct cvmx_pip_stat5_prtx_s cn50xx;
	struct cvmx_pip_stat5_prtx_s cn52xx;
	struct cvmx_pip_stat5_prtx_s cn52xxp1;
	struct cvmx_pip_stat5_prtx_s cn56xx;
	struct cvmx_pip_stat5_prtx_s cn56xxp1;
	struct cvmx_pip_stat5_prtx_s cn58xx;
	struct cvmx_pip_stat5_prtx_s cn58xxp1;
};

union cvmx_pip_stat6_prtx {
	uint64_t u64;
	struct cvmx_pip_stat6_prtx_s {
		uint64_t h1024to1518:32;
		uint64_t h512to1023:32;
	} s;
	struct cvmx_pip_stat6_prtx_s cn30xx;
	struct cvmx_pip_stat6_prtx_s cn31xx;
	struct cvmx_pip_stat6_prtx_s cn38xx;
	struct cvmx_pip_stat6_prtx_s cn38xxp2;
	struct cvmx_pip_stat6_prtx_s cn50xx;
	struct cvmx_pip_stat6_prtx_s cn52xx;
	struct cvmx_pip_stat6_prtx_s cn52xxp1;
	struct cvmx_pip_stat6_prtx_s cn56xx;
	struct cvmx_pip_stat6_prtx_s cn56xxp1;
	struct cvmx_pip_stat6_prtx_s cn58xx;
	struct cvmx_pip_stat6_prtx_s cn58xxp1;
};

union cvmx_pip_stat7_prtx {
	uint64_t u64;
	struct cvmx_pip_stat7_prtx_s {
		uint64_t fcs:32;
		uint64_t h1519:32;
	} s;
	struct cvmx_pip_stat7_prtx_s cn30xx;
	struct cvmx_pip_stat7_prtx_s cn31xx;
	struct cvmx_pip_stat7_prtx_s cn38xx;
	struct cvmx_pip_stat7_prtx_s cn38xxp2;
	struct cvmx_pip_stat7_prtx_s cn50xx;
	struct cvmx_pip_stat7_prtx_s cn52xx;
	struct cvmx_pip_stat7_prtx_s cn52xxp1;
	struct cvmx_pip_stat7_prtx_s cn56xx;
	struct cvmx_pip_stat7_prtx_s cn56xxp1;
	struct cvmx_pip_stat7_prtx_s cn58xx;
	struct cvmx_pip_stat7_prtx_s cn58xxp1;
};

union cvmx_pip_stat8_prtx {
	uint64_t u64;
	struct cvmx_pip_stat8_prtx_s {
		uint64_t frag:32;
		uint64_t undersz:32;
	} s;
	struct cvmx_pip_stat8_prtx_s cn30xx;
	struct cvmx_pip_stat8_prtx_s cn31xx;
	struct cvmx_pip_stat8_prtx_s cn38xx;
	struct cvmx_pip_stat8_prtx_s cn38xxp2;
	struct cvmx_pip_stat8_prtx_s cn50xx;
	struct cvmx_pip_stat8_prtx_s cn52xx;
	struct cvmx_pip_stat8_prtx_s cn52xxp1;
	struct cvmx_pip_stat8_prtx_s cn56xx;
	struct cvmx_pip_stat8_prtx_s cn56xxp1;
	struct cvmx_pip_stat8_prtx_s cn58xx;
	struct cvmx_pip_stat8_prtx_s cn58xxp1;
};

union cvmx_pip_stat9_prtx {
	uint64_t u64;
	struct cvmx_pip_stat9_prtx_s {
		uint64_t jabber:32;
		uint64_t oversz:32;
	} s;
	struct cvmx_pip_stat9_prtx_s cn30xx;
	struct cvmx_pip_stat9_prtx_s cn31xx;
	struct cvmx_pip_stat9_prtx_s cn38xx;
	struct cvmx_pip_stat9_prtx_s cn38xxp2;
	struct cvmx_pip_stat9_prtx_s cn50xx;
	struct cvmx_pip_stat9_prtx_s cn52xx;
	struct cvmx_pip_stat9_prtx_s cn52xxp1;
	struct cvmx_pip_stat9_prtx_s cn56xx;
	struct cvmx_pip_stat9_prtx_s cn56xxp1;
	struct cvmx_pip_stat9_prtx_s cn58xx;
	struct cvmx_pip_stat9_prtx_s cn58xxp1;
};

union cvmx_pip_stat_ctl {
	uint64_t u64;
	struct cvmx_pip_stat_ctl_s {
		uint64_t reserved_1_63:63;
		uint64_t rdclr:1;
	} s;
	struct cvmx_pip_stat_ctl_s cn30xx;
	struct cvmx_pip_stat_ctl_s cn31xx;
	struct cvmx_pip_stat_ctl_s cn38xx;
	struct cvmx_pip_stat_ctl_s cn38xxp2;
	struct cvmx_pip_stat_ctl_s cn50xx;
	struct cvmx_pip_stat_ctl_s cn52xx;
	struct cvmx_pip_stat_ctl_s cn52xxp1;
	struct cvmx_pip_stat_ctl_s cn56xx;
	struct cvmx_pip_stat_ctl_s cn56xxp1;
	struct cvmx_pip_stat_ctl_s cn58xx;
	struct cvmx_pip_stat_ctl_s cn58xxp1;
};

union cvmx_pip_stat_inb_errsx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_errsx_s {
		uint64_t reserved_16_63:48;
		uint64_t errs:16;
	} s;
	struct cvmx_pip_stat_inb_errsx_s cn30xx;
	struct cvmx_pip_stat_inb_errsx_s cn31xx;
	struct cvmx_pip_stat_inb_errsx_s cn38xx;
	struct cvmx_pip_stat_inb_errsx_s cn38xxp2;
	struct cvmx_pip_stat_inb_errsx_s cn50xx;
	struct cvmx_pip_stat_inb_errsx_s cn52xx;
	struct cvmx_pip_stat_inb_errsx_s cn52xxp1;
	struct cvmx_pip_stat_inb_errsx_s cn56xx;
	struct cvmx_pip_stat_inb_errsx_s cn56xxp1;
	struct cvmx_pip_stat_inb_errsx_s cn58xx;
	struct cvmx_pip_stat_inb_errsx_s cn58xxp1;
};

union cvmx_pip_stat_inb_octsx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_octsx_s {
		uint64_t reserved_48_63:16;
		uint64_t octs:48;
	} s;
	struct cvmx_pip_stat_inb_octsx_s cn30xx;
	struct cvmx_pip_stat_inb_octsx_s cn31xx;
	struct cvmx_pip_stat_inb_octsx_s cn38xx;
	struct cvmx_pip_stat_inb_octsx_s cn38xxp2;
	struct cvmx_pip_stat_inb_octsx_s cn50xx;
	struct cvmx_pip_stat_inb_octsx_s cn52xx;
	struct cvmx_pip_stat_inb_octsx_s cn52xxp1;
	struct cvmx_pip_stat_inb_octsx_s cn56xx;
	struct cvmx_pip_stat_inb_octsx_s cn56xxp1;
	struct cvmx_pip_stat_inb_octsx_s cn58xx;
	struct cvmx_pip_stat_inb_octsx_s cn58xxp1;
};

union cvmx_pip_stat_inb_pktsx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_pktsx_s {
		uint64_t reserved_32_63:32;
		uint64_t pkts:32;
	} s;
	struct cvmx_pip_stat_inb_pktsx_s cn30xx;
	struct cvmx_pip_stat_inb_pktsx_s cn31xx;
	struct cvmx_pip_stat_inb_pktsx_s cn38xx;
	struct cvmx_pip_stat_inb_pktsx_s cn38xxp2;
	struct cvmx_pip_stat_inb_pktsx_s cn50xx;
	struct cvmx_pip_stat_inb_pktsx_s cn52xx;
	struct cvmx_pip_stat_inb_pktsx_s cn52xxp1;
	struct cvmx_pip_stat_inb_pktsx_s cn56xx;
	struct cvmx_pip_stat_inb_pktsx_s cn56xxp1;
	struct cvmx_pip_stat_inb_pktsx_s cn58xx;
	struct cvmx_pip_stat_inb_pktsx_s cn58xxp1;
};

union cvmx_pip_tag_incx {
	uint64_t u64;
	struct cvmx_pip_tag_incx_s {
		uint64_t reserved_8_63:56;
		uint64_t en:8;
	} s;
	struct cvmx_pip_tag_incx_s cn30xx;
	struct cvmx_pip_tag_incx_s cn31xx;
	struct cvmx_pip_tag_incx_s cn38xx;
	struct cvmx_pip_tag_incx_s cn38xxp2;
	struct cvmx_pip_tag_incx_s cn50xx;
	struct cvmx_pip_tag_incx_s cn52xx;
	struct cvmx_pip_tag_incx_s cn52xxp1;
	struct cvmx_pip_tag_incx_s cn56xx;
	struct cvmx_pip_tag_incx_s cn56xxp1;
	struct cvmx_pip_tag_incx_s cn58xx;
	struct cvmx_pip_tag_incx_s cn58xxp1;
};

union cvmx_pip_tag_mask {
	uint64_t u64;
	struct cvmx_pip_tag_mask_s {
		uint64_t reserved_16_63:48;
		uint64_t mask:16;
	} s;
	struct cvmx_pip_tag_mask_s cn30xx;
	struct cvmx_pip_tag_mask_s cn31xx;
	struct cvmx_pip_tag_mask_s cn38xx;
	struct cvmx_pip_tag_mask_s cn38xxp2;
	struct cvmx_pip_tag_mask_s cn50xx;
	struct cvmx_pip_tag_mask_s cn52xx;
	struct cvmx_pip_tag_mask_s cn52xxp1;
	struct cvmx_pip_tag_mask_s cn56xx;
	struct cvmx_pip_tag_mask_s cn56xxp1;
	struct cvmx_pip_tag_mask_s cn58xx;
	struct cvmx_pip_tag_mask_s cn58xxp1;
};

union cvmx_pip_tag_secret {
	uint64_t u64;
	struct cvmx_pip_tag_secret_s {
		uint64_t reserved_32_63:32;
		uint64_t dst:16;
		uint64_t src:16;
	} s;
	struct cvmx_pip_tag_secret_s cn30xx;
	struct cvmx_pip_tag_secret_s cn31xx;
	struct cvmx_pip_tag_secret_s cn38xx;
	struct cvmx_pip_tag_secret_s cn38xxp2;
	struct cvmx_pip_tag_secret_s cn50xx;
	struct cvmx_pip_tag_secret_s cn52xx;
	struct cvmx_pip_tag_secret_s cn52xxp1;
	struct cvmx_pip_tag_secret_s cn56xx;
	struct cvmx_pip_tag_secret_s cn56xxp1;
	struct cvmx_pip_tag_secret_s cn58xx;
	struct cvmx_pip_tag_secret_s cn58xxp1;
};

union cvmx_pip_todo_entry {
	uint64_t u64;
	struct cvmx_pip_todo_entry_s {
		uint64_t val:1;
		uint64_t reserved_62_62:1;
		uint64_t entry:62;
	} s;
	struct cvmx_pip_todo_entry_s cn30xx;
	struct cvmx_pip_todo_entry_s cn31xx;
	struct cvmx_pip_todo_entry_s cn38xx;
	struct cvmx_pip_todo_entry_s cn38xxp2;
	struct cvmx_pip_todo_entry_s cn50xx;
	struct cvmx_pip_todo_entry_s cn52xx;
	struct cvmx_pip_todo_entry_s cn52xxp1;
	struct cvmx_pip_todo_entry_s cn56xx;
	struct cvmx_pip_todo_entry_s cn56xxp1;
	struct cvmx_pip_todo_entry_s cn58xx;
	struct cvmx_pip_todo_entry_s cn58xxp1;
};

#endif
