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

#ifndef __CVMX_SLI_DEFS_H__
#define __CVMX_SLI_DEFS_H__

#define CVMX_SLI_BIST_STATUS (0x0000000000000580ull)
#define CVMX_SLI_CTL_PORTX(offset) (0x0000000000000050ull + ((offset) & 3) * 16)
#define CVMX_SLI_CTL_STATUS (0x0000000000000570ull)
#define CVMX_SLI_DATA_OUT_CNT (0x00000000000005F0ull)
#define CVMX_SLI_DBG_DATA (0x0000000000000310ull)
#define CVMX_SLI_DBG_SELECT (0x0000000000000300ull)
#define CVMX_SLI_DMAX_CNT(offset) (0x0000000000000400ull + ((offset) & 1) * 16)
#define CVMX_SLI_DMAX_INT_LEVEL(offset) (0x00000000000003E0ull + ((offset) & 1) * 16)
#define CVMX_SLI_DMAX_TIM(offset) (0x0000000000000420ull + ((offset) & 1) * 16)
#define CVMX_SLI_INT_ENB_CIU (0x0000000000003CD0ull)
#define CVMX_SLI_INT_ENB_PORTX(offset) (0x0000000000000340ull + ((offset) & 1) * 16)
#define CVMX_SLI_INT_SUM (0x0000000000000330ull)
#define CVMX_SLI_LAST_WIN_RDATA0 (0x0000000000000600ull)
#define CVMX_SLI_LAST_WIN_RDATA1 (0x0000000000000610ull)
#define CVMX_SLI_LAST_WIN_RDATA2 (0x00000000000006C0ull)
#define CVMX_SLI_LAST_WIN_RDATA3 (0x00000000000006D0ull)
#define CVMX_SLI_MAC_CREDIT_CNT (0x0000000000003D70ull)
#define CVMX_SLI_MAC_CREDIT_CNT2 (0x0000000000003E10ull)
#define CVMX_SLI_MAC_NUMBER (0x0000000000003E00ull)
#define CVMX_SLI_MEM_ACCESS_CTL (0x00000000000002F0ull)
#define CVMX_SLI_MEM_ACCESS_SUBIDX(offset) (0x00000000000000E0ull + ((offset) & 31) * 16 - 16*12)
#define CVMX_SLI_MSI_ENB0 (0x0000000000003C50ull)
#define CVMX_SLI_MSI_ENB1 (0x0000000000003C60ull)
#define CVMX_SLI_MSI_ENB2 (0x0000000000003C70ull)
#define CVMX_SLI_MSI_ENB3 (0x0000000000003C80ull)
#define CVMX_SLI_MSI_RCV0 (0x0000000000003C10ull)
#define CVMX_SLI_MSI_RCV1 (0x0000000000003C20ull)
#define CVMX_SLI_MSI_RCV2 (0x0000000000003C30ull)
#define CVMX_SLI_MSI_RCV3 (0x0000000000003C40ull)
#define CVMX_SLI_MSI_RD_MAP (0x0000000000003CA0ull)
#define CVMX_SLI_MSI_W1C_ENB0 (0x0000000000003CF0ull)
#define CVMX_SLI_MSI_W1C_ENB1 (0x0000000000003D00ull)
#define CVMX_SLI_MSI_W1C_ENB2 (0x0000000000003D10ull)
#define CVMX_SLI_MSI_W1C_ENB3 (0x0000000000003D20ull)
#define CVMX_SLI_MSI_W1S_ENB0 (0x0000000000003D30ull)
#define CVMX_SLI_MSI_W1S_ENB1 (0x0000000000003D40ull)
#define CVMX_SLI_MSI_W1S_ENB2 (0x0000000000003D50ull)
#define CVMX_SLI_MSI_W1S_ENB3 (0x0000000000003D60ull)
#define CVMX_SLI_MSI_WR_MAP (0x0000000000003C90ull)
#define CVMX_SLI_PCIE_MSI_RCV (0x0000000000003CB0ull)
#define CVMX_SLI_PCIE_MSI_RCV_B1 (0x0000000000000650ull)
#define CVMX_SLI_PCIE_MSI_RCV_B2 (0x0000000000000660ull)
#define CVMX_SLI_PCIE_MSI_RCV_B3 (0x0000000000000670ull)
#define CVMX_SLI_PKTX_CNTS(offset) (0x0000000000002400ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKTX_INSTR_BADDR(offset) (0x0000000000002800ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKTX_INSTR_BAOFF_DBELL(offset) (0x0000000000002C00ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKTX_INSTR_FIFO_RSIZE(offset) (0x0000000000003000ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKTX_INSTR_HEADER(offset) (0x0000000000003400ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKTX_IN_BP(offset) (0x0000000000003800ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKTX_OUT_SIZE(offset) (0x0000000000000C00ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKTX_SLIST_BADDR(offset) (0x0000000000001400ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKTX_SLIST_BAOFF_DBELL(offset) (0x0000000000001800ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKTX_SLIST_FIFO_RSIZE(offset) (0x0000000000001C00ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKT_CNT_INT (0x0000000000001130ull)
#define CVMX_SLI_PKT_CNT_INT_ENB (0x0000000000001150ull)
#define CVMX_SLI_PKT_CTL (0x0000000000001220ull)
#define CVMX_SLI_PKT_DATA_OUT_ES (0x00000000000010B0ull)
#define CVMX_SLI_PKT_DATA_OUT_NS (0x00000000000010A0ull)
#define CVMX_SLI_PKT_DATA_OUT_ROR (0x0000000000001090ull)
#define CVMX_SLI_PKT_DPADDR (0x0000000000001080ull)
#define CVMX_SLI_PKT_INPUT_CONTROL (0x0000000000001170ull)
#define CVMX_SLI_PKT_INSTR_ENB (0x0000000000001000ull)
#define CVMX_SLI_PKT_INSTR_RD_SIZE (0x00000000000011A0ull)
#define CVMX_SLI_PKT_INSTR_SIZE (0x0000000000001020ull)
#define CVMX_SLI_PKT_INT_LEVELS (0x0000000000001120ull)
#define CVMX_SLI_PKT_IN_BP (0x0000000000001210ull)
#define CVMX_SLI_PKT_IN_DONEX_CNTS(offset) (0x0000000000002000ull + ((offset) & 31) * 16)
#define CVMX_SLI_PKT_IN_INSTR_COUNTS (0x0000000000001200ull)
#define CVMX_SLI_PKT_IN_PCIE_PORT (0x00000000000011B0ull)
#define CVMX_SLI_PKT_IPTR (0x0000000000001070ull)
#define CVMX_SLI_PKT_OUTPUT_WMARK (0x0000000000001180ull)
#define CVMX_SLI_PKT_OUT_BMODE (0x00000000000010D0ull)
#define CVMX_SLI_PKT_OUT_BP_EN (0x0000000000001240ull)
#define CVMX_SLI_PKT_OUT_ENB (0x0000000000001010ull)
#define CVMX_SLI_PKT_PCIE_PORT (0x00000000000010E0ull)
#define CVMX_SLI_PKT_PORT_IN_RST (0x00000000000011F0ull)
#define CVMX_SLI_PKT_SLIST_ES (0x0000000000001050ull)
#define CVMX_SLI_PKT_SLIST_NS (0x0000000000001040ull)
#define CVMX_SLI_PKT_SLIST_ROR (0x0000000000001030ull)
#define CVMX_SLI_PKT_TIME_INT (0x0000000000001140ull)
#define CVMX_SLI_PKT_TIME_INT_ENB (0x0000000000001160ull)
#define CVMX_SLI_PORTX_PKIND(offset) (0x0000000000000800ull + ((offset) & 31) * 16)
#define CVMX_SLI_S2M_PORTX_CTL(offset) (0x0000000000003D80ull + ((offset) & 3) * 16)
#define CVMX_SLI_SCRATCH_1 (0x00000000000003C0ull)
#define CVMX_SLI_SCRATCH_2 (0x00000000000003D0ull)
#define CVMX_SLI_STATE1 (0x0000000000000620ull)
#define CVMX_SLI_STATE2 (0x0000000000000630ull)
#define CVMX_SLI_STATE3 (0x0000000000000640ull)
#define CVMX_SLI_TX_PIPE (0x0000000000001230ull)
#define CVMX_SLI_WINDOW_CTL (0x00000000000002E0ull)
#define CVMX_SLI_WIN_RD_ADDR (0x0000000000000010ull)
#define CVMX_SLI_WIN_RD_DATA (0x0000000000000040ull)
#define CVMX_SLI_WIN_WR_ADDR (0x0000000000000000ull)
#define CVMX_SLI_WIN_WR_DATA (0x0000000000000020ull)
#define CVMX_SLI_WIN_WR_MASK (0x0000000000000030ull)

union cvmx_sli_bist_status {
	uint64_t u64;
	struct cvmx_sli_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ncb_req:1;
		uint64_t n2p0_c:1;
		uint64_t n2p0_o:1;
		uint64_t n2p1_c:1;
		uint64_t n2p1_o:1;
		uint64_t cpl_p0:1;
		uint64_t cpl_p1:1;
		uint64_t reserved_19_24:6;
		uint64_t p2n0_c0:1;
		uint64_t p2n0_c1:1;
		uint64_t p2n0_n:1;
		uint64_t p2n0_p0:1;
		uint64_t p2n0_p1:1;
		uint64_t p2n1_c0:1;
		uint64_t p2n1_c1:1;
		uint64_t p2n1_n:1;
		uint64_t p2n1_p0:1;
		uint64_t p2n1_p1:1;
		uint64_t reserved_6_8:3;
		uint64_t dsi1_1:1;
		uint64_t dsi1_0:1;
		uint64_t dsi0_1:1;
		uint64_t dsi0_0:1;
		uint64_t msi:1;
		uint64_t ncb_cmd:1;
#else
		uint64_t ncb_cmd:1;
		uint64_t msi:1;
		uint64_t dsi0_0:1;
		uint64_t dsi0_1:1;
		uint64_t dsi1_0:1;
		uint64_t dsi1_1:1;
		uint64_t reserved_6_8:3;
		uint64_t p2n1_p1:1;
		uint64_t p2n1_p0:1;
		uint64_t p2n1_n:1;
		uint64_t p2n1_c1:1;
		uint64_t p2n1_c0:1;
		uint64_t p2n0_p1:1;
		uint64_t p2n0_p0:1;
		uint64_t p2n0_n:1;
		uint64_t p2n0_c1:1;
		uint64_t p2n0_c0:1;
		uint64_t reserved_19_24:6;
		uint64_t cpl_p1:1;
		uint64_t cpl_p0:1;
		uint64_t n2p1_o:1;
		uint64_t n2p1_c:1;
		uint64_t n2p0_o:1;
		uint64_t n2p0_c:1;
		uint64_t ncb_req:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_bist_status_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t n2p0_c:1;
		uint64_t n2p0_o:1;
		uint64_t reserved_27_28:2;
		uint64_t cpl_p0:1;
		uint64_t cpl_p1:1;
		uint64_t reserved_19_24:6;
		uint64_t p2n0_c0:1;
		uint64_t p2n0_c1:1;
		uint64_t p2n0_n:1;
		uint64_t p2n0_p0:1;
		uint64_t p2n0_p1:1;
		uint64_t p2n1_c0:1;
		uint64_t p2n1_c1:1;
		uint64_t p2n1_n:1;
		uint64_t p2n1_p0:1;
		uint64_t p2n1_p1:1;
		uint64_t reserved_6_8:3;
		uint64_t dsi1_1:1;
		uint64_t dsi1_0:1;
		uint64_t dsi0_1:1;
		uint64_t dsi0_0:1;
		uint64_t msi:1;
		uint64_t ncb_cmd:1;
#else
		uint64_t ncb_cmd:1;
		uint64_t msi:1;
		uint64_t dsi0_0:1;
		uint64_t dsi0_1:1;
		uint64_t dsi1_0:1;
		uint64_t dsi1_1:1;
		uint64_t reserved_6_8:3;
		uint64_t p2n1_p1:1;
		uint64_t p2n1_p0:1;
		uint64_t p2n1_n:1;
		uint64_t p2n1_c1:1;
		uint64_t p2n1_c0:1;
		uint64_t p2n0_p1:1;
		uint64_t p2n0_p0:1;
		uint64_t p2n0_n:1;
		uint64_t p2n0_c1:1;
		uint64_t p2n0_c0:1;
		uint64_t reserved_19_24:6;
		uint64_t cpl_p1:1;
		uint64_t cpl_p0:1;
		uint64_t reserved_27_28:2;
		uint64_t n2p0_o:1;
		uint64_t n2p0_c:1;
		uint64_t reserved_31_63:33;
#endif
	} cn61xx;
	struct cvmx_sli_bist_status_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t n2p0_c:1;
		uint64_t n2p0_o:1;
		uint64_t n2p1_c:1;
		uint64_t n2p1_o:1;
		uint64_t cpl_p0:1;
		uint64_t cpl_p1:1;
		uint64_t reserved_19_24:6;
		uint64_t p2n0_c0:1;
		uint64_t p2n0_c1:1;
		uint64_t p2n0_n:1;
		uint64_t p2n0_p0:1;
		uint64_t p2n0_p1:1;
		uint64_t p2n1_c0:1;
		uint64_t p2n1_c1:1;
		uint64_t p2n1_n:1;
		uint64_t p2n1_p0:1;
		uint64_t p2n1_p1:1;
		uint64_t reserved_6_8:3;
		uint64_t dsi1_1:1;
		uint64_t dsi1_0:1;
		uint64_t dsi0_1:1;
		uint64_t dsi0_0:1;
		uint64_t msi:1;
		uint64_t ncb_cmd:1;
