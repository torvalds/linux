/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92CU_TRX_H__
#define __RTL92CU_TRX_H__

#define RTL92C_USB_BULK_IN_NUM			1
#define RTL92C_NUM_RX_URBS			8
#define RTL92C_NUM_TX_URBS			32

#define RTL92C_SIZE_MAX_RX_BUFFER		15360   /* 8192 */
#define RX_DRV_INFO_SIZE_UNIT			8

#define RTL_AGG_ON				1

enum usb_rx_agg_mode {
	USB_RX_AGG_DISABLE,
	USB_RX_AGG_DMA,
	USB_RX_AGG_USB,
	USB_RX_AGG_DMA_USB
};

#define TX_SELE_HQ				BIT(0)	/* High Queue */
#define TX_SELE_LQ				BIT(1)	/* Low Queue */
#define TX_SELE_NQ				BIT(2)	/* Normal Queue */

#define RTL_USB_TX_AGG_NUM_DESC			5

#define RTL_USB_RX_AGG_PAGE_NUM			4
#define RTL_USB_RX_AGG_PAGE_TIMEOUT		3

#define RTL_USB_RX_AGG_BLOCK_NUM		5
#define RTL_USB_RX_AGG_BLOCK_TIMEOUT		3

/*======================== rx status =========================================*/

struct rx_drv_info_92c {
	/*
	 * Driver info contain PHY status and other variabel size info
	 * PHY Status content as below
	 */

	/* DWORD 0 */
	u8 gain_trsw[4];

	/* DWORD 1 */
	u8 pwdb_all;
	u8 cfosho[4];

	/* DWORD 2 */
	u8 cfotail[4];

	/* DWORD 3 */
	s8 rxevm[2];
	s8 rxsnr[4];

	/* DWORD 4 */
	u8 pdsnr[2];

	/* DWORD 5 */
	u8 csi_current[2];
	u8 csi_target[2];

	/* DWORD 6 */
	u8 sigevm;
	u8 max_ex_pwr;
	u8 ex_intf_flag:1;
	u8 sgi_en:1;
	u8 rxsc:2;
	u8 reserve:4;
} __packed;

/* macros to read various fields in RX descriptor */

/* DWORD 0 */
static inline u32 get_rx_desc_pkt_len(__le32 *__rxdesc)
{
	return le32_get_bits(*__rxdesc, GENMASK(13, 0));
}

static inline u32 get_rx_desc_crc32(__le32 *__rxdesc)
{
	return le32_get_bits(*__rxdesc, BIT(14));
}

static inline u32 get_rx_desc_icv(__le32 *__rxdesc)
{
	return le32_get_bits(*__rxdesc, BIT(15));
}

static inline u32 get_rx_desc_drvinfo_size(__le32 *__rxdesc)
{
	return le32_get_bits(*__rxdesc, GENMASK(19, 16));
}

static inline u32 get_rx_desc_shift(__le32 *__rxdesc)
{
	return le32_get_bits(*__rxdesc, GENMASK(25, 24));
}

static inline u32 get_rx_desc_phy_status(__le32 *__rxdesc)
{
	return le32_get_bits(*__rxdesc, BIT(26));
}

static inline u32 get_rx_desc_swdec(__le32 *__rxdesc)
{
	return le32_get_bits(*__rxdesc, BIT(27));
}


/* DWORD 1 */
static inline u32 get_rx_desc_paggr(__le32 *__rxdesc)
{
	return le32_get_bits(*(__rxdesc + 1), BIT(14));
}

static inline u32 get_rx_desc_faggr(__le32 *__rxdesc)
{
	return le32_get_bits(*(__rxdesc + 1), BIT(15));
}


/* DWORD 3 */
static inline u32 get_rx_desc_rx_mcs(__le32 *__rxdesc)
{
	return le32_get_bits(*(__rxdesc + 3), GENMASK(5, 0));
}

static inline u32 get_rx_desc_rx_ht(__le32 *__rxdesc)
{
	return le32_get_bits(*(__rxdesc + 3), BIT(6));
}

static inline u32 get_rx_desc_splcp(__le32 *__rxdesc)
{
	return le32_get_bits(*(__rxdesc + 3), BIT(8));
}

static inline u32 get_rx_desc_bw(__le32 *__rxdesc)
{
	return le32_get_bits(*(__rxdesc + 3), BIT(9));
}


/* DWORD 5 */
static inline u32 get_rx_desc_tsfl(__le32 *__rxdesc)
{
	return le32_to_cpu(*((__rxdesc + 5)));
}


/*======================= tx desc ============================================*/

/* macros to set various fields in TX descriptor */

/* Dword 0 */
static inline void set_tx_desc_pkt_size(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits(__txdesc, __value, GENMASK(15, 0));
}

