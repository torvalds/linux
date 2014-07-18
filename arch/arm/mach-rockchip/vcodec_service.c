
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
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>

#include <asm/cacheflush.h>
#include <linux/uaccess.h>
#include <linux/rockchip/grf.h>

#if defined(CONFIG_ION_ROCKCHIP)
#include <linux/rockchip_ion.h>
#endif

#if defined(CONFIG_ROCKCHIP_IOMMU) & defined(CONFIG_ION_ROCKCHIP)
#define CONFIG_VCODEC_MMU
#endif

#ifdef CONFIG_VCODEC_MMU
#include <linux/rockchip/iovmm.h>
#include <linux/rockchip/sysmmu.h>
#include <linux/dma-buf.h>
#endif

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#if defined(CONFIG_ARCH_RK319X)
#include <mach/grf.h>
#endif

#include "vcodec_service.h"

#define HEVC_TEST_ENABLE	0
#define HEVC_SIM_ENABLE		0
#define VCODEC_CLOCK_ENABLE	1

typedef enum {
	VPU_DEC_ID_9190		= 0x6731,
	VPU_ID_8270		= 0x8270,
	VPU_ID_4831		= 0x4831,
	HEVC_ID			= 0x6867,
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
	VPU_FREQ_500M,
	VPU_FREQ_600M,
	VPU_FREQ_DEFAULT,
	VPU_FREQ_BUT,
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

#define VPU_SERVICE_SHOW_TIME			0

#if VPU_SERVICE_SHOW_TIME
static struct timeval enc_start, enc_end;
static struct timeval dec_start, dec_end;
static struct timeval pp_start,  pp_end;
#endif

#define MHZ					(1000*1000)

#define REG_NUM_9190_DEC			(60)
#define REG_NUM_9190_PP				(41)
#define REG_NUM_9190_DEC_PP			(REG_NUM_9190_DEC+REG_NUM_9190_PP)

#define REG_NUM_DEC_PP				(REG_NUM_9190_DEC+REG_NUM_9190_PP)

#define REG_NUM_ENC_8270			(96)
#define REG_SIZE_ENC_8270			(0x200)
#define REG_NUM_ENC_4831			(164)
#define REG_SIZE_ENC_4831			(0x400)

#define REG_NUM_HEVC_DEC			(68)

#define SIZE_REG(reg)				((reg)*4)

static VPU_HW_INFO_E vpu_hw_set[] = {
	[0] = {
		.hw_id		= VPU_ID_8270,
		.hw_addr	= 0,
		.enc_offset	= 0x0,
		.enc_reg_num	= REG_NUM_ENC_8270,
		.enc_io_size	= REG_NUM_ENC_8270 * 4,
		.dec_offset	= REG_SIZE_ENC_8270,
		.dec_reg_num	= REG_NUM_9190_DEC_PP,
		.dec_io_size	= REG_NUM_9190_DEC_PP * 4,
	},
	[1] = {
		.hw_id		= VPU_ID_4831,
		.hw_addr	= 0,
		.enc_offset	= 0x0,
		.enc_reg_num	= REG_NUM_ENC_4831,
		.enc_io_size	= REG_NUM_ENC_4831 * 4,
		.dec_offset	= REG_SIZE_ENC_4831,
		.dec_reg_num	= REG_NUM_9190_DEC_PP,
		.dec_io_size	= REG_NUM_9190_DEC_PP * 4,
	},
	[2] = {
		.hw_id		= HEVC_ID,
		.hw_addr	= 0,
		.dec_offset	= 0x0,
		.dec_reg_num	= REG_NUM_HEVC_DEC,
		.dec_io_size	= REG_NUM_HEVC_DEC * 4,
	},
	[3] = {
		.hw_id		= VPU_DEC_ID_9190,
		.hw_addr	= 0,
		.enc_offset	= 0x0,
		.enc_reg_num	= 0,
		.enc_io_size	= 0,
		.dec_offset	= REG_SIZE_ENC_4831,
		.dec_reg_num	= REG_NUM_9190_DEC_PP,
		.dec_io_size	= REG_NUM_9190_DEC_PP * 4,
	},
	
};


#define DEC_INTERRUPT_REGISTER			1
#define PP_INTERRUPT_REGISTER			60
#define ENC_INTERRUPT_REGISTER			1

#define DEC_INTERRUPT_BIT			0x100
#define DEC_BUFFER_EMPTY_BIT			0x4000
#define PP_INTERRUPT_BIT			0x100
#define ENC_INTERRUPT_BIT			0x1

#define HEVC_DEC_INT_RAW_BIT			0x200
#define HEVC_DEC_STR_ERROR_BIT			0x4000
#define HEVC_DEC_BUS_ERROR_BIT			0x2000
#define HEVC_DEC_BUFFER_EMPTY_BIT		0x10000

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

#if defined(CONFIG_VCODEC_MMU)
static u8 addr_tbl_vpu_h264dec[] = {
	12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
	25, 26, 27, 28, 29, 40, 41
};

static u8 addr_tbl_vpu_vp8dec[] = {
	10,12,13, 14, 18, 19, 27, 40
};

static u8 addr_tbl_vpu_vp6dec[] = {
	12, 13, 14, 18, 27, 40
};

static u8 addr_tbl_vpu_vc1dec[] = {
	12, 13, 14, 15, 16, 17, 27, 41
};

static u8 addr_tbl_vpu_jpegdec[] = {
	12, 40, 66, 67
};

static u8 addr_tbl_vpu_defaultdec[] = {
	12, 13, 14, 15, 16, 17, 40, 41
};

static u8 addr_tbl_vpu_enc[] = {
	5, 6, 7, 8, 9, 10, 11, 12, 13, 51
};

static u8 addr_tbl_hevc_dec[] = {
	4, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 42, 43
};
#endif

enum VPU_DEC_FMT {
	VPU_DEC_FMT_H264,
	VPU_DEC_FMT_MPEG4,
	VPU_DEC_FMT_H263,
	VPU_DEC_FMT_JPEG,
	VPU_DEC_FMT_VC1,
	VPU_DEC_FMT_MPEG2,
	VPU_DEC_FMT_MPEG1,
	VPU_DEC_FMT_VP6,
	VPU_DEC_FMT_RV,
	VPU_DEC_FMT_VP7,
	VPU_DEC_FMT_VP8,
	VPU_DEC_FMT_AVS,
	VPU_DEC_FMT_SVC,
	VPU_DEC_FMT_VC2,
	VPU_DEC_FMT_MVC,
	VPU_DEC_FMT_THEORA,
	VPU_DEC_FMT_RES
};

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
	VPU_FREQ		    freq;
	vpu_session 		*session;
	struct list_head	session_link;		/* link to vpu service session */
	struct list_head	status_link;		/* link to register set list */
	unsigned long		size;
#if defined(CONFIG_VCODEC_MMU)
	struct list_head	mem_region_list;
#endif
	unsigned long		*reg;
} vpu_reg;

typedef struct vpu_device {
	atomic_t		irq_count_codec;
	atomic_t		irq_count_pp;
	unsigned long		iobaseaddr;
	unsigned int		iosize;
	volatile u32		*hwregs;
} vpu_device;

enum vcodec_device_id {
	VCODEC_DEVICE_ID_VPU,
	VCODEC_DEVICE_ID_HEVC
};

struct vcodec_mem_region {
	struct list_head srv_lnk;
	struct list_head reg_lnk;
	struct list_head session_lnk;
	unsigned long iova;	/* virtual address for iommu */
	unsigned long len;
	struct ion_handle *hdl;
};

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
	bool			bug_dec_addr;
	atomic_t		freq_status;

	struct clk		*aclk_vcodec;
	struct clk		*hclk_vcodec;
	struct clk		*clk_core;
	struct clk		*clk_cabac;
	struct clk		*pd_video;

	int			irq_dec;
	int			irq_enc;

	vpu_device		enc_dev;
	vpu_device		dec_dev;

	struct device		*dev;

	struct cdev		cdev;
	dev_t			dev_t;
	struct class		*cls;
	struct device		*child_dev;

	struct dentry		*debugfs_dir;
	struct dentry		*debugfs_file_regs;

	u32 irq_status;
#if defined(CONFIG_VCODEC_MMU)	
	struct ion_client	*ion_client;
	struct list_head	mem_region_list;
	struct device		*mmu_dev;
#endif

	enum vcodec_device_id	dev_id;

	struct delayed_work	simulate_work;
} vpu_service_info;

typedef struct vpu_request
{
	unsigned long *req;
	unsigned long size;
} vpu_request;

/// global variable
//static struct clk *pd_video;
static struct dentry *parent; // debugfs root directory for all device (vpu, hevc).

#ifdef CONFIG_DEBUG_FS
static int vcodec_debugfs_init(void);
static void vcodec_debugfs_exit(void);
static struct dentry* vcodec_debugfs_create_device_dir(char *dirname, struct dentry *parent);
static int debug_vcodec_open(struct inode *inode, struct file *file);

