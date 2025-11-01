// SPDX-License-Identifier: GPL-2.0-only
/*
 * RP1 CSI-2 Driver
 *
 * Copyright (c) 2021-2024 Raspberry Pi Ltd.
 * Copyright (c) 2023-2024 Ideas on Board Oy
 */

#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>

#include <media/videobuf2-dma-contig.h>

#include "cfe.h"
#include "csi2.h"

#include "cfe-trace.h"

static bool csi2_track_errors;
module_param_named(track_csi2_errors, csi2_track_errors, bool, 0);
MODULE_PARM_DESC(track_csi2_errors, "track csi-2 errors");

#define csi2_dbg(csi2, fmt, arg...) dev_dbg((csi2)->v4l2_dev->dev, fmt, ##arg)
#define csi2_err(csi2, fmt, arg...) dev_err((csi2)->v4l2_dev->dev, fmt, ##arg)

/* CSI2-DMA registers */
#define CSI2_STATUS		0x000
#define CSI2_QOS		0x004
#define CSI2_DISCARDS_OVERFLOW	0x008
#define CSI2_DISCARDS_INACTIVE	0x00c
#define CSI2_DISCARDS_UNMATCHED	0x010
#define CSI2_DISCARDS_LEN_LIMIT	0x014

#define CSI2_DISCARDS_AMOUNT_SHIFT	0
#define CSI2_DISCARDS_AMOUNT_MASK	GENMASK(23, 0)
#define CSI2_DISCARDS_DT_SHIFT		24
#define CSI2_DISCARDS_DT_MASK		GENMASK(29, 24)
#define CSI2_DISCARDS_VC_SHIFT		30
#define CSI2_DISCARDS_VC_MASK		GENMASK(31, 30)

#define CSI2_LLEV_PANICS	0x018
#define CSI2_ULEV_PANICS	0x01c
#define CSI2_IRQ_MASK		0x020
#define CSI2_IRQ_MASK_IRQ_OVERFLOW		BIT(0)
#define CSI2_IRQ_MASK_IRQ_DISCARD_OVERFLOW	BIT(1)
#define CSI2_IRQ_MASK_IRQ_DISCARD_LENGTH_LIMIT	BIT(2)
#define CSI2_IRQ_MASK_IRQ_DISCARD_UNMATCHED	BIT(3)
#define CSI2_IRQ_MASK_IRQ_DISCARD_INACTIVE	BIT(4)
#define CSI2_IRQ_MASK_IRQ_ALL                                              \
	(CSI2_IRQ_MASK_IRQ_OVERFLOW | CSI2_IRQ_MASK_IRQ_DISCARD_OVERFLOW | \
	 CSI2_IRQ_MASK_IRQ_DISCARD_LENGTH_LIMIT |                          \
	 CSI2_IRQ_MASK_IRQ_DISCARD_UNMATCHED |                             \
	 CSI2_IRQ_MASK_IRQ_DISCARD_INACTIVE)

#define CSI2_CTRL		0x024
#define CSI2_CH_CTRL(x)		((x) * 0x40 + 0x28)
#define CSI2_CH_ADDR0(x)	((x) * 0x40 + 0x2c)
#define CSI2_CH_ADDR1(x)	((x) * 0x40 + 0x3c)
#define CSI2_CH_STRIDE(x)	((x) * 0x40 + 0x30)
#define CSI2_CH_LENGTH(x)	((x) * 0x40 + 0x34)
#define CSI2_CH_DEBUG(x)	((x) * 0x40 + 0x38)
#define CSI2_CH_FRAME_SIZE(x)	((x) * 0x40 + 0x40)
#define CSI2_CH_COMP_CTRL(x)	((x) * 0x40 + 0x44)
#define CSI2_CH_FE_FRAME_ID(x)	((x) * 0x40 + 0x48)

