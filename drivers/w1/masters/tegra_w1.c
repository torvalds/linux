/*
 * drivers/w1/masters/tegra-w1.c
 *
 * W1 master driver for internal OWR controllers in NVIDIA Tegra SoCs.
 *
 * Copyright (C) 2010 Motorola, Inc
 * Author: Andrei Warkentin <andreiw@motorola.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <mach/w1.h>

#include "../w1.h"
#include "../w1_int.h"
#include "../w1_log.h"

#define DRIVER_NAME "tegra_w1"

/* OWR_CONTROL_0 is the main control register, and should be configured
   last after configuring all other settings. */
#define OWR_CONTROL          (0x0)
#define OC_RD_BIT            (1 << 31)
#define OC_WR0_BIT           (1 << 30)
#define OC_RD_SCLK_SHIFT     (23)
#define OC_RD_SCLK_MASK      (0xF)
#define OC_P_SCLK_SHIFT      (15)
#define OC_P_SCLK_MASK       (0xFF)
#define OC_BIT_XMODE         (1 << 2)
#define OC_GO                (1 << 0)

/* OWR_WR_RD_TCTL_0 controls read/write timings. */
#define OWR_WR_RD_TCTL       (0xc)
#define ORWT_TSU_SHIFT       (28)
#define ORWT_TSU_MASK        (0x3)
#define ORWT_TRELEASE_SHIFT  (22)
#define ORWT_TRELEASE_MASK   (0x3F)
#define ORWT_TRDV_SHIFT      (18)
#define ORWT_TRDV_MASK       (0xF)
#define ORWT_TLOW0_SHIFT     (11)
#define ORWT_TLOW0_MASK      (0x7F)
#define ORWT_TLOW1_SHIFT     (7)
#define ORWT_TLOW1_MASK      (0xF)
#define ORWT_TSLOT_SHIFT     (0)
#define ORWT_TSLOT_MASK      (0x7F)

/* OWR_RST_PRES_TCTL_0 controls reset presence timings. */
#define OWR_RST_PRES_TCTL    (0x10)
#define ORPT_TPDL_SHIFT      (24)
#define ORPT_TPDL_MASK       (0xFF)
#define ORPT_TPDH_SHIFT      (18)
#define ORPT_TPDH_MASK       (0x3F)
#define ORPT_TRSTL_SHIFT     (9)
#define ORPT_TRSTL_MASK      (0x1FF)
#define ORPT_TRSTH_SHIFT     (0)
#define ORPT_TRSTH_MASK      (0x1FF)

/* OWR_INTR_MASK_0 stores the masks for the interrupts. */
#define OWR_INTR_MASK         (0x24)
#define OI_BIT_XFER_DONE      (1 << 13)
#define OI_PRESENCE_DONE      (1 << 5)
#define OI_PRESENCE_ERR       (1 << 0)

/* OWR_INTR_STATUS_0 is the interrupt status register. */
#define OWR_INTR_STATUS       (0x28)

/* OWR_STATUS_0 is the status register. */
#define OWR_STATUS            (0x34)
#define OS_READ_BIT_SHIFT     (23)
#define OS_RDY                (1 << 0)

/* Transfer_completion wait time. */
#define BIT_XFER_COMPLETION_TIMEOUT_MSEC (5000)

/* Errors in the interrupt status register for bit
   transfers. */
#define BIT_XFER_ERRORS (OI_PRESENCE_ERR)

/* OWR requires 1MHz clock. This value is in Herz. */
#define OWR_CLOCK (1000000)

