/*****************************************************************************
 *
 *     Author: Xilinx, Inc.
 *
 *     This program is free software; you can redistribute it and/or modify it
 *     under the terms of the GNU General Public License as published by the
 *     Free Software Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
 *     AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
 *     SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
 *     OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
 *     APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
 *     THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
 *     AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
 *     FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
 *     WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
 *     IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
 *     REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
 *     INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE.
 *
 *     (c) Copyright 2002 Xilinx Inc., Systems Engineering Group
 *     (c) Copyright 2004 Xilinx Inc., Systems Engineering Group
 *     (c) Copyright 2007-2008 Xilinx Inc.
 *     All rights reserved.
 *
 *     You should have received a copy of the GNU General Public License along
 *     with this program; if not, write to the Free Software Foundation, Inc.,
 *     675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

/*
 * This is the code behind /dev/icap* -- it allows a user-space
 * application to use the Xilinx ICAP subsystem.
 *
 * The following operations are possible:
 *
 * open         open the port and initialize for access.
 * release      release port
 * write        Write a bitstream to the configuration processor.
 * read         Read a data stream from the configuration processor.
 *
 * After being opened, the port is initialized and accessed to avoid a
 * corrupted first read which may occur with some hardware.  The port
 * is left in a desynched state, requiring that a synch sequence be
 * transmitted before any valid configuration data.  A user will have
 * exclusive access to the device while it remains open, and the state
 * of the ICAP cannot be guaranteed after the device is closed.  Note
 * that a complete reset of the core and the state of the ICAP cannot
 * be performed on many versions of the cores, hence users of this
 * device should avoid making inconsistent accesses to the device.  In
 * particular, accessing the read interface, without first generating
 * a write containing a readback packet can leave the ICAP in an
 * inaccessible state.
 *
 * Note that in order to use the read interface, it is first necessary
 * to write a request packet to the write interface.  i.e., it is not
 * possible to simply readback the bitstream (or any configuration
 * bits) from a device without specifically requesting them first.
 * The code to craft such packets is intended to be part of the
 * user-space application code that uses this device.  The simplest
 * way to use this interface is simply:
 *
 * cp foo.bit /dev/icap0
 *
 * Note that unless foo.bit is an appropriately constructed partial
 * bitstream, this has a high likelihood of overwriting the design
 * currently programmed in the FPGA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/sysctl.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <linux/uaccess.h>

#ifdef CONFIG_OF
/* For open firmware. */
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#endif

#include "xilinx_hwicap.h"
#include "buffer_icap.h"
#include "fifo_icap.h"

#define DRIVER_NAME "icap"

#define HWICAP_REGS   (0x10000)

#define XHWICAP_MAJOR 259
#define XHWICAP_MINOR 0
#define HWICAP_DEVICES 1

/* An array, which is set to true when the device is registered. */
static DEFINE_MUTEX(hwicap_mutex);
static bool probed_devices[HWICAP_DEVICES];
static struct mutex icap_sem;

static struct class *icap_class;

#define UNIMPLEMENTED 0xFFFF

static const struct config_registers v2_config_registers = {
	.CRC = 0,
	.FAR = 1,
	.FDRI = 2,
	.FDRO = 3,
	.CMD = 4,
	.CTL = 5,
	.MASK = 6,
	.STAT = 7,
	.LOUT = 8,
	.COR = 9,
	.MFWR = 10,
	.FLR = 11,
	.KEY = 12,
	.CBC = 13,
	.IDCODE = 14,
	.AXSS = UNIMPLEMENTED,
	.C0R_1 = UNIMPLEMENTED,
	.CSOB = UNIMPLEMENTED,
	.WBSTAR = UNIMPLEMENTED,
	.TIMER = UNIMPLEMENTED,
	.BOOTSTS = UNIMPLEMENTED,
	.CTL_1 = UNIMPLEMENTED,
};

static const struct config_registers v4_config_registers = {
	.CRC = 0,
	.FAR = 1,
	.FDRI = 2,
	.FDRO = 3,
	.CMD = 4,
	.CTL = 5,
	.MASK = 6,
	.STAT = 7,
	.LOUT = 8,
	.COR = 9,
	.MFWR = 10,
	.FLR = UNIMPLEMENTED,
	.KEY = UNIMPLEMENTED,
	.CBC = 11,
	.IDCODE = 12,
	.AXSS = 13,
	.C0R_1 = UNIMPLEMENTED,
	.CSOB = UNIMPLEMENTED,
	.WBSTAR = UNIMPLEMENTED,
	.TIMER = UNIMPLEMENTED,
	.BOOTSTS = UNIMPLEMENTED,
	.CTL_1 = UNIMPLEMENTED,
};

