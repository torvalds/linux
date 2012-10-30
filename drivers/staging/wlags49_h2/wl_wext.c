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

#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <asm/uaccess.h>

#include <debug.h>
#include <hcf.h>
#include <hcfdef.h>

#include <wl_if.h>
#include <wl_internal.h>
#include <wl_util.h>
#include <wl_main.h>
#include <wl_wext.h>
#include <wl_priv.h>

/*******************************************************************************
 * global definitions
 ******************************************************************************/
#if DBG
extern dbg_info_t *DbgInfo;
#endif  // DBG


/* Set up the LTV to program the appropriate key */
static int hermes_set_tkip_keys(ltv_t *ltv, u16 key_idx, u8 *addr,
				int set_tx, u8 *seq, u8 *key, size_t key_len)
{
	int ret = -EINVAL;
	int buf_idx = 0;
	hcf_8 tsc[IW_ENCODE_SEQ_MAX_SIZE] =
		{ 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00 };

	DBG_ENTER(DbgInfo);

	/*
	 * Check the key index here; if 0, load as Pairwise Key, otherwise,
	 * load as a group key. Note that for the Hermes, the RIDs for
	 * group/pairwise keys are different from each other and different
	 * than the default WEP keys as well.
	 */
	switch (key_idx) {
	case 0:
		ltv->len = 28;
		ltv->typ = CFG_ADD_TKIP_MAPPED_KEY;

		/* Load the BSSID */
		memcpy(&ltv->u.u8[buf_idx], addr, ETH_ALEN);
		buf_idx += ETH_ALEN;

		/* Load the TKIP key */
		memcpy(&ltv->u.u8[buf_idx], &key[0], 16);
		buf_idx += 16;

		/* Load the TSC */
		memcpy(&ltv->u.u8[buf_idx], tsc, IW_ENCODE_SEQ_MAX_SIZE);
		buf_idx += IW_ENCODE_SEQ_MAX_SIZE;

		/* Load the RSC */
		memcpy(&ltv->u.u8[buf_idx], seq, IW_ENCODE_SEQ_MAX_SIZE);
		buf_idx += IW_ENCODE_SEQ_MAX_SIZE;

		/* Load the TxMIC key */
		memcpy(&ltv->u.u8[buf_idx], &key[16], 8);
		buf_idx += 8;

		/* Load the RxMIC key */
		memcpy(&ltv->u.u8[buf_idx], &key[24], 8);

		ret = 0;
		break;
	case 1:
	case 2:
	case 3:
		ltv->len = 26;
		ltv->typ = CFG_ADD_TKIP_DEFAULT_KEY;

		/* Load the key Index */

		/* If this is a Tx Key, set bit 8000 */
		if (set_tx)
			key_idx |= 0x8000;
		ltv->u.u16[buf_idx] = cpu_to_le16(key_idx);
		buf_idx += 2;

		/* Load the RSC */
		memcpy(&ltv->u.u8[buf_idx], seq, IW_ENCODE_SEQ_MAX_SIZE);
		buf_idx += IW_ENCODE_SEQ_MAX_SIZE;

		/* Load the TKIP, TxMIC, and RxMIC keys in one shot, because in
		   CFG_ADD_TKIP_DEFAULT_KEY they are back-to-back */
		memcpy(&ltv->u.u8[buf_idx], key, key_len);
		buf_idx += key_len;

		/* Load the TSC */
		memcpy(&ltv->u.u8[buf_idx], tsc, IW_ENCODE_SEQ_MAX_SIZE);

		ret = 0;
		break;
	default:
		break;
	}

	DBG_LEAVE(DbgInfo);
	return ret;
}

/* Set up the LTV to clear the appropriate key */
static int hermes_clear_tkip_keys(ltv_t *ltv, u16 key_idx, u8 *addr)
{
	int ret;

	switch (key_idx) {
	case 0:
		if (!is_broadcast_ether_addr(addr)) {
			ltv->len = 7;
			ltv->typ = CFG_REMOVE_TKIP_MAPPED_KEY;
			memcpy(&ltv->u.u8[0], addr, ETH_ALEN);
			ret = 0;
		}
		break;
	case 1:
	case 2:
	case 3:
		/* Clear the Group TKIP keys by index */
		ltv->len = 2;
		ltv->typ = CFG_REMOVE_TKIP_DEFAULT_KEY;
		ltv->u.u16[0] = cpu_to_le16(key_idx);

		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

/* Set the WEP keys in the wl_private structure */
static int hermes_set_wep_keys(struct wl_private *lp, u16 key_idx,
			       u8 *key, size_t key_len,
			       bool enable, bool set_tx)
{
	hcf_8  encryption_state = lp->EnableEncryption;
	int tk = lp->TransmitKeyID - 1;	/* current key */
	int ret = 0;

	/* Is encryption supported? */
	if (!wl_has_wep(&(lp->hcfCtx))) {
		DBG_WARNING(DbgInfo, "WEP not supported on this device\n");
		ret = -EOPNOTSUPP;
		goto out;
	}

	DBG_NOTICE(DbgInfo, "pointer: %p, length: %d\n",
		   key, key_len);

	/* Check the size of the key */
	switch (key_len) {
	case MIN_KEY_SIZE:
	case MAX_KEY_SIZE:

		/* Check the index */
		if ((key_idx < 0) || (key_idx >= MAX_KEYS))
			key_idx = tk;

		/* Cleanup */
		memset(lp->DefaultKeys.key[key_idx].key, 0, MAX_KEY_SIZE);

		/* Copy the key in the driver */
		memcpy(lp->DefaultKeys.key[key_idx].key, key, key_len);

		/* Set the length */
		lp->DefaultKeys.key[key_idx].len = key_len;

		DBG_NOTICE(DbgInfo, "encoding.length: %d\n", key_len);
		DBG_NOTICE(DbgInfo, "set key: %s(%d) [%d]\n",
			   lp->DefaultKeys.key[key_idx].key,
			   lp->DefaultKeys.key[key_idx].len, key_idx);

		/* Enable WEP (if possible) */
		if ((key_idx == tk) && (lp->DefaultKeys.key[tk].len > 0))
			lp->EnableEncryption = 1;

		break;

	case 0:
		/* Do we want to just set the current transmit key? */
		if (set_tx && (key_idx >= 0) && (key_idx < MAX_KEYS)) {
			DBG_NOTICE(DbgInfo, "index: %d; len: %d\n", key_idx,
				   lp->DefaultKeys.key[key_idx].len);

			if (lp->DefaultKeys.key[key_idx].len > 0) {
				lp->TransmitKeyID    = key_idx + 1;
				lp->EnableEncryption = 1;
			} else {
				DBG_WARNING(DbgInfo, "Problem setting the current TxKey\n");
				ret = -EINVAL;
			}
		}
		break;

	default:
		DBG_WARNING(DbgInfo, "Invalid Key length\n");
		ret = -EINVAL;
		goto out;
	}

	/* Read the flags */
	if (enable) {
		lp->EnableEncryption = 1;
		lp->wext_enc = IW_ENCODE_ALG_WEP;
	} else {
		lp->EnableEncryption = 0;	/* disable encryption */
		lp->wext_enc = IW_ENCODE_ALG_NONE;
	}

	DBG_TRACE(DbgInfo, "encryption_state :     %d\n", encryption_state);
	DBG_TRACE(DbgInfo, "lp->EnableEncryption : %d\n", lp->EnableEncryption);
	DBG_TRACE(DbgInfo, "erq->length          : %d\n", key_len);

	/* Write the changes to the card */
	if (ret == 0) {
		DBG_NOTICE(DbgInfo, "encrypt: %d, ID: %d\n", lp->EnableEncryption,
			   lp->TransmitKeyID);

		if (lp->EnableEncryption == encryption_state) {
			if (key_len != 0) {
				/* Dynamic WEP key update */
				wl_set_wep_keys(lp);
			}
		} else {
			/* To switch encryption on/off, soft reset is
			 * required */
			wl_apply(lp);
		}
	}

out:
	return ret;
}

/*******************************************************************************
 *	wireless_commit()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Commit
 *  protocol used.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
static int wireless_commit(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *rqu, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
	/*------------------------------------------------------------------------*/

	DBG_FUNC( "wireless_commit" );
	DBG_ENTER(DbgInfo);

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	wl_apply(lp);

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_commit
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_protocol()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Returns a vendor-defined string that should identify the wireless
 *  protocol used.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
static int wireless_get_protocol(struct net_device *dev, struct iw_request_info *info, char *name, char *extra)
{
	DBG_FUNC( "wireless_get_protocol" );
	DBG_ENTER( DbgInfo );

	/* Originally, the driver was placing the string "Wireless" here. However,
	   the wireless extensions (/linux/wireless.h) indicate this string should
	   describe the wireless protocol. */

	strcpy(name, "IEEE 802.11b");

	DBG_LEAVE(DbgInfo);
	return 0;
} // wireless_get_protocol
/*============================================================================*/




/*******************************************************************************
 *	wireless_set_frequency()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Sets the frequency (channel) on which the card should Tx/Rx.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_frequency(struct net_device *dev, struct iw_request_info *info, struct iw_freq *freq, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int channel = 0;
	int ret     = 0;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_set_frequency" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	if( !capable( CAP_NET_ADMIN )) {
		ret = -EPERM;
		DBG_LEAVE( DbgInfo );
		return ret;
	}


	/* If frequency specified, look up channel */
	if( freq->e == 1 ) {
		int f = freq->m / 100000;
		channel = wl_get_chan_from_freq( f );
	}


	/* Channel specified */
	if( freq->e == 0 ) {
		channel = freq->m;
	}


	/* If the channel is an 802.11a channel, set Bit 8 */
	if( channel > 14 ) {
		channel = channel | 0x100;
	}


	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	lp->Channel = channel;


	/* Commit the adapter parameters */
	wl_apply( lp );

	/* Send an event that channel/freq has been set */
	wl_wext_event_freq( lp->dev );

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_set_frequency
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_frequency()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Gets the frequency (channel) on which the card is Tx/Rx.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
static int wireless_get_frequency(struct net_device *dev, struct iw_request_info *info, struct iw_freq *freq, char *extra)

{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = -1;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_frequency" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	lp->ltvRecord.len = 2;
	lp->ltvRecord.typ = CFG_CUR_CHANNEL;

	ret = hcf_get_info( &(lp->hcfCtx), (LTVP)&( lp->ltvRecord ));
	if( ret == HCF_SUCCESS ) {
		hcf_16 channel = CNV_LITTLE_TO_INT( lp->ltvRecord.u.u16[0] );

		freq->m = wl_get_freq_from_chan( channel ) * 100000;
		freq->e = 1;
	}

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

	ret = (ret == HCF_SUCCESS ? 0 : -EFAULT);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_frequency
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_range()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to provide misc info and statistics about the
 *  wireless device.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_range(struct net_device *dev, struct iw_request_info *info, struct iw_point *data, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long      flags;
	struct iw_range   *range = (struct iw_range *) extra;
	int                ret = 0;
	int                status = -1;
	int                count;
	__u16             *pTxRate;
	int                retries = 0;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_range" );
	DBG_ENTER( DbgInfo );

	/* Set range information */
	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* Set range information */
	memset( range, 0, sizeof( struct iw_range ));

retry:
	/* Get the current transmit rate from the adapter */
	lp->ltvRecord.len = 1 + (sizeof(*pTxRate) / sizeof(hcf_16));
	lp->ltvRecord.typ = CFG_CUR_TX_RATE;

	status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
	if( status != HCF_SUCCESS ) {
		/* Recovery action: reset and retry up to 10 times */
		DBG_TRACE( DbgInfo, "Get CFG_CUR_TX_RATE failed: 0x%x\n", status );

		if (retries < 10) {
			retries++;

			/* Holding the lock too long, makes a gap to allow other processes */
			wl_unlock(lp, &flags);
			wl_lock( lp, &flags );

			status = wl_reset( dev );
			if ( status != HCF_SUCCESS ) {
				DBG_TRACE( DbgInfo, "reset failed: 0x%x\n", status );

				ret = -EFAULT;
				goto out_unlock;
			}

			/* Holding the lock too long, makes a gap to allow other processes */
			wl_unlock(lp, &flags);
			wl_lock( lp, &flags );

			goto retry;

		} else {
			DBG_TRACE( DbgInfo, "Get CFG_CUR_TX_RATE failed: %d retries\n", retries );
			ret = -EFAULT;
			goto out_unlock;
		}
	}

	/* Holding the lock too long, makes a gap to allow other processes */
	wl_unlock(lp, &flags);
	wl_lock( lp, &flags );

	pTxRate = (__u16 *)&( lp->ltvRecord.u.u32 );

	range->throughput = CNV_LITTLE_TO_INT( *pTxRate ) * MEGABIT;

	if (retries > 0) {
		DBG_TRACE( DbgInfo, "Get CFG_CUR_TX_RATE succes: %d retries\n", retries );
	}

	// NWID - NOT SUPPORTED


	/* Channel/Frequency Info */
	range->num_channels = RADIO_CHANNELS;


	/* Signal Level Thresholds */
	range->sensitivity = RADIO_SENSITIVITY_LEVELS;


	/* Link quality */
	range->max_qual.qual     = (u_char)HCF_MAX_COMM_QUALITY;

	/* If the value returned in /proc/net/wireless is greater than the maximum range,
	   iwconfig assumes that the value is in dBm. Because an unsigned char is used,
	   it requires a bit of contorsion... */

	range->max_qual.level   = (u_char)( dbm( HCF_MIN_SIGNAL_LEVEL ) - 1 );
	range->max_qual.noise   = (u_char)( dbm( HCF_MIN_NOISE_LEVEL ) - 1 );


	/* Set available rates */
	range->num_bitrates = 0;

	lp->ltvRecord.len = 6;
	lp->ltvRecord.typ = CFG_SUPPORTED_DATA_RATES;

	status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
	if( status == HCF_SUCCESS ) {
		for( count = 0; count < MAX_RATES; count++ )
			if( lp->ltvRecord.u.u8[count+2] != 0 ) {
				range->bitrate[count] = lp->ltvRecord.u.u8[count+2] * MEGABIT / 2;
				range->num_bitrates++;
			}
	} else {
		DBG_TRACE( DbgInfo, "CFG_SUPPORTED_DATA_RATES: 0x%x\n", status );
		ret = -EFAULT;
		goto out_unlock;
	}

	/* RTS Threshold info */
	range->min_rts   = MIN_RTS_BYTES;
	range->max_rts   = MAX_RTS_BYTES;

	// Frag Threshold info - NOT SUPPORTED

	// Power Management info - NOT SUPPORTED

	/* Encryption */

	/* Holding the lock too long, makes a gap to allow other processes */
	wl_unlock(lp, &flags);
	wl_lock( lp, &flags );

	/* Is WEP supported? */

	if( wl_has_wep( &( lp->hcfCtx ))) {
		/* WEP: RC4 40 bits */
		range->encoding_size[0]      = MIN_KEY_SIZE;

		/* RC4 ~128 bits */
		range->encoding_size[1]      = MAX_KEY_SIZE;
		range->num_encoding_sizes    = 2;
		range->max_encoding_tokens   = MAX_KEYS;
	}

	/* Tx Power Info */
	range->txpower_capa  = IW_TXPOW_MWATT;
	range->num_txpower   = 1;
	range->txpower[0]    = RADIO_TX_POWER_MWATT;

	/* Wireless Extension Info */
	range->we_version_compiled   = WIRELESS_EXT;
	range->we_version_source     = WIRELESS_SUPPORT;

	// Retry Limits and Lifetime - NOT SUPPORTED

	/* Holding the lock too long, makes a gap to allow other processes */
	wl_unlock(lp, &flags);
	wl_lock( lp, &flags );

	DBG_TRACE( DbgInfo, "calling wl_wireless_stats\n" );
	wl_wireless_stats( lp->dev );
	range->avg_qual = lp->wstats.qual;
	DBG_TRACE( DbgInfo, "wl_wireless_stats done\n" );

	/* Event capability (kernel + driver) */
	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVREGISTERED);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVEXPIRED);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVMICHAELMICFAILURE);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVASSOCREQIE);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVASSOCRESPIE);

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_CIPHER_TKIP;
	range->scan_capa = IW_SCAN_CAPA_NONE;

