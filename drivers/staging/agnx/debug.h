#ifndef AGNX_DEBUG_H_
#define AGNX_DEBUG_H_

#include "agnx.h"
#include "phy.h"
#include "sta.h"
#include "xmit.h"

#define AGNX_TRACE              printk(KERN_ERR PFX "function:%s line:%d\n", __func__, __LINE__)

#define PRINTK_LE16(prefix, var)	printk(KERN_DEBUG PFX #prefix ": " #var " 0x%.4x\n", le16_to_cpu(var))
#define PRINTK_LE32(prefix, var)	printk(KERN_DEBUG PFX #prefix ": " #var " 0x%.8x\n", le32_to_cpu(var))
#define PRINTK_U8(prefix, var) 		printk(KERN_DEBUG PFX #prefix ": " #var " 0x%.2x\n", var)
#define PRINTK_BE16(prefix, var)	printk(KERN_DEBUG PFX #prefix ": " #var " 0x%.4x\n", be16_to_cpu(var))
#define PRINTK_BE32(prefix, var)	printk(KERN_DEBUG PFX #prefix ": " #var " 0x%.8x\n", be32_to_cpu(var))
#define PRINTK_BITS(prefix, field)    	printk(KERN_DEBUG PFX #prefix ": " #field ": 0x%x\n", (reg & field) >> field##_SHIFT)

static inline void agnx_bug(char *reason)
{
	printk(KERN_ERR PFX "%s\n", reason);
	BUG();
}

static inline void agnx_print_desc(struct agnx_desc *desc)
{
	u32 reg = be32_to_cpu(desc->frag);

	PRINTK_BITS(DESC, PACKET_LEN);

	if (reg & FIRST_FRAG) {
		PRINTK_BITS(DESC, FIRST_PACKET_MASK);
		PRINTK_BITS(DESC, FIRST_RESERV2);
		PRINTK_BITS(DESC, FIRST_TKIP_ERROR);
		PRINTK_BITS(DESC, FIRST_TKIP_PACKET);
		PRINTK_BITS(DESC, FIRST_RESERV1);
		PRINTK_BITS(DESC, FIRST_FRAG_LEN);
	} else {
		PRINTK_BITS(DESC, SUB_RESERV2);
		PRINTK_BITS(DESC, SUB_TKIP_ERROR);
		PRINTK_BITS(DESC, SUB_TKIP_PACKET);
		PRINTK_BITS(DESC, SUB_RESERV1);
		PRINTK_BITS(DESC, SUB_FRAG_LEN);
	}

	PRINTK_BITS(DESC, FIRST_FRAG);
	PRINTK_BITS(DESC, LAST_FRAG);
	PRINTK_BITS(DESC, OWNER);
}


static inline void dump_ieee80211b_phy_hdr(__be32 _11b0, __be32 _11b1)
{

}

