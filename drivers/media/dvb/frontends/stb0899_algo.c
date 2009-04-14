/*
	STB0899 Multistandard Frontend driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	Copyright (C) ST Microelectronics

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "stb0899_drv.h"
#include "stb0899_priv.h"
#include "stb0899_reg.h"

inline u32 stb0899_do_div(u64 n, u32 d)
{
	/* wrap do_div() for ease of use */

	do_div(n, d);
	return n;
}

#if 0
/* These functions are currently unused */
/*
 * stb0899_calc_srate
 * Compute symbol rate
 */
static u32 stb0899_calc_srate(u32 master_clk, u8 *sfr)
{
	u64 tmp;

	/* srate = (SFR * master_clk) >> 20 */

	/* sfr is of size 20 bit, stored with an offset of 4 bit */
	tmp = (((u32)sfr[0]) << 16) | (((u32)sfr[1]) << 8) | sfr[2];
	tmp &= ~0xf;
	tmp *= master_clk;
	tmp >>= 24;

	return tmp;
}

/*
 * stb0899_get_srate
 * Get the current symbol rate
 */
static u32 stb0899_get_srate(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	u8 sfr[3];

	stb0899_read_regs(state, STB0899_SFRH, sfr, 3);

	return stb0899_calc_srate(internal->master_clk, sfr);
}
#endif

/*
 * stb0899_set_srate
 * Set symbol frequency
 * MasterClock: master clock frequency (hz)
 * SymbolRate: symbol rate (bauds)
 * return symbol frequency
 */
static u32 stb0899_set_srate(struct stb0899_state *state, u32 master_clk, u32 srate)
{
	u32 tmp;
	u8 sfr[3];

	dprintk(state->verbose, FE_DEBUG, 1, "-->");
	/*
	 * in order to have the maximum precision, the symbol rate entered into
	 * the chip is computed as the closest value of the "true value".
	 * In this purpose, the symbol rate value is rounded (1 is added on the bit
	 * below the LSB )
	 *
	 * srate = (SFR * master_clk) >> 20
	 *      <=>
	 *   SFR = srate << 20 / master_clk
	 *
	 * rounded:
	 *   SFR = (srate << 21 + master_clk) / (2 * master_clk)
	 *
	 * stored as 20 bit number with an offset of 4 bit:
	 *   sfr = SFR << 4;
	 */

	tmp = stb0899_do_div((((u64)srate) << 21) + master_clk, 2 * master_clk);
	tmp <<= 4;

	sfr[0] = tmp >> 16;
	sfr[1] = tmp >>  8;
	sfr[2] = tmp;

	stb0899_write_regs(state, STB0899_SFRH, sfr, 3);

	return srate;
}

/*
 * stb0899_calc_derot_time
 * Compute the amount of time needed by the derotator to lock
 * SymbolRate: Symbol rate
 * return: derotator time constant (ms)
 */
static long stb0899_calc_derot_time(long srate)
{
	if (srate > 0)
		return (100000 / (srate / 1000));
	else
		return 0;
}

/*
 * stb0899_carr_width
 * Compute the width of the carrier
 * return: width of carrier (kHz or Mhz)
 */
long stb0899_carr_width(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;

	return (internal->srate + (internal->srate * internal->rolloff) / 100);
}

/*
 * stb0899_first_subrange
 * Compute the first subrange of the search
 */
static void stb0899_first_subrange(struct stb0899_state *state)
{
	struct stb0899_internal *internal	= &state->internal;
	struct stb0899_params *params		= &state->params;
	struct stb0899_config *config		=  state->config;

	int range = 0;
	u32 bandwidth = 0;

	if (config->tuner_get_bandwidth) {
		stb0899_i2c_gate_ctrl(&state->frontend, 1);
		config->tuner_get_bandwidth(&state->frontend, &bandwidth);
		stb0899_i2c_gate_ctrl(&state->frontend, 0);
		range = bandwidth - stb0899_carr_width(state) / 2;
	}

	if (range > 0)
		internal->sub_range = min(internal->srch_range, range);
	else
		internal->sub_range = 0;

	internal->freq = params->freq;
	internal->tuner_offst = 0L;
	internal->sub_dir = 1;
}

/*
 * stb0899_check_tmg
 * check for timing lock
 * internal.Ttiming: time to wait for loop lock
 */
static enum stb0899_status stb0899_check_tmg(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	int lock;
	u8 reg;
	s8 timing;

	msleep(internal->t_derot);

	stb0899_write_reg(state, STB0899_RTF, 0xf2);
	reg = stb0899_read_reg(state, STB0899_TLIR);
	lock = STB0899_GETFIELD(TLIR_TMG_LOCK_IND, reg);
	timing = stb0899_read_reg(state, STB0899_RTF);

	if (lock >= 42) {
		if ((lock > 48) && (abs(timing) >= 110)) {
			internal->status = ANALOGCARRIER;
			dprintk(state->verbose, FE_DEBUG, 1, "-->ANALOG Carrier !");
		} else {
			internal->status = TIMINGOK;
			dprintk(state->verbose, FE_DEBUG, 1, "------->TIMING OK !");
		}
	} else {
		internal->status = NOTIMING;
		dprintk(state->verbose, FE_DEBUG, 1, "-->NO TIMING !");
	}
	return internal->status;
}

/*
 * stb0899_search_tmg
 * perform a fs/2 zig-zag to find timing
 */
static enum stb0899_status stb0899_search_tmg(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	struct stb0899_params *params = &state->params;

	short int derot_step, derot_freq = 0, derot_limit, next_loop = 3;
	int index = 0;
	u8 cfr[2];

	internal->status = NOTIMING;

	/* timing loop computation & symbol rate optimisation	*/
	derot_limit = (internal->sub_range / 2L) / internal->mclk;
	derot_step = (params->srate / 2L) / internal->mclk;

	while ((stb0899_check_tmg(state) != TIMINGOK) && next_loop) {
		index++;
		derot_freq += index * internal->direction * derot_step;	/* next derot zig zag position	*/

		if (abs(derot_freq) > derot_limit)
			next_loop--;

		if (next_loop) {
			STB0899_SETFIELD_VAL(CFRM, cfr[0], MSB(state->config->inversion * derot_freq));
			STB0899_SETFIELD_VAL(CFRL, cfr[1], LSB(state->config->inversion * derot_freq));
			stb0899_write_regs(state, STB0899_CFRM, cfr, 2); /* derotator frequency		*/
		}
		internal->direction = -internal->direction;	/* Change zigzag direction		*/
	}

	if (internal->status == TIMINGOK) {
		stb0899_read_regs(state, STB0899_CFRM, cfr, 2); /* get derotator frequency		*/
		internal->derot_freq = state->config->inversion * MAKEWORD16(cfr[0], cfr[1]);
		dprintk(state->verbose, FE_DEBUG, 1, "------->TIMING OK ! Derot Freq = %d", internal->derot_freq);
	}

	return internal->status;
}

/*
 * stb0899_check_carrier
 * Check for carrier found
 */
static enum stb0899_status stb0899_check_carrier(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	u8 reg;

	msleep(internal->t_derot); /* wait for derotator ok	*/

	reg = stb0899_read_reg(state, STB0899_CFD);
	STB0899_SETFIELD_VAL(CFD_ON, reg, 1);
	stb0899_write_reg(state, STB0899_CFD, reg);

	reg = stb0899_read_reg(state, STB0899_DSTATUS);
	dprintk(state->verbose, FE_DEBUG, 1, "--------------------> STB0899_DSTATUS=[0x%02x]", reg);
	if (STB0899_GETFIELD(CARRIER_FOUND, reg)) {
		internal->status = CARRIEROK;
		dprintk(state->verbose, FE_DEBUG, 1, "-------------> CARRIEROK !");
	} else {
		internal->status = NOCARRIER;
		dprintk(state->verbose, FE_DEBUG, 1, "-------------> NOCARRIER !");
	}

	return internal->status;
}

/*
 * stb0899_search_carrier
 * Search for a QPSK carrier with the derotator
 */
