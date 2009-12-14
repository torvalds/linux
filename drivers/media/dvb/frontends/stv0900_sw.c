/*
 * stv0900_sw.c
 *
 * Driver for ST STV0900 satellite demodulator IC.
 *
 * Copyright (C) ST Microelectronics.
 * Copyright (C) 2009 NetUP Inc.
 * Copyright (C) 2009 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "stv0900.h"
#include "stv0900_reg.h"
#include "stv0900_priv.h"

s32 shiftx(s32 x, int demod, s32 shift)
{
	if (demod == 1)
		return x - shift;

	return x;
}

int stv0900_check_signal_presence(struct stv0900_internal *intp,
					enum fe_stv0900_demod_num demod)
{
	s32	carr_offset,
		agc2_integr,
		max_carrier;

	int no_signal = FALSE;

	carr_offset = (stv0900_read_reg(intp, CFR2) << 8)
					| stv0900_read_reg(intp, CFR1);
	carr_offset = ge2comp(carr_offset, 16);
	agc2_integr = (stv0900_read_reg(intp, AGC2I1) << 8)
					| stv0900_read_reg(intp, AGC2I0);
	max_carrier = intp->srch_range[demod] / 1000;

	max_carrier += (max_carrier / 10);
	max_carrier = 65536 * (max_carrier / 2);
	max_carrier /= intp->mclk / 1000;
	if (max_carrier > 0x4000)
		max_carrier = 0x4000;

	if ((agc2_integr > 0x2000)
			|| (carr_offset > (2 * max_carrier))
			|| (carr_offset < (-2 * max_carrier)))
		no_signal = TRUE;

	return no_signal;
}

static void stv0900_get_sw_loop_params(struct stv0900_internal *intp,
				s32 *frequency_inc, s32 *sw_timeout,
				s32 *steps,
				enum fe_stv0900_demod_num demod)
{
	s32 timeout, freq_inc, max_steps, srate, max_carrier;

	enum fe_stv0900_search_standard	standard;

	srate = intp->symbol_rate[demod];
	max_carrier = intp->srch_range[demod] / 1000;
	max_carrier += max_carrier / 10;
	standard = intp->srch_standard[demod];

	max_carrier = 65536 * (max_carrier / 2);
	max_carrier /= intp->mclk / 1000;

	if (max_carrier > 0x4000)
		max_carrier = 0x4000;

	freq_inc = srate;
	freq_inc /= intp->mclk >> 10;
	freq_inc = freq_inc << 6;

	switch (standard) {
	case STV0900_SEARCH_DVBS1:
	case STV0900_SEARCH_DSS:
		freq_inc *= 3;
		timeout = 20;
		break;
	case STV0900_SEARCH_DVBS2:
		freq_inc *= 4;
		timeout = 25;
		break;
	case STV0900_AUTO_SEARCH:
	default:
		freq_inc *= 3;
		timeout = 25;
		break;
	}

	freq_inc /= 100;

	if ((freq_inc > max_carrier) || (freq_inc < 0))
		freq_inc = max_carrier / 2;

	timeout *= 27500;

	if (srate > 0)
		timeout /= srate / 1000;

	if ((timeout > 100) || (timeout < 0))
		timeout = 100;

	max_steps = (max_carrier / freq_inc) + 1;

	if ((max_steps > 100) || (max_steps < 0)) {
		max_steps =  100;
		freq_inc = max_carrier / max_steps;
	}

	*frequency_inc = freq_inc;
	*sw_timeout = timeout;
	*steps = max_steps;

}

static int stv0900_search_carr_sw_loop(struct stv0900_internal *intp,
				s32 FreqIncr, s32 Timeout, int zigzag,
				s32 MaxStep, enum fe_stv0900_demod_num demod)
{
	int	no_signal,
		lock = FALSE;
	s32	stepCpt,
		freqOffset,
		max_carrier;

	max_carrier = intp->srch_range[demod] / 1000;
	max_carrier += (max_carrier / 10);

	max_carrier = 65536 * (max_carrier / 2);
	max_carrier /= intp->mclk / 1000;

	if (max_carrier > 0x4000)
		max_carrier = 0x4000;

	if (zigzag == TRUE)
		freqOffset = 0;
	else
		freqOffset = -max_carrier + FreqIncr;

	stepCpt = 0;

	do {
		stv0900_write_reg(intp, DMDISTATE, 0x1c);
		stv0900_write_reg(intp, CFRINIT1, (freqOffset / 256) & 0xff);
		stv0900_write_reg(intp, CFRINIT0, freqOffset & 0xff);
		stv0900_write_reg(intp, DMDISTATE, 0x18);
		stv0900_write_bits(intp, ALGOSWRST, 1);

		if (intp->chip_id == 0x12) {
			stv0900_write_bits(intp, RST_HWARE, 1);
			stv0900_write_bits(intp, RST_HWARE, 0);
		}

		if (zigzag == TRUE) {
			if (freqOffset >= 0)
				freqOffset = -freqOffset - 2 * FreqIncr;
			else
				freqOffset = -freqOffset;
		} else
			freqOffset += + 2 * FreqIncr;

		stepCpt++;
		lock = stv0900_get_demod_lock(intp, demod, Timeout);
		no_signal = stv0900_check_signal_presence(intp, demod);

	} while ((lock == FALSE)
			&& (no_signal == FALSE)
			&& ((freqOffset - FreqIncr) <  max_carrier)
			&& ((freqOffset + FreqIncr) > -max_carrier)
			&& (stepCpt < MaxStep));

	stv0900_write_bits(intp, ALGOSWRST, 0);

	return lock;
}

int stv0900_sw_algo(struct stv0900_internal *intp,
				enum fe_stv0900_demod_num demod)
{
	int	lock = FALSE,
		no_signal,
		zigzag;
	s32	s2fw,
		fqc_inc,
		sft_stp_tout,
		trial_cntr,
		max_steps;

	stv0900_get_sw_loop_params(intp, &fqc_inc, &sft_stp_tout,
					&max_steps, demod);
	switch (intp->srch_standard[demod]) {
	case STV0900_SEARCH_DVBS1:
	case STV0900_SEARCH_DSS:
		if (intp->chip_id >= 0x20)
			stv0900_write_reg(intp, CARFREQ, 0x3b);
		else
			stv0900_write_reg(intp, CARFREQ, 0xef);

		stv0900_write_reg(intp, DMDCFGMD, 0x49);
		zigzag = FALSE;
		break;
	case STV0900_SEARCH_DVBS2:
		if (intp->chip_id >= 0x20)
			stv0900_write_reg(intp, CORRELABS, 0x79);
		else
			stv0900_write_reg(intp, CORRELABS, 0x68);

		stv0900_write_reg(intp, DMDCFGMD, 0x89);

		zigzag = TRUE;
		break;
	case STV0900_AUTO_SEARCH:
	default:
		if (intp->chip_id >= 0x20) {
			stv0900_write_reg(intp, CARFREQ, 0x3b);
			stv0900_write_reg(intp, CORRELABS, 0x79);
		} else {
			stv0900_write_reg(intp, CARFREQ, 0xef);
			stv0900_write_reg(intp, CORRELABS, 0x68);
		}

		stv0900_write_reg(intp, DMDCFGMD, 0xc9);
		zigzag = FALSE;
		break;
	}

	trial_cntr = 0;
	do {
		lock = stv0900_search_carr_sw_loop(intp,
						fqc_inc,
						sft_stp_tout,
						zigzag,
						max_steps,
						demod);
		no_signal = stv0900_check_signal_presence(intp, demod);
		trial_cntr++;
		if ((lock == TRUE)
				|| (no_signal == TRUE)
				|| (trial_cntr == 2)) {

			if (intp->chip_id >= 0x20) {
				stv0900_write_reg(intp, CARFREQ, 0x49);
				stv0900_write_reg(intp, CORRELABS, 0x9e);
			} else {
				stv0900_write_reg(intp, CARFREQ, 0xed);
				stv0900_write_reg(intp, CORRELABS, 0x88);
			}

			if ((stv0900_get_bits(intp, HEADER_MODE) ==
						STV0900_DVBS2_FOUND) &&
							(lock == TRUE)) {
				msleep(sft_stp_tout);
				s2fw = stv0900_get_bits(intp, FLYWHEEL_CPT);

				if (s2fw < 0xd) {
					msleep(sft_stp_tout);
					s2fw = stv0900_get_bits(intp,
								FLYWHEEL_CPT);
				}

				if (s2fw < 0xd) {
					lock = FALSE;

					if (trial_cntr < 2) {
						if (intp->chip_id >= 0x20)
							stv0900_write_reg(intp,
								CORRELABS,
								0x79);
						else
							stv0900_write_reg(intp,
								CORRELABS,
								0x68);

						stv0900_write_reg(intp,
								DMDCFGMD,
								0x89);
					}
				}
			}
		}

	} while ((lock == FALSE)
		&& (trial_cntr < 2)
		&& (no_signal == FALSE));

	return lock;
}

static u32 stv0900_get_symbol_rate(struct stv0900_internal *intp,
					u32 mclk,
					enum fe_stv0900_demod_num demod)
{
	s32	rem1, rem2, intval1, intval2, srate;

	srate = (stv0900_get_bits(intp, SYMB_FREQ3) << 24) +
		(stv0900_get_bits(intp, SYMB_FREQ2) << 16) +
		(stv0900_get_bits(intp, SYMB_FREQ1) << 8) +
		(stv0900_get_bits(intp, SYMB_FREQ0));
	dprintk("lock: srate=%d r0=0x%x r1=0x%x r2=0x%x r3=0x%x \n",
		srate, stv0900_get_bits(intp, SYMB_FREQ0),
		stv0900_get_bits(intp, SYMB_FREQ1),
		stv0900_get_bits(intp, SYMB_FREQ2),
		stv0900_get_bits(intp, SYMB_FREQ3));

	intval1 = (mclk) >> 16;
	intval2 = (srate) >> 16;

	rem1 = (mclk) % 0x10000;
	rem2 = (srate) % 0x10000;
	srate =	(intval1 * intval2) +
		((intval1 * rem2) >> 16) +
		((intval2 * rem1) >> 16);

	return srate;
}

static void stv0900_set_symbol_rate(struct stv0900_internal *intp,
					u32 mclk, u32 srate,
					enum fe_stv0900_demod_num demod)
{
	u32 symb;

	dprintk("%s: Mclk %d, SR %d, Dmd %d\n", __func__, mclk,
							srate, demod);

	if (srate > 60000000) {
		symb = srate << 4;
		symb /= (mclk >> 12);
	} else if (srate > 6000000) {
		symb = srate << 6;
		symb /= (mclk >> 10);
	} else {
		symb = srate << 9;
		symb /= (mclk >> 7);
	}

	stv0900_write_reg(intp, SFRINIT1, (symb >> 8) & 0x7f);
	stv0900_write_reg(intp, SFRINIT1 + 1, (symb & 0xff));
}

static void stv0900_set_max_symbol_rate(struct stv0900_internal *intp,
					u32 mclk, u32 srate,
					enum fe_stv0900_demod_num demod)
{
	u32 symb;

	srate = 105 * (srate / 100);

	if (srate > 60000000) {
		symb = srate << 4;
		symb /= (mclk >> 12);
	} else if (srate > 6000000) {
		symb = srate << 6;
		symb /= (mclk >> 10);
	} else {
		symb = srate << 9;
		symb /= (mclk >> 7);
	}

	if (symb < 0x7fff) {
		stv0900_write_reg(intp, SFRUP1, (symb >> 8) & 0x7f);
		stv0900_write_reg(intp, SFRUP1 + 1, (symb & 0xff));
	} else {
		stv0900_write_reg(intp, SFRUP1, 0x7f);
		stv0900_write_reg(intp, SFRUP1 + 1, 0xff);
	}
}

static void stv0900_set_min_symbol_rate(struct stv0900_internal *intp,
					u32 mclk, u32 srate,
					enum fe_stv0900_demod_num demod)
{
	u32	symb;

	srate = 95 * (srate / 100);
	if (srate > 60000000) {
		symb = srate << 4;
		symb /= (mclk >> 12);

	} else if (srate > 6000000) {
		symb = srate << 6;
		symb /= (mclk >> 10);

	} else {
		symb = srate << 9;
		symb /= (mclk >> 7);
	}

	stv0900_write_reg(intp, SFRLOW1, (symb >> 8) & 0xff);
	stv0900_write_reg(intp, SFRLOW1 + 1, (symb & 0xff));
}

static s32 stv0900_get_timing_offst(struct stv0900_internal *intp,
					u32 srate,
					enum fe_stv0900_demod_num demod)
{
	s32 timingoffset;


	timingoffset = (stv0900_read_reg(intp, TMGREG2) << 16) +
		       (stv0900_read_reg(intp, TMGREG2 + 1) << 8) +
		       (stv0900_read_reg(intp, TMGREG2 + 2));

	timingoffset = ge2comp(timingoffset, 24);


	if (timingoffset == 0)
		timingoffset = 1;

	timingoffset = ((s32)srate * 10) / ((s32)0x1000000 / timingoffset);
	timingoffset /= 320;

	return timingoffset;
}

static void stv0900_set_dvbs2_rolloff(struct stv0900_internal *intp,
					enum fe_stv0900_demod_num demod)
{
	s32 rolloff;

	if (intp->chip_id == 0x10) {
		stv0900_write_bits(intp, MANUALSX_ROLLOFF, 1);
		rolloff = stv0900_read_reg(intp, MATSTR1) & 0x03;
		stv0900_write_bits(intp, ROLLOFF_CONTROL, rolloff);
	} else if (intp->chip_id <= 0x20)
		stv0900_write_bits(intp, MANUALSX_ROLLOFF, 0);
	else /* cut 3.0 */
		stv0900_write_bits(intp, MANUALS2_ROLLOFF, 0);
}

