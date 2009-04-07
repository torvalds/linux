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

int stv0900_check_signal_presence(struct stv0900_internal *i_params,
					enum fe_stv0900_demod_num demod)
{
	s32 carr_offset,
	agc2_integr,
	max_carrier;

	int no_signal;

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		carr_offset = (stv0900_read_reg(i_params, R0900_P1_CFR2) << 8)
						| stv0900_read_reg(i_params,
						R0900_P1_CFR1);
		carr_offset = ge2comp(carr_offset, 16);
		agc2_integr = (stv0900_read_reg(i_params, R0900_P1_AGC2I1) << 8)
						| stv0900_read_reg(i_params,
						R0900_P1_AGC2I0);
		max_carrier = i_params->dmd1_srch_range / 1000;
		break;
	case STV0900_DEMOD_2:
		carr_offset = (stv0900_read_reg(i_params, R0900_P2_CFR2) << 8)
						| stv0900_read_reg(i_params,
						R0900_P2_CFR1);
		carr_offset = ge2comp(carr_offset, 16);
		agc2_integr = (stv0900_read_reg(i_params, R0900_P2_AGC2I1) << 8)
						| stv0900_read_reg(i_params,
						R0900_P2_AGC2I0);
		max_carrier = i_params->dmd2_srch_range / 1000;
		break;
	}

	max_carrier += (max_carrier / 10);
	max_carrier = 65536 * (max_carrier / 2);
	max_carrier /= i_params->mclk / 1000;
	if (max_carrier > 0x4000)
		max_carrier = 0x4000;

	if ((agc2_integr > 0x2000)
			|| (carr_offset > + 2*max_carrier)
			|| (carr_offset < -2*max_carrier))
		no_signal = TRUE;
	else
		no_signal = FALSE;

	return no_signal;
}

static void stv0900_get_sw_loop_params(struct stv0900_internal *i_params,
				s32 *frequency_inc, s32 *sw_timeout,
				s32 *steps,
				enum fe_stv0900_demod_num demod)
{
	s32 timeout, freq_inc, max_steps, srate, max_carrier;

	enum fe_stv0900_search_standard	standard;

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		srate = i_params->dmd1_symbol_rate;
		max_carrier = i_params->dmd1_srch_range / 1000;
		max_carrier += max_carrier / 10;
		standard = i_params->dmd1_srch_standard;
		break;
	case STV0900_DEMOD_2:
		srate = i_params->dmd2_symbol_rate;
		max_carrier = i_params->dmd2_srch_range / 1000;
		max_carrier += max_carrier / 10;
		standard = i_params->dmd2_srch_stndrd;
		break;
	}

	max_carrier = 65536 * (max_carrier / 2);
	max_carrier /= i_params->mclk / 1000;

	if (max_carrier > 0x4000)
		max_carrier = 0x4000;

	freq_inc = srate;
	freq_inc /= i_params->mclk >> 10;
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

static int stv0900_search_carr_sw_loop(struct stv0900_internal *i_params,
				s32 FreqIncr, s32 Timeout, int zigzag,
				s32 MaxStep, enum fe_stv0900_demod_num demod)
{
	int	no_signal,
		lock = FALSE;
	s32	stepCpt,
		freqOffset,
		max_carrier;

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		max_carrier = i_params->dmd1_srch_range / 1000;
		max_carrier += (max_carrier / 10);
		break;
	case STV0900_DEMOD_2:
		max_carrier = i_params->dmd2_srch_range / 1000;
		max_carrier += (max_carrier / 10);
		break;
	}

	max_carrier = 65536 * (max_carrier / 2);
	max_carrier /= i_params->mclk / 1000;

	if (max_carrier > 0x4000)
		max_carrier = 0x4000;

	if (zigzag == TRUE)
		freqOffset = 0;
	else
		freqOffset = -max_carrier + FreqIncr;

	stepCpt = 0;

	do {
		switch (demod) {
		case STV0900_DEMOD_1:
		default:
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1C);
			stv0900_write_reg(i_params, R0900_P1_CFRINIT1,
					(freqOffset / 256) & 0xFF);
			stv0900_write_reg(i_params, R0900_P1_CFRINIT0,
					 freqOffset & 0xFF);
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x18);
			stv0900_write_bits(i_params, F0900_P1_ALGOSWRST, 1);

			if (i_params->chip_id == 0x12) {
				stv0900_write_bits(i_params,
						F0900_P1_RST_HWARE, 1);
				stv0900_write_bits(i_params,
						F0900_P1_RST_HWARE, 0);
			}
			break;
		case STV0900_DEMOD_2:
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1C);
			stv0900_write_reg(i_params, R0900_P2_CFRINIT1,
					(freqOffset / 256) & 0xFF);
			stv0900_write_reg(i_params, R0900_P2_CFRINIT0,
					freqOffset & 0xFF);
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x18);
			stv0900_write_bits(i_params, F0900_P2_ALGOSWRST, 1);

			if (i_params->chip_id == 0x12) {
				stv0900_write_bits(i_params,
						F0900_P2_RST_HWARE, 1);
				stv0900_write_bits(i_params,
						F0900_P2_RST_HWARE, 0);
			}
			break;
		}

		if (zigzag == TRUE) {
			if (freqOffset >= 0)
				freqOffset = -freqOffset - 2 * FreqIncr;
			else
				freqOffset = -freqOffset;
		} else
			freqOffset += + 2 * FreqIncr;

		stepCpt++;
		lock = stv0900_get_demod_lock(i_params, demod, Timeout);
		no_signal = stv0900_check_signal_presence(i_params, demod);

	} while ((lock == FALSE)
			&& (no_signal == FALSE)
			&& ((freqOffset - FreqIncr) <  max_carrier)
			&& ((freqOffset + FreqIncr) > -max_carrier)
			&& (stepCpt < MaxStep));

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		stv0900_write_bits(i_params, F0900_P1_ALGOSWRST, 0);
		break;
	case STV0900_DEMOD_2:
		stv0900_write_bits(i_params, F0900_P2_ALGOSWRST, 0);
		break;
	}

	return lock;
}

int stv0900_sw_algo(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod)
{
	int lock = FALSE;

	int no_signal,
	zigzag;
	s32 dvbs2_fly_wheel;

	s32 freqIncrement, softStepTimeout, trialCounter, max_steps;

	stv0900_get_sw_loop_params(i_params, &freqIncrement, &softStepTimeout,
					&max_steps, demod);
	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		switch (i_params->dmd1_srch_standard) {
		case STV0900_SEARCH_DVBS1:
		case STV0900_SEARCH_DSS:
			if (i_params->chip_id >= 0x20)
				stv0900_write_reg(i_params, R0900_P1_CARFREQ,
						0x3B);
			else
				stv0900_write_reg(i_params, R0900_P1_CARFREQ,
						0xef);

			stv0900_write_reg(i_params, R0900_P1_DMDCFGMD, 0x49);
			zigzag = FALSE;
			break;
		case STV0900_SEARCH_DVBS2:
			if (i_params->chip_id >= 0x20)
				stv0900_write_reg(i_params, R0900_P1_CORRELABS,
						0x79);
			else
				stv0900_write_reg(i_params, R0900_P1_CORRELABS,
						0x68);

			stv0900_write_reg(i_params, R0900_P1_DMDCFGMD,
						0x89);

			zigzag = TRUE;
			break;
		case STV0900_AUTO_SEARCH:
		default:
			if (i_params->chip_id >= 0x20) {
				stv0900_write_reg(i_params, R0900_P1_CARFREQ,
							0x3B);
				stv0900_write_reg(i_params, R0900_P1_CORRELABS,
							0x79);
			} else {
				stv0900_write_reg(i_params, R0900_P1_CARFREQ,
							0xef);
				stv0900_write_reg(i_params, R0900_P1_CORRELABS,
							0x68);
			}

			stv0900_write_reg(i_params, R0900_P1_DMDCFGMD,
							0xc9);
			zigzag = FALSE;
			break;
		}

		trialCounter = 0;
		do {
			lock = stv0900_search_carr_sw_loop(i_params,
							freqIncrement,
							softStepTimeout,
							zigzag,
							max_steps,
							demod);
			no_signal = stv0900_check_signal_presence(i_params,
								demod);
			trialCounter++;
			if ((lock == TRUE)
					|| (no_signal == TRUE)
					|| (trialCounter == 2)) {

				if (i_params->chip_id >= 0x20) {
					stv0900_write_reg(i_params,
							R0900_P1_CARFREQ,
							0x49);
					stv0900_write_reg(i_params,
							R0900_P1_CORRELABS,
							0x9e);
				} else {
					stv0900_write_reg(i_params,
							R0900_P1_CARFREQ,
							0xed);
					stv0900_write_reg(i_params,
							R0900_P1_CORRELABS,
							0x88);
				}

				if ((lock == TRUE) && (stv0900_get_bits(i_params, F0900_P1_HEADER_MODE) == STV0900_DVBS2_FOUND)) {
					msleep(softStepTimeout);
					dvbs2_fly_wheel = stv0900_get_bits(i_params, F0900_P1_FLYWHEEL_CPT);

					if (dvbs2_fly_wheel < 0xd) {
						msleep(softStepTimeout);
						dvbs2_fly_wheel = stv0900_get_bits(i_params, F0900_P1_FLYWHEEL_CPT);
					}

					if (dvbs2_fly_wheel < 0xd) {
						lock = FALSE;

						if (trialCounter < 2) {
							if (i_params->chip_id >= 0x20)
								stv0900_write_reg(i_params, R0900_P1_CORRELABS, 0x79);
							else
								stv0900_write_reg(i_params, R0900_P1_CORRELABS, 0x68);

							stv0900_write_reg(i_params, R0900_P1_DMDCFGMD, 0x89);
						}
					}
				}
			}

		} while ((lock == FALSE)
			&& (trialCounter < 2)
			&& (no_signal == FALSE));

		break;
	case STV0900_DEMOD_2:
		switch (i_params->dmd2_srch_stndrd) {
		case STV0900_SEARCH_DVBS1:
		case STV0900_SEARCH_DSS:
			if (i_params->chip_id >= 0x20)
				stv0900_write_reg(i_params, R0900_P2_CARFREQ,
						0x3b);
			else
				stv0900_write_reg(i_params, R0900_P2_CARFREQ,
						0xef);

			stv0900_write_reg(i_params, R0900_P2_DMDCFGMD,
						0x49);
			zigzag = FALSE;
			break;
		case STV0900_SEARCH_DVBS2:
			if (i_params->chip_id >= 0x20)
				stv0900_write_reg(i_params, R0900_P2_CORRELABS,
						0x79);
			else
				stv0900_write_reg(i_params, R0900_P2_CORRELABS,
						0x68);

			stv0900_write_reg(i_params, R0900_P2_DMDCFGMD, 0x89);
			zigzag = TRUE;
			break;
		case STV0900_AUTO_SEARCH:
		default:
			if (i_params->chip_id >= 0x20) {
				stv0900_write_reg(i_params, R0900_P2_CARFREQ,
						0x3b);
				stv0900_write_reg(i_params, R0900_P2_CORRELABS,
						0x79);
			} else {
				stv0900_write_reg(i_params, R0900_P2_CARFREQ,
						0xef);
				stv0900_write_reg(i_params, R0900_P2_CORRELABS,
						0x68);
			}

			stv0900_write_reg(i_params, R0900_P2_DMDCFGMD, 0xc9);

			zigzag = FALSE;
			break;
		}

		trialCounter = 0;

		do {
			lock = stv0900_search_carr_sw_loop(i_params,
							freqIncrement,
							softStepTimeout,
							zigzag,
							max_steps,
							demod);
			no_signal = stv0900_check_signal_presence(i_params,
								demod);
			trialCounter++;
			if ((lock == TRUE)
					|| (no_signal == TRUE)
					|| (trialCounter == 2)) {
				if (i_params->chip_id >= 0x20) {
					stv0900_write_reg(i_params,
							R0900_P2_CARFREQ,
							0x49);
					stv0900_write_reg(i_params,
							R0900_P2_CORRELABS,
							0x9e);
				} else {
					stv0900_write_reg(i_params,
							R0900_P2_CARFREQ,
							0xed);
					stv0900_write_reg(i_params,
							R0900_P2_CORRELABS,
							0x88);
				}

				if ((lock == TRUE) && (stv0900_get_bits(i_params, F0900_P2_HEADER_MODE) == STV0900_DVBS2_FOUND)) {
					msleep(softStepTimeout);
					dvbs2_fly_wheel = stv0900_get_bits(i_params, F0900_P2_FLYWHEEL_CPT);
					if (dvbs2_fly_wheel < 0xd) {
						msleep(softStepTimeout);
						dvbs2_fly_wheel = stv0900_get_bits(i_params, F0900_P2_FLYWHEEL_CPT);
					}

					if (dvbs2_fly_wheel < 0xd) {
						lock = FALSE;
						if (trialCounter < 2) {
							if (i_params->chip_id >= 0x20)
								stv0900_write_reg(i_params, R0900_P2_CORRELABS, 0x79);
							else
								stv0900_write_reg(i_params, R0900_P2_CORRELABS, 0x68);

							stv0900_write_reg(i_params, R0900_P2_DMDCFGMD, 0x89);
						}
					}
				}
			}

		} while ((lock == FALSE) && (trialCounter < 2) && (no_signal == FALSE));

		break;
	}

	return lock;
}

