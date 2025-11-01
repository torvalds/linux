// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>

#include <media/ipu-bridge.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "abi/ipu7_fw_isys_abi.h"

#include "ipu7-bus.h"
#include "ipu7-buttress-regs.h"
#include "ipu7-cpd.h"
#include "ipu7-dma.h"
#include "ipu7-fw-isys.h"
#include "ipu7-mmu.h"
#include "ipu7-isys.h"
#include "ipu7-isys-csi2.h"
#include "ipu7-isys-csi-phy.h"
#include "ipu7-isys-csi2-regs.h"
#include "ipu7-isys-video.h"
#include "ipu7-platform-regs.h"

#define ISYS_PM_QOS_VALUE	300

static int
isys_complete_ext_device_registration(struct ipu7_isys *isys,
				      struct v4l2_subdev *sd,
				      struct ipu7_isys_csi2_config *csi2)
{
	struct device *dev = &isys->adev->auxdev.dev;
	unsigned int i;
	int ret;

	v4l2_set_subdev_hostdata(sd, csi2);

	for (i = 0; i < sd->entity.num_pads; i++) {
		if (sd->entity.pads[i].flags & MEDIA_PAD_FL_SOURCE)
			break;
	}

	if (i == sd->entity.num_pads) {
		dev_warn(dev, "no source pad in external entity\n");
		ret = -ENOENT;
		goto skip_unregister_subdev;
	}

	ret = media_create_pad_link(&sd->entity, i,
				    &isys->csi2[csi2->port].asd.sd.entity,
				    0, MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_warn(dev, "can't create link\n");
		goto skip_unregister_subdev;
	}

	isys->csi2[csi2->port].nlanes = csi2->nlanes;
	if (csi2->bus_type == V4L2_MBUS_CSI2_DPHY)
		isys->csi2[csi2->port].phy_mode = PHY_MODE_DPHY;
	else
		isys->csi2[csi2->port].phy_mode = PHY_MODE_CPHY;

	return 0;

skip_unregister_subdev:
	v4l2_device_unregister_subdev(sd);
	return ret;
}

static void isys_stream_init(struct ipu7_isys *isys)
{
	unsigned int i;

	for (i = 0; i < IPU_ISYS_MAX_STREAMS; i++) {
		mutex_init(&isys->streams[i].mutex);
		init_completion(&isys->streams[i].stream_open_completion);
		init_completion(&isys->streams[i].stream_close_completion);
		init_completion(&isys->streams[i].stream_start_completion);
		init_completion(&isys->streams[i].stream_stop_completion);
		INIT_LIST_HEAD(&isys->streams[i].queues);
		isys->streams[i].isys = isys;
		isys->streams[i].stream_handle = i;
		isys->streams[i].vc = INVALID_VC_ID;
	}
}

static int isys_fw_log_init(struct ipu7_isys *isys)
{
	struct device *dev = &isys->adev->auxdev.dev;
	struct isys_fw_log *fw_log;
	void *log_buf;

	if (isys->fw_log)
		return 0;

	fw_log = devm_kzalloc(dev, sizeof(*fw_log), GFP_KERNEL);
	if (!fw_log)
		return -ENOMEM;

	mutex_init(&fw_log->mutex);

	log_buf = devm_kzalloc(dev, FW_LOG_BUF_SIZE, GFP_KERNEL);
	if (!log_buf)
		return -ENOMEM;

	fw_log->head = log_buf;
	fw_log->addr = log_buf;
	fw_log->count = 0;
	fw_log->size = 0;

	isys->fw_log = fw_log;

	return 0;
}

/* The .bound() notifier callback when a match is found */
static int isys_notifier_bound(struct v4l2_async_notifier *notifier,
			       struct v4l2_subdev *sd,
			       struct v4l2_async_connection *asc)
{
	struct ipu7_isys *isys = container_of(notifier,
					      struct ipu7_isys, notifier);
	struct sensor_async_sd *s_asd =
		container_of(asc, struct sensor_async_sd, asc);
	struct device *dev = &isys->adev->auxdev.dev;
	int ret;

	ret = ipu_bridge_instantiate_vcm(sd->dev);
	if (ret) {
		dev_err(dev, "instantiate vcm failed\n");
		return ret;
	}

	dev_info(dev, "bind %s nlanes is %d port is %d\n",
		 sd->name, s_asd->csi2.nlanes, s_asd->csi2.port);
	isys_complete_ext_device_registration(isys, sd, &s_asd->csi2);

	return v4l2_device_register_subdev_nodes(&isys->v4l2_dev);
}

static int isys_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct ipu7_isys *isys = container_of(notifier,
					      struct ipu7_isys, notifier);

	dev_info(&isys->adev->auxdev.dev,
		 "All sensor registration completed.\n");

	return v4l2_device_register_subdev_nodes(&isys->v4l2_dev);
}

static const struct v4l2_async_notifier_operations isys_async_ops = {
	.bound = isys_notifier_bound,
	.complete = isys_notifier_complete,
};