static u32 stv0900_carrier_width(u32 srate, enum fe_stv0900_rolloff ro)
{
	u32 rolloff;

	switch (ro) {
	case STV0900_20:
		rolloff = 20;
		break;
	case STV0900_25:
		rolloff = 25;
		break;
	case STV0900_35:
	default:
		rolloff = 35;
		break;
	}

	return srate  + (srate * rolloff) / 100;
}

static int stv0900_check_timing_lock(struct stv0900_internal *intp,
				enum fe_stv0900_demod_num demod)
{
	int timingLock = FALSE;
	s32	i,
		timingcpt = 0;
	u8	car_freq,
		tmg_th_high,
		tmg_th_low;

	car_freq = stv0900_read_reg(intp, CARFREQ);
	tmg_th_high = stv0900_read_reg(intp, TMGTHRISE);
	tmg_th_low = stv0900_read_reg(intp, TMGTHFALL);
	stv0900_write_reg(intp, TMGTHRISE, 0x20);
	stv0900_write_reg(intp, TMGTHFALL, 0x0);
	stv0900_write_bits(intp, CFR_AUTOSCAN, 0);
	stv0900_write_reg(intp, RTC, 0x80);
	stv0900_write_reg(intp, RTCS2, 0x40);
	stv0900_write_reg(intp, CARFREQ, 0x0);
	stv0900_write_reg(intp, CFRINIT1, 0x0);
	stv0900_write_reg(intp, CFRINIT0, 0x0);
	stv0900_write_reg(intp, AGC2REF, 0x65);
	stv0900_write_reg(intp, DMDISTATE, 0x18);
	msleep(7);

	for (i = 0; i < 10; i++) {
		if (stv0900_get_bits(intp, TMGLOCK_QUALITY) >= 2)
			timingcpt++;

		msleep(1);
	}

	if (timingcpt >= 3)
		timingLock = TRUE;

	stv0900_write_reg(intp, AGC2REF, 0x38);
	stv0900_write_reg(intp, RTC, 0x88);
	stv0900_write_reg(intp, RTCS2, 0x68);
	stv0900_write_reg(intp, CARFREQ, car_freq);
	stv0900_write_reg(intp, TMGTHRISE, tmg_th_high);
	stv0900_write_reg(intp, TMGTHFALL, tmg_th_low);

	return	timingLock;
}

