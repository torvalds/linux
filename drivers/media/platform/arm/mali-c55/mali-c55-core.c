// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Mali-C55 ISP Driver - Core driver code
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "mali-c55-common.h"
#include "mali-c55-registers.h"

static const char * const mali_c55_interrupt_names[] = {
	[MALI_C55_IRQ_ISP_START] = "ISP start",
	[MALI_C55_IRQ_ISP_DONE] = "ISP done",
	[MALI_C55_IRQ_MCM_ERROR] = "Multi-context management error",
	[MALI_C55_IRQ_BROKEN_FRAME_ERROR] = "Broken frame error",
	[MALI_C55_IRQ_MET_AF_DONE] = "AF metering done",
	[MALI_C55_IRQ_MET_AEXP_DONE] = "AEXP metering done",
	[MALI_C55_IRQ_MET_AWB_DONE] = "AWB metering done",
	[MALI_C55_IRQ_AEXP_1024_DONE] = "AEXP 1024-bit histogram done",
	[MALI_C55_IRQ_IRIDIX_MET_DONE] = "Iridix metering done",
	[MALI_C55_IRQ_LUT_INIT_DONE] = "LUT memory init done",
	[MALI_C55_IRQ_FR_Y_DONE] = "Full resolution Y plane DMA done",
	[MALI_C55_IRQ_FR_UV_DONE] = "Full resolution U/V plane DMA done",
	[MALI_C55_IRQ_DS_Y_DONE] = "Downscale Y plane DMA done",
	[MALI_C55_IRQ_DS_UV_DONE] = "Downscale U/V plane DMA done",
	[MALI_C55_IRQ_LINEARIZATION_DONE] = "Linearisation done",
	[MALI_C55_IRQ_RAW_FRONTEND_DONE] = "Raw frontend processing done",
	[MALI_C55_IRQ_NOISE_REDUCTION_DONE] = "Noise reduction done",
	[MALI_C55_IRQ_IRIDIX_DONE] = "Iridix done",
	[MALI_C55_IRQ_BAYER2RGB_DONE] = "Bayer to RGB conversion done",
	[MALI_C55_IRQ_WATCHDOG_TIMER] = "Watchdog timer timed out",
	[MALI_C55_IRQ_FRAME_COLLISION] = "Frame collision error",
	[MALI_C55_IRQ_UNUSED] = "IRQ bit unused",
	[MALI_C55_IRQ_DMA_ERROR] = "DMA error",
	[MALI_C55_IRQ_INPUT_STOPPED] = "Input port safely stopped",
	[MALI_C55_IRQ_MET_AWB_TARGET1_HIT] = "AWB metering target 1 address hit",
	[MALI_C55_IRQ_MET_AWB_TARGET2_HIT] = "AWB metering target 2 address hit"
};

static const unsigned int config_space_addrs[] = {
	[MALI_C55_CONFIG_PING] = 0x0ab6c,
	[MALI_C55_CONFIG_PONG] = 0x22b2c,
};

static const char * const mali_c55_clk_names[MALI_C55_NUM_CLKS] = {
	"vclk",
	"aclk",
	"hclk",
};

static const char * const mali_c55_reset_names[MALI_C55_NUM_RESETS] = {
	"vresetn",
	"aresetn",
	"hresetn",
};

/*
 * System IO
 *
 * The Mali-C55 ISP has up to two configuration register spaces (called 'ping'
 * and 'pong'), with the expectation that the 'active' space will be left
 * untouched whilst a frame is being processed and the 'inactive' space
 * configured ready to be switched to during the blanking period before the next
 * frame processing starts. These spaces should ideally be set via DMA transfer
 * from a buffer rather than through individual register set operations. There
 * is also a shared global register space which should be set normally. For now
 * though we will simply use a CPU write and target DMA transfers of the config
 * space in the future.
 *
 * As groundwork for that path any read/write call that is made to an address
 * within those config spaces should infact be directed to a buffer that was
 * allocated to hold them rather than the IO memory itself. The actual copy of
 * that buffer to IO mem will happen on interrupt.
 */

void mali_c55_write(struct mali_c55 *mali_c55, unsigned int addr, u32 val)
{
	WARN_ON(addr >= MALI_C55_REG_CONFIG_SPACES_OFFSET);

	writel(val, mali_c55->base + addr);
}