static u32 stv0900_get_symbol_rate(struct stv0900_internal *i_params,
					u32 mclk,
					enum fe_stv0900_demod_num demod)
{
	s32 sfr_field3, sfr_field2, sfr_field1, sfr_field0,
	rem1, rem2, intval1, intval2, srate;

	dmd_reg(sfr_field3, F0900_P1_SYMB_FREQ3, F0900_P2_SYMB_FREQ3);
	dmd_reg(sfr_field2, F0900_P1_SYMB_FREQ2, F0900_P2_SYMB_FREQ2);
	dmd_reg(sfr_field1, F0900_P1_SYMB_FREQ1, F0900_P2_SYMB_FREQ1);
	dmd_reg(sfr_field0, F0900_P1_SYMB_FREQ0, F0900_P2_SYMB_FREQ0);

	srate = (stv0900_get_bits(i_params, sfr_field3) << 24) +
		(stv0900_get_bits(i_params, sfr_field2) << 16) +
		(stv0900_get_bits(i_params, sfr_field1) << 8) +
		(stv0900_get_bits(i_params, sfr_field0));
	dprintk("lock: srate=%d r0=0x%x r1=0x%x r2=0x%x r3=0x%x \n",
		srate, stv0900_get_bits(i_params, sfr_field0),
		stv0900_get_bits(i_params, sfr_field1),
		stv0900_get_bits(i_params, sfr_field2),
		stv0900_get_bits(i_params, sfr_field3));

	intval1 = (mclk) >> 16;
	intval2 = (srate) >> 16;

	rem1 = (mclk) % 0x10000;
	rem2 = (srate) % 0x10000;
	srate =	(intval1 * intval2) +
		((intval1 * rem2) >> 16) +
		((intval2 * rem1) >> 16);

	return srate;
}

static void stv0900_set_symbol_rate(struct stv0900_internal *i_params,
					u32 mclk, u32 srate,
					enum fe_stv0900_demod_num demod)
{
	s32 sfr_init_reg;
	u32 symb;

	dprintk(KERN_INFO "%s: Mclk %d, SR %d, Dmd %d\n", __func__, mclk,
							srate, demod);

	dmd_reg(sfr_init_reg, R0900_P1_SFRINIT1, R0900_P2_SFRINIT1);

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

	stv0900_write_reg(i_params, sfr_init_reg, (symb >> 8) & 0x7F);
	stv0900_write_reg(i_params, sfr_init_reg + 1, (symb & 0xFF));
}

static void stv0900_set_max_symbol_rate(struct stv0900_internal *i_params,
					u32 mclk, u32 srate,
					enum fe_stv0900_demod_num demod)
{
	s32 sfr_max_reg;
	u32 symb;

	dmd_reg(sfr_max_reg, R0900_P1_SFRUP1, R0900_P2_SFRUP1);

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
		stv0900_write_reg(i_params, sfr_max_reg, (symb >> 8) & 0x7F);
		stv0900_write_reg(i_params, sfr_max_reg + 1, (symb & 0xFF));
	} else {
		stv0900_write_reg(i_params, sfr_max_reg, 0x7F);
		stv0900_write_reg(i_params, sfr_max_reg + 1, 0xFF);
	}
}

static void stv0900_set_min_symbol_rate(struct stv0900_internal *i_params,
					u32 mclk, u32 srate,
					enum fe_stv0900_demod_num demod)
{
	s32 sfr_min_reg;
	u32	symb;

	dmd_reg(sfr_min_reg, R0900_P1_SFRLOW1, R0900_P2_SFRLOW1);

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

	stv0900_write_reg(i_params, sfr_min_reg, (symb >> 8) & 0xFF);
	stv0900_write_reg(i_params, sfr_min_reg + 1, (symb & 0xFF));
}

static s32 stv0900_get_timing_offst(struct stv0900_internal *i_params,
					u32 srate,
					enum fe_stv0900_demod_num demod)
{
	s32 tmgreg,
	timingoffset;

	dmd_reg(tmgreg, R0900_P1_TMGREG2, R0900_P2_TMGREG2);

	timingoffset = (stv0900_read_reg(i_params, tmgreg) << 16) +
		       (stv0900_read_reg(i_params, tmgreg + 1) << 8) +
		       (stv0900_read_reg(i_params, tmgreg + 2));

	timingoffset = ge2comp(timingoffset, 24);


	if (timingoffset == 0)
		timingoffset = 1;

	timingoffset = ((s32)srate * 10) / ((s32)0x1000000 / timingoffset);
	timingoffset /= 320;

	return timingoffset;
}

static void stv0900_set_dvbs2_rolloff(struct stv0900_internal *i_params,
					enum fe_stv0900_demod_num demod)
{
	s32 rolloff, man_fld, matstr_reg, rolloff_ctl_fld;

	dmd_reg(man_fld, F0900_P1_MANUAL_ROLLOFF, F0900_P2_MANUAL_ROLLOFF);
	dmd_reg(matstr_reg, R0900_P1_MATSTR1, R0900_P2_MATSTR1);
	dmd_reg(rolloff_ctl_fld, F0900_P1_ROLLOFF_CONTROL,
				F0900_P2_ROLLOFF_CONTROL);

	if (i_params->chip_id == 0x10) {
		stv0900_write_bits(i_params, man_fld, 1);
		rolloff = stv0900_read_reg(i_params, matstr_reg) & 0x03;
		stv0900_write_bits(i_params, rolloff_ctl_fld, rolloff);
	} else
		stv0900_write_bits(i_params, man_fld, 0);
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

static int stv0900_check_timing_lock(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod)
{
	int timingLock = FALSE;
	s32 i,
	timingcpt = 0;
	u8 carFreq,
	tmgTHhigh,
	tmgTHLow;

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		carFreq = stv0900_read_reg(i_params, R0900_P1_CARFREQ);
		tmgTHhigh = stv0900_read_reg(i_params, R0900_P1_TMGTHRISE);
		tmgTHLow = stv0900_read_reg(i_params, R0900_P1_TMGTHFALL);
		stv0900_write_reg(i_params, R0900_P1_TMGTHRISE, 0x20);
		stv0900_write_reg(i_params, R0900_P1_TMGTHFALL, 0x0);
		stv0900_write_bits(i_params, F0900_P1_CFR_AUTOSCAN, 0);
		stv0900_write_reg(i_params, R0900_P1_RTC, 0x80);
		stv0900_write_reg(i_params, R0900_P1_RTCS2, 0x40);
		stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0x0);
		stv0900_write_reg(i_params, R0900_P1_CFRINIT1, 0x0);
		stv0900_write_reg(i_params, R0900_P1_CFRINIT0, 0x0);
		stv0900_write_reg(i_params, R0900_P1_AGC2REF, 0x65);
		stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x18);
		msleep(7);

		for (i = 0; i < 10; i++) {
			if (stv0900_get_bits(i_params, F0900_P1_TMGLOCK_QUALITY) >= 2)
				timingcpt++;

			msleep(1);
		}

		if (timingcpt >= 3)
			timingLock = TRUE;

		stv0900_write_reg(i_params, R0900_P1_AGC2REF, 0x38);
		stv0900_write_reg(i_params, R0900_P1_RTC, 0x88);
		stv0900_write_reg(i_params, R0900_P1_RTCS2, 0x68);
		stv0900_write_reg(i_params, R0900_P1_CARFREQ, carFreq);
		stv0900_write_reg(i_params, R0900_P1_TMGTHRISE, tmgTHhigh);
		stv0900_write_reg(i_params, R0900_P1_TMGTHFALL, tmgTHLow);
		break;
	case STV0900_DEMOD_2:
		carFreq = stv0900_read_reg(i_params, R0900_P2_CARFREQ);
		tmgTHhigh = stv0900_read_reg(i_params, R0900_P2_TMGTHRISE);
		tmgTHLow = stv0900_read_reg(i_params, R0900_P2_TMGTHFALL);
		stv0900_write_reg(i_params, R0900_P2_TMGTHRISE, 0x20);
		stv0900_write_reg(i_params, R0900_P2_TMGTHFALL, 0);
		stv0900_write_bits(i_params, F0900_P2_CFR_AUTOSCAN, 0);
		stv0900_write_reg(i_params, R0900_P2_RTC, 0x80);
		stv0900_write_reg(i_params, R0900_P2_RTCS2, 0x40);
		stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0x0);
		stv0900_write_reg(i_params, R0900_P2_CFRINIT1, 0x0);
		stv0900_write_reg(i_params, R0900_P2_CFRINIT0, 0x0);
		stv0900_write_reg(i_params, R0900_P2_AGC2REF, 0x65);
		stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x18);
		msleep(5);
		for (i = 0; i < 10; i++) {
			if (stv0900_get_bits(i_params, F0900_P2_TMGLOCK_QUALITY) >= 2)
				timingcpt++;

			msleep(1);
		}

		if (timingcpt >= 3)
			timingLock = TRUE;

		stv0900_write_reg(i_params, R0900_P2_AGC2REF, 0x38);
		stv0900_write_reg(i_params, R0900_P2_RTC, 0x88);
		stv0900_write_reg(i_params, R0900_P2_RTCS2, 0x68);
		stv0900_write_reg(i_params, R0900_P2_CARFREQ, carFreq);
		stv0900_write_reg(i_params, R0900_P2_TMGTHRISE, tmgTHhigh);
		stv0900_write_reg(i_params, R0900_P2_TMGTHFALL, tmgTHLow);
		break;
	}

	return	timingLock;
}

static int stv0900_get_demod_cold_lock(struct dvb_frontend *fe,
					s32 demod_timeout)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	int lock = FALSE;
	s32 srate, search_range, locktimeout,
		currier_step, nb_steps, current_step,
		direction, tuner_freq, timeout;

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		srate = i_params->dmd1_symbol_rate;
		search_range = i_params->dmd1_srch_range;
		break;

	case STV0900_DEMOD_2:
		srate = i_params->dmd2_symbol_rate;
		search_range = i_params->dmd2_srch_range;
		break;
	}

	if (srate >= 10000000)
		locktimeout = demod_timeout / 3;
	else
		locktimeout = demod_timeout / 2;

	lock = stv0900_get_demod_lock(i_params, demod, locktimeout);

	if (lock == FALSE) {
		if (srate >= 10000000) {
			if (stv0900_check_timing_lock(i_params, demod) == TRUE) {
				switch (demod) {
				case STV0900_DEMOD_1:
				default:
					stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1f);
					stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x15);
					break;
				case STV0900_DEMOD_2:
					stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1f);
					stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x15);
					break;
				}

				lock = stv0900_get_demod_lock(i_params, demod, demod_timeout);
			} else
				lock = FALSE;
		} else {
			if (srate <= 4000000)
				currier_step = 1000;
			else if (srate <= 7000000)
				currier_step = 2000;
			else if (srate <= 10000000)
				currier_step = 3000;
			else
				currier_step = 5000;

			nb_steps = ((search_range / 1000) / currier_step);
			nb_steps /= 2;
			nb_steps = (2 * (nb_steps + 1));
			if (nb_steps < 0)
				nb_steps = 2;
			else if (nb_steps > 12)
				nb_steps = 12;

			current_step = 1;
			direction = 1;
			timeout = (demod_timeout / 3);
			if (timeout > 1000)
				timeout = 1000;

			switch (demod) {
			case STV0900_DEMOD_1:
			default:
				if (lock == FALSE) {
					tuner_freq = i_params->tuner1_freq;
					i_params->tuner1_bw = stv0900_carrier_width(i_params->dmd1_symbol_rate, i_params->rolloff) + i_params->dmd1_symbol_rate;

					while ((current_step <= nb_steps) && (lock == FALSE)) {

						if (direction > 0)
							tuner_freq += (current_step * currier_step);
						else
							tuner_freq -= (current_step * currier_step);

						stv0900_set_tuner(fe, tuner_freq, i_params->tuner1_bw);
						stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1C);
						if (i_params->dmd1_srch_standard == STV0900_SEARCH_DVBS2) {
							stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 0);
							stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 0);
							stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 1);
							stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 1);
						}

						stv0900_write_reg(i_params, R0900_P1_CFRINIT1, 0);
						stv0900_write_reg(i_params, R0900_P1_CFRINIT0, 0);
						stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1F);
						stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x15);
						lock = stv0900_get_demod_lock(i_params, demod, timeout);
						direction *= -1;
						current_step++;
					}
				}
				break;
			case STV0900_DEMOD_2:
				if (lock == FALSE) {
					tuner_freq = i_params->tuner2_freq;
					i_params->tuner2_bw = stv0900_carrier_width(srate, i_params->rolloff) + srate;

					while ((current_step <= nb_steps) && (lock == FALSE)) {

						if (direction > 0)
							tuner_freq += (current_step * currier_step);
						else
							tuner_freq -= (current_step * currier_step);

						stv0900_set_tuner(fe, tuner_freq, i_params->tuner2_bw);
						stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1C);
						if (i_params->dmd2_srch_stndrd == STV0900_SEARCH_DVBS2) {
							stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 0);
							stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 0);
							stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 1);
							stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 1);
						}

						stv0900_write_reg(i_params, R0900_P2_CFRINIT1, 0);
						stv0900_write_reg(i_params, R0900_P2_CFRINIT0, 0);
						stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1F);
						stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x15);
						lock = stv0900_get_demod_lock(i_params, demod, timeout);
						direction *= -1;
						current_step++;
					}
				}
				break;
			}
		}
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
		}

		else {
			(*demod_timeout) = 300;
			(*fec_timeout) = 100;
		}

		break;

	}

	if (algo == STV0900_WARM_START)
		(*demod_timeout) /= 2;
}

