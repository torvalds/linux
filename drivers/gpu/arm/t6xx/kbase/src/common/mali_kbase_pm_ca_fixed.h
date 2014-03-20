/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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



/**
 * @file mali_kbase_pm_ca_fixed.h
 * A power policy implementing fixed core availability
 */

#ifndef MALI_KBASE_PM_CA_FIXED_H
#define MALI_KBASE_PM_CA_FIXED_H

/**
 * Private structure for policy instance data.
 *
 * This contains data that is private to the particular power policy that is active.
 */
typedef struct kbasep_pm_ca_policy_fixed {
	/** No state needed - just have a dummy variable here */
	int dummy;
} kbasep_pm_ca_policy_fixed;

#endif /* MALI_KBASE_PM_CA_FIXED_H */