out_unlock:
    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

	DBG_LEAVE(DbgInfo);
	return ret;
} // wireless_get_range
/*============================================================================*/


/*******************************************************************************
 *	wireless_get_bssid()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Gets the BSSID the wireless device is currently associated with.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_bssid(struct net_device *dev, struct iw_request_info *info, struct sockaddr *ap_addr, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
#if 1 //;? (HCF_TYPE) & HCF_TYPE_STA
	int status = -1;
#endif /* (HCF_TYPE) & HCF_TYPE_STA */
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_bssid" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	memset( &ap_addr->sa_data, 0, ETH_ALEN );

	ap_addr->sa_family = ARPHRD_ETHER;

	/* Assume AP mode here, which means the BSSID is our own MAC address. In
	   STA mode, this address will be overwritten with the actual BSSID using
	   the code below. */
	memcpy(&ap_addr->sa_data, lp->dev->dev_addr, ETH_ALEN);


#if 1 //;? (HCF_TYPE) & HCF_TYPE_STA
					//;?should we return an error status in AP mode

	if ( CNV_INT_TO_LITTLE( lp->hcfCtx.IFB_FWIdentity.comp_id ) == COMP_ID_FW_STA  ) {
		/* Get Current BSSID */
		lp->ltvRecord.typ = CFG_CUR_BSSID;
		lp->ltvRecord.len = 4;
		status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

		if( status == HCF_SUCCESS ) {
			/* Copy info into sockaddr struct */
			memcpy(&ap_addr->sa_data, lp->ltvRecord.u.u8, ETH_ALEN);
		} else {
			ret = -EFAULT;
		}
	}

#endif // (HCF_TYPE) & HCF_TYPE_STA

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE(DbgInfo);
	return ret;
} // wireless_get_bssid
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_ap_list()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Gets the results of a network scan.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 *  NOTE: SIOCGIWAPLIST has been deprecated by SIOCSIWSCAN. This function
 *       implements SIOCGIWAPLIST only to provide backwards compatibility. For
 *       all systems using WIRELESS_EXT v14 and higher, use SIOCSIWSCAN!
 *
 ******************************************************************************/
