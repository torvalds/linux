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
#include <linux/wakelock.h>

#include <asm/uaccess.h>

#include <mach/irqs.h>
#include <mach/pmu.h>
#include <mach/cru.h>

#include <plat/vpu_service.h>
#include <plat/cpu.h>

typedef enum {
	VPU_DEC_ID_9190		= 0x6731,
	VPU_ID_8270		= 0x8270,
	VPU_ID_4831		= 0x4831,
} VPU_HW_ID;

typedef enum {
	VPU_DEC_TYPE_9190	= 0,
	VPU_ENC_TYPE_8270	= 0x100,
	VPU_ENC_TYPE_4831	,
} VPU_HW_TYPE_E;

typedef enum VPU_FREQ {
	VPU_FREQ_200M,
	VPU_FREQ_266M,
	VPU_FREQ_300M,
	VPU_FREQ_400M,
	VPU_FREQ_DEFAULT,
} VPU_FREQ;

typedef struct {
	VPU_HW_ID		hw_id;
	unsigned long		hw_addr;
	unsigned long		enc_offset;
	unsigned long		enc_reg_num;
	unsigned long		enc_io_size;
	unsigned long		dec_offset;
	unsigned long		dec_reg_num;
	unsigned long		dec_io_size;
} VPU_HW_INFO_E;

#define MHZ					(1000*1000)

#define VCODEC_PHYS				(0x10104000)

#define REG_NUM_9190_DEC			(60)
#define REG_NUM_9190_PP				(41)
#define REG_NUM_9190_DEC_PP			(REG_NUM_9190_DEC+REG_NUM_9190_PP)

#define REG_NUM_DEC_PP				(REG_NUM_9190_DEC+REG_NUM_9190_PP)

#define REG_NUM_ENC_8270			(96)
#define REG_SIZE_ENC_8270			(0x200)
#define REG_NUM_ENC_4831			(164)
#define REG_SIZE_ENC_4831			(0x400)

#define SIZE_REG(reg)				((reg)*4)

VPU_HW_INFO_E vpu_hw_set[] = {
	[0] = {
		.hw_id		= VPU_ID_8270,
		.hw_addr	= VCODEC_PHYS,
		.enc_offset	= 0x0,
		.enc_reg_num	= REG_NUM_ENC_8270,
		.enc_io_size	= REG_NUM_ENC_8270 * 4,
		.dec_offset	= REG_SIZE_ENC_8270,
		.dec_reg_num	= REG_NUM_9190_DEC_PP,
		.dec_io_size    = REG_NUM_9190_DEC_PP * 4,
	},
	[1] = {
		.hw_id		= VPU_ID_4831,
		.hw_addr	= VCODEC_PHYS,
		.enc_offset	= 0x0,
		.enc_reg_num	= REG_NUM_ENC_4831,
		.enc_io_size	= REG_NUM_ENC_4831 * 4,
		.dec_offset	= REG_SIZE_ENC_4831,
		.dec_reg_num	= REG_NUM_9190_DEC_PP,
		.dec_io_size    = REG_NUM_9190_DEC_PP * 4,
	},
};


#define DEC_INTERRUPT_REGISTER	   		1
#define PP_INTERRUPT_REGISTER	   		60
#define ENC_INTERRUPT_REGISTER	   		1

#define DEC_INTERRUPT_BIT			 0x100
#define PP_INTERRUPT_BIT			 0x100
#define ENC_INTERRUPT_BIT			 0x1

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
typedef struct vpu_reg {
	VPU_CLIENT_TYPE		type;
	VPU_FREQ		freq;
	vpu_session 		*session;
	struct list_head	session_link;		/* link to vpu service session */
	struct list_head	status_link;		/* link to register set list */
	unsigned long		size;
	unsigned long		*reg;
} vpu_reg;

typedef struct vpu_device {
	atomic_t		irq_count_codec;
	atomic_t		irq_count_pp;
	unsigned long		iobaseaddr;
	unsigned int		iosize;
	volatile u32		*hwregs;
} vpu_device;

typedef struct vpu_service_info {
	struct wake_lock	wake_lock;
	struct delayed_work	power_off_work;
	struct mutex		lock;
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
	VPU_HW_INFO_E		*hw_info;
	unsigned long		reg_size;
	bool			auto_freq;
} vpu_service_info;

