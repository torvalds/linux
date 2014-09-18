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

#ifndef __CVMX_NPI_DEFS_H__
#define __CVMX_NPI_DEFS_H__

#define CVMX_NPI_BASE_ADDR_INPUT0 CVMX_NPI_BASE_ADDR_INPUTX(0)
#define CVMX_NPI_BASE_ADDR_INPUT1 CVMX_NPI_BASE_ADDR_INPUTX(1)
#define CVMX_NPI_BASE_ADDR_INPUT2 CVMX_NPI_BASE_ADDR_INPUTX(2)
#define CVMX_NPI_BASE_ADDR_INPUT3 CVMX_NPI_BASE_ADDR_INPUTX(3)
#define CVMX_NPI_BASE_ADDR_INPUTX(offset) (CVMX_ADD_IO_SEG(0x00011F0000000070ull) + ((offset) & 3) * 16)
#define CVMX_NPI_BASE_ADDR_OUTPUT0 CVMX_NPI_BASE_ADDR_OUTPUTX(0)
#define CVMX_NPI_BASE_ADDR_OUTPUT1 CVMX_NPI_BASE_ADDR_OUTPUTX(1)
#define CVMX_NPI_BASE_ADDR_OUTPUT2 CVMX_NPI_BASE_ADDR_OUTPUTX(2)
#define CVMX_NPI_BASE_ADDR_OUTPUT3 CVMX_NPI_BASE_ADDR_OUTPUTX(3)
#define CVMX_NPI_BASE_ADDR_OUTPUTX(offset) (CVMX_ADD_IO_SEG(0x00011F00000000B8ull) + ((offset) & 3) * 8)
#define CVMX_NPI_BIST_STATUS (CVMX_ADD_IO_SEG(0x00011F00000003F8ull))
#define CVMX_NPI_BUFF_SIZE_OUTPUT0 CVMX_NPI_BUFF_SIZE_OUTPUTX(0)
#define CVMX_NPI_BUFF_SIZE_OUTPUT1 CVMX_NPI_BUFF_SIZE_OUTPUTX(1)
#define CVMX_NPI_BUFF_SIZE_OUTPUT2 CVMX_NPI_BUFF_SIZE_OUTPUTX(2)
#define CVMX_NPI_BUFF_SIZE_OUTPUT3 CVMX_NPI_BUFF_SIZE_OUTPUTX(3)
#define CVMX_NPI_BUFF_SIZE_OUTPUTX(offset) (CVMX_ADD_IO_SEG(0x00011F00000000E0ull) + ((offset) & 3) * 8)
#define CVMX_NPI_COMP_CTL (CVMX_ADD_IO_SEG(0x00011F0000000218ull))
#define CVMX_NPI_CTL_STATUS (CVMX_ADD_IO_SEG(0x00011F0000000010ull))
#define CVMX_NPI_DBG_SELECT (CVMX_ADD_IO_SEG(0x00011F0000000008ull))
#define CVMX_NPI_DMA_CONTROL (CVMX_ADD_IO_SEG(0x00011F0000000128ull))
#define CVMX_NPI_DMA_HIGHP_COUNTS (CVMX_ADD_IO_SEG(0x00011F0000000148ull))
#define CVMX_NPI_DMA_HIGHP_NADDR (CVMX_ADD_IO_SEG(0x00011F0000000158ull))
#define CVMX_NPI_DMA_LOWP_COUNTS (CVMX_ADD_IO_SEG(0x00011F0000000140ull))
#define CVMX_NPI_DMA_LOWP_NADDR (CVMX_ADD_IO_SEG(0x00011F0000000150ull))
#define CVMX_NPI_HIGHP_DBELL (CVMX_ADD_IO_SEG(0x00011F0000000120ull))
#define CVMX_NPI_HIGHP_IBUFF_SADDR (CVMX_ADD_IO_SEG(0x00011F0000000110ull))
#define CVMX_NPI_INPUT_CONTROL (CVMX_ADD_IO_SEG(0x00011F0000000138ull))
#define CVMX_NPI_INT_ENB (CVMX_ADD_IO_SEG(0x00011F0000000020ull))
#define CVMX_NPI_INT_SUM (CVMX_ADD_IO_SEG(0x00011F0000000018ull))
#define CVMX_NPI_LOWP_DBELL (CVMX_ADD_IO_SEG(0x00011F0000000118ull))
#define CVMX_NPI_LOWP_IBUFF_SADDR (CVMX_ADD_IO_SEG(0x00011F0000000108ull))
#define CVMX_NPI_MEM_ACCESS_SUBID3 CVMX_NPI_MEM_ACCESS_SUBIDX(3)
#define CVMX_NPI_MEM_ACCESS_SUBID4 CVMX_NPI_MEM_ACCESS_SUBIDX(4)
#define CVMX_NPI_MEM_ACCESS_SUBID5 CVMX_NPI_MEM_ACCESS_SUBIDX(5)
#define CVMX_NPI_MEM_ACCESS_SUBID6 CVMX_NPI_MEM_ACCESS_SUBIDX(6)
#define CVMX_NPI_MEM_ACCESS_SUBIDX(offset) (CVMX_ADD_IO_SEG(0x00011F0000000028ull) + ((offset) & 7) * 8 - 8*3)
#define CVMX_NPI_MSI_RCV (0x0000000000000190ull)
#define CVMX_NPI_NPI_MSI_RCV (CVMX_ADD_IO_SEG(0x00011F0000001190ull))
#define CVMX_NPI_NUM_DESC_OUTPUT0 CVMX_NPI_NUM_DESC_OUTPUTX(0)
#define CVMX_NPI_NUM_DESC_OUTPUT1 CVMX_NPI_NUM_DESC_OUTPUTX(1)
#define CVMX_NPI_NUM_DESC_OUTPUT2 CVMX_NPI_NUM_DESC_OUTPUTX(2)
#define CVMX_NPI_NUM_DESC_OUTPUT3 CVMX_NPI_NUM_DESC_OUTPUTX(3)
#define CVMX_NPI_NUM_DESC_OUTPUTX(offset) (CVMX_ADD_IO_SEG(0x00011F0000000050ull) + ((offset) & 3) * 8)
#define CVMX_NPI_OUTPUT_CONTROL (CVMX_ADD_IO_SEG(0x00011F0000000100ull))
#define CVMX_NPI_P0_DBPAIR_ADDR CVMX_NPI_PX_DBPAIR_ADDR(0)
#define CVMX_NPI_P0_INSTR_ADDR CVMX_NPI_PX_INSTR_ADDR(0)
#define CVMX_NPI_P0_INSTR_CNTS CVMX_NPI_PX_INSTR_CNTS(0)
#define CVMX_NPI_P0_PAIR_CNTS CVMX_NPI_PX_PAIR_CNTS(0)
#define CVMX_NPI_P1_DBPAIR_ADDR CVMX_NPI_PX_DBPAIR_ADDR(1)
#define CVMX_NPI_P1_INSTR_ADDR CVMX_NPI_PX_INSTR_ADDR(1)
#define CVMX_NPI_P1_INSTR_CNTS CVMX_NPI_PX_INSTR_CNTS(1)
#define CVMX_NPI_P1_PAIR_CNTS CVMX_NPI_PX_PAIR_CNTS(1)
#define CVMX_NPI_P2_DBPAIR_ADDR CVMX_NPI_PX_DBPAIR_ADDR(2)
#define CVMX_NPI_P2_INSTR_ADDR CVMX_NPI_PX_INSTR_ADDR(2)
#define CVMX_NPI_P2_INSTR_CNTS CVMX_NPI_PX_INSTR_CNTS(2)
#define CVMX_NPI_P2_PAIR_CNTS CVMX_NPI_PX_PAIR_CNTS(2)
#define CVMX_NPI_P3_DBPAIR_ADDR CVMX_NPI_PX_DBPAIR_ADDR(3)
#define CVMX_NPI_P3_INSTR_ADDR CVMX_NPI_PX_INSTR_ADDR(3)
#define CVMX_NPI_P3_INSTR_CNTS CVMX_NPI_PX_INSTR_CNTS(3)
#define CVMX_NPI_P3_PAIR_CNTS CVMX_NPI_PX_PAIR_CNTS(3)
#define CVMX_NPI_PCI_BAR1_INDEXX(offset) (CVMX_ADD_IO_SEG(0x00011F0000001100ull) + ((offset) & 31) * 4)
#define CVMX_NPI_PCI_BIST_REG (CVMX_ADD_IO_SEG(0x00011F00000011C0ull))
#define CVMX_NPI_PCI_BURST_SIZE (CVMX_ADD_IO_SEG(0x00011F00000000D8ull))
#define CVMX_NPI_PCI_CFG00 (CVMX_ADD_IO_SEG(0x00011F0000001800ull))
#define CVMX_NPI_PCI_CFG01 (CVMX_ADD_IO_SEG(0x00011F0000001804ull))
#define CVMX_NPI_PCI_CFG02 (CVMX_ADD_IO_SEG(0x00011F0000001808ull))
#define CVMX_NPI_PCI_CFG03 (CVMX_ADD_IO_SEG(0x00011F000000180Cull))
#define CVMX_NPI_PCI_CFG04 (CVMX_ADD_IO_SEG(0x00011F0000001810ull))
#define CVMX_NPI_PCI_CFG05 (CVMX_ADD_IO_SEG(0x00011F0000001814ull))
#define CVMX_NPI_PCI_CFG06 (CVMX_ADD_IO_SEG(0x00011F0000001818ull))
#define CVMX_NPI_PCI_CFG07 (CVMX_ADD_IO_SEG(0x00011F000000181Cull))
#define CVMX_NPI_PCI_CFG08 (CVMX_ADD_IO_SEG(0x00011F0000001820ull))
#define CVMX_NPI_PCI_CFG09 (CVMX_ADD_IO_SEG(0x00011F0000001824ull))
#define CVMX_NPI_PCI_CFG10 (CVMX_ADD_IO_SEG(0x00011F0000001828ull))
#define CVMX_NPI_PCI_CFG11 (CVMX_ADD_IO_SEG(0x00011F000000182Cull))
#define CVMX_NPI_PCI_CFG12 (CVMX_ADD_IO_SEG(0x00011F0000001830ull))
#define CVMX_NPI_PCI_CFG13 (CVMX_ADD_IO_SEG(0x00011F0000001834ull))
#define CVMX_NPI_PCI_CFG15 (CVMX_ADD_IO_SEG(0x00011F000000183Cull))
#define CVMX_NPI_PCI_CFG16 (CVMX_ADD_IO_SEG(0x00011F0000001840ull))
#define CVMX_NPI_PCI_CFG17 (CVMX_ADD_IO_SEG(0x00011F0000001844ull))
#define CVMX_NPI_PCI_CFG18 (CVMX_ADD_IO_SEG(0x00011F0000001848ull))
#define CVMX_NPI_PCI_CFG19 (CVMX_ADD_IO_SEG(0x00011F000000184Cull))
#define CVMX_NPI_PCI_CFG20 (CVMX_ADD_IO_SEG(0x00011F0000001850ull))
#define CVMX_NPI_PCI_CFG21 (CVMX_ADD_IO_SEG(0x00011F0000001854ull))
#define CVMX_NPI_PCI_CFG22 (CVMX_ADD_IO_SEG(0x00011F0000001858ull))
#define CVMX_NPI_PCI_CFG56 (CVMX_ADD_IO_SEG(0x00011F00000018E0ull))
#define CVMX_NPI_PCI_CFG57 (CVMX_ADD_IO_SEG(0x00011F00000018E4ull))
#define CVMX_NPI_PCI_CFG58 (CVMX_ADD_IO_SEG(0x00011F00000018E8ull))
#define CVMX_NPI_PCI_CFG59 (CVMX_ADD_IO_SEG(0x00011F00000018ECull))
#define CVMX_NPI_PCI_CFG60 (CVMX_ADD_IO_SEG(0x00011F00000018F0ull))
#define CVMX_NPI_PCI_CFG61 (CVMX_ADD_IO_SEG(0x00011F00000018F4ull))
#define CVMX_NPI_PCI_CFG62 (CVMX_ADD_IO_SEG(0x00011F00000018F8ull))
#define CVMX_NPI_PCI_CFG63 (CVMX_ADD_IO_SEG(0x00011F00000018FCull))
#define CVMX_NPI_PCI_CNT_REG (CVMX_ADD_IO_SEG(0x00011F00000011B8ull))
#define CVMX_NPI_PCI_CTL_STATUS_2 (CVMX_ADD_IO_SEG(0x00011F000000118Cull))
#define CVMX_NPI_PCI_INT_ARB_CFG (CVMX_ADD_IO_SEG(0x00011F0000000130ull))
#define CVMX_NPI_PCI_INT_ENB2 (CVMX_ADD_IO_SEG(0x00011F00000011A0ull))
#define CVMX_NPI_PCI_INT_SUM2 (CVMX_ADD_IO_SEG(0x00011F0000001198ull))
#define CVMX_NPI_PCI_READ_CMD (CVMX_ADD_IO_SEG(0x00011F0000000048ull))
#define CVMX_NPI_PCI_READ_CMD_6 (CVMX_ADD_IO_SEG(0x00011F0000001180ull))
#define CVMX_NPI_PCI_READ_CMD_C (CVMX_ADD_IO_SEG(0x00011F0000001184ull))
#define CVMX_NPI_PCI_READ_CMD_E (CVMX_ADD_IO_SEG(0x00011F0000001188ull))
#define CVMX_NPI_PCI_SCM_REG (CVMX_ADD_IO_SEG(0x00011F00000011A8ull))
#define CVMX_NPI_PCI_TSR_REG (CVMX_ADD_IO_SEG(0x00011F00000011B0ull))
#define CVMX_NPI_PORT32_INSTR_HDR (CVMX_ADD_IO_SEG(0x00011F00000001F8ull))
#define CVMX_NPI_PORT33_INSTR_HDR (CVMX_ADD_IO_SEG(0x00011F0000000200ull))
#define CVMX_NPI_PORT34_INSTR_HDR (CVMX_ADD_IO_SEG(0x00011F0000000208ull))
#define CVMX_NPI_PORT35_INSTR_HDR (CVMX_ADD_IO_SEG(0x00011F0000000210ull))
#define CVMX_NPI_PORT_BP_CONTROL (CVMX_ADD_IO_SEG(0x00011F00000001F0ull))
#define CVMX_NPI_PX_DBPAIR_ADDR(offset) (CVMX_ADD_IO_SEG(0x00011F0000000180ull) + ((offset) & 3) * 8)
#define CVMX_NPI_PX_INSTR_ADDR(offset) (CVMX_ADD_IO_SEG(0x00011F00000001C0ull) + ((offset) & 3) * 8)
#define CVMX_NPI_PX_INSTR_CNTS(offset) (CVMX_ADD_IO_SEG(0x00011F00000001A0ull) + ((offset) & 3) * 8)
#define CVMX_NPI_PX_PAIR_CNTS(offset) (CVMX_ADD_IO_SEG(0x00011F0000000160ull) + ((offset) & 3) * 8)
#define CVMX_NPI_RSL_INT_BLOCKS (CVMX_ADD_IO_SEG(0x00011F0000000000ull))
#define CVMX_NPI_SIZE_INPUT0 CVMX_NPI_SIZE_INPUTX(0)
#define CVMX_NPI_SIZE_INPUT1 CVMX_NPI_SIZE_INPUTX(1)
#define CVMX_NPI_SIZE_INPUT2 CVMX_NPI_SIZE_INPUTX(2)
#define CVMX_NPI_SIZE_INPUT3 CVMX_NPI_SIZE_INPUTX(3)
#define CVMX_NPI_SIZE_INPUTX(offset) (CVMX_ADD_IO_SEG(0x00011F0000000078ull) + ((offset) & 3) * 16)
#define CVMX_NPI_WIN_READ_TO (CVMX_ADD_IO_SEG(0x00011F00000001E0ull))

