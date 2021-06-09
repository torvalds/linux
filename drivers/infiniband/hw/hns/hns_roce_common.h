/*
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _HNS_ROCE_COMMON_H
#define _HNS_ROCE_COMMON_H
#include <linux/bitfield.h>

#define roce_write(dev, reg, val)	writel((val), (dev)->reg_base + (reg))
#define roce_read(dev, reg)		readl((dev)->reg_base + (reg))
#define roce_raw_write(value, addr) \
	__raw_writel((__force u32)cpu_to_le32(value), (addr))

#define roce_get_field(origin, mask, shift)                                    \
	((le32_to_cpu(origin) & (mask)) >> (u32)(shift))

#define roce_get_bit(origin, shift) \
	roce_get_field((origin), (1ul << (shift)), (shift))

#define roce_set_field(origin, mask, shift, val)                               \
	do {                                                                   \
		(origin) &= ~cpu_to_le32(mask);                                \
		(origin) |=                                                    \
			cpu_to_le32(((u32)(val) << (u32)(shift)) & (mask));    \
	} while (0)

#define roce_set_bit(origin, shift, val)                                       \
	roce_set_field((origin), (1ul << (shift)), (shift), (val))

#define FIELD_LOC(field_type, field_h, field_l) field_type, field_h, field_l

#define _hr_reg_enable(ptr, field_type, field_h, field_l)                      \
	({                                                                     \
		const field_type *_ptr = ptr;                                  \
		*((__le32 *)_ptr + (field_h) / 32) |= cpu_to_le32(             \
			BIT((field_l) % 32) +                                  \
			BUILD_BUG_ON_ZERO((field_h) != (field_l)));            \
	})

#define hr_reg_enable(ptr, field) _hr_reg_enable(ptr, field)

#define _hr_reg_clear(ptr, field_type, field_h, field_l)                       \
	({                                                                     \
		const field_type *_ptr = ptr;                                  \
		BUILD_BUG_ON(((field_h) / 32) != ((field_l) / 32));            \
		*((__le32 *)_ptr + (field_h) / 32) &=                          \
			~cpu_to_le32(GENMASK((field_h) % 32, (field_l) % 32)); \
	})

#define hr_reg_clear(ptr, field) _hr_reg_clear(ptr, field)

#define _hr_reg_write(ptr, field_type, field_h, field_l, val)                  \
	({                                                                     \
		_hr_reg_clear(ptr, field_type, field_h, field_l);              \
		*((__le32 *)ptr + (field_h) / 32) |= cpu_to_le32(FIELD_PREP(   \
			GENMASK((field_h) % 32, (field_l) % 32), val));        \
	})

#define hr_reg_write(ptr, field, val) _hr_reg_write(ptr, field, val)

#define _hr_reg_read(ptr, field_type, field_h, field_l)                        \
	({                                                                     \
		const field_type *_ptr = ptr;                                  \
		BUILD_BUG_ON(((field_h) / 32) != ((field_l) / 32));            \
		FIELD_GET(GENMASK((field_h) % 32, (field_l) % 32),             \
			  le32_to_cpu(*((__le32 *)_ptr + (field_h) / 32)));    \
	})

#define hr_reg_read(ptr, field) _hr_reg_read(ptr, field)

#define ROCEE_GLB_CFG_ROCEE_DB_SQ_MODE_S 3
#define ROCEE_GLB_CFG_ROCEE_DB_OTH_MODE_S 4

#define ROCEE_GLB_CFG_SQ_EXT_DB_MODE_S 5

#define ROCEE_GLB_CFG_OTH_EXT_DB_MODE_S 6

#define ROCEE_GLB_CFG_ROCEE_PORT_ST_S 10
#define ROCEE_GLB_CFG_ROCEE_PORT_ST_M  \
	(((1UL << 6) - 1) << ROCEE_GLB_CFG_ROCEE_PORT_ST_S)

#define ROCEE_GLB_CFG_TRP_RAQ_DROP_EN_S 16

#define ROCEE_DMAE_USER_CFG1_ROCEE_STREAM_ID_TB_CFG_S 0
#define ROCEE_DMAE_USER_CFG1_ROCEE_STREAM_ID_TB_CFG_M  \
	(((1UL << 24) - 1) << ROCEE_DMAE_USER_CFG1_ROCEE_STREAM_ID_TB_CFG_S)

#define ROCEE_DMAE_USER_CFG1_ROCEE_CACHE_TB_CFG_S 24
#define ROCEE_DMAE_USER_CFG1_ROCEE_CACHE_TB_CFG_M  \
	(((1UL << 4) - 1) << ROCEE_DMAE_USER_CFG1_ROCEE_CACHE_TB_CFG_S)

#define ROCEE_DMAE_USER_CFG2_ROCEE_STREAM_ID_PKT_CFG_S 0
#define ROCEE_DMAE_USER_CFG2_ROCEE_STREAM_ID_PKT_CFG_M   \
	(((1UL << 24) - 1) << ROCEE_DMAE_USER_CFG2_ROCEE_STREAM_ID_PKT_CFG_S)

#define ROCEE_DMAE_USER_CFG2_ROCEE_CACHE_PKT_CFG_S 24
#define ROCEE_DMAE_USER_CFG2_ROCEE_CACHE_PKT_CFG_M   \
	(((1UL << 4) - 1) << ROCEE_DMAE_USER_CFG2_ROCEE_CACHE_PKT_CFG_S)

#define ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_S 0
#define ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_M   \
	(((1UL << 16) - 1) << ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_S)

#define ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_EMPTY_S 16
#define ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_EMPTY_M   \
	(((1UL << 16) - 1) << ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_EMPTY_S)

#define ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_S 0
#define ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_M   \
	(((1UL << 16) - 1) << ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_S)

#define ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_EMPTY_S 16
#define ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_EMPTY_M   \
	(((1UL << 16) - 1) << ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_EMPTY_S)

#define ROCEE_RAQ_WL_ROCEE_RAQ_WL_S 0
#define ROCEE_RAQ_WL_ROCEE_RAQ_WL_M   \
	(((1UL << 8) - 1) << ROCEE_RAQ_WL_ROCEE_RAQ_WL_S)

#define ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_POL_TIME_INTERVAL_S 0
#define ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_POL_TIME_INTERVAL_M   \
	(((1UL << 15) - 1) << \
	ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_POL_TIME_INTERVAL_S)

#define ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_RAQ_TIMEOUT_CHK_CFG_S 16
#define ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_RAQ_TIMEOUT_CHK_CFG_M   \
	(((1UL << 4) - 1) << \
	ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_RAQ_TIMEOUT_CHK_CFG_S)

#define ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_RAQ_TIMEOUT_CHK_EN_S 20

#define ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_EXT_RAQ_MODE 21

#define ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_SHIFT_S 0
#define ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_SHIFT_M   \
	(((1UL << 5) - 1) << ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_SHIFT_S)

#define ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_BA_H_S 5
#define ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_BA_H_M   \
	(((1UL << 5) - 1) << ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_BA_H_S)

#define ROCEE_EXT_DB_OTH_H_EXT_DB_OTH_SHIFT_S 0
#define ROCEE_EXT_DB_OTH_H_EXT_DB_OTH_SHIFT_M   \
	(((1UL << 5) - 1) << ROCEE_EXT_DB_OTH_H_EXT_DB_OTH_SHIFT_S)

#define ROCEE_EXT_DB_SQ_H_EXT_DB_OTH_BA_H_S 5
#define ROCEE_EXT_DB_SQ_H_EXT_DB_OTH_BA_H_M   \
	(((1UL << 5) - 1) << ROCEE_EXT_DB_SQ_H_EXT_DB_OTH_BA_H_S)

#define ROCEE_EXT_RAQ_H_EXT_RAQ_SHIFT_S 0
#define ROCEE_EXT_RAQ_H_EXT_RAQ_SHIFT_M   \
	(((1UL << 5) - 1) << ROCEE_EXT_RAQ_H_EXT_RAQ_SHIFT_S)

#define ROCEE_EXT_RAQ_H_EXT_RAQ_BA_H_S 8
#define ROCEE_EXT_RAQ_H_EXT_RAQ_BA_H_M   \
	(((1UL << 5) - 1) << ROCEE_EXT_RAQ_H_EXT_RAQ_BA_H_S)

#define ROCEE_BT_CMD_H_ROCEE_BT_CMD_IN_MDF_S 0
#define ROCEE_BT_CMD_H_ROCEE_BT_CMD_IN_MDF_M   \
	(((1UL << 19) - 1) << ROCEE_BT_CMD_H_ROCEE_BT_CMD_IN_MDF_S)

#define ROCEE_BT_CMD_H_ROCEE_BT_CMD_S 19

#define ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S 20
#define ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_M   \
	(((1UL << 2) - 1) << ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S)

#define ROCEE_BT_CMD_H_ROCEE_BT_CMD_BA_H_S 22
#define ROCEE_BT_CMD_H_ROCEE_BT_CMD_BA_H_M   \
	(((1UL << 5) - 1) << ROCEE_BT_CMD_H_ROCEE_BT_CMD_BA_H_S)

#define ROCEE_BT_CMD_H_ROCEE_BT_CMD_HW_SYNS_S 31

#define ROCEE_QP1C_CFG0_0_ROCEE_QP1C_QP_ST_S 0
#define ROCEE_QP1C_CFG0_0_ROCEE_QP1C_QP_ST_M   \
	(((1UL << 3) - 1) << ROCEE_QP1C_CFG0_0_ROCEE_QP1C_QP_ST_S)

#define ROCEE_QP1C_CFG3_0_ROCEE_QP1C_RQ_HEAD_S 0
#define ROCEE_QP1C_CFG3_0_ROCEE_QP1C_RQ_HEAD_M   \
	(((1UL << 15) - 1) << ROCEE_QP1C_CFG3_0_ROCEE_QP1C_RQ_HEAD_S)

#define ROCEE_MB6_ROCEE_MB_CMD_S 0
#define ROCEE_MB6_ROCEE_MB_CMD_M   \
	(((1UL << 8) - 1) << ROCEE_MB6_ROCEE_MB_CMD_S)

#define ROCEE_MB6_ROCEE_MB_CMD_MDF_S 8
#define ROCEE_MB6_ROCEE_MB_CMD_MDF_M   \
	(((1UL << 4) - 1) << ROCEE_MB6_ROCEE_MB_CMD_MDF_S)

#define ROCEE_MB6_ROCEE_MB_EVENT_S 14

#define ROCEE_MB6_ROCEE_MB_HW_RUN_S 15

#define ROCEE_MB6_ROCEE_MB_TOKEN_S 16
#define ROCEE_MB6_ROCEE_MB_TOKEN_M   \
	(((1UL << 16) - 1) << ROCEE_MB6_ROCEE_MB_TOKEN_S)

#define ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_INP_H_S 0
#define ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_INP_H_M   \
	(((1UL << 24) - 1) << ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_INP_H_S)

#define ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_MDF_S 24
#define ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_MDF_M   \
	(((1UL << 4) - 1) << ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_MDF_S)

#define ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_S 28
#define ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_M   \
	(((1UL << 3) - 1) << ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_S)

#define ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_HW_SYNS_S 31

#define ROCEE_SMAC_H_ROCEE_SMAC_H_S 0
#define ROCEE_SMAC_H_ROCEE_SMAC_H_M   \
	(((1UL << 16) - 1) << ROCEE_SMAC_H_ROCEE_SMAC_H_S)

#define ROCEE_SMAC_H_ROCEE_PORT_MTU_S 16
#define ROCEE_SMAC_H_ROCEE_PORT_MTU_M   \
	(((1UL << 4) - 1) << ROCEE_SMAC_H_ROCEE_PORT_MTU_S)

#define ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_S 0
#define ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_M   \
	(((1UL << 2) - 1) << ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_S)

#define ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_AEQE_SHIFT_S 8
#define ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_AEQE_SHIFT_M   \
	(((1UL << 4) - 1) << ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_AEQE_SHIFT_S)

#define ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQ_ALM_OVF_INT_ST_S 17

#define ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQ_BT_H_S 0
#define ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQ_BT_H_M   \
	(((1UL << 5) - 1) << ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQ_BT_H_S)

#define ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQE_CUR_IDX_S 16
#define ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQE_CUR_IDX_M   \
	(((1UL << 16) - 1) << ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQE_CUR_IDX_S)

#define ROCEE_CAEP_AEQE_CONS_IDX_CAEP_AEQE_CONS_IDX_S 0
#define ROCEE_CAEP_AEQE_CONS_IDX_CAEP_AEQE_CONS_IDX_M   \
	(((1UL << 16) - 1) << ROCEE_CAEP_AEQE_CONS_IDX_CAEP_AEQE_CONS_IDX_S)

#define ROCEE_CAEP_CEQC_SHIFT_CAEP_CEQ_ALM_OVF_INT_ST_S 16
#define ROCEE_CAEP_CE_IRQ_MASK_CAEP_CEQ_ALM_OVF_MASK_S 1
#define ROCEE_CAEP_CEQ_ALM_OVF_CAEP_CEQ_ALM_OVF_S 0

#define ROCEE_CAEP_AE_MASK_CAEP_AEQ_ALM_OVF_MASK_S 0
#define ROCEE_CAEP_AE_MASK_CAEP_AE_IRQ_MASK_S 1

#define ROCEE_CAEP_AE_ST_CAEP_AEQ_ALM_OVF_S 0

#define ROCEE_SDB_ISSUE_PTR_SDB_ISSUE_PTR_S 0
#define ROCEE_SDB_ISSUE_PTR_SDB_ISSUE_PTR_M   \
	(((1UL << 28) - 1) << ROCEE_SDB_ISSUE_PTR_SDB_ISSUE_PTR_S)

#define ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_S 0
#define ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_M   \
	(((1UL << 28) - 1) << ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_S)

#define ROCEE_SDB_INV_CNT_SDB_INV_CNT_S 0
#define ROCEE_SDB_INV_CNT_SDB_INV_CNT_M   \
	(((1UL << 16) - 1) << ROCEE_SDB_INV_CNT_SDB_INV_CNT_S)

#define ROCEE_SDB_RETRY_CNT_SDB_RETRY_CT_S	0
#define ROCEE_SDB_RETRY_CNT_SDB_RETRY_CT_M	\
	(((1UL << 16) - 1) << ROCEE_SDB_RETRY_CNT_SDB_RETRY_CT_S)

#define ROCEE_SDB_CNT_CMP_BITS 16

#define ROCEE_TSP_BP_ST_QH_FIFO_ENTRY_S	20

#define ROCEE_CNT_CLR_CE_CNT_CLR_CE_S 0

/*************ROCEE_REG DEFINITION****************/
#define ROCEE_VENDOR_ID_REG			0x0
#define ROCEE_VENDOR_PART_ID_REG		0x4

