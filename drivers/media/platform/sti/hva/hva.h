/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Yannick Fertre <yannick.fertre@st.com>
 *          Hugues Fruchet <hugues.fruchet@st.com>
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

#define HVA_NAME	"st-hva"
#define HVA_PREFIX	"[---:----]"

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

#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
/**
 * struct hva_ctx_dbg - instance context debug info
 *
 * @debugfs_entry:      debugfs entry
 * @is_valid_period:    true if the sequence is valid for performance
 * @begin:              start time of last HW task
 * @total_duration:     total HW processing durations in 0.1ms
 * @cnt_duration:       number of HW processings
 * @min_duration:       minimum HW processing duration in 0.1ms
 * @max_duration:       maximum HW processing duration in 0.1ms
 * @avg_duration:       average HW processing duration in 0.1ms
 * @max_fps:            maximum frames encoded per second (in 0.1Hz)
 * @total_period:       total encoding periods in 0.1ms
 * @cnt_period:         number of periods
 * @min_period:         minimum encoding period in 0.1ms
 * @max_period:         maximum encoding period in 0.1ms
 * @avg_period:         average encoding period in 0.1ms
 * @total_stream_size:  total number of encoded bytes
 * @avg_fps:            average frames encoded per second (in 0.1Hz)
 * @window_duration:    duration of the sampling window in 0.1ms
 * @cnt_window:         number of samples in the window
 * @window_stream_size: number of encoded bytes upon the sampling window
 * @last_bitrate:       bitrate upon the last sampling window
 * @min_bitrate:        minimum bitrate in kbps
 * @max_bitrate:        maximum bitrate in kbps
 * @avg_bitrate:        average bitrate in kbps
 */
struct hva_ctx_dbg {
	struct dentry	*debugfs_entry;
	bool		is_valid_period;
	ktime_t		begin;
	u32		total_duration;
	u32		cnt_duration;
	u32		min_duration;
	u32		max_duration;
	u32		avg_duration;
	u32		max_fps;
	u32		total_period;
	u32		cnt_period;
	u32		min_period;
	u32		max_period;
	u32		avg_period;
	u32		total_stream_size;
	u32		avg_fps;
	u32		window_duration;
	u32		cnt_window;
	u32		window_stream_size;
	u32		last_bitrate;
	u32		min_bitrate;
	u32		max_bitrate;
	u32		avg_bitrate;
};
#endif

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
 * @encoded_frames:  number of encoded frames
 * @sys_errors:      number of system errors (memory, resource, pm...)
 * @encode_errors:   number of encoding errors (hw/driver errors)
 * @frame_errors:    number of frame errors (format, size, header...)
 * @dbg:             context debug info
 */
struct hva_ctx {
	struct hva_dev			*hva_dev;
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
	u32				encoded_frames;
	u32				sys_errors;
	u32				encode_errors;
	u32				frame_errors;
#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
	struct hva_ctx_dbg		dbg;
#endif
};

#define HVA_FLAG_STREAMINFO	0x0001
#define HVA_FLAG_FRAMEINFO	0x0002

#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
/**
 * struct hva_dev_dbg - device debug info
 *
 * @debugfs_entry: debugfs entry
 * @last_ctx:      debug information about last running instance context
 */
struct hva_dev_dbg {
	struct dentry	*debugfs_entry;
	struct hva_ctx	last_ctx;
};
#endif

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
 * @dbg:                 device debug info
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
#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
	struct hva_dev_dbg	dbg;
#endif
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

#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
void hva_debugfs_create(struct hva_dev *hva);
void hva_debugfs_remove(struct hva_dev *hva);
void hva_dbg_ctx_create(struct hva_ctx *ctx);
void hva_dbg_ctx_remove(struct hva_ctx *ctx);
void hva_dbg_perf_begin(struct hva_ctx *ctx);
void hva_dbg_perf_end(struct hva_ctx *ctx, struct hva_stream *stream);
#endif

#endif /* HVA_H */