/* CSI2_STATUS */
#define CSI2_STATUS_IRQ_FS(x)			(BIT(0) << (x))
#define CSI2_STATUS_IRQ_FE(x)			(BIT(4) << (x))
#define CSI2_STATUS_IRQ_FE_ACK(x)		(BIT(8) << (x))
#define CSI2_STATUS_IRQ_LE(x)			(BIT(12) << (x))
#define CSI2_STATUS_IRQ_LE_ACK(x)		(BIT(16) << (x))
#define CSI2_STATUS_IRQ_CH_MASK(x) \
	(CSI2_STATUS_IRQ_FS(x) | CSI2_STATUS_IRQ_FE(x) | \
	 CSI2_STATUS_IRQ_FE_ACK(x) | CSI2_STATUS_IRQ_LE(x) | \
	 CSI2_STATUS_IRQ_LE_ACK(x))
#define CSI2_STATUS_IRQ_OVERFLOW		BIT(20)
#define CSI2_STATUS_IRQ_DISCARD_OVERFLOW	BIT(21)
#define CSI2_STATUS_IRQ_DISCARD_LEN_LIMIT	BIT(22)
#define CSI2_STATUS_IRQ_DISCARD_UNMATCHED	BIT(23)
#define CSI2_STATUS_IRQ_DISCARD_INACTIVE	BIT(24)

/* CSI2_CTRL */
#define CSI2_CTRL_EOP_IS_EOL			BIT(0)

/* CSI2_CH_CTRL */
#define CSI2_CH_CTRL_DMA_EN			BIT(0)
#define CSI2_CH_CTRL_FORCE			BIT(3)
#define CSI2_CH_CTRL_AUTO_ARM			BIT(4)
#define CSI2_CH_CTRL_IRQ_EN_FS			BIT(13)
#define CSI2_CH_CTRL_IRQ_EN_FE			BIT(14)
#define CSI2_CH_CTRL_IRQ_EN_FE_ACK		BIT(15)
#define CSI2_CH_CTRL_IRQ_EN_LE			BIT(16)
#define CSI2_CH_CTRL_IRQ_EN_LE_ACK		BIT(17)
#define CSI2_CH_CTRL_FLUSH_FE			BIT(28)
#define CSI2_CH_CTRL_PACK_LINE			BIT(29)
#define CSI2_CH_CTRL_PACK_BYTES			BIT(30)
#define CSI2_CH_CTRL_CH_MODE_MASK		GENMASK(2, 1)
#define CSI2_CH_CTRL_VC_MASK			GENMASK(6, 5)
#define CSI2_CH_CTRL_DT_MASK			GENMASK(12, 7)
#define CSI2_CH_CTRL_LC_MASK			GENMASK(27, 18)

/* CHx_COMPRESSION_CONTROL */
#define CSI2_CH_COMP_CTRL_OFFSET_MASK		GENMASK(15, 0)
#define CSI2_CH_COMP_CTRL_SHIFT_MASK		GENMASK(19, 16)
#define CSI2_CH_COMP_CTRL_MODE_MASK		GENMASK(25, 24)

static inline u32 csi2_reg_read(struct csi2_device *csi2, u32 offset)
{
	return readl(csi2->base + offset);
}

static inline void csi2_reg_write(struct csi2_device *csi2, u32 offset, u32 val)
{
	writel(val, csi2->base + offset);
}

static inline void set_field(u32 *valp, u32 field, u32 mask)
{
	u32 val = *valp;

	val &= ~mask;
	val |= (field << __ffs(mask)) & mask;
	*valp = val;
}

