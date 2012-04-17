/* arch/arm/mach-rk29/vpu.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * author: chenhengming chm@rock-chips.com
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

#ifdef CONFIG_RK29_VPU_DEBUG
#define DEBUG
#define pr_fmt(fmt) "VPU_SERVICE: %s: " fmt, __func__
#else
#define pr_fmt(fmt) "VPU_SERVICE: " fmt
#endif


#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include <asm/uaccess.h>

#include <mach/irqs.h>
#include <plat/vpu_service.h>
#include <mach/pmu.h>
#include <mach/cru.h>


#define DEC_INTERRUPT_REGISTER	   		1
#define PP_INTERRUPT_REGISTER	   		60
#define ENC_INTERRUPT_REGISTER	   		1

#define DEC_INTERRUPT_BIT			 0x100
#define PP_INTERRUPT_BIT			 0x100
#define ENC_INTERRUPT_BIT			 0x1

#define REG_NUM_DEC 				(60)
#define REG_NUM_PP				(41)
#define REG_NUM_ENC 				(96)
#define REG_NUM_DEC_PP				(REG_NUM_DEC+REG_NUM_PP)
#define SIZE_REG(reg)				((reg)*4)

#define DEC_IO_SIZE 				((100 + 1) * 4) /* bytes */
#define ENC_IO_SIZE 				(96 * 4)	/* bytes */
static const u16 dec_hw_ids[] = { 0x8190, 0x8170, 0x9170, 0x9190, 0x6731 };
#if defined(CONFIG_ARCH_RK29)
static const u16 enc_hw_ids[] = { 0x6280, 0x7280, 0x8270 };
#define DEC_PHY_OFFSET 				0x200
#elif defined(CONFIG_ARCH_RK30)
static const u16 enc_hw_ids[] = { 0x6280, 0x7280, 0x8270, 0x8290, 0x4831 };
#define DEC_PHY_OFFSET 				0x400
#define RK29_VCODEC_PHYS			RK30_VCODEC_PHYS
#endif

#define VPU_REG_EN_ENC				14
#define VPU_REG_ENC_GATE			2
#define VPU_REG_ENC_GATE_BIT			(1<<4)

#define VPU_REG_EN_DEC				1
#define VPU_REG_DEC_GATE			2
#define VPU_REG_DEC_GATE_BIT			(1<<10)
#define VPU_REG_EN_PP				0
#define VPU_REG_PP_GATE 			1
#define VPU_REG_PP_GATE_BIT 			(1<<8)
#define VPU_REG_EN_DEC_PP			1
#define VPU_REG_DEC_PP_GATE 			61
#define VPU_REG_DEC_PP_GATE_BIT 		(1<<8)


/**
 * struct for process session which connect to vpu
 *
 * @author ChenHengming (2011-5-3)
 */
typedef struct vpu_session {
	VPU_CLIENT_TYPE		type;
	/* a linked list of data so we can access them for debugging */
	struct list_head	list_session;
	/* a linked list of register data waiting for process */
	struct list_head	waiting;
	/* a linked list of register data in processing */
	struct list_head	running;
	/* a linked list of register data processed */
	struct list_head	done;
	wait_queue_head_t	wait;
	pid_t			pid;
	atomic_t		task_running;
} vpu_session;

/**
 * struct for process register set
 *
 * @author ChenHengming (2011-5-4)
 */
#define VPU_REG_NUM_MAX                     (((VPU_REG_NUM_ENC)>(VPU_REG_NUM_DEC_PP))?(VPU_REG_NUM_ENC):(VPU_REG_NUM_DEC_PP))
typedef struct vpu_reg {
	VPU_CLIENT_TYPE		type;
	vpu_session 		*session;
	struct list_head	session_link;		/* link to vpu service session */
	struct list_head	status_link;		/* link to register set list */
	unsigned long		size;
	unsigned long		reg[VPU_REG_NUM_MAX];
} vpu_reg;

typedef struct vpu_device {
	unsigned long		iobaseaddr;
	unsigned int		iosize;
	volatile u32		*hwregs;
} vpu_device;

typedef struct vpu_service_info {
	spinlock_t		lock;
	spinlock_t		lock_power;
	struct timer_list	timer;			/* timer for power off */
	struct list_head	waiting;		/* link to link_reg in struct vpu_reg */
	struct list_head	running;		/* link to link_reg in struct vpu_reg */
	struct list_head	done;			/* link to link_reg in struct vpu_reg */
	struct list_head	session;		/* link to list_session in struct vpu_session */
	atomic_t		total_running;
	bool			enabled;
	vpu_reg			*reg_codec;
	vpu_reg			*reg_pproc;
	vpu_reg			*reg_resev;
	VPUHwDecConfig_t	dec_config;
	VPUHwEncConfig_t	enc_config;
} vpu_service_info;

typedef struct vpu_request
{
	unsigned long   *req;
	unsigned long   size;
} vpu_request;

static struct clk *pd_video;
static struct clk *clk_vpu; /* for power on notify */
static struct clk *aclk_vepu;
static struct clk *hclk_vepu;
static struct clk *aclk_ddr_vepu;
static struct clk *hclk_cpu_vcodec;
static vpu_service_info service;
static vpu_device 	dec_dev;
static vpu_device 	enc_dev;

#define POWER_OFF_DELAY	4*HZ /* 4s */
#define TIMEOUT_DELAY	2*HZ /* 2s */

static void vpu_get_clk(void)
{
	pd_video	= clk_get(NULL, "pd_video");
	clk_vpu		= clk_get(NULL, "vpu");
	aclk_vepu 	= clk_get(NULL, "aclk_vepu");
	hclk_vepu 	= clk_get(NULL, "hclk_vepu");
	aclk_ddr_vepu 	= clk_get(NULL, "aclk_ddr_vepu");
	hclk_cpu_vcodec	= clk_get(NULL, "hclk_cpu_vcodec");
}

static void vpu_put_clk(void)
{
	clk_put(pd_video);
	clk_put(clk_vpu);
	clk_put(aclk_vepu);
	clk_put(hclk_vepu);
	clk_put(aclk_ddr_vepu);
	clk_put(hclk_cpu_vcodec);
}

