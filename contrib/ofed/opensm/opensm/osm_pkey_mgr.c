/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 * Implementation of the P_Key Manager (Partition Manager).
 * This is part of the OpenSM.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_PKEY_MGR_C
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_opensm.h>

static void clear_accum_pkey_index(osm_pkey_tbl_t * p_pkey_tbl,
				   uint16_t pkey_index);

/*
  The max number of pkeys/pkey blocks for a physical port is located
  in a different place for switch external ports (SwitchInfo) and the
  rest of the ports (NodeInfo).
*/
static uint16_t
pkey_mgr_get_physp_max_pkeys(IN const osm_physp_t * p_physp)
{
	osm_node_t *p_node = osm_physp_get_node_ptr(p_physp);
	uint16_t num_pkeys = 0;

	if (!p_node->sw || (osm_physp_get_port_num(p_physp) == 0))
		num_pkeys = cl_ntoh16(p_node->node_info.partition_cap);
	else
		num_pkeys = cl_ntoh16(p_node->sw->switch_info.enforce_cap);
	return num_pkeys;
}

static uint16_t
pkey_mgr_get_physp_max_blocks(IN const osm_physp_t * p_physp)
{
	return ((pkey_mgr_get_physp_max_pkeys(p_physp) + 31) / 32);
}

/*
 * Insert new pending pkey entry to the specific port pkey table
 * pending pkeys. New entries are inserted at the back.
 */
static void
pkey_mgr_process_physical_port(IN osm_log_t * p_log,
			       IN osm_sm_t * sm,
			       IN const ib_net16_t pkey,
			       IN osm_physp_t * p_physp)
{
	osm_node_t *p_node = osm_physp_get_node_ptr(p_physp);
	osm_pkey_tbl_t *p_pkey_tbl;
	ib_net16_t *p_orig_pkey;
	osm_pending_pkey_t *p_pending;

	p_pkey_tbl = &p_physp->pkeys;
	p_pending = (osm_pending_pkey_t *) calloc(1, sizeof(osm_pending_pkey_t));
	if (!p_pending) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0502: "
			"Failed to allocate new pending pkey entry for node "
			"0x%016" PRIx64 " port %u\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)),
			osm_physp_get_port_num(p_physp));
		return;
	}
	p_pending->pkey = pkey;
	if (sm->p_subn->opt.allow_both_pkeys)
		p_orig_pkey = cl_map_get(&p_pkey_tbl->keys, pkey);
	else
		p_orig_pkey = cl_map_get(&p_pkey_tbl->keys,
					 ib_pkey_get_base(pkey));

	if (!p_orig_pkey) {
		p_pending->is_new = TRUE;
	} else {
		CL_ASSERT(ib_pkey_get_base(*p_orig_pkey) ==
			  ib_pkey_get_base(pkey));
		p_pending->is_new = FALSE;
		if (osm_pkey_tbl_get_block_and_idx(p_pkey_tbl, p_orig_pkey,
						   &p_pending->block,
						   &p_pending->index) !=
		    IB_SUCCESS) {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0503: "
				"Failed to obtain P_Key 0x%04x block and index "
				"for node 0x%016" PRIx64 " port %u\n",
				cl_ntoh16(ib_pkey_get_base(pkey)),
				cl_ntoh64(osm_node_get_node_guid(p_node)),
				osm_physp_get_port_num(p_physp));
			free(p_pending);
			return;
		}
		if (p_physp->pkeys.indx0_pkey) {
			/*
			 * Remove the pkey that should be at index 0 from
			 * accum pkey if current position is not index 0
			 */
			if (((sm->p_subn->opt.allow_both_pkeys &&
			      pkey == p_physp->pkeys.indx0_pkey) ||
			     (!sm->p_subn->opt.allow_both_pkeys &&
			      ib_pkey_get_base(pkey) == ib_pkey_get_base(p_physp->pkeys.indx0_pkey))) &&
			    (p_pending->block != 0 || p_pending->index != 0)) {
					p_pending->is_new = TRUE;
					clear_accum_pkey_index(p_pkey_tbl,
							       p_pending->block *
							       IB_NUM_PKEY_ELEMENTS_IN_BLOCK +
							       p_pending->index);
			}

			if (p_pending->block == 0 && p_pending->index == 0) {
				/* Move the pkey away from index 0 */
				if ((sm->p_subn->opt.allow_both_pkeys &&
				     pkey != p_physp->pkeys.indx0_pkey) ||
				    (!sm->p_subn->opt.allow_both_pkeys &&
				     ib_pkey_get_base(pkey) != ib_pkey_get_base(p_physp->pkeys.indx0_pkey))) {
					p_pending->is_new = TRUE;
					clear_accum_pkey_index(p_pkey_tbl, 0);
				}
			}
		 } else {
			/* If index 0 is occupied by non-default, it should reoccupied by pkey 0x7FFF */
			if (p_pending->block == 0 && p_pending->index == 0) {
				if (ib_pkey_get_base(pkey) != IB_DEFAULT_PARTIAL_PKEY) {
					p_pending->is_new = TRUE;
					clear_accum_pkey_index(p_pkey_tbl, 0);
				}
			/* Need to move default pkey to index 0 */
			} else if ((sm->p_subn->opt.allow_both_pkeys &&
				    pkey == IB_DEFAULT_PKEY) ||
				   (!sm->p_subn->opt.allow_both_pkeys &&
				    ib_pkey_get_base(pkey) == IB_DEFAULT_PARTIAL_PKEY)) {
					p_pending->is_new = TRUE;
					clear_accum_pkey_index(p_pkey_tbl,
							       p_pending->block *
							       IB_NUM_PKEY_ELEMENTS_IN_BLOCK +
							       p_pending->index);
			}
		}

	}
	if (p_pending->is_new == TRUE)
		 cl_qlist_insert_tail(&p_pkey_tbl->pending,
				      (cl_list_item_t *) p_pending);
	else
		cl_qlist_insert_head(&p_pkey_tbl->pending,
				     (cl_list_item_t *) p_pending);

	OSM_LOG(p_log, OSM_LOG_DEBUG,
		"pkey 0x%04x was %s for node 0x%016" PRIx64 " port %u\n",
		cl_ntoh16(pkey), p_pending->is_new ? "inserted" : "updated",
		cl_ntoh64(osm_node_get_node_guid(p_node)),
		osm_physp_get_port_num(p_physp));
}

