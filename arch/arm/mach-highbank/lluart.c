/*
 * Copyright 2011 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/init.h>
#include <asm/page.h>
#include <asm/sizes.h>
#include <asm/mach/map.h>

#define HB_DEBUG_LL_PHYS_BASE	0xfff36000
#define HB_DEBUG_LL_VIRT_BASE	0xfee36000

static struct map_desc lluart_io_desc __initdata = {
	.virtual	= HB_DEBUG_LL_VIRT_BASE,
	.pfn		= __phys_to_pfn(HB_DEBUG_LL_PHYS_BASE),
	.length		= SZ_4K,
	.type		= MT_DEVICE,
};

void __init highbank_lluart_map_io(void)
{
	iotable_init(&lluart_io_desc, 1);
}
