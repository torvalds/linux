/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014, 2018-2022 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Intel Deutschland GmbH
 */
#ifndef	__iwl_prph_h__
#define __iwl_prph_h__
#include <linux/bitfield.h>

/*
 * Registers in this file are internal, not PCI bus memory mapped.
 * Driver accesses these via HBUS_TARG_PRPH_* registers.
 */
#define PRPH_BASE	(0x00000)
#define PRPH_END	(0xFFFFF)

/* APMG (power management) constants */
#define APMG_BASE			(PRPH_BASE + 0x3000)
#define APMG_CLK_CTRL_REG		(APMG_BASE + 0x0000)
#define APMG_CLK_EN_REG			(APMG_BASE + 0x0004)
#define APMG_CLK_DIS_REG		(APMG_BASE + 0x0008)
#define APMG_PS_CTRL_REG		(APMG_BASE + 0x000c)
#define APMG_PCIDEV_STT_REG		(APMG_BASE + 0x0010)
#define APMG_RFKILL_REG			(APMG_BASE + 0x0014)
#define APMG_RTC_INT_STT_REG		(APMG_BASE + 0x001c)
#define APMG_RTC_INT_MSK_REG		(APMG_BASE + 0x0020)
#define APMG_DIGITAL_SVR_REG		(APMG_BASE + 0x0058)
#define APMG_ANALOG_SVR_REG		(APMG_BASE + 0x006C)

#define APMS_CLK_VAL_MRB_FUNC_MODE	(0x00000001)
#define APMG_CLK_VAL_DMA_CLK_RQT	(0x00000200)
#define APMG_CLK_VAL_BSM_CLK_RQT	(0x00000800)

#define APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS	(0x00400000)
#define APMG_PS_CTRL_VAL_RESET_REQ		(0x04000000)
#define APMG_PS_CTRL_MSK_PWR_SRC		(0x03000000)
#define APMG_PS_CTRL_VAL_PWR_SRC_VMAIN		(0x00000000)
#define APMG_PS_CTRL_VAL_PWR_SRC_VAUX		(0x02000000)
#define APMG_SVR_VOLTAGE_CONFIG_BIT_MSK	(0x000001E0) /* bit 8:5 */
#define APMG_SVR_DIGITAL_VOLTAGE_1_32		(0x00000060)

#define APMG_PCIDEV_STT_VAL_PERSIST_DIS	(0x00000200)
#define APMG_PCIDEV_STT_VAL_L1_ACT_DIS	(0x00000800)
#define APMG_PCIDEV_STT_VAL_WAKE_ME	(0x00004000)

#define APMG_RTC_INT_STT_RFKILL		(0x10000000)

/* Device system time */
#define DEVICE_SYSTEM_TIME_REG 0xA0206C

/* Device NMI register and value for 8000 family and lower hw's */
#define DEVICE_SET_NMI_REG 0x00a01c30
#define DEVICE_SET_NMI_VAL_DRV BIT(7)
/* Device NMI register and value for 9000 family and above hw's */
#define UREG_NIC_SET_NMI_DRIVER 0x00a05c10
#define UREG_NIC_SET_NMI_DRIVER_NMI_FROM_DRIVER BIT(24)
#define UREG_NIC_SET_NMI_DRIVER_RESET_HANDSHAKE (BIT(24) | BIT(25))

/* Shared registers (0x0..0x3ff, via target indirect or periphery */
#define SHR_BASE	0x00a10000

/* Shared GP1 register */
#define SHR_APMG_GP1_REG		0x01dc
#define SHR_APMG_GP1_REG_PRPH		(SHR_BASE + SHR_APMG_GP1_REG)
#define SHR_APMG_GP1_WF_XTAL_LP_EN	0x00000004
#define SHR_APMG_GP1_CHICKEN_BIT_SELECT	0x80000000