static void
pkey_mgr_process_partition_table(osm_log_t * p_log, osm_sm_t * sm,
				 const osm_prtn_t * p_prtn,
				 const boolean_t full)
{
	const cl_map_t *p_tbl =
	    full ? &p_prtn->full_guid_tbl : &p_prtn->part_guid_tbl;
	cl_map_iterator_t i, i_next;
	ib_net16_t pkey = p_prtn->pkey;
	osm_physp_t *p_physp;

	if (full)
		pkey |= cl_hton16(0x8000);

	i_next = cl_map_head(p_tbl);
	while (i_next != cl_map_end(p_tbl)) {
		i = i_next;
		i_next = cl_map_next(i);
		p_physp = cl_map_obj(i);
		if (p_physp)
			pkey_mgr_process_physical_port(p_log, sm, pkey,
						       p_physp);
	}
}

static ib_api_status_t
pkey_mgr_update_pkey_entry(IN osm_sm_t * sm,
			   IN const osm_physp_t * p_physp,
			   IN const ib_pkey_table_t * block,
			   IN const uint16_t block_index)
{
	osm_madw_context_t context;
	osm_node_t *p_node = osm_physp_get_node_ptr(p_physp);
	osm_physp_t *physp0;
	uint32_t attr_mod;
	ib_net64_t m_key;

	context.pkey_context.node_guid = osm_node_get_node_guid(p_node);
	context.pkey_context.port_guid = osm_physp_get_port_guid(p_physp);
	context.pkey_context.set_method = TRUE;
	attr_mod = block_index;
	if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH &&
	    osm_physp_get_port_num(p_physp) != 0) {
		attr_mod |= osm_physp_get_port_num(p_physp) << 16;
		physp0 = osm_node_get_physp_ptr(p_node, 0);
		m_key = ib_port_info_get_m_key(&physp0->port_info);
	} else
		m_key = ib_port_info_get_m_key(&p_physp->port_info);
	return osm_req_set(sm, osm_physp_get_dr_path_ptr(p_physp),
			   (uint8_t *) block, sizeof(*block),
			   IB_MAD_ATTR_P_KEY_TABLE,
			   cl_hton32(attr_mod), FALSE, m_key,
			   CL_DISP_MSGID_NONE, &context);
}

