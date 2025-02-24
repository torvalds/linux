// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Video Capture/Differentiation Engine (VCD) and Encoding
 * Compression Engine (ECE) present on Nuvoton NPCM SoCs.
 *
 * Copyright (C) 2022 Nuvoton Technologies
 */

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <uapi/linux/npcm-video.h>
#include "npcm-regs.h"

#define DEVICE_NAME	"npcm-video"
#define MAX_WIDTH	1920
#define MAX_HEIGHT	1200
#define MIN_WIDTH	320
#define MIN_HEIGHT	240
#define MIN_LP		512
#define MAX_LP		4096
#define RECT_W		16
#define RECT_H		16
#define BITMAP_SIZE	32

struct npcm_video_addr {
	size_t size;
	dma_addr_t dma;
	void *virt;
};

struct npcm_video_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head link;
};

#define to_npcm_video_buffer(x) \
	container_of((x), struct npcm_video_buffer, vb)

/*
 * VIDEO_STREAMING:	a flag indicating if the video has started streaming
 * VIDEO_CAPTURING:	a flag indicating if the VCD is capturing a frame
 * VIDEO_RES_CHANGING:	a flag indicating if the resolution is changing
 * VIDEO_STOPPED:	a flag indicating if the video has stopped streaming
 */
enum {
	VIDEO_STREAMING,
	VIDEO_CAPTURING,
	VIDEO_RES_CHANGING,
	VIDEO_STOPPED,
};

struct rect_list {
	struct v4l2_clip clip;
	struct list_head list;
};

struct rect_list_info {
	struct rect_list *list;
	struct rect_list *first;
	struct list_head *head;
	unsigned int index;
	unsigned int tile_perline;
	unsigned int tile_perrow;
	unsigned int offset_perline;
	unsigned int tile_size;
	unsigned int tile_cnt;
};

struct npcm_ece {
	struct regmap *regmap;
	atomic_t clients;
	struct reset_control *reset;
	bool enable;
};

struct npcm_video {
	struct regmap *gcr_regmap;
	struct regmap *gfx_regmap;
	struct regmap *vcd_regmap;

	struct device *dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *rect_cnt_ctrl;
	struct v4l2_device v4l2_dev;
	struct v4l2_pix_format pix_fmt;
	struct v4l2_bt_timings active_timings;
	struct v4l2_bt_timings detected_timings;
	unsigned int v4l2_input_status;
	struct vb2_queue queue;
	struct video_device vdev;
	struct mutex video_lock; /* v4l2 and videobuf2 lock */

	struct list_head buffers;
	struct mutex buffer_lock; /* buffer list lock */
	unsigned long flags;
	unsigned int sequence;

	struct npcm_video_addr src;
	struct reset_control *reset;
	struct npcm_ece ece;

	unsigned int bytesperline;
	unsigned int bytesperpixel;
	unsigned int rect_cnt;
	struct list_head list[VIDEO_MAX_FRAME];
	unsigned int rect[VIDEO_MAX_FRAME];
	unsigned int ctrl_cmd;
	unsigned int op_cmd;
};

#define to_npcm_video(x) container_of((x), struct npcm_video, v4l2_dev)

struct npcm_fmt {
	unsigned int fourcc;
	unsigned int bpp; /* bytes per pixel */
};

static const struct npcm_fmt npcm_fmt_list[] = {
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.bpp	= 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_HEXTILE,
		.bpp	= 2,
	},
};

#define NUM_FORMATS ARRAY_SIZE(npcm_fmt_list)

static const struct v4l2_dv_timings_cap npcm_video_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.bt = {
		.min_width = MIN_WIDTH,
		.max_width = MAX_WIDTH,
		.min_height = MIN_HEIGHT,
		.max_height = MAX_HEIGHT,
		.min_pixelclock = 6574080, /* 640 x 480 x 24Hz */
		.max_pixelclock = 138240000, /* 1920 x 1200 x 60Hz */
		.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			     V4L2_DV_BT_STD_CVT | V4L2_DV_BT_STD_GTF,
		.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
				V4L2_DV_BT_CAP_REDUCED_BLANKING |
				V4L2_DV_BT_CAP_CUSTOM,
	},
};

static DECLARE_BITMAP(bitmap, BITMAP_SIZE);

