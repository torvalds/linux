/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/console.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/ipu-v3.h>
#include <linux/module.h>
#include <linux/mxcfb.h>
#include <linux/mxc_v4l2.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include <media/videobuf-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#define UYVY_BLACK	(0x00800080)
#define RGB_BLACK	(0x0)
#define UV_BLACK	(0x80)
#define Y_BLACK		(0x0)

#define MAX_FB_NUM	6
#define FB_BUFS		3
#define VDOA_FB_BUFS	(FB_BUFS - 1)
#define VALID_HEIGHT_1080P	(1080)
#define FRAME_HEIGHT_1080P	(1088)
#define FRAME_WIDTH_1080P	(1920)
#define CHECK_TILED_1080P_DISPLAY(vout)	\
	((((vout)->task.input.format == IPU_PIX_FMT_TILED_NV12) ||	\
	       ((vout)->task.input.format == IPU_PIX_FMT_TILED_NV12F)) &&\
	       ((vout)->task.input.width == FRAME_WIDTH_1080P) &&	\
	       ((vout)->task.input.height == FRAME_HEIGHT_1080P) &&	\
	       ((vout)->task.input.crop.w == FRAME_WIDTH_1080P) &&	\
	       (((vout)->task.input.crop.h == FRAME_HEIGHT_1080P) ||	\
	       ((vout)->task.input.crop.h == VALID_HEIGHT_1080P)) &&	\
	       ((vout)->task.output.width == FRAME_WIDTH_1080P) &&	\
	       ((vout)->task.output.height == VALID_HEIGHT_1080P) &&	\
	       ((vout)->task.output.crop.w == FRAME_WIDTH_1080P) &&	\
	       ((vout)->task.output.crop.h == VALID_HEIGHT_1080P))
#define CHECK_TILED_1080P_STREAM(vout)	\
	((((vout)->task.input.format == IPU_PIX_FMT_TILED_NV12) ||	\
	       ((vout)->task.input.format == IPU_PIX_FMT_TILED_NV12F)) &&\
	       ((vout)->task.input.width == FRAME_WIDTH_1080P) &&	\
	       ((vout)->task.input.crop.w == FRAME_WIDTH_1080P) &&	\
	       ((vout)->task.input.height == FRAME_HEIGHT_1080P) &&	\
	       ((vout)->task.input.crop.h == FRAME_HEIGHT_1080P))
#define IS_PLANAR_PIXEL_FORMAT(format) \
	(format == IPU_PIX_FMT_NV12 ||		\
	    format == IPU_PIX_FMT_YUV420P2 ||	\
	    format == IPU_PIX_FMT_YUV420P ||	\
	    format == IPU_PIX_FMT_YVU420P ||	\
	    format == IPU_PIX_FMT_YUV422P ||	\
	    format == IPU_PIX_FMT_YVU422P ||	\
	    format == IPU_PIX_FMT_YUV444P)

#define NSEC_PER_FRAME_30FPS		(33333333)

struct mxc_vout_fb {
	char *name;
	int ipu_id;
	struct v4l2_rect crop_bounds;
	unsigned int disp_fmt;
	bool disp_support_csc;
	bool disp_support_windows;
};

struct dma_mem {
	void *vaddr;
	dma_addr_t paddr;
	size_t size;
};

struct mxc_vout_output {
	int open_cnt;
	struct fb_info *fbi;
	unsigned long fb_smem_start;
	unsigned long fb_smem_len;
	struct video_device *vfd;
	struct mutex mutex;
	struct mutex task_lock;
	struct mutex accs_lock;
	enum v4l2_buf_type type;

	struct videobuf_queue vbq;
	spinlock_t vbq_lock;

	struct list_head queue_list;
	struct list_head active_list;

	struct v4l2_rect crop_bounds;
	unsigned int disp_fmt;
	struct mxcfb_pos win_pos;
	bool disp_support_windows;
	bool disp_support_csc;

	bool fmt_init;
	bool release;
	bool linear_bypass_pp;
	bool vdoa_1080p;
	bool tiled_bypass_pp;
	struct v4l2_rect in_rect;
	struct ipu_task	task;
	struct ipu_task	vdoa_task;
	struct dma_mem vdoa_work;
	struct dma_mem vdoa_output[VDOA_FB_BUFS];

	bool timer_stop;
	struct hrtimer timer;
	struct workqueue_struct *v4l_wq;
	struct work_struct disp_work;
	unsigned long frame_count;
	unsigned long vdi_frame_cnt;
	ktime_t start_ktime;

	int ctrl_rotate;
	int ctrl_vflip;
	int ctrl_hflip;

	dma_addr_t disp_bufs[FB_BUFS];

	struct videobuf_buffer *pre1_vb;
	struct videobuf_buffer *pre2_vb;

	bool input_crop;
};

struct mxc_vout_dev {
	struct device	*dev;
	struct v4l2_device v4l2_dev;
	struct mxc_vout_output *out[MAX_FB_NUM];
	int out_num;
};

/* Driver Configuration macros */
#define VOUT_NAME		"mxc_vout"

/* Variables configurable through module params*/
static int debug;
static int vdi_rate_double;
static int video_nr = 16;

static int mxc_vidioc_s_input_crop(struct mxc_vout_output *vout,
				const struct v4l2_crop *crop);
static int mxc_vidioc_g_input_crop(struct mxc_vout_output *vout,
				struct v4l2_crop *crop);
/* Module parameters */
module_param(video_nr, int, S_IRUGO);
MODULE_PARM_DESC(video_nr, "video device numbers");
module_param(debug, int, 0600);
MODULE_PARM_DESC(debug, "Debug level (0-1)");
module_param(vdi_rate_double, int, 0600);
MODULE_PARM_DESC(vdi_rate_double, "vdi frame rate double on/off");

static const struct v4l2_fmtdesc mxc_formats[] = {
	{
		.description = "RGB565",
		.pixelformat = V4L2_PIX_FMT_RGB565,
	},
	{
		.description = "BGR24",
		.pixelformat = V4L2_PIX_FMT_BGR24,
	},
	{
		.description = "RGB24",
		.pixelformat = V4L2_PIX_FMT_RGB24,
	},
	{
		.description = "RGB32",
		.pixelformat = V4L2_PIX_FMT_RGB32,
	},
	{
		.description = "BGR32",
		.pixelformat = V4L2_PIX_FMT_BGR32,
	},
	{
		.description = "NV12",
		.pixelformat = V4L2_PIX_FMT_NV12,
	},
	{
		.description = "UYVY",
		.pixelformat = V4L2_PIX_FMT_UYVY,
	},
	{
		.description = "YUYV",
		.pixelformat = V4L2_PIX_FMT_YUYV,
	},
	{
		.description = "YUV422 planar",
		.pixelformat = V4L2_PIX_FMT_YUV422P,
	},
	{
		.description = "YUV444",
		.pixelformat = V4L2_PIX_FMT_YUV444,
	},
	{
		.description = "YUV420",
		.pixelformat = V4L2_PIX_FMT_YUV420,
	},
	{
		.description = "YVU420",
		.pixelformat = V4L2_PIX_FMT_YVU420,
	},
	{
		.description = "TILED NV12P",
		.pixelformat = IPU_PIX_FMT_TILED_NV12,
	},
	{
		.description = "TILED NV12F",
		.pixelformat = IPU_PIX_FMT_TILED_NV12F,
	},
	{
		.description = "YUV444 planar",
		.pixelformat = IPU_PIX_FMT_YUV444P,
	},
};

#define NUM_MXC_VOUT_FORMATS (ARRAY_SIZE(mxc_formats))

#define DEF_INPUT_WIDTH		320
#define DEF_INPUT_HEIGHT	240

static int mxc_vidioc_streamoff(struct file *file, void *fh,
					enum v4l2_buf_type i);

static struct mxc_vout_fb g_fb_setting[MAX_FB_NUM];
static int config_disp_output(struct mxc_vout_output *vout);
static void release_disp_output(struct mxc_vout_output *vout);

static DEFINE_MUTEX(gfb_mutex);
static DEFINE_MUTEX(gfbi_mutex);

static unsigned int get_frame_size(struct mxc_vout_output *vout)
{
	unsigned int size;

	if (IPU_PIX_FMT_TILED_NV12 == vout->task.input.format)
		size = TILED_NV12_FRAME_SIZE(vout->task.input.width,
					vout->task.input.height);
	else if (IPU_PIX_FMT_TILED_NV12F == vout->task.input.format) {
		size = TILED_NV12_FRAME_SIZE(vout->task.input.width,
					vout->task.input.height/2);
		size *= 2;
	} else
		size = vout->task.input.width * vout->task.input.height *
				fmt_to_bpp(vout->task.input.format)/8;

	return size;
}

static void free_dma_buf(struct mxc_vout_output *vout, struct dma_mem *buf)
{
	dma_free_coherent(vout->vbq.dev, buf->size, buf->vaddr, buf->paddr);
	v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
			"free dma size:0x%x, paddr:0x%x\n",
			buf->size, buf->paddr);
	memset(buf, 0, sizeof(*buf));
}

static int alloc_dma_buf(struct mxc_vout_output *vout, struct dma_mem *buf)
{

	buf->vaddr = dma_alloc_coherent(vout->vbq.dev, buf->size, &buf->paddr,
						GFP_DMA | GFP_KERNEL);
	if (!buf->vaddr) {
		v4l2_err(vout->vfd->v4l2_dev,
			"cannot get dma buf size:0x%x\n", buf->size);
		return -ENOMEM;
	}
	v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
		"alloc dma buf size:0x%x, paddr:0x%x\n", buf->size, buf->paddr);
	return 0;
}

static ipu_channel_t get_ipu_channel(struct fb_info *fbi)
{
	ipu_channel_t ipu_ch = CHAN_NONE;
	mm_segment_t old_fs;

	if (fbi->fbops->fb_ioctl) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		fbi->fbops->fb_ioctl(fbi, MXCFB_GET_FB_IPU_CHAN,
				(unsigned long)&ipu_ch);
		set_fs(old_fs);
	}

	return ipu_ch;
}

