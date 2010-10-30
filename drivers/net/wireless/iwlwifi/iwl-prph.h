/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2010 Intel Corporation. All rights reserved.
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
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2010 Intel Corporation. All rights reserved.
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

#define APMG_CLK_VAL_DMA_CLK_RQT	(0x00000200)
#define APMG_CLK_VAL_BSM_CLK_RQT	(0x00000800)


#define APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS	(0x00400000)
#define APMG_PS_CTRL_VAL_RESET_REQ		(0x04000000)
#define APMG_PS_CTRL_MSK_PWR_SRC		(0x03000000)
#define APMG_PS_CTRL_VAL_PWR_SRC_VMAIN		(0x00000000)
#define APMG_PS_CTRL_VAL_PWR_SRC_MAX		(0x01000000) /* 3945 only */
#define APMG_PS_CTRL_VAL_PWR_SRC_VAUX		(0x02000000)
#define APMG_SVR_VOLTAGE_CONFIG_BIT_MSK	(0x000001E0) /* bit 8:5 */
#define APMG_SVR_DIGITAL_VOLTAGE_1_32		(0x00000060)

#define APMG_PCIDEV_STT_VAL_L1_ACT_DIS		(0x00000800)

/**
 * BSM (Bootstrap State Machine)
 *
 * The Bootstrap State Machine (BSM) stores a short bootstrap uCode program
 * in special SRAM that does not power down when the embedded control
 * processor is sleeping (e.g. for periodic power-saving shutdowns of radio).
 *
 * When powering back up after sleeps (or during initial uCode load), the BSM
 * internally loads the short bootstrap program from the special SRAM into the
 * embedded processor's instruction SRAM, and starts the processor so it runs
 * the bootstrap program.
 *
 * This bootstrap program loads (via PCI busmaster DMA) instructions and data
 * images for a uCode program from host DRAM locations.  The host driver
 * indicates DRAM locations and sizes for instruction and data images via the
 * four BSM_DRAM_* registers.  Once the bootstrap program loads the new program,
 * the new program starts automatically.
 *
 * The uCode used for open-source drivers includes two programs:
 *
 * 1)  Initialization -- performs hardware calibration and sets up some
 *     internal data, then notifies host via "initialize alive" notification
 *     (struct iwl_init_alive_resp) that it has completed all of its work.
 *     After signal from host, it then loads and starts the runtime program.
 *     The initialization program must be used when initially setting up the
 *     NIC after loading the driver.
 *
 * 2)  Runtime/Protocol -- performs all normal runtime operations.  This
 *     notifies host via "alive" notification (struct iwl_alive_resp) that it
 *     is ready to be used.
 *
 * When initializing the NIC, the host driver does the following procedure:
 *
 * 1)  Load bootstrap program (instructions only, no data image for bootstrap)
 *     into bootstrap memory.  Use dword writes starting at BSM_SRAM_LOWER_BOUND
 *
 * 2)  Point (via BSM_DRAM_*) to the "initialize" uCode data and instruction
 *     images in host DRAM.
 *
 * 3)  Set up BSM to copy from BSM SRAM into uCode instruction SRAM when asked:
 *     BSM_WR_MEM_SRC_REG = 0
 *     BSM_WR_MEM_DST_REG = RTC_INST_LOWER_BOUND
 *     BSM_WR_MEM_DWCOUNT_REG = # dwords in bootstrap instruction image
 *
 * 4)  Load bootstrap into instruction SRAM:
 *     BSM_WR_CTRL_REG = BSM_WR_CTRL_REG_BIT_START
 *
 * 5)  Wait for load completion:
 *     Poll BSM_WR_CTRL_REG for BSM_WR_CTRL_REG_BIT_START = 0
 *
 * 6)  Enable future boot loads whenever NIC's power management triggers it:
 *     BSM_WR_CTRL_REG = BSM_WR_CTRL_REG_BIT_START_EN
 *
 * 7)  Start the NIC by removing all reset bits:
 *     CSR_RESET = 0
 *
 *     The bootstrap uCode (already in instruction SRAM) loads initialization
 *     uCode.  Initialization uCode performs data initialization, sends
 *     "initialize alive" notification to host, and waits for a signal from
 *     host to load runtime code.
 *
 * 4)  Point (via BSM_DRAM_*) to the "runtime" uCode data and instruction
 *     images in host DRAM.  The last register loaded must be the instruction
 *     byte count register ("1" in MSbit tells initialization uCode to load
 *     the runtime uCode):
 *     BSM_DRAM_INST_BYTECOUNT_REG = byte count | BSM_DRAM_INST_LOAD
 *
 * 5)  Wait for "alive" notification, then issue normal runtime commands.
 *
 * Data caching during power-downs:
 *
 * Just before the embedded controller powers down (e.g for automatic
 * power-saving modes, or for RFKILL), uCode stores (via PCI busmaster DMA)
 * a current snapshot of the embedded processor's data SRAM into host DRAM.
 * This caches the data while the embedded processor's memory is powered down.
 * Location and size are controlled by BSM_DRAM_DATA_* registers.
 *
 * NOTE:  Instruction SRAM does not need to be saved, since that doesn't
 *        change during operation; the original image (from uCode distribution
 *        file) can be used for reload.
 *
 * When powering back up, the BSM loads the bootstrap program.  Bootstrap looks
 * at the BSM_DRAM_* registers, which now point to the runtime instruction
 * image and the cached (modified) runtime data (*not* the initialization
 * uCode).  Bootstrap reloads these runtime images into SRAM, and restarts the
 * uCode from where it left off before the power-down.
 *
 * NOTE:  Initialization uCode does *not* run as part of the save/restore
 *        procedure.
 *
 * This save/restore method is mostly for autonomous power management during
 * normal operation (result of POWER_TABLE_CMD).  Platform suspend/resume and
 * RFKILL should use complete restarts (with total re-initialization) of uCode,
 * allowing total shutdown (including BSM memory).
 *
 * Note that, during normal operation, the host DRAM that held the initial
 * startup data for the runtime code is now being used as a backup data cache
 * for modified data!  If you need to completely re-initialize the NIC, make
 * sure that you use the runtime data image from the uCode distribution file,
 * not the modified/saved runtime data.  You may want to store a separate
 * "clean" runtime data image in DRAM to avoid disk reads of distribution file.
 */

