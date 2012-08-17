/**
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


/**
 * @file mali_kernel_sysfs.c
 * Implementation of some sysfs data exports
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/module.h>
#include "mali_kernel_license.h"
#include "mali_kernel_common.h"
#include "mali_kernel_linux.h"
#include "mali_ukk.h"

#if MALI_LICENSE_IS_GPL

#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include "mali_kernel_sysfs.h"
#if MALI_INTERNAL_TIMELINE_PROFILING_ENABLED
#include <linux/slab.h>
#include "mali_osk_profiling.h"
#endif
#include "mali_pm.h"
#include "mali_cluster.h"
#include "mali_group.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_l2_cache.h"
#include "mali_hw_core.h"
#include "mali_kernel_core.h"
#include "mali_user_settings_db.h"
#include "mali_device_pause_resume.h"

#define POWER_BUFFER_SIZE 3

static struct dentry *mali_debugfs_dir = NULL;

typedef enum
{
        _MALI_DEVICE_SUSPEND,
        _MALI_DEVICE_RESUME,
        _MALI_DEVICE_DVFS_PAUSE,
        _MALI_DEVICE_DVFS_RESUME,
        _MALI_MAX_EVENTS
} _mali_device_debug_power_events;

static const char* const mali_power_events[_MALI_MAX_EVENTS] = {
        [_MALI_DEVICE_SUSPEND] = "suspend",
        [_MALI_DEVICE_RESUME] = "resume",
        [_MALI_DEVICE_DVFS_PAUSE] = "dvfs_pause",
        [_MALI_DEVICE_DVFS_RESUME] = "dvfs_resume",
};

static u32 virtual_power_status_register=0;
static char pwr_buf[POWER_BUFFER_SIZE];

static int open_copy_private_data(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t gp_gpx_counter_srcx_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *gpos, u32 src_id)
{
	char buf[64];
	int r;
	u32 val;
	struct mali_gp_core *gp_core = (struct mali_gp_core *)filp->private_data;

	if (0 == src_id)
	{
		val = mali_gp_core_get_counter_src0(gp_core);
	}
	else
	{
		val = mali_gp_core_get_counter_src1(gp_core);
	}

	if (MALI_HW_CORE_NO_COUNTER == val)
	{
		r = sprintf(buf, "-1\n");
	}
	else
	{
		r = sprintf(buf, "%u\n", val);
	}
	return simple_read_from_buffer(ubuf, cnt, gpos, buf, r);
}

static ssize_t gp_gpx_counter_srcx_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *gpos, u32 src_id)
{
	struct mali_gp_core *gp_core = (struct mali_gp_core *)filp->private_data;
	char buf[64];
	long val;
	int ret;

	if (cnt >= sizeof(buf))
	{
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt))
	{
		return -EFAULT;
	}

	buf[cnt] = 0;

	ret = strict_strtol(buf, 10, &val);
	if (ret < 0)
	{
		return ret;
	}

	if (val < 0)
	{
		/* any negative input will disable counter */
		val = MALI_HW_CORE_NO_COUNTER;
	}

	if (0 == src_id)
	{
		if (MALI_TRUE != mali_gp_core_set_counter_src0(gp_core, (u32)val))
		{
			return 0;
		}
	}
	else
	{
		if (MALI_TRUE != mali_gp_core_set_counter_src1(gp_core, (u32)val))
		{
			return 0;
		}
	}

	*gpos += cnt;
	return cnt;
}

static ssize_t gp_all_counter_srcx_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *gpos, u32 src_id)
{
	char buf[64];
	long val;
	int ret;
	u32 ci;
	struct mali_cluster *cluster;

	if (cnt >= sizeof(buf))
	{
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt))
	{
		return -EFAULT;
	}

	buf[cnt] = 0;

	ret = strict_strtol(buf, 10, &val);
	if (ret < 0)
	{
		return ret;
	}

	if (val < 0)
	{
		/* any negative input will disable counter */
		val = MALI_HW_CORE_NO_COUNTER;
	}

	ci = 0;
	cluster = mali_cluster_get_global_cluster(ci);
	while (NULL != cluster)
	{
		u32 gi = 0;
		struct mali_group *group = mali_cluster_get_group(cluster, gi);
		while (NULL != group)
		{
			struct mali_gp_core *gp_core = mali_group_get_gp_core(group);
			if (NULL != gp_core)
			{
				if (0 == src_id)
				{
					if (MALI_TRUE != mali_gp_core_set_counter_src0(gp_core, (u32)val))
					{
						return 0;
					}
				}
				else
				{
					if (MALI_TRUE != mali_gp_core_set_counter_src1(gp_core, (u32)val))
					{
						return 0;
					}
				}				
			}

			/* try next group */
			gi++;
			group = mali_cluster_get_group(cluster, gi);
		}

		/* try next cluster */
		ci++;
		cluster = mali_cluster_get_global_cluster(ci);
	}

	*gpos += cnt;
	return cnt;
}

