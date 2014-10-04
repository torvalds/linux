/*
 * drivers\media\audio\sun4i_dev_ace.c
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>

#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <mach/hardware.h>
#include <asm/system.h>
#include <linux/rmap.h>
#include <linux/string.h>
#include <mach/clock.h>
#include "sun4i_drv_ace.h"
#include "sun4i_ace_i.h"
#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

static struct class *ace_dev_class;
static struct cdev *ace_dev;
static dev_t dev_num ;
struct clk *ace_moduleclk,*dram_aceclk,*ahb_aceclk,*ace_pll5_pclk;
static unsigned long suspend_acerate = 0;
static int ref_count = 0;
static DECLARE_WAIT_QUEUE_HEAD(wait_ae);
__u32 ae_interrupt_sta = 0, ae_interrupt_value = 0;
void *       ccmu_hsram;
/*ae、ace、ce共享中断号*/
#define ACE_IRQ_NO (SW_INT_IRQNO_ACE)

//#define ACE_DEBUG
static int ace_dev_open(struct inode *inode, struct file *filp){
    int status = 0;
    return status;
}

static int ace_dev_release(struct inode *inode, struct file *filp){
    int status = 0;

    return status;
}

/*
 * Audio engine interrupt service routine
 * To wake up ae wait queue
 */
static irqreturn_t ace_interrupt(int irq, void *dev)
{
	volatile int ae_out_mode_reg = 0;

	/*status 0x24*/
	ae_out_mode_reg = readReg(AE_STATUS_REG);
	ae_interrupt_value = ae_out_mode_reg;
	if(ae_out_mode_reg & 0x04){
	  writeReg(AE_INT_EN_REG,readReg(AE_INT_EN_REG)&(~0x0f));
	}

	ae_out_mode_reg &= 0x0f;
	writeReg(AE_STATUS_REG,ae_out_mode_reg);
	ae_interrupt_sta = 1;

	ae_out_mode_reg = readReg(AE_STATUS_REG);
	wake_up_interruptible(&wait_ae);

    return IRQ_HANDLED;
}

/* 200MHz is the limit for ACE_CLK_REG (see the A10 User Manual) */
#define ACE_CLOCK_SPEED_LIMIT 200000000

