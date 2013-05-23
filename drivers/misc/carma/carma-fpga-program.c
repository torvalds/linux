/*
 * CARMA Board DATA-FPGA Programmer
 *
 * Copyright (c) 2009-2011 Ira W. Snyder <iws@ovro.caltech.edu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/completion.h>
#include <linux/miscdevice.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/kref.h>
#include <linux/fs.h>
#include <linux/io.h>

#include <media/videobuf-dma-sg.h>

/* MPC8349EMDS specific get_immrbase() */
#include <sysdev/fsl_soc.h>

static const char drv_name[] = "carma-fpga-program";

/*
 * Firmware images are always this exact size
 *
 * 12849552 bytes for a CARMA Digitizer Board (EP2S90 FPGAs)
 * 18662880 bytes for a CARMA Correlator Board (EP2S130 FPGAs)
 */
#define FW_SIZE_EP2S90		12849552
#define FW_SIZE_EP2S130		18662880

struct fpga_dev {
	struct miscdevice miscdev;

	/* Reference count */
	struct kref ref;

	/* Device Registers */
	struct device *dev;
	void __iomem *regs;
	void __iomem *immr;

	/* Freescale DMA Device */
	struct dma_chan *chan;

	/* Interrupts */
	int irq, status;
	struct completion completion;

	/* FPGA Bitfile */
	struct mutex lock;

	struct videobuf_dmabuf vb;
	bool vb_allocated;

	/* max size and written bytes */
	size_t fw_size;
	size_t bytes;
};

/*
 * FPGA Bitfile Helpers
 */

/**
 * fpga_drop_firmware_data() - drop the bitfile image from memory
 * @priv: the driver's private data structure
 *
 * LOCKING: must hold priv->lock
 */
static void fpga_drop_firmware_data(struct fpga_dev *priv)
{
	videobuf_dma_free(&priv->vb);
	priv->vb_allocated = false;
	priv->bytes = 0;
}

/*
 * Private Data Reference Count
 */

static void fpga_dev_remove(struct kref *ref)
{
	struct fpga_dev *priv = container_of(ref, struct fpga_dev, ref);

	/* free any firmware image that was not programmed */
	fpga_drop_firmware_data(priv);

	mutex_destroy(&priv->lock);
	kfree(priv);
}

/*
 * LED Trigger (could be a seperate module)
 */

/*
 * NOTE: this whole thing does have the problem that whenever the led's are
 * NOTE: first set to use the fpga trigger, they could be in the wrong state
 */

DEFINE_LED_TRIGGER(ledtrig_fpga);

static void ledtrig_fpga_programmed(bool enabled)
{
	if (enabled)
		led_trigger_event(ledtrig_fpga, LED_FULL);
	else
		led_trigger_event(ledtrig_fpga, LED_OFF);
}

/*
 * FPGA Register Helpers
 */

/* Register Definitions */
#define FPGA_CONFIG_CONTROL		0x40
#define FPGA_CONFIG_STATUS		0x44
#define FPGA_CONFIG_FIFO_SIZE		0x48
#define FPGA_CONFIG_FIFO_USED		0x4C
#define FPGA_CONFIG_TOTAL_BYTE_COUNT	0x50
#define FPGA_CONFIG_CUR_BYTE_COUNT	0x54

#define FPGA_FIFO_ADDRESS		0x3000

static int fpga_fifo_size(void __iomem *regs)
{
	return ioread32be(regs + FPGA_CONFIG_FIFO_SIZE);
}

#define CFG_STATUS_ERR_MASK	0xfffe

static int fpga_config_error(void __iomem *regs)
{
	return ioread32be(regs + FPGA_CONFIG_STATUS) & CFG_STATUS_ERR_MASK;
}

static int fpga_fifo_empty(void __iomem *regs)
{
	return ioread32be(regs + FPGA_CONFIG_FIFO_USED) == 0;
}

static void fpga_fifo_write(void __iomem *regs, u32 val)
{
	iowrite32be(val, regs + FPGA_FIFO_ADDRESS);
}

static void fpga_set_byte_count(void __iomem *regs, u32 count)
{
	iowrite32be(count, regs + FPGA_CONFIG_TOTAL_BYTE_COUNT);
}

