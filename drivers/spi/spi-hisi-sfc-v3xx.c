// SPDX-License-Identifier: GPL-2.0-only
//
// HiSilicon SPI NOR V3XX Flash Controller Driver for hi16xx chipsets
//
// Copyright (c) 2019 HiSilicon Technologies Co., Ltd.
// Author: John Garry <john.garry@huawei.com>

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/dmi.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#define HISI_SFC_V3XX_VERSION (0x1f8)

#define HISI_SFC_V3XX_GLB_CFG (0x100)
#define HISI_SFC_V3XX_GLB_CFG_CS0_ADDR_MODE BIT(2)
#define HISI_SFC_V3XX_RAW_INT_STAT (0x120)
#define HISI_SFC_V3XX_INT_STAT (0x124)
#define HISI_SFC_V3XX_INT_MASK (0x128)
#define HISI_SFC_V3XX_INT_CLR (0x12c)
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

/* Common definition of interrupt bit masks */
#define HISI_SFC_V3XX_INT_MASK_ALL (0x1ff)	/* all the masks */
#define HISI_SFC_V3XX_INT_MASK_CPLT BIT(0)	/* command execution complete */
#define HISI_SFC_V3XX_INT_MASK_PP_ERR BIT(2)	/* page progrom error */
#define HISI_SFC_V3XX_INT_MASK_IACCES BIT(5)	/* error visiting inaccessible/
						 * protected address
						 */

/* IO Mode definition in HISI_SFC_V3XX_CMD_CFG */
#define HISI_SFC_V3XX_STD (0 << 17)
#define HISI_SFC_V3XX_DIDO (1 << 17)
#define HISI_SFC_V3XX_DIO (2 << 17)
#define HISI_SFC_V3XX_FULL_DIO (3 << 17)
#define HISI_SFC_V3XX_QIQO (5 << 17)
#define HISI_SFC_V3XX_QIO (6 << 17)
#define HISI_SFC_V3XX_FULL_QIO (7 << 17)

/*
 * The IO modes lookup table. hisi_sfc_v3xx_io_modes[(z - 1) / 2][y / 2][x / 2]
 * stands for x-y-z mode, as described in SFDP terminology. -EIO indicates
 * an invalid mode.
 */
static const int hisi_sfc_v3xx_io_modes[2][3][3] = {
	{
		{ HISI_SFC_V3XX_DIDO, HISI_SFC_V3XX_DIDO, HISI_SFC_V3XX_DIDO },
		{ HISI_SFC_V3XX_DIO, HISI_SFC_V3XX_FULL_DIO, -EIO },
		{ -EIO, -EIO, -EIO },
	},
	{
		{ HISI_SFC_V3XX_QIQO, HISI_SFC_V3XX_QIQO, HISI_SFC_V3XX_QIQO },
		{ -EIO, -EIO, -EIO },
		{ HISI_SFC_V3XX_QIO, -EIO, HISI_SFC_V3XX_FULL_QIO },
	},
};

struct hisi_sfc_v3xx_host {
	struct device *dev;
	void __iomem *regbase;
	int max_cmd_dword;
	struct completion *completion;
	u8 address_mode;
	int irq;
};

static void hisi_sfc_v3xx_disable_int(struct hisi_sfc_v3xx_host *host)
{
	writel(0, host->regbase + HISI_SFC_V3XX_INT_MASK);
}

static void hisi_sfc_v3xx_enable_int(struct hisi_sfc_v3xx_host *host)
{
	writel(HISI_SFC_V3XX_INT_MASK_ALL, host->regbase + HISI_SFC_V3XX_INT_MASK);
}

static void hisi_sfc_v3xx_clear_int(struct hisi_sfc_v3xx_host *host)
{
	writel(HISI_SFC_V3XX_INT_MASK_ALL, host->regbase + HISI_SFC_V3XX_INT_CLR);
}

/*
 * The interrupt status register indicates whether an error occurs
 * after per operation. Check it, and clear the interrupts for
 * next time judgement.
 */
static int hisi_sfc_v3xx_handle_completion(struct hisi_sfc_v3xx_host *host)
{
	u32 reg;

	reg = readl(host->regbase + HISI_SFC_V3XX_RAW_INT_STAT);
	hisi_sfc_v3xx_clear_int(host);

	if (reg & HISI_SFC_V3XX_INT_MASK_IACCES) {
		dev_err(host->dev, "fail to access protected address\n");
		return -EIO;
	}

	if (reg & HISI_SFC_V3XX_INT_MASK_PP_ERR) {
		dev_err(host->dev, "page program operation failed\n");
		return -EIO;
	}

	/*
	 * The other bits of the interrupt registers is not currently
	 * used and probably not be triggered in this driver. When it
	 * happens, we regard it as an unsupported error here.
	 */
	if (!(reg & HISI_SFC_V3XX_INT_MASK_CPLT)) {
		dev_err(host->dev, "unsupported error occurred, status=0x%x\n", reg);
		return -EIO;
	}

	return 0;
}

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
 * The controller only supports Standard SPI mode, Duall mode and
 * Quad mode. Double sanitize the ops here to avoid OOB access.
 */
