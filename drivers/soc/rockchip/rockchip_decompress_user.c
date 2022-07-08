// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 *
 * Due to hardware limitations, this module only supports
 * up to 32bit continuous CMA memory.
 *
 * author:
 *	Xiao Yapeng, yp.xiao@rock-chips.com
 * mender:
 *	Lin Jinhan, troy.lin@rock-chips.com
 */

#include <linux/dma-buf.h>
#include <linux/dma-direct.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/soc/rockchip/rockchip_decompress.h>
#include <uapi/linux/rk-decom.h>

#define RK_DECOME_TIMEOUT	3 /* 3 seconds */

struct rk_decom_dev {
	struct miscdevice miscdev;
	struct device *dev;
	struct mutex mutex;
};

static long rk_decom_misc_ioctl(struct file *fptr, unsigned int cmd, unsigned long arg);

static const struct file_operations rk_decom_fops = {
	.owner           = THIS_MODULE,
	.unlocked_ioctl  = rk_decom_misc_ioctl,
};

static struct rk_decom_dev g_rk_decom = {
	.miscdev = {
		.minor = MISC_DYNAMIC_MINOR,
		.name  = RK_DECOM_NAME,
		.fops  = &rk_decom_fops,
	},
};

static bool check_scatter_list(unsigned int max_size, struct sg_table *sg_tbl)
{
	int i;
	unsigned int total_len = 0;
	dma_addr_t next_addr = 0;
	struct scatterlist *sgl = NULL;

	if (!sg_tbl || !(sg_tbl->sgl))
		return false;

	for_each_sgtable_sg(sg_tbl, sgl, i) {
		if  (sg_phys(sgl) > SZ_4G || sg_phys(sgl) + sg_dma_len(sgl) > SZ_4G)
			return false;

		if (i && next_addr != sg_dma_address(sgl))
			return false;

		total_len += sg_dma_len(sgl);

		next_addr = sg_dma_address(sgl) + sg_dma_len(sgl);
	}

	return max_size <= total_len;
}

static int get_dmafd_sgtbl(struct device *dev, int dma_fd, enum dma_data_direction dir,
			   struct sg_table **sg_tbl, struct dma_buf_attachment **dma_attach,
			   struct dma_buf **dmabuf)
{
	int ret = -EINVAL;

	if (!dev)
		return -EINVAL;

	*sg_tbl     = NULL;
	*dmabuf     = NULL;
	*dma_attach = NULL;

	*dmabuf = dma_buf_get(dma_fd);
	if (IS_ERR(*dmabuf)) {
		ret = PTR_ERR(*dmabuf);
		goto error;
	}

	*dma_attach = dma_buf_attach(*dmabuf, dev);
	if (IS_ERR(*dma_attach)) {
		ret = PTR_ERR(*dma_attach);
		goto error;
	}

	*sg_tbl = dma_buf_map_attachment(*dma_attach, dir);
	if (IS_ERR(*sg_tbl)) {
		ret = PTR_ERR(*sg_tbl);
		goto error;
	}

	return 0;
error:
	if (*sg_tbl)
		dma_buf_unmap_attachment(*dma_attach, *sg_tbl, dir);

	if (*dma_attach)
		dma_buf_detach(*dmabuf, *dma_attach);

	if (*dmabuf)
		dma_buf_put(*dmabuf);

	*sg_tbl     = NULL;
	*dmabuf     = NULL;
	*dma_attach = NULL;

	return ret;
}

static int put_dmafd_sgtbl(struct device *dev, int dma_fd, enum dma_data_direction dir,
			   struct sg_table *sg_tbl, struct dma_buf_attachment *dma_attach,
			   struct dma_buf *dmabuf)
{
	if (!dev)
		return -EINVAL;

	if (!sg_tbl || !dma_attach || !dmabuf)
		return -EINVAL;

	dma_buf_unmap_attachment(dma_attach, sg_tbl, dir);
	dma_buf_detach(dmabuf, dma_attach);
	dma_buf_put(dmabuf);

	return 0;
}

