/* 
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <asm/delay.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <mach/io.h>
#include <mach/irqs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/firmware.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <asm/cacheflush.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/fb.h>



#include "rga.h"
#include "rga_reg_info.h"
#include "rga_mmu_info.h"
#include "RGA_API.h"


#define RGA_TEST 1

#define PRE_SCALE_BUF_SIZE  2048*1024*4

#define RGA_POWER_OFF_DELAY	4*HZ /* 4s */
#define RGA_TIMEOUT_DELAY	2*HZ /* 2s */


static struct rga_drvdata *drvdata = NULL;
rga_service_info rga_service;

#if 1//def RGA_TEST
//uint32_t dst_buf[800*480*4];
#endif


static int rga_blit_async(rga_session *session, struct rga_req *req);


#define RGA_MAJOR		232

#define RK30_RGA_PHYS	0x10114000
#define RK30_RGA_SIZE	SZ_8K
#define RGA_RESET_TIMEOUT	1000

/* Driver information */
#define DRIVER_DESC		"RGA Device Driver"
#define DRIVER_NAME		"rga"


/* Logging */
#define RGA_DEBUG 1
#if RGA_DEBUG
#define DBG(format, args...) printk(KERN_DEBUG "%s: " format, DRIVER_NAME, ## args)
#define ERR(format, args...) printk(KERN_ERR "%s: " format, DRIVER_NAME, ## args)
#define WARNING(format, args...) printk(KERN_WARN "%s: " format, DRIVER_NAME, ## args)
#define INFO(format, args...) printk(KERN_INFO "%s: " format, DRIVER_NAME, ## args)
#else
#define DBG(format, args...)
#define ERR(format, args...)
#define WARNING(format, args...)
#define INFO(format, args...)
#endif


static inline void rga_write(u32 b, u32 r)
{
	__raw_writel(b, drvdata->rga_base + r);
}

static inline u32 rga_read(u32 r)
{
	return __raw_readl(drvdata->rga_base + r);
}

static void rga_soft_reset(void)
{
	u32 i;
	u32 reg;

	rga_write(1, RGA_SYS_CTRL); //RGA_SYS_CTRL

	for(i = 0; i < RGA_RESET_TIMEOUT; i++) {
		reg = rga_read(RGA_SYS_CTRL) & 1; //RGA_SYS_CTRL

		if(reg == 0)
			break;

		udelay(1);
	}

	if(i == RGA_RESET_TIMEOUT)
		ERR("soft reset timeout.\n");
}

static void rga_dump(void)
{
	int running;
    int num_done;
    struct rga_reg *reg, *reg_tmp;
    rga_session *session, *session_tmp;    
    struct list_head *next; 

	running = atomic_read(&rga_service.total_running);
	printk("rga total_running %d\n", running);

    /* Dump waiting list info */
    if (!list_empty(&rga_service.waiting))
    {        
        next = &rga_service.waiting;

        printk("rga_service dump waiting list\n");

        do
        {
            reg = list_entry(next->next, struct rga_reg, status_link);
            running = atomic_read(&reg->session->task_running);
            num_done = atomic_read(&reg->session->num_done);
            printk("rga session pid %d, done %d, running %d\n", reg->session->pid, num_done, running);                       
            next = next->next;
        }
        while(!list_empty(next));                        
    }

    /* Dump running list info */
    if (!list_empty(&rga_service.running))
    {
        printk("rga_service dump running list\n");
            
        next = &rga_service.running;
        do
        {
            reg = list_entry(next->next, struct rga_reg, status_link);
            running = atomic_read(&reg->session->task_running);
            num_done = atomic_read(&reg->session->num_done);
            printk("rga session pid %d, done %d, running %d:\n", reg->session->pid, num_done, running);                       
            next = next->next;
        }
        while(!list_empty(next));                            
    }
    

	list_for_each_entry_safe(session, session_tmp, &rga_service.session, list_session) 
    {
		printk("session pid %d:\n", session->pid);
		running = atomic_read(&session->task_running);
		printk("task_running %d\n", running);
		list_for_each_entry_safe(reg, reg_tmp, &session->waiting, session_link) 
        {
			printk("waiting register set 0x%.8x\n", (unsigned int)reg);
		}
		list_for_each_entry_safe(reg, reg_tmp, &session->running, session_link) 
        {
			printk("running register set 0x%.8x\n", (unsigned int)reg);
		}
	}
}


