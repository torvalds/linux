/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2024 Linaro Ltd.
 */
#ifndef _IPA_TABLE_H_
#define _IPA_TABLE_H_

#include <linux/types.h>

struct ipa;

/**
 * ipa_filtered_valid() - Validate a filter table endpoint bitmap
 * @ipa:	IPA pointer
 * @filtered:	Filter table endpoint bitmap to check
 *
 * Return:	true if all regions are valid, false otherwise
 */
bool ipa_filtered_valid(struct ipa *ipa, u64 filtered);

/**
 * ipa_table_hash_support() - Return true if hashed tables are supported
 * @ipa:	IPA pointer
 */
bool ipa_table_hash_support(struct ipa *ipa);

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
 *
 * There is no need for a matching ipa_table_teardown() function.
 */
int ipa_table_setup(struct ipa *ipa);

/**
 * ipa_table_config() - Configure filter and route tables
 * @ipa:	IPA pointer
 *
 * There is no need for a matching ipa_table_deconfig() function.
 */
void ipa_table_config(struct ipa *ipa);

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

/**
 * ipa_table_mem_valid() - Validate sizes of table memory regions
 * @ipa:	IPA pointer
 * @filter:	Whether to check filter or routing tables
 */
bool ipa_table_mem_valid(struct ipa *ipa, bool filter);

#endif /* _IPA_TABLE_H_ */
