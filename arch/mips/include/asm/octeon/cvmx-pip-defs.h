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

#define CVMX_PIP_ALT_SKIP_CFGX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002A00ull) + ((offset) & 3) * 8)
#define CVMX_PIP_BCK_PRS (CVMX_ADD_IO_SEG(0x00011800A0000038ull))
#define CVMX_PIP_BIST_STATUS (CVMX_ADD_IO_SEG(0x00011800A0000000ull))
#define CVMX_PIP_BSEL_EXT_CFGX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002800ull) + ((offset) & 3) * 16)
#define CVMX_PIP_BSEL_EXT_POSX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002808ull) + ((offset) & 3) * 16)
#define CVMX_PIP_BSEL_TBL_ENTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0003000ull) + ((offset) & 511) * 8)
#define CVMX_PIP_CLKEN (CVMX_ADD_IO_SEG(0x00011800A0000040ull))
#define CVMX_PIP_CRC_CTLX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000040ull) + ((offset) & 1) * 8)
#define CVMX_PIP_CRC_IVX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000050ull) + ((offset) & 1) * 8)
#define CVMX_PIP_DEC_IPSECX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000080ull) + ((offset) & 3) * 8)
#define CVMX_PIP_DSA_SRC_GRP (CVMX_ADD_IO_SEG(0x00011800A0000190ull))
#define CVMX_PIP_DSA_VID_GRP (CVMX_ADD_IO_SEG(0x00011800A0000198ull))
#define CVMX_PIP_FRM_LEN_CHKX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000180ull) + ((offset) & 1) * 8)
#define CVMX_PIP_GBL_CFG (CVMX_ADD_IO_SEG(0x00011800A0000028ull))
#define CVMX_PIP_GBL_CTL (CVMX_ADD_IO_SEG(0x00011800A0000020ull))
#define CVMX_PIP_HG_PRI_QOS (CVMX_ADD_IO_SEG(0x00011800A00001A0ull))
#define CVMX_PIP_INT_EN (CVMX_ADD_IO_SEG(0x00011800A0000010ull))
#define CVMX_PIP_INT_REG (CVMX_ADD_IO_SEG(0x00011800A0000008ull))
#define CVMX_PIP_IP_OFFSET (CVMX_ADD_IO_SEG(0x00011800A0000060ull))
#define CVMX_PIP_PRI_TBLX(offset) (CVMX_ADD_IO_SEG(0x00011800A0004000ull) + ((offset) & 255) * 8)
#define CVMX_PIP_PRT_CFGBX(offset) (CVMX_ADD_IO_SEG(0x00011800A0008000ull) + ((offset) & 63) * 8)
#define CVMX_PIP_PRT_CFGX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000200ull) + ((offset) & 63) * 8)
#define CVMX_PIP_PRT_TAGX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000400ull) + ((offset) & 63) * 8)
#define CVMX_PIP_QOS_DIFFX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000600ull) + ((offset) & 63) * 8)
#define CVMX_PIP_QOS_VLANX(offset) (CVMX_ADD_IO_SEG(0x00011800A00000C0ull) + ((offset) & 7) * 8)
#define CVMX_PIP_QOS_WATCHX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000100ull) + ((offset) & 7) * 8)
#define CVMX_PIP_RAW_WORD (CVMX_ADD_IO_SEG(0x00011800A00000B0ull))
#define CVMX_PIP_SFT_RST (CVMX_ADD_IO_SEG(0x00011800A0000030ull))
#define CVMX_PIP_STAT0_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000800ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT0_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040000ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT10_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001480ull) + ((offset) & 63) * 16)
#define CVMX_PIP_STAT10_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040050ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT11_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001488ull) + ((offset) & 63) * 16)
#define CVMX_PIP_STAT11_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040058ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT1_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000808ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT1_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040008ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT2_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000810ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT2_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040010ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT3_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000818ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT3_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040018ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT4_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000820ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT4_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040020ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT5_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000828ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT5_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040028ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT6_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000830ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT6_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040030ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT7_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000838ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT7_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040038ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT8_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000840ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT8_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040040ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT9_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000848ull) + ((offset) & 63) * 80)
#define CVMX_PIP_STAT9_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040048ull) + ((offset) & 63) * 128)
#define CVMX_PIP_STAT_CTL (CVMX_ADD_IO_SEG(0x00011800A0000018ull))
#define CVMX_PIP_STAT_INB_ERRSX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001A10ull) + ((offset) & 63) * 32)
#define CVMX_PIP_STAT_INB_ERRS_PKNDX(offset) (CVMX_ADD_IO_SEG(0x00011800A0020010ull) + ((offset) & 63) * 32)
#define CVMX_PIP_STAT_INB_OCTSX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001A08ull) + ((offset) & 63) * 32)
#define CVMX_PIP_STAT_INB_OCTS_PKNDX(offset) (CVMX_ADD_IO_SEG(0x00011800A0020008ull) + ((offset) & 63) * 32)
#define CVMX_PIP_STAT_INB_PKTSX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001A00ull) + ((offset) & 63) * 32)
#define CVMX_PIP_STAT_INB_PKTS_PKNDX(offset) (CVMX_ADD_IO_SEG(0x00011800A0020000ull) + ((offset) & 63) * 32)
#define CVMX_PIP_SUB_PKIND_FCSX(block_id) (CVMX_ADD_IO_SEG(0x00011800A0080000ull))
#define CVMX_PIP_TAG_INCX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001800ull) + ((offset) & 63) * 8)
#define CVMX_PIP_TAG_MASK (CVMX_ADD_IO_SEG(0x00011800A0000070ull))
#define CVMX_PIP_TAG_SECRET (CVMX_ADD_IO_SEG(0x00011800A0000068ull))
#define CVMX_PIP_TODO_ENTRY (CVMX_ADD_IO_SEG(0x00011800A0000078ull))
#define CVMX_PIP_VLAN_ETYPESX(offset) (CVMX_ADD_IO_SEG(0x00011800A00001C0ull) + ((offset) & 1) * 8)
#define CVMX_PIP_XSTAT0_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002000ull) + ((offset) & 63) * 80 - 80*40)
#define CVMX_PIP_XSTAT10_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001700ull) + ((offset) & 63) * 16 - 16*40)
#define CVMX_PIP_XSTAT11_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001708ull) + ((offset) & 63) * 16 - 16*40)
#define CVMX_PIP_XSTAT1_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002008ull) + ((offset) & 63) * 80 - 80*40)
#define CVMX_PIP_XSTAT2_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002010ull) + ((offset) & 63) * 80 - 80*40)
#define CVMX_PIP_XSTAT3_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002018ull) + ((offset) & 63) * 80 - 80*40)
#define CVMX_PIP_XSTAT4_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002020ull) + ((offset) & 63) * 80 - 80*40)
#define CVMX_PIP_XSTAT5_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002028ull) + ((offset) & 63) * 80 - 80*40)
#define CVMX_PIP_XSTAT6_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002030ull) + ((offset) & 63) * 80 - 80*40)
#define CVMX_PIP_XSTAT7_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002038ull) + ((offset) & 63) * 80 - 80*40)
#define CVMX_PIP_XSTAT8_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002040ull) + ((offset) & 63) * 80 - 80*40)
#define CVMX_PIP_XSTAT9_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002048ull) + ((offset) & 63) * 80 - 80*40)

union cvmx_pip_alt_skip_cfgx {
	uint64_t u64;
	struct cvmx_pip_alt_skip_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_57_63:7;
		uint64_t len:1;
		uint64_t reserved_46_55:10;
		uint64_t bit1:6;
		uint64_t reserved_38_39:2;
		uint64_t bit0:6;
		uint64_t reserved_23_31:9;
		uint64_t skip3:7;
		uint64_t reserved_15_15:1;
		uint64_t skip2:7;
		uint64_t reserved_7_7:1;
		uint64_t skip1:7;
#else
		uint64_t skip1:7;
		uint64_t reserved_7_7:1;
		uint64_t skip2:7;
		uint64_t reserved_15_15:1;
		uint64_t skip3:7;
		uint64_t reserved_23_31:9;
		uint64_t bit0:6;
		uint64_t reserved_38_39:2;
		uint64_t bit1:6;
		uint64_t reserved_46_55:10;
		uint64_t len:1;
		uint64_t reserved_57_63:7;
#endif
	} s;
	struct cvmx_pip_alt_skip_cfgx_s cn61xx;
	struct cvmx_pip_alt_skip_cfgx_s cn66xx;
	struct cvmx_pip_alt_skip_cfgx_s cn68xx;
	struct cvmx_pip_alt_skip_cfgx_s cnf71xx;
};

union cvmx_pip_bck_prs {
	uint64_t u64;
	struct cvmx_pip_bck_prs_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bckprs:1;
		uint64_t reserved_13_62:50;
		uint64_t hiwater:5;
		uint64_t reserved_5_7:3;
		uint64_t lowater:5;
#else
		uint64_t lowater:5;
		uint64_t reserved_5_7:3;
		uint64_t hiwater:5;
		uint64_t reserved_13_62:50;
		uint64_t bckprs:1;
#endif
	} s;
	struct cvmx_pip_bck_prs_s cn38xx;
	struct cvmx_pip_bck_prs_s cn38xxp2;
	struct cvmx_pip_bck_prs_s cn56xx;
	struct cvmx_pip_bck_prs_s cn56xxp1;
	struct cvmx_pip_bck_prs_s cn58xx;
	struct cvmx_pip_bck_prs_s cn58xxp1;
	struct cvmx_pip_bck_prs_s cn61xx;
	struct cvmx_pip_bck_prs_s cn63xx;
	struct cvmx_pip_bck_prs_s cn63xxp1;
	struct cvmx_pip_bck_prs_s cn66xx;
	struct cvmx_pip_bck_prs_s cn68xx;
	struct cvmx_pip_bck_prs_s cn68xxp1;
	struct cvmx_pip_bck_prs_s cnf71xx;
};

union cvmx_pip_bist_status {
	uint64_t u64;
	struct cvmx_pip_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t bist:22;
#else
		uint64_t bist:22;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_pip_bist_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t bist:18;
#else
		uint64_t bist:18;
		uint64_t reserved_18_63:46;
#endif
	} cn30xx;
	struct cvmx_pip_bist_status_cn30xx cn31xx;
	struct cvmx_pip_bist_status_cn30xx cn38xx;
	struct cvmx_pip_bist_status_cn30xx cn38xxp2;
	struct cvmx_pip_bist_status_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t bist:17;
#else
		uint64_t bist:17;
		uint64_t reserved_17_63:47;