static unsigned int get_ipu_fmt(struct fb_info *fbi)
{
	mm_segment_t old_fs;
	unsigned int fb_fmt;

	if (fbi->fbops->fb_ioctl) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		fbi->fbops->fb_ioctl(fbi, MXCFB_GET_DIFMT,
				(unsigned long)&fb_fmt);
		set_fs(old_fs);
	}

	return fb_fmt;
}

static void update_display_setting(void)
{
	int i;
	struct fb_info *fbi;
	struct v4l2_rect bg_crop_bounds[2];

	mutex_lock(&gfb_mutex);
	for (i = 0; i < num_registered_fb; i++) {
		fbi = registered_fb[i];

		memset(&g_fb_setting[i], 0, sizeof(struct mxc_vout_fb));

		if (!strncmp(fbi->fix.id, "DISP3", 5))
			g_fb_setting[i].ipu_id = 0;
		else
			g_fb_setting[i].ipu_id = 1;

		g_fb_setting[i].name = fbi->fix.id;
		g_fb_setting[i].crop_bounds.left = 0;
		g_fb_setting[i].crop_bounds.top = 0;
		g_fb_setting[i].crop_bounds.width = fbi->var.xres;
		g_fb_setting[i].crop_bounds.height = fbi->var.yres;
		g_fb_setting[i].disp_fmt = get_ipu_fmt(fbi);

		if (get_ipu_channel(fbi) == MEM_BG_SYNC) {
			bg_crop_bounds[g_fb_setting[i].ipu_id] =
				g_fb_setting[i].crop_bounds;
			g_fb_setting[i].disp_support_csc = true;
		} else if (get_ipu_channel(fbi) == MEM_FG_SYNC) {
			g_fb_setting[i].disp_support_csc = true;
			g_fb_setting[i].disp_support_windows = true;
		}
	}

	for (i = 0; i < num_registered_fb; i++) {
		fbi = registered_fb[i];

		if (get_ipu_channel(fbi) == MEM_FG_SYNC)
			g_fb_setting[i].crop_bounds =
				bg_crop_bounds[g_fb_setting[i].ipu_id];
	}
	mutex_unlock(&gfb_mutex);
}

/* called after g_fb_setting filled by update_display_setting */
static int update_setting_from_fbi(struct mxc_vout_output *vout,
			struct fb_info *fbi)
{
	int i;
	bool found = false;

	mutex_lock(&gfbi_mutex);

	update_display_setting();

	for (i = 0; i < MAX_FB_NUM; i++) {
		if (g_fb_setting[i].name) {
			if (!strcmp(fbi->fix.id, g_fb_setting[i].name)) {
				vout->crop_bounds = g_fb_setting[i].crop_bounds;
				vout->disp_fmt = g_fb_setting[i].disp_fmt;
				vout->disp_support_csc =
					g_fb_setting[i].disp_support_csc;
				vout->disp_support_windows =
					g_fb_setting[i].disp_support_windows;
				found = true;
				break;
			}
		}
	}

	if (!found) {
		v4l2_err(vout->vfd->v4l2_dev, "can not find output\n");
		mutex_unlock(&gfbi_mutex);
		return -EINVAL;
	}
	strlcpy(vout->vfd->name, fbi->fix.id, sizeof(vout->vfd->name));

	memset(&vout->task, 0, sizeof(struct ipu_task));

	vout->task.input.width = DEF_INPUT_WIDTH;
	vout->task.input.height = DEF_INPUT_HEIGHT;
	vout->task.input.crop.pos.x = 0;
	vout->task.input.crop.pos.y = 0;
	vout->task.input.crop.w = DEF_INPUT_WIDTH;
	vout->task.input.crop.h = DEF_INPUT_HEIGHT;
	vout->input_crop = false;

	vout->task.output.width = vout->crop_bounds.width;
	vout->task.output.height = vout->crop_bounds.height;
	vout->task.output.crop.pos.x = 0;
	vout->task.output.crop.pos.y = 0;
	vout->task.output.crop.w = vout->crop_bounds.width;
	vout->task.output.crop.h = vout->crop_bounds.height;
	if (colorspaceofpixel(vout->disp_fmt) == YUV_CS)
		vout->task.output.format = IPU_PIX_FMT_UYVY;
	else
		vout->task.output.format = IPU_PIX_FMT_RGB565;

	mutex_unlock(&gfbi_mutex);
	return 0;
}

static inline unsigned long get_jiffies(struct timeval *t)
{
	struct timeval cur;

	if (t->tv_usec >= 1000000) {
		t->tv_sec += t->tv_usec / 1000000;
		t->tv_usec = t->tv_usec % 1000000;
	}

	do_gettimeofday(&cur);
	if ((t->tv_sec < cur.tv_sec)
	    || ((t->tv_sec == cur.tv_sec) && (t->tv_usec < cur.tv_usec)))
		return jiffies;

	if (t->tv_usec < cur.tv_usec) {
		cur.tv_sec = t->tv_sec - cur.tv_sec - 1;
		cur.tv_usec = t->tv_usec + 1000000 - cur.tv_usec;
	} else {
		cur.tv_sec = t->tv_sec - cur.tv_sec;
		cur.tv_usec = t->tv_usec - cur.tv_usec;
	}

	return jiffies + timeval_to_jiffies(&cur);
}

static bool deinterlace_3_field(struct mxc_vout_output *vout)
{
	return (vout->task.input.deinterlace.enable &&
		(vout->task.input.deinterlace.motion != HIGH_MOTION));
}

static int set_field_fmt(struct mxc_vout_output *vout, enum v4l2_field field)
{
	struct ipu_deinterlace *deinterlace = &vout->task.input.deinterlace;

	switch (field) {
	/* Images are in progressive format, not interlaced */
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_ANY:
		deinterlace->enable = false;
		deinterlace->field_fmt = 0;
		v4l2_dbg(1, debug, vout->vfd->v4l2_dev, "Progressive frame.\n");
		break;
	case V4L2_FIELD_INTERLACED_TB:
		v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
				"Enable deinterlace TB.\n");
		deinterlace->enable = true;
		deinterlace->field_fmt = IPU_DEINTERLACE_FIELD_TOP;
		break;
	case V4L2_FIELD_INTERLACED_BT:
		v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
				"Enable deinterlace BT.\n");
		deinterlace->enable = true;
		deinterlace->field_fmt = IPU_DEINTERLACE_FIELD_BOTTOM;
		break;
	default:
		v4l2_err(vout->vfd->v4l2_dev,
			"field format:%d not supported yet!\n", field);
		return -EINVAL;
	}

	if (IPU_PIX_FMT_TILED_NV12F == vout->task.input.format) {
		v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
				"tiled fmt enable deinterlace.\n");
		deinterlace->enable = true;
	}

	if (deinterlace->enable && vdi_rate_double)
		deinterlace->field_fmt |= IPU_DEINTERLACE_RATE_EN;

	return 0;
}

static bool is_pp_bypass(struct mxc_vout_output *vout)
{
	if ((IPU_PIX_FMT_TILED_NV12 == vout->task.input.format) ||
		(IPU_PIX_FMT_TILED_NV12F == vout->task.input.format))
		return false;
	if ((vout->task.input.width == vout->task.output.width) &&
		(vout->task.input.height == vout->task.output.height) &&
		(vout->task.input.crop.w == vout->task.output.crop.w) &&
		(vout->task.input.crop.h == vout->task.output.crop.h) &&
		(vout->task.output.rotate < IPU_ROTATE_HORIZ_FLIP) &&
		!vout->task.input.deinterlace.enable) {
		if (vout->disp_support_csc)
			return true;
		else if (!need_csc(vout->task.input.format, vout->disp_fmt))
			return true;
	/*
	 * input crop show to full output which can show based on
	 * xres_virtual/yres_virtual
	 */
	} else if ((vout->task.input.crop.w == vout->task.output.crop.w) &&
			(vout->task.output.crop.w == vout->task.output.width) &&
			(vout->task.input.crop.h == vout->task.output.crop.h) &&
			(vout->task.output.crop.h ==
				vout->task.output.height) &&
			(vout->task.output.rotate < IPU_ROTATE_HORIZ_FLIP) &&
			!vout->task.input.deinterlace.enable) {
		if (vout->disp_support_csc)
			return true;
		else if (!need_csc(vout->task.input.format, vout->disp_fmt))
			return true;
	}
	return false;
}

static void setup_buf_timer(struct mxc_vout_output *vout,
			struct videobuf_buffer *vb)
{
	ktime_t expiry_time, now;

	/* if timestamp is 0, then default to 30fps */
	if ((vb->ts.tv_sec == 0) && (vb->ts.tv_usec == 0))
		expiry_time = ktime_add_ns(vout->start_ktime,
				NSEC_PER_FRAME_30FPS * vout->frame_count);
	else
		expiry_time = timeval_to_ktime(vb->ts);

	now = hrtimer_cb_get_time(&vout->timer);
	if ((now.tv64 > expiry_time.tv64)) {
		v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
				"warning: timer timeout already expired.\n");
		expiry_time = now;
	}

	hrtimer_start(&vout->timer, expiry_time, HRTIMER_MODE_ABS);

	v4l2_dbg(1, debug, vout->vfd->v4l2_dev, "timer handler next "
		"schedule: %lldnsecs\n", expiry_time.tv64);
}

static int show_buf(struct mxc_vout_output *vout, int idx,
	struct ipu_pos *ipos)
{
	struct fb_info *fbi = vout->fbi;
	struct fb_var_screeninfo var;
	int ret;
	u32 fb_base = 0;

	memcpy(&var, &fbi->var, sizeof(var));

