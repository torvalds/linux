/*
 * util/rtt.h - UDP round trip time estimator for resend timeouts.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains a data type and functions to help estimate good
 * round trip times for UDP resend timeout values.
 */

#ifndef UTIL_RTT_H
#define UTIL_RTT_H

/**
 * RTT information. Keeps packet Round Trip Time.
 */
struct rtt_info {
	/** smoothed rtt estimator, in milliseconds */
	int srtt;
	/** smoothed mean deviation, in milliseconds */
	int rttvar;
	/** current RTO in use, in milliseconds */
	int rto;
};

/** min retransmit timeout value, in milliseconds */
extern int RTT_MIN_TIMEOUT;
/** max retransmit timeout value, in milliseconds */
#define RTT_MAX_TIMEOUT 120000

/**
 * Initialize RTT estimators.
 * @param rtt: The structure. Caller is responsible for allocation of it.
 */
void rtt_init(struct rtt_info* rtt);

/** 
 * Get timeout to use for sending a UDP packet.
 * @param rtt: round trip statistics structure.
 * @return: timeout to use in milliseconds. Relative time value.
 */
int rtt_timeout(const struct rtt_info* rtt);

/** 
 * Get unclamped timeout to use for server selection.
 * Recent timeouts are reflected in the returned value.
 * @param rtt: round trip statistics structure.
 * @return: value to use in milliseconds. 
 */
int rtt_unclamped(const struct rtt_info* rtt);

/**
 * RTT for valid responses. Without timeouts.
 * @param rtt: round trip statistics structure.
 * @return: value in msec.
 */
int rtt_notimeout(const struct rtt_info* rtt);

/**
 * Update the statistics with a new roundtrip estimate observation.
 * @param rtt: round trip statistics structure.
 * @param ms: estimate of roundtrip time in milliseconds.
 */
void rtt_update(struct rtt_info* rtt, int ms);

/**
 * Update the statistics with a new timeout expired observation.
 * @param rtt: round trip statistics structure.
 * @param orig: original rtt time given for the query that timed out.
 * 	Used to calculate the maximum responsible backed off time that
 * 	can reasonably be applied.
 */
void rtt_lost(struct rtt_info* rtt, int orig);

#endif /* UTIL_RTT_H */
