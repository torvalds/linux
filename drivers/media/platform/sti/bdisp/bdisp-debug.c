// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 */

#include <linux/debugfs.h>
#include <linux/pm_runtime.h>

#include "bdisp.h"
#include "bdisp-filter.h"
#include "bdisp-reg.h"

void bdisp_dbg_perf_begin(struct bdisp_dev *bdisp)
{
	bdisp->dbg.hw_start = ktime_get();
}

void bdisp_dbg_perf_end(struct bdisp_dev *bdisp)
{
	s64 time_us;

	time_us = ktime_us_delta(ktime_get(), bdisp->dbg.hw_start);

	if (!bdisp->dbg.min_duration)
		bdisp->dbg.min_duration = time_us;
	else
		bdisp->dbg.min_duration = min(time_us, bdisp->dbg.min_duration);

	bdisp->dbg.last_duration = time_us;
	bdisp->dbg.max_duration = max(time_us, bdisp->dbg.max_duration);
	bdisp->dbg.tot_duration += time_us;
}

static void bdisp_dbg_dump_ins(struct seq_file *s, u32 val)
{
	seq_printf(s, "INS\t0x%08X\t", val);

	switch (val & BLT_INS_S1_MASK) {
	case BLT_INS_S1_OFF:
		break;
	case BLT_INS_S1_MEM:
		seq_puts(s, "SRC1=mem - ");
		break;
	case BLT_INS_S1_CF:
		seq_puts(s, "SRC1=ColorFill - ");
		break;
	case BLT_INS_S1_COPY:
		seq_puts(s, "SRC1=copy - ");
		break;
	case BLT_INS_S1_FILL:
		seq_puts(s, "SRC1=fil - ");
		break;
	default:
		seq_puts(s, "SRC1=??? - ");
		break;
	}

	switch (val & BLT_INS_S2_MASK) {
	case BLT_INS_S2_OFF:
		break;
	case BLT_INS_S2_MEM:
		seq_puts(s, "SRC2=mem - ");
		break;
	case BLT_INS_S2_CF:
		seq_puts(s, "SRC2=ColorFill - ");
		break;
	default:
		seq_puts(s, "SRC2=??? - ");
		break;
	}

	if ((val & BLT_INS_S3_MASK) == BLT_INS_S3_MEM)
		seq_puts(s, "SRC3=mem - ");

	if (val & BLT_INS_IVMX)
		seq_puts(s, "IVMX - ");
	if (val & BLT_INS_CLUT)
		seq_puts(s, "CLUT - ");
	if (val & BLT_INS_SCALE)
		seq_puts(s, "Scale - ");
	if (val & BLT_INS_FLICK)
		seq_puts(s, "Flicker - ");
	if (val & BLT_INS_CLIP)
		seq_puts(s, "Clip - ");
	if (val & BLT_INS_CKEY)
		seq_puts(s, "ColorKey - ");
	if (val & BLT_INS_OVMX)
		seq_puts(s, "OVMX - ");
	if (val & BLT_INS_DEI)
		seq_puts(s, "Deint - ");
	if (val & BLT_INS_PMASK)
		seq_puts(s, "PlaneMask - ");
	if (val & BLT_INS_VC1R)
		seq_puts(s, "VC1R - ");
	if (val & BLT_INS_ROTATE)
		seq_puts(s, "Rotate - ");
	if (val & BLT_INS_GRAD)
		seq_puts(s, "GradFill - ");
	if (val & BLT_INS_AQLOCK)
		seq_puts(s, "AQLock - ");
	if (val & BLT_INS_PACE)
		seq_puts(s, "Pace - ");
	if (val & BLT_INS_IRQ)
		seq_puts(s, "IRQ - ");

	seq_putc(s, '\n');
}