	if (vout->linear_bypass_pp || vout->tiled_bypass_pp) {
		/*
		 * crack fb base
		 * NOTE: should not do other fb operation during v4l2
		 */
		console_lock();
		fb_base = fbi->fix.smem_start;
		fbi->fix.smem_start = vout->task.output.paddr;
		fbi->var.yoffset = ipos->y + 1;
		var.xoffset = ipos->x;
		var.yoffset = ipos->y;
		var.vmode |= FB_VMODE_YWRAP;
		ret = fb_pan_display(fbi, &var);
		fbi->fix.smem_start = fb_base;
		console_unlock();
	} else {
		console_lock();
		var.yoffset = idx * fbi->var.yres;
		var.vmode &= ~FB_VMODE_YWRAP;
		ret = fb_pan_display(fbi, &var);
		console_unlock();
	}

	return ret;
}

static void disp_work_func(struct work_struct *work)
{
	struct mxc_vout_output *vout =
		container_of(work, struct mxc_vout_output, disp_work);
	struct videobuf_queue *q = &vout->vbq;
	struct videobuf_buffer *vb, *vb_next = NULL;
	unsigned long flags = 0;
	struct ipu_pos ipos;
	int ret = 0;
	u32 in_fmt = 0, in_width = 0, in_height = 0;
	u32 vdi_cnt = 0;
	u32 vdi_frame;
	u32 index = 0;
	u32 ocrop_h = 0;
	u32 o_height = 0;
	u32 tiled_interlaced = 0;
	bool tiled_fmt = false;

	v4l2_dbg(1, debug, vout->vfd->v4l2_dev, "disp work begin one frame\n");

	spin_lock_irqsave(q->irqlock, flags);

	if (list_empty(&vout->active_list)) {
		v4l2_warn(vout->vfd->v4l2_dev,
			"no entry in active_list, should not be here\n");
		spin_unlock_irqrestore(q->irqlock, flags);
		return;
	}

	vb = list_first_entry(&vout->active_list,
			struct videobuf_buffer, queue);
	ret = set_field_fmt(vout, vb->field);
	if (ret < 0) {
		spin_unlock_irqrestore(q->irqlock, flags);
		return;
	}
	if (deinterlace_3_field(vout)) {
		if (list_is_singular(&vout->active_list)) {
			if (list_empty(&vout->queue_list)) {
				vout->timer_stop = true;
				spin_unlock_irqrestore(q->irqlock, flags);
				v4l2_warn(vout->vfd->v4l2_dev,
					"no enough entry for 3 fields "
					"deinterlacer\n");
				return;
			}

			/*
			 * We need to use the next vb even if it is
			 * not on the active list.
			 */
			vb_next = list_first_entry(&vout->queue_list,
					struct videobuf_buffer, queue);
		} else
			vb_next = list_first_entry(vout->active_list.next,
						struct videobuf_buffer, queue);
		v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
			"cur field_fmt:%d, next field_fmt:%d.\n",
			vb->field, vb_next->field);
		/* repeat the last field during field format changing */
		if ((vb->field != vb_next->field) &&
			(vb_next->field != V4L2_FIELD_NONE))
			vb_next = vb;
	}

	spin_unlock_irqrestore(q->irqlock, flags);

vdi_frame_rate_double:
	mutex_lock(&vout->task_lock);

	v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
		"v4l2 frame_cnt:%ld, vb_field:%d, fmt:%d\n",
		vout->frame_count, vb->field,
		vout->task.input.deinterlace.field_fmt);
	if (vb->memory == V4L2_MEMORY_USERPTR)
		vout->task.input.paddr = vb->baddr;
	else
		vout->task.input.paddr = videobuf_to_dma_contig(vb);

	if (vout->task.input.deinterlace.field_fmt & IPU_DEINTERLACE_RATE_EN)
		index = vout->vdi_frame_cnt % FB_BUFS;
	else
		index = vout->frame_count % FB_BUFS;
	if (vout->linear_bypass_pp) {
		vout->task.output.paddr = vout->task.input.paddr;
		ipos.x = vout->task.input.crop.pos.x;
		ipos.y = vout->task.input.crop.pos.y;
	} else {
		if (deinterlace_3_field(vout)) {
			if (vb->memory == V4L2_MEMORY_USERPTR)
				vout->task.input.paddr_n = vb_next->baddr;
			else
				vout->task.input.paddr_n =
					videobuf_to_dma_contig(vb_next);
		}
		vout->task.output.paddr = vout->disp_bufs[index];
		if (vout->vdoa_1080p) {
			o_height =  vout->task.output.height;
			ocrop_h = vout->task.output.crop.h;
			vout->task.output.height = FRAME_HEIGHT_1080P;
			vout->task.output.crop.h = FRAME_HEIGHT_1080P;
		}
		tiled_fmt =
			(IPU_PIX_FMT_TILED_NV12 == vout->task.input.format) ||
			(IPU_PIX_FMT_TILED_NV12F == vout->task.input.format);
		if (vout->tiled_bypass_pp) {
			ipos.x = vout->task.input.crop.pos.x;
			ipos.y = vout->task.input.crop.pos.y;
		} else if (tiled_fmt) {
			vout->vdoa_task.input.paddr = vout->task.input.paddr;
			if (deinterlace_3_field(vout))
				vout->vdoa_task.input.paddr_n =
						vout->task.input.paddr_n;
			vout->vdoa_task.output.paddr = vout->vdoa_work.paddr;
			ret = ipu_queue_task(&vout->vdoa_task);
			if (ret < 0) {
				mutex_unlock(&vout->task_lock);
				goto err;
			}
			vout->task.input.paddr = vout->vdoa_task.output.paddr;
			in_fmt = vout->task.input.format;
			in_width = vout->task.input.width;
			in_height = vout->task.input.height;
			vout->task.input.format = vout->vdoa_task.output.format;
			vout->task.input.width = vout->vdoa_task.output.width;
			vout->task.input.height = vout->vdoa_task.output.height;
			if (vout->task.input.deinterlace.enable) {
				tiled_interlaced = 1;
				vout->task.input.deinterlace.enable = 0;
			}
			v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
					"tiled queue task\n");
		}
		ret = ipu_queue_task(&vout->task);
		if ((!vout->tiled_bypass_pp) && tiled_fmt) {
			vout->task.input.format = in_fmt;
			vout->task.input.width = in_width;
			vout->task.input.height = in_height;
		}
		if (tiled_interlaced)
			vout->task.input.deinterlace.enable = 1;
		if (ret < 0) {
			mutex_unlock(&vout->task_lock);
			goto err;
		}
		if (vout->vdoa_1080p) {
			vout->task.output.crop.h = ocrop_h;
			vout->task.output.height = o_height;
		}
	}

	mutex_unlock(&vout->task_lock);

	ret = show_buf(vout, index, &ipos);
	if (ret < 0)
		v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
			"show buf with ret %d\n", ret);

	if (vout->task.input.deinterlace.field_fmt & IPU_DEINTERLACE_RATE_EN) {
		vdi_frame = vout->task.input.deinterlace.field_fmt
				& IPU_DEINTERLACE_RATE_FRAME1;
		if (vdi_frame)
			vout->task.input.deinterlace.field_fmt &=
			~IPU_DEINTERLACE_RATE_FRAME1;
		else
			vout->task.input.deinterlace.field_fmt |=
			IPU_DEINTERLACE_RATE_FRAME1;
		vout->vdi_frame_cnt++;
		vdi_cnt++;
		if (vdi_cnt < IPU_DEINTERLACE_MAX_FRAME)
			goto vdi_frame_rate_double;
	}
	spin_lock_irqsave(q->irqlock, flags);

	list_del(&vb->queue);

	/*
	 * The videobuf before the last one has been shown. Set
	 * VIDEOBUF_DONE state here to avoid tearing issue in ic bypass
	 * case, which makes sure a buffer being shown will not be
	 * dequeued to be overwritten. It also brings side-effect that
	 * the last 2 buffers can not be dequeued correctly, apps need
	 * to take care of it.
	 */
	if (vout->pre2_vb) {
		vout->pre2_vb->state = VIDEOBUF_DONE;
		wake_up_interruptible(&vout->pre2_vb->done);
		vout->pre2_vb = NULL;
	}

	if (vout->linear_bypass_pp) {
		vout->pre2_vb = vout->pre1_vb;
		vout->pre1_vb = vb;
	} else {
		if (vout->pre1_vb) {
			vout->pre1_vb->state = VIDEOBUF_DONE;
			wake_up_interruptible(&vout->pre1_vb->done);
			vout->pre1_vb = NULL;
		}
		vb->state = VIDEOBUF_DONE;
		wake_up_interruptible(&vb->done);
	}

	vout->frame_count++;

	/* pick next queue buf to setup timer */
	if (list_empty(&vout->queue_list))
		vout->timer_stop = true;
	else {
		vb = list_first_entry(&vout->queue_list,
				struct videobuf_buffer, queue);
		setup_buf_timer(vout, vb);
	}

	spin_unlock_irqrestore(q->irqlock, flags);

	v4l2_dbg(1, debug, vout->vfd->v4l2_dev, "disp work finish one frame\n");

	return;
err:
	v4l2_err(vout->vfd->v4l2_dev, "display work fail ret = %d\n", ret);
	vout->timer_stop = true;
	vb->state = VIDEOBUF_ERROR;
	return;
}

static enum hrtimer_restart mxc_vout_timer_handler(struct hrtimer *timer)
{
	struct mxc_vout_output *vout = container_of(timer,
						    struct mxc_vout_output,
						    timer);
	struct videobuf_queue *q = &vout->vbq;
	struct videobuf_buffer *vb;
	unsigned long flags = 0;

	spin_lock_irqsave(q->irqlock, flags);

	/*
	 * put first queued entry into active, if previous entry did not
	 * finish, setup current entry's timer again.
	 */
	if (list_empty(&vout->queue_list)) {
		spin_unlock_irqrestore(q->irqlock, flags);
		return HRTIMER_NORESTART;
	}

