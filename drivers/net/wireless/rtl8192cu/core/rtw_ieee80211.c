/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _IEEE80211_C

#include <drv_types.h>
#include <ieee80211.h>
#include <wifi.h>
#include <osdep_service.h>
#include <wlan_bssdef.h>


//-----------------------------------------------------------
// for adhoc-master to generate ie and provide supported-rate to fw 
//-----------------------------------------------------------

static u8 	WIFI_CCKRATES[] = 
{(IEEE80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK),
 (IEEE80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK),
 (IEEE80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK),
 (IEEE80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK)};

static u8 	WIFI_OFDMRATES[] = 
{(IEEE80211_OFDM_RATE_6MB),
 (IEEE80211_OFDM_RATE_9MB),
 (IEEE80211_OFDM_RATE_12MB),
 (IEEE80211_OFDM_RATE_18MB),
 (IEEE80211_OFDM_RATE_24MB),
 IEEE80211_OFDM_RATE_36MB,
 IEEE80211_OFDM_RATE_48MB,
 IEEE80211_OFDM_RATE_54MB};


int rtw_get_bit_value_from_ieee_value(u8 val)
{
	unsigned char dot11_rate_table[]={2,4,11,22,12,18,24,36,48,72,96,108,0}; // last element must be zero!!

	int i=0;
	while(dot11_rate_table[i] != 0) {
		if (dot11_rate_table[i] == val)
			return BIT(i);
		i++;
	}
	return 0;
}

uint	rtw_is_cckrates_included(u8 *rate)
{	
		u32	i = 0;			

		while(rate[i]!=0)
		{		
			if  (  (((rate[i]) & 0x7f) == 2)	|| (((rate[i]) & 0x7f) == 4) ||		
			(((rate[i]) & 0x7f) == 11)  || (((rate[i]) & 0x7f) == 22) )		
			return _TRUE;	
			i++;
		}
		
		return _FALSE;
}

