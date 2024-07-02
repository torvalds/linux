/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for STM32 Digital Camera Memory Interface Pixel Processor
 *
 * Copyright (C) STMicroelectronics SA 2023
 * Authors: Hugues Fruchet <hugues.fruchet@foss.st.com>
 *          Alain Volmat <alain.volmat@foss.st.com>
 *          for STMicroelectronics.
 */

#ifndef _DCMIPP_COMMON_H_
#define _DCMIPP_COMMON_H_

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <media/media-device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define DCMIPP_PDEV_NAME "dcmipp"

#define DCMIPP_FRAME_MAX_WIDTH 4096
#define DCMIPP_FRAME_MAX_HEIGHT 2160
#define DCMIPP_FRAME_MIN_WIDTH 16
#define DCMIPP_FRAME_MIN_HEIGHT 16

#define DCMIPP_FMT_WIDTH_DEFAULT  640
#define DCMIPP_FMT_HEIGHT_DEFAULT 480

#define DCMIPP_COLORSPACE_DEFAULT	V4L2_COLORSPACE_REC709
#define DCMIPP_YCBCR_ENC_DEFAULT	V4L2_YCBCR_ENC_DEFAULT
#define DCMIPP_QUANTIZATION_DEFAULT	V4L2_QUANTIZATION_DEFAULT
#define DCMIPP_XFER_FUNC_DEFAULT	V4L2_XFER_FUNC_DEFAULT

/**
 * dcmipp_colorimetry_clamp() - Adjust colorimetry parameters
 *
 * @fmt:		the pointer to struct v4l2_pix_format or
 *			struct v4l2_mbus_framefmt
 *
 * Entities must check if colorimetry given by the userspace is valid, if not
 * then set them as DEFAULT
 */
#define dcmipp_colorimetry_clamp(fmt)					\
do {									\
	if ((fmt)->colorspace == V4L2_COLORSPACE_DEFAULT ||		\
	    (fmt)->colorspace > V4L2_COLORSPACE_DCI_P3) {		\
		(fmt)->colorspace = DCMIPP_COLORSPACE_DEFAULT;		\
		(fmt)->ycbcr_enc = DCMIPP_YCBCR_ENC_DEFAULT;		\
		(fmt)->quantization = DCMIPP_QUANTIZATION_DEFAULT;	\
		(fmt)->xfer_func = DCMIPP_XFER_FUNC_DEFAULT;		\
	}								\
	if ((fmt)->ycbcr_enc > V4L2_YCBCR_ENC_SMPTE240M)		\
		(fmt)->ycbcr_enc = DCMIPP_YCBCR_ENC_DEFAULT;		\
	if ((fmt)->quantization > V4L2_QUANTIZATION_LIM_RANGE)		\
		(fmt)->quantization = DCMIPP_QUANTIZATION_DEFAULT;	\
	if ((fmt)->xfer_func > V4L2_XFER_FUNC_SMPTE2084)		\
		(fmt)->xfer_func = DCMIPP_XFER_FUNC_DEFAULT;		\
} while (0)

/**
 * struct dcmipp_ent_device - core struct that represents a node in the topology
 *
 * @ent:		the pointer to struct media_entity for the node
 * @pads:		the list of pads of the node
 * @bus:		struct v4l2_mbus_config_parallel describing input bus
 * @bus_type:		type of input bus (parallel or BT656)
 * @handler:		irq handler dedicated to the subdev
 * @handler_ret:	value returned by the irq handler
 * @thread_fn:		threaded irq handler
 *
 * The DCMIPP provides a single IRQ line and a IRQ status registers for all
 * subdevs, hence once the main irq handler (registered at probe time) is
 * called, it will chain calls to the irq handler of each the subdevs of the
 * pipelines, using the handler/handler_ret/thread_fn variables.
 *
 * Each node of the topology must create a dcmipp_ent_device struct.
 * Depending on the node it will be of an instance of v4l2_subdev or
 * video_device struct where both contains a struct media_entity.
 * Those structures should embedded the dcmipp_ent_device struct through
 * v4l2_set_subdevdata() and video_set_drvdata() respectivaly, allowing the
 * dcmipp_ent_device struct to be retrieved from the corresponding struct
 * media_entity
 */
struct dcmipp_ent_device {
	struct media_entity *ent;
	struct media_pad *pads;

	/* Parallel input device */
	struct v4l2_mbus_config_parallel bus;
	enum v4l2_mbus_type bus_type;
	irq_handler_t handler;
	irqreturn_t handler_ret;
	irq_handler_t thread_fn;
};

/**
 * dcmipp_pads_init - initialize pads
 *
 * @num_pads:	number of pads to initialize
 * @pads_flags:	flags to use in each pad
 *
 * Helper functions to allocate/initialize pads
 */
