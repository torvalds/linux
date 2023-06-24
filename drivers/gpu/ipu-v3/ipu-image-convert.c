// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2016 Mentor Graphics Inc.
 *
 * Queued image conversion support, with tiling and rotation.
 */

#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <video/imx-ipu-image-convert.h>
#include "ipu-prv.h"

/*
 * The IC Resizer has a restriction that the output frame from the
 * resizer must be 1024 or less in both width (pixels) and height
 * (lines).
 *
 * The image converter attempts to split up a conversion when
 * the desired output (converted) frame resolution exceeds the
 * IC resizer limit of 1024 in either dimension.
 *
 * If either dimension of the output frame exceeds the limit, the
 * dimension is split into 1, 2, or 4 equal stripes, for a maximum
 * of 4*4 or 16 tiles. A conversion is then carried out for each
 * tile (but taking care to pass the full frame stride length to
 * the DMA channel's parameter memory!). IDMA double-buffering is used
 * to convert each tile back-to-back when possible (see note below
 * when double_buffering boolean is set).
 *
 * Note that the input frame must be split up into the same number
 * of tiles as the output frame:
 *
 *                       +---------+-----+
 *   +-----+---+         |  A      | B   |
 *   | A   | B |         |         |     |
 *   +-----+---+   -->   +---------+-----+
 *   | C   | D |         |  C      | D   |
 *   +-----+---+         |         |     |
 *                       +---------+-----+
 *
 * Clockwise 90° rotations are handled by first rescaling into a
 * reusable temporary tile buffer and then rotating with the 8x8
 * block rotator, writing to the correct destination:
 *
 *                                         +-----+-----+
 *                                         |     |     |
 *   +-----+---+         +---------+       | C   | A   |
 *   | A   | B |         | A,B, |  |       |     |     |
 *   +-----+---+   -->   | C,D  |  |  -->  |     |     |
 *   | C   | D |         +---------+       +-----+-----+
 *   +-----+---+                           | D   | B   |
 *                                         |     |     |
 *                                         +-----+-----+
 *
 * If the 8x8 block rotator is used, horizontal or vertical flipping
 * is done during the rotation step, otherwise flipping is done
 * during the scaling step.
 * With rotation or flipping, tile order changes between input and
 * output image. Tiles are numbered row major from top left to bottom
 * right for both input and output image.
 */

#define MAX_STRIPES_W    4
#define MAX_STRIPES_H    4
#define MAX_TILES (MAX_STRIPES_W * MAX_STRIPES_H)

#define MIN_W     16
#define MIN_H     8
#define MAX_W     4096
#define MAX_H     4096

enum ipu_image_convert_type {
	IMAGE_CONVERT_IN = 0,
	IMAGE_CONVERT_OUT,
};

struct ipu_image_convert_dma_buf {
	void          *virt;
	dma_addr_t    phys;
	unsigned long len;
};

struct ipu_image_convert_dma_chan {
	int in;
	int out;
	int rot_in;
	int rot_out;
	int vdi_in_p;
	int vdi_in;
	int vdi_in_n;
};

/* dimensions of one tile */
struct ipu_image_tile {
	u32 width;
	u32 height;
	u32 left;
	u32 top;
	/* size and strides are in bytes */
	u32 size;
	u32 stride;
	u32 rot_stride;
	/* start Y or packed offset of this tile */
	u32 offset;
	/* offset from start to tile in U plane, for planar formats */
	u32 u_off;
	/* offset from start to tile in V plane, for planar formats */
	u32 v_off;
};

struct ipu_image_convert_image {
	struct ipu_image base;
	enum ipu_image_convert_type type;

	const struct ipu_image_pixfmt *fmt;
	unsigned int stride;

	/* # of rows (horizontal stripes) if dest height is > 1024 */
	unsigned int num_rows;
	/* # of columns (vertical stripes) if dest width is > 1024 */
	unsigned int num_cols;

	struct ipu_image_tile tile[MAX_TILES];
};

struct ipu_image_pixfmt {
	u32	fourcc;        /* V4L2 fourcc */
	int     bpp;           /* total bpp */
	int     uv_width_dec;  /* decimation in width for U/V planes */
	int     uv_height_dec; /* decimation in height for U/V planes */
	bool    planar;        /* planar format */
	bool    uv_swapped;    /* U and V planes are swapped */
	bool    uv_packed;     /* partial planar (U and V in same plane) */
};

struct ipu_image_convert_ctx;
struct ipu_image_convert_chan;
struct ipu_image_convert_priv;

enum eof_irq_mask {
	EOF_IRQ_IN      = BIT(0),
	EOF_IRQ_ROT_IN  = BIT(1),
	EOF_IRQ_OUT     = BIT(2),
	EOF_IRQ_ROT_OUT = BIT(3),
};

#define EOF_IRQ_COMPLETE (EOF_IRQ_IN | EOF_IRQ_OUT)
#define EOF_IRQ_ROT_COMPLETE (EOF_IRQ_IN | EOF_IRQ_OUT |	\
			      EOF_IRQ_ROT_IN | EOF_IRQ_ROT_OUT)

struct ipu_image_convert_ctx {
	struct ipu_image_convert_chan *chan;

	ipu_image_convert_cb_t complete;
	void *complete_context;

	/* Source/destination image data and rotation mode */
	struct ipu_image_convert_image in;
	struct ipu_image_convert_image out;
	struct ipu_ic_csc csc;
	enum ipu_rotate_mode rot_mode;
	u32 downsize_coeff_h;
	u32 downsize_coeff_v;
	u32 image_resize_coeff_h;
	u32 image_resize_coeff_v;
	u32 resize_coeffs_h[MAX_STRIPES_W];
	u32 resize_coeffs_v[MAX_STRIPES_H];

	/* intermediate buffer for rotation */
	struct ipu_image_convert_dma_buf rot_intermediate[2];

	/* current buffer number for double buffering */
	int cur_buf_num;

	bool aborting;
	struct completion aborted;

	/* can we use double-buffering for this conversion operation? */
	bool double_buffering;
	/* num_rows * num_cols */
	unsigned int num_tiles;
	/* next tile to process */
	unsigned int next_tile;
	/* where to place converted tile in dest image */
	unsigned int out_tile_map[MAX_TILES];

	/* mask of completed EOF irqs at every tile conversion */
	enum eof_irq_mask eof_mask;

	struct list_head list;
};

struct ipu_image_convert_chan {
	struct ipu_image_convert_priv *priv;

	enum ipu_ic_task ic_task;
	const struct ipu_image_convert_dma_chan *dma_ch;

	struct ipu_ic *ic;
	struct ipuv3_channel *in_chan;
	struct ipuv3_channel *out_chan;
	struct ipuv3_channel *rotation_in_chan;
	struct ipuv3_channel *rotation_out_chan;

	/* the IPU end-of-frame irqs */
	int in_eof_irq;
	int rot_in_eof_irq;
	int out_eof_irq;
	int rot_out_eof_irq;

	spinlock_t irqlock;

	/* list of convert contexts */
	struct list_head ctx_list;
	/* queue of conversion runs */
	struct list_head pending_q;
	/* queue of completed runs */
	struct list_head done_q;

	/* the current conversion run */
	struct ipu_image_convert_run *current_run;
};

struct ipu_image_convert_priv {
	struct ipu_image_convert_chan chan[IC_NUM_TASKS];
	struct ipu_soc *ipu;
};

static const struct ipu_image_convert_dma_chan
image_convert_dma_chan[IC_NUM_TASKS] = {
	[IC_TASK_VIEWFINDER] = {
		.in = IPUV3_CHANNEL_MEM_IC_PRP_VF,
		.out = IPUV3_CHANNEL_IC_PRP_VF_MEM,
		.rot_in = IPUV3_CHANNEL_MEM_ROT_VF,
		.rot_out = IPUV3_CHANNEL_ROT_VF_MEM,
		.vdi_in_p = IPUV3_CHANNEL_MEM_VDI_PREV,
		.vdi_in = IPUV3_CHANNEL_MEM_VDI_CUR,
		.vdi_in_n = IPUV3_CHANNEL_MEM_VDI_NEXT,
	},
	[IC_TASK_POST_PROCESSOR] = {
		.in = IPUV3_CHANNEL_MEM_IC_PP,
		.out = IPUV3_CHANNEL_IC_PP_MEM,
		.rot_in = IPUV3_CHANNEL_MEM_ROT_PP,
		.rot_out = IPUV3_CHANNEL_ROT_PP_MEM,
	},
};

static const struct ipu_image_pixfmt image_convert_formats[] = {
	{
		.fourcc	= V4L2_PIX_FMT_RGB565,
		.bpp    = 16,
	}, {
		.fourcc	= V4L2_PIX_FMT_RGB24,
		.bpp    = 24,
	}, {
		.fourcc	= V4L2_PIX_FMT_BGR24,
		.bpp    = 24,
	}, {
		.fourcc	= V4L2_PIX_FMT_RGB32,
		.bpp    = 32,
	}, {
		.fourcc	= V4L2_PIX_FMT_BGR32,
		.bpp    = 32,
	}, {
		.fourcc	= V4L2_PIX_FMT_XRGB32,
		.bpp    = 32,
	}, {
		.fourcc	= V4L2_PIX_FMT_XBGR32,
		.bpp    = 32,
	}, {
		.fourcc	= V4L2_PIX_FMT_BGRX32,
		.bpp    = 32,
	}, {
		.fourcc	= V4L2_PIX_FMT_RGBX32,
		.bpp    = 32,
	}, {
		.fourcc	= V4L2_PIX_FMT_YUYV,
		.bpp    = 16,
		.uv_width_dec = 2,
		.uv_height_dec = 1,
	}, {
		.fourcc	= V4L2_PIX_FMT_UYVY,
		.bpp    = 16,
		.uv_width_dec = 2,
		.uv_height_dec = 1,
	}, {
		.fourcc	= V4L2_PIX_FMT_YUV420,
		.bpp    = 12,
		.planar = true,
		.uv_width_dec = 2,
		.uv_height_dec = 2,
	}, {
		.fourcc	= V4L2_PIX_FMT_YVU420,
		.bpp    = 12,
		.planar = true,
		.uv_width_dec = 2,
		.uv_height_dec = 2,
		.uv_swapped = true,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.bpp    = 12,
		.planar = true,
		.uv_width_dec = 2,
		.uv_height_dec = 2,
		.uv_packed = true,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.bpp    = 16,
		.planar = true,
		.uv_width_dec = 2,
		.uv_height_dec = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.bpp    = 16,
		.planar = true,
		.uv_width_dec = 2,
		.uv_height_dec = 1,
		.uv_packed = true,
	},
};

