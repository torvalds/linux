/*
 * drivers/mmc/sunxi-host/host_fs.c
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Aaron.Maoye <leafy.myeh@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include "host_op.h"

extern unsigned int smc_debug;

void dumphex32(char* name, char* base, int len)
{
    __u32 i;

    printk("dump %s registers:", name);
    for (i=0; i<len; i+=4)
    {
        if (!(i&0xf))
            printk("\n0x%p : ", base + i);
        printk("0x%08x ", readl(base + i));
    }
    printk("\n");
}

void hexdump(char* name, char* base, int len)
{
    __u32 i;

    printk("%s :", name);
    for (i=0; i<len; i++)
    {
        if (!(i&0x1f))
            printk("\n0x%p : ", base + i);
        if (!(i&0xf))
            printk(" ");
        printk("%02x ", readb(base + i));
    }
    printk("\n");
}

#ifdef CONFIG_PROC_FS
static const char sunximmc_drv_version[] = DRIVER_VERSION;

static int sunximmc_proc_read_drvversion(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *p = page;

    p += sprintf(p, "%s\n", sunximmc_drv_version);
    return p - page;
}

static int sunximmc_proc_read_hostinfo(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *p = page;
    struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
    struct device* dev = &smc_host->pdev->dev;
    char* clksrc[] = {"hosc", "satapll_2", "sdrampll_p", "hosc"};
    char* cd_mode[] = {"none", "gpio mode", "data3 mode", "always in", "manual"};

    p += sprintf(p, "%s controller information:\n", dev_name(dev));
    p += sprintf(p, "reg base \t : %p\n", smc_host->smc_base);
    p += sprintf(p, "clock source\t : %s\n", clksrc[smc_host->clk_source]);
    p += sprintf(p, "mod clock\t : %d\n", smc_host->mod_clk);
    p += sprintf(p, "card clock\t : %d\n", smc_host->real_cclk);
    p += sprintf(p, "bus width\t : %d\n", smc_host->bus_width);
    p += sprintf(p, "present  \t : %d\n", smc_host->present);
    p += sprintf(p, "cd mode  \t : %s\n", cd_mode[smc_host->cd_mode]);
    p += sprintf(p, "read only\t : %d\n", smc_host->read_only);

    return p - page;
}


static int sunximmc_proc_read_regs(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *p = page;
    struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
    u32 i;

    p += sprintf(p, "Dump smc regs:\n");

    for (i=0; i<0x100; i+=4)
    {
        if (!(i&0xf))
            p += sprintf(p, "\n0x%08x : ", i);
        p += sprintf(p, "%08x ", readl(smc_host->smc_base + i));
    }
    p += sprintf(p, "\n");

    p += sprintf(p, "Dump ccmu regs:\n");
    for (i=0; i<0x200; i+=4)
    {
        if (!(i&0xf))
            p += sprintf(p, "\n0x%08x : ", i);
        p += sprintf(p, "%08x ", readl(SW_VA_CCM_IO_BASE + i));
    }
    p += sprintf(p, "\n");

    p += sprintf(p, "Dump gpio regs:\n");
    for (i=0; i<0x200; i+=4)
    {
        if (!(i&0xf))
            p += sprintf(p, "\n0x%08x : ", i);
        p += sprintf(p, "%08x ", readl(SW_VA_PORTC_IO_BASE+ i));
    }
    p += sprintf(p, "\n");


    return p - page;
}

static int sunximmc_proc_read_dbglevel(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *p = page;

    p += sprintf(p, "debug-level : %d\n", smc_debug);
    return p - page;
}

static int sunximmc_proc_write_dbglevel(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
    smc_debug = simple_strtoul(buffer, NULL, 10);

    return sizeof(smc_debug);
}

static int sunximmc_proc_read_insert_status(char *page, char **start, off_t off, int coutn, int *eof, void *data)
{
	char *p = page;
    struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;

	p += sprintf(p, "Usage: \"echo 1 > insert\" to scan card and \"echo 0 > insert\" to remove card\n");
	if (smc_host->cd_mode != CARD_DETECT_BY_FS)
	{
		p += sprintf(p, "Sorry, this node if only for manual attach mode(cd mode 4)\n");
	}

	p += sprintf(p, "card attach status: %s\n", smc_host->present ? "inserted" : "removed");


	return p - page;
}

static int sunximmc_proc_card_insert_ctrl(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	u32 insert = simple_strtoul(buffer, NULL, 10);
    struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
	u32 present = insert ? 1 : 0;

	if (smc_host->present ^ present)
	{
		smc_host->present = present;
		mmc_detect_change(smc_host->mmc, msecs_to_jiffies(300));
	}

	return sizeof(insert);
}

void sunximmc_procfs_attach(struct sunxi_mmc_host *smc_host)
{
    struct device *dev = &smc_host->pdev->dev;
    char sunximmc_proc_rootname[32] = {0};

    //make mmc dir in proc fs path
    snprintf(sunximmc_proc_rootname, sizeof(sunximmc_proc_rootname), "driver/%s", dev_name(dev));
    smc_host->proc_root = proc_mkdir(sunximmc_proc_rootname, NULL);
    if (IS_ERR(smc_host->proc_root))
    {
        SMC_MSG("%s: failed to create procfs \"driver/mmc\".\n", dev_name(dev));
    }

    smc_host->proc_drvver = create_proc_read_entry("drv-version", 0444, smc_host->proc_root, sunximmc_proc_read_drvversion, NULL);
    if (IS_ERR(smc_host->proc_root))
    {
        SMC_MSG("%s: failed to create procfs \"drv-version\".\n", dev_name(dev));
    }

    smc_host->proc_hostinfo = create_proc_read_entry("hostinfo", 0444, smc_host->proc_root, sunximmc_proc_read_hostinfo, smc_host);
    if (IS_ERR(smc_host->proc_hostinfo))
    {
        SMC_MSG("%s: failed to create procfs \"hostinfo\".\n", dev_name(dev));
    }

    smc_host->proc_regs = create_proc_read_entry("register", 0444, smc_host->proc_root, sunximmc_proc_read_regs, smc_host);
    if (IS_ERR(smc_host->proc_regs))
    {
        SMC_MSG("%s: failed to create procfs \"hostinfo\".\n", dev_name(dev));
    }

    smc_host->proc_dbglevel = create_proc_entry("debug-level", 0644, smc_host->proc_root);
    if (IS_ERR(smc_host->proc_dbglevel))
    {
        SMC_MSG("%s: failed to create procfs \"debug-level\".\n", dev_name(dev));
    }
    smc_host->proc_dbglevel->data = smc_host;
    smc_host->proc_dbglevel->read_proc = sunximmc_proc_read_dbglevel;
    smc_host->proc_dbglevel->write_proc = sunximmc_proc_write_dbglevel;

	smc_host->proc_insert = create_proc_entry("insert", 0644, smc_host->proc_root);
	if (IS_ERR(smc_host->proc_insert))
	{
		SMC_MSG("%s: failed to create procfs \"insert\".\n", dev_name(dev));
	}
	smc_host->proc_insert->data = smc_host;
	smc_host->proc_insert->read_proc = sunximmc_proc_read_insert_status;
	smc_host->proc_insert->write_proc = sunximmc_proc_card_insert_ctrl;

}

void sunximmc_procfs_remove(struct sunxi_mmc_host *smc_host)
{
    struct device *dev = &smc_host->pdev->dev;
    char sunximmc_proc_rootname[32] = {0};

    snprintf(sunximmc_proc_rootname, sizeof(sunximmc_proc_rootname), "driver/%s", dev_name(dev));
    remove_proc_entry("insert", smc_host->proc_root);
    remove_proc_entry("debug-level", smc_host->proc_root);
    remove_proc_entry("register", smc_host->proc_root);
    remove_proc_entry("hostinfo", smc_host->proc_root);
    remove_proc_entry("drv-version", smc_host->proc_root);
    remove_proc_entry(sunximmc_proc_rootname, NULL);
}

#else

void sunximmc_procfs_attach(struct sunxi_mmc_host *smc_host) { }
void sunximmc_procfs_remove(struct sunxi_mmc_host *smc_host) { }

#endif