static inline void set_tx_desc_offset(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits(__txdesc, __value, GENMASK(23, 16));
}

static inline void set_tx_desc_bmc(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits(__txdesc, __value, BIT(24));
}

static inline void set_tx_desc_htc(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits(__txdesc, __value, BIT(25));
}

static inline void set_tx_desc_last_seg(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits(__txdesc, __value, BIT(26));
}

static inline void set_tx_desc_first_seg(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits(__txdesc, __value, BIT(27));
}

static inline void set_tx_desc_linip(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits(__txdesc, __value, BIT(28));
}

static inline void set_tx_desc_own(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits(__txdesc, __value, BIT(31));
}


/* Dword 1 */
static inline void set_tx_desc_macid(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, GENMASK(4, 0));
}

static inline void set_tx_desc_agg_enable(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, BIT(5));
}

static inline void set_tx_desc_agg_break(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, BIT(6));
}

static inline void set_tx_desc_rdg_enable(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, BIT(7));
}

static inline void set_tx_desc_queue_sel(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, GENMASK(12, 8));
}

static inline void set_tx_desc_rate_id(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, GENMASK(19, 16));
}

static inline void set_tx_desc_nav_use_hdr(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, BIT(20));
}

static inline void set_tx_desc_sec_type(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, GENMASK(23, 22));
}

static inline void set_tx_desc_pkt_offset(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, GENMASK(30, 26));
}


/* Dword 2 */
static inline void set_tx_desc_more_frag(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 2), __value, BIT(17));
}

static inline void set_tx_desc_ampdu_density(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 2), __value, GENMASK(22, 20));
}


/* Dword 3 */
static inline void set_tx_desc_seq(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 3), __value, GENMASK(27, 16));
}

static inline void set_tx_desc_pkt_id(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 3), __value, GENMASK(31, 28));
}


/* Dword 4 */
static inline void set_tx_desc_rts_rate(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, GENMASK(4, 0));
}

static inline void set_tx_desc_qos(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(6));
}

static inline void set_tx_desc_hwseq_en(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(7));
}

static inline void set_tx_desc_use_rate(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(8));
}

static inline void set_tx_desc_disable_fb(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(10));
}

static inline void set_tx_desc_cts2self(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(11));
}

static inline void set_tx_desc_rts_enable(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(12));
}

static inline void set_tx_desc_hw_rts_enable(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(13));
}

static inline void set_tx_desc_data_sc(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, GENMASK(21, 20));
}

static inline void set_tx_desc_data_bw(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(25));
}

static inline void set_tx_desc_rts_short(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(26));
}

static inline void set_tx_desc_rts_bw(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, BIT(27));
}

static inline void set_tx_desc_rts_sc(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, GENMASK(29, 28));
}

static inline void set_tx_desc_rts_stbc(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 4), __value, GENMASK(31, 30));
}


/* Dword 5 */
static inline void set_tx_desc_tx_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, GENMASK(5, 0));
}

static inline void set_tx_desc_data_shortgi(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, BIT(6));
}

static inline void set_tx_desc_data_rate_fb_limit(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 5), __value, GENMASK(12, 8));
}

static inline void set_tx_desc_rts_rate_fb_limit(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 5), __value, GENMASK(16, 13));
}


/* Dword 6 */
static inline void set_tx_desc_max_agg_num(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 6), __value, GENMASK(15, 11));
}


/* Dword 7 */
static inline void set_tx_desc_tx_desc_checksum(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 7), __value, GENMASK(15, 0));
}


int  rtl8192cu_endpoint_mapping(struct ieee80211_hw *hw);
u16 rtl8192cu_mq_to_hwq(__le16 fc, u16 mac80211_queue_index);
bool rtl92cu_rx_query_desc(struct ieee80211_hw *hw,
			   struct rtl_stats *stats,
			   struct ieee80211_rx_status *rx_status,
			   u8 *p_desc, struct sk_buff *skb);
void  rtl8192cu_rx_hdl(struct ieee80211_hw *hw, struct sk_buff * skb);
void rtl8192c_tx_cleanup(struct ieee80211_hw *hw, struct sk_buff  *skb);
int rtl8192c_tx_post_hdl(struct ieee80211_hw *hw, struct urb *urb,
			 struct sk_buff *skb);
struct sk_buff *rtl8192c_tx_aggregate_hdl(struct ieee80211_hw *,
					   struct sk_buff_head *);
void rtl92cu_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc_tx,
			  u8 *pbd_desc_tx, struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb,
			  u8 queue_index,
			  struct rtl_tcb_desc *tcb_desc);
void rtl92cu_fill_fake_txdesc(struct ieee80211_hw *hw, u8 *pdesc,
			       u32 buffer_len, bool ispspoll);
void rtl92cu_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc,
			     struct sk_buff *skb);

#endif
