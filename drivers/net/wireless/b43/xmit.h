#ifndef B43_XMIT_H_
#define B43_XMIT_H_

#include "main.h"
#include <net/mac80211.h>


#define _b43_declare_plcp_hdr(size) \
	struct b43_plcp_hdr##size {		\
		union {				\
			__le32 data;		\
			__u8 raw[size];		\
		} __attribute__((__packed__));	\
	} __attribute__((__packed__))

/* struct b43_plcp_hdr4 */
_b43_declare_plcp_hdr(4);
/* struct b43_plcp_hdr6 */
_b43_declare_plcp_hdr(6);

#undef _b43_declare_plcp_hdr

/* TX header for v4 firmware */
struct b43_txhdr {
	__le32 mac_ctl;			/* MAC TX control */
	__le16 mac_frame_ctl;		/* Copy of the FrameControl field */
	__le16 tx_fes_time_norm;	/* TX FES Time Normal */
	__le16 phy_ctl;			/* PHY TX control */
	__le16 phy_ctl1;		/* PHY TX control word 1 */
	__le16 phy_ctl1_fb;		/* PHY TX control word 1 for fallback rates */
	__le16 phy_ctl1_rts;		/* PHY TX control word 1 RTS */
	__le16 phy_ctl1_rts_fb;		/* PHY TX control word 1 RTS for fallback rates */
	__u8 phy_rate;			/* PHY rate */
	__u8 phy_rate_rts;		/* PHY rate for RTS/CTS */
	__u8 extra_ft;			/* Extra Frame Types */
	__u8 chan_radio_code;		/* Channel Radio Code */
	__u8 iv[16];			/* Encryption IV */
	__u8 tx_receiver[6];		/* TX Frame Receiver address */
	__le16 tx_fes_time_fb;		/* TX FES Time Fallback */
	struct b43_plcp_hdr6 rts_plcp_fb; /* RTS fallback PLCP header */
	__le16 rts_dur_fb;		/* RTS fallback duration */
	struct b43_plcp_hdr6 plcp_fb;	/* Fallback PLCP header */
	__le16 dur_fb;			/* Fallback duration */
	__le16 mimo_modelen;		/* MIMO mode length */
	__le16 mimo_ratelen_fb;		/* MIMO fallback rate length */
	__le32 timeout;			/* Timeout */

	union {
		/* The new r410 format. */
		struct {
			__le16 mimo_antenna;		/* MIMO antenna select */
			__le16 preload_size;		/* Preload size */
			PAD_BYTES(2);
			__le16 cookie;			/* TX frame cookie */
			__le16 tx_status;		/* TX status */
			struct b43_plcp_hdr6 rts_plcp;	/* RTS PLCP header */
			__u8 rts_frame[16];		/* The RTS frame (if used) */
			PAD_BYTES(2);
			struct b43_plcp_hdr6 plcp;	/* Main PLCP header */
		} new_format __attribute__ ((__packed__));

		/* The old r351 format. */
		struct {
			PAD_BYTES(2);
			__le16 cookie;			/* TX frame cookie */
			__le16 tx_status;		/* TX status */
			struct b43_plcp_hdr6 rts_plcp;	/* RTS PLCP header */
			__u8 rts_frame[16];		/* The RTS frame (if used) */
			PAD_BYTES(2);
			struct b43_plcp_hdr6 plcp;	/* Main PLCP header */
		} old_format __attribute__ ((__packed__));

	} __attribute__ ((__packed__));
} __attribute__ ((__packed__));