static inline void agnx_print_hdr(struct agnx_hdr *hdr)
{
	u32 reg;
	int i;

	reg = be32_to_cpu(hdr->reg0);
	PRINTK_BITS(HDR, RTS);
	PRINTK_BITS(HDR, MULTICAST);
	PRINTK_BITS(HDR, ACK);
	PRINTK_BITS(HDR, TM);
	PRINTK_BITS(HDR, RELAY);
	PRINTK_BITS(HDR, REVISED_FCS);
	PRINTK_BITS(HDR, NEXT_BUFFER_ADDR);

	reg = be32_to_cpu(hdr->reg1);
	PRINTK_BITS(HDR, MAC_HDR_LEN);
	PRINTK_BITS(HDR, DURATION_OVERIDE);
	PRINTK_BITS(HDR, PHY_HDR_OVERIDE);
	PRINTK_BITS(HDR, CRC_FAIL);
	PRINTK_BITS(HDR, SEQUENCE_NUMBER);
	PRINTK_BITS(HDR, BUFF_HEAD_ADDR);

	reg = be32_to_cpu(hdr->reg2);
	PRINTK_BITS(HDR, PDU_COUNT);
	PRINTK_BITS(HDR, WEP_KEY);
	PRINTK_BITS(HDR, USES_WEP_KEY);
	PRINTK_BITS(HDR, KEEP_ALIVE);
	PRINTK_BITS(HDR, BUFF_TAIL_ADDR);

	reg = be32_to_cpu(hdr->reg3);
	PRINTK_BITS(HDR, CTS_11G);
	PRINTK_BITS(HDR, RTS_11G);
	PRINTK_BITS(HDR, FRAG_SIZE);
	PRINTK_BITS(HDR, PAYLOAD_LEN);
	PRINTK_BITS(HDR, FRAG_NUM);

	reg = be32_to_cpu(hdr->reg4);
	PRINTK_BITS(HDR, RELAY_STAID);
	PRINTK_BITS(HDR, STATION_ID);
	PRINTK_BITS(HDR, WORKQUEUE_ID);

	reg = be32_to_cpu(hdr->reg5);
	/* printf the route flag */
	PRINTK_BITS(HDR, ROUTE_HOST);
	PRINTK_BITS(HDR, ROUTE_CARD_CPU);
	PRINTK_BITS(HDR, ROUTE_ENCRYPTION);
	PRINTK_BITS(HDR, ROUTE_TX);
	PRINTK_BITS(HDR, ROUTE_RX1);
	PRINTK_BITS(HDR, ROUTE_RX2);
	PRINTK_BITS(HDR, ROUTE_COMPRESSION);

	PRINTK_BE32(HDR, hdr->_11g0);
	PRINTK_BE32(HDR, hdr->_11g1);
	PRINTK_BE32(HDR, hdr->_11b0);
	PRINTK_BE32(HDR, hdr->_11b1);

	dump_ieee80211b_phy_hdr(hdr->_11b0, hdr->_11b1);

	/* Fixme */
	for (i = 0; i < ARRAY_SIZE(hdr->mac_hdr); i++) {
		if (i == 0)
			printk(KERN_DEBUG PFX "IEEE80211 HDR: ");
		printk("%.2x ", hdr->mac_hdr[i]);
		if (i + 1 == ARRAY_SIZE(hdr->mac_hdr))
			printk("\n");
	}

	PRINTK_BE16(HDR, hdr->rts_duration);
	PRINTK_BE16(HDR, hdr->last_duration);
	PRINTK_BE16(HDR, hdr->sec_last_duration);
	PRINTK_BE16(HDR, hdr->other_duration);
	PRINTK_BE16(HDR, hdr->tx_other_duration);
	PRINTK_BE16(HDR, hdr->last_11g_len);
	PRINTK_BE16(HDR, hdr->other_11g_len);
	PRINTK_BE16(HDR, hdr->last_11b_len);
	PRINTK_BE16(HDR, hdr->other_11b_len);

	/* FIXME */
	reg = be16_to_cpu(hdr->reg6);
	PRINTK_BITS(HDR, MBF);
	PRINTK_BITS(HDR, RSVD4);

	PRINTK_BE16(HDR, hdr->rx_frag_stat);

	PRINTK_BE32(HDR, hdr->time_stamp);
	PRINTK_BE32(HDR, hdr->phy_stats_hi);
	PRINTK_BE32(HDR, hdr->phy_stats_lo);
	PRINTK_BE32(HDR, hdr->mic_key0);
	PRINTK_BE32(HDR, hdr->mic_key1);
} /* agnx_print_hdr */


static inline void agnx_print_rx_hdr(struct agnx_hdr *hdr)
{
	agnx_print_hdr(hdr);

	PRINTK_BE16(HDR, hdr->rx.rx_packet_duration);
	PRINTK_BE16(HDR, hdr->rx.replay_cnt);

	PRINTK_U8(HDR, hdr->rx_channel);
}

static inline void agnx_print_tx_hdr(struct agnx_hdr *hdr)
{
	agnx_print_hdr(hdr);

	PRINTK_U8(HDR, hdr->tx.long_retry_limit);
	PRINTK_U8(HDR, hdr->tx.short_retry_limit);
	PRINTK_U8(HDR, hdr->tx.long_retry_cnt);
	PRINTK_U8(HDR, hdr->tx.short_retry_cnt);

	PRINTK_U8(HDR, hdr->rx_channel);
}

static inline void
agnx_print_sta_power(struct agnx_priv *priv, unsigned int sta_idx)
{
	struct agnx_sta_power power;
	u32 reg;

	get_sta_power(priv, &power, sta_idx);

	reg = le32_to_cpu(power.reg);
	PRINTK_BITS(STA_POWER, SIGNAL);
	PRINTK_BITS(STA_POWER, RATE);
	PRINTK_BITS(STA_POWER, TIFS);
	PRINTK_BITS(STA_POWER, EDCF);
	PRINTK_BITS(STA_POWER, CHANNEL_BOND);
	PRINTK_BITS(STA_POWER, PHY_MODE);
	PRINTK_BITS(STA_POWER, POWER_LEVEL);
	PRINTK_BITS(STA_POWER, NUM_TRANSMITTERS);
}

