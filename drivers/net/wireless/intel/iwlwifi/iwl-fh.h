/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2020 Intel Corporation
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
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2020 Intel Corporation
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
#ifndef __iwl_fh_h__
#define __iwl_fh_h__

#include <linux/types.h>
#include <linux/bitfield.h>

#include "iwl-trans.h"

/****************************/
/* Flow Handler Definitions */
/****************************/

/**
 * This I/O area is directly read/writable by driver (e.g. Linux uses writel())
 * Addresses are offsets from device's PCI hardware base address.
 */
#define FH_MEM_LOWER_BOUND                   (0x1000)
#define FH_MEM_UPPER_BOUND                   (0x2000)
#define FH_MEM_LOWER_BOUND_GEN2              (0xa06000)
#define FH_MEM_UPPER_BOUND_GEN2              (0xa08000)

/**
 * Keep-Warm (KW) buffer base address.
 *
 * Driver must allocate a 4KByte buffer that is for keeping the
 * host DRAM powered on (via dummy accesses to DRAM) to maintain low-latency
 * DRAM access when doing Txing or Rxing.  The dummy accesses prevent host
 * from going into a power-savings mode that would cause higher DRAM latency,
 * and possible data over/under-runs, before all Tx/Rx is complete.
 *
 * Driver loads FH_KW_MEM_ADDR_REG with the physical address (bits 35:4)
 * of the buffer, which must be 4K aligned.  Once this is set up, the device
 * automatically invokes keep-warm accesses when normal accesses might not
 * be sufficient to maintain fast DRAM response.
 *
 * Bit fields:
 *  31-0:  Keep-warm buffer physical base address [35:4], must be 4K aligned
 */
#define FH_KW_MEM_ADDR_REG		     (FH_MEM_LOWER_BOUND + 0x97C)


/**
 * TFD Circular Buffers Base (CBBC) addresses
 *
 * Device has 16 base pointer registers, one for each of 16 host-DRAM-resident
 * circular buffers (CBs/queues) containing Transmit Frame Descriptors (TFDs)
 * (see struct iwl_tfd_frame).  These 16 pointer registers are offset by 0x04
 * bytes from one another.  Each TFD circular buffer in DRAM must be 256-byte
 * aligned (address bits 0-7 must be 0).
 * Later devices have 20 (5000 series) or 30 (higher) queues, but the registers
 * for them are in different places.
 *
 * Bit fields in each pointer register:
 *  27-0: TFD CB physical base address [35:8], must be 256-byte aligned
 */
#define FH_MEM_CBBC_0_15_LOWER_BOUND		(FH_MEM_LOWER_BOUND + 0x9D0)
#define FH_MEM_CBBC_0_15_UPPER_BOUND		(FH_MEM_LOWER_BOUND + 0xA10)
#define FH_MEM_CBBC_16_19_LOWER_BOUND		(FH_MEM_LOWER_BOUND + 0xBF0)
#define FH_MEM_CBBC_16_19_UPPER_BOUND		(FH_MEM_LOWER_BOUND + 0xC00)
#define FH_MEM_CBBC_20_31_LOWER_BOUND		(FH_MEM_LOWER_BOUND + 0xB20)
#define FH_MEM_CBBC_20_31_UPPER_BOUND		(FH_MEM_LOWER_BOUND + 0xB80)
/* 22000 TFD table address, 64 bit */
#define TFH_TFDQ_CBB_TABLE			(0x1C00)

/* Find TFD CB base pointer for given queue */
static inline unsigned int FH_MEM_CBBC_QUEUE(struct iwl_trans *trans,
					     unsigned int chnl)
{
	if (trans->trans_cfg->use_tfh) {
		WARN_ON_ONCE(chnl >= 64);
		return TFH_TFDQ_CBB_TABLE + 8 * chnl;
	}
	if (chnl < 16)
		return FH_MEM_CBBC_0_15_LOWER_BOUND + 4 * chnl;
	if (chnl < 20)
		return FH_MEM_CBBC_16_19_LOWER_BOUND + 4 * (chnl - 16);
	WARN_ON_ONCE(chnl >= 32);
	return FH_MEM_CBBC_20_31_LOWER_BOUND + 4 * (chnl - 20);
}

/* 22000 configuration registers */