static const struct ipu_image_pixfmt *get_format(u32 fourcc)
{
	const struct ipu_image_pixfmt *ret = NULL;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(image_convert_formats); i++) {
		if (image_convert_formats[i].fourcc == fourcc) {
			ret = &image_convert_formats[i];
			break;
		}
	}

	return ret;
}

static void dump_format(struct ipu_image_convert_ctx *ctx,
			struct ipu_image_convert_image *ic_image)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;

	dev_dbg(priv->ipu->dev,
		"task %u: ctx %p: %s format: %dx%d (%dx%d tiles), %c%c%c%c\n",
		chan->ic_task, ctx,
		ic_image->type == IMAGE_CONVERT_OUT ? "Output" : "Input",
		ic_image->base.pix.width, ic_image->base.pix.height,
		ic_image->num_cols, ic_image->num_rows,
		ic_image->fmt->fourcc & 0xff,
		(ic_image->fmt->fourcc >> 8) & 0xff,
		(ic_image->fmt->fourcc >> 16) & 0xff,
		(ic_image->fmt->fourcc >> 24) & 0xff);
}

int ipu_image_convert_enum_format(int index, u32 *fourcc)
{
	const struct ipu_image_pixfmt *fmt;

	if (index >= (int)ARRAY_SIZE(image_convert_formats))
		return -EINVAL;

	/* Format found */
	fmt = &image_convert_formats[index];
	*fourcc = fmt->fourcc;
	return 0;
}
EXPORT_SYMBOL_GPL(ipu_image_convert_enum_format);

static void free_dma_buf(struct ipu_image_convert_priv *priv,
			 struct ipu_image_convert_dma_buf *buf)
{
	if (buf->virt)
		dma_free_coherent(priv->ipu->dev,
				  buf->len, buf->virt, buf->phys);
	buf->virt = NULL;
	buf->phys = 0;
}

static int alloc_dma_buf(struct ipu_image_convert_priv *priv,
			 struct ipu_image_convert_dma_buf *buf,
			 int size)
{
	buf->len = PAGE_ALIGN(size);
	buf->virt = dma_alloc_coherent(priv->ipu->dev, buf->len, &buf->phys,
				       GFP_DMA | GFP_KERNEL);
	if (!buf->virt) {
		dev_err(priv->ipu->dev, "failed to alloc dma buffer\n");
		return -ENOMEM;
	}

	return 0;
}

static inline int num_stripes(int dim)
{
	return (dim - 1) / 1024 + 1;
}

/*
 * Calculate downsizing coefficients, which are the same for all tiles,
 * and initial bilinear resizing coefficients, which are used to find the
 * best seam positions.
 * Also determine the number of tiles necessary to guarantee that no tile
 * is larger than 1024 pixels in either dimension at the output and between
 * IC downsizing and main processing sections.
 */
static int calc_image_resize_coefficients(struct ipu_image_convert_ctx *ctx,
					  struct ipu_image *in,
					  struct ipu_image *out)
{
	u32 downsized_width = in->rect.width;
	u32 downsized_height = in->rect.height;
	u32 downsize_coeff_v = 0;
	u32 downsize_coeff_h = 0;
	u32 resized_width = out->rect.width;
	u32 resized_height = out->rect.height;
	u32 resize_coeff_h;
	u32 resize_coeff_v;
	u32 cols;
	u32 rows;

	if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
		resized_width = out->rect.height;
		resized_height = out->rect.width;
	}

	/* Do not let invalid input lead to an endless loop below */
	if (WARN_ON(resized_width == 0 || resized_height == 0))
		return -EINVAL;

	while (downsized_width >= resized_width * 2) {
		downsized_width >>= 1;
		downsize_coeff_h++;
	}

	while (downsized_height >= resized_height * 2) {
		downsized_height >>= 1;
		downsize_coeff_v++;
	}

	/*
	 * Calculate the bilinear resizing coefficients that could be used if
	 * we were converting with a single tile. The bottom right output pixel
	 * should sample as close as possible to the bottom right input pixel
	 * out of the decimator, but not overshoot it:
	 */
	resize_coeff_h = 8192 * (downsized_width - 1) / (resized_width - 1);
	resize_coeff_v = 8192 * (downsized_height - 1) / (resized_height - 1);

	/*
	 * Both the output of the IC downsizing section before being passed to
	 * the IC main processing section and the final output of the IC main
	 * processing section must be <= 1024 pixels in both dimensions.
	 */
	cols = num_stripes(max_t(u32, downsized_width, resized_width));
	rows = num_stripes(max_t(u32, downsized_height, resized_height));

	dev_dbg(ctx->chan->priv->ipu->dev,
		"%s: hscale: >>%u, *8192/%u vscale: >>%u, *8192/%u, %ux%u tiles\n",
		__func__, downsize_coeff_h, resize_coeff_h, downsize_coeff_v,
		resize_coeff_v, cols, rows);

	if (downsize_coeff_h > 2 || downsize_coeff_v  > 2 ||
	    resize_coeff_h > 0x3fff || resize_coeff_v > 0x3fff)
		return -EINVAL;

	ctx->downsize_coeff_h = downsize_coeff_h;
	ctx->downsize_coeff_v = downsize_coeff_v;
	ctx->image_resize_coeff_h = resize_coeff_h;
	ctx->image_resize_coeff_v = resize_coeff_v;
	ctx->in.num_cols = cols;
	ctx->in.num_rows = rows;

	return 0;
}

#define round_closest(x, y) round_down((x) + (y)/2, (y))

/*
 * Find the best aligned seam position for the given column / row index.
 * Rotation and image offsets are out of scope.
 *
 * @index: column / row index, used to calculate valid interval
 * @in_edge: input right / bottom edge
 * @out_edge: output right / bottom edge
 * @in_align: input alignment, either horizontal 8-byte line start address
 *            alignment, or pixel alignment due to image format
 * @out_align: output alignment, either horizontal 8-byte line start address
 *             alignment, or pixel alignment due to image format or rotator
 *             block size
 * @in_burst: horizontal input burst size in case of horizontal flip
 * @out_burst: horizontal output burst size or rotator block size
 * @downsize_coeff: downsizing section coefficient
 * @resize_coeff: main processing section resizing coefficient
 * @_in_seam: aligned input seam position return value
 * @_out_seam: aligned output seam position return value
 */
static void find_best_seam(struct ipu_image_convert_ctx *ctx,
			   unsigned int index,
			   unsigned int in_edge,
			   unsigned int out_edge,
			   unsigned int in_align,
			   unsigned int out_align,
			   unsigned int in_burst,
			   unsigned int out_burst,
			   unsigned int downsize_coeff,
			   unsigned int resize_coeff,
			   u32 *_in_seam,
			   u32 *_out_seam)
{
	struct device *dev = ctx->chan->priv->ipu->dev;
	unsigned int out_pos;
	/* Input / output seam position candidates */
	unsigned int out_seam = 0;
	unsigned int in_seam = 0;
	unsigned int min_diff = UINT_MAX;
	unsigned int out_start;
	unsigned int out_end;
	unsigned int in_start;
	unsigned int in_end;

	/* Start within 1024 pixels of the right / bottom edge */
	out_start = max_t(int, index * out_align, out_edge - 1024);
	/* End before having to add more columns to the left / rows above */
	out_end = min_t(unsigned int, out_edge, index * 1024 + 1);

	/*
	 * Limit input seam position to make sure that the downsized input tile
	 * to the right or bottom does not exceed 1024 pixels.
	 */
	in_start = max_t(int, index * in_align,
			 in_edge - (1024 << downsize_coeff));
	in_end = min_t(unsigned int, in_edge,
		       index * (1024 << downsize_coeff) + 1);

	/*
	 * Output tiles must start at a multiple of 8 bytes horizontally and
	 * possibly at an even line horizontally depending on the pixel format.
	 * Only consider output aligned positions for the seam.
	 */
	out_start = round_up(out_start, out_align);
	for (out_pos = out_start; out_pos < out_end; out_pos += out_align) {
		unsigned int in_pos;
		unsigned int in_pos_aligned;
		unsigned int in_pos_rounded;
		unsigned int abs_diff;

		/*
		 * Tiles in the right row / bottom column may not be allowed to
		 * overshoot horizontally / vertically. out_burst may be the
		 * actual DMA burst size, or the rotator block size.
		 */
		if ((out_burst > 1) && (out_edge - out_pos) % out_burst)
			continue;

		/*
		 * Input sample position, corresponding to out_pos, 19.13 fixed
		 * point.
		 */
		in_pos = (out_pos * resize_coeff) << downsize_coeff;
		/*
		 * The closest input sample position that we could actually
		 * start the input tile at, 19.13 fixed point.
		 */
		in_pos_aligned = round_closest(in_pos, 8192U * in_align);
		/* Convert 19.13 fixed point to integer */
		in_pos_rounded = in_pos_aligned / 8192U;

		if (in_pos_rounded < in_start)
			continue;
		if (in_pos_rounded >= in_end)
			break;

		if ((in_burst > 1) &&
		    (in_edge - in_pos_rounded) % in_burst)
			continue;

		if (in_pos < in_pos_aligned)
			abs_diff = in_pos_aligned - in_pos;
		else
			abs_diff = in_pos - in_pos_aligned;

		if (abs_diff < min_diff) {
			in_seam = in_pos_rounded;
			out_seam = out_pos;
			min_diff = abs_diff;
		}
	}

	*_out_seam = out_seam;
	*_in_seam = in_seam;

	dev_dbg(dev, "%s: out_seam %u(%u) in [%u, %u], in_seam %u(%u) in [%u, %u] diff %u.%03u\n",
		__func__, out_seam, out_align, out_start, out_end,
		in_seam, in_align, in_start, in_end, min_diff / 8192,
		DIV_ROUND_CLOSEST(min_diff % 8192 * 1000, 8192));
}

/*
 * Tile left edges are required to be aligned to multiples of 8 bytes
 * by the IDMAC.
 */
static inline u32 tile_left_align(const struct ipu_image_pixfmt *fmt)
{
	if (fmt->planar)
		return fmt->uv_packed ? 8 : 8 * fmt->uv_width_dec;
	else
		return fmt->bpp == 32 ? 2 : fmt->bpp == 16 ? 4 : 8;
}

/*
 * Tile top edge alignment is only limited by chroma subsampling.
 */
static inline u32 tile_top_align(const struct ipu_image_pixfmt *fmt)
{
	return fmt->uv_height_dec > 1 ? 2 : 1;
}

static inline u32 tile_width_align(enum ipu_image_convert_type type,
				   const struct ipu_image_pixfmt *fmt,
				   enum ipu_rotate_mode rot_mode)
{
	if (type == IMAGE_CONVERT_IN) {
		/*
		 * The IC burst reads 8 pixels at a time. Reading beyond the
		 * end of the line is usually acceptable. Those pixels are
		 * ignored, unless the IC has to write the scaled line in
		 * reverse.
		 */
		return (!ipu_rot_mode_is_irt(rot_mode) &&
			(rot_mode & IPU_ROT_BIT_HFLIP)) ? 8 : 2;
	}

