/* linux/drivers/media/video/samsung/fimc/fimc.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Header file for Samsung Camera Interface (FIMC) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/


#ifndef __FIMC_H
#define __FIMC_H __FILE__

#ifdef __KERNEL__
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <linux/platform_device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-core.h>
#include <media/v4l2-mediabus.h>
#ifdef CONFIG_BUSFREQ_OPP
#include <mach/dev.h>
#endif
#include <plat/media.h>
#include <plat/fimc.h>
#include <plat/cpu.h>
#endif

#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif

#define FIMC_NAME		"s3c-fimc"
#define FIMC_CMA_NAME		"fimc"

#define FIMC_CORE_CLK		"sclk_fimc"
#define FIMC_CLK_RATE		166750000
#define EXYNOS_BUSFREQ_NAME	"exynos-busfreq"

#if defined(CONFIG_ARCH_EXYNOS4)
#define FIMC_DEVICES		4
#define FIMC_PHYBUFS		32
#define FIMC_MAXCAMS		7
#else
#define FIMC_DEVICES		3
#define FIMC_PHYBUFS		4
#define FIMC_MAXCAMS		5
#endif

#define FIMC_SUBDEVS		3
#define FIMC_OUTBUFS		3
#define FIMC_INQUEUES		10
#define FIMC_MAX_CTXS		4
#define FIMC_TPID		3
#define FIMC_CAPBUFS		32
#define FIMC_ONESHOT_TIMEOUT	200
#define FIMC_DQUEUE_TIMEOUT	1000
#define FIMC_FIFOOFF_CNT	1000000 /* Sufficiently big value for stop */

#define FORMAT_FLAGS_PACKED	0x1
#define FORMAT_FLAGS_PLANAR	0x2

#define FIMC_ADDR_Y		0
#define FIMC_ADDR_CB		1
#define FIMC_ADDR_CR		2

#define FIMC_HD_WIDTH		1280
#define FIMC_HD_HEIGHT		720

#define FIMC_FHD_WIDTH		1920
#define FIMC_FHD_HEIGHT		1080

#define FIMC_MMAP_IDX		-1
#define FIMC_USERPTR_IDX	-2

#define FIMC_HCLK		0
#define FIMC_SCLK		1
#define CSI_CH_0		0
#define CSI_CH_1		1
#if defined(CONFIG_VIDEO_FIMC_FIFO)
#define FIMC_OVLY_MODE FIMC_OVLY_FIFO
#elif defined(CONFIG_VIDEO_FIMC_DMA_AUTO)
#define FIMC_OVLY_MODE FIMC_OVLY_DMA_AUTO
#endif

#define PINGPONG_2ADDR_MODE
#if defined(PINGPONG_2ADDR_MODE)
#define FIMC_PINGPONG 2
#endif

#define check_bit(data, loc)	((data) & (0x1<<(loc)))
#define FRAME_SEQ		0xf

#define fimc_cam_use		((pdata->use_cam) ? 1 : 0)

#define L2_FLUSH_ALL	SZ_1M
#define L1_FLUSH_ALL	SZ_64K

/*
 * ENUMERATIONS
*/
enum fimc_status {
	FIMC_READY_OFF		= 0x00,
	FIMC_STREAMOFF		= 0x01,
	FIMC_READY_ON		= 0x02,
	FIMC_STREAMON		= 0x03,
	FIMC_STREAMON_IDLE	= 0x04, /* oneshot mode */
	FIMC_OFF_SLEEP		= 0x05,
	FIMC_ON_SLEEP		= 0x06,
	FIMC_ON_IDLE_SLEEP	= 0x07, /* oneshot mode */
	FIMC_READY_RESUME	= 0x08,
	FIMC_BUFFER_STOP	= 0x09,
	FIMC_BUFFER_START	= 0x0A,
};

enum fimc_fifo_state {
	FIFO_CLOSE,
	FIFO_SLEEP,
};

enum fimc_fimd_state {
	FIMD_OFF,
	FIMD_ON,
};

enum fimc_rot_flip {
	FIMC_XFLIP	= 0x01,
	FIMC_YFLIP	= 0x02,
	FIMC_ROT	= 0x10,
};

enum fimc_input {
	FIMC_SRC_CAM,
	FIMC_SRC_MSDMA,
};