/*
 * TFH Configuration register.
 *
 * BIT fields:
 *
 * Bits 3:0:
 * Define the maximum number of pending read requests.
 * Maximum configuration value allowed is 0xC
 * Bits 9:8:
 * Define the maximum transfer size. (64 / 128 / 256)
 * Bit 10:
 * When bit is set and transfer size is set to 128B, the TFH will enable
 * reading chunks of more than 64B only if the read address is aligned to 128B.
 * In case of DRAM read address which is not aligned to 128B, the TFH will
 * enable transfer size which doesn't cross 64B DRAM address boundary.
*/
#define TFH_TRANSFER_MODE		(0x1F40)
#define TFH_TRANSFER_MAX_PENDING_REQ	0xc
#define TFH_CHUNK_SIZE_128			BIT(8)
#define TFH_CHUNK_SPLIT_MODE		BIT(10)
/*
 * Defines the offset address in dwords referring from the beginning of the
 * Tx CMD which will be updated in DRAM.
 * Note that the TFH offset address for Tx CMD update is always referring to
 * the start of the TFD first TB.
 * In case of a DRAM Tx CMD update the TFH will update PN and Key ID
 */
#define TFH_TXCMD_UPDATE_CFG		(0x1F48)
/*
 * Controls TX DMA operation
 *
 * BIT fields:
 *
 * Bits 31:30: Enable the SRAM DMA channel.
 * Turning on bit 31 will kick the SRAM2DRAM DMA.
 * Note that the sram2dram may be enabled only after configuring the DRAM and
 * SRAM addresses registers and the byte count register.
 * Bits 25:24: Defines the interrupt target upon dram2sram transfer done. When
 * set to 1 - interrupt is sent to the driver
 * Bit 0: Indicates the snoop configuration
*/
#define TFH_SRV_DMA_CHNL0_CTRL	(0x1F60)
#define TFH_SRV_DMA_SNOOP	BIT(0)
#define TFH_SRV_DMA_TO_DRIVER	BIT(24)
#define TFH_SRV_DMA_START	BIT(31)

/* Defines the DMA SRAM write start address to transfer a data block */
#define TFH_SRV_DMA_CHNL0_SRAM_ADDR	(0x1F64)

/* Defines the 64bits DRAM start address to read the DMA data block from */
#define TFH_SRV_DMA_CHNL0_DRAM_ADDR	(0x1F68)

/*
 * Defines the number of bytes to transfer from DRAM to SRAM.
 * Note that this register may be configured with non-dword aligned size.
 */
#define TFH_SRV_DMA_CHNL0_BC	(0x1F70)

/**
 * Rx SRAM Control and Status Registers (RSCSR)
 *
 * These registers provide handshake between driver and device for the Rx queue
 * (this queue handles *all* command responses, notifications, Rx data, etc.
 * sent from uCode to host driver).  Unlike Tx, there is only one Rx
 * queue, and only one Rx DMA/FIFO channel.  Also unlike Tx, which can
 * concatenate up to 20 DRAM buffers to form a Tx frame, each Receive Buffer
 * Descriptor (RBD) points to only one Rx Buffer (RB); there is a 1:1
 * mapping between RBDs and RBs.
 *
 * Driver must allocate host DRAM memory for the following, and set the
 * physical address of each into device registers:
 *
 * 1)  Receive Buffer Descriptor (RBD) circular buffer (CB), typically with 256
 *     entries (although any power of 2, up to 4096, is selectable by driver).
 *     Each entry (1 dword) points to a receive buffer (RB) of consistent size
 *     (typically 4K, although 8K or 16K are also selectable by driver).
 *     Driver sets up RB size and number of RBDs in the CB via Rx config
 *     register FH_MEM_RCSR_CHNL0_CONFIG_REG.
 *
 *     Bit fields within one RBD:
 *     27-0:  Receive Buffer physical address bits [35:8], 256-byte aligned
 *
 *     Driver sets physical address [35:8] of base of RBD circular buffer
 *     into FH_RSCSR_CHNL0_RBDCB_BASE_REG [27:0].
 *
 * 2)  Rx status buffer, 8 bytes, in which uCode indicates which Rx Buffers
 *     (RBs) have been filled, via a "write pointer", actually the index of
 *     the RB's corresponding RBD within the circular buffer.  Driver sets
 *     physical address [35:4] into FH_RSCSR_CHNL0_STTS_WPTR_REG [31:0].
 *
 *     Bit fields in lower dword of Rx status buffer (upper dword not used
 *     by driver:
 *     31-12:  Not used by driver
 *     11- 0:  Index of last filled Rx buffer descriptor
 *             (device writes, driver reads this value)
 *
 * As the driver prepares Receive Buffers (RBs) for device to fill, driver must
 * enter pointers to these RBs into contiguous RBD circular buffer entries,
 * and update the device's "write" index register,
 * FH_RSCSR_CHNL0_RBDCB_WPTR_REG.
 *
 * This "write" index corresponds to the *next* RBD that the driver will make
 * available, i.e. one RBD past the tail of the ready-to-fill RBDs within
 * the circular buffer.  This value should initially be 0 (before preparing any
 * RBs), should be 8 after preparing the first 8 RBs (for example), and must
 * wrap back to 0 at the end of the circular buffer (but don't wrap before
 * "read" index has advanced past 1!  See below).
 * NOTE:  DEVICE EXPECTS THE WRITE INDEX TO BE INCREMENTED IN MULTIPLES OF 8.
 *
 * As the device fills RBs (referenced from contiguous RBDs within the circular
 * buffer), it updates the Rx status buffer in host DRAM, 2) described above,
 * to tell the driver the index of the latest filled RBD.  The driver must
 * read this "read" index from DRAM after receiving an Rx interrupt from device
 *
 * The driver must also internally keep track of a third index, which is the
 * next RBD to process.  When receiving an Rx interrupt, driver should process
 * all filled but unprocessed RBs up to, but not including, the RB
 * corresponding to the "read" index.  For example, if "read" index becomes "1",
 * driver may process the RB pointed to by RBD 0.  Depending on volume of
 * traffic, there may be many RBs to process.
 *
 * If read index == write index, device thinks there is no room to put new data.
 * Due to this, the maximum number of filled RBs is 255, instead of 256.  To
 * be safe, make sure that there is a gap of at least 2 RBDs between "write"
 * and "read" indexes; that is, make sure that there are no more than 254
 * buffers waiting to be filled.
 */