#else
		uint64_t ncb_cmd:1;
		uint64_t msi:1;
		uint64_t dsi0_0:1;
		uint64_t dsi0_1:1;
		uint64_t dsi1_0:1;
		uint64_t dsi1_1:1;
		uint64_t reserved_6_8:3;
		uint64_t p2n1_p1:1;
		uint64_t p2n1_p0:1;
		uint64_t p2n1_n:1;
		uint64_t p2n1_c1:1;
		uint64_t p2n1_c0:1;
		uint64_t p2n0_p1:1;
		uint64_t p2n0_p0:1;
		uint64_t p2n0_n:1;
		uint64_t p2n0_c1:1;
		uint64_t p2n0_c0:1;
		uint64_t reserved_19_24:6;
		uint64_t cpl_p1:1;
		uint64_t cpl_p0:1;
		uint64_t n2p1_o:1;
		uint64_t n2p1_c:1;
		uint64_t n2p0_o:1;
		uint64_t n2p0_c:1;
		uint64_t reserved_31_63:33;
#endif
	} cn63xx;
	struct cvmx_sli_bist_status_cn63xx cn63xxp1;
	struct cvmx_sli_bist_status_cn61xx cn66xx;
	struct cvmx_sli_bist_status_s cn68xx;
	struct cvmx_sli_bist_status_s cn68xxp1;
	struct cvmx_sli_bist_status_cn61xx cnf71xx;
};

union cvmx_sli_ctl_portx {
	uint64_t u64;
	struct cvmx_sli_ctl_portx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t intd:1;
		uint64_t intc:1;
		uint64_t intb:1;
		uint64_t inta:1;
		uint64_t dis_port:1;
		uint64_t waitl_com:1;
		uint64_t intd_map:2;
		uint64_t intc_map:2;
		uint64_t intb_map:2;
		uint64_t inta_map:2;
		uint64_t ctlp_ro:1;
		uint64_t reserved_6_6:1;
		uint64_t ptlp_ro:1;
		uint64_t reserved_1_4:4;
		uint64_t wait_com:1;
#else
		uint64_t wait_com:1;
		uint64_t reserved_1_4:4;
		uint64_t ptlp_ro:1;
		uint64_t reserved_6_6:1;
		uint64_t ctlp_ro:1;
		uint64_t inta_map:2;
		uint64_t intb_map:2;
		uint64_t intc_map:2;
		uint64_t intd_map:2;
		uint64_t waitl_com:1;
		uint64_t dis_port:1;
		uint64_t inta:1;
		uint64_t intb:1;
		uint64_t intc:1;
		uint64_t intd:1;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_sli_ctl_portx_s cn61xx;
	struct cvmx_sli_ctl_portx_s cn63xx;
	struct cvmx_sli_ctl_portx_s cn63xxp1;
	struct cvmx_sli_ctl_portx_s cn66xx;
	struct cvmx_sli_ctl_portx_s cn68xx;
	struct cvmx_sli_ctl_portx_s cn68xxp1;
	struct cvmx_sli_ctl_portx_s cnf71xx;
};

union cvmx_sli_ctl_status {
	uint64_t u64;
	struct cvmx_sli_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t p1_ntags:6;
		uint64_t p0_ntags:6;
		uint64_t chip_rev:8;
#else
		uint64_t chip_rev:8;
		uint64_t p0_ntags:6;
		uint64_t p1_ntags:6;
		uint64_t reserved_20_63:44;
#endif
	} s;
	struct cvmx_sli_ctl_status_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t p0_ntags:6;
		uint64_t chip_rev:8;
#else
		uint64_t chip_rev:8;
		uint64_t p0_ntags:6;
		uint64_t reserved_14_63:50;
#endif
	} cn61xx;
	struct cvmx_sli_ctl_status_s cn63xx;
	struct cvmx_sli_ctl_status_s cn63xxp1;
	struct cvmx_sli_ctl_status_cn61xx cn66xx;
	struct cvmx_sli_ctl_status_s cn68xx;
	struct cvmx_sli_ctl_status_s cn68xxp1;
	struct cvmx_sli_ctl_status_cn61xx cnf71xx;
};

union cvmx_sli_data_out_cnt {
	uint64_t u64;
	struct cvmx_sli_data_out_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t p1_ucnt:16;
		uint64_t p1_fcnt:6;
		uint64_t p0_ucnt:16;
		uint64_t p0_fcnt:6;
#else
		uint64_t p0_fcnt:6;
		uint64_t p0_ucnt:16;
		uint64_t p1_fcnt:6;
		uint64_t p1_ucnt:16;
		uint64_t reserved_44_63:20;
#endif
	} s;
	struct cvmx_sli_data_out_cnt_s cn61xx;
	struct cvmx_sli_data_out_cnt_s cn63xx;
	struct cvmx_sli_data_out_cnt_s cn63xxp1;
	struct cvmx_sli_data_out_cnt_s cn66xx;
	struct cvmx_sli_data_out_cnt_s cn68xx;
	struct cvmx_sli_data_out_cnt_s cn68xxp1;
	struct cvmx_sli_data_out_cnt_s cnf71xx;
};

union cvmx_sli_dbg_data {
	uint64_t u64;
	struct cvmx_sli_dbg_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t dsel_ext:1;
		uint64_t data:17;
#else
		uint64_t data:17;
		uint64_t dsel_ext:1;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_sli_dbg_data_s cn61xx;
	struct cvmx_sli_dbg_data_s cn63xx;
	struct cvmx_sli_dbg_data_s cn63xxp1;
	struct cvmx_sli_dbg_data_s cn66xx;
	struct cvmx_sli_dbg_data_s cn68xx;
	struct cvmx_sli_dbg_data_s cn68xxp1;
	struct cvmx_sli_dbg_data_s cnf71xx;
};

union cvmx_sli_dbg_select {
	uint64_t u64;
	struct cvmx_sli_dbg_select_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t adbg_sel:1;
		uint64_t dbg_sel:32;
#else
		uint64_t dbg_sel:32;
		uint64_t adbg_sel:1;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_sli_dbg_select_s cn61xx;
	struct cvmx_sli_dbg_select_s cn63xx;
	struct cvmx_sli_dbg_select_s cn63xxp1;
	struct cvmx_sli_dbg_select_s cn66xx;
	struct cvmx_sli_dbg_select_s cn68xx;
	struct cvmx_sli_dbg_select_s cn68xxp1;
	struct cvmx_sli_dbg_select_s cnf71xx;
};

union cvmx_sli_dmax_cnt {
	uint64_t u64;
	struct cvmx_sli_dmax_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_dmax_cnt_s cn61xx;
	struct cvmx_sli_dmax_cnt_s cn63xx;
	struct cvmx_sli_dmax_cnt_s cn63xxp1;
	struct cvmx_sli_dmax_cnt_s cn66xx;
	struct cvmx_sli_dmax_cnt_s cn68xx;
	struct cvmx_sli_dmax_cnt_s cn68xxp1;
	struct cvmx_sli_dmax_cnt_s cnf71xx;
};

union cvmx_sli_dmax_int_level {
	uint64_t u64;
	struct cvmx_sli_dmax_int_level_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t time:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t time:32;
#endif
	} s;
	struct cvmx_sli_dmax_int_level_s cn61xx;
	struct cvmx_sli_dmax_int_level_s cn63xx;
	struct cvmx_sli_dmax_int_level_s cn63xxp1;
	struct cvmx_sli_dmax_int_level_s cn66xx;
	struct cvmx_sli_dmax_int_level_s cn68xx;
	struct cvmx_sli_dmax_int_level_s cn68xxp1;
	struct cvmx_sli_dmax_int_level_s cnf71xx;
};

union cvmx_sli_dmax_tim {
	uint64_t u64;
	struct cvmx_sli_dmax_tim_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t tim:32;
#else
		uint64_t tim:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_dmax_tim_s cn61xx;
	struct cvmx_sli_dmax_tim_s cn63xx;
	struct cvmx_sli_dmax_tim_s cn63xxp1;
	struct cvmx_sli_dmax_tim_s cn66xx;
	struct cvmx_sli_dmax_tim_s cn68xx;
	struct cvmx_sli_dmax_tim_s cn68xxp1;
	struct cvmx_sli_dmax_tim_s cnf71xx;
};

union cvmx_sli_int_enb_ciu {
	uint64_t u64;
	struct cvmx_sli_int_enb_ciu_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t pipe_err:1;
		uint64_t ill_pad:1;
		uint64_t sprt3_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t pin_bp:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_28_31:4;
		uint64_t m3_un_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_up_b0:1;
		uint64_t reserved_18_19:2;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t reserved_18_19:2;
		uint64_t m2_up_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_un_wi:1;
		uint64_t reserved_28_31:4;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt3_err:1;
		uint64_t ill_pad:1;
		uint64_t pipe_err:1;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_sli_int_enb_ciu_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t ill_pad:1;
		uint64_t sprt3_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t pin_bp:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_28_31:4;
		uint64_t m3_un_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_up_b0:1;
		uint64_t reserved_18_19:2;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t reserved_18_19:2;
		uint64_t m2_up_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_un_wi:1;
		uint64_t reserved_28_31:4;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt3_err:1;
		uint64_t ill_pad:1;
		uint64_t reserved_61_63:3;
#endif
	} cn61xx;
	struct cvmx_sli_int_enb_ciu_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t ill_pad:1;
		uint64_t reserved_58_59:2;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t pin_bp:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_18_31:14;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t reserved_18_31:14;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t reserved_58_59:2;
		uint64_t ill_pad:1;
		uint64_t reserved_61_63:3;
#endif
	} cn63xx;
	struct cvmx_sli_int_enb_ciu_cn63xx cn63xxp1;
	struct cvmx_sli_int_enb_ciu_cn61xx cn66xx;
	struct cvmx_sli_int_enb_ciu_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t pipe_err:1;
		uint64_t ill_pad:1;
		uint64_t reserved_58_59:2;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t reserved_51_51:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_18_31:14;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t reserved_18_31:14;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t reserved_51_51:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t reserved_58_59:2;
		uint64_t ill_pad:1;
		uint64_t pipe_err:1;
		uint64_t reserved_62_63:2;
#endif
	} cn68xx;
	struct cvmx_sli_int_enb_ciu_cn68xx cn68xxp1;
	struct cvmx_sli_int_enb_ciu_cn61xx cnf71xx;
};

