// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <media/ipu-bridge.h>

#include "abi/ipu7_fw_common_abi.h"

#include "ipu7.h"
#include "ipu7-bus.h"
#include "ipu7-buttress.h"
#include "ipu7-buttress-regs.h"
#include "ipu7-cpd.h"
#include "ipu7-dma.h"
#include "ipu7-isys-csi2-regs.h"
#include "ipu7-mmu.h"
#include "ipu7-platform-regs.h"

#define IPU_PCI_BAR		0
#define IPU_PCI_PBBAR		4

static const unsigned int ipu7_csi_offsets[] = {
	IPU_CSI_PORT_A_ADDR_OFFSET,
	IPU_CSI_PORT_B_ADDR_OFFSET,
	IPU_CSI_PORT_C_ADDR_OFFSET,
	IPU_CSI_PORT_D_ADDR_OFFSET,
};

static struct ipu_isys_internal_pdata ipu7p5_isys_ipdata = {
	.csi2 = {
		.gpreg = IS_IO_CSI2_GPREGS_BASE,
	},
	.hw_variant = {
		.offset = IPU_UNIFIED_OFFSET,
		.nr_mmus = IPU7P5_IS_MMU_NUM,
		.mmu_hw = {
			{
				.name = "IS_FW_RD",
				.offset = IPU7P5_IS_MMU_FW_RD_OFFSET,
				.zlx_offset = IPU7P5_IS_ZLX_UC_RD_OFFSET,
				.uao_offset = IPU7P5_IS_UAO_UC_RD_OFFSET,
				.info_bits = 0x20005101,
				.refill = 0x00002726,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU7P5_IS_MMU_FW_RD_L1_BLOCKNR_REG,
				.l2_block = IPU7P5_IS_MMU_FW_RD_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7P5_IS_MMU_FW_RD_STREAM_NUM,
				.nr_l2streams = IPU7P5_IS_MMU_FW_RD_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x8, 0xa,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4,
				},
				.zlx_nr = IPU7P5_IS_ZLX_UC_RD_NUM,
				.zlx_axi_pool = {
					0x00000f30,
				},
				.zlx_en = {
					0, 1, 0, 0
				},
				.zlx_conf = {
					0x0,
				},
				.uao_p_num = IPU7P5_IS_UAO_UC_RD_PLANENUM,
				.uao_p2tlb = {
					0x00000049,
					0x0000004c,
					0x0000004d,
					0x00000000,
				},
			},
			{
				.name = "IS_FW_WR",
				.offset = IPU7P5_IS_MMU_FW_WR_OFFSET,
				.zlx_offset = IPU7P5_IS_ZLX_UC_WR_OFFSET,
				.uao_offset = IPU7P5_IS_UAO_UC_WR_OFFSET,
				.info_bits = 0x20005001,
				.refill = 0x00002524,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU7P5_IS_MMU_FW_WR_L1_BLOCKNR_REG,
				.l2_block = IPU7P5_IS_MMU_FW_WR_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7P5_IS_MMU_FW_WR_STREAM_NUM,
				.nr_l2streams = IPU7P5_IS_MMU_FW_WR_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x8, 0xa,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4,
				},
				.zlx_nr = IPU7P5_IS_ZLX_UC_WR_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					0, 1, 1, 0,
				},
				.zlx_conf = {
					0x0,
					0x00010101,
					0x00010101,
					0x0,
				},
				.uao_p_num = IPU7P5_IS_UAO_UC_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000049,
					0x0000004a,
					0x0000004b,
					0x00000000,
				},
			},
			{
				.name = "IS_DATA_WR_ISOC",
				.offset = IPU7P5_IS_MMU_M0_OFFSET,
				.zlx_offset = IPU7P5_IS_ZLX_M0_OFFSET,
				.uao_offset = IPU7P5_IS_UAO_M0_WR_OFFSET,
				.info_bits = 0x20004e01,
				.refill = 0x00002120,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU7P5_IS_MMU_M0_L1_BLOCKNR_REG,
				.l2_block = IPU7P5_IS_MMU_M0_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7P5_IS_MMU_M0_STREAM_NUM,
				.nr_l2streams = IPU7P5_IS_MMU_M0_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
				},
				.zlx_nr = IPU7P5_IS_ZLX_M0_NUM,
				.zlx_axi_pool = {
					0x00000f10,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
				},
				.zlx_conf = {
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
				},
				.uao_p_num = IPU7P5_IS_UAO_M0_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000041,
					0x00000042,
					0x00000043,
					0x00000044,
					0x00000041,
					0x00000042,
					0x00000043,
					0x00000044,
					0x00000041,
					0x00000042,
					0x00000043,
					0x00000044,
					0x00000041,
					0x00000042,
					0x00000043,
					0x00000044,
				},
			},
			{
				.name = "IS_DATA_WR_SNOOP",
				.offset = IPU7P5_IS_MMU_M1_OFFSET,
				.zlx_offset = IPU7P5_IS_ZLX_M1_OFFSET,
				.uao_offset = IPU7P5_IS_UAO_M1_WR_OFFSET,
				.info_bits = 0x20004f01,
				.refill = 0x00002322,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU7P5_IS_MMU_M1_L1_BLOCKNR_REG,
				.l2_block = IPU7P5_IS_MMU_M1_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7P5_IS_MMU_M1_STREAM_NUM,
				.nr_l2streams = IPU7P5_IS_MMU_M1_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
				},
				.zlx_nr = IPU7P5_IS_ZLX_M1_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
				},
				.zlx_conf = {
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
				},
				.uao_p_num = IPU7P5_IS_UAO_M1_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000045,
					0x00000046,
					0x00000047,
					0x00000048,
					0x00000045,
					0x00000046,
					0x00000047,
					0x00000048,
					0x00000045,
					0x00000046,
					0x00000047,
					0x00000048,
					0x00000045,
					0x00000046,
					0x00000047,
					0x00000048,
				},
			},
		},
		.cdc_fifos = 3,
		.cdc_fifo_threshold = {6, 8, 2},
		.dmem_offset = IPU_ISYS_DMEM_OFFSET,
		.spc_offset = IPU_ISYS_SPC_OFFSET,
	},
	.isys_dma_overshoot = IPU_ISYS_OVERALLOC_MIN,
};

