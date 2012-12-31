/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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

u8 	WIFI_CCKRATES[] = 
{(IEEE80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK),
 (IEEE80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK),
 (IEEE80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK),
 (IEEE80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK)};

u8 	WIFI_OFDMRATES[] = 
{(IEEE80211_OFDM_RATE_6MB),
 (IEEE80211_OFDM_RATE_9MB),
 (IEEE80211_OFDM_RATE_12MB),
 (IEEE80211_OFDM_RATE_18MB),
 (IEEE80211_OFDM_RATE_24MB),
 IEEE80211_OFDM_RATE_36MB,
 IEEE80211_OFDM_RATE_48MB,
 IEEE80211_OFDM_RATE_54MB};


int get_bit_value_from_ieee_value(u8 val)
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

uint	is_cckrates_included(u8 *rate)
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

uint	is_cckratesonly_included(u8 *rate)
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

int check_network_type(unsigned char *rate, int ratelen, int channel)
{
	if (channel > 14)
	{
		if ((is_cckrates_included(rate)) == _TRUE)
			return WIRELESS_INVALID;
		else
			return WIRELESS_11A;
	}	
	else  // could be pure B, pure G, or B/G
	{
		if ((is_cckratesonly_included(rate)) == _TRUE)	
			return WIRELESS_11B;
		else if((is_cckrates_included(rate)) == _TRUE)
			return 	WIRELESS_11BG;
		else
			return WIRELESS_11G;
	}
	
}

u8 *set_fixed_ie(unsigned char *pbuf, unsigned int len, unsigned char *source,
				unsigned int *frlen)
{
	_memcpy((void *)pbuf, (void *)source, len);
	*frlen = *frlen + len;
	return (pbuf + len);
}

// set_ie will update frame length
u8 *set_ie
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
		_memcpy((void *)(pbuf + 2), (void *)source, len);
	
	*frlen = *frlen + (len + 2);
	
	return (pbuf + len + 2);
_func_exit_;	
}



/*----------------------------------------------------------------------------
index: the information element id index, limit is the limit for search
-----------------------------------------------------------------------------*/
u8 *get_ie(u8 *pbuf, sint index, sint *len, sint limit)
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

