// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is the driver for the MGB4 video grabber card by Digiteq Automotive.
 *
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 *
 * This is the main driver module. The DMA, I2C and SPI sub-drivers are
 * initialized here and the input/output v4l2 devices are created.
 *
 * The mgb4 card uses different expansion modules for different video sources
 * (GMSL and FPDL3 for now) so in probe() we detect the module type based on
 * what we see on the I2C bus and check if it matches the FPGA bitstream (there
 * are different bitstreams for different expansion modules). When no expansion
 * module is present, we still let the driver initialize to allow flashing of
 * the FPGA firmware using the SPI FLASH device. No v4l2 video devices are
 * created in this case.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/dma/amd_xdma.h>
#include <linux/platform_data/amd_xdma.h>
#include <linux/spi/xilinx_spi.h>
#include <linux/mtd/mtd.h>
#include <linux/hwmon.h>
#include <linux/debugfs.h>
#include "mgb4_dma.h"
#include "mgb4_i2c.h"
#include "mgb4_sysfs.h"
#include "mgb4_vout.h"
#include "mgb4_vin.h"
#include "mgb4_trigger.h"
#include "mgb4_core.h"

#define MGB4_USER_IRQS 16

#define DIGITEQ_VID 0x1ed8
#define T100_DID    0x0101
#define T200_DID    0x0201

ATTRIBUTE_GROUPS(mgb4_pci);

static int flashid;

static struct xdma_chan_info h2c_chan_info = {
	.dir = DMA_MEM_TO_DEV,
};

static struct xdma_chan_info c2h_chan_info = {
	.dir = DMA_DEV_TO_MEM,
};

static struct xspi_platform_data spi_platform_data = {
	.num_chipselect = 1,
	.bits_per_word = 8
};

static const struct i2c_board_info extender_info = {
	I2C_BOARD_INFO("extender", 0x21)
};

#if IS_REACHABLE(CONFIG_HWMON)
static umode_t temp_is_visible(const void *data, enum hwmon_sensor_types type,
			       u32 attr, int channel)
{
	if (type == hwmon_temp &&
	    (attr == hwmon_temp_input || attr == hwmon_temp_label))
		return 0444;
	else
		return 0;
}

static int temp_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
		     int channel, long *val)
{
	struct mgb4_dev *mgbdev = dev_get_drvdata(dev);
	u32 val10, raw;

	if (type != hwmon_temp || attr != hwmon_temp_input)
		return -EOPNOTSUPP;

	raw = mgb4_read_reg(&mgbdev->video, 0xD0);
	/* register value -> Celsius degrees formula given by Xilinx */
	val10 = ((((raw >> 20) & 0xFFF) * 503975) - 1118822400) / 409600;
	*val = val10 * 100;

	return 0;
}

static int temp_read_string(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, const char **str)
{
	if (type != hwmon_temp || attr != hwmon_temp_label)
		return -EOPNOTSUPP;

	*str = "FPGA Temperature";

	return 0;
}

static const struct hwmon_ops temp_ops = {
	.is_visible = temp_is_visible,
	.read = temp_read,
	.read_string = temp_read_string
};

static const struct hwmon_channel_info *temp_channel_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_chip_info temp_chip_info = {
	.ops = &temp_ops,
	.info = temp_channel_info,
};
#endif

static int match_i2c_adap(struct device *dev, void *data)
{
	return i2c_verify_adapter(dev) ? 1 : 0;
}

static struct i2c_adapter *get_i2c_adap(struct platform_device *pdev)
{
	struct device *dev;

	mutex_lock(&pdev->dev.mutex);
	dev = device_find_child(&pdev->dev, NULL, match_i2c_adap);
	mutex_unlock(&pdev->dev.mutex);

	return dev ? to_i2c_adapter(dev) : NULL;
}

static int match_spi_adap(struct device *dev, void *data)
{
	return to_spi_device(dev) ? 1 : 0;
}