static const struct npcm_fmt *npcm_video_find_format(struct v4l2_format *f)
{
	const struct npcm_fmt *fmt;
	unsigned int k;

	for (k = 0; k < NUM_FORMATS; k++) {
		fmt = &npcm_fmt_list[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (k == NUM_FORMATS)
		return NULL;

	return &npcm_fmt_list[k];
}

static void npcm_video_ece_prepend_rect_header(void *addr, u16 x, u16 y, u16 w, u16 h)
{
	__be16 x_pos = cpu_to_be16(x);
	__be16 y_pos = cpu_to_be16(y);
	__be16 width = cpu_to_be16(w);
	__be16 height = cpu_to_be16(h);
	__be32 encoding = cpu_to_be32(5); /* Hextile encoding */

	memcpy(addr, &x_pos, 2);
	memcpy(addr + 2, &y_pos, 2);
	memcpy(addr + 4, &width, 2);
	memcpy(addr + 6, &height, 2);
	memcpy(addr + 8, &encoding, 4);
}

static unsigned int npcm_video_ece_get_ed_size(struct npcm_video *video,
					       unsigned int offset, void *addr)
{
	struct regmap *ece = video->ece.regmap;
	unsigned int size, gap, val;
	int ret;

	ret = regmap_read_poll_timeout(ece, ECE_DDA_STS, val,
				       (val & ECE_DDA_STS_CDREADY), 0,
				       ECE_POLL_TIMEOUT_US);

	if (ret) {
		dev_warn(video->dev, "Wait for ECE_DDA_STS_CDREADY timeout\n");
		return 0;
	}

	size = readl((void __iomem *)addr + offset);
	regmap_read(ece, ECE_HEX_CTRL, &val);
	gap = FIELD_GET(ECE_HEX_CTRL_ENC_GAP, val);

	dev_dbg(video->dev, "offset = %u, ed_size = %u, gap = %u\n", offset,
		size, gap);

	return size + gap;
}

static void npcm_video_ece_enc_rect(struct npcm_video *video,
				    unsigned int r_off_x, unsigned int r_off_y,
				    unsigned int r_w, unsigned int r_h)
{
	struct regmap *ece = video->ece.regmap;
	unsigned int rect_offset = (r_off_y * video->bytesperline) + (r_off_x * 2);
	unsigned int w_size = ECE_TILE_W, h_size = ECE_TILE_H;
	unsigned int temp, w_tile, h_tile;

	regmap_update_bits(ece, ECE_DDA_CTRL, ECE_DDA_CTRL_ECEEN, 0);
	regmap_update_bits(ece, ECE_DDA_CTRL, ECE_DDA_CTRL_ECEEN, ECE_DDA_CTRL_ECEEN);
	regmap_write(ece, ECE_DDA_STS, ECE_DDA_STS_CDREADY | ECE_DDA_STS_ACDRDY);
	regmap_write(ece, ECE_RECT_XY, rect_offset);

	w_tile = r_w / ECE_TILE_W;
	h_tile = r_h / ECE_TILE_H;

	if (r_w % ECE_TILE_W) {
		w_tile += 1;
		w_size = r_w % ECE_TILE_W;
	}
	if (r_h % ECE_TILE_H || !h_tile) {
		h_tile += 1;
		h_size = r_h % ECE_TILE_H;
	}

	temp = FIELD_PREP(ECE_RECT_DIMEN_WLTR, w_size - 1) |
	       FIELD_PREP(ECE_RECT_DIMEN_HLTR, h_size - 1) |
	       FIELD_PREP(ECE_RECT_DIMEN_WR, w_tile - 1) |
	       FIELD_PREP(ECE_RECT_DIMEN_HR, h_tile - 1);

	regmap_write(ece, ECE_RECT_DIMEN, temp);
}

static unsigned int npcm_video_ece_read_rect_offset(struct npcm_video *video)
{
	struct regmap *ece = video->ece.regmap;
	unsigned int offset;

	regmap_read(ece, ECE_HEX_RECT_OFFSET, &offset);
	return FIELD_GET(ECE_HEX_RECT_OFFSET_MASK, offset);
}

/*
 * Set the line pitch (in bytes) for the frame buffers.
 * Can be on of those values: 512, 1024, 2048, 2560 or 4096 bytes.
 */
static void npcm_video_ece_set_lp(struct npcm_video *video, unsigned int pitch)
{
	struct regmap *ece = video->ece.regmap;
	unsigned int lp;

	switch (pitch) {
	case 512:
		lp = ECE_RESOL_FB_LP_512;
		break;
	case 1024:
		lp = ECE_RESOL_FB_LP_1024;
		break;
	case 2048:
		lp = ECE_RESOL_FB_LP_2048;
		break;
	case 2560:
		lp = ECE_RESOL_FB_LP_2560;
		break;
	case 4096:
		lp = ECE_RESOL_FB_LP_4096;
		break;
	default:
		return;
	}

	regmap_write(ece, ECE_RESOL, lp);
}

static inline void npcm_video_ece_set_fb_addr(struct npcm_video *video,
					      unsigned int buffer)
{
	struct regmap *ece = video->ece.regmap;

	regmap_write(ece, ECE_FBR_BA, buffer);
}

static inline void npcm_video_ece_set_enc_dba(struct npcm_video *video,
					      unsigned int addr)
{
	struct regmap *ece = video->ece.regmap;

	regmap_write(ece, ECE_ED_BA, addr);
}

static inline void npcm_video_ece_clear_rect_offset(struct npcm_video *video)
{
	struct regmap *ece = video->ece.regmap;

	regmap_write(ece, ECE_HEX_RECT_OFFSET, 0);
}

static void npcm_video_ece_ctrl_reset(struct npcm_video *video)
{
	struct regmap *ece = video->ece.regmap;

	regmap_update_bits(ece, ECE_DDA_CTRL, ECE_DDA_CTRL_ECEEN, 0);
	regmap_update_bits(ece, ECE_HEX_CTRL, ECE_HEX_CTRL_ENCDIS, ECE_HEX_CTRL_ENCDIS);
	regmap_update_bits(ece, ECE_DDA_CTRL, ECE_DDA_CTRL_ECEEN, ECE_DDA_CTRL_ECEEN);
	regmap_update_bits(ece, ECE_HEX_CTRL, ECE_HEX_CTRL_ENCDIS, 0);

	npcm_video_ece_clear_rect_offset(video);
}

static void npcm_video_ece_ip_reset(struct npcm_video *video)
{
	/*
	 * After resetting a module and clearing the reset bit, it should wait
	 * at least 10 us before accessing the module.
	 */
	reset_control_assert(video->ece.reset);
	usleep_range(10, 20);
	reset_control_deassert(video->ece.reset);
	usleep_range(10, 20);
}

static void npcm_video_ece_stop(struct npcm_video *video)
{
	struct regmap *ece = video->ece.regmap;

	regmap_update_bits(ece, ECE_DDA_CTRL, ECE_DDA_CTRL_ECEEN, 0);
	regmap_update_bits(ece, ECE_DDA_CTRL, ECE_DDA_CTRL_INTEN, 0);
	regmap_update_bits(ece, ECE_HEX_CTRL, ECE_HEX_CTRL_ENCDIS, ECE_HEX_CTRL_ENCDIS);
	npcm_video_ece_clear_rect_offset(video);
}

static bool npcm_video_alloc_fb(struct npcm_video *video,
				struct npcm_video_addr *addr)
{
	addr->virt = dma_alloc_coherent(video->dev, VCD_FB_SIZE, &addr->dma,
					GFP_KERNEL);
	if (!addr->virt)
		return false;

	addr->size = VCD_FB_SIZE;
	return true;
}

static void npcm_video_free_fb(struct npcm_video *video,
			       struct npcm_video_addr *addr)
{
	dma_free_coherent(video->dev, addr->size, addr->virt, addr->dma);
	addr->size = 0;
	addr->dma = 0ULL;
	addr->virt = NULL;
}

static void npcm_video_free_diff_table(struct npcm_video *video)
{
	struct list_head *head, *pos, *nx;
	struct rect_list *tmp;
	unsigned int i;

	for (i = 0; i < vb2_get_num_buffers(&video->queue); i++) {
		head = &video->list[i];
		list_for_each_safe(pos, nx, head) {
			tmp = list_entry(pos, struct rect_list, list);
			list_del(&tmp->list);
			kfree(tmp);
		}
	}
}

static unsigned int npcm_video_add_rect(struct npcm_video *video,
					unsigned int index,
					unsigned int x, unsigned int y,
					unsigned int w, unsigned int h)
{
	struct list_head *head = &video->list[index];
	struct rect_list *list = NULL;
	struct v4l2_rect *r;

	list = kzalloc(sizeof(*list), GFP_KERNEL);
	if (!list)
		return 0;

	r = &list->clip.c;
	r->left = x;
	r->top = y;
	r->width = w;
	r->height = h;

	list_add_tail(&list->list, head);
	return 1;
}

static void npcm_video_merge_rect(struct npcm_video *video,
				  struct rect_list_info *info)
{
	struct list_head *head = info->head;
	struct rect_list *list = info->list, *first = info->first;
	struct v4l2_rect *r = &list->clip.c, *f = &first->clip.c;

	if (!first) {
		first = list;
		info->first = first;
		list_add_tail(&list->list, head);
		video->rect_cnt++;
	} else {
		if ((r->left == (f->left + f->width)) && r->top == f->top) {
			f->width += r->width;
			kfree(list);
		} else if ((r->top == (f->top + f->height)) &&
			   (r->left == f->left)) {
			f->height += r->height;
			kfree(list);
		} else if (((r->top > f->top) &&
			   (r->top < (f->top + f->height))) &&
			   ((r->left > f->left) &&
			   (r->left < (f->left + f->width)))) {
			kfree(list);
		} else {
			list_add_tail(&list->list, head);
			video->rect_cnt++;
			info->first = list;
		}
	}
}

static struct rect_list *npcm_video_new_rect(struct npcm_video *video,
					     unsigned int offset,
					     unsigned int index)
{
	struct v4l2_bt_timings *act = &video->active_timings;
	struct rect_list *list = NULL;
	struct v4l2_rect *r;

	list = kzalloc(sizeof(*list), GFP_KERNEL);
	if (!list)
		return NULL;

	r = &list->clip.c;

	r->left = (offset << 4);
	r->top = (index >> 2);
	r->width = RECT_W;
	r->height = RECT_H;
	if ((r->left + RECT_W) > act->width)
		r->width = act->width - r->left;
	if ((r->top + RECT_H) > act->height)
		r->height = act->height - r->top;

	return list;
}

static int npcm_video_find_rect(struct npcm_video *video,
				struct rect_list_info *info,
				unsigned int offset)
{
	if (offset < info->tile_perline) {
		info->list = npcm_video_new_rect(video, offset, info->index);
		if (!info->list) {
			dev_err(video->dev, "Failed to allocate rect_list\n");
			return -ENOMEM;
		}

		npcm_video_merge_rect(video, info);
	}
	return 0;
}

static int npcm_video_build_table(struct npcm_video *video,
				  struct rect_list_info *info)
{
	struct regmap *vcd = video->vcd_regmap;
	unsigned int j, bit, value;
	int ret;

	for (j = 0; j < info->offset_perline; j += 4) {
		regmap_read(vcd, VCD_DIFF_TBL + (j + info->index), &value);

		bitmap_from_arr32(bitmap, &value, BITMAP_SIZE);

		for_each_set_bit(bit, bitmap, BITMAP_SIZE) {
			ret = npcm_video_find_rect(video, info, bit + (j << 3));
			if (ret)
				return ret;
		}
	}
	info->index += 64;
	return info->tile_perline;
}

static void npcm_video_get_rect_list(struct npcm_video *video, unsigned int index)
{
	struct v4l2_bt_timings *act = &video->active_timings;
	struct rect_list_info info;
	unsigned int tile_cnt = 0, mod;
	int ret = 0;

	memset(&info, 0, sizeof(struct rect_list_info));
	info.head = &video->list[index];

	info.tile_perline = act->width >> 4;
	mod = act->width % RECT_W;
	if (mod != 0)
		info.tile_perline += 1;

	info.tile_perrow = act->height >> 4;
	mod = act->height % RECT_H;
	if (mod != 0)
		info.tile_perrow += 1;

	info.tile_size = info.tile_perrow * info.tile_perline;

	info.offset_perline = info.tile_perline >> 5;
	mod = info.tile_perline % 32;
	if (mod != 0)
		info.offset_perline += 1;

	info.offset_perline *= 4;

	do {
		ret = npcm_video_build_table(video, &info);
		if (ret < 0)
			return;

		tile_cnt += ret;
	} while (tile_cnt < info.tile_size);
}

static unsigned int npcm_video_is_mga(struct npcm_video *video)
{
	struct regmap *gfxi = video->gfx_regmap;
	unsigned int dispst;

	regmap_read(gfxi, DISPST, &dispst);
	return ((dispst & DISPST_MGAMODE) == DISPST_MGAMODE);
}

static unsigned int npcm_video_hres(struct npcm_video *video)
{
	struct regmap *gfxi = video->gfx_regmap;
	unsigned int hvcnth, hvcntl, apb_hor_res;

	regmap_read(gfxi, HVCNTH, &hvcnth);
	regmap_read(gfxi, HVCNTL, &hvcntl);
	apb_hor_res = (((hvcnth & HVCNTH_MASK) << 8) + (hvcntl & HVCNTL_MASK) + 1);

	return apb_hor_res;
}

static unsigned int npcm_video_vres(struct npcm_video *video)
{
	struct regmap *gfxi = video->gfx_regmap;
	unsigned int vvcnth, vvcntl, apb_ver_res;

	regmap_read(gfxi, VVCNTH, &vvcnth);
	regmap_read(gfxi, VVCNTL, &vvcntl);

	apb_ver_res = (((vvcnth & VVCNTH_MASK) << 8) + (vvcntl & VVCNTL_MASK));

	return apb_ver_res;
}

static int npcm_video_capres(struct npcm_video *video, unsigned int hor_res,
			     unsigned int vert_res)
{
	struct regmap *vcd = video->vcd_regmap;
	unsigned int res, cap_res;

	if (hor_res > MAX_WIDTH || vert_res > MAX_HEIGHT)
		return -EINVAL;

	res = FIELD_PREP(VCD_CAP_RES_VERT_RES, vert_res) |
	      FIELD_PREP(VCD_CAP_RES_HOR_RES, hor_res);

	regmap_write(vcd, VCD_CAP_RES, res);
	regmap_read(vcd, VCD_CAP_RES, &cap_res);

	if (cap_res != res)
		return -EINVAL;

	return 0;
}

static void npcm_video_vcd_ip_reset(struct npcm_video *video)
{
	/*
	 * After resetting a module and clearing the reset bit, it should wait
	 * at least 10 us before accessing the module.
	 */
	reset_control_assert(video->reset);
	usleep_range(10, 20);
	reset_control_deassert(video->reset);
	usleep_range(10, 20);
}

static void npcm_video_vcd_state_machine_reset(struct npcm_video *video)
{
	struct regmap *vcd = video->vcd_regmap;

	regmap_update_bits(vcd, VCD_MODE, VCD_MODE_VCDE, 0);
	regmap_update_bits(vcd, VCD_MODE, VCD_MODE_IDBC, 0);
	regmap_update_bits(vcd, VCD_CMD, VCD_CMD_RST, VCD_CMD_RST);

	/*
	 * VCD_CMD_RST will reset VCD internal state machines and clear FIFOs,
	 * it should wait at least 800 us for the reset operations completed.
	 */
	usleep_range(800, 1000);

	regmap_write(vcd, VCD_STAT, VCD_STAT_CLEAR);
	regmap_update_bits(vcd, VCD_MODE, VCD_MODE_VCDE, VCD_MODE_VCDE);
	regmap_update_bits(vcd, VCD_MODE, VCD_MODE_IDBC, VCD_MODE_IDBC);
}

static void npcm_video_gfx_reset(struct npcm_video *video)
{
	struct regmap *gcr = video->gcr_regmap;

	regmap_update_bits(gcr, INTCR2, INTCR2_GIRST2, INTCR2_GIRST2);
	npcm_video_vcd_state_machine_reset(video);
	regmap_update_bits(gcr, INTCR2, INTCR2_GIRST2, 0);
}

static void npcm_video_kvm_bw(struct npcm_video *video, bool set_bw)
{
	struct regmap *vcd = video->vcd_regmap;

	if (set_bw || !npcm_video_is_mga(video))
		regmap_update_bits(vcd, VCD_MODE, VCD_MODE_KVM_BW_SET,
				   VCD_MODE_KVM_BW_SET);
	else
		regmap_update_bits(vcd, VCD_MODE, VCD_MODE_KVM_BW_SET, 0);
}

static unsigned int npcm_video_pclk(struct npcm_video *video)
{
	struct regmap *gfxi = video->gfx_regmap;
	unsigned int tmp, pllfbdiv, pllinotdiv, gpllfbdiv;
	unsigned int gpllfbdv109, gpllfbdv8, gpllindiv;
	unsigned int gpllst_pllotdiv1, gpllst_pllotdiv2;

	regmap_read(gfxi, GPLLST, &tmp);
	gpllfbdv109 = FIELD_GET(GPLLST_GPLLFBDV109, tmp);
	gpllst_pllotdiv1 = FIELD_GET(GPLLST_PLLOTDIV1, tmp);
	gpllst_pllotdiv2 = FIELD_GET(GPLLST_PLLOTDIV2, tmp);

	regmap_read(gfxi, GPLLINDIV, &tmp);
	gpllfbdv8 = FIELD_GET(GPLLINDIV_GPLLFBDV8, tmp);
	gpllindiv = FIELD_GET(GPLLINDIV_MASK, tmp);

	regmap_read(gfxi, GPLLFBDIV, &tmp);
	gpllfbdiv = FIELD_GET(GPLLFBDIV_MASK, tmp);

	pllfbdiv = (512 * gpllfbdv109 + 256 * gpllfbdv8 + gpllfbdiv);
	pllinotdiv = (gpllindiv * gpllst_pllotdiv1 * gpllst_pllotdiv2);
	if (pllfbdiv == 0 || pllinotdiv == 0)
		return 0;

	return ((pllfbdiv * 25000) / pllinotdiv) * 1000;
}

static unsigned int npcm_video_get_bpp(struct npcm_video *video)
{
	const struct npcm_fmt *fmt;
	unsigned int k;

	for (k = 0; k < NUM_FORMATS; k++) {
		fmt = &npcm_fmt_list[k];
		if (fmt->fourcc == video->pix_fmt.pixelformat)
			break;
	}

	return fmt->bpp;
}

/*
 * Pitch must be a power of 2, >= linebytes,
 * at least 512, and no more than 4096.
 */
static void npcm_video_set_linepitch(struct npcm_video *video,
				     unsigned int linebytes)
{
	struct regmap *vcd = video->vcd_regmap;
	unsigned int pitch = MIN_LP;

	while ((pitch < linebytes) && (pitch < MAX_LP))
		pitch *= 2;

	regmap_write(vcd, VCD_FB_LP, FIELD_PREP(VCD_FBA_LP, pitch) |
		     FIELD_PREP(VCD_FBB_LP, pitch));
}

static unsigned int npcm_video_get_linepitch(struct npcm_video *video)
{
	struct regmap *vcd = video->vcd_regmap;
	unsigned int linepitch;

	regmap_read(vcd, VCD_FB_LP, &linepitch);
	return FIELD_GET(VCD_FBA_LP, linepitch);
}

static void npcm_video_command(struct npcm_video *video, unsigned int value)
{
	struct regmap *vcd = video->vcd_regmap;
	unsigned int cmd;

	regmap_write(vcd, VCD_STAT, VCD_STAT_CLEAR);
	regmap_read(vcd, VCD_CMD, &cmd);
	cmd |= FIELD_PREP(VCD_CMD_OPERATION, value);

	regmap_write(vcd, VCD_CMD, cmd);
	regmap_update_bits(vcd, VCD_CMD, VCD_CMD_GO, VCD_CMD_GO);
	video->op_cmd = value;
}

static void npcm_video_init_reg(struct npcm_video *video)
{
	struct regmap *gcr = video->gcr_regmap, *vcd = video->vcd_regmap;

	/* Selects Data Enable */
	regmap_update_bits(gcr, INTCR, INTCR_DEHS, 0);

	/* Enable display of KVM GFX and access to memory */
	regmap_update_bits(gcr, INTCR, INTCR_GFXIFDIS, 0);

	/* Active Vertical/Horizontal Counters Reset */
	regmap_update_bits(gcr, INTCR2, INTCR2_GIHCRST | INTCR2_GIVCRST,
			   INTCR2_GIHCRST | INTCR2_GIVCRST);

	/* Reset video modules */
	npcm_video_vcd_ip_reset(video);
	npcm_video_gfx_reset(video);

	/* Set the FIFO thresholds */
	regmap_write(vcd, VCD_FIFO, VCD_FIFO_TH);

	/* Set RCHG timer */
	regmap_write(vcd, VCD_RCHG, FIELD_PREP(VCD_RCHG_TIM_PRSCL, 0xf) |
		     FIELD_PREP(VCD_RCHG_IG_CHG0, 0x3));

	/* Set video mode */
	regmap_write(vcd, VCD_MODE, VCD_MODE_VCDE | VCD_MODE_CM565 |
		     VCD_MODE_IDBC | VCD_MODE_KVM_BW_SET);
}

static int npcm_video_start_frame(struct npcm_video *video)
{
	struct npcm_video_buffer *buf;
	struct regmap *vcd = video->vcd_regmap;
	unsigned int val;
	int ret;

	if (video->v4l2_input_status) {
		dev_dbg(video->dev, "No video signal; skip capture frame\n");
		return 0;
	}

	ret = regmap_read_poll_timeout(vcd, VCD_STAT, val, !(val & VCD_STAT_BUSY),
				       1000, VCD_TIMEOUT_US);
	if (ret) {
		dev_err(video->dev, "Wait for VCD_STAT_BUSY timeout\n");
		return -EBUSY;
	}

	mutex_lock(&video->buffer_lock);
	buf = list_first_entry_or_null(&video->buffers,
				       struct npcm_video_buffer, link);
	if (!buf) {
		mutex_unlock(&video->buffer_lock);
		dev_dbg(video->dev, "No empty buffers; skip capture frame\n");
		return 0;
	}

	set_bit(VIDEO_CAPTURING, &video->flags);
	mutex_unlock(&video->buffer_lock);

	npcm_video_vcd_state_machine_reset(video);

	regmap_read(vcd, VCD_HOR_AC_TIM, &val);
	regmap_update_bits(vcd, VCD_HOR_AC_LST, VCD_HOR_AC_LAST,
			   FIELD_GET(VCD_HOR_AC_TIME, val));

	regmap_read(vcd, VCD_VER_HI_TIM, &val);
	regmap_update_bits(vcd, VCD_VER_HI_LST, VCD_VER_HI_LAST,
			   FIELD_GET(VCD_VER_HI_TIME, val));

	regmap_update_bits(vcd, VCD_INTE, VCD_INTE_DONE_IE | VCD_INTE_IFOT_IE |
			   VCD_INTE_IFOR_IE | VCD_INTE_HAC_IE | VCD_INTE_VHT_IE,
			   VCD_INTE_DONE_IE | VCD_INTE_IFOT_IE | VCD_INTE_IFOR_IE |
			   VCD_INTE_HAC_IE | VCD_INTE_VHT_IE);

	npcm_video_command(video, video->ctrl_cmd);

	return 0;
}

static void npcm_video_bufs_done(struct npcm_video *video,
				 enum vb2_buffer_state state)
{
	struct npcm_video_buffer *buf;

	mutex_lock(&video->buffer_lock);
	list_for_each_entry(buf, &video->buffers, link)
		vb2_buffer_done(&buf->vb.vb2_buf, state);

	INIT_LIST_HEAD(&video->buffers);
	mutex_unlock(&video->buffer_lock);
}

static void npcm_video_get_diff_rect(struct npcm_video *video, unsigned int index)
{
	unsigned int width = video->active_timings.width;
	unsigned int height = video->active_timings.height;

	if (video->op_cmd != VCD_CMD_OPERATION_CAPTURE) {
		video->rect_cnt = 0;
		npcm_video_get_rect_list(video, index);
		video->rect[index] = video->rect_cnt;
	} else {
		video->rect[index] = npcm_video_add_rect(video, index, 0, 0,
							 width, height);
	}
}

static void npcm_video_detect_resolution(struct npcm_video *video)
{
	struct v4l2_bt_timings *act = &video->active_timings;
	struct v4l2_bt_timings *det = &video->detected_timings;
	struct regmap *gfxi = video->gfx_regmap;
	unsigned int dispst;

	video->v4l2_input_status = V4L2_IN_ST_NO_SIGNAL;
	det->width = npcm_video_hres(video);
	det->height = npcm_video_vres(video);

	if (act->width != det->width || act->height != det->height) {
		dev_dbg(video->dev, "Resolution changed\n");

		if (npcm_video_hres(video) > 0 && npcm_video_vres(video) > 0) {
			if (test_bit(VIDEO_STREAMING, &video->flags)) {
				/*
				 * Wait for resolution is available,
				 * and it is also captured by host.
				 */
				do {
					mdelay(100);
					regmap_read(gfxi, DISPST, &dispst);
				} while (npcm_video_vres(video) < 100 ||
					 npcm_video_pclk(video) == 0 ||
					 (dispst & DISPST_HSCROFF));
			}

			det->width = npcm_video_hres(video);
			det->height = npcm_video_vres(video);
			det->pixelclock = npcm_video_pclk(video);
		}

		clear_bit(VIDEO_RES_CHANGING, &video->flags);
	}

	if (det->width && det->height)
		video->v4l2_input_status = 0;

	dev_dbg(video->dev, "Got resolution[%dx%d] -> [%dx%d], status %d\n",
		act->width, act->height, det->width, det->height,
		video->v4l2_input_status);
}

static int npcm_video_set_resolution(struct npcm_video *video,
				     struct v4l2_bt_timings *timing)
{
	struct regmap *vcd = video->vcd_regmap;
	unsigned int mode;

	if (npcm_video_capres(video, timing->width, timing->height)) {
		dev_err(video->dev, "Failed to set VCD_CAP_RES\n");
		return -EINVAL;
	}

	video->active_timings = *timing;
	video->bytesperpixel = npcm_video_get_bpp(video);
	npcm_video_set_linepitch(video, timing->width * video->bytesperpixel);
	video->bytesperline = npcm_video_get_linepitch(video);
	video->pix_fmt.width = timing->width ? timing->width : MIN_WIDTH;
	video->pix_fmt.height = timing->height ? timing->height : MIN_HEIGHT;
	video->pix_fmt.sizeimage = video->pix_fmt.width * video->pix_fmt.height *
				   video->bytesperpixel;
	video->pix_fmt.bytesperline = video->bytesperline;

	npcm_video_kvm_bw(video, timing->pixelclock > VCD_KVM_BW_PCLK);
	npcm_video_gfx_reset(video);
	regmap_read(vcd, VCD_MODE, &mode);

	dev_dbg(video->dev, "VCD mode = 0x%x, %s mode\n", mode,
		npcm_video_is_mga(video) ? "Hi Res" : "VGA");

	dev_dbg(video->dev,
		"Digital mode: %d x %d x %d, pixelclock %lld, bytesperline %d\n",
		timing->width, timing->height, video->bytesperpixel,
		timing->pixelclock, video->bytesperline);

	return 0;
}

static void npcm_video_start(struct npcm_video *video)
{
	npcm_video_init_reg(video);

	if (!npcm_video_alloc_fb(video, &video->src)) {
		dev_err(video->dev, "Failed to allocate VCD frame buffer\n");
		return;
	}

	npcm_video_detect_resolution(video);
	if (npcm_video_set_resolution(video, &video->detected_timings)) {
		dev_err(video->dev, "Failed to set resolution\n");
		return;
	}

	/* Set frame buffer physical address */
	regmap_write(video->vcd_regmap, VCD_FBA_ADR, video->src.dma);
	regmap_write(video->vcd_regmap, VCD_FBB_ADR, video->src.dma);

	if (video->ece.enable && atomic_inc_return(&video->ece.clients) == 1) {
		npcm_video_ece_ip_reset(video);
		npcm_video_ece_ctrl_reset(video);
		npcm_video_ece_set_fb_addr(video, video->src.dma);
		npcm_video_ece_set_lp(video, video->bytesperline);

		dev_dbg(video->dev, "ECE open: client %d\n",
			atomic_read(&video->ece.clients));
	}
}

static void npcm_video_stop(struct npcm_video *video)
{
	struct regmap *vcd = video->vcd_regmap;

	set_bit(VIDEO_STOPPED, &video->flags);

	regmap_write(vcd, VCD_INTE, 0);
	regmap_write(vcd, VCD_MODE, 0);
	regmap_write(vcd, VCD_RCHG, 0);
	regmap_write(vcd, VCD_STAT, VCD_STAT_CLEAR);

	if (video->src.size)
		npcm_video_free_fb(video, &video->src);

	npcm_video_free_diff_table(video);
	video->v4l2_input_status = V4L2_IN_ST_NO_SIGNAL;
	video->flags = 0;
	video->ctrl_cmd = VCD_CMD_OPERATION_CAPTURE;

	if (video->ece.enable && atomic_dec_return(&video->ece.clients) == 0) {
		npcm_video_ece_stop(video);
		dev_dbg(video->dev, "ECE close: client %d\n",
			atomic_read(&video->ece.clients));
	}
}

static unsigned int npcm_video_raw(struct npcm_video *video, int index, void *addr)
{
	unsigned int width = video->active_timings.width;
	unsigned int height = video->active_timings.height;
	unsigned int i, len, offset, bytes = 0;

	video->rect[index] = npcm_video_add_rect(video, index, 0, 0, width, height);

	for (i = 0; i < height; i++) {
		len = width * video->bytesperpixel;
		offset = i * video->bytesperline;

		memcpy(addr + bytes, video->src.virt + offset, len);
		bytes += len;
	}

	return bytes;
}

static unsigned int npcm_video_hextile(struct npcm_video *video, unsigned int index,
				       unsigned int dma_addr, void *vaddr)
{
	struct rect_list *rect_list;
	struct v4l2_rect *rect;
	unsigned int offset, len, bytes = 0;

	npcm_video_ece_ctrl_reset(video);
	npcm_video_ece_clear_rect_offset(video);
	npcm_video_ece_set_fb_addr(video, video->src.dma);

	/* Set base address of encoded data to video buffer */
	npcm_video_ece_set_enc_dba(video, dma_addr);

	npcm_video_ece_set_lp(video, video->bytesperline);
	npcm_video_get_diff_rect(video, index);

	list_for_each_entry(rect_list, &video->list[index], list) {
		rect = &rect_list->clip.c;
		offset = npcm_video_ece_read_rect_offset(video);
		npcm_video_ece_enc_rect(video, rect->left, rect->top,
					rect->width, rect->height);

		len = npcm_video_ece_get_ed_size(video, offset, vaddr);
		npcm_video_ece_prepend_rect_header(vaddr + offset,
						   rect->left, rect->top,
						   rect->width, rect->height);
		bytes += len;
	}

	return bytes;
}

static irqreturn_t npcm_video_irq(int irq, void *arg)
{
	struct npcm_video *video = arg;
	struct regmap *vcd = video->vcd_regmap;
	struct npcm_video_buffer *buf;
	unsigned int index, size, status, fmt;
	dma_addr_t dma_addr;
	void *addr;
	static const struct v4l2_event ev = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	regmap_read(vcd, VCD_STAT, &status);
	dev_dbg(video->dev, "VCD irq status 0x%x\n", status);

	regmap_write(vcd, VCD_STAT, VCD_STAT_CLEAR);

	if (test_bit(VIDEO_STOPPED, &video->flags) ||
	    !test_bit(VIDEO_STREAMING, &video->flags))
		return IRQ_NONE;

	if (status & VCD_STAT_DONE) {
		regmap_write(vcd, VCD_INTE, 0);
		mutex_lock(&video->buffer_lock);
		clear_bit(VIDEO_CAPTURING, &video->flags);
		buf = list_first_entry_or_null(&video->buffers,
					       struct npcm_video_buffer, link);
		if (!buf) {
			mutex_unlock(&video->buffer_lock);
			return IRQ_NONE;
		}

		addr = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
		index = buf->vb.vb2_buf.index;
		fmt = video->pix_fmt.pixelformat;

		switch (fmt) {
		case V4L2_PIX_FMT_RGB565:
			size = npcm_video_raw(video, index, addr);
			break;
		case V4L2_PIX_FMT_HEXTILE:
			dma_addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
			size = npcm_video_hextile(video, index, dma_addr, addr);
			break;
		default:
			mutex_unlock(&video->buffer_lock);
			return IRQ_NONE;
		}

		vb2_set_plane_payload(&buf->vb.vb2_buf, 0, size);
		buf->vb.vb2_buf.timestamp = ktime_get_ns();
		buf->vb.sequence = video->sequence++;
		buf->vb.field = V4L2_FIELD_NONE;

		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		list_del(&buf->link);
		mutex_unlock(&video->buffer_lock);

		if (npcm_video_start_frame(video))
			dev_err(video->dev, "Failed to capture next frame\n");
	}

	/* Resolution changed */
	if (status & VCD_STAT_VHT_CHG || status & VCD_STAT_HAC_CHG) {
		if (!test_bit(VIDEO_RES_CHANGING, &video->flags)) {
			set_bit(VIDEO_RES_CHANGING, &video->flags);

			vb2_queue_error(&video->queue);
			v4l2_event_queue(&video->vdev, &ev);
		}
	}

	if (status & VCD_STAT_IFOR || status & VCD_STAT_IFOT) {
		dev_warn(video->dev, "VCD FIFO overrun or over thresholds\n");
		if (npcm_video_start_frame(video))
			dev_err(video->dev, "Failed to recover from FIFO overrun\n");
	}

	return IRQ_HANDLED;
}

static int npcm_video_querycap(struct file *file, void *fh,
			       struct v4l2_capability *cap)
{
	strscpy(cap->driver, DEVICE_NAME, sizeof(cap->driver));
	strscpy(cap->card, "NPCM Video Engine", sizeof(cap->card));

	return 0;
}

static int npcm_video_enum_format(struct file *file, void *fh,
				  struct v4l2_fmtdesc *f)
{
	struct npcm_video *video = video_drvdata(file);
	const struct npcm_fmt *fmt;

	if (f->index >= NUM_FORMATS)
		return -EINVAL;

	fmt = &npcm_fmt_list[f->index];
	if (fmt->fourcc == V4L2_PIX_FMT_HEXTILE && !video->ece.enable)
		return -EINVAL;

	f->pixelformat = fmt->fourcc;
	return 0;
}

static int npcm_video_try_format(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct npcm_video *video = video_drvdata(file);
	const struct npcm_fmt *fmt;

	fmt = npcm_video_find_format(f);

	/* If format not found or HEXTILE not supported, use RGB565 as default */
	if (!fmt || (fmt->fourcc == V4L2_PIX_FMT_HEXTILE && !video->ece.enable))
		f->fmt.pix.pixelformat = npcm_fmt_list[0].fourcc;

	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	f->fmt.pix.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	f->fmt.pix.width = video->pix_fmt.width;
	f->fmt.pix.height = video->pix_fmt.height;
	f->fmt.pix.bytesperline = video->bytesperline;
	f->fmt.pix.sizeimage = video->pix_fmt.sizeimage;

	return 0;
}

static int npcm_video_get_format(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct npcm_video *video = video_drvdata(file);

	f->fmt.pix = video->pix_fmt;
	return 0;
}

static int npcm_video_set_format(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct npcm_video *video = video_drvdata(file);
	int ret;

	ret = npcm_video_try_format(file, fh, f);
	if (ret)
		return ret;

	if (vb2_is_busy(&video->queue)) {
		dev_err(video->dev, "%s device busy\n", __func__);
		return -EBUSY;
	}

	video->pix_fmt.pixelformat = f->fmt.pix.pixelformat;
	return 0;
}

static int npcm_video_enum_input(struct file *file, void *fh,
				 struct v4l2_input *inp)
{
	struct npcm_video *video = video_drvdata(file);

	if (inp->index)
		return -EINVAL;

	strscpy(inp->name, "Host VGA capture", sizeof(inp->name));
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->capabilities = V4L2_IN_CAP_DV_TIMINGS;
	inp->status = video->v4l2_input_status;

	return 0;
}

static int npcm_video_get_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;

	return 0;
}

static int npcm_video_set_input(struct file *file, void *fh, unsigned int i)
{
	if (i)
		return -EINVAL;

	return 0;
}

static int npcm_video_set_dv_timings(struct file *file, void *fh,
				     struct v4l2_dv_timings *timings)
{
	struct npcm_video *video = video_drvdata(file);
	int rc;

	if (timings->bt.width == video->active_timings.width &&
	    timings->bt.height == video->active_timings.height)
		return 0;

	if (vb2_is_busy(&video->queue)) {
		dev_err(video->dev, "%s device busy\n", __func__);
		return -EBUSY;
	}

	rc = npcm_video_set_resolution(video, &timings->bt);
	if (rc)
		return rc;

	timings->type = V4L2_DV_BT_656_1120;

	return 0;
}

static int npcm_video_get_dv_timings(struct file *file, void *fh,
				     struct v4l2_dv_timings *timings)
{
	struct npcm_video *video = video_drvdata(file);

	timings->type = V4L2_DV_BT_656_1120;
	timings->bt = video->active_timings;

	return 0;
}

static int npcm_video_query_dv_timings(struct file *file, void *fh,
				       struct v4l2_dv_timings *timings)
{
	struct npcm_video *video = video_drvdata(file);

	npcm_video_detect_resolution(video);
	timings->type = V4L2_DV_BT_656_1120;
	timings->bt = video->detected_timings;

	return video->v4l2_input_status ? -ENOLINK : 0;
}

static int npcm_video_enum_dv_timings(struct file *file, void *fh,
				      struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &npcm_video_timings_cap,
					NULL, NULL);
}

