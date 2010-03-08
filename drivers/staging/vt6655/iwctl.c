/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: iwctl.c
 *
 * Purpose:  wireless ext & ioctl functions
 *
 * Author: Lyndon Chen
 *
 * Date: July 5, 2006
 *
 * Functions:
 *
 * Revision History:
 *
 */

#include "device.h"
#include "ioctl.h"
#include "iocmd.h"
#include "mac.h"
#include "card.h"
#include "hostap.h"
#include "power.h"
#include "rf.h"

#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
#include "iowpa.h"
#include "wpactl.h"
#endif

#include <net/iw_handler.h>
extern WORD TxRate_iwconfig;//2008-5-8 <add> by chester

/*---------------------  Static Definitions -------------------------*/

//2008-0409-07, <Add> by Einsn Liu
#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
#define SUPPORTED_WIRELESS_EXT                  18
#else
#define SUPPORTED_WIRELESS_EXT                  17
#endif

static const long frequency_list[] = {
    2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447, 2452, 2457, 2462, 2467, 2472, 2484,
    4915, 4920, 4925, 4935, 4940, 4945, 4960, 4980,
    5035, 5040, 5045, 5055, 5060, 5080, 5170, 5180, 5190, 5200, 5210, 5220, 5230, 5240,
    5260, 5280, 5300, 5320, 5500, 5520, 5540, 5560, 5580, 5600, 5620, 5640, 5660, 5680,
    5700, 5745, 5765, 5785, 5805, 5825
	};


/*---------------------  Static Classes  ----------------------------*/


//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;


/*---------------------  Static Variables  --------------------------*/
/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

struct iw_statistics *iwctl_get_wireless_stats(struct net_device *dev)
{
	PSDevice pDevice = netdev_priv(dev);
	long ldBm;

	pDevice->wstats.status = pDevice->eOPMode;
	#ifdef Calcu_LinkQual
	 #if 0
	  if(pDevice->byBBType == BB_TYPE_11B) {
	     if(pDevice->byCurrSQ > 120)
                  pDevice->scStatistic.LinkQuality = 100;
	     else
		 pDevice->scStatistic.LinkQuality = pDevice->byCurrSQ*100/120;
	    }
	  else if(pDevice->byBBType == BB_TYPE_11G) {
                if(pDevice->byCurrSQ < 20)
		   pDevice->scStatistic.LinkQuality = 100;
	       else if(pDevice->byCurrSQ >96)
		   pDevice->scStatistic.LinkQuality  = 0;
	       else
		   pDevice->scStatistic.LinkQuality = (96-pDevice->byCurrSQ)*100/76;
	   }
	   if(pDevice->bLinkPass !=TRUE)
	       pDevice->scStatistic.LinkQuality = 0;
	  #endif
	   if(pDevice->scStatistic.LinkQuality > 100)
   	       pDevice->scStatistic.LinkQuality = 100;
               pDevice->wstats.qual.qual =(BYTE) pDevice->scStatistic.LinkQuality;
	#else
	pDevice->wstats.qual.qual = pDevice->byCurrSQ;
	#endif
	RFvRSSITodBm(pDevice, (BYTE)(pDevice->uCurrRSSI), &ldBm);
	pDevice->wstats.qual.level = ldBm;
	//pDevice->wstats.qual.level = 0x100 - pDevice->uCurrRSSI;
	pDevice->wstats.qual.noise = 0;
	pDevice->wstats.qual.updated = 1;
	pDevice->wstats.discard.nwid = 0;
	pDevice->wstats.discard.code = 0;
	pDevice->wstats.discard.fragment = 0;
	pDevice->wstats.discard.retries = (U32)pDevice->scStatistic.dwTsrErr;
	pDevice->wstats.discard.misc = 0;
	pDevice->wstats.miss.beacon = 0;

	return &pDevice->wstats;
}



/*------------------------------------------------------------------*/


static int iwctl_commit(struct net_device *dev,
			      struct iw_request_info *info,
			      void *wrq,
			      char *extra)
{
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWCOMMIT \n");

	return 0;

}
/*
 * Wireless Handler : get protocol name
 */

int iwctl_giwname(struct net_device *dev,
			 struct iw_request_info *info,
			 char *wrq,
			 char *extra)
{
	strcpy(wrq, "802.11-a/b/g");
	return 0;
}

int iwctl_giwnwid(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
                   char *extra)
{
 	//wrq->value = 0x100;
	//wrq->disabled = 0;
	//wrq->fixed = 1;
	//return 0;
  return -EOPNOTSUPP;
}

/*
 * Wireless Handler : set scan
 */