static ib_api_status_t
pkey_mgr_enforce_partition(IN osm_log_t * p_log, osm_sm_t * sm,
			   IN osm_physp_t * p_physp,
			   IN osm_partition_enforce_type_enum enforce_type)
{
	osm_madw_context_t context;
	uint8_t payload[IB_SMP_DATA_SIZE];
	ib_port_info_t *p_pi;
	ib_net64_t m_key;
	osm_physp_t *physp0;
	ib_api_status_t status;
	uint8_t enforce_bits;

	p_pi = &p_physp->port_info;

	if (enforce_type == OSM_PARTITION_ENFORCE_TYPE_BOTH)
		enforce_bits = 0xc;
	else if (enforce_type == OSM_PARTITION_ENFORCE_TYPE_IN)
		enforce_bits = 0x8;
	else
		enforce_bits = 0x4;

	if ((p_pi->vl_enforce & 0xc) == enforce_bits *
	    (enforce_type != OSM_PARTITION_ENFORCE_TYPE_OFF)) {
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"No need to update PortInfo for "
			"node 0x%016" PRIx64 " port %u (%s)\n",
			cl_ntoh64(osm_node_get_node_guid
				  (osm_physp_get_node_ptr(p_physp))),
			osm_physp_get_port_num(p_physp),
			p_physp->p_node->print_desc);
		return IB_SUCCESS;
	}

	memcpy(payload, p_pi, sizeof(ib_port_info_t));

	p_pi = (ib_port_info_t *) payload;
	p_pi->vl_enforce &= ~0xc;
	if (enforce_type != OSM_PARTITION_ENFORCE_TYPE_OFF)
		p_pi->vl_enforce |= enforce_bits;

	p_pi->state_info2 = 0;
	ib_port_info_set_port_state(p_pi, IB_LINK_NO_CHANGE);

	physp0 = osm_node_get_physp_ptr(p_physp->p_node, 0);
	m_key = ib_port_info_get_m_key(&physp0->port_info);

	context.pi_context.node_guid =
	    osm_node_get_node_guid(osm_physp_get_node_ptr(p_physp));
	context.pi_context.port_guid = osm_physp_get_port_guid(p_physp);
	context.pi_context.set_method = TRUE;
	context.pi_context.light_sweep = FALSE;
	context.pi_context.active_transition = FALSE;
	context.pi_context.client_rereg = FALSE;

	status = osm_req_set(sm, osm_physp_get_dr_path_ptr(p_physp),
			     payload, sizeof(payload),
			     IB_MAD_ATTR_PORT_INFO,
			     cl_hton32(osm_physp_get_port_num(p_physp)),
			     FALSE, m_key,
			     CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0511: "
			"Failed to set PortInfo for "
			"node 0x%016" PRIx64 " port %u (%s)\n",
			cl_ntoh64(osm_node_get_node_guid
				  (osm_physp_get_node_ptr(p_physp))),
			osm_physp_get_port_num(p_physp),
			p_physp->p_node->print_desc);
	else
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Set PortInfo for node 0x%016" PRIx64 " port %u (%s)\n",
			cl_ntoh64(osm_node_get_node_guid
				  (osm_physp_get_node_ptr(p_physp))),
			osm_physp_get_port_num(p_physp),
			p_physp->p_node->print_desc);
	return status;
}

static void clear_accum_pkey_index(osm_pkey_tbl_t * p_pkey_tbl,
				   uint16_t pkey_index)
{
	uint16_t pkey_idx_bias, pkey_idx;
	void *ptr;
	uintptr_t pkey_idx_ptr;
	cl_map_iterator_t map_iter, map_iter_temp;

	map_iter = cl_map_head(&p_pkey_tbl->accum_pkeys);

	pkey_idx_bias = pkey_index + 1; // adjust for pkey index bias in accum_pkeys

	while (map_iter != cl_map_end(&p_pkey_tbl->accum_pkeys)) {
		map_iter_temp = cl_map_next(map_iter);
		ptr = (uint16_t *) cl_map_obj(map_iter);
		CL_ASSERT(ptr);
		pkey_idx_ptr = (uintptr_t) ptr;
		pkey_idx = pkey_idx_ptr;
		if (pkey_idx == pkey_idx_bias) {
			cl_map_remove_item(&p_pkey_tbl->accum_pkeys, map_iter);
			if (p_pkey_tbl->last_pkey_idx == pkey_idx)
				osm_pkey_find_last_accum_pkey_index(p_pkey_tbl);
			break;
		}
		map_iter = map_iter_temp;
	}
}