u32 mali_c55_read(struct mali_c55 *mali_c55, unsigned int addr)
{
	WARN_ON(addr >= MALI_C55_REG_CONFIG_SPACES_OFFSET);

	return readl(mali_c55->base + addr);
}

void mali_c55_update_bits(struct mali_c55 *mali_c55, unsigned int addr,
			  u32 mask, u32 val)
{
	u32 orig, new;

	orig = mali_c55_read(mali_c55, addr);

	new = orig & ~mask;
	new |= val & mask;

	if (new != orig)
		mali_c55_write(mali_c55, addr, new);
}

static void __mali_c55_ctx_write(struct mali_c55_context *ctx,
				 unsigned int addr, u32 val)
{
	addr = (addr - MALI_C55_REG_CONFIG_SPACES_OFFSET) / 4;
	ctx->registers[addr] = val;
}

void mali_c55_ctx_write(struct mali_c55 *mali_c55, unsigned int addr, u32 val)
{
	struct mali_c55_context *ctx = mali_c55_get_active_context(mali_c55);

	WARN_ON(addr < MALI_C55_REG_CONFIG_SPACES_OFFSET);

	spin_lock(&ctx->lock);
	__mali_c55_ctx_write(ctx, addr, val);
	spin_unlock(&ctx->lock);
}

static u32 __mali_c55_ctx_read(struct mali_c55_context *ctx, unsigned int addr)
{
	addr = (addr - MALI_C55_REG_CONFIG_SPACES_OFFSET) / 4;
	return ctx->registers[addr];
}

u32 mali_c55_ctx_read(struct mali_c55 *mali_c55, unsigned int addr)
{
	struct mali_c55_context *ctx = mali_c55_get_active_context(mali_c55);
	u32 val;

	WARN_ON(addr < MALI_C55_REG_CONFIG_SPACES_OFFSET);

	spin_lock(&ctx->lock);
	val = __mali_c55_ctx_read(ctx, addr);
	spin_unlock(&ctx->lock);

	return val;
}

void mali_c55_ctx_update_bits(struct mali_c55 *mali_c55, unsigned int addr,
			      u32 mask, u32 val)
{
	struct mali_c55_context *ctx = mali_c55_get_active_context(mali_c55);
	u32 orig, tmp;

	WARN_ON(addr < MALI_C55_REG_CONFIG_SPACES_OFFSET);

	spin_lock(&ctx->lock);

	orig = __mali_c55_ctx_read(ctx, addr);

	tmp = orig & ~mask;
	tmp |= val & mask;

	if (tmp != orig)
		__mali_c55_ctx_write(ctx, addr, tmp);

	spin_unlock(&ctx->lock);
}

int mali_c55_config_write(struct mali_c55_context *ctx,
			  enum mali_c55_config_spaces cfg_space,
			  bool force_synchronous)
{
	struct mali_c55 *mali_c55 = ctx->mali_c55;

	memcpy_toio(mali_c55->base + config_space_addrs[cfg_space],
		    ctx->registers, MALI_C55_CONFIG_SPACE_SIZE);

	return 0;
}

struct mali_c55_context *mali_c55_get_active_context(struct mali_c55 *mali_c55)
{
	return &mali_c55->context;
}

static void mali_c55_remove_links(struct mali_c55 *mali_c55)
{
	unsigned int i;

	media_entity_remove_links(&mali_c55->tpg.sd.entity);
	media_entity_remove_links(&mali_c55->isp.sd.entity);

	for (i = 0; i < MALI_C55_NUM_RSZS; i++)
		media_entity_remove_links(&mali_c55->resizers[i].sd.entity);

	for (i = 0; i < MALI_C55_NUM_CAP_DEVS; i++)
		media_entity_remove_links(&mali_c55->cap_devs[i].vdev.entity);
}

