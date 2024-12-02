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

#ifndef __CVMX_PCI_DEFS_H__
#define __CVMX_PCI_DEFS_H__

#define CVMX_PCI_BAR1_INDEXX(offset) (0x0000000000000100ull + ((offset) & 31) * 4)
#define CVMX_PCI_BIST_REG (0x00000000000001C0ull)
#define CVMX_PCI_CFG00 (0x0000000000000000ull)
#define CVMX_PCI_CFG01 (0x0000000000000004ull)
#define CVMX_PCI_CFG02 (0x0000000000000008ull)
#define CVMX_PCI_CFG03 (0x000000000000000Cull)
#define CVMX_PCI_CFG04 (0x0000000000000010ull)
#define CVMX_PCI_CFG05 (0x0000000000000014ull)
#define CVMX_PCI_CFG06 (0x0000000000000018ull)
#define CVMX_PCI_CFG07 (0x000000000000001Cull)
#define CVMX_PCI_CFG08 (0x0000000000000020ull)
#define CVMX_PCI_CFG09 (0x0000000000000024ull)
#define CVMX_PCI_CFG10 (0x0000000000000028ull)
#define CVMX_PCI_CFG11 (0x000000000000002Cull)
#define CVMX_PCI_CFG12 (0x0000000000000030ull)
#define CVMX_PCI_CFG13 (0x0000000000000034ull)
#define CVMX_PCI_CFG15 (0x000000000000003Cull)
#define CVMX_PCI_CFG16 (0x0000000000000040ull)
#define CVMX_PCI_CFG17 (0x0000000000000044ull)
#define CVMX_PCI_CFG18 (0x0000000000000048ull)
#define CVMX_PCI_CFG19 (0x000000000000004Cull)
#define CVMX_PCI_CFG20 (0x0000000000000050ull)
#define CVMX_PCI_CFG21 (0x0000000000000054ull)
#define CVMX_PCI_CFG22 (0x0000000000000058ull)
#define CVMX_PCI_CFG56 (0x00000000000000E0ull)
#define CVMX_PCI_CFG57 (0x00000000000000E4ull)
#define CVMX_PCI_CFG58 (0x00000000000000E8ull)
#define CVMX_PCI_CFG59 (0x00000000000000ECull)
#define CVMX_PCI_CFG60 (0x00000000000000F0ull)
#define CVMX_PCI_CFG61 (0x00000000000000F4ull)
#define CVMX_PCI_CFG62 (0x00000000000000F8ull)
#define CVMX_PCI_CFG63 (0x00000000000000FCull)
#define CVMX_PCI_CNT_REG (0x00000000000001B8ull)
#define CVMX_PCI_CTL_STATUS_2 (0x000000000000018Cull)
#define CVMX_PCI_DBELL_X(offset) (0x0000000000000080ull + ((offset) & 3) * 8)
#define CVMX_PCI_DMA_CNT0 CVMX_PCI_DMA_CNTX(0)
#define CVMX_PCI_DMA_CNT1 CVMX_PCI_DMA_CNTX(1)
#define CVMX_PCI_DMA_CNTX(offset) (0x00000000000000A0ull + ((offset) & 1) * 8)
#define CVMX_PCI_DMA_INT_LEV0 CVMX_PCI_DMA_INT_LEVX(0)
#define CVMX_PCI_DMA_INT_LEV1 CVMX_PCI_DMA_INT_LEVX(1)
#define CVMX_PCI_DMA_INT_LEVX(offset) (0x00000000000000A4ull + ((offset) & 1) * 8)
#define CVMX_PCI_DMA_TIME0 CVMX_PCI_DMA_TIMEX(0)
#define CVMX_PCI_DMA_TIME1 CVMX_PCI_DMA_TIMEX(1)
#define CVMX_PCI_DMA_TIMEX(offset) (0x00000000000000B0ull + ((offset) & 1) * 4)
#define CVMX_PCI_INSTR_COUNT0 CVMX_PCI_INSTR_COUNTX(0)
#define CVMX_PCI_INSTR_COUNT1 CVMX_PCI_INSTR_COUNTX(1)
#define CVMX_PCI_INSTR_COUNT2 CVMX_PCI_INSTR_COUNTX(2)
#define CVMX_PCI_INSTR_COUNT3 CVMX_PCI_INSTR_COUNTX(3)
#define CVMX_PCI_INSTR_COUNTX(offset) (0x0000000000000084ull + ((offset) & 3) * 8)
#define CVMX_PCI_INT_ENB (0x0000000000000038ull)
#define CVMX_PCI_INT_ENB2 (0x00000000000001A0ull)
#define CVMX_PCI_INT_SUM (0x0000000000000030ull)
#define CVMX_PCI_INT_SUM2 (0x0000000000000198ull)
#define CVMX_PCI_MSI_RCV (0x00000000000000F0ull)
#define CVMX_PCI_PKTS_SENT0 CVMX_PCI_PKTS_SENTX(0)
#define CVMX_PCI_PKTS_SENT1 CVMX_PCI_PKTS_SENTX(1)
#define CVMX_PCI_PKTS_SENT2 CVMX_PCI_PKTS_SENTX(2)
#define CVMX_PCI_PKTS_SENT3 CVMX_PCI_PKTS_SENTX(3)
#define CVMX_PCI_PKTS_SENTX(offset) (0x0000000000000040ull + ((offset) & 3) * 16)
#define CVMX_PCI_PKTS_SENT_INT_LEV0 CVMX_PCI_PKTS_SENT_INT_LEVX(0)
#define CVMX_PCI_PKTS_SENT_INT_LEV1 CVMX_PCI_PKTS_SENT_INT_LEVX(1)
#define CVMX_PCI_PKTS_SENT_INT_LEV2 CVMX_PCI_PKTS_SENT_INT_LEVX(2)
#define CVMX_PCI_PKTS_SENT_INT_LEV3 CVMX_PCI_PKTS_SENT_INT_LEVX(3)
#define CVMX_PCI_PKTS_SENT_INT_LEVX(offset) (0x0000000000000048ull + ((offset) & 3) * 16)
#define CVMX_PCI_PKTS_SENT_TIME0 CVMX_PCI_PKTS_SENT_TIMEX(0)
#define CVMX_PCI_PKTS_SENT_TIME1 CVMX_PCI_PKTS_SENT_TIMEX(1)
#define CVMX_PCI_PKTS_SENT_TIME2 CVMX_PCI_PKTS_SENT_TIMEX(2)
#define CVMX_PCI_PKTS_SENT_TIME3 CVMX_PCI_PKTS_SENT_TIMEX(3)
#define CVMX_PCI_PKTS_SENT_TIMEX(offset) (0x000000000000004Cull + ((offset) & 3) * 16)
#define CVMX_PCI_PKT_CREDITS0 CVMX_PCI_PKT_CREDITSX(0)
#define CVMX_PCI_PKT_CREDITS1 CVMX_PCI_PKT_CREDITSX(1)
#define CVMX_PCI_PKT_CREDITS2 CVMX_PCI_PKT_CREDITSX(2)
#define CVMX_PCI_PKT_CREDITS3 CVMX_PCI_PKT_CREDITSX(3)
#define CVMX_PCI_PKT_CREDITSX(offset) (0x0000000000000044ull + ((offset) & 3) * 16)
#define CVMX_PCI_READ_CMD_6 (0x0000000000000180ull)
#define CVMX_PCI_READ_CMD_C (0x0000000000000184ull)
#define CVMX_PCI_READ_CMD_E (0x0000000000000188ull)
#define CVMX_PCI_READ_TIMEOUT (CVMX_ADD_IO_SEG(0x00011F00000000B0ull))
#define CVMX_PCI_SCM_REG (0x00000000000001A8ull)
#define CVMX_PCI_TSR_REG (0x00000000000001B0ull)
#define CVMX_PCI_WIN_RD_ADDR (0x0000000000000008ull)
#define CVMX_PCI_WIN_RD_DATA (0x0000000000000020ull)
#define CVMX_PCI_WIN_WR_ADDR (0x0000000000000000ull)
#define CVMX_PCI_WIN_WR_DATA (0x0000000000000010ull)
#define CVMX_PCI_WIN_WR_MASK (0x0000000000000018ull)