union cvmx_npi_base_addr_inputx {
	uint64_t u64;
	struct cvmx_npi_base_addr_inputx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t baddr:61;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t baddr:61;
#endif
	} s;
	struct cvmx_npi_base_addr_inputx_s cn30xx;
	struct cvmx_npi_base_addr_inputx_s cn31xx;
	struct cvmx_npi_base_addr_inputx_s cn38xx;
	struct cvmx_npi_base_addr_inputx_s cn38xxp2;
	struct cvmx_npi_base_addr_inputx_s cn50xx;
	struct cvmx_npi_base_addr_inputx_s cn58xx;
	struct cvmx_npi_base_addr_inputx_s cn58xxp1;
};

union cvmx_npi_base_addr_outputx {
	uint64_t u64;
	struct cvmx_npi_base_addr_outputx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t baddr:61;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t baddr:61;
#endif
	} s;
	struct cvmx_npi_base_addr_outputx_s cn30xx;
	struct cvmx_npi_base_addr_outputx_s cn31xx;
	struct cvmx_npi_base_addr_outputx_s cn38xx;
	struct cvmx_npi_base_addr_outputx_s cn38xxp2;
	struct cvmx_npi_base_addr_outputx_s cn50xx;
	struct cvmx_npi_base_addr_outputx_s cn58xx;
	struct cvmx_npi_base_addr_outputx_s cn58xxp1;
};

union cvmx_npi_bist_status {
	uint64_t u64;
	struct cvmx_npi_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t csr_bs:1;
		uint64_t dif_bs:1;
		uint64_t rdp_bs:1;
		uint64_t pcnc_bs:1;
		uint64_t pcn_bs:1;
		uint64_t rdn_bs:1;
		uint64_t pcac_bs:1;
		uint64_t pcad_bs:1;
		uint64_t rdnl_bs:1;
		uint64_t pgf_bs:1;
		uint64_t pig_bs:1;
		uint64_t pof0_bs:1;
		uint64_t pof1_bs:1;
		uint64_t pof2_bs:1;
		uint64_t pof3_bs:1;
		uint64_t pos_bs:1;
		uint64_t nus_bs:1;
		uint64_t dob_bs:1;
		uint64_t pdf_bs:1;
		uint64_t dpi_bs:1;
#else
		uint64_t dpi_bs:1;
		uint64_t pdf_bs:1;
		uint64_t dob_bs:1;
		uint64_t nus_bs:1;
		uint64_t pos_bs:1;
		uint64_t pof3_bs:1;
		uint64_t pof2_bs:1;
		uint64_t pof1_bs:1;
		uint64_t pof0_bs:1;
		uint64_t pig_bs:1;
		uint64_t pgf_bs:1;
		uint64_t rdnl_bs:1;
		uint64_t pcad_bs:1;
		uint64_t pcac_bs:1;
		uint64_t rdn_bs:1;
		uint64_t pcn_bs:1;
		uint64_t pcnc_bs:1;
		uint64_t rdp_bs:1;
		uint64_t dif_bs:1;
		uint64_t csr_bs:1;
		uint64_t reserved_20_63:44;
#endif
	} s;
	struct cvmx_npi_bist_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t csr_bs:1;
		uint64_t dif_bs:1;
		uint64_t rdp_bs:1;
		uint64_t pcnc_bs:1;
		uint64_t pcn_bs:1;
		uint64_t rdn_bs:1;
		uint64_t pcac_bs:1;
		uint64_t pcad_bs:1;
		uint64_t rdnl_bs:1;
		uint64_t pgf_bs:1;
		uint64_t pig_bs:1;
		uint64_t pof0_bs:1;
		uint64_t reserved_5_7:3;
		uint64_t pos_bs:1;
		uint64_t nus_bs:1;
		uint64_t dob_bs:1;
		uint64_t pdf_bs:1;
		uint64_t dpi_bs:1;
#else
		uint64_t dpi_bs:1;
		uint64_t pdf_bs:1;
		uint64_t dob_bs:1;
		uint64_t nus_bs:1;
		uint64_t pos_bs:1;
		uint64_t reserved_5_7:3;
		uint64_t pof0_bs:1;
		uint64_t pig_bs:1;
		uint64_t pgf_bs:1;
		uint64_t rdnl_bs:1;
		uint64_t pcad_bs:1;
		uint64_t pcac_bs:1;
		uint64_t rdn_bs:1;
		uint64_t pcn_bs:1;
		uint64_t pcnc_bs:1;
		uint64_t rdp_bs:1;
		uint64_t dif_bs:1;
		uint64_t csr_bs:1;
		uint64_t reserved_20_63:44;
#endif
	} cn30xx;
	struct cvmx_npi_bist_status_s cn31xx;
	struct cvmx_npi_bist_status_s cn38xx;
	struct cvmx_npi_bist_status_s cn38xxp2;
	struct cvmx_npi_bist_status_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t csr_bs:1;
		uint64_t dif_bs:1;
		uint64_t rdp_bs:1;
		uint64_t pcnc_bs:1;
		uint64_t pcn_bs:1;
		uint64_t rdn_bs:1;
		uint64_t pcac_bs:1;
		uint64_t pcad_bs:1;
		uint64_t rdnl_bs:1;
		uint64_t pgf_bs:1;
		uint64_t pig_bs:1;
		uint64_t pof0_bs:1;
		uint64_t pof1_bs:1;
		uint64_t reserved_5_6:2;
		uint64_t pos_bs:1;
		uint64_t nus_bs:1;
		uint64_t dob_bs:1;
		uint64_t pdf_bs:1;
		uint64_t dpi_bs:1;
#else
		uint64_t dpi_bs:1;
		uint64_t pdf_bs:1;
		uint64_t dob_bs:1;
		uint64_t nus_bs:1;
		uint64_t pos_bs:1;
		uint64_t reserved_5_6:2;
		uint64_t pof1_bs:1;
		uint64_t pof0_bs:1;
		uint64_t pig_bs:1;
		uint64_t pgf_bs:1;
		uint64_t rdnl_bs:1;
		uint64_t pcad_bs:1;
		uint64_t pcac_bs:1;
		uint64_t rdn_bs:1;
		uint64_t pcn_bs:1;
		uint64_t pcnc_bs:1;
		uint64_t rdp_bs:1;
		uint64_t dif_bs:1;
		uint64_t csr_bs:1;
		uint64_t reserved_20_63:44;
#endif
	} cn50xx;
	struct cvmx_npi_bist_status_s cn58xx;
	struct cvmx_npi_bist_status_s cn58xxp1;
};

