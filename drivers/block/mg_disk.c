/*
 *  drivers/block/mg_disk.c
 *
 *  Support for the mGine m[g]flash IO mode.
 *  Based on legacy hd.c
 *
 * (c) 2008 mGine Co.,LTD
 * (c) 2008 unsik Kim <donari75@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/libata.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/mg_disk.h>

#define MG_RES_SEC (CONFIG_MG_DISK_RES << 1)

static void mg_request(struct request_queue *);

static void mg_dump_status(const char *msg, unsigned int stat,
		struct mg_host *host)
{
	char *name = MG_DISK_NAME;
	struct request *req;

	if (host->breq) {
		req = elv_next_request(host->breq);
		if (req)
			name = req->rq_disk->disk_name;
	}

	printk(KERN_ERR "%s: %s: status=0x%02x { ", name, msg, stat & 0xff);
	if (stat & MG_REG_STATUS_BIT_BUSY)
		printk("Busy ");
	if (stat & MG_REG_STATUS_BIT_READY)
		printk("DriveReady ");
	if (stat & MG_REG_STATUS_BIT_WRITE_FAULT)
		printk("WriteFault ");
	if (stat & MG_REG_STATUS_BIT_SEEK_DONE)
		printk("SeekComplete ");
	if (stat & MG_REG_STATUS_BIT_DATA_REQ)
		printk("DataRequest ");
	if (stat & MG_REG_STATUS_BIT_CORRECTED_ERROR)
		printk("CorrectedError ");
	if (stat & MG_REG_STATUS_BIT_ERROR)
		printk("Error ");
	printk("}\n");
	if ((stat & MG_REG_STATUS_BIT_ERROR) == 0) {
		host->error = 0;
	} else {
		host->error = inb((unsigned long)host->dev_base + MG_REG_ERROR);
		printk(KERN_ERR "%s: %s: error=0x%02x { ", name, msg,
				host->error & 0xff);
		if (host->error & MG_REG_ERR_BBK)
			printk("BadSector ");
		if (host->error & MG_REG_ERR_UNC)
			printk("UncorrectableError ");
		if (host->error & MG_REG_ERR_IDNF)
			printk("SectorIdNotFound ");
		if (host->error & MG_REG_ERR_ABRT)
			printk("DriveStatusError ");
		if (host->error & MG_REG_ERR_AMNF)
			printk("AddrMarkNotFound ");
		printk("}");
		if (host->error &
				(MG_REG_ERR_BBK | MG_REG_ERR_UNC |
				 MG_REG_ERR_IDNF | MG_REG_ERR_AMNF)) {
			if (host->breq) {
				req = elv_next_request(host->breq);
				if (req)
					printk(", sector=%u", (u32)req->sector);
			}

		}
		printk("\n");
	}
}

static unsigned int mg_wait(struct mg_host *host, u32 expect, u32 msec)
{
	u8 status;
	unsigned long expire, cur_jiffies;
	struct mg_drv_data *prv_data = host->dev->platform_data;

	host->error = MG_ERR_NONE;
	expire = jiffies + msecs_to_jiffies(msec);

	status = inb((unsigned long)host->dev_base + MG_REG_STATUS);

	do {
		cur_jiffies = jiffies;
		if (status & MG_REG_STATUS_BIT_BUSY) {
			if (expect == MG_REG_STATUS_BIT_BUSY)
				break;
		} else {
			/* Check the error condition! */
			if (status & MG_REG_STATUS_BIT_ERROR) {
				mg_dump_status("mg_wait", status, host);
				break;
			}

			if (expect == MG_STAT_READY)
				if (MG_READY_OK(status))
					break;

			if (expect == MG_REG_STATUS_BIT_DATA_REQ)
				if (status & MG_REG_STATUS_BIT_DATA_REQ)
					break;
		}
		if (!msec) {
			mg_dump_status("not ready", status, host);
			return MG_ERR_INV_STAT;
		}
		if (prv_data->use_polling)
			msleep(1);

		status = inb((unsigned long)host->dev_base + MG_REG_STATUS);
	} while (time_before(cur_jiffies, expire));

	if (time_after_eq(cur_jiffies, expire) && msec)
		host->error = MG_ERR_TIMEOUT;

	return host->error;
}

