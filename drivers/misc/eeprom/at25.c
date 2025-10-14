// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for most of the SPI EEPROMs, such as Atmel AT25 models
 * and Cypress FRAMs FM25 models.
 *
 * Copyright (C) 2006 David Brownell
 */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/spi/eeprom.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#include <linux/nvmem-provider.h>

/*
 * NOTE: this is an *EEPROM* driver. The vagaries of product naming
 * mean that some AT25 products are EEPROMs, and others are FLASH.
 * Handle FLASH chips with the drivers/mtd/devices/m25p80.c driver,
 * not this one!
 *
 * EEPROMs that can be used with this driver include, for example:
 *   AT25M02, AT25128B
 */

#define	FM25_SN_LEN	8		/* serial number length */
#define EE_MAXADDRLEN	3		/* 24 bit addresses, up to 2 MBytes */

struct at25_data {
	struct spi_eeprom	chip;
	struct spi_mem		*spimem;
	struct mutex		lock;
	unsigned		addrlen;
	struct nvmem_config	nvmem_config;
	struct nvmem_device	*nvmem;
	u8 sernum[FM25_SN_LEN];
};

#define	AT25_WREN	0x06		/* latch the write enable */
#define	AT25_WRDI	0x04		/* reset the write enable */
#define	AT25_RDSR	0x05		/* read status register */
#define	AT25_WRSR	0x01		/* write status register */
#define	AT25_READ	0x03		/* read byte(s) */
#define	AT25_WRITE	0x02		/* write byte(s)/sector */
#define	FM25_SLEEP	0xb9		/* enter sleep mode */
#define	FM25_RDID	0x9f		/* read device ID */
#define	FM25_RDSN	0xc3		/* read S/N */

#define	AT25_SR_nRDY	0x01		/* nRDY = write-in-progress */
#define	AT25_SR_WEN	0x02		/* write enable (latched) */
#define	AT25_SR_BP0	0x04		/* BP for software writeprotect */
#define	AT25_SR_BP1	0x08
#define	AT25_SR_WPEN	0x80		/* writeprotect enable */

#define	AT25_INSTR_BIT3	0x08		/* additional address bit in instr */

#define	FM25_ID_LEN	9		/* ID length */

/*
 * Specs often allow 5ms for a page write, sometimes 20ms;
 * it's important to recover from write timeouts.
 */
#define	EE_TIMEOUT	25

/*-------------------------------------------------------------------------*/

#define	io_limit	PAGE_SIZE	/* bytes */

/* Handle the address MSB as part of instruction byte */
static u8 at25_instr(struct at25_data *at25, u8 instr, unsigned int off)
{
	if (!(at25->chip.flags & EE_INSTR_BIT3_IS_ADDR))
		return instr;
	if (off < BIT(at25->addrlen * 8))
		return instr;
	return instr | AT25_INSTR_BIT3;
}

static int at25_ee_read(void *priv, unsigned int offset,
			void *val, size_t count)
{
	u8 *bounce __free(kfree) = kmalloc(min(count, io_limit), GFP_KERNEL);
	struct at25_data *at25 = priv;
	char *buf = val;
	unsigned int msg_offset = offset;
	size_t bytes_left = count;
	size_t segment;
	int status;

	if (!bounce)
		return -ENOMEM;

	if (unlikely(offset >= at25->chip.byte_len))
		return -EINVAL;
	if ((offset + count) > at25->chip.byte_len)
		count = at25->chip.byte_len - offset;
	if (unlikely(!count))
		return -EINVAL;

	do {
		struct spi_mem_op op;

		segment = min(bytes_left, io_limit);

		op = (struct spi_mem_op)SPI_MEM_OP(SPI_MEM_OP_CMD(at25_instr(at25, AT25_READ,
									     msg_offset), 1),
						   SPI_MEM_OP_ADDR(at25->addrlen, msg_offset, 1),
						   SPI_MEM_OP_NO_DUMMY,
						   SPI_MEM_OP_DATA_IN(segment, bounce, 1));

		status = spi_mem_adjust_op_size(at25->spimem, &op);
		if (status)
			return status;
		segment = op.data.nbytes;

		mutex_lock(&at25->lock);
		status = spi_mem_exec_op(at25->spimem, &op);
		mutex_unlock(&at25->lock);
		if (status)
			return status;
		memcpy(buf, bounce, segment);

		msg_offset += segment;
		buf += segment;
		bytes_left -= segment;
	} while (bytes_left > 0);

	dev_dbg(&at25->spimem->spi->dev, "read %zu bytes at %d\n",
		count, offset);
	return 0;
}

