/* SPDX-License-Identifier: GPL-2.0 */
/*
 * V4L2 Capture ISI subdev for i.MX8QXP/QM platform
 *
 * ISI is a Image Sensor Interface of i.MX8QXP/QM platform, which
 * used to process image from camera sensor to memory or DC
 * Copyright 2019-2020 NXP
 */

#ifndef __MXC_ISI_CORE_H__
#define __MXC_ISI_CORE_H__

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>

struct clk_bulk_data;
struct dentry;
struct device;
struct media_intf_devnode;
struct regmap;
struct v4l2_m2m_dev;

/* Pipeline pads */
#define MXC_ISI_PIPE_PAD_SINK		0
#define MXC_ISI_PIPE_PAD_SOURCE		1
#define MXC_ISI_PIPE_PADS_NUM		2

#define MXC_ISI_MIN_WIDTH		1U
#define MXC_ISI_MIN_HEIGHT		1U
#define MXC_ISI_MAX_WIDTH_UNCHAINED	2048U
#define MXC_ISI_MAX_WIDTH_CHAINED	4096U
#define MXC_ISI_MAX_HEIGHT		8191U

#define MXC_ISI_DEF_WIDTH		1920U
#define MXC_ISI_DEF_HEIGHT		1080U
#define MXC_ISI_DEF_MBUS_CODE_SINK	MEDIA_BUS_FMT_UYVY8_1X16
#define MXC_ISI_DEF_MBUS_CODE_SOURCE	MEDIA_BUS_FMT_YUV8_1X24
#define MXC_ISI_DEF_PIXEL_FORMAT	V4L2_PIX_FMT_YUYV
#define MXC_ISI_DEF_COLOR_SPACE		V4L2_COLORSPACE_SRGB
#define MXC_ISI_DEF_YCBCR_ENC		V4L2_YCBCR_ENC_601
#define MXC_ISI_DEF_QUANTIZATION	V4L2_QUANTIZATION_LIM_RANGE
#define MXC_ISI_DEF_XFER_FUNC		V4L2_XFER_FUNC_SRGB

#define MXC_ISI_DRIVER_NAME		"mxc-isi"
#define MXC_ISI_CAPTURE			"mxc-isi-cap"
#define MXC_ISI_M2M			"mxc-isi-m2m"
#define MXC_MAX_PLANES			3

struct mxc_isi_dev;
struct mxc_isi_m2m_ctx;

enum mxc_isi_buf_id {
	MXC_ISI_BUF1 = 0x0,
	MXC_ISI_BUF2,
};

enum mxc_isi_encoding {
	MXC_ISI_ENC_RAW,
	MXC_ISI_ENC_RGB,
	MXC_ISI_ENC_YUV,
};

enum mxc_isi_input_id {
	/* Inputs from the crossbar switch range from 0 to 15 */
	MXC_ISI_INPUT_MEM = 16,
};

enum mxc_isi_video_type {
	MXC_ISI_VIDEO_CAP = BIT(0),
	MXC_ISI_VIDEO_M2M_OUT = BIT(1),
	MXC_ISI_VIDEO_M2M_CAP = BIT(2),
};

struct mxc_isi_format_info {
	u32	mbus_code;
	u32	fourcc;
	enum mxc_isi_video_type type;
	u32	isi_in_format;
	u32	isi_out_format;
	u8	mem_planes;
	u8	color_planes;
	u8	depth[MXC_MAX_PLANES];
	u8	hsub;
	u8	vsub;
	enum mxc_isi_encoding encoding;
};

struct mxc_isi_bus_format_info {
	u32	mbus_code;
	u32	output;
	u32	pads;
	enum mxc_isi_encoding encoding;
};

struct mxc_isi_buffer {
	struct vb2_v4l2_buffer  v4l2_buf;
	struct list_head	list;
	dma_addr_t		dma_addrs[3];
	enum mxc_isi_buf_id	id;
	bool discard;
};

struct mxc_isi_reg {
	u32 mask;
};

