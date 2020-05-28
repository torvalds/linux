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

#define MPP_REGISTER_DRIVER(srv, X, x) {\
	if (IS_ENABLED(CONFIG_ROCKCHIP_MPP_##X))\
		mpp_add_driver(srv, MPP_DRIVER_##X, &rockchip_##x##_driver, "grf_"#x);\
	}

unsigned int mpp_dev_debug;
module_param(mpp_dev_debug, uint, 0644);
MODULE_PARM_DESC(mpp_dev_debug, "bit switch for mpp debug information");

static int mpp_init_grf(struct device_node *np,
			struct mpp_grf_info *grf_info,
			const char *grf_name)
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

	index = of_property_match_string(np, "rockchip,grf-names", grf_name);
	if (index < 0)
		return -ENODATA;

	ret = of_property_read_u32_index(np, "rockchip,grf-values",
					 index, &grf_value);
	if (ret)
		return -ENODATA;

	grf_info->grf = grf;
	grf_info->offset = grf_offset;
	grf_info->val = grf_value;

	mpp_set_grf(grf_info);

	return 0;
}

static int mpp_add_driver(struct mpp_service *srv,
			  enum MPP_DRIVER_TYPE type,
			  struct platform_driver *driver,
			  const char *grf_name)
{
	int ret;

	mpp_init_grf(srv->dev->of_node,
		     &srv->grf_infos[type],
		     grf_name);

	ret = platform_driver_register(driver);
	if (ret)
		return ret;

	srv->sub_drivers[type] = driver;

	return 0;
}

static int mpp_remove_driver(struct mpp_service *srv, int i)
{
	if (srv && srv->sub_drivers[i]) {
		mpp_set_grf(&srv->grf_infos[i]);
		platform_driver_unregister(srv->sub_drivers[i]);
		srv->sub_drivers[i] = NULL;
	}

	return 0;
}

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

	cdev_init(&srv->mpp_cdev, &rockchip_mpp_fops);
	srv->mpp_cdev.owner = THIS_MODULE;
	srv->mpp_cdev.ops = &rockchip_mpp_fops;

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

#ifdef CONFIG_DEBUG_FS
static int mpp_debugfs_remove(struct mpp_service *srv)
{
	debugfs_remove_recursive(srv->debugfs);

	return 0;
}

static int mpp_debugfs_init(struct mpp_service *srv)
{
	srv->debugfs = debugfs_create_dir(MPP_SERVICE_NAME, NULL);
	if (IS_ERR_OR_NULL(srv->debugfs)) {
		mpp_err("failed on open debugfs\n");
		srv->debugfs = NULL;
	}

	return 0;
}
#else
static inline int mpp_debugfs_remove(struct mpp_service *srv)
{
	return 0;
}

static inline int mpp_debugfs_init(struct mpp_service *srv)
{
	return 0;
}
#endif

static int mpp_service_probe(struct platform_device *pdev)
{
	int ret;
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
			     &srv->taskqueue_cnt);
	if (srv->taskqueue_cnt > MPP_DEVICE_BUTT) {
		dev_err(dev, "rockchip,taskqueue-count %d must less than %d\n",
			srv->taskqueue_cnt, MPP_DEVICE_BUTT);
		return -EINVAL;
	}

	if (srv->taskqueue_cnt) {
		u32 i = 0;
		struct mpp_taskqueue *queue;

		for (i = 0; i < srv->taskqueue_cnt; i++) {
			queue = devm_kzalloc(dev, sizeof(*queue), GFP_KERNEL);
			if (!queue)
				continue;

			mpp_taskqueue_init(queue, srv);
			srv->task_queues[i] = queue;
		}
	}

	of_property_read_u32(np, "rockchip,resetgroup-count",
			     &srv->reset_group_cnt);
	if (srv->reset_group_cnt > MPP_DEVICE_BUTT) {
		dev_err(dev, "rockchip,resetgroup-count %d must less than %d\n",
			srv->reset_group_cnt, MPP_DEVICE_BUTT);
		return -EINVAL;
	}

	if (srv->reset_group_cnt) {
		u32 i = 0;
		struct mpp_reset_group *group;

		for (i = 0; i < srv->reset_group_cnt; i++) {
			group = devm_kzalloc(dev, sizeof(*group), GFP_KERNEL);
			if (!group)
				continue;

			init_rwsem(&group->rw_sem);
			srv->reset_groups[i] = group;
		}
	}

	ret = mpp_register_service(srv, MPP_SERVICE_NAME);
	if (ret) {
		dev_err(dev, "register %s device\n", MPP_SERVICE_NAME);
		goto fail_register;
	}
	mpp_debugfs_init(srv);

	/* register sub drivers */
	MPP_REGISTER_DRIVER(srv, RKVDEC, rkvdec);
	MPP_REGISTER_DRIVER(srv, RKVENC, rkvenc);
	MPP_REGISTER_DRIVER(srv, VDPU1, vdpu1);
	MPP_REGISTER_DRIVER(srv, VEPU1, vepu1);
	MPP_REGISTER_DRIVER(srv, VDPU2, vdpu2);
	MPP_REGISTER_DRIVER(srv, VEPU2, vepu2);
	MPP_REGISTER_DRIVER(srv, VEPU22, vepu22);
	MPP_REGISTER_DRIVER(srv, IEP2, iep2);

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
	int i;

	dev_info(dev, "remove device\n");

	/* remove sub drivers */
	for (i = 0; i < MPP_DRIVER_BUTT; i++)
		mpp_remove_driver(srv, i);

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

module_platform_driver(mpp_service_driver);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("1.0.build.201911131848");
MODULE_AUTHOR("Ding Wei leo.ding@rock-chips.com");
MODULE_DESCRIPTION("Rockchip mpp service driver");
