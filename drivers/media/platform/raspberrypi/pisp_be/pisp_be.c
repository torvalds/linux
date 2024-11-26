// SPDX-License-Identifier: GPL-2.0
/*
 * PiSP Back End driver.
 * Copyright (c) 2021-2024 Raspberry Pi Limited.
 *
 */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>

#include <uapi/linux/media/raspberrypi/pisp_be_config.h>

#include "pisp_be_formats.h"

/* Maximum number of config buffers possible */
#define PISP_BE_NUM_CONFIG_BUFFERS VB2_MAX_FRAME

#define PISPBE_NAME "pispbe"

/* Some ISP-BE registers */
#define PISP_BE_VERSION_REG		0x0
#define PISP_BE_CONTROL_REG		0x4
#define PISP_BE_CONTROL_COPY_CONFIG	BIT(1)
#define PISP_BE_CONTROL_QUEUE_JOB	BIT(0)
#define PISP_BE_CONTROL_NUM_TILES(n)	((n) << 16)
#define PISP_BE_TILE_ADDR_LO_REG	0x8
#define PISP_BE_TILE_ADDR_HI_REG	0xc
#define PISP_BE_STATUS_REG		0x10
#define PISP_BE_STATUS_QUEUED		BIT(0)
#define PISP_BE_BATCH_STATUS_REG	0x14
#define PISP_BE_INTERRUPT_EN_REG	0x18
#define PISP_BE_INTERRUPT_STATUS_REG	0x1c
#define PISP_BE_AXI_REG			0x20
#define PISP_BE_CONFIG_BASE_REG		0x40
#define PISP_BE_IO_ADDR_LOW(n)		(PISP_BE_CONFIG_BASE_REG + 8 * (n))
#define PISP_BE_IO_ADDR_HIGH(n)		(PISP_BE_IO_ADDR_LOW((n)) + 4)
#define PISP_BE_GLOBAL_BAYER_ENABLE	0xb0
#define PISP_BE_GLOBAL_RGB_ENABLE	0xb4
#define N_HW_ADDRESSES			13
#define N_HW_ENABLES			2

#define PISP_BE_VERSION_2712		0x02252700
#define PISP_BE_VERSION_MINOR_BITS	0xf

/*
 * This maps our nodes onto the inputs/outputs of the actual PiSP Back End.
 * Be wary of the word "OUTPUT" which is used ambiguously here. In a V4L2
 * context it means an input to the hardware (source image or metadata).
 * Elsewhere it means an output from the hardware.
 */
enum pispbe_node_ids {
	MAIN_INPUT_NODE,
	TDN_INPUT_NODE,
	STITCH_INPUT_NODE,
	OUTPUT0_NODE,
	OUTPUT1_NODE,
	TDN_OUTPUT_NODE,
	STITCH_OUTPUT_NODE,
	CONFIG_NODE,
	PISPBE_NUM_NODES
};

struct pispbe_node_description {
	const char *ent_name;
	enum v4l2_buf_type buf_type;
	unsigned int caps;
};

static const struct pispbe_node_description node_desc[PISPBE_NUM_NODES] = {
	/* MAIN_INPUT_NODE */
	{
		.ent_name = PISPBE_NAME "-input",
		.buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.caps = V4L2_CAP_VIDEO_OUTPUT_MPLANE,
	},
	/* TDN_INPUT_NODE */
	{
		.ent_name = PISPBE_NAME "-tdn_input",
		.buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.caps = V4L2_CAP_VIDEO_OUTPUT_MPLANE,
	},
	/* STITCH_INPUT_NODE */
	{
		.ent_name = PISPBE_NAME "-stitch_input",
		.buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.caps = V4L2_CAP_VIDEO_OUTPUT_MPLANE,
	},
	/* OUTPUT0_NODE */
	{
		.ent_name = PISPBE_NAME "-output0",
		.buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE,
	},
	/* OUTPUT1_NODE */
	{
		.ent_name = PISPBE_NAME "-output1",
		.buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE,
	},
	/* TDN_OUTPUT_NODE */
	{
		.ent_name = PISPBE_NAME "-tdn_output",
		.buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE,
	},
	/* STITCH_OUTPUT_NODE */
	{
		.ent_name = PISPBE_NAME "-stitch_output",
		.buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE,
	},
	/* CONFIG_NODE */
	{
		.ent_name = PISPBE_NAME "-config",
		.buf_type = V4L2_BUF_TYPE_META_OUTPUT,
		.caps = V4L2_CAP_META_OUTPUT,
	}
};

#define NODE_DESC_IS_OUTPUT(desc) ( \
	((desc)->buf_type == V4L2_BUF_TYPE_META_OUTPUT) || \
	((desc)->buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT) || \
	((desc)->buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE))

#define NODE_IS_META(node) ( \
	((node)->buf_type == V4L2_BUF_TYPE_META_OUTPUT))
#define NODE_IS_OUTPUT(node) ( \
	((node)->buf_type == V4L2_BUF_TYPE_META_OUTPUT) || \
	((node)->buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT) || \
	((node)->buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE))
#define NODE_IS_CAPTURE(node) ( \
	((node)->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE) || \
	((node)->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE))
#define NODE_IS_MPLANE(node) ( \
	((node)->buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) || \
	((node)->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE))

/*
 * Structure to describe a single node /dev/video<N> which represents a single
 * input or output queue to the PiSP Back End device.
 */
struct pispbe_node {
	unsigned int id;
	int vfl_dir;
	enum v4l2_buf_type buf_type;
	struct video_device vfd;
	struct media_pad pad;
	struct media_intf_devnode *intf_devnode;
	struct media_link *intf_link;
	struct pispbe_dev *pispbe;
	/* Video device lock */
	struct mutex node_lock;
	/* vb2_queue lock */
	struct mutex queue_lock;
	/* Protect pispbe_node->ready_queue and pispbe_buffer->ready_list */
	spinlock_t ready_lock;
	struct list_head ready_queue;
	struct vb2_queue queue;
	struct v4l2_format format;
	const struct pisp_be_format *pisp_format;
};

/* For logging only, use the entity name with "pispbe" and separator removed */
#define NODE_NAME(node) \
		(node_desc[(node)->id].ent_name + sizeof(PISPBE_NAME))

/* Records details of the jobs currently running or queued on the h/w. */
struct pispbe_job {
	bool valid;
	/*
	 * An array of buffer pointers - remember it's source buffers first,
	 * then captures, then metadata last.
	 */
	struct pispbe_buffer *buf[PISPBE_NUM_NODES];
};

struct pispbe_hw_enables {
	u32 bayer_enables;
	u32 rgb_enables;
};

/* Records a job configuration and memory addresses. */
struct pispbe_job_descriptor {
	dma_addr_t hw_dma_addrs[N_HW_ADDRESSES];
	struct pisp_be_tiles_config *config;
	struct pispbe_hw_enables hw_enables;
	dma_addr_t tiles;
};

/*
 * Structure representing the entire PiSP Back End device, comprising several
 * nodes which share platform resources and a mutex for the actual HW.
 */
struct pispbe_dev {
	struct device *dev;
	struct pispbe_dev *pispbe;
	struct pisp_be_tiles_config *config;
	void __iomem *be_reg_base;
	struct clk *clk;
	struct v4l2_device v4l2_dev;
	struct v4l2_subdev sd;
	struct media_device mdev;
	struct media_pad pad[PISPBE_NUM_NODES]; /* output pads first */
	struct pispbe_node node[PISPBE_NUM_NODES];
	dma_addr_t config_dma_addr;
	unsigned int sequence;
	u32 streaming_map;
	struct pispbe_job queued_job, running_job;
	spinlock_t hw_lock; /* protects "hw_busy" flag and streaming_map */
	bool hw_busy; /* non-zero if a job is queued or is being started */
	int irq;
	u32 hw_version;
	u8 done, started;
};

static u32 pispbe_rd(struct pispbe_dev *pispbe, unsigned int offset)
{
	return readl(pispbe->be_reg_base + offset);
}

