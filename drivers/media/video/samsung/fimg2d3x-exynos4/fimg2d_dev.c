/* drivers/media/video/samsung/fimg2d3x/fimg2d3x_dev.c
 *
 * Copyright  2010 Samsung Electronics Co, Ltd. All Rights Reserved. 
 *		      http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file implements fimg2d driver.
 */

#include <linux/init.h>

#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>

#include <linux/version.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/semaphore.h>

#include <asm/io.h>

#include <mach/cpufreq.h>
#include <plat/cpu.h>
#include <plat/fimg2d.h>

#if defined(CONFIG_EXYNOS_DEV_PD)
#include <linux/pm_runtime.h>
#endif 

#include "fimg2d.h"
#include "fimg2d3x_regs.h"

#include <linux/smp.h>

struct g2d_global *g2d_dev;

int g2d_sysmmu_fault(unsigned int faulted_addr, unsigned int pt_base)
{
	g2d_reset(g2d_dev);

	atomic_set(&g2d_dev->is_mmu_faulted, 1);

	g2d_dev->faulted_addr = faulted_addr;

	wake_up_interruptible(&g2d_dev->waitq);

	return 0;
}


irqreturn_t g2d_irq(int irq, void *dev_id)
{
	g2d_set_int_finish(g2d_dev);

	g2d_dev->irq_handled = 1;

	wake_up_interruptible(&g2d_dev->waitq);

	atomic_set(&g2d_dev->in_use, 0);

	return IRQ_HANDLED;
}


static int g2d_open(struct inode *inode, struct file *file)
{
	atomic_inc(&g2d_dev->num_of_object);

	FIMG2D_DEBUG("Context Opened %d\n", atomic_read(&g2d_dev->num_of_object));

	return 0;
}


static int g2d_release(struct inode *inode, struct file *file)
{
	atomic_dec(&g2d_dev->num_of_object);

	FIMG2D_DEBUG("Context Closed %d\n", atomic_read(&g2d_dev->num_of_object));

	return 0;
}

static int g2d_mmap(struct file* filp, struct vm_area_struct *vma) 
{
	return 0;
}


static long g2d_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	g2d_params params;
	int ret = -1;

	struct g2d_dma_info dma_info;

	switch(cmd) {
	case G2D_GET_MEMORY :
		ret =  copy_to_user((unsigned int *)arg,
			&(g2d_dev->reserved_mem.base), sizeof(g2d_dev->reserved_mem.base));
		if (ret) {
			FIMG2D_ERROR("error : copy_to_user\n");
			return -EINVAL;
		}
		return 0;

	case G2D_GET_MEMORY_SIZE :
		ret =  copy_to_user((unsigned int *)arg,
			&(g2d_dev->reserved_mem.size), sizeof(g2d_dev->reserved_mem.size));
		if (ret) {
			FIMG2D_ERROR("error : copy_to_user\n");
			return -EINVAL;
		}
		return 0;

	case G2D_DMA_CACHE_CLEAN :
	case G2D_DMA_CACHE_FLUSH :
		mutex_lock(&g2d_dev->lock);
		ret = copy_from_user(&dma_info, (struct g2d_dma_info *)arg, sizeof(dma_info));

		if (ret) {
			FIMG2D_ERROR("error : copy_from_user\n");
			mutex_unlock(&g2d_dev->lock);
			return -EINVAL;
		}

		if (dma_info.addr == 0) {
			FIMG2D_ERROR("addr Null Error!!!\n");                
			mutex_unlock(&g2d_dev->lock);
			return -EINVAL;
		}

		g2d_mem_cache_op(cmd, (void *)dma_info.addr, dma_info.size);
		mutex_unlock(&g2d_dev->lock);
		return 0;

	case G2D_SYNC :
		g2d_check_fifo_state_wait(g2d_dev);
		ret = 0;
		goto g2d_ioctl_done;

	case G2D_RESET :
		g2d_reset(g2d_dev);
		FIMG2D_ERROR("G2D TimeOut Error\n");
		ret = 0;
		goto g2d_ioctl_done;
		
	case G2D_BLIT:
		if  (atomic_read(&g2d_dev->ready_to_run) == 0)
			goto g2d_ioctl_done2;
		
		mutex_lock(&g2d_dev->lock);
		
		g2d_clk_enable(g2d_dev);

		if (copy_from_user(&params, (struct g2d_params *)arg, sizeof(g2d_params))) {
			FIMG2D_ERROR("error : copy_from_user\n");
			goto g2d_ioctl_done;
		}

		atomic_set(&g2d_dev->in_use, 1);
		if (atomic_read(&g2d_dev->ready_to_run) == 0)
			goto g2d_ioctl_done;

		if (params.flag.memory_type == G2D_MEMORY_USER)
			down_write(&page_alloc_slow_rwsem);

		g2d_dev->irq_handled = 0;
		if (!g2d_do_blit(g2d_dev, &params)) {
			g2d_dev->irq_handled = 1;
			if (params.flag.memory_type == G2D_MEMORY_USER)
				up_write(&page_alloc_slow_rwsem);
			goto g2d_ioctl_done;
		}

		if(!(file->f_flags & O_NONBLOCK)) {
			if (!g2d_wait_for_finish(g2d_dev, &params)) {
				if (params.flag.memory_type == G2D_MEMORY_USER)
					up_write(&page_alloc_slow_rwsem);
				goto g2d_ioctl_done;
			}
		}

		if (params.flag.memory_type == G2D_MEMORY_USER)
			up_write(&page_alloc_slow_rwsem);
		ret = 0;

		break;
	default :
		goto g2d_ioctl_done2;
		break;
	}