static unsigned int mg_wait_rstout(u32 rstout, u32 msec)
{
	unsigned long expire;

	expire = jiffies + msecs_to_jiffies(msec);
	while (time_before(jiffies, expire)) {
		if (gpio_get_value(rstout) == 1)
			return MG_ERR_NONE;
		msleep(10);
	}

	return MG_ERR_RSTOUT;
}

static void mg_unexpected_intr(struct mg_host *host)
{
	u32 status = inb((unsigned long)host->dev_base + MG_REG_STATUS);

	mg_dump_status("mg_unexpected_intr", status, host);
}

static irqreturn_t mg_irq(int irq, void *dev_id)
{
	struct mg_host *host = dev_id;
	void (*handler)(struct mg_host *) = host->mg_do_intr;

	spin_lock(&host->lock);

	host->mg_do_intr = NULL;
	del_timer(&host->timer);
	if (!handler)
		handler = mg_unexpected_intr;
	handler(host);

	spin_unlock(&host->lock);

	return IRQ_HANDLED;
}

static int mg_get_disk_id(struct mg_host *host)
{
	u32 i;
	s32 err;
	const u16 *id = host->id;
	struct mg_drv_data *prv_data = host->dev->platform_data;
	char fwrev[ATA_ID_FW_REV_LEN + 1];
	char model[ATA_ID_PROD_LEN + 1];
	char serial[ATA_ID_SERNO_LEN + 1];

	if (!prv_data->use_polling)
		outb(MG_REG_CTRL_INTR_DISABLE,
				(unsigned long)host->dev_base +
				MG_REG_DRV_CTRL);

	outb(MG_CMD_ID, (unsigned long)host->dev_base + MG_REG_COMMAND);
	err = mg_wait(host, MG_REG_STATUS_BIT_DATA_REQ, MG_TMAX_WAIT_RD_DRQ);
	if (err)
		return err;

	for (i = 0; i < (MG_SECTOR_SIZE >> 1); i++)
		host->id[i] = le16_to_cpu(inw((unsigned long)host->dev_base +
					MG_BUFF_OFFSET + i * 2));

	outb(MG_CMD_RD_CONF, (unsigned long)host->dev_base + MG_REG_COMMAND);
	err = mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD);
	if (err)
		return err;

	if ((id[ATA_ID_FIELD_VALID] & 1) == 0)
		return MG_ERR_TRANSLATION;

	host->n_sectors = ata_id_u32(id, ATA_ID_LBA_CAPACITY);
	host->cyls = id[ATA_ID_CYLS];
	host->heads = id[ATA_ID_HEADS];
	host->sectors = id[ATA_ID_SECTORS];

	if (MG_RES_SEC && host->heads && host->sectors) {
		/* modify cyls, n_sectors */
		host->cyls = (host->n_sectors - MG_RES_SEC) /
			host->heads / host->sectors;
		host->nres_sectors = host->n_sectors - host->cyls *
			host->heads * host->sectors;
		host->n_sectors -= host->nres_sectors;
	}

	ata_id_c_string(id, fwrev, ATA_ID_FW_REV, sizeof(fwrev));
	ata_id_c_string(id, model, ATA_ID_PROD, sizeof(model));
	ata_id_c_string(id, serial, ATA_ID_SERNO, sizeof(serial));
	printk(KERN_INFO "mg_disk: model: %s\n", model);
	printk(KERN_INFO "mg_disk: firm: %.8s\n", fwrev);
	printk(KERN_INFO "mg_disk: serial: %s\n", serial);
	printk(KERN_INFO "mg_disk: %d + reserved %d sectors\n",
			host->n_sectors, host->nres_sectors);

	if (!prv_data->use_polling)
		outb(MG_REG_CTRL_INTR_ENABLE, (unsigned long)host->dev_base +
				MG_REG_DRV_CTRL);

	return err;
}