static struct ipu_psys_internal_pdata ipu7p5_psys_ipdata = {
	.hw_variant = {
		.offset = IPU_UNIFIED_OFFSET,
		.nr_mmus = IPU7P5_PS_MMU_NUM,
		.mmu_hw = {
			{
				.name = "PS_FW_RD",
				.offset = IPU7P5_PS_MMU_FW_RD_OFFSET,
				.zlx_offset = IPU7P5_PS_ZLX_FW_RD_OFFSET,
				.uao_offset = IPU7P5_PS_UAO_FW_RD_OFFSET,
				.info_bits = 0x20004001,
				.refill = 0x00002726,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU7P5_PS_MMU_FW_RD_L1_BLOCKNR_REG,
				.l2_block = IPU7P5_PS_MMU_FW_RD_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7P5_PS_MMU_FW_RD_STREAM_NUM,
				.nr_l2streams = IPU7P5_PS_MMU_FW_RD_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000d,
					0x0000000f,
					0x00000011,
					0x00000012,
					0x00000013,
					0x00000014,
					0x00000016,
					0x00000018,
					0x00000019,
					0x0000001a,
					0x0000001a,
					0x0000001a,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
				},
				.zlx_nr = IPU7P5_PS_ZLX_FW_RD_NUM,
				.zlx_axi_pool = {
					0x00000f30,
				},
				.zlx_en = {
					0, 1, 0, 0, 1, 1, 0, 0,
					0, 1, 1, 0, 0, 0, 0, 0,
				},
				.zlx_conf = {
					0x00000000,
					0x00010101,
					0x00000000,
					0x00000000,
					0x00010101,
					0x00010101,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00010101,
					0x00010101,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
				},
				.uao_p_num = IPU7P5_PS_UAO_FW_RD_PLANENUM,
				.uao_p2tlb = {
					0x0000002e,
					0x00000035,
					0x00000036,
					0x00000031,
					0x00000037,
					0x00000038,
					0x00000039,
					0x00000032,
					0x00000033,
					0x0000003a,
					0x0000003b,
					0x0000003c,
					0x00000034,
					0x0,
					0x0,
					0x0,
				},
			},
			{
				.name = "PS_FW_WR",
				.offset = IPU7P5_PS_MMU_FW_WR_OFFSET,
				.zlx_offset = IPU7P5_PS_ZLX_FW_WR_OFFSET,
				.uao_offset = IPU7P5_PS_UAO_FW_WR_OFFSET,
				.info_bits = 0x20003e01,
				.refill = 0x00002322,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU7P5_PS_MMU_FW_WR_L1_BLOCKNR_REG,
				.l2_block = IPU7P5_PS_MMU_FW_WR_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7P5_PS_MMU_FW_WR_STREAM_NUM,
				.nr_l2streams = IPU7P5_PS_MMU_FW_WR_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000d,
					0x0000000e,
					0x0000000f,
					0x00000010,
					0x00000010,
					0x00000010,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
				},
				.zlx_nr = IPU7P5_PS_ZLX_FW_WR_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
				},
				.zlx_conf = {
					0x00000000,
					0x00010101,
					0x00010101,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
				},
				.uao_p_num = IPU7P5_PS_UAO_FW_WR_PLANENUM,
				.uao_p2tlb = {
					0x0000002e,
					0x0000002f,
					0x00000030,
					0x00000031,
					0x00000032,
					0x00000033,
					0x00000034,
					0x0,
					0x0,
					0x0,
				},
			},
			{
				.name = "PS_DATA_RD",
				.offset = IPU7P5_PS_MMU_SRT_RD_OFFSET,
				.zlx_offset = IPU7P5_PS_ZLX_DATA_RD_OFFSET,
				.uao_offset = IPU7P5_PS_UAO_SRT_RD_OFFSET,
				.info_bits = 0x20003f01,
				.refill = 0x00002524,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU7P5_PS_MMU_SRT_RD_L1_BLOCKNR_REG,
				.l2_block = IPU7P5_PS_MMU_SRT_RD_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7P5_PS_MMU_SRT_RD_STREAM_NUM,
				.nr_l2streams = IPU7P5_PS_MMU_SRT_RD_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000b,
					0x0000000d,
					0x0000000f,
					0x00000013,
					0x00000017,
					0x00000019,
					0x0000001b,
					0x0000001d,
					0x0000001f,
					0x0000002b,
					0x00000033,
					0x0000003f,
					0x00000047,
					0x00000049,
					0x0000004b,
					0x0000004c,
					0x0000004d,
					0x0000004e,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
					0x00000020,
					0x00000022,
					0x00000024,
					0x00000026,
					0x00000028,
					0x0000002a,
				},
				.zlx_nr = IPU7P5_PS_ZLX_DATA_RD_NUM,
				.zlx_axi_pool = {
					0x00000f30,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 0, 0, 0, 0,
				},
				.zlx_conf = {
					0x00030303,
					0x00010101,
					0x00010101,
					0x00030202,
					0x00010101,
					0x00010101,
					0x00030303,
					0x00030303,
					0x00010101,
					0x00030800,
					0x00030500,
					0x00020101,
					0x00042000,
					0x00031000,
					0x00042000,
					0x00031000,
					0x00020400,
					0x00010101,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
				},
				.uao_p_num = IPU7P5_PS_UAO_SRT_RD_PLANENUM,
				.uao_p2tlb = {
					0x0000001c,
					0x0000001d,
					0x0000001e,
					0x0000001f,
					0x00000020,
					0x00000021,
					0x00000022,
					0x00000023,
					0x00000024,
					0x00000025,
					0x00000026,
					0x00000027,
					0x00000028,
					0x00000029,
					0x0000002a,
					0x0000002b,
					0x0000002c,
					0x0000002d,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
				},
			},
			{
				.name = "PS_DATA_WR",
				.offset = IPU7P5_PS_MMU_SRT_WR_OFFSET,
				.zlx_offset = IPU7P5_PS_ZLX_DATA_WR_OFFSET,
				.uao_offset = IPU7P5_PS_UAO_SRT_WR_OFFSET,
				.info_bits = 0x20003d01,
				.refill = 0x00002120,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU7P5_PS_MMU_SRT_WR_L1_BLOCKNR_REG,
				.l2_block = IPU7P5_PS_MMU_SRT_WR_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7P5_PS_MMU_SRT_WR_STREAM_NUM,
				.nr_l2streams = IPU7P5_PS_MMU_SRT_WR_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000002,
					0x00000006,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
					0x00000020,
					0x00000022,
					0x00000024,
					0x00000028,
					0x0000002a,
					0x00000036,
					0x0000003e,
					0x00000040,
					0x00000042,
					0x0000004e,
					0x00000056,
					0x0000005c,
					0x00000068,
					0x00000070,
					0x00000076,
					0x00000077,
					0x00000078,
					0x00000079,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000006,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
					0x00000020,
					0x00000022,
					0x00000024,
					0x00000028,
					0x0000002a,
					0x00000036,
					0x0000003e,
					0x00000040,
					0x00000042,
					0x0000004e,
					0x00000056,
					0x0000005c,
					0x00000068,
					0x00000070,
					0x00000076,
					0x00000077,
					0x00000078,
					0x00000079,
				},
				.zlx_nr = IPU7P5_PS_ZLX_DATA_WR_NUM,
				.zlx_axi_pool = {
					0x00000f50,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					0, 0, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 0, 0, 0, 0,
				},
				.zlx_conf = {
					0x00010102,
					0x00030103,
					0x00030103,
					0x00010101,
					0x00010101,
					0x00030101,
					0x00010101,
					0x38010101,
					0x00000000,
					0x00000000,
					0x38010101,
					0x38010101,
					0x38010101,
					0x38010101,
					0x38010101,
					0x38010101,
					0x00030303,
					0x00010101,
					0x00042000,
					0x00031000,
					0x00010101,
					0x00010101,
					0x00042000,
					0x00031000,
					0x00031000,
					0x00042000,
					0x00031000,
					0x00031000,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
				},
				.uao_p_num = IPU7P5_PS_UAO_SRT_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000000,
					0x00000001,
					0x00000002,
					0x00000003,
					0x00000004,
					0x00000005,
					0x00000006,
					0x00000007,
					0x00000008,
					0x00000009,
					0x0000000a,
					0x0000000b,
					0x0000000c,
					0x0000000d,
					0x0000000e,
					0x0000000f,
					0x00000010,
					0x00000011,
					0x00000012,
					0x00000013,
					0x00000014,
					0x00000015,
					0x00000016,
					0x00000017,
					0x00000018,
					0x00000019,
					0x0000001a,
					0x0000001b,
					0x00000000,
					0x00000000,
					0x00000000,
					0x00000000,
				},
			},
		},
		.dmem_offset = IPU_PSYS_DMEM_OFFSET,
	},
};

static struct ipu_isys_internal_pdata ipu7_isys_ipdata = {
	.csi2 = {
		.gpreg = IS_IO_CSI2_GPREGS_BASE,
	},
	.hw_variant = {
		.offset = IPU_UNIFIED_OFFSET,
		.nr_mmus = IPU7_IS_MMU_NUM,
		.mmu_hw = {
			{
				.name = "IS_FW_RD",
				.offset = IPU7_IS_MMU_FW_RD_OFFSET,
				.zlx_offset = IPU7_IS_ZLX_UC_RD_OFFSET,
				.uao_offset = IPU7_IS_UAO_UC_RD_OFFSET,
				.info_bits = 0x20006701,
				.refill = 0x00002726,
				.collapse_en_bitmap = 0x0,
				.l1_block = IPU7_IS_MMU_FW_RD_L1_BLOCKNR_REG,
				.l2_block = IPU7_IS_MMU_FW_RD_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7_IS_MMU_FW_RD_STREAM_NUM,
				.nr_l2streams = IPU7_IS_MMU_FW_RD_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x8, 0xa,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4,
				},
				.zlx_nr = IPU7_IS_ZLX_UC_RD_NUM,
				.zlx_axi_pool = {
					0x00000f30,
				},
				.zlx_en = {
					0, 0, 0, 0
				},
				.zlx_conf = {
					0x0, 0x0, 0x0, 0x0,
				},
				.uao_p_num = IPU7_IS_UAO_UC_RD_PLANENUM,
				.uao_p2tlb = {
					0x00000061,
					0x00000064,
					0x00000065,
				},
			},
			{
				.name = "IS_FW_WR",
				.offset = IPU7_IS_MMU_FW_WR_OFFSET,
				.zlx_offset = IPU7_IS_ZLX_UC_WR_OFFSET,
				.uao_offset = IPU7_IS_UAO_UC_WR_OFFSET,
				.info_bits = 0x20006801,
				.refill = 0x00002524,
				.collapse_en_bitmap = 0x0,
				.l1_block = IPU7_IS_MMU_FW_WR_L1_BLOCKNR_REG,
				.l2_block = IPU7_IS_MMU_FW_WR_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7_IS_MMU_FW_WR_STREAM_NUM,
				.nr_l2streams = IPU7_IS_MMU_FW_WR_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x8, 0xa,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4,
				},
				.zlx_nr = IPU7_IS_ZLX_UC_WR_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					0, 1, 1, 0,
				},
				.zlx_conf = {
					0x0,
					0x00010101,
					0x00010101,
				},
				.uao_p_num = IPU7_IS_UAO_UC_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000061,
					0x00000062,
					0x00000063,
				},
			},
			{
				.name = "IS_DATA_WR_ISOC",
				.offset = IPU7_IS_MMU_M0_OFFSET,
				.zlx_offset = IPU7_IS_ZLX_M0_OFFSET,
				.uao_offset = IPU7_IS_UAO_M0_WR_OFFSET,
				.info_bits = 0x20006601,
				.refill = 0x00002120,
				.collapse_en_bitmap = 0x0,
				.l1_block = IPU7_IS_MMU_M0_L1_BLOCKNR_REG,
				.l2_block = IPU7_IS_MMU_M0_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7_IS_MMU_M0_STREAM_NUM,
				.nr_l2streams = IPU7_IS_MMU_M0_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x3, 0x6, 0x8, 0xa, 0xc, 0xe, 0x10,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4, 0x6, 0x8, 0xa, 0xc, 0xe,
				},
				.zlx_nr = IPU7_IS_ZLX_M0_NUM,
				.zlx_axi_pool = {
					0x00000f10,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
				},
				.zlx_conf = {
					0x00010103,
					0x00010103,
					0x00010101,
					0x00010101,
					0x00010101,
					0x00010101,
					0x00010101,
					0x00010101,
				},
				.uao_p_num = IPU7_IS_UAO_M0_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000049,
					0x0000004a,
					0x0000004b,
					0x0000004c,
					0x0000004d,
					0x0000004e,
					0x0000004f,
					0x00000050,
				},
			},
			{
				.name = "IS_DATA_WR_SNOOP",
				.offset = IPU7_IS_MMU_M1_OFFSET,
				.zlx_offset = IPU7_IS_ZLX_M1_OFFSET,
				.uao_offset = IPU7_IS_UAO_M1_WR_OFFSET,
				.info_bits = 0x20006901,
				.refill = 0x00002322,
				.collapse_en_bitmap = 0x0,
				.l1_block = IPU7_IS_MMU_M1_L1_BLOCKNR_REG,
				.l2_block = IPU7_IS_MMU_M1_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7_IS_MMU_M1_STREAM_NUM,
				.nr_l2streams = IPU7_IS_MMU_M1_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x3, 0x6, 0x9, 0xc,
					0xe, 0x10, 0x12, 0x14, 0x16,
					0x18, 0x1a, 0x1c, 0x1e, 0x20,
					0x22,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4, 0x6, 0x8,
					0xa, 0xc, 0xe, 0x10, 0x12,
					0x14, 0x16, 0x18, 0x1a, 0x1c,
					0x1e,
				},
				.zlx_nr = IPU7_IS_ZLX_M1_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
				},
				.zlx_conf = {
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010103,
					0x00010101,
					0x00010101,
					0x00010101,
					0x00010101,
					0x00010101,
					0x00010101,
					0x00010101,
					0x00010101,
				},
				.uao_p_num = IPU7_IS_UAO_M1_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000051,
					0x00000052,
					0x00000053,
					0x00000054,
					0x00000055,
					0x00000056,
					0x00000057,
					0x00000058,
					0x00000059,
					0x0000005a,
					0x0000005b,
					0x0000005c,
					0x0000005d,
					0x0000005e,
					0x0000005f,
					0x00000060,
				},
			},
		},
		.cdc_fifos = 3,
		.cdc_fifo_threshold = {6, 8, 2},
		.dmem_offset = IPU_ISYS_DMEM_OFFSET,
		.spc_offset = IPU_ISYS_SPC_OFFSET,
	},
	.isys_dma_overshoot = IPU_ISYS_OVERALLOC_MIN,
};