static struct spi_controller *get_spi_adap(struct platform_device *pdev)
{
	struct device *dev;

	mutex_lock(&pdev->dev.mutex);
	dev = device_find_child(&pdev->dev, NULL, match_spi_adap);
	mutex_unlock(&pdev->dev.mutex);

	return dev ? container_of(dev, struct spi_controller, dev) : NULL;
}

static int init_spi(struct mgb4_dev *mgbdev, u32 devid)
{
	struct resource spi_resources[] = {
		{
			.start	= 0x400,
			.end	= 0x47f,
			.flags	= IORESOURCE_MEM,
			.name	= "io-memory",
		},
		{
			.start	= 14,
			.end	= 14,
			.flags	= IORESOURCE_IRQ,
			.name	= "irq",
		},
	};
	struct spi_board_info spi_info = {
		.max_speed_hz = 10000000,
		.modalias = "m25p80",
		.chip_select = 0,
		.mode = SPI_MODE_3,
	};
	struct pci_dev *pdev = mgbdev->pdev;
	struct device *dev = &pdev->dev;
	struct spi_controller *ctlr;
	struct spi_device *spi_dev;
	u32 irq;
	int rv, id;
	resource_size_t mapbase = pci_resource_start(pdev, MGB4_MGB4_BAR_ID);

	request_module("platform:xilinx_spi");

	irq = xdma_get_user_irq(mgbdev->xdev, 14);
	xdma_enable_user_irq(mgbdev->xdev, irq);

	spi_resources[0].parent = &pdev->resource[MGB4_MGB4_BAR_ID];
	spi_resources[0].start += mapbase;
	spi_resources[0].end += mapbase;
	spi_resources[1].start = irq;
	spi_resources[1].end = irq;

	id = pci_dev_id(pdev);
	mgbdev->spi_pdev = platform_device_register_resndata(dev, "xilinx_spi",
							     id, spi_resources,
							     ARRAY_SIZE(spi_resources),
							     &spi_platform_data,
							     sizeof(spi_platform_data));
	if (IS_ERR(mgbdev->spi_pdev)) {
		dev_err(dev, "failed to register SPI device\n");
		return PTR_ERR(mgbdev->spi_pdev);
	}

	ctlr = get_spi_adap(mgbdev->spi_pdev);
	if (!ctlr) {
		dev_err(dev, "failed to get SPI adapter\n");
		rv = -EINVAL;
		goto err_pdev;
	}

	snprintf(mgbdev->fw_part_name, sizeof(mgbdev->fw_part_name),
		 "mgb4-fw.%d", flashid);
	mgbdev->partitions[0].name = mgbdev->fw_part_name;
	if (devid == T200_DID) {
		mgbdev->partitions[0].size = 0x950000;
		mgbdev->partitions[0].offset = 0x1000000;
	} else {
		mgbdev->partitions[0].size = 0x400000;
		mgbdev->partitions[0].offset = 0x400000;
	}
	mgbdev->partitions[0].mask_flags = 0;

	snprintf(mgbdev->data_part_name, sizeof(mgbdev->data_part_name),
		 "mgb4-data.%d", flashid);
	mgbdev->partitions[1].name = mgbdev->data_part_name;
	mgbdev->partitions[1].size = 0x10000;
	mgbdev->partitions[1].offset = 0xFF0000;
	mgbdev->partitions[1].mask_flags = MTD_CAP_NORFLASH;

	snprintf(mgbdev->flash_name, sizeof(mgbdev->flash_name),
		 "mgb4-flash.%d", flashid);
	mgbdev->flash_data.name = mgbdev->flash_name;
	mgbdev->flash_data.parts = mgbdev->partitions;
	mgbdev->flash_data.nr_parts = ARRAY_SIZE(mgbdev->partitions);
	mgbdev->flash_data.type = "spi-nor";

	spi_info.platform_data = &mgbdev->flash_data;

	spi_dev = spi_new_device(ctlr, &spi_info);
	put_device(&ctlr->dev);
	if (!spi_dev) {
		dev_err(dev, "failed to create MTD device\n");
		rv = -EINVAL;
		goto err_pdev;
	}

	return 0;

err_pdev:
	platform_device_unregister(mgbdev->spi_pdev);

	return rv;
}

