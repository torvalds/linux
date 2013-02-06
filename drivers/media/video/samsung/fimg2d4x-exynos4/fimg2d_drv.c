/* linux/drivers/media/video/samsung/fimg2d4x/fimg2d_drv.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Samsung Graphics 2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <asm/cacheflush.h>
#include <plat/cpu.h>
#include <plat/fimg2d.h>
#include <plat/s5p-sysmmu.h>
#include <mach/dev.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include "fimg2d.h"
#include "fimg2d_clk.h"
#include "fimg2d_ctx.h"
#include "fimg2d_helper.h"
#include "fimg2d_cache.h"

#define CTX_TIMEOUT	msecs_to_jiffies(1000)
#define LV1_SHIFT		20
#define LV2_BASE_MASK		0x3ff
#define LV2_PT_MASK		0xff000
#define LV2_SHIFT		12
#define LV1_DESC_MASK		0x3
#define LV2_VALUE_META		0xc7f
#define LV2_VALUE_BASE_MASK	0xfff

static struct fimg2d_control *info;

static void fimg2d_worker(struct work_struct *work)
{
	fimg2d_debug("start kernel thread\n");
	info->blit(info);
}

static DECLARE_WORK(fimg2d_work, fimg2d_worker);

/**
 * @irq: irq number
 * @dev_id: pointer to private data
 */
static irqreturn_t fimg2d_irq(int irq, void *dev_id)
{
	fimg2d_debug("irq\n");
	if (!atomic_read(&info->clkon)) {
		fimg2d_clk_on(info);
		info->stop(info);
		fimg2d_clk_off(info);
	} else {
		info->stop(info);
	}

	return IRQ_HANDLED;
}

static int fimg2d_sysmmu_fault_handler(enum S5P_SYSMMU_INTERRUPT_TYPE itype,
		unsigned long pgtable_base, unsigned long fault_addr)
{
	struct fimg2d_bltcmd *cmd;
	unsigned long *pgd;
	unsigned long *lv1d, *lv2d;

	if (itype == SYSMMU_PAGEFAULT) {
		printk(KERN_ERR "[%s] sysmmu page fault(0x%lx), pgd(0x%lx)\n",
				__func__, fault_addr, pgtable_base);
	} else {
		printk(KERN_ERR "[%s] sysmmu interrupt "
				"type(%d) pgd(0x%lx) addr(0x%lx)\n",
				__func__, itype, pgtable_base, fault_addr);
	}

	cmd = fimg2d_get_first_command(info);
	if (!cmd) {
		printk(KERN_ERR "[%s] null command\n", __func__);
		goto next;
	}

	if (cmd->ctx->mm->pgd != phys_to_virt(pgtable_base)) {
		printk(KERN_ERR "[%s] pgtable base is different from current command\n",
				__func__);
		goto next;
	}

	fimg2d_dump_command(cmd);

	pgd = (unsigned long *)cmd->ctx->mm->pgd;
	lv1d = pgd + (fault_addr >> LV1_SHIFT);
	printk(KERN_ERR " Level 1 descriptor(0x%lx)\n", *lv1d);
	if ((*lv1d & LV1_DESC_MASK) != 0x1) {
		fimg2d_clean_outer_pagetable(cmd->ctx->mm, fault_addr, 4);
		goto next;
	}

	lv2d = (unsigned long *)phys_to_virt(*lv1d & ~LV2_BASE_MASK) +
			((fault_addr & LV2_PT_MASK) >> LV2_SHIFT);
	printk(KERN_ERR " Level 2 descriptor(0x%lx)\n", *lv2d);
	if (*lv2d == 0) {
		fimg2d_mmutable_value_replace(cmd, fault_addr,
			(info->dbuffer_addr & ~LV2_VALUE_BASE_MASK) | LV2_VALUE_META);
		info->fault_addr = fault_addr;
	} else
		fimg2d_clean_outer_pagetable(cmd->ctx->mm, fault_addr, 4);
next:

	return 0;
}