enum fimc_overlay_mode {
	FIMC_OVLY_NOT_FIXED		= 0x0,	/* Overlay mode isn't fixed. */
	FIMC_OVLY_FIFO			= 0x1,	/* Non-destructive Overlay with FIFO */
	FIMC_OVLY_DMA_AUTO		= 0x2,	/* Non-destructive Overlay with DMA */
	FIMC_OVLY_DMA_MANUAL		= 0x3,	/* Non-destructive Overlay with DMA */
	FIMC_OVLY_NONE_SINGLE_BUF	= 0x4,	/* Destructive Overlay with DMA single destination buffer */
	FIMC_OVLY_NONE_MULTI_BUF	= 0x5,	/* Destructive Overlay with DMA multiple dstination buffer */
};

enum fimc_autoload {
	FIMC_AUTO_LOAD,
	FIMC_ONE_SHOT,
};

enum fimc_log {
	FIMC_LOG_DEBUG		= 0x1000,
	FIMC_LOG_INFO_L2	= 0x0200,
	FIMC_LOG_INFO_L1	= 0x0100,
	FIMC_LOG_WARN		= 0x0010,
	FIMC_LOG_ERR		= 0x0001,
};

enum fimc_range {
	FIMC_RANGE_NARROW	= 0x0,
	FIMC_RANGE_WIDE		= 0x1,
};

enum fimc_pixel_format_type{
	FIMC_RGB,
	FIMC_YUV420,
	FIMC_YUV422,
	FIMC_YUV444,
};

enum fimc_framecnt_seq {
	FIMC_FRAMECNT_SEQ_DISABLE,
	FIMC_FRAMECNT_SEQ_ENABLE,
};

enum fimc_sysmmu_flag {
	FIMC_SYSMMU_OFF,
	FIMC_SYSMMU_ON,
};

enum fimc_id {
	FIMC0 = 0x0,
	FIMC1 = 0x1,
	FIMC2 = 0x2,
	FIMC3 = 0x3,
};

enum fimc_power_status {
	FIMC_POWER_OFF,
	FIMC_POWER_ON,
	FIMC_POWER_SUSPEND,
};

enum cam_mclk_status {
	CAM_MCLK_OFF,
	CAM_MCLK_ON,
};

/*
 * STRUCTURES
*/

/* for reserved memory */
struct fimc_meminfo {
	dma_addr_t	base;		/* buffer base */
	size_t		size;		/* total length */
	dma_addr_t	curr;		/* current addr */
	dma_addr_t	vaddr_base;		/* buffer base */
	dma_addr_t	vaddr_curr;		/* current addr */
};

struct fimc_buf {
	dma_addr_t	base[3];
	size_t		length[3];
};

struct fimc_overlay_buf {
	u32 vir_addr[3];
	size_t size[3];
	u32 phy_addr[3];
};

struct fimc_overlay {
	enum fimc_overlay_mode mode;
	struct fimc_overlay_buf buf;
	s32 req_idx;
};

/* general buffer */
struct fimc_buf_set {
	int			id;
	dma_addr_t		base[4];
	dma_addr_t		vaddr_base[4];
	size_t			length[4];
	size_t			garbage[4];
	enum videobuf_state	state;
	u32			flags;
	atomic_t		mapped_cnt;

	dma_addr_t		paddr_pktdata;
	u32			*vaddr_pktdata;

	struct list_head	list;
};

/* for capture device */
struct fimc_capinfo {
	struct v4l2_cropcap	cropcap;
	struct v4l2_rect	crop;
	struct v4l2_pix_format	fmt;
	struct v4l2_mbus_framefmt mbus_fmt;
	struct fimc_buf_set	bufs[FIMC_CAPBUFS];
	/* using c110 */
	struct list_head	inq;
	int			outq[FIMC_PHYBUFS];
	/* using c210 */
	struct list_head	outgoing_q;
	int			nr_bufs;
	int			irq;
	int			lastirq;

	bool		pktdata_enable;
	u32			pktdata_size;
	u32			pktdata_plane;

	/* flip: V4L2_CID_xFLIP, rotate: 90, 180, 270 */
	u32			flip;
	u32			rotate;
	bool		cacheable;
};

/* for output overlay device */
struct fimc_idx {
	int ctx;
	int idx;
};

