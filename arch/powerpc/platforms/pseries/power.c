/*
 *  Interface for power-management for ppc64 compliant platform
 *
 *  Manish Ahuja <mahuja@us.ibm.com>
 *
 *  Feb 2007
 *
 *  Copyright (C) 2007 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>

unsigned long rtas_poweron_auto; /* default and normal state is 0 */

static ssize_t auto_poweron_show(struct kset *kset, char *buf)
{
        return sprintf(buf, "%lu\n", rtas_poweron_auto);
}

static ssize_t
auto_poweron_store(struct kset *kset, const char *buf, size_t n)
{
	int ret;
	unsigned long ups_restart;
	ret = sscanf(buf, "%lu", &ups_restart);

	if ((ret == 1) && ((ups_restart == 1) || (ups_restart == 0))){
		rtas_poweron_auto = ups_restart;
		return n;
	}
	return -EINVAL;
}

static struct subsys_attribute auto_poweron_attr = {
        .attr   = {
                .name = __stringify(auto_poweron),
                .mode = 0644,
        },
        .show   = auto_poweron_show,
        .store  = auto_poweron_store,
};

#ifndef CONFIG_PM
decl_subsys(power,NULL,NULL);

static struct attribute *g[] = {
        &auto_poweron_attr.attr,
        NULL,
};

static struct attribute_group attr_group = {
        .attrs = g,
};

static int __init pm_init(void)
{
        int error = subsystem_register(&power_subsys);
        if (!error)
                error = sysfs_create_group(&power_subsys.kobj, &attr_group);
        return error;
}
core_initcall(pm_init);
#else
extern struct kset power_subsys;

static int __init apo_pm_init(void)
{
	return (subsys_create_file(&power_subsys, &auto_poweron_attr));
}
__initcall(apo_pm_init);
#endif