/* Shared DL_CFG register */
#define SHR_APMG_DL_CFG_REG			0x01c4
#define SHR_APMG_DL_CFG_REG_PRPH		(SHR_BASE + SHR_APMG_DL_CFG_REG)
#define SHR_APMG_DL_CFG_RTCS_CLK_SELECTOR_MSK	0x000000c0
#define SHR_APMG_DL_CFG_RTCS_CLK_INTERNAL_XTAL	0x00000080
#define SHR_APMG_DL_CFG_DL_CLOCK_POWER_UP	0x00000100

/* Shared APMG_XTAL_CFG register */
#define SHR_APMG_XTAL_CFG_REG		0x1c0
#define SHR_APMG_XTAL_CFG_XTAL_ON_REQ	0x80000000

/*
 * Device reset for family 8000
 * write to bit 24 in order to reset the CPU
*/
#define RELEASE_CPU_RESET		(0x300C)
#define RELEASE_CPU_RESET_BIT		BIT(24)

/*****************************************************************************
 *                        7000/3000 series SHR DTS addresses                 *
 *****************************************************************************/

#define SHR_MISC_WFM_DTS_EN	(0x00a10024)
#define DTSC_CFG_MODE		(0x00a10604)
#define DTSC_VREF_AVG		(0x00a10648)
#define DTSC_VREF5_AVG		(0x00a1064c)
#define DTSC_CFG_MODE_PERIODIC	(0x2)
#define DTSC_PTAT_AVG		(0x00a10650)


/**
 * Tx Scheduler
 *
 * The Tx Scheduler selects the next frame to be transmitted, choosing TFDs
 * (Transmit Frame Descriptors) from up to 16 circular Tx queues resident in
 * host DRAM.  It steers each frame's Tx command (which contains the frame
 * data) into one of up to 7 prioritized Tx DMA FIFO channels within the
 * device.  A queue maps to only one (selectable by driver) Tx DMA channel,
 * but one DMA channel may take input from several queues.
 *
 * Tx DMA FIFOs have dedicated purposes.
 *
 * For 5000 series and up, they are used differently
 * (cf. iwl5000_default_queue_to_tx_fifo in iwl-5000.c):
 *
 * 0 -- EDCA BK (background) frames, lowest priority
 * 1 -- EDCA BE (best effort) frames, normal priority
 * 2 -- EDCA VI (video) frames, higher priority
 * 3 -- EDCA VO (voice) and management frames, highest priority
 * 4 -- unused
 * 5 -- unused
 * 6 -- unused
 * 7 -- Commands
 *
 * Driver should normally map queues 0-6 to Tx DMA/FIFO channels 0-6.
 * In addition, driver can map the remaining queues to Tx DMA/FIFO
 * channels 0-3 to support 11n aggregation via EDCA DMA channels.
 *
 * The driver sets up each queue to work in one of two modes:
 *
 * 1)  Scheduler-Ack, in which the scheduler automatically supports a
 *     block-ack (BA) window of up to 64 TFDs.  In this mode, each queue
 *     contains TFDs for a unique combination of Recipient Address (RA)
 *     and Traffic Identifier (TID), that is, traffic of a given
 *     Quality-Of-Service (QOS) priority, destined for a single station.
 *
 *     In scheduler-ack mode, the scheduler keeps track of the Tx status of
 *     each frame within the BA window, including whether it's been transmitted,
 *     and whether it's been acknowledged by the receiving station.  The device
 *     automatically processes block-acks received from the receiving STA,
 *     and reschedules un-acked frames to be retransmitted (successful
 *     Tx completion may end up being out-of-order).
 *
 *     The driver must maintain the queue's Byte Count table in host DRAM
 *     for this mode.
 *     This mode does not support fragmentation.
 *
 * 2)  FIFO (a.k.a. non-Scheduler-ACK), in which each TFD is processed in order.
 *     The device may automatically retry Tx, but will retry only one frame
 *     at a time, until receiving ACK from receiving station, or reaching
 *     retry limit and giving up.
 *
 *     The command queue (#4/#9) must use this mode!
 *     This mode does not require use of the Byte Count table in host DRAM.
 *
 * Driver controls scheduler operation via 3 means:
 * 1)  Scheduler registers
 * 2)  Shared scheduler data base in internal SRAM
 * 3)  Shared data in host DRAM
 *
 * Initialization:
 *
 * When loading, driver should allocate memory for:
 * 1)  16 TFD circular buffers, each with space for (typically) 256 TFDs.
 * 2)  16 Byte Count circular buffers in 16 KBytes contiguous memory
 *     (1024 bytes for each queue).
 *
 * After receiving "Alive" response from uCode, driver must initialize
 * the scheduler (especially for queue #4/#9, the command queue, otherwise
 * the driver can't issue commands!):
 */