static ssize_t gp_gpx_counter_src0_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *gpos)
{
	return gp_gpx_counter_srcx_read(filp, ubuf, cnt, gpos, 0);
}

static ssize_t gp_gpx_counter_src1_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *gpos)
{
	return gp_gpx_counter_srcx_read(filp, ubuf, cnt, gpos, 1);
}

static ssize_t gp_gpx_counter_src0_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *gpos)
{
	return gp_gpx_counter_srcx_write(filp, ubuf, cnt, gpos, 0);
}

static ssize_t gp_gpx_counter_src1_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *gpos)
{
	return gp_gpx_counter_srcx_write(filp, ubuf, cnt, gpos, 1);
}

static ssize_t gp_all_counter_src0_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *gpos)
{
	return gp_all_counter_srcx_write(filp, ubuf, cnt, gpos, 0);
}

static ssize_t gp_all_counter_src1_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *gpos)
{
	return gp_all_counter_srcx_write(filp, ubuf, cnt, gpos, 1);
}

static const struct file_operations gp_gpx_counter_src0_fops = {
	.owner = THIS_MODULE,
	.open  = open_copy_private_data,
	.read  = gp_gpx_counter_src0_read,
	.write = gp_gpx_counter_src0_write,
};

static const struct file_operations gp_gpx_counter_src1_fops = {
	.owner = THIS_MODULE,
	.open  = open_copy_private_data,
	.read  = gp_gpx_counter_src1_read,
	.write = gp_gpx_counter_src1_write,
};

static const struct file_operations gp_all_counter_src0_fops = {
	.owner = THIS_MODULE,
	.write = gp_all_counter_src0_write,
};

static const struct file_operations gp_all_counter_src1_fops = {
	.owner = THIS_MODULE,
	.write = gp_all_counter_src1_write,
};

static ssize_t pp_ppx_counter_srcx_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos, u32 src_id)
{
	char buf[64];
	int r;
	u32 val;
	struct mali_pp_core *pp_core = (struct mali_pp_core *)filp->private_data;

	if (0 == src_id)
	{
		val = mali_pp_core_get_counter_src0(pp_core);
	}
	else
	{
		val = mali_pp_core_get_counter_src1(pp_core);
	}

	if (MALI_HW_CORE_NO_COUNTER == val)
	{
		r = sprintf(buf, "-1\n");
	}
	else
	{
		r = sprintf(buf, "%u\n", val);
	}
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t pp_ppx_counter_srcx_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos, u32 src_id)
{
	struct mali_pp_core *pp_core = (struct mali_pp_core *)filp->private_data;
	char buf[64];
	long val;
	int ret;

	if (cnt >= sizeof(buf))
	{
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt))
	{
		return -EFAULT;
	}

	buf[cnt] = 0;

	ret = strict_strtol(buf, 10, &val);
	if (ret < 0)
	{
		return ret;
	}

	if (val < 0)
	{
		/* any negative input will disable counter */
		val = MALI_HW_CORE_NO_COUNTER;
	}

	if (0 == src_id)
	{
		if (MALI_TRUE != mali_pp_core_set_counter_src0(pp_core, (u32)val))
		{
			return 0;
		}
	}
	else
	{
		if (MALI_TRUE != mali_pp_core_set_counter_src1(pp_core, (u32)val))
		{
			return 0;
		}
	}

	*ppos += cnt;
	return cnt;
}

