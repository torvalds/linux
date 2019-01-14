/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Mobile Communications GmbH
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
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Mobile Communications GmbH
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

#ifndef __iwl_scd_h__
#define __iwl_scd_h__

#include "iwl-trans.h"
#include "iwl-io.h"
#include "iwl-prph.h"


static inline void iwl_scd_txq_set_chain(struct iwl_trans *trans,
					 u16 txq_id)
{
	iwl_set_bits_prph(trans, SCD_QUEUECHAIN_SEL, BIT(txq_id));
}

static inline void iwl_scd_txq_enable_agg(struct iwl_trans *trans,
					  u16 txq_id)
{
	iwl_set_bits_prph(trans, SCD_AGGR_SEL, BIT(txq_id));
}

static inline void iwl_scd_txq_disable_agg(struct iwl_trans *trans,
					   u16 txq_id)
{
	iwl_clear_bits_prph(trans, SCD_AGGR_SEL, BIT(txq_id));
}

static inline void iwl_scd_disable_agg(struct iwl_trans *trans)
{
	iwl_set_bits_prph(trans, SCD_AGGR_SEL, 0);
}

static inline void iwl_scd_activate_fifos(struct iwl_trans *trans)
{
	iwl_write_prph(trans, SCD_TXFACT, IWL_MASK(0, 7));
}

static inline void iwl_scd_deactivate_fifos(struct iwl_trans *trans)
{
	iwl_write_prph(trans, SCD_TXFACT, 0);
}

static inline void iwl_scd_enable_set_active(struct iwl_trans *trans,
					     u32 value)
{
	iwl_write_prph(trans, SCD_EN_CTRL, value);
}

static inline unsigned int SCD_QUEUE_WRPTR(unsigned int chnl)
{
	if (chnl < 20)
		return SCD_BASE + 0x18 + chnl * 4;
	WARN_ON_ONCE(chnl >= 32);
	return SCD_BASE + 0x284 + (chnl - 20) * 4;
}

static inline unsigned int SCD_QUEUE_RDPTR(unsigned int chnl)
{
	if (chnl < 20)
		return SCD_BASE + 0x68 + chnl * 4;
	WARN_ON_ONCE(chnl >= 32);
	return SCD_BASE + 0x2B4 + chnl * 4;
}

static inline unsigned int SCD_QUEUE_STATUS_BITS(unsigned int chnl)
{
	if (chnl < 20)
		return SCD_BASE + 0x10c + chnl * 4;
	WARN_ON_ONCE(chnl >= 32);
	return SCD_BASE + 0x334 + chnl * 4;
}

static inline void iwl_scd_txq_set_inactive(struct iwl_trans *trans,
					    u16 txq_id)
{
	iwl_write_prph(trans, SCD_QUEUE_STATUS_BITS(txq_id),
		       (0 << SCD_QUEUE_STTS_REG_POS_ACTIVE)|
		       (1 << SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));
}

#endif
