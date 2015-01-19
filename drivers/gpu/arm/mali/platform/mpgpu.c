/*******************************************************************
 *
 *  Copyright C 2013 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2010/4/1   19:46
 *
 *******************************************************************/
/* Standard Linux headers */
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>

#include <mach/io.h>
#include <plat/io.h>
#include <asm/io.h>

#include <linux/mali/mali_utgard.h>
#include <common/mali_kernel_common.h>
#include <common/mali_pmu.h>
#include "meson_main.h"

static ssize_t domain_stat_read(struct class *class, 
			struct class_attribute *attr, char *buf)
{
	unsigned int val;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	val = readl((u32 *)(IO_AOBUS_BASE + 0xf0)) & 0xff;
#else
	val = 0xffffffff;
#endif
	return sprintf(buf, "%x\n", val>>4);
}

#if MESON_CPU_TYPE > MESON_CPU_TYPE_MESON6TVD
#define PREHEAT_CMD "preheat"
#define PLL2_CMD "mpl2"  /* mpl2 [11] or [0xxxxxxx] */
#define SCMPP_CMD "scmpp"  /* scmpp [number of pp your want in most of time]. */
#define BSTGPU_CMD "bstgpu"  /* bstgpu [0-256] */
#define BSTPP_CMD "bstpp"  /* bstpp [0-256] */
#define LIMIT_CMD "lmt"  /* lmt [0 or 1] */
#define MAX_TOKEN 20
#define FULL_UTILIZATION 256

static ssize_t mpgpu_write(struct class *class,
			struct class_attribute *attr, const char *buf, size_t count)
{
	char *pstart, *cprt = NULL;
	u32 val = 0;
	mali_plat_info_t* pmali_plat = get_mali_plat_data();

	cprt = skip_spaces(buf);
	pstart = strsep(&cprt," ");
	if (strlen(pstart) < 1)
		goto quit;

	if(!strncmp(pstart, PREHEAT_CMD, MAX_TOKEN)) {
		if (pmali_plat->plat_preheat) {
			pmali_plat->plat_preheat();
		}
	} else if (!strncmp(pstart, PLL2_CMD, MAX_TOKEN)) {
		int base = 10;
		if ((strlen(cprt) > 2) && (cprt[0] == '0') &&
				(cprt[1] == 'x' || cprt[1] == 'X'))
			base = 16;
		if (kstrtouint(cprt, base, &val) <0)
			goto quit;
		if (val < 11)
			pmali_plat->cfg_clock = pmali_plat->cfg_clock_bkup;
		else
			pmali_plat->cfg_clock = pmali_plat->turbo_clock;
		pmali_plat->scale_info.maxclk = pmali_plat->cfg_clock;
		set_str_src(val);
	} else if (!strncmp(pstart, SCMPP_CMD, MAX_TOKEN)) {
		if ((kstrtouint(cprt, 10, &val) <0) || pmali_plat == NULL)
			goto quit;
		if ((val > 0) && (val < pmali_plat->cfg_pp)) {
			pmali_plat->sc_mpp = val;
		}
	} else if (!strncmp(pstart, BSTGPU_CMD, MAX_TOKEN)) {
		if ((kstrtouint(cprt, 10, &val) <0) || pmali_plat == NULL)
			goto quit;
		if ((val > 0) && (val < FULL_UTILIZATION)) {
			pmali_plat->bst_gpu = val;
		}
	} else if (!strncmp(pstart, BSTPP_CMD, MAX_TOKEN)) {
		if ((kstrtouint(cprt, 10, &val) <0) || pmali_plat == NULL)
			goto quit;
		if ((val > 0) && (val < FULL_UTILIZATION)) {
			pmali_plat->bst_pp = val;
		}
	} else if (!strncmp(pstart, LIMIT_CMD, MAX_TOKEN)) {
		if ((kstrtouint(cprt, 10, &val) <0) || pmali_plat == NULL)
			goto quit;
		
		if (val < 2) {
			pmali_plat->limit_on = val;
			if (val == 0) {
				pmali_plat->scale_info.maxclk = pmali_plat->cfg_clock;
				pmali_plat->scale_info.maxpp = pmali_plat->cfg_pp;
				revise_mali_rt();
			}
		}
	}
quit:
	return count;
}

static ssize_t scale_mode_read(struct class *class,
			struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", get_mali_schel_mode());
}

static ssize_t scale_mode_write(struct class *class,
			struct class_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = kstrtouint(buf, 10, &val);
	if (0 != ret)
	{
		return -EINVAL;
	}

	set_mali_schel_mode(val);

	return count;
}

static ssize_t max_pp_read(struct class *class,
			struct class_attribute *attr, char *buf)
{
	mali_plat_info_t* pmali_plat = get_mali_plat_data();
	return sprintf(buf, "%d\n", pmali_plat->scale_info.maxpp);
}

static ssize_t max_pp_write(struct class *class,
			struct class_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int val;
	mali_plat_info_t* pmali_plat;
	mali_scale_info_t* pinfo;

	pmali_plat = get_mali_plat_data();
	pinfo = &pmali_plat->scale_info;

	ret = kstrtouint(buf, 10, &val);
	if ((0 != ret) || (val > pmali_plat->cfg_pp) || (val < pinfo->minpp))
		return -EINVAL;

	pinfo->maxpp = val;
	revise_mali_rt();

	return count;
}

