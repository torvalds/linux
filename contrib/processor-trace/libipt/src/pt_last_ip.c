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

#include "pt_last_ip.h"

#include "intel-pt.h"


void pt_last_ip_init(struct pt_last_ip *last_ip)
{
	if (!last_ip)
		return;

	last_ip->ip = 0ull;
	last_ip->have_ip = 0;
	last_ip->suppressed = 0;
}

int pt_last_ip_query(uint64_t *ip, const struct pt_last_ip *last_ip)
{
	if (!last_ip)
		return -pte_internal;

	if (!last_ip->have_ip) {
		if (ip)
			*ip = 0ull;
		return -pte_noip;
	}

	if (last_ip->suppressed) {
		if (ip)
			*ip = 0ull;
		return -pte_ip_suppressed;
	}

	if (ip)
		*ip = last_ip->ip;

	return 0;
}

/* Sign-extend a uint64_t value. */
static uint64_t sext(uint64_t val, uint8_t sign)
{
	uint64_t signbit, mask;

	signbit = 1ull << (sign - 1);
	mask = ~0ull << sign;

	return val & signbit ? val | mask : val & ~mask;
}

int pt_last_ip_update_ip(struct pt_last_ip *last_ip,
			 const struct pt_packet_ip *packet,
			 const struct pt_config *config)
{
	(void) config;

	if (!last_ip || !packet)
		return -pte_internal;

	switch (packet->ipc) {
	case pt_ipc_suppressed:
		last_ip->suppressed = 1;
		return 0;

	case pt_ipc_sext_48:
		last_ip->ip = sext(packet->ip, 48);
		last_ip->have_ip = 1;
		last_ip->suppressed = 0;
		return 0;

	case pt_ipc_update_16:
		last_ip->ip = (last_ip->ip & ~0xffffull)
			| (packet->ip & 0xffffull);
		last_ip->have_ip = 1;
		last_ip->suppressed = 0;
		return 0;

	case pt_ipc_update_32:
		last_ip->ip = (last_ip->ip & ~0xffffffffull)
			| (packet->ip & 0xffffffffull);
		last_ip->have_ip = 1;
		last_ip->suppressed = 0;
		return 0;

	case pt_ipc_update_48:
		last_ip->ip = (last_ip->ip & ~0xffffffffffffull)
			| (packet->ip & 0xffffffffffffull);
		last_ip->have_ip = 1;
		last_ip->suppressed = 0;
		return 0;

	case pt_ipc_full:
		last_ip->ip = packet->ip;
		last_ip->have_ip = 1;
		last_ip->suppressed = 0;
		return 0;
	}

	return -pte_bad_packet;
}
