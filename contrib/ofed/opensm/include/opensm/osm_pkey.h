/*
 * Copyright (c) 2010 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2012 Mellanox Technologies LTD. All rights reserved.
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

#ifndef _OSM_PKEY_H_
#define _OSM_PKEY_H_

#include <iba/ib_types.h>
#include <complib/cl_dispatcher.h>
#include <complib/cl_map.h>
#include <opensm/osm_base.h>
#include <opensm/osm_log.h>
#include <opensm/osm_msgdef.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/*
   Forward references.
*/
struct osm_physp;
struct osm_port;
struct osm_subn;
struct osm_node;
struct osm_physp;

/*
 * Abstract:
 * 	Declaration of pkey manipulation functions.
 */

/****s* OpenSM: osm_pkey_tbl_t
* NAME
*	osm_pkey_tbl_t
*
* DESCRIPTION
*	This object represents a pkey table. The need for a special object
*  is required to optimize search performance of a PKey in the IB standard
*  non sorted table.
*
*	The osm_pkey_tbl_t object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_pkeybl {
	cl_map_t accum_pkeys;
	cl_ptr_vector_t blocks;
	cl_ptr_vector_t new_blocks;
	cl_map_t keys;
	cl_qlist_t pending;
	uint16_t last_pkey_idx;
	uint16_t used_blocks;
	uint16_t max_blocks;
	uint16_t rcv_blocks_cnt;
	uint16_t indx0_pkey;
} osm_pkey_tbl_t;
/*
* FIELDS
*	accum_pkeys
*		Accumulated pkeys with pkey index. Used to
*		preserve pkey index.
*
*	blocks
*		The IBA defined blocks of pkey values, updated from the subnet
*
*	new_blocks
*		The blocks of pkey values, will be used for updates by SM
*
*	keys
*		A set holding all keys
*
*	pending
*		A list of osm_pending_pkey structs that is temporarily set by
*		the pkey mgr and used during pkey mgr algorithm only
*
*	used_blocks
*		Tracks the number of blocks having non-zero pkeys
*
*	max_blocks
*		The maximal number of blocks this partition table might hold
*		this value is based on node_info (for port 0 or CA) or
*		switch_info updated on receiving the node_info or switch_info
*		GetResp
*
*	rcv_blocks_cnt
*		Counter for the received GetPKeyTable mads.
*		For every GetPKeyTable mad we send, increase the counter,
*		and for every GetRespPKeyTable we decrease the counter.
*
*	indx0_pkey
*		stores the pkey to be inserted at block 0 index 0.
*		if this field is 0, the default pkey will be inserted.
*
* NOTES
* 'blocks' vector should be used to store pkey values obtained from
* the port and SM pkey manager should not change it directly, for this
* purpose 'new_blocks' should be used.
*
* The only pkey values stored in 'blocks' vector will be mapped with
* 'keys' map
*
*********/

/****s* OpenSM: osm_pending_pkey_t
* NAME
*	osm_pending_pkey_t
*
* DESCRIPTION
*	This objects stores temporary information on pkeys, their target block,
*  and index during the pkey manager operation
*
* SYNOPSIS
*/
typedef struct osm_pending_pkey {
	cl_list_item_t list_item;
	uint16_t pkey;
	uint16_t block;
	uint8_t index;
	boolean_t is_new;
} osm_pending_pkey_t;
/*
* FIELDS
*	pkey
*		The actual P_Key
*
*	block
*		The block index based on the previous table extracted from the
*		device
*
*	index
*		The index of the pkey within the block
*
*	is_new
*		TRUE for new P_Keys such that the block and index are invalid
*		in that case
*
*********/

/****f* OpenSM: osm_pkey_tbl_construct
* NAME
*  osm_pkey_tbl_construct
*
* DESCRIPTION
*  Constructs the PKey table object
*
* SYNOPSIS
*/
void osm_pkey_tbl_construct(IN osm_pkey_tbl_t * p_pkey_tbl);
/*
*  p_pkey_tbl
*     [in] Pointer to osm_pkey_tbl_t object.
*
* NOTES
*
*********/

/****f* OpenSM: osm_pkey_tbl_init
* NAME
*  osm_pkey_tbl_init
*
* DESCRIPTION
*  Inits the PKey table object
*
* SYNOPSIS
*/
ib_api_status_t osm_pkey_tbl_init(IN osm_pkey_tbl_t * p_pkey_tbl);
/*
*  p_pkey_tbl
*     [in] Pointer to osm_pkey_tbl_t object.
*
* NOTES
*
*********/

/****f* OpenSM: osm_pkey_tbl_destroy
* NAME
*  osm_pkey_tbl_destroy
*
* DESCRIPTION
*  Destroys the PKey table object
*
* SYNOPSIS
*/
void osm_pkey_tbl_destroy(IN osm_pkey_tbl_t * p_pkey_tbl);
/*
*  p_pkey_tbl
*     [in] Pointer to osm_pkey_tbl_t object.
*
* NOTES
*
*********/

