// SPDX-License-Identifier: GPL-2.0-only
//
// HiSilicon SPI Controller Driver for Kunpeng SoCs
//
// Copyright (c) 2021 HiSilicon Technologies Co., Ltd.
// Author: Jay Fang <f.fangjian@huawei.com>
//
// This code is based on spi-dw-core.c.

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

/* Register offsets */
#define HISI_SPI_CSCR		0x00	/* cs control register */
#define HISI_SPI_CR		0x04	/* spi common control register */
#define HISI_SPI_ENR		0x08	/* spi enable register */
#define HISI_SPI_FIFOC		0x0c	/* fifo level control register */
#define HISI_SPI_IMR		0x10	/* interrupt mask register */
#define HISI_SPI_DIN		0x14	/* data in register */
#define HISI_SPI_DOUT		0x18	/* data out register */
#define HISI_SPI_SR		0x1c	/* status register */
#define HISI_SPI_RISR		0x20	/* raw interrupt status register */
#define HISI_SPI_ISR		0x24	/* interrupt status register */
#define HISI_SPI_ICR		0x28	/* interrupt clear register */
#define HISI_SPI_VERSION	0xe0	/* version register */

/* Bit fields in HISI_SPI_CR */
#define CR_LOOP_MASK		GENMASK(1, 1)
#define CR_CPOL_MASK		GENMASK(2, 2)
#define CR_CPHA_MASK		GENMASK(3, 3)
#define CR_DIV_PRE_MASK		GENMASK(11, 4)
#define CR_DIV_POST_MASK	GENMASK(19, 12)
#define CR_BPW_MASK		GENMASK(24, 20)
#define CR_SPD_MODE_MASK	GENMASK(25, 25)

/* Bit fields in HISI_SPI_FIFOC */
#define FIFOC_TX_MASK		GENMASK(5, 3)
#define FIFOC_RX_MASK		GENMASK(11, 9)

/* Bit fields in HISI_SPI_IMR, 4 bits */
#define IMR_RXOF		BIT(0)		/* Receive Overflow */
#define IMR_RXTO		BIT(1)		/* Receive Timeout */
#define IMR_RX			BIT(2)		/* Receive */
#define IMR_TX			BIT(3)		/* Transmit */
#define IMR_MASK		(IMR_RXOF | IMR_RXTO | IMR_RX | IMR_TX)

/* Bit fields in HISI_SPI_SR, 5 bits */
#define SR_TXE			BIT(0)		/* Transmit FIFO empty */
#define SR_TXNF			BIT(1)		/* Transmit FIFO not full */
#define SR_RXNE			BIT(2)		/* Receive FIFO not empty */
#define SR_RXF			BIT(3)		/* Receive FIFO full */
#define SR_BUSY			BIT(4)		/* Busy Flag */

/* Bit fields in HISI_SPI_ISR, 4 bits */
#define ISR_RXOF		BIT(0)		/* Receive Overflow */
#define ISR_RXTO		BIT(1)		/* Receive Timeout */
#define ISR_RX			BIT(2)		/* Receive */
#define ISR_TX			BIT(3)		/* Transmit */
#define ISR_MASK		(ISR_RXOF | ISR_RXTO | ISR_RX | ISR_TX)

/* Bit fields in HISI_SPI_ICR, 2 bits */
#define ICR_RXOF		BIT(0)		/* Receive Overflow */
#define ICR_RXTO		BIT(1)		/* Receive Timeout */
#define ICR_MASK		(ICR_RXOF | ICR_RXTO)

#define DIV_POST_MAX		0xFF
#define DIV_POST_MIN		0x00
#define DIV_PRE_MAX		0xFE
#define DIV_PRE_MIN		0x02
#define CLK_DIV_MAX		((1 + DIV_POST_MAX) * DIV_PRE_MAX)
#define CLK_DIV_MIN		((1 + DIV_POST_MIN) * DIV_PRE_MIN)

#define DEFAULT_NUM_CS		1

