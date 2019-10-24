// SPDX-License-Identifier: GPL-2.0
/*
 * ip30-xtalk.c - Very basic Crosstalk (XIO) detection support.
 *   Copyright (C) 2004-2007 Stanislaw Skowronek <skylark@unaligned.org>
 *   Copyright (C) 2009 Johannes Dickgreber <tanzy@gmx.de>
 *   Copyright (C) 2007, 2014-2016 Joshua Kinard <kumba@gentoo.org>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/platform_data/sgi-w1.h>
#include <linux/platform_data/xtalk-bridge.h>

#include <asm/xtalk/xwidget.h>
#include <asm/pci/bridge.h>

#define IP30_SWIN_BASE(widget) \
		(0x0000000010000000 | (((unsigned long)(widget)) << 24))

#define IP30_RAW_SWIN_BASE(widget)	(IO_BASE + IP30_SWIN_BASE(widget))

#define IP30_SWIN_SIZE		(1 << 24)

#define IP30_WIDGET_XBOW        _AC(0x0, UL)    /* XBow is always 0 */
#define IP30_WIDGET_HEART       _AC(0x8, UL)    /* HEART is always 8 */
#define IP30_WIDGET_PCI_BASE    _AC(0xf, UL)    /* BaseIO PCI is always 15 */

#define XTALK_NODEV             0xffffffff

#define XBOW_REG_LINK_STAT_0    0x114
#define XBOW_REG_LINK_BLK_SIZE  0x40
#define XBOW_REG_LINK_ALIVE     0x80000000

#define HEART_INTR_ADDR		0x00000080

#define xtalk_read	__raw_readl

static void bridge_platform_create(int widget, int masterwid)
{
	struct xtalk_bridge_platform_data *bd;
	struct sgi_w1_platform_data *wd;
	struct platform_device *pdev;
	struct resource w1_res;

	wd = kzalloc(sizeof(*wd), GFP_KERNEL);
	if (!wd)
		goto no_mem;

	snprintf(wd->dev_id, sizeof(wd->dev_id), "bridge-%012lx",
		 IP30_SWIN_BASE(widget));

	memset(&w1_res, 0, sizeof(w1_res));
	w1_res.start = IP30_SWIN_BASE(widget) +
				offsetof(struct bridge_regs, b_nic);
	w1_res.end = w1_res.start + 3;
	w1_res.flags = IORESOURCE_MEM;

	pdev = platform_device_alloc("sgi_w1", PLATFORM_DEVID_AUTO);
	if (!pdev) {
		kfree(wd);
		goto no_mem;
	}
	platform_device_add_resources(pdev, &w1_res, 1);
	platform_device_add_data(pdev, wd, sizeof(*wd));
	platform_device_add(pdev);

	bd = kzalloc(sizeof(*bd), GFP_KERNEL);
	if (!bd)
		goto no_mem;
	pdev = platform_device_alloc("xtalk-bridge", PLATFORM_DEVID_AUTO);
	if (!pdev) {
		kfree(bd);
		goto no_mem;
	}

	bd->bridge_addr	= IP30_RAW_SWIN_BASE(widget);
	bd->intr_addr	= HEART_INTR_ADDR;
	bd->nasid	= 0;
	bd->masterwid	= masterwid;

	bd->mem.name	= "Bridge PCI MEM";
	bd->mem.start	= IP30_SWIN_BASE(widget) + BRIDGE_DEVIO0;
	bd->mem.end	= IP30_SWIN_BASE(widget) + IP30_SWIN_SIZE - 1;
	bd->mem.flags	= IORESOURCE_MEM;
	bd->mem_offset	= IP30_SWIN_BASE(widget);

	bd->io.name	= "Bridge PCI IO";
	bd->io.start	= IP30_SWIN_BASE(widget) + BRIDGE_DEVIO0;
	bd->io.end	= IP30_SWIN_BASE(widget) + IP30_SWIN_SIZE - 1;
	bd->io.flags	= IORESOURCE_IO;
	bd->io_offset	= IP30_SWIN_BASE(widget);

	platform_device_add_data(pdev, bd, sizeof(*bd));
	platform_device_add(pdev);
	pr_info("xtalk:%x bridge widget\n", widget);
	return;

no_mem:
	pr_warn("xtalk:%x bridge create out of memory\n", widget);
}

static unsigned int __init xbow_widget_active(s8 wid)
{
	unsigned int link_stat;

	link_stat = xtalk_read((void *)(IP30_RAW_SWIN_BASE(IP30_WIDGET_XBOW) +
					XBOW_REG_LINK_STAT_0 +
					XBOW_REG_LINK_BLK_SIZE *
					(wid - 8)));

	return (link_stat & XBOW_REG_LINK_ALIVE) ? 1 : 0;
}

static void __init xtalk_init_widget(s8 wid, s8 masterwid)
{
	xwidget_part_num_t partnum;
	widgetreg_t widget_id;

	if (!xbow_widget_active(wid))
		return;

	widget_id = xtalk_read((void *)(IP30_RAW_SWIN_BASE(wid) + WIDGET_ID));

	partnum = XWIDGET_PART_NUM(widget_id);

	switch (partnum) {
	case BRIDGE_WIDGET_PART_NUM:
	case XBRIDGE_WIDGET_PART_NUM:
		bridge_platform_create(wid, masterwid);
		break;
	default:
		pr_info("xtalk:%x unknown widget (0x%x)\n", wid, partnum);
		break;
	}
}

static int __init ip30_xtalk_init(void)
{
	int i;

	/*
	 * Walk widget IDs backwards so that BaseIO is probed first.  This
	 * ensures that the BaseIO IOC3 is always detected as eth0.
	 */
	for (i = IP30_WIDGET_PCI_BASE; i > IP30_WIDGET_HEART; i--)
		xtalk_init_widget(i, IP30_WIDGET_HEART);

	return 0;
}

arch_initcall(ip30_xtalk_init);
