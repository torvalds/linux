/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#define RTMP_MODULE_OS

#include "rtmp_comm.h"
#include "rt_os_util.h"
#include "rt_os_net.h"
/* #include "rt_config.h" */

#ifdef DBG
extern ULONG    RTDebugLevel;
#endif

#define NR_WEP_KEYS 				4
#define WEP_SMALL_KEY_LEN 			(40/8)
#define WEP_LARGE_KEY_LEN 			(104/8)

#define GROUP_KEY_NO                4

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#define IWE_STREAM_ADD_EVENT(_A, _B, _C, _D, _E)		iwe_stream_add_event(_A, _B, _C, _D, _E)
#define IWE_STREAM_ADD_POINT(_A, _B, _C, _D, _E)		iwe_stream_add_point(_A, _B, _C, _D, _E)
#define IWE_STREAM_ADD_VALUE(_A, _B, _C, _D, _E, _F)	iwe_stream_add_value(_A, _B, _C, _D, _E, _F)
#else
#define IWE_STREAM_ADD_EVENT(_A, _B, _C, _D, _E)		iwe_stream_add_event(_B, _C, _D, _E)
#define IWE_STREAM_ADD_POINT(_A, _B, _C, _D, _E)		iwe_stream_add_point(_B, _C, _D, _E)
#define IWE_STREAM_ADD_VALUE(_A, _B, _C, _D, _E, _F)	iwe_stream_add_value(_B, _C, _D, _E, _F)
#endif

extern UCHAR    CipherWpa2Template[];

typedef struct GNU_PACKED _RT_VERSION_INFO{
    UCHAR       DriverVersionW;
    UCHAR       DriverVersionX;
    UCHAR       DriverVersionY;
    UCHAR       DriverVersionZ;
    UINT        DriverBuildYear;
    UINT        DriverBuildMonth;
    UINT        DriverBuildDay;
} RT_VERSION_INFO, *PRT_VERSION_INFO;

struct iw_priv_args privtab[] = {
{ RTPRIV_IOCTL_SET, 
  IW_PRIV_TYPE_CHAR | 1024, 0,
  "set"},

{ RTPRIV_IOCTL_SHOW, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,
  ""},
/* --- sub-ioctls definitions --- */   
    { SHOW_CONN_STATUS,
	  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "connStatus" },
	{ SHOW_DRVIER_VERION,
	  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "driverVer" },
    { SHOW_BA_INFO,
	  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "bainfo" },
	{ SHOW_DESC_INFO,
	  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "descinfo" },
    { RAIO_OFF,
	  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "radio_off" },
	{ RAIO_ON,
	  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "radio_on" },
#ifdef QOS_DLS_SUPPORT
	{ SHOW_DLS_ENTRY_INFO,
	  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "dlsentryinfo" },
#endif /* QOS_DLS_SUPPORT */
	{ SHOW_CFG_VALUE,
	  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "show" },
	{ SHOW_ADHOC_ENTRY_INFO,
	  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "adhocEntry" },
/* --- sub-ioctls relations --- */

#ifdef DBG
{ RTPRIV_IOCTL_BBP,
  IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,
  "bbp"},
{ RTPRIV_IOCTL_MAC,
  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024,
  "mac"},  
#ifdef RTMP_RF_RW_SUPPORT
{ RTPRIV_IOCTL_RF,
  IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,
  "rf"},
#endif /* RTMP_RF_RW_SUPPORT */
{ RTPRIV_IOCTL_E2P,
  IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024,
  "e2p"},
#endif  /* DBG */

{ RTPRIV_IOCTL_STATISTICS,
  0, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,
  "stat"}, 
{ RTPRIV_IOCTL_GSITESURVEY,
  0, IW_PRIV_TYPE_CHAR | 1024,
  "get_site_survey"},

};

extern INT32 ralinkrate[];
extern UINT32 RT_RateSize;




/*
This is required for LinEX2004/kernel2.6.7 to provide iwlist scanning function
*/

int
rt_ioctl_giwname(struct net_device *dev,
		   struct iw_request_info *info,
		   char *name, char *extra)
{
	strncpy(name, "Ralink STA", IFNAMSIZ);
	return 0;
}

int rt_ioctl_siwfreq(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_freq *freq, char *extra)
{
	VOID *pAd = NULL;
/*	int 	chan = -1; */
	RT_CMD_STA_IOCTL_FREQ IoctlFreq, *pIoctlFreq = &IoctlFreq;

	GET_PAD_FROM_NET_DEV(pAd, dev);

    /*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
        DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;   
    }


	if (freq->e > 1)
		return -EINVAL;

    
	pIoctlFreq->m = freq->m;
	pIoctlFreq->e = freq->e;

	if (RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWFREQ, 0,
							pIoctlFreq, 0, dev->priv_flags) != NDIS_STATUS_SUCCESS)
		return -EINVAL;

	return 0;
}


int rt_ioctl_giwfreq(struct net_device *dev,
		   struct iw_request_info *info,
		   struct iw_freq *freq, char *extra)
{
	VOID *pAd = NULL;
/*	UCHAR ch; */
	ULONG	m = 2412000;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	/*check if the interface is down */
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;   
	}

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWFREQ, 0,
						&m, dev->priv_flags, dev->priv_flags);

	freq->m = m * 100;
	freq->e = 1;
	freq->i = 0;
	
	return 0;
}


int rt_ioctl_siwmode(struct net_device *dev,
		   struct iw_request_info *info,
		   __u32 *mode, char *extra)
{
	VOID *pAd = NULL;
	LONG Mode;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
    	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
       	return -ENETDOWN;   
    }


	if (*mode == IW_MODE_ADHOC)
		Mode = RTMP_CMD_STA_MODE_ADHOC;
	else if (*mode == IW_MODE_INFRA)
		Mode = RTMP_CMD_STA_MODE_INFRA;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,4,20))
	else if (*mode == IW_MODE_MONITOR)
		Mode = RTMP_CMD_STA_MODE_MONITOR;
#endif /* LINUX_VERSION_CODE */
	else
	{
		DBGPRINT(RT_DEBUG_TRACE, ("===>rt_ioctl_siwmode::SIOCSIWMODE (unknown %d)\n", *mode));
		return -EINVAL;
	}

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWMODE, 0, NULL, Mode, dev->priv_flags);
	return 0;
}


int rt_ioctl_giwmode(struct net_device *dev,
		   struct iw_request_info *info,
		   __u32 *mode, char *extra)
{
	VOID *pAd = NULL;
	ULONG Mode;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
        	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        	return -ENETDOWN;   
	}


	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWMODE, 0, &Mode, 0, dev->priv_flags);

	if (Mode == RTMP_CMD_STA_MODE_ADHOC)
		*mode = IW_MODE_ADHOC;
	else if (Mode == RTMP_CMD_STA_MODE_INFRA)
		*mode = IW_MODE_INFRA;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,4,20))
	else if (Mode == RTMP_CMD_STA_MODE_MONITOR)
		*mode = IW_MODE_MONITOR;
#endif /* LINUX_VERSION_CODE */
	else
		*mode = IW_MODE_AUTO;

	DBGPRINT(RT_DEBUG_TRACE, ("==>rt_ioctl_giwmode(mode=%d)\n", *mode));
	return 0;
}

int rt_ioctl_siwsens(struct net_device *dev,
		   struct iw_request_info *info,
		   char *name, char *extra)
{
	VOID *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
/*    	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    	{
        	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        	return -ENETDOWN;   
    	}

	return 0;
}

int rt_ioctl_giwsens(struct net_device *dev,
		   struct iw_request_info *info,
		   char *name, char *extra)
{
	return 0;
}

int rt_ioctl_giwrange(struct net_device *dev,
		   struct iw_request_info *info,
		   struct iw_point *data, char *extra)
{
	VOID *pAd = NULL;
	struct iw_range *range = (struct iw_range *) extra;
	u16 val;
	int i;
	ULONG Mode, ChannelListNum;
	UCHAR *pChannel;
	UINT32 *pFreq;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

#ifndef NATIVE_WPA_SUPPLICANT_SUPPORT
#ifndef RT_CFG80211_SUPPORT
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
    	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
    	return -ENETDOWN;   
	}
#endif /* RT_CFG80211_SUPPORT */
#endif /* NATIVE_WPA_SUPPLICANT_SUPPORT */

	DBGPRINT(RT_DEBUG_TRACE ,("===>rt_ioctl_giwrange\n"));
	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->txpower_capa = IW_TXPOW_DBM;

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWMODE, 0, &Mode, 0, dev->priv_flags);
/*	if (INFRA_ON(pAd)||ADHOC_ON(pAd)) */
	if ((Mode == RTMP_CMD_STA_MODE_INFRA) || (Mode == RTMP_CMD_STA_MODE_ADHOC))
	{
		range->min_pmp = 1 * 1024;
		range->max_pmp = 65535 * 1024;
		range->min_pmt = 1 * 1024;
		range->max_pmt = 1000 * 1024;
		range->pmp_flags = IW_POWER_PERIOD;
		range->pmt_flags = IW_POWER_TIMEOUT;
		range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT |
			IW_POWER_UNICAST_R | IW_POWER_ALL_R;
	}

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 14;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;

/*	range->num_channels =  pAd->ChannelListNum; */
	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_CHAN_LIST_NUM_GET, 0, &ChannelListNum, 0, dev->priv_flags);
	range->num_channels = ChannelListNum;

	os_alloc_mem(NULL, (UCHAR **)&pChannel, sizeof(UCHAR)*ChannelListNum);
	if (pChannel == NULL)
		return -ENOMEM;
	os_alloc_mem(NULL, (UCHAR **)&pFreq, sizeof(UINT32)*ChannelListNum);
	if (pFreq == NULL)
	{
		os_free_mem(NULL, pChannel);
		return -ENOMEM;
	}

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_CHAN_LIST_GET, 0, pChannel, 0, dev->priv_flags);
	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_FREQ_LIST_GET, 0, pFreq, 0, dev->priv_flags);

	val = 0;
	for (i = 1; i <= range->num_channels; i++) 
	{
/*		u32 m = 2412000; */
		range->freq[val].i = pChannel[i-1];
/*		MAP_CHANNEL_ID_TO_KHZ(pAd->ChannelList[i-1].Channel, m); */
		range->freq[val].m = pFreq[i-1] * 100; /* OS_HZ */
		
		range->freq[val].e = 1;
		val++;
		if (val == IW_MAX_FREQUENCIES)
			break;
	}
	os_free_mem(NULL, pChannel);
	os_free_mem(NULL, pFreq);

	range->num_frequency = val;

	range->max_qual.qual = 100; /* what is correct max? This was not
					* documented exactly. At least
					* 69 has been observed. */
	range->max_qual.level = 0; /* dB */
	range->max_qual.noise = 0; /* dB */

	/* What would be suitable values for "average/typical" qual? */
	range->avg_qual.qual = 20;
	range->avg_qual.level = -60;
	range->avg_qual.noise = -95;
	range->sensitivity = 3;

	range->max_encoding_tokens = NR_WEP_KEYS;
	range->num_encoding_sizes = 2;
	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;

	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

