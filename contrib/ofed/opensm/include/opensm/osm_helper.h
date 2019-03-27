/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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

#ifndef _OSM_HELPER_H_
#define _OSM_HELPER_H_

#include <iba/ib_types.h>
#include <complib/cl_dispatcher.h>
#include <opensm/osm_base.h>
#include <opensm/osm_log.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_path.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/*
 * Abstract:
 * 	Declaration of helpful functions.
 */
/****f* OpenSM: Helper/ib_get_sa_method_str
 * NAME
 *	ib_get_sa_method_str
 *
 * DESCRIPTION
 *	Returns a string for the specified SA Method value.
 *
 * SYNOPSIS
 */
const char *ib_get_sa_method_str(IN uint8_t method);
/*
 * PARAMETERS
 *	method
 *		[in] Network order METHOD ID value.
 *
 * RETURN VALUES
 *	Pointer to the method string.
 *
 * NOTES
 *
 * SEE ALSO
 *********/

/****f* OpenSM: Helper/ib_get_sm_method_str
* NAME
*	ib_get_sm_method_str
*
* DESCRIPTION
*	Returns a string for the specified SM Method value.
*
* SYNOPSIS
*/
const char *ib_get_sm_method_str(IN uint8_t method);
/*
* PARAMETERS
*	method
*		[in] Network order METHOD ID value.
*
* RETURN VALUES
*	Pointer to the method string.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/ib_get_sm_attr_str
* NAME
*	ib_get_sm_attr_str
*
* DESCRIPTION
*	Returns a string for the specified SM attribute value.
*
* SYNOPSIS
*/
const char *ib_get_sm_attr_str(IN ib_net16_t attr);
/*
* PARAMETERS
*	attr
*		[in] Network order attribute ID value.
*
* RETURN VALUES
*	Pointer to the attribute string.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/ib_get_sa_attr_str
* NAME
*	ib_get_sa_attr_str
*
* DESCRIPTION
*	Returns a string for the specified SA attribute value.
*
* SYNOPSIS
*/
const char *ib_get_sa_attr_str(IN ib_net16_t attr);
/*
* PARAMETERS
*	attr
*		[in] Network order attribute ID value.
*
* RETURN VALUES
*	Pointer to the attribute string.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/ib_get_trap_str
* NAME
*	ib_get_trap_str
*
* DESCRIPTION
*	Returns a name for the specified trap.
*
* SYNOPSIS
*/
const char *ib_get_trap_str(uint16_t trap_num);
/*
* PARAMETERS
*	trap_num
*		[in] Network order trap number.
*
* RETURN VALUES
*	Name of the trap.
*
*********/

extern const ib_gid_t ib_zero_gid;

/****f* IBA Base: Types/ib_gid_is_notzero
* NAME
*	ib_gid_is_notzero
*
* DESCRIPTION
*	Returns a boolean indicating whether or not the GID is zero.
*
* SYNOPSIS
*/
static inline boolean_t ib_gid_is_notzero(IN const ib_gid_t * p_gid)
{
	return memcmp(p_gid, &ib_zero_gid, sizeof(*p_gid));
}

/*
* PARAMETERS
*	p_gid
*		[in] Pointer to the GID object.
*
* RETURN VALUES
*	Returns TRUE if GID is not zero.
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*	ib_gid_t
*********/

/****f* OpenSM: Helper/osm_dump_port_info
* NAME
*	osm_dump_port_info
*
* DESCRIPTION
*	Dumps the PortInfo attribute to the log.
*
* SYNOPSIS
*/
void osm_dump_port_info(IN osm_log_t * p_log, IN ib_net64_t node_guid,
			IN ib_net64_t port_guid, IN uint8_t port_num,
			IN const ib_port_info_t * p_pi,
			IN osm_log_level_t log_level);

void osm_dump_port_info_v2(IN osm_log_t * p_log, IN ib_net64_t node_guid,
			   IN ib_net64_t port_guid, IN uint8_t port_num,
			   IN const ib_port_info_t * p_pi,
			   IN const int file_id,
			   IN osm_log_level_t log_level);