union cvmx_sli_int_enb_portx {
	uint64_t u64;
	struct cvmx_sli_int_enb_portx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t pipe_err:1;
		uint64_t ill_pad:1;
		uint64_t sprt3_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t pin_bp:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_28_31:4;
		uint64_t m3_un_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_up_b0:1;
		uint64_t mac1_int:1;
		uint64_t mac0_int:1;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t mac0_int:1;
		uint64_t mac1_int:1;
		uint64_t m2_up_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_un_wi:1;
		uint64_t reserved_28_31:4;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt3_err:1;
		uint64_t ill_pad:1;
		uint64_t pipe_err:1;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_sli_int_enb_portx_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t ill_pad:1;
		uint64_t sprt3_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t pin_bp:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_28_31:4;
		uint64_t m3_un_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_up_b0:1;
		uint64_t mac1_int:1;
		uint64_t mac0_int:1;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t mac0_int:1;
		uint64_t mac1_int:1;
		uint64_t m2_up_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_un_wi:1;
		uint64_t reserved_28_31:4;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt3_err:1;
		uint64_t ill_pad:1;
		uint64_t reserved_61_63:3;
#endif
	} cn61xx;
	struct cvmx_sli_int_enb_portx_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t ill_pad:1;
		uint64_t reserved_58_59:2;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t pin_bp:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_20_31:12;
		uint64_t mac1_int:1;
		uint64_t mac0_int:1;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t mac0_int:1;
		uint64_t mac1_int:1;
		uint64_t reserved_20_31:12;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t reserved_58_59:2;
		uint64_t ill_pad:1;
		uint64_t reserved_61_63:3;
#endif
	} cn63xx;
	struct cvmx_sli_int_enb_portx_cn63xx cn63xxp1;
	struct cvmx_sli_int_enb_portx_cn61xx cn66xx;
	struct cvmx_sli_int_enb_portx_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t pipe_err:1;
		uint64_t ill_pad:1;
		uint64_t reserved_58_59:2;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t reserved_51_51:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_20_31:12;
		uint64_t mac1_int:1;
		uint64_t mac0_int:1;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t mac0_int:1;
		uint64_t mac1_int:1;
		uint64_t reserved_20_31:12;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t reserved_51_51:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t reserved_58_59:2;
		uint64_t ill_pad:1;
		uint64_t pipe_err:1;
		uint64_t reserved_62_63:2;
#endif
	} cn68xx;
	struct cvmx_sli_int_enb_portx_cn68xx cn68xxp1;
	struct cvmx_sli_int_enb_portx_cn61xx cnf71xx;
};

union cvmx_sli_int_sum {
	uint64_t u64;
	struct cvmx_sli_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t pipe_err:1;
		uint64_t ill_pad:1;
		uint64_t sprt3_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t pin_bp:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_28_31:4;
		uint64_t m3_un_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_up_b0:1;
		uint64_t mac1_int:1;
		uint64_t mac0_int:1;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t mac0_int:1;
		uint64_t mac1_int:1;
		uint64_t m2_up_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_un_wi:1;
		uint64_t reserved_28_31:4;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt3_err:1;
		uint64_t ill_pad:1;
		uint64_t pipe_err:1;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_sli_int_sum_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t ill_pad:1;
		uint64_t sprt3_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t pin_bp:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_28_31:4;
		uint64_t m3_un_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_up_b0:1;
		uint64_t mac1_int:1;
		uint64_t mac0_int:1;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t mac0_int:1;
		uint64_t mac1_int:1;
		uint64_t m2_up_b0:1;
		uint64_t m2_up_wi:1;
		uint64_t m2_un_b0:1;
		uint64_t m2_un_wi:1;
		uint64_t m3_up_b0:1;
		uint64_t m3_up_wi:1;
		uint64_t m3_un_b0:1;
		uint64_t m3_un_wi:1;
		uint64_t reserved_28_31:4;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t sprt2_err:1;
		uint64_t sprt3_err:1;
		uint64_t ill_pad:1;
		uint64_t reserved_61_63:3;
#endif
	} cn61xx;
	struct cvmx_sli_int_sum_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t ill_pad:1;
		uint64_t reserved_58_59:2;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t pin_bp:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_20_31:12;
		uint64_t mac1_int:1;
		uint64_t mac0_int:1;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t mac0_int:1;
		uint64_t mac1_int:1;
		uint64_t reserved_20_31:12;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t reserved_58_59:2;
		uint64_t ill_pad:1;
		uint64_t reserved_61_63:3;
#endif
	} cn63xx;
	struct cvmx_sli_int_sum_cn63xx cn63xxp1;
	struct cvmx_sli_int_sum_cn61xx cn66xx;
	struct cvmx_sli_int_sum_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t pipe_err:1;
		uint64_t ill_pad:1;
		uint64_t reserved_58_59:2;
		uint64_t sprt1_err:1;
		uint64_t sprt0_err:1;
		uint64_t pins_err:1;
		uint64_t pop_err:1;
		uint64_t pdi_err:1;
		uint64_t pgl_err:1;
		uint64_t reserved_51_51:1;
		uint64_t pout_err:1;
		uint64_t psldbof:1;
		uint64_t pidbof:1;
		uint64_t reserved_38_47:10;
		uint64_t dtime:2;
		uint64_t dcnt:2;
		uint64_t dmafi:2;
		uint64_t reserved_20_31:12;
		uint64_t mac1_int:1;
		uint64_t mac0_int:1;
		uint64_t mio_int1:1;
		uint64_t mio_int0:1;
		uint64_t m1_un_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_up_b0:1;
		uint64_t reserved_6_7:2;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t reserved_1_1:1;
		uint64_t rml_to:1;
#else
		uint64_t rml_to:1;
		uint64_t reserved_1_1:1;
		uint64_t bar0_to:1;
		uint64_t iob2big:1;
		uint64_t pcnt:1;
		uint64_t ptime:1;
		uint64_t reserved_6_7:2;
		uint64_t m0_up_b0:1;
		uint64_t m0_up_wi:1;
		uint64_t m0_un_b0:1;
		uint64_t m0_un_wi:1;
		uint64_t m1_up_b0:1;
		uint64_t m1_up_wi:1;
		uint64_t m1_un_b0:1;
		uint64_t m1_un_wi:1;
		uint64_t mio_int0:1;
		uint64_t mio_int1:1;
		uint64_t mac0_int:1;
		uint64_t mac1_int:1;
		uint64_t reserved_20_31:12;
		uint64_t dmafi:2;
		uint64_t dcnt:2;
		uint64_t dtime:2;
		uint64_t reserved_38_47:10;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t pout_err:1;
		uint64_t reserved_51_51:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t sprt0_err:1;
		uint64_t sprt1_err:1;
		uint64_t reserved_58_59:2;
		uint64_t ill_pad:1;
		uint64_t pipe_err:1;
		uint64_t reserved_62_63:2;
#endif
	} cn68xx;
	struct cvmx_sli_int_sum_cn68xx cn68xxp1;
	struct cvmx_sli_int_sum_cn61xx cnf71xx;
};

union cvmx_sli_last_win_rdata0 {
	uint64_t u64;
	struct cvmx_sli_last_win_rdata0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_sli_last_win_rdata0_s cn61xx;
	struct cvmx_sli_last_win_rdata0_s cn63xx;
	struct cvmx_sli_last_win_rdata0_s cn63xxp1;
	struct cvmx_sli_last_win_rdata0_s cn66xx;
	struct cvmx_sli_last_win_rdata0_s cn68xx;
	struct cvmx_sli_last_win_rdata0_s cn68xxp1;
	struct cvmx_sli_last_win_rdata0_s cnf71xx;
};

union cvmx_sli_last_win_rdata1 {
	uint64_t u64;
	struct cvmx_sli_last_win_rdata1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_sli_last_win_rdata1_s cn61xx;
	struct cvmx_sli_last_win_rdata1_s cn63xx;
	struct cvmx_sli_last_win_rdata1_s cn63xxp1;
	struct cvmx_sli_last_win_rdata1_s cn66xx;
	struct cvmx_sli_last_win_rdata1_s cn68xx;
	struct cvmx_sli_last_win_rdata1_s cn68xxp1;
	struct cvmx_sli_last_win_rdata1_s cnf71xx;
};

union cvmx_sli_last_win_rdata2 {
	uint64_t u64;
	struct cvmx_sli_last_win_rdata2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_sli_last_win_rdata2_s cn61xx;
	struct cvmx_sli_last_win_rdata2_s cn66xx;
	struct cvmx_sli_last_win_rdata2_s cnf71xx;
};

union cvmx_sli_last_win_rdata3 {
	uint64_t u64;
	struct cvmx_sli_last_win_rdata3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_sli_last_win_rdata3_s cn61xx;
	struct cvmx_sli_last_win_rdata3_s cn66xx;
	struct cvmx_sli_last_win_rdata3_s cnf71xx;
};

union cvmx_sli_mac_credit_cnt {
	uint64_t u64;
	struct cvmx_sli_mac_credit_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t p1_c_d:1;
		uint64_t p1_n_d:1;
		uint64_t p1_p_d:1;
		uint64_t p0_c_d:1;
		uint64_t p0_n_d:1;
		uint64_t p0_p_d:1;
		uint64_t p1_ccnt:8;
		uint64_t p1_ncnt:8;
		uint64_t p1_pcnt:8;
		uint64_t p0_ccnt:8;
		uint64_t p0_ncnt:8;
		uint64_t p0_pcnt:8;
#else
		uint64_t p0_pcnt:8;
		uint64_t p0_ncnt:8;
		uint64_t p0_ccnt:8;
		uint64_t p1_pcnt:8;
		uint64_t p1_ncnt:8;
		uint64_t p1_ccnt:8;
		uint64_t p0_p_d:1;
		uint64_t p0_n_d:1;
		uint64_t p0_c_d:1;
		uint64_t p1_p_d:1;
		uint64_t p1_n_d:1;
		uint64_t p1_c_d:1;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_sli_mac_credit_cnt_s cn61xx;
	struct cvmx_sli_mac_credit_cnt_s cn63xx;
	struct cvmx_sli_mac_credit_cnt_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t p1_ccnt:8;
		uint64_t p1_ncnt:8;
		uint64_t p1_pcnt:8;
		uint64_t p0_ccnt:8;
		uint64_t p0_ncnt:8;
		uint64_t p0_pcnt:8;
#else
		uint64_t p0_pcnt:8;
		uint64_t p0_ncnt:8;
		uint64_t p0_ccnt:8;
		uint64_t p1_pcnt:8;
		uint64_t p1_ncnt:8;
		uint64_t p1_ccnt:8;
		uint64_t reserved_48_63:16;
#endif
	} cn63xxp1;
	struct cvmx_sli_mac_credit_cnt_s cn66xx;
	struct cvmx_sli_mac_credit_cnt_s cn68xx;
	struct cvmx_sli_mac_credit_cnt_s cn68xxp1;
	struct cvmx_sli_mac_credit_cnt_s cnf71xx;
};

union cvmx_sli_mac_credit_cnt2 {
	uint64_t u64;
	struct cvmx_sli_mac_credit_cnt2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t p3_c_d:1;
		uint64_t p3_n_d:1;
		uint64_t p3_p_d:1;
		uint64_t p2_c_d:1;
		uint64_t p2_n_d:1;
		uint64_t p2_p_d:1;
		uint64_t p3_ccnt:8;
		uint64_t p3_ncnt:8;
		uint64_t p3_pcnt:8;
		uint64_t p2_ccnt:8;
		uint64_t p2_ncnt:8;
		uint64_t p2_pcnt:8;
#else
		uint64_t p2_pcnt:8;
		uint64_t p2_ncnt:8;
		uint64_t p2_ccnt:8;
		uint64_t p3_pcnt:8;
		uint64_t p3_ncnt:8;
		uint64_t p3_ccnt:8;
		uint64_t p2_p_d:1;
		uint64_t p2_n_d:1;
		uint64_t p2_c_d:1;
		uint64_t p3_p_d:1;
		uint64_t p3_n_d:1;
		uint64_t p3_c_d:1;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_sli_mac_credit_cnt2_s cn61xx;
	struct cvmx_sli_mac_credit_cnt2_s cn66xx;
	struct cvmx_sli_mac_credit_cnt2_s cnf71xx;
};