static void pispbe_wr(struct pispbe_dev *pispbe, unsigned int offset, u32 val)
{
	writel(val, pispbe->be_reg_base + offset);
}

/*
 * Queue a job to the h/w. If the h/w is idle it will begin immediately.
 * Caller must ensure it is "safe to queue", i.e. we don't already have a
 * queued, unstarted job.
 */
static void pispbe_queue_job(struct pispbe_dev *pispbe,
			     struct pispbe_job_descriptor *job)
{
	unsigned int begin, end;

	if (pispbe_rd(pispbe, PISP_BE_STATUS_REG) & PISP_BE_STATUS_QUEUED)
		dev_err(pispbe->dev, "ERROR: not safe to queue new job!\n");

	/*
	 * Write configuration to hardware. DMA addresses and enable flags
	 * are passed separately, because the driver needs to sanitize them,
	 * and we don't want to modify (or be vulnerable to modifications of)
	 * the mmap'd buffer.
	 */
	for (unsigned int u = 0; u < N_HW_ADDRESSES; ++u) {
		pispbe_wr(pispbe, PISP_BE_IO_ADDR_LOW(u),
			  lower_32_bits(job->hw_dma_addrs[u]));
		pispbe_wr(pispbe, PISP_BE_IO_ADDR_HIGH(u),
			  upper_32_bits(job->hw_dma_addrs[u]));
	}
	pispbe_wr(pispbe, PISP_BE_GLOBAL_BAYER_ENABLE,
		  job->hw_enables.bayer_enables);
	pispbe_wr(pispbe, PISP_BE_GLOBAL_RGB_ENABLE,
		  job->hw_enables.rgb_enables);

	/* Everything else is as supplied by the user. */
	begin =	offsetof(struct pisp_be_config, global.bayer_order) /
		sizeof(u32);
	end = sizeof(struct pisp_be_config) / sizeof(u32);
	for (unsigned int u = begin; u < end; u++)
		pispbe_wr(pispbe, PISP_BE_CONFIG_BASE_REG + sizeof(u32) * u,
			  ((u32 *)job->config)[u]);

	/* Read back the addresses -- an error here could be fatal */
	for (unsigned int u = 0; u < N_HW_ADDRESSES; ++u) {
		unsigned int offset = PISP_BE_IO_ADDR_LOW(u);
		u64 along = pispbe_rd(pispbe, offset);

		along += ((u64)pispbe_rd(pispbe, offset + 4)) << 32;
		if (along != (u64)(job->hw_dma_addrs[u])) {
			dev_dbg(pispbe->dev,
				"ISP BE config error: check if ISP RAMs enabled?\n");
			return;
		}
	}

	/*
	 * Write tile pointer to hardware. The IOMMU should prevent
	 * out-of-bounds offsets reaching non-ISP buffers.
	 */
	pispbe_wr(pispbe, PISP_BE_TILE_ADDR_LO_REG, lower_32_bits(job->tiles));
	pispbe_wr(pispbe, PISP_BE_TILE_ADDR_HI_REG, upper_32_bits(job->tiles));

	/* Enqueue the job */
	pispbe_wr(pispbe, PISP_BE_CONTROL_REG,
		  PISP_BE_CONTROL_COPY_CONFIG | PISP_BE_CONTROL_QUEUE_JOB |
		  PISP_BE_CONTROL_NUM_TILES(job->config->num_tiles));
}

struct pispbe_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head ready_list;
	unsigned int config_index;
};

static int pispbe_get_planes_addr(dma_addr_t addr[3], struct pispbe_buffer *buf,
				  struct pispbe_node *node)
{
	unsigned int num_planes = node->format.fmt.pix_mp.num_planes;
	unsigned int plane_factor = 0;
	unsigned int size;
	unsigned int p;

	if (!buf || !node->pisp_format)
		return 0;

	/*
	 * Determine the base plane size. This will not be the same
	 * as node->format.fmt.pix_mp.plane_fmt[0].sizeimage for a single
	 * plane buffer in an mplane format.
	 */
	size = node->format.fmt.pix_mp.plane_fmt[0].bytesperline *
	       node->format.fmt.pix_mp.height;

	for (p = 0; p < num_planes && p < PISPBE_MAX_PLANES; p++) {
		addr[p] = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, p);
		plane_factor += node->pisp_format->plane_factor[p];
	}

	for (; p < PISPBE_MAX_PLANES && node->pisp_format->plane_factor[p]; p++) {
		/*
		 * Calculate the address offset of this plane as needed
		 * by the hardware. This is specifically for non-mplane
		 * buffer formats, where there are 3 image planes, e.g.
		 * for the V4L2_PIX_FMT_YUV420 format.
		 */
		addr[p] = addr[0] + ((size * plane_factor) >> 3);
		plane_factor += node->pisp_format->plane_factor[p];
	}

	return num_planes;
}

static dma_addr_t pispbe_get_addr(struct pispbe_buffer *buf)
{
	if (buf)
		return vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);

	return 0;
}

static void pispbe_xlate_addrs(struct pispbe_dev *pispbe,
			       struct pispbe_job_descriptor *job,
			       struct pispbe_buffer *buf[PISPBE_NUM_NODES])
{
	struct pispbe_hw_enables *hw_en = &job->hw_enables;
	struct pisp_be_tiles_config *config = job->config;
	dma_addr_t *addrs = job->hw_dma_addrs;
	int ret;

	/* Take a copy of the "enable" bitmaps so we can modify them. */
	hw_en->bayer_enables = config->config.global.bayer_enables;
	hw_en->rgb_enables = config->config.global.rgb_enables;

	/*
	 * Main input first. There are 3 address pointers, corresponding to up
	 * to 3 planes.
	 */
	ret = pispbe_get_planes_addr(addrs, buf[MAIN_INPUT_NODE],
				     &pispbe->node[MAIN_INPUT_NODE]);
	if (ret <= 0) {
		/*
		 * This shouldn't happen; pispbe_schedule_internal should insist
		 * on an input.
		 */
		dev_warn(pispbe->dev, "ISP-BE missing input\n");
		hw_en->bayer_enables = 0;
		hw_en->rgb_enables = 0;
		return;
	}

	/*
	 * Now TDN/Stitch inputs and outputs. These are single-plane and only
	 * used with Bayer input. Input enables must match the requirements
	 * of the processing stages, otherwise the hardware can lock up!
	 */
	if (hw_en->bayer_enables & PISP_BE_BAYER_ENABLE_INPUT) {
		addrs[3] = pispbe_get_addr(buf[TDN_INPUT_NODE]);
		if (addrs[3] == 0 ||
		    !(hw_en->bayer_enables & PISP_BE_BAYER_ENABLE_TDN_INPUT) ||
		    !(hw_en->bayer_enables & PISP_BE_BAYER_ENABLE_TDN) ||
		    (config->config.tdn.reset & 1)) {
			hw_en->bayer_enables &=
				~(PISP_BE_BAYER_ENABLE_TDN_INPUT |
				  PISP_BE_BAYER_ENABLE_TDN_DECOMPRESS);
			if (!(config->config.tdn.reset & 1))
				hw_en->bayer_enables &=
					~PISP_BE_BAYER_ENABLE_TDN;
		}

		addrs[4] = pispbe_get_addr(buf[STITCH_INPUT_NODE]);
		if (addrs[4] == 0 ||
		    !(hw_en->bayer_enables & PISP_BE_BAYER_ENABLE_STITCH_INPUT) ||
		    !(hw_en->bayer_enables & PISP_BE_BAYER_ENABLE_STITCH)) {
			hw_en->bayer_enables &=
				~(PISP_BE_BAYER_ENABLE_STITCH_INPUT |
				  PISP_BE_BAYER_ENABLE_STITCH_DECOMPRESS |
				  PISP_BE_BAYER_ENABLE_STITCH);
		}

		addrs[5] = pispbe_get_addr(buf[TDN_OUTPUT_NODE]);
		if (addrs[5] == 0)
			hw_en->bayer_enables &=
				~(PISP_BE_BAYER_ENABLE_TDN_COMPRESS |
				  PISP_BE_BAYER_ENABLE_TDN_OUTPUT);

		addrs[6] = pispbe_get_addr(buf[STITCH_OUTPUT_NODE]);
		if (addrs[6] == 0)
			hw_en->bayer_enables &=
				~(PISP_BE_BAYER_ENABLE_STITCH_COMPRESS |
				  PISP_BE_BAYER_ENABLE_STITCH_OUTPUT);
	} else {
		/* No Bayer input? Disable entire Bayer pipe (else lockup) */
		hw_en->bayer_enables = 0;
	}