#define CFG_CTL_ENABLE	(1 << 0)
#define CFG_CTL_RESET	(1 << 1)
#define CFG_CTL_DMA	(1 << 2)

static void fpga_programmer_enable(struct fpga_dev *priv, bool dma)
{
	u32 val;

	val = (dma) ? (CFG_CTL_ENABLE | CFG_CTL_DMA) : CFG_CTL_ENABLE;
	iowrite32be(val, priv->regs + FPGA_CONFIG_CONTROL);
}

static void fpga_programmer_disable(struct fpga_dev *priv)
{
	iowrite32be(0x0, priv->regs + FPGA_CONFIG_CONTROL);
}

static void fpga_dump_registers(struct fpga_dev *priv)
{
	u32 control, status, size, used, total, curr;

	/* good status: do nothing */
	if (priv->status == 0)
		return;

	/* Dump all status registers */
	control = ioread32be(priv->regs + FPGA_CONFIG_CONTROL);
	status = ioread32be(priv->regs + FPGA_CONFIG_STATUS);
	size = ioread32be(priv->regs + FPGA_CONFIG_FIFO_SIZE);
	used = ioread32be(priv->regs + FPGA_CONFIG_FIFO_USED);
	total = ioread32be(priv->regs + FPGA_CONFIG_TOTAL_BYTE_COUNT);
	curr = ioread32be(priv->regs + FPGA_CONFIG_CUR_BYTE_COUNT);

	dev_err(priv->dev, "Configuration failed, dumping status registers\n");
	dev_err(priv->dev, "Control:    0x%.8x\n", control);
	dev_err(priv->dev, "Status:     0x%.8x\n", status);
	dev_err(priv->dev, "FIFO Size:  0x%.8x\n", size);
	dev_err(priv->dev, "FIFO Used:  0x%.8x\n", used);
	dev_err(priv->dev, "FIFO Total: 0x%.8x\n", total);
	dev_err(priv->dev, "FIFO Curr:  0x%.8x\n", curr);
}

/*
 * FPGA Power Supply Code
 */

#define CTL_PWR_CONTROL		0x2006
#define CTL_PWR_STATUS		0x200A
#define CTL_PWR_FAIL		0x200B

#define PWR_CONTROL_ENABLE	0x01

#define PWR_STATUS_ERROR_MASK	0x10
#define PWR_STATUS_GOOD		0x0f

/*
 * Determine if the FPGA power is good for all supplies
 */
static bool fpga_power_good(struct fpga_dev *priv)
{
	u8 val;

	val = ioread8(priv->regs + CTL_PWR_STATUS);
	if (val & PWR_STATUS_ERROR_MASK)
		return false;

	return val == PWR_STATUS_GOOD;
}

/*
 * Disable the FPGA power supplies
 */
static void fpga_disable_power_supplies(struct fpga_dev *priv)
{
	unsigned long start;
	u8 val;

	iowrite8(0x0, priv->regs + CTL_PWR_CONTROL);

	/*
	 * Wait 500ms for the power rails to discharge
	 *
	 * Without this delay, the CTL-CPLD state machine can get into a
	 * state where it is waiting for the power-goods to assert, but they
	 * never do. This only happens when enabling and disabling the
	 * power sequencer very rapidly.
	 *
	 * The loop below will also wait for the power goods to de-assert,
	 * but testing has shown that they are always disabled by the time
	 * the sleep completes. However, omitting the sleep and only waiting
	 * for the power-goods to de-assert was not sufficient to ensure
	 * that the power sequencer would not wedge itself.
	 */
	msleep(500);

	start = jiffies;
	while (time_before(jiffies, start + HZ)) {
		val = ioread8(priv->regs + CTL_PWR_STATUS);
		if (!(val & PWR_STATUS_GOOD))
			break;

		usleep_range(5000, 10000);
	}

	val = ioread8(priv->regs + CTL_PWR_STATUS);
	if (val & PWR_STATUS_GOOD) {
		dev_err(priv->dev, "power disable failed: "
				   "power goods: status 0x%.2x\n", val);
	}

	if (val & PWR_STATUS_ERROR_MASK) {
		dev_err(priv->dev, "power disable failed: "
				   "alarm bit set: status 0x%.2x\n", val);
	}
}