static const struct file_operations debug_vcodec_fops = {
	.open = debug_vcodec_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

#define VPU_POWER_OFF_DELAY		4*HZ /* 4s */
#define VPU_TIMEOUT_DELAY		2*HZ /* 2s */
#define VPU_SIMULATE_DELAY		msecs_to_jiffies(15)

static void vcodec_select_mode(enum vcodec_device_id id)
{
	if (soc_is_rk3036()) {
#define BIT_VCODEC_SEL		(1<<3)
		if (id == VCODEC_DEVICE_ID_HEVC) {
			writel_relaxed(readl_relaxed(RK_GRF_VIRT + RK3036_GRF_SOC_CON1) | (BIT_VCODEC_SEL) | (BIT_VCODEC_SEL << 16), RK_GRF_VIRT + RK3036_GRF_SOC_CON1);
		} else {
			writel_relaxed((readl_relaxed(RK_GRF_VIRT + RK3036_GRF_SOC_CON1) & (~BIT_VCODEC_SEL)) | (BIT_VCODEC_SEL << 16), RK_GRF_VIRT + RK3036_GRF_SOC_CON1);
		}
	}
}

static int vpu_get_clk(struct vpu_service_info *pservice)
{
#if VCODEC_CLOCK_ENABLE
	do {
		pservice->aclk_vcodec   = devm_clk_get(pservice->dev, "aclk_vcodec");
		if (IS_ERR(pservice->aclk_vcodec)) {
			dev_err(pservice->dev, "failed on clk_get aclk_vcodec\n");
			break;
		}

		pservice->hclk_vcodec   = devm_clk_get(pservice->dev, "hclk_vcodec");
		if (IS_ERR(pservice->hclk_vcodec)) {
			dev_err(pservice->dev, "failed on clk_get hclk_vcodec\n");
			break;
		}

		if (pservice->dev_id == VCODEC_DEVICE_ID_HEVC) {
			pservice->clk_core = devm_clk_get(pservice->dev, "clk_core");
			if (IS_ERR(pservice->clk_core)) {
				dev_err(pservice->dev, "failed on clk_get clk_core\n");
				break;
			}

			if (!soc_is_rk3036()) {
				pservice->clk_cabac = devm_clk_get(pservice->dev, "clk_cabac");
				if (IS_ERR(pservice->clk_cabac)) {
					dev_err(pservice->dev, "failed on clk_get clk_cabac\n");
					break;
				}
			} else {
				pservice->clk_cabac = NULL;
			}

			if (!soc_is_rk3036()) {
				pservice->pd_video = devm_clk_get(pservice->dev, "pd_hevc");
				if (IS_ERR(pservice->pd_video)) {
					dev_err(pservice->dev, "failed on clk_get pd_hevc\n");
					break;
				}
			} else {
				pservice->pd_video = NULL;
			}
		} else {
			if (!soc_is_rk3036()) {
				pservice->pd_video = devm_clk_get(pservice->dev, "pd_video");
				if (IS_ERR(pservice->pd_video)) {
					dev_err(pservice->dev, "failed on clk_get pd_video\n");
					break;
				}
			} else {
				pservice->pd_video = NULL;
			}
		}

		return 0;
	} while (0);

	return -1;
#else
	return 0;
#endif
}

static void vpu_put_clk(struct vpu_service_info *pservice)
{
#if VCODEC_CLOCK_ENABLE
	if (pservice->pd_video) {
		devm_clk_put(pservice->dev, pservice->pd_video);
	}

	if (pservice->aclk_vcodec) {
		devm_clk_put(pservice->dev, pservice->aclk_vcodec);
	}

	if (pservice->hclk_vcodec) {
		devm_clk_put(pservice->dev, pservice->hclk_vcodec);
	}

	if (pservice->dev_id == VCODEC_DEVICE_ID_HEVC) {
		if (pservice->clk_core) {
			devm_clk_put(pservice->dev, pservice->clk_core);
		}

		if (pservice->clk_cabac) {
			devm_clk_put(pservice->dev, pservice->clk_cabac);
		}
	}
#endif
}

static void vpu_reset(struct vpu_service_info *pservice)
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
	pservice->reg_codec = NULL;
	pservice->reg_pproc = NULL;
	pservice->reg_resev = NULL;
}

static void reg_deinit(struct vpu_service_info *pservice, vpu_reg *reg);
static void vpu_service_session_clear(struct vpu_service_info *pservice, vpu_session *session)
{
	vpu_reg *reg, *n;
	list_for_each_entry_safe(reg, n, &session->waiting, session_link) {
		reg_deinit(pservice, reg);
	}
	list_for_each_entry_safe(reg, n, &session->running, session_link) {
		reg_deinit(pservice, reg);
	}
	list_for_each_entry_safe(reg, n, &session->done, session_link) {
		reg_deinit(pservice, reg);
	}
}