	/* Main image output channels. */
	for (unsigned int i = 0; i < PISP_BACK_END_NUM_OUTPUTS; i++) {
		ret = pispbe_get_planes_addr(addrs + 7 + 3 * i,
					     buf[OUTPUT0_NODE + i],
					     &pispbe->node[OUTPUT0_NODE + i]);
		if (ret <= 0)
			hw_en->rgb_enables &= ~(PISP_BE_RGB_ENABLE_OUTPUT0 << i);
	}
}

/*
 * Prepare a job description to be submitted to the HW.
 *
 * To schedule a job, we need all streaming nodes (apart from Output0,
 * Output1, Tdn and Stitch) to have a buffer ready, which must
 * include at least a config buffer and a main input image.
 *
 * For Output0, Output1, Tdn and Stitch, a buffer only needs to be
 * available if the blocks are enabled in the config.
 *
 * Needs to be called with hw_lock held.
 *
 * Returns 0 if a job has been successfully prepared, < 0 otherwise.
 */
static int pispbe_prepare_job(struct pispbe_dev *pispbe,
			      struct pispbe_job_descriptor *job)
{
	struct pispbe_buffer *buf[PISPBE_NUM_NODES] = {};
	unsigned int config_index;
	struct pispbe_node *node;
	unsigned long flags;

	lockdep_assert_held(&pispbe->hw_lock);

	memset(job, 0, sizeof(struct pispbe_job_descriptor));

	if (((BIT(CONFIG_NODE) | BIT(MAIN_INPUT_NODE)) &
		pispbe->streaming_map) !=
			(BIT(CONFIG_NODE) | BIT(MAIN_INPUT_NODE)))
		return -ENODEV;

	node = &pispbe->node[CONFIG_NODE];
	spin_lock_irqsave(&node->ready_lock, flags);
	buf[CONFIG_NODE] = list_first_entry_or_null(&node->ready_queue,
						    struct pispbe_buffer,
						    ready_list);
	if (buf[CONFIG_NODE]) {
		list_del(&buf[CONFIG_NODE]->ready_list);
		pispbe->queued_job.buf[CONFIG_NODE] = buf[CONFIG_NODE];
	}
	spin_unlock_irqrestore(&node->ready_lock, flags);

	/* Exit early if no config buffer has been queued. */
	if (!buf[CONFIG_NODE])
		return -ENODEV;

	config_index = buf[CONFIG_NODE]->vb.vb2_buf.index;
	job->config = &pispbe->config[config_index];
	job->tiles = pispbe->config_dma_addr +
		     config_index * sizeof(struct pisp_be_tiles_config) +
		     offsetof(struct pisp_be_tiles_config, tiles);

	/* remember: srcimages, captures then metadata */
	for (unsigned int i = 0; i < PISPBE_NUM_NODES; i++) {
		unsigned int bayer_en =
			job->config->config.global.bayer_enables;
		unsigned int rgb_en =
			job->config->config.global.rgb_enables;
		bool ignore_buffers = false;

		/* Config node is handled outside the loop above. */
		if (i == CONFIG_NODE)
			continue;

		buf[i] = NULL;
		if (!(pispbe->streaming_map & BIT(i)))
			continue;

		if ((!(rgb_en & PISP_BE_RGB_ENABLE_OUTPUT0) &&
		     i == OUTPUT0_NODE) ||
		    (!(rgb_en & PISP_BE_RGB_ENABLE_OUTPUT1) &&
		     i == OUTPUT1_NODE) ||
		    (!(bayer_en & PISP_BE_BAYER_ENABLE_TDN_INPUT) &&
		     i == TDN_INPUT_NODE) ||
		    (!(bayer_en & PISP_BE_BAYER_ENABLE_TDN_OUTPUT) &&
		     i == TDN_OUTPUT_NODE) ||
		    (!(bayer_en & PISP_BE_BAYER_ENABLE_STITCH_INPUT) &&
		     i == STITCH_INPUT_NODE) ||
		    (!(bayer_en & PISP_BE_BAYER_ENABLE_STITCH_OUTPUT) &&
		     i == STITCH_OUTPUT_NODE)) {
			/*
			 * Ignore Output0/Output1/Tdn/Stitch buffer check if the
			 * global enables aren't set for these blocks. If a
			 * buffer has been provided, we dequeue it back to the
			 * user with the other in-use buffers.
			 */
			ignore_buffers = true;
		}

		node = &pispbe->node[i];

		/* Pull a buffer from each V4L2 queue to form the queued job */
		spin_lock_irqsave(&node->ready_lock, flags);
		buf[i] = list_first_entry_or_null(&node->ready_queue,
						  struct pispbe_buffer,
						  ready_list);
		if (buf[i]) {
			list_del(&buf[i]->ready_list);
			pispbe->queued_job.buf[i] = buf[i];
		}
		spin_unlock_irqrestore(&node->ready_lock, flags);

		if (!buf[i] && !ignore_buffers)
			goto err_return_buffers;
	}

	pispbe->queued_job.valid = true;

	/* Convert buffers to DMA addresses for the hardware */
	pispbe_xlate_addrs(pispbe, job, buf);

	return 0;

err_return_buffers:
	for (unsigned int i = 0; i < PISPBE_NUM_NODES; i++) {
		struct pispbe_node *n =  &pispbe->node[i];

		if (!buf[i])
			continue;

		/* Return the buffer to the ready_list queue */
		spin_lock_irqsave(&n->ready_lock, flags);
		list_add(&buf[i]->ready_list, &n->ready_queue);
		spin_unlock_irqrestore(&n->ready_lock, flags);
	}

	memset(&pispbe->queued_job, 0, sizeof(pispbe->queued_job));

	return -ENODEV;
}

static void pispbe_schedule(struct pispbe_dev *pispbe, bool clear_hw_busy)
{
	struct pispbe_job_descriptor job;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pispbe->hw_lock, flags);

	if (clear_hw_busy)
		pispbe->hw_busy = false;

	if (pispbe->hw_busy)
		goto unlock_and_return;

	ret = pispbe_prepare_job(pispbe, &job);
	if (ret)
		goto unlock_and_return;

	/*
	 * We can kick the job off without the hw_lock, as this can
	 * never run again until hw_busy is cleared, which will happen
	 * only when the following job has been queued and an interrupt
	 * is rised.
	 */
	pispbe->hw_busy = true;
	spin_unlock_irqrestore(&pispbe->hw_lock, flags);

	if (job.config->num_tiles <= 0 ||
	    job.config->num_tiles > PISP_BACK_END_NUM_TILES ||
	    !((job.hw_enables.bayer_enables | job.hw_enables.rgb_enables) &
	      PISP_BE_BAYER_ENABLE_INPUT)) {
		/*
		 * Bad job. We can't let it proceed as it could lock up
		 * the hardware, or worse!
		 *
		 * For now, just force num_tiles to 0, which causes the
		 * H/W to do something bizarre but survivable. It
		 * increments (started,done) counters by more than 1,
		 * but we seem to survive...
		 */
		dev_dbg(pispbe->dev, "Bad job: invalid number of tiles: %u\n",
			job.config->num_tiles);
		job.config->num_tiles = 0;
	}

	pispbe_queue_job(pispbe, &job);

	return;

