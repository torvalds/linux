/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _RTW_HWMP_C_

#ifdef CONFIG_RTW_MESH
#include <drv_types.h>
#include <hal_data.h>

#define RTW_TEST_FRAME_LEN	8192
#define RTW_MAX_METRIC	0xffffffff
#define RTW_ARITH_SHIFT	8
#define RTW_LINK_FAIL_THRESH 95
#define RTW_MAX_PREQ_QUEUE_LEN	64
#define RTW_ATLM_REQ_CYCLE 1000

#define rtw_ilog2(n)			\
(					\
	(n) < 2 ? 0 :			\
	(n) & (1ULL << 63) ? 63 :	\
	(n) & (1ULL << 62) ? 62 :	\
	(n) & (1ULL << 61) ? 61 :	\
	(n) & (1ULL << 60) ? 60 :	\
	(n) & (1ULL << 59) ? 59 :	\
	(n) & (1ULL << 58) ? 58 :	\
	(n) & (1ULL << 57) ? 57 :	\
	(n) & (1ULL << 56) ? 56 :	\
	(n) & (1ULL << 55) ? 55 :	\
	(n) & (1ULL << 54) ? 54 :	\
	(n) & (1ULL << 53) ? 53 :	\
	(n) & (1ULL << 52) ? 52 :	\
	(n) & (1ULL << 51) ? 51 :	\
	(n) & (1ULL << 50) ? 50 :	\
	(n) & (1ULL << 49) ? 49 :	\
	(n) & (1ULL << 48) ? 48 :	\
	(n) & (1ULL << 47) ? 47 :	\
	(n) & (1ULL << 46) ? 46 :	\
	(n) & (1ULL << 45) ? 45 :	\
	(n) & (1ULL << 44) ? 44 :	\
	(n) & (1ULL << 43) ? 43 :	\
	(n) & (1ULL << 42) ? 42 :	\
	(n) & (1ULL << 41) ? 41 :	\
	(n) & (1ULL << 40) ? 40 :	\
	(n) & (1ULL << 39) ? 39 :	\
	(n) & (1ULL << 38) ? 38 :	\
	(n) & (1ULL << 37) ? 37 :	\
	(n) & (1ULL << 36) ? 36 :	\
	(n) & (1ULL << 35) ? 35 :	\
	(n) & (1ULL << 34) ? 34 :	\
	(n) & (1ULL << 33) ? 33 :	\
	(n) & (1ULL << 32) ? 32 :	\
	(n) & (1ULL << 31) ? 31 :	\
	(n) & (1ULL << 30) ? 30 :	\
	(n) & (1ULL << 29) ? 29 :	\
	(n) & (1ULL << 28) ? 28 :	\
	(n) & (1ULL << 27) ? 27 :	\
	(n) & (1ULL << 26) ? 26 :	\
	(n) & (1ULL << 25) ? 25 :	\
	(n) & (1ULL << 24) ? 24 :	\
	(n) & (1ULL << 23) ? 23 :	\
	(n) & (1ULL << 22) ? 22 :	\
	(n) & (1ULL << 21) ? 21 :	\
	(n) & (1ULL << 20) ? 20 :	\
	(n) & (1ULL << 19) ? 19 :	\
	(n) & (1ULL << 18) ? 18 :	\
	(n) & (1ULL << 17) ? 17 :	\
	(n) & (1ULL << 16) ? 16 :	\
	(n) & (1ULL << 15) ? 15 :	\
	(n) & (1ULL << 14) ? 14 :	\
	(n) & (1ULL << 13) ? 13 :	\
	(n) & (1ULL << 12) ? 12 :	\
	(n) & (1ULL << 11) ? 11 :	\
	(n) & (1ULL << 10) ? 10 :	\
	(n) & (1ULL <<  9) ?  9 :	\
	(n) & (1ULL <<  8) ?  8 :	\
	(n) & (1ULL <<  7) ?  7 :	\
	(n) & (1ULL <<  6) ?  6 :	\
	(n) & (1ULL <<  5) ?  5 :	\
	(n) & (1ULL <<  4) ?  4 :	\
	(n) & (1ULL <<  3) ?  3 :	\
	(n) & (1ULL <<  2) ?  2 :	\
	1				\
)

enum rtw_mpath_frame_type {
	RTW_MPATH_PREQ = 0,
	RTW_MPATH_PREP,
	RTW_MPATH_PERR,
	RTW_MPATH_RANN
};

static inline u32 rtw_u32_field_get(const u8 *preq_elem, int shift, BOOLEAN ae)
{
	if (ae)
		shift += 6;
	return LE_BITS_TO_4BYTE(preq_elem + shift, 0, 32);
}

static inline u16 rtw_u16_field_get(const u8 *preq_elem, int shift, BOOLEAN ae)
{
	if (ae)
		shift += 6;
	return LE_BITS_TO_2BYTE(preq_elem + shift, 0, 16);
}

/* HWMP IE processing macros */
#define RTW_AE_F			(1<<6)
#define RTW_AE_F_SET(x)			(*x & RTW_AE_F)
#define RTW_PREQ_IE_FLAGS(x)		(*(x))
#define RTW_PREQ_IE_HOPCOUNT(x)		(*(x + 1))
#define RTW_PREQ_IE_TTL(x)		(*(x + 2))
#define RTW_PREQ_IE_PREQ_ID(x)		rtw_u32_field_get(x, 3, 0)
#define RTW_PREQ_IE_ORIG_ADDR(x)	(x + 7)
#define RTW_PREQ_IE_ORIG_SN(x)		rtw_u32_field_get(x, 13, 0)
#define RTW_PREQ_IE_LIFETIME(x)		rtw_u32_field_get(x, 17, RTW_AE_F_SET(x))
#define RTW_PREQ_IE_METRIC(x) 		rtw_u32_field_get(x, 21, RTW_AE_F_SET(x))
#define RTW_PREQ_IE_TARGET_F(x)		(*(RTW_AE_F_SET(x) ? x + 32 : x + 26))
#define RTW_PREQ_IE_TARGET_ADDR(x) 	(RTW_AE_F_SET(x) ? x + 33 : x + 27)
#define RTW_PREQ_IE_TARGET_SN(x) 	rtw_u32_field_get(x, 33, RTW_AE_F_SET(x))

#define RTW_PREP_IE_FLAGS(x)		RTW_PREQ_IE_FLAGS(x)
#define RTW_PREP_IE_HOPCOUNT(x)		RTW_PREQ_IE_HOPCOUNT(x)
#define RTW_PREP_IE_TTL(x)		RTW_PREQ_IE_TTL(x)
#define RTW_PREP_IE_ORIG_ADDR(x)	(RTW_AE_F_SET(x) ? x + 27 : x + 21)
#define RTW_PREP_IE_ORIG_SN(x)		rtw_u32_field_get(x, 27, RTW_AE_F_SET(x))
#define RTW_PREP_IE_LIFETIME(x)		rtw_u32_field_get(x, 13, RTW_AE_F_SET(x))
#define RTW_PREP_IE_METRIC(x)		rtw_u32_field_get(x, 17, RTW_AE_F_SET(x))
#define RTW_PREP_IE_TARGET_ADDR(x)	(x + 3)
#define RTW_PREP_IE_TARGET_SN(x)	rtw_u32_field_get(x, 9, 0)

#define RTW_PERR_IE_TTL(x)		(*(x))
#define RTW_PERR_IE_TARGET_FLAGS(x)	(*(x + 2))
#define RTW_PERR_IE_TARGET_ADDR(x)	(x + 3)
#define RTW_PERR_IE_TARGET_SN(x)	rtw_u32_field_get(x, 9, 0)
#define RTW_PERR_IE_TARGET_RCODE(x)	rtw_u16_field_get(x, 13, 0)

#define RTW_TU_TO_SYSTIME(x)	(rtw_us_to_systime((x) * 1024))
#define RTW_TU_TO_EXP_TIME(x)	(rtw_get_current_time() + RTW_TU_TO_SYSTIME(x))
#define RTW_MSEC_TO_TU(x) (x*1000/1024)
#define RTW_SN_GT(x, y) ((s32)(y - x) < 0)
#define RTW_SN_LT(x, y) ((s32)(x - y) < 0)
#define RTW_MAX_SANE_SN_DELTA 32

static inline u32 RTW_SN_DELTA(u32 x, u32 y)
{
	return x >= y ? x - y : y - x;
}

#define rtw_net_traversal_jiffies(adapter) \
	rtw_ms_to_systime(adapter->mesh_cfg.dot11MeshHWMPnetDiameterTraversalTime)