int iwctl_siwscan(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_point *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
	 PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
	struct iw_scan_req  *req = (struct iw_scan_req *)extra;
	BYTE                abyScanSSID[WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1];
	PWLAN_IE_SSID       pItemSSID=NULL;
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWSCAN \n");


if(pDevice->byReAssocCount > 0) {   //reject scan when re-associating!
//send scan event to wpa_Supplicant
  union iwreq_data wrqu;
 PRINT_K("wireless_send_event--->SIOCGIWSCAN(scan done)\n");
 memset(&wrqu, 0, sizeof(wrqu));
 wireless_send_event(pDevice->dev, SIOCGIWSCAN, &wrqu, NULL);
  return 0;
}

	spin_lock_irq(&pDevice->lock);
	 BSSvClearBSSList((HANDLE)pDevice, pDevice->bLinkPass);

//mike add: active scan OR passive scan OR desire_ssid scan
 if(wrq->length == sizeof(struct iw_scan_req)) {
   if (wrq->flags & IW_SCAN_THIS_ESSID)  {                               //desire_ssid scan
       memset(abyScanSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
       pItemSSID = (PWLAN_IE_SSID)abyScanSSID;
       pItemSSID->byElementID = WLAN_EID_SSID;
       memcpy(pItemSSID->abySSID, req->essid, (int)req->essid_len);
         if (pItemSSID->abySSID[req->essid_len - 1] == '\0') {
           if(req->essid_len>0)
		pItemSSID->len = req->essid_len - 1;
         }
	else
	  pItemSSID->len = req->essid_len;
	  pMgmt->eScanType = WMAC_SCAN_PASSIVE;
         PRINT_K("SIOCSIWSCAN:[desired_ssid=%s,len=%d]\n",((PWLAN_IE_SSID)abyScanSSID)->abySSID,
		 	                                                                                ((PWLAN_IE_SSID)abyScanSSID)->len);
	bScheduleCommand((HANDLE) pDevice, WLAN_CMD_BSSID_SCAN, abyScanSSID);
	spin_unlock_irq(&pDevice->lock);

	return 0;
   }
   else if(req->scan_type == IW_SCAN_TYPE_PASSIVE) {          //passive scan
       pMgmt->eScanType = WMAC_SCAN_PASSIVE;
   }
 }
 else {           //active scan
     pMgmt->eScanType = WMAC_SCAN_ACTIVE;
 }

	 pMgmt->eScanType = WMAC_SCAN_PASSIVE;
          //printk("SIOCSIWSCAN:WLAN_CMD_BSSID_SCAN\n");
	bScheduleCommand((HANDLE) pDevice, WLAN_CMD_BSSID_SCAN, NULL);
	spin_unlock_irq(&pDevice->lock);

	return 0;
}


/*
 * Wireless Handler : get scan results
 */

int iwctl_giwscan(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_point *wrq,
             char *extra)
{
    int ii, jj, kk;
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
    PKnownBSS           pBSS;
    PWLAN_IE_SSID       pItemSSID;
    PWLAN_IE_SUPP_RATES pSuppRates, pExtSuppRates;
	char *current_ev = extra;
	char *end_buf = extra + IW_SCAN_MAX_DATA;
	char *current_val = NULL;
	struct iw_event iwe;
	long ldBm;
	char buf[MAX_WPA_IE_LEN * 2 + 30];


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWSCAN \n");

    if (pMgmt->eScanState ==  WMAC_IS_SCANNING) {
        // In scanning..
		return -EAGAIN;
	}
	pBSS = &(pMgmt->sBSSList[0]);
    for (ii = 0, jj = 0; jj < MAX_BSS_NUM ; jj++) {
		if (current_ev >= end_buf)
			break;
        pBSS = &(pMgmt->sBSSList[jj]);
        if (pBSS->bActive) {
		//ADD mac address
		    memset(&iwe, 0, sizeof(iwe));
		    iwe.cmd = SIOCGIWAP;
		    iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
			memcpy(iwe.u.ap_addr.sa_data, pBSS->abyBSSID, WLAN_BSSID_LEN);
                            current_ev = iwe_stream_add_event(info,current_ev,end_buf, &iwe, IW_EV_ADDR_LEN);
                 //ADD ssid
	             memset(&iwe, 0, sizeof(iwe));
                      iwe.cmd = SIOCGIWESSID;
                      pItemSSID = (PWLAN_IE_SSID)pBSS->abySSID;
                       iwe.u.data.length = pItemSSID->len;
                       iwe.u.data.flags = 1;
                      current_ev = iwe_stream_add_point(info,current_ev,end_buf, &iwe, pItemSSID->abySSID);
		//ADD mode
		    memset(&iwe, 0, sizeof(iwe));
		    iwe.cmd = SIOCGIWMODE;
            if (WLAN_GET_CAP_INFO_ESS(pBSS->wCapInfo)) {
		        iwe.u.mode = IW_MODE_INFRA;
            }
            else {
                iwe.u.mode = IW_MODE_ADHOC;
		    }
	        iwe.len = IW_EV_UINT_LEN;
                      current_ev = iwe_stream_add_event(info,current_ev, end_buf, &iwe,  IW_EV_UINT_LEN);
           //ADD frequency
            pSuppRates = (PWLAN_IE_SUPP_RATES)pBSS->abySuppRates;
            pExtSuppRates = (PWLAN_IE_SUPP_RATES)pBSS->abyExtSuppRates;
            memset(&iwe, 0, sizeof(iwe));
           	iwe.cmd = SIOCGIWFREQ;
           	iwe.u.freq.m = pBSS->uChannel;
           	iwe.u.freq.e = 0;
           	iwe.u.freq.i = 0;
                   current_ev = iwe_stream_add_event(info,current_ev,end_buf, &iwe, IW_EV_FREQ_LEN);
            //2008-0409-04, <Add> by Einsn Liu
			{
			int f = (int)pBSS->uChannel - 1;
			if(f < 0)f = 0;
			iwe.u.freq.m = frequency_list[f] * 100000;
			iwe.u.freq.e = 1;
			}
                   current_ev = iwe_stream_add_event(info,current_ev,end_buf, &iwe, IW_EV_FREQ_LEN);
       		//ADD quality
            memset(&iwe, 0, sizeof(iwe));
	        iwe.cmd = IWEVQUAL;
	        RFvRSSITodBm(pDevice, (BYTE)(pBSS->uRSSI), &ldBm);
		    iwe.u.qual.level = ldBm;
	        iwe.u.qual.noise = 0;
//2008-0409-01, <Add> by Einsn Liu
			if(-ldBm<50){
				iwe.u.qual.qual = 100;
			}else  if(-ldBm > 90) {
				 iwe.u.qual.qual = 0;
			}else {
				iwe.u.qual.qual=(40-(-ldBm-50))*100/40;
			}
			iwe.u.qual.updated=7;

	      //  iwe.u.qual.qual = 0;
            current_ev = iwe_stream_add_event(info,current_ev, end_buf, &iwe, IW_EV_QUAL_LEN);

            memset(&iwe, 0, sizeof(iwe));
            iwe.cmd = SIOCGIWENCODE;
            iwe.u.data.length = 0;
            if (WLAN_GET_CAP_INFO_PRIVACY(pBSS->wCapInfo)) {
                iwe.u.data.flags =IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
            }else {
                iwe.u.data.flags = IW_ENCODE_DISABLED;
            }
            current_ev = iwe_stream_add_point(info,current_ev,end_buf, &iwe, pItemSSID->abySSID);

            memset(&iwe, 0, sizeof(iwe));
            iwe.cmd = SIOCGIWRATE;
           	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
      		current_val = current_ev + IW_EV_LCP_LEN;

       		for (kk = 0 ; kk < 12 ; kk++) {
		        if (pSuppRates->abyRates[kk] == 0)
			        break;
		        // Bit rate given in 500 kb/s units (+ 0x80)
		        iwe.u.bitrate.value = ((pSuppRates->abyRates[kk] & 0x7f) * 500000);
                           current_val = iwe_stream_add_value(info,current_ev, current_val, end_buf, &iwe, IW_EV_PARAM_LEN);
		}
       		for (kk = 0 ; kk < 8 ; kk++) {
		        if (pExtSuppRates->abyRates[kk] == 0)
			        break;
		        // Bit rate given in 500 kb/s units (+ 0x80)
		        iwe.u.bitrate.value = ((pExtSuppRates->abyRates[kk] & 0x7f) * 500000);
                          current_val = iwe_stream_add_value(info,current_ev, current_val, end_buf, &iwe, IW_EV_PARAM_LEN);
	        }

	        if((current_val - current_ev) > IW_EV_LCP_LEN)
		        current_ev = current_val;

            memset(&iwe, 0, sizeof(iwe));
            iwe.cmd = IWEVCUSTOM;
            sprintf(buf, "bcn_int=%d", pBSS->wBeaconInterval);
            iwe.u.data.length = strlen(buf);
             current_ev = iwe_stream_add_point(info,current_ev, end_buf, &iwe, buf);

            if ((pBSS->wWPALen > 0) && (pBSS->wWPALen <= MAX_WPA_IE_LEN)) {
                memset(&iwe, 0, sizeof(iwe));
                iwe.cmd = IWEVGENIE;
                iwe.u.data.length = pBSS->wWPALen;
                current_ev = iwe_stream_add_point(info,current_ev, end_buf, &iwe, pBSS->byWPAIE);
            }

            if ((pBSS->wRSNLen > 0) && (pBSS->wRSNLen <= MAX_WPA_IE_LEN)) {
                memset(&iwe, 0, sizeof(iwe));
                iwe.cmd = IWEVGENIE;
                iwe.u.data.length = pBSS->wRSNLen;
                current_ev = iwe_stream_add_point(info,current_ev, end_buf, &iwe, pBSS->byRSNIE);
            }

        }
    }// for

	wrq->length = current_ev - extra;
	return 0;

}


/*
 * Wireless Handler : set frequence or channel
 */

int iwctl_siwfreq(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_freq *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
	int rc = 0;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWFREQ \n");

	// If setting by frequency, convert to a channel
	if((wrq->e == 1) &&
	   (wrq->m >= (int) 2.412e8) &&
	   (wrq->m <= (int) 2.487e8)) {
		int f = wrq->m / 100000;
		int c = 0;
		while((c < 14) && (f != frequency_list[c]))
			c++;
		wrq->e = 0;
		wrq->m = c + 1;
	}
	// Setting by channel number
	if((wrq->m > 14) || (wrq->e > 0))
		rc = -EOPNOTSUPP;
	else {
		int channel = wrq->m;
		if((channel < 1) || (channel > 14)) {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "%s: New channel value of %d is invalid!\n", dev->name, wrq->m);
			rc = -EINVAL;
		} else {
			  // Yes ! We can set it !!!
              DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " Set to channel = %d\n", channel);
			  pDevice->uChannel = channel;
 			 //2007-0207-04,<Add> by EinsnLiu
			 //Make change effect at once
			  pDevice->bCommit = TRUE;
		}
	}

	return rc;
}

/*
 * Wireless Handler : get frequence or channel
 */

int iwctl_giwfreq(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_freq *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWFREQ \n");

#ifdef WEXT_USECHANNELS
	wrq->m = (int)pMgmt->uCurrChannel;
	wrq->e = 0;
#else
	{
		int f = (int)pMgmt->uCurrChannel - 1;
		if(f < 0)
		   f = 0;
		wrq->m = frequency_list[f] * 100000;
		wrq->e = 1;
	}
#endif

	return 0;
}

/*
 * Wireless Handler : set operation mode
 */

int iwctl_siwmode(struct net_device *dev,
             struct iw_request_info *info,
             __u32 *wmode,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
    int rc = 0;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWMODE \n");

    if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP && pDevice->bEnableHostapd) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Can't set operation mode, hostapd is running \n");
        return rc;
    }

	switch(*wmode) {

	case IW_MODE_ADHOC:
	    if (pMgmt->eConfigMode != WMAC_CONFIG_IBSS_STA) {
            pMgmt->eConfigMode = WMAC_CONFIG_IBSS_STA;
            if (pDevice->flags & DEVICE_FLAGS_OPENED) {
		        pDevice->bCommit = TRUE;
   		    }
		}
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "set mode to ad-hoc \n");
		break;
	case IW_MODE_AUTO:
	case IW_MODE_INFRA:
	    if (pMgmt->eConfigMode != WMAC_CONFIG_ESS_STA) {
            pMgmt->eConfigMode = WMAC_CONFIG_ESS_STA;
            if (pDevice->flags & DEVICE_FLAGS_OPENED) {
		        pDevice->bCommit = TRUE;
   		    }
		}
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "set mode to infrastructure \n");
		break;
	case IW_MODE_MASTER:

        pMgmt->eConfigMode = WMAC_CONFIG_ESS_STA;
		rc = -EOPNOTSUPP;
		break;

	    if (pMgmt->eConfigMode != WMAC_CONFIG_AP) {
            pMgmt->eConfigMode = WMAC_CONFIG_AP;
            if (pDevice->flags & DEVICE_FLAGS_OPENED) {
		        pDevice->bCommit = TRUE;
   		    }
		}
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "set mode to Access Point \n");
		break;

	case IW_MODE_REPEAT:
        pMgmt->eConfigMode = WMAC_CONFIG_ESS_STA;
		rc = -EOPNOTSUPP;
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

/*
 * Wireless Handler : get operation mode
 */

int iwctl_giwmode(struct net_device *dev,
             struct iw_request_info *info,
             __u32 *wmode,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWMODE \n");
	// If not managed, assume it's ad-hoc
	switch (pMgmt->eConfigMode) {
	case WMAC_CONFIG_ESS_STA:
		*wmode = IW_MODE_INFRA;
		break;
	case WMAC_CONFIG_IBSS_STA:
        *wmode = IW_MODE_ADHOC;
		break;
	case WMAC_CONFIG_AUTO:
		*wmode = IW_MODE_INFRA;
		break;
	case WMAC_CONFIG_AP:
		*wmode = IW_MODE_MASTER;
		break;
	default:
		*wmode = IW_MODE_ADHOC;
	}

	return 0;
}


/*
 * Wireless Handler : get capability range
 */

