/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU Geeral Public License as
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
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2007 Intel Corporation. All rights reserved.
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
 *
 *****************************************************************************/

#ifndef __iwl_4965_hw_h__
#define __iwl_4965_hw_h__

#define IWL_RX_BUF_SIZE (4 * 1024)
#define IWL_MAX_BSM_SIZE BSM_SRAM_SIZE
#define KDR_RTC_INST_UPPER_BOUND		(0x018000)
#define KDR_RTC_DATA_UPPER_BOUND		(0x80A000)
#define KDR_RTC_INST_SIZE    (KDR_RTC_INST_UPPER_BOUND - RTC_INST_LOWER_BOUND)
#define KDR_RTC_DATA_SIZE    (KDR_RTC_DATA_UPPER_BOUND - RTC_DATA_LOWER_BOUND)

#define IWL_MAX_INST_SIZE KDR_RTC_INST_SIZE
#define IWL_MAX_DATA_SIZE KDR_RTC_DATA_SIZE

static inline int iwl_hw_valid_rtc_data_addr(u32 addr)
{
	return (addr >= RTC_DATA_LOWER_BOUND) &&
	       (addr < KDR_RTC_DATA_UPPER_BOUND);
}

/********************* START TXPOWER *****************************************/
enum {
	HT_IE_EXT_CHANNEL_NONE = 0,
	HT_IE_EXT_CHANNEL_ABOVE,
	HT_IE_EXT_CHANNEL_INVALID,
	HT_IE_EXT_CHANNEL_BELOW,
	HT_IE_EXT_CHANNEL_MAX
};

enum {
	CALIB_CH_GROUP_1 = 0,
	CALIB_CH_GROUP_2 = 1,
	CALIB_CH_GROUP_3 = 2,
	CALIB_CH_GROUP_4 = 3,
	CALIB_CH_GROUP_5 = 4,
	CALIB_CH_GROUP_MAX
};

/* Temperature calibration offset is 3% 0C in Kelvin */
#define TEMPERATURE_CALIB_KELVIN_OFFSET 8
#define TEMPERATURE_CALIB_A_VAL 259

#define IWL_TX_POWER_TEMPERATURE_MIN  (263)
#define IWL_TX_POWER_TEMPERATURE_MAX  (410)

#define IWL_TX_POWER_TEMPERATURE_OUT_OF_RANGE(t) \
	(((t) < IWL_TX_POWER_TEMPERATURE_MIN) || \
	 ((t) > IWL_TX_POWER_TEMPERATURE_MAX))

#define IWL_TX_POWER_ILLEGAL_TEMPERATURE (300)

#define IWL_TX_POWER_TEMPERATURE_DIFFERENCE (2)

#define IWL_TX_POWER_MIMO_REGULATORY_COMPENSATION (6)

#define IWL_TX_POWER_TARGET_POWER_MIN       (0)	/* 0 dBm = 1 milliwatt */
#define IWL_TX_POWER_TARGET_POWER_MAX      (16)	/* 16 dBm */

/* timeout equivalent to 3 minutes */
#define IWL_TX_POWER_TIMELIMIT_NOCALIB 1800000000

#define IWL_TX_POWER_CCK_COMPENSATION (9)

#define MIN_TX_GAIN_INDEX		(0)
#define MIN_TX_GAIN_INDEX_52GHZ_EXT	(-9)
#define MAX_TX_GAIN_INDEX_52GHZ		(98)
#define MIN_TX_GAIN_52GHZ		(98)
#define MAX_TX_GAIN_INDEX_24GHZ		(98)
#define MIN_TX_GAIN_24GHZ		(98)
#define MAX_TX_GAIN			(0)
#define MAX_TX_GAIN_52GHZ_EXT		(-9)

#define IWL_TX_POWER_DEFAULT_REGULATORY_24   (34)
#define IWL_TX_POWER_DEFAULT_REGULATORY_52   (34)
#define IWL_TX_POWER_REGULATORY_MIN          (0)
#define IWL_TX_POWER_REGULATORY_MAX          (34)
#define IWL_TX_POWER_DEFAULT_SATURATION_24   (38)
#define IWL_TX_POWER_DEFAULT_SATURATION_52   (38)
#define IWL_TX_POWER_SATURATION_MIN          (20)
#define IWL_TX_POWER_SATURATION_MAX          (50)