static int csi2_regs_show(struct seq_file *s, void *data)
{
	struct csi2_device *csi2 = s->private;
	int ret;

	ret = pm_runtime_resume_and_get(csi2->v4l2_dev->dev);
	if (ret)
		return ret;

#define DUMP(reg) seq_printf(s, #reg " \t0x%08x\n", csi2_reg_read(csi2, reg))
#define DUMP_CH(idx, reg) seq_printf(s, #reg "(%u) \t0x%08x\n", idx, \
				     csi2_reg_read(csi2, reg(idx)))

	DUMP(CSI2_STATUS);
	DUMP(CSI2_DISCARDS_OVERFLOW);
	DUMP(CSI2_DISCARDS_INACTIVE);
	DUMP(CSI2_DISCARDS_UNMATCHED);
	DUMP(CSI2_DISCARDS_LEN_LIMIT);
	DUMP(CSI2_LLEV_PANICS);
	DUMP(CSI2_ULEV_PANICS);
	DUMP(CSI2_IRQ_MASK);
	DUMP(CSI2_CTRL);

	for (unsigned int i = 0; i < CSI2_NUM_CHANNELS; ++i) {
		DUMP_CH(i, CSI2_CH_CTRL);
		DUMP_CH(i, CSI2_CH_ADDR0);
		DUMP_CH(i, CSI2_CH_ADDR1);
		DUMP_CH(i, CSI2_CH_STRIDE);
		DUMP_CH(i, CSI2_CH_LENGTH);
		DUMP_CH(i, CSI2_CH_DEBUG);
		DUMP_CH(i, CSI2_CH_FRAME_SIZE);
		DUMP_CH(i, CSI2_CH_COMP_CTRL);
		DUMP_CH(i, CSI2_CH_FE_FRAME_ID);
	}

#undef DUMP
#undef DUMP_CH

	pm_runtime_put(csi2->v4l2_dev->dev);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(csi2_regs);

