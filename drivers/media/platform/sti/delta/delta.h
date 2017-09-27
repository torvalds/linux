/*
 * Copyright (C) STMicroelectronics SA 2015
 * Author: Hugues Fruchet <hugues.fruchet@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef DELTA_H
#define DELTA_H

#include <linux/rpmsg.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>

#include "delta-cfg.h"

/*
 * enum delta_state - state of decoding instance
 *
 *@DELTA_STATE_WF_FORMAT:
 *	Wait for compressed format to be set by V4L2 client in order
 *	to know what is the relevant decoder to open.
 *
 *@DELTA_STATE_WF_STREAMINFO:
 *	Wait for stream information to be available (bitstream
 *	header parsing is done).
 *
 *@DELTA_STATE_READY:
 *	Decoding instance is ready to decode compressed access unit.
 *
 *@DELTA_STATE_WF_EOS:
 *	Decoding instance is waiting for EOS (End Of Stream) completion.
 *
 *@DELTA_STATE_EOS:
 *	EOS (End Of Stream) is completed (signaled to user). Decoding instance
 *	should then be closed.
 */
enum delta_state {
	DELTA_STATE_WF_FORMAT,
	DELTA_STATE_WF_STREAMINFO,
	DELTA_STATE_READY,
	DELTA_STATE_WF_EOS,
	DELTA_STATE_EOS
};

/*
 * struct delta_streaminfo - information about stream to decode
 *
 * @flags:		validity of fields (crop, pixelaspect, other)
 * @width:		width of video stream
 * @height:		height ""
 * @streamformat:	fourcc compressed format of video (MJPEG, MPEG2, ...)
 * @dpb:		number of frames needed to decode a single frame
 *			(h264 dpb, up to 16)
 * @crop:		cropping window inside decoded frame (1920x1080@0,0
 *			inside 1920x1088 frame for ex.)
 * @pixelaspect:	pixel aspect ratio of video (4/3, 5/4)
 * @field:		interlaced or not
 * @profile:		profile string
 * @level:		level string
 * @other:		other string information from codec
 * @colorspace:		colorspace identifier
 * @xfer_func:		transfer function identifier
 * @ycbcr_enc:		Y'CbCr encoding identifier
 * @quantization:	quantization identifier
 */
struct delta_streaminfo {
	u32 flags;
	u32 streamformat;
	u32 width;
	u32 height;
	u32 dpb;
	struct v4l2_rect crop;
	struct v4l2_fract pixelaspect;
	enum v4l2_field field;
	u8 profile[32];
	u8 level[32];
	u8 other[32];
	enum v4l2_colorspace colorspace;
	enum v4l2_xfer_func xfer_func;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
};

#define DELTA_STREAMINFO_FLAG_CROP		0x0001
#define DELTA_STREAMINFO_FLAG_PIXELASPECT	0x0002
#define DELTA_STREAMINFO_FLAG_OTHER		0x0004

/*
 * struct delta_au - access unit structure.
 *
 * @vbuf:	video buffer information for V4L2
 * @list:	V4L2 m2m list that the frame belongs to
 * @prepared:	if set vaddr/paddr are resolved
 * @vaddr:	virtual address (kernel can read/write)
 * @paddr:	physical address (for hardware)
 * @flags:	access unit type (V4L2_BUF_FLAG_KEYFRAME/PFRAME/BFRAME)
 * @dts:	decoding timestamp of this access unit
 */
struct delta_au {
	struct vb2_v4l2_buffer vbuf;	/* keep first */
	struct list_head list;	/* keep second */

	bool prepared;
	u32 size;
	void *vaddr;
	dma_addr_t paddr;
	u32 flags;
	u64 dts;
};

/*
 * struct delta_frameinfo - information about decoded frame
 *
 * @flags:		validity of fields (crop, pixelaspect)
 * @pixelformat:	fourcc code for uncompressed video format
 * @width:		width of frame
 * @height:		height of frame
 * @aligned_width:	width of frame (with encoder or decoder alignment
 *			constraint)
 * @aligned_height:	height of frame (with encoder or decoder alignment
 *			constraint)
 * @size:		maximum size in bytes required for data
 * @crop:		cropping window inside frame (1920x1080@0,0
 *			inside 1920x1088 frame for ex.)
 * @pixelaspect:	pixel aspect ratio of video (4/3, 5/4)
 * @field:		interlaced mode
 * @colorspace:		colorspace identifier
 * @xfer_func:		transfer function identifier
 * @ycbcr_enc:		Y'CbCr encoding identifier
 * @quantization:	quantization identifier
 */
