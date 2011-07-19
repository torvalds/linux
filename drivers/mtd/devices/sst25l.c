/*
 * sst25l.c
 *
 * Driver for SST25L SPI Flash chips
 *
 * Copyright © 2009 Bluewater Systems Ltd
 * Author: Andre Renaud <andre@bluewatersys.com>
 * Author: Ryan Mallon
 *
 * Based on m25p80.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

/* Erases can take up to 3 seconds! */
#define MAX_READY_WAIT_JIFFIES	msecs_to_jiffies(3000)

#define SST25L_CMD_WRSR		0x01	/* Write status register */
#define SST25L_CMD_WRDI		0x04	/* Write disable */
#define SST25L_CMD_RDSR		0x05	/* Read status register */
#define SST25L_CMD_WREN		0x06	/* Write enable */
#define SST25L_CMD_READ		0x03	/* High speed read */

#define SST25L_CMD_EWSR		0x50	/* Enable write status register */
#define SST25L_CMD_SECTOR_ERASE	0x20	/* Erase sector */
#define SST25L_CMD_READ_ID	0x90	/* Read device ID */
#define SST25L_CMD_AAI_PROGRAM	0xaf	/* Auto address increment */

#define SST25L_STATUS_BUSY	(1 << 0)	/* Chip is busy */
#define SST25L_STATUS_WREN	(1 << 1)	/* Write enabled */
#define SST25L_STATUS_BP0	(1 << 2)	/* Block protection 0 */
#define SST25L_STATUS_BP1	(1 << 3)	/* Block protection 1 */

struct sst25l_flash {
	struct spi_device	*spi;
	struct mutex		lock;
	struct mtd_info		mtd;
};

struct flash_info {
	const char		*name;
	uint16_t		device_id;
	unsigned		page_size;
	unsigned		nr_pages;
	unsigned		erase_size;
};

#define to_sst25l_flash(x) container_of(x, struct sst25l_flash, mtd)

static struct flash_info __devinitdata sst25l_flash_info[] = {
	{"sst25lf020a", 0xbf43, 256, 1024, 4096},
	{"sst25lf040a",	0xbf44,	256, 2048, 4096},
};

static int sst25l_status(struct sst25l_flash *flash, int *status)
{
	struct spi_message m;
	struct spi_transfer t;
	unsigned char cmd_resp[2];
	int err;

	spi_message_init(&m);
	memset(&t, 0, sizeof(struct spi_transfer));

	cmd_resp[0] = SST25L_CMD_RDSR;
	cmd_resp[1] = 0xff;
	t.tx_buf = cmd_resp;
	t.rx_buf = cmd_resp;
	t.len = sizeof(cmd_resp);
	spi_message_add_tail(&t, &m);
	err = spi_sync(flash->spi, &m);
	if (err < 0)
		return err;

	*status = cmd_resp[1];
	return 0;
}

static int sst25l_write_enable(struct sst25l_flash *flash, int enable)
{
	unsigned char command[2];
	int status, err;

	command[0] = enable ? SST25L_CMD_WREN : SST25L_CMD_WRDI;
	err = spi_write(flash->spi, command, 1);
	if (err)
		return err;

	command[0] = SST25L_CMD_EWSR;
	err = spi_write(flash->spi, command, 1);
	if (err)
		return err;

	command[0] = SST25L_CMD_WRSR;
	command[1] = enable ? 0 : SST25L_STATUS_BP0 | SST25L_STATUS_BP1;
	err = spi_write(flash->spi, command, 2);
	if (err)
		return err;

	if (enable) {
		err = sst25l_status(flash, &status);
		if (err)
			return err;
		if (!(status & SST25L_STATUS_WREN))
			return -EROFS;
	}

	return 0;
}

static int sst25l_wait_till_ready(struct sst25l_flash *flash)
{
	unsigned long deadline;
	int status, err;

	deadline = jiffies + MAX_READY_WAIT_JIFFIES;
	do {
		err = sst25l_status(flash, &status);
		if (err)
			return err;
		if (!(status & SST25L_STATUS_BUSY))
			return 0;

		cond_resched();
	} while (!time_after_eq(jiffies, deadline));

	return -ETIMEDOUT;
}