static ssize_t pp_all_counter_srcx_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos, u32 src_id)
{
	char buf[64];
	long val;
	int ret;
	u32 ci;
	struct mali_cluster *cluster;

	if (cnt >= sizeof(buf))
	{
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt))
	{
		return -EFAULT;
	}

	buf[cnt] = 0;

	ret = strict_strtol(buf, 10, &val);
	if (ret < 0)
	{
		return ret;
	}

	if (val < 0)
	{
		/* any negative input will disable counter */
		val = MALI_HW_CORE_NO_COUNTER;
	}

	ci = 0;
	cluster = mali_cluster_get_global_cluster(ci);
	while (NULL != cluster)
	{
		u32 gi = 0;
		struct mali_group *group = mali_cluster_get_group(cluster, gi);
		while (NULL != group)
		{
			struct mali_pp_core *pp_core = mali_group_get_pp_core(group);
			if (NULL != pp_core)
			{
				if (0 == src_id)
				{
					if (MALI_TRUE != mali_pp_core_set_counter_src0(pp_core, (u32)val))
					{
						return 0;
					}
				}
				else
				{
					if (MALI_TRUE != mali_pp_core_set_counter_src1(pp_core, (u32)val))
					{
						return 0;
					}
				}				
			}

			/* try next group */
			gi++;
			group = mali_cluster_get_group(cluster, gi);
		}

		/* try next cluster */
		ci++;
		cluster = mali_cluster_get_global_cluster(ci);
	}

	*ppos += cnt;
	return cnt;
}

static ssize_t pp_ppx_counter_src0_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return pp_ppx_counter_srcx_read(filp, ubuf, cnt, ppos, 0);
}

static ssize_t pp_ppx_counter_src1_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return pp_ppx_counter_srcx_read(filp, ubuf, cnt, ppos, 1);
}

static ssize_t pp_ppx_counter_src0_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return pp_ppx_counter_srcx_write(filp, ubuf, cnt, ppos, 0);
}

static ssize_t pp_ppx_counter_src1_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return pp_ppx_counter_srcx_write(filp, ubuf, cnt, ppos, 1);
}

static ssize_t pp_all_counter_src0_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return pp_all_counter_srcx_write(filp, ubuf, cnt, ppos, 0);
}

static ssize_t pp_all_counter_src1_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return pp_all_counter_srcx_write(filp, ubuf, cnt, ppos, 1);
}

static const struct file_operations pp_ppx_counter_src0_fops = {
	.owner = THIS_MODULE,
	.open  = open_copy_private_data,
	.read  = pp_ppx_counter_src0_read,
	.write = pp_ppx_counter_src0_write,
};

static const struct file_operations pp_ppx_counter_src1_fops = {
	.owner = THIS_MODULE,
	.open  = open_copy_private_data,
	.read  = pp_ppx_counter_src1_read,
	.write = pp_ppx_counter_src1_write,
};

static const struct file_operations pp_all_counter_src0_fops = {
	.owner = THIS_MODULE,
	.write = pp_all_counter_src0_write,
};

static const struct file_operations pp_all_counter_src1_fops = {
	.owner = THIS_MODULE,
	.write = pp_all_counter_src1_write,
};






static ssize_t l2_l2x_counter_srcx_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos, u32 src_id)
{
	char buf[64];
	int r;
	u32 val;
	struct mali_l2_cache_core *l2_core = (struct mali_l2_cache_core *)filp->private_data;

	if (0 == src_id)
	{
		val = mali_l2_cache_core_get_counter_src0(l2_core);
	}
	else
	{
		val = mali_l2_cache_core_get_counter_src1(l2_core);
	}

	if (MALI_HW_CORE_NO_COUNTER == val)
	{
		r = sprintf(buf, "-1\n");
	}
	else
	{
		r = sprintf(buf, "%u\n", val);
	}
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t l2_l2x_counter_srcx_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos, u32 src_id)
{
	struct mali_l2_cache_core *l2_core = (struct mali_l2_cache_core *)filp->private_data;
	char buf[64];
	long val;
	int ret;

	if (cnt >= sizeof(buf))
	{
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt))
	{
		return -EFAULT;
	}

	buf[cnt] = 0;

	ret = strict_strtol(buf, 10, &val);
	if (ret < 0)
	{
		return ret;
	}

	if (val < 0)
	{
		/* any negative input will disable counter */
		val = MALI_HW_CORE_NO_COUNTER;
	}

	if (0 == src_id)
	{
		if (MALI_TRUE != mali_l2_cache_core_set_counter_src0(l2_core, (u32)val))
		{
			return 0;
		}
	}
	else
	{
		if (MALI_TRUE != mali_l2_cache_core_set_counter_src1(l2_core, (u32)val))
		{
			return 0;
		}
	}

	*ppos += cnt;
	return cnt;
}