#define ROCEE_SYS_IMAGE_GUID_L_REG		0xC
#define ROCEE_SYS_IMAGE_GUID_H_REG		0x10

#define ROCEE_PORT_GID_L_0_REG			0x50
#define ROCEE_PORT_GID_ML_0_REG			0x54
#define ROCEE_PORT_GID_MH_0_REG			0x58
#define ROCEE_PORT_GID_H_0_REG			0x5C

#define ROCEE_BT_CMD_H_REG			0x204

#define ROCEE_SMAC_L_0_REG			0x240
#define ROCEE_SMAC_H_0_REG			0x244

#define ROCEE_QP1C_CFG3_0_REG			0x27C

#define ROCEE_CAEP_AEQE_CONS_IDX_REG		0x3AC
#define ROCEE_CAEP_CEQC_CONS_IDX_0_REG		0x3BC

#define ROCEE_ECC_UCERR_ALM1_REG		0xB38
#define ROCEE_ECC_UCERR_ALM2_REG		0xB3C
#define ROCEE_ECC_CERR_ALM1_REG			0xB44
#define ROCEE_ECC_CERR_ALM2_REG			0xB48

#define ROCEE_ACK_DELAY_REG			0x14
#define ROCEE_GLB_CFG_REG			0x18

#define ROCEE_DMAE_USER_CFG1_REG		0x40
#define ROCEE_DMAE_USER_CFG2_REG		0x44

