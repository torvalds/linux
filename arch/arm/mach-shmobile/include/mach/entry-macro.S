/*
 * Copyright (C) 2010  Paul Mundt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

	.macro  disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	.endm

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
	.endm

	.macro  test_for_ipi, irqnr, irqstat, base, tmp
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm
