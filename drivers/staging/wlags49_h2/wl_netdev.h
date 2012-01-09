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
 *   Header describing information required by the network layerentry points
 *   into the driver.
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
 * THIS SOFTWARE IS PROVIDED AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES,
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

#ifndef __WL_NETDEV_H__
#define __WL_NETDEV_H__




/*******************************************************************************
 *  function prototypes
 ******************************************************************************/
int wl_init( struct net_device *dev );

int wl_config( struct net_device *dev, struct ifmap *map );

struct net_device *wl_device_alloc( void );

void wl_device_dealloc( struct net_device *dev );

int wl_open( struct net_device *dev );

int wl_close( struct net_device *dev );

int wl_ioctl( struct net_device *dev, struct ifreq *rq, int cmd );

int wl_tx( struct sk_buff *skb, struct net_device *dev, int port );

int wl_send( struct wl_private *lp );

int wl_rx( struct net_device *dev );

void wl_tx_timeout( struct net_device *dev );

struct net_device_stats *wl_stats( struct net_device *dev );


#ifdef ENABLE_DMA
int wl_send_dma( struct wl_private *lp, struct sk_buff *skb, int port );
int wl_rx_dma( struct net_device *dev );
#endif

#ifdef NEW_MULTICAST
void wl_multicast( struct net_device *dev );
#else
void wl_multicast( struct net_device *dev, int num_addrs, void *addrs );
#endif // NEW_MULTICAST


int wl_tx_port0( struct sk_buff *skb, struct net_device *dev );


#ifdef USE_WDS

int wl_tx_port1( struct sk_buff *skb, struct net_device *dev );
int wl_tx_port2( struct sk_buff *skb, struct net_device *dev );
int wl_tx_port3( struct sk_buff *skb, struct net_device *dev );
int wl_tx_port4( struct sk_buff *skb, struct net_device *dev );
int wl_tx_port5( struct sk_buff *skb, struct net_device *dev );
int wl_tx_port6( struct sk_buff *skb, struct net_device *dev );

void wl_wds_device_alloc( struct wl_private *lp );
void wl_wds_device_dealloc( struct wl_private *lp );
void wl_wds_netif_start_queue( struct wl_private *lp );
void wl_wds_netif_stop_queue( struct wl_private *lp );
void wl_wds_netif_wake_queue( struct wl_private *lp );
void wl_wds_netif_carrier_on( struct wl_private *lp );
void wl_wds_netif_carrier_off( struct wl_private *lp );

#endif  /* USE_WDS */


#ifdef USE_WDS

#define WL_WDS_DEVICE_ALLOC( ARG )      wl_wds_device_alloc( ARG )
#define WL_WDS_DEVICE_DEALLOC( ARG )    wl_wds_device_dealloc( ARG )
#define WL_WDS_NETIF_START_QUEUE( ARG ) wl_wds_netif_start_queue( ARG )
#define WL_WDS_NETIF_STOP_QUEUE( ARG )  wl_wds_netif_stop_queue( ARG )
#define WL_WDS_NETIF_WAKE_QUEUE( ARG )  wl_wds_netif_wake_queue( ARG )
#define WL_WDS_NETIF_CARRIER_ON( ARG )  wl_wds_netif_carrier_on( ARG )
#define WL_WDS_NETIF_CARRIER_OFF( ARG ) wl_wds_netif_carrier_off( ARG )

#else

#define WL_WDS_DEVICE_ALLOC( ARG )
#define WL_WDS_DEVICE_DEALLOC( ARG )
#define WL_WDS_NETIF_START_QUEUE( ARG )
#define WL_WDS_NETIF_STOP_QUEUE( ARG )
#define WL_WDS_NETIF_WAKE_QUEUE( ARG )
#define WL_WDS_NETIF_CARRIER_ON( ARG )
#define WL_WDS_NETIF_CARRIER_OFF( ARG )

#endif  /* USE_WDS */


#endif  // __WL_NETDEV_H__
