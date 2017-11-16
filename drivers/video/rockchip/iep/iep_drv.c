/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
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
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/rk_fb.h>
#include <linux/wakelock.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/rockchip/cpu.h>
#include <asm/cacheflush.h>
#include "iep_drv.h"
#include "hw_iep_reg.h"
#include "iep_iommu_ops.h"

#define IEP_MAJOR		255
#define IEP_CLK_ENABLE
/*#define IEP_TEST_CASE*/

static int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug,
		 "Debug level - higher value produces more verbose messages");

#define RK_IEP_SIZE		0x1000
#define IEP_TIMEOUT_DELAY	2*HZ
#define IEP_POWER_OFF_DELAY	4*HZ

struct iep_drvdata {
	struct miscdevice miscdev;
	void *iep_base;
	int irq0;

	struct clk *aclk_iep;
	struct clk *hclk_iep;
	struct clk *pd_iep;
	struct clk *aclk_vio1;

	struct mutex mutex;

	/* direct path interface mode. true: enable, false: disable */
	bool dpi_mode;

	struct delayed_work power_off_work;

	/* clk enable or disable */
	bool enable;
	struct wake_lock wake_lock;

	atomic_t iep_int;
	atomic_t mmu_page_fault;
	atomic_t mmu_bus_error;

	/* capability for this iep device */
	struct IEP_CAP cap;
	struct device *dev;
};

struct iep_drvdata *iep_drvdata1 = NULL;
iep_service_info iep_service;

static void iep_reg_deinit(struct iep_reg *reg)
{
	struct iep_mem_region *mem_region = NULL, *n;
	/* release memory region attach to this registers table.*/
	if (iep_service.iommu_dev) {
		list_for_each_entry_safe(mem_region, n, &reg->mem_region_list,
					 reg_lnk) {
			iep_iommu_unmap_iommu(iep_service.iommu_info,
					      reg->session, mem_region->hdl);
			iep_iommu_free(iep_service.iommu_info,
				       reg->session, mem_region->hdl);
			list_del_init(&mem_region->reg_lnk);
			kfree(mem_region);
		}
	}

	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	kfree(reg);
}

static void iep_reg_from_wait_to_ready(struct iep_reg *reg)
{
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &iep_service.ready);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->ready);
}

static void iep_reg_from_ready_to_running(struct iep_reg *reg)
{
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &iep_service.running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
}

static void iep_del_running_list(void)
{
	struct iep_reg *reg;
	int cnt = 0;

	mutex_lock(&iep_service.lock);

	while (!list_empty(&iep_service.running)) {
		BUG_ON(cnt != 0);
		reg = list_entry(iep_service.running.next,
				 struct iep_reg, status_link);

		atomic_dec(&reg->session->task_running);
		atomic_dec(&iep_service.total_running);

		if (list_empty(&reg->session->waiting)) {
			atomic_set(&reg->session->done, 1);
			atomic_inc(&reg->session->num_done);
			wake_up(&reg->session->wait);
		}

		iep_reg_deinit(reg);
		cnt++;
	}

	mutex_unlock(&iep_service.lock);
}

static void iep_dump(void)
{
	struct iep_status sts;

	sts = iep_get_status(iep_drvdata1->iep_base);

	IEP_INFO("scl_sts: %u, dil_sts %u, wyuv_sts %u, ryuv_sts %u, wrgb_sts %u, rrgb_sts %u, voi_sts %u\n",
		sts.scl_sts, sts.dil_sts, sts.wyuv_sts, sts.ryuv_sts, sts.wrgb_sts, sts.rrgb_sts, sts.voi_sts); {
		int *reg = (int *)iep_drvdata1->iep_base;
		int i;

		/* could not read validate data from address after base+0x40 */
		for (i = 0; i < 0x40; i++) {
			IEP_INFO("%08x ", reg[i]);

			if ((i + 1) % 4 == 0) {
				IEP_INFO("\n");
			}
		}

		IEP_INFO("\n");
	}
}

/* Caller must hold iep_service.lock */
static void iep_del_running_list_timeout(void)
{
	struct iep_reg *reg;

	mutex_lock(&iep_service.lock);

	while (!list_empty(&iep_service.running)) {
		reg = list_entry(iep_service.running.next, struct iep_reg, status_link);

		atomic_dec(&reg->session->task_running);
		atomic_dec(&iep_service.total_running);

		/* iep_soft_rst(iep_drvdata1->iep_base); */

		iep_dump();

		if (list_empty(&reg->session->waiting)) {
			atomic_set(&reg->session->done, 1);
			wake_up(&reg->session->wait);
		}

		iep_reg_deinit(reg);
	}

	mutex_unlock(&iep_service.lock);
}

static inline void iep_queue_power_off_work(void)
{
	queue_delayed_work(system_wq, &iep_drvdata1->power_off_work, IEP_POWER_OFF_DELAY);
}