static int mali_c55_create_links(struct mali_c55 *mali_c55)
{
	int ret;

	/* Test pattern generator to ISP */
	ret = media_create_pad_link(&mali_c55->tpg.sd.entity, 0,
				    &mali_c55->isp.sd.entity,
				    MALI_C55_ISP_PAD_SINK_VIDEO, 0);
	if (ret) {
		dev_err(mali_c55->dev, "failed to link TPG and ISP\n");
		goto err_remove_links;
	}

	/* Full resolution resizer pipe. */
	ret = media_create_pad_link(&mali_c55->isp.sd.entity,
			MALI_C55_ISP_PAD_SOURCE_VIDEO,
			&mali_c55->resizers[MALI_C55_RSZ_FR].sd.entity,
			MALI_C55_RSZ_SINK_PAD,
			MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(mali_c55->dev, "failed to link ISP and FR resizer\n");
		goto err_remove_links;
	}

	/* Full resolution bypass. */
	ret = media_create_pad_link(&mali_c55->isp.sd.entity,
				    MALI_C55_ISP_PAD_SOURCE_BYPASS,
				    &mali_c55->resizers[MALI_C55_RSZ_FR].sd.entity,
				    MALI_C55_RSZ_SINK_BYPASS_PAD,
				    MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(mali_c55->dev, "failed to link ISP and FR resizer\n");
		goto err_remove_links;
	}

	/* Resizer pipe to video capture nodes. */
	ret = media_create_pad_link(&mali_c55->resizers[0].sd.entity,
			MALI_C55_RSZ_SOURCE_PAD,
			&mali_c55->cap_devs[MALI_C55_CAP_DEV_FR].vdev.entity,
			0, MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(mali_c55->dev,
			"failed to link FR resizer and video device\n");
		goto err_remove_links;
	}

	/* The downscale pipe is an optional hardware block */
	if (mali_c55->capabilities & MALI_C55_GPS_DS_PIPE_FITTED) {
		ret = media_create_pad_link(&mali_c55->isp.sd.entity,
			MALI_C55_ISP_PAD_SOURCE_VIDEO,
			&mali_c55->resizers[MALI_C55_RSZ_DS].sd.entity,
			MALI_C55_RSZ_SINK_PAD,
			MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
		if (ret) {
			dev_err(mali_c55->dev,
				"failed to link ISP and DS resizer\n");
			goto err_remove_links;
		}

		ret = media_create_pad_link(&mali_c55->resizers[1].sd.entity,
			MALI_C55_RSZ_SOURCE_PAD,
			&mali_c55->cap_devs[MALI_C55_CAP_DEV_DS].vdev.entity,
			0, MEDIA_LNK_FL_ENABLED);
		if (ret) {
			dev_err(mali_c55->dev,
				"failed to link DS resizer and video device\n");
			goto err_remove_links;
		}
	}

	ret = media_create_pad_link(&mali_c55->isp.sd.entity,
				    MALI_C55_ISP_PAD_SOURCE_STATS,
				    &mali_c55->stats.vdev.entity, 0,
				    MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(mali_c55->dev,
			"failed to link ISP and 3a stats node\n");
		goto err_remove_links;
	}

	ret = media_create_pad_link(&mali_c55->params.vdev.entity, 0,
				    &mali_c55->isp.sd.entity,
				    MALI_C55_ISP_PAD_SINK_PARAMS,
				    MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(mali_c55->dev,
			"failed to link ISP and parameters video node\n");
		goto err_remove_links;
	}

	return 0;

err_remove_links:
	mali_c55_remove_links(mali_c55);
	return ret;
}

static void mali_c55_unregister_entities(struct mali_c55 *mali_c55)
{
	mali_c55_remove_links(mali_c55);
	mali_c55_unregister_tpg(mali_c55);
	mali_c55_unregister_isp(mali_c55);
	mali_c55_unregister_resizers(mali_c55);
	mali_c55_unregister_capture_devs(mali_c55);
	mali_c55_unregister_params(mali_c55);
	mali_c55_unregister_stats(mali_c55);
}

static void mali_c55_swap_next_config(struct mali_c55 *mali_c55)
{
	struct mali_c55_context *ctx = mali_c55_get_active_context(mali_c55);

	mali_c55_config_write(ctx, mali_c55->next_config ?
			      MALI_C55_CONFIG_PING : MALI_C55_CONFIG_PONG,
			      false);

	mali_c55_update_bits(mali_c55, MALI_C55_REG_MCU_CONFIG,
		MALI_C55_REG_MCU_CONFIG_WRITE_MASK,
		MALI_C55_MCU_CONFIG_WRITE(mali_c55->next_config));
}

static int mali_c55_register_entities(struct mali_c55 *mali_c55)
{
	int ret;

	ret = mali_c55_register_tpg(mali_c55);
	if (ret)
		return ret;

	ret = mali_c55_register_isp(mali_c55);
	if (ret)
		goto err_unregister_entities;

	ret = mali_c55_register_resizers(mali_c55);
	if (ret)
		goto err_unregister_entities;

	ret = mali_c55_register_capture_devs(mali_c55);
	if (ret)
		goto err_unregister_entities;

	ret = mali_c55_register_params(mali_c55);
	if (ret)
		goto err_unregister_entities;

	ret = mali_c55_register_stats(mali_c55);
	if (ret)
		goto err_unregister_entities;

	ret = mali_c55_create_links(mali_c55);
	if (ret)
		goto err_unregister_entities;

	return 0;

err_unregister_entities:
	mali_c55_unregister_entities(mali_c55);

	return ret;
}

static int mali_c55_notifier_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_connection *asc)
{
	struct mali_c55 *mali_c55 = container_of(notifier,
						struct mali_c55, notifier);
	struct media_pad *pad = &mali_c55->isp.pads[MALI_C55_ISP_PAD_SINK_VIDEO];
	int ret;

	/*
	 * By default we'll flag this link enabled and the TPG disabled, but
	 * no immutable flag because we need to be able to switch between the
	 * two.
	 */
	ret = v4l2_create_fwnode_links_to_pad(subdev, pad,
					      MEDIA_LNK_FL_ENABLED);
	if (ret)
		dev_err(mali_c55->dev, "failed to create link for %s\n",
			subdev->name);

	return ret;
}

