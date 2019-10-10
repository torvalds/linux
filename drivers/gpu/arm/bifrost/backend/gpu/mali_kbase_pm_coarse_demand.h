/*
 *
 * (C) COPYRIGHT 2012-2015,2018 ARM Limited. All rights reserved.
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
 * "Coarse Demand" power management policy
 */

#ifndef MALI_KBASE_PM_COARSE_DEMAND_H
#define MALI_KBASE_PM_COARSE_DEMAND_H

/**
 * DOC:
 * The "Coarse" demand power management policy has the following
 * characteristics:
 * - When KBase indicates that the GPU will be powered up, but we don't yet
 *   know which Job Chains are to be run:
 *  - Shader Cores are powered up, regardless of whether or not they will be
 *    needed later.
 * - When KBase indicates that Shader Cores are needed to submit the currently
 *   queued Job Chains:
 *  - Shader Cores are kept powered, regardless of whether or not they will
 *    be needed
 * - When KBase indicates that the GPU need not be powered:
 *  - The Shader Cores are powered off, and the GPU itself is powered off too.
 *
 * @note:
 * - KBase indicates the GPU will be powered up when it has a User Process that
 *   has just started to submit Job Chains.
 * - KBase indicates the GPU need not be powered when all the Job Chains from
 *   User Processes have finished, and it is waiting for a User Process to
 *   submit some more Job Chains.
 */

/**
 * struct kbasep_pm_policy_coarse_demand - Private structure for coarse demand
 *                                         policy
 *
 * This contains data that is private to the coarse demand power policy.
 *
 * @dummy: Dummy member - no state needed
 */
struct kbasep_pm_policy_coarse_demand {
	int dummy;
};

extern const struct kbase_pm_policy kbase_pm_coarse_demand_policy_ops;

#endif /* MALI_KBASE_PM_COARSE_DEMAND_H */