uint	rtw_is_cckratesonly_included(u8 *rate)
{
	u32 i = 0;


	while(rate[i]!=0)
	{
			if  (  (((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
				(((rate[i]) & 0x7f) != 11)  && (((rate[i]) & 0x7f) != 22) )

			return _FALSE;		

			i++;
	}
	
	return _TRUE;

}

int rtw_check_network_type(unsigned char *rate, int ratelen, int channel)
{
	if (channel > 14)
	{
		if ((rtw_is_cckrates_included(rate)) == _TRUE)
			return WIRELESS_INVALID;
		else
			return WIRELESS_11A;
	}	
	else  // could be pure B, pure G, or B/G
	{
		if ((rtw_is_cckratesonly_included(rate)) == _TRUE)	
			return WIRELESS_11B;
		else if((rtw_is_cckrates_included(rate)) == _TRUE)
			return 	WIRELESS_11BG;
		else
			return WIRELESS_11G;
	}
	
}

u8 *rtw_set_fixed_ie(unsigned char *pbuf, unsigned int len, unsigned char *source,
				unsigned int *frlen)
{
	_rtw_memcpy((void *)pbuf, (void *)source, len);
	*frlen = *frlen + len;
	return (pbuf + len);
}

// rtw_set_ie will update frame length
u8 *rtw_set_ie
(
	u8 *pbuf, 
	sint index, 
	uint len,
	u8 *source, 
	uint *frlen //frame length
)
{
_func_enter_;
	*pbuf = (u8)index;

	*(pbuf + 1) = (u8)len;

	if (len > 0)
		_rtw_memcpy((void *)(pbuf + 2), (void *)source, len);
	
	*frlen = *frlen + (len + 2);
	
	return (pbuf + len + 2);
_func_exit_;	
}



/*----------------------------------------------------------------------------
index: the information element id index, limit is the limit for search
-----------------------------------------------------------------------------*/
u8 *rtw_get_ie(u8 *pbuf, sint index, sint *len, sint limit)
{
	sint tmp,i;
	u8 *p;
_func_enter_;
	if (limit < 1){
		_func_exit_;	
		return NULL;
	}

	p = pbuf;
	i = 0;
	*len = 0;
	while(1)
	{
		if (*p == index)
		{
			*len = *(p + 1);
			return (p);
		}
		else
		{
			tmp = *(p + 1);
			p += (tmp + 2);
			i += (tmp + 2);
		}
		if (i >= limit)
			break;
	}
_func_exit_;		
	return NULL;
}

void rtw_set_supported_rate(u8* SupportedRates, uint mode) 
{
_func_enter_;

	_rtw_memset(SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);
	
	switch (mode)
	{
		case WIRELESS_11B:
			_rtw_memcpy(SupportedRates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
			break;
		
		case WIRELESS_11G:
		case WIRELESS_11A:
		case WIRELESS_11_5N:
		case WIRELESS_11A_5N://Todo: no basic rate for ofdm ?
			_rtw_memcpy(SupportedRates, WIFI_OFDMRATES, IEEE80211_NUM_OFDM_RATESLEN);
			break;
		
		case WIRELESS_11BG:
		case WIRELESS_11G_24N:
		case WIRELESS_11_24N:
		case WIRELESS_11BG_24N:
			_rtw_memcpy(SupportedRates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
			_rtw_memcpy(SupportedRates + IEEE80211_CCK_RATE_LEN, WIFI_OFDMRATES, IEEE80211_NUM_OFDM_RATESLEN);
			break;
	
	}
_func_exit_;	
}

uint	rtw_get_rateset_len(u8	*rateset)
{
	uint i = 0;
_func_enter_;	
	while(1)
	{
		if ((rateset[i]) == 0)
			break;
			
		if (i > 12)
			break;
			
		i++;			
	}
_func_exit_;		
	return i;
}

int rtw_generate_ie(struct registry_priv *pregistrypriv)
{
	u8	wireless_mode;
	int 	sz = 0, rateLen;
	WLAN_BSSID_EX*	pdev_network = &pregistrypriv->dev_network;
	u8*	ie = pdev_network->IEs;
	
_func_enter_;		

	//timestamp will be inserted by hardware
	sz += 8;	
	ie += sz;
	
	//beacon interval : 2bytes
	*(u16*)ie = cpu_to_le16((u16)pdev_network->Configuration.BeaconPeriod);//BCN_INTERVAL;
	sz += 2; 
	ie += 2;
	
	//capability info
	*(u16*)ie = 0;
	
	*(u16*)ie |= cpu_to_le16(cap_IBSS);

	if(pregistrypriv->preamble == PREAMBLE_SHORT)
		*(u16*)ie |= cpu_to_le16(cap_ShortPremble);
	
	if (pdev_network->Privacy)
		*(u16*)ie |= cpu_to_le16(cap_Privacy);
	
	sz += 2;
	ie += 2;
	
	//SSID
	ie = rtw_set_ie(ie, _SSID_IE_, pdev_network->Ssid.SsidLength, pdev_network->Ssid.Ssid, &sz);
	
	//supported rates
	if(pregistrypriv->wireless_mode == WIRELESS_11ABGN)
	{
		if(pdev_network->Configuration.DSConfig > 14)
			wireless_mode = WIRELESS_11A_5N;
		else
			wireless_mode = WIRELESS_11BG_24N;
	}
	else
	{
		wireless_mode = pregistrypriv->wireless_mode;
	}
	
	rtw_set_supported_rate(pdev_network->SupportedRates, wireless_mode) ;
	
	rateLen = rtw_get_rateset_len(pdev_network->SupportedRates);

	if (rateLen > 8)
	{
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, 8, pdev_network->SupportedRates, &sz);
		//ie = rtw_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (pdev_network->SupportedRates + 8), &sz);
	}
	else
	{
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, rateLen, pdev_network->SupportedRates, &sz);
	}

	//DS parameter set
	ie = rtw_set_ie(ie, _DSSET_IE_, 1, (u8 *)&(pdev_network->Configuration.DSConfig), &sz);


	//IBSS Parameter Set
	
	ie = rtw_set_ie(ie, _IBSS_PARA_IE_, 2, (u8 *)&(pdev_network->Configuration.ATIMWindow), &sz);

	if (rateLen > 8)
	{		
		ie = rtw_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (pdev_network->SupportedRates + 8), &sz);
	}
	
		
	//HT Cap.
	if(((pregistrypriv->wireless_mode&WIRELESS_11_5N)||(pregistrypriv->wireless_mode&WIRELESS_11_24N)) 
		&& (pregistrypriv->ht_enable==_TRUE))
	{
		//todo:
	}

	//pdev_network->IELength =  sz; //update IELength

_func_exit_;

	//return _SUCCESS;

	return sz;

}

unsigned char *rtw_get_wpa_ie(unsigned char *pie, int *wpa_ie_len, int limit)
{	
	int len;
	u16 val16;
	unsigned char wpa_oui_type[] = {0x00, 0x50, 0xf2, 0x01};		
	u8 *pbuf = pie;

	while(1) 
	{
		pbuf = rtw_get_ie(pbuf, _WPA_IE_ID_, &len, limit);

		if (pbuf) {

			//check if oui matches...
			if (_rtw_memcmp((pbuf + 2), wpa_oui_type, sizeof (wpa_oui_type)) == _FALSE) {

				goto check_next_ie;
			}

			//check version...
			_rtw_memcpy((u8 *)&val16, (pbuf + 6), sizeof(val16));

			val16 = le16_to_cpu(val16);
			if (val16 != 0x0001)
				goto check_next_ie;	

			*wpa_ie_len = *(pbuf + 1);

			return pbuf;

		}
		else {

			*wpa_ie_len = 0;			
			return NULL;
		}

check_next_ie:

		limit = limit - (pbuf - pie) - 2 - len;

		if (limit <= 0)
			break;

		pbuf += (2 + len);
		
	}
	
	*wpa_ie_len = 0;
	
	return NULL;

}

unsigned char *rtw_get_wpa2_ie(unsigned char *pie, int *rsn_ie_len, int limit)
{	

	return rtw_get_ie(pie, _WPA2_IE_ID_,rsn_ie_len, limit);

}

int rtw_get_wpa_cipher_suite(u8 *s)
{
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_NONE, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_NONE;
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_WEP40, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_WEP40;
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_TKIP, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_TKIP;
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_CCMP, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_CCMP;
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_WEP104, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_WEP104;

	return 0;
}

int rtw_get_wpa2_cipher_suite(u8 *s)
{
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_NONE, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_NONE;
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_WEP40, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_WEP40;
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_TKIP, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_TKIP;
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_CCMP, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_CCMP;
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_WEP104, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_WEP104;

	return 0;
}


int rtw_parse_wpa_ie(u8* wpa_ie, int wpa_ie_len, int *group_cipher, int *pairwise_cipher)
{
	int i, ret=_SUCCESS;
	int left, count;
	u8 *pos;

	if (wpa_ie_len <= 0) {
		/* No WPA IE - fail silently */
		return _FAIL;
	}

	
	if ((*wpa_ie != _WPA_IE_ID_) || (*(wpa_ie+1) != (u8)(wpa_ie_len - 2)) ||
	   (_rtw_memcmp(wpa_ie+2, WPA_OUI_TYPE, WPA_SELECTOR_LEN) != _TRUE) )
	{		
		return _FAIL;
	}

	pos = wpa_ie;

	pos += 8;
	left = wpa_ie_len - 8;	


	//group_cipher
	if (left >= WPA_SELECTOR_LEN) {

		*group_cipher = rtw_get_wpa_cipher_suite(pos);
		
		pos += WPA_SELECTOR_LEN;
		left -= WPA_SELECTOR_LEN;
		
	} 
	else if (left > 0)
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s: ie length mismatch, %u too much", __FUNCTION__, left));
		
		return _FAIL;
	}


	//pairwise_cipher
	if (left >= 2)
	{		
                //count = le16_to_cpu(*(u16*)pos);	
		count = RTW_GET_LE16(pos);
		pos += 2;
		left -= 2;
		
		if (count == 0 || left < count * WPA_SELECTOR_LEN) {
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s: ie count botch (pairwise), "
				   		"count %u left %u", __FUNCTION__, count, left));
			return _FAIL;
		}
		
		for (i = 0; i < count; i++)
		{
			*pairwise_cipher |= rtw_get_wpa_cipher_suite(pos);
			
			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
		
	} 
	else if (left == 1)
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s: ie too short (for key mgmt)",   __FUNCTION__));
		return _FAIL;
	}

	
	return ret;
	
}