/* BSM bit fields */
#define BSM_WR_CTRL_REG_BIT_START     (0x80000000) /* start boot load now */
#define BSM_WR_CTRL_REG_BIT_START_EN  (0x40000000) /* enable boot after pwrup*/
#define BSM_DRAM_INST_LOAD            (0x80000000) /* start program load now */

/* BSM addresses */
#define BSM_BASE                     (PRPH_BASE + 0x3400)
#define BSM_END                      (PRPH_BASE + 0x3800)

#define BSM_WR_CTRL_REG              (BSM_BASE + 0x000) /* ctl and status */
#define BSM_WR_MEM_SRC_REG           (BSM_BASE + 0x004) /* source in BSM mem */
#define BSM_WR_MEM_DST_REG           (BSM_BASE + 0x008) /* dest in SRAM mem */
#define BSM_WR_DWCOUNT_REG           (BSM_BASE + 0x00C) /* bytes */
#define BSM_WR_STATUS_REG            (BSM_BASE + 0x010) /* bit 0:  1 == done */

/*
 * Pointers and size regs for bootstrap load and data SRAM save/restore.
 * NOTE:  3945 pointers use bits 31:0 of DRAM address.
 *        4965 pointers use bits 35:4 of DRAM address.
 */
#define BSM_DRAM_INST_PTR_REG        (BSM_BASE + 0x090)
#define BSM_DRAM_INST_BYTECOUNT_REG  (BSM_BASE + 0x094)
#define BSM_DRAM_DATA_PTR_REG        (BSM_BASE + 0x098)
#define BSM_DRAM_DATA_BYTECOUNT_REG  (BSM_BASE + 0x09C)