static void vpu_service_dump(struct vpu_service_info *pservice)
{
	int running;
	vpu_reg *reg, *reg_tmp;
	vpu_session *session, *session_tmp;

	running = atomic_read(&pservice->total_running);
	printk("total_running %d\n", running);

	printk("reg_codec 0x%.8x\n", (unsigned int)pservice->reg_codec);
	printk("reg_pproc 0x%.8x\n", (unsigned int)pservice->reg_pproc);
	printk("reg_resev 0x%.8x\n", (unsigned int)pservice->reg_resev);

	list_for_each_entry_safe(session, session_tmp, &pservice->session, list_session) {
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

static void vpu_service_power_off(struct vpu_service_info *pservice)
{
	int total_running;
	if (!pservice->enabled)
		return;

	pservice->enabled = false;
	total_running = atomic_read(&pservice->total_running);
	if (total_running) {
		pr_alert("alert: power off when %d task running!!\n", total_running);
		mdelay(50);
		pr_alert("alert: delay 50 ms for running task\n");
		vpu_service_dump(pservice);
	}

#if defined(CONFIG_VCODEC_MMU)
	if (pservice->mmu_dev)
		iovmm_deactivate(pservice->dev);
#endif

	pr_info("%s: power off...", dev_name(pservice->dev));
	udelay(10);
#if VCODEC_CLOCK_ENABLE
	if (pservice->pd_video)
		clk_disable_unprepare(pservice->pd_video);
	if (pservice->hclk_vcodec)
		clk_disable_unprepare(pservice->hclk_vcodec);
	if (pservice->aclk_vcodec)
		clk_disable_unprepare(pservice->aclk_vcodec);
	if (pservice->dev_id == VCODEC_DEVICE_ID_HEVC) {
		if (pservice->clk_core)
			clk_disable_unprepare(pservice->clk_core);
		if (pservice->clk_cabac)
			clk_disable_unprepare(pservice->clk_cabac);
	}
#endif
	wake_unlock(&pservice->wake_lock);
	pr_info("done\n");
}

static inline void vpu_queue_power_off_work(struct vpu_service_info *pservice)
{
	queue_delayed_work(system_nrt_wq, &pservice->power_off_work, VPU_POWER_OFF_DELAY);
}

static void vpu_power_off_work(struct work_struct *work_s)
{
	struct delayed_work *dlwork = container_of(work_s, struct delayed_work, work);
	struct vpu_service_info *pservice = container_of(dlwork, struct vpu_service_info, power_off_work);

	if (mutex_trylock(&pservice->lock)) {
		vpu_service_power_off(pservice);
		mutex_unlock(&pservice->lock);
	} else {
		/* Come back later if the device is busy... */
		vpu_queue_power_off_work(pservice);
	}
}

static void vpu_service_power_on(struct vpu_service_info *pservice)
{
	static ktime_t last;
	ktime_t now = ktime_get();
	if (ktime_to_ns(ktime_sub(now, last)) > NSEC_PER_SEC) {
		cancel_delayed_work_sync(&pservice->power_off_work);
		vpu_queue_power_off_work(pservice);
		last = now;
	}
	if (pservice->enabled)
		return ;

	pservice->enabled = true;
	printk("%s: power on\n", dev_name(pservice->dev));

#if VCODEC_CLOCK_ENABLE
	if (pservice->aclk_vcodec)
		clk_prepare_enable(pservice->aclk_vcodec);

	if (pservice->hclk_vcodec)
		clk_prepare_enable(pservice->hclk_vcodec);

	if (pservice->dev_id == VCODEC_DEVICE_ID_HEVC) {
		if (pservice->clk_core)
			clk_prepare_enable(pservice->clk_core);
	if (pservice->clk_cabac)
		clk_prepare_enable(pservice->clk_cabac);
	}

	if (pservice->pd_video)
		clk_prepare_enable(pservice->pd_video);
#endif

#if defined(CONFIG_ARCH_RK319X)
	/// select aclk_vepu as vcodec clock source. 
#define BIT_VCODEC_SEL	(1<<7)
	writel_relaxed(readl_relaxed(RK319X_GRF_BASE + GRF_SOC_CON1) |
		(BIT_VCODEC_SEL) | (BIT_VCODEC_SEL << 16),
		RK319X_GRF_BASE + GRF_SOC_CON1);
#endif

	udelay(10);
	wake_lock(&pservice->wake_lock);

#if defined(CONFIG_VCODEC_MMU)
	if (pservice->mmu_dev)
		iovmm_activate(pservice->dev);
#endif
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

static inline enum VPU_DEC_FMT reg_check_fmt(vpu_reg *reg)
{
	enum VPU_DEC_FMT type = (enum VPU_DEC_FMT)((reg->reg[3] & 0xF0000000) >> 28);
	return type;
}

static inline int reg_probe_width(vpu_reg *reg)
{
	int width_in_mb = reg->reg[4] >> 23;
	return width_in_mb * 16;
}

#if defined(CONFIG_VCODEC_MMU)
static int vcodec_bufid_to_iova(struct vpu_service_info *pservice, u8 *tbl, int size, vpu_reg *reg)
{
	int i;
	int usr_fd = 0;
	int offset = 0;

	if (tbl == NULL || size <= 0) {
		dev_err(pservice->dev, "input arguments invalidate\n");
		return -1;
	}

	vpu_service_power_on(pservice);

	for (i = 0; i < size; i++) {
		usr_fd = reg->reg[tbl[i]] & 0x3FF;

		if (tbl[i] == 41 && pservice->hw_info->hw_id != HEVC_ID &&
		    (reg->type == VPU_DEC || reg->type == VPU_DEC_PP))
			/* special for vpu dec num 41 regitster */
			offset = reg->reg[tbl[i]] >> 10 << 4;
		else
			offset = reg->reg[tbl[i]] >> 10;

		if (usr_fd != 0) {
			struct ion_handle *hdl;
			int ret;
			struct vcodec_mem_region *mem_region;

			hdl = ion_import_dma_buf(pservice->ion_client, usr_fd);
			if (IS_ERR(hdl)) {
				dev_err(pservice->dev, "import dma-buf from fd %d failed, reg[%d]\n", usr_fd, tbl[i]);
				return PTR_ERR(hdl);
			}

			mem_region = kzalloc(sizeof(struct vcodec_mem_region), GFP_KERNEL);

			if (mem_region == NULL) {
				dev_err(pservice->dev, "allocate memory for iommu memory region failed\n");
				ion_free(pservice->ion_client, hdl);
				return -1;
			}

			mem_region->hdl = hdl;

			ret = ion_map_iommu(pservice->dev, pservice->ion_client, mem_region->hdl, &mem_region->iova, &mem_region->len);
			if (ret < 0) {
				dev_err(pservice->dev, "ion map iommu failed\n");
				kfree(mem_region);
				ion_free(pservice->ion_client, hdl);
				return ret;
			}
			reg->reg[tbl[i]] = mem_region->iova + offset;
			INIT_LIST_HEAD(&mem_region->reg_lnk);
			list_add_tail(&mem_region->reg_lnk, &reg->mem_region_list);
		}
	}
	return 0;
}

static int vcodec_reg_address_translate(struct vpu_service_info *pservice, vpu_reg *reg)
{
	VPU_HW_ID hw_id;
	u8 *tbl;
	int size = 0;

	hw_id = pservice->hw_info->hw_id;

	if (hw_id == HEVC_ID) {
		tbl = addr_tbl_hevc_dec;
		size = sizeof(addr_tbl_hevc_dec);
	} else {
		if (reg->type == VPU_DEC || reg->type == VPU_DEC_PP) {
			switch (reg_check_fmt(reg)) {
			case VPU_DEC_FMT_H264:
				{
					tbl = addr_tbl_vpu_h264dec;
					size = sizeof(addr_tbl_vpu_h264dec);
					break;
				}
			case VPU_DEC_FMT_VP8:
			case VPU_DEC_FMT_VP7:
				{
					tbl = addr_tbl_vpu_vp8dec;
					size = sizeof(addr_tbl_vpu_vp8dec);
					break;
				}

			case VPU_DEC_FMT_VP6:
				{
					tbl = addr_tbl_vpu_vp6dec;
					size = sizeof(addr_tbl_vpu_vp6dec);
					break;
				}
			case VPU_DEC_FMT_VC1:
				{
					tbl = addr_tbl_vpu_vc1dec;
					size = sizeof(addr_tbl_vpu_vc1dec);
					break;
				}

			case VPU_DEC_FMT_JPEG:
				{
					tbl = addr_tbl_vpu_jpegdec;
					size = sizeof(addr_tbl_vpu_jpegdec);
					break;
				}
			default:
				tbl = addr_tbl_vpu_defaultdec;
				size = sizeof(addr_tbl_vpu_defaultdec);
				break;
			}
		} else if (reg->type == VPU_ENC) {
			tbl = addr_tbl_vpu_enc;
			size = sizeof(addr_tbl_vpu_enc);
		}
	}

	if (size != 0) {
		return vcodec_bufid_to_iova(pservice, tbl, size, reg);
	} else {
		return -1;
	}
}
#endif

static vpu_reg *reg_init(struct vpu_service_info *pservice, vpu_session *session, void __user *src, unsigned long size)
{
	vpu_reg *reg = kmalloc(sizeof(vpu_reg)+pservice->reg_size, GFP_KERNEL);
	if (NULL == reg) {
		pr_err("error: kmalloc fail in reg_init\n");
		return NULL;
	}

	if (size > pservice->reg_size) {
		printk("warning: vpu reg size %lu is larger than hw reg size %lu\n", size, pservice->reg_size);
		size = pservice->reg_size;
	}
	reg->session = session;
	reg->type = session->type;
	reg->size = size;
	reg->freq = VPU_FREQ_DEFAULT;
	reg->reg = (unsigned long *)&reg[1];
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

#if defined(CONFIG_VCODEC_MMU)  
	if (pservice->mmu_dev)
		INIT_LIST_HEAD(&reg->mem_region_list);
#endif

	if (copy_from_user(&reg->reg[0], (void __user *)src, size)) {
		pr_err("error: copy_from_user failed in reg_init\n");
		kfree(reg);
		return NULL;
	}

#if defined(CONFIG_VCODEC_MMU)
	if (pservice->mmu_dev && 0 > vcodec_reg_address_translate(pservice, reg)) {
		pr_err("error: translate reg address failed\n");
		kfree(reg);
		return NULL;
	}
#endif

	mutex_lock(&pservice->lock);
	list_add_tail(&reg->status_link, &pservice->waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	mutex_unlock(&pservice->lock);

	if (pservice->auto_freq) {
		if (!soc_is_rk2928g()) {
			if (reg->type == VPU_DEC || reg->type == VPU_DEC_PP) {
				if (reg_check_rmvb_wmv(reg)) {
					reg->freq = VPU_FREQ_200M;
				} else if (reg_check_fmt(reg) == VPU_DEC_FMT_H264) {
					if (reg_probe_width(reg) > 3200) {
						// raise frequency for 4k avc.
						reg->freq = VPU_FREQ_500M;
					}
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
	}

	return reg;
}

static void reg_deinit(struct vpu_service_info *pservice, vpu_reg *reg)
{
#if defined(CONFIG_VCODEC_MMU)
	struct vcodec_mem_region *mem_region = NULL, *n;
#endif

	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	if (reg == pservice->reg_codec)
		pservice->reg_codec = NULL;
	if (reg == pservice->reg_pproc)
		pservice->reg_pproc = NULL;

#if defined(CONFIG_VCODEC_MMU)
	// release memory region attach to this registers table.
	if (pservice->mmu_dev) {
		list_for_each_entry_safe(mem_region, n, &reg->mem_region_list, reg_lnk) {
			ion_unmap_iommu(pservice->dev, pservice->ion_client, mem_region->hdl);
			ion_free(pservice->ion_client, mem_region->hdl);
			list_del_init(&mem_region->reg_lnk);
			kfree(mem_region);
		}
	}
#endif

	kfree(reg);
}

static void reg_from_wait_to_run(struct vpu_service_info *pservice, vpu_reg *reg)
{
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &pservice->running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
}

static void reg_copy_from_hw(struct vpu_service_info *pservice, vpu_reg *reg, volatile u32 *src, u32 count)
{
	int i;
	u32 *dst = (u32 *)&reg->reg[0];

	vcodec_select_mode(pservice->dev_id);
	for (i = 0; i < count; i++)
		*dst++ = *src++;
}

static void reg_from_run_to_done(struct vpu_service_info *pservice, vpu_reg *reg)
{
	int irq_reg = -1;
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &pservice->done);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->done);

	switch (reg->type) {
	case VPU_ENC : {
		pservice->reg_codec = NULL;
		reg_copy_from_hw(pservice, reg, pservice->enc_dev.hwregs, pservice->hw_info->enc_reg_num);
		irq_reg = ENC_INTERRUPT_REGISTER;
		break;
	}
	case VPU_DEC : {
		int reg_len = pservice->hw_info->hw_id == HEVC_ID ? REG_NUM_HEVC_DEC : REG_NUM_9190_DEC;
		pservice->reg_codec = NULL;
		reg_copy_from_hw(pservice, reg, pservice->dec_dev.hwregs, reg_len);
		irq_reg = DEC_INTERRUPT_REGISTER;
		break;
	}
	case VPU_PP : {
		pservice->reg_pproc = NULL;
		reg_copy_from_hw(pservice, reg, pservice->dec_dev.hwregs + PP_INTERRUPT_REGISTER, REG_NUM_9190_PP);
		pservice->dec_dev.hwregs[PP_INTERRUPT_REGISTER] = 0;
		break;
	}
	case VPU_DEC_PP : {
		pservice->reg_codec = NULL;
		pservice->reg_pproc = NULL;
		reg_copy_from_hw(pservice, reg, pservice->dec_dev.hwregs, REG_NUM_9190_DEC_PP);
		vcodec_select_mode(pservice->dev_id);
		pservice->dec_dev.hwregs[PP_INTERRUPT_REGISTER] = 0;
		break;
	}
	default : {
		pr_err("error: copy reg from hw with unknown type %d\n", reg->type);
		break;
	}
	}

	if (irq_reg != -1) {
		reg->reg[irq_reg] = pservice->irq_status;
	}

	atomic_sub(1, &reg->session->task_running);
	atomic_sub(1, &pservice->total_running);
	wake_up(&reg->session->wait);
}

static void vpu_service_set_freq(struct vpu_service_info *pservice, vpu_reg *reg)
{
	VPU_FREQ curr = atomic_read(&pservice->freq_status);
	if (curr == reg->freq) {
		return ;
	}
	atomic_set(&pservice->freq_status, reg->freq);
	switch (reg->freq) {
	case VPU_FREQ_200M : {
		clk_set_rate(pservice->aclk_vcodec, 200*MHZ);
		//printk("default: 200M\n");
	} break;
	case VPU_FREQ_266M : {
		clk_set_rate(pservice->aclk_vcodec, 266*MHZ);
		//printk("default: 266M\n");
	} break;
	case VPU_FREQ_300M : {
		clk_set_rate(pservice->aclk_vcodec, 300*MHZ);
		//printk("default: 300M\n");
	} break;
	case VPU_FREQ_400M : {
		clk_set_rate(pservice->aclk_vcodec, 400*MHZ);
		//printk("default: 400M\n");
	} break;
	case VPU_FREQ_500M : {
		clk_set_rate(pservice->aclk_vcodec, 500*MHZ);
	} break;
	case VPU_FREQ_600M : {
		clk_set_rate(pservice->aclk_vcodec, 600*MHZ);
	} break;
	default : {
		if (soc_is_rk2928g()) {
			clk_set_rate(pservice->aclk_vcodec, 400*MHZ);
		} else {
			clk_set_rate(pservice->aclk_vcodec, 300*MHZ);
		}
		//printk("default: 300M\n");
	} break;
	}
}

#if HEVC_SIM_ENABLE
static void simulate_start(struct vpu_service_info *pservice);
#endif
static void reg_copy_to_hw(struct vpu_service_info *pservice, vpu_reg *reg)
{
	int i;
	u32 *src = (u32 *)&reg->reg[0];
	atomic_add(1, &pservice->total_running);
	atomic_add(1, &reg->session->task_running);
	if (pservice->auto_freq) {
		vpu_service_set_freq(pservice, reg);
	}
	
	vcodec_select_mode(pservice->dev_id);
	
	switch (reg->type) {
	case VPU_ENC : {
		int enc_count = pservice->hw_info->enc_reg_num;
		u32 *dst = (u32 *)pservice->enc_dev.hwregs;

		pservice->reg_codec = reg;

		dst[VPU_REG_EN_ENC] = src[VPU_REG_EN_ENC] & 0x6;

		for (i = 0; i < VPU_REG_EN_ENC; i++)
			dst[i] = src[i];

		for (i = VPU_REG_EN_ENC + 1; i < enc_count; i++)
			dst[i] = src[i];

		dsb();

		dst[VPU_REG_ENC_GATE] = src[VPU_REG_ENC_GATE] | VPU_REG_ENC_GATE_BIT;
		dst[VPU_REG_EN_ENC]   = src[VPU_REG_EN_ENC];

#if VPU_SERVICE_SHOW_TIME
		do_gettimeofday(&enc_start);
#endif

	} break;
	case VPU_DEC : {
		u32 *dst = (u32 *)pservice->dec_dev.hwregs;

		pservice->reg_codec = reg;

		if (pservice->hw_info->hw_id != HEVC_ID) {
			for (i = REG_NUM_9190_DEC - 1; i > VPU_REG_DEC_GATE; i--)
				dst[i] = src[i];
		} else {
			for (i = REG_NUM_HEVC_DEC - 1; i > VPU_REG_EN_DEC; i--) {
				dst[i] = src[i];
			}
		}

		dsb();

		if (pservice->hw_info->hw_id != HEVC_ID) {
			dst[VPU_REG_DEC_GATE] = src[VPU_REG_DEC_GATE] | VPU_REG_DEC_GATE_BIT;
			dst[VPU_REG_EN_DEC]   = src[VPU_REG_EN_DEC];
		} else {
			dst[VPU_REG_EN_DEC] = src[VPU_REG_EN_DEC];
		}

		dsb();
		dmb();

#if VPU_SERVICE_SHOW_TIME
		do_gettimeofday(&dec_start);
#endif

	} break;
	case VPU_PP : {
		u32 *dst = (u32 *)pservice->dec_dev.hwregs + PP_INTERRUPT_REGISTER;
		pservice->reg_pproc = reg;

		dst[VPU_REG_PP_GATE] = src[VPU_REG_PP_GATE] | VPU_REG_PP_GATE_BIT;

		for (i = VPU_REG_PP_GATE + 1; i < REG_NUM_9190_PP; i++)
			dst[i] = src[i];

		dsb();

		dst[VPU_REG_EN_PP] = src[VPU_REG_EN_PP];

#if VPU_SERVICE_SHOW_TIME
		do_gettimeofday(&pp_start);
#endif

	} break;
	case VPU_DEC_PP : {
		u32 *dst = (u32 *)pservice->dec_dev.hwregs;
		pservice->reg_codec = reg;
		pservice->reg_pproc = reg;

		for (i = VPU_REG_EN_DEC_PP + 1; i < REG_NUM_9190_DEC_PP; i++)
			dst[i] = src[i];

		dst[VPU_REG_EN_DEC_PP]   = src[VPU_REG_EN_DEC_PP] | 0x2;
		dsb();

		dst[VPU_REG_DEC_PP_GATE] = src[VPU_REG_DEC_PP_GATE] | VPU_REG_PP_GATE_BIT;
		dst[VPU_REG_DEC_GATE]	 = src[VPU_REG_DEC_GATE]    | VPU_REG_DEC_GATE_BIT;
		dst[VPU_REG_EN_DEC]	 = src[VPU_REG_EN_DEC];

#if VPU_SERVICE_SHOW_TIME
		do_gettimeofday(&dec_start);
#endif

	} break;
	default : {
		pr_err("error: unsupport session type %d", reg->type);
		atomic_sub(1, &pservice->total_running);
		atomic_sub(1, &reg->session->task_running);
		break;
	}
	}

#if HEVC_SIM_ENABLE
	if (pservice->hw_info->hw_id == HEVC_ID) {
		simulate_start(pservice);
	}
#endif
}

static void try_set_reg(struct vpu_service_info *pservice)
{
	// first get reg from reg list
	if (!list_empty(&pservice->waiting)) {
		int can_set = 0;
		vpu_reg *reg = list_entry(pservice->waiting.next, vpu_reg, status_link);

		vpu_service_power_on(pservice);

		switch (reg->type) {
		case VPU_ENC : {
			if ((NULL == pservice->reg_codec) &&  (NULL == pservice->reg_pproc))
				can_set = 1;
		} break;
		case VPU_DEC : {
			if (NULL == pservice->reg_codec)
				can_set = 1;
			if (pservice->auto_freq && (NULL != pservice->reg_pproc)) {
				can_set = 0;
			}
		} break;
		case VPU_PP : {
			if (NULL == pservice->reg_codec) {
				if (NULL == pservice->reg_pproc)
					can_set = 1;
			} else {
				if ((VPU_DEC == pservice->reg_codec->type) && (NULL == pservice->reg_pproc))
					can_set = 1;
				// can not charge frequency when vpu is working
				if (pservice->auto_freq) {
					can_set = 0;
				}
			}
		} break;
		case VPU_DEC_PP : {
			if ((NULL == pservice->reg_codec) && (NULL == pservice->reg_pproc))
				can_set = 1;
			} break;
		default : {
			printk("undefined reg type %d\n", reg->type);
		} break;
		}
		if (can_set) {
			reg_from_wait_to_run(pservice, reg);
			reg_copy_to_hw(pservice, reg);
		}
	}
}

static int return_reg(struct vpu_service_info *pservice, vpu_reg *reg, u32 __user *dst)
{
	int ret = 0;
	switch (reg->type) {
	case VPU_ENC : {
		if (copy_to_user(dst, &reg->reg[0], pservice->hw_info->enc_io_size))
			ret = -EFAULT;
		break;
	}
	case VPU_DEC : {
		int reg_len = pservice->hw_info->hw_id == HEVC_ID ? REG_NUM_HEVC_DEC : REG_NUM_9190_DEC;
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(reg_len)))
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
	reg_deinit(pservice, reg);
	return ret;
}

static long vpu_service_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct vpu_service_info *pservice = container_of(filp->f_dentry->d_inode->i_cdev, struct vpu_service_info, cdev);
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
				if (copy_to_user((void __user *)req.req, &pservice->dec_config, sizeof(VPUHwDecConfig_t))) {
					pr_err("error: VPU_IOC_GET_HW_FUSE_STATUS copy_to_user failed type %d\n", session->type);
					return -EFAULT;
				}
			} else {
				if (copy_to_user((void __user *)req.req, &pservice->enc_config, sizeof(VPUHwEncConfig_t))) {
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
		reg = reg_init(pservice, session, (void __user *)req.req, req.size);
		if (NULL == reg) {
			return -EFAULT;
		} else {
			mutex_lock(&pservice->lock);
			try_set_reg(pservice);
			mutex_unlock(&pservice->lock);
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
			int ret = wait_event_timeout(session->wait, !list_empty(&session->done), VPU_TIMEOUT_DELAY);
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
				mutex_lock(&pservice->lock);
				vpu_service_dump(pservice);
				if (task_running) {
					atomic_set(&session->task_running, 0);
					atomic_sub(task_running, &pservice->total_running);
					printk("%d task is running but not return, reset hardware...", task_running);
					vpu_reset(pservice);
					printk("done\n");
				}
				vpu_service_session_clear(pservice, session);
				mutex_unlock(&pservice->lock);
				return ret;
			}
		}
		mutex_lock(&pservice->lock);
		reg = list_entry(session->done.next, vpu_reg, session_link);
		return_reg(pservice, reg, (u32 __user *)req.req);
		mutex_unlock(&pservice->lock);
		break;
	}
	case VPU_IOC_PROBE_IOMMU_STATUS: {
		int iommu_enable = 0;

#if defined(CONFIG_VCODEC_MMU)
		iommu_enable = pservice->mmu_dev ? 1 : 0; 
#endif

		if (copy_to_user((void __user *)arg, &iommu_enable, sizeof(int))) {
			pr_err("error: VPU_IOC_PROBE_IOMMU_STATUS copy_to_user failed\n");
			return -EFAULT;
		}
		break;
	}
	default : {
		pr_err("error: unknow vpu service ioctl cmd %x\n", cmd);
		break;
	}
	}

	return 0;
}
#if 1
static int vpu_service_check_hw(vpu_service_info *p, unsigned long hw_addr)
{
	int ret = -EINVAL, i = 0;
	volatile u32 *tmp = (volatile u32 *)ioremap_nocache(hw_addr, 0x4);
	u32 enc_id = *tmp;

#if HEVC_SIM_ENABLE
	/// temporary, hevc driver test.
	if (strncmp(dev_name(p->dev), "hevc_service", strlen("hevc_service")) == 0) {
		p->hw_info = &vpu_hw_set[2];
		return 0;
	}
#endif
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
#endif

static int vpu_service_open(struct inode *inode, struct file *filp)
{
	struct vpu_service_info *pservice = container_of(inode->i_cdev, struct vpu_service_info, cdev);
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
	mutex_lock(&pservice->lock);
	list_add_tail(&session->list_session, &pservice->session);
	filp->private_data = (void *)session;
	mutex_unlock(&pservice->lock);

	pr_debug("dev opened\n");
	return nonseekable_open(inode, filp);
}

static int vpu_service_release(struct inode *inode, struct file *filp)
{
	struct vpu_service_info *pservice = container_of(inode->i_cdev, struct vpu_service_info, cdev);
	int task_running;
	vpu_session *session = (vpu_session *)filp->private_data;
	if (NULL == session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);
	if (task_running) {
		pr_err("error: vpu_service session %d still has %d task running when closing\n", session->pid, task_running);
		msleep(50);
	}
	wake_up(&session->wait);

	mutex_lock(&pservice->lock);
	/* remove this filp from the asynchronusly notified filp's */
	list_del_init(&session->list_session);
	vpu_service_session_clear(pservice, session);
	kfree(session);
	filp->private_data = NULL;
	mutex_unlock(&pservice->lock);

	pr_debug("dev closed\n");
	return 0;
}

static const struct file_operations vpu_service_fops = {
	.unlocked_ioctl = vpu_service_ioctl,
	.open		= vpu_service_open,
	.release	= vpu_service_release,
	//.fasync 	= vpu_service_fasync,
};

static irqreturn_t vdpu_irq(int irq, void *dev_id);
static irqreturn_t vdpu_isr(int irq, void *dev_id);
static irqreturn_t vepu_irq(int irq, void *dev_id);
static irqreturn_t vepu_isr(int irq, void *dev_id);
static void get_hw_info(struct vpu_service_info *pservice);

#if HEVC_SIM_ENABLE
static void simulate_work(struct work_struct *work_s)
{
	struct delayed_work *dlwork = container_of(work_s, struct delayed_work, work);
	struct vpu_service_info *pservice = container_of(dlwork, struct vpu_service_info, simulate_work);
	vpu_device *dev = &pservice->dec_dev;

	if (!list_empty(&pservice->running)) {
		atomic_add(1, &dev->irq_count_codec);
		vdpu_isr(0, (void*)pservice);
	} else {
		//simulate_start(pservice);
		pr_err("empty running queue\n");
	}
}

static void simulate_init(struct vpu_service_info *pservice)
{
	INIT_DELAYED_WORK(&pservice->simulate_work, simulate_work);
}

static void simulate_start(struct vpu_service_info *pservice)
{
	cancel_delayed_work_sync(&pservice->power_off_work);
	queue_delayed_work(system_nrt_wq, &pservice->simulate_work, VPU_SIMULATE_DELAY);
}
#endif

#if HEVC_TEST_ENABLE
static int hevc_test_case0(vpu_service_info *pservice);
#endif
#if defined(CONFIG_ION_ROCKCHIP)
extern struct ion_client *rockchip_ion_client_create(const char * name);
#endif
static int vcodec_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res = NULL;
	struct device *dev = &pdev->dev;
	void __iomem *regs = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct vpu_service_info *pservice = devm_kzalloc(dev, sizeof(struct vpu_service_info), GFP_KERNEL);
	char *prop = (char*)dev_name(dev);
#if defined(CONFIG_VCODEC_MMU)
	char mmu_dev_dts_name[40];
#endif

	pr_info("probe device %s\n", dev_name(dev));

	of_property_read_string(np, "name", (const char**)&prop);
	dev_set_name(dev, prop);

	if (strcmp(dev_name(dev), "hevc_service") == 0) {
		pservice->dev_id = VCODEC_DEVICE_ID_HEVC;
		vcodec_select_mode(VCODEC_DEVICE_ID_HEVC);
	} else if (strcmp(dev_name(dev), "vpu_service") == 0) {
		pservice->dev_id = VCODEC_DEVICE_ID_VPU;
	} else {
		dev_err(dev, "Unknown device %s to probe\n", dev_name(dev));
		return -1;
	}

	wake_lock_init(&pservice->wake_lock, WAKE_LOCK_SUSPEND, "vpu");
	INIT_LIST_HEAD(&pservice->waiting);
	INIT_LIST_HEAD(&pservice->running);
	INIT_LIST_HEAD(&pservice->done);
	INIT_LIST_HEAD(&pservice->session);
	mutex_init(&pservice->lock);
	pservice->reg_codec	= NULL;
	pservice->reg_pproc	= NULL;
	atomic_set(&pservice->total_running, 0);
	pservice->enabled = false;
#if defined(CONFIG_VCODEC_MMU)    
	pservice->mmu_dev = NULL;
#endif
	pservice->dev = dev;

	if (0 > vpu_get_clk(pservice))
		goto err;

	INIT_DELAYED_WORK(&pservice->power_off_work, vpu_power_off_work);

	vpu_service_power_on(pservice);

	mdelay(1);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	res->flags &= ~IORESOURCE_CACHEABLE;

	regs = devm_ioremap_resource(pservice->dev, res);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto err;
	}

	{
		u32 offset = res->start;
		if (soc_is_rk3036()) {
			if (pservice->dev_id == VCODEC_DEVICE_ID_VPU)
				offset += 0x400;
			vcodec_select_mode(pservice->dev_id);
		}
		ret = vpu_service_check_hw(pservice, offset);
		if (ret < 0) {
			pr_err("error: hw info check faild\n");
			goto err;
		}
	}

	/// define regs address.
	pservice->dec_dev.iobaseaddr = res->start + pservice->hw_info->dec_offset;
	pservice->dec_dev.iosize     = pservice->hw_info->dec_io_size;

	printk("%s %d\n", __func__, __LINE__);

	pservice->dec_dev.hwregs = (volatile u32 *)((u8 *)regs + pservice->hw_info->dec_offset);

	pservice->reg_size   = pservice->dec_dev.iosize;

	printk("%s %d\n", __func__, __LINE__);

	if (pservice->hw_info->hw_id != HEVC_ID && !soc_is_rk3036()) {
		pservice->enc_dev.iobaseaddr = res->start + pservice->hw_info->enc_offset;
		pservice->enc_dev.iosize     = pservice->hw_info->enc_io_size;

		pservice->reg_size = pservice->reg_size > pservice->enc_dev.iosize ? pservice->reg_size : pservice->enc_dev.iosize;

		pservice->enc_dev.hwregs = (volatile u32 *)((u8 *)regs + pservice->hw_info->enc_offset);

		pservice->irq_enc = platform_get_irq_byname(pdev, "irq_enc");
		if (pservice->irq_enc < 0) {
			dev_err(pservice->dev, "cannot find IRQ encoder\n");
			ret = -ENXIO;
			goto err;
		}

		ret = devm_request_threaded_irq(pservice->dev, pservice->irq_enc, vepu_irq, vepu_isr, 0, dev_name(pservice->dev), (void *)pservice);
		if (ret) {
			dev_err(pservice->dev, "error: can't request vepu irq %d\n", pservice->irq_enc);
			goto err;
		}
	}

	pservice->irq_dec = platform_get_irq_byname(pdev, "irq_dec");
	if (pservice->irq_dec < 0) {
		dev_err(pservice->dev, "cannot find IRQ decoder\n");
		ret = -ENXIO;
		goto err;
	}

	/* get the IRQ line */
	ret = devm_request_threaded_irq(pservice->dev, pservice->irq_dec, vdpu_irq, vdpu_isr, 0, dev_name(pservice->dev), (void *)pservice);
	if (ret) {
		dev_err(pservice->dev, "error: can't request vdpu irq %d\n", pservice->irq_dec);
		goto err;
	}

	atomic_set(&pservice->dec_dev.irq_count_codec, 0);
	atomic_set(&pservice->dec_dev.irq_count_pp, 0);
	atomic_set(&pservice->enc_dev.irq_count_codec, 0);
	atomic_set(&pservice->enc_dev.irq_count_pp, 0);

	/// create device
	ret = alloc_chrdev_region(&pservice->dev_t, 0, 1, dev_name(dev));
	if (ret) {
		dev_err(dev, "alloc dev_t failed\n");
		goto err;
	}

	cdev_init(&pservice->cdev, &vpu_service_fops);

	pservice->cdev.owner = THIS_MODULE;
	pservice->cdev.ops = &vpu_service_fops;

	ret = cdev_add(&pservice->cdev, pservice->dev_t, 1);

	if (ret) {
		dev_err(dev, "add dev_t failed\n");
		goto err;
	}

	pservice->cls = class_create(THIS_MODULE, dev_name(dev));

	if (IS_ERR(pservice->cls)) {
		ret = PTR_ERR(pservice->cls);
		dev_err(dev, "class_create err:%d\n", ret);
		goto err;
	}

	pservice->child_dev = device_create(pservice->cls, dev, pservice->dev_t, NULL, dev_name(dev));

	platform_set_drvdata(pdev, pservice);

	get_hw_info(pservice);


#ifdef CONFIG_DEBUG_FS
	pservice->debugfs_dir = vcodec_debugfs_create_device_dir((char*)dev_name(dev), parent);
	if (pservice->debugfs_dir == NULL)
		pr_err("create debugfs dir %s failed\n", dev_name(dev));

	pservice->debugfs_file_regs = 
		debugfs_create_file("regs", 0664,
				    pservice->debugfs_dir, pservice,
				    &debug_vcodec_fops);
#endif

#if defined(CONFIG_VCODEC_MMU)
	pservice->ion_client = rockchip_ion_client_create("vpu");
	if (IS_ERR(pservice->ion_client)) {
		dev_err(&pdev->dev, "failed to create ion client for vcodec");
		return PTR_ERR(pservice->ion_client);
	} else {
		dev_info(&pdev->dev, "vcodec ion client create success!\n");
	}

	if (pservice->hw_info->hw_id == HEVC_ID)
		sprintf(mmu_dev_dts_name, "iommu,hevc_mmu");
	else
		sprintf(mmu_dev_dts_name, "iommu,vpu_mmu");
	pservice->mmu_dev = rockchip_get_sysmmu_device_by_compatible(mmu_dev_dts_name);

	if (pservice->mmu_dev) {
		platform_set_sysmmu(pservice->mmu_dev, pservice->dev);
		iovmm_activate(pservice->dev);
	}
#endif

	vpu_service_power_off(pservice);
	pr_info("init success\n");

#if HEVC_SIM_ENABLE
	if (pservice->hw_info->hw_id == HEVC_ID)
		simulate_init(pservice);
#endif

#if HEVC_TEST_ENABLE
	hevc_test_case0(pservice);
#endif

	return 0;

err:
	pr_info("init failed\n");
	vpu_service_power_off(pservice);
	vpu_put_clk(pservice);
	wake_lock_destroy(&pservice->wake_lock);

	if (res)
		devm_release_mem_region(&pdev->dev, res->start, resource_size(res));
	if (pservice->irq_enc > 0)
		free_irq(pservice->irq_enc, (void *)pservice);
	if (pservice->irq_dec > 0)
		free_irq(pservice->irq_dec, (void *)pservice);

	if (pservice->child_dev) {
		device_destroy(pservice->cls, pservice->dev_t);
		cdev_del(&pservice->cdev);
		unregister_chrdev_region(pservice->dev_t, 1);
	}

	if (pservice->cls)
		class_destroy(pservice->cls);

	return ret;
}

static int vcodec_remove(struct platform_device *pdev)
{
	struct vpu_service_info *pservice = platform_get_drvdata(pdev);
	struct resource *res;

	device_destroy(pservice->cls, pservice->dev_t);
	class_destroy(pservice->cls);
	cdev_del(&pservice->cdev);
	unregister_chrdev_region(pservice->dev_t, 1);

	free_irq(pservice->irq_enc, (void *)&pservice->enc_dev);
	free_irq(pservice->irq_dec, (void *)&pservice->dec_dev);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));
	vpu_put_clk(pservice);
	wake_lock_destroy(&pservice->wake_lock);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove(pservice->debugfs_file_regs);
	debugfs_remove(pservice->debugfs_dir);