union cvmx_pci_bar1_indexx {
	uint32_t u32;
	struct cvmx_pci_bar1_indexx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_18_31:14;
		uint32_t addr_idx:14;
		uint32_t ca:1;
		uint32_t end_swp:2;
		uint32_t addr_v:1;
#else
		uint32_t addr_v:1;
		uint32_t end_swp:2;
		uint32_t ca:1;
		uint32_t addr_idx:14;
		uint32_t reserved_18_31:14;
#endif
	} s;
};

union cvmx_pci_bist_reg {
	uint64_t u64;
	struct cvmx_pci_bist_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t rsp_bs:1;
		uint64_t dma0_bs:1;
		uint64_t cmd0_bs:1;
		uint64_t cmd_bs:1;
		uint64_t csr2p_bs:1;
		uint64_t csrr_bs:1;
		uint64_t rsp2p_bs:1;
		uint64_t csr2n_bs:1;
		uint64_t dat2n_bs:1;
		uint64_t dbg2n_bs:1;
#else
		uint64_t dbg2n_bs:1;
		uint64_t dat2n_bs:1;
		uint64_t csr2n_bs:1;
		uint64_t rsp2p_bs:1;
		uint64_t csrr_bs:1;
		uint64_t csr2p_bs:1;
		uint64_t cmd_bs:1;
		uint64_t cmd0_bs:1;
		uint64_t dma0_bs:1;
		uint64_t rsp_bs:1;
		uint64_t reserved_10_63:54;
#endif
	} s;
};

union cvmx_pci_cfg00 {
	uint32_t u32;
	struct cvmx_pci_cfg00_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t devid:16;
		uint32_t vendid:16;
#else
		uint32_t vendid:16;
		uint32_t devid:16;
#endif
	} s;
};

union cvmx_pci_cfg01 {
	uint32_t u32;
	struct cvmx_pci_cfg01_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t dpe:1;
		uint32_t sse:1;
		uint32_t rma:1;
		uint32_t rta:1;
		uint32_t sta:1;
		uint32_t devt:2;
		uint32_t mdpe:1;
		uint32_t fbb:1;
		uint32_t reserved_22_22:1;
		uint32_t m66:1;
		uint32_t cle:1;
		uint32_t i_stat:1;
		uint32_t reserved_11_18:8;
		uint32_t i_dis:1;
		uint32_t fbbe:1;
		uint32_t see:1;
		uint32_t ads:1;
		uint32_t pee:1;
		uint32_t vps:1;
		uint32_t mwice:1;
		uint32_t scse:1;
		uint32_t me:1;
		uint32_t msae:1;
		uint32_t isae:1;
#else
		uint32_t isae:1;
		uint32_t msae:1;
		uint32_t me:1;
		uint32_t scse:1;
		uint32_t mwice:1;
		uint32_t vps:1;
		uint32_t pee:1;
		uint32_t ads:1;
		uint32_t see:1;
		uint32_t fbbe:1;
		uint32_t i_dis:1;
		uint32_t reserved_11_18:8;
		uint32_t i_stat:1;
		uint32_t cle:1;
		uint32_t m66:1;
		uint32_t reserved_22_22:1;
		uint32_t fbb:1;
		uint32_t mdpe:1;
		uint32_t devt:2;
		uint32_t sta:1;
		uint32_t rta:1;
		uint32_t rma:1;
		uint32_t sse:1;
		uint32_t dpe:1;
#endif
	} s;
};

union cvmx_pci_cfg02 {
	uint32_t u32;
	struct cvmx_pci_cfg02_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t cc:24;
		uint32_t rid:8;
#else
		uint32_t rid:8;
		uint32_t cc:24;
#endif
	} s;
};

union cvmx_pci_cfg03 {
	uint32_t u32;
	struct cvmx_pci_cfg03_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t bcap:1;
		uint32_t brb:1;
		uint32_t reserved_28_29:2;
		uint32_t bcod:4;
		uint32_t ht:8;
		uint32_t lt:8;
		uint32_t cls:8;
#else
		uint32_t cls:8;
		uint32_t lt:8;
		uint32_t ht:8;
		uint32_t bcod:4;
		uint32_t reserved_28_29:2;
		uint32_t brb:1;
		uint32_t bcap:1;
#endif
	} s;
};

union cvmx_pci_cfg04 {
	uint32_t u32;
	struct cvmx_pci_cfg04_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t lbase:20;
		uint32_t lbasez:8;
		uint32_t pf:1;
		uint32_t typ:2;
		uint32_t mspc:1;
#else
		uint32_t mspc:1;
		uint32_t typ:2;
		uint32_t pf:1;
		uint32_t lbasez:8;
		uint32_t lbase:20;
#endif
	} s;
};

union cvmx_pci_cfg05 {
	uint32_t u32;
	struct cvmx_pci_cfg05_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t hbase:32;
#else
		uint32_t hbase:32;
#endif
	} s;
};

union cvmx_pci_cfg06 {
	uint32_t u32;
	struct cvmx_pci_cfg06_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t lbase:5;
		uint32_t lbasez:23;
		uint32_t pf:1;
		uint32_t typ:2;
		uint32_t mspc:1;
#else
		uint32_t mspc:1;
		uint32_t typ:2;
		uint32_t pf:1;
		uint32_t lbasez:23;
		uint32_t lbase:5;
#endif
	} s;
};

union cvmx_pci_cfg07 {
	uint32_t u32;
	struct cvmx_pci_cfg07_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t hbase:32;
#else
		uint32_t hbase:32;
#endif
	} s;
};

union cvmx_pci_cfg08 {
	uint32_t u32;
	struct cvmx_pci_cfg08_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t lbasez:28;
		uint32_t pf:1;
		uint32_t typ:2;
		uint32_t mspc:1;
#else
		uint32_t mspc:1;
		uint32_t typ:2;
		uint32_t pf:1;
		uint32_t lbasez:28;
#endif
	} s;
};

