/*
 * Copyright (c) 2014-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "pt_time.h"
#include "pt_opcodes.h"

#include "intel-pt.h"

#include <string.h>
#include <limits.h>


void pt_time_init(struct pt_time *time)
{
	if (!time)
		return;

	memset(time, 0, sizeof(*time));
}

int pt_time_query_tsc(uint64_t *tsc, uint32_t *lost_mtc,
		      uint32_t *lost_cyc, const struct pt_time *time)
{
	if (!tsc || !time)
		return -pte_internal;

	*tsc = time->tsc;

	if (lost_mtc)
		*lost_mtc = time->lost_mtc;
	if (lost_cyc)
		*lost_cyc = time->lost_cyc;

	if (!time->have_tsc)
		return -pte_no_time;

	return 0;
}

int pt_time_query_cbr(uint32_t *cbr, const struct pt_time *time)
{
	if (!cbr || !time)
		return -pte_internal;

	if (!time->have_cbr)
		return -pte_no_cbr;

	*cbr = time->cbr;

	return 0;
}

/* Compute the distance between two CTC sources.
 *
 * We adjust a single wrap-around but fail if the distance is bigger than that.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_time_ctc_delta(uint32_t *ctc_delta, uint32_t ctc,
			     uint32_t last_ctc, const struct pt_config *config)
{
	if (!config || !ctc_delta)
		return -pte_internal;

	/* Correct a single wrap-around.  If we lost enough MTCs to wrap
	 * around twice, timing will be wrong until the next TSC.
	 */
	if (ctc < last_ctc) {
		ctc += 1u << (config->mtc_freq + pt_pl_mtc_bit_size);

		/* Since we only store the CTC between TMA/MTC or MTC/TMC a
		 * single correction should suffice.
		 */
		if (ctc < last_ctc)
			return -pte_bad_packet;
	}

	*ctc_delta = ctc - last_ctc;
	return 0;
}

/* Translate CTC into the same unit as the FastCounter by multiplying with P.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_time_ctc_fc(uint64_t *fc, uint64_t ctc,
			  const struct pt_config *config)
{
	uint32_t eax, ebx;

	if (!fc || !config)
		return -pte_internal;

	eax = config->cpuid_0x15_eax;
	ebx = config->cpuid_0x15_ebx;

	/* Neither multiply nor divide by zero. */
	if (!eax || !ebx)
		return -pte_bad_config;

	*fc = (ctc * ebx) / eax;
	return 0;
}

int pt_time_update_tsc(struct pt_time *time,
		       const struct pt_packet_tsc *packet,
		       const struct pt_config *config)
{
	(void) config;

	if (!time || !packet)
		return -pte_internal;

	time->have_tsc = 1;
	time->have_tma = 0;
	time->have_mtc = 0;
	time->tsc = time->base = packet->tsc;
	time->ctc = 0;
	time->fc = 0ull;

	/* We got the full time; we recover from previous losses. */
	time->lost_mtc = 0;
	time->lost_cyc = 0;

	return 0;
}

int pt_time_update_cbr(struct pt_time *time,
		       const struct pt_packet_cbr *packet,
		       const struct pt_config *config)
{
	(void) config;

	if (!time || !packet)
		return -pte_internal;

	time->have_cbr = 1;
	time->cbr = packet->ratio;

	return 0;
}

int pt_time_update_tma(struct pt_time *time,
		       const struct pt_packet_tma *packet,
		       const struct pt_config *config)
{
	uint32_t ctc, mtc_freq, mtc_hi, ctc_mask;
	uint64_t fc;

	if (!time || !packet || !config)
		return -pte_internal;

	/* Without a TSC something is seriously wrong. */
	if (!time->have_tsc)
		return -pte_bad_context;

	/* We shouldn't have more than one TMA per TSC. */
	if (time->have_tma)
		return -pte_bad_context;

	/* We're ignoring MTC between TSC and TMA. */
	if (time->have_mtc)
		return -pte_internal;

	ctc = packet->ctc;
	fc = packet->fc;

	mtc_freq = config->mtc_freq;
	mtc_hi = mtc_freq + pt_pl_mtc_bit_size;

	/* A mask for the relevant CTC bits ignoring high-order bits that are
	 * not provided by MTC.
	 */
	ctc_mask = (1u << mtc_hi) - 1u;

	time->have_tma = 1;
	time->base -= fc;
	time->fc += fc;

	/* If the MTC frequency is low enough that TMA provides the full CTC
	 * value, we can use the TMA as an MTC.
	 *
	 * If it isn't, we will estimate the preceding MTC based on the CTC bits
	 * the TMA provides at the next MTC.  We forget about the previous MTC
	 * in this case.
	 *
	 * If no MTC packets are dropped around TMA, we will estimate the
	 * forgotten value again at the next MTC.
	 *
	 * If MTC packets are dropped, we can't really tell where in this
	 * extended MTC period the TSC occurred.  The estimation will place it
	 * right before the next MTC.
	 */
	if (mtc_hi <= pt_pl_tma_ctc_bit_size)
		time->have_mtc = 1;

	/* In both cases, we store the TMA's CTC bits until the next MTC. */
	time->ctc = time->ctc_cyc = ctc & ctc_mask;

	return 0;
}