struct delta_frameinfo {
	u32 flags;
	u32 pixelformat;
	u32 width;
	u32 height;
	u32 aligned_width;
	u32 aligned_height;
	u32 size;
	struct v4l2_rect crop;
	struct v4l2_fract pixelaspect;
	enum v4l2_field field;
	enum v4l2_colorspace colorspace;
	enum v4l2_xfer_func xfer_func;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
};

#define DELTA_FRAMEINFO_FLAG_CROP		0x0001
#define DELTA_FRAMEINFO_FLAG_PIXELASPECT	0x0002

/*
 * struct delta_frame - frame structure.
 *
 * @vbuf:	video buffer information for V4L2
 * @list:	V4L2 m2m list that the frame belongs to
 * @info:	frame information (width, height, format, alignment...)
 * @prepared:	if set pix/vaddr/paddr are resolved
 * @index:	frame index, aligned on V4L2 wow
 * @vaddr:	virtual address (kernel can read/write)
 * @paddr:	physical address (for hardware)
 * @state:	frame state for frame lifecycle tracking
 *		(DELTA_FRAME_FREE/DEC/OUT/REC/...)
 * @flags:	frame type (V4L2_BUF_FLAG_KEYFRAME/PFRAME/BFRAME)
 * @dts:	decoding timestamp of this frame
 * @field:	field order for interlaced frame
 */
struct delta_frame {
	struct vb2_v4l2_buffer vbuf;	/* keep first */
	struct list_head list;	/* keep second */

	struct delta_frameinfo info;
	bool prepared;
	u32 index;
	void *vaddr;
	dma_addr_t paddr;
	u32 state;
	u32 flags;
	u64 dts;
	enum v4l2_field field;
};

/* frame state for frame lifecycle tracking */
#define DELTA_FRAME_FREE	0x00 /* is free and can be used for decoding */
#define DELTA_FRAME_REF		0x01 /* is a reference frame */
#define DELTA_FRAME_BSY		0x02 /* is owned by decoder and busy */
#define DELTA_FRAME_DEC		0x04 /* contains decoded content */
#define DELTA_FRAME_OUT		0x08 /* has been given to user */
#define DELTA_FRAME_RDY		0x10 /* is ready but still held by decoder */
#define DELTA_FRAME_M2M		0x20 /* is owned by mem2mem framework */

/*
 * struct delta_dts - decoding timestamp.
 *
 * @list:	list to chain timestamps
 * @val:	timestamp in microseconds
 */
struct delta_dts {
	struct list_head list;
	u64 val;
};

struct delta_buf {
	u32 size;
	void *vaddr;
	dma_addr_t paddr;
	const char *name;
	unsigned long attrs;
};

struct delta_ipc_ctx {
	int cb_err;
	u32 copro_hdl;
	struct completion done;
	struct delta_buf ipc_buf_struct;
	struct delta_buf *ipc_buf;
};

struct delta_ipc_param {
	u32 size;
	void *data;
};

struct delta_ctx;

/*
 * struct delta_dec - decoder structure.
 *
 * @name:		name of this decoder
 * @streamformat:	input stream format that this decoder support
 * @pixelformat:	pixel format of decoded frame that this decoder support
 * @max_width:		(optional) maximum width that can decode this decoder
 *			if not set, maximum width is DELTA_MAX_WIDTH
 * @max_height:		(optional) maximum height that can decode this decoder
 *			if not set, maximum height is DELTA_MAX_HEIGHT
 * @pm:			(optional) if set, decoder will manage power on its own
 * @open:		open this decoder
 * @close:		close this decoder
 * @setup_frame:	setup frame to be used by decoder, see below
 * @get_streaminfo:	get stream related infos, see below
 * @get_frameinfo:	get decoded frame related infos, see below
 * @set_frameinfo:	(optional) set decoded frame related infos, see below
 * @setup_frame:	setup frame to be used by decoder, see below
 * @decode:		decode a single access unit, see below
 * @get_frame:		get the next decoded frame available, see below
 * @recycle:		recycle the given frame, see below
 * @flush:		(optional) flush decoder, see below
 * @drain:		(optional) drain decoder, see below
 */