static enum stb0899_status stb0899_search_carrier(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;

	short int derot_freq = 0, last_derot_freq = 0, derot_limit, next_loop = 3;
	int index = 0;
	u8 cfr[2];
	u8 reg;

	internal->status = NOCARRIER;
	derot_limit = (internal->sub_range / 2L) / internal->mclk;
	derot_freq = internal->derot_freq;

	reg = stb0899_read_reg(state, STB0899_CFD);
	STB0899_SETFIELD_VAL(CFD_ON, reg, 1);
	stb0899_write_reg(state, STB0899_CFD, reg);

	do {
		dprintk(state->verbose, FE_DEBUG, 1, "Derot Freq=%d, mclk=%d", derot_freq, internal->mclk);
		if (stb0899_check_carrier(state) == NOCARRIER) {
			index++;
			last_derot_freq = derot_freq;
			derot_freq += index * internal->direction * internal->derot_step; /* next zig zag derotator position */

			if(abs(derot_freq) > derot_limit)
				next_loop--;

			if (next_loop) {
				reg = stb0899_read_reg(state, STB0899_CFD);
				STB0899_SETFIELD_VAL(CFD_ON, reg, 1);
				stb0899_write_reg(state, STB0899_CFD, reg);

				STB0899_SETFIELD_VAL(CFRM, cfr[0], MSB(state->config->inversion * derot_freq));
				STB0899_SETFIELD_VAL(CFRL, cfr[1], LSB(state->config->inversion * derot_freq));
				stb0899_write_regs(state, STB0899_CFRM, cfr, 2); /* derotator frequency	*/
			}
		}

		internal->direction = -internal->direction; /* Change zigzag direction */
	} while ((internal->status != CARRIEROK) && next_loop);

	if (internal->status == CARRIEROK) {
		stb0899_read_regs(state, STB0899_CFRM, cfr, 2); /* get derotator frequency */
		internal->derot_freq = state->config->inversion * MAKEWORD16(cfr[0], cfr[1]);
		dprintk(state->verbose, FE_DEBUG, 1, "----> CARRIER OK !, Derot Freq=%d", internal->derot_freq);
	} else {
		internal->derot_freq = last_derot_freq;
	}

	return internal->status;
}

/*
 * stb0899_check_data
 * Check for data found
 */
static enum stb0899_status stb0899_check_data(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	struct stb0899_params *params = &state->params;

	int lock = 0, index = 0, dataTime = 500, loop;
	u8 reg;

	internal->status = NODATA;

	/* RESET FEC	*/
	reg = stb0899_read_reg(state, STB0899_TSTRES);
	STB0899_SETFIELD_VAL(FRESACS, reg, 1);
	stb0899_write_reg(state, STB0899_TSTRES, reg);
	msleep(1);
	reg = stb0899_read_reg(state, STB0899_TSTRES);
	STB0899_SETFIELD_VAL(FRESACS, reg, 0);
	stb0899_write_reg(state, STB0899_TSTRES, reg);

	if (params->srate <= 2000000)
		dataTime = 2000;
	else if (params->srate <= 5000000)
		dataTime = 1500;
	else if (params->srate <= 15000000)
		dataTime = 1000;
	else
		dataTime = 500;

	stb0899_write_reg(state, STB0899_DSTATUS2, 0x00); /* force search loop	*/
	while (1) {
		/* WARNING! VIT LOCKED has to be tested before VIT_END_LOOOP	*/
		reg = stb0899_read_reg(state, STB0899_VSTATUS);
		lock = STB0899_GETFIELD(VSTATUS_LOCKEDVIT, reg);
		loop = STB0899_GETFIELD(VSTATUS_END_LOOPVIT, reg);

		if (lock || loop || (index > dataTime))
			break;
		index++;
	}

	if (lock) {	/* DATA LOCK indicator	*/
		internal->status = DATAOK;
		dprintk(state->verbose, FE_DEBUG, 1, "-----------------> DATA OK !");
	}

	return internal->status;
}

/*
 * stb0899_search_data
 * Search for a QPSK carrier with the derotator
 */
static enum stb0899_status stb0899_search_data(struct stb0899_state *state)
{
	short int derot_freq, derot_step, derot_limit, next_loop = 3;
	u8 cfr[2];
	u8 reg;
	int index = 1;

	struct stb0899_internal *internal = &state->internal;
	struct stb0899_params *params = &state->params;

	derot_step = (params->srate / 4L) / internal->mclk;
	derot_limit = (internal->sub_range / 2L) / internal->mclk;
	derot_freq = internal->derot_freq;

	do {
		if ((internal->status != CARRIEROK) || (stb0899_check_data(state) != DATAOK)) {

			derot_freq += index * internal->direction * derot_step;	/* next zig zag derotator position */
			if (abs(derot_freq) > derot_limit)
				next_loop--;

			if (next_loop) {
				dprintk(state->verbose, FE_DEBUG, 1, "Derot freq=%d, mclk=%d", derot_freq, internal->mclk);
				reg = stb0899_read_reg(state, STB0899_CFD);
				STB0899_SETFIELD_VAL(CFD_ON, reg, 1);
				stb0899_write_reg(state, STB0899_CFD, reg);

				STB0899_SETFIELD_VAL(CFRM, cfr[0], MSB(state->config->inversion * derot_freq));
				STB0899_SETFIELD_VAL(CFRL, cfr[1], LSB(state->config->inversion * derot_freq));
				stb0899_write_regs(state, STB0899_CFRM, cfr, 2); /* derotator frequency	*/

				stb0899_check_carrier(state);
				index++;
			}
		}
		internal->direction = -internal->direction; /* change zig zag direction */
	} while ((internal->status != DATAOK) && next_loop);

	if (internal->status == DATAOK) {
		stb0899_read_regs(state, STB0899_CFRM, cfr, 2); /* get derotator frequency */
		internal->derot_freq = state->config->inversion * MAKEWORD16(cfr[0], cfr[1]);
		dprintk(state->verbose, FE_DEBUG, 1, "------> DATAOK ! Derot Freq=%d", internal->derot_freq);
	}

	return internal->status;
}

/*
 * stb0899_check_range
 * check if the found frequency is in the correct range
 */
static enum stb0899_status stb0899_check_range(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	struct stb0899_params *params = &state->params;

	int range_offst, tp_freq;

	range_offst = internal->srch_range / 2000;
	tp_freq = internal->freq + (internal->derot_freq * internal->mclk) / 1000;

	if ((tp_freq >= params->freq - range_offst) && (tp_freq <= params->freq + range_offst)) {
		internal->status = RANGEOK;
		dprintk(state->verbose, FE_DEBUG, 1, "----> RANGEOK !");
	} else {
		internal->status = OUTOFRANGE;
		dprintk(state->verbose, FE_DEBUG, 1, "----> OUT OF RANGE !");
	}

	return internal->status;
}

/*
 * NextSubRange
 * Compute the next subrange of the search
 */
static void next_sub_range(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	struct stb0899_params *params = &state->params;

	long old_sub_range;

	if (internal->sub_dir > 0) {
		old_sub_range = internal->sub_range;
		internal->sub_range = min((internal->srch_range / 2) -
					  (internal->tuner_offst + internal->sub_range / 2),
					   internal->sub_range);

		if (internal->sub_range < 0)
			internal->sub_range = 0;

		internal->tuner_offst += (old_sub_range + internal->sub_range) / 2;
	}

	internal->freq = params->freq + (internal->sub_dir * internal->tuner_offst) / 1000;
	internal->sub_dir = -internal->sub_dir;
}

/*
 * stb0899_dvbs_algo
 * Search for a signal, timing, carrier and data for a
 * given frequency in a given range
 */
enum stb0899_status stb0899_dvbs_algo(struct stb0899_state *state)
{
	struct stb0899_params *params		= &state->params;
	struct stb0899_internal *internal	= &state->internal;
	struct stb0899_config *config		= state->config;

	u8 bclc, reg;
	u8 cfr[2];
	u8 eq_const[10];
	s32 clnI = 3;
	u32 bandwidth = 0;