#define FH_MEM_RSCSR_LOWER_BOUND	(FH_MEM_LOWER_BOUND + 0xBC0)
#define FH_MEM_RSCSR_UPPER_BOUND	(FH_MEM_LOWER_BOUND + 0xC00)
#define FH_MEM_RSCSR_CHNL0		(FH_MEM_RSCSR_LOWER_BOUND)

/**
 * Physical base address of 8-byte Rx Status buffer.
 * Bit fields:
 *  31-0: Rx status buffer physical base address [35:4], must 16-byte aligned.
 */
#define FH_RSCSR_CHNL0_STTS_WPTR_REG	(FH_MEM_RSCSR_CHNL0)

/**
 * Physical base address of Rx Buffer Descriptor Circular Buffer.
 * Bit fields:
 *  27-0:  RBD CD physical base address [35:8], must be 256-byte aligned.
 */
#define FH_RSCSR_CHNL0_RBDCB_BASE_REG	(FH_MEM_RSCSR_CHNL0 + 0x004)

/**
 * Rx write pointer (index, really!).
 * Bit fields:
 *  11-0:  Index of driver's most recent prepared-to-be-filled RBD, + 1.
 *         NOTE:  For 256-entry circular buffer, use only bits [7:0].
 */
#define FH_RSCSR_CHNL0_RBDCB_WPTR_REG	(FH_MEM_RSCSR_CHNL0 + 0x008)
#define FH_RSCSR_CHNL0_WPTR        (FH_RSCSR_CHNL0_RBDCB_WPTR_REG)

#define FW_RSCSR_CHNL0_RXDCB_RDPTR_REG	(FH_MEM_RSCSR_CHNL0 + 0x00c)
#define FH_RSCSR_CHNL0_RDPTR		FW_RSCSR_CHNL0_RXDCB_RDPTR_REG

/**
 * Rx Config/Status Registers (RCSR)
 * Rx Config Reg for channel 0 (only channel used)
 *
 * Driver must initialize FH_MEM_RCSR_CHNL0_CONFIG_REG as follows for
 * normal operation (see bit fields).
 *
 * Clearing FH_MEM_RCSR_CHNL0_CONFIG_REG to 0 turns off Rx DMA.
 * Driver should poll FH_MEM_RSSR_RX_STATUS_REG	for
 * FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE (bit 24) before continuing.
 *
 * Bit fields:
 * 31-30: Rx DMA channel enable: '00' off/pause, '01' pause at end of frame,
 *        '10' operate normally
 * 29-24: reserved
 * 23-20: # RBDs in circular buffer = 2^value; use "8" for 256 RBDs (normal),
 *        min "5" for 32 RBDs, max "12" for 4096 RBDs.
 * 19-18: reserved
 * 17-16: size of each receive buffer; '00' 4K (normal), '01' 8K,
 *        '10' 12K, '11' 16K.
 * 15-14: reserved
 * 13-12: IRQ destination; '00' none, '01' host driver (normal operation)
 * 11- 4: timeout for closing Rx buffer and interrupting host (units 32 usec)
 *        typical value 0x10 (about 1/2 msec)
 *  3- 0: reserved
 */
#define FH_MEM_RCSR_LOWER_BOUND      (FH_MEM_LOWER_BOUND + 0xC00)
#define FH_MEM_RCSR_UPPER_BOUND      (FH_MEM_LOWER_BOUND + 0xCC0)
#define FH_MEM_RCSR_CHNL0            (FH_MEM_RCSR_LOWER_BOUND)

#define FH_MEM_RCSR_CHNL0_CONFIG_REG	(FH_MEM_RCSR_CHNL0)
#define FH_MEM_RCSR_CHNL0_RBDCB_WPTR	(FH_MEM_RCSR_CHNL0 + 0x8)
#define FH_MEM_RCSR_CHNL0_FLUSH_RB_REQ	(FH_MEM_RCSR_CHNL0 + 0x10)