static void free_spi(struct mgb4_dev *mgbdev)
{
	platform_device_unregister(mgbdev->spi_pdev);
}

static int init_i2c(struct mgb4_dev *mgbdev)
{
	struct resource i2c_resources[] = {
		{
			.start	= 0x200,
			.end	= 0x3ff,
			.flags	= IORESOURCE_MEM,
			.name	= "io-memory",
		},
		{
			.start	= 15,
			.end	= 15,
			.flags	= IORESOURCE_IRQ,
			.name	= "irq",
		},
	};
	struct pci_dev *pdev = mgbdev->pdev;
	struct device *dev = &pdev->dev;
	char clk_name[16];
	u32 irq;
	int rv, id;
	resource_size_t mapbase = pci_resource_start(pdev, MGB4_MGB4_BAR_ID);

	request_module("platform:xiic-i2c");

	irq = xdma_get_user_irq(mgbdev->xdev, 15);
	xdma_enable_user_irq(mgbdev->xdev, irq);

	i2c_resources[0].parent = &pdev->resource[MGB4_MGB4_BAR_ID];
	i2c_resources[0].start += mapbase;
	i2c_resources[0].end += mapbase;
	i2c_resources[1].start = irq;
	i2c_resources[1].end = irq;

	id = pci_dev_id(pdev);

	/* create dummy clock required by the xiic-i2c adapter */
	snprintf(clk_name, sizeof(clk_name), "xiic-i2c.%d", id);
	mgbdev->i2c_clk = clk_hw_register_fixed_rate(NULL, clk_name, NULL,
						     0, 125000000);
	if (IS_ERR(mgbdev->i2c_clk)) {
		dev_err(dev, "failed to register I2C clock\n");
		return PTR_ERR(mgbdev->i2c_clk);
	}
	mgbdev->i2c_cl = clkdev_hw_create(mgbdev->i2c_clk, NULL, "xiic-i2c.%d",
					  id);
	if (!mgbdev->i2c_cl) {
		dev_err(dev, "failed to register I2C clockdev\n");
		rv = -ENOMEM;
		goto err_clk;
	}

	mgbdev->i2c_pdev = platform_device_register_resndata(dev, "xiic-i2c",
							     id, i2c_resources,
							     ARRAY_SIZE(i2c_resources),
							     NULL, 0);
	if (IS_ERR(mgbdev->i2c_pdev)) {
		dev_err(dev, "failed to register I2C device\n");
		rv = PTR_ERR(mgbdev->i2c_pdev);
		goto err_clkdev;
	}

	mgbdev->i2c_adap = get_i2c_adap(mgbdev->i2c_pdev);
	if (!mgbdev->i2c_adap) {
		dev_err(dev, "failed to get I2C adapter\n");
		rv = -EINVAL;
		goto err_pdev;
	}

	mutex_init(&mgbdev->i2c_lock);

	return 0;

err_pdev:
	platform_device_unregister(mgbdev->i2c_pdev);
err_clkdev:
	clkdev_drop(mgbdev->i2c_cl);
err_clk:
	clk_hw_unregister(mgbdev->i2c_clk);

	return rv;
}

static void free_i2c(struct mgb4_dev *mgbdev)
{
	put_device(&mgbdev->i2c_adap->dev);
	platform_device_unregister(mgbdev->i2c_pdev);
	clkdev_drop(mgbdev->i2c_cl);
	clk_hw_unregister(mgbdev->i2c_clk);
}

