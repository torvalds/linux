/*
 * Copyright (C) 2006 PA Semi, Inc
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/time.h>

#include <asm/time.h>

time64_t __init pas_get_boot_time(void)
{
	/* Let's just return a fake date right now */
	return mktime64(2006, 1, 1, 12, 0, 0);
}