static int isys_notifier_init(struct ipu7_isys *isys)
{
	const struct ipu7_isys_internal_csi2_pdata *csi2 =
		&isys->pdata->ipdata->csi2;
	struct ipu7_device *isp = isys->adev->isp;
	struct device *dev = &isp->pdev->dev;
	unsigned int i;
	int ret;

	v4l2_async_nf_init(&isys->notifier, &isys->v4l2_dev);

	for (i = 0; i < csi2->nports; i++) {
		struct v4l2_fwnode_endpoint vep = {
			.bus_type = V4L2_MBUS_UNKNOWN
		};
		struct sensor_async_sd *s_asd;
		struct fwnode_handle *ep;

		ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), i, 0,
						     FWNODE_GRAPH_ENDPOINT_NEXT);
		if (!ep)
			continue;

		ret = v4l2_fwnode_endpoint_parse(ep, &vep);
		if (ret)
			goto err_parse;

		if (vep.bus_type != V4L2_MBUS_CSI2_DPHY &&
		    vep.bus_type != V4L2_MBUS_CSI2_CPHY) {
			ret = -EINVAL;
			dev_err(dev, "unsupported bus type %d!\n",
				vep.bus_type);
			goto err_parse;
		}

		s_asd = v4l2_async_nf_add_fwnode_remote(&isys->notifier, ep,
							struct
							sensor_async_sd);
		if (IS_ERR(s_asd)) {
			ret = PTR_ERR(s_asd);
			goto err_parse;
		}

		s_asd->csi2.port = vep.base.port;
		s_asd->csi2.nlanes = vep.bus.mipi_csi2.num_data_lanes;
		s_asd->csi2.bus_type = vep.bus_type;

		fwnode_handle_put(ep);

		continue;

err_parse:
		fwnode_handle_put(ep);
		return ret;
	}

	if (list_empty(&isys->notifier.waiting_list)) {
		/* isys probe could continue with async subdevs missing */
		dev_warn(dev, "no subdev found in graph\n");
		return 0;
	}

	isys->notifier.ops = &isys_async_ops;
	ret = v4l2_async_nf_register(&isys->notifier);
	if (ret) {
		dev_err(dev, "failed to register async notifier(%d)\n", ret);
		v4l2_async_nf_cleanup(&isys->notifier);
	}

	return ret;
}

static void isys_notifier_cleanup(struct ipu7_isys *isys)
{
	v4l2_async_nf_unregister(&isys->notifier);
	v4l2_async_nf_cleanup(&isys->notifier);
}

static void isys_unregister_video_devices(struct ipu7_isys *isys)
{
	const struct ipu7_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	unsigned int i, j;

	for (i = 0; i < csi2_pdata->nports; i++)
		for (j = 0; j < IPU7_NR_OF_CSI2_SRC_PADS; j++)
			ipu7_isys_video_cleanup(&isys->csi2[i].av[j]);
}

static int isys_register_video_devices(struct ipu7_isys *isys)
{
	const struct ipu7_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	unsigned int i, j;
	int ret;

	for (i = 0; i < csi2_pdata->nports; i++) {
		for (j = 0; j < IPU7_NR_OF_CSI2_SRC_PADS; j++) {
			struct ipu7_isys_video *av = &isys->csi2[i].av[j];

			snprintf(av->vdev.name, sizeof(av->vdev.name),
				 IPU_ISYS_ENTITY_PREFIX " ISYS Capture %u",
				 i * IPU7_NR_OF_CSI2_SRC_PADS + j);
			av->isys = isys;
			av->aq.vbq.buf_struct_size =
				sizeof(struct ipu7_isys_video_buffer);

			ret = ipu7_isys_video_init(av);
			if (ret)
				goto fail;
		}
	}

	return 0;

fail:
	i = i + 1U;
	while (i--) {
		while (j--)
			ipu7_isys_video_cleanup(&isys->csi2[i].av[j]);
		j = IPU7_NR_OF_CSI2_SRC_PADS;
	}

	return ret;
}

static void isys_csi2_unregister_subdevices(struct ipu7_isys *isys)
{
	const struct ipu7_isys_internal_csi2_pdata *csi2 =
		&isys->pdata->ipdata->csi2;
	unsigned int i;

	for (i = 0; i < csi2->nports; i++)
		ipu7_isys_csi2_cleanup(&isys->csi2[i]);
}

static int isys_csi2_register_subdevices(struct ipu7_isys *isys)
{
	const struct ipu7_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	unsigned int i;
	int ret;

	for (i = 0; i < csi2_pdata->nports; i++) {
		ret = ipu7_isys_csi2_init(&isys->csi2[i], isys,
					  isys->pdata->base +
					  csi2_pdata->offsets[i], i);
		if (ret)
			goto fail;
	}

	isys->isr_csi2_mask = IPU7_CSI_RX_LEGACY_IRQ_MASK;

	return 0;

fail:
	while (i--)
		ipu7_isys_csi2_cleanup(&isys->csi2[i]);

	return ret;
}

