/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2008 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
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
 *    Implementation of osm_lid_mgr_t.
 * This file implements the LID Manager object which is responsible for
 * assigning LIDs to all ports on the subnet.
 *
 * DATA STRUCTURES:
 *  p_subn->port_lid_tbl : a vector pointing from lid to its port.
 *  osm db guid2lid domain : a hash from guid to lid (min lid).
 *  p_subn->port_guid_tbl : a map from guid to discovered port obj.
 *
 * ALGORITHM:
 *
 * 0. we define a function to obtain the correct port lid:
 *    lid_mgr_get_port_lid( p_mgr, port, &min_lid, &max_lid ):
 *    0.1 if the port info lid matches the guid2lid return 0
 *    0.2 if the port info has a lid and that range is empty in
 *        port_lid_tbl, return 0 and update the port_lid_tbl and
 *        guid2lid
 *    0.3 else find an empty space in port_lid_tbl, update the
 *    port_lid_tbl and guid2lid, return 1 to flag a change required.
 *
 * 1. During initialization:
 *   1.1 initialize the guid2lid database domain.
 *   1.2 if reassign_lid is not set:
 *   1.2.1 read the persistent data for the domain.
 *   1.2.2 validate no duplicate use of lids and lids are 2^(lmc-1)
 *
 * 2. During SM port lid assignment:
 *   2.1 if reassign_lids is set, make it 2^lmc
 *   2.2 cleanup all port_lid_tbl and re-fill it according to guid2lid
 *   2.3 call lid_mgr_get_port_lid for the SM port
 *   2.4 set the port info
 *
 * 3. During all other ports lid assignment:
 *   3.1 go through all ports in the subnet
 *   3.1.1 call lid_mgr_get_port_lid
 *   3.1.2 if a change required send the port info
 *   3.2 if any change send the signal PENDING...
 *
 * 4. Store the guid2lid
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_LID_MGR_C
#include <opensm/osm_lid_mgr.h>
#include <opensm/osm_sm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_db_pack.h>

/**********************************************************************
  lid range item of qlist
 **********************************************************************/
typedef struct osm_lid_mgr_range {
	cl_list_item_t item;
	uint16_t min_lid;
	uint16_t max_lid;
} osm_lid_mgr_range_t;

void osm_lid_mgr_construct(IN osm_lid_mgr_t * p_mgr)
{
	memset(p_mgr, 0, sizeof(*p_mgr));
}

void osm_lid_mgr_destroy(IN osm_lid_mgr_t * p_mgr)
{
	cl_list_item_t *p_item;

	OSM_LOG_ENTER(p_mgr->p_log);

	while ((p_item = cl_qlist_remove_head(&p_mgr->free_ranges)) !=
	       cl_qlist_end(&p_mgr->free_ranges))
		free((osm_lid_mgr_range_t *) p_item);
	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
Validate the guid to lid data by making sure that under the current
LMC we did not get duplicates. If we do flag them as errors and remove
the entry.
**********************************************************************/
static void lid_mgr_validate_db(IN osm_lid_mgr_t * p_mgr)
{
	cl_qlist_t guids;
	osm_db_guid_elem_t *p_item;
	uint16_t lid;
	uint16_t min_lid;
	uint16_t max_lid;
	uint16_t lmc_mask;
	boolean_t lids_ok;
	uint8_t lmc_num_lids = (uint8_t) (1 << p_mgr->p_subn->opt.lmc);

	OSM_LOG_ENTER(p_mgr->p_log);

	lmc_mask = ~(lmc_num_lids - 1);

	cl_qlist_init(&guids);

	if (osm_db_guid2lid_guids(p_mgr->p_g2l, &guids)) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 0310: "
			"could not get guid list\n");
		goto Exit;
	}

	while ((p_item = (osm_db_guid_elem_t *) cl_qlist_remove_head(&guids))
	       != (osm_db_guid_elem_t *) cl_qlist_end(&guids)) {
		if (osm_db_guid2lid_get(p_mgr->p_g2l, p_item->guid,
					&min_lid, &max_lid))
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 0311: "
				"could not get lid for guid:0x%016" PRIx64 "\n",
				p_item->guid);
		else {
			lids_ok = TRUE;

			if (min_lid > max_lid || min_lid == 0
			    || p_item->guid == 0
			    || max_lid > p_mgr->p_subn->max_ucast_lid_ho) {
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
					"ERR 0312: "
					"Illegal LID range [%u:%u] for "
					"guid:0x%016" PRIx64 "\n", min_lid,
					max_lid, p_item->guid);
				lids_ok = FALSE;
			} else if (min_lid != max_lid
				   && (min_lid & lmc_mask) != min_lid) {
				/* check that if the lids define a range that is
				   valid for the current LMC mask */
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
					"ERR 0313: "
					"LID range [%u:%u] for guid:0x%016"
					PRIx64
					" is not aligned according to mask:0x%04x\n",
					min_lid, max_lid, p_item->guid,
					lmc_mask);
				lids_ok = FALSE;
			} else {
				/* check if the lids were not previously assigned */
				for (lid = min_lid; lid <= max_lid; lid++) {
					if (p_mgr->used_lids[lid]) {
						OSM_LOG(p_mgr->p_log,
							OSM_LOG_ERROR,
							"ERR 0314: "
							"0x%04x for guid:0x%016"
							PRIx64
							" was previously used\n",
							lid, p_item->guid);
						lids_ok = FALSE;
					}
				}
			}

			if (lids_ok)
				/* mark that it was visited */
				for (lid = min_lid; lid <= max_lid; lid++) {
					if (lid < min_lid + lmc_num_lids)
						p_mgr->used_lids[lid] = 1;
				}
			else if (osm_db_guid2lid_delete(p_mgr->p_g2l,
							p_item->guid))
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
					"ERR 0315: failed to delete entry for "
					"guid:0x%016" PRIx64 "\n",
					p_item->guid);
		}		/* got a lid */
		free(p_item);
	}			/* all guids */
Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}

