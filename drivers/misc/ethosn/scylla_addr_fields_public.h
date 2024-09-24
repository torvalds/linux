/*
 *
 * (C) COPYRIGHT 2018-2019 Arm Limited. All rights reserved.
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
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#pragma once

#define REGION_REGISTERS (0x4 >> 1)
#define REGION_EXT_RAM0  (0x6 >> 1)
#define REGION_EXT_RAM1  (0x8 >> 1)

#define REGION_SHIFT 29
#define REGION_MASK 0x3

#define BROADCAST_SHIFT 28
#define BROADCAST_MASK 0x1

#define CE_SHIFT 20
#define CE_MASK 0xFF

#define REGPAGE_SHIFT 16
#define REGPAGE_MASK 0xF

#define REGOFFSET_SHIFT 0
#define REGOFFSET_MASK 0xFFFF

/* Register page offsets */
#define DL1_RP            0x1

/* Compose a Scylla register address from the bit components */
#define SCYLLA_REG(broadcast, ce, page, offset)             \
	(((REGION_REGISTERS & REGION_MASK) << REGION_SHIFT) |  \
	(((broadcast) & BROADCAST_MASK) << BROADCAST_SHIFT) |  \
	(((ce) & CE_MASK) << CE_SHIFT)                      |  \
	(((page) & REGPAGE_MASK) << REGPAGE_SHIFT)          |  \
	(((offset) & REGOFFSET_MASK) << REGOFFSET_SHIFT))

#define TOP_REG(page, offset) SCYLLA_REG(1, 0, (page), (offset))