static ssize_t l2_all_counter_srcx_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos, u32 src_id)
{
	char buf[64];
	long val;
	int ret;
	u32 l2_id;
	struct mali_l2_cache_core *l2_cache;

	if (cnt >= sizeof(buf))
	{
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt))
	{
		return -EFAULT;
	}

	buf[cnt] = 0;

	ret = strict_strtol(buf, 10, &val);
	if (ret < 0)
	{
		return ret;
	}

	if (val < 0)
	{
		/* any negative input will disable counter */
		val = MALI_HW_CORE_NO_COUNTER;
	}

	l2_id = 0;
	l2_cache = mali_l2_cache_core_get_glob_l2_core(l2_id);
	while (NULL != l2_cache)
	{
		if (0 == src_id)
		{
			if (MALI_TRUE != mali_l2_cache_core_set_counter_src0(l2_cache, (u32)val))
			{
				return 0;
			}
		}
		else
		{
			if (MALI_TRUE != mali_l2_cache_core_set_counter_src1(l2_cache, (u32)val))
			{
				return 0;
			}
		}				
		
		/* try next L2 */
		l2_id++;
		l2_cache = mali_l2_cache_core_get_glob_l2_core(l2_id);
	}

	*ppos += cnt;
	return cnt;
}

static ssize_t l2_l2x_counter_src0_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return l2_l2x_counter_srcx_read(filp, ubuf, cnt, ppos, 0);
}

static ssize_t l2_l2x_counter_src1_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return l2_l2x_counter_srcx_read(filp, ubuf, cnt, ppos, 1);
}

static ssize_t l2_l2x_counter_src0_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return l2_l2x_counter_srcx_write(filp, ubuf, cnt, ppos, 0);
}

static ssize_t l2_l2x_counter_src1_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return l2_l2x_counter_srcx_write(filp, ubuf, cnt, ppos, 1);
}

static ssize_t l2_all_counter_src0_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return l2_all_counter_srcx_write(filp, ubuf, cnt, ppos, 0);
}

static ssize_t l2_all_counter_src1_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return l2_all_counter_srcx_write(filp, ubuf, cnt, ppos, 1);
}

static const struct file_operations l2_l2x_counter_src0_fops = {
	.owner = THIS_MODULE,
	.open  = open_copy_private_data,
	.read  = l2_l2x_counter_src0_read,
	.write = l2_l2x_counter_src0_write,
};

static const struct file_operations l2_l2x_counter_src1_fops = {
	.owner = THIS_MODULE,
	.open  = open_copy_private_data,
	.read  = l2_l2x_counter_src1_read,
	.write = l2_l2x_counter_src1_write,
};

static const struct file_operations l2_all_counter_src0_fops = {
	.owner = THIS_MODULE,
	.write = l2_all_counter_src0_write,
};

static const struct file_operations l2_all_counter_src1_fops = {
	.owner = THIS_MODULE,
	.write = l2_all_counter_src1_write,
};

static ssize_t power_events_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	
	memset(pwr_buf,0,POWER_BUFFER_SIZE);
	virtual_power_status_register = 0;
	if (!strncmp(ubuf,mali_power_events[_MALI_DEVICE_SUSPEND],strlen(mali_power_events[_MALI_DEVICE_SUSPEND])))
	{
		mali_pm_os_suspend();
		/* @@@@ assuming currently suspend is successful later on to tune as per previous*/
		virtual_power_status_register =1;

	}
	else if (!strncmp(ubuf,mali_power_events[_MALI_DEVICE_RESUME],strlen(mali_power_events[_MALI_DEVICE_RESUME])))
	{
		mali_pm_os_resume();

		/* @@@@ assuming currently resume is successful later on to tune as per previous */
		virtual_power_status_register = 1;
	}
	else if (!strncmp(ubuf,mali_power_events[_MALI_DEVICE_DVFS_PAUSE],strlen(mali_power_events[_MALI_DEVICE_DVFS_PAUSE])))
	{
		mali_bool power_on;
		mali_dev_pause(&power_on);
		if (!power_on)
		{
			virtual_power_status_register = 2;
			mali_dev_resume();		
		}
		else
		{
			/*  @@@@ assuming currently resume is successful later on to tune as per previous */
			virtual_power_status_register =1;
		}
	}
	else if (!strncmp(ubuf,mali_power_events[_MALI_DEVICE_DVFS_RESUME],strlen(mali_power_events[_MALI_DEVICE_DVFS_RESUME])))
	{
		mali_dev_resume();
		/*  @@@@ assuming currently resume is successful later on to tune as per previous */
		virtual_power_status_register = 1;
                
	}
	*ppos += cnt;
	sprintf(pwr_buf, "%d",virtual_power_status_register);
	return cnt;
}

