/*
 * arch/arm/mach-tegra/tegra2_dvfs.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>

#include "clock.h"
#include "tegra2_dvfs.h"

static struct dvfs_table virtual_cpu_process_0[] = {
	{314000000,  750},
	{456000000,  825},
	{608000000,  900},
	{760000000,  975},
	{817000000,  1000},
	{912000000,  1050},
	{1000000000, 1100},
	{0, 0},
};

static struct dvfs_table virtual_cpu_process_1[] = {
	{314000000,  750},
	{456000000,  825},
	{618000000,  900},
	{770000000,  975},
	{827000000,  1000},
	{922000000,  1050},
	{1000000000, 1100},
	{0, 0},
};

static struct dvfs_table virtual_cpu_process_2[] = {
	{494000000,  750},
	{675000000,  825},
	{817000000,  875},
	{922000000,  925},
	{1000000000, 975},
	{0, 0},
};

static struct dvfs_table virtual_cpu_process_3[] = {
	{730000000,  750},
	{760000000,  775},
	{845000000,  800},
	{1000000000, 875},
	{0, 0},
};

struct dvfs tegra_dvfs_virtual_cpu_dvfs = {
	.reg_id = "vdd_cpu",
	.process_id_table = {
		{
			.process_id = 0,
			.table = virtual_cpu_process_0,
		},
		{
			.process_id = 1,
			.table = virtual_cpu_process_1,
		},
		{
			.process_id = 2,
			.table = virtual_cpu_process_2,
		},
		{
			.process_id = 3,
			.table = virtual_cpu_process_3,
		},
	},
	.process_id_table_length = 4,
	.cpu = 1,
};
