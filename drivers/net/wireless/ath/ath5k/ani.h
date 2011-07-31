/*
 * Copyright (C) 2010 Bruno Randolf <br1@einfach.org>
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
#ifndef ANI_H
#define ANI_H

/* these thresholds are relative to the ATH5K_ANI_LISTEN_PERIOD */
#define ATH5K_ANI_LISTEN_PERIOD		100
#define ATH5K_ANI_OFDM_TRIG_HIGH	500
#define ATH5K_ANI_OFDM_TRIG_LOW		200
#define ATH5K_ANI_CCK_TRIG_HIGH		200
#define ATH5K_ANI_CCK_TRIG_LOW		100

/* average beacon RSSI thresholds */
#define ATH5K_ANI_RSSI_THR_HIGH		40
#define ATH5K_ANI_RSSI_THR_LOW		7

/* maximum availabe levels */
#define ATH5K_ANI_MAX_FIRSTEP_LVL	2
#define ATH5K_ANI_MAX_NOISE_IMM_LVL	1


/**
 * enum ath5k_ani_mode - mode for ANI / noise sensitivity
 *
 * @ATH5K_ANI_MODE_OFF: Turn ANI off. This can be useful to just stop the ANI
 *	algorithm after it has been on auto mode.
 * ATH5K_ANI_MODE_MANUAL_LOW: Manually set all immunity parameters to low,
 *	maximizing sensitivity. ANI will not run.
 * ATH5K_ANI_MODE_MANUAL_HIGH: Manually set all immunity parameters to high,
 *	minimizing sensitivity. ANI will not run.
 * ATH5K_ANI_MODE_AUTO: Automatically control immunity parameters based on the
 *	amount of OFDM and CCK frame errors (default).
 */
enum ath5k_ani_mode {
	ATH5K_ANI_MODE_OFF		= 0,
	ATH5K_ANI_MODE_MANUAL_LOW	= 1,
	ATH5K_ANI_MODE_MANUAL_HIGH	= 2,
	ATH5K_ANI_MODE_AUTO		= 3
};


/**
 * struct ath5k_ani_state - ANI state and associated counters
 *
 * @max_spur_level: the maximum spur level is chip dependent
 */
struct ath5k_ani_state {
	enum ath5k_ani_mode	ani_mode;

	/* state */
	int			noise_imm_level;
	int			spur_level;
	int			firstep_level;
	bool			ofdm_weak_sig;
	bool			cck_weak_sig;

	int			max_spur_level;

	/* used by the algorithm */
	unsigned int		listen_time;
	unsigned int		ofdm_errors;
	unsigned int		cck_errors;

	/* debug/statistics only: numbers from last ANI calibration */
	unsigned int		pfc_tx;
	unsigned int		pfc_rx;
	unsigned int		pfc_busy;
	unsigned int		pfc_cycles;
	unsigned int		last_listen;
	unsigned int		last_ofdm_errors;
	unsigned int		last_cck_errors;
	unsigned int		sum_ofdm_errors;
	unsigned int		sum_cck_errors;
};

void ath5k_ani_init(struct ath5k_hw *ah, enum ath5k_ani_mode mode);
void ath5k_ani_mib_intr(struct ath5k_hw *ah);
void ath5k_ani_calibration(struct ath5k_hw *ah);
void ath5k_ani_phy_error_report(struct ath5k_hw *ah,
				enum ath5k_phy_error_code phyerr);

/* for manual control */
void ath5k_ani_set_noise_immunity_level(struct ath5k_hw *ah, int level);
void ath5k_ani_set_spur_immunity_level(struct ath5k_hw *ah, int level);
void ath5k_ani_set_firstep_level(struct ath5k_hw *ah, int level);
void ath5k_ani_set_ofdm_weak_signal_detection(struct ath5k_hw *ah, bool on);
void ath5k_ani_set_cck_weak_signal_detection(struct ath5k_hw *ah, bool on);

void ath5k_ani_print_counters(struct ath5k_hw *ah);

#endif /* ANI_H */
