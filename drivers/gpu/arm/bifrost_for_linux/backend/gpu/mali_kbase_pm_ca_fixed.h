/*
 *
 * (C) COPYRIGHT 2013-2015 ARM Limited. All rights reserved.
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



/*
 * A power policy implementing fixed core availability
 */

#ifndef MALI_KBASE_PM_CA_FIXED_H
#define MALI_KBASE_PM_CA_FIXED_H

/**
 * struct kbasep_pm_ca_policy_fixed - Private structure for policy instance data
 *
 * @dummy: Dummy member - no state is needed
 *
 * This contains data that is private to the particular power policy that is
 * active.
 */
struct kbasep_pm_ca_policy_fixed {
	int dummy;
};

extern const struct kbase_pm_ca_policy kbase_pm_ca_fixed_policy_ops;

#endif /* MALI_KBASE_PM_CA_FIXED_H */

