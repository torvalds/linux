// SPDX-License-Identifier: GPL-2.0-only
/*
 * datasheet: https://www.nxp.com/docs/en/data-sheet/K20P144M120SF3.pdf
 *
 * Copyright (C) 2018-2021 Collabora
 * Copyright (C) 2018-2021 GE Healthcare
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

#define ACHC_MAX_FREQ_HZ 300000
#define ACHC_FAST_READ_FREQ_HZ 1000000

struct achc_data {
	struct spi_device *main;
	struct spi_device *ezport;
	struct gpio_desc *reset;

	struct mutex device_lock; /* avoid concurrent device access */
};

#define EZPORT_RESET_DELAY_MS	100
#define EZPORT_STARTUP_DELAY_MS	200
#define EZPORT_WRITE_WAIT_MS	10
#define EZPORT_TRANSFER_SIZE	2048

#define EZPORT_CMD_SP		0x02 /* flash section program */
#define EZPORT_CMD_RDSR		0x05 /* read status register */
#define EZPORT_CMD_WREN		0x06 /* write enable */
#define EZPORT_CMD_FAST_READ	0x0b /* flash read data at high speed */
#define EZPORT_CMD_RESET	0xb9 /* reset chip */
#define EZPORT_CMD_BE		0xc7 /* bulk erase */
#define EZPORT_CMD_SE		0xd8 /* sector erase */

#define EZPORT_SECTOR_SIZE	4096
#define EZPORT_SECTOR_MASK	(EZPORT_SECTOR_SIZE - 1)

#define EZPORT_STATUS_WIP	BIT(0) /* write in progress */
#define EZPORT_STATUS_WEN	BIT(1) /* write enable */
#define EZPORT_STATUS_BEDIS	BIT(2) /* bulk erase disable */
#define EZPORT_STATUS_FLEXRAM	BIT(3) /* FlexRAM mode */
#define EZPORT_STATUS_WEF	BIT(6) /* write error flag */
#define EZPORT_STATUS_FS	BIT(7) /* flash security */

static void ezport_reset(struct gpio_desc *reset)
{
	gpiod_set_value(reset, 1);
	msleep(EZPORT_RESET_DELAY_MS);
	gpiod_set_value(reset, 0);
	msleep(EZPORT_STARTUP_DELAY_MS);
}

static int ezport_start_programming(struct spi_device *spi, struct gpio_desc *reset)
{
	struct spi_message msg;
	struct spi_transfer assert_cs = {
		.cs_change   = 1,
	};
	struct spi_transfer release_cs = { };
	int ret;

	spi_bus_lock(spi->master);

	/* assert chip select */
	spi_message_init(&msg);
	spi_message_add_tail(&assert_cs, &msg);
	ret = spi_sync_locked(spi, &msg);
	if (ret)
		goto fail;

	msleep(EZPORT_STARTUP_DELAY_MS);

	/* reset with asserted chip select to switch into programming mode */
	ezport_reset(reset);

	/* release chip select */
	spi_message_init(&msg);
	spi_message_add_tail(&release_cs, &msg);
	ret = spi_sync_locked(spi, &msg);

fail:
	spi_bus_unlock(spi->master);
	return ret;
}

static void ezport_stop_programming(struct spi_device *spi, struct gpio_desc *reset)
{
	/* reset without asserted chip select to return into normal mode */
	spi_bus_lock(spi->master);
	ezport_reset(reset);
	spi_bus_unlock(spi->master);
}

static int ezport_get_status_register(struct spi_device *spi)
{
	int ret;

	ret = spi_w8r8(spi, EZPORT_CMD_RDSR);
	if (ret < 0)
		return ret;
	if (ret == 0xff) {
		dev_err(&spi->dev, "Invalid EzPort status, EzPort is not functional!\n");
		return -EINVAL;
	}

	return ret;
}