/*
 * BSM special memory, stays powered on during power-save sleeps.
 * Read/write, address range from LOWER_BOUND to (LOWER_BOUND + SIZE -1)
 */
#define BSM_SRAM_LOWER_BOUND         (PRPH_BASE + 0x3800)
#define BSM_SRAM_SIZE			(1024) /* bytes */


/* 3945 Tx scheduler registers */
#define ALM_SCD_BASE                        (PRPH_BASE + 0x2E00)
#define ALM_SCD_MODE_REG                    (ALM_SCD_BASE + 0x000)
#define ALM_SCD_ARASTAT_REG                 (ALM_SCD_BASE + 0x004)
#define ALM_SCD_TXFACT_REG                  (ALM_SCD_BASE + 0x010)
#define ALM_SCD_TXF4MF_REG                  (ALM_SCD_BASE + 0x014)
#define ALM_SCD_TXF5MF_REG                  (ALM_SCD_BASE + 0x020)
#define ALM_SCD_SBYP_MODE_1_REG             (ALM_SCD_BASE + 0x02C)
#define ALM_SCD_SBYP_MODE_2_REG             (ALM_SCD_BASE + 0x030)

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
 * Tx DMA FIFOs have dedicated purposes.  For 4965, they are used as follows
 * (cf. default_queue_to_tx_fifo in iwl-4965.c):
 *
 * 0 -- EDCA BK (background) frames, lowest priority
 * 1 -- EDCA BE (best effort) frames, normal priority
 * 2 -- EDCA VI (video) frames, higher priority
 * 3 -- EDCA VO (voice) and management frames, highest priority
 * 4 -- Commands (e.g. RXON, etc.)
 * 5 -- unused (HCCA)
 * 6 -- unused (HCCA)
 * 7 -- not used by driver (device-internal only)
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
 *     (struct iwl4965_sched_queue_byte_cnt_tbl) for this mode.
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
 * 2)  Shared scheduler data base in internal 4956 SRAM
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

/**
 * Max Tx window size is the max number of contiguous TFDs that the scheduler
 * can keep track of at one time when creating block-ack chains of frames.
 * Note that "64" matches the number of ack bits in a block-ack packet.
 * Driver should use SCD_WIN_SIZE and SCD_FRAME_LIMIT values to initialize
 * IWL49_SCD_CONTEXT_QUEUE_OFFSET(x) values.
 */
#define SCD_WIN_SIZE				64
#define SCD_FRAME_LIMIT				64

/* SCD registers are internal, must be accessed via HBUS_TARG_PRPH regs */
#define IWL49_SCD_START_OFFSET		0xa02c00

/*
 * 4965 tells driver SRAM address for internal scheduler structs via this reg.
 * Value is valid only after "Alive" response from uCode.
 */
#define IWL49_SCD_SRAM_BASE_ADDR           (IWL49_SCD_START_OFFSET + 0x0)

/*
 * Driver may need to update queue-empty bits after changing queue's
 * write and read pointers (indexes) during (re-)initialization (i.e. when
 * scheduler is not tracking what's happening).
 * Bit fields:
 * 31-16:  Write mask -- 1: update empty bit, 0: don't change empty bit
 * 15-00:  Empty state, one for each queue -- 1: empty, 0: non-empty
 * NOTE:  This register is not used by Linux driver.
 */
#define IWL49_SCD_EMPTY_BITS               (IWL49_SCD_START_OFFSET + 0x4)

/*
 * Physical base address of array of byte count (BC) circular buffers (CBs).
 * Each Tx queue has a BC CB in host DRAM to support Scheduler-ACK mode.
 * This register points to BC CB for queue 0, must be on 1024-byte boundary.
 * Others are spaced by 1024 bytes.
 * Each BC CB is 2 bytes * (256 + 64) = 740 bytes, followed by 384 bytes pad.
 * (Index into a queue's BC CB) = (index into queue's TFD CB) = (SSN & 0xff).
 * Bit fields:
 * 25-00:  Byte Count CB physical address [35:10], must be 1024-byte aligned.
 */