#define rtw_default_lifetime(adapter) \
	RTW_MSEC_TO_TU(adapter->mesh_cfg.dot11MeshHWMPactivePathTimeout)
#define rtw_min_preq_int_jiff(adapter) \
	(rtw_ms_to_systime(adapter->mesh_cfg.dot11MeshHWMPpreqMinInterval))
#define rtw_max_preq_retries(adapter) (adapter->mesh_cfg.dot11MeshHWMPmaxPREQretries)
#define rtw_disc_timeout_jiff(adapter) \
	rtw_ms_to_systime(adapter->mesh_cfg.min_discovery_timeout)
#define rtw_root_path_confirmation_jiffies(adapter) \
	rtw_ms_to_systime(adapter->mesh_cfg.dot11MeshHWMPconfirmationInterval)

static inline BOOLEAN rtw_ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
	return _rtw_memcmp(addr1, addr2, ETH_ALEN);
}

#ifdef PLATFORM_LINUX
#define rtw_print_ratelimit()	printk_ratelimit()
#define rtw_mod_timer(ptimer, expires) mod_timer(&(ptimer)->timer, expires)
#else

#endif

#define RTW_MESH_EWMA_PRECISION 20
#define RTW_MESH_EWMA_WEIGHT_RCP 8
#define RTW_TOTAL_PKT_MIN_THRESHOLD 1
inline void rtw_ewma_err_rate_init(struct rtw_ewma_err_rate *e)
{
	e->internal = 0;
}
inline unsigned long rtw_ewma_err_rate_read(struct rtw_ewma_err_rate *e)
{
	return e->internal >> (RTW_MESH_EWMA_PRECISION);
}
inline void rtw_ewma_err_rate_add(struct rtw_ewma_err_rate *e,
				  unsigned long val)
{
	unsigned long internal = e->internal;
	unsigned long weight_rcp = rtw_ilog2(RTW_MESH_EWMA_WEIGHT_RCP);
	unsigned long precision = RTW_MESH_EWMA_PRECISION;

	(e->internal) = internal ? (((internal << weight_rcp) - internal) +
			(val << precision)) >> weight_rcp :
			(val << precision);
}

static const u8 bcast_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static int rtw_mesh_path_sel_frame_tx(enum rtw_mpath_frame_type mpath_action, u8 flags,
				      const u8 *originator_addr, u32 originator_sn,
				      u8 target_flags, const u8 *target,
				      u32 target_sn, const u8 *da, u8 hopcount, u8 ttl,
				      u32 lifetime, u32 metric, u32 preq_id, 
				      _adapter *adapter)
{
	struct xmit_priv *pxmitpriv = &(adapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);
	struct xmit_frame *pmgntframe = NULL;
	struct rtw_ieee80211_hdr *pwlanhdr = NULL;
	struct pkt_attrib *pattrib = NULL;
	u8 category = RTW_WLAN_CATEGORY_MESH;
	u8 action = RTW_ACT_MESH_HWMP_PATH_SELECTION;
	u16 *fctrl = NULL;
	u8 *pos, ie_len;


	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return -1;

	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, pattrib);
	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pos = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pos;


	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(adapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(adapter), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pos, WIFI_ACTION);

	pos += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pos = rtw_set_fixed_ie(pos, 1, &(category), &(pattrib->pktlen));
	pos = rtw_set_fixed_ie(pos, 1, &(action), &(pattrib->pktlen));

	switch (mpath_action) {
	case RTW_MPATH_PREQ:
		RTW_HWMP_DBG("sending PREQ to "MAC_FMT"\n", MAC_ARG(target));
		ie_len = 37;
		pattrib->pktlen += (ie_len + 2);
		*pos++ = WLAN_EID_PREQ;
		break;
	case RTW_MPATH_PREP:
		RTW_HWMP_DBG("sending PREP to "MAC_FMT"\n", MAC_ARG(originator_addr));
		ie_len = 31;
		pattrib->pktlen += (ie_len + 2);
		*pos++ = WLAN_EID_PREP;
		break;
	case RTW_MPATH_RANN:
		RTW_HWMP_DBG("sending RANN from "MAC_FMT"\n", MAC_ARG(originator_addr));
		ie_len = sizeof(struct rtw_ieee80211_rann_ie);
		pattrib->pktlen += (ie_len + 2);
		*pos++ = WLAN_EID_RANN;
		break;
	default:
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		return _FAIL;
	}
	*pos++ = ie_len;
	*pos++ = flags;
	*pos++ = hopcount;
	*pos++ = ttl;
	if (mpath_action == RTW_MPATH_PREP) {
		_rtw_memcpy(pos, target, ETH_ALEN);
		pos += ETH_ALEN;
		*(u32 *)pos = cpu_to_le32(target_sn);
		pos += 4;
	} else {
		if (mpath_action == RTW_MPATH_PREQ) {
			*(u32 *)pos = cpu_to_le32(preq_id);
			pos += 4;
		}
		_rtw_memcpy(pos, originator_addr, ETH_ALEN);
		pos += ETH_ALEN;
		*(u32 *)pos = cpu_to_le32(originator_sn);
		pos += 4;
	}
	*(u32 *)pos = cpu_to_le32(lifetime);
	pos += 4;
	*(u32 *)pos = cpu_to_le32(metric);
	pos += 4;
	if (mpath_action == RTW_MPATH_PREQ) {
		*pos++ = 1; /* support only 1 destination now */
		*pos++ = target_flags;
		_rtw_memcpy(pos, target, ETH_ALEN);
		pos += ETH_ALEN;
		*(u32 *)pos = cpu_to_le32(target_sn);
		pos += 4;
	} else if (mpath_action == RTW_MPATH_PREP) {
		_rtw_memcpy(pos, originator_addr, ETH_ALEN);
		pos += ETH_ALEN;
		*(u32 *)pos = cpu_to_le32(originator_sn);
		pos += 4;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(adapter, pmgntframe);
	return 0;
}

int rtw_mesh_path_error_tx(_adapter *adapter,
			   u8 ttl, const u8 *target, u32 target_sn,
			   u16 perr_reason_code, const u8 *ra)
{

	struct xmit_priv *pxmitpriv = &(adapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);
	struct xmit_frame *pmgntframe = NULL;
	struct rtw_ieee80211_hdr *pwlanhdr = NULL;
	struct pkt_attrib *pattrib = NULL;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	u8 category = RTW_WLAN_CATEGORY_MESH;
	u8 action = RTW_ACT_MESH_HWMP_PATH_SELECTION;
	u8 *pos, ie_len;
	u16 *fctrl = NULL;

	if (rtw_time_before(rtw_get_current_time(), minfo->next_perr))
		return -1;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return -1;

	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, pattrib);
	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pos = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pos;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(adapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(adapter), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pos, WIFI_ACTION);

	pos += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pos = rtw_set_fixed_ie(pos, 1, &(category), &(pattrib->pktlen));
	pos = rtw_set_fixed_ie(pos, 1, &(action), &(pattrib->pktlen));

	ie_len = 15;
	pattrib->pktlen += (2 + ie_len);
	*pos++ = WLAN_EID_PERR;
	*pos++ = ie_len;
	/* ttl */
	*pos++ = ttl;
	/* The Number of Destinations N */
	*pos++ = 1;
	/* Flags format | B7 | B6 | B5:B0 | = | rsvd | AE | rsvd | */
	*pos = 0;
	pos++;
	_rtw_memcpy(pos, target, ETH_ALEN);
	pos += ETH_ALEN;
	*(u32 *)pos = cpu_to_le32(target_sn);
	pos += 4;
	*(u16 *)pos = cpu_to_le16(perr_reason_code);

	adapter->mesh_info.next_perr = RTW_TU_TO_EXP_TIME(
				adapter->mesh_cfg.dot11MeshHWMPperrMinInterval);
	pattrib->last_txcmdsz = pattrib->pktlen;
	/* Send directly. Rewrite it if deferred tx is needed */
	dump_mgntframe(adapter, pmgntframe);

	RTW_HWMP_DBG("TX PERR toward "MAC_FMT", ra = "MAC_FMT"\n", MAC_ARG(target), MAC_ARG(ra));
	
	return 0;
}

