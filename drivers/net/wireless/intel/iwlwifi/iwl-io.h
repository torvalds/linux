/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
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
#ifndef __iwl_io_h__
#define __iwl_io_h__

#include "iwl-devtrace.h"
#include "iwl-trans.h"

void iwl_write8(struct iwl_trans *trans, u32 ofs, u8 val);
void iwl_write32(struct iwl_trans *trans, u32 ofs, u32 val);
void iwl_write64(struct iwl_trans *trans, u64 ofs, u64 val);
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
void iwl_write_direct64(struct iwl_trans *trans, u64 reg, u64 value);


u32 iwl_read_prph_no_grab(struct iwl_trans *trans, u32 ofs);
u32 iwl_read_prph(struct iwl_trans *trans, u32 ofs);
void iwl_write_prph_no_grab(struct iwl_trans *trans, u32 ofs, u32 val);
void iwl_write_prph64_no_grab(struct iwl_trans *trans, u64 ofs, u64 val);
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