static int wireless_get_ap_list (struct net_device *dev, struct iw_request_info *info, struct iw_point *data, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long	  flags;
	int                 ret;
	int                 num_aps = -1;
	int                 sec_count = 0;
	hcf_32              count;
	struct sockaddr     *hwa = NULL;
	struct iw_quality   *qual = NULL;
#ifdef WARP
	ScanResult			*p = &lp->scan_results;
#else
	ProbeResult         *p = &lp->probe_results;
#endif  // WARP
	/*------------------------------------------------------------------------*/

	DBG_FUNC( "wireless_get_ap_list" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* Set the completion state to FALSE */
	lp->scan_results.scan_complete = FALSE;
	lp->probe_results.scan_complete = FALSE;
	/* Channels to scan */
	lp->ltvRecord.len       = 2;
	lp->ltvRecord.typ       = CFG_SCAN_CHANNELS_2GHZ;
	lp->ltvRecord.u.u16[0]  = CNV_INT_TO_LITTLE( 0x7FFF );
	ret = hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
	DBG_TRACE( DbgInfo, "CFG_SCAN_CHANNELS_2GHZ result: 0x%x\n", ret );

	/* Set the SCAN_SSID to "ANY". Using this RID for scan prevents the need to
	   disassociate from the network we are currently on */
	lp->ltvRecord.len       = 2;
	lp->ltvRecord.typ       = CFG_SCAN_SSID;
	lp->ltvRecord.u.u16[0]  = CNV_INT_TO_LITTLE( 0 );
	ret = hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
	DBG_TRACE( DbgInfo, "CFG_SCAN_SSID to 'any' ret: 0x%x\n", ret );

	/* Initiate the scan */
#ifdef WARP
	ret = hcf_action( &( lp->hcfCtx ), MDD_ACT_SCAN );
#else
	ret = hcf_action( &( lp->hcfCtx ), HCF_ACT_ACS_SCAN );
#endif  // WARP

    	wl_act_int_on( lp );

	//;? unlock? what about the access to lp below? is it broken?
	wl_unlock(lp, &flags);

	if( ret == HCF_SUCCESS ) {
		DBG_TRACE( DbgInfo, "SUCCESSFULLY INITIATED SCAN...\n" );
		while( (*p).scan_complete == FALSE && ret == HCF_SUCCESS ) {
			DBG_TRACE( DbgInfo, "Waiting for scan results...\n" );
			/* Abort the scan if we've waited for more than MAX_SCAN_TIME_SEC */
			if( sec_count++ > MAX_SCAN_TIME_SEC ) {
				ret = -EIO;
			} else {
				/* Wait for 1 sec in 10ms intervals, scheduling the kernel to do
				   other things in the meantime, This prevents system lockups by
				   giving some time back to the kernel */
				for( count = 0; count < 100; count ++ ) {
					mdelay( 10 );
					schedule( );
				}
			}
		}

		rmb();

		if ( ret != HCF_SUCCESS ) {
			DBG_ERROR( DbgInfo, "timeout waiting for scan results\n" );
		} else {
			num_aps             = (*p)/*lp->probe_results*/.num_aps;
			if (num_aps > IW_MAX_AP) {
				num_aps = IW_MAX_AP;
			}
			data->length = num_aps;
			hwa = (struct sockaddr *)extra;
			qual = (struct iw_quality *) extra +
					( sizeof( struct sockaddr ) * num_aps );

			/* This flag is used to tell the user if we provide quality
			   information. Since we provide signal/noise levels but no
			   quality info on a scan, this is set to 0. Setting to 1 and
			   providing a quality of 0 produces weird results. If we ever
			   provide quality (or can calculate it), this can be changed */
			data->flags = 0;

			for( count = 0; count < num_aps; count++ ) {
#ifdef WARP
				memcpy( hwa[count].sa_data,
						(*p)/*lp->scan_results*/.APTable[count].bssid, ETH_ALEN );
#else  //;?why use BSSID and bssid as names in seemingly very comparable situations
				DBG_PRINT("BSSID: %pM\n",
						(*p).ProbeTable[count].BSSID);
				memcpy( hwa[count].sa_data,
						(*p)/*lp->probe_results*/.ProbeTable[count].BSSID, ETH_ALEN );
#endif // WARP
			}
			/* Once the data is copied to the wireless struct, invalidate the
			   scan result to initiate a rescan on the next request */
			(*p)/*lp->probe_results*/.scan_complete = FALSE;
			/* Send the wireless event that the scan has completed, just in case
			   it's needed */
			wl_wext_event_scan_complete( lp->dev );
		}
	}
out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_ap_list
/*============================================================================*/




/*******************************************************************************
 *	wireless_set_sensitivity()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Sets the sensitivity (distance between APs) of the wireless card.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_sensitivity(struct net_device *dev, struct iw_request_info *info, struct iw_param *sens, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
	int dens = sens->value;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_set_sensitivity" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	if(( dens < 1 ) || ( dens > 3 )) {
		ret = -EINVAL;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	lp->DistanceBetweenAPs = dens;
	wl_apply( lp );

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_set_sensitivity
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_sensitivity()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Gets the sensitivity (distance between APs) of the wireless card.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_sensitivity(struct net_device *dev, struct iw_request_info *info, struct iw_param *sens, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	int ret = 0;
	/*------------------------------------------------------------------------*/
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_sensitivity" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	/* not worth locking ... */
	sens->value = lp->DistanceBetweenAPs;
	sens->fixed = 0;	/* auto */
out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_sensitivity
/*============================================================================*/




/*******************************************************************************
 *	wireless_set_essid()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Sets the ESSID (network name) that the wireless device should associate
 *  with.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_essid(struct net_device *dev, struct iw_request_info *info, struct iw_point *data, char *ssid)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;

	DBG_FUNC( "wireless_set_essid" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	if (data->flags != 0 && data->length > HCF_MAX_NAME_LEN + 1) {
		ret = -EINVAL;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	memset( lp->NetworkName, 0, sizeof( lp->NetworkName ));

	/* data->flags is zero to ask for "any" */
	if( data->flags == 0 ) {
		/* Need this because in STAP build PARM_DEFAULT_SSID is "LinuxAP"
		 * ;?but there ain't no STAP anymore*/
		if ( CNV_INT_TO_LITTLE( lp->hcfCtx.IFB_FWIdentity.comp_id ) == COMP_ID_FW_STA  ) {
			strcpy( lp->NetworkName, "ANY" );
		} else {
			//strcpy( lp->NetworkName, "ANY" );
			strcpy( lp->NetworkName, PARM_DEFAULT_SSID );
		}
	} else {
		memcpy( lp->NetworkName, ssid, data->length );
	}

	DBG_NOTICE( DbgInfo, "set NetworkName: %s\n", ssid );

	/* Commit the adapter parameters */
	wl_apply( lp );

	/* Send an event that ESSID has been set */
	wl_wext_event_essid( lp->dev );

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_set_essid
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_essid()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Gets the ESSID (network name) that the wireless device is associated
 *  with.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_essid(struct net_device *dev, struct iw_request_info *info, struct iw_point *data, char *essid)

{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int         ret = 0;
	int         status = -1;
	wvName_t    *pName;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_essid" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* Get the desired network name */
	lp->ltvRecord.len = 1 + ( sizeof( *pName ) / sizeof( hcf_16 ));


#if 1 //;? (HCF_TYPE) & HCF_TYPE_STA
					//;?should we return an error status in AP mode

	lp->ltvRecord.typ = CFG_DESIRED_SSID;

#endif


#if 1 //;? (HCF_TYPE) & HCF_TYPE_AP
		//;?should we restore this to allow smaller memory footprint

	if ( CNV_INT_TO_LITTLE( lp->hcfCtx.IFB_FWIdentity.comp_id ) == COMP_ID_FW_AP  ) {
		lp->ltvRecord.typ = CFG_CNF_OWN_SSID;
	}

#endif // HCF_AP


	status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
	if( status == HCF_SUCCESS ) {
		pName = (wvName_t *)&( lp->ltvRecord.u.u32 );

		/* Endian translate the string length */
		pName->length = CNV_LITTLE_TO_INT( pName->length );

		/* Copy the information into the user buffer */
		data->length = pName->length;

		if( pName->length < HCF_MAX_NAME_LEN ) {
			pName->name[pName->length] = '\0';
		}

		data->flags = 1;


#if 1 //;? (HCF_TYPE) & HCF_TYPE_STA
					//;?should we return an error status in AP mode

		/* if desired is null ("any"), return current or "any" */
		if( pName->name[0] == '\0' ) {
			/* Get the current network name */
			lp->ltvRecord.len = 1 + ( sizeof(*pName ) / sizeof( hcf_16 ));
			lp->ltvRecord.typ = CFG_CUR_SSID;

			status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

			if( status == HCF_SUCCESS ) {
				pName = (wvName_t *)&( lp->ltvRecord.u.u32 );

				/* Endian translate the string length */
				pName->length = CNV_LITTLE_TO_INT( pName->length );

				/* Copy the information into the user buffer */
				data->length = pName->length;
				data->flags = 1;
			} else {
				ret = -EFAULT;
				goto out_unlock;
			}
		}

#endif // HCF_STA

		if (pName->length > IW_ESSID_MAX_SIZE) {
			ret = -EFAULT;
			goto out_unlock;
		}

		memcpy(essid, pName->name, pName->length);
	} else {
		ret = -EFAULT;
		goto out_unlock;
	}

out_unlock:
    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_essid
/*============================================================================*/




/*******************************************************************************
 *	wireless_set_encode()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Sets the encryption keys and status (enable or disable).
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_encode(struct net_device *dev, struct iw_request_info *info, struct iw_point *erq, char *keybuf)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int key_idx = (erq->flags & IW_ENCODE_INDEX) - 1;
	int ret = 0;
	bool enable = true;

	DBG_ENTER(DbgInfo);

	if (lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	if (erq->flags & IW_ENCODE_DISABLED)
		enable = false;

	wl_lock(lp, &flags);

	wl_act_int_off(lp);

	ret = hermes_set_wep_keys(lp, key_idx, keybuf, erq->length,
				  enable, true);

	/* Send an event that Encryption has been set */
	if (ret == 0)
		wl_wext_event_encode(dev);

	wl_act_int_on(lp);

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE(DbgInfo);
	return ret;
}

/*******************************************************************************
 *	wireless_get_encode()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Gets the encryption keys and status.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_encode(struct net_device *dev, struct iw_request_info *info, struct iw_point *erq, char *key)

{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
	int index;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_encode" );
	DBG_ENTER( DbgInfo );
	DBG_NOTICE(DbgInfo, "GIWENCODE: encrypt: %d, ID: %d\n", lp->EnableEncryption, lp->TransmitKeyID);

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	/* Only super-user can see WEP key */
	if( !capable( CAP_NET_ADMIN )) {
		ret = -EPERM;
		DBG_LEAVE( DbgInfo );
		return ret;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* Is it supported? */
	if( !wl_has_wep( &( lp->hcfCtx ))) {
		ret = -EOPNOTSUPP;
		goto out_unlock;
	}

	/* Basic checking */
	index = (erq->flags & IW_ENCODE_INDEX ) - 1;


	/* Set the flags */
	erq->flags = 0;

	if( lp->EnableEncryption == 0 ) {
		erq->flags |= IW_ENCODE_DISABLED;
	}

	/* Which key do we want */
	if(( index < 0 ) || ( index >= MAX_KEYS )) {
		index = lp->TransmitKeyID - 1;
	}

	erq->flags |= index + 1;

	/* Copy the key to the user buffer */
	erq->length = lp->DefaultKeys.key[index].len;

	memcpy(key, lp->DefaultKeys.key[index].key, erq->length);

out_unlock:

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_encode
/*============================================================================*/




/*******************************************************************************
 *	wireless_set_nickname()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Sets the nickname, or station name, of the wireless device.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_nickname(struct net_device *dev, struct iw_request_info *info, struct iw_point *data, char *nickname)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_set_nickname" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

#if 0 //;? Needed, was present in original code but not in 7.18 Linux 2.6 kernel version
	if( !capable(CAP_NET_ADMIN )) {
		ret = -EPERM;
		DBG_LEAVE( DbgInfo );
		return ret;
	}
#endif

	/* Validate the new value */
	if(data->length > HCF_MAX_NAME_LEN) {
		ret = -EINVAL;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	memset( lp->StationName, 0, sizeof( lp->StationName ));

	memcpy( lp->StationName, nickname, data->length );

	/* Commit the adapter parameters */
	wl_apply( lp );

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_set_nickname
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_nickname()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Gets the nickname, or station name, of the wireless device.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_nickname(struct net_device *dev, struct iw_request_info *info, struct iw_point *data, char *nickname)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int         ret = 0;
	int         status = -1;
	wvName_t    *pName;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_nickname" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* Get the current station name */
	lp->ltvRecord.len = 1 + ( sizeof( *pName ) / sizeof( hcf_16 ));
	lp->ltvRecord.typ = CFG_CNF_OWN_NAME;

	status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

	if( status == HCF_SUCCESS ) {
		pName = (wvName_t *)&( lp->ltvRecord.u.u32 );

		/* Endian translate the length */
		pName->length = CNV_LITTLE_TO_INT( pName->length );

		if ( pName->length > IW_ESSID_MAX_SIZE ) {
			ret = -EFAULT;
		} else {
			/* Copy the information into the user buffer */
			data->length = pName->length;
			memcpy(nickname, pName->name, pName->length);
		}
	} else {
		ret = -EFAULT;
	}

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE(DbgInfo);
	return ret;
} // wireless_get_nickname
/*============================================================================*/




/*******************************************************************************
 *	wireless_set_porttype()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Sets the port type of the wireless device.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_porttype(struct net_device *dev, struct iw_request_info *info, __u32 *mode, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
	hcf_16  portType;
	hcf_16	createIBSS;
	/*------------------------------------------------------------------------*/

	DBG_FUNC( "wireless_set_porttype" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* Validate the new value */
	switch( *mode ) {
	case IW_MODE_ADHOC:

		/* When user requests ad-hoc, set IBSS mode! */
		portType         = 1;
		createIBSS       = 1;

		lp->DownloadFirmware = WVLAN_DRV_MODE_STA; //1;

		break;


	case IW_MODE_AUTO:
	case IW_MODE_INFRA:

		/* Both automatic and infrastructure set port to BSS/STA mode */
		portType         = 1;
		createIBSS       = 0;

		lp->DownloadFirmware = WVLAN_DRV_MODE_STA; //1;

		break;


#if 0 //;? (HCF_TYPE) & HCF_TYPE_AP

	case IW_MODE_MASTER:

		/* Set BSS/AP mode */
		portType             = 1;

		lp->CreateIBSS       = 0;
		lp->DownloadFirmware = WVLAN_DRV_MODE_AP; //2;

		break;

#endif /* (HCF_TYPE) & HCF_TYPE_AP */


	default:

		portType   = 0;
		createIBSS = 0;
		ret = -EINVAL;
	}

	if( portType != 0 ) {
		/* Only do something if there is a mode change */
		if( ( lp->PortType != portType ) || (lp->CreateIBSS != createIBSS)) {
			lp->PortType   = portType;
			lp->CreateIBSS = createIBSS;

			/* Commit the adapter parameters */
			wl_go( lp );

			/* Send an event that mode has been set */
			wl_wext_event_mode( lp->dev );
		}
	}

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_set_porttype
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_porttype()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Gets the port type of the wireless device.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_porttype(struct net_device *dev, struct iw_request_info *info, __u32 *mode, char *extra)

{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int     ret = 0;
	int     status = -1;
	hcf_16  *pPortType;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_porttype" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* Get the current port type */
	lp->ltvRecord.len = 1 + ( sizeof( *pPortType ) / sizeof( hcf_16 ));
	lp->ltvRecord.typ = CFG_CNF_PORT_TYPE;

	status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

	if( status == HCF_SUCCESS ) {
		pPortType = (hcf_16 *)&( lp->ltvRecord.u.u32 );

		*pPortType = CNV_LITTLE_TO_INT( *pPortType );

		switch( *pPortType ) {
		case 1:

#if 0
#if (HCF_TYPE) & HCF_TYPE_AP

			if ( CNV_INT_TO_LITTLE( lp->hcfCtx.IFB_FWIdentity.comp_id ) == COMP_ID_FW_AP  ) {
				*mode = IW_MODE_MASTER;
			} else {
				*mode = IW_MODE_INFRA;
			}

#else

			*mode = IW_MODE_INFRA;

#endif  /* (HCF_TYPE) & HCF_TYPE_AP */
#endif

			if ( CNV_INT_TO_LITTLE( lp->hcfCtx.IFB_FWIdentity.comp_id ) == COMP_ID_FW_AP  ) {
				*mode =  IW_MODE_MASTER;
			} else {
				if( lp->CreateIBSS ) {
					*mode = IW_MODE_ADHOC;
				} else {
					*mode = IW_MODE_INFRA;
				}
			}

			break;


		case 3:
			*mode = IW_MODE_ADHOC;
			break;

		default:
			ret = -EFAULT;
			break;
		}
	} else {
		ret = -EFAULT;
	}

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_porttype
/*============================================================================*/




/*******************************************************************************
 *	wireless_set_power()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Sets the power management settings of the wireless device.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_power(struct net_device *dev, struct iw_request_info *info, struct iw_param *wrq, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_set_power" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	DBG_PRINT( "THIS CORRUPTS PMEnabled ;?!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" );

#if 0 //;? Needed, was present in original code but not in 7.18 Linux 2.6 kernel version
	if( !capable( CAP_NET_ADMIN )) {
		ret = -EPERM;

		DBG_LEAVE( DbgInfo );
		return ret;
	}
#endif

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* Set the power management state based on the 'disabled' value */
	if( wrq->disabled ) {
		lp->PMEnabled = 0;
	} else {
		lp->PMEnabled = 1;
	}

	/* Commit the adapter parameters */
	wl_apply( lp );

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_set_power
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_power()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Gets the power management settings of the wireless device.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_power(struct net_device *dev, struct iw_request_info *info, struct iw_param *rrq, char *extra)

{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
	/*------------------------------------------------------------------------*/
	DBG_FUNC( "wireless_get_power" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	DBG_PRINT( "THIS IS PROBABLY AN OVER-SIMPLIFICATION ;?!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" );

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	rrq->flags = 0;
	rrq->value = 0;

	if( lp->PMEnabled ) {
		rrq->disabled = 0;
	} else {
		rrq->disabled = 1;
	}

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_power
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_tx_power()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Gets the transmit power of the wireless device's radio.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_tx_power(struct net_device *dev, struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
	/*------------------------------------------------------------------------*/
	DBG_FUNC( "wireless_get_tx_power" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

#ifdef USE_POWER_DBM
	rrq->value = RADIO_TX_POWER_DBM;
	rrq->flags = IW_TXPOW_DBM;
#else
	rrq->value = RADIO_TX_POWER_MWATT;
	rrq->flags = IW_TXPOW_MWATT;
#endif
	rrq->fixed = 1;
	rrq->disabled = 0;

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_tx_power
/*============================================================================*/




/*******************************************************************************
 *	wireless_set_rts_threshold()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Sets the RTS threshold for the wireless card.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_rts_threshold (struct net_device *dev, struct iw_request_info *info, struct iw_param *rts, char *extra)
{
	int ret = 0;
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int rthr = rts->value;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_set_rts_threshold" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	if(rts->fixed == 0) {
		ret = -EINVAL;
		goto out;
	}

	if( rts->disabled ) {
		rthr = 2347;
	}

	if(( rthr < 256 ) || ( rthr > 2347 )) {
		ret = -EINVAL;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	lp->RTSThreshold = rthr;

	wl_apply( lp );

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_set_rts_threshold
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_rts_threshold()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *     Gets the RTS threshold for the wireless card.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_rts_threshold (struct net_device *dev, struct iw_request_info *info, struct iw_param *rts, char *extra)
{
	int ret = 0;
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	/*------------------------------------------------------------------------*/

	DBG_FUNC( "wireless_get_rts_threshold" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	rts->value = lp->RTSThreshold;

	rts->disabled = ( rts->value == 2347 );

	rts->fixed = 1;

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_rts_threshold
/*============================================================================*/





/*******************************************************************************
 *	wireless_set_rate()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Set the default data rate setting used by the wireless device.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_rate(struct net_device *dev, struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret = 0;
#ifdef WARP
	int status = -1;
	int index = 0;
#endif  // WARP
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_set_rate" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

#ifdef WARP

	/* Determine if the card is operating in the 2.4 or 5.0 GHz band; check
	   if Bit 9 is set in the current channel RID */
	lp->ltvRecord.len = 2;
	lp->ltvRecord.typ = CFG_CUR_CHANNEL;

	status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

	if( status == HCF_SUCCESS ) {
		index = ( CNV_LITTLE_TO_INT( lp->ltvRecord.u.u16[0] ) & 0x100 ) ? 1 : 0;

		DBG_PRINT( "Index: %d\n", index );
	} else {
		DBG_ERROR( DbgInfo, "Could not determine radio frequency\n" );
		DBG_LEAVE( DbgInfo );
		ret = -EINVAL;
		goto out_unlock;
	}

	if( rrq->value > 0 &&
		rrq->value <= 1 * MEGABIT ) {
		lp->TxRateControl[index] = 0x0001;
	}
	else if( rrq->value > 1 * MEGABIT &&
			rrq->value <= 2 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0002;
		} else {
			lp->TxRateControl[index] = 0x0003;
		}
	}
	else if( rrq->value > 2 * MEGABIT &&
			rrq->value <= 5 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0004;
		} else {
			lp->TxRateControl[index] = 0x0007;
		}
	}
	else if( rrq->value > 5 * MEGABIT &&
			rrq->value <= 6 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0010;
		} else {
			lp->TxRateControl[index] = 0x0017;
		}
	}
	else if( rrq->value > 6 * MEGABIT &&
			rrq->value <= 9 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0020;
		} else {
			lp->TxRateControl[index] = 0x0037;
		}
	}
	else if( rrq->value > 9 * MEGABIT &&
			rrq->value <= 11 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0008;
		} else {
			lp->TxRateControl[index] = 0x003F;
		}
	}
	else if( rrq->value > 11 * MEGABIT &&
			rrq->value <= 12 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0040;
		} else {
			lp->TxRateControl[index] = 0x007F;
		}
	}
	else if( rrq->value > 12 * MEGABIT &&
			rrq->value <= 18 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0080;
		} else {
			lp->TxRateControl[index] = 0x00FF;
		}
	}
	else if( rrq->value > 18 * MEGABIT &&
			rrq->value <= 24 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0100;
		} else {
			lp->TxRateControl[index] = 0x01FF;
		}
	}
	else if( rrq->value > 24 * MEGABIT &&
			rrq->value <= 36 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0200;
		} else {
			lp->TxRateControl[index] = 0x03FF;
		}
	}
	else if( rrq->value > 36 * MEGABIT &&
			rrq->value <= 48 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0400;
		} else {
			lp->TxRateControl[index] = 0x07FF;
		}
	}
	else if( rrq->value > 48 * MEGABIT &&
			rrq->value <= 54 * MEGABIT ) {
		if( rrq->fixed == 1 ) {
			lp->TxRateControl[index] = 0x0800;
		} else {
			lp->TxRateControl[index] = 0x0FFF;
		}
	}
	else if( rrq->fixed == 0 ) {
		/* In this case, the user has not specified a bitrate, only the "auto"
		   moniker. So, set to all supported rates */
		lp->TxRateControl[index] = PARM_MAX_TX_RATE;
	} else {
		rrq->value = 0;
		ret = -EINVAL;
		goto out_unlock;
	}


