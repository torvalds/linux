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

#define pr_fmt(fmt) "rga: " fmt
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
//#include <mach/io.h>
//#include <mach/irqs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <asm/cacheflush.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/wakelock.h>
#include <linux/dma-buf.h>
#include <linux/pm_runtime.h>
#include <linux/version.h>
#if defined(CONFIG_ION_ROCKCHIP)
#include <linux/rockchip_ion.h>
#endif


#include "rga.h"
#include "rga_reg_info.h"
#include "rga_mmu_info.h"
#include "RGA_API.h"

#define RGA_TEST_CASE 0

#define RGA_TEST 0
#define RGA_TEST_TIME 0
#define RGA_TEST_FLUSH_TIME 0
#define RGA_INFO_BUS_ERROR 1

#define PRE_SCALE_BUF_SIZE  2048*1024*4

#define RGA_POWER_OFF_DELAY	4*HZ /* 4s */
#define RGA_TIMEOUT_DELAY	2*HZ /* 2s */

#define RGA_MAJOR		255

#if defined(CONFIG_ARCH_RK2928) || defined(CONFIG_ARCH_RK3026)
#define RK30_RGA_PHYS		RK2928_RGA_PHYS
#define RK30_RGA_SIZE		RK2928_RGA_SIZE
#endif
#define RGA_RESET_TIMEOUT	1000

/* Driver information */
#define DRIVER_DESC		"RGA Device Driver"
#define DRIVER_NAME		"rga"


ktime_t rga_start;
ktime_t rga_end;

rga_session rga_session_global;

long (*rga_ioctl_kernel_p)(struct rga_req *);


struct rga_drvdata {
  	struct miscdevice miscdev;
	struct device *dev;
	void *rga_base;
	int irq;

	struct delayed_work power_off_work;
	void (*rga_irq_callback)(int rga_retval);   //callback function used by aync call
	struct wake_lock wake_lock;

    struct clk *pd_rga;
	struct clk *aclk_rga;
    struct clk *hclk_rga;

    //#if defined(CONFIG_ION_ROCKCHIP)
    struct ion_client * ion_client;
    //#endif
	char *version;
};

static struct rga_drvdata *drvdata;
rga_service_info rga_service;
struct rga_mmu_buf_t rga_mmu_buf;


#if defined(CONFIG_ION_ROCKCHIP)
extern struct ion_client *rockchip_ion_client_create(const char * name);
#endif

static int rga_blit_async(rga_session *session, struct rga_req *req);
static void rga_del_running_list(void);
static void rga_del_running_list_timeout(void);
static void rga_try_set_reg(void);


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

#if RGA_TEST
static void print_info(struct rga_req *req)
{
	printk(KERN_ERR "src : yrgb_addr = %.lx, src.uv_addr = %.lx, src.v_addr = %.lx, format = %d\n",
            req->src.yrgb_addr, req->src.uv_addr, req->src.v_addr, req->src.format);
    printk("src : act_w = %d, act_h = %d, vir_w = %d, vir_h = %d\n",
        req->src.act_w, req->src.act_h, req->src.vir_w, req->src.vir_h);
    printk("src : x_off = %.8x y_off = %.8x\n", req->src.x_offset, req->src.y_offset);

    printk("dst : yrgb_addr = %.8x, dst.uv_addr = %.8x, dst.v_addr = %.8x, format = %d\n",
            req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr, req->dst.format);
    printk("dst : x_off = %.8x y_off = %.8x\n", req->dst.x_offset, req->dst.y_offset);
    printk("dst : act_w = %d, act_h = %d, vir_w = %d, vir_h = %d\n",
        req->dst.act_w, req->dst.act_h, req->dst.vir_w, req->dst.vir_h);

    printk("clip.xmin = %d, clip.xmax = %d. clip.ymin = %d, clip.ymax = %d\n",
        req->clip.xmin, req->clip.xmax, req->clip.ymin, req->clip.ymax);

    printk("mmu_flag = %.8x\n", req->mmu_info.mmu_flag);

	printk(KERN_ERR "alpha_rop_flag = %.8x\n", req->alpha_rop_flag);
	printk(KERN_ERR "alpha_rop_mode = %.8x\n", req->alpha_rop_mode);
    //printk("PD_mode = %.8x\n", req->PD_mode);
}
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

	for(i = 0; i < RGA_RESET_TIMEOUT; i++)
	{
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
    struct rga_reg *reg, *reg_tmp;
    rga_session *session, *session_tmp;

	running = atomic_read(&rga_service.total_running);
	printk("rga total_running %d\n", running);

    #if 0

    /* Dump waiting list info */
    if (!list_empty(&rga_service.waiting))
    {
        list_head	*next;

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

        list_head	*next;

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
    #endif

	list_for_each_entry_safe(session, session_tmp, &rga_service.session, list_session)
    {
		printk("session pid %d:\n", session->pid);
		running = atomic_read(&session->task_running);
		printk("task_running %d\n", running);
		list_for_each_entry_safe(reg, reg_tmp, &session->waiting, session_link)
        {
			printk("waiting register set 0x %.lu\n", (unsigned long)reg);
		}
		list_for_each_entry_safe(reg, reg_tmp, &session->running, session_link)
        {
			printk("running register set 0x %.lu\n", (unsigned long)reg);
		}
	}
}

static inline void rga_queue_power_off_work(void)
{
	queue_delayed_work(system_wq, &drvdata->power_off_work, RGA_POWER_OFF_DELAY);
}

/* Caller must hold rga_service.lock */
static void rga_power_on(void)
{
	static ktime_t last;
	ktime_t now = ktime_get();

	if (ktime_to_ns(ktime_sub(now, last)) > NSEC_PER_SEC) {
		cancel_delayed_work_sync(&drvdata->power_off_work);
		rga_queue_power_off_work();
		last = now;
	}
	if (rga_service.enable)
		return;

	clk_prepare_enable(drvdata->aclk_rga);
	clk_prepare_enable(drvdata->hclk_rga);
	//clk_prepare_enable(drvdata->pd_rga);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_get_sync(drvdata->dev);
#endif

	wake_lock(&drvdata->wake_lock);
	rga_service.enable = true;
}

/* Caller must hold rga_service.lock */
static void rga_power_off(void)
{
	int total_running;

	if (!rga_service.enable) {
		return;
	}

	total_running = atomic_read(&rga_service.total_running);
	if (total_running) {
		pr_err("power off when %d task running!!\n", total_running);
		mdelay(50);
		pr_err("delay 50 ms for running task\n");
		rga_dump();
	}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_put(drvdata->dev);
#endif

	//clk_disable_unprepare(drvdata->pd_rga);
	clk_disable_unprepare(drvdata->aclk_rga);
	clk_disable_unprepare(drvdata->hclk_rga);
	wake_unlock(&drvdata->wake_lock);
	rga_service.enable = false;
}

static void rga_power_off_work(struct work_struct *work)
{
	if (mutex_trylock(&rga_service.lock)) {
		rga_power_off();
		mutex_unlock(&rga_service.lock);
	} else {
		/* Come back later if the device is busy... */

		rga_queue_power_off_work();
	}
}

static int rga_flush(rga_session *session, unsigned long arg)
{
    int ret = 0;
    int ret_timeout;

    #if RGA_TEST_FLUSH_TIME
    ktime_t start;
    ktime_t end;
    start = ktime_get();
    #endif

    ret_timeout = wait_event_timeout(session->wait, atomic_read(&session->done), RGA_TIMEOUT_DELAY);

	if (unlikely(ret_timeout < 0)) {
		//pr_err("flush pid %d wait task ret %d\n", session->pid, ret);
        mutex_lock(&rga_service.lock);
        rga_del_running_list();
        mutex_unlock(&rga_service.lock);
        ret = ret_timeout;
	} else if (0 == ret_timeout) {
		//pr_err("flush pid %d wait %d task done timeout\n", session->pid, atomic_read(&session->task_running));
        //printk("bus  = %.8x\n", rga_read(RGA_INT));
        mutex_lock(&rga_service.lock);
        rga_del_running_list_timeout();
        rga_try_set_reg();
        mutex_unlock(&rga_service.lock);
		ret = -ETIMEDOUT;
	}

    #if RGA_TEST_FLUSH_TIME
    end = ktime_get();
    end = ktime_sub(end, start);
    printk("one flush wait time %d\n", (int)ktime_to_us(end));
    #endif

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
			ret =  -EFAULT;
		}
	return ret;
}


