/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2010 Cavium Networks
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

#ifndef __CVMX_NPEI_DEFS_H__
#define __CVMX_NPEI_DEFS_H__

#define CVMX_NPEI_BAR1_INDEXX(offset) (0x0000000000000000ull + ((offset) & 31) * 16)
#define CVMX_NPEI_BIST_STATUS (0x0000000000000580ull)
#define CVMX_NPEI_BIST_STATUS2 (0x0000000000000680ull)
#define CVMX_NPEI_CTL_PORT0 (0x0000000000000250ull)
#define CVMX_NPEI_CTL_PORT1 (0x0000000000000260ull)
#define CVMX_NPEI_CTL_STATUS (0x0000000000000570ull)
#define CVMX_NPEI_CTL_STATUS2 (0x0000000000003C00ull)
#define CVMX_NPEI_DATA_OUT_CNT (0x00000000000005F0ull)
#define CVMX_NPEI_DBG_DATA (0x0000000000000510ull)
#define CVMX_NPEI_DBG_SELECT (0x0000000000000500ull)
#define CVMX_NPEI_DMA0_INT_LEVEL (0x00000000000005C0ull)
#define CVMX_NPEI_DMA1_INT_LEVEL (0x00000000000005D0ull)
#define CVMX_NPEI_DMAX_COUNTS(offset) (0x0000000000000450ull + ((offset) & 7) * 16)
#define CVMX_NPEI_DMAX_DBELL(offset) (0x00000000000003B0ull + ((offset) & 7) * 16)
#define CVMX_NPEI_DMAX_IBUFF_SADDR(offset) (0x0000000000000400ull + ((offset) & 7) * 16)
#define CVMX_NPEI_DMAX_NADDR(offset) (0x00000000000004A0ull + ((offset) & 7) * 16)
#define CVMX_NPEI_DMA_CNTS (0x00000000000005E0ull)
#define CVMX_NPEI_DMA_CONTROL (0x00000000000003A0ull)
#define CVMX_NPEI_DMA_PCIE_REQ_NUM (0x00000000000005B0ull)
#define CVMX_NPEI_DMA_STATE1 (0x00000000000006C0ull)
#define CVMX_NPEI_DMA_STATE1_P1 (0x0000000000000680ull)
#define CVMX_NPEI_DMA_STATE2 (0x00000000000006D0ull)
#define CVMX_NPEI_DMA_STATE2_P1 (0x0000000000000690ull)
#define CVMX_NPEI_DMA_STATE3_P1 (0x00000000000006A0ull)
#define CVMX_NPEI_DMA_STATE4_P1 (0x00000000000006B0ull)
#define CVMX_NPEI_DMA_STATE5_P1 (0x00000000000006C0ull)
#define CVMX_NPEI_INT_A_ENB (0x0000000000000560ull)
#define CVMX_NPEI_INT_A_ENB2 (0x0000000000003CE0ull)
#define CVMX_NPEI_INT_A_SUM (0x0000000000000550ull)
#define CVMX_NPEI_INT_ENB (0x0000000000000540ull)
#define CVMX_NPEI_INT_ENB2 (0x0000000000003CD0ull)
#define CVMX_NPEI_INT_INFO (0x0000000000000590ull)
#define CVMX_NPEI_INT_SUM (0x0000000000000530ull)
#define CVMX_NPEI_INT_SUM2 (0x0000000000003CC0ull)
#define CVMX_NPEI_LAST_WIN_RDATA0 (0x0000000000000600ull)
#define CVMX_NPEI_LAST_WIN_RDATA1 (0x0000000000000610ull)
#define CVMX_NPEI_MEM_ACCESS_CTL (0x00000000000004F0ull)
#define CVMX_NPEI_MEM_ACCESS_SUBIDX(offset) (0x0000000000000340ull + ((offset) & 31) * 16 - 16*12)
#define CVMX_NPEI_MSI_ENB0 (0x0000000000003C50ull)
#define CVMX_NPEI_MSI_ENB1 (0x0000000000003C60ull)
#define CVMX_NPEI_MSI_ENB2 (0x0000000000003C70ull)
#define CVMX_NPEI_MSI_ENB3 (0x0000000000003C80ull)
#define CVMX_NPEI_MSI_RCV0 (0x0000000000003C10ull)
#define CVMX_NPEI_MSI_RCV1 (0x0000000000003C20ull)
#define CVMX_NPEI_MSI_RCV2 (0x0000000000003C30ull)
#define CVMX_NPEI_MSI_RCV3 (0x0000000000003C40ull)
#define CVMX_NPEI_MSI_RD_MAP (0x0000000000003CA0ull)
#define CVMX_NPEI_MSI_W1C_ENB0 (0x0000000000003CF0ull)
#define CVMX_NPEI_MSI_W1C_ENB1 (0x0000000000003D00ull)
#define CVMX_NPEI_MSI_W1C_ENB2 (0x0000000000003D10ull)
#define CVMX_NPEI_MSI_W1C_ENB3 (0x0000000000003D20ull)
#define CVMX_NPEI_MSI_W1S_ENB0 (0x0000000000003D30ull)
#define CVMX_NPEI_MSI_W1S_ENB1 (0x0000000000003D40ull)
#define CVMX_NPEI_MSI_W1S_ENB2 (0x0000000000003D50ull)
#define CVMX_NPEI_MSI_W1S_ENB3 (0x0000000000003D60ull)
#define CVMX_NPEI_MSI_WR_MAP (0x0000000000003C90ull)
#define CVMX_NPEI_PCIE_CREDIT_CNT (0x0000000000003D70ull)
#define CVMX_NPEI_PCIE_MSI_RCV (0x0000000000003CB0ull)
#define CVMX_NPEI_PCIE_MSI_RCV_B1 (0x0000000000000650ull)
#define CVMX_NPEI_PCIE_MSI_RCV_B2 (0x0000000000000660ull)
#define CVMX_NPEI_PCIE_MSI_RCV_B3 (0x0000000000000670ull)
#define CVMX_NPEI_PKTX_CNTS(offset) (0x0000000000002400ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKTX_INSTR_BADDR(offset) (0x0000000000002800ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKTX_INSTR_BAOFF_DBELL(offset) (0x0000000000002C00ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKTX_INSTR_FIFO_RSIZE(offset) (0x0000000000003000ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKTX_INSTR_HEADER(offset) (0x0000000000003400ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKTX_IN_BP(offset) (0x0000000000003800ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKTX_SLIST_BADDR(offset) (0x0000000000001400ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKTX_SLIST_BAOFF_DBELL(offset) (0x0000000000001800ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKTX_SLIST_FIFO_RSIZE(offset) (0x0000000000001C00ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKT_CNT_INT (0x0000000000001110ull)
#define CVMX_NPEI_PKT_CNT_INT_ENB (0x0000000000001130ull)
#define CVMX_NPEI_PKT_DATA_OUT_ES (0x00000000000010B0ull)
#define CVMX_NPEI_PKT_DATA_OUT_NS (0x00000000000010A0ull)
#define CVMX_NPEI_PKT_DATA_OUT_ROR (0x0000000000001090ull)
#define CVMX_NPEI_PKT_DPADDR (0x0000000000001080ull)
#define CVMX_NPEI_PKT_INPUT_CONTROL (0x0000000000001150ull)
#define CVMX_NPEI_PKT_INSTR_ENB (0x0000000000001000ull)
#define CVMX_NPEI_PKT_INSTR_RD_SIZE (0x0000000000001190ull)
#define CVMX_NPEI_PKT_INSTR_SIZE (0x0000000000001020ull)
#define CVMX_NPEI_PKT_INT_LEVELS (0x0000000000001100ull)
#define CVMX_NPEI_PKT_IN_BP (0x00000000000006B0ull)
#define CVMX_NPEI_PKT_IN_DONEX_CNTS(offset) (0x0000000000002000ull + ((offset) & 31) * 16)
#define CVMX_NPEI_PKT_IN_INSTR_COUNTS (0x00000000000006A0ull)
#define CVMX_NPEI_PKT_IN_PCIE_PORT (0x00000000000011A0ull)
#define CVMX_NPEI_PKT_IPTR (0x0000000000001070ull)
#define CVMX_NPEI_PKT_OUTPUT_WMARK (0x0000000000001160ull)
#define CVMX_NPEI_PKT_OUT_BMODE (0x00000000000010D0ull)
#define CVMX_NPEI_PKT_OUT_ENB (0x0000000000001010ull)
#define CVMX_NPEI_PKT_PCIE_PORT (0x00000000000010E0ull)
#define CVMX_NPEI_PKT_PORT_IN_RST (0x0000000000000690ull)
#define CVMX_NPEI_PKT_SLIST_ES (0x0000000000001050ull)
#define CVMX_NPEI_PKT_SLIST_ID_SIZE (0x0000000000001180ull)
#define CVMX_NPEI_PKT_SLIST_NS (0x0000000000001040ull)
#define CVMX_NPEI_PKT_SLIST_ROR (0x0000000000001030ull)
#define CVMX_NPEI_PKT_TIME_INT (0x0000000000001120ull)
#define CVMX_NPEI_PKT_TIME_INT_ENB (0x0000000000001140ull)
#define CVMX_NPEI_RSL_INT_BLOCKS (0x0000000000000520ull)
#define CVMX_NPEI_SCRATCH_1 (0x0000000000000270ull)
#define CVMX_NPEI_STATE1 (0x0000000000000620ull)
#define CVMX_NPEI_STATE2 (0x0000000000000630ull)
#define CVMX_NPEI_STATE3 (0x0000000000000640ull)
#define CVMX_NPEI_WINDOW_CTL (0x0000000000000380ull)
#define CVMX_NPEI_WIN_RD_ADDR (0x0000000000000210ull)
#define CVMX_NPEI_WIN_RD_DATA (0x0000000000000240ull)
#define CVMX_NPEI_WIN_WR_ADDR (0x0000000000000200ull)
#define CVMX_NPEI_WIN_WR_DATA (0x0000000000000220ull)
#define CVMX_NPEI_WIN_WR_MASK (0x0000000000000230ull)