static int isys_csi2_create_media_links(struct ipu7_isys *isys)
{
	const struct ipu7_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	struct device *dev = &isys->adev->auxdev.dev;
	struct media_entity *sd;
	unsigned int i, j;
	int ret;

	for (i = 0; i < csi2_pdata->nports; i++) {
		sd = &isys->csi2[i].asd.sd.entity;

		for (j = 0; j < IPU7_NR_OF_CSI2_SRC_PADS; j++) {
			struct ipu7_isys_video *av = &isys->csi2[i].av[j];

			ret = media_create_pad_link(sd, IPU7_CSI2_PAD_SRC + j,
						    &av->vdev.entity, 0, 0);
			if (ret) {
				dev_err(dev, "CSI2 can't create link\n");
				return ret;
			}

			av->csi2 = &isys->csi2[i];
		}
	}

	return 0;
}

static int isys_register_devices(struct ipu7_isys *isys)
{
	struct device *dev = &isys->adev->auxdev.dev;
	struct pci_dev *pdev = isys->adev->isp->pdev;
	int ret;

	media_device_pci_init(&isys->media_dev,
			      pdev, IPU_MEDIA_DEV_MODEL_NAME);

	strscpy(isys->v4l2_dev.name, isys->media_dev.model,
		sizeof(isys->v4l2_dev.name));

	ret = media_device_register(&isys->media_dev);
	if (ret < 0)
		goto out_media_device_unregister;

	isys->v4l2_dev.mdev = &isys->media_dev;
	isys->v4l2_dev.ctrl_handler = NULL;

	ret = v4l2_device_register(dev, &isys->v4l2_dev);
	if (ret < 0)
		goto out_media_device_unregister;

	ret = isys_register_video_devices(isys);
	if (ret)
		goto out_v4l2_device_unregister;

	ret = isys_csi2_register_subdevices(isys);
	if (ret)
		goto out_video_unregister_device;

	ret = isys_csi2_create_media_links(isys);
	if (ret)
		goto out_csi2_unregister_subdevices;

	ret = isys_notifier_init(isys);
	if (ret)
		goto out_csi2_unregister_subdevices;

	return 0;

out_csi2_unregister_subdevices:
	isys_csi2_unregister_subdevices(isys);

out_video_unregister_device:
	isys_unregister_video_devices(isys);

out_v4l2_device_unregister:
	v4l2_device_unregister(&isys->v4l2_dev);

out_media_device_unregister:
	media_device_unregister(&isys->media_dev);
	media_device_cleanup(&isys->media_dev);

	dev_err(dev, "failed to register isys devices\n");

	return ret;
}

static void isys_unregister_devices(struct ipu7_isys *isys)
{
	isys_unregister_video_devices(isys);
	isys_csi2_unregister_subdevices(isys);
	v4l2_device_unregister(&isys->v4l2_dev);
	media_device_unregister(&isys->media_dev);
	media_device_cleanup(&isys->media_dev);
}

static void enable_csi2_legacy_irq(struct ipu7_isys *isys, bool enable)
{
	u32 offset = IS_IO_CSI2_LEGACY_IRQ_CTRL_BASE;
	void __iomem *base = isys->pdata->base;
	u32 mask = isys->isr_csi2_mask;

	if (!enable) {
		writel(mask, base + offset + IRQ_CTL_CLEAR);
		writel(0, base + offset + IRQ_CTL_ENABLE);
		return;
	}

	writel(mask, base + offset + IRQ_CTL_EDGE);
	writel(mask, base + offset + IRQ_CTL_CLEAR);
	writel(mask, base + offset + IRQ_CTL_MASK);
	writel(mask, base + offset + IRQ_CTL_ENABLE);
}

static void enable_to_sw_irq(struct ipu7_isys *isys, bool enable)
{
	void __iomem *base = isys->pdata->base;
	u32 mask = IS_UC_TO_SW_IRQ_MASK;
	u32 offset = IS_UC_CTRL_BASE;

	if (!enable) {
		writel(0, base + offset + TO_SW_IRQ_CNTL_ENABLE);
		return;
	}

	writel(mask, base + offset + TO_SW_IRQ_CNTL_CLEAR);
	writel(mask, base + offset + TO_SW_IRQ_CNTL_MASK_N);
	writel(mask, base + offset + TO_SW_IRQ_CNTL_ENABLE);
}

void ipu7_isys_setup_hw(struct ipu7_isys *isys)
{
	u32 offset;
	void __iomem *base = isys->pdata->base;

	/* soft reset */
	offset = IS_IO_GPREGS_BASE;

	writel(0x0, base + offset + CLK_EN_TXCLKESC);
	/* Update if ISYS freq updated (0: 400/1, 1:400/2, 63:400/64) */
	writel(0x0, base + offset + CLK_DIV_FACTOR_IS_CLK);
	/* correct the initial printf configuration */
	writel(0x200, base + IS_UC_CTRL_BASE + PRINTF_AXI_CNTL);

	enable_to_sw_irq(isys, 1);
	enable_csi2_legacy_irq(isys, 1);
}

static void isys_cleanup_hw(struct ipu7_isys *isys)
{
	enable_csi2_legacy_irq(isys, 0);
	enable_to_sw_irq(isys, 0);
}

