/**
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: chenhengming chm@rock-chips.com
 *	   Alpha Lin, alpha.lin@rock-chips.com
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
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "vpu_iommu_ops.h"
#include "mpp_dev_common.h"
#include "mpp_service.h"

int mpp_dev_debug;
module_param(mpp_dev_debug, int, 0644);
MODULE_PARM_DESC(mpp_dev_debug, "bit switch for mpp_dev debug information");

static int mpp_bufid_to_iova(struct rockchip_mpp_dev *mpp, const u8 *tbl,
			     int size, u32 *reg, struct mpp_ctx *ctx)
{
	int hdl;
	int ret = 0;
	struct mpp_mem_region *mem_region, *n;
	int i;
	int offset = 0;
	int retval = 0;

	if (!tbl || size <= 0) {
		mpp_err("input arguments invalidate, table %p, size %d\n",
			tbl, size);
		return -1;
	}

	for (i = 0; i < size; i++) {
		int usr_fd = reg[tbl[i]] & 0x3FF;

		mpp_debug(DEBUG_IOMMU, "reg[%03d] fd = %d\n", tbl[i], usr_fd);

		/* if userspace do not set the fd at this register, skip */
		if (usr_fd == 0)
			continue;

		offset = reg[tbl[i]] >> 10;

		mpp_debug(DEBUG_IOMMU, "pos %3d fd %3d offset %10d\n",
			  tbl[i], usr_fd, offset);

		hdl = vpu_iommu_import(mpp->iommu_info, ctx->session, usr_fd);
		if (hdl < 0) {
			mpp_err("import dma-buf from fd %d failed, reg[%d]\n",
				usr_fd, tbl[i]);
			retval = hdl;
			goto fail;
		}

		mem_region = kzalloc(sizeof(*mem_region), GFP_KERNEL);

		if (!mem_region) {
			vpu_iommu_free(mpp->iommu_info, ctx->session, hdl);
			retval = -1;
			goto fail;
		}

		mem_region->hdl = hdl;
		mem_region->reg_idx = tbl[i];

		ret = vpu_iommu_map_iommu(mpp->iommu_info, ctx->session,
					  hdl, (void *)&mem_region->iova,
					  &mem_region->len);

		if (ret < 0) {
			mpp_err("reg %d fd %d ion map iommu failed\n",
				tbl[i], usr_fd);
			kfree(mem_region);
			vpu_iommu_free(mpp->iommu_info, ctx->session, hdl);
			retval = -1;
			goto fail;
		}

		reg[tbl[i]] = mem_region->iova + offset;
		INIT_LIST_HEAD(&mem_region->reg_lnk);
		list_add_tail(&mem_region->reg_lnk, &ctx->mem_region_list);
	}

	return 0;

fail:
	list_for_each_entry_safe(mem_region, n,
				 &ctx->mem_region_list, reg_lnk) {
		vpu_iommu_free(mpp->iommu_info, ctx->session, mem_region->hdl);
		list_del_init(&mem_region->reg_lnk);
		kfree(mem_region);
	}

	return retval;
}

int mpp_reg_address_translate(struct rockchip_mpp_dev *mpp,
			      u32 *reg,
			      struct mpp_ctx *ctx,
			      int idx)
{
	struct mpp_trans_info *trans_info = mpp->variant->trans_info;
	const u8 *tbl = trans_info[idx].table;
	int size = trans_info[idx].count;

	return mpp_bufid_to_iova(mpp, tbl, size, reg, ctx);
}

void mpp_translate_extra_info(struct mpp_ctx *ctx,
			      struct extra_info_for_iommu *ext_inf,
			      u32 *reg)
{
	if (ext_inf) {
		int i;

		for (i = 0; i < ext_inf->cnt; i++) {
			mpp_debug(DEBUG_IOMMU, "reg[%d] + offset %d\n",
				  ext_inf->elem[i].index,
				  ext_inf->elem[i].offset);
			reg[ext_inf->elem[i].index] += ext_inf->elem[i].offset;
		}
	}
}