#define FH_RCSR_CHNL0_RX_CONFIG_RB_TIMEOUT_MSK (0x00000FF0) /* bits 4-11 */
#define FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_MSK   (0x00001000) /* bits 12 */
#define FH_RCSR_CHNL0_RX_CONFIG_SINGLE_FRAME_MSK (0x00008000) /* bit 15 */
#define FH_RCSR_CHNL0_RX_CONFIG_RB_SIZE_MSK   (0x00030000) /* bits 16-17 */
#define FH_RCSR_CHNL0_RX_CONFIG_RBDBC_SIZE_MSK (0x00F00000) /* bits 20-23 */
#define FH_RCSR_CHNL0_RX_CONFIG_DMA_CHNL_EN_MSK (0xC0000000) /* bits 30-31*/

#define FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS	(20)
#define FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS	(4)
#define RX_RB_TIMEOUT	(0x11)

#define FH_RCSR_RX_CONFIG_CHNL_EN_PAUSE_VAL         (0x00000000)
#define FH_RCSR_RX_CONFIG_CHNL_EN_PAUSE_EOF_VAL     (0x40000000)
#define FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL        (0x80000000)

#define FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K    (0x00000000)
#define FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_8K    (0x00010000)
#define FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_12K   (0x00020000)
#define FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_16K   (0x00030000)

#define FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY              (0x00000004)
#define FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_NO_INT_VAL    (0x00000000)
#define FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL  (0x00001000)

/**
 * Rx Shared Status Registers (RSSR)
 *
 * After stopping Rx DMA channel (writing 0 to
 * FH_MEM_RCSR_CHNL0_CONFIG_REG), driver must poll
 * FH_MEM_RSSR_RX_STATUS_REG until Rx channel is idle.
 *
 * Bit fields:
 *  24:  1 = Channel 0 is idle
 *
 * FH_MEM_RSSR_SHARED_CTRL_REG and FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV
 * contain default values that should not be altered by the driver.
 */
#define FH_MEM_RSSR_LOWER_BOUND           (FH_MEM_LOWER_BOUND + 0xC40)
#define FH_MEM_RSSR_UPPER_BOUND           (FH_MEM_LOWER_BOUND + 0xD00)

#define FH_MEM_RSSR_SHARED_CTRL_REG       (FH_MEM_RSSR_LOWER_BOUND)
#define FH_MEM_RSSR_RX_STATUS_REG	(FH_MEM_RSSR_LOWER_BOUND + 0x004)
#define FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV\
					(FH_MEM_RSSR_LOWER_BOUND + 0x008)

#define FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE	(0x01000000)

#define FH_MEM_TFDIB_REG1_ADDR_BITSHIFT	28
#define FH_MEM_TB_MAX_LENGTH			(0x00020000)

/* 9000 rx series registers */

#define RFH_Q0_FRBDCB_BA_LSB 0xA08000 /* 64 bit address */
#define RFH_Q_FRBDCB_BA_LSB(q) (RFH_Q0_FRBDCB_BA_LSB + (q) * 8)
/* Write index table */
#define RFH_Q0_FRBDCB_WIDX 0xA08080
#define RFH_Q_FRBDCB_WIDX(q) (RFH_Q0_FRBDCB_WIDX + (q) * 4)
/* Write index table - shadow registers */
#define RFH_Q0_FRBDCB_WIDX_TRG 0x1C80
#define RFH_Q_FRBDCB_WIDX_TRG(q) (RFH_Q0_FRBDCB_WIDX_TRG + (q) * 4)
/* Read index table */
#define RFH_Q0_FRBDCB_RIDX 0xA080C0
#define RFH_Q_FRBDCB_RIDX(q) (RFH_Q0_FRBDCB_RIDX + (q) * 4)
/* Used list table */
#define RFH_Q0_URBDCB_BA_LSB 0xA08100 /* 64 bit address */
#define RFH_Q_URBDCB_BA_LSB(q) (RFH_Q0_URBDCB_BA_LSB + (q) * 8)
/* Write index table */
#define RFH_Q0_URBDCB_WIDX 0xA08180
#define RFH_Q_URBDCB_WIDX(q) (RFH_Q0_URBDCB_WIDX + (q) * 4)
#define RFH_Q0_URBDCB_VAID 0xA081C0
#define RFH_Q_URBDCB_VAID(q) (RFH_Q0_URBDCB_VAID + (q) * 4)
/* stts */
#define RFH_Q0_URBD_STTS_WPTR_LSB 0xA08200 /*64 bits address */
#define RFH_Q_URBD_STTS_WPTR_LSB(q) (RFH_Q0_URBD_STTS_WPTR_LSB + (q) * 8)