static int npcm_video_dv_timings_cap(struct file *file, void *fh,
				     struct v4l2_dv_timings_cap *cap)
{
	*cap = npcm_video_timings_cap;

	return 0;
}

static int npcm_video_sub_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	}

	return v4l2_ctrl_subscribe_event(fh, sub);
}

static const struct v4l2_ioctl_ops npcm_video_ioctls = {
	.vidioc_querycap = npcm_video_querycap,

	.vidioc_enum_fmt_vid_cap = npcm_video_enum_format,
	.vidioc_g_fmt_vid_cap = npcm_video_get_format,
	.vidioc_s_fmt_vid_cap = npcm_video_set_format,
	.vidioc_try_fmt_vid_cap = npcm_video_try_format,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,

	.vidioc_enum_input = npcm_video_enum_input,
	.vidioc_g_input = npcm_video_get_input,
	.vidioc_s_input = npcm_video_set_input,

	.vidioc_s_dv_timings = npcm_video_set_dv_timings,
	.vidioc_g_dv_timings = npcm_video_get_dv_timings,
	.vidioc_query_dv_timings = npcm_video_query_dv_timings,
	.vidioc_enum_dv_timings = npcm_video_enum_dv_timings,
	.vidioc_dv_timings_cap = npcm_video_dv_timings_cap,

	.vidioc_subscribe_event = npcm_video_sub_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int npcm_video_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct npcm_video *video = container_of(ctrl->handler, struct npcm_video,
						ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_NPCM_CAPTURE_MODE:
		if (ctrl->val == V4L2_NPCM_CAPTURE_MODE_COMPLETE)
			video->ctrl_cmd = VCD_CMD_OPERATION_CAPTURE;
		else if (ctrl->val == V4L2_NPCM_CAPTURE_MODE_DIFF)
			video->ctrl_cmd = VCD_CMD_OPERATION_COMPARE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops npcm_video_ctrl_ops = {
	.s_ctrl = npcm_video_set_ctrl,
};

static const char * const npcm_ctrl_capture_mode_menu[] = {
	"COMPLETE",
	"DIFF",
	NULL,
};

static const struct v4l2_ctrl_config npcm_ctrl_capture_mode = {
	.ops = &npcm_video_ctrl_ops,
	.id = V4L2_CID_NPCM_CAPTURE_MODE,
	.name = "NPCM Video Capture Mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.min = 0,
	.max = V4L2_NPCM_CAPTURE_MODE_DIFF,
	.def = 0,
	.qmenu = npcm_ctrl_capture_mode_menu,
};

/*
 * This control value is set when a buffer is dequeued by userspace, i.e. in
 * npcm_video_buf_finish function.
 */
static const struct v4l2_ctrl_config npcm_ctrl_rect_count = {
	.id = V4L2_CID_NPCM_RECT_COUNT,
	.name = "NPCM Hextile Rectangle Count",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (MAX_WIDTH / RECT_W) * (MAX_HEIGHT / RECT_H),
	.step = 1,
	.def = 0,
};

static int npcm_video_open(struct file *file)
{
	struct npcm_video *video = video_drvdata(file);
	int rc;

	mutex_lock(&video->video_lock);
	rc = v4l2_fh_open(file);
	if (rc) {
		mutex_unlock(&video->video_lock);
		return rc;
	}

	if (v4l2_fh_is_singular_file(file))
		npcm_video_start(video);

	mutex_unlock(&video->video_lock);
	return 0;
}

static int npcm_video_release(struct file *file)
{
	struct npcm_video *video = video_drvdata(file);
	int rc;

	mutex_lock(&video->video_lock);
	if (v4l2_fh_is_singular_file(file))
		npcm_video_stop(video);

	rc = _vb2_fop_release(file, NULL);

	mutex_unlock(&video->video_lock);
	return rc;
}

static const struct v4l2_file_operations npcm_video_v4l2_fops = {
	.owner = THIS_MODULE,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
	.open = npcm_video_open,
	.release = npcm_video_release,
};

static int npcm_video_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
				  unsigned int *num_planes, unsigned int sizes[],
				  struct device *alloc_devs[])
{
	struct npcm_video *video = vb2_get_drv_priv(q);
	unsigned int i;

	if (*num_planes) {
		if (sizes[0] < video->pix_fmt.sizeimage)
			return -EINVAL;

		return 0;
	}

	*num_planes = 1;
	sizes[0] = video->pix_fmt.sizeimage;

	for (i = 0; i < VIDEO_MAX_FRAME; i++)
		INIT_LIST_HEAD(&video->list[i]);

	return 0;
}

static int npcm_video_buf_prepare(struct vb2_buffer *vb)
{
	struct npcm_video *video = vb2_get_drv_priv(vb->vb2_queue);

	if (vb2_plane_size(vb, 0) < video->pix_fmt.sizeimage)
		return -EINVAL;

	return 0;
}

static int npcm_video_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct npcm_video *video = vb2_get_drv_priv(q);
	int rc;

	video->sequence = 0;
	rc = npcm_video_start_frame(video);
	if (rc) {
		npcm_video_bufs_done(video, VB2_BUF_STATE_QUEUED);
		return rc;
	}

	set_bit(VIDEO_STREAMING, &video->flags);
	return 0;
}