union cvmx_npei_bar1_indexx {
	uint32_t u32;
	struct cvmx_npei_bar1_indexx_s {
		uint32_t reserved_18_31:14;
		uint32_t addr_idx:14;
		uint32_t ca:1;
		uint32_t end_swp:2;
		uint32_t addr_v:1;
	} s;
	struct cvmx_npei_bar1_indexx_s cn52xx;
	struct cvmx_npei_bar1_indexx_s cn52xxp1;
	struct cvmx_npei_bar1_indexx_s cn56xx;
	struct cvmx_npei_bar1_indexx_s cn56xxp1;
};

union cvmx_npei_bist_status {
	uint64_t u64;
	struct cvmx_npei_bist_status_s {
		uint64_t pkt_rdf:1;
		uint64_t reserved_60_62:3;
		uint64_t pcr_gim:1;
		uint64_t pkt_pif:1;
		uint64_t pcsr_int:1;
		uint64_t pcsr_im:1;
		uint64_t pcsr_cnt:1;
		uint64_t pcsr_id:1;
		uint64_t pcsr_sl:1;
		uint64_t reserved_50_52:3;
		uint64_t pkt_ind:1;
		uint64_t pkt_slm:1;
		uint64_t reserved_36_47:12;
		uint64_t d0_pst:1;
		uint64_t d1_pst:1;
		uint64_t d2_pst:1;
		uint64_t d3_pst:1;
		uint64_t reserved_31_31:1;
		uint64_t n2p0_c:1;
		uint64_t n2p0_o:1;
		uint64_t n2p1_c:1;
		uint64_t n2p1_o:1;
		uint64_t cpl_p0:1;
		uint64_t cpl_p1:1;
		uint64_t p2n1_po:1;
		uint64_t p2n1_no:1;
		uint64_t p2n1_co:1;
		uint64_t p2n0_po:1;
		uint64_t p2n0_no:1;
		uint64_t p2n0_co:1;
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
		uint64_t csm0:1;
		uint64_t csm1:1;
		uint64_t dif0:1;
		uint64_t dif1:1;
		uint64_t dif2:1;
		uint64_t dif3:1;
		uint64_t reserved_2_2:1;
		uint64_t msi:1;
		uint64_t ncb_cmd:1;
	} s;
	struct cvmx_npei_bist_status_cn52xx {
		uint64_t pkt_rdf:1;
		uint64_t reserved_60_62:3;
		uint64_t pcr_gim:1;
		uint64_t pkt_pif:1;
		uint64_t pcsr_int:1;
		uint64_t pcsr_im:1;
		uint64_t pcsr_cnt:1;
		uint64_t pcsr_id:1;
		uint64_t pcsr_sl:1;
		uint64_t pkt_imem:1;
		uint64_t pkt_pfm:1;
		uint64_t pkt_pof:1;
		uint64_t reserved_48_49:2;
		uint64_t pkt_pop0:1;
		uint64_t pkt_pop1:1;
		uint64_t d0_mem:1;
		uint64_t d1_mem:1;
		uint64_t d2_mem:1;
		uint64_t d3_mem:1;
		uint64_t d4_mem:1;
		uint64_t ds_mem:1;
		uint64_t reserved_36_39:4;
		uint64_t d0_pst:1;
		uint64_t d1_pst:1;
		uint64_t d2_pst:1;
		uint64_t d3_pst:1;
		uint64_t d4_pst:1;
		uint64_t n2p0_c:1;
		uint64_t n2p0_o:1;
		uint64_t n2p1_c:1;
		uint64_t n2p1_o:1;
		uint64_t cpl_p0:1;
		uint64_t cpl_p1:1;
		uint64_t p2n1_po:1;
		uint64_t p2n1_no:1;
		uint64_t p2n1_co:1;
		uint64_t p2n0_po:1;
		uint64_t p2n0_no:1;
		uint64_t p2n0_co:1;
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
		uint64_t csm0:1;
		uint64_t csm1:1;
		uint64_t dif0:1;
		uint64_t dif1:1;
		uint64_t dif2:1;
		uint64_t dif3:1;
		uint64_t dif4:1;
		uint64_t msi:1;
		uint64_t ncb_cmd:1;
	} cn52xx;
	struct cvmx_npei_bist_status_cn52xxp1 {
		uint64_t reserved_46_63:18;
		uint64_t d0_mem0:1;
		uint64_t d1_mem1:1;
		uint64_t d2_mem2:1;
		uint64_t d3_mem3:1;
		uint64_t dr0_mem:1;
		uint64_t d0_mem:1;
		uint64_t d1_mem:1;
		uint64_t d2_mem:1;
		uint64_t d3_mem:1;
		uint64_t dr1_mem:1;
		uint64_t d0_pst:1;
		uint64_t d1_pst:1;
		uint64_t d2_pst:1;
		uint64_t d3_pst:1;
		uint64_t dr2_mem:1;
		uint64_t n2p0_c:1;
		uint64_t n2p0_o:1;
		uint64_t n2p1_c:1;
		uint64_t n2p1_o:1;
		uint64_t cpl_p0:1;
		uint64_t cpl_p1:1;
		uint64_t p2n1_po:1;
		uint64_t p2n1_no:1;
		uint64_t p2n1_co:1;
		uint64_t p2n0_po:1;
		uint64_t p2n0_no:1;
		uint64_t p2n0_co:1;
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
		uint64_t csm0:1;
		uint64_t csm1:1;
		uint64_t dif0:1;
		uint64_t dif1:1;
		uint64_t dif2:1;
		uint64_t dif3:1;
		uint64_t dr3_mem:1;
		uint64_t msi:1;
		uint64_t ncb_cmd:1;
	} cn52xxp1;
	struct cvmx_npei_bist_status_cn52xx cn56xx;
	struct cvmx_npei_bist_status_cn56xxp1 {
		uint64_t reserved_58_63:6;
		uint64_t pcsr_int:1;
		uint64_t pcsr_im:1;
		uint64_t pcsr_cnt:1;
		uint64_t pcsr_id:1;
		uint64_t pcsr_sl:1;
		uint64_t pkt_pout:1;
		uint64_t pkt_imem:1;
		uint64_t pkt_cntm:1;
		uint64_t pkt_ind:1;
		uint64_t pkt_slm:1;
		uint64_t pkt_odf:1;
		uint64_t pkt_oif:1;
		uint64_t pkt_out:1;
		uint64_t pkt_i0:1;
		uint64_t pkt_i1:1;
		uint64_t pkt_s0:1;
		uint64_t pkt_s1:1;
		uint64_t d0_mem:1;
		uint64_t d1_mem:1;
		uint64_t d2_mem:1;
		uint64_t d3_mem:1;
		uint64_t d4_mem:1;
		uint64_t d0_pst:1;
		uint64_t d1_pst:1;
		uint64_t d2_pst:1;
		uint64_t d3_pst:1;
		uint64_t d4_pst:1;
		uint64_t n2p0_c:1;
		uint64_t n2p0_o:1;
		uint64_t n2p1_c:1;
		uint64_t n2p1_o:1;
		uint64_t cpl_p0:1;
		uint64_t cpl_p1:1;
		uint64_t p2n1_po:1;
		uint64_t p2n1_no:1;
		uint64_t p2n1_co:1;
		uint64_t p2n0_po:1;
		uint64_t p2n0_no:1;
		uint64_t p2n0_co:1;
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
		uint64_t csm0:1;
		uint64_t csm1:1;
		uint64_t dif0:1;
		uint64_t dif1:1;
		uint64_t dif2:1;
		uint64_t dif3:1;
		uint64_t dif4:1;
		uint64_t msi:1;
		uint64_t ncb_cmd:1;
	} cn56xxp1;
};

union cvmx_npei_bist_status2 {
	uint64_t u64;
	struct cvmx_npei_bist_status2_s {
		uint64_t reserved_14_63:50;
		uint64_t prd_tag:1;
		uint64_t prd_st0:1;
		uint64_t prd_st1:1;
		uint64_t prd_err:1;
		uint64_t nrd_st:1;
		uint64_t nwe_st:1;
		uint64_t nwe_wr0:1;
		uint64_t nwe_wr1:1;
		uint64_t pkt_rd:1;
		uint64_t psc_p0:1;
		uint64_t psc_p1:1;
		uint64_t pkt_gd:1;
		uint64_t pkt_gl:1;
		uint64_t pkt_blk:1;
	} s;
	struct cvmx_npei_bist_status2_s cn52xx;
	struct cvmx_npei_bist_status2_s cn56xx;
};

union cvmx_npei_ctl_port0 {
	uint64_t u64;
	struct cvmx_npei_ctl_port0_s {
		uint64_t reserved_21_63:43;
		uint64_t waitl_com:1;
		uint64_t intd:1;
		uint64_t intc:1;
		uint64_t intb:1;
		uint64_t inta:1;
		uint64_t intd_map:2;
		uint64_t intc_map:2;
		uint64_t intb_map:2;
		uint64_t inta_map:2;
		uint64_t ctlp_ro:1;
		uint64_t reserved_6_6:1;
		uint64_t ptlp_ro:1;
		uint64_t bar2_enb:1;
		uint64_t bar2_esx:2;
		uint64_t bar2_cax:1;
		uint64_t wait_com:1;
	} s;
	struct cvmx_npei_ctl_port0_s cn52xx;
	struct cvmx_npei_ctl_port0_s cn52xxp1;
	struct cvmx_npei_ctl_port0_s cn56xx;
	struct cvmx_npei_ctl_port0_s cn56xxp1;
};