static struct ipu_psys_internal_pdata ipu7_psys_ipdata = {
	.hw_variant = {
		.offset = IPU_UNIFIED_OFFSET,
		.nr_mmus = IPU7_PS_MMU_NUM,
		.mmu_hw = {
			{
				.name = "PS_FW_RD",
				.offset = IPU7_PS_MMU_FW_RD_OFFSET,
				.zlx_offset = IPU7_PS_ZLX_FW_RD_OFFSET,
				.uao_offset = IPU7_PS_UAO_FW_RD_OFFSET,
				.info_bits = 0x20004801,
				.refill = 0x00002726,
				.collapse_en_bitmap = 0x0,
				.l1_block = IPU7_PS_MMU_FW_RD_L1_BLOCKNR_REG,
				.l2_block = IPU7_PS_MMU_FW_RD_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7_PS_MMU_FW_RD_STREAM_NUM,
				.nr_l2streams = IPU7_PS_MMU_FW_RD_STREAM_NUM,
				.l1_block_sz = {
					0, 0x8, 0xa, 0xc, 0xd,
					0xf, 0x11, 0x12, 0x13, 0x14,
					0x16, 0x18, 0x19, 0x1a, 0x1a,
					0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4, 0x6, 0x8,
					0xa, 0xc, 0xe, 0x10, 0x12,
					0x14, 0x16, 0x18, 0x1a, 0x1c,
					0x1e, 0x20, 0x22, 0x24, 0x26,
				},
				.zlx_nr = IPU7_PS_ZLX_FW_RD_NUM,
				.zlx_axi_pool = {
					0x00000f30,
				},
				.zlx_en = {
					0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0,
				},
				.zlx_conf = {
					0x0,
				},
				.uao_p_num = IPU7_PS_UAO_FW_RD_PLANENUM,
				.uao_p2tlb = {
					0x00000036,
					0x0000003d,
					0x0000003e,
					0x00000039,
					0x0000003f,
					0x00000040,
					0x00000041,
					0x0000003a,
					0x0000003b,
					0x00000042,
					0x00000043,
					0x00000044,
					0x0000003c,
				},
			},
			{
				.name = "PS_FW_WR",
				.offset = IPU7_PS_MMU_FW_WR_OFFSET,
				.zlx_offset = IPU7_PS_ZLX_FW_WR_OFFSET,
				.uao_offset = IPU7_PS_UAO_FW_WR_OFFSET,
				.info_bits = 0x20004601,
				.refill = 0x00002322,
				.collapse_en_bitmap = 0x0,
				.l1_block = IPU7_PS_MMU_FW_WR_L1_BLOCKNR_REG,
				.l2_block = IPU7_PS_MMU_FW_WR_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7_PS_MMU_FW_WR_STREAM_NUM,
				.nr_l2streams = IPU7_PS_MMU_FW_WR_STREAM_NUM,
				.l1_block_sz = {
					0, 0x8, 0xa, 0xc, 0xd,
					0xe, 0xf, 0x10, 0x10, 0x10,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4, 0x6, 0x8,
					0xa, 0xc, 0xe, 0x10, 0x12,
				},
				.zlx_nr = IPU7_PS_ZLX_FW_WR_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					0, 1, 1, 0, 0, 0, 0, 0,
					0, 0,
				},
				.zlx_conf = {
					0x0,
					0x00010101,
					0x00010101,
				},
				.uao_p_num = IPU7_PS_UAO_FW_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000036,
					0x00000037,
					0x00000038,
					0x00000039,
					0x0000003a,
					0x0000003b,
					0x0000003c,
				},
			},
			{
				.name = "PS_DATA_RD",
				.offset = IPU7_PS_MMU_SRT_RD_OFFSET,
				.zlx_offset = IPU7_PS_ZLX_DATA_RD_OFFSET,
				.uao_offset = IPU7_PS_UAO_SRT_RD_OFFSET,
				.info_bits = 0x20004701,
				.refill = 0x00002120,
				.collapse_en_bitmap = 0x0,
				.l1_block = IPU7_PS_MMU_SRT_RD_L1_BLOCKNR_REG,
				.l2_block = IPU7_PS_MMU_SRT_RD_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7_PS_MMU_SRT_RD_STREAM_NUM,
				.nr_l2streams = IPU7_PS_MMU_SRT_RD_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x4, 0x6, 0x8, 0xb,
					0xd, 0xf, 0x11, 0x13, 0x15,
					0x17, 0x23, 0x2b, 0x37, 0x3f,
					0x41, 0x43, 0x44, 0x45, 0x46,
					0x47, 0x48, 0x49, 0x4a, 0x4b,
					0x4c, 0x4d, 0x4e, 0x4f, 0x50,
					0x51, 0x52, 0x53, 0x55, 0x57,
					0x59, 0x5b, 0x5d, 0x5f, 0x61,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4, 0x6, 0x8,
					0xa, 0xc, 0xe, 0x10, 0x12,
					0x14, 0x16, 0x18, 0x1a, 0x1c,
					0x1e, 0x20, 0x22, 0x24, 0x26,
					0x28, 0x2a, 0x2c, 0x2e, 0x30,
					0x32, 0x34, 0x36, 0x38, 0x3a,
					0x3c, 0x3e, 0x40, 0x42, 0x44,
					0x46, 0x48, 0x4a, 0x4c, 0x4e,
				},
				.zlx_nr = IPU7_PS_ZLX_DATA_RD_NUM,
				.zlx_axi_pool = {
					0x00000f30,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
					0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0,
				},
				.zlx_conf = {
					0x00030303,
					0x00010101,
					0x00010101,
					0x00030202,
					0x00010101,
					0x00010101,
					0x00010101,
					0x00030800,
					0x00030500,
					0x00020101,
					0x00042000,
					0x00031000,
					0x00042000,
					0x00031000,
					0x00020400,
					0x00010101,
				},
				.uao_p_num = IPU7_PS_UAO_SRT_RD_PLANENUM,
				.uao_p2tlb = {
					0x00000022,
					0x00000023,
					0x00000024,
					0x00000025,
					0x00000026,
					0x00000027,
					0x00000028,
					0x00000029,
					0x0000002a,
					0x0000002b,
					0x0000002c,
					0x0000002d,
					0x0000002e,
					0x0000002f,
					0x00000030,
					0x00000031,
					0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
					0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
					0x0000001e,
					0x0000001f,
					0x00000020,
					0x00000021,
					0x00000032,
					0x00000033,
					0x00000034,
					0x00000035,
				},
			},
			{
				.name = "PS_DATA_WR",
				.offset = IPU7_PS_MMU_SRT_WR_OFFSET,
				.zlx_offset = IPU7_PS_ZLX_DATA_WR_OFFSET,
				.uao_offset = IPU7_PS_UAO_SRT_WR_OFFSET,
				.info_bits = 0x20004501,
				.refill = 0x00002120,
				.collapse_en_bitmap = 0x0,
				.l1_block = IPU7_PS_MMU_SRT_WR_L1_BLOCKNR_REG,
				.l2_block = IPU7_PS_MMU_SRT_WR_L2_BLOCKNR_REG,
				.nr_l1streams = IPU7_PS_MMU_SRT_WR_STREAM_NUM,
				.nr_l2streams = IPU7_PS_MMU_SRT_WR_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x2, 0x6, 0xa, 0xc,
					0xe, 0x10, 0x12, 0x14, 0x16,
					0x18, 0x1a, 0x1c, 0x1e, 0x20,
					0x22, 0x24, 0x26, 0x32, 0x3a,
					0x3c, 0x3e, 0x4a, 0x52, 0x58,
					0x64, 0x6c, 0x72, 0x7e, 0x86,
					0x8c, 0x8d, 0x8e, 0x8f, 0x90,
					0x91, 0x92, 0x94, 0x96, 0x98,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4, 0x6, 0x8,
					0xa, 0xc, 0xe, 0x10, 0x12,
					0x14, 0x16, 0x18, 0x1a, 0x1c,
					0x1e, 0x20, 0x22, 0x24, 0x26,
					0x28, 0x2a, 0x2c, 0x2e, 0x30,
					0x32, 0x34, 0x36, 0x38, 0x3a,
					0x3c, 0x3e, 0x40, 0x42, 0x44,
					0x46, 0x48, 0x4a, 0x4c, 0x4e,
				},
				.zlx_nr = IPU7_PS_ZLX_DATA_WR_NUM,
				.zlx_axi_pool = {
					0x00000f50,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					0, 0, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 0, 0,
				},
				.zlx_conf = {
					0x00010102,
					0x00030103,
					0x00030103,
					0x00010101,
					0x00010101,
					0x00030101,
					0x00010101,
					0x38010101,
					0x0,
					0x0,
					0x38010101,
					0x38010101,
					0x38010101,
					0x38010101,
					0x38010101,
					0x38010101,
					0x00010101,
					0x00042000,
					0x00031000,
					0x00010101,
					0x00010101,
					0x00042000,
					0x00031000,
					0x00031000,
					0x00042000,
					0x00031000,
					0x00031000,
					0x00042000,
					0x00031000,
					0x00031000,
					0x0,
					0x0,
				},
				.uao_p_num = IPU7_PS_UAO_SRT_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000000,
					0x00000001,
					0x00000002,
					0x00000003,
					0x00000004,
					0x00000005,
					0x00000006,
					0x00000007,
					0x00000008,
					0x00000009,
					0x0000000a,
					0x0000000b,
					0x0000000c,
					0x0000000d,
					0x0000000e,
					0x0000000f,
					0x00000010,
					0x00000011,
					0x00000012,
					0x00000013,
					0x00000014,
					0x00000015,
					0x00000016,
					0x00000017,
					0x00000018,
					0x00000019,
					0x0000001a,
					0x0000001b,
					0x0000001c,
					0x0000001d,
					0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
					0x0000001e,
					0x0000001f,
					0x00000020,
					0x00000021,
				},
			},
		},
		.dmem_offset = IPU_PSYS_DMEM_OFFSET,
	},
};