union cvmx_npi_buff_size_outputx {
	uint64_t u64;
	struct cvmx_npi_buff_size_outputx_s {
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
	struct cvmx_npi_buff_size_outputx_s cn30xx;
	struct cvmx_npi_buff_size_outputx_s cn31xx;
	struct cvmx_npi_buff_size_outputx_s cn38xx;
	struct cvmx_npi_buff_size_outputx_s cn38xxp2;
	struct cvmx_npi_buff_size_outputx_s cn50xx;
	struct cvmx_npi_buff_size_outputx_s cn58xx;
	struct cvmx_npi_buff_size_outputx_s cn58xxp1;
};

union cvmx_npi_comp_ctl {
	uint64_t u64;
	struct cvmx_npi_comp_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t pctl:5;
		uint64_t nctl:5;
#else
		uint64_t nctl:5;
		uint64_t pctl:5;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_npi_comp_ctl_s cn50xx;
	struct cvmx_npi_comp_ctl_s cn58xx;
	struct cvmx_npi_comp_ctl_s cn58xxp1;
};

union cvmx_npi_ctl_status {
	uint64_t u64;
	struct cvmx_npi_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_63_63:1;
		uint64_t chip_rev:8;
		uint64_t dis_pniw:1;
		uint64_t out3_enb:1;
		uint64_t out2_enb:1;
		uint64_t out1_enb:1;
		uint64_t out0_enb:1;
		uint64_t ins3_enb:1;
		uint64_t ins2_enb:1;
		uint64_t ins1_enb:1;
		uint64_t ins0_enb:1;
		uint64_t ins3_64b:1;
		uint64_t ins2_64b:1;
		uint64_t ins1_64b:1;
		uint64_t ins0_64b:1;
		uint64_t pci_wdis:1;
		uint64_t wait_com:1;
		uint64_t reserved_37_39:3;
		uint64_t max_word:5;
		uint64_t reserved_10_31:22;
		uint64_t timer:10;
#else
		uint64_t timer:10;
		uint64_t reserved_10_31:22;
		uint64_t max_word:5;
		uint64_t reserved_37_39:3;
		uint64_t wait_com:1;
		uint64_t pci_wdis:1;
		uint64_t ins0_64b:1;
		uint64_t ins1_64b:1;
		uint64_t ins2_64b:1;
		uint64_t ins3_64b:1;
		uint64_t ins0_enb:1;
		uint64_t ins1_enb:1;
		uint64_t ins2_enb:1;
		uint64_t ins3_enb:1;
		uint64_t out0_enb:1;
		uint64_t out1_enb:1;
		uint64_t out2_enb:1;
		uint64_t out3_enb:1;
		uint64_t dis_pniw:1;
		uint64_t chip_rev:8;
		uint64_t reserved_63_63:1;
#endif
	} s;
	struct cvmx_npi_ctl_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_63_63:1;
		uint64_t chip_rev:8;
		uint64_t dis_pniw:1;
		uint64_t reserved_51_53:3;
		uint64_t out0_enb:1;
		uint64_t reserved_47_49:3;
		uint64_t ins0_enb:1;
		uint64_t reserved_43_45:3;
		uint64_t ins0_64b:1;
		uint64_t pci_wdis:1;
		uint64_t wait_com:1;
		uint64_t reserved_37_39:3;
		uint64_t max_word:5;
		uint64_t reserved_10_31:22;
		uint64_t timer:10;
#else
		uint64_t timer:10;
		uint64_t reserved_10_31:22;
		uint64_t max_word:5;
		uint64_t reserved_37_39:3;
		uint64_t wait_com:1;
		uint64_t pci_wdis:1;
		uint64_t ins0_64b:1;
		uint64_t reserved_43_45:3;
		uint64_t ins0_enb:1;
		uint64_t reserved_47_49:3;
		uint64_t out0_enb:1;
		uint64_t reserved_51_53:3;
		uint64_t dis_pniw:1;
		uint64_t chip_rev:8;
		uint64_t reserved_63_63:1;
#endif
	} cn30xx;
	struct cvmx_npi_ctl_status_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_63_63:1;
		uint64_t chip_rev:8;
		uint64_t dis_pniw:1;
		uint64_t reserved_52_53:2;
		uint64_t out1_enb:1;
		uint64_t out0_enb:1;
		uint64_t reserved_48_49:2;
		uint64_t ins1_enb:1;
		uint64_t ins0_enb:1;
		uint64_t reserved_44_45:2;
		uint64_t ins1_64b:1;
		uint64_t ins0_64b:1;
		uint64_t pci_wdis:1;
		uint64_t wait_com:1;
		uint64_t reserved_37_39:3;
		uint64_t max_word:5;
		uint64_t reserved_10_31:22;
		uint64_t timer:10;
#else
		uint64_t timer:10;
		uint64_t reserved_10_31:22;
		uint64_t max_word:5;
		uint64_t reserved_37_39:3;
		uint64_t wait_com:1;
		uint64_t pci_wdis:1;
		uint64_t ins0_64b:1;
		uint64_t ins1_64b:1;
		uint64_t reserved_44_45:2;
		uint64_t ins0_enb:1;
		uint64_t ins1_enb:1;
		uint64_t reserved_48_49:2;
		uint64_t out0_enb:1;
		uint64_t out1_enb:1;
		uint64_t reserved_52_53:2;
		uint64_t dis_pniw:1;
		uint64_t chip_rev:8;
		uint64_t reserved_63_63:1;
#endif
	} cn31xx;
	struct cvmx_npi_ctl_status_s cn38xx;
	struct cvmx_npi_ctl_status_s cn38xxp2;
	struct cvmx_npi_ctl_status_cn31xx cn50xx;
	struct cvmx_npi_ctl_status_s cn58xx;
	struct cvmx_npi_ctl_status_s cn58xxp1;
};

union cvmx_npi_dbg_select {
	uint64_t u64;
	struct cvmx_npi_dbg_select_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t dbg_sel:16;
#else
		uint64_t dbg_sel:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_npi_dbg_select_s cn30xx;
	struct cvmx_npi_dbg_select_s cn31xx;
	struct cvmx_npi_dbg_select_s cn38xx;
	struct cvmx_npi_dbg_select_s cn38xxp2;
	struct cvmx_npi_dbg_select_s cn50xx;
	struct cvmx_npi_dbg_select_s cn58xx;
	struct cvmx_npi_dbg_select_s cn58xxp1;
};

union cvmx_npi_dma_control {
	uint64_t u64;
	struct cvmx_npi_dma_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t b0_lend:1;
		uint64_t dwb_denb:1;
		uint64_t dwb_ichk:9;
		uint64_t fpa_que:3;
		uint64_t o_add1:1;
		uint64_t o_ro:1;
		uint64_t o_ns:1;
		uint64_t o_es:2;
		uint64_t o_mode:1;
		uint64_t hp_enb:1;
		uint64_t lp_enb:1;
		uint64_t csize:14;
#else
		uint64_t csize:14;
		uint64_t lp_enb:1;
		uint64_t hp_enb:1;
		uint64_t o_mode:1;
		uint64_t o_es:2;
		uint64_t o_ns:1;
		uint64_t o_ro:1;
		uint64_t o_add1:1;
		uint64_t fpa_que:3;
		uint64_t dwb_ichk:9;
		uint64_t dwb_denb:1;
		uint64_t b0_lend:1;
		uint64_t reserved_36_63:28;
#endif
	} s;
	struct cvmx_npi_dma_control_s cn30xx;
	struct cvmx_npi_dma_control_s cn31xx;
	struct cvmx_npi_dma_control_s cn38xx;
	struct cvmx_npi_dma_control_s cn38xxp2;
	struct cvmx_npi_dma_control_s cn50xx;
	struct cvmx_npi_dma_control_s cn58xx;
	struct cvmx_npi_dma_control_s cn58xxp1;
};

union cvmx_npi_dma_highp_counts {
	uint64_t u64;
	struct cvmx_npi_dma_highp_counts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_39_63:25;
		uint64_t fcnt:7;
		uint64_t dbell:32;
#else
		uint64_t dbell:32;
		uint64_t fcnt:7;
		uint64_t reserved_39_63:25;
#endif
	} s;
	struct cvmx_npi_dma_highp_counts_s cn30xx;
	struct cvmx_npi_dma_highp_counts_s cn31xx;
	struct cvmx_npi_dma_highp_counts_s cn38xx;
	struct cvmx_npi_dma_highp_counts_s cn38xxp2;
	struct cvmx_npi_dma_highp_counts_s cn50xx;
	struct cvmx_npi_dma_highp_counts_s cn58xx;
	struct cvmx_npi_dma_highp_counts_s cn58xxp1;
};

union cvmx_npi_dma_highp_naddr {
	uint64_t u64;
	struct cvmx_npi_dma_highp_naddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t state:4;
		uint64_t addr:36;
#else
		uint64_t addr:36;
		uint64_t state:4;
		uint64_t reserved_40_63:24;
#endif
	} s;
	struct cvmx_npi_dma_highp_naddr_s cn30xx;
	struct cvmx_npi_dma_highp_naddr_s cn31xx;
	struct cvmx_npi_dma_highp_naddr_s cn38xx;
	struct cvmx_npi_dma_highp_naddr_s cn38xxp2;
	struct cvmx_npi_dma_highp_naddr_s cn50xx;
	struct cvmx_npi_dma_highp_naddr_s cn58xx;
	struct cvmx_npi_dma_highp_naddr_s cn58xxp1;
};

union cvmx_npi_dma_lowp_counts {
	uint64_t u64;
	struct cvmx_npi_dma_lowp_counts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_39_63:25;
		uint64_t fcnt:7;
		uint64_t dbell:32;
#else
		uint64_t dbell:32;
		uint64_t fcnt:7;
		uint64_t reserved_39_63:25;
#endif
	} s;
	struct cvmx_npi_dma_lowp_counts_s cn30xx;
	struct cvmx_npi_dma_lowp_counts_s cn31xx;
	struct cvmx_npi_dma_lowp_counts_s cn38xx;
	struct cvmx_npi_dma_lowp_counts_s cn38xxp2;
	struct cvmx_npi_dma_lowp_counts_s cn50xx;
	struct cvmx_npi_dma_lowp_counts_s cn58xx;
	struct cvmx_npi_dma_lowp_counts_s cn58xxp1;
};

union cvmx_npi_dma_lowp_naddr {
	uint64_t u64;
	struct cvmx_npi_dma_lowp_naddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t state:4;
		uint64_t addr:36;
#else
		uint64_t addr:36;
		uint64_t state:4;
		uint64_t reserved_40_63:24;
#endif
	} s;
	struct cvmx_npi_dma_lowp_naddr_s cn30xx;
	struct cvmx_npi_dma_lowp_naddr_s cn31xx;
	struct cvmx_npi_dma_lowp_naddr_s cn38xx;
	struct cvmx_npi_dma_lowp_naddr_s cn38xxp2;
	struct cvmx_npi_dma_lowp_naddr_s cn50xx;
	struct cvmx_npi_dma_lowp_naddr_s cn58xx;
	struct cvmx_npi_dma_lowp_naddr_s cn58xxp1;
};

union cvmx_npi_highp_dbell {
	uint64_t u64;
	struct cvmx_npi_highp_dbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t dbell:16;
#else
		uint64_t dbell:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_npi_highp_dbell_s cn30xx;
	struct cvmx_npi_highp_dbell_s cn31xx;
	struct cvmx_npi_highp_dbell_s cn38xx;
	struct cvmx_npi_highp_dbell_s cn38xxp2;
	struct cvmx_npi_highp_dbell_s cn50xx;
	struct cvmx_npi_highp_dbell_s cn58xx;
	struct cvmx_npi_highp_dbell_s cn58xxp1;
};

union cvmx_npi_highp_ibuff_saddr {
	uint64_t u64;
	struct cvmx_npi_highp_ibuff_saddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t saddr:36;
#else
		uint64_t saddr:36;
		uint64_t reserved_36_63:28;
#endif
	} s;
	struct cvmx_npi_highp_ibuff_saddr_s cn30xx;
	struct cvmx_npi_highp_ibuff_saddr_s cn31xx;
	struct cvmx_npi_highp_ibuff_saddr_s cn38xx;
	struct cvmx_npi_highp_ibuff_saddr_s cn38xxp2;
	struct cvmx_npi_highp_ibuff_saddr_s cn50xx;
	struct cvmx_npi_highp_ibuff_saddr_s cn58xx;
	struct cvmx_npi_highp_ibuff_saddr_s cn58xxp1;
};

union cvmx_npi_input_control {
	uint64_t u64;
	struct cvmx_npi_input_control_s {
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
	} s;
	struct cvmx_npi_input_control_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
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
		uint64_t reserved_22_63:42;
#endif
	} cn30xx;
	struct cvmx_npi_input_control_cn30xx cn31xx;
	struct cvmx_npi_input_control_s cn38xx;
	struct cvmx_npi_input_control_cn30xx cn38xxp2;
	struct cvmx_npi_input_control_s cn50xx;
	struct cvmx_npi_input_control_s cn58xx;
	struct cvmx_npi_input_control_s cn58xxp1;
};

union cvmx_npi_int_enb {
	uint64_t u64;
	struct cvmx_npi_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t q1_a_f:1;
		uint64_t q1_s_e:1;
		uint64_t pdf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pcf_p_e:1;
		uint64_t rdx_s_e:1;
		uint64_t rwx_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t com_a_f:1;
		uint64_t com_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t fcr_s_e:1;
		uint64_t iobdma:1;
		uint64_t p_dperr:1;
		uint64_t win_rto:1;
		uint64_t i3_pperr:1;
		uint64_t i2_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t i0_pperr:1;
		uint64_t p3_ptout:1;
		uint64_t p2_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t p0_ptout:1;
		uint64_t p3_pperr:1;
		uint64_t p2_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t p0_pperr:1;
		uint64_t g3_rtout:1;
		uint64_t g2_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t g0_rtout:1;
		uint64_t p3_perr:1;
		uint64_t p2_perr:1;
		uint64_t p1_perr:1;
		uint64_t p0_perr:1;
		uint64_t p3_rtout:1;
		uint64_t p2_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t p0_rtout:1;
		uint64_t i3_overf:1;
		uint64_t i2_overf:1;
		uint64_t i1_overf:1;
		uint64_t i0_overf:1;
		uint64_t i3_rtout:1;
		uint64_t i2_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t i0_rtout:1;
		uint64_t po3_2sml:1;
		uint64_t po2_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t po0_2sml:1;
		uint64_t pci_rsl:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
#else
		uint64_t rml_rto:1;
		uint64_t rml_wto:1;
		uint64_t pci_rsl:1;
		uint64_t po0_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t po2_2sml:1;
		uint64_t po3_2sml:1;
		uint64_t i0_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t i2_rtout:1;
		uint64_t i3_rtout:1;
		uint64_t i0_overf:1;
		uint64_t i1_overf:1;
		uint64_t i2_overf:1;
		uint64_t i3_overf:1;
		uint64_t p0_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t p2_rtout:1;
		uint64_t p3_rtout:1;
		uint64_t p0_perr:1;
		uint64_t p1_perr:1;
		uint64_t p2_perr:1;
		uint64_t p3_perr:1;
		uint64_t g0_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t g2_rtout:1;
		uint64_t g3_rtout:1;
		uint64_t p0_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t p2_pperr:1;
		uint64_t p3_pperr:1;
		uint64_t p0_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t p2_ptout:1;
		uint64_t p3_ptout:1;
		uint64_t i0_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t i2_pperr:1;
		uint64_t i3_pperr:1;
		uint64_t win_rto:1;
		uint64_t p_dperr:1;
		uint64_t iobdma:1;
		uint64_t fcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t com_s_e:1;
		uint64_t com_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t rwx_s_e:1;
		uint64_t rdx_s_e:1;
		uint64_t pcf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pdf_p_f:1;
		uint64_t q1_s_e:1;
		uint64_t q1_a_f:1;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_npi_int_enb_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t q1_a_f:1;
		uint64_t q1_s_e:1;
		uint64_t pdf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pcf_p_e:1;
		uint64_t rdx_s_e:1;
		uint64_t rwx_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t com_a_f:1;
		uint64_t com_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t fcr_s_e:1;
		uint64_t iobdma:1;
		uint64_t p_dperr:1;
		uint64_t win_rto:1;
		uint64_t reserved_36_38:3;
		uint64_t i0_pperr:1;
		uint64_t reserved_32_34:3;
		uint64_t p0_ptout:1;
		uint64_t reserved_28_30:3;
		uint64_t p0_pperr:1;
		uint64_t reserved_24_26:3;
		uint64_t g0_rtout:1;
		uint64_t reserved_20_22:3;
		uint64_t p0_perr:1;
		uint64_t reserved_16_18:3;
		uint64_t p0_rtout:1;
		uint64_t reserved_12_14:3;
		uint64_t i0_overf:1;
		uint64_t reserved_8_10:3;
		uint64_t i0_rtout:1;
		uint64_t reserved_4_6:3;
		uint64_t po0_2sml:1;
		uint64_t pci_rsl:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
#else
		uint64_t rml_rto:1;
		uint64_t rml_wto:1;
		uint64_t pci_rsl:1;
		uint64_t po0_2sml:1;
		uint64_t reserved_4_6:3;
		uint64_t i0_rtout:1;
		uint64_t reserved_8_10:3;
		uint64_t i0_overf:1;
		uint64_t reserved_12_14:3;
		uint64_t p0_rtout:1;
		uint64_t reserved_16_18:3;
		uint64_t p0_perr:1;
		uint64_t reserved_20_22:3;
		uint64_t g0_rtout:1;
		uint64_t reserved_24_26:3;
		uint64_t p0_pperr:1;
		uint64_t reserved_28_30:3;
		uint64_t p0_ptout:1;
		uint64_t reserved_32_34:3;
		uint64_t i0_pperr:1;
		uint64_t reserved_36_38:3;
		uint64_t win_rto:1;
		uint64_t p_dperr:1;
		uint64_t iobdma:1;
		uint64_t fcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t com_s_e:1;
		uint64_t com_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t rwx_s_e:1;
		uint64_t rdx_s_e:1;
		uint64_t pcf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pdf_p_f:1;
		uint64_t q1_s_e:1;
		uint64_t q1_a_f:1;
		uint64_t reserved_62_63:2;
#endif
	} cn30xx;
	struct cvmx_npi_int_enb_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t q1_a_f:1;
		uint64_t q1_s_e:1;
		uint64_t pdf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pcf_p_e:1;
		uint64_t rdx_s_e:1;
		uint64_t rwx_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t com_a_f:1;
		uint64_t com_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t fcr_s_e:1;
		uint64_t iobdma:1;
		uint64_t p_dperr:1;
		uint64_t win_rto:1;
		uint64_t reserved_37_38:2;
		uint64_t i1_pperr:1;
		uint64_t i0_pperr:1;
		uint64_t reserved_33_34:2;
		uint64_t p1_ptout:1;
		uint64_t p0_ptout:1;
		uint64_t reserved_29_30:2;
		uint64_t p1_pperr:1;
		uint64_t p0_pperr:1;
		uint64_t reserved_25_26:2;
		uint64_t g1_rtout:1;
		uint64_t g0_rtout:1;
		uint64_t reserved_21_22:2;
		uint64_t p1_perr:1;
		uint64_t p0_perr:1;
		uint64_t reserved_17_18:2;
		uint64_t p1_rtout:1;
		uint64_t p0_rtout:1;
		uint64_t reserved_13_14:2;
		uint64_t i1_overf:1;
		uint64_t i0_overf:1;
		uint64_t reserved_9_10:2;
		uint64_t i1_rtout:1;
		uint64_t i0_rtout:1;
		uint64_t reserved_5_6:2;
		uint64_t po1_2sml:1;
		uint64_t po0_2sml:1;
		uint64_t pci_rsl:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
#else
		uint64_t rml_rto:1;
		uint64_t rml_wto:1;
		uint64_t pci_rsl:1;
		uint64_t po0_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t reserved_5_6:2;
		uint64_t i0_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t reserved_9_10:2;
		uint64_t i0_overf:1;
		uint64_t i1_overf:1;
		uint64_t reserved_13_14:2;
		uint64_t p0_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t reserved_17_18:2;
		uint64_t p0_perr:1;
		uint64_t p1_perr:1;
		uint64_t reserved_21_22:2;
		uint64_t g0_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t reserved_25_26:2;
		uint64_t p0_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t reserved_29_30:2;
		uint64_t p0_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t reserved_33_34:2;
		uint64_t i0_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t reserved_37_38:2;
		uint64_t win_rto:1;
		uint64_t p_dperr:1;
		uint64_t iobdma:1;
		uint64_t fcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t com_s_e:1;
		uint64_t com_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t rwx_s_e:1;
		uint64_t rdx_s_e:1;
		uint64_t pcf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pdf_p_f:1;
		uint64_t q1_s_e:1;
		uint64_t q1_a_f:1;
		uint64_t reserved_62_63:2;
#endif
	} cn31xx;
	struct cvmx_npi_int_enb_s cn38xx;
	struct cvmx_npi_int_enb_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_42_63:22;
		uint64_t iobdma:1;
		uint64_t p_dperr:1;
		uint64_t win_rto:1;
		uint64_t i3_pperr:1;
		uint64_t i2_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t i0_pperr:1;
		uint64_t p3_ptout:1;
		uint64_t p2_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t p0_ptout:1;
		uint64_t p3_pperr:1;
		uint64_t p2_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t p0_pperr:1;
		uint64_t g3_rtout:1;
		uint64_t g2_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t g0_rtout:1;
		uint64_t p3_perr:1;
		uint64_t p2_perr:1;
		uint64_t p1_perr:1;
		uint64_t p0_perr:1;
		uint64_t p3_rtout:1;
		uint64_t p2_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t p0_rtout:1;
		uint64_t i3_overf:1;
		uint64_t i2_overf:1;
		uint64_t i1_overf:1;
		uint64_t i0_overf:1;
		uint64_t i3_rtout:1;
		uint64_t i2_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t i0_rtout:1;
		uint64_t po3_2sml:1;
		uint64_t po2_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t po0_2sml:1;
		uint64_t pci_rsl:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
#else
		uint64_t rml_rto:1;
		uint64_t rml_wto:1;
		uint64_t pci_rsl:1;
		uint64_t po0_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t po2_2sml:1;
		uint64_t po3_2sml:1;
		uint64_t i0_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t i2_rtout:1;
		uint64_t i3_rtout:1;
		uint64_t i0_overf:1;
		uint64_t i1_overf:1;
		uint64_t i2_overf:1;
		uint64_t i3_overf:1;
		uint64_t p0_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t p2_rtout:1;
		uint64_t p3_rtout:1;
		uint64_t p0_perr:1;
		uint64_t p1_perr:1;
		uint64_t p2_perr:1;
		uint64_t p3_perr:1;
		uint64_t g0_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t g2_rtout:1;
		uint64_t g3_rtout:1;
		uint64_t p0_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t p2_pperr:1;
		uint64_t p3_pperr:1;
		uint64_t p0_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t p2_ptout:1;
		uint64_t p3_ptout:1;
		uint64_t i0_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t i2_pperr:1;
		uint64_t i3_pperr:1;
		uint64_t win_rto:1;
		uint64_t p_dperr:1;
		uint64_t iobdma:1;
		uint64_t reserved_42_63:22;
#endif
	} cn38xxp2;
	struct cvmx_npi_int_enb_cn31xx cn50xx;
	struct cvmx_npi_int_enb_s cn58xx;
	struct cvmx_npi_int_enb_s cn58xxp1;
};

