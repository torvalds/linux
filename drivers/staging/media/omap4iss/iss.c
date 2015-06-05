/*
 * TI OMAP4 ISS V4L2 Driver
 *
 * Copyright (C) 2012, Texas Instruments
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#include "iss.h"
#include "iss_regs.h"

#define ISS_PRINT_REGISTER(iss, name)\
	dev_dbg(iss->dev, "###ISS " #name "=0x%08x\n", \
		iss_reg_read(iss, OMAP4_ISS_MEM_TOP, ISS_##name))

static void iss_print_status(struct iss_device *iss)
{
	dev_dbg(iss->dev, "-------------ISS HL Register dump-------------\n");

	ISS_PRINT_REGISTER(iss, HL_REVISION);
	ISS_PRINT_REGISTER(iss, HL_SYSCONFIG);
	ISS_PRINT_REGISTER(iss, HL_IRQSTATUS(5));
	ISS_PRINT_REGISTER(iss, HL_IRQENABLE_SET(5));
	ISS_PRINT_REGISTER(iss, HL_IRQENABLE_CLR(5));
	ISS_PRINT_REGISTER(iss, CTRL);
	ISS_PRINT_REGISTER(iss, CLKCTRL);
	ISS_PRINT_REGISTER(iss, CLKSTAT);

	dev_dbg(iss->dev, "-----------------------------------------------\n");
}

/*
 * omap4iss_flush - Post pending L3 bus writes by doing a register readback
 * @iss: OMAP4 ISS device
 *
 * In order to force posting of pending writes, we need to write and
 * readback the same register, in this case the revision register.
 *
 * See this link for reference:
 *   http://www.mail-archive.com/linux-omap@vger.kernel.org/msg08149.html
 */
void omap4iss_flush(struct iss_device *iss)
{
	iss_reg_write(iss, OMAP4_ISS_MEM_TOP, ISS_HL_REVISION, 0);
	iss_reg_read(iss, OMAP4_ISS_MEM_TOP, ISS_HL_REVISION);
}

/*
 * iss_isp_enable_interrupts - Enable ISS ISP interrupts.
 * @iss: OMAP4 ISS device
 */
static void omap4iss_isp_enable_interrupts(struct iss_device *iss)
{
	static const u32 isp_irq = ISP5_IRQ_OCP_ERR |
				   ISP5_IRQ_RSZ_FIFO_IN_BLK_ERR |
				   ISP5_IRQ_RSZ_FIFO_OVF |
				   ISP5_IRQ_RSZ_INT_DMA |
				   ISP5_IRQ_ISIF_INT(0);

	/* Enable ISP interrupts */
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_IRQSTATUS(0), isp_irq);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_IRQENABLE_SET(0),
		      isp_irq);
}

/*
 * iss_isp_disable_interrupts - Disable ISS interrupts.
 * @iss: OMAP4 ISS device
 */
static void omap4iss_isp_disable_interrupts(struct iss_device *iss)
{
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_IRQENABLE_CLR(0), ~0);
}

/*
 * iss_enable_interrupts - Enable ISS interrupts.
 * @iss: OMAP4 ISS device
 */
static void iss_enable_interrupts(struct iss_device *iss)
{
	static const u32 hl_irq = ISS_HL_IRQ_CSIA | ISS_HL_IRQ_CSIB
				| ISS_HL_IRQ_ISP(0);

	/* Enable HL interrupts */
	iss_reg_write(iss, OMAP4_ISS_MEM_TOP, ISS_HL_IRQSTATUS(5), hl_irq);
	iss_reg_write(iss, OMAP4_ISS_MEM_TOP, ISS_HL_IRQENABLE_SET(5), hl_irq);

	if (iss->regs[OMAP4_ISS_MEM_ISP_SYS1])
		omap4iss_isp_enable_interrupts(iss);
}

/*
 * iss_disable_interrupts - Disable ISS interrupts.
 * @iss: OMAP4 ISS device
 */
static void iss_disable_interrupts(struct iss_device *iss)
{
	if (iss->regs[OMAP4_ISS_MEM_ISP_SYS1])
		omap4iss_isp_disable_interrupts(iss);

	iss_reg_write(iss, OMAP4_ISS_MEM_TOP, ISS_HL_IRQENABLE_CLR(5), ~0);
}

int omap4iss_get_external_info(struct iss_pipeline *pipe,
			       struct media_link *link)
{
	struct iss_device *iss =
		container_of(pipe, struct iss_video, pipe)->iss;
	struct v4l2_subdev_format fmt;
	struct v4l2_ctrl *ctrl;
	int ret;

	if (!pipe->external)
		return 0;

	if (pipe->external_rate)
		return 0;

	memset(&fmt, 0, sizeof(fmt));

	fmt.pad = link->source->index;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(media_entity_to_v4l2_subdev(link->sink->entity),
			       pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return -EPIPE;

	pipe->external_bpp = omap4iss_video_format_info(fmt.format.code)->bpp;

	ctrl = v4l2_ctrl_find(pipe->external->ctrl_handler,
			      V4L2_CID_PIXEL_RATE);
	if (ctrl == NULL) {
		dev_warn(iss->dev, "no pixel rate control in subdev %s\n",
			 pipe->external->name);
		return -EPIPE;
	}

	pipe->external_rate = v4l2_ctrl_g_ctrl_int64(ctrl);

	return 0;
}

/*
 * Configure the bridge. Valid inputs are
 *
 * IPIPEIF_INPUT_CSI2A: CSI2a receiver
 * IPIPEIF_INPUT_CSI2B: CSI2b receiver
 *
 * The bridge and lane shifter are configured according to the selected input
 * and the ISP platform data.
 */
void omap4iss_configure_bridge(struct iss_device *iss,
			       enum ipipeif_input_entity input)
{
	u32 issctrl_val;
	u32 isp5ctrl_val;

