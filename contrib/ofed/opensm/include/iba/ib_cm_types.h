/*
 * Copyright (c) 2004-2007 Voltaire, Inc. All rights reserved.
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

#if !defined(__IB_CM_TYPES_H__)
#define __IB_CM_TYPES_H__

#ifndef __WIN__

#include <iba/ib_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/*
 * Defines known Communication management class versions
 */
#define IB_MCLASS_CM_VER_2				2
#define IB_MCLASS_CM_VER_1				1
/*
 *	Defines the size of user available data in communication management MADs
 */
#define IB_REQ_PDATA_SIZE_VER2				92
#define IB_MRA_PDATA_SIZE_VER2				222
#define IB_REJ_PDATA_SIZE_VER2				148
#define IB_REP_PDATA_SIZE_VER2				196
#define IB_RTU_PDATA_SIZE_VER2				224
#define IB_LAP_PDATA_SIZE_VER2				168
#define IB_APR_PDATA_SIZE_VER2				148
#define IB_DREQ_PDATA_SIZE_VER2				220
#define IB_DREP_PDATA_SIZE_VER2				224
#define IB_SIDR_REQ_PDATA_SIZE_VER2			216
#define IB_SIDR_REP_PDATA_SIZE_VER2			136
#define IB_REQ_PDATA_SIZE_VER1				92
#define IB_MRA_PDATA_SIZE_VER1				222
#define IB_REJ_PDATA_SIZE_VER1				148
#define IB_REP_PDATA_SIZE_VER1				204
#define IB_RTU_PDATA_SIZE_VER1				224
#define IB_LAP_PDATA_SIZE_VER1				168
#define IB_APR_PDATA_SIZE_VER1				151
#define IB_DREQ_PDATA_SIZE_VER1				220
#define IB_DREP_PDATA_SIZE_VER1				224
#define IB_SIDR_REQ_PDATA_SIZE_VER1			216
#define IB_SIDR_REP_PDATA_SIZE_VER1			140
#define IB_ARI_SIZE					72	// redefine
#define IB_APR_INFO_SIZE				72
/****d* Access Layer/ib_rej_status_t
* NAME
*	ib_rej_status_t
*
* DESCRIPTION
*	Rejection reasons.
*
* SYNOPSIS
*/
typedef ib_net16_t ib_rej_status_t;
/*
* SEE ALSO
*	ib_cm_rej, ib_cm_rej_rec_t
*
* SOURCE
*/
#define IB_REJ_INSUF_QP					CL_HTON16(1)
#define IB_REJ_INSUF_EEC				CL_HTON16(2)
#define IB_REJ_INSUF_RESOURCES				CL_HTON16(3)
#define IB_REJ_TIMEOUT					CL_HTON16(4)
#define IB_REJ_UNSUPPORTED				CL_HTON16(5)
#define IB_REJ_INVALID_COMM_ID				CL_HTON16(6)
#define IB_REJ_INVALID_COMM_INSTANCE			CL_HTON16(7)
#define IB_REJ_INVALID_SID				CL_HTON16(8)
#define IB_REJ_INVALID_XPORT				CL_HTON16(9)
#define IB_REJ_STALE_CONN				CL_HTON16(10)
#define IB_REJ_RDC_NOT_EXIST				CL_HTON16(11)
#define IB_REJ_INVALID_GID				CL_HTON16(12)
#define IB_REJ_INVALID_LID				CL_HTON16(13)
#define IB_REJ_INVALID_SL				CL_HTON16(14)
#define IB_REJ_INVALID_TRAFFIC_CLASS			CL_HTON16(15)
#define IB_REJ_INVALID_HOP_LIMIT			CL_HTON16(16)
#define IB_REJ_INVALID_PKT_RATE				CL_HTON16(17)
#define IB_REJ_INVALID_ALT_GID				CL_HTON16(18)
#define IB_REJ_INVALID_ALT_LID				CL_HTON16(19)
#define IB_REJ_INVALID_ALT_SL				CL_HTON16(20)
#define IB_REJ_INVALID_ALT_TRAFFIC_CLASS		CL_HTON16(21)
#define IB_REJ_INVALID_ALT_HOP_LIMIT			CL_HTON16(22)
#define IB_REJ_INVALID_ALT_PKT_RATE			CL_HTON16(23)
#define IB_REJ_PORT_REDIRECT				CL_HTON16(24)
#define IB_REJ_INVALID_MTU				CL_HTON16(26)
#define IB_REJ_INSUFFICIENT_RESP_RES			CL_HTON16(27)
#define IB_REJ_USER_DEFINED				CL_HTON16(28)
#define IB_REJ_INVALID_RNR_RETRY			CL_HTON16(29)
#define IB_REJ_DUPLICATE_LOCAL_COMM_ID			CL_HTON16(30)
#define IB_REJ_INVALID_CLASS_VER			CL_HTON16(31)
#define IB_REJ_INVALID_FLOW_LBL				CL_HTON16(32)
#define IB_REJ_INVALID_ALT_FLOW_LBL			CL_HTON16(33)

#define IB_REJ_SERVICE_HANDOFF				CL_HTON16(65535)
/******/

/****d* Access Layer/ib_apr_status_t
* NAME
*	ib_apr_status_t
*
* DESCRIPTION
*	Automatic path migration status information.
*
* SYNOPSIS
*/
typedef uint8_t ib_apr_status_t;
/*
* SEE ALSO
*	ib_cm_apr, ib_cm_apr_rec_t
*
* SOURCE
 */
#define IB_AP_SUCCESS					0
#define IB_AP_INVALID_COMM_ID				1
#define IB_AP_UNSUPPORTED				2
#define IB_AP_REJECT					3
#define IB_AP_REDIRECT					4
#define IB_AP_IS_CURRENT				5
#define IB_AP_INVALID_QPN_EECN				6
#define IB_AP_INVALID_LID				7
#define IB_AP_INVALID_GID				8
#define IB_AP_INVALID_FLOW_LBL				9
#define IB_AP_INVALID_TCLASS				10
#define IB_AP_INVALID_HOP_LIMIT				11
#define IB_AP_INVALID_PKT_RATE				12
#define IB_AP_INVALID_SL				13
/******/

/****d* Access Layer/ib_cm_cap_mask_t
* NAME
*	ib_cm_cap_mask_t
*
* DESCRIPTION
*	Capability mask values in ClassPortInfo.
*
* SYNOPSIS
*/
#define IB_CM_RELIABLE_CONN_CAPABLE			CL_HTON16(9)
#define IB_CM_RELIABLE_DGRM_CAPABLE			CL_HTON16(10)
#define IB_CM_RDGRM_CAPABLE				CL_HTON16(11)
#define IB_CM_UNRELIABLE_CONN_CAPABLE			CL_HTON16(12)
#define IB_CM_SIDR_CAPABLE				CL_HTON16(13)
/*
* SEE ALSO
*	ib_cm_rep, ib_class_port_info_t
*
* SOURCE
*
*******/

/*
 *	Service ID resolution status
 */
typedef uint16_t ib_sidr_status_t;
#define IB_SIDR_SUCCESS					0
#define IB_SIDR_UNSUPPORTED				1
#define IB_SIDR_REJECT					2
#define IB_SIDR_NO_QP					3
#define IB_SIDR_REDIRECT				4
#define IB_SIDR_UNSUPPORTED_VER				5

END_C_DECLS
#endif				/* ndef __WIN__ */
#endif				/* __IB_CM_TYPES_H__ */
