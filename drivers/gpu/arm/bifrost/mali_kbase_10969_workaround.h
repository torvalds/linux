/*
 *
 * (C) COPYRIGHT 2013-2014, 2018 ARM Limited. All rights reserved.
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

#ifndef _KBASE_10969_WORKAROUND_
#define _KBASE_10969_WORKAROUND_

/**
 * kbasep_10969_workaround_clamp_coordinates - Apply the WA to clamp the restart indices
 * @katom: atom representing the fragment job for which the WA has to be applied
 *
 * This workaround is used to solve an HW issue with single iterator GPUs.
 * If a fragment job is soft-stopped on the edge of its bounding box, it can happen
 * that the restart index is out of bounds and the rerun causes a tile range
 * fault. If this happens we try to clamp the restart index to a correct value.
 */
int kbasep_10969_workaround_clamp_coordinates(struct kbase_jd_atom *katom);

#endif /* _KBASE_10969_WORKAROUND_ */