union cvmx_pci_cfg09 {
	uint32_t u32;
	struct cvmx_pci_cfg09_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t hbase:25;
		uint32_t hbasez:7;
#else
		uint32_t hbasez:7;
		uint32_t hbase:25;
#endif
	} s;
};

union cvmx_pci_cfg10 {
	uint32_t u32;
	struct cvmx_pci_cfg10_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t cisp:32;
#else
		uint32_t cisp:32;
#endif
	} s;
};

union cvmx_pci_cfg11 {
	uint32_t u32;
	struct cvmx_pci_cfg11_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t ssid:16;
		uint32_t ssvid:16;
#else
		uint32_t ssvid:16;
		uint32_t ssid:16;
#endif
	} s;
};

union cvmx_pci_cfg12 {
	uint32_t u32;
	struct cvmx_pci_cfg12_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t erbar:16;
		uint32_t erbarz:5;
		uint32_t reserved_1_10:10;
		uint32_t erbar_en:1;
#else
		uint32_t erbar_en:1;
		uint32_t reserved_1_10:10;
		uint32_t erbarz:5;
		uint32_t erbar:16;
#endif
	} s;
};

union cvmx_pci_cfg13 {
	uint32_t u32;
	struct cvmx_pci_cfg13_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_8_31:24;
		uint32_t cp:8;
#else
		uint32_t cp:8;
		uint32_t reserved_8_31:24;
#endif
	} s;
};

union cvmx_pci_cfg15 {
	uint32_t u32;
	struct cvmx_pci_cfg15_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t ml:8;
		uint32_t mg:8;
		uint32_t inta:8;
		uint32_t il:8;
#else
		uint32_t il:8;
		uint32_t inta:8;
		uint32_t mg:8;
		uint32_t ml:8;
#endif
	} s;
};

union cvmx_pci_cfg16 {
	uint32_t u32;
	struct cvmx_pci_cfg16_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t trdnpr:1;
		uint32_t trdard:1;
		uint32_t rdsati:1;
		uint32_t trdrs:1;
		uint32_t trtae:1;
		uint32_t twsei:1;
		uint32_t twsen:1;
		uint32_t twtae:1;
		uint32_t tmae:1;
		uint32_t tslte:3;
		uint32_t tilt:4;
		uint32_t pbe:12;
		uint32_t dppmr:1;
		uint32_t reserved_2_2:1;
		uint32_t tswc:1;
		uint32_t mltd:1;
#else
		uint32_t mltd:1;
		uint32_t tswc:1;
		uint32_t reserved_2_2:1;
		uint32_t dppmr:1;
		uint32_t pbe:12;
		uint32_t tilt:4;
		uint32_t tslte:3;
		uint32_t tmae:1;
		uint32_t twtae:1;
		uint32_t twsen:1;
		uint32_t twsei:1;
		uint32_t trtae:1;
		uint32_t trdrs:1;
		uint32_t rdsati:1;
		uint32_t trdard:1;
		uint32_t trdnpr:1;
#endif
	} s;
};

union cvmx_pci_cfg17 {
	uint32_t u32;
	struct cvmx_pci_cfg17_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t tscme:32;
#else
		uint32_t tscme:32;
#endif
	} s;
};

union cvmx_pci_cfg18 {
	uint32_t u32;
	struct cvmx_pci_cfg18_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t tdsrps:32;
#else
		uint32_t tdsrps:32;
#endif
	} s;
};

union cvmx_pci_cfg19 {
	uint32_t u32;
	struct cvmx_pci_cfg19_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t mrbcm:1;
		uint32_t mrbci:1;
		uint32_t mdwe:1;
		uint32_t mdre:1;
		uint32_t mdrimc:1;
		uint32_t mdrrmc:3;
		uint32_t tmes:8;
		uint32_t teci:1;
		uint32_t tmei:1;
		uint32_t tmse:1;
		uint32_t tmdpes:1;
		uint32_t tmapes:1;
		uint32_t reserved_9_10:2;
		uint32_t tibcd:1;
		uint32_t tibde:1;
		uint32_t reserved_6_6:1;
		uint32_t tidomc:1;
		uint32_t tdomc:5;
#else
		uint32_t tdomc:5;
		uint32_t tidomc:1;
		uint32_t reserved_6_6:1;
		uint32_t tibde:1;
		uint32_t tibcd:1;
		uint32_t reserved_9_10:2;
		uint32_t tmapes:1;
		uint32_t tmdpes:1;
		uint32_t tmse:1;
		uint32_t tmei:1;
		uint32_t teci:1;
		uint32_t tmes:8;
		uint32_t mdrrmc:3;
		uint32_t mdrimc:1;
		uint32_t mdre:1;
		uint32_t mdwe:1;
		uint32_t mrbci:1;
		uint32_t mrbcm:1;
#endif
	} s;
};

union cvmx_pci_cfg20 {
	uint32_t u32;
	struct cvmx_pci_cfg20_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t mdsp:32;
#else
		uint32_t mdsp:32;
#endif
	} s;
};

union cvmx_pci_cfg21 {
	uint32_t u32;
	struct cvmx_pci_cfg21_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t scmre:32;
#else
		uint32_t scmre:32;
#endif
	} s;
};

union cvmx_pci_cfg22 {
	uint32_t u32;
	struct cvmx_pci_cfg22_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t mac:7;
		uint32_t reserved_19_24:6;
		uint32_t flush:1;
		uint32_t mra:1;
		uint32_t mtta:1;
		uint32_t mrv:8;
		uint32_t mttv:8;
#else
		uint32_t mttv:8;
		uint32_t mrv:8;
		uint32_t mtta:1;
		uint32_t mra:1;
		uint32_t flush:1;
		uint32_t reserved_19_24:6;
		uint32_t mac:7;
#endif
	} s;
};

union cvmx_pci_cfg56 {
	uint32_t u32;
	struct cvmx_pci_cfg56_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_23_31:9;
		uint32_t most:3;
		uint32_t mmbc:2;
		uint32_t roe:1;
		uint32_t dpere:1;
		uint32_t ncp:8;
		uint32_t pxcid:8;
#else
		uint32_t pxcid:8;
		uint32_t ncp:8;
		uint32_t dpere:1;
		uint32_t roe:1;
		uint32_t mmbc:2;
		uint32_t most:3;
		uint32_t reserved_23_31:9;
#endif
	} s;
};

union cvmx_pci_cfg57 {
	uint32_t u32;
	struct cvmx_pci_cfg57_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_30_31:2;
		uint32_t scemr:1;
		uint32_t mcrsd:3;
		uint32_t mostd:3;
		uint32_t mmrbcd:2;
		uint32_t dc:1;
		uint32_t usc:1;
		uint32_t scd:1;
		uint32_t m133:1;
		uint32_t w64:1;
		uint32_t bn:8;
		uint32_t dn:5;
		uint32_t fn:3;
#else
		uint32_t fn:3;
		uint32_t dn:5;
		uint32_t bn:8;
		uint32_t w64:1;
		uint32_t m133:1;
		uint32_t scd:1;
		uint32_t usc:1;
		uint32_t dc:1;
		uint32_t mmrbcd:2;
		uint32_t mostd:3;
		uint32_t mcrsd:3;
		uint32_t scemr:1;
		uint32_t reserved_30_31:2;
#endif
	} s;
};

