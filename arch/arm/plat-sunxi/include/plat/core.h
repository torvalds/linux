/*
 * Copyright (C) 2012  Alejandro Mery <amery@geeks.cl>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _SUNXI_CORE_H
#define _SUNXI_CORE_H

#define pr_reserve_info(L, START, SIZE) \
	pr_info("\t%s : 0x%08x - 0x%08x  (%4d %s)\n", L, \
		(u32)(START), (u32)((START) + (SIZE) - 1), \
		(u32)((SIZE) < SZ_1M ? (SIZE) / SZ_1K : (SIZE) / SZ_1M), \
		(SIZE) < SZ_1M ? "kB" : "MB")

#endif