static int mg_disk_init(struct mg_host *host)
{
	struct mg_drv_data *prv_data = host->dev->platform_data;
	s32 err;
	u8 init_status;

	/* hdd rst low */
	gpio_set_value(host->rst, 0);
	err = mg_wait(host, MG_REG_STATUS_BIT_BUSY, MG_TMAX_RST_TO_BUSY);
	if (err)
		return err;

	/* hdd rst high */
	gpio_set_value(host->rst, 1);
	err = mg_wait(host, MG_STAT_READY, MG_TMAX_HDRST_TO_RDY);
	if (err)
		return err;

	/* soft reset on */
	outb(MG_REG_CTRL_RESET |
			(prv_data->use_polling ? MG_REG_CTRL_INTR_DISABLE :
			 MG_REG_CTRL_INTR_ENABLE),
			(unsigned long)host->dev_base + MG_REG_DRV_CTRL);
	err = mg_wait(host, MG_REG_STATUS_BIT_BUSY, MG_TMAX_RST_TO_BUSY);
	if (err)
		return err;

	/* soft reset off */
	outb(prv_data->use_polling ? MG_REG_CTRL_INTR_DISABLE :
			MG_REG_CTRL_INTR_ENABLE,
			(unsigned long)host->dev_base + MG_REG_DRV_CTRL);
	err = mg_wait(host, MG_STAT_READY, MG_TMAX_SWRST_TO_RDY);
	if (err)
		return err;

	init_status = inb((unsigned long)host->dev_base + MG_REG_STATUS) & 0xf;

	if (init_status == 0xf)
		return MG_ERR_INIT_STAT;

	return err;
}

static void mg_bad_rw_intr(struct mg_host *host)
{
	struct request *req = elv_next_request(host->breq);
	if (req != NULL)
		if (++req->errors >= MG_MAX_ERRORS ||
				host->error == MG_ERR_TIMEOUT)
			end_request(req, 0);
}

static unsigned int mg_out(struct mg_host *host,
		unsigned int sect_num,
		unsigned int sect_cnt,
		unsigned int cmd,
		void (*intr_addr)(struct mg_host *))
{
	struct mg_drv_data *prv_data = host->dev->platform_data;

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD))
		return host->error;

	if (!prv_data->use_polling) {
		host->mg_do_intr = intr_addr;
		mod_timer(&host->timer, jiffies + 3 * HZ);
	}
	if (MG_RES_SEC)
		sect_num += MG_RES_SEC;
	outb((u8)sect_cnt, (unsigned long)host->dev_base + MG_REG_SECT_CNT);
	outb((u8)sect_num, (unsigned long)host->dev_base + MG_REG_SECT_NUM);
	outb((u8)(sect_num >> 8), (unsigned long)host->dev_base +
			MG_REG_CYL_LOW);
	outb((u8)(sect_num >> 16), (unsigned long)host->dev_base +
			MG_REG_CYL_HIGH);
	outb((u8)((sect_num >> 24) | MG_REG_HEAD_LBA_MODE),
			(unsigned long)host->dev_base + MG_REG_DRV_HEAD);
	outb(cmd, (unsigned long)host->dev_base + MG_REG_COMMAND);
	return MG_ERR_NONE;
}

static void mg_read(struct request *req)
{
	u32 remains, j;
	struct mg_host *host = req->rq_disk->private_data;

	remains = req->nr_sectors;

	if (mg_out(host, req->sector, req->nr_sectors, MG_CMD_RD, NULL) !=
			MG_ERR_NONE)
		mg_bad_rw_intr(host);

	MG_DBG("requested %d sects (from %ld), buffer=0x%p\n",
			remains, req->sector, req->buffer);

	while (remains) {
		if (mg_wait(host, MG_REG_STATUS_BIT_DATA_REQ,
					MG_TMAX_WAIT_RD_DRQ) != MG_ERR_NONE) {
			mg_bad_rw_intr(host);
			return;
		}
		for (j = 0; j < MG_SECTOR_SIZE >> 1; j++) {
			*(u16 *)req->buffer =
				inw((unsigned long)host->dev_base +
						MG_BUFF_OFFSET + (j << 1));
			req->buffer += 2;
		}

		req->sector++;
		req->errors = 0;
		remains = --req->nr_sectors;
		--req->current_nr_sectors;

		if (req->current_nr_sectors <= 0) {
			MG_DBG("remain : %d sects\n", remains);
			end_request(req, 1);
			if (remains > 0)
				req = elv_next_request(host->breq);
		}

		outb(MG_CMD_RD_CONF, (unsigned long)host->dev_base +
				MG_REG_COMMAND);
	}
}