static int mali_c55_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct mali_c55 *mali_c55 = container_of(notifier,
						struct mali_c55, notifier);

	return v4l2_device_register_subdev_nodes(&mali_c55->v4l2_dev);
}

static const struct v4l2_async_notifier_operations mali_c55_notifier_ops = {
	.bound = mali_c55_notifier_bound,
	.complete = mali_c55_notifier_complete,
};

static int mali_c55_parse_endpoint(struct mali_c55 *mali_c55)
{
	struct v4l2_async_connection *asc;
	struct fwnode_handle *ep;

	/*
	 * The ISP should have a single endpoint pointing to some flavour of
	 * CSI-2 receiver...but for now at least we do want everything to work
	 * normally even with no sensors connected, as we have the TPG. If we
	 * don't find a sensor just warn and return success.
	 */
	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(mali_c55->dev),
					     0, 0, 0);
	if (!ep) {
		dev_warn(mali_c55->dev, "no local endpoint found\n");
		return 0;
	}

	asc = v4l2_async_nf_add_fwnode_remote(&mali_c55->notifier, ep,
					      struct v4l2_async_connection);
	fwnode_handle_put(ep);
	if (IS_ERR(asc)) {
		dev_err(mali_c55->dev, "failed to add remote fwnode\n");
		return PTR_ERR(asc);
	}

	return 0;
}

static int mali_c55_media_frameworks_init(struct mali_c55 *mali_c55)
{
	int ret;

	strscpy(mali_c55->media_dev.model, "ARM Mali-C55 ISP",
		sizeof(mali_c55->media_dev.model));

	media_device_init(&mali_c55->media_dev);

	ret = media_device_register(&mali_c55->media_dev);
	if (ret)
		goto err_cleanup_media_device;

	mali_c55->v4l2_dev.mdev = &mali_c55->media_dev;
	ret = v4l2_device_register(mali_c55->dev, &mali_c55->v4l2_dev);
	if (ret) {
		dev_err(mali_c55->dev, "failed to register V4L2 device\n");
		goto err_unregister_media_device;
	};

	mali_c55->notifier.ops = &mali_c55_notifier_ops;
	v4l2_async_nf_init(&mali_c55->notifier, &mali_c55->v4l2_dev);

	ret = mali_c55_register_entities(mali_c55);
	if (ret) {
		dev_err(mali_c55->dev, "failed to register entities\n");
		goto err_cleanup_nf;
	}

	ret = mali_c55_parse_endpoint(mali_c55);
	if (ret)
		goto err_cleanup_nf;

	ret = v4l2_async_nf_register(&mali_c55->notifier);
	if (ret) {
		dev_err(mali_c55->dev, "failed to register notifier\n");
		goto err_unregister_entities;
	}

	return 0;

err_unregister_entities:
	mali_c55_unregister_entities(mali_c55);
err_cleanup_nf:
	v4l2_async_nf_cleanup(&mali_c55->notifier);
	v4l2_device_unregister(&mali_c55->v4l2_dev);
err_unregister_media_device:
	media_device_unregister(&mali_c55->media_dev);
err_cleanup_media_device:
	media_device_cleanup(&mali_c55->media_dev);

	return ret;
}