struct mxc_isi_ier_reg {
	/* Overflow Y/U/V trigger enable*/
	struct mxc_isi_reg oflw_y_buf_en;
	struct mxc_isi_reg oflw_u_buf_en;
	struct mxc_isi_reg oflw_v_buf_en;

	/* Excess overflow Y/U/V trigger enable*/
	struct mxc_isi_reg excs_oflw_y_buf_en;
	struct mxc_isi_reg excs_oflw_u_buf_en;
	struct mxc_isi_reg excs_oflw_v_buf_en;

	/* Panic Y/U/V trigger enable*/
	struct mxc_isi_reg panic_y_buf_en;
	struct mxc_isi_reg panic_v_buf_en;
	struct mxc_isi_reg panic_u_buf_en;
};

struct mxc_isi_panic_thd {
	u32 mask;
	u32 offset;
	u32 threshold;
};

struct mxc_isi_set_thd {
	struct mxc_isi_panic_thd panic_set_thd_y;
	struct mxc_isi_panic_thd panic_set_thd_u;
	struct mxc_isi_panic_thd panic_set_thd_v;
};

struct mxc_gasket_ops {
	void (*enable)(struct mxc_isi_dev *isi,
		       const struct v4l2_mbus_frame_desc *fd,
		       const struct v4l2_mbus_framefmt *fmt,
		       const unsigned int port);
	void (*disable)(struct mxc_isi_dev *isi, const unsigned int port);
};

enum model {
	MXC_ISI_IMX8MN,
	MXC_ISI_IMX8MP,
	MXC_ISI_IMX8QM,
	MXC_ISI_IMX8QXP,
	MXC_ISI_IMX8ULP,
	MXC_ISI_IMX93,
};

struct mxc_isi_plat_data {
	enum model model;
	unsigned int num_ports;
	unsigned int num_channels;
	unsigned int reg_offset;
	const struct mxc_isi_ier_reg  *ier_reg;
	const struct mxc_isi_set_thd *set_thd;
	const struct mxc_gasket_ops *gasket_ops;
	bool buf_active_reverse;
	bool has_36bit_dma;
};

struct mxc_isi_dma_buffer {
	size_t				size;
	void				*addr;
	dma_addr_t			dma;
};

struct mxc_isi_input {
	unsigned int			enable_count;
};

struct mxc_isi_crossbar {
	struct mxc_isi_dev		*isi;

	unsigned int			num_sinks;
	unsigned int			num_sources;
	struct mxc_isi_input		*inputs;

	struct v4l2_subdev		sd;
	struct media_pad		*pads;
};

struct mxc_isi_video {
	struct mxc_isi_pipe		*pipe;

	struct video_device		vdev;
	struct media_pad		pad;

	/* Protects the vdev and vb2_q operations */
	struct mutex			lock;

	struct v4l2_pix_format_mplane	pix;
	const struct mxc_isi_format_info *fmtinfo;

	struct {
		struct v4l2_ctrl_handler handler;
		unsigned int		alpha;
		bool			hflip;
		bool			vflip;
	} ctrls;

	struct vb2_queue		vb2_q;
	struct mxc_isi_buffer		buf_discard[3];
	struct list_head		out_pending;
	struct list_head		out_active;
	struct list_head		out_discard;
	u32				frame_count;
	/* Protects out_pending, out_active, out_discard and frame_count */
	spinlock_t			buf_lock;

	struct mxc_isi_dma_buffer	discard_buffer[MXC_MAX_PLANES];
};

typedef void(*mxc_isi_pipe_irq_t)(struct mxc_isi_pipe *, u32);

struct mxc_isi_pipe {
	struct mxc_isi_dev		*isi;
	u32				id;
	void __iomem			*regs;

	struct media_pipeline		pipe;

	struct v4l2_subdev		sd;
	struct media_pad		pads[MXC_ISI_PIPE_PADS_NUM];

	struct mxc_isi_video		video;