	/*
	 * Align to 16x16 pixel blocks for planar 4:2:0 chroma subsampled
	 * formats to guarantee 8-byte aligned line start addresses in the
	 * chroma planes when IRT is used. Align to 8x8 pixel IRT block size
	 * for all other formats.
	 */
	return (ipu_rot_mode_is_irt(rot_mode) &&
		fmt->planar && !fmt->uv_packed) ?
		8 * fmt->uv_width_dec : 8;
}

static inline u32 tile_height_align(enum ipu_image_convert_type type,
				    const struct ipu_image_pixfmt *fmt,
				    enum ipu_rotate_mode rot_mode)
{
	if (type == IMAGE_CONVERT_IN || !ipu_rot_mode_is_irt(rot_mode))
		return 2;

	/*
	 * Align to 16x16 pixel blocks for planar 4:2:0 chroma subsampled
	 * formats to guarantee 8-byte aligned line start addresses in the
	 * chroma planes when IRT is used. Align to 8x8 pixel IRT block size
	 * for all other formats.
	 */
	return (fmt->planar && !fmt->uv_packed) ? 8 * fmt->uv_width_dec : 8;
}

/*
 * Fill in left position and width and for all tiles in an input column, and
 * for all corresponding output tiles. If the 90° rotator is used, the output
 * tiles are in a row, and output tile top position and height are set.
 */
static void fill_tile_column(struct ipu_image_convert_ctx *ctx,
			     unsigned int col,
			     struct ipu_image_convert_image *in,
			     unsigned int in_left, unsigned int in_width,
			     struct ipu_image_convert_image *out,
			     unsigned int out_left, unsigned int out_width)
{
	unsigned int row, tile_idx;
	struct ipu_image_tile *in_tile, *out_tile;

	for (row = 0; row < in->num_rows; row++) {
		tile_idx = in->num_cols * row + col;
		in_tile = &in->tile[tile_idx];
		out_tile = &out->tile[ctx->out_tile_map[tile_idx]];

		in_tile->left = in_left;
		in_tile->width = in_width;

		if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
			out_tile->top = out_left;
			out_tile->height = out_width;
		} else {
			out_tile->left = out_left;
			out_tile->width = out_width;
		}
	}
}

/*
 * Fill in top position and height and for all tiles in an input row, and
 * for all corresponding output tiles. If the 90° rotator is used, the output
 * tiles are in a column, and output tile left position and width are set.
 */
static void fill_tile_row(struct ipu_image_convert_ctx *ctx, unsigned int row,
			  struct ipu_image_convert_image *in,
			  unsigned int in_top, unsigned int in_height,
			  struct ipu_image_convert_image *out,
			  unsigned int out_top, unsigned int out_height)
{
	unsigned int col, tile_idx;
	struct ipu_image_tile *in_tile, *out_tile;

	for (col = 0; col < in->num_cols; col++) {
		tile_idx = in->num_cols * row + col;
		in_tile = &in->tile[tile_idx];
		out_tile = &out->tile[ctx->out_tile_map[tile_idx]];

		in_tile->top = in_top;
		in_tile->height = in_height;

		if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
			out_tile->left = out_top;
			out_tile->width = out_height;
		} else {
			out_tile->top = out_top;
			out_tile->height = out_height;
		}
	}
}

/*
 * Find the best horizontal and vertical seam positions to split into tiles.
 * Minimize the fractional part of the input sampling position for the
 * top / left pixels of each tile.
 */
static void find_seams(struct ipu_image_convert_ctx *ctx,
		       struct ipu_image_convert_image *in,
		       struct ipu_image_convert_image *out)
{
	struct device *dev = ctx->chan->priv->ipu->dev;
	unsigned int resized_width = out->base.rect.width;
	unsigned int resized_height = out->base.rect.height;
	unsigned int col;
	unsigned int row;
	unsigned int in_left_align = tile_left_align(in->fmt);
	unsigned int in_top_align = tile_top_align(in->fmt);
	unsigned int out_left_align = tile_left_align(out->fmt);
	unsigned int out_top_align = tile_top_align(out->fmt);
	unsigned int out_width_align = tile_width_align(out->type, out->fmt,
							ctx->rot_mode);
	unsigned int out_height_align = tile_height_align(out->type, out->fmt,
							  ctx->rot_mode);
	unsigned int in_right = in->base.rect.width;
	unsigned int in_bottom = in->base.rect.height;
	unsigned int out_right = out->base.rect.width;
	unsigned int out_bottom = out->base.rect.height;
	unsigned int flipped_out_left;
	unsigned int flipped_out_top;

	if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
		/* Switch width/height and align top left to IRT block size */
		resized_width = out->base.rect.height;
		resized_height = out->base.rect.width;
		out_left_align = out_height_align;
		out_top_align = out_width_align;
		out_width_align = out_left_align;
		out_height_align = out_top_align;
		out_right = out->base.rect.height;
		out_bottom = out->base.rect.width;
	}

	for (col = in->num_cols - 1; col > 0; col--) {
		bool allow_in_overshoot = ipu_rot_mode_is_irt(ctx->rot_mode) ||
					  !(ctx->rot_mode & IPU_ROT_BIT_HFLIP);
		bool allow_out_overshoot = (col < in->num_cols - 1) &&
					   !(ctx->rot_mode & IPU_ROT_BIT_HFLIP);
		unsigned int in_left;
		unsigned int out_left;

		/*
		 * Align input width to burst length if the scaling step flips
		 * horizontally.
		 */

		find_best_seam(ctx, col,
			       in_right, out_right,
			       in_left_align, out_left_align,
			       allow_in_overshoot ? 1 : 8 /* burst length */,
			       allow_out_overshoot ? 1 : out_width_align,
			       ctx->downsize_coeff_h, ctx->image_resize_coeff_h,
			       &in_left, &out_left);

		if (ctx->rot_mode & IPU_ROT_BIT_HFLIP)
			flipped_out_left = resized_width - out_right;
		else
			flipped_out_left = out_left;

		fill_tile_column(ctx, col, in, in_left, in_right - in_left,
				 out, flipped_out_left, out_right - out_left);

		dev_dbg(dev, "%s: col %u: %u, %u -> %u, %u\n", __func__, col,
			in_left, in_right - in_left,
			flipped_out_left, out_right - out_left);

		in_right = in_left;
		out_right = out_left;
	}

	flipped_out_left = (ctx->rot_mode & IPU_ROT_BIT_HFLIP) ?
			   resized_width - out_right : 0;

	fill_tile_column(ctx, 0, in, 0, in_right,
			 out, flipped_out_left, out_right);

	dev_dbg(dev, "%s: col 0: 0, %u -> %u, %u\n", __func__,
		in_right, flipped_out_left, out_right);

	for (row = in->num_rows - 1; row > 0; row--) {
		bool allow_overshoot = row < in->num_rows - 1;
		unsigned int in_top;
		unsigned int out_top;

		find_best_seam(ctx, row,
			       in_bottom, out_bottom,
			       in_top_align, out_top_align,
			       1, allow_overshoot ? 1 : out_height_align,
			       ctx->downsize_coeff_v, ctx->image_resize_coeff_v,
			       &in_top, &out_top);

		if ((ctx->rot_mode & IPU_ROT_BIT_VFLIP) ^
		    ipu_rot_mode_is_irt(ctx->rot_mode))
			flipped_out_top = resized_height - out_bottom;
		else
			flipped_out_top = out_top;

		fill_tile_row(ctx, row, in, in_top, in_bottom - in_top,
			      out, flipped_out_top, out_bottom - out_top);

		dev_dbg(dev, "%s: row %u: %u, %u -> %u, %u\n", __func__, row,
			in_top, in_bottom - in_top,
			flipped_out_top, out_bottom - out_top);

		in_bottom = in_top;
		out_bottom = out_top;
	}

	if ((ctx->rot_mode & IPU_ROT_BIT_VFLIP) ^
	    ipu_rot_mode_is_irt(ctx->rot_mode))
		flipped_out_top = resized_height - out_bottom;
	else
		flipped_out_top = 0;

	fill_tile_row(ctx, 0, in, 0, in_bottom,
		      out, flipped_out_top, out_bottom);

	dev_dbg(dev, "%s: row 0: 0, %u -> %u, %u\n", __func__,
		in_bottom, flipped_out_top, out_bottom);
}