#define SCD_MEM_LOWER_BOUND		(0x0000)

/**
 * Max Tx window size is the max number of contiguous TFDs that the scheduler
 * can keep track of at one time when creating block-ack chains of frames.
 * Note that "64" matches the number of ack bits in a block-ack packet.
 */
#define SCD_WIN_SIZE				64
#define SCD_FRAME_LIMIT				64

#define SCD_TXFIFO_POS_TID			(0)
#define SCD_TXFIFO_POS_RA			(4)
#define SCD_QUEUE_RA_TID_MAP_RATID_MSK	(0x01FF)

/* agn SCD */
#define SCD_QUEUE_STTS_REG_POS_TXF	(0)
#define SCD_QUEUE_STTS_REG_POS_ACTIVE	(3)
#define SCD_QUEUE_STTS_REG_POS_WSL	(4)
#define SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN (19)
#define SCD_QUEUE_STTS_REG_MSK		(0x017F0000)

#define SCD_QUEUE_CTX_REG1_CREDIT		(0x00FFFF00)
#define SCD_QUEUE_CTX_REG1_SUPER_CREDIT		(0xFF000000)
#define SCD_QUEUE_CTX_REG1_VAL(_n, _v)		FIELD_PREP(SCD_QUEUE_CTX_REG1_ ## _n, _v)

#define SCD_QUEUE_CTX_REG2_WIN_SIZE		(0x0000007F)
#define SCD_QUEUE_CTX_REG2_FRAME_LIMIT		(0x007F0000)
#define SCD_QUEUE_CTX_REG2_VAL(_n, _v)		FIELD_PREP(SCD_QUEUE_CTX_REG2_ ## _n, _v)

#define SCD_GP_CTRL_ENABLE_31_QUEUES		BIT(0)
#define SCD_GP_CTRL_AUTO_ACTIVE_MODE		BIT(18)

/* Context Data */
#define SCD_CONTEXT_MEM_LOWER_BOUND	(SCD_MEM_LOWER_BOUND + 0x600)
#define SCD_CONTEXT_MEM_UPPER_BOUND	(SCD_MEM_LOWER_BOUND + 0x6A0)

/* Tx status */
#define SCD_TX_STTS_MEM_LOWER_BOUND	(SCD_MEM_LOWER_BOUND + 0x6A0)
#define SCD_TX_STTS_MEM_UPPER_BOUND	(SCD_MEM_LOWER_BOUND + 0x7E0)

/* Translation Data */
#define SCD_TRANS_TBL_MEM_LOWER_BOUND	(SCD_MEM_LOWER_BOUND + 0x7E0)
#define SCD_TRANS_TBL_MEM_UPPER_BOUND	(SCD_MEM_LOWER_BOUND + 0x808)

#define SCD_CONTEXT_QUEUE_OFFSET(x)\
	(SCD_CONTEXT_MEM_LOWER_BOUND + ((x) * 8))