static int get_serial_number(struct mgb4_dev *mgbdev)
{
	struct device *dev = &mgbdev->pdev->dev;
	struct mtd_info *mtd;
	size_t rs;
	int rv;

	mgbdev->serial_number = 0;

	mtd = get_mtd_device_nm(mgbdev->data_part_name);
	if (IS_ERR(mtd)) {
		dev_warn(dev, "failed to get data MTD device\n");
		return -ENOENT;
	}
	rv = mtd_read(mtd, 0, sizeof(mgbdev->serial_number), &rs,
		      (u_char *)&mgbdev->serial_number);
	put_mtd_device(mtd);
	if (rv < 0 || rs != sizeof(mgbdev->serial_number)) {
		dev_warn(dev, "error reading MTD device\n");
		return -EIO;
	}

	return 0;
}

static int get_module_version(struct mgb4_dev *mgbdev)
{
	struct device *dev = &mgbdev->pdev->dev;
	struct mgb4_i2c_client extender;
	s32 version;
	u32 fw_version;
	int rv;

	rv = mgb4_i2c_init(&extender, mgbdev->i2c_adap, &extender_info, 8);
	if (rv < 0) {
		dev_err(dev, "failed to create extender I2C device\n");
		return rv;
	}
	version = mgb4_i2c_read_byte(&extender, 0x00);
	mgb4_i2c_free(&extender);
	if (version < 0) {
		dev_err(dev, "error reading module version\n");
		return -EIO;
	}

	mgbdev->module_version = ~((u32)version) & 0xff;
	if (!(MGB4_IS_FPDL3(mgbdev) || MGB4_IS_GMSL(mgbdev))) {
		dev_err(dev, "unknown module type\n");
		return -EINVAL;
	}
	fw_version = mgb4_read_reg(&mgbdev->video, 0xC4);
	if (fw_version >> 24 != mgbdev->module_version >> 4) {
		dev_err(dev, "module/firmware type mismatch\n");
		return -EINVAL;
	}

	dev_info(dev, "%s module detected\n",
		 MGB4_IS_FPDL3(mgbdev) ? "FPDL3" : "GMSL");

	return 0;
}

static int map_regs(struct pci_dev *pdev, struct resource *res,
		    struct mgb4_regs *regs)
{
	int rv;
	resource_size_t mapbase = pci_resource_start(pdev, MGB4_MGB4_BAR_ID);

	res->start += mapbase;
	res->end += mapbase;

	rv = mgb4_regs_map(res, regs);
	if (rv < 0) {
		dev_err(&pdev->dev, "failed to map %s registers\n", res->name);
		return rv;
	}

	return 0;
}

static int init_xdma(struct mgb4_dev *mgbdev)
{
	struct xdma_platdata data;
	struct resource res[2] = { 0 };
	struct dma_slave_map *map;
	struct pci_dev *pdev = mgbdev->pdev;
	struct device *dev = &pdev->dev;
	int i;

	res[0].start = pci_resource_start(pdev, MGB4_XDMA_BAR_ID);
	res[0].end = pci_resource_end(pdev, MGB4_XDMA_BAR_ID);
	res[0].flags = IORESOURCE_MEM;
	res[0].parent = &pdev->resource[MGB4_XDMA_BAR_ID];
	res[1].start = pci_irq_vector(pdev, 0);
	res[1].end = res[1].start + MGB4_VIN_DEVICES + MGB4_VOUT_DEVICES
		     + MGB4_USER_IRQS - 1;
	res[1].flags = IORESOURCE_IRQ;

	data.max_dma_channels = MGB4_VIN_DEVICES + MGB4_VOUT_DEVICES;
	data.device_map = mgbdev->slave_map;
	data.device_map_cnt = MGB4_VIN_DEVICES + MGB4_VOUT_DEVICES;

	for (i = 0; i < MGB4_VIN_DEVICES; i++) {
		sprintf(mgbdev->channel_names[i], "c2h%d", i);
		map = &data.device_map[i];
		map->slave = mgbdev->channel_names[i];
		map->devname = dev_name(dev);
		map->param = XDMA_FILTER_PARAM(&c2h_chan_info);
	}
	for (i = 0; i < MGB4_VOUT_DEVICES; i++) {
		sprintf(mgbdev->channel_names[i + MGB4_VIN_DEVICES], "h2c%d", i);
		map = &data.device_map[i + MGB4_VIN_DEVICES];
		map->slave = mgbdev->channel_names[i + MGB4_VIN_DEVICES];
		map->devname = dev_name(dev);
		map->param = XDMA_FILTER_PARAM(&h2c_chan_info);
	}

	mgbdev->xdev = platform_device_register_resndata(dev, "xdma",
							 PLATFORM_DEVID_AUTO, res,
							 2, &data, sizeof(data));
	if (IS_ERR(mgbdev->xdev)) {
		dev_err(dev, "failed to register XDMA device\n");
		return PTR_ERR(mgbdev->xdev);
	}

	return 0;
}