union cvmx_npei_ctl_port1 {
	uint64_t u64;
	struct cvmx_npei_ctl_port1_s {
		uint64_t reserved_21_63:43;
		uint64_t waitl_com:1;
		uint64_t intd:1;
		uint64_t intc:1;
		uint64_t intb:1;
		uint64_t inta:1;
		uint64_t intd_map:2;
		uint64_t intc_map:2;
		uint64_t intb_map:2;
		uint64_t inta_map:2;
		uint64_t ctlp_ro:1;
		uint64_t reserved_6_6:1;
		uint64_t ptlp_ro:1;
		uint64_t bar2_enb:1;
		uint64_t bar2_esx:2;
		uint64_t bar2_cax:1;
		uint64_t wait_com:1;
	} s;
	struct cvmx_npei_ctl_port1_s cn52xx;
	struct cvmx_npei_ctl_port1_s cn52xxp1;
	struct cvmx_npei_ctl_port1_s cn56xx;
	struct cvmx_npei_ctl_port1_s cn56xxp1;
};

union cvmx_npei_ctl_status {
	uint64_t u64;
	struct cvmx_npei_ctl_status_s {
		uint64_t reserved_44_63:20;
		uint64_t p1_ntags:6;
		uint64_t p0_ntags:6;
		uint64_t cfg_rtry:16;
		uint64_t ring_en:1;
		uint64_t lnk_rst:1;
		uint64_t arb:1;
		uint64_t pkt_bp:4;
		uint64_t host_mode:1;
		uint64_t chip_rev:8;
	} s;
	struct cvmx_npei_ctl_status_s cn52xx;
	struct cvmx_npei_ctl_status_cn52xxp1 {
		uint64_t reserved_44_63:20;
		uint64_t p1_ntags:6;
		uint64_t p0_ntags:6;
		uint64_t cfg_rtry:16;
		uint64_t reserved_15_15:1;
		uint64_t lnk_rst:1;
		uint64_t arb:1;
		uint64_t reserved_9_12:4;
		uint64_t host_mode:1;
		uint64_t chip_rev:8;
	} cn52xxp1;
	struct cvmx_npei_ctl_status_s cn56xx;
	struct cvmx_npei_ctl_status_cn56xxp1 {
		uint64_t reserved_15_63:49;
		uint64_t lnk_rst:1;
		uint64_t arb:1;
		uint64_t pkt_bp:4;
		uint64_t host_mode:1;
		uint64_t chip_rev:8;
	} cn56xxp1;
};

union cvmx_npei_ctl_status2 {
	uint64_t u64;
	struct cvmx_npei_ctl_status2_s {
		uint64_t reserved_16_63:48;
		uint64_t mps:1;
		uint64_t mrrs:3;
		uint64_t c1_w_flt:1;
		uint64_t c0_w_flt:1;
		uint64_t c1_b1_s:3;
		uint64_t c0_b1_s:3;
		uint64_t c1_wi_d:1;
		uint64_t c1_b0_d:1;
		uint64_t c0_wi_d:1;
		uint64_t c0_b0_d:1;
	} s;
	struct cvmx_npei_ctl_status2_s cn52xx;
	struct cvmx_npei_ctl_status2_s cn52xxp1;
	struct cvmx_npei_ctl_status2_s cn56xx;
	struct cvmx_npei_ctl_status2_s cn56xxp1;
};

union cvmx_npei_data_out_cnt {
	uint64_t u64;
	struct cvmx_npei_data_out_cnt_s {
		uint64_t reserved_44_63:20;
		uint64_t p1_ucnt:16;
		uint64_t p1_fcnt:6;
		uint64_t p0_ucnt:16;
		uint64_t p0_fcnt:6;
	} s;
	struct cvmx_npei_data_out_cnt_s cn52xx;
	struct cvmx_npei_data_out_cnt_s cn52xxp1;
	struct cvmx_npei_data_out_cnt_s cn56xx;
	struct cvmx_npei_data_out_cnt_s cn56xxp1;
};

union cvmx_npei_dbg_data {
	uint64_t u64;
	struct cvmx_npei_dbg_data_s {
		uint64_t reserved_28_63:36;
		uint64_t qlm0_rev_lanes:1;
		uint64_t reserved_25_26:2;
		uint64_t qlm1_spd:2;
		uint64_t c_mul:5;
		uint64_t dsel_ext:1;
		uint64_t data:17;
	} s;
	struct cvmx_npei_dbg_data_cn52xx {
		uint64_t reserved_29_63:35;
		uint64_t qlm0_link_width:1;
		uint64_t qlm0_rev_lanes:1;
		uint64_t qlm1_mode:2;
		uint64_t qlm1_spd:2;
		uint64_t c_mul:5;
		uint64_t dsel_ext:1;
		uint64_t data:17;
	} cn52xx;
	struct cvmx_npei_dbg_data_cn52xx cn52xxp1;
	struct cvmx_npei_dbg_data_cn56xx {
		uint64_t reserved_29_63:35;
		uint64_t qlm2_rev_lanes:1;
		uint64_t qlm0_rev_lanes:1;
		uint64_t qlm3_spd:2;
		uint64_t qlm1_spd:2;
		uint64_t c_mul:5;
		uint64_t dsel_ext:1;
		uint64_t data:17;
	} cn56xx;
	struct cvmx_npei_dbg_data_cn56xx cn56xxp1;
};

union cvmx_npei_dbg_select {
	uint64_t u64;
	struct cvmx_npei_dbg_select_s {
		uint64_t reserved_16_63:48;
		uint64_t dbg_sel:16;
	} s;
	struct cvmx_npei_dbg_select_s cn52xx;
	struct cvmx_npei_dbg_select_s cn52xxp1;
	struct cvmx_npei_dbg_select_s cn56xx;
	struct cvmx_npei_dbg_select_s cn56xxp1;
};

union cvmx_npei_dmax_counts {
	uint64_t u64;
	struct cvmx_npei_dmax_counts_s {
		uint64_t reserved_39_63:25;
		uint64_t fcnt:7;
		uint64_t dbell:32;
	} s;
	struct cvmx_npei_dmax_counts_s cn52xx;
	struct cvmx_npei_dmax_counts_s cn52xxp1;
	struct cvmx_npei_dmax_counts_s cn56xx;
	struct cvmx_npei_dmax_counts_s cn56xxp1;
};

union cvmx_npei_dmax_dbell {
	uint32_t u32;
	struct cvmx_npei_dmax_dbell_s {
		uint32_t reserved_16_31:16;
		uint32_t dbell:16;
	} s;
	struct cvmx_npei_dmax_dbell_s cn52xx;
	struct cvmx_npei_dmax_dbell_s cn52xxp1;
	struct cvmx_npei_dmax_dbell_s cn56xx;
	struct cvmx_npei_dmax_dbell_s cn56xxp1;
};

union cvmx_npei_dmax_ibuff_saddr {
	uint64_t u64;
	struct cvmx_npei_dmax_ibuff_saddr_s {
		uint64_t reserved_37_63:27;
		uint64_t idle:1;
		uint64_t saddr:29;
		uint64_t reserved_0_6:7;
	} s;
	struct cvmx_npei_dmax_ibuff_saddr_s cn52xx;
	struct cvmx_npei_dmax_ibuff_saddr_cn52xxp1 {
		uint64_t reserved_36_63:28;
		uint64_t saddr:29;
		uint64_t reserved_0_6:7;
	} cn52xxp1;
	struct cvmx_npei_dmax_ibuff_saddr_s cn56xx;
	struct cvmx_npei_dmax_ibuff_saddr_cn52xxp1 cn56xxp1;
};

union cvmx_npei_dmax_naddr {
	uint64_t u64;
	struct cvmx_npei_dmax_naddr_s {
		uint64_t reserved_36_63:28;
		uint64_t addr:36;
	} s;
	struct cvmx_npei_dmax_naddr_s cn52xx;
	struct cvmx_npei_dmax_naddr_s cn52xxp1;
	struct cvmx_npei_dmax_naddr_s cn56xx;
	struct cvmx_npei_dmax_naddr_s cn56xxp1;
};

union cvmx_npei_dma0_int_level {
	uint64_t u64;
	struct cvmx_npei_dma0_int_level_s {
		uint64_t time:32;
		uint64_t cnt:32;
	} s;
	struct cvmx_npei_dma0_int_level_s cn52xx;
	struct cvmx_npei_dma0_int_level_s cn52xxp1;
	struct cvmx_npei_dma0_int_level_s cn56xx;
	struct cvmx_npei_dma0_int_level_s cn56xxp1;
};

union cvmx_npei_dma1_int_level {
	uint64_t u64;
	struct cvmx_npei_dma1_int_level_s {
		uint64_t time:32;
		uint64_t cnt:32;
	} s;
	struct cvmx_npei_dma1_int_level_s cn52xx;
	struct cvmx_npei_dma1_int_level_s cn52xxp1;
	struct cvmx_npei_dma1_int_level_s cn56xx;
	struct cvmx_npei_dma1_int_level_s cn56xxp1;
};

union cvmx_npei_dma_cnts {
	uint64_t u64;
	struct cvmx_npei_dma_cnts_s {
		uint64_t dma1:32;
		uint64_t dma0:32;
	} s;
	struct cvmx_npei_dma_cnts_s cn52xx;
	struct cvmx_npei_dma_cnts_s cn52xxp1;
	struct cvmx_npei_dma_cnts_s cn56xx;
	struct cvmx_npei_dma_cnts_s cn56xxp1;
};

