// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2019 Felix Fietkau <nbd@nbd.name>
 */

#include "mt76.h"

#define AVG_PKT_SIZE	1024

/* Number of bits for an average sized packet */
#define MCS_NBITS (AVG_PKT_SIZE << 3)

/* Number of symbols for a packet with (bps) bits per symbol */
#define MCS_NSYMS(bps) DIV_ROUND_UP(MCS_NBITS, (bps))

/* Transmission time (1024 usec) for a packet containing (syms) * symbols */
#define MCS_SYMBOL_TIME(sgi, syms)					\
	(sgi ?								\
	  ((syms) * 18 * 1024 + 4 * 1024) / 5 :	/* syms * 3.6 us */	\
	  ((syms) * 1024) << 2			/* syms * 4 us */	\
	)

/* Transmit duration for the raw data part of an average sized packet */
#define MCS_DURATION(streams, sgi, bps) \
	MCS_SYMBOL_TIME(sgi, MCS_NSYMS((streams) * (bps)))

#define BW_20			0
#define BW_40			1
#define BW_80			2

/*
 * Define group sort order: HT40 -> SGI -> #streams
 */
#define MT_MAX_STREAMS		4
#define MT_HT_STREAM_GROUPS	4 /* BW(=2) * SGI(=2) */
#define MT_VHT_STREAM_GROUPS	6 /* BW(=3) * SGI(=2) */

#define MT_HT_GROUPS_NB	(MT_MAX_STREAMS *		\
				 MT_HT_STREAM_GROUPS)
#define MT_VHT_GROUPS_NB	(MT_MAX_STREAMS *		\
				 MT_VHT_STREAM_GROUPS)
#define MT_GROUPS_NB	(MT_HT_GROUPS_NB +	\
				 MT_VHT_GROUPS_NB)

#define MT_HT_GROUP_0	0
#define MT_VHT_GROUP_0	(MT_HT_GROUP_0 + MT_HT_GROUPS_NB)

#define MCS_GROUP_RATES		10

#define HT_GROUP_IDX(_streams, _sgi, _ht40)	\
	MT_HT_GROUP_0 +			\
	MT_MAX_STREAMS * 2 * _ht40 +	\
	MT_MAX_STREAMS * _sgi +	\
	_streams - 1

#define _MAX(a, b) (((a)>(b))?(a):(b))

#define GROUP_SHIFT(duration)						\
	_MAX(0, 16 - __builtin_clz(duration))

/* MCS rate information for an MCS group */
#define __MCS_GROUP(_streams, _sgi, _ht40, _s)				\
	[HT_GROUP_IDX(_streams, _sgi, _ht40)] = {			\
	.shift = _s,							\
	.duration = {							\
		MCS_DURATION(_streams, _sgi, _ht40 ? 54 : 26) >> _s,	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 108 : 52) >> _s,	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 162 : 78) >> _s,	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 216 : 104) >> _s,	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 324 : 156) >> _s,	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 432 : 208) >> _s,	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 486 : 234) >> _s,	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 540 : 260) >> _s	\
	}								\
}

#define MCS_GROUP_SHIFT(_streams, _sgi, _ht40)				\
	GROUP_SHIFT(MCS_DURATION(_streams, _sgi, _ht40 ? 54 : 26))

#define MCS_GROUP(_streams, _sgi, _ht40)				\
	__MCS_GROUP(_streams, _sgi, _ht40,				\
		    MCS_GROUP_SHIFT(_streams, _sgi, _ht40))

#define VHT_GROUP_IDX(_streams, _sgi, _bw)				\
	(MT_VHT_GROUP_0 +						\
	 MT_MAX_STREAMS * 2 * (_bw) +				\
	 MT_MAX_STREAMS * (_sgi) +				\
	 (_streams) - 1)

#define BW2VBPS(_bw, r3, r2, r1)					\
	(_bw == BW_80 ? r3 : _bw == BW_40 ? r2 : r1)

#define __VHT_GROUP(_streams, _sgi, _bw, _s)				\
	[VHT_GROUP_IDX(_streams, _sgi, _bw)] = {			\
	.shift = _s,							\
	.duration = {							\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw,  117,  54,  26)) >> _s,	\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw,  234, 108,  52)) >> _s,	\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw,  351, 162,  78)) >> _s,	\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw,  468, 216, 104)) >> _s,	\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw,  702, 324, 156)) >> _s,	\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw,  936, 432, 208)) >> _s,	\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw, 1053, 486, 234)) >> _s,	\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw, 1170, 540, 260)) >> _s,	\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw, 1404, 648, 312)) >> _s,	\
		MCS_DURATION(_streams, _sgi,				\
			     BW2VBPS(_bw, 1560, 720, 346)) >> _s	\
	}								\
}

#define VHT_GROUP_SHIFT(_streams, _sgi, _bw)				\
	GROUP_SHIFT(MCS_DURATION(_streams, _sgi,			\
				 BW2VBPS(_bw,  117,  54,  26)))

#define VHT_GROUP(_streams, _sgi, _bw)					\
	__VHT_GROUP(_streams, _sgi, _bw,				\
		    VHT_GROUP_SHIFT(_streams, _sgi, _bw))

struct mcs_group {
	u8 shift;
	u16 duration[MCS_GROUP_RATES];
};