union cvmx_pci_cfg58 {
	uint32_t u32;
	struct cvmx_pci_cfg58_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t pmes:5;
		uint32_t d2s:1;
		uint32_t d1s:1;
		uint32_t auxc:3;
		uint32_t dsi:1;
		uint32_t reserved_20_20:1;
		uint32_t pmec:1;
		uint32_t pcimiv:3;
		uint32_t ncp:8;
		uint32_t pmcid:8;
#else
		uint32_t pmcid:8;
		uint32_t ncp:8;
		uint32_t pcimiv:3;
		uint32_t pmec:1;
		uint32_t reserved_20_20:1;
		uint32_t dsi:1;
		uint32_t auxc:3;
		uint32_t d1s:1;
		uint32_t d2s:1;
		uint32_t pmes:5;
#endif
	} s;
};

union cvmx_pci_cfg59 {
	uint32_t u32;
	struct cvmx_pci_cfg59_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t pmdia:8;
		uint32_t bpccen:1;
		uint32_t bd3h:1;
		uint32_t reserved_16_21:6;
		uint32_t pmess:1;
		uint32_t pmedsia:2;
		uint32_t pmds:4;
		uint32_t pmeens:1;
		uint32_t reserved_2_7:6;
		uint32_t ps:2;
#else
		uint32_t ps:2;
		uint32_t reserved_2_7:6;
		uint32_t pmeens:1;
		uint32_t pmds:4;
		uint32_t pmedsia:2;
		uint32_t pmess:1;
		uint32_t reserved_16_21:6;
		uint32_t bd3h:1;
		uint32_t bpccen:1;
		uint32_t pmdia:8;
#endif
	} s;
};

union cvmx_pci_cfg60 {
	uint32_t u32;
	struct cvmx_pci_cfg60_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_24_31:8;
		uint32_t m64:1;
		uint32_t mme:3;
		uint32_t mmc:3;
		uint32_t msien:1;
		uint32_t ncp:8;
		uint32_t msicid:8;
#else
		uint32_t msicid:8;
		uint32_t ncp:8;
		uint32_t msien:1;
		uint32_t mmc:3;
		uint32_t mme:3;
		uint32_t m64:1;
		uint32_t reserved_24_31:8;
#endif
	} s;
};

union cvmx_pci_cfg61 {
	uint32_t u32;
	struct cvmx_pci_cfg61_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t msi31t2:30;
		uint32_t reserved_0_1:2;
#else
		uint32_t reserved_0_1:2;
		uint32_t msi31t2:30;
#endif
	} s;
};

union cvmx_pci_cfg62 {
	uint32_t u32;
	struct cvmx_pci_cfg62_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t msi:32;
#else
		uint32_t msi:32;
#endif
	} s;
};

union cvmx_pci_cfg63 {
	uint32_t u32;
	struct cvmx_pci_cfg63_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_16_31:16;
		uint32_t msimd:16;
#else
		uint32_t msimd:16;
		uint32_t reserved_16_31:16;
#endif
	} s;
};

union cvmx_pci_cnt_reg {
	uint64_t u64;
	struct cvmx_pci_cnt_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_38_63:26;
		uint64_t hm_pcix:1;
		uint64_t hm_speed:2;
		uint64_t ap_pcix:1;
		uint64_t ap_speed:2;
		uint64_t pcicnt:32;
#else
		uint64_t pcicnt:32;
		uint64_t ap_speed:2;
		uint64_t ap_pcix:1;
		uint64_t hm_speed:2;
		uint64_t hm_pcix:1;
		uint64_t reserved_38_63:26;
#endif
	} s;
};

union cvmx_pci_ctl_status_2 {
	uint32_t u32;
	struct cvmx_pci_ctl_status_2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_29_31:3;
		uint32_t bb1_hole:3;
		uint32_t bb1_siz:1;
		uint32_t bb_ca:1;
		uint32_t bb_es:2;
		uint32_t bb1:1;
		uint32_t bb0:1;
		uint32_t erst_n:1;
		uint32_t bar2pres:1;
		uint32_t scmtyp:1;
		uint32_t scm:1;
		uint32_t en_wfilt:1;
		uint32_t reserved_14_14:1;
		uint32_t ap_pcix:1;
		uint32_t ap_64ad:1;
		uint32_t b12_bist:1;
		uint32_t pmo_amod:1;
		uint32_t pmo_fpc:3;
		uint32_t tsr_hwm:3;
		uint32_t bar2_enb:1;
		uint32_t bar2_esx:2;
		uint32_t bar2_cax:1;
#else
		uint32_t bar2_cax:1;
		uint32_t bar2_esx:2;
		uint32_t bar2_enb:1;
		uint32_t tsr_hwm:3;
		uint32_t pmo_fpc:3;
		uint32_t pmo_amod:1;
		uint32_t b12_bist:1;
		uint32_t ap_64ad:1;
		uint32_t ap_pcix:1;
		uint32_t reserved_14_14:1;
		uint32_t en_wfilt:1;
		uint32_t scm:1;
		uint32_t scmtyp:1;
		uint32_t bar2pres:1;
		uint32_t erst_n:1;
		uint32_t bb0:1;
		uint32_t bb1:1;
		uint32_t bb_es:2;
		uint32_t bb_ca:1;
		uint32_t bb1_siz:1;
		uint32_t bb1_hole:3;
		uint32_t reserved_29_31:3;
#endif
	} s;
	struct cvmx_pci_ctl_status_2_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_20_31:12;
		uint32_t erst_n:1;
		uint32_t bar2pres:1;
		uint32_t scmtyp:1;
		uint32_t scm:1;
		uint32_t en_wfilt:1;
		uint32_t reserved_14_14:1;
		uint32_t ap_pcix:1;
		uint32_t ap_64ad:1;
		uint32_t b12_bist:1;
		uint32_t pmo_amod:1;
		uint32_t pmo_fpc:3;
		uint32_t tsr_hwm:3;
		uint32_t bar2_enb:1;
		uint32_t bar2_esx:2;
		uint32_t bar2_cax:1;
#else
		uint32_t bar2_cax:1;
		uint32_t bar2_esx:2;
		uint32_t bar2_enb:1;
		uint32_t tsr_hwm:3;
		uint32_t pmo_fpc:3;
		uint32_t pmo_amod:1;
		uint32_t b12_bist:1;
		uint32_t ap_64ad:1;
		uint32_t ap_pcix:1;
		uint32_t reserved_14_14:1;
		uint32_t en_wfilt:1;
		uint32_t scm:1;
		uint32_t scmtyp:1;
		uint32_t bar2pres:1;
		uint32_t erst_n:1;
		uint32_t reserved_20_31:12;
#endif
	} cn31xx;
};