/**
 * fpga_enable_power_supplies() - enable the DATA-FPGA power supplies
 * @priv: the driver's private data structure
 *
 * Enable the DATA-FPGA power supplies, waiting up to 1 second for
 * them to enable successfully.
 *
 * Returns 0 on success, -ERRNO otherwise
 */
static int fpga_enable_power_supplies(struct fpga_dev *priv)
{
	unsigned long start = jiffies;

	if (fpga_power_good(priv)) {
		dev_dbg(priv->dev, "power was already good\n");
		return 0;
	}

	iowrite8(PWR_CONTROL_ENABLE, priv->regs + CTL_PWR_CONTROL);
	while (time_before(jiffies, start + HZ)) {
		if (fpga_power_good(priv))
			return 0;

		usleep_range(5000, 10000);
	}

	return fpga_power_good(priv) ? 0 : -ETIMEDOUT;
}

/*
 * Determine if the FPGA power supplies are all enabled
 */
static bool fpga_power_enabled(struct fpga_dev *priv)
{
	u8 val;

	val = ioread8(priv->regs + CTL_PWR_CONTROL);
	if (val & PWR_CONTROL_ENABLE)
		return true;

	return false;
}

/*
 * Determine if the FPGA's are programmed and running correctly
 */
static bool fpga_running(struct fpga_dev *priv)
{
	if (!fpga_power_good(priv))
		return false;

	/* Check the config done bit */
	return ioread32be(priv->regs + FPGA_CONFIG_STATUS) & (1 << 18);
}

/*
 * FPGA Programming Code
 */

/**
 * fpga_program_block() - put a block of data into the programmer's FIFO
 * @priv: the driver's private data structure
 * @buf: the data to program
 * @count: the length of data to program (must be a multiple of 4 bytes)
 *
 * Returns 0 on success, -ERRNO otherwise
 */
static int fpga_program_block(struct fpga_dev *priv, void *buf, size_t count)
{
	u32 *data = buf;
	int size = fpga_fifo_size(priv->regs);
	int i, len;
	unsigned long timeout;

	/* enforce correct data length for the FIFO */
	BUG_ON(count % 4 != 0);

	while (count > 0) {

		/* Get the size of the block to write (maximum is FIFO_SIZE) */
		len = min_t(size_t, count, size);
		timeout = jiffies + HZ / 4;

		/* Write the block */
		for (i = 0; i < len / 4; i++)
			fpga_fifo_write(priv->regs, data[i]);

		/* Update the amounts left */
		count -= len;
		data += len / 4;

		/* Wait for the fifo to empty */
		while (true) {

			if (fpga_fifo_empty(priv->regs)) {
				break;
			} else {
				dev_dbg(priv->dev, "Fifo not empty\n");
				cpu_relax();
			}

			if (fpga_config_error(priv->regs)) {
				dev_err(priv->dev, "Error detected\n");
				return -EIO;
			}

			if (time_after(jiffies, timeout)) {
				dev_err(priv->dev, "Fifo drain timeout\n");
				return -ETIMEDOUT;
			}

			usleep_range(5000, 10000);
		}
	}

	return 0;
}

/**
 * fpga_program_cpu() - program the DATA-FPGA's using the CPU
 * @priv: the driver's private data structure
 *
 * This is useful when the DMA programming method fails. It is possible to
 * wedge the Freescale DMA controller such that the DMA programming method
 * always fails. This method has always succeeded.
 *
 * Returns 0 on success, -ERRNO otherwise
 */
static noinline int fpga_program_cpu(struct fpga_dev *priv)
{
	int ret;

	/* Disable the programmer */
	fpga_programmer_disable(priv);

	/* Set the total byte count */
	fpga_set_byte_count(priv->regs, priv->bytes);
	dev_dbg(priv->dev, "total byte count %u bytes\n", priv->bytes);

	/* Enable the controller for programming */
	fpga_programmer_enable(priv, false);
	dev_dbg(priv->dev, "enabled the controller\n");

	/* Write each chunk of the FPGA bitfile to FPGA programmer */
	ret = fpga_program_block(priv, priv->vb.vaddr, priv->bytes);
	if (ret)
		goto out_disable_controller;

	/* Wait for the interrupt handler to signal that programming finished */
	ret = wait_for_completion_timeout(&priv->completion, 2 * HZ);
	if (!ret) {
		dev_err(priv->dev, "Timed out waiting for completion\n");
		ret = -ETIMEDOUT;
		goto out_disable_controller;
	}

	/* Retrieve the status from the interrupt handler */
	ret = priv->status;

out_disable_controller:
	fpga_programmer_disable(priv);
	return ret;
}