static void bdisp_dbg_dump_tty(struct seq_file *s, u32 val)
{
	seq_printf(s, "TTY\t0x%08X\t", val);
	seq_printf(s, "Pitch=%d - ", val & 0xFFFF);

	switch ((val & BLT_TTY_COL_MASK) >> BLT_TTY_COL_SHIFT) {
	case BDISP_RGB565:
		seq_puts(s, "RGB565 - ");
		break;
	case BDISP_RGB888:
		seq_puts(s, "RGB888 - ");
		break;
	case BDISP_XRGB8888:
		seq_puts(s, "xRGB888 - ");
		break;
	case BDISP_ARGB8888:
		seq_puts(s, "ARGB8888 - ");
		break;
	case BDISP_NV12:
		seq_puts(s, "NV12 - ");
		break;
	case BDISP_YUV_3B:
		seq_puts(s, "YUV420P - ");
		break;
	default:
		seq_puts(s, "ColorFormat ??? - ");
		break;
	}

	if (val & BLT_TTY_ALPHA_R)
		seq_puts(s, "AlphaRange - ");
	if (val & BLT_TTY_CR_NOT_CB)
		seq_puts(s, "CrNotCb - ");
	if (val & BLT_TTY_MB)
		seq_puts(s, "MB - ");
	if (val & BLT_TTY_HSO)
		seq_puts(s, "HSO inverse - ");
	if (val & BLT_TTY_VSO)
		seq_puts(s, "VSO inverse - ");
	if (val & BLT_TTY_DITHER)
		seq_puts(s, "Dither - ");
	if (val & BLT_TTY_CHROMA)
		seq_puts(s, "Write CHROMA - ");
	if (val & BLT_TTY_BIG_END)
		seq_puts(s, "BigEndian - ");

	seq_putc(s, '\n');
}

static void bdisp_dbg_dump_xy(struct seq_file *s, u32 val, char *name)
{
	seq_printf(s, "%s\t0x%08X\t", name, val);
	seq_printf(s, "(%d,%d)\n", val & 0xFFFF, (val >> 16));
}

static void bdisp_dbg_dump_sz(struct seq_file *s, u32 val, char *name)
{
	seq_printf(s, "%s\t0x%08X\t", name, val);
	seq_printf(s, "%dx%d\n", val & 0x1FFF, (val >> 16) & 0x1FFF);
}

static void bdisp_dbg_dump_sty(struct seq_file *s,
			       u32 val, u32 addr, char *name)
{
	bool s1, s2, s3;

	seq_printf(s, "%s\t0x%08X\t", name, val);

	if (!addr || !name || (strlen(name) < 2))
		goto done;

	s1 = name[strlen(name) - 1] == '1';
	s2 = name[strlen(name) - 1] == '2';
	s3 = name[strlen(name) - 1] == '3';

	seq_printf(s, "Pitch=%d - ", val & 0xFFFF);

	switch ((val & BLT_TTY_COL_MASK) >> BLT_TTY_COL_SHIFT) {
	case BDISP_RGB565:
		seq_puts(s, "RGB565 - ");
		break;
	case BDISP_RGB888:
		seq_puts(s, "RGB888 - ");
		break;
	case BDISP_XRGB8888:
		seq_puts(s, "xRGB888 - ");
		break;
	case BDISP_ARGB8888:
		seq_puts(s, "ARGB888 - ");
		break;
	case BDISP_NV12:
		seq_puts(s, "NV12 - ");
		break;
	case BDISP_YUV_3B:
		seq_puts(s, "YUV420P - ");
		break;
	default:
		seq_puts(s, "ColorFormat ??? - ");
		break;
	}

	if ((val & BLT_TTY_ALPHA_R) && !s3)
		seq_puts(s, "AlphaRange - ");
	if ((val & BLT_S1TY_A1_SUBSET) && !s3)
		seq_puts(s, "A1SubSet - ");
	if ((val & BLT_TTY_MB) && !s1)
		seq_puts(s, "MB - ");
	if (val & BLT_TTY_HSO)
		seq_puts(s, "HSO inverse - ");
	if (val & BLT_TTY_VSO)
		seq_puts(s, "VSO inverse - ");
	if ((val & BLT_S1TY_CHROMA_EXT) && (s1 || s2))
		seq_puts(s, "ChromaExt - ");
	if ((val & BLT_S3TY_BLANK_ACC) && s3)
		seq_puts(s, "Blank Acc - ");
	if ((val & BTL_S1TY_SUBBYTE) && !s3)
		seq_puts(s, "SubByte - ");
	if ((val & BLT_S1TY_RGB_EXP) && !s3)
		seq_puts(s, "RGBExpand - ");
	if ((val & BLT_TTY_BIG_END) && !s3)
		seq_puts(s, "BigEndian - ");

done:
	seq_putc(s, '\n');
}