static int stv0900_get_demod_cold_lock(struct dvb_frontend *fe,
					s32 demod_timeout)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	int	lock = FALSE,
		d = demod;
	s32	srate,
		search_range,
		locktimeout,
		currier_step,
		nb_steps,
		current_step,
		direction,
		tuner_freq,
		timeout,
		freq;

	srate = intp->symbol_rate[d];
	search_range = intp->srch_range[d];

	if (srate >= 10000000)
		locktimeout = demod_timeout / 3;
	else
		locktimeout = demod_timeout / 2;

	lock = stv0900_get_demod_lock(intp, d, locktimeout);

	if (lock != FALSE)
		return lock;

	if (srate >= 10000000) {
		if (stv0900_check_timing_lock(intp, d) == TRUE) {
			stv0900_write_reg(intp, DMDISTATE, 0x1f);
			stv0900_write_reg(intp, DMDISTATE, 0x15);
			lock = stv0900_get_demod_lock(intp, d, demod_timeout);
		} else
			lock = FALSE;

		return lock;
	}

	if (intp->chip_id <= 0x20) {
		if (srate <= 1000000)
			currier_step = 500;
		else if (srate <= 4000000)
			currier_step = 1000;
		else if (srate <= 7000000)
			currier_step = 2000;
		else if (srate <= 10000000)
			currier_step = 3000;
		else
			currier_step = 5000;

		if (srate >= 2000000) {
			timeout = (demod_timeout / 3);
			if (timeout > 1000)
				timeout = 1000;
		} else
			timeout = (demod_timeout / 2);
	} else {
		/*cut 3.0 */
		currier_step = srate / 4000;
		timeout = (demod_timeout * 3) / 4;
	}

	nb_steps = ((search_range / 1000) / currier_step);

	if ((nb_steps % 2) != 0)
		nb_steps += 1;

	if (nb_steps <= 0)
		nb_steps = 2;
	else if (nb_steps > 12)
		nb_steps = 12;

	current_step = 1;
	direction = 1;

	if (intp->chip_id <= 0x20) {
		tuner_freq = intp->freq[d];
		intp->bw[d] = stv0900_carrier_width(intp->symbol_rate[d],
				intp->rolloff) + intp->symbol_rate[d];
	} else
		tuner_freq = 0;

	while ((current_step <= nb_steps) && (lock == FALSE)) {
		if (direction > 0)
			tuner_freq += (current_step * currier_step);
		else
			tuner_freq -= (current_step * currier_step);

		if (intp->chip_id <= 0x20) {
			if (intp->tuner_type[d] == 3)
				stv0900_set_tuner_auto(intp, tuner_freq,
						intp->bw[d], demod);
			else
				stv0900_set_tuner(fe, tuner_freq, intp->bw[d]);

			stv0900_write_reg(intp, DMDISTATE, 0x1c);
			stv0900_write_reg(intp, CFRINIT1, 0);
			stv0900_write_reg(intp, CFRINIT0, 0);
			stv0900_write_reg(intp, DMDISTATE, 0x1f);
			stv0900_write_reg(intp, DMDISTATE, 0x15);
		} else {
			stv0900_write_reg(intp, DMDISTATE, 0x1c);
			freq = (tuner_freq * 65536) / (intp->mclk / 1000);
			stv0900_write_bits(intp, CFR_INIT1, MSB(freq));
			stv0900_write_bits(intp, CFR_INIT0, LSB(freq));
			stv0900_write_reg(intp, DMDISTATE, 0x1f);
			stv0900_write_reg(intp, DMDISTATE, 0x05);
		}

		lock = stv0900_get_demod_lock(intp, d, timeout);
		direction *= -1;
		current_step++;
	}

	return	lock;
}

static void stv0900_get_lock_timeout(s32 *demod_timeout, s32 *fec_timeout,
					s32 srate,
					enum fe_stv0900_search_algo algo)
{
	switch (algo) {
	case STV0900_BLIND_SEARCH:
		if (srate <= 1500000) {
			(*demod_timeout) = 1500;
			(*fec_timeout) = 400;
		} else if (srate <= 5000000) {
			(*demod_timeout) = 1000;
			(*fec_timeout) = 300;
		} else {
			(*demod_timeout) = 700;
			(*fec_timeout) = 100;
		}

		break;
	case STV0900_COLD_START:
	case STV0900_WARM_START:
	default:
		if (srate <= 1000000) {
			(*demod_timeout) = 3000;
			(*fec_timeout) = 1700;
		} else if (srate <= 2000000) {
			(*demod_timeout) = 2500;
			(*fec_timeout) = 1100;
		} else if (srate <= 5000000) {
			(*demod_timeout) = 1000;
			(*fec_timeout) = 550;
		} else if (srate <= 10000000) {
			(*demod_timeout) = 700;
			(*fec_timeout) = 250;
		} else if (srate <= 20000000) {
			(*demod_timeout) = 400;
			(*fec_timeout) = 130;
		} else {
			(*demod_timeout) = 300;
			(*fec_timeout) = 100;
		}

		break;

	}

	if (algo == STV0900_WARM_START)
		(*demod_timeout) /= 2;
}

static void stv0900_set_viterbi_tracq(struct stv0900_internal *intp,
					enum fe_stv0900_demod_num demod)
{

	s32 vth_reg = VTH12;

	dprintk("%s\n", __func__);

	stv0900_write_reg(intp, vth_reg++, 0xd0);
	stv0900_write_reg(intp, vth_reg++, 0x7d);
	stv0900_write_reg(intp, vth_reg++, 0x53);
	stv0900_write_reg(intp, vth_reg++, 0x2f);
	stv0900_write_reg(intp, vth_reg++, 0x24);
	stv0900_write_reg(intp, vth_reg++, 0x1f);
}

static void stv0900_set_viterbi_standard(struct stv0900_internal *intp,
				   enum fe_stv0900_search_standard standard,
				   enum fe_stv0900_fec fec,
				   enum fe_stv0900_demod_num demod)
{
	dprintk("%s: ViterbiStandard = ", __func__);

	switch (standard) {
	case STV0900_AUTO_SEARCH:
		dprintk("Auto\n");
		stv0900_write_reg(intp, FECM, 0x10);
		stv0900_write_reg(intp, PRVIT, 0x3f);
		break;
	case STV0900_SEARCH_DVBS1:
		dprintk("DVBS1\n");
		stv0900_write_reg(intp, FECM, 0x00);
		switch (fec) {
		case STV0900_FEC_UNKNOWN:
		default:
			stv0900_write_reg(intp, PRVIT, 0x2f);
			break;
		case STV0900_FEC_1_2:
			stv0900_write_reg(intp, PRVIT, 0x01);
			break;
		case STV0900_FEC_2_3:
			stv0900_write_reg(intp, PRVIT, 0x02);
			break;
		case STV0900_FEC_3_4:
			stv0900_write_reg(intp, PRVIT, 0x04);
			break;
		case STV0900_FEC_5_6:
			stv0900_write_reg(intp, PRVIT, 0x08);
			break;
		case STV0900_FEC_7_8:
			stv0900_write_reg(intp, PRVIT, 0x20);
			break;
		}

		break;
	case STV0900_SEARCH_DSS:
		dprintk("DSS\n");
		stv0900_write_reg(intp, FECM, 0x80);
		switch (fec) {
		case STV0900_FEC_UNKNOWN:
		default:
			stv0900_write_reg(intp, PRVIT, 0x13);
			break;
		case STV0900_FEC_1_2:
			stv0900_write_reg(intp, PRVIT, 0x01);
			break;
		case STV0900_FEC_2_3:
			stv0900_write_reg(intp, PRVIT, 0x02);
			break;
		case STV0900_FEC_6_7:
			stv0900_write_reg(intp, PRVIT, 0x10);
			break;
		}
		break;
	default:
		break;
	}
}

static enum fe_stv0900_fec stv0900_get_vit_fec(struct stv0900_internal *intp,
						enum fe_stv0900_demod_num demod)
{
	enum fe_stv0900_fec prate;
	s32 rate_fld = stv0900_get_bits(intp, VIT_CURPUN);

	switch (rate_fld) {
	case 13:
		prate = STV0900_FEC_1_2;
		break;
	case 18:
		prate = STV0900_FEC_2_3;
		break;
	case 21:
		prate = STV0900_FEC_3_4;
		break;
	case 24:
		prate = STV0900_FEC_5_6;
		break;
	case 25:
		prate = STV0900_FEC_6_7;
		break;
	case 26:
		prate = STV0900_FEC_7_8;
		break;
	default:
		prate = STV0900_FEC_UNKNOWN;
		break;
	}

	return prate;
}

void stv0900_set_dvbs1_track_car_loop(struct stv0900_internal *intp,
					enum fe_stv0900_demod_num demod,
					u32 srate)
{
	if (intp->chip_id >= 0x30) {
		if (srate >= 15000000) {
			stv0900_write_reg(intp, ACLC, 0x2b);
			stv0900_write_reg(intp, BCLC, 0x1a);
		} else if ((srate >= 7000000) && (15000000 > srate)) {
			stv0900_write_reg(intp, ACLC, 0x0c);
			stv0900_write_reg(intp, BCLC, 0x1b);
		} else if (srate < 7000000) {
			stv0900_write_reg(intp, ACLC, 0x2c);
			stv0900_write_reg(intp, BCLC, 0x1c);
		}

	} else { /*cut 2.0 and 1.x*/
		stv0900_write_reg(intp, ACLC, 0x1a);
		stv0900_write_reg(intp, BCLC, 0x09);
	}

}