#else

	if( rrq->value > 0 &&
			rrq->value <= 1 * MEGABIT ) {
		lp->TxRateControl[0] = 1;
	}
	else if( rrq->value > 1 * MEGABIT &&
			rrq->value <= 2 * MEGABIT ) {
		if( rrq->fixed ) {
			lp->TxRateControl[0] = 2;
		} else {
			lp->TxRateControl[0] = 6;
		}
	}
	else if( rrq->value > 2 * MEGABIT &&
			rrq->value <= 5 * MEGABIT ) {
		if( rrq->fixed ) {
			lp->TxRateControl[0] = 4;
		} else {
			lp->TxRateControl[0] = 7;
		}
	}
	else if( rrq->value > 5 * MEGABIT &&
			rrq->value <= 11 * MEGABIT ) {
		if( rrq->fixed)  {
			lp->TxRateControl[0] = 5;
		} else {
			lp->TxRateControl[0] = 3;
		}
	}
	else if( rrq->fixed == 0 ) {
		/* In this case, the user has not specified a bitrate, only the "auto"
		   moniker. So, set the rate to 11Mb auto */
		lp->TxRateControl[0] = 3;
	} else {
		rrq->value = 0;
		ret = -EINVAL;
		goto out_unlock;
	}

#endif  // WARP


	/* Commit the adapter parameters */
	wl_apply( lp );

