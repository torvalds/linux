// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010-2013 Bluecherry, LLC <https://www.bluecherrydvr.com>
 *
 * Original author:
 * Ben Collins <bcollins@ubuntu.com>
 *
 * Additional work by:
 * John Brooks <john.brooks@bluecherry.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/ktime.h>
#include <linux/slab.h>

#include "solo6x10.h"
#include "solo6x10-tw28.h"

MODULE_DESCRIPTION("Softlogic 6x10 MPEG4/H.264/G.723 CODEC V4L2/ALSA Driver");
MODULE_AUTHOR("Bluecherry <maintainers@bluecherrydvr.com>");
MODULE_VERSION(SOLO6X10_VERSION);
MODULE_LICENSE("GPL");

static unsigned video_nr = -1;
module_param(video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX start number, -1 is autodetect (default)");

static int full_eeprom; /* default is only top 64B */
module_param(full_eeprom, uint, 0644);
MODULE_PARM_DESC(full_eeprom, "Allow access to full 128B EEPROM (dangerous)");


static void solo_set_time(struct solo_dev *solo_dev)
{
	struct timespec64 ts;

	ktime_get_ts64(&ts);

	/* no overflow because we use monotonic timestamps */
	solo_reg_write(solo_dev, SOLO_TIMER_SEC, (u32)ts.tv_sec);
	solo_reg_write(solo_dev, SOLO_TIMER_USEC, (u32)ts.tv_nsec / NSEC_PER_USEC);
}

static void solo_timer_sync(struct solo_dev *solo_dev)
{
	u32 sec, usec;
	struct timespec64 ts;
	long diff;

	if (solo_dev->type != SOLO_DEV_6110)
		return;

	if (++solo_dev->time_sync < 60)
		return;

	solo_dev->time_sync = 0;

	sec = solo_reg_read(solo_dev, SOLO_TIMER_SEC);
	usec = solo_reg_read(solo_dev, SOLO_TIMER_USEC);

	ktime_get_ts64(&ts);

	diff = (s32)ts.tv_sec - (s32)sec;
	diff = (diff * 1000000)
		+ ((s32)(ts.tv_nsec / NSEC_PER_USEC) - (s32)usec);

	if (diff > 1000 || diff < -1000) {
		solo_set_time(solo_dev);
	} else if (diff) {
		long usec_lsb = solo_dev->usec_lsb;

		usec_lsb -= diff / 4;
		if (usec_lsb < 0)
			usec_lsb = 0;
		else if (usec_lsb > 255)
			usec_lsb = 255;

		solo_dev->usec_lsb = usec_lsb;
		solo_reg_write(solo_dev, SOLO_TIMER_USEC_LSB,
			       solo_dev->usec_lsb);
	}
}

static irqreturn_t solo_isr(int irq, void *data)
{
	struct solo_dev *solo_dev = data;
	u32 status;
	int i;

	status = solo_reg_read(solo_dev, SOLO_IRQ_STAT);
	if (!status)
		return IRQ_NONE;

	/* Acknowledge all interrupts immediately */
	solo_reg_write(solo_dev, SOLO_IRQ_STAT, status);

	if (status & SOLO_IRQ_PCI_ERR)
		solo_p2m_error_isr(solo_dev);

	for (i = 0; i < SOLO_NR_P2M; i++)
		if (status & SOLO_IRQ_P2M(i))
			solo_p2m_isr(solo_dev, i);

	if (status & SOLO_IRQ_IIC)
		solo_i2c_isr(solo_dev);

	if (status & SOLO_IRQ_VIDEO_IN) {
		solo_video_in_isr(solo_dev);
		solo_timer_sync(solo_dev);
	}

	if (status & SOLO_IRQ_ENCODER)
		solo_enc_v4l2_isr(solo_dev);

	if (status & SOLO_IRQ_G723)
		solo_g723_isr(solo_dev);

	return IRQ_HANDLED;
}

static void free_solo_dev(struct solo_dev *solo_dev)
{
	struct pci_dev *pdev = solo_dev->pdev;

	if (solo_dev->dev.parent)
		device_unregister(&solo_dev->dev);

	if (solo_dev->reg_base) {
		/* Bring down the sub-devices first */
		solo_g723_exit(solo_dev);
		solo_enc_v4l2_exit(solo_dev);
		solo_enc_exit(solo_dev);
		solo_v4l2_exit(solo_dev);
		solo_disp_exit(solo_dev);
		solo_gpio_exit(solo_dev);
		solo_p2m_exit(solo_dev);
		solo_i2c_exit(solo_dev);

		/* Now cleanup the PCI device */
		solo_irq_off(solo_dev, ~0);
		free_irq(pdev->irq, solo_dev);
		pci_iounmap(pdev, solo_dev->reg_base);
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	v4l2_device_unregister(&solo_dev->v4l2_dev);
	pci_set_drvdata(pdev, NULL);

	kfree(solo_dev);
}

static ssize_t eeprom_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);
	u16 *p = (u16 *)buf;
	int i;

	if (count & 0x1)
		dev_warn(dev, "EEPROM Write not aligned (truncating)\n");

	if (!full_eeprom && count > 64) {
		dev_warn(dev, "EEPROM Write truncated to 64 bytes\n");
		count = 64;
	} else if (full_eeprom && count > 128) {
		dev_warn(dev, "EEPROM Write truncated to 128 bytes\n");
		count = 128;
	}

	solo_eeprom_ewen(solo_dev, 1);

	for (i = full_eeprom ? 0 : 32; i < min((int)(full_eeprom ? 64 : 32),
					       (int)(count / 2)); i++)
		solo_eeprom_write(solo_dev, i, cpu_to_be16(p[i]));

	solo_eeprom_ewen(solo_dev, 0);

	return count;
}