struct media_pad *dcmipp_pads_init(u16 num_pads,
				   const unsigned long *pads_flags);

/**
 * dcmipp_pads_cleanup - free pads
 *
 * @pads: pointer to the pads
 *
 * Helper function to free the pads initialized with dcmipp_pads_init
 */
static inline void dcmipp_pads_cleanup(struct media_pad *pads)
{
	kfree(pads);
}

/**
 * dcmipp_ent_sd_register - initialize and register a subdev node
 *
 * @ved:	the dcmipp_ent_device struct to be initialize
 * @sd:		the v4l2_subdev struct to be initialize and registered
 * @v4l2_dev:	the v4l2 device to register the v4l2_subdev
 * @name:	name of the sub-device. Please notice that the name must be
 *		unique.
 * @function:	media entity function defined by MEDIA_ENT_F_* macros
 * @num_pads:	number of pads to initialize
 * @pads_flag:	flags to use in each pad
 * @sd_int_ops:	pointer to &struct v4l2_subdev_internal_ops
 * @sd_ops:	pointer to &struct v4l2_subdev_ops.
 * @handler:	func pointer of the irq handler
 * @thread_fn:	func pointer of the threaded irq handler
 *
 * Helper function initialize and register the struct dcmipp_ent_device and
 * struct v4l2_subdev which represents a subdev node in the topology
 */
int dcmipp_ent_sd_register(struct dcmipp_ent_device *ved,
			   struct v4l2_subdev *sd,
			   struct v4l2_device *v4l2_dev,
			   const char *const name,
			   u32 function,
			   u16 num_pads,
			   const unsigned long *pads_flag,
			   const struct v4l2_subdev_internal_ops *sd_int_ops,
			   const struct v4l2_subdev_ops *sd_ops,
			   irq_handler_t handler,
			   irq_handler_t thread_fn);

/**
 * dcmipp_ent_sd_unregister - cleanup and unregister a subdev node
 *
 * @ved:	the dcmipp_ent_device struct to be cleaned up
 * @sd:		the v4l2_subdev struct to be unregistered
 *
 * Helper function cleanup and unregister the struct dcmipp_ent_device and
 * struct v4l2_subdev which represents a subdev node in the topology
 */
void dcmipp_ent_sd_unregister(struct dcmipp_ent_device *ved,
			      struct v4l2_subdev *sd);

#define reg_write(device, reg, val) \
	(__reg_write((device)->dev, (device)->regs, (reg), (val)))
#define reg_read(device, reg) \
	(__reg_read((device)->dev, (device)->regs, (reg)))
#define reg_set(device, reg, mask) \
	(__reg_set((device)->dev, (device)->regs, (reg), (mask)))
#define reg_clear(device, reg, mask) \
	(__reg_clear((device)->dev, (device)->regs, (reg), (mask)))

static inline u32 __reg_read(struct device *dev, void __iomem *base, u32 reg)
{
	u32 val = readl_relaxed(base + reg);

	dev_dbg(dev, "RD 0x%x %#10.8x\n", reg, val);
	return val;
}

static inline void __reg_write(struct device *dev, void __iomem *base, u32 reg,
			       u32 val)
{
	dev_dbg(dev, "WR 0x%x %#10.8x\n", reg, val);
	writel_relaxed(val, base + reg);
}

static inline void __reg_set(struct device *dev, void __iomem *base, u32 reg,
			     u32 mask)
{
	dev_dbg(dev, "SET 0x%x %#10.8x\n", reg, mask);
	__reg_write(dev, base, reg, readl_relaxed(base + reg) | mask);
}

static inline void __reg_clear(struct device *dev, void __iomem *base, u32 reg,
			       u32 mask)
{
	dev_dbg(dev, "CLR 0x%x %#10.8x\n", reg, mask);
	__reg_write(dev, base, reg, readl_relaxed(base + reg) & ~mask);
}

/* DCMIPP subdev init / release entry points */
struct dcmipp_ent_device *dcmipp_par_ent_init(struct device *dev,
					      const char *entity_name,
					      struct v4l2_device *v4l2_dev,
					      void __iomem *regs);
void dcmipp_par_ent_release(struct dcmipp_ent_device *ved);
struct dcmipp_ent_device *
dcmipp_byteproc_ent_init(struct device *dev, const char *entity_name,
			 struct v4l2_device *v4l2_dev, void __iomem *regs);
void dcmipp_byteproc_ent_release(struct dcmipp_ent_device *ved);
struct dcmipp_ent_device *dcmipp_bytecap_ent_init(struct device *dev,
						  const char *entity_name,
						  struct v4l2_device *v4l2_dev,
						  void __iomem *regs);
void dcmipp_bytecap_ent_release(struct dcmipp_ent_device *ved);

#endif