out_unlock:

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_set_rate
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_rate()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Get the default data rate setting used by the wireless device.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_rate(struct net_device *dev, struct iw_request_info *info, struct iw_param *rrq, char *extra)

{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int     ret = 0;
	int     status = -1;
	hcf_16  txRate;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_rate" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* Get the current transmit rate from the adapter */
	lp->ltvRecord.len = 1 + ( sizeof(txRate)/sizeof(hcf_16));
	lp->ltvRecord.typ = CFG_CUR_TX_RATE;

	status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

	if( status == HCF_SUCCESS ) {
#ifdef WARP

		txRate = CNV_LITTLE_TO_INT( lp->ltvRecord.u.u16[0] );

		if( txRate & 0x0001 ) {
			txRate = 1;
		}
		else if( txRate & 0x0002 ) {
			txRate = 2;
		}
		else if( txRate & 0x0004 ) {
			txRate = 5;
		}
		else if( txRate & 0x0008 ) {
			txRate = 11;
		}
		else if( txRate & 0x00010 ) {
			txRate = 6;
		}
		else if( txRate & 0x00020 ) {
			txRate = 9;
		}
		else if( txRate & 0x00040 ) {
			txRate = 12;
		}
		else if( txRate & 0x00080 ) {
			txRate = 18;
		}
		else if( txRate & 0x00100 ) {
			txRate = 24;
		}
		else if( txRate & 0x00200 ) {
			txRate = 36;
		}
		else if( txRate & 0x00400 ) {
			txRate = 48;
		}
		else if( txRate & 0x00800 ) {
			txRate = 54;
		}

#else

		txRate = (hcf_16)CNV_LITTLE_TO_LONG( lp->ltvRecord.u.u32[0] );

#endif  // WARP

		rrq->value = txRate * MEGABIT;
	} else {
		rrq->value = 0;
		ret = -EFAULT;
	}

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_rate
/*============================================================================*/




#if 0 //;? Not used anymore
/*******************************************************************************
 *	wireless_get_private_interface()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Returns the Linux Wireless Extensions' compatible private interface of
 *  the driver.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wireless_get_private_interface( struct iwreq *wrq, struct wl_private *lp )
{
	int ret = 0;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_private_interface" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	if( wrq->u.data.pointer != NULL ) {
		struct iw_priv_args priv[] =
		{
			{ SIOCSIWNETNAME, IW_PRIV_TYPE_CHAR | HCF_MAX_NAME_LEN, 0, "snetwork_name" },
			{ SIOCGIWNETNAME, 0, IW_PRIV_TYPE_CHAR | HCF_MAX_NAME_LEN, "gnetwork_name" },
			{ SIOCSIWSTANAME, IW_PRIV_TYPE_CHAR | HCF_MAX_NAME_LEN, 0, "sstation_name" },
			{ SIOCGIWSTANAME, 0, IW_PRIV_TYPE_CHAR | HCF_MAX_NAME_LEN, "gstation_name" },
			{ SIOCSIWPORTTYPE, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, 0, "sport_type" },
			{ SIOCGIWPORTTYPE, 0, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, "gport_type" },
		};

		/* Verify the user buffer */
		ret = verify_area( VERIFY_WRITE, wrq->u.data.pointer, sizeof( priv ));

		if( ret != 0 ) {
			DBG_LEAVE( DbgInfo );
			return ret;
		}

		/* Copy the data into the user's buffer */
		wrq->u.data.length = NELEM( priv );
		copy_to_user( wrq->u.data.pointer, &priv, sizeof( priv ));
	}

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_private_interface
/*============================================================================*/
#endif



/*******************************************************************************
 *	wireless_set_scan()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Instructs the driver to initiate a network scan.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_set_scan(struct net_device *dev, struct iw_request_info *info, struct iw_point *data, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int                 ret = 0;
	int                 status = -1;
	int		    retries = 0;
	/*------------------------------------------------------------------------*/

	//;? Note: shows results as trace, returns always 0 unless BUSY

	DBG_FUNC( "wireless_set_scan" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/*
         * This looks like a nice place to test if the HCF is still
         * communicating with the card. It seems that sometimes BAP_1
         * gets corrupted. By looking at the comments in HCF the
         * cause is still a mystery. Okay, the communication to the
         * card is dead, reset the card to revive.
         */
	if((lp->hcfCtx.IFB_CardStat & CARD_STAT_DEFUNCT) != 0)
	{
		DBG_TRACE( DbgInfo, "CARD is in DEFUNCT mode, reset it to bring it back to life\n" );
		wl_reset( dev );
	}

retry:
	/* Set the completion state to FALSE */
	lp->probe_results.scan_complete = FALSE;


	/* Channels to scan */
#ifdef WARP
	lp->ltvRecord.len       = 5;
	lp->ltvRecord.typ       = CFG_SCAN_CHANNEL;
	lp->ltvRecord.u.u16[0]  = CNV_INT_TO_LITTLE( 0x3FFF );  // 2.4 GHz Band
	lp->ltvRecord.u.u16[1]  = CNV_INT_TO_LITTLE( 0xFFFF );  // 5.0 GHz Band
	lp->ltvRecord.u.u16[2]  = CNV_INT_TO_LITTLE( 0xFFFF );  //      ..
	lp->ltvRecord.u.u16[3]  = CNV_INT_TO_LITTLE( 0x0007 );  //      ..
#else
	lp->ltvRecord.len       = 2;
	lp->ltvRecord.typ       = CFG_SCAN_CHANNEL;
	lp->ltvRecord.u.u16[0]  = CNV_INT_TO_LITTLE( 0x7FFF );
#endif  // WARP

	status = hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

	DBG_TRACE( DbgInfo, "CFG_SCAN_CHANNEL result      : 0x%x\n", status );

	// Holding the lock too long, makes a gap to allow other processes
	wl_unlock(lp, &flags);
	wl_lock( lp, &flags );

	if( status != HCF_SUCCESS ) {
		//Recovery
		retries++;
		if(retries <= 10) {
			DBG_TRACE( DbgInfo, "Reset card to recover, attempt: %d\n", retries );
			wl_reset( dev );

			// Holding the lock too long, makes a gap to allow other processes
			wl_unlock(lp, &flags);
			wl_lock( lp, &flags );

			goto retry;
		}
	}

	/* Set the SCAN_SSID to "ANY". Using this RID for scan prevents the need to
	   disassociate from the network we are currently on */
	lp->ltvRecord.len       = 18;
	lp->ltvRecord.typ       = CFG_SCAN_SSID;
	lp->ltvRecord.u.u16[0]  = CNV_INT_TO_LITTLE( 0 );
	lp->ltvRecord.u.u16[1]  = CNV_INT_TO_LITTLE( 0 );

	status = hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

	// Holding the lock too long, makes a gap to allow other processes
	wl_unlock(lp, &flags);
	wl_lock( lp, &flags );

	DBG_TRACE( DbgInfo, "CFG_SCAN_SSID to 'any' status: 0x%x\n", status );

	/* Initiate the scan */
	/* NOTE: Using HCF_ACT_SCAN has been removed, as using HCF_ACT_ACS_SCAN to
	   retrieve probe response must always be used to support WPA */
	status = hcf_action( &( lp->hcfCtx ), HCF_ACT_ACS_SCAN );

	if( status == HCF_SUCCESS ) {
		DBG_TRACE( DbgInfo, "SUCCESSFULLY INITIATED SCAN...\n" );
	} else {
		DBG_TRACE( DbgInfo, "INITIATE SCAN FAILED...\n" );
	}

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE(DbgInfo);
	return ret;
} // wireless_set_scan
/*============================================================================*/




/*******************************************************************************
 *	wireless_get_scan()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Instructs the driver to gather and return the results of a network scan.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
static int wireless_get_scan(struct net_device *dev, struct iw_request_info *info, struct iw_point *data, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int                 ret = 0;
	int                 count;
	char                *buf;
	char                *buf_end;
	struct iw_event     iwe;
	PROBE_RESP          *probe_resp;
	hcf_8               msg[512];
	hcf_8               *wpa_ie;
	hcf_16              wpa_ie_len;
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wireless_get_scan" );
	DBG_ENTER( DbgInfo );

	if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	/* If the scan is not done, tell the calling process to try again later */
	if( !lp->probe_results.scan_complete ) {
		ret = -EAGAIN;
		goto out_unlock;
	}

	DBG_TRACE( DbgInfo, "SCAN COMPLETE, Num of APs: %d\n",
			   lp->probe_results.num_aps );

	buf     = extra;
	buf_end = extra + IW_SCAN_MAX_DATA;

	for( count = 0; count < lp->probe_results.num_aps; count++ ) {
		/* Reference the probe response from the table */
		probe_resp = (PROBE_RESP *)&lp->probe_results.ProbeTable[count];


		/* First entry MUST be the MAC address */
		memset( &iwe, 0, sizeof( iwe ));

		iwe.cmd                 = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy( iwe.u.ap_addr.sa_data, probe_resp->BSSID, ETH_ALEN);
		iwe.len                 = IW_EV_ADDR_LEN;

		buf = iwe_stream_add_event(info, buf, buf_end,
					   &iwe, IW_EV_ADDR_LEN);

		/* Use the mode to indicate if it's a station or AP */
		/* Won't always be an AP if in IBSS mode */
		memset( &iwe, 0, sizeof( iwe ));

		iwe.cmd = SIOCGIWMODE;

		if( probe_resp->capability & CAPABILITY_IBSS ) {
			iwe.u.mode = IW_MODE_INFRA;
		} else {
			iwe.u.mode = IW_MODE_MASTER;
		}

		iwe.len = IW_EV_UINT_LEN;

		buf = iwe_stream_add_event(info, buf, buf_end,
					   &iwe, IW_EV_UINT_LEN);

		/* Any quality information */
		memset(&iwe, 0, sizeof(iwe));

		iwe.cmd             = IWEVQUAL;
		iwe.u.qual.level    = dbm(probe_resp->signal);
		iwe.u.qual.noise    = dbm(probe_resp->silence);
		iwe.u.qual.qual     = iwe.u.qual.level - iwe.u.qual.noise;
		iwe.u.qual.updated  = lp->probe_results.scan_complete | IW_QUAL_DBM;
		iwe.len             = IW_EV_QUAL_LEN;

		buf = iwe_stream_add_event(info, buf, buf_end,
					   &iwe, IW_EV_QUAL_LEN);


		/* ESSID information */
		if( probe_resp->rawData[1] > 0 ) {
			memset( &iwe, 0, sizeof( iwe ));

			iwe.cmd = SIOCGIWESSID;
			iwe.u.data.length = probe_resp->rawData[1];
			iwe.u.data.flags = 1;

			buf = iwe_stream_add_point(info, buf, buf_end,
					       &iwe, &probe_resp->rawData[2]);
		}


		/* Encryption Information */
		memset( &iwe, 0, sizeof( iwe ));

		iwe.cmd             = SIOCGIWENCODE;
		iwe.u.data.length   = 0;

		/* Check the capabilities field of the Probe Response to see if
		   'privacy' is supported on the AP in question */
		if( probe_resp->capability & CAPABILITY_PRIVACY ) {
			iwe.u.data.flags |= IW_ENCODE_ENABLED;
		} else {
			iwe.u.data.flags |= IW_ENCODE_DISABLED;
		}

		buf = iwe_stream_add_point(info, buf, buf_end, &iwe, NULL);


		/* Frequency Info */
		memset( &iwe, 0, sizeof( iwe ));

		iwe.cmd = SIOCGIWFREQ;
		iwe.len = IW_EV_FREQ_LEN;
		iwe.u.freq.m = wl_parse_ds_ie( probe_resp );
		iwe.u.freq.e = 0;

		buf = iwe_stream_add_event(info, buf, buf_end,
					   &iwe, IW_EV_FREQ_LEN);


		/* Custom info (Beacon Interval) */
		memset( &iwe, 0, sizeof( iwe ));
		memset( msg, 0, sizeof( msg ));

		iwe.cmd = IWEVCUSTOM;
		sprintf( msg, "beacon_interval=%d", probe_resp->beaconInterval );
		iwe.u.data.length = strlen( msg );

		buf = iwe_stream_add_point(info, buf, buf_end, &iwe, msg);


		/* WPA-IE */
		wpa_ie = NULL;
		wpa_ie_len = 0;

		wpa_ie = wl_parse_wpa_ie( probe_resp, &wpa_ie_len );
		if( wpa_ie != NULL ) {
			memset(&iwe, 0, sizeof(iwe));

			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = wpa_ie_len;

			buf = iwe_stream_add_point(info, buf, buf_end,
						   &iwe, wpa_ie);
		}

		/* Add other custom info in formatted string format as needed... */
	}

	data->length = buf - extra;

