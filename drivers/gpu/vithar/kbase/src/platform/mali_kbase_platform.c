/* drivers/gpu/vithar/kbase/src/platform/mali_kbase_platform.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T604 platform-dependent codes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_platform.c
 * Platform-dependent init.
 */

#include <osk/mali_osk.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_pm.h>
#include <kbase/src/common/mali_kbase_uku.h>
#include <kbase/src/common/mali_kbase_mem.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/linux/mali_kbase_mem_linux.h>
#include <uk/mali_ukk.h>

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

#include <mach/map.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/regs-pmu5.h>
#include <asm/delay.h>
#include <kbase/src/platform/mali_kbase_runtime_pm.h>
#include <kbase/src/platform/mali_kbase_dvfs.h>

#define VITHAR_DEFAULT_CLOCK 267000000
static int kbase_platform_power_clock_init(struct device *dev)
{
	int timeout;
	struct clk *mpll = NULL;
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);
	if (!kbdev)
		panic("oops");

	/* Turn on G3D power */
	__raw_writel(0x7, EXYNOS5_G3D_CONFIGURATION);

	/* Wait for G3D power stability for 1ms */
	timeout = 10;
	while((__raw_readl(EXYNOS5_G3D_STATUS) & 0x7) != 0x7) {
		if(timeout == 0) {
			/* need to call panic  */
			panic("failed to turn on g3d power\n");
			goto out;
		}
		timeout--;
		udelay(100);
	}

	/* Turn on G3D clock */

	mpll = clk_get(dev, "mout_mpll_user");
	if(IS_ERR(mpll)) {
		OSK_PRINT_ERROR(OSK_BASE_PM, "failed to clk_get [mout_mpll_user]\n");
		goto out;
	}

	kbdev->sclk_g3d = clk_get(dev, "sclk_g3d");
	if(IS_ERR(kbdev->sclk_g3d)) {
		OSK_PRINT_ERROR(OSK_BASE_PM, "failed to clk_get [sclk_g3d]\n");
		goto out;
	}

	clk_set_parent(kbdev->sclk_g3d, mpll);
	if(IS_ERR(kbdev->sclk_g3d)) {
		OSK_PRINT_ERROR(OSK_BASE_PM, "failed to clk_set_parent\n");
		goto out;
	}

	clk_set_rate(kbdev->sclk_g3d, VITHAR_DEFAULT_CLOCK);
	if(IS_ERR(kbdev->sclk_g3d)) {
		OSK_PRINT_ERROR(OSK_BASE_PM, "failed to clk_set_rate [sclk_g3d] = %d\n", VITHAR_DEFAULT_CLOCK);
		goto out;
	}

	(void) clk_enable(kbdev->sclk_g3d);

	return 0;
out:
	return -EPERM;
}

static int kbase_platform_clock_on(struct device *dev)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	(void) clk_enable(kbdev->sclk_g3d);

	return 0;
}

static int kbase_platform_clock_off(struct device *dev)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	(void)clk_disable(kbdev->sclk_g3d);

	return 0;
}

static inline int kbase_platform_is_power_on(void)
{
    return ((__raw_readl(EXYNOS5_G3D_STATUS) & 0x7) == 0x7) ? 1 : 0;
}

