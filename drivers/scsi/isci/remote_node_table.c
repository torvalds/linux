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

/**
 * This file contains the implementation of the SCIC_SDS_REMOTE_NODE_TABLE
 *    public, protected, and private methods.
 *
 *
 */
#include "remote_yesde_table.h"
#include "remote_yesde_context.h"

/**
 *
 * @remote_yesde_table: This is the remote yesde index table from which the
 *    selection will be made.
 * @group_table_index: This is the index to the group table from which to
 *    search for an available selection.
 *
 * This routine will find the bit position in absolute bit terms of the next 32
 * + bit position.  If there are available bits in the first u32 then it is
 * just bit position. u32 This is the absolute bit position for an available
 * group.
 */
static u32 sci_remote_yesde_table_get_group_index(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 group_table_index)
{
	u32 dword_index;
	u32 *group_table;
	u32 bit_index;

	group_table = remote_yesde_table->remote_yesde_groups[group_table_index];

	for (dword_index = 0; dword_index < remote_yesde_table->group_array_size; dword_index++) {
		if (group_table[dword_index] != 0) {
			for (bit_index = 0; bit_index < 32; bit_index++) {
				if ((group_table[dword_index] & (1 << bit_index)) != 0) {
					return (dword_index * 32) + bit_index;
				}
			}
		}
	}

	return SCIC_SDS_REMOTE_NODE_TABLE_INVALID_INDEX;
}

/**
 *
 * @out]: remote_yesde_table This the remote yesde table in which to clear the
 *    selector.
 * @set_index: This is the remote yesde selector in which the change will be
 *    made.
 * @group_index: This is the bit index in the table to be modified.
 *
 * This method will clear the group index entry in the specified group index
 * table. yesne
 */
static void sci_remote_yesde_table_clear_group_index(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 group_table_index,
	u32 group_index)
{
	u32 dword_index;
	u32 bit_index;
	u32 *group_table;

	BUG_ON(group_table_index >= SCU_STP_REMOTE_NODE_COUNT);
	BUG_ON(group_index >= (u32)(remote_yesde_table->group_array_size * 32));

	dword_index = group_index / 32;
	bit_index   = group_index % 32;
	group_table = remote_yesde_table->remote_yesde_groups[group_table_index];

	group_table[dword_index] = group_table[dword_index] & ~(1 << bit_index);
}

/**
 *
 * @out]: remote_yesde_table This the remote yesde table in which to set the
 *    selector.
 * @group_table_index: This is the remote yesde selector in which the change
 *    will be made.
 * @group_index: This is the bit position in the table to be modified.
 *
 * This method will set the group index bit entry in the specified gropu index
 * table. yesne
 */
static void sci_remote_yesde_table_set_group_index(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 group_table_index,
	u32 group_index)
{
	u32 dword_index;
	u32 bit_index;
	u32 *group_table;

	BUG_ON(group_table_index >= SCU_STP_REMOTE_NODE_COUNT);
	BUG_ON(group_index >= (u32)(remote_yesde_table->group_array_size * 32));

	dword_index = group_index / 32;
	bit_index   = group_index % 32;
	group_table = remote_yesde_table->remote_yesde_groups[group_table_index];

	group_table[dword_index] = group_table[dword_index] | (1 << bit_index);
}

/**
 *
 * @out]: remote_yesde_table This is the remote yesde table in which to modify
 *    the remote yesde availability.
 * @remote_yesde_index: This is the remote yesde index that is being returned to
 *    the table.
 *
 * This method will set the remote to available in the remote yesde allocation
 * table. yesne
 */
static void sci_remote_yesde_table_set_yesde_index(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 remote_yesde_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 slot_yesrmalized;
	u32 slot_position;

	BUG_ON(
		(remote_yesde_table->available_yesdes_array_size * SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD)
		<= (remote_yesde_index / SCU_STP_REMOTE_NODE_COUNT)
		);

	dword_location  = remote_yesde_index / SCIC_SDS_REMOTE_NODES_PER_DWORD;
	dword_remainder = remote_yesde_index % SCIC_SDS_REMOTE_NODES_PER_DWORD;
	slot_yesrmalized = (dword_remainder / SCU_STP_REMOTE_NODE_COUNT) * sizeof(u32);
	slot_position   = remote_yesde_index % SCU_STP_REMOTE_NODE_COUNT;

	remote_yesde_table->available_remote_yesdes[dword_location] |=
		1 << (slot_yesrmalized + slot_position);
}

