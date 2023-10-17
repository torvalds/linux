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

static inline void set_tx_desc_pkt_size(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(15, 0));
}

static inline void set_tx_desc_offset(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(23, 16));
}

static inline void set_tx_desc_bmc(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(24));
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

static inline int get_tx_desc_own(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), BIT(31));
}

static inline void set_tx_desc_macid(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(6, 0));
}

static inline void set_tx_desc_queue_sel(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(12, 8));
}

static inline void set_tx_desc_rate_id(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(20, 16));
}

static inline void set_tx_desc_sec_type(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(23, 22));
}

static inline void set_tx_desc_pkt_offset(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(28, 24));
}

static inline void set_tx_desc_agg_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 2), __val, BIT(12));
}

static inline void set_tx_desc_rdg_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 2), __val, BIT(13));
}

static inline void set_tx_desc_more_frag(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 2), __val, BIT(17));
}

static inline void set_tx_desc_ampdu_density(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 2), __val, GENMASK(22, 20));
}

static inline void set_tx_desc_use_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, BIT(8));
}

static inline void set_tx_desc_disable_fb(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, BIT(10));
}

static inline void set_tx_desc_cts2self(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, BIT(11));
}

static inline void set_tx_desc_rts_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, BIT(12));
}

static inline void set_tx_desc_hw_rts_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, BIT(13));
}

static inline void set_tx_desc_nav_use_hdr(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, BIT(15));
}

static inline void set_tx_desc_max_agg_num(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, GENMASK(21, 17));
}

/* Dword 4 */
static inline void set_tx_desc_tx_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(6, 0));
}

static inline void set_tx_desc_data_rate_fb_limit(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(12, 8));
}

static inline void set_tx_desc_rts_rate_fb_limit(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(16, 13));
}

static inline void set_tx_desc_rts_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(28, 24));
}

/* Dword 5 */
static inline void set_tx_desc_tx_sub_carrier(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, GENMASK(3, 0));
}

static inline void set_tx_desc_data_bw(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(6, 5));
}

static inline void set_tx_desc_rts_short(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, BIT(12));
}

static inline void set_tx_desc_rts_sc(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, GENMASK(16, 13));
}

/* Dword 7 */
static inline void set_tx_desc_tx_buffer_size(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 7), __val, GENMASK(15, 0));
}

/* Dword 9 */
static inline void set_tx_desc_seq(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 9), __val, GENMASK(23, 12));
}

/* Dword 10 */
static inline void set_tx_desc_tx_buffer_address(__le32 *__pdesc, u32 __val)
{
	*(__pdesc + 10) = cpu_to_le32(__val);
}

/* Dword 11*/
static inline void set_tx_desc_next_desc_address(__le32 *__pdesc, u32 __val)
{
	*(__pdesc + 12) = cpu_to_le32(__val);
}

static inline void set_earlymode_pktnum(__le32 *__paddr, u32 __val)
{
	le32p_replace_bits(__paddr, __val, GENMASK(3, 0));
}

static inline void set_earlymode_len0(__le32 *__paddr, u32 __val)
{
	le32p_replace_bits(__paddr, __val, GENMASK(18, 4));
}

static inline void set_earlymode_len1(__le32 *__paddr, u32 __val)
{
	le32p_replace_bits(__paddr, __val, GENMASK(17, 16));
}

static inline void set_earlymode_len2_1(__le32 *__paddr, u32 __val)
{
	le32p_replace_bits(__paddr, __val, GENMASK(5, 2));
}

static inline void set_earlymode_len2_2(__le32 *__paddr, u32 __val)
{
	le32p_replace_bits((__paddr + 1), __val, GENMASK(7, 0));
}

static inline void set_earlymode_len3(__le32 *__paddr, u32 __val)
{
	le32p_replace_bits((__paddr + 1), __val, GENMASK(31, 17));
}

static inline void set_earlymode_len4(__le32 *__paddr, u32 __val)
{
	le32p_replace_bits((__paddr + 1), __val, GENMASK(31, 20));
}

/* TX/RX buffer descriptor */

/* for Txfilldescroptor92ee, fill the desc content. */
static inline void set_txbuffer_desc_len_with_offset(__le32 *__pdesc,
						     u8 __offset, u32 __val)
{
	le32p_replace_bits((__pdesc + 4 * __offset), __val,
			   GENMASK(15, 0));
}

static inline void set_txbuffer_desc_amsdu_with_offset(__le32 *__pdesc,
						       u8 __offset, u32 __val)
{
	le32p_replace_bits((__pdesc + 4 * __offset), __val, BIT(31));
}

static inline void set_txbuffer_desc_add_low_with_offset(__le32 *__pdesc,
							 u8 __offset,
							 u32 __val)
{
	*(__pdesc + 4 * __offset + 1) = cpu_to_le32(__val);
}

static inline void set_txbuffer_desc_add_high_with_offset(__le32 *pbd, u8 off,
							  u32 val, bool dma64)
{
	if (dma64)
		*(pbd + 4 * off + 2) = cpu_to_le32(val);
	else
		*(pbd + 4 * off + 2) = 0;
}