int rtw_parse_wpa2_ie(u8* rsn_ie, int rsn_ie_len, int *group_cipher, int *pairwise_cipher)
{
	int i, ret=_SUCCESS;
	int left, count;
	u8 *pos;

	if (rsn_ie_len <= 0) {
		/* No RSN IE - fail silently */
		return _FAIL;
	}


	if ((*rsn_ie!= _WPA2_IE_ID_) || (*(rsn_ie+1) != (u8)(rsn_ie_len - 2)))
	{		
		return _FAIL;
	}
	
	pos = rsn_ie;
	pos += 4;
	left = rsn_ie_len - 4;	

	//group_cipher
	if (left >= RSN_SELECTOR_LEN) {

		*group_cipher = rtw_get_wpa2_cipher_suite(pos);
		
		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
		
	} else if (left > 0) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s: ie length mismatch, %u too much", __FUNCTION__, left));
		return _FAIL;
	}

	//pairwise_cipher
	if (left >= 2)
	{		
	        //count = le16_to_cpu(*(u16*)pos);
		count = RTW_GET_LE16(pos);
		pos += 2;
		left -= 2;

		if (count == 0 || left < count * RSN_SELECTOR_LEN) {
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s: ie count botch (pairwise), "
				  		 "count %u left %u", __FUNCTION__, count, left));
			return _FAIL;
		}
		
		for (i = 0; i < count; i++)
		{			
			*pairwise_cipher |= rtw_get_wpa2_cipher_suite(pos);
			
			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}

	} 
	else if (left == 1)
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s: ie too short (for key mgmt)",  __FUNCTION__));
		
		return _FAIL;
	}


	return ret;
	
}