void mpp_dump_reg(void __iomem *regs, int count)
{
	int i;

	pr_info("dumping registers:");

	for (i = 0; i < count; i++)
		pr_info("reg[%02d]: %08x\n", i, readl_relaxed(regs + i * 4));
}

void mpp_dump_reg_mem(u32 *regs, int count)
{
	int i;

	pr_info("Dumping mpp_service registers:\n");

	for (i = 0; i < count; i++)
		pr_info("reg[%03d]: %08x\n", i, regs[i]);
}

int mpp_dev_common_ctx_init(struct rockchip_mpp_dev *mpp, struct mpp_ctx *cfg)
{
	INIT_LIST_HEAD(&cfg->session_link);
	INIT_LIST_HEAD(&cfg->status_link);
	INIT_LIST_HEAD(&cfg->mem_region_list);

	return 0;
}

struct mpp_request {
	u32 *req;
	u32 size;
};

#ifdef CONFIG_COMPAT
struct compat_mpp_request {
	compat_uptr_t req;
	u32 size;
};
#endif

#define MPP_TIMEOUT_DELAY		(2 * HZ)
#define MPP_POWER_OFF_DELAY		(4 * HZ)

static void mpp_dev_session_clear(struct rockchip_mpp_dev *mpp,
				  struct mpp_session *session)
{
	struct mpp_ctx *ctx, *n;

	list_for_each_entry_safe(ctx, n, &session->done, session_link) {
		mpp_dev_common_ctx_deinit(mpp, ctx);
	}
}

static struct mpp_ctx *ctx_init(struct rockchip_mpp_dev *mpp,
				struct mpp_session *session,
				void __user *src, u32 size)
{
	struct mpp_ctx *ctx;

	mpp_debug_enter();

	if (mpp->ops->init)
		ctx = mpp->ops->init(mpp, session, src, size);
	else
		ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return NULL;

	ctx->session = session;
	ctx->mpp = mpp;

	mpp_srv_pending_locked(mpp->srv, ctx);

	mpp_debug_leave();

	return ctx;
}

void mpp_dev_common_ctx_deinit(struct rockchip_mpp_dev *mpp,
			       struct mpp_ctx *ctx)
{
	struct mpp_mem_region *mem_region = NULL, *n;

	list_del_init(&ctx->session_link);
	list_del_init(&ctx->status_link);

	/* release memory region attach to this registers table. */
	list_for_each_entry_safe(mem_region, n,
				 &ctx->mem_region_list, reg_lnk) {
		vpu_iommu_unmap_iommu(mpp->iommu_info, ctx->session,
				      mem_region->hdl);
		vpu_iommu_free(mpp->iommu_info, ctx->session, mem_region->hdl);
		list_del_init(&mem_region->reg_lnk);
		kfree(mem_region);
	}

	kfree(ctx);
}

static inline void mpp_queue_power_off_work(struct rockchip_mpp_dev *mpp)
{
	queue_delayed_work(system_wq, &mpp->power_off_work,
			   MPP_POWER_OFF_DELAY);
}

static void mpp_power_off_work(struct work_struct *work_s)
{
	struct delayed_work *dlwork = container_of(work_s,
						   struct delayed_work, work);
	struct rockchip_mpp_dev *mpp =
				       container_of(dlwork,
						    struct rockchip_mpp_dev,
						    power_off_work);

	if (mutex_trylock(&mpp->srv->lock)) {
		mpp_dev_power_off(mpp);
		mutex_unlock(&mpp->srv->lock);
	} else {
		/* Come back later if the device is busy... */
		mpp_queue_power_off_work(mpp);
	}
}

static void mpp_dev_reset(struct rockchip_mpp_dev *mpp)
{
	mpp_debug_enter();

	atomic_set(&mpp->reset_request, 0);

	mpp->variant->reset(mpp);

	if (!mpp->iommu_enable)
		return;

	if (test_bit(MMU_ACTIVATED, &mpp->state)) {
		if (atomic_read(&mpp->enabled))
			vpu_iommu_detach(mpp->iommu_info);
		else
			WARN_ON(!atomic_read(&mpp->enabled));

		vpu_iommu_attach(mpp->iommu_info);
	}

	mpp_debug_leave();
}