static int rga_check_param(const struct rga_req *req)
{
	/*RGA can support up to 8192*8192 resolution in RGB format,but we limit the image size to 8191*8191 here*/
	//check src width and height

    if(!((req->render_mode == color_fill_mode) || (req->render_mode == line_point_drawing_mode)))
    {
    	if (unlikely((req->src.act_w <= 0) || (req->src.act_w > 8191) || (req->src.act_h <= 0) || (req->src.act_h > 8191)))
        {
    		printk("invalid source resolution act_w = %d, act_h = %d\n", req->src.act_w, req->src.act_h);
    		return  -EINVAL;
    	}
    }

    if(!((req->render_mode == color_fill_mode) || (req->render_mode == line_point_drawing_mode)))
    {
    	if (unlikely((req->src.vir_w <= 0) || (req->src.vir_w > 8191) || (req->src.vir_h <= 0) || (req->src.vir_h > 8191)))
        {
    		printk("invalid source resolution vir_w = %d, vir_h = %d\n", req->src.vir_w, req->src.vir_h);
    		return  -EINVAL;
    	}
    }

	//check dst width and height
	if (unlikely((req->dst.act_w <= 0) || (req->dst.act_w > 2048) || (req->dst.act_h <= 0) || (req->dst.act_h > 2048)))
    {
		printk("invalid destination resolution act_w = %d, act_h = %d\n", req->dst.act_w, req->dst.act_h);
		return	-EINVAL;
	}

    if (unlikely((req->dst.vir_w <= 0) || (req->dst.vir_w > 4096) || (req->dst.vir_h <= 0) || (req->dst.vir_h > 2048)))
    {
		printk("invalid destination resolution vir_w = %d, vir_h = %d\n", req->dst.vir_w, req->dst.vir_h);
		return	-EINVAL;
	}

	//check src_vir_w
	if(unlikely(req->src.vir_w < req->src.act_w)){
		printk("invalid src_vir_w act_w = %d, vir_w = %d\n", req->src.act_w, req->src.vir_w);
		return	-EINVAL;
	}

	//check dst_vir_w
	if(unlikely(req->dst.vir_w < req->dst.act_w)){
        if(req->rotate_mode != 1)
        {
		    printk("invalid dst_vir_w act_h = %d, vir_h = %d\n", req->dst.act_w, req->dst.vir_w);
		    return	-EINVAL;
        }
	}

	return 0;
}

static void rga_copy_reg(struct rga_reg *reg, uint32_t offset)
{
    uint32_t i;
    uint32_t *cmd_buf;
    uint32_t *reg_p;

    if(atomic_read(&reg->session->task_running) != 0)
    {
        printk(KERN_ERR "task_running is no zero\n");
    }

    atomic_add(1, &rga_service.cmd_num);
	atomic_add(1, &reg->session->task_running);

    cmd_buf = (uint32_t *)rga_service.cmd_buff + offset*32;
    reg_p = (uint32_t *)reg->cmd_reg;

    for(i=0; i<32; i++)
        cmd_buf[i] = reg_p[i];

}


