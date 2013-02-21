/******************************************************************************
 * pcpu.c
 * Management physical cpu in dom0, get pcpu info and provide sys interface
 *
 * Copyright (c) 2012 Intel Corporation
 * Author: Liu, Jinsong <jinsong.liu@intel.com>
 * Author: Jiang, Yunhong <yunhong.jiang@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>
#include <linux/stat.h>
#include <linux/capability.h>

#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/platform.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#define XEN_PCPU "xen_cpu: "

/*
 * @cpu_id: Xen physical cpu logic number
 * @flags: Xen physical cpu status flag
 * - XEN_PCPU_FLAGS_ONLINE: cpu is online
 * - XEN_PCPU_FLAGS_INVALID: cpu is not present
 */
struct pcpu {
	struct list_head list;
	struct device dev;
	uint32_t cpu_id;
	uint32_t flags;
};

static struct bus_type xen_pcpu_subsys = {
	.name = "xen_cpu",
	.dev_name = "xen_cpu",
};

static DEFINE_MUTEX(xen_pcpu_lock);

static LIST_HEAD(xen_pcpus);

static int xen_pcpu_down(uint32_t cpu_id)
{
	struct xen_platform_op op = {
		.cmd			= XENPF_cpu_offline,
		.interface_version	= XENPF_INTERFACE_VERSION,
		.u.cpu_ol.cpuid		= cpu_id,
	};

	return HYPERVISOR_dom0_op(&op);
}

static int xen_pcpu_up(uint32_t cpu_id)
{
	struct xen_platform_op op = {
		.cmd			= XENPF_cpu_online,
		.interface_version	= XENPF_INTERFACE_VERSION,
		.u.cpu_ol.cpuid		= cpu_id,
	};

	return HYPERVISOR_dom0_op(&op);
}

static ssize_t show_online(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct pcpu *cpu = container_of(dev, struct pcpu, dev);

	return sprintf(buf, "%u\n", !!(cpu->flags & XEN_PCPU_FLAGS_ONLINE));
}

static ssize_t __ref store_online(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct pcpu *pcpu = container_of(dev, struct pcpu, dev);
	unsigned long long val;
	ssize_t ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (kstrtoull(buf, 0, &val) < 0)
		return -EINVAL;

	switch (val) {
	case 0:
		ret = xen_pcpu_down(pcpu->cpu_id);
		break;
	case 1:
		ret = xen_pcpu_up(pcpu->cpu_id);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret >= 0)
		ret = count;
	return ret;
}
static DEVICE_ATTR(online, S_IRUGO | S_IWUSR, show_online, store_online);

static bool xen_pcpu_online(uint32_t flags)
{
	return !!(flags & XEN_PCPU_FLAGS_ONLINE);
}

static void pcpu_online_status(struct xenpf_pcpuinfo *info,
			       struct pcpu *pcpu)
{
	if (xen_pcpu_online(info->flags) &&
	   !xen_pcpu_online(pcpu->flags)) {
		/* the pcpu is onlined */
		pcpu->flags |= XEN_PCPU_FLAGS_ONLINE;
		kobject_uevent(&pcpu->dev.kobj, KOBJ_ONLINE);
	} else if (!xen_pcpu_online(info->flags) &&
		    xen_pcpu_online(pcpu->flags)) {
		/* The pcpu is offlined */
		pcpu->flags &= ~XEN_PCPU_FLAGS_ONLINE;
		kobject_uevent(&pcpu->dev.kobj, KOBJ_OFFLINE);
	}
}

static struct pcpu *get_pcpu(uint32_t cpu_id)
{
	struct pcpu *pcpu;

	list_for_each_entry(pcpu, &xen_pcpus, list) {
		if (pcpu->cpu_id == cpu_id)
			return pcpu;
	}

	return NULL;
}

static void pcpu_release(struct device *dev)
{
	struct pcpu *pcpu = container_of(dev, struct pcpu, dev);

	list_del(&pcpu->list);
	kfree(pcpu);
}

static void unregister_and_remove_pcpu(struct pcpu *pcpu)
{
	struct device *dev;

	if (!pcpu)
		return;

	dev = &pcpu->dev;
	if (dev->id)
		device_remove_file(dev, &dev_attr_online);

	/* pcpu remove would be implicitly done */
	device_unregister(dev);
}