int iwctl_giwrange(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra)
{
	struct iw_range *range = (struct iw_range *) extra;
	int		i,k;
    BYTE abySupportedRates[13]= {0x02, 0x04, 0x0B, 0x16, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6C, 0x90};


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWRANGE \n");
	if (wrq->pointer) {
		wrq->length = sizeof(struct iw_range);
		memset(range, 0, sizeof(struct iw_range));
		range->min_nwid = 0x0000;
		range->max_nwid = 0x0000;
		range->num_channels = 14;
		// Should be based on cap_rid.country to give only
		//  what the current card support
		k = 0;
		for(i = 0; i < 14; i++) {
			range->freq[k].i = i + 1; // List index
			range->freq[k].m = frequency_list[i] * 100000;
			range->freq[k++].e = 1;	// Values in table in MHz -> * 10^5 * 10
		}
		range->num_frequency = k;
		// Hum... Should put the right values there
	     #ifdef Calcu_LinkQual
                 range->max_qual.qual = 100;
	     #else
		range->max_qual.qual = 255;
	     #endif
		range->max_qual.level = 0;
		range->max_qual.noise = 0;
		range->sensitivity = 255;

		for(i = 0 ; i < 13 ; i++) {
			range->bitrate[i] = abySupportedRates[i] * 500000;
			if(range->bitrate[i] == 0)
				break;
		}
		range->num_bitrates = i;

		// Set an indication of the max TCP throughput
		// in bit/s that we can expect using this interface.
		//  May be use for QoS stuff... Jean II
		if(i > 2)
			range->throughput = 5 * 1000 * 1000;
		else
			range->throughput = 1.5 * 1000 * 1000;

		range->min_rts = 0;
		range->max_rts = 2312;
		range->min_frag = 256;
		range->max_frag = 2312;


	    // the encoding capabilities
	    range->num_encoding_sizes = 3;
	    // 64(40) bits WEP
	    range->encoding_size[0] = 5;
	    // 128(104) bits WEP
	    range->encoding_size[1] = 13;
	    // 256 bits for WPA-PSK
	    range->encoding_size[2] = 32;
	    // 4 keys are allowed
	    range->max_encoding_tokens = 4;

	    range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
		    IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;

		range->min_pmp = 0;
		range->max_pmp = 1000000;// 1 secs
		range->min_pmt = 0;
		range->max_pmt = 1000000;// 1 secs
		range->pmp_flags = IW_POWER_PERIOD;
		range->pmt_flags = IW_POWER_TIMEOUT;
		range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT | IW_POWER_ALL_R;

		// Transmit Power - values are in mW

        range->txpower[0] = 100;
		range->num_txpower = 1;
		range->txpower_capa = IW_TXPOW_MWATT;
		range->we_version_source = SUPPORTED_WIRELESS_EXT;
		range->we_version_compiled = WIRELESS_EXT;
		range->retry_capa = IW_RETRY_LIMIT | IW_RETRY_LIFETIME;
		range->retry_flags = IW_RETRY_LIMIT;
		range->r_time_flags = IW_RETRY_LIFETIME;
		range->min_retry = 1;
		range->max_retry = 65535;
		range->min_r_time = 1024;
		range->max_r_time = 65535 * 1024;
		// Experimental measurements - boundary 11/5.5 Mb/s
		// Note : with or without the (local->rssi), results
		//  are somewhat different. - Jean II
		range->avg_qual.qual = 6;
		range->avg_qual.level = 176;	// -80 dBm
		range->avg_qual.noise = 0;
	}


	return 0;
}


/*
 * Wireless Handler : set ap mac address
 */

int iwctl_siwap(struct net_device *dev,
             struct iw_request_info *info,
			 struct sockaddr *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
    int rc = 0;
    BYTE                 ZeroBSSID[WLAN_BSSID_LEN]={0x00,0x00,0x00,0x00,0x00,0x00};

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWAP \n");
if (pMgmt->eScanState ==  WMAC_IS_SCANNING) {
        // In scanning..
     printk("SIOCSIWAP(??)-->In scanning...\n");
   //  return -EAGAIN;
  }
	if (wrq->sa_family != ARPHRD_ETHER)
		rc = -EINVAL;
	else {
		memset(pMgmt->abyDesireBSSID, 0xFF, 6);
		memcpy(pMgmt->abyDesireBSSID, wrq->sa_data, 6);
		                //2008-0409-05, <Add> by Einsn Liu
		if((pDevice->bLinkPass == TRUE) &&
                     (memcmp(pMgmt->abyDesireBSSID, pMgmt->abyCurrBSSID, 6)== 0)){
			return rc;
			}
	//mike :add
	 if ((IS_BROADCAST_ADDRESS(pMgmt->abyDesireBSSID)) ||
	     (memcmp(pMgmt->abyDesireBSSID, ZeroBSSID, 6) == 0)){
	      PRINT_K("SIOCSIWAP:invalid desired BSSID return!\n");
               return rc;
         }
       //mike add: if desired AP is hidden ssid(there are two same BSSID in list),
       //                  then ignore,because you don't known which one to be connect with??
       	{
           UINT            ii , uSameBssidNum=0;
                  for (ii = 0; ii < MAX_BSS_NUM; ii++) {
                     if (pMgmt->sBSSList[ii].bActive &&
                        IS_ETH_ADDRESS_EQUAL(pMgmt->sBSSList[ii].abyBSSID,pMgmt->abyDesireBSSID)) {
                        uSameBssidNum++;
                     }
                  }
	     if(uSameBssidNum >= 2) {  //hit: desired AP is in hidden ssid mode!!!
                 PRINT_K("SIOCSIWAP:ignore for desired AP in hidden mode\n");
	        return rc;
	     }
       	}

        if (pDevice->flags & DEVICE_FLAGS_OPENED) {
		    pDevice->bCommit = TRUE;
   		}
	}
	return rc;
}

/*
 * Wireless Handler : get ap mac address
 */

int iwctl_giwap(struct net_device *dev,
             struct iw_request_info *info,
			 struct sockaddr *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWAP \n");

    memcpy(wrq->sa_data, pMgmt->abyCurrBSSID, 6);
   //2008-0410,<Modify> by Einsn Liu
    if ((pDevice->bLinkPass == FALSE) && (pMgmt->eCurrMode != WMAC_MODE_ESS_AP))
        memset(wrq->sa_data, 0, 6);

    if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
        memcpy(wrq->sa_data, pMgmt->abyCurrBSSID, 6);
    }

	wrq->sa_family = ARPHRD_ETHER;

	return 0;

}


/*
 * Wireless Handler : get ap list
 */

int iwctl_giwaplist(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra)
{
	int ii,jj, rc = 0;
	struct sockaddr sock[IW_MAX_AP];
	struct iw_quality qual[IW_MAX_AP];
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWAPLIST \n");
	// Only super-user can see AP list

	if (!capable(CAP_NET_ADMIN)) {
		rc = -EPERM;
		return rc;
	}

	if (wrq->pointer) {

		PKnownBSS pBSS = &(pMgmt->sBSSList[0]);

		for (ii = 0, jj= 0; ii < MAX_BSS_NUM; ii++) {
		    pBSS = &(pMgmt->sBSSList[ii]);
            if (!pBSS->bActive)
                continue;
            if ( jj >= IW_MAX_AP)
                break;
			memcpy(sock[jj].sa_data, pBSS->abyBSSID, 6);
			sock[jj].sa_family = ARPHRD_ETHER;
			qual[jj].level = pBSS->uRSSI;
			qual[jj].qual = qual[jj].noise = 0;
			qual[jj].updated = 2;
			jj++;
		}

		wrq->flags = 1; // Should be define'd
		wrq->length = jj;
		memcpy(extra, sock, sizeof(struct sockaddr)*jj);
		memcpy(extra + sizeof(struct sockaddr)*jj, qual, sizeof(struct iw_quality)*jj);
	}

	return rc;
}


/*
 * Wireless Handler : set essid
 */

