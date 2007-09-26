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
 *****************************************************************************/

#ifndef	__iwl_prph_h__
#define __iwl_prph_h__


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

#define APMG_CLK_VAL_DMA_CLK_RQT	(0x00000200)
#define APMG_CLK_VAL_BSM_CLK_RQT	(0x00000800)

#define APMG_PS_CTRL_VAL_RESET_REQ	(0x04000000)

#define APMG_PCIDEV_STT_VAL_L1_ACT_DIS	(0x00000800)

#define APMG_PS_CTRL_MSK_PWR_SRC              (0x03000000)
#define APMG_PS_CTRL_VAL_PWR_SRC_VMAIN        (0x00000000)
#define APMG_PS_CTRL_VAL_PWR_SRC_VAUX         (0x01000000)


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
 *     bytecount register ("1" in MSbit tells initialization uCode to load
 *     the runtime uCode):
 *     BSM_DRAM_INST_BYTECOUNT_REG = bytecount | BSM_DRAM_INST_LOAD
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


#endif				/* __iwl_prph_h__ */