static void stv0900_set_viterbi_tracq(struct stv0900_internal *i_params,
					enum fe_stv0900_demod_num demod)
{

	s32 vth_reg;

	dprintk(KERN_INFO "%s\n", __func__);

	dmd_reg(vth_reg, R0900_P1_VTH12, R0900_P2_VTH12);

	stv0900_write_reg(i_params, vth_reg++, 0xd0);
	stv0900_write_reg(i_params, vth_reg++, 0x7d);
	stv0900_write_reg(i_params, vth_reg++, 0x53);
	stv0900_write_reg(i_params, vth_reg++, 0x2F);
	stv0900_write_reg(i_params, vth_reg++, 0x24);
	stv0900_write_reg(i_params, vth_reg++, 0x1F);
}

static void stv0900_set_viterbi_standard(struct stv0900_internal *i_params,
				   enum fe_stv0900_search_standard Standard,
				   enum fe_stv0900_fec PunctureRate,
				   enum fe_stv0900_demod_num demod)
{

	s32 fecmReg,
	prvitReg;

	dprintk(KERN_INFO "%s: ViterbiStandard = ", __func__);

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		fecmReg = R0900_P1_FECM;
		prvitReg = R0900_P1_PRVIT;
		break;
	case STV0900_DEMOD_2:
		fecmReg = R0900_P2_FECM;
		prvitReg = R0900_P2_PRVIT;
		break;
	}

	switch (Standard) {
	case STV0900_AUTO_SEARCH:
		dprintk("Auto\n");
		stv0900_write_reg(i_params, fecmReg, 0x10);
		stv0900_write_reg(i_params, prvitReg, 0x3F);
		break;
	case STV0900_SEARCH_DVBS1:
		dprintk("DVBS1\n");
		stv0900_write_reg(i_params, fecmReg, 0x00);
		switch (PunctureRate) {
		case STV0900_FEC_UNKNOWN:
		default:
			stv0900_write_reg(i_params, prvitReg, 0x2F);
			break;
		case STV0900_FEC_1_2:
			stv0900_write_reg(i_params, prvitReg, 0x01);
			break;
		case STV0900_FEC_2_3:
			stv0900_write_reg(i_params, prvitReg, 0x02);
			break;
		case STV0900_FEC_3_4:
			stv0900_write_reg(i_params, prvitReg, 0x04);
			break;
		case STV0900_FEC_5_6:
			stv0900_write_reg(i_params, prvitReg, 0x08);
			break;
		case STV0900_FEC_7_8:
			stv0900_write_reg(i_params, prvitReg, 0x20);
			break;
		}

		break;
	case STV0900_SEARCH_DSS:
		dprintk("DSS\n");
		stv0900_write_reg(i_params, fecmReg, 0x80);
		switch (PunctureRate) {
		case STV0900_FEC_UNKNOWN:
		default:
			stv0900_write_reg(i_params, prvitReg, 0x13);
			break;
		case STV0900_FEC_1_2:
			stv0900_write_reg(i_params, prvitReg, 0x01);
			break;
		case STV0900_FEC_2_3:
			stv0900_write_reg(i_params, prvitReg, 0x02);
			break;
		case STV0900_FEC_6_7:
			stv0900_write_reg(i_params, prvitReg, 0x10);
			break;
		}
		break;
	default:
		break;
	}
}

static void stv0900_track_optimization(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	s32 srate, pilots, aclc, freq1, freq0,
		i = 0, timed, timef, blindTunSw = 0;

	enum fe_stv0900_rolloff rolloff;
	enum fe_stv0900_modcode foundModcod;

	dprintk(KERN_INFO "%s\n", __func__);

	srate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);
	srate += stv0900_get_timing_offst(i_params, srate, demod);

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		switch (i_params->dmd1_rslts.standard) {
		case STV0900_DVBS1_STANDARD:
			if (i_params->dmd1_srch_standard == STV0900_AUTO_SEARCH) {
				stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 1);
				stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 0);
			}

			stv0900_write_bits(i_params, F0900_P1_ROLLOFF_CONTROL, i_params->rolloff);
			stv0900_write_bits(i_params, F0900_P1_MANUAL_ROLLOFF, 1);
			stv0900_write_reg(i_params, R0900_P1_ERRCTRL1, 0x75);
			break;
		case STV0900_DSS_STANDARD:
			if (i_params->dmd1_srch_standard == STV0900_AUTO_SEARCH) {
				stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 1);
				stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 0);
			}

			stv0900_write_bits(i_params, F0900_P1_ROLLOFF_CONTROL, i_params->rolloff);
			stv0900_write_bits(i_params, F0900_P1_MANUAL_ROLLOFF, 1);
			stv0900_write_reg(i_params, R0900_P1_ERRCTRL1, 0x75);
			break;
		case STV0900_DVBS2_STANDARD:
			stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 1);
			stv0900_write_reg(i_params, R0900_P1_ACLC, 0);
			stv0900_write_reg(i_params, R0900_P1_BCLC, 0);
			if (i_params->dmd1_rslts.frame_length == STV0900_LONG_FRAME) {
				foundModcod = stv0900_get_bits(i_params, F0900_P1_DEMOD_MODCOD);
				pilots = stv0900_get_bits(i_params, F0900_P1_DEMOD_TYPE) & 0x01;
				aclc = stv0900_get_optim_carr_loop(srate, foundModcod, pilots, i_params->chip_id);
				if (foundModcod <= STV0900_QPSK_910)
					stv0900_write_reg(i_params, R0900_P1_ACLC2S2Q, aclc);
				else if (foundModcod <= STV0900_8PSK_910) {
					stv0900_write_reg(i_params, R0900_P1_ACLC2S2Q, 0x2a);
					stv0900_write_reg(i_params, R0900_P1_ACLC2S28, aclc);
				}

				if ((i_params->demod_mode == STV0900_SINGLE) && (foundModcod > STV0900_8PSK_910)) {
					if (foundModcod <= STV0900_16APSK_910) {
						stv0900_write_reg(i_params, R0900_P1_ACLC2S2Q, 0x2a);
						stv0900_write_reg(i_params, R0900_P1_ACLC2S216A, aclc);
					} else if (foundModcod <= STV0900_32APSK_910) {
						stv0900_write_reg(i_params, R0900_P1_ACLC2S2Q, 0x2a);
						stv0900_write_reg(i_params, R0900_P1_ACLC2S232A, aclc);
					}
				}

			} else {
				aclc = stv0900_get_optim_short_carr_loop(srate, i_params->dmd1_rslts.modulation, i_params->chip_id);
				if (i_params->dmd1_rslts.modulation == STV0900_QPSK)
					stv0900_write_reg(i_params, R0900_P1_ACLC2S2Q, aclc);

				else if (i_params->dmd1_rslts.modulation == STV0900_8PSK) {
					stv0900_write_reg(i_params, R0900_P1_ACLC2S2Q, 0x2a);
					stv0900_write_reg(i_params, R0900_P1_ACLC2S28, aclc);
				} else if (i_params->dmd1_rslts.modulation == STV0900_16APSK) {
					stv0900_write_reg(i_params, R0900_P1_ACLC2S2Q, 0x2a);
					stv0900_write_reg(i_params, R0900_P1_ACLC2S216A, aclc);
				} else if (i_params->dmd1_rslts.modulation == STV0900_32APSK) {
					stv0900_write_reg(i_params, R0900_P1_ACLC2S2Q, 0x2a);
					stv0900_write_reg(i_params, R0900_P1_ACLC2S232A, aclc);
				}

			}

			if (i_params->chip_id <= 0x11) {
				if (i_params->demod_mode != STV0900_SINGLE)
					stv0900_activate_s2_modcode(i_params, demod);

			}

			stv0900_write_reg(i_params, R0900_P1_ERRCTRL1, 0x67);
			break;
		case STV0900_UNKNOWN_STANDARD:
		default:
			stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 1);
			break;
		}

		freq1 = stv0900_read_reg(i_params, R0900_P1_CFR2);
		freq0 = stv0900_read_reg(i_params, R0900_P1_CFR1);
		rolloff = stv0900_get_bits(i_params, F0900_P1_ROLLOFF_STATUS);
		if (i_params->dmd1_srch_algo == STV0900_BLIND_SEARCH) {
			stv0900_write_reg(i_params, R0900_P1_SFRSTEP, 0x00);
			stv0900_write_bits(i_params, F0900_P1_SCAN_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P1_CFR_AUTOSCAN, 0);
			stv0900_write_reg(i_params, R0900_P1_TMGCFG2, 0x01);
			stv0900_set_symbol_rate(i_params, i_params->mclk, srate, demod);
			stv0900_set_max_symbol_rate(i_params, i_params->mclk, srate, demod);
			stv0900_set_min_symbol_rate(i_params, i_params->mclk, srate, demod);
			blindTunSw = 1;
		}

		if (i_params->chip_id >= 0x20) {
			if ((i_params->dmd1_srch_standard == STV0900_SEARCH_DVBS1) || (i_params->dmd1_srch_standard == STV0900_SEARCH_DSS) || (i_params->dmd1_srch_standard == STV0900_AUTO_SEARCH)) {
				stv0900_write_reg(i_params, R0900_P1_VAVSRVIT, 0x0a);
				stv0900_write_reg(i_params, R0900_P1_VITSCALE, 0x0);
			}
		}

		if (i_params->chip_id < 0x20)
			stv0900_write_reg(i_params, R0900_P1_CARHDR, 0x08);

		if (i_params->chip_id == 0x10)
			stv0900_write_reg(i_params, R0900_P1_CORRELEXP, 0x0A);

		stv0900_write_reg(i_params, R0900_P1_AGC2REF, 0x38);

		if ((i_params->chip_id >= 0x20) || (blindTunSw == 1) || (i_params->dmd1_symbol_rate < 10000000)) {
			stv0900_write_reg(i_params, R0900_P1_CFRINIT1, freq1);
			stv0900_write_reg(i_params, R0900_P1_CFRINIT0, freq0);
			i_params->tuner1_bw = stv0900_carrier_width(srate, i_params->rolloff) + 10000000;

			if ((i_params->chip_id >= 0x20) || (blindTunSw == 1)) {
				if (i_params->dmd1_srch_algo != STV0900_WARM_START)
					stv0900_set_bandwidth(fe, i_params->tuner1_bw);
			}

			if ((i_params->dmd1_srch_algo == STV0900_BLIND_SEARCH) || (i_params->dmd1_symbol_rate < 10000000))
				msleep(50);
			else
				msleep(5);

			stv0900_get_lock_timeout(&timed, &timef, srate, STV0900_WARM_START);

			if (stv0900_get_demod_lock(i_params, demod, timed / 2) == FALSE) {
				stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1F);
				stv0900_write_reg(i_params, R0900_P1_CFRINIT1, freq1);
				stv0900_write_reg(i_params, R0900_P1_CFRINIT0, freq0);
				stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x18);
				i = 0;
				while ((stv0900_get_demod_lock(i_params, demod, timed / 2) == FALSE) && (i <= 2)) {
					stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1F);
					stv0900_write_reg(i_params, R0900_P1_CFRINIT1, freq1);
					stv0900_write_reg(i_params, R0900_P1_CFRINIT0, freq0);
					stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x18);
					i++;
				}
			}

		}

		if (i_params->chip_id >= 0x20)
			stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0x49);

		if ((i_params->dmd1_rslts.standard == STV0900_DVBS1_STANDARD) || (i_params->dmd1_rslts.standard == STV0900_DSS_STANDARD))
			stv0900_set_viterbi_tracq(i_params, demod);

		break;

	case STV0900_DEMOD_2:
		switch (i_params->dmd2_rslts.standard) {
		case STV0900_DVBS1_STANDARD:

			if (i_params->dmd2_srch_stndrd == STV0900_AUTO_SEARCH) {
				stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 1);
				stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 0);
			}

			stv0900_write_bits(i_params, F0900_P2_ROLLOFF_CONTROL, i_params->rolloff);
			stv0900_write_bits(i_params, F0900_P2_MANUAL_ROLLOFF, 1);
			stv0900_write_reg(i_params, R0900_P2_ERRCTRL1, 0x75);
			break;
		case STV0900_DSS_STANDARD:
			if (i_params->dmd2_srch_stndrd == STV0900_AUTO_SEARCH) {
				stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 1);
				stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 0);
			}

			stv0900_write_bits(i_params, F0900_P2_ROLLOFF_CONTROL, i_params->rolloff);
			stv0900_write_bits(i_params, F0900_P2_MANUAL_ROLLOFF, 1);
			stv0900_write_reg(i_params, R0900_P2_ERRCTRL1, 0x75);
			break;
		case STV0900_DVBS2_STANDARD:
			stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 1);
			stv0900_write_reg(i_params, R0900_P2_ACLC, 0);
			stv0900_write_reg(i_params, R0900_P2_BCLC, 0);
			if (i_params->dmd2_rslts.frame_length == STV0900_LONG_FRAME) {
				foundModcod = stv0900_get_bits(i_params, F0900_P2_DEMOD_MODCOD);
				pilots = stv0900_get_bits(i_params, F0900_P2_DEMOD_TYPE) & 0x01;
				aclc = stv0900_get_optim_carr_loop(srate, foundModcod, pilots, i_params->chip_id);
				if (foundModcod <= STV0900_QPSK_910)
					stv0900_write_reg(i_params, R0900_P2_ACLC2S2Q, aclc);
				else if (foundModcod <= STV0900_8PSK_910) {
					stv0900_write_reg(i_params, R0900_P2_ACLC2S2Q, 0x2a);
					stv0900_write_reg(i_params, R0900_P2_ACLC2S28, aclc);
				}

				if ((i_params->demod_mode == STV0900_SINGLE) && (foundModcod > STV0900_8PSK_910)) {
					if (foundModcod <= STV0900_16APSK_910) {
						stv0900_write_reg(i_params, R0900_P2_ACLC2S2Q, 0x2a);
						stv0900_write_reg(i_params, R0900_P2_ACLC2S216A, aclc);
					} else if (foundModcod <= STV0900_32APSK_910) {
						stv0900_write_reg(i_params, R0900_P2_ACLC2S2Q, 0x2a);
						stv0900_write_reg(i_params, R0900_P2_ACLC2S232A, aclc);
					}

				}

			} else {
				aclc = stv0900_get_optim_short_carr_loop(srate,
									i_params->dmd2_rslts.modulation,
									i_params->chip_id);

				if (i_params->dmd2_rslts.modulation == STV0900_QPSK)
					stv0900_write_reg(i_params, R0900_P2_ACLC2S2Q, aclc);

				else if (i_params->dmd2_rslts.modulation == STV0900_8PSK) {
					stv0900_write_reg(i_params, R0900_P2_ACLC2S2Q, 0x2a);
					stv0900_write_reg(i_params, R0900_P2_ACLC2S28, aclc);
				} else if (i_params->dmd2_rslts.modulation == STV0900_16APSK) {
					stv0900_write_reg(i_params, R0900_P2_ACLC2S2Q, 0x2a);
					stv0900_write_reg(i_params, R0900_P2_ACLC2S216A, aclc);
				} else if (i_params->dmd2_rslts.modulation == STV0900_32APSK) {
					stv0900_write_reg(i_params, R0900_P2_ACLC2S2Q, 0x2a);
					stv0900_write_reg(i_params, R0900_P2_ACLC2S232A, aclc);
				}
			}

			stv0900_write_reg(i_params, R0900_P2_ERRCTRL1, 0x67);

			break;
		case STV0900_UNKNOWN_STANDARD:
		default:
			stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 1);
			break;
		}

		freq1 = stv0900_read_reg(i_params, R0900_P2_CFR2);
		freq0 = stv0900_read_reg(i_params, R0900_P2_CFR1);
		rolloff = stv0900_get_bits(i_params, F0900_P2_ROLLOFF_STATUS);
		if (i_params->dmd2_srch_algo == STV0900_BLIND_SEARCH) {
			stv0900_write_reg(i_params, R0900_P2_SFRSTEP, 0x00);
			stv0900_write_bits(i_params, F0900_P2_SCAN_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P2_CFR_AUTOSCAN, 0);
			stv0900_write_reg(i_params, R0900_P2_TMGCFG2, 0x01);
			stv0900_set_symbol_rate(i_params, i_params->mclk, srate, demod);
			stv0900_set_max_symbol_rate(i_params, i_params->mclk, srate, demod);
			stv0900_set_min_symbol_rate(i_params, i_params->mclk, srate, demod);
			blindTunSw = 1;
		}

		if (i_params->chip_id >= 0x20) {
			if ((i_params->dmd2_srch_stndrd == STV0900_SEARCH_DVBS1) || (i_params->dmd2_srch_stndrd == STV0900_SEARCH_DSS) || (i_params->dmd2_srch_stndrd == STV0900_AUTO_SEARCH)) {
				stv0900_write_reg(i_params, R0900_P2_VAVSRVIT, 0x0a);
				stv0900_write_reg(i_params, R0900_P2_VITSCALE, 0x0);
			}
		}

		if (i_params->chip_id < 0x20)
			stv0900_write_reg(i_params, R0900_P2_CARHDR, 0x08);

		if (i_params->chip_id == 0x10)
			stv0900_write_reg(i_params, R0900_P2_CORRELEXP, 0x0a);

		stv0900_write_reg(i_params, R0900_P2_AGC2REF, 0x38);
		if ((i_params->chip_id >= 0x20) || (blindTunSw == 1) || (i_params->dmd2_symbol_rate < 10000000)) {
			stv0900_write_reg(i_params, R0900_P2_CFRINIT1, freq1);
			stv0900_write_reg(i_params, R0900_P2_CFRINIT0, freq0);
			i_params->tuner2_bw = stv0900_carrier_width(srate, i_params->rolloff) + 10000000;

			if ((i_params->chip_id >= 0x20) || (blindTunSw == 1)) {
				if (i_params->dmd2_srch_algo != STV0900_WARM_START)
					stv0900_set_bandwidth(fe, i_params->tuner2_bw);
			}

			if ((i_params->dmd2_srch_algo == STV0900_BLIND_SEARCH) || (i_params->dmd2_symbol_rate < 10000000))
				msleep(50);
			else
				msleep(5);

			stv0900_get_lock_timeout(&timed, &timef, srate, STV0900_WARM_START);
			if (stv0900_get_demod_lock(i_params, demod, timed / 2) == FALSE) {
				stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1F);
				stv0900_write_reg(i_params, R0900_P2_CFRINIT1, freq1);
				stv0900_write_reg(i_params, R0900_P2_CFRINIT0, freq0);
				stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x18);
				i = 0;
				while ((stv0900_get_demod_lock(i_params, demod, timed / 2) == FALSE) && (i <= 2)) {
					stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1F);
					stv0900_write_reg(i_params, R0900_P2_CFRINIT1, freq1);
					stv0900_write_reg(i_params, R0900_P2_CFRINIT0, freq0);
					stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x18);
					i++;
				}
			}
		}

		if (i_params->chip_id >= 0x20)
			stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0x49);

		if ((i_params->dmd2_rslts.standard == STV0900_DVBS1_STANDARD) || (i_params->dmd2_rslts.standard == STV0900_DSS_STANDARD))
			stv0900_set_viterbi_tracq(i_params, demod);

		break;
	}
}