unlock_and_return:
	/* No job has been queued, just release the lock and return. */
	spin_unlock_irqrestore(&pispbe->hw_lock, flags);
}

static void pispbe_isr_jobdone(struct pispbe_dev *pispbe,
			       struct pispbe_job *job)
{
	struct pispbe_buffer **buf = job->buf;
	u64 ts = ktime_get_ns();

	for (unsigned int i = 0; i < PISPBE_NUM_NODES; i++) {
		if (buf[i]) {
			buf[i]->vb.vb2_buf.timestamp = ts;
			buf[i]->vb.sequence = pispbe->sequence;
			vb2_buffer_done(&buf[i]->vb.vb2_buf,
					VB2_BUF_STATE_DONE);
		}
	}

	pispbe->sequence++;
}

static irqreturn_t pispbe_isr(int irq, void *dev)
{
	struct pispbe_dev *pispbe = (struct pispbe_dev *)dev;
	bool can_queue_another = false;
	u8 started, done;
	u32 u;

	u = pispbe_rd(pispbe, PISP_BE_INTERRUPT_STATUS_REG);
	if (u == 0)
		return IRQ_NONE;

	pispbe_wr(pispbe, PISP_BE_INTERRUPT_STATUS_REG, u);
	u = pispbe_rd(pispbe, PISP_BE_BATCH_STATUS_REG);
	done = (uint8_t)u;
	started = (uint8_t)(u >> 8);

	/*
	 * Be aware that done can go up by 2 and started by 1 when: a job that
	 * we previously saw "start" now finishes, and we then queued a new job
	 * which we see both start and finish "simultaneously".
	 */
	if (pispbe->running_job.valid && pispbe->done != done) {
		pispbe_isr_jobdone(pispbe, &pispbe->running_job);
		memset(&pispbe->running_job, 0, sizeof(pispbe->running_job));
		pispbe->done++;
	}

	if (pispbe->started != started) {
		pispbe->started++;
		can_queue_another = 1;

		if (pispbe->done != done && pispbe->queued_job.valid) {
			pispbe_isr_jobdone(pispbe, &pispbe->queued_job);
			pispbe->done++;
		} else {
			pispbe->running_job = pispbe->queued_job;
		}

		memset(&pispbe->queued_job, 0, sizeof(pispbe->queued_job));
	}

	if (pispbe->done != done || pispbe->started != started) {
		dev_dbg(pispbe->dev,
			"Job counters not matching: done = %u, expected %u - started = %u, expected %u\n",
			pispbe->done, done, pispbe->started, started);
		pispbe->started = started;
		pispbe->done = done;
	}

	/* check if there's more to do before going to sleep */
	pispbe_schedule(pispbe, can_queue_another);

	return IRQ_HANDLED;
}

static int pisp_be_validate_config(struct pispbe_dev *pispbe,
				   struct pisp_be_tiles_config *config)
{
	u32 bayer_enables = config->config.global.bayer_enables;
	u32 rgb_enables = config->config.global.rgb_enables;
	struct device *dev = pispbe->dev;
	struct v4l2_format *fmt;
	unsigned int bpl, size;

	if (!(bayer_enables & PISP_BE_BAYER_ENABLE_INPUT) ==
	    !(rgb_enables & PISP_BE_RGB_ENABLE_INPUT)) {
		dev_dbg(dev, "%s: Not one input enabled\n", __func__);
		return -EIO;
	}

	/* Ensure output config strides and buffer sizes match the V4L2 formats. */
	fmt = &pispbe->node[TDN_OUTPUT_NODE].format;
	if (bayer_enables & PISP_BE_BAYER_ENABLE_TDN_OUTPUT) {
		bpl = config->config.tdn_output_format.stride;
		size = bpl * config->config.tdn_output_format.height;

		if (fmt->fmt.pix_mp.plane_fmt[0].bytesperline < bpl) {
			dev_dbg(dev, "%s: bpl mismatch on tdn_output\n",
				__func__);
			return -EINVAL;
		}

		if (fmt->fmt.pix_mp.plane_fmt[0].sizeimage < size) {
			dev_dbg(dev, "%s: size mismatch on tdn_output\n",
				__func__);
			return -EINVAL;
		}
	}

	fmt = &pispbe->node[STITCH_OUTPUT_NODE].format;
	if (bayer_enables & PISP_BE_BAYER_ENABLE_STITCH_OUTPUT) {
		bpl = config->config.stitch_output_format.stride;
		size = bpl * config->config.stitch_output_format.height;

		if (fmt->fmt.pix_mp.plane_fmt[0].bytesperline < bpl) {
			dev_dbg(dev, "%s: bpl mismatch on stitch_output\n",
				__func__);
			return -EINVAL;
		}

		if (fmt->fmt.pix_mp.plane_fmt[0].sizeimage < size) {
			dev_dbg(dev, "%s: size mismatch on stitch_output\n",
				__func__);
			return -EINVAL;
		}
	}

