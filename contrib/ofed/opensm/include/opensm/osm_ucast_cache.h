/*
 * Copyright (c) 2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2008 Mellanox Technologies LTD. All rights reserved.
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
 * 	Header file that describes Unicast Cache functions.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_UCAST_CACHE_H_
#define _OSM_UCAST_CACHE_H_

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <opensm/osm_switch.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

struct osm_ucast_mgr;

/****h* OpenSM/Unicast Manager/Unicast Cache
* NAME
*	Unicast Cache
*
* DESCRIPTION
*	The Unicast Cache object encapsulates the information
*	needed to cache and write unicast routing of the subnet.
*
*	The Unicast Cache object is NOT thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Yevgeny Kliteynik, Mellanox
*
*********/

/****f* OpenSM: Unicast Cache/osm_ucast_cache_invalidate
* NAME
*	osm_ucast_cache_invalidate
*
* DESCRIPTION
*	The osm_ucast_cache_invalidate function purges the
*	unicast cache and marks the cache as invalid.
*
* SYNOPSIS
*/
void osm_ucast_cache_invalidate(struct osm_ucast_mgr *p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to the ucast mgr object.
*
* RETURN VALUE
*	This function does not return any value.
*
* NOTES
*
* SEE ALSO
*	Unicast Manager object
*********/

/****f* OpenSM: Unicast Cache/osm_ucast_cache_check_new_link
* NAME
*	osm_ucast_cache_check_new_link
*
* DESCRIPTION
*	The osm_ucast_cache_check_new_link checks whether
*	the newly discovered link still allows us to use
*	cached unicast routing.
*
* SYNOPSIS
*/
void osm_ucast_cache_check_new_link(struct osm_ucast_mgr *p_mgr,
				    osm_node_t * p_node_1, uint8_t port_num_1,
				    osm_node_t * p_node_2, uint8_t port_num_2);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to the unicast manager object.
*
*	physp1
*		[in] Pointer to the first physical port of the link.
*
*	physp2
*		[in] Pointer to the second physical port of the link.
*
* RETURN VALUE
*	This function does not return any value.
*
* NOTES
*	The function checks whether the link was previously
*	cached/dropped or is this a completely new link.
*	If it decides that the new link makes cached routing
*	invalid, the cache is purged and marked as invalid.
*
* SEE ALSO
*	Unicast Cache object
*********/

/****f* OpenSM: Unicast Cache/osm_ucast_cache_add_link
* NAME
*	osm_ucast_cache_add_link
*
* DESCRIPTION
*	The osm_ucast_cache_add_link adds link to the cache.
*
* SYNOPSIS
*/
void osm_ucast_cache_add_link(struct osm_ucast_mgr *p_mgr,
			      osm_physp_t * physp1, osm_physp_t * physp2);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to the unicast manager object.
*
*	physp1
*		[in] Pointer to the first physical port of the link.
*
*	physp2
*		[in] Pointer to the second physical port of the link.
*
* RETURN VALUE
*	This function does not return any value.
*
* NOTES
*	Since the cache operates with ports and not links,
*	the function adds two port entries (both sides of the
*	link) to the cache.
*	If it decides that the dropped link makes cached routing
*	invalid, the cache is purged and marked as invalid.
*
* SEE ALSO
*	Unicast Manager object
*********/

/****f* OpenSM: Unicast Cache/osm_ucast_cache_add_node
* NAME
*	osm_ucast_cache_add_node
*
* DESCRIPTION
*	The osm_ucast_cache_add_node adds node and all
*	its links to the cache.
*
* SYNOPSIS
*/
void osm_ucast_cache_add_node(struct osm_ucast_mgr *p_mgr, osm_node_t * p_node);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to the unicast manager object.
*
*	p_node
*		[in] Pointer to the node object that should be cached.
*
* RETURN VALUE
*	This function does not return any value.
*
* NOTES
*	If the function decides that the dropped node makes cached
*	routing invalid, the cache is purged and marked as invalid.
*
* SEE ALSO
*	Unicast Manager object
*********/

/****f* OpenSM: Unicast Cache/osm_ucast_cache_process
* NAME
*	osm_ucast_cache_process
*
* DESCRIPTION
*	The osm_ucast_cache_process function writes the
*	cached unicast routing on the subnet switches.
*
* SYNOPSIS
*/
int osm_ucast_cache_process(struct osm_ucast_mgr *p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to the unicast manager object.
*
* RETURN VALUE
*	This function returns zero on sucess and non-zero
*	value otherwise.
*
* NOTES
*	Iterates through all the subnet switches and writes
*	the LFTs that were calculated during the last routing
*       engine execution to the switches.
*
* SEE ALSO
*	Unicast Manager object
*********/

END_C_DECLS
#endif				/* _OSM_UCAST_CACHE_H_ */