static int calc_tile_dimensions(struct ipu_image_convert_ctx *ctx,
				struct ipu_image_convert_image *image)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;
	unsigned int max_width = 1024;
	unsigned int max_height = 1024;
	unsigned int i;

	if (image->type == IMAGE_CONVERT_IN) {
		/* Up to 4096x4096 input tile size */
		max_width <<= ctx->downsize_coeff_h;
		max_height <<= ctx->downsize_coeff_v;
	}

	for (i = 0; i < ctx->num_tiles; i++) {
		struct ipu_image_tile *tile;
		const unsigned int row = i / image->num_cols;
		const unsigned int col = i % image->num_cols;

		if (image->type == IMAGE_CONVERT_OUT)
			tile = &image->tile[ctx->out_tile_map[i]];
		else
			tile = &image->tile[i];

		tile->size = ((tile->height * image->fmt->bpp) >> 3) *
			tile->width;

		if (image->fmt->planar) {
			tile->stride = tile->width;
			tile->rot_stride = tile->height;
		} else {
			tile->stride =
				(image->fmt->bpp * tile->width) >> 3;
			tile->rot_stride =
				(image->fmt->bpp * tile->height) >> 3;
		}

		dev_dbg(priv->ipu->dev,
			"task %u: ctx %p: %s@[%u,%u]: %ux%u@%u,%u\n",
			chan->ic_task, ctx,
			image->type == IMAGE_CONVERT_IN ? "Input" : "Output",
			row, col,
			tile->width, tile->height, tile->left, tile->top);

		if (!tile->width || tile->width > max_width ||
		    !tile->height || tile->height > max_height) {
			dev_err(priv->ipu->dev, "invalid %s tile size: %ux%u\n",
				image->type == IMAGE_CONVERT_IN ? "input" :
				"output", tile->width, tile->height);
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Use the rotation transformation to find the tile coordinates
 * (row, col) of a tile in the destination frame that corresponds
 * to the given tile coordinates of a source frame. The destination
 * coordinate is then converted to a tile index.
 */
static int transform_tile_index(struct ipu_image_convert_ctx *ctx,
				int src_row, int src_col)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;
	struct ipu_image_convert_image *s_image = &ctx->in;
	struct ipu_image_convert_image *d_image = &ctx->out;
	int dst_row, dst_col;

	/* with no rotation it's a 1:1 mapping */
	if (ctx->rot_mode == IPU_ROTATE_NONE)
		return src_row * s_image->num_cols + src_col;

	/*
	 * before doing the transform, first we have to translate
	 * source row,col for an origin in the center of s_image
	 */
	src_row = src_row * 2 - (s_image->num_rows - 1);
	src_col = src_col * 2 - (s_image->num_cols - 1);

	/* do the rotation transform */
	if (ctx->rot_mode & IPU_ROT_BIT_90) {
		dst_col = -src_row;
		dst_row = src_col;
	} else {
		dst_col = src_col;
		dst_row = src_row;
	}

	/* apply flip */
	if (ctx->rot_mode & IPU_ROT_BIT_HFLIP)
		dst_col = -dst_col;
	if (ctx->rot_mode & IPU_ROT_BIT_VFLIP)
		dst_row = -dst_row;

	dev_dbg(priv->ipu->dev, "task %u: ctx %p: [%d,%d] --> [%d,%d]\n",
		chan->ic_task, ctx, src_col, src_row, dst_col, dst_row);

	/*
	 * finally translate dest row,col using an origin in upper
	 * left of d_image
	 */
	dst_row += d_image->num_rows - 1;
	dst_col += d_image->num_cols - 1;
	dst_row /= 2;
	dst_col /= 2;

	return dst_row * d_image->num_cols + dst_col;
}

/*
 * Fill the out_tile_map[] with transformed destination tile indeces.
 */
static void calc_out_tile_map(struct ipu_image_convert_ctx *ctx)
{
	struct ipu_image_convert_image *s_image = &ctx->in;
	unsigned int row, col, tile = 0;

	for (row = 0; row < s_image->num_rows; row++) {
		for (col = 0; col < s_image->num_cols; col++) {
			ctx->out_tile_map[tile] =
				transform_tile_index(ctx, row, col);
			tile++;
		}
	}
}

static int calc_tile_offsets_planar(struct ipu_image_convert_ctx *ctx,
				    struct ipu_image_convert_image *image)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;
	const struct ipu_image_pixfmt *fmt = image->fmt;
	unsigned int row, col, tile = 0;
	u32 H, top, y_stride, uv_stride;
	u32 uv_row_off, uv_col_off, uv_off, u_off, v_off;
	u32 y_row_off, y_col_off, y_off;
	u32 y_size, uv_size;

	/* setup some convenience vars */
	H = image->base.pix.height;

	y_stride = image->stride;
	uv_stride = y_stride / fmt->uv_width_dec;
	if (fmt->uv_packed)
		uv_stride *= 2;

	y_size = H * y_stride;
	uv_size = y_size / (fmt->uv_width_dec * fmt->uv_height_dec);

	for (row = 0; row < image->num_rows; row++) {
		top = image->tile[tile].top;
		y_row_off = top * y_stride;
		uv_row_off = (top * uv_stride) / fmt->uv_height_dec;

		for (col = 0; col < image->num_cols; col++) {
			y_col_off = image->tile[tile].left;
			uv_col_off = y_col_off / fmt->uv_width_dec;
			if (fmt->uv_packed)
				uv_col_off *= 2;

			y_off = y_row_off + y_col_off;
			uv_off = uv_row_off + uv_col_off;

			u_off = y_size - y_off + uv_off;
			v_off = (fmt->uv_packed) ? 0 : u_off + uv_size;
			if (fmt->uv_swapped)
				swap(u_off, v_off);

			image->tile[tile].offset = y_off;
			image->tile[tile].u_off = u_off;
			image->tile[tile++].v_off = v_off;

			if ((y_off & 0x7) || (u_off & 0x7) || (v_off & 0x7)) {
				dev_err(priv->ipu->dev,
					"task %u: ctx %p: %s@[%d,%d]: "
					"y_off %08x, u_off %08x, v_off %08x\n",
					chan->ic_task, ctx,
					image->type == IMAGE_CONVERT_IN ?
					"Input" : "Output", row, col,
					y_off, u_off, v_off);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int calc_tile_offsets_packed(struct ipu_image_convert_ctx *ctx,
				    struct ipu_image_convert_image *image)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;
	const struct ipu_image_pixfmt *fmt = image->fmt;
	unsigned int row, col, tile = 0;
	u32 bpp, stride, offset;
	u32 row_off, col_off;

	/* setup some convenience vars */
	stride = image->stride;
	bpp = fmt->bpp;

	for (row = 0; row < image->num_rows; row++) {
		row_off = image->tile[tile].top * stride;

		for (col = 0; col < image->num_cols; col++) {
			col_off = (image->tile[tile].left * bpp) >> 3;

			offset = row_off + col_off;

			image->tile[tile].offset = offset;
			image->tile[tile].u_off = 0;
			image->tile[tile++].v_off = 0;

			if (offset & 0x7) {
				dev_err(priv->ipu->dev,
					"task %u: ctx %p: %s@[%d,%d]: "
					"phys %08x\n",
					chan->ic_task, ctx,
					image->type == IMAGE_CONVERT_IN ?
					"Input" : "Output", row, col,
					row_off + col_off);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int calc_tile_offsets(struct ipu_image_convert_ctx *ctx,
			      struct ipu_image_convert_image *image)
{
	if (image->fmt->planar)
		return calc_tile_offsets_planar(ctx, image);

	return calc_tile_offsets_packed(ctx, image);
}

/*
 * Calculate the resizing ratio for the IC main processing section given input
 * size, fixed downsizing coefficient, and output size.
 * Either round to closest for the next tile's first pixel to minimize seams
 * and distortion (for all but right column / bottom row), or round down to
 * avoid sampling beyond the edges of the input image for this tile's last
 * pixel.
 * Returns the resizing coefficient, resizing ratio is 8192.0 / resize_coeff.
 */
static u32 calc_resize_coeff(u32 input_size, u32 downsize_coeff,
			     u32 output_size, bool allow_overshoot)
{
	u32 downsized = input_size >> downsize_coeff;

	if (allow_overshoot)
		return DIV_ROUND_CLOSEST(8192 * downsized, output_size);
	else
		return 8192 * (downsized - 1) / (output_size - 1);
}

/*
 * Slightly modify resize coefficients per tile to hide the bilinear
 * interpolator reset at tile borders, shifting the right / bottom edge
 * by up to a half input pixel. This removes noticeable seams between
 * tiles at higher upscaling factors.
 */
static void calc_tile_resize_coefficients(struct ipu_image_convert_ctx *ctx)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;
	struct ipu_image_tile *in_tile, *out_tile;
	unsigned int col, row, tile_idx;
	unsigned int last_output;

	for (col = 0; col < ctx->in.num_cols; col++) {
		bool closest = (col < ctx->in.num_cols - 1) &&
			       !(ctx->rot_mode & IPU_ROT_BIT_HFLIP);
		u32 resized_width;
		u32 resize_coeff_h;
		u32 in_width;

		tile_idx = col;
		in_tile = &ctx->in.tile[tile_idx];
		out_tile = &ctx->out.tile[ctx->out_tile_map[tile_idx]];

		if (ipu_rot_mode_is_irt(ctx->rot_mode))
			resized_width = out_tile->height;
		else
			resized_width = out_tile->width;

		resize_coeff_h = calc_resize_coeff(in_tile->width,
						   ctx->downsize_coeff_h,
						   resized_width, closest);

		dev_dbg(priv->ipu->dev, "%s: column %u hscale: *8192/%u\n",
			__func__, col, resize_coeff_h);

		/*
		 * With the horizontal scaling factor known, round up resized
		 * width (output width or height) to burst size.
		 */
		resized_width = round_up(resized_width, 8);

		/*
		 * Calculate input width from the last accessed input pixel
		 * given resized width and scaling coefficients. Round up to
		 * burst size.
		 */
		last_output = resized_width - 1;
		if (closest && ((last_output * resize_coeff_h) % 8192))
			last_output++;
		in_width = round_up(
			(DIV_ROUND_UP(last_output * resize_coeff_h, 8192) + 1)
			<< ctx->downsize_coeff_h, 8);

		for (row = 0; row < ctx->in.num_rows; row++) {
			tile_idx = row * ctx->in.num_cols + col;
			in_tile = &ctx->in.tile[tile_idx];
			out_tile = &ctx->out.tile[ctx->out_tile_map[tile_idx]];

			if (ipu_rot_mode_is_irt(ctx->rot_mode))
				out_tile->height = resized_width;
			else
				out_tile->width = resized_width;

			in_tile->width = in_width;
		}

		ctx->resize_coeffs_h[col] = resize_coeff_h;
	}

	for (row = 0; row < ctx->in.num_rows; row++) {
		bool closest = (row < ctx->in.num_rows - 1) &&
			       !(ctx->rot_mode & IPU_ROT_BIT_VFLIP);
		u32 resized_height;
		u32 resize_coeff_v;
		u32 in_height;

		tile_idx = row * ctx->in.num_cols;
		in_tile = &ctx->in.tile[tile_idx];
		out_tile = &ctx->out.tile[ctx->out_tile_map[tile_idx]];

		if (ipu_rot_mode_is_irt(ctx->rot_mode))
			resized_height = out_tile->width;
		else
			resized_height = out_tile->height;

		resize_coeff_v = calc_resize_coeff(in_tile->height,
						   ctx->downsize_coeff_v,
						   resized_height, closest);

		dev_dbg(priv->ipu->dev, "%s: row %u vscale: *8192/%u\n",
			__func__, row, resize_coeff_v);

		/*
		 * With the vertical scaling factor known, round up resized
		 * height (output width or height) to IDMAC limitations.
		 */
		resized_height = round_up(resized_height, 2);

		/*
		 * Calculate input width from the last accessed input pixel
		 * given resized height and scaling coefficients. Align to
		 * IDMAC restrictions.
		 */
		last_output = resized_height - 1;
		if (closest && ((last_output * resize_coeff_v) % 8192))
			last_output++;
		in_height = round_up(
			(DIV_ROUND_UP(last_output * resize_coeff_v, 8192) + 1)
			<< ctx->downsize_coeff_v, 2);

		for (col = 0; col < ctx->in.num_cols; col++) {
			tile_idx = row * ctx->in.num_cols + col;
			in_tile = &ctx->in.tile[tile_idx];
			out_tile = &ctx->out.tile[ctx->out_tile_map[tile_idx]];

			if (ipu_rot_mode_is_irt(ctx->rot_mode))
				out_tile->width = resized_height;
			else
				out_tile->height = resized_height;

			in_tile->height = in_height;
		}

		ctx->resize_coeffs_v[row] = resize_coeff_v;
	}
}

/*
 * return the number of runs in given queue (pending_q or done_q)
 * for this context. hold irqlock when calling.
 */
static int get_run_count(struct ipu_image_convert_ctx *ctx,
			 struct list_head *q)
{
	struct ipu_image_convert_run *run;
	int count = 0;

	lockdep_assert_held(&ctx->chan->irqlock);

	list_for_each_entry(run, q, list) {
		if (run->ctx == ctx)
			count++;
	}

	return count;
}

static void convert_stop(struct ipu_image_convert_run *run)
{
	struct ipu_image_convert_ctx *ctx = run->ctx;
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;

	dev_dbg(priv->ipu->dev, "%s: task %u: stopping ctx %p run %p\n",
		__func__, chan->ic_task, ctx, run);

	/* disable IC tasks and the channels */
	ipu_ic_task_disable(chan->ic);
	ipu_idmac_disable_channel(chan->in_chan);
	ipu_idmac_disable_channel(chan->out_chan);

	if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
		ipu_idmac_disable_channel(chan->rotation_in_chan);
		ipu_idmac_disable_channel(chan->rotation_out_chan);
		ipu_idmac_unlink(chan->out_chan, chan->rotation_in_chan);
	}

	ipu_ic_disable(chan->ic);
}

static void init_idmac_channel(struct ipu_image_convert_ctx *ctx,
			       struct ipuv3_channel *channel,
			       struct ipu_image_convert_image *image,
			       enum ipu_rotate_mode rot_mode,
			       bool rot_swap_width_height,
			       unsigned int tile)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	unsigned int burst_size;
	u32 width, height, stride;
	dma_addr_t addr0, addr1 = 0;
	struct ipu_image tile_image;
	unsigned int tile_idx[2];

	if (image->type == IMAGE_CONVERT_OUT) {
		tile_idx[0] = ctx->out_tile_map[tile];
		tile_idx[1] = ctx->out_tile_map[1];
	} else {
		tile_idx[0] = tile;
		tile_idx[1] = 1;
	}

	if (rot_swap_width_height) {
		width = image->tile[tile_idx[0]].height;
		height = image->tile[tile_idx[0]].width;
		stride = image->tile[tile_idx[0]].rot_stride;
		addr0 = ctx->rot_intermediate[0].phys;
		if (ctx->double_buffering)
			addr1 = ctx->rot_intermediate[1].phys;
	} else {
		width = image->tile[tile_idx[0]].width;
		height = image->tile[tile_idx[0]].height;
		stride = image->stride;
		addr0 = image->base.phys0 +
			image->tile[tile_idx[0]].offset;
		if (ctx->double_buffering)
			addr1 = image->base.phys0 +
				image->tile[tile_idx[1]].offset;
	}

	ipu_cpmem_zero(channel);

	memset(&tile_image, 0, sizeof(tile_image));
	tile_image.pix.width = tile_image.rect.width = width;
	tile_image.pix.height = tile_image.rect.height = height;
	tile_image.pix.bytesperline = stride;
	tile_image.pix.pixelformat =  image->fmt->fourcc;
	tile_image.phys0 = addr0;
	tile_image.phys1 = addr1;
	if (image->fmt->planar && !rot_swap_width_height) {
		tile_image.u_offset = image->tile[tile_idx[0]].u_off;
		tile_image.v_offset = image->tile[tile_idx[0]].v_off;
	}

	ipu_cpmem_set_image(channel, &tile_image);

	if (rot_mode)
		ipu_cpmem_set_rotation(channel, rot_mode);

	/*
	 * Skip writing U and V components to odd rows in the output
	 * channels for planar 4:2:0.
	 */
	if ((channel == chan->out_chan ||
	     channel == chan->rotation_out_chan) &&
	    image->fmt->planar && image->fmt->uv_height_dec == 2)
		ipu_cpmem_skip_odd_chroma_rows(channel);

	if (channel == chan->rotation_in_chan ||
	    channel == chan->rotation_out_chan) {
		burst_size = 8;
		ipu_cpmem_set_block_mode(channel);
	} else
		burst_size = (width % 16) ? 8 : 16;

	ipu_cpmem_set_burstsize(channel, burst_size);

	ipu_ic_task_idma_init(chan->ic, channel, width, height,
			      burst_size, rot_mode);

	/*
	 * Setting a non-zero AXI ID collides with the PRG AXI snooping, so
	 * only do this when there is no PRG present.
	 */
	if (!channel->ipu->prg_priv)
		ipu_cpmem_set_axi_id(channel, 1);

	ipu_idmac_set_double_buffer(channel, ctx->double_buffering);
}

static int convert_start(struct ipu_image_convert_run *run, unsigned int tile)
{
	struct ipu_image_convert_ctx *ctx = run->ctx;
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;
	struct ipu_image_convert_image *s_image = &ctx->in;
	struct ipu_image_convert_image *d_image = &ctx->out;
	unsigned int dst_tile = ctx->out_tile_map[tile];
	unsigned int dest_width, dest_height;
	unsigned int col, row;
	u32 rsc;
	int ret;

	dev_dbg(priv->ipu->dev, "%s: task %u: starting ctx %p run %p tile %u -> %u\n",
		__func__, chan->ic_task, ctx, run, tile, dst_tile);

	/* clear EOF irq mask */
	ctx->eof_mask = 0;

	if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
		/* swap width/height for resizer */
		dest_width = d_image->tile[dst_tile].height;
		dest_height = d_image->tile[dst_tile].width;
	} else {
		dest_width = d_image->tile[dst_tile].width;
		dest_height = d_image->tile[dst_tile].height;
	}

	row = tile / s_image->num_cols;
	col = tile % s_image->num_cols;

	rsc =  (ctx->downsize_coeff_v << 30) |
	       (ctx->resize_coeffs_v[row] << 16) |
	       (ctx->downsize_coeff_h << 14) |
	       (ctx->resize_coeffs_h[col]);

	dev_dbg(priv->ipu->dev, "%s: %ux%u -> %ux%u (rsc = 0x%x)\n",
		__func__, s_image->tile[tile].width,
		s_image->tile[tile].height, dest_width, dest_height, rsc);

	/* setup the IC resizer and CSC */
	ret = ipu_ic_task_init_rsc(chan->ic, &ctx->csc,
				   s_image->tile[tile].width,
				   s_image->tile[tile].height,
				   dest_width,
				   dest_height,
				   rsc);
	if (ret) {
		dev_err(priv->ipu->dev, "ipu_ic_task_init failed, %d\n", ret);
		return ret;
	}

	/* init the source MEM-->IC PP IDMAC channel */
	init_idmac_channel(ctx, chan->in_chan, s_image,
			   IPU_ROTATE_NONE, false, tile);

	if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
		/* init the IC PP-->MEM IDMAC channel */
		init_idmac_channel(ctx, chan->out_chan, d_image,
				   IPU_ROTATE_NONE, true, tile);

		/* init the MEM-->IC PP ROT IDMAC channel */
		init_idmac_channel(ctx, chan->rotation_in_chan, d_image,
				   ctx->rot_mode, true, tile);

		/* init the destination IC PP ROT-->MEM IDMAC channel */
		init_idmac_channel(ctx, chan->rotation_out_chan, d_image,
				   IPU_ROTATE_NONE, false, tile);

		/* now link IC PP-->MEM to MEM-->IC PP ROT */
		ipu_idmac_link(chan->out_chan, chan->rotation_in_chan);
	} else {
		/* init the destination IC PP-->MEM IDMAC channel */
		init_idmac_channel(ctx, chan->out_chan, d_image,
				   ctx->rot_mode, false, tile);
	}

	/* enable the IC */
	ipu_ic_enable(chan->ic);

	/* set buffers ready */
	ipu_idmac_select_buffer(chan->in_chan, 0);
	ipu_idmac_select_buffer(chan->out_chan, 0);
	if (ipu_rot_mode_is_irt(ctx->rot_mode))
		ipu_idmac_select_buffer(chan->rotation_out_chan, 0);
	if (ctx->double_buffering) {
		ipu_idmac_select_buffer(chan->in_chan, 1);
		ipu_idmac_select_buffer(chan->out_chan, 1);
		if (ipu_rot_mode_is_irt(ctx->rot_mode))
			ipu_idmac_select_buffer(chan->rotation_out_chan, 1);
	}

	/* enable the channels! */
	ipu_idmac_enable_channel(chan->in_chan);
	ipu_idmac_enable_channel(chan->out_chan);
	if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
		ipu_idmac_enable_channel(chan->rotation_in_chan);
		ipu_idmac_enable_channel(chan->rotation_out_chan);
	}

	ipu_ic_task_enable(chan->ic);

	ipu_cpmem_dump(chan->in_chan);
	ipu_cpmem_dump(chan->out_chan);
	if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
		ipu_cpmem_dump(chan->rotation_in_chan);
		ipu_cpmem_dump(chan->rotation_out_chan);
	}

	ipu_dump(priv->ipu);

	return 0;
}

