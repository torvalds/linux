/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2022 Linaro Ltd.
 */
#ifndef _IPA_POWER_H_
#define _IPA_POWER_H_

struct device;

struct ipa;
struct ipa_power_data;
enum ipa_irq_id;

/* IPA device power management function block */
extern const struct dev_pm_ops ipa_pm_ops;

/**
 * ipa_core_clock_rate() - Return the current IPA core clock rate
 * @ipa:	IPA structure
 *
 * Return: The current clock rate (in Hz), or 0.
 */
u32 ipa_core_clock_rate(struct ipa *ipa);

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
 * ipa_power_retention() - Control register retention on power collapse
 * @ipa:	IPA pointer
 * @enable:	Whether retention should be enabled or disabled
 */
void ipa_power_retention(struct ipa *ipa, bool enable);

/**
 * ipa_power_suspend_handler() - Handler for SUSPEND IPA interrupts
 * @ipa:	IPA pointer
 * @irq_id:	IPA interrupt ID (unused)
 *
 * If an RX endpoint is suspended, and the IPA has a packet destined for
 * that endpoint, the IPA generates a SUSPEND interrupt to inform the AP
 * that it should resume the endpoint.
 */
void ipa_power_suspend_handler(struct ipa *ipa, enum ipa_irq_id irq_id);

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
 * ipa_power_init() - Initialize IPA power management
 * @dev:	IPA device
 * @data:	Clock configuration data
 *
 * Return:	A pointer to an ipa_power structure, or a pointer-coded error
 */
struct ipa_power *ipa_power_init(struct device *dev,
				 const struct ipa_power_data *data);

/**
 * ipa_power_exit() - Inverse of ipa_power_init()
 * @power:	IPA power pointer
 */
void ipa_power_exit(struct ipa_power *power);

#endif /* _IPA_POWER_H_ */