static void vpu_reset(void)
{
#if defined(CONFIG_ARCH_RK29)
	clk_disable(aclk_ddr_vepu);
	cru_set_soft_reset(SOFT_RST_CPU_VODEC_A2A_AHB, true);
	cru_set_soft_reset(SOFT_RST_DDR_VCODEC_PORT, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB_BUS, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI_BUS, true);
	mdelay(10);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI_BUS, false);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB_BUS, false);
	cru_set_soft_reset(SOFT_RST_DDR_VCODEC_PORT, false);
	cru_set_soft_reset(SOFT_RST_CPU_VODEC_A2A_AHB, false);
	clk_enable(aclk_ddr_vepu);
#elif defined(CONFIG_ARCH_RK30)
	pmu_set_idle_request(IDLE_REQ_VIDEO, true);
	cru_set_soft_reset(SOFT_RST_CPU_VCODEC, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_NIU_AXI, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI, true);
	mdelay(1);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI, false);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB, false);
	cru_set_soft_reset(SOFT_RST_VCODEC_NIU_AXI, false);
	cru_set_soft_reset(SOFT_RST_CPU_VCODEC, false);
	pmu_set_idle_request(IDLE_REQ_VIDEO, false);
#endif
	service.reg_codec = NULL;
	service.reg_pproc = NULL;
	service.reg_resev = NULL;
}

static void reg_deinit(vpu_reg *reg);
static void vpu_service_session_clear(vpu_session *session)
{
	vpu_reg *reg, *n;
	list_for_each_entry_safe(reg, n, &session->waiting, session_link) {
		reg_deinit(reg);
	}
	list_for_each_entry_safe(reg, n, &session->running, session_link) {
		reg_deinit(reg);
	}
	list_for_each_entry_safe(reg, n, &session->done, session_link) {
		reg_deinit(reg);
	}
}

static void vpu_service_dump(void)
{
	int running;
	vpu_reg *reg, *reg_tmp;
	vpu_session *session, *session_tmp;

	running = atomic_read(&service.total_running);
	printk("total_running %d\n", running);

	printk("reg_codec 0x%.8x\n", (unsigned int)service.reg_codec);
	printk("reg_pproc 0x%.8x\n", (unsigned int)service.reg_pproc);
	printk("reg_resev 0x%.8x\n", (unsigned int)service.reg_resev);

	list_for_each_entry_safe(session, session_tmp, &service.session, list_session) {
		printk("session pid %d type %d:\n", session->pid, session->type);
		running = atomic_read(&session->task_running);
		printk("task_running %d\n", running);
		list_for_each_entry_safe(reg, reg_tmp, &session->waiting, session_link) {
			printk("waiting register set 0x%.8x\n", (unsigned int)reg);
		}
		list_for_each_entry_safe(reg, reg_tmp, &session->running, session_link) {
			printk("running register set 0x%.8x\n", (unsigned int)reg);
		}
		list_for_each_entry_safe(reg, reg_tmp, &session->done, session_link) {
			printk("done    register set 0x%.8x\n", (unsigned int)reg);
		}
	}
}

static void vpu_service_power_off(void)
{
	int total_running;

	spin_lock_bh(&service.lock_power);
	if (!service.enabled) {
		spin_unlock_bh(&service.lock_power);
		return;
	}

	service.enabled = false;
	total_running = atomic_read(&service.total_running);
	if (total_running) {
		pr_alert("power off when %d task running!!\n", total_running);
		mdelay(50);
		pr_alert("delay 50 ms for running task\n");
		vpu_service_dump();
	}

	printk("vpu: power off...");
#ifdef CONFIG_ARCH_RK29
	pmu_set_power_domain(PD_VCODEC, false);
#else
	clk_disable(pd_video);
#endif
	udelay(10);
	clk_disable(hclk_cpu_vcodec);
	clk_disable(aclk_ddr_vepu);
	clk_disable(hclk_vepu);
	clk_disable(aclk_vepu);
	clk_disable(clk_vpu);
	printk("done\n");
	spin_unlock_bh(&service.lock_power);
}

static void vpu_service_power_off_work_func(unsigned long data)
{
	printk("delayed ");
	vpu_service_power_off();
}

static void vpu_service_power_maintain(void)
{
	if (service.enabled) {
		mod_timer(&service.timer, jiffies + POWER_OFF_DELAY);
	} else {
		pr_err("maintain power when power is off!\n");
	}
}

static void vpu_service_power_on(void)
{
	clk_enable(clk_vpu); /* notify vpu on without lock. */

	spin_lock_bh(&service.lock_power);
	if (!service.enabled) {
		service.enabled = true;
		printk("vpu: power on\n");

		clk_enable(clk_vpu);
		clk_enable(aclk_vepu);
		clk_enable(hclk_vepu);
		clk_enable(hclk_cpu_vcodec);
		udelay(10);
#ifdef CONFIG_ARCH_RK29
		pmu_set_power_domain(PD_VCODEC, true);
#else
		clk_enable(pd_video);
#endif
		udelay(10);
		clk_enable(aclk_ddr_vepu);
		init_timer(&service.timer);
		service.timer.expires = jiffies + POWER_OFF_DELAY;
		service.timer.function = vpu_service_power_off_work_func;
		add_timer(&service.timer);
		spin_unlock_bh(&service.lock_power);
	} else {
		spin_unlock_bh(&service.lock_power);
		vpu_service_power_maintain();
	}

	clk_disable(clk_vpu);
}