/* hold irqlock when calling */
static int do_run(struct ipu_image_convert_run *run)
{
	struct ipu_image_convert_ctx *ctx = run->ctx;
	struct ipu_image_convert_chan *chan = ctx->chan;

	lockdep_assert_held(&chan->irqlock);

	ctx->in.base.phys0 = run->in_phys;
	ctx->out.base.phys0 = run->out_phys;

	ctx->cur_buf_num = 0;
	ctx->next_tile = 1;

	/* remove run from pending_q and set as current */
	list_del(&run->list);
	chan->current_run = run;

	return convert_start(run, 0);
}

/* hold irqlock when calling */
static void run_next(struct ipu_image_convert_chan *chan)
{
	struct ipu_image_convert_priv *priv = chan->priv;
	struct ipu_image_convert_run *run, *tmp;
	int ret;

	lockdep_assert_held(&chan->irqlock);

	list_for_each_entry_safe(run, tmp, &chan->pending_q, list) {
		/* skip contexts that are aborting */
		if (run->ctx->aborting) {
			dev_dbg(priv->ipu->dev,
				"%s: task %u: skipping aborting ctx %p run %p\n",
				__func__, chan->ic_task, run->ctx, run);
			continue;
		}

		ret = do_run(run);
		if (!ret)
			break;

		/*
		 * something went wrong with start, add the run
		 * to done q and continue to the next run in the
		 * pending q.
		 */
		run->status = ret;
		list_add_tail(&run->list, &chan->done_q);
		chan->current_run = NULL;
	}
}