void mpp_dev_power_on(struct rockchip_mpp_dev *mpp)
{
	int ret;
	ktime_t now = ktime_get();

	if (ktime_to_ns(ktime_sub(now, mpp->last)) > NSEC_PER_SEC) {
		cancel_delayed_work_sync(&mpp->power_off_work);
		mpp_queue_power_off_work(mpp);
		mpp->last = now;
	}
	ret = atomic_add_unless(&mpp->enabled, 1, 1);
	if (!ret)
		return;

	pr_info("%s: power on\n", dev_name(mpp->dev));

	mpp->variant->power_on(mpp);
	if (mpp->iommu_enable) {
		set_bit(MMU_ACTIVATED, &mpp->state);
		vpu_iommu_attach(mpp->iommu_info);
	}
	atomic_add(1, &mpp->power_on_cnt);
	wake_lock(&mpp->wake_lock);
}

void mpp_dev_power_off(struct rockchip_mpp_dev *mpp)
{
	int total_running;
	int ret = atomic_add_unless(&mpp->enabled, -1, 0);

	if (!ret)
		return;

	total_running = atomic_read(&mpp->total_running);
	if (total_running) {
		pr_alert("alert: power off when %d task running!!\n",
			 total_running);
		mdelay(50);
		pr_alert("alert: delay 50 ms for running task\n");
	}

	pr_info("%s: power off...", dev_name(mpp->dev));

	if (mpp->iommu_enable) {
		clear_bit(MMU_ACTIVATED, &mpp->state);
		vpu_iommu_detach(mpp->iommu_info);
	}
	mpp->variant->power_off(mpp);

	atomic_add(1, &mpp->power_off_cnt);
	wake_unlock(&mpp->wake_lock);
	pr_info("done\n");
}

bool mpp_dev_is_power_on(struct rockchip_mpp_dev *mpp)
{
	return !!atomic_read(&mpp->enabled);
}

static void rockchip_mpp_run(struct rockchip_mpp_dev *mpp)
{
	struct mpp_ctx *ctx;

	mpp_debug_enter();

	mpp_srv_run(mpp->srv);

	ctx = mpp_srv_get_last_running_ctx(mpp->srv);
	mpp_time_record(ctx);

	mpp_dev_power_on(mpp);

	mpp_debug(DEBUG_TASK_INFO, "pid %d, start hw %s\n",
		  ctx->session->pid, dev_name(mpp->dev));

	if (atomic_read(&mpp->reset_request))
		mpp_dev_reset(mpp);

	if (unlikely(mpp_dev_debug & DEBUG_REGISTER))
		mpp_dump_reg(mpp->reg_base, mpp->variant->reg_len);

	atomic_add(1, &mpp->total_running);
	if (mpp->ops->run)
		mpp->ops->run(mpp);

	mpp_debug_leave();
}

static void rockchip_mpp_try_run(struct rockchip_mpp_dev *mpp)
{
	int ret = 0;
	struct rockchip_mpp_dev *pending;
	struct mpp_ctx *ctx;

	mpp_debug_enter();

	if (!mpp_srv_pending_is_empty(mpp->srv)) {
		/*
		 * if prepare func in hw driver define, state will be determined
		 * by hw driver prepare func, or state will be determined by
		 * service. ret = 0, run ready ctx.
		 */
		ctx = mpp_srv_get_pending_ctx(mpp->srv);
		pending = ctx->mpp;
		if (mpp->ops->prepare)
			ret = mpp->ops->prepare(pending);
		else if (mpp_srv_is_running(mpp->srv))
			ret = -1;

		if (ret == 0)
			rockchip_mpp_run(pending);
	}

	mpp_debug_leave();
}

static int rockchip_mpp_result(struct rockchip_mpp_dev *mpp,
			       struct mpp_ctx *ctx, u32 __user *dst)
{
	mpp_debug_enter();