static void iep_power_on(void)
{
	static ktime_t last;
	ktime_t now = ktime_get();
	if (ktime_to_ns(ktime_sub(now, last)) > NSEC_PER_SEC) {
		cancel_delayed_work_sync(&iep_drvdata1->power_off_work);
		iep_queue_power_off_work();
		last = now;
	}

	if (iep_service.enable)
		return;

	IEP_INFO("IEP Power ON\n");

	/* iep_soft_rst(iep_drvdata1->iep_base); */

#ifdef IEP_CLK_ENABLE
	pm_runtime_get_sync(iep_drvdata1->dev);
	if (iep_drvdata1->pd_iep)
		clk_prepare_enable(iep_drvdata1->pd_iep);
	clk_prepare_enable(iep_drvdata1->aclk_iep);
	clk_prepare_enable(iep_drvdata1->hclk_iep);
#endif

	wake_lock(&iep_drvdata1->wake_lock);

	iep_iommu_attach(iep_service.iommu_info);

	iep_service.enable = true;
}

static void iep_power_off(void)
{
	int total_running;

	if (!iep_service.enable) {
		return;
	}

	IEP_INFO("IEP Power OFF\n");

	total_running = atomic_read(&iep_service.total_running);
	if (total_running) {
		IEP_WARNING("power off when %d task running!!\n", total_running);
		mdelay(50);
		IEP_WARNING("delay 50 ms for running task\n");
		iep_dump();
	}

	if (iep_service.iommu_dev) {
		iep_iommu_detach(iep_service.iommu_info);
	}

#ifdef IEP_CLK_ENABLE
	clk_disable_unprepare(iep_drvdata1->aclk_iep);
	clk_disable_unprepare(iep_drvdata1->hclk_iep);
	if (iep_drvdata1->pd_iep)
		clk_disable_unprepare(iep_drvdata1->pd_iep);
	pm_runtime_put(iep_drvdata1->dev);
#endif

	wake_unlock(&iep_drvdata1->wake_lock);
	iep_service.enable = false;
}

static void iep_power_off_work(struct work_struct *work)
{
	if (mutex_trylock(&iep_service.lock)) {
		if (!iep_drvdata1->dpi_mode) {
			IEP_INFO("iep dpi mode inactivity\n");
			iep_power_off();
		}
		mutex_unlock(&iep_service.lock);
	} else {
		/* Come back later if the device is busy... */
		iep_queue_power_off_work();
	}
}

#ifdef CONFIG_FB_ROCKCHIP
extern void rk_direct_fb_show(struct fb_info *fbi);
extern struct fb_info* rk_get_fb(int fb_id);
extern bool rk_fb_poll_wait_frame_complete(void);
extern int rk_fb_dpi_open(bool open);
extern int rk_fb_dpi_win_sel(int layer_id);

static void iep_config_lcdc(struct iep_reg *reg)
{
	struct fb_info *fb;
	int fbi = 0;
	int fmt = 0;

	fbi = reg->layer == 0 ? 0 : 1;

	rk_fb_dpi_win_sel(fbi);

	fb = rk_get_fb(fbi);
#if 1
	switch (reg->format) {
	case IEP_FORMAT_ARGB_8888:
	case IEP_FORMAT_ABGR_8888:
		fmt = HAL_PIXEL_FORMAT_RGBA_8888;
		fb->var.bits_per_pixel = 32;

		fb->var.red.length = 8;
		fb->var.red.offset = 16;
		fb->var.red.msb_right = 0;

		fb->var.green.length = 8;
		fb->var.green.offset = 8;
		fb->var.green.msb_right = 0;

		fb->var.blue.length = 8;
		fb->var.blue.offset = 0;
		fb->var.blue.msb_right = 0;

		fb->var.transp.length = 8;
		fb->var.transp.offset = 24;
		fb->var.transp.msb_right = 0;

		break;
	case IEP_FORMAT_BGRA_8888:
		fmt = HAL_PIXEL_FORMAT_BGRA_8888;
		fb->var.bits_per_pixel = 32;
		break;
	case IEP_FORMAT_RGB_565:
		fmt = HAL_PIXEL_FORMAT_RGB_565;
		fb->var.bits_per_pixel = 16;

		fb->var.red.length = 5;
		fb->var.red.offset = 11;
		fb->var.red.msb_right = 0;

		fb->var.green.length = 6;
		fb->var.green.offset = 5;
		fb->var.green.msb_right = 0;

		fb->var.blue.length = 5;
		fb->var.blue.offset = 0;
		fb->var.blue.msb_right = 0;

		break;
	case IEP_FORMAT_YCbCr_422_SP:
		fmt = HAL_PIXEL_FORMAT_YCbCr_422_SP;
		fb->var.bits_per_pixel = 16;
		break;
	case IEP_FORMAT_YCbCr_420_SP:
		fmt = HAL_PIXEL_FORMAT_YCrCb_NV12;
		fb->var.bits_per_pixel = 16;
		break;
	case IEP_FORMAT_YCbCr_422_P:
	case IEP_FORMAT_YCrCb_422_SP:
	case IEP_FORMAT_YCrCb_422_P:
	case IEP_FORMAT_YCrCb_420_SP:
	case IEP_FORMAT_YCbCr_420_P:
	case IEP_FORMAT_YCrCb_420_P:
	case IEP_FORMAT_RGBA_8888:
	case IEP_FORMAT_BGR_565:
		/* unsupported format */
		IEP_ERR("unsupported format %d\n", reg->format);
		break;
	default:
		;
	}

	fb->var.xoffset = 0;
	fb->var.yoffset = 0;
	fb->var.xres = reg->act_width;
	fb->var.yres = reg->act_height;
	fb->var.xres_virtual = reg->act_width;
	fb->var.yres_virtual = reg->act_height;
	fb->var.nonstd = ((reg->off_y & 0xFFF) << 20) +
		((reg->off_x & 0xFFF) << 8) + (fmt & 0xFF);
	fb->var.grayscale =
		((reg->vir_height & 0xFFF) << 20) +
		((reg->vir_width & 0xFFF) << 8) + 0;/*win0 xsize & ysize*/
#endif
	rk_direct_fb_show(fb);
}