static int ezport_soft_reset(struct spi_device *spi)
{
	u8 cmd = EZPORT_CMD_RESET;
	int ret;

	ret = spi_write(spi, &cmd, 1);
	if (ret < 0)
		return ret;

	msleep(EZPORT_STARTUP_DELAY_MS);

	return 0;
}

static int ezport_send_simple(struct spi_device *spi, u8 cmd)
{
	int ret;

	ret = spi_write(spi, &cmd, 1);
	if (ret < 0)
		return ret;

	return ezport_get_status_register(spi);
}

static int ezport_wait_write(struct spi_device *spi, u32 retries)
{
	int ret;
	u32 i;

	for (i = 0; i < retries; i++) {
		ret = ezport_get_status_register(spi);
		if (ret >= 0 && !(ret & EZPORT_STATUS_WIP))
			break;
		msleep(EZPORT_WRITE_WAIT_MS);
	}

	return ret;
}

static int ezport_write_enable(struct spi_device *spi)
{
	int ret = 0, retries = 3;

	for (retries = 0; retries < 3; retries++) {
		ret = ezport_send_simple(spi, EZPORT_CMD_WREN);
		if (ret > 0 && ret & EZPORT_STATUS_WEN)
			break;
	}

	if (!(ret & EZPORT_STATUS_WEN)) {
		dev_err(&spi->dev, "EzPort write enable timed out\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static int ezport_bulk_erase(struct spi_device *spi)
{
	int ret;
	static const u8 cmd = EZPORT_CMD_BE;

	dev_dbg(&spi->dev, "EzPort bulk erase...\n");

	ret = ezport_write_enable(spi);
	if (ret < 0)
		return ret;

	ret = spi_write(spi, &cmd, 1);
	if (ret < 0)
		return ret;

	ret = ezport_wait_write(spi, 1000);
	if (ret < 0)
		return ret;

	return 0;
}

static int ezport_section_erase(struct spi_device *spi, u32 address)
{
	u8 query[] = {EZPORT_CMD_SE, (address >> 16) & 0xff, (address >> 8) & 0xff, address & 0xff};
	int ret;

	dev_dbg(&spi->dev, "Ezport section erase @ 0x%06x...\n", address);

	if (address & EZPORT_SECTOR_MASK)
		return -EINVAL;

	ret = ezport_write_enable(spi);
	if (ret < 0)
		return ret;

	ret = spi_write(spi, query, sizeof(query));
	if (ret < 0)
		return ret;

	return ezport_wait_write(spi, 200);
}

static int ezport_flash_transfer(struct spi_device *spi, u32 address,
				 const u8 *payload, size_t payload_size)
{
	struct spi_transfer xfers[2] = {};
	u8 *command;
	int ret;

	dev_dbg(&spi->dev, "EzPort write %zu bytes @ 0x%06x...\n", payload_size, address);

	ret = ezport_write_enable(spi);
	if (ret < 0)
		return ret;

	command = kmalloc(4, GFP_KERNEL | GFP_DMA);
	if (!command)
		return -ENOMEM;

	command[0] = EZPORT_CMD_SP;
	command[1] = address >> 16;
	command[2] = address >> 8;
	command[3] = address >> 0;

	xfers[0].tx_buf = command;
	xfers[0].len = 4;

	xfers[1].tx_buf = payload;
	xfers[1].len = payload_size;

	ret = spi_sync_transfer(spi, xfers, 2);
	kfree(command);
	if (ret < 0)
		return ret;

	return ezport_wait_write(spi, 40);
}

static int ezport_flash_compare(struct spi_device *spi, u32 address,
				const u8 *payload, size_t payload_size)
{
	struct spi_transfer xfers[2] = {};
	u8 *buffer;
	int ret;

	buffer = kmalloc(payload_size + 5, GFP_KERNEL | GFP_DMA);
	if (!buffer)
		return -ENOMEM;

	buffer[0] = EZPORT_CMD_FAST_READ;
	buffer[1] = address >> 16;
	buffer[2] = address >> 8;
	buffer[3] = address >> 0;

	xfers[0].tx_buf = buffer;
	xfers[0].len = 4;
	xfers[0].speed_hz = ACHC_FAST_READ_FREQ_HZ;

	xfers[1].rx_buf = buffer + 4;
	xfers[1].len = payload_size + 1;
	xfers[1].speed_hz = ACHC_FAST_READ_FREQ_HZ;

	ret = spi_sync_transfer(spi, xfers, 2);
	if (ret)
		goto err;

	/* FAST_READ receives one dummy byte before the real data */
	ret = memcmp(payload, buffer + 4 + 1, payload_size);
	if (ret) {
		ret = -EBADMSG;
		dev_dbg(&spi->dev, "Verification failure @ %06x", address);
		print_hex_dump_bytes("fw:  ", DUMP_PREFIX_OFFSET, payload, payload_size);
		print_hex_dump_bytes("dev: ", DUMP_PREFIX_OFFSET, buffer + 4, payload_size);
	}

err:
	kfree(buffer);
	return ret;
}

static int ezport_firmware_compare_data(struct spi_device *spi,
					const u8 *data, size_t size)
{
	int ret;
	size_t address = 0;
	size_t transfer_size;

	dev_dbg(&spi->dev, "EzPort compare data with %zu bytes...\n", size);

	ret = ezport_get_status_register(spi);
	if (ret < 0)
		return ret;

	if (ret & EZPORT_STATUS_FS) {
		dev_info(&spi->dev, "Device is in secure mode (status=0x%02x)!\n", ret);
		dev_info(&spi->dev, "FW verification is not possible\n");
		return -EACCES;
	}

	while (size - address > 0) {
		transfer_size = min((size_t) EZPORT_TRANSFER_SIZE, size - address);

		ret = ezport_flash_compare(spi, address, data+address, transfer_size);
		if (ret)
			return ret;

		address += transfer_size;
	}

	return 0;
}

static int ezport_firmware_flash_data(struct spi_device *spi,
				      const u8 *data, size_t size)
{
	int ret;
	size_t address = 0;
	size_t transfer_size;

	dev_dbg(&spi->dev, "EzPort flash data with %zu bytes...\n", size);

	ret = ezport_get_status_register(spi);
	if (ret < 0)
		return ret;

	if (ret & EZPORT_STATUS_FS) {
		ret = ezport_bulk_erase(spi);
		if (ret < 0)
			return ret;
		if (ret & EZPORT_STATUS_FS)
			return -EINVAL;
	}

	while (size - address > 0) {
		if (!(address & EZPORT_SECTOR_MASK)) {
			ret = ezport_section_erase(spi, address);
			if (ret < 0)
				return ret;
			if (ret & EZPORT_STATUS_WIP || ret & EZPORT_STATUS_WEF)
				return -EIO;
		}

		transfer_size = min((size_t) EZPORT_TRANSFER_SIZE, size - address);

		ret = ezport_flash_transfer(spi, address,
					    data+address, transfer_size);
		if (ret < 0)
			return ret;
		else if (ret & EZPORT_STATUS_WIP)
			return -ETIMEDOUT;
		else if (ret & EZPORT_STATUS_WEF)
			return -EIO;

		address += transfer_size;
	}

	dev_dbg(&spi->dev, "EzPort verify flashed data...\n");
	ret = ezport_firmware_compare_data(spi, data, size);

	/* allow missing FW verfication in secure mode */
	if (ret == -EACCES)
		ret = 0;

	if (ret < 0)
		dev_err(&spi->dev, "Failed to verify flashed data: %d\n", ret);

	ret = ezport_soft_reset(spi);
	if (ret < 0)
		dev_warn(&spi->dev, "EzPort reset failed!\n");

	return ret;
}

static int ezport_firmware_load(struct spi_device *spi, const char *fwname)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, fwname, &spi->dev);
	if (ret) {
		dev_err(&spi->dev, "Could not get firmware: %d\n", ret);
		return ret;
	}

	ret = ezport_firmware_flash_data(spi, fw->data, fw->size);

	release_firmware(fw);

	return ret;
}

/**
 * ezport_flash - flash device firmware
 * @spi: SPI device for NXP EzPort interface
 * @reset: the gpio connected to the device reset pin
 * @fwname: filename of the firmware that should be flashed
 *
 * Context: can sleep
 *
 * Return: 0 on success; negative errno on failure
 */
static int ezport_flash(struct spi_device *spi, struct gpio_desc *reset, const char *fwname)
{
	int ret;

	ret = ezport_start_programming(spi, reset);
	if (ret)
		return ret;

	ret = ezport_firmware_load(spi, fwname);

	ezport_stop_programming(spi, reset);

	if (ret)
		dev_err(&spi->dev, "Failed to flash firmware: %d\n", ret);
	else
		dev_dbg(&spi->dev, "Finished FW flashing!\n");

	return ret;
}

static ssize_t update_firmware_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct achc_data *achc = dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret < 0 || value != 1)
		return -EINVAL;

	mutex_lock(&achc->device_lock);
	ret = ezport_flash(achc->ezport, achc->reset, "achc.bin");
	mutex_unlock(&achc->device_lock);

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(update_firmware);

