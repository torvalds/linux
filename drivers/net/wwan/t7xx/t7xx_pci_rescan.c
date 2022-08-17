// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":t7xx:%s: " fmt, __func__
#define dev_fmt(fmt) "t7xx: " fmt

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "t7xx_pci.h"
#include "t7xx_pci_rescan.h"

static struct remove_rescan_context g_mtk_rescan_context;

void t7xx_pci_dev_rescan(void)
{
	struct pci_bus *b = NULL;

	pci_lock_rescan_remove();
	while ((b = pci_find_next_bus(b)))
		pci_rescan_bus(b);

	pci_unlock_rescan_remove();
}

void t7xx_rescan_done(void)
{
	unsigned long flags;

	spin_lock_irqsave(&g_mtk_rescan_context.dev_lock, flags);
	if (g_mtk_rescan_context.rescan_done == 0) {
		pr_debug("this is a rescan probe\n");
		g_mtk_rescan_context.rescan_done = 1;
	} else {
		pr_debug("this is a init probe\n");
	}
	spin_unlock_irqrestore(&g_mtk_rescan_context.dev_lock, flags);
}

static void t7xx_remove_rescan(struct work_struct *work)
{
	struct pci_dev *pdev;
	int num_retries = RESCAN_RETRIES;
	unsigned long flags;

	spin_lock_irqsave(&g_mtk_rescan_context.dev_lock, flags);
	g_mtk_rescan_context.rescan_done = 0;
	pdev = g_mtk_rescan_context.dev;
	spin_unlock_irqrestore(&g_mtk_rescan_context.dev_lock, flags);

	if (pdev) {
		pci_stop_and_remove_bus_device_locked(pdev);
		pr_debug("start remove and rescan flow\n");
	}

	do {
		t7xx_pci_dev_rescan();
		spin_lock_irqsave(&g_mtk_rescan_context.dev_lock, flags);
		if (g_mtk_rescan_context.rescan_done) {
			spin_unlock_irqrestore(&g_mtk_rescan_context.dev_lock, flags);
			break;
		}

		spin_unlock_irqrestore(&g_mtk_rescan_context.dev_lock, flags);
		msleep(DELAY_RESCAN_MTIME);
	} while (num_retries--);
}

void t7xx_rescan_queue_work(struct pci_dev *pdev)
{
	unsigned long flags;

	dev_info(&pdev->dev, "start queue_mtk_rescan_work\n");
	spin_lock_irqsave(&g_mtk_rescan_context.dev_lock, flags);
	if (!g_mtk_rescan_context.rescan_done) {
		dev_err(&pdev->dev, "rescan failed because last rescan undone\n");
		spin_unlock_irqrestore(&g_mtk_rescan_context.dev_lock, flags);
		return;
	}

	g_mtk_rescan_context.dev = pdev;
	spin_unlock_irqrestore(&g_mtk_rescan_context.dev_lock, flags);
	queue_work(g_mtk_rescan_context.pcie_rescan_wq, &g_mtk_rescan_context.service_task);
}

int t7xx_rescan_init(void)
{
	spin_lock_init(&g_mtk_rescan_context.dev_lock);
	g_mtk_rescan_context.rescan_done = 1;
	g_mtk_rescan_context.dev = NULL;
	g_mtk_rescan_context.pcie_rescan_wq = create_singlethread_workqueue(MTK_RESCAN_WQ);
	if (!g_mtk_rescan_context.pcie_rescan_wq) {
		pr_err("Failed to create workqueue: %s\n", MTK_RESCAN_WQ);
		return -ENOMEM;
	}

	INIT_WORK(&g_mtk_rescan_context.service_task, t7xx_remove_rescan);

	return 0;
}

void t7xx_rescan_deinit(void)
{
	unsigned long flags;

	spin_lock_irqsave(&g_mtk_rescan_context.dev_lock, flags);
	g_mtk_rescan_context.rescan_done = 0;
	g_mtk_rescan_context.dev = NULL;
	spin_unlock_irqrestore(&g_mtk_rescan_context.dev_lock, flags);
	cancel_work_sync(&g_mtk_rescan_context.service_task);
	destroy_workqueue(g_mtk_rescan_context.pcie_rescan_wq);
}
