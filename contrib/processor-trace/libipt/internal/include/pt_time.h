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

#ifndef PT_TIME_H
#define PT_TIME_H

#include <stdint.h>

struct pt_config;
struct pt_packet_tsc;
struct pt_packet_cbr;
struct pt_packet_tma;
struct pt_packet_mtc;
struct pt_packet_cyc;


/* Intel(R) Processor Trace timing. */
struct pt_time {
	/* The estimated Time Stamp Count. */
	uint64_t tsc;

	/* The base Time Stamp Count (from TSC and MTC). */
	uint64_t base;

	/* The estimated Fast Counter. */
	uint64_t fc;

	/* The adjusted last CTC value (from MTC and TMA). */
	uint32_t ctc;

	/* The adjusted CTC value when @fc was cleared (from MTC and TMA). */
	uint32_t ctc_cyc;

	/* The number of lost MTC updates. */
	uint32_t lost_mtc;

	/* The number of lost CYC updates. */
	uint32_t lost_cyc;

	/* The core:bus ratio. */
	uint8_t cbr;

	/* A flag saying whether we have seen a TSC packet. */
	uint32_t have_tsc:1;

	/* A flag saying whether we have seen a CBR packet. */
	uint32_t have_cbr:1;

	/* A flag saying whether we have seen a TMA packet. */
	uint32_t have_tma:1;

	/* A flag saying whether we have seen a MTC packet. */
	uint32_t have_mtc:1;
};

/* Initialize (or reset) the time. */
extern void pt_time_init(struct pt_time *time);

/* Query the current time.
 *
 * Provides the estimated Time Stamp Count value in @tsc.
 *
 * If @lost_mtc is not NULL, provides the number of lost MTC packets.
 * If @lost_cyc is not NULL, provides the number of lost CYC packets.
 *
 * Returns zero on success; a negative error code, otherwise.
 * Returns -pte_internal if @tsc or @time is NULL.
 * Returns -pte_no_time if there has not been a TSC packet.
 */
extern int pt_time_query_tsc(uint64_t *tsc, uint32_t *lost_mtc,
			     uint32_t *lost_cyc, const struct pt_time *time);

/* Query the current core:bus ratio.
 *
 * Provides the core:bus ratio in @cbr.
 *
 * Returns zero on success; a negative error code, otherwise.
 * Returns -pte_internal if @cbr or @time is NULL.
 * Returns -pte_no_cbr if there has not been a CBR packet.
 */
extern int pt_time_query_cbr(uint32_t *cbr, const struct pt_time *time);

/* Update the time based on an Intel PT packet.
 *
 * Returns zero on success.
 * Returns a negative error code, otherwise.
 */
extern int pt_time_update_tsc(struct pt_time *, const struct pt_packet_tsc *,
			      const struct pt_config *);
extern int pt_time_update_cbr(struct pt_time *, const struct pt_packet_cbr *,
			      const struct pt_config *);
extern int pt_time_update_tma(struct pt_time *, const struct pt_packet_tma *,
			      const struct pt_config *);
extern int pt_time_update_mtc(struct pt_time *, const struct pt_packet_mtc *,
			      const struct pt_config *);
/* @fcr is the fast-counter:cycles ratio obtained by calibration. */
extern int pt_time_update_cyc(struct pt_time *, const struct pt_packet_cyc *,
			      const struct pt_config *, uint64_t fcr);


/* Timing calibration.
 *
 * Used for estimating the Fast-Counter:Cycles ratio.
 *
 * Ideally, we calibrate by counting CYCs between MTCs.  Lacking MTCs, we
 * use TSC, instead.
 */
struct pt_time_cal {
	/* The estimated fast-counter:cycles ratio. */
	uint64_t fcr;

	/* The minimal and maximal @fcr values. */
	uint64_t min_fcr, max_fcr;

	/* The last TSC value.
	 *
	 * Used for calibrating at TSC.
	 */
	uint64_t tsc;

	/* The number of cycles since the last TSC (from CYC).
	 *
	 * Used for calibrating at TSC.
	 */
	uint64_t cyc_tsc;

	/* The number of cycles since the last MTC (from CYC).
	 *
	 * Used for calibrating at MTC.
	 */
	uint64_t cyc_mtc;

	/* The adjusted last CTC value (from MTC).
	 *
	 * Used for calibrating at MTC.
	 */
	uint32_t ctc;

	/* The number of lost MTC updates since the last successful update. */
	uint32_t lost_mtc;

	/* A flag saying whether we have seen a MTC packet. */
	uint32_t have_mtc:1;
};

enum {
	/* The amount by which the fcr value is right-shifted.
	 *
	 * Do not shift the value obtained by pt_tcal_fcr() when passing it to
	 * pt_time_update_cyc().
	 * Do shift the value passed to pt_tcal_set_fcr().
	 */
	pt_tcal_fcr_shr	= 8
};

/* Initialize of reset timing calibration. */
extern void pt_tcal_init(struct pt_time_cal *tcal);

/* Query the estimated fast-counter:cycles ratio.
 *
 * Provides the estimated ratio in @fcr unless -pte_internal or
 * -pte_no_time is returned.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @fcr or @tcal is NULL.
 * Returns -pte_no_time if no information is available.
 */
extern int pt_tcal_fcr(uint64_t *fcr, const struct pt_time_cal *tcal);

/* Set the fast-counter:cycles ratio.
 *
 * Timing calibration takes one CBR or two MTC packets before it can provide
 * first estimations.  Use this to supply an initial value to be used in the
 * meantime.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @cal is NULL.
 */
extern int pt_tcal_set_fcr(struct pt_time_cal *tcal, uint64_t fcr);

/* Update calibration based on an Intel PT packet.
 *
 * Returns zero on success, a negative error code otherwise.
 */
extern int pt_tcal_update_tsc(struct pt_time_cal *,
			      const struct pt_packet_tsc *,
			      const struct pt_config *);
extern int pt_tcal_header_tsc(struct pt_time_cal *,
			      const struct pt_packet_tsc *,
			      const struct pt_config *);
extern int pt_tcal_update_cbr(struct pt_time_cal *,
			      const struct pt_packet_cbr *,
			      const struct pt_config *);
extern int pt_tcal_header_cbr(struct pt_time_cal *,
			      const struct pt_packet_cbr *,
			      const struct pt_config *);
extern int pt_tcal_update_tma(struct pt_time_cal *,
			      const struct pt_packet_tma *,
			      const struct pt_config *);
extern int pt_tcal_update_mtc(struct pt_time_cal *,
			      const struct pt_packet_mtc *,
			      const struct pt_config *);
extern int pt_tcal_update_cyc(struct pt_time_cal *,
			      const struct pt_packet_cyc *,
			      const struct pt_config *);

#endif /* PT_TIME_H */
