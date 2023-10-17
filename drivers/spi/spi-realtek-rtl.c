// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>

struct rtspi {
	void __iomem *base;
};

/* SPI Flash Configuration Register */
#define RTL_SPI_SFCR			0x00
#define RTL_SPI_SFCR_RBO		BIT(28)
#define RTL_SPI_SFCR_WBO		BIT(27)

/* SPI Flash Control and Status Register */
#define RTL_SPI_SFCSR			0x08
#define RTL_SPI_SFCSR_CSB0		BIT(31)
#define RTL_SPI_SFCSR_CSB1		BIT(30)
#define RTL_SPI_SFCSR_RDY		BIT(27)
#define RTL_SPI_SFCSR_CS		BIT(24)
#define RTL_SPI_SFCSR_LEN_MASK		~(0x03 << 28)
#define RTL_SPI_SFCSR_LEN1		(0x00 << 28)
#define RTL_SPI_SFCSR_LEN4		(0x03 << 28)

/* SPI Flash Data Register */
#define RTL_SPI_SFDR			0x0c

#define REG(x)		(rtspi->base + x)


static void rt_set_cs(struct spi_device *spi, bool active)
{
	struct rtspi *rtspi = spi_controller_get_devdata(spi->controller);
	u32 value;

	/* CS0 bit is active low */
	value = readl(REG(RTL_SPI_SFCSR));
	if (active)
		value |= RTL_SPI_SFCSR_CSB0;
	else
		value &= ~RTL_SPI_SFCSR_CSB0;
	writel(value, REG(RTL_SPI_SFCSR));
}

static void set_size(struct rtspi *rtspi, int size)
{
	u32 value;

	value = readl(REG(RTL_SPI_SFCSR));
	value &= RTL_SPI_SFCSR_LEN_MASK;
	if (size == 4)
		value |= RTL_SPI_SFCSR_LEN4;
	else if (size == 1)
		value |= RTL_SPI_SFCSR_LEN1;
	writel(value, REG(RTL_SPI_SFCSR));
}

static inline void wait_ready(struct rtspi *rtspi)
{
	while (!(readl(REG(RTL_SPI_SFCSR)) & RTL_SPI_SFCSR_RDY))
		cpu_relax();
}
static void send4(struct rtspi *rtspi, const u32 *buf)
{
	wait_ready(rtspi);
	set_size(rtspi, 4);
	writel(*buf, REG(RTL_SPI_SFDR));
}

static void send1(struct rtspi *rtspi, const u8 *buf)
{
	wait_ready(rtspi);
	set_size(rtspi, 1);
	writel(buf[0] << 24, REG(RTL_SPI_SFDR));
}

static void rcv4(struct rtspi *rtspi, u32 *buf)
{
	wait_ready(rtspi);
	set_size(rtspi, 4);
	*buf = readl(REG(RTL_SPI_SFDR));
}

static void rcv1(struct rtspi *rtspi, u8 *buf)
{
	wait_ready(rtspi);
	set_size(rtspi, 1);
	*buf = readl(REG(RTL_SPI_SFDR)) >> 24;
}

static int transfer_one(struct spi_controller *ctrl, struct spi_device *spi,
			struct spi_transfer *xfer)
{
	struct rtspi *rtspi = spi_controller_get_devdata(ctrl);
	void *rx_buf;
	const void *tx_buf;
	int cnt;

	tx_buf = xfer->tx_buf;
	rx_buf = xfer->rx_buf;
	cnt = xfer->len;
	if (tx_buf) {
		while (cnt >= 4) {
			send4(rtspi, tx_buf);
			tx_buf += 4;
			cnt -= 4;
		}
		while (cnt) {
			send1(rtspi, tx_buf);
			tx_buf++;
			cnt--;
		}
	} else if (rx_buf) {
		while (cnt >= 4) {
			rcv4(rtspi, rx_buf);
			rx_buf += 4;
			cnt -= 4;
		}
		while (cnt) {
			rcv1(rtspi, rx_buf);
			rx_buf++;
			cnt--;
		}
	}

	spi_finalize_current_transfer(ctrl);

	return 0;
}

static void init_hw(struct rtspi *rtspi)
{
	u32 value;

	/* Turn on big-endian byte ordering */
	value = readl(REG(RTL_SPI_SFCR));
	value |= RTL_SPI_SFCR_RBO | RTL_SPI_SFCR_WBO;
	writel(value, REG(RTL_SPI_SFCR));

	value = readl(REG(RTL_SPI_SFCSR));
	/* Permanently disable CS1, since it's never used */
	value |= RTL_SPI_SFCSR_CSB1;
	/* Select CS0 for use */
	value &= RTL_SPI_SFCSR_CS;
	writel(value, REG(RTL_SPI_SFCSR));
}

static int realtek_rtl_spi_probe(struct platform_device *pdev)
{
	struct spi_controller *ctrl;
	struct rtspi *rtspi;
	int err;

	ctrl = devm_spi_alloc_host(&pdev->dev, sizeof(*rtspi));
	if (!ctrl) {
		dev_err(&pdev->dev, "Error allocating SPI controller\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, ctrl);
	rtspi = spi_controller_get_devdata(ctrl);

	rtspi->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(rtspi->base)) {
		dev_err(&pdev->dev, "Could not map SPI register address");
		return -ENOMEM;
	}

	init_hw(rtspi);

	ctrl->dev.of_node = pdev->dev.of_node;
	ctrl->flags = SPI_CONTROLLER_HALF_DUPLEX;
	ctrl->set_cs = rt_set_cs;
	ctrl->transfer_one = transfer_one;

	err = devm_spi_register_controller(&pdev->dev, ctrl);
	if (err) {
		dev_err(&pdev->dev, "Could not register SPI controller\n");
		return -ENODEV;
	}

	return 0;
}


static const struct of_device_id realtek_rtl_spi_of_ids[] = {
	{ .compatible = "realtek,rtl8380-spi" },
	{ .compatible = "realtek,rtl8382-spi" },
	{ .compatible = "realtek,rtl8391-spi" },
	{ .compatible = "realtek,rtl8392-spi" },
	{ .compatible = "realtek,rtl8393-spi" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, realtek_rtl_spi_of_ids);

static struct platform_driver realtek_rtl_spi_driver = {
	.probe = realtek_rtl_spi_probe,
	.driver = {
		.name = "realtek-rtl-spi",
		.of_match_table = realtek_rtl_spi_of_ids,
	},
};

module_platform_driver(realtek_rtl_spi_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bert Vermeulen <bert@biot.com>");
MODULE_DESCRIPTION("Realtek RTL SPI driver");
