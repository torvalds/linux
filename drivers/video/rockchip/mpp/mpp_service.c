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
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/nospec.h>
#include <linux/mfd/syscon.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define MPP_CLASS_NAME		"mpp_class"
#define MPP_SERVICE_NAME	"mpp_service"

#define HAS_RKVDEC	IS_ENABLED(CONFIG_ROCKCHIP_MPP_RKVDEC)
#define HAS_RKVENC	IS_ENABLED(CONFIG_ROCKCHIP_MPP_RKVENC)
#define HAS_VDPU1	IS_ENABLED(CONFIG_ROCKCHIP_MPP_VDPU1)
#define HAS_VEPU1	IS_ENABLED(CONFIG_ROCKCHIP_MPP_VEPU1)
#define HAS_VDPU2	IS_ENABLED(CONFIG_ROCKCHIP_MPP_VDPU2)
#define HAS_VEPU2	IS_ENABLED(CONFIG_ROCKCHIP_MPP_VEPU2)
#define HAS_VEPU22	IS_ENABLED(CONFIG_ROCKCHIP_MPP_VEPU22)
#define HAS_IEP2	IS_ENABLED(CONFIG_ROCKCHIP_MPP_IEP2)
#define HAS_JPGDEC	IS_ENABLED(CONFIG_ROCKCHIP_MPP_JPGDEC)
#define HAS_RKVDEC2	IS_ENABLED(CONFIG_ROCKCHIP_MPP_RKVDEC2)
#define HAS_RKVENC2	IS_ENABLED(CONFIG_ROCKCHIP_MPP_RKVENC2)
#define HAS_AV1DEC	IS_ENABLED(CONFIG_ROCKCHIP_MPP_AV1DEC)

#define MPP_REGISTER_DRIVER(srv, flag, X, x) {\
	if (flag)\
		mpp_add_driver(srv, MPP_DRIVER_##X, &rockchip_##x##_driver, "grf_"#x);\
	}

unsigned int mpp_dev_debug;
module_param(mpp_dev_debug, uint, 0644);
MODULE_PARM_DESC(mpp_dev_debug, "bit switch for mpp debug information");

static const char mpp_version[] = MPP_VERSION;

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

	if (type == MPP_DRIVER_AV1DEC)
		ret = av1dec_driver_register(driver);
	else
		ret = platform_driver_register(driver);
	if (ret)
		return ret;

	srv->sub_drivers[type] = driver;

	return 0;
}