	/*
	 * Protects use_count, irq_handler, res_available, res_acquired,
	 * chained_res, and the CHNL_CTRL register.
	 */
	struct mutex			lock;
	unsigned int			use_count;
	mxc_isi_pipe_irq_t		irq_handler;

#define MXC_ISI_CHANNEL_RES_LINE_BUF	BIT(0)
#define MXC_ISI_CHANNEL_RES_OUTPUT_BUF	BIT(1)
	u8				available_res;
	u8				acquired_res;
	u8				chained_res;
	bool				chained;
};

struct mxc_isi_m2m {
	struct mxc_isi_dev		*isi;
	struct mxc_isi_pipe		*pipe;

	struct media_pad		pad;
	struct video_device		vdev;
	struct media_intf_devnode	*intf;
	struct v4l2_m2m_dev		*m2m_dev;

	/* Protects last_ctx, usage_count and chained_count */
	struct mutex			lock;

	struct mxc_isi_m2m_ctx		*last_ctx;
	int				usage_count;
	int				chained_count;
};

struct mxc_isi_dev {
	struct device			*dev;

	const struct mxc_isi_plat_data	*pdata;

	void __iomem			*regs;
	struct clk_bulk_data		*clks;
	int				num_clks;
	struct regmap			*gasket;

	struct mxc_isi_crossbar		crossbar;
	struct mxc_isi_pipe		*pipes;
	struct mxc_isi_m2m		m2m;

	struct media_device		media_dev;
	struct v4l2_device		v4l2_dev;
	struct v4l2_async_notifier	notifier;

	struct dentry			*debugfs_root;
};

extern const struct mxc_gasket_ops mxc_imx8_gasket_ops;
extern const struct mxc_gasket_ops mxc_imx93_gasket_ops;

int mxc_isi_crossbar_init(struct mxc_isi_dev *isi);
void mxc_isi_crossbar_cleanup(struct mxc_isi_crossbar *xbar);
int mxc_isi_crossbar_register(struct mxc_isi_crossbar *xbar);
void mxc_isi_crossbar_unregister(struct mxc_isi_crossbar *xbar);

const struct mxc_isi_bus_format_info *
mxc_isi_bus_format_by_code(u32 code, unsigned int pad);
const struct mxc_isi_bus_format_info *
mxc_isi_bus_format_by_index(unsigned int index, unsigned int pad);
const struct mxc_isi_format_info *
mxc_isi_format_by_fourcc(u32 fourcc, enum mxc_isi_video_type type);
const struct mxc_isi_format_info *
mxc_isi_format_enum(unsigned int index, enum mxc_isi_video_type type);
const struct mxc_isi_format_info *
mxc_isi_format_try(struct mxc_isi_pipe *pipe, struct v4l2_pix_format_mplane *pix,
		   enum mxc_isi_video_type type);

int mxc_isi_pipe_init(struct mxc_isi_dev *isi, unsigned int id);
void mxc_isi_pipe_cleanup(struct mxc_isi_pipe *pipe);
int mxc_isi_pipe_acquire(struct mxc_isi_pipe *pipe,
			 mxc_isi_pipe_irq_t irq_handler);
void mxc_isi_pipe_release(struct mxc_isi_pipe *pipe);
int mxc_isi_pipe_enable(struct mxc_isi_pipe *pipe);
void mxc_isi_pipe_disable(struct mxc_isi_pipe *pipe);

int mxc_isi_video_register(struct mxc_isi_pipe *pipe,
			   struct v4l2_device *v4l2_dev);
void mxc_isi_video_unregister(struct mxc_isi_pipe *pipe);
void mxc_isi_video_suspend(struct mxc_isi_pipe *pipe);
int mxc_isi_video_resume(struct mxc_isi_pipe *pipe);
int mxc_isi_video_queue_setup(const struct v4l2_pix_format_mplane *format,
			      const struct mxc_isi_format_info *info,
			      unsigned int *num_buffers,
			      unsigned int *num_planes, unsigned int sizes[]);
void mxc_isi_video_buffer_init(struct vb2_buffer *vb2, dma_addr_t dma_addrs[3],
			       const struct mxc_isi_format_info *info,
			       const struct v4l2_pix_format_mplane *pix);