static u32 rtw_get_vht_bitrate(u8 mcs, u8 bw, u8 nss, u8 sgi)
{
	static const u32 base[4][10] = {
		{   6500000,
		   13000000,
		   19500000,
		   26000000,
		   39000000,
		   52000000,
		   58500000,
		   65000000,
		   78000000,
		/* not in the spec, but some devices use this: */
		   86500000,
		},
		{  13500000,
		   27000000,
		   40500000,
		   54000000,
		   81000000,
		  108000000,
		  121500000,
		  135000000,
		  162000000,
		  180000000,
		},
		{  29300000,
		   58500000,
		   87800000,
		  117000000,
		  175500000,
		  234000000,
		  263300000,
		  292500000,
		  351000000,
		  390000000,
		},
		{  58500000,
		  117000000,
		  175500000,
		  234000000,
		  351000000,
		  468000000,
		  526500000,
		  585000000,
		  702000000,
		  780000000,
		},
	};
	u32 bitrate;
	int bw_idx;

	if (mcs > 9) {
		RTW_HWMP_INFO("Invalid mcs = %d\n", mcs);
		return 0;
	}

	if (nss > 4 || nss < 1) {
		RTW_HWMP_INFO("Now only support nss = 1, 2, 3, 4\n");
	}

	switch (bw) {
	case CHANNEL_WIDTH_160:
		bw_idx = 3;
		break;
	case CHANNEL_WIDTH_80:
		bw_idx = 2;
		break;
	case CHANNEL_WIDTH_40:
		bw_idx = 1;
		break;
	case CHANNEL_WIDTH_20:
		bw_idx = 0;
		break;
	default:
		RTW_HWMP_INFO("bw = %d currently not supported\n", bw);
		return 0;
	}

	bitrate = base[bw_idx][mcs];
	bitrate *= nss;

	if (sgi)
		bitrate = (bitrate / 9) * 10;

	/* do NOT round down here */
	return (bitrate + 50000) / 100000;
}

static u32 rtw_get_ht_bitrate(u8 mcs, u8 bw, u8 sgi)
{
	int modulation, streams, bitrate;

	/* the formula below does only work for MCS values smaller than 32 */
	if (mcs >= 32) {
		RTW_HWMP_INFO("Invalid mcs = %d\n", mcs);
		return 0;
	}

	if (bw > 1) {
		RTW_HWMP_INFO("Now HT only support bw = 0(20Mhz), 1(40Mhz)\n");
		return 0;
	}

	modulation = mcs & 7;
	streams = (mcs >> 3) + 1;

	bitrate = (bw == 1) ? 13500000 : 6500000;

	if (modulation < 4)
		bitrate *= (modulation + 1);
	else if (modulation == 4)
		bitrate *= (modulation + 2);
	else
		bitrate *= (modulation + 3);

	bitrate *= streams;

	if (sgi)
		bitrate = (bitrate / 9) * 10;

	/* do NOT round down here */
	return (bitrate + 50000) / 100000;
}

/**
 * @bw: 0(20Mhz), 1(40Mhz), 2(80Mhz), 3(160Mhz)
 * @rate_idx: DESC_RATEXXXX & 0x7f
 * @sgi: DESC_RATEXXXX >> 7
 * Returns: bitrate in 100kbps
 */
static u32 rtw_desc_rate_to_bitrate(u8 bw, u8 rate_idx, u8 sgi)
{
	u32 bitrate;

	if (rate_idx <= DESC_RATE54M){
		u16 ofdm_rate[12] = {10, 20, 55, 110,
			60, 90, 120, 180, 240, 360, 480, 540};
		bitrate = ofdm_rate[rate_idx];
	} else if ((DESC_RATEMCS0 <= rate_idx) &&
		   (rate_idx <= DESC_RATEMCS31)) {
		u8 mcs = rate_idx - DESC_RATEMCS0;
		bitrate = rtw_get_ht_bitrate(mcs, bw, sgi);
	} else if ((DESC_RATEVHTSS1MCS0 <= rate_idx) &&
		   (rate_idx <= DESC_RATEVHTSS4MCS9)) {
		u8 mcs = (rate_idx - DESC_RATEVHTSS1MCS0) % 10;
		u8 nss = ((rate_idx - DESC_RATEVHTSS1MCS0) / 10) + 1;
		bitrate = rtw_get_vht_bitrate(mcs, bw, nss, sgi);
	} else {
		/* 60Ghz ??? */
		bitrate = 1;
	}

	return bitrate;
}

static u32 rtw_airtime_link_metric_get(_adapter *adapter, struct sta_info *sta)
{
	struct dm_struct *dm = adapter_to_phydm(adapter);
	int device_constant = phydm_get_plcp(dm, sta->cmn.mac_id) << RTW_ARITH_SHIFT;
	u32 test_frame_len = RTW_TEST_FRAME_LEN << RTW_ARITH_SHIFT;
	u32 s_unit = 1 << RTW_ARITH_SHIFT;
	u32 err;
	u16 rate;
	u32 tx_time, estimated_retx;
	u64 result;
	/* The fail_avg should <= 100 here */
	u32 fail_avg = (u32)rtw_ewma_err_rate_read(&sta->metrics.err_rate);

	if (fail_avg > RTW_LINK_FAIL_THRESH)
		return RTW_MAX_METRIC;

	rate = sta->metrics.data_rate;
	/* rate unit is 100Kbps, min rate = 10 */
	if (rate < 10) {
		RTW_HWMP_INFO("rate = %d\n", rate);
		return RTW_MAX_METRIC;
	}

	err = (fail_avg << RTW_ARITH_SHIFT) / 100;

	/* test_frame_len*10 to adjust the unit of rate(100kbps/unit) */
	tx_time = (device_constant + 10 * test_frame_len / rate);
	estimated_retx = ((1 << (2 * RTW_ARITH_SHIFT)) / (s_unit - err));
	result = (tx_time * estimated_retx) >> (2 * RTW_ARITH_SHIFT);
	/* Convert us to 0.01 TU(10.24us). x/10.24 = x*100/1024 */
	result = (result * 100) >> 10;

	return (u32)result;
}

void rtw_ieee80211s_update_metric(_adapter *adapter, u8 mac_id,
				  u8 per, u8 rate,
				  u8 bw, u8 total_pkt)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	struct sta_info *sta;
	u8 rate_idx;
	u8 sgi;

	sta = macid_ctl->sta[mac_id];
	if (!sta)
		return;

	/* if RA, use reported rate */
	if (adapter->fix_rate == 0xff) {
		rate_idx = rate & 0x7f;
		sgi = rate >> 7;
	} else {
		rate_idx = adapter->fix_rate & 0x7f;
		sgi = adapter->fix_rate >> 7;
	}
	sta->metrics.data_rate = rtw_desc_rate_to_bitrate(bw, rate_idx, sgi);

	if (total_pkt < RTW_TOTAL_PKT_MIN_THRESHOLD)
		return;

	/* TBD: sta->metrics.overhead = phydm_get_plcp(void *dm_void, u16 macid); */
	sta->metrics.total_pkt = total_pkt;

	rtw_ewma_err_rate_add(&sta->metrics.err_rate, per);
	if (rtw_ewma_err_rate_read(&sta->metrics.err_rate) > 
			RTW_LINK_FAIL_THRESH)
		rtw_mesh_plink_broken(sta);
}