#if WIRELESS_EXT > 17
	/* IW_ENC_CAPA_* bit field */
	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 | 
					IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;
#endif

	return 0;
}

int rt_ioctl_siwap(struct net_device *dev,
		      struct iw_request_info *info,
		      struct sockaddr *ap_addr, char *extra)
{
	VOID *pAd = NULL;
    UCHAR Bssid[6];

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;   
	}


	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWAP, 0,
						(VOID *)(ap_addr->sa_data), 0, dev->priv_flags);

	memcpy(Bssid, ap_addr->sa_data, MAC_ADDR_LEN);

	DBGPRINT(RT_DEBUG_TRACE, ("IOCTL::SIOCSIWAP %02x:%02x:%02x:%02x:%02x:%02x\n",
	Bssid[0], Bssid[1], Bssid[2], Bssid[3], Bssid[4], Bssid[5]));

	return 0;
}

int rt_ioctl_giwap(struct net_device *dev,
		      struct iw_request_info *info,
		      struct sockaddr *ap_addr, char *extra)
{
	VOID *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
        DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;   
    }


	if (RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWAP, 0,
						(VOID *)(ap_addr->sa_data), 0, dev->priv_flags) != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("IOCTL::SIOCGIWAP(=EMPTY)\n"));
		return -ENOTCONN;
	}
	ap_addr->sa_family = ARPHRD_ETHER;

	return 0;
}

/*
 * Units are in db above the noise floor. That means the
 * rssi values reported in the tx/rx descriptors in the
 * driver are the SNR expressed in db.
 *
 * If you assume that the noise floor is -95, which is an
 * excellent assumption 99.5 % of the time, then you can
 * derive the absolute signal level (i.e. -95 + rssi). 
 * There are some other slight factors to take into account
 * depending on whether the rssi measurement is from 11b,
 * 11g, or 11a.   These differences are at most 2db and
 * can be documented.
 *
 * NB: various calculations are based on the orinoco/wavelan
 *     drivers for compatibility
 */
static void set_quality(VOID *pAd,
                        struct iw_quality *iq, 
						RT_CMD_STA_IOCTL_BSS *pBss)
/*                        PBSS_ENTRY pBssEntry) */
{

	iq->qual = pBss->ChannelQuality;
	iq->level = (__u8)(pBss->Rssi);
	iq->noise = pBss->Noise;

/*    iq->updated = pAd->iw_stats.qual.updated; */
/*	iq->updated = ((struct iw_statistics *)(pAd->iw_stats))->qual.updated; */
	iq->updated = 1;     /* Flags to know if updated */

#if WIRELESS_EXT >= 17
	iq->updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_UPDATED;
#endif

#if WIRELESS_EXT >= 19
	iq->updated |= IW_QUAL_DBM;	/* Level + Noise are dBm */
#endif
}

int rt_ioctl_iwaplist(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *data, char *extra)
{
 	VOID *pAd = NULL;	

/*	struct sockaddr addr[IW_MAX_AP]; */
	struct sockaddr *addr = NULL;
	struct iw_quality qual[IW_MAX_AP];
	int i;
	RT_CMD_STA_IOCTL_BSS_LIST BssList, *pBssList = &BssList;
	RT_CMD_STA_IOCTL_BSS *pList;

	GET_PAD_FROM_NET_DEV(pAd, dev);

   	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		data->length = 0;
		return 0;
        /*return -ENETDOWN; */
	}

	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **)&(pBssList->pList), sizeof(RT_CMD_STA_IOCTL_BSS_LIST) * IW_MAX_AP);
	if (pBssList->pList == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));
		return 0;
	}

	os_alloc_mem(NULL, (UCHAR **)&addr, sizeof(struct sockaddr) * IW_MAX_AP);
	if (addr == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));
		os_free_mem(NULL, pBssList);
		return 0;
	}

	pBssList->MaxNum = IW_MAX_AP;
	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_BSS_LIST_GET, 0, pBssList, 0, dev->priv_flags);

	for (i = 0; i <IW_MAX_AP ; i++)
	{
		if (i >=  pBssList->BssNum) /*pAd->ScanTab.BssNr) */
			break;
		addr[i].sa_family = ARPHRD_ETHER;
		pList = (pBssList->pList) + i;
		memcpy(addr[i].sa_data, pList->Bssid, MAC_ADDR_LEN);
		set_quality(pAd, &qual[i], pList); /*&pAd->ScanTab.BssEntry[i]); */
	}
	data->length = i;
	memcpy(extra, &addr, i*sizeof(addr[0]));
	data->flags = 1;		/* signal quality present (sort of) */
	memcpy(extra + i*sizeof(addr[0]), &qual, i*sizeof(qual[i]));

	os_free_mem(NULL, addr);
	os_free_mem(NULL, pBssList->pList);
	return 0;
}

#if defined(SIOCGIWSCAN) || defined(RT_CFG80211_SUPPORT)
int rt_ioctl_siwscan(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wreq, char *extra)
{
	VOID *pAd = NULL;
	int Status = NDIS_STATUS_SUCCESS;
	RT_CMD_STA_IOCTL_SCAN IoctlScan, *pIoctlScan = &IoctlScan;
#ifdef WPA_SUPPLICANT_SUPPORT
	struct iw_scan_req *req = (struct iw_scan_req *)extra;
#endif /* WPA_SUPPLICANT_SUPPORT */

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
/* because android will set scan and get scan when interface down */
#ifndef ANDROID_SUPPORT
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd, NULL) != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}
#endif /* ANDROID_SUPPORT */


	memset(pIoctlScan, 0, sizeof(RT_CMD_STA_IOCTL_SCAN));
#ifdef WPA_SUPPLICANT_SUPPORT
#if WIRELESS_EXT > 17
	pIoctlScan->FlgScanThisSsid = (wreq->data.length == sizeof(struct iw_scan_req) &&
									wreq->data.flags & IW_SCAN_THIS_ESSID);
	pIoctlScan->SsidLen = req->essid_len;
	pIoctlScan->pSsid = (CHAR *)(req->essid);
#endif
#endif /* WPA_SUPPLICANT_SUPPORT */
	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWSCAN, 0, pIoctlScan, 0, dev->priv_flags);

	RT_CMD_STATUS_TRANSLATE(pIoctlScan->Status);
	Status = pIoctlScan->Status;
	return Status;
}

int rt_ioctl_giwscan(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *data, char *extra)
{
	VOID *pAd = NULL;
	int i=0;
	PSTRING current_ev = extra, previous_ev = extra;
	PSTRING end_buf;
	PSTRING current_val;
	STRING custom[MAX_CUSTOM_LEN] = {0};
#ifndef IWEVGENIE
	unsigned char idx;
#endif /* IWEVGENIE */
	struct iw_event iwe;
	RT_CMD_STA_IOCTL_SCAN_TABLE IoctlScan, *pIoctlScan = &IoctlScan;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
/* because android will set scan and get scan when interface down */
#ifndef ANDROID_SUPPORT
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd, NULL) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;
	}
#endif /* ANDROID_SUPPORT */

#ifdef REFUSE_SCAN_QUERY_WHILE_SCANING
	if (RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SCAN_SANITY_CHECK, 0,
							NULL, 0, dev->priv_flags) != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("rt_ioctl_giwscan:: Still scanning\n"));
		return -EAGAIN;
	}
#endif /* REFUSE_SCAN_QUERY_WHILE_SCANING */


	pIoctlScan->priv_flags = dev->priv_flags;
	pIoctlScan->pBssTable = NULL;
	if (RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWSCAN, 0,
							pIoctlScan, 0, dev->priv_flags) != NDIS_STATUS_SUCCESS)
	{
		if (pIoctlScan->pBssTable != NULL)
			os_free_mem(NULL, pIoctlScan->pBssTable);
		return -EINVAL;
	}


	if (pIoctlScan->BssNr == 0)
	{
		data->length = 0;
		return 0;
	}
	
#if WIRELESS_EXT >= 17
    if (data->length > 0)
        end_buf = extra + data->length;
    else
        end_buf = extra + IW_SCAN_MAX_DATA;
#else
    end_buf = extra + IW_SCAN_MAX_DATA;