	/* move videobuf from queued list to active list */
	vb = list_first_entry(&vout->queue_list,
			struct videobuf_buffer, queue);
	list_del(&vb->queue);
	list_add_tail(&vb->queue, &vout->active_list);

	if (queue_work(vout->v4l_wq, &vout->disp_work) == 0) {
		v4l2_warn(vout->vfd->v4l2_dev,
		"disp work was in queue already, queue buf again next time\n");
		list_del(&vb->queue);
		list_add(&vb->queue, &vout->queue_list);
		spin_unlock_irqrestore(q->irqlock, flags);
		return HRTIMER_NORESTART;
	}

	vb->state = VIDEOBUF_ACTIVE;

	spin_unlock_irqrestore(q->irqlock, flags);

	return HRTIMER_NORESTART;
}

/* Video buffer call backs */

/*
 * Buffer setup function is called by videobuf layer when REQBUF ioctl is
 * called. This is used to setup buffers and return size and count of
 * buffers allocated. After the call to this buffer, videobuf layer will
 * setup buffer queue depending on the size and count of buffers
 */
static int mxc_vout_buffer_setup(struct videobuf_queue *q, unsigned int *count,
			  unsigned int *size)
{
	struct mxc_vout_output *vout = q->priv_data;
	unsigned int frame_size;

	if (!vout)
		return -EINVAL;

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != q->type)
		return -EINVAL;

	frame_size = get_frame_size(vout);
	*size = PAGE_ALIGN(frame_size);

	return 0;
}

/*
 * This function will be called when VIDIOC_QBUF ioctl is called.
 * It prepare buffers before give out for the display. This function
 * converts user space virtual address into physical address if userptr memory
 * exchange mechanism is used.
 */
static int mxc_vout_buffer_prepare(struct videobuf_queue *q,
			    struct videobuf_buffer *vb,
			    enum v4l2_field field)
{
	vb->state = VIDEOBUF_PREPARED;
	return 0;
}

/*
 * Buffer queue funtion will be called from the videobuf layer when _QBUF
 * ioctl is called. It is used to enqueue buffer, which is ready to be
 * displayed.
 * This function is protected by q->irqlock.
 */
static void mxc_vout_buffer_queue(struct videobuf_queue *q,
			  struct videobuf_buffer *vb)
{
	struct mxc_vout_output *vout = q->priv_data;
	struct videobuf_buffer *active_vb;

	list_add_tail(&vb->queue, &vout->queue_list);
	vb->state = VIDEOBUF_QUEUED;

	if (vout->timer_stop) {
		if (deinterlace_3_field(vout) &&
			!list_empty(&vout->active_list)) {
			active_vb = list_first_entry(&vout->active_list,
					struct videobuf_buffer, queue);
			setup_buf_timer(vout, active_vb);
		} else {
			setup_buf_timer(vout, vb);
		}
		vout->timer_stop = false;
	}
}

/*
 * Buffer release function is called from videobuf layer to release buffer
 * which are already allocated
 */
static void mxc_vout_buffer_release(struct videobuf_queue *q,
			    struct videobuf_buffer *vb)
{
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int mxc_vout_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	struct mxc_vout_output *vout = file->private_data;

	if (!vout)
		return -ENODEV;

	ret = videobuf_mmap_mapper(&vout->vbq, vma);
	if (ret < 0)
		v4l2_err(vout->vfd->v4l2_dev,
				"offset invalid [offset=0x%lx]\n",
				(vma->vm_pgoff << PAGE_SHIFT));

	return ret;
}

static int mxc_vout_release(struct file *file)
{
	unsigned int ret = 0;
	struct videobuf_queue *q;
	struct mxc_vout_output *vout = file->private_data;

	if (!vout)
		return 0;

	mutex_lock(&vout->accs_lock);
	if (--vout->open_cnt == 0) {
		q = &vout->vbq;
		if (q->streaming)
			mxc_vidioc_streamoff(file, vout, vout->type);
		else {
			release_disp_output(vout);
			videobuf_queue_cancel(q);
		}
		destroy_workqueue(vout->v4l_wq);
		ret = videobuf_mmap_free(q);
	}

	mutex_unlock(&vout->accs_lock);
	return ret;
}

static long mxc_vout_ioctl(struct file *file,
	       unsigned int cmd, unsigned long arg)
{
	struct mxc_vout_output *vout = file->private_data;
	struct v4l2_crop crop;
	int ret;

	switch (cmd) {
	case VIDIOC_S_INPUT_CROP:
		if (copy_from_user(&crop, (void __user *)arg, sizeof(struct v4l2_crop)))
			return -EFAULT;
		ret = mxc_vidioc_s_input_crop(vout, &crop);
		break;
	case VIDIOC_G_INPUT_CROP:
		mxc_vidioc_g_input_crop(vout, &crop);
		ret = copy_from_user((void __user *)arg, &crop, sizeof(struct v4l2_crop));
		break;
	default:
		ret = video_ioctl2(file, cmd, arg);
	}
	return ret;
}

static int mxc_vout_open(struct file *file)
{
	struct mxc_vout_output *vout = NULL;
	int ret = 0;

	vout = video_drvdata(file);

	if (vout == NULL)
		return -ENODEV;

	mutex_lock(&vout->accs_lock);
	if (vout->open_cnt++ == 0) {
		vout->ctrl_rotate = 0;
		vout->ctrl_vflip = 0;
		vout->ctrl_hflip = 0;
		ret = update_setting_from_fbi(vout, vout->fbi);
		if (ret < 0)
			goto err;

		vout->v4l_wq = create_singlethread_workqueue("v4l2q");
		if (!vout->v4l_wq) {
			v4l2_err(vout->vfd->v4l2_dev,
					"Could not create work queue\n");
			ret = -ENOMEM;
			goto err;
		}

		INIT_WORK(&vout->disp_work, disp_work_func);

		INIT_LIST_HEAD(&vout->queue_list);
		INIT_LIST_HEAD(&vout->active_list);

		vout->fmt_init = false;
		vout->frame_count = 0;
		vout->vdi_frame_cnt = 0;

		vout->win_pos.x = 0;
		vout->win_pos.y = 0;
		vout->release = true;
	}

	file->private_data = vout;

err:
	mutex_unlock(&vout->accs_lock);
	return ret;
}

/*
 * V4L2 ioctls
 */
static int mxc_vidioc_querycap(struct file *file, void *fh,
		struct v4l2_capability *cap)
{
	struct mxc_vout_output *vout = fh;

	strlcpy(cap->driver, VOUT_NAME, sizeof(cap->driver));
	strlcpy(cap->card, vout->vfd->name, sizeof(cap->card));
	cap->bus_info[0] = '\0';
	cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_OUTPUT;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int mxc_vidioc_enum_fmt_vid_out(struct file *file, void *fh,
			struct v4l2_fmtdesc *fmt)
{
	if (fmt->index >= NUM_MXC_VOUT_FORMATS)
		return -EINVAL;

	strlcpy(fmt->description, mxc_formats[fmt->index].description,
			sizeof(fmt->description));
	fmt->pixelformat = mxc_formats[fmt->index].pixelformat;

	return 0;
}

static int mxc_vidioc_g_fmt_vid_out(struct file *file, void *fh,
			struct v4l2_format *f)
{
	struct mxc_vout_output *vout = fh;

	f->fmt.pix.width = vout->task.input.width;
	f->fmt.pix.height = vout->task.input.height;
	f->fmt.pix.pixelformat = vout->task.input.format;
	f->fmt.pix.sizeimage = get_frame_size(vout);

	v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
			"frame_size:0x%x, pix_fmt:0x%x\n",
			f->fmt.pix.sizeimage,
			vout->task.input.format);

	return 0;
}

static inline int ipu_try_task(struct mxc_vout_output *vout)
{
	int ret;
	struct ipu_task *task = &vout->task;

again:
	ret = ipu_check_task(task);
	if (ret != IPU_CHECK_OK) {
		if (ret > IPU_CHECK_ERR_MIN) {
			if (ret == IPU_CHECK_ERR_SPLIT_INPUTW_OVER ||
			    ret == IPU_CHECK_ERR_W_DOWNSIZE_OVER) {
				task->input.crop.w -= 8;
				goto again;
			}
			if (ret == IPU_CHECK_ERR_SPLIT_INPUTH_OVER ||
			    ret == IPU_CHECK_ERR_H_DOWNSIZE_OVER) {
				task->input.crop.h -= 8;
				goto again;
			}
			if (ret == IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER) {
				if (vout->disp_support_windows) {
					task->output.width -= 8;
					task->output.crop.w =
						task->output.width;
				} else
					task->output.crop.w -= 8;
				goto again;
			}
			if (ret == IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER) {
				if (vout->disp_support_windows) {
					task->output.height -= 8;
					task->output.crop.h =
						task->output.height;
				} else
					task->output.crop.h -= 8;
				goto again;
			}
			ret = -EINVAL;
		}
	} else
		ret = 0;

	return ret;
}