/*
 * Read extra registers as ID or serial number
 *
 * Allow for the callers to provide @buf on stack (not necessary DMA-capable)
 * by allocating a bounce buffer internally.
 */
static int fm25_aux_read(struct at25_data *at25, u8 *buf, uint8_t command,
			 int len)
{
	u8 *bounce __free(kfree) = kmalloc(len, GFP_KERNEL);
	struct spi_mem_op op;
	int status;

	if (!bounce)
		return -ENOMEM;

	op = (struct spi_mem_op)SPI_MEM_OP(SPI_MEM_OP_CMD(command, 1),
					   SPI_MEM_OP_NO_ADDR,
					   SPI_MEM_OP_NO_DUMMY,
					   SPI_MEM_OP_DATA_IN(len, bounce, 1));

	status = spi_mem_exec_op(at25->spimem, &op);
	dev_dbg(&at25->spimem->spi->dev, "read %d aux bytes --> %d\n", len, status);
	if (status)
		return status;

	memcpy(buf, bounce, len);

	return 0;
}

static ssize_t sernum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct at25_data *at25;

	at25 = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%*ph\n", (int)sizeof(at25->sernum), at25->sernum);
}
static DEVICE_ATTR_RO(sernum);

static struct attribute *sernum_attrs[] = {
	&dev_attr_sernum.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sernum);

/*
 * Poll Read Status Register with timeout
 *
 * Return:
 * 0, if the chip is ready
 * [positive] Status Register value as-is, if the chip is busy
 * [negative] error code in case of read failure
 */
static int at25_wait_ready(struct at25_data *at25)
{
	u8 *bounce __free(kfree) = kmalloc(1, GFP_KERNEL);
	struct spi_mem_op op;
	int status;

	if (!bounce)
		return -ENOMEM;

	op = (struct spi_mem_op)SPI_MEM_OP(SPI_MEM_OP_CMD(AT25_RDSR, 1),
					   SPI_MEM_OP_NO_ADDR,
					   SPI_MEM_OP_NO_DUMMY,
					   SPI_MEM_OP_DATA_IN(1, bounce, 1));

	read_poll_timeout(spi_mem_exec_op, status,
			  status || !(bounce[0] & AT25_SR_nRDY), false,
			  USEC_PER_MSEC, USEC_PER_MSEC * EE_TIMEOUT,
			  at25->spimem, &op);
	if (status < 0)
		return status;
	if (!(bounce[0] & AT25_SR_nRDY))
		return 0;

	return bounce[0];
}

static int at25_ee_write(void *priv, unsigned int off, void *val, size_t count)
{
	u8 *bounce __free(kfree) = kmalloc(min(count, io_limit), GFP_KERNEL);
	struct at25_data *at25 = priv;
	const char *buf = val;
	unsigned int buf_size;
	int status;

	if (unlikely(off >= at25->chip.byte_len))
		return -EFBIG;
	if ((off + count) > at25->chip.byte_len)
		count = at25->chip.byte_len - off;
	if (unlikely(!count))
		return -EINVAL;

	buf_size = at25->chip.page_size;

	if (!bounce)
		return -ENOMEM;

	/*
	 * For write, rollover is within the page ... so we write at
	 * most one page, then manually roll over to the next page.
	 */
	guard(mutex)(&at25->lock);
	do {
		struct spi_mem_op op = SPI_MEM_OP(SPI_MEM_OP_CMD(AT25_WREN, 1),
						  SPI_MEM_OP_NO_ADDR,
						  SPI_MEM_OP_NO_DUMMY,
						  SPI_MEM_OP_NO_DATA);
		unsigned int segment;

		status = spi_mem_exec_op(at25->spimem, &op);
		if (status < 0) {
			dev_dbg(&at25->spimem->spi->dev, "WREN --> %d\n", status);
			return status;
		}

		/* Write as much of a page as we can */
		segment = buf_size - (off % buf_size);
		if (segment > count)
			segment = count;
		if (segment > io_limit)
			segment = io_limit;

		op = (struct spi_mem_op)SPI_MEM_OP(SPI_MEM_OP_CMD(at25_instr(at25, AT25_WRITE, off),
								  1),
						   SPI_MEM_OP_ADDR(at25->addrlen, off, 1),
						   SPI_MEM_OP_NO_DUMMY,
						   SPI_MEM_OP_DATA_OUT(segment, bounce, 1));

		status = spi_mem_adjust_op_size(at25->spimem, &op);
		if (status)
			return status;
		segment = op.data.nbytes;

		memcpy(bounce, buf, segment);

		status = spi_mem_exec_op(at25->spimem, &op);
		dev_dbg(&at25->spimem->spi->dev, "write %u bytes at %u --> %d\n",
			segment, off, status);
		if (status)
			return status;

		/*
		 * REVISIT this should detect (or prevent) failed writes
		 * to read-only sections of the EEPROM...
		 */

		status = at25_wait_ready(at25);
		if (status < 0) {
			dev_err_probe(&at25->spimem->spi->dev, status,
				      "Read Status Redister command failed\n");
			return status;
		}
		if (status) {
			dev_dbg(&at25->spimem->spi->dev,
				"Status %02x\n", status);
			dev_err(&at25->spimem->spi->dev,
				"write %u bytes offset %u, timeout after %u msecs\n",
				segment, off, EE_TIMEOUT);
			return -ETIMEDOUT;
		}

		off += segment;
		buf += segment;
		count -= segment;

	} while (count > 0);

	return status;
}