/*
* PARAMETERS
*	p_log
*		[in] Pointer to the osm_log_t object
*
*	node_guid
*		[in] Node GUID that owns this port.
*
*	port_guid
*		[in] Port GUID for this port.
*
*	port_num
*		[in] Port number for this port.
*
*	p_pi
*		[in] Pointer to the PortInfo attribute
*
*	log_level
*		[in] Log verbosity level with which to dump the data.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/osm_dump_guid_info
* NAME
*	osm_dump_guid_info
*
* DESCRIPTION
*	Dumps the GUIDInfo attribute to the log.
*
* SYNOPSIS
*/
void osm_dump_guid_info(IN osm_log_t * p_log, IN ib_net64_t node_guid,
			IN ib_net64_t port_guid, IN uint8_t block_num,
			IN const ib_guid_info_t * p_gi,
			IN osm_log_level_t log_level);

void osm_dump_guid_info_v2(IN osm_log_t * p_log, IN ib_net64_t node_guid,
			   IN ib_net64_t port_guid, IN uint8_t block_num,
			   IN const ib_guid_info_t * p_gi,
			   IN const int file_id,
			   IN osm_log_level_t log_level);
/*
* PARAMETERS
*	p_log
*		[in] Pointer to the osm_log_t object.
*
*	node_guid
*		[in] Node GUID that owns this port.
*
*	port_guid
*		[in] Port GUID for this port.
*
*	block_num
*		[in] Block number.
*
*	p_gi
*		[in] Pointer to the GUIDInfo attribute.
*
*	log_level
*		[in] Log verbosity level with which to dump the data.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

void osm_dump_mlnx_ext_port_info(IN osm_log_t * p_log, IN ib_net64_t node_guid,
				 IN ib_net64_t port_guid, IN uint8_t port_num,
				 IN const ib_mlnx_ext_port_info_t * p_pi,
				 IN osm_log_level_t log_level);

void osm_dump_mlnx_ext_port_info_v2(IN osm_log_t * p_log, IN ib_net64_t node_guid,
				    IN ib_net64_t port_guid, IN uint8_t port_num,
				    IN const ib_mlnx_ext_port_info_t * p_pi,
				    IN const int file_id,
				    IN osm_log_level_t log_level);

void osm_dump_path_record(IN osm_log_t * p_log, IN const ib_path_rec_t * p_pr,
			  IN osm_log_level_t log_level);

void osm_dump_path_record_v2(IN osm_log_t * p_log, IN const ib_path_rec_t * p_pr,
			     IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_multipath_record(IN osm_log_t * p_log,
			       IN const ib_multipath_rec_t * p_mpr,
			       IN osm_log_level_t log_level);

void osm_dump_multipath_record_v2(IN osm_log_t * p_log,
				  IN const ib_multipath_rec_t * p_mpr,
				  IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_node_record(IN osm_log_t * p_log,
			  IN const ib_node_record_t * p_nr,
			  IN osm_log_level_t log_level);

void osm_dump_node_record_v2(IN osm_log_t * p_log,
			     IN const ib_node_record_t * p_nr,
			     IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_mc_record(IN osm_log_t * p_log, IN const ib_member_rec_t * p_mcmr,
			IN osm_log_level_t log_level);

void osm_dump_mc_record_v2(IN osm_log_t * p_log, IN const ib_member_rec_t * p_mcmr,
			   IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_link_record(IN osm_log_t * p_log,
			  IN const ib_link_record_t * p_lr,
			  IN osm_log_level_t log_level);

void osm_dump_link_record_v2(IN osm_log_t * p_log,
			     IN const ib_link_record_t * p_lr,
			     IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_service_record(IN osm_log_t * p_log,
			     IN const ib_service_record_t * p_sr,
			     IN osm_log_level_t log_level);

void osm_dump_service_record_v2(IN osm_log_t * p_log,
				IN const ib_service_record_t * p_sr,
				IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_portinfo_record(IN osm_log_t * p_log,
			      IN const ib_portinfo_record_t * p_pir,
			      IN osm_log_level_t log_level);

void osm_dump_portinfo_record_v2(IN osm_log_t * p_log,
				 IN const ib_portinfo_record_t * p_pir,
				 IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_guidinfo_record(IN osm_log_t * p_log,
			      IN const ib_guidinfo_record_t * p_gir,
			      IN osm_log_level_t log_level);

void osm_dump_guidinfo_record_v2(IN osm_log_t * p_log,
				 IN const ib_guidinfo_record_t * p_gir,
				 IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_inform_info(IN osm_log_t * p_log,
			  IN const ib_inform_info_t * p_ii,
			  IN osm_log_level_t log_level);

void osm_dump_inform_info_v2(IN osm_log_t * p_log,
			     IN const ib_inform_info_t * p_ii,
			     IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_inform_info_record(IN osm_log_t * p_log,
				 IN const ib_inform_info_record_t * p_iir,
				 IN osm_log_level_t log_level);

void osm_dump_inform_info_record_v2(IN osm_log_t * p_log,
				    IN const ib_inform_info_record_t * p_iir,
				    IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_switch_info_record(IN osm_log_t * p_log,
				 IN const ib_switch_info_record_t * p_sir,
				 IN osm_log_level_t log_level);

void osm_dump_switch_info_record_v2(IN osm_log_t * p_log,
				    IN const ib_switch_info_record_t * p_sir,
				    IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_sm_info_record(IN osm_log_t * p_log,
			     IN const ib_sminfo_record_t * p_smir,
			     IN osm_log_level_t log_level);

void osm_dump_sm_info_record_v2(IN osm_log_t * p_log,
				IN const ib_sminfo_record_t * p_smir,
				IN const int file_id, IN osm_log_level_t log_level);

void osm_dump_pkey_block(IN osm_log_t * p_log, IN uint64_t port_guid,
			 IN uint16_t block_num, IN uint8_t port_num,
			 IN const ib_pkey_table_t * p_pkey_tbl,
			 IN osm_log_level_t log_level);

void osm_dump_pkey_block_v2(IN osm_log_t * p_log, IN uint64_t port_guid,
			    IN uint16_t block_num, IN uint8_t port_num,
			    IN const ib_pkey_table_t * p_pkey_tbl,
			    IN const int file_id,
			    IN osm_log_level_t log_level);

void osm_dump_slvl_map_table(IN osm_log_t * p_log, IN uint64_t port_guid,
			     IN uint8_t in_port_num, IN uint8_t out_port_num,
			     IN const ib_slvl_table_t * p_slvl_tbl,
			     IN osm_log_level_t log_level);

void osm_dump_slvl_map_table_v2(IN osm_log_t * p_log, IN uint64_t port_guid,
				IN uint8_t in_port_num, IN uint8_t out_port_num,
				IN const ib_slvl_table_t * p_slvl_tbl,
				IN const int file_id,
				IN osm_log_level_t log_level);


void osm_dump_vl_arb_table(IN osm_log_t * p_log, IN uint64_t port_guid,
			   IN uint8_t block_num, IN uint8_t port_num,
			   IN const ib_vl_arb_table_t * p_vla_tbl,
			   IN osm_log_level_t log_level);

void osm_dump_vl_arb_table_v2(IN osm_log_t * p_log, IN uint64_t port_guid,
			      IN uint8_t block_num, IN uint8_t port_num,
			      IN const ib_vl_arb_table_t * p_vla_tbl,
			      IN const int file_id,
			      IN osm_log_level_t log_level);

/****f* OpenSM: Helper/osm_dump_port_info
* NAME
*	osm_dump_port_info
*
* DESCRIPTION
*	Dumps the PortInfo attribute to the log.
*
* SYNOPSIS
*/
void osm_dump_node_info(IN osm_log_t * p_log,
			IN const ib_node_info_t * p_ni,
			IN osm_log_level_t log_level);