ib_api_status_t osm_lid_mgr_init(IN osm_lid_mgr_t * p_mgr, IN osm_sm_t * sm)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sm->p_log);

	osm_lid_mgr_construct(p_mgr);

	p_mgr->sm = sm;
	p_mgr->p_log = sm->p_log;
	p_mgr->p_subn = sm->p_subn;
	p_mgr->p_db = sm->p_db;
	p_mgr->p_lock = sm->p_lock;

	/* we initialize and restore the db domain of guid to lid map */
	p_mgr->p_g2l = osm_db_domain_init(p_mgr->p_db, "guid2lid");
	if (!p_mgr->p_g2l) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 0316: "
			"Error initializing Guid-to-Lid persistent database\n");
		status = IB_ERROR;
		goto Exit;
	}

	cl_qlist_init(&p_mgr->free_ranges);

	/* we use the stored guid to lid table if not forced to reassign */
	if (!p_mgr->p_subn->opt.reassign_lids) {
		if (osm_db_restore(p_mgr->p_g2l)) {
#ifndef __WIN__
			/*
			 * When Windows is BSODing, it might corrupt files that
			 * were previously opened for writing, even if the files
			 * are closed, so we might see corrupted guid2lid file.
			 */
			if (p_mgr->p_subn->opt.exit_on_fatal) {
				osm_log_v2(p_mgr->p_log, OSM_LOG_SYS, FILE_ID,
					   "FATAL: Error restoring Guid-to-Lid "
					   "persistent database\n");
				status = IB_ERROR;
				goto Exit;
			} else
#endif
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
					"ERR 0317: Error restoring Guid-to-Lid "
					"persistent database\n");
		}

		/* we need to make sure we did not get duplicates with
		   current lmc */
		lid_mgr_validate_db(p_mgr);
	}

Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
	return status;
}

static uint16_t trim_lid(IN uint16_t lid)
{
	if (lid > IB_LID_UCAST_END_HO || lid < IB_LID_UCAST_START_HO)
		return 0;
	return lid;
}