static inline void
agnx_print_sta_tx_wq(struct agnx_priv *priv, unsigned int sta_idx, unsigned int wq_idx)
{
	struct agnx_sta_tx_wq tx_wq;
	u32 reg;

	get_sta_tx_wq(priv, &tx_wq, sta_idx, wq_idx);

	reg = le32_to_cpu(tx_wq.reg0);
	PRINTK_BITS(STA_TX_WQ, TAIL_POINTER);
	PRINTK_BITS(STA_TX_WQ, HEAD_POINTER_LOW);

	reg = le32_to_cpu(tx_wq.reg3);
	PRINTK_BITS(STA_TX_WQ, HEAD_POINTER_HIGH);
	PRINTK_BITS(STA_TX_WQ, ACK_POINTER_LOW);

	reg = le32_to_cpu(tx_wq.reg1);
	PRINTK_BITS(STA_TX_WQ, ACK_POINTER_HIGH);
	PRINTK_BITS(STA_TX_WQ, HEAD_TIMOUT_TAIL_PACK_CNT);
	PRINTK_BITS(STA_TX_WQ, ACK_TIMOUT_TAIL_PACK_CNT);

	reg = le32_to_cpu(tx_wq.reg2);
	PRINTK_BITS(STA_TX_WQ, HEAD_TIMOUT_WIN_LIM_BYTE_CNT);
	PRINTK_BITS(STA_TX_WQ, HEAD_TIMOUT_WIN_LIM_FRAG_CNT);
	PRINTK_BITS(STA_TX_WQ, WORK_QUEUE_ACK_TYPE);
	PRINTK_BITS(STA_TX_WQ, WORK_QUEUE_VALID);
}

static inline void agnx_print_sta_traffic(struct agnx_sta_traffic *traffic)
{
	u32 reg;

	reg = le32_to_cpu(traffic->reg0);
	PRINTK_BITS(STA_TRAFFIC, ACK_TIMOUT_CNT);
	PRINTK_BITS(STA_TRAFFIC, TRAFFIC_ACK_TYPE);
	PRINTK_BITS(STA_TRAFFIC, NEW_PACKET);
	PRINTK_BITS(STA_TRAFFIC, TRAFFIC_VALID);
	PRINTK_BITS(STA_TRAFFIC, RX_HDR_DESC_POINTER);

	reg = le32_to_cpu(traffic->reg1);
	PRINTK_BITS(STA_TRAFFIC, RX_PACKET_TIMESTAMP);
	PRINTK_BITS(STA_TRAFFIC, TRAFFIC_RESERVED);
	PRINTK_BITS(STA_TRAFFIC, SV);
	PRINTK_BITS(STA_TRAFFIC, RX_SEQUENCE_NUM);

	PRINTK_LE32(STA_TRAFFIC, traffic->tx_replay_cnt_low);

	PRINTK_LE16(STA_TRAFFIC, traffic->tx_replay_cnt_high);
	PRINTK_LE16(STA_TRAFFIC, traffic->rx_replay_cnt_high);

	PRINTK_LE32(STA_TRAFFIC, traffic->rx_replay_cnt_low);
}

static inline void agnx_print_sta(struct agnx_priv *priv, unsigned int sta_idx)
{
	struct agnx_sta station;
	struct agnx_sta *sta = &station;
	u32 reg;
	unsigned int i;

	get_sta(priv, sta, sta_idx);

	for (i = 0; i < 4; i++)
		PRINTK_LE32(STA, sta->tx_session_keys[i]);
	for (i = 0; i < 4; i++)
		PRINTK_LE32(STA, sta->rx_session_keys[i]);

	reg = le32_to_cpu(sta->reg);
	PRINTK_BITS(STA, ID_1);
	PRINTK_BITS(STA, ID_0);
	PRINTK_BITS(STA, ENABLE_CONCATENATION);
	PRINTK_BITS(STA, ENABLE_DECOMPRESSION);
	PRINTK_BITS(STA, STA_RESERVED);
	PRINTK_BITS(STA, EAP);
	PRINTK_BITS(STA, ED_NULL);
	PRINTK_BITS(STA, ENCRYPTION_POLICY);
	PRINTK_BITS(STA, DEFINED_KEY_ID);
	PRINTK_BITS(STA, FIXED_KEY);
	PRINTK_BITS(STA, KEY_VALID);
	PRINTK_BITS(STA, STATION_VALID);

	PRINTK_LE32(STA, sta->tx_aes_blks_unicast);
	PRINTK_LE32(STA, sta->rx_aes_blks_unicast);

	PRINTK_LE16(STA, sta->aes_format_err_unicast_cnt);
	PRINTK_LE16(STA, sta->aes_replay_unicast);

	PRINTK_LE16(STA, sta->aes_decrypt_err_unicast);
	PRINTK_LE16(STA, sta->aes_decrypt_err_default);

	PRINTK_LE16(STA, sta->single_retry_packets);
	PRINTK_LE16(STA, sta->failed_tx_packets);

	PRINTK_LE16(STA, sta->muti_retry_packets);
	PRINTK_LE16(STA, sta->ack_timeouts);

	PRINTK_LE16(STA, sta->frag_tx_cnt);
	PRINTK_LE16(STA, sta->rts_brq_sent);

	PRINTK_LE16(STA, sta->tx_packets);
	PRINTK_LE16(STA, sta->cts_back_timeout);

	PRINTK_LE32(STA, sta->phy_stats_high);
	PRINTK_LE32(STA, sta->phy_stats_low);

	/* for (i = 0; i < 8; i++) */
	agnx_print_sta_traffic(sta->traffic + 0);

	PRINTK_LE16(STA, sta->traffic_class0_frag_success);
	PRINTK_LE16(STA, sta->traffic_class1_frag_success);
	PRINTK_LE16(STA, sta->traffic_class2_frag_success);
	PRINTK_LE16(STA, sta->traffic_class3_frag_success);
	PRINTK_LE16(STA, sta->traffic_class4_frag_success);
	PRINTK_LE16(STA, sta->traffic_class5_frag_success);
	PRINTK_LE16(STA, sta->traffic_class6_frag_success);
	PRINTK_LE16(STA, sta->traffic_class7_frag_success);

	PRINTK_LE16(STA, sta->num_frag_non_prime_rates);
	PRINTK_LE16(STA, sta->ack_timeout_non_prime_rates);
}


