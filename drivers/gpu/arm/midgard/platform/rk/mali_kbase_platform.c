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
 * 
 * 对 mali_kbase_platform.h 声明的 pm, clk 等接口的具体实现. 
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

// #define ENABLE_DEBUG_LOG
#include "custom_log.h"

/* ############################################################################################# */

#define MALI_T7XX_DEFAULT_CLOCK 100000


/**
 * clk_of_gpu_dvfs_node 的状态. 
 * 1, clock 被使能.
 * 0, 禁止.
 */
static int mali_clk_status = 0;

/**
 * gpu_power_domain 的状态. 
 * 1, 上电. 
 * 0, 掉电.
 */
static int mali_pd_status = 0;

// u32 kbase_group_error = 0;
static struct kobject *rk_gpu;

int mali_dvfs_clk_set(struct dvfs_node *node, unsigned long rate)
{
	int ret = 0;
	if(!node)
	{
		printk("clk_get_dvfs_node error \r\n");
		ret = -1;
	}
	/* .KP : 调用 dvfs_module 设置 gpu_clk. */
	ret = dvfs_clk_set_rate(node,rate * MALI_KHZ);
	if(ret)
	{
		printk("dvfs_clk_set_rate error \r\n");
	}
	return ret;
}

/**
 * 初始化和 gpu_pm 和 gpu_clk.
 */
static int kbase_platform_power_clock_init(struct kbase_device *kbdev)
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
	mali_dvfs_clk_set(platform->mali_clk_node, MALI_T7XX_DEFAULT_CLOCK);
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
		/* 若已经关闭, 则... */
		if (platform->cmu_pmu_status == 0) 
		{
			spin_unlock_irqrestore(&platform->cmu_pmu_lock, flags);
			return 0;
		}

		/* 关闭 gpu_power_domain. */
		if (kbase_platform_power_off(kbdev))
		{
			panic("failed to turn off mali power domain\n");
		}
		/* 关闭 gpu_dvfs_node 的 clock. */
		if (kbase_platform_clock_off(kbdev))
		{
			panic("failed to turn off mali clock\n");
		}

		platform->cmu_pmu_status = 0;
		printk("turn off mali power \n");
	} 
	else /* on */
	{
		if (platform->cmu_pmu_status == 1) 
		{
			spin_unlock_irqrestore(&platform->cmu_pmu_lock, flags);
			return 0;
		}

		/* 开启 gpu_power_domain. */
		if (kbase_platform_power_on(kbdev))
		{
			panic("failed to turn on mali power domain\n");
		}
		/* 使能 gpu_dvfs_node 的 clock. */
		if (kbase_platform_clock_on(kbdev))
		{
			panic("failed to turn on mali clock\n");
		}

		platform->cmu_pmu_status = 1;
		printk(KERN_ERR "turn on mali power\n");
	}

	spin_unlock_irqrestore(&platform->cmu_pmu_lock, flags);

	return 0;
}

/*---------------------------------------------------------------------------*/

static ssize_t error_count_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	ssize_t ret;

	D_PTR(dev);
	if ( NULL == kbdev )
	{
		E("fail to get kbase_device instance.");
		return 0;
	}

	D_DEC(kbdev->kbase_group_error);
	ret = scnprintf(buf, PAGE_SIZE, "%u\n", kbdev->kbase_group_error);
	return ret;
}
static DEVICE_ATTR(error_count, S_IRUGO, error_count_show, NULL);


/*---------------------------------------------------------------------------*/
/* < 对在 sysfs_dir_of_mali_device 下的 rk_ext_file_nodes 的具体实现,  >*/
// .DP : impl_of_rk_ext_file_nodes.

