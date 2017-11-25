/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include "ssi_config.h"
#include "ssi_driver.h"
#include "cc_crypto_ctx.h"
#include "ssi_sysfs.h"

#ifdef ENABLE_CC_SYSFS

static struct ssi_drvdata *sys_get_drvdata(void);

static ssize_t ssi_sys_regdump_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct ssi_drvdata *drvdata = sys_get_drvdata();
	u32 register_value;
	int offset = 0;

	register_value = cc_ioread(drvdata, CC_REG(HOST_SIGNATURE));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset,
			    "%s \t(0x%lX)\t 0x%08X\n", "HOST_SIGNATURE       ",
			    DX_HOST_SIGNATURE_REG_OFFSET, register_value);
	register_value = cc_ioread(drvdata, CC_REG(HOST_IRR));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset,
			    "%s \t(0x%lX)\t 0x%08X\n", "HOST_IRR             ",
			    DX_HOST_IRR_REG_OFFSET, register_value);
	register_value = cc_ioread(drvdata, CC_REG(HOST_POWER_DOWN_EN));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset,
			    "%s \t(0x%lX)\t 0x%08X\n", "HOST_POWER_DOWN_EN   ",
			    DX_HOST_POWER_DOWN_EN_REG_OFFSET, register_value);
	register_value =  cc_ioread(drvdata, CC_REG(AXIM_MON_ERR));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset,
			    "%s \t(0x%lX)\t 0x%08X\n", "AXIM_MON_ERR         ",
			    DX_AXIM_MON_ERR_REG_OFFSET, register_value);
	register_value = cc_ioread(drvdata, CC_REG(DSCRPTR_QUEUE_CONTENT));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset,
			    "%s \t(0x%lX)\t 0x%08X\n", "DSCRPTR_QUEUE_CONTENT",
			    DX_DSCRPTR_QUEUE_CONTENT_REG_OFFSET,
			    register_value);
	return offset;
}

static ssize_t ssi_sys_help_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	static const char * const help_str[] = {
				"cat reg_dump              ",
				"Print several of CC register values",
				};
	int i = 0, offset = 0;

	offset += scnprintf(buf + offset, PAGE_SIZE - offset, "Usage:\n");
	for (i = 0; i < ARRAY_SIZE(help_str); i += 2) {
		offset += scnprintf(buf + offset, PAGE_SIZE - offset,
				    "%s\t\t%s\n", help_str[i],
				    help_str[i + 1]);
	}

	return offset;
}

/********************************************************
 *		SYSFS objects				*
 ********************************************************/
/*
 * Structure used to create a directory
 * and its attributes in sysfs.
 */
struct sys_dir {
	struct kobject *sys_dir_kobj;
	struct attribute_group sys_dir_attr_group;
	struct attribute **sys_dir_attr_list;
	u32 num_of_attrs;
	struct ssi_drvdata *drvdata; /* Associated driver context */
};

/* top level directory structures */
static struct sys_dir sys_top_dir;

/* TOP LEVEL ATTRIBUTES */
static struct kobj_attribute ssi_sys_top_level_attrs[] = {
	__ATTR(dump_regs, 0444, ssi_sys_regdump_show, NULL),
	__ATTR(help, 0444, ssi_sys_help_show, NULL),
#if defined CC_CYCLE_COUNT
	__ATTR(stats_host, 0664, ssi_sys_stat_host_db_show,
	       ssi_sys_stats_host_db_clear),
	__ATTR(stats_cc, 0664, ssi_sys_stat_cc_db_show,
	       ssi_sys_stats_cc_db_clear),
#endif

};

static struct ssi_drvdata *sys_get_drvdata(void)
{
	/* TODO: supporting multiple SeP devices would require avoiding
	 * global "top_dir" and finding associated "top_dir" by traversing
	 * up the tree to the kobject which matches one of the top_dir's
	 */
	return sys_top_dir.drvdata;
}

static int sys_init_dir(struct sys_dir *sys_dir, struct ssi_drvdata *drvdata,
			struct kobject *parent_dir_kobj, const char *dir_name,
			struct kobj_attribute *attrs, u32 num_of_attrs)
{
	int i;

	memset(sys_dir, 0, sizeof(struct sys_dir));

	sys_dir->drvdata = drvdata;

	/* initialize directory kobject */
	sys_dir->sys_dir_kobj =
		kobject_create_and_add(dir_name, parent_dir_kobj);

	if (!(sys_dir->sys_dir_kobj))
		return -ENOMEM;
	/* allocate memory for directory's attributes list */
	sys_dir->sys_dir_attr_list =
		kcalloc(num_of_attrs + 1, sizeof(struct attribute *),
			GFP_KERNEL);

	if (!(sys_dir->sys_dir_attr_list)) {
		kobject_put(sys_dir->sys_dir_kobj);
		return -ENOMEM;
	}

	sys_dir->num_of_attrs = num_of_attrs;

	/* initialize attributes list */
	for (i = 0; i < num_of_attrs; ++i)
		sys_dir->sys_dir_attr_list[i] = &attrs[i].attr;

	/* last list entry should be NULL */
	sys_dir->sys_dir_attr_list[num_of_attrs] = NULL;

	sys_dir->sys_dir_attr_group.attrs = sys_dir->sys_dir_attr_list;

	return sysfs_create_group(sys_dir->sys_dir_kobj,
			&sys_dir->sys_dir_attr_group);
}

static void sys_free_dir(struct sys_dir *sys_dir)
{
	if (!sys_dir)
		return;

	kfree(sys_dir->sys_dir_attr_list);

	if (sys_dir->sys_dir_kobj) {
		sysfs_remove_group(sys_dir->sys_dir_kobj,
				   &sys_dir->sys_dir_attr_group);
		kobject_put(sys_dir->sys_dir_kobj);
	}
}

int ssi_sysfs_init(struct kobject *sys_dev_obj, struct ssi_drvdata *drvdata)
{
	int retval;
	struct device *dev = drvdata_to_dev(drvdata);

	dev_info(dev, "setup sysfs under %s\n", sys_dev_obj->name);

	/* Initialize top directory */
	retval = sys_init_dir(&sys_top_dir, drvdata, sys_dev_obj, "cc_info",
			      ssi_sys_top_level_attrs,
			      ARRAY_SIZE(ssi_sys_top_level_attrs));
	return retval;
}

void ssi_sysfs_fini(void)
{
	sys_free_dir(&sys_top_dir);
}

#endif /*ENABLE_CC_SYSFS*/

