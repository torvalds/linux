/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2014  Realtek Corporation.*/

#ifndef __RTL92E_TRX_H__
#define __RTL92E_TRX_H__

#define TX_DESC_SIZE					64

#define RX_DRV_INFO_SIZE_UNIT				8

#define	TX_DESC_NEXT_DESC_OFFSET			40
#define USB_HWDESC_HEADER_LEN				40

#define RX_DESC_SIZE					24
#define MAX_RECEIVE_BUFFER_SIZE				8192

#define SET_TX_DESC_PKT_SIZE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)__pdesc, __val, GENMASK(15, 0))
#define SET_TX_DESC_OFFSET(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)__pdesc, __val, GENMASK(23, 16))
#define SET_TX_DESC_BMC(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(24))
#define SET_TX_DESC_HTC(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(25))
#define SET_TX_DESC_LAST_SEG(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(26))
#define SET_TX_DESC_FIRST_SEG(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(27))
#define SET_TX_DESC_LINIP(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(28))
#define SET_TX_DESC_OWN(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(31))

#define GET_TX_DESC_OWN(__pdesc)			\
	le32_get_bits(*((__le32 *)__pdesc), BIT(31))

#define SET_TX_DESC_MACID(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(6, 0))
#define SET_TX_DESC_QUEUE_SEL(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(12, 8))
#define SET_TX_DESC_RATE_ID(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(20, 16))
#define SET_TX_DESC_SEC_TYPE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(23, 22))
#define SET_TX_DESC_PKT_OFFSET(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(28, 24))

#define SET_TX_DESC_AGG_ENABLE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, BIT(12))
#define SET_TX_DESC_RDG_ENABLE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, BIT(13))
#define SET_TX_DESC_MORE_FRAG(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, BIT(17))
#define SET_TX_DESC_AMPDU_DENSITY(__pdesc, __val)	\
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, GENMASK(22, 20))

#define SET_TX_DESC_USE_RATE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(8))
#define SET_TX_DESC_DISABLE_FB(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(10))
#define SET_TX_DESC_CTS2SELF(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(11))
#define SET_TX_DESC_RTS_ENABLE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(12))
#define SET_TX_DESC_HW_RTS_ENABLE(__pdesc, __val)	\
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(13))
#define SET_TX_DESC_NAV_USE_HDR(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(15))
#define SET_TX_DESC_MAX_AGG_NUM(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, GENMASK(21, 17))

/* Dword 4 */
#define SET_TX_DESC_TX_RATE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(6, 0))
#define SET_TX_DESC_DATA_RATE_FB_LIMIT(__pdesc, __val)	\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(12, 8))
#define SET_TX_DESC_RTS_RATE_FB_LIMIT(__pdesc, __val)	\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(16, 13))
#define SET_TX_DESC_RTS_RATE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(28, 24))

/* Dword 5 */
#define SET_TX_DESC_TX_SUB_CARRIER(__pdesc, __val)	\
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(3, 0))
#define SET_TX_DESC_DATA_BW(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(6, 5))
#define SET_TX_DESC_RTS_SHORT(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, BIT(12))
#define SET_TX_DESC_RTS_SC(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(16, 13))

/* Dword 7 */
#define SET_TX_DESC_TX_BUFFER_SIZE(__pdesc, __val)	\
	le32p_replace_bits((__le32 *)(__pdesc + 28), __val, GENMASK(15, 0))

/* Dword 9 */
#define SET_TX_DESC_SEQ(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 36), __val, GENMASK(23, 12))

/* Dword 10 */
#define SET_TX_DESC_TX_BUFFER_ADDRESS(__pdesc, __val)	\
	*(__le32 *)(__pdesc + 40) = cpu_to_le32(__val)

/* Dword 11*/
#define SET_TX_DESC_NEXT_DESC_ADDRESS(__pdesc, __val)	\
	*(__le32 *)(__pdesc + 48) = cpu_to_le32(__val)

#define SET_EARLYMODE_PKTNUM(__paddr, __val)		\
	le32p_replace_bits((__le32 *)__paddr, __val, GENMASK(3, 0))