static void rtw_hwmp_preq_frame_process(_adapter *adapter,
					struct rtw_ieee80211_hdr_3addr *mgmt,
					const u8 *preq_elem, u32 originator_metric)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct rtw_mesh_cfg *mshcfg = &adapter->mesh_cfg;
	struct rtw_mesh_path *path = NULL;
	const u8 *target_addr, *originator_addr;
	const u8 *da;
	u8 target_flags, ttl, flags, to_gate_ask = 0;
	u32 originator_sn, target_sn, lifetime, target_metric = 0;
	BOOLEAN reply = _FALSE;
	BOOLEAN forward = _TRUE;
	BOOLEAN preq_is_gate;

	/* Update target SN, if present */
	target_addr = RTW_PREQ_IE_TARGET_ADDR(preq_elem);
	originator_addr = RTW_PREQ_IE_ORIG_ADDR(preq_elem);
	target_sn = RTW_PREQ_IE_TARGET_SN(preq_elem);
	originator_sn = RTW_PREQ_IE_ORIG_SN(preq_elem);
	target_flags = RTW_PREQ_IE_TARGET_F(preq_elem);
	/* PREQ gate announcements */
	flags = RTW_PREQ_IE_FLAGS(preq_elem);
	preq_is_gate = !!(flags & RTW_IEEE80211_PREQ_IS_GATE_FLAG);

	RTW_HWMP_DBG("received PREQ from "MAC_FMT"\n", MAC_ARG(originator_addr));

	if (rtw_ether_addr_equal(target_addr, adapter_mac_addr(adapter))) {
		RTW_HWMP_DBG("PREQ is for us\n");
#ifdef CONFIG_RTW_MESH_ON_DMD_GANN
		rtw_rcu_read_lock();
		path = rtw_mesh_path_lookup(adapter, originator_addr);
		if (path) {
			if (preq_is_gate)
				rtw_mesh_path_add_gate(path);
			else if (path->is_gate) {
				enter_critical_bh(&path->state_lock);
				rtw_mesh_gate_del(adapter->mesh_info.mesh_paths, path);
				exit_critical_bh(&path->state_lock);
			}
		}
		path = NULL;
		rtw_rcu_read_unlock();
#endif
		forward = _FALSE;
		reply = _TRUE;
		to_gate_ask = 1;
		target_metric = 0;
		if (rtw_time_after(rtw_get_current_time(), minfo->last_sn_update +
					rtw_net_traversal_jiffies(adapter)) ||
		    rtw_time_before(rtw_get_current_time(), minfo->last_sn_update)) {
			++minfo->sn;
			minfo->last_sn_update = rtw_get_current_time();
		}
		target_sn = minfo->sn;
	} else if (is_broadcast_mac_addr(target_addr) &&
		   (target_flags & RTW_IEEE80211_PREQ_TO_FLAG)) {
		rtw_rcu_read_lock();
		path = rtw_mesh_path_lookup(adapter, originator_addr);
		if (path) {
			if (flags & RTW_IEEE80211_PREQ_PROACTIVE_PREP_FLAG) {
				reply = _TRUE;
				target_addr = adapter_mac_addr(adapter);
				target_sn = ++minfo->sn;
				target_metric = 0;
				minfo->last_sn_update = rtw_get_current_time();
			}

			if (preq_is_gate) {
				lifetime = RTW_PREQ_IE_LIFETIME(preq_elem);
				path->gate_ann_int = lifetime;
				path->gate_asked = false;
				rtw_mesh_path_add_gate(path);
			} else if (path->is_gate) {
				enter_critical_bh(&path->state_lock);
				rtw_mesh_gate_del(adapter->mesh_info.mesh_paths, path);
				exit_critical_bh(&path->state_lock);
			}
		}
		rtw_rcu_read_unlock();
	} else {
		rtw_rcu_read_lock();
#ifdef CONFIG_RTW_MESH_ON_DMD_GANN
		path = rtw_mesh_path_lookup(adapter, originator_addr);
		if (path) {
			if (preq_is_gate)
				rtw_mesh_path_add_gate(path);
			else if (path->is_gate) {
				enter_critical_bh(&path->state_lock);
				rtw_mesh_gate_del(adapter->mesh_info.mesh_paths, path);
				exit_critical_bh(&path->state_lock);
			}
		}
		path = NULL;
#endif
		path = rtw_mesh_path_lookup(adapter, target_addr);
		if (path) {
			if ((!(path->flags & RTW_MESH_PATH_SN_VALID)) ||
					RTW_SN_LT(path->sn, target_sn)) {
				path->sn = target_sn;
				path->flags |= RTW_MESH_PATH_SN_VALID;
			} else if ((!(target_flags & RTW_IEEE80211_PREQ_TO_FLAG)) &&
					(path->flags & RTW_MESH_PATH_ACTIVE)) {
				reply = _TRUE;
				target_metric = path->metric;
				target_sn = path->sn;
				/* Case E2 of sec 13.10.9.3 IEEE 802.11-2012*/
				target_flags |= RTW_IEEE80211_PREQ_TO_FLAG;
			}
		}
		rtw_rcu_read_unlock();
	}

	if (reply) {
		lifetime = RTW_PREQ_IE_LIFETIME(preq_elem);
		ttl = mshcfg->element_ttl;
		if (ttl != 0 && !to_gate_ask) {
			RTW_HWMP_DBG("replying to the PREQ\n");
			rtw_mesh_path_sel_frame_tx(RTW_MPATH_PREP, 0, originator_addr,
						   originator_sn, 0, target_addr,
						   target_sn, mgmt->addr2, 0, ttl,
						   lifetime, target_metric, 0,
						   adapter);
		} else if (ttl != 0 && to_gate_ask) {
			RTW_HWMP_DBG("replying to the PREQ (PREQ for us)\n");
			if (mshcfg->dot11MeshGateAnnouncementProtocol) {
				/* BIT 7 is used to identify the prep is from mesh gate */
				to_gate_ask = RTW_IEEE80211_PREQ_IS_GATE_FLAG | BIT(7);
			} else {
				to_gate_ask = 0;
			}

			rtw_mesh_path_sel_frame_tx(RTW_MPATH_PREP, to_gate_ask, originator_addr,
						   originator_sn, 0, target_addr,
						   target_sn, mgmt->addr2, 0, ttl,
						   lifetime, target_metric, 0,
						   adapter);
		} else {
			minfo->mshstats.dropped_frames_ttl++;
		}
	}

	if (forward && mshcfg->dot11MeshForwarding) {
		u32 preq_id;
		u8 hopcount;

		ttl = RTW_PREQ_IE_TTL(preq_elem);
		lifetime = RTW_PREQ_IE_LIFETIME(preq_elem);
		if (ttl <= 1) {
			minfo->mshstats.dropped_frames_ttl++;
			return;
		}
		RTW_HWMP_DBG("forwarding the PREQ from "MAC_FMT"\n", MAC_ARG(originator_addr));
		--ttl;
		preq_id = RTW_PREQ_IE_PREQ_ID(preq_elem);
		hopcount = RTW_PREQ_IE_HOPCOUNT(preq_elem) + 1;
		da = (path && path->is_root) ?
			path->rann_snd_addr : bcast_addr;

		if (flags & RTW_IEEE80211_PREQ_PROACTIVE_PREP_FLAG) {
			target_addr = RTW_PREQ_IE_TARGET_ADDR(preq_elem);
			target_sn = RTW_PREQ_IE_TARGET_SN(preq_elem);
		}

		rtw_mesh_path_sel_frame_tx(RTW_MPATH_PREQ, flags, originator_addr,
					   originator_sn, target_flags, target_addr,
					   target_sn, da, hopcount, ttl, lifetime,
					   originator_metric, preq_id, adapter);
		if (!is_multicast_mac_addr(da))
			minfo->mshstats.fwded_unicast++;
		else
			minfo->mshstats.fwded_mcast++;
		minfo->mshstats.fwded_frames++;
	}
}

static inline struct sta_info *
rtw_next_hop_deref_protected(struct rtw_mesh_path *path)
{
	return rtw_rcu_dereference_protected(path->next_hop,
					 rtw_lockdep_is_held(&path->state_lock));
}

static void rtw_hwmp_prep_frame_process(_adapter *adapter,
					struct rtw_ieee80211_hdr_3addr *mgmt,
					const u8 *prep_elem, u32 metric)
{
	struct rtw_mesh_cfg *mshcfg = &adapter->mesh_cfg;
	struct rtw_mesh_stats *mshstats = &adapter->mesh_info.mshstats;
	struct rtw_mesh_path *path;
	const u8 *target_addr, *originator_addr;
	u8 ttl, hopcount, flags;
	u8 next_hop[ETH_ALEN];
	u32 target_sn, originator_sn, lifetime;

	RTW_HWMP_DBG("received PREP from "MAC_FMT"\n",
		  MAC_ARG(RTW_PREP_IE_TARGET_ADDR(prep_elem)));

	originator_addr = RTW_PREP_IE_ORIG_ADDR(prep_elem);
	if (rtw_ether_addr_equal(originator_addr, adapter_mac_addr(adapter))) {
		/* destination, no forwarding required */
		rtw_rcu_read_lock();
		target_addr = RTW_PREP_IE_TARGET_ADDR(prep_elem);
		path = rtw_mesh_path_lookup(adapter, target_addr);
		if (path && path->gate_asked) {
			flags = RTW_PREP_IE_FLAGS(prep_elem);
			if (flags & BIT(7)) {
				enter_critical_bh(&path->state_lock);
				path->gate_asked = false;
				exit_critical_bh(&path->state_lock);
				if (!(flags & RTW_IEEE80211_PREQ_IS_GATE_FLAG)) {
					enter_critical_bh(&path->state_lock);
					rtw_mesh_gate_del(adapter->mesh_info.mesh_paths, path);
					exit_critical_bh(&path->state_lock);
				}
			}
		}

		rtw_rcu_read_unlock();
		return;
	}

	if (!mshcfg->dot11MeshForwarding)
		return;

	ttl = RTW_PREP_IE_TTL(prep_elem);
	if (ttl <= 1) {
		mshstats->dropped_frames_ttl++;
		return;
	}

	rtw_rcu_read_lock();
	path = rtw_mesh_path_lookup(adapter, originator_addr);
	if (path)
		enter_critical_bh(&path->state_lock);
	else
		goto fail;
	if (!(path->flags & RTW_MESH_PATH_ACTIVE)) {
		exit_critical_bh(&path->state_lock);
		goto fail;
	}
	_rtw_memcpy(next_hop, rtw_next_hop_deref_protected(path)->cmn.mac_addr, ETH_ALEN);
	exit_critical_bh(&path->state_lock);
	--ttl;
	flags = RTW_PREP_IE_FLAGS(prep_elem);
	lifetime = RTW_PREP_IE_LIFETIME(prep_elem);
	hopcount = RTW_PREP_IE_HOPCOUNT(prep_elem) + 1;
	target_addr = RTW_PREP_IE_TARGET_ADDR(prep_elem);
	target_sn = RTW_PREP_IE_TARGET_SN(prep_elem);
	originator_sn = RTW_PREP_IE_ORIG_SN(prep_elem);

	rtw_mesh_path_sel_frame_tx(RTW_MPATH_PREP, flags, originator_addr, originator_sn, 0,
				   target_addr, target_sn, next_hop, hopcount,
				   ttl, lifetime, metric, 0, adapter);
	rtw_rcu_read_unlock();

	mshstats->fwded_unicast++;
	mshstats->fwded_frames++;
	return;

fail:
	rtw_rcu_read_unlock();
	mshstats->dropped_frames_no_route++;
}