static int register_pcpu(struct pcpu *pcpu)
{
	struct device *dev;
	int err = -EINVAL;

	if (!pcpu)
		return err;

	dev = &pcpu->dev;
	dev->bus = &xen_pcpu_subsys;
	dev->id = pcpu->cpu_id;
	dev->release = pcpu_release;

	err = device_register(dev);
	if (err) {
		pcpu_release(dev);
		return err;
	}

	/*
	 * Xen never offline cpu0 due to several restrictions
	 * and assumptions. This basically doesn't add a sys control
	 * to user, one cannot attempt to offline BSP.
	 */
	if (dev->id) {
		err = device_create_file(dev, &dev_attr_online);
		if (err) {
			device_unregister(dev);
			return err;
		}
	}

	return 0;
}

static struct pcpu *create_and_register_pcpu(struct xenpf_pcpuinfo *info)
{
	struct pcpu *pcpu;
	int err;

	if (info->flags & XEN_PCPU_FLAGS_INVALID)
		return ERR_PTR(-ENODEV);

	pcpu = kzalloc(sizeof(struct pcpu), GFP_KERNEL);
	if (!pcpu)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&pcpu->list);
	pcpu->cpu_id = info->xen_cpuid;
	pcpu->flags = info->flags;

	/* Need hold on xen_pcpu_lock before pcpu list manipulations */
	list_add_tail(&pcpu->list, &xen_pcpus);

	err = register_pcpu(pcpu);
	if (err) {
		pr_warning(XEN_PCPU "Failed to register pcpu%u\n",
			   info->xen_cpuid);
		return ERR_PTR(-ENOENT);
	}

	return pcpu;
}

/*
 * Caller should hold the xen_pcpu_lock
 */
static int sync_pcpu(uint32_t cpu, uint32_t *max_cpu)
{
	int ret;
	struct pcpu *pcpu = NULL;
	struct xenpf_pcpuinfo *info;
	struct xen_platform_op op = {
		.cmd                   = XENPF_get_cpuinfo,
		.interface_version     = XENPF_INTERFACE_VERSION,
		.u.pcpu_info.xen_cpuid = cpu,
	};

	ret = HYPERVISOR_dom0_op(&op);
	if (ret)
		return ret;

	info = &op.u.pcpu_info;
	if (max_cpu)
		*max_cpu = info->max_present;

	pcpu = get_pcpu(cpu);

	/*
	 * Only those at cpu present map has its sys interface.
	 */
	if (info->flags & XEN_PCPU_FLAGS_INVALID) {
		unregister_and_remove_pcpu(pcpu);
		return 0;
	}

	if (!pcpu) {
		pcpu = create_and_register_pcpu(info);
		if (IS_ERR_OR_NULL(pcpu))
			return -ENODEV;
	} else
		pcpu_online_status(info, pcpu);

	return 0;
}

/*
 * Sync dom0's pcpu information with xen hypervisor's
 */
static int xen_sync_pcpus(void)
{
	/*
	 * Boot cpu always have cpu_id 0 in xen
	 */
	uint32_t cpu = 0, max_cpu = 0;
	int err = 0;
	struct pcpu *pcpu, *tmp;

	mutex_lock(&xen_pcpu_lock);

	while (!err && (cpu <= max_cpu)) {
		err = sync_pcpu(cpu, &max_cpu);
		cpu++;
	}

	if (err)
		list_for_each_entry_safe(pcpu, tmp, &xen_pcpus, list)
			unregister_and_remove_pcpu(pcpu);

	mutex_unlock(&xen_pcpu_lock);

	return err;
}

static void xen_pcpu_work_fn(struct work_struct *work)
{
	xen_sync_pcpus();
}
static DECLARE_WORK(xen_pcpu_work, xen_pcpu_work_fn);

static irqreturn_t xen_pcpu_interrupt(int irq, void *dev_id)
{
	schedule_work(&xen_pcpu_work);
	return IRQ_HANDLED;
}

static int __init xen_pcpu_init(void)
{
	int irq, ret;

	if (!xen_initial_domain())
		return -ENODEV;

	irq = bind_virq_to_irqhandler(VIRQ_PCPU_STATE, 0,
				      xen_pcpu_interrupt, 0,
				      "xen-pcpu", NULL);
	if (irq < 0) {
		pr_warning(XEN_PCPU "Failed to bind pcpu virq\n");
		return irq;
	}

	ret = subsys_system_register(&xen_pcpu_subsys, NULL);
	if (ret) {
		pr_warning(XEN_PCPU "Failed to register pcpu subsys\n");
		goto err1;
	}

	ret = xen_sync_pcpus();
	if (ret) {
		pr_warning(XEN_PCPU "Failed to sync pcpu info\n");
		goto err2;
	}

	return 0;

err2:
	bus_unregister(&xen_pcpu_subsys);
err1:
	unbind_from_irqhandler(irq, NULL);
	return ret;
}
arch_initcall(xen_pcpu_init);