static int kbase_platform_power_on(struct device *dev)
{
	int timeout;
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	/* Turn on G3D  */
	__raw_writel(0x7, EXYNOS5_G3D_CONFIGURATION);

	/* Wait for G3D power stability */
	timeout = 1000;

	while((__raw_readl(EXYNOS5_G3D_STATUS) & 0x7) != 0x7) {
		if(timeout == 0) {
			/* need to call panic  */
			panic("failed to turn on g3d via g3d_configuration\n");
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(10);
	}

	return 0;
}

static int kbase_platform_power_off(struct device *dev)
{
	int timeout;
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	/* Turn off G3D  */
	__raw_writel(0x0, EXYNOS5_G3D_CONFIGURATION);

	/* Wait for G3D power stability */
	timeout = 1000;

	while(__raw_readl(EXYNOS5_G3D_STATUS) & 0x7) {
		if(timeout == 0) {
			/* need to call panic */
			panic( "failed to turn off g3d via g3d_configuration\n");
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(10);
	}

	return 0;
}


int kbase_platform_cmu_pmu_control(struct device *dev, int control)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	osk_spinlock_irq_lock(&kbdev->pm.cmu_pmu_lock);

	if(control == 0) // off
	{
		if(kbdev->pm.cmu_pmu_status == 0)
		{
			osk_spinlock_irq_unlock(&kbdev->pm.cmu_pmu_lock);
			return 0;
		}

		if(kbase_platform_power_off(dev))
			panic("failed to turn off g3d power\n");
		if(kbase_platform_clock_off(dev))
			panic("failed to turn off sclk_g3d\n");

		kbdev->pm.cmu_pmu_status = 0;
		printk( KERN_ERR "3D cmu_pmu_control - off\n" );
	}
	else // on
	{
		if(kbdev->pm.cmu_pmu_status == 1)
		{
			osk_spinlock_irq_unlock(&kbdev->pm.cmu_pmu_lock);
			return 0;
		}

		if(kbase_platform_clock_on(dev))
			panic("failed to turn on sclk_g3d\n");
		if(kbase_platform_power_on(dev))
			panic("failed to turn on g3d power\n");

		kbdev->pm.cmu_pmu_status = 1;
		printk( KERN_ERR "3D cmu_pmu_control - on\n");
	}

	osk_spinlock_irq_unlock(&kbdev->pm.cmu_pmu_lock);

	return 0;
}

static ssize_t show_clock(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	unsigned int clkrate;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	if(!kbdev->sclk_g3d)
		return -ENODEV;

	clkrate = clk_get_rate(kbdev->sclk_g3d);
	ret += snprintf(buf+ret, PAGE_SIZE-ret, "Current sclk_g3d[G3D_BLK] = %dMhz", clkrate/1000000);

	/* To be revised  */
	ret += snprintf(buf+ret, PAGE_SIZE-ret, "\nPossible settings : 400, 266, 200, 160, 133, 100, 50Mhz");

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "\n");
	else
	{
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
	}

	return ret;
}

static ssize_t set_clock(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	unsigned int tmp = 0;
	unsigned int cmd = 0;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	if(!kbdev->sclk_g3d)
		return -ENODEV;

	if (sysfs_streq("400", buf)) {
	    cmd = 1;
	    clk_set_rate(kbdev->sclk_g3d, 400000000);
	} else if (sysfs_streq("266", buf)) {
	    cmd = 1;
	    clk_set_rate(kbdev->sclk_g3d, 267000000);
	} else if (sysfs_streq("200", buf)) {
	    cmd = 1;
	    clk_set_rate(kbdev->sclk_g3d, 200000000);
	} else if (sysfs_streq("160", buf)) {
	    cmd = 1;
	    clk_set_rate(kbdev->sclk_g3d, 160000000);
	} else if (sysfs_streq("133", buf)) {
	    cmd = 1;
	    clk_set_rate(kbdev->sclk_g3d, 134000000);
	} else if (sysfs_streq("100", buf)) {
	    cmd = 1;
	    clk_set_rate(kbdev->sclk_g3d, 100000000);
	} else if (sysfs_streq("50", buf)) {
	    cmd = 1;
	    clk_set_rate(kbdev->sclk_g3d, 50000000);
	} else {
	    dev_err(dev, "set_clock: invalid value\n");
	    return -ENOENT;
	}

	if(cmd == 1) {
	    /* Waiting for clock is stable */
	    do {
		tmp = __raw_readl(EXYNOS5_CLKDIV_STAT_TOP0);
	    } while (tmp & 0x1000000);
	}
	else if(cmd == 2) {
	    /* Do we need to check */
	}

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

	for(i = 0 ; i < num_registered_fb ; i++) {
	    ret += snprintf(buf+ret, PAGE_SIZE-ret, "fb[%d] xres=%d, yres=%d, addr=0x%lx\n", i, registered_fb[i]->var.xres, registered_fb[i]->var.yres, registered_fb[i]->fix.smem_start);
	}

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "\n");
	else
	{
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
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

	if(DL1Data0)
	{
	    asm volatile("mrc p15, 0, %0, c15, c1, 0" : "=r" (val));
	    *DL1Data0 = val;
	}
	if(DL1Data1)
	{
	    asm volatile("mrc p15, 0, %0, c15, c1, 1" : "=r" (val));
	    *DL1Data1 = val;
	}
	if(DL1Data2)
	{
	    asm volatile("mrc p15, 0, %0, c15, c1, 2" : "=r" (val));
	    *DL1Data2 = val;
	}
	if(DL1Data3)
	{
	    asm volatile("mrc p15, 0, %0, c15, c1, 3" : "=r" (val));
	    *DL1Data3 = val;
	}
}