#endif
	} cn50xx;
	struct cvmx_pip_bist_status_cn30xx cn52xx;
	struct cvmx_pip_bist_status_cn30xx cn52xxp1;
	struct cvmx_pip_bist_status_cn30xx cn56xx;
	struct cvmx_pip_bist_status_cn30xx cn56xxp1;
	struct cvmx_pip_bist_status_cn30xx cn58xx;
	struct cvmx_pip_bist_status_cn30xx cn58xxp1;
	struct cvmx_pip_bist_status_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t bist:20;
#else
		uint64_t bist:20;
		uint64_t reserved_20_63:44;
#endif
	} cn61xx;
	struct cvmx_pip_bist_status_cn30xx cn63xx;
	struct cvmx_pip_bist_status_cn30xx cn63xxp1;
	struct cvmx_pip_bist_status_cn61xx cn66xx;
	struct cvmx_pip_bist_status_s cn68xx;
	struct cvmx_pip_bist_status_cn61xx cn68xxp1;
	struct cvmx_pip_bist_status_cn61xx cnf71xx;
};

union cvmx_pip_bsel_ext_cfgx {
	uint64_t u64;
	struct cvmx_pip_bsel_ext_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t upper_tag:16;
		uint64_t tag:8;
		uint64_t reserved_25_31:7;
		uint64_t offset:9;
		uint64_t reserved_7_15:9;
		uint64_t skip:7;
#else
		uint64_t skip:7;
		uint64_t reserved_7_15:9;
		uint64_t offset:9;
		uint64_t reserved_25_31:7;
		uint64_t tag:8;
		uint64_t upper_tag:16;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_pip_bsel_ext_cfgx_s cn61xx;
	struct cvmx_pip_bsel_ext_cfgx_s cn68xx;
	struct cvmx_pip_bsel_ext_cfgx_s cnf71xx;
};

union cvmx_pip_bsel_ext_posx {
	uint64_t u64;
	struct cvmx_pip_bsel_ext_posx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t pos7_val:1;
		uint64_t pos7:7;
		uint64_t pos6_val:1;
		uint64_t pos6:7;
		uint64_t pos5_val:1;
		uint64_t pos5:7;
		uint64_t pos4_val:1;
		uint64_t pos4:7;
		uint64_t pos3_val:1;
		uint64_t pos3:7;
		uint64_t pos2_val:1;
		uint64_t pos2:7;
		uint64_t pos1_val:1;
		uint64_t pos1:7;
		uint64_t pos0_val:1;
		uint64_t pos0:7;
#else
		uint64_t pos0:7;
		uint64_t pos0_val:1;
		uint64_t pos1:7;
		uint64_t pos1_val:1;
		uint64_t pos2:7;
		uint64_t pos2_val:1;
		uint64_t pos3:7;
		uint64_t pos3_val:1;
		uint64_t pos4:7;
		uint64_t pos4_val:1;
		uint64_t pos5:7;
		uint64_t pos5_val:1;
		uint64_t pos6:7;
		uint64_t pos6_val:1;
		uint64_t pos7:7;
		uint64_t pos7_val:1;
#endif
	} s;
	struct cvmx_pip_bsel_ext_posx_s cn61xx;
	struct cvmx_pip_bsel_ext_posx_s cn68xx;
	struct cvmx_pip_bsel_ext_posx_s cnf71xx;
};

union cvmx_pip_bsel_tbl_entx {
	uint64_t u64;
	struct cvmx_pip_bsel_tbl_entx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t tag_en:1;
		uint64_t grp_en:1;
		uint64_t tt_en:1;
		uint64_t qos_en:1;
		uint64_t reserved_40_59:20;
		uint64_t tag:8;
		uint64_t reserved_22_31:10;
		uint64_t grp:6;
		uint64_t reserved_10_15:6;
		uint64_t tt:2;
		uint64_t reserved_3_7:5;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t reserved_3_7:5;
		uint64_t tt:2;
		uint64_t reserved_10_15:6;
		uint64_t grp:6;
		uint64_t reserved_22_31:10;
		uint64_t tag:8;
		uint64_t reserved_40_59:20;
		uint64_t qos_en:1;
		uint64_t tt_en:1;
		uint64_t grp_en:1;
		uint64_t tag_en:1;
#endif
	} s;
	struct cvmx_pip_bsel_tbl_entx_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t tag_en:1;
		uint64_t grp_en:1;
		uint64_t tt_en:1;
		uint64_t qos_en:1;
		uint64_t reserved_40_59:20;
		uint64_t tag:8;
		uint64_t reserved_20_31:12;
		uint64_t grp:4;
		uint64_t reserved_10_15:6;
		uint64_t tt:2;
		uint64_t reserved_3_7:5;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t reserved_3_7:5;
		uint64_t tt:2;
		uint64_t reserved_10_15:6;
		uint64_t grp:4;
		uint64_t reserved_20_31:12;
		uint64_t tag:8;
		uint64_t reserved_40_59:20;
		uint64_t qos_en:1;
		uint64_t tt_en:1;
		uint64_t grp_en:1;
		uint64_t tag_en:1;
#endif
	} cn61xx;
	struct cvmx_pip_bsel_tbl_entx_s cn68xx;
	struct cvmx_pip_bsel_tbl_entx_cn61xx cnf71xx;
};

union cvmx_pip_clken {
	uint64_t u64;
	struct cvmx_pip_clken_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t clken:1;
#else
		uint64_t clken:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_pip_clken_s cn61xx;
	struct cvmx_pip_clken_s cn63xx;
	struct cvmx_pip_clken_s cn63xxp1;
	struct cvmx_pip_clken_s cn66xx;
	struct cvmx_pip_clken_s cn68xx;
	struct cvmx_pip_clken_s cn68xxp1;
	struct cvmx_pip_clken_s cnf71xx;
};

union cvmx_pip_crc_ctlx {
	uint64_t u64;
	struct cvmx_pip_crc_ctlx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t invres:1;
		uint64_t reflect:1;
#else
		uint64_t reflect:1;
		uint64_t invres:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_pip_crc_ctlx_s cn38xx;
	struct cvmx_pip_crc_ctlx_s cn38xxp2;
	struct cvmx_pip_crc_ctlx_s cn58xx;
	struct cvmx_pip_crc_ctlx_s cn58xxp1;
};

union cvmx_pip_crc_ivx {
	uint64_t u64;
	struct cvmx_pip_crc_ivx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t iv:32;
#else
		uint64_t iv:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_pip_crc_ivx_s cn38xx;
	struct cvmx_pip_crc_ivx_s cn38xxp2;
	struct cvmx_pip_crc_ivx_s cn58xx;
	struct cvmx_pip_crc_ivx_s cn58xxp1;
};

union cvmx_pip_dec_ipsecx {
	uint64_t u64;
	struct cvmx_pip_dec_ipsecx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t tcp:1;
		uint64_t udp:1;
		uint64_t dprt:16;
#else
		uint64_t dprt:16;
		uint64_t udp:1;
		uint64_t tcp:1;
		uint64_t reserved_18_63:46;
#endif
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
	struct cvmx_pip_dec_ipsecx_s cn61xx;
	struct cvmx_pip_dec_ipsecx_s cn63xx;
	struct cvmx_pip_dec_ipsecx_s cn63xxp1;
	struct cvmx_pip_dec_ipsecx_s cn66xx;
	struct cvmx_pip_dec_ipsecx_s cn68xx;
	struct cvmx_pip_dec_ipsecx_s cn68xxp1;
	struct cvmx_pip_dec_ipsecx_s cnf71xx;
};

union cvmx_pip_dsa_src_grp {
	uint64_t u64;
	struct cvmx_pip_dsa_src_grp_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t map0:4;
		uint64_t map1:4;
		uint64_t map2:4;
		uint64_t map3:4;
		uint64_t map4:4;
		uint64_t map5:4;
		uint64_t map6:4;
		uint64_t map7:4;
		uint64_t map8:4;
		uint64_t map9:4;
		uint64_t map10:4;
		uint64_t map11:4;
		uint64_t map12:4;
		uint64_t map13:4;
		uint64_t map14:4;
		uint64_t map15:4;
#endif
	} s;
	struct cvmx_pip_dsa_src_grp_s cn52xx;
	struct cvmx_pip_dsa_src_grp_s cn52xxp1;
	struct cvmx_pip_dsa_src_grp_s cn56xx;
	struct cvmx_pip_dsa_src_grp_s cn61xx;
	struct cvmx_pip_dsa_src_grp_s cn63xx;
	struct cvmx_pip_dsa_src_grp_s cn63xxp1;
	struct cvmx_pip_dsa_src_grp_s cn66xx;
	struct cvmx_pip_dsa_src_grp_s cn68xx;
	struct cvmx_pip_dsa_src_grp_s cn68xxp1;
	struct cvmx_pip_dsa_src_grp_s cnf71xx;
};

union cvmx_pip_dsa_vid_grp {
	uint64_t u64;
	struct cvmx_pip_dsa_vid_grp_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t map0:4;
		uint64_t map1:4;
		uint64_t map2:4;
		uint64_t map3:4;
		uint64_t map4:4;
		uint64_t map5:4;
		uint64_t map6:4;
		uint64_t map7:4;
		uint64_t map8:4;
		uint64_t map9:4;
		uint64_t map10:4;
		uint64_t map11:4;
		uint64_t map12:4;
		uint64_t map13:4;
		uint64_t map14:4;
		uint64_t map15:4;
#endif
	} s;
	struct cvmx_pip_dsa_vid_grp_s cn52xx;
	struct cvmx_pip_dsa_vid_grp_s cn52xxp1;
	struct cvmx_pip_dsa_vid_grp_s cn56xx;
	struct cvmx_pip_dsa_vid_grp_s cn61xx;
	struct cvmx_pip_dsa_vid_grp_s cn63xx;
	struct cvmx_pip_dsa_vid_grp_s cn63xxp1;
	struct cvmx_pip_dsa_vid_grp_s cn66xx;
	struct cvmx_pip_dsa_vid_grp_s cn68xx;
	struct cvmx_pip_dsa_vid_grp_s cn68xxp1;
	struct cvmx_pip_dsa_vid_grp_s cnf71xx;
};