static void rga_power_on(void)
{
	//printk("rga_power_on\n");
	cancel_delayed_work_sync(&drvdata->power_off_work);
	if (drvdata->enable)
		return;
    
	clk_enable(drvdata->pd_display);
	clk_enable(drvdata->aclk_lcdc);
	clk_enable(drvdata->hclk_lcdc);
	clk_enable(drvdata->aclk_ddr_lcdc);
	clk_enable(drvdata->hclk_cpu_display);
	clk_enable(drvdata->aclk_disp_matrix);
	clk_enable(drvdata->hclk_disp_matrix);
	clk_enable(drvdata->axi_clk);
	clk_enable(drvdata->ahb_clk);

	drvdata->enable = true;
}


static void rga_power_off(struct work_struct *work)
{
    int total_running;
    
    //printk("rga_power_off\n");
	if(!drvdata->enable)
		return;

    total_running = atomic_read(&rga_service.total_running);
	if (total_running) {
		pr_alert("power off when %d task running!!\n", total_running);               
		mdelay(50);
		pr_alert("delay 50 ms for running task\n");        
        rga_dump();
	}
    
	clk_disable(drvdata->pd_display);
	clk_disable(drvdata->aclk_lcdc);
	clk_disable(drvdata->hclk_lcdc);
	clk_disable(drvdata->aclk_ddr_lcdc);
	clk_disable(drvdata->hclk_cpu_display);
	clk_disable(drvdata->aclk_disp_matrix);
	clk_disable(drvdata->hclk_disp_matrix);
	clk_disable(drvdata->axi_clk);
	clk_disable(drvdata->ahb_clk);

	drvdata->enable = false;
}


static int rga_flush(rga_session *session, unsigned long arg)
{
	//printk("rga_get_result %d\n",drvdata->rga_result);
	
    int ret;

    ret = wait_event_interruptible_timeout(session->wait, atomic_read(&session->done), RGA_TIMEOUT_DELAY);
    
	if (unlikely(ret < 0)) {
		pr_err("pid %d wait task ret %d\n", session->pid, ret);
	} else if (0 == ret) {
		pr_err("pid %d wait %d task done timeout\n", session->pid, atomic_read(&session->task_running));
		ret = -ETIMEDOUT;
	}
    
	return ret;
}


static int rga_get_result(rga_session *session, unsigned long arg)
{
	//printk("rga_get_result %d\n",drvdata->rga_result);
	
    int ret = 0;

    int num_done;

    num_done = atomic_read(&session->num_done);
    	
	if (unlikely(copy_to_user((void __user *)arg, &num_done, sizeof(int)))) {
			printk("copy_to_user failed\n");
			ERR("copy_to_user failed\n");
			ret =  -EFAULT;	
		}
	//idle_condition = 1;
	//dmac_clean_range((const void*)&idle_condition,(const void*)&idle_condition+4);
	//wake_up_interruptible_sync(&blit_wait_queue);
	return ret;
}