g2d_ioctl_done :

	g2d_clk_disable(g2d_dev);

	mutex_unlock(&g2d_dev->lock);

	atomic_set(&g2d_dev->in_use, 0);

g2d_ioctl_done2 :

	return ret;
}

static unsigned int g2d_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	if (atomic_read(&g2d_dev->in_use) == 0) {
		mask = POLLOUT | POLLWRNORM;
		g2d_clk_disable(g2d_dev);
		
		mutex_unlock(&g2d_dev->lock);

	} else {
		poll_wait(file, &g2d_dev->waitq, wait);

		if(atomic_read(&g2d_dev->in_use) == 0) {
			mask = POLLOUT | POLLWRNORM;
			g2d_clk_disable(g2d_dev);
			
			mutex_unlock(&g2d_dev->lock);
		}
	}
	
	return mask;
}

static struct file_operations fimg2d_fops = {
	.owner 		= THIS_MODULE,
	.open 		= g2d_open,
	.release 	= g2d_release,
	.mmap 		= g2d_mmap,
	.unlocked_ioctl = g2d_ioctl,
	.poll		= g2d_poll,
};


static struct miscdevice fimg2d_dev = {
	.minor		= G2D_MINOR,
	.name		= "fimg2d",
	.fops		= &fimg2d_fops,
};

static int g2d_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;
	struct clk *parent;
	struct clk *sclk;
	
	FIMG2D_DEBUG("start probe : name=%s num=%d res[0].start=0x%x res[1].start=0x%x\n",
	        			pdev->name, pdev->num_resources, 
	        			pdev->resource[0].start, pdev->resource[1].start);

	/* alloc g2d global */
	g2d_dev = kzalloc(sizeof(*g2d_dev), GFP_KERNEL);
	if (!g2d_dev) {
		FIMG2D_ERROR( "not enough memory\n");
		ret = -ENOENT;
		goto probe_out;
	}

#if defined(CONFIG_EXYNOS_DEV_PD)
	/* to use the runtime PM helper functions */
	pm_runtime_enable(&pdev->dev);
	/* enable the power domain */
	pm_runtime_get_sync(&pdev->dev);