static const struct config_registers v5_config_registers = {
	.CRC = 0,
	.FAR = 1,
	.FDRI = 2,
	.FDRO = 3,
	.CMD = 4,
	.CTL = 5,
	.MASK = 6,
	.STAT = 7,
	.LOUT = 8,
	.COR = 9,
	.MFWR = 10,
	.FLR = UNIMPLEMENTED,
	.KEY = UNIMPLEMENTED,
	.CBC = 11,
	.IDCODE = 12,
	.AXSS = 13,
	.C0R_1 = 14,
	.CSOB = 15,
	.WBSTAR = 16,
	.TIMER = 17,
	.BOOTSTS = 18,
	.CTL_1 = 19,
};

static const struct config_registers v6_config_registers = {
	.CRC = 0,
	.FAR = 1,
	.FDRI = 2,
	.FDRO = 3,
	.CMD = 4,
	.CTL = 5,
	.MASK = 6,
	.STAT = 7,
	.LOUT = 8,
	.COR = 9,
	.MFWR = 10,
	.FLR = UNIMPLEMENTED,
	.KEY = UNIMPLEMENTED,
	.CBC = 11,
	.IDCODE = 12,
	.AXSS = 13,
	.C0R_1 = 14,
	.CSOB = 15,
	.WBSTAR = 16,
	.TIMER = 17,
	.BOOTSTS = 22,
	.CTL_1 = 24,
};

/**
 * hwicap_command_desync - Send a DESYNC command to the ICAP port.
 * @drvdata: a pointer to the drvdata.
 *
 * This command desynchronizes the ICAP After this command, a
 * bitstream containing a NULL packet, followed by a SYNCH packet is
 * required before the ICAP will recognize commands.
 */
static int hwicap_command_desync(struct hwicap_drvdata *drvdata)
{
	u32 buffer[4];
	u32 index = 0;

	/*
	 * Create the data to be written to the ICAP.
	 */
	buffer[index++] = hwicap_type_1_write(drvdata->config_regs->CMD) | 1;
	buffer[index++] = XHI_CMD_DESYNCH;
	buffer[index++] = XHI_NOOP_PACKET;
	buffer[index++] = XHI_NOOP_PACKET;

	/*
	 * Write the data to the FIFO and intiate the transfer of data present
	 * in the FIFO to the ICAP device.
	 */
	return drvdata->config->set_configuration(drvdata,
			&buffer[0], index);
}

/**
 * hwicap_get_configuration_register - Query a configuration register.
 * @drvdata: a pointer to the drvdata.
 * @reg: a constant which represents the configuration
 *		register value to be returned.
 * 		Examples:  XHI_IDCODE, XHI_FLR.
 * @reg_data: returns the value of the register.
 *
 * Sends a query packet to the ICAP and then receives the response.
 * The icap is left in Synched state.
 */
static int hwicap_get_configuration_register(struct hwicap_drvdata *drvdata,
		u32 reg, u32 *reg_data)
{
	int status;
	u32 buffer[6];
	u32 index = 0;

	/*
	 * Create the data to be written to the ICAP.
	 */
	buffer[index++] = XHI_DUMMY_PACKET;
	buffer[index++] = XHI_NOOP_PACKET;
	buffer[index++] = XHI_SYNC_PACKET;
	buffer[index++] = XHI_NOOP_PACKET;
	buffer[index++] = XHI_NOOP_PACKET;

	/*
	 * Write the data to the FIFO and initiate the transfer of data present
	 * in the FIFO to the ICAP device.
	 */
	status = drvdata->config->set_configuration(drvdata,
						    &buffer[0], index);
	if (status)
		return status;

	/* If the syncword was not found, then we need to start over. */
	status = drvdata->config->get_status(drvdata);
	if ((status & XHI_SR_DALIGN_MASK) != XHI_SR_DALIGN_MASK)
		return -EIO;

	index = 0;
	buffer[index++] = hwicap_type_1_read(reg) | 1;
	buffer[index++] = XHI_NOOP_PACKET;
	buffer[index++] = XHI_NOOP_PACKET;

	/*
	 * Write the data to the FIFO and intiate the transfer of data present
	 * in the FIFO to the ICAP device.
	 */
	status = drvdata->config->set_configuration(drvdata,
			&buffer[0], index);
	if (status)
		return status;

	/*
	 * Read the configuration register
	 */
	status = drvdata->config->get_configuration(drvdata, reg_data, 1);
	if (status)
		return status;

	return 0;
}

