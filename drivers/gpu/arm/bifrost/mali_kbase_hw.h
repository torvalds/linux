/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2012-2017, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/**
 * DOC: Run-time work-arounds helpers
 */

#ifndef _KBASE_HW_H_
#define _KBASE_HW_H_

#include "mali_kbase_defs.h"

/**
 * Tell whether a work-around should be enabled
 * @kbdev: Device pointer
 * @issue: issue to be checked
 */
#define kbase_hw_has_issue(kbdev, issue)\
	test_bit(issue, &(kbdev)->hw_issues_mask[0])

/**
 * Tell whether a feature is supported
 * @kbdev: Device pointer
 * @feature: feature to be checked
 */
#define kbase_hw_has_feature(kbdev, feature)\
	test_bit(feature, &(kbdev)->hw_features_mask[0])

/**
 * kbase_hw_set_issues_mask - Set the hardware issues mask based on the GPU ID
 * @kbdev: Device pointer
 *
 * Return: 0 if the GPU ID was recognized, otherwise -EINVAL.
 *
 * The GPU ID is read from the @kbdev.
 *
 * In debugging versions of the driver, unknown versions of a known GPU with a
 * new-format ID will be treated as the most recent known version not later
 * than the actual version. In such circumstances, the GPU ID in @kbdev will
 * also be replaced with the most recent known version.
 *
 * Note: The GPU configuration must have been read by
 * kbase_gpuprops_get_props() before calling this function.
 */
int kbase_hw_set_issues_mask(struct kbase_device *kbdev);

/**
 * Set the features mask depending on the GPU ID
 * @kbdev: Device pointer
 */
void kbase_hw_set_features_mask(struct kbase_device *kbdev);

#endif				/* _KBASE_HW_H_ */
