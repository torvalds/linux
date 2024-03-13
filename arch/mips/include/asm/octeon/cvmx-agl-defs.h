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

#ifndef __CVMX_AGL_DEFS_H__
#define __CVMX_AGL_DEFS_H__

#define CVMX_AGL_GMX_BAD_REG (CVMX_ADD_IO_SEG(0x00011800E0000518ull))
#define CVMX_AGL_GMX_BIST (CVMX_ADD_IO_SEG(0x00011800E0000400ull))
#define CVMX_AGL_GMX_DRV_CTL (CVMX_ADD_IO_SEG(0x00011800E00007F0ull))
#define CVMX_AGL_GMX_INF_MODE (CVMX_ADD_IO_SEG(0x00011800E00007F8ull))
#define CVMX_AGL_GMX_PRTX_CFG(offset) (CVMX_ADD_IO_SEG(0x00011800E0000010ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_ADR_CAM0(offset) (CVMX_ADD_IO_SEG(0x00011800E0000180ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_ADR_CAM1(offset) (CVMX_ADD_IO_SEG(0x00011800E0000188ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_ADR_CAM2(offset) (CVMX_ADD_IO_SEG(0x00011800E0000190ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_ADR_CAM3(offset) (CVMX_ADD_IO_SEG(0x00011800E0000198ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_ADR_CAM4(offset) (CVMX_ADD_IO_SEG(0x00011800E00001A0ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_ADR_CAM5(offset) (CVMX_ADD_IO_SEG(0x00011800E00001A8ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_ADR_CAM_EN(offset) (CVMX_ADD_IO_SEG(0x00011800E0000108ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_ADR_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000100ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_DECISION(offset) (CVMX_ADD_IO_SEG(0x00011800E0000040ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_FRM_CHK(offset) (CVMX_ADD_IO_SEG(0x00011800E0000020ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_FRM_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000018ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_FRM_MAX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000030ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_FRM_MIN(offset) (CVMX_ADD_IO_SEG(0x00011800E0000028ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_IFG(offset) (CVMX_ADD_IO_SEG(0x00011800E0000058ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_INT_EN(offset) (CVMX_ADD_IO_SEG(0x00011800E0000008ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_INT_REG(offset) (CVMX_ADD_IO_SEG(0x00011800E0000000ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_JABBER(offset) (CVMX_ADD_IO_SEG(0x00011800E0000038ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_PAUSE_DROP_TIME(offset) (CVMX_ADD_IO_SEG(0x00011800E0000068ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_RX_INBND(offset) (CVMX_ADD_IO_SEG(0x00011800E0000060ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000050ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_OCTS(offset) (CVMX_ADD_IO_SEG(0x00011800E0000088ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_OCTS_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000098ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_OCTS_DMAC(offset) (CVMX_ADD_IO_SEG(0x00011800E00000A8ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_OCTS_DRP(offset) (CVMX_ADD_IO_SEG(0x00011800E00000B8ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_PKTS(offset) (CVMX_ADD_IO_SEG(0x00011800E0000080ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_PKTS_BAD(offset) (CVMX_ADD_IO_SEG(0x00011800E00000C0ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_PKTS_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000090ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_PKTS_DMAC(offset) (CVMX_ADD_IO_SEG(0x00011800E00000A0ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_STATS_PKTS_DRP(offset) (CVMX_ADD_IO_SEG(0x00011800E00000B0ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RXX_UDD_SKP(offset) (CVMX_ADD_IO_SEG(0x00011800E0000048ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_RX_BP_DROPX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000420ull) + ((offset) & 1) * 8)
#define CVMX_AGL_GMX_RX_BP_OFFX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000460ull) + ((offset) & 1) * 8)
#define CVMX_AGL_GMX_RX_BP_ONX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000440ull) + ((offset) & 1) * 8)
#define CVMX_AGL_GMX_RX_PRT_INFO (CVMX_ADD_IO_SEG(0x00011800E00004E8ull))
#define CVMX_AGL_GMX_RX_TX_STATUS (CVMX_ADD_IO_SEG(0x00011800E00007E8ull))
#define CVMX_AGL_GMX_SMACX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000230ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_STAT_BP (CVMX_ADD_IO_SEG(0x00011800E0000520ull))
#define CVMX_AGL_GMX_TXX_APPEND(offset) (CVMX_ADD_IO_SEG(0x00011800E0000218ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_CLK(offset) (CVMX_ADD_IO_SEG(0x00011800E0000208ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000270ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_MIN_PKT(offset) (CVMX_ADD_IO_SEG(0x00011800E0000240ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_PAUSE_PKT_INTERVAL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000248ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_PAUSE_PKT_TIME(offset) (CVMX_ADD_IO_SEG(0x00011800E0000238ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_PAUSE_TOGO(offset) (CVMX_ADD_IO_SEG(0x00011800E0000258ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_PAUSE_ZERO(offset) (CVMX_ADD_IO_SEG(0x00011800E0000260ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_SOFT_PAUSE(offset) (CVMX_ADD_IO_SEG(0x00011800E0000250ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT0(offset) (CVMX_ADD_IO_SEG(0x00011800E0000280ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT1(offset) (CVMX_ADD_IO_SEG(0x00011800E0000288ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT2(offset) (CVMX_ADD_IO_SEG(0x00011800E0000290ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT3(offset) (CVMX_ADD_IO_SEG(0x00011800E0000298ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT4(offset) (CVMX_ADD_IO_SEG(0x00011800E00002A0ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT5(offset) (CVMX_ADD_IO_SEG(0x00011800E00002A8ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT6(offset) (CVMX_ADD_IO_SEG(0x00011800E00002B0ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT7(offset) (CVMX_ADD_IO_SEG(0x00011800E00002B8ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT8(offset) (CVMX_ADD_IO_SEG(0x00011800E00002C0ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STAT9(offset) (CVMX_ADD_IO_SEG(0x00011800E00002C8ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_STATS_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000268ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TXX_THRESH(offset) (CVMX_ADD_IO_SEG(0x00011800E0000210ull) + ((offset) & 1) * 2048)
#define CVMX_AGL_GMX_TX_BP (CVMX_ADD_IO_SEG(0x00011800E00004D0ull))
#define CVMX_AGL_GMX_TX_COL_ATTEMPT (CVMX_ADD_IO_SEG(0x00011800E0000498ull))
#define CVMX_AGL_GMX_TX_IFG (CVMX_ADD_IO_SEG(0x00011800E0000488ull))
#define CVMX_AGL_GMX_TX_INT_EN (CVMX_ADD_IO_SEG(0x00011800E0000508ull))
#define CVMX_AGL_GMX_TX_INT_REG (CVMX_ADD_IO_SEG(0x00011800E0000500ull))
#define CVMX_AGL_GMX_TX_JAM (CVMX_ADD_IO_SEG(0x00011800E0000490ull))
#define CVMX_AGL_GMX_TX_LFSR (CVMX_ADD_IO_SEG(0x00011800E00004F8ull))
#define CVMX_AGL_GMX_TX_OVR_BP (CVMX_ADD_IO_SEG(0x00011800E00004C8ull))
#define CVMX_AGL_GMX_TX_PAUSE_PKT_DMAC (CVMX_ADD_IO_SEG(0x00011800E00004A0ull))
#define CVMX_AGL_GMX_TX_PAUSE_PKT_TYPE (CVMX_ADD_IO_SEG(0x00011800E00004A8ull))
#define CVMX_AGL_PRTX_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0002000ull) + ((offset) & 1) * 8)

