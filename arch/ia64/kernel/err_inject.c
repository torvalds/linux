/*
 * err_inject.c -
 *	1.) Inject errors to a processor.
 *	2.) Query error injection capabilities.
 * This driver along with user space code can be acting as an error
 * injection tool.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Written by: Fenghua Yu <fenghua.yu@intel.com>, Intel Corporation
 * Copyright (C) 2006, Intel Corp.  All rights reserved.
 *
 */
#include <linux/sysdev.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/module.h>

#define ERR_INJ_DEBUG

#define ERR_DATA_BUFFER_SIZE 3 		// Three 8-byte;

#define define_one_ro(name) 						\
static SYSDEV_ATTR(name, 0444, show_##name, NULL)

#define define_one_rw(name) 						\
static SYSDEV_ATTR(name, 0644, show_##name, store_##name)

static u64 call_start[NR_CPUS];
static u64 phys_addr[NR_CPUS];
static u64 err_type_info[NR_CPUS];
static u64 err_struct_info[NR_CPUS];
static struct {
	u64 data1;
	u64 data2;
	u64 data3;
} __attribute__((__aligned__(16))) err_data_buffer[NR_CPUS];
static s64 status[NR_CPUS];
static u64 capabilities[NR_CPUS];
static u64 resources[NR_CPUS];

#define show(name) 							\
static ssize_t 								\
show_##name(struct sys_device *dev, struct sysdev_attribute *attr,	\
		char *buf)						\
{									\
	u32 cpu=dev->id;						\
	return sprintf(buf, "%lx\n", name[cpu]);			\
}

#define store(name)							\
static ssize_t 								\
store_##name(struct sys_device *dev, struct sysdev_attribute *attr,	\
					const char *buf, size_t size)	\
{									\
	unsigned int cpu=dev->id;					\
	name[cpu] = simple_strtoull(buf, NULL, 16);			\
	return size;							\
}

show(call_start)

/* It's user's responsibility to call the PAL procedure on a specific
 * processor. The cpu number in driver is only used for storing data.
 */
static ssize_t
store_call_start(struct sys_device *dev, struct sysdev_attribute *attr,
		const char *buf, size_t size)
{
	unsigned int cpu=dev->id;
	unsigned long call_start = simple_strtoull(buf, NULL, 16);

#ifdef ERR_INJ_DEBUG
	printk(KERN_DEBUG "pal_mc_err_inject for cpu%d:\n", cpu);
	printk(KERN_DEBUG "err_type_info=%lx,\n", err_type_info[cpu]);
	printk(KERN_DEBUG "err_struct_info=%lx,\n", err_struct_info[cpu]);
	printk(KERN_DEBUG "err_data_buffer=%lx, %lx, %lx.\n",
			  err_data_buffer[cpu].data1,
			  err_data_buffer[cpu].data2,
			  err_data_buffer[cpu].data3);
#endif
	switch (call_start) {
	    case 0: /* Do nothing. */
		break;
	    case 1: /* Call pal_mc_error_inject in physical mode. */
		status[cpu]=ia64_pal_mc_error_inject_phys(err_type_info[cpu],
					err_struct_info[cpu],
					ia64_tpa(&err_data_buffer[cpu]),
					&capabilities[cpu],
			 		&resources[cpu]);
		break;
	    case 2: /* Call pal_mc_error_inject in virtual mode. */
		status[cpu]=ia64_pal_mc_error_inject_virt(err_type_info[cpu],
					err_struct_info[cpu],
					ia64_tpa(&err_data_buffer[cpu]),
					&capabilities[cpu],
			 		&resources[cpu]);
		break;
	    default:
		status[cpu] = -EINVAL;
		break;
	}

#ifdef ERR_INJ_DEBUG
	printk(KERN_DEBUG "Returns: status=%d,\n", (int)status[cpu]);
	printk(KERN_DEBUG "capapbilities=%lx,\n", capabilities[cpu]);
	printk(KERN_DEBUG "resources=%lx\n", resources[cpu]);
#endif
	return size;
}

show(err_type_info)
store(err_type_info)

static ssize_t
show_virtual_to_phys(struct sys_device *dev, struct sysdev_attribute *attr,
			char *buf)
{
	unsigned int cpu=dev->id;
	return sprintf(buf, "%lx\n", phys_addr[cpu]);
}