#define SET_EARLYMODE_LEN0(__paddr, __val)		\
	le32p_replace_bits((__le32 *)__paddr, __val, GENMASK(18, 4))
#define SET_EARLYMODE_LEN1(__paddr, __val)		\
	le32p_replace_bits((__le32 *)__paddr, __val, GENMASK(17, 16))
#define SET_EARLYMODE_LEN2_1(__paddr, __val)		\
	le32p_replace_bits((__le32 *)__paddr, __val, GENMASK(5, 2))
#define SET_EARLYMODE_LEN2_2(__paddr, __val)		\
	le32p_replace_bits((__le32 *)(__paddr + 4), __val, GENMASK(7, 0))
#define SET_EARLYMODE_LEN3(__paddr, __val)		\
	le32p_replace_bits((__le32 *)(__paddr + 4), __val, GENMASK(31, 17))
#define SET_EARLYMODE_LEN4(__paddr, __val)		\
	le32p_replace_bits((__le32 *)(__paddr + 4), __val, GENMASK(31, 20))

/* TX/RX buffer descriptor */

/* for Txfilldescroptor92ee, fill the desc content. */
#define SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pdesc, __offset, __val)            \
	le32p_replace_bits((__le32 *)(__pdesc + 16 * __offset), __val,	\
			   GENMASK(15, 0))
#define SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pdesc, __offset, __val)          \
	le32p_replace_bits((__le32 *)(__pdesc + 16 * __offset), __val, BIT(31))
#define SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pdesc, __offset, __val)        \
	*(__le32 *)(__pdesc + 16 * __offset + 4) = cpu_to_le32(__val)
#define SET_TXBUFFER_DESC_ADD_HIGH_WITH_OFFSET(pbd, off, val, dma64)	       \
	 *(__le32 *)(pbd + 16 * off + 8) = dma64 ? cpu_to_le32(val) : 0
#define GET_TXBUFFER_DESC_ADDR_LOW(__pdesc, __offset)                          \
	le32_to_cpu(*((__le32 *)(__pdesc + 16 * __offset + 4)))
#define GET_TXBUFFER_DESC_ADDR_HIGH(pbd, off, dma64)			       \
	(dma64 ? le32_to_cpu(*((__le32 *)(pbd + 16 * off + 8))) : 0)

/* Dword 0 */
#define SET_TX_BUFF_DESC_LEN_0(__pdesc, __val)                                 \
	le32p_replace_bits((__le32 *)__pdesc, __val, GENMASK(13, 0))
#define SET_TX_BUFF_DESC_PSB(__pdesc, __val)                                   \
	le32p_replace_bits((__le32 *)__pdesc, __val, GENMASK(30, 16))
#define SET_TX_BUFF_DESC_OWN(__pdesc, __val)                                   \
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(31))

/* Dword 1 */
#define SET_TX_BUFF_DESC_ADDR_LOW_0(__pdesc, __val)                            \
	*(__le32 *)(__pdesc + 4) = cpu_to_le32(__val)
/* Dword 2 */
#define SET_TX_BUFF_DESC_ADDR_HIGH_0(bdesc, val, dma64)			       \
	SET_TXBUFFER_DESC_ADD_HIGH_WITH_OFFSET(bdesc, 0, val, dma64)

/* RX buffer  */

/* DWORD 0 */
#define SET_RX_BUFFER_DESC_DATA_LENGTH(__status, __val)	\
	le32p_replace_bits((__le32 *)__status, __val, GENMASK(13, 0))
#define SET_RX_BUFFER_DESC_LS(__status, __val)		\
	le32p_replace_bits((__le32 *)__status, __val, BIT(15))
#define SET_RX_BUFFER_DESC_FS(__status, __val)		\
	le32p_replace_bits((__le32 *)__status, __val, BIT(16))
#define SET_RX_BUFFER_DESC_TOTAL_LENGTH(__status, __val)	\
	le32p_replace_bits((__le32 *)__status, __val, GENMASK(30, 16))

#define GET_RX_BUFFER_DESC_LS(__status)			\
	le32_get_bits(*((__le32 *)__status), BIT(15))
#define GET_RX_BUFFER_DESC_FS(__status)			\
	le32_get_bits(*((__le32 *)__status), BIT(16))