/*-------------------------------------------------------------------------*/

static int at25_fw_to_chip(struct device *dev, struct spi_eeprom *chip)
{
	u32 val;
	int err;

	strscpy(chip->name, "at25", sizeof(chip->name));

	err = device_property_read_u32(dev, "size", &val);
	if (err)
		err = device_property_read_u32(dev, "at25,byte-len", &val);
	if (err) {
		dev_err(dev, "Error: missing \"size\" property\n");
		return err;
	}
	chip->byte_len = val;

	err = device_property_read_u32(dev, "pagesize", &val);
	if (err)
		err = device_property_read_u32(dev, "at25,page-size", &val);
	if (err) {
		dev_err(dev, "Error: missing \"pagesize\" property\n");
		return err;
	}
	chip->page_size = val;

	err = device_property_read_u32(dev, "address-width", &val);
	if (err) {
		err = device_property_read_u32(dev, "at25,addr-mode", &val);
		if (err) {
			dev_err(dev, "Error: missing \"address-width\" property\n");
			return err;
		}
		chip->flags = (u16)val;
	} else {
		switch (val) {
		case 9:
			chip->flags |= EE_INSTR_BIT3_IS_ADDR;
			fallthrough;
		case 8:
			chip->flags |= EE_ADDR1;
			break;
		case 16:
			chip->flags |= EE_ADDR2;
			break;
		case 24:
			chip->flags |= EE_ADDR3;
			break;
		default:
			dev_err(dev,
				"Error: bad \"address-width\" property: %u\n",
				val);
			return -ENODEV;
		}
		if (device_property_present(dev, "read-only"))
			chip->flags |= EE_READONLY;
	}
	return 0;
}

static int at25_fram_to_chip(struct device *dev, struct spi_eeprom *chip)
{
	struct at25_data *at25 = container_of(chip, struct at25_data, chip);
	u8 sernum[FM25_SN_LEN];
	u8 id[FM25_ID_LEN];
	u32 val;
	int i;

	strscpy(chip->name, "fm25", sizeof(chip->name));

	if (!device_property_read_u32(dev, "size", &val)) {
		chip->byte_len = val;
	} else {
		/* Get ID of chip */
		fm25_aux_read(at25, id, FM25_RDID, FM25_ID_LEN);
		/* There are inside-out FRAM variations, detect them and reverse the ID bytes */
		if (id[6] == 0x7f && id[2] == 0xc2)
			for (i = 0; i < ARRAY_SIZE(id) / 2; i++) {
				u8 tmp = id[i];
				int j = ARRAY_SIZE(id) - i - 1;

				id[i] = id[j];
				id[j] = tmp;
			}
		if (id[6] != 0xc2) {
			dev_err(dev, "Error: no Cypress FRAM with device ID (manufacturer ID bank 7: %02x)\n", id[6]);
			return -ENODEV;
		}

		switch (id[7]) {
		case 0x21 ... 0x26:
			chip->byte_len = BIT(id[7] - 0x21 + 4) * 1024;
			break;
		case 0x2a ... 0x30:
			/* CY15B116QN ... CY15B116QN */
			chip->byte_len = BIT(((id[7] >> 1) & 0xf) + 13);
			break;
		default:
			dev_err(dev, "Error: unsupported size (id %02x)\n", id[7]);
			return -ENODEV;
		}

		if (id[8]) {
			fm25_aux_read(at25, sernum, FM25_RDSN, FM25_SN_LEN);
			/* Swap byte order */
			for (i = 0; i < FM25_SN_LEN; i++)
				at25->sernum[i] = sernum[FM25_SN_LEN - 1 - i];
		}
	}

	if (chip->byte_len > 64 * 1024)
		chip->flags |= EE_ADDR3;
	else
		chip->flags |= EE_ADDR2;

	chip->page_size = PAGE_SIZE;
	return 0;
}

