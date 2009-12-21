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
 *   Header describing information required for the wireless IOCTL handlers.
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

#ifndef __WL_WEXT_H__
#define __WL_WEXT_H__


#ifdef WIRELESS_EXT


/*******************************************************************************
 *  function protoypes
 ******************************************************************************/

struct iw_statistics *wl_wireless_stats( struct net_device *dev );

struct iw_statistics * wl_get_wireless_stats( struct net_device *dev );

inline void wl_spy_gather (struct net_device *dev, u_char *mac);

void wl_wext_event_freq( struct net_device *dev );
void wl_wext_event_mode( struct net_device *dev );
void wl_wext_event_essid( struct net_device *dev );
void wl_wext_event_encode( struct net_device *dev );
void wl_wext_event_ap( struct net_device *dev );
void wl_wext_event_scan_complete( struct net_device *dev );
void wl_wext_event_new_sta( struct net_device *dev );
void wl_wext_event_expired_sta( struct net_device *dev );
void wl_wext_event_mic_failed( struct net_device *dev );
void wl_wext_event_assoc_ie( struct net_device *dev );

extern const struct iw_handler_def wl_iw_handler_def;

#else
#error WIRELESS_EXT
#endif // WIRELESS_EXT


#endif  // __WL_WEXT_H__