#endif

	for (i = 0; i < pIoctlScan->BssNr; i++) 
	{
		if (current_ev >= end_buf)
        {
#if WIRELESS_EXT >= 17
            return -E2BIG;
#else
			break;
#endif
        }
		
		/*MAC address */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
				memcpy(iwe.u.ap_addr.sa_data, &pIoctlScan->pBssTable[i].Bssid, ETH_ALEN);

        previous_ev = current_ev;
		current_ev = IWE_STREAM_ADD_EVENT(info, current_ev,end_buf, &iwe, IW_EV_ADDR_LEN);
        if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
            return -E2BIG;
#else
			break;
#endif

		/* 
		Protocol:
			it will show scanned AP's WirelessMode .
			it might be
					802.11a
					802.11a/n
					802.11g/n
					802.11b/g/n
					802.11g
					802.11b/g
		*/
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWNAME;


	{
		RT_CMD_STA_IOCTL_BSS_TABLE *pBssEntry=&pIoctlScan->pBssTable[i];
		BOOLEAN isGonly=FALSE;
		int rateCnt=0;

		if (pBssEntry->Channel>14)
		{
			if (pBssEntry->HtCapabilityLen!=0)
				strcpy(iwe.u.name,"802.11a/n");
			else	
				strcpy(iwe.u.name,"802.11a");
		}
		else
		{
			/*
				if one of non B mode rate is set supported rate . it mean G only. 
			*/
			for (rateCnt=0;rateCnt<pBssEntry->SupRateLen;rateCnt++)
			{									
				/*
					6Mbps(140) 9Mbps(146) and >=12Mbps(152) are supported rate , it mean G only. 
				*/
				if (pBssEntry->SupRate[rateCnt]==140 || pBssEntry->SupRate[rateCnt]==146 || pBssEntry->SupRate[rateCnt]>=152)
					isGonly=TRUE;
			}

			for (rateCnt=0;rateCnt<pBssEntry->ExtRateLen;rateCnt++)
			{
				if (pBssEntry->ExtRate[rateCnt]==140 || pBssEntry->ExtRate[rateCnt]==146 || pBssEntry->ExtRate[rateCnt]>=152)
					isGonly=TRUE;
			}		
			
			
			if (pBssEntry->HtCapabilityLen!=0)
			{
				if (isGonly==TRUE)
					strcpy(iwe.u.name,"802.11g/n");
				else
					strcpy(iwe.u.name,"802.11b/g/n");
			}
			else
			{
				if (isGonly==TRUE)
					strcpy(iwe.u.name,"802.11g");
				else
				{
					if (pBssEntry->SupRateLen==4 && pBssEntry->ExtRateLen==0)
						strcpy(iwe.u.name,"802.11b");
					else
						strcpy(iwe.u.name,"802.11b/g");		
				}
			}
		}
	}

		previous_ev = current_ev;
		current_ev = IWE_STREAM_ADD_EVENT(info, current_ev,end_buf, &iwe, IW_EV_ADDR_LEN);
		if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
	   		return -E2BIG;
#else
			break;
#endif

		/*ESSID */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.length = pIoctlScan->pBssTable[i].SsidLen;
		iwe.u.data.flags = 1;
 
        previous_ev = current_ev;
		current_ev = IWE_STREAM_ADD_POINT(info, current_ev,end_buf, &iwe, (PSTRING) pIoctlScan->pBssTable[i].Ssid);
        if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
            return -E2BIG;
#else
			break;
#endif
		
		/*Network Type */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWMODE;
		if (pIoctlScan->pBssTable[i].BssType == Ndis802_11IBSS)
		{
			iwe.u.mode = IW_MODE_ADHOC;
		}
		else if (pIoctlScan->pBssTable[i].BssType == Ndis802_11Infrastructure)
		{
			iwe.u.mode = IW_MODE_INFRA;
		}
		else
		{
			iwe.u.mode = IW_MODE_AUTO;
		}
		iwe.len = IW_EV_UINT_LEN;

        previous_ev = current_ev;
		current_ev = IWE_STREAM_ADD_EVENT(info, current_ev, end_buf, &iwe,  IW_EV_UINT_LEN);
        if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
            return -E2BIG;
#else
			break;
#endif

		/*Channel and Frequency */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWFREQ;
		{
			UCHAR ch = pIoctlScan->pBssTable[i].Channel;
			ULONG	m = 0;
/*			MAP_CHANNEL_ID_TO_KHZ(ch, m); */
			RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_CHID_2_FREQ, 0,
								(VOID *)&m, ch, dev->priv_flags);
			iwe.u.freq.m = m * 100;
			iwe.u.freq.e = 1;
		iwe.u.freq.i = 0;
		previous_ev = current_ev;
		current_ev = IWE_STREAM_ADD_EVENT(info, current_ev,end_buf, &iwe, IW_EV_FREQ_LEN);
        if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
            return -E2BIG;
#else
			break;
#endif
		}	    

        /*Add quality statistics */
        /*================================ */
        memset(&iwe, 0, sizeof(iwe));
    	iwe.cmd = IWEVQUAL;
    	iwe.u.qual.level = 0;
    	iwe.u.qual.noise = 0;
		set_quality(pAd, &iwe.u.qual, &pIoctlScan->pBssTable[i].Signal);
    	current_ev = IWE_STREAM_ADD_EVENT(info, current_ev, end_buf, &iwe, IW_EV_QUAL_LEN);
	if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
            return -E2BIG;
#else
			break;
#endif

		/*Encyption key */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWENCODE;
		if (pIoctlScan->pBssTable[i].FlgIsPrivacyOn)
			iwe.u.data.flags =IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;

        previous_ev = current_ev;		
        current_ev = IWE_STREAM_ADD_POINT(info, current_ev, end_buf,&iwe, (char *)pIoctlScan->MainSharedKey[(iwe.u.data.flags & IW_ENCODE_INDEX)-1]);
        if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
            return -E2BIG;
#else
			break;
#endif

		/*Bit Rate */
		/*================================ */
		if (pIoctlScan->pBssTable[i].SupRateLen)
        {
            UCHAR tmpRate = pIoctlScan->pBssTable[i].SupRate[pIoctlScan->pBssTable[i].SupRateLen-1];
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWRATE;
    		current_val = current_ev + IW_EV_LCP_LEN;            
            if (tmpRate == 0x82)
                iwe.u.bitrate.value =  1 * 1000000;
            else if (tmpRate == 0x84)
                iwe.u.bitrate.value =  2 * 1000000;
            else if (tmpRate == 0x8B)
                iwe.u.bitrate.value =  5.5 * 1000000;
            else if (tmpRate == 0x96)
                iwe.u.bitrate.value =  11 * 1000000;
            else
    		    iwe.u.bitrate.value =  (tmpRate/2) * 1000000;
            
			if (pIoctlScan->pBssTable[i].ExtRateLen)
			{
				UCHAR tmpSupRate =(pIoctlScan->pBssTable[i].SupRate[pIoctlScan->pBssTable[i].SupRateLen-1]& 0x7f);
				UCHAR tmpExtRate =(pIoctlScan->pBssTable[i].ExtRate[pIoctlScan->pBssTable[i].ExtRateLen-1]& 0x7f);
				iwe.u.bitrate.value = (tmpSupRate > tmpExtRate) ? (tmpSupRate)*500000 : (tmpExtRate)*500000;	
			}

			if (tmpRate == 0x6c && pIoctlScan->pBssTable[i].HtCapabilityLen > 0)
			{
				int rate_count = RT_RateSize/sizeof(__s32);
/*				HT_CAP_INFO capInfo = pIoctlScan->pBssTable[i].HtCapability.HtCapInfo; */
				int shortGI = pIoctlScan->pBssTable[i].ChannelWidth ? pIoctlScan->pBssTable[i].ShortGIfor40 : pIoctlScan->pBssTable[i].ShortGIfor20;
				int maxMCS = pIoctlScan->pBssTable[i].MCSSet ?  15 : 7;
				int rate_index = 12 + ((UCHAR)pIoctlScan->pBssTable[i].ChannelWidth * 24) +
								((UCHAR)shortGI *48) + ((UCHAR)maxMCS);
				if (rate_index < 0)
					rate_index = 0;
				if (rate_index >= rate_count)
					rate_index = rate_count-1;
				iwe.u.bitrate.value	=  ralinkrate[rate_index] * 500000;
			}
            
			iwe.u.bitrate.disabled = 0;
			current_val = IWE_STREAM_ADD_VALUE(info, current_ev,
				current_val, end_buf, &iwe,
    			IW_EV_PARAM_LEN);            

        	if((current_val-current_ev)>IW_EV_LCP_LEN)
            	current_ev = current_val;
        	else
#if WIRELESS_EXT >= 17
                return -E2BIG;
#else
			    break;
#endif
        }
            
#ifdef IWEVGENIE
        /*WPA IE */
		if (pIoctlScan->pBssTable[i].WpaIeLen > 0)
        {
			memset(&iwe, 0, sizeof(iwe));
			memset(&custom[0], 0, MAX_CUSTOM_LEN);
			memcpy(custom, &(pIoctlScan->pBssTable[i].pWpaIe[0]), 
						   pIoctlScan->pBssTable[i].WpaIeLen);
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = pIoctlScan->pBssTable[i].WpaIeLen;
			current_ev = IWE_STREAM_ADD_POINT(info, current_ev, end_buf, &iwe, custom);
			if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
                return -E2BIG;
#else
			    break;
#endif
		}
            
		/*WPA2 IE */
        if (pIoctlScan->pBssTable[i].RsnIeLen > 0)
        {
        	memset(&iwe, 0, sizeof(iwe));
			memset(&custom[0], 0, MAX_CUSTOM_LEN);
			memcpy(custom, &(pIoctlScan->pBssTable[i].pRsnIe[0]), 
						   pIoctlScan->pBssTable[i].RsnIeLen);
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = pIoctlScan->pBssTable[i].RsnIeLen;
			current_ev = IWE_STREAM_ADD_POINT(info, current_ev, end_buf, &iwe, custom);
			if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
                return -E2BIG;
#else
			    break;
#endif
        }

		/*WPS IE */
		if (pIoctlScan->pBssTable[i].WpsIeLen > 0)
        {
        	memset(&iwe, 0, sizeof(iwe));
			memset(&custom[0], 0, MAX_CUSTOM_LEN);
			memcpy(custom, &(pIoctlScan->pBssTable[i].pWpsIe[0]), 
						   pIoctlScan->pBssTable[i].WpsIeLen);
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = pIoctlScan->pBssTable[i].WpsIeLen;
			current_ev = IWE_STREAM_ADD_POINT(info, current_ev, end_buf, &iwe, custom);
			if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
                return -E2BIG;
#else
			    break;
#endif
        }
#else
        /*WPA IE */
		/*================================ */
        if (pIoctlScan->pBssTable[i].WpaIeLen > 0)
        {
    		NdisZeroMemory(&iwe, sizeof(iwe));
			memset(&custom[0], 0, MAX_CUSTOM_LEN);
    		iwe.cmd = IWEVCUSTOM;
            iwe.u.data.length = (pIoctlScan->pBssTable[i].WpaIeLen * 2) + 7;
            NdisMoveMemory(custom, "wpa_ie=", 7);
            for (idx = 0; idx < pIoctlScan->pBssTable[i].WpaIeLen; idx++)
                sprintf(custom, "%s%02x", custom, pIoctlScan->pBssTable[i].pWpaIe[idx]);
            previous_ev = current_ev;
    		current_ev = IWE_STREAM_ADD_POINT(info, current_ev, end_buf, &iwe,  custom);
            if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
                return -E2BIG;
#else
			    break;
#endif
        }

        /*WPA2 IE */
        if (pIoctlScan->pBssTable[i].RsnIeLen > 0)
        {
    		NdisZeroMemory(&iwe, sizeof(iwe));
			memset(&custom[0], 0, MAX_CUSTOM_LEN);
    		iwe.cmd = IWEVCUSTOM;
            iwe.u.data.length = (pIoctlScan->pBssTable[i].RsnIeLen * 2) + 7;
            NdisMoveMemory(custom, "rsn_ie=", 7);
			for (idx = 0; idx < pIoctlScan->pBssTable[i].RsnIeLen; idx++)
                sprintf(custom, "%s%02x", custom, pIoctlScan->pBssTable[i].pRsnIe[idx]);
            previous_ev = current_ev;
    		current_ev = IWE_STREAM_ADD_POINT(info, current_ev, end_buf, &iwe,  custom);
            if (current_ev == previous_ev)
#if WIRELESS_EXT >= 17
                return -E2BIG;
#else
			    break;
#endif
        }


#endif /* IWEVGENIE */
	}

	data->length = current_ev - extra;
/*    pAd->StaCfg.bScanReqIsFromWebUI = FALSE; */
/*	DBGPRINT(RT_DEBUG_ERROR ,("===>rt_ioctl_giwscan. %d(%d) BSS returned, data->length = %d\n",i , pAd->ScanTab.BssNr, data->length)); */
 	os_free_mem(NULL, pIoctlScan->pBssTable);

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SCAN_END, 0, NULL, data->length, dev->priv_flags);
	return 0;
}
#endif