#define RFH_Q0_ORB_WPTR_LSB 0xA08280
#define RFH_Q_ORB_WPTR_LSB(q) (RFH_Q0_ORB_WPTR_LSB + (q) * 8)
#define RFH_RBDBUF_RBD0_LSB 0xA08300
#define RFH_RBDBUF_RBD_LSB(q) (RFH_RBDBUF_RBD0_LSB + (q) * 8)

/**
 * RFH Status Register
 *
 * Bit fields:
 *
 * Bit 29: RBD_FETCH_IDLE
 * This status flag is set by the RFH when there is no active RBD fetch from
 * DRAM.
 * Once the RFH RBD controller starts fetching (or when there is a pending
 * RBD read response from DRAM), this flag is immediately turned off.
 *
 * Bit 30: SRAM_DMA_IDLE
 * This status flag is set by the RFH when there is no active transaction from
 * SRAM to DRAM.
 * Once the SRAM to DRAM DMA is active, this flag is immediately turned off.
 *
 * Bit 31: RXF_DMA_IDLE
 * This status flag is set by the RFH when there is no active transaction from
 * RXF to DRAM.
 * Once the RXF-to-DRAM DMA is active, this flag is immediately turned off.
 */
#define RFH_GEN_STATUS		0xA09808
#define RFH_GEN_STATUS_GEN3	0xA07824
#define RBD_FETCH_IDLE	BIT(29)
#define SRAM_DMA_IDLE	BIT(30)
#define RXF_DMA_IDLE	BIT(31)

/* DMA configuration */
#define RFH_RXF_DMA_CFG		0xA09820
#define RFH_RXF_DMA_CFG_GEN3	0xA07880
/* RB size */
#define RFH_RXF_DMA_RB_SIZE_MASK (0x000F0000) /* bits 16-19 */
#define RFH_RXF_DMA_RB_SIZE_POS 16
#define RFH_RXF_DMA_RB_SIZE_1K	(0x1 << RFH_RXF_DMA_RB_SIZE_POS)
#define RFH_RXF_DMA_RB_SIZE_2K	(0x2 << RFH_RXF_DMA_RB_SIZE_POS)
#define RFH_RXF_DMA_RB_SIZE_4K	(0x4 << RFH_RXF_DMA_RB_SIZE_POS)
#define RFH_RXF_DMA_RB_SIZE_8K	(0x8 << RFH_RXF_DMA_RB_SIZE_POS)
#define RFH_RXF_DMA_RB_SIZE_12K	(0x9 << RFH_RXF_DMA_RB_SIZE_POS)
#define RFH_RXF_DMA_RB_SIZE_16K	(0xA << RFH_RXF_DMA_RB_SIZE_POS)
#define RFH_RXF_DMA_RB_SIZE_20K	(0xB << RFH_RXF_DMA_RB_SIZE_POS)
#define RFH_RXF_DMA_RB_SIZE_24K	(0xC << RFH_RXF_DMA_RB_SIZE_POS)
#define RFH_RXF_DMA_RB_SIZE_28K	(0xD << RFH_RXF_DMA_RB_SIZE_POS)
#define RFH_RXF_DMA_RB_SIZE_32K	(0xE << RFH_RXF_DMA_RB_SIZE_POS)
/* RB Circular Buffer size:defines the table sizes in RBD units */
#define RFH_RXF_DMA_RBDCB_SIZE_MASK (0x00F00000) /* bits 20-23 */
#define RFH_RXF_DMA_RBDCB_SIZE_POS 20
#define RFH_RXF_DMA_RBDCB_SIZE_8	(0x3 << RFH_RXF_DMA_RBDCB_SIZE_POS)
#define RFH_RXF_DMA_RBDCB_SIZE_16	(0x4 << RFH_RXF_DMA_RBDCB_SIZE_POS)
#define RFH_RXF_DMA_RBDCB_SIZE_32	(0x5 << RFH_RXF_DMA_RBDCB_SIZE_POS)
#define RFH_RXF_DMA_RBDCB_SIZE_64	(0x7 << RFH_RXF_DMA_RBDCB_SIZE_POS)
#define RFH_RXF_DMA_RBDCB_SIZE_128	(0x7 << RFH_RXF_DMA_RBDCB_SIZE_POS)
#define RFH_RXF_DMA_RBDCB_SIZE_256	(0x8 << RFH_RXF_DMA_RBDCB_SIZE_POS)
#define RFH_RXF_DMA_RBDCB_SIZE_512	(0x9 << RFH_RXF_DMA_RBDCB_SIZE_POS)
#define RFH_RXF_DMA_RBDCB_SIZE_1024	(0xA << RFH_RXF_DMA_RBDCB_SIZE_POS)
#define RFH_RXF_DMA_RBDCB_SIZE_2048	(0xB << RFH_RXF_DMA_RBDCB_SIZE_POS)
#define RFH_RXF_DMA_MIN_RB_SIZE_MASK	(0x03000000) /* bit 24-25 */
#define RFH_RXF_DMA_MIN_RB_SIZE_POS	24
#define RFH_RXF_DMA_MIN_RB_4_8		(3 << RFH_RXF_DMA_MIN_RB_SIZE_POS)
#define RFH_RXF_DMA_DROP_TOO_LARGE_MASK	(0x04000000) /* bit 26 */
#define RFH_RXF_DMA_SINGLE_FRAME_MASK	(0x20000000) /* bit 29 */
#define RFH_DMA_EN_MASK			(0xC0000000) /* bits 30-31*/
#define RFH_DMA_EN_ENABLE_VAL		BIT(31)