static void stv0900_track_optimization(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	s32	srate,
		pilots,
		aclc,
		freq1,
		freq0,
		i = 0,
		timed,
		timef,
		blind_tun_sw = 0,
		modulation;

	enum fe_stv0900_rolloff rolloff;
	enum fe_stv0900_modcode foundModcod;

	dprintk("%s\n", __func__);

	srate = stv0900_get_symbol_rate(intp, intp->mclk, demod);
	srate += stv0900_get_timing_offst(intp, srate, demod);

	switch (intp->result[demod].standard) {
	case STV0900_DVBS1_STANDARD:
	case STV0900_DSS_STANDARD:
		dprintk("%s: found DVB-S or DSS\n", __func__);
		if (intp->srch_standard[demod] == STV0900_AUTO_SEARCH) {
			stv0900_write_bits(intp, DVBS1_ENABLE, 1);
			stv0900_write_bits(intp, DVBS2_ENABLE, 0);
		}

		stv0900_write_bits(intp, ROLLOFF_CONTROL, intp->rolloff);
		stv0900_write_bits(intp, MANUALSX_ROLLOFF, 1);

		if (intp->chip_id < 0x30) {
			stv0900_write_reg(intp, ERRCTRL1, 0x75);
			break;
		}

		if (stv0900_get_vit_fec(intp, demod) == STV0900_FEC_1_2) {
			stv0900_write_reg(intp, GAUSSR0, 0x98);
			stv0900_write_reg(intp, CCIR0, 0x18);
		} else {
			stv0900_write_reg(intp, GAUSSR0, 0x18);
			stv0900_write_reg(intp, CCIR0, 0x18);
		}

		stv0900_write_reg(intp, ERRCTRL1, 0x75);
		break;
	case STV0900_DVBS2_STANDARD:
		dprintk("%s: found DVB-S2\n", __func__);
		stv0900_write_bits(intp, DVBS1_ENABLE, 0);
		stv0900_write_bits(intp, DVBS2_ENABLE, 1);
		stv0900_write_reg(intp, ACLC, 0);
		stv0900_write_reg(intp, BCLC, 0);
		if (intp->result[demod].frame_len == STV0900_LONG_FRAME) {
			foundModcod = stv0900_get_bits(intp, DEMOD_MODCOD);
			pilots = stv0900_get_bits(intp, DEMOD_TYPE) & 0x01;
			aclc = stv0900_get_optim_carr_loop(srate,
							foundModcod,
							pilots,
							intp->chip_id);
			if (foundModcod <= STV0900_QPSK_910)
				stv0900_write_reg(intp, ACLC2S2Q, aclc);
			else if (foundModcod <= STV0900_8PSK_910) {
				stv0900_write_reg(intp, ACLC2S2Q, 0x2a);
				stv0900_write_reg(intp, ACLC2S28, aclc);
			}

			if ((intp->demod_mode == STV0900_SINGLE) &&
					(foundModcod > STV0900_8PSK_910)) {
				if (foundModcod <= STV0900_16APSK_910) {
					stv0900_write_reg(intp, ACLC2S2Q, 0x2a);
					stv0900_write_reg(intp, ACLC2S216A,
									aclc);
				} else if (foundModcod <= STV0900_32APSK_910) {
					stv0900_write_reg(intp, ACLC2S2Q, 0x2a);
					stv0900_write_reg(intp,	ACLC2S232A,
									aclc);
				}
			}

		} else {
			modulation = intp->result[demod].modulation;
			aclc = stv0900_get_optim_short_carr_loop(srate,
					modulation, intp->chip_id);
			if (modulation == STV0900_QPSK)
				stv0900_write_reg(intp, ACLC2S2Q, aclc);
			else if (modulation == STV0900_8PSK) {
				stv0900_write_reg(intp, ACLC2S2Q, 0x2a);
				stv0900_write_reg(intp, ACLC2S28, aclc);
			} else if (modulation == STV0900_16APSK) {
				stv0900_write_reg(intp, ACLC2S2Q, 0x2a);
				stv0900_write_reg(intp, ACLC2S216A, aclc);
			} else if (modulation == STV0900_32APSK) {
				stv0900_write_reg(intp, ACLC2S2Q, 0x2a);
				stv0900_write_reg(intp, ACLC2S232A, aclc);
			}

		}

		if (intp->chip_id <= 0x11) {
			if (intp->demod_mode != STV0900_SINGLE)
				stv0900_activate_s2_modcod(intp, demod);

		}

		stv0900_write_reg(intp, ERRCTRL1, 0x67);
		break;
	case STV0900_UNKNOWN_STANDARD:
	default:
		dprintk("%s: found unknown standard\n", __func__);
		stv0900_write_bits(intp, DVBS1_ENABLE, 1);
		stv0900_write_bits(intp, DVBS2_ENABLE, 1);
		break;
	}

	freq1 = stv0900_read_reg(intp, CFR2);
	freq0 = stv0900_read_reg(intp, CFR1);
	rolloff = stv0900_get_bits(intp, ROLLOFF_STATUS);
	if (intp->srch_algo[demod] == STV0900_BLIND_SEARCH) {
		stv0900_write_reg(intp, SFRSTEP, 0x00);
		stv0900_write_bits(intp, SCAN_ENABLE, 0);
		stv0900_write_bits(intp, CFR_AUTOSCAN, 0);
		stv0900_write_reg(intp, TMGCFG2, 0xc1);
		stv0900_set_symbol_rate(intp, intp->mclk, srate, demod);
		blind_tun_sw = 1;
		if (intp->result[demod].standard != STV0900_DVBS2_STANDARD)
			stv0900_set_dvbs1_track_car_loop(intp, demod, srate);

	}

	if (intp->chip_id >= 0x20) {
		if ((intp->srch_standard[demod] == STV0900_SEARCH_DVBS1) ||
				(intp->srch_standard[demod] ==
							STV0900_SEARCH_DSS) ||
				(intp->srch_standard[demod] ==
							STV0900_AUTO_SEARCH)) {
			stv0900_write_reg(intp, VAVSRVIT, 0x0a);
			stv0900_write_reg(intp, VITSCALE, 0x0);
		}
	}

	if (intp->chip_id < 0x20)
		stv0900_write_reg(intp, CARHDR, 0x08);

	if (intp->chip_id == 0x10)
		stv0900_write_reg(intp, CORRELEXP, 0x0a);

	stv0900_write_reg(intp, AGC2REF, 0x38);

	if ((intp->chip_id >= 0x20) ||
			(blind_tun_sw == 1) ||
			(intp->symbol_rate[demod] < 10000000)) {
		stv0900_write_reg(intp, CFRINIT1, freq1);
		stv0900_write_reg(intp, CFRINIT0, freq0);
		intp->bw[demod] = stv0900_carrier_width(srate,
					intp->rolloff) + 10000000;

		if ((intp->chip_id >= 0x20) || (blind_tun_sw == 1)) {
			if (intp->srch_algo[demod] != STV0900_WARM_START) {
				if (intp->tuner_type[demod] == 3)
					stv0900_set_tuner_auto(intp,
							intp->freq[demod],
							intp->bw[demod],
							demod);
				else
					stv0900_set_bandwidth(fe,
							intp->bw[demod]);
			}
		}

		if ((intp->srch_algo[demod] == STV0900_BLIND_SEARCH) ||
				(intp->symbol_rate[demod] < 10000000))
			msleep(50);
		else
			msleep(5);

		stv0900_get_lock_timeout(&timed, &timef, srate,
						STV0900_WARM_START);

		if (stv0900_get_demod_lock(intp, demod, timed / 2) == FALSE) {
			stv0900_write_reg(intp, DMDISTATE, 0x1f);
			stv0900_write_reg(intp, CFRINIT1, freq1);
			stv0900_write_reg(intp, CFRINIT0, freq0);
			stv0900_write_reg(intp, DMDISTATE, 0x18);
			i = 0;
			while ((stv0900_get_demod_lock(intp,
							demod,
							timed / 2) == FALSE) &&
						(i <= 2)) {
				stv0900_write_reg(intp, DMDISTATE, 0x1f);
				stv0900_write_reg(intp, CFRINIT1, freq1);
				stv0900_write_reg(intp, CFRINIT0, freq0);
				stv0900_write_reg(intp, DMDISTATE, 0x18);
				i++;
			}
		}

	}

	if (intp->chip_id >= 0x20)
		stv0900_write_reg(intp, CARFREQ, 0x49);

	if ((intp->result[demod].standard == STV0900_DVBS1_STANDARD) ||
			(intp->result[demod].standard == STV0900_DSS_STANDARD))
		stv0900_set_viterbi_tracq(intp, demod);

}

static int stv0900_get_fec_lock(struct stv0900_internal *intp,
				enum fe_stv0900_demod_num demod, s32 time_out)
{
	s32 timer = 0, lock = 0;

	enum fe_stv0900_search_state dmd_state;

	dprintk("%s\n", __func__);

	dmd_state = stv0900_get_bits(intp, HEADER_MODE);