static void mg_write(struct request *req)
{
	u32 remains, j;
	struct mg_host *host = req->rq_disk->private_data;

	remains = req->nr_sectors;

	if (mg_out(host, req->sector, req->nr_sectors, MG_CMD_WR, NULL) !=
			MG_ERR_NONE) {
		mg_bad_rw_intr(host);
		return;
	}


	MG_DBG("requested %d sects (from %ld), buffer=0x%p\n",
			remains, req->sector, req->buffer);
	while (remains) {
		if (mg_wait(host, MG_REG_STATUS_BIT_DATA_REQ,
					MG_TMAX_WAIT_WR_DRQ) != MG_ERR_NONE) {
			mg_bad_rw_intr(host);
			return;
		}
		for (j = 0; j < MG_SECTOR_SIZE >> 1; j++) {
			outw(*(u16 *)req->buffer,
					(unsigned long)host->dev_base +
					MG_BUFF_OFFSET + (j << 1));
			req->buffer += 2;
		}
		req->sector++;
		remains = --req->nr_sectors;
		--req->current_nr_sectors;

		if (req->current_nr_sectors <= 0) {
			MG_DBG("remain : %d sects\n", remains);
			end_request(req, 1);
			if (remains > 0)
				req = elv_next_request(host->breq);
		}

		outb(MG_CMD_WR_CONF, (unsigned long)host->dev_base +
				MG_REG_COMMAND);
	}
}

static void mg_read_intr(struct mg_host *host)
{
	u32 i;
	struct request *req;

	/* check status */
	do {
		i = inb((unsigned long)host->dev_base + MG_REG_STATUS);
		if (i & MG_REG_STATUS_BIT_BUSY)
			break;
		if (!MG_READY_OK(i))
			break;
		if (i & MG_REG_STATUS_BIT_DATA_REQ)
			goto ok_to_read;
	} while (0);
	mg_dump_status("mg_read_intr", i, host);
	mg_bad_rw_intr(host);
	mg_request(host->breq);
	return;

ok_to_read:
	/* get current segment of request */
	req = elv_next_request(host->breq);

	/* read 1 sector */
	for (i = 0; i < MG_SECTOR_SIZE >> 1; i++) {
		*(u16 *)req->buffer =
			inw((unsigned long)host->dev_base + MG_BUFF_OFFSET +
					(i << 1));
		req->buffer += 2;
	}

	/* manipulate request */
	MG_DBG("sector %ld, remaining=%ld, buffer=0x%p\n",
			req->sector, req->nr_sectors - 1, req->buffer);

	req->sector++;
	req->errors = 0;
	i = --req->nr_sectors;
	--req->current_nr_sectors;

	/* let know if current segment done */
	if (req->current_nr_sectors <= 0)
		end_request(req, 1);

	/* set handler if read remains */
	if (i > 0) {
		host->mg_do_intr = mg_read_intr;
		mod_timer(&host->timer, jiffies + 3 * HZ);
	}

	/* send read confirm */
	outb(MG_CMD_RD_CONF, (unsigned long)host->dev_base + MG_REG_COMMAND);

	/* goto next request */
	if (!i)
		mg_request(host->breq);
}

