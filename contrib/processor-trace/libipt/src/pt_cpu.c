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

#include "pt_cpu.h"
#include "pt_cpuid.h"

#include "intel-pt.h"

#include <limits.h>
#include <stdlib.h>


static const char * const cpu_vendors[] = {
	"",
	"GenuineIntel"
};

enum {
	pt_cpuid_vendor_size = 12
};

union cpu_vendor {
	/* The raw data returned from cpuid. */
	struct {
		uint32_t ebx;
		uint32_t edx;
		uint32_t ecx;
	} cpuid;

	/* The resulting vendor string. */
	char vendor_string[pt_cpuid_vendor_size];
};

static enum pt_cpu_vendor cpu_vendor(void)
{
	union cpu_vendor vendor;
	uint32_t eax;
	size_t i;

	memset(&vendor, 0, sizeof(vendor));
	eax = 0;

	pt_cpuid(0u, &eax, &vendor.cpuid.ebx, &vendor.cpuid.ecx,
		 &vendor.cpuid.edx);

	for (i = 0; i < sizeof(cpu_vendors)/sizeof(*cpu_vendors); i++)
		if (strncmp(vendor.vendor_string,
			    cpu_vendors[i], pt_cpuid_vendor_size) == 0)
			return (enum pt_cpu_vendor) i;

	return pcv_unknown;
}

static uint32_t cpu_info(void)
{
	uint32_t eax, ebx, ecx, edx;

	eax = 0;
	ebx = 0;
	ecx = 0;
	edx = 0;
	pt_cpuid(1u, &eax, &ebx, &ecx, &edx);

	return eax;
}

int pt_cpu_parse(struct pt_cpu *cpu, const char *s)
{
	const char sep = '/';
	char *endptr;
	long family, model, stepping;

	if (!cpu || !s)
		return -pte_invalid;

	family = strtol(s, &endptr, 0);
	if (s == endptr || *endptr == '\0' || *endptr != sep)
		return -pte_invalid;

	if (family < 0 || family > USHRT_MAX)
		return -pte_invalid;

	/* skip separator */
	s = endptr + 1;

	model = strtol(s, &endptr, 0);
	if (s == endptr || (*endptr != '\0' && *endptr != sep))
		return -pte_invalid;

	if (model < 0 || model > UCHAR_MAX)
		return -pte_invalid;

	if (*endptr == '\0')
		/* stepping was omitted, it defaults to 0 */
		stepping = 0;
	else {
		/* skip separator */
		s = endptr + 1;

		stepping = strtol(s, &endptr, 0);
		if (*endptr != '\0')
			return -pte_invalid;

		if (stepping < 0 || stepping > UCHAR_MAX)
			return -pte_invalid;
	}

	cpu->vendor = pcv_intel;
	cpu->family = (uint16_t) family;
	cpu->model = (uint8_t) model;
	cpu->stepping = (uint8_t) stepping;

	return 0;
}

int pt_cpu_read(struct pt_cpu *cpu)
{
	uint32_t info;
	uint16_t family;

	if (!cpu)
		return -pte_invalid;

	cpu->vendor = cpu_vendor();

	info = cpu_info();

	cpu->family = family = (info>>8) & 0xf;
	if (family == 0xf)
		cpu->family += (info>>20) & 0xf;

	cpu->model = (info>>4) & 0xf;
	if (family == 0x6 || family == 0xf)
		cpu->model += (info>>12) & 0xf0;

	cpu->stepping = (info>>0) & 0xf;

	return 0;
}
