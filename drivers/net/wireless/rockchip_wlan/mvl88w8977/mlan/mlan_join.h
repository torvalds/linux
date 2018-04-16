/** @file mlan_join.h
 *
 *  @brief This file defines the interface for the WLAN infrastructure
 *  and adhoc join routines.
 *
 *  Driver interface functions and type declarations for the join module
 *  implemented in mlan_join.c.  Process all start/join requests for
 *  both adhoc and infrastructure networks
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
 */

/******************************************************
Change log:
    10/13/2008: initial version
******************************************************/

#ifndef _MLAN_JOIN_H_
#define _MLAN_JOIN_H_

/** Size of buffer allocated to store the association response from firmware */
#define MRVDRV_ASSOC_RSP_BUF_SIZE  500

/** Size of buffer allocated to store IEs passed to firmware in the assoc req */
#define MRVDRV_GENIE_BUF_SIZE      256

#endif /* _MLAN_JOIN_H_ */
