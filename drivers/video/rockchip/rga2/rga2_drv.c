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
#include <linux/scatterlist.h>


#if defined(CONFIG_ION_ROCKCHIP)
#include <linux/rockchip_ion.h>
#endif

#include "rga2.h"
#include "rga2_reg_info.h"
#include "rga2_mmu_info.h"
#include "RGA2_API.h"

#if defined(CONFIG_ROCKCHIP_IOMMU) & defined(CONFIG_ION_ROCKCHIP)
#define CONFIG_RGA_IOMMU
#endif



#define RGA2_TEST_FLUSH_TIME 0
#define RGA2_INFO_BUS_ERROR 1

#define RGA2_POWER_OFF_DELAY	4*HZ /* 4s */
#define RGA2_TIMEOUT_DELAY	2*HZ /* 2s */

#define RGA2_MAJOR		255

#define RGA2_RESET_TIMEOUT	1000

/* Driver information */
#define DRIVER_DESC		"RGA2 Device Driver"
#define DRIVER_NAME		"rga2"

#define RGA2_VERSION   "2.000"

ktime_t rga2_start;
ktime_t rga2_end;

int rga2_flag = 0;

extern long (*rga_ioctl_kernel_p)(struct rga_req *);

rga2_session rga2_session_global;

struct rga2_drvdata_t {
  	struct miscdevice miscdev;
  	struct device dev;
	void *rga_base;
	int irq;

	struct delayed_work power_off_work;
	void (*rga_irq_callback)(int rga_retval);   //callback function used by aync call
	struct wake_lock wake_lock;

	struct clk *aclk_rga2;
	struct clk *hclk_rga2;
	struct clk *pd_rga2;
    struct clk *rga2;

    #if defined(CONFIG_ION_ROCKCHIP)
    struct ion_client * ion_client;
    #endif
};

struct rga2_drvdata_t *rga2_drvdata;

struct rga2_service_info rga2_service;
struct rga2_mmu_buf_t rga2_mmu_buf;

#if defined(CONFIG_ION_ROCKCHIP)
extern struct ion_client *rockchip_ion_client_create(const char * name);
#endif

static int rga2_blit_async(rga2_session *session, struct rga2_req *req);
static void rga2_del_running_list(void);
static void rga2_del_running_list_timeout(void);
static void rga2_try_set_reg(void);


/* Logging */
#define RGA_DEBUG 0
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

#if RGA2_TEST_MSG
static void print_info(struct rga2_req *req)
{
    printk("render_mode=%d bitblt_mode=%d rotate_mode=%.8x\n",
            req->render_mode, req->bitblt_mode, req->rotate_mode);
    printk("src : y=%.8x uv=%.8x v=%.8x format=%d aw=%d ah=%d vw=%d vh=%d xoff=%d yoff=%d \n",
            req->src.yrgb_addr, req->src.uv_addr, req->src.v_addr, req->src.format,
            req->src.act_w, req->src.act_h, req->src.vir_w, req->src.vir_h,
            req->src.x_offset, req->src.y_offset);
    printk("dst : y=%.8x uv=%.8x v=%.8x format=%d aw=%d ah=%d vw=%d vh=%d xoff=%d yoff=%d \n",
            req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr, req->dst.format,
            req->dst.act_w, req->dst.act_h, req->dst.vir_w, req->dst.vir_h,
            req->dst.x_offset, req->dst.y_offset);
    printk("mmu : src=%.2x src1=%.2x dst=%.2x els=%.2x\n",
            req->mmu_info.src0_mmu_flag, req->mmu_info.src1_mmu_flag,
            req->mmu_info.dst_mmu_flag,  req->mmu_info.els_mmu_flag);
    printk("alpha : flag %.8x mode0=%.8x mode1=%.8x\n",
            req->alpha_rop_flag, req->alpha_mode_0, req->alpha_mode_1);
}
#endif


static inline void rga2_write(u32 b, u32 r)
{
	__raw_writel(b, rga2_drvdata->rga_base + r);
}

static inline u32 rga2_read(u32 r)
{
	return __raw_readl(rga2_drvdata->rga_base + r);
}

static void rga2_soft_reset(void)
{
	u32 i;
	u32 reg;

	rga2_write(1, RGA2_SYS_CTRL); //RGA_SYS_CTRL

	for(i = 0; i < RGA2_RESET_TIMEOUT; i++)
	{
		reg = rga2_read(RGA2_SYS_CTRL) & 1; //RGA_SYS_CTRL

		if(reg == 0)
			break;

		udelay(1);
	}

	if(i == RGA2_RESET_TIMEOUT)
		ERR("soft reset timeout.\n");
}