	if (mpp->ops->result)
		mpp->ops->result(mpp, ctx, dst);

	mpp_dev_common_ctx_deinit(mpp, ctx);

	mpp_debug_leave();
	return 0;
}

static int mpp_dev_wait_result(struct mpp_session *session,
			       struct rockchip_mpp_dev *mpp,
			       u32 __user *req)
{
	struct mpp_ctx *ctx;
	int ret;

	ret = wait_event_timeout(session->wait,
				 !list_empty(&session->done),
				 MPP_TIMEOUT_DELAY);

	if (!list_empty(&session->done)) {
		if (ret < 0)
			mpp_err("warning: pid %d wait task error ret %d\n",
				session->pid, ret);
		ret = 0;
	} else {
		if (unlikely(ret < 0)) {
			mpp_err("error: pid %d wait task ret %d\n",
				session->pid, ret);
		} else if (ret == 0) {
			mpp_err("error: pid %d wait %d task done timeout\n",
				session->pid,
				atomic_read(&session->task_running));
			ret = -ETIMEDOUT;

			mpp_dump_reg(mpp->reg_base, mpp->variant->reg_len);
		}
	}

	if (ret < 0) {
		mpp_srv_lock(mpp->srv);
		atomic_sub(1, &mpp->total_running);

		if (mpp->variant->reset)
			mpp->variant->reset(mpp);
		mpp_srv_unlock(mpp->srv);
		return ret;
	}

	mpp_srv_lock(mpp->srv);
	ctx = mpp_srv_get_done_ctx(session);
	rockchip_mpp_result(mpp, ctx, req);
	mpp_srv_unlock(mpp->srv);

	return 0;
}

static long mpp_dev_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	struct rockchip_mpp_dev *mpp =
			container_of(filp->f_path.dentry->d_inode->i_cdev,
				     struct rockchip_mpp_dev,
				     cdev);
	struct mpp_session *session = (struct mpp_session *)filp->private_data;

	mpp_debug_enter();
	if (!session)
		return -EINVAL;

	switch (cmd) {
	case MPP_IOC_SET_CLIENT_TYPE:
		break;
	case MPP_IOC_SET_REG:
		{
			struct mpp_request req;
			struct mpp_ctx *ctx;

			mpp_debug(DEBUG_IOCTL, "pid %d set reg\n",
				  session->pid);
			if (copy_from_user(&req, (void __user *)arg,
					   sizeof(struct mpp_request))) {
				mpp_err("error: set reg copy_from_user failed\n");
				return -EFAULT;
			}
			ctx = ctx_init(mpp, session, (void __user *)req.req,
				       req.size);
			if (!ctx)
				return -EFAULT;

			mpp_srv_lock(mpp->srv);
			rockchip_mpp_try_run(mpp);
			mpp_srv_unlock(mpp->srv);
		}
		break;
	case MPP_IOC_GET_REG:
		{
			struct mpp_request req;

			mpp_debug(DEBUG_IOCTL, "pid %d get reg\n",
				  session->pid);
			if (copy_from_user(&req, (void __user *)arg,
					   sizeof(struct mpp_request))) {
				mpp_err("error: get reg copy_from_user failed\n");
				return -EFAULT;
			}

			return mpp_dev_wait_result(session, mpp, req.req);
		}
		break;
	case MPP_IOC_PROBE_IOMMU_STATUS:
		{
			int iommu_enable = 1;

			mpp_debug(DEBUG_IOCTL, "VPU_IOC_PROBE_IOMMU_STATUS\n");

			if (copy_to_user((void __user *)arg,
					 &iommu_enable, sizeof(int))) {
				mpp_err("error: iommu status copy_to_user failed\n");
				return -EFAULT;
			}
		}
		break;
	default:
		{
			if (mpp->ops->ioctl)
				return mpp->ops->ioctl(session, cmd, arg);

			mpp_err("unknown mpp ioctl cmd %x\n", cmd);
		}
		break;
	}

	mpp_debug_leave();
	return 0;
}