union cvmx_sli_mac_number {
	uint64_t u64;
	struct cvmx_sli_mac_number_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t a_mode:1;
		uint64_t num:8;
#else
		uint64_t num:8;
		uint64_t a_mode:1;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_sli_mac_number_s cn61xx;
	struct cvmx_sli_mac_number_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t num:8;
#else
		uint64_t num:8;
		uint64_t reserved_8_63:56;
#endif
	} cn63xx;
	struct cvmx_sli_mac_number_s cn66xx;
	struct cvmx_sli_mac_number_cn63xx cn68xx;
	struct cvmx_sli_mac_number_cn63xx cn68xxp1;
	struct cvmx_sli_mac_number_s cnf71xx;
};

union cvmx_sli_mem_access_ctl {
	uint64_t u64;
	struct cvmx_sli_mem_access_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t max_word:4;
		uint64_t timer:10;
#else
		uint64_t timer:10;
		uint64_t max_word:4;
		uint64_t reserved_14_63:50;
#endif
	} s;
	struct cvmx_sli_mem_access_ctl_s cn61xx;
	struct cvmx_sli_mem_access_ctl_s cn63xx;
	struct cvmx_sli_mem_access_ctl_s cn63xxp1;
	struct cvmx_sli_mem_access_ctl_s cn66xx;
	struct cvmx_sli_mem_access_ctl_s cn68xx;
	struct cvmx_sli_mem_access_ctl_s cn68xxp1;
	struct cvmx_sli_mem_access_ctl_s cnf71xx;
};

union cvmx_sli_mem_access_subidx {
	uint64_t u64;
	struct cvmx_sli_mem_access_subidx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_43_63:21;
		uint64_t zero:1;
		uint64_t port:3;
		uint64_t nmerge:1;
		uint64_t esr:2;
		uint64_t esw:2;
		uint64_t wtype:2;
		uint64_t rtype:2;
		uint64_t reserved_0_29:30;
#else
		uint64_t reserved_0_29:30;
		uint64_t rtype:2;
		uint64_t wtype:2;
		uint64_t esw:2;
		uint64_t esr:2;
		uint64_t nmerge:1;
		uint64_t port:3;
		uint64_t zero:1;
		uint64_t reserved_43_63:21;
#endif
	} s;
	struct cvmx_sli_mem_access_subidx_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_43_63:21;
		uint64_t zero:1;
		uint64_t port:3;
		uint64_t nmerge:1;
		uint64_t esr:2;
		uint64_t esw:2;
		uint64_t wtype:2;
		uint64_t rtype:2;
		uint64_t ba:30;
#else
		uint64_t ba:30;
		uint64_t rtype:2;
		uint64_t wtype:2;
		uint64_t esw:2;
		uint64_t esr:2;
		uint64_t nmerge:1;
		uint64_t port:3;
		uint64_t zero:1;
		uint64_t reserved_43_63:21;
#endif
	} cn61xx;
	struct cvmx_sli_mem_access_subidx_cn61xx cn63xx;
	struct cvmx_sli_mem_access_subidx_cn61xx cn63xxp1;
	struct cvmx_sli_mem_access_subidx_cn61xx cn66xx;
	struct cvmx_sli_mem_access_subidx_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_43_63:21;
		uint64_t zero:1;
		uint64_t port:3;
		uint64_t nmerge:1;
		uint64_t esr:2;
		uint64_t esw:2;
		uint64_t wtype:2;
		uint64_t rtype:2;
		uint64_t ba:28;
		uint64_t reserved_0_1:2;
#else
		uint64_t reserved_0_1:2;
		uint64_t ba:28;
		uint64_t rtype:2;
		uint64_t wtype:2;
		uint64_t esw:2;
		uint64_t esr:2;
		uint64_t nmerge:1;
		uint64_t port:3;
		uint64_t zero:1;
		uint64_t reserved_43_63:21;
#endif
	} cn68xx;
	struct cvmx_sli_mem_access_subidx_cn68xx cn68xxp1;
	struct cvmx_sli_mem_access_subidx_cn61xx cnf71xx;
};

union cvmx_sli_msi_enb0 {
	uint64_t u64;
	struct cvmx_sli_msi_enb0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t enb:64;
#else
		uint64_t enb:64;
#endif
	} s;
	struct cvmx_sli_msi_enb0_s cn61xx;
	struct cvmx_sli_msi_enb0_s cn63xx;
	struct cvmx_sli_msi_enb0_s cn63xxp1;
	struct cvmx_sli_msi_enb0_s cn66xx;
	struct cvmx_sli_msi_enb0_s cn68xx;
	struct cvmx_sli_msi_enb0_s cn68xxp1;
	struct cvmx_sli_msi_enb0_s cnf71xx;
};

union cvmx_sli_msi_enb1 {
	uint64_t u64;
	struct cvmx_sli_msi_enb1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t enb:64;
#else
		uint64_t enb:64;
#endif
	} s;
	struct cvmx_sli_msi_enb1_s cn61xx;
	struct cvmx_sli_msi_enb1_s cn63xx;
	struct cvmx_sli_msi_enb1_s cn63xxp1;
	struct cvmx_sli_msi_enb1_s cn66xx;
	struct cvmx_sli_msi_enb1_s cn68xx;
	struct cvmx_sli_msi_enb1_s cn68xxp1;
	struct cvmx_sli_msi_enb1_s cnf71xx;
};

union cvmx_sli_msi_enb2 {
	uint64_t u64;
	struct cvmx_sli_msi_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t enb:64;
#else
		uint64_t enb:64;
#endif
	} s;
	struct cvmx_sli_msi_enb2_s cn61xx;
	struct cvmx_sli_msi_enb2_s cn63xx;
	struct cvmx_sli_msi_enb2_s cn63xxp1;
	struct cvmx_sli_msi_enb2_s cn66xx;
	struct cvmx_sli_msi_enb2_s cn68xx;
	struct cvmx_sli_msi_enb2_s cn68xxp1;
	struct cvmx_sli_msi_enb2_s cnf71xx;
};

union cvmx_sli_msi_enb3 {
	uint64_t u64;
	struct cvmx_sli_msi_enb3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t enb:64;
#else
		uint64_t enb:64;
#endif
	} s;
	struct cvmx_sli_msi_enb3_s cn61xx;
	struct cvmx_sli_msi_enb3_s cn63xx;
	struct cvmx_sli_msi_enb3_s cn63xxp1;
	struct cvmx_sli_msi_enb3_s cn66xx;
	struct cvmx_sli_msi_enb3_s cn68xx;
	struct cvmx_sli_msi_enb3_s cn68xxp1;
	struct cvmx_sli_msi_enb3_s cnf71xx;
};

union cvmx_sli_msi_rcv0 {
	uint64_t u64;
	struct cvmx_sli_msi_rcv0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t intr:64;
#else
		uint64_t intr:64;
#endif
	} s;
	struct cvmx_sli_msi_rcv0_s cn61xx;
	struct cvmx_sli_msi_rcv0_s cn63xx;
	struct cvmx_sli_msi_rcv0_s cn63xxp1;
	struct cvmx_sli_msi_rcv0_s cn66xx;
	struct cvmx_sli_msi_rcv0_s cn68xx;
	struct cvmx_sli_msi_rcv0_s cn68xxp1;
	struct cvmx_sli_msi_rcv0_s cnf71xx;
};

union cvmx_sli_msi_rcv1 {
	uint64_t u64;
	struct cvmx_sli_msi_rcv1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t intr:64;
#else
		uint64_t intr:64;
#endif
	} s;
	struct cvmx_sli_msi_rcv1_s cn61xx;
	struct cvmx_sli_msi_rcv1_s cn63xx;
	struct cvmx_sli_msi_rcv1_s cn63xxp1;
	struct cvmx_sli_msi_rcv1_s cn66xx;
	struct cvmx_sli_msi_rcv1_s cn68xx;
	struct cvmx_sli_msi_rcv1_s cn68xxp1;
	struct cvmx_sli_msi_rcv1_s cnf71xx;
};

union cvmx_sli_msi_rcv2 {
	uint64_t u64;
	struct cvmx_sli_msi_rcv2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t intr:64;
#else
		uint64_t intr:64;
#endif
	} s;
	struct cvmx_sli_msi_rcv2_s cn61xx;
	struct cvmx_sli_msi_rcv2_s cn63xx;
	struct cvmx_sli_msi_rcv2_s cn63xxp1;
	struct cvmx_sli_msi_rcv2_s cn66xx;
	struct cvmx_sli_msi_rcv2_s cn68xx;
	struct cvmx_sli_msi_rcv2_s cn68xxp1;
	struct cvmx_sli_msi_rcv2_s cnf71xx;
};

union cvmx_sli_msi_rcv3 {
	uint64_t u64;
	struct cvmx_sli_msi_rcv3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t intr:64;
#else
		uint64_t intr:64;
#endif
	} s;
	struct cvmx_sli_msi_rcv3_s cn61xx;
	struct cvmx_sli_msi_rcv3_s cn63xx;
	struct cvmx_sli_msi_rcv3_s cn63xxp1;
	struct cvmx_sli_msi_rcv3_s cn66xx;
	struct cvmx_sli_msi_rcv3_s cn68xx;
	struct cvmx_sli_msi_rcv3_s cn68xxp1;
	struct cvmx_sli_msi_rcv3_s cnf71xx;
};

union cvmx_sli_msi_rd_map {
	uint64_t u64;
	struct cvmx_sli_msi_rd_map_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t rd_int:8;
		uint64_t msi_int:8;
#else
		uint64_t msi_int:8;
		uint64_t rd_int:8;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_sli_msi_rd_map_s cn61xx;
	struct cvmx_sli_msi_rd_map_s cn63xx;
	struct cvmx_sli_msi_rd_map_s cn63xxp1;
	struct cvmx_sli_msi_rd_map_s cn66xx;
	struct cvmx_sli_msi_rd_map_s cn68xx;
	struct cvmx_sli_msi_rd_map_s cn68xxp1;
	struct cvmx_sli_msi_rd_map_s cnf71xx;
};

union cvmx_sli_msi_w1c_enb0 {
	uint64_t u64;
	struct cvmx_sli_msi_w1c_enb0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t clr:64;
#else
		uint64_t clr:64;
#endif
	} s;
	struct cvmx_sli_msi_w1c_enb0_s cn61xx;
	struct cvmx_sli_msi_w1c_enb0_s cn63xx;
	struct cvmx_sli_msi_w1c_enb0_s cn63xxp1;
	struct cvmx_sli_msi_w1c_enb0_s cn66xx;
	struct cvmx_sli_msi_w1c_enb0_s cn68xx;
	struct cvmx_sli_msi_w1c_enb0_s cn68xxp1;
	struct cvmx_sli_msi_w1c_enb0_s cnf71xx;
};

union cvmx_sli_msi_w1c_enb1 {
	uint64_t u64;
	struct cvmx_sli_msi_w1c_enb1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t clr:64;
#else
		uint64_t clr:64;
#endif
	} s;
	struct cvmx_sli_msi_w1c_enb1_s cn61xx;
	struct cvmx_sli_msi_w1c_enb1_s cn63xx;
	struct cvmx_sli_msi_w1c_enb1_s cn63xxp1;
	struct cvmx_sli_msi_w1c_enb1_s cn66xx;
	struct cvmx_sli_msi_w1c_enb1_s cn68xx;
	struct cvmx_sli_msi_w1c_enb1_s cn68xxp1;
	struct cvmx_sli_msi_w1c_enb1_s cnf71xx;
};

union cvmx_sli_msi_w1c_enb2 {
	uint64_t u64;
	struct cvmx_sli_msi_w1c_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t clr:64;
#else
		uint64_t clr:64;
#endif
	} s;
	struct cvmx_sli_msi_w1c_enb2_s cn61xx;
	struct cvmx_sli_msi_w1c_enb2_s cn63xx;
	struct cvmx_sli_msi_w1c_enb2_s cn63xxp1;
	struct cvmx_sli_msi_w1c_enb2_s cn66xx;
	struct cvmx_sli_msi_w1c_enb2_s cn68xx;
	struct cvmx_sli_msi_w1c_enb2_s cn68xxp1;
	struct cvmx_sli_msi_w1c_enb2_s cnf71xx;
};