union cvmx_pip_frm_len_chkx {
	uint64_t u64;
	struct cvmx_pip_frm_len_chkx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t maxlen:16;
		uint64_t minlen:16;
#else
		uint64_t minlen:16;
		uint64_t maxlen:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_pip_frm_len_chkx_s cn50xx;
	struct cvmx_pip_frm_len_chkx_s cn52xx;
	struct cvmx_pip_frm_len_chkx_s cn52xxp1;
	struct cvmx_pip_frm_len_chkx_s cn56xx;
	struct cvmx_pip_frm_len_chkx_s cn56xxp1;
	struct cvmx_pip_frm_len_chkx_s cn61xx;
	struct cvmx_pip_frm_len_chkx_s cn63xx;
	struct cvmx_pip_frm_len_chkx_s cn63xxp1;
	struct cvmx_pip_frm_len_chkx_s cn66xx;
	struct cvmx_pip_frm_len_chkx_s cn68xx;
	struct cvmx_pip_frm_len_chkx_s cn68xxp1;
	struct cvmx_pip_frm_len_chkx_s cnf71xx;
};

union cvmx_pip_gbl_cfg {
	uint64_t u64;
	struct cvmx_pip_gbl_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t tag_syn:1;
		uint64_t ip6_udp:1;
		uint64_t max_l2:1;
		uint64_t reserved_11_15:5;
		uint64_t raw_shf:3;
		uint64_t reserved_3_7:5;
		uint64_t nip_shf:3;
#else
		uint64_t nip_shf:3;
		uint64_t reserved_3_7:5;
		uint64_t raw_shf:3;
		uint64_t reserved_11_15:5;
		uint64_t max_l2:1;
		uint64_t ip6_udp:1;
		uint64_t tag_syn:1;
		uint64_t reserved_19_63:45;
#endif
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
	struct cvmx_pip_gbl_cfg_s cn61xx;
	struct cvmx_pip_gbl_cfg_s cn63xx;
	struct cvmx_pip_gbl_cfg_s cn63xxp1;
	struct cvmx_pip_gbl_cfg_s cn66xx;
	struct cvmx_pip_gbl_cfg_s cn68xx;
	struct cvmx_pip_gbl_cfg_s cn68xxp1;
	struct cvmx_pip_gbl_cfg_s cnf71xx;
};

union cvmx_pip_gbl_ctl {
	uint64_t u64;
	struct cvmx_pip_gbl_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t egrp_dis:1;
		uint64_t ihmsk_dis:1;
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
#else
		uint64_t ip_chk:1;
		uint64_t ip_mal:1;
		uint64_t ip_hop:1;
		uint64_t ip4_opts:1;
		uint64_t ip6_eext:2;
		uint64_t reserved_6_7:2;
		uint64_t l4_mal:1;
		uint64_t l4_prt:1;
		uint64_t l4_chk:1;
		uint64_t l4_len:1;
		uint64_t tcp_flag:1;
		uint64_t l2_mal:1;
		uint64_t vs_qos:1;
		uint64_t vs_wqe:1;
		uint64_t ignrs:1;
		uint64_t reserved_17_19:3;
		uint64_t ring_en:1;
		uint64_t reserved_21_23:3;
		uint64_t dsa_grp_sid:1;
		uint64_t dsa_grp_scmd:1;
		uint64_t dsa_grp_tvid:1;
		uint64_t ihmsk_dis:1;
		uint64_t egrp_dis:1;
		uint64_t reserved_29_63:35;
#endif
	} s;
	struct cvmx_pip_gbl_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t ip_chk:1;
		uint64_t ip_mal:1;
		uint64_t ip_hop:1;
		uint64_t ip4_opts:1;
		uint64_t ip6_eext:2;
		uint64_t reserved_6_7:2;
		uint64_t l4_mal:1;
		uint64_t l4_prt:1;
		uint64_t l4_chk:1;
		uint64_t l4_len:1;
		uint64_t tcp_flag:1;
		uint64_t l2_mal:1;
		uint64_t vs_qos:1;
		uint64_t vs_wqe:1;
		uint64_t ignrs:1;
		uint64_t reserved_17_63:47;
#endif
	} cn30xx;
	struct cvmx_pip_gbl_ctl_cn30xx cn31xx;
	struct cvmx_pip_gbl_ctl_cn30xx cn38xx;
	struct cvmx_pip_gbl_ctl_cn30xx cn38xxp2;
	struct cvmx_pip_gbl_ctl_cn30xx cn50xx;
	struct cvmx_pip_gbl_ctl_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t ip_chk:1;
		uint64_t ip_mal:1;
		uint64_t ip_hop:1;
		uint64_t ip4_opts:1;
		uint64_t ip6_eext:2;
		uint64_t reserved_6_7:2;
		uint64_t l4_mal:1;
		uint64_t l4_prt:1;
		uint64_t l4_chk:1;
		uint64_t l4_len:1;
		uint64_t tcp_flag:1;
		uint64_t l2_mal:1;
		uint64_t vs_qos:1;
		uint64_t vs_wqe:1;
		uint64_t ignrs:1;
		uint64_t reserved_17_19:3;
		uint64_t ring_en:1;
		uint64_t reserved_21_23:3;
		uint64_t dsa_grp_sid:1;
		uint64_t dsa_grp_scmd:1;
		uint64_t dsa_grp_tvid:1;
		uint64_t reserved_27_63:37;
#endif
	} cn52xx;
	struct cvmx_pip_gbl_ctl_cn52xx cn52xxp1;
	struct cvmx_pip_gbl_ctl_cn52xx cn56xx;
	struct cvmx_pip_gbl_ctl_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t ip_chk:1;
		uint64_t ip_mal:1;
		uint64_t ip_hop:1;
		uint64_t ip4_opts:1;
		uint64_t ip6_eext:2;
		uint64_t reserved_6_7:2;
		uint64_t l4_mal:1;
		uint64_t l4_prt:1;
		uint64_t l4_chk:1;
		uint64_t l4_len:1;
		uint64_t tcp_flag:1;
		uint64_t l2_mal:1;
		uint64_t vs_qos:1;
		uint64_t vs_wqe:1;
		uint64_t ignrs:1;
		uint64_t reserved_17_19:3;
		uint64_t ring_en:1;
		uint64_t reserved_21_63:43;
#endif
	} cn56xxp1;
	struct cvmx_pip_gbl_ctl_cn30xx cn58xx;
	struct cvmx_pip_gbl_ctl_cn30xx cn58xxp1;
	struct cvmx_pip_gbl_ctl_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t ihmsk_dis:1;
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
#else
		uint64_t ip_chk:1;
		uint64_t ip_mal:1;
		uint64_t ip_hop:1;
		uint64_t ip4_opts:1;
		uint64_t ip6_eext:2;
		uint64_t reserved_6_7:2;
		uint64_t l4_mal:1;
		uint64_t l4_prt:1;
		uint64_t l4_chk:1;
		uint64_t l4_len:1;
		uint64_t tcp_flag:1;
		uint64_t l2_mal:1;
		uint64_t vs_qos:1;
		uint64_t vs_wqe:1;
		uint64_t ignrs:1;
		uint64_t reserved_17_19:3;
		uint64_t ring_en:1;
		uint64_t reserved_21_23:3;
		uint64_t dsa_grp_sid:1;
		uint64_t dsa_grp_scmd:1;
		uint64_t dsa_grp_tvid:1;
		uint64_t ihmsk_dis:1;
		uint64_t reserved_28_63:36;
#endif
	} cn61xx;
	struct cvmx_pip_gbl_ctl_cn61xx cn63xx;
	struct cvmx_pip_gbl_ctl_cn61xx cn63xxp1;
	struct cvmx_pip_gbl_ctl_cn61xx cn66xx;
	struct cvmx_pip_gbl_ctl_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t egrp_dis:1;
		uint64_t ihmsk_dis:1;
		uint64_t dsa_grp_tvid:1;
		uint64_t dsa_grp_scmd:1;
		uint64_t dsa_grp_sid:1;
		uint64_t reserved_17_23:7;
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
#else
		uint64_t ip_chk:1;
		uint64_t ip_mal:1;
		uint64_t ip_hop:1;
		uint64_t ip4_opts:1;
		uint64_t ip6_eext:2;
		uint64_t reserved_6_7:2;
		uint64_t l4_mal:1;
		uint64_t l4_prt:1;
		uint64_t l4_chk:1;
		uint64_t l4_len:1;
		uint64_t tcp_flag:1;
		uint64_t l2_mal:1;
		uint64_t vs_qos:1;
		uint64_t vs_wqe:1;
		uint64_t ignrs:1;
		uint64_t reserved_17_23:7;
		uint64_t dsa_grp_sid:1;
		uint64_t dsa_grp_scmd:1;
		uint64_t dsa_grp_tvid:1;
		uint64_t ihmsk_dis:1;
		uint64_t egrp_dis:1;
		uint64_t reserved_29_63:35;
#endif
	} cn68xx;
	struct cvmx_pip_gbl_ctl_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t ihmsk_dis:1;
		uint64_t dsa_grp_tvid:1;
		uint64_t dsa_grp_scmd:1;
		uint64_t dsa_grp_sid:1;
		uint64_t reserved_17_23:7;
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
#else
		uint64_t ip_chk:1;
		uint64_t ip_mal:1;
		uint64_t ip_hop:1;
		uint64_t ip4_opts:1;
		uint64_t ip6_eext:2;
		uint64_t reserved_6_7:2;
		uint64_t l4_mal:1;
		uint64_t l4_prt:1;
		uint64_t l4_chk:1;
		uint64_t l4_len:1;
		uint64_t tcp_flag:1;
		uint64_t l2_mal:1;
		uint64_t vs_qos:1;
		uint64_t vs_wqe:1;
		uint64_t ignrs:1;
		uint64_t reserved_17_23:7;
		uint64_t dsa_grp_sid:1;
		uint64_t dsa_grp_scmd:1;
		uint64_t dsa_grp_tvid:1;
		uint64_t ihmsk_dis:1;
		uint64_t reserved_28_63:36;
#endif
	} cn68xxp1;
	struct cvmx_pip_gbl_ctl_cn61xx cnf71xx;
};

union cvmx_pip_hg_pri_qos {
	uint64_t u64;
	struct cvmx_pip_hg_pri_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t up_qos:1;
		uint64_t reserved_11_11:1;
		uint64_t qos:3;
		uint64_t reserved_6_7:2;
		uint64_t pri:6;
#else
		uint64_t pri:6;
		uint64_t reserved_6_7:2;
		uint64_t qos:3;
		uint64_t reserved_11_11:1;
		uint64_t up_qos:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_pip_hg_pri_qos_s cn52xx;
	struct cvmx_pip_hg_pri_qos_s cn52xxp1;
	struct cvmx_pip_hg_pri_qos_s cn56xx;
	struct cvmx_pip_hg_pri_qos_s cn61xx;
	struct cvmx_pip_hg_pri_qos_s cn63xx;
	struct cvmx_pip_hg_pri_qos_s cn63xxp1;
	struct cvmx_pip_hg_pri_qos_s cn66xx;
	struct cvmx_pip_hg_pri_qos_s cnf71xx;
};