	/* BETA values rated @ 99MHz	*/
	s32 betaTab[5][4] = {
	       /*  5   10   20   30MBps */
		{ 37,  34,  32,  31 }, /* QPSK 1/2	*/
		{ 37,  35,  33,  31 }, /* QPSK 2/3	*/
		{ 37,  35,  33,  31 }, /* QPSK 3/4	*/
		{ 37,  36,  33,	 32 }, /* QPSK 5/6	*/
		{ 37,  36,  33,	 32 }  /* QPSK 7/8	*/
	};

	internal->direction = 1;

	stb0899_set_srate(state, internal->master_clk, params->srate);
	/* Carrier loop optimization versus symbol rate for acquisition*/
	if (params->srate <= 5000000) {
		stb0899_write_reg(state, STB0899_ACLC, 0x89);
		bclc = stb0899_read_reg(state, STB0899_BCLC);
		STB0899_SETFIELD_VAL(BETA, bclc, 0x1c);
		stb0899_write_reg(state, STB0899_BCLC, bclc);
		clnI = 0;
	} else if (params->srate <= 15000000) {
		stb0899_write_reg(state, STB0899_ACLC, 0xc9);
		bclc = stb0899_read_reg(state, STB0899_BCLC);
		STB0899_SETFIELD_VAL(BETA, bclc, 0x22);
		stb0899_write_reg(state, STB0899_BCLC, bclc);
		clnI = 1;
	} else if(params->srate <= 25000000) {
		stb0899_write_reg(state, STB0899_ACLC, 0x89);
		bclc = stb0899_read_reg(state, STB0899_BCLC);
		STB0899_SETFIELD_VAL(BETA, bclc, 0x27);
		stb0899_write_reg(state, STB0899_BCLC, bclc);
		clnI = 2;
	} else {
		stb0899_write_reg(state, STB0899_ACLC, 0xc8);
		bclc = stb0899_read_reg(state, STB0899_BCLC);
		STB0899_SETFIELD_VAL(BETA, bclc, 0x29);
		stb0899_write_reg(state, STB0899_BCLC, bclc);
		clnI = 3;
	}

	dprintk(state->verbose, FE_DEBUG, 1, "Set the timing loop to acquisition");
	/* Set the timing loop to acquisition	*/
	stb0899_write_reg(state, STB0899_RTC, 0x46);
	stb0899_write_reg(state, STB0899_CFD, 0xee);

	/* !! WARNING !!
	 * Do not read any status variables while acquisition,
	 * If any needed, read before the acquisition starts
	 * querying status while acquiring causes the
	 * acquisition to go bad and hence no locks.
	 */
	dprintk(state->verbose, FE_DEBUG, 1, "Derot Percent=%d Srate=%d mclk=%d",
		internal->derot_percent, params->srate, internal->mclk);

	/* Initial calculations	*/
	internal->derot_step = internal->derot_percent * (params->srate / 1000L) / internal->mclk; /* DerotStep/1000 * Fsymbol	*/
	internal->t_derot = stb0899_calc_derot_time(params->srate);
	internal->t_data = 500;

	dprintk(state->verbose, FE_DEBUG, 1, "RESET stream merger");
	/* RESET Stream merger	*/
	reg = stb0899_read_reg(state, STB0899_TSTRES);
	STB0899_SETFIELD_VAL(FRESRS, reg, 1);
	stb0899_write_reg(state, STB0899_TSTRES, reg);

	/*
	 * Set KDIVIDER to an intermediate value between
	 * 1/2 and 7/8 for acquisition
	 */
	reg = stb0899_read_reg(state, STB0899_DEMAPVIT);
	STB0899_SETFIELD_VAL(DEMAPVIT_KDIVIDER, reg, 60);
	stb0899_write_reg(state, STB0899_DEMAPVIT, reg);

	stb0899_write_reg(state, STB0899_EQON, 0x01); /* Equalizer OFF while acquiring */
	stb0899_write_reg(state, STB0899_VITSYNC, 0x19);

	stb0899_first_subrange(state);
	do {
		/* Initialisations */
		cfr[0] = cfr[1] = 0;
		stb0899_write_regs(state, STB0899_CFRM, cfr, 2); /* RESET derotator frequency	*/

		stb0899_write_reg(state, STB0899_RTF, 0);
		reg = stb0899_read_reg(state, STB0899_CFD);
		STB0899_SETFIELD_VAL(CFD_ON, reg, 1);
		stb0899_write_reg(state, STB0899_CFD, reg);

		internal->derot_freq = 0;
		internal->status = NOAGC1;

		/* enable tuner I/O */
		stb0899_i2c_gate_ctrl(&state->frontend, 1);

		/* Move tuner to frequency */
		dprintk(state->verbose, FE_DEBUG, 1, "Tuner set frequency");
		if (state->config->tuner_set_frequency)
			state->config->tuner_set_frequency(&state->frontend, internal->freq);

		if (state->config->tuner_get_frequency)
			state->config->tuner_get_frequency(&state->frontend, &internal->freq);

		msleep(internal->t_agc1 + internal->t_agc2 + internal->t_derot); /* AGC1, AGC2 and timing loop	*/
		dprintk(state->verbose, FE_DEBUG, 1, "current derot freq=%d", internal->derot_freq);
		internal->status = AGC1OK;

		/* There is signal in the band	*/
		if (config->tuner_get_bandwidth)
			config->tuner_get_bandwidth(&state->frontend, &bandwidth);

		/* disable tuner I/O */
		stb0899_i2c_gate_ctrl(&state->frontend, 0);

		if (params->srate <= bandwidth / 2)
			stb0899_search_tmg(state); /* For low rates (SCPC)	*/
		else
			stb0899_check_tmg(state); /* For high rates (MCPC)	*/

		if (internal->status == TIMINGOK) {
			dprintk(state->verbose, FE_DEBUG, 1,
				"TIMING OK ! Derot freq=%d, mclk=%d",
				internal->derot_freq, internal->mclk);

			if (stb0899_search_carrier(state) == CARRIEROK) {	/* Search for carrier	*/
				dprintk(state->verbose, FE_DEBUG, 1,
					"CARRIER OK ! Derot freq=%d, mclk=%d",
					internal->derot_freq, internal->mclk);

				if (stb0899_search_data(state) == DATAOK) {	/* Check for data	*/
					dprintk(state->verbose, FE_DEBUG, 1,
						"DATA OK ! Derot freq=%d, mclk=%d",
						internal->derot_freq, internal->mclk);

					if (stb0899_check_range(state) == RANGEOK) {
						dprintk(state->verbose, FE_DEBUG, 1,
							"RANGE OK ! derot freq=%d, mclk=%d",
							internal->derot_freq, internal->mclk);

						internal->freq = params->freq + ((internal->derot_freq * internal->mclk) / 1000);
						reg = stb0899_read_reg(state, STB0899_PLPARM);
						internal->fecrate = STB0899_GETFIELD(VITCURPUN, reg);
						dprintk(state->verbose, FE_DEBUG, 1,
							"freq=%d, internal resultant freq=%d",
							params->freq, internal->freq);

						dprintk(state->verbose, FE_DEBUG, 1,
							"internal puncture rate=%d",
							internal->fecrate);
					}
				}
			}
		}
		if (internal->status != RANGEOK)
			next_sub_range(state);

	} while (internal->sub_range && internal->status != RANGEOK);

