#ifndef AGNX_STA_H_
#define AGNX_STA_H_

#define STA_TX_WQ_NUM	8	/* The number of TX workqueue one STA has */

struct agnx_hash_cmd {
	__be32 cmdhi;
#define MACLO		0xFFFF0000
#define MACLO_SHIFT	16
#define STA_ID		0x0000FFF0
#define STA_ID_SHIFT	4
#define CMD		0x0000000C
#define CMD_SHIFT	2
#define STATUS		0x00000002
#define STATUS_SHIFT	1
#define PASS		0x00000001
#define PASS_SHIFT	1
	__be32 cmdlo;
} __attribute__((__packed__));


/*
 * Station Power Template
 * FIXME Just for agn100 yet
 */
struct agnx_sta_power {
	__le32 reg;
#define SIGNAL			0x000000FF /* signal */
#define SIGNAL_SHIFT		0
#define RATE			0x00000F00
#define RATE_SHIFT		8
#define TIFS			0x00001000
#define TIFS_SHIFT		12
#define EDCF			0x00002000
#define EDCF_SHIFT		13
#define CHANNEL_BOND		0x00004000
#define CHANNEL_BOND_SHIFT	14
#define PHY_MODE		0x00038000
#define PHY_MODE_SHIFT		15
#define POWER_LEVEL		0x007C0000
#define POWER_LEVEL_SHIFT	18
#define NUM_TRANSMITTERS	0x00800000
#define NUM_TRANSMITTERS_SHIFT	23
} __attribute__((__packed__));

/*
 * TX Workqueue Descriptor
 */
struct agnx_sta_tx_wq {
	__le32 reg0;
#define HEAD_POINTER_LOW	0xFF000000 /* Head pointer low */
#define HEAD_POINTER_LOW_SHIFT	24
#define TAIL_POINTER		0x00FFFFFF /* Tail pointer */
#define TAIL_POINTER_SHIFT	0

	__le32 reg3;
#define ACK_POINTER_LOW	        0xFFFF0000	/* ACK pointer low */
#define ACK_POINTER_LOW_SHIFT	16
#define HEAD_POINTER_HIGH	0x0000FFFF	/* Head pointer high */
#define HEAD_POINTER_HIGH_SHIFT	0

	__le32 reg1;
/* ACK timeout tail packet count */
#define ACK_TIMOUT_TAIL_PACK_CNT	0xFFF00000
#define ACK_TIMOUT_TAIL_PACK_CNT_SHIFT	20
/* Head timeout tail packet count */
#define HEAD_TIMOUT_TAIL_PACK_CNT	0x000FFF00
#define HEAD_TIMOUT_TAIL_PACK_CNT_SHIFT	8
#define ACK_POINTER_HIGH	        0x000000FF /* ACK pointer high */
#define ACK_POINTER_HIGH_SHIFT		0

	__le32 reg2;
#define WORK_QUEUE_VALID		0x80000000 /* valid */
#define WORK_QUEUE_VALID_SHIFT		31
#define WORK_QUEUE_ACK_TYPE		0x40000000 /* ACK type */
#define WORK_QUEUE_ACK_TYPE_SHIFT	30
/* Head timeout window limit fragmentation count */
#define HEAD_TIMOUT_WIN_LIM_FRAG_CNT	0x3FFF0000
#define HEAD_TIMOUT_WIN_LIM_FRAG_CNT_SHIFT	16
/* Head timeout window limit byte count */
#define HEAD_TIMOUT_WIN_LIM_BYTE_CNT	0x0000FFFF
#define HEAD_TIMOUT_WIN_LIM_BYTE_CNT_SHIFT	 0
} __attribute__((__packed__));


/*
 * Traffic Class Structure
 */
struct agnx_sta_traffic {
	__le32 reg0;
#define ACK_TIMOUT_CNT		0xFF800000 /* ACK Timeout Counts */
#define ACK_TIMOUT_CNT_SHIFT	23
#define TRAFFIC_ACK_TYPE	0x00600000 /* ACK Type */
#define TRAFFIC_ACK_TYPE_SHIFT	21
#define NEW_PACKET		0x00100000 /* New Packet  */
#define NEW_PACKET_SHIFT	20
#define TRAFFIC_VALID		0x00080000 /* Valid */
#define TRAFFIC_VALID_SHIFT	19
#define RX_HDR_DESC_POINTER	0x0007FFFF /* RX Header Descripter pointer */
#define RX_HDR_DESC_POINTER_SHIFT	 0

	__le32 reg1;
#define RX_PACKET_TIMESTAMP	0xFFFF0000 /* RX Packet Timestamp */
#define RX_PACKET_TIMESTAMP_SHIFT	16
#define TRAFFIC_RESERVED	0x0000E000 /* Reserved */
#define TRAFFIC_RESERVED_SHIFT  13
#define SV			0x00001000 /* sv */
#define SV_SHIFT		12
#define RX_SEQUENCE_NUM		0x00000FFF /* RX Sequence Number */
#define RX_SEQUENCE_NUM_SHIFT	0

	__le32 tx_replay_cnt_low; /* TX Replay Counter Low */

	__le16 tx_replay_cnt_high; /* TX Replay Counter High */
	__le16 rx_replay_cnt_high; /* RX Replay Counter High */

	__be32 rx_replay_cnt_low; /* RX Replay Counter Low */
} __attribute__((__packed__));

/*
 * Station Descriptors
 */