void osm_dump_node_info_v2(IN osm_log_t * p_log,
			   IN const ib_node_info_t * p_ni,
			   IN const int file_id,
			   IN osm_log_level_t log_level);
/*
* PARAMETERS
*	p_log
*		[in] Pointer to the osm_log_t object
*
*	p_ni
*		[in] Pointer to the NodeInfo attribute
*
*	log_level
*		[in] Log verbosity level with which to dump the data.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/osm_dump_sm_info
* NAME
*	osm_dump_sm_info
*
* DESCRIPTION
*	Dumps the SMInfo attribute to the log.
*
* SYNOPSIS
*/
void osm_dump_sm_info(IN osm_log_t * p_log, IN const ib_sm_info_t * p_smi,
		      IN osm_log_level_t log_level);

void osm_dump_sm_info_v2(IN osm_log_t * p_log, IN const ib_sm_info_t * p_smi,
			 IN const int file_id, IN osm_log_level_t log_level);

/*
* PARAMETERS
*	p_log
*		[in] Pointer to the osm_log_t object
*
*	p_smi
*		[in] Pointer to the SMInfo attribute
*
*	log_level
*		[in] Log verbosity level with which to dump the data.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/osm_dump_switch_info
* NAME
*	osm_dump_switch_info
*
* DESCRIPTION
*	Dumps the SwitchInfo attribute to the log.
*
* SYNOPSIS
*/
void osm_dump_switch_info(IN osm_log_t * p_log,
			  IN const ib_switch_info_t * p_si,
			  IN osm_log_level_t log_level);