static bool hisi_sfc_v3xx_supports_op(struct spi_mem *mem,
				      const struct spi_mem_op *op)
{
	struct spi_device *spi = mem->spi;
	struct hisi_sfc_v3xx_host *host;

	host = spi_controller_get_devdata(spi->master);

	if (op->data.buswidth > 4 || op->dummy.buswidth > 4 ||
	    op->addr.buswidth > 4 || op->cmd.buswidth > 4)
		return false;

	if (op->addr.nbytes != host->address_mode && op->addr.nbytes)
		return false;

	return spi_mem_default_supports_op(mem, op);
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

static int hisi_sfc_v3xx_start_bus(struct hisi_sfc_v3xx_host *host,
				   const struct spi_mem_op *op,
				   u8 chip_select)
{
	int len = op->data.nbytes, buswidth_mode;
	u32 config = 0;

	if (op->addr.nbytes)
		config |= HISI_SFC_V3XX_CMD_CFG_ADDR_EN_MSK;

	if (op->data.buswidth == 0 || op->data.buswidth == 1) {
		buswidth_mode = HISI_SFC_V3XX_STD;
	} else {
		int data_idx, addr_idx, cmd_idx;

		data_idx = (op->data.buswidth - 1) / 2;
		addr_idx = op->addr.buswidth / 2;
		cmd_idx = op->cmd.buswidth / 2;
		buswidth_mode = hisi_sfc_v3xx_io_modes[data_idx][addr_idx][cmd_idx];
	}
	if (buswidth_mode < 0)
		return buswidth_mode;
	config |= buswidth_mode;

	if (op->data.dir != SPI_MEM_NO_DATA) {
		config |= (len - 1) << HISI_SFC_V3XX_CMD_CFG_DATA_CNT_OFF;
		config |= HISI_SFC_V3XX_CMD_CFG_DATA_EN_MSK;
	}

	if (op->data.dir == SPI_MEM_DATA_IN)
		config |= HISI_SFC_V3XX_CMD_CFG_RW_MSK;

	config |= op->dummy.nbytes << HISI_SFC_V3XX_CMD_CFG_DUMMY_CNT_OFF |
		  chip_select << HISI_SFC_V3XX_CMD_CFG_CS_SEL_OFF |
		  HISI_SFC_V3XX_CMD_CFG_START_MSK;

	writel(op->addr.val, host->regbase + HISI_SFC_V3XX_CMD_ADDR);
	writel(op->cmd.opcode, host->regbase + HISI_SFC_V3XX_CMD_INS);

	writel(config, host->regbase + HISI_SFC_V3XX_CMD_CFG);

	return 0;
}

static int hisi_sfc_v3xx_generic_exec_op(struct hisi_sfc_v3xx_host *host,
					 const struct spi_mem_op *op,
					 u8 chip_select)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int ret;

	if (host->irq) {
		host->completion = &done;
		hisi_sfc_v3xx_enable_int(host);
	}

	if (op->data.dir == SPI_MEM_DATA_OUT)
		hisi_sfc_v3xx_write_databuf(host, op->data.buf.out, op->data.nbytes);

	ret = hisi_sfc_v3xx_start_bus(host, op, chip_select);
	if (ret)
		return ret;

	if (host->irq) {
		ret = wait_for_completion_timeout(host->completion,
						  usecs_to_jiffies(HISI_SFC_V3XX_WAIT_TIMEOUT_US));
		if (!ret)
			ret = -ETIMEDOUT;
		else
			ret = 0;

		hisi_sfc_v3xx_disable_int(host);
		synchronize_irq(host->irq);
		host->completion = NULL;
	} else {
		ret = hisi_sfc_v3xx_wait_cmd_idle(host);
	}
	if (hisi_sfc_v3xx_handle_completion(host) || ret)
		return -EIO;

	if (op->data.dir == SPI_MEM_DATA_IN)
		hisi_sfc_v3xx_read_databuf(host, op->data.buf.in, op->data.nbytes);

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
	.supports_op = hisi_sfc_v3xx_supports_op,
	.exec_op = hisi_sfc_v3xx_exec_op,
};