static int stv0900_get_fec_lock(struct stv0900_internal *i_params, enum fe_stv0900_demod_num demod, s32 time_out)
{
	s32 timer = 0, lock = 0, header_field, pktdelin_field, lock_vit_field;

	enum fe_stv0900_search_state dmd_state;

	dprintk(KERN_INFO "%s\n", __func__);

	dmd_reg(header_field, F0900_P1_HEADER_MODE, F0900_P2_HEADER_MODE);
	dmd_reg(pktdelin_field, F0900_P1_PKTDELIN_LOCK, F0900_P2_PKTDELIN_LOCK);
	dmd_reg(lock_vit_field, F0900_P1_LOCKEDVIT, F0900_P2_LOCKEDVIT);

	dmd_state = stv0900_get_bits(i_params, header_field);

	while ((timer < time_out) && (lock == 0)) {
		switch (dmd_state) {
		case STV0900_SEARCH:
		case STV0900_PLH_DETECTED:
		default:
			lock = 0;
			break;
		case STV0900_DVBS2_FOUND:
			lock = stv0900_get_bits(i_params, pktdelin_field);
			break;
		case STV0900_DVBS_FOUND:
			lock = stv0900_get_bits(i_params, lock_vit_field);
			break;
		}

		if (lock == 0) {
			msleep(10);
			timer += 10;
		}
	}

	if (lock)
		dprintk("DEMOD FEC LOCK OK\n");
	else
		dprintk("DEMOD FEC LOCK FAIL\n");

	return lock;
}

static int stv0900_wait_for_lock(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod,
				s32 dmd_timeout, s32 fec_timeout)
{

	s32 timer = 0, lock = 0, str_merg_rst_fld, str_merg_lock_fld;

	dprintk(KERN_INFO "%s\n", __func__);

	dmd_reg(str_merg_rst_fld, F0900_P1_RST_HWARE, F0900_P2_RST_HWARE);
	dmd_reg(str_merg_lock_fld, F0900_P1_TSFIFO_LINEOK, F0900_P2_TSFIFO_LINEOK);

	lock = stv0900_get_demod_lock(i_params, demod, dmd_timeout);

	if (lock)
		lock = lock && stv0900_get_fec_lock(i_params, demod, fec_timeout);

	if (lock) {
		lock = 0;

		dprintk(KERN_INFO "%s: Timer = %d, time_out = %d\n", __func__, timer, fec_timeout);

		while ((timer < fec_timeout) && (lock == 0)) {
			lock = stv0900_get_bits(i_params, str_merg_lock_fld);
			msleep(1);
			timer++;
		}
	}

	if (lock)
		dprintk(KERN_INFO "%s: DEMOD LOCK OK\n", __func__);
	else
		dprintk(KERN_INFO "%s: DEMOD LOCK FAIL\n", __func__);

	if (lock)
		return TRUE;
	else
		return FALSE;
}

enum fe_stv0900_tracking_standard stv0900_get_standard(struct dvb_frontend *fe,
						enum fe_stv0900_demod_num demod)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_tracking_standard fnd_standard;
	s32 state_field,
	dss_dvb_field;

	dprintk(KERN_INFO "%s\n", __func__);

	dmd_reg(state_field, F0900_P1_HEADER_MODE, F0900_P2_HEADER_MODE);
	dmd_reg(dss_dvb_field, F0900_P1_DSS_DVB, F0900_P2_DSS_DVB);

	if (stv0900_get_bits(i_params, state_field) == 2)
		fnd_standard = STV0900_DVBS2_STANDARD;

	else if (stv0900_get_bits(i_params, state_field) == 3) {
		if (stv0900_get_bits(i_params, dss_dvb_field) == 1)
			fnd_standard = STV0900_DSS_STANDARD;
		else
			fnd_standard = STV0900_DVBS1_STANDARD;
	} else
		fnd_standard = STV0900_UNKNOWN_STANDARD;

	return fnd_standard;
}

static s32 stv0900_get_carr_freq(struct stv0900_internal *i_params, u32 mclk,
					enum fe_stv0900_demod_num demod)
{
	s32 cfr_field2, cfr_field1, cfr_field0,
		derot, rem1, rem2, intval1, intval2;

	dmd_reg(cfr_field2, F0900_P1_CAR_FREQ2, F0900_P2_CAR_FREQ2);
	dmd_reg(cfr_field1, F0900_P1_CAR_FREQ1, F0900_P2_CAR_FREQ1);
	dmd_reg(cfr_field0, F0900_P1_CAR_FREQ0, F0900_P2_CAR_FREQ0);

	derot = (stv0900_get_bits(i_params, cfr_field2) << 16) +
		(stv0900_get_bits(i_params, cfr_field1) << 8) +
		(stv0900_get_bits(i_params, cfr_field0));

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
	u32 frequency = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;

	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;

	if (tuner_ops->get_frequency) {
		if ((tuner_ops->get_frequency(fe, &frequency)) < 0)
			dprintk("%s: Invalid parameter\n", __func__);
		else
			dprintk("%s: Frequency=%d\n", __func__, frequency);

	}

	return frequency;
}

static enum fe_stv0900_fec stv0900_get_vit_fec(struct stv0900_internal *i_params,
						enum fe_stv0900_demod_num demod)
{
	s32 rate_fld, vit_curpun_fld;
	enum fe_stv0900_fec prate;

	dmd_reg(vit_curpun_fld, F0900_P1_VIT_CURPUN, F0900_P2_VIT_CURPUN);
	rate_fld = stv0900_get_bits(i_params, vit_curpun_fld);

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

static enum fe_stv0900_signal_type stv0900_get_signal_params(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	enum fe_stv0900_signal_type range = STV0900_OUTOFRANGE;
	s32 offsetFreq,
	srate_offset,
	i = 0;

	u8 timing;

	msleep(5);
	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		if (i_params->dmd1_srch_algo == STV0900_BLIND_SEARCH) {
			timing = stv0900_read_reg(i_params, R0900_P1_TMGREG2);
			i = 0;
			stv0900_write_reg(i_params, R0900_P1_SFRSTEP, 0x5c);

			while ((i <= 50) && (timing != 0) && (timing != 0xFF)) {
				timing = stv0900_read_reg(i_params, R0900_P1_TMGREG2);
				msleep(5);
				i += 5;
			}
		}

		i_params->dmd1_rslts.standard = stv0900_get_standard(fe, demod);
		i_params->dmd1_rslts.frequency = stv0900_get_tuner_freq(fe);
		offsetFreq = stv0900_get_carr_freq(i_params, i_params->mclk, demod) / 1000;
		i_params->dmd1_rslts.frequency += offsetFreq;
		i_params->dmd1_rslts.symbol_rate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);
		srate_offset = stv0900_get_timing_offst(i_params, i_params->dmd1_rslts.symbol_rate, demod);
		i_params->dmd1_rslts.symbol_rate += srate_offset;
		i_params->dmd1_rslts.fec = stv0900_get_vit_fec(i_params, demod);
		i_params->dmd1_rslts.modcode = stv0900_get_bits(i_params, F0900_P1_DEMOD_MODCOD);
		i_params->dmd1_rslts.pilot = stv0900_get_bits(i_params, F0900_P1_DEMOD_TYPE) & 0x01;
		i_params->dmd1_rslts.frame_length = ((u32)stv0900_get_bits(i_params, F0900_P1_DEMOD_TYPE)) >> 1;
		i_params->dmd1_rslts.rolloff = stv0900_get_bits(i_params, F0900_P1_ROLLOFF_STATUS);
		switch (i_params->dmd1_rslts.standard) {
		case STV0900_DVBS2_STANDARD:
			i_params->dmd1_rslts.spectrum = stv0900_get_bits(i_params, F0900_P1_SPECINV_DEMOD);
			if (i_params->dmd1_rslts.modcode <= STV0900_QPSK_910)
				i_params->dmd1_rslts.modulation = STV0900_QPSK;
			else if (i_params->dmd1_rslts.modcode <= STV0900_8PSK_910)
				i_params->dmd1_rslts.modulation = STV0900_8PSK;
			else if (i_params->dmd1_rslts.modcode <= STV0900_16APSK_910)
				i_params->dmd1_rslts.modulation = STV0900_16APSK;
			else if (i_params->dmd1_rslts.modcode <= STV0900_32APSK_910)
				i_params->dmd1_rslts.modulation = STV0900_32APSK;
			else
				i_params->dmd1_rslts.modulation = STV0900_UNKNOWN;
			break;
		case STV0900_DVBS1_STANDARD:
		case STV0900_DSS_STANDARD:
			i_params->dmd1_rslts.spectrum = stv0900_get_bits(i_params, F0900_P1_IQINV);
			i_params->dmd1_rslts.modulation = STV0900_QPSK;
			break;
		default:
			break;
		}