#endif

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id vcodec_service_dt_ids[] = {
	{.compatible = "vpu_service",},
	{.compatible = "rockchip,hevc_service",},
	{},
};
#endif

static struct platform_driver vcodec_driver = {
	.probe = vcodec_probe,
	.remove = vcodec_remove,
	.driver = {
		.name = "vcodec",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(vcodec_service_dt_ids),
#endif
	},
};

static void get_hw_info(struct vpu_service_info *pservice)
{
	VPUHwDecConfig_t *dec = &pservice->dec_config;
	VPUHwEncConfig_t *enc = &pservice->enc_config;

	if (pservice->dev_id == VCODEC_DEVICE_ID_VPU) {
		u32 configReg   = pservice->dec_dev.hwregs[VPU_DEC_HWCFG0];
		u32 asicID      = pservice->dec_dev.hwregs[0];
	
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

		if (!soc_is_rk3190() && !soc_is_rk3288()) {
			dec->maxDecPicWidth = configReg & 0x07FFU;
		} else {
			dec->maxDecPicWidth = 4096;
		}
	
		/* 2nd Config register */
		configReg   = pservice->dec_dev.hwregs[VPU_DEC_HWCFG1];
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
		if (((asicID >> 16) >= 0x8190U) || ((asicID >> 16) == 0x6731U))
			dec->jpegESupport = (configReg >> DWL_JPEG_EXT_E) & 0x01U;
		else
			dec->jpegESupport = JPEG_EXT_NOT_SUPPORTED;

		if (((asicID >> 16) >= 0x9170U) || ((asicID >> 16) == 0x6731U) )
			dec->rvSupport = (configReg >> DWL_RV_E) & 0x03U;
		else
			dec->rvSupport = RV_NOT_SUPPORTED;
		dec->mvcSupport = (configReg >> DWL_MVC_E) & 0x03U;

		if (dec->refBufSupport && (asicID >> 16) == 0x6731U )
			dec->refBufSupport |= 8; /* enable HW support for offset */
	
		/// invalidate fuse register value in rk319x vpu and following.
		if (!soc_is_rk3190() && !soc_is_rk3288() && !soc_is_rk3036()) {
			VPUHwFuseStatus_t hwFuseSts;
			/* Decoder fuse configuration */
			u32 fuseReg = pservice->dec_dev.hwregs[VPU_DEC_HW_FUSE_CFG];

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
			configReg = pservice->dec_dev.hwregs[VPU_PP_HW_SYNTH_CFG];

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
				configReg = pservice->dec_dev.hwregs[VPU_DEC_HW_FUSE_CFG];
				if ((configReg >> DWL_PP_E) & 0x01U) {
					/* Pp fuse configuration */
					u32 fuseRegPp = pservice->dec_dev.hwregs[VPU_PP_HW_FUSE_CFG];

					if ((fuseRegPp >> DWL_PP_FUSE_E) & 0x01U) {
						hwFuseSts.ppSupportFuse = 1;
						/* check max. pp output width */
						if (fuseRegPp & 0x8000U)
							hwFuseSts.maxPpOutPicWidthFuse = 1920;
						else if (fuseRegPp & 0x4000U)
							hwFuseSts.maxPpOutPicWidthFuse = 1280;
						else if (fuseRegPp & 0x2000U)
							hwFuseSts.maxPpOutPicWidthFuse = 720;
						else if (fuseRegPp & 0x1000U)
							hwFuseSts.maxPpOutPicWidthFuse = 352;
						else
							hwFuseSts.maxPpOutPicWidthFuse = 352;
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

		if (!soc_is_rk3036()) {
			configReg = pservice->enc_dev.hwregs[63];
			enc->maxEncodedWidth = configReg & ((1 << 11) - 1);
			enc->h264Enabled = (configReg >> 27) & 1;
			enc->mpeg4Enabled = (configReg >> 26) & 1;
			enc->jpegEnabled = (configReg >> 25) & 1;
			enc->vsEnabled = (configReg >> 24) & 1;
			enc->rgbEnabled = (configReg >> 28) & 1;
			/*enc->busType = (configReg >> 20) & 15;
			enc->synthesisLanguage = (configReg >> 16) & 15;
			enc->busWidth = (configReg >> 12) & 15;*/
			enc->reg_size = pservice->reg_size;
			enc->reserv[0] = enc->reserv[1] = 0;
		}

		pservice->auto_freq = soc_is_rk2928g() || soc_is_rk2928l() || soc_is_rk2926() || soc_is_rk3288();
		if (pservice->auto_freq) {
			pr_info("vpu_service set to auto frequency mode\n");
			atomic_set(&pservice->freq_status, VPU_FREQ_BUT);
		}

		pservice->bug_dec_addr = cpu_is_rk30xx();
	} else {
		/* disable frequency switch in hevc.*/
		pservice->auto_freq = false;
	}
}

static irqreturn_t vdpu_irq(int irq, void *dev_id)
{
	struct vpu_service_info *pservice = (struct vpu_service_info*)dev_id;
	vpu_device *dev = &pservice->dec_dev;
	u32 raw_status;
	u32 irq_status;

	vcodec_select_mode(pservice->dev_id);

	irq_status = raw_status = readl(dev->hwregs + DEC_INTERRUPT_REGISTER);

	pr_debug("dec_irq\n");

	if (irq_status & DEC_INTERRUPT_BIT) {
		pr_debug("dec_isr dec %x\n", irq_status);
		if ((irq_status & 0x40001) == 0x40001)
		{
			do {
				irq_status = readl(dev->hwregs + DEC_INTERRUPT_REGISTER);
			} while ((irq_status & 0x40001) == 0x40001);
		}

		/* clear dec IRQ */
		if (pservice->hw_info->hw_id != HEVC_ID)
			writel(irq_status & (~DEC_INTERRUPT_BIT|DEC_BUFFER_EMPTY_BIT), dev->hwregs + DEC_INTERRUPT_REGISTER);
		else
			writel(0, dev->hwregs + DEC_INTERRUPT_REGISTER);
		atomic_add(1, &dev->irq_count_codec);
	}

	if (pservice->hw_info->hw_id != HEVC_ID) {
		irq_status = readl(dev->hwregs + PP_INTERRUPT_REGISTER);
		if (irq_status & PP_INTERRUPT_BIT) {
			pr_debug("vdpu_isr pp  %x\n", irq_status);
			/* clear pp IRQ */
			writel(irq_status & (~DEC_INTERRUPT_BIT), dev->hwregs + PP_INTERRUPT_REGISTER);
			atomic_add(1, &dev->irq_count_pp);
		}
	}

	pservice->irq_status = raw_status;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t vdpu_isr(int irq, void *dev_id)
{
	struct vpu_service_info *pservice = (struct vpu_service_info*)dev_id;
	vpu_device *dev = &pservice->dec_dev;

	mutex_lock(&pservice->lock);
	if (atomic_read(&dev->irq_count_codec)) {
#if VPU_SERVICE_SHOW_TIME
		do_gettimeofday(&dec_end);
		pr_info("dec task: %ld ms\n",
			(dec_end.tv_sec  - dec_start.tv_sec)  * 1000 +
			(dec_end.tv_usec - dec_start.tv_usec) / 1000);
#endif
		atomic_sub(1, &dev->irq_count_codec);
		if (NULL == pservice->reg_codec) {
			pr_err("error: dec isr with no task waiting\n");
		} else {
			reg_from_run_to_done(pservice, pservice->reg_codec);
		}
	}

	if (atomic_read(&dev->irq_count_pp)) {

#if VPU_SERVICE_SHOW_TIME
		do_gettimeofday(&pp_end);
		printk("pp  task: %ld ms\n",
			(pp_end.tv_sec  - pp_start.tv_sec)  * 1000 +
			(pp_end.tv_usec - pp_start.tv_usec) / 1000);
#endif

		atomic_sub(1, &dev->irq_count_pp);
		if (NULL == pservice->reg_pproc) {
			pr_err("error: pp isr with no task waiting\n");
		} else {
			reg_from_run_to_done(pservice, pservice->reg_pproc);
		}
	}
	try_set_reg(pservice);
	mutex_unlock(&pservice->lock);
	return IRQ_HANDLED;
}

static irqreturn_t vepu_irq(int irq, void *dev_id)
{
	struct vpu_service_info *pservice = (struct vpu_service_info*)dev_id;
	vpu_device *dev = &pservice->enc_dev;
	u32 irq_status;

	vcodec_select_mode(pservice->dev_id);
	irq_status= readl(dev->hwregs + ENC_INTERRUPT_REGISTER);

	pr_debug("vepu_irq irq status %x\n", irq_status);

#if VPU_SERVICE_SHOW_TIME
	do_gettimeofday(&enc_end);
	pr_info("enc task: %ld ms\n",
		(enc_end.tv_sec  - enc_start.tv_sec)  * 1000 +
		(enc_end.tv_usec - enc_start.tv_usec) / 1000);
#endif
	if (likely(irq_status & ENC_INTERRUPT_BIT)) {
		/* clear enc IRQ */
		writel(irq_status & (~ENC_INTERRUPT_BIT), dev->hwregs + ENC_INTERRUPT_REGISTER);
		atomic_add(1, &dev->irq_count_codec);
	}

	pservice->irq_status = irq_status;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t vepu_isr(int irq, void *dev_id)
{
	struct vpu_service_info *pservice = (struct vpu_service_info*)dev_id;
	vpu_device *dev = &pservice->enc_dev;

	mutex_lock(&pservice->lock);
	if (atomic_read(&dev->irq_count_codec)) {
		atomic_sub(1, &dev->irq_count_codec);
		if (NULL == pservice->reg_codec) {
			pr_err("error: enc isr with no task waiting\n");
		} else {
			reg_from_run_to_done(pservice, pservice->reg_codec);
		}
	}
	try_set_reg(pservice);
	mutex_unlock(&pservice->lock);
	return IRQ_HANDLED;
}

static int __init vcodec_service_init(void)
{
	int ret;

	if ((ret = platform_driver_register(&vcodec_driver)) != 0) {
		pr_err("Platform device register failed (%d).\n", ret);
		return ret;
	}

#ifdef CONFIG_DEBUG_FS
	vcodec_debugfs_init();
#endif

	return ret;
}

static void __exit vcodec_service_exit(void)
{
#ifdef CONFIG_DEBUG_FS
	vcodec_debugfs_exit();
#endif

	platform_driver_unregister(&vcodec_driver);
}

module_init(vcodec_service_init);
module_exit(vcodec_service_exit);

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static int vcodec_debugfs_init()
{
	parent = debugfs_create_dir("vcodec", NULL);
	if (!parent)
		return -1;

	return 0;
}

static void vcodec_debugfs_exit()
{
	debugfs_remove(parent);
}

static struct dentry* vcodec_debugfs_create_device_dir(char *dirname, struct dentry *parent)
{
	return debugfs_create_dir(dirname, parent);
}

static int debug_vcodec_show(struct seq_file *s, void *unused)
{
	struct vpu_service_info *pservice = s->private;
	unsigned int i, n;
	vpu_reg *reg, *reg_tmp;
	vpu_session *session, *session_tmp;

	mutex_lock(&pservice->lock);
	vpu_service_power_on(pservice);
	if (pservice->hw_info->hw_id != HEVC_ID) {
		seq_printf(s, "\nENC Registers:\n");
		n = pservice->enc_dev.iosize >> 2;
		for (i = 0; i < n; i++) {
			seq_printf(s, "\tswreg%d = %08X\n", i, readl(pservice->enc_dev.hwregs + i));
		}
	}
	seq_printf(s, "\nDEC Registers:\n");
	n = pservice->dec_dev.iosize >> 2;
	for (i = 0; i < n; i++)
		seq_printf(s, "\tswreg%d = %08X\n", i, readl(pservice->dec_dev.hwregs + i));

	seq_printf(s, "\nvpu service status:\n");
	list_for_each_entry_safe(session, session_tmp, &pservice->session, list_session) {
		seq_printf(s, "session pid %d type %d:\n", session->pid, session->type);
		/*seq_printf(s, "waiting reg set %d\n");*/
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
	mutex_unlock(&pservice->lock);

	return 0;
}

static int debug_vcodec_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_vcodec_show, inode->i_private);
}

#endif

#if HEVC_TEST_ENABLE & defined(CONFIG_ION_ROCKCHIP)
#include "hevc_test_inc/pps_00.h"
#include "hevc_test_inc/register_00.h"
#include "hevc_test_inc/rps_00.h"
#include "hevc_test_inc/scaling_list_00.h"
#include "hevc_test_inc/stream_00.h"

#include "hevc_test_inc/pps_01.h"
#include "hevc_test_inc/register_01.h"
#include "hevc_test_inc/rps_01.h"
#include "hevc_test_inc/scaling_list_01.h"
#include "hevc_test_inc/stream_01.h"

#include "hevc_test_inc/cabac.h"

extern struct ion_client *rockchip_ion_client_create(const char * name);

static struct ion_client *ion_client = NULL;
u8* get_align_ptr(u8* tbl, int len, u32 *phy)
{
	int size = (len+15) & (~15);
	struct ion_handle *handle;
	u8 *ptr;// = (u8*)kzalloc(size, GFP_KERNEL);

	if (ion_client == NULL)
		ion_client = rockchip_ion_client_create("vcodec");

	handle = ion_alloc(ion_client, (size_t)len, 16, ION_HEAP(ION_CMA_HEAP_ID), 0);

	ptr = ion_map_kernel(ion_client, handle);

	ion_phys(ion_client, handle, phy, &size);

	memcpy(ptr, tbl, len);

	return ptr;
}

u8* get_align_ptr_no_copy(int len, u32 *phy)
{
	int size = (len+15) & (~15);
	struct ion_handle *handle;
	u8 *ptr;

	if (ion_client == NULL)
		ion_client = rockchip_ion_client_create("vcodec");

	handle = ion_alloc(ion_client, (size_t)len, 16, ION_HEAP(ION_CMA_HEAP_ID), 0);

	ptr = ion_map_kernel(ion_client, handle);

	ion_phys(ion_client, handle, phy, &size);

	return ptr;
}

#define TEST_CNT    2
static int hevc_test_case0(vpu_service_info *pservice)
{
	vpu_session session;
	vpu_reg *reg; 
	unsigned long size = 272;//sizeof(register_00); // registers array length
	int testidx = 0;
	int ret = 0;

	u8 *pps_tbl[TEST_CNT];
	u8 *register_tbl[TEST_CNT];
	u8 *rps_tbl[TEST_CNT];
	u8 *scaling_list_tbl[TEST_CNT];
	u8 *stream_tbl[TEST_CNT];

	int stream_size[2];
	int pps_size[2];
	int rps_size[2];
	int scl_size[2];
	int cabac_size[2];
	
	u32 phy_pps;
	u32 phy_rps;
	u32 phy_scl;
	u32 phy_str;
	u32 phy_yuv;
	u32 phy_ref;
	u32 phy_cabac;

	volatile u8 *stream_buf;
	volatile u8 *pps_buf;
	volatile u8 *rps_buf;
	volatile u8 *scl_buf;
	volatile u8 *yuv_buf;
	volatile u8 *cabac_buf;
	volatile u8 *ref_buf;

	u8 *pps;
	u8 *yuv[2];
	int i;

	pps_tbl[0] = pps_00;
	pps_tbl[1] = pps_01;

	register_tbl[0] = register_00;
	register_tbl[1] = register_01;

	rps_tbl[0] = rps_00;
	rps_tbl[1] = rps_01;

	scaling_list_tbl[0] = scaling_list_00;
	scaling_list_tbl[1] = scaling_list_01;

	stream_tbl[0] = stream_00;
	stream_tbl[1] = stream_01;

	stream_size[0] = sizeof(stream_00);
	stream_size[1] = sizeof(stream_01);

	pps_size[0] = sizeof(pps_00);
	pps_size[1] = sizeof(pps_01);

	rps_size[0] = sizeof(rps_00);
	rps_size[1] = sizeof(rps_01);

	scl_size[0] = sizeof(scaling_list_00);
	scl_size[1] = sizeof(scaling_list_01);
	
	cabac_size[0] = sizeof(Cabac_table);
	cabac_size[1] = sizeof(Cabac_table);

	/* create session */
	session.pid = current->pid;
	session.type = VPU_DEC;
	INIT_LIST_HEAD(&session.waiting);
	INIT_LIST_HEAD(&session.running);
	INIT_LIST_HEAD(&session.done);
	INIT_LIST_HEAD(&session.list_session);
	init_waitqueue_head(&session.wait);
	atomic_set(&session.task_running, 0);
	list_add_tail(&session.list_session, &pservice->session);

	yuv[0] = get_align_ptr_no_copy(256*256*2, &phy_yuv);
	yuv[1] = get_align_ptr_no_copy(256*256*2, &phy_ref);

	while (testidx < TEST_CNT) {
		/* create registers */
		reg = kmalloc(sizeof(vpu_reg)+pservice->reg_size, GFP_KERNEL);
		if (NULL == reg) {
			pr_err("error: kmalloc fail in reg_init\n");
			return -1;
		}

		if (size > pservice->reg_size) {
			printk("warning: vpu reg size %lu is larger than hw reg size %lu\n", size, pservice->reg_size);
			size = pservice->reg_size;
		}
		reg->session = &session;
		reg->type = session.type;
		reg->size = size;
		reg->freq = VPU_FREQ_DEFAULT;
		reg->reg = (unsigned long *)&reg[1];
		INIT_LIST_HEAD(&reg->session_link);
		INIT_LIST_HEAD(&reg->status_link);

		/* TODO: stuff registers */
		memcpy(&reg->reg[0], register_tbl[testidx], /*sizeof(register_00)*/ 176);

		stream_buf = get_align_ptr(stream_tbl[testidx], stream_size[testidx], &phy_str);
		pps_buf = get_align_ptr(pps_tbl[0], pps_size[0], &phy_pps);
		rps_buf = get_align_ptr(rps_tbl[testidx], rps_size[testidx], &phy_rps);
		scl_buf = get_align_ptr(scaling_list_tbl[testidx], scl_size[testidx], &phy_scl);
		cabac_buf = get_align_ptr(Cabac_table, cabac_size[testidx], &phy_cabac);

		pps = pps_buf;

		/* TODO: replace reigster address */
		for (i=0; i<64; i++) {
			u32 scaling_offset;
			u32 tmp;

			scaling_offset = (u32)pps[i*80+74];
			scaling_offset += (u32)pps[i*80+75] << 8;
			scaling_offset += (u32)pps[i*80+76] << 16;
			scaling_offset += (u32)pps[i*80+77] << 24;

			tmp = phy_scl + scaling_offset;

			pps[i*80+74] = tmp & 0xff;
			pps[i*80+75] = (tmp >> 8) & 0xff;
			pps[i*80+76] = (tmp >> 16) & 0xff;
			pps[i*80+77] = (tmp >> 24) & 0xff;
		}

		printk("%s %d, phy stream %08x, phy pps %08x, phy rps %08x\n",
			__func__, __LINE__, phy_str, phy_pps, phy_rps);

		reg->reg[1] = 0x21;
		reg->reg[4] = phy_str;
		reg->reg[5] = ((stream_size[testidx]+15)&(~15))+64;
		reg->reg[6] = phy_cabac;
		reg->reg[7] = testidx?phy_ref:phy_yuv;
		reg->reg[42] = phy_pps;
		reg->reg[43] = phy_rps;
		for (i = 10; i <= 24; i++)
			reg->reg[i] = phy_yuv;

		mutex_lock(&pservice->lock);
		list_add_tail(&reg->status_link, &pservice->waiting);
		list_add_tail(&reg->session_link, &session.waiting);
		mutex_unlock(&pservice->lock);

		printk("%s %d %p\n", __func__, __LINE__, pservice);

		/* stuff hardware */
		try_set_reg(pservice);

		/* wait for result */
		ret = wait_event_timeout(session.wait, !list_empty(&session.done), VPU_TIMEOUT_DELAY);
		if (!list_empty(&session.done)) {
			if (ret < 0)
				pr_err("warning: pid %d wait task sucess but wait_evernt ret %d\n", session.pid, ret);
			ret = 0;
		} else {
			if (unlikely(ret < 0)) {
				pr_err("error: pid %d wait task ret %d\n", session.pid, ret);
			} else if (0 == ret) {
				pr_err("error: pid %d wait %d task done timeout\n", session.pid, atomic_read(&session.task_running));
				ret = -ETIMEDOUT;
			}
		}
		if (ret < 0) {
			int task_running = atomic_read(&session.task_running);
			int n;
			mutex_lock(&pservice->lock);
			vpu_service_dump(pservice);
			if (task_running) {
				atomic_set(&session.task_running, 0);
				atomic_sub(task_running, &pservice->total_running);
				printk("%d task is running but not return, reset hardware...", task_running);
				vpu_reset(pservice);
				printk("done\n");
			}
			vpu_service_session_clear(pservice, &session);
			mutex_unlock(&pservice->lock);

			printk("\nDEC Registers:\n");
			n = pservice->dec_dev.iosize >> 2;
			for (i=0; i<n; i++)
				printk("\tswreg%d = %08X\n", i, readl(pservice->dec_dev.hwregs + i));

			pr_err("test index %d failed\n", testidx);
			break;
		} else {
			pr_info("test index %d success\n", testidx);

			vpu_reg *reg = list_entry(session.done.next, vpu_reg, session_link);

			for (i=0; i<68; i++) {
				if (i % 4 == 0)
					printk("%02d: ", i);
				printk("%08x ", reg->reg[i]);
				if ((i+1) % 4 == 0)
					printk("\n");
			}

			testidx++;
		}

		reg_deinit(pservice, reg);
	}

	return 0;
}

#endif