	for (unsigned int j = 0; j < PISP_BACK_END_NUM_OUTPUTS; j++) {
		if (!(rgb_enables & PISP_BE_RGB_ENABLE_OUTPUT(j)))
			continue;

		if (config->config.output_format[j].image.format &
		    PISP_IMAGE_FORMAT_WALLPAPER_ROLL)
			continue; /* TODO: Size checks for wallpaper formats */

		fmt = &pispbe->node[OUTPUT0_NODE + j].format;
		for (unsigned int i = 0; i < fmt->fmt.pix_mp.num_planes; i++) {
			bpl = !i ? config->config.output_format[j].image.stride
			    : config->config.output_format[j].image.stride2;
			size = bpl * config->config.output_format[j].image.height;

			if (config->config.output_format[j].image.format &
						PISP_IMAGE_FORMAT_SAMPLING_420)
				size >>= 1;

			if (fmt->fmt.pix_mp.plane_fmt[i].bytesperline < bpl) {
				dev_dbg(dev, "%s: bpl mismatch on output %d\n",
					__func__, j);
				return -EINVAL;
			}

			if (fmt->fmt.pix_mp.plane_fmt[i].sizeimage < size) {
				dev_dbg(dev, "%s: size mismatch on output\n",
					__func__);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int pispbe_node_queue_setup(struct vb2_queue *q, unsigned int *nbuffers,
				   unsigned int *nplanes, unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct pispbe_node *node = vb2_get_drv_priv(q);
	struct pispbe_dev *pispbe = node->pispbe;
	unsigned int num_planes = NODE_IS_MPLANE(node) ?
				  node->format.fmt.pix_mp.num_planes : 1;

	if (*nplanes) {
		if (*nplanes != num_planes)
			return -EINVAL;

		for (unsigned int i = 0; i < *nplanes; i++) {
			unsigned int size = NODE_IS_MPLANE(node) ?
				node->format.fmt.pix_mp.plane_fmt[i].sizeimage :
				node->format.fmt.meta.buffersize;

			if (sizes[i] < size)
				return -EINVAL;
		}

		return 0;
	}

	*nplanes = num_planes;
	for (unsigned int i = 0; i < *nplanes; i++) {
		unsigned int size = NODE_IS_MPLANE(node) ?
				node->format.fmt.pix_mp.plane_fmt[i].sizeimage :
				node->format.fmt.meta.buffersize;
		sizes[i] = size;
	}

	dev_dbg(pispbe->dev,
		"Image (or metadata) size %u, nbuffers %u for node %s\n",
		sizes[0], *nbuffers, NODE_NAME(node));

	return 0;
}

static int pispbe_node_buffer_prepare(struct vb2_buffer *vb)
{
	struct pispbe_node *node = vb2_get_drv_priv(vb->vb2_queue);
	struct pispbe_dev *pispbe = node->pispbe;
	unsigned int num_planes = NODE_IS_MPLANE(node) ?
				  node->format.fmt.pix_mp.num_planes : 1;

	for (unsigned int i = 0; i < num_planes; i++) {
		unsigned long size = NODE_IS_MPLANE(node) ?
				node->format.fmt.pix_mp.plane_fmt[i].sizeimage :
				node->format.fmt.meta.buffersize;

		if (vb2_plane_size(vb, i) < size) {
			dev_dbg(pispbe->dev,
				"data will not fit into plane %d (%lu < %lu)\n",
				i, vb2_plane_size(vb, i), size);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i, size);
	}

	if (node->id == CONFIG_NODE) {
		void *dst = &node->pispbe->config[vb->index];
		void *src = vb2_plane_vaddr(vb, 0);

		memcpy(dst, src, sizeof(struct pisp_be_tiles_config));

		return pisp_be_validate_config(pispbe, dst);
	}

	return 0;
}

static void pispbe_node_buffer_queue(struct vb2_buffer *buf)
{
	struct vb2_v4l2_buffer *vbuf =
		container_of(buf, struct vb2_v4l2_buffer, vb2_buf);
	struct pispbe_buffer *buffer =
		container_of(vbuf, struct pispbe_buffer, vb);
	struct pispbe_node *node = vb2_get_drv_priv(buf->vb2_queue);
	struct pispbe_dev *pispbe = node->pispbe;
	unsigned long flags;

	dev_dbg(pispbe->dev, "%s: for node %s\n", __func__, NODE_NAME(node));
	spin_lock_irqsave(&node->ready_lock, flags);
	list_add_tail(&buffer->ready_list, &node->ready_queue);
	spin_unlock_irqrestore(&node->ready_lock, flags);

	/*
	 * Every time we add a buffer, check if there's now some work for the hw
	 * to do.
	 */
	pispbe_schedule(pispbe, false);
}

static int pispbe_node_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct pispbe_node *node = vb2_get_drv_priv(q);
	struct pispbe_dev *pispbe = node->pispbe;
	struct pispbe_buffer *buf, *tmp;
	unsigned long flags;
	int ret;

	ret = pm_runtime_resume_and_get(pispbe->dev);
	if (ret < 0)
		goto err_return_buffers;

	spin_lock_irqsave(&pispbe->hw_lock, flags);
	node->pispbe->streaming_map |=  BIT(node->id);
	node->pispbe->sequence = 0;
	spin_unlock_irqrestore(&pispbe->hw_lock, flags);

	dev_dbg(pispbe->dev, "%s: for node %s (count %u)\n",
		__func__, NODE_NAME(node), count);
	dev_dbg(pispbe->dev, "Nodes streaming now 0x%x\n",
		node->pispbe->streaming_map);

	/* Maybe we're ready to run. */
	pispbe_schedule(pispbe, false);

	return 0;

err_return_buffers:
	spin_lock_irqsave(&pispbe->hw_lock, flags);
	list_for_each_entry_safe(buf, tmp, &node->ready_queue, ready_list) {
		list_del(&buf->ready_list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&pispbe->hw_lock, flags);

	return ret;
}

static void pispbe_node_stop_streaming(struct vb2_queue *q)
{
	struct pispbe_node *node = vb2_get_drv_priv(q);
	struct pispbe_dev *pispbe = node->pispbe;
	struct pispbe_buffer *buf;
	unsigned long flags;

	/*
	 * Now this is a bit awkward. In a simple M2M device we could just wait
	 * for all queued jobs to complete, but here there's a risk that a
	 * partial set of buffers was queued and cannot be run. For now, just
	 * cancel all buffers stuck in the "ready queue", then wait for any
	 * running job.
	 *
	 * This may return buffers out of order.
	 */
	dev_dbg(pispbe->dev, "%s: for node %s\n", __func__, NODE_NAME(node));
	spin_lock_irqsave(&pispbe->hw_lock, flags);
	do {
		unsigned long flags1;

		spin_lock_irqsave(&node->ready_lock, flags1);
		buf = list_first_entry_or_null(&node->ready_queue,
					       struct pispbe_buffer,
					       ready_list);
		if (buf) {
			list_del(&buf->ready_list);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		}
		spin_unlock_irqrestore(&node->ready_lock, flags1);
	} while (buf);
	spin_unlock_irqrestore(&pispbe->hw_lock, flags);

	vb2_wait_for_all_buffers(&node->queue);

	spin_lock_irqsave(&pispbe->hw_lock, flags);
	pispbe->streaming_map &= ~BIT(node->id);
	spin_unlock_irqrestore(&pispbe->hw_lock, flags);

	pm_runtime_mark_last_busy(pispbe->dev);
	pm_runtime_put_autosuspend(pispbe->dev);

	dev_dbg(pispbe->dev, "Nodes streaming now 0x%x\n",
		pispbe->streaming_map);
}

static const struct vb2_ops pispbe_node_queue_ops = {
	.queue_setup = pispbe_node_queue_setup,
	.buf_prepare = pispbe_node_buffer_prepare,
	.buf_queue = pispbe_node_buffer_queue,
	.start_streaming = pispbe_node_start_streaming,
	.stop_streaming = pispbe_node_stop_streaming,
};

static const struct v4l2_file_operations pispbe_fops = {
	.owner          = THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = vb2_fop_release,
	.poll           = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap
};

static int pispbe_node_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;

	strscpy(cap->driver, PISPBE_NAME, sizeof(cap->driver));
	strscpy(cap->card, PISPBE_NAME, sizeof(cap->card));

	dev_dbg(pispbe->dev, "Caps for node %s: %x and %x (dev %x)\n",
		NODE_NAME(node), cap->capabilities, cap->device_caps,
		node->vfd.device_caps);

	return 0;
}

static int pispbe_node_g_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;

	if (!NODE_IS_CAPTURE(node) || NODE_IS_META(node)) {
		dev_dbg(pispbe->dev,
			"Cannot get capture fmt for output node %s\n",
			NODE_NAME(node));
		return -EINVAL;
	}

	*f = node->format;
	dev_dbg(pispbe->dev, "Get capture format for node %s\n",
		NODE_NAME(node));

	return 0;
}

static int pispbe_node_g_fmt_vid_out(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;

	if (NODE_IS_CAPTURE(node) || NODE_IS_META(node)) {
		dev_dbg(pispbe->dev,
			"Cannot get capture fmt for output node %s\n",
			 NODE_NAME(node));
		return -EINVAL;
	}

	*f = node->format;
	dev_dbg(pispbe->dev, "Get output format for node %s\n",
		NODE_NAME(node));

	return 0;
}

static int pispbe_node_g_fmt_meta_out(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;

	if (!NODE_IS_META(node) || NODE_IS_CAPTURE(node)) {
		dev_dbg(pispbe->dev,
			"Cannot get capture fmt for meta output node %s\n",
			NODE_NAME(node));
		return -EINVAL;
	}

	*f = node->format;
	dev_dbg(pispbe->dev, "Get output format for meta node %s\n",
		NODE_NAME(node));

	return 0;
}

static const struct pisp_be_format *pispbe_find_fmt(unsigned int fourcc)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(supported_formats); i++) {
		if (supported_formats[i].fourcc == fourcc)
			return &supported_formats[i];
	}

	return NULL;
}

static void pispbe_set_plane_params(struct v4l2_format *f,
				    const struct pisp_be_format *fmt)
{
	unsigned int nplanes = f->fmt.pix_mp.num_planes;
	unsigned int total_plane_factor = 0;

	for (unsigned int i = 0; i < PISPBE_MAX_PLANES; i++)
		total_plane_factor += fmt->plane_factor[i];

	for (unsigned int i = 0; i < nplanes; i++) {
		struct v4l2_plane_pix_format *p = &f->fmt.pix_mp.plane_fmt[i];
		unsigned int bpl, plane_size;

		bpl = (f->fmt.pix_mp.width * fmt->bit_depth) >> 3;
		bpl = ALIGN(max(p->bytesperline, bpl), fmt->align);

		plane_size = bpl * f->fmt.pix_mp.height *
		      (nplanes > 1 ? fmt->plane_factor[i] : total_plane_factor);
		/*
		 * The shift is to divide out the plane_factor fixed point
		 * scaling of 8.
		 */
		plane_size = max(p->sizeimage, plane_size >> 3);

		p->bytesperline = bpl;
		p->sizeimage = plane_size;
	}
}