#define ROCEE_DB_SQ_WL_REG			0x154
#define ROCEE_DB_OTHERS_WL_REG			0x158
#define ROCEE_RAQ_WL_REG			0x15C
#define ROCEE_WRMS_POL_TIME_INTERVAL_REG	0x160
#define ROCEE_EXT_DB_SQ_REG			0x164
#define ROCEE_EXT_DB_SQ_H_REG			0x168
#define ROCEE_EXT_DB_OTH_REG			0x16C

#define ROCEE_EXT_DB_OTH_H_REG			0x170
#define ROCEE_EXT_DB_SQ_WL_EMPTY_REG		0x174
#define ROCEE_EXT_DB_SQ_WL_REG			0x178
#define ROCEE_EXT_DB_OTHERS_WL_EMPTY_REG	0x17C
#define ROCEE_EXT_DB_OTHERS_WL_REG		0x180
#define ROCEE_EXT_RAQ_REG			0x184
#define ROCEE_EXT_RAQ_H_REG			0x188

#define ROCEE_CAEP_CE_INTERVAL_CFG_REG		0x190
#define ROCEE_CAEP_CE_BURST_NUM_CFG_REG		0x194
#define ROCEE_BT_CMD_L_REG			0x200

#define ROCEE_MB1_REG				0x210
#define ROCEE_MB6_REG				0x224
#define ROCEE_DB_SQ_L_0_REG			0x230
#define ROCEE_DB_OTHERS_L_0_REG			0x238
#define ROCEE_QP1C_CFG0_0_REG			0x270