	/* Set the timing loop to tracking	*/
	stb0899_write_reg(state, STB0899_RTC, 0x33);
	stb0899_write_reg(state, STB0899_CFD, 0xf7);
	/* if locked and range ok, set Kdiv	*/
	if (internal->status == RANGEOK) {
		dprintk(state->verbose, FE_DEBUG, 1, "Locked & Range OK !");
		stb0899_write_reg(state, STB0899_EQON, 0x41);		/* Equalizer OFF while acquiring	*/
		stb0899_write_reg(state, STB0899_VITSYNC, 0x39);	/* SN to b'11 for acquisition		*/

		/*
		 * Carrier loop optimization versus
		 * symbol Rate/Puncture Rate for Tracking
		 */
		reg = stb0899_read_reg(state, STB0899_BCLC);
		switch (internal->fecrate) {
		case STB0899_FEC_1_2:		/* 13	*/
			stb0899_write_reg(state, STB0899_DEMAPVIT, 0x1a);
			STB0899_SETFIELD_VAL(BETA, reg, betaTab[0][clnI]);
			stb0899_write_reg(state, STB0899_BCLC, reg);
			break;
		case STB0899_FEC_2_3:		/* 18	*/
			stb0899_write_reg(state, STB0899_DEMAPVIT, 44);
			STB0899_SETFIELD_VAL(BETA, reg, betaTab[1][clnI]);
			stb0899_write_reg(state, STB0899_BCLC, reg);
			break;
		case STB0899_FEC_3_4:		/* 21	*/
			stb0899_write_reg(state, STB0899_DEMAPVIT, 60);
			STB0899_SETFIELD_VAL(BETA, reg, betaTab[2][clnI]);
			stb0899_write_reg(state, STB0899_BCLC, reg);
			break;
		case STB0899_FEC_5_6:		/* 24	*/
			stb0899_write_reg(state, STB0899_DEMAPVIT, 75);
			STB0899_SETFIELD_VAL(BETA, reg, betaTab[3][clnI]);
			stb0899_write_reg(state, STB0899_BCLC, reg);
			break;
		case STB0899_FEC_6_7:		/* 25	*/
			stb0899_write_reg(state, STB0899_DEMAPVIT, 88);
			stb0899_write_reg(state, STB0899_ACLC, 0x88);
			stb0899_write_reg(state, STB0899_BCLC, 0x9a);
			break;
		case STB0899_FEC_7_8:		/* 26	*/
			stb0899_write_reg(state, STB0899_DEMAPVIT, 94);
			STB0899_SETFIELD_VAL(BETA, reg, betaTab[4][clnI]);
			stb0899_write_reg(state, STB0899_BCLC, reg);
			break;
		default:
			dprintk(state->verbose, FE_DEBUG, 1, "Unsupported Puncture Rate");
			break;
		}
		/* release stream merger RESET	*/
		reg = stb0899_read_reg(state, STB0899_TSTRES);
		STB0899_SETFIELD_VAL(FRESRS, reg, 0);
		stb0899_write_reg(state, STB0899_TSTRES, reg);

		/* disable carrier detector	*/
		reg = stb0899_read_reg(state, STB0899_CFD);
		STB0899_SETFIELD_VAL(CFD_ON, reg, 0);
		stb0899_write_reg(state, STB0899_CFD, reg);

		stb0899_read_regs(state, STB0899_EQUAI1, eq_const, 10);
	}

	return internal->status;
}

/*
 * stb0899_dvbs2_config_uwp
 * Configure UWP state machine
 */
static void stb0899_dvbs2_config_uwp(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	struct stb0899_config *config = state->config;
	u32 uwp1, uwp2, uwp3, reg;

	uwp1 = STB0899_READ_S2REG(STB0899_S2DEMOD, UWP_CNTRL1);
	uwp2 = STB0899_READ_S2REG(STB0899_S2DEMOD, UWP_CNTRL2);
	uwp3 = STB0899_READ_S2REG(STB0899_S2DEMOD, UWP_CNTRL3);

	STB0899_SETFIELD_VAL(UWP_ESN0_AVE, uwp1, config->esno_ave);
	STB0899_SETFIELD_VAL(UWP_ESN0_QUANT, uwp1, config->esno_quant);
	STB0899_SETFIELD_VAL(UWP_TH_SOF, uwp1, config->uwp_threshold_sof);

	STB0899_SETFIELD_VAL(FE_COARSE_TRK, uwp2, internal->av_frame_coarse);
	STB0899_SETFIELD_VAL(FE_FINE_TRK, uwp2, internal->av_frame_fine);
	STB0899_SETFIELD_VAL(UWP_MISS_TH, uwp2, config->miss_threshold);

	STB0899_SETFIELD_VAL(UWP_TH_ACQ, uwp3, config->uwp_threshold_acq);
	STB0899_SETFIELD_VAL(UWP_TH_TRACK, uwp3, config->uwp_threshold_track);

	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_UWP_CNTRL1, STB0899_OFF0_UWP_CNTRL1, uwp1);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_UWP_CNTRL2, STB0899_OFF0_UWP_CNTRL2, uwp2);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_UWP_CNTRL3, STB0899_OFF0_UWP_CNTRL3, uwp3);

	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, SOF_SRCH_TO);
	STB0899_SETFIELD_VAL(SOF_SEARCH_TIMEOUT, reg, config->sof_search_timeout);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_SOF_SRCH_TO, STB0899_OFF0_SOF_SRCH_TO, reg);
}

/*
 * stb0899_dvbs2_config_csm_auto
 * Set CSM to AUTO mode
 */
static void stb0899_dvbs2_config_csm_auto(struct stb0899_state *state)
{
	u32 reg;

	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, CSM_CNTRL1);
	STB0899_SETFIELD_VAL(CSM_AUTO_PARAM, reg, 1);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CSM_CNTRL1, STB0899_OFF0_CSM_CNTRL1, reg);
}

static long Log2Int(int number)
{
	int i;

	i = 0;
	while ((1 << i) <= abs(number))
		i++;

	if (number == 0)
		i = 1;

	return i - 1;
}

/*
 * stb0899_dvbs2_calc_srate
 * compute BTR_NOM_FREQ for the symbol rate
 */
static u32 stb0899_dvbs2_calc_srate(struct stb0899_state *state)
{
	struct stb0899_internal *internal	= &state->internal;
	struct stb0899_config *config		= state->config;

	u32 dec_ratio, dec_rate, decim, remain, intval, btr_nom_freq;
	u32 master_clk, srate;

	dec_ratio = (internal->master_clk * 2) / (5 * internal->srate);
	dec_ratio = (dec_ratio == 0) ? 1 : dec_ratio;
	dec_rate = Log2Int(dec_ratio);
	decim = 1 << dec_rate;
	master_clk = internal->master_clk / 1000;
	srate = internal->srate / 1000;

	if (decim <= 4) {
		intval = (decim * (1 << (config->btr_nco_bits - 1))) / master_clk;
		remain = (decim * (1 << (config->btr_nco_bits - 1))) % master_clk;
	} else {
		intval = (1 << (config->btr_nco_bits - 1)) / (master_clk / 100) * decim / 100;
		remain = (decim * (1 << (config->btr_nco_bits - 1))) % master_clk;
	}
	btr_nom_freq = (intval * srate) + ((remain * srate) / master_clk);

	return btr_nom_freq;
}

/*
 * stb0899_dvbs2_calc_dev
 * compute the correction to be applied to symbol rate
 */
static u32 stb0899_dvbs2_calc_dev(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	u32 dec_ratio, correction, master_clk, srate;

	dec_ratio = (internal->master_clk * 2) / (5 * internal->srate);
	dec_ratio = (dec_ratio == 0) ? 1 : dec_ratio;

	master_clk = internal->master_clk / 1000;	/* for integer Caculation*/
	srate = internal->srate / 1000;	/* for integer Caculation*/
	correction = (512 * master_clk) / (2 * dec_ratio * srate);

	return	correction;
}

/*
 * stb0899_dvbs2_set_srate
 * Set DVBS2 symbol rate
 */
static void stb0899_dvbs2_set_srate(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;

	u32 dec_ratio, dec_rate, win_sel, decim, f_sym, btr_nom_freq;
	u32 correction, freq_adj, band_lim, decim_cntrl, reg;
	u8 anti_alias;

	/*set decimation to 1*/
	dec_ratio = (internal->master_clk * 2) / (5 * internal->srate);
	dec_ratio = (dec_ratio == 0) ? 1 : dec_ratio;
	dec_rate = Log2Int(dec_ratio);

	win_sel = 0;
	if (dec_rate >= 5)
		win_sel = dec_rate - 4;

	decim = (1 << dec_rate);
	/* (FSamp/Fsymbol *100) for integer Caculation */
	f_sym = internal->master_clk / ((decim * internal->srate) / 1000);

	if (f_sym <= 2250)	/* don't band limit signal going into btr block*/
		band_lim = 1;
	else
		band_lim = 0;	/* band limit signal going into btr block*/

	decim_cntrl = ((win_sel << 3) & 0x18) + ((band_lim << 5) & 0x20) + (dec_rate & 0x7);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_DECIM_CNTRL, STB0899_OFF0_DECIM_CNTRL, decim_cntrl);

	if (f_sym <= 3450)
		anti_alias = 0;
	else if (f_sym <= 4250)
		anti_alias = 1;
	else
		anti_alias = 2;

	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_ANTI_ALIAS_SEL, STB0899_OFF0_ANTI_ALIAS_SEL, anti_alias);
	btr_nom_freq = stb0899_dvbs2_calc_srate(state);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_BTR_NOM_FREQ, STB0899_OFF0_BTR_NOM_FREQ, btr_nom_freq);

	correction = stb0899_dvbs2_calc_dev(state);
	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, BTR_CNTRL);
	STB0899_SETFIELD_VAL(BTR_FREQ_CORR, reg, correction);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_BTR_CNTRL, STB0899_OFF0_BTR_CNTRL, reg);

	/* scale UWP+CSM frequency to sample rate*/
	freq_adj =  internal->srate / (internal->master_clk / 4096);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_FREQ_ADJ_SCALE, STB0899_OFF0_FREQ_ADJ_SCALE, freq_adj);
}

