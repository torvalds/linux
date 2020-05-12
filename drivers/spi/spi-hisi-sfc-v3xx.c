// SPDX-License-Identifier: GPL-2.0-only
//
// HiSilicon SPI NOR V3XX Flash Controller Driver for hi16xx chipsets
//
// Copyright (c) 2019 HiSilicon Technologies Co., Ltd.
// Author: John Garry <john.garry@huawei.com>

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#define HISI_SFC_V3XX_VERSION (0x1f8)

#define HISI_SFC_V3XX_CMD_CFG (0x300)
#define HISI_SFC_V3XX_CMD_CFG_DATA_CNT_OFF 9
#define HISI_SFC_V3XX_CMD_CFG_RW_MSK BIT(8)
#define HISI_SFC_V3XX_CMD_CFG_DATA_EN_MSK BIT(7)
#define HISI_SFC_V3XX_CMD_CFG_DUMMY_CNT_OFF 4
#define HISI_SFC_V3XX_CMD_CFG_ADDR_EN_MSK BIT(3)
#define HISI_SFC_V3XX_CMD_CFG_CS_SEL_OFF 1
#define HISI_SFC_V3XX_CMD_CFG_START_MSK BIT(0)
#define HISI_SFC_V3XX_CMD_INS (0x308)
#define HISI_SFC_V3XX_CMD_ADDR (0x30c)
#define HISI_SFC_V3XX_CMD_DATABUF0 (0x400)

struct hisi_sfc_v3xx_host {
	struct device *dev;
	void __iomem *regbase;
	int max_cmd_dword;
};

#define HISI_SFC_V3XX_WAIT_TIMEOUT_US		1000000
#define HISI_SFC_V3XX_WAIT_POLL_INTERVAL_US	10

static int hisi_sfc_v3xx_wait_cmd_idle(struct hisi_sfc_v3xx_host *host)
{
	u32 reg;

	return readl_poll_timeout(host->regbase + HISI_SFC_V3XX_CMD_CFG, reg,
				  !(reg & HISI_SFC_V3XX_CMD_CFG_START_MSK),
				  HISI_SFC_V3XX_WAIT_POLL_INTERVAL_US,
				  HISI_SFC_V3XX_WAIT_TIMEOUT_US);
}

static int hisi_sfc_v3xx_adjust_op_size(struct spi_mem *mem,
					struct spi_mem_op *op)
{
	struct spi_device *spi = mem->spi;
	struct hisi_sfc_v3xx_host *host;
	uintptr_t addr = (uintptr_t)op->data.buf.in;
	int max_byte_count;

	host = spi_controller_get_devdata(spi->master);

	max_byte_count = host->max_cmd_dword * 4;

	if (!IS_ALIGNED(addr, 4) && op->data.nbytes >= 4)
		op->data.nbytes = 4 - (addr % 4);
	else if (op->data.nbytes > max_byte_count)
		op->data.nbytes = max_byte_count;

	return 0;
}

/*
 * memcpy_{to,from}io doesn't gurantee 32b accesses - which we require for the
 * DATABUF registers -so use __io{read,write}32_copy when possible. For
 * trailing bytes, copy them byte-by-byte from the DATABUF register, as we
 * can't clobber outside the source/dest buffer.
 *
 * For efficient data read/write, we try to put any start 32b unaligned data
 * into a separate transaction in hisi_sfc_v3xx_adjust_op_size().
 */
static void hisi_sfc_v3xx_read_databuf(struct hisi_sfc_v3xx_host *host,
				       u8 *to, unsigned int len)
{
	void __iomem *from;
	int i;

	from = host->regbase + HISI_SFC_V3XX_CMD_DATABUF0;

	if (IS_ALIGNED((uintptr_t)to, 4)) {
		int words = len / 4;

		__ioread32_copy(to, from, words);

		len -= words * 4;
		if (len) {
			u32 val;

			to += words * 4;
			from += words * 4;

			val = __raw_readl(from);

			for (i = 0; i < len; i++, val >>= 8, to++)
				*to = (u8)val;
		}
	} else {
		for (i = 0; i < DIV_ROUND_UP(len, 4); i++, from += 4) {
			u32 val = __raw_readl(from);
			int j;

			for (j = 0; j < 4 && (j + (i * 4) < len);
			     to++, val >>= 8, j++)
				*to = (u8)val;
		}
	}
}