/* MAC TX control */
#define B43_TXH_MAC_USEFBR		0x10000000 /* Use fallback rate for this AMPDU */
#define B43_TXH_MAC_KEYIDX		0x0FF00000 /* Security key index */
#define B43_TXH_MAC_KEYIDX_SHIFT	20
#define B43_TXH_MAC_KEYALG		0x00070000 /* Security key algorithm */
#define B43_TXH_MAC_KEYALG_SHIFT	16
#define B43_TXH_MAC_AMIC		0x00008000 /* AMIC */
#define B43_TXH_MAC_RIFS		0x00004000 /* Use RIFS */
#define B43_TXH_MAC_LIFETIME		0x00002000 /* Lifetime */
#define B43_TXH_MAC_FRAMEBURST		0x00001000 /* Frameburst */
#define B43_TXH_MAC_SENDCTS		0x00000800 /* Send CTS-to-self */
#define B43_TXH_MAC_AMPDU		0x00000600 /* AMPDU status */
#define  B43_TXH_MAC_AMPDU_MPDU		0x00000000 /* Regular MPDU, not an AMPDU */
#define  B43_TXH_MAC_AMPDU_FIRST	0x00000200 /* First MPDU or AMPDU */
#define  B43_TXH_MAC_AMPDU_INTER	0x00000400 /* Intermediate MPDU or AMPDU */
#define  B43_TXH_MAC_AMPDU_LAST		0x00000600 /* Last (or only) MPDU of AMPDU */
#define B43_TXH_MAC_40MHZ		0x00000100 /* Use 40 MHz bandwidth */
#define B43_TXH_MAC_5GHZ		0x00000080 /* 5GHz band */
#define B43_TXH_MAC_DFCS		0x00000040 /* DFCS */
#define B43_TXH_MAC_IGNPMQ		0x00000020 /* Ignore PMQ */
#define B43_TXH_MAC_HWSEQ		0x00000010 /* Use Hardware Sequence Number */
#define B43_TXH_MAC_STMSDU		0x00000008 /* Start MSDU */
#define B43_TXH_MAC_SENDRTS		0x00000004 /* Send RTS */
#define B43_TXH_MAC_LONGFRAME		0x00000002 /* Long frame */
#define B43_TXH_MAC_ACK			0x00000001 /* Immediate ACK */

/* Extra Frame Types */
#define B43_TXH_EFT_FB			0x03 /* Data frame fallback encoding */
#define  B43_TXH_EFT_FB_CCK		0x00 /* CCK */
#define  B43_TXH_EFT_FB_OFDM		0x01 /* OFDM */
#define  B43_TXH_EFT_FB_EWC		0x02 /* EWC */
#define  B43_TXH_EFT_FB_N		0x03 /* N */
#define B43_TXH_EFT_RTS			0x0C /* RTS/CTS encoding */
#define  B43_TXH_EFT_RTS_CCK		0x00 /* CCK */
#define  B43_TXH_EFT_RTS_OFDM		0x04 /* OFDM */
#define  B43_TXH_EFT_RTS_EWC		0x08 /* EWC */
#define  B43_TXH_EFT_RTS_N		0x0C /* N */
#define B43_TXH_EFT_RTSFB		0x30 /* RTS/CTS fallback encoding */
#define  B43_TXH_EFT_RTSFB_CCK		0x00 /* CCK */
#define  B43_TXH_EFT_RTSFB_OFDM		0x10 /* OFDM */
#define  B43_TXH_EFT_RTSFB_EWC		0x20 /* EWC */
#define  B43_TXH_EFT_RTSFB_N		0x30 /* N */

/* PHY TX control word */
#define B43_TXH_PHY_ENC			0x0003 /* Data frame encoding */
#define  B43_TXH_PHY_ENC_CCK		0x0000 /* CCK */
#define  B43_TXH_PHY_ENC_OFDM		0x0001 /* OFDM */
#define  B43_TXH_PHY_ENC_EWC		0x0002 /* EWC */
#define  B43_TXH_PHY_ENC_N		0x0003 /* N */
#define B43_TXH_PHY_SHORTPRMBL		0x0010 /* Use short preamble */
#define B43_TXH_PHY_ANT			0x03C0 /* Antenna selection */
#define  B43_TXH_PHY_ANT0		0x0000 /* Use antenna 0 */
#define  B43_TXH_PHY_ANT1		0x0040 /* Use antenna 1 */
#define  B43_TXH_PHY_ANT01AUTO		0x00C0 /* Use antenna 0/1 auto */
#define  B43_TXH_PHY_ANT2		0x0100 /* Use antenna 2 */
#define  B43_TXH_PHY_ANT3		0x0200 /* Use antenna 3 */
#define B43_TXH_PHY_TXPWR		0xFC00 /* TX power */
#define B43_TXH_PHY_TXPWR_SHIFT		10