static ssize_t eeprom_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);
	u16 *p = (u16 *)buf;
	int count = (full_eeprom ? 128 : 64);
	int i;

	for (i = (full_eeprom ? 0 : 32); i < (count / 2); i++)
		p[i] = be16_to_cpu(solo_eeprom_read(solo_dev, i));

	return count;
}

static ssize_t p2m_timeouts_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);

	return sprintf(buf, "%d\n", solo_dev->p2m_timeouts);
}

static ssize_t sdram_size_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);

	return sprintf(buf, "%dMegs\n", solo_dev->sdram_size >> 20);
}

static ssize_t tw28xx_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);

	return sprintf(buf, "tw2815[%d] tw2864[%d] tw2865[%d]\n",
		       hweight32(solo_dev->tw2815),
		       hweight32(solo_dev->tw2864),
		       hweight32(solo_dev->tw2865));
}

static ssize_t input_map_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);
	unsigned int val;
	char *out = buf;

	val = solo_reg_read(solo_dev, SOLO_VI_CH_SWITCH_0);
	out += sprintf(out, "Channel 0   => Input %d\n", val & 0x1f);
	out += sprintf(out, "Channel 1   => Input %d\n", (val >> 5) & 0x1f);
	out += sprintf(out, "Channel 2   => Input %d\n", (val >> 10) & 0x1f);
	out += sprintf(out, "Channel 3   => Input %d\n", (val >> 15) & 0x1f);
	out += sprintf(out, "Channel 4   => Input %d\n", (val >> 20) & 0x1f);
	out += sprintf(out, "Channel 5   => Input %d\n", (val >> 25) & 0x1f);

	val = solo_reg_read(solo_dev, SOLO_VI_CH_SWITCH_1);
	out += sprintf(out, "Channel 6   => Input %d\n", val & 0x1f);
	out += sprintf(out, "Channel 7   => Input %d\n", (val >> 5) & 0x1f);
	out += sprintf(out, "Channel 8   => Input %d\n", (val >> 10) & 0x1f);
	out += sprintf(out, "Channel 9   => Input %d\n", (val >> 15) & 0x1f);
	out += sprintf(out, "Channel 10  => Input %d\n", (val >> 20) & 0x1f);
	out += sprintf(out, "Channel 11  => Input %d\n", (val >> 25) & 0x1f);

	val = solo_reg_read(solo_dev, SOLO_VI_CH_SWITCH_2);
	out += sprintf(out, "Channel 12  => Input %d\n", val & 0x1f);
	out += sprintf(out, "Channel 13  => Input %d\n", (val >> 5) & 0x1f);
	out += sprintf(out, "Channel 14  => Input %d\n", (val >> 10) & 0x1f);
	out += sprintf(out, "Channel 15  => Input %d\n", (val >> 15) & 0x1f);
	out += sprintf(out, "Spot Output => Input %d\n", (val >> 20) & 0x1f);

	return out - buf;
}