static int last_accum_pkey_index(osm_pkey_tbl_t * p_pkey_tbl,
				 uint16_t * p_block_idx,
				 uint8_t * p_pkey_idx)
{
	if (p_pkey_tbl->last_pkey_idx) {
		*p_block_idx = (p_pkey_tbl->last_pkey_idx - 1) / IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
		*p_pkey_idx = (p_pkey_tbl->last_pkey_idx - 1) % IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
		return 1;
	}

	return 0;
}

static int pkey_mgr_update_port(osm_log_t * p_log, osm_sm_t * sm,
				const osm_port_t * const p_port)
{
	osm_physp_t *p_physp;
	osm_node_t *p_node;
	ib_pkey_table_t *block, *new_block;
	osm_pkey_tbl_t *p_pkey_tbl;
	uint16_t block_index;
	uint8_t pkey_index;
	uint16_t last_free_block_index = 0;
	uint8_t last_free_pkey_index = 0;
	uint16_t num_of_blocks;
	uint16_t max_num_of_blocks;
	ib_api_status_t status;
	osm_pending_pkey_t *p_pending;
	boolean_t found;
	ib_pkey_table_t empty_block;
	int ret = 0, full = 0;
	void *ptr;
	uintptr_t pkey_idx_ptr;
	uint16_t pkey_idx;

	p_physp = p_port->p_physp;
	if (!p_physp)
		return FALSE;

	memset(&empty_block, 0, sizeof(ib_pkey_table_t));

	p_node = osm_physp_get_node_ptr(p_physp);
	p_pkey_tbl = &p_physp->pkeys;
	num_of_blocks = osm_pkey_tbl_get_num_blocks(p_pkey_tbl);
	max_num_of_blocks = pkey_mgr_get_physp_max_blocks(p_physp);
	if (p_pkey_tbl->max_blocks > max_num_of_blocks) {
		OSM_LOG(p_log, OSM_LOG_INFO,
			"Max number of blocks reduced from %u to %u "
			"for node 0x%016" PRIx64 " port %u (%s)\n",
			p_pkey_tbl->max_blocks, max_num_of_blocks,
			cl_ntoh64(osm_node_get_node_guid(p_node)),
			osm_physp_get_port_num(p_physp),
			p_physp->p_node->print_desc);
	}
	p_pkey_tbl->max_blocks = max_num_of_blocks;

	osm_pkey_tbl_init_new_blocks(p_pkey_tbl);
	p_pkey_tbl->used_blocks = 0;

	/*
	   process every pending pkey in order -
	   first must be "updated" last are "new"
	 */
	p_pending =
	    (osm_pending_pkey_t *) cl_qlist_remove_head(&p_pkey_tbl->pending);
	while (p_pending !=
	       (osm_pending_pkey_t *) cl_qlist_end(&p_pkey_tbl->pending)) {

		found = FALSE;
		ptr = NULL;

		if (p_pending->is_new == FALSE) {
			block_index = p_pending->block;
			pkey_index = p_pending->index;
			found = TRUE;
		} else {
			ptr = cl_map_get(&p_pkey_tbl->accum_pkeys,p_pending->pkey);
			if (ptr != NULL) {
				pkey_idx_ptr = (uintptr_t) ptr;
				pkey_idx = pkey_idx_ptr;
				pkey_idx--; /* adjust pkey index for bias */
				block_index = pkey_idx / IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
				pkey_index = pkey_idx % IB_NUM_PKEY_ELEMENTS_IN_BLOCK;

				if (((sm->p_subn->opt.allow_both_pkeys &&
				      p_pending->pkey == p_physp->pkeys.indx0_pkey) ||
				     (!sm->p_subn->opt.allow_both_pkeys &&
				      ib_pkey_get_base(p_pending->pkey) == ib_pkey_get_base(p_physp->pkeys.indx0_pkey))) ||
				    ((p_pending->pkey != p_physp->pkeys.indx0_pkey &&
				      pkey_idx == 0))) {
					clear_accum_pkey_index(p_pkey_tbl, pkey_idx);
					cl_qlist_insert_tail(&p_pkey_tbl->pending,
							     (cl_list_item_t *)p_pending);
					p_pending =
                                            (osm_pending_pkey_t *) cl_qlist_remove_head(&p_pkey_tbl->pending);
					continue;
				} else
					found = TRUE;
			}

			if (!found) {
				if (!p_pkey_tbl->indx0_pkey &&
				    ((sm->p_subn->opt.allow_both_pkeys &&
				      p_pending->pkey == IB_DEFAULT_PKEY) ||
				     (!sm->p_subn->opt.allow_both_pkeys &&
				      ib_pkey_get_base(p_pending->pkey) == IB_DEFAULT_PARTIAL_PKEY))) {
					block_index = 0;
					pkey_index = 0;
				} else if ((sm->p_subn->opt.allow_both_pkeys &&
					    p_pending->pkey == p_pkey_tbl->indx0_pkey) ||
					   (!sm->p_subn->opt.allow_both_pkeys &&
					    ib_pkey_get_base(p_pending->pkey) ==
					    ib_pkey_get_base(p_pkey_tbl->indx0_pkey))) {
					block_index = 0;
					pkey_index = 0;
				} else if (last_accum_pkey_index(p_pkey_tbl,
								 &last_free_block_index,
								 &last_free_pkey_index)) {
					block_index = last_free_block_index;
					pkey_index = last_free_pkey_index + 1;
					if (pkey_index >= IB_NUM_PKEY_ELEMENTS_IN_BLOCK) {
						block_index++;
						pkey_index -= IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
					}
				} else {
					block_index = 0;
					pkey_index = 1;
				}

				if (block_index * IB_NUM_PKEY_ELEMENTS_IN_BLOCK + pkey_index >= pkey_mgr_get_physp_max_pkeys(p_physp)) {
					if ((sm->p_subn->opt.allow_both_pkeys &&
					     p_pending->pkey != IB_DEFAULT_PKEY) ||
					    (!sm->p_subn->opt.allow_both_pkeys &&
					     ib_pkey_get_base(p_pending->pkey) != IB_DEFAULT_PARTIAL_PKEY)) {
						last_free_block_index = 0;
						last_free_pkey_index = 1;
						found = osm_pkey_find_next_free_entry(p_pkey_tbl, &last_free_block_index, &last_free_pkey_index);
					} else
						found = FALSE;
					if (!found)
						full = 1;
					else {
						block_index = last_free_block_index;
						pkey_index = last_free_pkey_index;
						if (block_index * IB_NUM_PKEY_ELEMENTS_IN_BLOCK + pkey_index >= pkey_mgr_get_physp_max_pkeys(p_physp)) {
							full = 1;
							found = FALSE;
						} else {
							OSM_LOG(p_log, OSM_LOG_INFO,
								"Reusing PKeyTable block index %u pkey index %u "
								"for pkey 0x%x on 0x%016" PRIx64 " port %u (%s)\n",
								block_index,
								pkey_index,
								cl_ntoh16(p_pending->pkey),
								cl_ntoh64(osm_node_get_node_guid(p_node)),
								osm_physp_get_port_num(p_physp),
								p_physp->p_node->print_desc);

							clear_accum_pkey_index(p_pkey_tbl, block_index * IB_NUM_PKEY_ELEMENTS_IN_BLOCK + pkey_index);
						}
					}
					if (full)
						OSM_LOG(p_log, OSM_LOG_ERROR,
							"ERR 0512: "
							"Failed to set PKey 0x%04x because Pkey table is full "
							"for node 0x%016" PRIx64 " port %u (%s)\n",
							cl_ntoh16(p_pending->pkey),
							cl_ntoh64(osm_node_get_node_guid(p_node)),
							osm_physp_get_port_num(p_physp),
							p_physp->p_node->print_desc);
				} else
					found = TRUE;
			}
		}

		if (found) {
			if (IB_SUCCESS !=
			    osm_pkey_tbl_set_new_entry(p_pkey_tbl, block_index,
						       pkey_index,
						       p_pending->pkey)) {
				OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0505: "
					"Failed to set PKey 0x%04x in block %u idx %u "
					"for node 0x%016" PRIx64 " port %u (%s)\n",
					cl_ntoh16(p_pending->pkey), block_index,
					pkey_index,
					cl_ntoh64(osm_node_get_node_guid
						  (p_node)),
					osm_physp_get_port_num(p_physp),
					p_physp->p_node->print_desc);
			}
			if (ptr == NULL &&
			    CL_SUCCESS !=
			    osm_pkey_tbl_set_accum_pkeys(p_pkey_tbl,
							 p_pending->pkey,
							 block_index * IB_NUM_PKEY_ELEMENTS_IN_BLOCK + pkey_index)) {
				OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0508: "
					"Failed to set accum_pkeys PKey 0x%04x "
					"in block %u idx %u for node 0x%016"
					PRIx64 " port %u (%s)\n",
					cl_ntoh16(p_pending->pkey), block_index,
					pkey_index,
					cl_ntoh64(osm_node_get_node_guid(p_node)),
					osm_physp_get_port_num(p_physp),
					p_physp->p_node->print_desc);
			}
		}
		free(p_pending);
		p_pending =
		    (osm_pending_pkey_t *) cl_qlist_remove_head(&p_pkey_tbl->
								pending);
	}

	p_pkey_tbl->indx0_pkey = 0;
	/* now look for changes and store */
	for (block_index = 0; block_index < num_of_blocks; block_index++) {
		block = osm_pkey_tbl_block_get(p_pkey_tbl, block_index);
		new_block = osm_pkey_tbl_new_block_get(p_pkey_tbl, block_index);
		if (!new_block)
			new_block = &empty_block;
		if (block && !memcmp(new_block, block, sizeof(*block)))
			continue;

		status =
		    pkey_mgr_update_pkey_entry(sm, p_physp, new_block,
					       block_index);
		if (status == IB_SUCCESS)
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Updated pkey table block %u for node 0x%016"
				PRIx64 " port %u (%s)\n", block_index,
				cl_ntoh64(osm_node_get_node_guid(p_node)),
				osm_physp_get_port_num(p_physp),
				p_physp->p_node->print_desc);
		else {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0506: "
				"pkey_mgr_update_pkey_entry() failed to update "
				"pkey table block %u for node 0x%016" PRIx64
				" port %u (%s)\n", block_index,
				cl_ntoh64(osm_node_get_node_guid(p_node)),
				osm_physp_get_port_num(p_physp),
				p_physp->p_node->print_desc);
			ret = -1;
		}
	}

	return ret;
}