static struct ipu_isys_internal_pdata ipu8_isys_ipdata = {
	.csi2 = {
		.gpreg = IPU8_IS_IO_CSI2_GPREGS_BASE,
	},
	.hw_variant = {
		.offset = IPU_UNIFIED_OFFSET,
		.nr_mmus = IPU8_IS_MMU_NUM,
		.mmu_hw = {
			{
				.name = "IS_FW_RD",
				.offset = IPU8_IS_MMU_FW_RD_OFFSET,
				.zlx_offset = IPU8_IS_ZLX_UC_RD_OFFSET,
				.uao_offset = IPU8_IS_UAO_UC_RD_OFFSET,
				.info_bits = 0x20005101,
				.refill = 0x00002726,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU8_IS_MMU_FW_RD_L1_BLOCKNR_REG,
				.l2_block = IPU8_IS_MMU_FW_RD_L2_BLOCKNR_REG,
				.nr_l1streams = IPU8_IS_MMU_FW_RD_STREAM_NUM,
				.nr_l2streams = IPU8_IS_MMU_FW_RD_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x8, 0xa,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4,
				},
				.zlx_nr = IPU8_IS_ZLX_UC_RD_NUM,
				.zlx_axi_pool = {
					0x00000f30,
				},
				.zlx_en = {
					0, 1, 0, 0
				},
				.zlx_conf = {
					0, 2, 0, 0
				},
				.uao_p_num = IPU8_IS_UAO_UC_RD_PLANENUM,
				.uao_p2tlb = {
					0x00000049,
					0x0000004c,
					0x0000004d,
					0x00000000,
				},
			},
			{
				.name = "IS_FW_WR",
				.offset = IPU8_IS_MMU_FW_WR_OFFSET,
				.zlx_offset = IPU8_IS_ZLX_UC_WR_OFFSET,
				.uao_offset = IPU8_IS_UAO_UC_WR_OFFSET,
				.info_bits = 0x20005001,
				.refill = 0x00002524,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU8_IS_MMU_FW_WR_L1_BLOCKNR_REG,
				.l2_block = IPU8_IS_MMU_FW_WR_L2_BLOCKNR_REG,
				.nr_l1streams = IPU8_IS_MMU_FW_WR_STREAM_NUM,
				.nr_l2streams = IPU8_IS_MMU_FW_WR_STREAM_NUM,
				.l1_block_sz = {
					0x0, 0x8, 0xa,
				},
				.l2_block_sz = {
					0x0, 0x2, 0x4,
				},
				.zlx_nr = IPU8_IS_ZLX_UC_WR_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					0, 1, 1, 0,
				},
				.zlx_conf = {
					0x0,
					0x2,
					0x2,
					0x0,
				},
				.uao_p_num = IPU8_IS_UAO_UC_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000049,
					0x0000004a,
					0x0000004b,
					0x00000000,
				},
			},
			{
				.name = "IS_DATA_WR_ISOC",
				.offset = IPU8_IS_MMU_M0_OFFSET,
				.zlx_offset = IPU8_IS_ZLX_M0_OFFSET,
				.uao_offset = IPU8_IS_UAO_M0_WR_OFFSET,
				.info_bits = 0x20004e01,
				.refill = 0x00002120,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU8_IS_MMU_M0_L1_BLOCKNR_REG,
				.l2_block = IPU8_IS_MMU_M0_L2_BLOCKNR_REG,
				.nr_l1streams = IPU8_IS_MMU_M0_STREAM_NUM,
				.nr_l2streams = IPU8_IS_MMU_M0_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
				},
				.zlx_nr = IPU8_IS_ZLX_M0_NUM,
				.zlx_axi_pool = {
					0x00000f10,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
				},
				.zlx_conf = {
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
				},
				.uao_p_num = IPU8_IS_UAO_M0_WR_PLANENUM,
				.uao_p2tlb = {
					0x0000003b,
					0x0000003c,
					0x0000003d,
					0x0000003e,
					0x0000003b,
					0x0000003c,
					0x0000003d,
					0x0000003e,
					0x0000003b,
					0x0000003c,
					0x0000003d,
					0x0000003e,
					0x0000003b,
					0x0000003c,
					0x0000003d,
					0x0000003e,
				},
			},
			{
				.name = "IS_DATA_WR_SNOOP",
				.offset = IPU8_IS_MMU_M1_OFFSET,
				.zlx_offset = IPU8_IS_ZLX_M1_OFFSET,
				.uao_offset = IPU8_IS_UAO_M1_WR_OFFSET,
				.info_bits = 0x20004f01,
				.refill = 0x00002322,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU8_IS_MMU_M1_L1_BLOCKNR_REG,
				.l2_block = IPU8_IS_MMU_M1_L2_BLOCKNR_REG,
				.nr_l1streams = IPU8_IS_MMU_M1_STREAM_NUM,
				.nr_l2streams = IPU8_IS_MMU_M1_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
				},
				.zlx_nr = IPU8_IS_ZLX_M1_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
				},
				.zlx_conf = {
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
				},
				.uao_p_num = IPU8_IS_UAO_M1_WR_PLANENUM,
				.uao_p2tlb = {
					0x0000003f,
					0x00000040,
					0x00000041,
					0x00000042,
					0x0000003f,
					0x00000040,
					0x00000041,
					0x00000042,
					0x0000003f,
					0x00000040,
					0x00000041,
					0x00000042,
					0x0000003f,
					0x00000040,
					0x00000041,
					0x00000042,
				},
			},
			{
				.name = "IS_UPIPE",
				.offset = IPU8_IS_MMU_UPIPE_OFFSET,
				.zlx_offset = IPU8_IS_ZLX_UPIPE_OFFSET,
				.uao_offset = IPU8_IS_UAO_UPIPE_OFFSET,
				.info_bits = 0x20005201,
				.refill = 0x00002928,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU8_IS_MMU_UPIPE_L1_BLOCKNR_REG,
				.l2_block = IPU8_IS_MMU_UPIPE_L2_BLOCKNR_REG,
				.nr_l1streams = IPU8_IS_MMU_UPIPE_STREAM_NUM,
				.nr_l2streams = IPU8_IS_MMU_UPIPE_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
				},
				.zlx_nr = IPU8_IS_ZLX_UPIPE_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1,
				},
				.zlx_conf = {
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
					0x3,
				},
				.uao_p_num = IPU8_IS_UAO_UPIPE_PLANENUM,
				.uao_p2tlb = {
					0x00000043,
					0x00000044,
					0x00000045,
					0x00000046,
					0x00000047,
					0x00000048,
				},
			},
		},
		.cdc_fifos = 3,
		.cdc_fifo_threshold = {6, 8, 2},
		.dmem_offset = IPU_ISYS_DMEM_OFFSET,
		.spc_offset = IPU_ISYS_SPC_OFFSET,
	},
	.isys_dma_overshoot = IPU_ISYS_OVERALLOC_MIN,
};

