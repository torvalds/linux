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
 *   This file defines misc utility functions.
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
#include <wl_version.h>

#include <linux/kernel.h>
// #include <linux/sched.h>
// #include <linux/ptrace.h>
#include <linux/ctype.h>
// #include <linux/string.h>
// #include <linux/timer.h>
// #include <linux/interrupt.h>
// #include <linux/in.h>
// #include <linux/delay.h>
// #include <asm/io.h>
// // #include <asm/bitops.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
// #include <linux/skbuff.h>
// #include <linux/if_arp.h>
// #include <linux/ioport.h>

#include <debug.h>
#include <hcf.h>
// #include <hcfdef.h>

#include <wl_if.h>
#include <wl_internal.h>
#include <wl_util.h>
#include <wl_wext.h>
#include <wl_main.h>



/*******************************************************************************
 * global variables
 ******************************************************************************/

/* A matrix which maps channels to frequencies */
static const long chan_freq_list[][2] =
{
    {1,2412},
    {2,2417},
    {3,2422},
    {4,2427},
    {5,2432},
    {6,2437},
    {7,2442},
    {8,2447},
    {9,2452},
    {10,2457},
    {11,2462},
    {12,2467},
    {13,2472},
    {14,2484},
    {36,5180},
    {40,5200},
    {44,5220},
    {48,5240},
    {52,5260},
    {56,5280},
    {60,5300},
    {64,5320},
    {149,5745},
    {153,5765},
    {157,5785},
    {161,5805}
};

/*******************************************************************************
 *	dbm()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Return an energy value in dBm.
 *
 *  PARAMETERS:
 *
 *      value - the energy value to be converted
 *
 *  RETURNS:
 *
 *      the value in dBm
 *
 ******************************************************************************/
int dbm( int value )
{
    /* Truncate the value to be between min and max. */
    if( value < HCF_MIN_SIGNAL_LEVEL )
        value = HCF_MIN_SIGNAL_LEVEL;

    if( value > HCF_MAX_SIGNAL_LEVEL )
        value = HCF_MAX_SIGNAL_LEVEL;

    /* Return the energy value in dBm. */
    return ( value - HCF_0DBM_OFFSET );
} // dbm
/*============================================================================*/




/*******************************************************************************
 *	percent()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Return a value as a percentage of min to max.
 *
 *  PARAMETERS:
 *
 *      value   - the value in question
 *      min     - the minimum range value
 *      max     - the maximum range value
 *
 *  RETURNS:
 *
 *      the percentage value
 *
 ******************************************************************************/
int percent( int value, int min, int max )
{
    /* Truncate the value to be between min and max. */
    if( value < min )
        value = min;

    if( value > max )
        value = max;

    /* Return the value as a percentage of min to max. */
    return ((( value - min ) * 100 ) / ( max - min ));
} // percent
/*============================================================================*/




/*******************************************************************************
 *	is_valid_key_string()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Checks to determine if the WEP key string is valid
 *
 *  PARAMETERS:
 *
 *      s - the string in question
 *
 *  RETURNS:
 *
 *      non-zero if the string contains a valid key
 *
 ******************************************************************************/
int is_valid_key_string( char *s )
{
    int l;
    int i;
    /*------------------------------------------------------------------------*/


    l = strlen( s );

    /* 0x followed by 5 or 13 hexadecimal digit pairs is valid */
    if( s[0] == '0' && ( s[1] == 'x' || s[1] == 'X' )) {
        if( l == 12 || l == 28 ) {
            for( i = 2; i < l; i++ ) {
                if( !isxdigit( s[i] ))
                    return 0;
            }

            return 1;
        } else {
            return 0;
        }
    }

    /* string with 0, 5, or 13 characters is valid */
    else
    {
        return( l == 0 || l == 5 || l == 13 );
    }
} // is_valid_key_string
/*============================================================================*/




/*******************************************************************************
 *	key_string2key()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Converts a key_string to a key, Assumes the key_string is validated with
 *  is_valid_key_string().
 *
 *  PARAMETERS:
 *
 *      ks  - the valid key string
 *      key - a pointer to a KEY_STRUCT where the converted key information will
 *            be stored.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void key_string2key( char *ks, KEY_STRCT *key )
{
    int l,i,n;
    char *p;
    /*------------------------------------------------------------------------*/


    l = strlen( ks );

    /* 0x followed by hexadecimal digit pairs */
    if( ks[0] == '0' && ( ks[1] == 'x' || ks[1] == 'X' )) {
        n = 0;
        p = (char *)key->key;

        for( i = 2; i < l; i+=2 ) {
			*p++ = (hex_to_bin(ks[i]) << 4) + hex_to_bin(ks[i+1]);
           n++;
        }

        /* Note that endian translation of the length field is not needed here
          because it's performed in wl_put_ltv() */
        key->len = n;
    }
    /* character string */
    else
    {
        strcpy( (char *)key->key, ks );
        key->len = l;
    }

    return;
} // key_string2key
/*============================================================================*/