static void bdisp_dbg_dump_fctl(struct seq_file *s, u32 val)
{
	seq_printf(s, "FCTL\t0x%08X\t", val);

	if ((val & BLT_FCTL_Y_HV_SCALE) == BLT_FCTL_Y_HV_SCALE)
		seq_puts(s, "Resize Luma - ");
	else if ((val & BLT_FCTL_Y_HV_SCALE) == BLT_FCTL_Y_HV_SAMPLE)
		seq_puts(s, "Sample Luma - ");

	if ((val & BLT_FCTL_HV_SCALE) == BLT_FCTL_HV_SCALE)
		seq_puts(s, "Resize Chroma");
	else if ((val & BLT_FCTL_HV_SCALE) == BLT_FCTL_HV_SAMPLE)
		seq_puts(s, "Sample Chroma");

	seq_putc(s, '\n');
}

static void bdisp_dbg_dump_rsf(struct seq_file *s, u32 val, char *name)
{
	u32 inc;

	seq_printf(s, "%s\t0x%08X\t", name, val);

	if (!val)
		goto done;

	inc = val & 0xFFFF;
	seq_printf(s, "H: %d(6.10) / scale~%dx0.1 - ", inc, 1024 * 10 / inc);

	inc = val >> 16;
	seq_printf(s, "V: %d(6.10) / scale~%dx0.1", inc, 1024 * 10 / inc);

done:
	seq_putc(s, '\n');
}

static void bdisp_dbg_dump_rzi(struct seq_file *s, u32 val, char *name)
{
	seq_printf(s, "%s\t0x%08X\t", name, val);

	if (!val)
		goto done;

	seq_printf(s, "H: init=%d repeat=%d - ", val & 0x3FF, (val >> 12) & 7);
	val >>= 16;
	seq_printf(s, "V: init=%d repeat=%d", val & 0x3FF, (val >> 12) & 7);

done:
	seq_putc(s, '\n');
}

static void bdisp_dbg_dump_ivmx(struct seq_file *s,
				u32 c0, u32 c1, u32 c2, u32 c3)
{
	seq_printf(s, "IVMX0\t0x%08X\n", c0);
	seq_printf(s, "IVMX1\t0x%08X\n", c1);
	seq_printf(s, "IVMX2\t0x%08X\n", c2);
	seq_printf(s, "IVMX3\t0x%08X\t", c3);

	if (!c0 && !c1 && !c2 && !c3) {
		seq_putc(s, '\n');
		return;
	}

	if ((c0 == bdisp_rgb_to_yuv[0]) &&
	    (c1 == bdisp_rgb_to_yuv[1]) &&
	    (c2 == bdisp_rgb_to_yuv[2]) &&
	    (c3 == bdisp_rgb_to_yuv[3])) {
		seq_puts(s, "RGB to YUV\n");
		return;
	}

	if ((c0 == bdisp_yuv_to_rgb[0]) &&
	    (c1 == bdisp_yuv_to_rgb[1]) &&
	    (c2 == bdisp_yuv_to_rgb[2]) &&
	    (c3 == bdisp_yuv_to_rgb[3])) {
		seq_puts(s, "YUV to RGB\n");
		return;
	}
	seq_puts(s, "Unknown conversion\n");
}