union cvmx_agl_gmx_bad_reg {
	uint64_t u64;
	struct cvmx_agl_gmx_bad_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_38_63:26;
		uint64_t txpsh1:1;
		uint64_t txpop1:1;
		uint64_t ovrflw1:1;
		uint64_t txpsh:1;
		uint64_t txpop:1;
		uint64_t ovrflw:1;
		uint64_t reserved_27_31:5;
		uint64_t statovr:1;
		uint64_t reserved_24_25:2;
		uint64_t loststat:2;
		uint64_t reserved_4_21:18;
		uint64_t out_ovr:2;
		uint64_t reserved_0_1:2;
#else
		uint64_t reserved_0_1:2;
		uint64_t out_ovr:2;
		uint64_t reserved_4_21:18;
		uint64_t loststat:2;
		uint64_t reserved_24_25:2;
		uint64_t statovr:1;
		uint64_t reserved_27_31:5;
		uint64_t ovrflw:1;
		uint64_t txpop:1;
		uint64_t txpsh:1;
		uint64_t ovrflw1:1;
		uint64_t txpop1:1;
		uint64_t txpsh1:1;
		uint64_t reserved_38_63:26;
#endif
	} s;
	struct cvmx_agl_gmx_bad_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_38_63:26;
		uint64_t txpsh1:1;
		uint64_t txpop1:1;
		uint64_t ovrflw1:1;
		uint64_t txpsh:1;
		uint64_t txpop:1;
		uint64_t ovrflw:1;
		uint64_t reserved_27_31:5;
		uint64_t statovr:1;
		uint64_t reserved_23_25:3;
		uint64_t loststat:1;
		uint64_t reserved_4_21:18;
		uint64_t out_ovr:2;
		uint64_t reserved_0_1:2;