		if ((i_params->dmd1_srch_algo == STV0900_BLIND_SEARCH) || (i_params->dmd1_symbol_rate < 10000000)) {
			offsetFreq =	i_params->dmd1_rslts.frequency - i_params->tuner1_freq;
			i_params->tuner1_freq = stv0900_get_tuner_freq(fe);
			if (ABS(offsetFreq) <= ((i_params->dmd1_srch_range / 2000) + 500))
				range = STV0900_RANGEOK;
			else
				if (ABS(offsetFreq) <= (stv0900_carrier_width(i_params->dmd1_rslts.symbol_rate, i_params->dmd1_rslts.rolloff) / 2000))
					range = STV0900_RANGEOK;
				else
					range = STV0900_OUTOFRANGE;

		} else {
			if (ABS(offsetFreq) <= ((i_params->dmd1_srch_range / 2000) + 500))
				range = STV0900_RANGEOK;
			else
				range = STV0900_OUTOFRANGE;
		}
		break;
	case STV0900_DEMOD_2:
		if (i_params->dmd2_srch_algo == STV0900_BLIND_SEARCH) {
			timing = stv0900_read_reg(i_params, R0900_P2_TMGREG2);
			i = 0;
			stv0900_write_reg(i_params, R0900_P2_SFRSTEP, 0x5c);

			while ((i <= 50) && (timing != 0) && (timing != 0xff)) {
				timing = stv0900_read_reg(i_params, R0900_P2_TMGREG2);
				msleep(5);
				i += 5;
			}
		}

		i_params->dmd2_rslts.standard = stv0900_get_standard(fe, demod);
		i_params->dmd2_rslts.frequency = stv0900_get_tuner_freq(fe);
		offsetFreq = stv0900_get_carr_freq(i_params, i_params->mclk, demod) / 1000;
		i_params->dmd2_rslts.frequency += offsetFreq;
		i_params->dmd2_rslts.symbol_rate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);
		srate_offset = stv0900_get_timing_offst(i_params, i_params->dmd2_rslts.symbol_rate, demod);
		i_params->dmd2_rslts.symbol_rate += srate_offset;
		i_params->dmd2_rslts.fec = stv0900_get_vit_fec(i_params, demod);
		i_params->dmd2_rslts.modcode = stv0900_get_bits(i_params, F0900_P2_DEMOD_MODCOD);
		i_params->dmd2_rslts.pilot = stv0900_get_bits(i_params, F0900_P2_DEMOD_TYPE) & 0x01;
		i_params->dmd2_rslts.frame_length = ((u32)stv0900_get_bits(i_params, F0900_P2_DEMOD_TYPE)) >> 1;
		i_params->dmd2_rslts.rolloff = stv0900_get_bits(i_params, F0900_P2_ROLLOFF_STATUS);
		switch (i_params->dmd2_rslts.standard) {
		case STV0900_DVBS2_STANDARD:
			i_params->dmd2_rslts.spectrum = stv0900_get_bits(i_params, F0900_P2_SPECINV_DEMOD);
			if (i_params->dmd2_rslts.modcode <= STV0900_QPSK_910)
				i_params->dmd2_rslts.modulation = STV0900_QPSK;
			else if (i_params->dmd2_rslts.modcode <= STV0900_8PSK_910)
				i_params->dmd2_rslts.modulation = STV0900_8PSK;
			else if (i_params->dmd2_rslts.modcode <= STV0900_16APSK_910)
				i_params->dmd2_rslts.modulation = STV0900_16APSK;
			else if (i_params->dmd2_rslts.modcode <= STV0900_32APSK_910)
				i_params->dmd2_rslts.modulation = STV0900_32APSK;
			else
				i_params->dmd2_rslts.modulation = STV0900_UNKNOWN;
			break;
		case STV0900_DVBS1_STANDARD:
		case STV0900_DSS_STANDARD:
			i_params->dmd2_rslts.spectrum = stv0900_get_bits(i_params, F0900_P2_IQINV);
			i_params->dmd2_rslts.modulation = STV0900_QPSK;
			break;
		default:
			break;
		}

		if ((i_params->dmd2_srch_algo == STV0900_BLIND_SEARCH) || (i_params->dmd2_symbol_rate < 10000000)) {
			offsetFreq =	i_params->dmd2_rslts.frequency - i_params->tuner2_freq;
			i_params->tuner2_freq = stv0900_get_tuner_freq(fe);

			if (ABS(offsetFreq) <= ((i_params->dmd2_srch_range / 2000) + 500))
				range = STV0900_RANGEOK;
			else
				if (ABS(offsetFreq) <= (stv0900_carrier_width(i_params->dmd2_rslts.symbol_rate, i_params->dmd2_rslts.rolloff) / 2000))
					range = STV0900_RANGEOK;
				else
					range = STV0900_OUTOFRANGE;
		} else {
			if (ABS(offsetFreq) <= ((i_params->dmd2_srch_range / 2000) + 500))
				range = STV0900_RANGEOK;
			else
				range = STV0900_OUTOFRANGE;
		}

		break;
	}

	return range;
}

static enum fe_stv0900_signal_type stv0900_dvbs1_acq_workaround(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	s32 srate, demod_timeout,
		fec_timeout, freq1, freq0;
	enum fe_stv0900_signal_type signal_type = STV0900_NODATA;;

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		i_params->dmd1_rslts.locked = FALSE;
		if (stv0900_get_bits(i_params, F0900_P1_HEADER_MODE) == STV0900_DVBS_FOUND) {
			srate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);
			srate += stv0900_get_timing_offst(i_params, srate, demod);
			if (i_params->dmd1_srch_algo == STV0900_BLIND_SEARCH)
				stv0900_set_symbol_rate(i_params, i_params->mclk, srate, demod);

			stv0900_get_lock_timeout(&demod_timeout, &fec_timeout, srate, STV0900_WARM_START);
			freq1 = stv0900_read_reg(i_params, R0900_P1_CFR2);
			freq0 = stv0900_read_reg(i_params, R0900_P1_CFR1);
			stv0900_write_bits(i_params, F0900_P1_CFR_AUTOSCAN, 0);
			stv0900_write_bits(i_params, F0900_P1_SPECINV_CONTROL, STV0900_IQ_FORCE_SWAPPED);
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1C);
			stv0900_write_reg(i_params, R0900_P1_CFRINIT1, freq1);
			stv0900_write_reg(i_params, R0900_P1_CFRINIT0, freq0);
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x18);
			if (stv0900_wait_for_lock(i_params, demod, demod_timeout, fec_timeout) == TRUE) {
				i_params->dmd1_rslts.locked = TRUE;
				signal_type = stv0900_get_signal_params(fe);
				stv0900_track_optimization(fe);
			} else {
				stv0900_write_bits(i_params, F0900_P1_SPECINV_CONTROL, STV0900_IQ_FORCE_NORMAL);
				stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1c);
				stv0900_write_reg(i_params, R0900_P1_CFRINIT1, freq1);
				stv0900_write_reg(i_params, R0900_P1_CFRINIT0, freq0);
				stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x18);
				if (stv0900_wait_for_lock(i_params, demod, demod_timeout, fec_timeout) == TRUE) {
					i_params->dmd1_rslts.locked = TRUE;
					signal_type = stv0900_get_signal_params(fe);
					stv0900_track_optimization(fe);
				}

			}

		} else
			i_params->dmd1_rslts.locked = FALSE;

		break;
	case STV0900_DEMOD_2:
		i_params->dmd2_rslts.locked = FALSE;
		if (stv0900_get_bits(i_params, F0900_P2_HEADER_MODE) == STV0900_DVBS_FOUND) {
			srate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);
			srate += stv0900_get_timing_offst(i_params, srate, demod);

			if (i_params->dmd2_srch_algo == STV0900_BLIND_SEARCH)
				stv0900_set_symbol_rate(i_params, i_params->mclk, srate, demod);

			stv0900_get_lock_timeout(&demod_timeout, &fec_timeout, srate, STV0900_WARM_START);
			freq1 = stv0900_read_reg(i_params, R0900_P2_CFR2);
			freq0 = stv0900_read_reg(i_params, R0900_P2_CFR1);
			stv0900_write_bits(i_params, F0900_P2_CFR_AUTOSCAN, 0);
			stv0900_write_bits(i_params, F0900_P2_SPECINV_CONTROL, STV0900_IQ_FORCE_SWAPPED);
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1C);
			stv0900_write_reg(i_params, R0900_P2_CFRINIT1, freq1);
			stv0900_write_reg(i_params, R0900_P2_CFRINIT0, freq0);
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x18);

			if (stv0900_wait_for_lock(i_params, demod, demod_timeout, fec_timeout) == TRUE) {
				i_params->dmd2_rslts.locked = TRUE;
				signal_type = stv0900_get_signal_params(fe);
				stv0900_track_optimization(fe);
			} else {
				stv0900_write_bits(i_params, F0900_P2_SPECINV_CONTROL, STV0900_IQ_FORCE_NORMAL);
				stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1c);
				stv0900_write_reg(i_params, R0900_P2_CFRINIT1, freq1);
				stv0900_write_reg(i_params, R0900_P2_CFRINIT0, freq0);
				stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x18);

				if (stv0900_wait_for_lock(i_params, demod, demod_timeout, fec_timeout) == TRUE) {
					i_params->dmd2_rslts.locked = TRUE;
					signal_type = stv0900_get_signal_params(fe);
					stv0900_track_optimization(fe);
				}

			}

		} else
			i_params->dmd1_rslts.locked = FALSE;

		break;
	}

	return signal_type;
}

static u16 stv0900_blind_check_agc2_min_level(struct stv0900_internal *i_params,
					enum fe_stv0900_demod_num demod)
{
	u32 minagc2level = 0xffff,
		agc2level,
		init_freq, freq_step;

	s32 i, j, nb_steps, direction;

	dprintk(KERN_INFO "%s\n", __func__);

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		stv0900_write_reg(i_params, R0900_P1_AGC2REF, 0x38);
		stv0900_write_bits(i_params, F0900_P1_SCAN_ENABLE, 1);
		stv0900_write_bits(i_params, F0900_P1_CFR_AUTOSCAN, 1);

		stv0900_write_reg(i_params, R0900_P1_SFRUP1, 0x83);
		stv0900_write_reg(i_params, R0900_P1_SFRUP0, 0xc0);

		stv0900_write_reg(i_params, R0900_P1_SFRLOW1, 0x82);
		stv0900_write_reg(i_params, R0900_P1_SFRLOW0, 0xa0);
		stv0900_write_reg(i_params, R0900_P1_DMDT0M, 0x0);

		stv0900_set_symbol_rate(i_params, i_params->mclk, 1000000, demod);
		nb_steps = -1 + (i_params->dmd1_srch_range / 1000000);
		nb_steps /= 2;
		nb_steps = (2 * nb_steps) + 1;

		if (nb_steps < 0)
			nb_steps = 1;

		direction = 1;

		freq_step = (1000000 << 8) / (i_params->mclk >> 8);

		init_freq = 0;