static ssize_t
store_virtual_to_phys(struct sys_device *dev, struct sysdev_attribute *attr,
			const char *buf, size_t size)
{
	unsigned int cpu=dev->id;
	u64 virt_addr=simple_strtoull(buf, NULL, 16);
	int ret;

        ret = get_user_pages(current, current->mm, virt_addr,
                        1, VM_READ, 0, NULL, NULL);
	if (ret<=0) {
#ifdef ERR_INJ_DEBUG
		printk("Virtual address %lx is not existing.\n",virt_addr);
#endif
		return -EINVAL;
	}

	phys_addr[cpu] = ia64_tpa(virt_addr);
	return size;
}

show(err_struct_info)
store(err_struct_info)

static ssize_t
show_err_data_buffer(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	unsigned int cpu=dev->id;

	return sprintf(buf, "%lx, %lx, %lx\n",
			err_data_buffer[cpu].data1,
			err_data_buffer[cpu].data2,
			err_data_buffer[cpu].data3);
}

static ssize_t
store_err_data_buffer(struct sys_device *dev,
			struct sysdev_attribute *attr,
			const char *buf, size_t size)
{
	unsigned int cpu=dev->id;
	int ret;

#ifdef ERR_INJ_DEBUG
	printk("write err_data_buffer=[%lx,%lx,%lx] on cpu%d\n",
		 err_data_buffer[cpu].data1,
		 err_data_buffer[cpu].data2,
		 err_data_buffer[cpu].data3,
		 cpu);
#endif
	ret=sscanf(buf, "%lx, %lx, %lx",
			&err_data_buffer[cpu].data1,
			&err_data_buffer[cpu].data2,
			&err_data_buffer[cpu].data3);
	if (ret!=ERR_DATA_BUFFER_SIZE)
		return -EINVAL;

	return size;
}

show(status)
show(capabilities)
show(resources)

define_one_rw(call_start);
define_one_rw(err_type_info);
define_one_rw(err_struct_info);
define_one_rw(err_data_buffer);
define_one_rw(virtual_to_phys);
define_one_ro(status);
define_one_ro(capabilities);
define_one_ro(resources);

static struct attribute *default_attrs[] = {
	&attr_call_start.attr,
	&attr_virtual_to_phys.attr,
	&attr_err_type_info.attr,
	&attr_err_struct_info.attr,
	&attr_err_data_buffer.attr,
	&attr_status.attr,
	&attr_capabilities.attr,
	&attr_resources.attr,
	NULL
};

static struct attribute_group err_inject_attr_group = {
	.attrs = default_attrs,
	.name = "err_inject"
};
/* Add/Remove err_inject interface for CPU device */
static int __cpuinit err_inject_add_dev(struct sys_device * sys_dev)
{
	return sysfs_create_group(&sys_dev->kobj, &err_inject_attr_group);
}

static int __cpuinit err_inject_remove_dev(struct sys_device * sys_dev)
{
	sysfs_remove_group(&sys_dev->kobj, &err_inject_attr_group);
	return 0;
}
static int __cpuinit err_inject_cpu_callback(struct notifier_block *nfb,
		unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct sys_device *sys_dev;

	sys_dev = get_cpu_sysdev(cpu);
	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		err_inject_add_dev(sys_dev);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		err_inject_remove_dev(sys_dev);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata err_inject_cpu_notifier =
{
	.notifier_call = err_inject_cpu_callback,
};

static int __init
err_inject_init(void)
{
	int i;

#ifdef ERR_INJ_DEBUG
	printk(KERN_INFO "Enter error injection driver.\n");
#endif
	for_each_online_cpu(i) {
		err_inject_cpu_callback(&err_inject_cpu_notifier, CPU_ONLINE,
				(void *)(long)i);
	}

	register_hotcpu_notifier(&err_inject_cpu_notifier);

	return 0;
}

static void __exit
err_inject_exit(void)
{
	int i;
	struct sys_device *sys_dev;

#ifdef ERR_INJ_DEBUG
	printk(KERN_INFO "Exit error injection driver.\n");
#endif
	for_each_online_cpu(i) {
		sys_dev = get_cpu_sysdev(i);
		sysfs_remove_group(&sys_dev->kobj, &err_inject_attr_group);
	}
	unregister_hotcpu_notifier(&err_inject_cpu_notifier);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_AUTHOR("Fenghua Yu <fenghua.yu@intel.com>");
MODULE_DESCRIPTION("MC error injection kernel sysfs interface");
MODULE_LICENSE("GPL");
