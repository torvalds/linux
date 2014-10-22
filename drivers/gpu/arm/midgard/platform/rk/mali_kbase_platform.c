/* drivers/gpu/t6xx/kbase/src/platform/rk/mali_kbase_platform.c
 *
 * Rockchip SoC Mali-T764 platform-dependent codes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_platform.c
 * Platform-dependent init.
 */
#include <mali_kbase.h>
#include <mali_kbase_pm.h>
#include <mali_kbase_uku.h>
#include <mali_kbase_mem.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_mem_linux.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <platform/rk/mali_kbase_platform.h>
#include <platform/rk/mali_kbase_dvfs.h>

#include <mali_kbase_gator.h>

#include <linux/rockchip/dvfs.h> 

#define MALI_T7XX_DEFAULT_CLOCK 100000


static int mali_clk_status = 0;
static int mali_pd_status = 0;

u32 kbase_group_error = 0;
static struct kobject *rk_gpu;

int mali_dvfs_clk_set(struct dvfs_node *node,unsigned long rate)
{
	int ret = 0;
	if(!node)
	{
		printk("clk_get_dvfs_node error \r\n");
		ret = -1;
	}
	ret = dvfs_clk_set_rate(node,rate * MALI_KHZ);
	if(ret)
	{
		printk("dvfs_clk_set_rate error \r\n");
	}
	return ret;
}
static int kbase_platform_power_clock_init(kbase_device *kbdev)
{
	/*struct device *dev = kbdev->dev;*/
	struct rk_context *platform;

	platform = (struct rk_context *)kbdev->platform_context;
	if (NULL == platform)
		panic("oops");
	
	/* enable mali t760 powerdomain*/	
	platform->mali_pd = clk_get(NULL,"pd_gpu");
	if(IS_ERR_OR_NULL(platform->mali_pd))
	{
		platform->mali_pd = NULL;
		printk(KERN_ERR "%s, %s(): failed to get [platform->mali_pd]\n", __FILE__, __func__);
		goto out;
	}
	else
	{
		clk_prepare_enable(platform->mali_pd);
		printk("mali pd enabled\n");
	}
	mali_pd_status = 1;
	
	/* enable mali t760 clock */
	platform->mali_clk_node = clk_get_dvfs_node("clk_gpu");
	if (IS_ERR_OR_NULL(platform->mali_clk_node)) 
	{
		platform->mali_clk_node = NULL;
		printk(KERN_ERR "%s, %s(): failed to get [platform->mali_clk_node]\n", __FILE__, __func__);
		goto out;
	} 
	else 
	{
		dvfs_clk_prepare_enable(platform->mali_clk_node);
		printk("clk enabled\n");
	}
	mali_dvfs_clk_set(platform->mali_clk_node,MALI_T7XX_DEFAULT_CLOCK);
	
	mali_clk_status = 1;
	return 0;
	
out:
	if(platform->mali_pd)
		clk_put(platform->mali_pd);
	
	return -EPERM;

}
int kbase_platform_clock_off(struct kbase_device *kbdev)
{
	struct rk_context *platform;
	if (!kbdev)
		return -ENODEV;

	platform = (struct rk_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (mali_clk_status == 0)
		return 0;
	
	if((platform->mali_clk_node))
		dvfs_clk_disable_unprepare(platform->mali_clk_node);
	
	mali_clk_status = 0;

	return 0;
}

int kbase_platform_clock_on(struct kbase_device *kbdev)
{
	struct rk_context *platform;
	if (!kbdev)
		return -ENODEV;

	platform = (struct rk_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (mali_clk_status == 1)
		return 0;
	
	if(platform->mali_clk_node)
		dvfs_clk_prepare_enable(platform->mali_clk_node);

	mali_clk_status = 1;

	return 0;
}
int kbase_platform_is_power_on(void)
{
	return mali_pd_status;
}

/*turn on power domain*/
int kbase_platform_power_on(struct kbase_device *kbdev)
{
	struct rk_context *platform;
	if (!kbdev)
		return -ENODEV;

	platform = (struct rk_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (mali_pd_status == 1)
		return 0;
#if 1	
	if(platform->mali_pd)
		clk_prepare_enable(platform->mali_pd);
#endif
	mali_pd_status = 1;
	KBASE_TIMELINE_GPU_POWER(kbdev, 1);

	return 0;
}

/*turn off power domain*/
int kbase_platform_power_off(struct kbase_device *kbdev)
{
	struct rk_context *platform;
	if (!kbdev)
		return -ENODEV;

	platform = (struct rk_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (mali_pd_status== 0)
		return 0;
#if 1
	if(platform->mali_pd)
		clk_disable_unprepare(platform->mali_pd);
#endif
	mali_pd_status = 0;
	KBASE_TIMELINE_GPU_POWER(kbdev, 0);

	return 0;
}

int kbase_platform_cmu_pmu_control(struct kbase_device *kbdev, int control)
{
	unsigned long flags;
	struct rk_context *platform;
	if (!kbdev)
		return -ENODEV;

	platform = (struct rk_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	spin_lock_irqsave(&platform->cmu_pmu_lock, flags);

	/* off */
	if (control == 0) 
	{
		if (platform->cmu_pmu_status == 0) 
		{
			spin_unlock_irqrestore(&platform->cmu_pmu_lock, flags);
			return 0;
		}

		if (kbase_platform_power_off(kbdev))
			panic("failed to turn off mali power domain\n");
		if (kbase_platform_clock_off(kbdev))
			panic("failed to turn off mali clock\n");

		platform->cmu_pmu_status = 0;
		printk("turn off mali power \n");
	} 
	else 
	{
		/* on */
		if (platform->cmu_pmu_status == 1) 
		{
			spin_unlock_irqrestore(&platform->cmu_pmu_lock, flags);
			return 0;
		}

		if (kbase_platform_power_on(kbdev))
			panic("failed to turn on mali power domain\n");
		if (kbase_platform_clock_on(kbdev))
			panic("failed to turn on mali clock\n");

		platform->cmu_pmu_status = 1;
		printk(KERN_ERR "turn on mali power\n");
	}

	spin_unlock_irqrestore(&platform->cmu_pmu_lock, flags);

	return 0;
}

static ssize_t error_count_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	ssize_t ret;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", kbdev->kbase_group_error);
	return ret;
}
static DEVICE_ATTR(error_count, S_IRUGO, error_count_show, NULL);

#ifdef CONFIG_MALI_MIDGARD_DEBUG_SYS
static ssize_t show_clock(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	struct rk_context *platform;
	ssize_t ret = 0;
	unsigned int clkrate;
	int i ;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	platform = (struct rk_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (!platform->mali_clk_node)
	{
		printk("mali_clk_node not init\n");
		return -ENODEV;
	}
	clkrate = dvfs_clk_get_rate(platform->mali_clk_node);
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current clk mali = %dMhz", clkrate / 1000000);

	/* To be revised  */
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings:");
	for(i=0;i<MALI_DVFS_STEP;i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ",p_mali_dvfs_infotbl[i].clock/1000);
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Mhz");

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_clock(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	struct rk_context *platform;
	unsigned int tmp = 0, freq = 0;
	kbdev = dev_get_drvdata(dev);
	tmp = 0;	
	if (!kbdev)
		return -ENODEV;

	platform = (struct rk_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (!platform->mali_clk_node)
		return -ENODEV;
#if 0
	if (sysfs_streq("500", buf)) {
		freq = 500;
	} else if (sysfs_streq("400", buf)) {
		freq = 400;
	} else if (sysfs_streq("350", buf)) {
		freq = 350;
	} else if (sysfs_streq("266", buf)) {
		freq = 266;
	} else if (sysfs_streq("160", buf)) {
		freq = 160;
	} else if (sysfs_streq("100", buf)) {
		freq = 100;
	} else {
		dev_err(dev, "set_clock: invalid value\n");
		return -ENOENT;
	}
#endif
	freq = simple_strtoul(buf, NULL, 10);

	kbase_platform_dvfs_set_level(kbdev, kbase_platform_dvfs_get_level(freq));
	return count;
}

static ssize_t show_fbdev(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int i;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < num_registered_fb; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "fb[%d] xres=%d, yres=%d, addr=0x%lx\n", i, registered_fb[i]->var.xres, registered_fb[i]->var.yres, registered_fb[i]->fix.smem_start);

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

typedef enum {
	L1_I_tag_RAM = 0x00,
	L1_I_data_RAM = 0x01,
	L1_I_BTB_RAM = 0x02,
	L1_I_GHB_RAM = 0x03,
	L1_I_TLB_RAM = 0x04,
	L1_I_indirect_predictor_RAM = 0x05,
	L1_D_tag_RAM = 0x08,
	L1_D_data_RAM = 0x09,
	L1_D_load_TLB_array = 0x0A,
	L1_D_store_TLB_array = 0x0B,
	L2_tag_RAM = 0x10,
	L2_data_RAM = 0x11,
	L2_snoop_tag_RAM = 0x12,
	L2_data_ECC_RAM = 0x13,
	L2_dirty_RAM = 0x14,
	L2_TLB_RAM = 0x18
} RAMID_type;

static inline void asm_ramindex_mrc(u32 *DL1Data0, u32 *DL1Data1, u32 *DL1Data2, u32 *DL1Data3)
{
	u32 val;

	if (DL1Data0) {
		asm volatile ("mrc p15, 0, %0, c15, c1, 0" : "=r" (val));
		*DL1Data0 = val;
	}
	if (DL1Data1) {
		asm volatile ("mrc p15, 0, %0, c15, c1, 1" : "=r" (val));
		*DL1Data1 = val;
	}
	if (DL1Data2) {
		asm volatile ("mrc p15, 0, %0, c15, c1, 2" : "=r" (val));
		*DL1Data2 = val;
	}
	if (DL1Data3) {
		asm volatile ("mrc p15, 0, %0, c15, c1, 3" : "=r" (val));
		*DL1Data3 = val;
	}
}

static inline void asm_ramindex_mcr(u32 val)
{
	asm volatile ("mcr p15, 0, %0, c15, c4, 0" : : "r" (val));
	asm volatile ("dsb");
	asm volatile ("isb");
}

static void get_tlb_array(u32 val, u32 *DL1Data0, u32 *DL1Data1, u32 *DL1Data2, u32 *DL1Data3)
{
	asm_ramindex_mcr(val);
	asm_ramindex_mrc(DL1Data0, DL1Data1, DL1Data2, DL1Data3);
}

static RAMID_type ramindex = L1_D_load_TLB_array;
static ssize_t show_dtlb(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int entries, ways;
	u32 DL1Data0 = 0, DL1Data1 = 0, DL1Data2 = 0, DL1Data3 = 0;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	/* L1-I tag RAM */
	if (ramindex == L1_I_tag_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-I data RAM */
	else if (ramindex == L1_I_data_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-I BTB RAM */
	else if (ramindex == L1_I_BTB_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-I GHB RAM */
	else if (ramindex == L1_I_GHB_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-I TLB RAM */
	else if (ramindex == L1_I_TLB_RAM) {
		printk(KERN_DEBUG "L1-I TLB RAM\n");
		for (entries = 0; entries < 32; entries++) {
			get_tlb_array((((u8) ramindex) << 24) + entries, &DL1Data0, &DL1Data1, &DL1Data2, NULL);
			printk(KERN_DEBUG "entries[%d], DL1Data0=%08x, DL1Data1=%08x DL1Data2=%08x\n", entries, DL1Data0, DL1Data1 & 0xffff, 0x0);
		}
	}
	/* L1-I indirect predictor RAM */
	else if (ramindex == L1_I_indirect_predictor_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-D tag RAM */
	else if (ramindex == L1_D_tag_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-D data RAM */
	else if (ramindex == L1_D_data_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-D load TLB array */
	else if (ramindex == L1_D_load_TLB_array) {
		printk(KERN_DEBUG "L1-D load TLB array\n");
		for (entries = 0; entries < 32; entries++) {
			get_tlb_array((((u8) ramindex) << 24) + entries, &DL1Data0, &DL1Data1, &DL1Data2, &DL1Data3);
			printk(KERN_DEBUG "entries[%d], DL1Data0=%08x, DL1Data1=%08x, DL1Data2=%08x, DL1Data3=%08x\n", entries, DL1Data0, DL1Data1, DL1Data2, DL1Data3 & 0x3f);
		}
	}
	/* L1-D store TLB array */
	else if (ramindex == L1_D_store_TLB_array) {
		printk(KERN_DEBUG "\nL1-D store TLB array\n");
		for (entries = 0; entries < 32; entries++) {
			get_tlb_array((((u8) ramindex) << 24) + entries, &DL1Data0, &DL1Data1, &DL1Data2, &DL1Data3);
			printk(KERN_DEBUG "entries[%d], DL1Data0=%08x, DL1Data1=%08x, DL1Data2=%08x, DL1Data3=%08x\n", entries, DL1Data0, DL1Data1, DL1Data2, DL1Data3 & 0x3f);
		}
	}
	/* L2 tag RAM */
	else if (ramindex == L2_tag_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L2 data RAM */
	else if (ramindex == L2_data_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L2 snoop tag RAM */
	else if (ramindex == L2_snoop_tag_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L2 data ECC RAM */
	else if (ramindex == L2_data_ECC_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L2 dirty RAM */
	else if (ramindex == L2_dirty_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");

	/* L2 TLB array */
	else if (ramindex == L2_TLB_RAM) {
		printk(KERN_DEBUG "\nL2 TLB array\n");
		for (ways = 0; ways < 4; ways++) {
			for (entries = 0; entries < 512; entries++) {
				get_tlb_array((ramindex << 24) + (ways << 18) + entries, &DL1Data0, &DL1Data1, &DL1Data2, &DL1Data3);
				printk(KERN_DEBUG "ways[%d]:entries[%d], DL1Data0=%08x, DL1Data1=%08x, DL1Data2=%08x, DL1Data3=%08x\n", ways, entries, DL1Data0, DL1Data1, DL1Data2, DL1Data3);
			}
		}
	} else {
	}

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "Succeeded...\n");

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}
	return ret;
}

static ssize_t set_dtlb(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	if (sysfs_streq("L1_I_tag_RAM", buf)) {
		ramindex = L1_I_tag_RAM;
	} else if (sysfs_streq("L1_I_data_RAM", buf)) {
		ramindex = L1_I_data_RAM;
	} else if (sysfs_streq("L1_I_BTB_RAM", buf)) {
		ramindex = L1_I_BTB_RAM;
	} else if (sysfs_streq("L1_I_GHB_RAM", buf)) {
		ramindex = L1_I_GHB_RAM;
	} else if (sysfs_streq("L1_I_TLB_RAM", buf)) {
		ramindex = L1_I_TLB_RAM;
	} else if (sysfs_streq("L1_I_indirect_predictor_RAM", buf)) {
		ramindex = L1_I_indirect_predictor_RAM;
	} else if (sysfs_streq("L1_D_tag_RAM", buf)) {
		ramindex = L1_D_tag_RAM;
	} else if (sysfs_streq("L1_D_data_RAM", buf)) {
		ramindex = L1_D_data_RAM;
	} else if (sysfs_streq("L1_D_load_TLB_array", buf)) {
		ramindex = L1_D_load_TLB_array;
	} else if (sysfs_streq("L1_D_store_TLB_array", buf)) {
		ramindex = L1_D_store_TLB_array;
	} else if (sysfs_streq("L2_tag_RAM", buf)) {
		ramindex = L2_tag_RAM;
	} else if (sysfs_streq("L2_data_RAM", buf)) {
		ramindex = L2_data_RAM;
	} else if (sysfs_streq("L2_snoop_tag_RAM", buf)) {
		ramindex = L2_snoop_tag_RAM;
	} else if (sysfs_streq("L2_data_ECC_RAM", buf)) {
		ramindex = L2_data_ECC_RAM;
	} else if (sysfs_streq("L2_dirty_RAM", buf)) {
		ramindex = L2_dirty_RAM;
	} else if (sysfs_streq("L2_TLB_RAM", buf)) {
		ramindex = L2_TLB_RAM;
	} else {
		printk(KERN_DEBUG "Invalid value....\n\n");
		printk(KERN_DEBUG "Available options are one of below\n");
		printk(KERN_DEBUG "L1_I_tag_RAM, L1_I_data_RAM, L1_I_BTB_RAM\n");
		printk(KERN_DEBUG "L1_I_GHB_RAM, L1_I_TLB_RAM, L1_I_indirect_predictor_RAM\n");
		printk(KERN_DEBUG "L1_D_tag_RAM, L1_D_data_RAM, L1_D_load_TLB_array, L1_D_store_TLB_array\n");
		printk(KERN_DEBUG "L2_tag_RAM, L2_data_RAM, L2_snoop_tag_RAM, L2_data_ECC_RAM\n");
		printk(KERN_DEBUG "L2_dirty_RAM, L2_TLB_RAM\n");
	}

	return count;
}

static ssize_t show_dvfs(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	struct rk_context *platform;
	ssize_t ret = 0;
	unsigned int clkrate;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;
	
	platform = (struct rk_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	clkrate = dvfs_clk_get_rate(platform->mali_clk_node);

#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (kbase_platform_dvfs_get_enable_status())
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "mali DVFS is on\nutilisation:%d\ncurrent clock:%dMhz", kbase_platform_dvfs_get_utilisation(),clkrate/1000000);
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "mali  DVFS is off,clock:%dMhz",clkrate/1000000);
#else
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "mali  DVFS is disabled");
#endif

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_dvfs(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
#ifdef CONFIG_MALI_MIDGARD_DVFS
	struct rk_context *platform;
#endif

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	platform = (struct rk_context *)kbdev->platform_context;
	if (sysfs_streq("off", buf)) {
		/*kbase_platform_dvfs_enable(false, MALI_DVFS_BL_CONFIG_FREQ);*/
		kbase_platform_dvfs_enable(false, p_mali_dvfs_infotbl[MALI_DVFS_STEP-1].clock);	
		platform->dvfs_enabled = false;
	} else if (sysfs_streq("on", buf)) {
		/*kbase_platform_dvfs_enable(true, MALI_DVFS_START_FREQ);*/
		kbase_platform_dvfs_enable(true, p_mali_dvfs_infotbl[0].clock);
		platform->dvfs_enabled = true;
	} else {
		printk(KERN_DEBUG "invalid val -only [on, off] is accepted\n");
	}
#else
	printk(KERN_DEBUG "mali  DVFS is disabled\n");
#endif
	return count;
}

static ssize_t show_upper_lock_dvfs(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int i;
#ifdef CONFIG_MALI_MIDGARD_DVFS
	int locked_level = -1;
#endif

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	locked_level = mali_get_dvfs_upper_locked_freq();
	if (locked_level > 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current Upper Lock Level = %dMhz", locked_level);
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Unset the Upper Lock Level");
	/*ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings : 400, 350,266, 160, 100, If you want to unlock : 600 or off");*/
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings :");
	for(i=0;i<MALI_DVFS_STEP;i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ",p_mali_dvfs_infotbl[i].clock/1000);
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "Mhz");
	ret += snprintf(buf + ret, PAGE_SIZE - ret, ", If you want to unlock : off");

#else
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "mali DVFS is disabled. You can not set");
#endif

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_upper_lock_dvfs(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int i;
	unsigned int freq;
	kbdev = dev_get_drvdata(dev);
	freq = 0;

	if (!kbdev)
		return -ENODEV;

freq = simple_strtoul(buf, NULL, 10);

#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (sysfs_streq("off", buf)) 
	{
		mali_dvfs_freq_unlock();
	} 
	else 
	{
		for(i=0;i<MALI_DVFS_STEP;i++)
		{
			if (p_mali_dvfs_infotbl[i].clock == freq) 
			{
				mali_dvfs_freq_lock(i);
				break;
			}
			if(i==MALI_DVFS_STEP)
			{
				dev_err(dev, "set_clock: invalid value\n");
				return -ENOENT;
			}
		}
	}
#else				/* CONFIG_MALI_MIDGARD_DVFS */
	printk(KERN_DEBUG "mali DVFS is disabled. You can not set\n");
#endif

	return count;
}

static ssize_t show_under_lock_dvfs(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int i;
#ifdef CONFIG_MALI_MIDGARD_DVFS
	int locked_level = -1;
#endif

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	locked_level = mali_get_dvfs_under_locked_freq();
	if (locked_level > 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current Under Lock Level = %dMhz", locked_level);
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Unset the Under Lock Level");
	/*ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings : 600, 400, 350,266, 160, If you want to unlock : 100 or off");*/
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings :");
	for(i=0;i<MALI_DVFS_STEP;i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ",p_mali_dvfs_infotbl[i].clock/1000);
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "Mhz");
	ret += snprintf(buf + ret, PAGE_SIZE - ret, ", If you want to unlock : off");

#else
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "mali DVFS is disabled. You can not set");
#endif

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_under_lock_dvfs(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i;
	unsigned int freq;
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);
	freq = 0;

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (sysfs_streq("off", buf)) 
	{
		mali_dvfs_freq_unlock();
	} 
	else 
	{
		for(i=0;i<MALI_DVFS_STEP;i++)
		{
			if (p_mali_dvfs_infotbl[i].clock == freq) 
			{
				mali_dvfs_freq_lock(i);
				break;
			}
			if(i==MALI_DVFS_STEP)
			{
				dev_err(dev, "set_clock: invalid value\n");
				return -ENOENT;
			}
		}
	}
#else				/* CONFIG_MALI_MIDGARD_DVFS */
	printk(KERN_DEBUG "mali DVFS is disabled. You can not set\n");
#endif
	return count;
}

/** The sysfs file @c clock, fbdev.
 *
 * This is used for obtaining information about the mali t6xx operating clock & framebuffer address,
 */
DEVICE_ATTR(clock, S_IRUGO | S_IWUSR, show_clock, set_clock);
DEVICE_ATTR(fbdev, S_IRUGO, show_fbdev, NULL);
DEVICE_ATTR(dtlb, S_IRUGO | S_IWUSR, show_dtlb, set_dtlb);
DEVICE_ATTR(dvfs, S_IRUGO | S_IWUSR, show_dvfs, set_dvfs);
DEVICE_ATTR(dvfs_upper_lock, S_IRUGO | S_IWUSR, show_upper_lock_dvfs, set_upper_lock_dvfs);
DEVICE_ATTR(dvfs_under_lock, S_IRUGO | S_IWUSR, show_under_lock_dvfs, set_under_lock_dvfs);
DEVICE_ATTR(time_in_state, S_IRUGO | S_IWUSR, show_time_in_state, set_time_in_state);

int kbase_platform_create_sysfs_file(struct device *dev)
{
	if (device_create_file(dev, &dev_attr_clock)) {
		dev_err(dev, "Couldn't create sysfs file [clock]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_fbdev)) {
		dev_err(dev, "Couldn't create sysfs file [fbdev]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dtlb)) {
		dev_err(dev, "Couldn't create sysfs file [dtlb]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dvfs)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dvfs_upper_lock)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs_upper_lock]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dvfs_under_lock)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs_under_lock]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_time_in_state)) {
		dev_err(dev, "Couldn't create sysfs file [time_in_state]\n");
		goto out;
	}
	return 0;
 out:
	return -ENOENT;
}

void kbase_platform_remove_sysfs_file(struct device *dev)
{
	device_remove_file(dev, &dev_attr_clock);
	device_remove_file(dev, &dev_attr_fbdev);
	device_remove_file(dev, &dev_attr_dtlb);
	device_remove_file(dev, &dev_attr_dvfs);
	device_remove_file(dev, &dev_attr_dvfs_upper_lock);
	device_remove_file(dev, &dev_attr_dvfs_under_lock);
	device_remove_file(dev, &dev_attr_time_in_state);
}
#endif				/* CONFIG_MALI_MIDGARD_DEBUG_SYS */

mali_error kbase_platform_init(struct kbase_device *kbdev)
{
	struct rk_context *platform;
	int ret;

	platform = kmalloc(sizeof(struct rk_context), GFP_KERNEL);

	if (NULL == platform)
		return MALI_ERROR_OUT_OF_MEMORY;

	kbdev->platform_context = (void *)platform;

	platform->cmu_pmu_status = 0;
#ifdef CONFIG_MALI_MIDGARD_DVFS
	platform->utilisation = 0;
	platform->time_busy = 0;
	platform->time_idle = 0;
	platform->time_tick = 0;
	platform->dvfs_enabled = true;
#endif

	rk_gpu = kobject_create_and_add("rk_gpu", NULL);
	if (!rk_gpu)
		return MALI_ERROR_FUNCTION_FAILED;

	ret = sysfs_create_file(rk_gpu, &dev_attr_error_count.attr);
	if(ret)
		return MALI_ERROR_FUNCTION_FAILED;

	spin_lock_init(&platform->cmu_pmu_lock);

	if (kbase_platform_power_clock_init(kbdev))
		goto clock_init_fail;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_platform_dvfs_init(kbdev);
#endif				/* CONFIG_MALI_MIDGARD_DVFS */

	/* Enable power */
	kbase_platform_cmu_pmu_control(kbdev, 1);
	return MALI_ERROR_NONE;

 clock_init_fail:
	kfree(platform);

	return MALI_ERROR_FUNCTION_FAILED;
}

void kbase_platform_term(kbase_device *kbdev)
{
	struct rk_context *platform;

	platform = (struct rk_context *)kbdev->platform_context;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_platform_dvfs_term();
#endif				/* CONFIG_MALI_MIDGARD_DVFS */

	/* Disable power */
	kbase_platform_cmu_pmu_control(kbdev, 0);
	kfree(kbdev->platform_context);
	kbdev->platform_context = 0;
	return;
}