/**
 * .doc : 对 sysfs_dir_of_mali_device 下 rk_ext_file_nodes 提供的接口的定义
 *
 * sysfs_dir_of_mali_device 通常是 sys/devices/ffa30000.gpu
 *
 * 其下有如下的 rk_ext_file_nodes : 
 *	clock, 
 *		对该文件的 cat 操作, 将输出当前 gpu_clk_freq 和可能的 freq 的列表, 形如 : 
 *			current_gpu_clk_freq :  99000 KHz
 *			possible_freqs : 99000, 179000, 297000, 417000, 480000 (KHz)
 *		出现在 "possible_freqs" 中的有效频点, 依赖在 .dts 文件中的配置.
 *		可以使用 echo 命令向本文件写入待设置的 gpu_clk_freq_in_khz, 比如 : 
 *			echo 417000 > clock
 *		注意, 这里写入的 gpu_clk_freq_in_khz "必须" 是出现在 possible_freqs 中的.
 *		另外, mali_module 默认使能 dvfs, 
 *		所以若希望将 gpu_clk 固定在上面的特定 freq, 要关闭 dvfs 先 :
 *			echo off > dvfs
 *	fbdev, 
 *		只支持 cat. 
 *		.R : 目前不确定该提供接口的用意.
 *	// dtlb,
 *	dvfs,
 *		cat 该节点, 将返回当前 mali_dvfs 的状态, 包括 mali_dvfs 是否开启, gpu 使用率, 当前 gpu_clk 频率.
 *		若当前 mali_dvfs 被开启, 可能返回如下信息 : 
 *		        mali_dvfs is ON 
 *			gpu_utilisation : 100 
 *			current_gpu_clk_freq : 480 MHz
 *		若当前 mali_dvfs 被关闭, 可能返回 : 
 *			mali_dvfs is OFF 
 *			current_gpu_clk_freq : 99 MHz
 *		若一段时间没有 job 下发到 gpu, common_parts 也会自动关闭 mali_dvfs.
 *
 *		将字串 off 写入该节点, 将关闭 mali_dvfs, 
 *		且会将 gpu_clk_freq 固定到可能的最高的频率 或者 gpu_clk_freq_of_upper_limit(若有指定).
 *		之后, 若将字串 on 写入该节点, 将重新开启 mali_dvfs.
 *
 *	dvfs_upper_lock,
 *	        cat 该节点, 返回当前 dvfs_level_upper_limit 的信息, 诸如
 *	                upper_lock_freq : 417000 KHz
 *                      possible upper_lock_freqs : 99000, 179000, 297000, 417000, 480000 (KHz)
 *                      if you want to unset upper_lock_freq, to echo 'off' to this file.
 *              
 *              对该节点写入上面 possible upper_lock_freqs 中的某个 频率, 可以将该频率设置为 gpu_clk_freq_of_upper_limit, 比如.
 *                      echo 417000 > dvfs_upper_lock
 *              若要清除之前设置的 dvfs_level_upper_limit, 写入 off 即可.
 *                      
 *	dvfs_under_lock,
 *	        cat 该节点, 返回当前 dvfs_level_lower_limit 的信息, 诸如
 *	                under_lock_freq : 179000 KHz 
 *	                possible under_lock_freqs : 99000, 179000, 297000, 417000, 480000 (KHz) 
 *	                if you want to unset under_lock_freq, to echo 'off' to this file.
 *              对该节点写入上面 possible under_lock_freq 中的某个 频率, 可以将该频率设置为 gpu_clk_freq_of_lower_limit, 比如.
 *                      echo 179000 > dvfs_under_lock
 *              若要清除之前设置的 dvfs_level_lower_limit, 写入 off 即可.
 *
 *	time_in_state
 *	        cat 该节点, 返回 mali_dvfs 停留在不同 level 中的时间统计, 譬如
 *	                ------------------------------------------------------------------------------
 *                      index_of_level          gpu_clk_freq (KHz)              time_in_this_level (s)  
 *                      ------------------------------------------------------------------------------
 *                      0                       99                              206                     
 *                      1                       179                             9                       
 *                      2                       297                             0                       
 *                      3                       417                             0                       
 *                      4                       480                             47                      
 *                      ------------------------------------------------------------------------------
 *              若通过 dvfs 节点, 开启/关闭 mali_dvfs, 则本节点输出的信息可能不准确.
 *
 *              若要复位上述时间统计, 可以向该节点写入任意字串, 比如 : 
 *                      echo dummy > time_in_state
 */

