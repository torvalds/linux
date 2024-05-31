/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

/* Register offsets from gpi-top */
#define GPI_GPII_MAP_EE_n_CH_k_VP_TABLE(n, k) \
		(0x17800 + (0x4 * (k)) + (0x80 * (n)))
#define GPI_GPII_n_CH_k_CNTXT_0_OFFS(n, k) \
		(0x20000 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_CH_k_CNTXT_2_OFFS(n, k) \
		(0x20008 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_CH_k_CNTXT_4_OFFS(n, k) \
		(0x20010 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_CH_k_CNTXT_6_OFFS(n, k) \
		(0x20018 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_CH_k_RE_FETCH_READ_PTR(n, k) \
		(0x20054 + (0x4000 * (n)) + (0x80 * (k)))

#define GPI_GPII_n_CH_k_CNTXT_0_ELEMENT_SIZE_BMSK (0xFF000000)
#define GPI_GPII_n_CH_k_CNTXT_0_ELEMENT_SIZE_SHFT (24)
#define GPI_GPII_n_CH_k_CNTXT_0_CHSTATE_BMSK (0xF00000)
#define GPI_GPII_n_CH_k_CNTXT_0_CHSTATE_SHFT (20)
#define GPI_GPII_n_CH_k_CNTXT_0_ERINDEX_BMSK (0x7C000)
#define GPI_GPII_n_CH_k_CNTXT_0_ERINDEX_SHFT (14)
#define GPI_GPII_n_CH_k_CNTXT_0_CHID_BMSK (0x1F00)
#define GPI_GPII_n_CH_k_CNTXT_0_CHID_SHFT (8)
#define GPI_GPII_n_CH_k_CNTXT_0_EE_BMSK (0xF0)
#define GPI_GPII_n_CH_k_CNTXT_0_EE_SHFT (4)
#define GPI_GPII_n_CH_k_CNTXT_0_CHTYPE_DIR_BMSK (0x8)
#define GPI_GPII_n_CH_k_CNTXT_0_CHTYPE_DIR_SHFT (3)
#define GPI_GPII_n_CH_k_CNTXT_0_CHTYPE_PROTO_BMSK (0x7)
#define GPI_GPII_n_CH_k_CNTXT_0_CHTYPE_PROTO_SHFT (0)
#define GPI_GPII_n_CH_k_CNTXT_0(el_size, erindex, chtype_dir, chtype_proto) \
	((el_size << 24) | (erindex << 14) | (chtype_dir << 3) | (chtype_proto))
#define GPI_CHTYPE_DIR_IN (0)
#define GPI_CHTYPE_DIR_OUT (1)
#define GPI_CHTYPE_PROTO_GPI (0x2)
#define GPI_GPII_n_CH_k_CNTXT_1_R_LENGTH_BMSK (0xFFFF)
#define GPI_GPII_n_CH_k_CNTXT_1_R_LENGTH_SHFT (0)
#define GPI_GPII_n_CH_k_DOORBELL_0_OFFS(n, k) (0x22000 + (0x4000 * (n)) \
					       + (0x8 * (k)))
#define GPI_GPII_n_CH_CMD_OFFS(n) (0x23008 + (0x4000 * (n)))
#define GPI_GPII_n_CH_CMD_OPCODE_BMSK (0xFF000000)
#define GPI_GPII_n_CH_CMD_OPCODE_SHFT (24)
#define GPI_GPII_n_CH_CMD_CHID_BMSK (0xFF)
#define GPI_GPII_n_CH_CMD_CHID_SHFT (0)
#define GPI_GPII_n_CH_CMD(opcode, chid) ((opcode << 24) | chid)
#define GPI_GPII_n_CH_CMD_ALLOCATE (0)
#define GPI_GPII_n_CH_CMD_START (1)
#define GPI_GPII_n_CH_CMD_STOP (2)
#define GPI_GPII_n_CH_CMD_RESET (9)
#define GPI_GPII_n_CH_CMD_DE_ALLOC (10)
#define GPI_GPII_n_CH_CMD_UART_SW_STALE (32)
#define GPI_GPII_n_CH_CMD_UART_RFR_READY (33)
#define GPI_GPII_n_CH_CMD_UART_RFR_NOT_READY (34)
#define GPI_GPII_n_CH_CMD_ENABLE_HID (48)
#define GPI_GPII_n_CH_CMD_DISABLE_HID (49)

/* EV Context Array */
#define GPI_GPII_n_EV_CH_k_CNTXT_0_OFFS(n, k) \
		(0x21000 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_EV_CH_k_CNTXT_2_OFFS(n, k) \
		(0x21008 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_EV_CH_k_CNTXT_4_OFFS(n, k) \
		(0x21010 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_EV_CH_k_CNTXT_6_OFFS(n, k) \
		(0x21018 + (0x4000 * (n)) + (0x80 * (k)))

#define GPI_GPII_n_EV_CH_k_CNTXT_0_ELEMENT_SIZE_BMSK (0xFF000000)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_ELEMENT_SIZE_SHFT (24)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_CHSTATE_BMSK (0xF00000)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_CHSTATE_SHFT (20)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_INTYPE_BMSK (0x10000)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_INTYPE_SHFT (16)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_EVCHID_BMSK (0xFF00)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_EVCHID_SHFT (8)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_EE_BMSK (0xF0)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_EE_SHFT (4)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_CHTYPE_BMSK (0xF)
#define GPI_GPII_n_EV_CH_k_CNTXT_0_CHTYPE_SHFT (0)
#define GPI_GPII_n_EV_CH_k_CNTXT_0(el_size, intype, chtype) \
	((el_size << 24) | (intype << 16) | (chtype))
#define GPI_INTTYPE_IRQ (1)
#define GPI_CHTYPE_GPI_EV (0x2)
#define GPI_GPII_n_EV_CH_k_CNTXT_1_R_LENGTH_BMSK (0xFFFF)
#define GPI_GPII_n_EV_CH_k_CNTXT_1_R_LENGTH_SHFT (0)

enum CNTXT_OFFS {
	CNTXT_0_CONFIG = 0x0,
	CNTXT_1_R_LENGTH = 0x4,
	CNTXT_2_RING_BASE_LSB = 0x8,
	CNTXT_3_RING_BASE_MSB = 0xC,
	CNTXT_4_RING_RP_LSB = 0x10,
	CNTXT_5_RING_RP_MSB = 0x14,
	CNTXT_6_RING_WP_LSB = 0x18,
	CNTXT_7_RING_WP_MSB = 0x1C,
	CNTXT_8_RING_INT_MOD = 0x20,
	CNTXT_9_RING_INTVEC = 0x24,
	CNTXT_10_RING_MSI_LSB = 0x28,
	CNTXT_11_RING_MSI_MSB = 0x2C,
	CNTXT_12_RING_RP_UPDATE_LSB = 0x30,
	CNTXT_13_RING_RP_UPDATE_MSB = 0x34,
};

#define GPI_GPII_n_EV_CH_k_DOORBELL_0_OFFS(n, k) \
	(0x22100 + (0x4000 * (n)) + (0x8 * (k)))
#define GPI_GPII_n_EV_CH_CMD_OFFS(n) \
	(0x23010 + (0x4000 * (n)))
#define GPI_GPII_n_EV_CH_CMD_OPCODE_BMSK (0xFF000000)
#define GPI_GPII_n_EV_CH_CMD_OPCODE_SHFT (24)
#define GPI_GPII_n_EV_CH_CMD_CHID_BMSK (0xFF)
#define GPI_GPII_n_EV_CH_CMD_CHID_SHFT (0)
#define GPI_GPII_n_EV_CH_CMD(opcode, chid) \
	((opcode << 24) | chid)
#define GPI_GPII_n_EV_CH_CMD_ALLOCATE (0x00)
#define GPI_GPII_n_EV_CH_CMD_RESET (0x09)
#define GPI_GPII_n_EV_CH_CMD_DE_ALLOC (0x0A)

#define GPI_GPII_n_CNTXT_TYPE_IRQ_OFFS(n) \
	(0x23080 + (0x4000 * (n)))

/* mask type register */
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_OFFS(n) \
	(0x23088 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_BMSK (0x7F)
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_SHFT (0)
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_GENERAL (0x40)
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_INTER_GPII_EV_CTRL (0x20)
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_INTER_GPII_CH_CTRL (0x10)
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_IEOB (0x08)
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_GLOB (0x04)
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_EV_CTRL (0x02)
#define GPI_GPII_n_CNTXT_TYPE_IRQ_MSK_CH_CTRL (0x01)

#define GPI_GPII_n_CNTXT_SRC_GPII_CH_IRQ_OFFS(n) \
	(0x23090 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_SRC_EV_CH_IRQ_OFFS(n) \
	(0x23094 + (0x4000 * (n)))

/* Mask channel control interrupt register */
#define GPI_GPII_n_CNTXT_SRC_CH_IRQ_MSK_OFFS(n) \
	(0x23098 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_SRC_CH_IRQ_MSK_BMSK (0x3)
#define GPI_GPII_n_CNTXT_SRC_CH_IRQ_MSK_SHFT (0)

/* Mask event control interrupt register */
#define GPI_GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_OFFS(n) \
	(0x2309C + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_BMSK (0x1)
#define GPI_GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_SHFT (0)

#define GPI_GPII_n_CNTXT_SRC_CH_IRQ_CLR_OFFS(n) \
	(0x230A0 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_SRC_EV_CH_IRQ_CLR_OFFS(n) \
	(0x230A4 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_SRC_IEOB_IRQ_OFFS(n) \
	(0x230B0 + (0x4000 * (n)))

/* Mask event interrupt register */
#define GPI_GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_OFFS(n) \
	(0x230B8 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_BMSK (0x1)
#define GPI_GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_SHFT (0)

#define GPI_GPII_n_CNTXT_SRC_IEOB_IRQ_CLR_OFFS(n) \
	(0x230C0 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_GLOB_IRQ_STTS_OFFS(n) \
	(0x23100 + (0x4000 * (n)))
#define GPI_GLOB_IRQ_ERROR_INT_MSK (0x1)
#define GPI_GLOB_IRQ_GP_INT1_MSK (0x2)
#define GPI_GLOB_IRQ_GP_INT2_MSK (0x4)
#define GPI_GLOB_IRQ_GP_INT3_MSK (0x8)

/* GPII specific Global - Enable bit register */
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_OFFS(n) \
	(0x23108 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_BMSK (0xF)
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_SHFT (0)
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_GP_INT3 (0x8)
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_GP_INT2 (0x4)
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_GP_INT1 (0x2)
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_ERROR_INT (0x1)

#define GPI_GPII_n_CNTXT_GLOB_IRQ_CLR_OFFS(n) \
	(0x23110 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_GPII_IRQ_STTS_OFFS(n) \
	(0x23118 + (0x4000 * (n)))

/* GPII general interrupt - Enable bit register */
#define GPI_GPII_n_CNTXT_GPII_IRQ_EN_OFFS(n) \
	(0x23120 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_GPII_IRQ_EN_BMSK (0xF)
#define GPI_GPII_n_CNTXT_GPII_IRQ_EN_SHFT (0)
#define GPI_GPII_n_CNTXT_GPII_IRQ_EN_STACK_OVRFLOW (0x8)
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_CMD_FIFO_OVRFLOW (0x4)
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_BUS_ERROR (0x2)
#define GPI_GPII_n_CNTXT_GLOB_IRQ_EN_BREAK_POINT (0x1)

#define GPI_GPII_n_CNTXT_GPII_IRQ_CLR_OFFS(n) \
	(0x23128 + (0x4000 * (n)))

/* GPII Interrupt Type register */
#define GPI_GPII_n_CNTXT_INTSET_OFFS(n) \
	(0x23180 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_INTSET_BMSK (0x1)
#define GPI_GPII_n_CNTXT_INTSET_SHFT (0)

#define GPI_GPII_n_CNTXT_MSI_BASE_LSB_OFFS(n) \
	(0x23188 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_MSI_BASE_MSB_OFFS(n) \
	(0x2318C + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_SCRATCH_0_OFFS(n) \
	(0x23400 + (0x4000 * (n)))
#define GPI_GPII_n_CNTXT_SCRATCH_1_OFFS(n) \
	(0x23404 + (0x4000 * (n)))

#define GPI_GPII_n_ERROR_LOG_OFFS(n) \
	(0x23200 + (0x4000 * (n)))
#define GPI_GPII_n_ERROR_LOG_CLR_OFFS(n) \
	(0x23210 + (0x4000 * (n)))

/* QOS Registers */
#define GPI_GPII_n_CH_k_QOS_OFFS(n, k) \
	(0x2005C + (0x4000 * (n)) + (0x80 * (k)))

/* Scratch registeres */
#define GPI_GPII_n_CH_k_SCRATCH_0_OFFS(n, k) \
	(0x20060 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_CH_K_SCRATCH_0(pair, int_config, proto, seid) \
	(((pair) << 16) | ((int_config) << 15) | ((proto) << 4) | (seid))
#define GPI_GPII_n_CH_k_SCRATCH_1_OFFS(n, k) \
	(0x20064 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_CH_k_SCRATCH_2_OFFS(n, k) \
	(0x20068 + (0x4000 * (n)) + (0x80 * (k)))
#define GPI_GPII_n_CH_k_SCRATCH_3_OFFS(n, k) \
	(0x2006C + (0x4000 * (n)) + (0x80 * (k)))

/* Debug registers */
#define GPI_DEBUG_PC_FOR_DEBUG (0x5048)
#define GPI_DEBUG_SW_RF_n_READ(n) (0x5100 + (0x4 * n))

/* GPI_DEBUG_QSB registers */
#define GPI_DEBUG_QSB_LOG_SEL (0x5050)
#define GPI_DEBUG_QSB_LOG_CLR (0x5058)
#define GPI_DEBUG_QSB_LOG_ERR_TRNS_ID (0x5060)
#define GPI_DEBUG_QSB_LOG_0 (0x5064)
#define GPI_DEBUG_QSB_LOG_1 (0x5068)
#define GPI_DEBUG_QSB_LOG_2 (0x506C)
#define GPI_DEBUG_QSB_LOG_LAST_MISC_ID(n) (0x5070 + (0x4*n))
#define GPI_DEBUG_BUSY_REG (0x5010)
