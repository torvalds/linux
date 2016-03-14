/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/export.h>

#include "iwl-drv.h"
#include "iwl-phy-db.h"
#include "iwl-debug.h"
#include "iwl-op-mode.h"
#include "iwl-trans.h"

#define CHANNEL_NUM_SIZE	4	/* num of channels in calib_ch size */
#define IWL_NUM_PAPD_CH_GROUPS	9
#define IWL_NUM_TXP_CH_GROUPS	9

struct iwl_phy_db_entry {
	u16	size;
	u8	*data;
};

/**
 * struct iwl_phy_db - stores phy configuration and calibration data.
 *
 * @cfg: phy configuration.
 * @calib_nch: non channel specific calibration data.
 * @calib_ch: channel specific calibration data.
 * @calib_ch_group_papd: calibration data related to papd channel group.
 * @calib_ch_group_txp: calibration data related to tx power chanel group.
 */
struct iwl_phy_db {
	struct iwl_phy_db_entry	cfg;
	struct iwl_phy_db_entry	calib_nch;
	struct iwl_phy_db_entry	calib_ch_group_papd[IWL_NUM_PAPD_CH_GROUPS];
	struct iwl_phy_db_entry	calib_ch_group_txp[IWL_NUM_TXP_CH_GROUPS];

	struct iwl_trans *trans;
};

enum iwl_phy_db_section_type {
	IWL_PHY_DB_CFG = 1,
	IWL_PHY_DB_CALIB_NCH,
	IWL_PHY_DB_UNUSED,
	IWL_PHY_DB_CALIB_CHG_PAPD,
	IWL_PHY_DB_CALIB_CHG_TXP,
	IWL_PHY_DB_MAX
};

#define PHY_DB_CMD 0x6c /* TEMP API - The actual is 0x8c */

/*
 * phy db - configure operational ucode
 */
struct iwl_phy_db_cmd {
	__le16 type;
	__le16 length;
	u8 data[];
} __packed;

/* for parsing of tx power channel group data that comes from the firmware*/
struct iwl_phy_db_chg_txp {
	__le32 space;
	__le16 max_channel_idx;
} __packed;

/*
 * phy db - Receive phy db chunk after calibrations
 */
struct iwl_calib_res_notif_phy_db {
	__le16 type;
	__le16 length;
	u8 data[];
} __packed;

struct iwl_phy_db *iwl_phy_db_init(struct iwl_trans *trans)
{
	struct iwl_phy_db *phy_db = kzalloc(sizeof(struct iwl_phy_db),
					    GFP_KERNEL);

	if (!phy_db)
		return phy_db;

	phy_db->trans = trans;

	/* TODO: add default values of the phy db. */
	return phy_db;
}
IWL_EXPORT_SYMBOL(iwl_phy_db_init);

/*
 * get phy db section: returns a pointer to a phy db section specified by
 * type and channel group id.
 */
static struct iwl_phy_db_entry *
iwl_phy_db_get_section(struct iwl_phy_db *phy_db,
		       enum iwl_phy_db_section_type type,
		       u16 chg_id)
{
	if (!phy_db || type >= IWL_PHY_DB_MAX)
		return NULL;

	switch (type) {
	case IWL_PHY_DB_CFG:
		return &phy_db->cfg;
	case IWL_PHY_DB_CALIB_NCH:
		return &phy_db->calib_nch;
	case IWL_PHY_DB_CALIB_CHG_PAPD:
		if (chg_id >= IWL_NUM_PAPD_CH_GROUPS)
			return NULL;
		return &phy_db->calib_ch_group_papd[chg_id];
	case IWL_PHY_DB_CALIB_CHG_TXP:
		if (chg_id >= IWL_NUM_TXP_CH_GROUPS)
			return NULL;
		return &phy_db->calib_ch_group_txp[chg_id];
	default:
		return NULL;
	}
	return NULL;
}

static void iwl_phy_db_free_section(struct iwl_phy_db *phy_db,
				    enum iwl_phy_db_section_type type,
				    u16 chg_id)
{
	struct iwl_phy_db_entry *entry =
				iwl_phy_db_get_section(phy_db, type, chg_id);
	if (!entry)
		return;

	kfree(entry->data);
	entry->data = NULL;
	entry->size = 0;
}

