/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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
 * 	Declaration of osm_subn_t.
 *	This object represents an IBA subnet.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_PKT_RANDOMIZER_H_
#define _OSM_PKT_RANDOMIZER_H_

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_map.h>
#include <complib/cl_ptr_vector.h>
#include <complib/cl_list.h>
#include <opensm/osm_log.h>
#include <opensm/osm_path.h>
#include <opensm/osm_madw.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Packet Randomizer
* NAME
*	Packet Randomizer
*
* DESCRIPTION
*	The Packet Randomizer object encapsulates the information needed for
*	randomly dropping packets for debug.
*
*	The Packet Randomizer object is not thread safe, thus callers must
*	provide serialization.
*
* AUTHOR
*	Yael Kalka, Mellanox
*
*********/
/****d* OpenSM: Pkt_Randomizer/osm_pkt_randomizer_t
* NAME
*	osm_pkt_randomizer_t
*
* DESCRIPTION
*	Packet randomizer structure. This structure contains the various
*  parameters needed by the packet randomizer.
*
* SYNOPSIS
*/
typedef struct _osm_pkt_randomizer {
	uint8_t osm_pkt_drop_rate;
	uint8_t osm_pkt_num_unstable_links;
	uint8_t osm_pkt_unstable_link_rate;
	osm_dr_path_t *fault_dr_paths;
	uint8_t num_paths_initialized;
} osm_pkt_randomizer_t;

/*
* FIELDS
*
*  osm_pkt_drop_rate
*     Used by the randomizer whether to drop a packet or not.
*     Taken from the global variable OSM_PKT_DROP_RATE. If not given or
*     if set to zero, the randomizer will not run.
*
*  osm_pkt_num_unstable_links
*     The number of unstable links to be drawn.
*     Taken from the global variable OSM_PKT_NUM_UNSTABLE_LINKS. default = 1.
*
*  osm_pkt_unstable_link_rate
*     Used by the randomizer whether to add a packet to the unstable links
*     list or not. Taken from the global variable OSM_PKT_UNSTABLE_LINK_RATE.
*     default = 20.
*
*	fault_dr_path
*		Array of osm_dr_path_t objects, that includes all the dr_paths
*     that are marked as errored.
*
*  num_paths_initialized
*     Describes the number of paths from the fault_dr_paths array that
*     have already been initialized.
*
* SEE ALSO
*	Packet Randomizer object
*********/

/****f* OpenSM: Pkt_Randomizer/osm_pkt_randomizer_init
* NAME
*	osm_pkt_randomizer_init
*
* DESCRIPTION
*	The osm_pkt_randomizer_init function initializes the Packet Randomizer object.
*
* SYNOPSIS
*/
ib_api_status_t
osm_pkt_randomizer_init(IN OUT osm_pkt_randomizer_t ** pp_pkt_randomizer,
			IN osm_log_t * p_log);
/*
* PARAMETERS
*  p_pkt_randomizer
*     [in] Pointer to the Packet Randomizer object to be initialized.
*
*	p_log
*		[in] Pointer to the log object.
*
* RETURN VALUE
*	None
*
* NOTES
*
* SEE ALSO
*
*********/

/****f* OpenSM: Pkt_Randomizer/osm_pkt_randomizer_destroy
* NAME
*	osm_pkt_randomizer_destroy
*
* DESCRIPTION
*	The osm_pkt_randomizer_destroy function destroys the Packet Randomizer object.
*
* SYNOPSIS
*/
void
osm_pkt_randomizer_destroy(IN osm_pkt_randomizer_t ** pp_pkt_randomizer,
			   IN osm_log_t * p_log);
/*
* PARAMETERS
*  p_pkt_randomizer
*     [in] Pointer to the Packet Randomizer object to be destroyed.
*
*	p_log
*		[in] Pointer to the log object.
*
* RETURN VALUE
*	None
*
* NOTES
*
* SEE ALSO
*
*********/

/****f* OpenSM: Pkt_Randomizer/osm_pkt_randomizer_madw_drop
* NAME
*	osm_pkt_randomizer_madw_drop
*
* DESCRIPTION
*	The osm_pkt_randomizer_madw_drop is base function of the packet
*  randomizer.
*  It decides according to different random criteria whether or not
*  the packet received should be dropped (according to its dr_path).
*  This function is relevant both for mads sent by the SM and mads
*  received by the SM.
*  It returns TRUE if the mad should be dropped, and FALSE otherwise.
*
* SYNOPSIS
*/
boolean_t
osm_pkt_randomizer_mad_drop(IN osm_log_t * p_log,
			    IN osm_pkt_randomizer_t * p_pkt_randomizer,
			    IN const ib_mad_t * p_mad);
/*
* PARAMETERS
*  p_subn
*     [in] Pointer to the Subnet object for this subnet.
*
*	p_log
*		[in] Pointer to the log object.
*
*  p_mad
*     [in] Pointer to the ib_mad_t mad to be checked.
*
* RETURN VALUE
*	TRUE if the mad should be dropped. FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*
*********/

END_C_DECLS
#endif				/* _OSM_PKT_RANDOMIZER_H */