/****f* OpenSM: osm_pkey_tbl_get_num_blocks
* NAME
*  osm_pkey_tbl_get_num_blocks
*
* DESCRIPTION
*  Obtain the number of blocks in IB PKey table
*
* SYNOPSIS
*/
static inline uint16_t
osm_pkey_tbl_get_num_blocks(IN const osm_pkey_tbl_t * p_pkey_tbl)
{
	return ((uint16_t) (cl_ptr_vector_get_size(&p_pkey_tbl->blocks)));
}

/*
*  p_pkey_tbl
*     [in] Pointer to osm_pkey_tbl_t object.
*
* RETURN VALUES
*  The IB pkey table of that pkey table element
*
* NOTES
*
*********/

/****f* OpenSM: osm_pkey_tbl_block_get
* NAME
*  osm_pkey_tbl_block_get
*
* DESCRIPTION
*  Obtain the pointer to the IB PKey table block stored in the object
*
* SYNOPSIS
*/
static inline ib_pkey_table_t *osm_pkey_tbl_block_get(const osm_pkey_tbl_t *
						      p_pkey_tbl,
						      uint16_t block)
{
	return ((block < cl_ptr_vector_get_size(&p_pkey_tbl->blocks)) ?
		(ib_pkey_table_t *)cl_ptr_vector_get(
		&p_pkey_tbl->blocks, block) : NULL);
};

/*
*  p_pkey_tbl
*     [in] Pointer to osm_pkey_tbl_t object.
*
*  block
*     [in] The block number to get
*
* RETURN VALUES
*  The IB pkey table of that pkey table element
*
* NOTES
*
*********/

/****f* OpenSM: osm_pkey_tbl_new_block_get
* NAME
*  osm_pkey_tbl_new_block_get
*
* DESCRIPTION
*  The same as above but for new block
*
* SYNOPSIS
*/
static inline ib_pkey_table_t *osm_pkey_tbl_new_block_get(const osm_pkey_tbl_t *
							  p_pkey_tbl,
							  uint16_t block)
{
	return ((block < cl_ptr_vector_get_size(&p_pkey_tbl->new_blocks)) ?
		(ib_pkey_table_t *)cl_ptr_vector_get(
		&p_pkey_tbl->new_blocks, block) : NULL);
};

/****f* OpenSM: osm_pkey_find_last_accum_pkey_index
 * NAME
 *   osm_pkey_find_last_accum_pkey_index
 *
 * DESCRIPTION
 *   Finds the next last accumulated pkey
 *
 * SYNOPSIS
 */
void osm_pkey_find_last_accum_pkey_index(IN osm_pkey_tbl_t * p_pkey_tbl);


/****f* OpenSM: osm_pkey_tbl_set_accum_pkeys
* NAME
*  osm_pkey_tbl_set_accum_pkeys
*
* DESCRIPTION
*   Stores the given pkey and pkey index in the "accum_pkeys" array
*
* SYNOPSIS
*/
cl_status_t
osm_pkey_tbl_set_accum_pkeys(IN osm_pkey_tbl_t * p_pkey_tbl,
			     IN uint16_t pkey, IN uint16_t pkey_idx);
/*
* p_pkey_tbl
*   [in] Pointer to the PKey table
*
* pkey
*   [in] PKey to store
*
* pkey_idx
*   [in] The overall index
*
* RETURN VALUES
*   CL_SUCCESS if OK
*   CL_INSUFFICIENT_MEMORY if failed
*
*********/

/****f* OpenSM: osm_pkey_tbl_set_new_entry
* NAME
*  osm_pkey_tbl_set_new_entry
*
* DESCRIPTION
*   Stores the given pkey in the "new" blocks array and update
*   the "map" to show that on the "old" blocks
*
* SYNOPSIS
*/
ib_api_status_t
osm_pkey_tbl_set_new_entry(IN osm_pkey_tbl_t * p_pkey_tbl,
			   IN uint16_t block_idx,
			   IN uint8_t pkey_idx, IN uint16_t pkey);
/*
* p_pkey_tbl
*   [in] Pointer to the PKey table
*
* block_idx
*   [in] The block index to use
*
* pkey_idx
*   [in] The index within the block
*
* pkey
*   [in] PKey to store
*
* RETURN VALUES
*   IB_SUCCESS if OK
*   IB_ERROR if failed
*
*********/

/****f* OpenSM: osm_pkey_find_next_free_entry
* NAME
*  osm_pkey_find_next_free_entry
*
* DESCRIPTION
*  Find the next free entry in the PKey table starting at the given
*  index and block number. The user should increment pkey_idx before
*  next call
*  Inspect the "new" blocks array for empty space.
*
* SYNOPSIS
*/
boolean_t
osm_pkey_find_next_free_entry(IN osm_pkey_tbl_t * p_pkey_tbl,
			      OUT uint16_t * p_block_idx,
			      OUT uint8_t * p_pkey_idx);