/*
 * stb0899_dvbs2_set_btr_loopbw
 * set bit timing loop bandwidth as a percentage of the symbol rate
 */
static void stb0899_dvbs2_set_btr_loopbw(struct stb0899_state *state)
{
	struct stb0899_internal *internal	= &state->internal;
	struct stb0899_config *config		= state->config;

	u32 sym_peak = 23, zeta = 707, loopbw_percent = 60;
	s32 dec_ratio, dec_rate, k_btr1_rshft, k_btr1, k_btr0_rshft;
	s32 k_btr0, k_btr2_rshft, k_direct_shift, k_indirect_shift;
	u32 decim, K, wn, k_direct, k_indirect;
	u32 reg;

	dec_ratio = (internal->master_clk * 2) / (5 * internal->srate);
	dec_ratio = (dec_ratio == 0) ? 1 : dec_ratio;
	dec_rate = Log2Int(dec_ratio);
	decim = (1 << dec_rate);

	sym_peak *= 576000;
	K = (1 << config->btr_nco_bits) / (internal->master_clk / 1000);
	K *= (internal->srate / 1000000) * decim; /*k=k 10^-8*/

	if (K != 0) {
		K = sym_peak / K;
		wn = (4 * zeta * zeta) + 1000000;
		wn = (2 * (loopbw_percent * 1000) * 40 * zeta) /wn;  /*wn =wn 10^-8*/

		k_indirect = (wn * wn) / K;
		k_indirect = k_indirect;	  /*kindirect = kindirect 10^-6*/
		k_direct   = (2 * wn * zeta) / K;	/*kDirect = kDirect 10^-2*/
		k_direct  *= 100;

		k_direct_shift = Log2Int(k_direct) - Log2Int(10000) - 2;
		k_btr1_rshft = (-1 * k_direct_shift) + config->btr_gain_shift_offset;
		k_btr1 = k_direct / (1 << k_direct_shift);
		k_btr1 /= 10000;

		k_indirect_shift = Log2Int(k_indirect + 15) - 20 /*- 2*/;
		k_btr0_rshft = (-1 * k_indirect_shift) + config->btr_gain_shift_offset;
		k_btr0 = k_indirect * (1 << (-k_indirect_shift));
		k_btr0 /= 1000000;

		k_btr2_rshft = 0;
		if (k_btr0_rshft > 15) {
			k_btr2_rshft = k_btr0_rshft - 15;
			k_btr0_rshft = 15;
		}
		reg = STB0899_READ_S2REG(STB0899_S2DEMOD, BTR_LOOP_GAIN);
		STB0899_SETFIELD_VAL(KBTR0_RSHFT, reg, k_btr0_rshft);
		STB0899_SETFIELD_VAL(KBTR0, reg, k_btr0);
		STB0899_SETFIELD_VAL(KBTR1_RSHFT, reg, k_btr1_rshft);
		STB0899_SETFIELD_VAL(KBTR1, reg, k_btr1);
		STB0899_SETFIELD_VAL(KBTR2_RSHFT, reg, k_btr2_rshft);
		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_BTR_LOOP_GAIN, STB0899_OFF0_BTR_LOOP_GAIN, reg);
	} else
		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_BTR_LOOP_GAIN, STB0899_OFF0_BTR_LOOP_GAIN, 0xc4c4f);
}

/*
 * stb0899_dvbs2_set_carr_freq
 * set nominal frequency for carrier search
 */
static void stb0899_dvbs2_set_carr_freq(struct stb0899_state *state, s32 carr_freq, u32 master_clk)
{
	struct stb0899_config *config = state->config;
	s32 crl_nom_freq;
	u32 reg;

	crl_nom_freq = (1 << config->crl_nco_bits) / master_clk;
	crl_nom_freq *= carr_freq;
	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, CRL_NOM_FREQ);
	STB0899_SETFIELD_VAL(CRL_NOM_FREQ, reg, crl_nom_freq);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CRL_NOM_FREQ, STB0899_OFF0_CRL_NOM_FREQ, reg);
}

/*
 * stb0899_dvbs2_init_calc
 * Initialize DVBS2 UWP, CSM, carrier and timing loops
 */
static void stb0899_dvbs2_init_calc(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	s32 steps, step_size;
	u32 range, reg;

	/* config uwp and csm */
	stb0899_dvbs2_config_uwp(state);
	stb0899_dvbs2_config_csm_auto(state);

	/* initialize BTR	*/
	stb0899_dvbs2_set_srate(state);
	stb0899_dvbs2_set_btr_loopbw(state);

	if (internal->srate / 1000000 >= 15)
		step_size = (1 << 17) / 5;
	else if (internal->srate / 1000000 >= 10)
		step_size = (1 << 17) / 7;
	else if (internal->srate / 1000000 >= 5)
		step_size = (1 << 17) / 10;
	else
		step_size = (1 << 17) / 4;

	range = internal->srch_range / 1000000;
	steps = (10 * range * (1 << 17)) / (step_size * (internal->srate / 1000000));
	steps = (steps + 6) / 10;
	steps = (steps == 0) ? 1 : steps;
	if (steps % 2 == 0)
		stb0899_dvbs2_set_carr_freq(state, internal->center_freq -
					   (internal->step_size * (internal->srate / 20000000)),
					   (internal->master_clk) / 1000000);
	else
		stb0899_dvbs2_set_carr_freq(state, internal->center_freq, (internal->master_clk) / 1000000);

	/*Set Carrier Search params (zigzag, num steps and freq step size*/
	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, ACQ_CNTRL2);
	STB0899_SETFIELD_VAL(ZIGZAG, reg, 1);
	STB0899_SETFIELD_VAL(NUM_STEPS, reg, steps);
	STB0899_SETFIELD_VAL(FREQ_STEPSIZE, reg, step_size);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_ACQ_CNTRL2, STB0899_OFF0_ACQ_CNTRL2, reg);
}

/*
 * stb0899_dvbs2_btr_init
 * initialize the timing loop
 */
static void stb0899_dvbs2_btr_init(struct stb0899_state *state)
{
	u32 reg;

	/* set enable BTR loopback	*/
	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, BTR_CNTRL);
	STB0899_SETFIELD_VAL(INTRP_PHS_SENSE, reg, 1);
	STB0899_SETFIELD_VAL(BTR_ERR_ENA, reg, 1);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_BTR_CNTRL, STB0899_OFF0_BTR_CNTRL, reg);

	/* fix btr freq accum at 0	*/
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_BTR_FREQ_INIT, STB0899_OFF0_BTR_FREQ_INIT, 0x10000000);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_BTR_FREQ_INIT, STB0899_OFF0_BTR_FREQ_INIT, 0x00000000);

	/* fix btr freq accum at 0	*/
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_BTR_PHS_INIT, STB0899_OFF0_BTR_PHS_INIT, 0x10000000);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_BTR_PHS_INIT, STB0899_OFF0_BTR_PHS_INIT, 0x00000000);
}

/*
 * stb0899_dvbs2_reacquire
 * trigger a DVB-S2 acquisition
 */
