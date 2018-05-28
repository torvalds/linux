/*
 *
 * (C) COPYRIGHT 2014 ARM Limited. All rights reserved.
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

#ifndef _BASE_DEVFREQ_H_
#define _BASE_DEVFREQ_H_

int kbase_devfreq_init(struct kbase_device *kbdev);
void kbase_devfreq_term(struct kbase_device *kbdev);
int kbase_platform_rk_init_opp_table(struct kbase_device *kbdev);

#endif /* _BASE_DEVFREQ_H_ */
