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

#ifdef CC_CYCLE_COUNT

#include <asm/timex.h>

struct stat_item {
	unsigned int min;
	unsigned int max;
	cycles_t sum;
	unsigned int count;
};

struct stat_name {
	const char *op_type_name;
	const char *stat_phase_name[MAX_STAT_PHASES];
};

static struct stat_name stat_name_db[MAX_STAT_OP_TYPES] =
{
	{
		/* STAT_OP_TYPE_NULL */
		.op_type_name = "NULL",
		.stat_phase_name = {NULL},
	},
	{
		.op_type_name = "Encode",
		.stat_phase_name[STAT_PHASE_0] = "Init and sanity checks",
		.stat_phase_name[STAT_PHASE_1] = "Map buffers",
		.stat_phase_name[STAT_PHASE_2] = "Create sequence",
		.stat_phase_name[STAT_PHASE_3] = "Send Request",
		.stat_phase_name[STAT_PHASE_4] = "HW-Q push",
		.stat_phase_name[STAT_PHASE_5] = "Sequence completion",
		.stat_phase_name[STAT_PHASE_6] = "HW cycles",
	},
	{	.op_type_name = "Decode",
		.stat_phase_name[STAT_PHASE_0] = "Init and sanity checks",
		.stat_phase_name[STAT_PHASE_1] = "Map buffers",
		.stat_phase_name[STAT_PHASE_2] = "Create sequence",
		.stat_phase_name[STAT_PHASE_3] = "Send Request",
		.stat_phase_name[STAT_PHASE_4] = "HW-Q push",
		.stat_phase_name[STAT_PHASE_5] = "Sequence completion",
		.stat_phase_name[STAT_PHASE_6] = "HW cycles",
	},
	{	.op_type_name = "Setkey",
		.stat_phase_name[STAT_PHASE_0] = "Init and sanity checks",
		.stat_phase_name[STAT_PHASE_1] = "Copy key to ctx",
		.stat_phase_name[STAT_PHASE_2] = "Create sequence",
		.stat_phase_name[STAT_PHASE_3] = "Send Request",
		.stat_phase_name[STAT_PHASE_4] = "HW-Q push",
		.stat_phase_name[STAT_PHASE_5] = "Sequence completion",
		.stat_phase_name[STAT_PHASE_6] = "HW cycles",
	},
	{
		.op_type_name = "Generic",
		.stat_phase_name[STAT_PHASE_0] = "Interrupt",
		.stat_phase_name[STAT_PHASE_1] = "ISR-to-Tasklet",
		.stat_phase_name[STAT_PHASE_2] = "Tasklet start-to-end",
		.stat_phase_name[STAT_PHASE_3] = "Tasklet:user_cb()",
		.stat_phase_name[STAT_PHASE_4] = "Tasklet:dx_X_complete() - w/o X_complete()",
		.stat_phase_name[STAT_PHASE_5] = "",
		.stat_phase_name[STAT_PHASE_6] = "HW cycles",
	}
};

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
struct sys_dir sys_top_dir;

static DEFINE_SPINLOCK(stat_lock);

/* List of DBs */
static struct stat_item stat_host_db[MAX_STAT_OP_TYPES][MAX_STAT_PHASES];
static struct stat_item stat_cc_db[MAX_STAT_OP_TYPES][MAX_STAT_PHASES];

static void init_db(struct stat_item item[MAX_STAT_OP_TYPES][MAX_STAT_PHASES])
{
	unsigned int i, j;

	/* Clear db */
	for (i = 0; i < MAX_STAT_OP_TYPES; i++) {
		for (j = 0; j < MAX_STAT_PHASES; j++) {
			item[i][j].min = 0xFFFFFFFF;
			item[i][j].max = 0;
			item[i][j].sum = 0;
			item[i][j].count = 0;
		}
	}
}

static void update_db(struct stat_item *item, unsigned int result)
{
	item->count++;
	item->sum += result;
	if (result < item->min)
		item->min = result;
	if (result > item->max)
		item->max = result;
}

static void display_db(struct stat_item item[MAX_STAT_OP_TYPES][MAX_STAT_PHASES])
{
	unsigned int i, j;
	u64 avg;

	for (i = STAT_OP_TYPE_ENCODE; i < MAX_STAT_OP_TYPES; i++) {
		for (j = 0; j < MAX_STAT_PHASES; j++) {
			if (item[i][j].count > 0) {
				avg = (u64)item[i][j].sum;
				do_div(avg, item[i][j].count);
				SSI_LOG_ERR("%s, %s: min=%d avg=%d max=%d sum=%lld count=%d\n",
					stat_name_db[i].op_type_name, stat_name_db[i].stat_phase_name[j],
					item[i][j].min, (int)avg, item[i][j].max, (long long)item[i][j].sum, item[i][j].count);
			}
		}
	}
}