typedef struct vpu_request
{
	unsigned long   *req;
	unsigned long   size;
} vpu_request;

static struct clk *pd_video;
static struct clk *aclk_vepu;
static struct clk *hclk_vepu;
static struct clk *aclk_ddr_vepu;
static struct clk *hclk_cpu_vcodec;
static vpu_service_info service;
static vpu_device 	dec_dev;
static vpu_device 	enc_dev;

#define VPU_POWER_OFF_DELAY		4*HZ /* 4s */
#define VPU_TIMEOUT_DELAY		2*HZ /* 2s */

static void vpu_get_clk(void)
{
	pd_video	= clk_get(NULL, "pd_video");
	aclk_vepu 	= clk_get(NULL, "aclk_vepu");
	hclk_vepu 	= clk_get(NULL, "hclk_vepu");
	aclk_ddr_vepu 	= clk_get(NULL, "aclk_ddr_vepu");
	hclk_cpu_vcodec	= clk_get(NULL, "hclk_cpu_vcodec");
}

static void vpu_put_clk(void)
{
	clk_put(pd_video);
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
	if (!service.enabled) {
		return;
	}

	service.enabled = false;
	total_running = atomic_read(&service.total_running);
	if (total_running) {
		pr_alert("alert: power off when %d task running!!\n", total_running);
		mdelay(50);
		pr_alert("alert: delay 50 ms for running task\n");
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
	wake_unlock(&service.wake_lock);
	printk("done\n");
}

static inline void vpu_queue_power_off_work(void)
{
	queue_delayed_work(system_nrt_wq, &service.power_off_work, VPU_POWER_OFF_DELAY);
}

static void vpu_power_off_work(struct work_struct *work)
{
	if (mutex_trylock(&service.lock)) {
		vpu_service_power_off();
		mutex_unlock(&service.lock);
	} else {
		/* Come back later if the device is busy... */
		vpu_queue_power_off_work();
	}
}

static void vpu_service_power_on(void)
{
	static ktime_t last;
	ktime_t now = ktime_get();
	if (ktime_to_ns(ktime_sub(now, last)) > NSEC_PER_SEC) {
		cancel_delayed_work_sync(&service.power_off_work);
		vpu_queue_power_off_work();
		last = now;
	}
	if (service.enabled)
		return ;

	service.enabled = true;
	printk("vpu: power on\n");

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
	wake_lock(&service.wake_lock);
}

static inline bool reg_check_rmvb_wmv(vpu_reg *reg)
{
	unsigned long type = (reg->reg[3] & 0xF0000000) >> 28;
	return ((type == 8) || (type == 4));
}

static inline bool reg_check_interlace(vpu_reg *reg)
{
	unsigned long type = (reg->reg[3] & (1 << 23));
	return (type > 0);
}

static vpu_reg *reg_init(vpu_session *session, void __user *src, unsigned long size)
{
	vpu_reg *reg = kmalloc(sizeof(vpu_reg)+service.reg_size, GFP_KERNEL);
	if (NULL == reg) {
		pr_err("error: kmalloc fail in reg_init\n");
		return NULL;
	}

	reg->session = session;
	reg->type = session->type;
	reg->size = size;
	reg->freq = VPU_FREQ_DEFAULT;
	reg->reg = (unsigned long *)&reg[1];
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

	if (copy_from_user(&reg->reg[0], (void __user *)src, size)) {
		pr_err("error: copy_from_user failed in reg_init\n");
		kfree(reg);
		return NULL;
	}

	mutex_lock(&service.lock);
	list_add_tail(&reg->status_link, &service.waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	mutex_unlock(&service.lock);

	if (service.auto_freq) {
		if (reg->type == VPU_DEC || reg->type == VPU_DEC_PP) {
			if (reg_check_rmvb_wmv(reg)) {
				reg->freq = VPU_FREQ_266M;
			} else {
				if (reg_check_interlace(reg)) {
					reg->freq = VPU_FREQ_400M;
				}
			}
		}
		if (reg->type == VPU_PP) {
			reg->freq = VPU_FREQ_400M;
		}
	}

	return reg;
}

static void reg_deinit(vpu_reg *reg)
{
	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	if (reg == service.reg_codec) service.reg_codec = NULL;
	if (reg == service.reg_pproc) service.reg_pproc = NULL;
	kfree(reg);
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
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &service.done);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->done);

	switch (reg->type) {
	case VPU_ENC : {
		service.reg_codec = NULL;
		reg_copy_from_hw(reg, enc_dev.hwregs, service.hw_info->enc_reg_num);
		break;
	}
	case VPU_DEC : {
		service.reg_codec = NULL;
		reg_copy_from_hw(reg, dec_dev.hwregs, REG_NUM_9190_DEC);
		break;
	}
	case VPU_PP : {
		service.reg_pproc = NULL;
		reg_copy_from_hw(reg, dec_dev.hwregs + PP_INTERRUPT_REGISTER, REG_NUM_9190_PP);
		dec_dev.hwregs[PP_INTERRUPT_REGISTER] = 0;
		break;
	}
	case VPU_DEC_PP : {
		service.reg_codec = NULL;
		service.reg_pproc = NULL;
		reg_copy_from_hw(reg, dec_dev.hwregs, REG_NUM_9190_DEC_PP);
		dec_dev.hwregs[PP_INTERRUPT_REGISTER] = 0;
		break;
	}
	default : {
		pr_err("error: copy reg from hw with unknown type %d\n", reg->type);
		break;
	}
	}
	atomic_sub(1, &reg->session->task_running);
	atomic_sub(1, &service.total_running);
	wake_up_interruptible_sync(&reg->session->wait);
}