#endif

	/* get the memory region */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(res == NULL) {
		FIMG2D_ERROR("failed to get memory region resouce\n");
		ret = -ENOENT;
		goto err_get_res;
	}

	/* request momory region */
	g2d_dev->mem = request_mem_region(res->start, 
					          res->end - res->start + 1, 
					          pdev->name);
	if(g2d_dev->mem == NULL) {
		FIMG2D_ERROR("failed to reserve memory region\n");
		ret = -ENOENT;
		goto err_mem_req;
	}
	
	/* ioremap */
	g2d_dev->base = ioremap(g2d_dev->mem->start, 
				g2d_dev->mem->end - res->start + 1);
	if(g2d_dev->base == NULL) {
		FIMG2D_ERROR("failed ioremap\n");
		ret = -ENOENT;
		goto err_mem_map;
	}

	/* get irq */
	g2d_dev->irq_num = platform_get_irq(pdev, 0);
	if(g2d_dev->irq_num <= 0) {
		FIMG2D_ERROR("failed to get irq resouce\n");
		ret = -ENOENT;
		goto err_irq_req;
	}

	/* blocking I/O */
	init_waitqueue_head(&g2d_dev->waitq);

	/* request irq */
	ret = request_irq(g2d_dev->irq_num, g2d_irq, 
			IRQF_DISABLED, pdev->name, NULL);
	if (ret) {
		FIMG2D_ERROR("request_irq(g2d) failed.\n");
		ret = -ENOENT;
		goto err_irq_req;
	}

	/* clock domain setting*/
	parent = clk_get(&pdev->dev, "mout_mpll");
	if (IS_ERR(parent)) {
		FIMG2D_ERROR("failed to get parent clock\n");
		ret = -ENOENT;
		goto err_clk_get1;
	}

	sclk = clk_get(&pdev->dev, "sclk_fimg2d");
	if (IS_ERR(sclk)) {
		FIMG2D_ERROR("failed to get sclk_g2d clock\n");
		ret = -ENOENT;
		goto err_clk_get2;
	}

	clk_set_parent(sclk, parent);
	clk_set_rate(sclk, 267 * MHZ);	/* 266 Mhz */

	/* clock for gating  */
	g2d_dev->clock = clk_get(&pdev->dev, "fimg2d");
	if (IS_ERR(g2d_dev->clock)) {
		FIMG2D_ERROR("failed to get clock clock\n");
		ret = -ENOENT;
		goto err_clk_get3;
	}

	ret = g2d_init_mem(&pdev->dev, &g2d_dev->reserved_mem.base, &g2d_dev->reserved_mem.size);

	if (ret != 0) {
		FIMG2D_ERROR("failed to init. fimg2d mem");
		ret = -ENOMEM;
		goto err_mem;
	}

	/* atomic init */
	atomic_set(&g2d_dev->in_use, 0);
	atomic_set(&g2d_dev->num_of_object, 0);
	atomic_set(&g2d_dev->is_mmu_faulted, 0);
	g2d_dev->faulted_addr = 0;

	/* misc register */
	ret = misc_register(&fimg2d_dev);
	if (ret) {
		FIMG2D_ERROR("cannot register miscdev on minor=%d (%d)\n",
			G2D_MINOR, ret);
		ret = -ENOMEM;
		goto err_misc_reg;
	}

	mutex_init(&g2d_dev->lock);

#if defined(CONFIG_HAS_EARLYSUSPEND)
	g2d_dev->early_suspend.suspend = g2d_early_suspend;
	g2d_dev->early_suspend.resume = g2d_late_resume;
	g2d_dev->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	register_early_suspend(&g2d_dev->early_suspend);
#endif

	g2d_dev->dev = &pdev->dev;
	atomic_set(&g2d_dev->ready_to_run, 1);

	g2d_sysmmu_on(g2d_dev);

	FIMG2D_DEBUG("g2d_probe ok!\n");

	return 0;

err_misc_reg:
err_mem:
	clk_put(g2d_dev->clock);
	g2d_dev->clock = NULL;	
err_clk_get3:
	clk_put(sclk);
err_clk_get2:
	clk_put(parent);
err_clk_get1:
	free_irq(g2d_dev->irq_num, NULL);
err_irq_req:
	iounmap(g2d_dev->base);
err_mem_map:
	release_resource(g2d_dev->mem);
	kfree(g2d_dev->mem);
err_mem_req:	
err_get_res:
	kfree(g2d_dev);
probe_out:
	FIMG2D_ERROR("g2d: sec_g2d_probe fail!\n");
	return ret;
}


static int g2d_remove(struct platform_device *dev)
{
	FIMG2D_DEBUG("g2d_remove called !\n");

	free_irq(g2d_dev->irq_num, NULL);

	if (g2d_dev->mem != NULL) {   
		FIMG2D_INFO("releasing resource\n");
		iounmap(g2d_dev->base);
		release_resource(g2d_dev->mem);
		kfree(g2d_dev->mem);
	}

	misc_deregister(&fimg2d_dev);

	atomic_set(&g2d_dev->in_use, 0);
	atomic_set(&g2d_dev->num_of_object, 0);

	g2d_clk_disable(g2d_dev);

	if (g2d_dev->clock) {
		clk_put(g2d_dev->clock);
		g2d_dev->clock = NULL;
	}

	mutex_destroy(&g2d_dev->lock);
	
#if defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&g2d_dev->early_suspend);
#endif

	kfree(g2d_dev);

#if defined(CONFIG_EXYNOS_DEV_PD)
	/* disable the power domain */
	pm_runtime_put(&dev->dev);
	pm_runtime_disable(&dev->dev);
#endif

	FIMG2D_DEBUG("g2d_remove ok!\n");

	return 0;
}

