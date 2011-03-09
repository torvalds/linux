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

#ifndef __CVMX_L2C_DEFS_H__
#define __CVMX_L2C_DEFS_H__

#define CVMX_L2C_BIG_CTL (CVMX_ADD_IO_SEG(0x0001180080800030ull))
#define CVMX_L2C_BST (CVMX_ADD_IO_SEG(0x00011800808007F8ull))
#define CVMX_L2C_BST0 (CVMX_ADD_IO_SEG(0x00011800800007F8ull))
#define CVMX_L2C_BST1 (CVMX_ADD_IO_SEG(0x00011800800007F0ull))
#define CVMX_L2C_BST2 (CVMX_ADD_IO_SEG(0x00011800800007E8ull))
#define CVMX_L2C_BST_MEMX(block_id) (CVMX_ADD_IO_SEG(0x0001180080C007F8ull))
#define CVMX_L2C_BST_TDTX(block_id) (CVMX_ADD_IO_SEG(0x0001180080A007F0ull))
#define CVMX_L2C_BST_TTGX(block_id) (CVMX_ADD_IO_SEG(0x0001180080A007F8ull))
#define CVMX_L2C_CFG (CVMX_ADD_IO_SEG(0x0001180080000000ull))
#define CVMX_L2C_COP0_MAPX(offset) (CVMX_ADD_IO_SEG(0x0001180080940000ull) + ((offset) & 16383) * 8)
#define CVMX_L2C_CTL (CVMX_ADD_IO_SEG(0x0001180080800000ull))
#define CVMX_L2C_DBG (CVMX_ADD_IO_SEG(0x0001180080000030ull))
#define CVMX_L2C_DUT (CVMX_ADD_IO_SEG(0x0001180080000050ull))
#define CVMX_L2C_DUT_MAPX(offset) (CVMX_ADD_IO_SEG(0x0001180080E00000ull) + ((offset) & 2047) * 8)
#define CVMX_L2C_ERR_TDTX(block_id) (CVMX_ADD_IO_SEG(0x0001180080A007E0ull))
#define CVMX_L2C_ERR_TTGX(block_id) (CVMX_ADD_IO_SEG(0x0001180080A007E8ull))
#define CVMX_L2C_ERR_VBFX(block_id) (CVMX_ADD_IO_SEG(0x0001180080C007F0ull))
#define CVMX_L2C_ERR_XMC (CVMX_ADD_IO_SEG(0x00011800808007D8ull))
#define CVMX_L2C_GRPWRR0 (CVMX_ADD_IO_SEG(0x00011800800000C8ull))
#define CVMX_L2C_GRPWRR1 (CVMX_ADD_IO_SEG(0x00011800800000D0ull))
#define CVMX_L2C_INT_EN (CVMX_ADD_IO_SEG(0x0001180080000100ull))
#define CVMX_L2C_INT_ENA (CVMX_ADD_IO_SEG(0x0001180080800020ull))
#define CVMX_L2C_INT_REG (CVMX_ADD_IO_SEG(0x0001180080800018ull))
#define CVMX_L2C_INT_STAT (CVMX_ADD_IO_SEG(0x00011800800000F8ull))
#define CVMX_L2C_IOCX_PFC(block_id) (CVMX_ADD_IO_SEG(0x0001180080800420ull))
#define CVMX_L2C_IORX_PFC(block_id) (CVMX_ADD_IO_SEG(0x0001180080800428ull))
#define CVMX_L2C_LCKBASE (CVMX_ADD_IO_SEG(0x0001180080000058ull))
#define CVMX_L2C_LCKOFF (CVMX_ADD_IO_SEG(0x0001180080000060ull))
#define CVMX_L2C_LFB0 (CVMX_ADD_IO_SEG(0x0001180080000038ull))
#define CVMX_L2C_LFB1 (CVMX_ADD_IO_SEG(0x0001180080000040ull))
#define CVMX_L2C_LFB2 (CVMX_ADD_IO_SEG(0x0001180080000048ull))
#define CVMX_L2C_LFB3 (CVMX_ADD_IO_SEG(0x00011800800000B8ull))
#define CVMX_L2C_OOB (CVMX_ADD_IO_SEG(0x00011800800000D8ull))
#define CVMX_L2C_OOB1 (CVMX_ADD_IO_SEG(0x00011800800000E0ull))
#define CVMX_L2C_OOB2 (CVMX_ADD_IO_SEG(0x00011800800000E8ull))
#define CVMX_L2C_OOB3 (CVMX_ADD_IO_SEG(0x00011800800000F0ull))
#define CVMX_L2C_PFC0 CVMX_L2C_PFCX(0)
#define CVMX_L2C_PFC1 CVMX_L2C_PFCX(1)
#define CVMX_L2C_PFC2 CVMX_L2C_PFCX(2)
#define CVMX_L2C_PFC3 CVMX_L2C_PFCX(3)
#define CVMX_L2C_PFCTL (CVMX_ADD_IO_SEG(0x0001180080000090ull))
#define CVMX_L2C_PFCX(offset) (CVMX_ADD_IO_SEG(0x0001180080000098ull) + ((offset) & 3) * 8)
#define CVMX_L2C_PPGRP (CVMX_ADD_IO_SEG(0x00011800800000C0ull))
#define CVMX_L2C_QOS_IOBX(block_id) (CVMX_ADD_IO_SEG(0x0001180080880200ull))
#define CVMX_L2C_QOS_PPX(offset) (CVMX_ADD_IO_SEG(0x0001180080880000ull) + ((offset) & 7) * 8)
#define CVMX_L2C_QOS_WGT (CVMX_ADD_IO_SEG(0x0001180080800008ull))
#define CVMX_L2C_RSCX_PFC(block_id) (CVMX_ADD_IO_SEG(0x0001180080800410ull))
#define CVMX_L2C_RSDX_PFC(block_id) (CVMX_ADD_IO_SEG(0x0001180080800418ull))
#define CVMX_L2C_SPAR0 (CVMX_ADD_IO_SEG(0x0001180080000068ull))
#define CVMX_L2C_SPAR1 (CVMX_ADD_IO_SEG(0x0001180080000070ull))
#define CVMX_L2C_SPAR2 (CVMX_ADD_IO_SEG(0x0001180080000078ull))
#define CVMX_L2C_SPAR3 (CVMX_ADD_IO_SEG(0x0001180080000080ull))
#define CVMX_L2C_SPAR4 (CVMX_ADD_IO_SEG(0x0001180080000088ull))
#define CVMX_L2C_TADX_ECC0(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00018ull))
#define CVMX_L2C_TADX_ECC1(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00020ull))
#define CVMX_L2C_TADX_IEN(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00000ull))
#define CVMX_L2C_TADX_INT(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00028ull))
#define CVMX_L2C_TADX_PFC0(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00400ull))
#define CVMX_L2C_TADX_PFC1(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00408ull))
#define CVMX_L2C_TADX_PFC2(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00410ull))
#define CVMX_L2C_TADX_PFC3(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00418ull))
#define CVMX_L2C_TADX_PRF(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00008ull))
#define CVMX_L2C_TADX_TAG(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00010ull))
#define CVMX_L2C_VER_ID (CVMX_ADD_IO_SEG(0x00011800808007E0ull))
#define CVMX_L2C_VER_IOB (CVMX_ADD_IO_SEG(0x00011800808007F0ull))
#define CVMX_L2C_VER_MSC (CVMX_ADD_IO_SEG(0x00011800808007D0ull))
#define CVMX_L2C_VER_PP (CVMX_ADD_IO_SEG(0x00011800808007E8ull))
#define CVMX_L2C_VIRTID_IOBX(block_id) (CVMX_ADD_IO_SEG(0x00011800808C0200ull))
#define CVMX_L2C_VIRTID_PPX(offset) (CVMX_ADD_IO_SEG(0x00011800808C0000ull) + ((offset) & 7) * 8)
#define CVMX_L2C_VRT_CTL (CVMX_ADD_IO_SEG(0x0001180080800010ull))
#define CVMX_L2C_VRT_MEMX(offset) (CVMX_ADD_IO_SEG(0x0001180080900000ull) + ((offset) & 1023) * 8)
#define CVMX_L2C_WPAR_IOBX(block_id) (CVMX_ADD_IO_SEG(0x0001180080840200ull))
#define CVMX_L2C_WPAR_PPX(offset) (CVMX_ADD_IO_SEG(0x0001180080840000ull) + ((offset) & 7) * 8)
#define CVMX_L2C_XMCX_PFC(block_id) (CVMX_ADD_IO_SEG(0x0001180080800400ull))
#define CVMX_L2C_XMC_CMD (CVMX_ADD_IO_SEG(0x0001180080800028ull))
#define CVMX_L2C_XMDX_PFC(block_id) (CVMX_ADD_IO_SEG(0x0001180080800408ull))