static inline void asm_ramindex_mcr(u32 val)
{
	asm volatile("mcr p15, 0, %0, c15, c4, 0" : : "r" (val));
	asm volatile("dsb");
	asm volatile("isb");
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
	if(ramindex == L1_I_tag_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L1-I data RAM */
	else if(ramindex == L1_I_data_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L1-I BTB RAM */
	else if(ramindex == L1_I_BTB_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L1-I GHB RAM */
	else if(ramindex == L1_I_GHB_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L1-I TLB RAM */
	else if(ramindex == L1_I_TLB_RAM) 
	{
	    printk("L1-I TLB RAM\n");
	    for(entries = 0 ; entries < 32 ; entries++)
	    {
		get_tlb_array((((u8)ramindex) << 24) + entries, &DL1Data0, &DL1Data1, &DL1Data2, NULL);
		printk("entries[%d], DL1Data0=%08x, DL1Data1=%08x DL1Data2=%08x\n", entries, DL1Data0, DL1Data1 & 0xffff, 0x0);
	    }
	}
	/* L1-I indirect predictor RAM */
	else if(ramindex == L1_I_indirect_predictor_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L1-D tag RAM */
	else if(ramindex == L1_D_tag_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L1-D data RAM */
	else if(ramindex == L1_D_data_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L1-D load TLB array */
	else if(ramindex == L1_D_load_TLB_array)
	{
	    printk("L1-D load TLB array\n");
	    for(entries = 0 ; entries < 32 ; entries++)
	    {
		get_tlb_array((((u8)ramindex) << 24) + entries, &DL1Data0, &DL1Data1, &DL1Data2, &DL1Data3);
		printk("entries[%d], DL1Data0=%08x, DL1Data1=%08x, DL1Data2=%08x, DL1Data3=%08x\n", entries, DL1Data0, DL1Data1, DL1Data2, DL1Data3 & 0x3f);
	    }
	}
	/* L1-D store TLB array */
	else if(ramindex == L1_D_store_TLB_array)
	{
	    printk("\nL1-D store TLB array\n");
	    for(entries = 0 ; entries < 32 ; entries++)
	    {
		get_tlb_array((((u8)ramindex) << 24) + entries, &DL1Data0, &DL1Data1, &DL1Data2, &DL1Data3);
		printk("entries[%d], DL1Data0=%08x, DL1Data1=%08x, DL1Data2=%08x, DL1Data3=%08x\n", entries, DL1Data0, DL1Data1, DL1Data2, DL1Data3 & 0x3f);
	    }
	}
	/* L2 tag RAM */
	else if(ramindex == L2_tag_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L2 data RAM */
	else if(ramindex == L2_data_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L2 snoop tag RAM */
	else if(ramindex == L2_snoop_tag_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L2 data ECC RAM */
	else if(ramindex == L2_data_ECC_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L2 dirty RAM */
	else if(ramindex == L2_dirty_RAM) 
	{
	    printk("Not implemented yet\n");
	}
	/* L2 TLB array */
	else if(ramindex == L2_TLB_RAM)
	{
	    printk("\nL2 TLB array\n");
	    for(ways = 0 ; ways < 4 ; ways++)
	    {
		for(entries = 0 ; entries < 512 ; entries++)
		{
		    get_tlb_array((ramindex << 24) + (ways << 18) + entries, &DL1Data0, &DL1Data1, &DL1Data2, &DL1Data3);
		    printk("ways[%d]:entries[%d], DL1Data0=%08x, DL1Data1=%08x, DL1Data2=%08x, DL1Data3=%08x\n", ways, entries, DL1Data0, DL1Data1, DL1Data2, DL1Data3);
		}
	    }
	}
	else {
	}

	ret += snprintf(buf+ret, PAGE_SIZE-ret, "Succeeded...\n");

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "\n");
	else
	{
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
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
	    printk("Invalid value....\n\n");
	    printk("Available options are one of below\n");
	    printk("L1_I_tag_RAM, L1_I_data_RAM, L1_I_BTB_RAM\n");
	    printk("L1_I_GHB_RAM, L1_I_TLB_RAM, L1_I_indirect_predictor_RAM\n");
	    printk("L1_D_tag_RAM, L1_D_data_RAM, L1_D_load_TLB_array, L1_D_store_TLB_array\n");
	    printk("L2_tag_RAM, L2_data_RAM, L2_snoop_tag_RAM, L2_data_ECC_RAM\n");
	    printk("L2_dirty_RAM, L2_TLB_RAM\n");
	}

	return count;
}

static ssize_t show_vol(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int vol;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	kbase_platform_get_voltage(dev, &vol);
	ret += snprintf(buf+ret, PAGE_SIZE-ret, "Current operating voltage for vithar = %d", vol);

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "\n");
	else
	{
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
	}

	return ret;
}

static ssize_t set_vol(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	if (sysfs_streq("950000", buf)) {
	    kbase_platform_set_voltage(dev, 950000);
	} else if (sysfs_streq("1000000", buf)) {
	    kbase_platform_set_voltage(dev, 1000000);
	} else if (sysfs_streq("1050000", buf)) {
	    kbase_platform_set_voltage(dev, 1050000);
	} else if (sysfs_streq("1100000", buf)) {
	    kbase_platform_set_voltage(dev, 1100000);
	} else if (sysfs_streq("1150000", buf)) {
	    kbase_platform_set_voltage(dev, 1150000);
	} else if (sysfs_streq("1200000", buf)) {
	    kbase_platform_set_voltage(dev, 1200000);
	} else {
	    printk("invalid voltage\n");
	}

	return count;
}

static int get_clkout_cmu_top(int *val)
{
    *val = __raw_readl(EXYNOS5_CLKOUT_CMU_TOP);
    if((*val & 0x1f) == 0xB) /* CLKOUT is ACLK_400 in CLKOUT_CMU_TOP */
	return 1;
    else
	return 0;
}

static void set_clkout_for_3d(void)
{
    int tmp;

    tmp = 0x0;
    tmp |= 0x1000B; // ACLK_400 selected
    tmp |= 9 << 8; // divided by (9 + 1)
    __raw_writel(tmp, EXYNOS5_CLKOUT_CMU_TOP);

    tmp = 0x0;
    tmp |= 7 << 8; // CLKOUT_CMU_TOP selected
    __raw_writel(tmp, S5P_PMU_DEBUG);
}

static ssize_t show_clkout(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int val;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	if(get_clkout_cmu_top(&val))
	   ret += snprintf(buf+ret, PAGE_SIZE-ret, "Current CLKOUT is g3d divided by 10, CLKOUT_CMU_TOP=0x%x", val);
	else
	   ret += snprintf(buf+ret, PAGE_SIZE-ret, "Current CLKOUT is not g3d, CLKOUT_CMU_TOP=0x%x", val);

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "\n");
	else
	{
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
	}

	return ret;
}

static ssize_t set_clkout(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	if (sysfs_streq("3d", buf)) {
	    set_clkout_for_3d();
	} else {
	    printk("invalid val (only 3d is accepted\n");
	}

	return count;
}

static ssize_t show_dvfs(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_VITHAR_DVFS
	if(kbdev->pm.metrics.timer.active == MALI_FALSE )
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "G3D DVFS is off");
	else
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "G3D DVFS is on");
#else
	ret += snprintf(buf+ret, PAGE_SIZE-ret, "G3D DVFS is disabled");
#endif

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "\n");
	else
	{
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
	}

	return ret;
}

static ssize_t set_dvfs(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef CONFIG_VITHAR_DVFS
	osk_error ret;
	int vol;
#endif
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_VITHAR_DVFS
	if (sysfs_streq("off", buf)) {
		if(kbdev->pm.metrics.timer.active == MALI_FALSE )
			return count;
		osk_timer_stop(&kbdev->pm.metrics.timer);
		kbase_platform_get_default_voltage(dev, &vol);
		if(vol != 0)
			kbase_platform_set_voltage(dev, vol);
		clk_set_rate(kbdev->sclk_g3d, VITHAR_DEFAULT_CLOCK);
	} else if (sysfs_streq("on", buf)) {
		if(kbdev->pm.metrics.timer.active == MALI_TRUE )
			return count;
		ret = osk_timer_start(&kbdev->pm.metrics.timer, KBASE_PM_DVFS_FREQUENCY);
		if (ret != OSK_ERR_NONE)
		{
			printk("osk_timer_start failed\n");
		}
	} else {
		printk("invalid val -only [on, off] is accepted\n");
	}
#else
	printk("G3D DVFS is disabled\n");
#endif

	return count;
}

/** The sysfs file @c clock, fbdev.
 *
 * This is used for obtaining information about the vithar operating clock & framebuffer address,
 */
DEVICE_ATTR(clock, S_IRUGO|S_IWUSR, show_clock, set_clock);
DEVICE_ATTR(fbdev, S_IRUGO, show_fbdev, NULL);
DEVICE_ATTR(dtlb, S_IRUGO|S_IWUSR, show_dtlb, set_dtlb);
DEVICE_ATTR(vol, S_IRUGO|S_IWUSR, show_vol, set_vol);
DEVICE_ATTR(clkout, S_IRUGO|S_IWUSR, show_clkout, set_clkout);
DEVICE_ATTR(dvfs, S_IRUGO|S_IWUSR, show_dvfs, set_dvfs);

static int kbase_platform_create_sysfs_file(struct device *dev)
{
	if (device_create_file(dev, &dev_attr_clock))
	{
		dev_err(dev, "Couldn't create sysfs file [clock]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_fbdev))
	{
		dev_err(dev, "Couldn't create sysfs file [fbdev]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dtlb))
	{
		dev_err(dev, "Couldn't create sysfs file [dtlb]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_vol))
	{
		dev_err(dev, "Couldn't create sysfs file [vol]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_clkout))
	{
		dev_err(dev, "Couldn't create sysfs file [clkout]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dvfs))
	{
		dev_err(dev, "Couldn't create sysfs file [dvfs]\n");
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
	device_remove_file(dev, &dev_attr_vol);
	device_remove_file(dev, &dev_attr_clkout);
	device_remove_file(dev, &dev_attr_dvfs);
}

int kbase_platform_init(struct device *dev)
{
	if(kbase_platform_power_clock_init(dev)){
		return -ENOENT;
	}
#ifdef CONFIG_REGULATOR
	if(kbase_platform_regulator_init(dev)){
		return -ENOENT;
	}
#endif

#ifdef CONFIG_VITHAR_RT_PM
	kbase_device_runtime_init_timer(dev);
#endif

	if(kbase_platform_create_sysfs_file(dev)){
		return -ENOENT;
	}

#ifdef CONFIG_VITHAR_DVFS
	kbase_platform_dvfs_init(dev, 3);
#endif

	return 0;
}

void kbase_platform_term(struct device *dev)
{
#ifdef CONFIG_VITHAR_RT_PM
	kbase_device_runtime_disable(dev);
#endif

#ifdef CONFIG_VITHAR_DVFS
	kbase_platform_dvfs_term();
#endif

#ifdef CONFIG_REGULATOR
	kbase_platform_regulator_disable(dev);
#endif

	return;
}