union cvmx_npi_int_sum {
	uint64_t u64;
	struct cvmx_npi_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t q1_a_f:1;
		uint64_t q1_s_e:1;
		uint64_t pdf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pcf_p_e:1;
		uint64_t rdx_s_e:1;
		uint64_t rwx_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t com_a_f:1;
		uint64_t com_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t fcr_s_e:1;
		uint64_t iobdma:1;
		uint64_t p_dperr:1;
		uint64_t win_rto:1;
		uint64_t i3_pperr:1;
		uint64_t i2_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t i0_pperr:1;
		uint64_t p3_ptout:1;
		uint64_t p2_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t p0_ptout:1;
		uint64_t p3_pperr:1;
		uint64_t p2_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t p0_pperr:1;
		uint64_t g3_rtout:1;
		uint64_t g2_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t g0_rtout:1;
		uint64_t p3_perr:1;
		uint64_t p2_perr:1;
		uint64_t p1_perr:1;
		uint64_t p0_perr:1;
		uint64_t p3_rtout:1;
		uint64_t p2_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t p0_rtout:1;
		uint64_t i3_overf:1;
		uint64_t i2_overf:1;
		uint64_t i1_overf:1;
		uint64_t i0_overf:1;
		uint64_t i3_rtout:1;
		uint64_t i2_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t i0_rtout:1;
		uint64_t po3_2sml:1;
		uint64_t po2_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t po0_2sml:1;
		uint64_t pci_rsl:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
#else
		uint64_t rml_rto:1;
		uint64_t rml_wto:1;
		uint64_t pci_rsl:1;
		uint64_t po0_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t po2_2sml:1;
		uint64_t po3_2sml:1;
		uint64_t i0_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t i2_rtout:1;
		uint64_t i3_rtout:1;
		uint64_t i0_overf:1;
		uint64_t i1_overf:1;
		uint64_t i2_overf:1;
		uint64_t i3_overf:1;
		uint64_t p0_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t p2_rtout:1;
		uint64_t p3_rtout:1;
		uint64_t p0_perr:1;
		uint64_t p1_perr:1;
		uint64_t p2_perr:1;
		uint64_t p3_perr:1;
		uint64_t g0_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t g2_rtout:1;
		uint64_t g3_rtout:1;
		uint64_t p0_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t p2_pperr:1;
		uint64_t p3_pperr:1;
		uint64_t p0_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t p2_ptout:1;
		uint64_t p3_ptout:1;
		uint64_t i0_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t i2_pperr:1;
		uint64_t i3_pperr:1;
		uint64_t win_rto:1;
		uint64_t p_dperr:1;
		uint64_t iobdma:1;
		uint64_t fcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t com_s_e:1;
		uint64_t com_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t rwx_s_e:1;
		uint64_t rdx_s_e:1;
		uint64_t pcf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pdf_p_f:1;
		uint64_t q1_s_e:1;
		uint64_t q1_a_f:1;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_npi_int_sum_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t q1_a_f:1;
		uint64_t q1_s_e:1;
		uint64_t pdf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pcf_p_e:1;
		uint64_t rdx_s_e:1;
		uint64_t rwx_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t com_a_f:1;
		uint64_t com_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t fcr_s_e:1;
		uint64_t iobdma:1;
		uint64_t p_dperr:1;
		uint64_t win_rto:1;
		uint64_t reserved_36_38:3;
		uint64_t i0_pperr:1;
		uint64_t reserved_32_34:3;
		uint64_t p0_ptout:1;
		uint64_t reserved_28_30:3;
		uint64_t p0_pperr:1;
		uint64_t reserved_24_26:3;
		uint64_t g0_rtout:1;
		uint64_t reserved_20_22:3;
		uint64_t p0_perr:1;
		uint64_t reserved_16_18:3;
		uint64_t p0_rtout:1;
		uint64_t reserved_12_14:3;
		uint64_t i0_overf:1;
		uint64_t reserved_8_10:3;
		uint64_t i0_rtout:1;
		uint64_t reserved_4_6:3;
		uint64_t po0_2sml:1;
		uint64_t pci_rsl:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
#else
		uint64_t rml_rto:1;
		uint64_t rml_wto:1;
		uint64_t pci_rsl:1;
		uint64_t po0_2sml:1;
		uint64_t reserved_4_6:3;
		uint64_t i0_rtout:1;
		uint64_t reserved_8_10:3;
		uint64_t i0_overf:1;
		uint64_t reserved_12_14:3;
		uint64_t p0_rtout:1;
		uint64_t reserved_16_18:3;
		uint64_t p0_perr:1;
		uint64_t reserved_20_22:3;
		uint64_t g0_rtout:1;
		uint64_t reserved_24_26:3;
		uint64_t p0_pperr:1;
		uint64_t reserved_28_30:3;
		uint64_t p0_ptout:1;
		uint64_t reserved_32_34:3;
		uint64_t i0_pperr:1;
		uint64_t reserved_36_38:3;
		uint64_t win_rto:1;
		uint64_t p_dperr:1;
		uint64_t iobdma:1;
		uint64_t fcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t com_s_e:1;
		uint64_t com_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t rwx_s_e:1;
		uint64_t rdx_s_e:1;
		uint64_t pcf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pdf_p_f:1;
		uint64_t q1_s_e:1;
		uint64_t q1_a_f:1;
		uint64_t reserved_62_63:2;
#endif
	} cn30xx;
	struct cvmx_npi_int_sum_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t q1_a_f:1;
		uint64_t q1_s_e:1;
		uint64_t pdf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pcf_p_e:1;
		uint64_t rdx_s_e:1;
		uint64_t rwx_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t com_a_f:1;
		uint64_t com_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t fcr_s_e:1;
		uint64_t iobdma:1;
		uint64_t p_dperr:1;
		uint64_t win_rto:1;
		uint64_t reserved_37_38:2;
		uint64_t i1_pperr:1;
		uint64_t i0_pperr:1;
		uint64_t reserved_33_34:2;
		uint64_t p1_ptout:1;
		uint64_t p0_ptout:1;
		uint64_t reserved_29_30:2;
		uint64_t p1_pperr:1;
		uint64_t p0_pperr:1;
		uint64_t reserved_25_26:2;
		uint64_t g1_rtout:1;
		uint64_t g0_rtout:1;
		uint64_t reserved_21_22:2;
		uint64_t p1_perr:1;
		uint64_t p0_perr:1;
		uint64_t reserved_17_18:2;
		uint64_t p1_rtout:1;
		uint64_t p0_rtout:1;
		uint64_t reserved_13_14:2;
		uint64_t i1_overf:1;
		uint64_t i0_overf:1;
		uint64_t reserved_9_10:2;
		uint64_t i1_rtout:1;
		uint64_t i0_rtout:1;
		uint64_t reserved_5_6:2;
		uint64_t po1_2sml:1;
		uint64_t po0_2sml:1;
		uint64_t pci_rsl:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
#else
		uint64_t rml_rto:1;
		uint64_t rml_wto:1;
		uint64_t pci_rsl:1;
		uint64_t po0_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t reserved_5_6:2;
		uint64_t i0_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t reserved_9_10:2;
		uint64_t i0_overf:1;
		uint64_t i1_overf:1;
		uint64_t reserved_13_14:2;
		uint64_t p0_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t reserved_17_18:2;
		uint64_t p0_perr:1;
		uint64_t p1_perr:1;
		uint64_t reserved_21_22:2;
		uint64_t g0_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t reserved_25_26:2;
		uint64_t p0_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t reserved_29_30:2;
		uint64_t p0_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t reserved_33_34:2;
		uint64_t i0_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t reserved_37_38:2;
		uint64_t win_rto:1;
		uint64_t p_dperr:1;
		uint64_t iobdma:1;
		uint64_t fcr_s_e:1;
		uint64_t fcr_a_f:1;
		uint64_t pcr_s_e:1;
		uint64_t pcr_a_f:1;
		uint64_t q2_s_e:1;
		uint64_t q2_a_f:1;
		uint64_t q3_s_e:1;
		uint64_t q3_a_f:1;
		uint64_t com_s_e:1;
		uint64_t com_a_f:1;
		uint64_t pnc_s_e:1;
		uint64_t pnc_a_f:1;
		uint64_t rwx_s_e:1;
		uint64_t rdx_s_e:1;
		uint64_t pcf_p_e:1;
		uint64_t pcf_p_f:1;
		uint64_t pdf_p_e:1;
		uint64_t pdf_p_f:1;
		uint64_t q1_s_e:1;
		uint64_t q1_a_f:1;
		uint64_t reserved_62_63:2;
#endif
	} cn31xx;
	struct cvmx_npi_int_sum_s cn38xx;
	struct cvmx_npi_int_sum_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_42_63:22;
		uint64_t iobdma:1;
		uint64_t p_dperr:1;
		uint64_t win_rto:1;
		uint64_t i3_pperr:1;
		uint64_t i2_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t i0_pperr:1;
		uint64_t p3_ptout:1;
		uint64_t p2_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t p0_ptout:1;
		uint64_t p3_pperr:1;
		uint64_t p2_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t p0_pperr:1;
		uint64_t g3_rtout:1;
		uint64_t g2_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t g0_rtout:1;
		uint64_t p3_perr:1;
		uint64_t p2_perr:1;
		uint64_t p1_perr:1;
		uint64_t p0_perr:1;
		uint64_t p3_rtout:1;
		uint64_t p2_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t p0_rtout:1;
		uint64_t i3_overf:1;
		uint64_t i2_overf:1;
		uint64_t i1_overf:1;
		uint64_t i0_overf:1;
		uint64_t i3_rtout:1;
		uint64_t i2_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t i0_rtout:1;
		uint64_t po3_2sml:1;
		uint64_t po2_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t po0_2sml:1;
		uint64_t pci_rsl:1;
		uint64_t rml_wto:1;
		uint64_t rml_rto:1;
#else
		uint64_t rml_rto:1;
		uint64_t rml_wto:1;
		uint64_t pci_rsl:1;
		uint64_t po0_2sml:1;
		uint64_t po1_2sml:1;
		uint64_t po2_2sml:1;
		uint64_t po3_2sml:1;
		uint64_t i0_rtout:1;
		uint64_t i1_rtout:1;
		uint64_t i2_rtout:1;
		uint64_t i3_rtout:1;
		uint64_t i0_overf:1;
		uint64_t i1_overf:1;
		uint64_t i2_overf:1;
		uint64_t i3_overf:1;
		uint64_t p0_rtout:1;
		uint64_t p1_rtout:1;
		uint64_t p2_rtout:1;
		uint64_t p3_rtout:1;
		uint64_t p0_perr:1;
		uint64_t p1_perr:1;
		uint64_t p2_perr:1;
		uint64_t p3_perr:1;
		uint64_t g0_rtout:1;
		uint64_t g1_rtout:1;
		uint64_t g2_rtout:1;
		uint64_t g3_rtout:1;
		uint64_t p0_pperr:1;
		uint64_t p1_pperr:1;
		uint64_t p2_pperr:1;
		uint64_t p3_pperr:1;
		uint64_t p0_ptout:1;
		uint64_t p1_ptout:1;
		uint64_t p2_ptout:1;
		uint64_t p3_ptout:1;
		uint64_t i0_pperr:1;
		uint64_t i1_pperr:1;
		uint64_t i2_pperr:1;
		uint64_t i3_pperr:1;
		uint64_t win_rto:1;
		uint64_t p_dperr:1;
		uint64_t iobdma:1;
		uint64_t reserved_42_63:22;
#endif
	} cn38xxp2;
	struct cvmx_npi_int_sum_cn31xx cn50xx;
	struct cvmx_npi_int_sum_s cn58xx;
	struct cvmx_npi_int_sum_s cn58xxp1;
};

union cvmx_npi_lowp_dbell {
	uint64_t u64;
	struct cvmx_npi_lowp_dbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t dbell:16;
#else
		uint64_t dbell:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_npi_lowp_dbell_s cn30xx;
	struct cvmx_npi_lowp_dbell_s cn31xx;
	struct cvmx_npi_lowp_dbell_s cn38xx;
	struct cvmx_npi_lowp_dbell_s cn38xxp2;
	struct cvmx_npi_lowp_dbell_s cn50xx;
	struct cvmx_npi_lowp_dbell_s cn58xx;
	struct cvmx_npi_lowp_dbell_s cn58xxp1;
};

union cvmx_npi_lowp_ibuff_saddr {
	uint64_t u64;
	struct cvmx_npi_lowp_ibuff_saddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t saddr:36;
#else
		uint64_t saddr:36;
		uint64_t reserved_36_63:28;
#endif
	} s;
	struct cvmx_npi_lowp_ibuff_saddr_s cn30xx;
	struct cvmx_npi_lowp_ibuff_saddr_s cn31xx;
	struct cvmx_npi_lowp_ibuff_saddr_s cn38xx;
	struct cvmx_npi_lowp_ibuff_saddr_s cn38xxp2;
	struct cvmx_npi_lowp_ibuff_saddr_s cn50xx;
	struct cvmx_npi_lowp_ibuff_saddr_s cn58xx;
	struct cvmx_npi_lowp_ibuff_saddr_s cn58xxp1;
};