static struct rga_reg * rga_reg_init(rga_session *session, struct rga_req *req)
{
    int32_t ret;
	struct rga_reg *reg = kzalloc(sizeof(struct rga_reg), GFP_KERNEL);
	if (NULL == reg) {
		pr_err("kmalloc fail in rga_reg_init\n");
		return NULL;
	}

    reg->session = session;
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

    reg->MMU_base = NULL;

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

    if(RGA_gen_reg_info(req, (uint8_t *)reg->cmd_reg) == -1)
    {
        printk("gen reg info error\n");
        if(reg != NULL)
        {
            kfree(reg);
        }
        return NULL;
    }

	reg->sg_src = req->sg_src;
	reg->sg_dst = req->sg_dst;
	reg->attach_src = req->attach_src;
	reg->attach_dst = req->attach_dst;

    mutex_lock(&rga_service.lock);
	list_add_tail(&reg->status_link, &rga_service.waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	mutex_unlock(&rga_service.lock);

    return reg;
}

/* Caller must hold rga_service.lock */
static void rga_reg_deinit(struct rga_reg *reg)
{
	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	kfree(reg);
}

/* Caller must hold rga_service.lock */
static void rga_reg_from_wait_to_run(struct rga_reg *reg)
{
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &rga_service.running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
}

/* Caller must hold rga_service.lock */
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

/* Caller must hold rga_service.lock */
static void rga_try_set_reg(void)
{
    struct rga_reg *reg ;

    if (list_empty(&rga_service.running))
    {
        if (!list_empty(&rga_service.waiting))
        {
            /* RGA is idle */
            reg = list_entry(rga_service.waiting.next, struct rga_reg, status_link);

            rga_power_on();
            udelay(1);

            rga_copy_reg(reg, 0);
            rga_reg_from_wait_to_run(reg);

            #ifdef CONFIG_ARM
            dmac_flush_range(&rga_service.cmd_buff[0], &rga_service.cmd_buff[32]);
            outer_flush_range(virt_to_phys(&rga_service.cmd_buff[0]),virt_to_phys(&rga_service.cmd_buff[32]));
            #elif defined(CONFIG_ARM64)
            __dma_flush_range(&rga_service.cmd_buff[0], &rga_service.cmd_buff[32]);
            #endif

            rga_soft_reset();

            rga_write(0x0, RGA_SYS_CTRL);
            rga_write(0, RGA_MMU_CTRL);

            /* CMD buff */
            rga_write(virt_to_phys(rga_service.cmd_buff), RGA_CMD_ADDR);

#if RGA_TEST
            {
                //printk(KERN_DEBUG "cmd_addr = %.8x\n", rga_read(RGA_CMD_ADDR));
                uint32_t i;
                uint32_t *p;
                p = rga_service.cmd_buff;
                printk("CMD_REG\n");
                for (i=0; i<7; i++)
                    printk("%.8x %.8x %.8x %.8x\n", p[0 + i*4], p[1+i*4], p[2 + i*4], p[3 + i*4]);
                printk("%.8x %.8x\n", p[0 + i*4], p[1+i*4]);
            }
#endif

            /* master mode */
            rga_write((0x1<<2)|(0x1<<3), RGA_SYS_CTRL);

            /* All CMD finish int */
            rga_write(rga_read(RGA_INT)|(0x1<<10)|(0x1<<8), RGA_INT);

            #if RGA_TEST_TIME
            rga_start = ktime_get();
            #endif

            /* Start proc */
            atomic_set(&reg->session->done, 0);
            rga_write(0x1, RGA_CMD_CTRL);

#if RGA_TEST
            {
                uint32_t i;
                printk("CMD_READ_BACK_REG\n");
                for (i=0; i<7; i++)
                    printk("%.8x %.8x %.8x %.8x\n", rga_read(0x100 + i*16 + 0),
                            rga_read(0x100 + i*16 + 4), rga_read(0x100 + i*16 + 8), rga_read(0x100 + i*16 + 12));
                printk("%.8x %.8x\n", rga_read(0x100 + i*16 + 0), rga_read(0x100 + i*16 + 4));
            }
#endif
        }
    }
}


static int rga_put_dma_buf(struct rga_req *req, struct rga_reg *reg)
{
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sgt = NULL;
	struct dma_buf *dma_buf = NULL;

	if (!req && !reg)
		return -EINVAL;

	attach = (!reg) ? req->attach_src : reg->attach_src;
	sgt = (!reg) ? req->sg_src : reg->sg_src;
	if (attach && sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if (attach) {
		dma_buf = attach->dmabuf;
		dma_buf_detach(dma_buf, attach);
		dma_buf_put(dma_buf);
	}

	attach = (!reg) ? req->attach_dst : reg->attach_dst;
	sgt = (!reg) ? req->sg_dst : reg->sg_dst;
	if (attach && sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if (attach) {
		dma_buf = attach->dmabuf;
		dma_buf_detach(dma_buf, attach);
		dma_buf_put(dma_buf);
	}

	return 0;
}

/* Caller must hold rga_service.lock */
static void rga_del_running_list(void)
{
    struct rga_reg *reg;

    while(!list_empty(&rga_service.running))
    {
        reg = list_entry(rga_service.running.next, struct rga_reg, status_link);

        if(reg->MMU_len != 0)
        {
            if (rga_mmu_buf.back + reg->MMU_len > 2*rga_mmu_buf.size)
                rga_mmu_buf.back = reg->MMU_len + rga_mmu_buf.size;
            else
                rga_mmu_buf.back += reg->MMU_len;
        }

		rga_put_dma_buf(NULL, reg);

        atomic_sub(1, &reg->session->task_running);
        atomic_sub(1, &rga_service.total_running);

        if(list_empty(&reg->session->waiting))
        {
            atomic_set(&reg->session->done, 1);
            wake_up(&reg->session->wait);
        }

        rga_reg_deinit(reg);
    }
}

/* Caller must hold rga_service.lock */
static void rga_del_running_list_timeout(void)
{
    struct rga_reg *reg;

    while(!list_empty(&rga_service.running))
    {
        reg = list_entry(rga_service.running.next, struct rga_reg, status_link);
        
        if(reg->MMU_len != 0)
        {
            if (rga_mmu_buf.back + reg->MMU_len > 2*rga_mmu_buf.size)
                rga_mmu_buf.back = reg->MMU_len + rga_mmu_buf.size;
            else
                rga_mmu_buf.back += reg->MMU_len;
        }

		rga_put_dma_buf(NULL, reg);

        atomic_sub(1, &reg->session->task_running);
        atomic_sub(1, &rga_service.total_running);

        //printk("RGA soft reset for timeout process\n");
        rga_soft_reset();


        #if 0
        printk("RGA_INT is %.8x\n", rga_read(RGA_INT));
        printk("reg->session->task_running = %d\n", atomic_read(&reg->session->task_running));
        printk("rga_service.total_running  = %d\n", atomic_read(&rga_service.total_running));

        print_info(&reg->req);

        {
            uint32_t *p, i;
            p = reg->cmd_reg;
            for (i=0; i<7; i++)
                printk("%.8x %.8x %.8x %.8x\n", p[0 + i*4], p[1+i*4], p[2 + i*4], p[3 + i*4]);

        }
        #endif

        if(list_empty(&reg->session->waiting))
        {
            atomic_set(&reg->session->done, 1);
            wake_up(&reg->session->wait);
        }

        rga_reg_deinit(reg);
    }
}

/*
static int rga_convert_dma_buf(struct rga_req *req)
{
	struct ion_handle *hdl;
	ion_phys_addr_t phy_addr;
	size_t len;
    int ret;
    uint32_t src_offset, dst_offset;

    req->sg_src  = NULL;
    req->sg_dst  = NULL;

	  src_offset = req->line_draw_info.flag;
	  dst_offset = req->line_draw_info.line_width;

    if(req->src.yrgb_addr) {
        hdl = ion_import_dma_buf(drvdata->ion_client, req->src.yrgb_addr);
        if (IS_ERR(hdl)) {
            ret = PTR_ERR(hdl);
            printk("RGA2 ERROR ion buf handle\n");
            return ret;
        }
        if ((req->mmu_info.mmu_flag >> 8) & 1) {
            req->sg_src = ion_sg_table(drvdata->ion_client, hdl);
            req->src.yrgb_addr = req->src.uv_addr;
            req->src.uv_addr = req->src.yrgb_addr + (req->src.vir_w * req->src.vir_h);
            req->src.v_addr = req->src.uv_addr + (req->src.vir_w * req->src.vir_h)/4;
        }
        else {
            ion_phys(drvdata->ion_client, hdl, &phy_addr, &len);
            req->src.yrgb_addr = phy_addr + src_offset;
            req->src.uv_addr = req->src.yrgb_addr + (req->src.vir_w * req->src.vir_h);
            req->src.v_addr = req->src.uv_addr + (req->src.vir_w * req->src.vir_h)/4;
        }
        ion_free(drvdata->ion_client, hdl);
    }
    else {
        req->src.yrgb_addr = req->src.uv_addr;
        req->src.uv_addr = req->src.yrgb_addr + (req->src.vir_w * req->src.vir_h);
        req->src.v_addr = req->src.uv_addr + (req->src.vir_w * req->src.vir_h)/4;
    }

    if(req->dst.yrgb_addr) {
        hdl = ion_import_dma_buf(drvdata->ion_client, req->dst.yrgb_addr);
        if (IS_ERR(hdl)) {
            ret = PTR_ERR(hdl);
            printk("RGA2 ERROR ion buf handle\n");
            return ret;
        }
        if ((req->mmu_info.mmu_flag >> 10) & 1) {
            req->sg_dst = ion_sg_table(drvdata->ion_client, hdl);
            req->dst.yrgb_addr = req->dst.uv_addr;
            req->dst.uv_addr = req->dst.yrgb_addr + (req->dst.vir_w * req->dst.vir_h);
            req->dst.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h)/4;
        }
        else {
            ion_phys(drvdata->ion_client, hdl, &phy_addr, &len);
            req->dst.yrgb_addr = phy_addr + dst_offset;
            req->dst.uv_addr = req->dst.yrgb_addr + (req->dst.vir_w * req->dst.vir_h);
            req->dst.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h)/4;
        }
        ion_free(drvdata->ion_client, hdl);
    }
    else {
        req->dst.yrgb_addr = req->dst.uv_addr;
        req->dst.uv_addr = req->dst.yrgb_addr + (req->dst.vir_w * req->dst.vir_h);
        req->dst.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h)/4;
    }

    return 0;
}
*/

static int rga_get_img_info(rga_img_info_t *img,
			     u8 mmu_flag,
			     struct sg_table **psgt,
			     struct dma_buf_attachment **pattach)
{
	struct dma_buf_attachment *attach = NULL;
	struct device *rga_dev = NULL;
	struct sg_table *sgt = NULL;
	struct dma_buf *dma_buf = NULL;
	u32 vir_w, vir_h;
	int yrgb_addr = -1;
	int ret = 0;

	rga_dev = drvdata->dev;
	yrgb_addr = (int)img->yrgb_addr;
	vir_w = img->vir_w;
	vir_h = img->vir_h;

	if (yrgb_addr > 0) {
		dma_buf = dma_buf_get(img->yrgb_addr);
		if (IS_ERR(dma_buf)) {
			ret = -EINVAL;
			pr_err("dma_buf_get fail fd[%d]\n", yrgb_addr);
			return ret;
		}

		attach = dma_buf_attach(dma_buf, rga_dev);
		if (IS_ERR(attach)) {
			dma_buf_put(dma_buf);
			ret = -EINVAL;
			pr_err("Failed to attach dma_buf\n");
			return ret;
		}

		*pattach = attach;
		sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR(sgt)) {
			ret = -EINVAL;
			pr_err("Failed to map src attachment\n");
			goto err_get_sg;
		}
		if (!mmu_flag) {
			ret = -EINVAL;
			pr_err("Fix it please enable iommu flag\n");
			goto err_get_sg;
		}

		if (mmu_flag) {
			*psgt = sgt;
			img->yrgb_addr = img->uv_addr;
			img->uv_addr = img->yrgb_addr + (vir_w * vir_h);
			img->v_addr = img->uv_addr + (vir_w * vir_h) / 4;
		}
	} else {
		img->yrgb_addr = img->uv_addr;
		img->uv_addr = img->yrgb_addr + (vir_w * vir_h);
		img->v_addr = img->uv_addr + (vir_w * vir_h) / 4;
	}

	return ret;

err_get_sg:
	if (sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if (attach) {
		dma_buf = attach->dmabuf;
		dma_buf_detach(dma_buf, attach);
		*pattach = NULL;
		dma_buf_put(dma_buf);
	}
	return ret;
}

static int rga_get_dma_buf(struct rga_req *req)
{
	struct dma_buf *dma_buf = NULL;
	u8 mmu_flag = 0;
	int ret = 0;

	req->sg_src = NULL;
	req->sg_dst = NULL;
	req->attach_src = NULL;
	req->attach_dst = NULL;
	mmu_flag = (req->mmu_info.mmu_flag >> 8) & 1;
	ret = rga_get_img_info(&req->src, mmu_flag, &req->sg_src,
				&req->attach_src);
	if (ret) {
		pr_err("src:rga_get_img_info fail\n");
		goto err_src;
	}

	mmu_flag = (req->mmu_info.mmu_flag >> 10) & 1;
	ret = rga_get_img_info(&req->dst, mmu_flag, &req->sg_dst,
				&req->attach_dst);
	if (ret) {
		pr_err("dst:rga_get_img_info fail\n");
		goto err_dst;
	}

	return ret;

err_dst:
	if (req->sg_src && req->attach_src) {
		dma_buf_unmap_attachment(req->attach_src,
					 req->sg_src, DMA_BIDIRECTIONAL);
		dma_buf = req->attach_src->dmabuf;
		dma_buf_detach(dma_buf, req->attach_src);
		dma_buf_put(dma_buf);
	}
err_src:

	return ret;
}

static int rga_blit(rga_session *session, struct rga_req *req)
{
    int ret = -1;
    int num = 0;
    struct rga_reg *reg;

	uint32_t saw, sah, daw, dah;

	saw = req->src.act_w;
	sah = req->src.act_h;
	daw = req->dst.act_w;
	dah = req->dst.act_h;

    #if RGA_TEST
    print_info(req);
    #endif
	if (rga_get_dma_buf(req)) {
        printk("RGA : DMA buf copy error\n");
        return -EFAULT;
    }
	req->render_mode &= (~RGA_BUF_GEM_TYPE_MASK);
    do {
			if (((saw >> 1) >= daw) || ((sah >> 1) >= dah)) {
				pr_err("unsupported to scaling less than 1/2 \n");
				goto err_put_dma_buf;
			}

			if (((daw >> 3) >= saw) || ((dah >> 3) >= daw)) {
				pr_err("unsupported to scaling more than 8 \n");
				goto err_put_dma_buf;
			}


            /* check value if legal */
            ret = rga_check_param(req);
        	if(ret == -EINVAL) {
                printk("req argument is inval\n");
				goto err_put_dma_buf;
        	}

            reg = rga_reg_init(session, req);
            if(reg == NULL) {
				pr_err("init reg fail\n");
				goto err_put_dma_buf;
            }

            num = 1;

			mutex_lock(&rga_service.lock);
			atomic_add(num, &rga_service.total_running);
			rga_try_set_reg();
			mutex_unlock(&rga_service.lock);

			return 0;
	}
    while(0);

err_put_dma_buf:
	rga_put_dma_buf(req, NULL);

    return -EFAULT;
}

static int rga_blit_async(rga_session *session, struct rga_req *req)
{
	int ret = -1;

    #if RGA_TEST
    printk("*** rga_blit_async proc ***\n");
    #endif

    atomic_set(&session->done, 0);
    ret = rga_blit(session, req);
    return ret;
}

static int rga_blit_sync(rga_session *session, struct rga_req *req)
{
    int ret = -1;
    int ret_timeout = 0;

    #if RGA_TEST
    printk("*** rga_blit_sync proc ***\n");
    #endif

    atomic_set(&session->done, 0);
    ret = rga_blit(session, req);
    if(ret < 0)
        return ret;

    ret_timeout = wait_event_timeout(session->wait, atomic_read(&session->done), RGA_TIMEOUT_DELAY);

    if (unlikely(ret_timeout< 0)) {
        mutex_lock(&rga_service.lock);
        rga_del_running_list();
        mutex_unlock(&rga_service.lock);
        ret = ret_timeout;
	}
    else if (0 == ret_timeout) {
        mutex_lock(&rga_service.lock);
        rga_del_running_list_timeout();
        rga_try_set_reg();
        mutex_unlock(&rga_service.lock);
		ret = -ETIMEDOUT;
	}

    #if RGA_TEST_TIME
    rga_end = ktime_get();
    rga_end = ktime_sub(rga_end, rga_start);
    printk("sync one cmd end time %d\n", (int)ktime_to_us(rga_end));
    #endif

    return ret;
}


static long rga_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
    struct rga_req req;
	int ret = 0;
    rga_session *session;

	memset(&req, 0x0, sizeof(req));
    mutex_lock(&rga_service.mutex);

    session = (rga_session *)file->private_data;

	if (NULL == session) {
        printk("%s [%d] rga thread session is null\n",__FUNCTION__,__LINE__);
        mutex_unlock(&rga_service.mutex);
		return -EINVAL;
	}

    memset(&req, 0x0, sizeof(req));

	switch (cmd) {
		case RGA_BLIT_SYNC:
    		if (unlikely(copy_from_user(&req, (struct rga_req*)arg, sizeof(struct rga_req))))
            {
        		ERR("copy_from_user failed\n");
        		ret = -EFAULT;
                break;
        	}
            ret = rga_blit_sync(session, &req);
            break;
		case RGA_BLIT_ASYNC:
    		if (unlikely(copy_from_user(&req, (struct rga_req*)arg, sizeof(struct rga_req))))
            {
        		ERR("copy_from_user failed\n");
        		ret = -EFAULT;
                break;
        	}

            if((atomic_read(&rga_service.total_running) > 16))
            {
			    ret = rga_blit_sync(session, &req);
            }
            else
            {
                ret = rga_blit_async(session, &req);
            }
			break;
		case RGA_FLUSH:
			ret = rga_flush(session, arg);
			break;
        case RGA_GET_RESULT:
            ret = rga_get_result(session, arg);
            break;
        case RGA_GET_VERSION:
			if (!drvdata->version) {
				drvdata->version = kzalloc(16, GFP_KERNEL);
				if (!drvdata->version) {
					ret = -ENOMEM;
					break;
				}
				rga_power_on();
				udelay(1);
				if (rga_read(RGA_VERSION) == 0x02018632)
					snprintf(drvdata->version, 16, "1.6");
				else
					snprintf(drvdata->version, 16, "1.003");
			}

			ret = copy_to_user((void *)arg, drvdata->version, 16);
            break;
		default:
			ERR("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}

	mutex_unlock(&rga_service.mutex);

	return ret;
}


long rga_ioctl_kernel(struct rga_req *req)
{
	int ret = 0;
    if (!rga_ioctl_kernel_p) {
        printk("rga_ioctl_kernel_p is NULL\n");
        return -1;
    }
    else {
        ret = (*rga_ioctl_kernel_p)(req);
	    return ret;
    }
}


long rga_ioctl_kernel_imp(struct rga_req *req)
{
	int ret = 0;
    rga_session *session;

    mutex_lock(&rga_service.mutex);

    session = &rga_session_global;

	if (NULL == session) {
        printk("%s [%d] rga thread session is null\n",__FUNCTION__,__LINE__);
        mutex_unlock(&rga_service.mutex);
		return -EINVAL;
	}

    ret = rga_blit_sync(session, req);

	mutex_unlock(&rga_service.mutex);

	return ret;
}


static int rga_open(struct inode *inode, struct file *file)
{
    rga_session *session = kzalloc(sizeof(rga_session), GFP_KERNEL);
	if (NULL == session) {
		pr_err("unable to allocate memory for rga_session.");
		return -ENOMEM;
	}

	session->pid = current->pid;
    //printk(KERN_DEBUG  "+");

	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	mutex_lock(&rga_service.lock);
	list_add_tail(&session->list_session, &rga_service.session);
	mutex_unlock(&rga_service.lock);
	atomic_set(&session->task_running, 0);
    atomic_set(&session->num_done, 0);

	file->private_data = (void *)session;

    //DBG("*** rga dev opened by pid %d *** \n", session->pid);
	return nonseekable_open(inode, file);

}

static int rga_release(struct inode *inode, struct file *file)
{
    int task_running;
	rga_session *session = (rga_session *)file->private_data;
	if (NULL == session)
		return -EINVAL;
    //printk(KERN_DEBUG  "-");
	task_running = atomic_read(&session->task_running);

    if (task_running)
    {
		pr_err("rga_service session %d still has %d task running when closing\n", session->pid, task_running);
		msleep(100);
        /*Í¬²½*/
	}

	wake_up(&session->wait);
	mutex_lock(&rga_service.lock);
	list_del(&session->list_session);
	rga_service_session_clear(session);
	kfree(session);
	mutex_unlock(&rga_service.lock);

    //DBG("*** rga dev close ***\n");
	return 0;
}

static irqreturn_t rga_irq_thread(int irq, void *dev_id)
{
	mutex_lock(&rga_service.lock);
	if (rga_service.enable) {
		rga_del_running_list();
		rga_try_set_reg();
	}
	mutex_unlock(&rga_service.lock);

	return IRQ_HANDLED;
}

static irqreturn_t rga_irq(int irq,  void *dev_id)
{
	/*clear INT */
	rga_write(rga_read(RGA_INT) | (0x1<<6) | (0x1<<7) | (0x1<<4), RGA_INT);

	return IRQ_WAKE_THREAD;
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


#if defined(CONFIG_OF)
static const struct of_device_id rockchip_rga_dt_ids[] = {
	{ .compatible = "rockchip,rk312x-rga", },
	{},
};
#endif

static int rga_drv_probe(struct platform_device *pdev)
{
	struct rga_drvdata *data;
    struct resource *res;
    //struct device_node *np = pdev->dev.of_node;
	int ret = 0;

	mutex_init(&rga_service.lock);
	mutex_init(&rga_service.mutex);
	atomic_set(&rga_service.total_running, 0);
	rga_service.enable = false;

    rga_ioctl_kernel_p = rga_ioctl_kernel_imp;

	data = devm_kzalloc(&pdev->dev, sizeof(struct rga_drvdata), GFP_KERNEL);
	if(! data) {
		ERR("failed to allocate driver data.\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&data->power_off_work, rga_power_off_work);
	wake_lock_init(&data->wake_lock, WAKE_LOCK_SUSPEND, "rga");

	//data->pd_rga = devm_clk_get(&pdev->dev, "pd_rga");
    data->aclk_rga = devm_clk_get(&pdev->dev, "aclk_rga");
    data->hclk_rga = devm_clk_get(&pdev->dev, "hclk_rga");

    /* map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->rga_base = devm_ioremap_resource(&pdev->dev, res);
	if (!data->rga_base) {
		ERR("rga ioremap failed\n");
		ret = -ENOENT;
		goto err_ioremap;
	}

	/* get the IRQ */
	data->irq = ret = platform_get_irq(pdev, 0);
	if (ret <= 0) {
		ERR("failed to get rga irq resource (%d).\n", data->irq);
		ret = data->irq;
		goto err_irq;
	}

	/* request the IRQ */
	//ret = request_threaded_irq(data->irq, rga_irq, rga_irq_thread, 0, "rga", pdev);
    ret = devm_request_threaded_irq(&pdev->dev, data->irq, rga_irq, rga_irq_thread, 0, "rga", data);
	if (ret)
	{
		ERR("rga request_irq failed (%d).\n", ret);
		goto err_irq;
	}

	platform_set_drvdata(pdev, data);
	data->dev = &pdev->dev;
	drvdata = data;

    #if defined(CONFIG_ION_ROCKCHIP)
	data->ion_client = rockchip_ion_client_create("rga");
	if (IS_ERR(data->ion_client)) {
		dev_err(&pdev->dev, "failed to create ion client for rga");
		return PTR_ERR(data->ion_client);
	} else {
		dev_info(&pdev->dev, "rga ion client create success!\n");
	}
    #endif

	ret = misc_register(&rga_dev);
	if(ret)
	{
		ERR("cannot register miscdev (%d)\n", ret);
		goto err_misc_register;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_enable(&pdev->dev);
#endif

	pr_info("Driver loaded succesfully\n");

	return 0;

err_misc_register:
	free_irq(data->irq, pdev);
err_irq:
	iounmap(data->rga_base);
err_ioremap:
	wake_lock_destroy(&data->wake_lock);
	//kfree(data);

	return ret;
}

static int rga_drv_remove(struct platform_device *pdev)
{
	struct rga_drvdata *data = platform_get_drvdata(pdev);
	DBG("%s [%d]\n",__FUNCTION__,__LINE__);

	wake_lock_destroy(&data->wake_lock);
	misc_deregister(&(data->miscdev));
	free_irq(data->irq, &data->miscdev);
	iounmap((void __iomem *)(data->rga_base));
	kfree(data->version);

	//clk_put(data->pd_rga);
	devm_clk_put(&pdev->dev, data->aclk_rga);
	devm_clk_put(&pdev->dev, data->hclk_rga);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_disable(&pdev->dev);
#endif

	//kfree(data);
	return 0;
}

static struct platform_driver rga_driver = {
	.probe		= rga_drv_probe,
	.remove		= rga_drv_remove,
	.driver		= {
		.name	= "rga",
		.of_match_table = of_match_ptr(rockchip_rga_dt_ids),
	},
};


void rga_test_0(void);
void rga_test_1(void);


static int __init rga_init(void)
{
	int ret;
    uint32_t *mmu_buf;
    unsigned long *mmu_buf_virtual;
    uint32_t i;
    uint32_t *buf_p;
    uint32_t *buf;

    /* malloc pre scale mid buf mmu table */
    mmu_buf = kzalloc(1024*8, GFP_KERNEL);
    mmu_buf_virtual = kzalloc(1024*2*sizeof(unsigned long), GFP_KERNEL);
    if(mmu_buf == NULL) {
        printk(KERN_ERR "RGA get Pre Scale buff failed. \n");
        return -1;
    }
	if (mmu_buf_virtual == NULL) {
		return -1;
	}

    /* malloc 4 M buf */
    for(i=0; i<1024; i++) {
        buf_p = (uint32_t *)__get_free_page(GFP_KERNEL|__GFP_ZERO);
        if(buf_p == NULL) {
            printk(KERN_ERR "RGA init pre scale buf falied\n");
            return -ENOMEM;
        }
        mmu_buf[i] = virt_to_phys((void *)((unsigned long)buf_p));
        mmu_buf_virtual[i] = (unsigned long)buf_p;
    }

    rga_service.pre_scale_buf = (uint32_t *)mmu_buf;
    rga_service.pre_scale_buf_virtual = (unsigned long *)mmu_buf_virtual;

    buf_p = kmalloc(1024*256, GFP_KERNEL);
    rga_mmu_buf.buf_virtual = buf_p;
#if (defined(CONFIG_ARM) && defined(CONFIG_ARM_LPAE))
    buf = (uint32_t *)(uint32_t)virt_to_phys((void *)((unsigned long)buf_p));
#else
    buf = (uint32_t *)virt_to_phys((void *)((unsigned long)buf_p));
#endif
    rga_mmu_buf.buf = buf;
    rga_mmu_buf.front = 0;
    rga_mmu_buf.back = 64*1024;
    rga_mmu_buf.size = 64*1024;

    rga_mmu_buf.pages = kmalloc((32768)* sizeof(struct page *), GFP_KERNEL);

	if ((ret = platform_driver_register(&rga_driver)) != 0)
	{
        printk(KERN_ERR "Platform device register failed (%d).\n", ret);
			return ret;
	}

    {
        rga_session_global.pid = 0x0000ffff;
        INIT_LIST_HEAD(&rga_session_global.waiting);
        INIT_LIST_HEAD(&rga_session_global.running);
        INIT_LIST_HEAD(&rga_session_global.list_session);

        INIT_LIST_HEAD(&rga_service.waiting);
	    INIT_LIST_HEAD(&rga_service.running);
	    INIT_LIST_HEAD(&rga_service.done);
	    INIT_LIST_HEAD(&rga_service.session);

        init_waitqueue_head(&rga_session_global.wait);
        //mutex_lock(&rga_service.lock);
        list_add_tail(&rga_session_global.list_session, &rga_service.session);
        //mutex_unlock(&rga_service.lock);
        atomic_set(&rga_session_global.task_running, 0);
        atomic_set(&rga_session_global.num_done, 0);
    }



    #if RGA_TEST_CASE
    rga_test_0();
    #endif

	INFO("Module initialized.\n");

	return 0;
}

static void __exit rga_exit(void)
{
    uint32_t i;

    rga_power_off();

    for(i=0; i<1024; i++)
    {
        if((unsigned long)rga_service.pre_scale_buf_virtual[i])
        {
            __free_page((void *)rga_service.pre_scale_buf_virtual[i]);
        }
    }

    if(rga_service.pre_scale_buf != NULL) {
        kfree((uint8_t *)rga_service.pre_scale_buf);
    }

    kfree(rga_mmu_buf.buf_virtual);

    kfree(rga_mmu_buf.pages);

	platform_driver_unregister(&rga_driver);
}


#if RGA_TEST_CASE

extern struct fb_info * rk_get_fb(int fb_id);
EXPORT_SYMBOL(rk_get_fb);

extern void rk_direct_fb_show(struct fb_info * fbi);
EXPORT_SYMBOL(rk_direct_fb_show);

unsigned int src_buf[1920*1080];
unsigned int dst_buf[1920*1080];
//unsigned int tmp_buf[1920*1080 * 2];

void rga_test_0(void)
{
    struct rga_req req;
    rga_session session;
    unsigned int *src, *dst;
    uint32_t i, j;
    uint8_t *p;
    uint8_t t;
    uint32_t *dst0, *dst1, *dst2;

    struct fb_info *fb;

    session.pid	= current->pid;
	INIT_LIST_HEAD(&session.waiting);
	INIT_LIST_HEAD(&session.running);
	INIT_LIST_HEAD(&session.list_session);
	init_waitqueue_head(&session.wait);
	/* no need to protect */
	list_add_tail(&session.list_session, &rga_service.session);
	atomic_set(&session.task_running, 0);
    atomic_set(&session.num_done, 0);
	//file->private_data = (void *)session;

    fb = rk_get_fb(0);

    memset(&req, 0, sizeof(struct rga_req));
    src = src_buf;
    dst = dst_buf;

    memset(src_buf, 0x80, 1024*600*4);

    dmac_flush_range(&src_buf[0], &src_buf[1024*600]);
    outer_flush_range(virt_to_phys(&src_buf[0]),virt_to_phys(&src_buf[1024*600]));


    #if 0
    memset(src_buf, 0x80, 800*480*4);
    memset(dst_buf, 0xcc, 800*480*4);

    dmac_flush_range(&dst_buf[0], &dst_buf[800*480]);
    outer_flush_range(virt_to_phys(&dst_buf[0]),virt_to_phys(&dst_buf[800*480]));
    #endif

    dst0 = &dst_buf[0];
    //dst1 = &dst_buf[1280*800*4];
    //dst2 = &dst_buf[1280*800*4*2];

    i = j = 0;

    printk("\n********************************\n");
    printk("************ RGA_TEST ************\n");
    printk("********************************\n\n");

    req.src.act_w = 1024;
    req.src.act_h = 600;

    req.src.vir_w = 1024;
    req.src.vir_h = 600;
    req.src.yrgb_addr = (uint32_t)virt_to_phys(src);
    req.src.uv_addr = (uint32_t)(req.src.yrgb_addr + 1080*1920);
    req.src.v_addr = (uint32_t)virt_to_phys(src);
    req.src.format = RK_FORMAT_RGBA_8888;

    req.dst.act_w = 600;
    req.dst.act_h = 352;

    req.dst.vir_w = 1280;
    req.dst.vir_h = 800;
    req.dst.x_offset = 600;
    req.dst.y_offset = 0;

    dst = dst0;

    req.dst.yrgb_addr = ((uint32_t)virt_to_phys(dst));

    //req.dst.format = RK_FORMAT_RGB_565;

    req.clip.xmin = 0;
    req.clip.xmax = 1279;
    req.clip.ymin = 0;
    req.clip.ymax = 799;

    //req.render_mode = color_fill_mode;
    //req.fg_color = 0x80ffffff;

    req.rotate_mode = 1;
    //req.scale_mode = 2;

    //req.alpha_rop_flag = 0;
    //req.alpha_rop_mode = 0x19;
    //req.PD_mode = 3;

    req.sina = 65536;
    req.cosa = 0;

    //req.mmu_info.mmu_flag = 0x21;
    //req.mmu_info.mmu_en = 1;

    //printk("src = %.8x\n", req.src.yrgb_addr);
    //printk("src = %.8x\n", req.src.uv_addr);
    //printk("dst = %.8x\n", req.dst.yrgb_addr);


    rga_blit_sync(&session, &req);

    #if 1
    fb->var.bits_per_pixel = 32;

    fb->var.xres = 1280;
    fb->var.yres = 800;

    fb->var.red.length = 8;
    fb->var.red.offset = 0;
    fb->var.red.msb_right = 0;

    fb->var.green.length = 8;
    fb->var.green.offset = 8;
    fb->var.green.msb_right = 0;

    fb->var.blue.length = 8;

    fb->var.blue.offset = 16;
    fb->var.blue.msb_right = 0;

    fb->var.transp.length = 8;
    fb->var.transp.offset = 24;
    fb->var.transp.msb_right = 0;

    fb->var.nonstd &= (~0xff);
    fb->var.nonstd |= 1;

    fb->fix.smem_start = virt_to_phys(dst);

    rk_direct_fb_show(fb);
    #endif

}

#endif
fs_initcall(rga_init);
module_exit(rga_exit);

/* Module information */
MODULE_AUTHOR("zsq@rock-chips.com");
MODULE_DESCRIPTION("Driver for rga device");
MODULE_LICENSE("GPL");
