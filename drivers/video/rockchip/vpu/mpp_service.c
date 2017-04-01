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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>

#include "mpp_dev_common.h"
#include "mpp_service.h"

void mpp_srv_lock(struct mpp_service *pservice)
{
	mutex_lock(&pservice->lock);
}
EXPORT_SYMBOL(mpp_srv_lock);

void mpp_srv_unlock(struct mpp_service *pservice)
{
	mutex_unlock(&pservice->lock);
}
EXPORT_SYMBOL(mpp_srv_unlock);

/* service queue schedule */
void mpp_srv_pending_locked(struct mpp_service *pservice,
			    struct mpp_ctx *ctx)
{
	mpp_srv_lock(pservice);

	list_add_tail(&ctx->status_link, &pservice->pending);

	mpp_srv_unlock(pservice);
}
EXPORT_SYMBOL(mpp_srv_pending_locked);

void mpp_srv_run(struct mpp_service *pservice)
{
	struct mpp_ctx *ctx = mpp_srv_get_pending_ctx(pservice);

	list_del_init(&ctx->status_link);
	list_add_tail(&ctx->status_link, &pservice->running);
}
EXPORT_SYMBOL(mpp_srv_run);

void mpp_srv_done(struct mpp_service *pservice)
{
	struct mpp_ctx *ctx = list_entry(pservice->running.next,
					 struct mpp_ctx, status_link);

	list_del_init(&ctx->session_link);
	list_add_tail(&ctx->session_link, &ctx->session->done);

	list_del_init(&ctx->status_link);
	list_add_tail(&ctx->status_link, &pservice->done);

	wake_up(&ctx->session->wait);
}
EXPORT_SYMBOL(mpp_srv_done);

struct mpp_ctx *mpp_srv_get_pending_ctx(struct mpp_service *pservice)
{
	return list_entry(pservice->pending.next, struct mpp_ctx, status_link);
}
EXPORT_SYMBOL(mpp_srv_get_pending_ctx);

struct mpp_ctx *mpp_srv_get_current_ctx(struct mpp_service *pservice)
{
	return list_entry(pservice->running.next, struct mpp_ctx, status_link);
}
EXPORT_SYMBOL(mpp_srv_get_current_ctx);

struct mpp_ctx *mpp_srv_get_last_running_ctx(struct mpp_service *pservice)
{
	return list_entry(pservice->running.prev, struct mpp_ctx, status_link);
}
EXPORT_SYMBOL(mpp_srv_get_last_running_ctx);

struct mpp_session *mpp_srv_get_current_session(struct mpp_service *pservice)
{
	struct mpp_ctx *ctx = list_entry(pservice->running.next,
					 struct mpp_ctx, status_link);
	return ctx ? ctx->session : NULL;
}
EXPORT_SYMBOL(mpp_srv_get_current_session);

struct mpp_ctx *mpp_srv_get_done_ctx(struct mpp_session *session)
{
	return list_entry(session->done.next, struct mpp_ctx, session_link);
}
EXPORT_SYMBOL(mpp_srv_get_done_ctx);

bool mpp_srv_pending_is_empty(struct mpp_service *pservice)
{
	return !!list_empty(&pservice->pending);
}
EXPORT_SYMBOL(mpp_srv_pending_is_empty);

void mpp_srv_attach(struct mpp_service *pservice, struct list_head *elem)
{
	INIT_LIST_HEAD(elem);
	list_add_tail(elem, &pservice->subdev_list);
	pservice->dev_cnt++;
}
EXPORT_SYMBOL(mpp_srv_attach);

void mpp_srv_detach(struct mpp_service *pservice, struct list_head *elem)
{
	list_del_init(elem);
	pservice->dev_cnt--;
}
EXPORT_SYMBOL(mpp_srv_detach);

bool mpp_srv_is_running(struct mpp_service *pservice)
{
	return !list_empty(&pservice->running);
}
EXPORT_SYMBOL(mpp_srv_is_running);

static void mpp_init_drvdata(struct mpp_service *pservice)
{
	INIT_LIST_HEAD(&pservice->pending);
	mutex_init(&pservice->lock);

	INIT_LIST_HEAD(&pservice->done);
	INIT_LIST_HEAD(&pservice->session);
	INIT_LIST_HEAD(&pservice->subdev_list);
	INIT_LIST_HEAD(&pservice->running);
}

#if defined(CONFIG_OF)
static const struct of_device_id mpp_service_dt_ids[] = {
	{ .compatible = "rockchip,mpp_service", },
	{ },
};
#endif

static int mpp_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct mpp_service *pservice =
				       devm_kzalloc(dev, sizeof(*pservice),
						    GFP_KERNEL);

	dev_info(dev, "%s enter\n", __func__);

	pservice->dev = dev;

	mpp_init_drvdata(pservice);

	if (of_property_read_bool(np, "reg")) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

		pservice->reg_base = devm_ioremap_resource(pservice->dev, res);
		if (IS_ERR(pservice->reg_base)) {
			dev_err(dev, "ioremap registers base failed\n");
			ret = PTR_ERR(pservice->reg_base);
			pservice->reg_base = 0;
		}
	} else {
		pservice->reg_base = 0;
	}

	pservice->cls = class_create(THIS_MODULE, dev_name(dev));

	if (IS_ERR(pservice->cls)) {
		ret = PTR_ERR(pservice->cls);
		dev_err(dev, "class_create err:%d\n", ret);
		return -1;
	}

	platform_set_drvdata(pdev, pservice);
	dev_info(dev, "init success\n");

	return 0;
}

static int mpp_remove(struct platform_device *pdev)
{
	struct mpp_service *pservice = platform_get_drvdata(pdev);

	class_destroy(pservice->cls);
	return 0;
}

static struct platform_driver mpp_driver = {
	.probe = mpp_probe,
	.remove = mpp_remove,
	.driver = {
		.name = "mpp",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(mpp_service_dt_ids),
#endif
	},
};

static int __init mpp_service_init(void)
{
	int ret = platform_driver_register(&mpp_driver);

	if (ret) {
		mpp_err("Platform device register failed (%d).\n", ret);
		return ret;
	}

	return ret;
}

subsys_initcall(mpp_service_init);
MODULE_LICENSE("GPL");