static int iep_switch_dpi(struct iep_reg *reg)
{
	if (reg->dpi_en) {
		if (!iep_drvdata1->dpi_mode) {
			/* Turn on dpi */
			rk_fb_dpi_open(true);
			iep_drvdata1->dpi_mode = true;
		}
		iep_config_lcdc(reg);
	} else {
		if (iep_drvdata1->dpi_mode) {
			/* Turn off dpi */
			/* wait_lcdc_dpi_close(); */
			bool status;
			rk_fb_dpi_open(false);
			status = rk_fb_poll_wait_frame_complete();
			iep_drvdata1->dpi_mode = false;
			IEP_INFO("%s %d, iep dpi inactivated\n",
				 __func__, __LINE__);
		}
	}

	return 0;
}
#endif

static void iep_reg_copy_to_hw(struct iep_reg *reg)
{
	int i;

	u32 *pbase = (u32 *)iep_drvdata1->iep_base;

	/* config registers */
	for (i = 0; i < IEP_CNF_REG_LEN; i++)
		pbase[IEP_CNF_REG_BASE + i] = reg->reg[IEP_CNF_REG_BASE + i];

	/* command registers */
	for (i = 0; i < IEP_CMD_REG_LEN; i++)
		pbase[IEP_CMD_REG_BASE + i] = reg->reg[IEP_CMD_REG_BASE + i];

	/* address registers */
	for (i = 0; i < IEP_ADD_REG_LEN; i++)
		pbase[IEP_ADD_REG_BASE + i] = reg->reg[IEP_ADD_REG_BASE + i];

	/* dmac_flush_range(&pbase[0], &pbase[IEP_REG_LEN]); */
	/* outer_flush_range(virt_to_phys(&pbase[0]),virt_to_phys(&pbase[IEP_REG_LEN])); */

	dsb(sy);
}

/** switch fields order before the next lcdc frame start
 *  coming */
static void iep_switch_fields_order(void)
{
	void *pbase = (void *)iep_drvdata1->iep_base;
	int mode = iep_get_deinterlace_mode(pbase);
#ifdef CONFIG_FB_ROCKCHIP
	struct fb_info *fb;
#endif
	switch (mode) {
	case dein_mode_I4O1B:
		iep_set_deinterlace_mode(dein_mode_I4O1T, pbase);
		break;
	case dein_mode_I4O1T:
		iep_set_deinterlace_mode(dein_mode_I4O1B, pbase);
		break;
	case dein_mode_I2O1B:
		iep_set_deinterlace_mode(dein_mode_I2O1T, pbase);
		break;
	case dein_mode_I2O1T:
		iep_set_deinterlace_mode(dein_mode_I2O1B, pbase);
		break;
	default:
		;
	}
#ifdef CONFIG_FB_ROCKCHIP
	fb = rk_get_fb(1);
	rk_direct_fb_show(fb);
#endif
	/*iep_switch_input_address(pbase);*/
}

/* Caller must hold iep_service.lock */
static void iep_try_set_reg(void)
{
	struct iep_reg *reg;

	mutex_lock(&iep_service.lock);

	if (list_empty(&iep_service.ready)) {
		if (!list_empty(&iep_service.waiting)) {
			reg = list_entry(iep_service.waiting.next, struct iep_reg, status_link);

			iep_power_on();
			udelay(1);

			iep_reg_from_wait_to_ready(reg);
			atomic_dec(&iep_service.waitcnt);

			/*iep_soft_rst(iep_drvdata1->iep_base);*/

			iep_reg_copy_to_hw(reg);
		}
	} else {
		if (iep_drvdata1->dpi_mode)
			iep_switch_fields_order();
	}

	mutex_unlock(&iep_service.lock);
}

static void iep_try_start_frm(void)
{
	struct iep_reg *reg;

	mutex_lock(&iep_service.lock);

	if (list_empty(&iep_service.running)) {
		if (!list_empty(&iep_service.ready)) {
			reg = list_entry(iep_service.ready.next, struct iep_reg, status_link);
#ifdef CONFIG_FB_ROCKCHIP
			iep_switch_dpi(reg);
#endif
			iep_reg_from_ready_to_running(reg);
			iep_config_frame_end_int_en(iep_drvdata1->iep_base);
			iep_config_done(iep_drvdata1->iep_base);

			/* Start proc */
			atomic_inc(&reg->session->task_running);
			atomic_inc(&iep_service.total_running);
			iep_config_frm_start(iep_drvdata1->iep_base);
		}
	}

	mutex_unlock(&iep_service.lock);
}