static void vpu_service_set_freq(vpu_reg *reg)
{
	switch (reg->freq) {
	case VPU_FREQ_200M : {
		clk_set_rate(aclk_vepu, 200*MHZ);
	} break;
	case VPU_FREQ_266M : {
		clk_set_rate(aclk_vepu, 266*MHZ);
	} break;
	case VPU_FREQ_300M : {
		clk_set_rate(aclk_vepu, 300*MHZ);
	} break;
	case VPU_FREQ_400M : {
		clk_set_rate(aclk_vepu, 400*MHZ);
	} break;
	default : {
		clk_set_rate(aclk_vepu, 300*MHZ);
	} break;
	}
}

static void reg_copy_to_hw(vpu_reg *reg)
{
	int i;
	u32 *src = (u32 *)&reg->reg[0];
	atomic_add(1, &service.total_running);
	atomic_add(1, &reg->session->task_running);
	if (service.auto_freq) {
		vpu_service_set_freq(reg);
	}
	switch (reg->type) {
	case VPU_ENC : {
		int enc_count = service.hw_info->enc_reg_num;
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

		for (i = VPU_REG_EN_ENC + 1; i < enc_count; i++)
			dst[i] = src[i];

		dsb();

		dst[VPU_REG_ENC_GATE] = src[VPU_REG_ENC_GATE] | VPU_REG_ENC_GATE_BIT;
		dst[VPU_REG_EN_ENC]   = src[VPU_REG_EN_ENC];
	} break;
	case VPU_DEC : {
		u32 *dst = (u32 *)dec_dev.hwregs;
		service.reg_codec = reg;

		for (i = REG_NUM_9190_DEC - 1; i > VPU_REG_DEC_GATE; i--)
			dst[i] = src[i];

		dsb();

		dst[VPU_REG_DEC_GATE] = src[VPU_REG_DEC_GATE] | VPU_REG_DEC_GATE_BIT;
		dst[VPU_REG_EN_DEC]   = src[VPU_REG_EN_DEC];
		printk("dec\n");
	} break;
	case VPU_PP : {
		u32 *dst = (u32 *)dec_dev.hwregs + PP_INTERRUPT_REGISTER;
		service.reg_pproc = reg;

		dst[VPU_REG_PP_GATE] = src[VPU_REG_PP_GATE] | VPU_REG_PP_GATE_BIT;

		for (i = VPU_REG_PP_GATE + 1; i < REG_NUM_9190_PP; i++)
			dst[i] = src[i];

		dsb();

		dst[VPU_REG_EN_PP] = src[VPU_REG_EN_PP];
		printk("pp\n");
	} break;
	case VPU_DEC_PP : {
		u32 *dst = (u32 *)dec_dev.hwregs;
		service.reg_codec = reg;
		service.reg_pproc = reg;

		for (i = VPU_REG_EN_DEC_PP + 1; i < REG_NUM_9190_DEC_PP; i++)
			dst[i] = src[i];

		dst[VPU_REG_EN_DEC_PP]   = src[VPU_REG_EN_DEC_PP] | 0x2;
		dsb();

		dst[VPU_REG_DEC_PP_GATE] = src[VPU_REG_DEC_PP_GATE] | VPU_REG_PP_GATE_BIT;
		dst[VPU_REG_DEC_GATE]	 = src[VPU_REG_DEC_GATE]    | VPU_REG_DEC_GATE_BIT;
		dst[VPU_REG_EN_DEC]	 = src[VPU_REG_EN_DEC];
		printk("dec_pp\n");
	} break;
	default : {
		pr_err("error: unsupport session type %d", reg->type);
		atomic_sub(1, &service.total_running);
		atomic_sub(1, &reg->session->task_running);
		break;
	}
	}
}

