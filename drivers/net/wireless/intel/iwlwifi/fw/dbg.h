/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
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
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
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

#ifndef __iwl_fw_dbg_h__
#define __iwl_fw_dbg_h__
#include <linux/workqueue.h>
#include <net/cfg80211.h>
#include "runtime.h"
#include "iwl-prph.h"
#include "iwl-io.h"
#include "file.h"
#include "error-dump.h"

/**
 * struct iwl_fw_dump_desc - describes the dump
 * @len: length of trig_desc->data
 * @trig_desc: the description of the dump
 */
struct iwl_fw_dump_desc {
	size_t len;
	/* must be last */
	struct iwl_fw_error_dump_trigger_desc trig_desc;
};

extern const struct iwl_fw_dump_desc iwl_dump_desc_assert;

static inline void iwl_fw_free_dump_desc(struct iwl_fw_runtime *fwrt)
{
	if (fwrt->dump.desc != &iwl_dump_desc_assert)
		kfree(fwrt->dump.desc);
	fwrt->dump.desc = NULL;
}

void iwl_fw_error_dump(struct iwl_fw_runtime *fwrt);
int iwl_fw_dbg_collect_desc(struct iwl_fw_runtime *fwrt,
			    const struct iwl_fw_dump_desc *desc,
			    const struct iwl_fw_dbg_trigger_tlv *trigger);
int iwl_fw_dbg_collect(struct iwl_fw_runtime *fwrt,
		       enum iwl_fw_dbg_trigger trig,
		       const char *str, size_t len,
		       const struct iwl_fw_dbg_trigger_tlv *trigger);
int iwl_fw_dbg_collect_trig(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_dbg_trigger_tlv *trigger,
			    const char *fmt, ...) __printf(3, 4);
int iwl_fw_start_dbg_conf(struct iwl_fw_runtime *fwrt, u8 id);

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
			     struct wireless_dev *wdev)
{
	u32 trig_vif = le32_to_cpu(trig->vif_type);

	return trig_vif == IWL_FW_DBG_CONF_VIF_ANY ||
	       wdev->iftype == trig_vif;
}

static inline bool
iwl_fw_dbg_trigger_stop_conf_match(struct iwl_fw_runtime *fwrt,
				   struct iwl_fw_dbg_trigger_tlv *trig)
{
	return ((trig->mode & IWL_FW_DBG_TRIGGER_STOP) &&
		(fwrt->dump.conf == FW_DBG_INVALID ||
		(BIT(fwrt->dump.conf) & le32_to_cpu(trig->stop_conf_ids))));
}

static inline bool
iwl_fw_dbg_no_trig_window(struct iwl_fw_runtime *fwrt,
			  struct iwl_fw_dbg_trigger_tlv *trig)
{
	unsigned long wind_jiff =
		msecs_to_jiffies(le16_to_cpu(trig->trig_dis_ms));
	u32 id = le32_to_cpu(trig->id);

	/* If this is the first event checked, jump to update start ts */
	if (fwrt->dump.non_collect_ts_start[id] &&
	    (time_after(fwrt->dump.non_collect_ts_start[id] + wind_jiff,
			jiffies)))
		return true;

	fwrt->dump.non_collect_ts_start[id] = jiffies;
	return false;
}

static inline bool
iwl_fw_dbg_trigger_check_stop(struct iwl_fw_runtime *fwrt,
			      struct wireless_dev *wdev,
			      struct iwl_fw_dbg_trigger_tlv *trig)
{
	if (wdev && !iwl_fw_dbg_trigger_vif_match(trig, wdev))
		return false;

	if (iwl_fw_dbg_no_trig_window(fwrt, trig)) {
		IWL_WARN(fwrt, "Trigger %d occurred while no-collect window.\n",
			 trig->id);
		return false;
	}

	return iwl_fw_dbg_trigger_stop_conf_match(fwrt, trig);
}

static inline void
_iwl_fw_dbg_trigger_simple_stop(struct iwl_fw_runtime *fwrt,
				struct wireless_dev *wdev,
				struct iwl_fw_dbg_trigger_tlv *trigger)
{
	if (!trigger)
		return;

	if (!iwl_fw_dbg_trigger_check_stop(fwrt, wdev, trigger))
		return;

	iwl_fw_dbg_collect_trig(fwrt, trigger, NULL);
}

#define iwl_fw_dbg_trigger_simple_stop(fwrt, wdev, trig)	\
	_iwl_fw_dbg_trigger_simple_stop((fwrt), (wdev),		\
					iwl_fw_dbg_get_trigger((fwrt)->fw,\
							       (trig)))

static inline void iwl_fw_dbg_stop_recording(struct iwl_fw_runtime *fwrt)
{
	if (fwrt->trans->cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		iwl_set_bits_prph(fwrt->trans, MON_BUFF_SAMPLE_CTL, 0x100);
	} else {
		iwl_write_prph(fwrt->trans, DBGC_IN_SAMPLE, 0);
		udelay(100);
		iwl_write_prph(fwrt->trans, DBGC_OUT_CTRL, 0);
	}
}

static inline void iwl_fw_dump_conf_clear(struct iwl_fw_runtime *fwrt)
{
	iwl_fw_dbg_stop_recording(fwrt);

	fwrt->dump.conf = FW_DBG_INVALID;
}

void iwl_fw_error_dump_wk(struct work_struct *work);

static inline void iwl_fw_flush_dump(struct iwl_fw_runtime *fwrt)
{
	flush_delayed_work(&fwrt->dump.wk);
}

static inline void iwl_fw_cancel_dump(struct iwl_fw_runtime *fwrt)
{
	cancel_delayed_work_sync(&fwrt->dump.wk);
}

#endif  /* __iwl_fw_dbg_h__ */