static void empty_done_q(struct ipu_image_convert_chan *chan)
{
	struct ipu_image_convert_priv *priv = chan->priv;
	struct ipu_image_convert_run *run;
	unsigned long flags;

	spin_lock_irqsave(&chan->irqlock, flags);

	while (!list_empty(&chan->done_q)) {
		run = list_entry(chan->done_q.next,
				 struct ipu_image_convert_run,
				 list);

		list_del(&run->list);

		dev_dbg(priv->ipu->dev,
			"%s: task %u: completing ctx %p run %p with %d\n",
			__func__, chan->ic_task, run->ctx, run, run->status);

		/* call the completion callback and free the run */
		spin_unlock_irqrestore(&chan->irqlock, flags);
		run->ctx->complete(run, run->ctx->complete_context);
		spin_lock_irqsave(&chan->irqlock, flags);
	}

	spin_unlock_irqrestore(&chan->irqlock, flags);
}

/*
 * the bottom half thread clears out the done_q, calling the
 * completion handler for each.
 */
static irqreturn_t do_bh(int irq, void *dev_id)
{
	struct ipu_image_convert_chan *chan = dev_id;
	struct ipu_image_convert_priv *priv = chan->priv;
	struct ipu_image_convert_ctx *ctx;
	unsigned long flags;

	dev_dbg(priv->ipu->dev, "%s: task %u: enter\n", __func__,
		chan->ic_task);

	empty_done_q(chan);

	spin_lock_irqsave(&chan->irqlock, flags);

	/*
	 * the done_q is cleared out, signal any contexts
	 * that are aborting that abort can complete.
	 */
	list_for_each_entry(ctx, &chan->ctx_list, list) {
		if (ctx->aborting) {
			dev_dbg(priv->ipu->dev,
				"%s: task %u: signaling abort for ctx %p\n",
				__func__, chan->ic_task, ctx);
			complete_all(&ctx->aborted);
		}
	}

	spin_unlock_irqrestore(&chan->irqlock, flags);

	dev_dbg(priv->ipu->dev, "%s: task %u: exit\n", __func__,
		chan->ic_task);

	return IRQ_HANDLED;
}

static bool ic_settings_changed(struct ipu_image_convert_ctx *ctx)
{
	unsigned int cur_tile = ctx->next_tile - 1;
	unsigned int next_tile = ctx->next_tile;

	if (ctx->resize_coeffs_h[cur_tile % ctx->in.num_cols] !=
	    ctx->resize_coeffs_h[next_tile % ctx->in.num_cols] ||
	    ctx->resize_coeffs_v[cur_tile / ctx->in.num_cols] !=
	    ctx->resize_coeffs_v[next_tile / ctx->in.num_cols] ||
	    ctx->in.tile[cur_tile].width != ctx->in.tile[next_tile].width ||
	    ctx->in.tile[cur_tile].height != ctx->in.tile[next_tile].height ||
	    ctx->out.tile[cur_tile].width != ctx->out.tile[next_tile].width ||
	    ctx->out.tile[cur_tile].height != ctx->out.tile[next_tile].height)
		return true;

	return false;
}

/* hold irqlock when calling */
static irqreturn_t do_tile_complete(struct ipu_image_convert_run *run)
{
	struct ipu_image_convert_ctx *ctx = run->ctx;
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_tile *src_tile, *dst_tile;
	struct ipu_image_convert_image *s_image = &ctx->in;
	struct ipu_image_convert_image *d_image = &ctx->out;
	struct ipuv3_channel *outch;
	unsigned int dst_idx;

	lockdep_assert_held(&chan->irqlock);

	outch = ipu_rot_mode_is_irt(ctx->rot_mode) ?
		chan->rotation_out_chan : chan->out_chan;

	/*
	 * It is difficult to stop the channel DMA before the channels
	 * enter the paused state. Without double-buffering the channels
	 * are always in a paused state when the EOF irq occurs, so it
	 * is safe to stop the channels now. For double-buffering we
	 * just ignore the abort until the operation completes, when it
	 * is safe to shut down.
	 */
	if (ctx->aborting && !ctx->double_buffering) {
		convert_stop(run);
		run->status = -EIO;
		goto done;
	}

	if (ctx->next_tile == ctx->num_tiles) {
		/*
		 * the conversion is complete
		 */
		convert_stop(run);
		run->status = 0;
		goto done;
	}

	/*
	 * not done, place the next tile buffers.
	 */
	if (!ctx->double_buffering) {
		if (ic_settings_changed(ctx)) {
			convert_stop(run);
			convert_start(run, ctx->next_tile);
		} else {
			src_tile = &s_image->tile[ctx->next_tile];
			dst_idx = ctx->out_tile_map[ctx->next_tile];
			dst_tile = &d_image->tile[dst_idx];

			ipu_cpmem_set_buffer(chan->in_chan, 0,
					     s_image->base.phys0 +
					     src_tile->offset);
			ipu_cpmem_set_buffer(outch, 0,
					     d_image->base.phys0 +
					     dst_tile->offset);
			if (s_image->fmt->planar)
				ipu_cpmem_set_uv_offset(chan->in_chan,
							src_tile->u_off,
							src_tile->v_off);
			if (d_image->fmt->planar)
				ipu_cpmem_set_uv_offset(outch,
							dst_tile->u_off,
							dst_tile->v_off);

			ipu_idmac_select_buffer(chan->in_chan, 0);
			ipu_idmac_select_buffer(outch, 0);
		}
	} else if (ctx->next_tile < ctx->num_tiles - 1) {

		src_tile = &s_image->tile[ctx->next_tile + 1];
		dst_idx = ctx->out_tile_map[ctx->next_tile + 1];
		dst_tile = &d_image->tile[dst_idx];

		ipu_cpmem_set_buffer(chan->in_chan, ctx->cur_buf_num,
				     s_image->base.phys0 + src_tile->offset);
		ipu_cpmem_set_buffer(outch, ctx->cur_buf_num,
				     d_image->base.phys0 + dst_tile->offset);

		ipu_idmac_select_buffer(chan->in_chan, ctx->cur_buf_num);
		ipu_idmac_select_buffer(outch, ctx->cur_buf_num);

		ctx->cur_buf_num ^= 1;
	}

	ctx->eof_mask = 0; /* clear EOF irq mask for next tile */
	ctx->next_tile++;
	return IRQ_HANDLED;
done:
	list_add_tail(&run->list, &chan->done_q);
	chan->current_run = NULL;
	run_next(chan);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t eof_irq(int irq, void *data)
{
	struct ipu_image_convert_chan *chan = data;
	struct ipu_image_convert_priv *priv = chan->priv;
	struct ipu_image_convert_ctx *ctx;
	struct ipu_image_convert_run *run;
	irqreturn_t ret = IRQ_HANDLED;
	bool tile_complete = false;
	unsigned long flags;

	spin_lock_irqsave(&chan->irqlock, flags);

	/* get current run and its context */
	run = chan->current_run;
	if (!run) {
		ret = IRQ_NONE;
		goto out;
	}

	ctx = run->ctx;

	if (irq == chan->in_eof_irq) {
		ctx->eof_mask |= EOF_IRQ_IN;
	} else if (irq == chan->out_eof_irq) {
		ctx->eof_mask |= EOF_IRQ_OUT;
	} else if (irq == chan->rot_in_eof_irq ||
		   irq == chan->rot_out_eof_irq) {
		if (!ipu_rot_mode_is_irt(ctx->rot_mode)) {
			/* this was NOT a rotation op, shouldn't happen */
			dev_err(priv->ipu->dev,
				"Unexpected rotation interrupt\n");
			goto out;
		}
		ctx->eof_mask |= (irq == chan->rot_in_eof_irq) ?
			EOF_IRQ_ROT_IN : EOF_IRQ_ROT_OUT;
	} else {
		dev_err(priv->ipu->dev, "Received unknown irq %d\n", irq);
		ret = IRQ_NONE;
		goto out;
	}

	if (ipu_rot_mode_is_irt(ctx->rot_mode))
		tile_complete =	(ctx->eof_mask == EOF_IRQ_ROT_COMPLETE);
	else
		tile_complete = (ctx->eof_mask == EOF_IRQ_COMPLETE);

	if (tile_complete)
		ret = do_tile_complete(run);
out:
	spin_unlock_irqrestore(&chan->irqlock, flags);
	return ret;
}

/*
 * try to force the completion of runs for this ctx. Called when
 * abort wait times out in ipu_image_convert_abort().
 */
static void force_abort(struct ipu_image_convert_ctx *ctx)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_run *run;
	unsigned long flags;

	spin_lock_irqsave(&chan->irqlock, flags);

	run = chan->current_run;
	if (run && run->ctx == ctx) {
		convert_stop(run);
		run->status = -EIO;
		list_add_tail(&run->list, &chan->done_q);
		chan->current_run = NULL;
		run_next(chan);
	}

	spin_unlock_irqrestore(&chan->irqlock, flags);

	empty_done_q(chan);
}

static void release_ipu_resources(struct ipu_image_convert_chan *chan)
{
	if (chan->in_eof_irq >= 0)
		free_irq(chan->in_eof_irq, chan);
	if (chan->rot_in_eof_irq >= 0)
		free_irq(chan->rot_in_eof_irq, chan);
	if (chan->out_eof_irq >= 0)
		free_irq(chan->out_eof_irq, chan);
	if (chan->rot_out_eof_irq >= 0)
		free_irq(chan->rot_out_eof_irq, chan);

	if (!IS_ERR_OR_NULL(chan->in_chan))
		ipu_idmac_put(chan->in_chan);
	if (!IS_ERR_OR_NULL(chan->out_chan))
		ipu_idmac_put(chan->out_chan);
	if (!IS_ERR_OR_NULL(chan->rotation_in_chan))
		ipu_idmac_put(chan->rotation_in_chan);
	if (!IS_ERR_OR_NULL(chan->rotation_out_chan))
		ipu_idmac_put(chan->rotation_out_chan);
	if (!IS_ERR_OR_NULL(chan->ic))
		ipu_ic_put(chan->ic);

	chan->in_chan = chan->out_chan = chan->rotation_in_chan =
		chan->rotation_out_chan = NULL;
	chan->in_eof_irq = -1;
	chan->rot_in_eof_irq = -1;
	chan->out_eof_irq = -1;
	chan->rot_out_eof_irq = -1;
}

static int get_eof_irq(struct ipu_image_convert_chan *chan,
		       struct ipuv3_channel *channel)
{
	struct ipu_image_convert_priv *priv = chan->priv;
	int ret, irq;

	irq = ipu_idmac_channel_irq(priv->ipu, channel, IPU_IRQ_EOF);

	ret = request_threaded_irq(irq, eof_irq, do_bh, 0, "ipu-ic", chan);
	if (ret < 0) {
		dev_err(priv->ipu->dev, "could not acquire irq %d\n", irq);
		return ret;
	}

	return irq;
}