#ifdef CONFIG_COMPAT
static long compat_mpp_dev_ioctl(struct file *filp, unsigned int cmd,
				 unsigned long arg)
{
	struct rockchip_mpp_dev *mpp =
			container_of(filp->f_path.dentry->d_inode->i_cdev,
				     struct rockchip_mpp_dev, cdev);
	struct mpp_session *session = (struct mpp_session *)filp->private_data;

	mpp_debug_enter();

	if (!session)
		return -EINVAL;

	switch (cmd) {
	case MPP_IOC_SET_CLIENT_TYPE:
		break;
	case MPP_IOC_SET_REG:
		{
			struct compat_mpp_request req;
			struct mpp_ctx *ctx;

			mpp_debug(DEBUG_IOCTL, "compat set reg\n");
			if (copy_from_user(&req, compat_ptr((compat_uptr_t)arg),
					   sizeof(struct compat_mpp_request))) {
				mpp_err("compat set_reg copy_from_user failed\n");
				return -EFAULT;
			}
			ctx = ctx_init(mpp, session,
				       compat_ptr((compat_uptr_t)req.req),
				       req.size);
			if (!ctx)
				return -EFAULT;

			mpp_srv_lock(mpp->srv);
			rockchip_mpp_try_run(mpp);
			mpp_srv_unlock(mpp->srv);
		}
		break;
	case MPP_IOC_GET_REG:
		{
			struct compat_mpp_request req;

			mpp_debug(DEBUG_IOCTL, "compat get reg\n");
			if (copy_from_user(&req, compat_ptr((compat_uptr_t)arg),
					   sizeof(struct compat_mpp_request))) {
				mpp_err("compat get reg copy_from_user failed\n");
				return -EFAULT;
			}

			return mpp_dev_wait_result(session,
						   mpp,
						   compat_ptr((compat_uptr_t)req.req));
		}
		break;
	case MPP_IOC_PROBE_IOMMU_STATUS:
		{
			int iommu_enable = 1;

			mpp_debug(DEBUG_IOCTL, "COMPAT_VPU_IOC_PROBE_IOMMU_STATUS\n");

			if (copy_to_user(compat_ptr((compat_uptr_t)arg),
					 &iommu_enable, sizeof(int))) {
				mpp_err("error: VPU_IOC_PROBE_IOMMU_STATUS failed\n");
				return -EFAULT;
			}
		}
		break;
	default:
		{
			if (mpp->ops->ioctl)
				return mpp->ops->ioctl(session, cmd, arg);

			mpp_err("unknown mpp ioctl cmd %x\n", cmd);
		}
		break;
	}
	mpp_debug_leave();
	return 0;
}
#endif

static int mpp_dev_open(struct inode *inode, struct file *filp)
{
	struct rockchip_mpp_dev *mpp =
				       container_of(inode->i_cdev,
						    struct rockchip_mpp_dev,
						    cdev);
	struct mpp_session *session;

	mpp_debug_enter();

	if (mpp->ops->open)
		session = mpp->ops->open(mpp);
	else
		session = kzalloc(sizeof(*session), GFP_KERNEL);

	if (!session)
		return -ENOMEM;

	session->pid = current->pid;
	session->mpp = mpp;
	INIT_LIST_HEAD(&session->done);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	atomic_set(&session->task_running, 0);
	mpp_srv_lock(mpp->srv);
	list_add_tail(&session->list_session, &mpp->srv->session);
	filp->private_data = (void *)session;
	mpp_srv_unlock(mpp->srv);

	mpp_debug_leave();

	return nonseekable_open(inode, filp);
}