#ifdef CONFIG_MALI_MIDGARD_DEBUG_SYS
static ssize_t show_clock(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	struct rk_context *platform;
	ssize_t ret = 0;
	unsigned int clkrate = 0;	// 从 dvfs_module 获取的 gpu_clk_freq, Hz 为单位.
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
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "current_gpu_clk_freq : %d KHz", clkrate / 1000);
	
	/* To be revised  */
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\npossible_freqs : ");
	for ( i = 0; i < MALI_DVFS_STEP; i++ )
	{
		if ( i < (MALI_DVFS_STEP - 1) )
		{
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d, ", p_mali_dvfs_infotbl[i].clock);
		}
		else
		{
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ", p_mali_dvfs_infotbl[i].clock);
		}
	}
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "(KHz)");

	if (ret < PAGE_SIZE - 1)
	{
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	}
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
	D("freq : %u.", freq);

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
	{
		ret += snprintf(buf + ret, 
				PAGE_SIZE - ret,
				"fb[%d] xres=%d, yres=%d, addr=0x%lx\n",
				i,
				registered_fb[i]->var.xres,
				registered_fb[i]->var.yres,
				registered_fb[i]->fix.smem_start);
	}

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

	/* 获取当前 gpu_dvfs_node 的 clk_freq, Hz 为单位. */
	clkrate = dvfs_clk_get_rate(platform->mali_clk_node);

#ifdef CONFIG_MALI_MIDGARD_DVFS
	/* 若 mali_dvfs 是 开启的, 则... */
	if (kbase_platform_dvfs_get_enable_status())
	{
		ret += snprintf(buf + ret,
				PAGE_SIZE - ret,
				"mali_dvfs is ON \ngpu_utilisation : %d \ncurrent_gpu_clk_freq : %u MHz",
				kbase_platform_dvfs_get_utilisation(),
				clkrate / 1000000);
	}
	/* 否则, ... */
	else
	{
		ret += snprintf(buf + ret,
				PAGE_SIZE - ret,
				"mali_dvfs is OFF \ncurrent_gpu_clk_freq : %u MHz",
				clkrate / 1000000);
	}
#else
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "mali_dvfs is DISABLED");
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
		D("to disable mali_dvfs, and set current_dvfs_level to the highest one.");
		kbase_platform_dvfs_enable(false, p_mali_dvfs_infotbl[MALI_DVFS_STEP-1].clock);	
		platform->dvfs_enabled = false;
	} else if (sysfs_streq("on", buf)) {
		/*kbase_platform_dvfs_enable(true, MALI_DVFS_START_FREQ);*/
		D("to disable mali_dvfs, and set current_dvfs_level to the lowest one.");
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
	int gpu_clk_freq = 0;
#endif

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
	{
		E("err.");
		return -ENODEV;
	}

#ifdef CONFIG_MALI_MIDGARD_DVFS
	gpu_clk_freq = mali_get_dvfs_upper_locked_freq();
	if (gpu_clk_freq > 0)
	{
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "upper_lock_freq : %d KHz", gpu_clk_freq);
	}
	else
	{
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "upper_lock_freq is NOT set");
	}
	/*ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings : 400, 350,266, 160, 100, If you want to unlock : 600 or off");*/

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\npossible upper_lock_freqs : ");
	for ( i = 0; i < MALI_DVFS_STEP; i++ )
	{
		if ( i < (MALI_DVFS_STEP - 1) )
		{
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d, ", p_mali_dvfs_infotbl[i].clock);
		}
		else
		{
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ", p_mali_dvfs_infotbl[i].clock);
		}
	}
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "(KHz)");
	
	if ( gpu_clk_freq > 0 )
	{
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nif you want to unset upper_lock_freq, to echo 'off' to this file.");
	}
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
	struct kbase_device *kbdev = NULL;
	int i;
	unsigned int freq = 0;              // 可能由 caller 传入的, 待设置的 gpu_freq_upper_limit.
        int ret = 0;

	kbdev = dev_get_drvdata(dev);

	if ( NULL == kbdev)
        {
                E("'kbdev' is NULL.");
	        return -ENODEV;
        }