static vpu_reg *reg_init(vpu_session *session, void __user *src, unsigned long size)
{
	unsigned long flag;
	vpu_reg *reg = kmalloc(sizeof(vpu_reg), GFP_KERNEL);
	if (NULL == reg) {
		pr_err("kmalloc fail in reg_init\n");
		return NULL;
	}

	reg->session = session;
	reg->type = session->type;
	reg->size = size;
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

	if (copy_from_user(&reg->reg[0], (void __user *)src, size)) {
		pr_err("copy_from_user failed in reg_init\n");
		kfree(reg);
		return NULL;
	}

	spin_lock_irqsave(&service.lock, flag);
	list_add_tail(&reg->status_link, &service.waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	spin_unlock_irqrestore(&service.lock, flag);

	return reg;
}

static void reg_deinit(vpu_reg *reg)
{
	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	kfree(reg);
	if (reg == service.reg_codec) service.reg_codec = NULL;
	if (reg == service.reg_pproc) service.reg_pproc = NULL;
}

static void reg_from_wait_to_run(vpu_reg *reg)
{
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &service.running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
}

static void reg_copy_from_hw(vpu_reg *reg, volatile u32 *src, u32 count)
{
	int i;
	u32 *dst = (u32 *)&reg->reg[0];
	for (i = 0; i < count; i++)
		*dst++ = *src++;
}

static void reg_from_run_to_done(vpu_reg *reg)
{
	spin_lock(&service.lock);
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &service.done);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->done);

	switch (reg->type) {
	case VPU_ENC : {
		service.reg_codec = NULL;
		reg_copy_from_hw(reg, enc_dev.hwregs, REG_NUM_ENC);
		break;
	}
	case VPU_DEC : {
		service.reg_codec = NULL;
		reg_copy_from_hw(reg, dec_dev.hwregs, REG_NUM_DEC);
		break;
	}
	case VPU_PP : {
		service.reg_pproc = NULL;
		reg_copy_from_hw(reg, dec_dev.hwregs + PP_INTERRUPT_REGISTER, REG_NUM_PP);
		dec_dev.hwregs[PP_INTERRUPT_REGISTER] = 0;
		break;
	}
	case VPU_DEC_PP : {
		service.reg_codec = NULL;
		service.reg_pproc = NULL;
		reg_copy_from_hw(reg, dec_dev.hwregs, REG_NUM_DEC_PP);
		dec_dev.hwregs[PP_INTERRUPT_REGISTER] = 0;
		break;
	}
	default : {
		pr_err("copy reg from hw with unknown type %d\n", reg->type);
		break;
	}
	}
	atomic_sub(1, &reg->session->task_running);
	atomic_sub(1, &service.total_running);
	wake_up_interruptible_sync(&reg->session->wait);
	spin_unlock(&service.lock);
}

void reg_copy_to_hw(vpu_reg *reg)
{
	int i;
	u32 *src = (u32 *)&reg->reg[0];
	atomic_add(1, &service.total_running);
	atomic_add(1, &reg->session->task_running);
	switch (reg->type) {
	case VPU_ENC : {
		u32 *dst = (u32 *)enc_dev.hwregs;
#if defined(CONFIG_ARCH_RK30)
		cru_set_soft_reset(SOFT_RST_CPU_VCODEC, true);
		cru_set_soft_reset(SOFT_RST_VCODEC_AHB, true);
		cru_set_soft_reset(SOFT_RST_VCODEC_AHB, false);
		cru_set_soft_reset(SOFT_RST_CPU_VCODEC, false);
#endif
		service.reg_codec = reg;

		dst[VPU_REG_EN_ENC] = src[VPU_REG_EN_ENC] & 0x6;

		for (i = 0; i < VPU_REG_EN_ENC; i++)
			dst[i] = src[i];

		for (i = VPU_REG_EN_ENC + 1; i < REG_NUM_ENC; i++)
			dst[i] = src[i];

		dsb();

		dst[VPU_REG_ENC_GATE] = src[VPU_REG_ENC_GATE] | VPU_REG_ENC_GATE_BIT;
		dst[VPU_REG_EN_ENC]   = src[VPU_REG_EN_ENC];
	} break;
	case VPU_DEC : {
		u32 *dst = (u32 *)dec_dev.hwregs;
		service.reg_codec = reg;

		for (i = REG_NUM_DEC - 1; i > VPU_REG_DEC_GATE; i--)
			dst[i] = src[i];

		dsb();

		dst[VPU_REG_DEC_GATE] = src[VPU_REG_DEC_GATE] | VPU_REG_DEC_GATE_BIT;
		dst[VPU_REG_EN_DEC]   = src[VPU_REG_EN_DEC];
	} break;
	case VPU_PP : {
		u32 *dst = (u32 *)dec_dev.hwregs + PP_INTERRUPT_REGISTER;
		service.reg_pproc = reg;

		dst[VPU_REG_PP_GATE] = src[VPU_REG_PP_GATE] | VPU_REG_PP_GATE_BIT;

		for (i = VPU_REG_PP_GATE + 1; i < REG_NUM_PP; i++)
			dst[i] = src[i];

		dsb();

		dst[VPU_REG_EN_PP] = src[VPU_REG_EN_PP];
	} break;
	case VPU_DEC_PP : {
		u32 *dst = (u32 *)dec_dev.hwregs;
		service.reg_codec = reg;
		service.reg_pproc = reg;

		for (i = VPU_REG_EN_DEC_PP + 1; i < REG_NUM_DEC_PP; i++)
			dst[i] = src[i];

		dst[VPU_REG_EN_DEC_PP]   = src[VPU_REG_EN_DEC_PP] | 0x2;
		dsb();

		dst[VPU_REG_DEC_PP_GATE] = src[VPU_REG_DEC_PP_GATE] | VPU_REG_PP_GATE_BIT;
		dst[VPU_REG_DEC_GATE]	 = src[VPU_REG_DEC_GATE]    | VPU_REG_DEC_GATE_BIT;
		dst[VPU_REG_EN_DEC]	 = src[VPU_REG_EN_DEC];
	} break;
	default : {
		pr_err("unsupport session type %d", reg->type);
		atomic_sub(1, &service.total_running);
		atomic_sub(1, &reg->session->task_running);
		break;
	}
	}
}

