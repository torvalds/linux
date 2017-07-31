/*
 * Driver for the VIA Chrome integrated camera controller.
 *
 * Copyright 2009,2010 Jonathan Corbet <corbet@lwn.net>
 * Distributable under the terms of the GNU General Public License, version 2
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
#include <media/v4l2-image-sizes.h>
#include <media/i2c/ov7670.h>
#include <media/videobuf-dma-sg.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/pm_qos.h>
#include <linux/via-core.h>
#include <linux/via-gpio.h>
#include <linux/via_i2c.h>
#include <asm/olpc.h>

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
	u8 __iomem *cb_addrs[3];		/* Kernel-space addresses */
	int n_cap_bufs;			/* How many are we using? */
	int next_buf;
	struct videobuf_queue vb_queue;
	struct list_head buffer_queue;	/* prot. by reg_lock */
	/*
	 * User tracking.
	 */
	int users;
	struct file *owner;
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
	dev_err(&(cam)->platdev->dev, fmt, ##arg);
#define cam_warn(cam, fmt, arg...) \
	dev_warn(&(cam)->platdev->dev, fmt, ##arg);
#define cam_dbg(cam, fmt, arg...) \
	dev_dbg(&(cam)->platdev->dev, fmt, ##arg);

/*
 * Format handling.  This is ripped almost directly from Hans's changes
 * to cafe_ccic.c.  It's a little unfortunate; until this change, we
 * didn't need to know anything about the format except its byte depth;
 * now this information must be managed at this level too.
 */
static struct via_format {
	__u8 *desc;
	__u32 pixelformat;
	int bpp;   /* Bytes per pixel */
	u32 mbus_code;
} via_formats[] = {
	{
		.desc		= "YUYV 4:2:2",
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
	if (cam->power_gpio < 0 || cam->reset_gpio < 0) {
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
 * Find the next videobuf buffer which has somebody waiting on it.
 */
static struct videobuf_buffer *viacam_next_buffer(struct via_camera *cam)
{
	unsigned long flags;
	struct videobuf_buffer *buf = NULL;

	spin_lock_irqsave(&cam->viadev->reg_lock, flags);
	if (cam->opstate != S_RUNNING)
		goto out;
	if (list_empty(&cam->buffer_queue))
		goto out;
	buf = list_entry(cam->buffer_queue.next, struct videobuf_buffer, queue);
	if (!waitqueue_active(&buf->done)) {/* Nobody waiting */
		buf = NULL;
		goto out;
	}
	list_del(&buf->queue);
	buf->state = VIDEOBUF_ACTIVE;
out:
	spin_unlock_irqrestore(&cam->viadev->reg_lock, flags);
	return buf;
}

/*
 * The threaded IRQ handler.
 */
static irqreturn_t viacam_irq(int irq, void *data)
{
	int bufn;
	struct videobuf_buffer *vb;
	struct via_camera *cam = data;
	struct videobuf_dmabuf *vdma;

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
	vdma = videobuf_to_dma(vb);
	viafb_dma_copy_out_sg(cam->cb_offsets[bufn], vdma->sglist, vdma->sglen);
	vb->state = VIDEOBUF_DONE;
	vb->size = cam->user_format.sizeimage;
	wake_up(&vb->done);
done:
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
	cam->next_buf = 0;
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
/* Videobuf callback ops */

/*
 * buffer_setup.  The purpose of this one would appear to be to tell
 * videobuf how big a single image is.	It's also evidently up to us
 * to put some sort of limit on the maximum number of buffers allowed.
 */
static int viacam_vb_buf_setup(struct videobuf_queue *q,
		unsigned int *count, unsigned int *size)
{
	struct via_camera *cam = q->priv_data;

	*size = cam->user_format.sizeimage;
	if (*count == 0 || *count > 6)	/* Arbitrary number */
		*count = 6;
	return 0;
}

/*
 * Prepare a buffer.
 */
static int viacam_vb_buf_prepare(struct videobuf_queue *q,
		struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct via_camera *cam = q->priv_data;

	vb->size = cam->user_format.sizeimage;
	vb->width = cam->user_format.width; /* bytesperline???? */
	vb->height = cam->user_format.height;
	vb->field = field;
	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		int ret = videobuf_iolock(q, vb, NULL);
		if (ret)
			return ret;
	}
	vb->state = VIDEOBUF_PREPARED;
	return 0;
}

/*
 * We've got a buffer to put data into.
 *
 * FIXME: check for a running engine and valid buffers?
 */
static void viacam_vb_buf_queue(struct videobuf_queue *q,
		struct videobuf_buffer *vb)
{
	struct via_camera *cam = q->priv_data;

	/*
	 * Note that videobuf holds the lock when it calls
	 * us, so we need not (indeed, cannot) take it here.
	 */
	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &cam->buffer_queue);
}

/*
 * Free a buffer.
 */
static void viacam_vb_buf_release(struct videobuf_queue *q,
		struct videobuf_buffer *vb)
{
	struct via_camera *cam = q->priv_data;

	videobuf_dma_unmap(&cam->platdev->dev, videobuf_to_dma(vb));
	videobuf_dma_free(videobuf_to_dma(vb));
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static const struct videobuf_queue_ops viacam_vb_ops = {
	.buf_setup	= viacam_vb_buf_setup,
	.buf_prepare	= viacam_vb_buf_prepare,
	.buf_queue	= viacam_vb_buf_queue,
	.buf_release	= viacam_vb_buf_release,
};

/* --------------------------------------------------------------------------*/
/* File operations */

static int viacam_open(struct file *filp)
{
	struct via_camera *cam = video_drvdata(filp);

	filp->private_data = cam;
	/*
	 * Note the new user.  If this is the first one, we'll also
	 * need to power up the sensor.
	 */
	mutex_lock(&cam->lock);
	if (cam->users == 0) {
		int ret = viafb_request_dma();

		if (ret) {
			mutex_unlock(&cam->lock);
			return ret;
		}
		via_sensor_power_up(cam);
		set_bit(CF_CONFIG_NEEDED, &cam->flags);
		/*
		 * Hook into videobuf.	Evidently this cannot fail.
		 */
		videobuf_queue_sg_init(&cam->vb_queue, &viacam_vb_ops,
				&cam->platdev->dev, &cam->viadev->reg_lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
				sizeof(struct videobuf_buffer), cam, NULL);
	}
	(cam->users)++;
	mutex_unlock(&cam->lock);
	return 0;
}

static int viacam_release(struct file *filp)
{
	struct via_camera *cam = video_drvdata(filp);

	mutex_lock(&cam->lock);
	(cam->users)--;
	/*
	 * If the "owner" is closing, shut down any ongoing
	 * operations.
	 */
	if (filp == cam->owner) {
		videobuf_stop(&cam->vb_queue);
		/*
		 * We don't hold the spinlock here, but, if release()
		 * is being called by the owner, nobody else will
		 * be changing the state.  And an extra stop would
		 * not hurt anyway.
		 */
		if (cam->opstate != S_IDLE)
			viacam_stop_engine(cam);
		cam->owner = NULL;
	}
	/*
	 * Last one out needs to turn out the lights.
	 */
	if (cam->users == 0) {
		videobuf_mmap_free(&cam->vb_queue);
		via_sensor_power_down(cam);
		viafb_release_dma();
	}
	mutex_unlock(&cam->lock);
	return 0;
}

/*
 * Read a frame from the device.
 */
static ssize_t viacam_read(struct file *filp, char __user *buffer,
		size_t len, loff_t *pos)
{
	struct via_camera *cam = video_drvdata(filp);
	int ret;

	mutex_lock(&cam->lock);
	/*
	 * Enforce the V4l2 "only one owner gets to read data" rule.
	 */
	if (cam->owner && cam->owner != filp) {
		ret = -EBUSY;
		goto out_unlock;
	}
	cam->owner = filp;
	/*
	 * Do we need to configure the hardware?
	 */
	if (test_bit(CF_CONFIG_NEEDED, &cam->flags)) {
		ret = viacam_configure_sensor(cam);
		if (!ret)
			ret = viacam_config_controller(cam);
		if (ret)
			goto out_unlock;
	}
	/*
	 * Fire up the capture engine, then have videobuf do
	 * the heavy lifting.  Someday it would be good to avoid
	 * stopping and restarting the engine each time.
	 */
	INIT_LIST_HEAD(&cam->buffer_queue);
	viacam_start_engine(cam);
	ret = videobuf_read_stream(&cam->vb_queue, buffer, len, pos, 0,
			filp->f_flags & O_NONBLOCK);
	viacam_stop_engine(cam);
	/* videobuf_stop() ?? */

out_unlock:
	mutex_unlock(&cam->lock);
	return ret;
}


static unsigned int viacam_poll(struct file *filp, struct poll_table_struct *pt)
{
	struct via_camera *cam = video_drvdata(filp);

	return videobuf_poll_stream(filp, &cam->vb_queue, pt);
}


static int viacam_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct via_camera *cam = video_drvdata(filp);

	return videobuf_mmap_mapper(&cam->vb_queue, vma);
}



static const struct v4l2_file_operations viacam_fops = {
	.owner		= THIS_MODULE,
	.open		= viacam_open,
	.release	= viacam_release,
	.read		= viacam_read,
	.poll		= viacam_poll,
	.mmap		= viacam_mmap,
	.unlocked_ioctl	= video_ioctl2,
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
	input->std = V4L2_STD_ALL; /* Not sure what should go here */
	strcpy(input->name, "Camera");
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

static int viacam_s_std(struct file *filp, void *priv, v4l2_std_id std)
{
	return 0;
}

static int viacam_g_std(struct file *filp, void *priv, v4l2_std_id *std)
{
	*std = V4L2_STD_NTSC_M;
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
};

static const u32 via_def_mbus_code = MEDIA_BUS_FMT_YUYV8_2X8;

static int viacam_enum_fmt_vid_cap(struct file *filp, void *priv,
		struct v4l2_fmtdesc *fmt)
{
	if (fmt->index >= N_VIA_FMTS)
		return -EINVAL;
	strlcpy(fmt->description, via_formats[fmt->index].desc,
			sizeof(fmt->description));
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
}


/*
 * The real work of figuring out a workable format.
 */
static int viacam_do_try_fmt(struct via_camera *cam,
		struct v4l2_pix_format *upix, struct v4l2_pix_format *spix)
{
	int ret;
	struct v4l2_subdev_pad_config pad_cfg;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	struct via_format *f = via_find_format(upix->pixelformat);

	upix->pixelformat = f->pixelformat;
	viacam_fmt_pre(upix, spix);
	v4l2_fill_mbus_format(&format.format, spix, f->mbus_code);
	ret = sensor_call(cam, pad, set_fmt, &pad_cfg, &format);
	v4l2_fill_pix_format(spix, &format.format);
	viacam_fmt_post(upix, spix);
	return ret;
}



static int viacam_try_fmt_vid_cap(struct file *filp, void *priv,
		struct v4l2_format *fmt)
{
	struct via_camera *cam = priv;
	struct v4l2_format sfmt;
	int ret;

	mutex_lock(&cam->lock);
	ret = viacam_do_try_fmt(cam, &fmt->fmt.pix, &sfmt.fmt.pix);
	mutex_unlock(&cam->lock);
	return ret;
}


static int viacam_g_fmt_vid_cap(struct file *filp, void *priv,
		struct v4l2_format *fmt)
{
	struct via_camera *cam = priv;

	mutex_lock(&cam->lock);
	fmt->fmt.pix = cam->user_format;
	mutex_unlock(&cam->lock);
	return 0;
}

static int viacam_s_fmt_vid_cap(struct file *filp, void *priv,
		struct v4l2_format *fmt)
{
	struct via_camera *cam = priv;
	int ret;
	struct v4l2_format sfmt;
	struct via_format *f = via_find_format(fmt->fmt.pix.pixelformat);

	/*
	 * Camera must be idle or we can't mess with the
	 * video setup.
	 */
	mutex_lock(&cam->lock);
	if (cam->opstate != S_IDLE) {
		ret = -EBUSY;
		goto out;
	}
	/*
	 * Let the sensor code look over and tweak the
	 * requested formatting.
	 */
	ret = viacam_do_try_fmt(cam, &fmt->fmt.pix, &sfmt.fmt.pix);
	if (ret)
		goto out;
	/*
	 * OK, let's commit to the new format.
	 */
	cam->user_format = fmt->fmt.pix;
	cam->sensor_format = sfmt.fmt.pix;
	cam->mbus_code = f->mbus_code;
	ret = viacam_configure_sensor(cam);
	if (!ret)
		ret = viacam_config_controller(cam);
out:
	mutex_unlock(&cam->lock);
	return ret;
}

static int viacam_querycap(struct file *filp, void *priv,
		struct v4l2_capability *cap)
{
	strcpy(cap->driver, "via-camera");
	strcpy(cap->card, "via-camera");
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

/*
 * Streaming operations - pure videobuf stuff.
 */
static int viacam_reqbufs(struct file *filp, void *priv,
		struct v4l2_requestbuffers *rb)
{
	struct via_camera *cam = priv;

	return videobuf_reqbufs(&cam->vb_queue, rb);
}

static int viacam_querybuf(struct file *filp, void *priv,
		struct v4l2_buffer *buf)
{
	struct via_camera *cam = priv;

	return videobuf_querybuf(&cam->vb_queue, buf);
}

static int viacam_qbuf(struct file *filp, void *priv, struct v4l2_buffer *buf)
{
	struct via_camera *cam = priv;

	return videobuf_qbuf(&cam->vb_queue, buf);
}

static int viacam_dqbuf(struct file *filp, void *priv, struct v4l2_buffer *buf)
{
	struct via_camera *cam = priv;

	return videobuf_dqbuf(&cam->vb_queue, buf, filp->f_flags & O_NONBLOCK);
}

static int viacam_streamon(struct file *filp, void *priv, enum v4l2_buf_type t)
{
	struct via_camera *cam = priv;
	int ret = 0;

	if (t != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	mutex_lock(&cam->lock);
	if (cam->opstate != S_IDLE) {
		ret = -EBUSY;
		goto out;
	}
	/*
	 * Enforce the V4l2 "only one owner gets to read data" rule.
	 */
	if (cam->owner && cam->owner != filp) {
		ret = -EBUSY;
		goto out;
	}
	cam->owner = filp;
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
	/*
	 * If the CPU goes into C3, the DMA transfer gets corrupted and
	 * users start filing unsightly bug reports.  Put in a "latency"
	 * requirement which will keep the CPU out of the deeper sleep
	 * states.
	 */
	pm_qos_add_request(&cam->qos_request, PM_QOS_CPU_DMA_LATENCY, 50);
	/*
	 * Fire things up.
	 */
	INIT_LIST_HEAD(&cam->buffer_queue);
	ret = videobuf_streamon(&cam->vb_queue);
	if (!ret)
		viacam_start_engine(cam);
out:
	mutex_unlock(&cam->lock);
	return ret;
}

static int viacam_streamoff(struct file *filp, void *priv, enum v4l2_buf_type t)
{
	struct via_camera *cam = priv;
	int ret;

	if (t != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	mutex_lock(&cam->lock);
	if (cam->opstate != S_RUNNING) {
		ret = -EINVAL;
		goto out;
	}
	pm_qos_remove_request(&cam->qos_request);
	viacam_stop_engine(cam);
	/*
	 * Videobuf will recycle all of the outstanding buffers, but
	 * we should be sure we don't retain any references to
	 * any of them.
	 */
	ret = videobuf_streamoff(&cam->vb_queue);
	INIT_LIST_HEAD(&cam->buffer_queue);
out:
	mutex_unlock(&cam->lock);
	return ret;
}

/* G/S_PARM */

static int viacam_g_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{
	struct via_camera *cam = priv;
	int ret;

	mutex_lock(&cam->lock);
	ret = sensor_call(cam, video, g_parm, parm);
	mutex_unlock(&cam->lock);
	parm->parm.capture.readbuffers = cam->n_cap_bufs;
	return ret;
}

static int viacam_s_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{
	struct via_camera *cam = priv;
	int ret;

	mutex_lock(&cam->lock);
	ret = sensor_call(cam, video, s_parm, parm);
	mutex_unlock(&cam->lock);
	parm->parm.capture.readbuffers = cam->n_cap_bufs;
	return ret;
}

static int viacam_enum_framesizes(struct file *filp, void *priv,
		struct v4l2_frmsizeenum *sizes)
{
	if (sizes->index != 0)
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
	struct via_camera *cam = priv;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = interval->index,
		.code = cam->mbus_code,
		.width = cam->sensor_format.width,
		.height = cam->sensor_format.height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	mutex_lock(&cam->lock);
	ret = sensor_call(cam, pad, enum_frame_interval, NULL, &fie);
	mutex_unlock(&cam->lock);
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
	.vidioc_s_std		= viacam_s_std,
	.vidioc_g_std		= viacam_g_std,
	.vidioc_enum_fmt_vid_cap = viacam_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = viacam_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= viacam_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= viacam_s_fmt_vid_cap,
	.vidioc_querycap	= viacam_querycap,
	.vidioc_reqbufs		= viacam_reqbufs,
	.vidioc_querybuf	= viacam_querybuf,
	.vidioc_qbuf		= viacam_qbuf,
	.vidioc_dqbuf		= viacam_dqbuf,
	.vidioc_streamon	= viacam_streamon,
	.vidioc_streamoff	= viacam_streamoff,
	.vidioc_g_parm		= viacam_g_parm,
	.vidioc_s_parm		= viacam_s_parm,
	.vidioc_enum_framesizes = viacam_enum_framesizes,
	.vidioc_enum_frameintervals = viacam_enum_frameintervals,
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
	if (cam->users > 0)
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

static struct video_device viacam_v4l_template = {
	.name		= "via-camera",
	.minor		= -1,
	.tvnorms	= V4L2_STD_NTSC_M,
	.fops		= &viacam_fops,
	.ioctl_ops	= &viacam_ioctl_ops,
	.release	= video_device_release_empty, /* Check this */
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
	cam->users = 0;
	cam->owner = NULL;
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
	/*
	 * Tell V4l2 that we exist.
	 */
	cam->vdev = viacam_v4l_template;
	cam->vdev.v4l2_dev = &cam->v4l2_dev;
	ret = video_register_device(&cam->vdev, VFL_TYPE_GRABBER, -1);
	if (ret)
		goto out_irq;
	video_set_drvdata(&cam->vdev, cam);

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
