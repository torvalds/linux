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
#define GET_RX_DESC_PKT_LEN(__rxdesc)		\
	le32_get_bits(*(__le32 *)__rxdesc, GENMASK(13, 0))
#define GET_RX_DESC_CRC32(__rxdesc)		\
	le32_get_bits(*(__le32 *)__rxdesc, BIT(14))
#define GET_RX_DESC_ICV(__rxdesc)		\
	le32_get_bits(*(__le32 *)__rxdesc, BIT(15))
#define GET_RX_DESC_DRVINFO_SIZE(__rxdesc)	\
	le32_get_bits(*(__le32 *)__rxdesc, GENMASK(19, 16))
#define GET_RX_DESC_SHIFT(__rxdesc)		\
	le32_get_bits(*(__le32 *)__rxdesc, GENMASK(25, 24))
#define GET_RX_DESC_PHY_STATUS(__rxdesc)	\
	le32_get_bits(*(__le32 *)__rxdesc, BIT(26))
#define GET_RX_DESC_SWDEC(__rxdesc)		\
	le32_get_bits(*(__le32 *)__rxdesc, BIT(27))

/* DWORD 1 */
#define GET_RX_DESC_PAGGR(__rxdesc)		\
	le32_get_bits(*(__le32 *)(__rxdesc + 4), BIT(14))
#define GET_RX_DESC_FAGGR(__rxdesc)		\
	le32_get_bits(*(__le32 *)(__rxdesc + 4), BIT(15))

/* DWORD 3 */
#define GET_RX_DESC_RX_MCS(__rxdesc)		\
	le32_get_bits(*(__le32 *)(__rxdesc + 12), GENMASK(5, 0))
#define GET_RX_DESC_RX_HT(__rxdesc)            \
	le32_get_bits(*(__le32 *)(__rxdesc + 12), BIT(6))
#define GET_RX_DESC_SPLCP(__rxdesc)            \
	le32_get_bits(*(__le32 *)(__rxdesc + 12), BIT(8))
#define GET_RX_DESC_BW(__rxdesc)               \
	le32_get_bits(*(__le32 *)(__rxdesc + 12), BIT(9))

/* DWORD 5 */
#define GET_RX_DESC_TSFL(__rxdesc)		\
	le32_to_cpu(*((__le32 *)(__rxdesc + 20)))

/*======================= tx desc ============================================*/

/* macros to set various fields in TX descriptor */

/* Dword 0 */
#define SET_TX_DESC_PKT_SIZE(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)__txdesc, __value, GENMASK(15, 0))
#define SET_TX_DESC_OFFSET(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)__txdesc, __value, GENMASK(23, 16))
#define SET_TX_DESC_BMC(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)__txdesc, __value, BIT(24))
#define SET_TX_DESC_HTC(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)__txdesc, __value, BIT(25))
#define SET_TX_DESC_LAST_SEG(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)__txdesc, __value, BIT(26))
#define SET_TX_DESC_FIRST_SEG(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)__txdesc, __value, BIT(27))
#define SET_TX_DESC_LINIP(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)__txdesc, __value, BIT(28))
#define SET_TX_DESC_OWN(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)__txdesc, __value, BIT(31))

/* Dword 1 */
#define SET_TX_DESC_MACID(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 4), __value, GENMASK(4, 0))
#define SET_TX_DESC_AGG_ENABLE(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 4), __value, BIT(5))
#define SET_TX_DESC_AGG_BREAK(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 4), __value, BIT(6))
#define SET_TX_DESC_RDG_ENABLE(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 4), __value, BIT(7))
#define SET_TX_DESC_QUEUE_SEL(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 4), __value, GENMASK(12, 8))
#define SET_TX_DESC_RATE_ID(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 4), __value, GENMASK(19, 16))
#define SET_TX_DESC_NAV_USE_HDR(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 4), __value, BIT(20))
#define SET_TX_DESC_SEC_TYPE(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 4), __value, GENMASK(23, 22))
#define SET_TX_DESC_PKT_OFFSET(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 4), __value, GENMASK(30, 26))

/* Dword 2 */
#define SET_TX_DESC_MORE_FRAG(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 8), __value, BIT(17))
#define SET_TX_DESC_AMPDU_DENSITY(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 8), __value, GENMASK(22, 20))

/* Dword 3 */
#define SET_TX_DESC_SEQ(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 12), __value, GENMASK(27, 16))
#define SET_TX_DESC_PKT_ID(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 12), __value, GENMASK(31, 28))

/* Dword 4 */
#define SET_TX_DESC_RTS_RATE(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, GENMASK(4, 0))
#define SET_TX_DESC_QOS(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(6))
#define SET_TX_DESC_HWSEQ_EN(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(7))
#define SET_TX_DESC_USE_RATE(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(8))
#define SET_TX_DESC_DISABLE_FB(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(10))
#define SET_TX_DESC_CTS2SELF(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(11))
#define SET_TX_DESC_RTS_ENABLE(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(12))
#define SET_TX_DESC_HW_RTS_ENABLE(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(13))
#define SET_TX_DESC_DATA_SC(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, GENMASK(21, 20))
#define SET_TX_DESC_DATA_BW(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(25))
#define SET_TX_DESC_RTS_SHORT(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(26))
#define SET_TX_DESC_RTS_BW(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, BIT(27))
#define SET_TX_DESC_RTS_SC(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, GENMASK(29, 28))
#define SET_TX_DESC_RTS_STBC(__txdesc, __value)		\
	le32p_replace_bits((__le32 *)(__txdesc + 16), __value, GENMASK(31, 30))

/* Dword 5 */
#define SET_TX_DESC_TX_RATE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(5, 0))
#define SET_TX_DESC_DATA_SHORTGI(__pdesc, __val)	\
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, BIT(6))
#define SET_TX_DESC_DATA_RATE_FB_LIMIT(__txdesc, __value) \
	le32p_replace_bits((__le32 *)(__txdesc + 20), __value, GENMASK(12, 8))
#define SET_TX_DESC_RTS_RATE_FB_LIMIT(__txdesc, __value) \
	le32p_replace_bits((__le32 *)(__txdesc + 20), __value, GENMASK(16, 13))

/* Dword 6 */
#define SET_TX_DESC_MAX_AGG_NUM(__txdesc, __value)	\
	le32p_replace_bits((__le32 *)(__txdesc + 24), __value, GENMASK(15, 11))

/* Dword 7 */
#define SET_TX_DESC_TX_DESC_CHECKSUM(__txdesc, __value) \
	le32p_replace_bits((__le32 *)(__txdesc + 28), __value, GENMASK(15, 0))

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
void rtl92cu_tx_fill_cmddesc(struct ieee80211_hw *hw,
			     u8 *pdesc, bool b_firstseg,
			     bool b_lastseg, struct sk_buff *skb);

#endif