union cvmx_npi_mem_access_subidx {
	uint64_t u64;
	struct cvmx_npi_mem_access_subidx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_38_63:26;
		uint64_t shortl:1;
		uint64_t nmerge:1;
		uint64_t esr:2;
		uint64_t esw:2;
		uint64_t nsr:1;
		uint64_t nsw:1;
		uint64_t ror:1;
		uint64_t row:1;
		uint64_t ba:28;
#else
		uint64_t ba:28;
		uint64_t row:1;
		uint64_t ror:1;
		uint64_t nsw:1;
		uint64_t nsr:1;
		uint64_t esw:2;
		uint64_t esr:2;
		uint64_t nmerge:1;
		uint64_t shortl:1;
		uint64_t reserved_38_63:26;
#endif
	} s;
	struct cvmx_npi_mem_access_subidx_s cn30xx;
	struct cvmx_npi_mem_access_subidx_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t esr:2;
		uint64_t esw:2;
		uint64_t nsr:1;
		uint64_t nsw:1;
		uint64_t ror:1;
		uint64_t row:1;
		uint64_t ba:28;
#else
		uint64_t ba:28;
		uint64_t row:1;
		uint64_t ror:1;
		uint64_t nsw:1;
		uint64_t nsr:1;
		uint64_t esw:2;
		uint64_t esr:2;
		uint64_t reserved_36_63:28;
#endif
	} cn31xx;
	struct cvmx_npi_mem_access_subidx_s cn38xx;
	struct cvmx_npi_mem_access_subidx_cn31xx cn38xxp2;
	struct cvmx_npi_mem_access_subidx_s cn50xx;
	struct cvmx_npi_mem_access_subidx_s cn58xx;
	struct cvmx_npi_mem_access_subidx_s cn58xxp1;
};

union cvmx_npi_msi_rcv {
	uint64_t u64;
	struct cvmx_npi_msi_rcv_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t int_vec:64;
#else
		uint64_t int_vec:64;
#endif
	} s;
	struct cvmx_npi_msi_rcv_s cn30xx;
	struct cvmx_npi_msi_rcv_s cn31xx;
	struct cvmx_npi_msi_rcv_s cn38xx;
	struct cvmx_npi_msi_rcv_s cn38xxp2;
	struct cvmx_npi_msi_rcv_s cn50xx;
	struct cvmx_npi_msi_rcv_s cn58xx;
	struct cvmx_npi_msi_rcv_s cn58xxp1;
};

union cvmx_npi_num_desc_outputx {
	uint64_t u64;
	struct cvmx_npi_num_desc_outputx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t size:32;
#else
		uint64_t size:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_npi_num_desc_outputx_s cn30xx;
	struct cvmx_npi_num_desc_outputx_s cn31xx;
	struct cvmx_npi_num_desc_outputx_s cn38xx;
	struct cvmx_npi_num_desc_outputx_s cn38xxp2;
	struct cvmx_npi_num_desc_outputx_s cn50xx;
	struct cvmx_npi_num_desc_outputx_s cn58xx;
	struct cvmx_npi_num_desc_outputx_s cn58xxp1;
};

union cvmx_npi_output_control {
	uint64_t u64;
	struct cvmx_npi_output_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t pkt_rr:1;
		uint64_t p3_bmode:1;
		uint64_t p2_bmode:1;
		uint64_t p1_bmode:1;
		uint64_t p0_bmode:1;
		uint64_t o3_es:2;
		uint64_t o3_ns:1;
		uint64_t o3_ro:1;
		uint64_t o2_es:2;
		uint64_t o2_ns:1;
		uint64_t o2_ro:1;
		uint64_t o1_es:2;
		uint64_t o1_ns:1;
		uint64_t o1_ro:1;
		uint64_t o0_es:2;
		uint64_t o0_ns:1;
		uint64_t o0_ro:1;
		uint64_t o3_csrm:1;
		uint64_t o2_csrm:1;
		uint64_t o1_csrm:1;
		uint64_t o0_csrm:1;
		uint64_t reserved_20_23:4;
		uint64_t iptr_o3:1;
		uint64_t iptr_o2:1;
		uint64_t iptr_o1:1;
		uint64_t iptr_o0:1;
		uint64_t esr_sl3:2;
		uint64_t nsr_sl3:1;
		uint64_t ror_sl3:1;
		uint64_t esr_sl2:2;
		uint64_t nsr_sl2:1;
		uint64_t ror_sl2:1;
		uint64_t esr_sl1:2;
		uint64_t nsr_sl1:1;
		uint64_t ror_sl1:1;
		uint64_t esr_sl0:2;
		uint64_t nsr_sl0:1;
		uint64_t ror_sl0:1;
#else
		uint64_t ror_sl0:1;
		uint64_t nsr_sl0:1;
		uint64_t esr_sl0:2;
		uint64_t ror_sl1:1;
		uint64_t nsr_sl1:1;
		uint64_t esr_sl1:2;
		uint64_t ror_sl2:1;
		uint64_t nsr_sl2:1;
		uint64_t esr_sl2:2;
		uint64_t ror_sl3:1;
		uint64_t nsr_sl3:1;
		uint64_t esr_sl3:2;
		uint64_t iptr_o0:1;
		uint64_t iptr_o1:1;
		uint64_t iptr_o2:1;
		uint64_t iptr_o3:1;
		uint64_t reserved_20_23:4;
		uint64_t o0_csrm:1;
		uint64_t o1_csrm:1;
		uint64_t o2_csrm:1;
		uint64_t o3_csrm:1;
		uint64_t o0_ro:1;
		uint64_t o0_ns:1;
		uint64_t o0_es:2;
		uint64_t o1_ro:1;
		uint64_t o1_ns:1;
		uint64_t o1_es:2;
		uint64_t o2_ro:1;
		uint64_t o2_ns:1;
		uint64_t o2_es:2;
		uint64_t o3_ro:1;
		uint64_t o3_ns:1;
		uint64_t o3_es:2;
		uint64_t p0_bmode:1;
		uint64_t p1_bmode:1;
		uint64_t p2_bmode:1;
		uint64_t p3_bmode:1;
		uint64_t pkt_rr:1;
		uint64_t reserved_49_63:15;
#endif
	} s;
	struct cvmx_npi_output_control_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_45_63:19;
		uint64_t p0_bmode:1;
		uint64_t reserved_32_43:12;
		uint64_t o0_es:2;
		uint64_t o0_ns:1;
		uint64_t o0_ro:1;
		uint64_t reserved_25_27:3;
		uint64_t o0_csrm:1;
		uint64_t reserved_17_23:7;
		uint64_t iptr_o0:1;
		uint64_t reserved_4_15:12;
		uint64_t esr_sl0:2;
		uint64_t nsr_sl0:1;
		uint64_t ror_sl0:1;
#else
		uint64_t ror_sl0:1;
		uint64_t nsr_sl0:1;
		uint64_t esr_sl0:2;
		uint64_t reserved_4_15:12;
		uint64_t iptr_o0:1;
		uint64_t reserved_17_23:7;
		uint64_t o0_csrm:1;
		uint64_t reserved_25_27:3;
		uint64_t o0_ro:1;
		uint64_t o0_ns:1;
		uint64_t o0_es:2;
		uint64_t reserved_32_43:12;
		uint64_t p0_bmode:1;
		uint64_t reserved_45_63:19;
#endif
	} cn30xx;
	struct cvmx_npi_output_control_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_46_63:18;
		uint64_t p1_bmode:1;
		uint64_t p0_bmode:1;
		uint64_t reserved_36_43:8;
		uint64_t o1_es:2;
		uint64_t o1_ns:1;
		uint64_t o1_ro:1;
		uint64_t o0_es:2;
		uint64_t o0_ns:1;
		uint64_t o0_ro:1;
		uint64_t reserved_26_27:2;
		uint64_t o1_csrm:1;
		uint64_t o0_csrm:1;
		uint64_t reserved_18_23:6;
		uint64_t iptr_o1:1;
		uint64_t iptr_o0:1;
		uint64_t reserved_8_15:8;
		uint64_t esr_sl1:2;
		uint64_t nsr_sl1:1;
		uint64_t ror_sl1:1;
		uint64_t esr_sl0:2;
		uint64_t nsr_sl0:1;
		uint64_t ror_sl0:1;
#else
		uint64_t ror_sl0:1;
		uint64_t nsr_sl0:1;
		uint64_t esr_sl0:2;
		uint64_t ror_sl1:1;
		uint64_t nsr_sl1:1;
		uint64_t esr_sl1:2;
		uint64_t reserved_8_15:8;
		uint64_t iptr_o0:1;
		uint64_t iptr_o1:1;
		uint64_t reserved_18_23:6;
		uint64_t o0_csrm:1;
		uint64_t o1_csrm:1;
		uint64_t reserved_26_27:2;
		uint64_t o0_ro:1;
		uint64_t o0_ns:1;
		uint64_t o0_es:2;
		uint64_t o1_ro:1;
		uint64_t o1_ns:1;
		uint64_t o1_es:2;
		uint64_t reserved_36_43:8;
		uint64_t p0_bmode:1;
		uint64_t p1_bmode:1;
		uint64_t reserved_46_63:18;
#endif
	} cn31xx;
	struct cvmx_npi_output_control_s cn38xx;
	struct cvmx_npi_output_control_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t p3_bmode:1;
		uint64_t p2_bmode:1;
		uint64_t p1_bmode:1;
		uint64_t p0_bmode:1;
		uint64_t o3_es:2;
		uint64_t o3_ns:1;
		uint64_t o3_ro:1;
		uint64_t o2_es:2;
		uint64_t o2_ns:1;
		uint64_t o2_ro:1;
		uint64_t o1_es:2;
		uint64_t o1_ns:1;
		uint64_t o1_ro:1;
		uint64_t o0_es:2;
		uint64_t o0_ns:1;
		uint64_t o0_ro:1;
		uint64_t o3_csrm:1;
		uint64_t o2_csrm:1;
		uint64_t o1_csrm:1;
		uint64_t o0_csrm:1;
		uint64_t reserved_20_23:4;
		uint64_t iptr_o3:1;
		uint64_t iptr_o2:1;
		uint64_t iptr_o1:1;
		uint64_t iptr_o0:1;
		uint64_t esr_sl3:2;
		uint64_t nsr_sl3:1;
		uint64_t ror_sl3:1;
		uint64_t esr_sl2:2;
		uint64_t nsr_sl2:1;
		uint64_t ror_sl2:1;
		uint64_t esr_sl1:2;
		uint64_t nsr_sl1:1;
		uint64_t ror_sl1:1;
		uint64_t esr_sl0:2;
		uint64_t nsr_sl0:1;
		uint64_t ror_sl0:1;
#else
		uint64_t ror_sl0:1;
		uint64_t nsr_sl0:1;
		uint64_t esr_sl0:2;
		uint64_t ror_sl1:1;
		uint64_t nsr_sl1:1;
		uint64_t esr_sl1:2;
		uint64_t ror_sl2:1;
		uint64_t nsr_sl2:1;
		uint64_t esr_sl2:2;
		uint64_t ror_sl3:1;
		uint64_t nsr_sl3:1;
		uint64_t esr_sl3:2;
		uint64_t iptr_o0:1;
		uint64_t iptr_o1:1;
		uint64_t iptr_o2:1;
		uint64_t iptr_o3:1;
		uint64_t reserved_20_23:4;
		uint64_t o0_csrm:1;
		uint64_t o1_csrm:1;
		uint64_t o2_csrm:1;
		uint64_t o3_csrm:1;
		uint64_t o0_ro:1;
		uint64_t o0_ns:1;
		uint64_t o0_es:2;
		uint64_t o1_ro:1;
		uint64_t o1_ns:1;
		uint64_t o1_es:2;
		uint64_t o2_ro:1;
		uint64_t o2_ns:1;
		uint64_t o2_es:2;
		uint64_t o3_ro:1;
		uint64_t o3_ns:1;
		uint64_t o3_es:2;
		uint64_t p0_bmode:1;
		uint64_t p1_bmode:1;
		uint64_t p2_bmode:1;
		uint64_t p3_bmode:1;
		uint64_t reserved_48_63:16;