static inline int vdoaipu_try_task(struct mxc_vout_output *vout)
{
	int ret;
	int is_1080p_stream;
	int in_width, in_height;
	size_t size;
	struct ipu_task *ipu_task = &vout->task;
	struct ipu_crop *icrop = &ipu_task->input.crop;
	struct ipu_task *vdoa_task = &vout->vdoa_task;
	u32 deinterlace = 0;
	u32 in_fmt;

	if (vout->task.input.deinterlace.enable)
		deinterlace = 1;

	memset(vdoa_task, 0, sizeof(*vdoa_task));
	vdoa_task->output.format = IPU_PIX_FMT_NV12;
	memcpy(&vdoa_task->input, &ipu_task->input,
			sizeof(ipu_task->input));
	if ((icrop->w % IPU_PIX_FMT_TILED_NV12_MBALIGN) ||
		(icrop->h % IPU_PIX_FMT_TILED_NV12_MBALIGN)) {
		vdoa_task->input.crop.w =
			ALIGN(icrop->w, IPU_PIX_FMT_TILED_NV12_MBALIGN);
		vdoa_task->input.crop.h =
			ALIGN(icrop->h, IPU_PIX_FMT_TILED_NV12_MBALIGN);
	}
	vdoa_task->output.width = vdoa_task->input.crop.w;
	vdoa_task->output.height = vdoa_task->input.crop.h;
	vdoa_task->output.crop.w = vdoa_task->input.crop.w;
	vdoa_task->output.crop.h = vdoa_task->input.crop.h;

	size = PAGE_ALIGN(vdoa_task->input.crop.w *
					vdoa_task->input.crop.h *
					fmt_to_bpp(vdoa_task->output.format)/8);
	if (size > vout->vdoa_work.size) {
		if (vout->vdoa_work.vaddr)
			free_dma_buf(vout, &vout->vdoa_work);
		vout->vdoa_work.size = size;
		ret = alloc_dma_buf(vout, &vout->vdoa_work);
		if (ret < 0)
			return ret;
	}
	ret = ipu_check_task(vdoa_task);
	if (ret != IPU_CHECK_OK)
		return -EINVAL;

	is_1080p_stream = CHECK_TILED_1080P_STREAM(vout);
	if (is_1080p_stream)
		ipu_task->input.crop.h = VALID_HEIGHT_1080P;
	in_fmt = ipu_task->input.format;
	in_width = ipu_task->input.width;
	in_height = ipu_task->input.height;
	ipu_task->input.format = vdoa_task->output.format;
	ipu_task->input.height = vdoa_task->output.height;
	ipu_task->input.width = vdoa_task->output.width;
	if (deinterlace)
		ipu_task->input.deinterlace.enable = 0;
	ret = ipu_try_task(vout);
	if (deinterlace)
		ipu_task->input.deinterlace.enable = 1;
	ipu_task->input.format = in_fmt;
	ipu_task->input.width = in_width;
	ipu_task->input.height = in_height;

	return ret;
}

static int mxc_vout_try_task(struct mxc_vout_output *vout)
{
	int ret = 0;
	struct ipu_output *output = &vout->task.output;
	struct ipu_input *input = &vout->task.input;
	struct ipu_crop *crop = &input->crop;
	u32 o_height = 0;
	u32 ocrop_h = 0;
	bool tiled_fmt = false;
	bool tiled_need_pp = false;

	vout->vdoa_1080p = CHECK_TILED_1080P_DISPLAY(vout);
	if (vout->vdoa_1080p) {
		input->crop.h = FRAME_HEIGHT_1080P;
		o_height = output->height;
		ocrop_h = output->crop.h;
		output->height = FRAME_HEIGHT_1080P;
		output->crop.h = FRAME_HEIGHT_1080P;
	}

	if ((IPU_PIX_FMT_TILED_NV12 == input->format) ||
		(IPU_PIX_FMT_TILED_NV12F == input->format)) {
		if ((input->width % IPU_PIX_FMT_TILED_NV12_MBALIGN) ||
			(input->height % IPU_PIX_FMT_TILED_NV12_MBALIGN) ||
			(crop->pos.x % IPU_PIX_FMT_TILED_NV12_MBALIGN) ||
			(crop->pos.y % IPU_PIX_FMT_TILED_NV12_MBALIGN)) {
			v4l2_err(vout->vfd->v4l2_dev,
				"ERR: tiled fmt needs 16 pixel align.\n");
			return -EINVAL;
		}
		if ((crop->w % IPU_PIX_FMT_TILED_NV12_MBALIGN) ||
			(crop->h % IPU_PIX_FMT_TILED_NV12_MBALIGN))
			tiled_need_pp = true;
	} else {
		crop->w -= crop->w % 8;
		crop->h -= crop->h % 8;
	}
	/* assume task.output already set by S_CROP */
	vout->linear_bypass_pp = is_pp_bypass(vout);
	if (vout->linear_bypass_pp) {
		v4l2_info(vout->vfd->v4l2_dev, "Bypass IC.\n");
		output->format = input->format;
	} else {
		/* if need CSC, choose IPU-DP or IPU_IC do it */
		if (vout->disp_support_csc) {
			if (colorspaceofpixel(input->format) == YUV_CS)
				output->format = IPU_PIX_FMT_UYVY;
			else
				output->format = IPU_PIX_FMT_RGB565;
		} else {
			if (colorspaceofpixel(vout->disp_fmt) == YUV_CS)
				output->format = IPU_PIX_FMT_UYVY;
			else
				output->format = IPU_PIX_FMT_RGB565;
		}

		vout->tiled_bypass_pp = false;
		if ((IPU_PIX_FMT_TILED_NV12 == input->format) ||
			(IPU_PIX_FMT_TILED_NV12F == input->format)) {
			/* check resize/rotate/flip, or csc task */
			if (!(tiled_need_pp ||
				(IPU_ROTATE_NONE != output->rotate) ||
				(input->crop.w != output->crop.w) ||
				(input->crop.h != output->crop.h) ||
				(!vout->disp_support_csc &&
				(colorspaceofpixel(vout->disp_fmt) == RGB_CS)))
				) {
				/* IC bypass */
				output->format = IPU_PIX_FMT_NV12;
				v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
						"tiled bypass pp\n");
				vout->tiled_bypass_pp = true;
			}
			tiled_fmt = true;
		}

		if ((!vout->tiled_bypass_pp) && tiled_fmt)
			ret = vdoaipu_try_task(vout);
		else
			ret = ipu_try_task(vout);
	}

	if (vout->vdoa_1080p) {
		output->height = o_height;
		output->crop.h = ocrop_h;
	}

	v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
			"icrop.w:%u, icrop.h:%u, iw:%u, ih:%u,"
			"ocrop.w:%u, ocrop.h:%u, ow:%u, oh:%u\n",
			input->crop.w, input->crop.h,
			input->width, input->height,
			output->crop.w, output->crop.h,
			output->width, output->height);
	return ret;
}

static int mxc_vout_try_format(struct mxc_vout_output *vout,
				struct v4l2_format *f)
{
	int ret = 0;

	if ((f->fmt.pix.field != V4L2_FIELD_NONE) &&
		(IPU_PIX_FMT_TILED_NV12 == vout->task.input.format)) {
		v4l2_err(vout->vfd->v4l2_dev,
			"progressive tiled fmt should used V4L2_FIELD_NONE!\n");
		return -EINVAL;
	}

	if (vout->input_crop == false) {
		vout->task.input.crop.pos.x = 0;
		vout->task.input.crop.pos.y = 0;
		vout->task.input.crop.w = f->fmt.pix.width;
		vout->task.input.crop.h = f->fmt.pix.height;
	}

	vout->task.input.width = f->fmt.pix.width;
	vout->task.input.height = f->fmt.pix.height;
	vout->task.input.format = f->fmt.pix.pixelformat;

	ret = set_field_fmt(vout, f->fmt.pix.field);
	if (ret < 0)
		return ret;

	ret = mxc_vout_try_task(vout);
	if (!ret) {
		f->fmt.pix.width = vout->task.input.crop.w;
		f->fmt.pix.height = vout->task.input.crop.h;
	}

	return ret;
}

static bool mxc_vout_need_fb_reconfig(struct mxc_vout_output *vout,
				      struct mxc_vout_output *pre_vout)
{
	if (!vout->vbq.streaming)
		return false;

	if (vout->tiled_bypass_pp)
		return true;

	if (vout->linear_bypass_pp != pre_vout->linear_bypass_pp)
		return true;

	/* cropped output resolution or format are changed */
	if (vout->task.output.format != pre_vout->task.output.format ||
	    vout->task.output.crop.w != pre_vout->task.output.crop.w ||
	    vout->task.output.crop.h != pre_vout->task.output.crop.h)
		return true;

	/* overlay: window position or resolution are changed */
	if (vout->disp_support_windows &&
	    (vout->win_pos.x != pre_vout->win_pos.x ||
	     vout->win_pos.y != pre_vout->win_pos.y ||
	     vout->task.output.width  != pre_vout->task.output.width ||
	     vout->task.output.height != pre_vout->task.output.height))
		return true;

	/* background: cropped position is changed */
	if (!vout->disp_support_windows &&
	    (vout->task.output.crop.pos.x !=
	     pre_vout->task.output.crop.pos.x ||
	     vout->task.output.crop.pos.y !=
	     pre_vout->task.output.crop.pos.y))
		return true;

	return false;
}

static int mxc_vidioc_s_fmt_vid_out(struct file *file, void *fh,
			struct v4l2_format *f)
{
	struct mxc_vout_output *vout = fh;
	int ret = 0;

	if (vout->vbq.streaming)
		return -EBUSY;

	mutex_lock(&vout->task_lock);
	ret = mxc_vout_try_format(vout, f);
	if (ret >= 0)
		vout->fmt_init = true;
	mutex_unlock(&vout->task_lock);

	return ret;
}

static int mxc_vidioc_cropcap(struct file *file, void *fh,
		struct v4l2_cropcap *cropcap)
{
	struct mxc_vout_output *vout = fh;

	if (cropcap->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	cropcap->bounds = vout->crop_bounds;
	cropcap->defrect = vout->crop_bounds;

	return 0;
}

static int mxc_vidioc_g_crop(struct file *file, void *fh,
				struct v4l2_crop *crop)
{
	struct mxc_vout_output *vout = fh;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	if (vout->disp_support_windows) {
		crop->c.left = vout->win_pos.x;
		crop->c.top = vout->win_pos.y;
		crop->c.width = vout->task.output.width;
		crop->c.height = vout->task.output.height;
	} else {
		if (vout->task.output.crop.w && vout->task.output.crop.h) {
			crop->c.left = vout->task.output.crop.pos.x;
			crop->c.top = vout->task.output.crop.pos.y;
			crop->c.width = vout->task.output.crop.w;
			crop->c.height = vout->task.output.crop.h;
		} else {
			crop->c.left = 0;
			crop->c.top = 0;
			crop->c.width = vout->task.output.width;
			crop->c.height = vout->task.output.height;
		}
	}