int pt_time_update_mtc(struct pt_time *time,
		       const struct pt_packet_mtc *packet,
		       const struct pt_config *config)
{
	uint32_t last_ctc, ctc, ctc_delta;
	uint64_t tsc, base;
	uint8_t mtc_freq;
	int errcode, have_tsc, have_tma, have_mtc;

	if (!time || !packet || !config)
		return -pte_internal;

	have_tsc = time->have_tsc;
	have_tma = time->have_tma;
	have_mtc = time->have_mtc;

	/* We ignore MTCs between TSC and TMA to avoid apparent CTC overflows.
	 *
	 * Later MTCs will ensure that no time is lost - provided TMA provides
	 * enough bits.  If TMA doesn't provide any of the MTC bits we may place
	 * the TSC into the wrong MTC period.
	 */
	if (have_tsc && !have_tma)
		return 0;

	base = time->base;
	last_ctc = time->ctc;
	mtc_freq = config->mtc_freq;

	ctc = packet->ctc << mtc_freq;

	/* Store our CTC value if we have or would have reset FC. */
	if (time->fc || time->lost_cyc || !have_mtc)
		time->ctc_cyc = ctc;

	/* Prepare for the next packet in case we error out below. */
	time->have_mtc = 1;
	time->fc = 0ull;
	time->ctc = ctc;

	/* We recover from previous CYC losses. */
	time->lost_cyc = 0;

	/* Avoid a big jump when we see the first MTC with an arbitrary CTC
	 * payload.
	 */
	if (!have_mtc) {
		uint32_t ctc_lo, ctc_hi;

		/* If we have not seen a TMA, we ignore this first MTC.
		 *
		 * We have no idea where in this MTC period tracing started.
		 * We could lose an entire MTC period or just a tiny fraction.
		 *
		 * On the other hand, if we assumed a previous MTC value, we
		 * might make just the same error.
		 */
		if (!have_tma)
			return 0;

		/* The TMA's CTC value didn't provide enough bits - otherwise,
		 * we would have treated the TMA as an MTC.
		 */
		if (last_ctc & ~pt_pl_tma_ctc_mask)
			return -pte_internal;

		/* Split this MTC's CTC value into low and high parts with
		 * respect to the bits provided by TMA.
		 */
		ctc_lo = ctc & pt_pl_tma_ctc_mask;
		ctc_hi = ctc & ~pt_pl_tma_ctc_mask;

		/* We estimate the high-order CTC bits that are not provided by
		 * TMA based on the CTC bits provided by this MTC.
		 *
		 * We assume that no MTC packets were dropped around TMA.  If
		 * there are, we might place the TSC into the wrong MTC period
		 * depending on how many CTC bits TMA provides and how many MTC
		 * packets were dropped.
		 *
		 * Note that we may underflow which results in more bits to be
		 * set than MTC packets may provide.  Drop those extra bits.
		 */
		if (ctc_lo < last_ctc) {
			ctc_hi -= 1u << pt_pl_tma_ctc_bit_size;
			ctc_hi &= pt_pl_mtc_mask << mtc_freq;
		}

		last_ctc |= ctc_hi;
	}

	errcode = pt_time_ctc_delta(&ctc_delta, ctc, last_ctc, config);
	if (errcode < 0) {
		time->lost_mtc += 1;
		return errcode;
	}

	errcode = pt_time_ctc_fc(&tsc, ctc_delta, config);
	if (errcode < 0)
		return errcode;

	base += tsc;
	time->tsc = time->base = base;

	return 0;
}

