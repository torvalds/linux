/* linux/drivers/media/video/samsung/jpeg/jpeg_dev.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Core file for Samsung Jpeg Interface driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/clk.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <linux/sched.h>

#include <plat/regs_jpeg.h>
#include <mach/irqs.h>
#if defined(CONFIG_CPU_S5PV210)
#include <mach/pd.h>
#endif

#if defined(CONFIG_S5P_SYSMMU_JPEG)
#include <plat/sysmmu.h>
#endif

#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif

#include "jpeg_core.h"
#include "jpeg_dev.h"
#include "jpeg_mem.h"

struct jpeg_control *jpeg_ctrl;
static struct device *jpeg_pm;

static int jpeg_open(struct inode *inode, struct file *file)
{
	int ret;
	int in_use;

	mutex_lock(&jpeg_ctrl->lock);

	in_use = atomic_read(&jpeg_ctrl->in_use);

	if (in_use > JPEG_MAX_INSTANCE) {
		ret = -EBUSY;
		goto resource_busy;
	} else {
		atomic_inc(&jpeg_ctrl->in_use);
		jpeg_info("jpeg driver opened.\n");
	}

	mutex_unlock(&jpeg_ctrl->lock);
#if defined(CONFIG_CPU_S5PV210)
	ret = s5pv210_pd_enable("jpeg_pd");
	if (ret < 0) {
		jpeg_err("failed to enable jpeg power domain\n");
		return -EINVAL;
	}
#endif

	/* clock enable */
	clk_enable(jpeg_ctrl->clk);

	file->private_data = (struct jpeg_control *)jpeg_ctrl;

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_sync(jpeg_pm);
#endif

	return 0;
resource_busy:
	mutex_unlock(&jpeg_ctrl->lock);
	return ret;
}

static int jpeg_release(struct inode *inode, struct file *file)
{
	atomic_dec(&jpeg_ctrl->in_use);

	jpeg_mem_free();

	clk_disable(jpeg_ctrl->clk);

#if defined(CONFIG_CPU_S5PV210)
	if (s5pv210_pd_disable("jpeg_pd") < 0) {
		jpeg_err("failed to disable jpeg power domain\n");
		return -EINVAL;
	}
#endif

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_put_sync(jpeg_pm);
#endif

	return 0;
}

static long jpeg_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int ret;
	struct jpeg_control	*ctrl;

	ctrl  = (struct jpeg_control *)file->private_data;
	if (!ctrl) {
		jpeg_err("jpeg invalid input argument\n");
		return -1;
	}

	switch (cmd) {

	case IOCTL_JPEG_DEC_EXE:
		ret = copy_from_user(&ctrl->dec_param,
			(struct jpeg_dec_param *)arg,
			sizeof(struct jpeg_dec_param));

		jpeg_exe_dec(ctrl);
		ret = copy_to_user((void *)arg,
			(void *) &ctrl->dec_param,
			sizeof(struct jpeg_dec_param));
		break;

	case IOCTL_JPEG_ENC_EXE:
		ret = copy_from_user(&ctrl->enc_param,
			(struct jpeg_enc_param *)arg,
			sizeof(struct jpeg_enc_param));

		jpeg_exe_enc(ctrl);
		ret = copy_to_user((void *)arg,
			(void *) &ctrl->enc_param,
			sizeof(struct jpeg_enc_param));
		break;

	case IOCTL_GET_DEC_IN_BUF:
	case IOCTL_GET_ENC_OUT_BUF:
		return jpeg_get_stream_buf(arg);

	case IOCTL_GET_DEC_OUT_BUF:
	case IOCTL_GET_ENC_IN_BUF:
		return jpeg_get_frame_buf(arg);

	case IOCTL_GET_PHYADDR:
		return jpeg_ctrl->mem.frame_data_addr;

	case IOCTL_GET_PHYMEM_BASE:
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_JPEG
		if (copy_to_user((void *)arg, &jpeg_ctrl->mem.base, sizeof(unsigned int))) {
			jpeg_err("IOCTL_GET_PHYMEM_BASE:::copy_to_user error\n");
			return -1;
		}
		return 0;
#else
		return -1;
#endif

	case IOCTL_GET_PHYMEM_SIZE:
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_JPEG
		ret = CONFIG_VIDEO_SAMSUNG_MEMSIZE_JPEG * 1024;
		if (copy_to_user((void *)arg, &ret, sizeof(unsigned int))) {
			jpeg_err("IOCTL_GET_PHYMEM_SIZE:::copy_to_user error\n");
			return -1;
		}
		return 0;
#else
		return -1;
#endif

	case IOCTL_SET_DEC_PARAM:
		ret = copy_from_user(&ctrl->dec_param,
			(struct jpeg_dec_param *)arg,
			sizeof(struct jpeg_dec_param));

		ret = jpeg_set_dec_param(ctrl);

		break;

	case IOCTL_SET_ENC_PARAM:
		ret = copy_from_user(&ctrl->enc_param,
			(struct jpeg_enc_param *)arg,
			sizeof(struct jpeg_enc_param));

		ret = jpeg_set_enc_param(ctrl);
		break;

	default:
		break;
	}
	return 0;
}