/*******************************************************************************
 *	wl_has_wep()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Checks to see if the device supports WEP
 *
 *  PARAMETERS:
 *
 *      ifbp    - the IFB pointer of the device in question
 *
 *  RETURNS:
 *
 *      1 if WEP is known enabled, else 0
 *
 ******************************************************************************/
int wl_has_wep (IFBP ifbp)
{
    CFG_PRIVACY_OPT_IMPLEMENTED_STRCT ltv;
	int rc, privacy;
    /*------------------------------------------------------------------------*/


	/* This function allows us to distiguish bronze cards from other types, to
       know if WEP exists. Does not distinguish (because there's no way to)
       between silver and gold cards. */
    ltv.len = 2;
    ltv.typ = CFG_PRIVACY_OPT_IMPLEMENTED;

	rc = hcf_get_info( ifbp, (LTVP) &ltv );

	privacy = CNV_LITTLE_TO_INT( ltv.privacy_opt_implemented );

	//return rc ? 0 : privacy;
    return 1;
} // wl_has_wep
/*============================================================================*/




/*******************************************************************************
 *	wl_hcf_error()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Report the type of HCF error message
 *
 *  PARAMETERS:
 *
 *      none
 *
 *  RETURNS:
 *
 *      A descriptive string indicating the error, quiet otherwise.
 *
 ******************************************************************************/
void wl_hcf_error( struct net_device *dev, int hcfStatus )
{
    char     buffer[64], *pMsg;
    /*------------------------------------------------------------------------*/


    if( hcfStatus != HCF_SUCCESS ) {
        switch( hcfStatus ) {

        case HCF_ERR_TIME_OUT:

            pMsg = "Expected adapter event did not occur in expected time";
            break;


        case HCF_ERR_NO_NIC:

            pMsg = "Card not found (ejected unexpectedly)";
            break;


        case HCF_ERR_LEN:

            pMsg = "Command buffer size insufficient";
            break;


        case HCF_ERR_INCOMP_PRI:

            pMsg = "Primary functions are not compatible";
            break;


        case HCF_ERR_INCOMP_FW:

            pMsg = "Primary functions are compatible, "
                "station/ap functions are not";
            break;


        case HCF_ERR_BUSY:

            pMsg = "Inquire cmd while another Inquire in progress";
            break;


        //case HCF_ERR_SEQ_BUG:

        //    pMsg = "Unexpected command completed";
        //    break;


        case HCF_ERR_DEFUNCT_AUX:

            pMsg = "Timeout on ack for enable/disable of AUX registers";
            break;


        case HCF_ERR_DEFUNCT_TIMER:
            pMsg = "Timeout on timer calibration during initialization process";
            break;


        case HCF_ERR_DEFUNCT_TIME_OUT:
            pMsg = "Timeout on Busy bit drop during BAP setup";
            break;


        case HCF_ERR_DEFUNCT_CMD_SEQ:
            pMsg = "Hermes and HCF are out of sync";
            break;


        default:

            sprintf( buffer, "Error code %d", hcfStatus );
            pMsg = buffer;
            break;
        }

        printk( KERN_INFO "%s: Wireless, HCF failure: \"%s\"\n",
                dev->name, pMsg );
    }
} // wl_hcf_error
/*============================================================================*/