int iwctl_siwessid(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
    PWLAN_IE_SSID       pItemSSID;
  //2008-0409-05, <Add> by Einsn Liu
    BYTE  len;


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWESSID \n");
 pDevice->fWPA_Authened = FALSE;
if (pMgmt->eScanState ==  WMAC_IS_SCANNING) {
        // In scanning..
     printk("SIOCSIWESSID(??)-->In scanning...\n");
   //  return -EAGAIN;
  }
	// Check if we asked for `any'
	if(wrq->flags == 0) {
		// Just send an empty SSID list
		memset(pMgmt->abyDesireSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
                  memset(pMgmt->abyDesireBSSID, 0xFF,6);
	    PRINT_K("set essid to 'any' \n");
           #ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
             //Unknown desired AP,so here need not associate??
            //if(pDevice->bWPASuppWextEnabled == TRUE)  {
                  return 0;
            // }
            #endif
	} else {
		// Set the SSID
		memset(pMgmt->abyDesireSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
        pItemSSID = (PWLAN_IE_SSID)pMgmt->abyDesireSSID;
        pItemSSID->byElementID = WLAN_EID_SSID;

		memcpy(pItemSSID->abySSID, extra, wrq->length);
	 if (pItemSSID->abySSID[wrq->length - 1] == '\0') {
           if(wrq->length>0)
		pItemSSID->len = wrq->length - 1;
         }
	else
	  pItemSSID->len = wrq->length;
	printk("set essid to %s \n",pItemSSID->abySSID);
		//2008-0409-05, <Add> by Einsn Liu
       len=(pItemSSID->len > ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->len)?pItemSSID->len:((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->len;
   if((pDevice->bLinkPass == TRUE) &&
  	(memcmp(pItemSSID->abySSID,((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->abySSID,len)==0))
         return 0;

     //mike:need clear desiredBSSID
     if(pItemSSID->len==0) {
        memset(pMgmt->abyDesireBSSID, 0xFF,6);
        return 0;
     }

#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
 //Wext wil order another command of siwap to link with desired AP,
 //so here need not associate??
  if(pDevice->bWPASuppWextEnabled == TRUE)  {
        /*******search if  in hidden ssid mode ****/
        {
           PKnownBSS       pCurr = NULL;
           BYTE                   abyTmpDesireSSID[WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1];
	  UINT            ii , uSameBssidNum=0;

	  memset(abyTmpDesireSSID,0,sizeof(abyTmpDesireSSID));
	  memcpy(abyTmpDesireSSID,pMgmt->abyDesireSSID,sizeof(abyTmpDesireSSID));
            pCurr = BSSpSearchBSSList(pDevice,
                                      NULL,
                                      abyTmpDesireSSID,
                                      pMgmt->eConfigPHYMode
                                      );

            if (pCurr == NULL){
               PRINT_K("SIOCSIWESSID:hidden ssid site survey before associate.......\n");
	      vResetCommandTimer((HANDLE) pDevice);
	      pMgmt->eScanType = WMAC_SCAN_ACTIVE;
               bScheduleCommand((HANDLE) pDevice, WLAN_CMD_BSSID_SCAN, pMgmt->abyDesireSSID);
	      bScheduleCommand((HANDLE) pDevice, WLAN_CMD_SSID, pMgmt->abyDesireSSID);
          }
	 else {  //mike:to find out if that desired SSID is a hidden-ssid AP ,
                     //         by means of judging if there are two same BSSID exist in list ?
                  for (ii = 0; ii < MAX_BSS_NUM; ii++) {
                     if (pMgmt->sBSSList[ii].bActive &&
                        IS_ETH_ADDRESS_EQUAL(pMgmt->sBSSList[ii].abyBSSID, pCurr->abyBSSID)) {
                        uSameBssidNum++;
                     }
                  }
	     if(uSameBssidNum >= 2) {  //hit: desired AP is in hidden ssid mode!!!
                 printk("SIOCSIWESSID:hidden ssid directly associate.......\n");
	        vResetCommandTimer((HANDLE) pDevice);
	        pMgmt->eScanType = WMAC_SCAN_PASSIVE;          //this scan type,you'll submit scan result!
	        bScheduleCommand((HANDLE) pDevice, WLAN_CMD_BSSID_SCAN, pMgmt->abyDesireSSID);
	        bScheduleCommand((HANDLE) pDevice, WLAN_CMD_SSID, pMgmt->abyDesireSSID);
	     }
	 }
        }
     return 0;
  }
	     #endif

	    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "set essid = %s \n", pItemSSID->abySSID);
	}

    if (pDevice->flags & DEVICE_FLAGS_OPENED) {
	    pDevice->bCommit = TRUE;
	}


	return 0;
}


/*
 * Wireless Handler : get essid
 */

int iwctl_giwessid(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra)
{

	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
	PWLAN_IE_SSID       pItemSSID;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWESSID \n");

	// Note : if wrq->u.data.flags != 0, we should
	// get the relevant SSID from the SSID list...

	// Get the current SSID
    pItemSSID = (PWLAN_IE_SSID)pMgmt->abyCurrSSID;
	//pItemSSID = (PWLAN_IE_SSID)pMgmt->abyDesireSSID;
	memcpy(extra, pItemSSID->abySSID , pItemSSID->len);
	extra[pItemSSID->len] = '\0';
	wrq->length = pItemSSID->len + 1;
	        //2008-0409-03, <Add> by Einsn Liu
        wrq->length = pItemSSID->len;
	wrq->flags = 1; // active


	return 0;
}

/*
 * Wireless Handler : set data rate
 */

int iwctl_siwrate(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    int rc = 0;
	u8	brate = 0;
	int	i;
	BYTE abySupportedRates[13]= {0x02, 0x04, 0x0B, 0x16, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6C, 0x90};


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWRATE \n");
    if (!(pDevice->flags & DEVICE_FLAGS_OPENED)) {
        rc = -EINVAL;
        return rc;
    }

	// First : get a valid bit rate value

	// Which type of value
	if((wrq->value < 13) &&
	   (wrq->value >= 0)) {
		// Setting by rate index
		// Find value in the magic rate table
		brate = wrq->value;
	} else {
		// Setting by frequency value
		u8	normvalue = (u8) (wrq->value/500000);

		// Check if rate is valid
		for(i = 0 ; i < 13 ; i++) {
			if(normvalue == abySupportedRates[i]) {
				brate = i;
				break;
			}
		}
	}
	// -1 designed the max rate (mostly auto mode)
	if(wrq->value == -1) {
		// Get the highest available rate
		for(i = 0 ; i < 13 ; i++) {
			if(abySupportedRates[i] == 0)
				break;
		}
		if(i != 0)
			brate = i - 1;

	}
	// Check that it is valid
	// brate is index of abySupportedRates[]
	if(brate > 13 ) {
		rc = -EINVAL;
		return rc;
	}

	// Now, check if we want a fixed or auto value
	if(wrq->fixed != 0) {
		// Fixed mode
		// One rate, fixed
	printk("Rate Fix\n");
		pDevice->bFixRate = TRUE;
        if ((pDevice->byBBType == BB_TYPE_11B)&& (brate > 3)) {
	    pDevice->uConnectionRate = 3;
        }
        else {
            pDevice->uConnectionRate = brate;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Fixed to Rate %d \n", pDevice->uConnectionRate);
        }

	}
	else {
        pDevice->bFixRate = FALSE;
        pDevice->uConnectionRate = 13;
	printk("auto rate:connection_rate is 13\n");
     }

	return rc;
}

/*
 * Wireless Handler : get data rate
 */

int iwctl_giwrate(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_param *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
//2007-0118-05,<Mark> by EinsnLiu
//Mark the unnecessary sentences.
//    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWRATE \n");
    {
        BYTE abySupportedRates[13]= {0x02, 0x04, 0x0B, 0x16, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6C, 0x90};
	    int brate = 0;
//2008-5-8 <modify> by chester
if(pDevice->bLinkPass){
if(pDevice->bFixRate == TRUE){
		if (pDevice->uConnectionRate < 13) {
	        brate = abySupportedRates[pDevice->uConnectionRate];
	    }else {
            if (pDevice->byBBType == BB_TYPE_11B)
	            brate = 0x16;
            if (pDevice->byBBType == BB_TYPE_11G)
	            brate = 0x6C;
            if (pDevice->byBBType == BB_TYPE_11A)
	            brate = 0x6C;
	    }
}
else
{

 brate = abySupportedRates[TxRate_iwconfig];
}
}
else brate =0;
//2007-0118-05,<Mark> by EinsnLiu
//Mark the unnecessary sentences.
/*
	    if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
            if (pDevice->byBBType == BB_TYPE_11B)
	            brate = 0x16;
            if (pDevice->byBBType == BB_TYPE_11G)
	            brate = 0x6C;
            if (pDevice->byBBType == BB_TYPE_11A)
	            brate = 0x6C;
	    }
*/

//    		if (pDevice->uConnectionRate == 13)
//                brate = abySupportedRates[pDevice->wCurrentRate];
	    wrq->value = brate * 500000;
	    // If more than one rate, set auto
	    if (pDevice->bFixRate == TRUE)
	        wrq->fixed = TRUE;
    }


	return 0;
}



/*
 * Wireless Handler : set rts threshold
 */

int iwctl_siwrts(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
	int rc = 0;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWRTS \n");

	{
	    int rthr = wrq->value;
	    if(wrq->disabled)
			rthr = 2312;
	    if((rthr < 0) || (rthr > 2312)) {
			rc = -EINVAL;
    	}else {
		    pDevice->wRTSThreshold = rthr;
	    }
    }

	return 0;
}

/*
 * Wireless Handler : get rts
 */

int iwctl_giwrts(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra)
{
	PSDevice	        pDevice = (PSDevice)netdev_priv(dev);

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWRTS \n");
	wrq->value = pDevice->wRTSThreshold;
	wrq->disabled = (wrq->value >= 2312);
	wrq->fixed = 1;

	return 0;
}

/*
 * Wireless Handler : set fragment threshold
 */

int iwctl_siwfrag(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra)
{
    PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    int rc = 0;
    int fthr = wrq->value;


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWFRAG \n");


    if (wrq->disabled)
		fthr = 2312;
    if((fthr < 256) || (fthr > 2312)) {
		rc = -EINVAL;
    }else {
		 fthr &= ~0x1;	// Get an even value
	     pDevice->wFragmentationThreshold = (u16)fthr;
    }

	return rc;
}

/*
 * Wireless Handler : get fragment threshold
 */

int iwctl_giwfrag(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra)
{
    PSDevice	        pDevice = (PSDevice)netdev_priv(dev);

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWFRAG \n");
	wrq->value = pDevice->wFragmentationThreshold;
	wrq->disabled = (wrq->value >= 2312);
	wrq->fixed = 1;

	return 0;
}



/*
 * Wireless Handler : set retry threshold
 */
int iwctl_siwretry(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra)
{
    PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    int rc = 0;


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWRETRY \n");

	if (wrq->disabled) {
		rc = -EINVAL;
		return rc;
	}

	if (wrq->flags & IW_RETRY_LIMIT) {
		if(wrq->flags & IW_RETRY_MAX)
			pDevice->byLongRetryLimit = wrq->value;
		else if (wrq->flags & IW_RETRY_MIN)
			pDevice->byShortRetryLimit = wrq->value;
		else {
			// No modifier : set both
			pDevice->byShortRetryLimit = wrq->value;
			pDevice->byLongRetryLimit = wrq->value;
		}
	}
	if (wrq->flags & IW_RETRY_LIFETIME) {
		pDevice->wMaxTransmitMSDULifetime = wrq->value;
	}


	return rc;
}

/*
 * Wireless Handler : get retry threshold
 */
int iwctl_giwretry(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra)
{
    PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWRETRY \n");
	wrq->disabled = 0;      // Can't be disabled

	// Note : by default, display the min retry number
	if((wrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
		wrq->flags = IW_RETRY_LIFETIME;
		wrq->value = (int)pDevice->wMaxTransmitMSDULifetime; //ms
	} else if((wrq->flags & IW_RETRY_MAX)) {
		wrq->flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
		wrq->value = (int)pDevice->byLongRetryLimit;
	} else {
		wrq->flags = IW_RETRY_LIMIT;
		wrq->value = (int)pDevice->byShortRetryLimit;
		if((int)pDevice->byShortRetryLimit != (int)pDevice->byLongRetryLimit)
			wrq->flags |= IW_RETRY_MIN;
	}


	return 0;
}


/*
 * Wireless Handler : set encode mode
 */
int iwctl_siwencode(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra)
{
    PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
	DWORD dwKeyIndex = (DWORD)(wrq->flags & IW_ENCODE_INDEX);
	int ii,uu, rc = 0;
	int index = (wrq->flags & IW_ENCODE_INDEX);

//2007-0207-07,<Modify> by EinsnLiu
//There are some problems when using iwconfig encode/key command to set the WEP key.
//I almost rewrite this function.
//now it support:(assume the wireless interface's name is eth0)
//iwconfig eth0 key [1] 1122334455 open  /*set key stirng to index 1,and driver using key index is set to 1*/
//iwconfig eth0 key [3]    /*set driver using  key index to 3,the key string no change */
//iwconfig eth0 key 1122334455  /*set key string to driver using index*/
//iwconfig eth0 key restricted  /*enable share key*/

	PSKeyTable pkeytab;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWENCODE \n");

if((wrq->flags & IW_ENCODE_DISABLED)==0){
	//Not disable encryption

	if (dwKeyIndex > WLAN_WEP_NKEYS) {
		rc = -EINVAL;
        return rc;
    	}

	if(dwKeyIndex<1&&((wrq->flags&IW_ENCODE_NOKEY)==0)){//set default key
		if(pDevice->byKeyIndex<WLAN_WEP_NKEYS){
			dwKeyIndex=pDevice->byKeyIndex;
			}
		else dwKeyIndex=0;
		}else dwKeyIndex--;


	// Check the size of the key
	if (wrq->length > WLAN_WEP232_KEYLEN) {
		rc = -EINVAL;
        return rc;
	}

	if(wrq->length>0){//have key

        if (wrq->length ==  WLAN_WEP232_KEYLEN) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Set 232 bit wep key\n");
        }
        else if (wrq->length ==  WLAN_WEP104_KEYLEN) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Set 104 bit wep key\n");
        }
        else if (wrq->length == WLAN_WEP40_KEYLEN) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Set 40 bit wep key, index= %d\n", (int)dwKeyIndex);
        }else {//no support length
		rc = -EINVAL;
        return rc;
		}
        memset(pDevice->abyKey, 0, WLAN_WEP232_KEYLEN);
        memcpy(pDevice->abyKey, extra, wrq->length);

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"abyKey: ");
        for (ii = 0; ii < wrq->length; ii++) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "%02x ", pDevice->abyKey[ii]);
        }

        if (pDevice->flags & DEVICE_FLAGS_OPENED) {
            spin_lock_irq(&pDevice->lock);
            KeybSetDefaultKey(&(pDevice->sKey),
                            (DWORD)(dwKeyIndex | (1 << 31)),
                           	wrq->length,
                            NULL,
                            pDevice->abyKey,
                            KEY_CTL_WEP,
                            pDevice->PortOffset,
                            pDevice->byLocalID
                          );
            spin_unlock_irq(&pDevice->lock);
        }
        pDevice->byKeyIndex = (BYTE)dwKeyIndex;
        pDevice->uKeyLength = wrq->length;
        pDevice->bTransmitKey = TRUE;
        pDevice->bEncryptionEnable = TRUE;
        pDevice->eEncryptionStatus = Ndis802_11Encryption1Enabled;

		}else if(index>0){
	//when the length is 0 the request only changes the default transmit key index
	//check the new key has a non zero lenget
	if(pDevice->bEncryptionEnable==FALSE)
	{
		rc = -EINVAL;
        	return rc;
	}
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Just set Default key Index:\n");
	pkeytab=&(pDevice->sKey.KeyTable[MAX_KEY_TABLE-1]);
	if(pkeytab->GroupKey[(BYTE)dwKeyIndex].uKeyLength==0){
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Default key len is 0\n");
		rc = -EINVAL;
      	  	return rc;
		}
	 pDevice->byKeyIndex =(BYTE)dwKeyIndex;
	 pkeytab->dwGTKeyIndex =dwKeyIndex | (1 << 31);
	 pkeytab->GroupKey[(BYTE)dwKeyIndex].dwKeyIndex=dwKeyIndex | (1 << 31);
	}

}else {//disable the key
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Disable WEP function\n");
	if(pDevice->bEncryptionEnable==FALSE)
		return 0;
	pMgmt->bShareKeyAlgorithm = FALSE;
        pDevice->bEncryptionEnable = FALSE;
        pDevice->eEncryptionStatus = Ndis802_11EncryptionDisabled;
        if (pDevice->flags & DEVICE_FLAGS_OPENED) {
            spin_lock_irq(&pDevice->lock);
            for(uu=0;uu<MAX_KEY_TABLE;uu++)
                MACvDisableKeyEntry(pDevice->PortOffset, uu);
            spin_unlock_irq(&pDevice->lock);
        }
}
//End Modify,Einsn

/*
     DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWENCODE \n");

	// Check the size of the key
	if (wrq->length > WLAN_WEP232_KEYLEN) {
		rc = -EINVAL;
        return rc;
	}

	if (dwKeyIndex > WLAN_WEP_NKEYS) {
		rc = -EINVAL;
        return rc;
    }

    if (dwKeyIndex > 0)
		dwKeyIndex--;

	// Send the key to the card
	if (wrq->length > 0) {

        if (wrq->length ==  WLAN_WEP232_KEYLEN) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Set 232 bit wep key\n");
        }
        else if (wrq->length ==  WLAN_WEP104_KEYLEN) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Set 104 bit wep key\n");
        }
        else if (wrq->length == WLAN_WEP40_KEYLEN) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Set 40 bit wep key, index= %d\n", (int)dwKeyIndex);
        }
        memset(pDevice->abyKey, 0, WLAN_WEP232_KEYLEN);
        memcpy(pDevice->abyKey, extra, wrq->length);

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"abyKey: ");
        for (ii = 0; ii < wrq->length; ii++) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "%02x ", pDevice->abyKey[ii]);
        }

        if (pDevice->flags & DEVICE_FLAGS_OPENED) {
            spin_lock_irq(&pDevice->lock);
            KeybSetDefaultKey(&(pDevice->sKey),
                            (DWORD)(pDevice->byKeyIndex | (1 << 31)),
                            pDevice->uKeyLength,
                            NULL,
                            pDevice->abyKey,
                            KEY_CTL_WEP,
                            pDevice->PortOffset,
                            pDevice->byLocalID
                          );
            spin_unlock_irq(&pDevice->lock);
        }
        pDevice->byKeyIndex = (BYTE)dwKeyIndex;
        pDevice->uKeyLength = wrq->length;
        pDevice->bTransmitKey = TRUE;
        pDevice->bEncryptionEnable = TRUE;
        pDevice->eEncryptionStatus = Ndis802_11Encryption1Enabled;

		// Do we want to just set the transmit key index ?
		if ( index < 4 ) {
		    pDevice->byKeyIndex = index;
		}
		else if(!(wrq->flags & IW_ENCODE_MODE)) {
				rc = -EINVAL;
				return rc;
	    }
	}
	// Read the flags
	if(wrq->flags & IW_ENCODE_DISABLED){

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Disable WEP function\n");
		pMgmt->bShareKeyAlgorithm = FALSE;
        pDevice->bEncryptionEnable = FALSE;
        pDevice->eEncryptionStatus = Ndis802_11EncryptionDisabled;
        if (pDevice->flags & DEVICE_FLAGS_OPENED) {
            spin_lock_irq(&pDevice->lock);
            for(uu=0;uu<MAX_KEY_TABLE;uu++)
                MACvDisableKeyEntry(pDevice->PortOffset, uu);
            spin_unlock_irq(&pDevice->lock);
        }
	}
*/

	if(wrq->flags & IW_ENCODE_RESTRICTED) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Enable WEP & ShareKey System\n");
		pMgmt->bShareKeyAlgorithm = TRUE;
	}
	if(wrq->flags & IW_ENCODE_OPEN) {
	    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Enable WEP & Open System\n");
		pMgmt->bShareKeyAlgorithm = FALSE;
	}
	return rc;
}

