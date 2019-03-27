/*
 * iterator/iter_resptype.h - response type information and classification.
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
 * This file defines the response type. DNS Responses can be classified as
 * one of the response types.
 */

#ifndef ITERATOR_ITER_RESPTYPE_H
#define ITERATOR_ITER_RESPTYPE_H
struct dns_msg;
struct query_info;
struct delegpt;

/**
 * The response type is used to interpret the response.
 */
enum response_type {
	/** 
	 * 'untyped' means that the type of this response hasn't been 
	 * assigned. 
	 */
	RESPONSE_TYPE_UNTYPED   = 0,

	/** 
	 * 'answer' means that the response terminates the resolution 
	 * process. 
	 */
	RESPONSE_TYPE_ANSWER,

	/** 'delegation' means that the response is a delegation. */
	RESPONSE_TYPE_REFERRAL,

	/**
	 * 'cname' means that the response is a cname without the final 
	 * answer, and thus must be restarted.
	 */
	RESPONSE_TYPE_CNAME,

	/**
	 * 'throwaway' means that this particular response should be 
	 * discarded and the next nameserver should be contacted
	 */
	RESPONSE_TYPE_THROWAWAY,

	/**
	 * 'lame' means that this particular response indicates that 
	 * the nameserver knew nothing about the question.
	 */
	RESPONSE_TYPE_LAME,

	/**
	 * Recursion lame means that the nameserver is some sort of
	 * open recursor, and not authoritative for the question.
	 * It may know something, but not authoritatively.
	 */
	RESPONSE_TYPE_REC_LAME
};

/**
 * Classifies a response message from cache based on the current request.
 * Note that this routine assumes that THROWAWAY or LAME responses will not
 * occur. Also, it will not detect REFERRAL type messages, since those are
 * (currently) automatically classified based on how they came from the
 * cache (findDelegation() instead of lookup()).
 *
 * @param msg: the message from the cache.
 * @param request: the request that generated the response.
 * @return the response type (CNAME or ANSWER).
 */
enum response_type response_type_from_cache(struct dns_msg* msg, 
	struct query_info* request);

/**
 * Classifies a response message (from the wire) based on the current
 * request.
 *
 * NOTE: currently this routine uses the AA bit in the response to help
 * distinguish between some non-standard referrals and answers. It also
 * relies somewhat on the originating zone to be accurate (for lameness
 * detection, mostly).
 *
 * @param rdset: if RD bit was sent in query sent by unbound.
 * @param msg: the message from the cache.
 * @param request: the request that generated the response.
 * @param dp: The delegation point that was being queried
 *          when the response was returned.
 * @return the response type (CNAME or ANSWER).
 */
enum response_type response_type_from_server(int rdset, 
	struct dns_msg* msg, struct query_info* request, struct delegpt* dp);

#endif /* ITERATOR_ITER_RESPTYPE_H */