static void mg_write_intr(struct mg_host *host)
{
	u32 i, j;
	u16 *buff;
	struct request *req;

	/* get current segment of request */
	req = elv_next_request(host->breq);

	/* check status */
	do {
		i = inb((unsigned long)host->dev_base + MG_REG_STATUS);
		if (i & MG_REG_STATUS_BIT_BUSY)
			break;
		if (!MG_READY_OK(i))
			break;
		if ((req->nr_sectors <= 1) || (i & MG_REG_STATUS_BIT_DATA_REQ))
			goto ok_to_write;
	} while (0);
	mg_dump_status("mg_write_intr", i, host);
	mg_bad_rw_intr(host);
	mg_request(host->breq);
	return;

ok_to_write:
	/* manipulate request */
	req->sector++;
	i = --req->nr_sectors;
	--req->current_nr_sectors;
	req->buffer += MG_SECTOR_SIZE;

	/* let know if current segment or all done */
	if (!i || (req->bio && req->current_nr_sectors <= 0))
		end_request(req, 1);

	/* write 1 sector and set handler if remains */
	if (i > 0) {
		buff = (u16 *)req->buffer;
		for (j = 0; j < MG_STORAGE_BUFFER_SIZE >> 1; j++) {
			outw(*buff, (unsigned long)host->dev_base +
					MG_BUFF_OFFSET + (j << 1));
			buff++;
		}
		MG_DBG("sector %ld, remaining=%ld, buffer=0x%p\n",
				req->sector, req->nr_sectors, req->buffer);
		host->mg_do_intr = mg_write_intr;
		mod_timer(&host->timer, jiffies + 3 * HZ);
	}

	/* send write confirm */
	outb(MG_CMD_WR_CONF, (unsigned long)host->dev_base + MG_REG_COMMAND);

	if (!i)
		mg_request(host->breq);
}

void mg_times_out(unsigned long data)
{
	struct mg_host *host = (struct mg_host *)data;
	char *name;
	struct request *req;

	spin_lock_irq(&host->lock);

	req = elv_next_request(host->breq);
	if (!req)
		goto out_unlock;

	host->mg_do_intr = NULL;

	name = req->rq_disk->disk_name;
	printk(KERN_DEBUG "%s: timeout\n", name);

	host->error = MG_ERR_TIMEOUT;
	mg_bad_rw_intr(host);

	mg_request(host->breq);
out_unlock:
	spin_unlock_irq(&host->lock);
}

static void mg_request_poll(struct request_queue *q)
{
	struct request *req;
	struct mg_host *host;

	while ((req = elv_next_request(q)) != NULL) {
		host = req->rq_disk->private_data;
		if (blk_fs_request(req)) {
			switch (rq_data_dir(req)) {
			case READ:
				mg_read(req);
				break;
			case WRITE:
				mg_write(req);
				break;
			default:
				printk(KERN_WARNING "%s:%d unknown command\n",
						__func__, __LINE__);
				end_request(req, 0);
				break;
			}
		}
	}
}

static unsigned int mg_issue_req(struct request *req,
		struct mg_host *host,
		unsigned int sect_num,
		unsigned int sect_cnt)
{
	u16 *buff;
	u32 i;

	switch (rq_data_dir(req)) {
	case READ:
		if (mg_out(host, sect_num, sect_cnt, MG_CMD_RD, &mg_read_intr)
				!= MG_ERR_NONE) {
			mg_bad_rw_intr(host);
			return host->error;
		}
		break;
	case WRITE:
		/* TODO : handler */
		outb(MG_REG_CTRL_INTR_DISABLE,
				(unsigned long)host->dev_base +
				MG_REG_DRV_CTRL);
		if (mg_out(host, sect_num, sect_cnt, MG_CMD_WR, &mg_write_intr)
				!= MG_ERR_NONE) {
			mg_bad_rw_intr(host);
			return host->error;
		}
		del_timer(&host->timer);
		mg_wait(host, MG_REG_STATUS_BIT_DATA_REQ, MG_TMAX_WAIT_WR_DRQ);
		outb(MG_REG_CTRL_INTR_ENABLE, (unsigned long)host->dev_base +
				MG_REG_DRV_CTRL);
		if (host->error) {
			mg_bad_rw_intr(host);
			return host->error;
		}
		buff = (u16 *)req->buffer;
		for (i = 0; i < MG_SECTOR_SIZE >> 1; i++) {
			outw(*buff, (unsigned long)host->dev_base +
					MG_BUFF_OFFSET + (i << 1));
			buff++;
		}
		mod_timer(&host->timer, jiffies + 3 * HZ);
		outb(MG_CMD_WR_CONF, (unsigned long)host->dev_base +
				MG_REG_COMMAND);
		break;
	default:
		printk(KERN_WARNING "%s:%d unknown command\n",
				__func__, __LINE__);
		end_request(req, 0);
		break;
	}
	return MG_ERR_NONE;
}