union cvmx_pci_dbellx {
	uint32_t u32;
	struct cvmx_pci_dbellx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_16_31:16;
		uint32_t inc_val:16;
#else
		uint32_t inc_val:16;
		uint32_t reserved_16_31:16;
#endif
	} s;
};

union cvmx_pci_dma_cntx {
	uint32_t u32;
	struct cvmx_pci_dma_cntx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t dma_cnt:32;
#else
		uint32_t dma_cnt:32;
#endif
	} s;
};

union cvmx_pci_dma_int_levx {
	uint32_t u32;
	struct cvmx_pci_dma_int_levx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t pkt_cnt:32;
#else
		uint32_t pkt_cnt:32;
#endif
	} s;
};

union cvmx_pci_dma_timex {
	uint32_t u32;
	struct cvmx_pci_dma_timex_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t dma_time:32;
#else
		uint32_t dma_time:32;
#endif
	} s;
};

union cvmx_pci_instr_countx {
	uint32_t u32;
	struct cvmx_pci_instr_countx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t icnt:32;
#else
		uint32_t icnt:32;
#endif
	} s;
};

union cvmx_pci_int_enb {
	uint64_t u64;
	struct cvmx_pci_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t idtime1:1;
		uint64_t idtime0:1;
		uint64_t idcnt1:1;
		uint64_t idcnt0:1;
		uint64_t iptime3:1;
		uint64_t iptime2:1;
		uint64_t iptime1:1;
		uint64_t iptime0:1;
		uint64_t ipcnt3:1;
		uint64_t ipcnt2:1;
		uint64_t ipcnt1:1;
		uint64_t ipcnt0:1;
		uint64_t irsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t idperr:1;
		uint64_t iaperr:1;
		uint64_t iserr:1;
		uint64_t itsr_abt:1;
		uint64_t imsc_msg:1;
		uint64_t imsi_mabt:1;
		uint64_t imsi_tabt:1;
		uint64_t imsi_per:1;
		uint64_t imr_tto:1;
		uint64_t imr_abt:1;
		uint64_t itr_abt:1;
		uint64_t imr_wtto:1;
		uint64_t imr_wabt:1;
		uint64_t itr_wabt:1;
#else
		uint64_t itr_wabt:1;
		uint64_t imr_wabt:1;
		uint64_t imr_wtto:1;
		uint64_t itr_abt:1;
		uint64_t imr_abt:1;
		uint64_t imr_tto:1;
		uint64_t imsi_per:1;
		uint64_t imsi_tabt:1;
		uint64_t imsi_mabt:1;
		uint64_t imsc_msg:1;
		uint64_t itsr_abt:1;
		uint64_t iserr:1;
		uint64_t iaperr:1;
		uint64_t idperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t irsl_int:1;
		uint64_t ipcnt0:1;
		uint64_t ipcnt1:1;
		uint64_t ipcnt2:1;
		uint64_t ipcnt3:1;
		uint64_t iptime0:1;
		uint64_t iptime1:1;
		uint64_t iptime2:1;
		uint64_t iptime3:1;
		uint64_t idcnt0:1;
		uint64_t idcnt1:1;
		uint64_t idtime0:1;
		uint64_t idtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_pci_int_enb_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t idtime1:1;
		uint64_t idtime0:1;
		uint64_t idcnt1:1;
		uint64_t idcnt0:1;
		uint64_t reserved_22_24:3;
		uint64_t iptime0:1;
		uint64_t reserved_18_20:3;
		uint64_t ipcnt0:1;
		uint64_t irsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t idperr:1;
		uint64_t iaperr:1;
		uint64_t iserr:1;
		uint64_t itsr_abt:1;
		uint64_t imsc_msg:1;
		uint64_t imsi_mabt:1;
		uint64_t imsi_tabt:1;
		uint64_t imsi_per:1;
		uint64_t imr_tto:1;
		uint64_t imr_abt:1;
		uint64_t itr_abt:1;
		uint64_t imr_wtto:1;
		uint64_t imr_wabt:1;
		uint64_t itr_wabt:1;
#else
		uint64_t itr_wabt:1;
		uint64_t imr_wabt:1;
		uint64_t imr_wtto:1;
		uint64_t itr_abt:1;
		uint64_t imr_abt:1;
		uint64_t imr_tto:1;
		uint64_t imsi_per:1;
		uint64_t imsi_tabt:1;
		uint64_t imsi_mabt:1;
		uint64_t imsc_msg:1;
		uint64_t itsr_abt:1;
		uint64_t iserr:1;
		uint64_t iaperr:1;
		uint64_t idperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t irsl_int:1;
		uint64_t ipcnt0:1;
		uint64_t reserved_18_20:3;
		uint64_t iptime0:1;
		uint64_t reserved_22_24:3;
		uint64_t idcnt0:1;
		uint64_t idcnt1:1;
		uint64_t idtime0:1;
		uint64_t idtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} cn30xx;
	struct cvmx_pci_int_enb_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t idtime1:1;
		uint64_t idtime0:1;
		uint64_t idcnt1:1;
		uint64_t idcnt0:1;
		uint64_t reserved_23_24:2;
		uint64_t iptime1:1;
		uint64_t iptime0:1;
		uint64_t reserved_19_20:2;
		uint64_t ipcnt1:1;
		uint64_t ipcnt0:1;
		uint64_t irsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t idperr:1;
		uint64_t iaperr:1;
		uint64_t iserr:1;
		uint64_t itsr_abt:1;
		uint64_t imsc_msg:1;
		uint64_t imsi_mabt:1;
		uint64_t imsi_tabt:1;
		uint64_t imsi_per:1;
		uint64_t imr_tto:1;
		uint64_t imr_abt:1;
		uint64_t itr_abt:1;
		uint64_t imr_wtto:1;
		uint64_t imr_wabt:1;
		uint64_t itr_wabt:1;
#else
		uint64_t itr_wabt:1;
		uint64_t imr_wabt:1;
		uint64_t imr_wtto:1;
		uint64_t itr_abt:1;
		uint64_t imr_abt:1;
		uint64_t imr_tto:1;
		uint64_t imsi_per:1;
		uint64_t imsi_tabt:1;
		uint64_t imsi_mabt:1;
		uint64_t imsc_msg:1;
		uint64_t itsr_abt:1;
		uint64_t iserr:1;
		uint64_t iaperr:1;
		uint64_t idperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t irsl_int:1;
		uint64_t ipcnt0:1;
		uint64_t ipcnt1:1;
		uint64_t reserved_19_20:2;
		uint64_t iptime0:1;
		uint64_t iptime1:1;
		uint64_t reserved_23_24:2;
		uint64_t idcnt0:1;
		uint64_t idcnt1:1;
		uint64_t idtime0:1;
		uint64_t idtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} cn31xx;
};