static ssize_t p2m_timeout_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);
	unsigned long ms;
	int ret = kstrtoul(buf, 10, &ms);

	if (ret < 0 || ms > 200)
		return -EINVAL;
	solo_dev->p2m_jiffies = msecs_to_jiffies(ms);

	return count;
}

static ssize_t p2m_timeout_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);

	return sprintf(buf, "%ums\n", jiffies_to_msecs(solo_dev->p2m_jiffies));
}

static ssize_t intervals_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);
	char *out = buf;
	int fps = solo_dev->fps;
	int i;

	for (i = 0; i < solo_dev->nr_chans; i++) {
		out += sprintf(out, "Channel %d: %d/%d (0x%08x)\n",
			       i, solo_dev->v4l2_enc[i]->interval, fps,
			       solo_reg_read(solo_dev, SOLO_CAP_CH_INTV(i)));
	}

	return out - buf;
}

static ssize_t sdram_offsets_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);
	char *out = buf;

	out += sprintf(out, "DISP: 0x%08x @ 0x%08x\n",
		       SOLO_DISP_EXT_ADDR,
		       SOLO_DISP_EXT_SIZE);

	out += sprintf(out, "EOSD: 0x%08x @ 0x%08x (0x%08x * %d)\n",
		       SOLO_EOSD_EXT_ADDR,
		       SOLO_EOSD_EXT_AREA(solo_dev),
		       SOLO_EOSD_EXT_SIZE(solo_dev),
		       SOLO_EOSD_EXT_AREA(solo_dev) /
		       SOLO_EOSD_EXT_SIZE(solo_dev));

	out += sprintf(out, "MOTI: 0x%08x @ 0x%08x\n",
		       SOLO_MOTION_EXT_ADDR(solo_dev),
		       SOLO_MOTION_EXT_SIZE);

	out += sprintf(out, "G723: 0x%08x @ 0x%08x\n",
		       SOLO_G723_EXT_ADDR(solo_dev),
		       SOLO_G723_EXT_SIZE);

	out += sprintf(out, "CAPT: 0x%08x @ 0x%08x (0x%08x * %d)\n",
		       SOLO_CAP_EXT_ADDR(solo_dev),
		       SOLO_CAP_EXT_SIZE(solo_dev),
		       SOLO_CAP_PAGE_SIZE,
		       SOLO_CAP_EXT_SIZE(solo_dev) / SOLO_CAP_PAGE_SIZE);

	out += sprintf(out, "EREF: 0x%08x @ 0x%08x (0x%08x * %d)\n",
		       SOLO_EREF_EXT_ADDR(solo_dev),
		       SOLO_EREF_EXT_AREA(solo_dev),
		       SOLO_EREF_EXT_SIZE,
		       SOLO_EREF_EXT_AREA(solo_dev) / SOLO_EREF_EXT_SIZE);

	out += sprintf(out, "MPEG: 0x%08x @ 0x%08x\n",
		       SOLO_MP4E_EXT_ADDR(solo_dev),
		       SOLO_MP4E_EXT_SIZE(solo_dev));

	out += sprintf(out, "JPEG: 0x%08x @ 0x%08x\n",
		       SOLO_JPEG_EXT_ADDR(solo_dev),
		       SOLO_JPEG_EXT_SIZE(solo_dev));

	return out - buf;
}

static ssize_t sdram_show(struct file *file, struct kobject *kobj,
			  struct bin_attribute *a, char *buf,
			  loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct solo_dev *solo_dev =
		container_of(dev, struct solo_dev, dev);
	const int size = solo_dev->sdram_size;

	if (off >= size)
		return 0;

	if (off + count > size)
		count = size - off;

	if (solo_p2m_dma(solo_dev, 0, buf, off, count, 0, 0))
		return -EIO;

	return count;
}

static const struct device_attribute solo_dev_attrs[] = {
	__ATTR(eeprom, 0640, eeprom_show, eeprom_store),
	__ATTR(p2m_timeout, 0644, p2m_timeout_show, p2m_timeout_store),
	__ATTR_RO(p2m_timeouts),
	__ATTR_RO(sdram_size),
	__ATTR_RO(tw28xx),
	__ATTR_RO(input_map),
	__ATTR_RO(intervals),
	__ATTR_RO(sdram_offsets),
};

static void solo_device_release(struct device *dev)
{
	/* Do nothing */
}