static int last_nodes_show(struct seq_file *s, void *data)
{
	/* Not dumping all fields, focusing on significant ones */
	struct bdisp_dev *bdisp = s->private;
	struct bdisp_node *node;
	int i = 0;

	if (!bdisp->dbg.copy_node[0]) {
		seq_puts(s, "No node built yet\n");
		return 0;
	}

	do {
		node = bdisp->dbg.copy_node[i];
		if (!node)
			break;
		seq_printf(s, "--------\nNode %d:\n", i);
		seq_puts(s, "-- General --\n");
		seq_printf(s, "NIP\t0x%08X\n", node->nip);
		seq_printf(s, "CIC\t0x%08X\n", node->cic);
		bdisp_dbg_dump_ins(s, node->ins);
		seq_printf(s, "ACK\t0x%08X\n", node->ack);
		seq_puts(s, "-- Target --\n");
		seq_printf(s, "TBA\t0x%08X\n", node->tba);
		bdisp_dbg_dump_tty(s, node->tty);
		bdisp_dbg_dump_xy(s, node->txy, "TXY");
		bdisp_dbg_dump_sz(s, node->tsz, "TSZ");
		/* Color Fill not dumped */
		seq_puts(s, "-- Source 1 --\n");
		seq_printf(s, "S1BA\t0x%08X\n", node->s1ba);
		bdisp_dbg_dump_sty(s, node->s1ty, node->s1ba, "S1TY");
		bdisp_dbg_dump_xy(s, node->s1xy, "S1XY");
		seq_puts(s, "-- Source 2 --\n");
		seq_printf(s, "S2BA\t0x%08X\n", node->s2ba);
		bdisp_dbg_dump_sty(s, node->s2ty, node->s2ba, "S2TY");
		bdisp_dbg_dump_xy(s, node->s2xy, "S2XY");
		bdisp_dbg_dump_sz(s, node->s2sz, "S2SZ");
		seq_puts(s, "-- Source 3 --\n");
		seq_printf(s, "S3BA\t0x%08X\n", node->s3ba);
		bdisp_dbg_dump_sty(s, node->s3ty, node->s3ba, "S3TY");
		bdisp_dbg_dump_xy(s, node->s3xy, "S3XY");
		bdisp_dbg_dump_sz(s, node->s3sz, "S3SZ");
		/* Clipping not dumped */
		/* CLUT not dumped */
		seq_puts(s, "-- Filter & Mask --\n");
		bdisp_dbg_dump_fctl(s, node->fctl);
		/* PMK not dumped */
		seq_puts(s, "-- Chroma Filter --\n");
		bdisp_dbg_dump_rsf(s, node->rsf, "RSF");
		bdisp_dbg_dump_rzi(s, node->rzi, "RZI");
		seq_printf(s, "HFP\t0x%08X\n", node->hfp);
		seq_printf(s, "VFP\t0x%08X\n", node->vfp);
		seq_puts(s, "-- Luma Filter --\n");
		bdisp_dbg_dump_rsf(s, node->y_rsf, "Y_RSF");
		bdisp_dbg_dump_rzi(s, node->y_rzi, "Y_RZI");
		seq_printf(s, "Y_HFP\t0x%08X\n", node->y_hfp);
		seq_printf(s, "Y_VFP\t0x%08X\n", node->y_vfp);
		/* Flicker not dumped */
		/* Color key not dumped */
		/* Reserved not dumped */
		/* Static Address & User not dumped */
		seq_puts(s, "-- Input Versatile Matrix --\n");
		bdisp_dbg_dump_ivmx(s, node->ivmx0, node->ivmx1,
				    node->ivmx2, node->ivmx3);
		/* Output Versatile Matrix not dumped */
		/* Pace not dumped */
		/* VC1R & DEI not dumped */
		/* Gradient Fill not dumped */
	} while ((++i < MAX_NB_NODE) && node->nip);

	return 0;
}

static int last_nodes_raw_show(struct seq_file *s, void *data)
{
	struct bdisp_dev *bdisp = s->private;
	struct bdisp_node *node;
	u32 *val;
	int j, i = 0;

	if (!bdisp->dbg.copy_node[0]) {
		seq_puts(s, "No node built yet\n");
		return 0;
	}

	do {
		node = bdisp->dbg.copy_node[i];
		if (!node)
			break;

		seq_printf(s, "--------\nNode %d:\n", i);
		val = (u32 *)node;
		for (j = 0; j < sizeof(struct bdisp_node) / sizeof(u32); j++)
			seq_printf(s, "0x%08X\n", *val++);
	} while ((++i < MAX_NB_NODE) && node->nip);

	return 0;
}

static const char *bdisp_fmt_to_str(struct bdisp_frame frame)
{
	switch (frame.fmt->pixelformat) {
	case V4L2_PIX_FMT_YUV420:
		return "YUV420P";
	case V4L2_PIX_FMT_NV12:
		if (frame.field == V4L2_FIELD_INTERLACED)
			return "NV12 interlaced";
		else
			return "NV12";
	case V4L2_PIX_FMT_RGB565:
		return "RGB16";
	case V4L2_PIX_FMT_RGB24:
		return "RGB24";
	case V4L2_PIX_FMT_XBGR32:
		return "XRGB";
	case V4L2_PIX_FMT_ABGR32:
		return "ARGB";
	default:
		return "????";
	}
}

