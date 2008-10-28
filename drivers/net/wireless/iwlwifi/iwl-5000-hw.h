/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2008 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2008 Intel Corporation. All rights reserved.
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
/*
 * Please use this file (iwl-5000-hw.h) only for hardware-related definitions.
 * Use iwl-5000-commands.h for uCode API definitions.
 */

#ifndef __iwl_5000_hw_h__
#define __iwl_5000_hw_h__

#define IWL50_RTC_INST_UPPER_BOUND		(0x020000)
#define IWL50_RTC_DATA_UPPER_BOUND		(0x80C000)
#define IWL50_RTC_INST_SIZE (IWL50_RTC_INST_UPPER_BOUND - RTC_INST_LOWER_BOUND)
#define IWL50_RTC_DATA_SIZE (IWL50_RTC_DATA_UPPER_BOUND - RTC_DATA_LOWER_BOUND)

/* EERPROM */
#define IWL_5000_EEPROM_IMG_SIZE			2048


#define IWL50_MAX_WIN_SIZE                64
#define IWL50_QUEUE_SIZE                 256
#define IWL50_CMD_FIFO_NUM                 7
#define IWL50_NUM_QUEUES                  20
#define IWL50_NUM_AMPDU_QUEUES		  10
#define IWL50_FIRST_AMPDU_QUEUE		  10

#define IWL_sta_id_POS 12
#define IWL_sta_id_LEN 4
#define IWL_sta_id_SYM val

/* Fixed (non-configurable) rx data from phy */

/* Base physical address of iwl5000_shared is provided to SCD_DRAM_BASE_ADDR
 * and &iwl5000_shared.val0 is provided to FH_RSCSR_CHNL0_STTS_WPTR_REG */
struct iwl5000_sched_queue_byte_cnt_tbl {
	struct iwl4965_queue_byte_cnt_entry tfd_offset[IWL50_QUEUE_SIZE +
						       IWL50_MAX_WIN_SIZE];
} __attribute__ ((packed));

struct iwl5000_shared {
	struct iwl5000_sched_queue_byte_cnt_tbl
	 queues_byte_cnt_tbls[IWL50_NUM_QUEUES];
	__le32 rb_closed;

	/* __le32 rb_closed_stts_rb_num:12; */
#define IWL_rb_closed_stts_rb_num_POS 0
#define IWL_rb_closed_stts_rb_num_LEN 12
#define IWL_rb_closed_stts_rb_num_SYM rb_closed
	/* __le32 rsrv1:4; */
	/* __le32 rb_closed_stts_rx_frame_num:12; */
#define IWL_rb_closed_stts_rx_frame_num_POS 16
#define IWL_rb_closed_stts_rx_frame_num_LEN 12
#define IWL_rb_closed_stts_rx_frame_num_SYM rb_closed
	/* __le32 rsrv2:4; */

	__le32 frm_finished;
	/* __le32 frame_finished_stts_rb_num:12; */
#define IWL_frame_finished_stts_rb_num_POS 0
#define IWL_frame_finished_stts_rb_num_LEN 12
#define IWL_frame_finished_stts_rb_num_SYM frm_finished
	/* __le32 rsrv3:4; */
	/* __le32 frame_finished_stts_rx_frame_num:12; */
#define IWL_frame_finished_stts_rx_frame_num_POS 16
#define IWL_frame_finished_stts_rx_frame_num_LEN 12
#define IWL_frame_finished_stts_rx_frame_num_SYM frm_finished
	/* __le32 rsrv4:4; */

	__le32 padding1;  /* so that allocation will be aligned to 16B */
	__le32 padding2;
} __attribute__ ((packed));

/* calibrations defined for 5000 */
/* defines the order in which results should be sent to the runtime uCode */
enum iwl5000_calib {
	IWL5000_CALIB_LO,
	IWL5000_CALIB_TX_IQ,
	IWL5000_CALIB_TX_IQ_PERD,
};

#endif /* __iwl_5000_hw_h__ */