static void try_set_reg(void)
{
	unsigned long flag;
	// first get reg from reg list
	spin_lock_irqsave(&service.lock, flag);
	if (!list_empty(&service.waiting)) {
		int can_set = 0;
		vpu_reg *reg = list_entry(service.waiting.next, vpu_reg, status_link);

		vpu_service_power_maintain();
		switch (reg->type) {
		case VPU_ENC : {
			if ((NULL == service.reg_codec) &&  (NULL == service.reg_pproc))
			can_set = 1;
		} break;
		case VPU_DEC : {
			if (NULL == service.reg_codec)
				can_set = 1;
		} break;
		case VPU_PP : {
			if (NULL == service.reg_codec) {
				if (NULL == service.reg_pproc)
					can_set = 1;
			} else {
				if ((VPU_DEC == service.reg_codec->type) && (NULL == service.reg_pproc))
					can_set = 1;
			}
		} break;
		case VPU_DEC_PP : {
			if ((NULL == service.reg_codec) && (NULL == service.reg_pproc))
				can_set = 1;
			} break;
		default : {
			printk("undefined reg type %d\n", reg->type);
		} break;
		}
		if (can_set) {
			reg_from_wait_to_run(reg);
			reg_copy_to_hw(reg);
		}
	}
	spin_unlock_irqrestore(&service.lock, flag);
}

static int return_reg(vpu_reg *reg, u32 __user *dst)
{
	int ret = 0;
	switch (reg->type) {
	case VPU_ENC : {
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(REG_NUM_ENC)))
			ret = -EFAULT;
		break;
	}
	case VPU_DEC : {
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(REG_NUM_DEC)))
			ret = -EFAULT;
		break;
	}
	case VPU_PP : {
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(REG_NUM_PP)))
			ret = -EFAULT;
		break;
	}
	case VPU_DEC_PP : {
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(REG_NUM_DEC_PP)))
			ret = -EFAULT;
		break;
	}
	default : {
		ret = -EFAULT;
		pr_err("copy reg to user with unknown type %d\n", reg->type);
		break;
	}
	}
	reg_deinit(reg);
	return ret;
}

static long vpu_service_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	vpu_session *session = (vpu_session *)filp->private_data;
	if (NULL == session) {
		return -EINVAL;
	}

	switch (cmd) {
	case VPU_IOC_SET_CLIENT_TYPE : {
	session->type = (VPU_CLIENT_TYPE)arg;
		break;
	}
	case VPU_IOC_GET_HW_FUSE_STATUS : {
		vpu_request req;
		if (copy_from_user(&req, (void __user *)arg, sizeof(vpu_request))) {
			pr_err("VPU_IOC_GET_HW_FUSE_STATUS copy_from_user failed\n");
			return -EFAULT;
		} else {
			if (VPU_ENC != session->type) {
				if (copy_to_user((void __user *)req.req, &service.dec_config, sizeof(VPUHwDecConfig_t))) {
					pr_err("VPU_IOC_GET_HW_FUSE_STATUS copy_to_user failed type %d\n", session->type);
					return -EFAULT;
				}
			} else {
				if (copy_to_user((void __user *)req.req, &service.enc_config, sizeof(VPUHwEncConfig_t))) {
					pr_err("VPU_IOC_GET_HW_FUSE_STATUS copy_to_user failed type %d\n", session->type);
					return -EFAULT;
				}
			}
		}

		break;
	}
	case VPU_IOC_SET_REG : {
		vpu_request req;
		vpu_reg *reg;
		if (copy_from_user(&req, (void __user *)arg, sizeof(vpu_request))) {
			pr_err("VPU_IOC_SET_REG copy_from_user failed\n");
			return -EFAULT;
		}

		reg = reg_init(session, (void __user *)req.req, req.size);
		if (NULL == reg) {
			return -EFAULT;
		} else {
			vpu_service_power_on();
			try_set_reg();
		}

		break;
	}
	case VPU_IOC_GET_REG : {
		vpu_request req;
		vpu_reg *reg;
		unsigned long flag;
		if (copy_from_user(&req, (void __user *)arg, sizeof(vpu_request))) {
			pr_err("VPU_IOC_GET_REG copy_from_user failed\n");
			return -EFAULT;
		} else {
			int ret = wait_event_interruptible_timeout(session->wait, !list_empty(&session->done), TIMEOUT_DELAY);
			if (unlikely(ret < 0)) {
				pr_err("pid %d wait task ret %d\n", session->pid, ret);
			} else if (0 == ret) {
				pr_err("pid %d wait %d task done timeout\n", session->pid, atomic_read(&session->task_running));
				ret = -ETIMEDOUT;
			}
			spin_lock_irqsave(&service.lock, flag);
			if (ret < 0) {
				int task_running = atomic_read(&session->task_running);
				vpu_service_dump();
				if (task_running) {
					atomic_set(&session->task_running, 0);
					atomic_sub(task_running, &service.total_running);
					printk("%d task is running but not return, reset hardware...", task_running);
					vpu_reset();
					printk("done\n");
				}
				vpu_service_session_clear(session);
				spin_unlock_irqrestore(&service.lock, flag);
				return ret;
			}
			spin_unlock_irqrestore(&service.lock, flag);
		}
		spin_lock_irqsave(&service.lock, flag);
		reg = list_entry(session->done.next, vpu_reg, session_link);
		return_reg(reg, (u32 __user *)req.req);
		spin_unlock_irqrestore(&service.lock, flag);
		break;
	}
	default : {
		pr_err("unknow vpu service ioctl cmd %x\n", cmd);
		break;
	}
	}

	return 0;
}

static int vpu_service_check_hw_id(struct vpu_device * dev, const u16 *hwids, size_t num)
{
	u32 hwid = readl(dev->hwregs);
	pr_info("HW ID = 0x%08x\n", hwid);

	hwid = (hwid >> 16) & 0xFFFF;	/* product version only */

	while (num--) {
		if (hwid == hwids[num]) {
			pr_info("Compatible HW found at 0x%08lx\n", dev->iobaseaddr);
			return 1;
		}
	}

	pr_info("No Compatible HW found at 0x%08lx\n", dev->iobaseaddr);
	return 0;
}

static void vpu_service_release_io(void)
{
	if (dec_dev.hwregs)
		iounmap((void *)dec_dev.hwregs);
	release_mem_region(dec_dev.iobaseaddr, dec_dev.iosize);

	if (enc_dev.hwregs)
		iounmap((void *)enc_dev.hwregs);
	release_mem_region(enc_dev.iobaseaddr, enc_dev.iosize);
}

