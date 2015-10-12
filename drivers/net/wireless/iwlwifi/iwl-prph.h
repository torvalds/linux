/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#ifndef	__iwl_prph_h__
#define __iwl_prph_h__

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

/* Device NMI register */
#define DEVICE_SET_NMI_REG 0x00a01c30
#define DEVICE_SET_NMI_VAL_HW BIT(0)
#define DEVICE_SET_NMI_VAL_DRV BIT(7)
#define DEVICE_SET_NMI_8000_REG 0x00a01c24
#define DEVICE_SET_NMI_8000_VAL 0x1000000

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

#define SCD_QUEUE_CTX_REG1_CREDIT_POS		(8)
#define SCD_QUEUE_CTX_REG1_CREDIT_MSK		(0x00FFFF00)
#define SCD_QUEUE_CTX_REG1_SUPER_CREDIT_POS	(24)
#define SCD_QUEUE_CTX_REG1_SUPER_CREDIT_MSK	(0xFF000000)
#define SCD_QUEUE_CTX_REG2_WIN_SIZE_POS		(0)
#define SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK		(0x0000007F)
#define SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS	(16)
#define SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK	(0x007F0000)
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

/* tcp checksum offload */
#define RX_EN_CSUM		(0x00a00d88)

/* Oscillator clock */
#define OSC_CLK				(0xa04068)
#define OSC_CLK_FORCE_CONTROL		(0x8)

#define FH_UCODE_LOAD_STATUS		(0x1AF0)
#define CSR_UCODE_LOAD_STATUS_ADDR	(0x1E70)
enum secure_load_status_reg {
	LMPM_CPU_UCODE_LOADING_STARTED			= 0x00000001,
	LMPM_CPU_HDRS_LOADING_COMPLETED			= 0x00000003,
	LMPM_CPU_UCODE_LOADING_COMPLETED		= 0x00000007,
	LMPM_CPU_STATUS_NUM_OF_LAST_COMPLETED		= 0x000000F8,
	LMPM_CPU_STATUS_NUM_OF_LAST_LOADED_BLOCK	= 0x0000FF00,
};

#define LMPM_SECURE_INSPECTOR_CODE_ADDR	(0x1E38)
#define LMPM_SECURE_INSPECTOR_DATA_ADDR	(0x1E3C)
#define LMPM_SECURE_UCODE_LOAD_CPU1_HDR_ADDR	(0x1E78)
#define LMPM_SECURE_UCODE_LOAD_CPU2_HDR_ADDR	(0x1E7C)

#define LMPM_SECURE_INSPECTOR_CODE_MEM_SPACE	(0x400000)
#define LMPM_SECURE_INSPECTOR_DATA_MEM_SPACE	(0x402000)
#define LMPM_SECURE_CPU1_HDR_MEM_SPACE		(0x420000)
#define LMPM_SECURE_CPU2_HDR_MEM_SPACE		(0x420400)

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

/* FW monitor */
#define MON_BUFF_SAMPLE_CTL		(0xa03c00)
#define MON_BUFF_BASE_ADDR		(0xa03c3c)
#define MON_BUFF_END_ADDR		(0xa03c40)
#define MON_BUFF_WRPTR			(0xa03c44)
#define MON_BUFF_CYCLE_CNT		(0xa03c48)

#define MON_DMARB_RD_CTL_ADDR		(0xa03c60)
#define MON_DMARB_RD_DATA_ADDR		(0xa03c5c)

#define DBGC_IN_SAMPLE			(0xa03c00)

/* enable the ID buf for read */
#define WFPM_PS_CTL_CLR			0xA0300C
#define WFMP_MAC_ADDR_0			0xA03080
#define WFMP_MAC_ADDR_1			0xA03084
#define LMPM_PMG_EN			0xA01CEC
#define RADIO_REG_SYS_MANUAL_DFT_0	0xAD4078
#define RFIC_REG_RD			0xAD0470
#define WFPM_CTRL_REG			0xA03030
enum {
	ENABLE_WFPM = BIT(31),
	WFPM_AUX_CTL_AUX_IF_MAC_OWNER_MSK	= 0x80000000,
};

#define AUX_MISC_REG			0xA200B0
enum {
	HW_STEP_LOCATION_BITS = 24,
};

#define AUX_MISC_MASTER1_EN		0xA20818
enum aux_misc_master1_en {
	AUX_MISC_MASTER1_EN_SBE_MSK	= 0x1,
};

#define AUX_MISC_MASTER1_SMPHR_STATUS	0xA20800
#define RSA_ENABLE			0xA24B08
#define PREG_AUX_BUS_WPROT_0		0xA04CC0
#define SB_CPU_1_STATUS			0xA01E30
#define SB_CPU_2_STATUS			0xA01E34

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

#endif				/* __iwl_prph_h__ */
