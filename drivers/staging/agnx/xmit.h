#ifndef AGNX_XMIT_H_
#define AGNX_XMIT_H_

#include <net/mac80211.h>

struct agnx_priv;

static inline u32 agnx_set_bits(u32 mask, u8 shift, u32 value)
{
	return (value << shift) & mask;
}

static inline u32 agnx_get_bits(u32 mask, u8 shift, u32 value)
{
	return (value & mask) >> shift;
}


struct agnx_rx {
	__be16 rx_packet_duration; /*  RX Packet Duration */
	__be16 replay_cnt;	/* Replay Count */
} __attribute__((__packed__));


struct agnx_tx {
	u8 long_retry_limit; /* Long Retry Limit */
	u8 short_retry_limit; /* Short Retry Limit */
	u8 long_retry_cnt;	/* Long Retry Count */
	u8 short_retry_cnt; /* Short Retry Count */
} __attribute__((__packed__));


/* Copy from bcm43xx */
#define P4D_BYT3S(magic, nr_bytes)      u8 __p4dding##magic[nr_bytes]
#define P4D_BYTES(line, nr_bytes)       P4D_BYT3S(line, nr_bytes)
#define PAD_BYTES(nr_bytes)             P4D_BYTES(__LINE__, nr_bytes)

#define P4D_BIT3S(magic, nr_bits)       __be32 __padding##magic:nr_bits
#define P4D_BITS(line, nr_bits)         P4D_BIT3S(line, nr_bits)
#define PAD_BITS(nr_bits)	        P4D_BITS(__LINE__, nr_bits)


struct agnx_hdr {
	__be32 reg0;
#define RTS		        0x80000000 /* RTS */
#define RTS_SHIFT		31
#define MULTICAST	        0x40000000 /* multicast */
#define MULTICAST_SHIFT		30
#define ACK			0x30000000 /* ACK */
#define ACK_SHIFT		28
#define TM			0x08000000 /* TM */
#define TM_SHIFT		27
#define RELAY			0x04000000 /* Relay */
#define RELAY_SHIFT		26
/* 	PAD_BITS(4); */
#define REVISED_FCS		0x00380000 /* revised FCS */
#define REVISED_FCS_SHIFT	19
#define NEXT_BUFFER_ADDR	0x0007FFFF /* Next Buffer Address */
#define NEXT_BUFFER_ADDR_SHIFT	0

	__be32 reg1;
#define MAC_HDR_LEN		0xFC000000 /* MAC Header Length  */
#define MAC_HDR_LEN_SHIFT	26
#define DURATION_OVERIDE	0x02000000 /* Duration Override */
#define DURATION_OVERIDE_SHIFT	25
#define PHY_HDR_OVERIDE		0x01000000 /* PHY Header Override */
#define PHY_HDR_OVERIDE_SHIFT	24
#define CRC_FAIL		0x00800000 /* CRC fail */
#define CRC_FAIL_SHIFT		23
/*	PAD_BITS(1); */
#define SEQUENCE_NUMBER		0x00200000 /* Sequence Number */
#define SEQUENCE_NUMBER_SHIFT	21
/*	PAD_BITS(2); */
#define BUFF_HEAD_ADDR		0x0007FFFF /* Buffer Head Address */
#define BUFF_HEAD_ADDR_SHIFT	0

	__be32 reg2;
#define PDU_COUNT		0xFC000000 /* PDU Count */
#define PDU_COUNT_SHIFT		26
/* 	PAD_BITS(3); */
#define WEP_KEY			0x00600000 /* WEP Key # */
#define WEP_KEY_SHIFT		21
#define USES_WEP_KEY		0x00100000 /* Uses WEP Key */
#define USES_WEP_KEY_SHIFT	20
#define KEEP_ALIVE		0x00080000 /* Keep alive */
#define KEEP_ALIVE_SHIFT	19
#define BUFF_TAIL_ADDR		0x0007FFFF /* Buffer Tail Address */
#define BUFF_TAIL_ADDR_SHIFT	0

	__be32 reg3;
#define CTS_11G			0x80000000	/* CTS in 11g */
#define CTS_11G_SHIFT		31
#define RTS_11G			0x40000000	/* RTS in 11g */
#define RTS_11G_SHIFT		30
/* PAD_BITS(2); */
#define FRAG_SIZE		0x0FFF0000	/* fragment size */
#define FRAG_SIZE_SHIFT		16
#define PAYLOAD_LEN		0x0000FFF0	/* payload length */
#define PAYLOAD_LEN_SHIFT	4
#define FRAG_NUM		0x0000000F	/* number of frags */
#define FRAG_NUM_SHIFT		0

	__be32 reg4;
/* 	PAD_BITS(4); */
#define RELAY_STAID		0x0FFF0000 /* relayStald */
#define RELAY_STAID_SHIFT	16
#define STATION_ID		0x0000FFF0 /* Station ID */
#define STATION_ID_SHIFT	4
#define WORKQUEUE_ID		0x0000000F /* Workqueue ID */
#define WORKQUEUE_ID_SHIFT	0

