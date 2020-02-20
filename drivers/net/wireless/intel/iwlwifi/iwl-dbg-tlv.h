/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright (C) 2018 - 2019 Intel Corporation
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
 * along with this program.
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
 * Copyright (C) 2018 - 2019 Intel Corporation
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
#ifndef __iwl_dbg_tlv_h__
#define __iwl_dbg_tlv_h__

#include <linux/device.h>
#include <linux/types.h>

/**
 * struct iwl_dbg_tlv_node - debug TLV node
 * @list: list of &struct iwl_dbg_tlv_node
 * @tlv: debug TLV
 */
struct iwl_dbg_tlv_node {
	struct list_head list;
	struct iwl_ucode_tlv tlv;
};

/**
 * union iwl_dbg_tlv_tp_data - data that is given in a time point
 * @fw_pkt: a packet received from the FW
 */
union iwl_dbg_tlv_tp_data {
	struct iwl_rx_packet *fw_pkt;
};

/**
 * struct iwl_dbg_tlv_time_point_data
 * @trig_list: list of triggers
 * @active_trig_list: list of active triggers
 * @hcmd_list: list of host commands
 */
struct iwl_dbg_tlv_time_point_data {
	struct list_head trig_list;
	struct list_head active_trig_list;
	struct list_head hcmd_list;
};

struct iwl_trans;
struct iwl_fw_runtime;

void iwl_dbg_tlv_load_bin(struct device *dev, struct iwl_trans *trans);
void iwl_dbg_tlv_free(struct iwl_trans *trans);
void iwl_dbg_tlv_alloc(struct iwl_trans *trans, struct iwl_ucode_tlv *tlv,
		       bool ext);
void iwl_dbg_tlv_init(struct iwl_trans *trans);
void iwl_dbg_tlv_time_point(struct iwl_fw_runtime *fwrt,
			    enum iwl_fw_ini_time_point tp_id,
			    union iwl_dbg_tlv_tp_data *tp_data);
int iwl_dbg_tlv_gen_active_trigs(struct iwl_fw_runtime *fwrt, u32 new_domain);
void iwl_dbg_tlv_del_timers(struct iwl_trans *trans);

#endif /* __iwl_dbg_tlv_h__*/
