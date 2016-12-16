/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Yannick Fertre <yannick.fertre@st.com>
 *          Hugues Fruchet <hugues.fruchet@st.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef HVA_H
#define HVA_H

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mem2mem.h>

#define fh_to_ctx(f)    (container_of(f, struct hva_ctx, fh))

#define hva_to_dev(h)   (h->dev)

#define ctx_to_dev(c)   (c->hva_dev->dev)

#define ctx_to_hdev(c)  (c->hva_dev)

#define HVA_PREFIX "[---:----]"

extern const struct hva_enc nv12h264enc;
extern const struct hva_enc nv21h264enc;

/**
 * struct hva_frameinfo - information about hva frame
 *
 * @pixelformat:    fourcc code for uncompressed video format
 * @width:          width of frame
 * @height:         height of frame
 * @aligned_width:  width of frame (with encoder alignment constraint)
 * @aligned_height: height of frame (with encoder alignment constraint)
 * @size:           maximum size in bytes required for data
*/
struct hva_frameinfo {
	u32	pixelformat;
	u32	width;
	u32	height;
	u32	aligned_width;
	u32	aligned_height;
	u32	size;
};

/**
 * struct hva_streaminfo - information about hva stream
 *
 * @streamformat: fourcc code of compressed video format (H.264...)
 * @width:        width of stream
 * @height:       height of stream
 * @profile:      profile string
 * @level:        level string
 */
struct hva_streaminfo {
	u32	streamformat;
	u32	width;
	u32	height;
	u8	profile[32];
	u8	level[32];
};

/**
 * struct hva_controls - hva controls set
 *
 * @time_per_frame: time per frame in seconds
 * @bitrate_mode:   bitrate mode (constant bitrate or variable bitrate)
 * @gop_size:       groupe of picture size
 * @bitrate:        bitrate (in bps)
 * @aspect:         video aspect
 * @profile:        H.264 profile
 * @level:          H.264 level
 * @entropy_mode:   H.264 entropy mode (CABAC or CVLC)
 * @cpb_size:       coded picture buffer size (in kB)
 * @dct8x8:         transform mode 8x8 enable
 * @qpmin:          minimum quantizer
 * @qpmax:          maximum quantizer
 * @vui_sar:        pixel aspect ratio enable
 * @vui_sar_idc:    pixel aspect ratio identifier
 * @sei_fp:         sei frame packing arrangement enable
 * @sei_fp_type:    sei frame packing arrangement type
 */
struct hva_controls {
	struct v4l2_fract					time_per_frame;
	enum v4l2_mpeg_video_bitrate_mode			bitrate_mode;
	u32							gop_size;
	u32							bitrate;
	enum v4l2_mpeg_video_aspect				aspect;
	enum v4l2_mpeg_video_h264_profile			profile;
	enum v4l2_mpeg_video_h264_level				level;
	enum v4l2_mpeg_video_h264_entropy_mode			entropy_mode;
	u32							cpb_size;
	bool							dct8x8;
	u32							qpmin;
	u32							qpmax;
	bool							vui_sar;
	enum v4l2_mpeg_video_h264_vui_sar_idc			vui_sar_idc;
	bool							sei_fp;
	enum v4l2_mpeg_video_h264_sei_fp_arrangement_type	sei_fp_type;
};

/**
 * struct hva_frame - hva frame buffer (output)
 *
 * @vbuf:     video buffer information for V4L2
 * @list:     V4L2 m2m list that the frame belongs to
 * @info:     frame information (width, height, format, alignment...)
 * @paddr:    physical address (for hardware)
 * @vaddr:    virtual address (kernel can read/write)
 * @prepared: true if vaddr/paddr are resolved
 */
struct hva_frame {
	struct vb2_v4l2_buffer	vbuf;
	struct list_head	list;
	struct hva_frameinfo	info;
	dma_addr_t		paddr;
	void			*vaddr;
	bool			prepared;
};

/*
 * to_hva_frame() - cast struct vb2_v4l2_buffer * to struct hva_frame *
 */
#define to_hva_frame(vb) \
	container_of(vb, struct hva_frame, vbuf)

/**
 * struct hva_stream - hva stream buffer (capture)
 *
 * @v4l2:       video buffer information for V4L2
 * @list:       V4L2 m2m list that the frame belongs to
 * @paddr:      physical address (for hardware)
 * @vaddr:      virtual address (kernel can read/write)
 * @prepared:   true if vaddr/paddr are resolved
 * @size:       size of the buffer in bytes
 * @bytesused:  number of bytes occupied by data in the buffer
 */
struct hva_stream {
	struct vb2_v4l2_buffer	vbuf;
	struct list_head	list;
	dma_addr_t		paddr;
	void			*vaddr;
	bool			prepared;
	unsigned int		size;
	unsigned int		bytesused;
};

/*
 * to_hva_stream() - cast struct vb2_v4l2_buffer * to struct hva_stream *
 */
#define to_hva_stream(vb) \
	container_of(vb, struct hva_stream, vbuf)

struct hva_dev;
struct hva_enc;

