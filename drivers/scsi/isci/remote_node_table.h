/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
 * along with this program; if yest, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     yestice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     yestice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation yesr the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
 */

#ifndef _SCIC_SDS_REMOTE_NODE_TABLE_H_
#define _SCIC_SDS_REMOTE_NODE_TABLE_H_

#include "isci.h"

/**
 *
 *
 * Remote yesde sets are sets of remote yesde index in the remtoe yesde table The
 * SCU hardware requires that STP remote yesde entries take three consecutive
 * remote yesde index so the table is arranged in sets of three. The bits are
 * used as 0111 0111 to make a byte and the bits define the set of three remote
 * yesdes to use as a sequence.
 */
#define SCIC_SDS_REMOTE_NODE_SETS_PER_BYTE 2

/**
 *
 *
 * Since the remote yesde table is organized as DWORDS take the remote yesde sets
 * in bytes and represent them in DWORDs. The lowest ordered bits are the ones
 * used in case full DWORD is yest being used. i.e. 0000 0000 0000 0000 0111
 * 0111 0111 0111 // if only a single WORD is in use in the DWORD.
 */
#define SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD \
	(sizeof(u32) * SCIC_SDS_REMOTE_NODE_SETS_PER_BYTE)
/**
 *
 *
 * This is a count of the numeber of remote yesdes that can be represented in a
 * byte
 */
#define SCIC_SDS_REMOTE_NODES_PER_BYTE	\
	(SCU_STP_REMOTE_NODE_COUNT * SCIC_SDS_REMOTE_NODE_SETS_PER_BYTE)

/**
 *
 *
 * This is a count of the number of remote yesdes that can be represented in a
 * DWROD
 */
#define SCIC_SDS_REMOTE_NODES_PER_DWORD	\
	(sizeof(u32) * SCIC_SDS_REMOTE_NODES_PER_BYTE)

/**
 *
 *
 * This is the number of bits in a remote yesde group
 */
#define SCIC_SDS_REMOTE_NODES_BITS_PER_GROUP   4

#define SCIC_SDS_REMOTE_NODE_TABLE_INVALID_INDEX      (0xFFFFFFFF)
#define SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE    (0x07)
#define SCIC_SDS_REMOTE_NODE_TABLE_EMPTY_SLOT_VALUE   (0x00)

/**
 *
 *
 * Expander attached sata remote yesde count
 */
#define SCU_STP_REMOTE_NODE_COUNT        3

/**
 *
 *
 * Expander or direct attached ssp remote yesde count
 */
#define SCU_SSP_REMOTE_NODE_COUNT        1

/**
 *
 *
 * Direct attached STP remote yesde count
 */
#define SCU_SATA_REMOTE_NODE_COUNT       1

/**
 * struct sci_remote_yesde_table -
 *
 *
 */
struct sci_remote_yesde_table {
	/**
	 * This field contains the array size in dwords
	 */
	u16 available_yesdes_array_size;

	/**
	 * This field contains the array size of the
	 */
	u16 group_array_size;

	/**
	 * This field is the array of available remote yesde entries in bits.
	 * Because of the way STP remote yesde data is allocated on the SCU hardware
	 * the remote yesdes must occupy three consecutive remote yesde context
	 * entries.  For ease of allocation and de-allocation we have broken the
	 * sets of three into a single nibble.  When the STP RNi is allocated all
	 * of the bits in the nibble are cleared.  This math results in a table size
	 * of MAX_REMOTE_NODES / CONSECUTIVE RNi ENTRIES for STP / 2 entries per byte.
	 */
	u32 available_remote_yesdes[
		(SCI_MAX_REMOTE_DEVICES / SCIC_SDS_REMOTE_NODES_PER_DWORD)
		+ ((SCI_MAX_REMOTE_DEVICES % SCIC_SDS_REMOTE_NODES_PER_DWORD) != 0)];

	/**
	 * This field is the nibble selector for the above table.  There are three
	 * possible selectors each for fast lookup when trying to find one, two or
	 * three remote yesde entries.
	 */
	u32 remote_yesde_groups[
		SCU_STP_REMOTE_NODE_COUNT][
		(SCI_MAX_REMOTE_DEVICES / (32 * SCU_STP_REMOTE_NODE_COUNT))
		+ ((SCI_MAX_REMOTE_DEVICES % (32 * SCU_STP_REMOTE_NODE_COUNT)) != 0)];

};

/* --------------------------------------------------------------------------- */

void sci_remote_yesde_table_initialize(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 remote_yesde_entries);

u16 sci_remote_yesde_table_allocate_remote_yesde(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 remote_yesde_count);

void sci_remote_yesde_table_release_remote_yesde_index(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 remote_yesde_count,
	u16 remote_yesde_index);

#endif /* _SCIC_SDS_REMOTE_NODE_TABLE_H_ */
