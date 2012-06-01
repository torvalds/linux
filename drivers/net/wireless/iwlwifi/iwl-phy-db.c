/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2012 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2005 - 2012 Intel Corporation. All rights reserved.
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

#include "iwl-debug.h"
#include "iwl-dev.h"

#include "iwl-phy-db.h"

#define CHANNEL_NUM_SIZE	4	/* num of channels in calib_ch size */

struct iwl_phy_db *iwl_phy_db_init(struct device *dev)
{
	struct iwl_phy_db *phy_db = kzalloc(sizeof(struct iwl_phy_db),
					    GFP_KERNEL);

	if (!phy_db)
		return phy_db;

	phy_db->dev = dev;

	/* TODO: add default values of the phy db. */
	return phy_db;
}

/*
 * get phy db section: returns a pointer to a phy db section specified by
 * type and channel group id.
 */
static struct iwl_phy_db_entry *
iwl_phy_db_get_section(struct iwl_phy_db *phy_db,
		       enum iwl_phy_db_section_type type,
		       u16 chg_id)
{
	if (!phy_db || type < 0 || type >= IWL_PHY_DB_MAX)
		return NULL;

	switch (type) {
	case IWL_PHY_DB_CFG:
		return &phy_db->cfg;
	case IWL_PHY_DB_CALIB_NCH:
		return &phy_db->calib_nch;
	case IWL_PHY_DB_CALIB_CH:
		return &phy_db->calib_ch;
	case IWL_PHY_DB_CALIB_CHG_PAPD:
		if (chg_id < 0 || chg_id >= IWL_NUM_PAPD_CH_GROUPS)
			return NULL;
		return &phy_db->calib_ch_group_papd[chg_id];
	case IWL_PHY_DB_CALIB_CHG_TXP:
		if (chg_id < 0 || chg_id >= IWL_NUM_TXP_CH_GROUPS)
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
	iwl_phy_db_free_section(phy_db, IWL_PHY_DB_CALIB_CH, 0);
	for (i = 0; i < IWL_NUM_PAPD_CH_GROUPS; i++)
		iwl_phy_db_free_section(phy_db, IWL_PHY_DB_CALIB_CHG_PAPD, i);
	for (i = 0; i < IWL_NUM_TXP_CH_GROUPS; i++)
		iwl_phy_db_free_section(phy_db, IWL_PHY_DB_CALIB_CHG_TXP, i);

	kfree(phy_db);
}

int iwl_phy_db_set_section(struct iwl_phy_db *phy_db,
			   enum iwl_phy_db_section_type type, u8 *data,
			   u16 size, gfp_t alloc_ctx)
{
	struct iwl_phy_db_entry *entry;
	u16 chg_id = 0;

	if (!phy_db)
		return -EINVAL;

	if (type == IWL_PHY_DB_CALIB_CHG_PAPD ||
	    type == IWL_PHY_DB_CALIB_CHG_TXP)
		chg_id = le16_to_cpup((__le16 *)data);

	entry = iwl_phy_db_get_section(phy_db, type, chg_id);
	if (!entry)
		return -EINVAL;

	kfree(entry->data);
	entry->data = kmemdup(data, size, alloc_ctx);
	if (!entry->data) {
		entry->size = 0;
		return -ENOMEM;
	}

	entry->size = size;

	if (type == IWL_PHY_DB_CALIB_CH) {
		phy_db->channel_num = le32_to_cpup((__le32 *)data);
		phy_db->channel_size =
		      (size - CHANNEL_NUM_SIZE) / phy_db->channel_num;
	}

	return 0;
}

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

int iwl_phy_db_get_section_data(struct iwl_phy_db *phy_db,
				enum iwl_phy_db_section_type type, u8 **data,
				u16 *size, u16 ch_id)
{
	struct iwl_phy_db_entry *entry;
	u32 channel_num;
	u32 channel_size;
	u16 ch_group_id = 0;
	u16 index;

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

	if (type == IWL_PHY_DB_CALIB_CH) {
		index = ch_id_to_ch_index(ch_id);
		channel_num = phy_db->channel_num;
		channel_size = phy_db->channel_size;
		if (index >= channel_num) {
			IWL_ERR(phy_db, "Wrong channel number %d", ch_id);
			return -EINVAL;
		}
		*data = entry->data + CHANNEL_NUM_SIZE + index * channel_size;
		*size = channel_size;
	} else {
		*data = entry->data;
		*size = entry->size;
	}
	return 0;
}