static int isys_runtime_pm_resume(struct device *dev)
{
	struct ipu7_bus_device *adev = to_ipu7_bus_device(dev);
	struct ipu7_isys *isys = ipu7_bus_get_drvdata(adev);
	struct ipu7_device *isp = adev->isp;
	unsigned long flags;
	int ret;

	if (!isys)
		return 0;

	ret = ipu7_mmu_hw_init(adev->mmu);
	if (ret)
		return ret;

	cpu_latency_qos_update_request(&isys->pm_qos, ISYS_PM_QOS_VALUE);

	ret = ipu_buttress_start_tsc_sync(isp);
	if (ret)
		return ret;

	spin_lock_irqsave(&isys->power_lock, flags);
	isys->power = 1;
	spin_unlock_irqrestore(&isys->power_lock, flags);

	return 0;
}

static int isys_runtime_pm_suspend(struct device *dev)
{
	struct ipu7_bus_device *adev = to_ipu7_bus_device(dev);
	struct ipu7_isys *isys = ipu7_bus_get_drvdata(adev);
	unsigned long flags;

	if (!isys)
		return 0;

	isys_cleanup_hw(isys);

	spin_lock_irqsave(&isys->power_lock, flags);
	isys->power = 0;
	spin_unlock_irqrestore(&isys->power_lock, flags);

	cpu_latency_qos_update_request(&isys->pm_qos, PM_QOS_DEFAULT_VALUE);

	ipu7_mmu_hw_cleanup(adev->mmu);

	return 0;
}

static int isys_suspend(struct device *dev)
{
	struct ipu7_isys *isys = dev_get_drvdata(dev);

	/* If stream is open, refuse to suspend */
	if (isys->stream_opened)
		return -EBUSY;

	return 0;
}

static int isys_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops isys_pm_ops = {
	.runtime_suspend = isys_runtime_pm_suspend,
	.runtime_resume = isys_runtime_pm_resume,
	.suspend = isys_suspend,
	.resume = isys_resume,
};

static void isys_remove(struct auxiliary_device *auxdev)
{
	struct ipu7_isys *isys = dev_get_drvdata(&auxdev->dev);
	struct isys_fw_msgs *fwmsg, *safe;
	struct ipu7_bus_device *adev = auxdev_to_adev(auxdev);

	for (int i = 0; i < IPU_ISYS_MAX_STREAMS; i++)
		mutex_destroy(&isys->streams[i].mutex);

	list_for_each_entry_safe(fwmsg, safe, &isys->framebuflist, head)
		ipu7_dma_free(adev, sizeof(struct isys_fw_msgs),
			      fwmsg, fwmsg->dma_addr, 0);

	list_for_each_entry_safe(fwmsg, safe, &isys->framebuflist_fw, head)
		ipu7_dma_free(adev, sizeof(struct isys_fw_msgs),
			      fwmsg, fwmsg->dma_addr, 0);

	isys_notifier_cleanup(isys);
	isys_unregister_devices(isys);

	cpu_latency_qos_remove_request(&isys->pm_qos);

	mutex_destroy(&isys->stream_mutex);
	mutex_destroy(&isys->mutex);
}

static int alloc_fw_msg_bufs(struct ipu7_isys *isys, int amount)
{
	struct ipu7_bus_device *adev = isys->adev;
	struct isys_fw_msgs *addr;
	dma_addr_t dma_addr;
	unsigned long flags;
	unsigned int i;

	for (i = 0; i < amount; i++) {
		addr = ipu7_dma_alloc(adev, sizeof(struct isys_fw_msgs),
				      &dma_addr, GFP_KERNEL, 0);
		if (!addr)
			break;
		addr->dma_addr = dma_addr;

		spin_lock_irqsave(&isys->listlock, flags);
		list_add(&addr->head, &isys->framebuflist);
		spin_unlock_irqrestore(&isys->listlock, flags);
	}

	if (i == amount)
		return 0;

	spin_lock_irqsave(&isys->listlock, flags);
	while (!list_empty(&isys->framebuflist)) {
		addr = list_first_entry(&isys->framebuflist,
					struct isys_fw_msgs, head);
		list_del(&addr->head);
		spin_unlock_irqrestore(&isys->listlock, flags);
		ipu7_dma_free(adev, sizeof(struct isys_fw_msgs),
			      addr, addr->dma_addr, 0);
		spin_lock_irqsave(&isys->listlock, flags);
	}
	spin_unlock_irqrestore(&isys->listlock, flags);

	return -ENOMEM;
}

struct isys_fw_msgs *ipu7_get_fw_msg_buf(struct ipu7_isys_stream *stream)
{
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu7_isys *isys = stream->isys;
	struct isys_fw_msgs *msg;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&isys->listlock, flags);
	if (list_empty(&isys->framebuflist)) {
		spin_unlock_irqrestore(&isys->listlock, flags);
		dev_dbg(dev, "Frame buffer list empty\n");

		ret = alloc_fw_msg_bufs(isys, 5);
		if (ret < 0)
			return NULL;

		spin_lock_irqsave(&isys->listlock, flags);
		if (list_empty(&isys->framebuflist)) {
			spin_unlock_irqrestore(&isys->listlock, flags);
			dev_err(dev, "Frame list empty\n");
			return NULL;
		}
	}
	msg = list_last_entry(&isys->framebuflist, struct isys_fw_msgs, head);
	list_move(&msg->head, &isys->framebuflist_fw);
	spin_unlock_irqrestore(&isys->listlock, flags);
	memset(&msg->fw_msg, 0, sizeof(msg->fw_msg));

	return msg;
}