static void rtw_hwmp_perr_frame_process(_adapter *adapter,
					struct rtw_ieee80211_hdr_3addr *mgmt,
					const u8 *perr_elem)
{
	struct rtw_mesh_cfg *mshcfg = &adapter->mesh_cfg;
	struct rtw_mesh_stats *mshstats = &adapter->mesh_info.mshstats;
	struct rtw_mesh_path *path;
	u8 ttl;
	const u8 *ta, *target_addr;
	u32 target_sn;
	u16 perr_reason_code;

	ta = mgmt->addr2;
	ttl = RTW_PERR_IE_TTL(perr_elem);
	if (ttl <= 1) {
		mshstats->dropped_frames_ttl++;
		return;
	}
	ttl--;
	target_addr = RTW_PERR_IE_TARGET_ADDR(perr_elem);
	target_sn = RTW_PERR_IE_TARGET_SN(perr_elem);
	perr_reason_code = RTW_PERR_IE_TARGET_RCODE(perr_elem);

	RTW_HWMP_DBG("received PERR toward target "MAC_FMT"\n", MAC_ARG(target_addr));

	rtw_rcu_read_lock();
	path = rtw_mesh_path_lookup(adapter, target_addr);
	if (path) {
		struct sta_info *sta;

		enter_critical_bh(&path->state_lock);
		sta = rtw_next_hop_deref_protected(path);
		if (path->flags & RTW_MESH_PATH_ACTIVE &&
		    rtw_ether_addr_equal(ta, sta->cmn.mac_addr) &&
		    !(path->flags & RTW_MESH_PATH_FIXED) &&
		    (!(path->flags & RTW_MESH_PATH_SN_VALID) ||
		    RTW_SN_GT(target_sn, path->sn)  || target_sn == 0)) {
			path->flags &= ~RTW_MESH_PATH_ACTIVE;
			if (target_sn != 0)
				path->sn = target_sn;
			else
				path->sn += 1;
			exit_critical_bh(&path->state_lock);
			if (!mshcfg->dot11MeshForwarding)
				goto endperr;
			rtw_mesh_path_error_tx(adapter, ttl, target_addr,
					       target_sn, perr_reason_code,
					       bcast_addr);
		} else
			exit_critical_bh(&path->state_lock);
	}
endperr:
	rtw_rcu_read_unlock();
}

static void rtw_hwmp_rann_frame_process(_adapter *adapter,
					struct rtw_ieee80211_hdr_3addr *mgmt,
					const struct rtw_ieee80211_rann_ie *rann)
{
	struct sta_info *sta;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct rtw_mesh_cfg *mshcfg = &adapter->mesh_cfg;
	struct rtw_mesh_stats *mshstats = &adapter->mesh_info.mshstats;
	struct rtw_mesh_path *path;
	u8 ttl, flags, hopcount;
	const u8 *originator_addr;
	u32 originator_sn, metric, metric_txsta, interval;
	BOOLEAN root_is_gate;

	ttl = rann->rann_ttl;
	flags = rann->rann_flags;
	root_is_gate = !!(flags & RTW_RANN_FLAG_IS_GATE);
	originator_addr = rann->rann_addr;
	originator_sn = le32_to_cpu(rann->rann_seq);
	interval = le32_to_cpu(rann->rann_interval);
	hopcount = rann->rann_hopcount;
	hopcount++;
	metric = le32_to_cpu(rann->rann_metric);

	/*  Ignore our own RANNs */
	if (rtw_ether_addr_equal(originator_addr, adapter_mac_addr(adapter)))
		return;

	RTW_HWMP_DBG("received RANN from "MAC_FMT" via neighbour "MAC_FMT" (is_gate=%d)\n",
		  MAC_ARG(originator_addr), MAC_ARG(mgmt->addr2), root_is_gate);

	rtw_rcu_read_lock();
	sta = rtw_get_stainfo(pstapriv, mgmt->addr2);
	if (!sta) {
		rtw_rcu_read_unlock();
		return;
	}

	metric_txsta = rtw_airtime_link_metric_get(adapter, sta);

	path = rtw_mesh_path_lookup(adapter, originator_addr);
	if (!path) {
		path = rtw_mesh_path_add(adapter, originator_addr);
		if (IS_ERR(path)) {
			rtw_rcu_read_unlock();
			mshstats->dropped_frames_no_route++;
			return;
		}
	}

	if (!(RTW_SN_LT(path->sn, originator_sn)) &&
	    !(path->sn == originator_sn && metric < path->rann_metric)) {
		rtw_rcu_read_unlock();
		return;
	}

	if ((!(path->flags & (RTW_MESH_PATH_ACTIVE | RTW_MESH_PATH_RESOLVING)) ||
	     (rtw_time_after(rtw_get_current_time(), path->last_preq_to_root +
				  rtw_root_path_confirmation_jiffies(adapter)) ||
	     rtw_time_before(rtw_get_current_time(), path->last_preq_to_root))) &&
	     !(path->flags & RTW_MESH_PATH_FIXED) && (ttl != 0)) {
		u8 preq_node_flag = RTW_PREQ_Q_F_START | RTW_PREQ_Q_F_REFRESH;

		RTW_HWMP_DBG("time to refresh root path "MAC_FMT"\n",
			  MAC_ARG(originator_addr));
#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
		if (RTW_SN_LT(path->sn, originator_sn) &&
		    (path->rann_metric + mshcfg->sane_metric_delta < metric) &&
		    _rtw_memcmp(bcast_addr, path->rann_snd_addr, ETH_ALEN) == _FALSE) {
			RTW_HWMP_DBG("Trigger additional check for root "
				     "confirm PREQ. rann_snd_addr = "MAC_FMT
				     "add_chk_rann_snd_addr= "MAC_FMT"\n",
					MAC_ARG(mgmt->addr2),
					MAC_ARG(path->rann_snd_addr));
			_rtw_memcpy(path->add_chk_rann_snd_addr,
				    path->rann_snd_addr, ETH_ALEN);
			preq_node_flag |= RTW_PREQ_Q_F_CHK;
			
		}
#endif
		rtw_mesh_queue_preq(path, preq_node_flag);
		path->last_preq_to_root = rtw_get_current_time();
	}

	path->sn = originator_sn;
	path->rann_metric = metric + metric_txsta;
	path->is_root = _TRUE;
	/* Recording RANNs sender address to send individually
	 * addressed PREQs destined for root mesh STA */
	_rtw_memcpy(path->rann_snd_addr, mgmt->addr2, ETH_ALEN);

	if (root_is_gate) {
		path->gate_ann_int = interval;
		path->gate_asked = false;
		rtw_mesh_path_add_gate(path);
	} else if (path->is_gate) {
		enter_critical_bh(&path->state_lock);
		rtw_mesh_gate_del(adapter->mesh_info.mesh_paths, path);
		exit_critical_bh(&path->state_lock);
	}

	if (ttl <= 1) {
		mshstats->dropped_frames_ttl++;
		rtw_rcu_read_unlock();
		return;
	}
	ttl--;

	if (mshcfg->dot11MeshForwarding) {
		rtw_mesh_path_sel_frame_tx(RTW_MPATH_RANN, flags, originator_addr,
					   originator_sn, 0, NULL, 0, bcast_addr,
					   hopcount, ttl, interval,
					   metric + metric_txsta, 0, adapter);
	}

	rtw_rcu_read_unlock();
}