static struct ipu_psys_internal_pdata ipu8_psys_ipdata = {
	.hw_variant = {
		.offset = IPU_UNIFIED_OFFSET,
		.nr_mmus = IPU8_PS_MMU_NUM,
		.mmu_hw = {
			{
				.name = "PS_FW_RD",
				.offset = IPU8_PS_MMU_FW_RD_OFFSET,
				.zlx_offset = IPU8_PS_ZLX_FW_RD_OFFSET,
				.uao_offset = IPU8_PS_UAO_FW_RD_OFFSET,
				.info_bits = 0x20003a01,
				.refill = 0x00002726,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU8_PS_MMU_FW_RD_L1_BLOCKNR_REG,
				.l2_block = IPU8_PS_MMU_FW_RD_L2_BLOCKNR_REG,
				.nr_l1streams = IPU8_PS_MMU_FW_RD_STREAM_NUM,
				.nr_l2streams = IPU8_PS_MMU_FW_RD_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000008,
					0x0000000a,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x00000018,
					0x00000018,
					0x00000018,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
				},
				.zlx_nr = IPU8_PS_ZLX_FW_RD_NUM,
				.zlx_axi_pool = {
					0x00000f30,
				},
				.zlx_en = {
					0, 1, 0, 0, 1, 1, 0, 0,
					0, 0, 0, 0,
				},
				.zlx_conf = {
					0x0,
					0x2,
					0x0,
					0x0,
					0x2,
					0x2,
					0x0,
					0x0,
					0x0,
					0x0,
					0x0,
					0x0,
				},
				.uao_p_num = IPU8_PS_UAO_FW_RD_PLANENUM,
				.uao_p2tlb = {
					0x0000002d,
					0x00000032,
					0x00000033,
					0x00000030,
					0x00000034,
					0x00000035,
					0x00000036,
					0x00000031,
					0x0,
					0x0,
					0x0,
					0x0,
				},
			},
			{
				.name = "PS_FW_WR",
				.offset = IPU8_PS_MMU_FW_WR_OFFSET,
				.zlx_offset = IPU8_PS_ZLX_FW_WR_OFFSET,
				.uao_offset = IPU8_PS_UAO_FW_WR_OFFSET,
				.info_bits = 0x20003901,
				.refill = 0x00002524,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU8_PS_MMU_FW_WR_L1_BLOCKNR_REG,
				.l2_block = IPU8_PS_MMU_FW_WR_L2_BLOCKNR_REG,
				.nr_l1streams = IPU8_PS_MMU_FW_WR_STREAM_NUM,
				.nr_l2streams = IPU8_PS_MMU_FW_WR_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000010,
					0x00000010,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
				},
				.zlx_nr = IPU8_PS_ZLX_FW_WR_NUM,
				.zlx_axi_pool = {
					0x00000f20,
				},
				.zlx_en = {
					0, 1, 1, 0, 0, 0, 0, 0,
				},
				.zlx_conf = {
					0x0, 0x2, 0x2, 0x0,
					0x0, 0x0, 0x0, 0x0,
				},
				.uao_p_num = IPU8_PS_UAO_FW_WR_PLANENUM,
				.uao_p2tlb = {
					0x0000002d,
					0x0000002e,
					0x0000002f,
					0x00000030,
					0x00000031,
					0x0,
					0x0,
					0x0,
				},
			},
			{
				.name = "PS_DATA_RD",
				.offset = IPU8_PS_MMU_SRT_RD_OFFSET,
				.zlx_offset = IPU8_PS_ZLX_DATA_RD_OFFSET,
				.uao_offset = IPU8_PS_UAO_SRT_RD_OFFSET,
				.info_bits = 0x20003801,
				.refill = 0x00002322,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU8_PS_MMU_SRT_RD_L1_BLOCKNR_REG,
				.l2_block = IPU8_PS_MMU_SRT_RD_L2_BLOCKNR_REG,
				.nr_l1streams = IPU8_PS_MMU_SRT_RD_STREAM_NUM,
				.nr_l2streams = IPU8_PS_MMU_SRT_RD_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000014,
					0x00000018,
					0x0000001c,
					0x0000001e,
					0x00000022,
					0x00000024,
					0x00000026,
					0x00000028,
					0x0000002a,
					0x0000002c,
					0x0000002e,
					0x00000030,
					0x00000032,
					0x00000036,
					0x0000003a,
					0x0000003c,
					0x0000003c,
					0x0000003c,
					0x0000003c,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
					0x00000020,
					0x00000022,
					0x00000024,
					0x00000026,
					0x00000028,
					0x0000002a,
					0x0000002c,
					0x0000002e,
					0x00000030,
					0x00000032,
				},
				.zlx_nr = IPU8_PS_ZLX_DATA_RD_NUM,
				.zlx_axi_pool = {
					0x00000f30,
				},
				.zlx_en = {
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 0, 0,
					0, 0,
				},
				.zlx_conf = {
					0x6, 0x3, 0x3, 0x6,
					0x2, 0x2, 0x6, 0x6,
					0x6, 0x3, 0x6, 0x3,
					0x3, 0x2, 0x2, 0x2,
					0x2, 0x2, 0x2, 0x6,
					0x6, 0x3, 0x0, 0x0,
					0x0, 0x0,
				},
				.uao_p_num = IPU8_PS_UAO_SRT_RD_PLANENUM,
				.uao_p2tlb = {
					0x00000017,
					0x00000018,
					0x00000019,
					0x0000001a,
					0x0000001b,
					0x0000001c,
					0x0000001d,
					0x0000001e,
					0x0000001f,
					0x00000020,
					0x00000021,
					0x00000022,
					0x00000023,
					0x00000024,
					0x00000025,
					0x00000026,
					0x00000027,
					0x00000028,
					0x00000029,
					0x0000002a,
					0x0000002b,
					0x0000002c,
					0x0,
					0x0,
					0x0,
					0x0,
				},
			},
			{
				.name = "PS_DATA_WR",
				.offset = IPU8_PS_MMU_SRT_WR_OFFSET,
				.zlx_offset = IPU8_PS_ZLX_DATA_WR_OFFSET,
				.uao_offset = IPU8_PS_UAO_SRT_WR_OFFSET,
				.info_bits = 0x20003701,
				.refill = 0x00002120,
				.collapse_en_bitmap = 0x1,
				.at_sp_arb_cfg = 0x1,
				.l1_block = IPU8_PS_MMU_SRT_WR_L1_BLOCKNR_REG,
				.l2_block = IPU8_PS_MMU_SRT_WR_L2_BLOCKNR_REG,
				.nr_l1streams = IPU8_PS_MMU_SRT_WR_STREAM_NUM,
				.nr_l2streams = IPU8_PS_MMU_SRT_WR_STREAM_NUM,
				.l1_block_sz = {
					0x00000000,
					0x00000002,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001c,
					0x0000001e,
					0x00000022,
					0x00000024,
					0x00000028,
					0x0000002a,
					0x0000002e,
					0x00000030,
					0x00000032,
					0x00000036,
					0x00000038,
					0x0000003a,
					0x0000003a,
					0x0000003a,
				},
				.l2_block_sz = {
					0x00000000,
					0x00000002,
					0x00000004,
					0x00000006,
					0x00000008,
					0x0000000a,
					0x0000000c,
					0x0000000e,
					0x00000010,
					0x00000012,
					0x00000014,
					0x00000016,
					0x00000018,
					0x0000001a,
					0x0000001c,
					0x0000001e,
					0x00000020,
					0x00000022,
					0x00000024,
					0x00000026,
					0x00000028,
					0x0000002a,
					0x0000002c,
					0x0000002e,
					0x00000030,
					0x00000032,
				},
				.zlx_nr = IPU8_PS_ZLX_DATA_WR_NUM,
				.zlx_axi_pool = {
					0x00000f50,
				},
				.zlx_en = {
					1, 1, 1, 0, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 0,
					0, 0,
				},
				.zlx_conf = {
					0x3,
					0x6,
					0x38000002,
					0x38000000,
					0x3,
					0x38000002,
					0x38000002,
					0x38000002,
					0x38000002,
					0x38000002,
					0x38000002,
					0x6,
					0x3,
					0x6,
					0x3,
					0x6,
					0x3,
					0x6,
					0x3,
					0x3,
					0x6,
					0x3,
					0x3,
					0x0,
					0x0,
					0x0,
				},
				.uao_p_num = IPU8_PS_UAO_SRT_WR_PLANENUM,
				.uao_p2tlb = {
					0x00000000,
					0x00000001,
					0x00000002,
					0x00000003,
					0x00000004,
					0x00000005,
					0x00000006,
					0x00000007,
					0x00000008,
					0x00000009,
					0x0000000a,
					0x0000000b,
					0x0000000c,
					0x0000000d,
					0x0000000e,
					0x0000000f,
					0x00000010,
					0x00000011,
					0x00000012,
					0x00000013,
					0x00000014,
					0x00000015,
					0x00000016,
					0x00000000,
					0x00000000,
					0x00000000,
				},
			},
		},
		.dmem_offset = IPU_PSYS_DMEM_OFFSET,
	},
};