/*
 * Wireless Handler : get encode mode
 */
 /*
int iwctl_giwencode(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra)
{
    PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
    int rc = 0;
    char abyKey[WLAN_WEP232_KEYLEN];
	UINT index = (UINT)(wrq->flags & IW_ENCODE_INDEX);
	PSKeyItem   pKey = NULL;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWENCODE\n");
//2007-0207-06,<Add> by EinsnLiu
//the key index in iwconfig is 1-4 when our driver is 0-3
//so it can't be used directly.
//if the index is 0,we should used the index set by driver.
	if (index > WLAN_WEP_NKEYS) {
		rc = -EINVAL;
        return rc;
    	}
	if(index<1){//set default key
		if(pDevice->byKeyIndex<WLAN_WEP_NKEYS){
			index=pDevice->byKeyIndex;
			}
		else index=0;
		}else index--;
//End Add,Einsn

	memset(abyKey, 0, sizeof(abyKey));
	// Check encryption mode
	wrq->flags = IW_ENCODE_NOKEY;
	// Is WEP enabled ???
	if (pDevice->bEncryptionEnable)
		wrq->flags |=  IW_ENCODE_ENABLED;
    else
		wrq->flags |=  IW_ENCODE_DISABLED;

    if (pMgmt->bShareKeyAlgorithm)
		wrq->flags |=  IW_ENCODE_RESTRICTED;
	else
		wrq->flags |=  IW_ENCODE_OPEN;

	if (KeybGetKey(&(pDevice->sKey), pDevice->abyBroadcastAddr, (BYTE)index , &pKey)){
        wrq->length = pKey->uKeyLength;
        memcpy(abyKey, pKey->abyKey,  pKey->uKeyLength);
//2007-0207-06,<Modify> by EinsnLiu
//only get key success need to  copy data
//index should +1.
//there is not necessary to return -EINVAL when get key failed
//if return -EINVAL,the encryption item can't be display by the command "iwconfig".
	wrq->flags |= index+1;
	memcpy(extra,  abyKey, WLAN_WEP232_KEYLEN);
    }

    //else {
    //    rc = -EINVAL;
   //     return rc;
  //  }


//End Modify,Einsn

	return 0;
}
*/