/**********************************************************************
 initialize the manager for a new sweep:
 scans the known persistent assignment and port_lid_tbl
 re-calculate all empty ranges.
 cleanup invalid port_lid_tbl entries
**********************************************************************/
static int lid_mgr_init_sweep(IN osm_lid_mgr_t * p_mgr)
{
	cl_ptr_vector_t *p_discovered_vec = &p_mgr->p_subn->port_lid_tbl;
	uint16_t max_defined_lid, max_persistent_lid, max_discovered_lid;
	uint16_t disc_min_lid, disc_max_lid, db_min_lid, db_max_lid;
	int status = 0;
	cl_list_item_t *p_item;
	boolean_t is_free;
	osm_lid_mgr_range_t *p_range = NULL;
	osm_port_t *p_port;
	cl_qmap_t *p_port_guid_tbl;
	uint8_t lmc_num_lids = (uint8_t) (1 << p_mgr->p_subn->opt.lmc);
	uint16_t lmc_mask, req_lid, num_lids, lid;

	OSM_LOG_ENTER(p_mgr->p_log);

	lmc_mask = ~((1 << p_mgr->p_subn->opt.lmc) - 1);

	/* We must discard previous guid2lid db if this is the first master
	 * sweep and reassign_lids option is TRUE.
	 * If we came out of standby and honor_guid2lid_file option is TRUE, we
	 * must restore guid2lid db. Otherwise if honor_guid2lid_file option is
	 * FALSE we must discard previous guid2lid db.
	 */
	if (p_mgr->p_subn->first_time_master_sweep == TRUE &&
	    p_mgr->p_subn->opt.reassign_lids == TRUE) {
		osm_db_clear(p_mgr->p_g2l);
		memset(p_mgr->used_lids, 0, sizeof(p_mgr->used_lids));
	} else if (p_mgr->p_subn->coming_out_of_standby == TRUE) {
		osm_db_clear(p_mgr->p_g2l);
		memset(p_mgr->used_lids, 0, sizeof(p_mgr->used_lids));
		if (p_mgr->p_subn->opt.honor_guid2lid_file == FALSE)
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Ignore guid2lid file when coming out of standby\n");
		else {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Honor current guid2lid file when coming out "
				"of standby\n");
			if (osm_db_restore(p_mgr->p_g2l))
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
					"ERR 0306: "
					"Error restoring Guid-to-Lid "
					"persistent database. Ignoring it\n");
			lid_mgr_validate_db(p_mgr);
		}
	}

	/* we need to cleanup the empty ranges list */
	while ((p_item = cl_qlist_remove_head(&p_mgr->free_ranges)) !=
	       cl_qlist_end(&p_mgr->free_ranges))
		free((osm_lid_mgr_range_t *) p_item);

	/* first clean up the port_by_lid_tbl */
	for (lid = 0; lid < cl_ptr_vector_get_size(p_discovered_vec); lid++)
		cl_ptr_vector_set(p_discovered_vec, lid, NULL);

	/* we if are in the first sweep and in reassign lids mode
	   we should ignore all the available info and simply define one
	   huge empty range */
	if (p_mgr->p_subn->first_time_master_sweep == TRUE &&
	    p_mgr->p_subn->opt.reassign_lids == TRUE) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Skipping all lids as we are reassigning them\n");
		p_range = malloc(sizeof(osm_lid_mgr_range_t));
		if (p_range)
			p_range->min_lid = 1;
		goto AfterScanningLids;
	}

	/* go over all discovered ports and mark their entries */
	p_port_guid_tbl = &p_mgr->p_subn->port_guid_tbl;

	for (p_port = (osm_port_t *) cl_qmap_head(p_port_guid_tbl);
	     p_port != (osm_port_t *) cl_qmap_end(p_port_guid_tbl);
	     p_port = (osm_port_t *) cl_qmap_next(&p_port->map_item)) {
		osm_port_get_lid_range_ho(p_port, &disc_min_lid, &disc_max_lid);
		disc_min_lid = trim_lid(disc_min_lid);
		disc_max_lid = trim_lid(disc_max_lid);
		for (lid = disc_min_lid; lid <= disc_max_lid; lid++) {
			if (lid < disc_min_lid + lmc_num_lids)
				cl_ptr_vector_set(p_discovered_vec, lid, p_port);
			else
				cl_ptr_vector_set(p_discovered_vec, lid, NULL);
		}
		/* make sure the guid2lid entry is valid. If not, clean it. */
		if (osm_db_guid2lid_get(p_mgr->p_g2l,
					cl_ntoh64(osm_port_get_guid(p_port)),
					&db_min_lid, &db_max_lid))
			continue;

		if (!p_port->p_node->sw ||
		    osm_switch_sp0_is_lmc_capable(p_port->p_node->sw,
						  p_mgr->p_subn))
			num_lids = lmc_num_lids;
		else
			num_lids = 1;

		if (num_lids != 1 &&
		    ((db_min_lid & lmc_mask) != db_min_lid ||
		     db_max_lid - db_min_lid + 1 < num_lids)) {
			/* Not aligned, or not wide enough, then remove the entry */
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Cleaning persistent entry for guid:"
				"0x%016" PRIx64 " illegal range:[0x%x:0x%x]\n",
				cl_ntoh64(osm_port_get_guid(p_port)),
				db_min_lid, db_max_lid);
			osm_db_guid2lid_delete(p_mgr->p_g2l,
					       cl_ntoh64
					       (osm_port_get_guid(p_port)));
			for (lid = db_min_lid; lid <= db_max_lid; lid++)
				p_mgr->used_lids[lid] = 0;
		}
	}

	/*
	   Our task is to find free lid ranges.
	   A lid can be used if
	   1. a persistent assignment exists
	   2. the lid is used by a discovered port that does not have a
	   persistent assignment.

	   scan through all lid values of both the persistent table and
	   discovered table.
	   If the lid has an assigned port in the discovered table:
	   * make sure the lid matches the persistent table, or
	   * there is no other persistent assignment for that lid.
	   * else cleanup the port_by_lid_tbl, mark this as empty range.
	   Else if the lid does not have an entry in the persistent table
	   mark it as free.
	 */

	/* find the range of lids to scan */
	max_discovered_lid =
	    (uint16_t) cl_ptr_vector_get_size(p_discovered_vec);
	max_persistent_lid = sizeof(p_mgr->used_lids) - 1;

	/* but the vectors have one extra entry for lid=0 */
	if (max_discovered_lid)
		max_discovered_lid--;

	if (max_persistent_lid > max_discovered_lid)
		max_defined_lid = max_persistent_lid;
	else
		max_defined_lid = max_discovered_lid;

	for (lid = 1; lid <= max_defined_lid; lid++) {
		is_free = TRUE;
		/* first check to see if the lid is used by a persistent assignment */
		if (lid <= max_persistent_lid && p_mgr->used_lids[lid]) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"0x%04x is not free as its mapped by the "
				"persistent db\n", lid);
			is_free = FALSE;
			/* check this is a discovered port */
		} else if (lid <= max_discovered_lid &&
			   (p_port = cl_ptr_vector_get(p_discovered_vec,
						       lid))) {
			/* we have a port. Now lets see if we can preserve its lid range. */
			/* For that, we need to make sure:
			   1. The port has a (legal) persistency entry. Then the
			   local lid is free (we will use the persistency value).
			   2. Can the port keep its local assignment?
			   a. Make sure the lid is aligned.
			   b. Make sure all needed lids (for the lmc) are free
			   according to persistency table.
			 */
			/* qualify the guid of the port is not persistently
			   mapped to another range */
			if (!osm_db_guid2lid_get(p_mgr->p_g2l,
						 cl_ntoh64
						 (osm_port_get_guid(p_port)),
						 &db_min_lid, &db_max_lid)) {
				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"0x%04x is free as it was "
					"discovered but mapped by the "
					"persistent db to [0x%04x:0x%04x]\n",
					lid, db_min_lid, db_max_lid);
			} else {
				/* can the port keep its assignment ? */
				/* get the lid range of that port, and the
				   required number of lids we are about to
				   assign to it */
				osm_port_get_lid_range_ho(p_port,
							  &disc_min_lid,
							  &disc_max_lid);
				if (!p_port->p_node->sw ||
				    osm_switch_sp0_is_lmc_capable
				    (p_port->p_node->sw, p_mgr->p_subn)) {
					disc_max_lid =
					    disc_min_lid + lmc_num_lids - 1;
					num_lids = lmc_num_lids;
				} else
					num_lids = 1;

				/* Make sure the lid is aligned */
				if (num_lids != 1
				    && (disc_min_lid & lmc_mask) !=
				    disc_min_lid) {
					/* The lid cannot be used */
					OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
						"0x%04x is free as it was "
						"discovered but not aligned\n",
						lid);
				} else {
					/* check that all needed lids are not persistently mapped */
					is_free = FALSE;
					for (req_lid = disc_min_lid + 1;
					     req_lid <= disc_max_lid;
					     req_lid++) {
						if (req_lid <=
						    max_persistent_lid &&
						    p_mgr->used_lids[req_lid]) {
							OSM_LOG(p_mgr->p_log,
								OSM_LOG_DEBUG,
								"0x%04x is free as it was discovered "
								"but mapped\n",
								lid);
							is_free = TRUE;
							break;
						}
					}

					if (is_free == FALSE) {
						/* This port will use its local lid, and consume the entire required lid range.
						   Thus we can skip that range. */
						/* If the disc_max_lid is greater then lid, we can skip right to it,
						   since we've done all neccessary checks on the lids in between. */
						if (disc_max_lid > lid)
							lid = disc_max_lid;
					}
				}
			}
		}

		if (is_free) {
			if (p_range)
				p_range->max_lid = lid;
			else {
				p_range = malloc(sizeof(osm_lid_mgr_range_t));
				if (p_range) {
					p_range->min_lid = lid;
					p_range->max_lid = lid;
				}
			}
		/* this lid is used so we need to finalize the previous free range */
		} else if (p_range) {
			cl_qlist_insert_tail(&p_mgr->free_ranges,
					     &p_range->item);
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"new free lid range [%u:%u]\n",
				p_range->min_lid, p_range->max_lid);
			p_range = NULL;
		}
	}