static void npcm_video_stop_streaming(struct vb2_queue *q)
{
	struct npcm_video *video = vb2_get_drv_priv(q);
	struct regmap *vcd = video->vcd_regmap;

	clear_bit(VIDEO_STREAMING, &video->flags);
	regmap_write(vcd, VCD_INTE, 0);
	regmap_write(vcd, VCD_STAT, VCD_STAT_CLEAR);
	npcm_video_gfx_reset(video);
	npcm_video_bufs_done(video, VB2_BUF_STATE_ERROR);
	video->ctrl_cmd = VCD_CMD_OPERATION_CAPTURE;
	v4l2_ctrl_s_ctrl(video->rect_cnt_ctrl, 0);
}

static void npcm_video_buf_queue(struct vb2_buffer *vb)
{
	struct npcm_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct npcm_video_buffer *nvb = to_npcm_video_buffer(vbuf);
	bool empty;

	mutex_lock(&video->buffer_lock);
	empty = list_empty(&video->buffers);
	list_add_tail(&nvb->link, &video->buffers);
	mutex_unlock(&video->buffer_lock);

	if (test_bit(VIDEO_STREAMING, &video->flags) &&
	    !test_bit(VIDEO_CAPTURING, &video->flags) && empty) {
		if (npcm_video_start_frame(video))
			dev_err(video->dev, "Failed to capture next frame\n");
	}
}