union cvmx_pci_int_enb2 {
	uint64_t u64;
	struct cvmx_pci_int_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t rdtime1:1;
		uint64_t rdtime0:1;
		uint64_t rdcnt1:1;
		uint64_t rdcnt0:1;
		uint64_t rptime3:1;
		uint64_t rptime2:1;
		uint64_t rptime1:1;
		uint64_t rptime0:1;
		uint64_t rpcnt3:1;
		uint64_t rpcnt2:1;
		uint64_t rpcnt1:1;
		uint64_t rpcnt0:1;
		uint64_t rrsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t rdperr:1;
		uint64_t raperr:1;
		uint64_t rserr:1;
		uint64_t rtsr_abt:1;
		uint64_t rmsc_msg:1;
		uint64_t rmsi_mabt:1;
		uint64_t rmsi_tabt:1;
		uint64_t rmsi_per:1;
		uint64_t rmr_tto:1;
		uint64_t rmr_abt:1;
		uint64_t rtr_abt:1;
		uint64_t rmr_wtto:1;
		uint64_t rmr_wabt:1;
		uint64_t rtr_wabt:1;
#else
		uint64_t rtr_wabt:1;
		uint64_t rmr_wabt:1;
		uint64_t rmr_wtto:1;
		uint64_t rtr_abt:1;
		uint64_t rmr_abt:1;
		uint64_t rmr_tto:1;
		uint64_t rmsi_per:1;
		uint64_t rmsi_tabt:1;
		uint64_t rmsi_mabt:1;
		uint64_t rmsc_msg:1;
		uint64_t rtsr_abt:1;
		uint64_t rserr:1;
		uint64_t raperr:1;
		uint64_t rdperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t rrsl_int:1;
		uint64_t rpcnt0:1;
		uint64_t rpcnt1:1;
		uint64_t rpcnt2:1;
		uint64_t rpcnt3:1;
		uint64_t rptime0:1;
		uint64_t rptime1:1;
		uint64_t rptime2:1;
		uint64_t rptime3:1;
		uint64_t rdcnt0:1;
		uint64_t rdcnt1:1;
		uint64_t rdtime0:1;
		uint64_t rdtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_pci_int_enb2_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t rdtime1:1;
		uint64_t rdtime0:1;
		uint64_t rdcnt1:1;
		uint64_t rdcnt0:1;
		uint64_t reserved_22_24:3;
		uint64_t rptime0:1;
		uint64_t reserved_18_20:3;
		uint64_t rpcnt0:1;
		uint64_t rrsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t rdperr:1;
		uint64_t raperr:1;
		uint64_t rserr:1;
		uint64_t rtsr_abt:1;
		uint64_t rmsc_msg:1;
		uint64_t rmsi_mabt:1;
		uint64_t rmsi_tabt:1;
		uint64_t rmsi_per:1;
		uint64_t rmr_tto:1;
		uint64_t rmr_abt:1;
		uint64_t rtr_abt:1;
		uint64_t rmr_wtto:1;
		uint64_t rmr_wabt:1;
		uint64_t rtr_wabt:1;
#else
		uint64_t rtr_wabt:1;
		uint64_t rmr_wabt:1;
		uint64_t rmr_wtto:1;
		uint64_t rtr_abt:1;
		uint64_t rmr_abt:1;
		uint64_t rmr_tto:1;
		uint64_t rmsi_per:1;
		uint64_t rmsi_tabt:1;
		uint64_t rmsi_mabt:1;
		uint64_t rmsc_msg:1;
		uint64_t rtsr_abt:1;
		uint64_t rserr:1;
		uint64_t raperr:1;
		uint64_t rdperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t rrsl_int:1;
		uint64_t rpcnt0:1;
		uint64_t reserved_18_20:3;
		uint64_t rptime0:1;
		uint64_t reserved_22_24:3;
		uint64_t rdcnt0:1;
		uint64_t rdcnt1:1;
		uint64_t rdtime0:1;
		uint64_t rdtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} cn30xx;
	struct cvmx_pci_int_enb2_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t rdtime1:1;
		uint64_t rdtime0:1;
		uint64_t rdcnt1:1;
		uint64_t rdcnt0:1;
		uint64_t reserved_23_24:2;
		uint64_t rptime1:1;
		uint64_t rptime0:1;
		uint64_t reserved_19_20:2;
		uint64_t rpcnt1:1;
		uint64_t rpcnt0:1;
		uint64_t rrsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t rdperr:1;
		uint64_t raperr:1;
		uint64_t rserr:1;
		uint64_t rtsr_abt:1;
		uint64_t rmsc_msg:1;
		uint64_t rmsi_mabt:1;
		uint64_t rmsi_tabt:1;
		uint64_t rmsi_per:1;
		uint64_t rmr_tto:1;
		uint64_t rmr_abt:1;
		uint64_t rtr_abt:1;
		uint64_t rmr_wtto:1;
		uint64_t rmr_wabt:1;
		uint64_t rtr_wabt:1;
#else
		uint64_t rtr_wabt:1;
		uint64_t rmr_wabt:1;
		uint64_t rmr_wtto:1;
		uint64_t rtr_abt:1;
		uint64_t rmr_abt:1;
		uint64_t rmr_tto:1;
		uint64_t rmsi_per:1;
		uint64_t rmsi_tabt:1;
		uint64_t rmsi_mabt:1;
		uint64_t rmsc_msg:1;
		uint64_t rtsr_abt:1;
		uint64_t rserr:1;
		uint64_t raperr:1;
		uint64_t rdperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t rrsl_int:1;
		uint64_t rpcnt0:1;
		uint64_t rpcnt1:1;
		uint64_t reserved_19_20:2;
		uint64_t rptime0:1;
		uint64_t rptime1:1;
		uint64_t reserved_23_24:2;
		uint64_t rdcnt0:1;
		uint64_t rdcnt1:1;
		uint64_t rdtime0:1;
		uint64_t rdtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} cn31xx;
};