#define SCD_TX_STTS_QUEUE_OFFSET(x)\
	(SCD_TX_STTS_MEM_LOWER_BOUND + ((x) * 16))

#define SCD_TRANS_TBL_OFFSET_QUEUE(x) \
	((SCD_TRANS_TBL_MEM_LOWER_BOUND + ((x) * 2)) & 0xfffc)

#define SCD_BASE			(PRPH_BASE + 0xa02c00)

#define SCD_SRAM_BASE_ADDR	(SCD_BASE + 0x0)
#define SCD_DRAM_BASE_ADDR	(SCD_BASE + 0x8)
#define SCD_AIT			(SCD_BASE + 0x0c)
#define SCD_TXFACT		(SCD_BASE + 0x10)
#define SCD_ACTIVE		(SCD_BASE + 0x14)
#define SCD_QUEUECHAIN_SEL	(SCD_BASE + 0xe8)
#define SCD_CHAINEXT_EN		(SCD_BASE + 0x244)
#define SCD_AGGR_SEL		(SCD_BASE + 0x248)
#define SCD_INTERRUPT_MASK	(SCD_BASE + 0x108)
#define SCD_GP_CTRL		(SCD_BASE + 0x1a8)
#define SCD_EN_CTRL		(SCD_BASE + 0x254)

/*********************** END TX SCHEDULER *************************************/

/* Oscillator clock */
#define OSC_CLK				(0xa04068)
#define OSC_CLK_FORCE_CONTROL		(0x8)

#define FH_UCODE_LOAD_STATUS		(0x1AF0)

/*
 * Replacing FH_UCODE_LOAD_STATUS
 * This register is writen by driver and is read by uCode during boot flow.
 * Note this address is cleared after MAC reset.
 */
#define UREG_UCODE_LOAD_STATUS		(0xa05c40)
#define UREG_CPU_INIT_RUN		(0xa05c44)

#define LMPM_SECURE_UCODE_LOAD_CPU1_HDR_ADDR	(0x1E78)
#define LMPM_SECURE_UCODE_LOAD_CPU2_HDR_ADDR	(0x1E7C)

#define LMPM_SECURE_CPU1_HDR_MEM_SPACE		(0x420000)
#define LMPM_SECURE_CPU2_HDR_MEM_SPACE		(0x420400)

#define LMAC2_PRPH_OFFSET		(0x100000)

/* Rx FIFO */
#define RXF_SIZE_ADDR			(0xa00c88)
#define RXF_RD_D_SPACE			(0xa00c40)
#define RXF_RD_WR_PTR			(0xa00c50)
#define RXF_RD_RD_PTR			(0xa00c54)
#define RXF_RD_FENCE_PTR		(0xa00c4c)
#define RXF_SET_FENCE_MODE		(0xa00c14)
#define RXF_LD_WR2FENCE		(0xa00c1c)
#define RXF_FIFO_RD_FENCE_INC		(0xa00c68)
#define RXF_SIZE_BYTE_CND_POS		(7)
#define RXF_SIZE_BYTE_CNT_MSK		(0x3ff << RXF_SIZE_BYTE_CND_POS)
#define RXF_DIFF_FROM_PREV		(0x200)
#define RXF2C_DIFF_FROM_PREV		(0x4e00)

#define RXF_LD_FENCE_OFFSET_ADDR	(0xa00c10)
#define RXF_FIFO_RD_FENCE_ADDR		(0xa00c0c)

/* Tx FIFO */
#define TXF_FIFO_ITEM_CNT		(0xa00438)
#define TXF_WR_PTR			(0xa00414)
#define TXF_RD_PTR			(0xa00410)
#define TXF_FENCE_PTR			(0xa00418)
#define TXF_LOCK_FENCE			(0xa00424)
#define TXF_LARC_NUM			(0xa0043c)
#define TXF_READ_MODIFY_DATA		(0xa00448)
#define TXF_READ_MODIFY_ADDR		(0xa0044c)