static long ace_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int 				ret_val = 0;
	unsigned long       test_arg;
	int pll5_div;
	__ace_req_e 		mpara;
	unsigned long rate;
	switch (cmd){
		case ACE_DEV_HWREQ:
			test_arg = copy_from_user(&mpara, (__ace_req_e *)arg,
                       sizeof(__ace_req_e));
			ret_val = ACE_HwReq(mpara.module, mpara.mode, mpara.timeout);
			break;

		case ACE_DEV_HWREL:
			test_arg = copy_from_user(&mpara, (__ace_req_e *)arg,
                       sizeof(__ace_req_e));
			ret_val = ACE_HwRel(mpara.module);
			break;

		case ACE_DEV_GETCLKFREQ:
			test_arg = ACE_GetClk();
			put_user(test_arg, (unsigned long *)arg);
			ret_val = 1;
			break;
		case ACE_DEV_GET_ADDR:
			put_user((int)ace_hsram, (int *)arg);
			break;
		case ACE_DEV_INS_ISR:
			break;

		case ACE_DEV_UNINS_ISR:
		   break;

		case ACE_DEV_WAIT_AE:
			wait_event_interruptible(wait_ae,
			    ae_interrupt_sta);
			ae_interrupt_sta = 0;
			return ae_interrupt_value;
		case ACE_DEV_CLK_OPEN:
			/* ace_moduleclk */
			ace_moduleclk = clk_get(NULL,"ace");
			ace_pll5_pclk = clk_get(NULL, "sdram_pll_p");
			if (clk_set_parent(ace_moduleclk, ace_pll5_pclk)) {
				printk("try to set parent of ace_moduleclk to ace_pll5clk failed!\n");
			}
			rate = clk_get_rate(ace_pll5_pclk);
			pll5_div = DIV_ROUND_UP(rate, ACE_CLOCK_SPEED_LIMIT);
			if(clk_set_rate(ace_moduleclk, rate / pll5_div)) {
				printk("try to set ace_moduleclk rate failed!!!\n");
				 goto out;
			}
			if(clk_reset(ace_moduleclk, 1)){
				printk("try to reset ace_moduleclkfailed!!!\n");
				 goto out;
			}
			if(clk_reset(ace_moduleclk, 0)){
				printk("try to reset ace_moduleclkfailed!!!\n");
				 goto out;
			}
			if (-1 == clk_enable(ace_moduleclk)) {
				printk("ace_moduleclk failed; \n");
				goto out;
			}

			/*geting dram clk for ace!*/
			dram_aceclk = clk_get(NULL, "sdram_ace");

			if (-1 == clk_enable(dram_aceclk)) {
				printk("dram_moduleclk failed; \n");
				 goto out1;
			}
			/* getting ahb clk for ace! */
			ahb_aceclk = clk_get(NULL,"ahb_ace");
			if (-1 == clk_enable(ahb_aceclk)) {
				printk("ahb_aceclk failed; \n");
				goto out2;
			}
			ref_count++;
			goto out;
			out2:
				clk_disable(dram_aceclk);
			out1:
				clk_disable(ace_moduleclk);
			out:
			break;
		case ACE_DEV_CLK_CLOSE:
			clk_disable(ace_moduleclk);
			//释放ace_moduleclk时钟句柄
			clk_put(ace_moduleclk);
			//释放ace_pll5_pclk时钟句柄
			clk_put(ace_pll5_pclk);

			clk_disable(dram_aceclk);
			//释放dram_aceclk时钟句柄
			clk_put(dram_aceclk);

			clk_disable(ahb_aceclk);
			//释放ahb_aceclk时钟句柄
			clk_put(ahb_aceclk);
			ref_count--;
			break;
		default:
			break;
	}
	return ret_val;
}

void acedev_vma_open(struct vm_area_struct *vma)
{

}

void acedev_vma_close(struct vm_area_struct *vma)
{

}

static struct vm_operations_struct acedev_remap_vm_ops = {
    .open = acedev_vma_open,
    .close = acedev_vma_close,
};

static int acedev_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long temp_pfn;
    temp_pfn = ACE_REGS_pBASE >> 12;
    /* Set reserved and I/O flag for the area. */
    vma->vm_flags |= VM_RESERVED | VM_IO;

    /* Select uncached access. */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    if (io_remap_pfn_range(vma, vma->vm_start, temp_pfn,
                    vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
        return -EAGAIN;
    }
    vma->vm_ops = &acedev_remap_vm_ops;
    acedev_vma_open(vma);

    return 0;
}

static int snd_sw_ace_suspend(struct platform_device *pdev,pm_message_t state)
{
	suspend_acerate = clk_get_rate(ace_moduleclk);
	clk_disable(dram_aceclk);
	//释放dram_aceclk时钟句柄
	clk_put(dram_aceclk);

	clk_disable(ace_moduleclk);
	//释放ace_moduleclk时钟句柄
	clk_put(ace_moduleclk);
	clk_disable(ahb_aceclk);
	//释放ahb_aceclk时钟句柄
	clk_put(ahb_aceclk);
	//释放ace_pll5_pclk时钟句柄
	clk_put(ace_pll5_pclk);

	/*for clk test*/
#ifdef ACE_DEBUG
	printk("[ace_suspend reg]\n");
	printk("ace_module CLK:0xf1c20148 is:%x\n", *(volatile int *)0xf1c20148);
	printk("ace_pll5_p CLK:0xf1c20020 is:%x\n", *(volatile int *)0xf1c20020);
	printk("dram_ace CLK:0xf1c20100 is:%x\n", *(volatile int *)0xf1c20100);
	printk("ahb_ace CLK:0xf1c20060 is:%x\n", *(volatile int *)0xf1c20060);
	printk("[ace_suspend reg]\n");
#endif
	return 0;
}