union cvmx_pci_int_sum {
	uint64_t u64;
	struct cvmx_pci_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t ptime3:1;
		uint64_t ptime2:1;
		uint64_t ptime1:1;
		uint64_t ptime0:1;
		uint64_t pcnt3:1;
		uint64_t pcnt2:1;
		uint64_t pcnt1:1;
		uint64_t pcnt0:1;
		uint64_t rsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t dperr:1;
		uint64_t aperr:1;
		uint64_t serr:1;
		uint64_t tsr_abt:1;
		uint64_t msc_msg:1;
		uint64_t msi_mabt:1;
		uint64_t msi_tabt:1;
		uint64_t msi_per:1;
		uint64_t mr_tto:1;
		uint64_t mr_abt:1;
		uint64_t tr_abt:1;
		uint64_t mr_wtto:1;
		uint64_t mr_wabt:1;
		uint64_t tr_wabt:1;
#else
		uint64_t tr_wabt:1;
		uint64_t mr_wabt:1;
		uint64_t mr_wtto:1;
		uint64_t tr_abt:1;
		uint64_t mr_abt:1;
		uint64_t mr_tto:1;
		uint64_t msi_per:1;
		uint64_t msi_tabt:1;
		uint64_t msi_mabt:1;
		uint64_t msc_msg:1;
		uint64_t tsr_abt:1;
		uint64_t serr:1;
		uint64_t aperr:1;
		uint64_t dperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t rsl_int:1;
		uint64_t pcnt0:1;
		uint64_t pcnt1:1;
		uint64_t pcnt2:1;
		uint64_t pcnt3:1;
		uint64_t ptime0:1;
		uint64_t ptime1:1;
		uint64_t ptime2:1;
		uint64_t ptime3:1;
		uint64_t dcnt0:1;
		uint64_t dcnt1:1;
		uint64_t dtime0:1;
		uint64_t dtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_pci_int_sum_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t reserved_22_24:3;
		uint64_t ptime0:1;
		uint64_t reserved_18_20:3;
		uint64_t pcnt0:1;
		uint64_t rsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t dperr:1;
		uint64_t aperr:1;
		uint64_t serr:1;
		uint64_t tsr_abt:1;
		uint64_t msc_msg:1;
		uint64_t msi_mabt:1;
		uint64_t msi_tabt:1;
		uint64_t msi_per:1;
		uint64_t mr_tto:1;
		uint64_t mr_abt:1;
		uint64_t tr_abt:1;
		uint64_t mr_wtto:1;
		uint64_t mr_wabt:1;
		uint64_t tr_wabt:1;
#else
		uint64_t tr_wabt:1;
		uint64_t mr_wabt:1;
		uint64_t mr_wtto:1;
		uint64_t tr_abt:1;
		uint64_t mr_abt:1;
		uint64_t mr_tto:1;
		uint64_t msi_per:1;
		uint64_t msi_tabt:1;
		uint64_t msi_mabt:1;
		uint64_t msc_msg:1;
		uint64_t tsr_abt:1;
		uint64_t serr:1;
		uint64_t aperr:1;
		uint64_t dperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t rsl_int:1;
		uint64_t pcnt0:1;
		uint64_t reserved_18_20:3;
		uint64_t ptime0:1;
		uint64_t reserved_22_24:3;
		uint64_t dcnt0:1;
		uint64_t dcnt1:1;
		uint64_t dtime0:1;
		uint64_t dtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} cn30xx;
	struct cvmx_pci_int_sum_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t reserved_23_24:2;
		uint64_t ptime1:1;
		uint64_t ptime0:1;
		uint64_t reserved_19_20:2;
		uint64_t pcnt1:1;
		uint64_t pcnt0:1;
		uint64_t rsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t dperr:1;
		uint64_t aperr:1;
		uint64_t serr:1;
		uint64_t tsr_abt:1;
		uint64_t msc_msg:1;
		uint64_t msi_mabt:1;
		uint64_t msi_tabt:1;
		uint64_t msi_per:1;
		uint64_t mr_tto:1;
		uint64_t mr_abt:1;
		uint64_t tr_abt:1;
		uint64_t mr_wtto:1;
		uint64_t mr_wabt:1;
		uint64_t tr_wabt:1;
#else
		uint64_t tr_wabt:1;
		uint64_t mr_wabt:1;
		uint64_t mr_wtto:1;
		uint64_t tr_abt:1;
		uint64_t mr_abt:1;
		uint64_t mr_tto:1;
		uint64_t msi_per:1;
		uint64_t msi_tabt:1;
		uint64_t msi_mabt:1;
		uint64_t msc_msg:1;
		uint64_t tsr_abt:1;
		uint64_t serr:1;
		uint64_t aperr:1;
		uint64_t dperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t rsl_int:1;
		uint64_t pcnt0:1;
		uint64_t pcnt1:1;
		uint64_t reserved_19_20:2;
		uint64_t ptime0:1;
		uint64_t ptime1:1;
		uint64_t reserved_23_24:2;
		uint64_t dcnt0:1;
		uint64_t dcnt1:1;
		uint64_t dtime0:1;
		uint64_t dtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} cn31xx;
};

union cvmx_pci_int_sum2 {
	uint64_t u64;
	struct cvmx_pci_int_sum2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t ptime3:1;
		uint64_t ptime2:1;
		uint64_t ptime1:1;
		uint64_t ptime0:1;
		uint64_t pcnt3:1;
		uint64_t pcnt2:1;
		uint64_t pcnt1:1;
		uint64_t pcnt0:1;
		uint64_t rsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t dperr:1;
		uint64_t aperr:1;
		uint64_t serr:1;
		uint64_t tsr_abt:1;
		uint64_t msc_msg:1;
		uint64_t msi_mabt:1;
		uint64_t msi_tabt:1;
		uint64_t msi_per:1;
		uint64_t mr_tto:1;
		uint64_t mr_abt:1;
		uint64_t tr_abt:1;
		uint64_t mr_wtto:1;
		uint64_t mr_wabt:1;
		uint64_t tr_wabt:1;
#else
		uint64_t tr_wabt:1;
		uint64_t mr_wabt:1;
		uint64_t mr_wtto:1;
		uint64_t tr_abt:1;
		uint64_t mr_abt:1;
		uint64_t mr_tto:1;
		uint64_t msi_per:1;
		uint64_t msi_tabt:1;
		uint64_t msi_mabt:1;
		uint64_t msc_msg:1;
		uint64_t tsr_abt:1;
		uint64_t serr:1;
		uint64_t aperr:1;
		uint64_t dperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t rsl_int:1;
		uint64_t pcnt0:1;
		uint64_t pcnt1:1;
		uint64_t pcnt2:1;
		uint64_t pcnt3:1;
		uint64_t ptime0:1;
		uint64_t ptime1:1;
		uint64_t ptime2:1;
		uint64_t ptime3:1;
		uint64_t dcnt0:1;
		uint64_t dcnt1:1;
		uint64_t dtime0:1;
		uint64_t dtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_pci_int_sum2_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t reserved_22_24:3;
		uint64_t ptime0:1;
		uint64_t reserved_18_20:3;
		uint64_t pcnt0:1;
		uint64_t rsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t dperr:1;
		uint64_t aperr:1;
		uint64_t serr:1;
		uint64_t tsr_abt:1;
		uint64_t msc_msg:1;
		uint64_t msi_mabt:1;
		uint64_t msi_tabt:1;
		uint64_t msi_per:1;
		uint64_t mr_tto:1;
		uint64_t mr_abt:1;
		uint64_t tr_abt:1;
		uint64_t mr_wtto:1;
		uint64_t mr_wabt:1;
		uint64_t tr_wabt:1;
#else
		uint64_t tr_wabt:1;
		uint64_t mr_wabt:1;
		uint64_t mr_wtto:1;
		uint64_t tr_abt:1;
		uint64_t mr_abt:1;
		uint64_t mr_tto:1;
		uint64_t msi_per:1;
		uint64_t msi_tabt:1;
		uint64_t msi_mabt:1;
		uint64_t msc_msg:1;
		uint64_t tsr_abt:1;
		uint64_t serr:1;
		uint64_t aperr:1;
		uint64_t dperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t rsl_int:1;
		uint64_t pcnt0:1;
		uint64_t reserved_18_20:3;
		uint64_t ptime0:1;
		uint64_t reserved_22_24:3;
		uint64_t dcnt0:1;
		uint64_t dcnt1:1;
		uint64_t dtime0:1;
		uint64_t dtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} cn30xx;
	struct cvmx_pci_int_sum2_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ill_rd:1;
		uint64_t ill_wr:1;
		uint64_t win_wr:1;
		uint64_t dma1_fi:1;
		uint64_t dma0_fi:1;
		uint64_t dtime1:1;
		uint64_t dtime0:1;
		uint64_t dcnt1:1;
		uint64_t dcnt0:1;
		uint64_t reserved_23_24:2;
		uint64_t ptime1:1;
		uint64_t ptime0:1;
		uint64_t reserved_19_20:2;
		uint64_t pcnt1:1;
		uint64_t pcnt0:1;
		uint64_t rsl_int:1;
		uint64_t ill_rrd:1;
		uint64_t ill_rwr:1;
		uint64_t dperr:1;
		uint64_t aperr:1;
		uint64_t serr:1;
		uint64_t tsr_abt:1;
		uint64_t msc_msg:1;
		uint64_t msi_mabt:1;
		uint64_t msi_tabt:1;
		uint64_t msi_per:1;
		uint64_t mr_tto:1;
		uint64_t mr_abt:1;
		uint64_t tr_abt:1;
		uint64_t mr_wtto:1;
		uint64_t mr_wabt:1;
		uint64_t tr_wabt:1;