static int last_used_pkey_index(const osm_port_t * const p_port,
				const osm_pkey_tbl_t * p_pkey_tbl,
				uint16_t * p_last_index)
{
	ib_pkey_table_t *last_block;
	uint16_t index, last_index = 0;

	CL_ASSERT(p_last_index);

	last_block = osm_pkey_tbl_new_block_get(p_pkey_tbl,
						p_pkey_tbl->used_blocks - 1);
	if (!last_block)
		return 1;

	if (p_pkey_tbl->used_blocks == p_pkey_tbl->max_blocks)
		last_index = cl_ntoh16(p_port->p_node->node_info.partition_cap) % IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
	if (last_index == 0)
		last_index = IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
	index = last_index;
	do {
		index--;
		if (!ib_pkey_is_invalid(last_block->pkey_entry[index]))
			break;
	} while (index != 0);

	*p_last_index = index;
	return 0;
}

static int update_peer_block(osm_log_t * p_log, osm_sm_t * sm,
			     osm_physp_t * peer,
			     osm_pkey_tbl_t * p_peer_pkey_tbl,
			     ib_pkey_table_t * new_peer_block,
			     uint16_t peer_block_idx, osm_node_t * p_node)
{
	int ret = 0;
	ib_pkey_table_t *peer_block;

	peer_block = osm_pkey_tbl_block_get(p_peer_pkey_tbl, peer_block_idx);
	if (!peer_block ||
	    memcmp(peer_block, new_peer_block, sizeof(*peer_block))) {
		if (pkey_mgr_update_pkey_entry(sm, peer, new_peer_block,
					       peer_block_idx) != IB_SUCCESS) {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0509: "
				"pkey_mgr_update_pkey_entry() failed to update "
				"pkey table block %u for node 0x%016"
				PRIx64 " port %u (%s)\n",
				peer_block_idx,
				cl_ntoh64(osm_node_get_node_guid(p_node)),
				osm_physp_get_port_num(peer),
				p_node->print_desc);
			ret = -1;
		}
	}

	return ret;
}