static u32 rtw_hwmp_route_info_get(_adapter *adapter,
				   struct rtw_ieee80211_hdr_3addr *mgmt,
				   const u8 *hwmp_ie, enum rtw_mpath_frame_type action)
{
	struct rtw_mesh_path *path;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *sta;
	BOOLEAN fresh_info;
	const u8 *originator_addr, *ta;
	u32 originator_sn, originator_metric;
	unsigned long originator_lifetime, exp_time;
	u32 last_hop_metric, new_metric;
	BOOLEAN process = _TRUE;

	rtw_rcu_read_lock();
	sta = rtw_get_stainfo(pstapriv, mgmt->addr2);
	if (!sta) {
		rtw_rcu_read_unlock();
		return 0;
	}

	last_hop_metric = rtw_airtime_link_metric_get(adapter, sta);
	/* Update and check originator routing info */
	fresh_info = _TRUE;

	switch (action) {
	case RTW_MPATH_PREQ:
		originator_addr = RTW_PREQ_IE_ORIG_ADDR(hwmp_ie);
		originator_sn = RTW_PREQ_IE_ORIG_SN(hwmp_ie);
		originator_lifetime = RTW_PREQ_IE_LIFETIME(hwmp_ie);
		originator_metric = RTW_PREQ_IE_METRIC(hwmp_ie);
		break;
	case RTW_MPATH_PREP:
		/* Note: For coding, the naming is not consist with spec */
		originator_addr = RTW_PREP_IE_TARGET_ADDR(hwmp_ie);
		originator_sn = RTW_PREP_IE_TARGET_SN(hwmp_ie);
		originator_lifetime = RTW_PREP_IE_LIFETIME(hwmp_ie);
		originator_metric = RTW_PREP_IE_METRIC(hwmp_ie);
		break;
	default:
		rtw_rcu_read_unlock();
		return 0;
	}
	new_metric = originator_metric + last_hop_metric;
	if (new_metric < originator_metric)
		new_metric = RTW_MAX_METRIC;
	exp_time = RTW_TU_TO_EXP_TIME(originator_lifetime);

	if (rtw_ether_addr_equal(originator_addr, adapter_mac_addr(adapter))) {
		process = _FALSE;
		fresh_info = _FALSE;
	} else {
		path = rtw_mesh_path_lookup(adapter, originator_addr);
		if (path) {
			enter_critical_bh(&path->state_lock);
			if (path->flags & RTW_MESH_PATH_FIXED)
				fresh_info = _FALSE;
			else if ((path->flags & RTW_MESH_PATH_ACTIVE) &&
			    (path->flags & RTW_MESH_PATH_SN_VALID)) {
				if (RTW_SN_GT(path->sn, originator_sn) ||
				    (path->sn == originator_sn &&
				     new_metric >= path->metric)) {
					process = _FALSE;
					fresh_info = _FALSE;
				}
			} else if (!(path->flags & RTW_MESH_PATH_ACTIVE)) {
				BOOLEAN have_sn, newer_sn, bounced;

				have_sn = path->flags & RTW_MESH_PATH_SN_VALID;
				newer_sn = have_sn && RTW_SN_GT(originator_sn, path->sn);
				bounced = have_sn &&
					  (RTW_SN_DELTA(originator_sn, path->sn) >
							RTW_MAX_SANE_SN_DELTA);

				if (!have_sn || newer_sn) {
				} else if (bounced) {
				} else {
					process = _FALSE;
					fresh_info = _FALSE;
				}
			}
		} else {
			path = rtw_mesh_path_add(adapter, originator_addr);
			if (IS_ERR(path)) {
				rtw_rcu_read_unlock();
				return 0;
			}
			enter_critical_bh(&path->state_lock);
		}

		if (fresh_info) {
			rtw_mesh_path_assign_nexthop(path, sta);
			path->flags |= RTW_MESH_PATH_SN_VALID;
			path->metric = new_metric;
			path->sn = originator_sn;
			path->exp_time = rtw_time_after(path->exp_time, exp_time)
					  ?  path->exp_time : exp_time;
			rtw_mesh_path_activate(path);
#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
			if (path->is_root && (action == RTW_MPATH_PREP)) {
				_rtw_memcpy(path->rann_snd_addr, 
				mgmt->addr2, ETH_ALEN);
				path->rann_metric = new_metric;
			}
#endif
			exit_critical_bh(&path->state_lock);
			rtw_mesh_path_tx_pending(path);
		} else
			exit_critical_bh(&path->state_lock);
	}

	/* Update and check transmitter routing info */
	ta = mgmt->addr2;
	if (rtw_ether_addr_equal(originator_addr, ta))
		fresh_info = _FALSE;
	else {
		fresh_info = _TRUE;

		path = rtw_mesh_path_lookup(adapter, ta);
		if (path) {
			enter_critical_bh(&path->state_lock);
			if ((path->flags & RTW_MESH_PATH_FIXED) ||
				((path->flags & RTW_MESH_PATH_ACTIVE) &&
					(last_hop_metric > path->metric)))
				fresh_info = _FALSE;
		} else {
			path = rtw_mesh_path_add(adapter, ta);
			if (IS_ERR(path)) {
				rtw_rcu_read_unlock();
				return 0;
			}
			enter_critical_bh(&path->state_lock);
		}

		if (fresh_info) {
			rtw_mesh_path_assign_nexthop(path, sta);
			path->metric = last_hop_metric;
			path->exp_time = rtw_time_after(path->exp_time, exp_time)
					  ?  path->exp_time : exp_time;
			rtw_mesh_path_activate(path);
			exit_critical_bh(&path->state_lock);
			rtw_mesh_path_tx_pending(path);
		} else
			exit_critical_bh(&path->state_lock);
	}

	rtw_rcu_read_unlock();

	return process ? new_metric : 0;
}

static void rtw_mesh_rx_hwmp_frame_cnts(_adapter *adapter, u8 *addr)
{
	struct sta_info *sta;

	sta = rtw_get_stainfo(&adapter->stapriv, addr);
	if (sta)
		sta->sta_stats.rx_hwmp_pkts++;
}

void rtw_mesh_rx_path_sel_frame(_adapter *adapter, union recv_frame *rframe)
{
	struct mesh_plink_ent *plink = NULL;
	struct rtw_ieee802_11_elems elems;
	u32 path_metric;
	struct rx_pkt_attrib *attrib = &rframe->u.hdr.attrib;
	u8 *pframe = rframe->u.hdr.rx_data, *start;
	uint frame_len = rframe->u.hdr.len, left;
	struct rtw_ieee80211_hdr_3addr *frame_hdr = (struct rtw_ieee80211_hdr_3addr *)pframe;
	u8 *frame_body = (u8 *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));
	ParseRes parse_res;

	plink = rtw_mesh_plink_get(adapter, get_addr2_ptr(pframe));
	if (!plink || plink->plink_state != RTW_MESH_PLINK_ESTAB)
		return;

	rtw_mesh_rx_hwmp_frame_cnts(adapter, get_addr2_ptr(pframe));

	/* Mesh action frame IE offset = 2 */
	attrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	left = frame_len - attrib->hdrlen - attrib->iv_len - attrib->icv_len - 2;
	start = pframe + attrib->hdrlen + 2;

	parse_res = rtw_ieee802_11_parse_elems(start, left, &elems, 1);
	if (parse_res == ParseFailed)
		RTW_HWMP_INFO(FUNC_ADPT_FMT" Path Select Frame ParseFailed\n"
			, FUNC_ADPT_ARG(adapter));
	else if (parse_res == ParseUnknown)
		RTW_HWMP_INFO(FUNC_ADPT_FMT" Path Select Frame ParseUnknown\n"
			, FUNC_ADPT_ARG(adapter));

	if (elems.preq) {
		if (elems.preq_len != 37)
			/* Right now we support just 1 destination and no AE */
			return;
		path_metric = rtw_hwmp_route_info_get(adapter, frame_hdr, elems.preq,
						  MPATH_PREQ);
		if (path_metric)
			rtw_hwmp_preq_frame_process(adapter, frame_hdr, elems.preq,
						path_metric);
	}
	if (elems.prep) {
		if (elems.prep_len != 31)
			/* Right now we support no AE */
			return;
		path_metric = rtw_hwmp_route_info_get(adapter, frame_hdr, elems.prep,
						  MPATH_PREP);
		if (path_metric)
			rtw_hwmp_prep_frame_process(adapter, frame_hdr, elems.prep,
						path_metric);
	}
	if (elems.perr) {
		if (elems.perr_len != 15)
			/* Right now we support only one destination per PERR */
			return;
		rtw_hwmp_perr_frame_process(adapter, frame_hdr, elems.perr);
	}
	if (elems.rann)
		rtw_hwmp_rann_frame_process(adapter, frame_hdr, (struct rtw_ieee80211_rann_ie *)elems.rann);
}