#define HISI_SPI_WAIT_TIMEOUT_MS	10UL

enum hisi_spi_rx_level_trig {
	HISI_SPI_RX_1,
	HISI_SPI_RX_4,
	HISI_SPI_RX_8,
	HISI_SPI_RX_16,
	HISI_SPI_RX_32,
	HISI_SPI_RX_64,
	HISI_SPI_RX_128
};

enum hisi_spi_tx_level_trig {
	HISI_SPI_TX_1_OR_LESS,
	HISI_SPI_TX_4_OR_LESS,
	HISI_SPI_TX_8_OR_LESS,
	HISI_SPI_TX_16_OR_LESS,
	HISI_SPI_TX_32_OR_LESS,
	HISI_SPI_TX_64_OR_LESS,
	HISI_SPI_TX_128_OR_LESS
};

enum hisi_spi_frame_n_bytes {
	HISI_SPI_N_BYTES_NULL,
	HISI_SPI_N_BYTES_U8,
	HISI_SPI_N_BYTES_U16,
	HISI_SPI_N_BYTES_U32 = 4
};

/* Slave spi_dev related */
struct hisi_chip_data {
	u32 cr;
	u32 speed_hz;	/* baud rate */
	u16 clk_div;	/* baud rate divider */

	/* clk_div = (1 + div_post) * div_pre */
	u8 div_post;	/* value from 0 to 255 */
	u8 div_pre;	/* value from 2 to 254 (even only!) */
};

struct hisi_spi {
	struct device		*dev;

	void __iomem		*regs;
	int			irq;
	u32			fifo_len; /* depth of the FIFO buffer */

	/* Current message transfer state info */
	const void		*tx;
	unsigned int		tx_len;
	void			*rx;
	unsigned int		rx_len;
	u8			n_bytes; /* current is a 1/2/4 bytes op */

	struct dentry *debugfs;
	struct debugfs_regset32 regset;
};

#define HISI_SPI_DBGFS_REG(_name, _off)	\
{					\
	.name = _name,			\
	.offset = _off,			\
}

static const struct debugfs_reg32 hisi_spi_regs[] = {
	HISI_SPI_DBGFS_REG("CSCR", HISI_SPI_CSCR),
	HISI_SPI_DBGFS_REG("CR", HISI_SPI_CR),
	HISI_SPI_DBGFS_REG("ENR", HISI_SPI_ENR),
	HISI_SPI_DBGFS_REG("FIFOC", HISI_SPI_FIFOC),
	HISI_SPI_DBGFS_REG("IMR", HISI_SPI_IMR),
	HISI_SPI_DBGFS_REG("DIN", HISI_SPI_DIN),
	HISI_SPI_DBGFS_REG("DOUT", HISI_SPI_DOUT),
	HISI_SPI_DBGFS_REG("SR", HISI_SPI_SR),
	HISI_SPI_DBGFS_REG("RISR", HISI_SPI_RISR),
	HISI_SPI_DBGFS_REG("ISR", HISI_SPI_ISR),
	HISI_SPI_DBGFS_REG("ICR", HISI_SPI_ICR),
	HISI_SPI_DBGFS_REG("VERSION", HISI_SPI_VERSION),
};

static int hisi_spi_debugfs_init(struct hisi_spi *hs)
{
	char name[32];

	struct spi_controller *master;

	master = container_of(hs->dev, struct spi_controller, dev);
	snprintf(name, 32, "hisi_spi%d", master->bus_num);
	hs->debugfs = debugfs_create_dir(name, NULL);
	if (!hs->debugfs)
		return -ENOMEM;

	hs->regset.regs = hisi_spi_regs;
	hs->regset.nregs = ARRAY_SIZE(hisi_spi_regs);
	hs->regset.base = hs->regs;
	debugfs_create_regset32("registers", 0400, hs->debugfs, &hs->regset);

	return 0;
}

static u32 hisi_spi_busy(struct hisi_spi *hs)
{
	return readl(hs->regs + HISI_SPI_SR) & SR_BUSY;
}

