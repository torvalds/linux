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
 *   Header for performing coding/decoding of the WEP keys.
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

#ifndef __WAVELAN2_ENCRYPTION_H__
#define __WAVELAN2_ENCRYPTION_H__




/*******************************************************************************
 * constant definitions
 ******************************************************************************/
#define CRYPT_CODE					"57617665A5D6"
#define ENCRYPTION_LEN				102
#define ENCRYPTION_MAGIC			0x48576877L	// HWhw
#define DEF_CRYPT_STR				"G?TIUEA]d5MAdZV'eUb&&6.)'&:,'VF/(FR2)6^5*'*8*W6;+GB>,7NA-'ZD-X&G.H2J/8>M0(JP0XVS1HbV29.Y3):\\3YF_4IRb56"

#define DEFAULT_CRYPT_MAC			"W\x01\x6B\x66\xA5\x5A"
#define CH_START					'&'
#define MACADDRESS_STR_LEN			12

#define KEY_LEN                     14
#define NUM_KEYS                    4

#define KEY_LENGTH_NONE_ASCII       0
#define KEY_LENGTH_64_BIT_ASCII     5
#define KEY_LENGTH_128_BIT_ASCII    13

#define KEY_LENGTH_NONE_HEX         ( KEY_LENGTH_NONE_ASCII    * sizeof( unsigned short ))
#define KEY_LENGTH_64_BIT_HEX       ( KEY_LENGTH_64_BIT_ASCII  * sizeof( unsigned short ))
#define KEY_LENGTH_128_BIT_HEX      ( KEY_LENGTH_128_BIT_ASCII * sizeof( unsigned short ))




/*******************************************************************************
 * type definitions
 ******************************************************************************/
typedef struct _encstct
{
	hcf_32	                   dwMagic;
	hcf_16	                   wTxKeyID;
	hcf_16	                   wEnabled;
	CFG_DEFAULT_KEYS_STRCT     EncStr;
}
ENCSTRCT, *PENCSTRCT;




/*******************************************************************************
 * function prototypes
 ******************************************************************************/
int wl_wep_code( char *szCrypt, char *szDest, void *Data, int nLen );

int wl_wep_decode( char *szCrypt, void *Dest, char *szData );




#endif  // __WAVELAN2_ENCRYPTION_H__