/* dv *0.4 = dt; so that 5 degrees temperature diff equals
 * 12.5 in voltage diff */
#define IWL_TX_TEMPERATURE_UPDATE_LIMIT 9

#define IWL_INVALID_CHANNEL                 (0xffffffff)
#define IWL_TX_POWER_REGITRY_BIT            (2)

#define MIN_IWL_TX_POWER_CALIB_DUR          (100)
#define IWL_CCK_FROM_OFDM_POWER_DIFF        (-5)
#define IWL_CCK_FROM_OFDM_INDEX_DIFF (9)

/* Number of entries in the gain table */
#define POWER_GAIN_NUM_ENTRIES 78
#define TX_POW_MAX_SESSION_NUM 5
/*  timeout equivalent to 3 minutes */
#define TX_IWL_TIMELIMIT_NOCALIB 1800000000

/* Kedron TX_CALIB_STATES */
#define IWL_TX_CALIB_STATE_SEND_TX        0x00000001
#define IWL_TX_CALIB_WAIT_TX_RESPONSE     0x00000002
#define IWL_TX_CALIB_ENABLED              0x00000004
#define IWL_TX_CALIB_XVT_ON               0x00000008
#define IWL_TX_CALIB_TEMPERATURE_CORRECT  0x00000010
#define IWL_TX_CALIB_WORKING_WITH_XVT     0x00000020
#define IWL_TX_CALIB_XVT_PERIODICAL       0x00000040

#define NUM_IWL_TX_CALIB_SETTINS 5	/* Number of tx correction groups */

#define IWL_MIN_POWER_IN_VP_TABLE 1	/* 0.5dBm multiplied by 2 */
#define IWL_MAX_POWER_IN_VP_TABLE 40	/* 20dBm - multiplied by 2 (because
					 * entries are for each 0.5dBm) */
#define IWL_STEP_IN_VP_TABLE 1	/* 0.5dB - multiplied by 2 */
#define IWL_NUM_POINTS_IN_VPTABLE \
	(1 + IWL_MAX_POWER_IN_VP_TABLE - IWL_MIN_POWER_IN_VP_TABLE)

#define MIN_TX_GAIN_INDEX         (0)
#define MAX_TX_GAIN_INDEX_52GHZ   (98)
#define MIN_TX_GAIN_52GHZ         (98)
#define MAX_TX_GAIN_INDEX_24GHZ   (98)
#define MIN_TX_GAIN_24GHZ         (98)
#define MAX_TX_GAIN               (0)

/* First and last channels of all groups */
#define CALIB_IWL_TX_ATTEN_GR1_FCH 34
#define CALIB_IWL_TX_ATTEN_GR1_LCH 43
#define CALIB_IWL_TX_ATTEN_GR2_FCH 44
#define CALIB_IWL_TX_ATTEN_GR2_LCH 70
#define CALIB_IWL_TX_ATTEN_GR3_FCH 71
#define CALIB_IWL_TX_ATTEN_GR3_LCH 124
#define CALIB_IWL_TX_ATTEN_GR4_FCH 125
#define CALIB_IWL_TX_ATTEN_GR4_LCH 200
#define CALIB_IWL_TX_ATTEN_GR5_FCH 1
#define CALIB_IWL_TX_ATTEN_GR5_LCH 20


union iwl_tx_power_dual_stream {
	struct {
		u8 radio_tx_gain[2];
		u8 dsp_predis_atten[2];
	} s;
	u32 dw;
};

/********************* END TXPOWER *****************************************/

/* HT flags */
#define RXON_FLG_CTRL_CHANNEL_LOC_POS		(22)
#define RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK	__constant_cpu_to_le32(0x1<<22)

#define RXON_FLG_HT_OPERATING_MODE_POS		(23)

#define RXON_FLG_HT_PROT_MSK			__constant_cpu_to_le32(0x1<<23)
#define RXON_FLG_FAT_PROT_MSK			__constant_cpu_to_le32(0x2<<23)