static void mali_c55_media_frameworks_deinit(struct mali_c55 *mali_c55)
{
	v4l2_async_nf_unregister(&mali_c55->notifier);
	mali_c55_unregister_entities(mali_c55);
	v4l2_async_nf_cleanup(&mali_c55->notifier);
	v4l2_device_unregister(&mali_c55->v4l2_dev);
	media_device_unregister(&mali_c55->media_dev);
	media_device_cleanup(&mali_c55->media_dev);
}

bool mali_c55_pipeline_ready(struct mali_c55 *mali_c55)
{
	struct mali_c55_cap_dev *fr = &mali_c55->cap_devs[MALI_C55_CAP_DEV_FR];
	struct mali_c55_cap_dev *ds = &mali_c55->cap_devs[MALI_C55_CAP_DEV_DS];
	struct mali_c55_params *params = &mali_c55->params;
	struct mali_c55_stats *stats = &mali_c55->stats;

	return vb2_start_streaming_called(&fr->queue) &&
	       (!(mali_c55->capabilities & MALI_C55_GPS_DS_PIPE_FITTED) ||
		vb2_start_streaming_called(&ds->queue)) &&
	       vb2_start_streaming_called(&params->queue) &&
	       vb2_start_streaming_called(&stats->queue);
}

static int mali_c55_check_hwcfg(struct mali_c55 *mali_c55)
{
	u32 product, version, revision, capabilities;

	product = mali_c55_read(mali_c55, MALI_C55_REG_PRODUCT);
	version = mali_c55_read(mali_c55, MALI_C55_REG_VERSION);
	revision = mali_c55_read(mali_c55, MALI_C55_REG_REVISION);

	mali_c55->media_dev.hw_revision = version;

	dev_info(mali_c55->dev, "Detected Mali-C55 ISP %u.%u.%u\n",
		 product, version, revision);

	capabilities = mali_c55_read(mali_c55,
				     MALI_C55_REG_GLOBAL_PARAMETER_STATUS);

	/*
	 * In its current iteration, the driver only supports inline mode. Given
	 * we cannot control input data timing in this mode, we cannot guarantee
	 * that the vertical blanking periods between frames will be long enough
	 * for us to write configuration data to the ISP during them. For that
	 * reason we can't really support single config space configuration
	 * until memory input mode is implemented.
	 */
	if (!(capabilities & MALI_C55_GPS_PONG_FITTED)) {
		dev_err(mali_c55->dev, "Pong config space not fitted.\n");
		return -EINVAL;
	}

	mali_c55->capabilities = capabilities & 0xffff;

	return 0;
}