/**************************************
 * Attributes show functions section  *
 **************************************/

static ssize_t ssi_sys_stats_host_db_clear(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	init_db(stat_host_db);
	return count;
}

static ssize_t ssi_sys_stats_cc_db_clear(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	init_db(stat_cc_db);
	return count;
}

static ssize_t ssi_sys_stat_host_db_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int i, j;
	char line[512];
	u32 min_cyc, max_cyc;
	u64 avg;
	ssize_t buf_len, tmp_len = 0;

	buf_len = scnprintf(buf, PAGE_SIZE,
		"phase\t\t\t\t\t\t\tmin[cy]\tavg[cy]\tmax[cy]\t#samples\n");
	if (buf_len < 0)/* scnprintf shouldn't return negative value according to its implementation*/
		return buf_len;
	for (i = STAT_OP_TYPE_ENCODE; i < MAX_STAT_OP_TYPES; i++) {
		for (j = 0; j < MAX_STAT_PHASES - 1; j++) {
			if (stat_host_db[i][j].count > 0) {
				avg = (u64)stat_host_db[i][j].sum;
				do_div(avg, stat_host_db[i][j].count);
				min_cyc = stat_host_db[i][j].min;
				max_cyc = stat_host_db[i][j].max;
			} else {
				avg = min_cyc = max_cyc = 0;
			}
			tmp_len = scnprintf(line, 512,
				"%s::%s\t\t\t\t\t%6u\t%6u\t%6u\t%7u\n",
				stat_name_db[i].op_type_name,
				stat_name_db[i].stat_phase_name[j],
				min_cyc, (unsigned int)avg, max_cyc,
				stat_host_db[i][j].count);
			if (tmp_len < 0)/* scnprintf shouldn't return negative value according to its implementation*/
				return buf_len;
			if (buf_len + tmp_len >= PAGE_SIZE)
				return buf_len;
			buf_len += tmp_len;
			strncat(buf, line, 512);
		}
	}
	return buf_len;
}

static ssize_t ssi_sys_stat_cc_db_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int i;
	char line[256];
	u32 min_cyc, max_cyc;
	u64 avg;
	ssize_t buf_len, tmp_len = 0;

	buf_len = scnprintf(buf, PAGE_SIZE,
		"phase\tmin[cy]\tavg[cy]\tmax[cy]\t#samples\n");
	if (buf_len < 0)/* scnprintf shouldn't return negative value according to its implementation*/
		return buf_len;
	for (i = STAT_OP_TYPE_ENCODE; i < MAX_STAT_OP_TYPES; i++) {
		if (stat_cc_db[i][STAT_PHASE_6].count > 0) {
			avg = (u64)stat_cc_db[i][STAT_PHASE_6].sum;
			do_div(avg, stat_cc_db[i][STAT_PHASE_6].count);
			min_cyc = stat_cc_db[i][STAT_PHASE_6].min;
			max_cyc = stat_cc_db[i][STAT_PHASE_6].max;
		} else {
			avg = min_cyc = max_cyc = 0;
		}
		tmp_len = scnprintf(line, 256,
			"%s\t%6u\t%6u\t%6u\t%7u\n",
			stat_name_db[i].op_type_name,
			min_cyc,
			(unsigned int)avg,
			max_cyc,
			stat_cc_db[i][STAT_PHASE_6].count);

		if (tmp_len < 0)/* scnprintf shouldn't return negative value according to its implementation*/
			return buf_len;

		if (buf_len + tmp_len >= PAGE_SIZE)
			return buf_len;
		buf_len += tmp_len;
		strncat(buf, line, 256);
	}
	return buf_len;
}

void update_host_stat(unsigned int op_type, unsigned int phase, cycles_t result)
{
	unsigned long flags;

	spin_lock_irqsave(&stat_lock, flags);
	update_db(&(stat_host_db[op_type][phase]), (unsigned int)result);
	spin_unlock_irqrestore(&stat_lock, flags);
}

void update_cc_stat(
	unsigned int op_type,
	unsigned int phase,
	unsigned int elapsed_cycles)
{
	update_db(&(stat_cc_db[op_type][phase]), elapsed_cycles);
}

void display_all_stat_db(void)
{
	SSI_LOG_ERR("\n=======    CYCLE COUNT STATS    =======\n");
	display_db(stat_host_db);
	SSI_LOG_ERR("\n======= CC HW CYCLE COUNT STATS =======\n");
	display_db(stat_cc_db);
}
#endif /*CC_CYCLE_COUNT*/