#define FIFO_DMA_ADDRESS	0xf0003000
#define FIFO_MAX_LEN		4096

/**
 * fpga_program_dma() - program the DATA-FPGA's using the DMA engine
 * @priv: the driver's private data structure
 *
 * Program the DATA-FPGA's using the Freescale DMA engine. This requires that
 * the engine is programmed such that the hardware DMA request lines can
 * control the entire DMA transaction. The system controller FPGA then
 * completely offloads the programming from the CPU.
 *
 * Returns 0 on success, -ERRNO otherwise
 */
static noinline int fpga_program_dma(struct fpga_dev *priv)
{
	struct videobuf_dmabuf *vb = &priv->vb;
	struct dma_chan *chan = priv->chan;
	struct dma_async_tx_descriptor *tx;
	size_t num_pages, len, avail = 0;
	struct dma_slave_config config;
	struct scatterlist *sg;
	struct sg_table table;
	dma_cookie_t cookie;
	int ret, i;

	/* Disable the programmer */
	fpga_programmer_disable(priv);

	/* Allocate a scatterlist for the DMA destination */
	num_pages = DIV_ROUND_UP(priv->bytes, FIFO_MAX_LEN);
	ret = sg_alloc_table(&table, num_pages, GFP_KERNEL);
	if (ret) {
		dev_err(priv->dev, "Unable to allocate dst scatterlist\n");
		ret = -ENOMEM;
		goto out_return;
	}

	/*
	 * This is an ugly hack
	 *
	 * We fill in a scatterlist as if it were mapped for DMA. This is
	 * necessary because there exists no better structure for this
	 * inside the kernel code.
	 *
	 * As an added bonus, we can use the DMAEngine API for all of this,
	 * rather than inventing another extremely similar API.
	 */
	avail = priv->bytes;
	for_each_sg(table.sgl, sg, num_pages, i) {
		len = min_t(size_t, avail, FIFO_MAX_LEN);
		sg_dma_address(sg) = FIFO_DMA_ADDRESS;
		sg_dma_len(sg) = len;

		avail -= len;
	}

	/* Map the buffer for DMA */
	ret = videobuf_dma_map(priv->dev, &priv->vb);
	if (ret) {
		dev_err(priv->dev, "Unable to map buffer for DMA\n");
		goto out_free_table;
	}

	/*
	 * Configure the DMA channel to transfer FIFO_SIZE / 2 bytes per
	 * transaction, and then put it under external control
	 */
	memset(&config, 0, sizeof(config));
	config.direction = DMA_MEM_TO_DEV;
	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.dst_maxburst = fpga_fifo_size(priv->regs) / 2 / 4;
	ret = chan->device->device_control(chan, DMA_SLAVE_CONFIG,
					   (unsigned long)&config);
	if (ret) {
		dev_err(priv->dev, "DMA slave configuration failed\n");
		goto out_dma_unmap;
	}

	ret = chan->device->device_control(chan, FSLDMA_EXTERNAL_START, 1);
	if (ret) {
		dev_err(priv->dev, "DMA external control setup failed\n");
		goto out_dma_unmap;
	}

	/* setup and submit the DMA transaction */
	tx = chan->device->device_prep_dma_sg(chan,
					      table.sgl, num_pages,
					      vb->sglist, vb->sglen, 0);
	if (!tx) {
		dev_err(priv->dev, "Unable to prep DMA transaction\n");
		ret = -ENOMEM;
		goto out_dma_unmap;
	}

	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		dev_err(priv->dev, "Unable to submit DMA transaction\n");
		ret = -ENOMEM;
		goto out_dma_unmap;
	}

	dma_async_issue_pending(chan);

	/* Set the total byte count */
	fpga_set_byte_count(priv->regs, priv->bytes);
	dev_dbg(priv->dev, "total byte count %u bytes\n", priv->bytes);

	/* Enable the controller for DMA programming */
	fpga_programmer_enable(priv, true);
	dev_dbg(priv->dev, "enabled the controller\n");

	/* Wait for the interrupt handler to signal that programming finished */
	ret = wait_for_completion_timeout(&priv->completion, 2 * HZ);
	if (!ret) {
		dev_err(priv->dev, "Timed out waiting for completion\n");
		ret = -ETIMEDOUT;
		goto out_disable_controller;
	}

	/* Retrieve the status from the interrupt handler */
	ret = priv->status;