		for (i = 0; i < nb_steps; i++) {
			if (direction > 0)
				init_freq = init_freq + (freq_step * i);
			else
				init_freq = init_freq - (freq_step * i);

			direction *= -1;
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x5C);
			stv0900_write_reg(i_params, R0900_P1_CFRINIT1, (init_freq >> 8) & 0xff);
			stv0900_write_reg(i_params, R0900_P1_CFRINIT0, init_freq  & 0xff);
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x58);
			msleep(10);
			agc2level = 0;

			for (j = 0; j < 10; j++)
				agc2level += (stv0900_read_reg(i_params, R0900_P1_AGC2I1) << 8)
						| stv0900_read_reg(i_params, R0900_P1_AGC2I0);

			agc2level /= 10;

			if (agc2level < minagc2level)
				minagc2level = agc2level;
		}
		break;
	case STV0900_DEMOD_2:
		stv0900_write_reg(i_params, R0900_P2_AGC2REF, 0x38);
		stv0900_write_bits(i_params, F0900_P2_SCAN_ENABLE, 1);
		stv0900_write_bits(i_params, F0900_P2_CFR_AUTOSCAN, 1);
		stv0900_write_reg(i_params, R0900_P2_SFRUP1, 0x83);
		stv0900_write_reg(i_params, R0900_P2_SFRUP0, 0xc0);
		stv0900_write_reg(i_params, R0900_P2_SFRLOW1, 0x82);
		stv0900_write_reg(i_params, R0900_P2_SFRLOW0, 0xa0);
		stv0900_write_reg(i_params, R0900_P2_DMDT0M, 0x0);
		stv0900_set_symbol_rate(i_params, i_params->mclk, 1000000, demod);
		nb_steps = -1 + (i_params->dmd2_srch_range / 1000000);
		nb_steps /= 2;
		nb_steps = (2 * nb_steps) + 1;

		if (nb_steps < 0)
			nb_steps = 1;

		direction = 1;
		freq_step = (1000000 << 8) / (i_params->mclk >> 8);
		init_freq = 0;
		for (i = 0; i < nb_steps; i++) {
			if (direction > 0)
				init_freq = init_freq + (freq_step * i);
			else
				init_freq = init_freq - (freq_step * i);

			direction *= -1;

			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x5C);
			stv0900_write_reg(i_params, R0900_P2_CFRINIT1, (init_freq >> 8) & 0xff);
			stv0900_write_reg(i_params, R0900_P2_CFRINIT0, init_freq  & 0xff);
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x58);

			msleep(10);
			agc2level = 0;
			for (j = 0; j < 10; j++)
				agc2level += (stv0900_read_reg(i_params, R0900_P2_AGC2I1) << 8)
						| stv0900_read_reg(i_params, R0900_P2_AGC2I0);

			agc2level /= 10;

			if (agc2level < minagc2level)
				minagc2level = agc2level;
		}
		break;
	}

	return (u16)minagc2level;
}

static u32 stv0900_search_srate_coarse(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	int timingLock = FALSE;
	s32 i, timingcpt = 0,
		direction = 1,
		nb_steps,
		current_step = 0,
		tuner_freq;

	u32 coarse_srate = 0, agc2_integr = 0, currier_step = 1200;

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		stv0900_write_bits(i_params, F0900_P1_I2C_DEMOD_MODE, 0x1F);
		stv0900_write_reg(i_params, R0900_P1_TMGCFG, 0x12);
		stv0900_write_reg(i_params, R0900_P1_TMGTHRISE, 0xf0);
		stv0900_write_reg(i_params, R0900_P1_TMGTHFALL, 0xe0);
		stv0900_write_bits(i_params, F0900_P1_SCAN_ENABLE, 1);
		stv0900_write_bits(i_params, F0900_P1_CFR_AUTOSCAN, 1);
		stv0900_write_reg(i_params, R0900_P1_SFRUP1, 0x83);
		stv0900_write_reg(i_params, R0900_P1_SFRUP0, 0xc0);
		stv0900_write_reg(i_params, R0900_P1_SFRLOW1, 0x82);
		stv0900_write_reg(i_params, R0900_P1_SFRLOW0, 0xa0);
		stv0900_write_reg(i_params, R0900_P1_DMDT0M, 0x0);
		stv0900_write_reg(i_params, R0900_P1_AGC2REF, 0x50);

		if (i_params->chip_id >= 0x20) {
			stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0x6a);
			stv0900_write_reg(i_params, R0900_P1_SFRSTEP, 0x95);
		} else {
			stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0xed);
			stv0900_write_reg(i_params, R0900_P1_SFRSTEP, 0x73);
		}

		if (i_params->dmd1_symbol_rate <= 2000000)
			currier_step = 1000;
		else if (i_params->dmd1_symbol_rate <= 5000000)
			currier_step = 2000;
		else if (i_params->dmd1_symbol_rate <= 12000000)
			currier_step = 3000;
		else
			currier_step = 5000;

		nb_steps = -1 + ((i_params->dmd1_srch_range / 1000) / currier_step);
		nb_steps /= 2;
		nb_steps = (2 * nb_steps) + 1;

		if (nb_steps < 0)
			nb_steps = 1;

		else if (nb_steps > 10) {
			nb_steps = 11;
			currier_step = (i_params->dmd1_srch_range / 1000) / 10;
		}

		current_step = 0;

		direction = 1;
		tuner_freq = i_params->tuner1_freq;

		while ((timingLock == FALSE) && (current_step < nb_steps)) {
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x5F);
			stv0900_write_bits(i_params, F0900_P1_I2C_DEMOD_MODE, 0x0);

			msleep(50);

			for (i = 0; i < 10; i++) {
				if (stv0900_get_bits(i_params, F0900_P1_TMGLOCK_QUALITY) >= 2)
					timingcpt++;

				agc2_integr += (stv0900_read_reg(i_params, R0900_P1_AGC2I1) << 8) | stv0900_read_reg(i_params, R0900_P1_AGC2I0);

			}

			agc2_integr /= 10;
			coarse_srate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);
			current_step++;
			direction *= -1;

			dprintk("lock: I2C_DEMOD_MODE_FIELD =0. Search started. tuner freq=%d agc2=0x%x srate_coarse=%d tmg_cpt=%d\n", tuner_freq, agc2_integr, coarse_srate, timingcpt);

			if ((timingcpt >= 5) && (agc2_integr < 0x1F00) && (coarse_srate < 55000000) && (coarse_srate > 850000)) {
				timingLock = TRUE;
			}

			else if (current_step < nb_steps) {
				if (direction > 0)
					tuner_freq += (current_step * currier_step);
				else
					tuner_freq -= (current_step * currier_step);

				stv0900_set_tuner(fe, tuner_freq, i_params->tuner1_bw);
			}
		}

		if (timingLock == FALSE)
			coarse_srate = 0;
		else
			coarse_srate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);
		break;
	case STV0900_DEMOD_2:
		stv0900_write_bits(i_params, F0900_P2_I2C_DEMOD_MODE, 0x1F);
		stv0900_write_reg(i_params, R0900_P2_TMGCFG, 0x12);
		stv0900_write_reg(i_params, R0900_P2_TMGTHRISE, 0xf0);
		stv0900_write_reg(i_params, R0900_P2_TMGTHFALL, 0xe0);
		stv0900_write_bits(i_params, F0900_P2_SCAN_ENABLE, 1);
		stv0900_write_bits(i_params, F0900_P2_CFR_AUTOSCAN, 1);
		stv0900_write_reg(i_params, R0900_P2_SFRUP1, 0x83);
		stv0900_write_reg(i_params, R0900_P2_SFRUP0, 0xc0);
		stv0900_write_reg(i_params, R0900_P2_SFRLOW1, 0x82);
		stv0900_write_reg(i_params, R0900_P2_SFRLOW0, 0xa0);
		stv0900_write_reg(i_params, R0900_P2_DMDT0M, 0x0);
		stv0900_write_reg(i_params, R0900_P2_AGC2REF, 0x50);

		if (i_params->chip_id >= 0x20) {
			stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0x6a);
			stv0900_write_reg(i_params, R0900_P2_SFRSTEP, 0x95);
		} else {
			stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0xed);
			stv0900_write_reg(i_params, R0900_P2_SFRSTEP, 0x73);
		}

		if (i_params->dmd2_symbol_rate <= 2000000)
			currier_step = 1000;
		else if (i_params->dmd2_symbol_rate <= 5000000)
			currier_step = 2000;
		else if (i_params->dmd2_symbol_rate <= 12000000)
			currier_step = 3000;
		else
			currier_step = 5000;


		nb_steps = -1 + ((i_params->dmd2_srch_range / 1000) / currier_step);
		nb_steps /= 2;
		nb_steps = (2 * nb_steps) + 1;

		if (nb_steps < 0)
			nb_steps = 1;
		else if (nb_steps > 10) {
			nb_steps = 11;
			currier_step = (i_params->dmd2_srch_range / 1000) / 10;
		}

		current_step = 0;
		direction = 1;
		tuner_freq = i_params->tuner2_freq;

		while ((timingLock == FALSE) && (current_step < nb_steps)) {
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x5F);
			stv0900_write_bits(i_params, F0900_P2_I2C_DEMOD_MODE, 0x0);

			msleep(50);
			timingcpt = 0;

			for (i = 0; i < 20; i++) {
				if (stv0900_get_bits(i_params, F0900_P2_TMGLOCK_QUALITY) >= 2)
					timingcpt++;
				agc2_integr += (stv0900_read_reg(i_params, R0900_P2_AGC2I1) << 8)
								| stv0900_read_reg(i_params, R0900_P2_AGC2I0);
			}

			agc2_integr /= 20;
			coarse_srate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);
			if ((timingcpt >= 10) && (agc2_integr < 0x1F00) && (coarse_srate < 55000000) && (coarse_srate > 850000))
				timingLock = TRUE;
			else {
				current_step++;
				direction *= -1;

				if (direction > 0)
					tuner_freq += (current_step * currier_step);
				else
					tuner_freq -= (current_step * currier_step);

				stv0900_set_tuner(fe, tuner_freq, i_params->tuner2_bw);
			}
		}

		if (timingLock == FALSE)
			coarse_srate = 0;
		else
			coarse_srate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);
		break;
	}

	return coarse_srate;
}

static u32 stv0900_search_srate_fine(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	u32 coarse_srate,
	coarse_freq,
	symb;

	coarse_srate = stv0900_get_symbol_rate(i_params, i_params->mclk, demod);

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		coarse_freq = (stv0900_read_reg(i_params, R0900_P1_CFR2) << 8)
						| stv0900_read_reg(i_params, R0900_P1_CFR1);
		symb = 13 * (coarse_srate / 10);

		if (symb < i_params->dmd1_symbol_rate)
			coarse_srate = 0;
		else {
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1F);
			stv0900_write_reg(i_params, R0900_P1_TMGCFG2, 0x01);
			stv0900_write_reg(i_params, R0900_P1_TMGTHRISE, 0x20);
			stv0900_write_reg(i_params, R0900_P1_TMGTHFALL, 0x00);
			stv0900_write_reg(i_params, R0900_P1_TMGCFG, 0xd2);
			stv0900_write_bits(i_params, F0900_P1_CFR_AUTOSCAN, 0);

			if (i_params->chip_id >= 0x20)
				stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0x49);
			else
				stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0xed);

			if (coarse_srate > 3000000) {
				symb = 13 * (coarse_srate / 10);
				symb = (symb / 1000) * 65536;
				symb /= (i_params->mclk / 1000);
				stv0900_write_reg(i_params, R0900_P1_SFRUP1, (symb >> 8) & 0x7F);
				stv0900_write_reg(i_params, R0900_P1_SFRUP0, (symb & 0xFF));

				symb = 10 * (coarse_srate / 13);
				symb = (symb / 1000) * 65536;
				symb /= (i_params->mclk / 1000);

				stv0900_write_reg(i_params, R0900_P1_SFRLOW1, (symb >> 8) & 0x7F);
				stv0900_write_reg(i_params, R0900_P1_SFRLOW0, (symb & 0xFF));

				symb = (coarse_srate / 1000) * 65536;
				symb /= (i_params->mclk / 1000);
				stv0900_write_reg(i_params, R0900_P1_SFRINIT1, (symb >> 8) & 0xFF);
				stv0900_write_reg(i_params, R0900_P1_SFRINIT0, (symb & 0xFF));
			} else {
				symb = 13 * (coarse_srate / 10);
				symb = (symb / 100) * 65536;
				symb /= (i_params->mclk / 100);
				stv0900_write_reg(i_params, R0900_P1_SFRUP1, (symb >> 8) & 0x7F);
				stv0900_write_reg(i_params, R0900_P1_SFRUP0, (symb & 0xFF));

				symb = 10 * (coarse_srate / 14);
				symb = (symb / 100) * 65536;
				symb /= (i_params->mclk / 100);
				stv0900_write_reg(i_params, R0900_P1_SFRLOW1, (symb >> 8) & 0x7F);
				stv0900_write_reg(i_params, R0900_P1_SFRLOW0, (symb & 0xFF));

				symb = (coarse_srate / 100) * 65536;
				symb /= (i_params->mclk / 100);
				stv0900_write_reg(i_params, R0900_P1_SFRINIT1, (symb >> 8) & 0xFF);
				stv0900_write_reg(i_params, R0900_P1_SFRINIT0, (symb & 0xFF));
			}

			stv0900_write_reg(i_params, R0900_P1_DMDT0M, 0x20);
			stv0900_write_reg(i_params, R0900_P1_CFRINIT1, (coarse_freq >> 8) & 0xff);
			stv0900_write_reg(i_params, R0900_P1_CFRINIT0, coarse_freq  & 0xff);
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x15);
		}
		break;
	case STV0900_DEMOD_2:
		coarse_freq = (stv0900_read_reg(i_params, R0900_P2_CFR2) << 8)
						| stv0900_read_reg(i_params, R0900_P2_CFR1);

		symb = 13 * (coarse_srate / 10);

		if (symb < i_params->dmd2_symbol_rate)
			coarse_srate = 0;
		else {
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1F);
			stv0900_write_reg(i_params, R0900_P2_TMGCFG2, 0x01);
			stv0900_write_reg(i_params, R0900_P2_TMGTHRISE, 0x20);
			stv0900_write_reg(i_params, R0900_P2_TMGTHFALL, 0x00);
			stv0900_write_reg(i_params, R0900_P2_TMGCFG, 0xd2);
			stv0900_write_bits(i_params, F0900_P2_CFR_AUTOSCAN, 0);

			if (i_params->chip_id >= 0x20)
				stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0x49);
			else
				stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0xed);

			if (coarse_srate > 3000000) {
				symb = 13 * (coarse_srate / 10);
				symb = (symb / 1000) * 65536;
				symb /= (i_params->mclk / 1000);
				stv0900_write_reg(i_params, R0900_P2_SFRUP1, (symb >> 8) & 0x7F);
				stv0900_write_reg(i_params, R0900_P2_SFRUP0, (symb & 0xFF));

				symb = 10 * (coarse_srate / 13);
				symb = (symb / 1000) * 65536;
				symb /= (i_params->mclk / 1000);

				stv0900_write_reg(i_params, R0900_P2_SFRLOW1, (symb >> 8) & 0x7F);
				stv0900_write_reg(i_params, R0900_P2_SFRLOW0, (symb & 0xFF));

				symb = (coarse_srate / 1000) * 65536;
				symb /= (i_params->mclk / 1000);
				stv0900_write_reg(i_params, R0900_P2_SFRINIT1, (symb >> 8) & 0xFF);
				stv0900_write_reg(i_params, R0900_P2_SFRINIT0, (symb & 0xFF));
			} else {
				symb = 13 * (coarse_srate / 10);
				symb = (symb / 100) * 65536;
				symb /= (i_params->mclk / 100);
				stv0900_write_reg(i_params, R0900_P2_SFRUP1, (symb >> 8) & 0x7F);
				stv0900_write_reg(i_params, R0900_P2_SFRUP0, (symb & 0xFF));

				symb = 10 * (coarse_srate / 14);
				symb = (symb / 100) * 65536;
				symb /= (i_params->mclk / 100);
				stv0900_write_reg(i_params, R0900_P2_SFRLOW1, (symb >> 8) & 0x7F);
				stv0900_write_reg(i_params, R0900_P2_SFRLOW0, (symb & 0xFF));

				symb = (coarse_srate / 100) * 65536;
				symb /= (i_params->mclk / 100);
				stv0900_write_reg(i_params, R0900_P2_SFRINIT1, (symb >> 8) & 0xFF);
				stv0900_write_reg(i_params, R0900_P2_SFRINIT0, (symb & 0xFF));
			}

			stv0900_write_reg(i_params, R0900_P2_DMDT0M, 0x20);
			stv0900_write_reg(i_params, R0900_P2_CFRINIT1, (coarse_freq >> 8) & 0xff);
			stv0900_write_reg(i_params, R0900_P2_CFRINIT0, coarse_freq  & 0xff);
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x15);
		}

		break;
	}

	return coarse_srate;
}

