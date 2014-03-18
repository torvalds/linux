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
 *   Header describing device specific routines and driver init/un-init.
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

#ifndef __WL_MAIN_H__
#define __WL_MAIN_H__




/*******************************************************************************
 *  function prototypes
 ******************************************************************************/
int wl_insert( struct net_device *dev );

void wl_set_wep_keys( struct wl_private *lp );

int wl_put_ltv_init( struct wl_private *lp );

int wl_put_ltv( struct wl_private *lp );

p_u16 wl_get_irq_mask( void );

p_s8 * wl_get_irq_list( void );

int wl_reset( struct net_device *dev );

int wl_go( struct wl_private *lp );

int wl_apply( struct wl_private *lp );

irqreturn_t wl_isr( int irq, void *dev_id, struct pt_regs *regs );

void wl_remove( struct net_device *dev );

void wl_suspend( struct net_device *dev );

void wl_resume( struct net_device *dev );

void wl_release( struct net_device *dev );

int wl_enable( struct wl_private *lp );

int wl_connect( struct wl_private *lp );

int wl_disable( struct wl_private *lp );

int wl_disconnect( struct wl_private *lp );

void wl_enable_wds_ports( struct wl_private * lp );

void wl_disable_wds_ports( struct wl_private * lp );

#ifndef USE_MBOX_SYNC

int wl_mbx( struct wl_private *lp );
void wl_endian_translate_mailbox( ltv_t *ltv );
void wl_process_mailbox( struct wl_private *lp );

#endif  /* USE_MBOX_SYNC */


#ifdef USE_WDS

void wl_wds_netdev_register( struct wl_private *lp );
void wl_wds_netdev_deregister( struct wl_private *lp );

#endif  /* USE_WDS */


#ifdef USE_WDS

#define WL_WDS_NETDEV_REGISTER( ARG )   wl_wds_netdev_register( ARG )
#define WL_WDS_NETDEV_DEREGISTER( ARG ) wl_wds_netdev_deregister( ARG )

#else

#define WL_WDS_NETDEV_REGISTER( ARG )
#define WL_WDS_NETDEV_DEREGISTER( ARG )

#endif  /* USE_WDS */
#endif  /* __WL_MAIN_H__ */
