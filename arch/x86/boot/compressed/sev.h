/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD SEV header for early boot related functions.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#ifndef BOOT_COMPRESSED_SEV_H
#define BOOT_COMPRESSED_SEV_H

#ifdef CONFIG_AMD_MEM_ENCRYPT

#include "../msr.h"

void snp_accept_memory(phys_addr_t start, phys_addr_t end);
u64 sev_get_status(void);
bool early_is_sevsnp_guest(void);

static inline u64 sev_es_rd_ghcb_msr(void)
{
	struct msr m;

	boot_rdmsr(MSR_AMD64_SEV_ES_GHCB, &m);

	return m.q;
}

static inline void sev_es_wr_ghcb_msr(u64 val)
{
	struct msr m;

	m.q = val;
	boot_wrmsr(MSR_AMD64_SEV_ES_GHCB, &m);
}

#else

static inline void snp_accept_memory(phys_addr_t start, phys_addr_t end) { }
static inline u64 sev_get_status(void) { return 0; }
static inline bool early_is_sevsnp_guest(void) { return false; }

#endif

#endif