void osm_dump_switch_info_v2(IN osm_log_t * p_log,
			     IN const ib_switch_info_t * p_si,
			     IN const int file_id,
			     IN osm_log_level_t log_level);
/*
* PARAMETERS
*	p_log
*		[in] Pointer to the osm_log_t object
*
*	p_si
*		[in] Pointer to the SwitchInfo attribute
*
*	log_level
*		[in] Log verbosity level with which to dump the data.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/osm_dump_notice
* NAME
*	osm_dump_notice
*
* DESCRIPTION
*	Dumps the Notice attribute to the log.
*
* SYNOPSIS
*/
void osm_dump_notice(IN osm_log_t * p_log,
		     IN const ib_mad_notice_attr_t * p_ntci,
		     IN osm_log_level_t log_level);

void osm_dump_notice_v2(IN osm_log_t * p_log,
			IN const ib_mad_notice_attr_t * p_ntci,
			IN const int file_id,
			IN osm_log_level_t log_level);
/*
* PARAMETERS
*	p_log
*		[in] Pointer to the osm_log_t object
*
*	p_ntci
*		[in] Pointer to the Notice attribute
*
*	log_level
*		[in] Log verbosity level with which to dump the data.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/osm_get_disp_msg_str
* NAME
*	osm_get_disp_msg_str
*
* DESCRIPTION
*	Returns a string for the specified Dispatcher message.
*
* SYNOPSIS
*/
const char *osm_get_disp_msg_str(IN cl_disp_msgid_t msg);
/*
* PARAMETERS
*	msg
*		[in] Dispatcher message ID value.
*
* RETURN VALUES
*	Pointer to the message description string.
*
* NOTES
*
* SEE ALSO
*********/

void osm_dump_dr_path(IN osm_log_t * p_log, IN const osm_dr_path_t * p_path,
		      IN osm_log_level_t level);

void osm_dump_dr_path_v2(IN osm_log_t * p_log, IN const osm_dr_path_t * p_path,
			 IN const int file_id, IN osm_log_level_t level);


void osm_dump_smp_dr_path(IN osm_log_t * p_log, IN const ib_smp_t * p_smp,
			  IN osm_log_level_t level);

void osm_dump_smp_dr_path_v2(IN osm_log_t * p_log, IN const ib_smp_t * p_smp,
			     IN const int file_id, IN osm_log_level_t level);

void osm_dump_dr_smp(IN osm_log_t * p_log, IN const ib_smp_t * p_smp,
		     IN osm_log_level_t level);

void osm_dump_dr_smp_v2(IN osm_log_t * p_log, IN const ib_smp_t * p_smp,
			IN const int file_id, IN osm_log_level_t level);

void osm_dump_sa_mad(IN osm_log_t * p_log, IN const ib_sa_mad_t * p_smp,
		     IN osm_log_level_t level);

void osm_dump_sa_mad_v2(IN osm_log_t * p_log, IN const ib_sa_mad_t * p_smp,
			IN const int file_id, IN osm_log_level_t level);

void osm_dump_dr_path_as_buf(IN size_t max_len, IN const osm_dr_path_t * p_path,
			     OUT char* buf);


/****f* IBA Base: Types/osm_get_sm_signal_str
* NAME
*	osm_get_sm_signal_str
*
* DESCRIPTION
*	Returns a string for the specified SM state.
*
* SYNOPSIS
*/
const char *osm_get_sm_signal_str(IN osm_signal_t signal);
/*
* PARAMETERS
*	state
*		[in] Signal value
*
* RETURN VALUES
*	Pointer to the signal description string.
*
* NOTES
*
* SEE ALSO
*********/

const char *osm_get_port_state_str_fixed_width(IN uint8_t port_state);

const char *osm_get_node_type_str_fixed_width(IN uint8_t node_type);

const char *osm_get_manufacturer_str(IN uint64_t guid_ho);