#define GET_RX_BUFFER_DESC_TOTAL_LENGTH(__status)	\
	le32_get_bits(*((__le32 *)__status), GENMASK(30, 16))

/* DWORD 1 */
#define SET_RX_BUFFER_PHYSICAL_LOW(__status, __val)	\
	*(__le32 *)(__status + 4) = cpu_to_le32(__val)

/* DWORD 2 */
#define SET_RX_BUFFER_PHYSICAL_HIGH(__rx_status_desc, __val, dma64)            \
	*(__le32 *)(__rx_status_desc + 8) = (dma64 ? cpu_to_le32(__val) : 0)

#define GET_RX_DESC_PKT_LEN(__pdesc)			\
	le32_get_bits(*(__le32 *)__pdesc, GENMASK(13, 0))
#define GET_RX_DESC_CRC32(__pdesc)			\
	le32_get_bits(*(__le32 *)__pdesc, BIT(14))
#define GET_RX_DESC_ICV(__pdesc)			\
	le32_get_bits(*(__le32 *)__pdesc, BIT(15))
#define GET_RX_DESC_DRV_INFO_SIZE(__pdesc)		\
	le32_get_bits(*(__le32 *)__pdesc, GENMASK(19, 16))
#define GET_RX_DESC_SHIFT(__pdesc)			\
	le32_get_bits(*(__le32 *)__pdesc, GENMASK(25, 24))
#define GET_RX_DESC_PHYST(__pdesc)			\
	le32_get_bits(*(__le32 *)__pdesc, BIT(26))
#define GET_RX_DESC_SWDEC(__pdesc)			\
	le32_get_bits(*(__le32 *)__pdesc, BIT(27))
#define GET_RX_DESC_OWN(__pdesc)			\
	le32_get_bits(*(__le32 *)__pdesc, BIT(31))

#define SET_RX_DESC_EOR(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(30))

#define GET_RX_DESC_MACID(__pdesc)			\
	le32_get_bits(*(__le32 *)(__pdesc + 4), GENMASK(6, 0))
#define GET_RX_DESC_PAGGR(__pdesc)			\
	le32_get_bits(*(__le32 *)(__pdesc + 4), BIT(15))
#define GET_RX_STATUS_DESC_RPT_SEL(__pdesc)		\
	le32_get_bits(*(__le32 *)(__pdesc + 8), BIT(28))

#define GET_RX_DESC_RXMCS(__pdesc)			\
	le32_get_bits(*(__le32 *)(__pdesc + 12), GENMASK(6, 0))
#define GET_RX_STATUS_DESC_PATTERN_MATCH(__pdesc)	\
	le32_get_bits(*(__le32 *)(__pdesc + 12), BIT(29))
#define GET_RX_STATUS_DESC_UNICAST_MATCH(__pdesc)	\
	le32_get_bits(*(__le32 *)(__pdesc + 12), BIT(30))
#define GET_RX_STATUS_DESC_MAGIC_MATCH(__pdesc)		\
	le32_get_bits(*(__le32 *)(__pdesc + 12), BIT(31))

#define GET_RX_DESC_TSFL(__pdesc)			\
	le32_to_cpu(*((__le32 *)(__pdesc + 20)))

#define GET_RX_DESC_BUFF_ADDR(__pdesc)			\
	le32_to_cpu(*((__le32 *)(__pdesc + 24)))

/* TX report 2 format in Rx desc*/

#define GET_RX_RPT2_DESC_MACID_VALID_1(__status)	\
	le32_to_cpu(*((__le32 *)(__status + 16)))
#define GET_RX_RPT2_DESC_MACID_VALID_2(__status)	\
	le32_to_cpu(*((__le32 *)(__status + 20)))

#define CLEAR_PCI_TX_DESC_CONTENT(__pdesc, _size)		\
do {								\
	if (_size > TX_DESC_NEXT_DESC_OFFSET)			\
		memset(__pdesc, 0, TX_DESC_NEXT_DESC_OFFSET);	\
	else							\
		memset(__pdesc, 0, _size);			\
} while (0)

