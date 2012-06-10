/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Authors: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundationr
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "drmP.h"
#include "exynos_drm.h"
#include "exynos_drm_drv.h"
#include "exynos_drm_gem.h"

#define G2D_HW_MAJOR_VER		4
#define G2D_HW_MINOR_VER		1

/* vaild register range set from user: 0x0104 ~ 0x0880 */
#define G2D_VALID_START			0x0104
#define G2D_VALID_END			0x0880

/* general registers */
#define G2D_SOFT_RESET			0x0000
#define G2D_INTEN			0x0004
#define G2D_INTC_PEND			0x000C
#define G2D_DMA_SFR_BASE_ADDR		0x0080
#define G2D_DMA_COMMAND			0x0084
#define G2D_DMA_STATUS			0x008C
#define G2D_DMA_HOLD_CMD		0x0090

/* command registers */
#define G2D_BITBLT_START		0x0100

/* registers for base address */
#define G2D_SRC_BASE_ADDR		0x0304
#define G2D_SRC_PLANE2_BASE_ADDR	0x0318
#define G2D_DST_BASE_ADDR		0x0404
#define G2D_DST_PLANE2_BASE_ADDR	0x0418
#define G2D_PAT_BASE_ADDR		0x0500
#define G2D_MSK_BASE_ADDR		0x0520

/* G2D_SOFT_RESET */
#define G2D_SFRCLEAR			(1 << 1)
#define G2D_R				(1 << 0)

/* G2D_INTEN */
#define G2D_INTEN_ACF			(1 << 3)
#define G2D_INTEN_UCF			(1 << 2)
#define G2D_INTEN_GCF			(1 << 1)
#define G2D_INTEN_SCF			(1 << 0)

/* G2D_INTC_PEND */
#define G2D_INTP_ACMD_FIN		(1 << 3)
#define G2D_INTP_UCMD_FIN		(1 << 2)
#define G2D_INTP_GCMD_FIN		(1 << 1)
#define G2D_INTP_SCMD_FIN		(1 << 0)

/* G2D_DMA_COMMAND */
#define G2D_DMA_HALT			(1 << 2)
#define G2D_DMA_CONTINUE		(1 << 1)
#define G2D_DMA_START			(1 << 0)

/* G2D_DMA_STATUS */
#define G2D_DMA_LIST_DONE_COUNT		(0xFF << 17)
#define G2D_DMA_BITBLT_DONE_COUNT	(0xFFFF << 1)
#define G2D_DMA_DONE			(1 << 0)
#define G2D_DMA_LIST_DONE_COUNT_OFFSET	17

/* G2D_DMA_HOLD_CMD */
#define G2D_USET_HOLD			(1 << 2)
#define G2D_LIST_HOLD			(1 << 1)
#define G2D_BITBLT_HOLD			(1 << 0)

/* G2D_BITBLT_START */
#define G2D_START_CASESEL		(1 << 2)
#define G2D_START_NHOLT			(1 << 1)
#define G2D_START_BITBLT		(1 << 0)

#define G2D_CMDLIST_SIZE		(PAGE_SIZE / 4)
#define G2D_CMDLIST_NUM			64
#define G2D_CMDLIST_POOL_SIZE		(G2D_CMDLIST_SIZE * G2D_CMDLIST_NUM)
#define G2D_CMDLIST_DATA_NUM		(G2D_CMDLIST_SIZE / sizeof(u32) - 2)

/* cmdlist data structure */
struct g2d_cmdlist {
	u32	head;
	u32	data[G2D_CMDLIST_DATA_NUM];
	u32	last;	/* last data offset */
};

struct drm_exynos_pending_g2d_event {
	struct drm_pending_event	base;
	struct drm_exynos_g2d_event	event;
};

struct g2d_gem_node {
	struct list_head	list;
	unsigned int		handle;
};

struct g2d_cmdlist_node {
	struct list_head	list;
	struct g2d_cmdlist	*cmdlist;
	unsigned int		gem_nr;
	dma_addr_t		dma_addr;