union cvmx_npei_dma_control {
	uint64_t u64;
	struct cvmx_npei_dma_control_s {
		uint64_t reserved_40_63:24;
		uint64_t p_32b_m:1;
		uint64_t dma4_enb:1;
		uint64_t dma3_enb:1;
		uint64_t dma2_enb:1;
		uint64_t dma1_enb:1;
		uint64_t dma0_enb:1;
		uint64_t b0_lend:1;
		uint64_t dwb_denb:1;
		uint64_t dwb_ichk:9;
		uint64_t fpa_que:3;
		uint64_t o_add1:1;
		uint64_t o_ro:1;
		uint64_t o_ns:1;
		uint64_t o_es:2;
		uint64_t o_mode:1;
		uint64_t csize:14;
	} s;
	struct cvmx_npei_dma_control_s cn52xx;
	struct cvmx_npei_dma_control_cn52xxp1 {
		uint64_t reserved_38_63:26;
		uint64_t dma3_enb:1;
		uint64_t dma2_enb:1;
		uint64_t dma1_enb:1;
		uint64_t dma0_enb:1;
		uint64_t b0_lend:1;
		uint64_t dwb_denb:1;
		uint64_t dwb_ichk:9;
		uint64_t fpa_que:3;
		uint64_t o_add1:1;
		uint64_t o_ro:1;
		uint64_t o_ns:1;
		uint64_t o_es:2;
		uint64_t o_mode:1;
		uint64_t csize:14;
	} cn52xxp1;
	struct cvmx_npei_dma_control_s cn56xx;
	struct cvmx_npei_dma_control_cn56xxp1 {
		uint64_t reserved_39_63:25;
		uint64_t dma4_enb:1;
		uint64_t dma3_enb:1;
		uint64_t dma2_enb:1;
		uint64_t dma1_enb:1;
		uint64_t dma0_enb:1;
		uint64_t b0_lend:1;
		uint64_t dwb_denb:1;
		uint64_t dwb_ichk:9;
		uint64_t fpa_que:3;
		uint64_t o_add1:1;
		uint64_t o_ro:1;
		uint64_t o_ns:1;
		uint64_t o_es:2;
		uint64_t o_mode:1;
		uint64_t csize:14;
	} cn56xxp1;
};

union cvmx_npei_dma_pcie_req_num {
	uint64_t u64;
	struct cvmx_npei_dma_pcie_req_num_s {
		uint64_t dma_arb:1;
		uint64_t reserved_53_62:10;
		uint64_t pkt_cnt:5;
		uint64_t reserved_45_47:3;
		uint64_t dma4_cnt:5;
		uint64_t reserved_37_39:3;
		uint64_t dma3_cnt:5;
		uint64_t reserved_29_31:3;
		uint64_t dma2_cnt:5;
		uint64_t reserved_21_23:3;
		uint64_t dma1_cnt:5;
		uint64_t reserved_13_15:3;
		uint64_t dma0_cnt:5;
		uint64_t reserved_5_7:3;
		uint64_t dma_cnt:5;
	} s;
	struct cvmx_npei_dma_pcie_req_num_s cn52xx;
	struct cvmx_npei_dma_pcie_req_num_s cn56xx;
};

union cvmx_npei_dma_state1 {
	uint64_t u64;
	struct cvmx_npei_dma_state1_s {
		uint64_t reserved_40_63:24;
		uint64_t d4_dwe:8;
		uint64_t d3_dwe:8;
		uint64_t d2_dwe:8;
		uint64_t d1_dwe:8;
		uint64_t d0_dwe:8;
	} s;
	struct cvmx_npei_dma_state1_s cn52xx;
};

union cvmx_npei_dma_state1_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state1_p1_s {
		uint64_t reserved_60_63:4;
		uint64_t d0_difst:7;
		uint64_t d1_difst:7;
		uint64_t d2_difst:7;
		uint64_t d3_difst:7;
		uint64_t d4_difst:7;
		uint64_t d0_reqst:5;
		uint64_t d1_reqst:5;
		uint64_t d2_reqst:5;
		uint64_t d3_reqst:5;
		uint64_t d4_reqst:5;
	} s;
	struct cvmx_npei_dma_state1_p1_cn52xxp1 {
		uint64_t reserved_60_63:4;
		uint64_t d0_difst:7;
		uint64_t d1_difst:7;
		uint64_t d2_difst:7;
		uint64_t d3_difst:7;
		uint64_t reserved_25_31:7;
		uint64_t d0_reqst:5;
		uint64_t d1_reqst:5;
		uint64_t d2_reqst:5;
		uint64_t d3_reqst:5;
		uint64_t reserved_0_4:5;
	} cn52xxp1;
	struct cvmx_npei_dma_state1_p1_s cn56xxp1;
};

union cvmx_npei_dma_state2 {
	uint64_t u64;
	struct cvmx_npei_dma_state2_s {
		uint64_t reserved_28_63:36;
		uint64_t ndwe:4;
		uint64_t reserved_21_23:3;
		uint64_t ndre:5;
		uint64_t reserved_10_15:6;
		uint64_t prd:10;
	} s;
	struct cvmx_npei_dma_state2_s cn52xx;
};

union cvmx_npei_dma_state2_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state2_p1_s {
		uint64_t reserved_45_63:19;
		uint64_t d0_dffst:9;
		uint64_t d1_dffst:9;
		uint64_t d2_dffst:9;
		uint64_t d3_dffst:9;
		uint64_t d4_dffst:9;
	} s;
	struct cvmx_npei_dma_state2_p1_cn52xxp1 {
		uint64_t reserved_45_63:19;
		uint64_t d0_dffst:9;
		uint64_t d1_dffst:9;
		uint64_t d2_dffst:9;
		uint64_t d3_dffst:9;
		uint64_t reserved_0_8:9;
	} cn52xxp1;
	struct cvmx_npei_dma_state2_p1_s cn56xxp1;
};

union cvmx_npei_dma_state3_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state3_p1_s {
		uint64_t reserved_60_63:4;
		uint64_t d0_drest:15;
		uint64_t d1_drest:15;
		uint64_t d2_drest:15;
		uint64_t d3_drest:15;
	} s;
	struct cvmx_npei_dma_state3_p1_s cn52xxp1;
	struct cvmx_npei_dma_state3_p1_s cn56xxp1;
};

union cvmx_npei_dma_state4_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state4_p1_s {
		uint64_t reserved_52_63:12;
		uint64_t d0_dwest:13;
		uint64_t d1_dwest:13;
		uint64_t d2_dwest:13;
		uint64_t d3_dwest:13;
	} s;
	struct cvmx_npei_dma_state4_p1_s cn52xxp1;
	struct cvmx_npei_dma_state4_p1_s cn56xxp1;
};

union cvmx_npei_dma_state5_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state5_p1_s {
		uint64_t reserved_28_63:36;
		uint64_t d4_drest:15;
		uint64_t d4_dwest:13;
	} s;
	struct cvmx_npei_dma_state5_p1_s cn56xxp1;
};

union cvmx_npei_int_a_enb {
	uint64_t u64;
	struct cvmx_npei_int_a_enb_s {
		uint64_t reserved_10_63:54;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t p1_rdlk:1;
		uint64_t p0_rdlk:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t dma1_cpl:1;
		uint64_t dma0_cpl:1;
	} s;
	struct cvmx_npei_int_a_enb_s cn52xx;
	struct cvmx_npei_int_a_enb_cn52xxp1 {
		uint64_t reserved_2_63:62;
		uint64_t dma1_cpl:1;
		uint64_t dma0_cpl:1;
	} cn52xxp1;
	struct cvmx_npei_int_a_enb_s cn56xx;
};

union cvmx_npei_int_a_enb2 {
	uint64_t u64;
	struct cvmx_npei_int_a_enb2_s {
		uint64_t reserved_10_63:54;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t p1_rdlk:1;
		uint64_t p0_rdlk:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t dma1_cpl:1;
		uint64_t dma0_cpl:1;
	} s;
	struct cvmx_npei_int_a_enb2_s cn52xx;
	struct cvmx_npei_int_a_enb2_cn52xxp1 {
		uint64_t reserved_2_63:62;
		uint64_t dma1_cpl:1;
		uint64_t dma0_cpl:1;
	} cn52xxp1;
	struct cvmx_npei_int_a_enb2_s cn56xx;
};

union cvmx_npei_int_a_sum {
	uint64_t u64;
	struct cvmx_npei_int_a_sum_s {
		uint64_t reserved_10_63:54;
		uint64_t pout_err:1;
		uint64_t pin_bp:1;
		uint64_t p1_rdlk:1;
		uint64_t p0_rdlk:1;
		uint64_t pgl_err:1;
		uint64_t pdi_err:1;
		uint64_t pop_err:1;
		uint64_t pins_err:1;
		uint64_t dma1_cpl:1;
		uint64_t dma0_cpl:1;
	} s;
	struct cvmx_npei_int_a_sum_s cn52xx;
	struct cvmx_npei_int_a_sum_cn52xxp1 {
		uint64_t reserved_2_63:62;
		uint64_t dma1_cpl:1;
		uint64_t dma0_cpl:1;
	} cn52xxp1;
	struct cvmx_npei_int_a_sum_s cn56xx;
};

