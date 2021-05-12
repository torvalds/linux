// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Rockchip Electronics Co., Ltd. */
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <linux/sem.h>
#include <linux/seq_file.h>

#include "dev.h"
#include "procfs.h"
#include "version.h"
#include "regs.h"
#include "regs_v2x.h"

#ifdef CONFIG_PROC_FS

static void isp20_show(struct rkisp_device *dev, struct seq_file *p)
{
	u32 full_range_flg = CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA | CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA;
	static const char * const effect[] = {
		"BLACKWHITE",
		"NEGATIVE",
		"SEPIA",
		"COLOR_SEL",
		"EMBOSS",
		"SKETCH",
		"SHARPEN",
		"RKSHARPEN"
	};
	u32 val;

	val = rkisp_read(dev, ISP_DPCC0_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DPCC1_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DPCC2_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_BLS_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BLS", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SDG",
		(val & CIF_ISP_CTRL_ISP_GAMMA_IN_ENA) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_LSC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LSC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (gain: 0x%08x, 0x%08x)\n", "AWBGAIN",
		(val & CIF_ISP_CTRL_ISP_AWB_ENA) ? "ON" : "OFF", val,
		rkisp_read(dev, CIF_ISP_AWB_GAIN_G_V12, false),
		rkisp_read(dev, CIF_ISP_AWB_GAIN_RB_V12, false));
	val = rkisp_read(dev, ISP_DEBAYER_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DEBAYER", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_CCM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CCM", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GAMMA_OUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAMMA_OUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CPROC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CPROC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_IMG_EFF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (effect: %s)\n", "IE",
		(val & 1) ? "ON" : "OFF", val,
		effect[(val & CIF_IMG_EFF_CTRL_MODE_MASK) >> 1]);
	val = rkisp_read(dev, ISP_WDR_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "WDR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HDRTMO_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRTMO", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HDRMGE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRMGE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GIC_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GIC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DHAZ_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DHAZ", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_3DLUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "3DLUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GAIN_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAIN", val ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_LDCH_STS, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LDCH", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CSM",
		(val & full_range_flg) ? "FULL" : "LIMITED", val);

	val = rkisp_read(dev, ISP_AFM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_AWB_PROP_V10, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIAWB",
		(val & CIF_ISP_AWB_ENABLE) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_YUVAE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "YUVAE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HIST_HIST_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIHST", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAWB_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAWB", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAE_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE3", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST3", (val & 1) ? "ON" : "OFF", val);
}

static void isp21_show(struct rkisp_device *dev, struct seq_file *p)
{
	u32 full_range_flg = CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA | CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA;
	static const char * const effect[] = {
		"BLACKWHITE",
		"NEGATIVE",
		"SEPIA",
		"COLOR_SEL",
		"EMBOSS",
		"SKETCH",
		"SHARPEN",
		"RKSHARPEN"
	};
	u32 val;

	val = rkisp_read(dev, ISP_DPCC0_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DPCC1_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_BLS_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BLS", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SDG",
		(val & CIF_ISP_CTRL_ISP_GAMMA_IN_ENA) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_LSC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LSC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (gain: 0x%08x, 0x%08x)\n", "AWBGAIN",
		(val & CIF_ISP_CTRL_ISP_AWB_ENA) ? "ON" : "OFF", val,
		rkisp_read(dev, CIF_ISP_AWB_GAIN_G_V12, false),
		rkisp_read(dev, CIF_ISP_AWB_GAIN_RB_V12, false));
	val = rkisp_read(dev, ISP_DEBAYER_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DEBAYER", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_CCM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CCM", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GAMMA_OUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAMMA_OUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CPROC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CPROC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_IMG_EFF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (effect: %s)\n", "IE",
		(val & 1) ? "ON" : "OFF", val,
		effect[(val & CIF_IMG_EFF_CTRL_MODE_MASK) >> 1]);
	val = rkisp_read(dev, ISP21_DRC_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRTMO", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HDRMGE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRMGE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_BAYNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BAYNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_BAY3D_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BAY3D", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_YNR_GLOBAL_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "YNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_CNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_SHARP_SHARP_EN, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SHARP", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GIC_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GIC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DHAZ_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DHAZ", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_3DLUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "3DLUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_LDCH_STS, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LDCH", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CSM",
		(val & full_range_flg) ? "FULL" : "LIMITED", val);

	val = rkisp_read(dev, ISP_AFM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_AWB_PROP_V10, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIAWB",
		(val & CIF_ISP_AWB_ENABLE) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_YUVAE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "YUVAE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HIST_HIST_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIHST", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_RAWAWB_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAWB", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAE_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE3", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST3", (val & 1) ? "ON" : "OFF", val);
}

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
	for (val = 0; val < dev->hw_dev->num_clks; val++) {
		seq_printf(p, "%-10s %ld\n",
			   dev->hw_dev->match_data->clks[val],
			   clk_get_rate(dev->hw_dev->clks[val]));
	}
	if (!(dev->isp_state & ISP_START))
		return 0;

	seq_printf(p, "%-10s Cnt:%d ErrCnt:%d\n",
		   "Interrupt",
		   dev->isp_isr_cnt,
		   dev->isp_err_cnt);

	if (sensor && sensor->fi.interval.numerator)
		val = sensor->fi.interval.denominator / sensor->fi.interval.numerator;
	seq_printf(p, "%-10s %s Format:%s Size:%dx%d@%dfps Offset(%d,%d)\n",
		   "Input",
		   sensor ? sensor->sd->name : NULL,
		   sdev->in_fmt.name,
		   sdev->in_crop.width, sdev->in_crop.height, val,
		   sdev->in_crop.left, sdev->in_crop.top);
	if (IS_HDR_RDBK(dev->hdr.op_mode))
		seq_printf(p, "%-10s mode:frame%d (frame:%d rate:%dms %s time:%dms) cnt(total:%d X1:%d X2:%d X3:%d)\n",
			   "Isp Read",
			   dev->rd_mode - 3,
			   dev->dmarx_dev.cur_frame.id,
			   (u32)(dev->dmarx_dev.cur_frame.timestamp - dev->dmarx_dev.pre_frame.timestamp) / 1000 / 1000,
			   (dev->isp_state & ISP_FRAME_END) ? "idle" : "working",
			   sdev->dbg.interval / 1000 / 1000,
			   dev->rdbk_cnt,
			   dev->rdbk_cnt_x1,
			   dev->rdbk_cnt_x2,
			   dev->rdbk_cnt_x3);
	else
		seq_printf(p, "%-10s frame:%d %s time:%dms\n",
			   "Isp online",
			   sdev->dbg.id,
			   (dev->isp_state & ISP_FRAME_END) ? "idle" : "working",
			   sdev->dbg.interval / 1000 / 1000);

	if (dev->br_dev.en)
		seq_printf(p, "%-10s rkispp%d Format:%s%s Size:%dx%d (frame:%d rate:%dms)\n",
			   "Output",
			   dev->dev_id,
			   (dev->br_dev.work_mode & ISP_ISPP_FBC) ? "FBC" : "YUV",
			   (dev->br_dev.work_mode & ISP_ISPP_422) ? "422" : "420",
			   dev->br_dev.crop.width,
			   dev->br_dev.crop.height,
			   dev->br_dev.dbg.id,
			   dev->br_dev.dbg.interval / 1000 / 1000);
	for (val = 0; val < RKISP_MAX_STREAM; val++) {
		struct rkisp_stream *stream = &dev->cap_dev.stream[val];

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

	switch (dev->isp_ver) {
	case ISP_V20:
		isp20_show(dev, p);
		break;
	case ISP_V21:
		isp21_show(dev, p);
		break;
	default:
		break;
	}

	seq_printf(p, "%-10s %s Cnt:%d\n",
		   "Monitor",
		   dev->hw_dev->monitor.is_en ? "ON" : "OFF",
		   dev->hw_dev->monitor.retry);
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