union cvmx_pip_int_en {
	uint64_t u64;
	struct cvmx_pip_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t crcerr:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t minerr:1;
		uint64_t maxerr:1;
		uint64_t lenerr:1;
		uint64_t punyerr:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_pip_int_en_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t crcerr:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t reserved_9_63:55;
#endif
	} cn30xx;
	struct cvmx_pip_int_en_cn30xx cn31xx;
	struct cvmx_pip_int_en_cn30xx cn38xx;
	struct cvmx_pip_int_en_cn30xx cn38xxp2;
	struct cvmx_pip_int_en_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t reserved_1_1:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t minerr:1;
		uint64_t maxerr:1;
		uint64_t lenerr:1;
		uint64_t reserved_12_63:52;
#endif
	} cn50xx;
	struct cvmx_pip_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t reserved_1_1:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t minerr:1;
		uint64_t maxerr:1;
		uint64_t lenerr:1;
		uint64_t punyerr:1;
		uint64_t reserved_13_63:51;
#endif
	} cn52xx;
	struct cvmx_pip_int_en_cn52xx cn52xxp1;
	struct cvmx_pip_int_en_s cn56xx;
	struct cvmx_pip_int_en_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t crcerr:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t minerr:1;
		uint64_t maxerr:1;
		uint64_t lenerr:1;
		uint64_t reserved_12_63:52;
#endif
	} cn56xxp1;
	struct cvmx_pip_int_en_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t crcerr:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t reserved_9_11:3;
		uint64_t punyerr:1;
		uint64_t reserved_13_63:51;
#endif
	} cn58xx;
	struct cvmx_pip_int_en_cn30xx cn58xxp1;
	struct cvmx_pip_int_en_s cn61xx;
	struct cvmx_pip_int_en_s cn63xx;
	struct cvmx_pip_int_en_s cn63xxp1;
	struct cvmx_pip_int_en_s cn66xx;
	struct cvmx_pip_int_en_s cn68xx;
	struct cvmx_pip_int_en_s cn68xxp1;
	struct cvmx_pip_int_en_s cnf71xx;
};

union cvmx_pip_int_reg {
	uint64_t u64;
	struct cvmx_pip_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t crcerr:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t minerr:1;
		uint64_t maxerr:1;
		uint64_t lenerr:1;
		uint64_t punyerr:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_pip_int_reg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t crcerr:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t reserved_9_63:55;
#endif
	} cn30xx;
	struct cvmx_pip_int_reg_cn30xx cn31xx;
	struct cvmx_pip_int_reg_cn30xx cn38xx;
	struct cvmx_pip_int_reg_cn30xx cn38xxp2;
	struct cvmx_pip_int_reg_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t reserved_1_1:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t minerr:1;
		uint64_t maxerr:1;
		uint64_t lenerr:1;
		uint64_t reserved_12_63:52;
#endif
	} cn50xx;
	struct cvmx_pip_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t reserved_1_1:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t minerr:1;
		uint64_t maxerr:1;
		uint64_t lenerr:1;
		uint64_t punyerr:1;
		uint64_t reserved_13_63:51;
#endif
	} cn52xx;
	struct cvmx_pip_int_reg_cn52xx cn52xxp1;
	struct cvmx_pip_int_reg_s cn56xx;
	struct cvmx_pip_int_reg_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t crcerr:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t minerr:1;
		uint64_t maxerr:1;
		uint64_t lenerr:1;
		uint64_t reserved_12_63:52;
#endif
	} cn56xxp1;
	struct cvmx_pip_int_reg_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pktdrp:1;
		uint64_t crcerr:1;
		uint64_t bckprs:1;
		uint64_t prtnxa:1;
		uint64_t badtag:1;
		uint64_t skprunt:1;
		uint64_t todoovr:1;
		uint64_t feperr:1;
		uint64_t beperr:1;
		uint64_t reserved_9_11:3;
		uint64_t punyerr:1;
		uint64_t reserved_13_63:51;
#endif
	} cn58xx;
	struct cvmx_pip_int_reg_cn30xx cn58xxp1;
	struct cvmx_pip_int_reg_s cn61xx;
	struct cvmx_pip_int_reg_s cn63xx;
	struct cvmx_pip_int_reg_s cn63xxp1;
	struct cvmx_pip_int_reg_s cn66xx;
	struct cvmx_pip_int_reg_s cn68xx;
	struct cvmx_pip_int_reg_s cn68xxp1;
	struct cvmx_pip_int_reg_s cnf71xx;
};

union cvmx_pip_ip_offset {
	uint64_t u64;
	struct cvmx_pip_ip_offset_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t offset:3;
#else
		uint64_t offset:3;
		uint64_t reserved_3_63:61;
#endif
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
	struct cvmx_pip_ip_offset_s cn61xx;
	struct cvmx_pip_ip_offset_s cn63xx;
	struct cvmx_pip_ip_offset_s cn63xxp1;
	struct cvmx_pip_ip_offset_s cn66xx;
	struct cvmx_pip_ip_offset_s cn68xx;
	struct cvmx_pip_ip_offset_s cn68xxp1;
	struct cvmx_pip_ip_offset_s cnf71xx;
};

union cvmx_pip_pri_tblx {
	uint64_t u64;
	struct cvmx_pip_pri_tblx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t diff2_padd:8;
		uint64_t hg2_padd:8;
		uint64_t vlan2_padd:8;
		uint64_t reserved_38_39:2;
		uint64_t diff2_bpid:6;
		uint64_t reserved_30_31:2;
		uint64_t hg2_bpid:6;
		uint64_t reserved_22_23:2;
		uint64_t vlan2_bpid:6;
		uint64_t reserved_11_15:5;
		uint64_t diff2_qos:3;
		uint64_t reserved_7_7:1;
		uint64_t hg2_qos:3;
		uint64_t reserved_3_3:1;
		uint64_t vlan2_qos:3;
#else
		uint64_t vlan2_qos:3;
		uint64_t reserved_3_3:1;
		uint64_t hg2_qos:3;
		uint64_t reserved_7_7:1;
		uint64_t diff2_qos:3;
		uint64_t reserved_11_15:5;
		uint64_t vlan2_bpid:6;
		uint64_t reserved_22_23:2;
		uint64_t hg2_bpid:6;
		uint64_t reserved_30_31:2;
		uint64_t diff2_bpid:6;
		uint64_t reserved_38_39:2;
		uint64_t vlan2_padd:8;
		uint64_t hg2_padd:8;
		uint64_t diff2_padd:8;
#endif
	} s;
	struct cvmx_pip_pri_tblx_s cn68xx;
	struct cvmx_pip_pri_tblx_s cn68xxp1;
};

union cvmx_pip_prt_cfgx {
	uint64_t u64;
	struct cvmx_pip_prt_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_55_63:9;
		uint64_t ih_pri:1;
		uint64_t len_chk_sel:1;
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
#else
		uint64_t skip:7;
		uint64_t reserved_7_7:1;
		uint64_t mode:2;
		uint64_t dsa_en:1;
		uint64_t higig_en:1;
		uint64_t crc_en:1;
		uint64_t reserved_13_15:3;
		uint64_t qos_vlan:1;
		uint64_t qos_diff:1;
		uint64_t qos_vod:1;
		uint64_t qos_vsel:1;
		uint64_t qos_wat:4;
		uint64_t qos:3;
		uint64_t hg_qos:1;
		uint64_t grp_wat:4;
		uint64_t inst_hdr:1;
		uint64_t dyn_rs:1;
		uint64_t tag_inc:2;
		uint64_t rawdrp:1;
		uint64_t reserved_37_39:3;
		uint64_t qos_wat_47:4;
		uint64_t grp_wat_47:4;
		uint64_t minerr_en:1;
		uint64_t maxerr_en:1;
		uint64_t lenerr_en:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t len_chk_sel:1;
		uint64_t ih_pri:1;
		uint64_t reserved_55_63:9;
#endif
	} s;
	struct cvmx_pip_prt_cfgx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t skip:7;
		uint64_t reserved_7_7:1;
		uint64_t mode:2;
		uint64_t reserved_10_15:6;
		uint64_t qos_vlan:1;
		uint64_t qos_diff:1;
		uint64_t reserved_18_19:2;
		uint64_t qos_wat:4;
		uint64_t qos:3;
		uint64_t reserved_27_27:1;
		uint64_t grp_wat:4;
		uint64_t inst_hdr:1;
		uint64_t dyn_rs:1;
		uint64_t tag_inc:2;
		uint64_t rawdrp:1;
		uint64_t reserved_37_63:27;
#endif
	} cn30xx;
	struct cvmx_pip_prt_cfgx_cn30xx cn31xx;
	struct cvmx_pip_prt_cfgx_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t skip:7;
		uint64_t reserved_7_7:1;
		uint64_t mode:2;
		uint64_t reserved_10_11:2;
		uint64_t crc_en:1;
		uint64_t reserved_13_15:3;
		uint64_t qos_vlan:1;
		uint64_t qos_diff:1;
		uint64_t reserved_18_19:2;
		uint64_t qos_wat:4;
		uint64_t qos:3;
		uint64_t reserved_27_27:1;
		uint64_t grp_wat:4;
		uint64_t inst_hdr:1;
		uint64_t dyn_rs:1;
		uint64_t tag_inc:2;
		uint64_t rawdrp:1;
		uint64_t reserved_37_63:27;
#endif
	} cn38xx;
	struct cvmx_pip_prt_cfgx_cn38xx cn38xxp2;
	struct cvmx_pip_prt_cfgx_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t skip:7;
		uint64_t reserved_7_7:1;
		uint64_t mode:2;
		uint64_t reserved_10_11:2;
		uint64_t crc_en:1;
		uint64_t reserved_13_15:3;
		uint64_t qos_vlan:1;
		uint64_t qos_diff:1;
		uint64_t qos_vod:1;
		uint64_t reserved_19_19:1;
		uint64_t qos_wat:4;
		uint64_t qos:3;
		uint64_t reserved_27_27:1;
		uint64_t grp_wat:4;
		uint64_t inst_hdr:1;
		uint64_t dyn_rs:1;
		uint64_t tag_inc:2;
		uint64_t rawdrp:1;
		uint64_t reserved_37_39:3;
		uint64_t qos_wat_47:4;
		uint64_t grp_wat_47:4;
		uint64_t minerr_en:1;
		uint64_t maxerr_en:1;
		uint64_t lenerr_en:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t reserved_53_63:11;