	issctrl_val = iss_reg_read(iss, OMAP4_ISS_MEM_TOP, ISS_CTRL);
	issctrl_val &= ~ISS_CTRL_INPUT_SEL_MASK;
	issctrl_val &= ~ISS_CTRL_CLK_DIV_MASK;

	isp5ctrl_val = iss_reg_read(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_CTRL);

	switch (input) {
	case IPIPEIF_INPUT_CSI2A:
		issctrl_val |= ISS_CTRL_INPUT_SEL_CSI2A;
		break;

	case IPIPEIF_INPUT_CSI2B:
		issctrl_val |= ISS_CTRL_INPUT_SEL_CSI2B;
		break;

	default:
		return;
	}

	issctrl_val |= ISS_CTRL_SYNC_DETECT_VS_RAISING;

	isp5ctrl_val |= ISP5_CTRL_VD_PULSE_EXT | ISP5_CTRL_PSYNC_CLK_SEL |
			ISP5_CTRL_SYNC_ENABLE;

	iss_reg_write(iss, OMAP4_ISS_MEM_TOP, ISS_CTRL, issctrl_val);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_CTRL, isp5ctrl_val);
}

#ifdef ISS_ISR_DEBUG
static void iss_isr_dbg(struct iss_device *iss, u32 irqstatus)
{
	static const char * const name[] = {
		"ISP_0",
		"ISP_1",
		"ISP_2",
		"ISP_3",
		"CSIA",
		"CSIB",
		"CCP2_0",
		"CCP2_1",
		"CCP2_2",
		"CCP2_3",
		"CBUFF",
		"BTE",
		"SIMCOP_0",
		"SIMCOP_1",
		"SIMCOP_2",
		"SIMCOP_3",
		"CCP2_8",
		"HS_VS",
		"18",
		"19",
		"20",
		"21",
		"22",
		"23",
		"24",
		"25",
		"26",
		"27",
		"28",
		"29",
		"30",
		"31",
	};
	unsigned int i;

	dev_dbg(iss->dev, "ISS IRQ: ");

	for (i = 0; i < ARRAY_SIZE(name); i++) {
		if ((1 << i) & irqstatus)
			pr_cont("%s ", name[i]);
	}
	pr_cont("\n");
}

static void iss_isp_isr_dbg(struct iss_device *iss, u32 irqstatus)
{
	static const char * const name[] = {
		"ISIF_0",
		"ISIF_1",
		"ISIF_2",
		"ISIF_3",
		"IPIPEREQ",
		"IPIPELAST_PIX",
		"IPIPEDMA",
		"IPIPEBSC",
		"IPIPEHST",
		"IPIPEIF",
		"AEW",
		"AF",
		"H3A",
		"RSZ_REG",
		"RSZ_LAST_PIX",
		"RSZ_DMA",
		"RSZ_CYC_RZA",
		"RSZ_CYC_RZB",
		"RSZ_FIFO_OVF",
		"RSZ_FIFO_IN_BLK_ERR",
		"20",
		"21",
		"RSZ_EOF0",
		"RSZ_EOF1",
		"H3A_EOF",
		"IPIPE_EOF",
		"26",
		"IPIPE_DPC_INI",
		"IPIPE_DPC_RNEW0",
		"IPIPE_DPC_RNEW1",
		"30",
		"OCP_ERR",
	};
	unsigned int i;

	dev_dbg(iss->dev, "ISP IRQ: ");

	for (i = 0; i < ARRAY_SIZE(name); i++) {
		if ((1 << i) & irqstatus)
			pr_cont("%s ", name[i]);
	}
	pr_cont("\n");
}
#endif

/*
 * iss_isr - Interrupt Service Routine for ISS module.
 * @irq: Not used currently.
 * @_iss: Pointer to the OMAP4 ISS device
 *
 * Handles the corresponding callback if plugged in.
 *
 * Returns IRQ_HANDLED when IRQ was correctly handled, or IRQ_NONE when the
 * IRQ wasn't handled.
 */
static irqreturn_t iss_isr(int irq, void *_iss)
{
	static const u32 ipipeif_events = ISP5_IRQ_IPIPEIF_IRQ |
					  ISP5_IRQ_ISIF_INT(0);
	static const u32 resizer_events = ISP5_IRQ_RSZ_FIFO_IN_BLK_ERR |
					  ISP5_IRQ_RSZ_FIFO_OVF |
					  ISP5_IRQ_RSZ_INT_DMA;
	struct iss_device *iss = _iss;
	u32 irqstatus;

	irqstatus = iss_reg_read(iss, OMAP4_ISS_MEM_TOP, ISS_HL_IRQSTATUS(5));
	iss_reg_write(iss, OMAP4_ISS_MEM_TOP, ISS_HL_IRQSTATUS(5), irqstatus);

	if (irqstatus & ISS_HL_IRQ_CSIA)
		omap4iss_csi2_isr(&iss->csi2a);

	if (irqstatus & ISS_HL_IRQ_CSIB)
		omap4iss_csi2_isr(&iss->csi2b);

	if (irqstatus & ISS_HL_IRQ_ISP(0)) {
		u32 isp_irqstatus = iss_reg_read(iss, OMAP4_ISS_MEM_ISP_SYS1,
						 ISP5_IRQSTATUS(0));
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_IRQSTATUS(0),
			      isp_irqstatus);

		if (isp_irqstatus & ISP5_IRQ_OCP_ERR)
			dev_dbg(iss->dev, "ISP5 OCP Error!\n");

		if (isp_irqstatus & ipipeif_events) {
			omap4iss_ipipeif_isr(&iss->ipipeif,
					     isp_irqstatus & ipipeif_events);
		}

		if (isp_irqstatus & resizer_events)
			omap4iss_resizer_isr(&iss->resizer,
					     isp_irqstatus & resizer_events);

#ifdef ISS_ISR_DEBUG
		iss_isp_isr_dbg(iss, isp_irqstatus);
#endif
	}

	omap4iss_flush(iss);