#if defined(CONFIG_HAS_EARLYSUSPEND)
void g2d_early_suspend(struct early_suspend *h)
{
	int i = 0;

	atomic_set(&g2d_dev->ready_to_run, 0);

	/* wait until G2D running is finished */
	while(1) {
		if (!atomic_read(&g2d_dev->in_use))
			break;

		msleep_interruptible(2);

		i++;
		/* Timeout 1sec */
		if (i > 500) {
			g2d_clk_enable(g2d_dev);
			g2d_reset(g2d_dev);
			g2d_clk_disable(g2d_dev);
			break;
		}
	}

	g2d_sysmmu_off(g2d_dev);

#if defined(CONFIG_EXYNOS_DEV_PD)
	/* disable the power domain */
	pm_runtime_put(g2d_dev->dev);
#endif
}

void g2d_late_resume(struct early_suspend *h)
{

#if defined(CONFIG_EXYNOS_DEV_PD)
	/* enable the power domain */
	pm_runtime_get_sync(g2d_dev->dev);
#endif

	g2d_sysmmu_on(g2d_dev);

	atomic_set(&g2d_dev->ready_to_run, 1);

}
#endif

#if !defined(CONFIG_HAS_EARLYSUSPEND)
static int g2d_suspend(struct platform_device *dev, pm_message_t state)
{
	int i = 0;

	atomic_set(&g2d_dev->ready_to_run, 0);

	/* wait until G2D running is finished */
	while(1) {
		if (!atomic_read(&g2d_dev->in_use))
			break;

		msleep_interruptible(2);

		i++;
		/* Timeout 1sec */
		if (i > 500) {
			g2d_clk_enable(g2d_dev);
			g2d_reset(g2d_dev);
			g2d_clk_disable(g2d_dev);
			break;
		}
	}

	g2d_sysmmu_off(g2d_dev);

#if defined(CONFIG_EXYNOS_DEV_PD)
	/* disable the power domain */
	pm_runtime_put(g2d_dev->dev);
#endif

	return 0;
}

static int g2d_resume(struct platform_device *pdev)
{

#if defined(CONFIG_EXYNOS_DEV_PD)
	/* enable the power domain */
	pm_runtime_get_sync(g2d_dev->dev);
#endif

	g2d_sysmmu_on(g2d_dev);

	atomic_set(&g2d_dev->ready_to_run, 1);

	return 0;
}
#endif

#if defined(CONFIG_EXYNOS_DEV_PD)
static int g2d_runtime_suspend(struct device *dev)
{
	return 0;
}

static int g2d_runtime_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops g2d_pm_ops = {
	.runtime_suspend = g2d_runtime_suspend,
	.runtime_resume = g2d_runtime_resume,
};
#endif


static struct platform_driver fimg2d_driver = {
	.probe		= g2d_probe,
	.remove		= g2d_remove,
#if !defined(CONFIG_HAS_EARLYSUSPEND)
	.suspend	= g2d_suspend,
	.resume		= g2d_resume,
#endif
	.driver		= {
				.owner	= THIS_MODULE,
				.name	= "s5p-fimg2d",
#if defined(CONFIG_EXYNOS_DEV_PD)
				.pm	= &g2d_pm_ops,
#endif
			},
};

int __init g2d_init(void)
{
 	if(platform_driver_register(&fimg2d_driver)!=0) {
   		FIMG2D_ERROR("platform device register Failed \n");
   		return -1;
  	}

	FIMG2D_DEBUG("ok!\n");

	return 0;
}

void  g2d_exit(void)
{
	platform_driver_unregister(&fimg2d_driver);

	FIMG2D_DEBUG("ok!\n");
}

module_init(g2d_init);
module_exit(g2d_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("SEC G2D Device Driver");
MODULE_LICENSE("GPL");