struct delta_dec {
	const char *name;
	u32 streamformat;
	u32 pixelformat;
	u32 max_width;
	u32 max_height;
	bool pm;

	/*
	 * decoder ops
	 */
	int (*open)(struct delta_ctx *ctx);
	int (*close)(struct delta_ctx *ctx);

	/*
	 * setup_frame() - setup frame to be used by decoder
	 * @ctx:	(in) instance
	 * @frame:	(in) frame to use
	 *  @frame.index	(in) identifier of frame
	 *  @frame.vaddr	(in) virtual address (kernel can read/write)
	 *  @frame.paddr	(in) physical address (for hardware)
	 *
	 * Frame is to be allocated by caller, then given
	 * to decoder through this call.
	 * Several frames must be given to decoder (dpb),
	 * each frame is identified using its index.
	 */
	int (*setup_frame)(struct delta_ctx *ctx, struct delta_frame *frame);

	/*
	 * get_streaminfo() - get stream related infos
	 * @ctx:	(in) instance
	 * @streaminfo:	(out) width, height, dpb,...
	 *
	 * Precondition: stream header must have been successfully
	 * parsed to have this call successful & @streaminfo valid.
	 * Header parsing must be done using decode(), giving
	 * explicitly header access unit or first access unit of bitstream.
	 * If no valid header is found, get_streaminfo will return -ENODATA,
	 * in this case the next bistream access unit must be decoded till
	 * get_streaminfo becomes successful.
	 */
	int (*get_streaminfo)(struct delta_ctx *ctx,
			      struct delta_streaminfo *streaminfo);

	/*
	 * get_frameinfo() - get decoded frame related infos
	 * @ctx:	(in) instance
	 * @frameinfo:	(out) width, height, alignment, crop, ...
	 *
	 * Precondition: get_streaminfo() must be successful
	 */
	int (*get_frameinfo)(struct delta_ctx *ctx,
			     struct delta_frameinfo *frameinfo);

	/*
	 * set_frameinfo() - set decoded frame related infos
	 * @ctx:	(in) instance
	 * @frameinfo:	(out) width, height, alignment, crop, ...
	 *
	 * Optional.
	 * Typically used to negotiate with decoder the output
	 * frame if decoder can do post-processing.
	 */
	int (*set_frameinfo)(struct delta_ctx *ctx,
			     struct delta_frameinfo *frameinfo);

	/*
	 * decode() - decode a single access unit
	 * @ctx:	(in) instance
	 * @au:		(in/out) access unit
	 *  @au.size	(in) size of au to decode
	 *  @au.vaddr	(in) virtual address (kernel can read/write)
	 *  @au.paddr	(in) physical address (for hardware)
	 *  @au.flags	(out) au type (V4L2_BUF_FLAG_KEYFRAME/
	 *			PFRAME/BFRAME)
	 *
	 * Decode the access unit given. Decode is synchronous;
	 * access unit memory is no more needed after this call.
	 * After this call, none, one or several frames could
	 * have been decoded, which can be retrieved using
	 * get_frame().
	 */
	int (*decode)(struct delta_ctx *ctx, struct delta_au *au);

	/*
	 * get_frame() - get the next decoded frame available
	 * @ctx:	(in) instance
	 * @frame:	(out) frame with decoded data:
	 *  @frame.index	(out) identifier of frame
	 *  @frame.field	(out) field order for interlaced frame
	 *  @frame.state	(out) frame state for frame lifecycle tracking
	 *  @frame.flags	(out) frame type (V4L2_BUF_FLAG_KEYFRAME/
	 *			PFRAME/BFRAME)
	 *
	 * Get the next available decoded frame.
	 * If no frame is available, -ENODATA is returned.
	 * If a frame is available, frame structure is filled with
	 * relevant data, frame.index identifying this exact frame.
	 * When this frame is no more needed by upper layers,
	 * recycle() must be called giving this frame identifier.
	 */
	int (*get_frame)(struct delta_ctx *ctx, struct delta_frame **frame);