#define W1_ERR(format, ...) \
	printk(KERN_ERR "(%s: line %d) " format, \
	__func__, __LINE__, ## __VA_ARGS__)

struct tegra_device {
	bool ready;
	struct w1_bus_master bus_master;
	struct clk *clk;
	void __iomem *ioaddr;
	struct mutex mutex;
	spinlock_t spinlock;
	struct completion *transfer_completion;
	unsigned long intr_status;
	struct tegra_w1_timings *timings;
};

/* If debug_print & DEBUG_PRESENCE, print whether slaves detected
   or not in reset_bus. */
#define DEBUG_PRESENCE (0x1)

/* If debug_print & DEBUG_TIMEOUT, print whether timeouts on waiting
   for device interrupts occurs. */
#define DEBUG_TIMEOUT (0x2)

static uint32_t debug_print;
module_param_named(debug, debug_print, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debugging output commands:\n"
		 "\tbit 0 - log reset_bus presence detects\n"
		 "\tbit 1 - log interrupt timeouts\n");

/* Reads the OWR register specified by base offset in 'reg'. */
static inline unsigned long w1_readl(struct tegra_device *dev,
				     unsigned long reg)
{
	return readl(dev->ioaddr + reg);
}

/* Writes 'val' into the OWR registers specified by base offset in 'reg'. */
static inline void w1_writel(struct tegra_device *dev, unsigned long val,
			     unsigned long reg)
{
	writel(val, dev->ioaddr + reg);
}

/* Sets interrupt mask the device. */
static inline void w1_imask(struct tegra_device *dev, unsigned long mask)
{
	w1_writel(dev, mask, OWR_INTR_MASK);
}

/* Waits for completion of a bit transfer, checks intr_status against
   BIT_XFER_ERRORS and an additional provided bit mask. */
static inline int w1_wait(struct tegra_device *dev, unsigned long mask)
{
	int ret;
	unsigned long irq_flags;
	unsigned long intr_status;

	ret = wait_for_completion_timeout(dev->transfer_completion,
		msecs_to_jiffies(BIT_XFER_COMPLETION_TIMEOUT_MSEC));

	if (unlikely(!ret)) {
		if (debug_print & DEBUG_TIMEOUT)
			W1_ERR("timeout\n");
		return -ETIME;
	}

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	intr_status = dev->intr_status;
	dev->intr_status = 0;
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	if (unlikely(intr_status & BIT_XFER_ERRORS ||
		     !(intr_status & mask)))
		return -EIO;
	return 0;
}

/* Programs timing registers, and puts the device into a known state.
   Interrupts are safe to enable past this point. */
static int w1_setup(struct tegra_device *dev)
{
	unsigned long value;
	clk_enable(dev->clk);

	value =
		((dev->timings->tslot & ORWT_TSLOT_MASK) << ORWT_TSLOT_SHIFT) |
		((dev->timings->tlow1 & ORWT_TLOW1_MASK) << ORWT_TLOW1_SHIFT) |
		((dev->timings->tlow0 & ORWT_TLOW0_MASK) << ORWT_TLOW0_SHIFT) |
		((dev->timings->trdv & ORWT_TRDV_MASK) << ORWT_TRDV_SHIFT) |
		((dev->timings->trelease & ORWT_TRELEASE_MASK) <<
		 ORWT_TRELEASE_SHIFT) |
		((dev->timings->tsu & ORWT_TSU_MASK) << ORWT_TSU_SHIFT);
	w1_writel(dev, value, OWR_WR_RD_TCTL);

	value =
		((dev->timings->trsth & ORPT_TRSTH_MASK) << ORPT_TRSTH_SHIFT) |
		((dev->timings->trstl & ORPT_TRSTL_MASK) << ORPT_TRSTL_SHIFT) |
		((dev->timings->tpdh & ORPT_TPDH_MASK) << ORPT_TPDH_SHIFT) |
		((dev->timings->tpdl & ORPT_TPDL_MASK) << ORPT_TPDL_SHIFT);
	w1_writel(dev, value, OWR_RST_PRES_TCTL);

	/* Clear interrupt status/mask registers in case
	   anything was set in it. */
	w1_imask(dev, 0);
	w1_writel(dev, 0xFFFFFFFF, OWR_INTR_STATUS);
	clk_disable(dev->clk);
	return 0;
}

/* Interrupt handler for OWR communication. */
static irqreturn_t tegra_w1_irq(int irq, void *cookie)
{
	unsigned long irq_flags;
	unsigned long status;
	struct tegra_device *dev = cookie;

	status = w1_readl(dev, OWR_INTR_STATUS);
	if (unlikely(!status)) {

		/* Not for me if no status bits are set. */
		return IRQ_NONE;
	}

	spin_lock_irqsave(&dev->spinlock, irq_flags);

	if (likely(dev->transfer_completion)) {
		dev->intr_status = status;
		w1_writel(dev, status, OWR_INTR_STATUS);
		complete(dev->transfer_completion);
	} else {
		W1_ERR("spurious interrupt, status = 0x%lx\n", status);
	}

	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
	return IRQ_HANDLED;
}

/* Perform a write-0 cycle if bit == 0, otherwise
   perform a read cycle. */
static u8 tegra_w1_touch_bit(void *data, u8 bit)
{
	int rc;
	u8 return_bit;
	unsigned long control;
	DECLARE_COMPLETION_ONSTACK(touch_done);
	struct tegra_device *dev = (struct tegra_device *) data;

	return_bit = 0;
	mutex_lock(&dev->mutex);
	if (!dev->ready)
		goto done;

	clk_enable(dev->clk);
	w1_imask(dev, OI_BIT_XFER_DONE);
	dev->transfer_completion = &touch_done;
	control =
		((dev->timings->rdsclk & OC_RD_SCLK_MASK) << OC_RD_SCLK_SHIFT) |
		((dev->timings->psclk & OC_P_SCLK_MASK) << OC_P_SCLK_SHIFT) |
		OC_BIT_XMODE;

	/* Read bit (well, writes a 1 to the bus as well). */
	if (bit) {
		w1_writel(dev, control | OC_RD_BIT, OWR_CONTROL);
		rc = w1_wait(dev, OI_BIT_XFER_DONE);

		if (rc) {
			W1_ERR("write-1/read failed\n");
			goto done;
		}

		return_bit =
		    (w1_readl(dev, OWR_STATUS) >> OS_READ_BIT_SHIFT) & 1;

	}

	/* Write 0. */
	else {
		w1_writel(dev, control | OC_WR0_BIT, OWR_CONTROL);
		rc = w1_wait(dev, OI_BIT_XFER_DONE);
		if (rc) {
			W1_ERR("write-0 failed\n");
			goto done;
		}
	}

done:

	w1_imask(dev, 0);
	dev->transfer_completion = NULL;
	clk_disable(dev->clk);
	mutex_unlock(&dev->mutex);
	return return_bit;
}

/* Performs a bus reset cycle, and returns 0 if slaves present. */
static u8 tegra_w1_reset_bus(void *data)
{
	int rc;
	int presence;
	unsigned long value;
	DECLARE_COMPLETION_ONSTACK(reset_done);
	struct tegra_device *dev = (struct tegra_device *) data;

	presence = 1;
	mutex_lock(&dev->mutex);
	if (!dev->ready)
		goto done;

	clk_enable(dev->clk);
	w1_imask(dev, OI_PRESENCE_DONE);
	dev->transfer_completion = &reset_done;
	value =
	    ((dev->timings->rdsclk & OC_RD_SCLK_MASK) << OC_RD_SCLK_SHIFT) |
	    ((dev->timings->psclk & OC_P_SCLK_MASK) << OC_P_SCLK_SHIFT) |
	    OC_BIT_XMODE | OC_GO;
	w1_writel(dev, value, OWR_CONTROL);

	rc = w1_wait(dev, OI_PRESENCE_DONE);
	if (rc)
		goto done;

	presence = 0;
done:

	if (debug_print & DEBUG_PRESENCE) {
		if (presence)
			W1_ERR("no slaves present\n");
		else
			W1_ERR("slaves present\n");
	}

	w1_imask(dev, 0);
	dev->transfer_completion = NULL;
	clk_disable(dev->clk);
	mutex_unlock(&dev->mutex);
	return presence;
}

static int tegra_w1_probe(struct platform_device *pdev)
{
	int rc;
	int irq;
	struct resource *res;
	struct tegra_device *dev;
	struct tegra_w1_platform_data *plat = pdev->dev.platform_data;

	printk(KERN_INFO "Driver for Tegra SoC 1-wire controller\n");

	if (plat == NULL || plat->timings == NULL)
		return -ENXIO;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL)
		return -ENODEV;

	irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	dev = kzalloc(sizeof(struct tegra_device), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, dev);
	dev->clk = clk_get(&pdev->dev, plat->clk_id);
	if (IS_ERR(dev->clk)) {
		rc = PTR_ERR(dev->clk);
		goto cleanup_alloc;
	}

	/* OWR requires 1MHz clock. */
	rc = clk_set_rate(dev->clk, OWR_CLOCK);
	if (rc)
		goto cleanup_clock;

	if (!request_mem_region
	    (res->start, res->end - res->start + 1, dev_name(&pdev->dev))) {
		rc = -EBUSY;
		goto cleanup_clock;
	}

	dev->ioaddr = ioremap(res->start, res->end - res->start + 1);
	if (!dev->ioaddr) {
		rc = -ENOMEM;
		goto cleanup_reqmem;
	}

	dev->timings = plat->timings;
	dev->bus_master.data = dev;
	dev->bus_master.touch_bit = tegra_w1_touch_bit;
	dev->bus_master.reset_bus = tegra_w1_reset_bus;

	spin_lock_init(&dev->spinlock);
	mutex_init(&dev->mutex);

	/* Program device into known state. */
	w1_setup(dev);

	rc = request_irq(irq, tegra_w1_irq, IRQF_SHARED, DRIVER_NAME, dev);
	if (rc)
		goto cleanup_ioremap;

	rc = w1_add_master_device(&dev->bus_master);
	if (rc)
		goto cleanup_irq;

	dev->ready = true;
	return 0;

cleanup_irq:
	free_irq(irq, dev);
cleanup_ioremap:
	iounmap(dev->ioaddr);
cleanup_reqmem:
	release_mem_region(res->start,
			   res->end - res->start + 1);
cleanup_clock:
	clk_put(dev->clk);
cleanup_alloc:
	platform_set_drvdata(pdev, NULL);
	kfree(dev);
	return rc;
}