static int mpp_dev_release(struct inode *inode, struct file *filp)
{
	struct rockchip_mpp_dev *mpp = container_of(
						    inode->i_cdev,
						    struct rockchip_mpp_dev,
						    cdev);
	int task_running;
	struct mpp_session *session = filp->private_data;

	mpp_debug_enter();
	if (!session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);
	if (task_running) {
		pr_err("session %d still has %d task running when closing\n",
		       session->pid, task_running);
		msleep(50);
	}
	wake_up(&session->wait);

	if (mpp->ops->release)
		mpp->ops->release(session);
	mpp_srv_lock(mpp->srv);
	/* remove this filp from the asynchronusly notified filp's */
	list_del_init(&session->list_session);
	mpp_dev_session_clear(mpp, session);
	vpu_iommu_clear(mpp->iommu_info, session);
	filp->private_data = NULL;
	mpp_srv_unlock(mpp->srv);
	if (mpp->ops->free)
		mpp->ops->free(session);
	else
		kfree(session);

	pr_debug("dev closed\n");
	mpp_debug_leave();
	return 0;
}

static const struct file_operations mpp_dev_fops = {
	.unlocked_ioctl = mpp_dev_ioctl,
	.open		= mpp_dev_open,
	.release	= mpp_dev_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = compat_mpp_dev_ioctl,
#endif
};

static irqreturn_t mpp_irq(int irq, void *dev_id)
{
	struct rockchip_mpp_dev *mpp = dev_id;

	int ret = -1;

	if (mpp->ops->irq)
		ret = mpp->ops->irq(mpp);

	if (ret < 0)
		return IRQ_NONE;
	else
		return IRQ_WAKE_THREAD;
}

