// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) KEBA Industrial Automation Gmbh 2024
 *
 * Driver for KEBA SPI host controller type 2 FPGA IP core
 */

#include <linux/iopoll.h>
#include <linux/misc/keba.h>
#include <linux/spi/spi.h>

#define KSPI2 "kspi2"

#define KSPI2_CLK_FREQ_REG	0x03
#define  KSPI2_CLK_FREQ_MASK	0x0f
#define  KSPI2_CLK_FREQ_62_5M	0x0
#define  KSPI2_CLK_FREQ_33_3M	0x1
#define  KSPI2_CLK_FREQ_125M	0x2
#define  KSPI2_CLK_FREQ_50M	0x3
#define  KSPI2_CLK_FREQ_100M	0x4

#define KSPI2_CONTROL_REG		0x04
#define  KSPI2_CONTROL_CLK_DIV_MAX	0x0f
#define  KSPI2_CONTROL_CLK_DIV_MASK	0x0f
#define  KSPI2_CONTROL_CPHA		0x10
#define  KSPI2_CONTROL_CPOL		0x20
#define  KSPI2_CONTROL_CLK_MODE_MASK	0x30
#define  KSPI2_CONTROL_INIT		KSPI2_CONTROL_CLK_DIV_MAX

#define KSPI2_STATUS_REG	0x08
#define  KSPI2_STATUS_IN_USE	0x01
#define  KSPI2_STATUS_BUSY	0x02

#define KSPI2_DATA_REG	0x0c

#define KSPI2_CS_NR_REG		0x10
#define  KSPI2_CS_NR_NONE	0xff

#define KSPI2_MODE_BITS	(SPI_CPHA | SPI_CPOL)
#define KSPI2_NUM_CS	255

#define KSPI2_SPEED_HZ_MIN(kspi)	(kspi->base_speed_hz / 65536)
#define KSPI2_SPEED_HZ_MAX(kspi)	(kspi->base_speed_hz / 2)

/* timeout is 10 times the time to transfer one byte at slowest clock */
#define KSPI2_XFER_TIMEOUT_US(kspi)	(USEC_PER_SEC / \
					 KSPI2_SPEED_HZ_MIN(kspi) * 8 * 10)

#define KSPI2_INUSE_SLEEP_US	(2 * USEC_PER_MSEC)
#define KSPI2_INUSE_TIMEOUT_US	(10 * USEC_PER_SEC)

struct kspi2 {
	struct keba_spi_auxdev *auxdev;
	void __iomem *base;
	struct spi_controller *host;

	u32 base_speed_hz; /* SPI base clock frequency in HZ */
	u8 control_shadow;

	struct spi_device **device;
	int device_size;
};

static int kspi2_inuse_lock(struct kspi2 *kspi)
{
	u8 sts;
	int ret;

	/*
	 * The SPI controller has an IN_USE bit for locking access to the
	 * controller. This enables the use of the SPI controller by other none
	 * Linux processors.
	 *
	 * If the SPI controller is free, then the first read returns
	 * IN_USE == 0. After that the SPI controller is locked and further
	 * reads of IN_USE return 1.
	 *
	 * The SPI controller is unlocked by writing 1 into IN_USE.
	 *
	 * The IN_USE bit acts as a hardware semaphore for the SPI controller.
	 * Poll for semaphore, but sleep while polling to free the CPU.
	 */
	ret = readb_poll_timeout(kspi->base + KSPI2_STATUS_REG,
				 sts, (sts & KSPI2_STATUS_IN_USE) == 0,
				 KSPI2_INUSE_SLEEP_US, KSPI2_INUSE_TIMEOUT_US);
	if (ret != 0)
		dev_warn(&kspi->auxdev->auxdev.dev, "%s err!\n", __func__);

	return ret;
}

static void kspi2_inuse_unlock(struct kspi2 *kspi)
{
	/* unlock the controller by writing 1 into IN_USE */
	iowrite8(KSPI2_STATUS_IN_USE, kspi->base + KSPI2_STATUS_REG);
}

static int kspi2_prepare_hardware(struct spi_controller *host)
{
	struct kspi2 *kspi = spi_controller_get_devdata(host);

	/* lock hardware semaphore before actual use of controller */
	return kspi2_inuse_lock(kspi);
}

static int kspi2_unprepare_hardware(struct spi_controller *host)
{
	struct kspi2 *kspi = spi_controller_get_devdata(host);

	/* unlock hardware semaphore after actual use of controller */
	kspi2_inuse_unlock(kspi);

	return 0;
}

static u8 kspi2_calc_minimal_divider(struct kspi2 *kspi, u32 max_speed_hz)
{
	u8 div;

	/*
	 * Divider values 2, 4, 8, 16, ..., 65536 are possible. They are coded
	 * as 0, 1, 2, 3, ..., 15 in the CONTROL_CLK_DIV bit.
	 */
	for (div = 0; div < KSPI2_CONTROL_CLK_DIV_MAX; div++) {
		if ((kspi->base_speed_hz >> (div + 1)) <= max_speed_hz)
			return div;
	}

	/* return divider for slowest clock if loop fails to find one */
	return KSPI2_CONTROL_CLK_DIV_MAX;
}