static int tegra_w1_remove(struct platform_device *pdev)
{
	struct resource *res;
	struct tegra_device *dev = platform_get_drvdata(pdev);

	mutex_lock(&dev->mutex);
	dev->ready = false;
	mutex_unlock(&dev->mutex);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	free_irq(res->start, dev);
	iounmap(dev->ioaddr);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, res->end - res->start + 1);
	clk_put(dev->clk);
	platform_set_drvdata(pdev, NULL);
	kfree(dev);
	return 0;
}

static int tegra_w1_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int tegra_w1_resume(struct platform_device *pdev)
{
	struct tegra_device *dev = platform_get_drvdata(pdev);

	/* TODO: Is this necessary? I would assume yes. */
	w1_setup(dev);
	return 0;
}

static struct platform_driver tegra_w1_driver = {
	.probe = tegra_w1_probe,
	.remove = tegra_w1_remove,
	.suspend = tegra_w1_suspend,
	.resume = tegra_w1_resume,
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
	},
};

static int __init tegra_w1_init(void)
{
	return platform_driver_register(&tegra_w1_driver);
}

static void __exit tegra_w1_exit(void)
{
	platform_driver_unregister(&tegra_w1_driver);
}

module_init(tegra_w1_init);
module_exit(tegra_w1_exit);

MODULE_DESCRIPTION("Tegra W1 master driver");
MODULE_AUTHOR("Andrei Warkentin <andreiw@motorola.com>");
MODULE_LICENSE("GPL");