#else
		uint64_t tr_wabt:1;
		uint64_t mr_wabt:1;
		uint64_t mr_wtto:1;
		uint64_t tr_abt:1;
		uint64_t mr_abt:1;
		uint64_t mr_tto:1;
		uint64_t msi_per:1;
		uint64_t msi_tabt:1;
		uint64_t msi_mabt:1;
		uint64_t msc_msg:1;
		uint64_t tsr_abt:1;
		uint64_t serr:1;
		uint64_t aperr:1;
		uint64_t dperr:1;
		uint64_t ill_rwr:1;
		uint64_t ill_rrd:1;
		uint64_t rsl_int:1;
		uint64_t pcnt0:1;
		uint64_t pcnt1:1;
		uint64_t reserved_19_20:2;
		uint64_t ptime0:1;
		uint64_t ptime1:1;
		uint64_t reserved_23_24:2;
		uint64_t dcnt0:1;
		uint64_t dcnt1:1;
		uint64_t dtime0:1;
		uint64_t dtime1:1;
		uint64_t dma0_fi:1;
		uint64_t dma1_fi:1;
		uint64_t win_wr:1;
		uint64_t ill_wr:1;
		uint64_t ill_rd:1;
		uint64_t reserved_34_63:30;
#endif
	} cn31xx;
};

union cvmx_pci_msi_rcv {
	uint32_t u32;
	struct cvmx_pci_msi_rcv_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_6_31:26;
		uint32_t intr:6;
#else
		uint32_t intr:6;
		uint32_t reserved_6_31:26;
#endif
	} s;
};

union cvmx_pci_pkt_creditsx {
	uint32_t u32;
	struct cvmx_pci_pkt_creditsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t pkt_cnt:16;
		uint32_t ptr_cnt:16;
#else
		uint32_t ptr_cnt:16;
		uint32_t pkt_cnt:16;
#endif
	} s;
};

union cvmx_pci_pkts_sentx {
	uint32_t u32;
	struct cvmx_pci_pkts_sentx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t pkt_cnt:32;
#else
		uint32_t pkt_cnt:32;
#endif
	} s;
};

union cvmx_pci_pkts_sent_int_levx {
	uint32_t u32;
	struct cvmx_pci_pkts_sent_int_levx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t pkt_cnt:32;
#else
		uint32_t pkt_cnt:32;
#endif
	} s;
};

union cvmx_pci_pkts_sent_timex {
	uint32_t u32;
	struct cvmx_pci_pkts_sent_timex_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t pkt_time:32;
#else
		uint32_t pkt_time:32;
#endif
	} s;
};

union cvmx_pci_read_cmd_6 {
	uint32_t u32;
	struct cvmx_pci_read_cmd_6_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_9_31:23;
		uint32_t min_data:6;
		uint32_t prefetch:3;
#else
		uint32_t prefetch:3;
		uint32_t min_data:6;
		uint32_t reserved_9_31:23;
#endif
	} s;
};

union cvmx_pci_read_cmd_c {
	uint32_t u32;
	struct cvmx_pci_read_cmd_c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_9_31:23;
		uint32_t min_data:6;
		uint32_t prefetch:3;
#else
		uint32_t prefetch:3;
		uint32_t min_data:6;
		uint32_t reserved_9_31:23;
#endif
	} s;
};

union cvmx_pci_read_cmd_e {
	uint32_t u32;
	struct cvmx_pci_read_cmd_e_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t reserved_9_31:23;
		uint32_t min_data:6;
		uint32_t prefetch:3;
#else
		uint32_t prefetch:3;
		uint32_t min_data:6;
		uint32_t reserved_9_31:23;
#endif
	} s;
};

union cvmx_pci_read_timeout {
	uint64_t u64;
	struct cvmx_pci_read_timeout_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t enb:1;
		uint64_t cnt:31;
#else
		uint64_t cnt:31;
		uint64_t enb:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_pci_scm_reg {
	uint64_t u64;
	struct cvmx_pci_scm_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t scm:32;
#else
		uint64_t scm:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_pci_tsr_reg {
	uint64_t u64;
	struct cvmx_pci_tsr_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t tsr:36;
#else
		uint64_t tsr:36;
		uint64_t reserved_36_63:28;
#endif
	} s;
};

union cvmx_pci_win_rd_addr {
	uint64_t u64;
	struct cvmx_pci_win_rd_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t iobit:1;
		uint64_t reserved_0_47:48;
#else
		uint64_t reserved_0_47:48;
		uint64_t iobit:1;
		uint64_t reserved_49_63:15;
#endif
	} s;
	struct cvmx_pci_win_rd_addr_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t iobit:1;
		uint64_t rd_addr:46;
		uint64_t reserved_0_1:2;
#else
		uint64_t reserved_0_1:2;
		uint64_t rd_addr:46;
		uint64_t iobit:1;
		uint64_t reserved_49_63:15;
#endif
	} cn30xx;
	struct cvmx_pci_win_rd_addr_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t iobit:1;
		uint64_t rd_addr:45;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t rd_addr:45;
		uint64_t iobit:1;
		uint64_t reserved_49_63:15;
#endif
	} cn38xx;
};

union cvmx_pci_win_rd_data {
	uint64_t u64;
	struct cvmx_pci_win_rd_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rd_data:64;
#else
		uint64_t rd_data:64;
#endif
	} s;
};

union cvmx_pci_win_wr_addr {
	uint64_t u64;
	struct cvmx_pci_win_wr_addr_s {
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
};

union cvmx_pci_win_wr_data {
	uint64_t u64;
	struct cvmx_pci_win_wr_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t wr_data:64;
#else
		uint64_t wr_data:64;
#endif
	} s;
};

union cvmx_pci_win_wr_mask {
	uint64_t u64;
	struct cvmx_pci_win_wr_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t wr_mask:8;
#else
		uint64_t wr_mask:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
};

#endif
