/*
 * comedi_fc.c
 * This is a place for code driver writers wish to share between
 * two or more drivers.  fc is short for frank-common.
 *
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Copyright (C) 2002 Frank Mori Hess
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
 */

#include <linux/module.h>
#include "../comedidev.h"

#include "comedi_fc.h"

static int __init comedi_fc_init_module(void)
{
	return 0;
}
module_init(comedi_fc_init_module);

static void __exit comedi_fc_cleanup_module(void)
{
}
module_exit(comedi_fc_cleanup_module);

MODULE_AUTHOR("Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_DESCRIPTION("Shared functions for Comedi low-level drivers");
MODULE_LICENSE("GPL");