	/*
	 * recycle() - recycle the given frame
	 * @ctx:	(in) instance
	 * @frame:	(in) frame to recycle:
	 *  @frame.index	(in) identifier of frame
	 *
	 * recycle() is to be called by user when the decoded frame
	 * is no more needed (composition/display done).
	 * This frame will then be reused by decoder to proceed
	 * with next frame decoding.
	 * If not enough frames have been provided through setup_frame(),
	 * or recycle() is not called fast enough, the decoder can run out
	 * of available frames to proceed with decoding (starvation).
	 * This case is guarded by wq_recycle wait queue which ensures that
	 * decoder is called only if at least one frame is available.
	 */
	int (*recycle)(struct delta_ctx *ctx, struct delta_frame *frame);

	/*
	 * flush() - flush decoder
	 * @ctx:	(in) instance
	 *
	 * Optional.
	 * Reset decoder context and discard all internal buffers.
	 * This allows implementation of seek, which leads to discontinuity
	 * of input bitstream that decoder must know to restart its internal
	 * decoding logic.
	 */
	int (*flush)(struct delta_ctx *ctx);

	/*
	 * drain() - drain decoder
	 * @ctx:	(in) instance
	 *
	 * Optional.
	 * Mark decoder pending frames (decoded but not yet output) as ready
	 * so that they can be output to client at EOS (End Of Stream).
	 * get_frame() is to be called in a loop right after drain() to
	 * get all those pending frames.
	 */
	int (*drain)(struct delta_ctx *ctx);
};

struct delta_dev;

/*
 * struct delta_ctx - instance structure.
 *
 * @flags:		validity of fields (streaminfo)
 * @fh:			V4L2 file handle
 * @dev:		device context
 * @dec:		selected decoder context for this instance
 * @ipc_ctx:		context of IPC communication with firmware
 * @state:		instance state
 * @frame_num:		frame number
 * @au_num:		access unit number
 * @max_au_size:	max size of an access unit
 * @streaminfo:		stream information (width, height, dpb, interlacing...)
 * @frameinfo:		frame information (width, height, format, alignment...)
 * @nb_of_frames:	number of frames available for decoding
 * @frames:		array of decoding frames to keep track of frame
 *			state and manage frame recycling
 * @decoded_frames:	nb of decoded frames from opening
 * @output_frames:	nb of output frames from opening
 * @dropped_frames:	nb of frames dropped (ie access unit not parsed
 *			or frame decoded but not output)
 * @stream_errors:	nb of stream errors (corrupted, not supported, ...)
 * @decode_errors:	nb of decode errors (firmware error)
 * @sys_errors:		nb of system errors (memory, ipc, ...)
 * @dts:		FIFO of decoding timestamp.
 *			output frames are timestamped with incoming access
 *			unit timestamps using this fifo.
 * @name:		string naming this instance (debug purpose)
 * @run_work:		decoding work
 * @lock:		lock for decoding work serialization
 * @aborting:		true if current job aborted
 * @priv:		private decoder context for this instance, allocated
 *			by decoder @open time.
 */
struct delta_ctx {
	u32 flags;
	struct v4l2_fh fh;
	struct delta_dev *dev;
	const struct delta_dec *dec;
	struct delta_ipc_ctx ipc_ctx;

	enum delta_state state;
	u32 frame_num;
	u32 au_num;
	size_t max_au_size;
	struct delta_streaminfo streaminfo;
	struct delta_frameinfo frameinfo;
	u32 nb_of_frames;
	struct delta_frame *frames[DELTA_MAX_FRAMES];
	u32 decoded_frames;
	u32 output_frames;
	u32 dropped_frames;
	u32 stream_errors;
	u32 decode_errors;
	u32 sys_errors;
	struct list_head dts;
	char name[100];
	struct work_struct run_work;
	struct mutex lock;
	bool aborting;
	void *priv;
};

