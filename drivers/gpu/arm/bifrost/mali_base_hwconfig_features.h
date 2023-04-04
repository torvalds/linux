/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2022 ARM Limited. All rights reserved.
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

/* AUTOMATICALLY GENERATED FILE. If you want to amend the issues/features,
 * please update base/tools/hwconfig_generator/hwc_{issues,features}.py
 * For more information see base/tools/hwconfig_generator/README
 */

#ifndef _BASE_HWCONFIG_FEATURES_H_
#define _BASE_HWCONFIG_FEATURES_H_

enum base_hw_feature {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_TLS_HASHING,
	BASE_HW_FEATURE_THREAD_GROUP_SPLIT,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_IDVS_GROUP_SIZE,
	BASE_HW_FEATURE_L2_CONFIG,
	BASE_HW_FEATURE_ASN_HASH,
	BASE_HW_FEATURE_GPU_SLEEP,
	BASE_HW_FEATURE_FLUSH_INV_SHADER_OTHER,
	BASE_HW_FEATURE_CORE_FEATURES,
	BASE_HW_FEATURE_PBHA_HWU,
	BASE_HW_FEATURE_LARGE_PAGE_ALLOC,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_generic[] = {
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tMIx[] = {
	BASE_HW_FEATURE_THREAD_GROUP_SPLIT,
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tHEx[] = {
	BASE_HW_FEATURE_THREAD_GROUP_SPLIT,
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tSIx[] = {
	BASE_HW_FEATURE_THREAD_GROUP_SPLIT,
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tDVx[] = {
	BASE_HW_FEATURE_THREAD_GROUP_SPLIT,
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tNOx[] = {
	BASE_HW_FEATURE_THREAD_GROUP_SPLIT,
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_TLS_HASHING,
	BASE_HW_FEATURE_IDVS_GROUP_SIZE,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tGOx[] = {
	BASE_HW_FEATURE_THREAD_GROUP_SPLIT,
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_TLS_HASHING,
	BASE_HW_FEATURE_IDVS_GROUP_SIZE,
	BASE_HW_FEATURE_CORE_FEATURES,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tTRx[] = {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_IDVS_GROUP_SIZE,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_FLUSH_INV_SHADER_OTHER,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tNAx[] = {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_IDVS_GROUP_SIZE,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_FLUSH_INV_SHADER_OTHER,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tBEx[] = {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_IDVS_GROUP_SIZE,
	BASE_HW_FEATURE_L2_CONFIG,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_FLUSH_INV_SHADER_OTHER,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tBAx[] = {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_IDVS_GROUP_SIZE,
	BASE_HW_FEATURE_L2_CONFIG,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_FLUSH_INV_SHADER_OTHER,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tODx[] = {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_L2_CONFIG,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tGRx[] = {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_L2_CONFIG,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_CORE_FEATURES,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tVAx[] = {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_L2_CONFIG,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_CORE_FEATURES,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tTUx[] = {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_L2_CONFIG,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_ASN_HASH,
	BASE_HW_FEATURE_GPU_SLEEP,
	BASE_HW_FEATURE_CORE_FEATURES,
	BASE_HW_FEATURE_END
};

__attribute__((unused)) static const enum base_hw_feature base_hw_features_tTIx[] = {
	BASE_HW_FEATURE_FLUSH_REDUCTION,
	BASE_HW_FEATURE_PROTECTED_DEBUG_MODE,
	BASE_HW_FEATURE_L2_CONFIG,
	BASE_HW_FEATURE_CLEAN_ONLY_SAFE,
	BASE_HW_FEATURE_ASN_HASH,
	BASE_HW_FEATURE_GPU_SLEEP,
	BASE_HW_FEATURE_CORE_FEATURES,
	BASE_HW_FEATURE_PBHA_HWU,
	BASE_HW_FEATURE_END
};


#endif /* _BASE_HWCONFIG_FEATURES_H_ */