static void npcm_video_buf_finish(struct vb2_buffer *vb)
{
	struct npcm_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct list_head *head, *pos, *nx;
	struct rect_list *tmp;

	/*
	 * This callback is called when the buffer is dequeued, so update
	 * V4L2_CID_NPCM_RECT_COUNT control value with the number of rectangles
	 * in this buffer and free associated rect_list.
	 */
	if (test_bit(VIDEO_STREAMING, &video->flags)) {
		v4l2_ctrl_s_ctrl(video->rect_cnt_ctrl, video->rect[vb->index]);

		head = &video->list[vb->index];
		list_for_each_safe(pos, nx, head) {
			tmp = list_entry(pos, struct rect_list, list);
			list_del(&tmp->list);
			kfree(tmp);
		}
	}
}

static const struct regmap_config npcm_video_regmap_cfg = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= VCD_FIFO,
};

static const struct regmap_config npcm_video_ece_regmap_cfg = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= ECE_HEX_RECT_OFFSET,
};

static const struct vb2_ops npcm_video_vb2_ops = {
	.queue_setup = npcm_video_queue_setup,
	.buf_prepare = npcm_video_buf_prepare,
	.buf_finish = npcm_video_buf_finish,
	.start_streaming = npcm_video_start_streaming,
	.stop_streaming = npcm_video_stop_streaming,
	.buf_queue =  npcm_video_buf_queue,
};

