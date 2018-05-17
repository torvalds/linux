/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 * Copyright (c) 2011 Neratec Solutions AG
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "hw.h"
#include "hw-ops.h"
#include "ath9k.h"
#include "dfs.h"
#include "dfs_debug.h"

/* internal struct to pass radar data */
struct ath_radar_data {
	u8 pulse_bw_info;
	u8 rssi;
	u8 ext_rssi;
	u8 pulse_length_ext;
	u8 pulse_length_pri;
};

/**** begin: CHIRP ************************************************************/

/* min and max gradients for defined FCC chirping pulses, given by
 * - 20MHz chirp width over a pulse width of  50us
 * -  5MHz chirp width over a pulse width of 100us
 */
static const int BIN_DELTA_MIN		= 1;
static const int BIN_DELTA_MAX		= 10;

/* we need at least 3 deltas / 4 samples for a reliable chirp detection */
#define NUM_DIFFS 3
#define FFT_NUM_SAMPLES		(NUM_DIFFS + 1)

/* Threshold for difference of delta peaks */
static const int MAX_DIFF		= 2;

/* width range to be checked for chirping */
static const int MIN_CHIRP_PULSE_WIDTH	= 20;
static const int MAX_CHIRP_PULSE_WIDTH	= 110;

struct ath9k_dfs_fft_20 {
	u8 bin[28];
	u8 lower_bins[3];
} __packed;
struct ath9k_dfs_fft_40 {
	u8 bin[64];
	u8 lower_bins[3];
	u8 upper_bins[3];
} __packed;

static inline int fft_max_index(u8 *bins)
{
	return (bins[2] & 0xfc) >> 2;
}
static inline int fft_max_magnitude(u8 *bins)
{
	return (bins[0] & 0xc0) >> 6 | bins[1] << 2 | (bins[2] & 0x03) << 10;
}
static inline u8 fft_bitmap_weight(u8 *bins)
{
	return bins[0] & 0x3f;
}

static int ath9k_get_max_index_ht40(struct ath9k_dfs_fft_40 *fft,
				    bool is_ctl, bool is_ext)
{
	const int DFS_UPPER_BIN_OFFSET = 64;
	/* if detected radar on both channels, select the significant one */
	if (is_ctl && is_ext) {
		/* first check wether channels have 'strong' bins */
		is_ctl = fft_bitmap_weight(fft->lower_bins) != 0;
		is_ext = fft_bitmap_weight(fft->upper_bins) != 0;

		/* if still unclear, take higher magnitude */
		if (is_ctl && is_ext) {
			int mag_lower = fft_max_magnitude(fft->lower_bins);
			int mag_upper = fft_max_magnitude(fft->upper_bins);
			if (mag_upper > mag_lower)
				is_ctl = false;
			else
				is_ext = false;
		}
	}
	if (is_ctl)
		return fft_max_index(fft->lower_bins);
	return fft_max_index(fft->upper_bins) + DFS_UPPER_BIN_OFFSET;
}
static bool ath9k_check_chirping(struct ath_softc *sc, u8 *data,
				 int datalen, bool is_ctl, bool is_ext)
{
	int i;
	int max_bin[FFT_NUM_SAMPLES];
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	int prev_delta;

	if (IS_CHAN_HT40(ah->curchan)) {
		struct ath9k_dfs_fft_40 *fft = (struct ath9k_dfs_fft_40 *) data;
		int num_fft_packets = datalen / sizeof(*fft);
		if (num_fft_packets == 0)
			return false;

		ath_dbg(common, DFS, "HT40: datalen=%d, num_fft_packets=%d\n",
			datalen, num_fft_packets);
		if (num_fft_packets < FFT_NUM_SAMPLES) {
			ath_dbg(common, DFS, "not enough packets for chirp\n");
			return false;
		}
		/* HW sometimes adds 2 garbage bytes in front of FFT samples */
		if ((datalen % sizeof(*fft)) == 2) {
			fft = (struct ath9k_dfs_fft_40 *) (data + 2);
			ath_dbg(common, DFS, "fixing datalen by 2\n");
		}
		if (IS_CHAN_HT40MINUS(ah->curchan))
			swap(is_ctl, is_ext);

		for (i = 0; i < FFT_NUM_SAMPLES; i++)
			max_bin[i] = ath9k_get_max_index_ht40(fft + i, is_ctl,
							      is_ext);
	} else {
		struct ath9k_dfs_fft_20 *fft = (struct ath9k_dfs_fft_20 *) data;
		int num_fft_packets = datalen / sizeof(*fft);
		if (num_fft_packets == 0)
			return false;
		ath_dbg(common, DFS, "HT20: datalen=%d, num_fft_packets=%d\n",
			datalen, num_fft_packets);
		if (num_fft_packets < FFT_NUM_SAMPLES) {
			ath_dbg(common, DFS, "not enough packets for chirp\n");
			return false;
		}
		/* in ht20, this is a 6-bit signed number => shift it to 0 */
		for (i = 0; i < FFT_NUM_SAMPLES; i++)
			max_bin[i] = fft_max_index(fft[i].lower_bins) ^ 0x20;
	}
	ath_dbg(common, DFS, "bin_max = [%d, %d, %d, %d]\n",
		max_bin[0], max_bin[1], max_bin[2], max_bin[3]);

	/* Check for chirp attributes within specs
	 * a) delta of adjacent max_bins is within range
	 * b) delta of adjacent deltas are within tolerance
	 */
	prev_delta = 0;
	for (i = 0; i < NUM_DIFFS; i++) {
		int ddelta = -1;
		int delta = max_bin[i + 1] - max_bin[i];

		/* ensure gradient is within valid range */
		if (abs(delta) < BIN_DELTA_MIN || abs(delta) > BIN_DELTA_MAX) {
			ath_dbg(common, DFS, "CHIRP: invalid delta %d "
				"in sample %d\n", delta, i);
			return false;
		}
		if (i == 0)
			goto done;
		ddelta = delta - prev_delta;
		if (abs(ddelta) > MAX_DIFF) {
			ath_dbg(common, DFS, "CHIRP: ddelta %d too high\n",
				ddelta);
			return false;
		}
done:
		ath_dbg(common, DFS, "CHIRP - %d: delta=%d, ddelta=%d\n",
			i, delta, ddelta);
		prev_delta = delta;
	}
	return true;
}
/**** end: CHIRP **************************************************************/