	while ((timer < time_out) && (lock == 0)) {
		switch (dmd_state) {
		case STV0900_SEARCH:
		case STV0900_PLH_DETECTED:
		default:
			lock = 0;
			break;
		case STV0900_DVBS2_FOUND:
			lock = stv0900_get_bits(intp, PKTDELIN_LOCK);
			break;
		case STV0900_DVBS_FOUND:
			lock = stv0900_get_bits(intp, LOCKEDVIT);
			break;
		}

		if (lock == 0) {
			msleep(10);
			timer += 10;
		}
	}

	if (lock)
		dprintk("%s: DEMOD FEC LOCK OK\n", __func__);
	else
		dprintk("%s: DEMOD FEC LOCK FAIL\n", __func__);

	return lock;
}

static int stv0900_wait_for_lock(struct stv0900_internal *intp,
				enum fe_stv0900_demod_num demod,
				s32 dmd_timeout, s32 fec_timeout)
{

	s32 timer = 0, lock = 0;

	dprintk("%s\n", __func__);

	lock = stv0900_get_demod_lock(intp, demod, dmd_timeout);

	if (lock)
		lock = lock && stv0900_get_fec_lock(intp, demod, fec_timeout);

	if (lock) {
		lock = 0;

		dprintk("%s: Timer = %d, time_out = %d\n",
				__func__, timer, fec_timeout);

		while ((timer < fec_timeout) && (lock == 0)) {
			lock = stv0900_get_bits(intp, TSFIFO_LINEOK);
			msleep(1);
			timer++;
		}
	}

	if (lock)
		dprintk("%s: DEMOD LOCK OK\n", __func__);
	else
		dprintk("%s: DEMOD LOCK FAIL\n", __func__);

	if (lock)
		return TRUE;
	else
		return FALSE;
}

enum fe_stv0900_tracking_standard stv0900_get_standard(struct dvb_frontend *fe,
						enum fe_stv0900_demod_num demod)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_tracking_standard fnd_standard;

	int hdr_mode = stv0900_get_bits(intp, HEADER_MODE);

	switch (hdr_mode) {
	case 2:
		fnd_standard = STV0900_DVBS2_STANDARD;
		break;
	case 3:
		if (stv0900_get_bits(intp, DSS_DVB) == 1)
			fnd_standard = STV0900_DSS_STANDARD;
		else
			fnd_standard = STV0900_DVBS1_STANDARD;

		break;
	default:
		fnd_standard = STV0900_UNKNOWN_STANDARD;
	}

	dprintk("%s: standard %d\n", __func__, fnd_standard);

	return fnd_standard;
}

static s32 stv0900_get_carr_freq(struct stv0900_internal *intp, u32 mclk,
					enum fe_stv0900_demod_num demod)
{
	s32	derot,
		rem1,
		rem2,
		intval1,
		intval2;

	derot = (stv0900_get_bits(intp, CAR_FREQ2) << 16) +
		(stv0900_get_bits(intp, CAR_FREQ1) << 8) +
		(stv0900_get_bits(intp, CAR_FREQ0));

	derot = ge2comp(derot, 24);
	intval1 = mclk >> 12;
	intval2 = derot >> 12;
	rem1 = mclk % 0x1000;
	rem2 = derot % 0x1000;
	derot = (intval1 * intval2) +
		((intval1 * rem2) >> 12) +
		((intval2 * rem1) >> 12);

	return derot;
}

static u32 stv0900_get_tuner_freq(struct dvb_frontend *fe)
{
	struct dvb_frontend_ops	*frontend_ops = NULL;
	struct dvb_tuner_ops *tuner_ops = NULL;
	u32 freq = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;

	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;

	if (tuner_ops->get_frequency) {
		if ((tuner_ops->get_frequency(fe, &freq)) < 0)
			dprintk("%s: Invalid parameter\n", __func__);
		else
			dprintk("%s: Frequency=%d\n", __func__, freq);

	}

	return freq;
}

static enum
fe_stv0900_signal_type stv0900_get_signal_params(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	enum fe_stv0900_signal_type range = STV0900_OUTOFRANGE;
	struct stv0900_signal_info *result = &intp->result[demod];
	s32	offsetFreq,
		srate_offset;
	int	i = 0,
		d = demod;

	u8 timing;

	msleep(5);
	if (intp->srch_algo[d] == STV0900_BLIND_SEARCH) {
		timing = stv0900_read_reg(intp, TMGREG2);
		i = 0;
		stv0900_write_reg(intp, SFRSTEP, 0x5c);

		while ((i <= 50) && (timing != 0) && (timing != 0xff)) {
			timing = stv0900_read_reg(intp, TMGREG2);
			msleep(5);
			i += 5;
		}
	}

	result->standard = stv0900_get_standard(fe, d);
	if (intp->tuner_type[demod] == 3)
		result->frequency = stv0900_get_freq_auto(intp, d);
	else
		result->frequency = stv0900_get_tuner_freq(fe);

	offsetFreq = stv0900_get_carr_freq(intp, intp->mclk, d) / 1000;
	result->frequency += offsetFreq;
	result->symbol_rate = stv0900_get_symbol_rate(intp, intp->mclk, d);
	srate_offset = stv0900_get_timing_offst(intp, result->symbol_rate, d);
	result->symbol_rate += srate_offset;
	result->fec = stv0900_get_vit_fec(intp, d);
	result->modcode = stv0900_get_bits(intp, DEMOD_MODCOD);
	result->pilot = stv0900_get_bits(intp, DEMOD_TYPE) & 0x01;
	result->frame_len = ((u32)stv0900_get_bits(intp, DEMOD_TYPE)) >> 1;
	result->rolloff = stv0900_get_bits(intp, ROLLOFF_STATUS);
	switch (result->standard) {
	case STV0900_DVBS2_STANDARD:
		result->spectrum = stv0900_get_bits(intp, SPECINV_DEMOD);
		if (result->modcode <= STV0900_QPSK_910)
			result->modulation = STV0900_QPSK;
		else if (result->modcode <= STV0900_8PSK_910)
			result->modulation = STV0900_8PSK;
		else if (result->modcode <= STV0900_16APSK_910)
			result->modulation = STV0900_16APSK;
		else if (result->modcode <= STV0900_32APSK_910)
			result->modulation = STV0900_32APSK;
		else
			result->modulation = STV0900_UNKNOWN;
		break;
	case STV0900_DVBS1_STANDARD:
	case STV0900_DSS_STANDARD:
		result->spectrum = stv0900_get_bits(intp, IQINV);
		result->modulation = STV0900_QPSK;
		break;
	default:
		break;
	}

	if ((intp->srch_algo[d] == STV0900_BLIND_SEARCH) ||
				(intp->symbol_rate[d] < 10000000)) {
		offsetFreq = result->frequency - intp->freq[d];
		if (intp->tuner_type[demod] == 3)
			intp->freq[d] = stv0900_get_freq_auto(intp, d);
		else
			intp->freq[d] = stv0900_get_tuner_freq(fe);

		if (ABS(offsetFreq) <= ((intp->srch_range[d] / 2000) + 500))
			range = STV0900_RANGEOK;
		else if (ABS(offsetFreq) <=
				(stv0900_carrier_width(result->symbol_rate,
						result->rolloff) / 2000))
			range = STV0900_RANGEOK;

	} else if (ABS(offsetFreq) <= ((intp->srch_range[d] / 2000) + 500))
		range = STV0900_RANGEOK;

	dprintk("%s: range %d\n", __func__, range);

	return range;
}

static enum
fe_stv0900_signal_type stv0900_dvbs1_acq_workaround(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	enum fe_stv0900_signal_type signal_type = STV0900_NODATA;

	s32	srate,
		demod_timeout,
		fec_timeout,
		freq1,
		freq0;

	intp->result[demod].locked = FALSE;

	if (stv0900_get_bits(intp, HEADER_MODE) == STV0900_DVBS_FOUND) {
		srate = stv0900_get_symbol_rate(intp, intp->mclk, demod);
		srate += stv0900_get_timing_offst(intp, srate, demod);
		if (intp->srch_algo[demod] == STV0900_BLIND_SEARCH)
			stv0900_set_symbol_rate(intp, intp->mclk, srate, demod);

		stv0900_get_lock_timeout(&demod_timeout, &fec_timeout,
					srate, STV0900_WARM_START);
		freq1 = stv0900_read_reg(intp, CFR2);
		freq0 = stv0900_read_reg(intp, CFR1);
		stv0900_write_bits(intp, CFR_AUTOSCAN, 0);
		stv0900_write_bits(intp, SPECINV_CONTROL,
					STV0900_IQ_FORCE_SWAPPED);
		stv0900_write_reg(intp, DMDISTATE, 0x1c);
		stv0900_write_reg(intp, CFRINIT1, freq1);
		stv0900_write_reg(intp, CFRINIT0, freq0);
		stv0900_write_reg(intp, DMDISTATE, 0x18);
		if (stv0900_wait_for_lock(intp, demod,
				demod_timeout, fec_timeout) == TRUE) {
			intp->result[demod].locked = TRUE;
			signal_type = stv0900_get_signal_params(fe);
			stv0900_track_optimization(fe);
		} else {
			stv0900_write_bits(intp, SPECINV_CONTROL,
					STV0900_IQ_FORCE_NORMAL);
			stv0900_write_reg(intp, DMDISTATE, 0x1c);
			stv0900_write_reg(intp, CFRINIT1, freq1);
			stv0900_write_reg(intp, CFRINIT0, freq0);
			stv0900_write_reg(intp, DMDISTATE, 0x18);
			if (stv0900_wait_for_lock(intp, demod,
					demod_timeout, fec_timeout) == TRUE) {
				intp->result[demod].locked = TRUE;
				signal_type = stv0900_get_signal_params(fe);
				stv0900_track_optimization(fe);
			}

		}

	} else
		intp->result[demod].locked = FALSE;

	return signal_type;
}