int rtw_get_sec_ie(u8 *in_ie,uint in_len,u8 *rsn_ie,u16 *rsn_len,u8 *wpa_ie,u16 *wpa_len)
{
	u8 authmode, sec_idx, i;
	u8 wpa_oui[4]={0x0,0x50,0xf2,0x01};
	uint 	cnt;
	
_func_enter_;

	//Search required WPA or WPA2 IE and copy to sec_ie[ ]
	
	cnt = (_TIMESTAMP_ + _BEACON_ITERVAL_ + _CAPABILITY_);
	
	sec_idx=0;
		
	while(cnt<in_len)
	{
		authmode=in_ie[cnt];
		
		if((authmode==_WPA_IE_ID_)&&(_rtw_memcmp(&in_ie[cnt+2], &wpa_oui[0],4)==_TRUE))
		{	
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\n rtw_get_wpa_ie: sec_idx=%d in_ie[cnt+1]+2=%d\n",sec_idx,in_ie[cnt+1]+2));		

				_rtw_memcpy(wpa_ie, &in_ie[cnt],in_ie[cnt+1]+2);

				for(i=0;i<(in_ie[cnt+1]+2);i=i+8){
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\n %2x,%2x,%2x,%2x,%2x,%2x,%2x,%2x\n",wpa_ie[i],wpa_ie[i+1],wpa_ie[i+2],wpa_ie[i+3],wpa_ie[i+4],wpa_ie[i+5],wpa_ie[i+6],wpa_ie[i+7]));
				}

				*wpa_len=in_ie[cnt+1]+2;
				cnt+=in_ie[cnt+1]+2;  //get next
		}
		else
		{
			if(authmode==_WPA2_IE_ID_)
			{
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\n get_rsn_ie: sec_idx=%d in_ie[cnt+1]+2=%d\n",sec_idx,in_ie[cnt+1]+2));		

				_rtw_memcpy(rsn_ie, &in_ie[cnt],in_ie[cnt+1]+2);

				for(i=0;i<(in_ie[cnt+1]+2);i=i+8){
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\n %2x,%2x,%2x,%2x,%2x,%2x,%2x,%2x\n",rsn_ie[i],rsn_ie[i+1],rsn_ie[i+2],rsn_ie[i+3],rsn_ie[i+4],rsn_ie[i+5],rsn_ie[i+6],rsn_ie[i+7]));
				}

				*rsn_len=in_ie[cnt+1]+2;
				cnt+=in_ie[cnt+1]+2;  //get next
			}
			else
			{
				cnt+=in_ie[cnt+1]+2;   //get next
			}	
		}
		
	}
	
_func_exit_;

	return (*rsn_len+*wpa_len);
	
}