/* PHY TX control word 1 */
#define B43_TXH_PHY1_BW			0x0007 /* Bandwidth */
#define  B43_TXH_PHY1_BW_10		0x0000 /* 10 MHz */
#define  B43_TXH_PHY1_BW_10U		0x0001 /* 10 MHz upper */
#define  B43_TXH_PHY1_BW_20		0x0002 /* 20 MHz */
#define  B43_TXH_PHY1_BW_20U		0x0003 /* 20 MHz upper */
#define  B43_TXH_PHY1_BW_40		0x0004 /* 40 MHz */
#define  B43_TXH_PHY1_BW_40DUP		0x0005 /* 50 MHz duplicate */
#define B43_TXH_PHY1_MODE		0x0038 /* Mode */
#define  B43_TXH_PHY1_MODE_SISO		0x0000 /* SISO */
#define  B43_TXH_PHY1_MODE_CDD		0x0008 /* CDD */
#define  B43_TXH_PHY1_MODE_STBC		0x0010 /* STBC */
#define  B43_TXH_PHY1_MODE_SDM		0x0018 /* SDM */
#define B43_TXH_PHY1_CRATE		0x0700 /* Coding rate */
#define  B43_TXH_PHY1_CRATE_1_2		0x0000 /* 1/2 */
#define  B43_TXH_PHY1_CRATE_2_3		0x0100 /* 2/3 */
#define  B43_TXH_PHY1_CRATE_3_4		0x0200 /* 3/4 */
#define  B43_TXH_PHY1_CRATE_4_5		0x0300 /* 4/5 */
#define  B43_TXH_PHY1_CRATE_5_6		0x0400 /* 5/6 */
#define  B43_TXH_PHY1_CRATE_7_8		0x0600 /* 7/8 */
#define B43_TXH_PHY1_MODUL		0x3800 /* Modulation scheme */
#define  B43_TXH_PHY1_MODUL_BPSK	0x0000 /* BPSK */
#define  B43_TXH_PHY1_MODUL_QPSK	0x0800 /* QPSK */
#define  B43_TXH_PHY1_MODUL_QAM16	0x1000 /* QAM16 */
#define  B43_TXH_PHY1_MODUL_QAM64	0x1800 /* QAM64 */
#define  B43_TXH_PHY1_MODUL_QAM256	0x2000 /* QAM256 */


/* r351 firmware compatibility stuff. */
static inline
bool b43_is_old_txhdr_format(struct b43_wldev *dev)
{
	return (dev->fw.rev <= 351);
}

static inline
size_t b43_txhdr_size(struct b43_wldev *dev)
{
	if (b43_is_old_txhdr_format(dev))
		return 100 + sizeof(struct b43_plcp_hdr6);
	return 104 + sizeof(struct b43_plcp_hdr6);
}


int b43_generate_txhdr(struct b43_wldev *dev,
		       u8 * txhdr,
		       struct sk_buff *skb_frag,
		       struct ieee80211_tx_info *txctl, u16 cookie);

/* Transmit Status */
struct b43_txstatus {
	u16 cookie;		/* The cookie from the txhdr */
	u16 seq;		/* Sequence number */
	u8 phy_stat;		/* PHY TX status */
	u8 frame_count;		/* Frame transmit count */
	u8 rts_count;		/* RTS transmit count */
	u8 supp_reason;		/* Suppression reason */
	/* flags */
	u8 pm_indicated;	/* PM mode indicated to AP */
	u8 intermediate;	/* Intermediate status notification (not final) */
	u8 for_ampdu;		/* Status is for an AMPDU (afterburner) */
	u8 acked;		/* Wireless ACK received */
};

