/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92D_TRX_COMMON_H__
#define __RTL92D_TRX_COMMON_H__

#define RX_DRV_INFO_SIZE_UNIT			8

enum rtl92d_rx_desc_enc {
	RX_DESC_ENC_NONE	= 0,
	RX_DESC_ENC_WEP40	= 1,
	RX_DESC_ENC_TKIP_WO_MIC	= 2,
	RX_DESC_ENC_TKIP_MIC	= 3,
	RX_DESC_ENC_AES		= 4,
	RX_DESC_ENC_WEP104	= 5,
};

/* macros to read/write various fields in RX or TX descriptors */

static inline void set_tx_desc_pkt_size(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(15, 0));
}

static inline void set_tx_desc_offset(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(23, 16));
}

static inline void set_tx_desc_htc(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(25));
}

static inline void set_tx_desc_last_seg(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(26));
}

static inline void set_tx_desc_first_seg(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(27));
}

static inline void set_tx_desc_linip(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(28));
}

static inline void set_tx_desc_own(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(31));
}

static inline u32 get_tx_desc_own(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(31));
}

static inline void set_tx_desc_macid(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(4, 0));
}

static inline void set_tx_desc_agg_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, BIT(5));
}

static inline void set_tx_desc_rdg_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, BIT(7));
}

static inline void set_tx_desc_queue_sel(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(12, 8));
}

static inline void set_tx_desc_rate_id(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(19, 16));
}

static inline void set_tx_desc_sec_type(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(23, 22));
}

static inline void set_tx_desc_pkt_offset(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(30, 26));
}

static inline void set_tx_desc_more_frag(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 2), __val, BIT(17));
}

static inline void set_tx_desc_ampdu_density(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 2), __val, GENMASK(22, 20));
}

static inline void set_tx_desc_seq(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, GENMASK(27, 16));
}

static inline void set_tx_desc_pkt_id(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, GENMASK(31, 28));
}

static inline void set_tx_desc_rts_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(4, 0));
}

static inline void set_tx_desc_qos(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(6));
}

static inline void set_tx_desc_hwseq_en(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(7));
}

static inline void set_tx_desc_use_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(8));
}

static inline void set_tx_desc_disable_fb(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(10));
}

static inline void set_tx_desc_cts2self(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(11));
}

static inline void set_tx_desc_rts_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(12));
}

static inline void set_tx_desc_hw_rts_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(13));
}

static inline void set_tx_desc_tx_sub_carrier(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(21, 20));
}

static inline void set_tx_desc_data_bw(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(25));
}

static inline void set_tx_desc_rts_short(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(26));
}

static inline void set_tx_desc_rts_bw(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(27));
}

static inline void set_tx_desc_rts_sc(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(29, 28));
}

static inline void set_tx_desc_rts_stbc(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(31, 30));
}

static inline void set_tx_desc_tx_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, GENMASK(5, 0));
}

static inline void set_tx_desc_data_shortgi(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, BIT(6));
}

static inline void set_tx_desc_data_rate_fb_limit(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, GENMASK(12, 8));
}

static inline void set_tx_desc_rts_rate_fb_limit(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, GENMASK(16, 13));
}

static inline void set_tx_desc_max_agg_num(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 6), __val, GENMASK(15, 11));
}

static inline void set_tx_desc_tx_buffer_size(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 7), __val, GENMASK(15, 0));
}

static inline void set_tx_desc_tx_buffer_address(__le32 *__pdesc, u32 __val)
{
	*(__pdesc + 8) = cpu_to_le32(__val);
}

static inline u32 get_tx_desc_tx_buffer_address(__le32 *__pdesc)
{
	return le32_to_cpu(*(__pdesc + 8));
}

static inline void set_tx_desc_next_desc_address(__le32 *__pdesc, u32 __val)
{
	*(__pdesc + 10) = cpu_to_le32(__val);
}

static inline u32 get_rx_desc_pkt_len(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, GENMASK(13, 0));
}

static inline u32 get_rx_desc_crc32(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(14));
}

static inline u32 get_rx_desc_icv(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(15));
}