static void try_set_reg(void)
{
	// first get reg from reg list
	if (!list_empty(&service.waiting)) {
		int can_set = 0;
		vpu_reg *reg = list_entry(service.waiting.next, vpu_reg, status_link);

		vpu_service_power_on();

		switch (reg->type) {
		case VPU_ENC : {
			if ((NULL == service.reg_codec) &&  (NULL == service.reg_pproc))
				can_set = 1;
		} break;
		case VPU_DEC : {
			if (NULL == service.reg_codec)
				can_set = 1;
			if (service.auto_freq && (NULL != service.reg_pproc)) {
				can_set = 0;
			}
		} break;
		case VPU_PP : {
			if (NULL == service.reg_codec) {
				if (NULL == service.reg_pproc)
					can_set = 1;
			} else {
				if ((VPU_DEC == service.reg_codec->type) && (NULL == service.reg_pproc))
					can_set = 1;
				// can not charge frequency when vpu is working
				if (service.auto_freq) {
					can_set = 0;
				}
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
}

static int return_reg(vpu_reg *reg, u32 __user *dst)
{
	int ret = 0;
	switch (reg->type) {
	case VPU_ENC : {
		if (copy_to_user(dst, &reg->reg[0], service.hw_info->enc_io_size))
			ret = -EFAULT;
		break;
	}
	case VPU_DEC : {
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(REG_NUM_9190_DEC)))
			ret = -EFAULT;
		break;
	}
	case VPU_PP : {
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(REG_NUM_9190_PP)))
			ret = -EFAULT;
		break;
	}
	case VPU_DEC_PP : {
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(REG_NUM_9190_DEC_PP)))
			ret = -EFAULT;
		break;
	}
	default : {
		ret = -EFAULT;
		pr_err("error: copy reg to user with unknown type %d\n", reg->type);
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
			pr_err("error: VPU_IOC_GET_HW_FUSE_STATUS copy_from_user failed\n");
			return -EFAULT;
		} else {
			if (VPU_ENC != session->type) {
				if (copy_to_user((void __user *)req.req, &service.dec_config, sizeof(VPUHwDecConfig_t))) {
					pr_err("error: VPU_IOC_GET_HW_FUSE_STATUS copy_to_user failed type %d\n", session->type);
					return -EFAULT;
				}
			} else {
				if (copy_to_user((void __user *)req.req, &service.enc_config, sizeof(VPUHwEncConfig_t))) {
					pr_err("error: VPU_IOC_GET_HW_FUSE_STATUS copy_to_user failed type %d\n", session->type);
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
			pr_err("error: VPU_IOC_SET_REG copy_from_user failed\n");
			return -EFAULT;
		}

		reg = reg_init(session, (void __user *)req.req, req.size);
		if (NULL == reg) {
			return -EFAULT;
		} else {
			mutex_lock(&service.lock);
			try_set_reg();
			mutex_unlock(&service.lock);
		}

		break;
	}
	case VPU_IOC_GET_REG : {
		vpu_request req;
		vpu_reg *reg;
		if (copy_from_user(&req, (void __user *)arg, sizeof(vpu_request))) {
			pr_err("error: VPU_IOC_GET_REG copy_from_user failed\n");
			return -EFAULT;
		} else {
			int ret = wait_event_interruptible_timeout(session->wait, !list_empty(&session->done), VPU_TIMEOUT_DELAY);
			if (!list_empty(&session->done)) {
				if (ret < 0) {
					pr_err("warning: pid %d wait task sucess but wait_evernt ret %d\n", session->pid, ret);
				}
				ret = 0;
			} else {
				if (unlikely(ret < 0)) {
					pr_err("error: pid %d wait task ret %d\n", session->pid, ret);
				} else if (0 == ret) {
					pr_err("error: pid %d wait %d task done timeout\n", session->pid, atomic_read(&session->task_running));
					ret = -ETIMEDOUT;
				}
			}
			if (ret < 0) {
				int task_running = atomic_read(&session->task_running);
				mutex_lock(&service.lock);
				vpu_service_dump();
				if (task_running) {
					atomic_set(&session->task_running, 0);
					atomic_sub(task_running, &service.total_running);
					printk("%d task is running but not return, reset hardware...", task_running);
					vpu_reset();
					printk("done\n");
				}
				vpu_service_session_clear(session);
				mutex_unlock(&service.lock);
				return ret;
			}
		}
		mutex_lock(&service.lock);
		reg = list_entry(session->done.next, vpu_reg, session_link);
		return_reg(reg, (u32 __user *)req.req);
		mutex_unlock(&service.lock);
		break;
	}
	default : {
		pr_err("error: unknow vpu service ioctl cmd %x\n", cmd);
		break;
	}
	}

	return 0;
}