/*******************************************************************************
 *	wl_endian_translate_event()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Determines what type of data is in the mailbox and performs the proper
 *  endian translation.
 *
 *  PARAMETERS:
 *
 *      pLtv - an LTV pointer
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_endian_translate_event( ltv_t *pLtv )
{
    switch( pLtv->typ ) {
    case CFG_TALLIES:
        break;


    case CFG_SCAN:
        {
            int numAPs;
            SCAN_RS_STRCT *pAps = (SCAN_RS_STRCT*)&pLtv->u.u8[0];

            numAPs = (hcf_16)(( (size_t)( pLtv->len - 1 ) * 2 ) /
                                (sizeof( SCAN_RS_STRCT )));

            while( numAPs >= 1 ) {
                numAPs--;

                pAps[numAPs].channel_id           =
                    CNV_LITTLE_TO_INT( pAps[numAPs].channel_id );

                pAps[numAPs].noise_level          =
                    CNV_LITTLE_TO_INT( pAps[numAPs].noise_level );

                pAps[numAPs].signal_level         =
                    CNV_LITTLE_TO_INT( pAps[numAPs].signal_level );

                pAps[numAPs].beacon_interval_time =
                    CNV_LITTLE_TO_INT( pAps[numAPs].beacon_interval_time );

                pAps[numAPs].capability           =
                    CNV_LITTLE_TO_INT( pAps[numAPs].capability );

                pAps[numAPs].ssid_len             =
                    CNV_LITTLE_TO_INT( pAps[numAPs].ssid_len );

                pAps[numAPs].ssid_val[pAps[numAPs].ssid_len] = 0;

            }
        }
        break;


    case CFG_ACS_SCAN:
        {
            PROBE_RESP *probe_resp = (PROBE_RESP *)pLtv;

            probe_resp->frameControl   = CNV_LITTLE_TO_INT( probe_resp->frameControl );
            probe_resp->durID          = CNV_LITTLE_TO_INT( probe_resp->durID );
            probe_resp->sequence       = CNV_LITTLE_TO_INT( probe_resp->sequence );
            probe_resp->dataLength     = CNV_LITTLE_TO_INT( probe_resp->dataLength );

#ifndef WARP
            probe_resp->lenType        = CNV_LITTLE_TO_INT( probe_resp->lenType );
#endif // WARP

            probe_resp->beaconInterval = CNV_LITTLE_TO_INT( probe_resp->beaconInterval );
            probe_resp->capability     = CNV_LITTLE_TO_INT( probe_resp->capability );
            probe_resp->flags          = CNV_LITTLE_TO_INT( probe_resp->flags );
        }
        break;


    case CFG_LINK_STAT:
#define ls ((LINK_STATUS_STRCT *)pLtv)
            ls->linkStatus = CNV_LITTLE_TO_INT( ls->linkStatus );
        break;
#undef ls

    case CFG_ASSOC_STAT:
        {
            ASSOC_STATUS_STRCT *pAs = (ASSOC_STATUS_STRCT *)pLtv;

            pAs->assocStatus = CNV_LITTLE_TO_INT( pAs->assocStatus );
        }
        break;


    case CFG_SECURITY_STAT:
        {
            SECURITY_STATUS_STRCT *pSs = (SECURITY_STATUS_STRCT *)pLtv;

            pSs->securityStatus = CNV_LITTLE_TO_INT( pSs->securityStatus );
            pSs->reason         = CNV_LITTLE_TO_INT( pSs->reason );
        }
        break;


    case CFG_WMP:
        break;


    case CFG_NULL:
        break;


    default:
        break;
    }
} // wl_endian_translate_event
/*============================================================================*/


/*******************************************************************************
 *	msf_assert()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Print statement used to display asserts from within the HCF. Only called
 *  when asserts in the HCF are turned on. See hcfcfg.h for more information.
 *
 *  PARAMETERS:
 *
 *      file_namep  - the filename in which the assert occurred.
 *      line_number - the line number on which the assert occurred.
 *      trace       - a comment associated with the assert.
 *      qual        - return code or other value related to the assert
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void msf_assert( unsigned int line_number, hcf_16 trace, hcf_32 qual )
{
    DBG_PRINT( "HCF ASSERT: Line %d, VAL: 0x%.8x\n", line_number, /*;?*/(u32)qual );
} // msf_assert
/*============================================================================*/




/*******************************************************************************
 *	wl_parse_ds_ie()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function parses the Direct Sequence Parameter Set IE, used to
 *      determine channel/frequency information.
 *
 *  PARAMETERS:
 *
 *      probe_rsp - a pointer to a PROBE_RESP structure containing the probe
 *                  response.
 *
 *  RETURNS:
 *
 *      The channel on which the BSS represented by this probe response is
 *      transmitting.
 *
 ******************************************************************************/