out_disable_controller:
	fpga_programmer_disable(priv);
out_dma_unmap:
	videobuf_dma_unmap(priv->dev, vb);
out_free_table:
	sg_free_table(&table);
out_return:
	return ret;
}

/*
 * Interrupt Handling
 */

static irqreturn_t fpga_irq(int irq, void *dev_id)
{
	struct fpga_dev *priv = dev_id;

	/* Save the status */
	priv->status = fpga_config_error(priv->regs) ? -EIO : 0;
	dev_dbg(priv->dev, "INTERRUPT status %d\n", priv->status);
	fpga_dump_registers(priv);

	/* Disabling the programmer clears the interrupt */
	fpga_programmer_disable(priv);

	/* Notify any waiters */
	complete(&priv->completion);

	return IRQ_HANDLED;
}

/*
 * SYSFS Helpers
 */

/**
 * fpga_do_stop() - deconfigure (reset) the DATA-FPGA's
 * @priv: the driver's private data structure
 *
 * LOCKING: must hold priv->lock
 */
static int fpga_do_stop(struct fpga_dev *priv)
{
	u32 val;

	/* Set the led to unprogrammed */
	ledtrig_fpga_programmed(false);

	/* Pulse the config line to reset the FPGA's */
	val = CFG_CTL_ENABLE | CFG_CTL_RESET;
	iowrite32be(val, priv->regs + FPGA_CONFIG_CONTROL);
	iowrite32be(0x0, priv->regs + FPGA_CONFIG_CONTROL);

	return 0;
}

static noinline int fpga_do_program(struct fpga_dev *priv)
{
	int ret;

	if (priv->bytes != priv->fw_size) {
		dev_err(priv->dev, "Incorrect bitfile size: got %zu bytes, "
				   "should be %zu bytes\n",
				   priv->bytes, priv->fw_size);
		return -EINVAL;
	}

	if (!fpga_power_enabled(priv)) {
		dev_err(priv->dev, "Power not enabled\n");
		return -EINVAL;
	}

	if (!fpga_power_good(priv)) {
		dev_err(priv->dev, "Power not good\n");
		return -EINVAL;
	}

	/* Set the LED to unprogrammed */
	ledtrig_fpga_programmed(false);

	/* Try to program the FPGA's using DMA */
	ret = fpga_program_dma(priv);

	/* If DMA failed or doesn't exist, try with CPU */
	if (ret) {
		dev_warn(priv->dev, "Falling back to CPU programming\n");
		ret = fpga_program_cpu(priv);
	}

	if (ret) {
		dev_err(priv->dev, "Unable to program FPGA's\n");
		return ret;
	}

	/* Drop the firmware bitfile from memory */
	fpga_drop_firmware_data(priv);

	dev_dbg(priv->dev, "FPGA programming successful\n");
	ledtrig_fpga_programmed(true);

	return 0;
}

/*
 * File Operations
 */

