// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Rockchip Electronics Co., Ltd. */
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <linux/sem.h>
#include <linux/seq_file.h>
#include <media/v4l2-common.h>

#include "dev.h"
#include "procfs.h"
#include "regs.h"
#include "version.h"

#ifdef CONFIG_PROC_FS
static int ispp_show(struct seq_file *p, void *v)
{
	struct rkispp_device *dev = p->private;
	enum rkispp_state state = dev->ispp_sdev.state;
	struct rkispp_stream *stream;
	u32 val;

	seq_printf(p, "%-10s Version:v%02x.%02x.%02x\n",
		   dev->name,
		   RKISPP_DRIVER_VERSION >> 16,
		   (RKISPP_DRIVER_VERSION & 0xff00) >> 8,
		   RKISPP_DRIVER_VERSION & 0x00ff);
	for (val = 0; val < dev->hw_dev->clks_num; val++) {
		seq_printf(p, "%-10s %ld\n",
			   dev->hw_dev->match_data->clks[val],
			   clk_get_rate(dev->hw_dev->clks[val]));
	}
	if (state != ISPP_START)
		return 0;

	seq_printf(p, "%-10s Cnt:%d ErrCnt:%d\n",
		   "Interrupt",
		   dev->isr_cnt,
		   dev->isr_err_cnt);
	seq_printf(p, "%-10s rkisp%d Format:%s%s Size:%dx%d (frame:%d rate:%dms delay:%dms)\n",
		   "Input",
		   dev->dev_id,
		   (dev->isp_mode & FMT_FBC) ? "FBC" : "YUV",
		   (dev->isp_mode & FMT_YUV422) ? "422" : "420",
		   dev->ispp_sdev.out_fmt.width,
		   dev->ispp_sdev.out_fmt.height,
		   dev->stream_vdev.dbg.id,
		   dev->stream_vdev.dbg.interval / 1000 / 1000,
		   dev->stream_vdev.dbg.delay / 1000 / 1000);
	for (val = STREAM_MB; val <= STREAM_S2; val++) {
		stream = &dev->stream_vdev.stream[val];
		if (!stream->streaming)
			continue;
		seq_printf(p, "%-10s %s Format:%c%c%c%c Size:%dx%d (frame:%d rate:%dms delay:%dms)\n",
			   "Output",
			   stream->vnode.vdev.name,
			   stream->out_fmt.pixelformat,
			   stream->out_fmt.pixelformat >> 8,
			   stream->out_fmt.pixelformat >> 16,
			   stream->out_fmt.pixelformat >> 24,
			   stream->out_fmt.width,
			   stream->out_fmt.height,
			   stream->dbg.id,
			   stream->dbg.interval / 1000 / 1000,
			   stream->dbg.delay / 1000 / 1000);
	}

	val = rkispp_read(dev, RKISPP_TNR_CORE_CTRL);
	seq_printf(p, "%-10s %s(0x%x) (mode: %s) (global gain: %s) (frame:%d time:%dms %s) CNT:0x%x STATE:0x%x\n",
		   "TNR",
		   (val & 1) ? "ON" : "OFF", val,
		   (val & SW_TNR_MODE) ? "3to1" : "2to1",
		   (val & SW_TNR_GLB_GAIN_EN) ? "enable" : "disable",
		   dev->stream_vdev.tnr.dbg.id,
		   dev->stream_vdev.tnr.dbg.interval / 1000 / 1000,
		   dev->stream_vdev.tnr.is_end ? "idle" : "working",
		   rkispp_read(dev, RKISPP_TNR_TILE_CNT),
		   rkispp_read(dev, RKISPP_TNR_STATE));
	val = rkispp_read(dev, RKISPP_NR_UVNR_CTRL_PARA);
	seq_printf(p, "%-10s %s(0x%x) (external gain: %s) (frame:%d time:%dms %s) 0x%x:0x%x 0x%x:0x%x\n",
		   "NR",
		   (val & 1) ? "ON" : "OFF", val,
		   (val & SW_NR_GAIN_BYPASS) ? "disable" : "enable",
		   dev->stream_vdev.nr.dbg.id,
		   dev->stream_vdev.nr.dbg.interval / 1000 / 1000,
		   dev->stream_vdev.nr.is_end ? "idle" : "working",
		   RKISPP_NR_BLOCK_CNT, rkispp_read(dev, RKISPP_NR_BLOCK_CNT),
		   RKISPP_NR_BUFFER_READY, rkispp_read(dev, RKISPP_NR_BUFFER_READY));
	val = rkispp_read(dev, RKISPP_SHARP_CORE_CTRL);
	seq_printf(p, "%-10s %s(0x%x) (YNR input filter: %s) (local ratio: %s) 0x%x:0x%x\n",
		   "SHARP",
		   (val & 1) ? "ON" : "OFF", val,
		   (val & SW_SHP_YIN_FLT_EN) ? "ON" : "OFF",
		   (val & SW_SHP_ALPHA_ADP_EN) ? "ON" : "OFF",
		   RKISPP_SHARP_TILE_IDX, rkispp_read(dev, RKISPP_SHARP_TILE_IDX));
	val = rkispp_read(dev, RKISPP_FEC_CORE_CTRL);
	seq_printf(p, "%-10s %s(0x%x) (frame:%d time:%dms %s) 0x%x:0x%x\n",
		   "FEC",
		   (val & 1) ? "ON" : "OFF", val,
		   dev->stream_vdev.fec.dbg.id,
		   dev->stream_vdev.fec.dbg.interval / 1000 / 1000,
		   dev->stream_vdev.fec.is_end ? "idle" : "working",
		   RKISPP_FEC_DMA_STATUS, rkispp_read(dev, RKISPP_FEC_DMA_STATUS));
	val = rkispp_read(dev, RKISPP_ORB_CORE_CTRL);
	seq_printf(p, "%-10s %s(0x%x)\n",
		   "ORB",
		   (val & 1) ? "ON" : "OFF", val);
	seq_printf(p, "%-10s %s Cnt:%d\n",
		   "Monitor",
		   dev->stream_vdev.monitor.is_en ? "ON" : "OFF",
		   dev->stream_vdev.monitor.retry);
	return 0;
}

static int ispp_open(struct inode *inode, struct file *file)
{
	struct rkispp_device *data = PDE_DATA(inode);

	return single_open(file, ispp_show, data);
}

static const struct file_operations ops = {
	.owner		= THIS_MODULE,
	.open		= ispp_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int rkispp_proc_init(struct rkispp_device *dev)
{
	dev->procfs = proc_create_data(dev->name, 0, NULL, &ops, dev);
	if (!dev->procfs)
		return -EINVAL;
	return 0;
}

void rkispp_proc_cleanup(struct rkispp_device *dev)
{
	if (dev->procfs)
		remove_proc_entry(dev->name, NULL);
	dev->procfs = NULL;
}
#endif /* CONFIG_PROC_FS */