void ipu7_cleanup_fw_msg_bufs(struct ipu7_isys *isys)
{
	struct isys_fw_msgs *fwmsg, *fwmsg0;
	unsigned long flags;

	spin_lock_irqsave(&isys->listlock, flags);
	list_for_each_entry_safe(fwmsg, fwmsg0, &isys->framebuflist_fw, head)
		list_move(&fwmsg->head, &isys->framebuflist);
	spin_unlock_irqrestore(&isys->listlock, flags);
}

void ipu7_put_fw_msg_buf(struct ipu7_isys *isys, uintptr_t data)
{
	struct isys_fw_msgs *msg;
	void *ptr = (void *)data;
	unsigned long flags;

	if (WARN_ON_ONCE(!ptr))
		return;

	spin_lock_irqsave(&isys->listlock, flags);
	msg = container_of(ptr, struct isys_fw_msgs, fw_msg.dummy);
	list_move(&msg->head, &isys->framebuflist);
	spin_unlock_irqrestore(&isys->listlock, flags);
}

static int isys_probe(struct auxiliary_device *auxdev,
		      const struct auxiliary_device_id *auxdev_id)
{
	const struct ipu7_isys_internal_csi2_pdata *csi2_pdata;
	struct ipu7_bus_device *adev = auxdev_to_adev(auxdev);
	struct ipu7_device *isp = adev->isp;
	struct ipu7_isys *isys;
	int ret = 0;

	if (!isp->ipu7_bus_ready_to_probe)
		return -EPROBE_DEFER;

	isys = devm_kzalloc(&auxdev->dev, sizeof(*isys), GFP_KERNEL);
	if (!isys)
		return -ENOMEM;

	ret = pm_runtime_resume_and_get(&auxdev->dev);
	if (ret < 0)
		return ret;

	adev->auxdrv_data =
		(const struct ipu7_auxdrv_data *)auxdev_id->driver_data;
	adev->auxdrv = to_auxiliary_drv(auxdev->dev.driver);
	isys->adev = adev;
	isys->pdata = adev->pdata;

	INIT_LIST_HEAD(&isys->requests);
	csi2_pdata = &isys->pdata->ipdata->csi2;

	isys->csi2 = devm_kcalloc(&auxdev->dev, csi2_pdata->nports,
				  sizeof(*isys->csi2), GFP_KERNEL);
	if (!isys->csi2) {
		ret = -ENOMEM;
		goto out_runtime_put;
	}

	ret = ipu7_mmu_hw_init(adev->mmu);
	if (ret)
		goto out_runtime_put;

	spin_lock_init(&isys->streams_lock);
	spin_lock_init(&isys->power_lock);
	isys->power = 0;

	mutex_init(&isys->mutex);
	mutex_init(&isys->stream_mutex);

	spin_lock_init(&isys->listlock);
	INIT_LIST_HEAD(&isys->framebuflist);
	INIT_LIST_HEAD(&isys->framebuflist_fw);

	dev_set_drvdata(&auxdev->dev, isys);

	isys->icache_prefetch = 0;
	isys->phy_rext_cal = 0;

	isys_stream_init(isys);

	cpu_latency_qos_add_request(&isys->pm_qos, PM_QOS_DEFAULT_VALUE);
	ret = alloc_fw_msg_bufs(isys, 20);
	if (ret < 0)
		goto out_cleanup_isys;

	ret = ipu7_fw_isys_init(isys);
	if (ret)
		goto out_cleanup_isys;

	ret = isys_register_devices(isys);
	if (ret)
		goto out_cleanup_fw;

	ret = isys_fw_log_init(isys);
	if (ret)
		goto out_cleanup;

	ipu7_mmu_hw_cleanup(adev->mmu);
	pm_runtime_put(&auxdev->dev);

	return 0;

out_cleanup:
	isys_unregister_devices(isys);
out_cleanup_fw:
	ipu7_fw_isys_release(isys);
out_cleanup_isys:
	cpu_latency_qos_remove_request(&isys->pm_qos);

	for (unsigned int i = 0; i < IPU_ISYS_MAX_STREAMS; i++)
		mutex_destroy(&isys->streams[i].mutex);

	mutex_destroy(&isys->mutex);
	mutex_destroy(&isys->stream_mutex);

	ipu7_mmu_hw_cleanup(adev->mmu);

out_runtime_put:
	pm_runtime_put(&auxdev->dev);

	return ret;
}

struct ipu7_csi2_error {
	const char *error_string;
	bool is_info_only;
};

/*
 * Strings corresponding to CSI-2 receiver errors are here.
 * Corresponding macros are defined in the header file.
 */