union cvmx_sli_msi_w1c_enb3 {
	uint64_t u64;
	struct cvmx_sli_msi_w1c_enb3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t clr:64;
#else
		uint64_t clr:64;
#endif
	} s;
	struct cvmx_sli_msi_w1c_enb3_s cn61xx;
	struct cvmx_sli_msi_w1c_enb3_s cn63xx;
	struct cvmx_sli_msi_w1c_enb3_s cn63xxp1;
	struct cvmx_sli_msi_w1c_enb3_s cn66xx;
	struct cvmx_sli_msi_w1c_enb3_s cn68xx;
	struct cvmx_sli_msi_w1c_enb3_s cn68xxp1;
	struct cvmx_sli_msi_w1c_enb3_s cnf71xx;
};

union cvmx_sli_msi_w1s_enb0 {
	uint64_t u64;
	struct cvmx_sli_msi_w1s_enb0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t set:64;
#else
		uint64_t set:64;
#endif
	} s;
	struct cvmx_sli_msi_w1s_enb0_s cn61xx;
	struct cvmx_sli_msi_w1s_enb0_s cn63xx;
	struct cvmx_sli_msi_w1s_enb0_s cn63xxp1;
	struct cvmx_sli_msi_w1s_enb0_s cn66xx;
	struct cvmx_sli_msi_w1s_enb0_s cn68xx;
	struct cvmx_sli_msi_w1s_enb0_s cn68xxp1;
	struct cvmx_sli_msi_w1s_enb0_s cnf71xx;
};

union cvmx_sli_msi_w1s_enb1 {
	uint64_t u64;
	struct cvmx_sli_msi_w1s_enb1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t set:64;
#else
		uint64_t set:64;
#endif
	} s;
	struct cvmx_sli_msi_w1s_enb1_s cn61xx;
	struct cvmx_sli_msi_w1s_enb1_s cn63xx;
	struct cvmx_sli_msi_w1s_enb1_s cn63xxp1;
	struct cvmx_sli_msi_w1s_enb1_s cn66xx;
	struct cvmx_sli_msi_w1s_enb1_s cn68xx;
	struct cvmx_sli_msi_w1s_enb1_s cn68xxp1;
	struct cvmx_sli_msi_w1s_enb1_s cnf71xx;
};

union cvmx_sli_msi_w1s_enb2 {
	uint64_t u64;
	struct cvmx_sli_msi_w1s_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t set:64;
#else
		uint64_t set:64;
#endif
	} s;
	struct cvmx_sli_msi_w1s_enb2_s cn61xx;
	struct cvmx_sli_msi_w1s_enb2_s cn63xx;
	struct cvmx_sli_msi_w1s_enb2_s cn63xxp1;
	struct cvmx_sli_msi_w1s_enb2_s cn66xx;
	struct cvmx_sli_msi_w1s_enb2_s cn68xx;
	struct cvmx_sli_msi_w1s_enb2_s cn68xxp1;
	struct cvmx_sli_msi_w1s_enb2_s cnf71xx;
};

union cvmx_sli_msi_w1s_enb3 {
	uint64_t u64;
	struct cvmx_sli_msi_w1s_enb3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t set:64;
#else
		uint64_t set:64;
#endif
	} s;
	struct cvmx_sli_msi_w1s_enb3_s cn61xx;
	struct cvmx_sli_msi_w1s_enb3_s cn63xx;
	struct cvmx_sli_msi_w1s_enb3_s cn63xxp1;
	struct cvmx_sli_msi_w1s_enb3_s cn66xx;
	struct cvmx_sli_msi_w1s_enb3_s cn68xx;
	struct cvmx_sli_msi_w1s_enb3_s cn68xxp1;
	struct cvmx_sli_msi_w1s_enb3_s cnf71xx;
};

union cvmx_sli_msi_wr_map {
	uint64_t u64;
	struct cvmx_sli_msi_wr_map_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t ciu_int:8;
		uint64_t msi_int:8;
#else
		uint64_t msi_int:8;
		uint64_t ciu_int:8;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_sli_msi_wr_map_s cn61xx;
	struct cvmx_sli_msi_wr_map_s cn63xx;
	struct cvmx_sli_msi_wr_map_s cn63xxp1;
	struct cvmx_sli_msi_wr_map_s cn66xx;
	struct cvmx_sli_msi_wr_map_s cn68xx;
	struct cvmx_sli_msi_wr_map_s cn68xxp1;
	struct cvmx_sli_msi_wr_map_s cnf71xx;
};

union cvmx_sli_pcie_msi_rcv {
	uint64_t u64;
	struct cvmx_sli_pcie_msi_rcv_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t intr:8;
#else
		uint64_t intr:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_sli_pcie_msi_rcv_s cn61xx;
	struct cvmx_sli_pcie_msi_rcv_s cn63xx;
	struct cvmx_sli_pcie_msi_rcv_s cn63xxp1;
	struct cvmx_sli_pcie_msi_rcv_s cn66xx;
	struct cvmx_sli_pcie_msi_rcv_s cn68xx;
	struct cvmx_sli_pcie_msi_rcv_s cn68xxp1;
	struct cvmx_sli_pcie_msi_rcv_s cnf71xx;
};

union cvmx_sli_pcie_msi_rcv_b1 {
	uint64_t u64;
	struct cvmx_sli_pcie_msi_rcv_b1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t intr:8;
		uint64_t reserved_0_7:8;
#else
		uint64_t reserved_0_7:8;
		uint64_t intr:8;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_sli_pcie_msi_rcv_b1_s cn61xx;
	struct cvmx_sli_pcie_msi_rcv_b1_s cn63xx;
	struct cvmx_sli_pcie_msi_rcv_b1_s cn63xxp1;
	struct cvmx_sli_pcie_msi_rcv_b1_s cn66xx;
	struct cvmx_sli_pcie_msi_rcv_b1_s cn68xx;
	struct cvmx_sli_pcie_msi_rcv_b1_s cn68xxp1;
	struct cvmx_sli_pcie_msi_rcv_b1_s cnf71xx;
};

union cvmx_sli_pcie_msi_rcv_b2 {
	uint64_t u64;
	struct cvmx_sli_pcie_msi_rcv_b2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t intr:8;
		uint64_t reserved_0_15:16;
#else
		uint64_t reserved_0_15:16;
		uint64_t intr:8;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_sli_pcie_msi_rcv_b2_s cn61xx;
	struct cvmx_sli_pcie_msi_rcv_b2_s cn63xx;
	struct cvmx_sli_pcie_msi_rcv_b2_s cn63xxp1;
	struct cvmx_sli_pcie_msi_rcv_b2_s cn66xx;
	struct cvmx_sli_pcie_msi_rcv_b2_s cn68xx;
	struct cvmx_sli_pcie_msi_rcv_b2_s cn68xxp1;
	struct cvmx_sli_pcie_msi_rcv_b2_s cnf71xx;
};

union cvmx_sli_pcie_msi_rcv_b3 {
	uint64_t u64;
	struct cvmx_sli_pcie_msi_rcv_b3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t intr:8;
		uint64_t reserved_0_23:24;
#else
		uint64_t reserved_0_23:24;
		uint64_t intr:8;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pcie_msi_rcv_b3_s cn61xx;
	struct cvmx_sli_pcie_msi_rcv_b3_s cn63xx;
	struct cvmx_sli_pcie_msi_rcv_b3_s cn63xxp1;
	struct cvmx_sli_pcie_msi_rcv_b3_s cn66xx;
	struct cvmx_sli_pcie_msi_rcv_b3_s cn68xx;
	struct cvmx_sli_pcie_msi_rcv_b3_s cn68xxp1;
	struct cvmx_sli_pcie_msi_rcv_b3_s cnf71xx;
};

union cvmx_sli_pktx_cnts {
	uint64_t u64;
	struct cvmx_sli_pktx_cnts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t timer:22;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t timer:22;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_sli_pktx_cnts_s cn61xx;
	struct cvmx_sli_pktx_cnts_s cn63xx;
	struct cvmx_sli_pktx_cnts_s cn63xxp1;
	struct cvmx_sli_pktx_cnts_s cn66xx;
	struct cvmx_sli_pktx_cnts_s cn68xx;
	struct cvmx_sli_pktx_cnts_s cn68xxp1;
	struct cvmx_sli_pktx_cnts_s cnf71xx;
};

union cvmx_sli_pktx_in_bp {
	uint64_t u64;
	struct cvmx_sli_pktx_in_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t wmark:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t wmark:32;
#endif
	} s;
	struct cvmx_sli_pktx_in_bp_s cn61xx;
	struct cvmx_sli_pktx_in_bp_s cn63xx;
	struct cvmx_sli_pktx_in_bp_s cn63xxp1;
	struct cvmx_sli_pktx_in_bp_s cn66xx;
	struct cvmx_sli_pktx_in_bp_s cnf71xx;
};

union cvmx_sli_pktx_instr_baddr {
	uint64_t u64;
	struct cvmx_sli_pktx_instr_baddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t addr:61;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t addr:61;
#endif
	} s;
	struct cvmx_sli_pktx_instr_baddr_s cn61xx;
	struct cvmx_sli_pktx_instr_baddr_s cn63xx;
	struct cvmx_sli_pktx_instr_baddr_s cn63xxp1;
	struct cvmx_sli_pktx_instr_baddr_s cn66xx;
	struct cvmx_sli_pktx_instr_baddr_s cn68xx;
	struct cvmx_sli_pktx_instr_baddr_s cn68xxp1;
	struct cvmx_sli_pktx_instr_baddr_s cnf71xx;
};

union cvmx_sli_pktx_instr_baoff_dbell {
	uint64_t u64;
	struct cvmx_sli_pktx_instr_baoff_dbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t aoff:32;
		uint64_t dbell:32;
#else
		uint64_t dbell:32;
		uint64_t aoff:32;
#endif
	} s;
	struct cvmx_sli_pktx_instr_baoff_dbell_s cn61xx;
	struct cvmx_sli_pktx_instr_baoff_dbell_s cn63xx;
	struct cvmx_sli_pktx_instr_baoff_dbell_s cn63xxp1;
	struct cvmx_sli_pktx_instr_baoff_dbell_s cn66xx;
	struct cvmx_sli_pktx_instr_baoff_dbell_s cn68xx;
	struct cvmx_sli_pktx_instr_baoff_dbell_s cn68xxp1;
	struct cvmx_sli_pktx_instr_baoff_dbell_s cnf71xx;
};

union cvmx_sli_pktx_instr_fifo_rsize {
	uint64_t u64;
	struct cvmx_sli_pktx_instr_fifo_rsize_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t max:9;
		uint64_t rrp:9;
		uint64_t wrp:9;
		uint64_t fcnt:5;
		uint64_t rsize:32;
#else
		uint64_t rsize:32;
		uint64_t fcnt:5;
		uint64_t wrp:9;
		uint64_t rrp:9;
		uint64_t max:9;
#endif
	} s;
	struct cvmx_sli_pktx_instr_fifo_rsize_s cn61xx;
	struct cvmx_sli_pktx_instr_fifo_rsize_s cn63xx;
	struct cvmx_sli_pktx_instr_fifo_rsize_s cn63xxp1;
	struct cvmx_sli_pktx_instr_fifo_rsize_s cn66xx;
	struct cvmx_sli_pktx_instr_fifo_rsize_s cn68xx;
	struct cvmx_sli_pktx_instr_fifo_rsize_s cn68xxp1;
	struct cvmx_sli_pktx_instr_fifo_rsize_s cnf71xx;
};

