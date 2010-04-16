/*
 * HSI framework internal interfaces,
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __LINUX_HSI_CORE_H__
#define __LINUX_HSI_CORE_H__

#include <linux/hsi/hsi.h>

struct hsi_cl_info {
	struct list_head	list;
	struct hsi_board_info	info;
};

extern struct list_head hsi_board_list;

#endif /* __LINUX_HSI_CORE_H__ */
