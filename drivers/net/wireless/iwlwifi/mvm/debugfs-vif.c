/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
#include "mvm.h"
#include "debugfs.h"

static ssize_t iwl_dbgfs_mac_params_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	u8 ap_sta_id;
	struct ieee80211_chanctx_conf *chanctx_conf;
	char buf[512];
	int bufsz = sizeof(buf);
	int pos = 0;
	int i;

	mutex_lock(&mvm->mutex);

	ap_sta_id = mvmvif->ap_sta_id;

	pos += scnprintf(buf+pos, bufsz-pos, "mac id/color: %d / %d\n",
			 mvmvif->id, mvmvif->color);
	pos += scnprintf(buf+pos, bufsz-pos, "bssid: %pM\n",
			 vif->bss_conf.bssid);
	pos += scnprintf(buf+pos, bufsz-pos, "QoS:\n");
	for (i = 0; i < ARRAY_SIZE(mvmvif->queue_params); i++)
		pos += scnprintf(buf+pos, bufsz-pos,
				 "\t%d: txop:%d - cw_min:%d - cw_max = %d - aifs = %d upasd = %d\n",
				 i, mvmvif->queue_params[i].txop,
				 mvmvif->queue_params[i].cw_min,
				 mvmvif->queue_params[i].cw_max,
				 mvmvif->queue_params[i].aifs,
				 mvmvif->queue_params[i].uapsd);

	if (vif->type == NL80211_IFTYPE_STATION &&
	    ap_sta_id != IWL_MVM_STATION_COUNT) {
		struct ieee80211_sta *sta;
		struct iwl_mvm_sta *mvm_sta;

		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[ap_sta_id],
						lockdep_is_held(&mvm->mutex));
		mvm_sta = (void *)sta->drv_priv;
		pos += scnprintf(buf+pos, bufsz-pos,
				 "ap_sta_id %d - reduced Tx power %d\n",
				 ap_sta_id, mvm_sta->bt_reduced_txpower);
	}

	rcu_read_lock();
	chanctx_conf = rcu_dereference(vif->chanctx_conf);
	if (chanctx_conf)
		pos += scnprintf(buf+pos, bufsz-pos,
				 "idle rx chains %d, active rx chains: %d\n",
				 chanctx_conf->rx_chains_static,
				 chanctx_conf->rx_chains_dynamic);
	rcu_read_unlock();

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

#define MVM_DEBUGFS_ADD_FILE_VIF(name, parent, mode) do {		\
		if (!debugfs_create_file(#name, mode, parent, vif,	\
					 &iwl_dbgfs_##name##_ops))	\
			goto err;					\
	} while (0)

MVM_DEBUGFS_READ_FILE_OPS(mac_params);

void iwl_mvm_vif_dbgfs_register(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct dentry *dbgfs_dir = vif->debugfs_dir;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	char buf[100];

	/*
	 * Check if debugfs directory already exist before creating it.
	 * This may happen when, for example, resetting hw or suspend-resume
	 */
	if (!dbgfs_dir || mvmvif->dbgfs_dir)
		return;

	mvmvif->dbgfs_dir = debugfs_create_dir("iwlmvm", dbgfs_dir);
	mvmvif->mvm = mvm;

	if (!mvmvif->dbgfs_dir) {
		IWL_ERR(mvm, "Failed to create debugfs directory under %s\n",
			dbgfs_dir->d_name.name);
		return;
	}

	MVM_DEBUGFS_ADD_FILE_VIF(mac_params, mvmvif->dbgfs_dir,
				 S_IRUSR);

	/*
	 * Create symlink for convenience pointing to interface specific
	 * debugfs entries for the driver. For example, under
	 * /sys/kernel/debug/iwlwifi/0000\:02\:00.0/iwlmvm/
	 * find
	 * netdev:wlan0 -> ../../../ieee80211/phy0/netdev:wlan0/iwlmvm/
	 */
	snprintf(buf, 100, "../../../%s/%s/%s/%s",
		 dbgfs_dir->d_parent->d_parent->d_name.name,
		 dbgfs_dir->d_parent->d_name.name,
		 dbgfs_dir->d_name.name,
		 mvmvif->dbgfs_dir->d_name.name);

	mvmvif->dbgfs_slink = debugfs_create_symlink(dbgfs_dir->d_name.name,
						     mvm->debugfs_dir, buf);
	if (!mvmvif->dbgfs_slink)
		IWL_ERR(mvm, "Can't create debugfs symbolic link under %s\n",
			dbgfs_dir->d_name.name);
	return;
err:
	IWL_ERR(mvm, "Can't create debugfs entity\n");
}

void iwl_mvm_vif_dbgfs_clean(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	debugfs_remove(mvmvif->dbgfs_slink);
	mvmvif->dbgfs_slink = NULL;

	debugfs_remove_recursive(mvmvif->dbgfs_dir);
	mvmvif->dbgfs_dir = NULL;
}