static u16 stv0900_blind_check_agc2_min_level(struct stv0900_internal *intp,
					enum fe_stv0900_demod_num demod)
{
	u32 minagc2level = 0xffff,
		agc2level,
		init_freq, freq_step;

	s32 i, j, nb_steps, direction;

	dprintk("%s\n", __func__);

	stv0900_write_reg(intp, AGC2REF, 0x38);
	stv0900_write_bits(intp, SCAN_ENABLE, 0);
	stv0900_write_bits(intp, CFR_AUTOSCAN, 0);

	stv0900_write_bits(intp, AUTO_GUP, 1);
	stv0900_write_bits(intp, AUTO_GLOW, 1);

	stv0900_write_reg(intp, DMDT0M, 0x0);

	stv0900_set_symbol_rate(intp, intp->mclk, 1000000, demod);
	nb_steps = -1 + (intp->srch_range[demod] / 1000000);
	nb_steps /= 2;
	nb_steps = (2 * nb_steps) + 1;

	if (nb_steps < 0)
		nb_steps = 1;

	direction = 1;

	freq_step = (1000000 << 8) / (intp->mclk >> 8);

	init_freq = 0;

	for (i = 0; i < nb_steps; i++) {
		if (direction > 0)
			init_freq = init_freq + (freq_step * i);
		else
			init_freq = init_freq - (freq_step * i);

		direction *= -1;
		stv0900_write_reg(intp, DMDISTATE, 0x5C);
		stv0900_write_reg(intp, CFRINIT1, (init_freq >> 8) & 0xff);
		stv0900_write_reg(intp, CFRINIT0, init_freq  & 0xff);
		stv0900_write_reg(intp, DMDISTATE, 0x58);
		msleep(10);
		agc2level = 0;

		for (j = 0; j < 10; j++)
			agc2level += (stv0900_read_reg(intp, AGC2I1) << 8)
					| stv0900_read_reg(intp, AGC2I0);

		agc2level /= 10;

		if (agc2level < minagc2level)
			minagc2level = agc2level;

	}

	return (u16)minagc2level;
}

static u32 stv0900_search_srate_coarse(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	int timing_lck = FALSE;
	s32 i, timingcpt = 0,
		direction = 1,
		nb_steps,
		current_step = 0,
		tuner_freq;
	u32 agc2_th,
		coarse_srate = 0,
		agc2_integr = 0,
		currier_step = 1200;

	if (intp->chip_id >= 0x30)
		agc2_th = 0x2e00;
	else
		agc2_th = 0x1f00;

	stv0900_write_bits(intp, DEMOD_MODE, 0x1f);
	stv0900_write_reg(intp, TMGCFG, 0x12);
	stv0900_write_reg(intp, TMGTHRISE, 0xf0);
	stv0900_write_reg(intp, TMGTHFALL, 0xe0);
	stv0900_write_bits(intp, SCAN_ENABLE, 1);
	stv0900_write_bits(intp, CFR_AUTOSCAN, 1);
	stv0900_write_reg(intp, SFRUP1, 0x83);
	stv0900_write_reg(intp, SFRUP0, 0xc0);
	stv0900_write_reg(intp, SFRLOW1, 0x82);
	stv0900_write_reg(intp, SFRLOW0, 0xa0);
	stv0900_write_reg(intp, DMDT0M, 0x0);
	stv0900_write_reg(intp, AGC2REF, 0x50);

	if (intp->chip_id >= 0x30) {
		stv0900_write_reg(intp, CARFREQ, 0x99);
		stv0900_write_reg(intp, SFRSTEP, 0x98);
	} else if (intp->chip_id >= 0x20) {
		stv0900_write_reg(intp, CARFREQ, 0x6a);
		stv0900_write_reg(intp, SFRSTEP, 0x95);
	} else {
		stv0900_write_reg(intp, CARFREQ, 0xed);
		stv0900_write_reg(intp, SFRSTEP, 0x73);
	}

	if (intp->symbol_rate[demod] <= 2000000)
		currier_step = 1000;
	else if (intp->symbol_rate[demod] <= 5000000)
		currier_step = 2000;
	else if (intp->symbol_rate[demod] <= 12000000)
		currier_step = 3000;
	else
			currier_step = 5000;

	nb_steps = -1 + ((intp->srch_range[demod] / 1000) / currier_step);
	nb_steps /= 2;
	nb_steps = (2 * nb_steps) + 1;

	if (nb_steps < 0)
		nb_steps = 1;
	else if (nb_steps > 10) {
		nb_steps = 11;
		currier_step = (intp->srch_range[demod] / 1000) / 10;
	}

	current_step = 0;
	direction = 1;

	tuner_freq = intp->freq[demod];

	while ((timing_lck == FALSE) && (current_step < nb_steps)) {
		stv0900_write_reg(intp, DMDISTATE, 0x5f);
		stv0900_write_bits(intp, DEMOD_MODE, 0);

		msleep(50);

		for (i = 0; i < 10; i++) {
			if (stv0900_get_bits(intp, TMGLOCK_QUALITY) >= 2)
				timingcpt++;

			agc2_integr += (stv0900_read_reg(intp, AGC2I1) << 8) |
					stv0900_read_reg(intp, AGC2I0);
		}

		agc2_integr /= 10;
		coarse_srate = stv0900_get_symbol_rate(intp, intp->mclk, demod);
		current_step++;
		direction *= -1;

		dprintk("lock: I2C_DEMOD_MODE_FIELD =0. Search started."
			" tuner freq=%d agc2=0x%x srate_coarse=%d tmg_cpt=%d\n",
			tuner_freq, agc2_integr, coarse_srate, timingcpt);

		if ((timingcpt >= 5) &&
				(agc2_integr < agc2_th) &&
				(coarse_srate < 55000000) &&
				(coarse_srate > 850000))
			timing_lck = TRUE;
		else if (current_step < nb_steps) {
			if (direction > 0)
				tuner_freq += (current_step * currier_step);
			else
				tuner_freq -= (current_step * currier_step);

			if (intp->tuner_type[demod] == 3)
				stv0900_set_tuner_auto(intp, tuner_freq,
						intp->bw[demod], demod);
			else
				stv0900_set_tuner(fe, tuner_freq,
						intp->bw[demod]);
		}
	}

	if (timing_lck == FALSE)
		coarse_srate = 0;
	else
		coarse_srate = stv0900_get_symbol_rate(intp, intp->mclk, demod);

	return coarse_srate;
}