#endif
	} cn50xx;
	struct cvmx_pip_prt_cfgx_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t skip:7;
		uint64_t reserved_7_7:1;
		uint64_t mode:2;
		uint64_t dsa_en:1;
		uint64_t higig_en:1;
		uint64_t crc_en:1;
		uint64_t reserved_13_15:3;
		uint64_t qos_vlan:1;
		uint64_t qos_diff:1;
		uint64_t qos_vod:1;
		uint64_t qos_vsel:1;
		uint64_t qos_wat:4;
		uint64_t qos:3;
		uint64_t hg_qos:1;
		uint64_t grp_wat:4;
		uint64_t inst_hdr:1;
		uint64_t dyn_rs:1;
		uint64_t tag_inc:2;
		uint64_t rawdrp:1;
		uint64_t reserved_37_39:3;
		uint64_t qos_wat_47:4;
		uint64_t grp_wat_47:4;
		uint64_t minerr_en:1;
		uint64_t maxerr_en:1;
		uint64_t lenerr_en:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t reserved_53_63:11;
#endif
	} cn52xx;
	struct cvmx_pip_prt_cfgx_cn52xx cn52xxp1;
	struct cvmx_pip_prt_cfgx_cn52xx cn56xx;
	struct cvmx_pip_prt_cfgx_cn50xx cn56xxp1;
	struct cvmx_pip_prt_cfgx_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t skip:7;
		uint64_t reserved_7_7:1;
		uint64_t mode:2;
		uint64_t reserved_10_11:2;
		uint64_t crc_en:1;
		uint64_t reserved_13_15:3;
		uint64_t qos_vlan:1;
		uint64_t qos_diff:1;
		uint64_t qos_vod:1;
		uint64_t reserved_19_19:1;
		uint64_t qos_wat:4;
		uint64_t qos:3;
		uint64_t reserved_27_27:1;
		uint64_t grp_wat:4;
		uint64_t inst_hdr:1;
		uint64_t dyn_rs:1;
		uint64_t tag_inc:2;
		uint64_t rawdrp:1;
		uint64_t reserved_37_63:27;
#endif
	} cn58xx;
	struct cvmx_pip_prt_cfgx_cn58xx cn58xxp1;
	struct cvmx_pip_prt_cfgx_cn52xx cn61xx;
	struct cvmx_pip_prt_cfgx_cn52xx cn63xx;
	struct cvmx_pip_prt_cfgx_cn52xx cn63xxp1;
	struct cvmx_pip_prt_cfgx_cn52xx cn66xx;
	struct cvmx_pip_prt_cfgx_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_55_63:9;
		uint64_t ih_pri:1;
		uint64_t len_chk_sel:1;
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
		uint64_t reserved_19_19:1;
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
#else
		uint64_t skip:7;
		uint64_t reserved_7_7:1;
		uint64_t mode:2;
		uint64_t dsa_en:1;
		uint64_t higig_en:1;
		uint64_t crc_en:1;
		uint64_t reserved_13_15:3;
		uint64_t qos_vlan:1;
		uint64_t qos_diff:1;
		uint64_t qos_vod:1;
		uint64_t reserved_19_19:1;
		uint64_t qos_wat:4;
		uint64_t qos:3;
		uint64_t hg_qos:1;
		uint64_t grp_wat:4;
		uint64_t inst_hdr:1;
		uint64_t dyn_rs:1;
		uint64_t tag_inc:2;
		uint64_t rawdrp:1;
		uint64_t reserved_37_39:3;
		uint64_t qos_wat_47:4;
		uint64_t grp_wat_47:4;
		uint64_t minerr_en:1;
		uint64_t maxerr_en:1;
		uint64_t lenerr_en:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t len_chk_sel:1;
		uint64_t ih_pri:1;
		uint64_t reserved_55_63:9;
#endif
	} cn68xx;
	struct cvmx_pip_prt_cfgx_cn68xx cn68xxp1;
	struct cvmx_pip_prt_cfgx_cn52xx cnf71xx;
};

union cvmx_pip_prt_cfgbx {
	uint64_t u64;
	struct cvmx_pip_prt_cfgbx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_39_63:25;
		uint64_t alt_skp_sel:2;
		uint64_t alt_skp_en:1;
		uint64_t reserved_35_35:1;
		uint64_t bsel_num:2;
		uint64_t bsel_en:1;
		uint64_t reserved_24_31:8;
		uint64_t base:8;
		uint64_t reserved_6_15:10;
		uint64_t bpid:6;
#else
		uint64_t bpid:6;
		uint64_t reserved_6_15:10;
		uint64_t base:8;
		uint64_t reserved_24_31:8;
		uint64_t bsel_en:1;
		uint64_t bsel_num:2;
		uint64_t reserved_35_35:1;
		uint64_t alt_skp_en:1;
		uint64_t alt_skp_sel:2;
		uint64_t reserved_39_63:25;
#endif
	} s;
	struct cvmx_pip_prt_cfgbx_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_39_63:25;
		uint64_t alt_skp_sel:2;
		uint64_t alt_skp_en:1;
		uint64_t reserved_35_35:1;
		uint64_t bsel_num:2;
		uint64_t bsel_en:1;
		uint64_t reserved_0_31:32;
#else
		uint64_t reserved_0_31:32;
		uint64_t bsel_en:1;
		uint64_t bsel_num:2;
		uint64_t reserved_35_35:1;
		uint64_t alt_skp_en:1;
		uint64_t alt_skp_sel:2;
		uint64_t reserved_39_63:25;
#endif
	} cn61xx;
	struct cvmx_pip_prt_cfgbx_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_39_63:25;
		uint64_t alt_skp_sel:2;
		uint64_t alt_skp_en:1;
		uint64_t reserved_0_35:36;
#else
		uint64_t reserved_0_35:36;
		uint64_t alt_skp_en:1;
		uint64_t alt_skp_sel:2;
		uint64_t reserved_39_63:25;
#endif
	} cn66xx;
	struct cvmx_pip_prt_cfgbx_s cn68xx;
	struct cvmx_pip_prt_cfgbx_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t base:8;
		uint64_t reserved_6_15:10;
		uint64_t bpid:6;
#else
		uint64_t bpid:6;
		uint64_t reserved_6_15:10;
		uint64_t base:8;
		uint64_t reserved_24_63:40;
#endif
	} cn68xxp1;
	struct cvmx_pip_prt_cfgbx_cn61xx cnf71xx;
};

union cvmx_pip_prt_tagx {
	uint64_t u64;
	struct cvmx_pip_prt_tagx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t portadd_en:1;
		uint64_t inc_hwchk:1;
		uint64_t reserved_50_51:2;
		uint64_t grptagbase_msb:2;
		uint64_t reserved_46_47:2;
		uint64_t grptagmask_msb:2;
		uint64_t reserved_42_43:2;
		uint64_t grp_msb:2;
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
#else
		uint64_t grp:4;
		uint64_t non_tag_type:2;
		uint64_t ip4_tag_type:2;
		uint64_t ip6_tag_type:2;
		uint64_t tcp4_tag_type:2;
		uint64_t tcp6_tag_type:2;
		uint64_t ip4_src_flag:1;
		uint64_t ip6_src_flag:1;
		uint64_t ip4_dst_flag:1;
		uint64_t ip6_dst_flag:1;
		uint64_t ip4_pctl_flag:1;
		uint64_t ip6_nxth_flag:1;
		uint64_t ip4_sprt_flag:1;
		uint64_t ip6_sprt_flag:1;
		uint64_t ip4_dprt_flag:1;
		uint64_t ip6_dprt_flag:1;
		uint64_t inc_prt_flag:1;
		uint64_t inc_vlan:1;
		uint64_t inc_vs:2;
		uint64_t tag_mode:2;
		uint64_t grptag_mskip:1;
		uint64_t grptag:1;
		uint64_t grptagmask:4;
		uint64_t grptagbase:4;
		uint64_t grp_msb:2;
		uint64_t reserved_42_43:2;
		uint64_t grptagmask_msb:2;
		uint64_t reserved_46_47:2;
		uint64_t grptagbase_msb:2;
		uint64_t reserved_50_51:2;
		uint64_t inc_hwchk:1;
		uint64_t portadd_en:1;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_pip_prt_tagx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t grp:4;
		uint64_t non_tag_type:2;
		uint64_t ip4_tag_type:2;
		uint64_t ip6_tag_type:2;
		uint64_t tcp4_tag_type:2;
		uint64_t tcp6_tag_type:2;
		uint64_t ip4_src_flag:1;
		uint64_t ip6_src_flag:1;
		uint64_t ip4_dst_flag:1;
		uint64_t ip6_dst_flag:1;
		uint64_t ip4_pctl_flag:1;
		uint64_t ip6_nxth_flag:1;
		uint64_t ip4_sprt_flag:1;
		uint64_t ip6_sprt_flag:1;
		uint64_t ip4_dprt_flag:1;
		uint64_t ip6_dprt_flag:1;
		uint64_t inc_prt_flag:1;
		uint64_t inc_vlan:1;
		uint64_t inc_vs:2;
		uint64_t tag_mode:2;
		uint64_t reserved_30_30:1;
		uint64_t grptag:1;
		uint64_t grptagmask:4;
		uint64_t grptagbase:4;
		uint64_t reserved_40_63:24;
#endif
	} cn30xx;
	struct cvmx_pip_prt_tagx_cn30xx cn31xx;
	struct cvmx_pip_prt_tagx_cn30xx cn38xx;
	struct cvmx_pip_prt_tagx_cn30xx cn38xxp2;
	struct cvmx_pip_prt_tagx_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t grp:4;
		uint64_t non_tag_type:2;
		uint64_t ip4_tag_type:2;
		uint64_t ip6_tag_type:2;
		uint64_t tcp4_tag_type:2;
		uint64_t tcp6_tag_type:2;
		uint64_t ip4_src_flag:1;
		uint64_t ip6_src_flag:1;
		uint64_t ip4_dst_flag:1;
		uint64_t ip6_dst_flag:1;
		uint64_t ip4_pctl_flag:1;
		uint64_t ip6_nxth_flag:1;
		uint64_t ip4_sprt_flag:1;
		uint64_t ip6_sprt_flag:1;
		uint64_t ip4_dprt_flag:1;
		uint64_t ip6_dprt_flag:1;
		uint64_t inc_prt_flag:1;
		uint64_t inc_vlan:1;
		uint64_t inc_vs:2;
		uint64_t tag_mode:2;
		uint64_t grptag_mskip:1;
		uint64_t grptag:1;
		uint64_t grptagmask:4;
		uint64_t grptagbase:4;
		uint64_t reserved_40_63:24;