static ssize_t power_events_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return simple_read_from_buffer(ubuf, cnt, ppos, pwr_buf, POWER_BUFFER_SIZE);
}

static loff_t power_events_seek(struct file *file, loff_t offset, int orig)
{
	file->f_pos = offset;
        return 0;
}

static const struct file_operations power_events_fops = {
	.owner = THIS_MODULE,
	.read  = power_events_read,
	.write = power_events_write,
	.llseek = power_events_seek,
};


#if MALI_STATE_TRACKING
static int mali_seq_internal_state_show(struct seq_file *seq_file, void *v)
{
	u32 len = 0;
	u32 size;
	char *buf;

	size = seq_get_buf(seq_file, &buf);

	if(!size)
	{
			return -ENOMEM;
	}

	/* Create the internal state dump. */
	len  = snprintf(buf+len, size-len, "Mali device driver %s\n", SVN_REV_STRING);
	len += snprintf(buf+len, size-len, "License: %s\n\n", MALI_KERNEL_LINUX_LICENSE);

	len += _mali_kernel_core_dump_state(buf + len, size - len);

	seq_commit(seq_file, len);

	return 0;
}

static int mali_seq_internal_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, mali_seq_internal_state_show, NULL);
}

static const struct file_operations mali_seq_internal_state_fops = {
	.owner = THIS_MODULE,
	.open = mali_seq_internal_state_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif /* MALI_STATE_TRACKING */


#if MALI_INTERNAL_TIMELINE_PROFILING_ENABLED
static ssize_t profiling_record_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;

	r = sprintf(buf, "%u\n", _mali_osk_profiling_is_recording() ? 1 : 0);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t profiling_record_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
	{
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt))
	{
		return -EFAULT;
	}

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
	{
		return ret;
	}

	if (val != 0)
	{
		u32 limit = MALI_PROFILING_MAX_BUFFER_ENTRIES; /* This can be made configurable at a later stage if we need to */

		/* check if we are already recording */
		if (MALI_TRUE == _mali_osk_profiling_is_recording())
		{
			MALI_DEBUG_PRINT(3, ("Recording of profiling events already in progress\n"));
			return -EFAULT;
		}

		/* check if we need to clear out an old recording first */
		if (MALI_TRUE == _mali_osk_profiling_have_recording())
		{
			if (_MALI_OSK_ERR_OK != _mali_osk_profiling_clear())
			{
				MALI_DEBUG_PRINT(3, ("Failed to clear existing recording of profiling events\n"));
				return -EFAULT;
			}
		}

		/* start recording profiling data */
		if (_MALI_OSK_ERR_OK != _mali_osk_profiling_start(&limit))
		{
			MALI_DEBUG_PRINT(3, ("Failed to start recording of profiling events\n"));
			return -EFAULT;
		}

		MALI_DEBUG_PRINT(3, ("Profiling recording started (max %u events)\n", limit));
	}
	else
	{
		/* stop recording profiling data */
		u32 count = 0;
		if (_MALI_OSK_ERR_OK != _mali_osk_profiling_stop(&count))
		{
			MALI_DEBUG_PRINT(2, ("Failed to stop recording of profiling events\n"));
			return -EFAULT;
		}
		
		MALI_DEBUG_PRINT(2, ("Profiling recording stopped (recorded %u events)\n", count));
	}

	*ppos += cnt;
	return cnt;
}

