/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2010-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_GPU_REGMAP_H_
#define _KBASE_GPU_REGMAP_H_

#include <uapi/gpu/arm/bifrost/gpu/mali_kbase_gpu_regmap.h>

/* Include POWER_CHANGED_SINGLE in debug builds for use in irq latency test. */
#ifdef CONFIG_MALI_BIFROST_DEBUG
#undef GPU_IRQ_REG_ALL
#define GPU_IRQ_REG_ALL (GPU_IRQ_REG_COMMON | POWER_CHANGED_SINGLE)
#endif /* CONFIG_MALI_BIFROST_DEBUG */

#endif /* _KBASE_GPU_REGMAP_H_ */