/* Adjust a CYC packet's payload spanning multiple MTC periods.
 *
 * CYC packets measure the Fast Counter since the last CYC(-eligible) packet.
 * Depending on the CYC threshold, we may not get a CYC for each MTC, so a CYC
 * period may overlap with or even span multiple MTC periods.
 *
 * We can't do much about the overlap case without examining all packets in
 * the respective periods.  We leave this as expected imprecision.
 *
 * If we find a CYC packet to span multiple MTC packets, though, we try to
 * approximate the portion for the current MTC period by subtracting the
 * estimated portion for previous MTC periods using calibration information.
 *
 * We only consider MTC.  For the first CYC after TSC, the corresponding TMA
 * will contain the Fast Counter at TSC.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_time_adjust_cyc(uint64_t *cyc, const struct pt_time *time,
			      const struct pt_config *config, uint64_t fcr)
{
	uint32_t last_ctc, ctc, ctc_delta;
	uint64_t fc, total_cyc, old_cyc;
	int errcode;

	if (!time || !config || !fcr)
		return -pte_internal;

	last_ctc = time->ctc_cyc;
	ctc = time->ctc;

	/* There is nothing to do if this is the current MTC period. */
	if (ctc == last_ctc)
		return 0;

	/* Calibration computes
	 *
	 *   fc  = (ctc_delta * cpuid[0x15].ebx) / cpuid[0x15].eax.
	 *   fcr = (fc << pt_tcal_fcr_shr) / cyc
	 *
	 * So cyc = (fc << pt_tcal_fcr_shr) / fcr.
	 */

	errcode = pt_time_ctc_delta(&ctc_delta, ctc, last_ctc, config);
	if (errcode < 0)
		return errcode;

	errcode = pt_time_ctc_fc(&fc, ctc_delta, config);
	if (errcode < 0)
		return errcode;

	old_cyc = (fc << pt_tcal_fcr_shr) / fcr;
	total_cyc = *cyc;

	/* Make sure we don't wrap around.  If we would, attribute the entire
	 * CYC payload to any previous MTC period.
	 *
	 * We lost an unknown portion of the CYC payload for the current MTC
	 * period, but it's usually better to run too slow than too fast.
	 */
	if (total_cyc < old_cyc)
		total_cyc = old_cyc;

	*cyc = total_cyc - old_cyc;
	return 0;
}

int pt_time_update_cyc(struct pt_time *time,
		       const struct pt_packet_cyc *packet,
		       const struct pt_config *config, uint64_t fcr)
{
	uint64_t cyc, fc;

	if (!time || !packet || !config)
		return -pte_internal;

	if (!fcr) {
		time->lost_cyc += 1;
		return 0;
	}

	cyc = packet->value;
	fc = time->fc;
	if (!fc) {
		int errcode;

		errcode = pt_time_adjust_cyc(&cyc, time, config, fcr);
		if (errcode < 0)
			return errcode;
	}

	fc += (cyc * fcr) >> pt_tcal_fcr_shr;

	time->fc = fc;
	time->tsc = time->base + fc;

	return 0;
}

void pt_tcal_init(struct pt_time_cal *tcal)
{
	if (!tcal)
		return;

	memset(tcal, 0, sizeof(*tcal));

	tcal->min_fcr = UINT64_MAX;
}

static int pt_tcal_have_fcr(const struct pt_time_cal *tcal)
{
	if (!tcal)
		return 0;

	return (tcal->min_fcr <= tcal->max_fcr);
}

int pt_tcal_fcr(uint64_t *fcr, const struct pt_time_cal *tcal)
{
	if (!fcr || !tcal)
		return -pte_internal;

	if (!pt_tcal_have_fcr(tcal))
		return -pte_no_time;

	*fcr = tcal->fcr;

	return 0;
}

int pt_tcal_set_fcr(struct pt_time_cal *tcal, uint64_t fcr)
{
	if (!tcal)
		return -pte_internal;

	tcal->fcr = fcr;

	if (fcr < tcal->min_fcr)
		tcal->min_fcr = fcr;

	if (fcr > tcal->max_fcr)
		tcal->max_fcr = fcr;

	return 0;
}

int pt_tcal_update_tsc(struct pt_time_cal *tcal,
		      const struct pt_packet_tsc *packet,
		      const struct pt_config *config)
{
	(void) config;

	if (!tcal || !packet)
		return -pte_internal;

	/* A TSC outside of PSB+ may indicate loss of time.  We do not use it
	 * for calibration.  We store the TSC value for calibration at the next
	 * TSC in PSB+, though.
	 */
	tcal->tsc = packet->tsc;
	tcal->cyc_tsc = 0ull;

	return 0;
}

