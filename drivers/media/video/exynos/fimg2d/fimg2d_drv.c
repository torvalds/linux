/* linux/drivers/media/video/exynos/fimg2d/fimg2d_drv.c
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
#include <plat/sysmmu.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include "fimg2d.h"
#include "fimg2d_clk.h"
#include "fimg2d_ctx.h"
#include "fimg2d_helper.h"

#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
struct pm_qos_request exynos5_g2d_cpu_qos;
#endif
#ifdef CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ
struct pm_qos_request exynos5_g2d_mif_qos;
#endif

#define POLL_TIMEOUT	2	/* 2 msec */
#define POLL_RETRY	1000
#define CTX_TIMEOUT	msecs_to_jiffies(10000)	/* 10 sec */

#ifdef DEBUG
int g2d_debug = DBG_INFO;
module_param(g2d_debug, int, S_IRUGO | S_IWUSR);
#endif

static struct fimg2d_control *ctrl;

static int fimg2d_do_bitblt(struct fimg2d_control *ctrl)
{
	int ret;

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_sync(ctrl->dev);
	fimg2d_debug("pm_runtime_get_sync\n");
	fimg2d_clk_on(ctrl);
#endif

	ret = ctrl->blit(ctrl);

#ifdef CONFIG_PM_RUNTIME
	fimg2d_clk_off(ctrl);
	pm_runtime_put_sync(ctrl->dev);
	fimg2d_debug("pm_runtime_put_sync\n");
#endif

	return ret;
}

#ifdef BLIT_WORKQUE
static void fimg2d_worker(struct work_struct *work)
{
	fimg2d_debug("start kernel thread\n");
	fimg2d_do_bitblt(ctrl);
}
static DECLARE_WORK(fimg2d_work, fimg2d_worker);

static int fimg2d_context_wait(struct fimg2d_context *ctx)
{
	int ret;

	ret = wait_event_timeout(ctx->wait_q, !atomic_read(&ctx->ncmd),
			CTX_TIMEOUT);
	if (!ret) {
		fimg2d_err("ctx %p wait timeout\n", ctx);
		return -ETIME;
	}
	return 0;
}
#endif

static irqreturn_t fimg2d_irq(int irq, void *dev_id)
{
	fimg2d_debug("irq\n");
	ctrl->stop(ctrl);

	return IRQ_HANDLED;
}

static int fimg2d_sysmmu_fault_handler(struct device *dev, const char *mmuname,
		enum exynos_sysmmu_inttype itype,
		unsigned long pgtable_base, unsigned long fault_addr)
{
	struct fimg2d_bltcmd *cmd;

	if (itype == SYSMMU_PAGEFAULT) {
		fimg2d_err("sysmmu page fault(0x%lx), pgd(0x%lx)\n",
				fault_addr, pgtable_base);
	} else {
		fimg2d_err("sysmmu fault type(%d) pgd(0x%lx) addr(0x%lx)\n",
				itype, pgtable_base, fault_addr);
	}

	cmd = fimg2d_get_command(ctrl);
	if (WARN_ON(!cmd))
		goto next;

	if (cmd->ctx->mm->pgd != phys_to_virt(pgtable_base)) {
		fimg2d_err("pgtable base invalid\n");
		goto next;
	}

	fimg2d_dump_command(cmd);

next:
	ctrl->dump(ctrl);

	BUG();
	return 0;
}

static int fimg2d_request_bitblt(struct fimg2d_control *ctrl,
		struct fimg2d_context *ctx)
{
#ifdef BLIT_WORKQUE
	unsigned long flags;

	g2d_spin_lock(&ctrl->bltlock, flags);
	fimg2d_debug("dispatch ctx %p to kernel thread\n", ctx);
	queue_work(ctrl->work_q, &fimg2d_work);
	g2d_spin_unlock(&ctrl->bltlock, flags);
	return fimg2d_context_wait(ctx);
#else
	return fimg2d_do_bitblt(ctrl);
#endif
}

