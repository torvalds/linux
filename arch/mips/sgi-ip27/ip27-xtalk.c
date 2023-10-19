// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silcon Graphics, Inc.
 * Copyright (C) 2004 Christoph Hellwig.
 *
 * Generic XTALK initialization code
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/platform_device.h>
#include <linux/platform_data/sgi-w1.h>
#include <linux/platform_data/xtalk-bridge.h>
#include <asm/sn/addrs.h>
#include <asm/sn/types.h>
#include <asm/sn/klconfig.h>
#include <asm/pci/bridge.h>
#include <asm/xtalk/xtalk.h>


#define XBOW_WIDGET_PART_NUM	0x0
#define XXBOW_WIDGET_PART_NUM	0xd000	/* Xbow in Xbridge */
#define BASE_XBOW_PORT		8     /* Lowest external port */

static void bridge_platform_create(nasid_t nasid, int widget, int masterwid)
{
	struct xtalk_bridge_platform_data *bd;
	struct sgi_w1_platform_data *wd;
	struct platform_device *pdev_wd;
	struct platform_device *pdev_bd;
	struct resource w1_res;
	unsigned long offset;

	offset = NODE_OFFSET(nasid);

	wd = kzalloc(sizeof(*wd), GFP_KERNEL);
	if (!wd) {
		pr_warn("xtalk:n%d/%x bridge create out of memory\n", nasid, widget);
		return;
	}

	snprintf(wd->dev_id, sizeof(wd->dev_id), "bridge-%012lx",
		 offset + (widget << SWIN_SIZE_BITS));

	memset(&w1_res, 0, sizeof(w1_res));
	w1_res.start = offset + (widget << SWIN_SIZE_BITS) +
				offsetof(struct bridge_regs, b_nic);
	w1_res.end = w1_res.start + 3;
	w1_res.flags = IORESOURCE_MEM;

	pdev_wd = platform_device_alloc("sgi_w1", PLATFORM_DEVID_AUTO);
	if (!pdev_wd) {
		pr_warn("xtalk:n%d/%x bridge create out of memory\n", nasid, widget);
		goto err_kfree_wd;
	}
	if (platform_device_add_resources(pdev_wd, &w1_res, 1)) {
		pr_warn("xtalk:n%d/%x bridge failed to add platform resources.\n", nasid, widget);
		goto err_put_pdev_wd;
	}
	if (platform_device_add_data(pdev_wd, wd, sizeof(*wd))) {
		pr_warn("xtalk:n%d/%x bridge failed to add platform data.\n", nasid, widget);
		goto err_put_pdev_wd;
	}
	if (platform_device_add(pdev_wd)) {
		pr_warn("xtalk:n%d/%x bridge failed to add platform device.\n", nasid, widget);
		goto err_put_pdev_wd;
	}
	/* platform_device_add_data() duplicates the data */
	kfree(wd);

	bd = kzalloc(sizeof(*bd), GFP_KERNEL);
	if (!bd) {
		pr_warn("xtalk:n%d/%x bridge create out of memory\n", nasid, widget);
		goto err_unregister_pdev_wd;
	}
	pdev_bd = platform_device_alloc("xtalk-bridge", PLATFORM_DEVID_AUTO);
	if (!pdev_bd) {
		pr_warn("xtalk:n%d/%x bridge create out of memory\n", nasid, widget);
		goto err_kfree_bd;
	}


	bd->bridge_addr = RAW_NODE_SWIN_BASE(nasid, widget);
	bd->intr_addr	= BIT_ULL(47) + 0x01800000 + PI_INT_PEND_MOD;
	bd->nasid	= nasid;
	bd->masterwid	= masterwid;

	bd->mem.name	= "Bridge PCI MEM";
	bd->mem.start	= offset + (widget << SWIN_SIZE_BITS) + BRIDGE_DEVIO0;
	bd->mem.end	= offset + (widget << SWIN_SIZE_BITS) + SWIN_SIZE - 1;
	bd->mem.flags	= IORESOURCE_MEM;
	bd->mem_offset	= offset;

	bd->io.name	= "Bridge PCI IO";
	bd->io.start	= offset + (widget << SWIN_SIZE_BITS) + BRIDGE_DEVIO0;
	bd->io.end	= offset + (widget << SWIN_SIZE_BITS) + SWIN_SIZE - 1;
	bd->io.flags	= IORESOURCE_IO;
	bd->io_offset	= offset;

	if (platform_device_add_data(pdev_bd, bd, sizeof(*bd))) {
		pr_warn("xtalk:n%d/%x bridge failed to add platform data.\n", nasid, widget);
		goto err_put_pdev_bd;
	}
	if (platform_device_add(pdev_bd)) {
		pr_warn("xtalk:n%d/%x bridge failed to add platform device.\n", nasid, widget);
		goto err_put_pdev_bd;
	}
	/* platform_device_add_data() duplicates the data */
	kfree(bd);
	pr_info("xtalk:n%d/%x bridge widget\n", nasid, widget);
	return;

err_put_pdev_bd:
	platform_device_put(pdev_bd);
err_kfree_bd:
	kfree(bd);
err_unregister_pdev_wd:
	platform_device_unregister(pdev_wd);
	return;
err_put_pdev_wd:
	platform_device_put(pdev_wd);
err_kfree_wd:
	kfree(wd);
	return;
}

