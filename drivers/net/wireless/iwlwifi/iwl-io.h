/******************************************************************************
 *
 * Copyright(c) 2003 - 2012 Intel Corporation. All rights reserved.
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

static inline void iwl_write8(struct iwl_trans *trans, u32 ofs, u8 val)
{
	trace_iwlwifi_dev_iowrite8(trans->dev, ofs, val);
	iwl_trans_write8(trans, ofs, val);
}

static inline void iwl_write32(struct iwl_trans *trans, u32 ofs, u32 val)
{
	trace_iwlwifi_dev_iowrite32(trans->dev, ofs, val);
	iwl_trans_write32(trans, ofs, val);
}

static inline u32 iwl_read32(struct iwl_trans *trans, u32 ofs)
{
	u32 val = iwl_trans_read32(trans, ofs);
	trace_iwlwifi_dev_ioread32(trans->dev, ofs, val);
	return val;
}

void iwl_set_bit(struct iwl_trans *trans, u32 reg, u32 mask);
void iwl_clear_bit(struct iwl_trans *trans, u32 reg, u32 mask);

int iwl_poll_bit(struct iwl_trans *trans, u32 addr,
		 u32 bits, u32 mask, int timeout);
int iwl_poll_direct_bit(struct iwl_trans *trans, u32 addr, u32 mask,
			int timeout);

int iwl_grab_nic_access_silent(struct iwl_trans *trans);
bool iwl_grab_nic_access(struct iwl_trans *trans);
void iwl_release_nic_access(struct iwl_trans *trans);

u32 iwl_read_direct32(struct iwl_trans *trans, u32 reg);
void iwl_write_direct32(struct iwl_trans *trans, u32 reg, u32 value);


u32 iwl_read_prph(struct iwl_trans *trans, u32 reg);
void iwl_write_prph(struct iwl_trans *trans, u32 addr, u32 val);
void iwl_set_bits_prph(struct iwl_trans *trans, u32 reg, u32 mask);
void iwl_set_bits_mask_prph(struct iwl_trans *trans, u32 reg,
			    u32 bits, u32 mask);
void iwl_clear_bits_prph(struct iwl_trans *trans, u32 reg, u32 mask);

void _iwl_read_targ_mem_words(struct iwl_trans *trans, u32 addr,
			      void *buf, int words);

#define iwl_read_targ_mem_words(trans, addr, buf, bufsize)	\
	do {							\
		BUILD_BUG_ON((bufsize) % sizeof(u32));		\
		_iwl_read_targ_mem_words(trans, addr, buf,	\
					 (bufsize) / sizeof(u32));\
	} while (0)

int _iwl_write_targ_mem_words(struct iwl_trans *trans, u32 addr,
			      void *buf, int words);

u32 iwl_read_targ_mem(struct iwl_trans *trans, u32 addr);
int iwl_write_targ_mem(struct iwl_trans *trans, u32 addr, u32 val);
#endif