static int fimg2d_open(struct inode *inode, struct file *file)
{
	struct fimg2d_context *ctx;
	unsigned long flags, count;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		fimg2d_err("not enough memory for ctx\n");
		return -ENOMEM;
	}
	file->private_data = (void *)ctx;

	g2d_spin_lock(&ctrl->bltlock, flags);
	fimg2d_add_context(ctrl, ctx);
	count = atomic_read(&ctrl->nctx);
	g2d_spin_unlock(&ctrl->bltlock, flags);

	if (count == 1) {
#ifdef CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ
		/* mif lock : 800MHz */
		pm_qos_update_request(&exynos5_g2d_mif_qos, 800000);
#endif
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		/* cpu lock : KFC 800MHz */
		pm_qos_update_request(&exynos5_g2d_cpu_qos, 400000);
#endif
	} else {
#ifdef CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ
		fimg2d_debug("count:%ld, pm_qos_update_request(exynos5_g2d_mif_qos) is already called\n", count);
#endif
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		fimg2d_debug("count:%ld, pm_qos_update_request(exynos5_g2d_cpu_qos) is already called\n", count);
#endif
	}
	return 0;
}

static int fimg2d_release(struct inode *inode, struct file *file)
{
	struct fimg2d_context *ctx = file->private_data;
	int retry = POLL_RETRY;
	unsigned long flags, count;

	fimg2d_debug("ctx %p\n", ctx);
	while (retry--) {
		if (!atomic_read(&ctx->ncmd))
			break;
		mdelay(POLL_TIMEOUT);
	}
	g2d_spin_lock(&ctrl->bltlock, flags);
	fimg2d_del_context(ctrl, ctx);
	count = atomic_read(&ctrl->nctx);
	g2d_spin_unlock(&ctrl->bltlock, flags);

	if (!count) {
#ifdef CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ
		/* mif unlock */
		pm_qos_update_request(&exynos5_g2d_mif_qos, 0);
#endif
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		pm_qos_update_request(&exynos5_g2d_cpu_qos, 0);
#endif
	} else {
#ifdef CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ
		fimg2d_debug("count:%ld, pm_qos_update_request(exynos5_g2d_mif_qos) is not called yet\n", count);
#endif
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		fimg2d_debug("count:%ld, pm_qos_update_request(exynos5_g2d_cpu_qos) is not called yet\n", count);
#endif
	}
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
	struct mm_struct *mm;

	ctx = file->private_data;

	switch (cmd) {
	case FIMG2D_BITBLT_BLIT:

		mm = get_task_mm(current);
		if(!mm) {
			fimg2d_err("no mm for ctx\n");
			return -ENXIO;
		}

		g2d_lock(&ctrl->drvlock);

		ctx->mm = mm;

		if (atomic_read(&ctrl->drvact) ||
				atomic_read(&ctrl->suspended)) {
			fimg2d_err("driver is unavailable, do sw fallback\n");
			g2d_unlock(&ctrl->drvlock);
			mmput(mm);
			return -EPERM;
		}

		ret = fimg2d_add_command(ctrl, ctx, (struct fimg2d_blit __user *)arg);
		if (ret) {
			fimg2d_err("add command not allowed.\n");
			g2d_unlock(&ctrl->drvlock);
			mmput(mm);
			return ret;
		}

		ret = fimg2d_request_bitblt(ctrl, ctx);
		if (ret) {
			fimg2d_err("request bitblit not allowed.\n");
			g2d_unlock(&ctrl->drvlock);
			mmput(mm);
			return -EBUSY;
		}

		g2d_unlock(&ctrl->drvlock);
		mmput(mm);
		break;

	case FIMG2D_BITBLT_VERSION:
	{
		struct fimg2d_version ver;
		struct fimg2d_platdata *pdata;

		pdata = to_fimg2d_plat(ctrl->dev);
		ver.hw = pdata->hw_ver;
		ver.sw = 0;
		fimg2d_info("version info. hw(0x%x), sw(0x%x)\n",
				ver.hw, ver.sw);
		if (copy_to_user((void *)arg, &ver, sizeof(ver)))
			return -EFAULT;
		break;
	}
	case FIMG2D_BITBLT_ACTIVATE:
	{
		enum driver_act act;

		if (copy_from_user(&act, (void *)arg, sizeof(act)))
			return -EFAULT;

		g2d_lock(&ctrl->drvlock);
		atomic_set(&ctrl->drvact, act);
		if (act == DRV_ACT)
			fimg2d_info("fimg2d driver is activated\n");
		else
			fimg2d_info("fimg2d driver is deactivated\n");
		g2d_unlock(&ctrl->drvlock);
		break;
	}
	default:
		fimg2d_err("unknown ioctl\n");
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

static int fimg2d_setup_controller(struct fimg2d_control *ctrl)
{
	atomic_set(&ctrl->drvact, DRV_ACT);
	atomic_set(&ctrl->suspended, 0);
	atomic_set(&ctrl->clkon, 0);
	atomic_set(&ctrl->busy, 0);
	atomic_set(&ctrl->nctx, 0);

	spin_lock_init(&ctrl->bltlock);
	mutex_init(&ctrl->drvlock);

	INIT_LIST_HEAD(&ctrl->cmd_q);
	init_waitqueue_head(&ctrl->wait_q);
	fimg2d_register_ops(ctrl);

#ifdef BLIT_WORKQUE
	ctrl->work_q = create_singlethread_workqueue("kfimg2dd");
	if (!ctrl->work_q)
		return -ENOMEM;
#endif

	return 0;
}

static int fimg2d_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;

	if (!to_fimg2d_plat(&pdev->dev)) {
		fimg2d_err("failed to get platform data\n");
		return -ENOMEM;
	}

	/* global structure */
	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl) {
		fimg2d_err("failed to allocate memory for controller\n");
		return -ENOMEM;
	}

	/* setup global ctrl */
	ret = fimg2d_setup_controller(ctrl);
	if (ret) {
		fimg2d_err("failed to setup controller\n");
		goto drv_free;
	}
	ctrl->dev = &pdev->dev;

	/* memory region */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		fimg2d_err("failed to get resource\n");
		ret = -ENOENT;
		goto drv_free;
	}

	ctrl->mem = request_mem_region(res->start, resource_size(res),
					pdev->name);
	if (!ctrl->mem) {
		fimg2d_err("failed to request memory region\n");
		ret = -ENOMEM;
		goto res_free;
	}

	/* ioremap */
	ctrl->regs = ioremap(res->start, resource_size(res));
	if (!ctrl->regs) {
		fimg2d_err("failed to ioremap for SFR\n");
		ret = -ENOENT;
		goto mem_free;
	}
	fimg2d_info("base address: 0x%lx\n", (unsigned long)res->start);

	/* irq */
	ctrl->irq = platform_get_irq(pdev, 0);
	if (!ctrl->irq) {
		fimg2d_err("failed to get irq resource\n");
		ret = -ENOENT;
		goto reg_unmap;
	}
	fimg2d_info("irq: %d\n", ctrl->irq);

	ret = request_irq(ctrl->irq, fimg2d_irq, IRQF_DISABLED,
			pdev->name, ctrl);
	if (ret) {
		fimg2d_err("failed to request irq\n");
		ret = -ENOENT;
		goto reg_unmap;
	}

	ret = fimg2d_clk_setup(ctrl);
	if (ret) {
		fimg2d_err("failed to setup clk\n");
		ret = -ENOENT;
		goto irq_free;
	}

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(ctrl->dev);
	fimg2d_info("enable runtime pm\n");