static int vpu_service_reserve_io(void)
{
	unsigned long iobaseaddr;
	unsigned long iosize;

	iobaseaddr 	= dec_dev.iobaseaddr;
	iosize		= dec_dev.iosize;

	if (!request_mem_region(iobaseaddr, iosize, "vdpu_io")) {
		pr_info("failed to reserve dec HW regs\n");
		return -EBUSY;
	}

	dec_dev.hwregs = (volatile u32 *)ioremap_nocache(iobaseaddr, iosize);

	if (dec_dev.hwregs == NULL) {
		pr_info("failed to ioremap dec HW regs\n");
		goto err;
	}

	/* check for correct HW */
	if (!vpu_service_check_hw_id(&dec_dev, dec_hw_ids, ARRAY_SIZE(dec_hw_ids))) {
		goto err;
	}

	iobaseaddr 	= enc_dev.iobaseaddr;
	iosize		= enc_dev.iosize;

	if (!request_mem_region(iobaseaddr, iosize, "hx280enc")) {
		pr_info("failed to reserve enc HW regs\n");
		goto err;
	}

	enc_dev.hwregs = (volatile u32 *)ioremap_nocache(iobaseaddr, iosize);

	if (enc_dev.hwregs == NULL) {
		pr_info("failed to ioremap enc HW regs\n");
		goto err;
	}

	/* check for correct HW */
	if (!vpu_service_check_hw_id(&enc_dev, enc_hw_ids, ARRAY_SIZE(enc_hw_ids))) {
		goto err;
	}
	return 0;

err:
	vpu_service_release_io();
	return -EBUSY;
}

static int vpu_service_open(struct inode *inode, struct file *filp)
{
	vpu_session *session = (vpu_session *)kmalloc(sizeof(vpu_session), GFP_KERNEL);
	if (NULL == session) {
		pr_err("unable to allocate memory for vpu_session.");
		return -ENOMEM;
	}

	session->type	= VPU_TYPE_BUTT;
	session->pid	= current->pid;
	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->done);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	/* no need to protect */
	list_add_tail(&session->list_session, &service.session);
	atomic_set(&session->task_running, 0);
	filp->private_data = (void *)session;

	pr_debug("dev opened\n");
	return nonseekable_open(inode, filp);
}

static int vpu_service_release(struct inode *inode, struct file *filp)
{
	int task_running;
	unsigned long flag;
	vpu_session *session = (vpu_session *)filp->private_data;
	if (NULL == session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);
	if (task_running) {
		pr_err("vpu_service session %d still has %d task running when closing\n", session->pid, task_running);
		msleep(50);
	}
	wake_up_interruptible_sync(&session->wait);

	spin_lock_irqsave(&service.lock, flag);
	/* remove this filp from the asynchronusly notified filp's */
	//vpu_service_fasync(-1, filp, 0);
	list_del(&session->list_session);

	vpu_service_session_clear(session);

	kfree(session);
	spin_unlock_irqrestore(&service.lock, flag);

	pr_debug("dev closed\n");
	return 0;
}

static const struct file_operations vpu_service_fops = {
	.unlocked_ioctl = vpu_service_ioctl,
	.open		= vpu_service_open,
	.release	= vpu_service_release,
	//.fasync 	= vpu_service_fasync,
};

static struct miscdevice vpu_service_misc_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "vpu_service",
	.fops		= &vpu_service_fops,
};

static void vpu_service_shutdown(struct platform_device *pdev)
{
	pr_cont("shutdown...");
	del_timer(&service.timer);
	vpu_service_power_off();
	pr_cont("done\n");
}

static int vpu_service_suspend(struct platform_device *pdev, pm_message_t state)
{
	bool enabled;
	pr_info("suspend...");
	del_timer(&service.timer);
	enabled = service.enabled;
	vpu_service_power_off();
	service.enabled = enabled;
	return 0;
}

static int vpu_service_resume(struct platform_device *pdev)
{
	pr_info("resume...");
	if (service.enabled) {
		service.enabled = false;
		vpu_service_power_on();
		try_set_reg();
	}
	return 0;
}

static struct platform_device vpu_service_device = {
	.name		   = "vpu_service",
	.id 		   = -1,
};

static struct platform_driver vpu_service_driver = {
	.driver    = {
		.name  = "vpu_service",
		.owner = THIS_MODULE,
	},
	.shutdown  = vpu_service_shutdown,
	.suspend   = vpu_service_suspend,
	.resume    = vpu_service_resume,
};