/**
 *
 * @out]: remote_yesde_table This is the remote yesde table from which to clear
 *    the available remote yesde bit.
 * @remote_yesde_index: This is the remote yesde index which is to be cleared
 *    from the table.
 *
 * This method clears the remote yesde index from the table of available remote
 * yesdes. yesne
 */
static void sci_remote_yesde_table_clear_yesde_index(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 remote_yesde_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 slot_position;
	u32 slot_yesrmalized;

	BUG_ON(
		(remote_yesde_table->available_yesdes_array_size * SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD)
		<= (remote_yesde_index / SCU_STP_REMOTE_NODE_COUNT)
		);

	dword_location  = remote_yesde_index / SCIC_SDS_REMOTE_NODES_PER_DWORD;
	dword_remainder = remote_yesde_index % SCIC_SDS_REMOTE_NODES_PER_DWORD;
	slot_yesrmalized = (dword_remainder / SCU_STP_REMOTE_NODE_COUNT) * sizeof(u32);
	slot_position   = remote_yesde_index % SCU_STP_REMOTE_NODE_COUNT;

	remote_yesde_table->available_remote_yesdes[dword_location] &=
		~(1 << (slot_yesrmalized + slot_position));
}

/**
 *
 * @out]: remote_yesde_table The remote yesde table from which the slot will be
 *    cleared.
 * @group_index: The index for the slot that is to be cleared.
 *
 * This method clears the entire table slot at the specified slot index. yesne
 */
static void sci_remote_yesde_table_clear_group(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 group_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 dword_value;

	BUG_ON(
		(remote_yesde_table->available_yesdes_array_size * SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD)
		<= (group_index / SCU_STP_REMOTE_NODE_COUNT)
		);

	dword_location  = group_index / SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;
	dword_remainder = group_index % SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;

	dword_value = remote_yesde_table->available_remote_yesdes[dword_location];
	dword_value &= ~(SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE << (dword_remainder * 4));
	remote_yesde_table->available_remote_yesdes[dword_location] = dword_value;
}

/**
 *
 * @remote_yesde_table:
 *
 * THis method sets an entire remote yesde group in the remote yesde table.
 */
static void sci_remote_yesde_table_set_group(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 group_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 dword_value;

	BUG_ON(
		(remote_yesde_table->available_yesdes_array_size * SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD)
		<= (group_index / SCU_STP_REMOTE_NODE_COUNT)
		);

	dword_location  = group_index / SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;
	dword_remainder = group_index % SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;

	dword_value = remote_yesde_table->available_remote_yesdes[dword_location];
	dword_value |= (SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE << (dword_remainder * 4));
	remote_yesde_table->available_remote_yesdes[dword_location] = dword_value;
}

/**
 *
 * @remote_yesde_table: This is the remote yesde table that for which the group
 *    value is to be returned.
 * @group_index: This is the group index to use to find the group value.
 *
 * This method will return the group value for the specified group index. The
 * bit values at the specified remote yesde group index.
 */
static u8 sci_remote_yesde_table_get_group_value(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 group_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 dword_value;

	dword_location  = group_index / SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;
	dword_remainder = group_index % SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;

	dword_value = remote_yesde_table->available_remote_yesdes[dword_location];
	dword_value &= (SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE << (dword_remainder * 4));
	dword_value = dword_value >> (dword_remainder * 4);

	return (u8)dword_value;
}

/**
 *
 * @out]: remote_yesde_table The remote that which is to be initialized.
 * @remote_yesde_entries: The number of entries to put in the table.
 *
 * This method will initialize the remote yesde table for use. yesne
 */
void sci_remote_yesde_table_initialize(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 remote_yesde_entries)
{
	u32 index;

	/*
	 * Initialize the raw data we could improve the speed by only initializing
	 * those entries that we are actually going to be used */
	memset(
		remote_yesde_table->available_remote_yesdes,
		0x00,
		sizeof(remote_yesde_table->available_remote_yesdes)
		);

	memset(
		remote_yesde_table->remote_yesde_groups,
		0x00,
		sizeof(remote_yesde_table->remote_yesde_groups)
		);

	/* Initialize the available remote yesde sets */
	remote_yesde_table->available_yesdes_array_size = (u16)
							(remote_yesde_entries / SCIC_SDS_REMOTE_NODES_PER_DWORD)
							+ ((remote_yesde_entries % SCIC_SDS_REMOTE_NODES_PER_DWORD) != 0);


	/* Initialize each full DWORD to a FULL SET of remote yesdes */
	for (index = 0; index < remote_yesde_entries; index++) {
		sci_remote_yesde_table_set_yesde_index(remote_yesde_table, index);
	}

	remote_yesde_table->group_array_size = (u16)
					      (remote_yesde_entries / (SCU_STP_REMOTE_NODE_COUNT * 32))
					      + ((remote_yesde_entries % (SCU_STP_REMOTE_NODE_COUNT * 32)) != 0);

	for (index = 0; index < (remote_yesde_entries / SCU_STP_REMOTE_NODE_COUNT); index++) {
		/*
		 * These are all guaranteed to be full slot values so fill them in the
		 * available sets of 3 remote yesdes */
		sci_remote_yesde_table_set_group_index(remote_yesde_table, 2, index);
	}

	/* Now fill in any remainders that we may find */
	if ((remote_yesde_entries % SCU_STP_REMOTE_NODE_COUNT) == 2) {
		sci_remote_yesde_table_set_group_index(remote_yesde_table, 1, index);
	} else if ((remote_yesde_entries % SCU_STP_REMOTE_NODE_COUNT) == 1) {
		sci_remote_yesde_table_set_group_index(remote_yesde_table, 0, index);
	}
}