union cvmx_npei_int_enb {
	uint64_t u64;
	struct cvmx_npei_int_enb_s {
		uint64_t mio_inta:1;
		uint64_t reserved_62_62:1;
		uint64_t int_a:1;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t crs1_dr:1;
		uint64_t c1_se:1;
		uint64_t crs1_er:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t crs0_dr:1;
		uint64_t c0_se:1;
		uint64_t crs0_er:1;
		uint64_t c0_aeri:1;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t dma4dbo:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} s;
	struct cvmx_npei_int_enb_s cn52xx;
	struct cvmx_npei_int_enb_cn52xxp1 {
		uint64_t mio_inta:1;
		uint64_t reserved_62_62:1;
		uint64_t int_a:1;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t crs1_dr:1;
		uint64_t c1_se:1;
		uint64_t crs1_er:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t crs0_dr:1;
		uint64_t c0_se:1;
		uint64_t crs0_er:1;
		uint64_t c0_aeri:1;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t reserved_8_8:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} cn52xxp1;
	struct cvmx_npei_int_enb_s cn56xx;
	struct cvmx_npei_int_enb_cn56xxp1 {
		uint64_t mio_inta:1;
		uint64_t reserved_61_62:2;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t reserved_29_29:1;
		uint64_t c1_se:1;
		uint64_t reserved_27_27:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t reserved_22_22:1;
		uint64_t c0_se:1;
		uint64_t reserved_20_20:1;
		uint64_t c0_aeri:1;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t dma4dbo:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} cn56xxp1;
};

union cvmx_npei_int_enb2 {
	uint64_t u64;
	struct cvmx_npei_int_enb2_s {
		uint64_t reserved_62_63:2;
		uint64_t int_a:1;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t crs1_dr:1;
		uint64_t c1_se:1;
		uint64_t crs1_er:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t crs0_dr:1;
		uint64_t c0_se:1;
		uint64_t crs0_er:1;
		uint64_t c0_aeri:1;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t dma4dbo:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} s;
	struct cvmx_npei_int_enb2_s cn52xx;
	struct cvmx_npei_int_enb2_cn52xxp1 {
		uint64_t reserved_62_63:2;
		uint64_t int_a:1;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t crs1_dr:1;
		uint64_t c1_se:1;
		uint64_t crs1_er:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t crs0_dr:1;
		uint64_t c0_se:1;
		uint64_t crs0_er:1;
		uint64_t c0_aeri:1;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t reserved_8_8:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} cn52xxp1;
	struct cvmx_npei_int_enb2_s cn56xx;
	struct cvmx_npei_int_enb2_cn56xxp1 {
		uint64_t reserved_61_63:3;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t reserved_29_29:1;
		uint64_t c1_se:1;
		uint64_t reserved_27_27:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t reserved_22_22:1;
		uint64_t c0_se:1;
		uint64_t reserved_20_20:1;
		uint64_t c0_aeri:1;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t dma4dbo:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} cn56xxp1;
};

union cvmx_npei_int_info {
	uint64_t u64;
	struct cvmx_npei_int_info_s {
		uint64_t reserved_12_63:52;
		uint64_t pidbof:6;
		uint64_t psldbof:6;
	} s;
	struct cvmx_npei_int_info_s cn52xx;
	struct cvmx_npei_int_info_s cn56xx;
	struct cvmx_npei_int_info_s cn56xxp1;
};

union cvmx_npei_int_sum {
	uint64_t u64;
	struct cvmx_npei_int_sum_s {
		uint64_t mio_inta:1;
		uint64_t reserved_62_62:1;
		uint64_t int_a:1;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t crs1_dr:1;
		uint64_t c1_se:1;
		uint64_t crs1_er:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t crs0_dr:1;
		uint64_t c0_se:1;
		uint64_t crs0_er:1;
		uint64_t c0_aeri:1;
		uint64_t ptime:1;
		uint64_t pcnt:1;
		uint64_t pidbof:1;
		uint64_t psldbof:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t dma4dbo:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} s;
	struct cvmx_npei_int_sum_s cn52xx;
	struct cvmx_npei_int_sum_cn52xxp1 {
		uint64_t mio_inta:1;
		uint64_t reserved_62_62:1;
		uint64_t int_a:1;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t crs1_dr:1;
		uint64_t c1_se:1;
		uint64_t crs1_er:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t crs0_dr:1;
		uint64_t c0_se:1;
		uint64_t crs0_er:1;
		uint64_t c0_aeri:1;
		uint64_t reserved_15_18:4;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t reserved_8_8:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} cn52xxp1;
	struct cvmx_npei_int_sum_s cn56xx;
	struct cvmx_npei_int_sum_cn56xxp1 {
		uint64_t mio_inta:1;
		uint64_t reserved_61_62:2;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t reserved_29_29:1;
		uint64_t c1_se:1;
		uint64_t reserved_27_27:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t reserved_22_22:1;
		uint64_t c0_se:1;
		uint64_t reserved_20_20:1;
		uint64_t c0_aeri:1;
		uint64_t reserved_15_18:4;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t dma4dbo:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} cn56xxp1;
};

union cvmx_npei_int_sum2 {
	uint64_t u64;
	struct cvmx_npei_int_sum2_s {
		uint64_t mio_inta:1;
		uint64_t reserved_62_62:1;
		uint64_t int_a:1;
		uint64_t c1_ldwn:1;
		uint64_t c0_ldwn:1;
		uint64_t c1_exc:1;
		uint64_t c0_exc:1;
		uint64_t c1_up_wf:1;
		uint64_t c0_up_wf:1;
		uint64_t c1_un_wf:1;
		uint64_t c0_un_wf:1;
		uint64_t c1_un_bx:1;
		uint64_t c1_un_wi:1;
		uint64_t c1_un_b2:1;
		uint64_t c1_un_b1:1;
		uint64_t c1_un_b0:1;
		uint64_t c1_up_bx:1;
		uint64_t c1_up_wi:1;
		uint64_t c1_up_b2:1;
		uint64_t c1_up_b1:1;
		uint64_t c1_up_b0:1;
		uint64_t c0_un_bx:1;
		uint64_t c0_un_wi:1;
		uint64_t c0_un_b2:1;
		uint64_t c0_un_b1:1;
		uint64_t c0_un_b0:1;
		uint64_t c0_up_bx:1;
		uint64_t c0_up_wi:1;
		uint64_t c0_up_b2:1;
		uint64_t c0_up_b1:1;
		uint64_t c0_up_b0:1;
		uint64_t c1_hpint:1;
		uint64_t c1_pmei:1;
		uint64_t c1_wake:1;
		uint64_t crs1_dr:1;
		uint64_t c1_se:1;
		uint64_t crs1_er:1;
		uint64_t c1_aeri:1;
		uint64_t c0_hpint:1;
		uint64_t c0_pmei:1;
		uint64_t c0_wake:1;
		uint64_t crs0_dr:1;
		uint64_t c0_se:1;
		uint64_t crs0_er:1;
		uint64_t c0_aeri:1;
		uint64_t reserved_15_18:4;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t dma1fi:1;
		uint64_t dma0fi:1;
		uint64_t reserved_8_8:1;
		uint64_t dma3dbo:1;
		uint64_t dma2dbo:1;
		uint64_t dma1dbo:1;
		uint64_t dma0dbo:1;
		uint64_t iob2big:1;
		uint64_t bar0_to:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
	} s;
	struct cvmx_npei_int_sum2_s cn52xx;
	struct cvmx_npei_int_sum2_s cn52xxp1;
	struct cvmx_npei_int_sum2_s cn56xx;
};

union cvmx_npei_last_win_rdata0 {
	uint64_t u64;
	struct cvmx_npei_last_win_rdata0_s {
		uint64_t data:64;
	} s;
	struct cvmx_npei_last_win_rdata0_s cn52xx;
	struct cvmx_npei_last_win_rdata0_s cn52xxp1;
	struct cvmx_npei_last_win_rdata0_s cn56xx;
	struct cvmx_npei_last_win_rdata0_s cn56xxp1;
};

union cvmx_npei_last_win_rdata1 {
	uint64_t u64;
	struct cvmx_npei_last_win_rdata1_s {
		uint64_t data:64;
	} s;
	struct cvmx_npei_last_win_rdata1_s cn52xx;
	struct cvmx_npei_last_win_rdata1_s cn52xxp1;
	struct cvmx_npei_last_win_rdata1_s cn56xx;
	struct cvmx_npei_last_win_rdata1_s cn56xxp1;
};

union cvmx_npei_mem_access_ctl {
	uint64_t u64;
	struct cvmx_npei_mem_access_ctl_s {
		uint64_t reserved_14_63:50;
		uint64_t max_word:4;
		uint64_t timer:10;
	} s;
	struct cvmx_npei_mem_access_ctl_s cn52xx;
	struct cvmx_npei_mem_access_ctl_s cn52xxp1;
	struct cvmx_npei_mem_access_ctl_s cn56xx;
	struct cvmx_npei_mem_access_ctl_s cn56xxp1;
};

union cvmx_npei_mem_access_subidx {
	uint64_t u64;
	struct cvmx_npei_mem_access_subidx_s {
		uint64_t reserved_42_63:22;
		uint64_t zero:1;
		uint64_t port:2;
		uint64_t nmerge:1;
		uint64_t esr:2;
		uint64_t esw:2;
		uint64_t nsr:1;
		uint64_t nsw:1;
		uint64_t ror:1;
		uint64_t row:1;
		uint64_t ba:30;
	} s;
	struct cvmx_npei_mem_access_subidx_s cn52xx;
	struct cvmx_npei_mem_access_subidx_s cn52xxp1;
	struct cvmx_npei_mem_access_subidx_s cn56xx;
	struct cvmx_npei_mem_access_subidx_s cn56xxp1;
};

union cvmx_npei_msi_enb0 {
	uint64_t u64;
	struct cvmx_npei_msi_enb0_s {
		uint64_t enb:64;
	} s;
	struct cvmx_npei_msi_enb0_s cn52xx;
	struct cvmx_npei_msi_enb0_s cn52xxp1;
	struct cvmx_npei_msi_enb0_s cn56xx;
	struct cvmx_npei_msi_enb0_s cn56xxp1;
};

union cvmx_npei_msi_enb1 {
	uint64_t u64;
	struct cvmx_npei_msi_enb1_s {
		uint64_t enb:64;
	} s;
	struct cvmx_npei_msi_enb1_s cn52xx;
	struct cvmx_npei_msi_enb1_s cn52xxp1;
	struct cvmx_npei_msi_enb1_s cn56xx;
	struct cvmx_npei_msi_enb1_s cn56xxp1;
};