static int get_ipu_resources(struct ipu_image_convert_chan *chan)
{
	const struct ipu_image_convert_dma_chan *dma = chan->dma_ch;
	struct ipu_image_convert_priv *priv = chan->priv;
	int ret;

	/* get IC */
	chan->ic = ipu_ic_get(priv->ipu, chan->ic_task);
	if (IS_ERR(chan->ic)) {
		dev_err(priv->ipu->dev, "could not acquire IC\n");
		ret = PTR_ERR(chan->ic);
		goto err;
	}

	/* get IDMAC channels */
	chan->in_chan = ipu_idmac_get(priv->ipu, dma->in);
	chan->out_chan = ipu_idmac_get(priv->ipu, dma->out);
	if (IS_ERR(chan->in_chan) || IS_ERR(chan->out_chan)) {
		dev_err(priv->ipu->dev, "could not acquire idmac channels\n");
		ret = -EBUSY;
		goto err;
	}

	chan->rotation_in_chan = ipu_idmac_get(priv->ipu, dma->rot_in);
	chan->rotation_out_chan = ipu_idmac_get(priv->ipu, dma->rot_out);
	if (IS_ERR(chan->rotation_in_chan) || IS_ERR(chan->rotation_out_chan)) {
		dev_err(priv->ipu->dev,
			"could not acquire idmac rotation channels\n");
		ret = -EBUSY;
		goto err;
	}

	/* acquire the EOF interrupts */
	ret = get_eof_irq(chan, chan->in_chan);
	if (ret < 0) {
		chan->in_eof_irq = -1;
		goto err;
	}
	chan->in_eof_irq = ret;

	ret = get_eof_irq(chan, chan->rotation_in_chan);
	if (ret < 0) {
		chan->rot_in_eof_irq = -1;
		goto err;
	}
	chan->rot_in_eof_irq = ret;

	ret = get_eof_irq(chan, chan->out_chan);
	if (ret < 0) {
		chan->out_eof_irq = -1;
		goto err;
	}
	chan->out_eof_irq = ret;

	ret = get_eof_irq(chan, chan->rotation_out_chan);
	if (ret < 0) {
		chan->rot_out_eof_irq = -1;
		goto err;
	}
	chan->rot_out_eof_irq = ret;

	return 0;
err:
	release_ipu_resources(chan);
	return ret;
}

static int fill_image(struct ipu_image_convert_ctx *ctx,
		      struct ipu_image_convert_image *ic_image,
		      struct ipu_image *image,
		      enum ipu_image_convert_type type)
{
	struct ipu_image_convert_priv *priv = ctx->chan->priv;

	ic_image->base = *image;
	ic_image->type = type;

	ic_image->fmt = get_format(image->pix.pixelformat);
	if (!ic_image->fmt) {
		dev_err(priv->ipu->dev, "pixelformat not supported for %s\n",
			type == IMAGE_CONVERT_OUT ? "Output" : "Input");
		return -EINVAL;
	}

	if (ic_image->fmt->planar)
		ic_image->stride = ic_image->base.pix.width;
	else
		ic_image->stride  = ic_image->base.pix.bytesperline;

	return 0;
}

/* borrowed from drivers/media/v4l2-core/v4l2-common.c */
static unsigned int clamp_align(unsigned int x, unsigned int min,
				unsigned int max, unsigned int align)
{
	/* Bits that must be zero to be aligned */
	unsigned int mask = ~((1 << align) - 1);

	/* Clamp to aligned min and max */
	x = clamp(x, (min + ~mask) & mask, max & mask);

	/* Round to nearest aligned value */
	if (align)
		x = (x + (1 << (align - 1))) & mask;

	return x;
}

/* Adjusts input/output images to IPU restrictions */
void ipu_image_convert_adjust(struct ipu_image *in, struct ipu_image *out,
			      enum ipu_rotate_mode rot_mode)
{
	const struct ipu_image_pixfmt *infmt, *outfmt;
	u32 w_align_out, h_align_out;
	u32 w_align_in, h_align_in;

	infmt = get_format(in->pix.pixelformat);
	outfmt = get_format(out->pix.pixelformat);

	/* set some default pixel formats if needed */
	if (!infmt) {
		in->pix.pixelformat = V4L2_PIX_FMT_RGB24;
		infmt = get_format(V4L2_PIX_FMT_RGB24);
	}
	if (!outfmt) {
		out->pix.pixelformat = V4L2_PIX_FMT_RGB24;
		outfmt = get_format(V4L2_PIX_FMT_RGB24);
	}

	/* image converter does not handle fields */
	in->pix.field = out->pix.field = V4L2_FIELD_NONE;

	/* resizer cannot downsize more than 4:1 */
	if (ipu_rot_mode_is_irt(rot_mode)) {
		out->pix.height = max_t(__u32, out->pix.height,
					in->pix.width / 4);
		out->pix.width = max_t(__u32, out->pix.width,
				       in->pix.height / 4);
	} else {
		out->pix.width = max_t(__u32, out->pix.width,
				       in->pix.width / 4);
		out->pix.height = max_t(__u32, out->pix.height,
					in->pix.height / 4);
	}

	/* align input width/height */
	w_align_in = ilog2(tile_width_align(IMAGE_CONVERT_IN, infmt,
					    rot_mode));
	h_align_in = ilog2(tile_height_align(IMAGE_CONVERT_IN, infmt,
					     rot_mode));
	in->pix.width = clamp_align(in->pix.width, MIN_W, MAX_W,
				    w_align_in);
	in->pix.height = clamp_align(in->pix.height, MIN_H, MAX_H,
				     h_align_in);

	/* align output width/height */
	w_align_out = ilog2(tile_width_align(IMAGE_CONVERT_OUT, outfmt,
					     rot_mode));
	h_align_out = ilog2(tile_height_align(IMAGE_CONVERT_OUT, outfmt,
					      rot_mode));
	out->pix.width = clamp_align(out->pix.width, MIN_W, MAX_W,
				     w_align_out);
	out->pix.height = clamp_align(out->pix.height, MIN_H, MAX_H,
				      h_align_out);

	/* set input/output strides and image sizes */
	in->pix.bytesperline = infmt->planar ?
		clamp_align(in->pix.width, 2 << w_align_in, MAX_W,
			    w_align_in) :
		clamp_align((in->pix.width * infmt->bpp) >> 3,
			    ((2 << w_align_in) * infmt->bpp) >> 3,
			    (MAX_W * infmt->bpp) >> 3,
			    w_align_in);
	in->pix.sizeimage = infmt->planar ?
		(in->pix.height * in->pix.bytesperline * infmt->bpp) >> 3 :
		in->pix.height * in->pix.bytesperline;
	out->pix.bytesperline = outfmt->planar ? out->pix.width :
		(out->pix.width * outfmt->bpp) >> 3;
	out->pix.sizeimage = outfmt->planar ?
		(out->pix.height * out->pix.bytesperline * outfmt->bpp) >> 3 :
		out->pix.height * out->pix.bytesperline;
}
EXPORT_SYMBOL_GPL(ipu_image_convert_adjust);

/*
 * this is used by ipu_image_convert_prepare() to verify set input and
 * output images are valid before starting the conversion. Clients can
 * also call it before calling ipu_image_convert_prepare().
 */
int ipu_image_convert_verify(struct ipu_image *in, struct ipu_image *out,
			     enum ipu_rotate_mode rot_mode)
{
	struct ipu_image testin, testout;

	testin = *in;
	testout = *out;

	ipu_image_convert_adjust(&testin, &testout, rot_mode);

	if (testin.pix.width != in->pix.width ||
	    testin.pix.height != in->pix.height ||
	    testout.pix.width != out->pix.width ||
	    testout.pix.height != out->pix.height)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_image_convert_verify);

/*
 * Call ipu_image_convert_prepare() to prepare for the conversion of
 * given images and rotation mode. Returns a new conversion context.
 */
