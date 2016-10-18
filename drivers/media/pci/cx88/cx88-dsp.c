/*
 *
 *  Stereo and SAP detection for cx88
 *
 *  Copyright (c) 2009 Marton Balint <cus@fazekas.hu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <asm/div64.h>

#include "cx88.h"
#include "cx88-reg.h"

#define INT_PI			((s32)(3.141592653589 * 32768.0))

#define compat_remainder(a, b) \
	 ((float)(((s32)((a)*100))%((s32)((b)*100)))/100.0)

#define baseband_freq(carrier, srate, tone) ((s32)( \
	 (compat_remainder(carrier + tone, srate)) / srate * 2 * INT_PI))

/* We calculate the baseband frequencies of the carrier and the pilot tones
 * based on the the sampling rate of the audio rds fifo. */

#define FREQ_A2_CARRIER         baseband_freq(54687.5, 2689.36, 0.0)
#define FREQ_A2_DUAL            baseband_freq(54687.5, 2689.36, 274.1)
#define FREQ_A2_STEREO          baseband_freq(54687.5, 2689.36, 117.5)

/* The frequencies below are from the reference driver. They probably need
 * further adjustments, because they are not tested at all. You may even need
 * to play a bit with the registers of the chip to select the proper signal
 * for the input of the audio rds fifo, and measure it's sampling rate to
 * calculate the proper baseband frequencies... */

#define FREQ_A2M_CARRIER	((s32)(2.114516 * 32768.0))
#define FREQ_A2M_DUAL		((s32)(2.754916 * 32768.0))
#define FREQ_A2M_STEREO		((s32)(2.462326 * 32768.0))

#define FREQ_EIAJ_CARRIER	((s32)(1.963495 * 32768.0)) /* 5pi/8  */
#define FREQ_EIAJ_DUAL		((s32)(2.562118 * 32768.0))
#define FREQ_EIAJ_STEREO	((s32)(2.601053 * 32768.0))

#define FREQ_BTSC_DUAL		((s32)(1.963495 * 32768.0)) /* 5pi/8  */
#define FREQ_BTSC_DUAL_REF	((s32)(1.374446 * 32768.0)) /* 7pi/16 */

#define FREQ_BTSC_SAP		((s32)(2.471532 * 32768.0))
#define FREQ_BTSC_SAP_REF	((s32)(1.730072 * 32768.0))

/* The spectrum of the signal should be empty between these frequencies. */
#define FREQ_NOISE_START	((s32)(0.100000 * 32768.0))
#define FREQ_NOISE_END		((s32)(1.200000 * 32768.0))

static unsigned int dsp_debug;
module_param(dsp_debug, int, 0644);
MODULE_PARM_DESC(dsp_debug, "enable audio dsp debug messages");