static void pispbe_try_format(struct v4l2_format *f, struct pispbe_node *node)
{
	struct pispbe_dev *pispbe = node->pispbe;
	u32 pixfmt = f->fmt.pix_mp.pixelformat;
	const struct pisp_be_format *fmt;
	bool is_rgb;

	dev_dbg(pispbe->dev,
		"%s: [%s] req %ux%u %p4cc, planes %d\n",
		__func__, NODE_NAME(node), f->fmt.pix_mp.width,
		f->fmt.pix_mp.height, &pixfmt,
		f->fmt.pix_mp.num_planes);

	fmt = pispbe_find_fmt(pixfmt);
	if (!fmt) {
		dev_dbg(pispbe->dev,
			"%s: [%s] Format not found, defaulting to YUV420\n",
			__func__, NODE_NAME(node));
		fmt = pispbe_find_fmt(V4L2_PIX_FMT_YUV420);
	}

	f->fmt.pix_mp.pixelformat = fmt->fourcc;
	f->fmt.pix_mp.num_planes = fmt->num_planes;
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.width = max(min(f->fmt.pix_mp.width, 65536u),
				  PISP_BACK_END_MIN_TILE_WIDTH);
	f->fmt.pix_mp.height = max(min(f->fmt.pix_mp.height, 65536u),
				   PISP_BACK_END_MIN_TILE_HEIGHT);

	/*
	 * Fill in the actual colour space when the requested one was
	 * not supported. This also catches the case when the "default"
	 * colour space was requested (as that's never in the mask).
	 */
	if (!(V4L2_COLORSPACE_MASK(f->fmt.pix_mp.colorspace) &
	    fmt->colorspace_mask))
		f->fmt.pix_mp.colorspace = fmt->colorspace_default;

	/* In all cases, we only support the defaults for these: */
	f->fmt.pix_mp.ycbcr_enc =
		V4L2_MAP_YCBCR_ENC_DEFAULT(f->fmt.pix_mp.colorspace);
	f->fmt.pix_mp.xfer_func =
		V4L2_MAP_XFER_FUNC_DEFAULT(f->fmt.pix_mp.colorspace);

	is_rgb = f->fmt.pix_mp.colorspace == V4L2_COLORSPACE_SRGB;
	f->fmt.pix_mp.quantization =
		V4L2_MAP_QUANTIZATION_DEFAULT(is_rgb, f->fmt.pix_mp.colorspace,
					      f->fmt.pix_mp.ycbcr_enc);

	/* Set plane size and bytes/line for each plane. */
	pispbe_set_plane_params(f, fmt);

	for (unsigned int i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		dev_dbg(pispbe->dev,
			"%s: [%s] calc plane %d, %ux%u, depth %u, bpl %u size %u\n",
			__func__, NODE_NAME(node), i, f->fmt.pix_mp.width,
			f->fmt.pix_mp.height, fmt->bit_depth,
			f->fmt.pix_mp.plane_fmt[i].bytesperline,
			f->fmt.pix_mp.plane_fmt[i].sizeimage);
	}
}

static int pispbe_node_try_fmt_vid_cap(struct file *file, void *priv,
				       struct v4l2_format *f)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;

	if (!NODE_IS_CAPTURE(node) || NODE_IS_META(node)) {
		dev_dbg(pispbe->dev,
			"Cannot set capture fmt for output node %s\n",
			NODE_NAME(node));
		return -EINVAL;
	}

	pispbe_try_format(f, node);

	return 0;
}

static int pispbe_node_try_fmt_vid_out(struct file *file, void *priv,
				       struct v4l2_format *f)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;

	if (!NODE_IS_OUTPUT(node) || NODE_IS_META(node)) {
		dev_dbg(pispbe->dev,
			"Cannot set capture fmt for output node %s\n",
			NODE_NAME(node));
		return -EINVAL;
	}

	pispbe_try_format(f, node);

	return 0;
}

static int pispbe_node_try_fmt_meta_out(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;

	if (!NODE_IS_META(node) || NODE_IS_CAPTURE(node)) {
		dev_dbg(pispbe->dev,
			"Cannot set capture fmt for meta output node %s\n",
			NODE_NAME(node));
		return -EINVAL;
	}

	f->fmt.meta.dataformat = V4L2_META_FMT_RPI_BE_CFG;
	f->fmt.meta.buffersize = sizeof(struct pisp_be_tiles_config);

	return 0;
}

static int pispbe_node_s_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;
	int ret;

	ret = pispbe_node_try_fmt_vid_cap(file, priv, f);
	if (ret < 0)
		return ret;

	if (vb2_is_busy(&node->queue))
		return -EBUSY;

	node->format = *f;
	node->pisp_format = pispbe_find_fmt(f->fmt.pix_mp.pixelformat);

	dev_dbg(pispbe->dev, "Set capture format for node %s to %p4cc\n",
		NODE_NAME(node), &f->fmt.pix_mp.pixelformat);

	return 0;
}

static int pispbe_node_s_fmt_vid_out(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;
	int ret;

	ret = pispbe_node_try_fmt_vid_out(file, priv, f);
	if (ret < 0)
		return ret;

	if (vb2_is_busy(&node->queue))
		return -EBUSY;

	node->format = *f;
	node->pisp_format = pispbe_find_fmt(f->fmt.pix_mp.pixelformat);

	dev_dbg(pispbe->dev, "Set output format for node %s to %p4cc\n",
		NODE_NAME(node), &f->fmt.pix_mp.pixelformat);

	return 0;
}

static int pispbe_node_s_fmt_meta_out(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;
	int ret;

	ret = pispbe_node_try_fmt_meta_out(file, priv, f);
	if (ret < 0)
		return ret;

	if (vb2_is_busy(&node->queue))
		return -EBUSY;

	node->format = *f;
	node->pisp_format = &meta_out_supported_formats[0];

	dev_dbg(pispbe->dev, "Set output format for meta node %s to %p4cc\n",
		NODE_NAME(node), &f->fmt.meta.dataformat);

	return 0;
}

static int pispbe_node_enum_fmt(struct file *file, void  *priv,
				struct v4l2_fmtdesc *f)
{
	struct pispbe_node *node = video_drvdata(file);

	if (f->type != node->queue.type)
		return -EINVAL;

	if (NODE_IS_META(node)) {
		if (f->index)
			return -EINVAL;

		f->pixelformat = V4L2_META_FMT_RPI_BE_CFG;
		f->flags = 0;
		return 0;
	}

	if (f->index >= ARRAY_SIZE(supported_formats))
		return -EINVAL;

	f->pixelformat = supported_formats[f->index].fourcc;
	f->flags = 0;

	return 0;
}

static int pispbe_enum_framesizes(struct file *file, void *priv,
				  struct v4l2_frmsizeenum *fsize)
{
	struct pispbe_node *node = video_drvdata(file);
	struct pispbe_dev *pispbe = node->pispbe;

	if (NODE_IS_META(node) || fsize->index)
		return -EINVAL;