out_unlock:

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_get_scan
/*============================================================================*/

#if DBG
static const char * const auth_names[] = {
	"IW_AUTH_WPA_VERSION",
	"IW_AUTH_CIPHER_PAIRWISE",
	"IW_AUTH_CIPHER_GROUP",
	"IW_AUTH_KEY_MGMT",
	"IW_AUTH_TKIP_COUNTERMEASURES",
	"IW_AUTH_DROP_UNENCRYPTED",
	"IW_AUTH_80211_AUTH_ALG",
	"IW_AUTH_WPA_ENABLED",
	"IW_AUTH_RX_UNENCRYPTED_EAPOL",
	"IW_AUTH_ROAMING_CONTROL",
	"IW_AUTH_PRIVACY_INVOKED",
	"IW_AUTH_CIPHER_GROUP_MGMT",
	"IW_AUTH_MFP",
	"Unsupported"
};
#endif

static int wireless_set_auth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *data, char *extra)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	ltv_t ltv;
	int ret;
	int iwa_idx = data->flags & IW_AUTH_INDEX;
	int iwa_val = data->value;

	DBG_FUNC( "wireless_set_auth" );
	DBG_ENTER( DbgInfo );

	if (lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

	if (iwa_idx > IW_AUTH_MFP)
		iwa_idx = IW_AUTH_MFP + 1;
	DBG_TRACE(DbgInfo, "%s\n", auth_names[iwa_idx]);
	switch (iwa_idx) {
	case IW_AUTH_WPA_VERSION:
		/* We do support WPA */
		if ((iwa_val == IW_AUTH_WPA_VERSION_WPA) ||
		    (iwa_val == IW_AUTH_WPA_VERSION_DISABLED))
			ret = 0;
		else
			ret = -EINVAL;
		break;

	case IW_AUTH_WPA_ENABLED:
		DBG_TRACE(DbgInfo, "val = %d\n", iwa_val);
		if (iwa_val)
			lp->EnableEncryption = 2;
		else
			lp->EnableEncryption = 0;

		/* Write straight to the card */
		ltv.len = 2;
		ltv.typ = CFG_CNF_ENCRYPTION;
		ltv.u.u16[0] = cpu_to_le16(lp->EnableEncryption);
		ret = hcf_put_info(&lp->hcfCtx, (LTVP)&ltv);

		break;

	case IW_AUTH_TKIP_COUNTERMEASURES:

		/* Immediately disable card */
		lp->driverEnable = !iwa_val;
		if (lp->driverEnable)
			hcf_cntl(&(lp->hcfCtx), HCF_CNTL_ENABLE | HCF_PORT_0);
		else
			hcf_cntl(&(lp->hcfCtx), HCF_CNTL_DISABLE | HCF_PORT_0);
		ret = 0;
		break;

	case IW_AUTH_MFP:
		/* Management Frame Protection not supported.
		 * Only fail if set to required.
		 */
		if (iwa_val == IW_AUTH_MFP_REQUIRED)
			ret = -EINVAL;
		else
			ret = 0;
		break;

	case IW_AUTH_KEY_MGMT:

		/* Record required management suite.
		 * Will take effect on next commit */
		if (iwa_val != 0)
			lp->AuthKeyMgmtSuite = 4;
		else
			lp->AuthKeyMgmtSuite = 0;

		ret = -EINPROGRESS;
		break;

	case IW_AUTH_80211_AUTH_ALG:

		/* Just record whether open or shared is required.
		 * Will take effect on next commit */
		ret = -EINPROGRESS;

		if (iwa_val & IW_AUTH_ALG_SHARED_KEY)
			lp->authentication = 1;
		else if (iwa_val & IW_AUTH_ALG_OPEN_SYSTEM)
			lp->authentication = 0;
		else
			ret = -EINVAL;
		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		/* Only needed for AP */
		lp->ExcludeUnencrypted = iwa_val;
		ret = -EINPROGRESS;
		break;

	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
	case IW_AUTH_ROAMING_CONTROL:
	case IW_AUTH_PRIVACY_INVOKED:
		/* Not used. May need to do something with
		 * CIPHER_PAIRWISE and CIPHER_GROUP*/
		ret = -EINPROGRESS;
		break;

	default:
		DBG_TRACE(DbgInfo, "IW_AUTH_?? (%d) unknown\n", iwa_idx);
		/* return an error */
		ret = -EOPNOTSUPP;
		break;
	}

    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE( DbgInfo );
	return ret;
} // wireless_set_auth
/*============================================================================*/


static void flush_tx(struct wl_private *lp)
{
	ltv_t ltv;
	int count;

	/*
	 * Make sure that there is no data queued up in the firmware
	 * before setting the TKIP keys. If this check is not
	 * performed, some data may be sent out with incorrect MIC
	 * and cause synchronization errors with the AP
	 */
	/* Check every 1ms for 100ms */
	for (count = 0; count < 100; count++) {
		udelay(1000);

		ltv.len = 2;
		ltv.typ = 0xFD91;  /* This RID not defined in HCF yet!!! */
		ltv.u.u16[0] = 0;

		hcf_get_info(&(lp->hcfCtx), (LTVP)&ltv);

		if (ltv.u.u16[0] == 0)
			break;
	}

	if (count >= 100)
		DBG_TRACE(DbgInfo, "Timed out waiting for TxQ flush!\n");

}

static int wireless_set_encodeext(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *erq, char *keybuf)
{
	struct wl_private *lp = wl_priv(dev);
	unsigned long flags;
	int ret;
	int key_idx = (erq->flags & IW_ENCODE_INDEX) - 1;
	ltv_t ltv;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)keybuf;
	bool enable = true;
	bool set_tx = false;

	DBG_ENTER(DbgInfo);

	if (lp->portState == WVLAN_PORT_STATE_DISABLED) {
		ret = -EBUSY;
		goto out;
	}

	if (erq->flags & IW_ENCODE_DISABLED) {
		ext->alg = IW_ENCODE_ALG_NONE;
		enable = false;
	}

	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
		set_tx = true;

	wl_lock(lp, &flags);

	wl_act_int_off(lp);

	memset(&ltv, 0, sizeof(ltv));

	switch (ext->alg) {
	case IW_ENCODE_ALG_TKIP:
		DBG_TRACE(DbgInfo, "IW_ENCODE_ALG_TKIP: key(%d)\n", key_idx);

		if (sizeof(ext->rx_seq) != 8) {
			DBG_TRACE(DbgInfo, "rx_seq size mismatch\n");
			DBG_LEAVE(DbgInfo);
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = hermes_set_tkip_keys(&ltv, key_idx, ext->addr.sa_data,
					   set_tx,
					   ext->rx_seq, ext->key, ext->key_len);

		if (ret != 0) {
			DBG_TRACE(DbgInfo, "hermes_set_tkip_keys returned != 0, key not set\n");
			goto out_unlock;
		}

		flush_tx(lp);

		lp->wext_enc = IW_ENCODE_ALG_TKIP;

		/* Write the key */
		ret = hcf_put_info(&(lp->hcfCtx), (LTVP)&ltv);
		break;

	case IW_ENCODE_ALG_WEP:
		DBG_TRACE(DbgInfo, "IW_ENCODE_ALG_WEP: key(%d)\n", key_idx);

		if (erq->flags & IW_ENCODE_RESTRICTED) {
			DBG_WARNING(DbgInfo, "IW_ENCODE_RESTRICTED invalid\n");
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = hermes_set_wep_keys(lp, key_idx, ext->key, ext->key_len,
					  enable, set_tx);

		break;

	case IW_ENCODE_ALG_CCMP:
		DBG_TRACE(DbgInfo, "IW_ENCODE_ALG_CCMP: key(%d)\n", key_idx);
		ret = -EOPNOTSUPP;
		break;

	case IW_ENCODE_ALG_NONE:
		DBG_TRACE(DbgInfo, "IW_ENCODE_ALG_NONE: key(%d)\n", key_idx);

		if (lp->wext_enc == IW_ENCODE_ALG_TKIP) {
			ret = hermes_clear_tkip_keys(&ltv, key_idx,
						     ext->addr.sa_data);
			flush_tx(lp);
			lp->wext_enc = IW_ENCODE_ALG_NONE;
			ret = hcf_put_info(&(lp->hcfCtx), (LTVP)&ltv);

		} else if (lp->wext_enc == IW_ENCODE_ALG_WEP) {
			ret = hermes_set_wep_keys(lp, key_idx,
						  ext->key, ext->key_len,
						  false, false);
		} else {
			ret = 0;
		}

		break;

	default:
		DBG_TRACE( DbgInfo, "IW_ENCODE_??: key(%d)\n", key_idx);
		ret = -EOPNOTSUPP;
		break;
	}

out_unlock:

	wl_act_int_on(lp);

	wl_unlock(lp, &flags);

out:
	DBG_LEAVE(DbgInfo);
	return ret;
}
/*============================================================================*/



static int wireless_set_genie(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *data, char *extra)

{
	int   ret = 0;

	DBG_ENTER(DbgInfo);

	/* We can't write this to the card, but apparently this
	 * operation needs to succeed */
	ret = 0;

	DBG_LEAVE(DbgInfo);
	return ret;
}
/*============================================================================*/


/*******************************************************************************
 *	wl_wireless_stats()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Return the current device wireless statistics.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
struct iw_statistics * wl_wireless_stats( struct net_device *dev )
{
	struct iw_statistics    *pStats;
	struct wl_private       *lp = wl_priv(dev);
	/*------------------------------------------------------------------------*/


	DBG_FUNC( "wl_wireless_stats" );
	DBG_ENTER(DbgInfo);
	DBG_PARAM(DbgInfo, "dev", "%s (0x%p)", dev->name, dev);

	pStats = NULL;

	/* Initialize the statistics */
	pStats			= &( lp->wstats );
	pStats->qual.updated    = 0x00;

	if( !( lp->flags & WVLAN2_UIL_BUSY ))
	{
		CFG_COMMS_QUALITY_STRCT *pQual;
		CFG_HERMES_TALLIES_STRCT tallies;
		int                         status;

		/* Update driver status */
		pStats->status = 0;

		/* Get the current link quality information */
		lp->ltvRecord.len = 1 + ( sizeof( *pQual ) / sizeof( hcf_16 ));
		lp->ltvRecord.typ = CFG_COMMS_QUALITY;
		status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

		if( status == HCF_SUCCESS ) {
			pQual = (CFG_COMMS_QUALITY_STRCT *)&( lp->ltvRecord );

			pStats->qual.qual  = (u_char) CNV_LITTLE_TO_INT( pQual->coms_qual );
			pStats->qual.level = (u_char) dbm( CNV_LITTLE_TO_INT( pQual->signal_lvl ));
			pStats->qual.noise = (u_char) dbm( CNV_LITTLE_TO_INT( pQual->noise_lvl ));

			pStats->qual.updated |= (IW_QUAL_QUAL_UPDATED  |
                                                 IW_QUAL_LEVEL_UPDATED |
                                                 IW_QUAL_NOISE_UPDATED |
                                                 IW_QUAL_DBM);
		} else {
			memset( &( pStats->qual ), 0, sizeof( pStats->qual ));
		}

		/* Get the current tallies from the adapter */
                /* Only possible when the device is open */
		if(lp->portState == WVLAN_PORT_STATE_DISABLED) {
			if( wl_get_tallies( lp, &tallies ) == 0 ) {
				/* No endian translation is needed here, as CFG_TALLIES is an
				   MSF RID; all processing is done on the host, not the card! */
				pStats->discard.nwid = 0L;
				pStats->discard.code = tallies.RxWEPUndecryptable;
				pStats->discard.misc = tallies.TxDiscards +
						       tallies.RxFCSErrors +
						       //tallies.RxDiscardsNoBuffer +
						       tallies.TxDiscardsWrongSA;
				//;? Extra taken over from Linux driver based on 7.18 version
				pStats->discard.retries = tallies.TxRetryLimitExceeded;
				pStats->discard.fragment = tallies.RxMsgInBadMsgFragments;
			} else {
				memset( &( pStats->discard ), 0, sizeof( pStats->discard ));
			}
		} else {
			memset( &( pStats->discard ), 0, sizeof( pStats->discard ));
		}
	}

	DBG_LEAVE( DbgInfo );
	return pStats;
} // wl_wireless_stats
/*============================================================================*/




