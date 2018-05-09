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
#include <linux/rockchip_ion.h>
#include <linux/version.h>
#include <linux/pm_runtime.h>
#include <linux/dma-buf.h>

#include "rga2.h"
#include "rga2_reg_info.h"
#include "rga2_mmu_info.h"
#include "RGA2_API.h"
#include "rga2_rop.h"

#if defined(CONFIG_RK_IOMMU) && defined(CONFIG_ION_ROCKCHIP)
#define CONFIG_RGA_IOMMU
#endif

#define RGA2_TEST_FLUSH_TIME 0
#define RGA2_INFO_BUS_ERROR 1
#define RGA2_POWER_OFF_DELAY	4*HZ /* 4s */
#define RGA2_TIMEOUT_DELAY	(HZ / 10) /* 100ms */
#define RGA2_MAJOR		255
#define RGA2_RESET_TIMEOUT	1000

/* Driver information */
#define DRIVER_DESC		"RGA2 Device Driver"
#define DRIVER_NAME		"rga2"
#define RGA2_VERSION   "2.000"

ktime_t rga2_start;
ktime_t rga2_end;
int rga2_flag;
int first_RGA2_proc;

rga2_session rga2_session_global;
long (*rga2_ioctl_kernel_p)(struct rga_req *);

struct rga2_drvdata_t {
	struct miscdevice miscdev;
	struct device *dev;
	void *rga_base;
	int irq;

	struct delayed_work power_off_work;
	struct wake_lock wake_lock;
	void (*rga_irq_callback)(int rga_retval);

	struct clk *aclk_rga2;
	struct clk *hclk_rga2;
	struct clk *rga2;

	struct ion_client * ion_client;
	char version[16];
};

struct rga2_drvdata_t *rga2_drvdata;
struct rga2_service_info rga2_service;
struct rga2_mmu_buf_t rga2_mmu_buf;

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
	printk("src : y=%.lx uv=%.lx v=%.lx format=%d aw=%d ah=%d vw=%d vh=%d xoff=%d yoff=%d \n",
	        req->src.yrgb_addr, req->src.uv_addr, req->src.v_addr, req->src.format,
	        req->src.act_w, req->src.act_h, req->src.vir_w, req->src.vir_h,
	        req->src.x_offset, req->src.y_offset);
	printk("dst : y=%lx uv=%lx v=%lx format=%d aw=%d ah=%d vw=%d vh=%d xoff=%d yoff=%d \n",
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
	*((volatile unsigned int *)(rga2_drvdata->rga_base + r)) = b;
}

static inline u32 rga2_read(u32 r)
{
	return *((volatile unsigned int *)(rga2_drvdata->rga_base + r));
}