#else
		uint64_t reserved_0_1:2;
		uint64_t out_ovr:2;
		uint64_t reserved_4_21:18;
		uint64_t loststat:1;
		uint64_t reserved_23_25:3;
		uint64_t statovr:1;
		uint64_t reserved_27_31:5;
		uint64_t ovrflw:1;
		uint64_t txpop:1;
		uint64_t txpsh:1;
		uint64_t ovrflw1:1;
		uint64_t txpop1:1;
		uint64_t txpsh1:1;
		uint64_t reserved_38_63:26;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_bad_reg_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_35_63:29;
		uint64_t txpsh:1;
		uint64_t txpop:1;
		uint64_t ovrflw:1;
		uint64_t reserved_27_31:5;
		uint64_t statovr:1;
		uint64_t reserved_23_25:3;
		uint64_t loststat:1;
		uint64_t reserved_3_21:19;
		uint64_t out_ovr:1;
		uint64_t reserved_0_1:2;
#else
		uint64_t reserved_0_1:2;
		uint64_t out_ovr:1;
		uint64_t reserved_3_21:19;
		uint64_t loststat:1;
		uint64_t reserved_23_25:3;
		uint64_t statovr:1;
		uint64_t reserved_27_31:5;
		uint64_t ovrflw:1;
		uint64_t txpop:1;
		uint64_t txpsh:1;
		uint64_t reserved_35_63:29;
#endif
	} cn56xx;
};

union cvmx_agl_gmx_bist {
	uint64_t u64;
	struct cvmx_agl_gmx_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t status:25;
#else
		uint64_t status:25;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_agl_gmx_bist_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t status:10;
#else
		uint64_t status:10;
		uint64_t reserved_10_63:54;
#endif
	} cn52xx;
};

union cvmx_agl_gmx_drv_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_drv_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t byp_en1:1;
		uint64_t reserved_45_47:3;
		uint64_t pctl1:5;
		uint64_t reserved_37_39:3;
		uint64_t nctl1:5;
		uint64_t reserved_17_31:15;
		uint64_t byp_en:1;
		uint64_t reserved_13_15:3;
		uint64_t pctl:5;
		uint64_t reserved_5_7:3;
		uint64_t nctl:5;
#else
		uint64_t nctl:5;
		uint64_t reserved_5_7:3;
		uint64_t pctl:5;
		uint64_t reserved_13_15:3;
		uint64_t byp_en:1;
		uint64_t reserved_17_31:15;
		uint64_t nctl1:5;
		uint64_t reserved_37_39:3;
		uint64_t pctl1:5;
		uint64_t reserved_45_47:3;
		uint64_t byp_en1:1;
		uint64_t reserved_49_63:15;
#endif
	} s;
	struct cvmx_agl_gmx_drv_ctl_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t byp_en:1;
		uint64_t reserved_13_15:3;
		uint64_t pctl:5;
		uint64_t reserved_5_7:3;
		uint64_t nctl:5;
#else
		uint64_t nctl:5;
		uint64_t reserved_5_7:3;
		uint64_t pctl:5;
		uint64_t reserved_13_15:3;
		uint64_t byp_en:1;
		uint64_t reserved_17_63:47;
#endif
	} cn56xx;
};

union cvmx_agl_gmx_inf_mode {
	uint64_t u64;
	struct cvmx_agl_gmx_inf_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t en:1;
		uint64_t reserved_0_0:1;
#else
		uint64_t reserved_0_0:1;
		uint64_t en:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
};