/*******************************************************************************
 *	wl_get_wireless_stats()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Return the current device wireless statistics. This function calls
 *      wl_wireless_stats, but acquires spinlocks first as it can be called
 *      directly by the network layer.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
struct iw_statistics * wl_get_wireless_stats( struct net_device *dev )
{
	unsigned long           flags;
	struct wl_private       *lp = wl_priv(dev);
	struct iw_statistics    *pStats = NULL;
	/*------------------------------------------------------------------------*/

	DBG_FUNC( "wl_get_wireless_stats" );
	DBG_ENTER(DbgInfo);

	wl_lock( lp, &flags );

    	wl_act_int_off( lp );

#ifdef USE_RTS
	if( lp->useRTS == 1 ) {
		DBG_TRACE( DbgInfo, "Skipping wireless stats, in RTS mode\n" );
	} else
#endif
	{
		pStats = wl_wireless_stats( dev );
	}
    	wl_act_int_on( lp );

	wl_unlock(lp, &flags);

	DBG_LEAVE( DbgInfo );
	return pStats;
} // wl_get_wireless_stats


/*******************************************************************************
 *	wl_spy_gather()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Gather wireless spy statistics.
 *
 *  PARAMETERS:
 *
 *      wrq - the wireless request buffer
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
inline void wl_spy_gather( struct net_device *dev, u_char *mac )
{
	struct iw_quality wstats;
	int                     status;
	u_char                  stats[2];
	DESC_STRCT              desc[1];
	struct wl_private   *lp = wl_priv(dev);
	/*------------------------------------------------------------------------*/

	/* shortcut */
	if (!lp->spy_data.spy_number) {
		return;
	}

	/* Gather wireless spy statistics: for each packet, compare the source
	   address with out list, and if match, get the stats. */
	memset( stats, 0, sizeof(stats));
	memset( desc, 0, sizeof(DESC_STRCT));

	desc[0].buf_addr	= stats;
	desc[0].BUF_SIZE	= sizeof(stats);
	desc[0].next_desc_addr  = 0;		// terminate list

	status = hcf_rcv_msg( &( lp->hcfCtx ), &desc[0], 0 );

	if( status == HCF_SUCCESS ) {
		wstats.level = (u_char) dbm(stats[1]);
		wstats.noise = (u_char) dbm(stats[0]);
		wstats.qual  = wstats.level > wstats.noise ? wstats.level - wstats.noise : 0;

		wstats.updated = (IW_QUAL_QUAL_UPDATED  |
				  IW_QUAL_LEVEL_UPDATED |
				  IW_QUAL_NOISE_UPDATED |
				  IW_QUAL_DBM);

		wireless_spy_update( dev, mac, &wstats );
	}
} // wl_spy_gather
/*============================================================================*/




/*******************************************************************************
 *	wl_wext_event_freq()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event that the channel/freq
 *      configuration for a specific device has changed.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_freq( struct net_device *dev )
{
	union iwreq_data wrqu;
	struct wl_private *lp = wl_priv(dev);
	/*------------------------------------------------------------------------*/


	memset( &wrqu, 0, sizeof( wrqu ));

	wrqu.freq.m = lp->Channel;
	wrqu.freq.e = 0;

	wireless_send_event( dev, SIOCSIWFREQ, &wrqu, NULL );

	return;
} // wl_wext_event_freq
/*============================================================================*/




/*******************************************************************************
 *	wl_wext_event_mode()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event that the mode of operation
 *      for a specific device has changed.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_mode( struct net_device *dev )
{
	union iwreq_data wrqu;
	struct wl_private *lp = wl_priv(dev);
	/*------------------------------------------------------------------------*/


	memset( &wrqu, 0, sizeof( wrqu ));

	if ( CNV_INT_TO_LITTLE( lp->hcfCtx.IFB_FWIdentity.comp_id ) == COMP_ID_FW_STA  ) {
		wrqu.mode = IW_MODE_INFRA;
	} else {
		wrqu.mode = IW_MODE_MASTER;
	}

	wireless_send_event( dev, SIOCSIWMODE, &wrqu, NULL );

	return;
} // wl_wext_event_mode
/*============================================================================*/




/*******************************************************************************
 *	wl_wext_event_essid()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event that the ESSID configuration for
 *      a specific device has changed.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_essid( struct net_device *dev )
{
	union iwreq_data wrqu;
	struct wl_private *lp = wl_priv(dev);
	/*------------------------------------------------------------------------*/


	memset( &wrqu, 0, sizeof( wrqu ));

	/* Fill out the buffer. Note that the buffer doesn't actually contain the
	   ESSID, but a pointer to the contents. In addition, the 'extra' field of
	   the call to wireless_send_event() must also point to where the ESSID
	   lives */
	wrqu.essid.length  = strlen( lp->NetworkName );
	wrqu.essid.pointer = (caddr_t)lp->NetworkName;
	wrqu.essid.flags   = 1;

	wireless_send_event( dev, SIOCSIWESSID, &wrqu, lp->NetworkName );

	return;
} // wl_wext_event_essid
/*============================================================================*/




/*******************************************************************************
 *	wl_wext_event_encode()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event that the encryption configuration
 *      for a specific device has changed.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_encode( struct net_device *dev )
{
	union iwreq_data wrqu;
	struct wl_private *lp = wl_priv(dev);
	int index = 0;
	/*------------------------------------------------------------------------*/


	memset( &wrqu, 0, sizeof( wrqu ));

	if( lp->EnableEncryption == 0 ) {
		wrqu.encoding.flags = IW_ENCODE_DISABLED;
	} else {
		wrqu.encoding.flags |= lp->TransmitKeyID;

		index = lp->TransmitKeyID - 1;

		/* Only set IW_ENCODE_RESTRICTED/OPEN flag using lp->ExcludeUnencrypted
		   if we're in AP mode */
#if 1 //;? (HCF_TYPE) & HCF_TYPE_AP
		//;?should we restore this to allow smaller memory footprint

		if ( CNV_INT_TO_LITTLE( lp->hcfCtx.IFB_FWIdentity.comp_id ) == COMP_ID_FW_AP  ) {
			if( lp->ExcludeUnencrypted ) {
				wrqu.encoding.flags |= IW_ENCODE_RESTRICTED;
			} else {
				wrqu.encoding.flags |= IW_ENCODE_OPEN;
			}
		}

#endif  // HCF_TYPE_AP

		/* Only provide the key if permissions allow */
		if( capable( CAP_NET_ADMIN )) {
			wrqu.encoding.pointer = (caddr_t)lp->DefaultKeys.key[index].key;
			wrqu.encoding.length  = lp->DefaultKeys.key[index].len;
		} else {
			wrqu.encoding.flags |= IW_ENCODE_NOKEY;
		}
	}

	wireless_send_event( dev, SIOCSIWENCODE, &wrqu,
						 lp->DefaultKeys.key[index].key );

	return;
} // wl_wext_event_encode
/*============================================================================*/




/*******************************************************************************
 *	wl_wext_event_ap()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event that the device has been
 *      associated to a new AP.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_ap( struct net_device *dev )
{
	union iwreq_data wrqu;
	struct wl_private *lp = wl_priv(dev);
	int status;
	/*------------------------------------------------------------------------*/


	/* Retrieve the WPA-IEs used by the firmware and send an event. We must send
	   this event BEFORE sending the association event, as there are timing
	   issues with the hostap supplicant. The supplicant will attempt to process
	   an EAPOL-Key frame from an AP before receiving this information, which
	   is required for a proper processed frame. */
	wl_wext_event_assoc_ie( dev );

	/* Get the BSSID */
	lp->ltvRecord.typ = CFG_CUR_BSSID;
	lp->ltvRecord.len = 4;

	status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
	if( status == HCF_SUCCESS ) {
		memset( &wrqu, 0, sizeof( wrqu ));

		memcpy( wrqu.addr.sa_data, lp->ltvRecord.u.u8, ETH_ALEN );

		wrqu.addr.sa_family = ARPHRD_ETHER;

		wireless_send_event( dev, SIOCGIWAP, &wrqu, NULL );
	}

	return;
} // wl_wext_event_ap
/*============================================================================*/