static void rga2_dump(void)
{
	int running;
    struct rga2_reg *reg, *reg_tmp;
    rga2_session *session, *session_tmp;

	running = atomic_read(&rga2_service.total_running);
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

	list_for_each_entry_safe(session, session_tmp, &rga2_service.session, list_session)
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

static inline void rga2_queue_power_off_work(void)
{
	queue_delayed_work(system_nrt_wq, &rga2_drvdata->power_off_work, RGA2_POWER_OFF_DELAY);
}

/* Caller must hold rga_service.lock */
static void rga2_power_on(void)
{
	static ktime_t last;
	ktime_t now = ktime_get();

	if (ktime_to_ns(ktime_sub(now, last)) > NSEC_PER_SEC) {
		cancel_delayed_work_sync(&rga2_drvdata->power_off_work);
		rga2_queue_power_off_work();
		last = now;
	}
	if (rga2_service.enable)
		return;

    clk_prepare_enable(rga2_drvdata->rga2);
    clk_prepare_enable(rga2_drvdata->pd_rga2);
	clk_prepare_enable(rga2_drvdata->aclk_rga2);
	clk_prepare_enable(rga2_drvdata->hclk_rga2);
	//clk_enable(rga2_drvdata->pd_rga2);
	wake_lock(&rga2_drvdata->wake_lock);
	rga2_service.enable = true;
}

/* Caller must hold rga_service.lock */
static void rga2_power_off(void)
{
	int total_running;

	if (!rga2_service.enable) {
		return;
	}

	total_running = atomic_read(&rga2_service.total_running);
	if (total_running) {
		pr_err("power off when %d task running!!\n", total_running);
		mdelay(50);
		pr_err("delay 50 ms for running task\n");
		rga2_dump();
	}

	//clk_disable(rga2_drvdata->pd_rga2);
    clk_disable_unprepare(rga2_drvdata->rga2);
    clk_disable_unprepare(rga2_drvdata->pd_rga2);
	clk_disable_unprepare(rga2_drvdata->aclk_rga2);
	clk_disable_unprepare(rga2_drvdata->hclk_rga2);
	wake_unlock(&rga2_drvdata->wake_lock);
	rga2_service.enable = false;
}

static void rga2_power_off_work(struct work_struct *work)
{
	if (mutex_trylock(&rga2_service.lock)) {
		rga2_power_off();
		mutex_unlock(&rga2_service.lock);
	} else {
		/* Come back later if the device is busy... */
		rga2_queue_power_off_work();
	}
}

static int rga2_flush(rga2_session *session, unsigned long arg)
{
    int ret = 0;
    int ret_timeout;

    #if RGA2_TEST_FLUSH_TIME
    ktime_t start;
    ktime_t end;
    start = ktime_get();
    #endif

    ret_timeout = wait_event_timeout(session->wait, atomic_read(&session->done), RGA2_TIMEOUT_DELAY);

	if (unlikely(ret_timeout < 0)) {
		//pr_err("flush pid %d wait task ret %d\n", session->pid, ret);
        mutex_lock(&rga2_service.lock);
        rga2_del_running_list();
        mutex_unlock(&rga2_service.lock);
        ret = ret_timeout;
	} else if (0 == ret_timeout) {
		//pr_err("flush pid %d wait %d task done timeout\n", session->pid, atomic_read(&session->task_running));
        //printk("bus  = %.8x\n", rga_read(RGA_INT));
        mutex_lock(&rga2_service.lock);
        rga2_del_running_list_timeout();
        rga2_try_set_reg();
        mutex_unlock(&rga2_service.lock);
		ret = -ETIMEDOUT;
	}

    #if RGA2_TEST_FLUSH_TIME
    end = ktime_get();
    end = ktime_sub(end, start);
    printk("one flush wait time %d\n", (int)ktime_to_us(end));
    #endif

	return ret;
}


static int rga2_get_result(rga2_session *session, unsigned long arg)
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


static int rga2_check_param(const struct rga2_req *req)
{
    /*RGA2 can support up to 8192*8192 resolution in RGB format,but we limit the image size to 8191*8191 here*/
	//check src width and height

    if(!((req->render_mode == color_fill_mode)))
    {
    	if (unlikely((req->src.act_w <= 0) || (req->src.act_w > 8191) || (req->src.act_h <= 0) || (req->src.act_h > 8191)))
        {
    		printk("invalid source resolution act_w = %d, act_h = %d\n", req->src.act_w, req->src.act_h);
    		return  -EINVAL;
    	}
    }

    if(!((req->render_mode == color_fill_mode)))
    {
    	if (unlikely((req->src.vir_w <= 0) || (req->src.vir_w > 8191) || (req->src.vir_h <= 0) || (req->src.vir_h > 8191)))
        {
    		printk("invalid source resolution vir_w = %d, vir_h = %d\n", req->src.vir_w, req->src.vir_h);
    		return  -EINVAL;
    	}
    }

	//check dst width and height
	if (unlikely((req->dst.act_w <= 0) || (req->dst.act_w > 4096) || (req->dst.act_h <= 0) || (req->dst.act_h > 4096)))
    {
		printk("invalid destination resolution act_w = %d, act_h = %d\n", req->dst.act_w, req->dst.act_h);
		return	-EINVAL;
	}

    if (unlikely((req->dst.vir_w <= 0) || (req->dst.vir_w > 4096) || (req->dst.vir_h <= 0) || (req->dst.vir_h > 4096)))
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

static void rga2_copy_reg(struct rga2_reg *reg, uint32_t offset)
{
    uint32_t i;
    uint32_t *cmd_buf;
    uint32_t *reg_p;

    if(atomic_read(&reg->session->task_running) != 0)
    {
        printk(KERN_ERR "task_running is no zero\n");
    }

    atomic_add(1, &rga2_service.cmd_num);
	atomic_add(1, &reg->session->task_running);

    cmd_buf = (uint32_t *)rga2_service.cmd_buff + offset*32;
    reg_p = (uint32_t *)reg->cmd_reg;

    for(i=0; i<32; i++)
    {
        cmd_buf[i] = reg_p[i];
    }

    dsb();
}


static struct rga2_reg * rga2_reg_init(rga2_session *session, struct rga2_req *req)
{
    int32_t ret;
	struct rga2_reg *reg = kzalloc(sizeof(struct rga2_reg), GFP_KERNEL);
	if (NULL == reg) {
		pr_err("kmalloc fail in rga_reg_init\n");
		return NULL;
	}

    reg->session = session;
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

    reg->MMU_base = NULL;

    if ((req->mmu_info.src0_mmu_flag & 1) || (req->mmu_info.src1_mmu_flag & 1)
        || (req->mmu_info.dst_mmu_flag & 1) || (req->mmu_info.els_mmu_flag & 1))
    {
        ret = rga2_set_mmu_info(reg, req);
        if(ret < 0) {
            printk("%s, [%d] set mmu info error \n", __FUNCTION__, __LINE__);
            if(reg != NULL)
                kfree(reg);

            return NULL;
        }
    }

    if(RGA2_gen_reg_info((uint8_t *)reg->cmd_reg, req) == -1) {
        printk("gen reg info error\n");
        if(reg != NULL)
            kfree(reg);

        return NULL;
    }

    mutex_lock(&rga2_service.lock);
	list_add_tail(&reg->status_link, &rga2_service.waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	mutex_unlock(&rga2_service.lock);

    return reg;
}


/* Caller must hold rga_service.lock */
static void rga2_reg_deinit(struct rga2_reg *reg)
{
	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	kfree(reg);
}

/* Caller must hold rga_service.lock */
static void rga2_reg_from_wait_to_run(struct rga2_reg *reg)
{
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &rga2_service.running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
}

/* Caller must hold rga_service.lock */
static void rga2_service_session_clear(rga2_session *session)
{
	struct rga2_reg *reg, *n;

    list_for_each_entry_safe(reg, n, &session->waiting, session_link)
    {
		rga2_reg_deinit(reg);
	}

    list_for_each_entry_safe(reg, n, &session->running, session_link)
    {
		rga2_reg_deinit(reg);
	}
}

/* Caller must hold rga_service.lock */
static void rga2_try_set_reg(void)
{
    struct rga2_reg *reg ;

    if (list_empty(&rga2_service.running))
    {
        if (!list_empty(&rga2_service.waiting))
        {
            /* RGA is idle */
            reg = list_entry(rga2_service.waiting.next, struct rga2_reg, status_link);

            rga2_power_on();
            udelay(1);

            rga2_copy_reg(reg, 0);
            rga2_reg_from_wait_to_run(reg);

            dmac_flush_range(&rga2_service.cmd_buff[0], &rga2_service.cmd_buff[32]);
            outer_flush_range(virt_to_phys(&rga2_service.cmd_buff[0]),virt_to_phys(&rga2_service.cmd_buff[32]));

            #if defined(CONFIG_ARCH_RK30)
            rga2_soft_reset();
            #endif

            rga2_write(0x0, RGA2_SYS_CTRL);
            //rga2_write(0, RGA_MMU_CTRL);

            /* CMD buff */
            rga2_write(virt_to_phys(rga2_service.cmd_buff), RGA2_CMD_BASE);

#if RGA2_TEST
            if(rga2_flag)
            {
                //printk(KERN_DEBUG "cmd_addr = %.8x\n", rga_read(RGA_CMD_ADDR));
                uint32_t i, *p;
                p = rga2_service.cmd_buff;
                printk("CMD_REG\n");
                for (i=0; i<8; i++)
                    printk("%.8x %.8x %.8x %.8x\n", p[0 + i*4], p[1+i*4], p[2 + i*4], p[3 + i*4]);
            }
#endif

            /* master mode */
            rga2_write((0x1<<1)|(0x1<<2)|(0x1<<5)|(0x1<<6), RGA2_SYS_CTRL);

            /* All CMD finish int */
            rga2_write(rga2_read(RGA2_INT)|(0x1<<10)|(0x1<<8), RGA2_INT);

            #if RGA2_TEST_TIME
            rga_start = ktime_get();
            #endif

            /* Start proc */
            atomic_set(&reg->session->done, 0);
            rga2_write(0x1, RGA2_CMD_CTRL);
#if RGA2_TEST
            if(rga2_flag)
            {
                uint32_t i;
                printk("CMD_READ_BACK_REG\n");
                for (i=0; i<8; i++)
                    printk("%.8x %.8x %.8x %.8x\n", rga2_read(0x100 + i*16 + 0),
                            rga2_read(0x100 + i*16 + 4), rga2_read(0x100 + i*16 + 8), rga2_read(0x100 + i*16 + 12));
            }
#endif
        }
    }
}




/* Caller must hold rga_service.lock */
static void rga2_del_running_list(void)
{
    struct rga2_reg *reg;

    while(!list_empty(&rga2_service.running))
    {
        reg = list_entry(rga2_service.running.next, struct rga2_reg, status_link);

        if(reg->MMU_len != 0)
        {
            if (rga2_mmu_buf.back + reg->MMU_len > 2*rga2_mmu_buf.size)
                rga2_mmu_buf.back = reg->MMU_len + rga2_mmu_buf.size;
            else
                rga2_mmu_buf.back += reg->MMU_len;
        }
        atomic_sub(1, &reg->session->task_running);
        atomic_sub(1, &rga2_service.total_running);

        if(list_empty(&reg->session->waiting))
        {
            atomic_set(&reg->session->done, 1);
            wake_up(&reg->session->wait);
        }

        rga2_reg_deinit(reg);
    }
}

/* Caller must hold rga_service.lock */
static void rga2_del_running_list_timeout(void)
{
    struct rga2_reg *reg;

    while(!list_empty(&rga2_service.running))
    {
        reg = list_entry(rga2_service.running.next, struct rga2_reg, status_link);

        if(reg->MMU_base != NULL)
        {
            kfree(reg->MMU_base);
        }

        atomic_sub(1, &reg->session->task_running);
        atomic_sub(1, &rga2_service.total_running);

        //printk("RGA soft reset for timeout process\n");
        rga2_soft_reset();


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

        rga2_reg_deinit(reg);
    }
}


static int rga2_convert_dma_buf(struct rga2_req *req)
{
	struct ion_handle *hdl;
	ion_phys_addr_t phy_addr;
	size_t len;
    int ret;

    req->sg_src0 = NULL;
    req->sg_src1 = NULL;
    req->sg_dst  = NULL;
    req->sg_els  = NULL;

    if(req->src.yrgb_addr) {
        hdl = ion_import_dma_buf(rga2_drvdata->ion_client, req->src.yrgb_addr);
        if (IS_ERR(hdl)) {
            ret = PTR_ERR(hdl);
            printk("RGA2 ERROR ion buf handle\n");
            return ret;
        }
        if (req->mmu_info.src0_mmu_flag) {
            req->sg_src0 = ion_sg_table(rga2_drvdata->ion_client, hdl);
            req->src.yrgb_addr = req->src.uv_addr;
            req->src.uv_addr = req->src.yrgb_addr + (req->src.vir_w * req->src.vir_h);
            req->src.v_addr = req->src.uv_addr + (req->src.vir_w * req->src.vir_h)/4;
        }
        else {
            ion_phys(rga2_drvdata->ion_client, hdl, &phy_addr, &len);
            req->src.yrgb_addr = phy_addr;
            req->src.uv_addr = req->src.yrgb_addr + (req->src.vir_w * req->src.vir_h);
            req->src.v_addr = req->src.uv_addr + (req->src.vir_w * req->src.vir_h)/4;
        }
        ion_free(rga2_drvdata->ion_client, hdl);
    }
    else {
        req->src.yrgb_addr = req->src.uv_addr;
        req->src.uv_addr = req->src.yrgb_addr + (req->src.vir_w * req->src.vir_h);
        req->src.v_addr = req->src.uv_addr + (req->src.vir_w * req->src.vir_h)/4;
    }

    if(req->dst.yrgb_addr) {
        hdl = ion_import_dma_buf(rga2_drvdata->ion_client, req->dst.yrgb_addr);
        if (IS_ERR(hdl)) {
            ret = PTR_ERR(hdl);
            printk("RGA2 ERROR ion buf handle\n");
            return ret;
        }
        if (req->mmu_info.dst_mmu_flag) {
            req->sg_dst = ion_sg_table(rga2_drvdata->ion_client, hdl);
            req->dst.yrgb_addr = req->dst.uv_addr;
            req->dst.uv_addr = req->dst.yrgb_addr + (req->dst.vir_w * req->dst.vir_h);
            req->dst.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h)/4;
        }
        else {
            ion_phys(rga2_drvdata->ion_client, hdl, &phy_addr, &len);
            req->dst.yrgb_addr = phy_addr;
            req->dst.uv_addr = req->dst.yrgb_addr + (req->dst.vir_w * req->dst.vir_h);
            req->dst.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h)/4;
        }
        ion_free(rga2_drvdata->ion_client, hdl);
    }
    else {
        req->dst.yrgb_addr = req->dst.uv_addr;
        req->dst.uv_addr = req->dst.yrgb_addr + (req->dst.vir_w * req->dst.vir_h);
        req->dst.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h)/4;
    }

    if(req->src1.yrgb_addr) {
        hdl = ion_import_dma_buf(rga2_drvdata->ion_client, req->src1.yrgb_addr);
        if (IS_ERR(hdl)) {
            ret = PTR_ERR(hdl);
            printk("RGA2 ERROR ion buf handle\n");
            return ret;
        }
        if (req->mmu_info.dst_mmu_flag) {
            req->sg_src1 = ion_sg_table(rga2_drvdata->ion_client, hdl);
            req->src1.yrgb_addr = 0;
            req->src1.uv_addr = req->dst.yrgb_addr + (req->dst.vir_w * req->dst.vir_h);
            req->src1.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h)/4;
        }
        else {
            ion_phys(rga2_drvdata->ion_client, hdl, &phy_addr, &len);
            req->src1.yrgb_addr = phy_addr;
            req->src1.uv_addr = req->dst.yrgb_addr + (req->dst.vir_w * req->dst.vir_h);
            req->src1.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h)/4;
        }
        ion_free(rga2_drvdata->ion_client, hdl);
    }
    else {
        req->src1.yrgb_addr = req->dst.uv_addr;
        req->src1.uv_addr = req->dst.yrgb_addr + (req->dst.vir_w * req->dst.vir_h);
        req->src1.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h)/4;
    }

    return 0;
}