static ssize_t ssi_sys_regdump_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct ssi_drvdata *drvdata = sys_get_drvdata();
	u32 register_value;
	void __iomem *cc_base = drvdata->cc_base;
	int offset = 0;

	register_value = CC_HAL_READ_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_SIGNATURE));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset, "%s \t(0x%lX)\t 0x%08X  \n", "HOST_SIGNATURE       ", DX_HOST_SIGNATURE_REG_OFFSET, register_value);
	register_value = CC_HAL_READ_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_IRR));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset, "%s \t(0x%lX)\t 0x%08X  \n", "HOST_IRR             ", DX_HOST_IRR_REG_OFFSET, register_value);
	register_value = CC_HAL_READ_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_POWER_DOWN_EN));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset, "%s \t(0x%lX)\t 0x%08X  \n", "HOST_POWER_DOWN_EN   ", DX_HOST_POWER_DOWN_EN_REG_OFFSET, register_value);
	register_value =  CC_HAL_READ_REGISTER(CC_REG_OFFSET(CRY_KERNEL, AXIM_MON_ERR));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset, "%s \t(0x%lX)\t 0x%08X  \n", "AXIM_MON_ERR         ", DX_AXIM_MON_ERR_REG_OFFSET, register_value);
	register_value = CC_HAL_READ_REGISTER(CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_CONTENT));
	offset += scnprintf(buf + offset, PAGE_SIZE - offset, "%s \t(0x%lX)\t 0x%08X  \n", "DSCRPTR_QUEUE_CONTENT", DX_DSCRPTR_QUEUE_CONTENT_REG_OFFSET, register_value);
	return offset;
}

static ssize_t ssi_sys_help_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	char *help_str[] = {
				"cat reg_dump              ", "Print several of CC register values",
		#if defined CC_CYCLE_COUNT
				"cat stats_host            ", "Print host statistics",
				"echo <number> > stats_host", "Clear host statistics database",
				"cat stats_cc              ", "Print CC statistics",
				"echo <number> > stats_cc  ", "Clear CC statistics database",
		#endif
				};
	int i = 0, offset = 0;

	offset += scnprintf(buf + offset, PAGE_SIZE - offset, "Usage:\n");
	for (i = 0; i < ARRAY_SIZE(help_str); i += 2)
	   offset += scnprintf(buf + offset, PAGE_SIZE - offset, "%s\t\t%s\n", help_str[i], help_str[i + 1]);

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
	__ATTR(stats_host, 0664, ssi_sys_stat_host_db_show, ssi_sys_stats_host_db_clear),
	__ATTR(stats_cc, 0664, ssi_sys_stat_cc_db_show, ssi_sys_stats_cc_db_clear),
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
		kzalloc(sizeof(struct attribute *) * (num_of_attrs + 1),
				GFP_KERNEL);

	if (!(sys_dir->sys_dir_attr_list)) {
		kobject_put(sys_dir->sys_dir_kobj);
		return -ENOMEM;
	}

	sys_dir->num_of_attrs = num_of_attrs;

	/* initialize attributes list */
	for (i = 0; i < num_of_attrs; ++i)
		sys_dir->sys_dir_attr_list[i] = &(attrs[i].attr);

	/* last list entry should be NULL */
	sys_dir->sys_dir_attr_list[num_of_attrs] = NULL;

	sys_dir->sys_dir_attr_group.attrs = sys_dir->sys_dir_attr_list;

	return sysfs_create_group(sys_dir->sys_dir_kobj,
			&(sys_dir->sys_dir_attr_group));
}

static void sys_free_dir(struct sys_dir *sys_dir)
{
	if (!sys_dir)
		return;

	kfree(sys_dir->sys_dir_attr_list);

	if (sys_dir->sys_dir_kobj)
		kobject_put(sys_dir->sys_dir_kobj);
}

int ssi_sysfs_init(struct kobject *sys_dev_obj, struct ssi_drvdata *drvdata)
{
	int retval;

#if defined CC_CYCLE_COUNT
	/* Init. statistics */
	init_db(stat_host_db);
	init_db(stat_cc_db);
#endif

	SSI_LOG_ERR("setup sysfs under %s\n", sys_dev_obj->name);

	/* Initialize top directory */
	retval = sys_init_dir(&sys_top_dir, drvdata, sys_dev_obj,
				"cc_info", ssi_sys_top_level_attrs,
				ARRAY_SIZE(ssi_sys_top_level_attrs));
	return retval;
}

void ssi_sysfs_fini(void)
{
	sys_free_dir(&sys_top_dir);
}

#endif /*ENABLE_CC_SYSFS*/