static int solo_sysfs_init(struct solo_dev *solo_dev)
{
	struct bin_attribute *sdram_attr = &solo_dev->sdram_attr;
	struct device *dev = &solo_dev->dev;
	const char *driver;
	int i;

	if (solo_dev->type == SOLO_DEV_6110)
		driver = "solo6110";
	else
		driver = "solo6010";

	dev->release = solo_device_release;
	dev->parent = &solo_dev->pdev->dev;
	set_dev_node(dev, dev_to_node(&solo_dev->pdev->dev));
	dev_set_name(dev, "%s-%d-%d", driver, solo_dev->vfd->num,
		     solo_dev->nr_chans);

	if (device_register(dev)) {
		put_device(dev);
		dev->parent = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(solo_dev_attrs); i++) {
		if (device_create_file(dev, &solo_dev_attrs[i])) {
			device_unregister(dev);
			return -ENOMEM;
		}
	}

	sysfs_attr_init(&sdram_attr->attr);
	sdram_attr->attr.name = "sdram";
	sdram_attr->attr.mode = 0440;
	sdram_attr->read = sdram_show;
	sdram_attr->size = solo_dev->sdram_size;

	if (device_create_bin_file(dev, sdram_attr)) {
		device_unregister(dev);
		return -ENOMEM;
	}

	return 0;
}

static int solo_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct solo_dev *solo_dev;
	int ret;
	u8 chip_id;

	solo_dev = kzalloc(sizeof(*solo_dev), GFP_KERNEL);
	if (solo_dev == NULL)
		return -ENOMEM;

	if (id->driver_data == SOLO_DEV_6010)
		dev_info(&pdev->dev, "Probing Softlogic 6010\n");
	else
		dev_info(&pdev->dev, "Probing Softlogic 6110\n");

	solo_dev->type = id->driver_data;
	solo_dev->pdev = pdev;
	ret = v4l2_device_register(&pdev->dev, &solo_dev->v4l2_dev);
	if (ret)
		goto fail_probe;

	/* Only for during init */
	solo_dev->p2m_jiffies = msecs_to_jiffies(100);

	ret = pci_enable_device(pdev);
	if (ret)
		goto fail_probe;

	pci_set_master(pdev);

	/* RETRY/TRDY Timeout disabled */
	pci_write_config_byte(pdev, 0x40, 0x00);
	pci_write_config_byte(pdev, 0x41, 0x00);

	ret = pci_request_regions(pdev, SOLO6X10_NAME);
	if (ret)
		goto fail_probe;

	solo_dev->reg_base = pci_ioremap_bar(pdev, 0);
	if (solo_dev->reg_base == NULL) {
		ret = -ENOMEM;
		goto fail_probe;
	}

	chip_id = solo_reg_read(solo_dev, SOLO_CHIP_OPTION) &
				SOLO_CHIP_ID_MASK;
	switch (chip_id) {
	case 7:
		solo_dev->nr_chans = 16;
		solo_dev->nr_ext = 5;
		break;
	case 6:
		solo_dev->nr_chans = 8;
		solo_dev->nr_ext = 2;
		break;
	default:
		dev_warn(&pdev->dev, "Invalid chip_id 0x%02x, assuming 4 ch\n",
			 chip_id);
		fallthrough;
	case 5:
		solo_dev->nr_chans = 4;
		solo_dev->nr_ext = 1;
	}

	/* Disable all interrupts to start */
	solo_irq_off(solo_dev, ~0);

	/* Initial global settings */
	if (solo_dev->type == SOLO_DEV_6010) {
		solo_dev->clock_mhz = 108;
		solo_dev->sys_config = SOLO_SYS_CFG_SDRAM64BIT
			| SOLO_SYS_CFG_INPUTDIV(25)
			| SOLO_SYS_CFG_FEEDBACKDIV(solo_dev->clock_mhz * 2 - 2)
			| SOLO_SYS_CFG_OUTDIV(3);
		solo_reg_write(solo_dev, SOLO_SYS_CFG, solo_dev->sys_config);
	} else {
		u32 divq, divf;

		solo_dev->clock_mhz = 135;

		if (solo_dev->clock_mhz < 125) {
			divq = 3;
			divf = (solo_dev->clock_mhz * 4) / 3 - 1;
		} else {
			divq = 2;
			divf = (solo_dev->clock_mhz * 2) / 3 - 1;
		}

		solo_reg_write(solo_dev, SOLO_PLL_CONFIG,
			       (1 << 20) | /* PLL_RANGE */
			       (8 << 15) | /* PLL_DIVR  */
			       (divq << 12) |
			       (divf <<  4) |
			       (1 <<  1)   /* PLL_FSEN */);

		solo_dev->sys_config = SOLO_SYS_CFG_SDRAM64BIT;
	}

	solo_reg_write(solo_dev, SOLO_SYS_CFG, solo_dev->sys_config);
	solo_reg_write(solo_dev, SOLO_TIMER_CLOCK_NUM,
		       solo_dev->clock_mhz - 1);

	/* PLL locking time of 1ms */
	mdelay(1);

	ret = request_irq(pdev->irq, solo_isr, IRQF_SHARED, SOLO6X10_NAME,
			  solo_dev);
	if (ret)
		goto fail_probe;

	/* Handle this from the start */
	solo_irq_on(solo_dev, SOLO_IRQ_PCI_ERR);

	ret = solo_i2c_init(solo_dev);
	if (ret)
		goto fail_probe;

	/* Setup the DMA engine */
	solo_reg_write(solo_dev, SOLO_DMA_CTRL,
		       SOLO_DMA_CTRL_REFRESH_CYCLE(1) |
		       SOLO_DMA_CTRL_SDRAM_SIZE(2) |
		       SOLO_DMA_CTRL_SDRAM_CLK_INVERT |
		       SOLO_DMA_CTRL_READ_CLK_SELECT |
		       SOLO_DMA_CTRL_LATENCY(1));

	/* Undocumented crap */
	solo_reg_write(solo_dev, SOLO_DMA_CTRL1,
		       solo_dev->type == SOLO_DEV_6010 ? 0x100 : 0x300);

	if (solo_dev->type != SOLO_DEV_6010) {
		solo_dev->usec_lsb = 0x3f;
		solo_set_time(solo_dev);
	}

	/* Disable watchdog */
	solo_reg_write(solo_dev, SOLO_WATCHDOG, 0);

	/* Initialize sub components */

	ret = solo_p2m_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_disp_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_gpio_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_tw28_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_v4l2_init(solo_dev, video_nr);
	if (ret)
		goto fail_probe;

	ret = solo_enc_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_enc_v4l2_init(solo_dev, video_nr);
	if (ret)
		goto fail_probe;

	ret = solo_g723_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_sysfs_init(solo_dev);
	if (ret)
		goto fail_probe;

	/* Now that init is over, set this lower */
	solo_dev->p2m_jiffies = msecs_to_jiffies(20);

	return 0;