#define RTL92EE_RX_HAL_IS_CCK_RATE(rxmcs)\
	(rxmcs == DESC_RATE1M ||\
	 rxmcs == DESC_RATE2M ||\
	 rxmcs == DESC_RATE5_5M ||\
	 rxmcs == DESC_RATE11M)

#define IS_LITTLE_ENDIAN	1

struct phy_rx_agc_info_t {
	#if IS_LITTLE_ENDIAN
		u8 gain:7, trsw:1;
	#else
		u8 trsw:1, gain:7;
	#endif
};

struct phy_status_rpt {
	struct phy_rx_agc_info_t path_agc[2];
	u8 ch_corr[2];
	u8 cck_sig_qual_ofdm_pwdb_all;
	u8 cck_agc_rpt_ofdm_cfosho_a;
	u8 cck_rpt_b_ofdm_cfosho_b;
	u8 rsvd_1;
	u8 noise_power_db_msb;
	u8 path_cfotail[2];
	u8 pcts_mask[2];
	u8 stream_rxevm[2];
	u8 path_rxsnr[2];
	u8 noise_power_db_lsb;
	u8 rsvd_2[3];
	u8 stream_csi[2];
	u8 stream_target_csi[2];
	u8 sig_evm;
	u8 rsvd_3;
#if IS_LITTLE_ENDIAN
	u8 antsel_rx_keep_2:1;	/*ex_intf_flg:1;*/
	u8 sgi_en:1;
	u8 rxsc:2;
	u8 idle_long:1;
	u8 r_ant_train_en:1;
	u8 ant_sel_b:1;
	u8 ant_sel:1;
#else	/* _BIG_ENDIAN_	*/
	u8 ant_sel:1;
	u8 ant_sel_b:1;
	u8 r_ant_train_en:1;
	u8 idle_long:1;
	u8 rxsc:2;
	u8 sgi_en:1;
	u8 antsel_rx_keep_2:1;	/*ex_intf_flg:1;*/
#endif
} __packed;

struct rx_fwinfo {
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
	u8 ex_intf_flag:1;
	u8 sgi_en:1;
	u8 rxsc:2;
	u8 reserve:4;
} __packed;

struct tx_desc {
	u32 pktsize:16;
	u32 offset:8;
	u32 bmc:1;
	u32 htc:1;
	u32 lastseg:1;
	u32 firstseg:1;
	u32 linip:1;
	u32 noacm:1;
	u32 gf:1;
	u32 own:1;

	u32 macid:6;
	u32 rsvd0:2;
	u32 queuesel:5;
	u32 rd_nav_ext:1;
	u32 lsig_txop_en:1;
	u32 pifs:1;
	u32 rateid:4;
	u32 nav_usehdr:1;
	u32 en_descid:1;
	u32 sectype:2;
	u32 pktoffset:8;

	u32 rts_rc:6;
	u32 data_rc:6;
	u32 agg_en:1;
	u32 rdg_en:1;
	u32 bar_retryht:2;
	u32 agg_break:1;
	u32 morefrag:1;
	u32 raw:1;
	u32 ccx:1;
	u32 ampdudensity:3;
	u32 bt_int:1;
	u32 ant_sela:1;
	u32 ant_selb:1;
	u32 txant_cck:2;
	u32 txant_l:2;
	u32 txant_ht:2;

	u32 nextheadpage:8;
	u32 tailpage:8;
	u32 seq:12;
	u32 cpu_handle:1;
	u32 tag1:1;
	u32 trigger_int:1;
	u32 hwseq_en:1;

	u32 rtsrate:5;
	u32 apdcfe:1;
	u32 qos:1;
	u32 hwseq_ssn:1;
	u32 userrate:1;
	u32 dis_rtsfb:1;
	u32 dis_datafb:1;
	u32 cts2self:1;
	u32 rts_en:1;
	u32 hwrts_en:1;
	u32 portid:1;
	u32 pwr_status:3;
	u32 waitdcts:1;
	u32 cts2ap_en:1;
	u32 txsc:2;
	u32 stbc:2;
	u32 txshort:1;
	u32 txbw:1;
	u32 rtsshort:1;
	u32 rtsbw:1;
	u32 rtssc:2;
	u32 rtsstbc:2;

