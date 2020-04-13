/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _IPA_MEM_H_
#define _IPA_MEM_H_

struct ipa;

/**
 * DOC: IPA Local Memory
 *
 * The IPA has a block of shared memory, divided into regions used for
 * specific purposes.
 *
 * The regions within the shared block are bounded by an offset (relative to
 * the "ipa-shared" memory range) and size found in the IPA_SHARED_MEM_SIZE
 * register.
 *
 * Each region is optionally preceded by one or more 32-bit "canary" values.
 * These are meant to detect out-of-range writes (if they become corrupted).
 * A given region (such as a filter or routing table) has the same number
 * of canaries for all IPA hardware versions.  Still, the number used is
 * defined in the config data, allowing for generic handling of regions.
 *
 * The set of memory regions is defined in configuration data.  They are
 * subject to these constraints:
 * - a zero offset and zero size represents and undefined region
 * - a region's offset is defined to be *past* all "canary" values
 * - offset must be large enough to account for all canaries
 * - a region's size may be zero, but may still have canaries
 * - all offsets must be 8-byte aligned
 * - most sizes must be a multiple of 8
 * - modem memory size must be a multiple of 4
 * - the microcontroller ring offset must be a multiple of 1024
 */

/* The maximum allowed size for any memory region */
#define IPA_MEM_MAX	(2 * PAGE_SIZE)

/* IPA-resident memory region ids */
enum ipa_mem_id {
	IPA_MEM_UC_SHARED,		/* 0 canaries */
	IPA_MEM_UC_INFO,		/* 0 canaries */
	IPA_MEM_V4_FILTER_HASHED,	/* 2 canaries */
	IPA_MEM_V4_FILTER,		/* 2 canaries */
	IPA_MEM_V6_FILTER_HASHED,	/* 2 canaries */
	IPA_MEM_V6_FILTER,		/* 2 canaries */
	IPA_MEM_V4_ROUTE_HASHED,	/* 2 canaries */
	IPA_MEM_V4_ROUTE,		/* 2 canaries */
	IPA_MEM_V6_ROUTE_HASHED,	/* 2 canaries */
	IPA_MEM_V6_ROUTE,		/* 2 canaries */
	IPA_MEM_MODEM_HEADER,		/* 2 canaries */
	IPA_MEM_AP_HEADER,		/* 0 canaries */
	IPA_MEM_MODEM_PROC_CTX,		/* 2 canaries */
	IPA_MEM_AP_PROC_CTX,		/* 0 canaries */
	IPA_MEM_PDN_CONFIG,		/* 2 canaries (IPA v4.0 and above) */
	IPA_MEM_STATS_QUOTA,		/* 2 canaries (IPA v4.0 and above) */
	IPA_MEM_STATS_TETHERING,	/* 0 canaries (IPA v4.0 and above) */
	IPA_MEM_STATS_DROP,		/* 0 canaries (IPA v4.0 and above) */
	IPA_MEM_MODEM,			/* 0 canaries */
	IPA_MEM_UC_EVENT_RING,		/* 1 canary */
	IPA_MEM_COUNT,			/* Number of regions (not an index) */
};

/**
 * struct ipa_mem - IPA local memory region description
 * @offset:		offset in IPA memory space to base of the region
 * @size:		size in bytes base of the region
 * @canary_count	# 32-bit "canary" values that precede region
 */
struct ipa_mem {
	u32 offset;
	u16 size;
	u16 canary_count;
};

int ipa_mem_config(struct ipa *ipa);
void ipa_mem_deconfig(struct ipa *ipa);

int ipa_mem_setup(struct ipa *ipa);
void ipa_mem_teardown(struct ipa *ipa);

int ipa_mem_zero_modem(struct ipa *ipa);

int ipa_mem_init(struct ipa *ipa, u32 count, const struct ipa_mem *mem);
void ipa_mem_exit(struct ipa *ipa);

#endif /* _IPA_MEM_H_ */