#else
	fimg2d_clk_on(ctrl);
#endif

	exynos_sysmmu_set_fault_handler(ctrl->dev, fimg2d_sysmmu_fault_handler);
	fimg2d_info("register sysmmu page fault handler\n");

	/* misc register */
	ret = misc_register(&fimg2d_dev);
	if (ret) {
		fimg2d_err("failed to register misc driver\n");
		goto clk_release;
	}

#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
	pm_qos_add_request(&exynos5_g2d_cpu_qos,
				PM_QOS_CPU_FREQ_MIN, 0);
#endif
#ifdef CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ
	pm_qos_add_request(&exynos5_g2d_mif_qos,
				PM_QOS_BUS_THROUGHPUT, 0);
#endif
	return 0;

clk_release:
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(ctrl->dev);
#else
	fimg2d_clk_off(ctrl);
#endif
	fimg2d_clk_release(ctrl);

irq_free:
	free_irq(ctrl->irq, NULL);
reg_unmap:
	iounmap(ctrl->regs);
mem_free:
	kfree(ctrl->mem);
res_free:
	release_resource(ctrl->mem);
drv_free:
#ifdef BLIT_WORKQUE
	if (ctrl->work_q)
		destroy_workqueue(ctrl->work_q);
#endif
	mutex_destroy(&ctrl->drvlock);
	kfree(ctrl);

	return ret;
}