int rt_ioctl_siwessid(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_point *data, char *essid)
{
	VOID *pAd = NULL;
	RT_CMD_STA_IOCTL_SSID IoctlEssid, *pIoctlEssid = &IoctlEssid;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
       	return -ENETDOWN;   
    }

	if (data->flags)
	{
		/* Includes null character. */
		if (data->length > (IW_ESSID_MAX_SIZE + 1)) 
			return -E2BIG;
	}


	pIoctlEssid->FlgAnySsid = data->flags;
	pIoctlEssid->SsidLen = data->length;
	pIoctlEssid->pSsid = (CHAR *)essid;
	pIoctlEssid->Status = 0;
	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWESSID, 0, pIoctlEssid, 0, dev->priv_flags);

	RT_CMD_STATUS_TRANSLATE(pIoctlEssid->Status);
	return pIoctlEssid->Status;
}

int rt_ioctl_giwessid(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_point *data, char *essid)
{
	VOID *pAd = NULL;
	RT_CMD_STA_IOCTL_SSID IoctlEssid, *pIoctlEssid = &IoctlEssid;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;
	}

	data->flags = 1;


	pIoctlEssid->pSsid = (CHAR *)essid;
	pIoctlEssid->Status = 0;
	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWESSID, 0, pIoctlEssid, 0, dev->priv_flags);
	data->length = pIoctlEssid->SsidLen;

	RT_CMD_STATUS_TRANSLATE(pIoctlEssid->Status);
	return pIoctlEssid->Status;
}

int rt_ioctl_siwnickn(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_point *data, char *nickname)
{
	VOID *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, dev);

    /*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
        DBGPRINT(RT_DEBUG_TRACE ,("INFO::Network is down!\n"));
        return -ENETDOWN;   
    }

	if (data->length > IW_ESSID_MAX_SIZE)
		return -EINVAL;


	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWNICKN, 0,
							nickname, data->length, dev->priv_flags);
	return 0;
}

int rt_ioctl_giwnickn(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_point *data, char *nickname)
{
	VOID *pAd = NULL;
	RT_CMD_STA_IOCTL_NICK_NAME NickName, *pNickName = &NickName;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		data->length = 0;
        return -ENETDOWN;
	}


	pNickName->NameLen = data->length;
	pNickName->pName = (CHAR *)nickname;

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWNICKN, 0,
							pNickName, 0, dev->priv_flags);

	data->length = pNickName->NameLen;
	return 0;
}

int rt_ioctl_siwrts(struct net_device *dev,
		       struct iw_request_info *info,
		       struct iw_param *rts, char *extra)
{
	VOID *pAd = NULL;
	u16 val;

	GET_PAD_FROM_NET_DEV(pAd, dev);

    /*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
        DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;   
    }
	
	if (rts->disabled)
		val = MAX_RTS_THRESHOLD;
	else if (rts->value < 0 || rts->value > MAX_RTS_THRESHOLD)
		return -EINVAL;
	else if (rts->value == 0)
	    val = MAX_RTS_THRESHOLD;
	else
		val = rts->value;
	
/*	if (val != pAd->CommonCfg.RtsThreshold) */
/*		pAd->CommonCfg.RtsThreshold = val; */

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWRTS, 0, NULL, val, dev->priv_flags);
	return 0;
}

int rt_ioctl_giwrts(struct net_device *dev,
		       struct iw_request_info *info,
		       struct iw_param *rts, char *extra)
{
	VOID *pAd = NULL;
	USHORT RtsThreshold;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	/*check if the interface is down */
/*    	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
		if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    	{
      		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        	return -ENETDOWN;   
    	}

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWRTS, 0,
						&RtsThreshold, 0, dev->priv_flags);
	rts->value = RtsThreshold;
	rts->disabled = (rts->value == MAX_RTS_THRESHOLD);
	rts->fixed = 1;

	return 0;
}

int rt_ioctl_siwfrag(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *frag, char *extra)
{
	VOID *pAd = NULL;
	u16 val;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
/*    	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
		if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    	{
      		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        	return -ENETDOWN;   
    	}

	if (frag->disabled)
		val = MAX_FRAG_THRESHOLD;
	else if (frag->value >= MIN_FRAG_THRESHOLD || frag->value <= MAX_FRAG_THRESHOLD)
        val = __cpu_to_le16(frag->value & ~0x1); /* even numbers only */
	else if (frag->value == 0)
	    val = MAX_FRAG_THRESHOLD;
	else
		return -EINVAL;

/*	pAd->CommonCfg.FragmentThreshold = val; */
	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWFRAG, 0, NULL, val, dev->priv_flags);
	return 0;
}

int rt_ioctl_giwfrag(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *frag, char *extra)
{
	VOID *pAd = NULL;
	USHORT FragmentThreshold;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	/*check if the interface is down */
/*    	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
		if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    	{
      		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        	return -ENETDOWN;   
    	}
		
	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWFRAG, 0,
						&FragmentThreshold, 0, dev->priv_flags);
	frag->value = FragmentThreshold;
	frag->disabled = (frag->value == MAX_FRAG_THRESHOLD);
	frag->fixed = 1;

	return 0;
}

#define MAX_WEP_KEY_SIZE 13
#define MIN_WEP_KEY_SIZE 5
int rt_ioctl_siwencode(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *erq, char *extra)
{
	VOID *pAd = NULL;
	RT_CMD_STA_IOCTL_SECURITY IoctlSec, *pIoctlSec = &IoctlSec;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
/*    	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
		if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    	{
      		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        	return -ENETDOWN;   
    	}


	pIoctlSec->pData = (CHAR *)extra;
	pIoctlSec->length = erq->length;
	pIoctlSec->KeyIdx = (erq->flags & IW_ENCODE_INDEX) - 1;
	pIoctlSec->flags = 0;

	if (erq->flags & IW_ENCODE_DISABLED)
		pIoctlSec->flags |= RT_CMD_STA_IOCTL_SECURITY_DISABLED;
	if (erq->flags & IW_ENCODE_RESTRICTED)
		pIoctlSec->flags |= RT_CMD_STA_IOCTL_SECURITY_RESTRICTED;
	if (erq->flags & IW_ENCODE_OPEN)
		pIoctlSec->flags |= RT_CMD_STA_IOCTL_SECURITY_OPEN;
	if (erq->flags & IW_ENCODE_NOKEY)
		pIoctlSec->flags |= RT_CMD_STA_IOCTL_SECURITY_NOKEY;
	if (erq->flags & IW_ENCODE_MODE)
		pIoctlSec->flags |= RT_CMD_STA_IOCTL_SECURITY_MODE;

	pIoctlSec->Status = 0;

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWENCODE, 0,
						pIoctlSec, 0, dev->priv_flags);
	RT_CMD_STATUS_TRANSLATE(pIoctlSec->Status);
	return pIoctlSec->Status;
}

int
rt_ioctl_giwencode(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *erq, char *key)
{
/*	int kid; */
	VOID *pAd = NULL;
	RT_CMD_STA_IOCTL_SECURITY IoctlSec, *pIoctlSec = &IoctlSec;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	/*check if the interface is down */
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
  		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
    	return -ENETDOWN;   
	}
		

	pIoctlSec->pData = (CHAR *)key;
	pIoctlSec->KeyIdx = erq->flags & IW_ENCODE_INDEX;
	pIoctlSec->length = erq->length;

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWENCODE, 0,
						pIoctlSec, 0, dev->priv_flags);

	erq->length = pIoctlSec->length;
	erq->flags = pIoctlSec->KeyIdx;
	if (pIoctlSec->flags & RT_CMD_STA_IOCTL_SECURITY_DISABLED)
		erq->flags = RT_CMD_STA_IOCTL_SECURITY_DISABLED;
	{
		if (pIoctlSec->flags & RT_CMD_STA_IOCTL_SECURITY_ENABLED)
			erq->flags |= IW_ENCODE_ENABLED;
		if (pIoctlSec->flags & RT_CMD_STA_IOCTL_SECURITY_RESTRICTED)
			erq->flags |= IW_ENCODE_RESTRICTED;	
		if (pIoctlSec->flags & RT_CMD_STA_IOCTL_SECURITY_OPEN)
			erq->flags |= IW_ENCODE_OPEN;
	}
	return 0;

}

int rt_ioctl_setparam(struct net_device *dev, struct iw_request_info *info,
			 void *w, char *extra)
{
	VOID *pAd;
/*	POS_COOKIE pObj; */
	PSTRING this_char = extra;
	PSTRING value;
	int  Status=0;
	RT_CMD_PARAM_SET CmdParam;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}


	
	/*check if the interface is down */
/*    	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
		if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    	{
#ifdef CONFIG_APSTA_MIXED_SUPPORT
			if (!*this_char)
				return -EINVAL;
	                                                                                                                            
			if ((value = rtstrchr(this_char, '=')) != NULL)                                                                             
	    		*value++ = 0;
	                                                                                                                            
			if (!value || (strcmp(this_char, "OpMode") != 0))   
#endif /* CONFIG_APSTA_MIXED_SUPPORT */
			{
				DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
				return -ENETDOWN;
			}
    	}
	else
	{
		if (!*this_char)
			return -EINVAL;
	                                                                                                                            
		if ((value = rtstrchr(this_char, '=')) != NULL)                                                                             
		    *value++ = 0;

		if (!value && (strcmp(this_char, "SiteSurvey") != 0))
		    return -EINVAL;
		else if (!value && (strcmp(this_char, "SiteSurvey") == 0))
			goto SET_PROC;

		/* reject setting nothing besides ANY ssid(ssidLen=0) */
		if (!*value && (strcmp(this_char, "SSID") != 0))
			return -EINVAL; 
	}

SET_PROC:
	CmdParam.pThisChar = this_char;
	CmdParam.pValue = value;
	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_PARAM_SET, 0, &CmdParam, 0, dev->priv_flags);
/*	Status = RTMPSTAPrivIoctlSet(pAd, this_char, value); */
		
    return Status;
}



static int
rt_private_get_statistics(struct net_device *dev, struct iw_request_info *info,
		struct iw_point *wrq, char *extra)
{
	INT				Status = 0;
    VOID   *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;
	}

    if (extra == NULL)
    {
        wrq->length = 0;
        return -EIO;
    }
    
    memset(extra, 0x00, IW_PRIV_SIZE_MASK);


	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_IW_GET_STATISTICS, 0,
						extra, IW_PRIV_SIZE_MASK, dev->priv_flags);

    wrq->length = strlen(extra) + 1; /* 1: size of '\0' */
    DBGPRINT(RT_DEBUG_TRACE, ("<== rt_private_get_statistics, wrq->length = %d\n", wrq->length));

    return Status;
}


