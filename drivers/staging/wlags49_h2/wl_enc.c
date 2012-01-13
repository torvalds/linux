
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
 *   This file defines functions related to WEP key coding/decoding.
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

/*******************************************************************************
 *  include files
 ******************************************************************************/
#include <linux/string.h>
#include <wl_version.h>

#include <debug.h>
#include <hcf.h>

#include <wl_enc.h>




/*******************************************************************************
 *  global definitions
 ******************************************************************************/
#if DBG

extern dbg_info_t *DbgInfo;

#endif  /* DBG */




/*******************************************************************************
 *	wl_wep_code()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function encodes a set of wep keys for privacy
 *
 *  PARAMETERS:
 *
 *      szCrypt -
 *      szDest  -
 *      Data    -
 *      nLen    -
 *
 *  RETURNS:
 *
 *      OK
 *
 ******************************************************************************/
int wl_wep_code( char *szCrypt, char *szDest, void *Data, int nLen )
{
    int     i;
    int     t;
    int     k ;
    char    bits;
    char    *szData = (char *) Data;
    /*------------------------------------------------------------------------*/


    for( i = bits = 0 ; i < MACADDRESS_STR_LEN; i++ ) {
	    bits ^= szCrypt[i];
	    bits += szCrypt[i];
    }

    for( i = t = *szDest = 0; i < nLen; i++, t++ ) {
	    k = szData[i] ^ ( bits + i );


        switch( i % 3 ) {

        case 0 :

            szDest[t]   = ((k & 0xFC) >> 2) + CH_START ;
			szDest[t+1] = ((k & 0x03) << 4) + CH_START ;
	        szDest[t+2] = '\0';

            break;


        case 1 :

            szDest[t]  += (( k & 0xF0 ) >> 4 );
			szDest[t+1] = (( k & 0x0F ) << 2 ) + CH_START ;
	        szDest[t+2] = '\0';

            break;


        case 2 :

            szDest[t]  += (( k & 0xC0 ) >> 6 );
			szDest[t+1] = ( k & 0x3F ) + CH_START ;
	        szDest[t+2] = '\0';
	        t++;

            break;
        }
    }

    return( strlen( szDest )) ;

}
/*============================================================================*/




/*******************************************************************************
 *	wl_wep_decode()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function decodes a set of WEP keys for use by the card.
 *
 *  PARAMETERS:
 *
 *      szCrypt -
 *      szDest  -
 *      Data    -
 *
 *  RETURNS:
 *
 *      OK
 *
 ******************************************************************************/
int wl_wep_decode( char *szCrypt, void *Dest, char *szData )
{
    int     i;
    int     t;
    int     nLen;
    char    bits;
    char    *szDest = Dest;
  /*------------------------------------------------------------------------*/


  for( i = bits = 0 ; i < 12; i++ ) {
      bits ^= szCrypt[i] ;
      bits += szCrypt[i] ;
  }

  nLen = ( strlen( szData ) * 3) / 4 ;

  for( i = t = 0; i < nLen; i++, t++ ) {
      switch( i % 3 ) {
      case 0 :

          szDest[i] = ((( szData[t]-CH_START ) & 0x3f ) << 2 ) +
                      ((( szData[t+1]-CH_START ) & 0x30 ) >> 4 );
	      break;


      case 1 :
          szDest[i] = ((( szData[t]-CH_START ) & 0x0f ) << 4 ) +
                      ((( szData[t+1]-CH_START ) & 0x3c ) >> 2 );
	      break;


      case 2 :
          szDest[i] = ((( szData[t]-CH_START ) & 0x03 ) << 6 ) +
                       (( szData[t+1]-CH_START ) & 0x3f );
	      t++;
	      break;
      }

	szDest[i] ^= ( bits + i ) ;

  }

  return( i ) ;

}
/*============================================================================*/