static const struct ipu_buttress_ctrl ipu7_isys_buttress_ctrl = {
	.subsys_id = IPU_IS,
	.ratio = IPU7_IS_FREQ_CTL_DEFAULT_RATIO,
	.ratio_shift = IPU_FREQ_CTL_RATIO_SHIFT,
	.cdyn = IPU_FREQ_CTL_CDYN,
	.cdyn_shift = IPU_FREQ_CTL_CDYN_SHIFT,
	.freq_ctl = BUTTRESS_REG_IS_WORKPOINT_REQ,
	.pwr_sts_shift = IPU_BUTTRESS_PWR_STATE_IS_PWR_SHIFT,
	.pwr_sts_mask = IPU_BUTTRESS_PWR_STATE_IS_PWR_MASK,
	.pwr_sts_on = IPU_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU_BUTTRESS_PWR_STATE_DN_DONE,
	.ovrd_clk = BUTTRESS_OVERRIDE_IS_CLK,
	.own_clk_ack = BUTTRESS_OWN_ACK_IS_CLK,
};

static const struct ipu_buttress_ctrl ipu7_psys_buttress_ctrl = {
	.subsys_id = IPU_PS,
	.ratio = IPU7_PS_FREQ_CTL_DEFAULT_RATIO,
	.ratio_shift = IPU_FREQ_CTL_RATIO_SHIFT,
	.cdyn = IPU_FREQ_CTL_CDYN,
	.cdyn_shift = IPU_FREQ_CTL_CDYN_SHIFT,
	.freq_ctl = BUTTRESS_REG_PS_WORKPOINT_REQ,
	.pwr_sts_shift = IPU_BUTTRESS_PWR_STATE_PS_PWR_SHIFT,
	.pwr_sts_mask = IPU_BUTTRESS_PWR_STATE_PS_PWR_MASK,
	.pwr_sts_on = IPU_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU_BUTTRESS_PWR_STATE_DN_DONE,
	.ovrd_clk = BUTTRESS_OVERRIDE_PS_CLK,
	.own_clk_ack = BUTTRESS_OWN_ACK_PS_CLK,
};

static const struct ipu_buttress_ctrl ipu8_isys_buttress_ctrl = {
	.subsys_id = IPU_IS,
	.ratio = IPU8_IS_FREQ_CTL_DEFAULT_RATIO,
	.ratio_shift = IPU_FREQ_CTL_RATIO_SHIFT,
	.cdyn = IPU_FREQ_CTL_CDYN,
	.cdyn_shift = IPU_FREQ_CTL_CDYN_SHIFT,
	.freq_ctl = BUTTRESS_REG_IS_WORKPOINT_REQ,
	.pwr_sts_shift = IPU_BUTTRESS_PWR_STATE_IS_PWR_SHIFT,
	.pwr_sts_mask = IPU_BUTTRESS_PWR_STATE_IS_PWR_MASK,
	.pwr_sts_on = IPU_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU_BUTTRESS_PWR_STATE_DN_DONE,
};

static const struct ipu_buttress_ctrl ipu8_psys_buttress_ctrl = {
	.subsys_id = IPU_PS,
	.ratio = IPU8_PS_FREQ_CTL_DEFAULT_RATIO,
	.ratio_shift = IPU_FREQ_CTL_RATIO_SHIFT,
	.cdyn = IPU_FREQ_CTL_CDYN,
	.cdyn_shift = IPU_FREQ_CTL_CDYN_SHIFT,
	.freq_ctl = BUTTRESS_REG_PS_WORKPOINT_REQ,
	.pwr_sts_shift = IPU_BUTTRESS_PWR_STATE_PS_PWR_SHIFT,
	.pwr_sts_mask = IPU_BUTTRESS_PWR_STATE_PS_PWR_MASK,
	.pwr_sts_on = IPU_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU_BUTTRESS_PWR_STATE_DN_DONE,
	.own_clk_ack = BUTTRESS_OWN_ACK_PS_PLL,
};

void ipu_internal_pdata_init(struct ipu_isys_internal_pdata *isys_ipdata,
			     struct ipu_psys_internal_pdata *psys_ipdata)
{
	isys_ipdata->csi2.nports = ARRAY_SIZE(ipu7_csi_offsets);
	isys_ipdata->csi2.offsets = ipu7_csi_offsets;
	isys_ipdata->num_parallel_streams = IPU7_ISYS_NUM_STREAMS;
	psys_ipdata->hw_variant.spc_offset = IPU7_PSYS_SPC_OFFSET;
}

static int ipu7_isys_check_fwnode_graph(struct fwnode_handle *fwnode)
{
	struct fwnode_handle *endpoint;

	if (IS_ERR_OR_NULL(fwnode))
		return -EINVAL;

	endpoint = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (endpoint) {
		fwnode_handle_put(endpoint);
		return 0;
	}

	return ipu7_isys_check_fwnode_graph(fwnode->secondary);
}

static struct ipu7_bus_device *
ipu7_isys_init(struct pci_dev *pdev, struct device *parent,
	       const struct ipu_buttress_ctrl *ctrl, void __iomem *base,
	       const struct ipu_isys_internal_pdata *ipdata,
	       unsigned int nr)
{
	struct fwnode_handle *fwnode = dev_fwnode(&pdev->dev);
	struct ipu7_bus_device *isys_adev;
	struct device *dev = &pdev->dev;
	struct ipu7_isys_pdata *pdata;
	int ret;

	ret = ipu7_isys_check_fwnode_graph(fwnode);
	if (ret) {
		if (fwnode && !IS_ERR_OR_NULL(fwnode->secondary)) {
			dev_err(dev,
				"fwnode graph has no endpoints connection\n");
			return ERR_PTR(-EINVAL);
		}

		ret = ipu_bridge_init(dev, ipu_bridge_parse_ssdb);
		if (ret) {
			dev_err_probe(dev, ret, "IPU bridge init failed\n");
			return ERR_PTR(ret);
		}
	}

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->base = base;
	pdata->ipdata = ipdata;

	isys_adev = ipu7_bus_initialize_device(pdev, parent, pdata, ctrl,
					       IPU_ISYS_NAME);
	if (IS_ERR(isys_adev)) {
		dev_err_probe(dev, PTR_ERR(isys_adev),
			      "ipu7_bus_initialize_device isys failed\n");
		kfree(pdata);
		return ERR_CAST(isys_adev);
	}

	isys_adev->mmu = ipu7_mmu_init(dev, base, ISYS_MMID,
				       &ipdata->hw_variant);
	if (IS_ERR(isys_adev->mmu)) {
		dev_err_probe(dev, PTR_ERR(isys_adev->mmu),
			      "ipu7_mmu_init(isys_adev->mmu) failed\n");
		put_device(&isys_adev->auxdev.dev);
		kfree(pdata);
		return ERR_CAST(isys_adev->mmu);
	}

	isys_adev->mmu->dev = &isys_adev->auxdev.dev;
	isys_adev->subsys = IPU_IS;

	ret = ipu7_bus_add_device(isys_adev);
	if (ret) {
		kfree(pdata);
		return ERR_PTR(ret);
	}

	return isys_adev;
}

static struct ipu7_bus_device *
ipu7_psys_init(struct pci_dev *pdev, struct device *parent,
	       const struct ipu_buttress_ctrl *ctrl, void __iomem *base,
	       const struct ipu_psys_internal_pdata *ipdata, unsigned int nr)
{
	struct ipu7_bus_device *psys_adev;
	struct ipu7_psys_pdata *pdata;
	int ret;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->base = base;
	pdata->ipdata = ipdata;

	psys_adev = ipu7_bus_initialize_device(pdev, parent, pdata, ctrl,
					       IPU_PSYS_NAME);
	if (IS_ERR(psys_adev)) {
		dev_err_probe(&pdev->dev, PTR_ERR(psys_adev),
			      "ipu7_bus_initialize_device psys failed\n");
		kfree(pdata);
		return ERR_CAST(psys_adev);
	}

	psys_adev->mmu = ipu7_mmu_init(&pdev->dev, base, PSYS_MMID,
				       &ipdata->hw_variant);
	if (IS_ERR(psys_adev->mmu)) {
		dev_err_probe(&pdev->dev, PTR_ERR(psys_adev->mmu),
			      "ipu7_mmu_init(psys_adev->mmu) failed\n");
		put_device(&psys_adev->auxdev.dev);
		kfree(pdata);
		return ERR_CAST(psys_adev->mmu);
	}

	psys_adev->mmu->dev = &psys_adev->auxdev.dev;
	psys_adev->subsys = IPU_PS;

	ret = ipu7_bus_add_device(psys_adev);
	if (ret) {
		kfree(pdata);
		return ERR_PTR(ret);
	}

	return psys_adev;
}