struct fimc_ctx_idx {
	struct fimc_idx prev;
	struct fimc_idx active;
	struct fimc_idx next;
};

/* scaler abstraction: local use recommended */
struct fimc_scaler {
	u32 bypass;
	u32 hfactor;
	u32 vfactor;
	u32 pre_hratio;
	u32 pre_vratio;
	u32 pre_dst_width;
	u32 pre_dst_height;
	u32 scaleup_h;
	u32 scaleup_v;
	u32 main_hratio;
	u32 main_vratio;
	u32 real_width;
	u32 real_height;
	u32 shfactor;
	u32 skipline;
};

struct fimc_ctx {
	u32			ctx_num;
	struct v4l2_cropcap	cropcap;
	struct v4l2_rect 	crop;
	struct v4l2_pix_format	pix;
	struct v4l2_window	win;
	struct v4l2_framebuffer	fbuf;
	struct fimc_scaler	sc;
	struct fimc_overlay	overlay;

	u32			buf_num;
	u32			is_requested;
	struct fimc_buf_set	src[FIMC_OUTBUFS];
	struct fimc_buf_set	dst[FIMC_OUTBUFS];
	s32			inq[FIMC_OUTBUFS];
	s32			outq[FIMC_OUTBUFS];

	u32			flip;
	u32			rotate;
	enum fimc_status	status;
};

struct fimc_outinfo {
	int			last_ctx;
	spinlock_t		lock_in;
	spinlock_t		lock_out;
	spinlock_t		slock;
	struct fimc_idx		inq[FIMC_INQUEUES];
	struct fimc_ctx		ctx[FIMC_MAX_CTXS];
	bool			ctx_used[FIMC_MAX_CTXS];
	struct fimc_ctx_idx	idxs;
};

struct s3cfb_user_window {
	int x;
	int y;
};

enum s3cfb_data_path_t {
	DATA_PATH_FIFO	= 0,
	DATA_PATH_DMA	= 1,
	DATA_PATH_IPC	= 2,
};

enum s3cfb_mem_owner_t {
	DMA_MEM_NONE	= 0,
	DMA_MEM_FIMD	= 1,
	DMA_MEM_OTHER	= 2,
};
#define S3CFB_WIN_OFF_ALL	_IO('F', 202)
#define S3CFB_WIN_POSITION	_IOW('F', 203, struct s3cfb_user_window)
#define S3CFB_GET_LCD_WIDTH	_IOR('F', 302, int)
#define S3CFB_GET_LCD_HEIGHT	_IOR('F', 303, int)
#define S3CFB_SET_WRITEBACK	_IOW('F', 304, u32)
#define S3CFB_SET_WIN_ON	_IOW('F', 305, u32)
#define S3CFB_SET_WIN_OFF	_IOW('F', 306, u32)
#define S3CFB_SET_WIN_PATH	_IOW('F', 307, enum s3cfb_data_path_t)
#define S3CFB_SET_WIN_ADDR	_IOW('F', 308, unsigned long)
#define S3CFB_SET_WIN_MEM	_IOW('F', 309, enum s3cfb_mem_owner_t)
/* ------------------------------------------------------------------------ */

struct fimc_fbinfo {
	struct fb_fix_screeninfo	*fix;
	struct fb_var_screeninfo	*var;
	int				lcd_hres;
	int				lcd_vres;
	u32				is_enable;
	/* lcd fifo control */

	int (*open_fifo)(int id, int ch, int (*do_priv)(void *), void *param);
	int (*close_fifo)(int id, int (*do_priv)(void *), void *param);
};

struct fimc_limit {
	u32 pre_dst_w;
	u32 bypass_w;
	u32 trg_h_no_rot;
	u32 trg_h_rot;
	u32 real_w_no_rot;
	u32 real_h_rot;
};

enum FIMC_EFFECT_FIN {
	FIMC_EFFECT_FIN_BYPASS = 0,
	FIMC_EFFECT_FIN_ARBITRARY_CBCR,
	FIMC_EFFECT_FIN_NEGATIVE,
	FIMC_EFFECT_FIN_ART_FREEZE,
	FIMC_EFFECT_FIN_EMBOSSING,
	FIMC_EFFECT_FIN_SILHOUETTE,
};


struct fimc_effect {
	int ie_on;
	int ie_after_sc;
	enum FIMC_EFFECT_FIN fin;
	int pat_cb;
	int pat_cr;
};