static irqreturn_t mali_c55_isr(int irq, void *context)
{
	struct device *dev = context;
	struct mali_c55 *mali_c55 = dev_get_drvdata(dev);
	unsigned long interrupt_status;
	u32 curr_config;
	unsigned int i;

	interrupt_status = mali_c55_read(mali_c55,
					 MALI_C55_REG_INTERRUPT_STATUS_VECTOR);
	if (!interrupt_status)
		return IRQ_NONE;

	mali_c55_write(mali_c55, MALI_C55_REG_INTERRUPT_CLEAR_VECTOR,
		       interrupt_status);
	mali_c55_write(mali_c55, MALI_C55_REG_INTERRUPT_CLEAR, 1);
	mali_c55_write(mali_c55, MALI_C55_REG_INTERRUPT_CLEAR, 0);

	for_each_set_bit(i, &interrupt_status, MALI_C55_NUM_IRQ_BITS) {
		switch (i) {
		case MALI_C55_IRQ_ISP_START:
			mali_c55_isp_queue_event_sof(mali_c55);

			mali_c55_set_next_buffer(&mali_c55->cap_devs[MALI_C55_CAP_DEV_FR]);
			if (mali_c55->capabilities & MALI_C55_GPS_DS_PIPE_FITTED)
				mali_c55_set_next_buffer(&mali_c55->cap_devs[MALI_C55_CAP_DEV_DS]);

			/*
			 * When the ISP starts a frame we have some work to do:
			 *
			 * 1. Copy over the config for the **next** frame
			 * 2. Read out the metering stats for the **last** frame
			 */

			curr_config = mali_c55_read(mali_c55,
						    MALI_C55_REG_PING_PONG_READ);
			curr_config &= MALI_C55_REG_PING_PONG_READ_MASK;
			curr_config >>= ffs(MALI_C55_REG_PING_PONG_READ_MASK) - 1;
			mali_c55->next_config = curr_config ^ 1;

			/*
			 * Write the configuration parameters received from
			 * userspace into the configuration buffer, which will
			 * be transferred to the 'next' active config space at
			 * by mali_c55_swap_next_config().
			 */
			mali_c55_params_write_config(mali_c55);

			mali_c55_stats_fill_buffer(mali_c55,
						   mali_c55->next_config ^ 1);

			mali_c55_swap_next_config(mali_c55);

			break;
		case MALI_C55_IRQ_ISP_DONE:
			/*
			 * TODO: Where the ISP has no Pong config fitted, we'd
			 * have to do the mali_c55_swap_next_config() call here.
			 */
			break;
		case MALI_C55_IRQ_FR_Y_DONE:
			mali_c55_set_plane_done(&mali_c55->cap_devs[MALI_C55_CAP_DEV_FR],
						MALI_C55_PLANE_Y);
			break;
		case MALI_C55_IRQ_FR_UV_DONE:
			mali_c55_set_plane_done(&mali_c55->cap_devs[MALI_C55_CAP_DEV_FR],
						MALI_C55_PLANE_UV);
			break;
		case MALI_C55_IRQ_DS_Y_DONE:
			mali_c55_set_plane_done(&mali_c55->cap_devs[MALI_C55_CAP_DEV_DS],
						MALI_C55_PLANE_Y);
			break;
		case MALI_C55_IRQ_DS_UV_DONE:
			mali_c55_set_plane_done(&mali_c55->cap_devs[MALI_C55_CAP_DEV_DS],
						MALI_C55_PLANE_UV);
			break;
		default:
			/*
			 * Only the above interrupts are currently unmasked. If
			 * we receive anything else here then something weird
			 * has gone on.
			 */
			dev_err(dev, "masked interrupt %s triggered\n",
				mali_c55_interrupt_names[i]);
		}
	}

	return IRQ_HANDLED;
}

static int mali_c55_init_context(struct mali_c55 *mali_c55,
				 struct resource *res)
{
	struct mali_c55_context *ctx = &mali_c55->context;

	ctx->base = res->start;
	ctx->mali_c55 = mali_c55;
	spin_lock_init(&ctx->lock);

	ctx->registers = kzalloc(MALI_C55_CONFIG_SPACE_SIZE, GFP_KERNEL);
	if (!ctx->registers)
		return -ENOMEM;

	/*
	 * The allocated memory is empty, we need to load the default
	 * register settings. We just read Ping; it's identical to Pong.
	 */
	memcpy_fromio(ctx->registers,
		      mali_c55->base + config_space_addrs[MALI_C55_CONFIG_PING],
		      MALI_C55_CONFIG_SPACE_SIZE);

	/*
	 * Some features of the ISP need to be disabled by default and only
	 * enabled at the same time as they're configured by a parameters buffer
	 */

	/* Bypass the sqrt and square compression and expansion modules */
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_BYPASS_1,
				 MALI_C55_REG_BYPASS_1_FE_SQRT,
				 MALI_C55_REG_BYPASS_1_FE_SQRT);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_BYPASS_3,
				 MALI_C55_REG_BYPASS_3_SQUARE_BE,
				 MALI_C55_REG_BYPASS_3_SQUARE_BE);

	/* Bypass the temper module */
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_BYPASS_2,
			   MALI_C55_REG_BYPASS_2_TEMPER);

	/* Disable the temper module's DMA read/write */
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_TEMPER_DMA_IO, 0x0);

	/* Bypass the colour noise reduction  */
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_BYPASS_4,
			   MALI_C55_REG_BYPASS_4_CNR);

	/* Disable the sinter module */
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_SINTER_CONFIG,
				 MALI_C55_SINTER_ENABLE_MASK, 0);

	/* Disable the RGB Gamma module for each output */
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_FR_GAMMA_RGB_ENABLE, 0);
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_DS_GAMMA_RGB_ENABLE, 0);

	/* Disable the colour correction matrix */
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_CCM_ENABLE, 0);

	return 0;
}