static int rga_check_param(const struct rga_req *req)
{
	#if 1
	/*RGA can support up to 8192*8192 resolution in RGB format,but we limit the image size to 8191*8191 here*/
	//check src width and height
	if (unlikely((req->src.act_w < 0) || (req->src.act_w > 8191) || (req->src.act_h < 0) || (req->src.act_h > 8191))) {
		ERR("invalid source resolution\n");
		return  -EINVAL;
	}

	//check dst width and height
	if (unlikely((req->dst.act_w < 0) || (req->dst.act_w > 2048) || (req->dst.act_h < 16) || (req->dst.act_h > 2048))) {
		ERR("invalid destination resolution\n");
		return	-EINVAL;
	}

	//check src_vir_w
	if(unlikely(req->src.vir_w < req->src.act_w)){
		ERR("invalid src_vir_w\n");
		return	-EINVAL;
	}

	//check dst_vir_w
	if(unlikely(req->dst.vir_w < req->dst.act_w)){
		ERR("invalid dst_vir_w\n");
		return	-EINVAL;
	}

	//check src address
	if (unlikely(req->src.yrgb_addr == 0))
	{
		ERR("could not retrieve src image from memory\n");
		return	-EINVAL;
	}
    
	//check src address
	if (unlikely(req->dst.yrgb_addr == 0))
	{
		ERR("could not retrieve dst image from memory\n");
		return	-EINVAL;
	}
	#endif
	
    	
	return 0;
}

static void rga_copy_reg(struct rga_reg *reg, uint32_t offset)
{   
    uint32_t i;
    uint32_t *cmd_buf;
    uint32_t *reg_p;
    
    atomic_add(1, &rga_service.cmd_num);
	atomic_add(1, &reg->session->task_running);
    
    cmd_buf = (uint32_t *)rga_service.cmd_buff + offset*28;
    reg_p = (uint32_t *)reg->cmd_reg;

    for(i=0; i<28; i++)
    {
        cmd_buf[i] = reg_p[i];
    }
    
    dsb();
}


static struct rga_reg * rga_reg_init(rga_session *session, struct rga_req *req)
{
    unsigned long flag;
    uint32_t ret;
	struct rga_reg *reg = kmalloc(sizeof(struct rga_reg), GFP_KERNEL);
	if (NULL == reg) {
		pr_err("kmalloc fail in rga_reg_init\n");
		return NULL;
	}

    reg->session = session;
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

    if (req->mmu_info.mmu_en)
    {
        ret = rga_set_mmu_info(reg, req);
        if(ret < 0) 
        {
            printk("%s, [%d] set mmu info error \n", __FUNCTION__, __LINE__);
            if(reg != NULL) 
            {            
                kfree(reg);
            }
            return NULL; 
        }
    }
    
    RGA_gen_reg_info(req, (uint8_t *)reg->cmd_reg);

    spin_lock_irqsave(&rga_service.lock, flag);
	list_add_tail(&reg->status_link, &rga_service.waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	spin_unlock_irqrestore(&rga_service.lock, flag);

    return reg;
}

static struct rga_reg * rga_reg_init_2(rga_session *session, struct rga_req *req0, struct rga_req *req1)
{
    unsigned long flag;
    uint32_t ret;

    struct rga_reg *reg0, *reg1;

    reg0 = NULL;
    reg1 = NULL;

    do
    {    
    	reg0 = kmalloc(sizeof(struct rga_reg), GFP_KERNEL);
    	if (NULL == reg0) {
    		pr_err("%s [%d] kmalloc fail in rga_reg_init\n", __FUNCTION__, __LINE__);
            break;
    	}

        reg1 = kmalloc(sizeof(struct rga_reg), GFP_KERNEL);
    	if (NULL == reg1) {
    		pr_err("%s [%d] kmalloc fail in rga_reg_init\n", __FUNCTION__, __LINE__);
            break;
    	}

        reg0->session = session;
    	INIT_LIST_HEAD(&reg0->session_link);
    	INIT_LIST_HEAD(&reg0->status_link);

        if(req0->mmu_info.mmu_en)
        {
            ret = rga_set_mmu_info(reg0, req0);
            if(ret < 0) {
                printk("%s, [%d] set mmu info error \n", __FUNCTION__, __LINE__);
                break;        
            }
        }
        
        RGA_gen_reg_info(req0, (uint8_t *)reg0->cmd_reg);

        if(req1->mmu_info.mmu_en)
        {
            ret = rga_set_mmu_info(reg0, req1);
            if(ret < 0) {
                printk("%s, [%d] set mmu info error \n", __FUNCTION__, __LINE__);
                break;        
            }
        }
        
        RGA_gen_reg_info(req1, (uint8_t *)reg0->cmd_reg);

        spin_lock_irqsave(&rga_service.lock, flag);
    	list_add_tail(&reg0->status_link, &rga_service.waiting);
        list_add_tail(&reg1->status_link, &rga_service.waiting);
    	list_add_tail(&reg0->session_link, &session->waiting);    
    	list_add_tail(&reg1->session_link, &session->waiting);
    	spin_unlock_irqrestore(&rga_service.lock, flag);

        return reg1;
    }
    while(0);