#endif
	} cn50xx;
	struct cvmx_pip_prt_tagx_cn50xx cn52xx;
	struct cvmx_pip_prt_tagx_cn50xx cn52xxp1;
	struct cvmx_pip_prt_tagx_cn50xx cn56xx;
	struct cvmx_pip_prt_tagx_cn50xx cn56xxp1;
	struct cvmx_pip_prt_tagx_cn30xx cn58xx;
	struct cvmx_pip_prt_tagx_cn30xx cn58xxp1;
	struct cvmx_pip_prt_tagx_cn50xx cn61xx;
	struct cvmx_pip_prt_tagx_cn50xx cn63xx;
	struct cvmx_pip_prt_tagx_cn50xx cn63xxp1;
	struct cvmx_pip_prt_tagx_cn50xx cn66xx;
	struct cvmx_pip_prt_tagx_s cn68xx;
	struct cvmx_pip_prt_tagx_s cn68xxp1;
	struct cvmx_pip_prt_tagx_cn50xx cnf71xx;
};

union cvmx_pip_qos_diffx {
	uint64_t u64;
	struct cvmx_pip_qos_diffx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t reserved_3_63:61;
#endif
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
	struct cvmx_pip_qos_diffx_s cn61xx;
	struct cvmx_pip_qos_diffx_s cn63xx;
	struct cvmx_pip_qos_diffx_s cn63xxp1;
	struct cvmx_pip_qos_diffx_s cn66xx;
	struct cvmx_pip_qos_diffx_s cnf71xx;
};

union cvmx_pip_qos_vlanx {
	uint64_t u64;
	struct cvmx_pip_qos_vlanx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t qos1:3;
		uint64_t reserved_3_3:1;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t reserved_3_3:1;
		uint64_t qos1:3;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_pip_qos_vlanx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t reserved_3_63:61;
#endif
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
	struct cvmx_pip_qos_vlanx_s cn61xx;
	struct cvmx_pip_qos_vlanx_s cn63xx;
	struct cvmx_pip_qos_vlanx_s cn63xxp1;
	struct cvmx_pip_qos_vlanx_s cn66xx;
	struct cvmx_pip_qos_vlanx_s cnf71xx;
};

union cvmx_pip_qos_watchx {
	uint64_t u64;
	struct cvmx_pip_qos_watchx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t mask:16;
		uint64_t reserved_30_31:2;
		uint64_t grp:6;
		uint64_t reserved_23_23:1;
		uint64_t qos:3;
		uint64_t reserved_19_19:1;
		uint64_t match_type:3;
		uint64_t match_value:16;
#else
		uint64_t match_value:16;
		uint64_t match_type:3;
		uint64_t reserved_19_19:1;
		uint64_t qos:3;
		uint64_t reserved_23_23:1;
		uint64_t grp:6;
		uint64_t reserved_30_31:2;
		uint64_t mask:16;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_pip_qos_watchx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t mask:16;
		uint64_t reserved_28_31:4;
		uint64_t grp:4;
		uint64_t reserved_23_23:1;
		uint64_t qos:3;
		uint64_t reserved_18_19:2;
		uint64_t match_type:2;
		uint64_t match_value:16;
#else
		uint64_t match_value:16;
		uint64_t match_type:2;
		uint64_t reserved_18_19:2;
		uint64_t qos:3;
		uint64_t reserved_23_23:1;
		uint64_t grp:4;
		uint64_t reserved_28_31:4;
		uint64_t mask:16;
		uint64_t reserved_48_63:16;
#endif
	} cn30xx;
	struct cvmx_pip_qos_watchx_cn30xx cn31xx;
	struct cvmx_pip_qos_watchx_cn30xx cn38xx;
	struct cvmx_pip_qos_watchx_cn30xx cn38xxp2;
	struct cvmx_pip_qos_watchx_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t mask:16;
		uint64_t reserved_28_31:4;
		uint64_t grp:4;
		uint64_t reserved_23_23:1;
		uint64_t qos:3;
		uint64_t reserved_19_19:1;
		uint64_t match_type:3;
		uint64_t match_value:16;
#else
		uint64_t match_value:16;
		uint64_t match_type:3;
		uint64_t reserved_19_19:1;
		uint64_t qos:3;
		uint64_t reserved_23_23:1;
		uint64_t grp:4;
		uint64_t reserved_28_31:4;
		uint64_t mask:16;
		uint64_t reserved_48_63:16;
#endif
	} cn50xx;
	struct cvmx_pip_qos_watchx_cn50xx cn52xx;
	struct cvmx_pip_qos_watchx_cn50xx cn52xxp1;
	struct cvmx_pip_qos_watchx_cn50xx cn56xx;
	struct cvmx_pip_qos_watchx_cn50xx cn56xxp1;
	struct cvmx_pip_qos_watchx_cn30xx cn58xx;
	struct cvmx_pip_qos_watchx_cn30xx cn58xxp1;
	struct cvmx_pip_qos_watchx_cn50xx cn61xx;
	struct cvmx_pip_qos_watchx_cn50xx cn63xx;
	struct cvmx_pip_qos_watchx_cn50xx cn63xxp1;
	struct cvmx_pip_qos_watchx_cn50xx cn66xx;
	struct cvmx_pip_qos_watchx_s cn68xx;
	struct cvmx_pip_qos_watchx_s cn68xxp1;
	struct cvmx_pip_qos_watchx_cn50xx cnf71xx;
};

union cvmx_pip_raw_word {
	uint64_t u64;
	struct cvmx_pip_raw_word_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t word:56;
#else
		uint64_t word:56;
		uint64_t reserved_56_63:8;
#endif
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
	struct cvmx_pip_raw_word_s cn61xx;
	struct cvmx_pip_raw_word_s cn63xx;
	struct cvmx_pip_raw_word_s cn63xxp1;
	struct cvmx_pip_raw_word_s cn66xx;
	struct cvmx_pip_raw_word_s cn68xx;
	struct cvmx_pip_raw_word_s cn68xxp1;
	struct cvmx_pip_raw_word_s cnf71xx;
};

union cvmx_pip_sft_rst {
	uint64_t u64;
	struct cvmx_pip_sft_rst_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t rst:1;
#else
		uint64_t rst:1;
		uint64_t reserved_1_63:63;
#endif
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
	struct cvmx_pip_sft_rst_s cn61xx;
	struct cvmx_pip_sft_rst_s cn63xx;
	struct cvmx_pip_sft_rst_s cn63xxp1;
	struct cvmx_pip_sft_rst_s cn66xx;
	struct cvmx_pip_sft_rst_s cn68xx;
	struct cvmx_pip_sft_rst_s cn68xxp1;
	struct cvmx_pip_sft_rst_s cnf71xx;
};

union cvmx_pip_stat0_x {
	uint64_t u64;
	struct cvmx_pip_stat0_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t drp_pkts:32;
		uint64_t drp_octs:32;
#else
		uint64_t drp_octs:32;
		uint64_t drp_pkts:32;
#endif
	} s;
	struct cvmx_pip_stat0_x_s cn68xx;
	struct cvmx_pip_stat0_x_s cn68xxp1;
};

union cvmx_pip_stat0_prtx {
	uint64_t u64;
	struct cvmx_pip_stat0_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t drp_pkts:32;
		uint64_t drp_octs:32;
#else
		uint64_t drp_octs:32;
		uint64_t drp_pkts:32;
#endif
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
	struct cvmx_pip_stat0_prtx_s cn61xx;
	struct cvmx_pip_stat0_prtx_s cn63xx;
	struct cvmx_pip_stat0_prtx_s cn63xxp1;
	struct cvmx_pip_stat0_prtx_s cn66xx;
	struct cvmx_pip_stat0_prtx_s cnf71xx;
};

union cvmx_pip_stat10_x {
	uint64_t u64;
	struct cvmx_pip_stat10_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bcast:32;
		uint64_t mcast:32;
#else
		uint64_t mcast:32;
		uint64_t bcast:32;
#endif
	} s;
	struct cvmx_pip_stat10_x_s cn68xx;
	struct cvmx_pip_stat10_x_s cn68xxp1;
};

union cvmx_pip_stat10_prtx {
	uint64_t u64;
	struct cvmx_pip_stat10_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bcast:32;
		uint64_t mcast:32;
#else
		uint64_t mcast:32;
		uint64_t bcast:32;
#endif
	} s;
	struct cvmx_pip_stat10_prtx_s cn52xx;
	struct cvmx_pip_stat10_prtx_s cn52xxp1;
	struct cvmx_pip_stat10_prtx_s cn56xx;
	struct cvmx_pip_stat10_prtx_s cn56xxp1;
	struct cvmx_pip_stat10_prtx_s cn61xx;
	struct cvmx_pip_stat10_prtx_s cn63xx;
	struct cvmx_pip_stat10_prtx_s cn63xxp1;
	struct cvmx_pip_stat10_prtx_s cn66xx;
	struct cvmx_pip_stat10_prtx_s cnf71xx;
};

union cvmx_pip_stat11_x {
	uint64_t u64;
	struct cvmx_pip_stat11_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bcast:32;
		uint64_t mcast:32;
#else
		uint64_t mcast:32;
		uint64_t bcast:32;
#endif
	} s;
	struct cvmx_pip_stat11_x_s cn68xx;
	struct cvmx_pip_stat11_x_s cn68xxp1;
};

union cvmx_pip_stat11_prtx {
	uint64_t u64;
	struct cvmx_pip_stat11_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bcast:32;
		uint64_t mcast:32;
#else
		uint64_t mcast:32;
		uint64_t bcast:32;
#endif
	} s;
	struct cvmx_pip_stat11_prtx_s cn52xx;
	struct cvmx_pip_stat11_prtx_s cn52xxp1;
	struct cvmx_pip_stat11_prtx_s cn56xx;
	struct cvmx_pip_stat11_prtx_s cn56xxp1;
	struct cvmx_pip_stat11_prtx_s cn61xx;
	struct cvmx_pip_stat11_prtx_s cn63xx;
	struct cvmx_pip_stat11_prtx_s cn63xxp1;
	struct cvmx_pip_stat11_prtx_s cn66xx;
	struct cvmx_pip_stat11_prtx_s cnf71xx;
};

union cvmx_pip_stat1_x {
	uint64_t u64;
	struct cvmx_pip_stat1_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t octs:48;
#else
		uint64_t octs:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_pip_stat1_x_s cn68xx;
	struct cvmx_pip_stat1_x_s cn68xxp1;
};