union cvmx_agl_gmx_prtx_cfg {
	uint64_t u64;
	struct cvmx_agl_gmx_prtx_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t tx_idle:1;
		uint64_t rx_idle:1;
		uint64_t reserved_9_11:3;
		uint64_t speed_msb:1;
		uint64_t reserved_7_7:1;
		uint64_t burst:1;
		uint64_t tx_en:1;
		uint64_t rx_en:1;
		uint64_t slottime:1;
		uint64_t duplex:1;
		uint64_t speed:1;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t speed:1;
		uint64_t duplex:1;
		uint64_t slottime:1;
		uint64_t rx_en:1;
		uint64_t tx_en:1;
		uint64_t burst:1;
		uint64_t reserved_7_7:1;
		uint64_t speed_msb:1;
		uint64_t reserved_9_11:3;
		uint64_t rx_idle:1;
		uint64_t tx_idle:1;
		uint64_t reserved_14_63:50;
#endif
	} s;
	struct cvmx_agl_gmx_prtx_cfg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t tx_en:1;
		uint64_t rx_en:1;
		uint64_t slottime:1;
		uint64_t duplex:1;
		uint64_t speed:1;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t speed:1;
		uint64_t duplex:1;
		uint64_t slottime:1;
		uint64_t rx_en:1;
		uint64_t tx_en:1;
		uint64_t reserved_6_63:58;
#endif
	} cn52xx;
};

union cvmx_agl_gmx_rxx_adr_cam0 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_adr_cam1 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_adr_cam2 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_adr_cam3 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_adr_cam4 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_adr_cam5 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam5_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_adr_cam_en {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t en:8;
#else
		uint64_t en:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_adr_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t cam_mode:1;
		uint64_t mcst:2;
		uint64_t bcst:1;
#else
		uint64_t bcst:1;
		uint64_t mcst:2;
		uint64_t cam_mode:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_decision {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_decision_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t cnt:5;
#else
		uint64_t cnt:5;
		uint64_t reserved_5_63:59;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_frm_chk {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_frm_chk_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_frm_chk_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t reserved_1_1:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t reserved_1_1:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_63:55;
#endif
	} cn52xx;
};

union cvmx_agl_gmx_rxx_frm_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_frm_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t ptp_mode:1;
		uint64_t reserved_11_11:1;
		uint64_t null_dis:1;
		uint64_t pre_align:1;
		uint64_t pad_len:1;
		uint64_t vlan_len:1;
		uint64_t pre_free:1;
		uint64_t ctl_smac:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_drp:1;
		uint64_t pre_strp:1;
		uint64_t pre_chk:1;
#else
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t pre_align:1;
		uint64_t null_dis:1;
		uint64_t reserved_11_11:1;
		uint64_t ptp_mode:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_frm_ctl_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t pre_align:1;
		uint64_t pad_len:1;
		uint64_t vlan_len:1;
		uint64_t pre_free:1;
		uint64_t ctl_smac:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_drp:1;
		uint64_t pre_strp:1;
		uint64_t pre_chk:1;
#else
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t pre_align:1;
		uint64_t reserved_10_63:54;
#endif
	} cn52xx;
};