static int fimg2d_remove(struct platform_device *pdev)
{
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
	pm_qos_remove_request(&exynos5_g2d_cpu_qos);
#endif
#ifdef CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ
	pm_qos_remove_request(&exynos5_g2d_mif_qos);
#endif
	misc_deregister(&fimg2d_dev);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&pdev->dev);
#else
	fimg2d_clk_off(ctrl);
#endif
	fimg2d_clk_release(ctrl);
	free_irq(ctrl->irq, NULL);

	if (ctrl->mem) {
		iounmap(ctrl->regs);
		release_resource(ctrl->mem);
		kfree(ctrl->mem);
	}

#ifdef BLIT_WORKQUE
	destroy_workqueue(ctrl->work_q);
#endif
	mutex_destroy(&ctrl->drvlock);
	kfree(ctrl);
	return 0;
}

static int fimg2d_suspend(struct device *dev)
{
	unsigned long flags;
	int retry = POLL_RETRY;

	g2d_lock(&ctrl->drvlock);
	g2d_spin_lock(&ctrl->bltlock, flags);
	atomic_set(&ctrl->suspended, 1);
	g2d_spin_unlock(&ctrl->bltlock, flags);
	while (retry--) {
		if (fimg2d_queue_is_empty(&ctrl->cmd_q))
			break;
		mdelay(POLL_TIMEOUT);
	}
	g2d_unlock(&ctrl->drvlock);
	fimg2d_info("suspend... done\n");
	return 0;
}

static int fimg2d_resume(struct device *dev)
{
	unsigned long flags;

	g2d_lock(&ctrl->drvlock);
	g2d_spin_lock(&ctrl->bltlock, flags);
	atomic_set(&ctrl->suspended, 0);
	g2d_spin_unlock(&ctrl->bltlock, flags);
	g2d_unlock(&ctrl->drvlock);
	fimg2d_info("resume... done\n");
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

static char banner[] __initdata =
	"Exynos Graphics 2D driver, (c) 2011 Samsung Electronics\n";

static int __init fimg2d_register(void)
{
	pr_info("%s", banner);
	return platform_driver_register(&fimg2d_driver);
}

static void __exit fimg2d_unregister(void)
{
	platform_driver_unregister(&fimg2d_driver);
}

int fimg2d_ip_version_is(void)
{
	struct fimg2d_platdata *pdata = to_fimg2d_plat(ctrl->dev);

	return pdata->ip_ver;
}

module_init(fimg2d_register);
module_exit(fimg2d_unregister);

MODULE_AUTHOR("Eunseok Choi <es10.choi@samsung.com>");
MODULE_AUTHOR("Jinsung Yang <jsgood.yang@samsung.com>");
MODULE_DESCRIPTION("Samsung Graphics 2D driver");
MODULE_LICENSE("GPL");