/*******************************************************************************
 *	wl_wext_event_scan_complete()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event that a request for a network scan
 *      has completed.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_scan_complete( struct net_device *dev )
{
	union iwreq_data wrqu;
	/*------------------------------------------------------------------------*/


	memset( &wrqu, 0, sizeof( wrqu ));

	wrqu.addr.sa_family = ARPHRD_ETHER;
	wireless_send_event( dev, SIOCGIWSCAN, &wrqu, NULL );

	return;
} // wl_wext_event_scan_complete
/*============================================================================*/




/*******************************************************************************
 *	wl_wext_event_new_sta()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event that an AP has registered a new
 *      station.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_new_sta( struct net_device *dev )
{
	union iwreq_data wrqu;
	/*------------------------------------------------------------------------*/


	memset( &wrqu, 0, sizeof( wrqu ));

	/* Send the station's mac address here */
	memcpy( wrqu.addr.sa_data, dev->dev_addr, ETH_ALEN );
	wrqu.addr.sa_family = ARPHRD_ETHER;
	wireless_send_event( dev, IWEVREGISTERED, &wrqu, NULL );

	return;
} // wl_wext_event_new_sta
/*============================================================================*/




/*******************************************************************************
 *	wl_wext_event_expired_sta()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event that an AP has deregistered a
 *      station.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_expired_sta( struct net_device *dev )
{
	union iwreq_data wrqu;
	/*------------------------------------------------------------------------*/


	memset( &wrqu, 0, sizeof( wrqu ));

	memcpy( wrqu.addr.sa_data, dev->dev_addr, ETH_ALEN );
	wrqu.addr.sa_family = ARPHRD_ETHER;
	wireless_send_event( dev, IWEVEXPIRED, &wrqu, NULL );

	return;
} // wl_wext_event_expired_sta
/*============================================================================*/




/*******************************************************************************
 *	wl_wext_event_mic_failed()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event that MIC calculations failed.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_mic_failed( struct net_device *dev )
{
	union iwreq_data   wrqu;
	struct wl_private *lp = wl_priv(dev);
	struct iw_michaelmicfailure wxmic;
	int                key_idx;
	char              *addr1;
	char              *addr2;
	WVLAN_RX_WMP_HDR  *hdr;
	/*------------------------------------------------------------------------*/


	key_idx = lp->lookAheadBuf[HFS_STAT+1] >> 3;
	key_idx &= 0x03;

	/* Cast the lookahead buffer into a RFS format */
	hdr = (WVLAN_RX_WMP_HDR *)&lp->lookAheadBuf[HFS_STAT];

	/* Cast the addresses to byte buffers, as in the above RFS they are word
	   length */
	addr1 = (char *)hdr->address1;
	addr2 = (char *)hdr->address2;

	DBG_PRINT( "MIC FAIL - KEY USED : %d, STATUS : 0x%04x\n", key_idx,
			   hdr->status );

	memset(&wrqu, 0, sizeof(wrqu));
	memset(&wxmic, 0, sizeof(wxmic));

	wxmic.flags = key_idx & IW_MICFAILURE_KEY_ID;
	wxmic.flags |= (addr1[0] & 1) ?
		IW_MICFAILURE_GROUP : IW_MICFAILURE_PAIRWISE;
	wxmic.src_addr.sa_family = ARPHRD_ETHER;
	memcpy(wxmic.src_addr.sa_data, addr2, ETH_ALEN);

	wrqu.data.length = sizeof(wxmic);
	wireless_send_event(dev, IWEVMICHAELMICFAILURE, &wrqu, (char *)&wxmic);

	return;
} // wl_wext_event_mic_failed
/*============================================================================*/




/*******************************************************************************
 *	wl_wext_event_assoc_ie()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function is used to send an event containing the WPA-IE generated
 *      by the firmware in an association request.
 *
 *
 *  PARAMETERS:
 *
 *      dev - the network device for which this event is to be issued
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wext_event_assoc_ie( struct net_device *dev )
{
	union iwreq_data   wrqu;
	struct wl_private *lp = wl_priv(dev);
	int status;
	PROBE_RESP         data;
	hcf_16             length;
	hcf_8              *wpa_ie;
	/*------------------------------------------------------------------------*/


	memset( &wrqu, 0, sizeof( wrqu ));

	/* Retrieve the Association Request IE */
	lp->ltvRecord.len = 45;
	lp->ltvRecord.typ = CFG_CUR_ASSOC_REQ_INFO;

	status = hcf_get_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
	if( status == HCF_SUCCESS )
	{
		length = 0;
		memcpy( &data.rawData, &( lp->ltvRecord.u.u8[1] ), 88 );
		wpa_ie = wl_parse_wpa_ie( &data, &length );

		if( length != 0 )
		{
			wrqu.data.length = wpa_ie[1] + 2;
			wireless_send_event(dev, IWEVASSOCREQIE,
					    &wrqu, wpa_ie);

			/* This bit is a hack. We send the respie
			 * event at the same time */
			wireless_send_event(dev, IWEVASSOCRESPIE,
					    &wrqu, wpa_ie);
		}
	}

	return;
}  // wl_wext_event_assoc_ie
/*============================================================================*/
/* Structures to export the Wireless Handlers */

static const iw_handler wl_handler[] =
{
	IW_HANDLER(SIOCSIWCOMMIT, (iw_handler) wireless_commit),
	IW_HANDLER(SIOCGIWNAME, (iw_handler) wireless_get_protocol),
	IW_HANDLER(SIOCSIWFREQ, (iw_handler) wireless_set_frequency),
	IW_HANDLER(SIOCGIWFREQ, (iw_handler) wireless_get_frequency),
	IW_HANDLER(SIOCSIWMODE, (iw_handler) wireless_set_porttype),
	IW_HANDLER(SIOCGIWMODE, (iw_handler) wireless_get_porttype),
	IW_HANDLER(SIOCSIWSENS, (iw_handler) wireless_set_sensitivity),
	IW_HANDLER(SIOCGIWSENS, (iw_handler) wireless_get_sensitivity),
	IW_HANDLER(SIOCGIWRANGE, (iw_handler) wireless_get_range),
	IW_HANDLER(SIOCSIWSPY, iw_handler_set_spy),
	IW_HANDLER(SIOCGIWSPY, iw_handler_get_spy),
#if 1 //;? (HCF_TYPE) & HCF_TYPE_STA
	IW_HANDLER(SIOCGIWAP, (iw_handler) wireless_get_bssid),
#endif
	IW_HANDLER(SIOCGIWAPLIST, (iw_handler) wireless_get_ap_list),
	IW_HANDLER(SIOCSIWSCAN, (iw_handler) wireless_set_scan),
	IW_HANDLER(SIOCGIWSCAN, (iw_handler) wireless_get_scan),
	IW_HANDLER(SIOCSIWESSID, (iw_handler) wireless_set_essid),
	IW_HANDLER(SIOCGIWESSID, (iw_handler) wireless_get_essid),
	IW_HANDLER(SIOCSIWNICKN, (iw_handler) wireless_set_nickname),
	IW_HANDLER(SIOCGIWNICKN, (iw_handler) wireless_get_nickname),
	IW_HANDLER(SIOCSIWRATE, (iw_handler) wireless_set_rate),
	IW_HANDLER(SIOCGIWRATE, (iw_handler) wireless_get_rate),
	IW_HANDLER(SIOCSIWRTS, (iw_handler) wireless_set_rts_threshold),
	IW_HANDLER(SIOCGIWRTS, (iw_handler) wireless_get_rts_threshold),
	IW_HANDLER(SIOCGIWTXPOW, (iw_handler) wireless_get_tx_power),
	IW_HANDLER(SIOCSIWENCODE, (iw_handler) wireless_set_encode),
	IW_HANDLER(SIOCGIWENCODE, (iw_handler) wireless_get_encode),
	IW_HANDLER(SIOCSIWPOWER, (iw_handler) wireless_set_power),
	IW_HANDLER(SIOCGIWPOWER, (iw_handler) wireless_get_power),
	IW_HANDLER(SIOCSIWGENIE, (iw_handler) wireless_set_genie),
	IW_HANDLER(SIOCSIWAUTH, (iw_handler) wireless_set_auth),
	IW_HANDLER(SIOCSIWENCODEEXT, (iw_handler) wireless_set_encodeext),
};

static const iw_handler wl_private_handler[] =
{                                                       /* SIOCIWFIRSTPRIV + */
                wvlan_set_netname,                      /* 0: SIOCSIWNETNAME */
                wvlan_get_netname,                      /* 1: SIOCGIWNETNAME */
                wvlan_set_station_nickname,             /* 2: SIOCSIWSTANAME */
                wvlan_get_station_nickname,             /* 3: SIOCGIWSTANAME */
#if 1 //;? (HCF_TYPE) & HCF_TYPE_STA
                wvlan_set_porttype,                     /* 4: SIOCSIWPORTTYPE */
                wvlan_get_porttype,                     /* 5: SIOCGIWPORTTYPE */
#endif
};

struct iw_priv_args wl_priv_args[] = {
        {SIOCSIWNETNAME,    IW_PRIV_TYPE_CHAR | HCF_MAX_NAME_LEN, 0, "snetwork_name" },
        {SIOCGIWNETNAME, 0, IW_PRIV_TYPE_CHAR | HCF_MAX_NAME_LEN,    "gnetwork_name" },
        {SIOCSIWSTANAME,    IW_PRIV_TYPE_CHAR | HCF_MAX_NAME_LEN, 0, "sstation_name" },
        {SIOCGIWSTANAME, 0, IW_PRIV_TYPE_CHAR | HCF_MAX_NAME_LEN,    "gstation_name" },
#if 1 //;? #if (HCF_TYPE) & HCF_TYPE_STA
        {SIOCSIWPORTTYPE,    IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "sport_type" },
        {SIOCGIWPORTTYPE, 0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,    "gport_type" },
#endif
};

const struct iw_handler_def wl_iw_handler_def =
{
        .num_private        = sizeof(wl_private_handler) / sizeof(iw_handler),
        .private            = (iw_handler *) wl_private_handler,
        .private_args       = (struct iw_priv_args *) wl_priv_args,
        .num_private_args   = sizeof(wl_priv_args) / sizeof(struct iw_priv_args),
        .num_standard       = sizeof(wl_handler) / sizeof(iw_handler),
        .standard           = (iw_handler *) wl_handler,
        .get_wireless_stats = wl_get_wireless_stats,
};
