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

#endif /* _IPA_CLOCK_H_ */