union cvmx_l2c_big_ctl {
	uint64_t u64;
	struct cvmx_l2c_big_ctl_s {
		uint64_t reserved_8_63:56;
		uint64_t maxdram:4;
		uint64_t reserved_1_3:3;
		uint64_t disable:1;
	} s;
	struct cvmx_l2c_big_ctl_s cn63xx;
};

union cvmx_l2c_bst {
	uint64_t u64;
	struct cvmx_l2c_bst_s {
		uint64_t reserved_38_63:26;
		uint64_t dutfl:6;
		uint64_t reserved_17_31:15;
		uint64_t ioccmdfl:1;
		uint64_t reserved_13_15:3;
		uint64_t iocdatfl:1;
		uint64_t reserved_9_11:3;
		uint64_t dutresfl:1;
		uint64_t reserved_5_7:3;
		uint64_t vrtfl:1;
		uint64_t reserved_1_3:3;
		uint64_t tdffl:1;
	} s;
	struct cvmx_l2c_bst_s cn63xx;
	struct cvmx_l2c_bst_s cn63xxp1;
};

union cvmx_l2c_bst0 {
	uint64_t u64;
	struct cvmx_l2c_bst0_s {
		uint64_t reserved_24_63:40;
		uint64_t dtbnk:1;
		uint64_t wlb_msk:4;
		uint64_t dtcnt:13;
		uint64_t dt:1;
		uint64_t stin_msk:1;
		uint64_t wlb_dat:4;
	} s;
	struct cvmx_l2c_bst0_cn30xx {
		uint64_t reserved_23_63:41;
		uint64_t wlb_msk:4;
		uint64_t reserved_15_18:4;
		uint64_t dtcnt:9;
		uint64_t dt:1;
		uint64_t reserved_4_4:1;
		uint64_t wlb_dat:4;
	} cn30xx;
	struct cvmx_l2c_bst0_cn31xx {
		uint64_t reserved_23_63:41;
		uint64_t wlb_msk:4;
		uint64_t reserved_16_18:3;
		uint64_t dtcnt:10;
		uint64_t dt:1;
		uint64_t stin_msk:1;
		uint64_t wlb_dat:4;
	} cn31xx;
	struct cvmx_l2c_bst0_cn38xx {
		uint64_t reserved_19_63:45;
		uint64_t dtcnt:13;
		uint64_t dt:1;
		uint64_t stin_msk:1;
		uint64_t wlb_dat:4;
	} cn38xx;
	struct cvmx_l2c_bst0_cn38xx cn38xxp2;
	struct cvmx_l2c_bst0_cn50xx {
		uint64_t reserved_24_63:40;
		uint64_t dtbnk:1;
		uint64_t wlb_msk:4;
		uint64_t reserved_16_18:3;
		uint64_t dtcnt:10;
		uint64_t dt:1;
		uint64_t stin_msk:1;
		uint64_t wlb_dat:4;
	} cn50xx;
	struct cvmx_l2c_bst0_cn50xx cn52xx;
	struct cvmx_l2c_bst0_cn50xx cn52xxp1;
	struct cvmx_l2c_bst0_s cn56xx;
	struct cvmx_l2c_bst0_s cn56xxp1;
	struct cvmx_l2c_bst0_s cn58xx;
	struct cvmx_l2c_bst0_s cn58xxp1;
};

union cvmx_l2c_bst1 {
	uint64_t u64;
	struct cvmx_l2c_bst1_s {
		uint64_t reserved_9_63:55;
		uint64_t l2t:9;
	} s;
	struct cvmx_l2c_bst1_cn30xx {
		uint64_t reserved_16_63:48;
		uint64_t vwdf:4;
		uint64_t lrf:2;
		uint64_t vab_vwcf:1;
		uint64_t reserved_5_8:4;
		uint64_t l2t:5;
	} cn30xx;
	struct cvmx_l2c_bst1_cn30xx cn31xx;
	struct cvmx_l2c_bst1_cn38xx {
		uint64_t reserved_16_63:48;
		uint64_t vwdf:4;
		uint64_t lrf:2;
		uint64_t vab_vwcf:1;
		uint64_t l2t:9;
	} cn38xx;
	struct cvmx_l2c_bst1_cn38xx cn38xxp2;
	struct cvmx_l2c_bst1_cn38xx cn50xx;
	struct cvmx_l2c_bst1_cn52xx {
		uint64_t reserved_19_63:45;
		uint64_t plc2:1;
		uint64_t plc1:1;
		uint64_t plc0:1;
		uint64_t vwdf:4;
		uint64_t reserved_11_11:1;
		uint64_t ilc:1;
		uint64_t vab_vwcf:1;
		uint64_t l2t:9;
	} cn52xx;
	struct cvmx_l2c_bst1_cn52xx cn52xxp1;
	struct cvmx_l2c_bst1_cn56xx {
		uint64_t reserved_24_63:40;
		uint64_t plc2:1;
		uint64_t plc1:1;
		uint64_t plc0:1;
		uint64_t ilc:1;
		uint64_t vwdf1:4;
		uint64_t vwdf0:4;
		uint64_t vab_vwcf1:1;
		uint64_t reserved_10_10:1;
		uint64_t vab_vwcf0:1;
		uint64_t l2t:9;
	} cn56xx;
	struct cvmx_l2c_bst1_cn56xx cn56xxp1;
	struct cvmx_l2c_bst1_cn38xx cn58xx;
	struct cvmx_l2c_bst1_cn38xx cn58xxp1;
};

union cvmx_l2c_bst2 {
	uint64_t u64;
	struct cvmx_l2c_bst2_s {
		uint64_t reserved_16_63:48;
		uint64_t mrb:4;
		uint64_t reserved_4_11:8;
		uint64_t ipcbst:1;
		uint64_t picbst:1;
		uint64_t xrdmsk:1;
		uint64_t xrddat:1;
	} s;
	struct cvmx_l2c_bst2_cn30xx {
		uint64_t reserved_16_63:48;
		uint64_t mrb:4;
		uint64_t rmdf:4;
		uint64_t reserved_4_7:4;
		uint64_t ipcbst:1;
		uint64_t reserved_2_2:1;
		uint64_t xrdmsk:1;
		uint64_t xrddat:1;
	} cn30xx;
	struct cvmx_l2c_bst2_cn30xx cn31xx;
	struct cvmx_l2c_bst2_cn38xx {
		uint64_t reserved_16_63:48;
		uint64_t mrb:4;
		uint64_t rmdf:4;
		uint64_t rhdf:4;
		uint64_t ipcbst:1;
		uint64_t picbst:1;
		uint64_t xrdmsk:1;
		uint64_t xrddat:1;
	} cn38xx;
	struct cvmx_l2c_bst2_cn38xx cn38xxp2;
	struct cvmx_l2c_bst2_cn30xx cn50xx;
	struct cvmx_l2c_bst2_cn30xx cn52xx;
	struct cvmx_l2c_bst2_cn30xx cn52xxp1;
	struct cvmx_l2c_bst2_cn56xx {
		uint64_t reserved_16_63:48;
		uint64_t mrb:4;
		uint64_t rmdb:4;
		uint64_t rhdb:4;
		uint64_t ipcbst:1;
		uint64_t picbst:1;
		uint64_t xrdmsk:1;
		uint64_t xrddat:1;
	} cn56xx;
	struct cvmx_l2c_bst2_cn56xx cn56xxp1;
	struct cvmx_l2c_bst2_cn56xx cn58xx;
	struct cvmx_l2c_bst2_cn56xx cn58xxp1;
};