static int
rt_private_show(struct net_device *dev, struct iw_request_info *info,
		struct iw_point *wrq, PSTRING extra)
{
	RTMP_IOCTL_INPUT_STRUCT wrqin;
	INT				Status = 0;
	VOID   			*pAd;
/*	POS_COOKIE		pObj; */
	u32             subcmd = wrq->flags;
	RT_CMD_STA_IOCTL_SHOW IoctlShow, *pIoctlShow = &IoctlShow;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;
	}

/*	pObj = (POS_COOKIE) pAd->OS_Cookie; */
	if (extra == NULL)
	{
		wrq->length = 0;
		return -EIO;
	}
	memset(extra, 0x00, IW_PRIV_SIZE_MASK);
    
    
	wrqin.u.data.pointer = wrq->pointer;
	wrqin.u.data.length = wrq->length;

	pIoctlShow->pData = (CHAR *)extra;
	pIoctlShow->MaxSize = IW_PRIV_SIZE_MASK;
	pIoctlShow->InfType = dev->priv_flags;
	RTMP_STA_IoctlHandle(pAd, &wrqin, CMD_RTPRIV_IOCTL_SHOW, subcmd, pIoctlShow, 0, dev->priv_flags);

	wrq->length = wrqin.u.data.length;
    return Status;
}

#ifdef SIOCSIWMLME
int rt_ioctl_siwmlme(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu,
			   char *extra)
{
	VOID   *pAd = NULL;
	struct iw_mlme *pMlme = (struct iw_mlme *)wrqu->data.pointer;
/*	MLME_QUEUE_ELEM				MsgElem; */
/*	MLME_QUEUE_ELEM				*pMsgElem = NULL; */
/*	MLME_DISASSOC_REQ_STRUCT	DisAssocReq; */
/*	MLME_DEAUTH_REQ_STRUCT      DeAuthReq; */
	ULONG Subcmd = 0;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	DBGPRINT(RT_DEBUG_TRACE, ("====> %s\n", __FUNCTION__));

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;
	}

	if (pMlme == NULL)
		return -EINVAL;


	switch(pMlme->cmd)
	{
#ifdef IW_MLME_DEAUTH	
		case IW_MLME_DEAUTH:
			Subcmd = RT_CMD_STA_IOCTL_IW_MLME_DEAUTH;
			break;
#endif /* IW_MLME_DEAUTH */
#ifdef IW_MLME_DISASSOC
		case IW_MLME_DISASSOC:
			Subcmd = RT_CMD_STA_IOCTL_IW_MLME_DISASSOC;
			break;
#endif /* IW_MLME_DISASSOC */
		default:
			DBGPRINT(RT_DEBUG_TRACE, ("====> %s - Unknow Command\n", __FUNCTION__));
			break;
	}

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWMLME, Subcmd,
						NULL, pMlme->reason_code, dev->priv_flags);
	return 0;
}
#endif /* SIOCSIWMLME */

#if WIRELESS_EXT > 17


int rt_ioctl_siwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	VOID   *pAd = NULL;
	struct iw_param *param = &wrqu->param;
	RT_CMD_STA_IOCTL_SECURITY_ADV IoctlWpa, *pIoctlWpa = &IoctlWpa;

	GET_PAD_FROM_NET_DEV(pAd, dev);

    /*check if the interface is down */
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
  		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
    		return -ENETDOWN;   
	}


	pIoctlWpa->flags = 0;
	pIoctlWpa->value = param->value; /* default */

	switch (param->flags & IW_AUTH_INDEX) {
    	case IW_AUTH_WPA_VERSION:
			pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_VERSION;
            if (param->value == IW_AUTH_WPA_VERSION_WPA)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_VERSION1;
            else if (param->value == IW_AUTH_WPA_VERSION_WPA2)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_VERSION2;

            DBGPRINT(RT_DEBUG_TRACE, ("%s::IW_AUTH_WPA_VERSION - param->value = %d!\n", __FUNCTION__, param->value));
            break;
    	case IW_AUTH_CIPHER_PAIRWISE:
			pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_PAIRWISE;
            if (param->value == IW_AUTH_CIPHER_NONE)
                pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_PAIRWISE_NONE;
            else if (param->value == IW_AUTH_CIPHER_WEP40)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_PAIRWISE_WEP40;
            else if (param->value == IW_AUTH_CIPHER_WEP104)
                pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_PAIRWISE_WEP104;
            else if (param->value == IW_AUTH_CIPHER_TKIP)
                pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_PAIRWISE_TKIP;
            else if (param->value == IW_AUTH_CIPHER_CCMP)
                pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_PAIRWISE_CCMP;
            DBGPRINT(RT_DEBUG_TRACE, ("%s::IW_AUTH_CIPHER_PAIRWISE - param->value = %d!\n", __FUNCTION__, param->value));
            break;
    	case IW_AUTH_CIPHER_GROUP:
			pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_GROUP;
            if (param->value == IW_AUTH_CIPHER_NONE)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_GROUP_NONE;
            else if (param->value == IW_AUTH_CIPHER_WEP40)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_GROUP_WEP40;
			else if (param->value == IW_AUTH_CIPHER_WEP104)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_GROUP_WEP104;
            else if (param->value == IW_AUTH_CIPHER_TKIP)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_GROUP_TKIP;
            else if (param->value == IW_AUTH_CIPHER_CCMP)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_GROUP_CCMP;
            DBGPRINT(RT_DEBUG_TRACE, ("%s::IW_AUTH_CIPHER_GROUP - param->value = %d!\n", __FUNCTION__, param->value));
            break;
    	case IW_AUTH_KEY_MGMT:
			pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_KEY_MGMT;
            if (param->value == IW_AUTH_KEY_MGMT_802_1X)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_KEY_MGMT_1X;
            DBGPRINT(RT_DEBUG_TRACE, ("%s::IW_AUTH_KEY_MGMT - param->value = %d!\n", __FUNCTION__, param->value));
            break;
    	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
			pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_AUTH_RX_UNENCRYPTED_EAPOL;
            break;
    	case IW_AUTH_PRIVACY_INVOKED:
			pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_AUTH_PRIVACY_INVOKED;
            DBGPRINT(RT_DEBUG_TRACE, ("%s::IW_AUTH_PRIVACY_INVOKED - param->value = %d!\n", __FUNCTION__, param->value));
    		break;
    	case IW_AUTH_DROP_UNENCRYPTED:
			pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_AUTH_DROP_UNENCRYPTED;
            DBGPRINT(RT_DEBUG_TRACE, ("%s::IW_AUTH_DROP_UNENCRYPTED - param->value = %d!\n", __FUNCTION__, param->value));
    		break;
    	case IW_AUTH_80211_AUTH_ALG: 
			pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_AUTH_80211_AUTH_ALG;
			if (param->value & IW_AUTH_ALG_SHARED_KEY) 
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_AUTH_80211_AUTH_ALG_SHARED;
            else if (param->value & IW_AUTH_ALG_OPEN_SYSTEM)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_AUTH_80211_AUTH_ALG_OPEN;
            else if (param->value & IW_AUTH_ALG_LEAP)
				pIoctlWpa->value = RT_CMD_STA_IOCTL_WPA_AUTH_80211_AUTH_ALG_LEAP;
            DBGPRINT(RT_DEBUG_TRACE, ("%s::IW_AUTH_80211_AUTH_ALG - param->value = %d!\n", __FUNCTION__, param->value));
			break;
    	case IW_AUTH_WPA_ENABLED:
			pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_AUTH_WPA_ENABLED;
    		DBGPRINT(RT_DEBUG_TRACE, ("%s::IW_AUTH_WPA_ENABLED - Driver supports WPA!(param->value = %d)\n", __FUNCTION__, param->value));
    		break;
    	default:
    		return -EOPNOTSUPP;
}

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWAUTH, 0,
						pIoctlWpa, 0, dev->priv_flags);

	return 0;
}

int rt_ioctl_giwauth(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	VOID   *pAd = NULL;
	struct iw_param *param = &wrqu->param;
	RT_CMD_STA_IOCTL_SECURITY_ADV IoctlWpa, *pIoctlWpa = &IoctlWpa;

	GET_PAD_FROM_NET_DEV(pAd, dev);

    /*check if the interface is down */
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
  		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
    	return -ENETDOWN;   
    }


	pIoctlWpa->flags = 0;
	pIoctlWpa->value = 0;

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_DROP_UNENCRYPTED:
		pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_AUTH_DROP_UNENCRYPTED;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_AUTH_80211_AUTH_ALG;
		break;

	case IW_AUTH_WPA_ENABLED:
		pIoctlWpa->flags = RT_CMD_STA_IOCTL_WPA_AUTH_WPA_ENABLED;
		break;

	default:
		return -EOPNOTSUPP;
	}

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWAUTH, 0,
						pIoctlWpa, 0, dev->priv_flags);

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_DROP_UNENCRYPTED:
		param->value = pIoctlWpa->value;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		param->value = (pIoctlWpa->value == 0) ? IW_AUTH_ALG_SHARED_KEY : IW_AUTH_ALG_OPEN_SYSTEM;
		break;

	case IW_AUTH_WPA_ENABLED:
		param->value = pIoctlWpa->value;
		break;
	}

    DBGPRINT(RT_DEBUG_TRACE, ("rt_ioctl_giwauth::param->value = %d!\n", param->value));
	return 0;
}


int rt_ioctl_siwencodeext(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu,
			   char *extra)
{
	VOID   *pAd = NULL;
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
    int /* keyIdx, */ alg = ext->alg;
	RT_CMD_STA_IOCTL_SECURITY IoctlSec, *pIoctlSec = &IoctlSec;
	
	GET_PAD_FROM_NET_DEV(pAd, dev);
	
    /*check if the interface is down */
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
  		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
    	return -ENETDOWN;   
	}

			
	pIoctlSec->pData = (CHAR *)ext->key;
	pIoctlSec->length = ext->key_len;
	pIoctlSec->KeyIdx = (encoding->flags & IW_ENCODE_INDEX) - 1;
	if (alg == IW_ENCODE_ALG_NONE )
		pIoctlSec->Alg = RT_CMD_STA_IOCTL_SECURITY_ALG_NONE;
	else if (alg == IW_ENCODE_ALG_WEP)
		pIoctlSec->Alg = RT_CMD_STA_IOCTL_SECURITY_ALG_WEP;
	else if (alg == IW_ENCODE_ALG_TKIP)
		pIoctlSec->Alg = RT_CMD_STA_IOCTL_SECURITY_ALG_TKIP;
	else if (alg == IW_ENCODE_ALG_CCMP)
		pIoctlSec->Alg = RT_CMD_STA_IOCTL_SECURITY_ALG_CCMP;
	else 
	{
		DBGPRINT(RT_DEBUG_WARN, ("Warning: Security type is not supported. (alg = %d) \n", alg));
		pIoctlSec->Alg = alg;
		return -EOPNOTSUPP;
	}
	pIoctlSec->ext_flags = 0;
	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
		pIoctlSec->ext_flags |= RT_CMD_STA_IOCTL_SECURTIY_EXT_SET_TX_KEY;
	if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY)
		pIoctlSec->ext_flags |= RT_CMD_STA_IOCTL_SECURTIY_EXT_GROUP_KEY;
	if (encoding->flags & IW_ENCODE_DISABLED)
		pIoctlSec->flags = RT_CMD_STA_IOCTL_SECURITY_DISABLED;
	else
		pIoctlSec->flags = 0;

	if (RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWENCODEEXT, 0,
						pIoctlSec, 0, dev->priv_flags) != NDIS_STATUS_SUCCESS)
		return -EINVAL;

    return 0;
}

