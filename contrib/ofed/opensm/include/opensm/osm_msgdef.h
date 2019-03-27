/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
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
 * 	Declaration of Dispatcher message values.
 */

#ifndef _OSM_MSGDEF_H_
#define _OSM_MSGDEF_H_

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Dispatcher Messages
* NAME
*	Dispatcher Messages
*
* DESCRIPTION
*	These constants define the messages sent between OpenSM controllers
*	attached to the Dispatcher.
*
*	Each message description contains the following information:
*	Sent by: which controller(s) send this message
*	Received by: which controller receives this message
*	Delivery notice: Indicates if the sender requires confirmation
*		that the message has been delivered.  Typically a "yes" here
*		means that some resources associated with sending the
*		message must be freed.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Dispatcher Messages/OSM_MSG_MAD_NODE_INFO
* NAME
*	OSM_MSG_MAD_NODE_INFO
*
* DESCRIPTION
*	Message for received NodeInfo MADs.
*
* NOTES
*	Sent by:			osm_mad_ctrl_t
*	Received by:			osm_ni_rcv_ctrl_t
*	Delivery notice:		yes
*
*
***********/
/****s* OpenSM: Dispatcher Messages/OSM_MSG_MAD_PORT_INFO
* NAME
*	OSM_MSG_MAD_PORT_INFO
*
* DESCRIPTION
*	Message for received PortInfo MADs.
*
* NOTES
*	Sent by:			osm_mad_ctrl_t
*	Received by:			osm_pi_rcv_ctrl_t
*	Delivery notice:		yes
*
*
***********/
/****s* OpenSM: Dispatcher Messages/OSM_MSG_MAD_SWITCH_INFO
* NAME
*	OSM_MSG_MAD_SWITCH_INFO
*
* DESCRIPTION
*	Message for received SwitchInfo MADs.
*
* NOTES
*	Sent by:			osm_mad_ctrl_t
*	Received by:			osm_si_rcv_ctrl_t
*	Delivery notice:		yes
*
***********/
/****s* OpenSM: Dispatcher Messages/OSM_MSG_MAD_NODE_DESC
* NAME
*	OSM_MSG_MAD_NODE_DESC
*
* DESCRIPTION
*	Message for received NodeDescription MADs.
*
* NOTES
*	Sent by:			osm_mad_ctrl_t
*	Received by:			osm_nd_rcv_ctrl_t
*	Delivery notice:		yes
*
* SOURCE
***********/
enum {
	OSM_MSG_NONE = 0,
	OSM_MSG_MAD_NODE_INFO,
	OSM_MSG_MAD_PORT_INFO,
	OSM_MSG_MAD_SWITCH_INFO,
	OSM_MSG_MAD_GUID_INFO,
	OSM_MSG_MAD_NODE_DESC,
	OSM_MSG_MAD_NODE_RECORD,
	OSM_MSG_MAD_PORTINFO_RECORD,
	OSM_MSG_MAD_SERVICE_RECORD,
	OSM_MSG_MAD_PATH_RECORD,
	OSM_MSG_MAD_MCMEMBER_RECORD,
	OSM_MSG_MAD_LINK_RECORD,
	OSM_MSG_MAD_SMINFO_RECORD,
	OSM_MSG_MAD_CLASS_PORT_INFO,
	OSM_MSG_MAD_INFORM_INFO,
	OSM_MSG_MAD_LFT_RECORD,
	OSM_MSG_MAD_LFT,
	OSM_MSG_MAD_SM_INFO,
	OSM_MSG_MAD_NOTICE,
	OSM_MSG_LIGHT_SWEEP_FAIL,
	OSM_MSG_MAD_MFT,
	OSM_MSG_MAD_PKEY_TBL_RECORD,
	OSM_MSG_MAD_VL_ARB_RECORD,
	OSM_MSG_MAD_SLVL_TBL_RECORD,
	OSM_MSG_MAD_PKEY,
	OSM_MSG_MAD_VL_ARB,
	OSM_MSG_MAD_SLVL,
	OSM_MSG_MAD_GUIDINFO_RECORD,
	OSM_MSG_MAD_INFORM_INFO_RECORD,
	OSM_MSG_MAD_SWITCH_INFO_RECORD,
	OSM_MSG_MAD_MFT_RECORD,
#if defined (VENDOR_RMPP_SUPPORT) && defined (DUAL_SIDED_RMPP)
	OSM_MSG_MAD_MULTIPATH_RECORD,
#endif
	OSM_MSG_MAD_PORT_COUNTERS,
	OSM_MSG_MAD_MLNX_EXT_PORT_INFO,
	OSM_MSG_MAD_CC,
	OSM_MSG_MAX
};

END_C_DECLS
#endif				/* _OSM_MSGDEF_H_ */