union cvmx_pip_stat1_prtx {
	uint64_t u64;
	struct cvmx_pip_stat1_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t octs:48;
#else
		uint64_t octs:48;
		uint64_t reserved_48_63:16;
#endif
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
	struct cvmx_pip_stat1_prtx_s cn61xx;
	struct cvmx_pip_stat1_prtx_s cn63xx;
	struct cvmx_pip_stat1_prtx_s cn63xxp1;
	struct cvmx_pip_stat1_prtx_s cn66xx;
	struct cvmx_pip_stat1_prtx_s cnf71xx;
};

union cvmx_pip_stat2_x {
	uint64_t u64;
	struct cvmx_pip_stat2_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t pkts:32;
		uint64_t raw:32;
#else
		uint64_t raw:32;
		uint64_t pkts:32;
#endif
	} s;
	struct cvmx_pip_stat2_x_s cn68xx;
	struct cvmx_pip_stat2_x_s cn68xxp1;
};

union cvmx_pip_stat2_prtx {
	uint64_t u64;
	struct cvmx_pip_stat2_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t pkts:32;
		uint64_t raw:32;
#else
		uint64_t raw:32;
		uint64_t pkts:32;
#endif
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
	struct cvmx_pip_stat2_prtx_s cn61xx;
	struct cvmx_pip_stat2_prtx_s cn63xx;
	struct cvmx_pip_stat2_prtx_s cn63xxp1;
	struct cvmx_pip_stat2_prtx_s cn66xx;
	struct cvmx_pip_stat2_prtx_s cnf71xx;
};

union cvmx_pip_stat3_x {
	uint64_t u64;
	struct cvmx_pip_stat3_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bcst:32;
		uint64_t mcst:32;
#else
		uint64_t mcst:32;
		uint64_t bcst:32;
#endif
	} s;
	struct cvmx_pip_stat3_x_s cn68xx;
	struct cvmx_pip_stat3_x_s cn68xxp1;
};

union cvmx_pip_stat3_prtx {
	uint64_t u64;
	struct cvmx_pip_stat3_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bcst:32;
		uint64_t mcst:32;
#else
		uint64_t mcst:32;
		uint64_t bcst:32;
#endif
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
	struct cvmx_pip_stat3_prtx_s cn61xx;
	struct cvmx_pip_stat3_prtx_s cn63xx;
	struct cvmx_pip_stat3_prtx_s cn63xxp1;
	struct cvmx_pip_stat3_prtx_s cn66xx;
	struct cvmx_pip_stat3_prtx_s cnf71xx;
};

union cvmx_pip_stat4_x {
	uint64_t u64;
	struct cvmx_pip_stat4_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t h65to127:32;
		uint64_t h64:32;
#else
		uint64_t h64:32;
		uint64_t h65to127:32;
#endif
	} s;
	struct cvmx_pip_stat4_x_s cn68xx;
	struct cvmx_pip_stat4_x_s cn68xxp1;
};

union cvmx_pip_stat4_prtx {
	uint64_t u64;
	struct cvmx_pip_stat4_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t h65to127:32;
		uint64_t h64:32;
#else
		uint64_t h64:32;
		uint64_t h65to127:32;
#endif
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
	struct cvmx_pip_stat4_prtx_s cn61xx;
	struct cvmx_pip_stat4_prtx_s cn63xx;
	struct cvmx_pip_stat4_prtx_s cn63xxp1;
	struct cvmx_pip_stat4_prtx_s cn66xx;
	struct cvmx_pip_stat4_prtx_s cnf71xx;
};

union cvmx_pip_stat5_x {
	uint64_t u64;
	struct cvmx_pip_stat5_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t h256to511:32;
		uint64_t h128to255:32;
#else
		uint64_t h128to255:32;
		uint64_t h256to511:32;
#endif
	} s;
	struct cvmx_pip_stat5_x_s cn68xx;
	struct cvmx_pip_stat5_x_s cn68xxp1;
};

union cvmx_pip_stat5_prtx {
	uint64_t u64;
	struct cvmx_pip_stat5_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t h256to511:32;
		uint64_t h128to255:32;
#else
		uint64_t h128to255:32;
		uint64_t h256to511:32;
#endif
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
	struct cvmx_pip_stat5_prtx_s cn61xx;
	struct cvmx_pip_stat5_prtx_s cn63xx;
	struct cvmx_pip_stat5_prtx_s cn63xxp1;
	struct cvmx_pip_stat5_prtx_s cn66xx;
	struct cvmx_pip_stat5_prtx_s cnf71xx;
};

union cvmx_pip_stat6_x {
	uint64_t u64;
	struct cvmx_pip_stat6_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t h1024to1518:32;
		uint64_t h512to1023:32;
#else
		uint64_t h512to1023:32;
		uint64_t h1024to1518:32;
#endif
	} s;
	struct cvmx_pip_stat6_x_s cn68xx;
	struct cvmx_pip_stat6_x_s cn68xxp1;
};

union cvmx_pip_stat6_prtx {
	uint64_t u64;
	struct cvmx_pip_stat6_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t h1024to1518:32;
		uint64_t h512to1023:32;
#else
		uint64_t h512to1023:32;
		uint64_t h1024to1518:32;
#endif
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
	struct cvmx_pip_stat6_prtx_s cn61xx;
	struct cvmx_pip_stat6_prtx_s cn63xx;
	struct cvmx_pip_stat6_prtx_s cn63xxp1;
	struct cvmx_pip_stat6_prtx_s cn66xx;
	struct cvmx_pip_stat6_prtx_s cnf71xx;
};

union cvmx_pip_stat7_x {
	uint64_t u64;
	struct cvmx_pip_stat7_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t fcs:32;
		uint64_t h1519:32;
#else
		uint64_t h1519:32;
		uint64_t fcs:32;
#endif
	} s;
	struct cvmx_pip_stat7_x_s cn68xx;
	struct cvmx_pip_stat7_x_s cn68xxp1;
};

union cvmx_pip_stat7_prtx {
	uint64_t u64;
	struct cvmx_pip_stat7_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t fcs:32;
		uint64_t h1519:32;
#else
		uint64_t h1519:32;
		uint64_t fcs:32;
#endif
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
	struct cvmx_pip_stat7_prtx_s cn61xx;
	struct cvmx_pip_stat7_prtx_s cn63xx;
	struct cvmx_pip_stat7_prtx_s cn63xxp1;
	struct cvmx_pip_stat7_prtx_s cn66xx;
	struct cvmx_pip_stat7_prtx_s cnf71xx;
};

union cvmx_pip_stat8_x {
	uint64_t u64;
	struct cvmx_pip_stat8_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t frag:32;
		uint64_t undersz:32;
#else
		uint64_t undersz:32;
		uint64_t frag:32;
#endif
	} s;
	struct cvmx_pip_stat8_x_s cn68xx;
	struct cvmx_pip_stat8_x_s cn68xxp1;
};

union cvmx_pip_stat8_prtx {
	uint64_t u64;
	struct cvmx_pip_stat8_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t frag:32;
		uint64_t undersz:32;
#else
		uint64_t undersz:32;
		uint64_t frag:32;
#endif
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
	struct cvmx_pip_stat8_prtx_s cn61xx;
	struct cvmx_pip_stat8_prtx_s cn63xx;
	struct cvmx_pip_stat8_prtx_s cn63xxp1;
	struct cvmx_pip_stat8_prtx_s cn66xx;
	struct cvmx_pip_stat8_prtx_s cnf71xx;
};

union cvmx_pip_stat9_x {
	uint64_t u64;
	struct cvmx_pip_stat9_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t jabber:32;
		uint64_t oversz:32;
#else
		uint64_t oversz:32;
		uint64_t jabber:32;
#endif
	} s;
	struct cvmx_pip_stat9_x_s cn68xx;
	struct cvmx_pip_stat9_x_s cn68xxp1;
};

union cvmx_pip_stat9_prtx {
	uint64_t u64;
	struct cvmx_pip_stat9_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t jabber:32;
		uint64_t oversz:32;
#else
		uint64_t oversz:32;
		uint64_t jabber:32;
#endif
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
	struct cvmx_pip_stat9_prtx_s cn61xx;
	struct cvmx_pip_stat9_prtx_s cn63xx;
	struct cvmx_pip_stat9_prtx_s cn63xxp1;
	struct cvmx_pip_stat9_prtx_s cn66xx;
	struct cvmx_pip_stat9_prtx_s cnf71xx;
};

union cvmx_pip_stat_ctl {
	uint64_t u64;
	struct cvmx_pip_stat_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t mode:1;
		uint64_t reserved_1_7:7;
		uint64_t rdclr:1;
#else
		uint64_t rdclr:1;
		uint64_t reserved_1_7:7;
		uint64_t mode:1;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_pip_stat_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t rdclr:1;
#else
		uint64_t rdclr:1;
		uint64_t reserved_1_63:63;
#endif
	} cn30xx;
	struct cvmx_pip_stat_ctl_cn30xx cn31xx;
	struct cvmx_pip_stat_ctl_cn30xx cn38xx;
	struct cvmx_pip_stat_ctl_cn30xx cn38xxp2;
	struct cvmx_pip_stat_ctl_cn30xx cn50xx;
	struct cvmx_pip_stat_ctl_cn30xx cn52xx;
	struct cvmx_pip_stat_ctl_cn30xx cn52xxp1;
	struct cvmx_pip_stat_ctl_cn30xx cn56xx;
	struct cvmx_pip_stat_ctl_cn30xx cn56xxp1;
	struct cvmx_pip_stat_ctl_cn30xx cn58xx;
	struct cvmx_pip_stat_ctl_cn30xx cn58xxp1;
	struct cvmx_pip_stat_ctl_cn30xx cn61xx;
	struct cvmx_pip_stat_ctl_cn30xx cn63xx;
	struct cvmx_pip_stat_ctl_cn30xx cn63xxp1;
	struct cvmx_pip_stat_ctl_cn30xx cn66xx;
	struct cvmx_pip_stat_ctl_s cn68xx;
	struct cvmx_pip_stat_ctl_s cn68xxp1;
	struct cvmx_pip_stat_ctl_cn30xx cnf71xx;
};

union cvmx_pip_stat_inb_errsx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_errsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t errs:16;
#else
		uint64_t errs:16;
		uint64_t reserved_16_63:48;
#endif
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
	struct cvmx_pip_stat_inb_errsx_s cn61xx;
	struct cvmx_pip_stat_inb_errsx_s cn63xx;
	struct cvmx_pip_stat_inb_errsx_s cn63xxp1;
	struct cvmx_pip_stat_inb_errsx_s cn66xx;
	struct cvmx_pip_stat_inb_errsx_s cnf71xx;
};

