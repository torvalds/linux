/*
 * Copyright (c) 2013-2018, Intel Corporation
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

#ifndef PT_LAST_IP_H
#define PT_LAST_IP_H

#include <stdint.h>

struct pt_packet_ip;
struct pt_config;


/* Keeping track of the last-ip in Intel PT packets. */
struct pt_last_ip {
	/* The last IP. */
	uint64_t ip;

	/* Flags governing the handling of IP updates and queries:
	 *
	 * - we have seen an IP update.
	 */
	uint32_t have_ip:1;
	/* - the IP has been suppressed in the last update. */
	uint32_t suppressed:1;
};


/* Initialize (or reset) the last-ip. */
extern void pt_last_ip_init(struct pt_last_ip *last_ip);

/* Query the last-ip.
 *
 * If @ip is not NULL, provides the last-ip in @ip on success.
 *
 * Returns zero on success.
 * Returns -pte_internal if @last_ip is NULL.
 * Returns -pte_noip if there is no last-ip.
 * Returns -pte_ip_suppressed if the last-ip has been suppressed.
 */
extern int pt_last_ip_query(uint64_t *ip, const struct pt_last_ip *last_ip);

/* Update last-ip.
 *
 * Updates @last_ip based on @packet and, if non-null, @config.
 *
 * Returns zero on success.
 * Returns -pte_internal if @last_ip or @packet is NULL.
 * Returns -pte_bad_packet if @packet appears to be corrupted.
 */
extern int pt_last_ip_update_ip(struct pt_last_ip *last_ip,
				const struct pt_packet_ip *packet,
				const struct pt_config *config);

#endif /* PT_LAST_IP_H */