#ifdef ISS_ISR_DEBUG
	iss_isr_dbg(iss, irqstatus);
#endif

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * Pipeline power management
 *
 * Entities must be powered up when part of a pipeline that contains at least
 * one open video device node.
 *
 * To achieve this use the entity use_count field to track the number of users.
 * For entities corresponding to video device nodes the use_count field stores
 * the users count of the node. For entities corresponding to subdevs the
 * use_count field stores the total number of users of all video device nodes
 * in the pipeline.
 *
 * The omap4iss_pipeline_pm_use() function must be called in the open() and
 * close() handlers of video device nodes. It increments or decrements the use
 * count of all subdev entities in the pipeline.
 *
 * To react to link management on powered pipelines, the link setup notification
 * callback updates the use count of all entities in the source and sink sides
 * of the link.
 */

/*
 * iss_pipeline_pm_use_count - Count the number of users of a pipeline
 * @entity: The entity
 *
 * Return the total number of users of all video device nodes in the pipeline.
 */
static int iss_pipeline_pm_use_count(struct media_entity *entity)
{
	struct media_entity_graph graph;
	int use = 0;

	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {
		if (media_entity_type(entity) == MEDIA_ENT_T_DEVNODE)
			use += entity->use_count;
	}

	return use;
}

/*
 * iss_pipeline_pm_power_one - Apply power change to an entity
 * @entity: The entity
 * @change: Use count change
 *
 * Change the entity use count by @change. If the entity is a subdev update its
 * power state by calling the core::s_power operation when the use count goes
 * from 0 to != 0 or from != 0 to 0.
 *
 * Return 0 on success or a negative error code on failure.
 */
static int iss_pipeline_pm_power_one(struct media_entity *entity, int change)
{
	struct v4l2_subdev *subdev;

	subdev = media_entity_type(entity) == MEDIA_ENT_T_V4L2_SUBDEV
	       ? media_entity_to_v4l2_subdev(entity) : NULL;

	if (entity->use_count == 0 && change > 0 && subdev != NULL) {
		int ret;

		ret = v4l2_subdev_call(subdev, core, s_power, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
	}

	entity->use_count += change;
	WARN_ON(entity->use_count < 0);

	if (entity->use_count == 0 && change < 0 && subdev != NULL)
		v4l2_subdev_call(subdev, core, s_power, 0);

	return 0;
}

/*
 * iss_pipeline_pm_power - Apply power change to all entities in a pipeline
 * @entity: The entity
 * @change: Use count change
 *
 * Walk the pipeline to update the use count and the power state of all non-node
 * entities.
 *
 * Return 0 on success or a negative error code on failure.
 */
static int iss_pipeline_pm_power(struct media_entity *entity, int change)
{
	struct media_entity_graph graph;
	struct media_entity *first = entity;
	int ret = 0;

	if (!change)
		return 0;

	media_entity_graph_walk_start(&graph, entity);

	while (!ret && (entity = media_entity_graph_walk_next(&graph)))
		if (media_entity_type(entity) != MEDIA_ENT_T_DEVNODE)
			ret = iss_pipeline_pm_power_one(entity, change);

	if (!ret)
		return 0;

	media_entity_graph_walk_start(&graph, first);

	while ((first = media_entity_graph_walk_next(&graph))
	       && first != entity)
		if (media_entity_type(first) != MEDIA_ENT_T_DEVNODE)
			iss_pipeline_pm_power_one(first, -change);

	return ret;
}

/*
 * omap4iss_pipeline_pm_use - Update the use count of an entity
 * @entity: The entity
 * @use: Use (1) or stop using (0) the entity
 *
 * Update the use count of all entities in the pipeline and power entities on or
 * off accordingly.
 *
 * Return 0 on success or a negative error code on failure. Powering entities
 * off is assumed to never fail. No failure can occur when the use parameter is
 * set to 0.
 */
int omap4iss_pipeline_pm_use(struct media_entity *entity, int use)
{
	int change = use ? 1 : -1;
	int ret;

	mutex_lock(&entity->parent->graph_mutex);

	/* Apply use count to node. */
	entity->use_count += change;
	WARN_ON(entity->use_count < 0);

	/* Apply power change to connected non-nodes. */
	ret = iss_pipeline_pm_power(entity, change);
	if (ret < 0)
		entity->use_count -= change;

	mutex_unlock(&entity->parent->graph_mutex);

	return ret;
}

/*
 * iss_pipeline_link_notify - Link management notification callback
 * @link: The link
 * @flags: New link flags that will be applied
 *
 * React to link management on powered pipelines by updating the use count of
 * all entities in the source and sink sides of the link. Entities are powered
 * on or off accordingly.
 *
 * Return 0 on success or a negative error code on failure. Powering entities
 * off is assumed to never fail. This function will not fail for disconnection
 * events.
 */
static int iss_pipeline_link_notify(struct media_link *link, u32 flags,
				    unsigned int notification)
{
	struct media_entity *source = link->source->entity;
	struct media_entity *sink = link->sink->entity;
	int source_use = iss_pipeline_pm_use_count(source);
	int sink_use = iss_pipeline_pm_use_count(sink);
	int ret;

	if (notification == MEDIA_DEV_NOTIFY_POST_LINK_CH &&
	    !(link->flags & MEDIA_LNK_FL_ENABLED)) {
		/* Powering off entities is assumed to never fail. */
		iss_pipeline_pm_power(source, -sink_use);
		iss_pipeline_pm_power(sink, -source_use);
		return 0;
	}

	if (notification == MEDIA_DEV_NOTIFY_POST_LINK_CH &&
		(flags & MEDIA_LNK_FL_ENABLED)) {
		ret = iss_pipeline_pm_power(source, sink_use);
		if (ret < 0)
			return ret;

		ret = iss_pipeline_pm_power(sink, source_use);
		if (ret < 0)
			iss_pipeline_pm_power(source, -sink_use);

		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Pipeline stream management
 */

/*
 * iss_pipeline_disable - Disable streaming on a pipeline
 * @pipe: ISS pipeline
 * @until: entity at which to stop pipeline walk
 *
 * Walk the entities chain starting at the pipeline output video node and stop
 * all modules in the chain. Wait synchronously for the modules to be stopped if
 * necessary.
 *
 * If the until argument isn't NULL, stop the pipeline walk when reaching the
 * until entity. This is used to disable a partially started pipeline due to a
 * subdev start error.
 */
static int iss_pipeline_disable(struct iss_pipeline *pipe,
				struct media_entity *until)
{
	struct iss_device *iss = pipe->output->iss;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	int failure = 0;
	int ret;

	entity = &pipe->output->video.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		entity = pad->entity;
		if (entity == until)
			break;

		subdev = media_entity_to_v4l2_subdev(entity);
		ret = v4l2_subdev_call(subdev, video, s_stream, 0);
		if (ret < 0) {
			dev_dbg(iss->dev, "%s: module stop timeout.\n",
				subdev->name);
			/* If the entity failed to stopped, assume it has
			 * crashed. Mark it as such, the ISS will be reset when
			 * applications will release it.
			 */
			iss->crashed |= 1U << subdev->entity.id;
			failure = -ETIMEDOUT;
		}
	}

	return failure;
}

/*
 * iss_pipeline_enable - Enable streaming on a pipeline
 * @pipe: ISS pipeline
 * @mode: Stream mode (single shot or continuous)
 *
 * Walk the entities chain starting at the pipeline output video node and start
 * all modules in the chain in the given mode.
 *
 * Return 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise.
 */
static int iss_pipeline_enable(struct iss_pipeline *pipe,
			       enum iss_pipeline_stream_state mode)
{
	struct iss_device *iss = pipe->output->iss;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	unsigned long flags;
	int ret;

	/* If one of the entities in the pipeline has crashed it will not work
	 * properly. Refuse to start streaming in that case. This check must be
	 * performed before the loop below to avoid starting entities if the
	 * pipeline won't start anyway (those entities would then likely fail to
	 * stop, making the problem worse).
	 */
	if (pipe->entities & iss->crashed)
		return -EIO;

	spin_lock_irqsave(&pipe->lock, flags);
	pipe->state &= ~(ISS_PIPELINE_IDLE_INPUT | ISS_PIPELINE_IDLE_OUTPUT);
	spin_unlock_irqrestore(&pipe->lock, flags);

	pipe->do_propagation = false;

	entity = &pipe->output->video.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, mode);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			iss_pipeline_disable(pipe, entity);
			return ret;
		}

		if (subdev == &iss->csi2a.subdev ||
		    subdev == &iss->csi2b.subdev)
			pipe->do_propagation = true;
	}

	iss_print_status(pipe->output->iss);
	return 0;
}