	/* FIXME this register maybe is LE? */
	__be32 reg5;
/* 	PAD_BITS(4); */
#define ROUTE_HOST		0x0F000000
#define ROUTE_HOST_SHIFT	24
#define ROUTE_CARD_CPU		0x00F00000
#define ROUTE_CARD_CPU_SHIFT	20
#define ROUTE_ENCRYPTION	0x000F0000
#define ROUTE_ENCRYPTION_SHIFT	16
#define ROUTE_TX		0x0000F000
#define ROUTE_TX_SHIFT		12
#define ROUTE_RX1		0x00000F00
#define ROUTE_RX1_SHIFT		8
#define ROUTE_RX2		0x000000F0
#define ROUTE_RX2_SHIFT		4
#define ROUTE_COMPRESSION	0x0000000F
#define ROUTE_COMPRESSION_SHIFT 0

	__be32 _11g0;			/* 11g */
	__be32 _11g1;			/* 11g */
	__be32 _11b0;			/* 11b */
	__be32 _11b1;			/* 11b */
	u8 mac_hdr[32];			/* MAC header */

	__be16 rts_duration;		/* RTS duration */
	__be16 last_duration;		/* Last duration */
	__be16 sec_last_duration;	/* Second to Last duration */
	__be16 other_duration;		/* Other duration */
	__be16 tx_last_duration;	/* TX Last duration */
	__be16 tx_other_duration;	/* TX Other Duration */
	__be16 last_11g_len;		/* Length of last 11g */
	__be16 other_11g_len;		/* Lenght of other 11g */

	__be16 last_11b_len;		/* Length of last 11b */
	__be16 other_11b_len;		/* Lenght of other 11b */


	__be16 reg6;
#define MBF			0xF000 /* mbf */
#define MBF_SHIFT		12
#define RSVD4			0x0FFF /* rsvd4 */
#define RSVD4_SHIFT		0

	__be16 rx_frag_stat;	/* RX fragmentation status */

	__be32 time_stamp;	/* TimeStamp */
	__be32 phy_stats_hi;	/* PHY stats hi */
	__be32 phy_stats_lo;	/* PHY stats lo */
	__be32 mic_key0;	/* MIC key 0 */
	__be32 mic_key1;	/* MIC key 1 */

	union {			/* RX/TX Union */
		struct agnx_rx rx;
		struct agnx_tx tx;
	};

	u8 rx_channel;		/* Recieve Channel */
	PAD_BYTES(3);

	u8 reserved[4];
} __attribute__((__packed__));


struct agnx_desc {
#define PACKET_LEN		0xFFF00000
#define PACKET_LEN_SHIFT	20
/* ------------------------------------------------ */
#define FIRST_PACKET_MASK	0x00080000
#define FIRST_PACKET_MASK_SHIFT	19
#define FIRST_RESERV2		0x00040000
#define FIRST_RESERV2_SHIFT	18
#define FIRST_TKIP_ERROR	0x00020000
#define FIRST_TKIP_ERROR_SHIFT	17
#define FIRST_TKIP_PACKET	0x00010000
#define FIRST_TKIP_PACKET_SHIFT	16
#define FIRST_RESERV1		0x0000F000
#define FIRST_RESERV1_SHIFT	12
#define FIRST_FRAG_LEN		0x00000FF8
#define FIRST_FRAG_LEN_SHIFT	3
/* ------------------------------------------------ */
#define SUB_RESERV2		0x000c0000
#define SUB_RESERV2_SHIFT	18
#define SUB_TKIP_ERROR		0x00020000
#define SUB_TKIP_ERROR_SHIFT	17
#define SUB_TKIP_PACKET		0x00010000
#define SUB_TKIP_PACKET_SHIFT	16
#define SUB_RESERV1		0x00008000
#define SUB_RESERV1_SHIFT	15
#define SUB_FRAG_LEN		0x00007FF8
#define SUB_FRAG_LEN_SHIFT	3
/* ------------------------------------------------ */
#define FIRST_FRAG		0x00000004
#define FIRST_FRAG_SHIFT	2
#define LAST_FRAG		0x00000002
#define LAST_FRAG_SHIFT		1
#define OWNER			0x00000001
#define OWNER_SHIFT		0
	__be32 frag;
	__be32 dma_addr;
} __attribute__((__packed__));

enum {HEADER, PACKET};

struct agnx_info {
        struct sk_buff *skb;
        dma_addr_t mapping;
	u32 dma_len;		/* dma buffer len  */
	/* Below fields only usful for tx */
	u32 hdr_len;		/* ieee80211 header length */
	unsigned int type;
        struct ieee80211_tx_info *txi;
        struct ieee80211_hdr hdr;
};


struct agnx_ring {
	struct agnx_desc *desc;
	dma_addr_t dma;
	struct agnx_info *info;
	/* Will lead to overflow when sent packet number enough? */
	unsigned int idx;
	unsigned int idx_sent;		/* only usful for txd and txm */
	unsigned int size;
};

#define AGNX_RX_RING_SIZE	128
#define AGNX_TXD_RING_SIZE	256
#define AGNX_TXM_RING_SIZE	128

void disable_rx_interrupt(struct agnx_priv *priv);
void enable_rx_interrupt(struct agnx_priv *priv);
int fill_rings(struct agnx_priv *priv);
void unfill_rings(struct agnx_priv *priv);
void handle_rx_irq(struct agnx_priv *priv);
void handle_txd_irq(struct agnx_priv *priv);
void handle_txm_irq(struct agnx_priv *priv);
void handle_other_irq(struct agnx_priv *priv);
int _agnx_tx(struct agnx_priv *priv, struct sk_buff *skb);
#endif /* AGNX_XMIT_H_ */