static void free_xdma(struct mgb4_dev *mgbdev)
{
	platform_device_unregister(mgbdev->xdev);
}

static int mgb4_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int i, rv;
	struct mgb4_dev *mgbdev;
	struct resource video = {
		.start	= 0x0,
		.end	= 0xff,
		.flags	= IORESOURCE_MEM,
		.name	= "mgb4-video",
	};
	struct resource cmt = {
		.start	= 0x1000,
		.end	= 0x17ff,
		.flags	= IORESOURCE_MEM,
		.name	= "mgb4-cmt",
	};
	int irqs = pci_msix_vec_count(pdev);

	mgbdev = kzalloc(sizeof(*mgbdev), GFP_KERNEL);
	if (!mgbdev)
		return -ENOMEM;

	mgbdev->pdev = pdev;
	pci_set_drvdata(pdev, mgbdev);

	/* PCIe related stuff */
	rv = pci_enable_device(pdev);
	if (rv) {
		dev_err(&pdev->dev, "error enabling PCI device\n");
		goto err_mgbdev;
	}

	rv = pcie_capability_set_word(pdev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_RELAX_EN);
	if (rv)
		dev_warn(&pdev->dev, "error enabling PCIe relaxed ordering\n");
	rv = pcie_capability_set_word(pdev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_EXT_TAG);
	if (rv)
		dev_warn(&pdev->dev, "error enabling PCIe extended tag field\n");
	rv = pcie_set_readrq(pdev, 512);
	if (rv)
		dev_warn(&pdev->dev, "error setting PCIe max. memory read size\n");
	pci_set_master(pdev);

	rv = pci_alloc_irq_vectors(pdev, irqs, irqs, PCI_IRQ_MSIX);
	if (rv < 0) {
		dev_err(&pdev->dev, "error allocating MSI-X IRQs\n");
		goto err_enable_pci;
	}

	rv = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rv) {
		dev_err(&pdev->dev, "error setting DMA mask\n");
		goto err_enable_pci;
	}

	/* DMA + IRQ engine */
	rv = init_xdma(mgbdev);
	if (rv)
		goto err_alloc_irq;
	rv = mgb4_dma_channel_init(mgbdev);
	if (rv)
		goto err_dma_chan;

	/* mgb4 video registers */
	rv = map_regs(pdev, &video, &mgbdev->video);
	if (rv < 0)
		goto err_dma_chan;
	/* mgb4 cmt registers */
	rv = map_regs(pdev, &cmt, &mgbdev->cmt);
	if (rv < 0)
		goto err_video_regs;

	/* SPI FLASH */
	rv = init_spi(mgbdev, id->device);
	if (rv < 0)
		goto err_cmt_regs;

	/* I2C controller */
	rv = init_i2c(mgbdev);
	if (rv < 0)
		goto err_spi;

	/* PCI card related sysfs attributes */
	rv = device_add_groups(&pdev->dev, mgb4_pci_groups);
	if (rv < 0)
		goto err_i2c;