#define RXON_FLG_CHANNEL_MODE_POS		(25)
#define RXON_FLG_CHANNEL_MODE_MSK		__constant_cpu_to_le32(0x3<<25)
#define RXON_FLG_CHANNEL_MODE_PURE_40_MSK	__constant_cpu_to_le32(0x1<<25)
#define RXON_FLG_CHANNEL_MODE_MIXED_MSK		__constant_cpu_to_le32(0x2<<25)

#define RXON_RX_CHAIN_DRIVER_FORCE_MSK		__constant_cpu_to_le16(0x1<<0)
#define RXON_RX_CHAIN_VALID_MSK			__constant_cpu_to_le16(0x7<<1)
#define RXON_RX_CHAIN_VALID_POS			(1)
#define RXON_RX_CHAIN_FORCE_SEL_MSK		__constant_cpu_to_le16(0x7<<4)
#define RXON_RX_CHAIN_FORCE_SEL_POS		(4)
#define RXON_RX_CHAIN_FORCE_MIMO_SEL_MSK	__constant_cpu_to_le16(0x7<<7)
#define RXON_RX_CHAIN_FORCE_MIMO_SEL_POS	(7)
#define RXON_RX_CHAIN_CNT_MSK			__constant_cpu_to_le16(0x3<<10)
#define RXON_RX_CHAIN_CNT_POS			(10)
#define RXON_RX_CHAIN_MIMO_CNT_MSK		__constant_cpu_to_le16(0x3<<12)
#define RXON_RX_CHAIN_MIMO_CNT_POS		(12)
#define RXON_RX_CHAIN_MIMO_FORCE_MSK		__constant_cpu_to_le16(0x1<<14)
#define RXON_RX_CHAIN_MIMO_FORCE_POS		(14)


#define MCS_DUP_6M_PLCP 0x20

/* OFDM HT rate masks */
/* ***************************************** */
#define R_MCS_6M_MSK 0x1
#define R_MCS_12M_MSK 0x2
#define R_MCS_18M_MSK 0x4
#define R_MCS_24M_MSK 0x8
#define R_MCS_36M_MSK 0x10
#define R_MCS_48M_MSK 0x20
#define R_MCS_54M_MSK 0x40
#define R_MCS_60M_MSK 0x80
#define R_MCS_12M_DUAL_MSK 0x100
#define R_MCS_24M_DUAL_MSK 0x200
#define R_MCS_36M_DUAL_MSK 0x400
#define R_MCS_48M_DUAL_MSK 0x800

#define is_legacy(tbl) (((tbl) == LQ_G) || ((tbl) == LQ_A))
#define is_siso(tbl) (((tbl) == LQ_SISO))
#define is_mimo(tbl) (((tbl) == LQ_MIMO))
#define is_Ht(tbl) (is_siso(tbl) || is_mimo(tbl))
#define is_a_band(tbl) (((tbl) == LQ_A))
#define is_g_and(tbl) (((tbl) == LQ_G))

/* Flow Handler Definitions */

/**********************/
/*     Addresses      */
/**********************/

#define FH_MEM_LOWER_BOUND                   (0x1000)
#define FH_MEM_UPPER_BOUND                   (0x1EF0)

#define IWL_FH_REGS_LOWER_BOUND		     (0x1000)
#define IWL_FH_REGS_UPPER_BOUND		     (0x2000)

#define IWL_FH_KW_MEM_ADDR_REG		     (FH_MEM_LOWER_BOUND + 0x97C)

/* CBBC Area - Circular buffers base address cache pointers table */
#define FH_MEM_CBBC_LOWER_BOUND              (FH_MEM_LOWER_BOUND + 0x9D0)
#define FH_MEM_CBBC_UPPER_BOUND              (FH_MEM_LOWER_BOUND + 0xA10)
/* queues 0 - 15 */
#define FH_MEM_CBBC_QUEUE(x)  (FH_MEM_CBBC_LOWER_BOUND + (x) * 0x4)

/* RSCSR Area */
#define FH_MEM_RSCSR_LOWER_BOUND	(FH_MEM_LOWER_BOUND + 0xBC0)
#define FH_MEM_RSCSR_UPPER_BOUND	(FH_MEM_LOWER_BOUND + 0xC00)
#define FH_MEM_RSCSR_CHNL0		(FH_MEM_RSCSR_LOWER_BOUND)

