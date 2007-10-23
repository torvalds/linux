/*
 * A driver for the CMOS camera controller in the Marvell 88ALP01 "cafe"
 * multifunction chip.  Currently works with the Omnivision OV7670
 * sensor.
 *
 * The data sheet for this device can be found at:
 *    http://www.marvell.com/products/pcconn/88ALP01.jsp
 *
 * Copyright 2006 One Laptop Per Child Association, Inc.
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * Written by Jonathan Corbet, corbet@lwn.net.
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/jiffies.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include "cafe_ccic-regs.h"

#define CAFE_VERSION 0x000002


/*
 * Parameters.
 */
MODULE_AUTHOR("Jonathan Corbet <corbet@lwn.net>");
MODULE_DESCRIPTION("Marvell 88ALP01 CMOS Camera Controller driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("Video");

/*
 * Internal DMA buffer management.  Since the controller cannot do S/G I/O,
 * we must have physically contiguous buffers to bring frames into.
 * These parameters control how many buffers we use, whether we
 * allocate them at load time (better chance of success, but nails down
 * memory) or when somebody tries to use the camera (riskier), and,
 * for load-time allocation, how big they should be.
 *
 * The controller can cycle through three buffers.  We could use
 * more by flipping pointers around, but it probably makes little
 * sense.
 */

#define MAX_DMA_BUFS 3
static int alloc_bufs_at_read = 0;
module_param(alloc_bufs_at_read, bool, 0444);
MODULE_PARM_DESC(alloc_bufs_at_read,
		"Non-zero value causes DMA buffers to be allocated when the "
		"video capture device is read, rather than at module load "
		"time.  This saves memory, but decreases the chances of "
		"successfully getting those buffers.");

static int n_dma_bufs = 3;
module_param(n_dma_bufs, uint, 0644);
MODULE_PARM_DESC(n_dma_bufs,
		"The number of DMA buffers to allocate.  Can be either two "
		"(saves memory, makes timing tighter) or three.");

static int dma_buf_size = VGA_WIDTH * VGA_HEIGHT * 2;  /* Worst case */
module_param(dma_buf_size, uint, 0444);
MODULE_PARM_DESC(dma_buf_size,
		"The size of the allocated DMA buffers.  If actual operating "
		"parameters require larger buffers, an attempt to reallocate "
		"will be made.");

static int min_buffers = 1;
module_param(min_buffers, uint, 0644);
MODULE_PARM_DESC(min_buffers,
		"The minimum number of streaming I/O buffers we are willing "
		"to work with.");

static int max_buffers = 10;
module_param(max_buffers, uint, 0644);
MODULE_PARM_DESC(max_buffers,
		"The maximum number of streaming I/O buffers an application "
		"will be allowed to allocate.  These buffers are big and live "
		"in vmalloc space.");

static int flip = 0;
module_param(flip, bool, 0444);
MODULE_PARM_DESC(flip,
		"If set, the sensor will be instructed to flip the image "
		"vertically.");


enum cafe_state {
	S_NOTREADY,	/* Not yet initialized */
	S_IDLE,		/* Just hanging around */
	S_FLAKED,	/* Some sort of problem */
	S_SINGLEREAD,	/* In read() */
	S_SPECREAD,   	/* Speculative read (for future read()) */
	S_STREAMING	/* Streaming data */
};

/*
 * Tracking of streaming I/O buffers.
 */
struct cafe_sio_buffer {
	struct list_head list;
	struct v4l2_buffer v4lbuf;
	char *buffer;   /* Where it lives in kernel space */
	int mapcount;
	struct cafe_camera *cam;
};

/*
 * A description of one of our devices.
 * Locking: controlled by s_mutex.  Certain fields, however, require
 * 	    the dev_lock spinlock; they are marked as such by comments.
 *	    dev_lock is also required for access to device registers.
 */
struct cafe_camera
{
	enum cafe_state state;
	unsigned long flags;   		/* Buffer status, mainly (dev_lock) */
	int users;			/* How many open FDs */
	struct file *owner;		/* Who has data access (v4l2) */

	/*
	 * Subsystem structures.
	 */
	struct pci_dev *pdev;
	struct video_device v4ldev;
	struct i2c_adapter i2c_adapter;
	struct i2c_client *sensor;

	unsigned char __iomem *regs;
	struct list_head dev_list;	/* link to other devices */

	/* DMA buffers */
	unsigned int nbufs;		/* How many are alloc'd */
	int next_buf;			/* Next to consume (dev_lock) */
	unsigned int dma_buf_size;  	/* allocated size */
	void *dma_bufs[MAX_DMA_BUFS];	/* Internal buffer addresses */
	dma_addr_t dma_handles[MAX_DMA_BUFS]; /* Buffer bus addresses */
	unsigned int specframes;	/* Unconsumed spec frames (dev_lock) */
	unsigned int sequence;		/* Frame sequence number */
	unsigned int buf_seq[MAX_DMA_BUFS]; /* Sequence for individual buffers */

	/* Streaming buffers */
	unsigned int n_sbufs;		/* How many we have */
	struct cafe_sio_buffer *sb_bufs; /* The array of housekeeping structs */
	struct list_head sb_avail;	/* Available for data (we own) (dev_lock) */
	struct list_head sb_full;	/* With data (user space owns) (dev_lock) */
	struct tasklet_struct s_tasklet;

	/* Current operating parameters */
	u32 sensor_type;		/* Currently ov7670 only */
	struct v4l2_pix_format pix_format;

	/* Locks */
	struct mutex s_mutex; /* Access to this structure */
	spinlock_t dev_lock;  /* Access to device */

	/* Misc */
	wait_queue_head_t smbus_wait;	/* Waiting on i2c events */
	wait_queue_head_t iowait;	/* Waiting on frame data */
#ifdef CONFIG_VIDEO_ADV_DEBUG
	struct dentry *dfs_regs;
	struct dentry *dfs_cam_regs;
#endif
};

/*
 * Status flags.  Always manipulated with bit operations.
 */
#define CF_BUF0_VALID	 0	/* Buffers valid - first three */
#define CF_BUF1_VALID	 1
#define CF_BUF2_VALID	 2
#define CF_DMA_ACTIVE	 3	/* A frame is incoming */
#define CF_CONFIG_NEEDED 4	/* Must configure hardware */



/*
 * Start over with DMA buffers - dev_lock needed.
 */
static void cafe_reset_buffers(struct cafe_camera *cam)
{
	int i;

	cam->next_buf = -1;
	for (i = 0; i < cam->nbufs; i++)
		clear_bit(i, &cam->flags);
	cam->specframes = 0;
}

static inline int cafe_needs_config(struct cafe_camera *cam)
{
	return test_bit(CF_CONFIG_NEEDED, &cam->flags);
}

static void cafe_set_config_needed(struct cafe_camera *cam, int needed)
{
	if (needed)
		set_bit(CF_CONFIG_NEEDED, &cam->flags);
	else
		clear_bit(CF_CONFIG_NEEDED, &cam->flags);
}




/*
 * Debugging and related.
 */
#define cam_err(cam, fmt, arg...) \
	dev_err(&(cam)->pdev->dev, fmt, ##arg);
#define cam_warn(cam, fmt, arg...) \
	dev_warn(&(cam)->pdev->dev, fmt, ##arg);
#define cam_dbg(cam, fmt, arg...) \
	dev_dbg(&(cam)->pdev->dev, fmt, ##arg);


/* ---------------------------------------------------------------------*/
/*
 * We keep a simple list of known devices to search at open time.
 */
static LIST_HEAD(cafe_dev_list);
static DEFINE_MUTEX(cafe_dev_list_lock);

static void cafe_add_dev(struct cafe_camera *cam)
{
	mutex_lock(&cafe_dev_list_lock);
	list_add_tail(&cam->dev_list, &cafe_dev_list);
	mutex_unlock(&cafe_dev_list_lock);
}

static void cafe_remove_dev(struct cafe_camera *cam)
{
	mutex_lock(&cafe_dev_list_lock);
	list_del(&cam->dev_list);
	mutex_unlock(&cafe_dev_list_lock);
}

static struct cafe_camera *cafe_find_dev(int minor)
{
	struct cafe_camera *cam;

	mutex_lock(&cafe_dev_list_lock);
	list_for_each_entry(cam, &cafe_dev_list, dev_list) {
		if (cam->v4ldev.minor == minor)
			goto done;
	}
	cam = NULL;
  done:
	mutex_unlock(&cafe_dev_list_lock);
	return cam;
}


static struct cafe_camera *cafe_find_by_pdev(struct pci_dev *pdev)
{
	struct cafe_camera *cam;

	mutex_lock(&cafe_dev_list_lock);
	list_for_each_entry(cam, &cafe_dev_list, dev_list) {
		if (cam->pdev == pdev)
			goto done;
	}
	cam = NULL;
  done:
	mutex_unlock(&cafe_dev_list_lock);
	return cam;
}


/* ------------------------------------------------------------------------ */
/*
 * Device register I/O
 */
static inline void cafe_reg_write(struct cafe_camera *cam, unsigned int reg,
		unsigned int val)
{
	iowrite32(val, cam->regs + reg);
}

static inline unsigned int cafe_reg_read(struct cafe_camera *cam,
		unsigned int reg)
{
	return ioread32(cam->regs + reg);
}


static inline void cafe_reg_write_mask(struct cafe_camera *cam, unsigned int reg,
		unsigned int val, unsigned int mask)
{
	unsigned int v = cafe_reg_read(cam, reg);

	v = (v & ~mask) | (val & mask);
	cafe_reg_write(cam, reg, v);
}

static inline void cafe_reg_clear_bit(struct cafe_camera *cam,
		unsigned int reg, unsigned int val)
{
	cafe_reg_write_mask(cam, reg, 0, val);
}

static inline void cafe_reg_set_bit(struct cafe_camera *cam,
		unsigned int reg, unsigned int val)
{
	cafe_reg_write_mask(cam, reg, val, val);
}



/* -------------------------------------------------------------------- */
/*
 * The I2C/SMBUS interface to the camera itself starts here.  The
 * controller handles SMBUS itself, presenting a relatively simple register
 * interface; all we have to do is to tell it where to route the data.
 */
#define CAFE_SMBUS_TIMEOUT (HZ)  /* generous */

static int cafe_smbus_write_done(struct cafe_camera *cam)
{
	unsigned long flags;
	int c1;

	/*
	 * We must delay after the interrupt, or the controller gets confused
	 * and never does give us good status.  Fortunately, we don't do this
	 * often.
	 */
	udelay(20);
	spin_lock_irqsave(&cam->dev_lock, flags);
	c1 = cafe_reg_read(cam, REG_TWSIC1);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
	return (c1 & (TWSIC1_WSTAT|TWSIC1_ERROR)) != TWSIC1_WSTAT;
}

static int cafe_smbus_write_data(struct cafe_camera *cam,
		u16 addr, u8 command, u8 value)
{
	unsigned int rval;
	unsigned long flags;
	DEFINE_WAIT(the_wait);

	spin_lock_irqsave(&cam->dev_lock, flags);
	rval = TWSIC0_EN | ((addr << TWSIC0_SID_SHIFT) & TWSIC0_SID);
	rval |= TWSIC0_OVMAGIC;  /* Make OV sensors work */
	/*
	 * Marvell sez set clkdiv to all 1's for now.
	 */
	rval |= TWSIC0_CLKDIV;
	cafe_reg_write(cam, REG_TWSIC0, rval);
	(void) cafe_reg_read(cam, REG_TWSIC1); /* force write */
	rval = value | ((command << TWSIC1_ADDR_SHIFT) & TWSIC1_ADDR);
	cafe_reg_write(cam, REG_TWSIC1, rval);
	spin_unlock_irqrestore(&cam->dev_lock, flags);

	/*
	 * Time to wait for the write to complete.  THIS IS A RACY
	 * WAY TO DO IT, but the sad fact is that reading the TWSIC1
	 * register too quickly after starting the operation sends
	 * the device into a place that may be kinder and better, but
	 * which is absolutely useless for controlling the sensor.  In
	 * practice we have plenty of time to get into our sleep state
	 * before the interrupt hits, and the worst case is that we
	 * time out and then see that things completed, so this seems
	 * the best way for now.
	 */
	do {
		prepare_to_wait(&cam->smbus_wait, &the_wait,
				TASK_UNINTERRUPTIBLE);
		schedule_timeout(1); /* even 1 jiffy is too long */
		finish_wait(&cam->smbus_wait, &the_wait);
	} while (!cafe_smbus_write_done(cam));

#ifdef IF_THE_CAFE_HARDWARE_WORKED_RIGHT
	wait_event_timeout(cam->smbus_wait, cafe_smbus_write_done(cam),
			CAFE_SMBUS_TIMEOUT);
#endif
	spin_lock_irqsave(&cam->dev_lock, flags);
	rval = cafe_reg_read(cam, REG_TWSIC1);
	spin_unlock_irqrestore(&cam->dev_lock, flags);

	if (rval & TWSIC1_WSTAT) {
		cam_err(cam, "SMBUS write (%02x/%02x/%02x) timed out\n", addr,
				command, value);
		return -EIO;
	}
	if (rval & TWSIC1_ERROR) {
		cam_err(cam, "SMBUS write (%02x/%02x/%02x) error\n", addr,
				command, value);
		return -EIO;
	}
	return 0;
}



static int cafe_smbus_read_done(struct cafe_camera *cam)
{
	unsigned long flags;
	int c1;

	/*
	 * We must delay after the interrupt, or the controller gets confused
	 * and never does give us good status.  Fortunately, we don't do this
	 * often.
	 */
	udelay(20);
	spin_lock_irqsave(&cam->dev_lock, flags);
	c1 = cafe_reg_read(cam, REG_TWSIC1);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
	return c1 & (TWSIC1_RVALID|TWSIC1_ERROR);
}



static int cafe_smbus_read_data(struct cafe_camera *cam,
		u16 addr, u8 command, u8 *value)
{
	unsigned int rval;
	unsigned long flags;

	spin_lock_irqsave(&cam->dev_lock, flags);
	rval = TWSIC0_EN | ((addr << TWSIC0_SID_SHIFT) & TWSIC0_SID);
	rval |= TWSIC0_OVMAGIC; /* Make OV sensors work */
	/*
	 * Marvel sez set clkdiv to all 1's for now.
	 */
	rval |= TWSIC0_CLKDIV;
	cafe_reg_write(cam, REG_TWSIC0, rval);
	(void) cafe_reg_read(cam, REG_TWSIC1); /* force write */
	rval = TWSIC1_READ | ((command << TWSIC1_ADDR_SHIFT) & TWSIC1_ADDR);
	cafe_reg_write(cam, REG_TWSIC1, rval);
	spin_unlock_irqrestore(&cam->dev_lock, flags);

	wait_event_timeout(cam->smbus_wait,
			cafe_smbus_read_done(cam), CAFE_SMBUS_TIMEOUT);
	spin_lock_irqsave(&cam->dev_lock, flags);
	rval = cafe_reg_read(cam, REG_TWSIC1);
	spin_unlock_irqrestore(&cam->dev_lock, flags);

	if (rval & TWSIC1_ERROR) {
		cam_err(cam, "SMBUS read (%02x/%02x) error\n", addr, command);
		return -EIO;
	}
	if (! (rval & TWSIC1_RVALID)) {
		cam_err(cam, "SMBUS read (%02x/%02x) timed out\n", addr,
				command);
		return -EIO;
	}
	*value = rval & 0xff;
	return 0;
}

/*
 * Perform a transfer over SMBUS.  This thing is called under
 * the i2c bus lock, so we shouldn't race with ourselves...
 */
static int cafe_smbus_xfer(struct i2c_adapter *adapter, u16 addr,
		unsigned short flags, char rw, u8 command,
		int size, union i2c_smbus_data *data)
{
	struct cafe_camera *cam = i2c_get_adapdata(adapter);
	int ret = -EINVAL;

	/*
	 * Refuse to talk to anything but OV cam chips.  We should
	 * never even see an attempt to do so, but one never knows.
	 */
	if (cam->sensor && addr != cam->sensor->addr) {
		cam_err(cam, "funky smbus addr %d\n", addr);
		return -EINVAL;
	}
	/*
	 * This interface would appear to only do byte data ops.  OK
	 * it can do word too, but the cam chip has no use for that.
	 */
	if (size != I2C_SMBUS_BYTE_DATA) {
		cam_err(cam, "funky xfer size %d\n", size);
		return -EINVAL;
	}

	if (rw == I2C_SMBUS_WRITE)
		ret = cafe_smbus_write_data(cam, addr, command, data->byte);
	else if (rw == I2C_SMBUS_READ)
		ret = cafe_smbus_read_data(cam, addr, command, &data->byte);
	return ret;
}


static void cafe_smbus_enable_irq(struct cafe_camera *cam)
{
	unsigned long flags;

	spin_lock_irqsave(&cam->dev_lock, flags);
	cafe_reg_set_bit(cam, REG_IRQMASK, TWSIIRQS);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
}

static u32 cafe_smbus_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_READ_BYTE_DATA  |
	       I2C_FUNC_SMBUS_WRITE_BYTE_DATA;
}

static struct i2c_algorithm cafe_smbus_algo = {
	.smbus_xfer = cafe_smbus_xfer,
	.functionality = cafe_smbus_func
};

/* Somebody is on the bus */
static int cafe_cam_init(struct cafe_camera *cam);
static void cafe_ctlr_stop_dma(struct cafe_camera *cam);
static void cafe_ctlr_power_down(struct cafe_camera *cam);

static int cafe_smbus_attach(struct i2c_client *client)
{
	struct cafe_camera *cam = i2c_get_adapdata(client->adapter);

	/*
	 * Don't talk to chips we don't recognize.
	 */
	if (client->driver->id == I2C_DRIVERID_OV7670) {
		cam->sensor = client;
		return cafe_cam_init(cam);
	}
	return -EINVAL;
}

static int cafe_smbus_detach(struct i2c_client *client)
{
	struct cafe_camera *cam = i2c_get_adapdata(client->adapter);

	if (cam->sensor == client) {
		cafe_ctlr_stop_dma(cam);
		cafe_ctlr_power_down(cam);
		cam_err(cam, "lost the sensor!\n");
		cam->sensor = NULL;  /* Bummer, no camera */
		cam->state = S_NOTREADY;
	}
	return 0;
}

static int cafe_smbus_setup(struct cafe_camera *cam)
{
	struct i2c_adapter *adap = &cam->i2c_adapter;
	int ret;

	cafe_smbus_enable_irq(cam);
	adap->id = I2C_HW_SMBUS_CAFE;
	adap->class = I2C_CLASS_CAM_DIGITAL;
	adap->owner = THIS_MODULE;
	adap->client_register = cafe_smbus_attach;
	adap->client_unregister = cafe_smbus_detach;
	adap->algo = &cafe_smbus_algo;
	strcpy(adap->name, "cafe_ccic");
	adap->dev.parent = &cam->pdev->dev;
	i2c_set_adapdata(adap, cam);
	ret = i2c_add_adapter(adap);
	if (ret)
		printk(KERN_ERR "Unable to register cafe i2c adapter\n");
	return ret;
}

static void cafe_smbus_shutdown(struct cafe_camera *cam)
{
	i2c_del_adapter(&cam->i2c_adapter);
}


/* ------------------------------------------------------------------- */
/*
 * Deal with the controller.
 */

/*
 * Do everything we think we need to have the interface operating
 * according to the desired format.
 */
static void cafe_ctlr_dma(struct cafe_camera *cam)
{
	/*
	 * Store the first two Y buffers (we aren't supporting
	 * planar formats for now, so no UV bufs).  Then either
	 * set the third if it exists, or tell the controller
	 * to just use two.
	 */
	cafe_reg_write(cam, REG_Y0BAR, cam->dma_handles[0]);
	cafe_reg_write(cam, REG_Y1BAR, cam->dma_handles[1]);
	if (cam->nbufs > 2) {
		cafe_reg_write(cam, REG_Y2BAR, cam->dma_handles[2]);
		cafe_reg_clear_bit(cam, REG_CTRL1, C1_TWOBUFS);
	}
	else
		cafe_reg_set_bit(cam, REG_CTRL1, C1_TWOBUFS);
	cafe_reg_write(cam, REG_UBAR, 0); /* 32 bits only for now */
}

static void cafe_ctlr_image(struct cafe_camera *cam)
{
	int imgsz;
	struct v4l2_pix_format *fmt = &cam->pix_format;

	imgsz = ((fmt->height << IMGSZ_V_SHIFT) & IMGSZ_V_MASK) |
		(fmt->bytesperline & IMGSZ_H_MASK);
	cafe_reg_write(cam, REG_IMGSIZE, imgsz);
	cafe_reg_write(cam, REG_IMGOFFSET, 0);
	/* YPITCH just drops the last two bits */
	cafe_reg_write_mask(cam, REG_IMGPITCH, fmt->bytesperline,
			IMGP_YP_MASK);
	/*
	 * Tell the controller about the image format we are using.
	 */
	switch (cam->pix_format.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	    cafe_reg_write_mask(cam, REG_CTRL0,
			    C0_DF_YUV|C0_YUV_PACKED|C0_YUVE_YUYV,
			    C0_DF_MASK);
	    break;

	case V4L2_PIX_FMT_RGB444:
	    cafe_reg_write_mask(cam, REG_CTRL0,
			    C0_DF_RGB|C0_RGBF_444|C0_RGB4_XRGB,
			    C0_DF_MASK);
		/* Alpha value? */
	    break;

	case V4L2_PIX_FMT_RGB565:
	    cafe_reg_write_mask(cam, REG_CTRL0,
			    C0_DF_RGB|C0_RGBF_565|C0_RGB5_BGGR,
			    C0_DF_MASK);
	    break;

	default:
	    cam_err(cam, "Unknown format %x\n", cam->pix_format.pixelformat);
	    break;
	}
	/*
	 * Make sure it knows we want to use hsync/vsync.
	 */
	cafe_reg_write_mask(cam, REG_CTRL0, C0_SIF_HVSYNC,
			C0_SIFM_MASK);
}


/*
 * Configure the controller for operation; caller holds the
 * device mutex.
 */
static int cafe_ctlr_configure(struct cafe_camera *cam)
{
	unsigned long flags;

	spin_lock_irqsave(&cam->dev_lock, flags);
	cafe_ctlr_dma(cam);
	cafe_ctlr_image(cam);
	cafe_set_config_needed(cam, 0);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
	return 0;
}

static void cafe_ctlr_irq_enable(struct cafe_camera *cam)
{
	/*
	 * Clear any pending interrupts, since we do not
	 * expect to have I/O active prior to enabling.
	 */
	cafe_reg_write(cam, REG_IRQSTAT, FRAMEIRQS);
	cafe_reg_set_bit(cam, REG_IRQMASK, FRAMEIRQS);
}

static void cafe_ctlr_irq_disable(struct cafe_camera *cam)
{
	cafe_reg_clear_bit(cam, REG_IRQMASK, FRAMEIRQS);
}

/*
 * Make the controller start grabbing images.  Everything must
 * be set up before doing this.
 */
static void cafe_ctlr_start(struct cafe_camera *cam)
{
	/* set_bit performs a read, so no other barrier should be
	   needed here */
	cafe_reg_set_bit(cam, REG_CTRL0, C0_ENABLE);
}

static void cafe_ctlr_stop(struct cafe_camera *cam)
{
	cafe_reg_clear_bit(cam, REG_CTRL0, C0_ENABLE);
}

static void cafe_ctlr_init(struct cafe_camera *cam)
{
	unsigned long flags;

	spin_lock_irqsave(&cam->dev_lock, flags);
	/*
	 * Added magic to bring up the hardware on the B-Test board
	 */
	cafe_reg_write(cam, 0x3038, 0x8);
	cafe_reg_write(cam, 0x315c, 0x80008);
	/*
	 * Go through the dance needed to wake the device up.
	 * Note that these registers are global and shared
	 * with the NAND and SD devices.  Interaction between the
	 * three still needs to be examined.
	 */
	cafe_reg_write(cam, REG_GL_CSR, GCSR_SRS|GCSR_MRS); /* Needed? */
	cafe_reg_write(cam, REG_GL_CSR, GCSR_SRC|GCSR_MRC);
	cafe_reg_write(cam, REG_GL_CSR, GCSR_SRC|GCSR_MRS);
	/*
	 * Here we must wait a bit for the controller to come around.
	 */
	spin_unlock_irqrestore(&cam->dev_lock, flags);
	msleep(5);
	spin_lock_irqsave(&cam->dev_lock, flags);

	cafe_reg_write(cam, REG_GL_CSR, GCSR_CCIC_EN|GCSR_SRC|GCSR_MRC);
	cafe_reg_set_bit(cam, REG_GL_IMASK, GIMSK_CCIC_EN);
	/*
	 * Make sure it's not powered down.
	 */
	cafe_reg_clear_bit(cam, REG_CTRL1, C1_PWRDWN);
	/*
	 * Turn off the enable bit.  It sure should be off anyway,
	 * but it's good to be sure.
	 */
	cafe_reg_clear_bit(cam, REG_CTRL0, C0_ENABLE);
	/*
	 * Mask all interrupts.
	 */
	cafe_reg_write(cam, REG_IRQMASK, 0);
	/*
	 * Clock the sensor appropriately.  Controller clock should
	 * be 48MHz, sensor "typical" value is half that.
	 */
	cafe_reg_write_mask(cam, REG_CLKCTRL, 2, CLK_DIV_MASK);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
}


/*
 * Stop the controller, and don't return until we're really sure that no
 * further DMA is going on.
 */
static void cafe_ctlr_stop_dma(struct cafe_camera *cam)
{
	unsigned long flags;

	/*
	 * Theory: stop the camera controller (whether it is operating
	 * or not).  Delay briefly just in case we race with the SOF
	 * interrupt, then wait until no DMA is active.
	 */
	spin_lock_irqsave(&cam->dev_lock, flags);
	cafe_ctlr_stop(cam);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
	mdelay(1);
	wait_event_timeout(cam->iowait,
			!test_bit(CF_DMA_ACTIVE, &cam->flags), HZ);
	if (test_bit(CF_DMA_ACTIVE, &cam->flags))
		cam_err(cam, "Timeout waiting for DMA to end\n");
		/* This would be bad news - what now? */
	spin_lock_irqsave(&cam->dev_lock, flags);
	cam->state = S_IDLE;
	cafe_ctlr_irq_disable(cam);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
}

/*
 * Power up and down.
 */
static void cafe_ctlr_power_up(struct cafe_camera *cam)
{
	unsigned long flags;

	spin_lock_irqsave(&cam->dev_lock, flags);
	cafe_reg_clear_bit(cam, REG_CTRL1, C1_PWRDWN);
	/*
	 * Part one of the sensor dance: turn the global
	 * GPIO signal on.
	 */
	cafe_reg_write(cam, REG_GL_FCR, GFCR_GPIO_ON);
	cafe_reg_write(cam, REG_GL_GPIOR, GGPIO_OUT|GGPIO_VAL);
	/*
	 * Put the sensor into operational mode (assumes OLPC-style
	 * wiring).  Control 0 is reset - set to 1 to operate.
	 * Control 1 is power down, set to 0 to operate.
	 */
	cafe_reg_write(cam, REG_GPR, GPR_C1EN|GPR_C0EN); /* pwr up, reset */
//	mdelay(1); /* Marvell says 1ms will do it */
	cafe_reg_write(cam, REG_GPR, GPR_C1EN|GPR_C0EN|GPR_C0);
//	mdelay(1); /* Enough? */
	spin_unlock_irqrestore(&cam->dev_lock, flags);
	msleep(5); /* Just to be sure */
}

static void cafe_ctlr_power_down(struct cafe_camera *cam)
{
	unsigned long flags;

	spin_lock_irqsave(&cam->dev_lock, flags);
	cafe_reg_write(cam, REG_GPR, GPR_C1EN|GPR_C0EN|GPR_C1);
	cafe_reg_write(cam, REG_GL_FCR, GFCR_GPIO_ON);
	cafe_reg_write(cam, REG_GL_GPIOR, GGPIO_OUT);
	cafe_reg_set_bit(cam, REG_CTRL1, C1_PWRDWN);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
}

/* -------------------------------------------------------------------- */
/*
 * Communications with the sensor.
 */

static int __cafe_cam_cmd(struct cafe_camera *cam, int cmd, void *arg)
{
	struct i2c_client *sc = cam->sensor;
	int ret;

	if (sc == NULL || sc->driver == NULL || sc->driver->command == NULL)
		return -EINVAL;
	ret = sc->driver->command(sc, cmd, arg);
	if (ret == -EPERM) /* Unsupported command */
		return 0;
	return ret;
}

static int __cafe_cam_reset(struct cafe_camera *cam)
{
	int zero = 0;
	return __cafe_cam_cmd(cam, VIDIOC_INT_RESET, &zero);
}

/*
 * We have found the sensor on the i2c.  Let's try to have a
 * conversation.
 */
static int cafe_cam_init(struct cafe_camera *cam)
{
	struct v4l2_chip_ident chip = { V4L2_CHIP_MATCH_I2C_ADDR, 0, 0, 0 };
	int ret;

	mutex_lock(&cam->s_mutex);
	if (cam->state != S_NOTREADY)
		cam_warn(cam, "Cam init with device in funky state %d",
				cam->state);
	ret = __cafe_cam_reset(cam);
	if (ret)
		goto out;
	chip.match_chip = cam->sensor->addr;
	ret = __cafe_cam_cmd(cam, VIDIOC_G_CHIP_IDENT, &chip);
	if (ret)
		goto out;
	cam->sensor_type = chip.ident;
//	if (cam->sensor->addr != OV7xx0_SID) {
	if (cam->sensor_type != V4L2_IDENT_OV7670) {
		cam_err(cam, "Unsupported sensor type %d", cam->sensor->addr);
		ret = -EINVAL;
		goto out;
	}
/* Get/set parameters? */
	ret = 0;
	cam->state = S_IDLE;
  out:
	cafe_ctlr_power_down(cam);
	mutex_unlock(&cam->s_mutex);
	return ret;
}

/*
 * Configure the sensor to match the parameters we have.  Caller should
 * hold s_mutex
 */
static int cafe_cam_set_flip(struct cafe_camera *cam)
{
	struct v4l2_control ctrl;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = V4L2_CID_VFLIP;
	ctrl.value = flip;
	return __cafe_cam_cmd(cam, VIDIOC_S_CTRL, &ctrl);
}


static int cafe_cam_configure(struct cafe_camera *cam)
{
	struct v4l2_format fmt;
	int ret, zero = 0;

	if (cam->state != S_IDLE)
		return -EINVAL;
	fmt.fmt.pix = cam->pix_format;
	ret = __cafe_cam_cmd(cam, VIDIOC_INT_INIT, &zero);
	if (ret == 0)
		ret = __cafe_cam_cmd(cam, VIDIOC_S_FMT, &fmt);
	/*
	 * OV7670 does weird things if flip is set *before* format...
	 */
	ret += cafe_cam_set_flip(cam);
	return ret;
}

/* -------------------------------------------------------------------- */
/*
 * DMA buffer management.  These functions need s_mutex held.
 */

/* FIXME: this is inefficient as hell, since dma_alloc_coherent just
 * does a get_free_pages() call, and we waste a good chunk of an orderN
 * allocation.  Should try to allocate the whole set in one chunk.
 */
static int cafe_alloc_dma_bufs(struct cafe_camera *cam, int loadtime)
{
	int i;

	cafe_set_config_needed(cam, 1);
	if (loadtime)
		cam->dma_buf_size = dma_buf_size;
	else
		cam->dma_buf_size = cam->pix_format.sizeimage;
	if (n_dma_bufs > 3)
		n_dma_bufs = 3;

	cam->nbufs = 0;
	for (i = 0; i < n_dma_bufs; i++) {
		cam->dma_bufs[i] = dma_alloc_coherent(&cam->pdev->dev,
				cam->dma_buf_size, cam->dma_handles + i,
				GFP_KERNEL);
		if (cam->dma_bufs[i] == NULL) {
			cam_warn(cam, "Failed to allocate DMA buffer\n");
			break;
		}
		/* For debug, remove eventually */
		memset(cam->dma_bufs[i], 0xcc, cam->dma_buf_size);
		(cam->nbufs)++;
	}

	switch (cam->nbufs) {
	case 1:
	    dma_free_coherent(&cam->pdev->dev, cam->dma_buf_size,
			    cam->dma_bufs[0], cam->dma_handles[0]);
	    cam->nbufs = 0;
	case 0:
	    cam_err(cam, "Insufficient DMA buffers, cannot operate\n");
	    return -ENOMEM;

	case 2:
	    if (n_dma_bufs > 2)
		    cam_warn(cam, "Will limp along with only 2 buffers\n");
	    break;
	}
	return 0;
}

static void cafe_free_dma_bufs(struct cafe_camera *cam)
{
	int i;

	for (i = 0; i < cam->nbufs; i++) {
		dma_free_coherent(&cam->pdev->dev, cam->dma_buf_size,
				cam->dma_bufs[i], cam->dma_handles[i]);
		cam->dma_bufs[i] = NULL;
	}
	cam->nbufs = 0;
}





/* ----------------------------------------------------------------------- */
/*
 * Here starts the V4L2 interface code.
 */

/*
 * Read an image from the device.
 */
static ssize_t cafe_deliver_buffer(struct cafe_camera *cam,
		char __user *buffer, size_t len, loff_t *pos)
{
	int bufno;
	unsigned long flags;

	spin_lock_irqsave(&cam->dev_lock, flags);
	if (cam->next_buf < 0) {
		cam_err(cam, "deliver_buffer: No next buffer\n");
		spin_unlock_irqrestore(&cam->dev_lock, flags);
		return -EIO;
	}
	bufno = cam->next_buf;
	clear_bit(bufno, &cam->flags);
	if (++(cam->next_buf) >= cam->nbufs)
		cam->next_buf = 0;
	if (! test_bit(cam->next_buf, &cam->flags))
		cam->next_buf = -1;
	cam->specframes = 0;
	spin_unlock_irqrestore(&cam->dev_lock, flags);

	if (len > cam->pix_format.sizeimage)
		len = cam->pix_format.sizeimage;
	if (copy_to_user(buffer, cam->dma_bufs[bufno], len))
		return -EFAULT;
	(*pos) += len;
	return len;
}

/*
 * Get everything ready, and start grabbing frames.
 */
static int cafe_read_setup(struct cafe_camera *cam, enum cafe_state state)
{
	int ret;
	unsigned long flags;

	/*
	 * Configuration.  If we still don't have DMA buffers,
	 * make one last, desperate attempt.
	 */
	if (cam->nbufs == 0)
		if (cafe_alloc_dma_bufs(cam, 0))
			return -ENOMEM;

	if (cafe_needs_config(cam)) {
		cafe_cam_configure(cam);
		ret = cafe_ctlr_configure(cam);
		if (ret)
			return ret;
	}

	/*
	 * Turn it loose.
	 */
	spin_lock_irqsave(&cam->dev_lock, flags);
	cafe_reset_buffers(cam);
	cafe_ctlr_irq_enable(cam);
	cam->state = state;
	cafe_ctlr_start(cam);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
	return 0;
}


static ssize_t cafe_v4l_read(struct file *filp,
		char __user *buffer, size_t len, loff_t *pos)
{
	struct cafe_camera *cam = filp->private_data;
	int ret = 0;

	/*
	 * Perhaps we're in speculative read mode and already
	 * have data?
	 */
	mutex_lock(&cam->s_mutex);
	if (cam->state == S_SPECREAD) {
		if (cam->next_buf >= 0) {
			ret = cafe_deliver_buffer(cam, buffer, len, pos);
			if (ret != 0)
				goto out_unlock;
		}
	} else if (cam->state == S_FLAKED || cam->state == S_NOTREADY) {
		ret = -EIO;
		goto out_unlock;
	} else if (cam->state != S_IDLE) {
		ret = -EBUSY;
		goto out_unlock;
	}

	/*
	 * v4l2: multiple processes can open the device, but only
	 * one gets to grab data from it.
	 */
	if (cam->owner && cam->owner != filp) {
		ret = -EBUSY;
		goto out_unlock;
	}
	cam->owner = filp;

	/*
	 * Do setup if need be.
	 */
	if (cam->state != S_SPECREAD) {
		ret = cafe_read_setup(cam, S_SINGLEREAD);
		if (ret)
			goto out_unlock;
	}
	/*
	 * Wait for something to happen.  This should probably
	 * be interruptible (FIXME).
	 */
	wait_event_timeout(cam->iowait, cam->next_buf >= 0, HZ);
	if (cam->next_buf < 0) {
		cam_err(cam, "read() operation timed out\n");
		cafe_ctlr_stop_dma(cam);
		ret = -EIO;
		goto out_unlock;
	}
	/*
	 * Give them their data and we should be done.
	 */
	ret = cafe_deliver_buffer(cam, buffer, len, pos);

  out_unlock:
	mutex_unlock(&cam->s_mutex);
	return ret;
}








/*
 * Streaming I/O support.
 */



static int cafe_vidioc_streamon(struct file *filp, void *priv,
		enum v4l2_buf_type type)
{
	struct cafe_camera *cam = filp->private_data;
	int ret = -EINVAL;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		goto out;
	mutex_lock(&cam->s_mutex);
	if (cam->state != S_IDLE || cam->n_sbufs == 0)
		goto out_unlock;

	cam->sequence = 0;
	ret = cafe_read_setup(cam, S_STREAMING);

  out_unlock:
	mutex_unlock(&cam->s_mutex);
  out:
	return ret;
}


static int cafe_vidioc_streamoff(struct file *filp, void *priv,
		enum v4l2_buf_type type)
{
	struct cafe_camera *cam = filp->private_data;
	int ret = -EINVAL;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		goto out;
	mutex_lock(&cam->s_mutex);
	if (cam->state != S_STREAMING)
		goto out_unlock;

	cafe_ctlr_stop_dma(cam);
	ret = 0;

  out_unlock:
	mutex_unlock(&cam->s_mutex);
  out:
	return ret;
}



static int cafe_setup_siobuf(struct cafe_camera *cam, int index)
{
	struct cafe_sio_buffer *buf = cam->sb_bufs + index;

	INIT_LIST_HEAD(&buf->list);
	buf->v4lbuf.length = PAGE_ALIGN(cam->pix_format.sizeimage);
	buf->buffer = vmalloc_user(buf->v4lbuf.length);
	if (buf->buffer == NULL)
		return -ENOMEM;
	buf->mapcount = 0;
	buf->cam = cam;

	buf->v4lbuf.index = index;
	buf->v4lbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->v4lbuf.field = V4L2_FIELD_NONE;
	buf->v4lbuf.memory = V4L2_MEMORY_MMAP;
	/*
	 * Offset: must be 32-bit even on a 64-bit system.  videobuf-dma-sg
	 * just uses the length times the index, but the spec warns
	 * against doing just that - vma merging problems.  So we
	 * leave a gap between each pair of buffers.
	 */
	buf->v4lbuf.m.offset = 2*index*buf->v4lbuf.length;
	return 0;
}

static int cafe_free_sio_buffers(struct cafe_camera *cam)
{
	int i;

	/*
	 * If any buffers are mapped, we cannot free them at all.
	 */
	for (i = 0; i < cam->n_sbufs; i++)
		if (cam->sb_bufs[i].mapcount > 0)
			return -EBUSY;
	/*
	 * OK, let's do it.
	 */
	for (i = 0; i < cam->n_sbufs; i++)
		vfree(cam->sb_bufs[i].buffer);
	cam->n_sbufs = 0;
	kfree(cam->sb_bufs);
	cam->sb_bufs = NULL;
	INIT_LIST_HEAD(&cam->sb_avail);
	INIT_LIST_HEAD(&cam->sb_full);
	return 0;
}



static int cafe_vidioc_reqbufs(struct file *filp, void *priv,
		struct v4l2_requestbuffers *req)
{
	struct cafe_camera *cam = filp->private_data;
	int ret = 0;  /* Silence warning */

	/*
	 * Make sure it's something we can do.  User pointers could be
	 * implemented without great pain, but that's not been done yet.
	 */
	if (req->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (req->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;
	/*
	 * If they ask for zero buffers, they really want us to stop streaming
	 * (if it's happening) and free everything.  Should we check owner?
	 */
	mutex_lock(&cam->s_mutex);
	if (req->count == 0) {
		if (cam->state == S_STREAMING)
			cafe_ctlr_stop_dma(cam);
		ret = cafe_free_sio_buffers (cam);
		goto out;
	}
	/*
	 * Device needs to be idle and working.  We *could* try to do the
	 * right thing in S_SPECREAD by shutting things down, but it
	 * probably doesn't matter.
	 */
	if (cam->state != S_IDLE || (cam->owner && cam->owner != filp)) {
		ret = -EBUSY;
		goto out;
	}
	cam->owner = filp;

	if (req->count < min_buffers)
		req->count = min_buffers;
	else if (req->count > max_buffers)
		req->count = max_buffers;
	if (cam->n_sbufs > 0) {
		ret = cafe_free_sio_buffers(cam);
		if (ret)
			goto out;
	}

	cam->sb_bufs = kzalloc(req->count*sizeof(struct cafe_sio_buffer),
			GFP_KERNEL);
	if (cam->sb_bufs == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	for (cam->n_sbufs = 0; cam->n_sbufs < req->count; (cam->n_sbufs++)) {
		ret = cafe_setup_siobuf(cam, cam->n_sbufs);
		if (ret)
			break;
	}

	if (cam->n_sbufs == 0)  /* no luck at all - ret already set */
		kfree(cam->sb_bufs);
	req->count = cam->n_sbufs;  /* In case of partial success */

  out:
	mutex_unlock(&cam->s_mutex);
	return ret;
}


static int cafe_vidioc_querybuf(struct file *filp, void *priv,
		struct v4l2_buffer *buf)
{
	struct cafe_camera *cam = filp->private_data;
	int ret = -EINVAL;

	mutex_lock(&cam->s_mutex);
	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		goto out;
	if (buf->index < 0 || buf->index >= cam->n_sbufs)
		goto out;
	*buf = cam->sb_bufs[buf->index].v4lbuf;
	ret = 0;
  out:
	mutex_unlock(&cam->s_mutex);
	return ret;
}

static int cafe_vidioc_qbuf(struct file *filp, void *priv,
		struct v4l2_buffer *buf)
{
	struct cafe_camera *cam = filp->private_data;
	struct cafe_sio_buffer *sbuf;
	int ret = -EINVAL;
	unsigned long flags;

	mutex_lock(&cam->s_mutex);
	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		goto out;
	if (buf->index < 0 || buf->index >= cam->n_sbufs)
		goto out;
	sbuf = cam->sb_bufs + buf->index;
	if (sbuf->v4lbuf.flags & V4L2_BUF_FLAG_QUEUED) {
		ret = 0; /* Already queued?? */
		goto out;
	}
	if (sbuf->v4lbuf.flags & V4L2_BUF_FLAG_DONE) {
		/* Spec doesn't say anything, seems appropriate tho */
		ret = -EBUSY;
		goto out;
	}
	sbuf->v4lbuf.flags |= V4L2_BUF_FLAG_QUEUED;
	spin_lock_irqsave(&cam->dev_lock, flags);
	list_add(&sbuf->list, &cam->sb_avail);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
	ret = 0;
  out:
	mutex_unlock(&cam->s_mutex);
	return ret;
}

static int cafe_vidioc_dqbuf(struct file *filp, void *priv,
		struct v4l2_buffer *buf)
{
	struct cafe_camera *cam = filp->private_data;
	struct cafe_sio_buffer *sbuf;
	int ret = -EINVAL;
	unsigned long flags;

	mutex_lock(&cam->s_mutex);
	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		goto out_unlock;
	if (cam->state != S_STREAMING)
		goto out_unlock;
	if (list_empty(&cam->sb_full) && filp->f_flags & O_NONBLOCK) {
		ret = -EAGAIN;
		goto out_unlock;
	}

	while (list_empty(&cam->sb_full) && cam->state == S_STREAMING) {
		mutex_unlock(&cam->s_mutex);
		if (wait_event_interruptible(cam->iowait,
						!list_empty(&cam->sb_full))) {
			ret = -ERESTARTSYS;
			goto out;
		}
		mutex_lock(&cam->s_mutex);
	}

	if (cam->state != S_STREAMING)
		ret = -EINTR;
	else {
		spin_lock_irqsave(&cam->dev_lock, flags);
		/* Should probably recheck !list_empty() here */
		sbuf = list_entry(cam->sb_full.next,
				struct cafe_sio_buffer, list);
		list_del_init(&sbuf->list);
		spin_unlock_irqrestore(&cam->dev_lock, flags);
		sbuf->v4lbuf.flags &= ~V4L2_BUF_FLAG_DONE;
		*buf = sbuf->v4lbuf;
		ret = 0;
	}

  out_unlock:
	mutex_unlock(&cam->s_mutex);
  out:
	return ret;
}



static void cafe_v4l_vm_open(struct vm_area_struct *vma)
{
	struct cafe_sio_buffer *sbuf = vma->vm_private_data;
	/*
	 * Locking: done under mmap_sem, so we don't need to
	 * go back to the camera lock here.
	 */
	sbuf->mapcount++;
}


static void cafe_v4l_vm_close(struct vm_area_struct *vma)
{
	struct cafe_sio_buffer *sbuf = vma->vm_private_data;

	mutex_lock(&sbuf->cam->s_mutex);
	sbuf->mapcount--;
	/* Docs say we should stop I/O too... */
	if (sbuf->mapcount == 0)
		sbuf->v4lbuf.flags &= ~V4L2_BUF_FLAG_MAPPED;
	mutex_unlock(&sbuf->cam->s_mutex);
}

static struct vm_operations_struct cafe_v4l_vm_ops = {
	.open = cafe_v4l_vm_open,
	.close = cafe_v4l_vm_close
};


static int cafe_v4l_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct cafe_camera *cam = filp->private_data;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	int ret = -EINVAL;
	int i;
	struct cafe_sio_buffer *sbuf = NULL;

	if (! (vma->vm_flags & VM_WRITE) || ! (vma->vm_flags & VM_SHARED))
		return -EINVAL;
	/*
	 * Find the buffer they are looking for.
	 */
	mutex_lock(&cam->s_mutex);
	for (i = 0; i < cam->n_sbufs; i++)
		if (cam->sb_bufs[i].v4lbuf.m.offset == offset) {
			sbuf = cam->sb_bufs + i;
			break;
		}
	if (sbuf == NULL)
		goto out;

	ret = remap_vmalloc_range(vma, sbuf->buffer, 0);
	if (ret)
		goto out;
	vma->vm_flags |= VM_DONTEXPAND;
	vma->vm_private_data = sbuf;
	vma->vm_ops = &cafe_v4l_vm_ops;
	sbuf->v4lbuf.flags |= V4L2_BUF_FLAG_MAPPED;
	cafe_v4l_vm_open(vma);
	ret = 0;
  out:
	mutex_unlock(&cam->s_mutex);
	return ret;
}



static int cafe_v4l_open(struct inode *inode, struct file *filp)
{
	struct cafe_camera *cam;

	cam = cafe_find_dev(iminor(inode));
	if (cam == NULL)
		return -ENODEV;
	filp->private_data = cam;

	mutex_lock(&cam->s_mutex);
	if (cam->users == 0) {
		cafe_ctlr_power_up(cam);
		__cafe_cam_reset(cam);
		cafe_set_config_needed(cam, 1);
	/* FIXME make sure this is complete */
	}
	(cam->users)++;
	mutex_unlock(&cam->s_mutex);
	return 0;
}


static int cafe_v4l_release(struct inode *inode, struct file *filp)
{
	struct cafe_camera *cam = filp->private_data;

	mutex_lock(&cam->s_mutex);
	(cam->users)--;
	if (filp == cam->owner) {
		cafe_ctlr_stop_dma(cam);
		cafe_free_sio_buffers(cam);
		cam->owner = NULL;
	}
	if (cam->users == 0) {
		cafe_ctlr_power_down(cam);
		if (alloc_bufs_at_read)
			cafe_free_dma_bufs(cam);
	}
	mutex_unlock(&cam->s_mutex);
	return 0;
}



static unsigned int cafe_v4l_poll(struct file *filp,
		struct poll_table_struct *pt)
{
	struct cafe_camera *cam = filp->private_data;

	poll_wait(filp, &cam->iowait, pt);
	if (cam->next_buf >= 0)
		return POLLIN | POLLRDNORM;
	return 0;
}



static int cafe_vidioc_queryctrl(struct file *filp, void *priv,
		struct v4l2_queryctrl *qc)
{
	struct cafe_camera *cam = filp->private_data;
	int ret;

	mutex_lock(&cam->s_mutex);
	ret = __cafe_cam_cmd(cam, VIDIOC_QUERYCTRL, qc);
	mutex_unlock(&cam->s_mutex);
	return ret;
}


static int cafe_vidioc_g_ctrl(struct file *filp, void *priv,
		struct v4l2_control *ctrl)
{
	struct cafe_camera *cam = filp->private_data;
	int ret;

	mutex_lock(&cam->s_mutex);
	ret = __cafe_cam_cmd(cam, VIDIOC_G_CTRL, ctrl);
	mutex_unlock(&cam->s_mutex);
	return ret;
}


static int cafe_vidioc_s_ctrl(struct file *filp, void *priv,
		struct v4l2_control *ctrl)
{
	struct cafe_camera *cam = filp->private_data;
	int ret;

	mutex_lock(&cam->s_mutex);
	ret = __cafe_cam_cmd(cam, VIDIOC_S_CTRL, ctrl);
	mutex_unlock(&cam->s_mutex);
	return ret;
}





static int cafe_vidioc_querycap(struct file *file, void *priv,
		struct v4l2_capability *cap)
{
	strcpy(cap->driver, "cafe_ccic");
	strcpy(cap->card, "cafe_ccic");
	cap->version = CAFE_VERSION;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	return 0;
}


/*
 * The default format we use until somebody says otherwise.
 */
static struct v4l2_pix_format cafe_def_pix_format = {
	.width		= VGA_WIDTH,
	.height		= VGA_HEIGHT,
	.pixelformat	= V4L2_PIX_FMT_YUYV,
	.field		= V4L2_FIELD_NONE,
	.bytesperline	= VGA_WIDTH*2,
	.sizeimage	= VGA_WIDTH*VGA_HEIGHT*2,
};

static int cafe_vidioc_enum_fmt_cap(struct file *filp,
		void *priv, struct v4l2_fmtdesc *fmt)
{
	struct cafe_camera *cam = priv;
	int ret;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	mutex_lock(&cam->s_mutex);
	ret = __cafe_cam_cmd(cam, VIDIOC_ENUM_FMT, fmt);
	mutex_unlock(&cam->s_mutex);
	return ret;
}


static int cafe_vidioc_try_fmt_cap (struct file *filp, void *priv,
		struct v4l2_format *fmt)
{
	struct cafe_camera *cam = priv;
	int ret;

	mutex_lock(&cam->s_mutex);
	ret = __cafe_cam_cmd(cam, VIDIOC_TRY_FMT, fmt);
	mutex_unlock(&cam->s_mutex);
	return ret;
}

static int cafe_vidioc_s_fmt_cap(struct file *filp, void *priv,
		struct v4l2_format *fmt)
{
	struct cafe_camera *cam = priv;
	int ret;

	/*
	 * Can't do anything if the device is not idle
	 * Also can't if there are streaming buffers in place.
	 */
	if (cam->state != S_IDLE || cam->n_sbufs > 0)
		return -EBUSY;
	/*
	 * See if the formatting works in principle.
	 */
	ret = cafe_vidioc_try_fmt_cap(filp, priv, fmt);
	if (ret)
		return ret;
	/*
	 * Now we start to change things for real, so let's do it
	 * under lock.
	 */
	mutex_lock(&cam->s_mutex);
	cam->pix_format = fmt->fmt.pix;
	/*
	 * Make sure we have appropriate DMA buffers.
	 */
	ret = -ENOMEM;
	if (cam->nbufs > 0 && cam->dma_buf_size < cam->pix_format.sizeimage)
		cafe_free_dma_bufs(cam);
	if (cam->nbufs == 0) {
		if (cafe_alloc_dma_bufs(cam, 0))
			goto out;
	}
	/*
	 * It looks like this might work, so let's program the sensor.
	 */
	ret = cafe_cam_configure(cam);
	if (! ret)
		ret = cafe_ctlr_configure(cam);
  out:
	mutex_unlock(&cam->s_mutex);
	return ret;
}

/*
 * Return our stored notion of how the camera is/should be configured.
 * The V4l2 spec wants us to be smarter, and actually get this from
 * the camera (and not mess with it at open time).  Someday.
 */
static int cafe_vidioc_g_fmt_cap(struct file *filp, void *priv,
		struct v4l2_format *f)
{
	struct cafe_camera *cam = priv;

	f->fmt.pix = cam->pix_format;
	return 0;
}

/*
 * We only have one input - the sensor - so minimize the nonsense here.
 */
static int cafe_vidioc_enum_input(struct file *filp, void *priv,
		struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	input->std = V4L2_STD_ALL; /* Not sure what should go here */
	strcpy(input->name, "Camera");
	return 0;
}

static int cafe_vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int cafe_vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

/* from vivi.c */
static int cafe_vidioc_s_std(struct file *filp, void *priv, v4l2_std_id *a)
{
	return 0;
}

/*
 * G/S_PARM.  Most of this is done by the sensor, but we are
 * the level which controls the number of read buffers.
 */
static int cafe_vidioc_g_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parms)
{
	struct cafe_camera *cam = priv;
	int ret;

	mutex_lock(&cam->s_mutex);
	ret = __cafe_cam_cmd(cam, VIDIOC_G_PARM, parms);
	mutex_unlock(&cam->s_mutex);
	parms->parm.capture.readbuffers = n_dma_bufs;
	return ret;
}

static int cafe_vidioc_s_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parms)
{
	struct cafe_camera *cam = priv;
	int ret;

	mutex_lock(&cam->s_mutex);
	ret = __cafe_cam_cmd(cam, VIDIOC_S_PARM, parms);
	mutex_unlock(&cam->s_mutex);
	parms->parm.capture.readbuffers = n_dma_bufs;
	return ret;
}


static void cafe_v4l_dev_release(struct video_device *vd)
{
	struct cafe_camera *cam = container_of(vd, struct cafe_camera, v4ldev);

	kfree(cam);
}


/*
 * This template device holds all of those v4l2 methods; we
 * clone it for specific real devices.
 */

static const struct file_operations cafe_v4l_fops = {
	.owner = THIS_MODULE,
	.open = cafe_v4l_open,
	.release = cafe_v4l_release,
	.read = cafe_v4l_read,
	.poll = cafe_v4l_poll,
	.mmap = cafe_v4l_mmap,
	.ioctl = video_ioctl2,
	.llseek = no_llseek,
};

static struct video_device cafe_v4l_template = {
	.name = "cafe",
	.type = VFL_TYPE_GRABBER,
	.type2 = VID_TYPE_CAPTURE,
	.minor = -1, /* Get one dynamically */
	.tvnorms = V4L2_STD_NTSC_M,
	.current_norm = V4L2_STD_NTSC_M,  /* make mplayer happy */

	.fops = &cafe_v4l_fops,
	.release = cafe_v4l_dev_release,

	.vidioc_querycap 	= cafe_vidioc_querycap,
	.vidioc_enum_fmt_cap	= cafe_vidioc_enum_fmt_cap,
	.vidioc_try_fmt_cap	= cafe_vidioc_try_fmt_cap,
	.vidioc_s_fmt_cap	= cafe_vidioc_s_fmt_cap,
	.vidioc_g_fmt_cap	= cafe_vidioc_g_fmt_cap,
	.vidioc_enum_input	= cafe_vidioc_enum_input,
	.vidioc_g_input		= cafe_vidioc_g_input,
	.vidioc_s_input		= cafe_vidioc_s_input,
	.vidioc_s_std		= cafe_vidioc_s_std,
	.vidioc_reqbufs		= cafe_vidioc_reqbufs,
	.vidioc_querybuf	= cafe_vidioc_querybuf,
	.vidioc_qbuf		= cafe_vidioc_qbuf,
	.vidioc_dqbuf		= cafe_vidioc_dqbuf,
	.vidioc_streamon	= cafe_vidioc_streamon,
	.vidioc_streamoff	= cafe_vidioc_streamoff,
	.vidioc_queryctrl	= cafe_vidioc_queryctrl,
	.vidioc_g_ctrl		= cafe_vidioc_g_ctrl,
	.vidioc_s_ctrl		= cafe_vidioc_s_ctrl,
	.vidioc_g_parm		= cafe_vidioc_g_parm,
	.vidioc_s_parm		= cafe_vidioc_s_parm,
};







/* ---------------------------------------------------------------------- */
/*
 * Interrupt handler stuff
 */



static void cafe_frame_tasklet(unsigned long data)
{
	struct cafe_camera *cam = (struct cafe_camera *) data;
	int i;
	unsigned long flags;
	struct cafe_sio_buffer *sbuf;

	spin_lock_irqsave(&cam->dev_lock, flags);
	for (i = 0; i < cam->nbufs; i++) {
		int bufno = cam->next_buf;
		if (bufno < 0) {  /* "will never happen" */
			cam_err(cam, "No valid bufs in tasklet!\n");
			break;
		}
		if (++(cam->next_buf) >= cam->nbufs)
			cam->next_buf = 0;
		if (! test_bit(bufno, &cam->flags))
			continue;
		if (list_empty(&cam->sb_avail))
			break;  /* Leave it valid, hope for better later */
		clear_bit(bufno, &cam->flags);
		sbuf = list_entry(cam->sb_avail.next,
				struct cafe_sio_buffer, list);
		/*
		 * Drop the lock during the big copy.  This *should* be safe...
		 */
		spin_unlock_irqrestore(&cam->dev_lock, flags);
		memcpy(sbuf->buffer, cam->dma_bufs[bufno],
				cam->pix_format.sizeimage);
		sbuf->v4lbuf.bytesused = cam->pix_format.sizeimage;
		sbuf->v4lbuf.sequence = cam->buf_seq[bufno];
		sbuf->v4lbuf.flags &= ~V4L2_BUF_FLAG_QUEUED;
		sbuf->v4lbuf.flags |= V4L2_BUF_FLAG_DONE;
		spin_lock_irqsave(&cam->dev_lock, flags);
		list_move_tail(&sbuf->list, &cam->sb_full);
	}
	if (! list_empty(&cam->sb_full))
		wake_up(&cam->iowait);
	spin_unlock_irqrestore(&cam->dev_lock, flags);
}



static void cafe_frame_complete(struct cafe_camera *cam, int frame)
{
	/*
	 * Basic frame housekeeping.
	 */
	if (test_bit(frame, &cam->flags) && printk_ratelimit())
		cam_err(cam, "Frame overrun on %d, frames lost\n", frame);
	set_bit(frame, &cam->flags);
	clear_bit(CF_DMA_ACTIVE, &cam->flags);
	if (cam->next_buf < 0)
		cam->next_buf = frame;
	cam->buf_seq[frame] = ++(cam->sequence);

	switch (cam->state) {
	/*
	 * If in single read mode, try going speculative.
	 */
	    case S_SINGLEREAD:
		cam->state = S_SPECREAD;
		cam->specframes = 0;
		wake_up(&cam->iowait);
		break;

	/*
	 * If we are already doing speculative reads, and nobody is
	 * reading them, just stop.
	 */
	    case S_SPECREAD:
		if (++(cam->specframes) >= cam->nbufs) {
			cafe_ctlr_stop(cam);
			cafe_ctlr_irq_disable(cam);
			cam->state = S_IDLE;
		}
		wake_up(&cam->iowait);
		break;
	/*
	 * For the streaming case, we defer the real work to the
	 * camera tasklet.
	 *
	 * FIXME: if the application is not consuming the buffers,
	 * we should eventually put things on hold and restart in
	 * vidioc_dqbuf().
	 */
	    case S_STREAMING:
		tasklet_schedule(&cam->s_tasklet);
		break;

	    default:
		cam_err(cam, "Frame interrupt in non-operational state\n");
		break;
	}
}




static void cafe_frame_irq(struct cafe_camera *cam, unsigned int irqs)
{
	unsigned int frame;

	cafe_reg_write(cam, REG_IRQSTAT, FRAMEIRQS); /* Clear'em all */
	/*
	 * Handle any frame completions.  There really should
	 * not be more than one of these, or we have fallen
	 * far behind.
	 */
	for (frame = 0; frame < cam->nbufs; frame++)
		if (irqs & (IRQ_EOF0 << frame))
			cafe_frame_complete(cam, frame);
	/*
	 * If a frame starts, note that we have DMA active.  This
	 * code assumes that we won't get multiple frame interrupts
	 * at once; may want to rethink that.
	 */
	if (irqs & (IRQ_SOF0 | IRQ_SOF1 | IRQ_SOF2))
		set_bit(CF_DMA_ACTIVE, &cam->flags);
}



static irqreturn_t cafe_irq(int irq, void *data)
{
	struct cafe_camera *cam = data;
	unsigned int irqs;

	spin_lock(&cam->dev_lock);
	irqs = cafe_reg_read(cam, REG_IRQSTAT);
	if ((irqs & ALLIRQS) == 0) {
		spin_unlock(&cam->dev_lock);
		return IRQ_NONE;
	}
	if (irqs & FRAMEIRQS)
		cafe_frame_irq(cam, irqs);
	if (irqs & TWSIIRQS) {
		cafe_reg_write(cam, REG_IRQSTAT, TWSIIRQS);
		wake_up(&cam->smbus_wait);
	}
	spin_unlock(&cam->dev_lock);
	return IRQ_HANDLED;
}


/* -------------------------------------------------------------------------- */
#ifdef CONFIG_VIDEO_ADV_DEBUG
/*
 * Debugfs stuff.
 */

static char cafe_debug_buf[1024];
static struct dentry *cafe_dfs_root;

static void cafe_dfs_setup(void)
{
	cafe_dfs_root = debugfs_create_dir("cafe_ccic", NULL);
	if (IS_ERR(cafe_dfs_root)) {
		cafe_dfs_root = NULL;  /* Never mind */
		printk(KERN_NOTICE "cafe_ccic unable to set up debugfs\n");
	}
}

static void cafe_dfs_shutdown(void)
{
	if (cafe_dfs_root)
		debugfs_remove(cafe_dfs_root);
}

static int cafe_dfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t cafe_dfs_read_regs(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct cafe_camera *cam = file->private_data;
	char *s = cafe_debug_buf;
	int offset;

	for (offset = 0; offset < 0x44; offset += 4)
		s += sprintf(s, "%02x: %08x\n", offset,
				cafe_reg_read(cam, offset));
	for (offset = 0x88; offset <= 0x90; offset += 4)
		s += sprintf(s, "%02x: %08x\n", offset,
				cafe_reg_read(cam, offset));
	for (offset = 0xb4; offset <= 0xbc; offset += 4)
		s += sprintf(s, "%02x: %08x\n", offset,
				cafe_reg_read(cam, offset));
	for (offset = 0x3000; offset <= 0x300c; offset += 4)
		s += sprintf(s, "%04x: %08x\n", offset,
				cafe_reg_read(cam, offset));
	return simple_read_from_buffer(buf, count, ppos, cafe_debug_buf,
			s - cafe_debug_buf);
}

static const struct file_operations cafe_dfs_reg_ops = {
	.owner = THIS_MODULE,
	.read = cafe_dfs_read_regs,
	.open = cafe_dfs_open
};

static ssize_t cafe_dfs_read_cam(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct cafe_camera *cam = file->private_data;
	char *s = cafe_debug_buf;
	int offset;

	if (! cam->sensor)
		return -EINVAL;
	for (offset = 0x0; offset < 0x8a; offset++)
	{
		u8 v;

		cafe_smbus_read_data(cam, cam->sensor->addr, offset, &v);
		s += sprintf(s, "%02x: %02x\n", offset, v);
	}
	return simple_read_from_buffer(buf, count, ppos, cafe_debug_buf,
			s - cafe_debug_buf);
}

static const struct file_operations cafe_dfs_cam_ops = {
	.owner = THIS_MODULE,
	.read = cafe_dfs_read_cam,
	.open = cafe_dfs_open
};



static void cafe_dfs_cam_setup(struct cafe_camera *cam)
{
	char fname[40];

	if (!cafe_dfs_root)
		return;
	sprintf(fname, "regs-%d", cam->v4ldev.minor);
	cam->dfs_regs = debugfs_create_file(fname, 0444, cafe_dfs_root,
			cam, &cafe_dfs_reg_ops);
	sprintf(fname, "cam-%d", cam->v4ldev.minor);
	cam->dfs_cam_regs = debugfs_create_file(fname, 0444, cafe_dfs_root,
			cam, &cafe_dfs_cam_ops);
}


static void cafe_dfs_cam_shutdown(struct cafe_camera *cam)
{
	if (! IS_ERR(cam->dfs_regs))
		debugfs_remove(cam->dfs_regs);
	if (! IS_ERR(cam->dfs_cam_regs))
		debugfs_remove(cam->dfs_cam_regs);
}

#else

#define cafe_dfs_setup()
#define cafe_dfs_shutdown()
#define cafe_dfs_cam_setup(cam)
#define cafe_dfs_cam_shutdown(cam)
#endif    /* CONFIG_VIDEO_ADV_DEBUG */




/* ------------------------------------------------------------------------*/
/*
 * PCI interface stuff.
 */

static int cafe_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	int ret;
	u16 classword;
	struct cafe_camera *cam;
	/*
	 * Make sure we have a camera here - we'll get calls for
	 * the other cafe devices as well.
	 */
	pci_read_config_word(pdev, PCI_CLASS_DEVICE, &classword);
	if (classword != PCI_CLASS_MULTIMEDIA_VIDEO)
		return -ENODEV;
	/*
	 * Start putting together one of our big camera structures.
	 */
	ret = -ENOMEM;
	cam = kzalloc(sizeof(struct cafe_camera), GFP_KERNEL);
	if (cam == NULL)
		goto out;
	mutex_init(&cam->s_mutex);
	mutex_lock(&cam->s_mutex);
	spin_lock_init(&cam->dev_lock);
	cam->state = S_NOTREADY;
	cafe_set_config_needed(cam, 1);
	init_waitqueue_head(&cam->smbus_wait);
	init_waitqueue_head(&cam->iowait);
	cam->pdev = pdev;
	cam->pix_format = cafe_def_pix_format;
	INIT_LIST_HEAD(&cam->dev_list);
	INIT_LIST_HEAD(&cam->sb_avail);
	INIT_LIST_HEAD(&cam->sb_full);
	tasklet_init(&cam->s_tasklet, cafe_frame_tasklet, (unsigned long) cam);
	/*
	 * Get set up on the PCI bus.
	 */
	ret = pci_enable_device(pdev);
	if (ret)
		goto out_free;
	pci_set_master(pdev);

	ret = -EIO;
	cam->regs = pci_iomap(pdev, 0, 0);
	if (! cam->regs) {
		printk(KERN_ERR "Unable to ioremap cafe-ccic regs\n");
		goto out_free;
	}
	ret = request_irq(pdev->irq, cafe_irq, IRQF_SHARED, "cafe-ccic", cam);
	if (ret)
		goto out_iounmap;
	/*
	 * Initialize the controller and leave it powered up.  It will
	 * stay that way until the sensor driver shows up.
	 */
	cafe_ctlr_init(cam);
	cafe_ctlr_power_up(cam);
	/*
	 * Set up I2C/SMBUS communications.  We have to drop the mutex here
	 * because the sensor could attach in this call chain, leading to
	 * unsightly deadlocks.
	 */
	mutex_unlock(&cam->s_mutex);  /* attach can deadlock */
	ret = cafe_smbus_setup(cam);
	if (ret)
		goto out_freeirq;
	/*
	 * Get the v4l2 setup done.
	 */
	mutex_lock(&cam->s_mutex);
	cam->v4ldev = cafe_v4l_template;
	cam->v4ldev.debug = 0;
//	cam->v4ldev.debug = V4L2_DEBUG_IOCTL_ARG;
	cam->v4ldev.dev = &pdev->dev;
	ret = video_register_device(&cam->v4ldev, VFL_TYPE_GRABBER, -1);
	if (ret)
		goto out_smbus;
	/*
	 * If so requested, try to get our DMA buffers now.
	 */
	if (!alloc_bufs_at_read) {
		if (cafe_alloc_dma_bufs(cam, 1))
			cam_warn(cam, "Unable to alloc DMA buffers at load"
					" will try again later.");
	}

	cafe_dfs_cam_setup(cam);
	mutex_unlock(&cam->s_mutex);
	cafe_add_dev(cam);
	return 0;

  out_smbus:
	cafe_smbus_shutdown(cam);
  out_freeirq:
	cafe_ctlr_power_down(cam);
	free_irq(pdev->irq, cam);
  out_iounmap:
	pci_iounmap(pdev, cam->regs);
  out_free:
	kfree(cam);
  out:
	return ret;
}


/*
 * Shut down an initialized device
 */
static void cafe_shutdown(struct cafe_camera *cam)
{
/* FIXME: Make sure we take care of everything here */
	cafe_dfs_cam_shutdown(cam);
	if (cam->n_sbufs > 0)
		/* What if they are still mapped?  Shouldn't be, but... */
		cafe_free_sio_buffers(cam);
	cafe_remove_dev(cam);
	cafe_ctlr_stop_dma(cam);
	cafe_ctlr_power_down(cam);
	cafe_smbus_shutdown(cam);
	cafe_free_dma_bufs(cam);
	free_irq(cam->pdev->irq, cam);
	pci_iounmap(cam->pdev, cam->regs);
	video_unregister_device(&cam->v4ldev);
	/* kfree(cam); done in v4l_release () */
}


static void cafe_pci_remove(struct pci_dev *pdev)
{
	struct cafe_camera *cam = cafe_find_by_pdev(pdev);

	if (cam == NULL) {
		printk(KERN_WARNING "pci_remove on unknown pdev %p\n", pdev);
		return;
	}
	mutex_lock(&cam->s_mutex);
	if (cam->users > 0)
		cam_warn(cam, "Removing a device with users!\n");
	cafe_shutdown(cam);
/* No unlock - it no longer exists */
}


#ifdef CONFIG_PM
/*
 * Basic power management.
 */
static int cafe_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct cafe_camera *cam = cafe_find_by_pdev(pdev);
	int ret;
	enum cafe_state cstate;

	ret = pci_save_state(pdev);
	if (ret)
		return ret;
	cstate = cam->state; /* HACK - stop_dma sets to idle */
	cafe_ctlr_stop_dma(cam);
	cafe_ctlr_power_down(cam);
	pci_disable_device(pdev);
	cam->state = cstate;
	return 0;
}


static int cafe_pci_resume(struct pci_dev *pdev)
{
	struct cafe_camera *cam = cafe_find_by_pdev(pdev);
	int ret = 0;

	ret = pci_restore_state(pdev);
	if (ret)
		return ret;
	ret = pci_enable_device(pdev);

	if (ret) {
		cam_warn(cam, "Unable to re-enable device on resume!\n");
		return ret;
	}
	cafe_ctlr_init(cam);
	cafe_ctlr_power_down(cam);

	mutex_lock(&cam->s_mutex);
	if (cam->users > 0) {
		cafe_ctlr_power_up(cam);
		__cafe_cam_reset(cam);
	}
	mutex_unlock(&cam->s_mutex);

	set_bit(CF_CONFIG_NEEDED, &cam->flags);
	if (cam->state == S_SPECREAD)
		cam->state = S_IDLE;  /* Don't bother restarting */
	else if (cam->state == S_SINGLEREAD || cam->state == S_STREAMING)
		ret = cafe_read_setup(cam, cam->state);
	return ret;
}

#endif  /* CONFIG_PM */


static struct pci_device_id cafe_ids[] = {
	{ PCI_DEVICE(0x11ab, 0x4100) }, /* Eventual real ID */
	{ PCI_DEVICE(0x11ab, 0x4102) }, /* Really eventual real ID */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, cafe_ids);

static struct pci_driver cafe_pci_driver = {
	.name = "cafe1000-ccic",
	.id_table = cafe_ids,
	.probe = cafe_pci_probe,
	.remove = cafe_pci_remove,
#ifdef CONFIG_PM
	.suspend = cafe_pci_suspend,
	.resume = cafe_pci_resume,
#endif
};




static int __init cafe_init(void)
{
	int ret;

	printk(KERN_NOTICE "Marvell M88ALP01 'CAFE' Camera Controller version %d\n",
			CAFE_VERSION);
	cafe_dfs_setup();
	ret = pci_register_driver(&cafe_pci_driver);
	if (ret) {
		printk(KERN_ERR "Unable to register cafe_ccic driver\n");
		goto out;
	}
	request_module("ov7670");  /* FIXME want something more general */
	ret = 0;

  out:
	return ret;
}


static void __exit cafe_exit(void)
{
	pci_unregister_driver(&cafe_pci_driver);
	cafe_dfs_shutdown();
}

module_init(cafe_init);
module_exit(cafe_exit);