int mxc_isi_video_buffer_prepare(struct mxc_isi_dev *isi, struct vb2_buffer *vb2,
				 const struct mxc_isi_format_info *info,
				 const struct v4l2_pix_format_mplane *pix);

#ifdef CONFIG_VIDEO_IMX8_ISI_M2M
int mxc_isi_m2m_register(struct mxc_isi_dev *isi, struct v4l2_device *v4l2_dev);
int mxc_isi_m2m_unregister(struct mxc_isi_dev *isi);
void mxc_isi_m2m_suspend(struct mxc_isi_m2m *m2m);
int mxc_isi_m2m_resume(struct mxc_isi_m2m *m2m);
#else
static inline int mxc_isi_m2m_register(struct mxc_isi_dev *isi,
				       struct v4l2_device *v4l2_dev)
{
	return 0;
}
static inline int mxc_isi_m2m_unregister(struct mxc_isi_dev *isi)
{
	return 0;
}
static inline void mxc_isi_m2m_suspend(struct mxc_isi_m2m *m2m)
{
}
static inline int mxc_isi_m2m_resume(struct mxc_isi_m2m *m2m)
{
	return 0;
}
#endif

int mxc_isi_channel_acquire(struct mxc_isi_pipe *pipe,
			    mxc_isi_pipe_irq_t irq_handler, bool bypass);
void mxc_isi_channel_release(struct mxc_isi_pipe *pipe);
void mxc_isi_channel_get(struct mxc_isi_pipe *pipe);
void mxc_isi_channel_put(struct mxc_isi_pipe *pipe);
void mxc_isi_channel_enable(struct mxc_isi_pipe *pipe);
void mxc_isi_channel_disable(struct mxc_isi_pipe *pipe);
int mxc_isi_channel_chain(struct mxc_isi_pipe *pipe);
void mxc_isi_channel_unchain(struct mxc_isi_pipe *pipe);

void mxc_isi_channel_config(struct mxc_isi_pipe *pipe,
			    enum mxc_isi_input_id input,
			    const struct v4l2_area *in_size,
			    const struct v4l2_area *scale,
			    const struct v4l2_rect *crop,
			    enum mxc_isi_encoding in_encoding,
			    enum mxc_isi_encoding out_encoding);

void mxc_isi_channel_set_input_format(struct mxc_isi_pipe *pipe,
				      const struct mxc_isi_format_info *info,
				      const struct v4l2_pix_format_mplane *format);
void mxc_isi_channel_set_output_format(struct mxc_isi_pipe *pipe,
				       const struct mxc_isi_format_info *info,
				       struct v4l2_pix_format_mplane *format);
void mxc_isi_channel_m2m_start(struct mxc_isi_pipe *pipe);

void mxc_isi_channel_set_alpha(struct mxc_isi_pipe *pipe, u8 alpha);
void mxc_isi_channel_set_flip(struct mxc_isi_pipe *pipe, bool hflip, bool vflip);

void mxc_isi_channel_set_inbuf(struct mxc_isi_pipe *pipe, dma_addr_t dma_addr);
void mxc_isi_channel_set_outbuf(struct mxc_isi_pipe *pipe,
				const dma_addr_t dma_addrs[3],
				enum mxc_isi_buf_id buf_id);

u32 mxc_isi_channel_irq_status(struct mxc_isi_pipe *pipe, bool clear);
void mxc_isi_channel_irq_clear(struct mxc_isi_pipe *pipe);

#if IS_ENABLED(CONFIG_DEBUG_FS)
void mxc_isi_debug_init(struct mxc_isi_dev *isi);
void mxc_isi_debug_cleanup(struct mxc_isi_dev *isi);
#else
static inline void mxc_isi_debug_init(struct mxc_isi_dev *isi)
{
}
static inline void mxc_isi_debug_cleanup(struct mxc_isi_dev *isi)
{
}
#endif

#endif /* __MXC_ISI_CORE_H__ */