static void stb0899_dvbs2_reacquire(struct stb0899_state *state)
{
	u32 reg = 0;

	/* demod soft reset	*/
	STB0899_SETFIELD_VAL(DVBS2_RESET, reg, 1);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_RESET_CNTRL, STB0899_OFF0_RESET_CNTRL, reg);

	/*Reset Timing Loop	*/
	stb0899_dvbs2_btr_init(state);

	/* reset Carrier loop	*/
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CRL_FREQ_INIT, STB0899_OFF0_CRL_FREQ_INIT, (1 << 30));
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CRL_FREQ_INIT, STB0899_OFF0_CRL_FREQ_INIT, 0);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CRL_LOOP_GAIN, STB0899_OFF0_CRL_LOOP_GAIN, 0);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CRL_PHS_INIT, STB0899_OFF0_CRL_PHS_INIT, (1 << 30));
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CRL_PHS_INIT, STB0899_OFF0_CRL_PHS_INIT, 0);

	/*release demod soft reset	*/
	reg = 0;
	STB0899_SETFIELD_VAL(DVBS2_RESET, reg, 0);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_RESET_CNTRL, STB0899_OFF0_RESET_CNTRL, reg);

	/* start acquisition process	*/
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_ACQUIRE_TRIG, STB0899_OFF0_ACQUIRE_TRIG, 1);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_LOCK_LOST, STB0899_OFF0_LOCK_LOST, 0);

	/* equalizer Init	*/
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_EQUALIZER_INIT, STB0899_OFF0_EQUALIZER_INIT, 1);

	/*Start equilizer	*/
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_EQUALIZER_INIT, STB0899_OFF0_EQUALIZER_INIT, 0);

	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, EQ_CNTRL);
	STB0899_SETFIELD_VAL(EQ_SHIFT, reg, 0);
	STB0899_SETFIELD_VAL(EQ_DISABLE_UPDATE, reg, 0);
	STB0899_SETFIELD_VAL(EQ_DELAY, reg, 0x05);
	STB0899_SETFIELD_VAL(EQ_ADAPT_MODE, reg, 0x01);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_EQ_CNTRL, STB0899_OFF0_EQ_CNTRL, reg);

	/* RESET Packet delineator	*/
	stb0899_write_reg(state, STB0899_PDELCTRL, 0x4a);
}

/*
 * stb0899_dvbs2_get_dmd_status
 * get DVB-S2 Demod LOCK status
 */
static enum stb0899_status stb0899_dvbs2_get_dmd_status(struct stb0899_state *state, int timeout)
{
	int time = -10, lock = 0, uwp, csm;
	u32 reg;

	do {
		reg = STB0899_READ_S2REG(STB0899_S2DEMOD, DMD_STATUS);
		dprintk(state->verbose, FE_DEBUG, 1, "DMD_STATUS=[0x%02x]", reg);
		if (STB0899_GETFIELD(IF_AGC_LOCK, reg))
			dprintk(state->verbose, FE_DEBUG, 1, "------------->IF AGC LOCKED !");
		reg = STB0899_READ_S2REG(STB0899_S2DEMOD, DMD_STAT2);
		dprintk(state->verbose, FE_DEBUG, 1, "----------->DMD STAT2=[0x%02x]", reg);
		uwp = STB0899_GETFIELD(UWP_LOCK, reg);
		csm = STB0899_GETFIELD(CSM_LOCK, reg);
		if (uwp && csm)
			lock = 1;

		time += 10;
		msleep(10);

	} while ((!lock) && (time <= timeout));

	if (lock) {
		dprintk(state->verbose, FE_DEBUG, 1, "----------------> DVB-S2 LOCK !");
		return DVBS2_DEMOD_LOCK;
	} else {
		return DVBS2_DEMOD_NOLOCK;
	}
}

/*
 * stb0899_dvbs2_get_data_lock
 * get FEC status
 */
static int stb0899_dvbs2_get_data_lock(struct stb0899_state *state, int timeout)
{
	int time = 0, lock = 0;
	u8 reg;

	while ((!lock) && (time < timeout)) {
		reg = stb0899_read_reg(state, STB0899_CFGPDELSTATUS1);
		dprintk(state->verbose, FE_DEBUG, 1, "---------> CFGPDELSTATUS=[0x%02x]", reg);
		lock = STB0899_GETFIELD(CFGPDELSTATUS_LOCK, reg);
		time++;
	}

	return lock;
}

/*
 * stb0899_dvbs2_get_fec_status
 * get DVB-S2 FEC LOCK status
 */
static enum stb0899_status stb0899_dvbs2_get_fec_status(struct stb0899_state *state, int timeout)
{
	int time = 0, Locked;

	do {
		Locked = stb0899_dvbs2_get_data_lock(state, 1);
		time++;
		msleep(1);

	} while ((!Locked) && (time < timeout));

	if (Locked) {
		dprintk(state->verbose, FE_DEBUG, 1, "---------->DVB-S2 FEC LOCK !");
		return DVBS2_FEC_LOCK;
	} else {
		return DVBS2_FEC_NOLOCK;
	}
}


/*
 * stb0899_dvbs2_init_csm
 * set parameters for manual mode
 */
static void stb0899_dvbs2_init_csm(struct stb0899_state *state, int pilots, enum stb0899_modcod modcod)
{
	struct stb0899_internal *internal = &state->internal;

	s32 dvt_tbl = 1, two_pass = 0, agc_gain = 6, agc_shift = 0, loop_shift = 0, phs_diff_thr = 0x80;
	s32 gamma_acq, gamma_rho_acq, gamma_trk, gamma_rho_trk, lock_count_thr;
	u32 csm1, csm2, csm3, csm4;

	if (((internal->master_clk / internal->srate) <= 4) && (modcod <= 11) && (pilots == 1)) {
		switch (modcod) {
		case STB0899_QPSK_12:
			gamma_acq		= 25;
			gamma_rho_acq		= 2700;
			gamma_trk		= 12;
			gamma_rho_trk		= 180;
			lock_count_thr		= 8;
			break;
		case STB0899_QPSK_35:
			gamma_acq		= 38;
			gamma_rho_acq		= 7182;
			gamma_trk		= 14;
			gamma_rho_trk		= 308;
			lock_count_thr		= 8;
			break;
		case STB0899_QPSK_23:
			gamma_acq		= 42;
			gamma_rho_acq		= 9408;
			gamma_trk		= 17;
			gamma_rho_trk		= 476;
			lock_count_thr		= 8;
			break;
		case STB0899_QPSK_34:
			gamma_acq		= 53;
			gamma_rho_acq		= 16642;
			gamma_trk		= 19;
			gamma_rho_trk		= 646;
			lock_count_thr		= 8;
			break;
		case STB0899_QPSK_45:
			gamma_acq		= 53;
			gamma_rho_acq		= 17119;
			gamma_trk		= 22;
			gamma_rho_trk		= 880;
			lock_count_thr		= 8;
			break;
		case STB0899_QPSK_56:
			gamma_acq		= 55;
			gamma_rho_acq		= 19250;
			gamma_trk		= 23;
			gamma_rho_trk		= 989;
			lock_count_thr		= 8;
			break;
		case STB0899_QPSK_89:
			gamma_acq		= 60;
			gamma_rho_acq		= 24240;
			gamma_trk		= 24;
			gamma_rho_trk		= 1176;
			lock_count_thr		= 8;
			break;
		case STB0899_QPSK_910:
			gamma_acq		= 66;
			gamma_rho_acq		= 29634;
			gamma_trk		= 24;
			gamma_rho_trk		= 1176;
			lock_count_thr		= 8;
			break;
		default:
			gamma_acq		= 66;
			gamma_rho_acq		= 29634;
			gamma_trk		= 24;
			gamma_rho_trk		= 1176;
			lock_count_thr		= 8;
			break;
		}

		csm1 = STB0899_READ_S2REG(STB0899_S2DEMOD, CSM_CNTRL1);
		STB0899_SETFIELD_VAL(CSM_AUTO_PARAM, csm1, 0);
		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CSM_CNTRL1, STB0899_OFF0_CSM_CNTRL1, csm1);

		csm1 = STB0899_READ_S2REG(STB0899_S2DEMOD, CSM_CNTRL1);
		csm2 = STB0899_READ_S2REG(STB0899_S2DEMOD, CSM_CNTRL2);
		csm3 = STB0899_READ_S2REG(STB0899_S2DEMOD, CSM_CNTRL3);
		csm4 = STB0899_READ_S2REG(STB0899_S2DEMOD, CSM_CNTRL4);

		STB0899_SETFIELD_VAL(CSM_DVT_TABLE, csm1, dvt_tbl);
		STB0899_SETFIELD_VAL(CSM_TWO_PASS, csm1, two_pass);
		STB0899_SETFIELD_VAL(CSM_AGC_GAIN, csm1, agc_gain);
		STB0899_SETFIELD_VAL(CSM_AGC_SHIFT, csm1, agc_shift);
		STB0899_SETFIELD_VAL(FE_LOOP_SHIFT, csm1, loop_shift);
		STB0899_SETFIELD_VAL(CSM_GAMMA_ACQ, csm2, gamma_acq);
		STB0899_SETFIELD_VAL(CSM_GAMMA_RHOACQ, csm2, gamma_rho_acq);
		STB0899_SETFIELD_VAL(CSM_GAMMA_TRACK, csm3, gamma_trk);
		STB0899_SETFIELD_VAL(CSM_GAMMA_RHOTRACK, csm3, gamma_rho_trk);
		STB0899_SETFIELD_VAL(CSM_LOCKCOUNT_THRESH, csm4, lock_count_thr);
		STB0899_SETFIELD_VAL(CSM_PHASEDIFF_THRESH, csm4, phs_diff_thr);

		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CSM_CNTRL1, STB0899_OFF0_CSM_CNTRL1, csm1);
		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CSM_CNTRL2, STB0899_OFF0_CSM_CNTRL2, csm2);
		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CSM_CNTRL3, STB0899_OFF0_CSM_CNTRL3, csm3);
		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CSM_CNTRL4, STB0899_OFF0_CSM_CNTRL4, csm4);
	}
}