static void kspi2_write_control_reg(struct kspi2 *kspi, u8 val, u8 mask)
{
	/* write control register only when necessary to improve performance */
	if (val != (kspi->control_shadow & mask)) {
		kspi->control_shadow = (kspi->control_shadow & ~mask) | val;
		iowrite8(kspi->control_shadow, kspi->base + KSPI2_CONTROL_REG);
	}
}

static int kspi2_txrx_byte(struct kspi2 *kspi, u8 tx, u8 *rx)
{
	u8 sts;
	int ret;

	/* start transfer by writing TX byte */
	iowrite8(tx, kspi->base + KSPI2_DATA_REG);

	/* wait till finished (BUSY == 0) */
	ret = readb_poll_timeout(kspi->base + KSPI2_STATUS_REG,
				 sts, (sts & KSPI2_STATUS_BUSY) == 0,
				 0, KSPI2_XFER_TIMEOUT_US(kspi));
	if (ret != 0)
		return ret;

	/* read RX byte */
	if (rx)
		*rx = ioread8(kspi->base + KSPI2_DATA_REG);

	return 0;
}

static int kspi2_process_transfer(struct kspi2 *kspi, struct spi_transfer *t)
{
	u8 tx = 0;
	u8 rx;
	int i;
	int ret;

	for (i = 0; i < t->len; i++) {
		if (t->tx_buf)
			tx = ((const u8 *)t->tx_buf)[i];

		ret = kspi2_txrx_byte(kspi, tx, &rx);
		if (ret)
			return ret;

		if (t->rx_buf)
			((u8 *)t->rx_buf)[i] = rx;
	}

	return 0;
}

static int kspi2_setup_transfer(struct kspi2 *kspi,
				 struct spi_device *spi,
				 struct spi_transfer *t)
{
	u32 max_speed_hz = spi->max_speed_hz;
	u8 clk_div;

	/*
	 * spi_device (spi) has default parameters. Some of these can be
	 * overwritten by parameters in spi_transfer (t).
	 */
	if (t->bits_per_word && ((t->bits_per_word % 8) != 0)) {
		dev_err(&spi->dev, "Word width %d not supported!\n",
			t->bits_per_word);

		return -EINVAL;
	}

	if (t->speed_hz && (t->speed_hz < max_speed_hz))
		max_speed_hz = t->speed_hz;

	clk_div = kspi2_calc_minimal_divider(kspi, max_speed_hz);
	kspi2_write_control_reg(kspi, clk_div, KSPI2_CONTROL_CLK_DIV_MASK);

	return 0;
}

static int kspi2_transfer_one(struct spi_controller *host,
			      struct spi_device *spi,
			      struct spi_transfer *t)
{
	struct kspi2 *kspi = spi_controller_get_devdata(host);
	int ret;

	ret = kspi2_setup_transfer(kspi, spi, t);
	if (ret != 0)
		return ret;

	if (t->len) {
		ret = kspi2_process_transfer(kspi, t);
		if (ret != 0)
			return ret;
	}

	return 0;
}

static void kspi2_set_cs(struct spi_device *spi, bool enable)
{
	struct spi_controller *host = spi->controller;
	struct kspi2 *kspi = spi_controller_get_devdata(host);

	/* controller is using active low chip select signals by design */
	if (!enable)
		iowrite8(spi_get_chipselect(spi, 0), kspi->base + KSPI2_CS_NR_REG);
	else
		iowrite8(KSPI2_CS_NR_NONE, kspi->base + KSPI2_CS_NR_REG);
}

static int kspi2_prepare_message(struct spi_controller *host,
				 struct spi_message *msg)
{
	struct kspi2 *kspi = spi_controller_get_devdata(host);
	struct spi_device *spi = msg->spi;
	u8 mode = 0;

	/* setup SPI clock phase and polarity */
	if (spi->mode & SPI_CPHA)
		mode |= KSPI2_CONTROL_CPHA;
	if (spi->mode & SPI_CPOL)
		mode |= KSPI2_CONTROL_CPOL;
	kspi2_write_control_reg(kspi, mode, KSPI2_CONTROL_CLK_MODE_MASK);

	return 0;
}

static int kspi2_setup(struct spi_device *spi)
{
	struct kspi2 *kspi = spi_controller_get_devdata(spi->controller);

	/*
	 * Check only parameters. Actual setup is done in kspi2_prepare_message
	 * and directly before the SPI transfer starts.
	 */

	if (spi->mode & ~KSPI2_MODE_BITS) {
		dev_err(&spi->dev, "Mode %d not supported!\n", spi->mode);

		return -EINVAL;
	}

	if ((spi->bits_per_word % 8) != 0) {
		dev_err(&spi->dev, "Word width %d not supported!\n",
			spi->bits_per_word);

		return -EINVAL;
	}

	if ((spi->max_speed_hz == 0) ||
	    (spi->max_speed_hz > KSPI2_SPEED_HZ_MAX(kspi)))
		spi->max_speed_hz = KSPI2_SPEED_HZ_MAX(kspi);

	if (spi->max_speed_hz < KSPI2_SPEED_HZ_MIN(kspi)) {
		dev_err(&spi->dev, "Requested speed of %d Hz is too low!\n",
			spi->max_speed_hz);

		return -EINVAL;
	}

	return 0;
}