static void fimg2d_context_wait(struct fimg2d_context *ctx)
{
	while (atomic_read(&ctx->ncmd)) {
		if (!wait_event_timeout(ctx->wait_q, !atomic_read(&ctx->ncmd), CTX_TIMEOUT)) {
			atomic_set(&info->active, 1);
			queue_work(info->work_q, &fimg2d_work);
			printk(KERN_ERR "[%s] ctx %p cmd wait timeout\n", __func__, ctx);
		}
	}
}

static void fimg2d_request_bitblt(struct fimg2d_context *ctx)
{
	spin_lock(&info->bltlock);
	if (!atomic_read(&info->active)) {
		atomic_set(&info->active, 1);
		fimg2d_debug("dispatch ctx %p to kernel thread\n", ctx);
		queue_work(info->work_q, &fimg2d_work);
	}
	spin_unlock(&info->bltlock);
	fimg2d_context_wait(ctx);
}

static int fimg2d_open(struct inode *inode, struct file *file)
{
	struct fimg2d_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		printk(KERN_ERR "[%s] not enough memory for ctx\n", __func__);
		return -ENOMEM;
	}
	file->private_data = (void *)ctx;

	ctx->mm = current->mm;
	fimg2d_debug("ctx %p current pgd %p init_mm pgd %p\n",
			ctx, (unsigned long *)ctx->mm->pgd,
			(unsigned long *)init_mm.pgd);

	ctx->pgd_clone = kzalloc(L1_DESCRIPTOR_SIZE, GFP_KERNEL);

	fimg2d_add_context(info, ctx);
	return 0;
}

static int fimg2d_release(struct inode *inode, struct file *file)
{
	struct fimg2d_context *ctx = file->private_data;

	fimg2d_debug("ctx %p\n", ctx);
	while (1) {
		if (!atomic_read(&ctx->ncmd))
			break;

		mdelay(2);
	}
	fimg2d_del_context(info, ctx);

	kfree(ctx->pgd_clone);
	kfree(ctx);
	return 0;
}

static int fimg2d_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static unsigned int fimg2d_poll(struct file *file, struct poll_table_struct *wait)
{
	return 0;
}

static long fimg2d_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct fimg2d_context *ctx;
	struct fimg2d_platdata *pdata;
	struct fimg2d_blit blit;
	struct fimg2d_version ver;
	struct fimg2d_image dst;

	ctx = file->private_data;
	if (!ctx) {
		printk(KERN_ERR "[%s] missing ctx\n", __func__);
		return -EFAULT;
	}

	switch (cmd) {
	case FIMG2D_BITBLT_BLIT:
		if (info->secure)
			return -EFAULT;

		if (copy_from_user(&blit, (void *)arg, sizeof(blit)))
			return -EFAULT;
		if (blit.dst)
			if (copy_from_user(&dst, (void *)blit.dst, sizeof(dst)))
				return -EFAULT;

#ifdef CONFIG_BUSFREQ_OPP
#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
			dev_lock(info->bus_dev, info->dev, 160160);
#endif
#endif
		if ((blit.dst) && (dst.addr.type == ADDR_USER)
				&& (blit.seq_no == SEQ_NO_BLT_SKIA))
			if (!down_write_trylock(&page_alloc_slow_rwsem))
				ret = -EAGAIN;

		if (ret != -EAGAIN)
			ret = fimg2d_add_command(info, ctx, &blit, dst.addr.type);

		if (!ret) {
			fimg2d_request_bitblt(ctx);
		}

#ifdef PERF_PROFILE
		perf_print(ctx, blit.seq_no);
		perf_clear(ctx);
#endif
		if ((blit.dst) && (dst.addr.type == ADDR_USER)
				&& (blit.seq_no == SEQ_NO_BLT_SKIA)
				&& ret != -EAGAIN)
			up_write(&page_alloc_slow_rwsem);

#ifdef CONFIG_BUSFREQ_OPP
#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
			dev_unlock(info->bus_dev, info->dev);
#endif
#endif

		if (info->fault_addr) {
			printk(KERN_INFO "Return by G2D fault handler");
			info->fault_addr = 0;
			ret = -EFAULT;
		}

		break;

	case FIMG2D_BITBLT_SYNC:
		fimg2d_debug("FIMG2D_BITBLT_SYNC ctx: %p\n", ctx);
		/* FIXME: */
		break;

	case FIMG2D_BITBLT_VERSION:
		pdata = to_fimg2d_plat(info->dev);
		ver.hw = pdata->hw_ver;
		ver.sw = 0;
		fimg2d_debug("fimg2d version, hw: 0x%x sw: 0x%x\n",
				ver.hw, ver.sw);
		if (copy_to_user((void *)arg, &ver, sizeof(ver)))
			return -EFAULT;
		break;

	case FIMG2D_BITBLT_SECURE:
		if (copy_from_user(&info->secure,
				   (unsigned int *)arg,
				   sizeof(unsigned int))) {
			printk(KERN_ERR
				"[%s] failed to FIMG2D_BITBLT_SECURE: copy_from_user error\n\n",
				__func__);
			return -EFAULT;
		}

		while (1) {
			if (fimg2d_queue_is_empty(&info->cmd_q))
				break;
			mdelay(2);
		}

		break;

	case FIMG2D_BITBLT_DBUFFER:
		if (copy_from_user(&info->dbuffer_addr,
				(unsigned long *)arg,
				sizeof(unsigned long))) {
			printk(KERN_ERR
				"[%s] failed to FIMG2D_BITBLT_DBUFFER: copy_from_user error\n\n",
				__func__);
			return -EFAULT;
		}
		break;

	default:
		printk(KERN_ERR "[%s] unknown ioctl\n", __func__);
		ret = -EFAULT;
		break;
	}

	return ret;
}