void iwl_phy_db_free(struct iwl_phy_db *phy_db)
{
	int i;

	if (!phy_db)
		return;

	iwl_phy_db_free_section(phy_db, IWL_PHY_DB_CFG, 0);
	iwl_phy_db_free_section(phy_db, IWL_PHY_DB_CALIB_NCH, 0);
	for (i = 0; i < IWL_NUM_PAPD_CH_GROUPS; i++)
		iwl_phy_db_free_section(phy_db, IWL_PHY_DB_CALIB_CHG_PAPD, i);
	for (i = 0; i < IWL_NUM_TXP_CH_GROUPS; i++)
		iwl_phy_db_free_section(phy_db, IWL_PHY_DB_CALIB_CHG_TXP, i);

	kfree(phy_db);
}
IWL_EXPORT_SYMBOL(iwl_phy_db_free);

int iwl_phy_db_set_section(struct iwl_phy_db *phy_db, struct iwl_rx_packet *pkt,
			   gfp_t alloc_ctx)
{
	struct iwl_calib_res_notif_phy_db *phy_db_notif =
			(struct iwl_calib_res_notif_phy_db *)pkt->data;
	enum iwl_phy_db_section_type type = le16_to_cpu(phy_db_notif->type);
	u16 size  = le16_to_cpu(phy_db_notif->length);
	struct iwl_phy_db_entry *entry;
	u16 chg_id = 0;

	if (!phy_db)
		return -EINVAL;

	if (type == IWL_PHY_DB_CALIB_CHG_PAPD ||
	    type == IWL_PHY_DB_CALIB_CHG_TXP)
		chg_id = le16_to_cpup((__le16 *)phy_db_notif->data);

	entry = iwl_phy_db_get_section(phy_db, type, chg_id);
	if (!entry)
		return -EINVAL;

	kfree(entry->data);
	entry->data = kmemdup(phy_db_notif->data, size, alloc_ctx);
	if (!entry->data) {
		entry->size = 0;
		return -ENOMEM;
	}

	entry->size = size;

	IWL_DEBUG_INFO(phy_db->trans,
		       "%s(%d): [PHYDB]SET: Type %d , Size: %d\n",
		       __func__, __LINE__, type, size);

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_phy_db_set_section);

static int is_valid_channel(u16 ch_id)
{
	if (ch_id <= 14 ||
	    (36 <= ch_id && ch_id <= 64 && ch_id % 4 == 0) ||
	    (100 <= ch_id && ch_id <= 140 && ch_id % 4 == 0) ||
	    (145 <= ch_id && ch_id <= 165 && ch_id % 4 == 1))
		return 1;
	return 0;
}

static u8 ch_id_to_ch_index(u16 ch_id)
{
	if (WARN_ON(!is_valid_channel(ch_id)))
		return 0xff;

	if (ch_id <= 14)
		return ch_id - 1;
	if (ch_id <= 64)
		return (ch_id + 20) / 4;
	if (ch_id <= 140)
		return (ch_id - 12) / 4;
	return (ch_id - 13) / 4;
}


static u16 channel_id_to_papd(u16 ch_id)
{
	if (WARN_ON(!is_valid_channel(ch_id)))
		return 0xff;

	if (1 <= ch_id && ch_id <= 14)
		return 0;
	if (36 <= ch_id && ch_id <= 64)
		return 1;
	if (100 <= ch_id && ch_id <= 140)
		return 2;
	return 3;
}

static u16 channel_id_to_txp(struct iwl_phy_db *phy_db, u16 ch_id)
{
	struct iwl_phy_db_chg_txp *txp_chg;
	int i;
	u8 ch_index = ch_id_to_ch_index(ch_id);
	if (ch_index == 0xff)
		return 0xff;

	for (i = 0; i < IWL_NUM_TXP_CH_GROUPS; i++) {
		txp_chg = (void *)phy_db->calib_ch_group_txp[i].data;
		if (!txp_chg)
			return 0xff;
		/*
		 * Looking for the first channel group that its max channel is
		 * higher then wanted channel.
		 */
		if (le16_to_cpu(txp_chg->max_channel_idx) >= ch_index)
			return i;
	}
	return 0xff;
}
static
int iwl_phy_db_get_section_data(struct iwl_phy_db *phy_db,
				u32 type, u8 **data, u16 *size, u16 ch_id)
{
	struct iwl_phy_db_entry *entry;
	u16 ch_group_id = 0;

	if (!phy_db)
		return -EINVAL;

	/* find wanted channel group */
	if (type == IWL_PHY_DB_CALIB_CHG_PAPD)
		ch_group_id = channel_id_to_papd(ch_id);
	else if (type == IWL_PHY_DB_CALIB_CHG_TXP)
		ch_group_id = channel_id_to_txp(phy_db, ch_id);

	entry = iwl_phy_db_get_section(phy_db, type, ch_group_id);
	if (!entry)
		return -EINVAL;

	*data = entry->data;
	*size = entry->size;

	IWL_DEBUG_INFO(phy_db->trans,
		       "%s(%d): [PHYDB] GET: Type %d , Size: %d\n",
		       __func__, __LINE__, type, *size);

	return 0;
}