union cvmx_l2c_bst_memx {
	uint64_t u64;
	struct cvmx_l2c_bst_memx_s {
		uint64_t start_bist:1;
		uint64_t clear_bist:1;
		uint64_t reserved_5_61:57;
		uint64_t rdffl:1;
		uint64_t vbffl:4;
	} s;
	struct cvmx_l2c_bst_memx_s cn63xx;
	struct cvmx_l2c_bst_memx_s cn63xxp1;
};

union cvmx_l2c_bst_tdtx {
	uint64_t u64;
	struct cvmx_l2c_bst_tdtx_s {
		uint64_t reserved_32_63:32;
		uint64_t fbfrspfl:8;
		uint64_t sbffl:8;
		uint64_t fbffl:8;
		uint64_t l2dfl:8;
	} s;
	struct cvmx_l2c_bst_tdtx_s cn63xx;
	struct cvmx_l2c_bst_tdtx_cn63xxp1 {
		uint64_t reserved_24_63:40;
		uint64_t sbffl:8;
		uint64_t fbffl:8;
		uint64_t l2dfl:8;
	} cn63xxp1;
};

union cvmx_l2c_bst_ttgx {
	uint64_t u64;
	struct cvmx_l2c_bst_ttgx_s {
		uint64_t reserved_17_63:47;
		uint64_t lrufl:1;
		uint64_t tagfl:16;
	} s;
	struct cvmx_l2c_bst_ttgx_s cn63xx;
	struct cvmx_l2c_bst_ttgx_s cn63xxp1;
};

union cvmx_l2c_cfg {
	uint64_t u64;
	struct cvmx_l2c_cfg_s {
		uint64_t reserved_20_63:44;
		uint64_t bstrun:1;
		uint64_t lbist:1;
		uint64_t xor_bank:1;
		uint64_t dpres1:1;
		uint64_t dpres0:1;
		uint64_t dfill_dis:1;
		uint64_t fpexp:4;
		uint64_t fpempty:1;
		uint64_t fpen:1;
		uint64_t idxalias:1;
		uint64_t mwf_crd:4;
		uint64_t rsp_arb_mode:1;
		uint64_t rfb_arb_mode:1;
		uint64_t lrf_arb_mode:1;
	} s;
	struct cvmx_l2c_cfg_cn30xx {
		uint64_t reserved_14_63:50;
		uint64_t fpexp:4;
		uint64_t fpempty:1;
		uint64_t fpen:1;
		uint64_t idxalias:1;
		uint64_t mwf_crd:4;
		uint64_t rsp_arb_mode:1;
		uint64_t rfb_arb_mode:1;
		uint64_t lrf_arb_mode:1;
	} cn30xx;
	struct cvmx_l2c_cfg_cn30xx cn31xx;
	struct cvmx_l2c_cfg_cn30xx cn38xx;
	struct cvmx_l2c_cfg_cn30xx cn38xxp2;
	struct cvmx_l2c_cfg_cn50xx {
		uint64_t reserved_20_63:44;
		uint64_t bstrun:1;
		uint64_t lbist:1;
		uint64_t reserved_14_17:4;
		uint64_t fpexp:4;
		uint64_t fpempty:1;
		uint64_t fpen:1;
		uint64_t idxalias:1;
		uint64_t mwf_crd:4;
		uint64_t rsp_arb_mode:1;
		uint64_t rfb_arb_mode:1;
		uint64_t lrf_arb_mode:1;
	} cn50xx;
	struct cvmx_l2c_cfg_cn50xx cn52xx;
	struct cvmx_l2c_cfg_cn50xx cn52xxp1;
	struct cvmx_l2c_cfg_s cn56xx;
	struct cvmx_l2c_cfg_s cn56xxp1;
	struct cvmx_l2c_cfg_cn58xx {
		uint64_t reserved_20_63:44;
		uint64_t bstrun:1;
		uint64_t lbist:1;
		uint64_t reserved_15_17:3;
		uint64_t dfill_dis:1;
		uint64_t fpexp:4;
		uint64_t fpempty:1;
		uint64_t fpen:1;
		uint64_t idxalias:1;
		uint64_t mwf_crd:4;
		uint64_t rsp_arb_mode:1;
		uint64_t rfb_arb_mode:1;
		uint64_t lrf_arb_mode:1;
	} cn58xx;
	struct cvmx_l2c_cfg_cn58xxp1 {
		uint64_t reserved_15_63:49;
		uint64_t dfill_dis:1;
		uint64_t fpexp:4;
		uint64_t fpempty:1;
		uint64_t fpen:1;
		uint64_t idxalias:1;
		uint64_t mwf_crd:4;
		uint64_t rsp_arb_mode:1;
		uint64_t rfb_arb_mode:1;
		uint64_t lrf_arb_mode:1;
	} cn58xxp1;
};

union cvmx_l2c_cop0_mapx {
	uint64_t u64;
	struct cvmx_l2c_cop0_mapx_s {
		uint64_t data:64;
	} s;
	struct cvmx_l2c_cop0_mapx_s cn63xx;
	struct cvmx_l2c_cop0_mapx_s cn63xxp1;
};

union cvmx_l2c_ctl {
	uint64_t u64;
	struct cvmx_l2c_ctl_s {
		uint64_t reserved_28_63:36;
		uint64_t disstgl2i:1;
		uint64_t l2dfsbe:1;
		uint64_t l2dfdbe:1;
		uint64_t discclk:1;
		uint64_t maxvab:4;
		uint64_t maxlfb:4;
		uint64_t rsp_arb_mode:1;
		uint64_t xmc_arb_mode:1;
		uint64_t ef_ena:1;
		uint64_t ef_cnt:7;
		uint64_t vab_thresh:4;
		uint64_t disecc:1;
		uint64_t disidxalias:1;
	} s;
	struct cvmx_l2c_ctl_s cn63xx;
	struct cvmx_l2c_ctl_cn63xxp1 {
		uint64_t reserved_25_63:39;
		uint64_t discclk:1;
		uint64_t maxvab:4;
		uint64_t maxlfb:4;
		uint64_t rsp_arb_mode:1;
		uint64_t xmc_arb_mode:1;
		uint64_t ef_ena:1;
		uint64_t ef_cnt:7;
		uint64_t vab_thresh:4;
		uint64_t disecc:1;
		uint64_t disidxalias:1;
	} cn63xxp1;
};

union cvmx_l2c_dbg {
	uint64_t u64;
	struct cvmx_l2c_dbg_s {
		uint64_t reserved_15_63:49;
		uint64_t lfb_enum:4;
		uint64_t lfb_dmp:1;
		uint64_t ppnum:4;
		uint64_t set:3;
		uint64_t finv:1;
		uint64_t l2d:1;
		uint64_t l2t:1;
	} s;
	struct cvmx_l2c_dbg_cn30xx {
		uint64_t reserved_13_63:51;
		uint64_t lfb_enum:2;
		uint64_t lfb_dmp:1;
		uint64_t reserved_7_9:3;
		uint64_t ppnum:1;
		uint64_t reserved_5_5:1;
		uint64_t set:2;
		uint64_t finv:1;
		uint64_t l2d:1;
		uint64_t l2t:1;
	} cn30xx;
	struct cvmx_l2c_dbg_cn31xx {
		uint64_t reserved_14_63:50;
		uint64_t lfb_enum:3;
		uint64_t lfb_dmp:1;
		uint64_t reserved_7_9:3;
		uint64_t ppnum:1;
		uint64_t reserved_5_5:1;
		uint64_t set:2;
		uint64_t finv:1;
		uint64_t l2d:1;
		uint64_t l2t:1;
	} cn31xx;
	struct cvmx_l2c_dbg_s cn38xx;
	struct cvmx_l2c_dbg_s cn38xxp2;
	struct cvmx_l2c_dbg_cn50xx {
		uint64_t reserved_14_63:50;
		uint64_t lfb_enum:3;
		uint64_t lfb_dmp:1;
		uint64_t reserved_7_9:3;
		uint64_t ppnum:1;
		uint64_t set:3;
		uint64_t finv:1;
		uint64_t l2d:1;
		uint64_t l2t:1;
	} cn50xx;
	struct cvmx_l2c_dbg_cn52xx {
		uint64_t reserved_14_63:50;
		uint64_t lfb_enum:3;
		uint64_t lfb_dmp:1;
		uint64_t reserved_8_9:2;
		uint64_t ppnum:2;
		uint64_t set:3;
		uint64_t finv:1;
		uint64_t l2d:1;
		uint64_t l2t:1;
	} cn52xx;
	struct cvmx_l2c_dbg_cn52xx cn52xxp1;
	struct cvmx_l2c_dbg_s cn56xx;
	struct cvmx_l2c_dbg_s cn56xxp1;
	struct cvmx_l2c_dbg_s cn58xx;
	struct cvmx_l2c_dbg_s cn58xxp1;
};