static const struct file_operations profiling_record_fops = {
	.owner = THIS_MODULE,
	.read  = profiling_record_read,
	.write = profiling_record_write,
};

static void *profiling_events_start(struct seq_file *s, loff_t *pos)
{
	loff_t *spos;

	/* check if we have data avaiable */
	if (MALI_TRUE != _mali_osk_profiling_have_recording())
	{
		return NULL;
	}

	spos = kmalloc(sizeof(loff_t), GFP_KERNEL);
	if (NULL == spos)
	{
		return NULL;
	}

	*spos = *pos;
	return spos;
}

static void *profiling_events_next(struct seq_file *s, void *v, loff_t *pos)
{
	loff_t *spos = v;

	/* check if we have data avaiable */
	if (MALI_TRUE != _mali_osk_profiling_have_recording())
	{
		return NULL;
	}

	/* check if the next entry actually is avaiable */
	if (_mali_osk_profiling_get_count() <= (u32)(*spos + 1))
	{
		return NULL;
	}

	*pos = ++*spos;
	return spos;
}

static void profiling_events_stop(struct seq_file *s, void *v)
{
	kfree(v);
}

static int profiling_events_show(struct seq_file *seq_file, void *v)
{
	loff_t *spos = v;
	u32 index;
	u64 timestamp;
	u32 event_id;
	u32 data[5];

	index = (u32)*spos;

	/* Retrieve all events */
	if (_MALI_OSK_ERR_OK == _mali_osk_profiling_get_event(index, &timestamp, &event_id, data))
	{
		seq_printf(seq_file, "%llu %u %u %u %u %u %u\n", timestamp, event_id, data[0], data[1], data[2], data[3], data[4]);
		return 0;
	}

	return 0;
}

static const struct seq_operations profiling_events_seq_ops = {
	.start = profiling_events_start,
	.next  = profiling_events_next,
	.stop  = profiling_events_stop,
	.show  = profiling_events_show
};

static int profiling_events_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &profiling_events_seq_ops);
}

static const struct file_operations profiling_events_fops = {
	.owner = THIS_MODULE,
	.open = profiling_events_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

static ssize_t memory_used_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	size_t r;
	u32 mem = _mali_ukk_report_memory_usage();

	r = snprintf(buf, 64, "%u\n", mem);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static const struct file_operations memory_usage_fops = {
	.owner = THIS_MODULE,
	.read = memory_used_read,
};


static ssize_t user_settings_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	unsigned long val;
	int ret;
	_mali_uk_user_setting_t setting;
	char buf[32];

	cnt = min(cnt, sizeof(buf) - 1);
	if (copy_from_user(buf, ubuf, cnt))
	{
		return -EFAULT;
	}
	buf[cnt] = '\0';

	ret = strict_strtoul(buf, 10, &val);
	if (0 != ret)
	{
		return ret;
	}

	/* Update setting */
	setting = (_mali_uk_user_setting_t)(filp->private_data);
	mali_set_user_setting(setting, val);

	*ppos += cnt;
	return cnt;
}

static ssize_t user_settings_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	size_t r;
	u32 value;
	_mali_uk_user_setting_t setting;

	setting = (_mali_uk_user_setting_t)(filp->private_data);
	value = mali_get_user_setting(setting);

	r = snprintf(buf, 64, "%u\n", value);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static const struct file_operations user_settings_fops = {
	.owner = THIS_MODULE,
	.open = open_copy_private_data,
	.read = user_settings_read,
	.write = user_settings_write,
};

static int mali_sysfs_user_settings_register(void)
{
	struct dentry *mali_user_settings_dir = debugfs_create_dir("userspace_settings", mali_debugfs_dir);

	if (mali_user_settings_dir != NULL)
	{
		int i;
		for (i = 0; i < _MALI_UK_USER_SETTING_MAX; i++)
		{
			debugfs_create_file(_mali_uk_user_setting_descriptions[i], 0600, mali_user_settings_dir, (void*)i, &user_settings_fops);
		}
	}

	return 0;
}

