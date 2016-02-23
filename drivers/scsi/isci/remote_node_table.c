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
 * along with this program; if not, write to the Free Software
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
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
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
#include "remote_node_table.h"
#include "remote_node_context.h"

/**
 *
 * @remote_node_table: This is the remote node index table from which the
 *    selection will be made.
 * @group_table_index: This is the index to the group table from which to
 *    search for an available selection.
 *
 * This routine will find the bit position in absolute bit terms of the next 32
 * + bit position.  If there are available bits in the first u32 then it is
 * just bit position. u32 This is the absolute bit position for an available
 * group.
 */
static u32 sci_remote_node_table_get_group_index(
	struct sci_remote_node_table *remote_node_table,
	u32 group_table_index)
{
	u32 dword_index;
	u32 *group_table;
	u32 bit_index;

	group_table = remote_node_table->remote_node_groups[group_table_index];

	for (dword_index = 0; dword_index < remote_node_table->group_array_size; dword_index++) {
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
 * @out]: remote_node_table This the remote node table in which to clear the
 *    selector.
 * @set_index: This is the remote node selector in which the change will be
 *    made.
 * @group_index: This is the bit index in the table to be modified.
 *
 * This method will clear the group index entry in the specified group index
 * table. none
 */
static void sci_remote_node_table_clear_group_index(
	struct sci_remote_node_table *remote_node_table,
	u32 group_table_index,
	u32 group_index)
{
	u32 dword_index;
	u32 bit_index;
	u32 *group_table;

	BUG_ON(group_table_index >= SCU_STP_REMOTE_NODE_COUNT);
	BUG_ON(group_index >= (u32)(remote_node_table->group_array_size * 32));

	dword_index = group_index / 32;
	bit_index   = group_index % 32;
	group_table = remote_node_table->remote_node_groups[group_table_index];

	group_table[dword_index] = group_table[dword_index] & ~(1 << bit_index);
}

/**
 *
 * @out]: remote_node_table This the remote node table in which to set the
 *    selector.
 * @group_table_index: This is the remote node selector in which the change
 *    will be made.
 * @group_index: This is the bit position in the table to be modified.
 *
 * This method will set the group index bit entry in the specified gropu index
 * table. none
 */
static void sci_remote_node_table_set_group_index(
	struct sci_remote_node_table *remote_node_table,
	u32 group_table_index,
	u32 group_index)
{
	u32 dword_index;
	u32 bit_index;
	u32 *group_table;

	BUG_ON(group_table_index >= SCU_STP_REMOTE_NODE_COUNT);
	BUG_ON(group_index >= (u32)(remote_node_table->group_array_size * 32));

	dword_index = group_index / 32;
	bit_index   = group_index % 32;
	group_table = remote_node_table->remote_node_groups[group_table_index];

	group_table[dword_index] = group_table[dword_index] | (1 << bit_index);
}

/**
 *
 * @out]: remote_node_table This is the remote node table in which to modify
 *    the remote node availability.
 * @remote_node_index: This is the remote node index that is being returned to
 *    the table.
 *
 * This method will set the remote to available in the remote node allocation
 * table. none
 */
static void sci_remote_node_table_set_node_index(
	struct sci_remote_node_table *remote_node_table,
	u32 remote_node_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 slot_normalized;
	u32 slot_position;

	BUG_ON(
		(remote_node_table->available_nodes_array_size * SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD)
		<= (remote_node_index / SCU_STP_REMOTE_NODE_COUNT)
		);

	dword_location  = remote_node_index / SCIC_SDS_REMOTE_NODES_PER_DWORD;
	dword_remainder = remote_node_index % SCIC_SDS_REMOTE_NODES_PER_DWORD;
	slot_normalized = (dword_remainder / SCU_STP_REMOTE_NODE_COUNT) * sizeof(u32);
	slot_position   = remote_node_index % SCU_STP_REMOTE_NODE_COUNT;

	remote_node_table->available_remote_nodes[dword_location] |=
		1 << (slot_normalized + slot_position);
}

/**
 *
 * @out]: remote_node_table This is the remote node table from which to clear
 *    the available remote node bit.
 * @remote_node_index: This is the remote node index which is to be cleared
 *    from the table.
 *
 * This method clears the remote node index from the table of available remote
 * nodes. none
 */
static void sci_remote_node_table_clear_node_index(
	struct sci_remote_node_table *remote_node_table,
	u32 remote_node_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 slot_position;
	u32 slot_normalized;

	BUG_ON(
		(remote_node_table->available_nodes_array_size * SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD)
		<= (remote_node_index / SCU_STP_REMOTE_NODE_COUNT)
		);

	dword_location  = remote_node_index / SCIC_SDS_REMOTE_NODES_PER_DWORD;
	dword_remainder = remote_node_index % SCIC_SDS_REMOTE_NODES_PER_DWORD;
	slot_normalized = (dword_remainder / SCU_STP_REMOTE_NODE_COUNT) * sizeof(u32);
	slot_position   = remote_node_index % SCU_STP_REMOTE_NODE_COUNT;

	remote_node_table->available_remote_nodes[dword_location] &=
		~(1 << (slot_normalized + slot_position));
}

/**
 *
 * @out]: remote_node_table The remote node table from which the slot will be
 *    cleared.
 * @group_index: The index for the slot that is to be cleared.
 *
 * This method clears the entire table slot at the specified slot index. none
 */
static void sci_remote_node_table_clear_group(
	struct sci_remote_node_table *remote_node_table,
	u32 group_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 dword_value;

	BUG_ON(
		(remote_node_table->available_nodes_array_size * SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD)
		<= (group_index / SCU_STP_REMOTE_NODE_COUNT)
		);

	dword_location  = group_index / SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;
	dword_remainder = group_index % SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;

	dword_value = remote_node_table->available_remote_nodes[dword_location];
	dword_value &= ~(SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE << (dword_remainder * 4));
	remote_node_table->available_remote_nodes[dword_location] = dword_value;
}

/**
 *
 * @remote_node_table:
 *
 * THis method sets an entire remote node group in the remote node table.
 */
static void sci_remote_node_table_set_group(
	struct sci_remote_node_table *remote_node_table,
	u32 group_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 dword_value;

	BUG_ON(
		(remote_node_table->available_nodes_array_size * SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD)
		<= (group_index / SCU_STP_REMOTE_NODE_COUNT)
		);

	dword_location  = group_index / SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;
	dword_remainder = group_index % SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;

	dword_value = remote_node_table->available_remote_nodes[dword_location];
	dword_value |= (SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE << (dword_remainder * 4));
	remote_node_table->available_remote_nodes[dword_location] = dword_value;
}

/**
 *
 * @remote_node_table: This is the remote node table that for which the group
 *    value is to be returned.
 * @group_index: This is the group index to use to find the group value.
 *
 * This method will return the group value for the specified group index. The
 * bit values at the specified remote node group index.
 */
static u8 sci_remote_node_table_get_group_value(
	struct sci_remote_node_table *remote_node_table,
	u32 group_index)
{
	u32 dword_location;
	u32 dword_remainder;
	u32 dword_value;

	dword_location  = group_index / SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;
	dword_remainder = group_index % SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD;

	dword_value = remote_node_table->available_remote_nodes[dword_location];
	dword_value &= (SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE << (dword_remainder * 4));
	dword_value = dword_value >> (dword_remainder * 4);

	return (u8)dword_value;
}

/**
 *
 * @out]: remote_node_table The remote that which is to be initialized.
 * @remote_node_entries: The number of entries to put in the table.
 *
 * This method will initialize the remote node table for use. none
 */
void sci_remote_node_table_initialize(
	struct sci_remote_node_table *remote_node_table,
	u32 remote_node_entries)
{
	u32 index;

	/*
	 * Initialize the raw data we could improve the speed by only initializing
	 * those entries that we are actually going to be used */
	memset(
		remote_node_table->available_remote_nodes,
		0x00,
		sizeof(remote_node_table->available_remote_nodes)
		);

	memset(
		remote_node_table->remote_node_groups,
		0x00,
		sizeof(remote_node_table->remote_node_groups)
		);

	/* Initialize the available remote node sets */
	remote_node_table->available_nodes_array_size = (u16)
							(remote_node_entries / SCIC_SDS_REMOTE_NODES_PER_DWORD)
							+ ((remote_node_entries % SCIC_SDS_REMOTE_NODES_PER_DWORD) != 0);


	/* Initialize each full DWORD to a FULL SET of remote nodes */
	for (index = 0; index < remote_node_entries; index++) {
		sci_remote_node_table_set_node_index(remote_node_table, index);
	}

	remote_node_table->group_array_size = (u16)
					      (remote_node_entries / (SCU_STP_REMOTE_NODE_COUNT * 32))
					      + ((remote_node_entries % (SCU_STP_REMOTE_NODE_COUNT * 32)) != 0);

	for (index = 0; index < (remote_node_entries / SCU_STP_REMOTE_NODE_COUNT); index++) {
		/*
		 * These are all guaranteed to be full slot values so fill them in the
		 * available sets of 3 remote nodes */
		sci_remote_node_table_set_group_index(remote_node_table, 2, index);
	}

	/* Now fill in any remainders that we may find */
	if ((remote_node_entries % SCU_STP_REMOTE_NODE_COUNT) == 2) {
		sci_remote_node_table_set_group_index(remote_node_table, 1, index);
	} else if ((remote_node_entries % SCU_STP_REMOTE_NODE_COUNT) == 1) {
		sci_remote_node_table_set_group_index(remote_node_table, 0, index);
	}
}

/**
 *
 * @out]: remote_node_table The remote node table from which to allocate a
 *    remote node.
 * @table_index: The group index that is to be used for the search.
 *
 * This method will allocate a single RNi from the remote node table.  The
 * table index will determine from which remote node group table to search.
 * This search may fail and another group node table can be specified.  The
 * function is designed to allow a serach of the available single remote node
 * group up to the triple remote node group.  If an entry is found in the
 * specified table the remote node is removed and the remote node groups are
 * updated. The RNi value or an invalid remote node context if an RNi can not
 * be found.
 */
static u16 sci_remote_node_table_allocate_single_remote_node(
	struct sci_remote_node_table *remote_node_table,
	u32 group_table_index)
{
	u8 index;
	u8 group_value;
	u32 group_index;
	u16 remote_node_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;

	group_index = sci_remote_node_table_get_group_index(
		remote_node_table, group_table_index);

	/* We could not find an available slot in the table selector 0 */
	if (group_index != SCIC_SDS_REMOTE_NODE_TABLE_INVALID_INDEX) {
		group_value = sci_remote_node_table_get_group_value(
			remote_node_table, group_index);

		for (index = 0; index < SCU_STP_REMOTE_NODE_COUNT; index++) {
			if (((1 << index) & group_value) != 0) {
				/* We have selected a bit now clear it */
				remote_node_index = (u16)(group_index * SCU_STP_REMOTE_NODE_COUNT
							  + index);

				sci_remote_node_table_clear_group_index(
					remote_node_table, group_table_index, group_index
					);

				sci_remote_node_table_clear_node_index(
					remote_node_table, remote_node_index
					);

				if (group_table_index > 0) {
					sci_remote_node_table_set_group_index(
						remote_node_table, group_table_index - 1, group_index
						);
				}

				break;
			}
		}
	}

	return remote_node_index;
}

/**
 *
 * @remote_node_table: This is the remote node table from which to allocate the
 *    remote node entries.
 * @group_table_index: THis is the group table index which must equal two (2)
 *    for this operation.
 *
 * This method will allocate three consecutive remote node context entries. If
 * there are no remaining triple entries the function will return a failure.
 * The remote node index that represents three consecutive remote node entries
 * or an invalid remote node context if none can be found.
 */
static u16 sci_remote_node_table_allocate_triple_remote_node(
	struct sci_remote_node_table *remote_node_table,
	u32 group_table_index)
{
	u32 group_index;
	u16 remote_node_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;

	group_index = sci_remote_node_table_get_group_index(
		remote_node_table, group_table_index);

	if (group_index != SCIC_SDS_REMOTE_NODE_TABLE_INVALID_INDEX) {
		remote_node_index = (u16)group_index * SCU_STP_REMOTE_NODE_COUNT;

		sci_remote_node_table_clear_group_index(
			remote_node_table, group_table_index, group_index
			);

		sci_remote_node_table_clear_group(
			remote_node_table, group_index
			);
	}

	return remote_node_index;
}

/**
 *
 * @remote_node_table: This is the remote node table from which the remote node
 *    allocation is to take place.
 * @remote_node_count: This is ther remote node count which is one of
 *    SCU_SSP_REMOTE_NODE_COUNT(1) or SCU_STP_REMOTE_NODE_COUNT(3).
 *
 * This method will allocate a remote node that mataches the remote node count
 * specified by the caller.  Valid values for remote node count is
 * SCU_SSP_REMOTE_NODE_COUNT(1) or SCU_STP_REMOTE_NODE_COUNT(3). u16 This is
 * the remote node index that is returned or an invalid remote node context.
 */
u16 sci_remote_node_table_allocate_remote_node(
	struct sci_remote_node_table *remote_node_table,
	u32 remote_node_count)
{
	u16 remote_node_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;

	if (remote_node_count == SCU_SSP_REMOTE_NODE_COUNT) {
		remote_node_index =
			sci_remote_node_table_allocate_single_remote_node(
				remote_node_table, 0);

		if (remote_node_index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX) {
			remote_node_index =
				sci_remote_node_table_allocate_single_remote_node(
					remote_node_table, 1);
		}

		if (remote_node_index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX) {
			remote_node_index =
				sci_remote_node_table_allocate_single_remote_node(
					remote_node_table, 2);
		}
	} else if (remote_node_count == SCU_STP_REMOTE_NODE_COUNT) {
		remote_node_index =
			sci_remote_node_table_allocate_triple_remote_node(
				remote_node_table, 2);
	}

	return remote_node_index;
}

/**
 *
 * @remote_node_table:
 *
 * This method will free a single remote node index back to the remote node
 * table.  This routine will update the remote node groups
 */
static void sci_remote_node_table_release_single_remote_node(
	struct sci_remote_node_table *remote_node_table,
	u16 remote_node_index)
{
	u32 group_index;
	u8 group_value;

	group_index = remote_node_index / SCU_STP_REMOTE_NODE_COUNT;

	group_value = sci_remote_node_table_get_group_value(remote_node_table, group_index);

	/*
	 * Assert that we are not trying to add an entry to a slot that is already
	 * full. */
	BUG_ON(group_value == SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE);

	if (group_value == 0x00) {
		/*
		 * There are no entries in this slot so it must be added to the single
		 * slot table. */
		sci_remote_node_table_set_group_index(remote_node_table, 0, group_index);
	} else if ((group_value & (group_value - 1)) == 0) {
		/*
		 * There is only one entry in this slot so it must be moved from the
		 * single slot table to the dual slot table */
		sci_remote_node_table_clear_group_index(remote_node_table, 0, group_index);
		sci_remote_node_table_set_group_index(remote_node_table, 1, group_index);
	} else {
		/*
		 * There are two entries in the slot so it must be moved from the dual
		 * slot table to the tripple slot table. */
		sci_remote_node_table_clear_group_index(remote_node_table, 1, group_index);
		sci_remote_node_table_set_group_index(remote_node_table, 2, group_index);
	}

	sci_remote_node_table_set_node_index(remote_node_table, remote_node_index);
}

/**
 *
 * @remote_node_table: This is the remote node table to which the remote node
 *    index is to be freed.
 *
 * This method will release a group of three consecutive remote nodes back to
 * the free remote nodes.
 */
static void sci_remote_node_table_release_triple_remote_node(
	struct sci_remote_node_table *remote_node_table,
	u16 remote_node_index)
{
	u32 group_index;

	group_index = remote_node_index / SCU_STP_REMOTE_NODE_COUNT;

	sci_remote_node_table_set_group_index(
		remote_node_table, 2, group_index
		);

	sci_remote_node_table_set_group(remote_node_table, group_index);
}

/**
 *
 * @remote_node_table: The remote node table to which the remote node index is
 *    to be freed.
 * @remote_node_count: This is the count of consecutive remote nodes that are
 *    to be freed.
 *
 * This method will release the remote node index back into the remote node
 * table free pool.
 */
void sci_remote_node_table_release_remote_node_index(
	struct sci_remote_node_table *remote_node_table,
	u32 remote_node_count,
	u16 remote_node_index)
{
	if (remote_node_count == SCU_SSP_REMOTE_NODE_COUNT) {
		sci_remote_node_table_release_single_remote_node(
			remote_node_table, remote_node_index);
	} else if (remote_node_count == SCU_STP_REMOTE_NODE_COUNT) {
		sci_remote_node_table_release_triple_remote_node(
			remote_node_table, remote_node_index);
	}
}