static const struct ipu7_csi2_error dphy_rx_errors[] = {
	{ "Error handler FIFO full", false },
	{ "Reserved Short Packet encoding detected", true },
	{ "Reserved Long Packet encoding detected", true },
	{ "Received packet is too short", false},
	{ "Received packet is too long", false},
	{ "Short packet discarded due to errors", false },
	{ "Long packet discarded due to errors", false },
	{ "CSI Combo Rx interrupt", false },
	{ "IDI CDC FIFO overflow(remaining bits are reserved as 0)", false },
	{ "Received NULL packet", true },
	{ "Received blanking packet", true },
	{ "Tie to 0", true },
	{ }
};

static void ipu7_isys_register_errors(struct ipu7_isys_csi2 *csi2)
{
	u32 offset = IS_IO_CSI2_ERR_LEGACY_IRQ_CTL_BASE(csi2->port);
	u32 status = readl(csi2->base + offset + IRQ_CTL_STATUS);
	u32 mask = IPU7_CSI_RX_ERROR_IRQ_MASK;

	if (!status)
		return;

	dev_dbg(&csi2->isys->adev->auxdev.dev, "csi2-%u error status 0x%08x\n",
		csi2->port, status);

	writel(status & mask, csi2->base + offset + IRQ_CTL_CLEAR);
	csi2->receiver_errors |= status & mask;
}

static void ipu7_isys_csi2_error(struct ipu7_isys_csi2 *csi2)
{
	struct ipu7_csi2_error const *errors;
	unsigned int i;
	u32 status;

	/* Register errors once more in case of error interrupts are disabled */
	ipu7_isys_register_errors(csi2);
	status = csi2->receiver_errors;
	csi2->receiver_errors = 0;
	errors = dphy_rx_errors;

	for (i = 0; i < CSI_RX_NUM_ERRORS_IN_IRQ; i++) {
		if (status & BIT(i))
			dev_err_ratelimited(&csi2->isys->adev->auxdev.dev,
					    "csi2-%i error: %s\n",
					    csi2->port,
					    errors[i].error_string);
	}
}

struct resp_to_msg {
	enum ipu7_insys_resp_type type;
	const char *msg;
};

static const struct resp_to_msg is_fw_msg[] = {
	{IPU_INSYS_RESP_TYPE_STREAM_OPEN_DONE,
	 "IPU_INSYS_RESP_TYPE_STREAM_OPEN_DONE"},
	{IPU_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_ACK,
	 "IPU_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_ACK"},
	{IPU_INSYS_RESP_TYPE_STREAM_CAPTURE_ACK,
	 "IPU_INSYS_RESP_TYPE_STREAM_CAPTURE_ACK"},
	{IPU_INSYS_RESP_TYPE_STREAM_ABORT_ACK,
	 "IPU_INSYS_RESP_TYPE_STREAM_ABORT_ACK"},
	{IPU_INSYS_RESP_TYPE_STREAM_FLUSH_ACK,
	 "IPU_INSYS_RESP_TYPE_STREAM_FLUSH_ACK"},
	{IPU_INSYS_RESP_TYPE_STREAM_CLOSE_ACK,
	 "IPU_INSYS_RESP_TYPE_STREAM_CLOSE_ACK"},
	{IPU_INSYS_RESP_TYPE_PIN_DATA_READY,
	 "IPU_INSYS_RESP_TYPE_PIN_DATA_READY"},
	{IPU_INSYS_RESP_TYPE_FRAME_SOF, "IPU_INSYS_RESP_TYPE_FRAME_SOF"},
	{IPU_INSYS_RESP_TYPE_FRAME_EOF, "IPU_INSYS_RESP_TYPE_FRAME_EOF"},
	{IPU_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_DONE,
	 "IPU_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_DONE"},
	{IPU_INSYS_RESP_TYPE_STREAM_CAPTURE_DONE,
	 "IPU_INSYS_RESP_TYPE_STREAM_CAPTURE_DONE"},
	{N_IPU_INSYS_RESP_TYPE, "N_IPU_INSYS_RESP_TYPE"},
};