/* This function also called from IRQ context */
static void mg_request(struct request_queue *q)
{
	struct request *req;
	struct mg_host *host;
	u32 sect_num, sect_cnt;

	while (1) {
		req = elv_next_request(q);
		if (!req)
			return;

		host = req->rq_disk->private_data;

		/* check unwanted request call */
		if (host->mg_do_intr)
			return;

		del_timer(&host->timer);

		sect_num = req->sector;
		/* deal whole segments */
		sect_cnt = req->nr_sectors;

		/* sanity check */
		if (sect_num >= get_capacity(req->rq_disk) ||
				((sect_num + sect_cnt) >
				 get_capacity(req->rq_disk))) {
			printk(KERN_WARNING
					"%s: bad access: sector=%d, count=%d\n",
					req->rq_disk->disk_name,
					sect_num, sect_cnt);
			end_request(req, 0);
			continue;
		}

		if (!blk_fs_request(req))
			return;

		if (!mg_issue_req(req, host, sect_num, sect_cnt))
			return;
	}
}

static int mg_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct mg_host *host = bdev->bd_disk->private_data;

	geo->cylinders = (unsigned short)host->cyls;
	geo->heads = (unsigned char)host->heads;
	geo->sectors = (unsigned char)host->sectors;
	return 0;
}

static struct block_device_operations mg_disk_ops = {
	.getgeo = mg_getgeo
};

static int mg_suspend(struct platform_device *plat_dev, pm_message_t state)
{
	struct mg_drv_data *prv_data = plat_dev->dev.platform_data;
	struct mg_host *host = prv_data->host;

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD))
		return -EIO;

	if (!prv_data->use_polling)
		outb(MG_REG_CTRL_INTR_DISABLE,
				(unsigned long)host->dev_base +
				MG_REG_DRV_CTRL);

	outb(MG_CMD_SLEEP, (unsigned long)host->dev_base + MG_REG_COMMAND);
	/* wait until mflash deep sleep */
	msleep(1);

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD)) {
		if (!prv_data->use_polling)
			outb(MG_REG_CTRL_INTR_ENABLE,
					(unsigned long)host->dev_base +
					MG_REG_DRV_CTRL);
		return -EIO;
	}

	return 0;
}

static int mg_resume(struct platform_device *plat_dev)
{
	struct mg_drv_data *prv_data = plat_dev->dev.platform_data;
	struct mg_host *host = prv_data->host;

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD))
		return -EIO;

	outb(MG_CMD_WAKEUP, (unsigned long)host->dev_base + MG_REG_COMMAND);
	/* wait until mflash wakeup */
	msleep(1);

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD))
		return -EIO;

	if (!prv_data->use_polling)
		outb(MG_REG_CTRL_INTR_ENABLE, (unsigned long)host->dev_base +
				MG_REG_DRV_CTRL);

	return 0;
}