union cvmx_sli_pktx_instr_header {
	uint64_t u64;
	struct cvmx_sli_pktx_instr_header_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t pbp:1;
		uint64_t reserved_38_42:5;
		uint64_t rparmode:2;
		uint64_t reserved_35_35:1;
		uint64_t rskp_len:7;
		uint64_t rngrpext:2;
		uint64_t rnqos:1;
		uint64_t rngrp:1;
		uint64_t rntt:1;
		uint64_t rntag:1;
		uint64_t use_ihdr:1;
		uint64_t reserved_16_20:5;
		uint64_t par_mode:2;
		uint64_t reserved_13_13:1;
		uint64_t skp_len:7;
		uint64_t ngrpext:2;
		uint64_t nqos:1;
		uint64_t ngrp:1;
		uint64_t ntt:1;
		uint64_t ntag:1;
#else
		uint64_t ntag:1;
		uint64_t ntt:1;
		uint64_t ngrp:1;
		uint64_t nqos:1;
		uint64_t ngrpext:2;
		uint64_t skp_len:7;
		uint64_t reserved_13_13:1;
		uint64_t par_mode:2;
		uint64_t reserved_16_20:5;
		uint64_t use_ihdr:1;
		uint64_t rntag:1;
		uint64_t rntt:1;
		uint64_t rngrp:1;
		uint64_t rnqos:1;
		uint64_t rngrpext:2;
		uint64_t rskp_len:7;
		uint64_t reserved_35_35:1;
		uint64_t rparmode:2;
		uint64_t reserved_38_42:5;
		uint64_t pbp:1;
		uint64_t reserved_44_63:20;
#endif
	} s;
	struct cvmx_sli_pktx_instr_header_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t pbp:1;
		uint64_t reserved_38_42:5;
		uint64_t rparmode:2;
		uint64_t reserved_35_35:1;
		uint64_t rskp_len:7;
		uint64_t reserved_26_27:2;
		uint64_t rnqos:1;
		uint64_t rngrp:1;
		uint64_t rntt:1;
		uint64_t rntag:1;
		uint64_t use_ihdr:1;
		uint64_t reserved_16_20:5;
		uint64_t par_mode:2;
		uint64_t reserved_13_13:1;
		uint64_t skp_len:7;
		uint64_t reserved_4_5:2;
		uint64_t nqos:1;
		uint64_t ngrp:1;
		uint64_t ntt:1;
		uint64_t ntag:1;
#else
		uint64_t ntag:1;
		uint64_t ntt:1;
		uint64_t ngrp:1;
		uint64_t nqos:1;
		uint64_t reserved_4_5:2;
		uint64_t skp_len:7;
		uint64_t reserved_13_13:1;
		uint64_t par_mode:2;
		uint64_t reserved_16_20:5;
		uint64_t use_ihdr:1;
		uint64_t rntag:1;
		uint64_t rntt:1;
		uint64_t rngrp:1;
		uint64_t rnqos:1;
		uint64_t reserved_26_27:2;
		uint64_t rskp_len:7;
		uint64_t reserved_35_35:1;
		uint64_t rparmode:2;
		uint64_t reserved_38_42:5;
		uint64_t pbp:1;
		uint64_t reserved_44_63:20;
#endif
	} cn61xx;
	struct cvmx_sli_pktx_instr_header_cn61xx cn63xx;
	struct cvmx_sli_pktx_instr_header_cn61xx cn63xxp1;
	struct cvmx_sli_pktx_instr_header_cn61xx cn66xx;
	struct cvmx_sli_pktx_instr_header_s cn68xx;
	struct cvmx_sli_pktx_instr_header_cn61xx cn68xxp1;
	struct cvmx_sli_pktx_instr_header_cn61xx cnf71xx;
};

union cvmx_sli_pktx_out_size {
	uint64_t u64;
	struct cvmx_sli_pktx_out_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t isize:7;
		uint64_t bsize:16;
#else
		uint64_t bsize:16;
		uint64_t isize:7;
		uint64_t reserved_23_63:41;
#endif
	} s;
	struct cvmx_sli_pktx_out_size_s cn61xx;
	struct cvmx_sli_pktx_out_size_s cn63xx;
	struct cvmx_sli_pktx_out_size_s cn63xxp1;
	struct cvmx_sli_pktx_out_size_s cn66xx;
	struct cvmx_sli_pktx_out_size_s cn68xx;
	struct cvmx_sli_pktx_out_size_s cn68xxp1;
	struct cvmx_sli_pktx_out_size_s cnf71xx;
};

union cvmx_sli_pktx_slist_baddr {
	uint64_t u64;
	struct cvmx_sli_pktx_slist_baddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t addr:60;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t addr:60;
#endif
	} s;
	struct cvmx_sli_pktx_slist_baddr_s cn61xx;
	struct cvmx_sli_pktx_slist_baddr_s cn63xx;
	struct cvmx_sli_pktx_slist_baddr_s cn63xxp1;
	struct cvmx_sli_pktx_slist_baddr_s cn66xx;
	struct cvmx_sli_pktx_slist_baddr_s cn68xx;
	struct cvmx_sli_pktx_slist_baddr_s cn68xxp1;
	struct cvmx_sli_pktx_slist_baddr_s cnf71xx;
};

union cvmx_sli_pktx_slist_baoff_dbell {
	uint64_t u64;
	struct cvmx_sli_pktx_slist_baoff_dbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t aoff:32;
		uint64_t dbell:32;
#else
		uint64_t dbell:32;
		uint64_t aoff:32;
#endif
	} s;
	struct cvmx_sli_pktx_slist_baoff_dbell_s cn61xx;
	struct cvmx_sli_pktx_slist_baoff_dbell_s cn63xx;
	struct cvmx_sli_pktx_slist_baoff_dbell_s cn63xxp1;
	struct cvmx_sli_pktx_slist_baoff_dbell_s cn66xx;
	struct cvmx_sli_pktx_slist_baoff_dbell_s cn68xx;
	struct cvmx_sli_pktx_slist_baoff_dbell_s cn68xxp1;
	struct cvmx_sli_pktx_slist_baoff_dbell_s cnf71xx;
};

union cvmx_sli_pktx_slist_fifo_rsize {
	uint64_t u64;
	struct cvmx_sli_pktx_slist_fifo_rsize_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t rsize:32;
#else
		uint64_t rsize:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pktx_slist_fifo_rsize_s cn61xx;
	struct cvmx_sli_pktx_slist_fifo_rsize_s cn63xx;
	struct cvmx_sli_pktx_slist_fifo_rsize_s cn63xxp1;
	struct cvmx_sli_pktx_slist_fifo_rsize_s cn66xx;
	struct cvmx_sli_pktx_slist_fifo_rsize_s cn68xx;
	struct cvmx_sli_pktx_slist_fifo_rsize_s cn68xxp1;
	struct cvmx_sli_pktx_slist_fifo_rsize_s cnf71xx;
};

union cvmx_sli_pkt_cnt_int {
	uint64_t u64;
	struct cvmx_sli_pkt_cnt_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t port:32;
#else
		uint64_t port:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_cnt_int_s cn61xx;
	struct cvmx_sli_pkt_cnt_int_s cn63xx;
	struct cvmx_sli_pkt_cnt_int_s cn63xxp1;
	struct cvmx_sli_pkt_cnt_int_s cn66xx;
	struct cvmx_sli_pkt_cnt_int_s cn68xx;
	struct cvmx_sli_pkt_cnt_int_s cn68xxp1;
	struct cvmx_sli_pkt_cnt_int_s cnf71xx;
};

union cvmx_sli_pkt_cnt_int_enb {
	uint64_t u64;
	struct cvmx_sli_pkt_cnt_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t port:32;
#else
		uint64_t port:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_cnt_int_enb_s cn61xx;
	struct cvmx_sli_pkt_cnt_int_enb_s cn63xx;
	struct cvmx_sli_pkt_cnt_int_enb_s cn63xxp1;
	struct cvmx_sli_pkt_cnt_int_enb_s cn66xx;
	struct cvmx_sli_pkt_cnt_int_enb_s cn68xx;
	struct cvmx_sli_pkt_cnt_int_enb_s cn68xxp1;
	struct cvmx_sli_pkt_cnt_int_enb_s cnf71xx;
};

union cvmx_sli_pkt_ctl {
	uint64_t u64;
	struct cvmx_sli_pkt_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t ring_en:1;
		uint64_t pkt_bp:4;
#else
		uint64_t pkt_bp:4;
		uint64_t ring_en:1;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_sli_pkt_ctl_s cn61xx;
	struct cvmx_sli_pkt_ctl_s cn63xx;
	struct cvmx_sli_pkt_ctl_s cn63xxp1;
	struct cvmx_sli_pkt_ctl_s cn66xx;
	struct cvmx_sli_pkt_ctl_s cn68xx;
	struct cvmx_sli_pkt_ctl_s cn68xxp1;
	struct cvmx_sli_pkt_ctl_s cnf71xx;
};

union cvmx_sli_pkt_data_out_es {
	uint64_t u64;
	struct cvmx_sli_pkt_data_out_es_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t es:64;
#else
		uint64_t es:64;
#endif
	} s;
	struct cvmx_sli_pkt_data_out_es_s cn61xx;
	struct cvmx_sli_pkt_data_out_es_s cn63xx;
	struct cvmx_sli_pkt_data_out_es_s cn63xxp1;
	struct cvmx_sli_pkt_data_out_es_s cn66xx;
	struct cvmx_sli_pkt_data_out_es_s cn68xx;
	struct cvmx_sli_pkt_data_out_es_s cn68xxp1;
	struct cvmx_sli_pkt_data_out_es_s cnf71xx;
};

union cvmx_sli_pkt_data_out_ns {
	uint64_t u64;
	struct cvmx_sli_pkt_data_out_ns_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t nsr:32;
#else
		uint64_t nsr:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_data_out_ns_s cn61xx;
	struct cvmx_sli_pkt_data_out_ns_s cn63xx;
	struct cvmx_sli_pkt_data_out_ns_s cn63xxp1;
	struct cvmx_sli_pkt_data_out_ns_s cn66xx;
	struct cvmx_sli_pkt_data_out_ns_s cn68xx;
	struct cvmx_sli_pkt_data_out_ns_s cn68xxp1;
	struct cvmx_sli_pkt_data_out_ns_s cnf71xx;
};

union cvmx_sli_pkt_data_out_ror {
	uint64_t u64;
	struct cvmx_sli_pkt_data_out_ror_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ror:32;
#else
		uint64_t ror:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_data_out_ror_s cn61xx;
	struct cvmx_sli_pkt_data_out_ror_s cn63xx;
	struct cvmx_sli_pkt_data_out_ror_s cn63xxp1;
	struct cvmx_sli_pkt_data_out_ror_s cn66xx;
	struct cvmx_sli_pkt_data_out_ror_s cn68xx;
	struct cvmx_sli_pkt_data_out_ror_s cn68xxp1;
	struct cvmx_sli_pkt_data_out_ror_s cnf71xx;
};

union cvmx_sli_pkt_dpaddr {
	uint64_t u64;
	struct cvmx_sli_pkt_dpaddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t dptr:32;
#else
		uint64_t dptr:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_dpaddr_s cn61xx;
	struct cvmx_sli_pkt_dpaddr_s cn63xx;
	struct cvmx_sli_pkt_dpaddr_s cn63xxp1;
	struct cvmx_sli_pkt_dpaddr_s cn66xx;
	struct cvmx_sli_pkt_dpaddr_s cn68xx;
	struct cvmx_sli_pkt_dpaddr_s cn68xxp1;
	struct cvmx_sli_pkt_dpaddr_s cnf71xx;
};

union cvmx_sli_pkt_in_bp {
	uint64_t u64;
	struct cvmx_sli_pkt_in_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bp:32;
#else
		uint64_t bp:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_in_bp_s cn61xx;
	struct cvmx_sli_pkt_in_bp_s cn63xx;
	struct cvmx_sli_pkt_in_bp_s cn63xxp1;
	struct cvmx_sli_pkt_in_bp_s cn66xx;
	struct cvmx_sli_pkt_in_bp_s cnf71xx;
};

union cvmx_sli_pkt_in_donex_cnts {
	uint64_t u64;
	struct cvmx_sli_pkt_in_donex_cnts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_in_donex_cnts_s cn61xx;
	struct cvmx_sli_pkt_in_donex_cnts_s cn63xx;
	struct cvmx_sli_pkt_in_donex_cnts_s cn63xxp1;
	struct cvmx_sli_pkt_in_donex_cnts_s cn66xx;
	struct cvmx_sli_pkt_in_donex_cnts_s cn68xx;
	struct cvmx_sli_pkt_in_donex_cnts_s cn68xxp1;
	struct cvmx_sli_pkt_in_donex_cnts_s cnf71xx;
};