int mali_sysfs_register(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
	int err = 0;
	struct device * mdev;

	device->mali_class = class_create(THIS_MODULE, mali_dev_name);
	if (IS_ERR(device->mali_class))
	{
		err = PTR_ERR(device->mali_class);
		goto init_class_err;
	}
	mdev = device_create(device->mali_class, NULL, dev, NULL, mali_dev_name);
	if (IS_ERR(mdev))
	{
		err = PTR_ERR(mdev);
		goto init_mdev_err;
	}

	mali_debugfs_dir = debugfs_create_dir(mali_dev_name, NULL);
	if(ERR_PTR(-ENODEV) == mali_debugfs_dir)
	{
		/* Debugfs not supported. */
		mali_debugfs_dir = NULL;
	}
	else
	{
		if(NULL != mali_debugfs_dir)
		{
			/* Debugfs directory created successfully; create files now */
			struct dentry *mali_power_dir;
			struct dentry *mali_gp_dir;
			struct dentry *mali_pp_dir;
			struct dentry *mali_l2_dir;
#if MALI_INTERNAL_TIMELINE_PROFILING_ENABLED
			struct dentry *mali_profiling_dir;
#endif

			mali_power_dir = debugfs_create_dir("power", mali_debugfs_dir);
			if (mali_power_dir != NULL)
			{
				debugfs_create_file("power_events", 0400, mali_power_dir, NULL, &power_events_fops);
			}

			mali_gp_dir = debugfs_create_dir("gp", mali_debugfs_dir);
			if (mali_gp_dir != NULL)
			{
				struct dentry *mali_gp_all_dir;
				u32 ci;
				struct mali_cluster *cluster;

				mali_gp_all_dir = debugfs_create_dir("all", mali_gp_dir);
				if (mali_gp_all_dir != NULL)
				{
					debugfs_create_file("counter_src0", 0400, mali_gp_all_dir, NULL, &gp_all_counter_src0_fops);
					debugfs_create_file("counter_src1", 0400, mali_gp_all_dir, NULL, &gp_all_counter_src1_fops);
				}

				ci = 0;
				cluster = mali_cluster_get_global_cluster(ci);
				while (NULL != cluster)
				{
					u32 gi = 0;
					struct mali_group *group = mali_cluster_get_group(cluster, gi);
					while (NULL != group)
					{
						struct mali_gp_core *gp_core = mali_group_get_gp_core(group);
						if (NULL != gp_core)
						{
							struct dentry *mali_gp_gpx_dir;
							mali_gp_gpx_dir = debugfs_create_dir("gp0", mali_gp_dir);
							if (NULL != mali_gp_gpx_dir)
							{
								debugfs_create_file("counter_src0", 0600, mali_gp_gpx_dir, gp_core, &gp_gpx_counter_src0_fops);
								debugfs_create_file("counter_src1", 0600, mali_gp_gpx_dir, gp_core, &gp_gpx_counter_src1_fops);
							}
							break; /* no need to look for any other GP cores */
						}

						/* try next group */
						gi++;
						group = mali_cluster_get_group(cluster, gi);
					}

					/* try next cluster */
					ci++;
					cluster = mali_cluster_get_global_cluster(ci);
				}
			}

			mali_pp_dir = debugfs_create_dir("pp", mali_debugfs_dir);
			if (mali_pp_dir != NULL)
			{
				struct dentry *mali_pp_all_dir;
				u32 ci;
				struct mali_cluster *cluster;

				mali_pp_all_dir = debugfs_create_dir("all", mali_pp_dir);
				if (mali_pp_all_dir != NULL)
				{
					debugfs_create_file("counter_src0", 0400, mali_pp_all_dir, NULL, &pp_all_counter_src0_fops);
					debugfs_create_file("counter_src1", 0400, mali_pp_all_dir, NULL, &pp_all_counter_src1_fops);
				}

				ci = 0;
				cluster = mali_cluster_get_global_cluster(ci);
				while (NULL != cluster)
				{
					u32 gi = 0;
					struct mali_group *group = mali_cluster_get_group(cluster, gi);
					while (NULL != group)
					{
						struct mali_pp_core *pp_core = mali_group_get_pp_core(group);
						if (NULL != pp_core)
						{
							char buf[16];
							struct dentry *mali_pp_ppx_dir;
							_mali_osk_snprintf(buf, sizeof(buf), "pp%u", mali_pp_core_get_id(pp_core));
							mali_pp_ppx_dir = debugfs_create_dir(buf, mali_pp_dir);
							if (NULL != mali_pp_ppx_dir)
							{
								debugfs_create_file("counter_src0", 0600, mali_pp_ppx_dir, pp_core, &pp_ppx_counter_src0_fops);
								debugfs_create_file("counter_src1", 0600, mali_pp_ppx_dir, pp_core, &pp_ppx_counter_src1_fops);
							}
						}

						/* try next group */
						gi++;
						group = mali_cluster_get_group(cluster, gi);
					}

					/* try next cluster */
					ci++;
					cluster = mali_cluster_get_global_cluster(ci);
				}
			}

			mali_l2_dir = debugfs_create_dir("l2", mali_debugfs_dir);
			if (mali_l2_dir != NULL)
			{
				struct dentry *mali_l2_all_dir;
				u32 l2_id;
				struct mali_l2_cache_core *l2_cache;

				mali_l2_all_dir = debugfs_create_dir("all", mali_l2_dir);
				if (mali_l2_all_dir != NULL)
				{
					debugfs_create_file("counter_src0", 0400, mali_l2_all_dir, NULL, &l2_all_counter_src0_fops);
					debugfs_create_file("counter_src1", 0400, mali_l2_all_dir, NULL, &l2_all_counter_src1_fops);
				}

				l2_id = 0;
				l2_cache = mali_l2_cache_core_get_glob_l2_core(l2_id);
				while (NULL != l2_cache)
				{
					char buf[16];
					struct dentry *mali_l2_l2x_dir;
					_mali_osk_snprintf(buf, sizeof(buf), "l2%u", l2_id);
					mali_l2_l2x_dir = debugfs_create_dir(buf, mali_l2_dir);
					if (NULL != mali_l2_l2x_dir)
					{
						debugfs_create_file("counter_src0", 0600, mali_l2_l2x_dir, l2_cache, &l2_l2x_counter_src0_fops);
						debugfs_create_file("counter_src1", 0600, mali_l2_l2x_dir, l2_cache, &l2_l2x_counter_src1_fops);
					}
					
					/* try next L2 */
					l2_id++;
					l2_cache = mali_l2_cache_core_get_glob_l2_core(l2_id);
				}
			}

			debugfs_create_file("memory_usage", 0400, mali_debugfs_dir, NULL, &memory_usage_fops);

#if MALI_INTERNAL_TIMELINE_PROFILING_ENABLED
			mali_profiling_dir = debugfs_create_dir("profiling", mali_debugfs_dir);
			if (mali_profiling_dir != NULL)
			{
				struct dentry *mali_profiling_proc_dir = debugfs_create_dir("proc", mali_profiling_dir);
				if (mali_profiling_proc_dir != NULL)
				{
					struct dentry *mali_profiling_proc_default_dir = debugfs_create_dir("default", mali_profiling_proc_dir);
					if (mali_profiling_proc_default_dir != NULL)
					{
						debugfs_create_file("enable", 0600, mali_profiling_proc_default_dir, (void*)_MALI_UK_USER_SETTING_SW_EVENTS_ENABLE, &user_settings_fops);
					}
				}
				debugfs_create_file("record", 0600, mali_profiling_dir, NULL, &profiling_record_fops);
				debugfs_create_file("events", 0400, mali_profiling_dir, NULL, &profiling_events_fops);
			}
#endif

#if MALI_STATE_TRACKING
			debugfs_create_file("state_dump", 0400, mali_debugfs_dir, NULL, &mali_seq_internal_state_fops);
#endif

			if (mali_sysfs_user_settings_register())
			{
				/* Failed to create the debugfs entries for the user settings DB. */
				MALI_DEBUG_PRINT(2, ("Failed to create user setting debugfs files. Ignoring...\n"));
			}
		}
	}

	/* Success! */
	return 0;

	/* Error handling */
init_mdev_err:
	class_destroy(device->mali_class);
init_class_err:

	return err;
}

int mali_sysfs_unregister(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
	if(NULL != mali_debugfs_dir)
	{
		debugfs_remove_recursive(mali_debugfs_dir);
	}
	device_destroy(device->mali_class, dev);
	class_destroy(device->mali_class);

	return 0;
}

#else

/* Dummy implementations for non-GPL */

int mali_sysfs_register(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
	return 0;
}

int mali_sysfs_unregister(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
	return 0;
}


#endif