static int last_request_show(struct seq_file *s, void *data)
{
	struct bdisp_dev *bdisp = s->private;
	struct bdisp_request *request = &bdisp->dbg.copy_request;
	struct bdisp_frame src, dst;

	if (!request->nb_req) {
		seq_puts(s, "No request\n");
		return 0;
	}

	src = request->src;
	dst = request->dst;

	seq_printf(s, "\nRequest #%d\n", request->nb_req);

	seq_printf(s, "Format:    %s\t\t\t%s\n",
		   bdisp_fmt_to_str(src), bdisp_fmt_to_str(dst));
	seq_printf(s, "Crop area: %dx%d @ %d,%d  ==>\t%dx%d @ %d,%d\n",
		   src.crop.width, src.crop.height,
		   src.crop.left, src.crop.top,
		   dst.crop.width, dst.crop.height,
		   dst.crop.left, dst.crop.top);
	seq_printf(s, "Buff size: %dx%d\t\t%dx%d\n\n",
		   src.width, src.height, dst.width, dst.height);

	if (request->hflip)
		seq_puts(s, "Horizontal flip\n\n");

	if (request->vflip)
		seq_puts(s, "Vertical flip\n\n");

	return 0;
}

#define DUMP(reg) seq_printf(s, #reg " \t0x%08X\n", readl(bdisp->regs + reg))

static int regs_show(struct seq_file *s, void *data)
{
	struct bdisp_dev *bdisp = s->private;
	int ret;
	unsigned int i;

	ret = pm_runtime_resume_and_get(bdisp->dev);
	if (ret < 0) {
		seq_puts(s, "Cannot wake up IP\n");
		return 0;
	}

	seq_printf(s, "Reg @ = 0x%p\n", bdisp->regs);

	seq_puts(s, "\nStatic:\n");
	DUMP(BLT_CTL);
	DUMP(BLT_ITS);
	DUMP(BLT_STA1);
	DUMP(BLT_AQ1_CTL);
	DUMP(BLT_AQ1_IP);
	DUMP(BLT_AQ1_LNA);
	DUMP(BLT_AQ1_STA);
	DUMP(BLT_ITM0);

	seq_puts(s, "\nPlugs:\n");
	DUMP(BLT_PLUGS1_OP2);
	DUMP(BLT_PLUGS1_CHZ);
	DUMP(BLT_PLUGS1_MSZ);
	DUMP(BLT_PLUGS1_PGZ);
	DUMP(BLT_PLUGS2_OP2);
	DUMP(BLT_PLUGS2_CHZ);
	DUMP(BLT_PLUGS2_MSZ);
	DUMP(BLT_PLUGS2_PGZ);
	DUMP(BLT_PLUGS3_OP2);
	DUMP(BLT_PLUGS3_CHZ);
	DUMP(BLT_PLUGS3_MSZ);
	DUMP(BLT_PLUGS3_PGZ);
	DUMP(BLT_PLUGT_OP2);
	DUMP(BLT_PLUGT_CHZ);
	DUMP(BLT_PLUGT_MSZ);
	DUMP(BLT_PLUGT_PGZ);

	seq_puts(s, "\nNode:\n");
	DUMP(BLT_NIP);
	DUMP(BLT_CIC);
	DUMP(BLT_INS);
	DUMP(BLT_ACK);
	DUMP(BLT_TBA);
	DUMP(BLT_TTY);
	DUMP(BLT_TXY);
	DUMP(BLT_TSZ);
	DUMP(BLT_S1BA);
	DUMP(BLT_S1TY);
	DUMP(BLT_S1XY);
	DUMP(BLT_S2BA);
	DUMP(BLT_S2TY);
	DUMP(BLT_S2XY);
	DUMP(BLT_S2SZ);
	DUMP(BLT_S3BA);
	DUMP(BLT_S3TY);
	DUMP(BLT_S3XY);
	DUMP(BLT_S3SZ);
	DUMP(BLT_FCTL);
	DUMP(BLT_RSF);
	DUMP(BLT_RZI);
	DUMP(BLT_HFP);
	DUMP(BLT_VFP);
	DUMP(BLT_Y_RSF);
	DUMP(BLT_Y_RZI);
	DUMP(BLT_Y_HFP);
	DUMP(BLT_Y_VFP);
	DUMP(BLT_IVMX0);
	DUMP(BLT_IVMX1);
	DUMP(BLT_IVMX2);
	DUMP(BLT_IVMX3);
	DUMP(BLT_OVMX0);
	DUMP(BLT_OVMX1);
	DUMP(BLT_OVMX2);
	DUMP(BLT_OVMX3);
	DUMP(BLT_DEI);

	seq_puts(s, "\nFilter:\n");
	for (i = 0; i < BLT_NB_H_COEF; i++) {
		seq_printf(s, "BLT_HFC%d \t0x%08X\n", i,
			   readl(bdisp->regs + BLT_HFC_N + i * 4));
	}
	for (i = 0; i < BLT_NB_V_COEF; i++) {
		seq_printf(s, "BLT_VFC%d \t0x%08X\n", i,
			   readl(bdisp->regs + BLT_VFC_N + i * 4));
	}

	seq_puts(s, "\nLuma filter:\n");
	for (i = 0; i < BLT_NB_H_COEF; i++) {
		seq_printf(s, "BLT_Y_HFC%d \t0x%08X\n", i,
			   readl(bdisp->regs + BLT_Y_HFC_N + i * 4));
	}
	for (i = 0; i < BLT_NB_V_COEF; i++) {
		seq_printf(s, "BLT_Y_VFC%d \t0x%08X\n", i,
			   readl(bdisp->regs + BLT_Y_VFC_N + i * 4));
	}

	pm_runtime_put(bdisp->dev);

	return 0;
}