int
rt_ioctl_giwencodeext(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	VOID *pAd = NULL;
/*	PCHAR pKey = NULL; */
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int /* idx, */ max_key_len;
	RT_CMD_STA_IOCTL_SECURITY IoctlSec, *pIoctlSec = &IoctlSec;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	DBGPRINT(RT_DEBUG_TRACE ,("===> rt_ioctl_giwencodeext\n"));

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;
	}

	max_key_len = encoding->length - sizeof(*ext);
	if (max_key_len < 0)
		return -EINVAL;
	memset(ext, 0, sizeof(*ext));


	memset(pIoctlSec, 0, sizeof(RT_CMD_STA_IOCTL_SECURITY));
	pIoctlSec->KeyIdx = encoding->flags & IW_ENCODE_INDEX;
	pIoctlSec->MaxKeyLen = max_key_len;

	if (RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWENCODEEXT, 0,
						pIoctlSec, 0, dev->priv_flags) != NDIS_STATUS_SUCCESS)
	{
		ext->key_len = 0;
		RT_CMD_STATUS_TRANSLATE(pIoctlSec->Status);
		return pIoctlSec->Status;
	}

	encoding->flags = pIoctlSec->KeyIdx;
	ext->key_len = pIoctlSec->length;

	if (pIoctlSec->Alg == RT_CMD_STA_IOCTL_SECURITY_ALG_NONE)
		ext->alg = IW_ENCODE_ALG_NONE;
	else if (pIoctlSec->Alg == RT_CMD_STA_IOCTL_SECURITY_ALG_WEP)
		ext->alg = IW_ENCODE_ALG_WEP;
	else if (pIoctlSec->Alg == RT_CMD_STA_IOCTL_SECURITY_ALG_TKIP)
		ext->alg = IW_ENCODE_ALG_TKIP;
	else if (pIoctlSec->Alg == RT_CMD_STA_IOCTL_SECURITY_ALG_CCMP)
		ext->alg = IW_ENCODE_ALG_CCMP;

	if (pIoctlSec->flags & RT_CMD_STA_IOCTL_SECURITY_DISABLED)
		encoding->flags |= IW_ENCODE_DISABLED;

	if (ext->key_len && pIoctlSec->pData)
	{
		encoding->flags |= IW_ENCODE_ENABLED;
		memcpy(ext->key, pIoctlSec->pData, ext->key_len);
	}
	
	return 0;
}

#ifdef SIOCSIWGENIE
int rt_ioctl_siwgenie(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	VOID   *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, dev);	
	
	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;
	}	
#ifdef WPA_SUPPLICANT_SUPPORT

	if (RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWGENIE, 0,
						extra, wrqu->data.length, dev->priv_flags) != NDIS_STATUS_SUCCESS)
		return -EINVAL;
#endif /* WPA_SUPPLICANT_SUPPORT */

	return -EOPNOTSUPP;
}
#endif /* SIOCSIWGENIE */

int rt_ioctl_giwgenie(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	VOID   *pAd = NULL;
	RT_CMD_STA_IOCTL_RSN_IE IoctlRsnIe, *pIoctlRsnIe = &IoctlRsnIe;

	GET_PAD_FROM_NET_DEV(pAd, dev);	
	
	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;
	}


	pIoctlRsnIe->length = wrqu->data.length;
	pIoctlRsnIe->pRsnIe = (UCHAR *)extra;

	if (RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWGENIE, 0,
						pIoctlRsnIe, 0, dev->priv_flags) != NDIS_STATUS_SUCCESS)
		return -E2BIG;

	wrqu->data.length = pIoctlRsnIe->length;
	return 0;
}

int rt_ioctl_siwpmksa(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu,
			   char *extra)
{
	VOID   *pAd = NULL;
	struct iw_pmksa *pPmksa = (struct iw_pmksa *)wrqu->data.pointer;
/*	INT	CachedIdx = 0, idx = 0; */
	RT_CMD_STA_IOCTL_PMA_SA IoctlPmaSa, *pIoctlPmaSa = &IoctlPmaSa;

	GET_PAD_FROM_NET_DEV(pAd, dev);	

	/*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
       	DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
        return -ENETDOWN;
	}

	if (pPmksa == NULL)
		return -EINVAL;

	DBGPRINT(RT_DEBUG_TRACE ,("===> rt_ioctl_siwpmksa\n"));


	if (pPmksa->cmd == IW_PMKSA_FLUSH)
		pIoctlPmaSa->Cmd = RT_CMD_STA_IOCTL_PMA_SA_FLUSH;
	else if (pPmksa->cmd == IW_PMKSA_REMOVE)
		pIoctlPmaSa->Cmd = RT_CMD_STA_IOCTL_PMA_SA_REMOVE;
	else if (pPmksa->cmd == IW_PMKSA_ADD)
		pIoctlPmaSa->Cmd = RT_CMD_STA_IOCTL_PMA_SA_ADD;
	else
		pIoctlPmaSa->Cmd = 0;
	pIoctlPmaSa->pBssid = (UCHAR *)pPmksa->bssid.sa_data;
	pIoctlPmaSa->pPmkid = pPmksa->pmkid;

	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWPMKSA, 0,
						pIoctlPmaSa, 0, dev->priv_flags);

	return 0;
}
#endif /* #if WIRELESS_EXT > 17 */

#ifdef DBG
static int
rt_private_ioctl_bbp(struct net_device *dev, struct iw_request_info *info,
		struct iw_point *wrq, char *extra)
{
	RTMP_IOCTL_INPUT_STRUCT wrqin;
	INT					Status = 0;
    VOID       			*pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, dev);	


	memset(extra, 0x00, IW_PRIV_SIZE_MASK);

	wrqin.u.data.pointer = wrq->pointer;
	wrqin.u.data.length = wrq->length;

	RTMP_STA_IoctlHandle(pAd, &wrqin, CMD_RTPRIV_IOCTL_BBP, 0, extra, IW_PRIV_SIZE_MASK, dev->priv_flags);

	wrq->length = wrqin.u.data.length;

	
	DBGPRINT(RT_DEBUG_TRACE, ("<==rt_private_ioctl_bbp\n\n"));	
    
    return Status;
}
#endif /* DBG */

int rt_ioctl_siwrate(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
    VOID   *pAd = NULL;
    UINT32          rate = wrqu->bitrate.value, fixed = wrqu->bitrate.fixed;
	RT_CMD_RATE_SET CmdRate;

	GET_PAD_FROM_NET_DEV(pAd, dev);

    /*check if the interface is down */
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
  		DBGPRINT(RT_DEBUG_TRACE, ("rt_ioctl_siwrate::Network is down!\n"));
    	return -ENETDOWN;   
	}    

    DBGPRINT(RT_DEBUG_TRACE, ("rt_ioctl_siwrate::(rate = %d, fixed = %d)\n", rate, fixed));
    /* rate = -1 => auto rate
       rate = X, fixed = 1 => (fixed rate X)       
    */


	CmdRate.Rate = rate;
	CmdRate.Fixed = fixed;

	if (RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWRATE, 0,
							&CmdRate, 0, dev->priv_flags) != NDIS_STATUS_SUCCESS)
		return -EOPNOTSUPP;

    return 0;
}

int rt_ioctl_giwrate(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
    VOID   *pAd = NULL;
/*    int rate_index = 0, rate_count = 0; */
/*    HTTRANSMIT_SETTING ht_setting; */
	ULONG Rate;

	GET_PAD_FROM_NET_DEV(pAd, dev);

    /*check if the interface is down */
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
  		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
    	return -ENETDOWN;   
	}


	RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWRATE, 0, &Rate, 0, dev->priv_flags);
	wrqu->bitrate.value = Rate;
    wrqu->bitrate.disabled = 0;

    return 0;
}

static int andriod_handle_private(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra)
{
	VOID *pAd = NULL;
	int len = 0;
	char *ext;
	int ret = 0;

	len = dwrq->length;
	if (!(ext = kmalloc(len, /*GFP_KERNEL*/GFP_ATOMIC)))
		return -ENOMEM;

	if (copy_from_user(ext, dwrq->pointer, len))
	{
		kfree(ext);
		printk("andriod_handle_private   copy_from_user\n");
		return -EFAULT;
	}
#if 0
    printk("Check Command!!!!\n");
      printk("%s\n", ext);
         printk(">>>>>>>>>>>>>\n");
            unsigned char hex=0;

            for (i=0; i<len; i++){
                hex = *(ext+i);
                printk("%x", hex);

      //           printk("%c", *(ext+i));
             }

      printk("\nCheck End!!!!!\n");
#endif
	GET_PAD_FROM_NET_DEV(pAd, dev);

	if(0 == strcasecmp(ext,"START"))
	{
		//Turn on Wi-Fi hardware
		//OK if successful
		printk("ralink test START Turn on Wi-Fi hardware \n");
		return -1;
	}
	else if(0 == strcasecmp(ext,"STOP"))
	{
		printk("ralink test STOP Turn off  Wi-Fi hardware \n");
		return -1;
	}
	else if(0 == strcasecmp(ext,"RSSI"))
	{
		CHAR AvgRssi0;
		RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWPRIVRSSI,
								0, &AvgRssi0, 0, dev->priv_flags);
		sprintf(ext, "rssi %d", AvgRssi0);
	}
	else if(0 == strcasecmp(ext,"LINKSPEED"))
	{
		sprintf(ext, "LINKSPEED %d", 150);
	}
	else if(0 == strcasecmp(ext,"MACADDR"))
	{
		UCHAR mac[6];
		RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIFHWADDR,
								0, mac, 0, dev->priv_flags);
		sprintf(ext,
			"MACADDR = %02x.%02x.%02x.%02x.%02x.%02x",
			mac[0], mac[1], mac[2],
			mac[3], mac[4], mac[5]);
	}
	else if(0 == strcasecmp(ext,"SCAN-ACTIVE"))
	{
		sprintf(ext, "OK");
	}
	else if(0 == strcasecmp(ext,"SCAN-PASSIVE"))
	{
//		sprintf(ext, "OK");
		RT_CMD_STA_IOCTL_SCAN IoctlScan, *pIoctlScan = &IoctlScan;
        memset(pIoctlScan, 0, sizeof(RT_CMD_STA_IOCTL_SCAN));
         RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWSCAN, 0, pIoctlScan, 0, dev->priv_flags);
       sprintf(ext, "OK");
         DBGPRINT(RT_DEBUG_TRACE, ("%s, handle SCAN-PASSIVE command \n", __func__));
