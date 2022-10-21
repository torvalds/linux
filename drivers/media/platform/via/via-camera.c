// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the VIA Chrome integrated camera controller.
 *
 * Copyright 2009,2010 Jonathan Corbet <corbet@lwn.net>
 *
 * This work was supported by the One Laptop Per Child project
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-image-sizes.h>
#include <media/i2c/ov7670.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/pm_qos.h>
#include <linux/via-core.h>
#include <linux/via-gpio.h>
#include <linux/via_i2c.h>

#ifdef CONFIG_X86
#include <asm/olpc.h>
#else
#define machine_is_olpc(x) 0
#endif

#include "via-camera.h"

MODULE_ALIAS("platform:viafb-camera");
MODULE_AUTHOR("Jonathan Corbet <corbet@lwn.net>");
MODULE_DESCRIPTION("VIA framebuffer-based camera controller driver");
MODULE_LICENSE("GPL");

static bool flip_image;
module_param(flip_image, bool, 0444);
MODULE_PARM_DESC(flip_image,
		"If set, the sensor will be instructed to flip the image vertically.");

static bool override_serial;
module_param(override_serial, bool, 0444);
MODULE_PARM_DESC(override_serial,
		"The camera driver will normally refuse to load if the XO 1.5 serial port is enabled.  Set this option to force-enable the camera.");

/*
 * The structure describing our camera.
 */
enum viacam_opstate { S_IDLE = 0, S_RUNNING = 1 };

struct via_camera {
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct video_device vdev;
	struct v4l2_subdev *sensor;
	struct platform_device *platdev;
	struct viafb_dev *viadev;
	struct mutex lock;
	enum viacam_opstate opstate;
	unsigned long flags;
	struct pm_qos_request qos_request;
	/*
	 * GPIO info for power/reset management
	 */
	int power_gpio;
	int reset_gpio;
	/*
	 * I/O memory stuff.
	 */
	void __iomem *mmio;	/* Where the registers live */
	void __iomem *fbmem;	/* Frame buffer memory */
	u32 fb_offset;		/* Reserved memory offset (FB) */
	/*
	 * Capture buffers and related.	 The controller supports
	 * up to three, so that's what we have here.  These buffers
	 * live in frame buffer memory, so we don't call them "DMA".
	 */
	unsigned int cb_offsets[3];	/* offsets into fb mem */
	u8 __iomem *cb_addrs[3];	/* Kernel-space addresses */
	int n_cap_bufs;			/* How many are we using? */
	struct vb2_queue vq;
	struct list_head buffer_queue;
	u32 sequence;
	/*
	 * Video format information.  sensor_format is kept in a form
	 * that we can use to pass to the sensor.  We always run the
	 * sensor in VGA resolution, though, and let the controller
	 * downscale things if need be.	 So we keep the "real*
	 * dimensions separately.
	 */
	struct v4l2_pix_format sensor_format;
	struct v4l2_pix_format user_format;
	u32 mbus_code;
};

/* buffer for one video frame */
struct via_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer		vbuf;
	struct list_head		queue;
};

/*
 * Yes, this is a hack, but there's only going to be one of these
 * on any system we know of.
 */
static struct via_camera *via_cam_info;

/*
 * Flag values, manipulated with bitops
 */
#define CF_DMA_ACTIVE	 0	/* A frame is incoming */
#define CF_CONFIG_NEEDED 1	/* Must configure hardware */


/*
 * Nasty ugly v4l2 boilerplate.
 */