union cvmx_sli_pkt_in_instr_counts {
	uint64_t u64;
	struct cvmx_sli_pkt_in_instr_counts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t wr_cnt:32;
		uint64_t rd_cnt:32;
#else
		uint64_t rd_cnt:32;
		uint64_t wr_cnt:32;
#endif
	} s;
	struct cvmx_sli_pkt_in_instr_counts_s cn61xx;
	struct cvmx_sli_pkt_in_instr_counts_s cn63xx;
	struct cvmx_sli_pkt_in_instr_counts_s cn63xxp1;
	struct cvmx_sli_pkt_in_instr_counts_s cn66xx;
	struct cvmx_sli_pkt_in_instr_counts_s cn68xx;
	struct cvmx_sli_pkt_in_instr_counts_s cn68xxp1;
	struct cvmx_sli_pkt_in_instr_counts_s cnf71xx;
};

union cvmx_sli_pkt_in_pcie_port {
	uint64_t u64;
	struct cvmx_sli_pkt_in_pcie_port_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t pp:64;
#else
		uint64_t pp:64;
#endif
	} s;
	struct cvmx_sli_pkt_in_pcie_port_s cn61xx;
	struct cvmx_sli_pkt_in_pcie_port_s cn63xx;
	struct cvmx_sli_pkt_in_pcie_port_s cn63xxp1;
	struct cvmx_sli_pkt_in_pcie_port_s cn66xx;
	struct cvmx_sli_pkt_in_pcie_port_s cn68xx;
	struct cvmx_sli_pkt_in_pcie_port_s cn68xxp1;
	struct cvmx_sli_pkt_in_pcie_port_s cnf71xx;
};

union cvmx_sli_pkt_input_control {
	uint64_t u64;
	struct cvmx_sli_pkt_input_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t prd_erst:1;
		uint64_t prd_rds:7;
		uint64_t gii_erst:1;
		uint64_t gii_rds:7;
		uint64_t reserved_41_47:7;
		uint64_t prc_idle:1;
		uint64_t reserved_24_39:16;
		uint64_t pin_rst:1;
		uint64_t pkt_rr:1;
		uint64_t pbp_dhi:13;
		uint64_t d_nsr:1;
		uint64_t d_esr:2;
		uint64_t d_ror:1;
		uint64_t use_csr:1;
		uint64_t nsr:1;
		uint64_t esr:2;
		uint64_t ror:1;
#else
		uint64_t ror:1;
		uint64_t esr:2;
		uint64_t nsr:1;
		uint64_t use_csr:1;
		uint64_t d_ror:1;
		uint64_t d_esr:2;
		uint64_t d_nsr:1;
		uint64_t pbp_dhi:13;
		uint64_t pkt_rr:1;
		uint64_t pin_rst:1;
		uint64_t reserved_24_39:16;
		uint64_t prc_idle:1;
		uint64_t reserved_41_47:7;
		uint64_t gii_rds:7;
		uint64_t gii_erst:1;
		uint64_t prd_rds:7;
		uint64_t prd_erst:1;
#endif
	} s;
	struct cvmx_sli_pkt_input_control_s cn61xx;
	struct cvmx_sli_pkt_input_control_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t pkt_rr:1;
		uint64_t pbp_dhi:13;
		uint64_t d_nsr:1;
		uint64_t d_esr:2;
		uint64_t d_ror:1;
		uint64_t use_csr:1;
		uint64_t nsr:1;
		uint64_t esr:2;
		uint64_t ror:1;
#else
		uint64_t ror:1;
		uint64_t esr:2;
		uint64_t nsr:1;
		uint64_t use_csr:1;
		uint64_t d_ror:1;
		uint64_t d_esr:2;
		uint64_t d_nsr:1;
		uint64_t pbp_dhi:13;
		uint64_t pkt_rr:1;
		uint64_t reserved_23_63:41;
#endif
	} cn63xx;
	struct cvmx_sli_pkt_input_control_cn63xx cn63xxp1;
	struct cvmx_sli_pkt_input_control_s cn66xx;
	struct cvmx_sli_pkt_input_control_s cn68xx;
	struct cvmx_sli_pkt_input_control_s cn68xxp1;
	struct cvmx_sli_pkt_input_control_s cnf71xx;
};

union cvmx_sli_pkt_instr_enb {
	uint64_t u64;
	struct cvmx_sli_pkt_instr_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t enb:32;
#else
		uint64_t enb:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_instr_enb_s cn61xx;
	struct cvmx_sli_pkt_instr_enb_s cn63xx;
	struct cvmx_sli_pkt_instr_enb_s cn63xxp1;
	struct cvmx_sli_pkt_instr_enb_s cn66xx;
	struct cvmx_sli_pkt_instr_enb_s cn68xx;
	struct cvmx_sli_pkt_instr_enb_s cn68xxp1;
	struct cvmx_sli_pkt_instr_enb_s cnf71xx;
};

union cvmx_sli_pkt_instr_rd_size {
	uint64_t u64;
	struct cvmx_sli_pkt_instr_rd_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rdsize:64;
#else
		uint64_t rdsize:64;
#endif
	} s;
	struct cvmx_sli_pkt_instr_rd_size_s cn61xx;
	struct cvmx_sli_pkt_instr_rd_size_s cn63xx;
	struct cvmx_sli_pkt_instr_rd_size_s cn63xxp1;
	struct cvmx_sli_pkt_instr_rd_size_s cn66xx;
	struct cvmx_sli_pkt_instr_rd_size_s cn68xx;
	struct cvmx_sli_pkt_instr_rd_size_s cn68xxp1;
	struct cvmx_sli_pkt_instr_rd_size_s cnf71xx;
};

union cvmx_sli_pkt_instr_size {
	uint64_t u64;
	struct cvmx_sli_pkt_instr_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t is_64b:32;
#else
		uint64_t is_64b:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_instr_size_s cn61xx;
	struct cvmx_sli_pkt_instr_size_s cn63xx;
	struct cvmx_sli_pkt_instr_size_s cn63xxp1;
	struct cvmx_sli_pkt_instr_size_s cn66xx;
	struct cvmx_sli_pkt_instr_size_s cn68xx;
	struct cvmx_sli_pkt_instr_size_s cn68xxp1;
	struct cvmx_sli_pkt_instr_size_s cnf71xx;
};

union cvmx_sli_pkt_int_levels {
	uint64_t u64;
	struct cvmx_sli_pkt_int_levels_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t time:22;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t time:22;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_sli_pkt_int_levels_s cn61xx;
	struct cvmx_sli_pkt_int_levels_s cn63xx;
	struct cvmx_sli_pkt_int_levels_s cn63xxp1;
	struct cvmx_sli_pkt_int_levels_s cn66xx;
	struct cvmx_sli_pkt_int_levels_s cn68xx;
	struct cvmx_sli_pkt_int_levels_s cn68xxp1;
	struct cvmx_sli_pkt_int_levels_s cnf71xx;
};

union cvmx_sli_pkt_iptr {
	uint64_t u64;
	struct cvmx_sli_pkt_iptr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t iptr:32;
#else
		uint64_t iptr:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_iptr_s cn61xx;
	struct cvmx_sli_pkt_iptr_s cn63xx;
	struct cvmx_sli_pkt_iptr_s cn63xxp1;
	struct cvmx_sli_pkt_iptr_s cn66xx;
	struct cvmx_sli_pkt_iptr_s cn68xx;
	struct cvmx_sli_pkt_iptr_s cn68xxp1;
	struct cvmx_sli_pkt_iptr_s cnf71xx;
};

union cvmx_sli_pkt_out_bmode {
	uint64_t u64;
	struct cvmx_sli_pkt_out_bmode_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bmode:32;
#else
		uint64_t bmode:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_out_bmode_s cn61xx;
	struct cvmx_sli_pkt_out_bmode_s cn63xx;
	struct cvmx_sli_pkt_out_bmode_s cn63xxp1;
	struct cvmx_sli_pkt_out_bmode_s cn66xx;
	struct cvmx_sli_pkt_out_bmode_s cn68xx;
	struct cvmx_sli_pkt_out_bmode_s cn68xxp1;
	struct cvmx_sli_pkt_out_bmode_s cnf71xx;
};

union cvmx_sli_pkt_out_bp_en {
	uint64_t u64;
	struct cvmx_sli_pkt_out_bp_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bp_en:32;
#else
		uint64_t bp_en:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_out_bp_en_s cn68xx;
	struct cvmx_sli_pkt_out_bp_en_s cn68xxp1;
};

union cvmx_sli_pkt_out_enb {
	uint64_t u64;
	struct cvmx_sli_pkt_out_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t enb:32;
#else
		uint64_t enb:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_out_enb_s cn61xx;
	struct cvmx_sli_pkt_out_enb_s cn63xx;
	struct cvmx_sli_pkt_out_enb_s cn63xxp1;
	struct cvmx_sli_pkt_out_enb_s cn66xx;
	struct cvmx_sli_pkt_out_enb_s cn68xx;
	struct cvmx_sli_pkt_out_enb_s cn68xxp1;
	struct cvmx_sli_pkt_out_enb_s cnf71xx;
};

union cvmx_sli_pkt_output_wmark {
	uint64_t u64;
	struct cvmx_sli_pkt_output_wmark_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wmark:32;
#else
		uint64_t wmark:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_output_wmark_s cn61xx;
	struct cvmx_sli_pkt_output_wmark_s cn63xx;
	struct cvmx_sli_pkt_output_wmark_s cn63xxp1;
	struct cvmx_sli_pkt_output_wmark_s cn66xx;
	struct cvmx_sli_pkt_output_wmark_s cn68xx;
	struct cvmx_sli_pkt_output_wmark_s cn68xxp1;
	struct cvmx_sli_pkt_output_wmark_s cnf71xx;
};

union cvmx_sli_pkt_pcie_port {
	uint64_t u64;
	struct cvmx_sli_pkt_pcie_port_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t pp:64;
#else
		uint64_t pp:64;
#endif
	} s;
	struct cvmx_sli_pkt_pcie_port_s cn61xx;
	struct cvmx_sli_pkt_pcie_port_s cn63xx;
	struct cvmx_sli_pkt_pcie_port_s cn63xxp1;
	struct cvmx_sli_pkt_pcie_port_s cn66xx;
	struct cvmx_sli_pkt_pcie_port_s cn68xx;
	struct cvmx_sli_pkt_pcie_port_s cn68xxp1;
	struct cvmx_sli_pkt_pcie_port_s cnf71xx;
};

union cvmx_sli_pkt_port_in_rst {
	uint64_t u64;
	struct cvmx_sli_pkt_port_in_rst_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t in_rst:32;
		uint64_t out_rst:32;
#else
		uint64_t out_rst:32;
		uint64_t in_rst:32;
#endif
	} s;
	struct cvmx_sli_pkt_port_in_rst_s cn61xx;
	struct cvmx_sli_pkt_port_in_rst_s cn63xx;
	struct cvmx_sli_pkt_port_in_rst_s cn63xxp1;
	struct cvmx_sli_pkt_port_in_rst_s cn66xx;
	struct cvmx_sli_pkt_port_in_rst_s cn68xx;
	struct cvmx_sli_pkt_port_in_rst_s cn68xxp1;
	struct cvmx_sli_pkt_port_in_rst_s cnf71xx;
};

union cvmx_sli_pkt_slist_es {
	uint64_t u64;
	struct cvmx_sli_pkt_slist_es_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t es:64;
#else
		uint64_t es:64;
#endif
	} s;
	struct cvmx_sli_pkt_slist_es_s cn61xx;
	struct cvmx_sli_pkt_slist_es_s cn63xx;
	struct cvmx_sli_pkt_slist_es_s cn63xxp1;
	struct cvmx_sli_pkt_slist_es_s cn66xx;
	struct cvmx_sli_pkt_slist_es_s cn68xx;
	struct cvmx_sli_pkt_slist_es_s cn68xxp1;
	struct cvmx_sli_pkt_slist_es_s cnf71xx;
};

union cvmx_sli_pkt_slist_ns {
	uint64_t u64;
	struct cvmx_sli_pkt_slist_ns_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t nsr:32;
#else
		uint64_t nsr:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_slist_ns_s cn61xx;
	struct cvmx_sli_pkt_slist_ns_s cn63xx;
	struct cvmx_sli_pkt_slist_ns_s cn63xxp1;
	struct cvmx_sli_pkt_slist_ns_s cn66xx;
	struct cvmx_sli_pkt_slist_ns_s cn68xx;
	struct cvmx_sli_pkt_slist_ns_s cn68xxp1;
	struct cvmx_sli_pkt_slist_ns_s cnf71xx;
};