static struct ia_gofo_msg_log_info_ts fw_error_log[IPU_SUBSYS_NUM];
void ipu7_dump_fw_error_log(const struct ipu7_bus_device *adev)
{
	void __iomem *reg = adev->isp->base + ((adev->subsys == IPU_IS) ?
					       BUTTRESS_REG_FW_GP24 :
					       BUTTRESS_REG_FW_GP8);

	memcpy_fromio(&fw_error_log[adev->subsys], reg,
		      sizeof(fw_error_log[adev->subsys]));
}
EXPORT_SYMBOL_NS_GPL(ipu7_dump_fw_error_log, "INTEL_IPU7");

static void ipu7_pci_config_setup(struct pci_dev *dev)
{
	u16 pci_command;

	pci_read_config_word(dev, PCI_COMMAND, &pci_command);
	pci_command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	pci_write_config_word(dev, PCI_COMMAND, pci_command);
}

static int ipu7_map_fw_code_region(struct ipu7_bus_device *sys,
				   void *data, size_t size)
{
	struct device *dev = &sys->auxdev.dev;
	struct ipu7_bus_device *adev = to_ipu7_bus_device(dev);
	struct sg_table *sgt = &sys->fw_sgt;
	struct ipu7_device *isp = adev->isp;
	struct pci_dev *pdev = isp->pdev;
	unsigned long n_pages, i;
	unsigned long attr = 0;
	struct page **pages;
	int ret;

	n_pages = PFN_UP(size);

	pages = kmalloc_array(n_pages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0; i < n_pages; i++) {
		struct page *p = vmalloc_to_page(data);

		if (!p) {
			ret = -ENODEV;
			goto out;
		}

		pages[i] = p;
		data += PAGE_SIZE;
	}

	ret = sg_alloc_table_from_pages(sgt, pages, n_pages, 0, size,
					GFP_KERNEL);
	if (ret) {
		ret = -ENOMEM;
		goto out;
	}

	if (!isp->secure_mode)
		attr |= DMA_ATTR_RESERVE_REGION;

	ret = dma_map_sgtable(&pdev->dev, sgt, DMA_BIDIRECTIONAL, 0);
	if (ret < 0) {
		dev_err(dev, "map fw code[%lu pages %u nents] failed\n",
			n_pages, sgt->nents);
		ret = -ENOMEM;
		sg_free_table(sgt);
		goto out;
	}

	ret = ipu7_dma_map_sgtable(sys, sgt, DMA_BIDIRECTIONAL, attr);
	if (ret) {
		dma_unmap_sgtable(&pdev->dev, sgt, DMA_BIDIRECTIONAL, 0);
		sg_free_table(sgt);
		goto out;
	}

	ipu7_dma_sync_sgtable(sys, sgt);

	dev_dbg(dev, "fw code region mapped at 0x%pad entries %d\n",
		&sgt->sgl->dma_address, sgt->nents);

out:
	kfree(pages);

	return ret;
}

static void ipu7_unmap_fw_code_region(struct ipu7_bus_device *sys)
{
	struct pci_dev *pdev = sys->isp->pdev;
	struct sg_table *sgt = &sys->fw_sgt;

	ipu7_dma_unmap_sgtable(sys, sgt, DMA_BIDIRECTIONAL, 0);
	dma_unmap_sgtable(&pdev->dev, sgt, DMA_BIDIRECTIONAL, 0);
	sg_free_table(sgt);
}

static int ipu7_init_fw_code_region_by_sys(struct ipu7_bus_device *sys,
					   const char *sys_name)
{
	struct device *dev = &sys->auxdev.dev;
	struct ipu7_device *isp = sys->isp;
	int ret;

	/* Copy FW binaries to specific location. */
	ret = ipu7_cpd_copy_binary(isp->cpd_fw->data, sys_name,
				   isp->fw_code_region, &sys->fw_entry);
	if (ret) {
		dev_err(dev, "%s binary not found.\n", sys_name);
		return ret;
	}

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to get runtime PM\n");
		return ret;
	}

	ret = ipu7_mmu_hw_init(sys->mmu);
	if (ret) {
		dev_err(dev, "Failed to set mmu hw\n");
		pm_runtime_put(dev);
		return ret;
	}

	/* Map code region. */
	ret = ipu7_map_fw_code_region(sys, isp->fw_code_region,
				      IPU_FW_CODE_REGION_SIZE);
	if (ret)
		dev_err(dev, "Failed to map fw code region for %s.\n",
			sys_name);

	ipu7_mmu_hw_cleanup(sys->mmu);
	pm_runtime_put(dev);

	return ret;
}

static int ipu7_init_fw_code_region(struct ipu7_device *isp)
{
	int ret;

	/*
	 * Allocate and map memory for FW execution.
	 * Not required in secure mode, in which FW runs in IMR.
	 */
	isp->fw_code_region = vmalloc(IPU_FW_CODE_REGION_SIZE);
	if (!isp->fw_code_region)
		return -ENOMEM;

	ret = ipu7_init_fw_code_region_by_sys(isp->isys, "isys");
	if (ret)
		goto fail_init;

	ret = ipu7_init_fw_code_region_by_sys(isp->psys, "psys");
	if (ret)
		goto fail_init;

	return 0;

fail_init:
	vfree(isp->fw_code_region);

	return ret;
}

static int ipu7_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ipu_buttress_ctrl *isys_ctrl = NULL, *psys_ctrl = NULL;
	struct fwnode_handle *fwnode = dev_fwnode(&pdev->dev);
	const struct ipu_buttress_ctrl *isys_buttress_ctrl;
	const struct ipu_buttress_ctrl *psys_buttress_ctrl;
	struct ipu_isys_internal_pdata *isys_ipdata;
	struct ipu_psys_internal_pdata *psys_ipdata;
	unsigned int dma_mask = IPU_DMA_MASK;
	struct device *dev = &pdev->dev;
	void __iomem *isys_base = NULL;
	void __iomem *psys_base = NULL;
	phys_addr_t phys, pb_phys;
	struct ipu7_device *isp;
	u32 is_es;
	int ret;

	if (!fwnode || fwnode_property_read_u32(fwnode, "is_es", &is_es))
		is_es = 0;

	isp = devm_kzalloc(dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->pdev = pdev;
	INIT_LIST_HEAD(&isp->devices);

	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "Enable PCI device failed\n");

	dev_info(dev, "Device 0x%x (rev: 0x%x)\n",
		 pdev->device, pdev->revision);

	phys = pci_resource_start(pdev, IPU_PCI_BAR);
	pb_phys = pci_resource_start(pdev, IPU_PCI_PBBAR);
	dev_info(dev, "IPU7 PCI BAR0 base %pap BAR2 base %pap\n",
		 &phys, &pb_phys);

	isp->base = pcim_iomap_region(pdev, IPU_PCI_BAR, IPU_NAME);
	if (IS_ERR(isp->base))
		return dev_err_probe(dev, PTR_ERR(isp->base),
				     "Failed to I/O memory remapping bar %u\n",
				     IPU_PCI_BAR);

	isp->pb_base = pcim_iomap_region(pdev, IPU_PCI_PBBAR, IPU_NAME);
	if (IS_ERR(isp->pb_base))
		return dev_err_probe(dev, PTR_ERR(isp->pb_base),
				     "Failed to I/O memory remapping bar %u\n",
				     IPU_PCI_PBBAR);

	dev_info(dev, "IPU7 PCI BAR0 mapped at %p\n BAR2 mapped at %p\n",
		 isp->base, isp->pb_base);

	pci_set_drvdata(pdev, isp);
	pci_set_master(pdev);

	switch (id->device) {
	case IPU7_PCI_ID:
		isp->hw_ver = IPU_VER_7;
		isp->cpd_fw_name = IPU7_FIRMWARE_NAME;
		isys_ipdata = &ipu7_isys_ipdata;
		psys_ipdata = &ipu7_psys_ipdata;
		isys_buttress_ctrl = &ipu7_isys_buttress_ctrl;
		psys_buttress_ctrl = &ipu7_psys_buttress_ctrl;
		break;
	case IPU7P5_PCI_ID:
		isp->hw_ver = IPU_VER_7P5;
		isp->cpd_fw_name = IPU7P5_FIRMWARE_NAME;
		isys_ipdata = &ipu7p5_isys_ipdata;
		psys_ipdata = &ipu7p5_psys_ipdata;
		isys_buttress_ctrl = &ipu7_isys_buttress_ctrl;
		psys_buttress_ctrl = &ipu7_psys_buttress_ctrl;
		break;
	case IPU8_PCI_ID:
		isp->hw_ver = IPU_VER_8;
		isp->cpd_fw_name = IPU8_FIRMWARE_NAME;
		isys_ipdata = &ipu8_isys_ipdata;
		psys_ipdata = &ipu8_psys_ipdata;
		isys_buttress_ctrl = &ipu8_isys_buttress_ctrl;
		psys_buttress_ctrl = &ipu8_psys_buttress_ctrl;
		break;
	default:
		WARN(1, "Unsupported IPU device");
		return -ENODEV;
	}

	ipu_internal_pdata_init(isys_ipdata, psys_ipdata);

	isys_base = isp->base + isys_ipdata->hw_variant.offset;
	psys_base = isp->base + psys_ipdata->hw_variant.offset;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(dma_mask));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set DMA mask\n");

	dma_set_max_seg_size(dev, UINT_MAX);

	ipu7_pci_config_setup(pdev);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to alloc irq vector\n");

	ret = ipu_buttress_init(isp);
	if (ret)
		goto pci_irq_free;

	dev_info(dev, "firmware cpd file: %s\n", isp->cpd_fw_name);

	ret = request_firmware(&isp->cpd_fw, isp->cpd_fw_name, dev);
	if (ret) {
		dev_err_probe(dev, ret,
			      "Requesting signed firmware %s failed\n",
			      isp->cpd_fw_name);
		goto buttress_exit;
	}

	ret = ipu7_cpd_validate_cpd_file(isp, isp->cpd_fw->data,
					 isp->cpd_fw->size);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to validate cpd\n");
		goto out_ipu_bus_del_devices;
	}

	isys_ctrl = devm_kmemdup(dev, isys_buttress_ctrl,
				 sizeof(*isys_buttress_ctrl), GFP_KERNEL);
	if (!isys_ctrl) {
		ret = -ENOMEM;
		goto out_ipu_bus_del_devices;
	}

	isp->isys = ipu7_isys_init(pdev, dev, isys_ctrl, isys_base,
				   isys_ipdata, 0);
	if (IS_ERR(isp->isys)) {
		ret = PTR_ERR(isp->isys);
		goto out_ipu_bus_del_devices;
	}

	psys_ctrl = devm_kmemdup(dev, psys_buttress_ctrl,
				 sizeof(*psys_buttress_ctrl), GFP_KERNEL);
	if (!psys_ctrl) {
		ret = -ENOMEM;
		goto out_ipu_bus_del_devices;
	}

	isp->psys = ipu7_psys_init(pdev, &isp->isys->auxdev.dev,
				   psys_ctrl, psys_base,
				   psys_ipdata, 0);
	if (IS_ERR(isp->psys)) {
		ret = PTR_ERR(isp->psys);
		goto out_ipu_bus_del_devices;
	}

	ret = devm_request_threaded_irq(dev, pdev->irq,
					ipu_buttress_isr,
					ipu_buttress_isr_threaded,
					IRQF_SHARED, IPU_NAME, isp);
	if (ret)
		goto out_ipu_bus_del_devices;

	if (!isp->secure_mode) {
		ret = ipu7_init_fw_code_region(isp);
		if (ret)
			goto out_ipu_bus_del_devices;
	} else {
		ret = pm_runtime_get_sync(&isp->psys->auxdev.dev);
		if (ret < 0) {
			dev_err(&isp->psys->auxdev.dev,
				"Failed to get runtime PM\n");
			goto out_ipu_bus_del_devices;
		}

		ret = ipu7_mmu_hw_init(isp->psys->mmu);
		if (ret) {
			dev_err_probe(&isp->pdev->dev, ret,
				      "Failed to init MMU hardware\n");
			goto out_ipu_bus_del_devices;
		}

		ret = ipu7_map_fw_code_region(isp->psys,
					      (void *)isp->cpd_fw->data,
					      isp->cpd_fw->size);
		if (ret) {
			dev_err_probe(&isp->pdev->dev, ret,
				      "failed to map fw image\n");
			goto out_ipu_bus_del_devices;
		}

		ret = ipu_buttress_authenticate(isp);
		if (ret) {
			dev_err_probe(&isp->pdev->dev, ret,
				      "FW authentication failed\n");
			goto out_ipu_bus_del_devices;
		}

		ipu7_mmu_hw_cleanup(isp->psys->mmu);
		pm_runtime_put(&isp->psys->auxdev.dev);
	}

	pm_runtime_put_noidle(dev);
	pm_runtime_allow(dev);

	isp->ipu7_bus_ready_to_probe = true;

	return 0;