static irqreturn_t iep_isr(int irq, void *dev_id)
{
	if (atomic_read(&iep_drvdata1->iep_int) > 0) {
		if (iep_service.enable) {
			if (list_empty(&iep_service.waiting)) {
				if (iep_drvdata1->dpi_mode) {
					iep_switch_fields_order();
				}
			}
			iep_del_running_list();
		}

		iep_try_set_reg();
		iep_try_start_frm();

		atomic_dec(&iep_drvdata1->iep_int);
	}

	return IRQ_HANDLED;
}

static irqreturn_t iep_irq(int irq,  void *dev_id)
{
	/*clear INT */
	void *pbase = (void *)iep_drvdata1->iep_base;

	if (iep_probe_int(pbase)) {
		iep_config_frame_end_int_clr(pbase);
		atomic_inc(&iep_drvdata1->iep_int);
	}

	return IRQ_WAKE_THREAD;
}

static void iep_service_session_clear(iep_session *session)
{
	struct iep_reg *reg, *n;

	list_for_each_entry_safe(reg, n, &session->waiting, session_link) {
		iep_reg_deinit(reg);
	}

	list_for_each_entry_safe(reg, n, &session->ready, session_link) {
		iep_reg_deinit(reg);
	}

	list_for_each_entry_safe(reg, n, &session->running, session_link) {
		iep_reg_deinit(reg);
	}
}

static int iep_open(struct inode *inode, struct file *filp)
{
	//DECLARE_WAITQUEUE(wait, current);
	iep_session *session = (iep_session *)kzalloc(sizeof(iep_session),
		GFP_KERNEL);
	if (NULL == session) {
		IEP_ERR("unable to allocate memory for iep_session.\n");
		return -ENOMEM;
	}

	session->pid = current->pid;
	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->ready);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	/*add_wait_queue(&session->wait, wait);*/
	/* no need to protect */
	mutex_lock(&iep_service.lock);
	list_add_tail(&session->list_session, &iep_service.session);
	mutex_unlock(&iep_service.lock);
	atomic_set(&session->task_running, 0);
	atomic_set(&session->num_done, 0);

	filp->private_data = (void *)session;

	return nonseekable_open(inode, filp);
}

static int iep_release(struct inode *inode, struct file *filp)
{
	int task_running;
	iep_session *session = (iep_session *)filp->private_data;

	if (NULL == session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);

	if (task_running) {
		IEP_ERR("iep_service session %d still "
			"has %d task running when closing\n",
			session->pid, task_running);
		msleep(100);
		/*synchronization*/
	}

	wake_up(&session->wait);
	iep_power_on();
	mutex_lock(&iep_service.lock);
	list_del(&session->list_session);
	iep_service_session_clear(session);
	iep_iommu_clear(iep_service.iommu_info, session);
	kfree(session);
	mutex_unlock(&iep_service.lock);

	return 0;
}

static unsigned int iep_poll(struct file *filp, poll_table *wait)
{
	int mask = 0;
	iep_session *session = (iep_session *)filp->private_data;
	if (NULL == session)
		return POLL_ERR;
	poll_wait(filp, &session->wait, wait);
	if (atomic_read(&session->done))
		mask |= POLL_IN | POLLRDNORM;

	return mask;
}

static int iep_get_result_sync(iep_session *session)
{
	int ret = 0;

	iep_try_start_frm();

	ret = wait_event_timeout(session->wait,
		atomic_read(&session->done), IEP_TIMEOUT_DELAY);

	if (unlikely(ret < 0)) {
		IEP_ERR("sync pid %d wait task ret %d\n", session->pid, ret);
		iep_del_running_list();
		ret = ret;
	} else if (0 == ret) {
		IEP_ERR("sync pid %d wait %d task done timeout\n",
			session->pid, atomic_read(&session->task_running));
		iep_del_running_list_timeout();
		iep_try_set_reg();
		iep_try_start_frm();
		ret = -ETIMEDOUT;
	}

	return ret;
}

static void iep_get_result_async(iep_session *session)
{
	iep_try_start_frm();
	return;
}