#define RFH_RXF_RXQ_ACTIVE 0xA0980C

#define RFH_GEN_CFG	0xA09800
#define RFH_GEN_CFG_SERVICE_DMA_SNOOP	BIT(0)
#define RFH_GEN_CFG_RFH_DMA_SNOOP	BIT(1)
#define RFH_GEN_CFG_RB_CHUNK_SIZE	BIT(4)
#define RFH_GEN_CFG_RB_CHUNK_SIZE_128	1
#define RFH_GEN_CFG_RB_CHUNK_SIZE_64	0
/* the driver assumes everywhere that the default RXQ is 0 */
#define RFH_GEN_CFG_DEFAULT_RXQ_NUM	0xF00
#define RFH_GEN_CFG_VAL(_n, _v)		FIELD_PREP(RFH_GEN_CFG_ ## _n, _v)

/* end of 9000 rx series registers */

/* TFDB  Area - TFDs buffer table */
#define FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK      (0xFFFFFFFF)
#define FH_TFDIB_LOWER_BOUND       (FH_MEM_LOWER_BOUND + 0x900)
#define FH_TFDIB_UPPER_BOUND       (FH_MEM_LOWER_BOUND + 0x958)
#define FH_TFDIB_CTRL0_REG(_chnl)  (FH_TFDIB_LOWER_BOUND + 0x8 * (_chnl))
#define FH_TFDIB_CTRL1_REG(_chnl)  (FH_TFDIB_LOWER_BOUND + 0x8 * (_chnl) + 0x4)

/**
 * Transmit DMA Channel Control/Status Registers (TCSR)
 *
 * Device has one configuration register for each of 8 Tx DMA/FIFO channels
 * supported in hardware (don't confuse these with the 16 Tx queues in DRAM,
 * which feed the DMA/FIFO channels); config regs are separated by 0x20 bytes.
 *
 * To use a Tx DMA channel, driver must initialize its
 * FH_TCSR_CHNL_TX_CONFIG_REG(chnl) with:
 *
 * FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
 * FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL
 *
 * All other bits should be 0.
 *
 * Bit fields:
 * 31-30: Tx DMA channel enable: '00' off/pause, '01' pause at end of frame,
 *        '10' operate normally
 * 29- 4: Reserved, set to "0"
 *     3: Enable internal DMA requests (1, normal operation), disable (0)
 *  2- 0: Reserved, set to "0"
 */
#define FH_TCSR_LOWER_BOUND  (FH_MEM_LOWER_BOUND + 0xD00)
#define FH_TCSR_UPPER_BOUND  (FH_MEM_LOWER_BOUND + 0xE60)

/* Find Control/Status reg for given Tx DMA/FIFO channel */
#define FH_TCSR_CHNL_NUM                            (8)

/* TCSR: tx_config register values */
#define FH_TCSR_CHNL_TX_CONFIG_REG(_chnl)	\
		(FH_TCSR_LOWER_BOUND + 0x20 * (_chnl))
#define FH_TCSR_CHNL_TX_CREDIT_REG(_chnl)	\
		(FH_TCSR_LOWER_BOUND + 0x20 * (_chnl) + 0x4)
#define FH_TCSR_CHNL_TX_BUF_STS_REG(_chnl)	\
		(FH_TCSR_LOWER_BOUND + 0x20 * (_chnl) + 0x8)

#define FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_TXF		(0x00000000)
#define FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_DRV		(0x00000001)

#define FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE	(0x00000000)
#define FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE	(0x00000008)

#define FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_NOINT	(0x00000000)
#define FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD	(0x00100000)
#define FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_IFTFD	(0x00200000)

#define FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_NOINT	(0x00000000)
#define FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_ENDTFD	(0x00400000)
#define FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_IFTFD	(0x00800000)

#define FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE	(0x00000000)
#define FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE_EOF	(0x40000000)
#define FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE	(0x80000000)

#define FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_EMPTY	(0x00000000)
#define FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_WAIT	(0x00002000)
#define FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID	(0x00000003)

#define FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM		(20)
#define FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX		(12)

