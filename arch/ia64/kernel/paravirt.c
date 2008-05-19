/******************************************************************************
 * arch/ia64/kernel/paravirt.c
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *     Yaozu (Eddie) Dong <eddie.dong@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/init.h>

#include <linux/compiler.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/iosapic.h>
#include <asm/paravirt.h>

/***************************************************************************
 * general info
 */
struct pv_info pv_info = {
	.kernel_rpl = 0,
	.paravirt_enabled = 0,
	.name = "bare hardware"
};