int jpeg_mmap(struct file *filp, struct vm_area_struct *vma)
{
#if defined(CONFIG_S5P_SYSMMU_JPEG)
#if !defined(CONFIG_S5P_VMEM)
	unsigned long	page_frame_no;
	unsigned long	start;
	unsigned long	size;
	char		*ptr;	/* vmalloc */

	size = vma->vm_end - vma->vm_start;
	ptr = (char *)jpeg_ctrl->mem.base;
	start = 0;

	vma->vm_flags |= VM_RESERVED | VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	while (size > 0) {
		page_frame_no = vmalloc_to_pfn(ptr);
		if (remap_pfn_range(vma, vma->vm_start + start, page_frame_no,
			PAGE_SIZE, vma->vm_page_prot)) {
			jpeg_err("failed to remap jpeg pfn range.\n");
			return -ENOMEM;
		}

		start += PAGE_SIZE;
		ptr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
#endif /* CONFIG_S5P_VMEM */
#else
	unsigned long	page_frame_no;
	unsigned long	size;
	int		ret;

	size = vma->vm_end - vma->vm_start;

	vma->vm_flags |= VM_RESERVED | VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	page_frame_no = __phys_to_pfn(jpeg_ctrl->mem.base);
	ret = remap_pfn_range(vma, vma->vm_start, page_frame_no,
					size, vma->vm_page_prot);
	if (ret != 0) {
		jpeg_err("failed to remap jpeg pfn range.\n");
		return -ENOMEM;
	}
#endif /* SYSMMU_JPEG_ON */

	return 0;
}

static const struct file_operations jpeg_fops = {
	.owner = THIS_MODULE,
	.open =	jpeg_open,
	.release = jpeg_release,
	.unlocked_ioctl = jpeg_ioctl,
	.mmap =	jpeg_mmap,
};

static struct miscdevice jpeg_miscdev = {
	.minor = JPEG_MINOR_NUMBER,
	.name =	JPEG_NAME,
	.fops =	&jpeg_fops,
};

static irqreturn_t jpeg_irq(int irq, void *dev_id)
{
	unsigned int int_status;
	struct jpeg_control *ctrl = (struct jpeg_control *) dev_id;

	int_status = jpeg_int_pending(ctrl);

	if (int_status) {
		switch (int_status) {
		case 0x40:
			ctrl->irq_ret = OK_ENC_OR_DEC;
			break;
		case 0x20:
			ctrl->irq_ret = ERR_ENC_OR_DEC;
			break;
		default:
			ctrl->irq_ret = ERR_UNKNOWN;
		}
		wake_up_interruptible(&ctrl->wq);
	} else {
		ctrl->irq_ret = ERR_UNKNOWN;
		wake_up_interruptible(&ctrl->wq);
	}

	return IRQ_HANDLED;
}

static int jpeg_setup_controller(struct jpeg_control *ctrl)
{
#if defined(CONFIG_S5P_SYSMMU_JPEG)
	s5p_sysmmu_enable(jpeg_pm);
	jpeg_dbg("sysmmu on\n");
	/* jpeg hw uses kernel virtual address */
	s5p_sysmmu_set_tablebase_pgd(jpeg_pm, __pa(swapper_pg_dir));
#endif
	atomic_set(&ctrl->in_use, 0);
	mutex_init(&ctrl->lock);
	init_waitqueue_head(&ctrl->wq);

	return 0;
}

static int jpeg_probe(struct platform_device *pdev)
{
	struct	resource *res;
	int	ret;

	/* global structure */
	jpeg_ctrl = kzalloc(sizeof(*jpeg_ctrl), GFP_KERNEL);
	if (!jpeg_ctrl) {
		dev_err(&pdev->dev, "%s: not enough memory\n",
			__func__);
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* setup jpeg control */
	ret = jpeg_setup_controller(jpeg_ctrl);
	if (ret) {
		jpeg_err("failed to setup controller\n");
		goto err_setup;
	}

	/* memory region */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		jpeg_err("failed to get jpeg memory region resource\n");
		ret = -ENOENT;
		goto err_res;
	}

	res = request_mem_region(res->start,
				res->end - res->start + 1, pdev->name);
	if (!res) {
		jpeg_err("failed to request jpeg io memory region\n");
		ret = -ENOMEM;
		goto err_region;
	}

	/* ioremap */
	jpeg_ctrl->reg_base = ioremap(res->start, res->end - res->start + 1);
	if (!jpeg_ctrl->reg_base) {
		jpeg_err("failed to remap jpeg io region\n");
		ret = -ENOENT;
		goto err_map;
	}

	/* irq */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		jpeg_err("failed to request jpeg irq resource\n");
		ret = -ENOENT;
		goto err_irq;
	}

	jpeg_ctrl->irq_no = res->start;
	ret = request_irq(jpeg_ctrl->irq_no, (void *)jpeg_irq,
			IRQF_DISABLED, pdev->name, jpeg_ctrl);
	if (ret != 0) {
		jpeg_err("failed to jpeg request irq\n");
		ret = -ENOENT;
		goto err_irq;
	}

	/* clock */
	jpeg_ctrl->clk = clk_get(&pdev->dev, "jpeg");
	if (IS_ERR(jpeg_ctrl->clk)) {
		jpeg_err("failed to find jpeg clock source\n");
		ret = -ENOENT;
		goto err_clk;
	}
	ret = jpeg_init_mem(&pdev->dev, &jpeg_ctrl->mem.base);
	if (ret != 0) {
		jpeg_err("failed to init. jpeg mem");
		ret = -ENOMEM;
		goto err_mem;
	}

	ret = misc_register(&jpeg_miscdev);
	if (ret) {
		jpeg_err("failed to register misc driver\n");
		goto err_reg;
	}

	jpeg_pm = &pdev->dev;
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(jpeg_pm);
#endif
	return 0;

err_reg:
	clk_put(jpeg_ctrl->clk);
err_mem:
err_clk:
	free_irq(jpeg_ctrl->irq_no, NULL);
err_irq:
	iounmap(jpeg_ctrl->reg_base);
err_map:
err_region:
	kfree(res);
err_res:
	mutex_destroy(&jpeg_ctrl->lock);
err_setup:
	kfree(jpeg_ctrl);
err_alloc:
	return ret;

}