AfterScanningLids:
	/* after scanning all known lids we need to extend the last range
	   to the max allowed lid */
	if (!p_range) {
		p_range = malloc(sizeof(osm_lid_mgr_range_t));
		/*
		   The p_range can be NULL in one of 2 cases:
		   1. If max_defined_lid == 0. In this case, we want the
		   entire range.
		   2. If all lids discovered in the loop where mapped. In this
		   case, no free range exists and we want to define it after the
		   last mapped lid.
		 */
		if (p_range)
			p_range->min_lid = lid;
	}
	if (p_range) {
		p_range->max_lid = p_mgr->p_subn->max_ucast_lid_ho;
		cl_qlist_insert_tail(&p_mgr->free_ranges, &p_range->item);
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"final free lid range [%u:%u]\n",
			p_range->min_lid, p_range->max_lid);
	}

	OSM_LOG_EXIT(p_mgr->p_log);
	return status;
}

/**********************************************************************
 check if the given range of lids is free
**********************************************************************/
static boolean_t lid_mgr_is_range_not_persistent(IN osm_lid_mgr_t * p_mgr,
						 IN uint16_t lid,
						 IN uint16_t num_lids)
{
	uint16_t i;

	for (i = lid; i < lid + num_lids; i++)
		if (p_mgr->used_lids[i])
			return FALSE;

	return TRUE;
}

/**********************************************************************
find a free lid range
**********************************************************************/
static void lid_mgr_find_free_lid_range(IN osm_lid_mgr_t * p_mgr,
					IN uint8_t num_lids,
					OUT uint16_t * p_min_lid,
					OUT uint16_t * p_max_lid)
{
	uint16_t lid;
	cl_list_item_t *p_item;
	cl_list_item_t *p_next_item;
	osm_lid_mgr_range_t *p_range = NULL;
	uint8_t lmc_num_lids;
	uint16_t lmc_mask;

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG, "LMC = %u, number LIDs = %u\n",
		p_mgr->p_subn->opt.lmc, num_lids);

	lmc_num_lids = (1 << p_mgr->p_subn->opt.lmc);
	lmc_mask = ~((1 << p_mgr->p_subn->opt.lmc) - 1);

	/*
	   Search the list of free lid ranges for a range which is big enough
	 */
	p_item = cl_qlist_head(&p_mgr->free_ranges);
	while (p_item != cl_qlist_end(&p_mgr->free_ranges)) {
		p_next_item = cl_qlist_next(p_item);
		p_range = (osm_lid_mgr_range_t *) p_item;

		lid = p_range->min_lid;

		/* if we require more then one lid we must align to LMC */
		if (num_lids > 1) {
			if ((lid & lmc_mask) != lid)
				lid = (lid + lmc_num_lids) & lmc_mask;
		}

		/* but we can be out of the range */
		if (lid + num_lids - 1 <= p_range->max_lid) {
			/* ok let us use that range */
			if (lid + num_lids - 1 == p_range->max_lid) {
				/* we consumed the entire range */
				cl_qlist_remove_item(&p_mgr->free_ranges,
						     p_item);
				free(p_item);
			} else
				/* only update the available range */
				p_range->min_lid = lid + num_lids;

			*p_min_lid = lid;
			*p_max_lid = (uint16_t) (lid + num_lids - 1);
			return;
		}
		p_item = p_next_item;
	}

	/*
	   Couldn't find a free range of lids.
	 */
	*p_min_lid = *p_max_lid = 0;
	/* if we run out of lids, give an error and abort! */
	OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 0307: "
		"OPENSM RAN OUT OF LIDS!!!\n");
	CL_ASSERT(0);
}

static void lid_mgr_cleanup_discovered_port_lid_range(IN osm_lid_mgr_t * p_mgr,
						      IN osm_port_t * p_port)
{
	cl_ptr_vector_t *p_discovered_vec = &p_mgr->p_subn->port_lid_tbl;
	uint16_t lid, min_lid, max_lid;
	uint16_t max_tbl_lid =
	    (uint16_t) (cl_ptr_vector_get_size(p_discovered_vec));

	osm_port_get_lid_range_ho(p_port, &min_lid, &max_lid);
	min_lid = trim_lid(min_lid);
	max_lid = trim_lid(max_lid);
	for (lid = min_lid; lid <= max_lid; lid++)
		if (lid < max_tbl_lid &&
		    p_port == cl_ptr_vector_get(p_discovered_vec, lid))
			cl_ptr_vector_set(p_discovered_vec, lid, NULL);
}