#define ROCEE_CAEP_AEQC_AEQE_SHIFT_REG		0x3A0
#define ROCEE_CAEP_CEQC_SHIFT_0_REG		0x3B0
#define ROCEE_CAEP_CE_IRQ_MASK_0_REG		0x3C0
#define ROCEE_CAEP_CEQ_ALM_OVF_0_REG		0x3C4
#define ROCEE_CAEP_AE_MASK_REG			0x6C8
#define ROCEE_CAEP_AE_ST_REG			0x6CC

#define ROCEE_CAEP_CQE_WCMD_EMPTY		0x850
#define ROCEE_SCAEP_WR_CQE_CNT			0x8D0
#define ROCEE_ECC_UCERR_ALM0_REG		0xB34
#define ROCEE_ECC_CERR_ALM0_REG			0xB40

/* V2 ROCEE REG */
#define ROCEE_TX_CMQ_BASEADDR_L_REG		0x07000
#define ROCEE_TX_CMQ_BASEADDR_H_REG		0x07004
#define ROCEE_TX_CMQ_DEPTH_REG			0x07008
#define ROCEE_TX_CMQ_HEAD_REG			0x07010
#define ROCEE_TX_CMQ_TAIL_REG			0x07014

#define ROCEE_RX_CMQ_BASEADDR_L_REG		0x07018
#define ROCEE_RX_CMQ_BASEADDR_H_REG		0x0701c
#define ROCEE_RX_CMQ_DEPTH_REG			0x07020
#define ROCEE_RX_CMQ_TAIL_REG			0x07024
#define ROCEE_RX_CMQ_HEAD_REG			0x07028

#define ROCEE_VF_EQ_DB_CFG0_REG			0x238
#define ROCEE_VF_EQ_DB_CFG1_REG			0x23C

#define ROCEE_VF_ABN_INT_CFG_REG		0x13000
#define ROCEE_VF_ABN_INT_ST_REG			0x13004
#define ROCEE_VF_ABN_INT_EN_REG			0x13008
#define ROCEE_VF_EVENT_INT_EN_REG		0x1300c

#endif /* _HNS_ROCE_COMMON_H */