static u32 hisi_spi_rx_not_empty(struct hisi_spi *hs)
{
	return readl(hs->regs + HISI_SPI_SR) & SR_RXNE;
}

static u32 hisi_spi_tx_not_full(struct hisi_spi *hs)
{
	return readl(hs->regs + HISI_SPI_SR) & SR_TXNF;
}

static void hisi_spi_flush_fifo(struct hisi_spi *hs)
{
	unsigned long limit = loops_per_jiffy << 1;

	do {
		while (hisi_spi_rx_not_empty(hs))
			readl(hs->regs + HISI_SPI_DOUT);
	} while (hisi_spi_busy(hs) && limit--);
}

/* Disable the controller and all interrupts */
static void hisi_spi_disable(struct hisi_spi *hs)
{
	writel(0, hs->regs + HISI_SPI_ENR);
	writel(IMR_MASK, hs->regs + HISI_SPI_IMR);
	writel(ICR_MASK, hs->regs + HISI_SPI_ICR);
}

static u8 hisi_spi_n_bytes(struct spi_transfer *transfer)
{
	if (transfer->bits_per_word <= 8)
		return HISI_SPI_N_BYTES_U8;
	else if (transfer->bits_per_word <= 16)
		return HISI_SPI_N_BYTES_U16;
	else
		return HISI_SPI_N_BYTES_U32;
}

static void hisi_spi_reader(struct hisi_spi *hs)
{
	u32 max = min_t(u32, hs->rx_len, hs->fifo_len);
	u32 rxw;

	while (hisi_spi_rx_not_empty(hs) && max--) {
		rxw = readl(hs->regs + HISI_SPI_DOUT);
		/* Check the transfer's original "rx" is not null */
		if (hs->rx) {
			switch (hs->n_bytes) {
			case HISI_SPI_N_BYTES_U8:
				*(u8 *)(hs->rx) = rxw;
				break;
			case HISI_SPI_N_BYTES_U16:
				*(u16 *)(hs->rx) = rxw;
				break;
			case HISI_SPI_N_BYTES_U32:
				*(u32 *)(hs->rx) = rxw;
				break;
			}
			hs->rx += hs->n_bytes;
		}
		--hs->rx_len;
	}
}

static void hisi_spi_writer(struct hisi_spi *hs)
{
	u32 max = min_t(u32, hs->tx_len, hs->fifo_len);
	u32 txw = 0;

	while (hisi_spi_tx_not_full(hs) && max--) {
		/* Check the transfer's original "tx" is not null */
		if (hs->tx) {
			switch (hs->n_bytes) {
			case HISI_SPI_N_BYTES_U8:
				txw = *(u8 *)(hs->tx);
				break;
			case HISI_SPI_N_BYTES_U16:
				txw = *(u16 *)(hs->tx);
				break;
			case HISI_SPI_N_BYTES_U32:
				txw = *(u32 *)(hs->tx);
				break;
			}
			hs->tx += hs->n_bytes;
		}
		writel(txw, hs->regs + HISI_SPI_DIN);
		--hs->tx_len;
	}
}

static void __hisi_calc_div_reg(struct hisi_chip_data *chip)
{
	chip->div_pre = DIV_PRE_MAX;
	while (chip->div_pre >= DIV_PRE_MIN) {
		if (chip->clk_div % chip->div_pre == 0)
			break;

		chip->div_pre -= 2;
	}

	if (chip->div_pre > chip->clk_div)
		chip->div_pre = chip->clk_div;

	chip->div_post = (chip->clk_div / chip->div_pre) - 1;
}

static u32 hisi_calc_effective_speed(struct spi_controller *master,
			struct hisi_chip_data *chip, u32 speed_hz)
{
	u32 effective_speed;

	/* Note clock divider doesn't support odd numbers */
	chip->clk_div = DIV_ROUND_UP(master->max_speed_hz, speed_hz) + 1;
	chip->clk_div &= 0xfffe;
	if (chip->clk_div > CLK_DIV_MAX)
		chip->clk_div = CLK_DIV_MAX;