/**
 *
 * @out]: remote_yesde_table The remote yesde table from which to allocate a
 *    remote yesde.
 * @table_index: The group index that is to be used for the search.
 *
 * This method will allocate a single RNi from the remote yesde table.  The
 * table index will determine from which remote yesde group table to search.
 * This search may fail and ayesther group yesde table can be specified.  The
 * function is designed to allow a serach of the available single remote yesde
 * group up to the triple remote yesde group.  If an entry is found in the
 * specified table the remote yesde is removed and the remote yesde groups are
 * updated. The RNi value or an invalid remote yesde context if an RNi can yest
 * be found.
 */
static u16 sci_remote_yesde_table_allocate_single_remote_yesde(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 group_table_index)
{
	u8 index;
	u8 group_value;
	u32 group_index;
	u16 remote_yesde_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;

	group_index = sci_remote_yesde_table_get_group_index(
		remote_yesde_table, group_table_index);

	/* We could yest find an available slot in the table selector 0 */
	if (group_index != SCIC_SDS_REMOTE_NODE_TABLE_INVALID_INDEX) {
		group_value = sci_remote_yesde_table_get_group_value(
			remote_yesde_table, group_index);

		for (index = 0; index < SCU_STP_REMOTE_NODE_COUNT; index++) {
			if (((1 << index) & group_value) != 0) {
				/* We have selected a bit yesw clear it */
				remote_yesde_index = (u16)(group_index * SCU_STP_REMOTE_NODE_COUNT
							  + index);

				sci_remote_yesde_table_clear_group_index(
					remote_yesde_table, group_table_index, group_index
					);

				sci_remote_yesde_table_clear_yesde_index(
					remote_yesde_table, remote_yesde_index
					);

				if (group_table_index > 0) {
					sci_remote_yesde_table_set_group_index(
						remote_yesde_table, group_table_index - 1, group_index
						);
				}

				break;
			}
		}
	}

	return remote_yesde_index;
}

/**
 *
 * @remote_yesde_table: This is the remote yesde table from which to allocate the
 *    remote yesde entries.
 * @group_table_index: THis is the group table index which must equal two (2)
 *    for this operation.
 *
 * This method will allocate three consecutive remote yesde context entries. If
 * there are yes remaining triple entries the function will return a failure.
 * The remote yesde index that represents three consecutive remote yesde entries
 * or an invalid remote yesde context if yesne can be found.
 */
static u16 sci_remote_yesde_table_allocate_triple_remote_yesde(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 group_table_index)
{
	u32 group_index;
	u16 remote_yesde_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;

	group_index = sci_remote_yesde_table_get_group_index(
		remote_yesde_table, group_table_index);

	if (group_index != SCIC_SDS_REMOTE_NODE_TABLE_INVALID_INDEX) {
		remote_yesde_index = (u16)group_index * SCU_STP_REMOTE_NODE_COUNT;

		sci_remote_yesde_table_clear_group_index(
			remote_yesde_table, group_table_index, group_index
			);

		sci_remote_yesde_table_clear_group(
			remote_yesde_table, group_index
			);
	}

	return remote_yesde_index;
}

/**
 *
 * @remote_yesde_table: This is the remote yesde table from which the remote yesde
 *    allocation is to take place.
 * @remote_yesde_count: This is ther remote yesde count which is one of
 *    SCU_SSP_REMOTE_NODE_COUNT(1) or SCU_STP_REMOTE_NODE_COUNT(3).
 *
 * This method will allocate a remote yesde that mataches the remote yesde count
 * specified by the caller.  Valid values for remote yesde count is
 * SCU_SSP_REMOTE_NODE_COUNT(1) or SCU_STP_REMOTE_NODE_COUNT(3). u16 This is
 * the remote yesde index that is returned or an invalid remote yesde context.
 */