static int mpp_remove_driver(struct mpp_service *srv, int i)
{
	if (srv && srv->sub_drivers[i]) {
		if (i != MPP_DRIVER_AV1DEC) {
			mpp_set_grf(&srv->grf_infos[i]);
			platform_driver_unregister(srv->sub_drivers[i]);
		}
#if IS_ENABLED(CONFIG_ROCKCHIP_MPP_AV1DEC)
		else
			av1dec_driver_unregister(srv->sub_drivers[i]);
#endif
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

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
static int mpp_procfs_remove(struct mpp_service *srv)
{
	if (srv->procfs) {
		proc_remove(srv->procfs);
		srv->procfs = NULL;
	}

	return 0;
}

static int mpp_show_version(struct seq_file *seq, void *offset)
{
	seq_printf(seq, "%s\n", mpp_version);

	return 0;
}

static int mpp_dump_session(struct mpp_session *session, struct seq_file *s)
{
	struct mpp_dma_session *dma = session->dma;
	struct mpp_dma_buffer *n;
	struct mpp_dma_buffer *buffer;
	phys_addr_t end;
	unsigned long z = 0, t = 0;
	int i = 0;
#define K(size) ((unsigned long)((size) >> 10))

	if (!dma)
		return 0;

	seq_puts(s, "session iova range dump:\n");

	mutex_lock(&dma->list_mutex);
	list_for_each_entry_safe(buffer, n, &dma->used_list, link) {
		end = buffer->iova + buffer->size - 1;
		z = (unsigned long)buffer->size;
		t += z;

		seq_printf(s, "%4d: ", i++);
		seq_printf(s, "%pa..%pa (%10lu %s)\n", &buffer->iova, &end,
			   (z >= 1024) ? (K(z)) : z,
			   (z >= 1024) ? "KiB" : "Bytes");
	}
	i = 0;
	list_for_each_entry_safe(buffer, n, &dma->unused_list, link) {
		if (!buffer->dmabuf)
			continue;

		end = buffer->iova + buffer->size - 1;
		z = (unsigned long)buffer->size;
		t += z;

		seq_printf(s, "%4d: ", i++);
		seq_printf(s, "%pa..%pa (%10lu %s)\n", &buffer->iova, &end,
			   (z >= 1024) ? (K(z)) : z,
			   (z >= 1024) ? "KiB" : "Bytes");
	}

	mutex_unlock(&dma->list_mutex);
	seq_printf(s, "session: pid=%d index=%d\n", session->pid, session->index);
	seq_printf(s, " device: %s\n", dev_name(session->mpp->dev));
	seq_printf(s, " memory: %lu MiB\n", K(K(t)));

	return 0;
}

static int mpp_show_session_summary(struct seq_file *seq, void *offset)
{
	struct mpp_session *session = NULL, *n;
	struct mpp_service *srv = seq->private;

	mutex_lock(&srv->session_lock);
	list_for_each_entry_safe(session, n,
				 &srv->session_list,
				 service_link) {
		struct  mpp_dev *mpp;

		if (!session->priv)
			continue;

		if (!session->mpp)
			continue;
		mpp = session->mpp;

		mpp_dump_session(session, seq);

		if (mpp->dev_ops->dump_session)
			mpp->dev_ops->dump_session(session, seq);
	}
	mutex_unlock(&srv->session_lock);

	return 0;
}

static int mpp_show_support_cmd(struct seq_file *file, void *v)
{
	seq_puts(file, "------------- SUPPORT CMD -------------\n");
	seq_printf(file, "QUERY_HW_SUPPORT:     0x%08x\n", MPP_CMD_QUERY_HW_SUPPORT);
	seq_printf(file, "QUERY_HW_ID:          0x%08x\n", MPP_CMD_QUERY_HW_ID);
	seq_printf(file, "QUERY_CMD_SUPPORT:    0x%08x\n", MPP_CMD_QUERY_CMD_SUPPORT);
	seq_printf(file, "QUERY_BUTT:           0x%08x\n", MPP_CMD_QUERY_BUTT);
	seq_puts(file, "----\n");
	seq_printf(file, "INIT_CLIENT_TYPE:     0x%08x\n", MPP_CMD_INIT_CLIENT_TYPE);
	seq_printf(file, "INIT_TRANS_TABLE:     0x%08x\n", MPP_CMD_INIT_TRANS_TABLE);
	seq_printf(file, "INIT_BUTT:            0x%08x\n", MPP_CMD_INIT_BUTT);
	seq_puts(file, "----\n");
	seq_printf(file, "SET_REG_WRITE:        0x%08x\n", MPP_CMD_SET_REG_WRITE);
	seq_printf(file, "SET_REG_READ:         0x%08x\n", MPP_CMD_SET_REG_READ);
	seq_printf(file, "SET_REG_ADDR_OFFSET:  0x%08x\n", MPP_CMD_SET_REG_ADDR_OFFSET);
	seq_printf(file, "SEND_BUTT:            0x%08x\n", MPP_CMD_SEND_BUTT);
	seq_puts(file, "----\n");
	seq_printf(file, "POLL_HW_FINISH:       0x%08x\n", MPP_CMD_POLL_HW_FINISH);
	seq_printf(file, "POLL_BUTT:            0x%08x\n", MPP_CMD_POLL_BUTT);
	seq_puts(file, "----\n");
	seq_printf(file, "RESET_SESSION:        0x%08x\n", MPP_CMD_RESET_SESSION);
	seq_printf(file, "TRANS_FD_TO_IOVA:     0x%08x\n", MPP_CMD_TRANS_FD_TO_IOVA);
	seq_printf(file, "RELEASE_FD:           0x%08x\n", MPP_CMD_RELEASE_FD);
	seq_printf(file, "SEND_CODEC_INFO:      0x%08x\n", MPP_CMD_SEND_CODEC_INFO);
	seq_printf(file, "CONTROL_BUTT:         0x%08x\n", MPP_CMD_CONTROL_BUTT);

	return 0;
}

static int mpp_show_support_device(struct seq_file *file, void *v)
{
	u32 i;
	struct mpp_service *srv = file->private;

	seq_puts(file, "---- SUPPORT DEVICES ----\n");
	for (i = 0; i < MPP_DEVICE_BUTT; i++) {
		struct mpp_dev *mpp;
		struct mpp_hw_info *hw_info;

		if (test_bit(i, &srv->hw_support)) {
			mpp = srv->sub_devices[array_index_nospec(i, MPP_DEVICE_BUTT)];
			if (!mpp)
				continue;

			seq_printf(file, "DEVICE[%2d]:%-10s", i, mpp_device_name[i]);
			hw_info = mpp->var->hw_info;
			if (hw_info->hw_id)
				seq_printf(file, "HW_ID:0x%08x", hw_info->hw_id);
			seq_puts(file, "\n");
		}
	}

	return 0;
}

static int mpp_procfs_init(struct mpp_service *srv)
{
	srv->procfs = proc_mkdir(MPP_SERVICE_NAME, NULL);
	if (IS_ERR_OR_NULL(srv->procfs)) {
		mpp_err("failed on mkdir /proc/%s\n", MPP_SERVICE_NAME);
		srv->procfs = NULL;
		return -EIO;
	}
	/* show version */
	proc_create_single("version", 0444, srv->procfs, mpp_show_version);
	/* for show session info */
	proc_create_single_data("sessions-summary", 0444,
				srv->procfs, mpp_show_session_summary, srv);
	/* show support dev cmd */
	proc_create_single("supports-cmd", 0444, srv->procfs, mpp_show_support_cmd);
	/* show support devices */
	proc_create_single_data("supports-device", 0444,
				srv->procfs, mpp_show_support_device, srv);
	mpp_procfs_create_u32("timing_en", 0644, srv->procfs, &srv->timing_en);

	return 0;
}
#else
static inline int mpp_procfs_remove(struct mpp_service *srv)
{
	return 0;
}

static inline int mpp_procfs_init(struct mpp_service *srv)
{
	return 0;
}
#endif

static int mpp_service_probe(struct platform_device *pdev)
{
	int ret, i;
	struct mpp_service *srv = NULL;
	struct mpp_taskqueue *queue;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	dev_info(dev, "%s\n", mpp_version);
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

	for (i = 0; i < srv->taskqueue_cnt; i++) {
		queue = mpp_taskqueue_init(dev);
		if (!queue)
			continue;

		kthread_init_worker(&queue->worker);
		queue->kworker_task = kthread_run(kthread_worker_fn, &queue->worker,
						  "queue_work%d", i);
		srv->task_queues[i] = queue;
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
	mutex_init(&srv->session_lock);
	INIT_LIST_HEAD(&srv->session_list);
	mpp_procfs_init(srv);

	/* register sub drivers */
	MPP_REGISTER_DRIVER(srv, HAS_RKVDEC, RKVDEC, rkvdec);
	MPP_REGISTER_DRIVER(srv, HAS_RKVENC, RKVENC, rkvenc);
	MPP_REGISTER_DRIVER(srv, HAS_VDPU1, VDPU1, vdpu1);
	MPP_REGISTER_DRIVER(srv, HAS_VEPU1, VEPU1, vepu1);
	MPP_REGISTER_DRIVER(srv, HAS_VDPU2, VDPU2, vdpu2);
	MPP_REGISTER_DRIVER(srv, HAS_VEPU2, VEPU2, vepu2);
	MPP_REGISTER_DRIVER(srv, HAS_VEPU22, VEPU22, vepu22);
	MPP_REGISTER_DRIVER(srv, HAS_IEP2, IEP2, iep2);
	MPP_REGISTER_DRIVER(srv, HAS_JPGDEC, JPGDEC, jpgdec);
	MPP_REGISTER_DRIVER(srv, HAS_RKVDEC2, RKVDEC2, rkvdec2);
	MPP_REGISTER_DRIVER(srv, HAS_RKVENC2, RKVENC2, rkvenc2);
	MPP_REGISTER_DRIVER(srv, HAS_AV1DEC, AV1DEC, av1dec);

	dev_info(dev, "probe success\n");

	return 0;

fail_register:
	class_destroy(srv->cls);

	return ret;
}

static int mpp_service_remove(struct platform_device *pdev)
{
	struct mpp_taskqueue *queue;
	struct device *dev = &pdev->dev;
	struct mpp_service *srv = platform_get_drvdata(pdev);
	int i;

	dev_info(dev, "remove device\n");

	for (i = 0; i < srv->taskqueue_cnt; i++) {
		queue = srv->task_queues[i];
		if (queue && queue->kworker_task) {
			kthread_flush_worker(&queue->worker);
			kthread_stop(queue->kworker_task);
			queue->kworker_task = NULL;
		}
	}

	/* remove sub drivers */
	for (i = 0; i < MPP_DRIVER_BUTT; i++)
		mpp_remove_driver(srv, i);

	mpp_remove_service(srv);
	class_destroy(srv->cls);
	mpp_procfs_remove(srv);

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
MODULE_VERSION(MPP_VERSION);
MODULE_AUTHOR("Ding Wei leo.ding@rock-chips.com");
MODULE_DESCRIPTION("Rockchip mpp service driver");