#endif
	} cn38xxp2;
	struct cvmx_npi_output_control_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t pkt_rr:1;
		uint64_t reserved_46_47:2;
		uint64_t p1_bmode:1;
		uint64_t p0_bmode:1;
		uint64_t reserved_36_43:8;
		uint64_t o1_es:2;
		uint64_t o1_ns:1;
		uint64_t o1_ro:1;
		uint64_t o0_es:2;
		uint64_t o0_ns:1;
		uint64_t o0_ro:1;
		uint64_t reserved_26_27:2;
		uint64_t o1_csrm:1;
		uint64_t o0_csrm:1;
		uint64_t reserved_18_23:6;
		uint64_t iptr_o1:1;
		uint64_t iptr_o0:1;
		uint64_t reserved_8_15:8;
		uint64_t esr_sl1:2;
		uint64_t nsr_sl1:1;
		uint64_t ror_sl1:1;
		uint64_t esr_sl0:2;
		uint64_t nsr_sl0:1;
		uint64_t ror_sl0:1;
#else
		uint64_t ror_sl0:1;
		uint64_t nsr_sl0:1;
		uint64_t esr_sl0:2;
		uint64_t ror_sl1:1;
		uint64_t nsr_sl1:1;
		uint64_t esr_sl1:2;
		uint64_t reserved_8_15:8;
		uint64_t iptr_o0:1;
		uint64_t iptr_o1:1;
		uint64_t reserved_18_23:6;
		uint64_t o0_csrm:1;
		uint64_t o1_csrm:1;
		uint64_t reserved_26_27:2;
		uint64_t o0_ro:1;
		uint64_t o0_ns:1;
		uint64_t o0_es:2;
		uint64_t o1_ro:1;
		uint64_t o1_ns:1;
		uint64_t o1_es:2;
		uint64_t reserved_36_43:8;
		uint64_t p0_bmode:1;
		uint64_t p1_bmode:1;
		uint64_t reserved_46_47:2;
		uint64_t pkt_rr:1;
		uint64_t reserved_49_63:15;
#endif
	} cn50xx;
	struct cvmx_npi_output_control_s cn58xx;
	struct cvmx_npi_output_control_s cn58xxp1;
};

union cvmx_npi_px_dbpair_addr {
	uint64_t u64;
	struct cvmx_npi_px_dbpair_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_63_63:1;
		uint64_t state:2;
		uint64_t naddr:61;
#else
		uint64_t naddr:61;
		uint64_t state:2;
		uint64_t reserved_63_63:1;
#endif
	} s;
	struct cvmx_npi_px_dbpair_addr_s cn30xx;
	struct cvmx_npi_px_dbpair_addr_s cn31xx;
	struct cvmx_npi_px_dbpair_addr_s cn38xx;
	struct cvmx_npi_px_dbpair_addr_s cn38xxp2;
	struct cvmx_npi_px_dbpair_addr_s cn50xx;
	struct cvmx_npi_px_dbpair_addr_s cn58xx;
	struct cvmx_npi_px_dbpair_addr_s cn58xxp1;
};

union cvmx_npi_px_instr_addr {
	uint64_t u64;
	struct cvmx_npi_px_instr_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t state:3;
		uint64_t naddr:61;
#else
		uint64_t naddr:61;
		uint64_t state:3;
#endif
	} s;
	struct cvmx_npi_px_instr_addr_s cn30xx;
	struct cvmx_npi_px_instr_addr_s cn31xx;
	struct cvmx_npi_px_instr_addr_s cn38xx;
	struct cvmx_npi_px_instr_addr_s cn38xxp2;
	struct cvmx_npi_px_instr_addr_s cn50xx;
	struct cvmx_npi_px_instr_addr_s cn58xx;
	struct cvmx_npi_px_instr_addr_s cn58xxp1;
};

union cvmx_npi_px_instr_cnts {
	uint64_t u64;
	struct cvmx_npi_px_instr_cnts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_38_63:26;
		uint64_t fcnt:6;
		uint64_t avail:32;
#else
		uint64_t avail:32;
		uint64_t fcnt:6;
		uint64_t reserved_38_63:26;
#endif
	} s;
	struct cvmx_npi_px_instr_cnts_s cn30xx;
	struct cvmx_npi_px_instr_cnts_s cn31xx;
	struct cvmx_npi_px_instr_cnts_s cn38xx;
	struct cvmx_npi_px_instr_cnts_s cn38xxp2;
	struct cvmx_npi_px_instr_cnts_s cn50xx;
	struct cvmx_npi_px_instr_cnts_s cn58xx;
	struct cvmx_npi_px_instr_cnts_s cn58xxp1;
};

union cvmx_npi_px_pair_cnts {
	uint64_t u64;
	struct cvmx_npi_px_pair_cnts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_37_63:27;
		uint64_t fcnt:5;
		uint64_t avail:32;
#else
		uint64_t avail:32;
		uint64_t fcnt:5;
		uint64_t reserved_37_63:27;
#endif
	} s;
	struct cvmx_npi_px_pair_cnts_s cn30xx;
	struct cvmx_npi_px_pair_cnts_s cn31xx;
	struct cvmx_npi_px_pair_cnts_s cn38xx;
	struct cvmx_npi_px_pair_cnts_s cn38xxp2;
	struct cvmx_npi_px_pair_cnts_s cn50xx;
	struct cvmx_npi_px_pair_cnts_s cn58xx;
	struct cvmx_npi_px_pair_cnts_s cn58xxp1;
};

union cvmx_npi_pci_burst_size {
	uint64_t u64;
	struct cvmx_npi_pci_burst_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t wr_brst:7;
		uint64_t rd_brst:7;
#else
		uint64_t rd_brst:7;
		uint64_t wr_brst:7;
		uint64_t reserved_14_63:50;
#endif
	} s;
	struct cvmx_npi_pci_burst_size_s cn30xx;
	struct cvmx_npi_pci_burst_size_s cn31xx;
	struct cvmx_npi_pci_burst_size_s cn38xx;
	struct cvmx_npi_pci_burst_size_s cn38xxp2;
	struct cvmx_npi_pci_burst_size_s cn50xx;
	struct cvmx_npi_pci_burst_size_s cn58xx;
	struct cvmx_npi_pci_burst_size_s cn58xxp1;
};

union cvmx_npi_pci_int_arb_cfg {
	uint64_t u64;
	struct cvmx_npi_pci_int_arb_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t hostmode:1;
		uint64_t pci_ovr:4;
		uint64_t reserved_5_7:3;
		uint64_t en:1;
		uint64_t park_mod:1;
		uint64_t park_dev:3;
#else
		uint64_t park_dev:3;
		uint64_t park_mod:1;
		uint64_t en:1;
		uint64_t reserved_5_7:3;
		uint64_t pci_ovr:4;
		uint64_t hostmode:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_npi_pci_int_arb_cfg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t en:1;
		uint64_t park_mod:1;
		uint64_t park_dev:3;
#else
		uint64_t park_dev:3;
		uint64_t park_mod:1;
		uint64_t en:1;
		uint64_t reserved_5_63:59;
#endif
	} cn30xx;
	struct cvmx_npi_pci_int_arb_cfg_cn30xx cn31xx;
	struct cvmx_npi_pci_int_arb_cfg_cn30xx cn38xx;
	struct cvmx_npi_pci_int_arb_cfg_cn30xx cn38xxp2;
	struct cvmx_npi_pci_int_arb_cfg_s cn50xx;
	struct cvmx_npi_pci_int_arb_cfg_s cn58xx;
	struct cvmx_npi_pci_int_arb_cfg_s cn58xxp1;
};

union cvmx_npi_pci_read_cmd {
	uint64_t u64;
	struct cvmx_npi_pci_read_cmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t cmd_size:11;
#else
		uint64_t cmd_size:11;
		uint64_t reserved_11_63:53;
#endif
	} s;
	struct cvmx_npi_pci_read_cmd_s cn30xx;
	struct cvmx_npi_pci_read_cmd_s cn31xx;
	struct cvmx_npi_pci_read_cmd_s cn38xx;
	struct cvmx_npi_pci_read_cmd_s cn38xxp2;
	struct cvmx_npi_pci_read_cmd_s cn50xx;
	struct cvmx_npi_pci_read_cmd_s cn58xx;
	struct cvmx_npi_pci_read_cmd_s cn58xxp1;
};

union cvmx_npi_port32_instr_hdr {
	uint64_t u64;
	struct cvmx_npi_port32_instr_hdr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t pbp:1;
		uint64_t rsv_f:5;
		uint64_t rparmode:2;
		uint64_t rsv_e:1;
		uint64_t rskp_len:7;
		uint64_t rsv_d:6;
		uint64_t use_ihdr:1;
		uint64_t rsv_c:5;
		uint64_t par_mode:2;
		uint64_t rsv_b:1;
		uint64_t skp_len:7;
		uint64_t rsv_a:6;
#else
		uint64_t rsv_a:6;
		uint64_t skp_len:7;
		uint64_t rsv_b:1;
		uint64_t par_mode:2;
		uint64_t rsv_c:5;
		uint64_t use_ihdr:1;
		uint64_t rsv_d:6;
		uint64_t rskp_len:7;
		uint64_t rsv_e:1;
		uint64_t rparmode:2;
		uint64_t rsv_f:5;
		uint64_t pbp:1;
		uint64_t reserved_44_63:20;
#endif
	} s;
	struct cvmx_npi_port32_instr_hdr_s cn30xx;
	struct cvmx_npi_port32_instr_hdr_s cn31xx;
	struct cvmx_npi_port32_instr_hdr_s cn38xx;
	struct cvmx_npi_port32_instr_hdr_s cn38xxp2;
	struct cvmx_npi_port32_instr_hdr_s cn50xx;
	struct cvmx_npi_port32_instr_hdr_s cn58xx;
	struct cvmx_npi_port32_instr_hdr_s cn58xxp1;
};

union cvmx_npi_port33_instr_hdr {
	uint64_t u64;
	struct cvmx_npi_port33_instr_hdr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t pbp:1;
		uint64_t rsv_f:5;
		uint64_t rparmode:2;
		uint64_t rsv_e:1;
		uint64_t rskp_len:7;
		uint64_t rsv_d:6;
		uint64_t use_ihdr:1;
		uint64_t rsv_c:5;
		uint64_t par_mode:2;
		uint64_t rsv_b:1;
		uint64_t skp_len:7;
		uint64_t rsv_a:6;
#else
		uint64_t rsv_a:6;
		uint64_t skp_len:7;
		uint64_t rsv_b:1;
		uint64_t par_mode:2;
		uint64_t rsv_c:5;
		uint64_t use_ihdr:1;
		uint64_t rsv_d:6;
		uint64_t rskp_len:7;
		uint64_t rsv_e:1;
		uint64_t rparmode:2;
		uint64_t rsv_f:5;
		uint64_t pbp:1;
		uint64_t reserved_44_63:20;
#endif
	} s;
	struct cvmx_npi_port33_instr_hdr_s cn31xx;
	struct cvmx_npi_port33_instr_hdr_s cn38xx;
	struct cvmx_npi_port33_instr_hdr_s cn38xxp2;
	struct cvmx_npi_port33_instr_hdr_s cn50xx;
	struct cvmx_npi_port33_instr_hdr_s cn58xx;
	struct cvmx_npi_port33_instr_hdr_s cn58xxp1;
};

