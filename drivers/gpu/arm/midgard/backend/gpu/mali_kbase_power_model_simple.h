/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifndef _BASE_POWER_MODEL_SIMPLE_H_
#define _BASE_POWER_MODEL_SIMPLE_H_

/**
 * kbase_power_model_simple_init - Initialise the simple power model
 * @kbdev: Device pointer
 *
 * The simple power model estimates power based on current voltage, temperature,
 * and coefficients read from device tree. It does not take utilization into
 * account.
 *
 * The power model requires coefficients from the power_model node in device
 * tree. The absence of this node will prevent the model from functioning, but
 * should not prevent the rest of the driver from running.
 *
 * Return: 0 on success
 *         -ENOSYS if the power_model node is not present in device tree
 *         -EPROBE_DEFER if the thermal zone specified in device tree is not
 *         currently available
 *         Any other negative value on failure
 */
int kbase_power_model_simple_init(struct kbase_device *kbdev);

extern struct devfreq_cooling_power power_model_simple_ops;

#endif /* _BASE_POWER_MODEL_SIMPLE_H_ */