/**********************************************************************
 0.1 if the port info lid matches the guid2lid return 0
 0.2 if the port info has a lid and that range is empty in
     port_lid_tbl, return 0 and update the port_lid_tbl and
     guid2lid
 0.3 else find an empty space in port_lid_tbl, update the
 port_lid_tbl and guid2lid, return 1 to flag a change required.
**********************************************************************/
static int lid_mgr_get_port_lid(IN osm_lid_mgr_t * p_mgr,
				IN osm_port_t * p_port,
				OUT uint16_t * p_min_lid,
				OUT uint16_t * p_max_lid)
{
	uint16_t lid, min_lid, max_lid;
	uint64_t guid;
	uint8_t num_lids = (1 << p_mgr->p_subn->opt.lmc);
	int lid_changed = 0;
	uint16_t lmc_mask;

	OSM_LOG_ENTER(p_mgr->p_log);

	/* get the lid from the guid2lid */
	guid = cl_ntoh64(osm_port_get_guid(p_port));

	/* if the port is a base switch port 0 then we only need one lid */
	if (p_port->p_node->sw &&
	    !osm_switch_sp0_is_lmc_capable(p_port->p_node->sw, p_mgr->p_subn))
		num_lids = 1;

	if (p_mgr->p_subn->first_time_master_sweep == TRUE &&
	    p_mgr->p_subn->opt.reassign_lids == TRUE)
		goto AssignLid;

	lmc_mask = ~(num_lids - 1);

	/* if the port matches the guid2lid */
	if (!osm_db_guid2lid_get(p_mgr->p_g2l, guid, &min_lid, &max_lid)) {
		*p_min_lid = min_lid;
		*p_max_lid = min_lid + num_lids - 1;
		if (min_lid == cl_ntoh16(osm_port_get_base_lid(p_port)))
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG, "0x%016" PRIx64
				" matches its known lid:%u\n", guid, min_lid);
		else {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"0x%016" PRIx64 " with lid:%u "
				"does not match its known lid:%u\n",
				guid, cl_ntoh16(osm_port_get_base_lid(p_port)),
				min_lid);
			lid_mgr_cleanup_discovered_port_lid_range(p_mgr,
								  p_port);
			/* we still need to send the setting to the target port */
			lid_changed = 1;
		}
		goto NewLidSet;
	} else
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"0x%016" PRIx64 " has no persistent lid assigned\n",
			guid);

	/* if the port info carries a lid it must be lmc aligned and not mapped
	   by the persistent storage  */
	min_lid = cl_ntoh16(osm_port_get_base_lid(p_port));

	/* we want to ignore the discovered lid if we are also on first sweep of
	   reassign lids flow */
	if (min_lid) {
		/* make sure lid is valid */
		if ((min_lid & lmc_mask) == min_lid) {
			/* is it free */
			if (lid_mgr_is_range_not_persistent
			    (p_mgr, min_lid, num_lids)) {
				*p_min_lid = min_lid;
				*p_max_lid = min_lid + num_lids - 1;
				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"0x%016" PRIx64
					" lid range:[%u-%u] is free\n",
					guid, *p_min_lid, *p_max_lid);
				goto NewLidSet;
			} else
				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"0x%016" PRIx64 " existing lid "
					"range:[%u:%u] is not free\n",
					guid, min_lid, min_lid + num_lids - 1);
		} else
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"0x%016" PRIx64 " existing lid range:"
				"[%u:%u] is not lmc aligned\n",
				guid, min_lid, min_lid + num_lids - 1);
	}

AssignLid:
	/* first cleanup the existing discovered lid range */
	lid_mgr_cleanup_discovered_port_lid_range(p_mgr, p_port);

	/* find an empty space */
	lid_mgr_find_free_lid_range(p_mgr, num_lids, p_min_lid, p_max_lid);
	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"0x%016" PRIx64 " assigned a new lid range:[%u-%u]\n",
		guid, *p_min_lid, *p_max_lid);
	lid_changed = 1;

NewLidSet:
	/* update the guid2lid db and used_lids */
	osm_db_guid2lid_set(p_mgr->p_g2l, guid, *p_min_lid, *p_max_lid);
	for (lid = *p_min_lid; lid <= *p_max_lid; lid++)
		p_mgr->used_lids[lid] = 1;

	/* make sure the assigned lids are marked in port_lid_tbl */
	for (lid = *p_min_lid; lid <= *p_max_lid; lid++)
		cl_ptr_vector_set(&p_mgr->p_subn->port_lid_tbl, lid, p_port);

	OSM_LOG_EXIT(p_mgr->p_log);
	return lid_changed;
}

/**********************************************************************
 Set to INIT the remote port of the given physical port
 **********************************************************************/
static void lid_mgr_set_remote_pi_state_to_init(IN osm_lid_mgr_t * p_mgr,
						IN osm_physp_t * p_physp)
{
	osm_physp_t *p_rem_physp = osm_physp_get_remote(p_physp);

	if (p_rem_physp == NULL)
		return;

	/* but in some rare cases the remote side might be non responsive */
	ib_port_info_set_port_state(&p_rem_physp->port_info, IB_LINK_INIT);
}