static int stv0900_blind_search_algo(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	u8 k_ref_tmg, k_ref_tmg_max, k_ref_tmg_min;
	u32 coarse_srate;
	int lock = FALSE, coarse_fail = FALSE;
	s32 demod_timeout = 500, fec_timeout = 50, kref_tmg_reg, fail_cpt, i, agc2_overflow;
	u16 agc2_integr;
	u8 dstatus2;

	dprintk(KERN_INFO "%s\n", __func__);

	if (i_params->chip_id < 0x20) {
		k_ref_tmg_max = 233;
		k_ref_tmg_min = 143;
	} else {
		k_ref_tmg_max = 120;
		k_ref_tmg_min = 30;
	}

	agc2_integr = stv0900_blind_check_agc2_min_level(i_params, demod);

	if (agc2_integr > STV0900_BLIND_SEARCH_AGC2_TH) {
		lock = FALSE;

	} else {
		switch (demod) {
		case STV0900_DEMOD_1:
		default:
			if (i_params->chip_id == 0x10)
				stv0900_write_reg(i_params, R0900_P1_CORRELEXP, 0xAA);

			if (i_params->chip_id < 0x20)
				stv0900_write_reg(i_params, R0900_P1_CARHDR, 0x55);

			stv0900_write_reg(i_params, R0900_P1_CARCFG, 0xC4);
			stv0900_write_reg(i_params, R0900_P1_RTCS2, 0x44);

			if (i_params->chip_id >= 0x20) {
				stv0900_write_reg(i_params, R0900_P1_EQUALCFG, 0x41);
				stv0900_write_reg(i_params, R0900_P1_FFECFG, 0x41);
				stv0900_write_reg(i_params, R0900_P1_VITSCALE, 0x82);
				stv0900_write_reg(i_params, R0900_P1_VAVSRVIT, 0x0);
			}

			kref_tmg_reg = R0900_P1_KREFTMG;
			break;
		case STV0900_DEMOD_2:
			if (i_params->chip_id == 0x10)
				stv0900_write_reg(i_params, R0900_P2_CORRELEXP, 0xAA);

			if (i_params->chip_id < 0x20)
				stv0900_write_reg(i_params, R0900_P2_CARHDR, 0x55);

			stv0900_write_reg(i_params, R0900_P2_CARCFG, 0xC4);
			stv0900_write_reg(i_params, R0900_P2_RTCS2, 0x44);

			if (i_params->chip_id >= 0x20) {
				stv0900_write_reg(i_params, R0900_P2_EQUALCFG, 0x41);
				stv0900_write_reg(i_params, R0900_P2_FFECFG, 0x41);
				stv0900_write_reg(i_params, R0900_P2_VITSCALE, 0x82);
				stv0900_write_reg(i_params, R0900_P2_VAVSRVIT, 0x0);
			}

			kref_tmg_reg = R0900_P2_KREFTMG;
			break;
		}

		k_ref_tmg = k_ref_tmg_max;

		do {
			stv0900_write_reg(i_params, kref_tmg_reg, k_ref_tmg);
			if (stv0900_search_srate_coarse(fe) != 0) {
				coarse_srate = stv0900_search_srate_fine(fe);

				if (coarse_srate != 0) {
					stv0900_get_lock_timeout(&demod_timeout, &fec_timeout, coarse_srate, STV0900_BLIND_SEARCH);
					lock = stv0900_get_demod_lock(i_params, demod, demod_timeout);
				} else
					lock = FALSE;
			} else {
				fail_cpt = 0;
				agc2_overflow = 0;

				switch (demod) {
				case STV0900_DEMOD_1:
				default:
					for (i = 0; i < 10; i++) {
						agc2_integr = (stv0900_read_reg(i_params, R0900_P1_AGC2I1) << 8)
								| stv0900_read_reg(i_params, R0900_P1_AGC2I0);

						if (agc2_integr >= 0xff00)
							agc2_overflow++;

						dstatus2 = stv0900_read_reg(i_params, R0900_P1_DSTATUS2);

						if (((dstatus2 & 0x1) == 0x1) && ((dstatus2 >> 7) == 1))
							fail_cpt++;
					}
					break;
				case STV0900_DEMOD_2:
					for (i = 0; i < 10; i++) {
						agc2_integr = (stv0900_read_reg(i_params, R0900_P2_AGC2I1) << 8)
								| stv0900_read_reg(i_params, R0900_P2_AGC2I0);

						if (agc2_integr >= 0xff00)
							agc2_overflow++;

						dstatus2 = stv0900_read_reg(i_params, R0900_P2_DSTATUS2);

						if (((dstatus2 & 0x1) == 0x1) && ((dstatus2 >> 7) == 1))
							fail_cpt++;
					}
					break;
				}

				if ((fail_cpt > 7) || (agc2_overflow > 7))
					coarse_fail = TRUE;

				lock = FALSE;
			}
			k_ref_tmg -= 30;
		} while ((k_ref_tmg >= k_ref_tmg_min) && (lock == FALSE) && (coarse_fail == FALSE));
	}

	return lock;
}

static void stv0900_set_viterbi_acq(struct stv0900_internal *i_params,
					enum fe_stv0900_demod_num demod)
{
	s32 vth_reg;

	dprintk(KERN_INFO "%s\n", __func__);

	dmd_reg(vth_reg, R0900_P1_VTH12, R0900_P2_VTH12);

	stv0900_write_reg(i_params, vth_reg++, 0x96);
	stv0900_write_reg(i_params, vth_reg++, 0x64);
	stv0900_write_reg(i_params, vth_reg++, 0x36);
	stv0900_write_reg(i_params, vth_reg++, 0x23);
	stv0900_write_reg(i_params, vth_reg++, 0x1E);
	stv0900_write_reg(i_params, vth_reg++, 0x19);
}

static void stv0900_set_search_standard(struct stv0900_internal *i_params,
					enum fe_stv0900_demod_num demod)
{

	int sstndrd;

	dprintk(KERN_INFO "%s\n", __func__);

	sstndrd = i_params->dmd1_srch_standard;
	if (demod == 1)
		sstndrd = i_params->dmd2_srch_stndrd;

	switch (sstndrd) {
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

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		switch (i_params->dmd1_srch_standard) {
		case STV0900_SEARCH_DVBS1:
		case STV0900_SEARCH_DSS:
			stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 0);

			stv0900_write_bits(i_params, F0900_STOP_CLKVIT1, 0);
			stv0900_write_reg(i_params, R0900_P1_ACLC, 0x1a);
			stv0900_write_reg(i_params, R0900_P1_BCLC, 0x09);
			stv0900_write_reg(i_params, R0900_P1_CAR2CFG, 0x22);

			stv0900_set_viterbi_acq(i_params, demod);
			stv0900_set_viterbi_standard(i_params,
						i_params->dmd1_srch_standard,
						i_params->dmd1_fec, demod);

			break;
		case STV0900_SEARCH_DVBS2:
			stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_STOP_CLKVIT1, 1);
			stv0900_write_reg(i_params, R0900_P1_ACLC, 0x1a);
			stv0900_write_reg(i_params, R0900_P1_BCLC, 0x09);
			stv0900_write_reg(i_params, R0900_P1_CAR2CFG, 0x26);
			if (i_params->demod_mode != STV0900_SINGLE) {
				if (i_params->chip_id <= 0x11)
					stv0900_stop_all_s2_modcod(i_params, demod);
				else
					stv0900_activate_s2_modcode(i_params, demod);

			} else
				stv0900_activate_s2_modcode_single(i_params, demod);

			stv0900_set_viterbi_tracq(i_params, demod);

			break;
		case STV0900_AUTO_SEARCH:
		default:
			stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P1_DVBS1_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_P1_DVBS2_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_STOP_CLKVIT1, 0);
			stv0900_write_reg(i_params, R0900_P1_ACLC, 0x1a);
			stv0900_write_reg(i_params, R0900_P1_BCLC, 0x09);
			stv0900_write_reg(i_params, R0900_P1_CAR2CFG, 0x26);
			if (i_params->demod_mode != STV0900_SINGLE) {
				if (i_params->chip_id <= 0x11)
					stv0900_stop_all_s2_modcod(i_params, demod);
				else
					stv0900_activate_s2_modcode(i_params, demod);

			} else
				stv0900_activate_s2_modcode_single(i_params, demod);

			if (i_params->dmd1_symbol_rate >= 2000000)
				stv0900_set_viterbi_acq(i_params, demod);
			else
				stv0900_set_viterbi_tracq(i_params, demod);

			stv0900_set_viterbi_standard(i_params, i_params->dmd1_srch_standard, i_params->dmd1_fec, demod);

			break;
		}
		break;
	case STV0900_DEMOD_2:
		switch (i_params->dmd2_srch_stndrd) {
		case STV0900_SEARCH_DVBS1:
		case STV0900_SEARCH_DSS:
			stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_STOP_CLKVIT2, 0);
			stv0900_write_reg(i_params, R0900_P2_ACLC, 0x1a);
			stv0900_write_reg(i_params, R0900_P2_BCLC, 0x09);
			stv0900_write_reg(i_params, R0900_P2_CAR2CFG, 0x22);
			stv0900_set_viterbi_acq(i_params, demod);
			stv0900_set_viterbi_standard(i_params, i_params->dmd2_srch_stndrd, i_params->dmd2_fec, demod);
			break;
		case STV0900_SEARCH_DVBS2:
			stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_STOP_CLKVIT2, 1);
			stv0900_write_reg(i_params, R0900_P2_ACLC, 0x1a);
			stv0900_write_reg(i_params, R0900_P2_BCLC, 0x09);
			stv0900_write_reg(i_params, R0900_P2_CAR2CFG, 0x26);
			if (i_params->demod_mode != STV0900_SINGLE)
				stv0900_activate_s2_modcode(i_params, demod);
			else
				stv0900_activate_s2_modcode_single(i_params, demod);

			stv0900_set_viterbi_tracq(i_params, demod);
			break;
		case STV0900_AUTO_SEARCH:
		default:
			stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 0);
			stv0900_write_bits(i_params, F0900_P2_DVBS1_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_P2_DVBS2_ENABLE, 1);
			stv0900_write_bits(i_params, F0900_STOP_CLKVIT2, 0);
			stv0900_write_reg(i_params, R0900_P2_ACLC, 0x1a);
			stv0900_write_reg(i_params, R0900_P2_BCLC, 0x09);
			stv0900_write_reg(i_params, R0900_P2_CAR2CFG, 0x26);
			if (i_params->demod_mode != STV0900_SINGLE)
				stv0900_activate_s2_modcode(i_params, demod);
			else
				stv0900_activate_s2_modcode_single(i_params, demod);

			if (i_params->dmd2_symbol_rate >= 2000000)
				stv0900_set_viterbi_acq(i_params, demod);
			else
				stv0900_set_viterbi_tracq(i_params, demod);

			stv0900_set_viterbi_standard(i_params, i_params->dmd2_srch_stndrd, i_params->dmd2_fec, demod);

			break;
		}

		break;
	}
}

