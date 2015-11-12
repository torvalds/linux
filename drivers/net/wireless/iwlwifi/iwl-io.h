/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl_io_h__
#define __iwl_io_h__

#include "iwl-devtrace.h"
#include "iwl-trans.h"

void iwl_write8(struct iwl_trans *trans, u32 ofs, u8 val);
void iwl_write32(struct iwl_trans *trans, u32 ofs, u32 val);
u32 iwl_read32(struct iwl_trans *trans, u32 ofs);

static inline void iwl_set_bit(struct iwl_trans *trans, u32 reg, u32 mask)
{
	iwl_trans_set_bits_mask(trans, reg, mask, mask);
}

static inline void iwl_clear_bit(struct iwl_trans *trans, u32 reg, u32 mask)
{
	iwl_trans_set_bits_mask(trans, reg, mask, 0);
}

int iwl_poll_bit(struct iwl_trans *trans, u32 addr,
		 u32 bits, u32 mask, int timeout);
int iwl_poll_direct_bit(struct iwl_trans *trans, u32 addr, u32 mask,
			int timeout);

u32 iwl_read_direct32(struct iwl_trans *trans, u32 reg);
void iwl_write_direct32(struct iwl_trans *trans, u32 reg, u32 value);


u32 __iwl_read_prph(struct iwl_trans *trans, u32 ofs);
u32 iwl_read_prph(struct iwl_trans *trans, u32 ofs);
void __iwl_write_prph(struct iwl_trans *trans, u32 ofs, u32 val);
void iwl_write_prph(struct iwl_trans *trans, u32 ofs, u32 val);
int iwl_poll_prph_bit(struct iwl_trans *trans, u32 addr,
		      u32 bits, u32 mask, int timeout);
void iwl_set_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask);
void iwl_set_bits_mask_prph(struct iwl_trans *trans, u32 ofs,
			    u32 bits, u32 mask);
void iwl_clear_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask);
void iwl_force_nmi(struct iwl_trans *trans);

/* Error handling */
int iwl_dump_fh(struct iwl_trans *trans, char **buf);

#endif