static ssize_t min_pp_read(struct class *class,
			struct class_attribute *attr, char *buf)
{
	mali_plat_info_t* pmali_plat = get_mali_plat_data();
	return sprintf(buf, "%d\n", pmali_plat->scale_info.minpp);
}

static ssize_t min_pp_write(struct class *class,
			struct class_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int val;
	mali_plat_info_t* pmali_plat;
	mali_scale_info_t* pinfo;

	pmali_plat = get_mali_plat_data();
	pinfo = &pmali_plat->scale_info;

	ret = kstrtouint(buf, 10, &val);
	if ((0 != ret) || (val > pinfo->maxpp) || (val < 1))
		return -EINVAL;

	pinfo->minpp = val;
	revise_mali_rt();

	return count;
}

static ssize_t max_freq_read(struct class *class,
			struct class_attribute *attr, char *buf)
{
	mali_plat_info_t* pmali_plat = get_mali_plat_data();
	return sprintf(buf, "%d\n", pmali_plat->scale_info.maxclk);
}

static ssize_t max_freq_write(struct class *class,
			struct class_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int val;
	mali_plat_info_t* pmali_plat;
	mali_scale_info_t* pinfo;

	pmali_plat = get_mali_plat_data();
	pinfo = &pmali_plat->scale_info;

	ret = kstrtouint(buf, 10, &val);
	if ((0 != ret) || (val > pmali_plat->cfg_clock) || (val < pinfo->minclk))
		return -EINVAL;

	pinfo->maxclk = val;
	revise_mali_rt();

	return count;
}

static ssize_t min_freq_read(struct class *class,
			struct class_attribute *attr, char *buf)
{
	mali_plat_info_t* pmali_plat = get_mali_plat_data();
	return sprintf(buf, "%d\n", pmali_plat->scale_info.minclk);
}

static ssize_t min_freq_write(struct class *class,
			struct class_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int val;
	mali_plat_info_t* pmali_plat;
	mali_scale_info_t* pinfo;

	pmali_plat = get_mali_plat_data();
	pinfo = &pmali_plat->scale_info;

	ret = kstrtouint(buf, 10, &val);
	if ((0 != ret) || (val > pinfo->maxclk))
		return -EINVAL;

	pinfo->minclk = val;
	revise_mali_rt();

	return count;
}

static ssize_t freq_read(struct class *class,
			struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", get_current_frequency());
}

static ssize_t freq_write(struct class *class,
			struct class_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int val;
	u32 clk, pp;
	get_mali_rt_clkpp(&clk, &pp);

	ret = kstrtouint(buf, 10, &val);
	if (0 != ret)
		return -EINVAL;

	set_mali_rt_clkpp(val, pp, 1);

	return count;
}

static ssize_t current_pp_read(struct class *class,
			struct class_attribute *attr, char *buf)
{
	u32 clk, pp;
	get_mali_rt_clkpp(&clk, &pp);
	return sprintf(buf, "%d\n", pp);
}

static ssize_t current_pp_write(struct class *class,
			struct class_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int val;
	u32 clk, pp;

	get_mali_rt_clkpp(&clk, &pp);
	ret = kstrtouint(buf, 10, &val);
	if (0 != ret)
	{
		return -EINVAL;
	}

	ret = kstrtouint(buf, 10, &val);
	if (0 != ret)
		return -EINVAL;

	set_mali_rt_clkpp(clk, val, 1);

	return count;
}

#endif


static struct class_attribute mali_class_attrs[] = {
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	__ATTR(domain_stat,	0644, domain_stat_read, NULL),
	__ATTR(mpgpucmd,	0644, NULL,		mpgpu_write),
	__ATTR(scale_mode,	0644, scale_mode_read,  scale_mode_write),
	__ATTR(min_freq,	0644, min_freq_read,  	min_freq_write),
	__ATTR(max_freq,	0644, max_freq_read,	max_freq_write),
	__ATTR(min_pp,		0644, min_pp_read,	min_pp_write),
	__ATTR(max_pp,		0644, max_pp_read,	max_pp_write),
	__ATTR(cur_freq,	0644, freq_read,	freq_write),
	__ATTR(cur_pp,		0644, current_pp_read,	current_pp_write),
#endif
};

static struct class mpgpu_class = {
	.name = "mpgpu",
};

int mpgpu_class_init(void)
{
	int ret = 0;
	int i;
	int attr_num =  ARRAY_SIZE(mali_class_attrs);
	
	ret = class_register(&mpgpu_class);
	if (ret) {
		printk(KERN_ERR "%s: class_register failed\n", __func__);
		return ret;
	}
	for (i = 0; i< attr_num; i++) {
		ret = class_create_file(&mpgpu_class, &mali_class_attrs[i]);
		if (ret) {
			printk(KERN_ERR "%d ST: class item failed to register\n", i);
		}
	}
	return ret;
}

void  mpgpu_class_exit(void)
{
	class_unregister(&mpgpu_class);
}

