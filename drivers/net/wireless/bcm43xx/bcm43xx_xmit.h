#ifndef BCM43xx_XMIT_H_
#define BCM43xx_XMIT_H_

#include "bcm43xx_main.h"


#define _bcm43xx_declare_plcp_hdr(size) \
	struct bcm43xx_plcp_hdr##size {		\
		union {				\
			__le32 data;		\
			__u8 raw[size];		\
		} __attribute__((__packed__));	\
	} __attribute__((__packed__))

/* struct bcm43xx_plcp_hdr4 */
_bcm43xx_declare_plcp_hdr(4);
/* struct bcm43xx_plcp_hdr6 */
_bcm43xx_declare_plcp_hdr(6);

#undef _bcm43xx_declare_plcp_hdr

/* Device specific TX header. To be prepended to TX frames. */
struct bcm43xx_txhdr {
	union {
		struct {
			__le16 flags;
			__le16 wsec_rate;
			__le16 frame_control;
			u16 unknown_zeroed_0;
			__le16 control;
			u8 wep_iv[10];
			u8 unknown_wsec_tkip_data[3]; //FIXME
			PAD_BYTES(3);
			u8 mac1[6];
			u16 unknown_zeroed_1;
			struct bcm43xx_plcp_hdr4 rts_cts_fallback_plcp;
			__le16 rts_cts_dur_fallback;
			struct bcm43xx_plcp_hdr4 fallback_plcp;
			__le16 fallback_dur_id;
			PAD_BYTES(2);
			__le16 cookie;
			__le16 unknown_scb_stuff; //FIXME
			struct bcm43xx_plcp_hdr6 rts_cts_plcp;
			__le16 rts_cts_frame_control;
			__le16 rts_cts_dur;
			u8 rts_cts_mac1[6];
			u8 rts_cts_mac2[6];
			PAD_BYTES(2);
			struct bcm43xx_plcp_hdr6 plcp;
		} __attribute__((__packed__));
		u8 raw[82];
	} __attribute__((__packed__));
} __attribute__((__packed__));

/* Values/Masks for the device TX header */
#define BCM43xx_TXHDRFLAG_EXPECTACK		0x0001
#define BCM43xx_TXHDRFLAG_RTSCTS		0x0002
#define BCM43xx_TXHDRFLAG_RTS			0x0004
#define BCM43xx_TXHDRFLAG_FIRSTFRAGMENT		0x0008
#define BCM43xx_TXHDRFLAG_DESTPSMODE		0x0020
#define BCM43xx_TXHDRFLAG_RTSCTS_OFDM		0x0080
#define BCM43xx_TXHDRFLAG_FALLBACKOFDM		0x0100
#define BCM43xx_TXHDRFLAG_RTSCTSFALLBACK_OFDM	0x0200
#define BCM43xx_TXHDRFLAG_CTS			0x0400
#define BCM43xx_TXHDRFLAG_FRAMEBURST		0x0800

#define BCM43xx_TXHDRCTL_OFDM			0x0001
#define BCM43xx_TXHDRCTL_SHORT_PREAMBLE		0x0010
#define BCM43xx_TXHDRCTL_ANTENNADIV_MASK	0x0030
#define BCM43xx_TXHDRCTL_ANTENNADIV_SHIFT	8

#define BCM43xx_TXHDR_RATE_MASK			0x0F00
#define BCM43xx_TXHDR_RATE_SHIFT		8
#define BCM43xx_TXHDR_RTSRATE_MASK		0xF000
#define BCM43xx_TXHDR_RTSRATE_SHIFT		12
#define BCM43xx_TXHDR_WSEC_KEYINDEX_MASK	0x00F0
#define BCM43xx_TXHDR_WSEC_KEYINDEX_SHIFT	4
#define BCM43xx_TXHDR_WSEC_ALGO_MASK		0x0003
#define BCM43xx_TXHDR_WSEC_ALGO_SHIFT		0

void bcm43xx_generate_txhdr(struct bcm43xx_private *bcm,
			    struct bcm43xx_txhdr *txhdr,
			    const unsigned char *fragment_data,
			    const unsigned int fragment_len,
			    const int is_first_fragment,
			    const u16 cookie);

/* RX header as received from the hardware. */
struct bcm43xx_rxhdr {
	/* Frame Length. Must be generated explicitely in PIO mode. */
	__le16 frame_length;
	PAD_BYTES(2);
	/* Flags field 1 */
	__le16 flags1;
	u8 rssi;
	u8 signal_quality;
	PAD_BYTES(2);
	/* Flags field 3 */
	__le16 flags3;
	/* Flags field 2 */
	__le16 flags2;
	/* Lower 16bits of the TSF at the time the frame started. */
	__le16 mactime;
	PAD_BYTES(14);
} __attribute__((__packed__));

#define BCM43xx_RXHDR_FLAGS1_OFDM		(1 << 0)
/*#define BCM43xx_RXHDR_FLAGS1_SIGNAL???	(1 << 3) FIXME */
#define BCM43xx_RXHDR_FLAGS1_SHORTPREAMBLE	(1 << 7)
#define BCM43xx_RXHDR_FLAGS1_2053RSSIADJ	(1 << 14)

#define BCM43xx_RXHDR_FLAGS2_INVALIDFRAME	(1 << 0)
#define BCM43xx_RXHDR_FLAGS2_TYPE2FRAME		(1 << 2)
/*FIXME: WEP related flags */

#define BCM43xx_RXHDR_FLAGS3_2050RSSIADJ	(1 << 10)

/* Transmit Status as received from the hardware. */
struct bcm43xx_hwxmitstatus {
	PAD_BYTES(4);
	__le16 cookie;
	u8 flags;
	u8 cnt1:4,
	   cnt2:4;
	PAD_BYTES(2);
	__le16 seq;
	__le16 unknown; //FIXME
} __attribute__((__packed__));

/* Transmit Status in CPU byteorder. */
struct bcm43xx_xmitstatus {
	u16 cookie;
	u8 flags;
	u8 cnt1:4,
	   cnt2:4;
	u16 seq;
	u16 unknown; //FIXME
};

#define BCM43xx_TXSTAT_FLAG_ACK		0x01
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x02
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x04
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x08
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x10
#define BCM43xx_TXSTAT_FLAG_IGNORE	0x20
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x40
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x80

u8 bcm43xx_plcp_get_ratecode_cck(const u8 bitrate);
u8 bcm43xx_plcp_get_ratecode_ofdm(const u8 bitrate);

int bcm43xx_rx(struct bcm43xx_private *bcm,
	       struct sk_buff *skb,
	       struct bcm43xx_rxhdr *rxhdr);

#endif /* BCM43xx_XMIT_H_ */