	struct drm_exynos_pending_g2d_event	*event;
};

struct g2d_runqueue_node {
	struct list_head	list;
	struct list_head	run_cmdlist;
	struct list_head	event_list;
	struct completion	complete;
	int			async;
};

struct g2d_data {
	struct device			*dev;
	struct clk			*gate_clk;
	struct resource			*regs_res;
	void __iomem			*regs;
	int				irq;
	struct workqueue_struct		*g2d_workq;
	struct work_struct		runqueue_work;
	struct exynos_drm_subdrv	subdrv;
	bool				suspended;

	/* cmdlist */
	struct g2d_cmdlist_node		*cmdlist_node;
	struct list_head		free_cmdlist;
	struct mutex			cmdlist_mutex;
	dma_addr_t			cmdlist_pool;
	void				*cmdlist_pool_virt;

	/* runqueue*/
	struct g2d_runqueue_node	*runqueue_node;
	struct list_head		runqueue;
	struct mutex			runqueue_mutex;
	struct kmem_cache		*runqueue_slab;
};

static int g2d_init_cmdlist(struct g2d_data *g2d)
{
	struct device *dev = g2d->dev;
	struct g2d_cmdlist_node *node = g2d->cmdlist_node;
	int nr;
	int ret;

	g2d->cmdlist_pool_virt = dma_alloc_coherent(dev, G2D_CMDLIST_POOL_SIZE,
						&g2d->cmdlist_pool, GFP_KERNEL);
	if (!g2d->cmdlist_pool_virt) {
		dev_err(dev, "failed to allocate dma memory\n");
		return -ENOMEM;
	}

	node = kcalloc(G2D_CMDLIST_NUM, G2D_CMDLIST_NUM * sizeof(*node),
			GFP_KERNEL);
	if (!node) {
		dev_err(dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto err;
	}

	for (nr = 0; nr < G2D_CMDLIST_NUM; nr++) {
		node[nr].cmdlist =
			g2d->cmdlist_pool_virt + nr * G2D_CMDLIST_SIZE;
		node[nr].dma_addr =
			g2d->cmdlist_pool + nr * G2D_CMDLIST_SIZE;

		list_add_tail(&node[nr].list, &g2d->free_cmdlist);
	}

	return 0;

err:
	dma_free_coherent(dev, G2D_CMDLIST_POOL_SIZE, g2d->cmdlist_pool_virt,
			g2d->cmdlist_pool);
	return ret;
}

static void g2d_fini_cmdlist(struct g2d_data *g2d)
{
	struct device *dev = g2d->dev;

	kfree(g2d->cmdlist_node);
	dma_free_coherent(dev, G2D_CMDLIST_POOL_SIZE, g2d->cmdlist_pool_virt,
			g2d->cmdlist_pool);
}

static struct g2d_cmdlist_node *g2d_get_cmdlist(struct g2d_data *g2d)
{
	struct device *dev = g2d->dev;
	struct g2d_cmdlist_node *node;

	mutex_lock(&g2d->cmdlist_mutex);
	if (list_empty(&g2d->free_cmdlist)) {
		dev_err(dev, "there is no free cmdlist\n");
		mutex_unlock(&g2d->cmdlist_mutex);
		return NULL;
	}

	node = list_first_entry(&g2d->free_cmdlist, struct g2d_cmdlist_node,
				list);
	list_del_init(&node->list);
	mutex_unlock(&g2d->cmdlist_mutex);

	return node;
}

static void g2d_put_cmdlist(struct g2d_data *g2d, struct g2d_cmdlist_node *node)
{
	mutex_lock(&g2d->cmdlist_mutex);
	list_move_tail(&node->list, &g2d->free_cmdlist);
	mutex_unlock(&g2d->cmdlist_mutex);
}

static void g2d_add_cmdlist_to_inuse(struct exynos_drm_g2d_private *g2d_priv,
				     struct g2d_cmdlist_node *node)
{
	struct g2d_cmdlist_node *lnode;

	if (list_empty(&g2d_priv->inuse_cmdlist))
		goto add_to_list;

	/* this links to base address of new cmdlist */
	lnode = list_entry(g2d_priv->inuse_cmdlist.prev,
				struct g2d_cmdlist_node, list);
	lnode->cmdlist->data[lnode->cmdlist->last] = node->dma_addr;

add_to_list:
	list_add_tail(&node->list, &g2d_priv->inuse_cmdlist);

	if (node->event)
		list_add_tail(&node->event->base.link, &g2d_priv->event_list);
}

static int g2d_get_cmdlist_gem(struct drm_device *drm_dev,
			       struct drm_file *file,
			       struct g2d_cmdlist_node *node)
{
	struct drm_exynos_file_private *file_priv = file->driver_priv;
	struct exynos_drm_g2d_private *g2d_priv = file_priv->g2d_priv;
	struct g2d_cmdlist *cmdlist = node->cmdlist;
	dma_addr_t *addr;
	int offset;
	int i;

	for (i = 0; i < node->gem_nr; i++) {
		struct g2d_gem_node *gem_node;

		gem_node = kzalloc(sizeof(*gem_node), GFP_KERNEL);
		if (!gem_node) {
			dev_err(g2d_priv->dev, "failed to allocate gem node\n");
			return -ENOMEM;
		}

		offset = cmdlist->last - (i * 2 + 1);
		gem_node->handle = cmdlist->data[offset];

		addr = exynos_drm_gem_get_dma_addr(drm_dev, gem_node->handle,
						   file);
		if (IS_ERR(addr)) {
			node->gem_nr = i;
			kfree(gem_node);
			return PTR_ERR(addr);
		}

		cmdlist->data[offset] = *addr;
		list_add_tail(&gem_node->list, &g2d_priv->gem_list);
		g2d_priv->gem_nr++;
	}

	return 0;
}

static void g2d_put_cmdlist_gem(struct drm_device *drm_dev,
				struct drm_file *file,
				unsigned int nr)
{
	struct drm_exynos_file_private *file_priv = file->driver_priv;
	struct exynos_drm_g2d_private *g2d_priv = file_priv->g2d_priv;
	struct g2d_gem_node *node, *n;

	list_for_each_entry_safe_reverse(node, n, &g2d_priv->gem_list, list) {
		if (!nr)
			break;

		exynos_drm_gem_put_dma_addr(drm_dev, node->handle, file);
		list_del_init(&node->list);
		kfree(node);
		nr--;
	}
}

static void g2d_dma_start(struct g2d_data *g2d,
			  struct g2d_runqueue_node *runqueue_node)
{
	struct g2d_cmdlist_node *node =
				list_first_entry(&runqueue_node->run_cmdlist,
						struct g2d_cmdlist_node, list);

	pm_runtime_get_sync(g2d->dev);
	clk_enable(g2d->gate_clk);

	/* interrupt enable */
	writel_relaxed(G2D_INTEN_ACF | G2D_INTEN_UCF | G2D_INTEN_GCF,
			g2d->regs + G2D_INTEN);

	writel_relaxed(node->dma_addr, g2d->regs + G2D_DMA_SFR_BASE_ADDR);
	writel_relaxed(G2D_DMA_START, g2d->regs + G2D_DMA_COMMAND);
}

static struct g2d_runqueue_node *g2d_get_runqueue_node(struct g2d_data *g2d)
{
	struct g2d_runqueue_node *runqueue_node;

	if (list_empty(&g2d->runqueue))
		return NULL;

	runqueue_node = list_first_entry(&g2d->runqueue,
					 struct g2d_runqueue_node, list);
	list_del_init(&runqueue_node->list);
	return runqueue_node;
}

static void g2d_free_runqueue_node(struct g2d_data *g2d,
				   struct g2d_runqueue_node *runqueue_node)
{
	if (!runqueue_node)
		return;

	mutex_lock(&g2d->cmdlist_mutex);
	list_splice_tail_init(&runqueue_node->run_cmdlist, &g2d->free_cmdlist);
	mutex_unlock(&g2d->cmdlist_mutex);

	kmem_cache_free(g2d->runqueue_slab, runqueue_node);
}

static void g2d_exec_runqueue(struct g2d_data *g2d)
{
	g2d->runqueue_node = g2d_get_runqueue_node(g2d);
	if (g2d->runqueue_node)
		g2d_dma_start(g2d, g2d->runqueue_node);
}

static void g2d_runqueue_worker(struct work_struct *work)
{
	struct g2d_data *g2d = container_of(work, struct g2d_data,
					    runqueue_work);


	mutex_lock(&g2d->runqueue_mutex);
	clk_disable(g2d->gate_clk);
	pm_runtime_put_sync(g2d->dev);

	complete(&g2d->runqueue_node->complete);
	if (g2d->runqueue_node->async)
		g2d_free_runqueue_node(g2d, g2d->runqueue_node);

	if (g2d->suspended)
		g2d->runqueue_node = NULL;
	else
		g2d_exec_runqueue(g2d);
	mutex_unlock(&g2d->runqueue_mutex);
}

static void g2d_finish_event(struct g2d_data *g2d, u32 cmdlist_no)
{
	struct drm_device *drm_dev = g2d->subdrv.drm_dev;
	struct g2d_runqueue_node *runqueue_node = g2d->runqueue_node;
	struct drm_exynos_pending_g2d_event *e;
	struct timeval now;
	unsigned long flags;

	if (list_empty(&runqueue_node->event_list))
		return;

	e = list_first_entry(&runqueue_node->event_list,
			     struct drm_exynos_pending_g2d_event, base.link);

	do_gettimeofday(&now);
	e->event.tv_sec = now.tv_sec;
	e->event.tv_usec = now.tv_usec;
	e->event.cmdlist_no = cmdlist_no;

	spin_lock_irqsave(&drm_dev->event_lock, flags);
	list_move_tail(&e->base.link, &e->base.file_priv->event_list);
	wake_up_interruptible(&e->base.file_priv->event_wait);
	spin_unlock_irqrestore(&drm_dev->event_lock, flags);
}

static irqreturn_t g2d_irq_handler(int irq, void *dev_id)
{
	struct g2d_data *g2d = dev_id;
	u32 pending;

	pending = readl_relaxed(g2d->regs + G2D_INTC_PEND);
	if (pending)
		writel_relaxed(pending, g2d->regs + G2D_INTC_PEND);

	if (pending & G2D_INTP_GCMD_FIN) {
		u32 cmdlist_no = readl_relaxed(g2d->regs + G2D_DMA_STATUS);

		cmdlist_no = (cmdlist_no & G2D_DMA_LIST_DONE_COUNT) >>
						G2D_DMA_LIST_DONE_COUNT_OFFSET;

		g2d_finish_event(g2d, cmdlist_no);

		writel_relaxed(0, g2d->regs + G2D_DMA_HOLD_CMD);
		if (!(pending & G2D_INTP_ACMD_FIN)) {
			writel_relaxed(G2D_DMA_CONTINUE,
					g2d->regs + G2D_DMA_COMMAND);
		}
	}

	if (pending & G2D_INTP_ACMD_FIN)
		queue_work(g2d->g2d_workq, &g2d->runqueue_work);

	return IRQ_HANDLED;
}

static int g2d_check_reg_offset(struct device *dev, struct g2d_cmdlist *cmdlist,
				int nr, bool for_addr)
{
	int reg_offset;
	int index;
	int i;

	for (i = 0; i < nr; i++) {
		index = cmdlist->last - 2 * (i + 1);
		reg_offset = cmdlist->data[index] & ~0xfffff000;

		if (reg_offset < G2D_VALID_START || reg_offset > G2D_VALID_END)
			goto err;
		if (reg_offset % 4)
			goto err;

		switch (reg_offset) {
		case G2D_SRC_BASE_ADDR:
		case G2D_SRC_PLANE2_BASE_ADDR:
		case G2D_DST_BASE_ADDR:
		case G2D_DST_PLANE2_BASE_ADDR:
		case G2D_PAT_BASE_ADDR:
		case G2D_MSK_BASE_ADDR:
			if (!for_addr)
				goto err;
			break;
		default:
			if (for_addr)
				goto err;
			break;
		}
	}

	return 0;

err:
	dev_err(dev, "Bad register offset: 0x%x\n", cmdlist->data[index]);
	return -EINVAL;
}

/* ioctl functions */
int exynos_g2d_get_ver_ioctl(struct drm_device *drm_dev, void *data,
			     struct drm_file *file)
{
	struct drm_exynos_g2d_get_ver *ver = data;

	ver->major = G2D_HW_MAJOR_VER;
	ver->minor = G2D_HW_MINOR_VER;

	return 0;
}
EXPORT_SYMBOL_GPL(exynos_g2d_get_ver_ioctl);

int exynos_g2d_set_cmdlist_ioctl(struct drm_device *drm_dev, void *data,
				 struct drm_file *file)
{
	struct drm_exynos_file_private *file_priv = file->driver_priv;
	struct exynos_drm_g2d_private *g2d_priv = file_priv->g2d_priv;
	struct device *dev = g2d_priv->dev;
	struct g2d_data *g2d;
	struct drm_exynos_g2d_set_cmdlist *req = data;
	struct drm_exynos_g2d_cmd *cmd;
	struct drm_exynos_pending_g2d_event *e;
	struct g2d_cmdlist_node *node;
	struct g2d_cmdlist *cmdlist;
	unsigned long flags;
	int size;
	int ret;

	if (!dev)
		return -ENODEV;

	g2d = dev_get_drvdata(dev);
	if (!g2d)
		return -EFAULT;

	node = g2d_get_cmdlist(g2d);
	if (!node)
		return -ENOMEM;

	node->event = NULL;

	if (req->event_type != G2D_EVENT_NOT) {
		spin_lock_irqsave(&drm_dev->event_lock, flags);
		if (file->event_space < sizeof(e->event)) {
			spin_unlock_irqrestore(&drm_dev->event_lock, flags);
			ret = -ENOMEM;
			goto err;
		}
		file->event_space -= sizeof(e->event);
		spin_unlock_irqrestore(&drm_dev->event_lock, flags);

		e = kzalloc(sizeof(*node->event), GFP_KERNEL);
		if (!e) {
			dev_err(dev, "failed to allocate event\n");

			spin_lock_irqsave(&drm_dev->event_lock, flags);
			file->event_space += sizeof(e->event);
			spin_unlock_irqrestore(&drm_dev->event_lock, flags);

			ret = -ENOMEM;
			goto err;
		}

		e->event.base.type = DRM_EXYNOS_G2D_EVENT;
		e->event.base.length = sizeof(e->event);
		e->event.user_data = req->user_data;
		e->base.event = &e->event.base;
		e->base.file_priv = file;
		e->base.destroy = (void (*) (struct drm_pending_event *)) kfree;

		node->event = e;
	}

	cmdlist = node->cmdlist;

	cmdlist->last = 0;

	/*
	 * If don't clear SFR registers, the cmdlist is affected by register
	 * values of previous cmdlist. G2D hw executes SFR clear command and
	 * a next command at the same time then the next command is ignored and
	 * is executed rightly from next next command, so needs a dummy command
	 * to next command of SFR clear command.
	 */
	cmdlist->data[cmdlist->last++] = G2D_SOFT_RESET;
	cmdlist->data[cmdlist->last++] = G2D_SFRCLEAR;
	cmdlist->data[cmdlist->last++] = G2D_SRC_BASE_ADDR;
	cmdlist->data[cmdlist->last++] = 0;

	if (node->event) {
		cmdlist->data[cmdlist->last++] = G2D_DMA_HOLD_CMD;
		cmdlist->data[cmdlist->last++] = G2D_LIST_HOLD;
	}

	/* Check size of cmdlist: last 2 is about G2D_BITBLT_START */
	size = cmdlist->last + req->cmd_nr * 2 + req->cmd_gem_nr * 2 + 2;
	if (size > G2D_CMDLIST_DATA_NUM) {
		dev_err(dev, "cmdlist size is too big\n");
		ret = -EINVAL;
		goto err_free_event;
	}

	cmd = (struct drm_exynos_g2d_cmd *)(uint32_t)req->cmd;

	if (copy_from_user(cmdlist->data + cmdlist->last,
				(void __user *)cmd,
				sizeof(*cmd) * req->cmd_nr)) {
		ret = -EFAULT;
		goto err_free_event;
	}
	cmdlist->last += req->cmd_nr * 2;

	ret = g2d_check_reg_offset(dev, cmdlist, req->cmd_nr, false);
	if (ret < 0)
		goto err_free_event;

	node->gem_nr = req->cmd_gem_nr;
	if (req->cmd_gem_nr) {
		struct drm_exynos_g2d_cmd *cmd_gem;

		cmd_gem = (struct drm_exynos_g2d_cmd *)(uint32_t)req->cmd_gem;

		if (copy_from_user(cmdlist->data + cmdlist->last,
					(void __user *)cmd_gem,
					sizeof(*cmd_gem) * req->cmd_gem_nr)) {
			ret = -EFAULT;
			goto err_free_event;
		}
		cmdlist->last += req->cmd_gem_nr * 2;

		ret = g2d_check_reg_offset(dev, cmdlist, req->cmd_gem_nr, true);
		if (ret < 0)
			goto err_free_event;

		ret = g2d_get_cmdlist_gem(drm_dev, file, node);
		if (ret < 0)
			goto err_unmap;
	}

	cmdlist->data[cmdlist->last++] = G2D_BITBLT_START;
	cmdlist->data[cmdlist->last++] = G2D_START_BITBLT;

	/* head */
	cmdlist->head = cmdlist->last / 2;

	/* tail */
	cmdlist->data[cmdlist->last] = 0;

	g2d_add_cmdlist_to_inuse(g2d_priv, node);

	return 0;

err_unmap:
	g2d_put_cmdlist_gem(drm_dev, file, node->gem_nr);
err_free_event:
	if (node->event) {
		spin_lock_irqsave(&drm_dev->event_lock, flags);
		file->event_space += sizeof(e->event);
		spin_unlock_irqrestore(&drm_dev->event_lock, flags);
		kfree(node->event);
	}
err:
	g2d_put_cmdlist(g2d, node);
	return ret;
}
EXPORT_SYMBOL_GPL(exynos_g2d_set_cmdlist_ioctl);

int exynos_g2d_exec_ioctl(struct drm_device *drm_dev, void *data,
			  struct drm_file *file)
{
	struct drm_exynos_file_private *file_priv = file->driver_priv;
	struct exynos_drm_g2d_private *g2d_priv = file_priv->g2d_priv;
	struct device *dev = g2d_priv->dev;
	struct g2d_data *g2d;
	struct drm_exynos_g2d_exec *req = data;
	struct g2d_runqueue_node *runqueue_node;
	struct list_head *run_cmdlist;
	struct list_head *event_list;

	if (!dev)
		return -ENODEV;

	g2d = dev_get_drvdata(dev);
	if (!g2d)
		return -EFAULT;

	runqueue_node = kmem_cache_alloc(g2d->runqueue_slab, GFP_KERNEL);
	if (!runqueue_node) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	run_cmdlist = &runqueue_node->run_cmdlist;
	event_list = &runqueue_node->event_list;
	INIT_LIST_HEAD(run_cmdlist);
	INIT_LIST_HEAD(event_list);
	init_completion(&runqueue_node->complete);
	runqueue_node->async = req->async;

	list_splice_init(&g2d_priv->inuse_cmdlist, run_cmdlist);
	list_splice_init(&g2d_priv->event_list, event_list);

	if (list_empty(run_cmdlist)) {
		dev_err(dev, "there is no inuse cmdlist\n");
		kmem_cache_free(g2d->runqueue_slab, runqueue_node);
		return -EPERM;
	}

	mutex_lock(&g2d->runqueue_mutex);
	list_add_tail(&runqueue_node->list, &g2d->runqueue);
	if (!g2d->runqueue_node)
		g2d_exec_runqueue(g2d);
	mutex_unlock(&g2d->runqueue_mutex);

	if (runqueue_node->async)
		goto out;

	wait_for_completion(&runqueue_node->complete);
	g2d_free_runqueue_node(g2d, runqueue_node);

out:
	return 0;
}
EXPORT_SYMBOL_GPL(exynos_g2d_exec_ioctl);

static int g2d_open(struct drm_device *drm_dev, struct device *dev,
			struct drm_file *file)
{
	struct drm_exynos_file_private *file_priv = file->driver_priv;
	struct exynos_drm_g2d_private *g2d_priv;

	g2d_priv = kzalloc(sizeof(*g2d_priv), GFP_KERNEL);
	if (!g2d_priv) {
		dev_err(dev, "failed to allocate g2d private data\n");
		return -ENOMEM;
	}

	g2d_priv->dev = dev;
	file_priv->g2d_priv = g2d_priv;

	INIT_LIST_HEAD(&g2d_priv->inuse_cmdlist);
	INIT_LIST_HEAD(&g2d_priv->event_list);
	INIT_LIST_HEAD(&g2d_priv->gem_list);

	return 0;
}

static void g2d_close(struct drm_device *drm_dev, struct device *dev,
			struct drm_file *file)
{
	struct drm_exynos_file_private *file_priv = file->driver_priv;
	struct exynos_drm_g2d_private *g2d_priv = file_priv->g2d_priv;
	struct g2d_data *g2d;
	struct g2d_cmdlist_node *node, *n;

	if (!dev)
		return;

	g2d = dev_get_drvdata(dev);
	if (!g2d)
		return;

	mutex_lock(&g2d->cmdlist_mutex);
	list_for_each_entry_safe(node, n, &g2d_priv->inuse_cmdlist, list)
		list_move_tail(&node->list, &g2d->free_cmdlist);
	mutex_unlock(&g2d->cmdlist_mutex);

	g2d_put_cmdlist_gem(drm_dev, file, g2d_priv->gem_nr);

	kfree(file_priv->g2d_priv);
}

static int __devinit g2d_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct g2d_data *g2d;
	struct exynos_drm_subdrv *subdrv;
	int ret;

	g2d = kzalloc(sizeof(*g2d), GFP_KERNEL);
	if (!g2d) {
		dev_err(dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	g2d->runqueue_slab = kmem_cache_create("g2d_runqueue_slab",
			sizeof(struct g2d_runqueue_node), 0, 0, NULL);
	if (!g2d->runqueue_slab) {
		ret = -ENOMEM;
		goto err_free_mem;
	}

	g2d->dev = dev;

	g2d->g2d_workq = create_singlethread_workqueue("g2d");
	if (!g2d->g2d_workq) {
		dev_err(dev, "failed to create workqueue\n");
		ret = -EINVAL;
		goto err_destroy_slab;
	}

	INIT_WORK(&g2d->runqueue_work, g2d_runqueue_worker);
	INIT_LIST_HEAD(&g2d->free_cmdlist);
	INIT_LIST_HEAD(&g2d->runqueue);

	mutex_init(&g2d->cmdlist_mutex);
	mutex_init(&g2d->runqueue_mutex);

	ret = g2d_init_cmdlist(g2d);
	if (ret < 0)
		goto err_destroy_workqueue;

	g2d->gate_clk = clk_get(dev, "fimg2d");
	if (IS_ERR(g2d->gate_clk)) {
		dev_err(dev, "failed to get gate clock\n");
		ret = PTR_ERR(g2d->gate_clk);
		goto err_fini_cmdlist;
	}

	pm_runtime_enable(dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get I/O memory\n");
		ret = -ENOENT;
		goto err_put_clk;
	}

	g2d->regs_res = request_mem_region(res->start, resource_size(res),
					   dev_name(dev));
	if (!g2d->regs_res) {
		dev_err(dev, "failed to request I/O memory\n");
		ret = -ENOENT;
		goto err_put_clk;
	}

	g2d->regs = ioremap(res->start, resource_size(res));
	if (!g2d->regs) {
		dev_err(dev, "failed to remap I/O memory\n");
		ret = -ENXIO;
		goto err_release_res;
	}

	g2d->irq = platform_get_irq(pdev, 0);
	if (g2d->irq < 0) {
		dev_err(dev, "failed to get irq\n");
		ret = g2d->irq;
		goto err_unmap_base;
	}

	ret = request_irq(g2d->irq, g2d_irq_handler, 0, "drm_g2d", g2d);
	if (ret < 0) {
		dev_err(dev, "irq request failed\n");
		goto err_unmap_base;
	}

	platform_set_drvdata(pdev, g2d);

	subdrv = &g2d->subdrv;
	subdrv->dev = dev;
	subdrv->open = g2d_open;
	subdrv->close = g2d_close;

	ret = exynos_drm_subdrv_register(subdrv);
	if (ret < 0) {
		dev_err(dev, "failed to register drm g2d device\n");
		goto err_free_irq;
	}

	dev_info(dev, "The exynos g2d(ver %d.%d) successfully probed\n",
			G2D_HW_MAJOR_VER, G2D_HW_MINOR_VER);

	return 0;

err_free_irq:
	free_irq(g2d->irq, g2d);
err_unmap_base:
	iounmap(g2d->regs);
err_release_res:
	release_resource(g2d->regs_res);
	kfree(g2d->regs_res);
err_put_clk:
	pm_runtime_disable(dev);
	clk_put(g2d->gate_clk);
err_fini_cmdlist:
	g2d_fini_cmdlist(g2d);
err_destroy_workqueue:
	destroy_workqueue(g2d->g2d_workq);
err_destroy_slab:
	kmem_cache_destroy(g2d->runqueue_slab);
err_free_mem:
	kfree(g2d);
	return ret;
}

static int __devexit g2d_remove(struct platform_device *pdev)
{
	struct g2d_data *g2d = platform_get_drvdata(pdev);

	cancel_work_sync(&g2d->runqueue_work);
	exynos_drm_subdrv_unregister(&g2d->subdrv);
	free_irq(g2d->irq, g2d);

	while (g2d->runqueue_node) {
		g2d_free_runqueue_node(g2d, g2d->runqueue_node);
		g2d->runqueue_node = g2d_get_runqueue_node(g2d);
	}

	iounmap(g2d->regs);
	release_resource(g2d->regs_res);
	kfree(g2d->regs_res);

	pm_runtime_disable(&pdev->dev);
	clk_put(g2d->gate_clk);

	g2d_fini_cmdlist(g2d);
	destroy_workqueue(g2d->g2d_workq);
	kmem_cache_destroy(g2d->runqueue_slab);
	kfree(g2d);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int g2d_suspend(struct device *dev)
{
	struct g2d_data *g2d = dev_get_drvdata(dev);

	mutex_lock(&g2d->runqueue_mutex);
	g2d->suspended = true;
	mutex_unlock(&g2d->runqueue_mutex);

	while (g2d->runqueue_node)
		/* FIXME: good range? */
		usleep_range(500, 1000);

	flush_work_sync(&g2d->runqueue_work);

	return 0;
}

static int g2d_resume(struct device *dev)
{
	struct g2d_data *g2d = dev_get_drvdata(dev);

	g2d->suspended = false;
	g2d_exec_runqueue(g2d);

	return 0;
}
#endif

SIMPLE_DEV_PM_OPS(g2d_pm_ops, g2d_suspend, g2d_resume);

struct platform_driver g2d_driver = {
	.probe		= g2d_probe,
	.remove		= __devexit_p(g2d_remove),
	.driver		= {
		.name	= "s5p-g2d",
		.owner	= THIS_MODULE,
		.pm	= &g2d_pm_ops,
	},
};
