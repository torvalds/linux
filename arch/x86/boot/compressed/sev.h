/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD SEV header for early boot related functions.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#ifndef BOOT_COMPRESSED_SEV_H
#define BOOT_COMPRESSED_SEV_H

#ifdef CONFIG_AMD_MEM_ENCRYPT

bool sev_snp_enabled(void);
void snp_accept_memory(phys_addr_t start, phys_addr_t end);
u64 sev_get_status(void);
bool early_is_sevsnp_guest(void);

#else

static inline bool sev_snp_enabled(void) { return false; }
static inline void snp_accept_memory(phys_addr_t start, phys_addr_t end) { }
static inline u64 sev_get_status(void) { return 0; }
static inline bool early_is_sevsnp_guest(void) { return false; }

#endif

#endif
