/*
 *  Cobalt time initialization.
 *
 *  Copyright (C) 2007  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <linux/init.h>

#include <asm/gt64120.h>
#include <asm/i8253.h>
#include <asm/time.h>

#define GT641XX_BASE_CLOCK	50000000	/* 50MHz */

void __init plat_time_init(void)
{
	setup_pit_timer();

	gt641xx_set_base_clock(GT641XX_BASE_CLOCK);

	mips_timer_state = gt641xx_timer0_state;
}
