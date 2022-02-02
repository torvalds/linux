// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Microchip 48L640 64 Kb SPI Serial EERAM
 *
 * Copyright Heiko Schocher <hs@denx.de>
 *
 * datasheet: http://ww1.microchip.com/downloads/en/DeviceDoc/20006055B.pdf
 *
 * we set continuous mode but reading/writing more bytes than
 * pagesize seems to bring chip into state where readden values
 * are wrong ... no idea why.
 *
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/sizes.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>

struct mchp48_caps {
	unsigned int size;
	unsigned int page_size;
};

struct mchp48l640_flash {
	struct spi_device	*spi;
	struct mutex		lock;
	struct mtd_info		mtd;
	const struct mchp48_caps	*caps;
};

#define MCHP48L640_CMD_WREN		0x06
#define MCHP48L640_CMD_WRDI		0x04
#define MCHP48L640_CMD_WRITE		0x02
#define MCHP48L640_CMD_READ		0x03
#define MCHP48L640_CMD_WRSR		0x01
#define MCHP48L640_CMD_RDSR		0x05

#define MCHP48L640_STATUS_RDY		0x01
#define MCHP48L640_STATUS_WEL		0x02
#define MCHP48L640_STATUS_BP0		0x04
#define MCHP48L640_STATUS_BP1		0x08
#define MCHP48L640_STATUS_SWM		0x10
#define MCHP48L640_STATUS_PRO		0x20
#define MCHP48L640_STATUS_ASE		0x40

#define MCHP48L640_TIMEOUT		100

#define MAX_CMD_SIZE			0x10

#define to_mchp48l640_flash(x) container_of(x, struct mchp48l640_flash, mtd)

static int mchp48l640_mkcmd(struct mchp48l640_flash *flash, u8 cmd, loff_t addr, char *buf)
{
	buf[0] = cmd;
	buf[1] = addr >> 8;
	buf[2] = addr;

	return 3;
}

static int mchp48l640_read_status(struct mchp48l640_flash *flash, int *status)
{
	unsigned char cmd[2];
	int ret;

	cmd[0] = MCHP48L640_CMD_RDSR;
	cmd[1] = 0x00;
	mutex_lock(&flash->lock);
	ret = spi_write_then_read(flash->spi, &cmd[0], 1, &cmd[1], 1);
	mutex_unlock(&flash->lock);
	if (!ret)
		*status = cmd[1];
	dev_dbg(&flash->spi->dev, "read status ret: %d status: %x", ret, *status);

	return ret;
}

static int mchp48l640_waitforbit(struct mchp48l640_flash *flash, int bit, bool set)
{
	int ret, status;
	unsigned long deadline;

	deadline = jiffies + msecs_to_jiffies(MCHP48L640_TIMEOUT);
	do {
		ret = mchp48l640_read_status(flash, &status);
		dev_dbg(&flash->spi->dev, "read status ret: %d bit: %x %sset status: %x",
			ret, bit, (set ? "" : "not"), status);
		if (ret)
			return ret;

		if (set) {
			if ((status & bit) == bit)
				return 0;
		} else {
			if ((status & bit) == 0)
				return 0;
		}

		usleep_range(1000, 2000);
	} while (!time_after_eq(jiffies, deadline));

	dev_err(&flash->spi->dev, "Timeout waiting for bit %x %s set in status register.",
		bit, (set ? "" : "not"));
	return -ETIMEDOUT;
}

static int mchp48l640_write_prepare(struct mchp48l640_flash *flash, bool enable)
{
	unsigned char cmd[2];
	int ret;

	if (enable)
		cmd[0] = MCHP48L640_CMD_WREN;
	else
		cmd[0] = MCHP48L640_CMD_WRDI;

	mutex_lock(&flash->lock);
	ret = spi_write(flash->spi, cmd, 1);
	mutex_unlock(&flash->lock);

	if (ret)
		dev_err(&flash->spi->dev, "write %sable failed ret: %d",
			(enable ? "en" : "dis"), ret);

	dev_dbg(&flash->spi->dev, "write %sable success ret: %d",
		(enable ? "en" : "dis"), ret);
	if (enable)
		return mchp48l640_waitforbit(flash, MCHP48L640_STATUS_WEL, true);

	return ret;
}