static int new_pkey_exists(osm_pkey_tbl_t * p_pkey_tbl, ib_net16_t pkey)
{
	uint16_t num_blocks;
	uint16_t block_index;
	ib_pkey_table_t *block;
	uint16_t pkey_idx;

	num_blocks = (uint16_t) cl_ptr_vector_get_size(&p_pkey_tbl->new_blocks);
	for (block_index = 0; block_index < num_blocks; block_index++) {
		block = osm_pkey_tbl_new_block_get(p_pkey_tbl, block_index);
		if (!block)
			continue;

		for (pkey_idx = 0; pkey_idx < IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
		     pkey_idx++) {
			if (block->pkey_entry[pkey_idx] == pkey)
				return 1;
		}
	}
	return 0;
}

static int pkey_mgr_update_peer_port(osm_log_t * p_log, osm_sm_t * sm,
				     const osm_subn_t * p_subn,
				     const osm_port_t * const p_port,
				     osm_partition_enforce_type_enum enforce_type)
{
	osm_physp_t *p_physp, *peer;
	osm_node_t *p_node;
	ib_pkey_table_t *block;
	const osm_pkey_tbl_t *p_pkey_tbl;
	osm_pkey_tbl_t *p_peer_pkey_tbl;
	uint16_t block_index, peer_block_idx;
	uint16_t peer_max_blocks;
	uint16_t last_index;
	ib_pkey_table_t new_peer_block;
	uint16_t pkey_idx, peer_pkey_idx;
	ib_net16_t pkey, full_pkey;
	int ret = 0, loop_exit = 0;

	p_physp = p_port->p_physp;
	if (!p_physp)
		return -1;
	peer = osm_physp_get_remote(p_physp);
	if (!peer)
		return -1;
	p_node = osm_physp_get_node_ptr(peer);
	if (!p_node->sw || !p_node->sw->switch_info.enforce_cap)
		return 0;

	if (enforce_type == OSM_PARTITION_ENFORCE_TYPE_OFF) {
		pkey_mgr_enforce_partition(p_log, sm, peer, OSM_PARTITION_ENFORCE_TYPE_OFF);
		return ret;
	}

	p_pkey_tbl = osm_physp_get_pkey_tbl(p_physp);
	peer_max_blocks = pkey_mgr_get_physp_max_blocks(peer);
	p_peer_pkey_tbl = &peer->pkeys;
	peer_block_idx = 0;
	peer_pkey_idx = 0;
	for (block_index = 0; block_index < p_pkey_tbl->used_blocks;
	     block_index++) {
		if (loop_exit)
			break;
		block = osm_pkey_tbl_new_block_get(p_pkey_tbl, block_index);
		if (!block)
			continue;
		for (pkey_idx = 0; pkey_idx < IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
		     pkey_idx++) {
			pkey = block->pkey_entry[pkey_idx];
			if (ib_pkey_is_invalid(pkey))
				continue;
			if (!ib_pkey_is_full_member(pkey)) {
				full_pkey = pkey | IB_PKEY_TYPE_MASK;
				if (new_pkey_exists(&p_physp->pkeys, full_pkey))
					continue;
			}
			new_peer_block.pkey_entry[peer_pkey_idx] = pkey;
			if (peer_block_idx >= peer_max_blocks) {
				loop_exit = 1;
				break;
			}
			if (++peer_pkey_idx == IB_NUM_PKEY_ELEMENTS_IN_BLOCK) {
				if (update_peer_block(p_log, sm, peer,
						      p_peer_pkey_tbl,
						      &new_peer_block,
						      peer_block_idx, p_node))
					ret = -1;
				peer_pkey_idx = 0;
				peer_block_idx++;
			}
		}
	}

	if (peer_block_idx < peer_max_blocks) {
		if (peer_pkey_idx) {
			/* Handle partial last block */
			for (; peer_pkey_idx < IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
			     peer_pkey_idx++)
				new_peer_block.pkey_entry[peer_pkey_idx] = 0;
			if (update_peer_block(p_log, sm, peer, p_peer_pkey_tbl,
					      &new_peer_block, peer_block_idx,
					      p_node))
				ret = -1;
		} else
			peer_block_idx--;

		p_peer_pkey_tbl->used_blocks = peer_block_idx + 1;
		if (p_peer_pkey_tbl->used_blocks == peer_max_blocks) {
			/* Is last used pkey index beyond switch peer port capacity ? */
			if (!last_used_pkey_index(p_port, p_peer_pkey_tbl,
						  &last_index)) {
				last_index += peer_block_idx * IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
				if (cl_ntoh16(p_node->sw->switch_info.enforce_cap) <= last_index) {
					OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0507: "
						"Not enough pkey entries (%u <= %u) on switch 0x%016"
						PRIx64 " port %u (%s). Clearing Enforcement bit\n",
						cl_ntoh16(p_node->sw->switch_info.enforce_cap),
						last_index,
						cl_ntoh64(osm_node_get_node_guid(p_node)),
						osm_physp_get_port_num(peer),
						p_node->print_desc);
					enforce_type = OSM_PARTITION_ENFORCE_TYPE_OFF;
					ret = -1;
				}
			}
		}
	} else {
		p_peer_pkey_tbl->used_blocks = peer_max_blocks;
		enforce_type = OSM_PARTITION_ENFORCE_TYPE_OFF;
	}

	if (!ret)
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Pkey table was successfully updated for node 0x%016"
			PRIx64 " port %u (%s)\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)),
			osm_physp_get_port_num(peer), p_node->print_desc);

	if (pkey_mgr_enforce_partition(p_log, sm, peer, enforce_type))
		ret = -1;

	return ret;
}