int isys_isr_one(struct ipu7_bus_device *adev)
{
	struct ipu7_isys *isys = ipu7_bus_get_drvdata(adev);
	struct ipu7_isys_stream *stream = NULL;
	struct device *dev = &adev->auxdev.dev;
	struct ipu7_isys_csi2 *csi2 = NULL;
	struct ia_gofo_msg_err err_info;
	struct ipu7_insys_resp *resp;
	u64 ts;

	if (!isys->adev->syscom)
		return 1;

	resp = ipu7_fw_isys_get_resp(isys);
	if (!resp)
		return 1;
	if (resp->type >= N_IPU_INSYS_RESP_TYPE) {
		dev_err(dev, "Unknown response type %u stream %u\n",
			resp->type, resp->stream_id);
		ipu7_fw_isys_put_resp(isys);
		return 1;
	}

	err_info = resp->error_info;
	ts = ((u64)resp->timestamp[1] << 32) | resp->timestamp[0];
	if (err_info.err_group == INSYS_MSG_ERR_GROUP_CAPTURE &&
	    err_info.err_code == INSYS_MSG_ERR_CAPTURE_SYNC_FRAME_DROP) {
		/* receive a sp w/o command, firmware drop it */
		dev_dbg(dev, "FRAME DROP: %02u %s stream %u\n",
			resp->type, is_fw_msg[resp->type].msg,
			resp->stream_id);
		dev_dbg(dev, "\tpin %u buf_id %llx frame %u\n",
			resp->pin_id, resp->buf_id, resp->frame_id);
		dev_dbg(dev, "\terror group %u code %u details [%u %u]\n",
			err_info.err_group, err_info.err_code,
			err_info.err_detail[0], err_info.err_detail[1]);
	} else if (!IA_GOFO_MSG_ERR_IS_OK(err_info)) {
		dev_err(dev, "%02u %s stream %u pin %u buf_id %llx frame %u\n",
			resp->type, is_fw_msg[resp->type].msg, resp->stream_id,
			resp->pin_id, resp->buf_id, resp->frame_id);
		dev_err(dev, "\terror group %u code %u details [%u %u]\n",
			err_info.err_group, err_info.err_code,
			err_info.err_detail[0], err_info.err_detail[1]);
	} else {
		dev_dbg(dev, "%02u %s stream %u pin %u buf_id %llx frame %u\n",
			resp->type, is_fw_msg[resp->type].msg, resp->stream_id,
			resp->pin_id, resp->buf_id, resp->frame_id);
		dev_dbg(dev, "\tts %llu\n", ts);
	}

	if (resp->stream_id >= IPU_ISYS_MAX_STREAMS) {
		dev_err(dev, "bad stream handle %u\n",
			resp->stream_id);
		goto leave;
	}

	stream = ipu7_isys_query_stream_by_handle(isys, resp->stream_id);
	if (!stream) {
		dev_err(dev, "stream of stream_handle %u is unused\n",
			resp->stream_id);
		goto leave;
	}

	stream->error = err_info.err_code;

	if (stream->asd)
		csi2 = ipu7_isys_subdev_to_csi2(stream->asd);

	switch (resp->type) {
	case IPU_INSYS_RESP_TYPE_STREAM_OPEN_DONE:
		complete(&stream->stream_open_completion);
		break;
	case IPU_INSYS_RESP_TYPE_STREAM_CLOSE_ACK:
		complete(&stream->stream_close_completion);
		break;
	case IPU_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_ACK:
		complete(&stream->stream_start_completion);
		break;
	case IPU_INSYS_RESP_TYPE_STREAM_ABORT_ACK:
		complete(&stream->stream_stop_completion);
		break;
	case IPU_INSYS_RESP_TYPE_STREAM_FLUSH_ACK:
		complete(&stream->stream_stop_completion);
		break;
	case IPU_INSYS_RESP_TYPE_PIN_DATA_READY:
		/*
		 * firmware only release the capture msg until software
		 * get pin_data_ready event
		 */
		ipu7_put_fw_msg_buf(ipu7_bus_get_drvdata(adev), resp->buf_id);
		if (resp->pin_id < IPU_INSYS_OUTPUT_PINS &&
		    stream->output_pins[resp->pin_id].pin_ready)
			stream->output_pins[resp->pin_id].pin_ready(stream,
								    resp);
		else
			dev_err(dev, "No handler for pin %u ready\n",
				resp->pin_id);
		if (csi2)
			ipu7_isys_csi2_error(csi2);

		break;
	case IPU_INSYS_RESP_TYPE_STREAM_CAPTURE_ACK:
		break;
	case IPU_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_DONE:
	case IPU_INSYS_RESP_TYPE_STREAM_CAPTURE_DONE:
		break;
	case IPU_INSYS_RESP_TYPE_FRAME_SOF:
		if (csi2)
			ipu7_isys_csi2_sof_event_by_stream(stream);

		stream->seq[stream->seq_index].sequence =
			atomic_read(&stream->sequence) - 1U;
		stream->seq[stream->seq_index].timestamp = ts;
		dev_dbg(dev,
			"SOF: stream %u frame %u (index %u), ts 0x%16.16llx\n",
			resp->stream_id, resp->frame_id,
			stream->seq[stream->seq_index].sequence, ts);
		stream->seq_index = (stream->seq_index + 1U)
			% IPU_ISYS_MAX_PARALLEL_SOF;
		break;
	case IPU_INSYS_RESP_TYPE_FRAME_EOF:
		if (csi2)
			ipu7_isys_csi2_eof_event_by_stream(stream);

		dev_dbg(dev, "eof: stream %d(index %u) ts 0x%16.16llx\n",
			resp->stream_id,
			stream->seq[stream->seq_index].sequence, ts);
		break;
	default:
		dev_err(dev, "Unknown response type %u stream %u\n",
			resp->type, resp->stream_id);
		break;
	}

	ipu7_isys_put_stream(stream);
leave:
	ipu7_fw_isys_put_resp(isys);

	return 0;
}

