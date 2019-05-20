/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2010  Realtek Corporation.*/

#ifndef __RTL8821AE_TRX_H__
#define __RTL8821AE_TRX_H__

#define TX_DESC_SIZE					40
#define TX_DESC_AGGR_SUBFRAME_SIZE		32

#define RX_DESC_SIZE					32
#define RX_DRV_INFO_SIZE_UNIT			8

#define	TX_DESC_NEXT_DESC_OFFSET		40
#define USB_HWDESC_HEADER_LEN			40
#define CRCLENGTH						4

static inline void set_tx_desc_pkt_size(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)__pdesc, __val, GENMASK(15, 0));
}

static inline void set_tx_desc_offset(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)__pdesc, __val, GENMASK(23, 16));
}

static inline void set_tx_desc_bmc(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(24));
}

static inline void set_tx_desc_htc(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(25));
}

static inline void set_tx_desc_last_seg(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(26));
}

static inline void set_tx_desc_first_seg(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(27));
}

static inline void set_tx_desc_linip(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(28));
}

static inline void set_tx_desc_own(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(31));
}

static inline int get_tx_desc_own(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)__pdesc), BIT(31));
}

static inline void set_tx_desc_macid(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(6, 0));
}

static inline void set_tx_desc_queue_sel(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(12, 8));
}

static inline void set_tx_desc_rate_id(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(20, 16));
}

static inline void set_tx_desc_sec_type(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(23, 22));
}

static inline void set_tx_desc_pkt_offset(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(28, 24));
}

static inline void set_tx_desc_agg_enable(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, BIT(12));
}

static inline void set_tx_desc_rdg_enable(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, BIT(13));
}

static inline void set_tx_desc_more_frag(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, BIT(17));
}

static inline void set_tx_desc_ampdu_density(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, GENMASK(22, 20));
}

static inline void set_tx_desc_hwseq_sel(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, GENMASK(7, 6));
}

static inline void set_tx_desc_use_rate(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(8));
}

static inline void set_tx_desc_disable_fb(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(10));
}

static inline void set_tx_desc_cts2self(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(11));
}

static inline void set_tx_desc_rts_enable(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(12));
}

static inline void set_tx_desc_hw_rts_enable(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(13));
}

static inline void set_tx_desc_nav_use_hdr(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, BIT(15));
}

static inline void set_tx_desc_max_agg_num(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, GENMASK(21, 17));
}

static inline void set_tx_desc_tx_ant(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(27, 24));
}

static inline void set_tx_desc_tx_rate(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(6, 0));
}

static inline void set_tx_desc_data_rate_fb_limit(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(12, 8));
}

static inline void set_tx_desc_rts_rate_fb_limit(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(16, 13));
}

static inline void set_tx_desc_rts_rate(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(28, 24));
}

static inline void set_tx_desc_tx_sub_carrier(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(3, 0));
}

static inline void set_tx_desc_data_shortgi(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, BIT(4));
}

static inline void set_tx_desc_data_bw(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(6, 5));
}

static inline void set_tx_desc_rts_short(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, BIT(12));
}

static inline void set_tx_desc_rts_sc(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(16, 13));
}

static inline void set_tx_desc_tx_buffer_size(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 28), __val, GENMASK(15, 0));
}

static inline void set_tx_desc_hwseq_en(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 32), __val, BIT(15));
}

static inline void set_tx_desc_seq(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc + 36), __val, GENMASK(23, 12));
}

static inline void set_tx_desc_tx_buffer_address(u8 *__pdesc, u32 __val)
{
	*(__le32 *)(__pdesc + 40) = cpu_to_le32(__val);
}

static inline int get_tx_desc_tx_buffer_address(u8 *__pdesc)
{
	return le32_to_cpu(*((__le32 *)(__pdesc + 40)));
}