out_ipu_bus_del_devices:
	if (!IS_ERR_OR_NULL(isp->isys) && isp->isys->fw_sgt.nents)
		ipu7_unmap_fw_code_region(isp->isys);
	if (!IS_ERR_OR_NULL(isp->psys) && isp->psys->fw_sgt.nents)
		ipu7_unmap_fw_code_region(isp->psys);
	if (!IS_ERR_OR_NULL(isp->psys) && !IS_ERR_OR_NULL(isp->psys->mmu))
		ipu7_mmu_cleanup(isp->psys->mmu);
	if (!IS_ERR_OR_NULL(isp->isys) && !IS_ERR_OR_NULL(isp->isys->mmu))
		ipu7_mmu_cleanup(isp->isys->mmu);
	if (!IS_ERR_OR_NULL(isp->psys))
		pm_runtime_put(&isp->psys->auxdev.dev);
	ipu7_bus_del_devices(pdev);
	release_firmware(isp->cpd_fw);
buttress_exit:
	ipu_buttress_exit(isp);
pci_irq_free:
	pci_free_irq_vectors(pdev);

	return ret;
}

static void ipu7_pci_remove(struct pci_dev *pdev)
{
	struct ipu7_device *isp = pci_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(isp->isys) && isp->isys->fw_sgt.nents)
		ipu7_unmap_fw_code_region(isp->isys);
	if (!IS_ERR_OR_NULL(isp->psys) && isp->psys->fw_sgt.nents)
		ipu7_unmap_fw_code_region(isp->psys);

	if (!IS_ERR_OR_NULL(isp->fw_code_region))
		vfree(isp->fw_code_region);

	ipu7_mmu_cleanup(isp->isys->mmu);
	ipu7_mmu_cleanup(isp->psys->mmu);

	ipu7_bus_del_devices(pdev);

	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);

	ipu_buttress_exit(isp);

	release_firmware(isp->cpd_fw);
}

static void ipu7_pci_reset_prepare(struct pci_dev *pdev)
{
	struct ipu7_device *isp = pci_get_drvdata(pdev);

	dev_warn(&pdev->dev, "FLR prepare\n");
	pm_runtime_forbid(&isp->pdev->dev);
}

static void ipu7_pci_reset_done(struct pci_dev *pdev)
{
	struct ipu7_device *isp = pci_get_drvdata(pdev);

	ipu_buttress_restore(isp);
	if (isp->secure_mode)
		ipu_buttress_reset_authentication(isp);

	isp->ipc_reinit = true;
	pm_runtime_allow(&isp->pdev->dev);

	dev_warn(&pdev->dev, "FLR completed\n");
}

/*
 * PCI base driver code requires driver to provide these to enable
 * PCI device level PM state transitions (D0<->D3)
 */
static int ipu7_suspend(struct device *dev)
{
	return 0;
}

static int ipu7_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ipu7_device *isp = pci_get_drvdata(pdev);
	struct ipu_buttress *b = &isp->buttress;
	int ret;

	isp->secure_mode = ipu_buttress_get_secure_mode(isp);
	dev_info(dev, "IPU7 in %s mode\n",
		 isp->secure_mode ? "secure" : "non-secure");

	ipu_buttress_restore(isp);

	ret = ipu_buttress_ipc_reset(isp, &b->cse);
	if (ret)
		dev_err(dev, "IPC reset protocol failed!\n");

	ret = pm_runtime_get_sync(&isp->psys->auxdev.dev);
	if (ret < 0) {
		dev_err(dev, "Failed to get runtime PM\n");
		return 0;
	}

	ret = ipu_buttress_authenticate(isp);
	if (ret)
		dev_err(dev, "FW authentication failed(%d)\n", ret);

	pm_runtime_put(&isp->psys->auxdev.dev);

	return 0;
}

static int ipu7_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ipu7_device *isp = pci_get_drvdata(pdev);
	int ret;

	ipu_buttress_restore(isp);

	if (isp->ipc_reinit) {
		struct ipu_buttress *b = &isp->buttress;

		isp->ipc_reinit = false;
		ret = ipu_buttress_ipc_reset(isp, &b->cse);
		if (ret)
			dev_err(dev, "IPC reset protocol failed!\n");
	}

	return 0;
}

static const struct dev_pm_ops ipu7_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(&ipu7_suspend, &ipu7_resume)
	RUNTIME_PM_OPS(&ipu7_suspend, &ipu7_runtime_resume, NULL)
};

static const struct pci_device_id ipu7_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IPU7_PCI_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IPU7P5_PCI_ID)},
	{0,}
};
MODULE_DEVICE_TABLE(pci, ipu7_pci_tbl);

static const struct pci_error_handlers pci_err_handlers = {
	.reset_prepare = ipu7_pci_reset_prepare,
	.reset_done = ipu7_pci_reset_done,
};

static struct pci_driver ipu7_pci_driver = {
	.name = IPU_NAME,
	.id_table = ipu7_pci_tbl,
	.probe = ipu7_pci_probe,
	.remove = ipu7_pci_remove,
	.driver = {
		.pm = &ipu7_pm_ops,
	},
	.err_handler = &pci_err_handlers,
};

module_pci_driver(ipu7_pci_driver);

MODULE_IMPORT_NS("INTEL_IPU_BRIDGE");
MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_AUTHOR("Tianshu Qiu <tian.shu.qiu@intel.com>");
MODULE_AUTHOR("Qingwu Zhang <qingwu.zhang@intel.com>");
MODULE_AUTHOR("Intel");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel ipu7 pci driver");