/**
 * struct hva_ctx - context of hva instance
 *
 * @hva_dev:         the device that this instance is associated with
 * @fh:              V4L2 file handle
 * @ctrl_handler:    V4L2 controls handler
 * @ctrls:           hva controls set
 * @id:              instance identifier
 * @aborting:        true if current job aborted
 * @name:            instance name (debug purpose)
 * @run_work:        encode work
 * @lock:            mutex used to lock access of this context
 * @flags:           validity of streaminfo and frameinfo fields
 * @frame_num:       frame number
 * @stream_num:      stream number
 * @max_stream_size: maximum size in bytes required for stream data
 * @colorspace:      colorspace identifier
 * @xfer_func:       transfer function identifier
 * @ycbcr_enc:       Y'CbCr encoding identifier
 * @quantization:    quantization identifier
 * @streaminfo:      stream properties
 * @frameinfo:       frame properties
 * @enc:             current encoder
 * @priv:            private codec data for this instance, allocated
 *                   by encoder @open time
 * @hw_err:          true if hardware error detected
 */
struct hva_ctx {
	struct hva_dev		        *hva_dev;
	struct v4l2_fh			fh;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct hva_controls		ctrls;
	u8				id;
	bool				aborting;
	char				name[100];
	struct work_struct		run_work;
	/* mutex protecting this data structure */
	struct mutex			lock;
	u32				flags;
	u32				frame_num;
	u32				stream_num;
	u32				max_stream_size;
	enum v4l2_colorspace		colorspace;
	enum v4l2_xfer_func		xfer_func;
	enum v4l2_ycbcr_encoding	ycbcr_enc;
	enum v4l2_quantization		quantization;
	struct hva_streaminfo		streaminfo;
	struct hva_frameinfo		frameinfo;
	struct hva_enc			*enc;
	void				*priv;
	bool				hw_err;
};

#define HVA_FLAG_STREAMINFO	0x0001
#define HVA_FLAG_FRAMEINFO	0x0002

#define HVA_MAX_INSTANCES	16
#define HVA_MAX_ENCODERS	10
#define HVA_MAX_FORMATS		HVA_MAX_ENCODERS

/**
 * struct hva_dev - abstraction for hva entity
 *
 * @v4l2_dev:            V4L2 device
 * @vdev:                video device
 * @pdev:                platform device
 * @dev:                 device
 * @lock:                mutex used for critical sections & V4L2 ops
 *                       serialization
 * @m2m_dev:             memory-to-memory V4L2 device information
 * @instances:           opened instances
 * @nb_of_instances:     number of opened instances
 * @instance_id:         rolling counter identifying an instance (debug purpose)
 * @regs:                register io memory access
 * @esram_addr:          esram address
 * @esram_size:          esram size
 * @clk:                 hva clock
 * @irq_its:             status interruption
 * @irq_err:             error interruption
 * @work_queue:          work queue to handle the encode jobs
 * @protect_mutex:       mutex used to lock access of hardware
 * @interrupt:           completion interrupt
 * @ip_version:          IP hardware version
 * @encoders:            registered encoders
 * @nb_of_encoders:      number of registered encoders
 * @pixelformats:        supported uncompressed video formats
 * @nb_of_pixelformats:  number of supported umcompressed video formats
 * @streamformats:       supported compressed video formats
 * @nb_of_streamformats: number of supported compressed video formats
 * @sfl_reg:             status fifo level register value
 * @sts_reg:             status register value
 * @lmi_err_reg:         local memory interface error register value
 * @emi_err_reg:         external memory interface error register value
 * @hec_mif_err_reg:     HEC memory interface error register value
 */
struct hva_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	*vdev;
	struct platform_device	*pdev;
	struct device		*dev;
	/* mutex protecting vb2_queue structure */
	struct mutex		lock;
	struct v4l2_m2m_dev	*m2m_dev;
	struct hva_ctx		*instances[HVA_MAX_INSTANCES];
	unsigned int		nb_of_instances;
	unsigned int		instance_id;
	void __iomem		*regs;
	u32			esram_addr;
	u32			esram_size;
	struct clk		*clk;
	int			irq_its;
	int			irq_err;
	struct workqueue_struct *work_queue;
	/* mutex protecting hardware access */
	struct mutex		protect_mutex;
	struct completion	interrupt;
	unsigned long int	ip_version;
	const struct hva_enc	*encoders[HVA_MAX_ENCODERS];
	u32			nb_of_encoders;
	u32			pixelformats[HVA_MAX_FORMATS];
	u32			nb_of_pixelformats;
	u32			streamformats[HVA_MAX_FORMATS];
	u32			nb_of_streamformats;
	u32			sfl_reg;
	u32			sts_reg;
	u32			lmi_err_reg;
	u32			emi_err_reg;
	u32			hec_mif_err_reg;
};

/**
 * struct hva_enc - hva encoder
 *
 * @name:         encoder name
 * @streamformat: fourcc code for compressed video format (H.264...)
 * @pixelformat:  fourcc code for uncompressed video format
 * @max_width:    maximum width of frame for this encoder
 * @max_height:   maximum height of frame for this encoder
 * @open:         open encoder
 * @close:        close encoder
 * @encode:       encode a frame (struct hva_frame) in a stream
 *                (struct hva_stream)
 */

struct hva_enc {
	const char	*name;
	u32		streamformat;
	u32		pixelformat;
	u32		max_width;
	u32		max_height;
	int		(*open)(struct hva_ctx *ctx);
	int		(*close)(struct hva_ctx *ctx);
	int		(*encode)(struct hva_ctx *ctx, struct hva_frame *frame,
				  struct hva_stream *stream);
};

#endif /* HVA_H */