const char *osm_get_mtu_str(IN uint8_t mtu);

const char *osm_get_lwa_str(IN uint8_t lwa);

const char *osm_get_lsa_str(IN uint8_t lsa, IN uint8_t lsea, IN uint8_t state,
			    IN uint8_t fdr10);

/****f* IBA Base: Types/osm_get_sm_mgr_signal_str
* NAME
*	osm_get_sm_mgr_signal_str
*
* DESCRIPTION
*	Returns a string for the specified SM manager signal.
*
* SYNOPSIS
*/
const char *osm_get_sm_mgr_signal_str(IN osm_sm_signal_t signal);
/*
* PARAMETERS
*	signal
*		[in] SM manager signal
*
* RETURN VALUES
*	Pointer to the signal description string.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/osm_get_sm_mgr_state_str
* NAME
*	osm_get_sm_mgr_state_str
*
* DESCRIPTION
*	Returns a string for the specified SM manager state.
*
* SYNOPSIS
*/
const char *osm_get_sm_mgr_state_str(IN uint16_t state);
/*
* PARAMETERS
*	state
*		[in] SM manager state
*
* RETURN VALUES
*	Pointer to the state description string.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_mtu_is_valid
* NAME
*	ib_mtu_is_valid
*
* DESCRIPTION
*	Validates encoded MTU
*
* SYNOPSIS
*/
int ib_mtu_is_valid(IN const int mtu);
/*
* PARAMETERS
*	mtu
*		[in] Encoded path mtu.
*
* RETURN VALUES
*	Returns an int indicating mtu is valid (1)
*	or invalid (0).
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_rate_is_valid
* NAME
*	ib_rate_is_valid
*
* DESCRIPTION
*	Validates encoded rate
*
* SYNOPSIS
*/
int ib_rate_is_valid(IN const int rate);
/*
* PARAMETERS
*	rate
*		[in] Encoded path rate.
*
* RETURN VALUES
*	Returns an int indicating rate is valid (1)
*	or invalid (0).
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_path_compare_rates
* NAME
*	ib_path_compare_rates
*
* DESCRIPTION
*	Compares the encoded values for two path rates and
*	return value is based on the ordered comparison of
*	the path rates (or path rate equivalents).
*
* SYNOPSIS
*/
int ib_path_compare_rates(IN const int rate1, IN const int rate2);

/*
* PARAMETERS
*	rate1
*		[in] Encoded path rate 1.
*
*	rate2
*		[in] Encoded path rate 2.
*
* RETURN VALUES
*	Returns an int indicating less than (-1), equal to (0), or
*	greater than (1) rate1 as compared with rate2.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_path_rate_get_prev
* NAME
*	ib_path_rate_get_prev
*
* DESCRIPTION
*	Obtains encoded rate for the rate previous to the one requested.
*
* SYNOPSIS
*/
int ib_path_rate_get_prev(IN const int rate);

/*
* PARAMETERS
*	rate
*		[in] Encoded path rate.
*
* RETURN VALUES
*	Returns an int indicating encoded rate or
*	0 if none can be found.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_path_rate_get_next
* NAME
*	ib_path_rate_get_next
*
* DESCRIPTION
*	Obtains encoded rate for the rate subsequent to the one requested.
*
* SYNOPSIS
*/
int ib_path_rate_get_next(IN const int rate);

/*
* PARAMETERS
*	rate
*		[in] Encoded path rate.
*
* RETURN VALUES
*	Returns an int indicating encoded rate or
*	0 if none can be found.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/sprint_uint8_arr
* NAME
*	sprint_uint8_arr
*
* DESCRIPTION
*	Create the comma-separated string of numbers
*	from input array of uint8 numbers
*	(e.g. "1,2,3,4")
*
* SYNOPSIS
*/
int sprint_uint8_arr(IN char *buf, IN size_t size,
		     IN const uint8_t * arr, IN size_t len);

/*
* PARAMETERS
*	buf
*		[in] Pointer to the output buffer
*
*	size
*		[in] Size of the output buffer
*
*	arr
*		[in] Pointer to the input array of uint8
*
*	len
*		[in] Size of the input array
*
* RETURN VALUES
*	Return the number of characters printed to the buffer
*
* NOTES
*
* SEE ALSO
*********/


END_C_DECLS
#endif				/* _OSM_HELPER_H_ */