int osm_pkey_mgr_process(IN osm_opensm_t * p_osm)
{
	cl_qmap_t *p_tbl;
	cl_map_item_t *p_next;
	osm_prtn_t *p_prtn;
	osm_port_t *p_port;
	osm_switch_t *p_sw;
	osm_physp_t *p_physp;
	osm_node_t *p_remote_node;
	uint8_t i;
	int ret = 0;

	CL_ASSERT(p_osm);

	OSM_LOG_ENTER(&p_osm->log);

	CL_PLOCK_EXCL_ACQUIRE(&p_osm->lock);

	if (osm_prtn_make_partitions(&p_osm->log, &p_osm->subn) != IB_SUCCESS) {
		OSM_LOG(&p_osm->log, OSM_LOG_ERROR, "ERR 0510: "
			"osm_prtn_make_partitions() failed\n");
		ret = -1;
		goto _err;
	}

	/* populate the pending pkey entries by scanning all partitions */
	p_tbl = &p_osm->subn.prtn_pkey_tbl;
	p_next = cl_qmap_head(p_tbl);
	while (p_next != cl_qmap_end(p_tbl)) {
		p_prtn = (osm_prtn_t *) p_next;
		p_next = cl_qmap_next(p_next);
		pkey_mgr_process_partition_table(&p_osm->log, &p_osm->sm,
						 p_prtn, FALSE);
		pkey_mgr_process_partition_table(&p_osm->log, &p_osm->sm,
						 p_prtn, TRUE);
	}

	/* calculate and set new pkey tables */
	p_tbl = &p_osm->subn.port_guid_tbl;
	p_next = cl_qmap_head(p_tbl);
	while (p_next != cl_qmap_end(p_tbl)) {
		p_port = (osm_port_t *) p_next;
		p_next = cl_qmap_next(p_next);
		if (pkey_mgr_update_port(&p_osm->log, &p_osm->sm, p_port))
			ret = -1;
		if ((osm_node_get_type(p_port->p_node) != IB_NODE_TYPE_SWITCH)
		    && pkey_mgr_update_peer_port(&p_osm->log, &p_osm->sm,
						 &p_osm->subn, p_port,
						 p_osm->subn.opt.part_enforce_enum))
			ret = -1;
	}

	/* clear partition enforcement on inter-switch links */
	p_tbl = &p_osm->subn.sw_guid_tbl;
	p_next = cl_qmap_head(p_tbl);
	while (p_next != cl_qmap_end(p_tbl)) {
		p_sw = (osm_switch_t *) p_next;
		p_next = cl_qmap_next(p_next);
		for (i = 1; i < p_sw->num_ports; i++) {
			p_physp = osm_node_get_physp_ptr(p_sw->p_node, i);
			if (p_physp && p_physp->p_remote_physp)
				p_remote_node = p_physp->p_remote_physp->p_node;
			else
				continue;

			if (osm_node_get_type(p_remote_node) != IB_NODE_TYPE_SWITCH)
				continue;

			if(! (p_physp->port_info.vl_enforce & 0xc ))
				continue;

			/* clear partition enforcement */
			if (pkey_mgr_enforce_partition(&p_osm->log, &p_osm->sm, p_physp, OSM_PARTITION_ENFORCE_TYPE_OFF))
				ret = -1;
		}
	}
_err:
	CL_PLOCK_RELEASE(&p_osm->lock);
	OSM_LOG_EXIT(&p_osm->log);
	return ret;
}