	return 0;
}

static int mxc_vidioc_s_crop(struct file *file, void *fh,
				const struct v4l2_crop *crop)
{
	struct mxc_vout_output *vout = fh, *pre_vout;
	struct v4l2_rect *b = &vout->crop_bounds;
	struct v4l2_crop fix_up_crop;
	int ret = 0;

	memcpy(&fix_up_crop, crop, sizeof(*crop));

	if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	if (crop->c.width < 0 || crop->c.height < 0)
		return -EINVAL;

	if (crop->c.width == 0)
		fix_up_crop.c.width = b->width - b->left;
	if (crop->c.height == 0)
		fix_up_crop.c.height = b->height - b->top;

	if (crop->c.top < b->top)
		fix_up_crop.c.top = b->top;
	if (crop->c.top >= b->top + b->height)
		fix_up_crop.c.top = b->top + b->height - 1;
	if (crop->c.height > b->top - crop->c.top + b->height)
		fix_up_crop.c.height =
			b->top - fix_up_crop.c.top + b->height;

	if (crop->c.left < b->left)
		fix_up_crop.c.left = b->left;
	if (crop->c.left >= b->left + b->width)
		fix_up_crop.c.left = b->left + b->width - 1;
	if (crop->c.width > b->left - crop->c.left + b->width)
		fix_up_crop.c.width =
			b->left - fix_up_crop.c.left + b->width;

	/* stride line limitation */
	fix_up_crop.c.height -= fix_up_crop.c.height % 8;
	fix_up_crop.c.width -= fix_up_crop.c.width % 8;
	if ((fix_up_crop.c.width <= 0) || (fix_up_crop.c.height <= 0) ||
		((fix_up_crop.c.left + fix_up_crop.c.width) >
		 (b->left + b->width)) ||
		((fix_up_crop.c.top + fix_up_crop.c.height) >
		 (b->top + b->height))) {
		v4l2_err(vout->vfd->v4l2_dev, "s_crop err: %d, %d, %d, %d",
			fix_up_crop.c.left, fix_up_crop.c.top,
			fix_up_crop.c.width, fix_up_crop.c.height);
		return -EINVAL;
	}

	/* the same setting, return */
	if (vout->disp_support_windows) {
		if ((vout->win_pos.x == fix_up_crop.c.left) &&
			(vout->win_pos.y == fix_up_crop.c.top) &&
			(vout->task.output.crop.w == fix_up_crop.c.width) &&
			(vout->task.output.crop.h == fix_up_crop.c.height))
			return 0;
	} else {
		if ((vout->task.output.crop.pos.x == fix_up_crop.c.left) &&
			(vout->task.output.crop.pos.y == fix_up_crop.c.top) &&
			(vout->task.output.crop.w == fix_up_crop.c.width) &&
			(vout->task.output.crop.h == fix_up_crop.c.height))
			return 0;
	}

	pre_vout = vmalloc(sizeof(*pre_vout));
	if (!pre_vout)
		return -ENOMEM;

	/* wait current work finish */
	if (vout->vbq.streaming)
		flush_workqueue(vout->v4l_wq);

	mutex_lock(&vout->task_lock);

	memcpy(pre_vout, vout, sizeof(*vout));

	if (vout->disp_support_windows) {
		vout->task.output.crop.pos.x = 0;
		vout->task.output.crop.pos.y = 0;
		vout->win_pos.x = fix_up_crop.c.left;
		vout->win_pos.y = fix_up_crop.c.top;
		vout->task.output.width = fix_up_crop.c.width;
		vout->task.output.height = fix_up_crop.c.height;
	} else {
		vout->task.output.crop.pos.x = fix_up_crop.c.left;
		vout->task.output.crop.pos.y = fix_up_crop.c.top;
	}

	vout->task.output.crop.w = fix_up_crop.c.width;
	vout->task.output.crop.h = fix_up_crop.c.height;

	/*
	 * must S_CROP before S_FMT, for fist time S_CROP, will not check
	 * ipu task, it will check in S_FMT, after S_FMT, S_CROP should
	 * check ipu task too.
	 */
	if (vout->fmt_init) {
		memcpy(&vout->task.input.crop, &vout->in_rect,
			sizeof(vout->in_rect));
		ret = mxc_vout_try_task(vout);
		if (ret < 0) {
			v4l2_err(vout->vfd->v4l2_dev,
					"vout check task failed\n");
			memcpy(vout, pre_vout, sizeof(*vout));
			goto done;
		}

		if (mxc_vout_need_fb_reconfig(vout, pre_vout)) {
			ret = config_disp_output(vout);
			if (ret < 0)
				v4l2_err(vout->vfd->v4l2_dev,
					"Config display output failed\n");
		}
	}

done:
	vfree(pre_vout);
	mutex_unlock(&vout->task_lock);

	return ret;
}

static int mxc_vidioc_queryctrl(struct file *file, void *fh,
		struct v4l2_queryctrl *ctrl)
{
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_ROTATE:
		ret = v4l2_ctrl_query_fill(ctrl, 0, 270, 90, 0);
		break;
	case V4L2_CID_VFLIP:
		ret = v4l2_ctrl_query_fill(ctrl, 0, 1, 1, 0);
		break;
	case V4L2_CID_HFLIP:
		ret = v4l2_ctrl_query_fill(ctrl, 0, 1, 1, 0);
		break;
	case V4L2_CID_MXC_MOTION:
		ret = v4l2_ctrl_query_fill(ctrl, 0, 2, 1, 0);
		break;
	default:
		ctrl->name[0] = '\0';
		ret = -EINVAL;
	}
	return ret;
}

static int mxc_vidioc_g_ctrl(struct file *file, void *fh,
				struct v4l2_control *ctrl)
{
	int ret = 0;
	struct mxc_vout_output *vout = fh;

	switch (ctrl->id) {
	case V4L2_CID_ROTATE:
		ctrl->value = vout->ctrl_rotate;
		break;
	case V4L2_CID_VFLIP:
		ctrl->value = vout->ctrl_vflip;
		break;
	case V4L2_CID_HFLIP:
		ctrl->value = vout->ctrl_hflip;
		break;
	case V4L2_CID_MXC_MOTION:
		if (vout->task.input.deinterlace.enable)
			ctrl->value = vout->task.input.deinterlace.motion;
		else
			ctrl->value = 0;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static void setup_task_rotation(struct mxc_vout_output *vout)
{
	if (vout->ctrl_rotate == 0) {
		if (vout->ctrl_vflip && vout->ctrl_hflip)
			vout->task.output.rotate = IPU_ROTATE_180;
		else if (vout->ctrl_vflip)
			vout->task.output.rotate = IPU_ROTATE_VERT_FLIP;
		else if (vout->ctrl_hflip)
			vout->task.output.rotate = IPU_ROTATE_HORIZ_FLIP;
		else
			vout->task.output.rotate = IPU_ROTATE_NONE;
	} else if (vout->ctrl_rotate == 90) {
		if (vout->ctrl_vflip && vout->ctrl_hflip)
			vout->task.output.rotate = IPU_ROTATE_90_LEFT;
		else if (vout->ctrl_vflip)
			vout->task.output.rotate = IPU_ROTATE_90_RIGHT_VFLIP;
		else if (vout->ctrl_hflip)
			vout->task.output.rotate = IPU_ROTATE_90_RIGHT_HFLIP;
		else
			vout->task.output.rotate = IPU_ROTATE_90_RIGHT;
	} else if (vout->ctrl_rotate == 180) {
		if (vout->ctrl_vflip && vout->ctrl_hflip)
			vout->task.output.rotate = IPU_ROTATE_NONE;
		else if (vout->ctrl_vflip)
			vout->task.output.rotate = IPU_ROTATE_HORIZ_FLIP;
		else if (vout->ctrl_hflip)
			vout->task.output.rotate = IPU_ROTATE_VERT_FLIP;
		else
			vout->task.output.rotate = IPU_ROTATE_180;
	} else if (vout->ctrl_rotate == 270) {
		if (vout->ctrl_vflip && vout->ctrl_hflip)
			vout->task.output.rotate = IPU_ROTATE_90_RIGHT;
		else if (vout->ctrl_vflip)
			vout->task.output.rotate = IPU_ROTATE_90_RIGHT_HFLIP;
		else if (vout->ctrl_hflip)
			vout->task.output.rotate = IPU_ROTATE_90_RIGHT_VFLIP;
		else
			vout->task.output.rotate = IPU_ROTATE_90_LEFT;
	}
}

static int mxc_vidioc_s_ctrl(struct file *file, void *fh,
				struct v4l2_control *ctrl)
{
	int ret = 0;
	struct mxc_vout_output *vout = fh, *pre_vout;

	pre_vout = vmalloc(sizeof(*pre_vout));
	if (!pre_vout)
		return -ENOMEM;

	/* wait current work finish */
	if (vout->vbq.streaming)
		flush_workqueue(vout->v4l_wq);

	mutex_lock(&vout->task_lock);

	memcpy(pre_vout, vout, sizeof(*vout));

	switch (ctrl->id) {
	case V4L2_CID_ROTATE:
	{
		vout->ctrl_rotate = (ctrl->value/90) * 90;
		if (vout->ctrl_rotate > 270)
			vout->ctrl_rotate = 270;
		setup_task_rotation(vout);
		break;
	}
	case V4L2_CID_VFLIP:
	{
		vout->ctrl_vflip = ctrl->value;
		setup_task_rotation(vout);
		break;
	}
	case V4L2_CID_HFLIP:
	{
		vout->ctrl_hflip = ctrl->value;
		setup_task_rotation(vout);
		break;
	}
	case V4L2_CID_MXC_MOTION:
	{
		vout->task.input.deinterlace.motion = ctrl->value;
		break;
	}
	default:
		ret = -EINVAL;
		goto done;
	}

	if (vout->fmt_init) {
		memcpy(&vout->task.input.crop, &vout->in_rect,
				sizeof(vout->in_rect));
		ret = mxc_vout_try_task(vout);
		if (ret < 0) {
			v4l2_err(vout->vfd->v4l2_dev,
					"vout check task failed\n");
			memcpy(vout, pre_vout, sizeof(*vout));
			goto done;
		}

		if (mxc_vout_need_fb_reconfig(vout, pre_vout)) {
			ret = config_disp_output(vout);
			if (ret < 0)
				v4l2_err(vout->vfd->v4l2_dev,
					"Config display output failed\n");
		}
	}

done:
	vfree(pre_vout);
	mutex_unlock(&vout->task_lock);

	return ret;
}

static int mxc_vidioc_reqbufs(struct file *file, void *fh,
			struct v4l2_requestbuffers *req)
{
	int ret = 0;
	struct mxc_vout_output *vout = fh;
	struct videobuf_queue *q = &vout->vbq;

	if (req->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	/* should not be here after streaming, videobuf_reqbufs will control */
	mutex_lock(&vout->task_lock);

	ret = videobuf_reqbufs(q, req);

	mutex_unlock(&vout->task_lock);
	return ret;
}

static int mxc_vidioc_querybuf(struct file *file, void *fh,
			struct v4l2_buffer *b)
{
	int ret;
	struct mxc_vout_output *vout = fh;

	ret = videobuf_querybuf(&vout->vbq, b);
	if (!ret) {
		/* return physical address */
		struct videobuf_buffer *vb = vout->vbq.bufs[b->index];
		if (b->flags & V4L2_BUF_FLAG_MAPPED)
			b->m.offset = videobuf_to_dma_contig(vb);
	}

	return ret;
}

static int mxc_vidioc_qbuf(struct file *file, void *fh,
			struct v4l2_buffer *buffer)
{
	struct mxc_vout_output *vout = fh;

	return videobuf_qbuf(&vout->vbq, buffer);
}

static int mxc_vidioc_dqbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct mxc_vout_output *vout = fh;

	if (!vout->vbq.streaming)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK)
		return videobuf_dqbuf(&vout->vbq, (struct v4l2_buffer *)b, 1);
	else
		return videobuf_dqbuf(&vout->vbq, (struct v4l2_buffer *)b, 0);
}

static int mxc_vidioc_s_input_crop(struct mxc_vout_output *vout,
				const struct v4l2_crop *crop)
{
	int ret = 0;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	if (crop->c.width < 0 || crop->c.height < 0)
		return -EINVAL;

	vout->task.input.crop.pos.x = crop->c.left;
	vout->task.input.crop.pos.y = crop->c.top;
	vout->task.input.crop.w = crop->c.width;
	vout->task.input.crop.h = crop->c.height;

	vout->input_crop = true;
	memcpy(&vout->in_rect, &vout->task.input.crop, sizeof(vout->in_rect));

	return ret;
}

static int mxc_vidioc_g_input_crop(struct mxc_vout_output *vout,
				struct v4l2_crop *crop)
{
	int ret = 0;