static void kspi2_unregister_devices(struct kspi2 *kspi)
{
	int i;

	for (i = 0; i < kspi->device_size; i++) {
		struct spi_device *device = kspi->device[i];

		if (device)
			spi_unregister_device(device);
	}
}

static int kspi2_register_devices(struct kspi2 *kspi)
{
	struct spi_board_info *info = kspi->auxdev->info;
	int i;

	/* register all known SPI devices */
	for (i = 0; i < kspi->auxdev->info_size; i++) {
		struct spi_device *device = spi_new_device(kspi->host, &info[i]);

		if (!device) {
			kspi2_unregister_devices(kspi);

			return -ENODEV;
		}
		kspi->device[i] = device;
	}

	return 0;
}

static void kspi2_init(struct kspi2 *kspi)
{
	iowrite8(KSPI2_CONTROL_INIT, kspi->base + KSPI2_CONTROL_REG);
	kspi->control_shadow = KSPI2_CONTROL_INIT;

	iowrite8(KSPI2_CS_NR_NONE, kspi->base + KSPI2_CS_NR_REG);
}

static int kspi2_probe(struct auxiliary_device *auxdev,
		       const struct auxiliary_device_id *id)
{
	struct device *dev = &auxdev->dev;
	struct spi_controller *host;
	struct kspi2 *kspi;
	u8 clk_reg;
	int ret;

	host = devm_spi_alloc_host(dev, sizeof(struct kspi2));
	if (!host)
		return -ENOMEM;
	kspi = spi_controller_get_devdata(host);
	kspi->auxdev = container_of(auxdev, struct keba_spi_auxdev, auxdev);
	kspi->host = host;
	kspi->device = devm_kcalloc(dev, kspi->auxdev->info_size,
				    sizeof(*kspi->device), GFP_KERNEL);
	if (!kspi->device)
		return -ENOMEM;
	kspi->device_size = kspi->auxdev->info_size;
	auxiliary_set_drvdata(auxdev, kspi);

	kspi->base = devm_ioremap_resource(dev, &kspi->auxdev->io);
	if (IS_ERR(kspi->base))
		return PTR_ERR(kspi->base);

	/* read the SPI base clock frequency */
	clk_reg = ioread8(kspi->base + KSPI2_CLK_FREQ_REG);
	switch (clk_reg & KSPI2_CLK_FREQ_MASK) {
	case KSPI2_CLK_FREQ_62_5M:
		kspi->base_speed_hz = 62500000; break;
	case KSPI2_CLK_FREQ_33_3M:
		kspi->base_speed_hz = 33333333; break;
	case KSPI2_CLK_FREQ_125M:
		kspi->base_speed_hz = 125000000; break;
	case KSPI2_CLK_FREQ_50M:
		kspi->base_speed_hz = 50000000; break;
	case KSPI2_CLK_FREQ_100M:
		kspi->base_speed_hz = 100000000; break;
	default:
		dev_err(dev, "Undefined SPI base clock frequency!\n");
		return -ENODEV;
	}

	kspi2_init(kspi);

	host->bus_num = -1;
	host->num_chipselect = KSPI2_NUM_CS;
	host->mode_bits = KSPI2_MODE_BITS;
	host->setup = kspi2_setup;
	host->prepare_transfer_hardware = kspi2_prepare_hardware;
	host->unprepare_transfer_hardware = kspi2_unprepare_hardware;
	host->prepare_message = kspi2_prepare_message;
	host->set_cs = kspi2_set_cs;
	host->transfer_one = kspi2_transfer_one;
	ret = devm_spi_register_controller(dev, host);
	if (ret) {
		dev_err(dev, "Failed to register host (%d)!\n", ret);
		return ret;
	}

	ret = kspi2_register_devices(kspi);
	if (ret) {
		dev_err(dev, "Failed to register devices (%d)!\n", ret);
		return ret;
	}

	return 0;
}

static void kspi2_remove(struct auxiliary_device *auxdev)
{
	struct kspi2 *kspi = auxiliary_get_drvdata(auxdev);

	kspi2_unregister_devices(kspi);
}

static const struct auxiliary_device_id kspi2_devtype_aux[] = {
	{ .name = "keba.spi" },
	{ },
};
MODULE_DEVICE_TABLE(auxiliary, kspi2_devtype_aux);

static struct auxiliary_driver kspi2_driver_aux = {
	.name = KSPI2,
	.id_table = kspi2_devtype_aux,
	.probe = kspi2_probe,
	.remove = kspi2_remove,
};
module_auxiliary_driver(kspi2_driver_aux);

MODULE_AUTHOR("Gerhard Engleder <eg@keba.com>");
MODULE_DESCRIPTION("KEBA SPI host controller driver");
MODULE_LICENSE("GPL");