#if 0
		#ifdef WPA_SUPPLICANT_SUPPORT
        struct iw_scan_req *req = (struct iw_scan_req *)extra;
        #endif /*  WPA_SUPPLICANT_SUPPORT */
        printk("error after %d\n",__LINE__);
        memset(pIoctlScan, 0, sizeof(RT_CMD_STA_IOCTL_SCAN));
//		                                 RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWSCAN, 0, pIoctlScan, 0, dev->priv_flags);
        #ifdef WPA_SUPPLICANT_SUPPORT
        #if WIRELESS_EXT > 17
        printk("error after %d\n",__LINE__);
        pIoctlScan->FlgScanThisSsid = (dwrq->length == sizeof(struct iw_scan_req) &&
        dwrq->flags & IW_SCAN_THIS_ESSID);
        printk("error after %d\n",__LINE__);
        pIoctlScan->SsidLen = req->essid_len;
        printk("error after %d\n",__LINE__);
        pIoctlScan->pSsid = (CHAR *)(req->essid);
        printk("error after %d\n",__LINE__);
        #endif
        #endif /*  WPA_SUPPLICANT_SUPPORT */

         RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCSIWSCAN, 0, pIoctlScan, 0, dev->priv_flags);
        printk("error after %d\n",__LINE__);
         sprintf(ext, "OK");
         DBGPRINT(RT_DEBUG_TRACE, ("%s, handle SCAN-PASSIVE command \n", __func__));
#endif
	}
	else
	{
		goto FREE_EXT;
	}

	if (copy_to_user(dwrq->pointer, ext, min(dwrq->length, (UINT16)(strlen(ext)+1)) ) )
		ret = -EFAULT;

FREE_EXT:

	kfree(ext);

	return ret;
}

static const iw_handler rt_handler[] =
{
	(iw_handler) NULL,			            /* SIOCSIWCOMMIT */
	(iw_handler) rt_ioctl_giwname,			/* SIOCGIWNAME   */
	(iw_handler) NULL,			            /* SIOCSIWNWID   */
	(iw_handler) NULL,			            /* SIOCGIWNWID   */
	(iw_handler) rt_ioctl_siwfreq,		    /* SIOCSIWFREQ   */
	(iw_handler) rt_ioctl_giwfreq,		    /* SIOCGIWFREQ   */
	(iw_handler) rt_ioctl_siwmode,		    /* SIOCSIWMODE   */
	(iw_handler) rt_ioctl_giwmode,		    /* SIOCGIWMODE   */
	(iw_handler) NULL,		                /* SIOCSIWSENS   */
	(iw_handler) NULL,		                /* SIOCGIWSENS   */
	(iw_handler) NULL /* not used */,		/* SIOCSIWRANGE  */
	(iw_handler) rt_ioctl_giwrange,		    /* SIOCGIWRANGE  */
	(iw_handler) andriod_handle_private, /* not used */		/* SIOCSIWPRIV   */
	(iw_handler) NULL /* kernel code */,    /* SIOCGIWPRIV   */
	(iw_handler) NULL /* not used */,		/* SIOCSIWSTATS  */
	(iw_handler) rt28xx_get_wireless_stats /* kernel code */,    /* SIOCGIWSTATS  */
	(iw_handler) NULL,		                /* SIOCSIWSPY    */
	(iw_handler) NULL,		                /* SIOCGIWSPY    */
	(iw_handler) NULL,				        /* SIOCSIWTHRSPY */
	(iw_handler) NULL,				        /* SIOCGIWTHRSPY */
	(iw_handler) rt_ioctl_siwap,            /* SIOCSIWAP     */
	(iw_handler) rt_ioctl_giwap,		    /* SIOCGIWAP     */
#ifdef SIOCSIWMLME
	(iw_handler) rt_ioctl_siwmlme,	        /* SIOCSIWMLME   */
#else
	(iw_handler) NULL,				        /* SIOCSIWMLME */
#endif /* SIOCSIWMLME */
	(iw_handler) rt_ioctl_iwaplist,		    /* SIOCGIWAPLIST */
#ifdef SIOCGIWSCAN
	(iw_handler) rt_ioctl_siwscan,		    /* SIOCSIWSCAN   */
	(iw_handler) rt_ioctl_giwscan,		    /* SIOCGIWSCAN   */
#else
	(iw_handler) NULL,				        /* SIOCSIWSCAN   */
	(iw_handler) NULL,				        /* SIOCGIWSCAN   */
#endif /* SIOCGIWSCAN */
	(iw_handler) rt_ioctl_siwessid,		    /* SIOCSIWESSID  */
	(iw_handler) rt_ioctl_giwessid,		    /* SIOCGIWESSID  */
	(iw_handler) rt_ioctl_siwnickn,		    /* SIOCSIWNICKN  */
	(iw_handler) rt_ioctl_giwnickn,		    /* SIOCGIWNICKN  */
	(iw_handler) NULL,				        /* -- hole --    */
	(iw_handler) NULL,				        /* -- hole --    */
	(iw_handler) rt_ioctl_siwrate,          /* SIOCSIWRATE   */
	(iw_handler) rt_ioctl_giwrate,          /* SIOCGIWRATE   */
	(iw_handler) rt_ioctl_siwrts,		    /* SIOCSIWRTS    */
	(iw_handler) rt_ioctl_giwrts,		    /* SIOCGIWRTS    */
	(iw_handler) rt_ioctl_siwfrag,		    /* SIOCSIWFRAG   */
	(iw_handler) rt_ioctl_giwfrag,		    /* SIOCGIWFRAG   */
	(iw_handler) NULL,		                /* SIOCSIWTXPOW  */
	(iw_handler) NULL,		                /* SIOCGIWTXPOW  */
	(iw_handler) NULL,		                /* SIOCSIWRETRY  */
	(iw_handler) NULL,		                /* SIOCGIWRETRY  */
	(iw_handler) rt_ioctl_siwencode,		/* SIOCSIWENCODE */
	(iw_handler) rt_ioctl_giwencode,		/* SIOCGIWENCODE */
	(iw_handler) NULL,		                /* SIOCSIWPOWER  */
	(iw_handler) NULL,		                /* SIOCGIWPOWER  */
	(iw_handler) NULL,						/* -- hole -- */	
	(iw_handler) NULL,						/* -- hole -- */
#if WIRELESS_EXT > 17	
    (iw_handler) rt_ioctl_siwgenie,         /* SIOCSIWGENIE  */
	(iw_handler) rt_ioctl_giwgenie,         /* SIOCGIWGENIE  */
	(iw_handler) rt_ioctl_siwauth,		    /* SIOCSIWAUTH   */
	(iw_handler) rt_ioctl_giwauth,		    /* SIOCGIWAUTH   */
	(iw_handler) rt_ioctl_siwencodeext,	    /* SIOCSIWENCODEEXT */
	(iw_handler) rt_ioctl_giwencodeext,		/* SIOCGIWENCODEEXT */
	(iw_handler) rt_ioctl_siwpmksa,         /* SIOCSIWPMKSA  */
#endif
};

static const iw_handler rt_priv_handlers[] = {
	(iw_handler) NULL, /* + 0x00 */
	(iw_handler) NULL, /* + 0x01 */
	(iw_handler) rt_ioctl_setparam, /* + 0x02 */
#ifdef DBG	
	(iw_handler) rt_private_ioctl_bbp, /* + 0x03 */	
#else
	(iw_handler) NULL, /* + 0x03 */
#endif
	(iw_handler) NULL, /* + 0x04 */
	(iw_handler) NULL, /* + 0x05 */
	(iw_handler) NULL, /* + 0x06 */
	(iw_handler) NULL, /* + 0x07 */
	(iw_handler) NULL, /* + 0x08 */
	(iw_handler) rt_private_get_statistics, /* + 0x09 */
	(iw_handler) NULL, /* + 0x0A */
	(iw_handler) NULL, /* + 0x0B */
	(iw_handler) NULL, /* + 0x0C */
	(iw_handler) NULL, /* + 0x0D */
	(iw_handler) NULL, /* + 0x0E */
	(iw_handler) NULL, /* + 0x0F */
	(iw_handler) NULL, /* + 0x10 */
	(iw_handler) rt_private_show, /* + 0x11 */
    (iw_handler) NULL, /* + 0x12 */
	(iw_handler) NULL, /* + 0x13 */
    (iw_handler) NULL, /* + 0x14 */
	(iw_handler) NULL, /* + 0x15 */
    (iw_handler) NULL, /* + 0x16 */
	(iw_handler) NULL, /* + 0x17 */
	(iw_handler) NULL, /* + 0x18 */
};

const struct iw_handler_def rt28xx_iw_handler_def =
{
#define	N(a)	(sizeof (a) / sizeof (a[0]))
	.standard	= (iw_handler *) rt_handler,
	.num_standard	= sizeof(rt_handler) / sizeof(iw_handler),
#ifdef CONFIG_WEXT_PRIV	
	.private	= (iw_handler *) rt_priv_handlers,
	.num_private		= N(rt_priv_handlers),
	.private_args	= (struct iw_priv_args *) privtab,
	.num_private_args	= N(privtab),
#endif 
#if IW_HANDLER_VERSION >= 7
    .get_wireless_stats = rt28xx_get_wireless_stats,
#endif 
};


