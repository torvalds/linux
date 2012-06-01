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

#ifndef __IWL_PHYDB_H__
#define __IWL_PHYDB_H__

#include <linux/types.h>

#define IWL_NUM_PAPD_CH_GROUPS	4
#define IWL_NUM_TXP_CH_GROUPS	8

struct iwl_phy_db_entry {
	u16	size;
	u8	*data;
};

struct iwl_shared;

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
	struct iwl_phy_db_entry	calib_ch;
	struct iwl_phy_db_entry	calib_ch_group_papd[IWL_NUM_PAPD_CH_GROUPS];
	struct iwl_phy_db_entry	calib_ch_group_txp[IWL_NUM_TXP_CH_GROUPS];

	u32 channel_num;
	u32 channel_size;

	/* for an access to the logger */
	struct device *dev;
};

enum iwl_phy_db_section_type {
	IWL_PHY_DB_CFG = 1,
	IWL_PHY_DB_CALIB_NCH,
	IWL_PHY_DB_CALIB_CH,
	IWL_PHY_DB_CALIB_CHG_PAPD,
	IWL_PHY_DB_CALIB_CHG_TXP,
	IWL_PHY_DB_MAX
};

/* for parsing of tx power channel group data that comes from the firmware*/
struct iwl_phy_db_chg_txp {
	__le32 space;
	__le16 max_channel_idx;
} __packed;

struct iwl_phy_db *iwl_phy_db_init(struct device *dev);

void iwl_phy_db_free(struct iwl_phy_db *phy_db);

int iwl_phy_db_set_section(struct iwl_phy_db *phy_db,
			   enum iwl_phy_db_section_type type, u8 *data,
			   u16 size, gfp_t alloc_ctx);

int iwl_phy_db_get_section_data(struct iwl_phy_db *phy_db,
				enum iwl_phy_db_section_type type, u8 **data,
				u16 *size, u16 ch_id);

#endif /* __IWL_PHYDB_H__ */