#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (sysfs_streq("off", buf)) 
	{
		mali_dvfs_freq_unlock();
	} 
	else 
	{
		freq = simple_strtoul(buf, NULL, 10);
		D_DEC(freq);

                D("to search the level that matches target_freq; num_of_mali_dvfs_levels : %d.", MALI_DVFS_STEP);
		for(i=0;i<MALI_DVFS_STEP;i++)
		{
                        D("p_mali_dvfs_infotbl[%d].clock : %d", i, p_mali_dvfs_infotbl[i].clock);
			if (p_mali_dvfs_infotbl[i].clock == freq) 
			{
                                D("target_freq is acceptable in level '%d', to set '%d' as index of dvfs_level_upper_limit.", i, i);
				ret = mali_dvfs_freq_lock(i);
                                if ( 0 != ret )
                                {
                                        E("fail to set dvfs_level_upper_limit, ret : %d.", ret);
                                        return -EINVAL;
                                }
				break;
			}
		}
		/* 若 "没有" 找到和 target_freq match 的 level, 则... */
		if ( MALI_DVFS_STEP == i )
		{
			// dev_err(dev, "set_clock: invalid value\n");
			E("invalid target_freq : %d", freq);
			return -ENOENT;
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
	int gpu_clk_freq = 0;
#endif

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	gpu_clk_freq = mali_get_dvfs_under_locked_freq();
	if (gpu_clk_freq > 0)
	{
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "under_lock_freq : %d KHz",gpu_clk_freq);
	}
	else
	{
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "under_lock_freq is NOT set.");
	}
	/*ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings : 600, 400, 350,266, 160, If you want to unlock : 100 or off");*/
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\npossible under_lock_freqs : ");
	for ( i = 0; i < MALI_DVFS_STEP; i++ )
	{
		if ( i < (MALI_DVFS_STEP - 1) )
		{
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d, ", p_mali_dvfs_infotbl[i].clock);
		}
		else
		{
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ", p_mali_dvfs_infotbl[i].clock);
		}
	}
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "(KHz)");

	if ( gpu_clk_freq > 0 )
	{
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nif you want to unset under_lock_freq, to echo 'off' to this file.");
	}
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
	unsigned int freq = 0;
	struct kbase_device *kbdev = NULL;
        int ret = 0;

	kbdev = dev_get_drvdata(dev);
	if ( NULL == kbdev)
	{
		E("err.")
		return -ENODEV;
	}

#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (sysfs_streq("off", buf)) 
	{
		mali_dvfs_freq_under_unlock();
	} 
	else 
	{
		freq = simple_strtoul(buf, NULL, 10);
		D_DEC(freq);

		for(i=0;i<MALI_DVFS_STEP;i++)
		{
			if (p_mali_dvfs_infotbl[i].clock == freq) 
			{
                                D("to set '%d' as the index of dvfs_level_lower_limit", i);
				ret = mali_dvfs_freq_under_lock(i);
                                if ( 0 != ret )
                                {
                                        E("fail to set dvfs_level_lower_limit, ret : %d.", ret);
                                        return -EINVAL;
                                }
				break;
			}
		}
		/* 若 "没有" 找到和 target_freq match 的 level, 则... */
		if( i == MALI_DVFS_STEP )
		{
			dev_err(dev, "set_clock: invalid value\n");
			return -ENOENT;
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
/*---------------------------------------------------------------------------*/

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

	/*  rk_ext : device will crash after "cat /sys/devices/ffa30000.gpu/dtlb".
	if (device_create_file(dev, &dev_attr_dtlb)) {
		dev_err(dev, "Couldn't create sysfs file [dtlb]\n");
		goto out;
	}
	*/

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

	/* .KP : 将 'rk_context' 关联到 mali_device. */
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

void kbase_platform_term(struct kbase_device *kbdev)
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