/* convert pulse duration to usecs, considering clock mode */
static u32 dur_to_usecs(struct ath_hw *ah, u32 dur)
{
	const u32 AR93X_NSECS_PER_DUR = 800;
	const u32 AR93X_NSECS_PER_DUR_FAST = (8000 / 11);
	u32 nsecs;

	if (IS_CHAN_A_FAST_CLOCK(ah, ah->curchan))
		nsecs = dur * AR93X_NSECS_PER_DUR_FAST;
	else
		nsecs = dur * AR93X_NSECS_PER_DUR;

	return (nsecs + 500) / 1000;
}

#define PRI_CH_RADAR_FOUND 0x01
#define EXT_CH_RADAR_FOUND 0x02
static bool
ath9k_postprocess_radar_event(struct ath_softc *sc,
			      struct ath_radar_data *ard,
			      struct pulse_event *pe)
{
	u8 rssi;
	u16 dur;

	/*
	 * Only the last 2 bits of the BW info are relevant, they indicate
	 * which channel the radar was detected in.
	 */
	ard->pulse_bw_info &= 0x03;

	switch (ard->pulse_bw_info) {
	case PRI_CH_RADAR_FOUND:
		/* radar in ctrl channel */
		dur = ard->pulse_length_pri;
		DFS_STAT_INC(sc, pri_phy_errors);
		/*
		 * cannot use ctrl channel RSSI
		 * if extension channel is stronger
		 */
		rssi = (ard->ext_rssi >= (ard->rssi + 3)) ? 0 : ard->rssi;
		break;
	case EXT_CH_RADAR_FOUND:
		/* radar in extension channel */
		dur = ard->pulse_length_ext;
		DFS_STAT_INC(sc, ext_phy_errors);
		/*
		 * cannot use extension channel RSSI
		 * if control channel is stronger
		 */
		rssi = (ard->rssi >= (ard->ext_rssi + 12)) ? 0 : ard->ext_rssi;
		break;
	case (PRI_CH_RADAR_FOUND | EXT_CH_RADAR_FOUND):
		/*
		 * Conducted testing, when pulse is on DC, both pri and ext
		 * durations are reported to be same
		 *
		 * Radiated testing, when pulse is on DC, different pri and
		 * ext durations are reported, so take the larger of the two
		 */
		if (ard->pulse_length_ext >= ard->pulse_length_pri)
			dur = ard->pulse_length_ext;
		else
			dur = ard->pulse_length_pri;
		DFS_STAT_INC(sc, dc_phy_errors);

		/* when both are present use stronger one */
		rssi = (ard->rssi < ard->ext_rssi) ? ard->ext_rssi : ard->rssi;
		break;
	default:
		/*
		 * Bogus bandwidth info was received in descriptor,
		 * so ignore this PHY error
		 */
		DFS_STAT_INC(sc, bwinfo_discards);
		return false;
	}