static void get_hw_info(void)
{
	VPUHwDecConfig_t *dec = &service.dec_config;
	VPUHwEncConfig_t *enc = &service.enc_config;
	u32 configReg   = dec_dev.hwregs[VPU_DEC_HWCFG0];
	u32 asicID      = dec_dev.hwregs[0];

	dec->h264Support    = (configReg >> DWL_H264_E) & 0x3U;
	dec->jpegSupport    = (configReg >> DWL_JPEG_E) & 0x01U;
	if (dec->jpegSupport && ((configReg >> DWL_PJPEG_E) & 0x01U))
		dec->jpegSupport = JPEG_PROGRESSIVE;
	dec->mpeg4Support   = (configReg >> DWL_MPEG4_E) & 0x3U;
	dec->vc1Support     = (configReg >> DWL_VC1_E) & 0x3U;
	dec->mpeg2Support   = (configReg >> DWL_MPEG2_E) & 0x01U;
	dec->sorensonSparkSupport = (configReg >> DWL_SORENSONSPARK_E) & 0x01U;
	dec->refBufSupport  = (configReg >> DWL_REF_BUFF_E) & 0x01U;
	dec->vp6Support     = (configReg >> DWL_VP6_E) & 0x01U;
	dec->maxDecPicWidth = configReg & 0x07FFU;

	/* 2nd Config register */
	configReg   = dec_dev.hwregs[VPU_DEC_HWCFG1];
	if (dec->refBufSupport) {
		if ((configReg >> DWL_REF_BUFF_ILACE_E) & 0x01U)
			dec->refBufSupport |= 2;
		if ((configReg >> DWL_REF_BUFF_DOUBLE_E) & 0x01U)
			dec->refBufSupport |= 4;
	}
	dec->customMpeg4Support = (configReg >> DWL_MPEG4_CUSTOM_E) & 0x01U;
	dec->vp7Support     = (configReg >> DWL_VP7_E) & 0x01U;
	dec->vp8Support     = (configReg >> DWL_VP8_E) & 0x01U;
	dec->avsSupport     = (configReg >> DWL_AVS_E) & 0x01U;

	/* JPEG xtensions */
	if (((asicID >> 16) >= 0x8190U) || ((asicID >> 16) == 0x6731U)) {
		dec->jpegESupport = (configReg >> DWL_JPEG_EXT_E) & 0x01U;
	} else {
		dec->jpegESupport = JPEG_EXT_NOT_SUPPORTED;
	}

	if (((asicID >> 16) >= 0x9170U) || ((asicID >> 16) == 0x6731U) ) {
		dec->rvSupport = (configReg >> DWL_RV_E) & 0x03U;
	} else {
		dec->rvSupport = RV_NOT_SUPPORTED;
	}

	dec->mvcSupport = (configReg >> DWL_MVC_E) & 0x03U;

	if (dec->refBufSupport && (asicID >> 16) == 0x6731U ) {
		dec->refBufSupport |= 8; /* enable HW support for offset */
	}

	{
	VPUHwFuseStatus_t hwFuseSts;
	/* Decoder fuse configuration */
	u32 fuseReg = dec_dev.hwregs[VPU_DEC_HW_FUSE_CFG];

	hwFuseSts.h264SupportFuse = (fuseReg >> DWL_H264_FUSE_E) & 0x01U;
	hwFuseSts.mpeg4SupportFuse = (fuseReg >> DWL_MPEG4_FUSE_E) & 0x01U;
	hwFuseSts.mpeg2SupportFuse = (fuseReg >> DWL_MPEG2_FUSE_E) & 0x01U;
	hwFuseSts.sorensonSparkSupportFuse = (fuseReg >> DWL_SORENSONSPARK_FUSE_E) & 0x01U;
	hwFuseSts.jpegSupportFuse = (fuseReg >> DWL_JPEG_FUSE_E) & 0x01U;
	hwFuseSts.vp6SupportFuse = (fuseReg >> DWL_VP6_FUSE_E) & 0x01U;
	hwFuseSts.vc1SupportFuse = (fuseReg >> DWL_VC1_FUSE_E) & 0x01U;
	hwFuseSts.jpegProgSupportFuse = (fuseReg >> DWL_PJPEG_FUSE_E) & 0x01U;
	hwFuseSts.rvSupportFuse = (fuseReg >> DWL_RV_FUSE_E) & 0x01U;
	hwFuseSts.avsSupportFuse = (fuseReg >> DWL_AVS_FUSE_E) & 0x01U;
	hwFuseSts.vp7SupportFuse = (fuseReg >> DWL_VP7_FUSE_E) & 0x01U;
	hwFuseSts.vp8SupportFuse = (fuseReg >> DWL_VP8_FUSE_E) & 0x01U;
	hwFuseSts.customMpeg4SupportFuse = (fuseReg >> DWL_CUSTOM_MPEG4_FUSE_E) & 0x01U;
	hwFuseSts.mvcSupportFuse = (fuseReg >> DWL_MVC_FUSE_E) & 0x01U;

	/* check max. decoder output width */

	if (fuseReg & 0x8000U)
		hwFuseSts.maxDecPicWidthFuse = 1920;
	else if (fuseReg & 0x4000U)
		hwFuseSts.maxDecPicWidthFuse = 1280;
	else if (fuseReg & 0x2000U)
		hwFuseSts.maxDecPicWidthFuse = 720;
	else if (fuseReg & 0x1000U)
		hwFuseSts.maxDecPicWidthFuse = 352;
	else    /* remove warning */
		hwFuseSts.maxDecPicWidthFuse = 352;

	hwFuseSts.refBufSupportFuse = (fuseReg >> DWL_REF_BUFF_FUSE_E) & 0x01U;

	/* Pp configuration */
	configReg = dec_dev.hwregs[VPU_PP_HW_SYNTH_CFG];

	if ((configReg >> DWL_PP_E) & 0x01U) {
		dec->ppSupport = 1;
		dec->maxPpOutPicWidth = configReg & 0x07FFU;
		/*pHwCfg->ppConfig = (configReg >> DWL_CFG_E) & 0x0FU; */
		dec->ppConfig = configReg;
	} else {
		dec->ppSupport = 0;
		dec->maxPpOutPicWidth = 0;
		dec->ppConfig = 0;
	}

	/* check the HW versio */
	if (((asicID >> 16) >= 0x8190U) || ((asicID >> 16) == 0x6731U))	{
		/* Pp configuration */
		configReg = dec_dev.hwregs[VPU_DEC_HW_FUSE_CFG];

		if ((configReg >> DWL_PP_E) & 0x01U) {
			/* Pp fuse configuration */
			u32 fuseRegPp = dec_dev.hwregs[VPU_PP_HW_FUSE_CFG];

			if ((fuseRegPp >> DWL_PP_FUSE_E) & 0x01U) {
				hwFuseSts.ppSupportFuse = 1;
				/* check max. pp output width */
				if      (fuseRegPp & 0x8000U) hwFuseSts.maxPpOutPicWidthFuse = 1920;
				else if (fuseRegPp & 0x4000U) hwFuseSts.maxPpOutPicWidthFuse = 1280;
				else if (fuseRegPp & 0x2000U) hwFuseSts.maxPpOutPicWidthFuse = 720;
				else if (fuseRegPp & 0x1000U) hwFuseSts.maxPpOutPicWidthFuse = 352;
				else                          hwFuseSts.maxPpOutPicWidthFuse = 352;
				hwFuseSts.ppConfigFuse = fuseRegPp;
			} else {
				hwFuseSts.ppSupportFuse = 0;
				hwFuseSts.maxPpOutPicWidthFuse = 0;
				hwFuseSts.ppConfigFuse = 0;
			}
		} else {
			hwFuseSts.ppSupportFuse = 0;
			hwFuseSts.maxPpOutPicWidthFuse = 0;
			hwFuseSts.ppConfigFuse = 0;
		}

		if (dec->maxDecPicWidth > hwFuseSts.maxDecPicWidthFuse)
			dec->maxDecPicWidth = hwFuseSts.maxDecPicWidthFuse;
		if (dec->maxPpOutPicWidth > hwFuseSts.maxPpOutPicWidthFuse)
			dec->maxPpOutPicWidth = hwFuseSts.maxPpOutPicWidthFuse;
		if (!hwFuseSts.h264SupportFuse) dec->h264Support = H264_NOT_SUPPORTED;
		if (!hwFuseSts.mpeg4SupportFuse) dec->mpeg4Support = MPEG4_NOT_SUPPORTED;
		if (!hwFuseSts.customMpeg4SupportFuse) dec->customMpeg4Support = MPEG4_CUSTOM_NOT_SUPPORTED;
		if (!hwFuseSts.jpegSupportFuse) dec->jpegSupport = JPEG_NOT_SUPPORTED;
		if ((dec->jpegSupport == JPEG_PROGRESSIVE) && !hwFuseSts.jpegProgSupportFuse)
			dec->jpegSupport = JPEG_BASELINE;
		if (!hwFuseSts.mpeg2SupportFuse) dec->mpeg2Support = MPEG2_NOT_SUPPORTED;
		if (!hwFuseSts.vc1SupportFuse) dec->vc1Support = VC1_NOT_SUPPORTED;
		if (!hwFuseSts.vp6SupportFuse) dec->vp6Support = VP6_NOT_SUPPORTED;
		if (!hwFuseSts.vp7SupportFuse) dec->vp7Support = VP7_NOT_SUPPORTED;
		if (!hwFuseSts.vp8SupportFuse) dec->vp8Support = VP8_NOT_SUPPORTED;
		if (!hwFuseSts.ppSupportFuse) dec->ppSupport = PP_NOT_SUPPORTED;

		/* check the pp config vs fuse status */
		if ((dec->ppConfig & 0xFC000000) && ((hwFuseSts.ppConfigFuse & 0xF0000000) >> 5)) {
			u32 deInterlace = ((dec->ppConfig & PP_DEINTERLACING) >> 25);
			u32 alphaBlend  = ((dec->ppConfig & PP_ALPHA_BLENDING) >> 24);
			u32 deInterlaceFuse = (((hwFuseSts.ppConfigFuse >> 5) & PP_DEINTERLACING) >> 25);
			u32 alphaBlendFuse  = (((hwFuseSts.ppConfigFuse >> 5) & PP_ALPHA_BLENDING) >> 24);

			if (deInterlace && !deInterlaceFuse) dec->ppConfig &= 0xFD000000;
			if (alphaBlend && !alphaBlendFuse) dec->ppConfig &= 0xFE000000;
		}
		if (!hwFuseSts.sorensonSparkSupportFuse) dec->sorensonSparkSupport = SORENSON_SPARK_NOT_SUPPORTED;
		if (!hwFuseSts.refBufSupportFuse)   dec->refBufSupport = REF_BUF_NOT_SUPPORTED;
		if (!hwFuseSts.rvSupportFuse)       dec->rvSupport = RV_NOT_SUPPORTED;
		if (!hwFuseSts.avsSupportFuse)      dec->avsSupport = AVS_NOT_SUPPORTED;
		if (!hwFuseSts.mvcSupportFuse)      dec->mvcSupport = MVC_NOT_SUPPORTED;
	}
	}
	configReg = enc_dev.hwregs[63];
	enc->maxEncodedWidth = configReg & ((1 << 11) - 1);
	enc->h264Enabled = (configReg >> 27) & 1;
	enc->mpeg4Enabled = (configReg >> 26) & 1;
	enc->jpegEnabled = (configReg >> 25) & 1;
	enc->vsEnabled = (configReg >> 24) & 1;
	enc->rgbEnabled = (configReg >> 28) & 1;
	enc->busType = (configReg >> 20) & 15;
	enc->synthesisLanguage = (configReg >> 16) & 15;
	enc->busWidth = (configReg >> 12) & 15;
}