enum fe_stv0900_signal_type stv0900_algo(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	s32 demod_timeout = 500, fec_timeout = 50, stream_merger_field;

	int lock = FALSE, low_sr = FALSE;

	enum fe_stv0900_signal_type signal_type = STV0900_NOCARRIER;
	enum fe_stv0900_search_algo algo;
	int no_signal = FALSE;

	dprintk(KERN_INFO "%s\n", __func__);

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		algo = i_params->dmd1_srch_algo;

		stv0900_write_bits(i_params, F0900_P1_RST_HWARE, 1);
		stream_merger_field = F0900_P1_RST_HWARE;

		stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x5C);

		if (i_params->chip_id >= 0x20)
			stv0900_write_reg(i_params, R0900_P1_CORRELABS, 0x9e);
		else
			stv0900_write_reg(i_params, R0900_P1_CORRELABS, 0x88);

		stv0900_get_lock_timeout(&demod_timeout, &fec_timeout, i_params->dmd1_symbol_rate, i_params->dmd1_srch_algo);

		if (i_params->dmd1_srch_algo == STV0900_BLIND_SEARCH) {
			i_params->tuner1_bw = 2 * 36000000;

			stv0900_write_reg(i_params, R0900_P1_TMGCFG2, 0x00);
			stv0900_write_reg(i_params, R0900_P1_CORRELMANT, 0x70);

			stv0900_set_symbol_rate(i_params, i_params->mclk, 1000000, demod);
		} else {
			stv0900_write_reg(i_params, R0900_P1_DMDT0M, 0x20);
			stv0900_write_reg(i_params, R0900_P1_TMGCFG, 0xd2);

			if (i_params->dmd1_symbol_rate < 2000000)
				stv0900_write_reg(i_params, R0900_P1_CORRELMANT, 0x63);
			else
				stv0900_write_reg(i_params, R0900_P1_CORRELMANT, 0x70);

			stv0900_write_reg(i_params, R0900_P1_AGC2REF, 0x38);
			if (i_params->chip_id >= 0x20) {
				stv0900_write_reg(i_params, R0900_P1_KREFTMG, 0x5a);

				if (i_params->dmd1_srch_algo == STV0900_COLD_START)
					i_params->tuner1_bw = (15 * (stv0900_carrier_width(i_params->dmd1_symbol_rate, i_params->rolloff) + 10000000)) / 10;
				else if (i_params->dmd1_srch_algo == STV0900_WARM_START)
					i_params->tuner1_bw = stv0900_carrier_width(i_params->dmd1_symbol_rate, i_params->rolloff) + 10000000;
			} else {
				stv0900_write_reg(i_params, R0900_P1_KREFTMG, 0xc1);
				i_params->tuner1_bw = (15 * (stv0900_carrier_width(i_params->dmd1_symbol_rate, i_params->rolloff) + 10000000)) / 10;
			}

			stv0900_write_reg(i_params, R0900_P1_TMGCFG2, 0x01);

			stv0900_set_symbol_rate(i_params, i_params->mclk, i_params->dmd1_symbol_rate, demod);
			stv0900_set_max_symbol_rate(i_params, i_params->mclk, i_params->dmd1_symbol_rate, demod);
			stv0900_set_min_symbol_rate(i_params, i_params->mclk, i_params->dmd1_symbol_rate, demod);
			if (i_params->dmd1_symbol_rate >= 10000000)
				low_sr = FALSE;
			else
				low_sr = TRUE;

		}

		stv0900_set_tuner(fe, i_params->tuner1_freq, i_params->tuner1_bw);

		stv0900_write_bits(i_params, F0900_P1_SPECINV_CONTROL, i_params->dmd1_srch_iq_inv);
		stv0900_write_bits(i_params, F0900_P1_MANUAL_ROLLOFF, 1);

		stv0900_set_search_standard(i_params, demod);

		if (i_params->dmd1_srch_algo != STV0900_BLIND_SEARCH)
			stv0900_start_search(i_params, demod);
		break;
	case STV0900_DEMOD_2:
		algo = i_params->dmd2_srch_algo;

		stv0900_write_bits(i_params, F0900_P2_RST_HWARE, 1);

		stream_merger_field = F0900_P2_RST_HWARE;

		stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x5C);

		if (i_params->chip_id >= 0x20)
			stv0900_write_reg(i_params, R0900_P2_CORRELABS, 0x9e);
		else
			stv0900_write_reg(i_params, R0900_P2_CORRELABS, 0x88);

		stv0900_get_lock_timeout(&demod_timeout, &fec_timeout, i_params->dmd2_symbol_rate, i_params->dmd2_srch_algo);

		if (i_params->dmd2_srch_algo == STV0900_BLIND_SEARCH) {
			i_params->tuner2_bw = 2 * 36000000;

			stv0900_write_reg(i_params, R0900_P2_TMGCFG2, 0x00);
			stv0900_write_reg(i_params, R0900_P2_CORRELMANT, 0x70);

			stv0900_set_symbol_rate(i_params, i_params->mclk, 1000000, demod);
		} else {
			stv0900_write_reg(i_params, R0900_P2_DMDT0M, 0x20);
			stv0900_write_reg(i_params, R0900_P2_TMGCFG, 0xd2);

			if (i_params->dmd2_symbol_rate < 2000000)
				stv0900_write_reg(i_params, R0900_P2_CORRELMANT, 0x63);
			else
				stv0900_write_reg(i_params, R0900_P2_CORRELMANT, 0x70);

			if (i_params->dmd2_symbol_rate >= 10000000)
				stv0900_write_reg(i_params, R0900_P2_AGC2REF, 0x38);
			else
				stv0900_write_reg(i_params, R0900_P2_AGC2REF, 0x60);

			if (i_params->chip_id >= 0x20) {
				stv0900_write_reg(i_params, R0900_P2_KREFTMG, 0x5a);

				if (i_params->dmd2_srch_algo == STV0900_COLD_START)
					i_params->tuner2_bw = (15 * (stv0900_carrier_width(i_params->dmd2_symbol_rate,
							i_params->rolloff) + 10000000)) / 10;
				else if (i_params->dmd2_srch_algo == STV0900_WARM_START)
					i_params->tuner2_bw = stv0900_carrier_width(i_params->dmd2_symbol_rate,
							i_params->rolloff) + 10000000;
			} else {
				stv0900_write_reg(i_params, R0900_P2_KREFTMG, 0xc1);
				i_params->tuner2_bw = (15 * (stv0900_carrier_width(i_params->dmd2_symbol_rate,
									i_params->rolloff) + 10000000)) / 10;
			}

			stv0900_write_reg(i_params, R0900_P2_TMGCFG2, 0x01);

			stv0900_set_symbol_rate(i_params, i_params->mclk, i_params->dmd2_symbol_rate, demod);
			stv0900_set_max_symbol_rate(i_params, i_params->mclk, i_params->dmd2_symbol_rate, demod);
			stv0900_set_min_symbol_rate(i_params, i_params->mclk, i_params->dmd2_symbol_rate, demod);
			if (i_params->dmd2_symbol_rate >= 10000000)
				low_sr = FALSE;
			else
				low_sr = TRUE;

		}

		stv0900_set_tuner(fe, i_params->tuner2_freq, i_params->tuner2_bw);

		stv0900_write_bits(i_params, F0900_P2_SPECINV_CONTROL, i_params->dmd2_srch_iq_inv);
		stv0900_write_bits(i_params, F0900_P2_MANUAL_ROLLOFF, 1);

		stv0900_set_search_standard(i_params, demod);

		if (i_params->dmd2_srch_algo != STV0900_BLIND_SEARCH)
			stv0900_start_search(i_params, demod);
		break;
	}

	if (i_params->chip_id == 0x12) {
		stv0900_write_bits(i_params, stream_merger_field, 0);
		msleep(3);
		stv0900_write_bits(i_params, stream_merger_field, 1);
		stv0900_write_bits(i_params, stream_merger_field, 0);
	}

	if (algo == STV0900_BLIND_SEARCH)
		lock = stv0900_blind_search_algo(fe);
	else if (algo == STV0900_COLD_START)
		lock = stv0900_get_demod_cold_lock(fe, demod_timeout);
	else if (algo == STV0900_WARM_START)
		lock = stv0900_get_demod_lock(i_params, demod, demod_timeout);

	if ((lock == FALSE) && (algo == STV0900_COLD_START)) {
		if (low_sr == FALSE) {
			if (stv0900_check_timing_lock(i_params, demod) == TRUE)
				lock = stv0900_sw_algo(i_params, demod);
		}
	}

	if (lock == TRUE)
		signal_type = stv0900_get_signal_params(fe);

	if ((lock == TRUE) && (signal_type == STV0900_RANGEOK)) {
		stv0900_track_optimization(fe);
		if (i_params->chip_id <= 0x11) {
			if ((stv0900_get_standard(fe, STV0900_DEMOD_1) == STV0900_DVBS1_STANDARD) && (stv0900_get_standard(fe, STV0900_DEMOD_2) == STV0900_DVBS1_STANDARD)) {
				msleep(20);
				stv0900_write_bits(i_params, stream_merger_field, 0);
			} else {
				stv0900_write_bits(i_params, stream_merger_field, 0);
				msleep(3);
				stv0900_write_bits(i_params, stream_merger_field, 1);
				stv0900_write_bits(i_params, stream_merger_field, 0);
			}
		} else if (i_params->chip_id == 0x20) {
			stv0900_write_bits(i_params, stream_merger_field, 0);
			msleep(3);
			stv0900_write_bits(i_params, stream_merger_field, 1);
			stv0900_write_bits(i_params, stream_merger_field, 0);
		}

		if (stv0900_wait_for_lock(i_params, demod, fec_timeout, fec_timeout) == TRUE) {
			lock = TRUE;
			switch (demod) {
			case STV0900_DEMOD_1:
			default:
				i_params->dmd1_rslts.locked = TRUE;
				if (i_params->dmd1_rslts.standard == STV0900_DVBS2_STANDARD) {
					stv0900_set_dvbs2_rolloff(i_params, demod);
					stv0900_write_reg(i_params, R0900_P1_PDELCTRL2, 0x40);
					stv0900_write_reg(i_params, R0900_P1_PDELCTRL2, 0);
					stv0900_write_reg(i_params, R0900_P1_ERRCTRL1, 0x67);
				} else {
					stv0900_write_reg(i_params, R0900_P1_ERRCTRL1, 0x75);
				}

				stv0900_write_reg(i_params, R0900_P1_FBERCPT4, 0);
				stv0900_write_reg(i_params, R0900_P1_ERRCTRL2, 0xc1);
				break;
			case STV0900_DEMOD_2:
				i_params->dmd2_rslts.locked = TRUE;

				if (i_params->dmd2_rslts.standard == STV0900_DVBS2_STANDARD) {
					stv0900_set_dvbs2_rolloff(i_params, demod);
					stv0900_write_reg(i_params, R0900_P2_PDELCTRL2, 0x60);
					stv0900_write_reg(i_params, R0900_P2_PDELCTRL2, 0x20);
					stv0900_write_reg(i_params, R0900_P2_ERRCTRL1, 0x67);
				} else {
					stv0900_write_reg(i_params, R0900_P2_ERRCTRL1, 0x75);
				}

				stv0900_write_reg(i_params, R0900_P2_FBERCPT4, 0);

				stv0900_write_reg(i_params, R0900_P2_ERRCTRL2, 0xc1);
				break;
			}
		} else {
			lock = FALSE;
			signal_type = STV0900_NODATA;
			no_signal = stv0900_check_signal_presence(i_params, demod);

			switch (demod) {
			case STV0900_DEMOD_1:
			default:
				i_params->dmd1_rslts.locked = FALSE;
				break;
			case STV0900_DEMOD_2:
				i_params->dmd2_rslts.locked = FALSE;
				break;
			}
		}
	}

	if ((signal_type == STV0900_NODATA) && (no_signal == FALSE)) {
		switch (demod) {
		case STV0900_DEMOD_1:
		default:
			if (i_params->chip_id <= 0x11) {
				if ((stv0900_get_bits(i_params, F0900_P1_HEADER_MODE) == STV0900_DVBS_FOUND) &&
						(i_params->dmd1_srch_iq_inv <= STV0900_IQ_AUTO_NORMAL_FIRST))
					signal_type = stv0900_dvbs1_acq_workaround(fe);
			} else
				i_params->dmd1_rslts.locked = FALSE;

			break;
		case STV0900_DEMOD_2:
			if (i_params->chip_id <= 0x11) {
				if ((stv0900_get_bits(i_params, F0900_P2_HEADER_MODE) == STV0900_DVBS_FOUND) &&
						(i_params->dmd2_srch_iq_inv <= STV0900_IQ_AUTO_NORMAL_FIRST))
					signal_type = stv0900_dvbs1_acq_workaround(fe);
			} else
				i_params->dmd2_rslts.locked = FALSE;
			break;
		}
	}

	return signal_type;
}

