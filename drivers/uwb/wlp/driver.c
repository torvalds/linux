/*
 * WiMedia Logical Link Control Protocol (WLP)
 *
 * Copyright (C) 2007 Intel Corporation
 * Reinette Chatre <reinette.chatre@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Life cycle of WLP substack
 *
 * FIXME: Docs
 */

#include <linux/module.h>

static int __init wlp_subsys_init(void)
{
	return 0;
}
module_init(wlp_subsys_init);

static void __exit wlp_subsys_exit(void)
{
	return;
}
module_exit(wlp_subsys_exit);

MODULE_AUTHOR("Reinette Chatre <reinette.chatre@intel.com>");
MODULE_DESCRIPTION("WiMedia Logical Link Control Protocol (WLP)");
MODULE_LICENSE("GPL");