static const struct of_device_id at25_of_match[] = {
	{ .compatible = "atmel,at25" },
	{ .compatible = "cypress,fm25" },
	{ }
};
MODULE_DEVICE_TABLE(of, at25_of_match);

static const struct spi_device_id at25_spi_ids[] = {
	{ .name = "at25" },
	{ .name = "fm25" },
	{ }
};
MODULE_DEVICE_TABLE(spi, at25_spi_ids);

static int at25_probe(struct spi_mem *mem)
{
	struct spi_device *spi = mem->spi;
	struct spi_eeprom *pdata;
	struct at25_data *at25;
	bool is_fram;
	int err;

	at25 = devm_kzalloc(&spi->dev, sizeof(*at25), GFP_KERNEL);
	if (!at25)
		return -ENOMEM;

	at25->spimem = mem;

	/*
	 * Ping the chip ... the status register is pretty portable,
	 * unlike probing manufacturer IDs.
	 */
	err = at25_wait_ready(at25);
	if (err < 0)
		return dev_err_probe(&spi->dev, err, "Read Status Register command failed\n");
	if (err) {
		dev_err(&spi->dev, "Not ready (%02x)\n", err);
		return -ENXIO;
	}

	mutex_init(&at25->lock);
	spi_set_drvdata(spi, at25);

	is_fram = fwnode_device_is_compatible(dev_fwnode(&spi->dev), "cypress,fm25");

	/* Chip description */
	pdata = dev_get_platdata(&spi->dev);
	if (pdata) {
		at25->chip = *pdata;
	} else {
		if (is_fram)
			err = at25_fram_to_chip(&spi->dev, &at25->chip);
		else
			err = at25_fw_to_chip(&spi->dev, &at25->chip);
		if (err)
			return err;
	}

	/* For now we only support 8/16/24 bit addressing */
	if (at25->chip.flags & EE_ADDR1)
		at25->addrlen = 1;
	else if (at25->chip.flags & EE_ADDR2)
		at25->addrlen = 2;
	else if (at25->chip.flags & EE_ADDR3)
		at25->addrlen = 3;
	else {
		dev_dbg(&spi->dev, "unsupported address type\n");
		return -EINVAL;
	}

	at25->nvmem_config.type = is_fram ? NVMEM_TYPE_FRAM : NVMEM_TYPE_EEPROM;
	at25->nvmem_config.name = dev_name(&spi->dev);
	at25->nvmem_config.dev = &spi->dev;
	at25->nvmem_config.read_only = at25->chip.flags & EE_READONLY;
	at25->nvmem_config.root_only = true;
	at25->nvmem_config.owner = THIS_MODULE;
	at25->nvmem_config.compat = true;
	at25->nvmem_config.base_dev = &spi->dev;
	at25->nvmem_config.reg_read = at25_ee_read;
	at25->nvmem_config.reg_write = at25_ee_write;
	at25->nvmem_config.priv = at25;
	at25->nvmem_config.stride = 1;
	at25->nvmem_config.word_size = 1;
	at25->nvmem_config.size = at25->chip.byte_len;

	at25->nvmem = devm_nvmem_register(&spi->dev, &at25->nvmem_config);
	if (IS_ERR(at25->nvmem))
		return PTR_ERR(at25->nvmem);

	dev_info(&spi->dev, "%d %s %s %s%s, pagesize %u\n",
		 (at25->chip.byte_len < 1024) ?
			at25->chip.byte_len : (at25->chip.byte_len / 1024),
		 (at25->chip.byte_len < 1024) ? "Byte" : "KByte",
		 at25->chip.name, is_fram ? "fram" : "eeprom",
		 (at25->chip.flags & EE_READONLY) ? " (readonly)" : "",
		 at25->chip.page_size);
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct spi_mem_driver at25_driver = {
	.spidrv = {
		.driver = {
			.name		= "at25",
			.of_match_table = at25_of_match,
			.dev_groups	= sernum_groups,
		},
		.id_table	= at25_spi_ids,
	},
	.probe		= at25_probe,
};

module_spi_mem_driver(at25_driver);

MODULE_DESCRIPTION("Driver for most SPI EEPROMs");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
