/*
 * Copyright (c) 2012 Mellanox Technologies LTD. All rights reserved.
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
 * 	Declaration of osm_file_ids_enum.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_FILE_ID_H_
#define _OSM_FILE_ID_H_

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

/****d* OpenSM: osm_file_ids_enum
* NAME
*	osm_file_ids_enum
*
* DESCRIPTION
*	Enumerates all FILE_IDs used for logging support.
*
* SYNOPSIS
*/
typedef enum _osm_file_ids_enum {
	OSM_FILE_MAIN_C = 0,
	OSM_FILE_CONSOLE_C,
	OSM_FILE_CONSOLE_IO_C,
	OSM_FILE_DB_FILES_C,
	OSM_FILE_DB_PACK_C,
	OSM_FILE_DROP_MGR_C,
	OSM_FILE_DUMP_C,
	OSM_FILE_EVENT_PLUGIN_C,
	OSM_FILE_GUID_INFO_RCV_C,
	OSM_FILE_GUID_MGR_C,
	OSM_FILE_HELPER_C,
	OSM_FILE_INFORM_C,
	OSM_FILE_LID_MGR_C,
	OSM_FILE_LIN_FWD_RCV_C,
	OSM_FILE_LINK_MGR_C,
	OSM_FILE_LOG_C,
	OSM_FILE_MAD_POOL_C,
	OSM_FILE_MCAST_FWD_RCV_C,
	OSM_FILE_MCAST_MGR_C,
	OSM_FILE_MCAST_TBL_C,
	OSM_FILE_MCM_PORT_C,
	OSM_FILE_MESH_C,
	OSM_FILE_MLNX_EXT_PORT_INFO_RCV_C,
	OSM_FILE_MTREE_C,
	OSM_FILE_MULTICAST_C,
	OSM_FILE_NODE_C,
	OSM_FILE_NODE_DESC_RCV_C,
	OSM_FILE_NODE_INFO_RCV_C,
	OSM_FILE_OPENSM_C,
	OSM_FILE_PERFMGR_C,
	OSM_FILE_PERFMGR_DB_C,
	OSM_FILE_PKEY_C,
	OSM_FILE_PKEY_MGR_C,
	OSM_FILE_PKEY_RCV_C,
	OSM_FILE_PORT_C,
	OSM_FILE_PORT_INFO_RCV_C,
	OSM_FILE_PRTN_C,
	OSM_FILE_PRTN_CONFIG_C,
	OSM_FILE_QOS_C,
	OSM_FILE_QOS_PARSER_L_L,
	OSM_FILE_QOS_PARSER_Y_Y,
	OSM_FILE_QOS_POLICY_C,
	OSM_FILE_REMOTE_SM_C,
	OSM_FILE_REQ_C,
	OSM_FILE_RESP_C,
	OSM_FILE_ROUTER_C,
	OSM_FILE_SA_C,
	OSM_FILE_SA_CLASS_PORT_INFO_C,
	OSM_FILE_SA_GUIDINFO_RECORD_C,
	OSM_FILE_SA_INFORMINFO_C,
	OSM_FILE_SA_LFT_RECORD_C,
	OSM_FILE_SA_LINK_RECORD_C,
	OSM_FILE_SA_MAD_CTRL_C,
	OSM_FILE_SA_MCMEMBER_RECORD_C,
	OSM_FILE_SA_MFT_RECORD_C,
	OSM_FILE_SA_MULTIPATH_RECORD_C,
	OSM_FILE_SA_NODE_RECORD_C,
	OSM_FILE_SA_PATH_RECORD_C,
	OSM_FILE_SA_PKEY_RECORD_C,
	OSM_FILE_SA_PORTINFO_RECORD_C,
	OSM_FILE_SA_SERVICE_RECORD_C,
	OSM_FILE_SA_SLVL_RECORD_C,
	OSM_FILE_SA_SMINFO_RECORD_C,
	OSM_FILE_SA_SW_INFO_RECORD_C,
	OSM_FILE_SA_VLARB_RECORD_C,
	OSM_FILE_SERVICE_C,
	OSM_FILE_SLVL_MAP_RCV_C,
	OSM_FILE_SM_C,
	OSM_FILE_SMINFO_RCV_C,
	OSM_FILE_SM_MAD_CTRL_C,
	OSM_FILE_SM_STATE_MGR_C,
	OSM_FILE_STATE_MGR_C,
	OSM_FILE_SUBNET_C,
	OSM_FILE_SW_INFO_RCV_C,
	OSM_FILE_SWITCH_C,
	OSM_FILE_TORUS_C,
	OSM_FILE_TRAP_RCV_C,
	OSM_FILE_UCAST_CACHE_C,
	OSM_FILE_UCAST_DNUP_C,
	OSM_FILE_UCAST_FILE_C,
	OSM_FILE_UCAST_FTREE_C,
	OSM_FILE_UCAST_LASH_C,
	OSM_FILE_UCAST_MGR_C,
	OSM_FILE_UCAST_UPDN_C,
	OSM_FILE_VENDOR_IBUMAD_C,
	OSM_FILE_VL15INTF_C,
	OSM_FILE_VL_ARB_RCV_C,
	OSM_FILE_ST_C,
	OSM_FILE_UCAST_DFSSSP_C,
	OSM_FILE_CONGESTION_CONTROL_C,
} osm_file_ids_enum;
/***********/

END_C_DECLS
#endif				/* _OSM_FILE_ID_H_ */