#if IS_REACHABLE(CONFIG_HWMON)
	/* HWmon (card temperature) */
	mgbdev->hwmon_dev = hwmon_device_register_with_info(&pdev->dev, "mgb4",
							    mgbdev,
							    &temp_chip_info,
							    NULL);
#endif

#ifdef CONFIG_DEBUG_FS
	mgbdev->debugfs = debugfs_create_dir(dev_name(&pdev->dev), NULL);
#endif

	/* Get card serial number. On systems without MTD flash support we may
	 * get an error thus ignore the return value. An invalid serial number
	 * should not break anything...
	 */
	if (get_serial_number(mgbdev) < 0)
		dev_warn(&pdev->dev, "error reading card serial number\n");

	/* Get module type. If no valid module is found, skip the video device
	 * creation part but do not exit with error to allow flashing the card.
	 */
	rv = get_module_version(mgbdev);
	if (rv < 0)
		goto exit;

	/* Video input v4l2 devices */
	for (i = 0; i < MGB4_VIN_DEVICES; i++)
		mgbdev->vin[i] = mgb4_vin_create(mgbdev, i);

	/* Video output v4l2 devices */
	for (i = 0; i < MGB4_VOUT_DEVICES; i++)
		mgbdev->vout[i] = mgb4_vout_create(mgbdev, i);

	/* Triggers */
	mgbdev->indio_dev = mgb4_trigger_create(mgbdev);

exit:
	flashid++;

	return 0;

err_i2c:
	free_i2c(mgbdev);
err_spi:
	free_spi(mgbdev);
err_cmt_regs:
	mgb4_regs_free(&mgbdev->cmt);
err_video_regs:
	mgb4_regs_free(&mgbdev->video);
err_dma_chan:
	mgb4_dma_channel_free(mgbdev);
	free_xdma(mgbdev);
err_alloc_irq:
	pci_disable_msix(pdev);
err_enable_pci:
	pci_disable_device(pdev);
err_mgbdev:
	kfree(mgbdev);

	return rv;
}

static void mgb4_remove(struct pci_dev *pdev)
{
	struct mgb4_dev *mgbdev = pci_get_drvdata(pdev);
	int i;

#if IS_REACHABLE(CONFIG_HWMON)
	hwmon_device_unregister(mgbdev->hwmon_dev);
#endif

	if (mgbdev->indio_dev)
		mgb4_trigger_free(mgbdev->indio_dev);

	for (i = 0; i < MGB4_VOUT_DEVICES; i++)
		if (mgbdev->vout[i])
			mgb4_vout_free(mgbdev->vout[i]);
	for (i = 0; i < MGB4_VIN_DEVICES; i++)
		if (mgbdev->vin[i])
			mgb4_vin_free(mgbdev->vin[i]);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(mgbdev->debugfs);
#endif

	device_remove_groups(&mgbdev->pdev->dev, mgb4_pci_groups);
	free_spi(mgbdev);
	free_i2c(mgbdev);
	mgb4_regs_free(&mgbdev->video);
	mgb4_regs_free(&mgbdev->cmt);

	mgb4_dma_channel_free(mgbdev);
	free_xdma(mgbdev);

	pci_disable_msix(mgbdev->pdev);
	pci_disable_device(mgbdev->pdev);

	kfree(mgbdev);
}

static const struct pci_device_id mgb4_pci_ids[] = {
	{ PCI_DEVICE(DIGITEQ_VID, T100_DID), },
	{ PCI_DEVICE(DIGITEQ_VID, T200_DID), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, mgb4_pci_ids);

static struct pci_driver mgb4_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = mgb4_pci_ids,
	.probe = mgb4_probe,
	.remove = mgb4_remove,
};

module_pci_driver(mgb4_pci_driver);

MODULE_AUTHOR("Digiteq Automotive s.r.o.");
MODULE_DESCRIPTION("Digiteq Automotive MGB4 Driver");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: platform:xiic-i2c platform:xilinx_spi spi-nor");