static int npcm_video_setup_video(struct npcm_video *video)
{
	struct v4l2_device *v4l2_dev = &video->v4l2_dev;
	struct video_device *vdev = &video->vdev;
	struct vb2_queue *vbq = &video->queue;
	int rc;

	if (video->ece.enable)
		video->pix_fmt.pixelformat = V4L2_PIX_FMT_HEXTILE;
	else
		video->pix_fmt.pixelformat = V4L2_PIX_FMT_RGB565;

	video->pix_fmt.field = V4L2_FIELD_NONE;
	video->pix_fmt.colorspace = V4L2_COLORSPACE_SRGB;
	video->pix_fmt.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	video->v4l2_input_status = V4L2_IN_ST_NO_SIGNAL;

	rc = v4l2_device_register(video->dev, v4l2_dev);
	if (rc) {
		dev_err(video->dev, "Failed to register v4l2 device\n");
		return rc;
	}

	v4l2_ctrl_handler_init(&video->ctrl_handler, 2);
	v4l2_ctrl_new_custom(&video->ctrl_handler, &npcm_ctrl_capture_mode, NULL);
	video->rect_cnt_ctrl = v4l2_ctrl_new_custom(&video->ctrl_handler,
						    &npcm_ctrl_rect_count, NULL);
	if (video->ctrl_handler.error) {
		dev_err(video->dev, "Failed to init controls: %d\n",
			video->ctrl_handler.error);

		rc = video->ctrl_handler.error;
		goto rel_ctrl_handler;
	}
	v4l2_dev->ctrl_handler = &video->ctrl_handler;

	vbq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vbq->io_modes = VB2_MMAP | VB2_DMABUF;
	vbq->dev = v4l2_dev->dev;
	vbq->lock = &video->video_lock;
	vbq->ops = &npcm_video_vb2_ops;
	vbq->mem_ops = &vb2_dma_contig_memops;
	vbq->drv_priv = video;
	vbq->buf_struct_size = sizeof(struct npcm_video_buffer);
	vbq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vbq->min_queued_buffers = 3;

	rc = vb2_queue_init(vbq);
	if (rc) {
		dev_err(video->dev, "Failed to init vb2 queue\n");
		goto rel_ctrl_handler;
	}
	vdev->queue = vbq;
	vdev->fops = &npcm_video_v4l2_fops;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	vdev->v4l2_dev = v4l2_dev;
	strscpy(vdev->name, DEVICE_NAME, sizeof(vdev->name));
	vdev->vfl_type = VFL_TYPE_VIDEO;
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->release = video_device_release_empty;
	vdev->ioctl_ops = &npcm_video_ioctls;
	vdev->lock = &video->video_lock;

	video_set_drvdata(vdev, video);
	rc = video_register_device(vdev, VFL_TYPE_VIDEO, 0);
	if (rc) {
		dev_err(video->dev, "Failed to register video device\n");
		goto rel_vb_queue;
	}

	return 0;

rel_vb_queue:
	vb2_queue_release(vbq);
rel_ctrl_handler:
	v4l2_ctrl_handler_free(&video->ctrl_handler);
	v4l2_device_unregister(v4l2_dev);

	return rc;
}