static int iwl_send_phy_db_cmd(struct iwl_phy_db *phy_db, u16 type,
			       u16 length, void *data)
{
	struct iwl_phy_db_cmd phy_db_cmd;
	struct iwl_host_cmd cmd = {
		.id = PHY_DB_CMD,
	};

	IWL_DEBUG_INFO(phy_db->trans,
		       "Sending PHY-DB hcmd of type %d, of length %d\n",
		       type, length);

	/* Set phy db cmd variables */
	phy_db_cmd.type = cpu_to_le16(type);
	phy_db_cmd.length = cpu_to_le16(length);

	/* Set hcmd variables */
	cmd.data[0] = &phy_db_cmd;
	cmd.len[0] = sizeof(struct iwl_phy_db_cmd);
	cmd.data[1] = data;
	cmd.len[1] = length;
	cmd.dataflags[1] = IWL_HCMD_DFL_NOCOPY;

	return iwl_trans_send_cmd(phy_db->trans, &cmd);
}

static int iwl_phy_db_send_all_channel_groups(
					struct iwl_phy_db *phy_db,
					enum iwl_phy_db_section_type type,
					u8 max_ch_groups)
{
	u16 i;
	int err;
	struct iwl_phy_db_entry *entry;

	/* Send all the  channel specific groups to operational fw */
	for (i = 0; i < max_ch_groups; i++) {
		entry = iwl_phy_db_get_section(phy_db,
					       type,
					       i);
		if (!entry)
			return -EINVAL;

		if (!entry->size)
			continue;

		/* Send the requested PHY DB section */
		err = iwl_send_phy_db_cmd(phy_db,
					  type,
					  entry->size,
					  entry->data);
		if (err) {
			IWL_ERR(phy_db->trans,
				"Can't SEND phy_db section %d (%d), err %d\n",
				type, i, err);
			return err;
		}

		IWL_DEBUG_INFO(phy_db->trans,
			       "Sent PHY_DB HCMD, type = %d num = %d\n",
			       type, i);
	}

	return 0;
}

int iwl_send_phy_db_data(struct iwl_phy_db *phy_db)
{
	u8 *data = NULL;
	u16 size = 0;
	int err;

	IWL_DEBUG_INFO(phy_db->trans,
		       "Sending phy db data and configuration to runtime image\n");

	/* Send PHY DB CFG section */
	err = iwl_phy_db_get_section_data(phy_db, IWL_PHY_DB_CFG,
					  &data, &size, 0);
	if (err) {
		IWL_ERR(phy_db->trans, "Cannot get Phy DB cfg section\n");
		return err;
	}

	err = iwl_send_phy_db_cmd(phy_db, IWL_PHY_DB_CFG, size, data);
	if (err) {
		IWL_ERR(phy_db->trans,
			"Cannot send HCMD of  Phy DB cfg section\n");
		return err;
	}

	err = iwl_phy_db_get_section_data(phy_db, IWL_PHY_DB_CALIB_NCH,
					  &data, &size, 0);
	if (err) {
		IWL_ERR(phy_db->trans,
			"Cannot get Phy DB non specific channel section\n");
		return err;
	}

	err = iwl_send_phy_db_cmd(phy_db, IWL_PHY_DB_CALIB_NCH, size, data);
	if (err) {
		IWL_ERR(phy_db->trans,
			"Cannot send HCMD of Phy DB non specific channel section\n");
		return err;
	}

	/* Send all the TXP channel specific data */
	err = iwl_phy_db_send_all_channel_groups(phy_db,
						 IWL_PHY_DB_CALIB_CHG_PAPD,
						 IWL_NUM_PAPD_CH_GROUPS);
	if (err) {
		IWL_ERR(phy_db->trans,
			"Cannot send channel specific PAPD groups\n");
		return err;
	}

	/* Send all the TXP channel specific data */
	err = iwl_phy_db_send_all_channel_groups(phy_db,
						 IWL_PHY_DB_CALIB_CHG_TXP,
						 IWL_NUM_TXP_CH_GROUPS);
	if (err) {
		IWL_ERR(phy_db->trans,
			"Cannot send channel specific TX power groups\n");
		return err;
	}

	IWL_DEBUG_INFO(phy_db->trans,
		       "Finished sending phy db non channel data\n");
	return 0;
}
IWL_EXPORT_SYMBOL(iwl_send_phy_db_data);
