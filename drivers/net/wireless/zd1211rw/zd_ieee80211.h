#ifndef _ZD_IEEE80211_H
#define _ZD_IEEE80211_H

#include <net/ieee80211.h>

/* Additional definitions from the standards.
 */

#define ZD_REGDOMAIN_FCC	0x10
#define ZD_REGDOMAIN_IC		0x20
#define ZD_REGDOMAIN_ETSI	0x30
#define ZD_REGDOMAIN_SPAIN	0x31
#define ZD_REGDOMAIN_FRANCE	0x32
#define ZD_REGDOMAIN_JAPAN_ADD	0x40
#define ZD_REGDOMAIN_JAPAN	0x41

enum {
	MIN_CHANNEL24 = 1,
	MAX_CHANNEL24 = 14,
};

struct channel_range {
	u8 start;
	u8 end; /* exclusive (channel must be less than end) */
};

struct iw_freq;

int zd_geo_init(struct ieee80211_device *ieee, u8 regdomain);

const struct channel_range *zd_channel_range(u8 regdomain);
int zd_regdomain_supports_channel(u8 regdomain, u8 channel);
int zd_regdomain_supported(u8 regdomain);

/* for 2.4 GHz band */
int zd_channel_to_freq(struct iw_freq *freq, u8 channel);
int zd_find_channel(u8 *channel, const struct iw_freq *freq);

#define ZD_PLCP_SERVICE_LENGTH_EXTENSION 0x80

struct ofdm_plcp_header {
	u8 prefix[3];
	__le16 service;
} __attribute__((packed));

static inline u8 zd_ofdm_plcp_header_rate(const struct ofdm_plcp_header *header)
{
	return header->prefix[0] & 0xf;
}

/* The following defines give the encoding of the 4-bit rate field in the
 * OFDM (802.11a/802.11g) PLCP header. Notify that these values are used to
 * define the zd-rate values for OFDM.
 *
 * See the struct zd_ctrlset definition in zd_mac.h.
 */
#define ZD_OFDM_PLCP_RATE_6M		0xb
#define ZD_OFDM_PLCP_RATE_9M		0xf
#define ZD_OFDM_PLCP_RATE_12M	0xa
#define ZD_OFDM_PLCP_RATE_18M	0xe
#define ZD_OFDM_PLCP_RATE_24M	0x9
#define ZD_OFDM_PLCP_RATE_36M	0xd
#define ZD_OFDM_PLCP_RATE_48M	0x8
#define ZD_OFDM_PLCP_RATE_54M	0xc

struct cck_plcp_header {
	u8 signal;
	u8 service;
	__le16 length;
	__le16 crc16;
} __attribute__((packed));

static inline u8 zd_cck_plcp_header_signal(const struct cck_plcp_header *header)
{
	return header->signal;
}

/* These defines give the encodings of the signal field in the 802.11b PLCP
 * header. The signal field gives the bit rate of the following packet. Even
 * if technically wrong we use CCK here also for the 1 MBit/s and 2 MBit/s
 * rate to stay consistent with Zydas and our use of the term.
 *
 * Notify that these values are *not* used in the zd-rates.
 */
#define ZD_CCK_PLCP_SIGNAL_1M	0x0a
#define ZD_CCK_PLCP_SIGNAL_2M	0x14
#define ZD_CCK_PLCP_SIGNAL_5M5	0x37
#define ZD_CCK_PLCP_SIGNAL_11M	0x6e

enum ieee80211_std {
	IEEE80211B = 0x01,
	IEEE80211A = 0x02,
	IEEE80211G = 0x04,
};

#endif /* _ZD_IEEE80211_H */