/*
* p_pkey_tbl
*   [in] Pointer to the PKey table
*
* p_block_idx
*   [out] The block index to use
*
* p_pkey_idx
*   [out] The index within the block to use
*
* RETURN VALUES
*   TRUE if found
*   FALSE if did not find
*
*********/

/****f* OpenSM: osm_pkey_tbl_init_new_blocks
* NAME
*  osm_pkey_tbl_init_new_blocks
*
* DESCRIPTION
*  Initializes new_blocks vector content (allocate and clear)
*
* SYNOPSIS
*/
void osm_pkey_tbl_init_new_blocks(const osm_pkey_tbl_t * p_pkey_tbl);
/*
*  p_pkey_tbl
*     [in] Pointer to osm_pkey_tbl_t object.
*
* NOTES
*
*********/

/****f* OpenSM: osm_pkey_tbl_get_block_and_idx
* NAME
*  osm_pkey_tbl_get_block_and_idx
*
* DESCRIPTION
*  Set the block index and pkey index the given
*  pkey is found in. Return IB_NOT_FOUND if could
*  not find it, IB_SUCCESS if OK
*
* SYNOPSIS
*/
ib_api_status_t
osm_pkey_tbl_get_block_and_idx(IN osm_pkey_tbl_t * p_pkey_tbl,
			       IN uint16_t * p_pkey,
			       OUT uint16_t * block_idx,
			       OUT uint8_t * pkey_index);
/*
*  p_pkey_tbl
*     [in] Pointer to osm_pkey_tbl_t object.
*
*  p_pkey
*     [in] Pointer to the P_Key entry searched
*
*  p_block_idx
*     [out] Pointer to the block index to be updated
*
*  p_pkey_idx
*     [out] Pointer to the pkey index (in the block) to be updated
*
* NOTES
*
*********/

/****f* OpenSM: osm_pkey_tbl_set
* NAME
*  osm_pkey_tbl_set
*
* DESCRIPTION
*  Set the PKey table block provided in the PKey object.
*
* SYNOPSIS
*/
ib_api_status_t
osm_pkey_tbl_set(IN osm_pkey_tbl_t * p_pkey_tbl,
		 IN uint16_t block, IN ib_pkey_table_t * p_tbl,
		 IN boolean_t allow_both_pkeys);
/*
*  p_pkey_tbl
*     [in] Pointer to osm_pkey_tbl_t object
*
*  block
*     [in] The block number to set
*
*  p_tbl
*     [in] The IB PKey block to copy to the object
*
*  allow_both_pkeys
*     [in] Whether both full and limited membership on same partition
*          are allowed
*
* RETURN VALUES
*  IB_SUCCESS or IB_ERROR
*
* NOTES
*
*********/

/****f* OpenSM: osm_physp_share_this_pkey
* NAME
*  osm_physp_share_this_pkey
*
* DESCRIPTION
*  Checks if the given physical ports share the specified pkey.
*
* SYNOPSIS
*/
boolean_t osm_physp_share_this_pkey(IN const struct osm_physp * p_physp1,
				    IN const struct osm_physp * p_physp2,
				    IN ib_net16_t pkey,
				    IN boolean_t allow_both_pkeys);
/*
* PARAMETERS
*
*  p_physp1
*     [in] Pointer to an osm_physp_t object.
*
*  p_physp2
*     [in] Pointer to an osm_physp_t object.
*
*  pkey
*     [in] value of P_Key to check.
*
*  allow_both_pkeys
*     [in] whether both pkeys allowed policy is being used.
*
* RETURN VALUES
*  Returns TRUE if the two ports are matching.
*  FALSE otherwise.
*
* NOTES
*
*********/

/****f* OpenSM: osm_physp_find_common_pkey
* NAME
*  osm_physp_find_common_pkey
*
* DESCRIPTION
*  Returns first matching P_Key values for specified physical ports.
*
* SYNOPSIS
*/
ib_net16_t osm_physp_find_common_pkey(IN const struct osm_physp *p_physp1,
				      IN const struct osm_physp *p_physp2,
				      IN boolean_t allow_both_pkeys);
/*
* PARAMETERS
*
*  p_physp1
*     [in] Pointer to an osm_physp_t object.
*
*  p_physp2
*     [in] Pointer to an osm_physp_t object.
*
*  allow_both_pkeys
*     [in] Whether both full and limited membership on same partition
*          are allowed
*
* RETURN VALUES
*  Returns value of first shared P_Key or INVALID P_Key (0x0) if not
*  found.
*
* NOTES
*
*********/