static int probe_one_port(nasid_t nasid, int widget, int masterwid)
{
	widgetreg_t		widget_id;
	xwidget_part_num_t	partnum;

	widget_id = *(volatile widgetreg_t *)
		(RAW_NODE_SWIN_BASE(nasid, widget) + WIDGET_ID);
	partnum = XWIDGET_PART_NUM(widget_id);

	switch (partnum) {
	case BRIDGE_WIDGET_PART_NUM:
	case XBRIDGE_WIDGET_PART_NUM:
		bridge_platform_create(nasid, widget, masterwid);
		break;
	default:
		pr_info("xtalk:n%d/%d unknown widget (0x%x)\n",
			nasid, widget, partnum);
		break;
	}

	return 0;
}

static int xbow_probe(nasid_t nasid)
{
	lboard_t *brd;
	klxbow_t *xbow_p;
	unsigned masterwid, i;

	/*
	 * found xbow, so may have multiple bridges
	 * need to probe xbow
	 */
	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_MIDPLANE8);
	if (!brd)
		return -ENODEV;

	xbow_p = (klxbow_t *)find_component(brd, NULL, KLSTRUCT_XBOW);
	if (!xbow_p)
		return -ENODEV;

	/*
	 * Okay, here's a xbow. Let's arbitrate and find
	 * out if we should initialize it. Set enabled
	 * hub connected at highest or lowest widget as
	 * master.
	 */
#ifdef WIDGET_A
	i = HUB_WIDGET_ID_MAX + 1;
	do {
		i--;
	} while ((!XBOW_PORT_TYPE_HUB(xbow_p, i)) ||
		 (!XBOW_PORT_IS_ENABLED(xbow_p, i)));
#else
	i = HUB_WIDGET_ID_MIN - 1;
	do {
		i++;
	} while ((!XBOW_PORT_TYPE_HUB(xbow_p, i)) ||
		 (!XBOW_PORT_IS_ENABLED(xbow_p, i)));
#endif

	masterwid = i;
	if (nasid != XBOW_PORT_NASID(xbow_p, i))
		return 1;

	for (i = HUB_WIDGET_ID_MIN; i <= HUB_WIDGET_ID_MAX; i++) {
		if (XBOW_PORT_IS_ENABLED(xbow_p, i) &&
		    XBOW_PORT_TYPE_IO(xbow_p, i))
			probe_one_port(nasid, i, masterwid);
	}

	return 0;
}

static void xtalk_probe_node(nasid_t nasid)
{
	volatile u64		hubreg;
	xwidget_part_num_t	partnum;
	widgetreg_t		widget_id;

	hubreg = REMOTE_HUB_L(nasid, IIO_LLP_CSR);

	/* check whether the link is up */
	if (!(hubreg & IIO_LLP_CSR_IS_UP))
		return;

	widget_id = *(volatile widgetreg_t *)
		       (RAW_NODE_SWIN_BASE(nasid, 0x0) + WIDGET_ID);
	partnum = XWIDGET_PART_NUM(widget_id);

	switch (partnum) {
	case BRIDGE_WIDGET_PART_NUM:
		bridge_platform_create(nasid, 0x8, 0xa);
		break;
	case XBOW_WIDGET_PART_NUM:
	case XXBOW_WIDGET_PART_NUM:
		pr_info("xtalk:n%d/0 xbow widget\n", nasid);
		xbow_probe(nasid);
		break;
	default:
		pr_info("xtalk:n%d/0 unknown widget (0x%x)\n", nasid, partnum);
		break;
	}
}

static int __init xtalk_init(void)
{
	nasid_t nasid;

	for_each_online_node(nasid)
		xtalk_probe_node(nasid);

	return 0;
}
arch_initcall(xtalk_init);