//2008-0409-06, <Add> by Einsn Liu

int iwctl_giwencode(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrq,
			char *extra)
{
	PSDevice			pDevice = (PSDevice)netdev_priv(dev);
	PSMgmtObject		pMgmt = &(pDevice->sMgmtObj);
	char abyKey[WLAN_WEP232_KEYLEN];

	UINT index = (UINT)(wrq->flags & IW_ENCODE_INDEX);
	PSKeyItem	pKey = NULL;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWENCODE\n");

	if (index > WLAN_WEP_NKEYS) {
		return	-EINVAL;
	}
	if(index<1){//get default key
		if(pDevice->byKeyIndex<WLAN_WEP_NKEYS){
			index=pDevice->byKeyIndex;
         	} else
                      index=0;
	}else
             index--;

	memset(abyKey, 0, WLAN_WEP232_KEYLEN);
	// Check encryption mode
	wrq->flags = IW_ENCODE_NOKEY;
	// Is WEP enabled ???
	if (pDevice->bEncryptionEnable)
		wrq->flags |=  IW_ENCODE_ENABLED;
	else
		wrq->flags |=  IW_ENCODE_DISABLED;

	if (pMgmt->bShareKeyAlgorithm)
		wrq->flags |=  IW_ENCODE_RESTRICTED;
	else
		wrq->flags |=  IW_ENCODE_OPEN;
		wrq->length=0;

	if((index==0)&&(pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled||
		pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled)){//get wpa pairwise  key
			if (KeybGetKey(&(pDevice->sKey),pMgmt->abyCurrBSSID, 0xffffffff, &pKey)){
			   wrq->length = pKey->uKeyLength;
				  memcpy(abyKey, pKey->abyKey,	pKey->uKeyLength);
				  memcpy(extra,  abyKey, WLAN_WEP232_KEYLEN);
			   }
	}else if (KeybGetKey(&(pDevice->sKey), pDevice->abyBroadcastAddr, (BYTE)index , &pKey)){
			wrq->length = pKey->uKeyLength;
			memcpy(abyKey, pKey->abyKey,  pKey->uKeyLength);
		memcpy(extra,  abyKey, WLAN_WEP232_KEYLEN);
	}

	wrq->flags |= index+1;

	return 0;
}


/*
 * Wireless Handler : set power mode
 */
