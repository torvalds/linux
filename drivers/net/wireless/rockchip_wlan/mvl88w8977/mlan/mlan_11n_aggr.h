/** @file mlan_11n_aggr.h
 *
 *  @brief This file contains related macros, enum, and struct
 *  of 11n aggregation functionalities
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

/********************************************************
Change log:
    11/10/2008: initial version
********************************************************/

#ifndef _MLAN_11N_AGGR_H_
#define _MLAN_11N_AGGR_H_

/** Aggregate 11N packets */
mlan_status wlan_11n_deaggregate_pkt(pmlan_private priv, pmlan_buffer pmbuf);
/** Deaggregate 11N packets */
int wlan_11n_aggregate_pkt(mlan_private *priv, raListTbl *ptr,
			   int headroom, int ptrindex);

#endif /* !_MLAN_11N_AGGR_H_ */
