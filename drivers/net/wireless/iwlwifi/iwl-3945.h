/******************************************************************************
 *
 * Copyright(c) 2003 - 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl_3945_h__
#define __iwl_3945_h__

/*
 * Forward declare iwl-3945.c functions for iwl-base.c
 */
extern int iwl_eeprom_aqcuire_semaphore(struct iwl_priv *priv);
extern __le32 iwl3945_get_antenna_flags(const struct iwl_priv *priv);
extern int iwl3945_init_hw_rate_table(struct iwl_priv *priv);
extern void iwl3945_reg_txpower_periodic(struct iwl_priv *priv);
extern void iwl3945_bg_reg_txpower_periodic(struct work_struct *work);
extern int iwl3945_txpower_set_from_eeprom(struct iwl_priv *priv);
extern u8 iwl3945_sync_sta(struct iwl_priv *priv, int sta_id,
		 u16 tx_rate, u8 flags);
#endif