union cvmx_l2c_dut {
	uint64_t u64;
	struct cvmx_l2c_dut_s {
		uint64_t reserved_32_63:32;
		uint64_t dtena:1;
		uint64_t reserved_30_30:1;
		uint64_t dt_vld:1;
		uint64_t dt_tag:29;
	} s;
	struct cvmx_l2c_dut_s cn30xx;
	struct cvmx_l2c_dut_s cn31xx;
	struct cvmx_l2c_dut_s cn38xx;
	struct cvmx_l2c_dut_s cn38xxp2;
	struct cvmx_l2c_dut_s cn50xx;
	struct cvmx_l2c_dut_s cn52xx;
	struct cvmx_l2c_dut_s cn52xxp1;
	struct cvmx_l2c_dut_s cn56xx;
	struct cvmx_l2c_dut_s cn56xxp1;
	struct cvmx_l2c_dut_s cn58xx;
	struct cvmx_l2c_dut_s cn58xxp1;
};

union cvmx_l2c_dut_mapx {
	uint64_t u64;
	struct cvmx_l2c_dut_mapx_s {
		uint64_t reserved_38_63:26;
		uint64_t tag:28;
		uint64_t reserved_1_9:9;
		uint64_t valid:1;
	} s;
	struct cvmx_l2c_dut_mapx_s cn63xx;
	struct cvmx_l2c_dut_mapx_s cn63xxp1;
};

union cvmx_l2c_err_tdtx {
	uint64_t u64;
	struct cvmx_l2c_err_tdtx_s {
		uint64_t dbe:1;
		uint64_t sbe:1;
		uint64_t vdbe:1;
		uint64_t vsbe:1;
		uint64_t syn:10;
		uint64_t reserved_21_49:29;
		uint64_t wayidx:17;
		uint64_t reserved_2_3:2;
		uint64_t type:2;
	} s;
	struct cvmx_l2c_err_tdtx_s cn63xx;
	struct cvmx_l2c_err_tdtx_s cn63xxp1;
};

union cvmx_l2c_err_ttgx {
	uint64_t u64;
	struct cvmx_l2c_err_ttgx_s {
		uint64_t dbe:1;
		uint64_t sbe:1;
		uint64_t noway:1;
		uint64_t reserved_56_60:5;
		uint64_t syn:6;
		uint64_t reserved_21_49:29;
		uint64_t wayidx:14;
		uint64_t reserved_2_6:5;
		uint64_t type:2;
	} s;
	struct cvmx_l2c_err_ttgx_s cn63xx;
	struct cvmx_l2c_err_ttgx_s cn63xxp1;
};

union cvmx_l2c_err_vbfx {
	uint64_t u64;
	struct cvmx_l2c_err_vbfx_s {
		uint64_t reserved_62_63:2;
		uint64_t vdbe:1;
		uint64_t vsbe:1;
		uint64_t vsyn:10;
		uint64_t reserved_2_49:48;
		uint64_t type:2;
	} s;
	struct cvmx_l2c_err_vbfx_s cn63xx;
	struct cvmx_l2c_err_vbfx_s cn63xxp1;
};

union cvmx_l2c_err_xmc {
	uint64_t u64;
	struct cvmx_l2c_err_xmc_s {
		uint64_t cmd:6;
		uint64_t reserved_52_57:6;
		uint64_t sid:4;
		uint64_t reserved_38_47:10;
		uint64_t addr:38;
	} s;
	struct cvmx_l2c_err_xmc_s cn63xx;
	struct cvmx_l2c_err_xmc_s cn63xxp1;
};

union cvmx_l2c_grpwrr0 {
	uint64_t u64;
	struct cvmx_l2c_grpwrr0_s {
		uint64_t plc1rmsk:32;
		uint64_t plc0rmsk:32;
	} s;
	struct cvmx_l2c_grpwrr0_s cn52xx;
	struct cvmx_l2c_grpwrr0_s cn52xxp1;
	struct cvmx_l2c_grpwrr0_s cn56xx;
	struct cvmx_l2c_grpwrr0_s cn56xxp1;
};

union cvmx_l2c_grpwrr1 {
	uint64_t u64;
	struct cvmx_l2c_grpwrr1_s {
		uint64_t ilcrmsk:32;
		uint64_t plc2rmsk:32;
	} s;
	struct cvmx_l2c_grpwrr1_s cn52xx;
	struct cvmx_l2c_grpwrr1_s cn52xxp1;
	struct cvmx_l2c_grpwrr1_s cn56xx;
	struct cvmx_l2c_grpwrr1_s cn56xxp1;
};

union cvmx_l2c_int_en {
	uint64_t u64;
	struct cvmx_l2c_int_en_s {
		uint64_t reserved_9_63:55;
		uint64_t lck2ena:1;
		uint64_t lckena:1;
		uint64_t l2ddeden:1;
		uint64_t l2dsecen:1;
		uint64_t l2tdeden:1;
		uint64_t l2tsecen:1;
		uint64_t oob3en:1;
		uint64_t oob2en:1;
		uint64_t oob1en:1;
	} s;
	struct cvmx_l2c_int_en_s cn52xx;
	struct cvmx_l2c_int_en_s cn52xxp1;
	struct cvmx_l2c_int_en_s cn56xx;
	struct cvmx_l2c_int_en_s cn56xxp1;
};

union cvmx_l2c_int_ena {
	uint64_t u64;
	struct cvmx_l2c_int_ena_s {
		uint64_t reserved_8_63:56;
		uint64_t bigrd:1;
		uint64_t bigwr:1;
		uint64_t vrtpe:1;
		uint64_t vrtadrng:1;
		uint64_t vrtidrng:1;
		uint64_t vrtwr:1;
		uint64_t holewr:1;
		uint64_t holerd:1;
	} s;
	struct cvmx_l2c_int_ena_s cn63xx;
	struct cvmx_l2c_int_ena_cn63xxp1 {
		uint64_t reserved_6_63:58;
		uint64_t vrtpe:1;
		uint64_t vrtadrng:1;
		uint64_t vrtidrng:1;
		uint64_t vrtwr:1;
		uint64_t holewr:1;
		uint64_t holerd:1;
	} cn63xxp1;
};

union cvmx_l2c_int_reg {
	uint64_t u64;
	struct cvmx_l2c_int_reg_s {
		uint64_t reserved_17_63:47;
		uint64_t tad0:1;
		uint64_t reserved_8_15:8;
		uint64_t bigrd:1;
		uint64_t bigwr:1;
		uint64_t vrtpe:1;
		uint64_t vrtadrng:1;
		uint64_t vrtidrng:1;
		uint64_t vrtwr:1;
		uint64_t holewr:1;
		uint64_t holerd:1;
	} s;
	struct cvmx_l2c_int_reg_s cn63xx;
	struct cvmx_l2c_int_reg_cn63xxp1 {
		uint64_t reserved_17_63:47;
		uint64_t tad0:1;
		uint64_t reserved_6_15:10;
		uint64_t vrtpe:1;
		uint64_t vrtadrng:1;
		uint64_t vrtidrng:1;
		uint64_t vrtwr:1;
		uint64_t holewr:1;
		uint64_t holerd:1;
	} cn63xxp1;
};