static inline void set_tx_desc_next_desc_address(u8 *__pdesc, u32 __val)
{
	*(__le32 *)(__pdesc + 48) = cpu_to_le32(__val);
}

static inline int get_rx_desc_pkt_len(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc)), GENMASK(13, 0));
}

static inline int get_rx_desc_crc32(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc)), BIT(14));
}

static inline int get_rx_desc_icv(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc)), BIT(15));
}

static inline int get_rx_desc_drv_info_size(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc)), GENMASK(19, 16));
}

static inline int get_rx_desc_shift(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc)), GENMASK(25, 24));
}

static inline int get_rx_desc_physt(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc)), BIT(26));
}

static inline int get_rx_desc_swdec(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc)), BIT(27));
}

static inline int get_rx_desc_own(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc)), BIT(31));
}

static inline void set_rx_desc_pkt_len(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc), __val, GENMASK(13, 0));
}

static inline void set_rx_desc_eor(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc), __val, BIT(30));
}

static inline void set_rx_desc_own(u8 *__pdesc, u32 __val)
{
	le32p_replace_bits((__le32 *)(__pdesc), __val, BIT(31));
}

static inline int get_rx_desc_macid(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc + 4)), GENMASK(6, 0));
}

static inline int get_rx_desc_paggr(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc + 4)), BIT(15));
}

static inline int get_rx_status_desc_rpt_sel(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc + 4)), BIT(28));
}

static inline int get_rx_desc_rxmcs(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc + 12)), GENMASK(6, 0));
}

static inline int get_rx_status_desc_pattern_match(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc + 12)), BIT(29));
}

static inline int get_rx_status_desc_unicast_match(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc + 12)), BIT(30));
}

static inline int get_rx_status_desc_magic_match(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc + 12)), BIT(31));
}

static inline int get_rx_desc_splcp(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc + 16)), BIT(0));
}

static inline int get_rx_desc_bw(u8 *__pdesc)
{
	return le32_get_bits(*((__le32 *)(__pdesc + 16)), GENMASK(5, 4));
}

static inline int get_rx_desc_tsfl(u8 *__pdesc)
{
	return le32_to_cpu(*((__le32 *)(__pdesc + 20)));
}

static inline int get_rx_desc_buff_addr(u8 *__pdesc)
{
	return le32_to_cpu(*((__le32 *)(__pdesc + 24)));
}

static inline void set_rx_desc_buff_addr(u8 *__pdesc, u32 __val)
{
	*(__le32 *)(__pdesc + 24) = cpu_to_le32(__val);
}

/* TX report 2 format in Rx desc*/

static inline int get_rx_rpt2_desc_macid_valid_1(u8 *__status)
{
	return le32_to_cpu(*((__le32 *)(__status + 16)));
}

static inline int get_rx_rpt2_desc_macid_valid_2(u8 *__status)
{
	return le32_to_cpu(*((__le32 *)(__status + 20)));
}

static inline void set_earlymode_pktnum(u8 *__paddr, u32 __value)
{
	le32p_replace_bits((__le32 *)(__paddr), __value, GENMASK(3, 0));
}

static inline void set_earlymode_len0(u8 *__paddr, u32 __value)
{
	le32p_replace_bits((__le32 *)(__paddr), __value, GENMASK(15, 4));
}

static inline void set_earlymode_len1(u8 *__paddr, u32 __value)
{
	le32p_replace_bits((__le32 *)(__paddr), __value, GENMASK(27, 16));
}

static inline void set_earlymode_len2_1(u8 *__paddr, u32 __value)
{
	le32p_replace_bits((__le32 *)(__paddr), __value, GENMASK(31, 28));
}

static inline void set_earlymode_len2_2(u8 *__paddr, u32 __value)
{
	le32p_replace_bits((__le32 *)(__paddr), __value, GENMASK(7, 0));
}

static inline void set_earlymode_len3(u8 *__paddr, u32 __value)
{
	le32p_replace_bits((__le32 *)(__paddr + 4), __value, GENMASK(19, 8));
}