static int vpu_service_check_hw(vpu_service_info *p, unsigned long hw_addr)
{
	int ret = -EINVAL, i = 0;
	volatile u32 *tmp = (volatile u32 *)ioremap_nocache(hw_addr, 0x4);
	u32 enc_id = *tmp;
	enc_id = (enc_id >> 16) & 0xFFFF;
	pr_info("checking hw id %x\n", enc_id);
	p->hw_info = NULL;
	for (i = 0; i < ARRAY_SIZE(vpu_hw_set); i++) {
		if (enc_id == vpu_hw_set[i].hw_id) {
			p->hw_info = &vpu_hw_set[i];
			ret = 0;
			break;
		}
	}
	iounmap((void *)tmp);
	return ret;
}

static void vpu_service_release_io(void)
{
	if (dec_dev.hwregs) {
		iounmap((void *)dec_dev.hwregs);
		dec_dev.hwregs = NULL;
	}
	if (dec_dev.iobaseaddr) {
		release_mem_region(dec_dev.iobaseaddr, dec_dev.iosize);
		dec_dev.iobaseaddr = 0;
		dec_dev.iosize = 0;
	}

	if (enc_dev.hwregs) {
		iounmap((void *)enc_dev.hwregs);
		enc_dev.hwregs = NULL;
	}
	if (enc_dev.iobaseaddr) {
		release_mem_region(enc_dev.iobaseaddr, enc_dev.iosize);
		enc_dev.iobaseaddr = 0;
		enc_dev.iosize = 0;
	}
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

	iobaseaddr 	= enc_dev.iobaseaddr;
	iosize		= enc_dev.iosize;

	if (!request_mem_region(iobaseaddr, iosize, "vepu_io")) {
		pr_info("failed to reserve enc HW regs\n");
		goto err;
	}

	enc_dev.hwregs = (volatile u32 *)ioremap_nocache(iobaseaddr, iosize);

	if (enc_dev.hwregs == NULL) {
		pr_info("failed to ioremap enc HW regs\n");
		goto err;
	}

	return 0;

err:
	return -EBUSY;
}

static int vpu_service_open(struct inode *inode, struct file *filp)
{
	vpu_session *session = (vpu_session *)kmalloc(sizeof(vpu_session), GFP_KERNEL);
	if (NULL == session) {
		pr_err("error: unable to allocate memory for vpu_session.");
		return -ENOMEM;
	}

	session->type	= VPU_TYPE_BUTT;
	session->pid	= current->pid;
	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->done);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	atomic_set(&session->task_running, 0);
	mutex_lock(&service.lock);
	list_add_tail(&session->list_session, &service.session);
	filp->private_data = (void *)session;
	mutex_unlock(&service.lock);

	pr_debug("dev opened\n");
	return nonseekable_open(inode, filp);
}

