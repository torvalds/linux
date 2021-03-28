/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _IPA_TABLE_H_
#define _IPA_TABLE_H_

#include <linux/types.h>

struct ipa;

/* The maximum number of filter table entries (IPv4, IPv6; hashed or not) */
#define IPA_FILTER_COUNT_MAX	14

/* The maximum number of route table entries (IPv4, IPv6; hashed or not) */
#define IPA_ROUTE_COUNT_MAX	15

#ifdef IPA_VALIDATE

/**
 * ipa_table_valid() - Validate route and filter table memory regions
 * @ipa:	IPA pointer

 * Return:	true if all regions are valid, false otherwise
 */
bool ipa_table_valid(struct ipa *ipa);

/**
 * ipa_filter_map_valid() - Validate a filter table endpoint bitmap
 * @ipa:	IPA pointer
 *
 * Return:	true if all regions are valid, false otherwise
 */
bool ipa_filter_map_valid(struct ipa *ipa, u32 filter_mask);

#else /* !IPA_VALIDATE */

static inline bool ipa_table_valid(struct ipa *ipa)
{
	return true;
}

static inline bool ipa_filter_map_valid(struct ipa *ipa, u32 filter_mask)
{
	return true;
}

#endif /* !IPA_VALIDATE */

/**
 * ipa_table_reset() - Reset filter and route tables entries to "none"
 * @ipa:	IPA pointer
 * @modem:	Whether to reset modem or AP entries
 */
void ipa_table_reset(struct ipa *ipa, bool modem);

/**
 * ipa_table_hash_flush() - Synchronize hashed filter and route updates
 * @ipa:	IPA pointer
 */
int ipa_table_hash_flush(struct ipa *ipa);

/**
 * ipa_table_setup() - Set up filter and route tables
 * @ipa:	IPA pointer
 */
int ipa_table_setup(struct ipa *ipa);

/**
 * ipa_table_teardown() - Inverse of ipa_table_setup()
 * @ipa:	IPA pointer
 */
void ipa_table_teardown(struct ipa *ipa);

/**
 * ipa_table_config() - Configure filter and route tables
 * @ipa:	IPA pointer
 */
void ipa_table_config(struct ipa *ipa);

/**
 * ipa_table_deconfig() - Inverse of ipa_table_config()
 * @ipa:	IPA pointer
 */
void ipa_table_deconfig(struct ipa *ipa);

/**
 * ipa_table_init() - Do early initialization of filter and route tables
 * @ipa:	IPA pointer
 */
int ipa_table_init(struct ipa *ipa);

/**
 * ipa_table_exit() - Inverse of ipa_table_init()
 * @ipa:	IPA pointer
 */
void ipa_table_exit(struct ipa *ipa);

#endif /* _IPA_TABLE_H_ */
