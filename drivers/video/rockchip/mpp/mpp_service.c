// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define MPP_CLASS_NAME		"mpp_class"
#define MPP_SERVICE_NAME	"mpp_service"

#define MPP_REGISTER_GRF(np, X, x) {\
	if (IS_ENABLED(CONFIG_ROCKCHIP_MPP_##X))\
		mpp_init_grf(np, &srv->grf_infos[MPP_DRIVER_##X], x);\
	}

#define MPP_REGISTER_DRIVER(X, x) {\
	if (IS_ENABLED(CONFIG_ROCKCHIP_MPP_##X))\
		mpp_add_driver(MPP_DRIVER_##X, &rockchip_##x##_driver);\
	}

unsigned int mpp_dev_debug;
module_param(mpp_dev_debug, uint, 0644);
MODULE_PARM_DESC(mpp_dev_debug, "bit switch for mpp debug information");

static struct mpp_grf_info *mpp_grf_infos;
static struct platform_driver **mpp_sub_drivers;

static int mpp_init_grf(struct device_node *np,
			struct mpp_grf_info *grf_info,
			const char *name)
{
	int ret;
	int index;
	u32 grf_offset = 0;
	u32 grf_value = 0;
	struct regmap *grf;

	grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR_OR_NULL(grf))
		return -EINVAL;

	ret = of_property_read_u32(np, "rockchip,grf-offset", &grf_offset);
	if (ret)
		return -ENODATA;

	index = of_property_match_string(np, "rockchip,grf-names", name);
	if (index < 0)
		return -ENODATA;

	ret = of_property_read_u32_index(np, "rockchip,grf-values",
				   index, &grf_value);
	if (ret)
		return -ENODATA;

	grf_info->grf = grf;
	grf_info->offset = grf_offset;
	grf_info->val = grf_value;

	return 0;
}

static const struct file_operations mpp_dev_fops = {
	.unlocked_ioctl = mpp_dev_ioctl,
	.open		= mpp_dev_open,
	.release	= mpp_dev_release,
	.poll		= mpp_dev_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = mpp_dev_compat_ioctl,
#endif
};

static int mpp_register_service(struct mpp_service *srv,
				const char *service_name)
{
	int ret;
	struct device *dev = srv->dev;

	/* create a device */
	ret = alloc_chrdev_region(&srv->dev_id, 0, 1, service_name);
	if (ret) {
		dev_err(dev, "alloc dev_t failed\n");
		return ret;
	}

	cdev_init(&srv->mpp_cdev, &mpp_dev_fops);
	srv->mpp_cdev.owner = THIS_MODULE;
	srv->mpp_cdev.ops = &mpp_dev_fops;

	ret = cdev_add(&srv->mpp_cdev, srv->dev_id, 1);
	if (ret) {
		unregister_chrdev_region(srv->dev_id, 1);
		dev_err(dev, "add device failed\n");
		return ret;
	}

	srv->child_dev = device_create(srv->cls, dev, srv->dev_id,
				       NULL, "%s", service_name);

	return 0;
}

static int mpp_remove_service(struct mpp_service *srv)
{
	device_destroy(srv->cls, srv->dev_id);
	cdev_del(&srv->mpp_cdev);
	unregister_chrdev_region(srv->dev_id, 1);

	return 0;
}

static int mpp_debugfs_remove(struct mpp_service *srv)
{
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(srv->debugfs);
#endif
	return 0;
}

static int mpp_debugfs_init(struct mpp_service *srv)
{
#ifdef CONFIG_DEBUG_FS
	srv->debugfs = debugfs_create_dir(MPP_SERVICE_NAME, NULL);
	if (IS_ERR_OR_NULL(srv->debugfs)) {
		mpp_err("failed on open debugfs\n");
		srv->debugfs = NULL;
	}
#endif

	return 0;
}

static int mpp_service_probe(struct platform_device *pdev)
{
	int ret;
	u32 taskqueue_cnt;
	struct mpp_service *srv = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	dev_info(dev, "probe start\n");
	srv = devm_kzalloc(dev, sizeof(*srv), GFP_KERNEL);
	if (!srv)
		return -ENOMEM;

	srv->dev = dev;
	atomic_set(&srv->shutdown_request, 0);
	platform_set_drvdata(pdev, srv);

	srv->cls = class_create(THIS_MODULE, MPP_CLASS_NAME);
	if (PTR_ERR_OR_ZERO(srv->cls))
		return PTR_ERR(srv->cls);

	of_property_read_u32(np, "rockchip,taskqueue-count",
			     &taskqueue_cnt);
	if (taskqueue_cnt > MPP_DEVICE_BUTT) {
		dev_err(dev, "rockchip,taskqueue-count %d must less than %d\n",
			taskqueue_cnt, MPP_DEVICE_BUTT);
		return -EINVAL;
	}

	if (taskqueue_cnt) {
		u32 i = 0;
		struct mpp_taskqueue *queue;

		for (i = 0; i < taskqueue_cnt; i++) {
			queue = devm_kzalloc(dev, sizeof(*queue), GFP_KERNEL);
			if (!queue)
				continue;

			mpp_taskqueue_init(queue, srv);
			srv->task_queues[i] = queue;
		}
	}
	MPP_REGISTER_GRF(np, RKVDEC, "grf_rkvdec");
	MPP_REGISTER_GRF(np, RKVENC, "grf_rkvenc");
	MPP_REGISTER_GRF(np, VEPU1, "grf_vepu1");
	MPP_REGISTER_GRF(np, VDPU1, "grf_vdpu1");
	MPP_REGISTER_GRF(np, VEPU2, "grf_vepu2");
	MPP_REGISTER_GRF(np, VDPU2, "grf_vdpu2");
	MPP_REGISTER_GRF(np, VEPU22, "grf_vepu22");

	mpp_grf_infos = srv->grf_infos;

	ret = mpp_register_service(srv, MPP_SERVICE_NAME);
	if (ret) {
		dev_err(dev, "register %s device\n", MPP_SERVICE_NAME);
		goto fail_register;
	}
	mpp_sub_drivers = srv->sub_drivers;

	mpp_debugfs_init(srv);

	dev_info(dev, "probe success\n");

	return 0;

fail_register:
	class_destroy(srv->cls);

	return ret;
}

static int mpp_service_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpp_service *srv = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");

	mpp_remove_service(srv);
	class_destroy(srv->cls);
	mpp_debugfs_remove(srv);

	return 0;
}

static const struct of_device_id mpp_dt_ids[] = {
	{
		.compatible = "rockchip,mpp-service",
	},
	{ },
};

static struct platform_driver mpp_service_driver = {
	.probe = mpp_service_probe,
	.remove = mpp_service_remove,
	.driver = {
		.name = "mpp_service",
		.of_match_table = of_match_ptr(mpp_dt_ids),
	},
};

static int mpp_add_driver(enum MPP_DRIVER_TYPE type,
			  struct platform_driver *driver)
{
	int ret;

	mpp_set_grf(&mpp_grf_infos[type]);

	ret = platform_driver_register(driver);
	if (ret)
		return ret;

	mpp_sub_drivers[type] = driver;

	return 0;
}

static int mpp_remove_driver(int i, struct platform_driver *driver)
{
	if (driver) {
		mpp_set_grf(&mpp_grf_infos[i]);
		platform_driver_unregister(driver);
	}

	return 0;
}

static int __init mpp_service_init(void)
{
	int ret;

	ret = platform_driver_register(&mpp_service_driver);
	if (ret) {
		pr_err("Mpp service device register failed (%d).\n", ret);
		return ret;
	}
	MPP_REGISTER_DRIVER(RKVDEC, rkvdec);
	MPP_REGISTER_DRIVER(RKVENC, rkvenc);
	MPP_REGISTER_DRIVER(VDPU1, vdpu1);
	MPP_REGISTER_DRIVER(VEPU1, vepu1);
	MPP_REGISTER_DRIVER(VDPU2, vdpu2);
	MPP_REGISTER_DRIVER(VEPU2, vepu2);
	MPP_REGISTER_DRIVER(VEPU22, vepu22);

	return 0;
}

static void __exit mpp_service_exit(void)
{
	int i;

	for (i = 0; i < MPP_DRIVER_BUTT; i++)
		mpp_remove_driver(i, mpp_sub_drivers[i]);

	platform_driver_unregister(&mpp_service_driver);
}

module_init(mpp_service_init);
module_exit(mpp_service_exit);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("1.0.build.201911131848");
MODULE_AUTHOR("Ding Wei leo.ding@rock-chips.com");
MODULE_DESCRIPTION("Rockchip mpp service driver");