/* UMAC Internal Tx Fifo */
#define TXF_CPU2_FIFO_ITEM_CNT		(0xA00538)
#define TXF_CPU2_WR_PTR		(0xA00514)
#define TXF_CPU2_RD_PTR		(0xA00510)
#define TXF_CPU2_FENCE_PTR		(0xA00518)
#define TXF_CPU2_LOCK_FENCE		(0xA00524)
#define TXF_CPU2_NUM			(0xA0053C)
#define TXF_CPU2_READ_MODIFY_DATA	(0xA00548)
#define TXF_CPU2_READ_MODIFY_ADDR	(0xA0054C)

/* Radio registers access */
#define RSP_RADIO_CMD			(0xa02804)
#define RSP_RADIO_RDDAT			(0xa02814)
#define RADIO_RSP_ADDR_POS		(6)
#define RADIO_RSP_RD_CMD		(3)

/* LTR control (Qu only) */
#define HPM_MAC_LTR_CSR			0xa0348c
#define HPM_MAC_LRT_ENABLE_ALL		0xf
/* also uses CSR_LTR_* for values */
#define HPM_UMAC_LTR			0xa03480

/* FW monitor */
#define MON_BUFF_SAMPLE_CTL		(0xa03c00)
#define MON_BUFF_BASE_ADDR		(0xa03c1c)
#define MON_BUFF_END_ADDR		(0xa03c40)
#define MON_BUFF_WRPTR			(0xa03c44)
#define MON_BUFF_CYCLE_CNT		(0xa03c48)
/* FW monitor family 8000 and on */
#define MON_BUFF_BASE_ADDR_VER2		(0xa03c1c)
#define MON_BUFF_END_ADDR_VER2		(0xa03c20)
#define MON_BUFF_WRPTR_VER2		(0xa03c24)
#define MON_BUFF_CYCLE_CNT_VER2		(0xa03c28)
#define MON_BUFF_SHIFT_VER2		(0x8)
/* FW monitor familiy AX210 and on */
#define DBGC_CUR_DBGBUF_BASE_ADDR_LSB		(0xd03c20)
#define DBGC_CUR_DBGBUF_BASE_ADDR_MSB		(0xd03c24)
#define DBGC_CUR_DBGBUF_STATUS			(0xd03c1c)
#define DBGC_DBGBUF_WRAP_AROUND			(0xd03c2c)
#define DBGC_CUR_DBGBUF_STATUS_OFFSET_MSK	(0x00ffffff)
#define DBGC_CUR_DBGBUF_STATUS_IDX_MSK		(0x0f000000)

#define MON_DMARB_RD_CTL_ADDR		(0xa03c60)
#define MON_DMARB_RD_DATA_ADDR		(0xa03c5c)

#define DBGC_IN_SAMPLE			(0xa03c00)
#define DBGC_OUT_CTRL			(0xa03c0c)

/* M2S registers */
#define LDBG_M2S_BUF_WPTR			(0xa0476c)
#define LDBG_M2S_BUF_WRAP_CNT			(0xa04774)
#define LDBG_M2S_BUF_WPTR_VAL_MSK		(0x000fffff)
#define LDBG_M2S_BUF_WRAP_CNT_VAL_MSK		(0x000fffff)

/* enable the ID buf for read */
#define WFPM_PS_CTL_CLR			0xA0300C
#define WFMP_MAC_ADDR_0			0xA03080
#define WFMP_MAC_ADDR_1			0xA03084
#define LMPM_PMG_EN			0xA01CEC
#define RADIO_REG_SYS_MANUAL_DFT_0	0xAD4078
#define RFIC_REG_RD			0xAD0470
#define WFPM_CTRL_REG			0xA03030
#define WFPM_OTP_CFG1_ADDR		0x00a03098
#define WFPM_OTP_CFG1_IS_JACKET_BIT	BIT(4)
#define WFPM_OTP_CFG1_IS_CDB_BIT	BIT(5)