static int sst25l_erase_sector(struct sst25l_flash *flash, uint32_t offset)
{
	unsigned char command[4];
	int err;

	err = sst25l_write_enable(flash, 1);
	if (err)
		return err;

	command[0] = SST25L_CMD_SECTOR_ERASE;
	command[1] = offset >> 16;
	command[2] = offset >> 8;
	command[3] = offset;
	err = spi_write(flash->spi, command, 4);
	if (err)
		return err;

	err = sst25l_wait_till_ready(flash);
	if (err)
		return err;

	return sst25l_write_enable(flash, 0);
}

static int sst25l_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct sst25l_flash *flash = to_sst25l_flash(mtd);
	uint32_t addr, end;
	int err;

	/* Sanity checks */
	if (instr->addr + instr->len > flash->mtd.size)
		return -EINVAL;

	if ((uint32_t)instr->len % mtd->erasesize)
		return -EINVAL;

	if ((uint32_t)instr->addr % mtd->erasesize)
		return -EINVAL;

	addr = instr->addr;
	end = addr + instr->len;

	mutex_lock(&flash->lock);

	err = sst25l_wait_till_ready(flash);
	if (err) {
		mutex_unlock(&flash->lock);
		return err;
	}

	while (addr < end) {
		err = sst25l_erase_sector(flash, addr);
		if (err) {
			mutex_unlock(&flash->lock);
			instr->state = MTD_ERASE_FAILED;
			dev_err(&flash->spi->dev, "Erase failed\n");
			return err;
		}

		addr += mtd->erasesize;
	}

	mutex_unlock(&flash->lock);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);
	return 0;
}

static int sst25l_read(struct mtd_info *mtd, loff_t from, size_t len,
		       size_t *retlen, unsigned char *buf)
{
	struct sst25l_flash *flash = to_sst25l_flash(mtd);
	struct spi_transfer transfer[2];
	struct spi_message message;
	unsigned char command[4];
	int ret;

	/* Sanity checking */
	if (len == 0)
		return 0;

	if (from + len > flash->mtd.size)
		return -EINVAL;

	if (retlen)
		*retlen = 0;

	spi_message_init(&message);
	memset(&transfer, 0, sizeof(transfer));

	command[0] = SST25L_CMD_READ;
	command[1] = from >> 16;
	command[2] = from >> 8;
	command[3] = from;

	transfer[0].tx_buf = command;
	transfer[0].len = sizeof(command);
	spi_message_add_tail(&transfer[0], &message);

	transfer[1].rx_buf = buf;
	transfer[1].len = len;
	spi_message_add_tail(&transfer[1], &message);

	mutex_lock(&flash->lock);

	/* Wait for previous write/erase to complete */
	ret = sst25l_wait_till_ready(flash);
	if (ret) {
		mutex_unlock(&flash->lock);
		return ret;
	}

	spi_sync(flash->spi, &message);

	if (retlen && message.actual_length > sizeof(command))
		*retlen += message.actual_length - sizeof(command);

	mutex_unlock(&flash->lock);
	return 0;
}

static int sst25l_write(struct mtd_info *mtd, loff_t to, size_t len,
			size_t *retlen, const unsigned char *buf)
{
	struct sst25l_flash *flash = to_sst25l_flash(mtd);
	int i, j, ret, bytes, copied = 0;
	unsigned char command[5];

	/* Sanity checks */
	if (!len)
		return 0;

	if (to + len > flash->mtd.size)
		return -EINVAL;

	if ((uint32_t)to % mtd->writesize)
		return -EINVAL;

	mutex_lock(&flash->lock);

	ret = sst25l_write_enable(flash, 1);
	if (ret)
		goto out;

	for (i = 0; i < len; i += mtd->writesize) {
		ret = sst25l_wait_till_ready(flash);
		if (ret)
			goto out;

		/* Write the first byte of the page */
		command[0] = SST25L_CMD_AAI_PROGRAM;
		command[1] = (to + i) >> 16;
		command[2] = (to + i) >> 8;
		command[3] = (to + i);
		command[4] = buf[i];
		ret = spi_write(flash->spi, command, 5);
		if (ret < 0)
			goto out;
		copied++;

		/*
		 * Write the remaining bytes using auto address
		 * increment mode
		 */
		bytes = min_t(uint32_t, mtd->writesize, len - i);
		for (j = 1; j < bytes; j++, copied++) {
			ret = sst25l_wait_till_ready(flash);
			if (ret)
				goto out;

			command[1] = buf[i + j];
			ret = spi_write(flash->spi, command, 2);
			if (ret)
				goto out;
		}
	}

out:
	ret = sst25l_write_enable(flash, 0);

	if (retlen)
		*retlen = copied;

	mutex_unlock(&flash->lock);
	return ret;
}