/* txstatus supp_reason values */
enum {
	B43_TXST_SUPP_NONE,	/* Not suppressed */
	B43_TXST_SUPP_PMQ,	/* Suppressed due to PMQ entry */
	B43_TXST_SUPP_FLUSH,	/* Suppressed due to flush request */
	B43_TXST_SUPP_PREV,	/* Previous fragment failed */
	B43_TXST_SUPP_CHAN,	/* Channel mismatch */
	B43_TXST_SUPP_LIFE,	/* Lifetime expired */
	B43_TXST_SUPP_UNDER,	/* Buffer underflow */
	B43_TXST_SUPP_ABNACK,	/* Afterburner NACK */
};

/* Receive header for v4 firmware. */
struct b43_rxhdr_fw4 {
	__le16 frame_len;	/* Frame length */
	 PAD_BYTES(2);
	__le16 phy_status0;	/* PHY RX Status 0 */
	union {
		/* RSSI for A/B/G-PHYs */
		struct {
			__u8 jssi;	/* PHY RX Status 1: JSSI */
			__u8 sig_qual;	/* PHY RX Status 1: Signal Quality */
		} __attribute__ ((__packed__));

		/* RSSI for N-PHYs */
		struct {
			__s8 power0;	/* PHY RX Status 1: Power 0 */
			__s8 power1;	/* PHY RX Status 1: Power 1 */
		} __attribute__ ((__packed__));
	} __attribute__ ((__packed__));
	__le16 phy_status2;	/* PHY RX Status 2 */
	__le16 phy_status3;	/* PHY RX Status 3 */
	__le32 mac_status;	/* MAC RX status */
	__le16 mac_time;
	__le16 channel;
} __attribute__ ((__packed__));

/* PHY RX Status 0 */
#define B43_RX_PHYST0_GAINCTL		0x4000 /* Gain Control */
#define B43_RX_PHYST0_PLCPHCF		0x0200
#define B43_RX_PHYST0_PLCPFV		0x0100
#define B43_RX_PHYST0_SHORTPRMBL	0x0080 /* Received with Short Preamble */
#define B43_RX_PHYST0_LCRS		0x0040
#define B43_RX_PHYST0_ANT		0x0020 /* Antenna */
#define B43_RX_PHYST0_UNSRATE		0x0010
#define B43_RX_PHYST0_CLIP		0x000C
#define B43_RX_PHYST0_CLIP_SHIFT	2
#define B43_RX_PHYST0_FTYPE		0x0003 /* Frame type */
#define  B43_RX_PHYST0_CCK		0x0000 /* Frame type: CCK */
#define  B43_RX_PHYST0_OFDM		0x0001 /* Frame type: OFDM */
#define  B43_RX_PHYST0_PRE_N		0x0002 /* Pre-standard N-PHY frame */
#define  B43_RX_PHYST0_STD_N		0x0003 /* Standard N-PHY frame */

/* PHY RX Status 2 */
#define B43_RX_PHYST2_LNAG		0xC000 /* LNA Gain */
#define B43_RX_PHYST2_LNAG_SHIFT	14
#define B43_RX_PHYST2_PNAG		0x3C00 /* PNA Gain */
#define B43_RX_PHYST2_PNAG_SHIFT	10
#define B43_RX_PHYST2_FOFF		0x03FF /* F offset */

/* PHY RX Status 3 */
#define B43_RX_PHYST3_DIGG		0x1800 /* DIG Gain */
#define B43_RX_PHYST3_DIGG_SHIFT	11
#define B43_RX_PHYST3_TRSTATE		0x0400 /* TR state */