static int jpeg_remove(struct platform_device *dev)
{
#if defined(CONFIG_S5P_SYSMMU_JPEG)
	s5p_sysmmu_disable(jpeg_pm);
	jpeg_dbg("sysmmu off\n");
#endif
	free_irq(jpeg_ctrl->irq_no, dev);
	mutex_destroy(&jpeg_ctrl->lock);
	iounmap(jpeg_ctrl->reg_base);

	kfree(jpeg_ctrl);
	misc_deregister(&jpeg_miscdev);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(jpeg_pm);
#endif
	return 0;
}

static int jpeg_suspend(struct platform_device *pdev, pm_message_t state)
{
	/* clock disable */
	clk_disable(jpeg_ctrl->clk);
#if defined(CONFIG_CPU_S5PV210)
	if (s5pv210_pd_disable("jpeg_pd") < 0) {
		jpeg_err("failed to disable jpeg power domain\n");
		return -EINVAL;
	}
#endif
	return 0;
}

static int jpeg_resume(struct platform_device *pdev)
{
#if defined(CONFIG_CPU_S5PV210)
	if (s5pv210_pd_enable("jpeg_pd") < 0) {
		jpeg_err("failed to enable jpeg power domain\n");
		return -EINVAL;
	}
#endif
	/* clock enable */
	clk_enable(jpeg_ctrl->clk);

	return 0;
}

int jpeg_suspend_pd(struct device *dev)
{
	struct platform_device *pdev;
	int ret;
	pm_message_t state;

	state.event = 0;
	pdev = to_platform_device(dev);
	ret = jpeg_suspend(pdev, state);

	return 0;
}

int jpeg_resume_pd(struct device *dev)
{
	struct platform_device *pdev;
	int ret;

	pdev = to_platform_device(dev);
	ret = jpeg_resume(pdev);

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int jpeg_runtime_suspend(struct device *dev)
{
	return 0;
}

static int jpeg_runtime_resume(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops jpeg_pm_ops = {
	.suspend	= jpeg_suspend_pd,
	.resume		= jpeg_resume_pd,
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = jpeg_runtime_suspend,
	.runtime_resume = jpeg_runtime_resume,
#endif
};
static struct platform_driver jpeg_driver = {
	.probe		= jpeg_probe,
	.remove		= jpeg_remove,
#if (!defined(CONFIG_S5PV310_DEV_PD) || !defined(CONFIG_PM_RUNTIME))
	.suspend	= jpeg_suspend,
	.resume		= jpeg_resume,
#endif
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= JPEG_NAME,
#if (defined(CONFIG_S5PV310_DEV_PD) && defined(CONFIG_PM_RUNTIME))
		.pm = &jpeg_pm_ops,
#else
		.pm = NULL,
#endif
	},
};

static int __init jpeg_init(void)
{
	printk("Initialize JPEG driver\n");

	platform_driver_register(&jpeg_driver);

	return 0;
}

static void __exit jpeg_exit(void)
{
	platform_driver_unregister(&jpeg_driver);
}

module_init(jpeg_init);
module_exit(jpeg_exit);

MODULE_AUTHOR("Hyunmin, Kwak <hyunmin.kwak@samsung.com>");
MODULE_DESCRIPTION("JPEG Codec Device Driver");
MODULE_LICENSE("GPL");