static ssize_t reset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct achc_data *achc = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&achc->device_lock);
	ret = gpiod_get_value(achc->reset);
	mutex_unlock(&achc->device_lock);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", ret);
}

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct achc_data *achc = dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret < 0 || value > 1)
		return -EINVAL;

	mutex_lock(&achc->device_lock);
	gpiod_set_value(achc->reset, value);
	mutex_unlock(&achc->device_lock);

	return count;
}
static DEVICE_ATTR_RW(reset);

static struct attribute *gehc_achc_attrs[] = {
	&dev_attr_update_firmware.attr,
	&dev_attr_reset.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gehc_achc);

static void unregister_ezport(void *data)
{
	struct spi_device *ezport = data;

	spi_unregister_device(ezport);
}

static int gehc_achc_probe(struct spi_device *spi)
{
	struct achc_data *achc;
	int ezport_reg, ret;

	spi->max_speed_hz = ACHC_MAX_FREQ_HZ;
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;

	achc = devm_kzalloc(&spi->dev, sizeof(*achc), GFP_KERNEL);
	if (!achc)
		return -ENOMEM;
	spi_set_drvdata(spi, achc);
	achc->main = spi;

	mutex_init(&achc->device_lock);

	ret = of_property_read_u32_index(spi->dev.of_node, "reg", 1, &ezport_reg);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "missing second reg entry!\n");

	achc->ezport = spi_new_ancillary_device(spi, ezport_reg);
	if (IS_ERR(achc->ezport))
		return PTR_ERR(achc->ezport);

	ret = devm_add_action_or_reset(&spi->dev, unregister_ezport, achc->ezport);
	if (ret)
		return ret;

	achc->reset = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(achc->reset))
		return dev_err_probe(&spi->dev, PTR_ERR(achc->reset), "Could not get reset gpio\n");

	return 0;
}

static const struct spi_device_id gehc_achc_id[] = {
	{ "ge,achc", 0 },
	{ "achc", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, gehc_achc_id);

static const struct of_device_id gehc_achc_of_match[] = {
	{ .compatible = "ge,achc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gehc_achc_of_match);

static struct spi_driver gehc_achc_spi_driver = {
	.driver = {
		.name	= "gehc-achc",
		.of_match_table = gehc_achc_of_match,
		.dev_groups = gehc_achc_groups,
	},
	.probe		= gehc_achc_probe,
	.id_table	= gehc_achc_id,
};
module_spi_driver(gehc_achc_spi_driver);

MODULE_DESCRIPTION("GEHC ACHC driver");
MODULE_AUTHOR("Sebastian Reichel <sebastian.reichel@collabora.com>");
MODULE_LICENSE("GPL");