static void ipu7_isys_csi2_isr(struct ipu7_isys_csi2 *csi2)
{
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	struct ipu7_device *isp = csi2->isys->adev->isp;
	struct ipu7_isys_stream *s;
	u32 sync, offset;
	u32 fe = 0;
	u8 vc;

	ipu7_isys_register_errors(csi2);

	offset = IS_IO_CSI2_SYNC_LEGACY_IRQ_CTL_BASE(csi2->port);
	sync = readl(csi2->base + offset + IRQ_CTL_STATUS);
	writel(sync, csi2->base + offset + IRQ_CTL_CLEAR);
	dev_dbg(dev, "csi2-%u sync status 0x%08x\n", csi2->port, sync);

	if (!is_ipu7(isp->hw_ver)) {
		fe = readl(csi2->base + offset + IRQ1_CTL_STATUS);
		writel(fe, csi2->base + offset + IRQ1_CTL_CLEAR);
		dev_dbg(dev, "csi2-%u FE status 0x%08x\n", csi2->port, fe);
	}

	for (vc = 0; vc < IPU7_NR_OF_CSI2_VC && (sync || fe); vc++) {
		s = ipu7_isys_query_stream_by_source(csi2->isys,
						     csi2->asd.source, vc);
		if (!s)
			continue;

		if (!is_ipu7(isp->hw_ver)) {
			if (sync & IPU7P5_CSI_RX_SYNC_FS_VC & (1U << vc))
				ipu7_isys_csi2_sof_event_by_stream(s);

			if (fe & IPU7P5_CSI_RX_SYNC_FE_VC & (1U << vc))
				ipu7_isys_csi2_eof_event_by_stream(s);
		} else {
			if (sync & IPU7_CSI_RX_SYNC_FS_VC & (1U << (vc * 2)))
				ipu7_isys_csi2_sof_event_by_stream(s);

			if (sync & IPU7_CSI_RX_SYNC_FE_VC & (2U << (vc * 2)))
				ipu7_isys_csi2_eof_event_by_stream(s);
		}
	}
}

static irqreturn_t isys_isr(struct ipu7_bus_device *adev)
{
	struct ipu7_isys *isys = ipu7_bus_get_drvdata(adev);
	u32 status_csi, status_sw, csi_offset, sw_offset;
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *base = isys->pdata->base;

	spin_lock(&isys->power_lock);
	if (!isys->power) {
		spin_unlock(&isys->power_lock);
		return IRQ_NONE;
	}

	csi_offset = IS_IO_CSI2_LEGACY_IRQ_CTRL_BASE;
	sw_offset = IS_BASE;

	status_csi = readl(base + csi_offset + IRQ_CTL_STATUS);
	status_sw = readl(base + sw_offset + TO_SW_IRQ_CNTL_STATUS);
	if (!status_csi && !status_sw) {
		spin_unlock(&isys->power_lock);
		return IRQ_NONE;
	}

	if (status_csi)
		dev_dbg(dev, "status csi 0x%08x\n", status_csi);
	if (status_sw)
		dev_dbg(dev, "status to_sw 0x%08x\n", status_sw);

	do {
		writel(status_sw, base + sw_offset + TO_SW_IRQ_CNTL_CLEAR);
		writel(status_csi, base + csi_offset + IRQ_CTL_CLEAR);

		if (isys->isr_csi2_mask & status_csi) {
			unsigned int i;

			for (i = 0; i < isys->pdata->ipdata->csi2.nports; i++) {
				/* irq from not enabled port */
				if (!isys->csi2[i].base)
					continue;
				if (status_csi & isys->csi2[i].legacy_irq_mask)
					ipu7_isys_csi2_isr(&isys->csi2[i]);
			}
		}

		if (!isys_isr_one(adev))
			status_sw = TO_SW_IRQ_FW;
		else
			status_sw = 0;

		status_csi = readl(base + csi_offset + IRQ_CTL_STATUS);
		status_sw |= readl(base + sw_offset + TO_SW_IRQ_CNTL_STATUS);
	} while ((status_csi & isys->isr_csi2_mask) ||
		 (status_sw & TO_SW_IRQ_FW));

	writel(TO_SW_IRQ_MASK, base + sw_offset + TO_SW_IRQ_CNTL_MASK_N);

	spin_unlock(&isys->power_lock);

	return IRQ_HANDLED;
}

static const struct ipu7_auxdrv_data ipu7_isys_auxdrv_data = {
	.isr = isys_isr,
	.isr_threaded = NULL,
	.wake_isr_thread = false,
};

static const struct auxiliary_device_id ipu7_isys_id_table[] = {
	{
		.name = "intel_ipu7.isys",
		.driver_data = (kernel_ulong_t)&ipu7_isys_auxdrv_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, ipu7_isys_id_table);

static struct auxiliary_driver isys_driver = {
	.name = IPU_ISYS_NAME,
	.probe = isys_probe,
	.remove = isys_remove,
	.id_table = ipu7_isys_id_table,
	.driver = {
		.pm = &isys_pm_ops,
	},
};

module_auxiliary_driver(isys_driver);

MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_AUTHOR("Tianshu Qiu <tian.shu.qiu@intel.com>");
MODULE_AUTHOR("Qingwu Zhang <qingwu.zhang@intel.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel ipu7 input system driver");
MODULE_IMPORT_NS("INTEL_IPU7");
MODULE_IMPORT_NS("INTEL_IPU_BRIDGE");