	effective_speed = master->max_speed_hz / chip->clk_div;
	if (chip->speed_hz != effective_speed) {
		__hisi_calc_div_reg(chip);
		chip->speed_hz = effective_speed;
	}

	return effective_speed;
}

static u32 hisi_spi_prepare_cr(struct spi_device *spi)
{
	u32 cr = FIELD_PREP(CR_SPD_MODE_MASK, 1);

	cr |= FIELD_PREP(CR_CPHA_MASK, (spi->mode & SPI_CPHA) ? 1 : 0);
	cr |= FIELD_PREP(CR_CPOL_MASK, (spi->mode & SPI_CPOL) ? 1 : 0);
	cr |= FIELD_PREP(CR_LOOP_MASK, (spi->mode & SPI_LOOP) ? 1 : 0);

	return cr;
}

static void hisi_spi_hw_init(struct hisi_spi *hs)
{
	hisi_spi_disable(hs);

	/* FIFO default config */
	writel(FIELD_PREP(FIFOC_TX_MASK, HISI_SPI_TX_64_OR_LESS) |
		FIELD_PREP(FIFOC_RX_MASK, HISI_SPI_RX_16),
		hs->regs + HISI_SPI_FIFOC);

	hs->fifo_len = 256;
}

static irqreturn_t hisi_spi_irq(int irq, void *dev_id)
{
	struct spi_controller *master = dev_id;
	struct hisi_spi *hs = spi_controller_get_devdata(master);
	u32 irq_status = readl(hs->regs + HISI_SPI_ISR) & ISR_MASK;

	if (!irq_status)
		return IRQ_NONE;

	if (!master->cur_msg)
		return IRQ_HANDLED;

	/* Error handling */
	if (irq_status & ISR_RXOF) {
		dev_err(hs->dev, "interrupt_transfer: fifo overflow\n");
		master->cur_msg->status = -EIO;
		goto finalize_transfer;
	}

	/*
	 * Read data from the Rx FIFO every time. If there is
	 * nothing left to receive, finalize the transfer.
	 */
	hisi_spi_reader(hs);
	if (!hs->rx_len)
		goto finalize_transfer;

	/* Send data out when Tx FIFO IRQ triggered */
	if (irq_status & ISR_TX)
		hisi_spi_writer(hs);

	return IRQ_HANDLED;

finalize_transfer:
	hisi_spi_disable(hs);
	spi_finalize_current_transfer(master);
	return IRQ_HANDLED;
}

static int hisi_spi_transfer_one(struct spi_controller *master,
		struct spi_device *spi, struct spi_transfer *transfer)
{
	struct hisi_spi *hs = spi_controller_get_devdata(master);
	struct hisi_chip_data *chip = spi_get_ctldata(spi);
	u32 cr = chip->cr;

	/* Update per transfer options for speed and bpw */
	transfer->effective_speed_hz =
		hisi_calc_effective_speed(master, chip, transfer->speed_hz);
	cr |= FIELD_PREP(CR_DIV_PRE_MASK, chip->div_pre);
	cr |= FIELD_PREP(CR_DIV_POST_MASK, chip->div_post);
	cr |= FIELD_PREP(CR_BPW_MASK, transfer->bits_per_word - 1);
	writel(cr, hs->regs + HISI_SPI_CR);

	hisi_spi_flush_fifo(hs);

	hs->n_bytes = hisi_spi_n_bytes(transfer);
	hs->tx = transfer->tx_buf;
	hs->tx_len = transfer->len / hs->n_bytes;
	hs->rx = transfer->rx_buf;
	hs->rx_len = hs->tx_len;

	/*
	 * Ensure that the transfer data above has been updated
	 * before the interrupt to start.
	 */
	smp_mb();

	/* Enable all interrupts and the controller */
	writel(~(u32)IMR_MASK, hs->regs + HISI_SPI_IMR);
	writel(1, hs->regs + HISI_SPI_ENR);