#define FH_RSCSR_CHNL0_STTS_WPTR_REG		(FH_MEM_RSCSR_CHNL0)
#define FH_RSCSR_CHNL0_RBDCB_BASE_REG		(FH_MEM_RSCSR_CHNL0 + 0x004)
#define FH_RSCSR_CHNL0_RBDCB_WPTR_REG		(FH_MEM_RSCSR_CHNL0 + 0x008)

/* RCSR Area - Registers address map */
#define FH_MEM_RCSR_LOWER_BOUND      (FH_MEM_LOWER_BOUND + 0xC00)
#define FH_MEM_RCSR_UPPER_BOUND      (FH_MEM_LOWER_BOUND + 0xCC0)
#define FH_MEM_RCSR_CHNL0            (FH_MEM_RCSR_LOWER_BOUND)

#define FH_MEM_RCSR_CHNL0_CONFIG_REG	(FH_MEM_RCSR_CHNL0)

/* RSSR Area - Rx shared ctrl & status registers */
#define FH_MEM_RSSR_LOWER_BOUND                	(FH_MEM_LOWER_BOUND + 0xC40)
#define FH_MEM_RSSR_UPPER_BOUND               	(FH_MEM_LOWER_BOUND + 0xD00)
#define FH_MEM_RSSR_SHARED_CTRL_REG           	(FH_MEM_RSSR_LOWER_BOUND)
#define FH_MEM_RSSR_RX_STATUS_REG	(FH_MEM_RSSR_LOWER_BOUND + 0x004)
#define FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV  (FH_MEM_RSSR_LOWER_BOUND + 0x008)

/* TCSR */
#define IWL_FH_TCSR_LOWER_BOUND  (IWL_FH_REGS_LOWER_BOUND + 0xD00)
#define IWL_FH_TCSR_UPPER_BOUND  (IWL_FH_REGS_LOWER_BOUND + 0xE60)

#define IWL_FH_TCSR_CHNL_NUM                            (7)
#define IWL_FH_TCSR_CHNL_TX_CONFIG_REG(_chnl) \
	(IWL_FH_TCSR_LOWER_BOUND + 0x20 * _chnl)

/* TSSR Area - Tx shared status registers */
/* TSSR */
#define IWL_FH_TSSR_LOWER_BOUND		(IWL_FH_REGS_LOWER_BOUND + 0xEA0)
#define IWL_FH_TSSR_UPPER_BOUND		(IWL_FH_REGS_LOWER_BOUND + 0xEC0)

#define IWL_FH_TSSR_TX_MSG_CONFIG_REG	(IWL_FH_TSSR_LOWER_BOUND + 0x008)
#define IWL_FH_TSSR_TX_STATUS_REG	(IWL_FH_TSSR_LOWER_BOUND + 0x010)

#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_SNOOP_RD_TXPD_ON	(0xFF000000)
#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RD_TXPD_ON	(0x00FF0000)

#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_MAX_FRAG_SIZE_64B	(0x00000000)
#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_MAX_FRAG_SIZE_128B	(0x00000400)
#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_MAX_FRAG_SIZE_256B	(0x00000800)
#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_MAX_FRAG_SIZE_512B	(0x00000C00)

#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_SNOOP_RD_TFD_ON	(0x00000100)
#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RD_CBB_ON	(0x00000080)

#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RSP_WAIT_TH	(0x00000020)
#define IWL_FH_TSSR_TX_MSG_CONFIG_REG_VAL_RSP_WAIT_TH		(0x00000005)

#define IWL_FH_TSSR_TX_STATUS_REG_BIT_BUFS_EMPTY(_chnl)	\
	((1 << (_chnl)) << 24)
#define IWL_FH_TSSR_TX_STATUS_REG_BIT_NO_PEND_REQ(_chnl) \
	((1 << (_chnl)) << 16)

#define IWL_FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(_chnl) \
	(IWL_FH_TSSR_TX_STATUS_REG_BIT_BUFS_EMPTY(_chnl) | \
	IWL_FH_TSSR_TX_STATUS_REG_BIT_NO_PEND_REQ(_chnl))

