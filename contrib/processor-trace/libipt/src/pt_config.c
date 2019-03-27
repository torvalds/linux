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

#include "pt_config.h"
#include "pt_opcodes.h"

#include "intel-pt.h"

#include <string.h>
#include <stddef.h>


int pt_cpu_errata(struct pt_errata *errata, const struct pt_cpu *cpu)
{
	if (!errata || !cpu)
		return -pte_invalid;

	memset(errata, 0, sizeof(*errata));

	/* We don't know about others. */
	if (cpu->vendor != pcv_intel)
		return -pte_bad_cpu;

	switch (cpu->family) {
	case 0x6:
		switch (cpu->model) {
		case 0x3d:
		case 0x47:
		case 0x4f:
		case 0x56:
			errata->bdm70 = 1;
			errata->bdm64 = 1;
			return 0;

		case 0x4e:
		case 0x5e:
			errata->bdm70 = 1;
			errata->skd007 = 1;
			errata->skd022 = 1;
			errata->skd010 = 1;
			errata->skl014 = 1;
			return 0;

		case 0x8e:
		case 0x9e:
			errata->bdm70 = 1;
			errata->skl014 = 1;
			errata->skd022 = 1;
			errata->skd010 = 1;
			errata->skd007 = 1;
			return 0;

		case 0x5c:
		case 0x5f:
			errata->apl12 = 1;
			errata->apl11 = 1;
			return 0;
		}
		break;
	}

	return -pte_bad_cpu;
}

int pt_config_from_user(struct pt_config *config,
			const struct pt_config *uconfig)
{
	uint8_t *begin, *end;
	size_t size;

	if (!config)
		return -pte_internal;

	if (!uconfig)
		return -pte_invalid;

	size = uconfig->size;
	if (size < offsetof(struct pt_config, decode))
		return -pte_bad_config;

	begin = uconfig->begin;
	end = uconfig->end;

	if (!begin || !end || end < begin)
		return -pte_bad_config;

	/* Ignore fields in the user's configuration we don't know; zero out
	 * fields the user didn't know about.
	 */
	if (sizeof(*config) <= size)
		size = sizeof(*config);
	else
		memset(((uint8_t *) config) + size, 0, sizeof(*config) - size);

	/* Copy (portions of) the user's configuration. */
	memcpy(config, uconfig, size);

	/* We copied user's size - fix it. */
	config->size = size;

	return 0;
}

/* The maximum number of filter addresses that fit into the configuration. */
static inline size_t pt_filter_addr_ncfg(void)
{
	return (sizeof(struct pt_conf_addr_filter) -
		offsetof(struct pt_conf_addr_filter, addr0_a)) /
		(2 * sizeof(uint64_t));
}

uint32_t pt_filter_addr_cfg(const struct pt_conf_addr_filter *filter, uint8_t n)
{
	if (!filter)
		return 0u;

	if (pt_filter_addr_ncfg() <= n)
		return 0u;

	return (filter->config.addr_cfg >> (4 * n)) & 0xf;
}

uint64_t pt_filter_addr_a(const struct pt_conf_addr_filter *filter, uint8_t n)
{
	const uint64_t *addr;

	if (!filter)
		return 0ull;

	if (pt_filter_addr_ncfg() <= n)
		return 0ull;

	addr = &filter->addr0_a;
	return addr[2 * n];
}

uint64_t pt_filter_addr_b(const struct pt_conf_addr_filter *filter, uint8_t n)
{
	const uint64_t *addr;

	if (!filter)
		return 0ull;

	if (pt_filter_addr_ncfg() <= n)
		return 0ull;

	addr = &filter->addr0_a;
	return addr[(2 * n) + 1];
}

static int pt_filter_check_cfg_filter(const struct pt_conf_addr_filter *filter,
				      uint64_t addr)
{
	uint8_t n;

	if (!filter)
		return -pte_internal;

	for (n = 0; n < pt_filter_addr_ncfg(); ++n) {
		uint64_t addr_a, addr_b;
		uint32_t addr_cfg;

		addr_cfg = pt_filter_addr_cfg(filter, n);
		if (addr_cfg != pt_addr_cfg_filter)
			continue;

		addr_a = pt_filter_addr_a(filter, n);
		addr_b = pt_filter_addr_b(filter, n);

		/* Note that both A and B are inclusive. */
		if ((addr_a <= addr) && (addr <= addr_b))
			return 1;
	}

	/* No filter hit.  If we have at least one FilterEn filter, this means
	 * that tracing is disabled; otherwise, tracing is enabled.
	 */
	for (n = 0; n < pt_filter_addr_ncfg(); ++n) {
		uint32_t addr_cfg;

		addr_cfg = pt_filter_addr_cfg(filter, n);
		if (addr_cfg == pt_addr_cfg_filter)
			return 0;
	}

	return 1;
}

static int pt_filter_check_cfg_stop(const struct pt_conf_addr_filter *filter,
				    uint64_t addr)
{
	uint8_t n;

	if (!filter)
		return -pte_internal;

	for (n = 0; n < pt_filter_addr_ncfg(); ++n) {
		uint64_t addr_a, addr_b;
		uint32_t addr_cfg;

		addr_cfg = pt_filter_addr_cfg(filter, n);
		if (addr_cfg != pt_addr_cfg_stop)
			continue;

		addr_a = pt_filter_addr_a(filter, n);
		addr_b = pt_filter_addr_b(filter, n);

		/* Note that both A and B are inclusive. */
		if ((addr_a <= addr) && (addr <= addr_b))
			return 0;
	}

	return 1;
}

int pt_filter_addr_check(const struct pt_conf_addr_filter *filter,
			 uint64_t addr)
{
	int status;

	status = pt_filter_check_cfg_stop(filter, addr);
	if (status <= 0)
		return status;

	return pt_filter_check_cfg_filter(filter, addr);
}