static irqreturn_t hisi_sfc_v3xx_isr(int irq, void *data)
{
	struct hisi_sfc_v3xx_host *host = data;

	hisi_sfc_v3xx_disable_int(host);

	complete(host->completion);

	return IRQ_HANDLED;
}

static int hisi_sfc_v3xx_buswidth_override_bits;

/*
 * ACPI FW does not allow us to currently set the device buswidth, so quirk it
 * depending on the board.
 */
static int __init hisi_sfc_v3xx_dmi_quirk(const struct dmi_system_id *d)
{
	hisi_sfc_v3xx_buswidth_override_bits = SPI_RX_QUAD | SPI_TX_QUAD;

	return 0;
}

static const struct dmi_system_id hisi_sfc_v3xx_dmi_quirk_table[]  = {
	{
	.callback = hisi_sfc_v3xx_dmi_quirk,
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Huawei"),
		DMI_MATCH(DMI_PRODUCT_NAME, "D06"),
	},
	},
	{
	.callback = hisi_sfc_v3xx_dmi_quirk,
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Huawei"),
		DMI_MATCH(DMI_PRODUCT_NAME, "TaiShan 2280 V2"),
	},
	},
	{
	.callback = hisi_sfc_v3xx_dmi_quirk,
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Huawei"),
		DMI_MATCH(DMI_PRODUCT_NAME, "TaiShan 200 (Model 2280)"),
	},
	},
	{}
};

static int hisi_sfc_v3xx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hisi_sfc_v3xx_host *host;
	struct spi_controller *ctlr;
	u32 version, glb_config;
	int ret;

	ctlr = spi_alloc_master(&pdev->dev, sizeof(*host));
	if (!ctlr)
		return -ENOMEM;

	ctlr->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD |
			  SPI_TX_DUAL | SPI_TX_QUAD;

	ctlr->buswidth_override_bits = hisi_sfc_v3xx_buswidth_override_bits;

	host = spi_controller_get_devdata(ctlr);
	host->dev = dev;

	platform_set_drvdata(pdev, host);

	host->regbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->regbase)) {
		ret = PTR_ERR(host->regbase);
		goto err_put_master;
	}

	host->irq = platform_get_irq_optional(pdev, 0);
	if (host->irq == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err_put_master;
	}

	hisi_sfc_v3xx_disable_int(host);

	if (host->irq > 0) {
		ret = devm_request_irq(dev, host->irq, hisi_sfc_v3xx_isr, 0,
				       "hisi-sfc-v3xx", host);

		if (ret) {
			dev_err(dev, "failed to request irq%d, ret = %d\n", host->irq, ret);
			host->irq = 0;
		}
	} else {
		host->irq = 0;
	}

	ctlr->bus_num = -1;
	ctlr->num_chipselect = 1;
	ctlr->mem_ops = &hisi_sfc_v3xx_mem_ops;

	/*
	 * The address mode of the controller is either 3 or 4,
	 * which is indicated by the address mode bit in
	 * the global config register. The register is read only
	 * for the OS driver.
	 */
	glb_config = readl(host->regbase + HISI_SFC_V3XX_GLB_CFG);
	if (glb_config & HISI_SFC_V3XX_GLB_CFG_CS0_ADDR_MODE)
		host->address_mode = 4;
	else
		host->address_mode = 3;

	version = readl(host->regbase + HISI_SFC_V3XX_VERSION);

	if (version >= 0x351)
		host->max_cmd_dword = 64;
	else
		host->max_cmd_dword = 16;

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret)
		goto err_put_master;

	dev_info(&pdev->dev, "hw version 0x%x, %s mode.\n",
		 version, host->irq ? "irq" : "polling");

	return 0;

err_put_master:
	spi_master_put(ctlr);
	return ret;
}

static const struct acpi_device_id hisi_sfc_v3xx_acpi_ids[] = {
	{"HISI0341", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_sfc_v3xx_acpi_ids);

static struct platform_driver hisi_sfc_v3xx_spi_driver = {
	.driver = {
		.name	= "hisi-sfc-v3xx",
		.acpi_match_table = hisi_sfc_v3xx_acpi_ids,
	},
	.probe	= hisi_sfc_v3xx_probe,
};

static int __init hisi_sfc_v3xx_spi_init(void)
{
	dmi_check_system(hisi_sfc_v3xx_dmi_quirk_table);

	return platform_driver_register(&hisi_sfc_v3xx_spi_driver);
}

static void __exit hisi_sfc_v3xx_spi_exit(void)
{
	platform_driver_unregister(&hisi_sfc_v3xx_spi_driver);
}

module_init(hisi_sfc_v3xx_spi_init);
module_exit(hisi_sfc_v3xx_spi_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HiSilicon SPI NOR V3XX Flash Controller Driver for hi16xx chipsets");
