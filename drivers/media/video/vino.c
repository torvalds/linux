/*
 * (incomplete) Driver for the VINO (Video In No Out) system found in SGI Indys.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * Copyright (C) 2003 Ladislav Michl <ladis@linux-mips.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/wrapper.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/videodev.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-sgi.h>

#include <asm/addrspace.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/pgtable.h>
#include <asm/paccess.h>
#include <asm/io.h>
#include <asm/sgi/ip22.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/mc.h>

#include "vino.h"

/* debugging? */
#if 1
#define DEBUG(x...)     printk(x);
#else
#define DEBUG(x...)
#endif


/* VINO ASIC registers */
struct sgi_vino *vino;

static const char *vinostr = "VINO IndyCam/TV";
static int threshold_a = 512;
static int threshold_b = 512;

struct vino_device {
	struct video_device vdev;
#define VINO_CHAN_A		1
#define VINO_CHAN_B		2
	int chan;
};

struct vino_client {
	struct i2c_client *driver;
	int owner;
};

struct vino_video {
	struct vino_device chA;
	struct vino_device chB;

	struct vino_client decoder;
	struct vino_client camera;

	struct semaphore input_lock;

	/* Loaded into VINO descriptors to clear End Of Descriptors table
	 * interupt condition */
	unsigned long dummy_page;
	unsigned int dummy_buf[4] __attribute__((aligned(8)));
};

static struct vino_video *Vino;

unsigned i2c_vino_getctrl(void *data)
{
	return vino->i2c_control;
}

void i2c_vino_setctrl(void *data, unsigned val)
{
	vino->i2c_control = val;
}

unsigned i2c_vino_rdata(void *data)
{
	return vino->i2c_data;
}

void i2c_vino_wdata(void *data, unsigned val)
{
	vino->i2c_data = val;
}

static struct i2c_algo_sgi_data i2c_sgi_vino_data =
{
	.getctrl = &i2c_vino_getctrl,
	.setctrl = &i2c_vino_setctrl,
	.rdata   = &i2c_vino_rdata,
	.wdata   = &i2c_vino_wdata,
	.xfer_timeout = 200,
	.ack_timeout  = 1000,
};

/*
 * There are two possible clients on VINO I2C bus, so we limit usage only
 * to them.
 */
static int i2c_vino_client_reg(struct i2c_client *client)
{
	int res = 0;

	down(&Vino->input_lock);
	switch (client->driver->id) {
	case I2C_DRIVERID_SAA7191:
		if (Vino->decoder.driver)
			res = -EBUSY;
		else
			Vino->decoder.driver = client;
		break;
	case I2C_DRIVERID_INDYCAM:
		if (Vino->camera.driver)
			res = -EBUSY;
		else
			Vino->camera.driver = client;
		break;
	default:
		res = -ENODEV;
	}
	up(&Vino->input_lock);

	return res;
}

static int i2c_vino_client_unreg(struct i2c_client *client)
{
	int res = 0;

	down(&Vino->input_lock);
	if (client == Vino->decoder.driver) {
		if (Vino->decoder.owner)
			res = -EBUSY;
		else
			Vino->decoder.driver = NULL;
	} else if (client == Vino->camera.driver) {
		if (Vino->camera.owner)
			res = -EBUSY;
		else
			Vino->camera.driver = NULL;
	}
	up(&Vino->input_lock);

	return res;
}

static struct i2c_adapter vino_i2c_adapter =
{
	.name			= "VINO I2C bus",
	.id			= I2C_HW_SGI_VINO,
	.algo_data		= &i2c_sgi_vino_data,
	.client_register	= &i2c_vino_client_reg,
	.client_unregister	= &i2c_vino_client_unreg,
};

static int vino_i2c_add_bus(void)
{
	return i2c_sgi_add_bus(&vino_i2c_adapter);
}

static int vino_i2c_del_bus(void)
{
	return i2c_sgi_del_bus(&vino_i2c_adapter);
}


static void vino_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
}

static int vino_open(struct video_device *dev, int flags)
{
	struct vino_device *videv = (struct vino_device *)dev;

	return 0;
}

static void vino_close(struct video_device *dev)
{
	struct vino_device *videv = (struct vino_device *)dev;
}

static int vino_mmap(struct video_device *dev, const char *adr,
		     unsigned long size)
{
	struct vino_device *videv = (struct vino_device *)dev;

	return -EINVAL;
}

static int vino_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct vino_device *videv = (struct vino_device *)dev;

	return -EINVAL;
}