	if (!pispbe_find_fmt(fsize->pixel_format)) {
		dev_dbg(pispbe->dev, "Invalid pixel code: %x\n",
			fsize->pixel_format);
		return -EINVAL;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = 32;
	fsize->stepwise.max_width = 65535;
	fsize->stepwise.step_width = 2;

	fsize->stepwise.min_height = 32;
	fsize->stepwise.max_height = 65535;
	fsize->stepwise.step_height = 2;

	return 0;
}

static const struct v4l2_ioctl_ops pispbe_node_ioctl_ops = {
	.vidioc_querycap = pispbe_node_querycap,
	.vidioc_g_fmt_vid_cap_mplane = pispbe_node_g_fmt_vid_cap,
	.vidioc_g_fmt_vid_out_mplane = pispbe_node_g_fmt_vid_out,
	.vidioc_g_fmt_meta_out = pispbe_node_g_fmt_meta_out,
	.vidioc_try_fmt_vid_cap_mplane = pispbe_node_try_fmt_vid_cap,
	.vidioc_try_fmt_vid_out_mplane = pispbe_node_try_fmt_vid_out,
	.vidioc_try_fmt_meta_out = pispbe_node_try_fmt_meta_out,
	.vidioc_s_fmt_vid_cap_mplane = pispbe_node_s_fmt_vid_cap,
	.vidioc_s_fmt_vid_out_mplane = pispbe_node_s_fmt_vid_out,
	.vidioc_s_fmt_meta_out = pispbe_node_s_fmt_meta_out,
	.vidioc_enum_fmt_vid_cap = pispbe_node_enum_fmt,
	.vidioc_enum_fmt_vid_out = pispbe_node_enum_fmt,
	.vidioc_enum_fmt_meta_out = pispbe_node_enum_fmt,
	.vidioc_enum_framesizes = pispbe_enum_framesizes,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static const struct video_device pispbe_videodev = {
	.name = PISPBE_NAME,
	.vfl_dir = VFL_DIR_M2M, /* gets overwritten */
	.fops = &pispbe_fops,
	.ioctl_ops = &pispbe_node_ioctl_ops,
	.minor = -1,
	.release = video_device_release_empty,
};

static void pispbe_node_def_fmt(struct pispbe_node *node)
{
	if (NODE_IS_META(node) && NODE_IS_OUTPUT(node)) {
		/* Config node */
		struct v4l2_format *f = &node->format;

		f->fmt.meta.dataformat = V4L2_META_FMT_RPI_BE_CFG;
		f->fmt.meta.buffersize = sizeof(struct pisp_be_tiles_config);
		f->type = node->buf_type;
	} else {
		struct v4l2_format f = {
			.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420,
			.fmt.pix_mp.width = 1920,
			.fmt.pix_mp.height = 1080,
			.type = node->buf_type,
		};
		pispbe_try_format(&f, node);
		node->format = f;
	}

	node->pisp_format = pispbe_find_fmt(node->format.fmt.pix_mp.pixelformat);
}

/*
 * Initialise a struct pispbe_node and register it as /dev/video<N>
 * to represent one of the PiSP Back End's input or output streams.
 */
static int pispbe_init_node(struct pispbe_dev *pispbe, unsigned int id)
{
	bool output = NODE_DESC_IS_OUTPUT(&node_desc[id]);
	struct pispbe_node *node = &pispbe->node[id];
	struct media_entity *entity = &node->vfd.entity;
	struct video_device *vdev = &node->vfd;
	struct vb2_queue *q = &node->queue;
	int ret;

	node->id = id;
	node->pispbe = pispbe;
	node->buf_type = node_desc[id].buf_type;

	mutex_init(&node->node_lock);
	mutex_init(&node->queue_lock);
	INIT_LIST_HEAD(&node->ready_queue);
	spin_lock_init(&node->ready_lock);

	node->format.type = node->buf_type;
	pispbe_node_def_fmt(node);

	q->type = node->buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->mem_ops = &vb2_dma_contig_memops;
	q->drv_priv = node;
	q->ops = &pispbe_node_queue_ops;
	q->buf_struct_size = sizeof(struct pispbe_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->dev = pispbe->dev;
	/* get V4L2 to handle node->queue locking */
	q->lock = &node->queue_lock;

	ret = vb2_queue_init(q);
	if (ret < 0) {
		dev_err(pispbe->dev, "vb2_queue_init failed\n");
		goto err_mutex_destroy;
	}

	*vdev = pispbe_videodev; /* default initialization */
	strscpy(vdev->name, node_desc[id].ent_name, sizeof(vdev->name));
	vdev->v4l2_dev = &pispbe->v4l2_dev;
	vdev->vfl_dir = output ? VFL_DIR_TX : VFL_DIR_RX;
	/* get V4L2 to serialise our ioctls */
	vdev->lock = &node->node_lock;
	vdev->queue = &node->queue;
	vdev->device_caps = V4L2_CAP_STREAMING | node_desc[id].caps;

	node->pad.flags = output ? MEDIA_PAD_FL_SOURCE : MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(entity, 1, &node->pad);
	if (ret) {
		dev_err(pispbe->dev,
			"Failed to register media pads for %s device node\n",
			NODE_NAME(node));
		goto err_unregister_queue;
	}

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(pispbe->dev,
			"Failed to register video %s device node\n",
			NODE_NAME(node));
		goto err_unregister_queue;
	}
	video_set_drvdata(vdev, node);

	if (output)
		ret = media_create_pad_link(entity, 0, &pispbe->sd.entity,
					    id, MEDIA_LNK_FL_IMMUTABLE |
					    MEDIA_LNK_FL_ENABLED);
	else
		ret = media_create_pad_link(&pispbe->sd.entity, id, entity,
					    0, MEDIA_LNK_FL_IMMUTABLE |
					    MEDIA_LNK_FL_ENABLED);
	if (ret)
		goto err_unregister_video_dev;

	dev_dbg(pispbe->dev, "%s device node registered as /dev/video%d\n",
		NODE_NAME(node), node->vfd.num);

	return 0;

err_unregister_video_dev:
	video_unregister_device(&node->vfd);
err_unregister_queue:
	vb2_queue_release(&node->queue);
err_mutex_destroy:
	mutex_destroy(&node->node_lock);
	mutex_destroy(&node->queue_lock);
	return ret;
}

static const struct v4l2_subdev_pad_ops pispbe_pad_ops = {
	.link_validate = v4l2_subdev_link_validate_default,
};

static const struct v4l2_subdev_ops pispbe_sd_ops = {
	.pad = &pispbe_pad_ops,
};

static int pispbe_init_subdev(struct pispbe_dev *pispbe)
{
	struct v4l2_subdev *sd = &pispbe->sd;
	int ret;

	v4l2_subdev_init(sd, &pispbe_sd_ops);
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->owner = THIS_MODULE;
	sd->dev = pispbe->dev;
	strscpy(sd->name, PISPBE_NAME, sizeof(sd->name));

	for (unsigned int i = 0; i < PISPBE_NUM_NODES; i++)
		pispbe->pad[i].flags =
			NODE_DESC_IS_OUTPUT(&node_desc[i]) ?
			MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity, PISPBE_NUM_NODES,
				     pispbe->pad);
	if (ret)
		goto error;

	ret = v4l2_device_register_subdev(&pispbe->v4l2_dev, sd);
	if (ret)
		goto error;

	return 0;

error:
	media_entity_cleanup(&sd->entity);
	return ret;
}

static int pispbe_init_devices(struct pispbe_dev *pispbe)
{
	struct v4l2_device *v4l2_dev;
	struct media_device *mdev;
	unsigned int num_regist;
	int ret;

	/* Register v4l2_device and media_device */
	mdev = &pispbe->mdev;
	mdev->hw_revision = pispbe->hw_version;
	mdev->dev = pispbe->dev;
	strscpy(mdev->model, PISPBE_NAME, sizeof(mdev->model));
	media_device_init(mdev);

	v4l2_dev = &pispbe->v4l2_dev;
	v4l2_dev->mdev = &pispbe->mdev;
	strscpy(v4l2_dev->name, PISPBE_NAME, sizeof(v4l2_dev->name));

	ret = v4l2_device_register(pispbe->dev, v4l2_dev);
	if (ret)
		goto err_media_dev_cleanup;

	/* Register the PISPBE subdevice. */
	ret = pispbe_init_subdev(pispbe);
	if (ret)
		goto err_unregister_v4l2;

	/* Create device video nodes */
	for (num_regist = 0; num_regist < PISPBE_NUM_NODES; num_regist++) {
		ret = pispbe_init_node(pispbe, num_regist);
		if (ret)
			goto err_unregister_nodes;
	}

	ret = media_device_register(mdev);
	if (ret)
		goto err_unregister_nodes;

	pispbe->config =
		dma_alloc_coherent(pispbe->dev,
				   sizeof(struct pisp_be_tiles_config) *
					PISP_BE_NUM_CONFIG_BUFFERS,
				   &pispbe->config_dma_addr, GFP_KERNEL);
	if (!pispbe->config) {
		dev_err(pispbe->dev, "Unable to allocate cached config buffers.\n");
		ret = -ENOMEM;
		goto err_unregister_mdev;
	}

	return 0;

err_unregister_mdev:
	media_device_unregister(mdev);
err_unregister_nodes:
	while (num_regist-- > 0) {
		video_unregister_device(&pispbe->node[num_regist].vfd);
		vb2_queue_release(&pispbe->node[num_regist].queue);
	}
	v4l2_device_unregister_subdev(&pispbe->sd);
	media_entity_cleanup(&pispbe->sd.entity);
err_unregister_v4l2:
	v4l2_device_unregister(v4l2_dev);
err_media_dev_cleanup:
	media_device_cleanup(mdev);
	return ret;
}

static void pispbe_destroy_devices(struct pispbe_dev *pispbe)
{
	if (pispbe->config) {
		dma_free_coherent(pispbe->dev,
				  sizeof(struct pisp_be_tiles_config) *
					PISP_BE_NUM_CONFIG_BUFFERS,
				  pispbe->config,
				  pispbe->config_dma_addr);
	}

	dev_dbg(pispbe->dev, "Unregister from media controller\n");

	v4l2_device_unregister_subdev(&pispbe->sd);
	media_entity_cleanup(&pispbe->sd.entity);
	media_device_unregister(&pispbe->mdev);

	for (int i = PISPBE_NUM_NODES - 1; i >= 0; i--) {
		video_unregister_device(&pispbe->node[i].vfd);
		vb2_queue_release(&pispbe->node[i].queue);
		mutex_destroy(&pispbe->node[i].node_lock);
		mutex_destroy(&pispbe->node[i].queue_lock);
	}

	media_device_cleanup(&pispbe->mdev);
	v4l2_device_unregister(&pispbe->v4l2_dev);
}

static int pispbe_runtime_suspend(struct device *dev)
{
	struct pispbe_dev *pispbe = dev_get_drvdata(dev);

	clk_disable_unprepare(pispbe->clk);

	return 0;
}

static int pispbe_runtime_resume(struct device *dev)
{
	struct pispbe_dev *pispbe = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(pispbe->clk);
	if (ret) {
		dev_err(dev, "Unable to enable clock\n");
		return ret;
	}

	dev_dbg(dev, "%s: Enabled clock, rate=%lu\n",
		__func__, clk_get_rate(pispbe->clk));

	return 0;
}

static int pispbe_hw_init(struct pispbe_dev *pispbe)
{
	u32 u;

	/* Check the HW is present and has a known version */
	u = pispbe_rd(pispbe, PISP_BE_VERSION_REG);
	dev_dbg(pispbe->dev, "pispbe_probe: HW version:  0x%08x", u);
	pispbe->hw_version = u;
	if ((u & ~PISP_BE_VERSION_MINOR_BITS) != PISP_BE_VERSION_2712)
		return -ENODEV;

	/* Clear leftover interrupts */
	pispbe_wr(pispbe, PISP_BE_INTERRUPT_STATUS_REG, 0xFFFFFFFFu);
	u = pispbe_rd(pispbe, PISP_BE_BATCH_STATUS_REG);
	dev_dbg(pispbe->dev, "pispbe_probe: BatchStatus: 0x%08x", u);

	pispbe->done = (uint8_t)u;
	pispbe->started = (uint8_t)(u >> 8);
	u = pispbe_rd(pispbe, PISP_BE_STATUS_REG);
	dev_dbg(pispbe->dev, "pispbe_probe: Status:      0x%08x", u);

	if (u != 0 || pispbe->done != pispbe->started) {
		dev_err(pispbe->dev, "pispbe_probe: HW is stuck or busy\n");
		return -EBUSY;
	}

	/*
	 * AXI QOS=0, CACHE=4'b0010, PROT=3'b011
	 * Also set "chicken bits" 22:20 which enable sub-64-byte bursts
	 * and AXI AWID/BID variability (on versions which support this).
	 */
	pispbe_wr(pispbe, PISP_BE_AXI_REG, 0x32703200u);

	/* Enable both interrupt flags */
	pispbe_wr(pispbe, PISP_BE_INTERRUPT_EN_REG, 0x00000003u);

	return 0;
}

/* Probe the ISP-BE hardware block, as a single platform device. */
static int pispbe_probe(struct platform_device *pdev)
{
	struct pispbe_dev *pispbe;
	int ret;

	pispbe = devm_kzalloc(&pdev->dev, sizeof(*pispbe), GFP_KERNEL);
	if (!pispbe)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, pispbe);
	pispbe->dev = &pdev->dev;
	platform_set_drvdata(pdev, pispbe);