/*
 * omap4iss_pipeline_set_stream - Enable/disable streaming on a pipeline
 * @pipe: ISS pipeline
 * @state: Stream state (stopped, single shot or continuous)
 *
 * Set the pipeline to the given stream state. Pipelines can be started in
 * single-shot or continuous mode.
 *
 * Return 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise. The pipeline state is not updated when the operation
 * fails, except when stopping the pipeline.
 */
int omap4iss_pipeline_set_stream(struct iss_pipeline *pipe,
				 enum iss_pipeline_stream_state state)
{
	int ret;

	if (state == ISS_PIPELINE_STREAM_STOPPED)
		ret = iss_pipeline_disable(pipe, NULL);
	else
		ret = iss_pipeline_enable(pipe, state);

	if (ret == 0 || state == ISS_PIPELINE_STREAM_STOPPED)
		pipe->stream_state = state;

	return ret;
}

/*
 * omap4iss_pipeline_cancel_stream - Cancel stream on a pipeline
 * @pipe: ISS pipeline
 *
 * Cancelling a stream mark all buffers on all video nodes in the pipeline as
 * erroneous and makes sure no new buffer can be queued. This function is called
 * when a fatal error that prevents any further operation on the pipeline
 * occurs.
 */
void omap4iss_pipeline_cancel_stream(struct iss_pipeline *pipe)
{
	if (pipe->input)
		omap4iss_video_cancel_stream(pipe->input);
	if (pipe->output)
		omap4iss_video_cancel_stream(pipe->output);
}

/*
 * iss_pipeline_is_last - Verify if entity has an enabled link to the output
 *			  video node
 * @me: ISS module's media entity
 *
 * Returns 1 if the entity has an enabled link to the output video node or 0
 * otherwise. It's true only while pipeline can have no more than one output
 * node.
 */
static int iss_pipeline_is_last(struct media_entity *me)
{
	struct iss_pipeline *pipe;
	struct media_pad *pad;

	if (!me->pipe)
		return 0;
	pipe = to_iss_pipeline(me);
	if (pipe->stream_state == ISS_PIPELINE_STREAM_STOPPED)
		return 0;
	pad = media_entity_remote_pad(&pipe->output->pad);
	return pad->entity == me;
}

static int iss_reset(struct iss_device *iss)
{
	unsigned int timeout;

	iss_reg_set(iss, OMAP4_ISS_MEM_TOP, ISS_HL_SYSCONFIG,
		    ISS_HL_SYSCONFIG_SOFTRESET);

	timeout = iss_poll_condition_timeout(
		!(iss_reg_read(iss, OMAP4_ISS_MEM_TOP, ISS_HL_SYSCONFIG) &
		ISS_HL_SYSCONFIG_SOFTRESET), 1000, 10, 100);
	if (timeout) {
		dev_err(iss->dev, "ISS reset timeout\n");
		return -ETIMEDOUT;
	}

	iss->crashed = 0;
	return 0;
}

