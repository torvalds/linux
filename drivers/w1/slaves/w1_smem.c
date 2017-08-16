/*
 *	w1_smem.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/types.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>

#include <linux/w1.h>

#define W1_FAMILY_SMEM_01	0x01
#define W1_FAMILY_SMEM_81	0x81

static struct w1_family w1_smem_family_01 = {
	.fid = W1_FAMILY_SMEM_01,
};

static struct w1_family w1_smem_family_81 = {
	.fid = W1_FAMILY_SMEM_81,
};

static int __init w1_smem_init(void)
{
	int err;

	err = w1_register_family(&w1_smem_family_01);
	if (err)
		return err;

	err = w1_register_family(&w1_smem_family_81);
	if (err) {
		w1_unregister_family(&w1_smem_family_01);
		return err;
	}

	return 0;
}

static void __exit w1_smem_fini(void)
{
	w1_unregister_family(&w1_smem_family_01);
	w1_unregister_family(&w1_smem_family_81);
}

module_init(w1_smem_init);
module_exit(w1_smem_fini);

MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol, 64bit memory family.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_SMEM_01));
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_SMEM_81));