static int fpga_open(struct inode *inode, struct file *filp)
{
	/*
	 * The miscdevice layer puts our struct miscdevice into the
	 * filp->private_data field. We use this to find our private
	 * data and then overwrite it with our own private structure.
	 */
	struct fpga_dev *priv = container_of(filp->private_data,
					     struct fpga_dev, miscdev);
	unsigned int nr_pages;
	int ret;

	/* We only allow one process at a time */
	ret = mutex_lock_interruptible(&priv->lock);
	if (ret)
		return ret;

	filp->private_data = priv;
	kref_get(&priv->ref);

	/* Truncation: drop any existing data */
	if (filp->f_flags & O_TRUNC)
		priv->bytes = 0;

	/* Check if we have already allocated a buffer */
	if (priv->vb_allocated)
		return 0;

	/* Allocate a buffer to hold enough data for the bitfile */
	nr_pages = DIV_ROUND_UP(priv->fw_size, PAGE_SIZE);
	ret = videobuf_dma_init_kernel(&priv->vb, DMA_TO_DEVICE, nr_pages);
	if (ret) {
		dev_err(priv->dev, "unable to allocate data buffer\n");
		mutex_unlock(&priv->lock);
		kref_put(&priv->ref, fpga_dev_remove);
		return ret;
	}

	priv->vb_allocated = true;
	return 0;
}

static int fpga_release(struct inode *inode, struct file *filp)
{
	struct fpga_dev *priv = filp->private_data;

	mutex_unlock(&priv->lock);
	kref_put(&priv->ref, fpga_dev_remove);
	return 0;
}

static ssize_t fpga_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *f_pos)
{
	struct fpga_dev *priv = filp->private_data;

	/* FPGA bitfiles have an exact size: disallow anything else */
	if (priv->bytes >= priv->fw_size)
		return -ENOSPC;

	count = min_t(size_t, priv->fw_size - priv->bytes, count);
	if (copy_from_user(priv->vb.vaddr + priv->bytes, buf, count))
		return -EFAULT;

	priv->bytes += count;
	return count;
}

static ssize_t fpga_read(struct file *filp, char __user *buf, size_t count,
			 loff_t *f_pos)
{
	struct fpga_dev *priv = filp->private_data;

	count = min_t(size_t, priv->bytes - *f_pos, count);
	if (copy_to_user(buf, priv->vb.vaddr + *f_pos, count))
		return -EFAULT;

	*f_pos += count;
	return count;
}

static loff_t fpga_llseek(struct file *filp, loff_t offset, int origin)
{
	struct fpga_dev *priv = filp->private_data;
	loff_t newpos;

	/* only read-only opens are allowed to seek */
	if ((filp->f_flags & O_ACCMODE) != O_RDONLY)
		return -EINVAL;

	switch (origin) {
	case SEEK_SET: /* seek relative to the beginning of the file */
		newpos = offset;
		break;
	case SEEK_CUR: /* seek relative to current position in the file */
		newpos = filp->f_pos + offset;
		break;
	case SEEK_END: /* seek relative to the end of the file */
		newpos = priv->fw_size - offset;
		break;
	default:
		return -EINVAL;
	}

	/* check for sanity */
	if (newpos > priv->fw_size)
		return -EINVAL;

	filp->f_pos = newpos;
	return newpos;
}

static const struct file_operations fpga_fops = {
	.open		= fpga_open,
	.release	= fpga_release,
	.write		= fpga_write,
	.read		= fpga_read,
	.llseek		= fpga_llseek,
};

/*
 * Device Attributes
 */

static ssize_t pfail_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct fpga_dev *priv = dev_get_drvdata(dev);
	u8 val;

	val = ioread8(priv->regs + CTL_PWR_FAIL);
	return snprintf(buf, PAGE_SIZE, "0x%.2x\n", val);
}

static ssize_t pgood_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct fpga_dev *priv = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", fpga_power_good(priv));
}

static ssize_t penable_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct fpga_dev *priv = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", fpga_power_enabled(priv));
}

static ssize_t penable_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct fpga_dev *priv = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if (val) {
		ret = fpga_enable_power_supplies(priv);
		if (ret)
			return ret;
	} else {
		fpga_do_stop(priv);
		fpga_disable_power_supplies(priv);
	}

	return count;
}

static ssize_t program_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct fpga_dev *priv = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", fpga_running(priv));
}

static ssize_t program_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct fpga_dev *priv = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	/* We can't have an image writer and be programming simultaneously */
	if (mutex_lock_interruptible(&priv->lock))
		return -ERESTARTSYS;

	/* Program or Reset the FPGA's */
	ret = val ? fpga_do_program(priv) : fpga_do_stop(priv);
	if (ret)
		goto out_unlock;

	/* Success */
	ret = count;

out_unlock:
	mutex_unlock(&priv->lock);
	return ret;
}