#define IWL49_SCD_DRAM_BASE_ADDR           (IWL49_SCD_START_OFFSET + 0x10)

/*
 * Enables any/all Tx DMA/FIFO channels.
 * Scheduler generates requests for only the active channels.
 * Set this to 0xff to enable all 8 channels (normal usage).
 * Bit fields:
 *  7- 0:  Enable (1), disable (0), one bit for each channel 0-7
 */
#define IWL49_SCD_TXFACT                   (IWL49_SCD_START_OFFSET + 0x1c)
/*
 * Queue (x) Write Pointers (indexes, really!), one for each Tx queue.
 * Initialized and updated by driver as new TFDs are added to queue.
 * NOTE:  If using Block Ack, index must correspond to frame's
 *        Start Sequence Number; index = (SSN & 0xff)
 * NOTE:  Alternative to HBUS_TARG_WRPTR, which is what Linux driver uses?
 */
#define IWL49_SCD_QUEUE_WRPTR(x)  (IWL49_SCD_START_OFFSET + 0x24 + (x) * 4)

/*
 * Queue (x) Read Pointers (indexes, really!), one for each Tx queue.
 * For FIFO mode, index indicates next frame to transmit.
 * For Scheduler-ACK mode, index indicates first frame in Tx window.
 * Initialized by driver, updated by scheduler.
 */
#define IWL49_SCD_QUEUE_RDPTR(x)  (IWL49_SCD_START_OFFSET + 0x64 + (x) * 4)

/*
 * Select which queues work in chain mode (1) vs. not (0).
 * Use chain mode to build chains of aggregated frames.
 * Bit fields:
 * 31-16:  Reserved
 * 15-00:  Mode, one bit for each queue -- 1: Chain mode, 0: one-at-a-time
 * NOTE:  If driver sets up queue for chain mode, it should be also set up
 *        Scheduler-ACK mode as well, via SCD_QUEUE_STATUS_BITS(x).
 */
#define IWL49_SCD_QUEUECHAIN_SEL  (IWL49_SCD_START_OFFSET + 0xd0)

/*
 * Select which queues interrupt driver when scheduler increments
 * a queue's read pointer (index).
 * Bit fields:
 * 31-16:  Reserved
 * 15-00:  Interrupt enable, one bit for each queue -- 1: enabled, 0: disabled
 * NOTE:  This functionality is apparently a no-op; driver relies on interrupts
 *        from Rx queue to read Tx command responses and update Tx queues.
 */
#define IWL49_SCD_INTERRUPT_MASK  (IWL49_SCD_START_OFFSET + 0xe4)

/*
 * Queue search status registers.  One for each queue.
 * Sets up queue mode and assigns queue to Tx DMA channel.
 * Bit fields:
 * 19-10: Write mask/enable bits for bits 0-9
 *     9: Driver should init to "0"
 *     8: Scheduler-ACK mode (1), non-Scheduler-ACK (i.e. FIFO) mode (0).
 *        Driver should init to "1" for aggregation mode, or "0" otherwise.
 *   7-6: Driver should init to "0"
 *     5: Window Size Left; indicates whether scheduler can request
 *        another TFD, based on window size, etc.  Driver should init
 *        this bit to "1" for aggregation mode, or "0" for non-agg.
 *   4-1: Tx FIFO to use (range 0-7).
 *     0: Queue is active (1), not active (0).
 * Other bits should be written as "0"
 *
 * NOTE:  If enabling Scheduler-ACK mode, chain mode should also be enabled
 *        via SCD_QUEUECHAIN_SEL.
 */
#define IWL49_SCD_QUEUE_STATUS_BITS(x)\
	(IWL49_SCD_START_OFFSET + 0x104 + (x) * 4)