static u32 stv0900_search_srate_fine(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	u32	coarse_srate,
		coarse_freq,
		symb,
		symbmax,
		symbmin,
		symbcomp;

	coarse_srate = stv0900_get_symbol_rate(intp, intp->mclk, demod);

	if (coarse_srate > 3000000) {
		symbmax = 13 * (coarse_srate / 10);
		symbmax = (symbmax / 1000) * 65536;
		symbmax /= (intp->mclk / 1000);

		symbmin = 10 * (coarse_srate / 13);
		symbmin = (symbmin / 1000)*65536;
		symbmin /= (intp->mclk / 1000);

		symb = (coarse_srate / 1000) * 65536;
		symb /= (intp->mclk / 1000);
	} else {
		symbmax = 13 * (coarse_srate / 10);
		symbmax = (symbmax / 100) * 65536;
		symbmax /= (intp->mclk / 100);

		symbmin = 10 * (coarse_srate / 14);
		symbmin = (symbmin / 100) * 65536;
		symbmin /= (intp->mclk / 100);

		symb = (coarse_srate / 100) * 65536;
		symb /= (intp->mclk / 100);
	}

	symbcomp = 13 * (coarse_srate / 10);
		coarse_freq = (stv0900_read_reg(intp, CFR2) << 8)
					| stv0900_read_reg(intp, CFR1);

	if (symbcomp < intp->symbol_rate[demod])
		coarse_srate = 0;
	else {
		stv0900_write_reg(intp, DMDISTATE, 0x1f);
		stv0900_write_reg(intp, TMGCFG2, 0xc1);
		stv0900_write_reg(intp, TMGTHRISE, 0x20);
		stv0900_write_reg(intp, TMGTHFALL, 0x00);
		stv0900_write_reg(intp, TMGCFG, 0xd2);
		stv0900_write_bits(intp, CFR_AUTOSCAN, 0);
		stv0900_write_reg(intp, AGC2REF, 0x38);

		if (intp->chip_id >= 0x30)
			stv0900_write_reg(intp, CARFREQ, 0x79);
		else if (intp->chip_id >= 0x20)
			stv0900_write_reg(intp, CARFREQ, 0x49);
		else
			stv0900_write_reg(intp, CARFREQ, 0xed);

		stv0900_write_reg(intp, SFRUP1, (symbmax >> 8) & 0x7f);
		stv0900_write_reg(intp, SFRUP0, (symbmax & 0xff));

		stv0900_write_reg(intp, SFRLOW1, (symbmin >> 8) & 0x7f);
		stv0900_write_reg(intp, SFRLOW0, (symbmin & 0xff));

		stv0900_write_reg(intp, SFRINIT1, (symb >> 8) & 0xff);
		stv0900_write_reg(intp, SFRINIT0, (symb & 0xff));

		stv0900_write_reg(intp, DMDT0M, 0x20);
		stv0900_write_reg(intp, CFRINIT1, (coarse_freq >> 8) & 0xff);
		stv0900_write_reg(intp, CFRINIT0, coarse_freq  & 0xff);
		stv0900_write_reg(intp, DMDISTATE, 0x15);
	}

	return coarse_srate;
}

static int stv0900_blind_search_algo(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	u8	k_ref_tmg,
		k_ref_tmg_max,
		k_ref_tmg_min;
	u32	coarse_srate,
		agc2_th;
	int	lock = FALSE,
		coarse_fail = FALSE;
	s32	demod_timeout = 500,
		fec_timeout = 50,
		fail_cpt,
		i,
		agc2_overflow;
	u16	agc2_int;
	u8	dstatus2;

	dprintk("%s\n", __func__);

	if (intp->chip_id < 0x20) {
		k_ref_tmg_max = 233;
		k_ref_tmg_min = 143;
	} else {
		k_ref_tmg_max = 110;
		k_ref_tmg_min = 10;
	}

	if (intp->chip_id <= 0x20)
		agc2_th = STV0900_BLIND_SEARCH_AGC2_TH;
	else
		agc2_th = STV0900_BLIND_SEARCH_AGC2_TH_CUT30;

	agc2_int = stv0900_blind_check_agc2_min_level(intp, demod);

	if (agc2_int > STV0900_BLIND_SEARCH_AGC2_TH)
		return FALSE;

	if (intp->chip_id == 0x10)
		stv0900_write_reg(intp, CORRELEXP, 0xaa);

	if (intp->chip_id < 0x20)
		stv0900_write_reg(intp, CARHDR, 0x55);
	else
		stv0900_write_reg(intp, CARHDR, 0x20);

	if (intp->chip_id <= 0x20)
		stv0900_write_reg(intp, CARCFG, 0xc4);
	else
		stv0900_write_reg(intp, CARCFG, 0x6);

	stv0900_write_reg(intp, RTCS2, 0x44);

	if (intp->chip_id >= 0x20) {
		stv0900_write_reg(intp, EQUALCFG, 0x41);
		stv0900_write_reg(intp, FFECFG, 0x41);
		stv0900_write_reg(intp, VITSCALE, 0x82);
		stv0900_write_reg(intp, VAVSRVIT, 0x0);
	}

	k_ref_tmg = k_ref_tmg_max;

	do {
		stv0900_write_reg(intp, KREFTMG, k_ref_tmg);
		if (stv0900_search_srate_coarse(fe) != 0) {
			coarse_srate = stv0900_search_srate_fine(fe);

			if (coarse_srate != 0) {
				stv0900_get_lock_timeout(&demod_timeout,
							&fec_timeout,
							coarse_srate,
							STV0900_BLIND_SEARCH);
				lock = stv0900_get_demod_lock(intp,
							demod,
							demod_timeout);
			} else
				lock = FALSE;
		} else {
			fail_cpt = 0;
			agc2_overflow = 0;

			for (i = 0; i < 10; i++) {
				agc2_int = (stv0900_read_reg(intp, AGC2I1) << 8)
					| stv0900_read_reg(intp, AGC2I0);

				if (agc2_int >= 0xff00)
					agc2_overflow++;

				dstatus2 = stv0900_read_reg(intp, DSTATUS2);

				if (((dstatus2 & 0x1) == 0x1) &&
						((dstatus2 >> 7) == 1))
					fail_cpt++;
			}

			if ((fail_cpt > 7) || (agc2_overflow > 7))
				coarse_fail = TRUE;

			lock = FALSE;
		}
		k_ref_tmg -= 30;
	} while ((k_ref_tmg >= k_ref_tmg_min) &&
				(lock == FALSE) &&
				(coarse_fail == FALSE));

	return lock;
}

static void stv0900_set_viterbi_acq(struct stv0900_internal *intp,
					enum fe_stv0900_demod_num demod)
{
	s32 vth_reg = VTH12;

	dprintk("%s\n", __func__);

	stv0900_write_reg(intp, vth_reg++, 0x96);
	stv0900_write_reg(intp, vth_reg++, 0x64);
	stv0900_write_reg(intp, vth_reg++, 0x36);
	stv0900_write_reg(intp, vth_reg++, 0x23);
	stv0900_write_reg(intp, vth_reg++, 0x1e);
	stv0900_write_reg(intp, vth_reg++, 0x19);
}

static void stv0900_set_search_standard(struct stv0900_internal *intp,
					enum fe_stv0900_demod_num demod)
{

	dprintk("%s\n", __func__);

	switch (intp->srch_standard[demod]) {
	case STV0900_SEARCH_DVBS1:
		dprintk("Search Standard = DVBS1\n");
		break;
	case STV0900_SEARCH_DSS:
		dprintk("Search Standard = DSS\n");
	case STV0900_SEARCH_DVBS2:
		break;
		dprintk("Search Standard = DVBS2\n");
	case STV0900_AUTO_SEARCH:
	default:
		dprintk("Search Standard = AUTO\n");
		break;
	}

	switch (intp->srch_standard[demod]) {
	case STV0900_SEARCH_DVBS1:
	case STV0900_SEARCH_DSS:
		stv0900_write_bits(intp, DVBS1_ENABLE, 1);
		stv0900_write_bits(intp, DVBS2_ENABLE, 0);
		stv0900_write_bits(intp, STOP_CLKVIT, 0);
		stv0900_set_dvbs1_track_car_loop(intp,
						demod,
						intp->symbol_rate[demod]);
		stv0900_write_reg(intp, CAR2CFG, 0x22);

		stv0900_set_viterbi_acq(intp, demod);
		stv0900_set_viterbi_standard(intp,
					intp->srch_standard[demod],
					intp->fec[demod], demod);

		break;
	case STV0900_SEARCH_DVBS2:
		stv0900_write_bits(intp, DVBS1_ENABLE, 0);
		stv0900_write_bits(intp, DVBS2_ENABLE, 1);
		stv0900_write_bits(intp, STOP_CLKVIT, 1);
		stv0900_write_reg(intp, ACLC, 0x1a);
		stv0900_write_reg(intp, BCLC, 0x09);
		if (intp->chip_id <= 0x20) /*cut 1.x and 2.0*/
			stv0900_write_reg(intp, CAR2CFG, 0x26);
		else
			stv0900_write_reg(intp, CAR2CFG, 0x66);

		if (intp->demod_mode != STV0900_SINGLE) {
			if (intp->chip_id <= 0x11)
				stv0900_stop_all_s2_modcod(intp, demod);
			else
				stv0900_activate_s2_modcod(intp, demod);

		} else
			stv0900_activate_s2_modcod_single(intp, demod);

		stv0900_set_viterbi_tracq(intp, demod);

		break;
	case STV0900_AUTO_SEARCH:
	default:
		stv0900_write_bits(intp, DVBS1_ENABLE, 1);
		stv0900_write_bits(intp, DVBS2_ENABLE, 1);
		stv0900_write_bits(intp, STOP_CLKVIT, 0);
		stv0900_write_reg(intp, ACLC, 0x1a);
		stv0900_write_reg(intp, BCLC, 0x09);
		stv0900_set_dvbs1_track_car_loop(intp,
						demod,
						intp->symbol_rate[demod]);
		if (intp->chip_id <= 0x20) /*cut 1.x and 2.0*/
			stv0900_write_reg(intp, CAR2CFG, 0x26);
		else
			stv0900_write_reg(intp, CAR2CFG, 0x66);

		if (intp->demod_mode != STV0900_SINGLE) {
			if (intp->chip_id <= 0x11)
				stv0900_stop_all_s2_modcod(intp, demod);
			else
				stv0900_activate_s2_modcod(intp, demod);

		} else
			stv0900_activate_s2_modcod_single(intp, demod);

		stv0900_set_viterbi_tracq(intp, demod);
		stv0900_set_viterbi_standard(intp,
						intp->srch_standard[demod],
						intp->fec[demod], demod);

		break;
	}
}