union cvmx_l2c_int_stat {
	uint64_t u64;
	struct cvmx_l2c_int_stat_s {
		uint64_t reserved_9_63:55;
		uint64_t lck2:1;
		uint64_t lck:1;
		uint64_t l2dded:1;
		uint64_t l2dsec:1;
		uint64_t l2tded:1;
		uint64_t l2tsec:1;
		uint64_t oob3:1;
		uint64_t oob2:1;
		uint64_t oob1:1;
	} s;
	struct cvmx_l2c_int_stat_s cn52xx;
	struct cvmx_l2c_int_stat_s cn52xxp1;
	struct cvmx_l2c_int_stat_s cn56xx;
	struct cvmx_l2c_int_stat_s cn56xxp1;
};

union cvmx_l2c_iocx_pfc {
	uint64_t u64;
	struct cvmx_l2c_iocx_pfc_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_iocx_pfc_s cn63xx;
	struct cvmx_l2c_iocx_pfc_s cn63xxp1;
};

union cvmx_l2c_iorx_pfc {
	uint64_t u64;
	struct cvmx_l2c_iorx_pfc_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_iorx_pfc_s cn63xx;
	struct cvmx_l2c_iorx_pfc_s cn63xxp1;
};

union cvmx_l2c_lckbase {
	uint64_t u64;
	struct cvmx_l2c_lckbase_s {
		uint64_t reserved_31_63:33;
		uint64_t lck_base:27;
		uint64_t reserved_1_3:3;
		uint64_t lck_ena:1;
	} s;
	struct cvmx_l2c_lckbase_s cn30xx;
	struct cvmx_l2c_lckbase_s cn31xx;
	struct cvmx_l2c_lckbase_s cn38xx;
	struct cvmx_l2c_lckbase_s cn38xxp2;
	struct cvmx_l2c_lckbase_s cn50xx;
	struct cvmx_l2c_lckbase_s cn52xx;
	struct cvmx_l2c_lckbase_s cn52xxp1;
	struct cvmx_l2c_lckbase_s cn56xx;
	struct cvmx_l2c_lckbase_s cn56xxp1;
	struct cvmx_l2c_lckbase_s cn58xx;
	struct cvmx_l2c_lckbase_s cn58xxp1;
};

union cvmx_l2c_lckoff {
	uint64_t u64;
	struct cvmx_l2c_lckoff_s {
		uint64_t reserved_10_63:54;
		uint64_t lck_offset:10;
	} s;
	struct cvmx_l2c_lckoff_s cn30xx;
	struct cvmx_l2c_lckoff_s cn31xx;
	struct cvmx_l2c_lckoff_s cn38xx;
	struct cvmx_l2c_lckoff_s cn38xxp2;
	struct cvmx_l2c_lckoff_s cn50xx;
	struct cvmx_l2c_lckoff_s cn52xx;
	struct cvmx_l2c_lckoff_s cn52xxp1;
	struct cvmx_l2c_lckoff_s cn56xx;
	struct cvmx_l2c_lckoff_s cn56xxp1;
	struct cvmx_l2c_lckoff_s cn58xx;
	struct cvmx_l2c_lckoff_s cn58xxp1;
};

union cvmx_l2c_lfb0 {
	uint64_t u64;
	struct cvmx_l2c_lfb0_s {
		uint64_t reserved_32_63:32;
		uint64_t stcpnd:1;
		uint64_t stpnd:1;
		uint64_t stinv:1;
		uint64_t stcfl:1;
		uint64_t vam:1;
		uint64_t inxt:4;
		uint64_t itl:1;
		uint64_t ihd:1;
		uint64_t set:3;
		uint64_t vabnum:4;
		uint64_t sid:9;
		uint64_t cmd:4;
		uint64_t vld:1;
	} s;
	struct cvmx_l2c_lfb0_cn30xx {
		uint64_t reserved_32_63:32;
		uint64_t stcpnd:1;
		uint64_t stpnd:1;
		uint64_t stinv:1;
		uint64_t stcfl:1;
		uint64_t vam:1;
		uint64_t reserved_25_26:2;
		uint64_t inxt:2;
		uint64_t itl:1;
		uint64_t ihd:1;
		uint64_t reserved_20_20:1;
		uint64_t set:2;
		uint64_t reserved_16_17:2;
		uint64_t vabnum:2;
		uint64_t sid:9;
		uint64_t cmd:4;
		uint64_t vld:1;
	} cn30xx;
	struct cvmx_l2c_lfb0_cn31xx {
		uint64_t reserved_32_63:32;
		uint64_t stcpnd:1;
		uint64_t stpnd:1;
		uint64_t stinv:1;
		uint64_t stcfl:1;
		uint64_t vam:1;
		uint64_t reserved_26_26:1;
		uint64_t inxt:3;
		uint64_t itl:1;
		uint64_t ihd:1;
		uint64_t reserved_20_20:1;
		uint64_t set:2;
		uint64_t reserved_17_17:1;
		uint64_t vabnum:3;
		uint64_t sid:9;
		uint64_t cmd:4;
		uint64_t vld:1;
	} cn31xx;
	struct cvmx_l2c_lfb0_s cn38xx;
	struct cvmx_l2c_lfb0_s cn38xxp2;
	struct cvmx_l2c_lfb0_cn50xx {
		uint64_t reserved_32_63:32;
		uint64_t stcpnd:1;
		uint64_t stpnd:1;
		uint64_t stinv:1;
		uint64_t stcfl:1;
		uint64_t vam:1;
		uint64_t reserved_26_26:1;
		uint64_t inxt:3;
		uint64_t itl:1;
		uint64_t ihd:1;
		uint64_t set:3;
		uint64_t reserved_17_17:1;
		uint64_t vabnum:3;
		uint64_t sid:9;
		uint64_t cmd:4;
		uint64_t vld:1;
	} cn50xx;
	struct cvmx_l2c_lfb0_cn50xx cn52xx;
	struct cvmx_l2c_lfb0_cn50xx cn52xxp1;
	struct cvmx_l2c_lfb0_s cn56xx;
	struct cvmx_l2c_lfb0_s cn56xxp1;
	struct cvmx_l2c_lfb0_s cn58xx;
	struct cvmx_l2c_lfb0_s cn58xxp1;
};

union cvmx_l2c_lfb1 {
	uint64_t u64;
	struct cvmx_l2c_lfb1_s {
		uint64_t reserved_19_63:45;
		uint64_t dsgoing:1;
		uint64_t bid:2;
		uint64_t wtrsp:1;
		uint64_t wtdw:1;
		uint64_t wtdq:1;
		uint64_t wtwhp:1;
		uint64_t wtwhf:1;
		uint64_t wtwrm:1;
		uint64_t wtstm:1;
		uint64_t wtrda:1;
		uint64_t wtstdt:1;
		uint64_t wtstrsp:1;
		uint64_t wtstrsc:1;
		uint64_t wtvtm:1;
		uint64_t wtmfl:1;
		uint64_t prbrty:1;
		uint64_t wtprb:1;
		uint64_t vld:1;
	} s;
	struct cvmx_l2c_lfb1_s cn30xx;
	struct cvmx_l2c_lfb1_s cn31xx;
	struct cvmx_l2c_lfb1_s cn38xx;
	struct cvmx_l2c_lfb1_s cn38xxp2;
	struct cvmx_l2c_lfb1_s cn50xx;
	struct cvmx_l2c_lfb1_s cn52xx;
	struct cvmx_l2c_lfb1_s cn52xxp1;
	struct cvmx_l2c_lfb1_s cn56xx;
	struct cvmx_l2c_lfb1_s cn56xxp1;
	struct cvmx_l2c_lfb1_s cn58xx;
	struct cvmx_l2c_lfb1_s cn58xxp1;
};

