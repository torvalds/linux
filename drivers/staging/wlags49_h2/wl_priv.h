/*******************************************************************************
 * Agere Systems Inc.
 * Wireless device driver for Linux (wlags49).
 *
 * Copyright (c) 1998-2003 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * Initially developed by TriplePoint, Inc.
 *   http://www.triplepoint.com
 *
 *------------------------------------------------------------------------------
 *
 *   Header describing information required for the private IOCTL handlers.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2003 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 ******************************************************************************/

#ifndef __WL_PRIV_H__
#define __WL_PRIV_H__




/*******************************************************************************
 *  function prototypes
 ******************************************************************************/
#ifdef WIRELESS_EXT


int wvlan_set_netname( struct net_device *,  struct iw_request_info *, union iwreq_data *, char *extra );

int wvlan_get_netname( struct net_device *,  struct iw_request_info *, union iwreq_data *, char *extra );

int wvlan_set_station_nickname( struct net_device *,  struct iw_request_info *, union iwreq_data *, char *extra );

int wvlan_get_station_nickname( struct net_device *,  struct iw_request_info *, union iwreq_data *, char *extra );

int wvlan_set_porttype( struct net_device *,  struct iw_request_info *, union iwreq_data *, char *extra );

int wvlan_get_porttype( struct net_device *,  struct iw_request_info *, union iwreq_data *, char *extra );


#endif  // WIRELESS_EXT




#ifdef USE_UIL

int wvlan_uil( struct uilreq *urq, struct wl_private *lp );

// int wvlan_uil_connect( struct uilreq *urq, struct wl_private *lp );
// int wvlan_uil_disconnect( struct uilreq *urq, struct wl_private *lp );
// int wvlan_uil_action( struct uilreq *urq, struct wl_private *lp );
// int wvlan_uil_block( struct uilreq *urq, struct wl_private *lp );
// int wvlan_uil_unblock( struct uilreq *urq, struct wl_private *lp );
// int wvlan_uil_send_diag_msg( struct uilreq *urq, struct wl_private *lp );
// int wvlan_uil_put_info( struct uilreq *urq, struct wl_private *lp );
// int wvlan_uil_get_info( struct uilreq *urq, struct wl_private *lp );

//int cfg_driver_info( struct uilreq *urq, struct wl_private *lp );
//int cfg_driver_identity( struct uilreq *urq, struct wl_private *lp );

#endif  // USE_UIL


#ifdef USE_RTS

int wvlan_rts( struct rtsreq *rrq, __u32 io_base );
int wvlan_rts_read( __u16 reg, __u16 *val, __u32 io_base );
int wvlan_rts_write( __u16 reg, __u16 val, __u32 io_base );
int wvlan_rts_batch_read( struct rtsreq *rrq, __u32 io_base );
int wvlan_rts_batch_write( struct rtsreq *rrq, __u32 io_base );

#endif  // USE_RTS


#endif  // __WL_PRIV_H__