u8 rtw_is_wps_ie(u8 *ie_ptr, uint *wps_ielen)
{	
	u8 match = _FALSE;
	u8 eid, wps_oui[4]={0x0,0x50,0xf2,0x04};
	
	if(ie_ptr == NULL) return match;
	
	eid = ie_ptr[0];
	
	if((eid==_WPA_IE_ID_)&&(_rtw_memcmp(&ie_ptr[2], wps_oui, 4)==_TRUE))
	{			
		//printk("==> found WPS_IE.....\n");		
		*wps_ielen = ie_ptr[1]+2;			
		match=_TRUE;
	}	
	return match;
}

u8 *rtw_get_wps_ie(u8 *in_ie, uint in_len, u8 *wps_ie, uint *wps_ielen)
{
	//int match;	
	uint cnt;	
	u8 *wpsie_ptr=NULL;
	u8 eid, wps_oui[4]={0x0,0x50,0xf2,0x04};

	cnt=_FIXED_IE_LENGTH_;	
	//match=_FALSE;
	while(cnt<in_len)
	{
		eid = in_ie[cnt];

		if((eid==_WPA_IE_ID_)&&(_rtw_memcmp(&in_ie[cnt+2], wps_oui, 4)==_TRUE))
		{
			if(wps_ie)
			_rtw_memcpy(wps_ie, &in_ie[cnt], in_ie[cnt+1]+2);
			
			wpsie_ptr = &in_ie[cnt];
			
			if(wps_ielen)
				*wps_ielen = in_ie[cnt+1]+2;
			
			cnt+=in_ie[cnt+1]+2;

			//match = _TRUE;
			break;
		}
		else
		{
			cnt+=in_ie[cnt+1]+2; //goto next	
		}		

	}	

	//return match;
	return wpsie_ptr;

}

static int rtw_ieee802_11_parse_vendor_specific(u8 *pos, uint elen,
					    struct ieee802_11_elems *elems,
					    int show_errors)
{
	unsigned int oui;

	/* first 3 bytes in vendor specific information element are the IEEE
	 * OUI of the vendor. The following byte is used a vendor specific
	 * sub-type. */
	if (elen < 4) {
		if (show_errors) {
			DBG_871X("short vendor specific "
				   "information element ignored (len=%lu)\n",
				   (unsigned long) elen);
		}
		return -1;
	}

	oui = RTW_GET_BE24(pos);
	switch (oui) {
	case OUI_MICROSOFT:
		/* Microsoft/Wi-Fi information elements are further typed and
		 * subtyped */
		switch (pos[3]) {
		case 1:
			/* Microsoft OUI (00:50:F2) with OUI Type 1:
			 * real WPA information element */
			elems->wpa_ie = pos;
			elems->wpa_ie_len = elen;
			break;
		case WME_OUI_TYPE: /* this is a Wi-Fi WME info. element */
			if (elen < 5) {
				DBG_871X("short WME "
					   "information element ignored "
					   "(len=%lu)\n",
					   (unsigned long) elen);
				return -1;
			}
			switch (pos[4]) {
			case WME_OUI_SUBTYPE_INFORMATION_ELEMENT:
			case WME_OUI_SUBTYPE_PARAMETER_ELEMENT:
				elems->wme = pos;
				elems->wme_len = elen;
				break;
			case WME_OUI_SUBTYPE_TSPEC_ELEMENT:
				elems->wme_tspec = pos;
				elems->wme_tspec_len = elen;
				break;
			default:
				DBG_871X("unknown WME "
					   "information element ignored "
					   "(subtype=%d len=%lu)\n",
					   pos[4], (unsigned long) elen);
				return -1;
			}
			break;
		case 4:
			/* Wi-Fi Protected Setup (WPS) IE */
			elems->wps_ie = pos;
			elems->wps_ie_len = elen;
			break;
		default:
			DBG_871X("Unknown Microsoft "
				   "information element ignored "
				   "(type=%d len=%lu)\n",
				   pos[3], (unsigned long) elen);
			return -1;
		}
		break;

	case OUI_BROADCOM:
		switch (pos[3]) {
		case VENDOR_HT_CAPAB_OUI_TYPE:
			elems->vendor_ht_cap = pos;
			elems->vendor_ht_cap_len = elen;
			break;
		default:
			DBG_871X("Unknown Broadcom "
				   "information element ignored "
				   "(type=%d len=%lu)\n",
				   pos[3], (unsigned long) elen);
			return -1;
		}
		break;

	default:
		DBG_871X("unknown vendor specific information "
			   "element ignored (vendor OUI %02x:%02x:%02x "
			   "len=%lu)\n",
			   pos[0], pos[1], pos[2], (unsigned long) elen);
		return -1;
	}

	return 0;
	
}

