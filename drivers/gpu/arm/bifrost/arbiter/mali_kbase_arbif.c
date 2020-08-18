// SPDX-License-Identifier: GPL-2.0

/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/**
 * @file mali_kbase_arbif.c
 * Mali arbiter interface APIs to share GPU between Virtual Machines
 */

#include <mali_kbase.h>
#include "mali_kbase_arbif.h"
#include <tl/mali_kbase_tracepoints.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include "mali_kbase_arbiter_interface.h"

static void on_gpu_stop(struct device *dev)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);

	KBASE_TLSTREAM_TL_EVENT_ARB_STOP_REQUESTED(kbdev, kbdev);
	kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_GPU_STOP_EVT);
}

static void on_gpu_granted(struct device *dev)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);

	KBASE_TLSTREAM_TL_EVENT_ARB_GRANTED(kbdev, kbdev);
	kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_GPU_GRANTED_EVT);
}

static void on_gpu_lost(struct device *dev)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);

	kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_GPU_LOST_EVT);
}

int kbase_arbif_init(struct kbase_device *kbdev)
{
#ifdef CONFIG_OF
	struct arbiter_if_arb_vm_ops ops;
	struct arbiter_if_dev *arb_if;
	struct device_node *arbiter_if_node;
	struct platform_device *pdev;
	int err;

	dev_dbg(kbdev->dev, "%s\n", __func__);

	arbiter_if_node = of_parse_phandle(kbdev->dev->of_node,
		"arbiter_if", 0);
	if (!arbiter_if_node) {
		dev_dbg(kbdev->dev, "No arbiter_if in Device Tree\n");
		/* no arbiter interface defined in device tree */
		kbdev->arb.arb_dev = NULL;
		kbdev->arb.arb_if = NULL;
		return 0;
	}

	pdev = of_find_device_by_node(arbiter_if_node);
	if (!pdev) {
		dev_err(kbdev->dev, "Failed to find arbiter_if device\n");
		return -EPROBE_DEFER;
	}

	if (!pdev->dev.driver || !try_module_get(pdev->dev.driver->owner)) {
		dev_err(kbdev->dev, "arbiter_if driver not available\n");
		return -EPROBE_DEFER;
	}
	kbdev->arb.arb_dev = &pdev->dev;
	arb_if = platform_get_drvdata(pdev);
	if (!arb_if) {
		dev_err(kbdev->dev, "arbiter_if driver not ready\n");
		module_put(pdev->dev.driver->owner);
		return -EPROBE_DEFER;
	}

	kbdev->arb.arb_if = arb_if;
	ops.arb_vm_gpu_stop = on_gpu_stop;
	ops.arb_vm_gpu_granted = on_gpu_granted;
	ops.arb_vm_gpu_lost = on_gpu_lost;

	/* register kbase arbiter_if callbacks */
	if (arb_if->vm_ops.vm_arb_register_dev) {
		err = arb_if->vm_ops.vm_arb_register_dev(arb_if,
			kbdev->dev, &ops);
		if (err) {
			dev_err(kbdev->dev, "Arbiter registration failed.\n");
			module_put(pdev->dev.driver->owner);
			return err;
		}
	}
#else /* CONFIG_OF */
	dev_dbg(kbdev->dev, "No arbiter without Device Tree support\n");
	kbdev->arb.arb_dev = NULL;
	kbdev->arb.arb_if = NULL;
#endif
	return 0;
}

void kbase_arbif_destroy(struct kbase_device *kbdev)
{
	struct arbiter_if_dev *arb_if = kbdev->arb.arb_if;

	if (arb_if && arb_if->vm_ops.vm_arb_unregister_dev) {
		dev_dbg(kbdev->dev, "%s\n", __func__);
		arb_if->vm_ops.vm_arb_unregister_dev(kbdev->arb.arb_if);
	}
	kbdev->arb.arb_if = NULL;
	if (kbdev->arb.arb_dev)
		module_put(kbdev->arb.arb_dev->driver->owner);
	kbdev->arb.arb_dev = NULL;
}

void kbase_arbif_gpu_request(struct kbase_device *kbdev)
{
	struct arbiter_if_dev *arb_if = kbdev->arb.arb_if;

	if (arb_if && arb_if->vm_ops.vm_arb_gpu_request) {
		dev_dbg(kbdev->dev, "%s\n", __func__);
		arb_if->vm_ops.vm_arb_gpu_request(arb_if);
	}
}

void kbase_arbif_gpu_stopped(struct kbase_device *kbdev, u8 gpu_required)
{
	struct arbiter_if_dev *arb_if = kbdev->arb.arb_if;

	if (arb_if && arb_if->vm_ops.vm_arb_gpu_stopped) {
		dev_dbg(kbdev->dev, "%s\n", __func__);
		KBASE_TLSTREAM_TL_EVENT_ARB_STOPPED(kbdev, kbdev);
		arb_if->vm_ops.vm_arb_gpu_stopped(arb_if, gpu_required);
	}
}

void kbase_arbif_gpu_active(struct kbase_device *kbdev)
{
	struct arbiter_if_dev *arb_if = kbdev->arb.arb_if;

	if (arb_if && arb_if->vm_ops.vm_arb_gpu_active) {
		dev_dbg(kbdev->dev, "%s\n", __func__);
		arb_if->vm_ops.vm_arb_gpu_active(arb_if);
	}
}

void kbase_arbif_gpu_idle(struct kbase_device *kbdev)
{
	struct arbiter_if_dev *arb_if = kbdev->arb.arb_if;

	if (arb_if && arb_if->vm_ops.vm_arb_gpu_idle) {
		dev_dbg(kbdev->dev, "vm_arb_gpu_idle\n");
		arb_if->vm_ops.vm_arb_gpu_idle(arb_if);
	}
}