static DEVICE_ATTR(power_fail, S_IRUGO, pfail_show, NULL);
static DEVICE_ATTR(power_good, S_IRUGO, pgood_show, NULL);
static DEVICE_ATTR(power_enable, S_IRUGO | S_IWUSR,
		   penable_show, penable_store);

static DEVICE_ATTR(program, S_IRUGO | S_IWUSR,
		   program_show, program_store);

static struct attribute *fpga_attributes[] = {
	&dev_attr_power_fail.attr,
	&dev_attr_power_good.attr,
	&dev_attr_power_enable.attr,
	&dev_attr_program.attr,
	NULL,
};

static const struct attribute_group fpga_attr_group = {
	.attrs = fpga_attributes,
};

/*
 * OpenFirmware Device Subsystem
 */

#define SYS_REG_VERSION		0x00
#define SYS_REG_GEOGRAPHIC	0x10

static bool dma_filter(struct dma_chan *chan, void *data)
{
	/*
	 * DMA Channel #0 is the only acceptable device
	 *
	 * This probably won't survive an unload/load cycle of the Freescale
	 * DMAEngine driver, but that won't be a problem
	 */
	return chan->chan_id == 0 && chan->device->dev_id == 0;
}

static int fpga_of_remove(struct platform_device *op)
{
	struct fpga_dev *priv = platform_get_drvdata(op);
	struct device *this_device = priv->miscdev.this_device;

	sysfs_remove_group(&this_device->kobj, &fpga_attr_group);
	misc_deregister(&priv->miscdev);

	free_irq(priv->irq, priv);
	irq_dispose_mapping(priv->irq);

	/* make sure the power supplies are off */
	fpga_disable_power_supplies(priv);

	/* unmap registers */
	iounmap(priv->immr);
	iounmap(priv->regs);

	dma_release_channel(priv->chan);

	/* drop our reference to the private data structure */
	kref_put(&priv->ref, fpga_dev_remove);
	return 0;
}

/* CTL-CPLD Version Register */
#define CTL_CPLD_VERSION	0x2000