static int rga2_blit(rga2_session *session, struct rga2_req *req)
{
    int ret = -1;
    int num = 0;
    struct rga2_reg *reg;

    if(rga2_convert_dma_buf(req)) {
        printk("RGA2 : DMA buf copy error\n");
        return -EFAULT;
    }

    do {
        /* check value if legal */
        ret = rga2_check_param(req);
    	if(ret == -EINVAL) {
            printk("req argument is inval\n");
            break;
    	}

        reg = rga2_reg_init(session, req);
        if(reg == NULL) {
            break;
        }
        num = 1;

        mutex_lock(&rga2_service.lock);
        atomic_add(num, &rga2_service.total_running);
        rga2_try_set_reg();
        mutex_unlock(&rga2_service.lock);

        return 0;
    }
    while(0);

    return -EFAULT;
}

static int rga2_blit_async(rga2_session *session, struct rga2_req *req)
{
	int ret = -1;

    #if RGA2_TEST_MSG
    if (1) {//req->src.format >= 0x10) {
        print_info(req);
        rga2_flag = 1;
        printk("*** rga_blit_async proc ***\n");
    }
    else
        rga2_flag = 0;
    #endif
    atomic_set(&session->done, 0);
    ret = rga2_blit(session, req);

    return ret;
}

static int rga2_blit_sync(rga2_session *session, struct rga2_req *req)
{
    int ret = -1;
    int ret_timeout = 0;

    #if RGA2_TEST_MSG
    if (1) {//req->bitblt_mode == 0x2) {
        print_info(req);
        rga2_flag = 1;
        printk("*** rga2_blit_sync proc ***\n");
    }
    else
        rga2_flag = 0;
    #endif

    atomic_set(&session->done, 0);

    ret = rga2_blit(session, req);
    if(ret < 0)
    {
        return ret;
    }

    ret_timeout = wait_event_timeout(session->wait, atomic_read(&session->done), RGA2_TIMEOUT_DELAY);

    if (unlikely(ret_timeout< 0))
    {
		//pr_err("sync pid %d wait task ret %d\n", session->pid, ret_timeout);
        mutex_lock(&rga2_service.lock);
        rga2_del_running_list();
        mutex_unlock(&rga2_service.lock);
        ret = ret_timeout;
	}
    else if (0 == ret_timeout)
    {
		//pr_err("sync pid %d wait %d task done timeout\n", session->pid, atomic_read(&session->task_running));
        mutex_lock(&rga2_service.lock);
        rga2_del_running_list_timeout();
        rga2_try_set_reg();
        mutex_unlock(&rga2_service.lock);
		ret = -ETIMEDOUT;
	}

    #if RGA2_TEST_TIME
    rga2_end = ktime_get();
    rga2_end = ktime_sub(rga2_end, rga2_start);
    printk("sync one cmd end time %d\n", (int)ktime_to_us(rga2_end));
    #endif

    return ret;
}