static inline u32 get_rx_desc_drv_info_size(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, GENMASK(19, 16));
}

static inline u32 get_rx_desc_enc_type(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, GENMASK(22, 20));
}

static inline u32 get_rx_desc_shift(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, GENMASK(25, 24));
}

static inline u32 get_rx_desc_physt(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(26));
}

static inline u32 get_rx_desc_swdec(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(27));
}

static inline u32 get_rx_desc_own(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(31));
}

static inline void set_rx_desc_pkt_len(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(13, 0));
}

static inline void set_rx_desc_eor(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(30));
}

static inline void set_rx_desc_own(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(31));
}

static inline u32 get_rx_desc_paggr(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 1), BIT(14));
}

static inline u32 get_rx_desc_faggr(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 1), BIT(15));
}

static inline u32 get_rx_desc_rxmcs(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), GENMASK(5, 0));
}

static inline u32 get_rx_desc_rxht(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), BIT(6));
}

static inline u32 get_rx_desc_splcp(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), BIT(8));
}

static inline u32 get_rx_desc_bw(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), BIT(9));
}

static inline u32 get_rx_desc_tsfl(__le32 *__pdesc)
{
	return le32_to_cpu(*(__pdesc + 5));
}

static inline u32 get_rx_desc_buff_addr(__le32 *__pdesc)
{
	return le32_to_cpu(*(__pdesc + 6));
}

static inline void set_rx_desc_buff_addr(__le32 *__pdesc, u32 __val)
{
	*(__pdesc + 6) = cpu_to_le32(__val);
}

/* For 92D early mode */
static inline void set_earlymode_pktnum(__le32 *__paddr, u32 __value)
{
	le32p_replace_bits(__paddr, __value, GENMASK(2, 0));
}

static inline void set_earlymode_len0(__le32 *__paddr, u32 __value)
{
	le32p_replace_bits(__paddr, __value, GENMASK(15, 4));
}

static inline void set_earlymode_len1(__le32 *__paddr, u32 __value)
{
	le32p_replace_bits(__paddr, __value, GENMASK(27, 16));
}

static inline void set_earlymode_len2_1(__le32 *__paddr, u32 __value)
{
	le32p_replace_bits(__paddr, __value, GENMASK(31, 28));
}

static inline void set_earlymode_len2_2(__le32 *__paddr, u32 __value)
{
	le32p_replace_bits((__paddr + 1), __value, GENMASK(7, 0));
}

static inline void set_earlymode_len3(__le32 *__paddr, u32 __value)
{
	le32p_replace_bits((__paddr + 1), __value, GENMASK(19, 8));
}

static inline void set_earlymode_len4(__le32 *__paddr, u32 __value)
{
	le32p_replace_bits((__paddr + 1), __value, GENMASK(31, 20));
}

struct rx_fwinfo_92d {
	u8 gain_trsw[4];
	u8 pwdb_all;
	u8 cfosho[4];
	u8 cfotail[4];
	s8 rxevm[2];
	s8 rxsnr[4];
	u8 pdsnr[2];
	u8 csi_current[2];
	u8 csi_target[2];
	u8 sigevm;
	u8 max_ex_pwr;
#ifdef __LITTLE_ENDIAN
	u8 ex_intf_flag:1;
	u8 sgi_en:1;
	u8 rxsc:2;
	u8 reserve:4;
#else
	u8 reserve:4;
	u8 rxsc:2;
	u8 sgi_en:1;
	u8 ex_intf_flag:1;
#endif
} __packed;

bool rtl92de_rx_query_desc(struct ieee80211_hw *hw,
			   struct rtl_stats *stats,
			   struct ieee80211_rx_status *rx_status,
			   u8 *pdesc, struct sk_buff *skb);
void rtl92de_set_desc(struct ieee80211_hw *hw, u8 *pdesc, bool istx,
		      u8 desc_name, u8 *val);
u64 rtl92de_get_desc(struct ieee80211_hw *hw,
		     u8 *p_desc, bool istx, u8 desc_name);

#endif