static int rk_decom_for_user(struct device *dev, struct rk_decom_param *param)
{
	int ret;
	struct sg_table *sg_tbl_in = NULL, *sg_tbl_out = NULL;
	struct dma_buf *dma_buf_in = NULL, *dma_buf_out = NULL;
	struct dma_buf_attachment *dma_attach_in = NULL, *dma_attach_out = NULL;

	if (param->mode != RK_GZIP_MOD && param->mode != RK_ZLIB_MOD) {
		dev_err(dev, "unsupported mode %u for decompress.\n", param->mode);
		return -EINVAL;
	}

	ret = get_dmafd_sgtbl(dev, param->src_fd, DMA_TO_DEVICE,
			      &sg_tbl_in, &dma_attach_in, &dma_buf_in);
	if (unlikely(ret)) {
		dev_err(dev, "src_fd[%d] get_dmafd_sgtbl error.", (int)param->src_fd);
		goto exit;
	}

	ret = get_dmafd_sgtbl(dev, param->dst_fd, DMA_FROM_DEVICE,
			      &sg_tbl_out, &dma_attach_out, &dma_buf_out);
	if (unlikely(ret)) {
		dev_err(dev, "dst_fd[%d] get_dmafd_sgtbl error.", (int)param->dst_fd);
		goto exit;
	}

	if (!check_scatter_list(0, sg_tbl_in)) {
		dev_err(dev, "Input dma_fd not a continuous buffer.\n");
		ret = -EINVAL;
		goto exit;
	}

	if (!check_scatter_list(param->dst_max_size, sg_tbl_out)) {
		dev_err(dev, "Output dma_fd not a continuous buffer or dst_max_size too big.\n");
		ret = -EINVAL;
		goto exit;
	}

	ret = rk_decom_start(param->mode | DECOM_NOBLOCKING, sg_dma_address(sg_tbl_in->sgl),
			     sg_dma_address(sg_tbl_out->sgl), param->dst_max_size);

	if (ret) {
		dev_err(dev, "rk_decom_start failed[%d].", ret);
		goto exit;
	}

	ret = rk_decom_wait_done(RK_DECOME_TIMEOUT, &param->decom_data_len);

exit:
	if (sg_tbl_in && dma_buf_in && dma_attach_in)
		put_dmafd_sgtbl(dev, param->src_fd, DMA_TO_DEVICE,
				sg_tbl_in, dma_attach_in, dma_buf_in);

	if (sg_tbl_out && dma_buf_out && dma_attach_out)
		put_dmafd_sgtbl(dev, param->dst_fd, DMA_FROM_DEVICE,
				sg_tbl_out, dma_attach_out, dma_buf_out);

	return ret;
}

static long rk_decom_misc_ioctl(struct file *fptr, unsigned int cmd, unsigned long arg)
{
	struct rk_decom_param param;
	struct rk_decom_dev *rk_decom = NULL;
	int ret = -EINVAL;

	rk_decom = container_of(fptr->private_data, struct rk_decom_dev, miscdev);

	mutex_lock(&rk_decom->mutex);

	switch (cmd) {
	case RK_DECOM_USER: {
		ret = copy_from_user((char *)&param, (char *)arg, sizeof(param));
		if (unlikely(ret)) {
			ret = -EFAULT;
			dev_err(rk_decom->dev, "copy from user fail.\n");
			goto exit;
		}

		ret = rk_decom_for_user(rk_decom->dev, &param);

		if (copy_to_user((char *)arg, &param, sizeof(param))) {
			dev_err(rk_decom->dev, " copy to user fail.\n");
			ret = -EFAULT;
			goto exit;
		}

		break;
	}

	default:
		ret = -EINVAL;
		break;
	}

exit:
	mutex_unlock(&rk_decom->mutex);

	return ret;
}

static int __init rk_decom_misc_init(void)
{
	int ret;
	struct rk_decom_dev *rk_decom = &g_rk_decom;
	struct miscdevice *misc = &g_rk_decom.miscdev;

	ret = misc_register(misc);
	if (ret < 0) {
		pr_err("rk_decom: misc device %s register failed[%d].\n", RK_DECOM_NAME, ret);
		goto error;
	}

	rk_decom->dev = misc->this_device;

	/* Save driver private data */
	dev_set_drvdata(rk_decom->dev, rk_decom);

	ret = dma_coerce_mask_and_coherent(misc->this_device, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(rk_decom->dev, "No suitable DMA available.\n");
		goto error;
	}

	mutex_init(&rk_decom->mutex);

	dev_info(rk_decom->dev, "misc device %s register success.\n", RK_DECOM_NAME);

	return 0;
error:
	if (rk_decom->dev)
		misc_deregister(&rk_decom->miscdev);

	return ret;
}

static void __exit rk_decom_misc_exit(void)
{
	misc_deregister(&g_rk_decom.miscdev);
}

module_init(rk_decom_misc_init)
module_exit(rk_decom_misc_exit)

MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("1.0.0");
MODULE_AUTHOR("Xiao Yapeng yp.xiao@rock-chips.com");
MODULE_DESCRIPTION("Rockchip decom driver");