	u32 txrate:6;
	u32 shortgi:1;
	u32 ccxt:1;
	u32 txrate_fb_lmt:5;
	u32 rtsrate_fb_lmt:4;
	u32 retrylmt_en:1;
	u32 txretrylmt:6;
	u32 usb_txaggnum:8;

	u32 txagca:5;
	u32 txagcb:5;
	u32 usemaxlen:1;
	u32 maxaggnum:5;
	u32 mcsg1maxlen:4;
	u32 mcsg2maxlen:4;
	u32 mcsg3maxlen:4;
	u32 mcs7sgimaxlen:4;

	u32 txbuffersize:16;
	u32 sw_offset30:8;
	u32 sw_offset31:4;
	u32 rsvd1:1;
	u32 antsel_c:1;
	u32 null_0:1;
	u32 null_1:1;

	u32 txbuffaddr;
	u32 txbufferaddr64;
	u32 nextdescaddress;
	u32 nextdescaddress64;

	u32 reserve_pass_pcie_mm_limit[4];
} __packed;

struct rx_desc {
	u32 length:14;
	u32 crc32:1;
	u32 icverror:1;
	u32 drv_infosize:4;
	u32 security:3;
	u32 qos:1;
	u32 shift:2;
	u32 phystatus:1;
	u32 swdec:1;
	u32 lastseg:1;
	u32 firstseg:1;
	u32 eor:1;
	u32 own:1;

	u32 macid:6;
	u32 tid:4;
	u32 hwrsvd:5;
	u32 paggr:1;
	u32 faggr:1;
	u32 a1_fit:4;
	u32 a2_fit:4;
	u32 pam:1;
	u32 pwr:1;
	u32 moredata:1;
	u32 morefrag:1;
	u32 type:2;
	u32 mc:1;
	u32 bc:1;

	u32 seq:12;
	u32 frag:4;
	u32 nextpktlen:14;
	u32 nextind:1;
	u32 rsvd:1;

	u32 rxmcs:6;
	u32 rxht:1;
	u32 amsdu:1;
	u32 splcp:1;
	u32 bandwidth:1;
	u32 htc:1;
	u32 tcpchk_rpt:1;
	u32 ipcchk_rpt:1;
	u32 tcpchk_valid:1;
	u32 hwpcerr:1;
	u32 hwpcind:1;
	u32 iv0:16;

	u32 iv1;

	u32 tsfl;

	u32 bufferaddress;
	u32 bufferaddress64;

} __packed;

void rtl92ee_rx_check_dma_ok(struct ieee80211_hw *hw, u8 *header_desc,
			     u8 queue_index);
u16	rtl92ee_rx_desc_buff_remained_cnt(struct ieee80211_hw *hw,
					  u8 queue_index);
u16 rtl92ee_get_available_desc(struct ieee80211_hw *hw, u8 queue_index);
void rtl92ee_pre_fill_tx_bd_desc(struct ieee80211_hw *hw,
				 u8 *tx_bd_desc, u8 *desc, u8 queue_index,
				 struct sk_buff *skb, dma_addr_t addr);

void rtl92ee_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc_tx,
			  u8 *pbd_desc_tx,
			  struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb,
			  u8 hw_queue, struct rtl_tcb_desc *ptcb_desc);
bool rtl92ee_rx_query_desc(struct ieee80211_hw *hw,
			   struct rtl_stats *status,
			   struct ieee80211_rx_status *rx_status,
			   u8 *pdesc, struct sk_buff *skb);
void rtl92ee_set_desc(struct ieee80211_hw *hw, u8 *pdesc, bool istx,
		      u8 desc_name, u8 *val);

u64 rtl92ee_get_desc(struct ieee80211_hw *hw,
		     u8 *pdesc, bool istx, u8 desc_name);
bool rtl92ee_is_tx_desc_closed(struct ieee80211_hw *hw, u8 hw_queue, u16 index);
void rtl92ee_tx_polling(struct ieee80211_hw *hw, u8 hw_queue);
void rtl92ee_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc,
			     bool firstseg, bool lastseg,
			     struct sk_buff *skb);
#endif