static void hisi_sfc_v3xx_write_databuf(struct hisi_sfc_v3xx_host *host,
					const u8 *from, unsigned int len)
{
	void __iomem *to;
	int i;

	to = host->regbase + HISI_SFC_V3XX_CMD_DATABUF0;

	if (IS_ALIGNED((uintptr_t)from, 4)) {
		int words = len / 4;

		__iowrite32_copy(to, from, words);

		len -= words * 4;
		if (len) {
			u32 val = 0;

			to += words * 4;
			from += words * 4;

			for (i = 0; i < len; i++, from++)
				val |= *from << i * 8;
			__raw_writel(val, to);
		}

	} else {
		for (i = 0; i < DIV_ROUND_UP(len, 4); i++, to += 4) {
			u32 val = 0;
			int j;

			for (j = 0; j < 4 && (j + (i * 4) < len);
			     from++, j++)
				val |= *from << j * 8;
			__raw_writel(val, to);
		}
	}
}

static int hisi_sfc_v3xx_generic_exec_op(struct hisi_sfc_v3xx_host *host,
					 const struct spi_mem_op *op,
					 u8 chip_select)
{
	int ret, len = op->data.nbytes;
	u32 config = 0;

	if (op->addr.nbytes)
		config |= HISI_SFC_V3XX_CMD_CFG_ADDR_EN_MSK;

	if (op->data.dir != SPI_MEM_NO_DATA) {
		config |= (len - 1) << HISI_SFC_V3XX_CMD_CFG_DATA_CNT_OFF;
		config |= HISI_SFC_V3XX_CMD_CFG_DATA_EN_MSK;
	}

	if (op->data.dir == SPI_MEM_DATA_OUT)
		hisi_sfc_v3xx_write_databuf(host, op->data.buf.out, len);
	else if (op->data.dir == SPI_MEM_DATA_IN)
		config |= HISI_SFC_V3XX_CMD_CFG_RW_MSK;

	config |= op->dummy.nbytes << HISI_SFC_V3XX_CMD_CFG_DUMMY_CNT_OFF |
		  chip_select << HISI_SFC_V3XX_CMD_CFG_CS_SEL_OFF |
		  HISI_SFC_V3XX_CMD_CFG_START_MSK;

	writel(op->addr.val, host->regbase + HISI_SFC_V3XX_CMD_ADDR);
	writel(op->cmd.opcode, host->regbase + HISI_SFC_V3XX_CMD_INS);

	writel(config, host->regbase + HISI_SFC_V3XX_CMD_CFG);

	ret = hisi_sfc_v3xx_wait_cmd_idle(host);
	if (ret)
		return ret;

	if (op->data.dir == SPI_MEM_DATA_IN)
		hisi_sfc_v3xx_read_databuf(host, op->data.buf.in, len);

	return 0;
}

static int hisi_sfc_v3xx_exec_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	struct hisi_sfc_v3xx_host *host;
	struct spi_device *spi = mem->spi;
	u8 chip_select = spi->chip_select;

	host = spi_controller_get_devdata(spi->master);

	return hisi_sfc_v3xx_generic_exec_op(host, op, chip_select);
}

static const struct spi_controller_mem_ops hisi_sfc_v3xx_mem_ops = {
	.adjust_op_size = hisi_sfc_v3xx_adjust_op_size,
	.exec_op = hisi_sfc_v3xx_exec_op,
};

static int hisi_sfc_v3xx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hisi_sfc_v3xx_host *host;
	struct spi_controller *ctlr;
	u32 version;
	int ret;

	ctlr = spi_alloc_master(&pdev->dev, sizeof(*host));
	if (!ctlr)
		return -ENOMEM;

	ctlr->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD |
			  SPI_TX_DUAL | SPI_TX_QUAD;

	host = spi_controller_get_devdata(ctlr);
	host->dev = dev;

	platform_set_drvdata(pdev, host);

	host->regbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->regbase)) {
		ret = PTR_ERR(host->regbase);
		goto err_put_master;
	}

	ctlr->bus_num = -1;
	ctlr->num_chipselect = 1;
	ctlr->mem_ops = &hisi_sfc_v3xx_mem_ops;

	version = readl(host->regbase + HISI_SFC_V3XX_VERSION);

	switch (version) {
	case 0x351:
		host->max_cmd_dword = 64;
		break;
	default:
		host->max_cmd_dword = 16;
		break;
	}

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret)
		goto err_put_master;

	dev_info(&pdev->dev, "hw version 0x%x\n", version);

	return 0;

err_put_master:
	spi_master_put(ctlr);
	return ret;
}

#if IS_ENABLED(CONFIG_ACPI)
static const struct acpi_device_id hisi_sfc_v3xx_acpi_ids[] = {
	{"HISI0341", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_sfc_v3xx_acpi_ids);
#endif

static struct platform_driver hisi_sfc_v3xx_spi_driver = {
	.driver = {
		.name	= "hisi-sfc-v3xx",
		.acpi_match_table = ACPI_PTR(hisi_sfc_v3xx_acpi_ids),
	},
	.probe	= hisi_sfc_v3xx_probe,
};

module_platform_driver(hisi_sfc_v3xx_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HiSilicon SPI NOR V3XX Flash Controller Driver for hi16xx chipsets");
