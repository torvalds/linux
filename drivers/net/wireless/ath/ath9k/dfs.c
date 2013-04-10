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

	ath_dbg(ath9k_hw_common(sc->sc_ah), DFS,
		"pulse_bw_info=0x%x, pri,ext len/rssi=(%u/%u, %u/%u)\n",
		ard->pulse_bw_info,
		ard->pulse_length_pri, ard->rssi,
		ard->pulse_length_ext, ard->ext_rssi);

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

	/*
	 * TODO: check chirping pulses
	 *	 checks for chirping are dependent on the DFS regulatory domain
	 *	 used, which is yet TBD
	 */

	/* convert duration to usecs */
	pe->width = dur_to_usecs(sc->sc_ah, dur);
	pe->rssi = rssi;

	DFS_STAT_INC(sc, pulses_detected);
	return true;
}
#undef PRI_CH_RADAR_FOUND
#undef EXT_CH_RADAR_FOUND

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

	ard.rssi = rs->rs_rssi_ctl0;
	ard.ext_rssi = rs->rs_rssi_ext0;

	/*
	 * hardware stores this as 8 bit signed value.
	 * we will cap it at 0 if it is a negative number
	 */
	if (ard.rssi & 0x80)
		ard.rssi = 0;
	if (ard.ext_rssi & 0x80)
		ard.ext_rssi = 0;

	vdata_end = (char *)data + datalen;
	ard.pulse_bw_info = vdata_end[-1];
	ard.pulse_length_ext = vdata_end[-2];
	ard.pulse_length_pri = vdata_end[-3];
	pe.freq = ah->curchan->channel;
	pe.ts = mactime;
	if (ath9k_postprocess_radar_event(sc, &ard, &pe)) {
		struct dfs_pattern_detector *pd = sc->dfs_detector;
		static u64 last_ts;
		ath_dbg(common, DFS,
			"ath9k_dfs_process_phyerr: channel=%d, ts=%llu, "
			"width=%d, rssi=%d, delta_ts=%llu\n",
			pe.freq, pe.ts, pe.width, pe.rssi, pe.ts-last_ts);
		last_ts = pe.ts;
		DFS_STAT_INC(sc, pulses_processed);
		if (pd != NULL && pd->add_pulse(pd, &pe)) {
			DFS_STAT_INC(sc, radar_detected);
			ieee80211_radar_detected(sc->hw);
		}
	}
}