    if(reg0 != NULL) {
        kfree(reg0);
    }

    if(reg1 != NULL) {
        kfree(reg1);
    }

    return NULL;
}


static void rga_reg_deinit(struct rga_reg *reg)
{
	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	kfree(reg);	
}

static void rga_reg_from_wait_to_run(struct rga_reg *reg)
{
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &rga_service.running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
}

static void rga_service_session_clear(rga_session *session)
{
	struct rga_reg *reg, *n;

    list_for_each_entry_safe(reg, n, &session->waiting, session_link) 
    {
		rga_reg_deinit(reg);
	}

    list_for_each_entry_safe(reg, n, &session->running, session_link) 
    {
		rga_reg_deinit(reg);
	}
}


static void rga_try_set_reg(uint32_t num)
{
    unsigned long flag;
    
	// first get reg from reg list

    if (!num)
    {
        printk("rga try set reg cmd num is 0\n");
        return;
    }
    
	spin_lock_irqsave(&rga_service.lock, flag);
	if (!list_empty(&rga_service.waiting)) 
    {
        do
        {
            struct rga_reg *reg = list_entry(rga_service.waiting.next, struct rga_reg, status_link);            
            if((rga_read(RGA_STATUS) & 0x1)) 
            {            
                /* RGA is busy */
                if((atomic_read(&rga_service.cmd_num) <= 0xf) && (atomic_read(&rga_service.int_disable) == 0)) 
                {
                    rga_copy_reg(reg, atomic_read(&rga_service.cmd_num));                
                    rga_reg_from_wait_to_run(reg);
                    
                    rga_write(0x1<<10, RGA_INT);
                    
                    atomic_set(&reg->session->done, 0);
                    
                    rga_write((0x1<<3)|(0x1<<1), RGA_CMD_CTRL);
                    
                    if(atomic_read(&reg->int_enable))
                        atomic_set(&rga_service.int_disable, 1);
                }
            }
            else 
            {  
                /* RGA is idle */
                rga_copy_reg(reg, 0);            
                rga_reg_from_wait_to_run(reg);
                dmac_flush_range(&rga_service.cmd_buff[0], &rga_service.cmd_buff[28]);


                /*  
                 *  if cmd buf must use mmu
                 *  it should be writed before cmd start  
                 */
                rga_write((2<<4)|0x1, RGA_MMU_CTRL);
                rga_write(virt_to_phys(reg->MMU_base)>>2, RGA_MMU_TBL);
                                
                /* CMD buff */
                rga_write(virt_to_phys(rga_service.cmd_buff) & (~PAGE_MASK), RGA_CMD_ADDR);

                /* master mode */
                rga_write(0x1<<2, RGA_SYS_CTRL);

                /* All CMD finish int */
                rga_write(0x1<<10, RGA_INT);

                /* Start proc */
                atomic_set(&reg->session->done, 0);
                rga_write(0x1, RGA_CMD_CTRL);

            }
            num--;
        }
        while(num);
	}
	spin_unlock_irqrestore(&rga_service.lock, flag);
}



static int rga_blit_async(rga_session *session, struct rga_req *req)
{
	int ret = -1;
    struct rga_reg *reg;
    struct rga_req *req2;

    uint32_t saw, sah, daw, dah;
            
    saw = req->src.act_w;
    sah = req->src.act_h;
    daw = req->dst.act_w;
    dah = req->dst.act_h;

    if((req->render_mode == bitblt_mode) && (((saw>>1) >= daw) || ((sah>>1) >= dah))) 
    {
        /* generate 2 cmd for pre scale */
        
        req2 = kmalloc(sizeof(struct rga_req), GFP_KERNEL);
        if(NULL == req2) {
            return -EINVAL;            
        }

        RGA_gen_two_pro(req, req2);

        reg = rga_reg_init_2(session, req2, req);
        if(reg == NULL) {
            return -EFAULT;
        }
        
        atomic_set(&reg->int_enable, 1);

        rga_try_set_reg(2);

        if(req2 != NULL)
        {
            kfree(req2);
        }

    }
    else {
        /* check value if legal */
        ret = rga_check_param(req);
    	if(ret == -EINVAL) {
            return -EINVAL;
    	}
        
        reg = rga_reg_init(session, req);
        if(reg == NULL) {
            return -EFAULT;
        }
        
        rga_try_set_reg(1);        
    }
   
	//printk("rga_blit_async done******************\n");

#if 0	
error_status:
error_scale:
	ret = -EINVAL;
	rga_soft_reset();
	rga_power_off();
#endif    
	return ret;    

}

static int rga_blit_sync(rga_session *session, struct rga_req *req)
{
    int ret = 0;
    struct rga_reg *reg;
    struct rga_req *req2;

    uint32_t saw, sah, daw, dah;
        
    saw = req->src.act_w;
    sah = req->src.act_h;
    daw = req->dst.act_w;
    dah = req->dst.act_h;

    if((req->render_mode == bitblt_mode) && (((saw>>1) >= daw) || ((sah>>1) >= dah))) 
    {
        /* generate 2 cmd for pre scale */
        
        req2 = kmalloc(sizeof(struct rga_req), GFP_KERNEL);
        if(NULL == req2) {
            return -EINVAL;            
        }

        RGA_gen_two_pro(req, req2);

        reg = rga_reg_init_2(session, req2, req);
        if(reg == NULL) {
            return -EFAULT;
        }
        
        atomic_set(&reg->int_enable, 1);

        rga_try_set_reg(2);        

    }
    else {
        /* check value if legal */        
        ret = rga_check_param(req);
    	if(ret == -EINVAL) {
    		return -EFAULT;
    	}
        
        reg = rga_reg_init(session, req);
        if(reg == NULL) {
            return -EFAULT;
        }

        atomic_set(&reg->int_enable, 1);
        
        rga_try_set_reg(1);
    }    

    ret = wait_event_interruptible_timeout(session->wait, atomic_read(&session->done), RGA_TIMEOUT_DELAY);
    if (unlikely(ret < 0)) 
    {
		pr_err("pid %d wait task ret %d\n", session->pid, ret);
	} 
    else if (0 == ret) 
    {
		pr_err("pid %d wait %d task done timeout\n", session->pid, atomic_read(&session->task_running));
		ret = -ETIMEDOUT;
	}

    return ret;
   
	//printk("rga_blit_sync done******************\n");
}

static long rga_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
    struct rga_req *req;
	int ret = 0;
    rga_session *session = (rga_session *)file->private_data;
	if (NULL == session) 
    {
        printk("%s [%d] rga thread session is null\n",__FUNCTION__,__LINE__);
		return -EINVAL;
	}