static int hwicap_initialize_hwicap(struct hwicap_drvdata *drvdata)
{
	int status;
	u32 idcode;

	dev_dbg(drvdata->dev, "initializing\n");

	/* Abort any current transaction, to make sure we have the
	 * ICAP in a good state. */
	dev_dbg(drvdata->dev, "Reset...\n");
	drvdata->config->reset(drvdata);

	dev_dbg(drvdata->dev, "Desync...\n");
	status = hwicap_command_desync(drvdata);
	if (status)
		return status;

	/* Attempt to read the IDCODE from ICAP.  This
	 * may not be returned correctly, due to the design of the
	 * hardware.
	 */
	dev_dbg(drvdata->dev, "Reading IDCODE...\n");
	status = hwicap_get_configuration_register(
			drvdata, drvdata->config_regs->IDCODE, &idcode);
	dev_dbg(drvdata->dev, "IDCODE = %x\n", idcode);
	if (status)
		return status;

	dev_dbg(drvdata->dev, "Desync...\n");
	status = hwicap_command_desync(drvdata);
	if (status)
		return status;

	return 0;
}

static ssize_t
hwicap_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct hwicap_drvdata *drvdata = file->private_data;
	ssize_t bytes_to_read = 0;
	u32 *kbuf;
	u32 words;
	u32 bytes_remaining;
	int status;

	status = mutex_lock_interruptible(&drvdata->sem);
	if (status)
		return status;

	if (drvdata->read_buffer_in_use) {
		/* If there are leftover bytes in the buffer, just */
		/* return them and don't try to read more from the */
		/* ICAP device. */
		bytes_to_read =
			(count < drvdata->read_buffer_in_use) ? count :
			drvdata->read_buffer_in_use;

		/* Return the data currently in the read buffer. */
		if (copy_to_user(buf, drvdata->read_buffer, bytes_to_read)) {
			status = -EFAULT;
			goto error;
		}
		drvdata->read_buffer_in_use -= bytes_to_read;
		memmove(drvdata->read_buffer,
		       drvdata->read_buffer + bytes_to_read,
		       4 - bytes_to_read);
	} else {
		/* Get new data from the ICAP, and return was was requested. */
		kbuf = (u32 *) get_zeroed_page(GFP_KERNEL);
		if (!kbuf) {
			status = -ENOMEM;
			goto error;
		}

		/* The ICAP device is only able to read complete */
		/* words.  If a number of bytes that do not correspond */
		/* to complete words is requested, then we read enough */
		/* words to get the required number of bytes, and then */
		/* save the remaining bytes for the next read. */

		/* Determine the number of words to read, rounding up */
		/* if necessary. */
		words = ((count + 3) >> 2);
		bytes_to_read = words << 2;

		if (bytes_to_read > PAGE_SIZE)
			bytes_to_read = PAGE_SIZE;

		/* Ensure we only read a complete number of words. */
		bytes_remaining = bytes_to_read & 3;
		bytes_to_read &= ~3;
		words = bytes_to_read >> 2;

		status = drvdata->config->get_configuration(drvdata,
				kbuf, words);

		/* If we didn't read correctly, then bail out. */
		if (status) {
			free_page((unsigned long)kbuf);
			goto error;
		}

		/* If we fail to return the data to the user, then bail out. */
		if (copy_to_user(buf, kbuf, bytes_to_read)) {
			free_page((unsigned long)kbuf);
			status = -EFAULT;
			goto error;
		}
		memcpy(drvdata->read_buffer,
		       kbuf,
		       bytes_remaining);
		drvdata->read_buffer_in_use = bytes_remaining;
		free_page((unsigned long)kbuf);
	}
	status = bytes_to_read;
 error:
	mutex_unlock(&drvdata->sem);
	return status;
}

