/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */
#ifndef _IPA_CLOCK_H_
#define _IPA_CLOCK_H_

struct device;

struct ipa;

/**
 * ipa_clock_rate() - Return the current IPA core clock rate
 * @ipa:	IPA structure
 *
 * Return: The current clock rate (in Hz), or 0.
 */
u32 ipa_clock_rate(struct ipa *ipa);

/**
 * ipa_clock_init() - Initialize IPA clocking
 * @dev:	IPA device
 *
 * @Return:	A pointer to an ipa_clock structure, or a pointer-coded error
 */
struct ipa_clock *ipa_clock_init(struct device *dev);

/**
 * ipa_clock_exit() - Inverse of ipa_clock_init()
 * @clock:	IPA clock pointer
 */
void ipa_clock_exit(struct ipa_clock *clock);

/**
 * ipa_clock_get() - Get an IPA clock reference
 * @ipa:	IPA pointer
 *
 * This call blocks if this is the first reference.
 */
void ipa_clock_get(struct ipa *ipa);

/**
 * ipa_clock_get_additional() - Get an IPA clock reference if not first
 * @ipa:	IPA pointer
 *
 * This returns immediately, and only takes a reference if not the first
 */
bool ipa_clock_get_additional(struct ipa *ipa);

/**
 * ipa_clock_put() - Drop an IPA clock reference
 * @ipa:	IPA pointer
 *
 * This drops a clock reference.  If the last reference is being dropped,
 * the clock is stopped and RX endpoints are suspended.  This call will
 * not block unless the last reference is dropped.
 */
void ipa_clock_put(struct ipa *ipa);

#endif /* _IPA_CLOCK_H_ */