void rtw_mesh_queue_preq(struct rtw_mesh_path *path, u8 flags)
{
	_adapter *adapter = path->adapter;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct rtw_mesh_preq_queue *preq_node;

	preq_node = rtw_malloc(sizeof(struct rtw_mesh_preq_queue));
	if (!preq_node) {
		RTW_HWMP_INFO("could not allocate PREQ node\n");
		return;
	}

	enter_critical_bh(&minfo->mesh_preq_queue_lock);
	if (minfo->preq_queue_len == RTW_MAX_PREQ_QUEUE_LEN) {
		exit_critical_bh(&minfo->mesh_preq_queue_lock);
		rtw_mfree(preq_node, sizeof(struct rtw_mesh_preq_queue));
		if (rtw_print_ratelimit())
			RTW_HWMP_INFO("PREQ node queue full\n");
		return;
	}

	_rtw_spinlock(&path->state_lock);
	if (path->flags & RTW_MESH_PATH_REQ_QUEUED) {
		_rtw_spinunlock(&path->state_lock);
		exit_critical_bh(&minfo->mesh_preq_queue_lock);
		rtw_mfree(preq_node, sizeof(struct rtw_mesh_preq_queue));
		return;
	}

	_rtw_memcpy(preq_node->dst, path->dst, ETH_ALEN);
	preq_node->flags = flags;

	path->flags |= RTW_MESH_PATH_REQ_QUEUED;
#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
	if (flags & RTW_PREQ_Q_F_CHK)
		path->flags |= RTW_MESH_PATH_ROOT_ADD_CHK;
#endif
	if (flags & RTW_PREQ_Q_F_PEER_AKA)
		path->flags |= RTW_MESH_PATH_PEER_AKA;
	if (flags & RTW_PREQ_Q_F_BCAST_PREQ)
		path->flags |= RTW_MESH_PATH_BCAST_PREQ;
	_rtw_spinunlock(&path->state_lock);

	rtw_list_insert_tail(&preq_node->list, &minfo->preq_queue.list);
	++minfo->preq_queue_len;
	exit_critical_bh(&minfo->mesh_preq_queue_lock);

	if (rtw_time_after(rtw_get_current_time(), minfo->last_preq + rtw_min_preq_int_jiff(adapter)))
		rtw_mesh_work(&adapter->mesh_work);

	else if (rtw_time_before(rtw_get_current_time(), minfo->last_preq)) {
		/* systime wrapped around issue */
		minfo->last_preq = rtw_get_current_time() - rtw_min_preq_int_jiff(adapter) - 1;
		rtw_mesh_work(&adapter->mesh_work);
	} else
		rtw_mod_timer(&adapter->mesh_path_timer, minfo->last_preq +
					rtw_min_preq_int_jiff(adapter) + 1);
}

static const u8 *rtw_hwmp_preq_da(struct rtw_mesh_path *path,
			    BOOLEAN is_root_add_chk, BOOLEAN da_is_peer,
			    BOOLEAN force_preq_bcast)
{
	const u8 *da;

	if (da_is_peer)
		da = path->dst;
	else if (force_preq_bcast)
		da = bcast_addr;
	else if (path->is_root)
#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
		da = is_root_add_chk ? path->add_chk_rann_snd_addr:
				       path->rann_snd_addr;
#else
		da = path->rann_snd_addr;
#endif
	else
		da = bcast_addr;

	return da;
}

void rtw_mesh_path_start_discovery(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct rtw_mesh_cfg *mshcfg = &adapter->mesh_cfg;
	struct rtw_mesh_preq_queue *preq_node;
	struct rtw_mesh_path *path;
	u8 ttl, target_flags = 0;
	const u8 *da;
	u32 lifetime;
	u8 flags = 0;
	BOOLEAN is_root_add_chk = _FALSE;
	BOOLEAN da_is_peer, force_preq_bcast;

	enter_critical_bh(&minfo->mesh_preq_queue_lock);
	if (!minfo->preq_queue_len ||
		rtw_time_before(rtw_get_current_time(), minfo->last_preq +
				rtw_min_preq_int_jiff(adapter))) {
		exit_critical_bh(&minfo->mesh_preq_queue_lock);
		return;
	}

	preq_node = rtw_list_first_entry(&minfo->preq_queue.list,
			struct rtw_mesh_preq_queue, list);
	rtw_list_delete(&preq_node->list); /* list_del_init(&preq_node->list); */
	--minfo->preq_queue_len;
	exit_critical_bh(&minfo->mesh_preq_queue_lock);

	rtw_rcu_read_lock();
	path = rtw_mesh_path_lookup(adapter, preq_node->dst);
	if (!path)
		goto enddiscovery;

	enter_critical_bh(&path->state_lock);
	if (path->flags & (RTW_MESH_PATH_DELETED | RTW_MESH_PATH_FIXED)) {
		exit_critical_bh(&path->state_lock);
		goto enddiscovery;
	}
	path->flags &= ~RTW_MESH_PATH_REQ_QUEUED;
	if (preq_node->flags & RTW_PREQ_Q_F_START) {
		if (path->flags & RTW_MESH_PATH_RESOLVING) {
			exit_critical_bh(&path->state_lock);
			goto enddiscovery;
		} else {
			path->flags &= ~RTW_MESH_PATH_RESOLVED;
			path->flags |= RTW_MESH_PATH_RESOLVING;
			path->discovery_retries = 0;
			path->discovery_timeout = rtw_disc_timeout_jiff(adapter);
		}
	} else if (!(path->flags & RTW_MESH_PATH_RESOLVING) ||
			path->flags & RTW_MESH_PATH_RESOLVED) {
		path->flags &= ~RTW_MESH_PATH_RESOLVING;
		exit_critical_bh(&path->state_lock);
		goto enddiscovery;
	}

	minfo->last_preq = rtw_get_current_time();

	if (rtw_time_after(rtw_get_current_time(), minfo->last_sn_update +
				rtw_net_traversal_jiffies(adapter)) ||
	    rtw_time_before(rtw_get_current_time(), minfo->last_sn_update)) {
		++minfo->sn;
		minfo->last_sn_update = rtw_get_current_time();
	}
	lifetime = rtw_default_lifetime(adapter);
	ttl = mshcfg->element_ttl;
	if (ttl == 0) {
		minfo->mshstats.dropped_frames_ttl++;
		exit_critical_bh(&path->state_lock);
		goto enddiscovery;
	}

	if (preq_node->flags & RTW_PREQ_Q_F_REFRESH)
		target_flags |= RTW_IEEE80211_PREQ_TO_FLAG;
	else
		target_flags &= ~RTW_IEEE80211_PREQ_TO_FLAG;

#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
	is_root_add_chk = !!(path->flags & RTW_MESH_PATH_ROOT_ADD_CHK);
#endif
	da_is_peer = !!(path->flags & RTW_MESH_PATH_PEER_AKA);
	force_preq_bcast = !!(path->flags & RTW_MESH_PATH_BCAST_PREQ);
	exit_critical_bh(&path->state_lock);

	da = rtw_hwmp_preq_da(path, is_root_add_chk,
			      da_is_peer, force_preq_bcast);

#ifdef CONFIG_RTW_MESH_ON_DMD_GANN
	flags = (mshcfg->dot11MeshGateAnnouncementProtocol)
		? RTW_IEEE80211_PREQ_IS_GATE_FLAG : 0;
#endif
	rtw_mesh_path_sel_frame_tx(RTW_MPATH_PREQ, flags, adapter_mac_addr(adapter), minfo->sn,
				   target_flags, path->dst, path->sn, da, 0,
				   ttl, lifetime, 0, minfo->preq_id++, adapter);
	rtw_mod_timer(&path->timer, rtw_get_current_time() + path->discovery_timeout);

enddiscovery:
	rtw_rcu_read_unlock();
	rtw_mfree(preq_node, sizeof(struct rtw_mesh_preq_queue));
}