/* TCSR: tx_config register values */
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_TXF              (0x00000000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_DRIVER           (0x00000001)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_ARC              (0x00000002)

#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE_VAL    (0x00000000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL     (0x00000008)

#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_NOINT           (0x00000000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD          (0x00100000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_IFTFD           (0x00200000)

#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_NOINT            (0x00000000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_ENDTFD           (0x00400000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_IFTFD            (0x00800000)

#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE            (0x00000000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE_EOF        (0x40000000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE           (0x80000000)

#define IWL_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_EMPTY          (0x00000000)
#define IWL_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_WAIT           (0x00002000)
#define IWL_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID          (0x00000003)

#define IWL_FH_TCSR_CHNL_TX_BUF_STS_REG_BIT_TFDB_WPTR           (0x00000001)

#define IWL_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM              (20)
#define IWL_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX              (12)

/* RCSR:  channel 0 rx_config register defines */
#define FH_RCSR_CHNL0_RX_CONFIG_DMA_CHNL_EN_MASK  (0xC0000000) /* bits 30-31 */
#define FH_RCSR_CHNL0_RX_CONFIG_RBDBC_SIZE_MASK   (0x00F00000) /* bits 20-23 */
#define FH_RCSR_CHNL0_RX_CONFIG_RB_SIZE_MASK	  (0x00030000) /* bits 16-17 */
#define FH_RCSR_CHNL0_RX_CONFIG_SINGLE_FRAME_MASK (0x00008000) /* bit 15 */
#define FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_MASK     (0x00001000) /* bit 12 */
#define FH_RCSR_CHNL0_RX_CONFIG_RB_TIMEOUT_MASK   (0x00000FF0) /* bit 4-11 */

#define FH_RCSR_RX_CONFIG_RBDCB_SIZE_BITSHIFT       (20)
#define FH_RCSR_RX_CONFIG_RB_SIZE_BITSHIFT			(16)

/* RCSR: rx_config register values */
#define FH_RCSR_RX_CONFIG_CHNL_EN_PAUSE_VAL         (0x00000000)
#define FH_RCSR_RX_CONFIG_CHNL_EN_PAUSE_EOF_VAL     (0x40000000)
#define FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL        (0x80000000)

#define IWL_FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K    (0x00000000)

/* RCSR channel 0 config register values */
#define FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_NO_INT_VAL       (0x00000000)
#define FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL     (0x00001000)

/* RSCSR: defs used in normal mode */
#define FH_RSCSR_CHNL0_RBDCB_WPTR_MASK		(0x00000FFF)	/* bits 0-11 */

#define SCD_WIN_SIZE				64
#define SCD_FRAME_LIMIT				64

/* memory mapped registers */
#define SCD_START_OFFSET		0xa02c00

#define SCD_SRAM_BASE_ADDR           (SCD_START_OFFSET + 0x0)
#define SCD_EMPTY_BITS               (SCD_START_OFFSET + 0x4)
#define SCD_DRAM_BASE_ADDR           (SCD_START_OFFSET + 0x10)
#define SCD_AIT                      (SCD_START_OFFSET + 0x18)
#define SCD_TXFACT                   (SCD_START_OFFSET + 0x1c)
#define SCD_QUEUE_WRPTR(x)           (SCD_START_OFFSET + 0x24 + (x) * 4)
#define SCD_QUEUE_RDPTR(x)           (SCD_START_OFFSET + 0x64 + (x) * 4)
#define SCD_SETQUEUENUM              (SCD_START_OFFSET + 0xa4)
#define SCD_SET_TXSTAT_TXED          (SCD_START_OFFSET + 0xa8)
#define SCD_SET_TXSTAT_DONE          (SCD_START_OFFSET + 0xac)
#define SCD_SET_TXSTAT_NOT_SCHD      (SCD_START_OFFSET + 0xb0)
#define SCD_DECREASE_CREDIT          (SCD_START_OFFSET + 0xb4)
#define SCD_DECREASE_SCREDIT         (SCD_START_OFFSET + 0xb8)
#define SCD_LOAD_CREDIT              (SCD_START_OFFSET + 0xbc)
#define SCD_LOAD_SCREDIT             (SCD_START_OFFSET + 0xc0)
#define SCD_BAR                      (SCD_START_OFFSET + 0xc4)
#define SCD_BAR_DW0                  (SCD_START_OFFSET + 0xc8)
#define SCD_BAR_DW1                  (SCD_START_OFFSET + 0xcc)
#define SCD_QUEUECHAIN_SEL           (SCD_START_OFFSET + 0xd0)
#define SCD_QUERY_REQ                (SCD_START_OFFSET + 0xd8)
#define SCD_QUERY_RES                (SCD_START_OFFSET + 0xdc)
#define SCD_PENDING_FRAMES           (SCD_START_OFFSET + 0xe0)
#define SCD_INTERRUPT_MASK           (SCD_START_OFFSET + 0xe4)
#define SCD_INTERRUPT_THRESHOLD      (SCD_START_OFFSET + 0xe8)
#define SCD_QUERY_MIN_FRAME_SIZE     (SCD_START_OFFSET + 0x100)
#define SCD_QUEUE_STATUS_BITS(x)     (SCD_START_OFFSET + 0x104 + (x) * 4)

/* SRAM structures */
#define SCD_CONTEXT_DATA_OFFSET			0x380
#define SCD_TX_STTS_BITMAP_OFFSET		0x400
#define SCD_TRANSLATE_TBL_OFFSET		0x500
#define SCD_CONTEXT_QUEUE_OFFSET(x)	(SCD_CONTEXT_DATA_OFFSET + ((x) * 8))
#define SCD_TRANSLATE_TBL_OFFSET_QUEUE(x) \
	((SCD_TRANSLATE_TBL_OFFSET + ((x) * 2)) & 0xfffffffc)

#define SCD_TXFACT_REG_TXFIFO_MASK(lo, hi) \
       ((1<<(hi))|((1<<(hi))-(1<<(lo))))


#define SCD_MODE_REG_BIT_SEARCH_MODE		(1<<0)
#define SCD_MODE_REG_BIT_SBYP_MODE		(1<<1)

#define SCD_TXFIFO_POS_TID			(0)
#define SCD_TXFIFO_POS_RA			(4)
#define SCD_QUEUE_STTS_REG_POS_ACTIVE		(0)
#define SCD_QUEUE_STTS_REG_POS_TXF		(1)
#define SCD_QUEUE_STTS_REG_POS_WSL		(5)
#define SCD_QUEUE_STTS_REG_POS_SCD_ACK		(8)
#define SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN	(10)
#define SCD_QUEUE_STTS_REG_MSK			(0x0007FC00)

#define SCD_QUEUE_RA_TID_MAP_RATID_MSK		(0x01FF)

#define SCD_QUEUE_CTX_REG1_WIN_SIZE_POS		(0)
#define SCD_QUEUE_CTX_REG1_WIN_SIZE_MSK		(0x0000007F)
#define SCD_QUEUE_CTX_REG1_CREDIT_POS		(8)
#define SCD_QUEUE_CTX_REG1_CREDIT_MSK		(0x00FFFF00)
#define SCD_QUEUE_CTX_REG1_SUPER_CREDIT_POS	(24)
#define SCD_QUEUE_CTX_REG1_SUPER_CREDIT_MSK	(0xFF000000)
#define SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS	(16)
#define SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK	(0x007F0000)

#define CSR_HW_IF_CONFIG_REG_BIT_KEDRON_R	(0x00000010)
#define CSR_HW_IF_CONFIG_REG_MSK_BOARD_VER	(0x00000C00)
#define CSR_HW_IF_CONFIG_REG_BIT_MAC_SI		(0x00000100)
#define CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI	(0x00000200)

static inline u8 iwl_hw_get_rate(__le32 rate_n_flags)
{
	return le32_to_cpu(rate_n_flags) & 0xFF;
}
static inline u16 iwl_hw_get_rate_n_flags(__le32 rate_n_flags)
{
	return le32_to_cpu(rate_n_flags) & 0xFFFF;
}
static inline __le32 iwl_hw_set_rate_n_flags(u8 rate, u16 flags)
{
	return cpu_to_le32(flags|(u16)rate);
}

struct iwl_tfd_frame_data {
	__le32 tb1_addr;

	__le32 val1;
	/* __le32 ptb1_32_35:4; */
#define IWL_tb1_addr_hi_POS 0
#define IWL_tb1_addr_hi_LEN 4
#define IWL_tb1_addr_hi_SYM val1
	/* __le32 tb_len1:12; */
#define IWL_tb1_len_POS 4
#define IWL_tb1_len_LEN 12
#define IWL_tb1_len_SYM val1
	/* __le32 ptb2_0_15:16; */
#define IWL_tb2_addr_lo16_POS 16
#define IWL_tb2_addr_lo16_LEN 16
#define IWL_tb2_addr_lo16_SYM val1

	__le32 val2;
	/* __le32 ptb2_16_35:20; */
#define IWL_tb2_addr_hi20_POS 0
#define IWL_tb2_addr_hi20_LEN 20
#define IWL_tb2_addr_hi20_SYM val2
	/* __le32 tb_len2:12; */
#define IWL_tb2_len_POS 20
#define IWL_tb2_len_LEN 12
#define IWL_tb2_len_SYM val2
} __attribute__ ((packed));

struct iwl_tfd_frame {
	__le32 val0;
	/* __le32 rsvd1:24; */
	/* __le32 num_tbs:5; */
#define IWL_num_tbs_POS 24
#define IWL_num_tbs_LEN 5
#define IWL_num_tbs_SYM val0
	/* __le32 rsvd2:1; */
	/* __le32 padding:2; */
	struct iwl_tfd_frame_data pa[10];
	__le32 reserved;
} __attribute__ ((packed));

#define IWL4965_MAX_WIN_SIZE              64
#define IWL4965_QUEUE_SIZE               256
#define IWL4965_NUM_FIFOS                  7
#define IWL_MAX_NUM_QUEUES                16

struct iwl4965_queue_byte_cnt_entry {
	__le16 val;
	/* __le16 byte_cnt:12; */
#define IWL_byte_cnt_POS 0
#define IWL_byte_cnt_LEN 12
#define IWL_byte_cnt_SYM val
	/* __le16 rsvd:4; */
} __attribute__ ((packed));

struct iwl4965_sched_queue_byte_cnt_tbl {
	struct iwl4965_queue_byte_cnt_entry tfd_offset[IWL4965_QUEUE_SIZE +
						       IWL4965_MAX_WIN_SIZE];
	u8 dont_care[1024 -
		     (IWL4965_QUEUE_SIZE + IWL4965_MAX_WIN_SIZE) *
		     sizeof(__le16)];
} __attribute__ ((packed));

/* Base physical address of iwl_shared is provided to SCD_DRAM_BASE_ADDR
 * and &iwl_shared.val0 is provided to FH_RSCSR_CHNL0_STTS_WPTR_REG */
struct iwl_shared {
	struct iwl4965_sched_queue_byte_cnt_tbl
	 queues_byte_cnt_tbls[IWL_MAX_NUM_QUEUES];
	__le32 val0;

	/* __le32 rb_closed_stts_rb_num:12; */
#define IWL_rb_closed_stts_rb_num_POS 0
#define IWL_rb_closed_stts_rb_num_LEN 12
#define IWL_rb_closed_stts_rb_num_SYM val0
	/* __le32 rsrv1:4; */
	/* __le32 rb_closed_stts_rx_frame_num:12; */
#define IWL_rb_closed_stts_rx_frame_num_POS 16
#define IWL_rb_closed_stts_rx_frame_num_LEN 12
#define IWL_rb_closed_stts_rx_frame_num_SYM val0
	/* __le32 rsrv2:4; */

	__le32 val1;
	/* __le32 frame_finished_stts_rb_num:12; */
#define IWL_frame_finished_stts_rb_num_POS 0
#define IWL_frame_finished_stts_rb_num_LEN 12
#define IWL_frame_finished_stts_rb_num_SYM val1
	/* __le32 rsrv3:4; */
	/* __le32 frame_finished_stts_rx_frame_num:12; */
#define IWL_frame_finished_stts_rx_frame_num_POS 16
#define IWL_frame_finished_stts_rx_frame_num_LEN 12
#define IWL_frame_finished_stts_rx_frame_num_SYM val1
	/* __le32 rsrv4:4; */

	__le32 padding1;  /* so that allocation will be aligned to 16B */
	__le32 padding2;
} __attribute__ ((packed));

#endif /* __iwl_4965_hw_h__ */