	if (rssi == 0) {
		DFS_STAT_INC(sc, rssi_discards);
		return false;
	}

	/* convert duration to usecs */
	pe->width = dur_to_usecs(sc->sc_ah, dur);
	pe->rssi = rssi;

	DFS_STAT_INC(sc, pulses_detected);
	return true;
}

static void
ath9k_dfs_process_radar_pulse(struct ath_softc *sc, struct pulse_event *pe)
{
	struct dfs_pattern_detector *pd = sc->dfs_detector;
	DFS_STAT_INC(sc, pulses_processed);
	if (pd == NULL)
		return;
	if (!pd->add_pulse(pd, pe))
		return;
	DFS_STAT_INC(sc, radar_detected);
	ieee80211_radar_detected(sc->hw);
}

/*
 * DFS: check PHY-error for radar pulse and feed the detector
 */
void ath9k_dfs_process_phyerr(struct ath_softc *sc, void *data,
			      struct ath_rx_status *rs, u64 mactime)
{
	struct ath_radar_data ard;
	u16 datalen;
	char *vdata_end;
	struct pulse_event pe;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	DFS_STAT_INC(sc, pulses_total);
	if ((rs->rs_phyerr != ATH9K_PHYERR_RADAR) &&
	    (rs->rs_phyerr != ATH9K_PHYERR_FALSE_RADAR_EXT)) {
		ath_dbg(common, DFS,
			"Error: rs_phyer=0x%x not a radar error\n",
			rs->rs_phyerr);
		DFS_STAT_INC(sc, pulses_no_dfs);
		return;
	}

	datalen = rs->rs_datalen;
	if (datalen == 0) {
		DFS_STAT_INC(sc, datalen_discards);
		return;
	}

	ard.rssi = rs->rs_rssi_ctl[0];
	ard.ext_rssi = rs->rs_rssi_ext[0];

	/*
	 * hardware stores this as 8 bit signed value.
	 * we will cap it at 0 if it is a negative number
	 */
	if (ard.rssi & 0x80)
		ard.rssi = 0;
	if (ard.ext_rssi & 0x80)
		ard.ext_rssi = 0;

	vdata_end = data + datalen;
	ard.pulse_bw_info = vdata_end[-1];
	ard.pulse_length_ext = vdata_end[-2];
	ard.pulse_length_pri = vdata_end[-3];
	pe.freq = ah->curchan->channel;
	pe.ts = mactime;
	if (!ath9k_postprocess_radar_event(sc, &ard, &pe))
		return;

	if (pe.width > MIN_CHIRP_PULSE_WIDTH &&
	    pe.width < MAX_CHIRP_PULSE_WIDTH) {
		bool is_ctl = !!(ard.pulse_bw_info & PRI_CH_RADAR_FOUND);
		bool is_ext = !!(ard.pulse_bw_info & EXT_CH_RADAR_FOUND);
		int clen = datalen - 3;
		pe.chirp = ath9k_check_chirping(sc, data, clen, is_ctl, is_ext);
	} else {
		pe.chirp = false;
	}

	ath_dbg(common, DFS,
		"ath9k_dfs_process_phyerr: type=%d, freq=%d, ts=%llu, "
		"width=%d, rssi=%d, delta_ts=%llu\n",
		ard.pulse_bw_info, pe.freq, pe.ts, pe.width, pe.rssi,
		pe.ts - sc->dfs_prev_pulse_ts);
	sc->dfs_prev_pulse_ts = pe.ts;
	if (ard.pulse_bw_info & PRI_CH_RADAR_FOUND)
		ath9k_dfs_process_radar_pulse(sc, &pe);
	if (IS_CHAN_HT40(ah->curchan) &&
	    ard.pulse_bw_info & EXT_CH_RADAR_FOUND) {
		pe.freq += IS_CHAN_HT40PLUS(ah->curchan) ? 20 : -20;
		ath9k_dfs_process_radar_pulse(sc, &pe);
	}
}
#undef PRI_CH_RADAR_FOUND
#undef EXT_CH_RADAR_FOUND