/**
 * ieee802_11_parse_elems - Parse information elements in management frames
 * @start: Pointer to the start of IEs
 * @len: Length of IE buffer in octets
 * @elems: Data structure for parsed elements
 * @show_errors: Whether to show parsing errors in debug log
 * Returns: Parsing result
 */
ParseRes rtw_ieee802_11_parse_elems(u8 *start, uint len,
				struct ieee802_11_elems *elems,
				int show_errors)
{
	uint left = len;
	u8 *pos = start;
	int unknown = 0;

	_rtw_memset(elems, 0, sizeof(*elems));

	while (left >= 2) {
		u8 id, elen;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left) {
			if (show_errors) {
				DBG_871X("IEEE 802.11 element "
					   "parse failed (id=%d elen=%d "
					   "left=%lu)\n",
					   id, elen, (unsigned long) left);				
			}
			return ParseFailed;
		}

		switch (id) {
		case WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_FH_PARAMS:
			elems->fh_params = pos;
			elems->fh_params_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			elems->ds_params = pos;
			elems->ds_params_len = elen;
			break;
		case WLAN_EID_CF_PARAMS:
			elems->cf_params = pos;
			elems->cf_params_len = elen;
			break;
		case WLAN_EID_TIM:
			elems->tim = pos;
			elems->tim_len = elen;
			break;
		case WLAN_EID_IBSS_PARAMS:
			elems->ibss_params = pos;
			elems->ibss_params_len = elen;
			break;
		case WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case WLAN_EID_ERP_INFO:
			elems->erp_info = pos;
			elems->erp_info_len = elen;
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		case WLAN_EID_VENDOR_SPECIFIC:
			if (rtw_ieee802_11_parse_vendor_specific(pos, elen,
							     elems,
							     show_errors))
				unknown++;
			break;
		case WLAN_EID_RSN:
			elems->rsn_ie = pos;
			elems->rsn_ie_len = elen;
			break;
		case WLAN_EID_PWR_CAPABILITY:
			elems->power_cap = pos;
			elems->power_cap_len = elen;
			break;
		case WLAN_EID_SUPPORTED_CHANNELS:
			elems->supp_channels = pos;
			elems->supp_channels_len = elen;
			break;
		case WLAN_EID_MOBILITY_DOMAIN:
			elems->mdie = pos;
			elems->mdie_len = elen;
			break;
		case WLAN_EID_FAST_BSS_TRANSITION:
			elems->ftie = pos;
			elems->ftie_len = elen;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			elems->timeout_int = pos;
			elems->timeout_int_len = elen;
			break;
		case WLAN_EID_HT_CAP:
			elems->ht_capabilities = pos;
			elems->ht_capabilities_len = elen;
			break;
		case WLAN_EID_HT_OPERATION:
			elems->ht_operation = pos;
			elems->ht_operation_len = elen;
			break;
		default:
			unknown++;
			if (!show_errors)
				break;
			DBG_871X("IEEE 802.11 element parse "
				   "ignored unknown element (id=%d elen=%d)\n",
				   id, elen);
			break;
		}

		left -= elen;
		pos += elen;
	}

	if (left)
		return ParseFailed;

	return unknown ? ParseUnknown : ParseOK;
	
}

u8 key_char2num(u8 ch)
{
    if((ch>='0')&&(ch<='9'))
        return ch - '0';
    else if ((ch>='a')&&(ch<='f'))
        return ch - 'a' + 10;
    else if ((ch>='A')&&(ch<='F'))
        return ch - 'A' + 10;
    else
	 return 0xff;
}

u8 str_2char2num(u8 hch, u8 lch)
{
    return ((key_char2num(hch) * 10 ) + key_char2num(lch));
}

u8 key_2char2num(u8 hch, u8 lch)
{
    return ((key_char2num(hch) << 4) | key_char2num(lch));
}