enum fe_stv0900_signal_type stv0900_algo(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	s32 demod_timeout = 500, fec_timeout = 50;
	s32 aq_power, agc1_power, i;

	int lock = FALSE, low_sr = FALSE;

	enum fe_stv0900_signal_type signal_type = STV0900_NOCARRIER;
	enum fe_stv0900_search_algo algo;
	int no_signal = FALSE;

	dprintk("%s\n", __func__);

	algo = intp->srch_algo[demod];
	stv0900_write_bits(intp, RST_HWARE, 1);
	stv0900_write_reg(intp, DMDISTATE, 0x5c);
	if (intp->chip_id >= 0x20) {
		if (intp->symbol_rate[demod] > 5000000)
			stv0900_write_reg(intp, CORRELABS, 0x9e);
		else
			stv0900_write_reg(intp, CORRELABS, 0x82);
	} else
		stv0900_write_reg(intp, CORRELABS, 0x88);

	stv0900_get_lock_timeout(&demod_timeout, &fec_timeout,
				intp->symbol_rate[demod],
				intp->srch_algo[demod]);

	if (intp->srch_algo[demod] == STV0900_BLIND_SEARCH) {
		intp->bw[demod] = 2 * 36000000;

		stv0900_write_reg(intp, TMGCFG2, 0xc0);
		stv0900_write_reg(intp, CORRELMANT, 0x70);

		stv0900_set_symbol_rate(intp, intp->mclk, 1000000, demod);
	} else {
		stv0900_write_reg(intp, DMDT0M, 0x20);
		stv0900_write_reg(intp, TMGCFG, 0xd2);

		if (intp->symbol_rate[demod] < 2000000)
			stv0900_write_reg(intp, CORRELMANT, 0x63);
		else
			stv0900_write_reg(intp, CORRELMANT, 0x70);

		stv0900_write_reg(intp, AGC2REF, 0x38);

		intp->bw[demod] =
				stv0900_carrier_width(intp->symbol_rate[demod],
								intp->rolloff);
		if (intp->chip_id >= 0x20) {
			stv0900_write_reg(intp, KREFTMG, 0x5a);

			if (intp->srch_algo[demod] == STV0900_COLD_START) {
				intp->bw[demod] += 10000000;
				intp->bw[demod] *= 15;
				intp->bw[demod] /= 10;
			} else if (intp->srch_algo[demod] == STV0900_WARM_START)
				intp->bw[demod] += 10000000;

		} else {
			stv0900_write_reg(intp, KREFTMG, 0xc1);
			intp->bw[demod] += 10000000;
			intp->bw[demod] *= 15;
			intp->bw[demod] /= 10;
		}

		stv0900_write_reg(intp, TMGCFG2, 0xc1);

		stv0900_set_symbol_rate(intp, intp->mclk,
					intp->symbol_rate[demod], demod);
		stv0900_set_max_symbol_rate(intp, intp->mclk,
					intp->symbol_rate[demod], demod);
		stv0900_set_min_symbol_rate(intp, intp->mclk,
					intp->symbol_rate[demod], demod);
		if (intp->symbol_rate[demod] >= 10000000)
			low_sr = FALSE;
		else
			low_sr = TRUE;

	}

	if (intp->tuner_type[demod] == 3)
		stv0900_set_tuner_auto(intp, intp->freq[demod],
				intp->bw[demod], demod);
	else
		stv0900_set_tuner(fe, intp->freq[demod], intp->bw[demod]);

	agc1_power = MAKEWORD(stv0900_get_bits(intp, AGCIQ_VALUE1),
				stv0900_get_bits(intp, AGCIQ_VALUE0));

	aq_power = 0;

	if (agc1_power == 0) {
		for (i = 0; i < 5; i++)
			aq_power += (stv0900_get_bits(intp, POWER_I) +
					stv0900_get_bits(intp, POWER_Q)) / 2;

		aq_power /= 5;
	}

	if ((agc1_power == 0) && (aq_power < IQPOWER_THRESHOLD)) {
		intp->result[demod].locked = FALSE;
		signal_type = STV0900_NOAGC1;
		dprintk("%s: NO AGC1, POWERI, POWERQ\n", __func__);
	} else {
		stv0900_write_bits(intp, SPECINV_CONTROL,
					intp->srch_iq_inv[demod]);
		if (intp->chip_id <= 0x20) /*cut 2.0*/
			stv0900_write_bits(intp, MANUALSX_ROLLOFF, 1);
		else /*cut 3.0*/
			stv0900_write_bits(intp, MANUALS2_ROLLOFF, 1);

		stv0900_set_search_standard(intp, demod);

		if (intp->srch_algo[demod] != STV0900_BLIND_SEARCH)
			stv0900_start_search(intp, demod);
	}

	if (signal_type == STV0900_NOAGC1)
		return signal_type;

	if (intp->chip_id == 0x12) {
		stv0900_write_bits(intp, RST_HWARE, 0);
		msleep(3);
		stv0900_write_bits(intp, RST_HWARE, 1);
		stv0900_write_bits(intp, RST_HWARE, 0);
	}

	if (algo == STV0900_BLIND_SEARCH)
		lock = stv0900_blind_search_algo(fe);
	else if (algo == STV0900_COLD_START)
		lock = stv0900_get_demod_cold_lock(fe, demod_timeout);
	else if (algo == STV0900_WARM_START)
		lock = stv0900_get_demod_lock(intp, demod, demod_timeout);

	if ((lock == FALSE) && (algo == STV0900_COLD_START)) {
		if (low_sr == FALSE) {
			if (stv0900_check_timing_lock(intp, demod) == TRUE)
				lock = stv0900_sw_algo(intp, demod);
		}
	}

	if (lock == TRUE)
		signal_type = stv0900_get_signal_params(fe);

	if ((lock == TRUE) && (signal_type == STV0900_RANGEOK)) {
		stv0900_track_optimization(fe);
		if (intp->chip_id <= 0x11) {
			if ((stv0900_get_standard(fe, 0) ==
						STV0900_DVBS1_STANDARD) &&
			   (stv0900_get_standard(fe, 1) ==
						STV0900_DVBS1_STANDARD)) {
				msleep(20);
				stv0900_write_bits(intp, RST_HWARE, 0);
			} else {
				stv0900_write_bits(intp, RST_HWARE, 0);
				msleep(3);
				stv0900_write_bits(intp, RST_HWARE, 1);
				stv0900_write_bits(intp, RST_HWARE, 0);
			}

		} else if (intp->chip_id >= 0x20) {
			stv0900_write_bits(intp, RST_HWARE, 0);
			msleep(3);
			stv0900_write_bits(intp, RST_HWARE, 1);
			stv0900_write_bits(intp, RST_HWARE, 0);
		}

		if (stv0900_wait_for_lock(intp, demod,
					fec_timeout, fec_timeout) == TRUE) {
			lock = TRUE;
			intp->result[demod].locked = TRUE;
			if (intp->result[demod].standard ==
						STV0900_DVBS2_STANDARD) {
				stv0900_set_dvbs2_rolloff(intp, demod);
				stv0900_write_bits(intp, RESET_UPKO_COUNT, 1);
				stv0900_write_bits(intp, RESET_UPKO_COUNT, 0);
				stv0900_write_reg(intp, ERRCTRL1, 0x67);
			} else {
				stv0900_write_reg(intp, ERRCTRL1, 0x75);
			}

			stv0900_write_reg(intp, FBERCPT4, 0);
			stv0900_write_reg(intp, ERRCTRL2, 0xc1);
		} else {
			lock = FALSE;
			signal_type = STV0900_NODATA;
			no_signal = stv0900_check_signal_presence(intp, demod);

				intp->result[demod].locked = FALSE;
		}
	}

	if ((signal_type != STV0900_NODATA) || (no_signal != FALSE))
		return signal_type;

	if (intp->chip_id > 0x11) {
		intp->result[demod].locked = FALSE;
		return signal_type;
	}

	if ((stv0900_get_bits(intp, HEADER_MODE) == STV0900_DVBS_FOUND) &&
	   (intp->srch_iq_inv[demod] <= STV0900_IQ_AUTO_NORMAL_FIRST))
		signal_type = stv0900_dvbs1_acq_workaround(fe);

	return signal_type;
}