#define sensor_call(cam, optype, func, args...) \
	v4l2_subdev_call(cam->sensor, optype, func, ##args)

/*
 * Debugging and related.
 */
#define cam_err(cam, fmt, arg...) \
	dev_err(&(cam)->platdev->dev, fmt, ##arg)
#define cam_warn(cam, fmt, arg...) \
	dev_warn(&(cam)->platdev->dev, fmt, ##arg)
#define cam_dbg(cam, fmt, arg...) \
	dev_dbg(&(cam)->platdev->dev, fmt, ##arg)

/*
 * Format handling.  This is ripped almost directly from Hans's changes
 * to cafe_ccic.c.  It's a little unfortunate; until this change, we
 * didn't need to know anything about the format except its byte depth;
 * now this information must be managed at this level too.
 */
static struct via_format {
	__u32 pixelformat;
	int bpp;   /* Bytes per pixel */
	u32 mbus_code;
} via_formats[] = {
	{
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.bpp		= 2,
	},
	/* RGB444 and Bayer should be doable, but have never been
	   tested with this driver. RGB565 seems to work at the default
	   resolution, but results in color corruption when being scaled by
	   viacam_set_scaled(), and is disabled as a result. */
};
#define N_VIA_FMTS ARRAY_SIZE(via_formats)

static struct via_format *via_find_format(u32 pixelformat)
{
	unsigned i;

	for (i = 0; i < N_VIA_FMTS; i++)
		if (via_formats[i].pixelformat == pixelformat)
			return via_formats + i;
	/* Not found? Then return the first format. */
	return via_formats;
}


/*--------------------------------------------------------------------------*/
/*
 * Sensor power/reset management.  This piece is OLPC-specific for
 * sure; other configurations will have things connected differently.
 */
static int via_sensor_power_setup(struct via_camera *cam)
{
	int ret;

	cam->power_gpio = viafb_gpio_lookup("VGPIO3");
	cam->reset_gpio = viafb_gpio_lookup("VGPIO2");
	if (!gpio_is_valid(cam->power_gpio) || !gpio_is_valid(cam->reset_gpio)) {
		dev_err(&cam->platdev->dev, "Unable to find GPIO lines\n");
		return -EINVAL;
	}
	ret = gpio_request(cam->power_gpio, "viafb-camera");
	if (ret) {
		dev_err(&cam->platdev->dev, "Unable to request power GPIO\n");
		return ret;
	}
	ret = gpio_request(cam->reset_gpio, "viafb-camera");
	if (ret) {
		dev_err(&cam->platdev->dev, "Unable to request reset GPIO\n");
		gpio_free(cam->power_gpio);
		return ret;
	}
	gpio_direction_output(cam->power_gpio, 0);
	gpio_direction_output(cam->reset_gpio, 0);
	return 0;
}

/*
 * Power up the sensor and perform the reset dance.
 */
static void via_sensor_power_up(struct via_camera *cam)
{
	gpio_set_value(cam->power_gpio, 1);
	gpio_set_value(cam->reset_gpio, 0);
	msleep(20);  /* Probably excessive */
	gpio_set_value(cam->reset_gpio, 1);
	msleep(20);
}

static void via_sensor_power_down(struct via_camera *cam)
{
	gpio_set_value(cam->power_gpio, 0);
	gpio_set_value(cam->reset_gpio, 0);
}


static void via_sensor_power_release(struct via_camera *cam)
{
	via_sensor_power_down(cam);
	gpio_free(cam->power_gpio);
	gpio_free(cam->reset_gpio);
}

/* --------------------------------------------------------------------------*/
/* Sensor ops */

/*
 * Manage the ov7670 "flip" bit, which needs special help.
 */
static int viacam_set_flip(struct via_camera *cam)
{
	struct v4l2_control ctrl;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = V4L2_CID_VFLIP;
	ctrl.value = flip_image;
	return v4l2_s_ctrl(NULL, cam->sensor->ctrl_handler, &ctrl);
}

/*
 * Configure the sensor.  It's up to the caller to ensure
 * that the camera is in the correct operating state.
 */
static int viacam_configure_sensor(struct via_camera *cam)
{
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	v4l2_fill_mbus_format(&format.format, &cam->sensor_format, cam->mbus_code);
	ret = sensor_call(cam, core, init, 0);
	if (ret == 0)
		ret = sensor_call(cam, pad, set_fmt, NULL, &format);
	/*
	 * OV7670 does weird things if flip is set *before* format...
	 */
	if (ret == 0)
		ret = viacam_set_flip(cam);
	return ret;
}



/* --------------------------------------------------------------------------*/
/*
 * Some simple register accessors; they assume that the lock is held.
 *
 * Should we want to support the second capture engine, we could
 * hide the register difference by adding 0x1000 to registers in the
 * 0x300-350 range.
 */
static inline void viacam_write_reg(struct via_camera *cam,
		int reg, int value)
{
	iowrite32(value, cam->mmio + reg);
}

static inline int viacam_read_reg(struct via_camera *cam, int reg)
{
	return ioread32(cam->mmio + reg);
}

static inline void viacam_write_reg_mask(struct via_camera *cam,
		int reg, int value, int mask)
{
	int tmp = viacam_read_reg(cam, reg);

	tmp = (tmp & ~mask) | (value & mask);
	viacam_write_reg(cam, reg, tmp);
}


/* --------------------------------------------------------------------------*/
/* Interrupt management and handling */

static irqreturn_t viacam_quick_irq(int irq, void *data)
{
	struct via_camera *cam = data;
	irqreturn_t ret = IRQ_NONE;
	int icv;

	/*
	 * All we do here is to clear the interrupts and tell
	 * the handler thread to wake up.
	 */
	spin_lock(&cam->viadev->reg_lock);
	icv = viacam_read_reg(cam, VCR_INTCTRL);
	if (icv & VCR_IC_EAV) {
		icv |= VCR_IC_EAV|VCR_IC_EVBI|VCR_IC_FFULL;
		viacam_write_reg(cam, VCR_INTCTRL, icv);
		ret = IRQ_WAKE_THREAD;
	}
	spin_unlock(&cam->viadev->reg_lock);
	return ret;
}

/*
 * Find the next buffer which has somebody waiting on it.
 */
static struct via_buffer *viacam_next_buffer(struct via_camera *cam)
{
	if (cam->opstate != S_RUNNING)
		return NULL;
	if (list_empty(&cam->buffer_queue))
		return NULL;
	return list_entry(cam->buffer_queue.next, struct via_buffer, queue);
}

/*
 * The threaded IRQ handler.
 */
static irqreturn_t viacam_irq(int irq, void *data)
{
	struct via_camera *cam = data;
	struct via_buffer *vb;
	int bufn;
	struct sg_table *sgt;

	mutex_lock(&cam->lock);
	/*
	 * If there is no place to put the data frame, don't bother
	 * with anything else.
	 */
	vb = viacam_next_buffer(cam);
	if (vb == NULL)
		goto done;
	/*
	 * Figure out which buffer we just completed.
	 */
	bufn = (viacam_read_reg(cam, VCR_INTCTRL) & VCR_IC_ACTBUF) >> 3;
	bufn -= 1;
	if (bufn < 0)
		bufn = cam->n_cap_bufs - 1;
	/*
	 * Copy over the data and let any waiters know.
	 */
	sgt = vb2_dma_sg_plane_desc(&vb->vbuf.vb2_buf, 0);
	vb->vbuf.vb2_buf.timestamp = ktime_get_ns();
	viafb_dma_copy_out_sg(cam->cb_offsets[bufn], sgt->sgl, sgt->nents);
	vb->vbuf.sequence = cam->sequence++;
	vb->vbuf.field = V4L2_FIELD_NONE;
	list_del(&vb->queue);
	vb2_buffer_done(&vb->vbuf.vb2_buf, VB2_BUF_STATE_DONE);
done:
	mutex_unlock(&cam->lock);
	return IRQ_HANDLED;
}


/*
 * These functions must mess around with the general interrupt
 * control register, which is relevant to much more than just the
 * camera.  Nothing else uses interrupts, though, as of this writing.
 * Should that situation change, we'll have to improve support at
 * the via-core level.
 */
static void viacam_int_enable(struct via_camera *cam)
{
	viacam_write_reg(cam, VCR_INTCTRL,
			VCR_IC_INTEN|VCR_IC_EAV|VCR_IC_EVBI|VCR_IC_FFULL);
	viafb_irq_enable(VDE_I_C0AVEN);
}

static void viacam_int_disable(struct via_camera *cam)
{
	viafb_irq_disable(VDE_I_C0AVEN);
	viacam_write_reg(cam, VCR_INTCTRL, 0);
}



/* --------------------------------------------------------------------------*/
/* Controller operations */

/*
 * Set up our capture buffers in framebuffer memory.
 */
static int viacam_ctlr_cbufs(struct via_camera *cam)
{
	int nbuf = cam->viadev->camera_fbmem_size/cam->sensor_format.sizeimage;
	int i;
	unsigned int offset;

	/*
	 * See how many buffers we can work with.
	 */
	if (nbuf >= 3) {
		cam->n_cap_bufs = 3;
		viacam_write_reg_mask(cam, VCR_CAPINTC, VCR_CI_3BUFS,
				VCR_CI_3BUFS);
	} else if (nbuf == 2) {
		cam->n_cap_bufs = 2;
		viacam_write_reg_mask(cam, VCR_CAPINTC, 0, VCR_CI_3BUFS);
	} else {
		cam_warn(cam, "Insufficient frame buffer memory\n");
		return -ENOMEM;
	}
	/*
	 * Set them up.
	 */
	offset = cam->fb_offset;
	for (i = 0; i < cam->n_cap_bufs; i++) {
		cam->cb_offsets[i] = offset;
		cam->cb_addrs[i] = cam->fbmem + offset;
		viacam_write_reg(cam, VCR_VBUF1 + i*4, offset & VCR_VBUF_MASK);
		offset += cam->sensor_format.sizeimage;
	}
	return 0;
}

/*
 * Set the scaling register for downscaling the image.
 *
 * This register works like this...  Vertical scaling is enabled
 * by bit 26; if that bit is set, downscaling is controlled by the
 * value in bits 16:25.	 Those bits are divided by 1024 to get
 * the scaling factor; setting just bit 25 thus cuts the height
 * in half.
 *
 * Horizontal scaling works about the same, but it's enabled by
 * bit 11, with bits 0:10 giving the numerator of a fraction
 * (over 2048) for the scaling value.
 *
 * This function is naive in that, if the user departs from
 * the 3x4 VGA scaling factor, the image will distort.	We
 * could work around that if it really seemed important.
 */
static void viacam_set_scale(struct via_camera *cam)
{
	unsigned int avscale;
	int sf;

	if (cam->user_format.width == VGA_WIDTH)
		avscale = 0;
	else {
		sf = (cam->user_format.width*2048)/VGA_WIDTH;
		avscale = VCR_AVS_HEN | sf;
	}
	if (cam->user_format.height < VGA_HEIGHT) {
		sf = (1024*cam->user_format.height)/VGA_HEIGHT;
		avscale |= VCR_AVS_VEN | (sf << 16);
	}
	viacam_write_reg(cam, VCR_AVSCALE, avscale);
}


/*
 * Configure image-related information into the capture engine.
 */
static void viacam_ctlr_image(struct via_camera *cam)
{
	int cicreg;

	/*
	 * Disable clock before messing with stuff - from the via
	 * sample driver.
	 */
	viacam_write_reg(cam, VCR_CAPINTC, ~(VCR_CI_ENABLE|VCR_CI_CLKEN));
	/*
	 * Set up the controller for VGA resolution, modulo magic
	 * offsets from the via sample driver.
	 */
	viacam_write_reg(cam, VCR_HORRANGE, 0x06200120);
	viacam_write_reg(cam, VCR_VERTRANGE, 0x01de0000);
	viacam_set_scale(cam);
	/*
	 * Image size info.
	 */
	viacam_write_reg(cam, VCR_MAXDATA,
			(cam->sensor_format.height << 16) |
			(cam->sensor_format.bytesperline >> 3));
	viacam_write_reg(cam, VCR_MAXVBI, 0);
	viacam_write_reg(cam, VCR_VSTRIDE,
			cam->user_format.bytesperline & VCR_VS_STRIDE);
	/*
	 * Set up the capture interface control register,
	 * everything but the "go" bit.
	 *
	 * The FIFO threshold is a bit of a magic number; 8 is what
	 * VIA's sample code uses.
	 */
	cicreg = VCR_CI_CLKEN |
		0x08000000 |		/* FIFO threshold */
		VCR_CI_FLDINV |		/* OLPC-specific? */
		VCR_CI_VREFINV |	/* OLPC-specific? */
		VCR_CI_DIBOTH |		/* Capture both fields */
		VCR_CI_CCIR601_8;
	if (cam->n_cap_bufs == 3)
		cicreg |= VCR_CI_3BUFS;
	/*
	 * YUV formats need different byte swapping than RGB.
	 */
	if (cam->user_format.pixelformat == V4L2_PIX_FMT_YUYV)
		cicreg |= VCR_CI_YUYV;
	else
		cicreg |= VCR_CI_UYVY;
	viacam_write_reg(cam, VCR_CAPINTC, cicreg);
}


static int viacam_config_controller(struct via_camera *cam)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&cam->viadev->reg_lock, flags);
	ret = viacam_ctlr_cbufs(cam);
	if (!ret)
		viacam_ctlr_image(cam);
	spin_unlock_irqrestore(&cam->viadev->reg_lock, flags);
	clear_bit(CF_CONFIG_NEEDED, &cam->flags);
	return ret;
}

/*
 * Make it start grabbing data.
 */
static void viacam_start_engine(struct via_camera *cam)
{
	spin_lock_irq(&cam->viadev->reg_lock);
	viacam_write_reg_mask(cam, VCR_CAPINTC, VCR_CI_ENABLE, VCR_CI_ENABLE);
	viacam_int_enable(cam);
	(void) viacam_read_reg(cam, VCR_CAPINTC); /* Force post */
	cam->opstate = S_RUNNING;
	spin_unlock_irq(&cam->viadev->reg_lock);
}


static void viacam_stop_engine(struct via_camera *cam)
{
	spin_lock_irq(&cam->viadev->reg_lock);
	viacam_int_disable(cam);
	viacam_write_reg_mask(cam, VCR_CAPINTC, 0, VCR_CI_ENABLE);
	(void) viacam_read_reg(cam, VCR_CAPINTC); /* Force post */
	cam->opstate = S_IDLE;
	spin_unlock_irq(&cam->viadev->reg_lock);
}


/* --------------------------------------------------------------------------*/
/* vb2 callback ops */

static struct via_buffer *vb2_to_via_buffer(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	return container_of(vbuf, struct via_buffer, vbuf);
}

static void viacam_vb2_queue(struct vb2_buffer *vb)
{
	struct via_camera *cam = vb2_get_drv_priv(vb->vb2_queue);
	struct via_buffer *via = vb2_to_via_buffer(vb);

	list_add_tail(&via->queue, &cam->buffer_queue);
}

static int viacam_vb2_prepare(struct vb2_buffer *vb)
{
	struct via_camera *cam = vb2_get_drv_priv(vb->vb2_queue);

	if (vb2_plane_size(vb, 0) < cam->user_format.sizeimage) {
		cam_dbg(cam,
			"Plane size too small (%lu < %u)\n",
			vb2_plane_size(vb, 0),
			cam->user_format.sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, cam->user_format.sizeimage);

	return 0;
}

static int viacam_vb2_queue_setup(struct vb2_queue *vq,
				  unsigned int *nbufs,
				  unsigned int *num_planes, unsigned int sizes[],
				  struct device *alloc_devs[])
{
	struct via_camera *cam = vb2_get_drv_priv(vq);
	int size = cam->user_format.sizeimage;

	if (*num_planes)
		return sizes[0] < size ? -EINVAL : 0;

	*num_planes = 1;
	sizes[0] = size;
	return 0;
}

static int viacam_vb2_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct via_camera *cam = vb2_get_drv_priv(vq);
	struct via_buffer *buf, *tmp;
	int ret = 0;

	if (cam->opstate != S_IDLE) {
		ret = -EBUSY;
		goto out;
	}
	/*
	 * Configure things if need be.
	 */
	if (test_bit(CF_CONFIG_NEEDED, &cam->flags)) {
		ret = viacam_configure_sensor(cam);
		if (ret)
			goto out;
		ret = viacam_config_controller(cam);
		if (ret)
			goto out;
	}
	cam->sequence = 0;
	/*
	 * If the CPU goes into C3, the DMA transfer gets corrupted and
	 * users start filing unsightly bug reports.  Put in a "latency"
	 * requirement which will keep the CPU out of the deeper sleep
	 * states.
	 */
	cpu_latency_qos_add_request(&cam->qos_request, 50);
	viacam_start_engine(cam);
	return 0;
out:
	list_for_each_entry_safe(buf, tmp, &cam->buffer_queue, queue) {
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vbuf.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	return ret;
}

static void viacam_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct via_camera *cam = vb2_get_drv_priv(vq);
	struct via_buffer *buf, *tmp;

	cpu_latency_qos_remove_request(&cam->qos_request);
	viacam_stop_engine(cam);

	list_for_each_entry_safe(buf, tmp, &cam->buffer_queue, queue) {
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vbuf.vb2_buf, VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops viacam_vb2_ops = {
	.queue_setup		= viacam_vb2_queue_setup,
	.buf_queue		= viacam_vb2_queue,
	.buf_prepare		= viacam_vb2_prepare,
	.start_streaming	= viacam_vb2_start_streaming,
	.stop_streaming		= viacam_vb2_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* --------------------------------------------------------------------------*/
/* File operations */

static int viacam_open(struct file *filp)
{
	struct via_camera *cam = video_drvdata(filp);
	int ret;

	/*
	 * Note the new user.  If this is the first one, we'll also
	 * need to power up the sensor.
	 */
	mutex_lock(&cam->lock);
	ret = v4l2_fh_open(filp);
	if (ret)
		goto out;
	if (v4l2_fh_is_singular_file(filp)) {
		ret = viafb_request_dma();

		if (ret) {
			v4l2_fh_release(filp);
			goto out;
		}
		via_sensor_power_up(cam);
		set_bit(CF_CONFIG_NEEDED, &cam->flags);
	}
out:
	mutex_unlock(&cam->lock);
	return ret;
}

static int viacam_release(struct file *filp)
{
	struct via_camera *cam = video_drvdata(filp);
	bool last_open;

	mutex_lock(&cam->lock);
	last_open = v4l2_fh_is_singular_file(filp);
	_vb2_fop_release(filp, NULL);
	/*
	 * Last one out needs to turn out the lights.
	 */
	if (last_open) {
		via_sensor_power_down(cam);
		viafb_release_dma();
	}
	mutex_unlock(&cam->lock);
	return 0;
}

static const struct v4l2_file_operations viacam_fops = {
	.owner		= THIS_MODULE,
	.open		= viacam_open,
	.release	= viacam_release,
	.read		= vb2_fop_read,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

/*----------------------------------------------------------------------------*/
/*
 * The long list of v4l2 ioctl ops
 */

/*
 * Only one input.
 */
static int viacam_enum_input(struct file *filp, void *priv,
		struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "Camera", sizeof(input->name));
	return 0;
}

static int viacam_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int viacam_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

/*
 * Video format stuff.	Here is our default format until
 * user space messes with things.
 */
static const struct v4l2_pix_format viacam_def_pix_format = {
	.width		= VGA_WIDTH,
	.height		= VGA_HEIGHT,
	.pixelformat	= V4L2_PIX_FMT_YUYV,
	.field		= V4L2_FIELD_NONE,
	.bytesperline	= VGA_WIDTH * 2,
	.sizeimage	= VGA_WIDTH * VGA_HEIGHT * 2,
	.colorspace	= V4L2_COLORSPACE_SRGB,
};

static const u32 via_def_mbus_code = MEDIA_BUS_FMT_YUYV8_2X8;

static int viacam_enum_fmt_vid_cap(struct file *filp, void *priv,
		struct v4l2_fmtdesc *fmt)
{
	if (fmt->index >= N_VIA_FMTS)
		return -EINVAL;
	fmt->pixelformat = via_formats[fmt->index].pixelformat;
	return 0;
}

/*
 * Figure out proper image dimensions, but always force the
 * sensor to VGA.
 */
static void viacam_fmt_pre(struct v4l2_pix_format *userfmt,
		struct v4l2_pix_format *sensorfmt)
{
	*sensorfmt = *userfmt;
	if (userfmt->width < QCIF_WIDTH || userfmt->height < QCIF_HEIGHT) {
		userfmt->width = QCIF_WIDTH;
		userfmt->height = QCIF_HEIGHT;
	}
	if (userfmt->width > VGA_WIDTH || userfmt->height > VGA_HEIGHT) {
		userfmt->width = VGA_WIDTH;
		userfmt->height = VGA_HEIGHT;
	}
	sensorfmt->width = VGA_WIDTH;
	sensorfmt->height = VGA_HEIGHT;
}

static void viacam_fmt_post(struct v4l2_pix_format *userfmt,
		struct v4l2_pix_format *sensorfmt)
{
	struct via_format *f = via_find_format(userfmt->pixelformat);

	sensorfmt->bytesperline = sensorfmt->width * f->bpp;
	sensorfmt->sizeimage = sensorfmt->height * sensorfmt->bytesperline;
	userfmt->pixelformat = sensorfmt->pixelformat;
	userfmt->field = sensorfmt->field;
	userfmt->bytesperline = 2 * userfmt->width;
	userfmt->sizeimage = userfmt->bytesperline * userfmt->height;
	userfmt->colorspace = sensorfmt->colorspace;
	userfmt->ycbcr_enc = sensorfmt->ycbcr_enc;
	userfmt->quantization = sensorfmt->quantization;
	userfmt->xfer_func = sensorfmt->xfer_func;
}


/*
 * The real work of figuring out a workable format.
 */
static int viacam_do_try_fmt(struct via_camera *cam,
		struct v4l2_pix_format *upix, struct v4l2_pix_format *spix)
{
	int ret;
	struct v4l2_subdev_pad_config pad_cfg;
	struct v4l2_subdev_state pad_state = {
		.pads = &pad_cfg
		};
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	struct via_format *f = via_find_format(upix->pixelformat);

	upix->pixelformat = f->pixelformat;
	viacam_fmt_pre(upix, spix);
	v4l2_fill_mbus_format(&format.format, spix, f->mbus_code);
	ret = sensor_call(cam, pad, set_fmt, &pad_state, &format);
	v4l2_fill_pix_format(spix, &format.format);
	viacam_fmt_post(upix, spix);
	return ret;
}



static int viacam_try_fmt_vid_cap(struct file *filp, void *priv,
		struct v4l2_format *fmt)
{
	struct via_camera *cam = video_drvdata(filp);
	struct v4l2_format sfmt;

	return viacam_do_try_fmt(cam, &fmt->fmt.pix, &sfmt.fmt.pix);
}


static int viacam_g_fmt_vid_cap(struct file *filp, void *priv,
		struct v4l2_format *fmt)
{
	struct via_camera *cam = video_drvdata(filp);

	fmt->fmt.pix = cam->user_format;
	return 0;
}

static int viacam_s_fmt_vid_cap(struct file *filp, void *priv,
		struct v4l2_format *fmt)
{
	struct via_camera *cam = video_drvdata(filp);
	int ret;
	struct v4l2_format sfmt;
	struct via_format *f = via_find_format(fmt->fmt.pix.pixelformat);

	/*
	 * Camera must be idle or we can't mess with the
	 * video setup.
	 */
	if (cam->opstate != S_IDLE)
		return -EBUSY;
	/*
	 * Let the sensor code look over and tweak the
	 * requested formatting.
	 */
	ret = viacam_do_try_fmt(cam, &fmt->fmt.pix, &sfmt.fmt.pix);
	if (ret)
		return ret;
	/*
	 * OK, let's commit to the new format.
	 */
	cam->user_format = fmt->fmt.pix;
	cam->sensor_format = sfmt.fmt.pix;
	cam->mbus_code = f->mbus_code;
	ret = viacam_configure_sensor(cam);
	if (!ret)
		ret = viacam_config_controller(cam);
	return ret;
}

static int viacam_querycap(struct file *filp, void *priv,
		struct v4l2_capability *cap)
{
	strscpy(cap->driver, "via-camera", sizeof(cap->driver));
	strscpy(cap->card, "via-camera", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:via-camera", sizeof(cap->bus_info));
	return 0;
}

/* G/S_PARM */

static int viacam_g_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{
	struct via_camera *cam = video_drvdata(filp);

	return v4l2_g_parm_cap(video_devdata(filp), cam->sensor, parm);
}

static int viacam_s_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{
	struct via_camera *cam = video_drvdata(filp);

	return v4l2_s_parm_cap(video_devdata(filp), cam->sensor, parm);
}

static int viacam_enum_framesizes(struct file *filp, void *priv,
		struct v4l2_frmsizeenum *sizes)
{
	unsigned int i;

	if (sizes->index != 0)
		return -EINVAL;
	for (i = 0; i < N_VIA_FMTS; i++)
		if (sizes->pixel_format == via_formats[i].pixelformat)
			break;
	if (i >= N_VIA_FMTS)
		return -EINVAL;
	sizes->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	sizes->stepwise.min_width = QCIF_WIDTH;
	sizes->stepwise.min_height = QCIF_HEIGHT;
	sizes->stepwise.max_width = VGA_WIDTH;
	sizes->stepwise.max_height = VGA_HEIGHT;
	sizes->stepwise.step_width = sizes->stepwise.step_height = 1;
	return 0;
}

static int viacam_enum_frameintervals(struct file *filp, void *priv,
		struct v4l2_frmivalenum *interval)
{
	struct via_camera *cam = video_drvdata(filp);
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = interval->index,
		.code = cam->mbus_code,
		.width = cam->sensor_format.width,
		.height = cam->sensor_format.height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	unsigned int i;
	int ret;

	for (i = 0; i < N_VIA_FMTS; i++)
		if (interval->pixel_format == via_formats[i].pixelformat)
			break;
	if (i >= N_VIA_FMTS)
		return -EINVAL;
	if (interval->width < QCIF_WIDTH || interval->width > VGA_WIDTH ||
	    interval->height < QCIF_HEIGHT || interval->height > VGA_HEIGHT)
		return -EINVAL;
	ret = sensor_call(cam, pad, enum_frame_interval, NULL, &fie);
	if (ret)
		return ret;
	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	interval->discrete = fie.interval;
	return 0;
}

static const struct v4l2_ioctl_ops viacam_ioctl_ops = {
	.vidioc_enum_input	= viacam_enum_input,
	.vidioc_g_input		= viacam_g_input,
	.vidioc_s_input		= viacam_s_input,
	.vidioc_enum_fmt_vid_cap = viacam_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = viacam_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= viacam_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= viacam_s_fmt_vid_cap,
	.vidioc_querycap	= viacam_querycap,
	.vidioc_reqbufs		= vb2_ioctl_reqbufs,
	.vidioc_create_bufs	= vb2_ioctl_create_bufs,
	.vidioc_querybuf	= vb2_ioctl_querybuf,
	.vidioc_prepare_buf	= vb2_ioctl_prepare_buf,
	.vidioc_qbuf		= vb2_ioctl_qbuf,
	.vidioc_dqbuf		= vb2_ioctl_dqbuf,
	.vidioc_expbuf		= vb2_ioctl_expbuf,
	.vidioc_streamon	= vb2_ioctl_streamon,
	.vidioc_streamoff	= vb2_ioctl_streamoff,
	.vidioc_g_parm		= viacam_g_parm,
	.vidioc_s_parm		= viacam_s_parm,
	.vidioc_enum_framesizes = viacam_enum_framesizes,
	.vidioc_enum_frameintervals = viacam_enum_frameintervals,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/*----------------------------------------------------------------------------*/

/*
 * Power management.
 */
#ifdef CONFIG_PM

static int viacam_suspend(void *priv)
{
	struct via_camera *cam = priv;
	enum viacam_opstate state = cam->opstate;

	if (cam->opstate != S_IDLE) {
		viacam_stop_engine(cam);
		cam->opstate = state; /* So resume restarts */
	}

	return 0;
}

static int viacam_resume(void *priv)
{
	struct via_camera *cam = priv;
	int ret = 0;

	/*
	 * Get back to a reasonable operating state.
	 */
	via_write_reg_mask(VIASR, 0x78, 0, 0x80);
	via_write_reg_mask(VIASR, 0x1e, 0xc0, 0xc0);
	viacam_int_disable(cam);
	set_bit(CF_CONFIG_NEEDED, &cam->flags);
	/*
	 * Make sure the sensor's power state is correct
	 */
	if (!list_empty(&cam->vdev.fh_list))
		via_sensor_power_up(cam);
	else
		via_sensor_power_down(cam);
	/*
	 * If it was operating, try to restart it.
	 */
	if (cam->opstate != S_IDLE) {
		mutex_lock(&cam->lock);
		ret = viacam_configure_sensor(cam);
		if (!ret)
			ret = viacam_config_controller(cam);
		mutex_unlock(&cam->lock);
		if (!ret)
			viacam_start_engine(cam);
	}

	return ret;
}

static struct viafb_pm_hooks viacam_pm_hooks = {
	.suspend = viacam_suspend,
	.resume = viacam_resume
};

#endif /* CONFIG_PM */

/*
 * Setup stuff.
 */

static const struct video_device viacam_v4l_template = {
	.name		= "via-camera",
	.minor		= -1,
	.fops		= &viacam_fops,
	.ioctl_ops	= &viacam_ioctl_ops,
	.release	= video_device_release_empty, /* Check this */
	.device_caps	= V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
			  V4L2_CAP_STREAMING,
};

/*
 * The OLPC folks put the serial port on the same pin as
 * the camera.	They also get grumpy if we break the
 * serial port and keep them from using it.  So we have
 * to check the serial enable bit and not step on it.
 */
#define VIACAM_SERIAL_DEVFN 0x88
#define VIACAM_SERIAL_CREG 0x46
#define VIACAM_SERIAL_BIT 0x40

static bool viacam_serial_is_enabled(void)
{
	struct pci_bus *pbus = pci_find_bus(0, 0);
	u8 cbyte;

	if (!pbus)
		return false;
	pci_bus_read_config_byte(pbus, VIACAM_SERIAL_DEVFN,
			VIACAM_SERIAL_CREG, &cbyte);
	if ((cbyte & VIACAM_SERIAL_BIT) == 0)
		return false; /* Not enabled */
	if (!override_serial) {
		printk(KERN_NOTICE "Via camera: serial port is enabled, " \
				"refusing to load.\n");
		printk(KERN_NOTICE "Specify override_serial=1 to force " \
				"module loading.\n");
		return true;
	}
	printk(KERN_NOTICE "Via camera: overriding serial port\n");
	pci_bus_write_config_byte(pbus, VIACAM_SERIAL_DEVFN,
			VIACAM_SERIAL_CREG, cbyte & ~VIACAM_SERIAL_BIT);
	return false;
}

static struct ov7670_config sensor_cfg = {
	/* The XO-1.5 (only known user) clocks the camera at 90MHz. */
	.clock_speed = 90,
};

static int viacam_probe(struct platform_device *pdev)
{
	int ret;
	struct i2c_adapter *sensor_adapter;
	struct viafb_dev *viadev = pdev->dev.platform_data;
	struct vb2_queue *vq;
	struct i2c_board_info ov7670_info = {
		.type = "ov7670",
		.addr = 0x42 >> 1,
		.platform_data = &sensor_cfg,
	};

	/*
	 * Note that there are actually two capture channels on
	 * the device.	We only deal with one for now.	That
	 * is encoded here; nothing else assumes it's dealing with
	 * a unique capture device.
	 */
	struct via_camera *cam;

	/*
	 * Ensure that frame buffer memory has been set aside for
	 * this purpose.  As an arbitrary limit, refuse to work
	 * with less than two frames of VGA 16-bit data.
	 *
	 * If we ever support the second port, we'll need to set
	 * aside more memory.
	 */
	if (viadev->camera_fbmem_size < (VGA_HEIGHT*VGA_WIDTH*4)) {
		printk(KERN_ERR "viacam: insufficient FB memory reserved\n");
		return -ENOMEM;
	}
	if (viadev->engine_mmio == NULL) {
		printk(KERN_ERR "viacam: No I/O memory, so no pictures\n");
		return -ENOMEM;
	}

	if (machine_is_olpc() && viacam_serial_is_enabled())
		return -EBUSY;

	/*
	 * Basic structure initialization.
	 */
	cam = kzalloc (sizeof(struct via_camera), GFP_KERNEL);
	if (cam == NULL)
		return -ENOMEM;
	via_cam_info = cam;
	cam->platdev = pdev;
	cam->viadev = viadev;
	cam->opstate = S_IDLE;
	cam->user_format = cam->sensor_format = viacam_def_pix_format;
	mutex_init(&cam->lock);
	INIT_LIST_HEAD(&cam->buffer_queue);
	cam->mmio = viadev->engine_mmio;
	cam->fbmem = viadev->fbmem;
	cam->fb_offset = viadev->camera_fbmem_offset;
	cam->flags = 1 << CF_CONFIG_NEEDED;
	cam->mbus_code = via_def_mbus_code;
	/*
	 * Tell V4L that we exist.
	 */
	ret = v4l2_device_register(&pdev->dev, &cam->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to register v4l2 device\n");
		goto out_free;
	}
	ret = v4l2_ctrl_handler_init(&cam->ctrl_handler, 10);
	if (ret)
		goto out_unregister;
	cam->v4l2_dev.ctrl_handler = &cam->ctrl_handler;
	/*
	 * Convince the system that we can do DMA.
	 */
	pdev->dev.dma_mask = &viadev->pdev->dma_mask;
	dma_set_mask(&pdev->dev, 0xffffffff);
	/*
	 * Fire up the capture port.  The write to 0x78 looks purely
	 * OLPCish; any system will need to tweak 0x1e.
	 */
	via_write_reg_mask(VIASR, 0x78, 0, 0x80);
	via_write_reg_mask(VIASR, 0x1e, 0xc0, 0xc0);
	/*
	 * Get the sensor powered up.
	 */
	ret = via_sensor_power_setup(cam);
	if (ret)
		goto out_ctrl_hdl_free;
	via_sensor_power_up(cam);

	/*
	 * See if we can't find it on the bus.	The VIA_PORT_31 assumption
	 * is OLPC-specific.  0x42 assumption is ov7670-specific.
	 */
	sensor_adapter = viafb_find_i2c_adapter(VIA_PORT_31);
	cam->sensor = v4l2_i2c_new_subdev_board(&cam->v4l2_dev, sensor_adapter,
			&ov7670_info, NULL);
	if (cam->sensor == NULL) {
		dev_err(&pdev->dev, "Unable to find the sensor!\n");
		ret = -ENODEV;
		goto out_power_down;
	}
	/*
	 * Get the IRQ.
	 */
	viacam_int_disable(cam);
	ret = request_threaded_irq(viadev->pdev->irq, viacam_quick_irq,
			viacam_irq, IRQF_SHARED, "via-camera", cam);
	if (ret)
		goto out_power_down;

	vq = &cam->vq;
	vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	vq->drv_priv = cam;
	vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vq->buf_struct_size = sizeof(struct via_buffer);
	vq->dev = cam->v4l2_dev.dev;

	vq->ops = &viacam_vb2_ops;
	vq->mem_ops = &vb2_dma_sg_memops;
	vq->lock = &cam->lock;

	ret = vb2_queue_init(vq);
	/*
	 * Tell V4l2 that we exist.
	 */
	cam->vdev = viacam_v4l_template;
	cam->vdev.v4l2_dev = &cam->v4l2_dev;
	cam->vdev.lock = &cam->lock;
	cam->vdev.queue = vq;
	video_set_drvdata(&cam->vdev, cam);
	ret = video_register_device(&cam->vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto out_irq;

#ifdef CONFIG_PM
	/*
	 * Hook into PM events
	 */
	viacam_pm_hooks.private = cam;
	viafb_pm_register(&viacam_pm_hooks);
#endif

	/* Power the sensor down until somebody opens the device */
	via_sensor_power_down(cam);
	return 0;

out_irq:
	free_irq(viadev->pdev->irq, cam);
out_power_down:
	via_sensor_power_release(cam);
out_ctrl_hdl_free:
	v4l2_ctrl_handler_free(&cam->ctrl_handler);
out_unregister:
	v4l2_device_unregister(&cam->v4l2_dev);
out_free:
	kfree(cam);
	return ret;
}

static int viacam_remove(struct platform_device *pdev)
{
	struct via_camera *cam = via_cam_info;
	struct viafb_dev *viadev = pdev->dev.platform_data;

	video_unregister_device(&cam->vdev);
	v4l2_device_unregister(&cam->v4l2_dev);
#ifdef CONFIG_PM
	viafb_pm_unregister(&viacam_pm_hooks);
#endif
	free_irq(viadev->pdev->irq, cam);
	via_sensor_power_release(cam);
	v4l2_ctrl_handler_free(&cam->ctrl_handler);
	kfree(cam);
	via_cam_info = NULL;
	return 0;
}

static struct platform_driver viacam_driver = {
	.driver = {
		.name = "viafb-camera",
	},
	.probe = viacam_probe,
	.remove = viacam_remove,
};

module_platform_driver(viacam_driver);
