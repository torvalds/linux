/*
 * A driver for the CMOS camera controller in the Marvell 88ALP01 "cafe"
 * multifunction chip.  Currently works with the Omnivision OV7670
 * sensor.
 *
 * The data sheet for this device can be found at:
 *    http://www.marvell.com/products/pc_connectivity/88alp01/
 *
 * Copyright 2006-11 One Laptop Per Child Association, Inc.
 * Copyright 2006-11 Jonathan Corbet <corbet@lwn.net>
 *
 * Written by Jonathan Corbet, corbet@lwn.net.
 *
 * v4l2_device/v4l2_subdev conversion by:
 * Copyright (C) 2009 Hans Verkuil <hverkuil@xs4all.nl>
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "mcam-core.h"

#define CAFE_VERSION 0x000002


/*
 * Parameters.
 */
MODULE_AUTHOR("Jonathan Corbet <corbet@lwn.net>");
MODULE_DESCRIPTION("Marvell 88ALP01 CMOS Camera Controller driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("Video");




struct cafe_camera {
	int registered;			/* Fully initialized? */
	struct mcam_camera mcam;
	struct pci_dev *pdev;
	wait_queue_head_t smbus_wait;	/* Waiting on i2c events */
};

/*
 * Most of the camera controller registers are defined in mcam-core.h,
 * but the Cafe platform has some additional registers of its own;
 * they are described here.
 */

/*
 * "General purpose register" has a couple of GPIOs used for sensor
 * power and reset on OLPC XO 1.0 systems.
 */
#define REG_GPR		0xb4
#define	  GPR_C1EN	  0x00000020	/* Pad 1 (power down) enable */
#define	  GPR_C0EN	  0x00000010	/* Pad 0 (reset) enable */
#define	  GPR_C1	  0x00000002	/* Control 1 value */
/*
 * Control 0 is wired to reset on OLPC machines.  For ov7x sensors,
 * it is active low.
 */
#define	  GPR_C0	  0x00000001	/* Control 0 value */

/*
 * These registers control the SMBUS module for communicating
 * with the sensor.
 */
#define REG_TWSIC0	0xb8	/* TWSI (smbus) control 0 */
#define	  TWSIC0_EN	  0x00000001	/* TWSI enable */
#define	  TWSIC0_MODE	  0x00000002	/* 1 = 16-bit, 0 = 8-bit */
#define	  TWSIC0_SID	  0x000003fc	/* Slave ID */
/*
 * Subtle trickery: the slave ID field starts with bit 2.  But the
 * Linux i2c stack wants to treat the bottommost bit as a separate
 * read/write bit, which is why slave ID's are usually presented
 * >>1.  For consistency with that behavior, we shift over three
 * bits instead of two.
 */
#define	  TWSIC0_SID_SHIFT 3
#define	  TWSIC0_CLKDIV	  0x0007fc00	/* Clock divider */
#define	  TWSIC0_MASKACK  0x00400000	/* Mask ack from sensor */
#define	  TWSIC0_OVMAGIC  0x00800000	/* Make it work on OV sensors */

#define REG_TWSIC1	0xbc	/* TWSI control 1 */
#define	  TWSIC1_DATA	  0x0000ffff	/* Data to/from camchip */
#define	  TWSIC1_ADDR	  0x00ff0000	/* Address (register) */
#define	  TWSIC1_ADDR_SHIFT 16
#define	  TWSIC1_READ	  0x01000000	/* Set for read op */
#define	  TWSIC1_WSTAT	  0x02000000	/* Write status */
#define	  TWSIC1_RVALID	  0x04000000	/* Read data valid */
#define	  TWSIC1_ERROR	  0x08000000	/* Something screwed up */

/*
 * Here's the weird global control registers
 */
#define REG_GL_CSR     0x3004  /* Control/status register */
#define	  GCSR_SRS	 0x00000001	/* SW Reset set */
#define	  GCSR_SRC	 0x00000002	/* SW Reset clear */
#define	  GCSR_MRS	 0x00000004	/* Master reset set */
#define	  GCSR_MRC	 0x00000008	/* HW Reset clear */
#define	  GCSR_CCIC_EN	 0x00004000    /* CCIC Clock enable */
#define REG_GL_IMASK   0x300c  /* Interrupt mask register */
#define	  GIMSK_CCIC_EN		 0x00000004    /* CCIC Interrupt enable */

#define REG_GL_FCR	0x3038	/* GPIO functional control register */
#define	  GFCR_GPIO_ON	  0x08		/* Camera GPIO enabled */
#define REG_GL_GPIOR	0x315c	/* GPIO register */
#define	  GGPIO_OUT		0x80000	/* GPIO output */
#define	  GGPIO_VAL		0x00008	/* Output pin value */

#define REG_LEN		       (REG_GL_IMASK + 4)


/*
 * Debugging and related.
 */
#define cam_err(cam, fmt, arg...) \
	dev_err(&(cam)->pdev->dev, fmt, ##arg);
#define cam_warn(cam, fmt, arg...) \
	dev_warn(&(cam)->pdev->dev, fmt, ##arg);

/* -------------------------------------------------------------------- */
/*
 * The I2C/SMBUS interface to the camera itself starts here.  The
 * controller handles SMBUS itself, presenting a relatively simple register
 * interface; all we have to do is to tell it where to route the data.
 */
#define CAFE_SMBUS_TIMEOUT (HZ)  /* generous */

static inline struct cafe_camera *to_cam(struct v4l2_device *dev)
{
	struct mcam_camera *m = container_of(dev, struct mcam_camera, v4l2_dev);
	return container_of(m, struct cafe_camera, mcam);
}


static int cafe_smbus_write_done(struct mcam_camera *mcam)
{
	unsigned long flags;
	int c1;

	/*
	 * We must delay after the interrupt, or the controller gets confused
	 * and never does give us good status.  Fortunately, we don't do this
	 * often.
	 */
	udelay(20);
	spin_lock_irqsave(&mcam->dev_lock, flags);
	c1 = mcam_reg_read(mcam, REG_TWSIC1);
	spin_unlock_irqrestore(&mcam->dev_lock, flags);
	return (c1 & (TWSIC1_WSTAT|TWSIC1_ERROR)) != TWSIC1_WSTAT;
}

static int cafe_smbus_write_data(struct cafe_camera *cam,
		u16 addr, u8 command, u8 value)
{
	unsigned int rval;
	unsigned long flags;
	struct mcam_camera *mcam = &cam->mcam;

	spin_lock_irqsave(&mcam->dev_lock, flags);
	rval = TWSIC0_EN | ((addr << TWSIC0_SID_SHIFT) & TWSIC0_SID);
	rval |= TWSIC0_OVMAGIC;  /* Make OV sensors work */
	/*
	 * Marvell sez set clkdiv to all 1's for now.
	 */
	rval |= TWSIC0_CLKDIV;
	mcam_reg_write(mcam, REG_TWSIC0, rval);
	(void) mcam_reg_read(mcam, REG_TWSIC1); /* force write */
	rval = value | ((command << TWSIC1_ADDR_SHIFT) & TWSIC1_ADDR);
	mcam_reg_write(mcam, REG_TWSIC1, rval);
	spin_unlock_irqrestore(&mcam->dev_lock, flags);

	/* Unfortunately, reading TWSIC1 too soon after sending a command
	 * causes the device to die.
	 * Use a busy-wait because we often send a large quantity of small
	 * commands at-once; using msleep() would cause a lot of context
	 * switches which take longer than 2ms, resulting in a noticeable
	 * boot-time and capture-start delays.
	 */
	mdelay(2);

	/*
	 * Another sad fact is that sometimes, commands silently complete but
	 * cafe_smbus_write_done() never becomes aware of this.
	 * This happens at random and appears to possible occur with any
	 * command.
	 * We don't understand why this is. We work around this issue
	 * with the timeout in the wait below, assuming that all commands
	 * complete within the timeout.
	 */
	wait_event_timeout(cam->smbus_wait, cafe_smbus_write_done(mcam),
			CAFE_SMBUS_TIMEOUT);

	spin_lock_irqsave(&mcam->dev_lock, flags);
	rval = mcam_reg_read(mcam, REG_TWSIC1);
	spin_unlock_irqrestore(&mcam->dev_lock, flags);

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



static int cafe_smbus_read_done(struct mcam_camera *mcam)
{
	unsigned long flags;
	int c1;

	/*
	 * We must delay after the interrupt, or the controller gets confused
	 * and never does give us good status.  Fortunately, we don't do this
	 * often.
	 */
	udelay(20);
	spin_lock_irqsave(&mcam->dev_lock, flags);
	c1 = mcam_reg_read(mcam, REG_TWSIC1);
	spin_unlock_irqrestore(&mcam->dev_lock, flags);
	return c1 & (TWSIC1_RVALID|TWSIC1_ERROR);
}



static int cafe_smbus_read_data(struct cafe_camera *cam,
		u16 addr, u8 command, u8 *value)
{
	unsigned int rval;
	unsigned long flags;
	struct mcam_camera *mcam = &cam->mcam;

	spin_lock_irqsave(&mcam->dev_lock, flags);
	rval = TWSIC0_EN | ((addr << TWSIC0_SID_SHIFT) & TWSIC0_SID);
	rval |= TWSIC0_OVMAGIC; /* Make OV sensors work */
	/*
	 * Marvel sez set clkdiv to all 1's for now.
	 */
	rval |= TWSIC0_CLKDIV;
	mcam_reg_write(mcam, REG_TWSIC0, rval);
	(void) mcam_reg_read(mcam, REG_TWSIC1); /* force write */
	rval = TWSIC1_READ | ((command << TWSIC1_ADDR_SHIFT) & TWSIC1_ADDR);
	mcam_reg_write(mcam, REG_TWSIC1, rval);
	spin_unlock_irqrestore(&mcam->dev_lock, flags);

	wait_event_timeout(cam->smbus_wait,
			cafe_smbus_read_done(mcam), CAFE_SMBUS_TIMEOUT);
	spin_lock_irqsave(&mcam->dev_lock, flags);
	rval = mcam_reg_read(mcam, REG_TWSIC1);
	spin_unlock_irqrestore(&mcam->dev_lock, flags);

	if (rval & TWSIC1_ERROR) {
		cam_err(cam, "SMBUS read (%02x/%02x) error\n", addr, command);
		return -EIO;
	}
	if (!(rval & TWSIC1_RVALID)) {
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

	spin_lock_irqsave(&cam->mcam.dev_lock, flags);
	mcam_reg_set_bit(&cam->mcam, REG_IRQMASK, TWSIIRQS);
	spin_unlock_irqrestore(&cam->mcam.dev_lock, flags);
}

static u32 cafe_smbus_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_READ_BYTE_DATA  |
	       I2C_FUNC_SMBUS_WRITE_BYTE_DATA;
}

static const struct i2c_algorithm cafe_smbus_algo = {
	.smbus_xfer = cafe_smbus_xfer,
	.functionality = cafe_smbus_func
};

static int cafe_smbus_setup(struct cafe_camera *cam)
{
	struct i2c_adapter *adap;
	int ret;

	adap = kzalloc(sizeof(*adap), GFP_KERNEL);
	if (adap == NULL)
		return -ENOMEM;
	adap->owner = THIS_MODULE;
	adap->algo = &cafe_smbus_algo;
	strcpy(adap->name, "cafe_ccic");
	adap->dev.parent = &cam->pdev->dev;
	i2c_set_adapdata(adap, cam);
	ret = i2c_add_adapter(adap);
	if (ret) {
		printk(KERN_ERR "Unable to register cafe i2c adapter\n");
		kfree(adap);
		return ret;
	}

	cam->mcam.i2c_adapter = adap;
	cafe_smbus_enable_irq(cam);
	return 0;
}

static void cafe_smbus_shutdown(struct cafe_camera *cam)
{
	i2c_del_adapter(cam->mcam.i2c_adapter);
	kfree(cam->mcam.i2c_adapter);
}


/*
 * Controller-level stuff
 */

static void cafe_ctlr_init(struct mcam_camera *mcam)
{
	unsigned long flags;

	spin_lock_irqsave(&mcam->dev_lock, flags);
	/*
	 * Added magic to bring up the hardware on the B-Test board
	 */
	mcam_reg_write(mcam, 0x3038, 0x8);
	mcam_reg_write(mcam, 0x315c, 0x80008);
	/*
	 * Go through the dance needed to wake the device up.
	 * Note that these registers are global and shared
	 * with the NAND and SD devices.  Interaction between the
	 * three still needs to be examined.
	 */
	mcam_reg_write(mcam, REG_GL_CSR, GCSR_SRS|GCSR_MRS); /* Needed? */
	mcam_reg_write(mcam, REG_GL_CSR, GCSR_SRC|GCSR_MRC);
	mcam_reg_write(mcam, REG_GL_CSR, GCSR_SRC|GCSR_MRS);
	/*
	 * Here we must wait a bit for the controller to come around.
	 */
	spin_unlock_irqrestore(&mcam->dev_lock, flags);
	msleep(5);
	spin_lock_irqsave(&mcam->dev_lock, flags);

	mcam_reg_write(mcam, REG_GL_CSR, GCSR_CCIC_EN|GCSR_SRC|GCSR_MRC);
	mcam_reg_set_bit(mcam, REG_GL_IMASK, GIMSK_CCIC_EN);
	/*
	 * Mask all interrupts.
	 */
	mcam_reg_write(mcam, REG_IRQMASK, 0);
	spin_unlock_irqrestore(&mcam->dev_lock, flags);
}


static int cafe_ctlr_power_up(struct mcam_camera *mcam)
{
	/*
	 * Part one of the sensor dance: turn the global
	 * GPIO signal on.
	 */
	mcam_reg_write(mcam, REG_GL_FCR, GFCR_GPIO_ON);
	mcam_reg_write(mcam, REG_GL_GPIOR, GGPIO_OUT|GGPIO_VAL);
	/*
	 * Put the sensor into operational mode (assumes OLPC-style
	 * wiring).  Control 0 is reset - set to 1 to operate.
	 * Control 1 is power down, set to 0 to operate.
	 */
	mcam_reg_write(mcam, REG_GPR, GPR_C1EN|GPR_C0EN); /* pwr up, reset */
	mcam_reg_write(mcam, REG_GPR, GPR_C1EN|GPR_C0EN|GPR_C0);

	return 0;
}

static void cafe_ctlr_power_down(struct mcam_camera *mcam)
{
	mcam_reg_write(mcam, REG_GPR, GPR_C1EN|GPR_C0EN|GPR_C1);
	mcam_reg_write(mcam, REG_GL_FCR, GFCR_GPIO_ON);
	mcam_reg_write(mcam, REG_GL_GPIOR, GGPIO_OUT);
}



/*
 * The platform interrupt handler.
 */
static irqreturn_t cafe_irq(int irq, void *data)
{
	struct cafe_camera *cam = data;
	struct mcam_camera *mcam = &cam->mcam;
	unsigned int irqs, handled;

	spin_lock(&mcam->dev_lock);
	irqs = mcam_reg_read(mcam, REG_IRQSTAT);
	handled = cam->registered && mccic_irq(mcam, irqs);
	if (irqs & TWSIIRQS) {
		mcam_reg_write(mcam, REG_IRQSTAT, TWSIIRQS);
		wake_up(&cam->smbus_wait);
		handled = 1;
	}
	spin_unlock(&mcam->dev_lock);
	return IRQ_RETVAL(handled);
}


/* -------------------------------------------------------------------------- */
/*
 * PCI interface stuff.
 */

static int cafe_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	int ret;
	struct cafe_camera *cam;
	struct mcam_camera *mcam;

	/*
	 * Start putting together one of our big camera structures.
	 */
	ret = -ENOMEM;
	cam = kzalloc(sizeof(struct cafe_camera), GFP_KERNEL);
	if (cam == NULL)
		goto out;
	cam->pdev = pdev;
	mcam = &cam->mcam;
	mcam->chip_id = MCAM_CAFE;
	spin_lock_init(&mcam->dev_lock);
	init_waitqueue_head(&cam->smbus_wait);
	mcam->plat_power_up = cafe_ctlr_power_up;
	mcam->plat_power_down = cafe_ctlr_power_down;
	mcam->dev = &pdev->dev;
	snprintf(mcam->bus_info, sizeof(mcam->bus_info), "PCI:%s", pci_name(pdev));
	/*
	 * Set the clock speed for the XO 1; I don't believe this
	 * driver has ever run anywhere else.
	 */
	mcam->clock_speed = 45;
	mcam->use_smbus = 1;
	/*
	 * Vmalloc mode for buffers is traditional with this driver.
	 * We *might* be able to run DMA_contig, especially on a system
	 * with CMA in it.
	 */
	mcam->buffer_mode = B_vmalloc;
	/*
	 * Get set up on the PCI bus.
	 */
	ret = pci_enable_device(pdev);
	if (ret)
		goto out_free;
	pci_set_master(pdev);

	ret = -EIO;
	mcam->regs = pci_iomap(pdev, 0, 0);
	if (!mcam->regs) {
		printk(KERN_ERR "Unable to ioremap cafe-ccic regs\n");
		goto out_disable;
	}
	mcam->regs_size = pci_resource_len(pdev, 0);
	ret = request_irq(pdev->irq, cafe_irq, IRQF_SHARED, "cafe-ccic", cam);
	if (ret)
		goto out_iounmap;

	/*
	 * Initialize the controller and leave it powered up.  It will
	 * stay that way until the sensor driver shows up.
	 */
	cafe_ctlr_init(mcam);
	cafe_ctlr_power_up(mcam);
	/*
	 * Set up I2C/SMBUS communications.  We have to drop the mutex here
	 * because the sensor could attach in this call chain, leading to
	 * unsightly deadlocks.
	 */
	ret = cafe_smbus_setup(cam);
	if (ret)
		goto out_pdown;

	ret = mccic_register(mcam);
	if (ret == 0) {
		cam->registered = 1;
		return 0;
	}

	cafe_smbus_shutdown(cam);
out_pdown:
	cafe_ctlr_power_down(mcam);
	free_irq(pdev->irq, cam);
out_iounmap:
	pci_iounmap(pdev, mcam->regs);
out_disable:
	pci_disable_device(pdev);
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
	mccic_shutdown(&cam->mcam);
	cafe_smbus_shutdown(cam);
	free_irq(cam->pdev->irq, cam);
	pci_iounmap(cam->pdev, cam->mcam.regs);
}


static void cafe_pci_remove(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct cafe_camera *cam = to_cam(v4l2_dev);

	if (cam == NULL) {
		printk(KERN_WARNING "pci_remove on unknown pdev %p\n", pdev);
		return;
	}
	cafe_shutdown(cam);
	kfree(cam);
}


#ifdef CONFIG_PM
/*
 * Basic power management.
 */
static int cafe_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct cafe_camera *cam = to_cam(v4l2_dev);
	int ret;

	ret = pci_save_state(pdev);
	if (ret)
		return ret;
	mccic_suspend(&cam->mcam);
	pci_disable_device(pdev);
	return 0;
}


static int cafe_pci_resume(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct cafe_camera *cam = to_cam(v4l2_dev);
	int ret = 0;

	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);

	if (ret) {
		cam_warn(cam, "Unable to re-enable device on resume!\n");
		return ret;
	}
	cafe_ctlr_init(&cam->mcam);
	return mccic_resume(&cam->mcam);
}

#endif  /* CONFIG_PM */

static const struct pci_device_id cafe_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL,
		     PCI_DEVICE_ID_MARVELL_88ALP01_CCIC) },
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
	ret = pci_register_driver(&cafe_pci_driver);
	if (ret) {
		printk(KERN_ERR "Unable to register cafe_ccic driver\n");
		goto out;
	}
	ret = 0;

out:
	return ret;
}


static void __exit cafe_exit(void)
{
	pci_unregister_driver(&cafe_pci_driver);
}

module_init(cafe_init);
module_exit(cafe_exit);
