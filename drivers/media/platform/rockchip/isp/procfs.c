// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Rockchip Electronics Co., Ltd. */
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <linux/sem.h>
#include <linux/seq_file.h>

#include "dev.h"
#include "procfs.h"
#include "version.h"

#ifdef CONFIG_PROC_FS
static int isp_show(struct seq_file *p, void *v)
{
	struct rkisp_device *dev = p->private;
	struct rkisp_isp_subdev *sdev = &dev->isp_sdev;
	struct rkisp_sensor_info *sensor = dev->active_sensor;
	u32 val = 0;

	seq_printf(p, "%-10s Version:v%02x.%02x.%02x\n",
		   dev->name,
		   RKISP_DRIVER_VERSION >> 16,
		   (RKISP_DRIVER_VERSION & 0xff00) >> 8,
		   RKISP_DRIVER_VERSION & 0x00ff);
	if (sensor && sensor->fi.interval.numerator)
		val = sensor->fi.interval.denominator / sensor->fi.interval.numerator;
	seq_printf(p, "%-10s %s Format:%s Size:%dx%d@%dfps Offset(%d,%d) | RDBK_X%d(frame:%d rate:%dms)\n",
		   "Input",
		   sensor ? sensor->sd->name : NULL,
		   sdev->in_fmt.name,
		   sdev->in_crop.width, sdev->in_crop.height, val,
		   sdev->in_crop.left, sdev->in_crop.top,
		   dev->csi_dev.rd_mode - 3,
		   dev->dmarx_dev.cur_frame.id,
		   (u32)(dev->dmarx_dev.cur_frame.timestamp - dev->dmarx_dev.pre_frame.timestamp) / 1000 / 1000);
	seq_printf(p, "%-10s rkispp%d Format:%s%s Size:%dx%d (frame:%d rate:%dms)\n",
		   "Output",
		   dev->dev_id,
		   (dev->br_dev.work_mode & ISP_ISPP_FBC) ? "FBC" : "YUV",
		   (dev->br_dev.work_mode & ISP_ISPP_422) ? "422" : "420",
		   dev->br_dev.crop.width,
		   dev->br_dev.crop.height,
		   dev->br_dev.dbg.id,
		   dev->br_dev.dbg.interval / 1000 / 1000);
	seq_printf(p, "%-10s Cnt:%d ErrCnt:%d\n",
		   "Interrupt",
		   dev->isp_isr_cnt,
		   dev->isp_err_cnt);
	for (val = 0; val < dev->hw_dev->num_clks; val++) {
		seq_printf(p, "%-10s %ld\n",
			   dev->hw_dev->match_data->clks[val],
			   clk_get_rate(dev->hw_dev->clks[val]));
	}
	return 0;
}

static int isp_open(struct inode *inode, struct file *file)
{
	struct rkisp_device *data = PDE_DATA(inode);

	return single_open(file, isp_show, data);
}

static const struct file_operations ops = {
	.owner		= THIS_MODULE,
	.open		= isp_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int rkisp_proc_init(struct rkisp_device *dev)
{
	dev->procfs = proc_create_data(dev->name, 0, NULL, &ops, dev);
	if (!dev->procfs)
		return -EINVAL;
	return 0;
}

void rkisp_proc_cleanup(struct rkisp_device *dev)
{
	if (dev->procfs)
		remove_proc_entry(dev->name, NULL);
	dev->procfs = NULL;
}

#endif /* CONFIG_PROC_FS */