int iwctl_siwpower(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra)
{
    PSDevice            pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
    int rc = 0;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWPOWER \n");

    if (!(pDevice->flags & DEVICE_FLAGS_OPENED)) {
		 rc = -EINVAL;
		 return rc;
	}

	if (wrq->disabled) {
		pDevice->ePSMode = WMAC_POWER_CAM;
		PSvDisablePowerSaving(pDevice);
		return rc;
	}
	if ((wrq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
         pDevice->ePSMode = WMAC_POWER_FAST;
         PSvEnablePowerSaving((HANDLE)pDevice, pMgmt->wListenInterval);

	} else if ((wrq->flags & IW_POWER_TYPE) == IW_POWER_PERIOD) {
	     pDevice->ePSMode = WMAC_POWER_FAST;
         PSvEnablePowerSaving((HANDLE)pDevice, pMgmt->wListenInterval);
	}
	switch (wrq->flags & IW_POWER_MODE) {
	case IW_POWER_UNICAST_R:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWPOWER: IW_POWER_UNICAST_R \n");
		rc = -EINVAL;
		break;
	case IW_POWER_ALL_R:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWPOWER: IW_POWER_ALL_R \n");
		rc = -EINVAL;
	case IW_POWER_ON:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWPOWER: IW_POWER_ON \n");
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

/*
 * Wireless Handler : get power mode
 */
int iwctl_giwpower(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra)
{
    PSDevice            pDevice = (PSDevice)netdev_priv(dev);
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
    int mode = pDevice->ePSMode;


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWPOWER \n");


	if ((wrq->disabled = (mode == WMAC_POWER_CAM)))
	    return 0;

	if ((wrq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		wrq->value = (int)((pMgmt->wListenInterval * pMgmt->wCurrBeaconPeriod) << 10);
		wrq->flags = IW_POWER_TIMEOUT;
	} else {
		wrq->value = (int)((pMgmt->wListenInterval * pMgmt->wCurrBeaconPeriod) << 10);
		wrq->flags = IW_POWER_PERIOD;
	}
	wrq->flags |= IW_POWER_ALL_R;

	return 0;
}


/*
 * Wireless Handler : get Sensitivity
 */
int iwctl_giwsens(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *wrq,
			 char *extra)
{
    PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
    long ldBm;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWSENS \n");
    if (pDevice->bLinkPass == TRUE) {
        RFvRSSITodBm(pDevice, (BYTE)(pDevice->uCurrRSSI), &ldBm);
	    wrq->value = ldBm;
	}
	else {
	    wrq->value = 0;
    };
	wrq->disabled = (wrq->value == 0);
	wrq->fixed = 1;


	return 0;
}

//2008-0409-07, <Add> by Einsn Liu
#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT

int iwctl_siwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *wrq,
			  char *extra)
{
	PSDevice			pDevice = (PSDevice)netdev_priv(dev);
	PSMgmtObject	pMgmt = &(pDevice->sMgmtObj);
	int ret=0;
	static int wpa_version=0;  //must be static to save the last value,einsn liu
	static int pairwise=0;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWAUTH \n");
	switch (wrq->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
		wpa_version = wrq->value;
		if(wrq->value == IW_AUTH_WPA_VERSION_DISABLED) {
		       PRINT_K("iwctl_siwauth:set WPADEV to disable at 1??????\n");
			//pDevice->bWPADevEnable = FALSE;
		}
		else if(wrq->value == IW_AUTH_WPA_VERSION_WPA) {
                          PRINT_K("iwctl_siwauth:set WPADEV to WPA1******\n");
		}
		else {
                          PRINT_K("iwctl_siwauth:set WPADEV to WPA2******\n");
		}
		//pDevice->bWPASuppWextEnabled =TRUE;
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
		pairwise = wrq->value;
		if(pairwise == IW_AUTH_CIPHER_CCMP){
			pDevice->eEncryptionStatus = Ndis802_11Encryption3Enabled;
		}else if(pairwise == IW_AUTH_CIPHER_TKIP){
			pDevice->eEncryptionStatus = Ndis802_11Encryption2Enabled;
		}else if(pairwise == IW_AUTH_CIPHER_WEP40||pairwise == IW_AUTH_CIPHER_WEP104){
			pDevice->eEncryptionStatus = Ndis802_11Encryption1Enabled;
		}else if(pairwise == IW_AUTH_CIPHER_NONE){
			//do nothing,einsn liu
		}else pDevice->eEncryptionStatus = Ndis802_11EncryptionDisabled;

		break;
	case IW_AUTH_CIPHER_GROUP:
		if(wpa_version == IW_AUTH_WPA_VERSION_DISABLED)
			break;
		if(pairwise == IW_AUTH_CIPHER_NONE){
			if(wrq->value == IW_AUTH_CIPHER_CCMP){
				pDevice->eEncryptionStatus = Ndis802_11Encryption3Enabled;
			}else {
				pDevice->eEncryptionStatus = Ndis802_11Encryption2Enabled;
			}
		}
		break;
	case IW_AUTH_KEY_MGMT:

		if(wpa_version == IW_AUTH_WPA_VERSION_WPA2){
			if(wrq->value == IW_AUTH_KEY_MGMT_PSK)
				pMgmt->eAuthenMode = WMAC_AUTH_WPA2PSK;
			else pMgmt->eAuthenMode = WMAC_AUTH_WPA2;
		}else if(wpa_version == IW_AUTH_WPA_VERSION_WPA){
			if(wrq->value == 0){
				pMgmt->eAuthenMode = WMAC_AUTH_WPANONE;
			}else if(wrq->value == IW_AUTH_KEY_MGMT_PSK)
				pMgmt->eAuthenMode = WMAC_AUTH_WPAPSK;
			else pMgmt->eAuthenMode = WMAC_AUTH_WPA;
		}

		break;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		break;		/* FIXME */
	case IW_AUTH_DROP_UNENCRYPTED:
		break;
	case IW_AUTH_80211_AUTH_ALG:
		if(wrq->value==IW_AUTH_ALG_OPEN_SYSTEM){
			pMgmt->bShareKeyAlgorithm=FALSE;
		}else if(wrq->value==IW_AUTH_ALG_SHARED_KEY){
			pMgmt->bShareKeyAlgorithm=TRUE;
		}
		break;
	case IW_AUTH_WPA_ENABLED:
		//pDevice->bWPADevEnable = !! wrq->value;
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		break;
	case IW_AUTH_ROAMING_CONTROL:
		ret = -EOPNOTSUPP;
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		pDevice->bEncryptionEnable = !!wrq->value;
		if(pDevice->bEncryptionEnable == FALSE){
			wpa_version = 0;
			pairwise = 0;
			pDevice->eEncryptionStatus = Ndis802_11EncryptionDisabled;
			pMgmt->bShareKeyAlgorithm = FALSE;
			pMgmt->eAuthenMode = FALSE;
			//pDevice->bWPADevEnable = FALSE;
		}

		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
/*
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "wpa_version = %d\n",wpa_version);
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "pairwise = %d\n",pairwise);
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "pDevice->eEncryptionStatus = %d\n",pDevice->eEncryptionStatus);
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "pMgmt->eAuthenMode  = %d\n",pMgmt->eAuthenMode);
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "pMgmt->bShareKeyAlgorithm = %s\n",pMgmt->bShareKeyAlgorithm?"TRUE":"FALSE");
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "pDevice->bEncryptionEnable = %s\n",pDevice->bEncryptionEnable?"TRUE":"FALSE");
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "pDevice->bWPADevEnable = %s\n",pDevice->bWPADevEnable?"TRUE":"FALSE");
*/
   return ret;
}


int iwctl_giwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *wrq,
			  char *extra)
{
	return -EOPNOTSUPP;
}



int iwctl_siwgenie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *wrq,
			  char *extra)
{
	PSDevice			pDevice = (PSDevice)netdev_priv(dev);
	PSMgmtObject	pMgmt = &(pDevice->sMgmtObj);
	int ret=0;

	if(wrq->length){
		if ((wrq->length < 2) || (extra[1]+2 != wrq->length)) {
			ret = -EINVAL;
			goto out;
		}
		if(wrq->length > MAX_WPA_IE_LEN){
			ret = -ENOMEM;
			goto out;
		}
		memset(pMgmt->abyWPAIE, 0, MAX_WPA_IE_LEN);
		if(copy_from_user(pMgmt->abyWPAIE, extra, wrq->length)){
			ret = -EFAULT;
			goto out;
		}
		pMgmt->wWPAIELen = wrq->length;
	}else {
		memset(pMgmt->abyWPAIE, 0, MAX_WPA_IE_LEN);
		pMgmt->wWPAIELen = 0;
	}

	out://not completely ...not necessary in wpa_supplicant 0.5.8
	return 0;
}

int iwctl_giwgenie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *wrq,
			  char *extra)
{
	PSDevice			pDevice = (PSDevice)netdev_priv(dev);
	PSMgmtObject	pMgmt = &(pDevice->sMgmtObj);
	int ret=0;
	int space = wrq->length;

	wrq->length = 0;
	if(pMgmt->wWPAIELen > 0){
		wrq->length = pMgmt->wWPAIELen;
		if(pMgmt->wWPAIELen <= space){
			if(copy_to_user(extra, pMgmt->abyWPAIE, pMgmt->wWPAIELen)){
				ret = -EFAULT;
			}
		}else
			ret = -E2BIG;
	}

	return ret;
}


int iwctl_siwencodeext(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra)
{
    PSDevice	        pDevice = (PSDevice)netdev_priv(dev);
	struct iw_encode_ext *ext = (struct iw_encode_ext*)extra;
    struct viawget_wpa_param *param=NULL;
//original member
    wpa_alg alg_name;
    u8  addr[6];
    int key_idx, set_tx=0;
    u8  seq[IW_ENCODE_SEQ_MAX_SIZE];
    u8 key[64];
    size_t seq_len=0,key_len=0;
//
   // int ii;
    u8 *buf;
    size_t blen;
    u8 key_array[64];
    int ret=0;

PRINT_K("SIOCSIWENCODEEXT...... \n");

blen = sizeof(*param);
buf = kmalloc((int)blen, (int)GFP_KERNEL);
if (buf == NULL)
    return -ENOMEM;
memset(buf, 0, blen);
param = (struct viawget_wpa_param *) buf;

//recover alg_name
switch (ext->alg) {
    case IW_ENCODE_ALG_NONE:
                  alg_name = WPA_ALG_NONE;
		break;
    case IW_ENCODE_ALG_WEP:
                  alg_name = WPA_ALG_WEP;
		break;
    case IW_ENCODE_ALG_TKIP:
                  alg_name = WPA_ALG_TKIP;
		break;
    case IW_ENCODE_ALG_CCMP:
                  alg_name = WPA_ALG_CCMP;
		break;
    default:
		PRINT_K("Unknown alg = %d\n",ext->alg);
		ret= -ENOMEM;
		goto error;
		}
//recover addr
 memcpy(addr, ext->addr.sa_data, ETH_ALEN);
//recover key_idx
  key_idx = (wrq->flags&IW_ENCODE_INDEX) - 1;
//recover set_tx
if(ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
   set_tx = 1;
//recover seq,seq_len
	if(ext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
   seq_len=IW_ENCODE_SEQ_MAX_SIZE;
   memcpy(seq, ext->rx_seq, seq_len);
		}
//recover key,key_len
if(ext->key_len) {
  key_len=ext->key_len;
  memcpy(key, &ext->key[0], key_len);
	}

memset(key_array, 0, 64);
if ( key_len > 0) {
     memcpy(key_array, key, key_len);
    if (key_len == 32) {
          // notice ! the oder
	  memcpy(&key_array[16], &key[24], 8);
	  memcpy(&key_array[24], &key[16], 8);
	}
	}

/**************Translate iw_encode_ext to viawget_wpa_param****************/
memcpy(param->addr, addr, ETH_ALEN);
param->u.wpa_key.alg_name = (int)alg_name;
param->u.wpa_key.set_tx = set_tx;
param->u.wpa_key.key_index = key_idx;
param->u.wpa_key.key_len = key_len;
param->u.wpa_key.key = (u8 *)key_array;
param->u.wpa_key.seq = (u8 *)seq;
param->u.wpa_key.seq_len = seq_len;

#if 0
printk("param->u.wpa_key.alg_name =%d\n",param->u.wpa_key.alg_name);
printk("param->addr=%02x:%02x:%02x:%02x:%02x:%02x\n",
	      param->addr[0],param->addr[1],param->addr[2],
	      param->addr[3],param->addr[4],param->addr[5]);
printk("param->u.wpa_key.set_tx =%d\n",param->u.wpa_key.set_tx);
printk("param->u.wpa_key.key_index =%d\n",param->u.wpa_key.key_index);
printk("param->u.wpa_key.key_len =%d\n",param->u.wpa_key.key_len);
printk("param->u.wpa_key.key =");
for(ii=0;ii<param->u.wpa_key.key_len;ii++)
	printk("%02x:",param->u.wpa_key.key[ii]);
         printk("\n");
printk("param->u.wpa_key.seq_len =%d\n",param->u.wpa_key.seq_len);
printk("param->u.wpa_key.seq =");
for(ii=0;ii<param->u.wpa_key.seq_len;ii++)
	printk("%02x:",param->u.wpa_key.seq[ii]);
         printk("\n");

printk("...........\n");
#endif
//****set if current action is Network Manager count??
//****this method is so foolish,but there is no other way???
if(param->u.wpa_key.alg_name == WPA_ALG_NONE) {
   if(param->u.wpa_key.key_index ==0) {
     pDevice->bwextcount++;
    }
   if((pDevice->bwextcount == 1)&&(param->u.wpa_key.key_index ==1)) {
 pDevice->bwextcount++;
    }
   if((pDevice->bwextcount ==2)&&(param->u.wpa_key.key_index ==2)) {
 pDevice->bwextcount++;
	}
   if((pDevice->bwextcount ==3)&&(param->u.wpa_key.key_index ==3)) {
 pDevice->bwextcount++;
        }
		 }
if( pDevice->bwextcount == 4) {
    printk("SIOCSIWENCODEEXT:Enable WPA WEXT SUPPORT!!!!!\n");
 pDevice->bwextcount=0;
   pDevice->bWPASuppWextEnabled = TRUE;
		 }
//******

		spin_lock_irq(&pDevice->lock);
 ret = wpa_set_keys(pDevice, param, TRUE);
		spin_unlock_irq(&pDevice->lock);

error:
kfree(param);
	return ret;
}



int iwctl_giwencodeext(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra)
{
		return -EOPNOTSUPP;;
}

int iwctl_siwmlme(struct net_device *dev,
				struct iw_request_info * info,
				struct iw_point *wrq,
				char *extra)
{
	PSDevice			pDevice = (PSDevice)netdev_priv(dev);
	PSMgmtObject	pMgmt = &(pDevice->sMgmtObj);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	//u16 reason = cpu_to_le16(mlme->reason_code);
	int ret = 0;

	if(memcmp(pMgmt->abyCurrBSSID, mlme->addr.sa_data, ETH_ALEN)){
		ret = -EINVAL;
		return ret;
	}
	switch(mlme->cmd){
	case IW_MLME_DEAUTH:
		//this command seems to be not complete,please test it --einsnliu
		//bScheduleCommand((HANDLE) pDevice, WLAN_CMD_DEAUTH, (PBYTE)&reason);
		break;
	case IW_MLME_DISASSOC:
		if(pDevice->bLinkPass == TRUE){
					  printk("iwctl_siwmlme--->send DISASSOCIATE\n");
		  //clear related flags
		   memset(pMgmt->abyDesireBSSID, 0xFF,6);
		KeyvInitTable(&pDevice->sKey, pDevice->PortOffset);
			bScheduleCommand((HANDLE)pDevice, WLAN_CMD_DISASSOCIATE, NULL);
		}
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;

}

#endif


/*------------------------------------------------------------------*/
/*
 * Structures to export the Wireless Handlers
 */


/*
static const iw_handler		iwctl_handler[] =
{
	(iw_handler) iwctl_commit,      // SIOCSIWCOMMIT
	(iw_handler) iwctl_giwname,     // SIOCGIWNAME
	(iw_handler) NULL,				// SIOCSIWNWID
	(iw_handler) NULL,				// SIOCGIWNWID
	(iw_handler) iwctl_siwfreq,		// SIOCSIWFREQ
	(iw_handler) iwctl_giwfreq,		// SIOCGIWFREQ
	(iw_handler) iwctl_siwmode,		// SIOCSIWMODE
	(iw_handler) iwctl_giwmode,		// SIOCGIWMODE
	(iw_handler) NULL,		        // SIOCSIWSENS
	(iw_handler) iwctl_giwsens,		        // SIOCGIWSENS
	(iw_handler) NULL, 		        // SIOCSIWRANGE
	(iw_handler) iwctl_giwrange,		// SIOCGIWRANGE
	(iw_handler) NULL,         		    // SIOCSIWPRIV
	(iw_handler) NULL,             		// SIOCGIWPRIV
	(iw_handler) NULL,             		// SIOCSIWSTATS
	(iw_handler) NULL,                  // SIOCGIWSTATS
    (iw_handler) NULL,                  // SIOCSIWSPY
	(iw_handler) NULL,		            // SIOCGIWSPY
	(iw_handler) NULL,				    // -- hole --
	(iw_handler) NULL,				    // -- hole --
	(iw_handler) iwctl_siwap,		    // SIOCSIWAP
	(iw_handler) iwctl_giwap,		    // SIOCGIWAP
	(iw_handler) NULL,				    // -- hole -- 0x16
	(iw_handler) iwctl_giwaplist,       // SIOCGIWAPLIST
	(iw_handler) iwctl_siwscan,         // SIOCSIWSCAN
	(iw_handler) iwctl_giwscan,         // SIOCGIWSCAN
	(iw_handler) iwctl_siwessid,		// SIOCSIWESSID
	(iw_handler) iwctl_giwessid,		// SIOCGIWESSID
	(iw_handler) NULL,		// SIOCSIWNICKN
	(iw_handler) NULL,		// SIOCGIWNICKN
	(iw_handler) NULL,				    // -- hole --
	(iw_handler) NULL,				    // -- hole --
	(iw_handler) iwctl_siwrate,		// SIOCSIWRATE 0x20
	(iw_handler) iwctl_giwrate,		// SIOCGIWRATE
	(iw_handler) iwctl_siwrts,		// SIOCSIWRTS
	(iw_handler) iwctl_giwrts,		// SIOCGIWRTS
	(iw_handler) iwctl_siwfrag,		// SIOCSIWFRAG
	(iw_handler) iwctl_giwfrag,		// SIOCGIWFRAG
	(iw_handler) NULL,		// SIOCSIWTXPOW
	(iw_handler) NULL,		// SIOCGIWTXPOW
	(iw_handler) iwctl_siwretry,		// SIOCSIWRETRY
	(iw_handler) iwctl_giwretry,		// SIOCGIWRETRY
	(iw_handler) iwctl_siwencode,		// SIOCSIWENCODE
	(iw_handler) iwctl_giwencode,		// SIOCGIWENCODE
	(iw_handler) iwctl_siwpower,		// SIOCSIWPOWER
	(iw_handler) iwctl_giwpower,		// SIOCGIWPOWER
	(iw_handler) NULL,			// -- hole --
	(iw_handler) NULL,			// -- hole --
	(iw_handler) iwctl_siwgenie,    // SIOCSIWGENIE
	(iw_handler) iwctl_giwgenie,    // SIOCGIWGENIE
	(iw_handler) iwctl_siwauth,		// SIOCSIWAUTH
	(iw_handler) iwctl_giwauth,		// SIOCGIWAUTH
	(iw_handler) iwctl_siwencodeext,		// SIOCSIWENCODEEXT
	(iw_handler) iwctl_giwencodeext,		// SIOCGIWENCODEEXT
	(iw_handler) NULL,				// SIOCSIWPMKSA
	(iw_handler) NULL,				// -- hole --

};
*/

static const iw_handler		iwctl_handler[] =
{
	(iw_handler) iwctl_commit,      // SIOCSIWCOMMIT
	(iw_handler) NULL,      // SIOCGIWNAME
	(iw_handler) NULL,				// SIOCSIWNWID
	(iw_handler) NULL,				// SIOCGIWNWID
	(iw_handler) NULL,		// SIOCSIWFREQ
	(iw_handler) NULL,		// SIOCGIWFREQ
	(iw_handler) NULL,		// SIOCSIWMODE
	(iw_handler) NULL,		// SIOCGIWMODE
	(iw_handler) NULL,		        // SIOCSIWSENS
	(iw_handler) NULL,		        // SIOCGIWSENS
	(iw_handler) NULL, 		        // SIOCSIWRANGE
	(iw_handler) iwctl_giwrange,		// SIOCGIWRANGE
	(iw_handler) NULL,         		    // SIOCSIWPRIV
	(iw_handler) NULL,             		// SIOCGIWPRIV
	(iw_handler) NULL,             		// SIOCSIWSTATS
	(iw_handler) NULL,                  // SIOCGIWSTATS
    (iw_handler) NULL,                  // SIOCSIWSPY
	(iw_handler) NULL,		            // SIOCGIWSPY
	(iw_handler) NULL,				    // -- hole --
	(iw_handler) NULL,				    // -- hole --
	(iw_handler) NULL,		    // SIOCSIWAP
	(iw_handler) NULL,		    // SIOCGIWAP
	(iw_handler) NULL,				    // -- hole -- 0x16
	(iw_handler) NULL,       // SIOCGIWAPLIST
	(iw_handler) iwctl_siwscan,         // SIOCSIWSCAN
	(iw_handler) iwctl_giwscan,         // SIOCGIWSCAN
	(iw_handler) NULL,		// SIOCSIWESSID
	(iw_handler) NULL,		// SIOCGIWESSID
	(iw_handler) NULL,		// SIOCSIWNICKN
	(iw_handler) NULL,		// SIOCGIWNICKN
	(iw_handler) NULL,		// -- hole --
	(iw_handler) NULL,		// -- hole --
	(iw_handler) NULL,		// SIOCSIWRATE 0x20
	(iw_handler) NULL,		// SIOCGIWRATE
	(iw_handler) NULL,		// SIOCSIWRTS
	(iw_handler) NULL,		// SIOCGIWRTS
	(iw_handler) NULL,		// SIOCSIWFRAG
	(iw_handler) NULL,		// SIOCGIWFRAG
	(iw_handler) NULL,		// SIOCSIWTXPOW
	(iw_handler) NULL,		// SIOCGIWTXPOW
	(iw_handler) NULL,		// SIOCSIWRETRY
	(iw_handler) NULL,		// SIOCGIWRETRY
	(iw_handler) NULL,		// SIOCSIWENCODE
	(iw_handler) NULL,		// SIOCGIWENCODE
	(iw_handler) NULL,		// SIOCSIWPOWER
	(iw_handler) NULL,		// SIOCGIWPOWER

//2008-0409-07, <Add> by Einsn Liu
	(iw_handler) NULL,			// -- hole --
	(iw_handler) NULL,			// -- hole --
	(iw_handler) NULL,    // SIOCSIWGENIE
	(iw_handler) NULL,    // SIOCGIWGENIE
	(iw_handler) NULL,		// SIOCSIWAUTH
	(iw_handler) NULL,		// SIOCGIWAUTH
	(iw_handler) NULL,		// SIOCSIWENCODEEXT
	(iw_handler) NULL,		// SIOCGIWENCODEEXT
	(iw_handler) NULL,				// SIOCSIWPMKSA
	(iw_handler) NULL,				// -- hole --
};


static const iw_handler		iwctl_private_handler[] =
{
	NULL,				// SIOCIWFIRSTPRIV
};


struct iw_priv_args iwctl_private_args[] = {
{ IOCTL_CMD_SET,
  IW_PRIV_TYPE_CHAR | 1024, 0,
  "set"},
};



const struct iw_handler_def	iwctl_handler_def =
{
	.get_wireless_stats = &iwctl_get_wireless_stats,
	.num_standard	= sizeof(iwctl_handler)/sizeof(iw_handler),
//	.num_private	= sizeof(iwctl_private_handler)/sizeof(iw_handler),
//	.num_private_args = sizeof(iwctl_private_args)/sizeof(struct iw_priv_args),
	.num_private	= 0,
	.num_private_args = 0,
	.standard	= (iw_handler *) iwctl_handler,
//	.private	= (iw_handler *) iwctl_private_handler,
//	.private_args	= (struct iw_priv_args *)iwctl_private_args,
	.private	= NULL,
	.private_args	= NULL,
};