static int npcm_video_ece_init(struct npcm_video *video)
{
	struct device_node *ece_node __free(device_node) = NULL;
	struct device *dev = video->dev;
	struct platform_device *ece_pdev;
	void __iomem *regs;

	ece_node = of_parse_phandle(video->dev->of_node, "nuvoton,ece", 0);
	if (!ece_node) {
		dev_err(dev, "Failed to get ECE phandle in DTS\n");
		return -ENODEV;
	}

	video->ece.enable = of_device_is_available(ece_node);

	if (video->ece.enable) {
		dev_info(dev, "Support HEXTILE pixel format\n");

		ece_pdev = of_find_device_by_node(ece_node);
		if (!ece_pdev) {
			dev_err(dev, "Failed to find ECE device\n");
			return -ENODEV;
		}
		struct device *ece_dev __free(put_device) = &ece_pdev->dev;

		regs = devm_platform_ioremap_resource(ece_pdev, 0);
		if (IS_ERR(regs)) {
			dev_err(dev, "Failed to parse ECE reg in DTS\n");
			return PTR_ERR(regs);
		}

		video->ece.regmap = devm_regmap_init_mmio(dev, regs,
							  &npcm_video_ece_regmap_cfg);
		if (IS_ERR(video->ece.regmap)) {
			dev_err(dev, "Failed to initialize ECE regmap\n");
			return PTR_ERR(video->ece.regmap);
		}

		video->ece.reset = devm_reset_control_get(ece_dev, NULL);
		if (IS_ERR(video->ece.reset)) {
			dev_err(dev, "Failed to get ECE reset control in DTS\n");
			return PTR_ERR(video->ece.reset);
		}
	}

	return 0;
}