static irqreturn_t vdpu_isr(int irq, void *dev_id)
{
	vpu_device *dev = (vpu_device *) dev_id;
	u32 irq_status_dec = readl(dev->hwregs + DEC_INTERRUPT_REGISTER);
	u32 irq_status_pp  = readl(dev->hwregs + PP_INTERRUPT_REGISTER);

	pr_debug("vdpu_isr dec %x pp %x\n", irq_status_dec, irq_status_pp);

	if (irq_status_dec & DEC_INTERRUPT_BIT) {
		irq_status_dec = readl(dev->hwregs + DEC_INTERRUPT_REGISTER);
		if ((irq_status_dec & 0x40001) == 0x40001)
		{
			do {
				irq_status_dec = readl(dev->hwregs + DEC_INTERRUPT_REGISTER);
			} while ((irq_status_dec & 0x40001) == 0x40001);
		}
		/* clear dec IRQ */
		writel(irq_status_dec & (~DEC_INTERRUPT_BIT), dev->hwregs + DEC_INTERRUPT_REGISTER);
		pr_debug("DEC IRQ received!\n");
		if (NULL == service.reg_codec) {
			pr_err("dec isr with no task waiting\n");
		} else {
			reg_from_run_to_done(service.reg_codec);
		}
	}

	if (irq_status_pp & PP_INTERRUPT_BIT) {
		/* clear pp IRQ */
		writel(irq_status_pp & (~DEC_INTERRUPT_BIT), dev->hwregs + PP_INTERRUPT_REGISTER);
		pr_debug("PP IRQ received!\n");
		if (NULL == service.reg_pproc) {
			pr_err("pp isr with no task waiting\n");
		} else {
			reg_from_run_to_done(service.reg_pproc);
		}
	}
	try_set_reg();
	return IRQ_HANDLED;
}