union cvmx_l2c_lfb2 {
	uint64_t u64;
	struct cvmx_l2c_lfb2_s {
		uint64_t reserved_0_63:64;
	} s;
	struct cvmx_l2c_lfb2_cn30xx {
		uint64_t reserved_27_63:37;
		uint64_t lfb_tag:19;
		uint64_t lfb_idx:8;
	} cn30xx;
	struct cvmx_l2c_lfb2_cn31xx {
		uint64_t reserved_27_63:37;
		uint64_t lfb_tag:17;
		uint64_t lfb_idx:10;
	} cn31xx;
	struct cvmx_l2c_lfb2_cn31xx cn38xx;
	struct cvmx_l2c_lfb2_cn31xx cn38xxp2;
	struct cvmx_l2c_lfb2_cn50xx {
		uint64_t reserved_27_63:37;
		uint64_t lfb_tag:20;
		uint64_t lfb_idx:7;
	} cn50xx;
	struct cvmx_l2c_lfb2_cn52xx {
		uint64_t reserved_27_63:37;
		uint64_t lfb_tag:18;
		uint64_t lfb_idx:9;
	} cn52xx;
	struct cvmx_l2c_lfb2_cn52xx cn52xxp1;
	struct cvmx_l2c_lfb2_cn56xx {
		uint64_t reserved_27_63:37;
		uint64_t lfb_tag:16;
		uint64_t lfb_idx:11;
	} cn56xx;
	struct cvmx_l2c_lfb2_cn56xx cn56xxp1;
	struct cvmx_l2c_lfb2_cn56xx cn58xx;
	struct cvmx_l2c_lfb2_cn56xx cn58xxp1;
};

union cvmx_l2c_lfb3 {
	uint64_t u64;
	struct cvmx_l2c_lfb3_s {
		uint64_t reserved_5_63:59;
		uint64_t stpartdis:1;
		uint64_t lfb_hwm:4;
	} s;
	struct cvmx_l2c_lfb3_cn30xx {
		uint64_t reserved_5_63:59;
		uint64_t stpartdis:1;
		uint64_t reserved_2_3:2;
		uint64_t lfb_hwm:2;
	} cn30xx;
	struct cvmx_l2c_lfb3_cn31xx {
		uint64_t reserved_5_63:59;
		uint64_t stpartdis:1;
		uint64_t reserved_3_3:1;
		uint64_t lfb_hwm:3;
	} cn31xx;
	struct cvmx_l2c_lfb3_s cn38xx;
	struct cvmx_l2c_lfb3_s cn38xxp2;
	struct cvmx_l2c_lfb3_cn31xx cn50xx;
	struct cvmx_l2c_lfb3_cn31xx cn52xx;
	struct cvmx_l2c_lfb3_cn31xx cn52xxp1;
	struct cvmx_l2c_lfb3_s cn56xx;
	struct cvmx_l2c_lfb3_s cn56xxp1;
	struct cvmx_l2c_lfb3_s cn58xx;
	struct cvmx_l2c_lfb3_s cn58xxp1;
};

union cvmx_l2c_oob {
	uint64_t u64;
	struct cvmx_l2c_oob_s {
		uint64_t reserved_2_63:62;
		uint64_t dwbena:1;
		uint64_t stena:1;
	} s;
	struct cvmx_l2c_oob_s cn52xx;
	struct cvmx_l2c_oob_s cn52xxp1;
	struct cvmx_l2c_oob_s cn56xx;
	struct cvmx_l2c_oob_s cn56xxp1;
};

union cvmx_l2c_oob1 {
	uint64_t u64;
	struct cvmx_l2c_oob1_s {
		uint64_t fadr:27;
		uint64_t fsrc:1;
		uint64_t reserved_34_35:2;
		uint64_t sadr:14;
		uint64_t reserved_14_19:6;
		uint64_t size:14;
	} s;
	struct cvmx_l2c_oob1_s cn52xx;
	struct cvmx_l2c_oob1_s cn52xxp1;
	struct cvmx_l2c_oob1_s cn56xx;
	struct cvmx_l2c_oob1_s cn56xxp1;
};

union cvmx_l2c_oob2 {
	uint64_t u64;
	struct cvmx_l2c_oob2_s {
		uint64_t fadr:27;
		uint64_t fsrc:1;
		uint64_t reserved_34_35:2;
		uint64_t sadr:14;
		uint64_t reserved_14_19:6;
		uint64_t size:14;
	} s;
	struct cvmx_l2c_oob2_s cn52xx;
	struct cvmx_l2c_oob2_s cn52xxp1;
	struct cvmx_l2c_oob2_s cn56xx;
	struct cvmx_l2c_oob2_s cn56xxp1;
};

union cvmx_l2c_oob3 {
	uint64_t u64;
	struct cvmx_l2c_oob3_s {
		uint64_t fadr:27;
		uint64_t fsrc:1;
		uint64_t reserved_34_35:2;
		uint64_t sadr:14;
		uint64_t reserved_14_19:6;
		uint64_t size:14;
	} s;
	struct cvmx_l2c_oob3_s cn52xx;
	struct cvmx_l2c_oob3_s cn52xxp1;
	struct cvmx_l2c_oob3_s cn56xx;
	struct cvmx_l2c_oob3_s cn56xxp1;
};

union cvmx_l2c_pfcx {
	uint64_t u64;
	struct cvmx_l2c_pfcx_s {
		uint64_t reserved_36_63:28;
		uint64_t pfcnt0:36;
	} s;
	struct cvmx_l2c_pfcx_s cn30xx;
	struct cvmx_l2c_pfcx_s cn31xx;
	struct cvmx_l2c_pfcx_s cn38xx;
	struct cvmx_l2c_pfcx_s cn38xxp2;
	struct cvmx_l2c_pfcx_s cn50xx;
	struct cvmx_l2c_pfcx_s cn52xx;
	struct cvmx_l2c_pfcx_s cn52xxp1;
	struct cvmx_l2c_pfcx_s cn56xx;
	struct cvmx_l2c_pfcx_s cn56xxp1;
	struct cvmx_l2c_pfcx_s cn58xx;
	struct cvmx_l2c_pfcx_s cn58xxp1;
};

union cvmx_l2c_pfctl {
	uint64_t u64;
	struct cvmx_l2c_pfctl_s {
		uint64_t reserved_36_63:28;
		uint64_t cnt3rdclr:1;
		uint64_t cnt2rdclr:1;
		uint64_t cnt1rdclr:1;
		uint64_t cnt0rdclr:1;
		uint64_t cnt3ena:1;
		uint64_t cnt3clr:1;
		uint64_t cnt3sel:6;
		uint64_t cnt2ena:1;
		uint64_t cnt2clr:1;
		uint64_t cnt2sel:6;
		uint64_t cnt1ena:1;
		uint64_t cnt1clr:1;
		uint64_t cnt1sel:6;
		uint64_t cnt0ena:1;
		uint64_t cnt0clr:1;
		uint64_t cnt0sel:6;
	} s;
	struct cvmx_l2c_pfctl_s cn30xx;
	struct cvmx_l2c_pfctl_s cn31xx;
	struct cvmx_l2c_pfctl_s cn38xx;
	struct cvmx_l2c_pfctl_s cn38xxp2;
	struct cvmx_l2c_pfctl_s cn50xx;
	struct cvmx_l2c_pfctl_s cn52xx;
	struct cvmx_l2c_pfctl_s cn52xxp1;
	struct cvmx_l2c_pfctl_s cn56xx;
	struct cvmx_l2c_pfctl_s cn56xxp1;
	struct cvmx_l2c_pfctl_s cn58xx;
	struct cvmx_l2c_pfctl_s cn58xxp1;
};

union cvmx_l2c_ppgrp {
	uint64_t u64;
	struct cvmx_l2c_ppgrp_s {
		uint64_t reserved_24_63:40;
		uint64_t pp11grp:2;
		uint64_t pp10grp:2;
		uint64_t pp9grp:2;
		uint64_t pp8grp:2;
		uint64_t pp7grp:2;
		uint64_t pp6grp:2;
		uint64_t pp5grp:2;
		uint64_t pp4grp:2;
		uint64_t pp3grp:2;
		uint64_t pp2grp:2;
		uint64_t pp1grp:2;
		uint64_t pp0grp:2;
	} s;
	struct cvmx_l2c_ppgrp_cn52xx {
		uint64_t reserved_8_63:56;
		uint64_t pp3grp:2;
		uint64_t pp2grp:2;
		uint64_t pp1grp:2;
		uint64_t pp0grp:2;
	} cn52xx;
	struct cvmx_l2c_ppgrp_cn52xx cn52xxp1;
	struct cvmx_l2c_ppgrp_s cn56xx;
	struct cvmx_l2c_ppgrp_s cn56xxp1;
};