u16 sci_remote_yesde_table_allocate_remote_yesde(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 remote_yesde_count)
{
	u16 remote_yesde_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;

	if (remote_yesde_count == SCU_SSP_REMOTE_NODE_COUNT) {
		remote_yesde_index =
			sci_remote_yesde_table_allocate_single_remote_yesde(
				remote_yesde_table, 0);

		if (remote_yesde_index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX) {
			remote_yesde_index =
				sci_remote_yesde_table_allocate_single_remote_yesde(
					remote_yesde_table, 1);
		}

		if (remote_yesde_index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX) {
			remote_yesde_index =
				sci_remote_yesde_table_allocate_single_remote_yesde(
					remote_yesde_table, 2);
		}
	} else if (remote_yesde_count == SCU_STP_REMOTE_NODE_COUNT) {
		remote_yesde_index =
			sci_remote_yesde_table_allocate_triple_remote_yesde(
				remote_yesde_table, 2);
	}

	return remote_yesde_index;
}

/**
 *
 * @remote_yesde_table:
 *
 * This method will free a single remote yesde index back to the remote yesde
 * table.  This routine will update the remote yesde groups
 */
static void sci_remote_yesde_table_release_single_remote_yesde(
	struct sci_remote_yesde_table *remote_yesde_table,
	u16 remote_yesde_index)
{
	u32 group_index;
	u8 group_value;

	group_index = remote_yesde_index / SCU_STP_REMOTE_NODE_COUNT;

	group_value = sci_remote_yesde_table_get_group_value(remote_yesde_table, group_index);

	/*
	 * Assert that we are yest trying to add an entry to a slot that is already
	 * full. */
	BUG_ON(group_value == SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE);

	if (group_value == 0x00) {
		/*
		 * There are yes entries in this slot so it must be added to the single
		 * slot table. */
		sci_remote_yesde_table_set_group_index(remote_yesde_table, 0, group_index);
	} else if ((group_value & (group_value - 1)) == 0) {
		/*
		 * There is only one entry in this slot so it must be moved from the
		 * single slot table to the dual slot table */
		sci_remote_yesde_table_clear_group_index(remote_yesde_table, 0, group_index);
		sci_remote_yesde_table_set_group_index(remote_yesde_table, 1, group_index);
	} else {
		/*
		 * There are two entries in the slot so it must be moved from the dual
		 * slot table to the tripple slot table. */
		sci_remote_yesde_table_clear_group_index(remote_yesde_table, 1, group_index);
		sci_remote_yesde_table_set_group_index(remote_yesde_table, 2, group_index);
	}

	sci_remote_yesde_table_set_yesde_index(remote_yesde_table, remote_yesde_index);
}

/**
 *
 * @remote_yesde_table: This is the remote yesde table to which the remote yesde
 *    index is to be freed.
 *
 * This method will release a group of three consecutive remote yesdes back to
 * the free remote yesdes.
 */
static void sci_remote_yesde_table_release_triple_remote_yesde(
	struct sci_remote_yesde_table *remote_yesde_table,
	u16 remote_yesde_index)
{
	u32 group_index;

	group_index = remote_yesde_index / SCU_STP_REMOTE_NODE_COUNT;

	sci_remote_yesde_table_set_group_index(
		remote_yesde_table, 2, group_index
		);

	sci_remote_yesde_table_set_group(remote_yesde_table, group_index);
}

/**
 *
 * @remote_yesde_table: The remote yesde table to which the remote yesde index is
 *    to be freed.
 * @remote_yesde_count: This is the count of consecutive remote yesdes that are
 *    to be freed.
 *
 * This method will release the remote yesde index back into the remote yesde
 * table free pool.
 */
void sci_remote_yesde_table_release_remote_yesde_index(
	struct sci_remote_yesde_table *remote_yesde_table,
	u32 remote_yesde_count,
	u16 remote_yesde_index)
{
	if (remote_yesde_count == SCU_SSP_REMOTE_NODE_COUNT) {
		sci_remote_yesde_table_release_single_remote_yesde(
			remote_yesde_table, remote_yesde_index);
	} else if (remote_yesde_count == SCU_STP_REMOTE_NODE_COUNT) {
		sci_remote_yesde_table_release_triple_remote_yesde(
			remote_yesde_table, remote_yesde_index);
	}
}

