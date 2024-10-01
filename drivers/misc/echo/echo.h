/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SpanDSP - a series of DSP components for telephony
 *
 * echo.c - A line echo canceller.  This code is being developed
 *          against and partially complies with G168.
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *         and David Rowe <david_at_rowetel_dot_com>
 *
 * Copyright (C) 2001 Steve Underwood and 2007 David Rowe
 *
 * All rights reserved.
 */

#ifndef __ECHO_H
#define __ECHO_H

/*
Line echo cancellation for voice

What does it do?

This module aims to provide G.168-2002 compliant echo cancellation, to remove
electrical echoes (e.g. from 2-4 wire hybrids) from voice calls.

How does it work?

The heart of the echo cancellor is FIR filter. This is adapted to match the
echo impulse response of the telephone line. It must be long enough to
adequately cover the duration of that impulse response. The signal transmitted
to the telephone line is passed through the FIR filter. Once the FIR is
properly adapted, the resulting output is an estimate of the echo signal
received from the line. This is subtracted from the received signal. The result
is an estimate of the signal which originated at the far end of the line, free
from echos of our own transmitted signal.

The least mean squares (LMS) algorithm is attributed to Widrow and Hoff, and
was introduced in 1960. It is the commonest form of filter adaption used in
things like modem line equalisers and line echo cancellers. There it works very
well.  However, it only works well for signals of constant amplitude. It works
very poorly for things like speech echo cancellation, where the signal level
varies widely.  This is quite easy to fix. If the signal level is normalised -
similar to applying AGC - LMS can work as well for a signal of varying
amplitude as it does for a modem signal. This normalised least mean squares
(NLMS) algorithm is the commonest one used for speech echo cancellation. Many
other algorithms exist - e.g. RLS (essentially the same as Kalman filtering),
FAP, etc. Some perform significantly better than NLMS.  However, factors such
as computational complexity and patents favour the use of NLMS.

A simple refinement to NLMS can improve its performance with speech. NLMS tends
to adapt best to the strongest parts of a signal. If the signal is white noise,
the NLMS algorithm works very well. However, speech has more low frequency than
high frequency content. Pre-whitening (i.e. filtering the signal to flatten its
spectrum) the echo signal improves the adapt rate for speech, and ensures the
final residual signal is not heavily biased towards high frequencies. A very
low complexity filter is adequate for this, so pre-whitening adds little to the
compute requirements of the echo canceller.

An FIR filter adapted using pre-whitened NLMS performs well, provided certain
conditions are met:

    - The transmitted signal has poor self-correlation.
    - There is no signal being generated within the environment being
      cancelled.

The difficulty is that neither of these can be guaranteed.

If the adaption is performed while transmitting noise (or something fairly
noise like, such as voice) the adaption works very well. If the adaption is
performed while transmitting something highly correlative (typically narrow
band energy such as signalling tones or DTMF), the adaption can go seriously
wrong. The reason is there is only one solution for the adaption on a near
random signal - the impulse response of the line. For a repetitive signal,
there are any number of solutions which converge the adaption, and nothing
guides the adaption to choose the generalised one. Allowing an untrained
canceller to converge on this kind of narrowband energy probably a good thing,
since at least it cancels the tones. Allowing a well converged canceller to
continue converging on such energy is just a way to ruin its generalised
adaption. A narrowband detector is needed, so adapation can be suspended at
appropriate times.

The adaption process is based on trying to eliminate the received signal. When
there is any signal from within the environment being cancelled it may upset
the adaption process. Similarly, if the signal we are transmitting is small,
noise may dominate and disturb the adaption process. If we can ensure that the
adaption is only performed when we are transmitting a significant signal level,
and the environment is not, things will be OK. Clearly, it is easy to tell when
we are sending a significant signal. Telling, if the environment is generating
a significant signal, and doing it with sufficient speed that the adaption will
not have diverged too much more we stop it, is a little harder.

The key problem in detecting when the environment is sourcing significant
energy is that we must do this very quickly. Given a reasonably long sample of
the received signal, there are a number of strategies which may be used to
assess whether that signal contains a strong far end component. However, by the
time that assessment is complete the far end signal will have already caused
major mis-convergence in the adaption process. An assessment algorithm is
needed which produces a fairly accurate result from a very short burst of far
end energy.

How do I use it?

The echo cancellor processes both the transmit and receive streams sample by
sample. The processing function is not declared inline. Unfortunately,
cancellation requires many operations per sample, so the call overhead is only
a minor burden.
*/

#include "fir.h"
#include "oslec.h"

/*
    G.168 echo canceller descriptor. This defines the working state for a line
    echo canceller.
*/
struct oslec_state {
	int16_t tx;
	int16_t rx;
	int16_t clean;
	int16_t clean_nlp;

	int nonupdate_dwell;
	int curr_pos;
	int taps;
	int log2taps;
	int adaption_mode;

	int cond_met;
	int32_t pstates;
	int16_t adapt;
	int32_t factor;
	int16_t shift;

	/* Average levels and averaging filter states */
	int ltxacc;
	int lrxacc;
	int lcleanacc;
	int lclean_bgacc;
	int ltx;
	int lrx;
	int lclean;
	int lclean_bg;
	int lbgn;
	int lbgn_acc;
	int lbgn_upper;
	int lbgn_upper_acc;

	/* foreground and background filter states */
	struct fir16_state_t fir_state;
	struct fir16_state_t fir_state_bg;
	int16_t *fir_taps16[2];

	/* DC blocking filter states */
	int tx_1;
	int tx_2;
	int rx_1;
	int rx_2;

	/* optional High Pass Filter states */
	int32_t xvtx[5];
	int32_t yvtx[5];
	int32_t xvrx[5];
	int32_t yvrx[5];

	/* Parameters for the optional Hoth noise generator */
	int cng_level;
	int cng_rndnum;
	int cng_filter;

	/* snapshot sample of coeffs used for development */
	int16_t *snapshot;
};

#endif /* __ECHO_H */