static int fpga_of_probe(struct platform_device *op)
{
	struct device_node *of_node = op->dev.of_node;
	struct device *this_device;
	struct fpga_dev *priv;
	dma_cap_mask_t mask;
	u32 ver;
	int ret;

	/* Allocate private data */
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&op->dev, "Unable to allocate private data\n");
		ret = -ENOMEM;
		goto out_return;
	}

	/* Setup the miscdevice */
	priv->miscdev.minor = MISC_DYNAMIC_MINOR;
	priv->miscdev.name = drv_name;
	priv->miscdev.fops = &fpga_fops;

	kref_init(&priv->ref);

	platform_set_drvdata(op, priv);
	priv->dev = &op->dev;
	mutex_init(&priv->lock);
	init_completion(&priv->completion);
	videobuf_dma_init(&priv->vb);

	dev_set_drvdata(priv->dev, priv);
	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_SG, mask);

	/* Get control of DMA channel #0 */
	priv->chan = dma_request_channel(mask, dma_filter, NULL);
	if (!priv->chan) {
		dev_err(&op->dev, "Unable to acquire DMA channel #0\n");
		ret = -ENODEV;
		goto out_free_priv;
	}

	/* Remap the registers for use */
	priv->regs = of_iomap(of_node, 0);
	if (!priv->regs) {
		dev_err(&op->dev, "Unable to ioremap registers\n");
		ret = -ENOMEM;
		goto out_dma_release_channel;
	}

	/* Remap the IMMR for use */
	priv->immr = ioremap(get_immrbase(), 0x100000);
	if (!priv->immr) {
		dev_err(&op->dev, "Unable to ioremap IMMR\n");
		ret = -ENOMEM;
		goto out_unmap_regs;
	}

	/*
	 * Check that external DMA is configured
	 *
	 * U-Boot does this for us, but we should check it and bail out if
	 * there is a problem. Failing to have this register setup correctly
	 * will cause the DMA controller to transfer a single cacheline
	 * worth of data, then wedge itself.
	 */
	if ((ioread32be(priv->immr + 0x114) & 0xE00) != 0xE00) {
		dev_err(&op->dev, "External DMA control not configured\n");
		ret = -ENODEV;
		goto out_unmap_immr;
	}

	/*
	 * Check the CTL-CPLD version
	 *
	 * This driver uses the CTL-CPLD DATA-FPGA power sequencer, and we
	 * don't want to run on any version of the CTL-CPLD that does not use
	 * a compatible register layout.
	 *
	 * v2: changed register layout, added power sequencer
	 * v3: added glitch filter on the i2c overcurrent/overtemp outputs
	 */
	ver = ioread8(priv->regs + CTL_CPLD_VERSION);
	if (ver != 0x02 && ver != 0x03) {
		dev_err(&op->dev, "CTL-CPLD is not version 0x02 or 0x03!\n");
		ret = -ENODEV;
		goto out_unmap_immr;
	}

	/* Set the exact size that the firmware image should be */
	ver = ioread32be(priv->regs + SYS_REG_VERSION);
	priv->fw_size = (ver & (1 << 18)) ? FW_SIZE_EP2S130 : FW_SIZE_EP2S90;

	/* Find the correct IRQ number */
	priv->irq = irq_of_parse_and_map(of_node, 0);
	if (priv->irq == NO_IRQ) {
		dev_err(&op->dev, "Unable to find IRQ line\n");
		ret = -ENODEV;
		goto out_unmap_immr;
	}

	/* Request the IRQ */
	ret = request_irq(priv->irq, fpga_irq, IRQF_SHARED, drv_name, priv);
	if (ret) {
		dev_err(&op->dev, "Unable to request IRQ %d\n", priv->irq);
		ret = -ENODEV;
		goto out_irq_dispose_mapping;
	}

	/* Reset and stop the FPGA's, just in case */
	fpga_do_stop(priv);

	/* Register the miscdevice */
	ret = misc_register(&priv->miscdev);
	if (ret) {
		dev_err(&op->dev, "Unable to register miscdevice\n");
		goto out_free_irq;
	}

	/* Create the sysfs files */
	this_device = priv->miscdev.this_device;
	dev_set_drvdata(this_device, priv);
	ret = sysfs_create_group(&this_device->kobj, &fpga_attr_group);
	if (ret) {
		dev_err(&op->dev, "Unable to create sysfs files\n");
		goto out_misc_deregister;
	}

	dev_info(priv->dev, "CARMA FPGA Programmer: %s rev%s with %s FPGAs\n",
			(ver & (1 << 17)) ? "Correlator" : "Digitizer",
			(ver & (1 << 16)) ? "B" : "A",
			(ver & (1 << 18)) ? "EP2S130" : "EP2S90");

	return 0;

out_misc_deregister:
	misc_deregister(&priv->miscdev);
out_free_irq:
	free_irq(priv->irq, priv);
out_irq_dispose_mapping:
	irq_dispose_mapping(priv->irq);
out_unmap_immr:
	iounmap(priv->immr);
out_unmap_regs:
	iounmap(priv->regs);
out_dma_release_channel:
	dma_release_channel(priv->chan);
out_free_priv:
	kref_put(&priv->ref, fpga_dev_remove);
out_return:
	return ret;
}

static struct of_device_id fpga_of_match[] = {
	{ .compatible = "carma,fpga-programmer", },
	{},
};

static struct platform_driver fpga_of_driver = {
	.probe		= fpga_of_probe,
	.remove		= fpga_of_remove,
	.driver		= {
		.name		= drv_name,
		.of_match_table	= fpga_of_match,
		.owner		= THIS_MODULE,
	},
};

/*
 * Module Init / Exit
 */

static int __init fpga_init(void)
{
	led_trigger_register_simple("fpga", &ledtrig_fpga);
	return platform_driver_register(&fpga_of_driver);
}

static void __exit fpga_exit(void)
{
	platform_driver_unregister(&fpga_of_driver);
	led_trigger_unregister_simple(ledtrig_fpga);
}

MODULE_AUTHOR("Ira W. Snyder <iws@ovro.caltech.edu>");
MODULE_DESCRIPTION("CARMA Board DATA-FPGA Programmer");
MODULE_LICENSE("GPL");

module_init(fpga_init);
module_exit(fpga_exit);