/*
 * stb0899_dvbs2_get_srate
 * get DVB-S2 Symbol Rate
 */
static u32 stb0899_dvbs2_get_srate(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	struct stb0899_config *config = state->config;

	u32 bTrNomFreq, srate, decimRate, intval1, intval2, reg;
	int div1, div2, rem1, rem2;

	div1 = config->btr_nco_bits / 2;
	div2 = config->btr_nco_bits - div1 - 1;

	bTrNomFreq = STB0899_READ_S2REG(STB0899_S2DEMOD, BTR_NOM_FREQ);

	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, DECIM_CNTRL);
	decimRate = STB0899_GETFIELD(DECIM_RATE, reg);
	decimRate = (1 << decimRate);

	intval1 = internal->master_clk / (1 << div1);
	intval2 = bTrNomFreq / (1 << div2);

	rem1 = internal->master_clk % (1 << div1);
	rem2 = bTrNomFreq % (1 << div2);
	/* only for integer calculation	*/
	srate = (intval1 * intval2) + ((intval1 * rem2) / (1 << div2)) + ((intval2 * rem1) / (1 << div1));
	srate /= decimRate;	/*symbrate = (btrnomfreq_register_val*MasterClock)/2^(27+decim_rate_field) */

	return	srate;
}

/*
 * stb0899_dvbs2_algo
 * Search for signal, timing, carrier and data for a given
 * frequency in a given range
 */
enum stb0899_status stb0899_dvbs2_algo(struct stb0899_state *state)
{
	struct stb0899_internal *internal = &state->internal;
	enum stb0899_modcod modcod;

	s32 offsetfreq, searchTime, FecLockTime, pilots, iqSpectrum;
	int i = 0;
	u32 reg, csm1;

	if (internal->srate <= 2000000) {
		searchTime	= 5000;	/* 5000 ms max time to lock UWP and CSM, SYMB <= 2Mbs		*/
		FecLockTime	= 350;	/* 350  ms max time to lock FEC, SYMB <= 2Mbs			*/
	} else if (internal->srate <= 5000000) {
		searchTime	= 2500;	/* 2500 ms max time to lock UWP and CSM, 2Mbs < SYMB <= 5Mbs	*/
		FecLockTime	= 170;	/* 170  ms max time to lock FEC, 2Mbs< SYMB <= 5Mbs		*/
	} else if (internal->srate <= 10000000) {
		searchTime	= 1500;	/* 1500 ms max time to lock UWP and CSM, 5Mbs <SYMB <= 10Mbs	*/
		FecLockTime	= 80;	/* 80  ms max time to lock FEC, 5Mbs< SYMB <= 10Mbs		*/
	} else if (internal->srate <= 15000000) {
		searchTime	= 500;	/* 500 ms max time to lock UWP and CSM, 10Mbs <SYMB <= 15Mbs	*/
		FecLockTime	= 50;	/* 50  ms max time to lock FEC, 10Mbs< SYMB <= 15Mbs		*/
	} else if (internal->srate <= 20000000) {
		searchTime	= 300;	/* 300 ms max time to lock UWP and CSM, 15Mbs < SYMB <= 20Mbs	*/
		FecLockTime	= 30;	/* 50  ms max time to lock FEC, 15Mbs< SYMB <= 20Mbs		*/
	} else if (internal->srate <= 25000000) {
		searchTime	= 250;	/* 250 ms max time to lock UWP and CSM, 20 Mbs < SYMB <= 25Mbs	*/
		FecLockTime	= 25;	/* 25 ms max time to lock FEC, 20Mbs< SYMB <= 25Mbs		*/
	} else {
		searchTime	= 150;	/* 150 ms max time to lock UWP and CSM, SYMB > 25Mbs		*/
		FecLockTime	= 20;	/* 20 ms max time to lock FEC, 20Mbs< SYMB <= 25Mbs		*/
	}

	/* Maintain Stream Merger in reset during acquisition	*/
	reg = stb0899_read_reg(state, STB0899_TSTRES);
	STB0899_SETFIELD_VAL(FRESRS, reg, 1);
	stb0899_write_reg(state, STB0899_TSTRES, reg);

	/* enable tuner I/O */
	stb0899_i2c_gate_ctrl(&state->frontend, 1);

	/* Move tuner to frequency	*/
	if (state->config->tuner_set_frequency)
		state->config->tuner_set_frequency(&state->frontend, internal->freq);
	if (state->config->tuner_get_frequency)
		state->config->tuner_get_frequency(&state->frontend, &internal->freq);

	/* disable tuner I/O */
	stb0899_i2c_gate_ctrl(&state->frontend, 0);