extern char* rtw_initmac;
void rtw_macaddr_cfg(u8 *mac_addr)
{
	u8 mac[ETH_ALEN];
	if(mac_addr == NULL)	return;
	
	if ( rtw_initmac )
	{	//	Users specify the mac address
		int jj,kk;

		for( jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3 )
		{
			mac[jj] = key_2char2num(rtw_initmac[kk], rtw_initmac[kk+ 1]);
		}
		_rtw_memcpy(mac_addr, mac, ETH_ALEN);
	}
	else
	{	//	Use the mac address stored in the Efuse
		_rtw_memcpy(mac, mac_addr, ETH_ALEN);
	}
	
	if (((mac[0]==0xff) &&(mac[1]==0xff) && (mac[2]==0xff) &&
	     (mac[3]==0xff) && (mac[4]==0xff) &&(mac[5]==0xff)) ||
	    ((mac[0]==0x0) && (mac[1]==0x0) && (mac[2]==0x0) &&
	     (mac[3]==0x0) && (mac[4]==0x0) &&(mac[5]==0x0)))
	{
		mac[0] = 0x00;
		mac[1] = 0xe0;
		mac[2] = 0x4c;
		mac[3] = 0x87;
		mac[4] = 0x00;
		mac[5] = 0x00;
		// use default mac addresss
		_rtw_memcpy(mac_addr, mac, ETH_ALEN);
		DBG_8192C("MAC Address from efuse error, assign default one !!!\n");
	}	

	DBG_8192C("rtw_macaddr_cfg MAC Address  = "MAC_FMT"\n", MAC_ARG(mac_addr));
}

#ifdef CONFIG_P2P
//	Added by Albert 20100910
//	The input buffer in_ie should be pointer to the address of any information element of management frame.
//	The p2p_ie buffer will contain "whole" the P2P IE, include the Element ID, Length, P2P OUI and P2P Attributes.
//	The p2p_ielen will be the length of p2p_ie buffer.
//	The return value will be the offset for the next IE.

int rtw_get_p2p_ie(u8 *in_ie, uint in_len, u8 *p2p_ie, uint *p2p_ielen)
{
	int match;
	uint cnt = 0;	
	u8 eid, p2p_oui[4]={0x50,0x6F,0x9A,0x09};


	match=_FALSE;
	while(cnt<in_len)
	{
		eid = in_ie[cnt];
		
		if( ( eid == _VENDOR_SPECIFIC_IE_ ) && ( _rtw_memcmp( &in_ie[cnt+2], p2p_oui, 4) == _TRUE ) )
		{
			if ( p2p_ie != NULL )
			{
				_rtw_memcpy( p2p_ie, &in_ie[ cnt ], in_ie[ cnt + 1 ] + 2 );
				if ( p2p_ielen != NULL )
				{
					*p2p_ielen = in_ie[ cnt + 1 ] + 2;
				}
			}
			else
			{
				if ( p2p_ielen != NULL )
				{
					*p2p_ielen = 0;
				}
			}
			
			cnt += in_ie[ cnt + 1 ] + 2;

			match = _TRUE;
			break;
		}
		else
		{
			cnt += in_ie[ cnt + 1 ] +2; //goto next	
		}		
		
	}	

	if ( match == _TRUE )
	{
		match = cnt;
	}
	
	return match;

}

//	attr_content: The output buffer, contains the "body field" of P2P attribute.
//	attr_contentlen: The data length of the "body field" of P2P attribute.
int rtw_get_p2p_attr_content(u8 *p2p_ie, uint p2p_ielen, u8 target_attr_id ,u8 *attr_content, uint *attr_contentlen)
{
	int match;
	uint cnt = 0;	
	u8 attr_id, p2p_oui[4]={0x50,0x6F,0x9A,0x09};


	match=_FALSE;

	if ( ( p2p_ie[ 0 ] != _VENDOR_SPECIFIC_IE_ ) ||
		( _rtw_memcmp( p2p_ie + 2, p2p_oui , 4 ) != _TRUE ) )
	{
		return( match );
	}

	//	1 ( P2P IE ) + 1 ( Length ) + 3 ( OUI ) + 1 ( OUI Type )
	cnt = 6;
	while( cnt < p2p_ielen )
	{
		//u16	attrlen = le16_to_cpu(*(u16*)(p2p_ie + cnt + 1 ));
		u16 attrlen = RTW_GET_LE16(p2p_ie + cnt + 1);
		
		attr_id = p2p_ie[cnt];
		if( attr_id == target_attr_id )
		{
			//	3 -> 1 byte for attribute ID field, 2 bytes for length field
			if(attr_content)
			_rtw_memcpy( attr_content, &p2p_ie[ cnt + 3 ], attrlen );
			
			if(attr_contentlen)
			*attr_contentlen = attrlen;
			
			cnt += attrlen + 3;

			match = _TRUE;
			break;
		}
		else
		{
			cnt += attrlen + 3; //goto next	
		}		
		
	}	

	return match;

}