union cvmx_npei_msi_enb2 {
	uint64_t u64;
	struct cvmx_npei_msi_enb2_s {
		uint64_t enb:64;
	} s;
	struct cvmx_npei_msi_enb2_s cn52xx;
	struct cvmx_npei_msi_enb2_s cn52xxp1;
	struct cvmx_npei_msi_enb2_s cn56xx;
	struct cvmx_npei_msi_enb2_s cn56xxp1;
};

union cvmx_npei_msi_enb3 {
	uint64_t u64;
	struct cvmx_npei_msi_enb3_s {
		uint64_t enb:64;
	} s;
	struct cvmx_npei_msi_enb3_s cn52xx;
	struct cvmx_npei_msi_enb3_s cn52xxp1;
	struct cvmx_npei_msi_enb3_s cn56xx;
	struct cvmx_npei_msi_enb3_s cn56xxp1;
};

union cvmx_npei_msi_rcv0 {
	uint64_t u64;
	struct cvmx_npei_msi_rcv0_s {
		uint64_t intr:64;
	} s;
	struct cvmx_npei_msi_rcv0_s cn52xx;
	struct cvmx_npei_msi_rcv0_s cn52xxp1;
	struct cvmx_npei_msi_rcv0_s cn56xx;
	struct cvmx_npei_msi_rcv0_s cn56xxp1;
};

union cvmx_npei_msi_rcv1 {
	uint64_t u64;
	struct cvmx_npei_msi_rcv1_s {
		uint64_t intr:64;
	} s;
	struct cvmx_npei_msi_rcv1_s cn52xx;
	struct cvmx_npei_msi_rcv1_s cn52xxp1;
	struct cvmx_npei_msi_rcv1_s cn56xx;
	struct cvmx_npei_msi_rcv1_s cn56xxp1;
};

union cvmx_npei_msi_rcv2 {
	uint64_t u64;
	struct cvmx_npei_msi_rcv2_s {
		uint64_t intr:64;
	} s;
	struct cvmx_npei_msi_rcv2_s cn52xx;
	struct cvmx_npei_msi_rcv2_s cn52xxp1;
	struct cvmx_npei_msi_rcv2_s cn56xx;
	struct cvmx_npei_msi_rcv2_s cn56xxp1;
};

union cvmx_npei_msi_rcv3 {
	uint64_t u64;
	struct cvmx_npei_msi_rcv3_s {
		uint64_t intr:64;
	} s;
	struct cvmx_npei_msi_rcv3_s cn52xx;
	struct cvmx_npei_msi_rcv3_s cn52xxp1;
	struct cvmx_npei_msi_rcv3_s cn56xx;
	struct cvmx_npei_msi_rcv3_s cn56xxp1;
};

union cvmx_npei_msi_rd_map {
	uint64_t u64;
	struct cvmx_npei_msi_rd_map_s {
		uint64_t reserved_16_63:48;
		uint64_t rd_int:8;
		uint64_t msi_int:8;
	} s;
	struct cvmx_npei_msi_rd_map_s cn52xx;
	struct cvmx_npei_msi_rd_map_s cn52xxp1;
	struct cvmx_npei_msi_rd_map_s cn56xx;
	struct cvmx_npei_msi_rd_map_s cn56xxp1;
};

union cvmx_npei_msi_w1c_enb0 {
	uint64_t u64;
	struct cvmx_npei_msi_w1c_enb0_s {
		uint64_t clr:64;
	} s;
	struct cvmx_npei_msi_w1c_enb0_s cn52xx;
	struct cvmx_npei_msi_w1c_enb0_s cn56xx;
};

union cvmx_npei_msi_w1c_enb1 {
	uint64_t u64;
	struct cvmx_npei_msi_w1c_enb1_s {
		uint64_t clr:64;
	} s;
	struct cvmx_npei_msi_w1c_enb1_s cn52xx;
	struct cvmx_npei_msi_w1c_enb1_s cn56xx;
};

union cvmx_npei_msi_w1c_enb2 {
	uint64_t u64;
	struct cvmx_npei_msi_w1c_enb2_s {
		uint64_t clr:64;
	} s;
	struct cvmx_npei_msi_w1c_enb2_s cn52xx;
	struct cvmx_npei_msi_w1c_enb2_s cn56xx;
};

union cvmx_npei_msi_w1c_enb3 {
	uint64_t u64;
	struct cvmx_npei_msi_w1c_enb3_s {
		uint64_t clr:64;
	} s;
	struct cvmx_npei_msi_w1c_enb3_s cn52xx;
	struct cvmx_npei_msi_w1c_enb3_s cn56xx;
};

union cvmx_npei_msi_w1s_enb0 {
	uint64_t u64;
	struct cvmx_npei_msi_w1s_enb0_s {
		uint64_t set:64;
	} s;
	struct cvmx_npei_msi_w1s_enb0_s cn52xx;
	struct cvmx_npei_msi_w1s_enb0_s cn56xx;
};

union cvmx_npei_msi_w1s_enb1 {
	uint64_t u64;
	struct cvmx_npei_msi_w1s_enb1_s {
		uint64_t set:64;
	} s;
	struct cvmx_npei_msi_w1s_enb1_s cn52xx;
	struct cvmx_npei_msi_w1s_enb1_s cn56xx;
};

union cvmx_npei_msi_w1s_enb2 {
	uint64_t u64;
	struct cvmx_npei_msi_w1s_enb2_s {
		uint64_t set:64;
	} s;
	struct cvmx_npei_msi_w1s_enb2_s cn52xx;
	struct cvmx_npei_msi_w1s_enb2_s cn56xx;
};

union cvmx_npei_msi_w1s_enb3 {
	uint64_t u64;
	struct cvmx_npei_msi_w1s_enb3_s {
		uint64_t set:64;
	} s;
	struct cvmx_npei_msi_w1s_enb3_s cn52xx;
	struct cvmx_npei_msi_w1s_enb3_s cn56xx;
};

union cvmx_npei_msi_wr_map {
	uint64_t u64;
	struct cvmx_npei_msi_wr_map_s {
		uint64_t reserved_16_63:48;
		uint64_t ciu_int:8;
		uint64_t msi_int:8;
	} s;
	struct cvmx_npei_msi_wr_map_s cn52xx;
	struct cvmx_npei_msi_wr_map_s cn52xxp1;
	struct cvmx_npei_msi_wr_map_s cn56xx;
	struct cvmx_npei_msi_wr_map_s cn56xxp1;
};

union cvmx_npei_pcie_credit_cnt {
	uint64_t u64;
	struct cvmx_npei_pcie_credit_cnt_s {
		uint64_t reserved_48_63:16;
		uint64_t p1_ccnt:8;
		uint64_t p1_ncnt:8;
		uint64_t p1_pcnt:8;
		uint64_t p0_ccnt:8;
		uint64_t p0_ncnt:8;
		uint64_t p0_pcnt:8;
	} s;
	struct cvmx_npei_pcie_credit_cnt_s cn52xx;
	struct cvmx_npei_pcie_credit_cnt_s cn56xx;
};

union cvmx_npei_pcie_msi_rcv {
	uint64_t u64;
	struct cvmx_npei_pcie_msi_rcv_s {
		uint64_t reserved_8_63:56;
		uint64_t intr:8;
	} s;
	struct cvmx_npei_pcie_msi_rcv_s cn52xx;
	struct cvmx_npei_pcie_msi_rcv_s cn52xxp1;
	struct cvmx_npei_pcie_msi_rcv_s cn56xx;
	struct cvmx_npei_pcie_msi_rcv_s cn56xxp1;
};

union cvmx_npei_pcie_msi_rcv_b1 {
	uint64_t u64;
	struct cvmx_npei_pcie_msi_rcv_b1_s {
		uint64_t reserved_16_63:48;
		uint64_t intr:8;
		uint64_t reserved_0_7:8;
	} s;
	struct cvmx_npei_pcie_msi_rcv_b1_s cn52xx;
	struct cvmx_npei_pcie_msi_rcv_b1_s cn52xxp1;
	struct cvmx_npei_pcie_msi_rcv_b1_s cn56xx;
	struct cvmx_npei_pcie_msi_rcv_b1_s cn56xxp1;
};

union cvmx_npei_pcie_msi_rcv_b2 {
	uint64_t u64;
	struct cvmx_npei_pcie_msi_rcv_b2_s {
		uint64_t reserved_24_63:40;
		uint64_t intr:8;
		uint64_t reserved_0_15:16;
	} s;
	struct cvmx_npei_pcie_msi_rcv_b2_s cn52xx;
	struct cvmx_npei_pcie_msi_rcv_b2_s cn52xxp1;
	struct cvmx_npei_pcie_msi_rcv_b2_s cn56xx;
	struct cvmx_npei_pcie_msi_rcv_b2_s cn56xxp1;
};

union cvmx_npei_pcie_msi_rcv_b3 {
	uint64_t u64;
	struct cvmx_npei_pcie_msi_rcv_b3_s {
		uint64_t reserved_32_63:32;
		uint64_t intr:8;
		uint64_t reserved_0_23:24;
	} s;
	struct cvmx_npei_pcie_msi_rcv_b3_s cn52xx;
	struct cvmx_npei_pcie_msi_rcv_b3_s cn52xxp1;
	struct cvmx_npei_pcie_msi_rcv_b3_s cn56xx;
	struct cvmx_npei_pcie_msi_rcv_b3_s cn56xxp1;
};