static const struct mcs_group airtime_mcs_groups[] = {
	MCS_GROUP(1, 0, BW_20),
	MCS_GROUP(2, 0, BW_20),
	MCS_GROUP(3, 0, BW_20),
	MCS_GROUP(4, 0, BW_20),

	MCS_GROUP(1, 1, BW_20),
	MCS_GROUP(2, 1, BW_20),
	MCS_GROUP(3, 1, BW_20),
	MCS_GROUP(4, 1, BW_20),

	MCS_GROUP(1, 0, BW_40),
	MCS_GROUP(2, 0, BW_40),
	MCS_GROUP(3, 0, BW_40),
	MCS_GROUP(4, 0, BW_40),

	MCS_GROUP(1, 1, BW_40),
	MCS_GROUP(2, 1, BW_40),
	MCS_GROUP(3, 1, BW_40),
	MCS_GROUP(4, 1, BW_40),

	VHT_GROUP(1, 0, BW_20),
	VHT_GROUP(2, 0, BW_20),
	VHT_GROUP(3, 0, BW_20),
	VHT_GROUP(4, 0, BW_20),

	VHT_GROUP(1, 1, BW_20),
	VHT_GROUP(2, 1, BW_20),
	VHT_GROUP(3, 1, BW_20),
	VHT_GROUP(4, 1, BW_20),

	VHT_GROUP(1, 0, BW_40),
	VHT_GROUP(2, 0, BW_40),
	VHT_GROUP(3, 0, BW_40),
	VHT_GROUP(4, 0, BW_40),

	VHT_GROUP(1, 1, BW_40),
	VHT_GROUP(2, 1, BW_40),
	VHT_GROUP(3, 1, BW_40),
	VHT_GROUP(4, 1, BW_40),

	VHT_GROUP(1, 0, BW_80),
	VHT_GROUP(2, 0, BW_80),
	VHT_GROUP(3, 0, BW_80),
	VHT_GROUP(4, 0, BW_80),

	VHT_GROUP(1, 1, BW_80),
	VHT_GROUP(2, 1, BW_80),
	VHT_GROUP(3, 1, BW_80),
	VHT_GROUP(4, 1, BW_80),
};

static u32
mt76_calc_legacy_rate_duration(const struct ieee80211_rate *rate, bool short_pre,
			       int len)
{
	u32 duration;

	switch (rate->hw_value >> 8) {
	case MT_PHY_TYPE_CCK:
		duration = 144 + 48; /* preamble + PLCP */
		if (short_pre)
			duration >>= 1;

		duration += 10; /* SIFS */
		break;
	case MT_PHY_TYPE_OFDM:
		duration = 20 + 16; /* premable + SIFS */
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	len <<= 3;
	duration += (len * 10) / rate->bitrate;

	return duration;
}

u32 mt76_calc_rx_airtime(struct mt76_dev *dev, struct mt76_rx_status *status,
			 int len)
{
	struct ieee80211_supported_band *sband;
	const struct ieee80211_rate *rate;
	bool sgi = status->enc_flags & RX_ENC_FLAG_SHORT_GI;
	bool sp = status->enc_flags & RX_ENC_FLAG_SHORTPRE;
	int bw, streams;
	u32 duration;
	int group, idx;

	switch (status->bw) {
	case RATE_INFO_BW_20:
		bw = BW_20;
		break;
	case RATE_INFO_BW_40:
		bw = BW_40;
		break;
	case RATE_INFO_BW_80:
		bw = BW_80;
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	switch (status->encoding) {
	case RX_ENC_LEGACY:
		if (WARN_ON_ONCE(status->band > NL80211_BAND_5GHZ))
			return 0;

		sband = dev->hw->wiphy->bands[status->band];
		if (!sband || status->rate_idx >= sband->n_bitrates)
			return 0;

		rate = &sband->bitrates[status->rate_idx];

		return mt76_calc_legacy_rate_duration(rate, sp, len);
	case RX_ENC_VHT:
		streams = status->nss;
		idx = status->rate_idx;
		group = VHT_GROUP_IDX(streams, sgi, bw);
		break;
	case RX_ENC_HT:
		streams = ((status->rate_idx >> 3) & 3) + 1;
		idx = status->rate_idx & 7;
		group = HT_GROUP_IDX(streams, sgi, bw);
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	if (WARN_ON_ONCE(streams > 4))
		return 0;

	duration = airtime_mcs_groups[group].duration[idx];
	duration <<= airtime_mcs_groups[group].shift;
	duration *= len;
	duration /= AVG_PKT_SIZE;
	duration /= 1024;

	duration += 36 + (streams << 2);

	return duration;
}

u32 mt76_calc_tx_airtime(struct mt76_dev *dev, struct ieee80211_tx_info *info,
			 int len)
{
	struct mt76_rx_status stat = {
		.band = info->band,
	};
	u32 duration = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(info->status.rates); i++) {
		struct ieee80211_tx_rate *rate = &info->status.rates[i];
		u32 cur_duration;

		if (rate->idx < 0 || !rate->count)
			break;

		if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			stat.bw = RATE_INFO_BW_80;
		else if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			stat.bw = RATE_INFO_BW_40;
		else
			stat.bw = RATE_INFO_BW_20;

		stat.enc_flags = 0;
		if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			stat.enc_flags |= RX_ENC_FLAG_SHORTPRE;
		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			stat.enc_flags |= RX_ENC_FLAG_SHORT_GI;

		stat.rate_idx = rate->idx;
		if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
			stat.encoding = RX_ENC_VHT;
			stat.rate_idx = ieee80211_rate_get_vht_mcs(rate);
			stat.nss = ieee80211_rate_get_vht_nss(rate);
		} else if (rate->flags & IEEE80211_TX_RC_MCS) {
			stat.encoding = RX_ENC_HT;
		} else {
			stat.encoding = RX_ENC_LEGACY;
		}

		cur_duration = mt76_calc_rx_airtime(dev, &stat, len);
		duration += cur_duration * rate->count;
	}

	return duration;
}
EXPORT_SYMBOL_GPL(mt76_calc_tx_airtime);