static int mchp48l640_set_mode(struct mchp48l640_flash *flash)
{
	unsigned char cmd[2];
	int ret;

	ret = mchp48l640_write_prepare(flash, true);
	if (ret)
		return ret;

	cmd[0] = MCHP48L640_CMD_WRSR;
	cmd[1] = MCHP48L640_STATUS_PRO;

	mutex_lock(&flash->lock);
	ret = spi_write(flash->spi, cmd, 2);
	mutex_unlock(&flash->lock);
	if (ret)
		dev_err(&flash->spi->dev, "Could not set continuous mode ret: %d", ret);

	return mchp48l640_waitforbit(flash, MCHP48L640_STATUS_PRO, true);
}

static int mchp48l640_wait_rdy(struct mchp48l640_flash *flash)
{
	return mchp48l640_waitforbit(flash, MCHP48L640_STATUS_RDY, false);
};

static int mchp48l640_write_page(struct mtd_info *mtd, loff_t to, size_t len,
			    size_t *retlen, const unsigned char *buf)
{
	struct mchp48l640_flash *flash = to_mchp48l640_flash(mtd);
	unsigned char *cmd;
	int ret;
	int cmdlen;

	cmd = kmalloc((3 + len), GFP_KERNEL | GFP_DMA);
	if (!cmd)
		return -ENOMEM;

	ret = mchp48l640_wait_rdy(flash);
	if (ret)
		goto fail;

	ret = mchp48l640_write_prepare(flash, true);
	if (ret)
		goto fail;

	mutex_lock(&flash->lock);
	cmdlen = mchp48l640_mkcmd(flash, MCHP48L640_CMD_WRITE, to, cmd);
	memcpy(&cmd[cmdlen], buf, len);
	ret = spi_write(flash->spi, cmd, cmdlen + len);
	mutex_unlock(&flash->lock);
	if (!ret)
		*retlen += len;
	else
		goto fail;

	ret = mchp48l640_waitforbit(flash, MCHP48L640_STATUS_WEL, false);
	if (ret)
		goto fail;

	kfree(cmd);
	return 0;
fail:
	kfree(cmd);
	dev_err(&flash->spi->dev, "write fail with: %d", ret);
	return ret;
};

static int mchp48l640_write(struct mtd_info *mtd, loff_t to, size_t len,
			    size_t *retlen, const unsigned char *buf)
{
	struct mchp48l640_flash *flash = to_mchp48l640_flash(mtd);
	int ret;
	size_t wlen = 0;
	loff_t woff = to;
	size_t ws;
	size_t page_sz = flash->caps->page_size;

	/*
	 * we set PRO bit (page rollover), but writing length > page size
	 * does result in total chaos, so write in 32 byte chunks.
	 */
	while (wlen < len) {
		ws = min((len - wlen), page_sz);
		ret = mchp48l640_write_page(mtd, woff, ws, retlen, &buf[wlen]);
		if (ret)
			return ret;
		wlen += ws;
		woff += ws;
	}

	return 0;
}

static int mchp48l640_read_page(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, unsigned char *buf)
{
	struct mchp48l640_flash *flash = to_mchp48l640_flash(mtd);
	unsigned char *cmd;
	int ret;
	int cmdlen;

	cmd = kmalloc((3 + len), GFP_KERNEL | GFP_DMA);
	if (!cmd)
		return -ENOMEM;

	ret = mchp48l640_wait_rdy(flash);
	if (ret)
		goto fail;

	mutex_lock(&flash->lock);
	cmdlen = mchp48l640_mkcmd(flash, MCHP48L640_CMD_READ, from, cmd);
	ret = spi_write_then_read(flash->spi, cmd, cmdlen, buf, len);
	mutex_unlock(&flash->lock);
	if (!ret)
		*retlen += len;

	kfree(cmd);
	return ret;

fail:
	kfree(cmd);
	dev_err(&flash->spi->dev, "read fail with: %d", ret);
	return ret;
}