static long rga_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
    struct rga2_req req;
    struct rga_req req_rga;
	int ret = 0;
    rga2_session *session;

    memset(&req, 0x0, sizeof(req));

    mutex_lock(&rga2_service.mutex);

    session = (rga2_session *)file->private_data;

	if (NULL == session)
    {
        printk("%s [%d] rga thread session is null\n",__FUNCTION__,__LINE__);
        mutex_unlock(&rga2_service.mutex);
		return -EINVAL;
	}

    memset(&req, 0x0, sizeof(req));

	switch (cmd)
	{
        case RGA_BLIT_SYNC:
    		if (unlikely(copy_from_user(&req_rga, (struct rga_req*)arg, sizeof(struct rga_req))))
            {
        		ERR("copy_from_user failed\n");
        		ret = -EFAULT;
                break;
        	}
            RGA_MSG_2_RGA2_MSG(&req_rga, &req);

            ret = rga2_blit_sync(session, &req);
            break;
		case RGA_BLIT_ASYNC:
    		if (unlikely(copy_from_user(&req_rga, (struct rga_req*)arg, sizeof(struct rga_req))))
            {
        		ERR("copy_from_user failed\n");
        		ret = -EFAULT;
                break;
        	}

            RGA_MSG_2_RGA2_MSG(&req_rga, &req);

            if((atomic_read(&rga2_service.total_running) > 8))
            {
			    ret = rga2_blit_sync(session, &req);
            }
            else
            {
                ret = rga2_blit_async(session, &req);
            }
			break;
		case RGA2_BLIT_SYNC:
    		if (unlikely(copy_from_user(&req, (struct rga2_req*)arg, sizeof(struct rga2_req))))
            {
        		ERR("copy_from_user failed\n");
        		ret = -EFAULT;
                break;
        	}
            ret = rga2_blit_sync(session, &req);
            break;
		case RGA2_BLIT_ASYNC:
    		if (unlikely(copy_from_user(&req, (struct rga2_req*)arg, sizeof(struct rga2_req))))
            {
        		ERR("copy_from_user failed\n");
        		ret = -EFAULT;
                break;
        	}

            if((atomic_read(&rga2_service.total_running) > 16))
            {
			    ret = rga2_blit_sync(session, &req);
            }
            else
            {
                ret = rga2_blit_async(session, &req);
            }
			break;
        case RGA_FLUSH:
		case RGA2_FLUSH:
			ret = rga2_flush(session, arg);
			break;
        case RGA_GET_RESULT:
        case RGA2_GET_RESULT:
            ret = rga2_get_result(session, arg);
            break;
        case RGA_GET_VERSION:
        case RGA2_GET_VERSION:
            ret = copy_to_user((void *)arg, RGA2_VERSION, sizeof(RGA2_VERSION));
            //ret = 0;
            break;
		default:
			ERR("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}

	mutex_unlock(&rga2_service.mutex);

	return ret;
}


long rga2_ioctl_kernel(struct rga_req *req_rga)
{
	int ret = 0;
    rga2_session *session;
    struct rga2_req req;

    memset(&req, 0x0, sizeof(req));

    mutex_lock(&rga2_service.mutex);

    session = &rga2_session_global;

	if (NULL == session)
    {
        printk("%s [%d] rga thread session is null\n",__FUNCTION__,__LINE__);
        mutex_unlock(&rga2_service.mutex);
		return -EINVAL;
	}

    RGA_MSG_2_RGA2_MSG(req_rga, &req);
    ret = rga2_blit_sync(session, &req);

	mutex_unlock(&rga2_service.mutex);

	return ret;
}


static int rga2_open(struct inode *inode, struct file *file)
{
    rga2_session *session = kzalloc(sizeof(rga2_session), GFP_KERNEL);
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
	mutex_lock(&rga2_service.lock);
	list_add_tail(&session->list_session, &rga2_service.session);
	mutex_unlock(&rga2_service.lock);
	atomic_set(&session->task_running, 0);
    atomic_set(&session->num_done, 0);

	file->private_data = (void *)session;

    //DBG("*** rga dev opened by pid %d *** \n", session->pid);
	return nonseekable_open(inode, file);

}

static int rga2_release(struct inode *inode, struct file *file)
{
    int task_running;
	rga2_session *session = (rga2_session *)file->private_data;
	if (NULL == session)
		return -EINVAL;
    //printk(KERN_DEBUG  "-");
	task_running = atomic_read(&session->task_running);

    if (task_running)
    {
		pr_err("rga2_service session %d still has %d task running when closing\n", session->pid, task_running);
		msleep(100);
        /*Í¬²½*/
	}

	wake_up(&session->wait);
	mutex_lock(&rga2_service.lock);
	list_del(&session->list_session);
	rga2_service_session_clear(session);
	kfree(session);
	mutex_unlock(&rga2_service.lock);

    //DBG("*** rga dev close ***\n");
	return 0;
}

static irqreturn_t rga2_irq_thread(int irq, void *dev_id)
{
	mutex_lock(&rga2_service.lock);
	if (rga2_service.enable) {
		rga2_del_running_list();
		rga2_try_set_reg();
	}
	mutex_unlock(&rga2_service.lock);

	return IRQ_HANDLED;
}

static irqreturn_t rga2_irq(int irq,  void *dev_id)
{
	/*clear INT */
	rga2_write(rga2_read(RGA2_INT) | (0x1<<4) | (0x1<<5) | (0x1<<6) | (0x1<<7), RGA2_INT);

	return IRQ_WAKE_THREAD;
}

struct file_operations rga2_fops = {
	.owner		= THIS_MODULE,
	.open		= rga2_open,
	.release	= rga2_release,
	.unlocked_ioctl		= rga_ioctl,
};

static struct miscdevice rga2_dev ={
    .minor = RGA2_MAJOR,
    .name  = "rga",
    .fops  = &rga2_fops,
};

static const struct of_device_id rockchip_rga_dt_ids[] = {
	{ .compatible = "rockchip,rk3288-rga2", },
	{},
};

static int rga2_drv_probe(struct platform_device *pdev)
{
	struct rga2_drvdata_t *data;
    struct resource *res;
	int ret = 0;

	mutex_init(&rga2_service.lock);
	mutex_init(&rga2_service.mutex);
	atomic_set(&rga2_service.total_running, 0);
	atomic_set(&rga2_service.src_format_swt, 0);
	rga2_service.last_prc_src_format = 1; /* default is yuv first*/
	rga2_service.enable = false;

    rga_ioctl_kernel_p = rga2_ioctl_kernel;

	data = devm_kzalloc(&pdev->dev, sizeof(struct rga2_drvdata_t), GFP_KERNEL);
	if(NULL == data)
	{
		ERR("failed to allocate driver data.\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&data->power_off_work, rga2_power_off_work);
	wake_lock_init(&data->wake_lock, WAKE_LOCK_SUSPEND, "rga");

	//data->pd_rga2 = clk_get(NULL, "pd_rga");
    data->rga2 = devm_clk_get(&pdev->dev, "clk_rga");
    data->pd_rga2 = devm_clk_get(&pdev->dev, "pd_rga");
	data->aclk_rga2 = devm_clk_get(&pdev->dev, "aclk_rga");
    data->hclk_rga2 = devm_clk_get(&pdev->dev, "hclk_rga");

	/* map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->rga_base = devm_ioremap_resource(&pdev->dev, res);
	if (!data->rga_base) {
		ERR("rga ioremap failed\n");
		ret = -ENOENT;
		goto err_ioremap;
	}

	/* get the IRQ */
	data->irq = platform_get_irq(pdev, 0);
	if (data->irq <= 0) {
		ERR("failed to get rga irq resource (%d).\n", data->irq);
		ret = data->irq;
		goto err_irq;
	}

	/* request the IRQ */
	ret = devm_request_threaded_irq(&pdev->dev, data->irq, rga2_irq, rga2_irq_thread, 0, "rga", pdev);
	if (ret)
	{
		ERR("rga request_irq failed (%d).\n", ret);
		goto err_irq;
	}

	platform_set_drvdata(pdev, data);
	rga2_drvdata = data;

    #if defined(CONFIG_ION_ROCKCHIP)
	data->ion_client = rockchip_ion_client_create("rga");
	if (IS_ERR(data->ion_client)) {
		dev_err(&pdev->dev, "failed to create ion client for rga");
		return PTR_ERR(data->ion_client);
	} else {
		dev_info(&pdev->dev, "rga ion client create success!\n");
	}
    #endif

	ret = misc_register(&rga2_dev);
	if(ret)
	{
		ERR("cannot register miscdev (%d)\n", ret);
		goto err_misc_register;
	}

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

static int rga2_drv_remove(struct platform_device *pdev)
{
	struct rga2_drvdata_t *data = platform_get_drvdata(pdev);
	DBG("%s [%d]\n",__FUNCTION__,__LINE__);

	wake_lock_destroy(&data->wake_lock);
	misc_deregister(&(data->miscdev));
	free_irq(data->irq, &data->miscdev);
	iounmap((void __iomem *)(data->rga_base));

	//clk_put(data->pd_rga2);
    devm_clk_put(&pdev->dev, data->rga2);
	devm_clk_put(&pdev->dev, data->aclk_rga2);
	devm_clk_put(&pdev->dev, data->hclk_rga2);

	kfree(data);
	return 0;
}

static struct platform_driver rga2_driver = {
	.probe		= rga2_drv_probe,
	.remove		= rga2_drv_remove,
	.driver		= {
		.owner  = THIS_MODULE,
		.name	= "rga2",
		.of_match_table = of_match_ptr(rockchip_rga_dt_ids),
	},
};


void rga2_test_0(void);

static int __init rga2_init(void)
{
	int ret;
    uint32_t *buf_p;

    /* malloc pre scale mid buf mmu table */
    buf_p = kmalloc(1024*256, GFP_KERNEL);
    rga2_mmu_buf.buf_virtual = buf_p;
    rga2_mmu_buf.buf = (uint32_t *)virt_to_phys((void *)((uint32_t)buf_p));
    rga2_mmu_buf.front = 0;
    rga2_mmu_buf.back = 64*1024;
    rga2_mmu_buf.size = 64*1024;

	if ((ret = platform_driver_register(&rga2_driver)) != 0)
	{
        printk(KERN_ERR "Platform device register failed (%d).\n", ret);
			return ret;
	}

    {
        rga2_session_global.pid = 0x0000ffff;
        INIT_LIST_HEAD(&rga2_session_global.waiting);
        INIT_LIST_HEAD(&rga2_session_global.running);
        INIT_LIST_HEAD(&rga2_session_global.list_session);

        INIT_LIST_HEAD(&rga2_service.waiting);
	    INIT_LIST_HEAD(&rga2_service.running);
	    INIT_LIST_HEAD(&rga2_service.done);
        INIT_LIST_HEAD(&rga2_service.session);
        init_waitqueue_head(&rga2_session_global.wait);
        //mutex_lock(&rga_service.lock);
        list_add_tail(&rga2_session_global.list_session, &rga2_service.session);
        //mutex_unlock(&rga_service.lock);
        atomic_set(&rga2_session_global.task_running, 0);
        atomic_set(&rga2_session_global.num_done, 0);
    }

    #if RGA2_TEST_CASE
    rga2_test_0();
    #endif

	INFO("Module initialized.\n");

	return 0;
}

static void __exit rga2_exit(void)
{
    rga2_power_off();

    if (rga2_mmu_buf.buf_virtual)
        kfree(rga2_mmu_buf.buf_virtual);

	platform_driver_unregister(&rga2_driver);
}


#if RGA2_TEST_CASE

extern struct fb_info * rk_get_fb(int fb_id);
EXPORT_SYMBOL(rk_get_fb);

extern void rk_direct_fb_show(struct fb_info * fbi);
EXPORT_SYMBOL(rk_direct_fb_show);

//unsigned int src_buf[4096*2304*3/2];
//unsigned int dst_buf[3840*2304*3/2];
//unsigned int tmp_buf[1920*1080 * 2];

void rga2_test_0(void)
{
    struct rga2_req req;
    rga2_session session;
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
	list_add_tail(&session.list_session, &rga2_service.session);
	atomic_set(&session.task_running, 0);
    atomic_set(&session.num_done, 0);
	//file->private_data = (void *)session;

    //fb = rk_get_fb(0);

    memset(&req, 0, sizeof(struct rga2_req));
    src = kmalloc(4096*2304*3/2, GFP_KERNEL);
    dst = kmalloc(3840*2160*3/2, GFP_KERNEL);

    //memset(src, 0x80, 4096*2304*4);

    //dmac_flush_range(&src, &src[800*480*4]);
    //outer_flush_range(virt_to_phys(&src),virt_to_phys(&src[800*480*4]));


    #if 0
    memset(src_buf, 0x80, 800*480*4);
    memset(dst_buf, 0xcc, 800*480*4);

    dmac_flush_range(&dst_buf[0], &dst_buf[800*480]);
    outer_flush_range(virt_to_phys(&dst_buf[0]),virt_to_phys(&dst_buf[800*480]));
    #endif

    dst0 = &dst;

    i = j = 0;

    printk("\n********************************\n");
    printk("************ RGA2_TEST ************\n");
    printk("********************************\n\n");

    req.pat.act_w = 16;
    req.pat.act_h = 16;
    req.pat.vir_w = 16;
    req.pat.vir_h = 16;
    req.pat.yrgb_addr = virt_to_phys(src);
    req.render_mode = update_palette_table_mode;
    rga2_blit_sync(&session, &req);

    req.src.act_w  = 4096;
    req.src.act_h = 2304;

    req.src.vir_w  = 4096;
    req.src.vir_h = 2304;
    req.src.yrgb_addr = (uint32_t)0;//virt_to_phys(src);
    req.src.uv_addr = (uint32_t)virt_to_phys(src);
    req.src.v_addr = 0;
    req.src.format = RGA2_FORMAT_YCbCr_420_SP;

    req.dst.act_w  = 3840;
    req.dst.act_h = 2160;
    req.dst.x_offset = 0;
    req.dst.y_offset = 0;

    req.dst.vir_w = 3840;
    req.dst.vir_h = 2160;

    req.dst.yrgb_addr = 0;//((uint32_t)virt_to_phys(dst));
    req.dst.uv_addr = ((uint32_t)virt_to_phys(dst));
    req.dst.format = RGA2_FORMAT_YCbCr_420_SP;

    //dst = dst0;

    //req.render_mode = color_fill_mode;
    //req.fg_color = 0x80ffffff;

    req.rotate_mode = 0;
    req.scale_bicu_mode = 2;

    //req.alpha_rop_flag = 0;
    //req.alpha_rop_mode = 0x19;
    //req.PD_mode = 3;

    //req.mmu_info.mmu_flag = 0x21;
    //req.mmu_info.mmu_en = 1;

    //printk("src = %.8x\n", req.src.yrgb_addr);
    //printk("src = %.8x\n", req.src.uv_addr);
    //printk("dst = %.8x\n", req.dst.yrgb_addr);

    rga2_blit_sync(&session, &req);

    #if 0
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

    if(src)
        kfree(src);
    if(dst)
        kfree(dst);
}

#endif
module_init(rga2_init);
module_exit(rga2_exit);

/* Module information */
MODULE_AUTHOR("zsq@rock-chips.com");
MODULE_DESCRIPTION("Driver for rga device");
MODULE_LICENSE("GPL");