#define dprintk(level, fmt, arg...)	if (dsp_debug >= level) \
	printk(KERN_DEBUG "%s/0: " fmt, core->name , ## arg)

static s32 int_cos(u32 x)
{
	u32 t2, t4, t6, t8;
	s32 ret;
	u16 period = x / INT_PI;
	if (period % 2)
		return -int_cos(x - INT_PI);
	x = x % INT_PI;
	if (x > INT_PI/2)
		return -int_cos(INT_PI/2 - (x % (INT_PI/2)));
	/* Now x is between 0 and INT_PI/2.
	 * To calculate cos(x) we use it's Taylor polinom. */
	t2 = x*x/32768/2;
	t4 = t2*x/32768*x/32768/3/4;
	t6 = t4*x/32768*x/32768/5/6;
	t8 = t6*x/32768*x/32768/7/8;
	ret = 32768-t2+t4-t6+t8;
	return ret;
}

static u32 int_goertzel(s16 x[], u32 N, u32 freq)
{
	/* We use the Goertzel algorithm to determine the power of the
	 * given frequency in the signal */
	s32 s_prev = 0;
	s32 s_prev2 = 0;
	s32 coeff = 2*int_cos(freq);
	u32 i;

	u64 tmp;
	u32 divisor;

	for (i = 0; i < N; i++) {
		s32 s = x[i] + ((s64)coeff*s_prev/32768) - s_prev2;
		s_prev2 = s_prev;
		s_prev = s;
	}

	tmp = (s64)s_prev2 * s_prev2 + (s64)s_prev * s_prev -
		      (s64)coeff * s_prev2 * s_prev / 32768;

	/* XXX: N must be low enough so that N*N fits in s32.
	 * Else we need two divisions. */
	divisor = N * N;
	do_div(tmp, divisor);

	return (u32) tmp;
}

static u32 freq_magnitude(s16 x[], u32 N, u32 freq)
{
	u32 sum = int_goertzel(x, N, freq);
	return (u32)int_sqrt(sum);
}

static u32 noise_magnitude(s16 x[], u32 N, u32 freq_start, u32 freq_end)
{
	int i;
	u32 sum = 0;
	u32 freq_step;
	int samples = 5;

	if (N > 192) {
		/* The last 192 samples are enough for noise detection */
		x += (N-192);
		N = 192;
	}

	freq_step = (freq_end - freq_start) / (samples - 1);

	for (i = 0; i < samples; i++) {
		sum += int_goertzel(x, N, freq_start);
		freq_start += freq_step;
	}

	return (u32)int_sqrt(sum / samples);
}

static s32 detect_a2_a2m_eiaj(struct cx88_core *core, s16 x[], u32 N)
{
	s32 carrier, stereo, dual, noise;
	s32 carrier_freq, stereo_freq, dual_freq;
	s32 ret;

	switch (core->tvaudio) {
	case WW_BG:
	case WW_DK:
		carrier_freq = FREQ_A2_CARRIER;
		stereo_freq = FREQ_A2_STEREO;
		dual_freq = FREQ_A2_DUAL;
		break;
	case WW_M:
		carrier_freq = FREQ_A2M_CARRIER;
		stereo_freq = FREQ_A2M_STEREO;
		dual_freq = FREQ_A2M_DUAL;
		break;
	case WW_EIAJ:
		carrier_freq = FREQ_EIAJ_CARRIER;
		stereo_freq = FREQ_EIAJ_STEREO;
		dual_freq = FREQ_EIAJ_DUAL;
		break;
	default:
		printk(KERN_WARNING "%s/0: unsupported audio mode %d for %s\n",
		       core->name, core->tvaudio, __func__);
		return UNSET;
	}

	carrier = freq_magnitude(x, N, carrier_freq);
	stereo  = freq_magnitude(x, N, stereo_freq);
	dual    = freq_magnitude(x, N, dual_freq);
	noise   = noise_magnitude(x, N, FREQ_NOISE_START, FREQ_NOISE_END);

	dprintk(1, "detect a2/a2m/eiaj: carrier=%d, stereo=%d, dual=%d, noise=%d\n",
		carrier, stereo, dual, noise);

	if (stereo > dual)
		ret = V4L2_TUNER_SUB_STEREO;
	else
		ret = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;

	if (core->tvaudio == WW_EIAJ) {
		/* EIAJ checks may need adjustments */
		if ((carrier > max(stereo, dual)*2) &&
		    (carrier < max(stereo, dual)*6) &&
		    (carrier > 20 && carrier < 200) &&
		    (max(stereo, dual) > min(stereo, dual))) {
			/* For EIAJ the carrier is always present,
			   so we probably don't need noise detection */
			return ret;
		}
	} else {
		if ((carrier > max(stereo, dual)*2) &&
		    (carrier < max(stereo, dual)*8) &&
		    (carrier > 20 && carrier < 200) &&
		    (noise < 10) &&
		    (max(stereo, dual) > min(stereo, dual)*2)) {
			return ret;
		}
	}
	return V4L2_TUNER_SUB_MONO;
}

static s32 detect_btsc(struct cx88_core *core, s16 x[], u32 N)
{
	s32 sap_ref = freq_magnitude(x, N, FREQ_BTSC_SAP_REF);
	s32 sap = freq_magnitude(x, N, FREQ_BTSC_SAP);
	s32 dual_ref = freq_magnitude(x, N, FREQ_BTSC_DUAL_REF);
	s32 dual = freq_magnitude(x, N, FREQ_BTSC_DUAL);
	dprintk(1, "detect btsc: dual_ref=%d, dual=%d, sap_ref=%d, sap=%d\n",
		dual_ref, dual, sap_ref, sap);
	/* FIXME: Currently not supported */
	return UNSET;
}

static s16 *read_rds_samples(struct cx88_core *core, u32 *N)
{
	const struct sram_channel *srch = &cx88_sram_channels[SRAM_CH27];
	s16 *samples;

	unsigned int i;
	unsigned int bpl = srch->fifo_size/AUD_RDS_LINES;
	unsigned int spl = bpl/4;
	unsigned int sample_count = spl*(AUD_RDS_LINES-1);

	u32 current_address = cx_read(srch->ptr1_reg);
	u32 offset = (current_address - srch->fifo_start + bpl);

	dprintk(1, "read RDS samples: current_address=%08x (offset=%08x), sample_count=%d, aud_intstat=%08x\n",
		current_address,
		current_address - srch->fifo_start, sample_count,
		cx_read(MO_AUD_INTSTAT));

	samples = kmalloc(sizeof(s16)*sample_count, GFP_KERNEL);
	if (!samples)
		return NULL;

	*N = sample_count;

	for (i = 0; i < sample_count; i++)  {
		offset = offset % (AUD_RDS_LINES*bpl);
		samples[i] = cx_read(srch->fifo_start + offset);
		offset += 4;
	}

	if (dsp_debug >= 2) {
		dprintk(2, "RDS samples dump: ");
		for (i = 0; i < sample_count; i++)
			printk("%hd ", samples[i]);
		printk(".\n");
	}

	return samples;
}

s32 cx88_dsp_detect_stereo_sap(struct cx88_core *core)
{
	s16 *samples;
	u32 N = 0;
	s32 ret = UNSET;

	/* If audio RDS fifo is disabled, we can't read the samples */
	if (!(cx_read(MO_AUD_DMACNTRL) & 0x04))
		return ret;
	if (!(cx_read(AUD_CTL) & EN_FMRADIO_EN_RDS))
		return ret;

	/* Wait at least 500 ms after an audio standard change */
	if (time_before(jiffies, core->last_change + msecs_to_jiffies(500)))
		return ret;

	samples = read_rds_samples(core, &N);

	if (!samples)
		return ret;

	switch (core->tvaudio) {
	case WW_BG:
	case WW_DK:
	case WW_EIAJ:
	case WW_M:
		ret = detect_a2_a2m_eiaj(core, samples, N);
		break;
	case WW_BTSC:
		ret = detect_btsc(core, samples, N);
		break;
	case WW_NONE:
	case WW_I:
	case WW_L:
	case WW_I2SPT:
	case WW_FM:
	case WW_I2SADC:
		break;
	}

	kfree(samples);

	if (UNSET != ret)
		dprintk(1, "stereo/sap detection result:%s%s%s\n",
			   (ret & V4L2_TUNER_SUB_MONO) ? " mono" : "",
			   (ret & V4L2_TUNER_SUB_STEREO) ? " stereo" : "",
			   (ret & V4L2_TUNER_SUB_LANG2) ? " dual" : "");

	return ret;
}
EXPORT_SYMBOL(cx88_dsp_detect_stereo_sap);