union cvmx_npei_pktx_cnts {
	uint64_t u64;
	struct cvmx_npei_pktx_cnts_s {
		uint64_t reserved_54_63:10;
		uint64_t timer:22;
		uint64_t cnt:32;
	} s;
	struct cvmx_npei_pktx_cnts_s cn52xx;
	struct cvmx_npei_pktx_cnts_s cn56xx;
};

union cvmx_npei_pktx_in_bp {
	uint64_t u64;
	struct cvmx_npei_pktx_in_bp_s {
		uint64_t wmark:32;
		uint64_t cnt:32;
	} s;
	struct cvmx_npei_pktx_in_bp_s cn52xx;
	struct cvmx_npei_pktx_in_bp_s cn56xx;
};

union cvmx_npei_pktx_instr_baddr {
	uint64_t u64;
	struct cvmx_npei_pktx_instr_baddr_s {
		uint64_t addr:61;
		uint64_t reserved_0_2:3;
	} s;
	struct cvmx_npei_pktx_instr_baddr_s cn52xx;
	struct cvmx_npei_pktx_instr_baddr_s cn56xx;
};

union cvmx_npei_pktx_instr_baoff_dbell {
	uint64_t u64;
	struct cvmx_npei_pktx_instr_baoff_dbell_s {
		uint64_t aoff:32;
		uint64_t dbell:32;
	} s;
	struct cvmx_npei_pktx_instr_baoff_dbell_s cn52xx;
	struct cvmx_npei_pktx_instr_baoff_dbell_s cn56xx;
};

union cvmx_npei_pktx_instr_fifo_rsize {
	uint64_t u64;
	struct cvmx_npei_pktx_instr_fifo_rsize_s {
		uint64_t max:9;
		uint64_t rrp:9;
		uint64_t wrp:9;
		uint64_t fcnt:5;
		uint64_t rsize:32;
	} s;
	struct cvmx_npei_pktx_instr_fifo_rsize_s cn52xx;
	struct cvmx_npei_pktx_instr_fifo_rsize_s cn56xx;
};

union cvmx_npei_pktx_instr_header {
	uint64_t u64;
	struct cvmx_npei_pktx_instr_header_s {
		uint64_t reserved_44_63:20;
		uint64_t pbp:1;
		uint64_t reserved_38_42:5;
		uint64_t rparmode:2;
		uint64_t reserved_35_35:1;
		uint64_t rskp_len:7;
		uint64_t reserved_22_27:6;
		uint64_t use_ihdr:1;
		uint64_t reserved_16_20:5;
		uint64_t par_mode:2;
		uint64_t reserved_13_13:1;
		uint64_t skp_len:7;
		uint64_t reserved_0_5:6;
	} s;
	struct cvmx_npei_pktx_instr_header_s cn52xx;
	struct cvmx_npei_pktx_instr_header_s cn56xx;
};

union cvmx_npei_pktx_slist_baddr {
	uint64_t u64;
	struct cvmx_npei_pktx_slist_baddr_s {
		uint64_t addr:60;
		uint64_t reserved_0_3:4;
	} s;
	struct cvmx_npei_pktx_slist_baddr_s cn52xx;
	struct cvmx_npei_pktx_slist_baddr_s cn56xx;
};

union cvmx_npei_pktx_slist_baoff_dbell {
	uint64_t u64;
	struct cvmx_npei_pktx_slist_baoff_dbell_s {
		uint64_t aoff:32;
		uint64_t dbell:32;
	} s;
	struct cvmx_npei_pktx_slist_baoff_dbell_s cn52xx;
	struct cvmx_npei_pktx_slist_baoff_dbell_s cn56xx;
};

union cvmx_npei_pktx_slist_fifo_rsize {
	uint64_t u64;
	struct cvmx_npei_pktx_slist_fifo_rsize_s {
		uint64_t reserved_32_63:32;
		uint64_t rsize:32;
	} s;
	struct cvmx_npei_pktx_slist_fifo_rsize_s cn52xx;
	struct cvmx_npei_pktx_slist_fifo_rsize_s cn56xx;
};

union cvmx_npei_pkt_cnt_int {
	uint64_t u64;
	struct cvmx_npei_pkt_cnt_int_s {
		uint64_t reserved_32_63:32;
		uint64_t port:32;
	} s;
	struct cvmx_npei_pkt_cnt_int_s cn52xx;
	struct cvmx_npei_pkt_cnt_int_s cn56xx;
};

union cvmx_npei_pkt_cnt_int_enb {
	uint64_t u64;
	struct cvmx_npei_pkt_cnt_int_enb_s {
		uint64_t reserved_32_63:32;
		uint64_t port:32;
	} s;
	struct cvmx_npei_pkt_cnt_int_enb_s cn52xx;
	struct cvmx_npei_pkt_cnt_int_enb_s cn56xx;
};

union cvmx_npei_pkt_data_out_es {
	uint64_t u64;
	struct cvmx_npei_pkt_data_out_es_s {
		uint64_t es:64;
	} s;
	struct cvmx_npei_pkt_data_out_es_s cn52xx;
	struct cvmx_npei_pkt_data_out_es_s cn56xx;
};

union cvmx_npei_pkt_data_out_ns {
	uint64_t u64;
	struct cvmx_npei_pkt_data_out_ns_s {
		uint64_t reserved_32_63:32;
		uint64_t nsr:32;
	} s;
	struct cvmx_npei_pkt_data_out_ns_s cn52xx;
	struct cvmx_npei_pkt_data_out_ns_s cn56xx;
};

union cvmx_npei_pkt_data_out_ror {
	uint64_t u64;
	struct cvmx_npei_pkt_data_out_ror_s {
		uint64_t reserved_32_63:32;
		uint64_t ror:32;
	} s;
	struct cvmx_npei_pkt_data_out_ror_s cn52xx;
	struct cvmx_npei_pkt_data_out_ror_s cn56xx;
};

union cvmx_npei_pkt_dpaddr {
	uint64_t u64;
	struct cvmx_npei_pkt_dpaddr_s {
		uint64_t reserved_32_63:32;
		uint64_t dptr:32;
	} s;
	struct cvmx_npei_pkt_dpaddr_s cn52xx;
	struct cvmx_npei_pkt_dpaddr_s cn56xx;
};

union cvmx_npei_pkt_in_bp {
	uint64_t u64;
	struct cvmx_npei_pkt_in_bp_s {
		uint64_t reserved_32_63:32;
		uint64_t bp:32;
	} s;
	struct cvmx_npei_pkt_in_bp_s cn52xx;
	struct cvmx_npei_pkt_in_bp_s cn56xx;
};

union cvmx_npei_pkt_in_donex_cnts {
	uint64_t u64;
	struct cvmx_npei_pkt_in_donex_cnts_s {
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
	} s;
	struct cvmx_npei_pkt_in_donex_cnts_s cn52xx;
	struct cvmx_npei_pkt_in_donex_cnts_s cn56xx;
};

union cvmx_npei_pkt_in_instr_counts {
	uint64_t u64;
	struct cvmx_npei_pkt_in_instr_counts_s {
		uint64_t wr_cnt:32;
		uint64_t rd_cnt:32;
	} s;
	struct cvmx_npei_pkt_in_instr_counts_s cn52xx;
	struct cvmx_npei_pkt_in_instr_counts_s cn56xx;
};

union cvmx_npei_pkt_in_pcie_port {
	uint64_t u64;
	struct cvmx_npei_pkt_in_pcie_port_s {
		uint64_t pp:64;
	} s;
	struct cvmx_npei_pkt_in_pcie_port_s cn52xx;
	struct cvmx_npei_pkt_in_pcie_port_s cn56xx;
};

union cvmx_npei_pkt_input_control {
	uint64_t u64;
	struct cvmx_npei_pkt_input_control_s {
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
	} s;
	struct cvmx_npei_pkt_input_control_s cn52xx;
	struct cvmx_npei_pkt_input_control_s cn56xx;
};

union cvmx_npei_pkt_instr_enb {
	uint64_t u64;
	struct cvmx_npei_pkt_instr_enb_s {
		uint64_t reserved_32_63:32;
		uint64_t enb:32;
	} s;
	struct cvmx_npei_pkt_instr_enb_s cn52xx;
	struct cvmx_npei_pkt_instr_enb_s cn56xx;
};

union cvmx_npei_pkt_instr_rd_size {
	uint64_t u64;
	struct cvmx_npei_pkt_instr_rd_size_s {
		uint64_t rdsize:64;
	} s;
	struct cvmx_npei_pkt_instr_rd_size_s cn52xx;
	struct cvmx_npei_pkt_instr_rd_size_s cn56xx;
};

union cvmx_npei_pkt_instr_size {
	uint64_t u64;
	struct cvmx_npei_pkt_instr_size_s {
		uint64_t reserved_32_63:32;
		uint64_t is_64b:32;
	} s;
	struct cvmx_npei_pkt_instr_size_s cn52xx;
	struct cvmx_npei_pkt_instr_size_s cn56xx;
};

union cvmx_npei_pkt_int_levels {
	uint64_t u64;
	struct cvmx_npei_pkt_int_levels_s {
		uint64_t reserved_54_63:10;
		uint64_t time:22;
		uint64_t cnt:32;
	} s;
	struct cvmx_npei_pkt_int_levels_s cn52xx;
	struct cvmx_npei_pkt_int_levels_s cn56xx;
};

union cvmx_npei_pkt_iptr {
	uint64_t u64;
	struct cvmx_npei_pkt_iptr_s {
		uint64_t reserved_32_63:32;
		uint64_t iptr:32;
	} s;
	struct cvmx_npei_pkt_iptr_s cn52xx;
	struct cvmx_npei_pkt_iptr_s cn56xx;
};