hcf_8 wl_parse_ds_ie( PROBE_RESP *probe_rsp )
{
    int     i;
    int     ie_length = 0;
    hcf_8   *buf;
    hcf_8   buf_size;
    /*------------------------------------------------------------------------*/


    if( probe_rsp == NULL ) {
        return 0;
    }

    buf      = probe_rsp->rawData;
    buf_size = sizeof( probe_rsp->rawData );


    for( i = 0; i < buf_size; i++ ) {
        if( buf[i] == DS_INFO_ELEM ) {
            /* Increment by 1 to get the length, and test it; in a DS element,
               length should always be 1 */
            i++;
            ie_length = buf[i];

            if( buf[i] == 1 ) {
                /* Get the channel information */
                i++;
                return buf[i];
            }
        }
    }

    /* If we get here, we didn't find a DS-IE, which is strange */
    return 0;
} // wl_parse_ds_ie


/*******************************************************************************
 *	wl_parse_wpa_ie()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function parses the Probe Response for a valid WPA-IE.
 *
 *  PARAMETERS:
 *
 *      probe_rsp - a pointer to a PROBE_RESP structure containing the probe
 *                  response
 *      length    - a pointer to an hcf_16 in which the size of the WPA-IE will
 *                  be stored (if found).
 *
 *  RETURNS:
 *
 *      A pointer to the location in the probe response buffer where a valid
 *      WPA-IE lives. The length of this IE is written back to the 'length'
 *      argument passed to the function.
 *
 ******************************************************************************/
hcf_8 * wl_parse_wpa_ie( PROBE_RESP *probe_rsp, hcf_16 *length )
{
    int     i;
    int     ie_length = 0;
    hcf_8   *buf;
    hcf_8   buf_size;
    hcf_8   wpa_oui[] = WPA_OUI_TYPE;
    /*------------------------------------------------------------------------*/


    if( probe_rsp == NULL || length == NULL ) {
        return NULL;
    }

    buf      = probe_rsp->rawData;
    buf_size = sizeof( probe_rsp->rawData );
    *length  = 0;


    for( i = 0; i < buf_size; i++ ) {
        if( buf[i] == GENERIC_INFO_ELEM ) {
            /* Increment by one to get the IE length */
            i++;
            ie_length = probe_rsp->rawData[i];

            /* Increment by one to point to the IE payload */
            i++;

            /* Does the IE contain a WPA OUI? If not, it's a proprietary IE */
            if( memcmp( &buf[i], &wpa_oui, WPA_SELECTOR_LEN ) == 0 ) {
                /* Pass back length and return a pointer to the WPA-IE */
                /* NOTE: Length contained in the WPA-IE is only the length of
                   the payload. The entire WPA-IE, including the IE identifier
                   and the length, is 2 bytes larger */
                *length = ie_length + 2;

                /* Back up the pointer 2 bytes to include the IE identifier and
                   the length in the buffer returned */
                i -= 2;
                return &buf[i];
            }

            /* Increment past this non-WPA IE and continue looking */
            i += ( ie_length - 1 );
        }
    }

    /* If we're here, we didn't find a WPA-IE in the buffer */
    return NULL;
} // wl_parse_wpa_ie


/*******************************************************************************
 *	wl_print_wpa_ie()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Function used to take a WPA Information Element (WPA-IE) buffer and
 *      display it in a readable format.
 *
 *  PARAMETERS:
 *
 *      buffer - the byte buffer containing the WPA-IE
 *      length - the length of the above buffer
 *
 *  RETURNS:
 *
 *      A pointer to the formatted WPA-IE string. Note that the format used is
 *      byte-by-byte printing as %02x hex values with no spaces. This is
 *      required for proper operation with some WPA supplicants.
 *
 ******************************************************************************/
hcf_8 * wl_print_wpa_ie( hcf_8 *buffer, int length )
{
    int count;
    int rows;
    int remainder;
    int rowsize = 4;
    hcf_8 row_buf[64];
    static hcf_8 output[512];
    /*------------------------------------------------------------------------*/


    memset( output, 0, sizeof( output ));
    memset( row_buf, 0, sizeof( row_buf ));


    /* Determine how many rows will be needed, and the remainder */
    rows = length / rowsize;
    remainder = length % rowsize;


    /* Format the rows */
    for( count = 0; count < rows; count++ ) {
        sprintf( row_buf, "%02x%02x%02x%02x",
                 buffer[count*rowsize], buffer[count*rowsize+1],
                 buffer[count*rowsize+2], buffer[count*rowsize+3]);
        strcat( output, row_buf );
    }

    memset( row_buf, 0, sizeof( row_buf ));


    /* Format the remainder */
    for( count = 0; count < remainder; count++ ) {
        sprintf( row_buf, "%02x", buffer[(rows*rowsize)+count]);
        strcat( output, row_buf );
    }

    return output;
} // wl_print_wpa_ie
/*============================================================================*/