static ssize_t
hwicap_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct hwicap_drvdata *drvdata = file->private_data;
	ssize_t written = 0;
	ssize_t left = count;
	u32 *kbuf;
	ssize_t len;
	ssize_t status;

	status = mutex_lock_interruptible(&drvdata->sem);
	if (status)
		return status;

	left += drvdata->write_buffer_in_use;

	/* Only write multiples of 4 bytes. */
	if (left < 4) {
		status = 0;
		goto error;
	}

	kbuf = (u32 *) __get_free_page(GFP_KERNEL);
	if (!kbuf) {
		status = -ENOMEM;
		goto error;
	}

	while (left > 3) {
		/* only write multiples of 4 bytes, so there might */
		/* be as many as 3 bytes left (at the end). */
		len = left;

		if (len > PAGE_SIZE)
			len = PAGE_SIZE;
		len &= ~3;

		if (drvdata->write_buffer_in_use) {
			memcpy(kbuf, drvdata->write_buffer,
					drvdata->write_buffer_in_use);
			if (copy_from_user(
			    (((char *)kbuf) + drvdata->write_buffer_in_use),
			    buf + written,
			    len - (drvdata->write_buffer_in_use))) {
				free_page((unsigned long)kbuf);
				status = -EFAULT;
				goto error;
			}
		} else {
			if (copy_from_user(kbuf, buf + written, len)) {
				free_page((unsigned long)kbuf);
				status = -EFAULT;
				goto error;
			}
		}

		status = drvdata->config->set_configuration(drvdata,
				kbuf, len >> 2);

		if (status) {
			free_page((unsigned long)kbuf);
			status = -EFAULT;
			goto error;
		}
		if (drvdata->write_buffer_in_use) {
			len -= drvdata->write_buffer_in_use;
			left -= drvdata->write_buffer_in_use;
			drvdata->write_buffer_in_use = 0;
		}
		written += len;
		left -= len;
	}
	if ((left > 0) && (left < 4)) {
		if (!copy_from_user(drvdata->write_buffer,
						buf + written, left)) {
			drvdata->write_buffer_in_use = left;
			written += left;
			left = 0;
		}
	}

	free_page((unsigned long)kbuf);
	status = written;
 error:
	mutex_unlock(&drvdata->sem);
	return status;
}

static int hwicap_open(struct inode *inode, struct file *file)
{
	struct hwicap_drvdata *drvdata;
	int status;

	mutex_lock(&hwicap_mutex);
	drvdata = container_of(inode->i_cdev, struct hwicap_drvdata, cdev);

	status = mutex_lock_interruptible(&drvdata->sem);
	if (status)
		goto out;

	if (drvdata->is_open) {
		status = -EBUSY;
		goto error;
	}

	status = hwicap_initialize_hwicap(drvdata);
	if (status) {
		dev_err(drvdata->dev, "Failed to open file");
		goto error;
	}

	file->private_data = drvdata;
	drvdata->write_buffer_in_use = 0;
	drvdata->read_buffer_in_use = 0;
	drvdata->is_open = 1;

 error:
	mutex_unlock(&drvdata->sem);
 out:
	mutex_unlock(&hwicap_mutex);
	return status;
}

static int hwicap_release(struct inode *inode, struct file *file)
{
	struct hwicap_drvdata *drvdata = file->private_data;
	int i;
	int status = 0;

	mutex_lock(&drvdata->sem);

	if (drvdata->write_buffer_in_use) {
		/* Flush write buffer. */
		for (i = drvdata->write_buffer_in_use; i < 4; i++)
			drvdata->write_buffer[i] = 0;

		status = drvdata->config->set_configuration(drvdata,
				(u32 *) drvdata->write_buffer, 1);
		if (status)
			goto error;
	}

	status = hwicap_command_desync(drvdata);
	if (status)
		goto error;

 error:
	drvdata->is_open = 0;
	mutex_unlock(&drvdata->sem);
	return status;
}

static const struct file_operations hwicap_fops = {
	.owner = THIS_MODULE,
	.write = hwicap_write,
	.read = hwicap_read,
	.open = hwicap_open,
	.release = hwicap_release,
	.llseek = noop_llseek,
};