union cvmx_agl_gmx_rxx_frm_max {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_frm_max_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t len:16;
#else
		uint64_t len:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_frm_min {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_frm_min_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t len:16;
#else
		uint64_t len:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_ifg {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_ifg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t ifg:4;
#else
		uint64_t ifg:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_int_en {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t pause_drp:1;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t pause_drp:1;
		uint64_t reserved_16_18:3;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t reserved_9_9:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t reserved_1_1:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t reserved_1_1:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
};

union cvmx_agl_gmx_rxx_int_reg {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t pause_drp:1;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t pause_drp:1;
		uint64_t reserved_16_18:3;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t reserved_9_9:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t reserved_1_1:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t reserved_1_1:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
};

union cvmx_agl_gmx_rxx_jabber {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_jabber_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt:16;
#else
		uint64_t cnt:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_pause_drop_time {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t status:16;
#else
		uint64_t status:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_rx_inbnd {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_rx_inbnd_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t duplex:1;
		uint64_t speed:2;
		uint64_t status:1;
#else
		uint64_t status:1;
		uint64_t speed:2;
		uint64_t duplex:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t rd_clr:1;
#else
		uint64_t rd_clr:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_octs {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_octs_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t cnt:48;
#else
		uint64_t cnt:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_octs_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t cnt:48;
#else
		uint64_t cnt:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_octs_dmac {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t cnt:48;
#else
		uint64_t cnt:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_octs_drp {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t cnt:48;
#else
		uint64_t cnt:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_pkts {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_pkts_bad {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_pkts_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_pkts_dmac {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_stats_pkts_drp {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_agl_gmx_rxx_udd_skp {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_udd_skp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t fcssel:1;
		uint64_t reserved_7_7:1;
		uint64_t len:7;
#else
		uint64_t len:7;
		uint64_t reserved_7_7:1;
		uint64_t fcssel:1;
		uint64_t reserved_9_63:55;
#endif
	} s;
};

union cvmx_agl_gmx_rx_bp_dropx {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_bp_dropx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t mark:6;
#else
		uint64_t mark:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
};

union cvmx_agl_gmx_rx_bp_offx {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_bp_offx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t mark:6;
#else
		uint64_t mark:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
};

union cvmx_agl_gmx_rx_bp_onx {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_bp_onx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t mark:9;
#else
		uint64_t mark:9;
		uint64_t reserved_9_63:55;
#endif
	} s;
};

union cvmx_agl_gmx_rx_prt_info {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_prt_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t drop:2;
		uint64_t reserved_2_15:14;
		uint64_t commit:2;
#else
		uint64_t commit:2;
		uint64_t reserved_2_15:14;
		uint64_t drop:2;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_agl_gmx_rx_prt_info_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t drop:1;
		uint64_t reserved_1_15:15;
		uint64_t commit:1;
#else
		uint64_t commit:1;
		uint64_t reserved_1_15:15;
		uint64_t drop:1;
		uint64_t reserved_17_63:47;
#endif
	} cn56xx;
};

union cvmx_agl_gmx_rx_tx_status {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_tx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t tx:2;
		uint64_t reserved_2_3:2;
		uint64_t rx:2;
#else
		uint64_t rx:2;
		uint64_t reserved_2_3:2;
		uint64_t tx:2;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_agl_gmx_rx_tx_status_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t tx:1;
		uint64_t reserved_1_3:3;
		uint64_t rx:1;
#else
		uint64_t rx:1;
		uint64_t reserved_1_3:3;
		uint64_t tx:1;
		uint64_t reserved_5_63:59;
#endif
	} cn56xx;
};

union cvmx_agl_gmx_smacx {
	uint64_t u64;
	struct cvmx_agl_gmx_smacx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t smac:48;
#else
		uint64_t smac:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
};

union cvmx_agl_gmx_stat_bp {
	uint64_t u64;
	struct cvmx_agl_gmx_stat_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t bp:1;
		uint64_t cnt:16;
#else
		uint64_t cnt:16;
		uint64_t bp:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
};

union cvmx_agl_gmx_txx_append {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_append_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t force_fcs:1;
		uint64_t fcs:1;
		uint64_t pad:1;
		uint64_t preamble:1;
#else
		uint64_t preamble:1;
		uint64_t pad:1;
		uint64_t fcs:1;
		uint64_t force_fcs:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

union cvmx_agl_gmx_txx_clk {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_clk_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t clk_cnt:6;
#else
		uint64_t clk_cnt:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
};

union cvmx_agl_gmx_txx_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t xsdef_en:1;
		uint64_t xscol_en:1;
#else
		uint64_t xscol_en:1;
		uint64_t xsdef_en:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
};

union cvmx_agl_gmx_txx_min_pkt {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_min_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t min_size:8;
#else
		uint64_t min_size:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
};

union cvmx_agl_gmx_txx_pause_pkt_interval {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t interval:16;
#else
		uint64_t interval:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_gmx_txx_pause_pkt_time {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t time:16;
#else
		uint64_t time:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_gmx_txx_pause_togo {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_pause_togo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t time:16;
#else
		uint64_t time:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_gmx_txx_pause_zero {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_pause_zero_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t send:1;
#else
		uint64_t send:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
};

union cvmx_agl_gmx_txx_soft_pause {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_soft_pause_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t time:16;
#else
		uint64_t time:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat0 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t xsdef:32;
		uint64_t xscol:32;
#else
		uint64_t xscol:32;
		uint64_t xsdef:32;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat1 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t scol:32;
		uint64_t mcol:32;
#else
		uint64_t mcol:32;
		uint64_t scol:32;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat2 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t octs:48;
#else
		uint64_t octs:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat3 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t pkts:32;
#else
		uint64_t pkts:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat4 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t hist1:32;
		uint64_t hist0:32;
#else
		uint64_t hist0:32;
		uint64_t hist1:32;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat5 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat5_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t hist3:32;
		uint64_t hist2:32;
#else
		uint64_t hist2:32;
		uint64_t hist3:32;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat6 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat6_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t hist5:32;
		uint64_t hist4:32;
#else
		uint64_t hist4:32;
		uint64_t hist5:32;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat7 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat7_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t hist7:32;
		uint64_t hist6:32;
#else
		uint64_t hist6:32;
		uint64_t hist7:32;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat8 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat8_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t mcst:32;
		uint64_t bcst:32;
#else
		uint64_t bcst:32;
		uint64_t mcst:32;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stat9 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat9_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t undflw:32;
		uint64_t ctl:32;
#else
		uint64_t ctl:32;
		uint64_t undflw:32;
#endif
	} s;
};

union cvmx_agl_gmx_txx_stats_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stats_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t rd_clr:1;
#else
		uint64_t rd_clr:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
};

union cvmx_agl_gmx_txx_thresh {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_thresh_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t cnt:6;
#else
		uint64_t cnt:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
};

union cvmx_agl_gmx_tx_bp {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t bp:2;
#else
		uint64_t bp:2;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_agl_gmx_tx_bp_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t bp:1;
#else
		uint64_t bp:1;
		uint64_t reserved_1_63:63;
#endif
	} cn56xx;
};

union cvmx_agl_gmx_tx_col_attempt {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_col_attempt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t limit:5;
#else
		uint64_t limit:5;
		uint64_t reserved_5_63:59;
#endif
	} s;
};

union cvmx_agl_gmx_tx_ifg {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_ifg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t ifg2:4;
		uint64_t ifg1:4;
#else
		uint64_t ifg1:4;
		uint64_t ifg2:4;
		uint64_t reserved_8_63:56;
#endif
	} s;
};

union cvmx_agl_gmx_tx_int_en {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t ptp_lost:2;
		uint64_t reserved_18_19:2;
		uint64_t late_col:2;
		uint64_t reserved_14_15:2;
		uint64_t xsdef:2;
		uint64_t reserved_10_11:2;
		uint64_t xscol:2;
		uint64_t reserved_4_7:4;
		uint64_t undflw:2;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:2;
		uint64_t reserved_4_7:4;
		uint64_t xscol:2;
		uint64_t reserved_10_11:2;
		uint64_t xsdef:2;
		uint64_t reserved_14_15:2;
		uint64_t late_col:2;
		uint64_t reserved_18_19:2;
		uint64_t ptp_lost:2;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_agl_gmx_tx_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t late_col:2;
		uint64_t reserved_14_15:2;
		uint64_t xsdef:2;
		uint64_t reserved_10_11:2;
		uint64_t xscol:2;
		uint64_t reserved_4_7:4;
		uint64_t undflw:2;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:2;
		uint64_t reserved_4_7:4;
		uint64_t xscol:2;
		uint64_t reserved_10_11:2;
		uint64_t xsdef:2;
		uint64_t reserved_14_15:2;
		uint64_t late_col:2;
		uint64_t reserved_18_63:46;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_tx_int_en_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t late_col:1;
		uint64_t reserved_13_15:3;
		uint64_t xsdef:1;
		uint64_t reserved_9_11:3;
		uint64_t xscol:1;
		uint64_t reserved_3_7:5;
		uint64_t undflw:1;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:1;
		uint64_t reserved_3_7:5;
		uint64_t xscol:1;
		uint64_t reserved_9_11:3;
		uint64_t xsdef:1;
		uint64_t reserved_13_15:3;
		uint64_t late_col:1;
		uint64_t reserved_17_63:47;
#endif
	} cn56xx;
};

union cvmx_agl_gmx_tx_int_reg {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t ptp_lost:2;
		uint64_t reserved_18_19:2;
		uint64_t late_col:2;
		uint64_t reserved_14_15:2;
		uint64_t xsdef:2;
		uint64_t reserved_10_11:2;
		uint64_t xscol:2;
		uint64_t reserved_4_7:4;
		uint64_t undflw:2;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:2;
		uint64_t reserved_4_7:4;
		uint64_t xscol:2;
		uint64_t reserved_10_11:2;
		uint64_t xsdef:2;
		uint64_t reserved_14_15:2;
		uint64_t late_col:2;
		uint64_t reserved_18_19:2;
		uint64_t ptp_lost:2;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_agl_gmx_tx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t late_col:2;
		uint64_t reserved_14_15:2;
		uint64_t xsdef:2;
		uint64_t reserved_10_11:2;
		uint64_t xscol:2;
		uint64_t reserved_4_7:4;
		uint64_t undflw:2;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:2;
		uint64_t reserved_4_7:4;
		uint64_t xscol:2;
		uint64_t reserved_10_11:2;
		uint64_t xsdef:2;
		uint64_t reserved_14_15:2;
		uint64_t late_col:2;
		uint64_t reserved_18_63:46;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_tx_int_reg_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t late_col:1;
		uint64_t reserved_13_15:3;
		uint64_t xsdef:1;
		uint64_t reserved_9_11:3;
		uint64_t xscol:1;
		uint64_t reserved_3_7:5;
		uint64_t undflw:1;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:1;
		uint64_t reserved_3_7:5;
		uint64_t xscol:1;
		uint64_t reserved_9_11:3;
		uint64_t xsdef:1;
		uint64_t reserved_13_15:3;
		uint64_t late_col:1;
		uint64_t reserved_17_63:47;
#endif
	} cn56xx;
};

union cvmx_agl_gmx_tx_jam {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_jam_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t jam:8;
#else
		uint64_t jam:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
};

union cvmx_agl_gmx_tx_lfsr {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_lfsr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t lfsr:16;
#else
		uint64_t lfsr:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_gmx_tx_ovr_bp {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_ovr_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t en:2;
		uint64_t reserved_6_7:2;
		uint64_t bp:2;
		uint64_t reserved_2_3:2;
		uint64_t ign_full:2;
#else
		uint64_t ign_full:2;
		uint64_t reserved_2_3:2;
		uint64_t bp:2;
		uint64_t reserved_6_7:2;
		uint64_t en:2;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_agl_gmx_tx_ovr_bp_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t en:1;
		uint64_t reserved_5_7:3;
		uint64_t bp:1;
		uint64_t reserved_1_3:3;
		uint64_t ign_full:1;
#else
		uint64_t ign_full:1;
		uint64_t reserved_1_3:3;
		uint64_t bp:1;
		uint64_t reserved_5_7:3;
		uint64_t en:1;
		uint64_t reserved_9_63:55;
#endif
	} cn56xx;
};

union cvmx_agl_gmx_tx_pause_pkt_dmac {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t dmac:48;
#else
		uint64_t dmac:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
};

union cvmx_agl_gmx_tx_pause_pkt_type {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t type:16;
#else
		uint64_t type:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_agl_prtx_ctl {
	uint64_t u64;
	struct cvmx_agl_prtx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t drv_byp:1;
		uint64_t reserved_62_62:1;
		uint64_t cmp_pctl:6;
		uint64_t reserved_54_55:2;
		uint64_t cmp_nctl:6;
		uint64_t reserved_46_47:2;
		uint64_t drv_pctl:6;
		uint64_t reserved_38_39:2;
		uint64_t drv_nctl:6;
		uint64_t reserved_29_31:3;
		uint64_t clk_set:5;
		uint64_t clkrx_byp:1;
		uint64_t reserved_21_22:2;
		uint64_t clkrx_set:5;
		uint64_t clktx_byp:1;
		uint64_t reserved_13_14:2;
		uint64_t clktx_set:5;
		uint64_t reserved_5_7:3;
		uint64_t dllrst:1;
		uint64_t comp:1;
		uint64_t enable:1;
		uint64_t clkrst:1;
		uint64_t mode:1;
#else
		uint64_t mode:1;
		uint64_t clkrst:1;
		uint64_t enable:1;
		uint64_t comp:1;
		uint64_t dllrst:1;
		uint64_t reserved_5_7:3;
		uint64_t clktx_set:5;
		uint64_t reserved_13_14:2;
		uint64_t clktx_byp:1;
		uint64_t clkrx_set:5;
		uint64_t reserved_21_22:2;
		uint64_t clkrx_byp:1;
		uint64_t clk_set:5;
		uint64_t reserved_29_31:3;
		uint64_t drv_nctl:6;
		uint64_t reserved_38_39:2;
		uint64_t drv_pctl:6;
		uint64_t reserved_46_47:2;
		uint64_t cmp_nctl:6;
		uint64_t reserved_54_55:2;
		uint64_t cmp_pctl:6;
		uint64_t reserved_62_62:1;
		uint64_t drv_byp:1;
#endif
	} s;
};

#endif