/*******************************************************************************
 *	wl_is_a_valid_chan()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Checks if a given channel is valid
 *
 *  PARAMETERS:
 *
 *      channel - the channel
 *
 *  RETURNS:
 *
 *      1 if TRUE
 *      0 if FALSE
 *
 ******************************************************************************/
int wl_is_a_valid_chan( int channel )
{
    int i;
    /*------------------------------------------------------------------------*/


    /* Strip out the high bit set by the FW for 802.11a channels */
    if( channel & 0x100 ) {
        channel = channel & 0x0FF;
    }

    /* Iterate through the matrix and retrieve the frequency */
    for( i = 0; i < ARRAY_SIZE(chan_freq_list); i++ ) {
        if( chan_freq_list[i][0] == channel ) {
            return 1;
        }
    }

    return 0;
} // wl_is_a_valid_chan
/*============================================================================*/




/*******************************************************************************
 *	wl_get_chan_from_freq()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Checks if a given frequency is valid
 *
 *  PARAMETERS:
 *
 *      freq - the frequency
 *
 *  RETURNS:
 *
 *      1 if TRUE
 *      0 if FALSE
 *
 ******************************************************************************/
int wl_is_a_valid_freq( long frequency )
{
    int i;
    /*------------------------------------------------------------------------*/


    /* Iterate through the matrix and retrieve the channel */
    for( i = 0; i < ARRAY_SIZE(chan_freq_list); i++ ) {
        if( chan_freq_list[i][1] == frequency ) {
            return 1;
        }
    }

    return 0;
} // wl_is_a_valid_freq
/*============================================================================*/




/*******************************************************************************
 *	wl_get_freq_from_chan()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Function used to look up the frequency for a given channel on which the
 *      adapter is Tx/Rx.
 *
 *  PARAMETERS:
 *
 *      channel - the channel
 *
 *  RETURNS:
 *
 *      The corresponding frequency
 *
 ******************************************************************************/
long wl_get_freq_from_chan( int channel )
{
    int i;
    /*------------------------------------------------------------------------*/


    /* Strip out the high bit set by the FW for 802.11a channels */
    if( channel & 0x100 ) {
        channel = channel & 0x0FF;
    }

    /* Iterate through the matrix and retrieve the frequency */
    for( i = 0; i < ARRAY_SIZE(chan_freq_list); i++ ) {
        if( chan_freq_list[i][0] == channel ) {
            return chan_freq_list[i][1];
        }
    }

    return 0;
} // wl_get_freq_from_chan
/*============================================================================*/




/*******************************************************************************
 *	wl_get_chan_from_freq()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Function used to look up the channel for a given frequency on which the
 *      adapter is Tx/Rx.
 *
 *  PARAMETERS:
 *
 *      frequency - the frequency
 *
 *  RETURNS:
 *
 *      The corresponding channel
 *
 ******************************************************************************/
int wl_get_chan_from_freq( long frequency )
{
    int i;
    /*------------------------------------------------------------------------*/


    /* Iterate through the matrix and retrieve the channel */
    for( i = 0; i < ARRAY_SIZE(chan_freq_list); i++ ) {
        if( chan_freq_list[i][1] == frequency ) {
            return chan_freq_list[i][0];
        }
    }

    return 0;
} // wl_get_chan_from_freq
/*============================================================================*/




/*******************************************************************************
 *	wl_process_link_status()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Process the link status message signaled by the device.
 *
 *  PARAMETERS:
 *
 *      lp - a pointer to the device's private structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_process_link_status( struct wl_private *lp )
{
    hcf_16 link_stat;

    if( lp != NULL ) {
        //link_stat = lp->hcfCtx.IFB_DSLinkStat & CFG_LINK_STAT_FW;
        link_stat = lp->hcfCtx.IFB_LinkStat & CFG_LINK_STAT_FW;
        switch( link_stat ) {
        case 1:
            DBG_TRACE( DbgInfo, "Link Status : Connected\n" );
            wl_wext_event_ap( lp->dev );
            break;
        case 2:
            DBG_TRACE( DbgInfo, "Link Status : Disconnected\n"  );
            break;
        case 3:
            DBG_TRACE( DbgInfo, "Link Status : Access Point Change\n" );
            break;
        case 4:
            DBG_TRACE( DbgInfo, "Link Status : Access Point Out of Range\n" );
            break;
        case 5:
            DBG_TRACE( DbgInfo, "Link Status : Access Point In Range\n" );
            break;
        default:
            DBG_TRACE( DbgInfo, "Link Status : UNKNOWN (0x%04x)\n", link_stat );
            break;
        }
    }
} // wl_process_link_status
/*============================================================================*/