static irqreturn_t vepu_isr(int irq, void *dev_id)
{
	struct vpu_device *dev = (struct vpu_device *) dev_id;
	u32 irq_status = readl(dev->hwregs + ENC_INTERRUPT_REGISTER);

	pr_debug("enc_isr\n");

	if (likely(irq_status & ENC_INTERRUPT_BIT)) {
		/* clear enc IRQ */
		writel(irq_status & (~ENC_INTERRUPT_BIT), dev->hwregs + ENC_INTERRUPT_REGISTER);
		pr_debug("ENC IRQ received!\n");
		if (NULL == service.reg_codec) {
			pr_err("enc isr with no task waiting\n");
		} else {
			reg_from_run_to_done(service.reg_codec);
		}
	}
	try_set_reg();
	return IRQ_HANDLED;
}

static int __init vpu_service_proc_init(void);
static int __init vpu_service_init(void)
{
	int ret;

	pr_debug("baseaddr = 0x%08x vdpu irq = %d vepu irq = %d\n", RK29_VCODEC_PHYS, IRQ_VDPU, IRQ_VEPU);

	dec_dev.iobaseaddr 	= RK29_VCODEC_PHYS + DEC_PHY_OFFSET;
	dec_dev.iosize 		= DEC_IO_SIZE;
	enc_dev.iobaseaddr 	= RK29_VCODEC_PHYS;
	enc_dev.iosize 		= ENC_IO_SIZE;

	INIT_LIST_HEAD(&service.waiting);
	INIT_LIST_HEAD(&service.running);
	INIT_LIST_HEAD(&service.done);
	INIT_LIST_HEAD(&service.session);
	spin_lock_init(&service.lock);
	spin_lock_init(&service.lock_power);
	service.reg_codec	= NULL;
	service.reg_pproc	= NULL;
	atomic_set(&service.total_running, 0);
	service.enabled		= false;

	vpu_get_clk();
	vpu_service_power_on();

	ret = vpu_service_reserve_io();
	if (ret < 0) {
		pr_err("reserve io failed\n");
		goto err_reserve_io;
	}

	/* get the IRQ line */
	ret = request_irq(IRQ_VDPU, vdpu_isr, IRQF_SHARED, "vdpu", (void *)&dec_dev);
	if (ret) {
		pr_err("can't request vdpu irq %d\n", IRQ_VDPU);
		goto err_req_vdpu_irq;
	}

	ret = request_irq(IRQ_VEPU, vepu_isr, IRQF_SHARED, "vepu", (void *)&enc_dev);
	if (ret) {
		pr_err("can't request vepu irq %d\n", IRQ_VEPU);
		goto err_req_vepu_irq;
	}

	ret = misc_register(&vpu_service_misc_device);
	if (ret) {
		pr_err("misc_register failed\n");
		goto err_register;
	}

	platform_device_register(&vpu_service_device);
	platform_driver_probe(&vpu_service_driver, NULL);
	get_hw_info();
	del_timer(&service.timer);
	vpu_service_power_off();
	pr_info("init success\n");

	vpu_service_proc_init();
	return 0;

err_register:
	free_irq(IRQ_VEPU, (void *)&enc_dev);
err_req_vepu_irq:
	free_irq(IRQ_VDPU, (void *)&dec_dev);
err_req_vdpu_irq:
	pr_info("init failed\n");
err_reserve_io:
	del_timer(&service.timer);
	vpu_service_power_off();
	vpu_service_release_io();
	vpu_put_clk();
	pr_info("init failed\n");
	return ret;
}

static void __exit vpu_service_exit(void)
{
	del_timer(&service.timer);
	vpu_service_power_off();
	platform_device_unregister(&vpu_service_device);
	platform_driver_unregister(&vpu_service_driver);
	misc_deregister(&vpu_service_misc_device);
	free_irq(IRQ_VEPU, (void *)&enc_dev);
	free_irq(IRQ_VDPU, (void *)&dec_dev);
	vpu_put_clk();
}

module_init(vpu_service_init);
module_exit(vpu_service_exit);

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int proc_vpu_service_show(struct seq_file *s, void *v)
{
	unsigned int i, n;
	unsigned long flag;
	vpu_reg *reg, *reg_tmp;
	vpu_session *session, *session_tmp;

	vpu_service_power_on();
	seq_printf(s, "\nENC Registers:\n");
	n = enc_dev.iosize >> 2;
	for (i = 0; i < n; i++) {
		seq_printf(s, "\tswreg%d = %08X\n", i, readl(enc_dev.hwregs + i));
	}
	seq_printf(s, "\nDEC Registers:\n");
	n = dec_dev.iosize >> 2;
	for (i = 0; i < n; i++) {
		seq_printf(s, "\tswreg%d = %08X\n", i, readl(dec_dev.hwregs + i));
	}

	seq_printf(s, "\nvpu service status:\n");
	spin_lock_irqsave(&service.lock, flag);
	list_for_each_entry_safe(session, session_tmp, &service.session, list_session) {
		seq_printf(s, "session pid %d type %d:\n", session->pid, session->type);
		//seq_printf(s, "waiting reg set %d\n");
		list_for_each_entry_safe(reg, reg_tmp, &session->waiting, session_link) {
			seq_printf(s, "waiting register set\n");
		}
		list_for_each_entry_safe(reg, reg_tmp, &session->running, session_link) {
			seq_printf(s, "running register set\n");
		}
		list_for_each_entry_safe(reg, reg_tmp, &session->done, session_link) {
			seq_printf(s, "done    register set\n");
		}
	}
	spin_unlock_irqrestore(&service.lock, flag);

	return 0;
}

static int proc_vpu_service_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_vpu_service_show, NULL);
}

static const struct file_operations proc_vpu_service_fops = {
	.open		= proc_vpu_service_open,
	.read		= seq_read,
	.llseek 	= seq_lseek,
	.release	= single_release,
};

static int __init vpu_service_proc_init(void)
{
	proc_create("vpu_service", 0, NULL, &proc_vpu_service_fops);
	return 0;

}
#endif /* CONFIG_PROC_FS */