#define SECOND 1000000

static int perf_show(struct seq_file *s, void *data)
{
	struct bdisp_dev *bdisp = s->private;
	struct bdisp_request *request = &bdisp->dbg.copy_request;
	s64 avg_time_us;
	int avg_fps, min_fps, max_fps, last_fps;

	if (!request->nb_req) {
		seq_puts(s, "No request\n");
		return 0;
	}

	avg_time_us = div64_s64(bdisp->dbg.tot_duration, request->nb_req);
	if (avg_time_us > SECOND)
		avg_fps = 0;
	else
		avg_fps = SECOND / (s32)avg_time_us;

	if (bdisp->dbg.min_duration > SECOND)
		min_fps = 0;
	else
		min_fps = SECOND / (s32)bdisp->dbg.min_duration;

	if (bdisp->dbg.max_duration > SECOND)
		max_fps = 0;
	else
		max_fps = SECOND / (s32)bdisp->dbg.max_duration;

	if (bdisp->dbg.last_duration > SECOND)
		last_fps = 0;
	else
		last_fps = SECOND / (s32)bdisp->dbg.last_duration;

	seq_printf(s, "HW processing (%d requests):\n", request->nb_req);
	seq_printf(s, " Average: %5lld us  (%3d fps)\n",
		   avg_time_us, avg_fps);
	seq_printf(s, " Min-Max: %5lld us  (%3d fps) - %5lld us  (%3d fps)\n",
		   bdisp->dbg.min_duration, min_fps,
		   bdisp->dbg.max_duration, max_fps);
	seq_printf(s, " Last:    %5lld us  (%3d fps)\n",
		   bdisp->dbg.last_duration, last_fps);

	return 0;
}

#define bdisp_dbg_create_entry(name) \
	debugfs_create_file(#name, S_IRUGO, bdisp->dbg.debugfs_entry, bdisp, \
			    &name##_fops)

DEFINE_SHOW_ATTRIBUTE(regs);
DEFINE_SHOW_ATTRIBUTE(last_nodes);
DEFINE_SHOW_ATTRIBUTE(last_nodes_raw);
DEFINE_SHOW_ATTRIBUTE(last_request);
DEFINE_SHOW_ATTRIBUTE(perf);

void bdisp_debugfs_create(struct bdisp_dev *bdisp)
{
	char dirname[16];

	snprintf(dirname, sizeof(dirname), "%s%d", BDISP_NAME, bdisp->id);
	bdisp->dbg.debugfs_entry = debugfs_create_dir(dirname, NULL);

	bdisp_dbg_create_entry(regs);
	bdisp_dbg_create_entry(last_nodes);
	bdisp_dbg_create_entry(last_nodes_raw);
	bdisp_dbg_create_entry(last_request);
	bdisp_dbg_create_entry(perf);
}

void bdisp_debugfs_remove(struct bdisp_dev *bdisp)
{
	debugfs_remove_recursive(bdisp->dbg.debugfs_entry);
	bdisp->dbg.debugfs_entry = NULL;
}