union cvmx_l2c_qos_iobx {
	uint64_t u64;
	struct cvmx_l2c_qos_iobx_s {
		uint64_t reserved_6_63:58;
		uint64_t dwblvl:2;
		uint64_t reserved_2_3:2;
		uint64_t lvl:2;
	} s;
	struct cvmx_l2c_qos_iobx_s cn63xx;
	struct cvmx_l2c_qos_iobx_s cn63xxp1;
};

union cvmx_l2c_qos_ppx {
	uint64_t u64;
	struct cvmx_l2c_qos_ppx_s {
		uint64_t reserved_2_63:62;
		uint64_t lvl:2;
	} s;
	struct cvmx_l2c_qos_ppx_s cn63xx;
	struct cvmx_l2c_qos_ppx_s cn63xxp1;
};

union cvmx_l2c_qos_wgt {
	uint64_t u64;
	struct cvmx_l2c_qos_wgt_s {
		uint64_t reserved_32_63:32;
		uint64_t wgt3:8;
		uint64_t wgt2:8;
		uint64_t wgt1:8;
		uint64_t wgt0:8;
	} s;
	struct cvmx_l2c_qos_wgt_s cn63xx;
	struct cvmx_l2c_qos_wgt_s cn63xxp1;
};

union cvmx_l2c_rscx_pfc {
	uint64_t u64;
	struct cvmx_l2c_rscx_pfc_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_rscx_pfc_s cn63xx;
	struct cvmx_l2c_rscx_pfc_s cn63xxp1;
};

union cvmx_l2c_rsdx_pfc {
	uint64_t u64;
	struct cvmx_l2c_rsdx_pfc_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_rsdx_pfc_s cn63xx;
	struct cvmx_l2c_rsdx_pfc_s cn63xxp1;
};

union cvmx_l2c_spar0 {
	uint64_t u64;
	struct cvmx_l2c_spar0_s {
		uint64_t reserved_32_63:32;
		uint64_t umsk3:8;
		uint64_t umsk2:8;
		uint64_t umsk1:8;
		uint64_t umsk0:8;
	} s;
	struct cvmx_l2c_spar0_cn30xx {
		uint64_t reserved_4_63:60;
		uint64_t umsk0:4;
	} cn30xx;
	struct cvmx_l2c_spar0_cn31xx {
		uint64_t reserved_12_63:52;
		uint64_t umsk1:4;
		uint64_t reserved_4_7:4;
		uint64_t umsk0:4;
	} cn31xx;
	struct cvmx_l2c_spar0_s cn38xx;
	struct cvmx_l2c_spar0_s cn38xxp2;
	struct cvmx_l2c_spar0_cn50xx {
		uint64_t reserved_16_63:48;
		uint64_t umsk1:8;
		uint64_t umsk0:8;
	} cn50xx;
	struct cvmx_l2c_spar0_s cn52xx;
	struct cvmx_l2c_spar0_s cn52xxp1;
	struct cvmx_l2c_spar0_s cn56xx;
	struct cvmx_l2c_spar0_s cn56xxp1;
	struct cvmx_l2c_spar0_s cn58xx;
	struct cvmx_l2c_spar0_s cn58xxp1;
};

union cvmx_l2c_spar1 {
	uint64_t u64;
	struct cvmx_l2c_spar1_s {
		uint64_t reserved_32_63:32;
		uint64_t umsk7:8;
		uint64_t umsk6:8;
		uint64_t umsk5:8;
		uint64_t umsk4:8;
	} s;
	struct cvmx_l2c_spar1_s cn38xx;
	struct cvmx_l2c_spar1_s cn38xxp2;
	struct cvmx_l2c_spar1_s cn56xx;
	struct cvmx_l2c_spar1_s cn56xxp1;
	struct cvmx_l2c_spar1_s cn58xx;
	struct cvmx_l2c_spar1_s cn58xxp1;
};

union cvmx_l2c_spar2 {
	uint64_t u64;
	struct cvmx_l2c_spar2_s {
		uint64_t reserved_32_63:32;
		uint64_t umsk11:8;
		uint64_t umsk10:8;
		uint64_t umsk9:8;
		uint64_t umsk8:8;
	} s;
	struct cvmx_l2c_spar2_s cn38xx;
	struct cvmx_l2c_spar2_s cn38xxp2;
	struct cvmx_l2c_spar2_s cn56xx;
	struct cvmx_l2c_spar2_s cn56xxp1;
	struct cvmx_l2c_spar2_s cn58xx;
	struct cvmx_l2c_spar2_s cn58xxp1;
};

union cvmx_l2c_spar3 {
	uint64_t u64;
	struct cvmx_l2c_spar3_s {
		uint64_t reserved_32_63:32;
		uint64_t umsk15:8;
		uint64_t umsk14:8;
		uint64_t umsk13:8;
		uint64_t umsk12:8;
	} s;
	struct cvmx_l2c_spar3_s cn38xx;
	struct cvmx_l2c_spar3_s cn38xxp2;
	struct cvmx_l2c_spar3_s cn58xx;
	struct cvmx_l2c_spar3_s cn58xxp1;
};

union cvmx_l2c_spar4 {
	uint64_t u64;
	struct cvmx_l2c_spar4_s {
		uint64_t reserved_8_63:56;
		uint64_t umskiob:8;
	} s;
	struct cvmx_l2c_spar4_cn30xx {
		uint64_t reserved_4_63:60;
		uint64_t umskiob:4;
	} cn30xx;
	struct cvmx_l2c_spar4_cn30xx cn31xx;
	struct cvmx_l2c_spar4_s cn38xx;
	struct cvmx_l2c_spar4_s cn38xxp2;
	struct cvmx_l2c_spar4_s cn50xx;
	struct cvmx_l2c_spar4_s cn52xx;
	struct cvmx_l2c_spar4_s cn52xxp1;
	struct cvmx_l2c_spar4_s cn56xx;
	struct cvmx_l2c_spar4_s cn56xxp1;
	struct cvmx_l2c_spar4_s cn58xx;
	struct cvmx_l2c_spar4_s cn58xxp1;
};

union cvmx_l2c_tadx_ecc0 {
	uint64_t u64;
	struct cvmx_l2c_tadx_ecc0_s {
		uint64_t reserved_58_63:6;
		uint64_t ow3ecc:10;
		uint64_t reserved_42_47:6;
		uint64_t ow2ecc:10;
		uint64_t reserved_26_31:6;
		uint64_t ow1ecc:10;
		uint64_t reserved_10_15:6;
		uint64_t ow0ecc:10;
	} s;
	struct cvmx_l2c_tadx_ecc0_s cn63xx;
	struct cvmx_l2c_tadx_ecc0_s cn63xxp1;
};

union cvmx_l2c_tadx_ecc1 {
	uint64_t u64;
	struct cvmx_l2c_tadx_ecc1_s {
		uint64_t reserved_58_63:6;
		uint64_t ow7ecc:10;
		uint64_t reserved_42_47:6;
		uint64_t ow6ecc:10;
		uint64_t reserved_26_31:6;
		uint64_t ow5ecc:10;
		uint64_t reserved_10_15:6;
		uint64_t ow4ecc:10;
	} s;
	struct cvmx_l2c_tadx_ecc1_s cn63xx;
	struct cvmx_l2c_tadx_ecc1_s cn63xxp1;
};