/* MAC RX Status */
#define B43_RX_MAC_RXST_VALID		0x01000000 /* PHY RXST valid */
#define B43_RX_MAC_TKIP_MICERR		0x00100000 /* TKIP MIC error */
#define B43_RX_MAC_TKIP_MICATT		0x00080000 /* TKIP MIC attempted */
#define B43_RX_MAC_AGGTYPE		0x00060000 /* Aggregation type */
#define B43_RX_MAC_AGGTYPE_SHIFT	17
#define B43_RX_MAC_AMSDU		0x00010000 /* A-MSDU mask */
#define B43_RX_MAC_BEACONSENT		0x00008000 /* Beacon sent flag */
#define B43_RX_MAC_KEYIDX		0x000007E0 /* Key index */
#define B43_RX_MAC_KEYIDX_SHIFT		5
#define B43_RX_MAC_DECERR		0x00000010 /* Decrypt error */
#define B43_RX_MAC_DEC			0x00000008 /* Decryption attempted */
#define B43_RX_MAC_PADDING		0x00000004 /* Pad bytes present */
#define B43_RX_MAC_RESP			0x00000002 /* Response frame transmitted */
#define B43_RX_MAC_FCSERR		0x00000001 /* FCS error */

/* RX channel */
#define B43_RX_CHAN_40MHZ		0x1000 /* 40 Mhz channel width */
#define B43_RX_CHAN_5GHZ		0x0800 /* 5 Ghz band */
#define B43_RX_CHAN_ID			0x07F8 /* Channel ID */
#define B43_RX_CHAN_ID_SHIFT		3
#define B43_RX_CHAN_PHYTYPE		0x0007 /* PHY type */


u8 b43_plcp_get_ratecode_cck(const u8 bitrate);
u8 b43_plcp_get_ratecode_ofdm(const u8 bitrate);

void b43_generate_plcp_hdr(struct b43_plcp_hdr4 *plcp,
			   const u16 octets, const u8 bitrate);

void b43_rx(struct b43_wldev *dev, struct sk_buff *skb, const void *_rxhdr);

void b43_handle_txstatus(struct b43_wldev *dev,
			 const struct b43_txstatus *status);
bool b43_fill_txstatus_report(struct b43_wldev *dev,
			      struct ieee80211_tx_info *report,
			      const struct b43_txstatus *status);

void b43_tx_suspend(struct b43_wldev *dev);
void b43_tx_resume(struct b43_wldev *dev);


/* Helper functions for converting the key-table index from "firmware-format"
 * to "raw-format" and back. The firmware API changed for this at some revision.
 * We need to account for that here. */
static inline int b43_new_kidx_api(struct b43_wldev *dev)
{
	/* FIXME: Not sure the change was at rev 351 */
	return (dev->fw.rev >= 351);
}
static inline u8 b43_kidx_to_fw(struct b43_wldev *dev, u8 raw_kidx)
{
	u8 firmware_kidx;
	if (b43_new_kidx_api(dev)) {
		firmware_kidx = raw_kidx;
	} else {
		if (raw_kidx >= 4)	/* Is per STA key? */
			firmware_kidx = raw_kidx - 4;
		else
			firmware_kidx = raw_kidx;	/* TX default key */
	}
	return firmware_kidx;
}
static inline u8 b43_kidx_to_raw(struct b43_wldev *dev, u8 firmware_kidx)
{
	u8 raw_kidx;
	if (b43_new_kidx_api(dev))
		raw_kidx = firmware_kidx;
	else
		raw_kidx = firmware_kidx + 4;	/* RX default keys or per STA keys */
	return raw_kidx;
}

/* struct b43_private_tx_info - TX info private to b43.
 * The structure is placed in (struct ieee80211_tx_info *)->rate_driver_data
 *
 * @bouncebuffer: DMA Bouncebuffer (if used)
 */
struct b43_private_tx_info {
	void *bouncebuffer;
};

static inline struct b43_private_tx_info *
b43_get_priv_tx_info(struct ieee80211_tx_info *info)
{
	BUILD_BUG_ON(sizeof(struct b43_private_tx_info) >
		     sizeof(info->rate_driver_data));
	return (struct b43_private_tx_info *)info->rate_driver_data;
}

#endif /* B43_XMIT_H_ */