/****f* OpenSM: osm_physp_share_pkey
* NAME
*  osm_physp_share_pkey
*
* DESCRIPTION
*  Checks if the given physical ports share a pkey.
*  The meaning P_Key matching:
*  10.9.3 :
*   In the following, let M_P_Key(Message P_Key) be the P_Key in the incoming
*   packet and E_P_Key(Endnode P_Key) be the P_Key it is being compared against
*   in the packet's destination endnode.
*
*    If:
*    * neither M_P_Key nor E_P_Key are the invalid P_Key
*    * and the low-order 15 bits of the M_P_Key match the low order 15
*      bits of the E_P_Key
*    * and the high order bit(membership type) of both the M_P_Key and
*      E_P_Key are not both 0 (i.e., both are not Limited members of
*      the partition)
*
*    then the P_Keys are said to match.
*
* SYNOPSIS
*/
boolean_t osm_physp_share_pkey(IN osm_log_t * p_log,
			       IN const struct osm_physp * p_physp_1,
			       IN const struct osm_physp * p_physp_2,
			       IN boolean_t allow_both_pkeys);

/*
* PARAMETERS
*  p_log
*     [in] Pointer to a log object.
*
*  p_physp_1
*     [in] Pointer to an osm_physp_t object.
*
*  p_physp_2
*     [in] Pointer to an osm_physp_t object.
*
*  allow_both_pkeys
*     [in] Whether both full and limited membership on same partition
*          are allowed
*
* RETURN VALUES
*  Returns TRUE if the 2 physical ports are matching.
*  FALSE otherwise.
*
* NOTES
*
*********/

/****f* OpenSM: osm_port_share_pkey
* NAME
*  osm_port_share_pkey
*
* DESCRIPTION
*  Checks if the given ports (on their default physical port) share a pkey.
*  The meaning P_Key matching:
*  10.9.3 :
*   In the following, let M_P_Key(Message P_Key) be the P_Key in the incoming
*   packet and E_P_Key(Endnode P_Key) be the P_Key it is being compared against
*   in the packet's destination endnode.
*
*    If:
*    * neither M_P_Key nor E_P_Key are the invalid P_Key
*    * and the low-order 15 bits of the M_P_Key match the low order 15
*      bits of the E_P_Key
*    * and the high order bit(membership type) of both the M_P_Key and
*      E_P_Key are not both 0 (i.e., both are not Limited members of
*      the partition)
*
*    then the P_Keys are said to match.
*
* SYNOPSIS
*/
boolean_t osm_port_share_pkey(IN osm_log_t * p_log,
			      IN const struct osm_port * p_port_1,
			      IN const struct osm_port * p_port_2,
			      IN boolean_t allow_both_pkeys);

/*
* PARAMETERS
*  p_log
*     [in] Pointer to a log object.
*
*  p_port_1
*     [in] Pointer to an osm_port_t object.
*
*  p_port_2
*     [in] Pointer to an osm_port_t object.
*
* RETURN VALUES
*  Returns TRUE if the 2 ports are matching.
*  FALSE otherwise.
*
* NOTES
*
*********/

/****f* OpenSM: osm_physp_has_pkey
* NAME
*  osm_physp_has_pkey
*
* DESCRIPTION
*	Given a physp and a pkey, check if pkey exists in physp pkey table
*
* SYNOPSIS
*/
boolean_t osm_physp_has_pkey(IN osm_log_t * p_log, IN ib_net16_t pkey,
			     IN const struct osm_physp *p_physp);

/*
* PARAMETERS
*  p_log
*     [in] Pointer to a log object.
*
*  pkey
*     [in] pkey number to look for.
*
*  p_physp
*     [in] Pointer to osm_physp_t object.
*
* RETURN VALUES
*  Returns TRUE if the p_physp has the pkey given. False otherwise.
*
* NOTES
*
*********/

/****f* OpenSM: osm_pkey_tbl_set_indx0_pkey
* NAME
*  osm_pkey_tbl_set_indx0_pkey
*
* DESCRIPTION
*  Sets given pkey at index0 in given pkey_tbl.
*
* SYNOPSIS
*/
void osm_pkey_tbl_set_indx0_pkey(IN osm_log_t * p_log, IN ib_net16_t pkey,
				 IN boolean_t full,
				 OUT osm_pkey_tbl_t * p_pkey_tbl);
/*
* PARAMETERS
*  p_log
*     [in] Pointer to a log object.
*
*  pkey
*     [in] P_Key.
*
*  full
*     [in] Indication if this is a full/limited membership pkey.
*
*  p_pkey_tbl
*     [out] Pointer to osm_pkey_tbl_t object in which to set indx0 pkey.
*
* RETURN VALUES
*  None
*
* NOTES
*
*********/
END_C_DECLS
#endif				/* _OSM_PKEY_H_ */