static long iep_ioctl(struct file *filp, uint32_t cmd, unsigned long arg)
{
	int ret = 0;
	iep_session *session = (iep_session *)filp->private_data;

	if (NULL == session) {
		IEP_ERR("%s [%d] iep thread session is null\n",
			__FUNCTION__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&iep_service.mutex);

	switch (cmd) {
	case IEP_SET_PARAMETER:
		{
			struct IEP_MSG *msg;
			msg = (struct IEP_MSG *)kzalloc(sizeof(struct IEP_MSG),
				GFP_KERNEL);
			if (msg) {
				if (copy_from_user(msg, (struct IEP_MSG *)arg,
						sizeof(struct IEP_MSG))) {
					IEP_ERR("copy_from_user failure\n");
					ret = -EFAULT;
				}
			}

			if (ret == 0) {
				if (atomic_read(&iep_service.waitcnt) < 10) {
					iep_power_on();
					iep_config(session, msg);
					atomic_inc(&iep_service.waitcnt);
				} else {
					IEP_ERR("iep task queue full\n");
					ret = -EFAULT;
				}
			}

			/** REGISTER CONFIG must accord to Timing When DPI mode
			 *  enable */
			if (!iep_drvdata1->dpi_mode)
				iep_try_set_reg();
			kfree(msg);
		}
		break;
	case IEP_GET_RESULT_SYNC:
		if (0 > iep_get_result_sync(session)) {
			ret = -ETIMEDOUT;
		}
		break;
	case IEP_GET_RESULT_ASYNC:
		iep_get_result_async(session);
		break;
	case IEP_RELEASE_CURRENT_TASK:
		iep_del_running_list_timeout();
		iep_try_set_reg();
		iep_try_start_frm();
		break;
	case IEP_GET_IOMMU_STATE:
		{
			int iommu_enable = 0;

			iommu_enable = iep_service.iommu_dev ? 1 : 0;

			if (copy_to_user((void __user *)arg, &iommu_enable,
				sizeof(int))) {
				IEP_ERR("error: copy_to_user failed\n");
				ret = -EFAULT;
			}
		}
		break;
	case IEP_QUERY_CAP:
		if (copy_to_user((void __user *)arg, &iep_drvdata1->cap,
			sizeof(struct IEP_CAP))) {
			IEP_ERR("error: copy_to_user failed\n");
			ret = -EFAULT;
		}
		break;
	default:
		IEP_ERR("unknown ioctl cmd!\n");
		ret = -EINVAL;
	}
	mutex_unlock(&iep_service.mutex);

	return ret;
}

#ifdef CONFIG_COMPAT
static long compat_iep_ioctl(struct file *filp, uint32_t cmd,
			     unsigned long arg)
{
	int ret = 0;
	iep_session *session = (iep_session *)filp->private_data;

	if (NULL == session) {
		IEP_ERR("%s [%d] iep thread session is null\n",
			__func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&iep_service.mutex);

	switch (cmd) {
	case COMPAT_IEP_SET_PARAMETER:
		{
			struct IEP_MSG *msg;

			msg = kzalloc(sizeof(*msg), GFP_KERNEL);

			if (msg) {
				if (copy_from_user
				    (msg, compat_ptr((compat_uptr_t)arg),
				     sizeof(struct IEP_MSG))) {
					IEP_ERR("copy_from_user failure\n");
					ret = -EFAULT;
				}
			}

			if (ret == 0) {
				if (atomic_read(&iep_service.waitcnt) < 10) {
					iep_power_on();
					iep_config(session, msg);
					atomic_inc(&iep_service.waitcnt);
				} else {
					IEP_ERR("iep task queue full\n");
					ret = -EFAULT;
				}
			}

			/** REGISTER CONFIG must accord to Timing When DPI mode
			 *  enable */
			if (!iep_drvdata1->dpi_mode)
				iep_try_set_reg();
			kfree(msg);
		}
		break;
	case COMPAT_IEP_GET_RESULT_SYNC:
		if (0 > iep_get_result_sync(session))
			ret = -ETIMEDOUT;
		break;
	case COMPAT_IEP_GET_RESULT_ASYNC:
		iep_get_result_async(session);
		break;
	case COMPAT_IEP_RELEASE_CURRENT_TASK:
		iep_del_running_list_timeout();
		iep_try_set_reg();
		iep_try_start_frm();
		break;
	case COMPAT_IEP_GET_IOMMU_STATE:
		{
			int iommu_enable = 0;

			iommu_enable = iep_service.iommu_dev ? 1 : 0;

			if (copy_to_user((void __user *)arg, &iommu_enable,
				sizeof(int))) {
				IEP_ERR("error: copy_to_user failed\n");
				ret = -EFAULT;
			}
		}
		break;
	case COMPAT_IEP_QUERY_CAP:
		if (copy_to_user((void __user *)arg, &iep_drvdata1->cap,
			sizeof(struct IEP_CAP))) {
			IEP_ERR("error: copy_to_user failed\n");
			ret = -EFAULT;
		}
		break;
	default:
		IEP_ERR("unknown ioctl cmd!\n");
		ret = -EINVAL;
	}
	mutex_unlock(&iep_service.mutex);

	return ret;
}
#endif

struct file_operations iep_fops = {
	.owner		= THIS_MODULE,
	.open		= iep_open,
	.release	= iep_release,
	.poll		= iep_poll,
	.unlocked_ioctl	= iep_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= compat_iep_ioctl,
#endif
};

static struct miscdevice iep_dev = {
	.minor = IEP_MAJOR,
	.name  = "iep",
	.fops  = &iep_fops,
};

static struct device* rockchip_get_sysmmu_device_by_compatible(
	const char *compt)
{
	struct device_node *dn = NULL;
	struct platform_device *pd = NULL;
	struct device *ret = NULL;

	dn = of_find_compatible_node(NULL, NULL, compt);
	if (!dn) {
		printk("can't find device node %s \r\n", compt);
		return NULL;
	}

	pd = of_find_device_by_node(dn);
	if (!pd) {
		printk("can't find platform device in device node %s \r\n",
			compt);
		return  NULL;
	}
	ret = &pd->dev;

	return ret;

}
#ifdef CONFIG_IOMMU_API
static inline void platform_set_sysmmu(struct device *iommu,
	struct device *dev)
{
	dev->archdata.iommu = iommu;
}
#else
static inline void platform_set_sysmmu(struct device *iommu,
	struct device *dev)
{
}
#endif

static int iep_sysmmu_fault_handler(struct device *dev,
	enum rk_iommu_inttype itype,
	unsigned long pgtable_base,
	unsigned long fault_addr, unsigned int status)
{
	struct iep_reg *reg = list_entry(iep_service.running.next,
		struct iep_reg, status_link);
	if (reg != NULL) {
		struct iep_mem_region *mem, *n;
		int i = 0;
		pr_info("iep, fault addr 0x%08x\n", (u32)fault_addr);
		list_for_each_entry_safe(mem, n,
			&reg->mem_region_list,
			reg_lnk) {
			pr_info("iep, mem region [%02d] 0x%08x %ld\n",
				i, (u32)mem->iova, mem->len);
			i++;
		}

		pr_alert("iep, page fault occur\n");

		iep_del_running_list();
	}

	return 0;
}

static int iep_drv_probe(struct platform_device *pdev)
{
	struct iep_drvdata *data;
	int ret = 0;
	struct resource *res = NULL;
	u32 version;
	struct device_node *np = pdev->dev.of_node;
	struct platform_device *sub_dev = NULL;
	struct device_node *sub_np = NULL;
	u32 iommu_en = 0;
	struct device *mmu_dev = NULL;
	of_property_read_u32(np, "iommu_enabled", &iommu_en);

	data = (struct iep_drvdata *)devm_kzalloc(&pdev->dev,
		sizeof(struct iep_drvdata), GFP_KERNEL);
	if (NULL == data) {
		IEP_ERR("failed to allocate driver data.\n");
		return  -ENOMEM;
	}

	iep_drvdata1 = data;

	INIT_LIST_HEAD(&iep_service.waiting);
	INIT_LIST_HEAD(&iep_service.ready);
	INIT_LIST_HEAD(&iep_service.running);
	INIT_LIST_HEAD(&iep_service.done);
	INIT_LIST_HEAD(&iep_service.session);
	atomic_set(&iep_service.waitcnt, 0);
	mutex_init(&iep_service.lock);
	atomic_set(&iep_service.total_running, 0);
	iep_service.enable = false;

#ifdef IEP_CLK_ENABLE
	data->pd_iep = devm_clk_get(&pdev->dev, "pd_iep");
	if (IS_ERR(data->pd_iep)) {
		IEP_ERR("failed to find iep power down clock source.\n");
		data->pd_iep = NULL;
	}

	data->aclk_iep = devm_clk_get(&pdev->dev, "aclk_iep");
	if (IS_ERR(data->aclk_iep)) {
		IEP_ERR("failed to find iep axi clock source.\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->hclk_iep = devm_clk_get(&pdev->dev, "hclk_iep");
	if (IS_ERR(data->hclk_iep)) {
		IEP_ERR("failed to find iep ahb clock source.\n");
		ret = -ENOENT;
		goto err_clock;
	}
#endif

	iep_service.enable = false;
	INIT_DELAYED_WORK(&data->power_off_work, iep_power_off_work);
	wake_lock_init(&data->wake_lock, WAKE_LOCK_SUSPEND, "iep");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	data->iep_base = (void *)devm_ioremap_resource(&pdev->dev, res);
	if (data->iep_base == NULL) {
		IEP_ERR("iep ioremap failed\n");
		ret = -ENOENT;
		goto err_ioremap;
	}

	atomic_set(&data->iep_int, 0);
	atomic_set(&data->mmu_page_fault, 0);
	atomic_set(&data->mmu_bus_error, 0);

	/* get the IRQ */
	data->irq0 = platform_get_irq(pdev, 0);
	if (data->irq0 <= 0) {
		IEP_ERR("failed to get iep irq resource (%d).\n", data->irq0);
		ret = data->irq0;
		goto err_irq;
	}

	/* request the IRQ */
	ret = devm_request_threaded_irq(&pdev->dev, data->irq0, iep_irq,
		iep_isr, IRQF_SHARED, dev_name(&pdev->dev), pdev);
	if (ret) {
		IEP_ERR("iep request_irq failed (%d).\n", ret);
		goto err_irq;
	}

	mutex_init(&iep_service.mutex);

	if (of_property_read_u32(np, "version", &version)) {
		version = 0;
	}

	data->cap.scaling_supported = 0;
	data->cap.i4_deinterlace_supported = 1;
	data->cap.i2_deinterlace_supported = 1;
	data->cap.compression_noise_reduction_supported = 1;
	data->cap.sampling_noise_reduction_supported = 1;
	data->cap.hsb_enhancement_supported = 1;
	data->cap.cg_enhancement_supported = 1;
	data->cap.direct_path_supported = 1;
	data->cap.max_dynamic_width = 1920;
	data->cap.max_dynamic_height = 1088;
	data->cap.max_static_width = 8192;
	data->cap.max_static_height = 8192;
	data->cap.max_enhance_radius = 3;

	switch (version) {
	case 0:
		data->cap.scaling_supported = 1;
		break;
	case 1:
		data->cap.compression_noise_reduction_supported = 0;
		data->cap.sampling_noise_reduction_supported = 0;
		if (soc_is_rk3126b() || soc_is_rk3126c()) {
			data->cap.i4_deinterlace_supported = 0;
			data->cap.hsb_enhancement_supported = 0;
			data->cap.cg_enhancement_supported = 0;
		}
		break;
	case 2:
		data->cap.max_dynamic_width = 4096;
		data->cap.max_dynamic_height = 2340;
		data->cap.max_enhance_radius = 2;
		break;
	default:
		;
	}

	platform_set_drvdata(pdev, data);

	ret = misc_register(&iep_dev);
	if (ret) {
		IEP_ERR("cannot register miscdev (%d)\n", ret);
		goto err_misc_register;
	}

	data->dev = &pdev->dev;
#ifdef IEP_CLK_ENABLE
	pm_runtime_enable(data->dev);
#endif

	iep_service.iommu_dev = NULL;
	sub_np = of_parse_phandle(np, "iommus", 0);
	if (sub_np) {
		sub_dev = of_find_device_by_node(sub_np);
		iep_service.iommu_dev = &sub_dev->dev;
	}

	if (!iep_service.iommu_dev) {
		mmu_dev = rockchip_get_sysmmu_device_by_compatible(
			IEP_IOMMU_COMPATIBLE_NAME);

		if (mmu_dev) {
			platform_set_sysmmu(mmu_dev, &pdev->dev);
		}

		rockchip_iovmm_set_fault_handler(&pdev->dev,
						 iep_sysmmu_fault_handler);

		iep_service.iommu_dev = mmu_dev;
	}
	of_property_read_u32(np, "allocator", (u32 *)&iep_service.alloc_type);
	iep_power_on();
	iep_service.iommu_info = iep_iommu_info_create(data->dev,
						       iep_service.iommu_dev,
						       iep_service.alloc_type);
	iep_power_off();

	IEP_INFO("IEP Driver loaded succesfully\n");

	return 0;

err_misc_register:
	free_irq(data->irq0, pdev);
err_irq:
	if (res) {
		if (data->iep_base) {
			devm_ioremap_release(&pdev->dev, res);
		}
		devm_release_mem_region(&pdev->dev, res->start, resource_size(res));
	}
err_ioremap:
	wake_lock_destroy(&data->wake_lock);
#ifdef IEP_CLK_ENABLE
err_clock:
#endif
	return ret;
}

static int iep_drv_remove(struct platform_device *pdev)
{
	struct iep_drvdata *data = platform_get_drvdata(pdev);
	struct resource *res;

	iep_iommu_info_destroy(iep_service.iommu_info);
	iep_service.iommu_info = NULL;

	wake_lock_destroy(&data->wake_lock);

	misc_deregister(&(data->miscdev));
	free_irq(data->irq0, &data->miscdev);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	devm_ioremap_release(&pdev->dev, res);
	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));

#ifdef IEP_CLK_ENABLE
	if (data->aclk_iep)
		devm_clk_put(&pdev->dev, data->aclk_iep);

	if (data->hclk_iep)
		devm_clk_put(&pdev->dev, data->hclk_iep);

	if (data->pd_iep)
		devm_clk_put(&pdev->dev, data->pd_iep);

	pm_runtime_disable(data->dev);
#endif

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id iep_dt_ids[] = {
	{ .compatible = "rockchip,iep", },
	{ },
};
#endif

static struct platform_driver iep_driver = {
	.probe		= iep_drv_probe,
	.remove		= iep_drv_remove,
	.driver		= {
		.owner  = THIS_MODULE,
		.name	= "iep",
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(iep_dt_ids),
#endif
	},
};

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int proc_iep_show(struct seq_file *s, void *v)
{
	struct iep_status sts;
	//mutex_lock(&iep_service.mutex);
	iep_power_on();
	seq_printf(s, "\nIEP Modules Status:\n");
	sts = iep_get_status(iep_drvdata1->iep_base);
	seq_printf(s, "scl_sts: %u, dil_sts %u, wyuv_sts %u, "
		      "ryuv_sts %u, wrgb_sts %u, rrgb_sts %u, voi_sts %u\n",
		sts.scl_sts, sts.dil_sts, sts.wyuv_sts, sts.ryuv_sts,
		sts.wrgb_sts, sts.rrgb_sts, sts.voi_sts); {
		int *reg = (int *)iep_drvdata1->iep_base;
		int i;

		/* could not read validate data from address after base+0x40 */
		for (i = 0; i < 0x40; i++) {
			seq_printf(s, "%08x ", reg[i]);

			if ((i + 1) % 4 == 0)
				seq_printf(s, "\n");
		}

		seq_printf(s, "\n");
	}

	//mutex_unlock(&iep_service.mutex);

	return 0;
}

static int proc_iep_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_iep_show, NULL);
}

static const struct file_operations proc_iep_fops = {
	.open		= proc_iep_open,
	.read		= seq_read,
	.llseek 	= seq_lseek,
	.release	= single_release,
};

static int __init iep_proc_init(void)
{
	proc_create("iep", 0, NULL, &proc_iep_fops);
	return 0;
}

static void __exit iep_proc_release(void)
{
	remove_proc_entry("iep", NULL);
}
#endif

#ifdef IEP_TEST_CASE
void iep_test_case0(void);
#endif

static int __init iep_init(void)
{
	int ret;

	if ((ret = platform_driver_register(&iep_driver)) != 0) {
		IEP_ERR("Platform device register failed (%d).\n", ret);
		return ret;
	}

#ifdef CONFIG_PROC_FS
	iep_proc_init();
#endif

	IEP_INFO("Module initialized.\n");

#ifdef IEP_TEST_CASE
	iep_test_case0();
#endif

	return 0;
}

static void __exit iep_exit(void)
{
	IEP_ERR("%s IN\n", __func__);
#ifdef CONFIG_PROC_FS
	iep_proc_release();
#endif

	iep_power_off();
	platform_driver_unregister(&iep_driver);
}

module_init(iep_init);
module_exit(iep_exit);

/* Module information */
MODULE_AUTHOR("ljf@rock-chips.com");
MODULE_DESCRIPTION("Driver for iep device");
MODULE_LICENSE("GPL");

#ifdef IEP_TEST_CASE

#include "yuv420sp_480x480_interlaced.h"
#include "yuv420sp_480x480_deinterlaced_i2o1.h"

//unsigned char tmp_buf[480*480*3/2];

void iep_test_case0(void)
{
	struct IEP_MSG msg;
	iep_session session;
	unsigned int phy_src, phy_dst, phy_tmp;
	int i;
	int ret = 0;
	unsigned char *tmp_buf;

	tmp_buf = kmalloc(480 * 480 * 3 / 2, GFP_KERNEL);

	session.pid	= current->pid;
	INIT_LIST_HEAD(&session.waiting);
	INIT_LIST_HEAD(&session.ready);
	INIT_LIST_HEAD(&session.running);
	INIT_LIST_HEAD(&session.list_session);
	init_waitqueue_head(&session.wait);
	list_add_tail(&session.list_session, &iep_service.session);
	atomic_set(&session.task_running, 0);
	atomic_set(&session.num_done, 0);

	memset(&msg, 0, sizeof(struct IEP_MSG));
	memset(tmp_buf, 0xCC, 480 * 480 * 3 / 2);

	dmac_flush_range(&tmp_buf[0], &tmp_buf[480 * 480 * 3 / 2]);
	outer_flush_range(virt_to_phys(&tmp_buf[0]), virt_to_phys(&tmp_buf[480 * 480 * 3 / 2]));

	phy_src = virt_to_phys(&yuv420sp_480x480_interlaced[0]);
	phy_tmp = virt_to_phys(&tmp_buf[0]);
	phy_dst = virt_to_phys(&yuv420sp_480x480_deinterlaced_i2o1[0]);

	dmac_flush_range(&yuv420sp_480x480_interlaced[0], &yuv420sp_480x480_interlaced[480 * 480 * 3 / 2]);
	outer_flush_range(virt_to_phys(&yuv420sp_480x480_interlaced[0]), virt_to_phys(&yuv420sp_480x480_interlaced[480 * 480 * 3 / 2]));

	IEP_INFO("*********** IEP MSG GENARATE ************\n");

	msg.src.act_w = 480;
	msg.src.act_h = 480;
	msg.src.x_off = 0;
	msg.src.y_off = 0;
	msg.src.vir_w = 480;
	msg.src.vir_h = 480;
	msg.src.format = IEP_FORMAT_YCbCr_420_SP;
	msg.src.mem_addr = (uint32_t *)phy_src;
	msg.src.uv_addr  = (uint32_t *)(phy_src + 480 * 480);
	msg.src.v_addr = 0;

	msg.dst.act_w = 480;
	msg.dst.act_h = 480;
	msg.dst.x_off = 0;
	msg.dst.y_off = 0;
	msg.dst.vir_w = 480;
	msg.dst.vir_h = 480;
	msg.dst.format = IEP_FORMAT_YCbCr_420_SP;
	msg.dst.mem_addr = (uint32_t *)phy_tmp;
	msg.dst.uv_addr = (uint32_t *)(phy_tmp + 480 * 480);
	msg.dst.v_addr = 0;

	msg.dein_mode = IEP_DEINTERLACE_MODE_I2O1;
	msg.field_order = FIELD_ORDER_BOTTOM_FIRST;

	IEP_INFO("*********** IEP TEST CASE 0  ************\n");

	iep_config(&session, &msg);
	iep_try_set_reg();
	if (0 > iep_get_result_sync(&session)) {
		IEP_INFO("%s failed, timeout\n", __func__);
		ret = -ETIMEDOUT;
	}

	mdelay(10);

	dmac_flush_range(&tmp_buf[0], &tmp_buf[480 * 480 * 3 / 2]);
	outer_flush_range(virt_to_phys(&tmp_buf[0]), virt_to_phys(&tmp_buf[480 * 480 * 3 / 2]));

	IEP_INFO("*********** RESULT CHECKING  ************\n");

	for (i = 0; i < 480 * 480 * 3 / 2; i++) {
		if (tmp_buf[i] != yuv420sp_480x480_deinterlaced_i2o1[i]) {
			IEP_INFO("diff occur position %d, 0x%02x 0x%02x\n", i, tmp_buf[i], yuv420sp_480x480_deinterlaced_i2o1[i]);

			if (i > 10) {
				iep_dump();
				break;
			}
		}
	}

	if (i == 480 * 480 * 3 / 2)
		IEP_INFO("IEP pass the checking\n");
}

#endif
