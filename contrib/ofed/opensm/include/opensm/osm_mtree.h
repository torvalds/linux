/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
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
 * 	Declaration of osm_mtree_t.
 *	This object represents multicast spanning tree.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_MTREE_H_
#define _OSM_MTREE_H_

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <opensm/osm_base.h>
#include <opensm/osm_switch.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
#define OSM_MTREE_LEAF ((void*)-1)
/****h* OpenSM/Multicast Tree
* NAME
*	Multicast Tree
*
* DESCRIPTION
*	The Multicast Tree object encapsulates the information needed by the
*	OpenSM to manage multicast fabric routes.  It is a tree structure
*	in which each node in the tree represents a switch, and may have a
*	varying number of children.
*
*	Multicast trees do not contain loops.
*
*	The Multicast Tree is not thread safe, thus callers must provide
*	serialization.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Multicast Tree/osm_mtree_node_t
* NAME
*	osm_mtree_node_t
*
* DESCRIPTION
*	The MTree Node object encapsulates the information needed by the
*	OpenSM for a particular switch in the multicast tree.
*
*	The MTree Node object is not thread safe, thus callers must provide
*	serialization.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_mtree_node {
	cl_map_item_t map_item;
	const osm_switch_t *p_sw;
	uint8_t max_children;
	struct osm_mtree_node *p_up;
	struct osm_mtree_node *child_array[1];
} osm_mtree_node_t;
/*
* FIELDS
*	map_item
*		Linkage for quick map.  MUST BE FIRST ELEMENT!!!
*
*	p_sw
*		Pointer to the switch represented by this tree node.
*
*	max_children
*		Maximum number of child nodes of this node.  Equal to the
*		the number of ports on the switch if the switch supports
*		multicast.  Equal to 1 (default route) if the switch does
*		not support multicast.
*
*	p_up
*		Pointer to the parent of this node.  If this pointer is
*		NULL, the node is at the root of the tree.
*
*	child_array
*		Array (indexed by port number) of pointers to the
*		child osm_mtree_node_t objects of this tree node, if any.
*
* SEE ALSO
*********/

/****f* OpenSM: Multicast Tree/osm_mtree_node_new
* NAME
*	osm_mtree_node_new
*
* DESCRIPTION
*	Returns an initialized a Multicast Tree object for use.
*
* SYNOPSIS
*/
osm_mtree_node_t *osm_mtree_node_new(IN const osm_switch_t * p_sw);
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch represented by this node.
*
* RETURN VALUES
*	Pointer to an initialized tree node.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Multicast Tree/osm_mtree_destroy
* NAME
*	osm_mtree_destroy
*
* DESCRIPTION
*	Destroys a Multicast Tree object given by the p_mtn
*
* SYNOPSIS
*/
void osm_mtree_destroy(IN osm_mtree_node_t * p_mtn);
/*
* PARAMETERS
*	p_mtn
*		[in] Pointer to an osm_mtree_node_t object to destroy.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Multicast Tree/osm_mtree_node_get_max_children
* NAME
*	osm_mtree_node_get_max_children
*
* DESCRIPTION
*	Returns the number maximum number of children of this node.
*	The return value is 1 greater than the highest valid port
*	number on the switch.
*
*
* SYNOPSIS
*/
static inline uint8_t
osm_mtree_node_get_max_children(IN const osm_mtree_node_t * p_mtn)
{
	return (p_mtn->max_children);
}
/*
* PARAMETERS
*	p_mtn
*		[in] Pointer to the multicast tree node.
*
* RETURN VALUES
*	See description.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Multicast Tree/osm_mtree_node_get_child
* NAME
*	osm_mtree_node_get_child
*
* DESCRIPTION
*	Returns the specified child node of this node.
*
* SYNOPSIS
*/
static inline osm_mtree_node_t *osm_mtree_node_get_child(IN const
							 osm_mtree_node_t *
							 p_mtn,
							 IN uint8_t child)
{
	CL_ASSERT(child < p_mtn->max_children);
	return (p_mtn->child_array[child]);
}
/*
* PARAMETERS
*	p_mtn
*		[in] Pointer to the multicast tree node.
*
*	child
*		[in] Index of the child to retrieve.
*
* RETURN VALUES
*	Returns the specified child node of this node.
*
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Multicast Tree/osm_mtree_node_get_switch_ptr
* NAME
*	osm_mtree_node_get_switch_ptr
*
* DESCRIPTION
*	Returns a pointer to the switch object represented by this tree node.
*
* SYNOPSIS
*/
static inline const osm_switch_t *osm_mtree_node_get_switch_ptr(IN const
							  osm_mtree_node_t *
							  p_mtn)
{
	return p_mtn->p_sw;
}
/*
* PARAMETERS
*	p_mtn
*		[in] Pointer to the multicast tree node.
*
*	child
*		[in] Index of the child to retrieve.
*
* RETURN VALUES
*	Returns a pointer to the switch object represented by this tree node.
*
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_MTREE_H_ */
