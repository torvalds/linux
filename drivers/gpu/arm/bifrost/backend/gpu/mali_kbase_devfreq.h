/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014, 2019-2021 ARM Limited. All rights reserved.
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

#ifndef _BASE_DEVFREQ_H_
#define _BASE_DEVFREQ_H_

int kbase_devfreq_init(struct kbase_device *kbdev);

void kbase_devfreq_term(struct kbase_device *kbdev);
int kbase_devfreq_opp_helper(struct dev_pm_set_opp_data *data);

/**
 * kbase_devfreq_force_freq - Set GPU frequency on L2 power on/off.
 * @kbdev:      Device pointer
 * @freq:       GPU frequency in HZ to be set when
 *              MALI_HW_ERRATA_1485982_USE_CLOCK_ALTERNATIVE is enabled
 */
void kbase_devfreq_force_freq(struct kbase_device *kbdev, unsigned long freq);

/**
 * kbase_devfreq_enqueue_work - Enqueue a work item for suspend/resume devfreq.
 * @kbdev:      Device pointer
 * @work_type:  The type of the devfreq work item, i.e. suspend or resume
 */
void kbase_devfreq_enqueue_work(struct kbase_device *kbdev,
				enum kbase_devfreq_work_type work_type);

/**
 * kbase_devfreq_opp_translate - Translate nominal OPP frequency from devicetree
 *                               into real frequency & voltage pair, along with
 *                               core mask
 * @kbdev:     Device pointer
 * @freq:      Nominal frequency
 * @core_mask: Pointer to u64 to store core mask to
 * @freqs:     Pointer to array of frequencies
 * @volts:     Pointer to array of voltages
 *
 * This function will only perform translation if an operating-points-v2-mali
 * table is present in devicetree. If one is not present then it will return an
 * untranslated frequency (and corresponding voltage) and all cores enabled.
 * The voltages returned are in micro Volts (uV).
 */
void kbase_devfreq_opp_translate(struct kbase_device *kbdev, unsigned long freq,
	u64 *core_mask, unsigned long *freqs, unsigned long *volts);
#endif /* _BASE_DEVFREQ_H_ */