fail_probe:
	free_solo_dev(solo_dev);
	return ret;
}

static void solo_pci_remove(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pdev);
	struct solo_dev *solo_dev = container_of(v4l2_dev, struct solo_dev, v4l2_dev);

	free_solo_dev(solo_dev);
}

static const struct pci_device_id solo_id_table[] = {
	/* 6010 based cards */
	{ PCI_DEVICE(PCI_VENDOR_ID_SOFTLOGIC, PCI_DEVICE_ID_SOLO6010),
	  .driver_data = SOLO_DEV_6010 },
	{ PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_NEUSOLO_4),
	  .driver_data = SOLO_DEV_6010 },
	{ PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_NEUSOLO_9),
	  .driver_data = SOLO_DEV_6010 },
	{ PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_NEUSOLO_16),
	  .driver_data = SOLO_DEV_6010 },
	{ PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_SOLO_4),
	  .driver_data = SOLO_DEV_6010 },
	{ PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_SOLO_9),
	  .driver_data = SOLO_DEV_6010 },
	{ PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_SOLO_16),
	  .driver_data = SOLO_DEV_6010 },
	/* 6110 based cards */
	{ PCI_DEVICE(PCI_VENDOR_ID_SOFTLOGIC, PCI_DEVICE_ID_SOLO6110),
	  .driver_data = SOLO_DEV_6110 },
	{ PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_6110_4),
	  .driver_data = SOLO_DEV_6110 },
	{ PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_6110_8),
	  .driver_data = SOLO_DEV_6110 },
	{ PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_6110_16),
	  .driver_data = SOLO_DEV_6110 },
	{0,}
};

MODULE_DEVICE_TABLE(pci, solo_id_table);

static struct pci_driver solo_pci_driver = {
	.name = SOLO6X10_NAME,
	.id_table = solo_id_table,
	.probe = solo_pci_probe,
	.remove = solo_pci_remove,
};

module_pci_driver(solo_pci_driver);