struct fimc_is {
	struct v4l2_pix_format	fmt;
	struct v4l2_mbus_framefmt mbus_fmt;
	struct v4l2_subdev      *sd;
	u32 frame_count;
	u32 valid;
	u32 bad_mark;
	u32 offset_x;
	u32 offset_y;
	u32 zoom_in_width;
	u32 zoom_in_height;
};

/* fimc controller abstration */
struct fimc_control {
	int				id;		/* controller id */
	char				name[16];
	atomic_t			in_use;
	void __iomem			*regs;		/* register i/o */
	struct clk			*clk;		/* interface clock */
	struct fimc_meminfo		mem;		/* for reserved mem */
	atomic_t			irq_cnt;	/* for interrupt cnt */
	struct work_struct		work_struct;	/* for work queue */
	struct workqueue_struct		*fimc_irq_wq;	/* for work queue */

	/* kernel helpers */
	struct mutex			lock;		/* controller lock */
	struct mutex			v4l2_lock;
	spinlock_t			outq_lock;
	wait_queue_head_t		wq;
	struct device			*dev;
#ifdef CONFIG_BUSFREQ_OPP
	struct device			*bus_dev;
#endif
	int				irq;

	/* v4l2 related */
	struct video_device		*vd;
	struct v4l2_device		v4l2_dev;
	struct v4l2_subdev		*flite_sd;
	struct fimc_is			is;
	/* fimc specific */
	struct fimc_limit		*limit;		/* H/W limitation */
	struct s3c_platform_camera	*cam;		/* activated camera */
	struct fimc_capinfo		*cap;		/* capture dev info */
	struct fimc_outinfo		*out;		/* output dev info */
	struct fimc_fbinfo		fb;		/* fimd info */
	struct fimc_scaler		sc;		/* scaler info */
	struct fimc_effect		fe;		/* fimc effect info */

	enum fimc_status		status;
	enum fimc_log			log;
	enum fimc_range			range;
	/* for suspend mode */
	int 				suspend_flag;
	int 				suspend_framecnt;
	enum fimc_sysmmu_flag		sysmmu_flag;
	enum fimc_power_status		power_status;
	char 				cma_name[16];
	bool				restart;
};

/* global */
struct fimc_global {
	struct fimc_control 		ctrl[FIMC_DEVICES];
	struct s3c_platform_camera	*camera[FIMC_MAXCAMS];
	int				camera_isvalid[FIMC_MAXCAMS];
	int 				active_camera;
	int				initialized;
	enum cam_mclk_status		mclk_status;
};

struct fimc_prv_data {
	struct fimc_control *ctrl;
	int ctx_id;
};

/* debug macro */
#define FIMC_LOG_DEFAULT	(FIMC_LOG_WARN | FIMC_LOG_ERR )