static irqreturn_t mpp_isr(int irq, void *dev_id)
{
	struct rockchip_mpp_dev *mpp = dev_id;
	struct mpp_ctx *ctx;
	int ret = 0;

	ctx = mpp_srv_get_current_ctx(mpp->srv);
	if (IS_ERR_OR_NULL(ctx)) {
		mpp_err("no current context present\n");
		return IRQ_HANDLED;
	}

	mpp_time_diff(ctx);
	mpp_srv_lock(mpp->srv);

	if (mpp->ops->done)
		ret = mpp->ops->done(mpp);

	if (ret == 0)
		mpp_srv_done(mpp->srv);

	atomic_sub(1, &mpp->total_running);
	rockchip_mpp_try_run(mpp);

	mpp_srv_unlock(mpp->srv);

	return IRQ_HANDLED;
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

static int mpp_sysmmu_fault_hdl(struct device *dev,
				enum rk_iommu_inttype itype,
				unsigned long pgtable_base,
				unsigned long fault_addr, unsigned int status)
{
	struct platform_device *pdev;
	struct rockchip_mpp_dev *mpp;
	struct mpp_ctx *ctx;

	mpp_debug_enter();

	if (!dev) {
		mpp_err("invalid NULL dev\n");
		return 0;
	}

	pdev = container_of(dev, struct platform_device, dev);
	if (!pdev) {
		mpp_err("invalid NULL platform_device\n");
		return 0;
	}

	mpp = platform_get_drvdata(pdev);
	if (!mpp || !mpp->srv) {
		mpp_err("invalid mpp_dev or mpp_srv\n");
		return 0;
	}

	ctx = mpp_srv_get_current_ctx(mpp->srv);
	if (ctx) {
		struct mpp_mem_region *mem, *n;
		int i = 0;

		mpp_err("mpp, fault addr 0x%08lx\n", fault_addr);
		if (!list_empty(&ctx->mem_region_list)) {
			list_for_each_entry_safe(mem, n, &ctx->mem_region_list,
						 reg_lnk) {
				mpp_err("mpp, reg[%02u] mem[%02d] 0x%lx %lx\n",
					mem->reg_idx, i, mem->iova, mem->len);
				i++;
			}
		} else {
			mpp_err("no memory region mapped\n");
		}

		if (ctx->mpp) {
			struct rockchip_mpp_dev *mpp = ctx->mpp;

			mpp_err("current errror register set:\n");
			mpp_dump_reg(mpp->reg_base, mpp->variant->reg_len);
		}

		if (mpp->variant->reset)
			mpp->variant->reset(mpp);
	}

	mpp_debug_leave();

	return 0;
}

static struct device *rockchip_get_sysmmu_dev(const char *compt)
{
	struct device_node *dn = NULL;
	struct platform_device *pd = NULL;
	struct device *ret = NULL;

	dn = of_find_compatible_node(NULL, NULL, compt);
	if (!dn) {
		pr_err("can't find device node %s \r\n", compt);
		return NULL;
	}

	pd = of_find_device_by_node(dn);
	if (!pd) {
		pr_err("can't find platform device in device node %s\n", compt);
		return  NULL;
	}
	ret = &pd->dev;

	return ret;
}

#if defined(CONFIG_OF)
static const struct of_device_id mpp_dev_dt_ids[] = {
	{ .compatible = "rockchip,rkvenc", .data = &rkvenc_variant, },
	{ .compatible = "rockchip,vepu", .data = &vepu_variant, },
	{ .compatible = "rockchip,h265e", .data = &h265e_variant, },
	{ },
};
#endif

static int mpp_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	char *name = (char *)dev_name(dev);
	struct device_node *np = pdev->dev.of_node;
	struct rockchip_mpp_dev *mpp = NULL;
	const struct of_device_id *match;
	const struct rockchip_mpp_dev_variant *variant;
	struct device_node *srv_np, *mmu_np;
	struct platform_device *srv_pdev;
	struct resource *res = NULL;
	struct mpp_session *session;
	int allocator_type;

	pr_info("probe device %s\n", dev_name(dev));

	match = of_match_node(mpp_dev_dt_ids, dev->of_node);
	variant = match->data;

	mpp = devm_kzalloc(dev, variant->data_len, GFP_KERNEL);

	/* Get service */
	srv_np = of_parse_phandle(np, "rockchip,srv", 0);
	srv_pdev = of_find_device_by_node(srv_np);

	mpp->srv = platform_get_drvdata(srv_pdev);

	mpp->dev = dev;
	mpp->state = 0;
	mpp->variant = variant;

	wake_lock_init(&mpp->wake_lock, WAKE_LOCK_SUSPEND, "mpp");
	atomic_set(&mpp->enabled, 0);
	atomic_set(&mpp->power_on_cnt, 0);
	atomic_set(&mpp->power_off_cnt, 0);
	atomic_set(&mpp->total_running, 0);
	atomic_set(&mpp->reset_request, 0);

	INIT_DELAYED_WORK(&mpp->power_off_work, mpp_power_off_work);
	mpp->last.tv64 = 0;

	of_property_read_string(np, "name", (const char **)&name);
	of_property_read_u32(np, "iommu_enabled", &mpp->iommu_enable);

	if (mpp->srv->reg_base == 0) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		mpp->reg_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(mpp->reg_base)) {
			ret = PTR_ERR(mpp->reg_base);
			goto err;
		}
	} else {
		mpp->reg_base = mpp->srv->reg_base;
	}

	mpp->irq = platform_get_irq(pdev, 0);
	if (mpp->irq > 0) {
		ret = devm_request_threaded_irq(dev, mpp->irq,
						mpp_irq, mpp_isr,
						IRQF_SHARED, dev_name(dev),
						(void *)mpp);
		if (ret) {
			dev_err(dev, "error: can't request vepu irq %d\n",
				mpp->irq);
			goto err;
		}
	} else {
		dev_info(dev, "No interrupt resource found\n");
	}

	mmu_np = of_parse_phandle(np, "iommus", 0);
	if (mmu_np) {
		struct platform_device *pd = NULL;

		pd = of_find_device_by_node(mmu_np);
		mpp->mmu_dev = &pd->dev;
		if (!mpp->mmu_dev) {
			mpp->iommu_enable = false;
			dev_err(dev, "get iommu dev failed");
		}
	} else {
		mpp->mmu_dev =
			rockchip_get_sysmmu_dev(mpp->variant->mmu_dev_dts_name);
		if (mpp->mmu_dev) {
			platform_set_sysmmu(mpp->mmu_dev, dev);
			rockchip_iovmm_set_fault_handler(dev,
							 mpp_sysmmu_fault_hdl);
		} else {
			dev_err(dev,
				"get iommu dev %s failed, set iommu_enable to false\n",
				mpp->variant->mmu_dev_dts_name);
			mpp->iommu_enable = false;
		}
	}

	dev_info(dev, "try to get iommu dev %p\n",
		 mpp->mmu_dev);

	of_property_read_u32(np, "allocator", &allocator_type);
	mpp->iommu_info = vpu_iommu_info_create(dev, mpp->mmu_dev,
						allocator_type);
	if (IS_ERR(mpp->iommu_info)) {
		dev_err(dev, "failed to create ion client for mpp ret %ld\n",
			PTR_ERR(mpp->iommu_info));
	}

	/*
	 * this session is global session, each dev
	 * only has one global session, and will be
	 * release when dev remove
	 */
	session = devm_kzalloc(dev, sizeof(*session), GFP_KERNEL);

	if (!session)
		return -ENOMEM;

	session->mpp = mpp;
	INIT_LIST_HEAD(&session->done);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	atomic_set(&session->task_running, 0);
	/* this first session of each dev is global session */
	list_add_tail(&session->list_session, &mpp->srv->session);

	ret = mpp->variant->hw_probe(mpp);
	if (ret)
		goto err;

	dev_info(dev, "resource ready, register device\n");
	/* create device node */
	ret = alloc_chrdev_region(&mpp->dev_t, 0, 1, name);
	if (ret) {
		dev_err(dev, "alloc dev_t failed\n");
		goto err;
	}

	cdev_init(&mpp->cdev, &mpp_dev_fops);

	mpp->cdev.owner = THIS_MODULE;
	mpp->cdev.ops = &mpp_dev_fops;

	ret = cdev_add(&mpp->cdev, mpp->dev_t, 1);
	if (ret) {
		unregister_chrdev_region(mpp->dev_t, 1);
		dev_err(dev, "add dev_t failed\n");
		goto err;
	}

	mpp->child_dev = device_create(mpp->srv->cls, dev,
				       mpp->dev_t, NULL, name);

	mpp_srv_attach(mpp->srv, &mpp->lnk_service);

	platform_set_drvdata(pdev, mpp);

	return 0;