static void __mali_c55_power_off(struct mali_c55 *mali_c55)
{
	reset_control_bulk_assert(ARRAY_SIZE(mali_c55->resets), mali_c55->resets);
	clk_bulk_disable_unprepare(ARRAY_SIZE(mali_c55->clks), mali_c55->clks);
}

static int __maybe_unused mali_c55_runtime_suspend(struct device *dev)
{
	struct mali_c55 *mali_c55 = dev_get_drvdata(dev);

	if (irq_has_action(mali_c55->irqnum))
		free_irq(mali_c55->irqnum, dev);
	__mali_c55_power_off(mali_c55);

	return 0;
}

static int __mali_c55_power_on(struct mali_c55 *mali_c55)
{
	int ret;
	u32 val;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(mali_c55->clks),
				      mali_c55->clks);
	if (ret) {
		dev_err(mali_c55->dev, "failed to enable clocks\n");
		return ret;
	}

	ret = reset_control_bulk_deassert(ARRAY_SIZE(mali_c55->resets),
					  mali_c55->resets);
	if (ret) {
		dev_err(mali_c55->dev, "failed to deassert resets\n");
		return ret;
	}

	/* Use "software only" context management. */
	mali_c55_update_bits(mali_c55, MALI_C55_REG_MCU_CONFIG,
			     MALI_C55_REG_MCU_CONFIG_OVERRIDE_MASK, 0x01);

	/*
	 * Mask the interrupts and clear any that were set, then unmask the ones
	 * that we actually want to handle.
	 */
	mali_c55_write(mali_c55, MALI_C55_REG_INTERRUPT_MASK_VECTOR,
		       MALI_C55_INTERRUPT_MASK_ALL);
	mali_c55_write(mali_c55, MALI_C55_REG_INTERRUPT_CLEAR_VECTOR,
		       MALI_C55_INTERRUPT_MASK_ALL);
	mali_c55_write(mali_c55, MALI_C55_REG_INTERRUPT_CLEAR, 0x01);
	mali_c55_write(mali_c55, MALI_C55_REG_INTERRUPT_CLEAR, 0x00);

	mali_c55_update_bits(mali_c55, MALI_C55_REG_INTERRUPT_MASK_VECTOR,
			     MALI_C55_INTERRUPT_BIT(MALI_C55_IRQ_ISP_START) |
			     MALI_C55_INTERRUPT_BIT(MALI_C55_IRQ_ISP_DONE) |
			     MALI_C55_INTERRUPT_BIT(MALI_C55_IRQ_FR_Y_DONE) |
			     MALI_C55_INTERRUPT_BIT(MALI_C55_IRQ_FR_UV_DONE) |
			     MALI_C55_INTERRUPT_BIT(MALI_C55_IRQ_DS_Y_DONE) |
			     MALI_C55_INTERRUPT_BIT(MALI_C55_IRQ_DS_UV_DONE),
			     0x00);

	/* Set safe stop to ensure we're in a non-streaming state */
	mali_c55_write(mali_c55, MALI_C55_REG_INPUT_MODE_REQUEST,
		       MALI_C55_INPUT_SAFE_STOP);
	readl_poll_timeout(mali_c55->base + MALI_C55_REG_MODE_STATUS,
			   val, !val, 10 * USEC_PER_MSEC, 250 * USEC_PER_MSEC);

	return 0;
}

static int __maybe_unused mali_c55_runtime_resume(struct device *dev)
{
	struct mali_c55 *mali_c55 = dev_get_drvdata(dev);
	int ret;

	ret = __mali_c55_power_on(mali_c55);
	if (ret)
		return ret;

	/*
	 * The driver needs to transfer large amounts of register settings to
	 * the ISP each frame, using either a DMA transfer or memcpy. We use a
	 * threaded IRQ to avoid disabling interrupts the entire time that's
	 * happening.
	 */
	ret = request_threaded_irq(mali_c55->irqnum, NULL, mali_c55_isr,
				   IRQF_ONESHOT, dev_driver_string(dev), dev);
	if (ret) {
		__mali_c55_power_off(mali_c55);
		dev_err(dev, "failed to request irq\n");
	}

	return ret;
}

