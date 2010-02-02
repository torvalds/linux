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
 *   Header describing information required for utility functions used
 *   throughout the driver.
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

#ifndef __WL_UTIL_H__
#define __WL_UTIL_H__

/*******************************************************************************
 *  function prototypes
 ******************************************************************************/
int dbm( int value );

int is_valid_key_string( char *s );

void key_string2key( char *ks, KEY_STRCT *key );

int hexdigit2int( char c );

void wl_hcf_error( struct net_device *dev, int hcfStatus );

void wl_endian_translate_event( ltv_t *pLtv );

int wl_has_wep( IFBP ifbp );


#if DBG
const char *DbgHwAddr( unsigned char *hwAddr );
#endif // DBG

hcf_8   wl_parse_ds_ie( PROBE_RESP *probe_rsp );
hcf_8 * wl_parse_wpa_ie( PROBE_RESP *probe_rsp, hcf_16 *length );
hcf_8 * wl_print_wpa_ie( hcf_8 *buffer, int length );

int wl_get_tallies(struct wl_private *, CFG_HERMES_TALLIES_STRCT *);
int wl_is_a_valid_chan( int channel );
int wl_is_a_valid_freq( long frequency );
long wl_get_freq_from_chan( int channel );
int wl_get_chan_from_freq( long frequency );

void wl_process_link_status( struct wl_private *lp );
void wl_process_probe_response( struct wl_private *lp );
void wl_process_updated_record( struct wl_private *lp );
void wl_process_assoc_status( struct wl_private *lp );
void wl_process_security_status( struct wl_private *lp );

unsigned int wl_atoi( char *string );

#endif  // __WL_UTIL_H__