union cvmx_npi_port34_instr_hdr {
	uint64_t u64;
	struct cvmx_npi_port34_instr_hdr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t pbp:1;
		uint64_t rsv_f:5;
		uint64_t rparmode:2;
		uint64_t rsv_e:1;
		uint64_t rskp_len:7;
		uint64_t rsv_d:6;
		uint64_t use_ihdr:1;
		uint64_t rsv_c:5;
		uint64_t par_mode:2;
		uint64_t rsv_b:1;
		uint64_t skp_len:7;
		uint64_t rsv_a:6;
#else
		uint64_t rsv_a:6;
		uint64_t skp_len:7;
		uint64_t rsv_b:1;
		uint64_t par_mode:2;
		uint64_t rsv_c:5;
		uint64_t use_ihdr:1;
		uint64_t rsv_d:6;
		uint64_t rskp_len:7;
		uint64_t rsv_e:1;
		uint64_t rparmode:2;
		uint64_t rsv_f:5;
		uint64_t pbp:1;
		uint64_t reserved_44_63:20;
#endif
	} s;
	struct cvmx_npi_port34_instr_hdr_s cn38xx;
	struct cvmx_npi_port34_instr_hdr_s cn38xxp2;
	struct cvmx_npi_port34_instr_hdr_s cn58xx;
	struct cvmx_npi_port34_instr_hdr_s cn58xxp1;
};

union cvmx_npi_port35_instr_hdr {
	uint64_t u64;
	struct cvmx_npi_port35_instr_hdr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t pbp:1;
		uint64_t rsv_f:5;
		uint64_t rparmode:2;
		uint64_t rsv_e:1;
		uint64_t rskp_len:7;
		uint64_t rsv_d:6;
		uint64_t use_ihdr:1;
		uint64_t rsv_c:5;
		uint64_t par_mode:2;
		uint64_t rsv_b:1;
		uint64_t skp_len:7;
		uint64_t rsv_a:6;
#else
		uint64_t rsv_a:6;
		uint64_t skp_len:7;
		uint64_t rsv_b:1;
		uint64_t par_mode:2;
		uint64_t rsv_c:5;
		uint64_t use_ihdr:1;
		uint64_t rsv_d:6;
		uint64_t rskp_len:7;
		uint64_t rsv_e:1;
		uint64_t rparmode:2;
		uint64_t rsv_f:5;
		uint64_t pbp:1;
		uint64_t reserved_44_63:20;
#endif
	} s;
	struct cvmx_npi_port35_instr_hdr_s cn38xx;
	struct cvmx_npi_port35_instr_hdr_s cn38xxp2;
	struct cvmx_npi_port35_instr_hdr_s cn58xx;
	struct cvmx_npi_port35_instr_hdr_s cn58xxp1;
};

union cvmx_npi_port_bp_control {
	uint64_t u64;
	struct cvmx_npi_port_bp_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t bp_on:4;
		uint64_t enb:4;
#else
		uint64_t enb:4;
		uint64_t bp_on:4;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_npi_port_bp_control_s cn30xx;
	struct cvmx_npi_port_bp_control_s cn31xx;
	struct cvmx_npi_port_bp_control_s cn38xx;
	struct cvmx_npi_port_bp_control_s cn38xxp2;
	struct cvmx_npi_port_bp_control_s cn50xx;
	struct cvmx_npi_port_bp_control_s cn58xx;
	struct cvmx_npi_port_bp_control_s cn58xxp1;
};

union cvmx_npi_rsl_int_blocks {
	uint64_t u64;
	struct cvmx_npi_rsl_int_blocks_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t rint_31:1;
		uint64_t iob:1;
		uint64_t reserved_28_29:2;
		uint64_t rint_27:1;
		uint64_t rint_26:1;
		uint64_t rint_25:1;
		uint64_t rint_24:1;
		uint64_t asx1:1;
		uint64_t asx0:1;
		uint64_t rint_21:1;
		uint64_t pip:1;
		uint64_t spx1:1;
		uint64_t spx0:1;
		uint64_t lmc:1;
		uint64_t l2c:1;
		uint64_t rint_15:1;
		uint64_t reserved_13_14:2;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t rint_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t npi:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
#else
		uint64_t mio:1;
		uint64_t gmx0:1;
		uint64_t gmx1:1;
		uint64_t npi:1;
		uint64_t key:1;
		uint64_t fpa:1;
		uint64_t dfa:1;
		uint64_t zip:1;
		uint64_t rint_8:1;
		uint64_t ipd:1;
		uint64_t pko:1;
		uint64_t tim:1;
		uint64_t pow:1;
		uint64_t reserved_13_14:2;
		uint64_t rint_15:1;
		uint64_t l2c:1;
		uint64_t lmc:1;
		uint64_t spx0:1;
		uint64_t spx1:1;
		uint64_t pip:1;
		uint64_t rint_21:1;
		uint64_t asx0:1;
		uint64_t asx1:1;
		uint64_t rint_24:1;
		uint64_t rint_25:1;
		uint64_t rint_26:1;
		uint64_t rint_27:1;
		uint64_t reserved_28_29:2;
		uint64_t iob:1;
		uint64_t rint_31:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_npi_rsl_int_blocks_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t rint_31:1;
		uint64_t iob:1;
		uint64_t rint_29:1;
		uint64_t rint_28:1;
		uint64_t rint_27:1;
		uint64_t rint_26:1;
		uint64_t rint_25:1;
		uint64_t rint_24:1;
		uint64_t asx1:1;
		uint64_t asx0:1;
		uint64_t rint_21:1;
		uint64_t pip:1;
		uint64_t spx1:1;
		uint64_t spx0:1;
		uint64_t lmc:1;
		uint64_t l2c:1;
		uint64_t rint_15:1;
		uint64_t rint_14:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t rint_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t npi:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
#else
		uint64_t mio:1;
		uint64_t gmx0:1;
		uint64_t gmx1:1;
		uint64_t npi:1;
		uint64_t key:1;
		uint64_t fpa:1;
		uint64_t dfa:1;
		uint64_t zip:1;
		uint64_t rint_8:1;
		uint64_t ipd:1;
		uint64_t pko:1;
		uint64_t tim:1;
		uint64_t pow:1;
		uint64_t usb:1;
		uint64_t rint_14:1;
		uint64_t rint_15:1;
		uint64_t l2c:1;
		uint64_t lmc:1;
		uint64_t spx0:1;
		uint64_t spx1:1;
		uint64_t pip:1;
		uint64_t rint_21:1;
		uint64_t asx0:1;
		uint64_t asx1:1;
		uint64_t rint_24:1;
		uint64_t rint_25:1;
		uint64_t rint_26:1;
		uint64_t rint_27:1;
		uint64_t rint_28:1;
		uint64_t rint_29:1;
		uint64_t iob:1;
		uint64_t rint_31:1;
		uint64_t reserved_32_63:32;
#endif
	} cn30xx;
	struct cvmx_npi_rsl_int_blocks_cn30xx cn31xx;
	struct cvmx_npi_rsl_int_blocks_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t rint_31:1;
		uint64_t iob:1;
		uint64_t rint_29:1;
		uint64_t rint_28:1;
		uint64_t rint_27:1;
		uint64_t rint_26:1;
		uint64_t rint_25:1;
		uint64_t rint_24:1;
		uint64_t asx1:1;
		uint64_t asx0:1;
		uint64_t rint_21:1;
		uint64_t pip:1;
		uint64_t spx1:1;
		uint64_t spx0:1;
		uint64_t lmc:1;
		uint64_t l2c:1;
		uint64_t rint_15:1;
		uint64_t rint_14:1;
		uint64_t rint_13:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t rint_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t npi:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
#else
		uint64_t mio:1;
		uint64_t gmx0:1;
		uint64_t gmx1:1;
		uint64_t npi:1;
		uint64_t key:1;
		uint64_t fpa:1;
		uint64_t dfa:1;
		uint64_t zip:1;
		uint64_t rint_8:1;
		uint64_t ipd:1;
		uint64_t pko:1;
		uint64_t tim:1;
		uint64_t pow:1;
		uint64_t rint_13:1;
		uint64_t rint_14:1;
		uint64_t rint_15:1;
		uint64_t l2c:1;
		uint64_t lmc:1;
		uint64_t spx0:1;
		uint64_t spx1:1;
		uint64_t pip:1;
		uint64_t rint_21:1;
		uint64_t asx0:1;
		uint64_t asx1:1;
		uint64_t rint_24:1;
		uint64_t rint_25:1;
		uint64_t rint_26:1;
		uint64_t rint_27:1;
		uint64_t rint_28:1;
		uint64_t rint_29:1;
		uint64_t iob:1;
		uint64_t rint_31:1;
		uint64_t reserved_32_63:32;
#endif
	} cn38xx;
	struct cvmx_npi_rsl_int_blocks_cn38xx cn38xxp2;
	struct cvmx_npi_rsl_int_blocks_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t iob:1;
		uint64_t lmc1:1;
		uint64_t agl:1;
		uint64_t reserved_24_27:4;
		uint64_t asx1:1;
		uint64_t asx0:1;
		uint64_t reserved_21_21:1;
		uint64_t pip:1;
		uint64_t spx1:1;
		uint64_t spx0:1;
		uint64_t lmc:1;
		uint64_t l2c:1;
		uint64_t reserved_15_15:1;
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
		uint64_t npi:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
#else
		uint64_t mio:1;
		uint64_t gmx0:1;
		uint64_t gmx1:1;
		uint64_t npi:1;
		uint64_t key:1;
		uint64_t fpa:1;
		uint64_t dfa:1;
		uint64_t zip:1;
		uint64_t reserved_8_8:1;
		uint64_t ipd:1;
		uint64_t pko:1;
		uint64_t tim:1;
		uint64_t pow:1;
		uint64_t usb:1;
		uint64_t rad:1;
		uint64_t reserved_15_15:1;
		uint64_t l2c:1;
		uint64_t lmc:1;
		uint64_t spx0:1;
		uint64_t spx1:1;
		uint64_t pip:1;
		uint64_t reserved_21_21:1;
		uint64_t asx0:1;
		uint64_t asx1:1;
		uint64_t reserved_24_27:4;
		uint64_t agl:1;
		uint64_t lmc1:1;
		uint64_t iob:1;
		uint64_t reserved_31_63:33;
#endif
	} cn50xx;
	struct cvmx_npi_rsl_int_blocks_cn38xx cn58xx;
	struct cvmx_npi_rsl_int_blocks_cn38xx cn58xxp1;
};

union cvmx_npi_size_inputx {
	uint64_t u64;
	struct cvmx_npi_size_inputx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t size:32;
#else
		uint64_t size:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_npi_size_inputx_s cn30xx;
	struct cvmx_npi_size_inputx_s cn31xx;
	struct cvmx_npi_size_inputx_s cn38xx;
	struct cvmx_npi_size_inputx_s cn38xxp2;
	struct cvmx_npi_size_inputx_s cn50xx;
	struct cvmx_npi_size_inputx_s cn58xx;
	struct cvmx_npi_size_inputx_s cn58xxp1;
};

union cvmx_npi_win_read_to {
	uint64_t u64;
	struct cvmx_npi_win_read_to_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t time:32;
#else
		uint64_t time:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_npi_win_read_to_s cn30xx;
	struct cvmx_npi_win_read_to_s cn31xx;
	struct cvmx_npi_win_read_to_s cn38xx;
	struct cvmx_npi_win_read_to_s cn38xxp2;
	struct cvmx_npi_win_read_to_s cn50xx;
	struct cvmx_npi_win_read_to_s cn58xx;
	struct cvmx_npi_win_read_to_s cn58xxp1;
};

#endif