static const struct video_device vino_device = {
	.owner		= THIS_MODULE,
	.type		= VID_TYPE_CAPTURE | VID_TYPE_SUBCAPTURE,
	.hardware	= VID_HARDWARE_VINO,
	.name		= "VINO",
	.open		= vino_open,
	.close		= vino_close,
	.ioctl		= vino_ioctl,
	.mmap		= vino_mmap,
};

static int __init vino_init(void)
{
	unsigned long rev;
	int i, ret = 0;

	/* VINO is Indy specific beast */
	if (ip22_is_fullhouse())
		return -ENODEV;

	/*
	 * VINO is in the EISA address space, so the sysid register will tell
	 * us if the EISA_PRESENT pin on MC has been pulled low.
	 *
	 * If EISA_PRESENT is not set we definitely don't have a VINO equiped
	 * system.
	 */
	if (!(sgimc->systemid & SGIMC_SYSID_EPRESENT)) {
		printk(KERN_ERR "VINO not found\n");
		return -ENODEV;
	}

	vino = (struct sgi_vino *)ioremap(VINO_BASE, sizeof(struct sgi_vino));
	if (!vino)
		return -EIO;

	/* Okay, once we know that VINO is present we'll read its revision
	 * safe way. One never knows... */
	if (get_dbe(rev, &(vino->rev_id))) {
		printk(KERN_ERR "VINO: failed to read revision register\n");
		ret = -ENODEV;
		goto out_unmap;
	}
	if (VINO_ID_VALUE(rev) != VINO_CHIP_ID) {
		printk(KERN_ERR "VINO is not VINO (Rev/ID: 0x%04lx)\n", rev);
		ret = -ENODEV;
		goto out_unmap;
	}
	printk(KERN_INFO "VINO Rev: 0x%02lx\n", VINO_REV_NUM(rev));

	Vino = (struct vino_video *)
		kmalloc(sizeof(struct vino_video), GFP_KERNEL);
	if (!Vino) {
		ret = -ENOMEM;
		goto out_unmap;
	}

	Vino->dummy_page = get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!Vino->dummy_page) {
		ret = -ENOMEM;
		goto out_free_vino;
	}
	for (i = 0; i < 4; i++)
		Vino->dummy_buf[i] = PHYSADDR(Vino->dummy_page);

	vino->control = 0;
	/* prevent VINO from throwing spurious interrupts */
	vino->a.next_4_desc = PHYSADDR(Vino->dummy_buf);
	vino->b.next_4_desc = PHYSADDR(Vino->dummy_buf);
	udelay(5);
	vino->intr_status = 0;
        /* set threshold level */
        vino->a.fifo_thres = threshold_a;
	vino->b.fifo_thres = threshold_b;

	init_MUTEX(&Vino->input_lock);

	if (request_irq(SGI_VINO_IRQ, vino_interrupt, 0, vinostr, NULL)) {
		printk(KERN_ERR "VINO: irq%02d registration failed\n",
		       SGI_VINO_IRQ);
		ret = -EAGAIN;
		goto out_free_page;
	}

	ret = vino_i2c_add_bus();
	if (ret) {
		printk(KERN_ERR "VINO: I2C bus registration failed\n");
		goto out_free_irq;
	}

	if (video_register_device(&Vino->chA.vdev, VFL_TYPE_GRABBER, -1) < 0) {
		printk("%s, chnl %d: device registration failed.\n",
			Vino->chA.vdev.name, Vino->chA.chan);
		ret = -EINVAL;
		goto out_i2c_del_bus;
	}
	if (video_register_device(&Vino->chB.vdev, VFL_TYPE_GRABBER, -1) < 0) {
		printk("%s, chnl %d: device registration failed.\n",
			Vino->chB.vdev.name, Vino->chB.chan);
		ret = -EINVAL;
		goto out_unregister_vdev;
	}

	return 0;

out_unregister_vdev:
	video_unregister_device(&Vino->chA.vdev);
out_i2c_del_bus:
	vino_i2c_del_bus();
out_free_irq:
	free_irq(SGI_VINO_IRQ, NULL);
out_free_page:
	free_page(Vino->dummy_page);
out_free_vino:
	kfree(Vino);
out_unmap:
	iounmap(vino);

	return ret;
}

static void __exit vino_exit(void)
{
	video_unregister_device(&Vino->chA.vdev);
	video_unregister_device(&Vino->chB.vdev);
	vino_i2c_del_bus();
	free_irq(SGI_VINO_IRQ, NULL);
	free_page(Vino->dummy_page);
	kfree(Vino);
	iounmap(vino);
}

module_init(vino_init);
module_exit(vino_exit);

MODULE_DESCRIPTION("Video4Linux driver for SGI Indy VINO (IndyCam)");
MODULE_LICENSE("GPL");