static inline int rga2_init_version(void)
{
	struct rga2_drvdata_t *rga = rga2_drvdata;
	u32 major_version, minor_version;
	u32 reg_version;

	if (!rga) {
		pr_err("rga2_drvdata is null\n");
		return -EINVAL;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_get_sync(rga2_drvdata->dev);
#endif

	clk_prepare_enable(rga2_drvdata->aclk_rga2);
	clk_prepare_enable(rga2_drvdata->hclk_rga2);

	reg_version = rga2_read(0x028);

	clk_disable_unprepare(rga2_drvdata->aclk_rga2);
	clk_disable_unprepare(rga2_drvdata->hclk_rga2);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_put(rga2_drvdata->dev);
#endif

	major_version = (reg_version & RGA2_MAJOR_VERSION_MASK) >> 24;
	minor_version = (reg_version & RGA2_MINOR_VERSION_MASK) >> 20;

	/*
	 * some old rga ip has no rga version register, so force set to 2.00
	 */
	if (!major_version && !minor_version)
		major_version = 2;
	sprintf(rga->version, "%d.%02d", major_version, minor_version);

	return 0;
}

static void rga2_soft_reset(void)
{
	u32 i;
	u32 reg;

	rga2_write((1 << 3) | (1 << 4) | (1 << 6), RGA2_SYS_CTRL);

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
	list_for_each_entry_safe(session, session_tmp, &rga2_service.session,
		list_session)
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

static inline void rga2_queue_power_off_work(void)
{
	queue_delayed_work(system_wq, &rga2_drvdata->power_off_work,
		RGA2_POWER_OFF_DELAY);
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_get_sync(rga2_drvdata->dev);
#endif

	clk_prepare_enable(rga2_drvdata->rga2);
	clk_prepare_enable(rga2_drvdata->aclk_rga2);
	clk_prepare_enable(rga2_drvdata->hclk_rga2);
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

	clk_disable_unprepare(rga2_drvdata->rga2);
	clk_disable_unprepare(rga2_drvdata->aclk_rga2);
	clk_disable_unprepare(rga2_drvdata->hclk_rga2);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_put(rga2_drvdata->dev);
#endif

	wake_unlock(&rga2_drvdata->wake_lock);
    first_RGA2_proc = 0;
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
	if(!((req->render_mode == color_fill_mode)))
	{
	    if (unlikely((req->src.act_w <= 0) || (req->src.act_w > 8191) || (req->src.act_h <= 0) || (req->src.act_h > 8191)))
	    {
		printk("invalid source resolution act_w = %d, act_h = %d\n", req->src.act_w, req->src.act_h);
		return -EINVAL;
	    }
	}

	if(!((req->render_mode == color_fill_mode)))
	{
	    if (unlikely((req->src.vir_w <= 0) || (req->src.vir_w > 8191) || (req->src.vir_h <= 0) || (req->src.vir_h > 8191)))
	    {
		printk("invalid source resolution vir_w = %d, vir_h = %d\n", req->src.vir_w, req->src.vir_h);
		return -EINVAL;
	    }
	}

	//check dst width and height
	if (unlikely((req->dst.act_w <= 0) || (req->dst.act_w > 4096) || (req->dst.act_h <= 0) || (req->dst.act_h > 4096)))
	{
	    printk("invalid destination resolution act_w = %d, act_h = %d\n", req->dst.act_w, req->dst.act_h);
	    return -EINVAL;
	}

	if (unlikely((req->dst.vir_w <= 0) || (req->dst.vir_w > 4096) || (req->dst.vir_h <= 0) || (req->dst.vir_h > 4096)))
	{
	    printk("invalid destination resolution vir_w = %d, vir_h = %d\n", req->dst.vir_w, req->dst.vir_h);
	    return -EINVAL;
	}

	//check src_vir_w
	if(unlikely(req->src.vir_w < req->src.act_w)){
	    printk("invalid src_vir_w act_w = %d, vir_w = %d\n", req->src.act_w, req->src.vir_w);
	    return -EINVAL;
	}

	//check dst_vir_w
	if(unlikely(req->dst.vir_w < req->dst.act_w)){
	    if(req->rotate_mode != 1)
	    {
		printk("invalid dst_vir_w act_h = %d, vir_h = %d\n", req->dst.act_w, req->dst.vir_w);
		return -EINVAL;
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
        printk(KERN_ERR "task_running is no zero\n");

    atomic_add(1, &rga2_service.cmd_num);
	atomic_add(1, &reg->session->task_running);

    cmd_buf = (uint32_t *)rga2_service.cmd_buff + offset*32;
    reg_p = (uint32_t *)reg->cmd_reg;

    for(i=0; i<32; i++)
        cmd_buf[i] = reg_p[i];
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
            kfree(reg);

            return NULL;
        }
    }

    if(RGA2_gen_reg_info((uint8_t *)reg->cmd_reg, req) == -1) {
        printk("gen reg info error\n");
        kfree(reg);

        return NULL;
    }

	reg->sg_src0 = req->sg_src0;
	reg->sg_dst = req->sg_dst;
	reg->sg_src1 = req->sg_src1;
	reg->attach_src0 = req->attach_src0;
	reg->attach_dst = req->attach_dst;
	reg->attach_src1 = req->attach_src1;

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

#ifdef CONFIG_ARM
			dmac_flush_range(&rga2_service.cmd_buff[0], &rga2_service.cmd_buff[32]);
			outer_flush_range(virt_to_phys(&rga2_service.cmd_buff[0]),virt_to_phys(&rga2_service.cmd_buff[32]));
#elif defined(CONFIG_ARM64)
			__dma_flush_range(&rga2_service.cmd_buff[0], &rga2_service.cmd_buff[32]);
#endif

			//rga2_soft_reset();

			rga2_write(0x0, RGA2_SYS_CTRL);

			/* CMD buff */
			rga2_write(virt_to_phys(rga2_service.cmd_buff), RGA2_CMD_BASE);

#if RGA2_TEST
			if(rga2_flag) {
				int32_t i, *p;
				p = rga2_service.cmd_buff;
				printk("CMD_REG\n");
				for (i=0; i<8; i++)
					printk("%.8x %.8x %.8x %.8x\n", p[0 + i*4], p[1+i*4], p[2 + i*4], p[3 + i*4]);
			}
#endif

			/* master mode */
			rga2_write((0x1<<1)|(0x1<<2)|(0x1<<5)|(0x1<<6), RGA2_SYS_CTRL);

			/* All CMD finish int */
			rga2_write(rga2_read(RGA2_INT)|(0x1<<10)|(0x1<<9)|(0x1<<8), RGA2_INT);

#if RGA2_TEST_TIME
			rga2_start = ktime_get();
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

static int rga2_put_dma_buf(struct rga2_req *req, struct rga2_reg *reg)
{
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sgt = NULL;
	struct dma_buf *dma_buf = NULL;

	if (!req && !reg)
		return -EINVAL;

	attach = (!reg) ? req->attach_src0 : reg->attach_src0;
	sgt = (!reg) ? req->sg_src0 : reg->sg_src0;
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

	attach = (!reg) ? req->attach_src1 : reg->attach_src1;
	sgt = (!reg) ? req->sg_src1 : reg->sg_src1;
	if (attach && sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if (attach) {
		dma_buf = attach->dmabuf;
		dma_buf_detach(dma_buf, attach);
		dma_buf_put(dma_buf);
	}

	return 0;
}

static void rga2_del_running_list(void)
{
	struct rga2_mmu_buf_t *tbuf = &rga2_mmu_buf;
	struct rga2_reg *reg;

	while (!list_empty(&rga2_service.running)) {
		reg = list_entry(rga2_service.running.next, struct rga2_reg,
				 status_link);
		if (reg->MMU_len && tbuf) {
			if (tbuf->back + reg->MMU_len > 2 * tbuf->size)
				tbuf->back = reg->MMU_len + tbuf->size;
			else
				tbuf->back += reg->MMU_len;
		}

		rga2_put_dma_buf(NULL, reg);

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

static void rga2_del_running_list_timeout(void)
{
	struct rga2_mmu_buf_t *tbuf = &rga2_mmu_buf;
	struct rga2_reg *reg;

	while (!list_empty(&rga2_service.running)) {
		reg = list_entry(rga2_service.running.next, struct rga2_reg,
				 status_link);
		kfree(reg->MMU_base);
		if (reg->MMU_len && tbuf) {
			if (tbuf->back + reg->MMU_len > 2 * tbuf->size)
				tbuf->back = reg->MMU_len + tbuf->size;
			else
				tbuf->back += reg->MMU_len;
		}

		rga2_put_dma_buf(NULL, reg);

		atomic_sub(1, &reg->session->task_running);
		atomic_sub(1, &rga2_service.total_running);
		rga2_soft_reset();
		if (list_empty(&reg->session->waiting)) {
			atomic_set(&reg->session->done, 1);
			wake_up(&reg->session->wait);
		}
		rga2_reg_deinit(reg);
	}
	return;
}

static int rga2_get_img_info(rga_img_info_t *img,
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

	rga_dev = rga2_drvdata->dev;
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

static int rga2_get_dma_buf(struct rga2_req *req)
{
	struct dma_buf *dma_buf = NULL;
	u8 mmu_flag = 0;
	int ret = 0;

	req->sg_src0 = NULL;
	req->sg_src1 = NULL;
	req->sg_dst = NULL;
	req->sg_els = NULL;
	req->attach_src0 = NULL;
	req->attach_dst = NULL;
	req->attach_src1 = NULL;
	mmu_flag = req->mmu_info.src0_mmu_flag;
	ret = rga2_get_img_info(&req->src, mmu_flag, &req->sg_src0,
				&req->attach_src0);
	if (ret) {
		pr_err("src:rga2_get_img_info fail\n");
		goto err_src;
	}

	mmu_flag = req->mmu_info.dst_mmu_flag;
	ret = rga2_get_img_info(&req->dst, mmu_flag, &req->sg_dst,
				&req->attach_dst);
	if (ret) {
		pr_err("dst:rga2_get_img_info fail\n");
		goto err_dst;
	}

	mmu_flag = req->mmu_info.src1_mmu_flag;
	ret = rga2_get_img_info(&req->src1, mmu_flag, &req->sg_src1,
				&req->attach_src1);
	if (ret) {
		pr_err("src1:rga2_get_img_info fail\n");
		goto err_src1;
	}

	return ret;

err_src1:
	if (req->sg_dst && req->attach_dst) {
		dma_buf_unmap_attachment(req->attach_dst,
					 req->sg_dst, DMA_BIDIRECTIONAL);
		dma_buf = req->attach_dst->dmabuf;
		dma_buf_detach(dma_buf, req->attach_dst);
		dma_buf_put(dma_buf);
	}
err_dst:
	if (req->sg_src0 && req->attach_src0) {
		dma_buf_unmap_attachment(req->attach_src0,
					 req->sg_src0, DMA_BIDIRECTIONAL);
		dma_buf = req->attach_src0->dmabuf;
		dma_buf_detach(dma_buf, req->attach_src0);
		dma_buf_put(dma_buf);
	}
err_src:

	return ret;
}

static int rga2_blit(rga2_session *session, struct rga2_req *req)
{
	int ret = -1;
	int num = 0;
	struct rga2_reg *reg;

	if (rga2_get_dma_buf(req)) {
		pr_err("RGA2 : DMA buf copy error\n");
		return -EFAULT;
	}

	do {
		/* check value if legal */
		ret = rga2_check_param(req);
		if(ret == -EINVAL) {
			pr_err("req argument is inval\n");
			goto err_put_dma_buf;
		}

		reg = rga2_reg_init(session, req);
		if(reg == NULL) {
			pr_err("init reg fail\n");
			goto err_put_dma_buf;
		}

		num = 1;
		mutex_lock(&rga2_service.lock);
		atomic_add(num, &rga2_service.total_running);
		rga2_try_set_reg();
		mutex_unlock(&rga2_service.lock);

		return 0;
	}
	while(0);

err_put_dma_buf:
	rga2_put_dma_buf(req, NULL);

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
	struct rga2_req req_bak;
	int restore = 0;
	int try = 10;
	int ret = -1;
	int ret_timeout = 0;

	memcpy(&req_bak, req, sizeof(req_bak));
retry:

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
		return ret;

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
	if (ret == -ETIMEDOUT && try--) {
		memcpy(req, &req_bak, sizeof(req_bak));
		/*
		 * if rga work timeout with scaling, need do a non-scale work
		 * first, restore hardware status, then do actually work.
		 */
		if (req->src.act_w != req->dst.act_w ||
		    req->src.act_h != req->dst.act_h) {
			req->src.act_w = MIN(320, MIN(req->src.act_w,
						      req->dst.act_w));
			req->src.act_h = MIN(240, MIN(req->src.act_h,
						      req->dst.act_h));
			req->dst.act_w = req->src.act_w;
			req->dst.act_h = req->src.act_h;
			restore = 1;
		}
		goto retry;
	}
	if (!ret && restore) {
		memcpy(req, &req_bak, sizeof(req_bak));
		restore = 0;
		goto retry;
	}

	return ret;
}

static long rga_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
	struct rga2_drvdata_t *rga = rga2_drvdata;
	struct rga2_req req, req_first;
	struct rga_req req_rga;
	int ret = 0;
	rga2_session *session;

	if (!rga) {
		pr_err("rga2_drvdata is null, rga2 is not init\n");
		return -ENODEV;
	}
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

			if (first_RGA2_proc == 0 && req.bitblt_mode == bitblt_mode && rga2_service.dev_mode == 1) {
				memcpy(&req_first, &req, sizeof(struct rga2_req));
				if ((req_first.src.act_w != req_first.dst.act_w)
						|| (req_first.src.act_h != req_first.dst.act_h)) {
					req_first.src.act_w = MIN(320, MIN(req_first.src.act_w, req_first.dst.act_w));
					req_first.src.act_h = MIN(240, MIN(req_first.src.act_h, req_first.dst.act_h));
					req_first.dst.act_w = req_first.src.act_w;
					req_first.dst.act_h = req_first.src.act_h;
					ret = rga2_blit_async(session, &req_first);
				}
				ret = rga2_blit_sync(session, &req);
				first_RGA2_proc = 1;
			}
			else {
				ret = rga2_blit_sync(session, &req);
			}
			break;
		case RGA_BLIT_ASYNC:
			if (unlikely(copy_from_user(&req_rga, (struct rga_req*)arg, sizeof(struct rga_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}

			RGA_MSG_2_RGA2_MSG(&req_rga, &req);

			if (first_RGA2_proc == 0 && req.bitblt_mode == bitblt_mode && rga2_service.dev_mode == 1) {
				memcpy(&req_first, &req, sizeof(struct rga2_req));
				if ((req_first.src.act_w != req_first.dst.act_w)
						|| (req_first.src.act_h != req_first.dst.act_h)) {
					req_first.src.act_w = MIN(320, MIN(req_first.src.act_w, req_first.dst.act_w));
					req_first.src.act_h = MIN(240, MIN(req_first.src.act_h, req_first.dst.act_h));
					req_first.dst.act_w = req_first.src.act_w;
					req_first.dst.act_h = req_first.src.act_h;
					ret = rga2_blit_async(session, &req_first);
				}
				ret = rga2_blit_async(session, &req);
				first_RGA2_proc = 1;
			}
			else {
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
			ret = copy_to_user((void *)arg, rga->version, 16);
			break;
		default:
			ERR("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}

	mutex_unlock(&rga2_service.mutex);

	return ret;
}

#ifdef CONFIG_COMPAT
static long compat_rga_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
	struct rga2_drvdata_t *rga = rga2_drvdata;
	struct rga2_req req, req_first;
	struct rga_req_32 req_rga;
	int ret = 0;
	rga2_session *session;

	if (!rga) {
		pr_err("rga2_drvdata is null, rga2 is not init\n");
		return -ENODEV;
	}
	memset(&req, 0x0, sizeof(req));

	mutex_lock(&rga2_service.mutex);

	session = (rga2_session *)file->private_data;

#if RGA2_TEST_MSG
	printk("use compat_rga_ioctl\n");
#endif

	if (NULL == session) {
		printk("%s [%d] rga thread session is null\n",__FUNCTION__,__LINE__);
		mutex_unlock(&rga2_service.mutex);
		return -EINVAL;
	}

	memset(&req, 0x0, sizeof(req));

	switch (cmd) {
		case RGA_BLIT_SYNC:
			if (unlikely(copy_from_user(&req_rga, compat_ptr((compat_uptr_t)arg), sizeof(struct rga_req_32))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}

			RGA_MSG_2_RGA2_MSG_32(&req_rga, &req);

			if (first_RGA2_proc == 0 && req.bitblt_mode == bitblt_mode && rga2_service.dev_mode == 1) {
				memcpy(&req_first, &req, sizeof(struct rga2_req));
				if ((req_first.src.act_w != req_first.dst.act_w)
						|| (req_first.src.act_h != req_first.dst.act_h)) {
					req_first.src.act_w = MIN(320, MIN(req_first.src.act_w, req_first.dst.act_w));
					req_first.src.act_h = MIN(240, MIN(req_first.src.act_h, req_first.dst.act_h));
					req_first.dst.act_w = req_first.src.act_w;
					req_first.dst.act_h = req_first.src.act_h;
					ret = rga2_blit_async(session, &req_first);
				}
				ret = rga2_blit_sync(session, &req);
				first_RGA2_proc = 1;
			}
			else {
				ret = rga2_blit_sync(session, &req);
			}
			break;
		case RGA_BLIT_ASYNC:
			if (unlikely(copy_from_user(&req_rga, compat_ptr((compat_uptr_t)arg), sizeof(struct rga_req_32))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}
			RGA_MSG_2_RGA2_MSG_32(&req_rga, &req);

			if (first_RGA2_proc == 0 && req.bitblt_mode == bitblt_mode && rga2_service.dev_mode == 1) {
				memcpy(&req_first, &req, sizeof(struct rga2_req));
				if ((req_first.src.act_w != req_first.dst.act_w)
						|| (req_first.src.act_h != req_first.dst.act_h)) {
					req_first.src.act_w = MIN(320, MIN(req_first.src.act_w, req_first.dst.act_w));
					req_first.src.act_h = MIN(240, MIN(req_first.src.act_h, req_first.dst.act_h));
					req_first.dst.act_w = req_first.src.act_w;
					req_first.dst.act_h = req_first.src.act_h;
					ret = rga2_blit_async(session, &req_first);
				}
				ret = rga2_blit_sync(session, &req);
				first_RGA2_proc = 1;
			}
			else {
				ret = rga2_blit_sync(session, &req);
			}

			//if((atomic_read(&rga2_service.total_running) > 8))
			//    ret = rga2_blit_sync(session, &req);
			//else
			//    ret = rga2_blit_async(session, &req);

			break;
		case RGA2_BLIT_SYNC:
			if (unlikely(copy_from_user(&req, compat_ptr((compat_uptr_t)arg), sizeof(struct rga2_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}
			ret = rga2_blit_sync(session, &req);
			break;
		case RGA2_BLIT_ASYNC:
			if (unlikely(copy_from_user(&req, compat_ptr((compat_uptr_t)arg), sizeof(struct rga2_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}

			if((atomic_read(&rga2_service.total_running) > 16))
				ret = rga2_blit_sync(session, &req);
			else
				ret = rga2_blit_async(session, &req);

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
			ret = copy_to_user((void *)arg, rga->version, 16);
			break;
		default:
			ERR("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}

	mutex_unlock(&rga2_service.mutex);

	return ret;
}
#endif


static long rga2_ioctl_kernel(struct rga_req *req_rga)
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

	return nonseekable_open(inode, file);
}

static int rga2_release(struct inode *inode, struct file *file)
{
	int task_running;
	rga2_session *session = (rga2_session *)file->private_data;

	if (NULL == session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);
	if (task_running)
	{
		pr_err("rga2_service session %d still has %d task running when closing\n", session->pid, task_running);
		msleep(100);
	}

	wake_up(&session->wait);
	mutex_lock(&rga2_service.lock);
	list_del(&session->list_session);
	rga2_service_session_clear(session);
	kfree(session);
	mutex_unlock(&rga2_service.lock);

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
#ifdef CONFIG_COMPAT
	.compat_ioctl		= compat_rga_ioctl,
#endif
};

static struct miscdevice rga2_dev ={
	.minor = RGA2_MAJOR,
	.name  = "rga",
	.fops  = &rga2_fops,
};

static const struct of_device_id rockchip_rga_dt_ids[] = {
	{ .compatible = "rockchip,rga2", },
	{},
};

static int rga2_drv_probe(struct platform_device *pdev)
{
	struct rga2_drvdata_t *data;
	struct resource *res;
	int ret = 0;
	struct device_node *np = pdev->dev.of_node;

	mutex_init(&rga2_service.lock);
	mutex_init(&rga2_service.mutex);
	atomic_set(&rga2_service.total_running, 0);
	atomic_set(&rga2_service.src_format_swt, 0);
	rga2_service.last_prc_src_format = 1; /* default is yuv first*/
	rga2_service.enable = false;

	rga2_ioctl_kernel_p = rga2_ioctl_kernel;

	data = devm_kzalloc(&pdev->dev, sizeof(struct rga2_drvdata_t), GFP_KERNEL);
	if(NULL == data)
	{
		ERR("failed to allocate driver data.\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&data->power_off_work, rga2_power_off_work);
	wake_lock_init(&data->wake_lock, WAKE_LOCK_SUSPEND, "rga");

	data->rga2 = devm_clk_get(&pdev->dev, "clk_rga");
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
	data->dev = &pdev->dev;
	rga2_drvdata = data;
	of_property_read_u32(np, "dev_mode", &rga2_service.dev_mode);

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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_enable(&pdev->dev);
#endif
	rga2_init_version();
	pr_info("Driver loaded successfully ver:%s\n", rga2_drvdata->version);

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

	devm_clk_put(&pdev->dev, data->rga2);
	devm_clk_put(&pdev->dev, data->aclk_rga2);
	devm_clk_put(&pdev->dev, data->hclk_rga2);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_disable(&pdev->dev);
#endif

	//kfree(data);
	return 0;
}

static struct platform_driver rga2_driver = {
	.probe		= rga2_drv_probe,
	.remove		= rga2_drv_remove,
	.driver		= {
		.name	= "rga2",
		.of_match_table = of_match_ptr(rockchip_rga_dt_ids),
	},
};


void rga2_test_0(void);

static int __init rga2_init(void)
{
	int ret;
	uint32_t *buf_p;
	uint32_t *buf;

	/* malloc pre scale mid buf mmu table */
	buf_p = kmalloc(1024*256, GFP_KERNEL);
	rga2_mmu_buf.buf_virtual = buf_p;
#if (defined(CONFIG_ARM) && defined(CONFIG_ARM_LPAE))
	buf = (uint32_t *)(uint32_t)virt_to_phys((void *)((unsigned long)buf_p));
#else
	buf = (uint32_t *)virt_to_phys((void *)((unsigned long)buf_p));
#endif
	rga2_mmu_buf.buf = buf;
	rga2_mmu_buf.front = 0;
	rga2_mmu_buf.back = 64*1024;
	rga2_mmu_buf.size = 64*1024;

	rga2_mmu_buf.pages = kmalloc(32768 * sizeof(struct page *), GFP_KERNEL);

	ret = platform_driver_register(&rga2_driver);
	if (ret != 0) {
		printk(KERN_ERR "Platform device register failed (%d).\n", ret);
		return ret;
	}

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

#if RGA2_TEST_CASE
	rga2_test_0();
#endif

	INFO("Module initialized.\n");

	return 0;
}

static void __exit rga2_exit(void)
{
	rga2_power_off();

	kfree(rga2_mmu_buf.buf_virtual);

	platform_driver_unregister(&rga2_driver);
}


#if RGA2_TEST_CASE

void rga2_test_0(void)
{
	struct rga2_req req;
	rga2_session session;
	unsigned int *src, *dst;

	session.pid	= current->pid;
	INIT_LIST_HEAD(&session.waiting);
	INIT_LIST_HEAD(&session.running);
	INIT_LIST_HEAD(&session.list_session);
	init_waitqueue_head(&session.wait);
	/* no need to protect */
	list_add_tail(&session.list_session, &rga2_service.session);
	atomic_set(&session.task_running, 0);
	atomic_set(&session.num_done, 0);

	memset(&req, 0, sizeof(struct rga2_req));
	src = kmalloc(800*480*4, GFP_KERNEL);
	dst = kmalloc(800*480*4, GFP_KERNEL);

	printk("\n********************************\n");
	printk("************ RGA2_TEST ************\n");
	printk("********************************\n\n");

#if 1
	memset(src, 0x80, 800 * 480 * 4);
	memset(dst, 0xcc, 800 * 480 * 4);
#endif
#if 0
	dmac_flush_range(src, &src[800 * 480]);
	outer_flush_range(virt_to_phys(src), virt_to_phys(&src[800 * 480]));

	dmac_flush_range(dst, &dst[800 * 480]);
	outer_flush_range(virt_to_phys(dst), virt_to_phys(&dst[800 * 480]));
#endif

#if 0
	req.pat.act_w = 16;
	req.pat.act_h = 16;
	req.pat.vir_w = 16;
	req.pat.vir_h = 16;
	req.pat.yrgb_addr = virt_to_phys(src);
	req.render_mode = 0;
	rga2_blit_sync(&session, &req);
#endif
	{
		uint32_t i, j;
		uint8_t *sp;

		sp = (uint8_t *)src;
		for (j = 0; j < 240; j++) {
			sp = (uint8_t *)src + j * 320 * 10 / 8;
			for (i = 0; i < 320; i++) {
				if ((i & 3) == 0) {
					sp[i * 5 / 4] = 0;
					sp[i * 5 / 4+1] = 0x1;
				} else if ((i & 3) == 1) {
					sp[i * 5 / 4+1] = 0x4;
				} else if ((i & 3) == 2) {
					sp[i * 5 / 4+1] = 0x10;
				} else if ((i & 3) == 3) {
					sp[i * 5 / 4+1] = 0x40;
			    }
			}
		}
		sp = (uint8_t *)src;
		for (j = 0; j < 100; j++)
			printk("src %.2x\n", sp[j]);
	}
	req.src.act_w = 320;
	req.src.act_h = 240;

	req.src.vir_w = 320;
	req.src.vir_h = 240;
	req.src.yrgb_addr = 0;//(uint32_t)virt_to_phys(src);
	req.src.uv_addr = (unsigned long)virt_to_phys(src);
	req.src.v_addr = 0;
	req.src.format = RGA2_FORMAT_YCbCr_420_SP_10B;

	req.dst.act_w  = 320;
	req.dst.act_h = 240;
	req.dst.x_offset = 0;
	req.dst.y_offset = 0;

	req.dst.vir_w = 320;
	req.dst.vir_h = 240;

	req.dst.yrgb_addr = 0;//((uint32_t)virt_to_phys(dst));
	req.dst.uv_addr = (unsigned long)virt_to_phys(dst);
	req.dst.format = RGA2_FORMAT_YCbCr_420_SP;

	//dst = dst0;

	//req.render_mode = color_fill_mode;
	//req.fg_color = 0x80ffffff;

	req.rotate_mode = 0;
	req.scale_bicu_mode = 2;

#if 0
	//req.alpha_rop_flag = 0;
	//req.alpha_rop_mode = 0x19;
	//req.PD_mode = 3;

	//req.mmu_info.mmu_flag = 0x21;
	//req.mmu_info.mmu_en = 1;

	//printk("src = %.8x\n", req.src.yrgb_addr);
	//printk("src = %.8x\n", req.src.uv_addr);
	//printk("dst = %.8x\n", req.dst.yrgb_addr);
#endif

	rga2_blit_sync(&session, &req);

#if 0
	uint32_t j;
	for (j = 0; j < 320 * 240 * 10 / 8; j++) {
        if (src[j] != dst[j])
		printk("error value dst not equal src j %d, s %.2x d %.2x\n",
			j, src[j], dst[j]);
	}
#endif

#if 1
	{
		uint32_t j;
		uint8_t *dp = (uint8_t *)dst;

		for (j = 0; j < 100; j++)
			printk("%d %.2x\n", j, dp[j]);
	}
#endif

	kfree(src);
	kfree(dst);
}
#endif

late_initcall(rga2_init);
module_exit(rga2_exit);

/* Module information */
MODULE_AUTHOR("zsq@rock-chips.com");
MODULE_DESCRIPTION("Driver for rga device");
MODULE_LICENSE("GPL");