u32 rtw_set_p2p_attr_content(u8 *pbuf, u8 attr_id, u16 attr_len, u8 *pdata_attr)
{	
	u32 a_len;

	*pbuf = attr_id;
		
	//*(u16*)(pbuf + 1) = cpu_to_le16(attr_len);
	RTW_PUT_LE16(pbuf + 1, attr_len);

	if(pdata_attr)
		_rtw_memcpy(pbuf + 3, pdata_attr, attr_len);		
		
	a_len = attr_len + 3;
		
	return a_len;
}

//	Noted by Albert 20100910
//	1. WPS uses the TLV format to store the attribute contents.
//	T: Type, 2bytes length
//	L: Length, 2bytes length
//	V: Value, variable length
//	2. WPS uses the big endian to store the WPS attribute contents.

//	attr_content: The output buffer, contains the "value part" of WPS attribute field.
//	attr_contentlen: The data length of the "value part" of WPS attribute field.

int rtw_get_wps_attr_content(u8 *wps_ie, uint wps_ielen, u16 target_attr_id ,u8 *attr_content, uint *attr_contentlen)
{
	int match;
	uint cnt = 0;	
	u8 wps_oui[4]={0x00,0x50,0xF2,0x04};
	u16	attr_id;


	match=_FALSE;

	if ( ( wps_ie[ 0 ] != _VENDOR_SPECIFIC_IE_ ) ||
		( _rtw_memcmp( wps_ie + 2, wps_oui , 4 ) != _TRUE ) )
	{
		return( match );
	}

	//	1 ( WPS IE ) + 1 ( Length ) + 4 ( WPS OUI )
	cnt = 6;
	while( cnt < wps_ielen )
	{
		//	2 -> the length of WPS attribute type field.
		//u16	attrlen = be16_to_cpu(*(u16*)(wps_ie + cnt + 2 ));
		u16	attrlen = RTW_GET_BE16(wps_ie + cnt + 2);
		
		//attr_id = be16_to_cpu( *(u16*) ( wps_ie + cnt ) );
		attr_id = RTW_GET_BE16(wps_ie + cnt);
		if( attr_id == target_attr_id )
		{
			//	3 -> 1 byte for attribute ID field, 2 bytes for length field
			_rtw_memcpy( attr_content, &wps_ie[ cnt + 4 ], attrlen );
			
			*attr_contentlen = attrlen;
			
			cnt += attrlen + 4;

			match = _TRUE;
			break;
		}
		else
		{
			cnt += attrlen + 4; //goto next	
		}		
		
	}	

	return match;

}

//	Commented by Albert 20110520
//	This function is only used by P2P
int rtw_get_wps_ie_p2p(u8 *in_ie, uint in_len, u8 *wps_ie, uint *wps_ielen)
{
	int match;
	uint cnt = 0;
	u8 eid, wps_oui[4]={0x0,0x50,0xf2,0x04};

	match=_FALSE;
	while(cnt<in_len)
	{
		eid = in_ie[cnt];
		
		if((eid==_WPA_IE_ID_)&&(_rtw_memcmp(&in_ie[cnt+2], wps_oui, 4)==_TRUE))
		{
			if ( wps_ie != NULL )
			{
				_rtw_memcpy(wps_ie, &in_ie[cnt], in_ie[cnt+1]+2);
			}
			
			*wps_ielen = in_ie[cnt+1]+2;
			
			cnt+=in_ie[cnt+1]+2;

			match = _TRUE;
			break;
		}
		else
		{
			cnt+=in_ie[cnt+1]+2; //goto next	
		}		
		
	}	

	return match;

}
#endif // CONFIG_P2P