/**
 * Tx Shared Status Registers (TSSR)
 *
 * After stopping Tx DMA channel (writing 0 to
 * FH_TCSR_CHNL_TX_CONFIG_REG(chnl)), driver must poll
 * FH_TSSR_TX_STATUS_REG until selected Tx channel is idle
 * (channel's buffers empty | no pending requests).
 *
 * Bit fields:
 * 31-24:  1 = Channel buffers empty (channel 7:0)
 * 23-16:  1 = No pending requests (channel 7:0)
 */
#define FH_TSSR_LOWER_BOUND		(FH_MEM_LOWER_BOUND + 0xEA0)
#define FH_TSSR_UPPER_BOUND		(FH_MEM_LOWER_BOUND + 0xEC0)

#define FH_TSSR_TX_STATUS_REG		(FH_TSSR_LOWER_BOUND + 0x010)

/**
 * Bit fields for TSSR(Tx Shared Status & Control) error status register:
 * 31:  Indicates an address error when accessed to internal memory
 *	uCode/driver must write "1" in order to clear this flag
 * 30:  Indicates that Host did not send the expected number of dwords to FH
 *	uCode/driver must write "1" in order to clear this flag
 * 16-9:Each status bit is for one channel. Indicates that an (Error) ActDMA
 *	command was received from the scheduler while the TRB was already full
 *	with previous command
 *	uCode/driver must write "1" in order to clear this flag
 * 7-0: Each status bit indicates a channel's TxCredit error. When an error
 *	bit is set, it indicates that the FH has received a full indication
 *	from the RTC TxFIFO and the current value of the TxCredit counter was
 *	not equal to zero. This mean that the credit mechanism was not
 *	synchronized to the TxFIFO status
 *	uCode/driver must write "1" in order to clear this flag
 */
#define FH_TSSR_TX_ERROR_REG		(FH_TSSR_LOWER_BOUND + 0x018)
#define FH_TSSR_TX_MSG_CONFIG_REG	(FH_TSSR_LOWER_BOUND + 0x008)

#define FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(_chnl) ((1 << (_chnl)) << 16)

/* Tx service channels */
#define FH_SRVC_CHNL		(9)
#define FH_SRVC_LOWER_BOUND	(FH_MEM_LOWER_BOUND + 0x9C8)
#define FH_SRVC_UPPER_BOUND	(FH_MEM_LOWER_BOUND + 0x9D0)
#define FH_SRVC_CHNL_SRAM_ADDR_REG(_chnl) \
		(FH_SRVC_LOWER_BOUND + ((_chnl) - 9) * 0x4)

#define FH_TX_CHICKEN_BITS_REG	(FH_MEM_LOWER_BOUND + 0xE98)
#define FH_TX_TRB_REG(_chan)	(FH_MEM_LOWER_BOUND + 0x958 + (_chan) * 4)

/* Instruct FH to increment the retry count of a packet when
 * it is brought from the memory to TX-FIFO
 */
#define FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN	(0x00000002)

#define RX_POOL_SIZE(rbds)	((rbds) - 1 +	\
				 IWL_MAX_RX_HW_QUEUES *	\
				 (RX_CLAIM_REQ_ALLOC - RX_POST_REQ_ALLOC))
/* cb size is the exponent */
#define RX_QUEUE_CB_SIZE(x)	ilog2(x)

#define RX_QUEUE_SIZE                         256
#define RX_QUEUE_MASK                         255
#define RX_QUEUE_SIZE_LOG                     8

/**
 * struct iwl_rb_status - reserve buffer status
 * 	host memory mapped FH registers
 * @closed_rb_num [0:11] - Indicates the index of the RB which was closed
 * @closed_fr_num [0:11] - Indicates the index of the RX Frame which was closed
 * @finished_rb_num [0:11] - Indicates the index of the current RB
 * 	in which the last frame was written to
 * @finished_fr_num [0:11] - Indicates the index of the RX Frame
 * 	which was transferred
 */
struct iwl_rb_status {
	__le16 closed_rb_num;
	__le16 closed_fr_num;
	__le16 finished_rb_num;
	__le16 finished_fr_nam;
	__le32 __unused;
} __packed;


#define TFD_QUEUE_SIZE_MAX      (256)
#define TFD_QUEUE_SIZE_MAX_GEN3 (65536)
/* cb size is the exponent - 3 */
#define TFD_QUEUE_CB_SIZE(x)	(ilog2(x) - 3)
#define TFD_QUEUE_SIZE_BC_DUP	(64)
#define TFD_QUEUE_BC_SIZE	(TFD_QUEUE_SIZE_MAX + TFD_QUEUE_SIZE_BC_DUP)
#define TFD_QUEUE_BC_SIZE_GEN3	1024
#define IWL_TX_DMA_MASK        DMA_BIT_MASK(36)
#define IWL_NUM_OF_TBS		20
#define IWL_TFH_NUM_TBS		25