static int hwicap_setup(struct device *dev, int id,
		const struct resource *regs_res,
		const struct hwicap_driver_config *config,
		const struct config_registers *config_regs)
{
	dev_t devt;
	struct hwicap_drvdata *drvdata = NULL;
	int retval = 0;

	dev_info(dev, "Xilinx icap port driver\n");

	mutex_lock(&icap_sem);

	if (id < 0) {
		for (id = 0; id < HWICAP_DEVICES; id++)
			if (!probed_devices[id])
				break;
	}
	if (id < 0 || id >= HWICAP_DEVICES) {
		mutex_unlock(&icap_sem);
		dev_err(dev, "%s%i too large\n", DRIVER_NAME, id);
		return -EINVAL;
	}
	if (probed_devices[id]) {
		mutex_unlock(&icap_sem);
		dev_err(dev, "cannot assign to %s%i; it is already in use\n",
			DRIVER_NAME, id);
		return -EBUSY;
	}

	probed_devices[id] = 1;
	mutex_unlock(&icap_sem);

	devt = MKDEV(XHWICAP_MAJOR, XHWICAP_MINOR + id);

	drvdata = kzalloc(sizeof(struct hwicap_drvdata), GFP_KERNEL);
	if (!drvdata) {
		dev_err(dev, "Couldn't allocate device private record\n");
		retval = -ENOMEM;
		goto failed0;
	}
	dev_set_drvdata(dev, (void *)drvdata);

	if (!regs_res) {
		dev_err(dev, "Couldn't get registers resource\n");
		retval = -EFAULT;
		goto failed1;
	}

	drvdata->mem_start = regs_res->start;
	drvdata->mem_end = regs_res->end;
	drvdata->mem_size = resource_size(regs_res);

	if (!request_mem_region(drvdata->mem_start,
					drvdata->mem_size, DRIVER_NAME)) {
		dev_err(dev, "Couldn't lock memory region at %Lx\n",
			(unsigned long long) regs_res->start);
		retval = -EBUSY;
		goto failed1;
	}

	drvdata->devt = devt;
	drvdata->dev = dev;
	drvdata->base_address = ioremap(drvdata->mem_start, drvdata->mem_size);
	if (!drvdata->base_address) {
		dev_err(dev, "ioremap() failed\n");
		retval = -ENOMEM;
		goto failed2;
	}

	drvdata->config = config;
	drvdata->config_regs = config_regs;

	mutex_init(&drvdata->sem);
	drvdata->is_open = 0;

	dev_info(dev, "ioremap %llx to %p with size %llx\n",
		 (unsigned long long) drvdata->mem_start,
		 drvdata->base_address,
		 (unsigned long long) drvdata->mem_size);

	cdev_init(&drvdata->cdev, &hwicap_fops);
	drvdata->cdev.owner = THIS_MODULE;
	retval = cdev_add(&drvdata->cdev, devt, 1);
	if (retval) {
		dev_err(dev, "cdev_add() failed\n");
		goto failed3;
	}

	device_create(icap_class, dev, devt, NULL, "%s%d", DRIVER_NAME, id);
	return 0;		/* success */

 failed3:
	iounmap(drvdata->base_address);

 failed2:
	release_mem_region(regs_res->start, drvdata->mem_size);

 failed1:
	kfree(drvdata);

 failed0:
	mutex_lock(&icap_sem);
	probed_devices[id] = 0;
	mutex_unlock(&icap_sem);

	return retval;
}

static struct hwicap_driver_config buffer_icap_config = {
	.get_configuration = buffer_icap_get_configuration,
	.set_configuration = buffer_icap_set_configuration,
	.get_status = buffer_icap_get_status,
	.reset = buffer_icap_reset,
};

static struct hwicap_driver_config fifo_icap_config = {
	.get_configuration = fifo_icap_get_configuration,
	.set_configuration = fifo_icap_set_configuration,
	.get_status = fifo_icap_get_status,
	.reset = fifo_icap_reset,
};

static int hwicap_remove(struct device *dev)
{
	struct hwicap_drvdata *drvdata;

	drvdata = dev_get_drvdata(dev);

	if (!drvdata)
		return 0;

	device_destroy(icap_class, drvdata->devt);
	cdev_del(&drvdata->cdev);
	iounmap(drvdata->base_address);
	release_mem_region(drvdata->mem_start, drvdata->mem_size);
	kfree(drvdata);

	mutex_lock(&icap_sem);
	probed_devices[MINOR(dev->devt)-XHWICAP_MINOR] = 0;
	mutex_unlock(&icap_sem);
	return 0;		/* success */
}