int pt_tcal_header_tsc(struct pt_time_cal *tcal,
		      const struct pt_packet_tsc *packet,
		      const struct pt_config *config)
{
	uint64_t tsc, last_tsc, tsc_delta, cyc, fcr;

	(void) config;

	if (!tcal || !packet)
		return -pte_internal;

	last_tsc = tcal->tsc;
	cyc = tcal->cyc_tsc;

	tsc = packet->tsc;

	tcal->tsc = tsc;
	tcal->cyc_tsc = 0ull;

	if (!last_tsc || !cyc)
		return 0;

	/* Correct a single wrap-around. */
	if (tsc < last_tsc) {
		tsc += 1ull << pt_pl_tsc_bit_size;

		if (tsc < last_tsc)
			return -pte_bad_packet;
	}

	tsc_delta = tsc - last_tsc;

	/* We shift the nominator to improve rounding precision.
	 *
	 * Since we're only collecting the CYCs between two TSC, we shouldn't
	 * overflow.  Let's rather fail than overflow.
	 */
	if (tsc_delta & ~(~0ull >> pt_tcal_fcr_shr))
		return -pte_internal;

	fcr = (tsc_delta << pt_tcal_fcr_shr) / cyc;

	return pt_tcal_set_fcr(tcal, fcr);
}

int pt_tcal_update_cbr(struct pt_time_cal *tcal,
		      const struct pt_packet_cbr *packet,
		      const struct pt_config *config)
{
	/* A CBR outside of PSB+ indicates a frequency change.  Reset our
	 * calibration state.
	 */
	pt_tcal_init(tcal);

	return pt_tcal_header_cbr(tcal, packet, config);
}

int pt_tcal_header_cbr(struct pt_time_cal *tcal,
		      const struct pt_packet_cbr *packet,
		      const struct pt_config *config)
{
	uint64_t cbr, p1, fcr;

	if (!tcal || !packet || !config)
		return -pte_internal;

	p1 = config->nom_freq;
	if (!p1)
		return 0;

	/* If we know the nominal frequency, we can use it for calibration. */
	cbr = packet->ratio;

	fcr = (p1 << pt_tcal_fcr_shr) / cbr;

	return pt_tcal_set_fcr(tcal, fcr);
}

int pt_tcal_update_tma(struct pt_time_cal *tcal,
		      const struct pt_packet_tma *packet,
		      const struct pt_config *config)
{
	(void) tcal;
	(void) packet;
	(void) config;

	/* Nothing to do. */
	return 0;
}

int pt_tcal_update_mtc(struct pt_time_cal *tcal,
		      const struct pt_packet_mtc *packet,
		      const struct pt_config *config)
{
	uint32_t last_ctc, ctc, ctc_delta, have_mtc;
	uint64_t cyc, fc, fcr;
	int errcode;

	if (!tcal || !packet || !config)
		return -pte_internal;

	last_ctc = tcal->ctc;
	have_mtc = tcal->have_mtc;
	cyc = tcal->cyc_mtc;

	ctc = packet->ctc << config->mtc_freq;

	/* We need at least two MTC (including this). */
	if (!have_mtc) {
		tcal->cyc_mtc = 0ull;
		tcal->ctc = ctc;
		tcal->have_mtc = 1;

		return 0;
	}

	/* Without any cycles, we can't calibrate.  Try again at the next
	 * MTC and distribute the cycles over the combined MTC period.
	 */
	if (!cyc)
		return 0;

	/* Prepare for the next packet in case we error out below. */
	tcal->have_mtc = 1;
	tcal->cyc_mtc = 0ull;
	tcal->ctc = ctc;

	/* Let's pretend we will fail.  We'll correct it at the end. */
	tcal->lost_mtc += 1;

	errcode = pt_time_ctc_delta(&ctc_delta, ctc, last_ctc, config);
	if (errcode < 0)
		return errcode;

	errcode = pt_time_ctc_fc(&fc, ctc_delta, config);
	if (errcode < 0)
		return errcode;

	/* We shift the nominator to improve rounding precision.
	 *
	 * Since we're only collecting the CYCs between two MTC, we shouldn't
	 * overflow.  Let's rather fail than overflow.
	 */
	if (fc & ~(~0ull >> pt_tcal_fcr_shr))
		return -pte_internal;

	fcr = (fc << pt_tcal_fcr_shr) / cyc;

	errcode = pt_tcal_set_fcr(tcal, fcr);
	if (errcode < 0)
		return errcode;

	/* We updated the FCR.  This recovers from previous MTC losses. */
	tcal->lost_mtc = 0;

	return 0;
}

int pt_tcal_update_cyc(struct pt_time_cal *tcal,
		      const struct pt_packet_cyc *packet,
		      const struct pt_config *config)
{
	uint64_t cyc;

	(void) config;

	if (!tcal || !packet)
		return -pte_internal;

	cyc = packet->value;
	tcal->cyc_mtc += cyc;
	tcal->cyc_tsc += cyc;

	return 0;
}