/*******************************************************************************
 *	wl_process_probe_response()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Process the probe responses retunred by the device as a result of an
 *      active scan.
 *
 *  PARAMETERS:
 *
 *      lp - a pointer to the device's private structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_process_probe_response( struct wl_private *lp )
{
    PROBE_RESP  *probe_rsp;
    hcf_8       *wpa_ie = NULL;
    hcf_16      wpa_ie_len = 0;

    if( lp != NULL ) {
        probe_rsp = (PROBE_RESP *)&lp->ProbeResp;

        wl_endian_translate_event( (ltv_t *)probe_rsp );

        DBG_TRACE( DbgInfo, "(%s) =========================\n", lp->dev->name );
        DBG_TRACE( DbgInfo, "(%s) length      : 0x%04x.\n",  lp->dev->name,
                probe_rsp->length );

        if( probe_rsp->length > 1 ) {
            DBG_TRACE( DbgInfo, "(%s) infoType    : 0x%04x.\n", lp->dev->name,
                    probe_rsp->infoType );

            DBG_TRACE( DbgInfo, "(%s) signal      : 0x%02x.\n", lp->dev->name,
                    probe_rsp->signal );

            DBG_TRACE( DbgInfo, "(%s) silence     : 0x%02x.\n", lp->dev->name,
                    probe_rsp->silence );

            DBG_TRACE( DbgInfo, "(%s) rxFlow      : 0x%02x.\n", lp->dev->name,
                    probe_rsp->rxFlow );

            DBG_TRACE( DbgInfo, "(%s) rate        : 0x%02x.\n", lp->dev->name,
                    probe_rsp->rate );

            DBG_TRACE( DbgInfo, "(%s) frame cntl  : 0x%04x.\n", lp->dev->name,
                    probe_rsp->frameControl );

            DBG_TRACE( DbgInfo, "(%s) durID       : 0x%04x.\n", lp->dev->name,
                    probe_rsp->durID );

		DBG_TRACE(DbgInfo, "(%s) address1    : %pM\n", lp->dev->name,
			probe_rsp->address1);

		DBG_TRACE(DbgInfo, "(%s) address2    : %pM\n", lp->dev->name,
			probe_rsp->address2);

		DBG_TRACE(DbgInfo, "(%s) BSSID       : %pM\n", lp->dev->name,
			probe_rsp->BSSID);

            DBG_TRACE( DbgInfo, "(%s) sequence    : 0x%04x.\n", lp->dev->name,
                    probe_rsp->sequence );

		DBG_TRACE(DbgInfo, "(%s) address4    : %pM\n", lp->dev->name,
			probe_rsp->address4);

            DBG_TRACE( DbgInfo, "(%s) datalength  : 0x%04x.\n", lp->dev->name,
                    probe_rsp->dataLength );

		DBG_TRACE(DbgInfo, "(%s) DA          : %pM\n", lp->dev->name,
			probe_rsp->DA);

		DBG_TRACE(DbgInfo, "(%s) SA          : %pM\n", lp->dev->name,
			probe_rsp->SA);

#ifdef WARP

            DBG_TRACE( DbgInfo, "(%s) channel     : %d\n", lp->dev->name,
                    probe_rsp->channel );

            DBG_TRACE( DbgInfo, "(%s) band        : %d\n", lp->dev->name,
                    probe_rsp->band );
#else
            DBG_TRACE( DbgInfo, "(%s) lenType     : 0x%04x.\n", lp->dev->name,
                    probe_rsp->lenType );
#endif  // WARP

            DBG_TRACE( DbgInfo, "(%s) timeStamp   : %d.%d.%d.%d.%d.%d.%d.%d\n",
                    lp->dev->name,
                    probe_rsp->timeStamp[0],
                    probe_rsp->timeStamp[1],
                    probe_rsp->timeStamp[2],
                    probe_rsp->timeStamp[3],
                    probe_rsp->timeStamp[4],
                    probe_rsp->timeStamp[5],
                    probe_rsp->timeStamp[6],
                    probe_rsp->timeStamp[7]);

            DBG_TRACE( DbgInfo, "(%s) beaconInt   : 0x%04x.\n", lp->dev->name,
                    probe_rsp->beaconInterval );

            DBG_TRACE( DbgInfo, "(%s) capability  : 0x%04x.\n", lp->dev->name,
                    probe_rsp->capability );

            DBG_TRACE( DbgInfo, "(%s) SSID len    : 0x%04x.\n", lp->dev->name,
                    probe_rsp->rawData[1] );


            if( probe_rsp->rawData[1] > 0 ) {
                char ssid[HCF_MAX_NAME_LEN];

                memset( ssid, 0, sizeof( ssid ));
                strncpy( ssid, &probe_rsp->rawData[2],
                            probe_rsp->rawData[1] );

                DBG_TRACE( DbgInfo, "(%s) SSID        : %s\n",
                            lp->dev->name, ssid );
            }


            /* Parse out the WPA-IE, if one exists */
            wpa_ie = wl_parse_wpa_ie( probe_rsp, &wpa_ie_len );
            if( wpa_ie != NULL ) {
                DBG_TRACE( DbgInfo, "(%s) WPA-IE      : %s\n",
                lp->dev->name, wl_print_wpa_ie( wpa_ie, wpa_ie_len ));
            }

            DBG_TRACE( DbgInfo, "(%s) flags       : 0x%04x.\n",
                        lp->dev->name, probe_rsp->flags );
        }

        DBG_TRACE( DbgInfo, "\n" );


        /* If probe response length is 1, then the scan is complete */
        if( probe_rsp->length == 1 ) {
            DBG_TRACE( DbgInfo, "SCAN COMPLETE\n" );
            lp->probe_results.num_aps = lp->probe_num_aps;
            lp->probe_results.scan_complete = TRUE;

            /* Reset the counter for the next scan request */
            lp->probe_num_aps = 0;

            /* Send a wireless extensions event that the scan completed */
            wl_wext_event_scan_complete( lp->dev );
        } else {
            /* Only copy to the table if the entry is unique; APs sometimes
                respond more than once to a probe */
            if( lp->probe_num_aps == 0 ) {
                /* Copy the info to the ScanResult structure in the private
                adapter struct */
                memcpy( &( lp->probe_results.ProbeTable[lp->probe_num_aps] ),
                        probe_rsp, sizeof( PROBE_RESP ));

                /* Increment the number of APs detected */
                lp->probe_num_aps++;
            } else {
                int count;
                int unique = 1;

                for( count = 0; count < lp->probe_num_aps; count++ ) {
                    if( memcmp( &( probe_rsp->BSSID ),
                        lp->probe_results.ProbeTable[count].BSSID,
                        ETH_ALEN ) == 0 ) {
                        unique = 0;
                    }
                }

                if( unique ) {
                    /* Copy the info to the ScanResult structure in the
                    private adapter struct. Only copy if there's room in the
                    table */
                    if( lp->probe_num_aps < MAX_NAPS )
                    {
                        memcpy( &( lp->probe_results.ProbeTable[lp->probe_num_aps] ),
                                probe_rsp, sizeof( PROBE_RESP ));
                    }
                    else
                    {
                        DBG_WARNING( DbgInfo, "Num of scan results exceeds storage, truncating\n" );
                    }

                    /* Increment the number of APs detected. Note I do this
                        here even when I don't copy the probe response to the
                        buffer in order to detect the overflow condition */
                    lp->probe_num_aps++;
                }
            }
        }
    }
} // wl_process_probe_response
/*============================================================================*/




