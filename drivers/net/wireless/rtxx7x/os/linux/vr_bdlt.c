/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#define RTMP_MODULE_OS
#define RTMP_MODULE_OS_UTIL

#define MODULE_BDLT

/*#include "rt_config.h" */
#include "rtmp_comm.h"
#include "rt_os_util.h"
#include "rtmp_osabl.h"


#ifdef PLATFORM_BL2348

/* global variables */
int (*pToUpperLayerPktSent)(struct sk_buff *pSkb) = netif_rx ;




/*
========================================================================
Routine Description:
	Assign the briding function.

Arguments:
	xi_destination_ptr	- bridging function

Return Value:
	None

Note:
	The function name must be replace_upper_layer_packet_destination.
========================================================================
*/
VOID replace_upper_layer_packet_destination(VOID *pXiDestination)
{
	DBGPRINT(RT_DEBUG_TRACE, ("ralink broad light> replace_upper_layer_packet_destination\n"));
	pToUpperLayerPktSent = pXiDestination ;
} /* End of replace_upper_layer_packet_destination */


EXPORT_SYMBOL(pToUpperLayerPktSent);
EXPORT_SYMBOL(replace_upper_layer_packet_destination);

#endif /* PLATFORM_BL2348 */


/* End of vr_bdlt.c */