	crop->c.left = vout->task.input.crop.pos.x;
	crop->c.top = vout->task.input.crop.pos.y;
	crop->c.width = vout->task.input.crop.w;
	crop->c.height = vout->task.input.crop.h;

	return ret;
}

static int set_window_position(struct mxc_vout_output *vout,
				struct mxcfb_pos *pos)
{
	struct fb_info *fbi = vout->fbi;
	mm_segment_t old_fs;
	int ret = 0;

	if (vout->disp_support_windows) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = fbi->fbops->fb_ioctl(fbi, MXCFB_SET_OVERLAY_POS,
				(unsigned long)pos);
		set_fs(old_fs);
	}

	return ret;
}

static int config_disp_output(struct mxc_vout_output *vout)
{
	struct dma_mem *buf = NULL;
	struct fb_info *fbi = vout->fbi;
	struct fb_var_screeninfo var;
	struct mxcfb_pos pos;
	int i, fb_num, ret;
	u32 fb_base;
	u32 size;
	u32 display_buf_size;
	u32 *pixel = NULL;
	u32 color;
	int j;

	memcpy(&var, &fbi->var, sizeof(var));
	fb_base = fbi->fix.smem_start;

	var.xres = vout->task.output.width;
	var.yres = vout->task.output.height;
	if (vout->linear_bypass_pp || vout->tiled_bypass_pp) {
		fb_num = 1;
		/* input crop */
		if (vout->task.input.width > vout->task.output.width)
			var.xres_virtual = vout->task.input.width;
		else
			var.xres_virtual = var.xres;
		if (vout->task.input.height > vout->task.output.height)
			var.yres_virtual = vout->task.input.height;
		else
			var.yres_virtual = var.yres;
		var.rotate = vout->task.output.rotate;
		var.vmode |= FB_VMODE_YWRAP;
	} else {
		fb_num = FB_BUFS;
		var.xres_virtual = var.xres;
		var.yres_virtual = fb_num * var.yres;
		var.vmode &= ~FB_VMODE_YWRAP;
	}
	var.bits_per_pixel = fmt_to_bpp(vout->task.output.format);
	var.nonstd = vout->task.output.format;

	v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
			"set display fb to %d %d\n",
			var.xres, var.yres);

	/*
	 * To setup the overlay fb from scratch without
	 * the last time overlay fb position or resolution's
	 * impact, we take the following steps:
	 * - blank fb
	 * - set fb position to the starting point
	 * - reconfigure fb
	 * - set fb position to a specific point
	 * - unblank fb
	 * This procedure applies to non-overlay fbs as well.
	 */
	console_lock();
	fbi->flags |= FBINFO_MISC_USEREVENT;
	fb_blank(fbi, FB_BLANK_POWERDOWN);
	fbi->flags &= ~FBINFO_MISC_USEREVENT;
	console_unlock();

	pos.x = 0;
	pos.y = 0;
	ret = set_window_position(vout, &pos);
	if (ret < 0) {
		v4l2_err(vout->vfd->v4l2_dev, "failed to set fb position "
			"to starting point\n");
		return ret;
	}

	/* Init display channel through fb API */
	var.yoffset = 0;
	var.activate |= FB_ACTIVATE_FORCE;
	console_lock();
	fbi->flags |= FBINFO_MISC_USEREVENT;
	ret = fb_set_var(fbi, &var);
	fbi->flags &= ~FBINFO_MISC_USEREVENT;
	console_unlock();
	if (ret < 0) {
		v4l2_err(vout->vfd->v4l2_dev,
				"ERR:%s fb_set_var ret:%d\n", __func__, ret);
		return ret;
	}

	ret = set_window_position(vout, &vout->win_pos);
	if (ret < 0) {
		v4l2_err(vout->vfd->v4l2_dev, "failed to set fb position\n");
		return ret;
	}

	if (vout->linear_bypass_pp || vout->tiled_bypass_pp)
		display_buf_size = fbi->fix.line_length * fbi->var.yres_virtual;
	else
		display_buf_size = fbi->fix.line_length * fbi->var.yres;
	for (i = 0; i < fb_num; i++)
		vout->disp_bufs[i] = fbi->fix.smem_start + i * display_buf_size;
	if (vout->tiled_bypass_pp) {
		size = PAGE_ALIGN(vout->task.input.crop.w *
					vout->task.input.crop.h *
					fmt_to_bpp(vout->task.output.format)/8);
		if (size > vout->vdoa_output[0].size) {
			for (i = 0; i < VDOA_FB_BUFS; i++) {
				buf = &vout->vdoa_output[i];
				if (buf->vaddr)
					free_dma_buf(vout, buf);
				buf->size = size;
				ret = alloc_dma_buf(vout, buf);
				if (ret < 0)
					goto err;
			}
		}
		for (i = fb_num; i < (fb_num + VDOA_FB_BUFS); i++)
			vout->disp_bufs[i] =
				vout->vdoa_output[i - fb_num].paddr;
	}
	vout->fb_smem_len = fbi->fix.smem_len;
	vout->fb_smem_start = fbi->fix.smem_start;
	if (fb_base != fbi->fix.smem_start) {
		v4l2_dbg(1, debug, vout->vfd->v4l2_dev,
			"realloc fb mem size:0x%x@0x%lx,old paddr @0x%x\n",
			fbi->fix.smem_len, fbi->fix.smem_start, fb_base);
	}

	/* fill black when video config changed */
	color = colorspaceofpixel(vout->task.output.format) == YUV_CS ?
			UYVY_BLACK : RGB_BLACK;
	if (IS_PLANAR_PIXEL_FORMAT(vout->task.output.format)) {
		size = display_buf_size * 8 /
			fmt_to_bpp(vout->task.output.format);
		memset(fbi->screen_base, Y_BLACK, size);
		memset(fbi->screen_base + size, UV_BLACK,
				display_buf_size - size);
	} else {
		pixel = (u32 *)fbi->screen_base;
		for (i = 0; i < ((display_buf_size * fb_num) >> 2); i++)
			*pixel++ = color;
	}
	console_lock();
	fbi->flags |= FBINFO_MISC_USEREVENT;
	ret = fb_blank(fbi, FB_BLANK_UNBLANK);
	fbi->flags &= ~FBINFO_MISC_USEREVENT;
	console_unlock();
	vout->release = false;

	return ret;
err:
	for (j = i - 1; j >= 0; j--) {
		buf = &vout->vdoa_output[j];
		if (buf->vaddr)
			free_dma_buf(vout, buf);
	}
	return ret;
}

