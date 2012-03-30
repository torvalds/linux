/*
 * Copyright (c) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/cpuquiet.h>
#include <linux/sysfs.h>

static DEFINE_MUTEX(userspace_mutex);

static int governor_set(unsigned int cpu, bool active)
{
	mutex_lock(&userspace_mutex);
	if (active)
		cpuquiet_wake_cpu(cpu);
	else
		cpuquiet_quiesence_cpu(cpu);
	mutex_unlock(&userspace_mutex);

	return 0;
}

struct cpuquiet_governor userspace_governor = {
	.name		= "userspace",
	.store_active	= governor_set,
	.owner		= THIS_MODULE,
};

static int __init init_usermode(void)
{
	return cpuquiet_register_governor(&userspace_governor);
}

static void __exit exit_usermode(void)
{
	cpuquiet_unregister_governor(&userspace_governor);
}

MODULE_LICENSE("GPL");
module_init(init_usermode);
module_exit(exit_usermode);