static int mg_probe(struct platform_device *plat_dev)
{
	struct mg_host *host;
	struct resource *rsc;
	struct mg_drv_data *prv_data = plat_dev->dev.platform_data;
	int err = 0;

	if (!prv_data) {
		printk(KERN_ERR	"%s:%d fail (no driver_data)\n",
				__func__, __LINE__);
		err = -EINVAL;
		goto probe_err;
	}

	/* alloc mg_host */
	host = kzalloc(sizeof(struct mg_host), GFP_KERNEL);
	if (!host) {
		printk(KERN_ERR "%s:%d fail (no memory for mg_host)\n",
				__func__, __LINE__);
		err = -ENOMEM;
		goto probe_err;
	}
	host->major = MG_DISK_MAJ;

	/* link each other */
	prv_data->host = host;
	host->dev = &plat_dev->dev;

	/* io remap */
	rsc = platform_get_resource(plat_dev, IORESOURCE_MEM, 0);
	if (!rsc) {
		printk(KERN_ERR "%s:%d platform_get_resource fail\n",
				__func__, __LINE__);
		err = -EINVAL;
		goto probe_err_2;
	}
	host->dev_base = ioremap(rsc->start , rsc->end + 1);
	if (!host->dev_base) {
		printk(KERN_ERR "%s:%d ioremap fail\n",
				__func__, __LINE__);
		err = -EIO;
		goto probe_err_2;
	}
	MG_DBG("dev_base = 0x%x\n", (u32)host->dev_base);

	/* get reset pin */
	rsc = platform_get_resource_byname(plat_dev, IORESOURCE_IO,
			MG_RST_PIN);
	if (!rsc) {
		printk(KERN_ERR "%s:%d get reset pin fail\n",
				__func__, __LINE__);
		err = -EIO;
		goto probe_err_3;
	}
	host->rst = rsc->start;

	/* init rst pin */
	err = gpio_request(host->rst, MG_RST_PIN);
	if (err)
		goto probe_err_3;
	gpio_direction_output(host->rst, 1);

	/* reset out pin */
	if (!(prv_data->dev_attr & MG_DEV_MASK))
		goto probe_err_3a;

	if (prv_data->dev_attr != MG_BOOT_DEV) {
		rsc = platform_get_resource_byname(plat_dev, IORESOURCE_IO,
				MG_RSTOUT_PIN);
		if (!rsc) {
			printk(KERN_ERR "%s:%d get reset-out pin fail\n",
					__func__, __LINE__);
			err = -EIO;
			goto probe_err_3a;
		}
		host->rstout = rsc->start;
		err = gpio_request(host->rstout, MG_RSTOUT_PIN);
		if (err)
			goto probe_err_3a;
		gpio_direction_input(host->rstout);
	}

	/* disk reset */
	if (prv_data->dev_attr == MG_STORAGE_DEV) {
		/* If POR seq. not yet finised, wait */
		err = mg_wait_rstout(host->rstout, MG_TMAX_RSTOUT);
		if (err)
			goto probe_err_3b;
		err = mg_disk_init(host);
		if (err) {
			printk(KERN_ERR "%s:%d fail (err code : %d)\n",
					__func__, __LINE__, err);
			err = -EIO;
			goto probe_err_3b;
		}
	}

	/* get irq resource */
	if (!prv_data->use_polling) {
		host->irq = platform_get_irq(plat_dev, 0);
		if (host->irq == -ENXIO) {
			err = host->irq;
			goto probe_err_3b;
		}
		err = request_irq(host->irq, mg_irq,
				IRQF_DISABLED | IRQF_TRIGGER_RISING,
				MG_DEV_NAME, host);
		if (err) {
			printk(KERN_ERR "%s:%d fail (request_irq err=%d)\n",
					__func__, __LINE__, err);
			goto probe_err_3b;
		}

	}

	/* get disk id */
	err = mg_get_disk_id(host);
	if (err) {
		printk(KERN_ERR "%s:%d fail (err code : %d)\n",
				__func__, __LINE__, err);
		err = -EIO;
		goto probe_err_4;
	}

	err = register_blkdev(host->major, MG_DISK_NAME);
	if (err < 0) {
		printk(KERN_ERR "%s:%d register_blkdev fail (err code : %d)\n",
				__func__, __LINE__, err);
		goto probe_err_4;
	}
	if (!host->major)
		host->major = err;

	spin_lock_init(&host->lock);

	if (prv_data->use_polling)
		host->breq = blk_init_queue(mg_request_poll, &host->lock);
	else
		host->breq = blk_init_queue(mg_request, &host->lock);

	if (!host->breq) {
		err = -ENOMEM;
		printk(KERN_ERR "%s:%d (blk_init_queue) fail\n",
				__func__, __LINE__);
		goto probe_err_5;
	}

	/* mflash is random device, thanx for the noop */
	elevator_exit(host->breq->elevator);
	err = elevator_init(host->breq, "noop");
	if (err) {
		printk(KERN_ERR "%s:%d (elevator_init) fail\n",
				__func__, __LINE__);
		goto probe_err_6;
	}
	blk_queue_max_sectors(host->breq, MG_MAX_SECTS);
	blk_queue_hardsect_size(host->breq, MG_SECTOR_SIZE);

	init_timer(&host->timer);
	host->timer.function = mg_times_out;
	host->timer.data = (unsigned long)host;

	host->gd = alloc_disk(MG_DISK_MAX_PART);
	if (!host->gd) {
		printk(KERN_ERR "%s:%d (alloc_disk) fail\n",
				__func__, __LINE__);
		err = -ENOMEM;
		goto probe_err_7;
	}
	host->gd->major = host->major;
	host->gd->first_minor = 0;
	host->gd->fops = &mg_disk_ops;
	host->gd->queue = host->breq;
	host->gd->private_data = host;
	sprintf(host->gd->disk_name, MG_DISK_NAME"a");

	set_capacity(host->gd, host->n_sectors);

	add_disk(host->gd);

	return err;

probe_err_7:
	del_timer_sync(&host->timer);
probe_err_6:
	blk_cleanup_queue(host->breq);
probe_err_5:
	unregister_blkdev(MG_DISK_MAJ, MG_DISK_NAME);
probe_err_4:
	if (!prv_data->use_polling)
		free_irq(host->irq, host);
probe_err_3b:
	gpio_free(host->rstout);
probe_err_3a:
	gpio_free(host->rst);
probe_err_3:
	iounmap(host->dev_base);
probe_err_2:
	kfree(host);
probe_err:
	return err;
}