static struct flash_info *__devinit sst25l_match_device(struct spi_device *spi)
{
	struct flash_info *flash_info = NULL;
	struct spi_message m;
	struct spi_transfer t;
	unsigned char cmd_resp[6];
	int i, err;
	uint16_t id;

	spi_message_init(&m);
	memset(&t, 0, sizeof(struct spi_transfer));

	cmd_resp[0] = SST25L_CMD_READ_ID;
	cmd_resp[1] = 0;
	cmd_resp[2] = 0;
	cmd_resp[3] = 0;
	cmd_resp[4] = 0xff;
	cmd_resp[5] = 0xff;
	t.tx_buf = cmd_resp;
	t.rx_buf = cmd_resp;
	t.len = sizeof(cmd_resp);
	spi_message_add_tail(&t, &m);
	err = spi_sync(spi, &m);
	if (err < 0) {
		dev_err(&spi->dev, "error reading device id\n");
		return NULL;
	}

	id = (cmd_resp[4] << 8) | cmd_resp[5];

	for (i = 0; i < ARRAY_SIZE(sst25l_flash_info); i++)
		if (sst25l_flash_info[i].device_id == id)
			flash_info = &sst25l_flash_info[i];

	if (!flash_info)
		dev_err(&spi->dev, "unknown id %.4x\n", id);

	return flash_info;
}

static int __devinit sst25l_probe(struct spi_device *spi)
{
	struct flash_info *flash_info;
	struct sst25l_flash *flash;
	struct flash_platform_data *data;
	int ret, i;

	flash_info = sst25l_match_device(spi);
	if (!flash_info)
		return -ENODEV;

	flash = kzalloc(sizeof(struct sst25l_flash), GFP_KERNEL);
	if (!flash)
		return -ENOMEM;

	flash->spi = spi;
	mutex_init(&flash->lock);
	dev_set_drvdata(&spi->dev, flash);

	data = spi->dev.platform_data;
	if (data && data->name)
		flash->mtd.name = data->name;
	else
		flash->mtd.name = dev_name(&spi->dev);

	flash->mtd.type		= MTD_NORFLASH;
	flash->mtd.flags	= MTD_CAP_NORFLASH;
	flash->mtd.erasesize	= flash_info->erase_size;
	flash->mtd.writesize	= flash_info->page_size;
	flash->mtd.size		= flash_info->page_size * flash_info->nr_pages;
	flash->mtd.erase	= sst25l_erase;
	flash->mtd.read		= sst25l_read;
	flash->mtd.write 	= sst25l_write;

	dev_info(&spi->dev, "%s (%lld KiB)\n", flash_info->name,
		 (long long)flash->mtd.size >> 10);

	pr_debug("mtd .name = %s, .size = 0x%llx (%lldMiB) "
	      ".erasesize = 0x%.8x (%uKiB) .numeraseregions = %d\n",
	      flash->mtd.name,
	      (long long)flash->mtd.size, (long long)(flash->mtd.size >> 20),
	      flash->mtd.erasesize, flash->mtd.erasesize / 1024,
	      flash->mtd.numeraseregions);


	ret = mtd_device_parse_register(&flash->mtd, NULL, 0,
			data ? data->parts : NULL,
			data ? data->nr_parts : 0);
	if (ret) {
		kfree(flash);
		dev_set_drvdata(&spi->dev, NULL);
		return -ENODEV;
	}

	return 0;
}

static int __devexit sst25l_remove(struct spi_device *spi)
{
	struct sst25l_flash *flash = dev_get_drvdata(&spi->dev);
	int ret;

	ret = mtd_device_unregister(&flash->mtd);
	if (ret == 0)
		kfree(flash);
	return ret;
}

static struct spi_driver sst25l_driver = {
	.driver = {
		.name	= "sst25l",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= sst25l_probe,
	.remove		= __devexit_p(sst25l_remove),
};

static int __init sst25l_init(void)
{
	return spi_register_driver(&sst25l_driver);
}

static void __exit sst25l_exit(void)
{
	spi_unregister_driver(&sst25l_driver);
}

module_init(sst25l_init);
module_exit(sst25l_exit);

MODULE_DESCRIPTION("MTD SPI driver for SST25L Flash chips");
MODULE_AUTHOR("Andre Renaud <andre@bluewatersys.com>, "
	      "Ryan Mallon");
MODULE_LICENSE("GPL");