union cvmx_l2c_tadx_ien {
	uint64_t u64;
	struct cvmx_l2c_tadx_ien_s {
		uint64_t reserved_9_63:55;
		uint64_t wrdislmc:1;
		uint64_t rddislmc:1;
		uint64_t noway:1;
		uint64_t vbfdbe:1;
		uint64_t vbfsbe:1;
		uint64_t tagdbe:1;
		uint64_t tagsbe:1;
		uint64_t l2ddbe:1;
		uint64_t l2dsbe:1;
	} s;
	struct cvmx_l2c_tadx_ien_s cn63xx;
	struct cvmx_l2c_tadx_ien_cn63xxp1 {
		uint64_t reserved_7_63:57;
		uint64_t noway:1;
		uint64_t vbfdbe:1;
		uint64_t vbfsbe:1;
		uint64_t tagdbe:1;
		uint64_t tagsbe:1;
		uint64_t l2ddbe:1;
		uint64_t l2dsbe:1;
	} cn63xxp1;
};

union cvmx_l2c_tadx_int {
	uint64_t u64;
	struct cvmx_l2c_tadx_int_s {
		uint64_t reserved_9_63:55;
		uint64_t wrdislmc:1;
		uint64_t rddislmc:1;
		uint64_t noway:1;
		uint64_t vbfdbe:1;
		uint64_t vbfsbe:1;
		uint64_t tagdbe:1;
		uint64_t tagsbe:1;
		uint64_t l2ddbe:1;
		uint64_t l2dsbe:1;
	} s;
	struct cvmx_l2c_tadx_int_s cn63xx;
};

union cvmx_l2c_tadx_pfc0 {
	uint64_t u64;
	struct cvmx_l2c_tadx_pfc0_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_tadx_pfc0_s cn63xx;
	struct cvmx_l2c_tadx_pfc0_s cn63xxp1;
};

union cvmx_l2c_tadx_pfc1 {
	uint64_t u64;
	struct cvmx_l2c_tadx_pfc1_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_tadx_pfc1_s cn63xx;
	struct cvmx_l2c_tadx_pfc1_s cn63xxp1;
};

union cvmx_l2c_tadx_pfc2 {
	uint64_t u64;
	struct cvmx_l2c_tadx_pfc2_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_tadx_pfc2_s cn63xx;
	struct cvmx_l2c_tadx_pfc2_s cn63xxp1;
};

union cvmx_l2c_tadx_pfc3 {
	uint64_t u64;
	struct cvmx_l2c_tadx_pfc3_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_tadx_pfc3_s cn63xx;
	struct cvmx_l2c_tadx_pfc3_s cn63xxp1;
};

union cvmx_l2c_tadx_prf {
	uint64_t u64;
	struct cvmx_l2c_tadx_prf_s {
		uint64_t reserved_32_63:32;
		uint64_t cnt3sel:8;
		uint64_t cnt2sel:8;
		uint64_t cnt1sel:8;
		uint64_t cnt0sel:8;
	} s;
	struct cvmx_l2c_tadx_prf_s cn63xx;
	struct cvmx_l2c_tadx_prf_s cn63xxp1;
};

union cvmx_l2c_tadx_tag {
	uint64_t u64;
	struct cvmx_l2c_tadx_tag_s {
		uint64_t reserved_46_63:18;
		uint64_t ecc:6;
		uint64_t reserved_36_39:4;
		uint64_t tag:19;
		uint64_t reserved_4_16:13;
		uint64_t use:1;
		uint64_t valid:1;
		uint64_t dirty:1;
		uint64_t lock:1;
	} s;
	struct cvmx_l2c_tadx_tag_s cn63xx;
	struct cvmx_l2c_tadx_tag_s cn63xxp1;
};

union cvmx_l2c_ver_id {
	uint64_t u64;
	struct cvmx_l2c_ver_id_s {
		uint64_t mask:64;
	} s;
	struct cvmx_l2c_ver_id_s cn63xx;
	struct cvmx_l2c_ver_id_s cn63xxp1;
};

union cvmx_l2c_ver_iob {
	uint64_t u64;
	struct cvmx_l2c_ver_iob_s {
		uint64_t reserved_1_63:63;
		uint64_t mask:1;
	} s;
	struct cvmx_l2c_ver_iob_s cn63xx;
	struct cvmx_l2c_ver_iob_s cn63xxp1;
};

union cvmx_l2c_ver_msc {
	uint64_t u64;
	struct cvmx_l2c_ver_msc_s {
		uint64_t reserved_2_63:62;
		uint64_t invl2:1;
		uint64_t dwb:1;
	} s;
	struct cvmx_l2c_ver_msc_s cn63xx;
};

union cvmx_l2c_ver_pp {
	uint64_t u64;
	struct cvmx_l2c_ver_pp_s {
		uint64_t reserved_6_63:58;
		uint64_t mask:6;
	} s;
	struct cvmx_l2c_ver_pp_s cn63xx;
	struct cvmx_l2c_ver_pp_s cn63xxp1;
};

union cvmx_l2c_virtid_iobx {
	uint64_t u64;
	struct cvmx_l2c_virtid_iobx_s {
		uint64_t reserved_14_63:50;
		uint64_t dwbid:6;
		uint64_t reserved_6_7:2;
		uint64_t id:6;
	} s;
	struct cvmx_l2c_virtid_iobx_s cn63xx;
	struct cvmx_l2c_virtid_iobx_s cn63xxp1;
};

union cvmx_l2c_virtid_ppx {
	uint64_t u64;
	struct cvmx_l2c_virtid_ppx_s {
		uint64_t reserved_6_63:58;
		uint64_t id:6;
	} s;
	struct cvmx_l2c_virtid_ppx_s cn63xx;
	struct cvmx_l2c_virtid_ppx_s cn63xxp1;
};

union cvmx_l2c_vrt_ctl {
	uint64_t u64;
	struct cvmx_l2c_vrt_ctl_s {
		uint64_t reserved_9_63:55;
		uint64_t ooberr:1;
		uint64_t reserved_7_7:1;
		uint64_t memsz:3;
		uint64_t numid:3;
		uint64_t enable:1;
	} s;
	struct cvmx_l2c_vrt_ctl_s cn63xx;
	struct cvmx_l2c_vrt_ctl_s cn63xxp1;
};

union cvmx_l2c_vrt_memx {
	uint64_t u64;
	struct cvmx_l2c_vrt_memx_s {
		uint64_t reserved_36_63:28;
		uint64_t parity:4;
		uint64_t data:32;
	} s;
	struct cvmx_l2c_vrt_memx_s cn63xx;
	struct cvmx_l2c_vrt_memx_s cn63xxp1;
};

union cvmx_l2c_wpar_iobx {
	uint64_t u64;
	struct cvmx_l2c_wpar_iobx_s {
		uint64_t reserved_16_63:48;
		uint64_t mask:16;
	} s;
	struct cvmx_l2c_wpar_iobx_s cn63xx;
	struct cvmx_l2c_wpar_iobx_s cn63xxp1;
};

union cvmx_l2c_wpar_ppx {
	uint64_t u64;
	struct cvmx_l2c_wpar_ppx_s {
		uint64_t reserved_16_63:48;
		uint64_t mask:16;
	} s;
	struct cvmx_l2c_wpar_ppx_s cn63xx;
	struct cvmx_l2c_wpar_ppx_s cn63xxp1;
};

union cvmx_l2c_xmcx_pfc {
	uint64_t u64;
	struct cvmx_l2c_xmcx_pfc_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_xmcx_pfc_s cn63xx;
	struct cvmx_l2c_xmcx_pfc_s cn63xxp1;
};

union cvmx_l2c_xmc_cmd {
	uint64_t u64;
	struct cvmx_l2c_xmc_cmd_s {
		uint64_t inuse:1;
		uint64_t cmd:6;
		uint64_t reserved_38_56:19;
		uint64_t addr:38;
	} s;
	struct cvmx_l2c_xmc_cmd_s cn63xx;
	struct cvmx_l2c_xmc_cmd_s cn63xxp1;
};

union cvmx_l2c_xmdx_pfc {
	uint64_t u64;
	struct cvmx_l2c_xmdx_pfc_s {
		uint64_t count:64;
	} s;
	struct cvmx_l2c_xmdx_pfc_s cn63xx;
	struct cvmx_l2c_xmdx_pfc_s cn63xxp1;
};

#endif