static inline void dump_ieee80211_hdr(struct ieee80211_hdr *hdr, char *tag)
{
	u16 fctl;
	int hdrlen;

	fctl = le16_to_cpu(hdr->frame_control);
	switch (fctl & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
		printk(PFX "%s DATA ", tag);
		break;
	case IEEE80211_FTYPE_CTL:
		printk(PFX "%s CTL ", tag);
		break;
	case IEEE80211_FTYPE_MGMT:
		printk(PFX "%s MGMT ", tag);
		switch (fctl & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_ASSOC_REQ:
			printk("SubType: ASSOC_REQ ");
			break;
		case IEEE80211_STYPE_ASSOC_RESP:
			printk("SubType: ASSOC_RESP ");
			break;
		case IEEE80211_STYPE_REASSOC_REQ:
			printk("SubType: REASSOC_REQ ");
			break;
		case IEEE80211_STYPE_REASSOC_RESP:
			printk("SubType: REASSOC_RESP ");
			break;
		case IEEE80211_STYPE_PROBE_REQ:
			printk("SubType: PROBE_REQ ");
			break;
		case IEEE80211_STYPE_PROBE_RESP:
			printk("SubType: PROBE_RESP ");
			break;
		case IEEE80211_STYPE_BEACON:
			printk("SubType: BEACON ");
			break;
		case IEEE80211_STYPE_ATIM:
			printk("SubType: ATIM ");
			break;
		case IEEE80211_STYPE_DISASSOC:
			printk("SubType: DISASSOC ");
			break;
		case IEEE80211_STYPE_AUTH:
			printk("SubType: AUTH ");
			break;
		case IEEE80211_STYPE_DEAUTH:
			printk("SubType: DEAUTH ");
			break;
		case IEEE80211_STYPE_ACTION:
			printk("SubType: ACTION ");
			break;
		default:
			printk("SubType: Unknow\n");
		}
		break;
	default:
		printk(PFX "%s Packet type: Unknow\n", tag);
	}

	hdrlen = ieee80211_hdrlen(fctl);

	if (hdrlen >= 4)
		printk("FC=0x%04x DUR=0x%04x",
		       fctl, le16_to_cpu(hdr->duration_id));
	if (hdrlen >= 10)
		printk(" A1=%pM", hdr->addr1);
	if (hdrlen >= 16)
		printk(" A2=%pM", hdr->addr2);
	if (hdrlen >= 24)
		printk(" A3=%pM", hdr->addr3);
	if (hdrlen >= 30)
		printk(" A4=%pM", hdr->addr4);
	printk("\n");
}

static inline void dump_txm_registers(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	int i;
	for (i = 0; i <= 0x1e8; i += 4)
		printk(KERN_DEBUG PFX "TXM: %x---> 0x%.8x\n", i, ioread32(ctl + i));
}
static inline void dump_rxm_registers(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	int i;
	for (i = 0; i <= 0x108; i += 4)
		printk(KERN_DEBUG PFX "RXM: %x---> 0x%.8x\n", i, ioread32(ctl + 0x2000 + i));
}
static inline void dump_bm_registers(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	int i;
	for (i = 0; i <= 0x90; i += 4)
		printk(KERN_DEBUG PFX "BM: %x---> 0x%.8x\n", i, ioread32(ctl + 0x2c00 + i));
}
static inline void dump_cir_registers(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	int i;
	for (i = 0; i <= 0xb8; i += 4)
		printk(KERN_DEBUG PFX "CIR: %x---> 0x%.8x\n", i, ioread32(ctl + 0x3000 + i));
}

#endif /* AGNX_DEBUG_H_ */