/* fops */
static const struct file_operations fimg2d_fops = {
	.owner          = THIS_MODULE,
	.open           = fimg2d_open,
	.release        = fimg2d_release,
	.mmap           = fimg2d_mmap,
	.poll           = fimg2d_poll,
	.unlocked_ioctl = fimg2d_ioctl,
};

/* miscdev */
static struct miscdevice fimg2d_dev = {
	.minor		= FIMG2D_MINOR,
	.name		= "fimg2d",
	.fops		= &fimg2d_fops,
};

static int fimg2d_setup_controller(struct fimg2d_control *info)
{
	atomic_set(&info->suspended, 0);
	atomic_set(&info->clkon, 0);
	atomic_set(&info->busy, 0);
	atomic_set(&info->nctx, 0);
	atomic_set(&info->active, 0);
	info->secure = 0;
	info->fault_addr = 0;

	spin_lock_init(&info->bltlock);

	INIT_LIST_HEAD(&info->cmd_q);
	init_waitqueue_head(&info->wait_q);
	fimg2d_register_ops(info);

	info->work_q = create_singlethread_workqueue("kfimg2dd");
	if (!info->work_q)
		return -ENOMEM;

	return 0;
}

static int fimg2d_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct fimg2d_platdata *pdata;
	int ret;

	pdata = to_fimg2d_plat(&pdev->dev);
	if (!pdata) {
		printk(KERN_ERR "FIMG2D failed to get platform data\n");
		ret = -ENOMEM;
		goto err_plat;
	}

	/* global structure */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		printk(KERN_ERR "FIMG2D failed to allocate memory for controller\n");
		ret = -ENOMEM;
		goto err_plat;
	}

	/* setup global info */
	ret = fimg2d_setup_controller(info);
	if (ret) {
		printk(KERN_ERR "FIMG2D failed to setup controller\n");
		goto err_setup;
	}
	info->dev = &pdev->dev;

	/* memory region */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		printk(KERN_ERR "FIMG2D failed to get resource\n");
		ret = -ENOENT;
		goto err_res;
	}

	info->mem = request_mem_region(res->start, resource_size(res),
					pdev->name);
	if (!info->mem) {
		printk(KERN_ERR "FIMG2D failed to request memory region\n");
		ret = -ENOMEM;
		goto err_region;
	}

	/* ioremap */
	info->regs = ioremap(res->start, resource_size(res));
	if (!info->regs) {
		printk(KERN_ERR "FIMG2D failed to ioremap for SFR\n");
		ret = -ENOENT;
		goto err_map;
	}
	fimg2d_debug("device name: %s base address: 0x%lx\n",
			pdev->name, (unsigned long)res->start);

	/* Clock setup */
	ret = fimg2d_clk_setup(info);
	if (ret) {
		printk(KERN_ERR "FIMG2D failed to setup clk\n");
		ret = -ENOENT;
		goto err_clk;
	}

	/* irq */
	info->irq = platform_get_irq(pdev, 0);
	if (!info->irq) {
		printk(KERN_ERR "FIMG2D failed to get irq resource\n");
		ret = -ENOENT;
		goto err_irq;
	}
	fimg2d_debug("irq: %d\n", info->irq);

	ret = request_irq(info->irq, fimg2d_irq, IRQF_DISABLED, pdev->name, info);
	if (ret) {
		printk(KERN_ERR "FIMG2D failed to request irq\n");
		ret = -ENOENT;
		goto err_irq;
	}

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(info->dev);
	fimg2d_debug("enable runtime pm\n");