	/* Set IF AGC to acquisition	*/
	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, IF_AGC_CNTRL);
	STB0899_SETFIELD_VAL(IF_LOOP_GAIN, reg,  4);
	STB0899_SETFIELD_VAL(IF_AGC_REF, reg, 32);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_IF_AGC_CNTRL, STB0899_OFF0_IF_AGC_CNTRL, reg);

	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, IF_AGC_CNTRL2);
	STB0899_SETFIELD_VAL(IF_AGC_DUMP_PER, reg, 0);
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_IF_AGC_CNTRL2, STB0899_OFF0_IF_AGC_CNTRL2, reg);

	/* Initialisation	*/
	stb0899_dvbs2_init_calc(state);

	reg = STB0899_READ_S2REG(STB0899_S2DEMOD, DMD_CNTRL2);
	switch (internal->inversion) {
	case IQ_SWAP_OFF:
		STB0899_SETFIELD_VAL(SPECTRUM_INVERT, reg, 0);
		break;
	case IQ_SWAP_ON:
		STB0899_SETFIELD_VAL(SPECTRUM_INVERT, reg, 1);
		break;
	case IQ_SWAP_AUTO:	/* use last successful search first	*/
		STB0899_SETFIELD_VAL(SPECTRUM_INVERT, reg, 1);
		break;
	}
	stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_DMD_CNTRL2, STB0899_OFF0_DMD_CNTRL2, reg);
	stb0899_dvbs2_reacquire(state);

	/* Wait for demod lock (UWP and CSM)	*/
	internal->status = stb0899_dvbs2_get_dmd_status(state, searchTime);

	if (internal->status == DVBS2_DEMOD_LOCK) {
		dprintk(state->verbose, FE_DEBUG, 1, "------------> DVB-S2 DEMOD LOCK !");
		i = 0;
		/* Demod Locked, check FEC status	*/
		internal->status = stb0899_dvbs2_get_fec_status(state, FecLockTime);

		/*If false lock (UWP and CSM Locked but no FEC) try 3 time max*/
		while ((internal->status != DVBS2_FEC_LOCK) && (i < 3)) {
			/*	Read the frequency offset*/
			offsetfreq = STB0899_READ_S2REG(STB0899_S2DEMOD, CRL_FREQ);

			/* Set the Nominal frequency to the found frequency offset for the next reacquire*/
			reg = STB0899_READ_S2REG(STB0899_S2DEMOD, CRL_NOM_FREQ);
			STB0899_SETFIELD_VAL(CRL_NOM_FREQ, reg, offsetfreq);
			stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CRL_NOM_FREQ, STB0899_OFF0_CRL_NOM_FREQ, reg);
			stb0899_dvbs2_reacquire(state);
			internal->status = stb0899_dvbs2_get_fec_status(state, searchTime);
			i++;
		}
	}

	if (internal->status != DVBS2_FEC_LOCK) {
		if (internal->inversion == IQ_SWAP_AUTO) {
			reg = STB0899_READ_S2REG(STB0899_S2DEMOD, DMD_CNTRL2);
			iqSpectrum = STB0899_GETFIELD(SPECTRUM_INVERT, reg);
			/* IQ Spectrum Inversion	*/
			STB0899_SETFIELD_VAL(SPECTRUM_INVERT, reg, !iqSpectrum);
			stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_DMD_CNTRL2, STB0899_OFF0_DMD_CNTRL2, reg);
			/* start acquistion process	*/
			stb0899_dvbs2_reacquire(state);

			/* Wait for demod lock (UWP and CSM)	*/
			internal->status = stb0899_dvbs2_get_dmd_status(state, searchTime);
			if (internal->status == DVBS2_DEMOD_LOCK) {
				i = 0;
				/* Demod Locked, check FEC	*/
				internal->status = stb0899_dvbs2_get_fec_status(state, FecLockTime);
				/*try thrice for false locks, (UWP and CSM Locked but no FEC)	*/
				while ((internal->status != DVBS2_FEC_LOCK) && (i < 3)) {
					/*	Read the frequency offset*/
					offsetfreq = STB0899_READ_S2REG(STB0899_S2DEMOD, CRL_FREQ);

					/* Set the Nominal frequency to the found frequency offset for the next reacquire*/
					reg = STB0899_READ_S2REG(STB0899_S2DEMOD, CRL_NOM_FREQ);
					STB0899_SETFIELD_VAL(CRL_NOM_FREQ, reg, offsetfreq);
					stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CRL_NOM_FREQ, STB0899_OFF0_CRL_NOM_FREQ, reg);

					stb0899_dvbs2_reacquire(state);
					internal->status = stb0899_dvbs2_get_fec_status(state, searchTime);
					i++;
				}
			}
/*
			if (pParams->DVBS2State == FE_DVBS2_FEC_LOCKED)
				pParams->IQLocked = !iqSpectrum;
*/
		}
	}
	if (internal->status == DVBS2_FEC_LOCK) {
		dprintk(state->verbose, FE_DEBUG, 1, "----------------> DVB-S2 FEC Lock !");
		reg = STB0899_READ_S2REG(STB0899_S2DEMOD, UWP_STAT2);
		modcod = STB0899_GETFIELD(UWP_DECODE_MOD, reg) >> 2;
		pilots = STB0899_GETFIELD(UWP_DECODE_MOD, reg) & 0x01;

		if ((((10 * internal->master_clk) / (internal->srate / 10)) <= 410) &&
		      (INRANGE(STB0899_QPSK_23, modcod, STB0899_QPSK_910)) &&
		      (pilots == 1)) {

			stb0899_dvbs2_init_csm(state, pilots, modcod);
			/* Wait for UWP,CSM and data LOCK 20ms max	*/
			internal->status = stb0899_dvbs2_get_fec_status(state, FecLockTime);

			i = 0;
			while ((internal->status != DVBS2_FEC_LOCK) && (i < 3)) {
				csm1 = STB0899_READ_S2REG(STB0899_S2DEMOD, CSM_CNTRL1);
				STB0899_SETFIELD_VAL(CSM_TWO_PASS, csm1, 1);
				stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CSM_CNTRL1, STB0899_OFF0_CSM_CNTRL1, csm1);
				csm1 = STB0899_READ_S2REG(STB0899_S2DEMOD, CSM_CNTRL1);
				STB0899_SETFIELD_VAL(CSM_TWO_PASS, csm1, 0);
				stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_CSM_CNTRL1, STB0899_OFF0_CSM_CNTRL1, csm1);

				internal->status = stb0899_dvbs2_get_fec_status(state, FecLockTime);
				i++;
			}
		}

		if ((((10 * internal->master_clk) / (internal->srate / 10)) <= 410) &&
		      (INRANGE(STB0899_QPSK_12, modcod, STB0899_QPSK_35)) &&
		      (pilots == 1)) {

			/* Equalizer Disable update	 */
			reg = STB0899_READ_S2REG(STB0899_S2DEMOD, EQ_CNTRL);
			STB0899_SETFIELD_VAL(EQ_DISABLE_UPDATE, reg, 1);
			stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_EQ_CNTRL, STB0899_OFF0_EQ_CNTRL, reg);
		}

		/* slow down the Equalizer once locked	*/
		reg = STB0899_READ_S2REG(STB0899_S2DEMOD, EQ_CNTRL);
		STB0899_SETFIELD_VAL(EQ_SHIFT, reg, 0x02);
		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_EQ_CNTRL, STB0899_OFF0_EQ_CNTRL, reg);

		/* Store signal parameters	*/
		offsetfreq = STB0899_READ_S2REG(STB0899_S2DEMOD, CRL_FREQ);

		offsetfreq = offsetfreq / ((1 << 30) / 1000);
		offsetfreq *= (internal->master_clk / 1000000);
		reg = STB0899_READ_S2REG(STB0899_S2DEMOD, DMD_CNTRL2);
		if (STB0899_GETFIELD(SPECTRUM_INVERT, reg))
			offsetfreq *= -1;

		internal->freq = internal->freq - offsetfreq;
		internal->srate = stb0899_dvbs2_get_srate(state);

		reg = STB0899_READ_S2REG(STB0899_S2DEMOD, UWP_STAT2);
		internal->modcod = STB0899_GETFIELD(UWP_DECODE_MOD, reg) >> 2;
		internal->pilots = STB0899_GETFIELD(UWP_DECODE_MOD, reg) & 0x01;
		internal->frame_length = (STB0899_GETFIELD(UWP_DECODE_MOD, reg) >> 1) & 0x01;

		 /* Set IF AGC to tracking	*/
		reg = STB0899_READ_S2REG(STB0899_S2DEMOD, IF_AGC_CNTRL);
		STB0899_SETFIELD_VAL(IF_LOOP_GAIN, reg,  3);

		/* if QPSK 1/2,QPSK 3/5 or QPSK 2/3 set IF AGC reference to 16 otherwise 32*/
		if (INRANGE(STB0899_QPSK_12, internal->modcod, STB0899_QPSK_23))
			STB0899_SETFIELD_VAL(IF_AGC_REF, reg, 16);

		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_IF_AGC_CNTRL, STB0899_OFF0_IF_AGC_CNTRL, reg);

		reg = STB0899_READ_S2REG(STB0899_S2DEMOD, IF_AGC_CNTRL2);
		STB0899_SETFIELD_VAL(IF_AGC_DUMP_PER, reg, 7);
		stb0899_write_s2reg(state, STB0899_S2DEMOD, STB0899_BASE_IF_AGC_CNTRL2, STB0899_OFF0_IF_AGC_CNTRL2, reg);
	}

	/* Release Stream Merger Reset		*/
	reg = stb0899_read_reg(state, STB0899_TSTRES);
	STB0899_SETFIELD_VAL(FRESRS, reg, 0);
	stb0899_write_reg(state, STB0899_TSTRES, reg);

	return internal->status;
}