static const struct dev_pm_ops mali_c55_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(mali_c55_runtime_suspend, mali_c55_runtime_resume,
			   NULL)
};

static int mali_c55_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mali_c55 *mali_c55;
	struct resource *res;
	int ret;

	mali_c55 = devm_kzalloc(dev, sizeof(*mali_c55), GFP_KERNEL);
	if (!mali_c55)
		return -ENOMEM;

	mali_c55->dev = dev;
	platform_set_drvdata(pdev, mali_c55);

	mali_c55->base = devm_platform_get_and_ioremap_resource(pdev, 0,
								&res);
	if (IS_ERR(mali_c55->base))
		return dev_err_probe(dev, PTR_ERR(mali_c55->base),
				     "failed to map IO memory\n");

	for (unsigned int i = 0; i < ARRAY_SIZE(mali_c55_clk_names); i++)
		mali_c55->clks[i].id = mali_c55_clk_names[i];

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(mali_c55->clks), mali_c55->clks);
	if (ret)
		return dev_err_probe(dev, ret, "failed to acquire clocks\n");

	for (unsigned int i = 0; i < ARRAY_SIZE(mali_c55_reset_names); i++)
		mali_c55->resets[i].id = mali_c55_reset_names[i];

	ret = devm_reset_control_bulk_get_optional_shared(dev,
			ARRAY_SIZE(mali_c55_reset_names), mali_c55->resets);
	if (ret)
		return dev_err_probe(dev, ret, "failed to acquire resets\n");

	of_reserved_mem_device_init(dev);
	vb2_dma_contig_set_max_seg_size(dev, UINT_MAX);

	ret = __mali_c55_power_on(mali_c55);
	if (ret)
		return dev_err_probe(dev, ret, "failed to power on\n");

	ret = mali_c55_check_hwcfg(mali_c55);
	if (ret)
		goto err_power_off;

	ret = mali_c55_init_context(mali_c55, res);
	if (ret)
		goto err_power_off;

	mali_c55->media_dev.dev = dev;

	pm_runtime_set_autosuspend_delay(&pdev->dev, 2000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = mali_c55_media_frameworks_init(mali_c55);
	if (ret)
		goto err_free_context_registers;

	pm_runtime_idle(&pdev->dev);

	mali_c55->irqnum = platform_get_irq(pdev, 0);
	if (mali_c55->irqnum < 0) {
		ret = mali_c55->irqnum;
		dev_err(dev, "failed to get interrupt\n");
		goto err_deinit_media_frameworks;
	}

	return 0;

err_deinit_media_frameworks:
	mali_c55_media_frameworks_deinit(mali_c55);
	pm_runtime_disable(&pdev->dev);
err_free_context_registers:
	kfree(mali_c55->context.registers);
err_power_off:
	__mali_c55_power_off(mali_c55);

	return ret;
}

static void mali_c55_remove(struct platform_device *pdev)
{
	struct mali_c55 *mali_c55 = platform_get_drvdata(pdev);

	kfree(mali_c55->context.registers);
	mali_c55_media_frameworks_deinit(mali_c55);
}

static const struct of_device_id mali_c55_of_match[] = {
	{ .compatible = "arm,mali-c55", },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, mali_c55_of_match);

static struct platform_driver mali_c55_driver = {
	.driver = {
		.name = "mali-c55",
		.of_match_table = mali_c55_of_match,
		.pm = &mali_c55_pm_ops,
	},
	.probe = mali_c55_probe,
	.remove = mali_c55_remove,
};

module_platform_driver(mali_c55_driver);

MODULE_AUTHOR("Daniel Scally <dan.scally@ideasonboard.com>");
MODULE_AUTHOR("Jacopo Mondi <jacopo.mondi@ideasonboard.com>");
MODULE_DESCRIPTION("ARM Mali-C55 ISP platform driver");
MODULE_LICENSE("GPL");