#endif

#ifdef CONFIG_BUSFREQ_OPP
#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
	/* To lock bus frequency in OPP mode */
	info->bus_dev = dev_get("exynos-busfreq");
#endif
#endif
	s5p_sysmmu_set_fault_handler(info->dev, fimg2d_sysmmu_fault_handler);
	fimg2d_debug("register sysmmu page fault handler\n");

	/* misc register */
	ret = misc_register(&fimg2d_dev);
	if (ret) {
		printk(KERN_ERR "FIMG2D failed to register misc driver\n");
		goto err_reg;
	}

	printk(KERN_INFO "Samsung Graphics 2D driver, (c) 2011 Samsung Electronics\n");
	return 0;

err_reg:
	free_irq(info->irq, NULL);

err_irq:
	fimg2d_clk_release(info);

err_clk:
	iounmap(info->regs);

err_map:
	release_mem_region(res->start, resource_size(res));
	kfree(info->mem);

err_region:
	release_resource(res);

err_res:
	destroy_workqueue(info->work_q);

err_setup:
	kfree(info);

err_plat:
	return ret;
}

static int fimg2d_remove(struct platform_device *pdev)
{
	free_irq(info->irq, NULL);

	if (info->mem) {
		iounmap(info->regs);
		release_resource(info->mem);
		kfree(info->mem);
	}

	destroy_workqueue(info->work_q);
	misc_deregister(&fimg2d_dev);
	kfree(info);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&pdev->dev);
	fimg2d_debug("disable runtime pm\n");
#endif

	return 0;
}

static int fimg2d_suspend(struct device *dev)
{
	fimg2d_debug("suspend... start\n");
	atomic_set(&info->suspended, 1);
	while (1) {
		if (fimg2d_queue_is_empty(&info->cmd_q))
			break;

		mdelay(2);
	}
	fimg2d_debug("suspend... done\n");
	return 0;
}

static int fimg2d_resume(struct device *dev)
{
	fimg2d_debug("resume... start\n");
	atomic_set(&info->suspended, 0);
	fimg2d_debug("resume... done\n");
	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int fimg2d_runtime_suspend(struct device *dev)
{
	fimg2d_debug("runtime suspend... done\n");
	return 0;
}

static int fimg2d_runtime_resume(struct device *dev)
{
	fimg2d_debug("runtime resume... done\n");
	return 0;
}
#endif

static const struct dev_pm_ops fimg2d_pm_ops = {
	.suspend		= fimg2d_suspend,
	.resume			= fimg2d_resume,
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend	= fimg2d_runtime_suspend,
	.runtime_resume		= fimg2d_runtime_resume,
#endif
};

static struct platform_driver fimg2d_driver = {
	.probe		= fimg2d_probe,
	.remove		= fimg2d_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "s5p-fimg2d",
		.pm     = &fimg2d_pm_ops,
	},
};

static int __init fimg2d_register(void)
{
	return platform_driver_register(&fimg2d_driver);
}

static void __exit fimg2d_unregister(void)
{
	platform_driver_unregister(&fimg2d_driver);
}

module_init(fimg2d_register);
module_exit(fimg2d_unregister);

MODULE_AUTHOR("Eunseok Choi <es10.choi@samsung.com>");
MODULE_AUTHOR("Jinsung Yang <jsgood.yang@samsung.com>");
MODULE_DESCRIPTION("Samsung Graphics 2D driver");
MODULE_LICENSE("GPL");