struct agnx_sta {
	__le32 tx_session_keys[4]; /* Transmit Session Key (0-3) */
	__le32 rx_session_keys[4]; /* Receive Session Key (0-3) */

	__le32 reg;
#define ID_1			0xC0000000 /* id 1 */
#define ID_1_SHIFT		30
#define ID_0			0x30000000 /* id 0 */
#define ID_0_SHIFT		28
#define ENABLE_CONCATENATION	0x0FF00000 /* Enable concatenation */
#define ENABLE_CONCATENATION_SHIFT	20
#define ENABLE_DECOMPRESSION	0x000FF000 /* Enable decompression */
#define ENABLE_DECOMPRESSION_SHIFT	12
#define STA_RESERVED		0x00000C00 /* Reserved */
#define STA_RESERVED_SHIFT	10
#define EAP			0x00000200 /* EAP */
#define EAP_SHIFT		9
#define ED_NULL			0x00000100 /* ED NULL */
#define ED_NULL_SHIFT		8
#define ENCRYPTION_POLICY	0x000000E0 /* Encryption Policy */
#define ENCRYPTION_POLICY_SHIFT 5
#define DEFINED_KEY_ID		0x00000018 /* Defined Key ID */
#define DEFINED_KEY_ID_SHIFT	3
#define FIXED_KEY		0x00000004 /* Fixed Key */
#define FIXED_KEY_SHIFT		2
#define KEY_VALID		0x00000002 /* Key Valid */
#define KEY_VALID_SHIFT		1
#define STATION_VALID		0x00000001 /* Station Valid */
#define STATION_VALID_SHIFT	0

	__le32 tx_aes_blks_unicast; /* TX AES Blks Unicast */
	__le32 rx_aes_blks_unicast; /* RX AES Blks Unicast */

	__le16 aes_format_err_unicast_cnt; /* AES Format Error Unicast Counts */
	__le16 aes_replay_unicast; /* AES Replay Unicast */

	__le16 aes_decrypt_err_unicast;	/* AES Decrypt Error Unicast */
	__le16 aes_decrypt_err_default;	/* AES Decrypt Error default */

	__le16 single_retry_packets; /* Single Retry Packets */
	__le16 failed_tx_packets; /* Failed Tx Packets */

	__le16 muti_retry_packets; /* Multiple Retry Packets */
	__le16 ack_timeouts;	/* ACK Timeouts */

	__le16 frag_tx_cnt;	/* Fragment TX Counts */
	__le16 rts_brq_sent;	/* RTS Brq Sent */

	__le16 tx_packets;	/* TX Packets */
	__le16 cts_back_timeout; /* CTS Back Timeout */

	__le32 phy_stats_high;	/* PHY Stats High */
	__le32 phy_stats_low;	/* PHY Stats Low */

	struct agnx_sta_traffic traffic[8];	/* Traffic Class Structure (8) */

	__le16 traffic_class0_frag_success; /* Traffic Class 0 Fragment Success */
	__le16 traffic_class1_frag_success; /* Traffic Class 1 Fragment Success */
	__le16 traffic_class2_frag_success; /* Traffic Class 2 Fragment Success */
	__le16 traffic_class3_frag_success; /* Traffic Class 3 Fragment Success */
	__le16 traffic_class4_frag_success; /* Traffic Class 4 Fragment Success */
	__le16 traffic_class5_frag_success; /* Traffic Class 5 Fragment Success */
	__le16 traffic_class6_frag_success; /* Traffic Class 6 Fragment Success */
	__le16 traffic_class7_frag_success; /* Traffic Class 7 Fragment Success */

	__le16 num_frag_non_prime_rates; /* number of Fragments for non-prime rates */
	__le16 ack_timeout_non_prime_rates; /* ACK Timeout for non-prime rates */

} __attribute__((__packed__));


struct agnx_beacon_hdr {
	struct agnx_sta_power power; /* Tx Station Power Template  */
	u8 phy_hdr[6];		/* PHY Hdr */
	u8 frame_len_lo;	/* Frame Length Lo */
	u8 frame_len_hi;	/* Frame Length Hi */
	u8 mac_hdr[24];		/* MAC Header */
	/* FIXME */
	/* 802.11(abg) beacon */
} __attribute__((__packed__));

void hash_write(struct agnx_priv *priv, const u8 *mac_addr, u8 sta_id);
void hash_dump(struct agnx_priv *priv, u8 sta_id);
void hash_read(struct agnx_priv *priv, u32 reghi, u32 reglo, u8 sta_id);
void hash_delete(struct agnx_priv *priv, u32 reghi, u32 reglo, u8 sta_id);

void get_sta_power(struct agnx_priv *priv, struct agnx_sta_power *power, unsigned int sta_idx);
void set_sta_power(struct agnx_priv *priv, struct agnx_sta_power *power,
		   unsigned int sta_idx);
void get_sta_tx_wq(struct agnx_priv *priv, struct agnx_sta_tx_wq *tx_wq,
		   unsigned int sta_idx, unsigned int wq_idx);
void set_sta_tx_wq(struct agnx_priv *priv, struct agnx_sta_tx_wq *tx_wq,
		   unsigned int sta_idx, unsigned int wq_idx);
void get_sta(struct agnx_priv *priv, struct agnx_sta *sta, unsigned int sta_idx);
void set_sta(struct agnx_priv *priv, struct agnx_sta *sta, unsigned int sta_idx);

void sta_power_init(struct agnx_priv *priv, unsigned int num);
void sta_init(struct agnx_priv *priv, unsigned int num);

#endif /* AGNX_STA_H_ */
