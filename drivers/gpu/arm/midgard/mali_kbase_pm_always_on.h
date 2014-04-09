
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
 * @file mali_kbase_pm_always_on.h
 * "Always on" power management policy
 */

#ifndef MALI_KBASE_PM_ALWAYS_ON_H
#define MALI_KBASE_PM_ALWAYS_ON_H

/**
 * The "Always on" power management policy has the following
 * characteristics:
 * - When KBase indicates that the GPU will be powered up, but we don't yet
 *   know which Job Chains are to be run:
 *  - All Shader Cores are powered up, regardless of whether or not they will
 *    be needed later.
 * - When KBase indicates that a set of Shader Cores are needed to submit the
 *   currently queued Job Chains:
 *  - All Shader Cores are kept powered, regardless of whether or not they will
 *    be needed
 * - When KBase indicates that the GPU need not be powered:
 *  - The Shader Cores are kept powered, regardless of whether or not they will
 *    be needed. The GPU itself is also kept powered, even though it is not
 *    needed.
 *
 * This policy is automatically overridden during system suspend: the desired
 * core state is ignored, and the cores are forced off regardless of what the
 * policy requests. After resuming from suspend, new changes to the desired
 * core state made by the policy are honored.
 *
 * @note:
 * - KBase indicates the GPU will be powered up when it has a User Process that
 *   has just started to submit Job Chains.
 * - KBase indicates the GPU need not be powered when all the Job Chains from
 *   User Processes have finished, and it is waiting for a User Process to
 *   submit some more Job Chains.
 */

/**
 * Private structure for policy instance data.
 *
 * This contains data that is private to the particular power policy that is active.
 */
typedef struct kbasep_pm_policy_always_on {
	/** No state needed - just have a dummy variable here */
	int dummy;
} kbasep_pm_policy_always_on;

#endif 				/* MALI_KBASE_PM_ALWAYS_ON_H */