union cvmx_npei_pkt_out_bmode {
	uint64_t u64;
	struct cvmx_npei_pkt_out_bmode_s {
		uint64_t reserved_32_63:32;
		uint64_t bmode:32;
	} s;
	struct cvmx_npei_pkt_out_bmode_s cn52xx;
	struct cvmx_npei_pkt_out_bmode_s cn56xx;
};

union cvmx_npei_pkt_out_enb {
	uint64_t u64;
	struct cvmx_npei_pkt_out_enb_s {
		uint64_t reserved_32_63:32;
		uint64_t enb:32;
	} s;
	struct cvmx_npei_pkt_out_enb_s cn52xx;
	struct cvmx_npei_pkt_out_enb_s cn56xx;
};

union cvmx_npei_pkt_output_wmark {
	uint64_t u64;
	struct cvmx_npei_pkt_output_wmark_s {
		uint64_t reserved_32_63:32;
		uint64_t wmark:32;
	} s;
	struct cvmx_npei_pkt_output_wmark_s cn52xx;
	struct cvmx_npei_pkt_output_wmark_s cn56xx;
};

union cvmx_npei_pkt_pcie_port {
	uint64_t u64;
	struct cvmx_npei_pkt_pcie_port_s {
		uint64_t pp:64;
	} s;
	struct cvmx_npei_pkt_pcie_port_s cn52xx;
	struct cvmx_npei_pkt_pcie_port_s cn56xx;
};

union cvmx_npei_pkt_port_in_rst {
	uint64_t u64;
	struct cvmx_npei_pkt_port_in_rst_s {
		uint64_t in_rst:32;
		uint64_t out_rst:32;
	} s;
	struct cvmx_npei_pkt_port_in_rst_s cn52xx;
	struct cvmx_npei_pkt_port_in_rst_s cn56xx;
};

union cvmx_npei_pkt_slist_es {
	uint64_t u64;
	struct cvmx_npei_pkt_slist_es_s {
		uint64_t es:64;
	} s;
	struct cvmx_npei_pkt_slist_es_s cn52xx;
	struct cvmx_npei_pkt_slist_es_s cn56xx;
};

union cvmx_npei_pkt_slist_id_size {
	uint64_t u64;
	struct cvmx_npei_pkt_slist_id_size_s {
		uint64_t reserved_23_63:41;
		uint64_t isize:7;
		uint64_t bsize:16;
	} s;
	struct cvmx_npei_pkt_slist_id_size_s cn52xx;
	struct cvmx_npei_pkt_slist_id_size_s cn56xx;
};

union cvmx_npei_pkt_slist_ns {
	uint64_t u64;
	struct cvmx_npei_pkt_slist_ns_s {
		uint64_t reserved_32_63:32;
		uint64_t nsr:32;
	} s;
	struct cvmx_npei_pkt_slist_ns_s cn52xx;
	struct cvmx_npei_pkt_slist_ns_s cn56xx;
};

union cvmx_npei_pkt_slist_ror {
	uint64_t u64;
	struct cvmx_npei_pkt_slist_ror_s {
		uint64_t reserved_32_63:32;
		uint64_t ror:32;
	} s;
	struct cvmx_npei_pkt_slist_ror_s cn52xx;
	struct cvmx_npei_pkt_slist_ror_s cn56xx;
};

union cvmx_npei_pkt_time_int {
	uint64_t u64;
	struct cvmx_npei_pkt_time_int_s {
		uint64_t reserved_32_63:32;
		uint64_t port:32;
	} s;
	struct cvmx_npei_pkt_time_int_s cn52xx;
	struct cvmx_npei_pkt_time_int_s cn56xx;
};

union cvmx_npei_pkt_time_int_enb {
	uint64_t u64;
	struct cvmx_npei_pkt_time_int_enb_s {
		uint64_t reserved_32_63:32;
		uint64_t port:32;
	} s;
	struct cvmx_npei_pkt_time_int_enb_s cn52xx;
	struct cvmx_npei_pkt_time_int_enb_s cn56xx;
};

union cvmx_npei_rsl_int_blocks {
	uint64_t u64;
	struct cvmx_npei_rsl_int_blocks_s {
		uint64_t reserved_31_63:33;
		uint64_t iob:1;
		uint64_t lmc1:1;
		uint64_t agl:1;
		uint64_t reserved_24_27:4;
		uint64_t asxpcs1:1;
		uint64_t asxpcs0:1;
		uint64_t reserved_21_21:1;
		uint64_t pip:1;
		uint64_t spx1:1;
		uint64_t spx0:1;
		uint64_t lmc0:1;
		uint64_t l2c:1;
		uint64_t usb1:1;
		uint64_t rad:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t reserved_8_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t npei:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
	} s;
	struct cvmx_npei_rsl_int_blocks_s cn52xx;
	struct cvmx_npei_rsl_int_blocks_s cn52xxp1;
	struct cvmx_npei_rsl_int_blocks_s cn56xx;
	struct cvmx_npei_rsl_int_blocks_s cn56xxp1;
};

union cvmx_npei_scratch_1 {
	uint64_t u64;
	struct cvmx_npei_scratch_1_s {
		uint64_t data:64;
	} s;
	struct cvmx_npei_scratch_1_s cn52xx;
	struct cvmx_npei_scratch_1_s cn52xxp1;
	struct cvmx_npei_scratch_1_s cn56xx;
	struct cvmx_npei_scratch_1_s cn56xxp1;
};

union cvmx_npei_state1 {
	uint64_t u64;
	struct cvmx_npei_state1_s {
		uint64_t cpl1:12;
		uint64_t cpl0:12;
		uint64_t arb:1;
		uint64_t csr:39;
	} s;
	struct cvmx_npei_state1_s cn52xx;
	struct cvmx_npei_state1_s cn52xxp1;
	struct cvmx_npei_state1_s cn56xx;
	struct cvmx_npei_state1_s cn56xxp1;
};

union cvmx_npei_state2 {
	uint64_t u64;
	struct cvmx_npei_state2_s {
		uint64_t reserved_48_63:16;
		uint64_t npei:1;
		uint64_t rac:1;
		uint64_t csm1:15;
		uint64_t csm0:15;
		uint64_t nnp0:8;
		uint64_t nnd:8;
	} s;
	struct cvmx_npei_state2_s cn52xx;
	struct cvmx_npei_state2_s cn52xxp1;
	struct cvmx_npei_state2_s cn56xx;
	struct cvmx_npei_state2_s cn56xxp1;
};

union cvmx_npei_state3 {
	uint64_t u64;
	struct cvmx_npei_state3_s {
		uint64_t reserved_56_63:8;
		uint64_t psm1:15;
		uint64_t psm0:15;
		uint64_t nsm1:13;
		uint64_t nsm0:13;
	} s;
	struct cvmx_npei_state3_s cn52xx;
	struct cvmx_npei_state3_s cn52xxp1;
	struct cvmx_npei_state3_s cn56xx;
	struct cvmx_npei_state3_s cn56xxp1;
};

union cvmx_npei_win_rd_addr {
	uint64_t u64;
	struct cvmx_npei_win_rd_addr_s {
		uint64_t reserved_51_63:13;
		uint64_t ld_cmd:2;
		uint64_t iobit:1;
		uint64_t rd_addr:48;
	} s;
	struct cvmx_npei_win_rd_addr_s cn52xx;
	struct cvmx_npei_win_rd_addr_s cn52xxp1;
	struct cvmx_npei_win_rd_addr_s cn56xx;
	struct cvmx_npei_win_rd_addr_s cn56xxp1;
};

union cvmx_npei_win_rd_data {
	uint64_t u64;
	struct cvmx_npei_win_rd_data_s {
		uint64_t rd_data:64;
	} s;
	struct cvmx_npei_win_rd_data_s cn52xx;
	struct cvmx_npei_win_rd_data_s cn52xxp1;
	struct cvmx_npei_win_rd_data_s cn56xx;
	struct cvmx_npei_win_rd_data_s cn56xxp1;
};

union cvmx_npei_win_wr_addr {
	uint64_t u64;
	struct cvmx_npei_win_wr_addr_s {
		uint64_t reserved_49_63:15;
		uint64_t iobit:1;
		uint64_t wr_addr:46;
		uint64_t reserved_0_1:2;
	} s;
	struct cvmx_npei_win_wr_addr_s cn52xx;
	struct cvmx_npei_win_wr_addr_s cn52xxp1;
	struct cvmx_npei_win_wr_addr_s cn56xx;
	struct cvmx_npei_win_wr_addr_s cn56xxp1;
};

union cvmx_npei_win_wr_data {
	uint64_t u64;
	struct cvmx_npei_win_wr_data_s {
		uint64_t wr_data:64;
	} s;
	struct cvmx_npei_win_wr_data_s cn52xx;
	struct cvmx_npei_win_wr_data_s cn52xxp1;
	struct cvmx_npei_win_wr_data_s cn56xx;
	struct cvmx_npei_win_wr_data_s cn56xxp1;
};

union cvmx_npei_win_wr_mask {
	uint64_t u64;
	struct cvmx_npei_win_wr_mask_s {
		uint64_t reserved_8_63:56;
		uint64_t wr_mask:8;
	} s;
	struct cvmx_npei_win_wr_mask_s cn52xx;
	struct cvmx_npei_win_wr_mask_s cn52xxp1;
	struct cvmx_npei_win_wr_mask_s cn56xx;
	struct cvmx_npei_win_wr_mask_s cn56xxp1;
};

union cvmx_npei_window_ctl {
	uint64_t u64;
	struct cvmx_npei_window_ctl_s {
		uint64_t reserved_32_63:32;
		uint64_t time:32;
	} s;
	struct cvmx_npei_window_ctl_s cn52xx;
	struct cvmx_npei_window_ctl_s cn52xxp1;
	struct cvmx_npei_window_ctl_s cn56xx;
	struct cvmx_npei_window_ctl_s cn56xxp1;
};

#endif