union cvmx_sli_pkt_slist_ror {
	uint64_t u64;
	struct cvmx_sli_pkt_slist_ror_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ror:32;
#else
		uint64_t ror:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_slist_ror_s cn61xx;
	struct cvmx_sli_pkt_slist_ror_s cn63xx;
	struct cvmx_sli_pkt_slist_ror_s cn63xxp1;
	struct cvmx_sli_pkt_slist_ror_s cn66xx;
	struct cvmx_sli_pkt_slist_ror_s cn68xx;
	struct cvmx_sli_pkt_slist_ror_s cn68xxp1;
	struct cvmx_sli_pkt_slist_ror_s cnf71xx;
};

union cvmx_sli_pkt_time_int {
	uint64_t u64;
	struct cvmx_sli_pkt_time_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t port:32;
#else
		uint64_t port:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_time_int_s cn61xx;
	struct cvmx_sli_pkt_time_int_s cn63xx;
	struct cvmx_sli_pkt_time_int_s cn63xxp1;
	struct cvmx_sli_pkt_time_int_s cn66xx;
	struct cvmx_sli_pkt_time_int_s cn68xx;
	struct cvmx_sli_pkt_time_int_s cn68xxp1;
	struct cvmx_sli_pkt_time_int_s cnf71xx;
};

union cvmx_sli_pkt_time_int_enb {
	uint64_t u64;
	struct cvmx_sli_pkt_time_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t port:32;
#else
		uint64_t port:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_pkt_time_int_enb_s cn61xx;
	struct cvmx_sli_pkt_time_int_enb_s cn63xx;
	struct cvmx_sli_pkt_time_int_enb_s cn63xxp1;
	struct cvmx_sli_pkt_time_int_enb_s cn66xx;
	struct cvmx_sli_pkt_time_int_enb_s cn68xx;
	struct cvmx_sli_pkt_time_int_enb_s cn68xxp1;
	struct cvmx_sli_pkt_time_int_enb_s cnf71xx;
};

union cvmx_sli_portx_pkind {
	uint64_t u64;
	struct cvmx_sli_portx_pkind_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t rpk_enb:1;
		uint64_t reserved_22_23:2;
		uint64_t pkindr:6;
		uint64_t reserved_14_15:2;
		uint64_t bpkind:6;
		uint64_t reserved_6_7:2;
		uint64_t pkind:6;
#else
		uint64_t pkind:6;
		uint64_t reserved_6_7:2;
		uint64_t bpkind:6;
		uint64_t reserved_14_15:2;
		uint64_t pkindr:6;
		uint64_t reserved_22_23:2;
		uint64_t rpk_enb:1;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_sli_portx_pkind_s cn68xx;
	struct cvmx_sli_portx_pkind_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t bpkind:6;
		uint64_t reserved_6_7:2;
		uint64_t pkind:6;
#else
		uint64_t pkind:6;
		uint64_t reserved_6_7:2;
		uint64_t bpkind:6;
		uint64_t reserved_14_63:50;
#endif
	} cn68xxp1;
};

union cvmx_sli_s2m_portx_ctl {
	uint64_t u64;
	struct cvmx_sli_s2m_portx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t wind_d:1;
		uint64_t bar0_d:1;
		uint64_t mrrs:3;
#else
		uint64_t mrrs:3;
		uint64_t bar0_d:1;
		uint64_t wind_d:1;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_sli_s2m_portx_ctl_s cn61xx;
	struct cvmx_sli_s2m_portx_ctl_s cn63xx;
	struct cvmx_sli_s2m_portx_ctl_s cn63xxp1;
	struct cvmx_sli_s2m_portx_ctl_s cn66xx;
	struct cvmx_sli_s2m_portx_ctl_s cn68xx;
	struct cvmx_sli_s2m_portx_ctl_s cn68xxp1;
	struct cvmx_sli_s2m_portx_ctl_s cnf71xx;
};

union cvmx_sli_scratch_1 {
	uint64_t u64;
	struct cvmx_sli_scratch_1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_sli_scratch_1_s cn61xx;
	struct cvmx_sli_scratch_1_s cn63xx;
	struct cvmx_sli_scratch_1_s cn63xxp1;
	struct cvmx_sli_scratch_1_s cn66xx;
	struct cvmx_sli_scratch_1_s cn68xx;
	struct cvmx_sli_scratch_1_s cn68xxp1;
	struct cvmx_sli_scratch_1_s cnf71xx;
};

union cvmx_sli_scratch_2 {
	uint64_t u64;
	struct cvmx_sli_scratch_2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} s;
	struct cvmx_sli_scratch_2_s cn61xx;
	struct cvmx_sli_scratch_2_s cn63xx;
	struct cvmx_sli_scratch_2_s cn63xxp1;
	struct cvmx_sli_scratch_2_s cn66xx;
	struct cvmx_sli_scratch_2_s cn68xx;
	struct cvmx_sli_scratch_2_s cn68xxp1;
	struct cvmx_sli_scratch_2_s cnf71xx;
};

union cvmx_sli_state1 {
	uint64_t u64;
	struct cvmx_sli_state1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t cpl1:12;
		uint64_t cpl0:12;
		uint64_t arb:1;
		uint64_t csr:39;
#else
		uint64_t csr:39;
		uint64_t arb:1;
		uint64_t cpl0:12;
		uint64_t cpl1:12;
#endif
	} s;
	struct cvmx_sli_state1_s cn61xx;
	struct cvmx_sli_state1_s cn63xx;
	struct cvmx_sli_state1_s cn63xxp1;
	struct cvmx_sli_state1_s cn66xx;
	struct cvmx_sli_state1_s cn68xx;
	struct cvmx_sli_state1_s cn68xxp1;
	struct cvmx_sli_state1_s cnf71xx;
};

union cvmx_sli_state2 {
	uint64_t u64;
	struct cvmx_sli_state2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t nnp1:8;
		uint64_t reserved_47_47:1;
		uint64_t rac:1;
		uint64_t csm1:15;
		uint64_t csm0:15;
		uint64_t nnp0:8;
		uint64_t nnd:8;
#else
		uint64_t nnd:8;
		uint64_t nnp0:8;
		uint64_t csm0:15;
		uint64_t csm1:15;
		uint64_t rac:1;
		uint64_t reserved_47_47:1;
		uint64_t nnp1:8;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_sli_state2_s cn61xx;
	struct cvmx_sli_state2_s cn63xx;
	struct cvmx_sli_state2_s cn63xxp1;
	struct cvmx_sli_state2_s cn66xx;
	struct cvmx_sli_state2_s cn68xx;
	struct cvmx_sli_state2_s cn68xxp1;
	struct cvmx_sli_state2_s cnf71xx;
};

union cvmx_sli_state3 {
	uint64_t u64;
	struct cvmx_sli_state3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t psm1:15;
		uint64_t psm0:15;
		uint64_t nsm1:13;
		uint64_t nsm0:13;
#else
		uint64_t nsm0:13;
		uint64_t nsm1:13;
		uint64_t psm0:15;
		uint64_t psm1:15;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_sli_state3_s cn61xx;
	struct cvmx_sli_state3_s cn63xx;
	struct cvmx_sli_state3_s cn63xxp1;
	struct cvmx_sli_state3_s cn66xx;
	struct cvmx_sli_state3_s cn68xx;
	struct cvmx_sli_state3_s cn68xxp1;
	struct cvmx_sli_state3_s cnf71xx;
};

union cvmx_sli_tx_pipe {
	uint64_t u64;
	struct cvmx_sli_tx_pipe_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t nump:8;
		uint64_t reserved_7_15:9;
		uint64_t base:7;
#else
		uint64_t base:7;
		uint64_t reserved_7_15:9;
		uint64_t nump:8;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_sli_tx_pipe_s cn68xx;
	struct cvmx_sli_tx_pipe_s cn68xxp1;
};

union cvmx_sli_win_rd_addr {
	uint64_t u64;
	struct cvmx_sli_win_rd_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_51_63:13;
		uint64_t ld_cmd:2;
		uint64_t iobit:1;
		uint64_t rd_addr:48;
#else
		uint64_t rd_addr:48;
		uint64_t iobit:1;
		uint64_t ld_cmd:2;
		uint64_t reserved_51_63:13;
#endif
	} s;
	struct cvmx_sli_win_rd_addr_s cn61xx;
	struct cvmx_sli_win_rd_addr_s cn63xx;
	struct cvmx_sli_win_rd_addr_s cn63xxp1;
	struct cvmx_sli_win_rd_addr_s cn66xx;
	struct cvmx_sli_win_rd_addr_s cn68xx;
	struct cvmx_sli_win_rd_addr_s cn68xxp1;
	struct cvmx_sli_win_rd_addr_s cnf71xx;
};

union cvmx_sli_win_rd_data {
	uint64_t u64;
	struct cvmx_sli_win_rd_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rd_data:64;
#else
		uint64_t rd_data:64;
#endif
	} s;
	struct cvmx_sli_win_rd_data_s cn61xx;
	struct cvmx_sli_win_rd_data_s cn63xx;
	struct cvmx_sli_win_rd_data_s cn63xxp1;
	struct cvmx_sli_win_rd_data_s cn66xx;
	struct cvmx_sli_win_rd_data_s cn68xx;
	struct cvmx_sli_win_rd_data_s cn68xxp1;
	struct cvmx_sli_win_rd_data_s cnf71xx;
};

union cvmx_sli_win_wr_addr {
	uint64_t u64;
	struct cvmx_sli_win_wr_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t iobit:1;
		uint64_t wr_addr:45;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t wr_addr:45;
		uint64_t iobit:1;
		uint64_t reserved_49_63:15;
#endif
	} s;
	struct cvmx_sli_win_wr_addr_s cn61xx;
	struct cvmx_sli_win_wr_addr_s cn63xx;
	struct cvmx_sli_win_wr_addr_s cn63xxp1;
	struct cvmx_sli_win_wr_addr_s cn66xx;
	struct cvmx_sli_win_wr_addr_s cn68xx;
	struct cvmx_sli_win_wr_addr_s cn68xxp1;
	struct cvmx_sli_win_wr_addr_s cnf71xx;
};

union cvmx_sli_win_wr_data {
	uint64_t u64;
	struct cvmx_sli_win_wr_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t wr_data:64;
#else
		uint64_t wr_data:64;
#endif
	} s;
	struct cvmx_sli_win_wr_data_s cn61xx;
	struct cvmx_sli_win_wr_data_s cn63xx;
	struct cvmx_sli_win_wr_data_s cn63xxp1;
	struct cvmx_sli_win_wr_data_s cn66xx;
	struct cvmx_sli_win_wr_data_s cn68xx;
	struct cvmx_sli_win_wr_data_s cn68xxp1;
	struct cvmx_sli_win_wr_data_s cnf71xx;
};

union cvmx_sli_win_wr_mask {
	uint64_t u64;
	struct cvmx_sli_win_wr_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t wr_mask:8;
#else
		uint64_t wr_mask:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_sli_win_wr_mask_s cn61xx;
	struct cvmx_sli_win_wr_mask_s cn63xx;
	struct cvmx_sli_win_wr_mask_s cn63xxp1;
	struct cvmx_sli_win_wr_mask_s cn66xx;
	struct cvmx_sli_win_wr_mask_s cn68xx;
	struct cvmx_sli_win_wr_mask_s cn68xxp1;
	struct cvmx_sli_win_wr_mask_s cnf71xx;
};

union cvmx_sli_window_ctl {
	uint64_t u64;
	struct cvmx_sli_window_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t time:32;
#else
		uint64_t time:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sli_window_ctl_s cn61xx;
	struct cvmx_sli_window_ctl_s cn63xx;
	struct cvmx_sli_window_ctl_s cn63xxp1;
	struct cvmx_sli_window_ctl_s cn66xx;
	struct cvmx_sli_window_ctl_s cn68xx;
	struct cvmx_sli_window_ctl_s cn68xxp1;
	struct cvmx_sli_window_ctl_s cnf71xx;
};

#endif