/* Bit field positions */
#define IWL49_SCD_QUEUE_STTS_REG_POS_ACTIVE	(0)
#define IWL49_SCD_QUEUE_STTS_REG_POS_TXF	(1)
#define IWL49_SCD_QUEUE_STTS_REG_POS_WSL	(5)
#define IWL49_SCD_QUEUE_STTS_REG_POS_SCD_ACK	(8)

/* Write masks */
#define IWL49_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN	(10)
#define IWL49_SCD_QUEUE_STTS_REG_MSK		(0x0007FC00)

/**
 * 4965 internal SRAM structures for scheduler, shared with driver ...
 *
 * Driver should clear and initialize the following areas after receiving
 * "Alive" response from 4965 uCode, i.e. after initial
 * uCode load, or after a uCode load done for error recovery:
 *
 * SCD_CONTEXT_DATA_OFFSET (size 128 bytes)
 * SCD_TX_STTS_BITMAP_OFFSET (size 256 bytes)
 * SCD_TRANSLATE_TBL_OFFSET (size 32 bytes)
 *
 * Driver accesses SRAM via HBUS_TARG_MEM_* registers.
 * Driver reads base address of this scheduler area from SCD_SRAM_BASE_ADDR.
 * All OFFSET values must be added to this base address.
 */

/*
 * Queue context.  One 8-byte entry for each of 16 queues.
 *
 * Driver should clear this entire area (size 0x80) to 0 after receiving
 * "Alive" notification from uCode.  Additionally, driver should init
 * each queue's entry as follows:
 *
 * LS Dword bit fields:
 *  0-06:  Max Tx window size for Scheduler-ACK.  Driver should init to 64.
 *
 * MS Dword bit fields:
 * 16-22:  Frame limit.  Driver should init to 10 (0xa).
 *
 * Driver should init all other bits to 0.
 *
 * Init must be done after driver receives "Alive" response from 4965 uCode,
 * and when setting up queue for aggregation.
 */
#define IWL49_SCD_CONTEXT_DATA_OFFSET			0x380
#define IWL49_SCD_CONTEXT_QUEUE_OFFSET(x) \
			(IWL49_SCD_CONTEXT_DATA_OFFSET + ((x) * 8))

#define IWL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_POS		(0)
#define IWL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_MSK		(0x0000007F)
#define IWL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS	(16)
#define IWL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK	(0x007F0000)

/*
 * Tx Status Bitmap
 *
 * Driver should clear this entire area (size 0x100) to 0 after receiving
 * "Alive" notification from uCode.  Area is used only by device itself;
 * no other support (besides clearing) is required from driver.
 */
#define IWL49_SCD_TX_STTS_BITMAP_OFFSET		0x400

/*
 * RAxTID to queue translation mapping.
 *
 * When queue is in Scheduler-ACK mode, frames placed in a that queue must be
 * for only one combination of receiver address (RA) and traffic ID (TID), i.e.
 * one QOS priority level destined for one station (for this wireless link,
 * not final destination).  The SCD_TRANSLATE_TABLE area provides 16 16-bit
 * mappings, one for each of the 16 queues.  If queue is not in Scheduler-ACK
 * mode, the device ignores the mapping value.
 *
 * Bit fields, for each 16-bit map:
 * 15-9:  Reserved, set to 0
 *  8-4:  Index into device's station table for recipient station
 *  3-0:  Traffic ID (tid), range 0-15
 *
 * Driver should clear this entire area (size 32 bytes) to 0 after receiving
 * "Alive" notification from uCode.  To update a 16-bit map value, driver
 * must read a dword-aligned value from device SRAM, replace the 16-bit map
 * value of interest, and write the dword value back into device SRAM.
 */
#define IWL49_SCD_TRANSLATE_TBL_OFFSET		0x500

/* Find translation table dword to read/write for given queue */
#define IWL49_SCD_TRANSLATE_TBL_OFFSET_QUEUE(x) \
	((IWL49_SCD_TRANSLATE_TBL_OFFSET + ((x) * 2)) & 0xfffffffc)