err:
	wake_lock_destroy(&mpp->wake_lock);
	return ret;
}

static int mpp_dev_remove(struct platform_device *pdev)
{
	struct rockchip_mpp_dev *mpp = platform_get_drvdata(pdev);
	struct mpp_session *session = list_first_entry(&mpp->srv->session,
						       struct mpp_session,
						       list_session);

	mpp->variant->hw_remove(mpp);

	vpu_iommu_clear(mpp->iommu_info, session);
	vpu_iommu_destroy(mpp->iommu_info);
	kfree(session);

	mpp_srv_lock(mpp->srv);
	cancel_delayed_work_sync(&mpp->power_off_work);
	mpp_dev_power_off(mpp);
	mpp_srv_detach(mpp->srv, &mpp->lnk_service);
	mpp_srv_unlock(mpp->srv);

	device_destroy(mpp->srv->cls, mpp->dev_t);
	cdev_del(&mpp->cdev);
	unregister_chrdev_region(mpp->dev_t, 1);

	return 0;
}

static struct platform_driver mpp_dev_driver = {
	.probe = mpp_dev_probe,
	.remove = mpp_dev_remove,
	.driver = {
		.name = "mpp_dev",
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(mpp_dev_dt_ids),
#endif
	},
};

static int __init mpp_dev_init(void)
{
	int ret = platform_driver_register(&mpp_dev_driver);

	if (ret) {
		mpp_err("Platform device register failed (%d).\n", ret);
		return ret;
	}

	return ret;
}

static void __exit mpp_dev_exit(void)
{
	platform_driver_unregister(&mpp_dev_driver);
}

module_init(mpp_dev_init);
module_exit(mpp_dev_exit);

MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.build.201610121711");
MODULE_AUTHOR("Alpha Lin alpha.lin@rock-chips.com");
MODULE_DESCRIPTION("Rockchip mpp device driver");