static int snd_sw_ace_resume(struct platform_device *pdev)
{
	if(ref_count == 0){
		return 0;
	}
	/* ace_moduleclk */
	ace_moduleclk = clk_get(NULL,"ace");
	ace_pll5_pclk = clk_get(NULL, "sdram_pll_p");
	if (clk_set_parent(ace_moduleclk, ace_pll5_pclk)) {
		printk("try to set parent of ace_moduleclk to ace_pll5clk failed!\n");
		goto out;
	}

	if(clk_set_rate(ace_moduleclk, suspend_acerate)) {
	   	printk("try to set ace_moduleclk rate failed!!!\n");
	   	goto out;
	}
	if(clk_reset(ace_moduleclk, 1)){
	   	printk("try to reset ace_moduleclkfailed!!!\n");
	   	goto out;
	}
	if(clk_reset(ace_moduleclk, 0)){
	   	printk("try to reset ace_moduleclkfailed!!!\n");
	   	goto out;
	}
	if (-1 == clk_enable(ace_moduleclk)) {
	   printk("ace_moduleclk failed; \n");
	   goto out;
	}

	/*geting dram clk for ace!*/
	dram_aceclk = clk_get(NULL, "sdram_ace");

	if (-1 == clk_enable(dram_aceclk)) {
	   	printk("dram_moduleclk failed; \n");
	   	goto out1;
	}
	/* getting ahb clk for ace! */
	ahb_aceclk = clk_get(NULL,"ahb_ace");
	if (-1 == clk_enable(ahb_aceclk)) {
	   	printk("ahb_aceclk failed; \n");
	   	goto out2;
	}
	goto out;

	out2:
		clk_disable(dram_aceclk);
	out1:
		clk_disable(ace_moduleclk);
	out:
	return 0;
}

static struct file_operations ace_dev_fops = {
    .owner =    THIS_MODULE,
    .unlocked_ioctl = ace_dev_ioctl,
    .mmap           = acedev_mmap,
    .open           = ace_dev_open,
    .release        = ace_dev_release,
};

/*data relating*/
static struct platform_device sw_device_ace = {
	.name = "sun4i-ace",
};

/*method relating*/
static struct platform_driver sw_ace_driver = {
#ifdef CONFIG_PM
	.suspend	= snd_sw_ace_suspend,
	.resume		= snd_sw_ace_resume,
#endif
	.driver		= {
		.name	= "sun4i-ace",
	},
};

static int __init ace_dev_init(void)
{
    int status = 0;
    int err = 0;
	int ret = 0;
	printk("[ace_drv] start!!!\n");
	ret = request_irq(ACE_IRQ_NO, ace_interrupt, 0, "ace_dev", NULL);
	if (ret < 0) {
	   printk("request ace irq err\n");
	   return -EINVAL;
	}

	if((platform_device_register(&sw_device_ace))<0)
		return err;
	if ((err = platform_driver_register(&sw_ace_driver)) < 0)
		return err;

    alloc_chrdev_region(&dev_num, 0, 1, "ace_chrdev");
    ace_dev = cdev_alloc();
    cdev_init(ace_dev, &ace_dev_fops);
    ace_dev->owner = THIS_MODULE;
    err = cdev_add(ace_dev, dev_num, 1);
    if (err) {
    	printk(KERN_NOTICE"Error %d adding ace_dev!\n", err);
        return -1;
    }

    ace_dev_class = class_create(THIS_MODULE, "ace_cls");
    device_create(ace_dev_class, NULL, dev_num, NULL, "ace_dev");
    ACE_Init();
    printk("[ace_drv] init end!!!\n");
    return status;
}
module_init(ace_dev_init);

static void __exit ace_dev_exit(void)
{
	free_irq(ACE_IRQ_NO, NULL);
	ACE_Exit();
    device_destroy(ace_dev_class,  dev_num);
    class_destroy(ace_dev_class);
    platform_driver_unregister(&sw_ace_driver);
}
module_exit(ace_dev_exit);

MODULE_AUTHOR("young");
MODULE_DESCRIPTION("User mode encrypt device interface");
MODULE_LICENSE("GPL");