/*******************************************************************************
 *	wl_process_updated_record()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Process the updated information record message signaled by the device.
 *
 *  PARAMETERS:
 *
 *      lp - a pointer to the device's private structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_process_updated_record( struct wl_private *lp )
{
    if( lp != NULL ) {
        lp->updatedRecord.u.u16[0] = CNV_LITTLE_TO_INT( lp->updatedRecord.u.u16[0] );

        switch( lp->updatedRecord.u.u16[0] ) {
        case CFG_CUR_COUNTRY_INFO:
            DBG_TRACE( DbgInfo, "Updated Record: CFG_CUR_COUNTRY_INFO\n" );
            wl_connect( lp );
            break;

        case CFG_PORT_STAT:
            DBG_TRACE( DbgInfo, "Updated Record: WAIT_FOR_CONNECT (0xFD40)\n" );
            //wl_connect( lp );
            break;

        default:
            DBG_TRACE( DbgInfo, "UNKNOWN: 0x%04x\n",
                       lp->updatedRecord.u.u16[0] );
        }
    }
} // wl_process_updated_record
/*============================================================================*/




/*******************************************************************************
 *	wl_process_assoc_status()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Process the association status event signaled by the device.
 *
 *  PARAMETERS:
 *
 *      lp - a pointer to the device's private structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_process_assoc_status( struct wl_private *lp )
{
    ASSOC_STATUS_STRCT *assoc_stat;

    if( lp != NULL ) {
        assoc_stat = (ASSOC_STATUS_STRCT *)&lp->assoc_stat;

        wl_endian_translate_event( (ltv_t *)assoc_stat );

        switch( assoc_stat->assocStatus ) {
        case 1:
            DBG_TRACE( DbgInfo, "Association Status : STA Associated\n" );
            break;

        case 2:
            DBG_TRACE( DbgInfo, "Association Status : STA Reassociated\n" );
            break;

        case 3:
            DBG_TRACE( DbgInfo, "Association Status : STA Disassociated\n" );
            break;

        default:
            DBG_TRACE( DbgInfo, "Association Status : UNKNOWN (0x%04x)\n",
                        assoc_stat->assocStatus );
            break;
        }

	DBG_TRACE(DbgInfo, "STA Address        : %pM\n", assoc_stat->staAddr);

        if(( assoc_stat->assocStatus == 2 )  && ( assoc_stat->len == 8 )) {
		DBG_TRACE(DbgInfo, "Old AP Address     : %pM\n",
			assoc_stat->oldApAddr);
        }
    }
} // wl_process_assoc_status
/*============================================================================*/