#define WFPM_GP2			0xA030B4

/* DBGI SRAM Register details */
#define DBGI_SRAM_TARGET_ACCESS_RDATA_LSB		0x00A2E154
#define DBGI_SRAM_TARGET_ACCESS_RDATA_MSB		0x00A2E158
#define DBGI_SRAM_FIFO_POINTERS				0x00A2E148
#define DBGI_SRAM_FIFO_POINTERS_WR_PTR_MSK		0x00000FFF

enum {
	ENABLE_WFPM = BIT(31),
	WFPM_AUX_CTL_AUX_IF_MAC_OWNER_MSK	= 0x80000000,
};

#define CNVI_AUX_MISC_CHIP				0xA200B0
#define CNVR_AUX_MISC_CHIP				0xA2B800
#define CNVR_SCU_SD_REGS_SD_REG_DIG_DCDC_VTRIM		0xA29890
#define CNVR_SCU_SD_REGS_SD_REG_ACTIVE_VDIG_MIRROR	0xA29938

#define PREG_AUX_BUS_WPROT_0		0xA04CC0

/* device family 9000 WPROT register */
#define PREG_PRPH_WPROT_9000		0xA04CE0
/* device family 22000 WPROT register */
#define PREG_PRPH_WPROT_22000		0xA04D00

#define SB_MODIFY_CFG_FLAG		0xA03088
#define SB_CPU_1_STATUS			0xA01E30
#define SB_CPU_2_STATUS			0xA01E34
#define UMAG_SB_CPU_1_STATUS		0xA038C0
#define UMAG_SB_CPU_2_STATUS		0xA038C4
#define UMAG_GEN_HW_STATUS		0xA038C8
#define UREG_UMAC_CURRENT_PC		0xa05c18
#define UREG_LMAC1_CURRENT_PC		0xa05c1c
#define UREG_LMAC2_CURRENT_PC		0xa05c20

#define WFPM_LMAC1_PD_NOTIFICATION      0xa0338c
#define WFPM_ARC1_PD_NOTIFICATION       0xa03044

/* For UMAG_GEN_HW_STATUS reg check */
enum {
	UMAG_GEN_HW_IS_FPGA = BIT(1),
};

/* FW chicken bits */
#define LMPM_CHICK			0xA01FF8
enum {
	LMPM_CHICK_EXTENDED_ADDR_SPACE = BIT(0),
};

/* FW chicken bits */
#define LMPM_PAGE_PASS_NOTIF			0xA03824
enum {
	LMPM_PAGE_PASS_NOTIF_POS = BIT(20),
};

/*
 * CRF ID register
 *
 * type: bits 0-11
 * reserved: bits 12-18
 * slave_exist: bit 19
 * dash: bits 20-23
 * step: bits 24-26
 * flavor: bits 27-31
 */
#define REG_CRF_ID_TYPE(val)		(((val) & 0x00000FFF) >> 0)
#define REG_CRF_ID_SLAVE(val)		(((val) & 0x00080000) >> 19)
#define REG_CRF_ID_DASH(val)		(((val) & 0x00F00000) >> 20)
#define REG_CRF_ID_STEP(val)		(((val) & 0x07000000) >> 24)
#define REG_CRF_ID_FLAVOR(val)		(((val) & 0xF8000000) >> 27)

#define UREG_CHICK		(0xA05C00)
#define UREG_CHICK_MSI_ENABLE	BIT(24)
#define UREG_CHICK_MSIX_ENABLE	BIT(25)

#define SD_REG_VER		0xa29600
#define SD_REG_VER_GEN2		0x00a2b800