static int iss_isp_reset(struct iss_device *iss)
{
	unsigned int timeout;

	/* Fist, ensure that the ISP is IDLE (no transactions happening) */
	iss_reg_update(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_SYSCONFIG,
		       ISP5_SYSCONFIG_STANDBYMODE_MASK,
		       ISP5_SYSCONFIG_STANDBYMODE_SMART);

	iss_reg_set(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_CTRL, ISP5_CTRL_MSTANDBY);

	timeout = iss_poll_condition_timeout(
		iss_reg_read(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_CTRL) &
		ISP5_CTRL_MSTANDBY_WAIT, 1000000, 1000, 1500);
	if (timeout) {
		dev_err(iss->dev, "ISP5 standby timeout\n");
		return -ETIMEDOUT;
	}

	/* Now finally, do the reset */
	iss_reg_set(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_SYSCONFIG,
		    ISP5_SYSCONFIG_SOFTRESET);

	timeout = iss_poll_condition_timeout(
		!(iss_reg_read(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_SYSCONFIG) &
		ISP5_SYSCONFIG_SOFTRESET), 1000000, 1000, 1500);
	if (timeout) {
		dev_err(iss->dev, "ISP5 reset timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * iss_module_sync_idle - Helper to sync module with its idle state
 * @me: ISS submodule's media entity
 * @wait: ISS submodule's wait queue for streamoff/interrupt synchronization
 * @stopping: flag which tells module wants to stop
 *
 * This function checks if ISS submodule needs to wait for next interrupt. If
 * yes, makes the caller to sleep while waiting for such event.
 */
int omap4iss_module_sync_idle(struct media_entity *me, wait_queue_head_t *wait,
			      atomic_t *stopping)
{
	struct iss_pipeline *pipe = to_iss_pipeline(me);
	struct iss_video *video = pipe->output;
	unsigned long flags;

	if (pipe->stream_state == ISS_PIPELINE_STREAM_STOPPED ||
	    (pipe->stream_state == ISS_PIPELINE_STREAM_SINGLESHOT &&
	     !iss_pipeline_ready(pipe)))
		return 0;

	/*
	 * atomic_set() doesn't include memory barrier on ARM platform for SMP
	 * scenario. We'll call it here to avoid race conditions.
	 */
	atomic_set(stopping, 1);
	smp_wmb();

	/*
	 * If module is the last one, it's writing to memory. In this case,
	 * it's necessary to check if the module is already paused due to
	 * DMA queue underrun or if it has to wait for next interrupt to be
	 * idle.
	 * If it isn't the last one, the function won't sleep but *stopping
	 * will still be set to warn next submodule caller's interrupt the
	 * module wants to be idle.
	 */
	if (!iss_pipeline_is_last(me))
		return 0;

	spin_lock_irqsave(&video->qlock, flags);
	if (video->dmaqueue_flags & ISS_VIDEO_DMAQUEUE_UNDERRUN) {
		spin_unlock_irqrestore(&video->qlock, flags);
		atomic_set(stopping, 0);
		smp_wmb();
		return 0;
	}
	spin_unlock_irqrestore(&video->qlock, flags);
	if (!wait_event_timeout(*wait, !atomic_read(stopping),
				msecs_to_jiffies(1000))) {
		atomic_set(stopping, 0);
		smp_wmb();
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * omap4iss_module_sync_is_stopped - Helper to verify if module was stopping
 * @wait: ISS submodule's wait queue for streamoff/interrupt synchronization
 * @stopping: flag which tells module wants to stop
 *
 * This function checks if ISS submodule was stopping. In case of yes, it
 * notices the caller by setting stopping to 0 and waking up the wait queue.
 * Returns 1 if it was stopping or 0 otherwise.
 */
int omap4iss_module_sync_is_stopping(wait_queue_head_t *wait,
				     atomic_t *stopping)
{
	if (atomic_cmpxchg(stopping, 1, 0)) {
		wake_up(wait);
		return 1;
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * Clock management
 */

#define ISS_CLKCTRL_MASK	(ISS_CLKCTRL_CSI2_A |\
				 ISS_CLKCTRL_CSI2_B |\
				 ISS_CLKCTRL_ISP)

static int __iss_subclk_update(struct iss_device *iss)
{
	u32 clk = 0;
	int ret = 0, timeout = 1000;

	if (iss->subclk_resources & OMAP4_ISS_SUBCLK_CSI2_A)
		clk |= ISS_CLKCTRL_CSI2_A;

	if (iss->subclk_resources & OMAP4_ISS_SUBCLK_CSI2_B)
		clk |= ISS_CLKCTRL_CSI2_B;

	if (iss->subclk_resources & OMAP4_ISS_SUBCLK_ISP)
		clk |= ISS_CLKCTRL_ISP;

	iss_reg_update(iss, OMAP4_ISS_MEM_TOP, ISS_CLKCTRL,
		       ISS_CLKCTRL_MASK, clk);

	/* Wait for HW assertion */
	while (--timeout > 0) {
		udelay(1);
		if ((iss_reg_read(iss, OMAP4_ISS_MEM_TOP, ISS_CLKSTAT) &
		    ISS_CLKCTRL_MASK) == clk)
			break;
	}

	if (!timeout)
		ret = -EBUSY;

	return ret;
}

int omap4iss_subclk_enable(struct iss_device *iss,
			    enum iss_subclk_resource res)
{
	iss->subclk_resources |= res;

	return __iss_subclk_update(iss);
}

int omap4iss_subclk_disable(struct iss_device *iss,
			     enum iss_subclk_resource res)
{
	iss->subclk_resources &= ~res;

	return __iss_subclk_update(iss);
}

#define ISS_ISP5_CLKCTRL_MASK	(ISP5_CTRL_BL_CLK_ENABLE |\
				 ISP5_CTRL_ISIF_CLK_ENABLE |\
				 ISP5_CTRL_H3A_CLK_ENABLE |\
				 ISP5_CTRL_RSZ_CLK_ENABLE |\
				 ISP5_CTRL_IPIPE_CLK_ENABLE |\
				 ISP5_CTRL_IPIPEIF_CLK_ENABLE)

static void __iss_isp_subclk_update(struct iss_device *iss)
{
	u32 clk = 0;

	if (iss->isp_subclk_resources & OMAP4_ISS_ISP_SUBCLK_ISIF)
		clk |= ISP5_CTRL_ISIF_CLK_ENABLE;

	if (iss->isp_subclk_resources & OMAP4_ISS_ISP_SUBCLK_H3A)
		clk |= ISP5_CTRL_H3A_CLK_ENABLE;

	if (iss->isp_subclk_resources & OMAP4_ISS_ISP_SUBCLK_RSZ)
		clk |= ISP5_CTRL_RSZ_CLK_ENABLE;

	if (iss->isp_subclk_resources & OMAP4_ISS_ISP_SUBCLK_IPIPE)
		clk |= ISP5_CTRL_IPIPE_CLK_ENABLE;

	if (iss->isp_subclk_resources & OMAP4_ISS_ISP_SUBCLK_IPIPEIF)
		clk |= ISP5_CTRL_IPIPEIF_CLK_ENABLE;

	if (clk)
		clk |= ISP5_CTRL_BL_CLK_ENABLE;

	iss_reg_update(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_CTRL,
		       ISS_ISP5_CLKCTRL_MASK, clk);
}

void omap4iss_isp_subclk_enable(struct iss_device *iss,
				enum iss_isp_subclk_resource res)
{
	iss->isp_subclk_resources |= res;

	__iss_isp_subclk_update(iss);
}

void omap4iss_isp_subclk_disable(struct iss_device *iss,
				 enum iss_isp_subclk_resource res)
{
	iss->isp_subclk_resources &= ~res;

	__iss_isp_subclk_update(iss);
}

/*
 * iss_enable_clocks - Enable ISS clocks
 * @iss: OMAP4 ISS device
 *
 * Return 0 if successful, or clk_enable return value if any of tthem fails.
 */
static int iss_enable_clocks(struct iss_device *iss)
{
	int ret;

	ret = clk_enable(iss->iss_fck);
	if (ret) {
		dev_err(iss->dev, "clk_enable iss_fck failed\n");
		return ret;
	}

	ret = clk_enable(iss->iss_ctrlclk);
	if (ret) {
		dev_err(iss->dev, "clk_enable iss_ctrlclk failed\n");
		clk_disable(iss->iss_fck);
		return ret;
	}

	return 0;
}

/*
 * iss_disable_clocks - Disable ISS clocks
 * @iss: OMAP4 ISS device
 */
static void iss_disable_clocks(struct iss_device *iss)
{
	clk_disable(iss->iss_ctrlclk);
	clk_disable(iss->iss_fck);
}

static int iss_get_clocks(struct iss_device *iss)
{
	iss->iss_fck = devm_clk_get(iss->dev, "iss_fck");
	if (IS_ERR(iss->iss_fck)) {
		dev_err(iss->dev, "Unable to get iss_fck clock info\n");
		return PTR_ERR(iss->iss_fck);
	}

	iss->iss_ctrlclk = devm_clk_get(iss->dev, "iss_ctrlclk");
	if (IS_ERR(iss->iss_ctrlclk)) {
		dev_err(iss->dev, "Unable to get iss_ctrlclk clock info\n");
		return PTR_ERR(iss->iss_ctrlclk);
	}

	return 0;
}

/*
 * omap4iss_get - Acquire the ISS resource.
 *
 * Initializes the clocks for the first acquire.
 *
 * Increment the reference count on the ISS. If the first reference is taken,
 * enable clocks and power-up all submodules.
 *
 * Return a pointer to the ISS device structure, or NULL if an error occurred.
 */
struct iss_device *omap4iss_get(struct iss_device *iss)
{
	struct iss_device *__iss = iss;

	if (iss == NULL)
		return NULL;

	mutex_lock(&iss->iss_mutex);
	if (iss->ref_count > 0)
		goto out;

	if (iss_enable_clocks(iss) < 0) {
		__iss = NULL;
		goto out;
	}

	iss_enable_interrupts(iss);

out:
	if (__iss != NULL)
		iss->ref_count++;
	mutex_unlock(&iss->iss_mutex);

	return __iss;
}

/*
 * omap4iss_put - Release the ISS
 *
 * Decrement the reference count on the ISS. If the last reference is released,
 * power-down all submodules, disable clocks and free temporary buffers.
 */
void omap4iss_put(struct iss_device *iss)
{
	if (iss == NULL)
		return;

	mutex_lock(&iss->iss_mutex);
	BUG_ON(iss->ref_count == 0);
	if (--iss->ref_count == 0) {
		iss_disable_interrupts(iss);
		/* Reset the ISS if an entity has failed to stop. This is the
		 * only way to recover from such conditions, although it would
		 * be worth investigating whether resetting the ISP only can't
		 * fix the problem in some cases.
		 */
		if (iss->crashed)
			iss_reset(iss);
		iss_disable_clocks(iss);
	}
	mutex_unlock(&iss->iss_mutex);
}

static int iss_map_mem_resource(struct platform_device *pdev,
				struct iss_device *iss,
				enum iss_mem_resources res)
{
	struct resource *mem;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, res);

	iss->regs[res] = devm_ioremap_resource(iss->dev, mem);

	return PTR_ERR_OR_ZERO(iss->regs[res]);
}

static void iss_unregister_entities(struct iss_device *iss)
{
	omap4iss_resizer_unregister_entities(&iss->resizer);
	omap4iss_ipipe_unregister_entities(&iss->ipipe);
	omap4iss_ipipeif_unregister_entities(&iss->ipipeif);
	omap4iss_csi2_unregister_entities(&iss->csi2a);
	omap4iss_csi2_unregister_entities(&iss->csi2b);

	v4l2_device_unregister(&iss->v4l2_dev);
	media_device_unregister(&iss->media_dev);
}

/*
 * iss_register_subdev_group - Register a group of subdevices
 * @iss: OMAP4 ISS device
 * @board_info: I2C subdevs board information array
 *
 * Register all I2C subdevices in the board_info array. The array must be
 * terminated by a NULL entry, and the first entry must be the sensor.
 *
 * Return a pointer to the sensor media entity if it has been successfully
 * registered, or NULL otherwise.
 */
static struct v4l2_subdev *
iss_register_subdev_group(struct iss_device *iss,
		     struct iss_subdev_i2c_board_info *board_info)
{
	struct v4l2_subdev *sensor = NULL;
	unsigned int first;

	if (board_info->board_info == NULL)
		return NULL;

	for (first = 1; board_info->board_info; ++board_info, first = 0) {
		struct v4l2_subdev *subdev;
		struct i2c_adapter *adapter;

		adapter = i2c_get_adapter(board_info->i2c_adapter_id);
		if (adapter == NULL) {
			dev_err(iss->dev,
				"%s: Unable to get I2C adapter %d for device %s\n",
				__func__, board_info->i2c_adapter_id,
				board_info->board_info->type);
			continue;
		}

		subdev = v4l2_i2c_new_subdev_board(&iss->v4l2_dev, adapter,
				board_info->board_info, NULL);
		if (subdev == NULL) {
			dev_err(iss->dev, "Unable to register subdev %s\n",
				board_info->board_info->type);
			continue;
		}

		if (first)
			sensor = subdev;
	}

	return sensor;
}

static int iss_register_entities(struct iss_device *iss)
{
	struct iss_platform_data *pdata = iss->pdata;
	struct iss_v4l2_subdevs_group *subdevs;
	int ret;

	iss->media_dev.dev = iss->dev;
	strlcpy(iss->media_dev.model, "TI OMAP4 ISS",
		sizeof(iss->media_dev.model));
	iss->media_dev.hw_revision = iss->revision;
	iss->media_dev.link_notify = iss_pipeline_link_notify;
	ret = media_device_register(&iss->media_dev);
	if (ret < 0) {
		dev_err(iss->dev, "Media device registration failed (%d)\n",
			ret);
		return ret;
	}

	iss->v4l2_dev.mdev = &iss->media_dev;
	ret = v4l2_device_register(iss->dev, &iss->v4l2_dev);
	if (ret < 0) {
		dev_err(iss->dev, "V4L2 device registration failed (%d)\n",
			ret);
		goto done;
	}

	/* Register internal entities */
	ret = omap4iss_csi2_register_entities(&iss->csi2a, &iss->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap4iss_csi2_register_entities(&iss->csi2b, &iss->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap4iss_ipipeif_register_entities(&iss->ipipeif, &iss->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap4iss_ipipe_register_entities(&iss->ipipe, &iss->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap4iss_resizer_register_entities(&iss->resizer, &iss->v4l2_dev);
	if (ret < 0)
		goto done;

	/* Register external entities */
	for (subdevs = pdata->subdevs; subdevs && subdevs->subdevs; ++subdevs) {
		struct v4l2_subdev *sensor;
		struct media_entity *input;
		unsigned int flags;
		unsigned int pad;

		sensor = iss_register_subdev_group(iss, subdevs->subdevs);
		if (sensor == NULL)
			continue;

		sensor->host_priv = subdevs;

		/* Connect the sensor to the correct interface module.
		 * CSI2a receiver through CSIPHY1, or
		 * CSI2b receiver through CSIPHY2
		 */
		switch (subdevs->interface) {
		case ISS_INTERFACE_CSI2A_PHY1:
			input = &iss->csi2a.subdev.entity;
			pad = CSI2_PAD_SINK;
			flags = MEDIA_LNK_FL_IMMUTABLE
			      | MEDIA_LNK_FL_ENABLED;
			break;

		case ISS_INTERFACE_CSI2B_PHY2:
			input = &iss->csi2b.subdev.entity;
			pad = CSI2_PAD_SINK;
			flags = MEDIA_LNK_FL_IMMUTABLE
			      | MEDIA_LNK_FL_ENABLED;
			break;

		default:
			dev_err(iss->dev, "invalid interface type %u\n",
				subdevs->interface);
			ret = -EINVAL;
			goto done;
		}

		ret = media_entity_create_link(&sensor->entity, 0, input, pad,
					       flags);
		if (ret < 0)
			goto done;
	}

	ret = v4l2_device_register_subdev_nodes(&iss->v4l2_dev);

done:
	if (ret < 0)
		iss_unregister_entities(iss);

	return ret;
}

static void iss_cleanup_modules(struct iss_device *iss)
{
	omap4iss_csi2_cleanup(iss);
	omap4iss_ipipeif_cleanup(iss);
	omap4iss_ipipe_cleanup(iss);
	omap4iss_resizer_cleanup(iss);
}

static int iss_initialize_modules(struct iss_device *iss)
{
	int ret;

	ret = omap4iss_csiphy_init(iss);
	if (ret < 0) {
		dev_err(iss->dev, "CSI PHY initialization failed\n");
		goto error_csiphy;
	}

	ret = omap4iss_csi2_init(iss);
	if (ret < 0) {
		dev_err(iss->dev, "CSI2 initialization failed\n");
		goto error_csi2;
	}

	ret = omap4iss_ipipeif_init(iss);
	if (ret < 0) {
		dev_err(iss->dev, "ISP IPIPEIF initialization failed\n");
		goto error_ipipeif;
	}

	ret = omap4iss_ipipe_init(iss);
	if (ret < 0) {
		dev_err(iss->dev, "ISP IPIPE initialization failed\n");
		goto error_ipipe;
	}

	ret = omap4iss_resizer_init(iss);
	if (ret < 0) {
		dev_err(iss->dev, "ISP RESIZER initialization failed\n");
		goto error_resizer;
	}

	/* Connect the submodules. */
	ret = media_entity_create_link(
			&iss->csi2a.subdev.entity, CSI2_PAD_SOURCE,
			&iss->ipipeif.subdev.entity, IPIPEIF_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&iss->csi2b.subdev.entity, CSI2_PAD_SOURCE,
			&iss->ipipeif.subdev.entity, IPIPEIF_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&iss->ipipeif.subdev.entity, IPIPEIF_PAD_SOURCE_VP,
			&iss->resizer.subdev.entity, RESIZER_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&iss->ipipeif.subdev.entity, IPIPEIF_PAD_SOURCE_VP,
			&iss->ipipe.subdev.entity, IPIPE_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&iss->ipipe.subdev.entity, IPIPE_PAD_SOURCE_VP,
			&iss->resizer.subdev.entity, RESIZER_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	return 0;

error_link:
	omap4iss_resizer_cleanup(iss);
error_resizer:
	omap4iss_ipipe_cleanup(iss);
error_ipipe:
	omap4iss_ipipeif_cleanup(iss);
error_ipipeif:
	omap4iss_csi2_cleanup(iss);
error_csi2:
error_csiphy:
	return ret;
}

static int iss_probe(struct platform_device *pdev)
{
	struct iss_platform_data *pdata = pdev->dev.platform_data;
	struct iss_device *iss;
	unsigned int i;
	int ret;

	if (pdata == NULL)
		return -EINVAL;

	iss = devm_kzalloc(&pdev->dev, sizeof(*iss), GFP_KERNEL);
	if (!iss)
		return -ENOMEM;

	mutex_init(&iss->iss_mutex);

	iss->dev = &pdev->dev;
	iss->pdata = pdata;

	iss->raw_dmamask = DMA_BIT_MASK(32);
	iss->dev->dma_mask = &iss->raw_dmamask;
	iss->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	platform_set_drvdata(pdev, iss);

	/*
	 * TODO: When implementing DT support switch to syscon regmap lookup by
	 * phandle.
	 */
	iss->syscon = syscon_regmap_lookup_by_compatible("syscon");
	if (IS_ERR(iss->syscon)) {
		ret = PTR_ERR(iss->syscon);
		goto error;
	}

	/* Clocks */
	ret = iss_map_mem_resource(pdev, iss, OMAP4_ISS_MEM_TOP);
	if (ret < 0)
		goto error;

	ret = iss_get_clocks(iss);
	if (ret < 0)
		goto error;

	if (omap4iss_get(iss) == NULL)
		goto error;

	ret = iss_reset(iss);
	if (ret < 0)
		goto error_iss;

	iss->revision = iss_reg_read(iss, OMAP4_ISS_MEM_TOP, ISS_HL_REVISION);
	dev_info(iss->dev, "Revision %08x found\n", iss->revision);

	for (i = 1; i < OMAP4_ISS_MEM_LAST; i++) {
		ret = iss_map_mem_resource(pdev, iss, i);
		if (ret)
			goto error_iss;
	}

	/* Configure BTE BW_LIMITER field to max recommended value (1 GB) */
	iss_reg_update(iss, OMAP4_ISS_MEM_BTE, BTE_CTRL,
		       BTE_CTRL_BW_LIMITER_MASK,
		       18 << BTE_CTRL_BW_LIMITER_SHIFT);

	/* Perform ISP reset */
	ret = omap4iss_subclk_enable(iss, OMAP4_ISS_SUBCLK_ISP);
	if (ret < 0)
		goto error_iss;

	ret = iss_isp_reset(iss);
	if (ret < 0)
		goto error_iss;

	dev_info(iss->dev, "ISP Revision %08x found\n",
		 iss_reg_read(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_REVISION));

	/* Interrupt */
	iss->irq_num = platform_get_irq(pdev, 0);
	if (iss->irq_num <= 0) {
		dev_err(iss->dev, "No IRQ resource\n");
		ret = -ENODEV;
		goto error_iss;
	}

	if (devm_request_irq(iss->dev, iss->irq_num, iss_isr, IRQF_SHARED,
			     "OMAP4 ISS", iss)) {
		dev_err(iss->dev, "Unable to request IRQ\n");
		ret = -EINVAL;
		goto error_iss;
	}

	/* Entities */
	ret = iss_initialize_modules(iss);
	if (ret < 0)
		goto error_iss;

	ret = iss_register_entities(iss);
	if (ret < 0)
		goto error_modules;

	omap4iss_put(iss);

	return 0;

error_modules:
	iss_cleanup_modules(iss);
error_iss:
	omap4iss_put(iss);
error:
	platform_set_drvdata(pdev, NULL);

	mutex_destroy(&iss->iss_mutex);

	return ret;
}

static int iss_remove(struct platform_device *pdev)
{
	struct iss_device *iss = platform_get_drvdata(pdev);

	iss_unregister_entities(iss);
	iss_cleanup_modules(iss);

	return 0;
}

static struct platform_device_id omap4iss_id_table[] = {
	{ "omap4iss", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, omap4iss_id_table);

static struct platform_driver iss_driver = {
	.probe		= iss_probe,
	.remove		= iss_remove,
	.id_table	= omap4iss_id_table,
	.driver = {
		.name	= "omap4iss",
	},
};

module_platform_driver(iss_driver);

MODULE_DESCRIPTION("TI OMAP4 ISS driver");
MODULE_AUTHOR("Sergio Aguirre <sergio.a.aguirre@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(ISS_VIDEO_DRIVER_VERSION);