static int mg_remove(struct platform_device *plat_dev)
{
	struct mg_drv_data *prv_data = plat_dev->dev.platform_data;
	struct mg_host *host = prv_data->host;
	int err = 0;

	/* delete timer */
	del_timer_sync(&host->timer);

	/* remove disk */
	if (host->gd) {
		del_gendisk(host->gd);
		put_disk(host->gd);
	}
	/* remove queue */
	if (host->breq)
		blk_cleanup_queue(host->breq);

	/* unregister blk device */
	unregister_blkdev(host->major, MG_DISK_NAME);

	/* free irq */
	if (!prv_data->use_polling)
		free_irq(host->irq, host);

	/* free reset-out pin */
	if (prv_data->dev_attr != MG_BOOT_DEV)
		gpio_free(host->rstout);

	/* free rst pin */
	if (host->rst)
		gpio_free(host->rst);

	/* unmap io */
	if (host->dev_base)
		iounmap(host->dev_base);

	/* free mg_host */
	kfree(host);

	return err;
}

static struct platform_driver mg_disk_driver = {
	.probe = mg_probe,
	.remove = mg_remove,
	.suspend = mg_suspend,
	.resume = mg_resume,
	.driver = {
		.name = MG_DEV_NAME,
		.owner = THIS_MODULE,
	}
};

/****************************************************************************
 *
 * Module stuff
 *
 ****************************************************************************/

static int __init mg_init(void)
{
	printk(KERN_INFO "mGine mflash driver, (c) 2008 mGine Co.\n");
	return platform_driver_register(&mg_disk_driver);
}

static void __exit mg_exit(void)
{
	printk(KERN_INFO "mflash driver : bye bye\n");
	platform_driver_unregister(&mg_disk_driver);
}

module_init(mg_init);
module_exit(mg_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("unsik Kim <donari75@gmail.com>");
MODULE_DESCRIPTION("mGine m[g]flash device driver");
