/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */
#ifndef _IPA_CLOCK_H_
#define _IPA_CLOCK_H_

struct device;

struct ipa;
struct ipa_clock_data;

/* IPA device power management function block */
extern const struct dev_pm_ops ipa_pm_ops;

/**
 * ipa_clock_rate() - Return the current IPA core clock rate
 * @ipa:	IPA structure
 *
 * Return: The current clock rate (in Hz), or 0.
 */
u32 ipa_clock_rate(struct ipa *ipa);

/**
 * ipa_power_modem_queue_stop() - Possibly stop the modem netdev TX queue
 * @ipa:	IPA pointer
 */
void ipa_power_modem_queue_stop(struct ipa *ipa);

/**
 * ipa_power_modem_queue_wake() - Possibly wake the modem netdev TX queue
 * @ipa:	IPA pointer
 */
void ipa_power_modem_queue_wake(struct ipa *ipa);

/**
 * ipa_power_modem_queue_active() - Report modem netdev TX queue active
 * @ipa:	IPA pointer
 */
void ipa_power_modem_queue_active(struct ipa *ipa);

/**
 * ipa_power_setup() - Set up IPA power management
 * @ipa:	IPA pointer
 *
 * Return:	0 if successful, or a negative error code
 */
int ipa_power_setup(struct ipa *ipa);

/**
 * ipa_power_teardown() - Inverse of ipa_power_setup()
 * @ipa:	IPA pointer
 */
void ipa_power_teardown(struct ipa *ipa);

/**
 * ipa_clock_init() - Initialize IPA clocking
 * @dev:	IPA device
 * @data:	Clock configuration data
 *
 * Return:	A pointer to an ipa_clock structure, or a pointer-coded error
 */
struct ipa_clock *ipa_clock_init(struct device *dev,
				 const struct ipa_clock_data *data);

/**
 * ipa_clock_exit() - Inverse of ipa_clock_init()
 * @clock:	IPA clock pointer
 */
void ipa_clock_exit(struct ipa_clock *clock);

/**
 * ipa_clock_get() - Get an IPA clock reference
 * @ipa:	IPA pointer
 *
 * Return:	0 if clock started, 1 if clock already running, or a negative
 *		error code
 *
 * This call blocks if this is the first reference.  A reference is
 * taken even if an error occurs starting the IPA clock.
 */
int ipa_clock_get(struct ipa *ipa);

/**
 * ipa_clock_put() - Drop an IPA clock reference
 * @ipa:	IPA pointer
 *
 * Return:	0 if successful, or a negative error code
 *
 * This drops a clock reference.  If the last reference is being dropped,
 * the clock is stopped and RX endpoints are suspended.  This call will
 * not block unless the last reference is dropped.
 */
int ipa_clock_put(struct ipa *ipa);

#endif /* _IPA_CLOCK_H_ */
