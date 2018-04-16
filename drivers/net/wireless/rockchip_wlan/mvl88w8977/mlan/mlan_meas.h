/**
 *  @file mlan_meas.h
 *
 *  @brief Interface for the measurement module implemented in mlan_meas.c
 *
 *  Driver interface functions and type declarations for the measurement module
 *    implemented in mlan_meas.c
 *
 *  @sa mlan_meas.c
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 *
 */

/*************************************************************
Change Log:
    03/25/2009: initial version
************************************************************/

#ifndef _MLAN_MEAS_H_
#define _MLAN_MEAS_H_

#include  "mlan_fw.h"

/* Send a given measurement request to the firmware, report back the result */
extern int

wlan_meas_util_send_req(mlan_private *pmpriv,
			HostCmd_DS_MEASUREMENT_REQUEST *pmeas_req,
			t_u32 wait_for_resp_timeout, pmlan_ioctl_req pioctl_req,
			HostCmd_DS_MEASUREMENT_REPORT *pmeas_rpt);

/* Setup a measurement command before it is sent to the firmware */
extern int wlan_meas_cmd_process(mlan_private *pmpriv,
				 HostCmd_DS_COMMAND *pcmd_ptr,
				 const t_void *pinfo_buf);

/* Handle a given measurement command response from the firmware */
extern int wlan_meas_cmdresp_process(mlan_private *pmpriv,
				     const HostCmd_DS_COMMAND *resp);

#endif /* _MLAN_MEAS_H_ */