void rtw_mesh_path_timer(void *ctx)
{
	struct rtw_mesh_path *path = (void *) ctx;
	_adapter *adapter = path->adapter;
	int ret;
	u8 retry = 0;
#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
	struct rtw_mesh_cfg *mshcfg = &adapter->mesh_cfg;
#endif
	/* TBD: Proctect for suspend */
#if 0
	if (suspending)
		return;
#endif
	enter_critical_bh(&path->state_lock);
	if (path->flags & RTW_MESH_PATH_RESOLVED ||
			(!(path->flags & RTW_MESH_PATH_RESOLVING))) {
		path->flags &= ~(RTW_MESH_PATH_RESOLVING |
				 RTW_MESH_PATH_RESOLVED |
				 RTW_MESH_PATH_ROOT_ADD_CHK |
				 RTW_MESH_PATH_PEER_AKA |
				 RTW_MESH_PATH_BCAST_PREQ);
		exit_critical_bh(&path->state_lock);
	} else if (path->discovery_retries < rtw_max_preq_retries(adapter)) {
		++path->discovery_retries;
		path->discovery_timeout *= 2;
		path->flags &= ~RTW_MESH_PATH_REQ_QUEUED;
#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
		if (path->discovery_retries > mshcfg->max_root_add_chk_cnt)
			path->flags &= ~RTW_MESH_PATH_ROOT_ADD_CHK;
#endif
		if (path->gate_asked)
			retry |= RTW_PREQ_Q_F_REFRESH;

		exit_critical_bh(&path->state_lock);
		rtw_mesh_queue_preq(path, retry);
	} else {
		path->flags &= ~(RTW_MESH_PATH_RESOLVING |
				  RTW_MESH_PATH_RESOLVED |
				  RTW_MESH_PATH_REQ_QUEUED |
				  RTW_MESH_PATH_ROOT_ADD_CHK |
				  RTW_MESH_PATH_PEER_AKA |
				  RTW_MESH_PATH_BCAST_PREQ);
		path->exp_time = rtw_get_current_time();
		exit_critical_bh(&path->state_lock);
		if (!path->is_gate && rtw_mesh_gate_num(adapter) > 0) {
			ret = rtw_mesh_path_send_to_gates(path);
			if (ret)
				RTW_HWMP_DBG("no gate was reachable\n");
		} else
			rtw_mesh_path_flush_pending(path);
	}
}


void rtw_mesh_path_tx_root_frame(_adapter *adapter)
{
	struct rtw_mesh_cfg *mshcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	u32 interval = mshcfg->dot11MeshHWMPRannInterval;
	u8 flags, target_flags = 0;

	flags = (mshcfg->dot11MeshGateAnnouncementProtocol)
			? RTW_RANN_FLAG_IS_GATE : 0;

	switch (mshcfg->dot11MeshHWMPRootMode) {
	case RTW_IEEE80211_PROACTIVE_RANN:
		rtw_mesh_path_sel_frame_tx(RTW_MPATH_RANN, flags, adapter_mac_addr(adapter),
					   ++minfo->sn, 0, NULL, 0, bcast_addr,
					   0, mshcfg->element_ttl,
					   interval, 0, 0, adapter);
		break;
	case RTW_IEEE80211_PROACTIVE_PREQ_WITH_PREP:
		flags |= RTW_IEEE80211_PREQ_PROACTIVE_PREP_FLAG;
	case RTW_IEEE80211_PROACTIVE_PREQ_NO_PREP:
		interval = mshcfg->dot11MeshHWMPactivePathToRootTimeout;
		target_flags |= RTW_IEEE80211_PREQ_TO_FLAG |
				RTW_IEEE80211_PREQ_USN_FLAG;
		rtw_mesh_path_sel_frame_tx(RTW_MPATH_PREQ, flags, adapter_mac_addr(adapter),
					   ++minfo->sn, target_flags,
					   (u8 *) bcast_addr, 0, bcast_addr,
					   0, mshcfg->element_ttl, interval,
					   0, minfo->preq_id++, adapter);
		break;
	default:
		RTW_HWMP_INFO("Proactive mechanism not supported\n");
		return;
	}
}

void rtw_mesh_work(_workitem *work)
{
	/* use kernel global workqueue */
	_set_workitem(work);
}

void rtw_ieee80211_mesh_path_timer(void *ctx)
{
	_adapter *adapter = (_adapter *)ctx;
	rtw_mesh_work(&adapter->mesh_work);
}

void rtw_ieee80211_mesh_path_root_timer(void *ctx)
{
	_adapter *adapter = (_adapter *)ctx;

	rtw_set_bit(RTW_MESH_WORK_ROOT, &adapter->wrkq_flags);

	rtw_mesh_work(&adapter->mesh_work);
}

static void rtw_ieee80211_mesh_rootpath(_adapter *adapter)
{
	u32 interval;

	rtw_mesh_path_tx_root_frame(adapter);

	if (adapter->mesh_cfg.dot11MeshHWMPRootMode == RTW_IEEE80211_PROACTIVE_RANN)
		interval = adapter->mesh_cfg.dot11MeshHWMPRannInterval;
	else
		interval = adapter->mesh_cfg.dot11MeshHWMProotInterval;

	rtw_mod_timer(&adapter->mesh_path_root_timer,
		  RTW_TU_TO_EXP_TIME(interval));
}

BOOLEAN rtw_ieee80211_mesh_root_setup(_adapter *adapter)
{
	BOOLEAN root_enabled = _FALSE;

	if (adapter->mesh_cfg.dot11MeshHWMPRootMode > RTW_IEEE80211_ROOTMODE_ROOT) {
		rtw_set_bit(RTW_MESH_WORK_ROOT, &adapter->wrkq_flags);
		root_enabled = _TRUE;
	}
	else {
		rtw_clear_bit(RTW_MESH_WORK_ROOT, &adapter->wrkq_flags);
		/* stop running timer */
		_cancel_timer_ex(&adapter->mesh_path_root_timer);
		root_enabled = _FALSE;
	}

	return root_enabled;
}

void rtw_mesh_work_hdl(_workitem *work)
{
	_adapter *adapter = container_of(work, _adapter, mesh_work);

	while(adapter->mesh_info.preq_queue_len) {
		if (rtw_time_after(rtw_get_current_time(),
		       adapter->mesh_info.last_preq + rtw_min_preq_int_jiff(adapter)))
		       /* It will consume preq_queue_len */
		       rtw_mesh_path_start_discovery(adapter);
		else {
			struct rtw_mesh_info *minfo = &adapter->mesh_info;

			rtw_mod_timer(&adapter->mesh_path_timer,
				minfo->last_preq + rtw_min_preq_int_jiff(adapter) + 1);
			break;
		}
	}

	if (rtw_test_and_clear_bit(RTW_MESH_WORK_ROOT, &adapter->wrkq_flags))
		rtw_ieee80211_mesh_rootpath(adapter);
}

#ifndef RTW_PER_CMD_SUPPORT_FW
static void rtw_update_metric_directly(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	u8 i;

	for (i = 0; i < macid_ctl->num; i++) {
		u8 role;
		role = GET_H2CCMD_MSRRPT_PARM_ROLE(&macid_ctl->h2c_msr[i]);
		if (role == H2C_MSR_ROLE_MESH) {
			struct sta_info *sta = macid_ctl->sta[i];
			u8 rate_idx, sgi, bw;
			u32 rate;

			if (!sta)
				continue;
			rate_idx = rtw_get_current_tx_rate(adapter, sta);
			sgi = rtw_get_current_tx_sgi(adapter, sta);
			bw = sta->cmn.bw_mode;
			rate = rtw_desc_rate_to_bitrate(bw, rate_idx, sgi);
			sta->metrics.data_rate = rate;
		}
	}
}
#endif

void rtw_mesh_atlm_param_req_timer(void *ctx)
{
	_adapter *adapter = (_adapter *)ctx;
	u8 ret = _FAIL;

#ifdef RTW_PER_CMD_SUPPORT_FW
	ret = rtw_req_per_cmd(adapter);
	if (ret == _FAIL)
		RTW_HWMP_INFO("rtw_req_per_cmd fail\n");
#else
	rtw_update_metric_directly(adapter);
#endif
	_set_timer(&adapter->mesh_atlm_param_req_timer, RTW_ATLM_REQ_CYCLE);
}

#endif /* CONFIG_RTW_MESH */