static inline u8 iwl_get_dma_hi_addr(dma_addr_t addr)
{
	return (sizeof(addr) > sizeof(u32) ? upper_32_bits(addr) : 0) & 0xF;
}

/**
 * enum iwl_tfd_tb_hi_n_len - TB hi_n_len bits
 * @TB_HI_N_LEN_ADDR_HI_MSK: high 4 bits (to make it 36) of DMA address
 * @TB_HI_N_LEN_LEN_MSK: length of the TB
 */
enum iwl_tfd_tb_hi_n_len {
	TB_HI_N_LEN_ADDR_HI_MSK	= 0xf,
	TB_HI_N_LEN_LEN_MSK	= 0xfff0,
};

/**
 * struct iwl_tfd_tb transmit buffer descriptor within transmit frame descriptor
 *
 * This structure contains dma address and length of transmission address
 *
 * @lo: low [31:0] portion of the dma address of TX buffer
 * 	every even is unaligned on 16 bit boundary
 * @hi_n_len: &enum iwl_tfd_tb_hi_n_len
 */
struct iwl_tfd_tb {
	__le32 lo;
	__le16 hi_n_len;
} __packed;

/**
 * struct iwl_tfh_tb transmit buffer descriptor within transmit frame descriptor
 *
 * This structure contains dma address and length of transmission address
 *
 * @tb_len length of the tx buffer
 * @addr 64 bits dma address
 */
struct iwl_tfh_tb {
	__le16 tb_len;
	__le64 addr;
} __packed;

/**
 * Each Tx queue uses a circular buffer of 256 TFDs stored in host DRAM.
 * Both driver and device share these circular buffers, each of which must be
 * contiguous 256 TFDs.
 * For pre 22000 HW it is 256 x 128 bytes-per-TFD = 32 KBytes
 * For 22000 HW and on it is 256 x 256 bytes-per-TFD = 65 KBytes
 *
 * Driver must indicate the physical address of the base of each
 * circular buffer via the FH_MEM_CBBC_QUEUE registers.
 *
 * Each TFD contains pointer/size information for up to 20 / 25 data buffers
 * in host DRAM.  These buffers collectively contain the (one) frame described
 * by the TFD.  Each buffer must be a single contiguous block of memory within
 * itself, but buffers may be scattered in host DRAM.  Each buffer has max size
 * of (4K - 4).  The concatenates all of a TFD's buffers into a single
 * Tx frame, up to 8 KBytes in size.
 *
 * A maximum of 255 (not 256!) TFDs may be on a queue waiting for Tx.
 */

/**
 * struct iwl_tfd - Transmit Frame Descriptor (TFD)
 * @ __reserved1[3] reserved
 * @ num_tbs 0-4 number of active tbs
 *	     5   reserved
 *	     6-7 padding (not used)
 * @ tbs[20]	transmit frame buffer descriptors
 * @ __pad	padding
 */
struct iwl_tfd {
	u8 __reserved1[3];
	u8 num_tbs;
	struct iwl_tfd_tb tbs[IWL_NUM_OF_TBS];
	__le32 __pad;
} __packed;

/**
 * struct iwl_tfh_tfd - Transmit Frame Descriptor (TFD)
 * @ num_tbs 0-4 number of active tbs
 *	     5 -15   reserved
 * @ tbs[25]	transmit frame buffer descriptors
 * @ __pad	padding
 */
struct iwl_tfh_tfd {
	__le16 num_tbs;
	struct iwl_tfh_tb tbs[IWL_TFH_NUM_TBS];
	__le32 __pad;
} __packed;

/* Keep Warm Size */
#define IWL_KW_SIZE 0x1000	/* 4k */

/* Fixed (non-configurable) rx data from phy */

/**
 * struct iwlagn_schedq_bc_tbl scheduler byte count table
 *	base physical address provided by SCD_DRAM_BASE_ADDR
 * For devices up to 22000:
 * @tfd_offset  0-12 - tx command byte count
 *		12-16 - station index
 * For 22000:
 * @tfd_offset  0-12 - tx command byte count
 *		12-13 - number of 64 byte chunks
 *		14-16 - reserved
 */
struct iwlagn_scd_bc_tbl {
	__le16 tfd_offset[TFD_QUEUE_BC_SIZE];
} __packed;

/**
 * struct iwl_gen3_bc_tbl scheduler byte count table gen3
 * For AX210 and on:
 * @tfd_offset: 0-12 - tx command byte count
 *		12-13 - number of 64 byte chunks
 *		14-16 - reserved
 */
struct iwl_gen3_bc_tbl {
	__le16 tfd_offset[TFD_QUEUE_BC_SIZE_GEN3];
} __packed;

#endif /* !__iwl_fh_h__ */