static int csi2_errors_show(struct seq_file *s, void *data)
{
	struct csi2_device *csi2 = s->private;
	unsigned long flags;
	u32 discards_table[DISCARDS_TABLE_NUM_VCS][DISCARDS_TABLE_NUM_ENTRIES];
	u32 discards_dt_table[DISCARDS_TABLE_NUM_ENTRIES];
	u32 overflows;

	spin_lock_irqsave(&csi2->errors_lock, flags);

	memcpy(discards_table, csi2->discards_table, sizeof(discards_table));
	memcpy(discards_dt_table, csi2->discards_dt_table,
	       sizeof(discards_dt_table));
	overflows = csi2->overflows;

	csi2->overflows = 0;
	memset(csi2->discards_table, 0, sizeof(discards_table));
	memset(csi2->discards_dt_table, 0, sizeof(discards_dt_table));

	spin_unlock_irqrestore(&csi2->errors_lock, flags);

	seq_printf(s, "Overflows %u\n", overflows);
	seq_puts(s, "Discards:\n");
	seq_puts(s, "VC            OVLF        LEN  UNMATCHED   INACTIVE\n");

	for (unsigned int vc = 0; vc < DISCARDS_TABLE_NUM_VCS; ++vc) {
		seq_printf(s, "%u       %10u %10u %10u %10u\n", vc,
			   discards_table[vc][DISCARDS_TABLE_OVERFLOW],
			   discards_table[vc][DISCARDS_TABLE_LENGTH_LIMIT],
			   discards_table[vc][DISCARDS_TABLE_UNMATCHED],
			   discards_table[vc][DISCARDS_TABLE_INACTIVE]);
	}

	seq_printf(s, "Last DT %10u %10u %10u %10u\n",
		   discards_dt_table[DISCARDS_TABLE_OVERFLOW],
		   discards_dt_table[DISCARDS_TABLE_LENGTH_LIMIT],
		   discards_dt_table[DISCARDS_TABLE_UNMATCHED],
		   discards_dt_table[DISCARDS_TABLE_INACTIVE]);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(csi2_errors);

static void csi2_isr_handle_errors(struct csi2_device *csi2, u32 status)
{
	spin_lock(&csi2->errors_lock);

	if (status & CSI2_STATUS_IRQ_OVERFLOW)
		csi2->overflows++;

	for (unsigned int i = 0; i < DISCARDS_TABLE_NUM_ENTRIES; ++i) {
		static const u32 discard_bits[] = {
			CSI2_STATUS_IRQ_DISCARD_OVERFLOW,
			CSI2_STATUS_IRQ_DISCARD_LEN_LIMIT,
			CSI2_STATUS_IRQ_DISCARD_UNMATCHED,
			CSI2_STATUS_IRQ_DISCARD_INACTIVE,
		};
		static const u8 discard_regs[] = {
			CSI2_DISCARDS_OVERFLOW,
			CSI2_DISCARDS_LEN_LIMIT,
			CSI2_DISCARDS_UNMATCHED,
			CSI2_DISCARDS_INACTIVE,
		};
		u32 amount;
		u8 dt, vc;
		u32 v;

		if (!(status & discard_bits[i]))
			continue;

		v = csi2_reg_read(csi2, discard_regs[i]);
		csi2_reg_write(csi2, discard_regs[i], 0);

		amount = (v & CSI2_DISCARDS_AMOUNT_MASK) >>
			 CSI2_DISCARDS_AMOUNT_SHIFT;
		dt = (v & CSI2_DISCARDS_DT_MASK) >> CSI2_DISCARDS_DT_SHIFT;
		vc = (v & CSI2_DISCARDS_VC_MASK) >> CSI2_DISCARDS_VC_SHIFT;

		csi2->discards_table[vc][i] += amount;
		csi2->discards_dt_table[i] = dt;
	}

	spin_unlock(&csi2->errors_lock);
}

void csi2_isr(struct csi2_device *csi2, bool *sof, bool *eof)
{
	u32 status;

	status = csi2_reg_read(csi2, CSI2_STATUS);

	/* Write value back to clear the interrupts */
	csi2_reg_write(csi2, CSI2_STATUS, status);

	for (unsigned int i = 0; i < CSI2_NUM_CHANNELS; i++) {
		u32 dbg;

		if ((status & CSI2_STATUS_IRQ_CH_MASK(i)) == 0)
			continue;

		dbg = csi2_reg_read(csi2, CSI2_CH_DEBUG(i));

		trace_csi2_irq(i, status, dbg);

		sof[i] = !!(status & CSI2_STATUS_IRQ_FS(i));
		eof[i] = !!(status & CSI2_STATUS_IRQ_FE_ACK(i));
	}

	if (csi2_track_errors)
		csi2_isr_handle_errors(csi2, status);
}

void csi2_set_buffer(struct csi2_device *csi2, unsigned int channel,
		     dma_addr_t dmaaddr, unsigned int stride, unsigned int size)
{
	u64 addr = dmaaddr;
	/*
	 * ADDRESS0 must be written last as it triggers the double buffering
	 * mechanism for all buffer registers within the hardware.
	 */
	addr >>= 4;
	csi2_reg_write(csi2, CSI2_CH_LENGTH(channel), size >> 4);
	csi2_reg_write(csi2, CSI2_CH_STRIDE(channel), stride >> 4);
	csi2_reg_write(csi2, CSI2_CH_ADDR1(channel), addr >> 32);
	csi2_reg_write(csi2, CSI2_CH_ADDR0(channel), addr & 0xffffffff);
}

void csi2_set_compression(struct csi2_device *csi2, unsigned int channel,
			  enum csi2_compression_mode mode, unsigned int shift,
			  unsigned int offset)
{
	u32 compression = 0;

	set_field(&compression, CSI2_CH_COMP_CTRL_OFFSET_MASK, offset);
	set_field(&compression, CSI2_CH_COMP_CTRL_SHIFT_MASK, shift);
	set_field(&compression, CSI2_CH_COMP_CTRL_MODE_MASK, mode);
	csi2_reg_write(csi2, CSI2_CH_COMP_CTRL(channel), compression);
}

void csi2_start_channel(struct csi2_device *csi2, unsigned int channel,
			enum csi2_mode mode, bool auto_arm, bool pack_bytes,
			unsigned int width, unsigned int height,
			u8 vc, u8 dt)
{
	u32 ctrl;

	csi2_dbg(csi2, "%s [%u]\n", __func__, channel);

	csi2_reg_write(csi2, CSI2_CH_CTRL(channel), 0);
	csi2_reg_write(csi2, CSI2_CH_DEBUG(channel), 0);
	csi2_reg_write(csi2, CSI2_STATUS, CSI2_STATUS_IRQ_CH_MASK(channel));

	/* Enable channel and FS/FE interrupts. */
	ctrl = CSI2_CH_CTRL_DMA_EN | CSI2_CH_CTRL_IRQ_EN_FS |
	       CSI2_CH_CTRL_IRQ_EN_FE_ACK | CSI2_CH_CTRL_PACK_LINE;
	/* PACK_BYTES ensures no striding for embedded data. */
	if (pack_bytes)
		ctrl |= CSI2_CH_CTRL_PACK_BYTES;

	if (auto_arm)
		ctrl |= CSI2_CH_CTRL_AUTO_ARM;

	if (width && height) {
		set_field(&ctrl, mode, CSI2_CH_CTRL_CH_MODE_MASK);
		csi2_reg_write(csi2, CSI2_CH_FRAME_SIZE(channel),
			       (height << 16) | width);
	} else {
		set_field(&ctrl, 0x0, CSI2_CH_CTRL_CH_MODE_MASK);
		csi2_reg_write(csi2, CSI2_CH_FRAME_SIZE(channel), 0);
	}

	set_field(&ctrl, vc, CSI2_CH_CTRL_VC_MASK);
	set_field(&ctrl, dt, CSI2_CH_CTRL_DT_MASK);
	csi2_reg_write(csi2, CSI2_CH_CTRL(channel), ctrl);
	csi2->num_lines[channel] = height;
}

void csi2_stop_channel(struct csi2_device *csi2, unsigned int channel)
{
	csi2_dbg(csi2, "%s [%u]\n", __func__, channel);

	/* Channel disable.  Use FORCE to allow stopping mid-frame. */
	csi2_reg_write(csi2, CSI2_CH_CTRL(channel), CSI2_CH_CTRL_FORCE);
	/* Latch the above change by writing to the ADDR0 register. */
	csi2_reg_write(csi2, CSI2_CH_ADDR0(channel), 0);
	/* Write this again, the HW needs it! */
	csi2_reg_write(csi2, CSI2_CH_ADDR0(channel), 0);
}

void csi2_open_rx(struct csi2_device *csi2)
{
	csi2_reg_write(csi2, CSI2_IRQ_MASK,
		       csi2_track_errors ? CSI2_IRQ_MASK_IRQ_ALL : 0);

	dphy_start(&csi2->dphy);

	csi2_reg_write(csi2, CSI2_CTRL, CSI2_CTRL_EOP_IS_EOL);
}

void csi2_close_rx(struct csi2_device *csi2)
{
	dphy_stop(&csi2->dphy);

	csi2_reg_write(csi2, CSI2_IRQ_MASK, 0);
}

static int csi2_init_state(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route routes[] = { {
		.sink_pad = CSI2_PAD_SINK,
		.sink_stream = 0,
		.source_pad = CSI2_PAD_FIRST_SOURCE,
		.source_stream = 0,
		.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
	} };

	struct v4l2_subdev_krouting routing = {
		.num_routes = ARRAY_SIZE(routes),
		.routes = routes,
	};

	int ret;

	ret = v4l2_subdev_set_routing_with_fmt(sd, state, &routing,
					       &cfe_default_format);
	if (ret)
		return ret;

	return 0;
}

static int csi2_pad_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *format)
{
	if (format->pad == CSI2_PAD_SINK) {
		/* Store the sink format and propagate it to the source. */

		const struct cfe_fmt *cfe_fmt;

		cfe_fmt = find_format_by_code(format->format.code);
		if (!cfe_fmt) {
			cfe_fmt = find_format_by_code(MEDIA_BUS_FMT_SRGGB10_1X10);
			format->format.code = cfe_fmt->code;
		}

		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_state_get_format(state, format->pad,
						   format->stream);
		if (!fmt)
			return -EINVAL;

		*fmt = format->format;

		fmt = v4l2_subdev_state_get_opposite_stream_format(state,
								   format->pad,
								   format->stream);
		if (!fmt)
			return -EINVAL;

		format->format.field = V4L2_FIELD_NONE;

		*fmt = format->format;
	} else {
		/* Only allow changing the source pad mbus code. */

		struct v4l2_mbus_framefmt *sink_fmt, *source_fmt;
		u32 sink_code;
		u32 code;

		sink_fmt = v4l2_subdev_state_get_opposite_stream_format(state,
									format->pad,
									format->stream);
		if (!sink_fmt)
			return -EINVAL;

		source_fmt = v4l2_subdev_state_get_format(state, format->pad,
							  format->stream);
		if (!source_fmt)
			return -EINVAL;

		sink_code = sink_fmt->code;
		code = format->format.code;

		/*
		 * Only allow changing the mbus code to:
		 * - The sink's mbus code
		 * - The 16-bit version of the sink's mbus code
		 * - The compressed version of the sink's mbus code
		 */
		if (code == sink_code ||
		    code == cfe_find_16bit_code(sink_code) ||
		    code == cfe_find_compressed_code(sink_code))
			source_fmt->code = code;

		format->format.code = source_fmt->code;
	}

	return 0;
}