/*******************************************************************************
 *	wl_process_security_status()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Process the security status message signaled by the device.
 *
 *  PARAMETERS:
 *
 *      lp - a pointer to the device's private structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_process_security_status( struct wl_private *lp )
{
    SECURITY_STATUS_STRCT *sec_stat;

    if( lp != NULL ) {
        sec_stat = (SECURITY_STATUS_STRCT *)&lp->sec_stat;

        wl_endian_translate_event( (ltv_t *)sec_stat );

        switch( sec_stat->securityStatus ) {
        case 1:
            DBG_TRACE( DbgInfo, "Security Status : Dissassociate [AP]\n" );
            break;

        case 2:
            DBG_TRACE( DbgInfo, "Security Status : Deauthenticate [AP]\n" );
            break;

        case 3:
            DBG_TRACE( DbgInfo, "Security Status : Authenticate Fail [STA] or [AP]\n" );
            break;

        case 4:
            DBG_TRACE( DbgInfo, "Security Status : MIC Fail\n" );
            break;

        case 5:
            DBG_TRACE( DbgInfo, "Security Status : Associate Fail\n" );
            break;

        default:
            DBG_TRACE( DbgInfo, "Security Status : UNKNOWN (0x%04x)\n",
                        sec_stat->securityStatus );
            break;
        }

	DBG_TRACE(DbgInfo, "STA Address     : %pM\n", sec_stat->staAddr);
	DBG_TRACE(DbgInfo, "Reason          : 0x%04x\n", sec_stat->reason);

    }
} // wl_process_security_status
/*============================================================================*/

int wl_get_tallies(struct wl_private *lp,
		   CFG_HERMES_TALLIES_STRCT *tallies)
{
    int ret = 0;
    int status;
    CFG_HERMES_TALLIES_STRCT *pTallies;

    /* Get the current tallies from the adapter */
    lp->ltvRecord.len = 1 + HCF_TOT_TAL_CNT * sizeof(hcf_16);
    lp->ltvRecord.typ = CFG_TALLIES;

    status = hcf_get_info(&(lp->hcfCtx), (LTVP)&(lp->ltvRecord));

    if( status == HCF_SUCCESS ) {
	pTallies = (CFG_HERMES_TALLIES_STRCT *)&(lp->ltvRecord.u.u32);
	memcpy(tallies, pTallies, sizeof(*tallies));
    	DBG_TRACE( DbgInfo, "Get tallies okay, dixe: %d\n", sizeof(*tallies) );
    } else {
    	DBG_TRACE( DbgInfo, "Get tallies failed\n" );
	ret = -EFAULT;
    }

    return ret;
}

