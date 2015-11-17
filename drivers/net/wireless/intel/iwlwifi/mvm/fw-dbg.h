/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015        Intel Deutschland GmbH
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
 * along with this program;
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
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015        Intel Deutschland GmbH
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

#ifndef __mvm_fw_dbg_h__
#define __mvm_fw_dbg_h__
#include "iwl-fw-file.h"
#include "iwl-fw-error-dump.h"
#include "mvm.h"

void iwl_mvm_fw_error_dump(struct iwl_mvm *mvm);
void iwl_mvm_free_fw_dump_desc(struct iwl_mvm *mvm);
int iwl_mvm_fw_dbg_collect_desc(struct iwl_mvm *mvm,
				struct iwl_mvm_dump_desc *desc,
				struct iwl_fw_dbg_trigger_tlv *trigger);
int iwl_mvm_fw_dbg_collect(struct iwl_mvm *mvm, enum iwl_fw_dbg_trigger trig,
			   const char *str, size_t len,
			   struct iwl_fw_dbg_trigger_tlv *trigger);
int iwl_mvm_fw_dbg_collect_trig(struct iwl_mvm *mvm,
				struct iwl_fw_dbg_trigger_tlv *trigger,
				const char *fmt, ...) __printf(3, 4);
int iwl_mvm_start_fw_dbg_conf(struct iwl_mvm *mvm, u8 id);

#define iwl_fw_dbg_trigger_enabled(fw, id) ({			\
	void *__dbg_trigger = (fw)->dbg_trigger_tlv[(id)];	\
	unlikely(__dbg_trigger);				\
})

static inline struct iwl_fw_dbg_trigger_tlv*
_iwl_fw_dbg_get_trigger(const struct iwl_fw *fw, enum iwl_fw_dbg_trigger id)
{
	return fw->dbg_trigger_tlv[id];
}

#define iwl_fw_dbg_get_trigger(fw, id) ({			\
	BUILD_BUG_ON(!__builtin_constant_p(id));		\
	BUILD_BUG_ON((id) >= FW_DBG_TRIGGER_MAX);		\
	_iwl_fw_dbg_get_trigger((fw), (id));			\
})

static inline bool
iwl_fw_dbg_trigger_vif_match(struct iwl_fw_dbg_trigger_tlv *trig,
			     struct ieee80211_vif *vif)
{
	u32 trig_vif = le32_to_cpu(trig->vif_type);

	return trig_vif == IWL_FW_DBG_CONF_VIF_ANY || vif->type == trig_vif;
}

static inline bool
iwl_fw_dbg_trigger_stop_conf_match(struct iwl_mvm *mvm,
				   struct iwl_fw_dbg_trigger_tlv *trig)
{
	return ((trig->mode & IWL_FW_DBG_TRIGGER_STOP) &&
		(mvm->fw_dbg_conf == FW_DBG_INVALID ||
		(BIT(mvm->fw_dbg_conf) & le32_to_cpu(trig->stop_conf_ids))));
}

static inline bool
iwl_fw_dbg_trigger_check_stop(struct iwl_mvm *mvm,
			      struct ieee80211_vif *vif,
			      struct iwl_fw_dbg_trigger_tlv *trig)
{
	if (vif && !iwl_fw_dbg_trigger_vif_match(trig, vif))
		return false;

	return iwl_fw_dbg_trigger_stop_conf_match(mvm, trig);
}

static inline void
_iwl_fw_dbg_trigger_simple_stop(struct iwl_mvm *mvm,
				struct ieee80211_vif *vif,
				struct iwl_fw_dbg_trigger_tlv *trigger)
{
	if (!trigger)
		return;

	if (!iwl_fw_dbg_trigger_check_stop(mvm, vif, trigger))
		return;

	iwl_mvm_fw_dbg_collect_trig(mvm, trigger, NULL);
}

#define iwl_fw_dbg_trigger_simple_stop(mvm, vif, trig)	\
	_iwl_fw_dbg_trigger_simple_stop((mvm), (vif),	\
					iwl_fw_dbg_get_trigger((mvm)->fw,\
							       (trig)))

#endif  /* __mvm_fw_dbg_h__ */
