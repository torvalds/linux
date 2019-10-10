/*
 *
 * (C) COPYRIGHT 2011-2015,2018 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */



/*
 * "Always on" power management policy
 */

#ifndef MALI_KBASE_PM_ALWAYS_ON_H
#define MALI_KBASE_PM_ALWAYS_ON_H

/**
 * DOC:
 * The "Always on" power management policy has the following
 * characteristics:
 *
 * - When KBase indicates that the GPU will be powered up, but we don't yet
 *   know which Job Chains are to be run:
 *    Shader Cores are powered up, regardless of whether or not they will be
 *    needed later.
 *
 * - When KBase indicates that Shader Cores are needed to submit the currently
 *   queued Job Chains:
 *    Shader Cores are kept powered, regardless of whether or not they will be
 *    needed
 *
 * - When KBase indicates that the GPU need not be powered:
 *    The Shader Cores are kept powered, regardless of whether or not they will
 *    be needed. The GPU itself is also kept powered, even though it is not
 *    needed.
 *
 * This policy is automatically overridden during system suspend: the desired
 * core state is ignored, and the cores are forced off regardless of what the
 * policy requests. After resuming from suspend, new changes to the desired
 * core state made by the policy are honored.
 *
 * Note:
 *
 * - KBase indicates the GPU will be powered up when it has a User Process that
 *   has just started to submit Job Chains.
 *
 * - KBase indicates the GPU need not be powered when all the Job Chains from
 *   User Processes have finished, and it is waiting for a User Process to
 *   submit some more Job Chains.
 */

/**
 * struct kbasep_pm_policy_always_on - Private struct for policy instance data
 * @dummy: unused dummy variable
 *
 * This contains data that is private to the particular power policy that is
 * active.
 */
struct kbasep_pm_policy_always_on {
	int dummy;
};

extern const struct kbase_pm_policy kbase_pm_always_on_policy_ops;

#endif /* MALI_KBASE_PM_ALWAYS_ON_H */