static int mchp48l640_read(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, unsigned char *buf)
{
	struct mchp48l640_flash *flash = to_mchp48l640_flash(mtd);
	int ret;
	size_t wlen = 0;
	loff_t woff = from;
	size_t ws;
	size_t page_sz = flash->caps->page_size;

	/*
	 * we set PRO bit (page rollover), but if read length > page size
	 * does result in total chaos in result ...
	 */
	while (wlen < len) {
		ws = min((len - wlen), page_sz);
		ret = mchp48l640_read_page(mtd, woff, ws, retlen, &buf[wlen]);
		if (ret)
			return ret;
		wlen += ws;
		woff += ws;
	}

	return 0;
};

static const struct mchp48_caps mchp48l640_caps = {
	.size = SZ_8K,
	.page_size = 32,
};

static int mchp48l640_probe(struct spi_device *spi)
{
	struct mchp48l640_flash *flash;
	struct flash_platform_data *data;
	int err;
	int status;

	flash = devm_kzalloc(&spi->dev, sizeof(*flash), GFP_KERNEL);
	if (!flash)
		return -ENOMEM;

	flash->spi = spi;
	mutex_init(&flash->lock);
	spi_set_drvdata(spi, flash);

	err = mchp48l640_read_status(flash, &status);
	if (err)
		return err;

	err = mchp48l640_set_mode(flash);
	if (err)
		return err;

	data = dev_get_platdata(&spi->dev);

	flash->caps = of_device_get_match_data(&spi->dev);
	if (!flash->caps)
		flash->caps = &mchp48l640_caps;

	mtd_set_of_node(&flash->mtd, spi->dev.of_node);
	flash->mtd.dev.parent	= &spi->dev;
	flash->mtd.type		= MTD_RAM;
	flash->mtd.flags	= MTD_CAP_RAM;
	flash->mtd.writesize	= flash->caps->page_size;
	flash->mtd.size		= flash->caps->size;
	flash->mtd._read	= mchp48l640_read;
	flash->mtd._write	= mchp48l640_write;

	err = mtd_device_register(&flash->mtd, data ? data->parts : NULL,
				  data ? data->nr_parts : 0);
	if (err)
		return err;

	return 0;
}

static int mchp48l640_remove(struct spi_device *spi)
{
	struct mchp48l640_flash *flash = spi_get_drvdata(spi);

	WARN_ON(mtd_device_unregister(&flash->mtd));

	return 0;
}

static const struct of_device_id mchp48l640_of_table[] = {
	{
		.compatible = "microchip,48l640",
		.data = &mchp48l640_caps,
	},
	{}
};
MODULE_DEVICE_TABLE(of, mchp48l640_of_table);

static const struct spi_device_id mchp48l640_spi_ids[] = {
	{
		.name = "48l640",
		.driver_data = (kernel_ulong_t)&mchp48l640_caps,
	},
	{}
};
MODULE_DEVICE_TABLE(spi, mchp48l640_spi_ids);

static struct spi_driver mchp48l640_driver = {
	.driver = {
		.name	= "mchp48l640",
		.of_match_table = mchp48l640_of_table,
	},
	.probe		= mchp48l640_probe,
	.remove		= mchp48l640_remove,
	.id_table	= mchp48l640_spi_ids,
};

module_spi_driver(mchp48l640_driver);

MODULE_DESCRIPTION("MTD SPI driver for Microchip 48l640 EERAM chips");
MODULE_AUTHOR("Heiko Schocher <hs@denx.de>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:mchp48l640");