static int lid_mgr_set_physp_pi(IN osm_lid_mgr_t * p_mgr,
				IN osm_port_t * p_port,
				IN osm_physp_t * p_physp, IN ib_net16_t lid)
{
	uint8_t payload[IB_SMP_DATA_SIZE];
	ib_port_info_t *p_pi = (ib_port_info_t *) payload;
	const ib_port_info_t *p_old_pi;
	osm_madw_context_t context;
	osm_node_t *p_node;
	ib_api_status_t status;
	uint8_t mtu;
	uint8_t op_vls;
	uint8_t port_num;
	boolean_t send_set = FALSE;
	boolean_t send_client_rereg = FALSE;
	boolean_t update_mkey = FALSE;
	int ret = 0;

	OSM_LOG_ENTER(p_mgr->p_log);

	/*
	   Don't bother doing anything if this Physical Port is not valid.
	   This allows simplified code in the caller.
	 */
	if (!p_physp)
		goto Exit;

	port_num = osm_physp_get_port_num(p_physp);
	p_node = osm_physp_get_node_ptr(p_physp);

	if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH && port_num != 0) {
		/*
		   Switch ports that are not numbered 0 should not be set
		   with the following attributes as they are set later
		   (during NO_CHANGE state in link mgr).
		 */
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Skipping switch port %u, GUID 0x%016" PRIx64 "\n",
			port_num, cl_ntoh64(osm_physp_get_port_guid(p_physp)));
		goto Exit;
	}

	p_old_pi = &p_physp->port_info;

	/*
	   First, copy existing parameters from the PortInfo attribute we
	   already have for this node.

	   Second, update with default values that we know must be set for
	   every Physical Port and the LID and set the neighbor MTU field
	   appropriately.

	   Third, send the SMP to this physical port.
	 */

	memcpy(payload, p_old_pi, sizeof(ib_port_info_t));

	/*
	   Should never write back a value that is bigger then 3 in
	   the PortPhysicalState field, so cannot simply copy!

	   Actually we want to write there:
	   port physical state - no change
	   link down default state = polling
	   port state - no change
	 */
	p_pi->state_info2 = 0x02;
	ib_port_info_set_port_state(p_pi, IB_LINK_NO_CHANGE);

	if (ib_port_info_get_link_down_def_state(p_pi) !=
	    ib_port_info_get_link_down_def_state(p_old_pi))
		send_set = TRUE;

	/* didn't get PortInfo before */
	if (!ib_port_info_get_port_state(p_old_pi))
		send_set = TRUE;

	p_pi->m_key = p_mgr->p_subn->opt.m_key;
	if (memcmp(&p_pi->m_key, &p_old_pi->m_key, sizeof(p_pi->m_key))) {
		update_mkey = TRUE;
		send_set = TRUE;
	}

	p_pi->subnet_prefix = p_mgr->p_subn->opt.subnet_prefix;
	if (memcmp(&p_pi->subnet_prefix, &p_old_pi->subnet_prefix,
		   sizeof(p_pi->subnet_prefix)))
		send_set = TRUE;

	p_port->lid = lid;
	p_pi->base_lid = lid;
	if (memcmp(&p_pi->base_lid, &p_old_pi->base_lid,
		   sizeof(p_pi->base_lid))) {
		/*
		 * Reset stored base_lid.
		 * On successful send, we'll update it when we'll get a reply.
		 */
		osm_physp_set_base_lid(p_physp, 0);
		send_set = TRUE;
		p_mgr->dirty = TRUE;
	}

	/*
	   We are updating the ports with our local sm_base_lid
	   if for some reason currently received SM LID is different from our SM LID,
	   need to send client reregister to this port
	*/
	p_pi->master_sm_base_lid = p_mgr->p_subn->sm_base_lid;
	if (memcmp(&p_pi->master_sm_base_lid, &p_old_pi->master_sm_base_lid,
		   sizeof(p_pi->master_sm_base_lid))) {
		send_client_rereg = TRUE;
		send_set = TRUE;
	}

	p_pi->m_key_lease_period = p_mgr->p_subn->opt.m_key_lease_period;
	if (memcmp(&p_pi->m_key_lease_period, &p_old_pi->m_key_lease_period,
		   sizeof(p_pi->m_key_lease_period)))
		send_set = TRUE;

	p_pi->mkey_lmc = 0;
	ib_port_info_set_mpb(p_pi, p_mgr->p_subn->opt.m_key_protect_bits);
	if (ib_port_info_get_mpb(p_pi) != ib_port_info_get_mpb(p_old_pi))
		send_set = TRUE;

	/*
	   we want to set the timeout for both the switch port 0
	   and the CA ports
	 */
	ib_port_info_set_timeout(p_pi, p_mgr->p_subn->opt.subnet_timeout);
	if (ib_port_info_get_timeout(p_pi) !=
	    ib_port_info_get_timeout(p_old_pi))
		send_set = TRUE;

	if (port_num != 0) {
		/*
		   CAs don't have a port 0, and for switch port 0,
		   the state bits are ignored.
		   This is not the switch management port
		 */
		p_pi->link_width_enabled = p_old_pi->link_width_supported;
		if (p_pi->link_width_enabled != p_old_pi->link_width_enabled)
			send_set = TRUE;

		/* p_pi->mkey_lmc is initialized earlier */
		ib_port_info_set_lmc(p_pi, p_mgr->p_subn->opt.lmc);
		if (ib_port_info_get_lmc(p_pi) !=
		    ib_port_info_get_lmc(p_old_pi))
			send_set = TRUE;

		/* calc new op_vls and mtu */
		op_vls = osm_physp_calc_link_op_vls(p_mgr->p_log, p_mgr->p_subn,
					      p_physp,
					      ib_port_info_get_op_vls(p_old_pi));
		mtu = osm_physp_calc_link_mtu(p_mgr->p_log, p_physp,
					      ib_port_info_get_neighbor_mtu(p_old_pi));

		ib_port_info_set_neighbor_mtu(p_pi, mtu);

		if (ib_port_info_get_neighbor_mtu(p_pi) !=
		    ib_port_info_get_neighbor_mtu(p_old_pi))
			send_set = TRUE;

		ib_port_info_set_op_vls(p_pi, op_vls);
		if (ib_port_info_get_op_vls(p_pi) !=
		    ib_port_info_get_op_vls(p_old_pi))
			send_set = TRUE;

		/*
		   Several timeout mechanisms:
		 */
		ib_port_info_set_phy_and_overrun_err_thd(p_pi,
							 p_mgr->p_subn->opt.
							 local_phy_errors_threshold,
							 p_mgr->p_subn->opt.
							 overrun_errors_threshold);

		if (p_pi->error_threshold != p_old_pi->error_threshold)
			send_set = TRUE;

		/*
		   To reset the port state machine we can send
		   PortInfo.State = DOWN. (see: 7.2.7 p171 lines:10-19)
		 */
		if (mtu != ib_port_info_get_neighbor_mtu(p_old_pi) ||
		    op_vls != ib_port_info_get_op_vls(p_old_pi)) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Sending Link Down to GUID 0x%016"
				PRIx64 " port %d due to op_vls or "
				"mtu change. MTU:%u,%u VL_CAP:%u,%u\n",
				cl_ntoh64(osm_physp_get_port_guid(p_physp)),
				port_num, mtu,
				ib_port_info_get_neighbor_mtu(p_old_pi),
				op_vls, ib_port_info_get_op_vls(p_old_pi));

			/*
			   we need to make sure the internal DB will follow the
			   fact that the remote port is also going through
			   "down" state into "init"...
			 */
			lid_mgr_set_remote_pi_state_to_init(p_mgr, p_physp);

			ib_port_info_set_port_state(p_pi, IB_LINK_DOWN);
			if (ib_port_info_get_port_state(p_pi) !=
			    ib_port_info_get_port_state(p_old_pi))
				send_set = TRUE;
		}
	} else if (ib_switch_info_is_enhanced_port0(&p_node->sw->switch_info)) {
		/*
		 * Configure Enh. SP0:
		 * Set MTU according to the mtu_cap.
		 * Set LMC if lmc_esp0 is defined.
		 */
		ib_port_info_set_neighbor_mtu(p_pi,
					      ib_port_info_get_mtu_cap
					      (p_old_pi));
		if (ib_port_info_get_neighbor_mtu(p_pi) !=
		    ib_port_info_get_neighbor_mtu(p_old_pi))
			send_set = TRUE;

		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Updating neighbor_mtu on switch GUID 0x%016" PRIx64
			" port 0 to:%u\n",
			cl_ntoh64(osm_physp_get_port_guid(p_physp)),
			ib_port_info_get_neighbor_mtu(p_pi));

		/* Configure LMC on enhanced SP0 */
		if (p_mgr->p_subn->opt.lmc_esp0) {
			/* p_pi->mkey_lmc is initialized earlier */
			ib_port_info_set_lmc(p_pi, p_mgr->p_subn->opt.lmc);
			if (ib_port_info_get_lmc(p_pi) !=
			    ib_port_info_get_lmc(p_old_pi))
				send_set = TRUE;
		}
	}

	context.pi_context.node_guid = osm_node_get_node_guid(p_node);
	context.pi_context.port_guid = osm_physp_get_port_guid(p_physp);
	context.pi_context.set_method = TRUE;
	context.pi_context.light_sweep = FALSE;
	context.pi_context.active_transition = FALSE;

	/*
	  For ports supporting the ClientReregistration Vol1 (v1.2) p811 14.4.11:
	  need to set the cli_rereg bit when current SM LID at the Host
	  is different from our SM LID,
	  also if we are in first_time_master_sweep,
	  also if this port was just now discovered, then we should also set
	  the cli_rereg bit (we know that the port was just discovered
	  if its is_new field is set).
	*/
	if  ((send_client_rereg ||
	    p_mgr->p_subn->first_time_master_sweep == TRUE || p_port->is_new)
	    && !p_mgr->p_subn->opt.no_clients_rereg
	    && (p_old_pi->capability_mask & IB_PORT_CAP_HAS_CLIENT_REREG)) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Setting client rereg on %s, port %d\n",
			p_port->p_node->print_desc, p_port->p_physp->port_num);
		ib_port_info_set_client_rereg(p_pi, 1);
		context.pi_context.client_rereg = TRUE;
		send_set = TRUE;
	} else {
		ib_port_info_set_client_rereg(p_pi, 0);
		context.pi_context.client_rereg = FALSE;
	}

	/* We need to send the PortInfo Set request with the new sm_lid
	   in the following cases:
	   1. There is a change in the values (send_set == TRUE)
	   2. first_time_master_sweep flag on the subnet is TRUE. This means the
	   SM just became master, and it then needs to send a PortInfo Set to
	   every port.
	 */
	if (p_mgr->p_subn->first_time_master_sweep == TRUE)
		send_set = TRUE;

	if (!send_set)
		goto Exit;

	status = osm_req_set(p_mgr->sm, osm_physp_get_dr_path_ptr(p_physp),
			     payload, sizeof(payload), IB_MAD_ATTR_PORT_INFO,
			     cl_hton32(osm_physp_get_port_num(p_physp)),
			     FALSE, ib_port_info_get_m_key(&p_physp->port_info),
			     CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		ret = -1;
	/* If we sent a new mkey above, update our guid2mkey map
	   now, on the assumption that the SubnSet succeeds
	*/
	if (update_mkey)
		osm_db_guid2mkey_set(p_mgr->p_subn->p_g2m,
				     cl_ntoh64(p_physp->port_guid),
				     cl_ntoh64(p_pi->m_key));

Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
	return ret;
}

