/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014, 2018, 2020-2021 ARM Limited. All rights reserved.
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

/*
 * Backend-specific HW access instrumentation APIs
 */

#ifndef _KBASE_INSTR_INTERNAL_H_
#define _KBASE_INSTR_INTERNAL_H_

/**
 * kbasep_cache_clean_worker() - Workqueue for handling cache cleaning
 * @data: a &struct work_struct
 */
void kbasep_cache_clean_worker(struct work_struct *data);

/**
 * kbase_instr_hwcnt_sample_done() - Dump complete interrupt received
 * @kbdev: Kbase device
 */
void kbase_instr_hwcnt_sample_done(struct kbase_device *kbdev);

#endif /* _KBASE_INSTR_INTERNAL_H_ */