union cvmx_pip_stat_inb_errs_pkndx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_errs_pkndx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t errs:16;
#else
		uint64_t errs:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_pip_stat_inb_errs_pkndx_s cn68xx;
	struct cvmx_pip_stat_inb_errs_pkndx_s cn68xxp1;
};

union cvmx_pip_stat_inb_octsx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_octsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t octs:48;
#else
		uint64_t octs:48;
		uint64_t reserved_48_63:16;
#endif
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
	struct cvmx_pip_stat_inb_octsx_s cn61xx;
	struct cvmx_pip_stat_inb_octsx_s cn63xx;
	struct cvmx_pip_stat_inb_octsx_s cn63xxp1;
	struct cvmx_pip_stat_inb_octsx_s cn66xx;
	struct cvmx_pip_stat_inb_octsx_s cnf71xx;
};

union cvmx_pip_stat_inb_octs_pkndx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_octs_pkndx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t octs:48;
#else
		uint64_t octs:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_pip_stat_inb_octs_pkndx_s cn68xx;
	struct cvmx_pip_stat_inb_octs_pkndx_s cn68xxp1;
};

union cvmx_pip_stat_inb_pktsx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_pktsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t pkts:32;
#else
		uint64_t pkts:32;
		uint64_t reserved_32_63:32;
#endif
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
	struct cvmx_pip_stat_inb_pktsx_s cn61xx;
	struct cvmx_pip_stat_inb_pktsx_s cn63xx;
	struct cvmx_pip_stat_inb_pktsx_s cn63xxp1;
	struct cvmx_pip_stat_inb_pktsx_s cn66xx;
	struct cvmx_pip_stat_inb_pktsx_s cnf71xx;
};

union cvmx_pip_stat_inb_pkts_pkndx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_pkts_pkndx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t pkts:32;
#else
		uint64_t pkts:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_pip_stat_inb_pkts_pkndx_s cn68xx;
	struct cvmx_pip_stat_inb_pkts_pkndx_s cn68xxp1;
};

union cvmx_pip_sub_pkind_fcsx {
	uint64_t u64;
	struct cvmx_pip_sub_pkind_fcsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t port_bit:64;
#else
		uint64_t port_bit:64;
#endif
	} s;
	struct cvmx_pip_sub_pkind_fcsx_s cn68xx;
	struct cvmx_pip_sub_pkind_fcsx_s cn68xxp1;
};

union cvmx_pip_tag_incx {
	uint64_t u64;
	struct cvmx_pip_tag_incx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t en:8;
#else
		uint64_t en:8;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_pip_tag_incx_s cn61xx;
	struct cvmx_pip_tag_incx_s cn63xx;
	struct cvmx_pip_tag_incx_s cn63xxp1;
	struct cvmx_pip_tag_incx_s cn66xx;
	struct cvmx_pip_tag_incx_s cn68xx;
	struct cvmx_pip_tag_incx_s cn68xxp1;
	struct cvmx_pip_tag_incx_s cnf71xx;
};

union cvmx_pip_tag_mask {
	uint64_t u64;
	struct cvmx_pip_tag_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t mask:16;
#else
		uint64_t mask:16;
		uint64_t reserved_16_63:48;
#endif
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
	struct cvmx_pip_tag_mask_s cn61xx;
	struct cvmx_pip_tag_mask_s cn63xx;
	struct cvmx_pip_tag_mask_s cn63xxp1;
	struct cvmx_pip_tag_mask_s cn66xx;
	struct cvmx_pip_tag_mask_s cn68xx;
	struct cvmx_pip_tag_mask_s cn68xxp1;
	struct cvmx_pip_tag_mask_s cnf71xx;
};

union cvmx_pip_tag_secret {
	uint64_t u64;
	struct cvmx_pip_tag_secret_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t dst:16;
		uint64_t src:16;
#else
		uint64_t src:16;
		uint64_t dst:16;
		uint64_t reserved_32_63:32;
#endif
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
	struct cvmx_pip_tag_secret_s cn61xx;
	struct cvmx_pip_tag_secret_s cn63xx;
	struct cvmx_pip_tag_secret_s cn63xxp1;
	struct cvmx_pip_tag_secret_s cn66xx;
	struct cvmx_pip_tag_secret_s cn68xx;
	struct cvmx_pip_tag_secret_s cn68xxp1;
	struct cvmx_pip_tag_secret_s cnf71xx;
};

union cvmx_pip_todo_entry {
	uint64_t u64;
	struct cvmx_pip_todo_entry_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t val:1;
		uint64_t reserved_62_62:1;
		uint64_t entry:62;
#else
		uint64_t entry:62;
		uint64_t reserved_62_62:1;
		uint64_t val:1;
#endif
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
	struct cvmx_pip_todo_entry_s cn61xx;
	struct cvmx_pip_todo_entry_s cn63xx;
	struct cvmx_pip_todo_entry_s cn63xxp1;
	struct cvmx_pip_todo_entry_s cn66xx;
	struct cvmx_pip_todo_entry_s cn68xx;
	struct cvmx_pip_todo_entry_s cn68xxp1;
	struct cvmx_pip_todo_entry_s cnf71xx;
};

union cvmx_pip_vlan_etypesx {
	uint64_t u64;
	struct cvmx_pip_vlan_etypesx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t type3:16;
		uint64_t type2:16;
		uint64_t type1:16;
		uint64_t type0:16;
#else
		uint64_t type0:16;
		uint64_t type1:16;
		uint64_t type2:16;
		uint64_t type3:16;
#endif
	} s;
	struct cvmx_pip_vlan_etypesx_s cn61xx;
	struct cvmx_pip_vlan_etypesx_s cn66xx;
	struct cvmx_pip_vlan_etypesx_s cn68xx;
	struct cvmx_pip_vlan_etypesx_s cnf71xx;
};

union cvmx_pip_xstat0_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat0_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t drp_pkts:32;
		uint64_t drp_octs:32;
#else
		uint64_t drp_octs:32;
		uint64_t drp_pkts:32;
#endif
	} s;
	struct cvmx_pip_xstat0_prtx_s cn63xx;
	struct cvmx_pip_xstat0_prtx_s cn63xxp1;
	struct cvmx_pip_xstat0_prtx_s cn66xx;
};

union cvmx_pip_xstat10_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat10_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bcast:32;
		uint64_t mcast:32;
#else
		uint64_t mcast:32;
		uint64_t bcast:32;
#endif
	} s;
	struct cvmx_pip_xstat10_prtx_s cn63xx;
	struct cvmx_pip_xstat10_prtx_s cn63xxp1;
	struct cvmx_pip_xstat10_prtx_s cn66xx;
};

union cvmx_pip_xstat11_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat11_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bcast:32;
		uint64_t mcast:32;
#else
		uint64_t mcast:32;
		uint64_t bcast:32;
#endif
	} s;
	struct cvmx_pip_xstat11_prtx_s cn63xx;
	struct cvmx_pip_xstat11_prtx_s cn63xxp1;
	struct cvmx_pip_xstat11_prtx_s cn66xx;
};

union cvmx_pip_xstat1_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat1_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t octs:48;
#else
		uint64_t octs:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_pip_xstat1_prtx_s cn63xx;
	struct cvmx_pip_xstat1_prtx_s cn63xxp1;
	struct cvmx_pip_xstat1_prtx_s cn66xx;
};

union cvmx_pip_xstat2_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat2_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t pkts:32;
		uint64_t raw:32;
#else
		uint64_t raw:32;
		uint64_t pkts:32;
#endif
	} s;
	struct cvmx_pip_xstat2_prtx_s cn63xx;
	struct cvmx_pip_xstat2_prtx_s cn63xxp1;
	struct cvmx_pip_xstat2_prtx_s cn66xx;
};

union cvmx_pip_xstat3_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat3_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bcst:32;
		uint64_t mcst:32;
#else
		uint64_t mcst:32;
		uint64_t bcst:32;
#endif
	} s;
	struct cvmx_pip_xstat3_prtx_s cn63xx;
	struct cvmx_pip_xstat3_prtx_s cn63xxp1;
	struct cvmx_pip_xstat3_prtx_s cn66xx;
};

union cvmx_pip_xstat4_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat4_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t h65to127:32;
		uint64_t h64:32;
#else
		uint64_t h64:32;
		uint64_t h65to127:32;
#endif
	} s;
	struct cvmx_pip_xstat4_prtx_s cn63xx;
	struct cvmx_pip_xstat4_prtx_s cn63xxp1;
	struct cvmx_pip_xstat4_prtx_s cn66xx;
};

union cvmx_pip_xstat5_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat5_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t h256to511:32;
		uint64_t h128to255:32;
#else
		uint64_t h128to255:32;
		uint64_t h256to511:32;
#endif
	} s;
	struct cvmx_pip_xstat5_prtx_s cn63xx;
	struct cvmx_pip_xstat5_prtx_s cn63xxp1;
	struct cvmx_pip_xstat5_prtx_s cn66xx;
};

union cvmx_pip_xstat6_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat6_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t h1024to1518:32;
		uint64_t h512to1023:32;
#else
		uint64_t h512to1023:32;
		uint64_t h1024to1518:32;
#endif
	} s;
	struct cvmx_pip_xstat6_prtx_s cn63xx;
	struct cvmx_pip_xstat6_prtx_s cn63xxp1;
	struct cvmx_pip_xstat6_prtx_s cn66xx;
};

union cvmx_pip_xstat7_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat7_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t fcs:32;
		uint64_t h1519:32;
#else
		uint64_t h1519:32;
		uint64_t fcs:32;
#endif
	} s;
	struct cvmx_pip_xstat7_prtx_s cn63xx;
	struct cvmx_pip_xstat7_prtx_s cn63xxp1;
	struct cvmx_pip_xstat7_prtx_s cn66xx;
};

union cvmx_pip_xstat8_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat8_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t frag:32;
		uint64_t undersz:32;
#else
		uint64_t undersz:32;
		uint64_t frag:32;
#endif
	} s;
	struct cvmx_pip_xstat8_prtx_s cn63xx;
	struct cvmx_pip_xstat8_prtx_s cn63xxp1;
	struct cvmx_pip_xstat8_prtx_s cn66xx;
};

union cvmx_pip_xstat9_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat9_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t jabber:32;
		uint64_t oversz:32;
#else
		uint64_t oversz:32;
		uint64_t jabber:32;
#endif
	} s;
	struct cvmx_pip_xstat9_prtx_s cn63xx;
	struct cvmx_pip_xstat9_prtx_s cn63xxp1;
	struct cvmx_pip_xstat9_prtx_s cn66xx;
};

#endif