/**********************************************************************
 Processes our own node
 Lock must already be held.
**********************************************************************/
static int lid_mgr_process_our_sm_node(IN osm_lid_mgr_t * p_mgr)
{
	osm_port_t *p_port;
	uint16_t min_lid_ho;
	uint16_t max_lid_ho;
	int ret;

	OSM_LOG_ENTER(p_mgr->p_log);

	/*
	   Acquire our own port object.
	 */
	p_port = osm_get_port_by_guid(p_mgr->p_subn,
				      p_mgr->p_subn->sm_port_guid);
	if (!p_port) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 0308: "
			"Can't acquire SM's port object, GUID 0x%016" PRIx64
			"\n", cl_ntoh64(p_mgr->p_subn->sm_port_guid));
		ret = -1;
		goto Exit;
	}

	/*
	   Determine the LID this SM will use for its own port.
	   Be careful.  With an LMC > 0, the bottom of the LID range becomes
	   unusable, since port hardware will mask off least significant bits,
	   leaving a LID of 0 (invalid).  Therefore, make sure that we always
	   configure the SM with a LID that has non-zero bits, even after
	   LMC masking by hardware.
	 */
	lid_mgr_get_port_lid(p_mgr, p_port, &min_lid_ho, &max_lid_ho);
	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"Current base LID is %u\n", min_lid_ho);
	/*
	   Update subnet object.
	 */
	p_mgr->p_subn->master_sm_base_lid = cl_hton16(min_lid_ho);
	p_mgr->p_subn->sm_base_lid = cl_hton16(min_lid_ho);

	OSM_LOG(p_mgr->p_log, OSM_LOG_VERBOSE,
		"Assigning SM's port 0x%016" PRIx64
		"\n\t\t\t\tto LID range [%u,%u]\n",
		cl_ntoh64(osm_port_get_guid(p_port)), min_lid_ho, max_lid_ho);

	/*
	   Set the PortInfo the Physical Port associated with this Port.
	 */
	ret = lid_mgr_set_physp_pi(p_mgr, p_port, p_port->p_physp,
				   cl_hton16(min_lid_ho));

Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
	return ret;
}