	return 1;
}

static void hisi_spi_handle_err(struct spi_controller *master,
		struct spi_message *msg)
{
	struct hisi_spi *hs = spi_controller_get_devdata(master);

	hisi_spi_disable(hs);

	/*
	 * Wait for interrupt handler that is
	 * already in timeout to complete.
	 */
	msleep(HISI_SPI_WAIT_TIMEOUT_MS);
}

static int hisi_spi_setup(struct spi_device *spi)
{
	struct hisi_chip_data *chip;

	/* Only alloc on first setup */
	chip = spi_get_ctldata(spi);
	if (!chip) {
		chip = kzalloc(sizeof(*chip), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;
		spi_set_ctldata(spi, chip);
	}

	chip->cr = hisi_spi_prepare_cr(spi);

	return 0;
}

static void hisi_spi_cleanup(struct spi_device *spi)
{
	struct hisi_chip_data *chip = spi_get_ctldata(spi);

	kfree(chip);
	spi_set_ctldata(spi, NULL);
}

static int hisi_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *master;
	struct hisi_spi *hs;
	int ret, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	master = devm_spi_alloc_master(dev, sizeof(*hs));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	hs = spi_controller_get_devdata(master);
	hs->dev = dev;
	hs->irq = irq;

	hs->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hs->regs))
		return PTR_ERR(hs->regs);

	/* Specify maximum SPI clocking speed (master only) by firmware */
	ret = device_property_read_u32(dev, "spi-max-frequency",
					&master->max_speed_hz);
	if (ret) {
		dev_err(dev, "failed to get max SPI clocking speed, ret=%d\n",
			ret);
		return -EINVAL;
	}

	ret = device_property_read_u16(dev, "num-cs",
					&master->num_chipselect);
	if (ret)
		master->num_chipselect = DEFAULT_NUM_CS;

	master->use_gpio_descriptors = true;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LOOP;
	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
	master->bus_num = pdev->id;
	master->setup = hisi_spi_setup;
	master->cleanup = hisi_spi_cleanup;
	master->transfer_one = hisi_spi_transfer_one;
	master->handle_err = hisi_spi_handle_err;
	master->dev.fwnode = dev->fwnode;

	hisi_spi_hw_init(hs);

	ret = devm_request_irq(dev, hs->irq, hisi_spi_irq, 0, dev_name(dev),
			master);
	if (ret < 0) {
		dev_err(dev, "failed to get IRQ=%d, ret=%d\n", hs->irq, ret);
		return ret;
	}

	ret = spi_register_controller(master);
	if (ret) {
		dev_err(dev, "failed to register spi master, ret=%d\n", ret);
		return ret;
	}

	if (hisi_spi_debugfs_init(hs))
		dev_info(dev, "failed to create debugfs dir\n");

	dev_info(dev, "hw version:0x%x max-freq:%u kHz\n",
		readl(hs->regs + HISI_SPI_VERSION),
		master->max_speed_hz / 1000);

	return 0;
}

static int hisi_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *master = platform_get_drvdata(pdev);
	struct hisi_spi *hs = spi_controller_get_devdata(master);

	debugfs_remove_recursive(hs->debugfs);
	spi_unregister_controller(master);

	return 0;
}

static const struct acpi_device_id hisi_spi_acpi_match[] = {
	{"HISI03E1", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_spi_acpi_match);

static struct platform_driver hisi_spi_driver = {
	.probe		= hisi_spi_probe,
	.remove		= hisi_spi_remove,
	.driver		= {
		.name	= "hisi-kunpeng-spi",
		.acpi_match_table = hisi_spi_acpi_match,
	},
};
module_platform_driver(hisi_spi_driver);

MODULE_AUTHOR("Jay Fang <f.fangjian@huawei.com>");
MODULE_DESCRIPTION("HiSilicon SPI Controller Driver for Kunpeng SoCs");
MODULE_LICENSE("GPL v2");