static int npcm_video_init(struct npcm_video *video)
{
	struct device *dev = video->dev;
	int irq, rc;

	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (!irq) {
		dev_err(dev, "Failed to find VCD IRQ\n");
		return -ENODEV;
	}

	rc = devm_request_threaded_irq(dev, irq, NULL, npcm_video_irq,
				       IRQF_ONESHOT, DEVICE_NAME, video);
	if (rc < 0) {
		dev_err(dev, "Failed to request IRQ %d\n", irq);
		return rc;
	}

	of_reserved_mem_device_init(dev);
	rc = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(dev, "Failed to set DMA mask\n");
		of_reserved_mem_device_release(dev);
	}

	rc = npcm_video_ece_init(video);
	if (rc) {
		dev_err(dev, "Failed to initialize ECE\n");
		return rc;
	}

	return 0;
}

static int npcm_video_probe(struct platform_device *pdev)
{
	struct npcm_video *video = kzalloc(sizeof(*video), GFP_KERNEL);
	int rc;
	void __iomem *regs;

	if (!video)
		return -ENOMEM;

	video->dev = &pdev->dev;
	mutex_init(&video->video_lock);
	mutex_init(&video->buffer_lock);
	INIT_LIST_HEAD(&video->buffers);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs)) {
		dev_err(&pdev->dev, "Failed to parse VCD reg in DTS\n");
		return PTR_ERR(regs);
	}

	video->vcd_regmap = devm_regmap_init_mmio(&pdev->dev, regs,
						  &npcm_video_regmap_cfg);
	if (IS_ERR(video->vcd_regmap)) {
		dev_err(&pdev->dev, "Failed to initialize VCD regmap\n");
		return PTR_ERR(video->vcd_regmap);
	}

	video->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(video->reset)) {
		dev_err(&pdev->dev, "Failed to get VCD reset control in DTS\n");
		return PTR_ERR(video->reset);
	}

	video->gcr_regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							    "nuvoton,sysgcr");
	if (IS_ERR(video->gcr_regmap))
		return PTR_ERR(video->gcr_regmap);

	video->gfx_regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							    "nuvoton,sysgfxi");
	if (IS_ERR(video->gfx_regmap))
		return PTR_ERR(video->gfx_regmap);

	rc = npcm_video_init(video);
	if (rc)
		return rc;

	rc = npcm_video_setup_video(video);
	if (rc)
		return rc;

	dev_info(video->dev, "NPCM video driver probed\n");
	return 0;
}

static void npcm_video_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev = dev_get_drvdata(dev);
	struct npcm_video *video = to_npcm_video(v4l2_dev);

	video_unregister_device(&video->vdev);
	vb2_queue_release(&video->queue);
	v4l2_ctrl_handler_free(&video->ctrl_handler);
	v4l2_device_unregister(v4l2_dev);
	if (video->ece.enable)
		npcm_video_ece_stop(video);
	of_reserved_mem_device_release(dev);
}

static const struct of_device_id npcm_video_match[] = {
	{ .compatible = "nuvoton,npcm750-vcd" },
	{ .compatible = "nuvoton,npcm845-vcd" },
	{},
};

MODULE_DEVICE_TABLE(of, npcm_video_match);

static struct platform_driver npcm_video_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = npcm_video_match,
	},
	.probe = npcm_video_probe,
	.remove = npcm_video_remove,
};

module_platform_driver(npcm_video_driver);

MODULE_AUTHOR("Joseph Liu <kwliu@nuvoton.com>");
MODULE_AUTHOR("Marvin Lin <kflin@nuvoton.com>");
MODULE_DESCRIPTION("Driver for Nuvoton NPCM Video Capture/Encode Engine");
MODULE_LICENSE("GPL v2");