struct ipu_image_convert_ctx *
ipu_image_convert_prepare(struct ipu_soc *ipu, enum ipu_ic_task ic_task,
			  struct ipu_image *in, struct ipu_image *out,
			  enum ipu_rotate_mode rot_mode,
			  ipu_image_convert_cb_t complete,
			  void *complete_context)
{
	struct ipu_image_convert_priv *priv = ipu->image_convert_priv;
	struct ipu_image_convert_image *s_image, *d_image;
	struct ipu_image_convert_chan *chan;
	struct ipu_image_convert_ctx *ctx;
	unsigned long flags;
	unsigned int i;
	bool get_res;
	int ret;

	if (!in || !out || !complete ||
	    (ic_task != IC_TASK_VIEWFINDER &&
	     ic_task != IC_TASK_POST_PROCESSOR))
		return ERR_PTR(-EINVAL);

	/* verify the in/out images before continuing */
	ret = ipu_image_convert_verify(in, out, rot_mode);
	if (ret) {
		dev_err(priv->ipu->dev, "%s: in/out formats invalid\n",
			__func__);
		return ERR_PTR(ret);
	}

	chan = &priv->chan[ic_task];

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	dev_dbg(priv->ipu->dev, "%s: task %u: ctx %p\n", __func__,
		chan->ic_task, ctx);

	ctx->chan = chan;
	init_completion(&ctx->aborted);

	ctx->rot_mode = rot_mode;

	/* Sets ctx->in.num_rows/cols as well */
	ret = calc_image_resize_coefficients(ctx, in, out);
	if (ret)
		goto out_free;

	s_image = &ctx->in;
	d_image = &ctx->out;

	/* set tiling and rotation */
	if (ipu_rot_mode_is_irt(rot_mode)) {
		d_image->num_rows = s_image->num_cols;
		d_image->num_cols = s_image->num_rows;
	} else {
		d_image->num_rows = s_image->num_rows;
		d_image->num_cols = s_image->num_cols;
	}

	ctx->num_tiles = d_image->num_cols * d_image->num_rows;

	ret = fill_image(ctx, s_image, in, IMAGE_CONVERT_IN);
	if (ret)
		goto out_free;
	ret = fill_image(ctx, d_image, out, IMAGE_CONVERT_OUT);
	if (ret)
		goto out_free;

	calc_out_tile_map(ctx);

	find_seams(ctx, s_image, d_image);

	ret = calc_tile_dimensions(ctx, s_image);
	if (ret)
		goto out_free;

	ret = calc_tile_offsets(ctx, s_image);
	if (ret)
		goto out_free;

	calc_tile_dimensions(ctx, d_image);
	ret = calc_tile_offsets(ctx, d_image);
	if (ret)
		goto out_free;

	calc_tile_resize_coefficients(ctx);

	ret = ipu_ic_calc_csc(&ctx->csc,
			s_image->base.pix.ycbcr_enc,
			s_image->base.pix.quantization,
			ipu_pixelformat_to_colorspace(s_image->fmt->fourcc),
			d_image->base.pix.ycbcr_enc,
			d_image->base.pix.quantization,
			ipu_pixelformat_to_colorspace(d_image->fmt->fourcc));
	if (ret)
		goto out_free;

	dump_format(ctx, s_image);
	dump_format(ctx, d_image);

	ctx->complete = complete;
	ctx->complete_context = complete_context;

	/*
	 * Can we use double-buffering for this operation? If there is
	 * only one tile (the whole image can be converted in a single
	 * operation) there's no point in using double-buffering. Also,
	 * the IPU's IDMAC channels allow only a single U and V plane
	 * offset shared between both buffers, but these offsets change
	 * for every tile, and therefore would have to be updated for
	 * each buffer which is not possible. So double-buffering is
	 * impossible when either the source or destination images are
	 * a planar format (YUV420, YUV422P, etc.). Further, differently
	 * sized tiles or different resizing coefficients per tile
	 * prevent double-buffering as well.
	 */
	ctx->double_buffering = (ctx->num_tiles > 1 &&
				 !s_image->fmt->planar &&
				 !d_image->fmt->planar);
	for (i = 1; i < ctx->num_tiles; i++) {
		if (ctx->in.tile[i].width != ctx->in.tile[0].width ||
		    ctx->in.tile[i].height != ctx->in.tile[0].height ||
		    ctx->out.tile[i].width != ctx->out.tile[0].width ||
		    ctx->out.tile[i].height != ctx->out.tile[0].height) {
			ctx->double_buffering = false;
			break;
		}
	}
	for (i = 1; i < ctx->in.num_cols; i++) {
		if (ctx->resize_coeffs_h[i] != ctx->resize_coeffs_h[0]) {
			ctx->double_buffering = false;
			break;
		}
	}
	for (i = 1; i < ctx->in.num_rows; i++) {
		if (ctx->resize_coeffs_v[i] != ctx->resize_coeffs_v[0]) {
			ctx->double_buffering = false;
			break;
		}
	}

	if (ipu_rot_mode_is_irt(ctx->rot_mode)) {
		unsigned long intermediate_size = d_image->tile[0].size;

		for (i = 1; i < ctx->num_tiles; i++) {
			if (d_image->tile[i].size > intermediate_size)
				intermediate_size = d_image->tile[i].size;
		}

		ret = alloc_dma_buf(priv, &ctx->rot_intermediate[0],
				    intermediate_size);
		if (ret)
			goto out_free;
		if (ctx->double_buffering) {
			ret = alloc_dma_buf(priv,
					    &ctx->rot_intermediate[1],
					    intermediate_size);
			if (ret)
				goto out_free_dmabuf0;
		}
	}

	spin_lock_irqsave(&chan->irqlock, flags);

	get_res = list_empty(&chan->ctx_list);

	list_add_tail(&ctx->list, &chan->ctx_list);

	spin_unlock_irqrestore(&chan->irqlock, flags);

	if (get_res) {
		ret = get_ipu_resources(chan);
		if (ret)
			goto out_free_dmabuf1;
	}

	return ctx;

out_free_dmabuf1:
	free_dma_buf(priv, &ctx->rot_intermediate[1]);
	spin_lock_irqsave(&chan->irqlock, flags);
	list_del(&ctx->list);
	spin_unlock_irqrestore(&chan->irqlock, flags);
out_free_dmabuf0:
	free_dma_buf(priv, &ctx->rot_intermediate[0]);
out_free:
	kfree(ctx);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(ipu_image_convert_prepare);

/*
 * Carry out a single image conversion run. Only the physaddr's of the input
 * and output image buffers are needed. The conversion context must have
 * been created previously with ipu_image_convert_prepare().
 */
int ipu_image_convert_queue(struct ipu_image_convert_run *run)
{
	struct ipu_image_convert_chan *chan;
	struct ipu_image_convert_priv *priv;
	struct ipu_image_convert_ctx *ctx;
	unsigned long flags;
	int ret = 0;

	if (!run || !run->ctx || !run->in_phys || !run->out_phys)
		return -EINVAL;

	ctx = run->ctx;
	chan = ctx->chan;
	priv = chan->priv;

	dev_dbg(priv->ipu->dev, "%s: task %u: ctx %p run %p\n", __func__,
		chan->ic_task, ctx, run);

	INIT_LIST_HEAD(&run->list);

	spin_lock_irqsave(&chan->irqlock, flags);

	if (ctx->aborting) {
		ret = -EIO;
		goto unlock;
	}

	list_add_tail(&run->list, &chan->pending_q);

	if (!chan->current_run) {
		ret = do_run(run);
		if (ret)
			chan->current_run = NULL;
	}
unlock:
	spin_unlock_irqrestore(&chan->irqlock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(ipu_image_convert_queue);

/* Abort any active or pending conversions for this context */
static void __ipu_image_convert_abort(struct ipu_image_convert_ctx *ctx)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;
	struct ipu_image_convert_run *run, *active_run, *tmp;
	unsigned long flags;
	int run_count, ret;

	spin_lock_irqsave(&chan->irqlock, flags);

	/* move all remaining pending runs in this context to done_q */
	list_for_each_entry_safe(run, tmp, &chan->pending_q, list) {
		if (run->ctx != ctx)
			continue;
		run->status = -EIO;
		list_move_tail(&run->list, &chan->done_q);
	}

	run_count = get_run_count(ctx, &chan->done_q);
	active_run = (chan->current_run && chan->current_run->ctx == ctx) ?
		chan->current_run : NULL;

	if (active_run)
		reinit_completion(&ctx->aborted);

	ctx->aborting = true;

	spin_unlock_irqrestore(&chan->irqlock, flags);

	if (!run_count && !active_run) {
		dev_dbg(priv->ipu->dev,
			"%s: task %u: no abort needed for ctx %p\n",
			__func__, chan->ic_task, ctx);
		return;
	}

	if (!active_run) {
		empty_done_q(chan);
		return;
	}

	dev_dbg(priv->ipu->dev,
		"%s: task %u: wait for completion: %d runs\n",
		__func__, chan->ic_task, run_count);

	ret = wait_for_completion_timeout(&ctx->aborted,
					  msecs_to_jiffies(10000));
	if (ret == 0) {
		dev_warn(priv->ipu->dev, "%s: timeout\n", __func__);
		force_abort(ctx);
	}
}

void ipu_image_convert_abort(struct ipu_image_convert_ctx *ctx)
{
	__ipu_image_convert_abort(ctx);
	ctx->aborting = false;
}
EXPORT_SYMBOL_GPL(ipu_image_convert_abort);

/* Unprepare image conversion context */
void ipu_image_convert_unprepare(struct ipu_image_convert_ctx *ctx)
{
	struct ipu_image_convert_chan *chan = ctx->chan;
	struct ipu_image_convert_priv *priv = chan->priv;
	unsigned long flags;
	bool put_res;

	/* make sure no runs are hanging around */
	__ipu_image_convert_abort(ctx);

	dev_dbg(priv->ipu->dev, "%s: task %u: removing ctx %p\n", __func__,
		chan->ic_task, ctx);

	spin_lock_irqsave(&chan->irqlock, flags);

	list_del(&ctx->list);

	put_res = list_empty(&chan->ctx_list);

	spin_unlock_irqrestore(&chan->irqlock, flags);

	if (put_res)
		release_ipu_resources(chan);

	free_dma_buf(priv, &ctx->rot_intermediate[1]);
	free_dma_buf(priv, &ctx->rot_intermediate[0]);

	kfree(ctx);
}
EXPORT_SYMBOL_GPL(ipu_image_convert_unprepare);

/*
 * "Canned" asynchronous single image conversion. Allocates and returns
 * a new conversion run.  On successful return the caller must free the
 * run and call ipu_image_convert_unprepare() after conversion completes.
 */
struct ipu_image_convert_run *
ipu_image_convert(struct ipu_soc *ipu, enum ipu_ic_task ic_task,
		  struct ipu_image *in, struct ipu_image *out,
		  enum ipu_rotate_mode rot_mode,
		  ipu_image_convert_cb_t complete,
		  void *complete_context)
{
	struct ipu_image_convert_ctx *ctx;
	struct ipu_image_convert_run *run;
	int ret;

	ctx = ipu_image_convert_prepare(ipu, ic_task, in, out, rot_mode,
					complete, complete_context);
	if (IS_ERR(ctx))
		return ERR_CAST(ctx);

	run = kzalloc(sizeof(*run), GFP_KERNEL);
	if (!run) {
		ipu_image_convert_unprepare(ctx);
		return ERR_PTR(-ENOMEM);
	}

	run->ctx = ctx;
	run->in_phys = in->phys0;
	run->out_phys = out->phys0;

	ret = ipu_image_convert_queue(run);
	if (ret) {
		ipu_image_convert_unprepare(ctx);
		kfree(run);
		return ERR_PTR(ret);
	}

	return run;
}
EXPORT_SYMBOL_GPL(ipu_image_convert);

/* "Canned" synchronous single image conversion */
static void image_convert_sync_complete(struct ipu_image_convert_run *run,
					void *data)
{
	struct completion *comp = data;

	complete(comp);
}

int ipu_image_convert_sync(struct ipu_soc *ipu, enum ipu_ic_task ic_task,
			   struct ipu_image *in, struct ipu_image *out,
			   enum ipu_rotate_mode rot_mode)
{
	struct ipu_image_convert_run *run;
	struct completion comp;
	int ret;

	init_completion(&comp);

	run = ipu_image_convert(ipu, ic_task, in, out, rot_mode,
				image_convert_sync_complete, &comp);
	if (IS_ERR(run))
		return PTR_ERR(run);

	ret = wait_for_completion_timeout(&comp, msecs_to_jiffies(10000));
	ret = (ret == 0) ? -ETIMEDOUT : 0;

	ipu_image_convert_unprepare(run->ctx);
	kfree(run);

	return ret;
}
EXPORT_SYMBOL_GPL(ipu_image_convert_sync);

int ipu_image_convert_init(struct ipu_soc *ipu, struct device *dev)
{
	struct ipu_image_convert_priv *priv;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ipu->image_convert_priv = priv;
	priv->ipu = ipu;

	for (i = 0; i < IC_NUM_TASKS; i++) {
		struct ipu_image_convert_chan *chan = &priv->chan[i];

		chan->ic_task = i;
		chan->priv = priv;
		chan->dma_ch = &image_convert_dma_chan[i];
		chan->in_eof_irq = -1;
		chan->rot_in_eof_irq = -1;
		chan->out_eof_irq = -1;
		chan->rot_out_eof_irq = -1;

		spin_lock_init(&chan->irqlock);
		INIT_LIST_HEAD(&chan->ctx_list);
		INIT_LIST_HEAD(&chan->pending_q);
		INIT_LIST_HEAD(&chan->done_q);
	}

	return 0;
}

void ipu_image_convert_exit(struct ipu_soc *ipu)
{
}