	pispbe->be_reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pispbe->be_reg_base)) {
		dev_err(&pdev->dev, "Failed to get ISP-BE registers address\n");
		return PTR_ERR(pispbe->be_reg_base);
	}

	pispbe->irq = platform_get_irq(pdev, 0);
	if (pispbe->irq <= 0)
		return -EINVAL;

	ret = devm_request_irq(&pdev->dev, pispbe->irq, pispbe_isr, 0,
			       PISPBE_NAME, pispbe);
	if (ret) {
		dev_err(&pdev->dev, "Unable to request interrupt\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(pispbe->dev, DMA_BIT_MASK(36));
	if (ret)
		return ret;

	pispbe->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pispbe->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(pispbe->clk),
				     "Failed to get clock");

	/* Hardware initialisation */
	pm_runtime_set_autosuspend_delay(pispbe->dev, 200);
	pm_runtime_use_autosuspend(pispbe->dev);
	pm_runtime_enable(pispbe->dev);

	ret = pispbe_runtime_resume(pispbe->dev);
	if (ret)
		goto pm_runtime_disable_err;

	pispbe->hw_busy = false;
	spin_lock_init(&pispbe->hw_lock);
	ret = pispbe_hw_init(pispbe);
	if (ret)
		goto pm_runtime_suspend_err;

	ret = pispbe_init_devices(pispbe);
	if (ret)
		goto disable_devs_err;

	pm_runtime_mark_last_busy(pispbe->dev);
	pm_runtime_put_autosuspend(pispbe->dev);

	return 0;

disable_devs_err:
	pispbe_destroy_devices(pispbe);
pm_runtime_suspend_err:
	pispbe_runtime_suspend(pispbe->dev);
pm_runtime_disable_err:
	pm_runtime_dont_use_autosuspend(pispbe->dev);
	pm_runtime_disable(pispbe->dev);

	return ret;
}

static void pispbe_remove(struct platform_device *pdev)
{
	struct pispbe_dev *pispbe = platform_get_drvdata(pdev);

	pispbe_destroy_devices(pispbe);

	pispbe_runtime_suspend(pispbe->dev);
	pm_runtime_dont_use_autosuspend(pispbe->dev);
	pm_runtime_disable(pispbe->dev);
}

static const struct dev_pm_ops pispbe_pm_ops = {
	SET_RUNTIME_PM_OPS(pispbe_runtime_suspend, pispbe_runtime_resume, NULL)
};

static const struct of_device_id pispbe_of_match[] = {
	{
		.compatible = "raspberrypi,pispbe",
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, pispbe_of_match);

static struct platform_driver pispbe_pdrv = {
	.probe		= pispbe_probe,
	.remove		= pispbe_remove,
	.driver		= {
		.name	= PISPBE_NAME,
		.of_match_table = pispbe_of_match,
		.pm = &pispbe_pm_ops,
	},
};

module_platform_driver(pispbe_pdrv);

MODULE_DESCRIPTION("PiSP Back End driver");
MODULE_AUTHOR("David Plowman <david.plowman@raspberrypi.com>");
MODULE_AUTHOR("Nick Hollinghurst <nick.hollinghurst@raspberrypi.com>");
MODULE_LICENSE("GPL");