#define DELTA_FLAG_STREAMINFO 0x0001
#define DELTA_FLAG_FRAMEINFO 0x0002

#define DELTA_MAX_FORMATS  DELTA_MAX_DECODERS

/*
 * struct delta_dev - device struct, 1 per probe (so single one for
 * all platform life)
 *
 * @v4l2_dev:		v4l2 device
 * @vdev:		v4l2 video device
 * @pdev:		platform device
 * @dev:		device
 * @m2m_dev:		memory-to-memory V4L2 device
 * @lock:		device lock, for crit section & V4L2 ops serialization.
 * @clk_delta:		delta main clock
 * @clk_st231:		st231 coprocessor main clock
 * @clk_flash_promip:	flash promip clock
 * @decoders:		list of registered decoders
 * @nb_of_decoders:	nb of registered decoders
 * @pixelformats:	supported uncompressed video formats
 * @nb_of_pixelformats:	number of supported umcompressed video formats
 * @streamformats:	supported compressed video formats
 * @nb_of_streamformats:number of supported compressed video formats
 * @instance_id:	rolling counter identifying an instance (debug purpose)
 * @work_queue:		decoding job work queue
 * @rpmsg_driver:	rpmsg IPC driver
 * @rpmsg_device:	rpmsg IPC device
 */
struct delta_dev {
	struct v4l2_device v4l2_dev;
	struct video_device *vdev;
	struct platform_device *pdev;
	struct device *dev;
	struct v4l2_m2m_dev *m2m_dev;
	struct mutex lock;
	struct clk *clk_delta;
	struct clk *clk_st231;
	struct clk *clk_flash_promip;
	const struct delta_dec *decoders[DELTA_MAX_DECODERS];
	u32 nb_of_decoders;
	u32 pixelformats[DELTA_MAX_FORMATS];
	u32 nb_of_pixelformats;
	u32 streamformats[DELTA_MAX_FORMATS];
	u32 nb_of_streamformats;
	u8 instance_id;
	struct workqueue_struct *work_queue;
	struct rpmsg_driver rpmsg_driver;
	struct rpmsg_device *rpmsg_device;
};

static inline char *frame_type_str(u32 flags)
{
	if (flags & V4L2_BUF_FLAG_KEYFRAME)
		return "I";
	if (flags & V4L2_BUF_FLAG_PFRAME)
		return "P";
	if (flags & V4L2_BUF_FLAG_BFRAME)
		return "B";
	if (flags & V4L2_BUF_FLAG_LAST)
		return "EOS";
	return "?";
}

static inline char *frame_field_str(enum v4l2_field field)
{
	if (field == V4L2_FIELD_NONE)
		return "-";
	if (field == V4L2_FIELD_TOP)
		return "T";
	if (field == V4L2_FIELD_BOTTOM)
		return "B";
	if (field == V4L2_FIELD_INTERLACED)
		return "I";
	if (field == V4L2_FIELD_INTERLACED_TB)
		return "TB";
	if (field == V4L2_FIELD_INTERLACED_BT)
		return "BT";
	return "?";
}

static inline char *frame_state_str(u32 state, char *str, unsigned int len)
{
	snprintf(str, len, "%s %s %s %s %s %s",
		 (state & DELTA_FRAME_REF)  ? "ref" : "   ",
		 (state & DELTA_FRAME_BSY)  ? "bsy" : "   ",
		 (state & DELTA_FRAME_DEC)  ? "dec" : "   ",
		 (state & DELTA_FRAME_OUT)  ? "out" : "   ",
		 (state & DELTA_FRAME_M2M)  ? "m2m" : "   ",
		 (state & DELTA_FRAME_RDY)  ? "rdy" : "   ");
	return str;
}

int delta_get_frameinfo_default(struct delta_ctx *ctx,
				struct delta_frameinfo *frameinfo);
int delta_recycle_default(struct delta_ctx *pctx,
			  struct delta_frame *frame);

int delta_get_free_frame(struct delta_ctx *ctx,
			 struct delta_frame **pframe);

int delta_get_sync(struct delta_ctx *ctx);
void delta_put_autosuspend(struct delta_ctx *ctx);

#endif /* DELTA_H */