static inline void wait_for_vsync(struct mxc_vout_output *vout)
{
	struct fb_info *fbi = vout->fbi;
	mm_segment_t old_fs;

	if (fbi->fbops->fb_ioctl) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		fbi->fbops->fb_ioctl(fbi, MXCFB_WAIT_FOR_VSYNC,
				(unsigned long)NULL);
		set_fs(old_fs);
	}

	return;
}

static void release_disp_output(struct mxc_vout_output *vout)
{
	struct fb_info *fbi = vout->fbi;
	struct mxcfb_pos pos;

	if (vout->release)
		return;
	console_lock();
	fbi->flags |= FBINFO_MISC_USEREVENT;
	fb_blank(fbi, FB_BLANK_POWERDOWN);
	fbi->flags &= ~FBINFO_MISC_USEREVENT;
	console_unlock();

	/* restore pos to 0,0 avoid fb pan display hang? */
	pos.x = 0;
	pos.y = 0;
	set_window_position(vout, &pos);

	if (get_ipu_channel(fbi) == MEM_BG_SYNC) {
		console_lock();
		fbi->fix.smem_start = vout->disp_bufs[0];
		fbi->flags |= FBINFO_MISC_USEREVENT;
		fb_blank(fbi, FB_BLANK_UNBLANK);
		fbi->flags &= ~FBINFO_MISC_USEREVENT;
		console_unlock();

	}

	vout->release = true;
}

static int mxc_vidioc_streamon(struct file *file, void *fh,
				enum v4l2_buf_type i)
{
	struct mxc_vout_output *vout = fh;
	struct videobuf_queue *q = &vout->vbq;
	int ret;

	if (q->streaming) {
		v4l2_err(vout->vfd->v4l2_dev,
				"video output already run\n");
		ret = -EBUSY;
		goto done;
	}

	if (deinterlace_3_field(vout) && list_is_singular(&q->stream)) {
		v4l2_err(vout->vfd->v4l2_dev,
			"deinterlacing: need queue 2 frame before streamon\n");
		ret = -EINVAL;
		goto done;
	}

	ret = config_disp_output(vout);
	if (ret < 0) {
		v4l2_err(vout->vfd->v4l2_dev,
				"Config display output failed\n");
		goto done;
	}

	hrtimer_init(&vout->timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	vout->timer.function = mxc_vout_timer_handler;
	vout->timer_stop = true;

	vout->start_ktime = hrtimer_cb_get_time(&vout->timer);

	vout->pre1_vb = NULL;
	vout->pre2_vb = NULL;

	ret = videobuf_streamon(q);
done:
	return ret;
}

static int mxc_vidioc_streamoff(struct file *file, void *fh,
				enum v4l2_buf_type i)
{
	struct mxc_vout_output *vout = fh;
	struct videobuf_queue *q = &vout->vbq;
	int ret = 0;

	if (q->streaming) {
		flush_workqueue(vout->v4l_wq);

		hrtimer_cancel(&vout->timer);

		/*
		 * Wait for 2 vsyncs to make sure
		 * frames are drained on triple
		 * buffer.
		 */
		wait_for_vsync(vout);
		wait_for_vsync(vout);

		release_disp_output(vout);

		ret = videobuf_streamoff(&vout->vbq);
	}
	INIT_LIST_HEAD(&vout->queue_list);
	INIT_LIST_HEAD(&vout->active_list);

	return ret;
}

static const struct v4l2_ioctl_ops mxc_vout_ioctl_ops = {
	.vidioc_querycap			= mxc_vidioc_querycap,
	.vidioc_enum_fmt_vid_out		= mxc_vidioc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out			= mxc_vidioc_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out			= mxc_vidioc_s_fmt_vid_out,
	.vidioc_cropcap				= mxc_vidioc_cropcap,
	.vidioc_g_crop				= mxc_vidioc_g_crop,
	.vidioc_s_crop				= mxc_vidioc_s_crop,
	.vidioc_queryctrl			= mxc_vidioc_queryctrl,
	.vidioc_g_ctrl				= mxc_vidioc_g_ctrl,
	.vidioc_s_ctrl				= mxc_vidioc_s_ctrl,
	.vidioc_reqbufs				= mxc_vidioc_reqbufs,
	.vidioc_querybuf			= mxc_vidioc_querybuf,
	.vidioc_qbuf				= mxc_vidioc_qbuf,
	.vidioc_dqbuf				= mxc_vidioc_dqbuf,
	.vidioc_streamon			= mxc_vidioc_streamon,
	.vidioc_streamoff			= mxc_vidioc_streamoff,
};

static const struct v4l2_file_operations mxc_vout_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= mxc_vout_ioctl,
	.mmap		= mxc_vout_mmap,
	.open		= mxc_vout_open,
	.release	= mxc_vout_release,
};

static struct video_device mxc_vout_template = {
	.name		= "MXC Video Output",
	.fops           = &mxc_vout_fops,
	.ioctl_ops	= &mxc_vout_ioctl_ops,
	.release	= video_device_release,
};

static struct videobuf_queue_ops mxc_vout_vbq_ops = {
	.buf_setup = mxc_vout_buffer_setup,
	.buf_prepare = mxc_vout_buffer_prepare,
	.buf_release = mxc_vout_buffer_release,
	.buf_queue = mxc_vout_buffer_queue,
};

static void mxc_vout_free_output(struct mxc_vout_dev *dev)
{
	int i;
	int j;
	struct mxc_vout_output *vout;
	struct video_device *vfd;

	for (i = 0; i < dev->out_num; i++) {
		vout = dev->out[i];
		vfd = vout->vfd;
		if (vout->vdoa_work.vaddr)
			free_dma_buf(vout, &vout->vdoa_work);
		for (j = 0; j < VDOA_FB_BUFS; j++) {
			if (vout->vdoa_output[j].vaddr)
				free_dma_buf(vout, &vout->vdoa_output[j]);
		}
		if (vfd) {
			if (!video_is_registered(vfd))
				video_device_release(vfd);
			else
				video_unregister_device(vfd);
		}
		kfree(vout);
	}
}

static int mxc_vout_setup_output(struct mxc_vout_dev *dev)
{
	struct videobuf_queue *q;
	struct fb_info *fbi;
	struct mxc_vout_output *vout;
	int i, ret = 0;

	update_display_setting();

	/* all output/overlay based on fb */
	for (i = 0; i < num_registered_fb; i++) {
		fbi = registered_fb[i];

		vout = kzalloc(sizeof(struct mxc_vout_output), GFP_KERNEL);
		if (!vout) {
			ret = -ENOMEM;
			break;
		}

		dev->out[dev->out_num] = vout;
		dev->out_num++;

		vout->fbi = fbi;
		vout->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		vout->vfd = video_device_alloc();
		if (!vout->vfd) {
			ret = -ENOMEM;
			break;
		}

		*vout->vfd = mxc_vout_template;
		vout->vfd->v4l2_dev = &dev->v4l2_dev;
		vout->vfd->lock = &vout->mutex;
		vout->vfd->vfl_dir = VFL_DIR_TX;

		mutex_init(&vout->mutex);
		mutex_init(&vout->task_lock);
		mutex_init(&vout->accs_lock);

		strlcpy(vout->vfd->name, fbi->fix.id, sizeof(vout->vfd->name));

		video_set_drvdata(vout->vfd, vout);

		if (video_register_device(vout->vfd,
			VFL_TYPE_GRABBER, video_nr + i) < 0) {
			ret = -ENODEV;
			break;
		}

		q = &vout->vbq;
		q->dev = dev->dev;
		spin_lock_init(&vout->vbq_lock);
		videobuf_queue_dma_contig_init(q, &mxc_vout_vbq_ops, q->dev,
				&vout->vbq_lock, vout->type, V4L2_FIELD_NONE,
				sizeof(struct videobuf_buffer), vout, NULL);

		v4l2_info(vout->vfd->v4l2_dev, "V4L2 device registered as %s\n",
				video_device_node_name(vout->vfd));

	}

	return ret;
}

static int mxc_vout_probe(struct platform_device *pdev)
{
	int ret;
	struct mxc_vout_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->dev = &pdev->dev;
	dev->dev->dma_mask = kmalloc(sizeof(*dev->dev->dma_mask), GFP_KERNEL);
	*dev->dev->dma_mask = DMA_BIT_MASK(32);
	dev->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	ret = v4l2_device_register(dev->dev, &dev->v4l2_dev);
	if (ret) {
		dev_err(dev->dev, "v4l2_device_register failed\n");
		goto free_dev;
	}

	ret = mxc_vout_setup_output(dev);
	if (ret < 0)
		goto rel_vdev;

	return 0;

rel_vdev:
	mxc_vout_free_output(dev);
	v4l2_device_unregister(&dev->v4l2_dev);
free_dev:
	kfree(dev);
	return ret;
}

static int mxc_vout_remove(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct mxc_vout_dev *dev = container_of(v4l2_dev, struct
			mxc_vout_dev, v4l2_dev);

	mxc_vout_free_output(dev);
	v4l2_device_unregister(v4l2_dev);
	kfree(dev);
	return 0;
}

static const struct of_device_id mxc_v4l2_dt_ids[] = {
	{ .compatible = "fsl,mxc_v4l2_output", },
	{ /* sentinel */ }
};

static struct platform_driver mxc_vout_driver = {
	.driver = {
		.name = "mxc_v4l2_output",
		.of_match_table = mxc_v4l2_dt_ids,
	},
	.probe = mxc_vout_probe,
	.remove = mxc_vout_remove,
};

static int __init mxc_vout_init(void)
{
	if (platform_driver_register(&mxc_vout_driver) != 0) {
		printk(KERN_ERR VOUT_NAME ":Could not register Video driver\n");
		return -EINVAL;
	}
	return 0;
}

static void mxc_vout_cleanup(void)
{
	platform_driver_unregister(&mxc_vout_driver);
}

module_init(mxc_vout_init);
module_exit(mxc_vout_cleanup);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("V4L2-driver for MXC video output");
MODULE_LICENSE("GPL");