int osm_lid_mgr_process_sm(IN osm_lid_mgr_t * p_mgr)
{
	int ret;

	OSM_LOG_ENTER(p_mgr->p_log);

	CL_ASSERT(p_mgr->p_subn->sm_port_guid);

	CL_PLOCK_EXCL_ACQUIRE(p_mgr->p_lock);

	/* initialize the port_lid_tbl and empty ranges list following the
	   persistent db */
	lid_mgr_init_sweep(p_mgr);

	ret = lid_mgr_process_our_sm_node(p_mgr);

	CL_PLOCK_RELEASE(p_mgr->p_lock);

	OSM_LOG_EXIT(p_mgr->p_log);
	return ret;
}

/**********************************************************************
 1 go through all ports in the subnet.
 1.1 call lid_mgr_get_port_lid
 1.2 if a change is required send the port info
 2 if any change send the signal PENDING...
**********************************************************************/
int osm_lid_mgr_process_subnet(IN osm_lid_mgr_t * p_mgr)
{
	cl_qmap_t *p_port_guid_tbl;
	osm_port_t *p_port;
	ib_net64_t port_guid;
	int lid_changed, ret = 0;
	uint16_t min_lid_ho, max_lid_ho;

	CL_ASSERT(p_mgr);

	OSM_LOG_ENTER(p_mgr->p_log);

	CL_PLOCK_EXCL_ACQUIRE(p_mgr->p_lock);

	CL_ASSERT(p_mgr->p_subn->sm_port_guid);

	p_port_guid_tbl = &p_mgr->p_subn->port_guid_tbl;

	for (p_port = (osm_port_t *) cl_qmap_head(p_port_guid_tbl);
	     p_port != (osm_port_t *) cl_qmap_end(p_port_guid_tbl);
	     p_port = (osm_port_t *) cl_qmap_next(&p_port->map_item)) {
		port_guid = osm_port_get_guid(p_port);

		/*
		   Our own port is a special case in that we want to
		   assign a LID to ourselves first, since we have to
		   advertise that LID value to the other ports.

		   For that reason, our node is treated separately and
		   we will not add it to any of these lists.
		 */
		if (port_guid == p_mgr->p_subn->sm_port_guid) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Skipping our own port 0x%016" PRIx64 "\n",
				cl_ntoh64(port_guid));
			continue;
		}

		/*
		   get the port lid range - we need to send it on first active
		   sweep or if there was a change (the result of
		   lid_mgr_get_port_lid)
		 */
		lid_changed = lid_mgr_get_port_lid(p_mgr, p_port,
						   &min_lid_ho, &max_lid_ho);

		/* we can call the function to update the port info as it known
		   to look for any field change and will only send an updated
		   if required */
		OSM_LOG(p_mgr->p_log, OSM_LOG_VERBOSE,
			"Assigned port 0x%016" PRIx64 ", %s LID [%u,%u]\n",
			cl_ntoh64(port_guid), lid_changed ? "new" : "",
			min_lid_ho, max_lid_ho);

		/* the proc returns the fact it sent a set port info */
		if (lid_mgr_set_physp_pi(p_mgr, p_port, p_port->p_physp,
					 cl_hton16(min_lid_ho)))
			ret = -1;
	}			/* all ports */

	/* store the guid to lid table in persistent db */
	osm_db_store(p_mgr->p_g2l, p_mgr->p_subn->opt.fsync_high_avail_files);

	CL_PLOCK_RELEASE(p_mgr->p_lock);

	OSM_LOG_EXIT(p_mgr->p_log);
	return ret;
}