INT rt28xx_sta_ioctl(
	IN	struct net_device	*net_dev, 
	IN	OUT	struct ifreq	*rq, 
	IN	INT					cmd)
{
/*	POS_COOKIE			pObj; */
	VOID        		*pAd = NULL;
	struct iwreq        *wrqin = (struct iwreq *) rq;
	RTMP_IOCTL_INPUT_STRUCT rt_wrq, *wrq = &rt_wrq;
/*	BOOLEAN				StateMachineTouched = FALSE; */
	INT					Status = NDIS_STATUS_SUCCESS;
	USHORT				subcmd;
	UINT32				org_len;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	wrq->u.data.pointer = wrqin->u.data.pointer;
	wrq->u.data.length = wrqin->u.data.length;
	org_len = wrq->u.data.length;

/*	pObj = (POS_COOKIE) pAd->OS_Cookie; */
	
    /*check if the interface is down */
/*    if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) */
	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
    {
#ifdef CONFIG_APSTA_MIXED_SUPPORT
	    if (wrqin->u.data.pointer == NULL)
	    {
		    return Status;
	    }

		if (cmd == RTPRIV_IOCTL_SET)
		{
	    if (strstr(wrqin->u.data.pointer, "OpMode") == NULL)
				return -ENETDOWN;
		}
		else
#endif /* CONFIG_APSTA_MIXED_SUPPORT */
		{
            DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		    return -ENETDOWN;  
        }
    }



	switch(cmd)
	{			
#ifdef RALINK_ATE
#ifdef RALINK_QA
		case RTPRIV_IOCTL_ATE:
			{
				/*
					ATE is always controlled by ra0
				*/
				RTMP_COM_IoctlHandle(pAd, wrq, CMD_RTPRIV_IOCTL_ATE, 0, wrqin->ifr_name, 0);
			}
			break;
#endif /* RALINK_QA */ 
#endif /* RALINK_ATE */
        case SIOCGIFHWADDR:
			DBGPRINT(RT_DEBUG_TRACE, ("IOCTL::SIOCGIFHWADDR\n"));
/*			memcpy(wrqin->u.name, pAd->CurrentAddress, ETH_ALEN); */
			RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIFHWADDR,
								0, wrqin->u.name, 0, net_dev->priv_flags);
			break;	
		case SIOCGIWNAME:
        {
        	char *name=&wrqin->u.name[0];
        	rt_ioctl_giwname(net_dev, NULL, name, NULL);
            break;
		}
		case SIOCGIWESSID:  /*Get ESSID */
        {
        	struct iw_point *essid=&wrqin->u.essid;
        	rt_ioctl_giwessid(net_dev, NULL, essid, essid->pointer);
            break;
		}
		case SIOCSIWESSID:  /*Set ESSID */
        	{
        	struct iw_point	*essid=&wrqin->u.essid;
        	rt_ioctl_siwessid(net_dev, NULL, essid, essid->pointer);
            break;  
		}
		case SIOCSIWNWID:   /* set network id (the cell) */
		case SIOCGIWNWID:   /* get network id */
			Status = -EOPNOTSUPP;
			break;
		case SIOCSIWFREQ:   /*set channel/frequency (Hz) */
        	{
        	struct iw_freq *freq=&wrqin->u.freq;
        	rt_ioctl_siwfreq(net_dev, NULL, freq, NULL);
			break;
		}
		case SIOCGIWFREQ:   /* get channel/frequency (Hz) */
        	{
        	struct iw_freq *freq=&wrqin->u.freq;
        	rt_ioctl_giwfreq(net_dev, NULL, freq, NULL);
			break;
		}
		case SIOCSIWNICKN: /*set node name/nickname */
        	{
        	/*struct iw_point *data=&wrq->u.data; */
        	/*rt_ioctl_siwnickn(net_dev, NULL, data, NULL); */
			break;
			}
		case SIOCGIWNICKN: /*get node name/nickname */
        {
			RT_CMD_STA_IOCTL_NICK_NAME NickName, *pNickName = &NickName;
			CHAR nickname[IW_ESSID_MAX_SIZE+1];
			struct iw_point	*erq = NULL;
        	erq = &wrqin->u.data;

			pNickName->NameLen = IW_ESSID_MAX_SIZE+1;
			pNickName->pName = (CHAR *)nickname;
			RTMP_STA_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_STA_SIOCGIWNICKN, 0,
							pNickName, 0, net_dev->priv_flags);

            erq->length = pNickName->NameLen; /*strlen((PSTRING) pAd->nickname); */
            Status = copy_to_user(erq->pointer, nickname, erq->length);
			break;
		}
		case SIOCGIWRATE:   /*get default bit rate (bps) */
		    rt_ioctl_giwrate(net_dev, NULL, &wrqin->u, NULL);
            break;
	    case SIOCSIWRATE:  /*set default bit rate (bps) */
	        rt_ioctl_siwrate(net_dev, NULL, &wrqin->u, NULL);
            break;
        case SIOCGIWRTS:  /* get RTS/CTS threshold (bytes) */
        	{
        	struct iw_param *rts=&wrqin->u.rts;
        	rt_ioctl_giwrts(net_dev, NULL, rts, NULL);
            break;
		}
        case SIOCSIWRTS:  /*set RTS/CTS threshold (bytes) */
        	{
        	struct iw_param *rts=&wrqin->u.rts;
        	rt_ioctl_siwrts(net_dev, NULL, rts, NULL);
            break;
		}
        case SIOCGIWFRAG:  /*get fragmentation thr (bytes) */
        	{
        	struct iw_param *frag=&wrqin->u.frag;
        	rt_ioctl_giwfrag(net_dev, NULL, frag, NULL);
            break;
		}
        case SIOCSIWFRAG:  /*set fragmentation thr (bytes) */
        	{
        	struct iw_param *frag=&wrqin->u.frag;
        	rt_ioctl_siwfrag(net_dev, NULL, frag, NULL);
            break;
		}
        case SIOCGIWENCODE:  /*get encoding token & mode */
        	{
        	struct iw_point *erq=&wrqin->u.encoding;
        	if(erq)
        		rt_ioctl_giwencode(net_dev, NULL, erq, erq->pointer);
            break;
		}
        case SIOCSIWENCODE:  /*set encoding token & mode */
        	{
        	struct iw_point *erq=&wrqin->u.encoding;
        	if(erq)
        		rt_ioctl_siwencode(net_dev, NULL, erq, erq->pointer);
            break;
		}
		case SIOCGIWAP:     /*get access point MAC addresses */
        	{
        	struct sockaddr *ap_addr=&wrqin->u.ap_addr;
        	rt_ioctl_giwap(net_dev, NULL, ap_addr, ap_addr->sa_data);
			break;
		}
	    case SIOCSIWAP:  /*set access point MAC addresses */
        	{
        	struct sockaddr *ap_addr=&wrqin->u.ap_addr;
        	rt_ioctl_siwap(net_dev, NULL, ap_addr, ap_addr->sa_data);
            break;
		}
		case SIOCGIWMODE:   /*get operation mode */
        	{
        	__u32 *mode=&wrqin->u.mode;
        	rt_ioctl_giwmode(net_dev, NULL, mode, NULL);
            break;
		}
		case SIOCSIWMODE:   /*set operation mode */
        	{
        	__u32 *mode=&wrqin->u.mode;
        	rt_ioctl_siwmode(net_dev, NULL, mode, NULL);
            break;
		}
		case SIOCGIWSENS:   /*get sensitivity (dBm) */
		case SIOCSIWSENS:	/*set sensitivity (dBm) */
		case SIOCGIWPOWER:  /*get Power Management settings */
		case SIOCSIWPOWER:  /*set Power Management settings */
		case SIOCGIWTXPOW:  /*get transmit power (dBm) */
		case SIOCSIWTXPOW:  /*set transmit power (dBm) */
		case SIOCGIWRANGE:	/*Get range of parameters */
		case SIOCGIWRETRY:	/*get retry limits and lifetime */
		case SIOCSIWRETRY:	/*set retry limits and lifetime */
			Status = -EOPNOTSUPP;
			break;

		case RT_PRIV_IOCTL:
        case RT_PRIV_IOCTL_EXT:
			subcmd = wrqin->u.data.flags;

			Status = RTMP_STA_IoctlHandle(pAd, wrq, CMD_RT_PRIV_IOCTL, subcmd, NULL, 0, net_dev->priv_flags);
			break;		
		case SIOCGIWPRIV:
			if (wrqin->u.data.pointer) 
			{
				if ( access_ok(VERIFY_WRITE, wrqin->u.data.pointer, sizeof(privtab)) != TRUE)
					break;
				if ((sizeof(privtab) / sizeof(privtab[0])) <= wrq->u.data.length)
				{
					wrqin->u.data.length = sizeof(privtab) / sizeof(privtab[0]);
					if (copy_to_user(wrqin->u.data.pointer, privtab, sizeof(privtab)))
						Status = -EFAULT;
				}
				else
					Status = -E2BIG;
			}
			break;
		case RTPRIV_IOCTL_SET:
			if(access_ok(VERIFY_READ, wrqin->u.data.pointer, wrqin->u.data.length) != TRUE)   
					break;
			rt_ioctl_setparam(net_dev, NULL, NULL, wrqin->u.data.pointer);
			break;
		case RTPRIV_IOCTL_GSITESURVEY:
			RTMP_STA_IoctlHandle(pAd, wrq, CMD_RTPRIV_IOCTL_SITESURVEY_GET, 0, NULL, 0, net_dev->priv_flags);
/*			RTMPIoctlGetSiteSurvey(pAd, wrq); */
		    break;			
#ifdef DBG
		case RTPRIV_IOCTL_MAC:
			RTMP_STA_IoctlHandle(pAd, wrq, CMD_RTPRIV_IOCTL_MAC, 0, NULL, 0, net_dev->priv_flags);
/*			RTMPIoctlMAC(pAd, wrq); */
			break;
		case RTPRIV_IOCTL_E2P:
			RTMP_STA_IoctlHandle(pAd, wrq, CMD_RTPRIV_IOCTL_E2P, 0, NULL, 0, net_dev->priv_flags);
/*			RTMPIoctlE2PROM(pAd, wrq); */
			break;
#ifdef RTMP_RF_RW_SUPPORT
		case RTPRIV_IOCTL_RF:
			RTMP_STA_IoctlHandle(pAd, wrq, CMD_RTPRIV_IOCTL_RF, 0, NULL, 0, net_dev->priv_flags);
/*			RTMPIoctlRF(pAd, wrq); */
			break;
#endif /* RTMP_RF_RW_SUPPORT */
#endif /* DBG */

        case SIOCETHTOOL:
                break;
		default:
			DBGPRINT(RT_DEBUG_ERROR, ("IOCTL::unknown IOCTL's cmd = 0x%08x\n", cmd));
			Status = -EOPNOTSUPP;
			break;
	}

/*    if(StateMachineTouched) // Upper layer sent a MLME-related operations */
/*    	RTMP_MLME_HANDLER(pAd); */

	if (Status != 0)
	{
		RT_CMD_STATUS_TRANSLATE(Status);
	}
	else
	{
		/*
			If wrq length is modified, we reset the lenght of origin wrq;

			Or we can not modify it because the address of wrq->u.data.length
			maybe same as other union field, ex: iw_range, etc.

			if the length is not changed but we change it, the value for other
			union will also be changed, this is not correct.
		*/
		if (wrq->u.data.length != org_len)
			wrqin->u.data.length = wrq->u.data.length;
	}

	return Status;
}