#define IWL_SCD_TXFIFO_POS_TID			(0)
#define IWL_SCD_TXFIFO_POS_RA			(4)
#define IWL_SCD_QUEUE_RA_TID_MAP_RATID_MSK	(0x01FF)

/* agn SCD */
#define IWLAGN_SCD_QUEUE_STTS_REG_POS_TXF	(0)
#define IWLAGN_SCD_QUEUE_STTS_REG_POS_ACTIVE	(3)
#define IWLAGN_SCD_QUEUE_STTS_REG_POS_WSL	(4)
#define IWLAGN_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN (19)
#define IWLAGN_SCD_QUEUE_STTS_REG_MSK		(0x00FF0000)

#define IWLAGN_SCD_QUEUE_CTX_REG1_CREDIT_POS		(8)
#define IWLAGN_SCD_QUEUE_CTX_REG1_CREDIT_MSK		(0x00FFFF00)
#define IWLAGN_SCD_QUEUE_CTX_REG1_SUPER_CREDIT_POS	(24)
#define IWLAGN_SCD_QUEUE_CTX_REG1_SUPER_CREDIT_MSK	(0xFF000000)
#define IWLAGN_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS		(0)
#define IWLAGN_SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK		(0x0000007F)
#define IWLAGN_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS	(16)
#define IWLAGN_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK	(0x007F0000)

#define IWLAGN_SCD_CONTEXT_DATA_OFFSET		(0x600)
#define IWLAGN_SCD_TX_STTS_BITMAP_OFFSET		(0x7B1)
#define IWLAGN_SCD_TRANSLATE_TBL_OFFSET		(0x7E0)

#define IWLAGN_SCD_CONTEXT_QUEUE_OFFSET(x)\
	(IWLAGN_SCD_CONTEXT_DATA_OFFSET + ((x) * 8))

#define IWLAGN_SCD_TRANSLATE_TBL_OFFSET_QUEUE(x) \
	((IWLAGN_SCD_TRANSLATE_TBL_OFFSET + ((x) * 2)) & 0xfffc)

#define IWLAGN_SCD_QUEUECHAIN_SEL_ALL(priv)	\
	(((1<<(priv)->hw_params.max_txq_num) - 1) &\
	(~(1<<(priv)->cmd_queue)))

#define IWLAGN_SCD_BASE			(PRPH_BASE + 0xa02c00)

#define IWLAGN_SCD_SRAM_BASE_ADDR	(IWLAGN_SCD_BASE + 0x0)
#define IWLAGN_SCD_DRAM_BASE_ADDR	(IWLAGN_SCD_BASE + 0x8)
#define IWLAGN_SCD_AIT			(IWLAGN_SCD_BASE + 0x0c)
#define IWLAGN_SCD_TXFACT		(IWLAGN_SCD_BASE + 0x10)
#define IWLAGN_SCD_ACTIVE		(IWLAGN_SCD_BASE + 0x14)
#define IWLAGN_SCD_QUEUE_WRPTR(x)	(IWLAGN_SCD_BASE + 0x18 + (x) * 4)
#define IWLAGN_SCD_QUEUE_RDPTR(x)	(IWLAGN_SCD_BASE + 0x68 + (x) * 4)
#define IWLAGN_SCD_QUEUECHAIN_SEL	(IWLAGN_SCD_BASE + 0xe8)
#define IWLAGN_SCD_AGGR_SEL		(IWLAGN_SCD_BASE + 0x248)
#define IWLAGN_SCD_INTERRUPT_MASK	(IWLAGN_SCD_BASE + 0x108)
#define IWLAGN_SCD_QUEUE_STATUS_BITS(x)	(IWLAGN_SCD_BASE + 0x10c + (x) * 4)

/*********************** END TX SCHEDULER *************************************/

#endif				/* __iwl_prph_h__ */