#ifdef CONFIG_OF
static int hwicap_of_probe(struct platform_device *op,
				     const struct hwicap_driver_config *config)
{
	struct resource res;
	const unsigned int *id;
	const char *family;
	int rc;
	const struct config_registers *regs;


	rc = of_address_to_resource(op->dev.of_node, 0, &res);
	if (rc) {
		dev_err(&op->dev, "invalid address\n");
		return rc;
	}

	id = of_get_property(op->dev.of_node, "port-number", NULL);

	/* It's most likely that we're using V4, if the family is not
	   specified */
	regs = &v4_config_registers;
	family = of_get_property(op->dev.of_node, "xlnx,family", NULL);

	if (family) {
		if (!strcmp(family, "virtex2p")) {
			regs = &v2_config_registers;
		} else if (!strcmp(family, "virtex4")) {
			regs = &v4_config_registers;
		} else if (!strcmp(family, "virtex5")) {
			regs = &v5_config_registers;
		} else if (!strcmp(family, "virtex6")) {
			regs = &v6_config_registers;
		}
	}
	return hwicap_setup(&op->dev, id ? *id : -1, &res, config,
			regs);
}
#else
static inline int hwicap_of_probe(struct platform_device *op,
				  const struct hwicap_driver_config *config)
{
	return -EINVAL;
}
#endif /* CONFIG_OF */

static const struct of_device_id hwicap_of_match[];
static int hwicap_drv_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct resource *res;
	const struct config_registers *regs;
	const char *family;

	match = of_match_device(hwicap_of_match, &pdev->dev);
	if (match)
		return hwicap_of_probe(pdev, match->data);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	/* It's most likely that we're using V4, if the family is not
	   specified */
	regs = &v4_config_registers;
	family = pdev->dev.platform_data;

	if (family) {
		if (!strcmp(family, "virtex2p")) {
			regs = &v2_config_registers;
		} else if (!strcmp(family, "virtex4")) {
			regs = &v4_config_registers;
		} else if (!strcmp(family, "virtex5")) {
			regs = &v5_config_registers;
		} else if (!strcmp(family, "virtex6")) {
			regs = &v6_config_registers;
		}
	}

	return hwicap_setup(&pdev->dev, pdev->id, res,
			&buffer_icap_config, regs);
}

static int hwicap_drv_remove(struct platform_device *pdev)
{
	return hwicap_remove(&pdev->dev);
}

#ifdef CONFIG_OF
/* Match table for device tree binding */
static const struct of_device_id hwicap_of_match[] = {
	{ .compatible = "xlnx,opb-hwicap-1.00.b", .data = &buffer_icap_config},
	{ .compatible = "xlnx,xps-hwicap-1.00.a", .data = &fifo_icap_config},
	{},
};
MODULE_DEVICE_TABLE(of, hwicap_of_match);
#else
#define hwicap_of_match NULL
#endif

static struct platform_driver hwicap_platform_driver = {
	.probe = hwicap_drv_probe,
	.remove = hwicap_drv_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = hwicap_of_match,
	},
};

static int __init hwicap_module_init(void)
{
	dev_t devt;
	int retval;

	icap_class = class_create(THIS_MODULE, "xilinx_config");
	mutex_init(&icap_sem);

	devt = MKDEV(XHWICAP_MAJOR, XHWICAP_MINOR);
	retval = register_chrdev_region(devt,
					HWICAP_DEVICES,
					DRIVER_NAME);
	if (retval < 0)
		return retval;

	retval = platform_driver_register(&hwicap_platform_driver);
	if (retval)
		goto failed;

	return retval;

 failed:
	unregister_chrdev_region(devt, HWICAP_DEVICES);

	return retval;
}

static void __exit hwicap_module_cleanup(void)
{
	dev_t devt = MKDEV(XHWICAP_MAJOR, XHWICAP_MINOR);

	class_destroy(icap_class);

	platform_driver_unregister(&hwicap_platform_driver);

	unregister_chrdev_region(devt, HWICAP_DEVICES);
}

module_init(hwicap_module_init);
module_exit(hwicap_module_cleanup);

MODULE_AUTHOR("Xilinx, Inc; Xilinx Research Labs Group");
MODULE_DESCRIPTION("Xilinx ICAP Port Driver");
MODULE_LICENSE("GPL");