void set_supported_rate(u8* SupportedRates, uint mode) 
{
_func_enter_;

	_memset(SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);
	
	switch (mode)
	{
		case WIRELESS_11B:
			_memcpy(SupportedRates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
			break;
		
		case WIRELESS_11G:
		case WIRELESS_11A:	
			_memcpy(SupportedRates, WIFI_OFDMRATES, IEEE80211_NUM_OFDM_RATESLEN);
			break;
		
		case WIRELESS_11BG:
			_memcpy(SupportedRates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
			_memcpy(SupportedRates + IEEE80211_CCK_RATE_LEN, WIFI_OFDMRATES, IEEE80211_NUM_OFDM_RATESLEN);
			break;
	
	}
_func_exit_;	
}

uint	get_rateset_len(u8	*rateset)
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

int generate_ie(struct registry_priv *pregistrypriv)
{
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
	ie = set_ie(ie, _SSID_IE_, pdev_network->Ssid.SsidLength, pdev_network->Ssid.Ssid, &sz);
	
	//supported rates
	set_supported_rate(pdev_network->SupportedRates, pregistrypriv->wireless_mode) ;
	
	rateLen = get_rateset_len(pdev_network->SupportedRates);

	if (rateLen > 8)
	{
		ie = set_ie(ie, _SUPPORTEDRATES_IE_, 8, pdev_network->SupportedRates, &sz);
		ie = set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (pdev_network->SupportedRates + 8), &sz);
	}
	else
	{
		ie = set_ie(ie, _SUPPORTEDRATES_IE_, rateLen, pdev_network->SupportedRates, &sz);
	}

	//DS parameter set
	ie = set_ie(ie, _DSSET_IE_, 1, (u8 *)&(pdev_network->Configuration.DSConfig), &sz);


	//IBSS Parameter Set
	
	ie = set_ie(ie, _IBSS_PARA_IE_, 2, (u8 *)&(pdev_network->Configuration.ATIMWindow), &sz);

	//pdev_network->IELength =  sz; //update IELength

_func_exit_;		

	//return _SUCCESS;
	return sz;
	
}

unsigned char *get_wpa_ie(unsigned char *pie, int *wpa_ie_len, int limit)
{	
	int len;
	u16 val16;
	unsigned char wpa_oui_type[] = {0x00, 0x50, 0xf2, 0x01};		
	u8 *pbuf = pie;

	while(1) 
	{
		pbuf = get_ie(pbuf, _WPA_IE_ID_, &len, limit);

		if (pbuf) {

			//check if oui matches...
			if (_memcmp((pbuf + 2), wpa_oui_type, sizeof (wpa_oui_type)) == _FALSE) {

				goto check_next_ie;
			}

			//check version...
			_memcpy((u8 *)&val16, (pbuf + 6), sizeof(val16));

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

unsigned char *get_wpa2_ie(unsigned char *pie, int *rsn_ie_len, int limit)
{	

	return get_ie(pie, _WPA2_IE_ID_,rsn_ie_len, limit);

}

int get_wpa_cipher_suite(u8 *s)
{
	if (_memcmp(s, WPA_CIPHER_SUITE_NONE, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_NONE;
	if (_memcmp(s, WPA_CIPHER_SUITE_WEP40, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_WEP40;
	if (_memcmp(s, WPA_CIPHER_SUITE_TKIP, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_TKIP;
	if (_memcmp(s, WPA_CIPHER_SUITE_CCMP, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_CCMP;
	if (_memcmp(s, WPA_CIPHER_SUITE_WEP104, WPA_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_WEP104;

	return 0;
}

int get_wpa2_cipher_suite(u8 *s)
{
	if (_memcmp(s, RSN_CIPHER_SUITE_NONE, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_NONE;
	if (_memcmp(s, RSN_CIPHER_SUITE_WEP40, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_WEP40;
	if (_memcmp(s, RSN_CIPHER_SUITE_TKIP, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_TKIP;
	if (_memcmp(s, RSN_CIPHER_SUITE_CCMP, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_CCMP;
	if (_memcmp(s, RSN_CIPHER_SUITE_WEP104, RSN_SELECTOR_LEN) == _TRUE)
		return WPA_CIPHER_WEP104;

	return 0;
}


int parse_wpa_ie(u8* wpa_ie, int wpa_ie_len, int *group_cipher, int *pairwise_cipher)
{
	int i, ret=_SUCCESS;
	int left, count;
	u8 *pos;

	if (wpa_ie_len <= 0) {
		/* No WPA IE - fail silently */
		return _FAIL;
	}

	
	if ((*wpa_ie != _WPA_IE_ID_) || (*(wpa_ie+1) != (u8)(wpa_ie_len - 2)) ||
	   (_memcmp(wpa_ie+2, WPA_OUI_TYPE, WPA_SELECTOR_LEN) != _TRUE) )
	{		
		return _FAIL;
	}

	pos = wpa_ie;

	pos += 8;
	left = wpa_ie_len - 8;	


	//group_cipher
	if (left >= WPA_SELECTOR_LEN) {

		*group_cipher = get_wpa_cipher_suite(pos);
		
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
		count = le16_to_cpu(*(u16*)pos);//			
		pos += 2;
		left -= 2;
		
		if (count == 0 || left < count * WPA_SELECTOR_LEN) {
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s: ie count botch (pairwise), "
				   		"count %u left %u", __FUNCTION__, count, left));
			return _FAIL;
		}
		
		for (i = 0; i < count; i++)
		{
			*pairwise_cipher |= get_wpa_cipher_suite(pos);
			
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

int parse_wpa2_ie(u8* rsn_ie, int rsn_ie_len, int *group_cipher, int *pairwise_cipher)
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

		*group_cipher = get_wpa2_cipher_suite(pos);
		
		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
		
	} else if (left > 0) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s: ie length mismatch, %u too much", __FUNCTION__, left));
		return _FAIL;
	}

	//pairwise_cipher
	if (left >= 2)
	{		
		count = le16_to_cpu(*(u16*)pos);//			
		pos += 2;
		left -= 2;

		if (count == 0 || left < count * RSN_SELECTOR_LEN) {
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s: ie count botch (pairwise), "
				  		 "count %u left %u", __FUNCTION__, count, left));
			return _FAIL;
		}
		
		for (i = 0; i < count; i++)
		{			
			*pairwise_cipher |= get_wpa2_cipher_suite(pos);
			
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

int get_sec_ie(u8 *in_ie,uint in_len,u8 *rsn_ie,u16 *rsn_len,u8 *wpa_ie,u16 *wpa_len)
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
		
		if((authmode==_WPA_IE_ID_)&&(_memcmp(&in_ie[cnt+2], &wpa_oui[0],4)==_TRUE))
		{	
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\n get_wpa_ie: sec_idx=%d in_ie[cnt+1]+2=%d\n",sec_idx,in_ie[cnt+1]+2));		

				_memcpy(wpa_ie, &in_ie[cnt],in_ie[cnt+1]+2);

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

				_memcpy(rsn_ie, &in_ie[cnt],in_ie[cnt+1]+2);

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

/**
 * rtw_get_wps_ie - Search WPS IE from a series of IEs
 * @in_ie: Address of IEs to search
 * @in_len: Length limit from in_ie
 * @wps_ie: If not NULL and WPS IE is found, WPS IE will be copied to the buf starting from wps_ie
 * @wps_ielen: If not NULL and WPS IE is found, will set to the length of the entire WPS IE
 *
 * Returns: The address of the WPS IE found, or NULL
 */
u8 *get_wps_ie(u8 *in_ie, uint in_len, u8 *wps_ie, uint *wps_ielen)
{
	uint cnt;
	u8 *wpsie_ptr=NULL;
	u8 eid, wps_oui[4]={0x0,0x50,0xf2,0x04};

	if(wps_ielen)
		*wps_ielen = 0;

	if(!in_ie || in_len<=0)
		return wpsie_ptr;

	cnt = 0;

	while(cnt<in_len)
	{
		eid = in_ie[cnt];

		if((eid==_WPA_IE_ID_)&&(_memcmp(&in_ie[cnt+2], wps_oui, 4)==_TRUE))
		{
			wpsie_ptr = &in_ie[cnt];

			if(wps_ie)
				_memcpy(wps_ie, &in_ie[cnt], in_ie[cnt+1]+2);
			
			if(wps_ielen)
				*wps_ielen = in_ie[cnt+1]+2;
			
			cnt+=in_ie[cnt+1]+2;

			break;
		}
		else
		{
			cnt+=in_ie[cnt+1]+2; //goto next	
		}		

	}	

	return wpsie_ptr;
}