    req = (struct rga_req *)kmalloc(sizeof(struct rga_req), GFP_KERNEL);
    if(req == NULL) 
    {
        printk("%s [%d] get rga_req mem failed\n",__FUNCTION__,__LINE__);
        ret = -EINVAL;
    }
        
    if (unlikely(copy_from_user(&req, (struct rga_req*)arg, sizeof(struct rga_req)))) 
    {
		ERR("copy_from_user failed\n");
		ret = -EFAULT;
	}
    
	switch (cmd)
	{
		case RGA_BLIT_SYNC:
            ret = rga_blit_sync(session, req);
            break;
		case RGA_BLIT_ASYNC:
			ret = rga_blit_async(session, req);            
			break;
		case RGA_FLUSH:
			ret = rga_flush(session, arg);
			break;
        case RGA_GET_RESULT:
            ret = rga_get_result(session, arg);
		default:
			ERR("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}

    if(req != NULL) {
        kfree(req);
    }
    
	return ret;
}

static int rga_open(struct inode *inode, struct file *file)
{
    rga_session *session = (rga_session *)kmalloc(sizeof(rga_session), GFP_KERNEL);
	if (NULL == session) {
		pr_err("unable to allocate memory for rga_session.");
		return -ENOMEM;
	}

	session->pid	= current->pid;
	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	/* no need to protect */
	list_add_tail(&session->list_session, &rga_service.session);
	atomic_set(&session->task_running, 0);
    atomic_set(&session->num_done, 0);
	file->private_data = (void *)session;

	DBG("*** rga dev opened *** \n");
	return nonseekable_open(inode, file);
    
}

static int rga_release(struct inode *inode, struct file *file)
{
    int task_running;
	unsigned long flag;
	rga_session *session = (rga_session *)file->private_data;
	if (NULL == session)
		return -EINVAL;
    
	task_running = atomic_read(&session->task_running);

    while (task_running) 
    {
		pr_err("rga_service session %d still has %d task running when closing\n", session->pid, task_running);
		msleep(50);
        /*Í¬²½*/        
	}
    
	wake_up_interruptible_sync(&session->wait);
	spin_lock_irqsave(&rga_service.lock, flag);
	list_del(&session->list_session);
	rga_service_session_clear(session);
	kfree(session);
	spin_unlock_irqrestore(&rga_service.lock, flag);

	pr_debug("dev closed\n");
	return 0;
}

static irqreturn_t rga_irq(int irq,  void *dev_id)
{
    struct rga_reg *reg;
    uint32_t num = 0;
    struct list_head *next;
    int int_enable = 0;

    DBG("rga_irq %d \n", irq);

    printk("rga_irq is valid\n");

    /*clear INT */
	rga_write(rga_read(RGA_INT) | (0x1<<6), RGA_INT);

    if(((rga_read(RGA_STATUS) & 0x1) != 0))// idle
	{	
		printk("RGA is not idle!\n");
		rga_soft_reset();
	}


    spin_lock(&rga_service.lock);
    do
    {
        reg = list_entry(rga_service.running.next, struct rga_reg, status_link);

        #if 0
        if(reg->MMU_base != NULL)
        {
            kfree(reg->MMU_base);
        }
        #endif
                
        atomic_sub(1, &reg->session->task_running);
	    atomic_sub(1, &rga_service.total_running);
        
        if(list_empty(&reg->session->waiting))
        {
            atomic_set(&reg->session->done, 1);
            wake_up_interruptible_sync(&reg->session->wait);
        }
        
        rga_reg_deinit(reg);
        
    }
    while(!list_empty(&rga_service.running));

    atomic_set(&rga_service.cmd_num, 0);

    spin_unlock(&rga_service.lock);

    next = &rga_service.waiting;
   
    /* add cmd to cmd buf */
    while((!list_empty(next)) && ((int_enable) == 0) && (num <= 0xf))
    {        
        num += 1;
        reg = list_entry(next->next, struct rga_reg, status_link);
        int_enable = atomic_read(&reg->int_enable);        
        next = next->next;
    }

    rga_try_set_reg(num);
   			
	return IRQ_HANDLED;
}

static int rga_suspend(struct platform_device *pdev, pm_message_t state)
{
	uint32_t enable;
    
    enable = drvdata->enable;    
	//rga_power_off(NULL);    
    drvdata->enable = enable;

	return 0;
}

static int rga_resume(struct platform_device *pdev)
{    
    //rga_power_on();    
	return 0;
}

static void rga_shutdown(struct platform_device *pdev)
{
	pr_cont("shutdown...");	    
	//rga_power_off(NULL);    
    pr_cont("done\n");
}



struct file_operations rga_fops = {
	.owner		= THIS_MODULE,
	.open		= rga_open,
	.release	= rga_release,
	.unlocked_ioctl		= rga_ioctl,
};

static struct miscdevice rga_dev ={
    .minor = RGA_MAJOR,
    .name  = "rga",
    .fops  = &rga_fops,
};

static int __devinit rga_drv_probe(struct platform_device *pdev)
{
	struct rga_drvdata *data;
	int ret = 0;

	data = kmalloc(sizeof(struct rga_drvdata), GFP_KERNEL);

    INIT_LIST_HEAD(&rga_service.waiting);
	INIT_LIST_HEAD(&rga_service.running);
	INIT_LIST_HEAD(&rga_service.done);
	INIT_LIST_HEAD(&rga_service.session);
	spin_lock_init(&rga_service.lock);
    atomic_set(&rga_service.total_running, 0);
	rga_service.enabled		= false;    
          
	if(NULL == data)
	{
		ERR("failed to allocate driver data.\n");
		return  -ENOMEM;
	}

    #if 0
	/* get the clock */
	data->pd_display = clk_get(&pdev->dev, "pd_display");
	if (IS_ERR(data->pd_display))
	{
		ERR("failed to find rga pd_display source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->aclk_lcdc = clk_get(&pdev->dev, "aclk_lcdc");
	if (IS_ERR(data->aclk_lcdc))
	{
		ERR("failed to find rga aclk_lcdc source\n");
		ret = -ENOENT;
		goto err_clock;
	}
	
	data->hclk_lcdc = clk_get(&pdev->dev, "hclk_lcdc");
	if (IS_ERR(data->hclk_lcdc))
	{
		ERR("failed to find rga hclk_lcdc source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->aclk_ddr_lcdc = clk_get(&pdev->dev, "aclk_ddr_lcdc");
	if (IS_ERR(data->aclk_ddr_lcdc))
	{
		ERR("failed to find rga aclk_ddr_lcdc source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->hclk_cpu_display = clk_get(&pdev->dev, "hclk_cpu_display");
	if (IS_ERR(data->hclk_cpu_display))
	{
		ERR("failed to find rga hclk_cpu_display source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->aclk_disp_matrix = clk_get(&pdev->dev, "aclk_disp_matrix");
	if (IS_ERR(data->aclk_disp_matrix))
	{
		ERR("failed to find rga aclk_disp_matrix source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->hclk_disp_matrix = clk_get(&pdev->dev, "hclk_disp_matrix");
	if (IS_ERR(data->hclk_disp_matrix))
	{
		ERR("failed to find rga hclk_disp_matrix source\n");
		ret = -ENOENT;
		goto err_clock;
	}
	
	data->axi_clk = clk_get(&pdev->dev, "aclk_rga");
	if (IS_ERR(data->axi_clk))
	{
		ERR("failed to find rga axi clock source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->ahb_clk = clk_get(&pdev->dev, "hclk_rga");
	if (IS_ERR(data->ahb_clk))
	{
		ERR("failed to find rga ahb clock source\n");
		ret = -ENOENT;
		goto err_clock;
	}
    #endif

	/* map the memory */
    if (!request_mem_region(RK30_RGA_PHYS, RK30_RGA_SIZE, "rga_io")) 
    {
		pr_info("failed to reserve rga HW regs\n");
		return -EBUSY;
	}
	data->rga_base = (void*)ioremap_nocache(RK30_RGA_PHYS, RK30_RGA_SIZE);
	if (data->rga_base == NULL)
	{
		ERR("rga ioremap failed\n");
		ret = -ENOENT;
		goto err_ioremap;
	}

	/* get the IRQ */
	data->irq0 = pdev->resource[1].start;
	printk("rga irq %d\n", data->irq0);
	if (data->irq0 <= 0)
	{
		ERR("failed to get rga irq resource (%d).\n", data->irq0);
		ret = data->irq0;
		goto err_irq;
	}

	/* request the IRQ */
	ret = request_irq(data->irq0, rga_irq, 0/*IRQF_DISABLED*/, "rga", pdev);
	if (ret)
	{
		ERR("rga request_irq failed (%d).\n", ret);
		goto err_irq;
	}

	mutex_init(&data->mutex);
	data->enable = false;
	INIT_DELAYED_WORK(&data->power_off_work, rga_power_off);
	data->rga_irq_callback = NULL;
	
	platform_set_drvdata(pdev, data);
	drvdata = data;

	ret = misc_register(&rga_dev);
	if(ret)
	{
		ERR("cannot register miscdev (%d)\n", ret);
		goto err_misc_register;
	}
	DBG("RGA Driver loaded succesfully\n");

	return 0;    

err_misc_register:
	free_irq(data->irq0, pdev);
err_irq:
	iounmap(data->rga_base);
err_ioremap:
err_clock:
	kfree(data);

	return ret;
}

static int rga_drv_remove(struct platform_device *pdev)
{
	struct rga_drvdata *data = platform_get_drvdata(pdev);
    DBG("%s [%d]\n",__FUNCTION__,__LINE__);

    misc_deregister(&(data->miscdev));
	free_irq(data->irq0, &data->miscdev);
    iounmap((void __iomem *)(data->rga_base));

            

    #if 0    
	if(data->axi_clk) {
		clk_put(data->axi_clk);
	}
    
	if(data->ahb_clk) {
		clk_put(data->ahb_clk);
	}
	if(data->aclk_disp_matrix) {
		clk_put(data->aclk_disp_matrix);
	}

	if(data->hclk_disp_matrix) {
		clk_put(data->hclk_disp_matrix);
	}
	
	if(data->aclk_ddr_lcdc) {
		clk_put(data->aclk_ddr_lcdc);
	}
	
	if(data->hclk_lcdc) {
		clk_put(data->hclk_lcdc);
	}

	if(data->aclk_lcdc) {
		clk_put(data->aclk_lcdc);
	}
	
	if(data->hclk_cpu_display) {
		clk_put(data->hclk_cpu_display);
	}

	if(data->pd_display){
		clk_put(data->pd_display);
	}
    #endif

    kfree(data);
    return 0;
}

static struct platform_driver rga_driver = {
	.probe		= rga_drv_probe,
	.remove		= __devexit_p(rga_drv_remove),
	.suspend    = rga_suspend,
	.resume     = rga_resume,
	.shutdown   = rga_shutdown,
	.driver		= {
		.owner  = THIS_MODULE,
		.name	= "rga",
	},
};

static int __init rga_init(void)
{
	int ret;
    uint32_t *mmu_buf;
    uint32_t i;
    uint32_t *buf_p;

    /* malloc pre scale mid buf mmu table */
    mmu_buf = (uint32_t *)kmalloc(1024*8, GFP_KERNEL);    
    if(mmu_buf == NULL) 
    {
        ERR("RGA get Pre Scale buff failed. \n");
        return -1;
    }

    /* malloc 8 M buf */
    for(i=0; i<2048; i++)
    {        
        buf_p = (uint32_t *)__get_free_page(GFP_KERNEL);               
        if(buf_p == NULL)
        {
            printk("RGA init pre scale buf falied\n");
            return -ENOMEM;
        }
        
        mmu_buf[i] = virt_to_phys((void *)((uint32_t)buf_p));        
    }
        
    rga_service.pre_scale_buf = (uint32_t *)mmu_buf;    

	if ((ret = platform_driver_register(&rga_driver)) != 0)
	{
        ERR("Platform device register failed (%d).\n", ret);
			return ret;
	}
    
	INFO("Module initialized.\n");  
    
	return 0;
}

static void __exit rga_exit(void)
{
    uint32_t i;

    for(i=0; i<2048; i++)
    {
        if((uint32_t *)rga_service.pre_scale_buf[i] != NULL)
        {
            __free_page((void *)rga_service.pre_scale_buf[i]);
        }        
    }
    
    if(rga_service.pre_scale_buf != NULL) {
        kfree((uint8_t *)rga_service.pre_scale_buf);
    }
	platform_driver_unregister(&rga_driver); 
}


module_init(rga_init);
module_exit(rga_exit);


/* Module information */
MODULE_AUTHOR("zsq@rock-chips.com");
MODULE_DESCRIPTION("Driver for rga device");
MODULE_LICENSE("GPL");