static int csi2_set_routing(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    enum v4l2_subdev_format_whence which,
			    struct v4l2_subdev_krouting *routing)
{
	int ret;

	ret = v4l2_subdev_routing_validate(sd, routing,
					   V4L2_SUBDEV_ROUTING_ONLY_1_TO_1 |
					   V4L2_SUBDEV_ROUTING_NO_SOURCE_MULTIPLEXING);
	if (ret)
		return ret;

	/* Only stream ID 0 allowed on source pads */
	for (unsigned int i = 0; i < routing->num_routes; ++i) {
		const struct v4l2_subdev_route *route = &routing->routes[i];

		if (route->source_stream != 0)
			return -EINVAL;
	}

	ret = v4l2_subdev_set_routing_with_fmt(sd, state, routing,
					       &cfe_default_format);
	if (ret)
		return ret;

	return 0;
}

static const struct v4l2_subdev_pad_ops csi2_subdev_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = csi2_pad_set_fmt,
	.set_routing = csi2_set_routing,
	.link_validate = v4l2_subdev_link_validate_default,
};

static const struct media_entity_operations csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

static const struct v4l2_subdev_ops csi2_subdev_ops = {
	.pad = &csi2_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops csi2_internal_ops = {
	.init_state = csi2_init_state,
};

int csi2_init(struct csi2_device *csi2, struct dentry *debugfs)
{
	int ret;

	spin_lock_init(&csi2->errors_lock);

	csi2->dphy.dev = csi2->v4l2_dev->dev;
	dphy_probe(&csi2->dphy);

	debugfs_create_file("csi2_regs", 0440, debugfs, csi2, &csi2_regs_fops);

	if (csi2_track_errors)
		debugfs_create_file("csi2_errors", 0440, debugfs, csi2,
				    &csi2_errors_fops);

	csi2->pad[CSI2_PAD_SINK].flags = MEDIA_PAD_FL_SINK;

	for (unsigned int i = CSI2_PAD_FIRST_SOURCE;
	     i < CSI2_PAD_FIRST_SOURCE + CSI2_PAD_NUM_SOURCES; i++)
		csi2->pad[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&csi2->sd.entity, ARRAY_SIZE(csi2->pad),
				     csi2->pad);
	if (ret)
		return ret;

	/* Initialize subdev */
	v4l2_subdev_init(&csi2->sd, &csi2_subdev_ops);
	csi2->sd.internal_ops = &csi2_internal_ops;
	csi2->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csi2->sd.entity.ops = &csi2_entity_ops;
	csi2->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_STREAMS;
	csi2->sd.owner = THIS_MODULE;
	snprintf(csi2->sd.name, sizeof(csi2->sd.name), "csi2");

	ret = v4l2_subdev_init_finalize(&csi2->sd);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_device_register_subdev(csi2->v4l2_dev, &csi2->sd);
	if (ret) {
		csi2_err(csi2, "Failed register csi2 subdev (%d)\n", ret);
		goto err_subdev_cleanup;
	}

	return 0;

err_subdev_cleanup:
	v4l2_subdev_cleanup(&csi2->sd);
err_entity_cleanup:
	media_entity_cleanup(&csi2->sd.entity);

	return ret;
}

void csi2_uninit(struct csi2_device *csi2)
{
	v4l2_device_unregister_subdev(&csi2->sd);
	v4l2_subdev_cleanup(&csi2->sd);
	media_entity_cleanup(&csi2->sd.entity);
}