static int vpu_service_release(struct inode *inode, struct file *filp)
{
	int task_running;
	vpu_session *session = (vpu_session *)filp->private_data;
	if (NULL == session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);
	if (task_running) {
		pr_err("error: vpu_service session %d still has %d task running when closing\n", session->pid, task_running);
		msleep(50);
	}
	wake_up_interruptible_sync(&session->wait);

	mutex_lock(&service.lock);
	/* remove this filp from the asynchronusly notified filp's */
	list_del_init(&session->list_session);
	vpu_service_session_clear(session);
	kfree(session);
	filp->private_data = NULL;
	mutex_unlock(&service.lock);

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

static struct platform_device vpu_service_device = {
	.name		   = "vpu_service",
	.id 		   = -1,
};

static struct platform_driver vpu_service_driver = {
	.driver    = {
		.name  = "vpu_service",
		.owner = THIS_MODULE,
	},
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
	//enc->busType = (configReg >> 20) & 15;
	//enc->synthesisLanguage = (configReg >> 16) & 15;
	//enc->busWidth = (configReg >> 12) & 15;
	enc->reg_size = service.reg_size;
	enc->reserv[0] = enc->reserv[1] = 0;

	service.auto_freq = soc_is_rk2928g() || soc_is_rk2928l() || soc_is_rk2926();
	if (service.auto_freq) {
		printk("vpu_service set to auto frequency mode\n");
	}
}

static irqreturn_t vdpu_irq(int irq, void *dev_id)
{
	vpu_device *dev = (vpu_device *) dev_id;
	u32 irq_status = readl(dev->hwregs + DEC_INTERRUPT_REGISTER);

	pr_debug("vdpu_irq\n");

	if (irq_status & DEC_INTERRUPT_BIT) {
		pr_debug("vdpu_isr dec %x\n", irq_status);
		if ((irq_status & 0x40001) == 0x40001)
		{
			do {
				irq_status = readl(dev->hwregs + DEC_INTERRUPT_REGISTER);
			} while ((irq_status & 0x40001) == 0x40001);
		}
		/* clear dec IRQ */
		writel(irq_status & (~DEC_INTERRUPT_BIT), dev->hwregs + DEC_INTERRUPT_REGISTER);
		atomic_add(1, &dev->irq_count_codec);
	}

	irq_status  = readl(dev->hwregs + PP_INTERRUPT_REGISTER);
	if (irq_status & PP_INTERRUPT_BIT) {
		pr_debug("vdpu_isr pp  %x\n", irq_status);
		/* clear pp IRQ */
		writel(irq_status & (~DEC_INTERRUPT_BIT), dev->hwregs + PP_INTERRUPT_REGISTER);
		atomic_add(1, &dev->irq_count_pp);
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t vdpu_isr(int irq, void *dev_id)
{
	vpu_device *dev = (vpu_device *) dev_id;

	mutex_lock(&service.lock);
	if (atomic_read(&dev->irq_count_codec)) {
		atomic_sub(1, &dev->irq_count_codec);
		if (NULL == service.reg_codec) {
			pr_err("error: dec isr with no task waiting\n");
		} else {
			reg_from_run_to_done(service.reg_codec);
		}
	}

	if (atomic_read(&dev->irq_count_pp)) {
		atomic_sub(1, &dev->irq_count_pp);
		if (NULL == service.reg_pproc) {
			pr_err("error: pp isr with no task waiting\n");
		} else {
			reg_from_run_to_done(service.reg_pproc);
		}
	}
	try_set_reg();
	mutex_unlock(&service.lock);
	return IRQ_HANDLED;
}

static irqreturn_t vepu_irq(int irq, void *dev_id)
{
	struct vpu_device *dev = (struct vpu_device *) dev_id;
	u32 irq_status = readl(dev->hwregs + ENC_INTERRUPT_REGISTER);

	pr_debug("vepu_irq irq status %x\n", irq_status);

	if (likely(irq_status & ENC_INTERRUPT_BIT)) {
		/* clear enc IRQ */
		writel(irq_status & (~ENC_INTERRUPT_BIT), dev->hwregs + ENC_INTERRUPT_REGISTER);
		atomic_add(1, &dev->irq_count_codec);
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t vepu_isr(int irq, void *dev_id)
{
	struct vpu_device *dev = (struct vpu_device *) dev_id;

	mutex_lock(&service.lock);
	if (atomic_read(&dev->irq_count_codec)) {
		atomic_sub(1, &dev->irq_count_codec);
		if (NULL == service.reg_codec) {
			pr_err("error: enc isr with no task waiting\n");
		} else {
			reg_from_run_to_done(service.reg_codec);
		}
	}
	try_set_reg();
	mutex_unlock(&service.lock);
	return IRQ_HANDLED;
}

static int __init vpu_service_proc_init(void);
static int __init vpu_service_init(void)
{
	int ret;

	pr_debug("baseaddr = 0x%08x vdpu irq = %d vepu irq = %d\n", VCODEC_PHYS, IRQ_VDPU, IRQ_VEPU);

	wake_lock_init(&service.wake_lock, WAKE_LOCK_SUSPEND, "vpu");
	INIT_LIST_HEAD(&service.waiting);
	INIT_LIST_HEAD(&service.running);
	INIT_LIST_HEAD(&service.done);
	INIT_LIST_HEAD(&service.session);
	mutex_init(&service.lock);
	service.reg_codec	= NULL;
	service.reg_pproc	= NULL;
	atomic_set(&service.total_running, 0);
	service.enabled		= false;

	vpu_get_clk();

	INIT_DELAYED_WORK(&service.power_off_work, vpu_power_off_work);

	vpu_service_power_on();
	ret = vpu_service_check_hw(&service, VCODEC_PHYS);
	if (ret < 0) {
		pr_err("error: hw info check faild\n");
		goto err_hw_id_check;
	}

	atomic_set(&dec_dev.irq_count_codec, 0);
	atomic_set(&dec_dev.irq_count_pp, 0);
	dec_dev.iobaseaddr 	= service.hw_info->hw_addr + service.hw_info->dec_offset;
	dec_dev.iosize		= service.hw_info->dec_io_size;
	atomic_set(&enc_dev.irq_count_codec, 0);
	atomic_set(&enc_dev.irq_count_pp, 0);
	enc_dev.iobaseaddr 	= service.hw_info->hw_addr + service.hw_info->enc_offset;
	enc_dev.iosize		= service.hw_info->enc_io_size;;
	service.reg_size	= max(dec_dev.iosize, enc_dev.iosize);

	ret = vpu_service_reserve_io();
	if (ret < 0) {
		pr_err("error: reserve io failed\n");
		goto err_reserve_io;
	}

	/* get the IRQ line */
	ret = request_threaded_irq(IRQ_VDPU, vdpu_irq, vdpu_isr, IRQF_SHARED, "vdpu", (void *)&dec_dev);
	if (ret) {
		pr_err("error: can't request vdpu irq %d\n", IRQ_VDPU);
		goto err_req_vdpu_irq;
	}

	ret = request_threaded_irq(IRQ_VEPU, vepu_irq, vepu_isr, IRQF_SHARED, "vepu", (void *)&enc_dev);
	if (ret) {
		pr_err("error: can't request vepu irq %d\n", IRQ_VEPU);
		goto err_req_vepu_irq;
	}

	ret = misc_register(&vpu_service_misc_device);
	if (ret) {
		pr_err("error: misc_register failed\n");
		goto err_register;
	}

	platform_device_register(&vpu_service_device);
	platform_driver_probe(&vpu_service_driver, NULL);
	get_hw_info();
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
	vpu_service_release_io();
err_hw_id_check:
	vpu_service_power_off();
	vpu_put_clk();
	wake_lock_destroy(&service.wake_lock);
	pr_info("init failed\n");
	return ret;
}

static void __exit vpu_service_proc_release(void);
static void __exit vpu_service_exit(void)
{
	vpu_service_proc_release();
	vpu_service_power_off();
	platform_device_unregister(&vpu_service_device);
	platform_driver_unregister(&vpu_service_driver);
	misc_deregister(&vpu_service_misc_device);
	free_irq(IRQ_VEPU, (void *)&enc_dev);
	free_irq(IRQ_VDPU, (void *)&dec_dev);
	vpu_service_release_io();
	vpu_put_clk();
	wake_lock_destroy(&service.wake_lock);
}

module_init(vpu_service_init);
module_exit(vpu_service_exit);

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int proc_vpu_service_show(struct seq_file *s, void *v)
{
	unsigned int i, n;
	vpu_reg *reg, *reg_tmp;
	vpu_session *session, *session_tmp;

	mutex_lock(&service.lock);
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
	mutex_unlock(&service.lock);

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
static void __exit vpu_service_proc_release(void)
{
	remove_proc_entry("vpu_service", NULL);
}
#endif /* CONFIG_PROC_FS */