static inline u32 get_txbuffer_desc_addr_low(__le32 *__pdesc, u8 __offset)
{
	return le32_to_cpu(*((__pdesc + 4 * __offset + 1)));
}

static inline u32 get_txbuffer_desc_addr_high(__le32 *pbd, u32 off, bool dma64)
{
	if (dma64)
		return le32_to_cpu(*((pbd + 4 * off + 2)));
	return 0;
}

/* Dword 0 */
static inline void set_tx_buff_desc_len_0(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(13, 0));
}

static inline void set_tx_buff_desc_psb(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(30, 16));
}

static inline void set_tx_buff_desc_own(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(31));
}

/* Dword 1 */
static inline void set_tx_buff_desc_addr_low_0(__le32 *__pdesc, u32 __val)
{
	*(__pdesc + 1) = cpu_to_le32(__val);
}

/* Dword 2 */
static inline void set_tx_buff_desc_addr_high_0(__le32 *pdesc, u32 val,
						bool dma64)
{
	if (dma64)
		*(pdesc + 2) = cpu_to_le32(val);
	else
		*(pdesc + 2) = 0;
}

/* RX buffer  */

/* DWORD 0 */
static inline void set_rx_buffer_desc_data_length(__le32 *__status, u32 __val)
{
	le32p_replace_bits(__status, __val, GENMASK(13, 0));
}

static inline void set_rx_buffer_desc_ls(__le32 *__status, u32 __val)
{
	le32p_replace_bits(__status, __val, BIT(15));
}

static inline void set_rx_buffer_desc_fs(__le32 *__status, u32 __val)
{
	le32p_replace_bits(__status, __val, BIT(16));
}

static inline void set_rx_buffer_desc_total_length(__le32 *__status, u32 __val)
{
	le32p_replace_bits(__status, __val, GENMASK(30, 16));
}

static inline int get_rx_buffer_desc_ls(__le32 *__status)
{
	return le32_get_bits(*(__status), BIT(15));
}

static inline int get_rx_buffer_desc_fs(__le32 *__status)
{
	return le32_get_bits(*(__status), BIT(16));
}

static inline int get_rx_buffer_desc_total_length(__le32 *__status)
{
	return le32_get_bits(*(__status), GENMASK(30, 16));
}

/* DWORD 1 */
static inline void set_rx_buffer_physical_low(__le32 *__status, u32 __val)
{
	*(__status + 1) = cpu_to_le32(__val);
}

/* DWORD 2 */
static inline void set_rx_buffer_physical_high(__le32 *__rx_status_desc,
					       u32 __val, bool dma64)
{
	if (dma64)
		*(__rx_status_desc + 2) = cpu_to_le32(__val);
	else
		*(__rx_status_desc + 2) = 0;
}

static inline int get_rx_desc_pkt_len(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, GENMASK(13, 0));
}

static inline int get_rx_desc_crc32(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(14));
}

static inline int get_rx_desc_icv(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(15));
}

static inline int get_rx_desc_drv_info_size(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, GENMASK(19, 16));
}

static inline int get_rx_desc_shift(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, GENMASK(25, 24));
}

static inline int get_rx_desc_physt(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(26));
}

static inline int get_rx_desc_swdec(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(27));
}

static inline int get_rx_desc_own(__le32 *__pdesc)
{
	return le32_get_bits(*__pdesc, BIT(31));
}

static inline void set_rx_desc_eor(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(30));
}

static inline int get_rx_desc_macid(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 1), GENMASK(6, 0));
}

static inline int get_rx_desc_paggr(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 1), BIT(15));
}

static inline int get_rx_status_desc_rpt_sel(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 2), BIT(28));
}

static inline int get_rx_desc_rxmcs(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), GENMASK(6, 0));
}

static inline int get_rx_status_desc_pattern_match(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), BIT(29));
}

static inline int get_rx_status_desc_unicast_match(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), BIT(30));
}

static inline int get_rx_status_desc_magic_match(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), BIT(31));
}

static inline u32 get_rx_desc_tsfl(__le32 *__pdesc)
{
	return le32_to_cpu(*((__pdesc + 5)));
}

static inline u32 get_rx_desc_buff_addr(__le32 *__pdesc)
{
	return le32_to_cpu(*((__pdesc + 6)));
}

/* TX report 2 format in Rx desc*/

static inline u32 get_rx_rpt2_desc_macid_valid_1(__le32 *__status)
{
	return le32_to_cpu(*((__status + 4)));
}

static inline u32 get_rx_rpt2_desc_macid_valid_2(__le32 *__status)
{
	return le32_to_cpu(*((__status + 5)));
}

static inline void clear_pci_tx_desc_content(__le32 *__pdesc, int _size)
{
	if (_size > TX_DESC_NEXT_DESC_OFFSET)
		memset(__pdesc, 0, TX_DESC_NEXT_DESC_OFFSET);
	else
		memset(__pdesc, 0, _size);
}

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
			     struct sk_buff *skb);
#endif