#define FIMC_DEBUG(fmt, ...)						\
	do {								\
		if (ctrl->log & FIMC_LOG_DEBUG)				\
			printk(KERN_INFO FIMC_NAME "%d: "		\
				fmt, ctrl->id, ##__VA_ARGS__);			\
	} while (0)

#define FIMC_INFO_L2(fmt, ...)						\
	do {								\
		if (ctrl->log & FIMC_LOG_INFO_L2)			\
			printk(KERN_INFO FIMC_NAME "%d: "		\
				fmt, ctrl->id, ##__VA_ARGS__);			\
	} while (0)

#define FIMC_INFO_L1(fmt, ...)						\
	do {								\
		if (ctrl->log & FIMC_LOG_INFO_L1)			\
			printk(KERN_INFO FIMC_NAME "%d: "		\
				fmt, ctrl->id, ##__VA_ARGS__);			\
	} while (0)

#define FIMC_WARN(fmt, ...)						\
	do {								\
		if (ctrl->log & FIMC_LOG_WARN)				\
			printk(KERN_INFO FIMC_NAME "%d: "		\
				fmt, ctrl->id, ##__VA_ARGS__);			\
	} while (0)


#define FIMC_ERROR(fmt, ...)						\
	do {								\
		if (ctrl->log & FIMC_LOG_ERR)				\
			printk(KERN_INFO FIMC_NAME "%d: "		\
				fmt, ctrl->id, ##__VA_ARGS__);			\
	} while (0)


#define fimc_dbg(fmt, ...)		FIMC_DEBUG(fmt, ##__VA_ARGS__)
#define fimc_info2(fmt, ...)		FIMC_INFO_L2(fmt, ##__VA_ARGS__)
#define fimc_info1(fmt, ...)		FIMC_INFO_L1(fmt, ##__VA_ARGS__)
#define fimc_warn(fmt, ...)		FIMC_WARN(fmt, ##__VA_ARGS__)
#define fimc_err(fmt, ...)		FIMC_ERROR(fmt, ##__VA_ARGS__)

/*
 * EXTERNS
*/
extern struct fimc_global *fimc_dev;
extern struct video_device fimc_video_device[FIMC_DEVICES];
extern const struct v4l2_ioctl_ops fimc_v4l2_ops;
extern struct fimc_limit fimc40_limits[FIMC_DEVICES];
extern struct fimc_limit fimc43_limits[FIMC_DEVICES];
extern struct fimc_limit fimc50_limits[FIMC_DEVICES];
extern struct fimc_limit fimc51_limits[FIMC_DEVICES];

/* FIMD */
#ifdef CONFIG_FB_S5P /* Legacy FIMD */
extern int s3cfb_direct_ioctl(int id, unsigned int cmd, unsigned long arg);
extern int s3cfb_open_fifo(int id, int ch, int (*do_priv)(void *), void *param);
extern int s3cfb_close_fifo(int id, int (*do_priv)(void *), void *param);
#else /* Mainline FIMD */
static inline int s3cfb_direct_ioctl(int id, unsigned int cmd, unsigned long arg) { return 0; }
static inline int s3cfb_open_fifo(int id, int ch, int (*do_priv)(void *), void *param) { return 0; }
static inline int s3cfb_close_fifo(int id, int (*do_priv)(void *), void *param) { return 0; }
#endif

/* general */
extern void s3c_csis_start(int csis_id, int lanes, int settle, int align, int width, int height, int pixel_format);
extern void s3c_csis_stop(int csis_id);
extern int s3c_csis_get_pkt(int csis_id, void *pktdata);
extern void s3c_csis_enable_pktdata(int csis_id, bool enable);

extern int fimc_dma_alloc(struct fimc_control *ctrl, struct fimc_buf_set *bs, int i, int align);
extern void fimc_dma_free(struct fimc_control *ctrl, struct fimc_buf_set *bs, int i);
extern u32 fimc_mapping_rot_flip(u32 rot, u32 flip);
extern int fimc_get_scaler_factor(u32 src, u32 tar, u32 *ratio, u32 *shift);
extern void fimc_get_nv12t_size(int img_hres, int img_vres,
					int *y_size, int *cb_size);
extern int fimc_hwget_number_of_bits(u32 framecnt_seq);

/* camera */
extern int fimc_select_camera(struct fimc_control *ctrl);

/* capture device */
extern int fimc_enum_input(struct file *file, void *fh, struct v4l2_input *inp);
extern int fimc_g_input(struct file *file, void *fh, unsigned int *i);
extern int fimc_s_input(struct file *file, void *fh, unsigned int i);
extern int fimc_enum_fmt_vid_capture(struct file *file, void *fh, struct v4l2_fmtdesc *f);
extern int fimc_g_fmt_vid_capture(struct file *file, void *fh, struct v4l2_format *f);
extern int fimc_s_fmt_vid_capture(struct file *file, void *fh, struct v4l2_format *f);
extern int fimc_s_fmt_vid_private(struct file *file, void *fh, struct v4l2_format *f);
extern int fimc_try_fmt_vid_capture(struct file *file, void *fh, struct v4l2_format *f);
extern int fimc_reqbufs_capture(void *fh, struct v4l2_requestbuffers *b);
extern int fimc_querybuf_capture(void *fh, struct v4l2_buffer *b);
extern int fimc_g_ctrl_capture(void *fh, struct v4l2_control *c);
extern int fimc_g_ext_ctrls_capture(void *fh, struct v4l2_ext_controls *c);
extern int fimc_s_ctrl_capture(void *fh, struct v4l2_control *c);
extern int fimc_s_ext_ctrls_capture(void *fh, struct v4l2_ext_controls *c);
#if defined(CONFIG_CPU_S5PV210)
extern int fimc_change_clksrc(struct fimc_control *ctrl, int fimc_clk);
#endif
extern int fimc_cropcap_capture(void *fh, struct v4l2_cropcap *a);
extern int fimc_g_crop_capture(void *fh, struct v4l2_crop *a);
extern int fimc_s_crop_capture(void *fh, struct v4l2_crop *a);
extern int fimc_streamon_capture(void *fh);
extern int fimc_streamoff_capture(void *fh);
extern int fimc_qbuf_capture(void *fh, struct v4l2_buffer *b);
extern int fimc_dqbuf_capture(void *fh, struct v4l2_buffer *b);
extern int fimc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a);
extern int fimc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a);
extern int fimc_queryctrl(struct file *file, void *fh, struct v4l2_queryctrl *qc);
extern int fimc_querymenu(struct file *file, void *fh, struct v4l2_querymenu *qm);
extern int fimc_stop_capture(struct fimc_control *ctrl);
extern int fimc_enum_framesizes(struct file *filp, void *fh, struct v4l2_frmsizeenum *fsize);
extern int fimc_enum_frameintervals(struct file *filp, void *fh, struct v4l2_frmivalenum *fival);
extern int fimc_release_subdev(struct fimc_control *ctrl);
extern int fimc_is_release_subdev(struct fimc_control *ctrl);
extern int fimc_is_set_zoom(struct fimc_control *ctrl, struct v4l2_control *c);
/* output device */
extern void fimc_outdev_set_src_addr(struct fimc_control *ctrl, dma_addr_t *base);
extern int fimc_outdev_set_ctx_param(struct fimc_control *ctrl, struct fimc_ctx *ctx);
extern int fimc_start_fifo(struct fimc_control *ctrl, struct fimc_ctx *ctx);
extern int fimc_fimd_rect(const struct fimc_control *ctrl, const struct fimc_ctx *ctx, struct v4l2_rect *fimd_rect);
extern int fimc_outdev_stop_streaming(struct fimc_control *ctrl, struct fimc_ctx *ctx);
extern int fimc_outdev_resume_dma(struct fimc_control *ctrl, struct fimc_ctx *ctx);
extern int fimc_outdev_start_camif(void *param);
extern int fimc_reqbufs_output(void *fh, struct v4l2_requestbuffers *b);
extern int fimc_querybuf_output(void *fh, struct v4l2_buffer *b);
extern int fimc_g_ctrl_output(void *fh, struct v4l2_control *c);
extern int fimc_s_ctrl_output(struct file *filp, void *fh, struct v4l2_control *c);
extern int fimc_cropcap_output(void *fh, struct v4l2_cropcap *a);
extern int fimc_g_crop_output(void *fh, struct v4l2_crop *a);
extern int fimc_s_crop_output(void *fh, struct v4l2_crop *a);
extern int fimc_streamon_output(void *fh);
extern int fimc_streamoff_output(void *fh);
extern int fimc_qbuf_output(void *fh, struct v4l2_buffer *b);
extern int fimc_dqbuf_output(void *fh, struct v4l2_buffer *b);
extern int fimc_g_fmt_vid_out(struct file *filp, void *fh, struct v4l2_format *f);
extern int fimc_s_fmt_vid_out(struct file *filp, void *fh, struct v4l2_format *f);
extern int fimc_try_fmt_vid_out(struct file *filp, void *fh, struct v4l2_format *f);

extern int fimc_init_in_queue(struct fimc_control *ctrl, struct fimc_ctx *ctx);
extern int fimc_push_inq(struct fimc_control *ctrl, struct fimc_ctx *ctx, int idx);
extern int fimc_pop_inq(struct fimc_control *ctrl, int *ctx_num, int *idx);
extern int fimc_push_outq(struct fimc_control *ctrl, struct fimc_ctx *ctx, int idx);
extern int fimc_pop_outq(struct fimc_control *ctrl, struct fimc_ctx *ctx, int *idx);
extern int fimc_init_out_queue(struct fimc_control *ctrl, struct fimc_ctx *ctx);
extern void fimc_outdev_init_idxs(struct fimc_control *ctrl);

extern void fimc_dump_context(struct fimc_control *ctrl, struct fimc_ctx *ctx);
extern void fimc_print_signal(struct fimc_control *ctrl);

/* overlay device */
extern int fimc_try_fmt_overlay(struct file *filp, void *fh, struct v4l2_format *f);
extern int fimc_g_fmt_vid_overlay(struct file *filp, void *fh, struct v4l2_format *f);
extern int fimc_s_fmt_vid_overlay(struct file *filp, void *fh, struct v4l2_format *f);
extern int fimc_g_fbuf(struct file *filp, void *fh, struct v4l2_framebuffer *fb);
extern int fimc_s_fbuf(struct file *filp, void *fh, struct v4l2_framebuffer *fb);

/* Register access file */
extern int fimc_hwset_camera_source(struct fimc_control *ctrl);
extern int fimc_hwset_camera_change_source(struct fimc_control *ctrl);
extern int fimc_hwset_enable_irq(struct fimc_control *ctrl, int overflow, int level);
extern int fimc_hwset_disable_irq(struct fimc_control *ctrl);
extern int fimc_hwset_clear_irq(struct fimc_control *ctrl);
extern int fimc_hwset_reset(struct fimc_control *ctrl);
extern int fimc_hwget_frame_end(struct fimc_control *ctrl);
extern int fimc_hwset_clksrc(struct fimc_control *ctrl, int src_clk);
extern int fimc_hwget_overflow_state(struct fimc_control *ctrl);
extern int fimc_hwset_camera_offset(struct fimc_control *ctrl);
extern int fimc_hwset_camera_polarity(struct fimc_control *ctrl);
extern int fimc_hwset_camera_type(struct fimc_control *ctrl);
extern int fimc_hwset_output_size(struct fimc_control *ctrl, int width, int height);
extern int fimc_hwset_output_colorspace(struct fimc_control *ctrl, u32 pixelformat);
extern int fimc_hwset_output_rot_flip(struct fimc_control *ctrl, u32 rot, u32 flip);
extern int fimc_hwset_output_area(struct fimc_control *ctrl, u32 width, u32 height);
extern int fimc_hwset_output_area_size(struct fimc_control *ctrl, u32 size);
extern int fimc_hwset_output_scan(struct fimc_control *ctrl, struct v4l2_pix_format *fmt);
extern int fimc_hwset_enable_lastirq(struct fimc_control *ctrl);
extern int fimc_hwset_disable_lastirq(struct fimc_control *ctrl);
extern int fimc_hwset_enable_lastend(struct fimc_control *ctrl);
extern int fimc_hwset_disable_lastend(struct fimc_control *ctrl);
extern int fimc_hwset_prescaler(struct fimc_control *ctrl, struct fimc_scaler *sc);
extern int fimc_hwset_output_yuv(struct fimc_control *ctrl, u32 pixelformat);
extern int fimc_hwset_output_address(struct fimc_control *ctrl, struct fimc_buf_set *bs, int id);
extern int fimc_hwset_input_rot(struct fimc_control *ctrl, u32 rot, u32 flip);
extern int fimc_hwset_scaler(struct fimc_control *ctrl, struct fimc_scaler *sc);
extern int fimc_hwset_scaler_bypass(struct fimc_control *ctrl);
extern int fimc_hwset_enable_lcdfifo(struct fimc_control *ctrl);
extern int fimc_hwset_disable_lcdfifo(struct fimc_control *ctrl);
extern int fimc_hwset_start_scaler(struct fimc_control *ctrl);
extern int fimc_hwset_stop_scaler(struct fimc_control *ctrl);
extern int fimc_hwset_input_rgb(struct fimc_control *ctrl, u32 pixelformat);
extern int fimc_hwset_intput_field(struct fimc_control *ctrl, enum v4l2_field field);
extern int fimc_hwset_output_rgb(struct fimc_control *ctrl, u32 pixelformat);
extern int fimc_hwset_ext_rgb(struct fimc_control *ctrl, int enable);
extern int fimc_hwset_enable_capture(struct fimc_control *ctrl, u32 bypass);
extern int fimc_hwset_disable_capture(struct fimc_control *ctrl);
extern void fimc_wait_disable_capture(struct fimc_control *ctrl);
extern int fimc_hwset_input_address(struct fimc_control *ctrl, dma_addr_t *base);
extern int fimc_hwset_enable_autoload(struct fimc_control *ctrl);
extern int fimc_hwset_disable_autoload(struct fimc_control *ctrl);
extern int fimc_hwset_real_input_size(struct fimc_control *ctrl, u32 width, u32 height);
extern int fimc_hwset_addr_change_enable(struct fimc_control *ctrl);
extern int fimc_hwset_addr_change_disable(struct fimc_control *ctrl);
extern int fimc_hwset_input_burst_cnt(struct fimc_control *ctrl, u32 cnt);
extern int fimc_hwset_input_colorspace(struct fimc_control *ctrl, u32 pixelformat);
extern int fimc_hwset_input_yuv(struct fimc_control *ctrl, u32 pixelformat);
extern int fimc_hwset_input_flip(struct fimc_control *ctrl, u32 rot, u32 flip);
extern int fimc_hwset_input_source(struct fimc_control *ctrl, enum fimc_input path);
extern int fimc_hwset_start_input_dma(struct fimc_control *ctrl);
extern int fimc_hwset_stop_input_dma(struct fimc_control *ctrl);
extern int fimc_hwset_output_offset(struct fimc_control *ctrl, u32 pixelformat, struct v4l2_rect *bound, struct v4l2_rect *crop);
extern int fimc_hwset_input_offset(struct fimc_control *ctrl, u32 pixelformat, struct v4l2_rect *bound, struct v4l2_rect *crop);
extern int fimc_hwset_org_input_size(struct fimc_control *ctrl, u32 width, u32 height);
extern int fimc_hwset_org_output_size(struct fimc_control *ctrl, u32 width, u32 height);
extern int fimc_hwset_ext_output_size(struct fimc_control *ctrl, u32 width, u32 height);
extern int fimc_hwset_input_addr_style(struct fimc_control *ctrl, u32 pixelformat);
extern int fimc_hwset_output_addr_style(struct fimc_control *ctrl, u32 pixelformat);
extern int fimc_hwset_jpeg_mode(struct fimc_control *ctrl, bool enable);
extern int fimc_hwget_frame_count(struct fimc_control *ctrl);
extern int fimc_hw_wait_winoff(struct fimc_control *ctrl);
extern int fimc_hw_wait_stop_input_dma(struct fimc_control *ctrl);
extern int fimc_hwset_input_lineskip(struct fimc_control *ctrl);
extern int fimc_hw_reset_camera(struct fimc_control *ctrl);
void fimc_hwset_stop_processing(struct fimc_control *ctrl);
extern int fimc_hw_reset_output_buf_sequence(struct fimc_control *ctrl);
extern int fimc_hwset_output_buf_sequence(struct fimc_control *ctrl, u32 shift, u32 enable);
extern void fimc_hwset_output_buf_sequence_all(struct fimc_control *ctrl, u32 framecnt_seq);
extern int fimc_hwget_output_buf_sequence(struct fimc_control *ctrl);
extern int fimc_hwget_before_frame_count(struct fimc_control *ctrl);
extern int fimc_hwget_present_frame_count(struct fimc_control *ctrl);
extern int fimc_hwget_output_buf_sequence(struct fimc_control *ctrl);
extern int fimc_hwget_check_framecount_sequence(struct fimc_control *ctrl, u32 frame);
extern int fimc_hwset_image_effect(struct fimc_control *ctrl);
extern int fimc_hwset_sysreg_camblk_fimd0_wb(struct fimc_control *ctrl);
extern int fimc_hwset_sysreg_camblk_fimd1_wb(struct fimc_control *ctrl);
extern int fimc_hwset_sysreg_camblk_isp_wb(struct fimc_control *ctrl);
extern int fimc_hwget_last_frame_end(struct fimc_control *ctrl);
extern void fimc_hwset_enable_frame_end_irq(struct fimc_control *ctrl);
extern void fimc_hwset_disable_frame_end_irq(struct fimc_control *ctrl);
extern void fimc_reset_status_reg(struct fimc_control *ctrl);
/* IPC related file */
extern void ipc_start(void);

/*
 * DRIVER HELPERS
 *
*/
#define to_fimc_plat(d)		(to_platform_device(d)->dev.platform_data)

static inline struct fimc_global *get_fimc_dev(void)
{
	return fimc_dev;
}

static inline struct fimc_control *get_fimc_ctrl(int id)
{
	return &fimc_dev->ctrl[id];
}

#endif /* __FIMC_H */