#define REG_CRF_ID_TYPE_JF_1			0x201
#define REG_CRF_ID_TYPE_JF_2			0x202
#define REG_CRF_ID_TYPE_HR_CDB			0x503
#define REG_CRF_ID_TYPE_HR_NONE_CDB		0x504
#define REG_CRF_ID_TYPE_HR_NONE_CDB_1X1	0x501
#define REG_CRF_ID_TYPE_HR_NONE_CDB_CCP	0x532
#define REG_CRF_ID_TYPE_GF			0x410
#define REG_CRF_ID_TYPE_GF_TC			0xF08
#define REG_CRF_ID_TYPE_MR			0x810
#define REG_CRF_ID_TYPE_FM			0x910

#define HPM_DEBUG			0xA03440
#define PERSISTENCE_BIT			BIT(12)
#define PREG_WFPM_ACCESS		BIT(12)

#define HPM_HIPM_GEN_CFG			0xA03458
#define HPM_HIPM_GEN_CFG_CR_PG_EN		BIT(0)
#define HPM_HIPM_GEN_CFG_CR_SLP_EN		BIT(1)
#define HPM_HIPM_GEN_CFG_CR_FORCE_ACTIVE	BIT(10)

#define UREG_DOORBELL_TO_ISR6		0xA05C04
#define UREG_DOORBELL_TO_ISR6_NMI_BIT	BIT(0)
#define UREG_DOORBELL_TO_ISR6_RESET_HANDSHAKE (BIT(0) | BIT(1))
#define UREG_DOORBELL_TO_ISR6_SUSPEND	BIT(18)
#define UREG_DOORBELL_TO_ISR6_RESUME	BIT(19)
#define UREG_DOORBELL_TO_ISR6_PNVM	BIT(20)

/*
 * From BZ family driver triggers this bit for suspend and resume
 * The driver should update CSR_IPC_SLEEP_CONTROL before triggering
 * this interrupt with suspend/resume value
 */
#define UREG_DOORBELL_TO_ISR6_SLEEP_CTRL	BIT(31)

#define CNVI_MBOX_C			0xA3400C

#define FSEQ_ERROR_CODE			0xA340C8
#define FSEQ_TOP_INIT_VERSION		0xA34038
#define FSEQ_CNVIO_INIT_VERSION		0xA3403C
#define FSEQ_OTP_VERSION		0xA340FC
#define FSEQ_TOP_CONTENT_VERSION	0xA340F4
#define FSEQ_ALIVE_TOKEN		0xA340F0
#define FSEQ_CNVI_ID			0xA3408C
#define FSEQ_CNVR_ID			0xA34090

#define IWL_D3_SLEEP_STATUS_SUSPEND	0xD3
#define IWL_D3_SLEEP_STATUS_RESUME	0xD0

#define WMAL_INDRCT_RD_CMD1_OPMOD_POS 28
#define WMAL_INDRCT_RD_CMD1_BYTE_ADDRESS_MSK 0xFFFFF
#define WMAL_CMD_READ_BURST_ACCESS 2
#define WMAL_MRSPF_1 0xADFC20
#define WMAL_INDRCT_RD_CMD1 0xADFD44
#define WMAL_INDRCT_CMD1 0xADFC14
#define WMAL_INDRCT_CMD(addr) \
	((WMAL_CMD_READ_BURST_ACCESS << WMAL_INDRCT_RD_CMD1_OPMOD_POS) | \
	 ((addr) & WMAL_INDRCT_RD_CMD1_BYTE_ADDRESS_MSK))

#define WFPM_LMAC1_PS_CTL_RW 0xA03380
#define WFPM_LMAC2_PS_CTL_RW 0xA033C0
#define WFPM_PS_CTL_RW_PHYRF_PD_FSM_CURSTATE_MSK 0x0000000F
#define WFPM_PHYRF_STATE_ON 5
#define HBUS_TIMEOUT 0xA5A5A5A1
#define WFPM_DPHY_OFF 0xDF10FF

#define REG_OTP_MINOR 0xA0333C

#endif				/* __iwl_prph_h__ */