static inline void set_earlymode_len4(u8 *__paddr, u32 __value)
{
	le32p_replace_bits((__le32 *)(__paddr + 4), __value, GENMASK(31, 20));
}

#define CLEAR_PCI_TX_DESC_CONTENT(__pdesc, _size)		\
do {								\
	if (_size > TX_DESC_NEXT_DESC_OFFSET)			\
		memset(__pdesc, 0, TX_DESC_NEXT_DESC_OFFSET);	\
	else							\
		memset(__pdesc, 0, _size);			\
} while (0)

#define RTL8821AE_RX_HAL_IS_CCK_RATE(rxmcs)\
	(rxmcs == DESC_RATE1M ||\
	 rxmcs == DESC_RATE2M ||\
	 rxmcs == DESC_RATE5_5M ||\
	 rxmcs == DESC_RATE11M)

struct phy_rx_agc_info_t {
	#ifdef __LITTLE_ENDIAN
		u8	gain:7, trsw:1;
	#else
		u8	trsw:1, gain:7;
	#endif
};

struct phy_status_rpt {
	/* DWORD 0 */
	u8 gain_trsw[2];
#ifdef __LITTLE_ENDIAN
	u16 chl_num:10;
	u16 sub_chnl:4;
	u16 r_rfmod:2;
#else	/* _BIG_ENDIAN_ */
	u16 r_rfmod:2;
	u16 sub_chnl:4;
	u16 chl_num:10;
#endif
	/* DWORD 1 */
	u8 pwdb_all;
	u8 cfosho[4];	/* DW 1 byte 1 DW 2 byte 0 */

	/* DWORD 2 */
	s8 cfotail[4];	/* DW 2 byte 1 DW 3 byte 0 */

	/* DWORD 3 */
	s8 rxevm[2];	/* DW 3 byte 1 DW 3 byte 2 */
	s8 rxsnr[2];	/* DW 3 byte 3 DW 4 byte 0 */

	/* DWORD 4 */
	u8 pcts_msk_rpt[2];
	u8 pdsnr[2];	/* DW 4 byte 3 DW 5 Byte 0 */

	/* DWORD 5 */
	u8 csi_current[2];
	u8 rx_gain_c;

	/* DWORD 6 */
	u8 rx_gain_d;
	u8 sigevm;
	u8 resvd_0;
	u8 antidx_anta:3;
	u8 antidx_antb:3;
	u8 resvd_1:2;
} __packed;

struct rx_fwinfo_8821ae {
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

struct tx_desc_8821ae {
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

struct rx_desc_8821ae {
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

void rtl8821ae_tx_fill_desc(struct ieee80211_hw *hw,
			    struct ieee80211_hdr *hdr, u8 *pdesc_tx, u8 *txbd,
			    struct ieee80211_tx_info *info,
			    struct ieee80211_sta *sta,
			    struct sk_buff *skb,
			    u8 hw_queue, struct rtl_tcb_desc *ptcb_desc);
bool rtl8821ae_rx_query_desc(struct ieee80211_hw *hw,
			     struct rtl_stats *status,
			     struct ieee80211_rx_status *rx_status,
			     u8 *pdesc, struct sk_buff *skb);
void rtl8821ae_set_desc(struct ieee80211_hw *hw, u8 *pdesc,
			bool istx, u8 desc_name, u8 *val);
u64 rtl8821ae_get_desc(struct ieee80211_hw *hw,
		       u8 *pdesc, bool istx, u8 desc_name);
bool rtl8821ae_is_tx_desc_closed(struct ieee80211_hw *hw,
				 u8 hw_queue, u16 index);
void rtl8821ae_tx_polling(struct ieee80211_hw *hw, u8 hw_queue);
void rtl8821ae_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc,
			       bool firstseg, bool lastseg,
			       struct sk_buff *skb);
#endif
